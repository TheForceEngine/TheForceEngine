#include "mission.h"
#include "agent.h"
#include "animLogic.h"
#include "automap.h"
#include "cheats.h"
#include "config.h"
#include "gameMusic.h"
#include "hud.h"
#include "updateLogic.h"
#include "pickup.h"
#include "player.h"
#include "projectile.h"
#include "weapon.h"
#include "darkForcesMain.h"
#include <TFE_DarkForces/Actor/actor.h>
#include <TFE_DarkForces/GameUI/escapeMenu.h>
#include <TFE_DarkForces/GameUI/pda.h>
#include <TFE_DarkForces/logic.h>
#include <TFE_Game/igame.h>
#include <TFE_Game/reticle.h>
#include <TFE_Settings/settings.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Level/level.h>
#include <TFE_Jedi/Level/levelData.h>
#include <TFE_Jedi/InfSystem/infSystem.h>
#include <TFE_Jedi/Renderer/rlimits.h>
#include <TFE_Jedi/Renderer/jediRenderer.h>
#include <TFE_Jedi/Renderer/rcommon.h>
#include <TFE_Jedi/Renderer/screenDraw.h>
#include <TFE_Jedi/Renderer/RClassic_Fixed/rclassicFixed.h>
#include <TFE_RenderShared/texturePacker.h>
#include <TFE_Jedi/Serialization/serialization.h>
#include <TFE_FrontEndUI/frontEndUi.h>
#include <TFE_FrontEndUI/console.h>
#include <TFE_Settings/settings.h>
#include <TFE_System/system.h>
#include <TFE_System/tfeMessage.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_Input/replay.h>

using namespace TFE_Jedi;
using namespace TFE_Input;

namespace TFE_DarkForces
{
	// Show the loading screen for at least 1 second.
	#define MIN_LOAD_TIME 145

	/////////////////////////////////////////////
	// Shared State
	/////////////////////////////////////////////
	JBool s_gamePaused = JTRUE;
	JBool s_canTeleport = JTRUE;
	GameMissionMode s_missionMode = MISSION_MODE_MAIN;

	TextureData* s_defaultLoadScreen = nullptr;
	TextureData* s_loadScreen = nullptr;
	u8 s_loadingScreenPal[768];
	u8 s_levelPalette[768];
	u8 s_basePalette[768];
	u8 s_escMenuPalette[768];
	u8 s_framePalette[768];

	// Move these to color?
	JBool s_palModified = JTRUE;
	JBool s_canChangePal = JTRUE;
	JBool s_screenFxEnabled = JTRUE;
	JBool s_screenBrightnessEnabled = JTRUE;
	JBool s_luminanceMask[3] = { JFALSE, JFALSE, JFALSE };
	JBool s_updateHudColors = JFALSE;

	JBool s_screenBrightnessChanged = JFALSE;
	JBool s_screenFxChanged = JFALSE;
	JBool s_lumMaskChanged = JFALSE;

	JBool s_loadingFromSave = JFALSE;

	s32 s_flashFxLevel = 0;
	s32 s_healthFxLevel = 0;
	s32 s_shieldFxLevel = 0;
	s32 s_screenBrightness = ONE_16;

	u8* s_colormap;
	u8* s_lightSourceRamp;

	// Loading
	u8* s_levelColorMap = nullptr;
	u8* s_levelColorMapBasePtr = nullptr;
	u8 s_levelLightRamp[LIGHT_SOURCE_LEVELS];

	/////////////////////////////////////////////
	// Internal State
	/////////////////////////////////////////////
	static u8*   s_framebuffer = nullptr;
	static JBool s_exitLevel = JFALSE;
	static Task* s_levelEndTask = nullptr;
	static Task* s_mainTask = nullptr;
	static Task* s_missionLoadTask = nullptr;

	static s32 s_visionFxCountdown = 0;
	static s32 s_visionFxEndCountdown = 0;

	/////////////////////////////////////////////
	// Forward Declarations
	/////////////////////////////////////////////
	void mission_mainTaskFunc(MessageType msg);
	void setPalette(u8* pal);
	void blitLoadingScreen();
	void displayLoadingScreen();
	u8*  color_loadMap(FilePath* path, u8* lightRamp, u8** basePtr);

	void setScreenBrightness(fixed16_16 brightness);
	void setScreenFxLevels(s32 healthFx, s32 shieldFx, s32 flashFx);
	void setLuminanceMask(JBool r, JBool g, JBool b);
	void setCurrentColorMap(u8* colorMap, u8* lightRamp);
	void mainTask_handleCall(MessageType msg);
	void handleGeneralInput();

	void updateScreensize();

	// Palette Filters and Effects
	void handlePaletteFx();
	void applyLuminanceFilter(u8* pal, JBool lumRedMask, JBool lumGreenMask, JBool lumBlueMask);
	void applyScreenFxToPalette(u8* pal, s32 healthFxLevel, s32 shieldFxLevel, s32 flashFxLevel);
	void applyScreenBrightness(u8* pal, s32 brightness);
	void blankScreen();

	void executeCheat(CheatID cheatID);
	extern void resumeLevelSound();
	extern void skipToLevelNextScene(s32 index);

	/////////////////////////////////////////////
	// API Implementation
	/////////////////////////////////////////////
	static Tick s_loadingScreenStart;
	static Tick s_loadingScreenDelta;
	static CheatID s_queuedCheatID = CHEAT_NONE;

	void console_cheat(const ConsoleArgList& args)
	{
		if (args.size() < 2) { return; }

		const char* cheatStr = args[1].c_str();
		s_queuedCheatID = cheat_getIDFromString(cheatStr);

		// Close the console so the cheat can be executed.
		TFE_FrontEndUI::toggleConsole();
		mission_pause(JFALSE);
		TFE_Input::enableRelativeMode(true);
	}

#define CHEAT_CMD(c) \
	void console_##c (const ConsoleArgList& args)\
	{	\
		if (s_playerDying) \
		{ \
			return; \
		} \
		s_queuedCheatID = CHEAT_##c ;	\
		TFE_FrontEndUI::toggleConsole(); \
		mission_pause(JFALSE); \
		TFE_Input::enableRelativeMode(true); \
	}
	CHEAT_CMD(LACDS);
	CHEAT_CMD(LANTFH);
	CHEAT_CMD(LAPOGO);
	CHEAT_CMD(LARANDY);
	CHEAT_CMD(LAIMLAME);
	CHEAT_CMD(LAPOSTAL);
	CHEAT_CMD(LADATA);
	CHEAT_CMD(LABUG);
	CHEAT_CMD(LAREDLITE);
	CHEAT_CMD(LASECBASE);
	CHEAT_CMD(LATALAY);
	CHEAT_CMD(LASEWERS);
	CHEAT_CMD(LATESTBASE);
	CHEAT_CMD(LAGROMAS);
	CHEAT_CMD(LADTENTION);
	CHEAT_CMD(LARAMSHED);
	CHEAT_CMD(LAROBOTICS);
	CHEAT_CMD(LANARSHADA);
	CHEAT_CMD(LAJABSHIP);
	CHEAT_CMD(LAIMPCITY);
	CHEAT_CMD(LAFUELSTAT);
	CHEAT_CMD(LAEXECUTOR);
	CHEAT_CMD(LAARC);
	CHEAT_CMD(LASKIP);
	CHEAT_CMD(LABRADY);
	CHEAT_CMD(LAUNLOCK);
	CHEAT_CMD(LAMAXOUT);
	CHEAT_CMD(LAFLY);
	CHEAT_CMD(LANOCLIP);
	CHEAT_CMD(LATESTER);
	CHEAT_CMD(LAADDLIFE);
	CHEAT_CMD(LASUBLIFE);
	CHEAT_CMD(LACAT);
	CHEAT_CMD(LADIE);
	CHEAT_CMD(LAIMDEATH);
	CHEAT_CMD(LAHARDCORE);
	CHEAT_CMD(LABRIGHT);

	void console_spawnEnemy(const ConsoleArgList& args)
	{
		if (args.size() < 3) { return; }
		logic_spawnEnemy(args[1].c_str(), args[2].c_str());
	}

	void mission_createDisplay()
	{
		vfb_setResolution(320, 200);
		s_framebuffer = vfb_getCpuBuffer();
	}

	void mission_createRenderDisplay()
	{
		TFE_Settings_Graphics* graphics = TFE_Settings::getGraphicsSettings();
		DisplayInfo info;
		TFE_RenderBackend::getDisplayInfo(&info);

		s32 adjustedWidth = graphics->gameResolution.x;
		if (graphics->widescreen && (graphics->gameResolution.z == 200 || graphics->gameResolution.z == 400))
		{
			adjustedWidth = (graphics->gameResolution.z * info.width / info.height) * 12 / 10;
		}
		else if (graphics->widescreen)
		{
			adjustedWidth = graphics->gameResolution.z * info.width / info.height;
		}
		// Make sure the adjustedWidth is divisible by 4.
		adjustedWidth = 4*((adjustedWidth + 3) >> 2);

		vfb_setResolution(adjustedWidth, graphics->gameResolution.z);
		s_framebuffer = vfb_getCpuBuffer();

		if (graphics->gameResolution.x != 320 || graphics->gameResolution.z != 200 || graphics->widescreen)
		{
			TFE_Jedi::setSubRenderer(TSR_HIGH_RESOLUTION);
		}
		else
		{
			TFE_Jedi::setSubRenderer(TSR_CLASSIC_FIXED);
		}
		automap_resetScale();

		// Copy the level palette...
		// Update the palette.
		u32 levelPalette[256];
		u32* outColor = levelPalette;
		u8* srcColor = s_levelPalette;
		for (s32 i = 0; i < 256; i++, outColor++, srcColor += 3)
		{
			*outColor = CONV_6bitTo8bit(srcColor[0]) | (CONV_6bitTo8bit(srcColor[1]) << 8u) | (CONV_6bitTo8bit(srcColor[2]) << 16u) | (0xffu << 24u);
		}
		TFE_RenderBackend::setPalette(levelPalette);
		TFE_Jedi::renderer_setSourcePalette(levelPalette);
		texturepacker_setConversionPalette(1, 6, s_levelPalette);

		// TFE
		if (TFE_Settings::getGraphicsSettings()->colorMode == COLORMODE_TRUE_COLOR)
		{
			TFE_Jedi::render_clearCachedTextures();
		}

		TFE_Jedi::renderer_setType(RendererType(graphics->rendererIndex));
		TFE_Jedi::render_setResolution();
		TFE_Jedi::renderer_setLimits();

		// Clear the palette for now.
		blankScreen();
	}

	void mission_setLoadingFromSave()
	{
		s_loadingFromSave = JTRUE;
	}

	char s_colormapName[TFE_MAX_PATH] = { 0 };

	void mission_loadColormap()
	{
		FilePath filePath;
		if (TFE_Paths::getFilePath(s_colormapName, &filePath))
		{
			s_levelColorMap = color_loadMap(&filePath, s_levelLightRamp, &s_levelColorMapBasePtr);
		}
		else if (TFE_Paths::getFilePath("DEFAULT.CMP", &filePath))
		{
			TFE_System::logWrite(LOG_WARNING, "mission_startTaskFunc", "USING DEFAULT.CMP");
			s_levelColorMap = color_loadMap(&filePath, s_levelLightRamp, &s_levelColorMapBasePtr);
		}
		setCurrentColorMap(s_levelColorMap, s_levelLightRamp);
	}

	void mission_serialize(Stream* stream)
	{
		SERIALIZE(SaveVersionInit, s_palModified, JTRUE);
		SERIALIZE(SaveVersionInit, s_canChangePal, JTRUE);
		SERIALIZE(SaveVersionInit, s_screenFxEnabled, JTRUE);
		SERIALIZE(SaveVersionInit, s_screenBrightnessEnabled, JTRUE);
		SERIALIZE_BUF(SaveVersionInit, s_luminanceMask, 3 * sizeof(JBool));
		SERIALIZE(SaveVersionInit, s_updateHudColors, JFALSE);
		SERIALIZE(SaveVersionInit, s_screenBrightnessChanged, JFALSE);
		SERIALIZE(SaveVersionInit, s_screenFxChanged, JFALSE);
		SERIALIZE(SaveVersionInit, s_lumMaskChanged, JFALSE);

		SERIALIZE(SaveVersionInit, s_flashFxLevel, 0);
		SERIALIZE(SaveVersionInit, s_healthFxLevel, 0);
		SERIALIZE(SaveVersionInit, s_shieldFxLevel, 0);
		SERIALIZE(SaveVersionInit, s_screenBrightness, ONE_16);

		SERIALIZE(SaveVersionInit, s_visionFxCountdown, 0);
		SERIALIZE(SaveVersionInit, s_visionFxEndCountdown, 0);
	}

	void mission_serializeColorMap(Stream* stream)
	{
		u8 length = 0;
		if (serialization_getMode() == SMODE_WRITE)
		{
			length = (u8)strlen(s_colormapName);
		}
		SERIALIZE(SaveVersionInit, length, 0);
		SERIALIZE_BUF(SaveVersionInit, s_colormapName, length);
		s_colormapName[length] = 0;

		if (serialization_getMode() == SMODE_READ)
		{
			mission_loadColormap();
		}
	}

	void mission_addCheatCommands()
	{
		TFE_Console::ConsoleFunc cheatFunc[] =
		{
			nullptr,			// CHEAT_NONE = 0,
			console_LACDS,		// CHEAT_LACDS,
			console_LANTFH,		// CHEAT_LANTFH,
			console_LAPOGO,		// CHEAT_LAPOGO,
			console_LARANDY,	// CHEAT_LARANDY,
			console_LAIMLAME,	// CHEAT_LAIMLAME,
			console_LAPOSTAL,	// CHEAT_LAPOSTAL,
			console_LADATA,		// CHEAT_LADATA,
			console_LABUG,		// CHEAT_LABUG,
			console_LAREDLITE,	// CHEAT_LAREDLITE,
			console_LASECBASE,	// CHEAT_LASECBASE,
			console_LATALAY,	// CHEAT_LATALAY,
			console_LASEWERS,	// CHEAT_LASEWERS,
			console_LATESTBASE, // CHEAT_LATESTBASE,
			console_LAGROMAS,	// CHEAT_LAGROMAS,
			console_LADTENTION, // CHEAT_LADTENTION,
			console_LARAMSHED,	// CHEAT_LARAMSHED,
			console_LAROBOTICS, // CHEAT_LAROBOTICS,
			console_LANARSHADA, // CHEAT_LANARSHADA,
			console_LAJABSHIP,	// CHEAT_LAJABSHIP,
			console_LAIMPCITY,	// CHEAT_LAIMPCITY,
			console_LAFUELSTAT, // CHEAT_LAFUELSTAT,
			console_LAEXECUTOR, // CHEAT_LAEXECUTOR,
			console_LAARC,		// CHEAT_LAARC,
			console_LASKIP,		// CHEAT_LASKIP,
			console_LABRADY,	// CHEAT_LABRADY,
			console_LAUNLOCK,	// CHEAT_LAUNLOCK,
			console_LAMAXOUT,	// CHEAT_LAMAXOUT,
			console_LAFLY,		// CHEAT_LAFLY,
			console_LANOCLIP,	// CHEAT_LANOCLIP,
			console_LATESTER,	// CHEAT_LATESTER,
			console_LAADDLIFE,
			console_LASUBLIFE,
			console_LACAT,
			console_LADIE,
			console_LAIMDEATH,
			console_LAHARDCORE,
			console_LABRIGHT
		};

		CCMD("cheat", console_cheat, 1, "Enter a Dark Forces cheat code as a string, example: cheat lacds");
		for (u32 i = CHEAT_NONE + 1; i < CHEAT_COUNT; i++)
		{
			const char* str = cheat_getStringFromID(CheatID(i));
			// Add "LA" to the beginning.
			char finalStr[256];
			sprintf(finalStr, "LA%s", str);
			char helpMsg[256];
			sprintf(helpMsg, "Activate the %s cheat.", finalStr);
			CCMD(finalStr, cheatFunc[i], 0, helpMsg);
		}
	}

	void mission_startTaskFunc(MessageType msg)
	{
		task_begin;
		{
			// TFE-specific
			mission_addCheatCommands();
			CCMD("spawnEnemy", console_spawnEnemy, 2, "spawnEnemy(waxName, enemyTypeName) - spawns an enemy 8 units away in the player direction. Example: spawnEnemy offcfin.wax i_officer");

			// Make sure the loading screen is displayed for at least 1 second.
			if (!s_loadingFromSave)
			{
				time_pause(JFALSE);
				mission_createDisplay();
				displayLoadingScreen();
				task_yield(TFE_Settings::getTempSettings()->skipLoadDelay ? 0 : MIN_LOAD_TIME);

				s_prevTick = s_curTick;
				s_playerTick = s_curTick;
				s_levelComplete = JFALSE;
			}
			s_mainTask = createTask("main task", mission_mainTaskFunc);

			s_invalidLevelIndex = JFALSE;
			s_exitLevel = JFALSE;

			if (!s_loadingFromSave)
			{
				s_missionMode = MISSION_MODE_LOAD_START;
				mission_setupTasks();
				displayLoadingScreen();

				// Add a yield here, so the loading screen is shown immediately.
				task_yield(TASK_NO_DELAY);
				s_loadingScreenStart = s_curTick;
				{
					const char* levelName = agent_getLevelName();
					// For now always load medium difficulty since it cannot be selected.
					if (level_load(levelName, s_agentData[s_agentId].difficulty))
					{
						setScreenBrightness(ONE_16);
						setScreenFxLevels(0, 0, 0);
						setLuminanceMask(0, 0, 0);

						strcpy(s_colormapName, levelName);
						strcat(s_colormapName, ".CMP");
						s_levelColorMap = nullptr;

						mission_loadColormap();
						automap_updateMapData(MAP_CENTER_PLAYER);
						setSkyParallax(s_levelState.parallax0, s_levelState.parallax1);
						s_missionMode = MISSION_MODE_MAIN;
						s_gamePaused = JFALSE;
						mission_createRenderDisplay();
						hud_startup(JFALSE);

						reticle_enable(true);
					}
					s_flatLighting = JFALSE;
					// Note: I am not sure why this is there but it overrides all player settings
					// By default the player load disables night vision so it won't carry over from previous maps.
					// s_nightvisionActive = JFALSE;
				}
			}
			else // Loading from save.
			{
				s_missionMode = MISSION_MODE_MAIN;
				mission_createRenderDisplay();
				hud_startup(JTRUE);
				reticle_enable(true);
			}
			s_loadingFromSave = JFALSE;
			TFE_Input::clearAccumulatedMouseMove();

			s_gamePaused = JFALSE;
			escapeMenu_resetLevel();
		}
		// Sleep until we are done with the main task.
		task_yield(TASK_SLEEP);

		// Cleanup - shut down all tasks.
		task_freeAll();

		// End the task.
		task_end;
	}

	void mission_setLoadMissionTask(Task* task)
	{
		s_missionLoadTask = task;
	}

	// In DOS, this was part of drawWorld() - 
	// for TFE I split it out to limit the amount of game code in the renderer.
	void handleVisionFx()
	{
		// Countdown to enable the night vision effect.
		if (s_visionFxCountdown != 0)
		{
			s_visionFxCountdown--;
			if (s_visionFxCountdown == 0)
			{
				setLuminanceMask(JFALSE, JTRUE, JFALSE);
			}
		}

		// Countdown to disable the night vision effect.
		if (s_visionFxEndCountdown != 0)
		{
			s_visionFxEndCountdown--;
			if (s_visionFxEndCountdown == 0)
			{
				setLuminanceMask(JFALSE, JFALSE, JFALSE);
			}
		}
	}

	void mission_exitLevel()
	{
		// Force the game to exit the replay modes in case you try to exit through the menu / console
		if (isDemoPlayback())
		{
			endReplay();
		}

		if (isRecording())
		{
			endRecording();
		}

		// Restore the default messages if you are exiting a mod. 
		if (TFE_System::modMessagesLoaded())
		{
			TFE_System::restoreDefaultMessages();
		}

		s_exitLevel = JTRUE;
	}

	void mission_setExitLevel(JBool exitLevel)
	{
		s_exitLevel = exitLevel;
	}

	void mission_render(s32 rendererIndex, bool forceTextureUpdate)
	{
		if (task_getCount() > 1 && s_missionMode == MISSION_MODE_MAIN)
		{
			TFE_Jedi::renderer_setType(rendererIndex == 0 ? RENDERER_SOFTWARE : RENDERER_HARDWARE);
			TFE_Jedi::render_setResolution(forceTextureUpdate);
			TFE_Jedi::renderer_setLimits();

			s_framebuffer = vfb_getCpuBuffer();
			TFE_Jedi::beginRender();

			updateScreensize();
			drawWorld(s_framebuffer, s_playerEye->sector, s_levelColorMap, s_lightSourceRamp);
			weapon_draw(s_framebuffer, (DrawRect*)vfb_getScreenRect(VFB_RECT_UI));
			handleVisionFx();
			handlePaletteFx();
			hud_drawAndUpdate(s_framebuffer);
			hud_drawMessage(s_framebuffer);
			automap_resetScale();

			TFE_Jedi::endRender();
			vfb_swap();
		}
	}
				
	void mission_mainTaskFunc(MessageType msg)
	{
		task_begin;
		blankScreen();

		while (msg != MSG_FREE_TASK)
		{
			// This means it is time to abort, we are done with this level.
			if (s_curTick >= 0 && (s_exitLevel || msg < MSG_RUN_TASK))
			{
				break;
			}

			// Grab the current framebuffer in case in changed.
			s_framebuffer = vfb_getCpuBuffer();
			TFE_Jedi::beginRender();
						
			// Handle delta time.
			if (!TFE_Settings::getGraphicsSettings()->useSmoothDeltaTime)
			{
				s_deltaTime = div16(intToFixed16(s_curTick - s_prevTick), FIXED(TICKS_PER_SECOND));
			}
			else
			{
				s64 curTickF = s64(intToFixed16(s_curTick)) + s_curTickFract;
				s64 prevTickF = s64(intToFixed16(s_prevTick)) + s_prevTickFract;
				s_deltaTime = div16(fixed16_16(curTickF - prevTickF), FIXED(TICKS_PER_SECOND));
			}

			s_deltaTime = min(s_deltaTime, MAX_DELTA_TIME);
			s_prevTick  = s_curTick;
			s_prevTickFract = s_curTickFract;
			s_playerTick = s_curTick;
						
			if (!escapeMenu_isOpen() && !pda_isOpen())
			{
				player_setupCamera();

				if (s_missionMode == MISSION_MODE_LOADING)
				{
					blitLoadingScreen();
				}
				else if (s_missionMode == MISSION_MODE_MAIN)
				{
					// TFE - Level Script Support.
					updateLevelScript(fixed16ToFloat(s_deltaTime));
					// Dark Forces Draw.
					updateScreensize();
					if (s_playerEye)
					{
						drawWorld(s_framebuffer, s_playerEye->sector, s_levelColorMap, s_lightSourceRamp);
					}
					weapon_draw(s_framebuffer, (DrawRect*)vfb_getScreenRect(VFB_RECT_UI));
					handleVisionFx();
				}
			}
						
			if (!escapeMenu_isOpen() && !pda_isOpen())
			{
				handleGeneralInput();
				if (s_drawAutomap)
				{
					automap_draw(s_framebuffer);
				}
				hud_drawAndUpdate(s_framebuffer);
				hud_drawMessage(s_framebuffer);
				handlePaletteFx();
			}
			else
			{
				// TFE: Gpu Renderer.
				Vec3f lumMaskGpu = { 0 };
				Vec3f palFxGpu = { 0 };
				TFE_Jedi::renderer_setPalFx(&lumMaskGpu, &palFxGpu);
			}
			
			// Move this out of handleGeneralInput so that the HUD is properly copied.
			if (escapeMenu_isOpen())
			{
				EscapeMenuAction action = escapeMenu_update();
				if (action == ESC_RETURN || action == ESC_CONFIG)
				{
					s_gamePaused = JFALSE;
					TFE_Input::clearAccumulatedMouseMove();
					task_pause(s_gamePaused);
					time_pause(s_gamePaused);

					if (action == ESC_CONFIG)
					{
						TFE_System::postSystemUiRequest();
					}
					blankScreen();
				}
				else if (action == ESC_ABORT_OR_NEXT)
				{
					s_exitLevel = JTRUE;
					TFE_Input::clearAccumulatedMouseMove();
					task_pause(JFALSE);
					time_pause(JFALSE);
				}
				else if (action == ESC_QUIT)
				{
					saveLevelStatus();
					if (TFE_Settings::getSystemSettings()->gameQuitExitsToMenu)
					{
						TFE_FrontEndUI::exitToMenu();
					}
					else
					{
						TFE_System::postQuitMessage();
					}
				}
			}
			else if (pda_isOpen())
			{
				pda_update();

				// If the PDA was closed, then unpause the game.
				if (!pda_isOpen())
				{
					mission_pause(JFALSE);
					resumeLevelSound();
				}
			}
			else if (inputMapping_getActionState(IADF_MENU_TOGGLE) == STATE_PRESSED && !s_playerDying && !TFE_FrontEndUI::isConsoleOpen())
			{
				escapeMenu_open(s_framebuffer, s_basePalette);
				s_gamePaused = JTRUE;
				task_pause(s_gamePaused, s_mainTask);
				time_pause(s_gamePaused);
			}

			// vgaSwapBuffers() in the DOS code.
			TFE_Jedi::endRender();
			vfb_swap();

			// Pump tasks and look for any with a different ID.
			do
			{
				task_yield(TASK_NO_DELAY);
				if (msg != MSG_FREE_TASK && msg != MSG_RUN_TASK)
				{
					mainTask_handleCall(msg);
				}
			} while (msg != MSG_FREE_TASK && msg != MSG_RUN_TASK);
		}
		if (TFE_Input::isRecording())
		{
			endRecording();
		}
		s_mainTask = nullptr;
		task_makeActive(s_missionLoadTask);
		task_end;
	}

	/////////////////////////////////////////////
	// Internal Implementation
	/////////////////////////////////////////////
	void mainTask_handleCall(MessageType msg)
	{
		if (msg == MSG_LIGHTS)
		{
			sector_changeGlobalLightLevel();
		}
	}
	
	// Convert the palette to 32 bit color and then send to the render backend.
	// This is functionally similar to loading the palette into VGA registers.
	void setPalette(u8* pal)
	{
		// Update the palette.
		u32 palette[256];
		u32* outColor = palette;
		u8* srcColor = pal;
		for (s32 i = 0; i < 256; i++, outColor++, srcColor += 3)
		{
			*outColor = CONV_6bitTo8bit(srcColor[0]) | (CONV_6bitTo8bit(srcColor[1]) << 8u) | (CONV_6bitTo8bit(srcColor[2]) << 16u) | (0xffu << 24u);
		}
		vfb_setPalette(palette);
	}
				
	void blitLoadingScreen()
	{
		// Moddable loading screen
		const char* levelName = agent_getLevelName();
		ModSettingLevelOverride* modLevelOverride = TFE_Settings::getLevelOverrides(levelName);
		if (modLevelOverride && !modLevelOverride->levName.empty())
		{
			if (modLevelOverride->textureOverrideMap["loadScreen"])
			{
				s_loadScreen = modLevelOverride->textureOverrideMap["loadScreen"];
			}
			else
			{
				s_loadScreen = s_defaultLoadScreen;
			}
		}
		else
		{
			s_loadScreen = s_defaultLoadScreen;
		}
		
		if (!s_loadScreen) { return; }
		blitTextureToScreen(s_loadScreen, (DrawRect*)vfb_getScreenRect(VFB_RECT_UI), 0/*x0*/, 0/*y0*/, s_framebuffer, JFALSE, JTRUE);
	}

	void displayLoadingScreen()
	{
		blitLoadingScreen();

		// Update twice to make sure the loading screen is visible.
		// The virtual display is buffered, meaning there is a frame of latency.
		// This hackery basically removes that latency so the image is properly displayed immediately.
		setPalette(s_loadingScreenPal);
		setPalette(s_loadingScreenPal);
		vfb_swap();
		vfb_swap();
	}

	void mission_setupTasks()
	{
		setSpriteAnimation(nullptr, nullptr);
		bitmap_setupAnimationTask();
		hud_startup(JFALSE);
		hud_clearMessage();
		automap_computeScreenBounds();
		weapon_clearFireRate();
		weapon_createPlayerWeaponTask();
		projectile_createTask();
		player_createController(s_loadingFromSave ? JFALSE : JTRUE);
		inf_createElevatorTask();
		player_clearEyeObject();
		pickup_createTask();
		inf_createTeleportTask();
		inf_createTriggerTask();
		actor_createTask();
		hitEffect_createTask();
		level_clearData();
		updateLogic_clearTask();
		s_drawAutomap = JFALSE;
		s_levelEndTask = nullptr;
		s_cheatString[0] = 0;
		s_cheatCharIndex = 0;
		s_cheatInputCount = 0;
		s_queuedCheatID = CHEAT_NONE;
	}
		
	void setScreenBrightness(fixed16_16 brightness)
	{
		if (brightness != s_screenBrightness)
		{
			s_screenBrightness = brightness;
			s_screenBrightnessChanged = JTRUE;
		}
	}

	void setScreenFxLevels(s32 healthFx, s32 shieldFx, s32 flashFx)
	{
		if (healthFx != s_healthFxLevel || shieldFx != s_shieldFxLevel || flashFx != s_flashFxLevel)
		{
			s_healthFxLevel = healthFx;
			s_shieldFxLevel = shieldFx;
			s_flashFxLevel  = flashFx;
			s_screenFxChanged = JTRUE;
		}
	}

	void setLuminanceMask(JBool r, JBool g, JBool b)
	{
		if (r != s_luminanceMask[0] || g != s_luminanceMask[1] || b != s_luminanceMask[2])
		{
			s_luminanceMask[0] = r;
			s_luminanceMask[1] = g;
			s_luminanceMask[2] = b;
			s_lumMaskChanged = JTRUE;
		}
	}
		
	void handlePaletteFx()
	{
		JBool useFramePal   = JFALSE;
		JBool updateBasePal = JFALSE;
		JBool copiedPalette = JFALSE;
		if (!s_canChangePal) { return; }

		// TFE
		Vec3f lumMaskGpu = { 0 };
		Vec3f palFxGpu = { 0 };

		if (s_luminanceMask[0] || s_luminanceMask[1] || s_luminanceMask[2])
		{
			if (!copiedPalette)
			{
				memcpy(s_framePalette, s_basePalette, 768);
				copiedPalette = JTRUE;
			}
			applyLuminanceFilter(s_framePalette, s_luminanceMask[0], s_luminanceMask[1], s_luminanceMask[2]);
			s_palModified = JTRUE;
			useFramePal = JTRUE;

			lumMaskGpu = { s_luminanceMask[0] ? 1.0f : 0.0f, s_luminanceMask[1] ? 1.0f : 0.0f, s_luminanceMask[2] ? 1.0f : 0.0f };
		}
		else if (s_palModified)
		{
			updateBasePal = JTRUE;
		}

		s_lumMaskChanged = JFALSE;
		if (s_screenFxEnabled)
		{
			if (s_healthFxLevel || s_shieldFxLevel || s_flashFxLevel)
			{
				if (!copiedPalette)
				{
					memcpy(s_framePalette, s_basePalette, 768);
					copiedPalette = JTRUE;
				}
				applyScreenFxToPalette(s_framePalette, s_healthFxLevel, s_shieldFxLevel, s_flashFxLevel);
				useFramePal = JTRUE;
				s_palModified = JTRUE;

				palFxGpu = { min(1.0f, f32(s_healthFxLevel) / 63.0f), min(1.0f, f32(s_shieldFxLevel) / 63.0f), min(1.0f, f32(s_flashFxLevel) / 63.0f) };
			}
			else if (s_palModified)
			{
				updateBasePal = JTRUE;
			}
			s_screenFxChanged = JFALSE;
		}

		if (s_screenBrightnessEnabled)
		{
			if (s_screenBrightness < ONE_16)
			{
				if (!copiedPalette)
				{
					memcpy(s_framePalette, s_basePalette, 768);
					copiedPalette = JTRUE;
				}
				applyScreenBrightness(s_framePalette, s_screenBrightness);
				s_palModified = JTRUE;
				useFramePal = JTRUE;
			}
			else if (s_palModified)
			{
				updateBasePal = JTRUE;
			}
			s_screenBrightnessChanged = JFALSE;
		}

		if (useFramePal)
		{
			// Convert the palette to true color and send to the render backend.
			setPalette(s_framePalette);
			s_updateHudColors = JFALSE;
		}
		else
		{
			if (updateBasePal)
			{
				copiedPalette = JFALSE;
				setPalette(s_basePalette);
				s_updateHudColors = JFALSE;
			}
			else if (s_updateHudColors)
			{
				// Update the HUD shield color indices.
				// For TFE, just upload the full palette, the original DOS code
				// only updated the VGA registers for the changed HUD colors.
				setPalette(s_basePalette);
				s_updateHudColors = JFALSE;
			}
		}

		// TFE: Gpu Renderer.
		TFE_Jedi::renderer_setPalFx(&lumMaskGpu, &palFxGpu);

		// TFE uses a dynamic multi-buffered texture for the palette. This doesn't work well when trying to set it only once.
		// For this reason, it is easier to just set the palette every frame regardless of change.
	#if 0
		if (!s_luminanceMask[0] && !s_luminanceMask[1] && !s_luminanceMask[2] && !s_healthFxLevel && !s_shieldFxLevel && !s_flashFxLevel && s_screenBrightness == ONE_16)
		{
			s_palModified = JFALSE;
		}
	#endif
	}

	void setCurrentColorMap(u8* colorMap, u8* lightRamp)
	{
		s_colormap = colorMap;
		s_lightSourceRamp = lightRamp;
	}

	// Move to the appropriate place.
	u8* color_loadMap(FilePath* path, u8* lightRamp, u8** basePtr)
	{
		FileStream file;
		if (!file.open(path, Stream::MODE_READ))
		{
			TFE_System::logWrite(LOG_ERROR, "color_loadMap", "Error loading color map.");
			return nullptr;
		}

		// Allocate 256 colors * 32 light levels + 256, where the last 256 is so that the address can be rounded to the next 256 byte boundary.
		u8* colorMapBase = (u8*)level_alloc(8576);
		u8* colorMap = colorMapBase;
		*basePtr = colorMapBase;
		if (size_t(colorMap) & 0xffu)
		{
			// colorMap2 is rounded to the next 256 bytes.
			colorMap = colorMapBase + 256 - (size_t(colorMap) & 0xff);
		}
		// 256 colors * 32 light levels = 8192
		file.readBuffer(colorMap, 8192);
		file.readBuffer(lightRamp, LIGHT_SOURCE_LEVELS);
		file.close();

		return colorMap;
	}

	void disableHeadlamp()
	{
		hud_sendTextMessage(12);
		s_headlampActive = JFALSE;
	}

	void enableHeadlamp()
	{
		if (s_batteryPower)
		{
			s_headlampActive = JTRUE;
			hud_sendTextMessage(13);
		}
	}

	void beginNightVision(s32 ambient)
	{
		s_flatAmbient = ambient;
		s_flatLighting = JTRUE;
		s_visionFxCountdown = 2;
	}

	void disableNightVisionInternal()
	{
		s_flatLighting = JFALSE;
		s_visionFxEndCountdown = 3;
	}
		
	void disableNightVision()
	{
		disableNightVisionInternal();
		s_nightVisionActive = JFALSE;
		hud_sendTextMessage(9);
	}

	void enableNightVision()
	{
		if (!s_playerInfo.itemGoggles || s_externalCameraMode) { return; }

		if (!s_batteryPower)
		{
			hud_sendTextMessage(11);
			sound_play(s_nightVisionDeactiveSoundSource);
			s_nightVisionActive = JFALSE;
			return;
		}

		s_nightVisionActive = JTRUE;
		beginNightVision(16);
		hud_sendTextMessage(10);
		sound_play(s_nightVisionActiveSoundSource);
	}

	void disableCleats()
	{
		if (s_wearingCleats)
		{
			s_wearingCleats = JFALSE;
			hud_sendTextMessage(21);
		}
	}

	void enableCleats()
	{
		s_wearingCleats = JTRUE;
		hud_sendTextMessage(22);
	}
		
	void disableMask()
	{
		if (!s_wearingGasmask)
		{
			return;
		}

		hud_sendTextMessage(19);
		s_wearingGasmask = JFALSE;
		if (s_gasmaskTask)
		{
			task_free(s_gasmaskTask);
			s_gasmaskTask = nullptr;
		}
	}

	void enableMask()
	{
		if (!s_playerInfo.itemMask) { return; }

		if (!s_batteryPower)
		{
			hud_sendTextMessage(11);
			s_wearingGasmask = JFALSE;
			if (s_gasmaskTask)
			{
				task_free(s_gasmaskTask);
				s_gasmaskTask = nullptr;
			}
			return;
		}

		s_wearingGasmask = JTRUE;
		hud_sendTextMessage(20);
		if (!s_gasmaskTask)
		{
			s_gasmaskTask = createSubTask("gasmask", gasmaskTaskFunc);
		}
	}

	void cheat_gotoLevel(s32 index)
	{
		agent_setNextLevelByIndex(index);
		s_levelComplete = JTRUE;
		s_exitLevel = JTRUE;

		skipToLevelNextScene(index + 1);
	}

	void cheat_levelSkip()
	{
		agent_levelComplete();
		s_exitLevel = JTRUE;
	}

	void cheat_revealMap()
	{
		automap_updateMapData(MAP_INCR_SECTOR_MODE);
	}

	void cheat_supercharge()
	{
		pickupSupercharge();
		hud_sendTextMessage(701);
	}
	
	void cheat_toggleData()
	{
		hud_toggleDataDisplay();
	}

	void cheat_toggleFullBright()
	{
		const char* msg = TFE_System::getMessage(TFE_MSG_FULLBRIGHT);
		if (msg) { hud_sendTextMessage(msg, 1); }
		s_fullBright = ~s_fullBright;
	}

	void executeCheat(CheatID cheatID)
	{
		// Do not allow cheats while recording
		if (cheatID == CHEAT_NONE || isRecording())
		{
			return;
		}

		switch (cheatID)
		{
			case CHEAT_LACDS:
			{
				cheat_revealMap();
			} break;
			case CHEAT_LANTFH:
			{
				automap_updateMapData(MAP_TELEPORT);
				cheat_teleport();
			} break;
			case CHEAT_LAPOGO:
			{
				cheat_toggleHeightCheck();
			} break;
			case CHEAT_LARANDY:
			{
				cheat_supercharge();
			} break;
			case CHEAT_LAIMLAME:
			{
				cheat_godMode();
			} break;
			case CHEAT_LAPOSTAL:
			{
				cheat_postal();
			} break;
			case CHEAT_LADATA:
			{
				cheat_toggleData();
			} break;
			case CHEAT_LABUG:
			{
				cheat_bugMode();
			} break;
			case CHEAT_LAREDLITE:
			{
				cheat_pauseAI();
			} break;
			case CHEAT_LASECBASE:
			{
				cheat_gotoLevel(0);
			} break;
			case CHEAT_LATALAY:
			{
				cheat_gotoLevel(1);
			} break;
			case CHEAT_LASEWERS:
			{
				cheat_gotoLevel(2);
			} break;
			case CHEAT_LATESTBASE:
			{
				cheat_gotoLevel(3);
			} break;
			case CHEAT_LAGROMAS:
			{
				cheat_gotoLevel(4);
			} break;
			case CHEAT_LADTENTION:
			{
				cheat_gotoLevel(5);
			} break;
			case CHEAT_LARAMSHED:
			{
				cheat_gotoLevel(6);
			} break;
			case CHEAT_LAROBOTICS:
			{
				cheat_gotoLevel(7);
			} break;
			case CHEAT_LANARSHADA:
			{
				cheat_gotoLevel(8);
			} break;
			case CHEAT_LAJABSHIP:
			{
				cheat_gotoLevel(9);
			} break;
			case CHEAT_LAIMPCITY:
			{
				cheat_gotoLevel(10);
			} break;
			case CHEAT_LAFUELSTAT:
			{
				cheat_gotoLevel(11);
			} break;
			case CHEAT_LAEXECUTOR:
			{
				cheat_gotoLevel(12);
			} break;
			case CHEAT_LAARC:
			{
				cheat_gotoLevel(13);
			} break;
			case CHEAT_LASKIP:
			{
				cheat_levelSkip();
			} break;
			case CHEAT_LABRADY:
			{
				cheat_fullAmmo();
			} break;
			case CHEAT_LAUNLOCK:
			{
				cheat_unlock();
			} break;
			case CHEAT_LAMAXOUT:
			{
				cheat_maxout();
			} break;
			case CHEAT_LAFLY:
			{
				cheat_fly();
			} break;
			case CHEAT_LANOCLIP:
			{
				cheat_noclip();
			} break;
			case CHEAT_LATESTER:
			{
				cheat_tester();
			} break;
			case CHEAT_LAADDLIFE:
			{
				cheat_addLife();
			} break;
			case CHEAT_LASUBLIFE:
			{
				cheat_subLife();
			} break;
			case CHEAT_LACAT:
			{
				cheat_maxLives();
			} break;
			case CHEAT_LADIE:
			{
				cheat_die();
			} break;
			case CHEAT_LAIMDEATH:
			{
				cheat_oneHitKill();
			} break;
			case CHEAT_LAHARDCORE:
			{
				cheat_instaDeath();
			} break;
			case CHEAT_LABRIGHT:
			{
				cheat_toggleFullBright();
			} break;
		}
	}
		
	void handleBufferedInput()
	{
		const char* bufferedText = TFE_Input::getBufferedText();
		const size_t bufferedLen = strlen(bufferedText);

		for (size_t i = 0; i < bufferedLen; i++)
		{
			char c = toupper(bufferedText[i]);
			if (s_cheatInputCount < 2 && c == 'L')
			{
				s_cheatInputCount = 1;
			}
			else if (s_cheatInputCount == 1)
			{
				if (c == 'A')
				{
					s_cheatInputCount = 2;
					s_cheatCharIndex = 0;
					s_cheatString[0] = 0;
				}
				else
				{
					s_cheatInputCount = 0;
				}
			}
			else if (s_cheatInputCount >= 2)
			{
				s32 index = s_cheatCharIndex;
				s_cheatCharIndex++;

				s_cheatString[index] = c;
				s_cheatString[s_cheatCharIndex] = 0;

				CheatID cheatID = cheat_getID();
				executeCheat(cheatID);
			}
		}
	}

	void mission_pause(JBool pause)
	{
		s_gamePaused = pause;
		task_pause(s_gamePaused, s_mainTask);
		time_pause(s_gamePaused);
		TFE_Input::clearAccumulatedMouseMove();
	}

	void handleGeneralInput()
	{
		// Early out if the player is dying.
		if (s_playerDying)
		{
			return;
		}

		// In the DOS code, the game would just loop here - checking to see if paused has been pressed and then continue.
		// Obviously that won't work for TFE, so the game paused variable is set and the game will have to handle it.
		if (inputMapping_getActionState(IADF_PAUSE) == STATE_PRESSED)
		{
			mission_pause(~s_gamePaused);
		}

		if (!s_gamePaused)
		{
			handleBufferedInput();
			if (s_queuedCheatID != CHEAT_NONE)
			{
				executeCheat(s_queuedCheatID);
				s_queuedCheatID = CHEAT_NONE;
			}

			// For now just deal with a few controls.
			if (inputMapping_getActionState(IADF_PDA_TOGGLE) == STATE_PRESSED)
			{
				// Clear out the PDA_TOGGLE state for this frame since it will be read again later.
				inputMapping_removeState(IADF_PDA_TOGGLE);

				mission_pause(JTRUE);
				pda_start(agent_getLevelName());
			}
			if (inputMapping_getActionState(IADF_NIGHT_VISION_TOG) == STATE_PRESSED && s_playerInfo.itemGoggles)
			{
				if (s_nightVisionActive)
				{
					disableNightVision();
				}
				else
				{
					enableNightVision();
				}
			}
			if (inputMapping_getActionState(IADF_CLEATS_TOGGLE) == STATE_PRESSED && s_playerInfo.itemCleats)
			{
				if (s_wearingCleats)
				{
					disableCleats();
				}
				else
				{
					enableCleats();
				}
			}
			if (inputMapping_getActionState(IADF_GAS_MASK_TOGGLE) == STATE_PRESSED && s_playerInfo.itemMask)
			{
				if (s_wearingGasmask)
				{
					disableMask();
				}
				else
				{
					enableMask();
				}
			}
			if (inputMapping_getActionState(IADF_HEAD_LAMP_TOGGLE) == STATE_PRESSED)
			{
				if (s_headlampActive)
				{
					disableHeadlamp();
				}
				else
				{
					enableHeadlamp();
				}
			}
			renderer_setupCameraLight(JFALSE, s_headlampActive);

			if (inputMapping_getActionState(IADF_HEADWAVE_TOGGLE) == STATE_PRESSED)
			{
				TFE_Settings_A11y* settings = TFE_Settings::getA11ySettings();
				settings->enableHeadwave = !settings->enableHeadwave;
				if (settings->enableHeadwave)
				{
					hud_sendTextMessage(14);
				}
				else
				{
					hud_sendTextMessage(15);
				}
			}

			if (inputMapping_getActionState(IADF_HUD_TOGGLE) == STATE_PRESSED)
			{
				s_config.showUI = hud_setupToggleAnim1(JFALSE);
			}

			if (inputMapping_getActionState(IADF_HOLSTER_WEAPON) == STATE_PRESSED)
			{
				weapon_holster();
			}

			if (inputMapping_getActionState(IADF_HD_ASSET_TOGGLE) == STATE_PRESSED)
			{
				const char* msg = TFE_System::getMessage(TFE_MSG_HD);
				if (msg)
				{
					hud_sendTextMessage(msg, 1);	// HD assets
				}

				// Ensure the return state is in the game otherwise it won't render in FrontEndUI 
				// Does the render call need the state check?
				TFE_FrontEndUI::setMenuReturnState(APP_STATE_GAME);
				TFE_FrontEndUI::toggleEnhancements();
			}

			if (inputMapping_getActionState(IADF_AUTOMOUNT_TOGGLE) == STATE_PRESSED)
			{
				s_config.wpnAutoMount = ~s_config.wpnAutoMount;
				weapon_enableAutomount(s_config.wpnAutoMount);
				if (!s_config.wpnAutoMount)
				{
					hud_sendTextMessage(23);
				}
				else
				{
					hud_sendTextMessage(24);
				}
			}

			if (inputMapping_getActionState(IADF_CYCLEWPN_PREV) == STATE_PRESSED)
			{
				player_cycleWeapons(-1);
			}
			else if (inputMapping_getActionState(IADF_CYCLEWPN_NEXT) == STATE_PRESSED)
			{
				player_cycleWeapons(1);
			}

			if (inputMapping_getActionState(IADF_AUTOMAP) == STATE_PRESSED)
			{
				s_drawAutomap = ~s_drawAutomap;
				if (s_drawAutomap)
				{
					automap_updateMapData(MAP_ENABLE_AUTOCENTER);
					automap_updateMapData(MAP_CENTER_PLAYER);
					s_canTeleport = JTRUE;
					reticle_enable(false);
				}
				else
				{
					s_canTeleport = JFALSE;
					reticle_enable(true);
				}
			}

			if (s_drawAutomap)
			{
				reticle_enable(false);

				if (inputMapping_getActionState(IADF_MAP_ENABLE_SCROLL))
				{
					automap_disableLock();
				}
				else
				{
					automap_enableLock();
				}

				if (inputMapping_getActionState(IADF_MAP_ZOOM_IN))
				{
					automap_updateMapData(MAP_ZOOM_IN);
				}
				else if (inputMapping_getActionState(IADF_MAP_ZOOM_OUT))
				{
					automap_updateMapData(MAP_ZOOM_OUT);
				}

				if (inputMapping_getActionState(IADF_MAP_LAYER_UP) == STATE_PRESSED)
				{
					automap_updateMapData(MAP_LAYER_UP);

					s32 msgId = min(automap_getLayer() + 609, 618);
					hud_sendTextMessage(msgId);
				}
				else if (inputMapping_getActionState(IADF_MAP_LAYER_DN) == STATE_PRESSED)
				{
					automap_updateMapData(MAP_LAYER_DOWN);

					s32 msgId = min(automap_getLayer() + 609, 618);
					hud_sendTextMessage(msgId);
				}

				if (!s_automapLocked)
				{
					if (inputMapping_getActionState(IADF_MAP_SCROLL_UP))
					{
						automap_updateMapData(MAP_MOVE1_UP);
					}
					if (inputMapping_getActionState(IADF_MAP_SCROLL_DN))
					{
						automap_updateMapData(MAP_MOVE1_DN);
					}
					if (inputMapping_getActionState(IADF_MAP_SCROLL_LT))
					{
						automap_updateMapData(MAP_MOVE1_LEFT);
					}
					if (inputMapping_getActionState(IADF_MAP_SCROLL_RT))
					{
						automap_updateMapData(MAP_MOVE1_RIGHT);
					}
				}
			}
			else
			{
				automap_enableLock();
			}
		}

		// DEBUG - change music state.
	#if 0
		if (TFE_Input::keyPressed(KEY_F))
		{
			gameMusic_setState(MUS_STATE_FIGHT);
		}
		else if (TFE_Input::keyPressed(KEY_G))
		{
			gameMusic_setState(MUS_STATE_STALK);
		}
	#endif
	}

	void updateScreensize()
	{
		// STUB: Not needed until screensizes are supported.
	}

	////////////////////////////////////////////
	// Palette Filters and Effects
	////////////////////////////////////////////
	void applyLuminanceFilter(u8* pal, JBool lumRedMask, JBool lumGreenMask, JBool lumBlueMask)
	{
		u8* end = &pal[768];
		for (; pal < end; pal += 3)
		{
			// Compute the approximate luminance (red/4 + green/2 + blue/4)
			u32 L = (pal[0] >> 2) + (pal[1] >> 1) + (pal[2] >> 2);
			// Then assign the luminance to the requested channels.
			pal[0] = lumRedMask ? L : 0;
			pal[1] = lumGreenMask ? L : 0;
			pal[2] = lumBlueMask ? L : 0;
		}
	}

	void applyScreenFxToPalette(u8* pal, s32 healthFxLevel, s32 shieldFxLevel, s32 flashFxLevel)
	{
		s32 intensity;
		s32 filterIndex;
		s32 clrIndex0, clrIndex1;
		if (healthFxLevel)
		{
			intensity = 63 - (healthFxLevel & 63);
			filterIndex = 0;
			clrIndex0 = 1;
			clrIndex1 = 2;
		}
		else if (shieldFxLevel)
		{
			intensity = 63 - (shieldFxLevel & 63);
			filterIndex = 1;
			clrIndex0 = 0;
			clrIndex1 = 2;
		}
		else
		{
			intensity = 63 - (flashFxLevel & 63);
			filterIndex = 2;
			clrIndex0 = 0;
			clrIndex1 = 1;
		}

		u8* c0 = &pal[clrIndex0];
		u8* c1 = &pal[clrIndex1];
		u8* filterColor = &pal[filterIndex];

		for (s32 i = 0; i < 256; i++)
		{
			// Essentially: 63 - abs(63 - color) * (1 - flashLevel)
			// Note that when flashLevel = 0: 63 - 63 + color = color
			//                flashLevel = 1: 63 = pure blue.
			*filterColor = 63 - (TFE_Jedi::abs((63 - (*filterColor)) * intensity) >> 6);

			// Color channel = color * (1 - flashLevel)
			*c0 = TFE_Jedi::abs((*c0) * intensity) >> 6;
			*c1 = TFE_Jedi::abs((*c1) * intensity) >> 6;

			filterColor += 3;
			c0 += 3;
			c1 += 3;
		}
	}

	void applyScreenBrightness(u8* pal, s32 brightness)
	{
		u8* end = &pal[768];
		for (s32 i = 0; i < 256; i++, pal += 3)
		{
			fixed16_16 color = intToFixed16(pal[0]);
			color = mul16(color, brightness) + HALF_16;
			pal[0] = floor16(color);

			color = intToFixed16(pal[1]);
			color = mul16(color, brightness) + HALF_16;
			pal[1] = floor16(color);

			color = intToFixed16(pal[2]);
			color = mul16(color, brightness) + HALF_16;
			pal[2] = floor16(color);
		}
	}

	void blankScreen()
	{
		u32 zero[256] = { 0 };
		// Make sure both buffers are cleared out to avoid flashes.
		vfb_setPalette(zero);
		vfb_setPalette(zero);
	}

}  // TFE_DarkForces