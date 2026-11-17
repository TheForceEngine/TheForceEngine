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

namespace TFE_Editor
{
	struct EditorFrame
	{
		TextureGpu* texGpu = nullptr;
		u32  width = 0;
		u32  height = 0;
		f32  worldWidth = 0.0f;
		f32  worldHeight = 0.0f;
		f32  offsetX = 0.0f;
		f32  offsetY = 0.0f;
		u8   paletteIndex = 0;
		u8   lightLevel = 32;
		u8   pad[2];
		char name[64] = "";
	};
	enum FrameSourceType
	{
		FRAME_FME,
		FRAME_COUNT
	};
		
	void freeCachedFrames();
	void freeFrame(const char* name);

	EditorFrame* getFrameData(u32 index);
	s32 loadEditorFrame(FrameSourceType type, Archive* archive, const char* filename, const char* filePath, const u32* palette, s32 palIndex, s32 id = -1);
	bool loadEditorFrameLit(FrameSourceType type, Archive* archive, const u32* palette, s32 palIndex, const u8* colormap, s32 lightLevel, u32 index);
}
