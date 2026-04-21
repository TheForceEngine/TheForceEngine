// main.cpp : Defines the entry point for the application.
#include "version.h"
#include <SDL.h>
#include <TFE_System/types.h>
#include <TFE_System/profiler.h>
#include <TFE_Memory/memoryRegion.h>
#include <TFE_Archive/gobArchive.h>
#include <TFE_Game/igame.h>
#include <TFE_Game/saveSystem.h>
#include <TFE_Game/reticle.h>
#include <TFE_Jedi/InfSystem/infSystem.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_Audio/audioSystem.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_Polygon/polygon.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_Settings/settings.h>
#include <TFE_System/system.h>
#include <TFE_System/CrashHandler/crashHandler.h>
#include <TFE_System/frameLimiter.h>
#include <TFE_System/tfeMessage.h>
#include <TFE_Jedi/Task/task.h>
#include <TFE_RenderShared/texturePacker.h>
#include <TFE_Asset/paletteAsset.h>
#include <TFE_Asset/imageAsset.h>
#include <TFE_Ui/ui.h>
#include <TFE_FrontEndUI/frontEndUi.h>
#include <TFE_FrontEndUI/modLoader.h>
#include <TFE_A11y/accessibility.h>
#include <algorithm>
#include <cinttypes>
#include <time.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <TFE_DarkForces/hud.h>
#include <TFE_DarkForces/mission.h>
#include <TFE_Input/replay.h>

#if ENABLE_EDITOR == 1
#include <TFE_Editor/editor.h>
#endif
#include <TFE_ForceScript/forceScript.h>

#include <TFE_Audio/midiPlayer.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>
#ifdef min
#undef min
#undef max
#pragma comment(lib, "SDL2main.lib")
#endif
#endif

#define PROGRAM_ERROR   1
#define PROGRAM_SUCCESS 0

#ifndef _DEBUG
#define INSTALL_CRASH_HANDLER 1
#else
#define INSTALL_CRASH_HANDLER 0
#endif

using namespace TFE_Input;
using namespace TFE_A11Y;

static bool s_loop  = true;
static bool s_nullAudioDevice = false;
static f32  s_refreshRate  = 0;
static s32  s_displayIndex = 0;
static u32  s_baseWindowWidth  = 1280;
static u32  s_baseWindowHeight = 720;
static u32  s_displayWidth  = s_baseWindowWidth;
static u32  s_displayHeight = s_baseWindowHeight;
static u32  s_monitorWidth  = 1280;
static u32  s_monitorHeight = 720;
static char s_screenshotTime[TFE_MAX_PATH];
static s32  s_startupGame = -1;
static IGame* s_curGame = nullptr;
static const char* s_loadRequestFilename = nullptr;

void parseOption(const char* name, const std::vector<const char*>& values, bool longName);
bool validatePath();

void handleEvent(SDL_Event& Event)
{
	TFE_Ui::setUiInput(&Event);
	TFE_Settings_Window* windowSettings = TFE_Settings::getWindowSettings();

	switch (Event.type)
	{
		case SDL_QUIT:
		{
			TFE_System::logWrite(LOG_MSG, "Main", "App Quit");
			s_loop = false;
		} break;
		case SDL_WINDOWEVENT:
		{
			if (Event.window.event == SDL_WINDOWEVENT_RESIZED || Event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
			{
				TFE_RenderBackend::resize(Event.window.data1, Event.window.data2);
			}
		} break;
		case SDL_CONTROLLERDEVICEADDED:
		{
			const s32 cIdx = Event.cdevice.which;
			if (SDL_IsGameController(cIdx))
			{
				SDL_GameController* controller = SDL_GameControllerOpen(cIdx);
				SDL_Joystick* j = SDL_GameControllerGetJoystick(controller);
				SDL_JoystickID joyId = SDL_JoystickInstanceID(j);

				//Save the joystick id to used in the future events
				SDL_GameControllerOpen(0);
			}
		} break;
		case SDL_MOUSEBUTTONDOWN:
		{
			TFE_Input::setMouseButtonDown(MouseButton(Event.button.button - SDL_BUTTON_LEFT));
		} break;
		case SDL_MOUSEBUTTONUP:
		{
			TFE_Input::setMouseButtonUp(MouseButton(Event.button.button - SDL_BUTTON_LEFT));
		} break;
		case SDL_MOUSEWHEEL:
		{
			TFE_Input::setMouseWheel(Event.wheel.x, Event.wheel.y);
		} break;
		case SDL_KEYDOWN:
		{
			if (Event.key.keysym.scancode)
			{
				TFE_Input::setKeyDown(KeyboardCode(Event.key.keysym.scancode), Event.key.repeat != 0);
			}

			if (Event.key.keysym.scancode)
			{
				TFE_Input::setBufferedKey(KeyboardCode(Event.key.keysym.scancode));
			}
		} break;
		case SDL_KEYUP:
		{
			if (Event.key.keysym.scancode)
			{
				const KeyboardCode code = KeyboardCode(Event.key.keysym.scancode);
				TFE_Input::setKeyUp(KeyboardCode(Event.key.keysym.scancode));

				// Fullscreen toggle.
				bool altHeld = TFE_Input::keyDown(KEY_LALT) || TFE_Input::keyDown(KEY_RALT);
				if (code == KeyboardCode::KEY_F11 || (code == KeyboardCode::KEY_RETURN && altHeld))
				{
					windowSettings->fullscreen = !windowSettings->fullscreen;
					TFE_RenderBackend::enableFullscreen(windowSettings->fullscreen);
				}				
			}
		} break;
		case SDL_TEXTINPUT:
		{
			TFE_Input::setBufferedInput(Event.text.text);
		} break;
		case SDL_CONTROLLERAXISMOTION:
		{
			// Axis are now handled interally so the deadzone can be changed.
			if (Event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
			{ TFE_Input::setAxis(AXIS_LEFT_X, f32(Event.caxis.value) / 32768.0f); }
			else if (Event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
			{ TFE_Input::setAxis(AXIS_LEFT_Y, -f32(Event.caxis.value) / 32768.0f); }

			if (Event.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTX)
			{ TFE_Input::setAxis(AXIS_RIGHT_X, f32(Event.caxis.value) / 32768.0f); }
			else if (Event.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY)
			{ TFE_Input::setAxis(AXIS_RIGHT_Y, -f32(Event.caxis.value) / 32768.0f); }

			const s32 deadzone = 3200;
			if ((Event.caxis.value < -deadzone) || (Event.caxis.value > deadzone))
			{
				if (Event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT)
				{ TFE_Input::setAxis(AXIS_LEFT_TRIGGER, f32(Event.caxis.value) / 32768.0f); }
				if (Event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
				{ TFE_Input::setAxis(AXIS_RIGHT_TRIGGER, f32(Event.caxis.value) / 32768.0f); }
			}
			else
			{
				if (Event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT)
				{ TFE_Input::setAxis(AXIS_LEFT_TRIGGER, 0.0f); }
				if (Event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
				{ TFE_Input::setAxis(AXIS_RIGHT_TRIGGER, 0.0f); }
			}
		} break;
		case SDL_CONTROLLERBUTTONDOWN:
		{
			if (Event.cbutton.button < CONTROLLER_BUTTON_COUNT)
			{
				TFE_Input::setButtonDown(Button(Event.cbutton.button));
			}
		} break;
		case SDL_CONTROLLERBUTTONUP:
		{
			if (Event.cbutton.button < CONTROLLER_BUTTON_COUNT)
			{
				TFE_Input::setButtonUp(Button(Event.cbutton.button));
			}
		} break;
		default:
		{
		} break;
	}
}

bool sdlInit()
{
	const int code = SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO);
	if (code != 0) { return false; }

	TFE_Settings_Window* windowSettings = TFE_Settings::getWindowSettings();
	bool fullscreen    = windowSettings->fullscreen || TFE_Settings::getTempSettings()->forceFullscreen;
	s_displayWidth     = windowSettings->width;
	s_displayHeight    = windowSettings->height;
	s_baseWindowWidth  = windowSettings->baseWidth;
	s_baseWindowHeight = windowSettings->baseHeight;

	// Get the displays and their bounds.
	s_displayIndex = TFE_RenderBackend::getDisplayIndex(windowSettings->x, windowSettings->y);
	// Reset the display if the window is out of bounds.
	if (s_displayIndex < 0)
	{
		MonitorInfo mInfo;
		s_displayIndex = 0;
		TFE_RenderBackend::getDisplayMonitorInfo(0, &mInfo);

		windowSettings->x = mInfo.x;
		windowSettings->y = mInfo.y + 32;
		windowSettings->width  = min((s32)windowSettings->width,  mInfo.w);
		windowSettings->height = min((s32)windowSettings->height, mInfo.h);
		windowSettings->baseWidth  = windowSettings->width;
		windowSettings->baseHeight = windowSettings->height;
		TFE_Settings::writeToDisk();

		s_displayWidth     = windowSettings->width;
		s_displayHeight    = windowSettings->height;
		s_baseWindowWidth  = windowSettings->baseWidth;
		s_baseWindowHeight = windowSettings->baseHeight;
	}

	// Determine the display mode settings based on the desktop.
	SDL_DisplayMode mode = {};
	SDL_GetDesktopDisplayMode(s_displayIndex, &mode);
	s_refreshRate = (f32)mode.refresh_rate;

	if (fullscreen)
	{
		s_displayWidth  = mode.w;
		s_displayHeight = mode.h;
	}
	else
	{
		s_displayWidth  = std::min(s_displayWidth,  (u32)mode.w);
		s_displayHeight = std::min(s_displayHeight, (u32)mode.h);
	}

	s_monitorWidth  = mode.w;
	s_monitorHeight = mode.h;

#ifdef SDL_HINT_APP_NAME  // SDL 2.0.18+
	SDL_SetHint(SDL_HINT_APP_NAME, "The Force Engine");
#endif

	return true;
}

static AppState s_curState = APP_STATE_UNINIT;
static bool s_soundPaused = false;

void setAppState(AppState newState, int argc, char* argv[])
{
	const TFE_Settings_Graphics* config = TFE_Settings::getGraphicsSettings();

#if ENABLE_EDITOR == 1
	if (newState != APP_STATE_EDITOR)
	{
		TFE_Editor::disable();
	}
#endif

	switch (newState)
	{
	case APP_STATE_MENU:
	case APP_STATE_SET_DEFAULTS:
		break;
	case APP_STATE_EDITOR:

		if (validatePath())
		{
		#if ENABLE_EDITOR == 1
			TFE_Editor::enable();
		#endif
		}
		else
		{
			newState = APP_STATE_NO_GAME_DATA;
		}
		break;
	case APP_STATE_LOAD:
	{
		bool pathIsValid = validatePath();
		if (pathIsValid && s_loadRequestFilename)
		{
			newState = APP_STATE_GAME;
			TFE_FrontEndUI::setAppState(APP_STATE_GAME);

			TFE_Game* gameInfo = TFE_Settings::getGame();
			if (s_curGame)
			{
				freeGame(s_curGame);
				s_curGame = nullptr;
			}
			s_soundPaused = false;
			s_curGame = createGame(gameInfo->id);
			TFE_SaveSystem::setCurrentGame(s_curGame);
			if (!s_curGame)
			{
				TFE_System::logWrite(LOG_ERROR, "AppMain", "Cannot create game '%s'.", gameInfo->game);
				newState = APP_STATE_CANNOT_RUN;
			}
			else if (!TFE_SaveSystem::loadGame(s_loadRequestFilename))
			{
				TFE_System::logWrite(LOG_ERROR, "AppMain", "Cannot run game '%s'.", gameInfo->game);
				freeGame(s_curGame);
				s_curGame = nullptr;
				newState = APP_STATE_CANNOT_RUN;
			}
			else
			{
				TFE_Input::enableRelativeMode(true);
			}
		}
		else if (!pathIsValid)
		{
			newState = APP_STATE_NO_GAME_DATA;
		}
		else
		{
			newState = s_curState;
		}
		s_loadRequestFilename = nullptr;
	} break;
	case APP_STATE_GAME:
		if (validatePath())
		{
			TFE_Game* gameInfo = TFE_Settings::getGame();
			if (!s_curGame || gameInfo->id != s_curGame->id || startReplayStatus())
			{
				s_soundPaused = false;
				if (s_curGame)
				{
					freeGame(s_curGame);
					s_curGame = nullptr;
				}
				s_curGame = createGame(gameInfo->id);
				TFE_SaveSystem::setCurrentGame(s_curGame);
				if (!s_curGame)
				{
					TFE_System::logWrite(LOG_ERROR, "AppMain", "Cannot create game '%s'.", gameInfo->game);
					newState = APP_STATE_CANNOT_RUN;
				}
				else if (!s_curGame->runGame(argc, (const char**)argv, nullptr))
				{
					TFE_System::logWrite(LOG_ERROR, "AppMain", "Cannot run game '%s'.", gameInfo->game);
					freeGame(s_curGame);
					s_curGame = nullptr;
					newState = APP_STATE_CANNOT_RUN;
				}
				else
				{
					TFE_Input::enableRelativeMode(true);
				}
			}
		}
		else
		{
			newState = APP_STATE_NO_GAME_DATA;
		}
		break;
	};

	s_curState = newState;
}

bool systemMenuKeyCombo()
{
	return TFE_System::systemUiRequestPosted() || (inputMapping_getActionState(IAS_SYSTEM_MENU) == STATE_PRESSED);
}

void parseCommandLine(s32 argc, char* argv[])
{
	if (argc < 1) { return; }

	const char* curOptionName = nullptr;
	bool longName = false;
	std::vector<const char*> values;
	for (s32 i = 1; i < argc; i++)
	{
		const char* opt = argv[i];
		const size_t len = strlen(opt);

		TFE_System::logWrite(LOG_MSG, "Main", "Parsing parameter %s", opt);

		// Is this an option name or value?
		const char* optValue = nullptr;
		if (len && opt[0] == '-')
		{
			if (curOptionName)
			{
				parseOption(curOptionName, values, longName);
			}
			if (len > 2 && opt[0] == '-' && opt[1] == '-')
			{
				longName = true;
				curOptionName = opt + 2;
			}
			else
			{
				longName = false;
				curOptionName = opt + 1;
			}
			values.clear();
		}
		else if (len && opt[0] != '-')
		{
			values.push_back(opt);
		}
	}
	if (curOptionName)
	{
		parseOption(curOptionName, values, longName);
	}
}

void generateScreenshotTime()
{
#ifdef _WIN32
	__time64_t time;
	_time64(&time);
	const char* timeString = _ctime64(&time);
	if (timeString)
	{
		strcpy(s_screenshotTime, timeString);
	}

#else
	time_t tt = time(NULL);
	memset(s_screenshotTime, 0, 1024);
	strcpy(s_screenshotTime, ctime(&tt));
#endif
	// Replace ':' with '_'
	size_t len = strlen(s_screenshotTime);
	for (size_t i = 0; i < len; i++)
	{
		if (s_screenshotTime[i] == ':')
		{
			s_screenshotTime[i] = '_';
		}
		else if (s_screenshotTime[i] == ' ')
		{
			s_screenshotTime[i] = '-';
		}
		if (s_screenshotTime[i] == '\n')
		{
			s_screenshotTime[i] = 0;
			break;
		}
	}
}

bool validatePath()
{
	if (!TFE_Paths::hasPath(PATH_SOURCE_DATA)) { return false; }

	char testFile[TFE_MAX_PATH];
	// if (game->id == Game_Dark_Forces)
	{
		// Does DARK.GOB exist?
		sprintf(testFile, "%s%s", TFE_Paths::getPath(PATH_SOURCE_DATA), "DARK.GOB");
		if (!FileUtil::exists(testFile))
		{
			TFE_System::logWrite(LOG_ERROR, "Main", "Invalid game source path: '%s' - '%s' does not exist.", TFE_Paths::getPath(PATH_SOURCE_DATA), testFile);
			TFE_Paths::setPath(PATH_SOURCE_DATA, "");
		}
		else if (!GobArchive::validate(testFile, 130))
		{
			TFE_System::logWrite(LOG_ERROR, "Main", "Invalid game source path: '%s' - '%s' GOB is invalid, too few files.", TFE_Paths::getPath(PATH_SOURCE_DATA), testFile);
			TFE_Paths::setPath(PATH_SOURCE_DATA, "");
		}
	}
	return TFE_Paths::hasPath(PATH_SOURCE_DATA);
}

int main(int argc, char* argv[])
{
	#if INSTALL_CRASH_HANDLER
	TFE_CrashHandler::setProcessExceptionHandlers();
	TFE_CrashHandler::setThreadExceptionHandlers();
	#endif

	// Paths
	bool pathsSet = true;
	pathsSet &= TFE_Paths::setProgramPath();
	pathsSet &= TFE_Paths::setProgramDataPath("TheForceEngine");
	pathsSet &= TFE_Paths::setUserDocumentsPath("TheForceEngine");
	TFE_System::openRotatingLog("the_force_engine_log.txt");
	TFE_System::logWrite(LOG_MSG, "Main", "The Force Engine %s", c_gitVersion);
	if (!pathsSet)
	{
		TFE_System::logWrite(LOG_ERROR, "Main", "Cannot set paths.");
		return PROGRAM_ERROR;
	}

	// Before loading settings, read in the Input key lists.
	if (!TFE_Input::loadKeyNames("UI_Text/KeyText.txt"))
	{
		TFE_System::logWrite(LOG_ERROR, "Main", "Cannot load key names.");
		return PROGRAM_ERROR;
	}

	if (!TFE_System::loadMessages("UI_Text/TfeMessages.txt"))
	{
		TFE_System::logWrite(LOG_ERROR, "Main", "Cannot load TFE messages.");
		return PROGRAM_ERROR;
	}

	// Initialize settings so that the paths can be read.
	bool firstRun;
	if (!TFE_Settings::init(firstRun))
	{
		TFE_System::logWrite(LOG_ERROR, "Main", "Cannot load settings.");
		return PROGRAM_ERROR;
	}

	// Override settings with command line options.
	parseCommandLine(argc, argv);

	// Setup game paths.
	// Get the current game.
	const TFE_Game* game = TFE_Settings::getGame();
	const TFE_GameHeader* gameHeader = TFE_Settings::getGameHeader(game->game);
	TFE_Paths::setPath(PATH_SOURCE_DATA, gameHeader->sourcePath);
	TFE_Paths::setPath(PATH_EMULATOR, gameHeader->emulatorPath);
	TFE_Paths::setRemasterDocsPath(game->id);

	// Validate the current game path.
	validatePath();

	TFE_System::logWrite(LOG_MSG, "Paths", "Program Path: \"%s\"",   TFE_Paths::getPath(PATH_PROGRAM));
	TFE_System::logWrite(LOG_MSG, "Paths", "Program Data: \"%s\"",   TFE_Paths::getPath(PATH_PROGRAM_DATA));
	TFE_System::logWrite(LOG_MSG, "Paths", "User Documents: \"%s\"", TFE_Paths::getPath(PATH_USER_DOCUMENTS));
	TFE_System::logWrite(LOG_MSG, "Paths", "Source Data: \"%s\"",    TFE_Paths::getPath(PATH_SOURCE_DATA));

	// Create a screenshot directory
	char screenshotDir[TFE_MAX_PATH];
	TFE_Paths::appendPath(TFE_PathType::PATH_USER_DOCUMENTS, "Screenshots/", screenshotDir);
	if (!FileUtil::directoryExists(screenshotDir))
	{
		FileUtil::makeDirectory(screenshotDir);
	}

	// Create a mods temporary directory.
	char tempPath[TFE_MAX_PATH];
	sprintf(tempPath, "%sTemp/", TFE_Paths::getPath(PATH_PROGRAM_DATA));
	if (!FileUtil::directoryExists(tempPath))
	{
		FileUtil::makeDirectory(tempPath);
	}
	generateScreenshotTime();

	// Create Replay Directory
	initReplays();

	// Initialize SDL
	if (!sdlInit())
	{
		TFE_System::logWrite(LOG_CRITICAL, "SDL", "Cannot initialize SDL.");
		TFE_System::logClose();
		return PROGRAM_ERROR;
	}
	TFE_Settings_Window* windowSettings = TFE_Settings::getWindowSettings();
	TFE_Settings_Graphics* graphics = TFE_Settings::getGraphicsSettings();
	TFE_System::init(s_refreshRate, graphics->vsync, c_gitVersion);

	// Setup the GPU Device and Window.
	u32 windowFlags = 0;
	if (windowSettings->fullscreen || TFE_Settings::getTempSettings()->forceFullscreen)
	{
		TFE_System::logWrite(LOG_MSG, "Display", "Fullscreen enabled.");
		windowFlags |= WINFLAG_FULLSCREEN;
	}
	if (graphics->vsync) { TFE_System::logWrite(LOG_MSG, "Display", "Vertical Sync enabled."); windowFlags |= WINFLAG_VSYNC; }
	
	WindowState windowState =
	{
		"",
		s_displayWidth,
		s_displayHeight,
		s_baseWindowWidth,
		s_baseWindowHeight,
		s_monitorWidth,
		s_monitorHeight,
		windowFlags,
		s_refreshRate
	};
	sprintf(windowState.name, "The Force Engine  %s", TFE_System::getVersionString());
	if (!TFE_RenderBackend::init(windowState))
	{
		TFE_System::logWrite(LOG_CRITICAL, "GPU", "Cannot initialize GPU/Window.");
		TFE_System::logClose();
		return PROGRAM_ERROR;
	}
	TFE_FrontEndUI::initConsole();
	TFE_Audio::init(s_nullAudioDevice, TFE_Settings::getSoundSettings()->audioDevice);
	TFE_MidiPlayer::init(TFE_Settings::getSoundSettings()->midiOutput, (MidiDeviceType)TFE_Settings::getSoundSettings()->midiType);
	TFE_Image::init();
	TFE_Palette::createDefault256();
	TFE_FrontEndUI::init();
	game_init();
	inputMapping_startup();
	TFE_SaveSystem::init();
	TFE_A11Y::init();

	// Uncomment to test memory region allocator.
	// TFE_Memory::region_test();

	// Color correction.
	const ColorCorrection colorCorrection = { graphics->brightness, graphics->contrast, graphics->saturation, graphics->gamma };
	TFE_RenderBackend::setColorCorrection(graphics->colorCorrection, &colorCorrection);

	// Optional Reticle.
	reticle_init();
	// Scripting system.
	TFE_ForceScript::init();
		
	// Start up the game and skip the title screen.
	if (firstRun)
	{
		TFE_FrontEndUI::setAppState(APP_STATE_SET_DEFAULTS);
	}
	else if (s_startupGame >= Game_Dark_Forces && validatePath())
	{
		TFE_FrontEndUI::setAppState(APP_STATE_GAME);
	}

	// Try to set the game right away, so the load menu works.
	TFE_Game* gameInfo = TFE_Settings::getGame();
	TFE_SaveSystem::setCurrentGame(gameInfo->id);

	// Setup the framelimiter.
	TFE_System::frameLimiter_set(graphics->frameRateLimit);

	// Start reading the mods immediately?
	TFE_FrontEndUI::modLoader_read();

	// Game loop
	u32 frame = 0u;
	bool showPerf = false;
	bool relativeMode = false;
	bool minimized = false;
	TFE_System::logWrite(LOG_MSG, "Progam Flow", "The Force Engine Game Loop Started");
	while (s_loop && !TFE_System::quitMessagePosted())
	{
		minimized = TFE_RenderBackend::isWindowMinimized();

		TFE_FRAME_BEGIN();
		TFE_System::frameLimiter_begin();
		bool enableRelative = TFE_Input::relativeModeEnabled();
		if (enableRelative != relativeMode)
		{
			static bool showRelativeErrorOnce = true;
			const s32 result = SDL_SetRelativeMouseMode(enableRelative ? SDL_TRUE : SDL_FALSE);
			if (result >= 0)
			{
				relativeMode = enableRelative;
			}
			else if (showRelativeErrorOnce)
			{
				TFE_System::logWrite(LOG_ERROR, "System", "Changing relative mouse mode failed!");
				showRelativeErrorOnce = false;
			}
		}

		// System events
		SDL_Event event;
		while (SDL_PollEvent(&event)) { handleEvent(event); }

		// Inputs Main Entry - skip frame any further processing during replay pause
		if (!inputMapping_handleInputs())
		{
			TFE_Input::endFrame();
			inputMapping_endFrame();
			continue;
		}

		// Can we save?
		TFE_FrontEndUI::setCanSave(s_curGame ? s_curGame->canSave() : false);

		// Update the System UI.
		AppState appState = TFE_FrontEndUI::update();
		s_loadRequestFilename = TFE_SaveSystem::loadRequestFilename();
		if (s_loadRequestFilename)
		{
			appState = APP_STATE_LOAD;
		}

		if (appState == APP_STATE_QUIT)
		{
			s_loop = false;
		}
		else if (appState != s_curState)
		{
			if (appState == APP_STATE_EXIT_TO_MENU)	// Return to the menu from the game.
			{
				if (s_curGame)
				{
					freeGame(s_curGame);
					s_curGame = nullptr;
				}
				s_soundPaused = false;
				appState = APP_STATE_MENU;
			}

			char* selectedMod = TFE_FrontEndUI::getSelectedMod();
			if (selectedMod && selectedMod[0] && appState == APP_STATE_GAME)
			{				

				// Handle mod overrides and setings including calls from replay module
				char* newArgs[16];
				newArgs[0] = argv[0];

				std::vector<std::string> modOverrides;
				modOverrides = TFE_FrontEndUI::getModOverrides();

				size_t newArgc = 0;
				newArgs[newArgc] = argv[newArgc];
				size_t modOverrideSize = modOverrides.size();
				newArgc += modOverrideSize + 1;
				if (modOverrideSize > 0)
				{
					for (s32 i = 0; i < modOverrides.size(); i++)
					{
						newArgs[i + 1] = new char[modOverrides[i].size() + 1];
						std::strcpy(newArgs[i + 1], modOverrides[i].c_str());
					}
				}
				else
				{
					for (s32 i = 1; i < argc && i < 15; i++)
					{
						newArgs[i] = argv[i];
					}
				}
				newArgs[newArgc] = selectedMod;
				setAppState(appState, (s32)newArgc + 1, newArgs);
			}
			else
			{
				setAppState(appState, argc, argv);
			}
		}

		if (TFE_A11Y::hasPendingFont()) { TFE_A11Y::loadPendingFont(); } // Can't load new fonts between TFE_Ui::begin() and TFE_Ui::render();
		TFE_Ui::begin();
		TFE_System::update();
		TFE_ForceScript::update();

		// Update
		if (TFE_FrontEndUI::uiControlsEnabled() && task_canRun())
		{
			if (TFE_FrontEndUI::isConsoleOpen() && !TFE_FrontEndUI::isConsoleAnimating() && TFE_Input::keyPressed(KEY_ESCAPE))
			{
				TFE_FrontEndUI::toggleConsole();
				// "Eat" the key so it doesn't extend to the Escape menu.
				TFE_Input::clearKeyPressed(KEY_ESCAPE);
				inputMapping_clearKeyBinding(KEY_ESCAPE);
				if (s_curGame)
				{
					s_curGame->pauseGame(false);
					TFE_Input::enableRelativeMode(true);
				}
			}
			else if (inputMapping_getActionState(IAS_CONSOLE) == STATE_PRESSED)
			{
				bool isOpening = TFE_FrontEndUI::toggleConsole();
				if (s_curGame)
				{
					s_curGame->pauseGame(isOpening);
					TFE_Input::enableRelativeMode(!isOpening);
				}
			}
			else if (TFE_Input::keyPressed(KEY_F9) && TFE_Input::keyDown(KEY_LALT))
			{
				showPerf = !showPerf;
			}
			else if (TFE_Input::keyPressed(KEY_F10) && TFE_Input::keyDown(KEY_LALT))
			{
				TFE_FrontEndUI::toggleProfilerView();
			}

			bool toggleSystemMenu = systemMenuKeyCombo();
			if (TFE_FrontEndUI::isConfigMenuOpen() && (toggleSystemMenu || TFE_Input::keyPressed(KEY_ESCAPE)))
			{
				// "Eat" the escape key so it doesn't also open the Escape menu.
				TFE_Input::clearKeyPressed(KEY_ESCAPE);
				inputMapping_clearKeyBinding(KEY_ESCAPE);

				s_curState = TFE_FrontEndUI::menuReturn();

				if ((s_soundPaused || TFE_Settings::getSoundSettings()->disableSoundInMenus) && s_curGame)
				{
					s_curGame->pauseSound(false);
					s_soundPaused = false;
				}
			}
			else if (toggleSystemMenu)
			{
				TFE_FrontEndUI::enableConfigMenu();
				TFE_FrontEndUI::setMenuReturnState(s_curState);

				if (TFE_Settings::getSoundSettings()->disableSoundInMenus && s_curGame)
				{
					s_curGame->pauseSound(true);
					s_soundPaused = true;
				}
			}
			else if (s_soundPaused && !TFE_FrontEndUI::isConfigMenuOpen())
			{
				if (s_curGame)
				{
					s_curGame->pauseSound(false);
				}
				s_soundPaused = false;
			}
		}

		// Take screenshot handler
		if (inputMapping_getActionState(IADF_SCREENSHOT) == STATE_PRESSED)
		{
			static u64 _screenshotIndex = 0;

			char screenshotDir[TFE_MAX_PATH];
			TFE_Paths::appendPath(TFE_PathType::PATH_USER_DOCUMENTS, "Screenshots/", screenshotDir);

			char screenshotPath[TFE_MAX_PATH];
			sprintf(screenshotPath, "%stfe_screenshot_%s_%" PRIu64 ".png", screenshotDir, s_screenshotTime, _screenshotIndex);
			_screenshotIndex++;

			TFE_RenderBackend::queueScreenshot(screenshotPath);
		}

		bool pressedRecordNoCountdown = inputMapping_getActionState(IADF_GIF_RECORD_NO_COUNTDOWN) == STATE_PRESSED;
		// Gif recording handler
		if (inputMapping_getActionState(IADF_GIF_RECORD) == STATE_PRESSED || pressedRecordNoCountdown)
		{
			static u64 _gifIndex = 0;
			static bool _recording = false;

			if (!_recording)
			{
				char screenshotDir[TFE_MAX_PATH];
				TFE_Paths::appendPath(TFE_PathType::PATH_USER_DOCUMENTS, "Screenshots/", screenshotDir);

				char gifPath[TFE_MAX_PATH];
				sprintf(gifPath, "%stfe_gif_%s_%" PRIu64 ".gif", screenshotDir, s_screenshotTime, _gifIndex);
				_gifIndex++;

				TFE_RenderBackend::startGifRecording(gifPath, pressedRecordNoCountdown);
				_recording = true;
			}
			else
			{
				TFE_RenderBackend::stopGifRecording();
				_recording = false;
			}
		}

		const bool isConsoleOpen = TFE_FrontEndUI::isConsoleOpen();
		bool endInputFrame = true;
		if (s_curState == APP_STATE_EDITOR)
		{
		#if ENABLE_EDITOR == 1
			if (TFE_Editor::update(isConsoleOpen, minimized))
			{
				TFE_FrontEndUI::setAppState(APP_STATE_MENU);
			}
		#endif
		}
		else if (s_curState == APP_STATE_GAME)
		{
			if (!s_curGame)
			{
				s_curState = APP_STATE_MENU;
			}
			else
			{
				TFE_SaveSystem::update();
				s_curGame->loopGame();
				endInputFrame = TFE_Jedi::task_run() != 0;
			}
		}
		else
		{
			TFE_RenderBackend::clearWindow();
		}

		bool drawFps =  s_curGame&& graphics->showFps;
		if (s_curGame) { drawFps = drawFps && (!s_curGame->isPaused()); }

		TFE_FrontEndUI::setCurrentGame(s_curGame);
		TFE_FrontEndUI::draw(s_curState == APP_STATE_MENU || s_curState == APP_STATE_NO_GAME_DATA || s_curState == APP_STATE_SET_DEFAULTS,
			s_curState == APP_STATE_NO_GAME_DATA, s_curState == APP_STATE_SET_DEFAULTS, drawFps);

		// Make sure the clear the no game data state if the data becomes valid.
		if (TFE_FrontEndUI::isNoDataMessageSet() && validatePath())
		{
			TFE_FrontEndUI::clearNoDataState();
		}

		bool swap = s_curState != APP_STATE_EDITOR && (s_curState != APP_STATE_MENU || TFE_FrontEndUI::isConfigMenuOpen());
	#if ENABLE_EDITOR == 1
		if (s_curState == APP_STATE_EDITOR)
		{
			swap = TFE_Editor::render();
		}
	#endif

		// Blit the frame to the window and draw UI.
		TFE_RenderBackend::swap(swap);

		// Handle framerate limiter.
		TFE_System::frameLimiter_end();

		// Clear transitory input state.
		if (endInputFrame)
		{
			TFE_Input::endFrame();
			inputMapping_endFrame();
		}
		frame++;

		if (endInputFrame)
		{
			TFE_FRAME_END();
		}
	}	

#if ENABLE_EDITOR == 1
	if (s_curState == APP_STATE_EDITOR)
	{
		bool canExit = TFE_Editor::disable();
		while (!canExit)
		{
			//////////////////////////////////////////////////////
			TFE_FRAME_BEGIN();

			// System events
			SDL_Event event;
			while (SDL_PollEvent(&event)) { handleEvent(event); }

			TFE_Ui::begin();
			TFE_System::update();

			TFE_Editor::update(false, false, /*exiting*/true);
			bool swap = TFE_Editor::render();

			// Blit the frame to the window and draw UI.
			TFE_RenderBackend::swap(swap);

			// Clear transitory input state.
			TFE_Input::endFrame();
			inputMapping_endFrame();
			frame++;
			TFE_FRAME_END();

			//////////////////////////////////////////////////////
			canExit = TFE_Editor::disable();
		}
	}
#endif

	if (s_curGame)
	{
		freeGame(s_curGame);
		s_curGame = nullptr;
	}
	s_soundPaused = false;
	game_destroy();
	reticle_destroy();
	inputMapping_shutdown();

	// Cleanup
	TFE_FrontEndUI::shutdown();
	TFE_Audio::shutdown();
	TFE_MidiPlayer::destroy();
	TFE_Image::shutdown();
	TFE_Palette::freeAll();
	TFE_RenderBackend::updateSettings();
	TFE_Settings::shutdown();
	TFE_Jedi::texturepacker_freeGlobal();
	TFE_RenderBackend::destroy();
	TFE_SaveSystem::destroy();
	TFE_ForceScript::destroy();
	SDL_Quit();
		
	TFE_System::logWrite(LOG_MSG, "Progam Flow", "The Force Engine Game Loop Ended.");
	TFE_System::logClose();
	TFE_System::freeMessages();
	return PROGRAM_SUCCESS;
}

void parseOption(const char* name, const std::vector<const char*>& values, bool longName)
{
	if (!longName)	// short names use the same style as the originals.
	{
		if (name[0] == 'g')		// Directly load a game, skipping the titlescreen.
		{
			// -gDARK
			const char* gameToLoad = &name[1];
			TFE_System::logWrite(LOG_MSG, "CommandLine", "Game to load: %s", gameToLoad);
			if (!strcasecmp(gameToLoad, "dark"))
			{
				s_startupGame = Game_Dark_Forces;
			}
		}
		else if (name[0] == 'r')
		{
			// -r<replay_path>
			TFE_Input::loadReplayFromPath(&name[1]);
		}
		else if (strcasecmp(name, "nosound") == 0)
		{
			// -noaudio
			s_nullAudioDevice = true;
		}
		else if (strcasecmp(name, "fullscreen") == 0)
		{
			TFE_Settings::getTempSettings()->forceFullscreen = true;
		}
		else if (strcasecmp(name, "skip_load_delay") == 0)
		{
			TFE_Settings::getTempSettings()->skipLoadDelay = true;
		}
	}
	else  // long names use the more traditional style of arguments which allow for multiple values.
	{
		if (strcasecmp(name, "game") == 0 && values.size() >= 1)	// Directly load a game, skipping the titlescreen.
		{
			// --game DARK
			const char* gameToLoad = values[0];
			TFE_System::logWrite(LOG_MSG, "CommandLine", "Game to load: %s", gameToLoad);
			if (!strcasecmp(gameToLoad, "dark"))
			{
				s_startupGame = Game_Dark_Forces;
			}
		}
		else if (strcasecmp(name, "nosound") == 0)
		{
			// --noaudio
			s_nullAudioDevice = true;
		}
		else if (strcasecmp(name, "fullscreen") == 0)
		{
			TFE_Settings::getTempSettings()->forceFullscreen = true;
		}
		else if (strcasecmp(name, "skip_load_delay") == 0)
		{
			TFE_Settings::getTempSettings()->skipLoadDelay = true;
		}
		else if (strcasecmp(name, "demo_logging") == 0)
		{
			TFE_Settings::getTempSettings()->df_demologging = true;
		}
		else if (strcasecmp(name, "exit_after_replay") == 0)
		{
			TFE_Settings::getTempSettings()->exit_after_replay = true;
		}
	}
}