#pragma once
//////////////////////////////////////////////////////////////////////
// The Force Engine Editor
// A system built to view and edit Dark Forces data files.
// The viewing aspect needs to be put in place at the beginning
// in order to properly test elements in isolation without having
// to "play" the game as intended.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_Archive/archive.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <vector>

namespace TFE_Editor
{
	struct EditorTexture
	{
		std::vector<TextureGpu*> frames;
		u32  width = 0;
		u32  height = 0;
		u32  frameCount = 1;
		s32  curFrame = 0;
		u8   paletteIndex = 0;
		u8   lightLevel = 32;
		u8   pad[2];
		char name[64] = "";
	};
	enum SourceType
	{
		TEX_BM = 0,
		TEX_PCX,
		TEX_RAW,
		TEX_COUNT
	};
		
	void freeCachedTextures();
	void freeTexture(const char* name);

	s32 loadEditorTexture(SourceType type, Archive* archive, const char* filename, const char* filePath, const u32* palette, s32 palIndex, s32 id = -1);
	s32 loadPaletteAsTexture(Archive* archive, const char* filename, const char* filePath, const u8* colormapData, s32 id = -1);
	bool loadEditorTextureLit(SourceType type, Archive* archive, const u32* palette, s32 palIndex, const u8* colormap, s32 lightLevel, u32 index);
	EditorTexture* getTextureData(u32 index);
}
