#include <cstring>
#include <cstdlib>
#include <TFE_System/cJSON.h>
#include <TFE_System/system.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_FileSystem/paths.h>
#include "weaponWheelExternal.h"

namespace TFE_ExternalData
{

	static const s32 c_maxWeapons = 10;

	static WeaponWheelOverride s_override[c_maxWeapons];
	static bool s_hasOverride[c_maxWeapons] = { false };
	static bool s_overridesFromMod = false;

	void clearWeaponWheelOverrides()
	{
		s_overridesFromMod = false;
		for (s32 i = 0; i < c_maxWeapons; i++)
		{
			s_override[i] = WeaponWheelOverride();
			s_hasOverride[i] = false;
		}
	}

	void loadWeaponWheelOverrides()
	{
		char extDataFile[TFE_MAX_PATH];
		strcpy(extDataFile, "ExternalData/DarkForces/weaponWheel.json");
		if (!TFE_Paths::mapSystemPath(extDataFile))
		{
			const char* programDir = TFE_Paths::getPath(PATH_PROGRAM);
			sprintf(extDataFile, "%sExternalData/DarkForces/weaponWheel.json", programDir);
		}

		// This is optional - most installs/mods won't override the
		// weapon wheel, so a missing file is not an error.
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

		TFE_System::logWrite(LOG_MSG, "EXTERNAL_DATA", "Loading weapon wheel override data");
		parseWeaponWheelOverrides(data, false);
		free(data);
	}

	void parseWeaponWheelOverrides(char* data, bool fromMod)
	{
		// Once a mod has supplied weapon wheel overrides, don't let the
		// base (non-mod) data replace them - same first-writer-wins
		// policy used by the other ExternalData categories.
		if (s_overridesFromMod && !fromMod)
		{
			return;
		}

		cJSON* root = cJSON_Parse(data);
		if (!root) { return; }

		if (fromMod)
		{
			clearWeaponWheelOverrides();
			s_overridesFromMod = true;
		}

		cJSON* entry = root->child;
		while (entry)
		{
			if (cJSON_IsObject(entry) && entry->string && strncasecmp(entry->string, "weapon", 6) == 0)
			{
				const s32 wpnId = atoi(entry->string + 6);
				if (wpnId >= 0 && wpnId < c_maxWeapons)
				{
					WeaponWheelOverride& ov = s_override[wpnId];

					cJSON* name = cJSON_GetObjectItemCaseSensitive(entry, "name");
					if (name && cJSON_IsString(name) && name->valuestring)
					{
						ov.name = name->valuestring;
						ov.hasName = true;
					}

					cJSON* imgSel = cJSON_GetObjectItemCaseSensitive(entry, "image_selected");
					if (imgSel && cJSON_IsString(imgSel) && imgSel->valuestring)
					{
						ov.imageSelected = imgSel->valuestring;
						ov.hasImageSelected = true;
					}

					cJSON* imgUnsel = cJSON_GetObjectItemCaseSensitive(entry, "image_unselected");
					if (imgUnsel && cJSON_IsString(imgUnsel) && imgUnsel->valuestring)
					{
						ov.imageUnselected = imgUnsel->valuestring;
						ov.hasImageUnselected = true;
					}

					s_hasOverride[wpnId] = true;
				}
			}
			entry = entry->next;
		}

		cJSON_Delete(root);
	}

	const WeaponWheelOverride* getWeaponWheelOverride(s32 wpnId)
	{
		if (wpnId < 0 || wpnId >= c_maxWeapons || !s_hasOverride[wpnId]) { return nullptr; }
		return &s_override[wpnId];
	}
}
