#include <cstring>
#include <cstdlib>
#include <TFE_System/cJSON.h>
#include <TFE_System/system.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_FileSystem/paths.h>
#include "musicExternal.h"

namespace TFE_ExternalData
{
	static std::map<std::string, std::string> s_musicOverrides;
	static bool s_externalMusicFromMod = false;

	void clearExternalMusic()
	{
		s_externalMusicFromMod = false;
		s_musicOverrides.clear();
	}

	void loadExternalMusic()
	{
		char extDataFile[TFE_MAX_PATH];
		strcpy(extDataFile, "ExternalData/DarkForces/music.json");
		if (!TFE_Paths::mapSystemPath(extDataFile))
		{
			const char* programDir = TFE_Paths::getPath(PATH_PROGRAM);
			sprintf(extDataFile, "%sExternalData/DarkForces/music.json", programDir);
		}

		// This is optional - most installs/mods won't have any music
		// overrides, so a missing file is not an error.
		FileStream file;
		if (!file.open(extDataFile, FileStream::MODE_READ)) { return; }

		const size_t size = file.getSize();
		char* data = (char*)malloc(size + 1);
		if (!data || size == 0)
		{
			file.close();
			return;
		}
		file.readBuffer(data, (u32)size);
		data[size] = 0;
		file.close();

		TFE_System::logWrite(LOG_MSG, "EXTERNAL_DATA", "Loading music override data");
		parseExternalMusic(data, false);
		free(data);
	}

	void parseExternalMusic(char* data, bool fromMod)
	{
		// Once a mod has supplied music overrides, don't let the base
		// (non-mod) data replace them - same first-writer-wins policy
		// used by the other ExternalData categories.
		if (s_externalMusicFromMod && !fromMod)
		{
			return;
		}

		cJSON* root = cJSON_Parse(data);
		if (!root) { return; }

		if (fromMod)
		{
			s_musicOverrides.clear();
			s_externalMusicFromMod = true;
		}

		cJSON* section = root->child;
		while (section)
		{
			if (cJSON_IsObject(section) && strcasecmp(section->string, "tracks") == 0)
			{
				cJSON* track = section->child;
				while (track)
				{
					// Each track is { "file": "<path>", "transitions": {} }.
					// "transitions" is reserved for future beat/measure-
					// aligned transition offsets and is parsed but not
					// used yet.
					if (cJSON_IsObject(track) && track->string)
					{
						cJSON* file = cJSON_GetObjectItemCaseSensitive(track, "file");
						if (file && cJSON_IsString(file) && file->valuestring)
						{
							s_musicOverrides[track->string] = file->valuestring;
						}
					}
					track = track->next;
				}
			}
			section = section->next;
		}

		cJSON_Delete(root);
	}

	const char* getMusicOverride(const char* trackName)
	{
		if (!trackName || !trackName[0]) { return nullptr; }

		std::map<std::string, std::string>::iterator it = s_musicOverrides.find(trackName);
		if (it != s_musicOverrides.end() && !it->second.empty())
		{
			return it->second.c_str();
		}
		return nullptr;
	}
}
