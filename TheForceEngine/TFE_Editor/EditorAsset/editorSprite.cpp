#include "editorSprite.h"
#include "editorColormap.h"
#include <TFE_Editor/editor.h>
#include <TFE_Asset/spriteAsset_Jedi.h>
#include <TFE_DarkForces/mission.h>
#include <TFE_System/system.h>
#include <TFE_Archive/archive.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Renderer/rcommon.h>
#include <algorithm>
#include <climits>
#include <cstring>
#include <vector>
#include <map>

namespace TFE_Editor
{
	typedef std::vector<EditorSprite> SpriteList;
	static SpriteList s_spriteList;
	static std::map<WaxCell*, s32> s_waxDataMap;
	static std::vector<WaxCell*> s_waxDataList;
	
	void freeSprite(const char* name)
	{
		const size_t count = s_spriteList.size();
		EditorSprite* sprite = s_spriteList.data();
		for (size_t i = 0; i < count; i++, sprite++)
		{
			if (strcasecmp(sprite->name, name) == 0)
			{
				TFE_RenderBackend::freeTexture(sprite->texGpu);
				*sprite = EditorSprite{};
				break;
			}
		}
	}

	void freeCachedSprites()
	{
		const size_t count = s_spriteList.size();
		const EditorSprite* sprite = s_spriteList.data();
		for (size_t i = 0; i < count; i++, sprite++)
		{
			TFE_RenderBackend::freeTexture(sprite->texGpu);
		}
		s_spriteList.clear();
	}

	s32 allocateSprite(const char* name)
	{
		s32 index = (s32)s_spriteList.size();
		s_spriteList.emplace_back();
		return index;
	}

	bool findWaxCellInMap(WaxCell* cell, s32& id)
	{
		std::map<WaxCell*, s32>::iterator iCell = s_waxDataMap.find(cell);
		if (iCell != s_waxDataMap.end())
		{
			id = iCell->second;
			return true;
		}
		return false;
	}

	void insertWaxCellIntoMap(WaxCell* cell, s32 id)
	{
		s_waxDataMap[cell] = id;
		s_waxDataList.push_back(cell);
	}

	void writeCellToImage(s32 u, s32 v, s32 w, s32 h, s32 stride, const void* basePtr, const WaxCell* cell, const u32* palette, u32* imageBuffer)
	{
		u32* outImage = &imageBuffer[v * stride];

		const u8* imageData = (u8*)cell + sizeof(WaxCell);
		const u8* image = (cell->compressed == 1) ? imageData + (cell->sizeX * sizeof(u32)) : imageData;

		u8 columnWorkBuffer[WAX_DECOMPRESS_SIZE];
		const u32* columnOffset = (u32*)((u8*)basePtr + cell->columnOffset);

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
				outImage[y*stride + x + u] = column[y] ? palette[s_remapTable[column[y]]] : 0;
			}
		}
	}

	s32 loadEditorSprite(SpriteSourceType type, Archive* archive, const char* filename, const char* filePath, const u32* palette, s32 palIndex, s32 id)
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
		

		if (type == SPRITE_WAX)
		{
			JediWax* wax = TFE_Sprite_Jedi::loadWaxFromMemory(buffer.data(), len, false);
			if (!wax) { return -1; }

			// Allocate
			if (id < 0) { id = allocateSprite(filename); }
			EditorSprite* outSprite = &s_spriteList[id];
			outSprite->anim.clear();
			outSprite->frame.clear();
			outSprite->cell.clear();
			outSprite->texGpu = nullptr;

			// First gather all of the cells.
			s_waxDataMap.clear();
			s_waxDataList.clear();

			outSprite->rect[0] =  INT_MAX;
			outSprite->rect[1] =  INT_MAX;
			outSprite->rect[2] = -INT_MAX;
			outSprite->rect[3] = -INT_MAX;

			for (s32 animId = 0; animId < wax->animCount; animId++)
			{
				WaxAnim* anim = WAX_AnimPtr(wax, animId);
				if (!anim) { continue; }
								
				SpriteAnim outAnim;
				outAnim.frameRate   = anim->frameRate;
				outAnim.frameCount  = anim->frameCount;
				outAnim.worldWidth  = fixed16ToFloat(anim->worldWidth);
				outAnim.worldHeight = fixed16ToFloat(anim->worldHeight);
				for (s32 v = 0; v < WAX_MAX_VIEWS; v++)
				{
					WaxView* view = WAX_ViewPtr(wax, anim, v);
					memset(outAnim.views[v].frameIndex, 0, sizeof(s32) * 32);
					if (!view) { continue; }

					for (s32 f = 0; f < anim->frameCount; f++)
					{
						outAnim.views[v].frameIndex[f] = (s32)outSprite->frame.size();

						WaxFrame* frame = WAX_FramePtr(wax, view, f);
						WaxCell* cell = frame ? WAX_CellPtr(wax, frame) : nullptr;

						s32 id = -1;
						if (cell && !findWaxCellInMap(cell, id))
						{
							id = (s32)s_waxDataList.size();
							insertWaxCellIntoMap(cell, id);
						}
						else if (!cell)
						{
							id = 0;
						}

						SpriteFrame outFrame = { 0 };
						outFrame.cellIndex = id;
						outFrame.flip = frame ? frame->flip : 0;
						outFrame.offsetX = frame ? frame->offsetX : 0;
						outFrame.offsetY = frame ? frame->offsetY : 0;
						outFrame.widthWS  = frame ? fixed16ToFloat(frame->widthWS) : 0;
						outFrame.heightWS = frame ? fixed16ToFloat(frame->heightWS) : 0;
						outSprite->frame.push_back(outFrame);

						if (frame)
						{
							outSprite->rect[0] = min(outSprite->rect[0], frame->offsetX);
							outSprite->rect[1] = min(outSprite->rect[1], frame->offsetY);
							outSprite->rect[2] = max(outSprite->rect[2], frame->offsetX + cell->sizeX);
							outSprite->rect[3] = max(outSprite->rect[3], frame->offsetY + cell->sizeY);
						}
					}
				}
				outSprite->anim.push_back(outAnim);
			}

			// Now pack the cells into the texture...
			const size_t cellCount = s_waxDataList.size();
			WaxCell** waxCell = s_waxDataList.data();
			s32 totalWidth = 0, maxHeight = 0;
			for (size_t c = 0; c < cellCount; c++)
			{
				totalWidth += waxCell[c]->sizeX + 2;
				maxHeight = max(maxHeight, waxCell[c]->sizeY + 2);
			}

			// Super basic packing.
			s32 rows = (totalWidth + 4095) / 4096;
			s32 texW = totalWidth <= 4096 ? totalWidth : 4096;
			s32 texH = min(8192, (rows + 1) * maxHeight);

			// make sure the texture size is divisible by 4.
			texW = ((texW + 3) >> 2) << 2;
			texH = ((texH + 3) >> 2) << 2;

			WorkBuffer& buffer = getWorkBuffer();
			buffer.resize(texW * texH * 4);
			u32* imageBuffer = (u32*)buffer.data();
			memset(imageBuffer, 0, texW * texH * 4);

			s32 u = 0, v = 0;
			for (size_t c = 0; c < cellCount; c++)
			{
				if (u + waxCell[c]->sizeX >= 4096)
				{
					u = 0;
					v += maxHeight;
				}

				SpriteCell outCell;
				outCell.u = u;
				outCell.v = v;
				outCell.w = waxCell[c]->sizeX;
				outCell.h = waxCell[c]->sizeY;
				outSprite->cell.push_back(outCell);

				writeCellToImage(u, v, waxCell[c]->sizeX, waxCell[c]->sizeY, texW, wax, waxCell[c], palette, imageBuffer);
				u += waxCell[c]->sizeX + 2;
			}

			outSprite->texGpu = TFE_RenderBackend::createTexture(texW, texH, imageBuffer);
			outSprite->paletteIndex = palIndex;
			outSprite->lightLevel = 32;
			strcpy(outSprite->name, filename);

			free(wax);
		}
		return id;
	}
				
	bool loadEditorSpriteLit(SpriteSourceType type, Archive* archive, const u32* palette, s32 palIndex, const u8* colormap, s32 lightLevel, u32 index)
	{
		EditorSprite* sprite = getSpriteData(index);

		s_remapTable = lightLevel == 32 ? s_identityTable : &colormap[lightLevel * 256];
		char name[TFE_MAX_PATH];
		strcpy(name, sprite->name);
		freeSprite(name);

		bool result = loadEditorSprite(type, archive, name, nullptr, palette, palIndex, (s32)index);
		if (result)
		{
			sprite->lightLevel = lightLevel;
		}
		s_remapTable = s_identityTable;

		return result;
	}

	EditorSprite* getSpriteData(u32 index)
	{
		if (index >= s_spriteList.size()) { return nullptr; }
		return &s_spriteList[index];
	}
}