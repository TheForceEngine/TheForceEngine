#include "editorTexture.h"
#include "editorColormap.h"
#include "editorAsset.h"
#include <TFE_Editor/editor.h>
#include <TFE_DarkForces/mission.h>
#include <TFE_System/system.h>
#include <TFE_Archive/archive.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Renderer/rcommon.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include <map>

namespace TFE_Editor
{
	typedef std::vector<EditorTexture> TextureList;
	static TextureList s_textureList;
	static std::vector<u8> s_rawBuffer;

	void freeTexture(const char* name)
	{
		const size_t count = s_textureList.size();
		EditorTexture* texture = s_textureList.data();
		for (size_t i = 0; i < count; i++, texture++)
		{
			if (strcasecmp(texture->name, name) == 0)
			{
				for (u32 f = 0; f < texture->frameCount; f++)
				{
					TFE_RenderBackend::freeTexture(texture->frames[f]);
				}
				texture->frames.clear();
				*texture = EditorTexture{};
				break;
			}
		}
	}

	void freeCachedTextures()
	{
		const size_t count = s_textureList.size();
		const EditorTexture* texture = s_textureList.data();
		for (size_t i = 0; i < count; i++, texture++)
		{
			for (u32 f = 0; f < texture->frameCount; f++)
			{
				TFE_RenderBackend::freeTexture(texture->frames[f]);
			}
		}
		s_textureList.clear();
	}

	s32 allocateTexture(const char* name)
	{
		s32 index = (s32)s_textureList.size();
		s_textureList.emplace_back();
		return index;
	}

	s32 getTextureIndexByName(const char* name)
	{
		const size_t count = s_textureList.size();
		const EditorTexture* texture = s_textureList.data();
		for (size_t i = 0; i < count; i++, texture++)
		{
			if (strcasecmp(texture->name, name) == 0)
			{
				return s32(i);
			}
		}
		return -1;
	}
		
	TextureGpu* loadBmFrame(u32 width, u32 height, const u8* image, const u32* palette)
	{
		buildIdentityTable();

		WorkBuffer& buffer = getWorkBuffer();
		buffer.resize(width * height * 4);
		u32* imageBuffer = (u32*)buffer.data();
		const u8* srcImage = image;

		for (u32 x = 0; x < width; x++, srcImage += height)
		{
			const u8* column = srcImage;
			for (u32 y = 0; y < height; y++)
			{
				u8 palIndex = column[y];
				imageBuffer[y*width + x] = palette[s_remapTable[palIndex]];
			}
		}
		return TFE_RenderBackend::createTexture(width, height, imageBuffer);
	}

	TextureGpu* loadRawFrame(u32 width, u32 height, const u32* image)
	{
		s_rawBuffer.resize(width * height * 4);
		u32* imageBuffer = (u32*)s_rawBuffer.data();
		memset(imageBuffer, 0, width * height * 4);
		const u32* srcImage = image;

		for (u32 y = 0; y < height; y++)
		{
			memcpy(&imageBuffer[y*width], &srcImage[(height - y - 1)*width], width * 4);
		}
		return TFE_RenderBackend::createTexture(width, height, imageBuffer);
	}

	EditorTexture* getTextureData(u32 index)
	{
		if (index >= s_textureList.size()) { return nullptr; }
		return &s_textureList[index];
	}

	s32 loadEditorTexture(SourceType type, Archive* archive, const char* filename, const char* filePath, const u32* palette, s32 palIndex, s32 id)
	{
		if (!archive && !filename) { return -1; }

		WorkBuffer& buffer = getWorkBuffer();
		size_t len;

		if (archive)
		{
			if (!archive->openFile(filename))
			{
				return -1;
			}
			len = archive->getFileLength();
			if (!len)
			{
				archive->closeFile();
				return -1;
			}
			buffer.resize(len);
			archive->readFile(buffer.data(), len);
			archive->closeFile();
		}
		else
		{
			FileStream file;
			if (!filePath || !file.open(filePath, Stream::MODE_READ))
			{
				return -1;
			}

			len = file.getSize();
			buffer.resize(len);
			file.readBuffer(buffer.data(), len);
			file.close();
		}

		if (type == TEX_BM)
		{
			bool result = false;
			TextureData* texData = bitmap_loadFromMemory(buffer.data(), len, 1);
			if (texData)
			{
				// What about animated textures?
				if (texData->uvWidth == BM_ANIMATED_TEXTURE)
				{
					u8 frameRate = texData->image[0];
					u8 animatedId = texData->image[1];
					u8 frameCount = (u8)texData->uvHeight;
					if (animatedId != 2)
					{
						free(texData);
						return -1;
					}

					if (id < 0) { id = allocateTexture(filename); }
					s_textureList[id].frameCount = frameCount;

					const u32* textureOffsets = (u32*)(texData->image + 2);
					const u8* base = texData->image + 2;
					s_textureList[id].frames.resize(frameCount);
					for (s32 i = 0; i < frameCount; i++)
					{
						const TextureData* frame = (TextureData*)(base + textureOffsets[i]);
						TextureData outFrame;

						// Allocate an image buffer since everything no longer fits nicely.
						u8* image = (u8*)frame + 0x1c;

						// We have to make sure the structure offsets line up with DOS...
						outFrame.flags = *((u8*)frame + 0x18);
						outFrame.compressed = *((u8*)frame + 0x19);
						outFrame.animIndex = 0;
						outFrame.frameIdx = i;
						outFrame.animPtr = nullptr;
						outFrame.animSetup = 1;
						outFrame.palIndex = 1;
						outFrame.columns = nullptr;
						s_textureList[id].frames[i] = loadBmFrame(frame->width, frame->height, image, palette);

						if (i == 0)
						{
							s_textureList[id].width = frame->width;
							s_textureList[id].height = frame->height;
						}
					}
				}
				else
				{
					if (id < 0) { id = allocateTexture(filename); }
					s_textureList[id].width = texData->width;
					s_textureList[id].height = texData->height;
					s_textureList[id].frameCount = 1;

					s_textureList[id].frames.resize(1);
					s_textureList[id].frames[0] = loadBmFrame(texData->width, texData->height, texData->image, palette);
				}
				strcpy(s_textureList[id].name, filename);
				s_textureList[id].paletteIndex = palIndex;
				s_textureList[id].lightLevel = 32;
			}
			if (texData) { free(texData->image); }
			free(texData);
		}
		else if (type == TEX_RAW)
		{
			char baseFilename[TFE_MAX_PATH];
			FileUtil::replaceExtension(filename, "bm", baseFilename);
			s32 baseIndex = getTextureIndexByName(baseFilename);
			if (baseIndex >= 0)
			{
				s32 width  = s_textureList[baseIndex].width * 2;
				s32 height = s_textureList[baseIndex].height * 2;
				s32 frameCount = s_textureList[baseIndex].frameCount;

				// Raw image data.
				u32* data = (u32*)buffer.data();

				if (id < 0) { id = allocateTexture(filename); }
				s_textureList[id].width  = width;
				s_textureList[id].height = height;
				s_textureList[id].frameCount = frameCount;
				s_textureList[id].frames.resize(frameCount);
				for (s32 i = 0; i < frameCount; i++)
				{
					s_textureList[id].frames[i] = loadRawFrame(width, height, data);
					data += width * height;
				}
				strcpy(s_textureList[id].name, filename);
				s_textureList[id].paletteIndex = palIndex;
				s_textureList[id].lightLevel = 32;
			}
		}
		return id;
	}

	bool loadEditorTextureLit(SourceType type, Archive* archive, const u32* palette, s32 palIndex, const u8* colormap, s32 lightLevel, u32 index)
	{
		EditorTexture* texture = getTextureData(index);

		s_remapTable = lightLevel == 32 ? s_identityTable : &colormap[lightLevel * 256];
		char name[TFE_MAX_PATH];
		strcpy(name, texture->name);
		freeTexture(name);

		bool result = loadEditorTexture(type, archive, name, nullptr, palette, palIndex, (s32)index);
		if (result)
		{
			texture->lightLevel = lightLevel;
		}
		s_remapTable = s_identityTable;

		return result;
	}

	s32 loadPaletteAsTexture(Archive* archive, const char* filename, const char* filePath, const u8* colormapData, s32 id)
	{
		if (!archive && !filename) { return -1; }

		WorkBuffer& buffer = getWorkBuffer();
		size_t len;

		if (archive)
		{
			if (!archive->openFile(filename))
			{
				return -1;
			}
			len = archive->getFileLength();
			if (!len)
			{
				archive->closeFile();
				return -1;
			}
			buffer.resize(len);
			archive->readFile(buffer.data(), len);
			archive->closeFile();
		}
		else
		{
			FileStream file;
			if (!filePath || !file.open(filePath, Stream::MODE_READ))
			{
				return -1;
			}
			len = file.getSize();
			buffer.resize(len);
			file.readBuffer(buffer.data(), len);
			file.close();
		}

		if (id < 0) { id = allocateTexture(filename); }
		strcpy(s_textureList[id].name, filename);
		s_textureList[id].width = 16;
		s_textureList[id].height = 16;
		s_textureList[id].frameCount = 1;
		s_textureList[id].curFrame = 0;
		s_textureList[id].lightLevel = 32;
		s_textureList[id].paletteIndex = 0;

		u32 image[16 * 16];
		const u8* srcPal = buffer.data();
		for (s32 i = 0; i < 256; i++, srcPal += 3)
		{
			s32 x = i & 15;
			s32 y = 15 - (i >> 4);

			image[(y << 4) + x] = 0xff000000 | CONV_6bitTo8bit(srcPal[0]) | (CONV_6bitTo8bit(srcPal[1]) << 8) | (CONV_6bitTo8bit(srcPal[2]) << 16);
		}
		s_textureList[id].frames.resize(colormapData ? 2 : 1);
		s_textureList[id].frames[0] = TFE_RenderBackend::createTexture(16, 16, image);

		// Create a second frame to hold the colormap data if it exists.
		if (colormapData)
		{
			s_textureList[id].frameCount = 2;
			u32 colormapTrueColor[256 * 32];
			srcPal = buffer.data();
			for (s32 y = 0; y < 32; y++)
			{
				const u8* ramp = &colormapData[y << 8];
				for (s32 x = 0; x < 256; x++)
				{
					const u8* color = &srcPal[ramp[x] * 3];
					colormapTrueColor[y * 256 + x] = 0xff000000 |
						CONV_6bitTo8bit(color[0]) | (CONV_6bitTo8bit(color[1]) << 8) | (CONV_6bitTo8bit(color[2]) << 16);
				}
			}
			s_textureList[id].frames[1] = TFE_RenderBackend::createTexture(256, 32, colormapTrueColor);
		}
		return id;
	}
}