#include "editorSound.h"
#include <TFE_Editor/editor.h>
#include <TFE_DarkForces/mission.h>
#include <TFE_System/system.h>
#include <TFE_Archive/archive.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Renderer/rcommon.h>
#include <cstring>
#include <algorithm>
#include <vector>
#include <map>

// =====================================
// TODO:
//   Load Sound data.
//   Add ability to play sound.
// =====================================

namespace TFE_Editor
{
	typedef std::vector<EditorSound> SoundList;
	static SoundList s_soundList;

	s32 allocateSound(const char* name, bool& alreadyLoaded)
	{
		// Does it already exist?
		const s32 count = (s32)s_soundList.size();
		const EditorSound* sound = s_soundList.data();
		for (s32 i = 0; i < count; i++, sound++)
		{
			if (strcasecmp(sound->name, name) == 0)
			{
				alreadyLoaded = true;
				return i;
			}
		}
		alreadyLoaded = false;
		// Create a new sound.
		s32 index = (s32)s_soundList.size();
		s_soundList.emplace_back();
		return index;
	}

	s32 loadEditorSound(SoundSourceType type, Archive* archive, const char* filename, const char* filePath)
	{
		if (!archive && !filename) { return -1; }

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
		}
		else
		{
			FileStream file;
			if (!filePath || !file.open(filePath, Stream::MODE_READ))
			{
				return -1;
			}

			len = file.getSize();
			file.close();
		}

		// Allocate
		bool alreadyLoaded = false;
		s32 id = allocateSound(filename, alreadyLoaded);
		if (!alreadyLoaded)
		{
			EditorSound* outSound = &s_soundList[id];
			strcpy(outSound->name, filename);
		}
		return id;
	}

	EditorSound* getSoundData(u32 index)
	{
		if (index >= s_soundList.size()) { return nullptr; }
		return &s_soundList[index];
	}

	void freeCachedSounds()
	{
		const size_t count = s_soundList.size();
		const EditorSound* sound = s_soundList.data();
		for (size_t i = 0; i < count; i++, sound++)
		{
			// TODO: Free resource data
			// Note nothing to do until resource data is loaded.
		}
		s_soundList.clear();
	}
}