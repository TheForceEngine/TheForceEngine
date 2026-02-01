#include "remasterCutscenes.h"
#include <TFE_DarkForces/Landru/cutsceneList.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_Settings/settings.h>
#include <TFE_System/system.h>
#ifdef _WIN32
#include <TFE_Settings/windows/registry.h>
#endif
#include <TFE_Settings/gameSourceData.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace TFE_DarkForces
{
	static bool s_initialized = false;
	static bool s_available = false;
	static std::string s_videoBasePath;
	static std::string s_subtitleBasePath;
	static char s_videoPathResult[TFE_MAX_PATH];
	static char s_subtitlePathResult[TFE_MAX_PATH];

	// "ARCFLY.LFD" -> "arcfly"
	static std::string archiveToBaseName(const char* archive)
	{
		std::string name(archive);
		for (size_t i = 0; i < name.size(); i++)
		{
			name[i] = (char)tolower((u8)name[i]);
		}
		size_t dot = name.rfind(".lfd");
		if (dot != std::string::npos)
		{
			name = name.substr(0, dot);
		}
		return name;
	}

	static const char* s_subdirNames[] = { "movies/", "Cutscenes/" };
	static const int s_subdirCount = 2;

	static bool tryBasePath(const char* basePath)
	{
		char testPath[TFE_MAX_PATH];
		for (int i = 0; i < s_subdirCount; i++)
		{
			snprintf(testPath, TFE_MAX_PATH, "%s%s", basePath, s_subdirNames[i]);
			if (FileUtil::directoryExits(testPath))
			{
				s_videoBasePath = testPath;
				TFE_System::logWrite(LOG_MSG, "Remaster", "Found remaster cutscenes at: %s", testPath);
				return true;
			}
		}
		return false;
	}

	static bool detectVideoPath()
	{
		// Custom path from settings.
#ifdef ENABLE_OGV_CUTSCENES
		const TFE_Settings_Game* gameSettings = TFE_Settings::getGameSettings();
		if (gameSettings->df_remasterCutscenesPath[0])
		{
			std::string custom = gameSettings->df_remasterCutscenesPath;
			if (custom.back() != '/' && custom.back() != '\\') { custom += '/'; }
			if (FileUtil::directoryExits(custom.c_str()))
			{
				s_videoBasePath = custom;
				TFE_System::logWrite(LOG_MSG, "Remaster", "Using custom cutscene path: %s", custom.c_str());
				return true;
			}
		}
#endif

		// Remaster docs path.
		if (TFE_Paths::hasPath(PATH_REMASTER_DOCS))
		{
			if (tryBasePath(TFE_Paths::getPath(PATH_REMASTER_DOCS)))
				return true;
		}

		// Source data path.
		const char* sourcePath = TFE_Settings::getGameHeader("Dark Forces")->sourcePath;
		if (sourcePath && sourcePath[0])
		{
			if (tryBasePath(sourcePath))
				return true;
		}

		// Steam registry lookup (Windows).
#ifdef _WIN32
		{
			char remasterPath[TFE_MAX_PATH] = {};
			if (WindowsRegistry::getSteamPathFromRegistry(
				TFE_Settings::c_steamRemasterProductId[Game_Dark_Forces],
				TFE_Settings::c_steamRemasterLocalPath[Game_Dark_Forces],
				TFE_Settings::c_steamRemasterLocalSubPath[Game_Dark_Forces],
				TFE_Settings::c_validationFile[Game_Dark_Forces],
				remasterPath))
			{
				if (tryBasePath(remasterPath))
					return true;
			}
			// TM variant path.
			if (WindowsRegistry::getSteamPathFromRegistry(
				TFE_Settings::c_steamRemasterProductId[Game_Dark_Forces],
				TFE_Settings::c_steamRemasterTMLocalPath[Game_Dark_Forces],
				TFE_Settings::c_steamRemasterLocalSubPath[Game_Dark_Forces],
				TFE_Settings::c_validationFile[Game_Dark_Forces],
				remasterPath))
			{
				if (tryBasePath(remasterPath))
					return true;
			}
		}
#endif

		// Program directory.
		if (tryBasePath(TFE_Paths::getPath(PATH_PROGRAM)))
			return true;

		return false;
	}

	static void detectSubtitlePath()
	{
		if (s_videoBasePath.empty()) { return; }

		char testPath[TFE_MAX_PATH];
		snprintf(testPath, TFE_MAX_PATH, "%sSubtitles/", s_videoBasePath.c_str());
		if (FileUtil::directoryExits(testPath))
		{
			s_subtitleBasePath = testPath;
			return;
		}

		// Fall back to same directory as videos.
		s_subtitleBasePath = s_videoBasePath;
	}

	void remasterCutscenes_init()
	{
		if (s_initialized) { return; }
		s_initialized = true;
		s_available = false;

		if (detectVideoPath())
		{
			s_available = true;
			detectSubtitlePath();
			TFE_System::logWrite(LOG_MSG, "Remaster", "Remaster OGV cutscene directory found.");
		}
		else
		{
			TFE_System::logWrite(LOG_MSG, "Remaster", "No remaster cutscene directory found; using original LFD cutscenes.");
		}
	}

	bool remasterCutscenes_available()
	{
		return s_available;
	}

	const char* remasterCutscenes_getVideoPath(const CutsceneState* scene)
	{
		if (!s_available || !scene) { return nullptr; }

		std::string baseName = archiveToBaseName(scene->archive);
		if (baseName.empty()) { return nullptr; }

		snprintf(s_videoPathResult, TFE_MAX_PATH, "%s%s.ogv", s_videoBasePath.c_str(), baseName.c_str());
		if (FileUtil::exists(s_videoPathResult))
		{
			return s_videoPathResult;
		}
		return nullptr;
	}

	const char* remasterCutscenes_getSubtitlePath(const CutsceneState* scene)
	{
		if (!s_available || !scene || s_subtitleBasePath.empty()) { return nullptr; }

		std::string baseName = archiveToBaseName(scene->archive);
		if (baseName.empty()) { return nullptr; }

		// Try language-specific subtitle first.
		const TFE_Settings_A11y* a11y = TFE_Settings::getA11ySettings();
		snprintf(s_subtitlePathResult, TFE_MAX_PATH, "%s%s.%s.srt",
			s_subtitleBasePath.c_str(), baseName.c_str(), a11y->language.c_str());
		if (FileUtil::exists(s_subtitlePathResult))
		{
			return s_subtitlePathResult;
		}

		// Fall back to default (no language suffix).
		snprintf(s_subtitlePathResult, TFE_MAX_PATH, "%s%s.srt",
			s_subtitleBasePath.c_str(), baseName.c_str());
		if (FileUtil::exists(s_subtitlePathResult))
		{
			return s_subtitlePathResult;
		}

		return nullptr;
	}

	void remasterCutscenes_setCustomPath(const char* path)
	{
		if (!path || !path[0])
		{
			s_videoBasePath.clear();
			s_available = false;
			return;
		}

		s_videoBasePath = path;
		if (s_videoBasePath.back() != '/' && s_videoBasePath.back() != '\\')
		{
			s_videoBasePath += '/';
		}

		s_available = FileUtil::directoryExits(s_videoBasePath.c_str());
		if (s_available)
		{
			detectSubtitlePath();
		}
	}
}
