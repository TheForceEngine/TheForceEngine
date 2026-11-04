#include "editorFrame.h"
#include "editorColormap.h"
#include <TFE_Editor/editor.h>
#include <TFE_DarkForces/mission.h>
#include <TFE_System/system.h>
#include <TFE_Archive/archive.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Renderer/rcommon.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include <map>

namespace TFE_Editor
{
	typedef std::vector<EditorFrame> FrameList;
	static FrameList s_frameList;
	
	void freeFrame(const char* name)
	{
		const size_t count = s_frameList.size();
		EditorFrame* frame = s_frameList.data();
		for (size_t i = 0; i < count; i++, frame++)
		{
			if (strcasecmp(frame->name, name) == 0)
			{
				TFE_RenderBackend::freeTexture(frame->texGpu);
				*frame = EditorFrame{};
				break;
			}
		}
	}

	void freeCachedFrames()
	{
		const size_t count = s_frameList.size();
		const EditorFrame* frame = s_frameList.data();
		for (size_t i = 0; i < count; i++, frame++)
		{
			TFE_RenderBackend::freeTexture(frame->texGpu);
		}
		s_frameList.clear();
	}

	s32 allocateFrame(const char* name)
	{
		s32 index = (s32)s_frameList.size();
		s_frameList.emplace_back();
		return index;
	}

	s32 loadEditorFrame(FrameSourceType type, Archive* archive, const char* filename, const char* filePath, const u32* palette, s32 palIndex, s32 id)
	{
		if (!archive && !filename) { return -1; }
		buildIdentityTable();

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

		if (type == FRAME_FME)
		{
			WaxFrame* frameData = TFE_Sprite_Jedi::loadFrameFromMemory(buffer.data(), len, false);
			if (!frameData)
			{
				return -1;
			}

			const WaxCell* cell = WAX_CellPtr(frameData, frameData);
			WorkBuffer& buffer = getWorkBuffer();
			buffer.resize(cell->sizeX * cell->sizeY * 4);
			u32* imageBuffer = (u32*)buffer.data();

			u8* imageData = (u8*)cell + sizeof(WaxCell);
			u8* image = (cell->compressed == 1) ? imageData + (cell->sizeX * sizeof(u32)) : imageData;

			u8 columnWorkBuffer[WAX_DECOMPRESS_SIZE];
			const u32* columnOffset = (u32*)((u8*)frameData + cell->columnOffset);

			for (s32 x = 0; x < cell->sizeX; x++)
			{
				u8* column = (u8*)image + columnOffset[x];
				if (cell->compressed)
				{
					const u8* colPtr = (u8*)cell + columnOffset[x];
					sprite_decompressColumn(colPtr, columnWorkBuffer, cell->sizeY);
					column = columnWorkBuffer;
				}

				for (s32 y = 0; y < cell->sizeY; y++)
				{
					imageBuffer[y*cell->sizeX + x] = column[y] ? palette[s_remapTable[column[y]]] : 0;
				}
			}

			if (id < 0) { id = allocateFrame(filename); }
			s_frameList[id].width = cell->sizeX;
			s_frameList[id].height = cell->sizeY;
			s_frameList[id].worldWidth = fixed16ToFloat(frameData->widthWS);
			s_frameList[id].worldHeight = fixed16ToFloat(frameData->heightWS);
			s_frameList[id].offsetX = (f32)frameData->offsetX;
			s_frameList[id].offsetY = (f32)frameData->offsetY;
			strcpy(s_frameList[id].name, filename);
			s_frameList[id].paletteIndex = palIndex;
			s_frameList[id].lightLevel = 32;
			s_frameList[id].texGpu = TFE_RenderBackend::createTexture(cell->sizeX, cell->sizeY, imageBuffer);
			free(frameData);
		}
		return id;
	}

	bool loadEditorFrameLit(FrameSourceType type, Archive* archive, const u32* palette, s32 palIndex, const u8* colormap, s32 lightLevel, u32 index)
	{
		EditorFrame* frame = getFrameData(index);

		s_remapTable = lightLevel == 32 ? s_identityTable : &colormap[lightLevel * 256];
		char name[TFE_MAX_PATH];
		strcpy(name, frame->name);
		freeFrame(name);

		bool result = loadEditorFrame(type, archive, name, nullptr, palette, palIndex, (s32)index);
		if (result)
		{
			frame->lightLevel = lightLevel;
		}
		s_remapTable = s_identityTable;

		return result;
	}

	EditorFrame* getFrameData(u32 index)
	{
		if (index >= s_frameList.size()) { return nullptr; }
		return &s_frameList[index];
	}
}