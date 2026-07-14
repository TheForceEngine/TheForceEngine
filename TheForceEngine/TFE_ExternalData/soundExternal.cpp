#include <cstring>
#include <cstdlib>
#include <cctype>
#include <TFE_System/cJSON.h>
#include <TFE_System/system.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_FileSystem/paths.h>
#include "soundExternal.h"

namespace TFE_ExternalData
{
	static std::map<std::string, std::string> s_soundOverrides;
	static bool s_externalSoundsFromMod = false;

	std::string normalizeSoundName(const char* name)
	{
		if (!name) { return std::string(); }

		// Strip any directory component.
		const char* base = name;
		for (const char* c = name; *c; c++)
		{
			if (*c == '/' || *c == '\\') { base = c + 1; }
		}

		std::string result(base);

		// Strip a trailing .voc/.VOC/.voic/.VOIC extension, if present.
		size_t dot = result.find_last_of('.');
		if (dot != std::string::npos)
		{
			std::string ext = result.substr(dot + 1);
			for (char& c : ext) { c = (char)tolower((unsigned char)c); }
			if (ext == "voc" || ext == "voic")
			{
				result = result.substr(0, dot);
			}
		}

		for (char& c : result) { c = (char)tolower((unsigned char)c); }
		result += ".voc";
		return result;
	}

	void clearExternalSounds()
	{
		s_externalSoundsFromMod = false;
		s_soundOverrides.clear();
	}

	void loadExternalSounds()
	{
		char extDataFile[TFE_MAX_PATH];
		strcpy(extDataFile, "ExternalData/DarkForces/sounds.json");
		if (!TFE_Paths::mapSystemPath(extDataFile))
		{
			const char* programDir = TFE_Paths::getPath(PATH_PROGRAM);
			sprintf(extDataFile, "%sExternalData/DarkForces/sounds.json", programDir);
		}

		// This is optional - most installs/mods won't have any sound
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

		TFE_System::logWrite(LOG_MSG, "EXTERNAL_DATA", "Loading sound override data");
		parseExternalSounds(data, false);
		free(data);
	}

	void parseExternalSounds(char* data, bool fromMod)
	{
		// Once a mod has supplied sound overrides, don't let the base
		// (non-mod) data replace them - same first-writer-wins policy
		// used by the other ExternalData categories.
		if (s_externalSoundsFromMod && !fromMod)
		{
			return;
		}

		cJSON* root = cJSON_Parse(data);
		if (!root) { return; }

		if (fromMod)
		{
			s_soundOverrides.clear();
			s_externalSoundsFromMod = true;
		}

		cJSON* section = root->child;
		while (section)
		{
			if (cJSON_IsObject(section) && strcasecmp(section->string, "sounds") == 0)
			{
				cJSON* entry = section->child;
				while (entry)
				{
					if (cJSON_IsString(entry) && entry->string && entry->valuestring)
					{
						s_soundOverrides[normalizeSoundName(entry->string)] = entry->valuestring;
					}
					entry = entry->next;
				}
			}
			section = section->next;
		}

		cJSON_Delete(root);
	}

	const char* getSoundOverride(const char* name)
	{
		if (!name || !name[0]) { return nullptr; }
		TFE_System::logWrite(LOG_MSG, "EXTERNAL_DATA", "Loading sound %s", name);
		std::map<std::string, std::string>::iterator it = s_soundOverrides.find(normalizeSoundName(name));
		if (it != s_soundOverrides.end() && !it->second.empty())
		{
			TFE_System::logWrite(LOG_MSG, "EXTERNAL_DATA", "Got override sound %s", name);
			return it->second.c_str();
		}
		return nullptr;
	}
}
