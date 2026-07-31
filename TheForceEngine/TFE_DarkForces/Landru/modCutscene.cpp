#include "modCutscene.h"
#include <TFE_DarkForces/darkForcesMain.h>
#include <TFE_System/system.h>
#include <TFE_System/cJSON.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_FileSystem/filestream.h>
#include <cstring>
#include <vector>
#include <string>

namespace TFE_DarkForces
{
	struct ModCutsceneSlot
	{
		char intro[TFE_MAX_PATH] = "";
		char outro[TFE_MAX_PATH] = "";
		bool hasIntro = false;
		bool hasOutro = false;
	};

	struct ModMissionSlot
	{
		std::string name;   
		ModCutsceneSlot slot;
	};

	static bool s_active = false;
	static ModCutsceneSlot s_gameSlot;
	static std::vector<ModMissionSlot> s_missionSlots;

	// Reads "intro"/"outro" string fields out of a JSON object into a slot.
	// Missing, non-string, or empty-string fields just leave hasIntro/
	// hasOutro false for that field - exactly "not defined", which is what
	// tells the caller to play nothing for that slot.
	static void parseSlot(const cJSON* obj, ModCutsceneSlot& out)
	{
		const cJSON* introItem = cJSON_GetObjectItem(obj, "intro");
		if (introItem && cJSON_IsString(introItem) && introItem->valuestring && introItem->valuestring[0])
		{
			strncpy(out.intro, introItem->valuestring, sizeof(out.intro) - 1);
			out.intro[sizeof(out.intro) - 1] = 0;
			out.hasIntro = true;
		}

		const cJSON* outroItem = cJSON_GetObjectItem(obj, "outro");
		if (outroItem && cJSON_IsString(outroItem) && outroItem->valuestring && outroItem->valuestring[0])
		{
			strncpy(out.outro, outroItem->valuestring, sizeof(out.outro) - 1);
			out.outro[sizeof(out.outro) - 1] = 0;
			out.hasOutro = true;
		}
	}

	void modCutscene_clear()
	{
		s_active = false;
		s_gameSlot = ModCutsceneSlot();
		s_missionSlots.clear();
	}

	void modCutscene_init()
	{
		modCutscene_clear();

		// Only ever read from an actual loaded mod - never from the base
		// game data or a generic search path. See df_isCustomModLoaded()
		// for why this is the right check (and not just probing whether
		// the file happens to be findable).
		if (!df_isCustomModLoaded()) { return; }

		FilePath filePath;
		if (!TFE_Paths::getFilePath("cutscene.json", &filePath)) { return; }

		FileStream file;
		if (!file.open(&filePath, FileStream::MODE_READ)) { return; }

		const size_t size = file.getSize();
		if (size == 0)
		{
			file.close();
			TFE_System::logWrite(LOG_WARNING, "ModCutscene", "cutscene.json is empty, ignoring.");
			return;
		}

		char* data = (char*)malloc(size + 1);
		if (!data)
		{
			file.close();
			return;
		}
		file.readBuffer(data, (u32)size);
		data[size] = 0;
		file.close();

		cJSON* root = cJSON_Parse(data);
		free(data);

		if (!root)
		{
			const char* err = cJSON_GetErrorPtr();
			TFE_System::logWrite(LOG_ERROR, "ModCutscene", "Failed to parse cutscene.json %s%s. Is the JSON valid? Check on jsonlint.com",
				err ? " near: " : "", err ? err : "");
			return;
		}

		s32 missionCount = 0;
		const cJSON* curElem = root->child;
		for (; curElem; curElem = curElem->next)
		{
			if (!curElem->string || !cJSON_IsObject(curElem)) { continue; }

			ModCutsceneSlot slot;
			parseSlot(curElem, slot);

			if (strcasecmp(curElem->string, "game") == 0)
			{
				s_gameSlot = slot;
			}
			else
			{
				ModMissionSlot m;
				m.name = curElem->string;
				m.slot = slot;
				s_missionSlots.push_back(m);
				missionCount++;
			}
		}
		cJSON_Delete(root);

		// Active even if every slot turned out empty - an intentionally
		// sparse or "no cutscenes" cutscene.json should still fully
		// override the stock system, not silently fall back to it.
		s_active = true;

		TFE_System::logWrite(LOG_MSG, "ModCutscene", "Loaded cutscene.json (%d mission entr%s).",
			missionCount, missionCount == 1 ? "y" : "ies");
	}

	bool modCutscene_isActive()
	{
		return s_active;
	}

	const char* modCutscene_getGameIntro()
	{
 		return s_gameSlot.hasIntro ? s_gameSlot.intro : nullptr;
	}

	const char* modCutscene_getGameOutro()
	{
		return s_gameSlot.hasOutro ? s_gameSlot.outro : nullptr;
	}

	static const ModCutsceneSlot* findMissionSlot(const char* levelName)
	{
		if (!levelName || !levelName[0]) { return nullptr; }
		for (size_t i = 0; i < s_missionSlots.size(); i++)
		{
			if (strcasecmp(s_missionSlots[i].name.c_str(), levelName) == 0)
			{
				return &s_missionSlots[i].slot;
			}
		}
		return nullptr;
	}

	const char* modCutscene_getMissionIntro(const char* levelName)
	{
		const ModCutsceneSlot* slot = findMissionSlot(levelName);
		return (slot && slot->hasIntro) ? slot->intro : nullptr;
	}

	const char* modCutscene_getMissionOutro(const char* levelName)
	{
		const ModCutsceneSlot* slot = findMissionSlot(levelName);
		return (slot && slot->hasOutro) ? slot->outro : nullptr;
	}
}  // TFE_DarkForces
