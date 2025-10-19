#pragma once
//////////////////////////////////////////////////////////////////////
// Dark Forces Mission Loop
// The main mission loop, including loading and shutdown.
//////////////////////////////////////////////////////////////////////

#include <TFE_System/types.h>
#include <TFE_Asset/dfKeywords.h>
#include <TFE_Jedi/Level/rsector.h>
#include <TFE_Jedi/Level/robject.h>
#include <TFE_Jedi/Task/task.h>

#define CONV_6bitTo8bit(x) (((x)<<2) | ((x)>>4))

namespace TFE_DarkForces
{
	enum GameMissionMode
	{
		MISSION_MODE_LOADING    = 0,	// causes the loading screen to be displayed.
		MISSION_MODE_MAIN       = 1,	// the main in-game experience.
		MISSION_MODE_UNKNOWN    = 2,	// unknown - not set (as far as I can tell).
		MISSION_MODE_LOAD_START = 3,	// set right as loading starts.
	};
		
	void mission_setLoadingFromSave();
	void mission_startTaskFunc(MessageType msg);
	void mission_setLoadMissionTask(Task* task);
	void mission_exitLevel();
	void mission_setExitLevel(JBool exitLevel);
	void mission_pause(JBool pause);

	void setScreenFxLevels(s32 healthFx, s32 shieldFx, s32 flashFx);
	void disableNightVisionInternal();

	void enableMask();
	void enableCleats();
	void enableNightVision();
	void enableHeadlamp();

	void disableMask();
	void disableCleats();
	void disableNightVision();

	void mission_render(s32 rendererIndex = 0, bool forceTextureUpdate = false);

	void mission_setupTasks();
	void mission_serialize(Stream* stream);
	void mission_serializeColorMap(Stream* stream);

	void cheat_revealMap();
	void cheat_supercharge();
	void cheat_toggleData();
	void cheat_toggleFullBright();
	void cheat_levelSkip();
	
	extern JBool s_gamePaused;
	extern GameMissionMode s_missionMode;
	extern TextureData* s_defaultLoadScreen;
	extern TextureData* s_loadScreen;
	extern u8 s_loadingScreenPal[];
	extern u8 s_levelPalette[];
	extern u8 s_basePalette[];
}  // namespace TFE_DarkForces