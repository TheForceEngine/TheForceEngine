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
	enum SoundSourceType
	{
		SOUND_VOC = 0,
		SOUND_COUNT
	};

	struct EditorSound
	{
		char name[64] = "";
		// TODO
	};

	s32 loadEditorSound(SoundSourceType type, Archive* archive, const char* filename, const char* filePath);
	EditorSound* getSoundData(u32 index);
	void freeCachedSounds();
}
