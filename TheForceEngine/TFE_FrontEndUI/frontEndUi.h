#pragma once
//////////////////////////////////////////////////////////////////////
// The Force Engine Editor
// A system built to view and edit Dark Forces data files.
// The viewing aspect needs to be put in place at the beginning
// in order to properly test elements in isolation without having
// to "play" the game as intended.
//////////////////////////////////////////////////////////////////////

#include <TFE_System/types.h>
#include <vector>
#include <string>

enum AppState
{
	APP_STATE_MENU = 0,
	APP_STATE_EDITOR,
	APP_STATE_LOAD,
	APP_STATE_GAME,
	APP_STATE_QUIT,
	APP_STATE_NO_GAME_DATA,
	APP_STATE_CANNOT_RUN,
	APP_STATE_EXIT_TO_MENU,
	APP_STATE_SET_DEFAULTS,
	APP_STATE_COUNT,
	APP_STATE_UNINIT = APP_STATE_COUNT
};

struct ImFont;
struct IGame;

namespace TFE_FrontEndUI
{
	struct UiImage
	{
		void* image = nullptr;	// ImTextureID-compatible GPU handle, null if unavailable.
		u32   width = 0;
		u32   height = 0;
	};

	bool loadGpuImage(const char* localPath, UiImage* uiImage);

	void init();
	void initConsole();
	void shutdown();

	AppState update();
	void draw(bool drawFrontEnd = true, bool noGameData = false, bool setDefaults = false, bool showFps = false);
	void setCurrentGame(IGame* game);
	IGame* getCurrentGame();

	void setAppState(AppState state);
	void enableConfigMenu();
	AppState menuReturn();
	void setMenuReturnState(AppState state);
	bool toggleConsole();
	void exitToMenu();

	bool shouldClearScreen();

	bool isConfigMenuOpen();
	bool isConsoleOpen();
	bool isConsoleAnimating();
	void logToConsole(const char* str);
	bool uiControlsEnabled();
	bool toggleEnhancements();

	char* getSelectedMod();
	void  clearSelectedMod();
	int getRecordFramerate();
	std::string getPlaybackFramerate();
	void  setModOverrides(std::vector<std::string>&);
	std::vector<std::string> getModOverrides();
	void  setSelectedMod(const char* mod);
	void* getGradientTexture();
	void  setState(AppState state);
	void  clearMenuState();
	void  clearNoDataState();
	bool  isNoDataMessageSet();
	ImFont* getDialogFont();

	void setCanSave(bool canSave);
	bool getCanSave();

	void toggleProfilerView();
	void drawFps(s32 windowWidth);

	bool isModUI();
}