#include "remasterCutscenes.h"
#include <TFE_DarkForces/Landru/cutsceneList.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_Settings/settings.h>
#include <TFE_System/system.h>
#include <TFE_System/utf8.h>
#ifdef _WIN32
#include <TFE_Settings/windows/registry.h>
#endif
#include <TFE_Settings/gameSourceData.h>
#include <TFE_Archive/zipArchive.h>
#include <cstdio>
#include <cstring>
#include <string>


//============================================================================
// Remastered cutscene path resolution
//============================================================================
//
// The remaster (NightDive's Kex engine port, codename "khonsu") packages
// its OGV cutscenes alongside the original DOS game data. Depending on how
// the user got it, the actual filesystem layout is one of:
//
//   Steam install:   <SteamApps>/common/STAR WARS Dark Forces Remaster/
//                      dark.gob                      (original DF data)
//                      DarkEX.kpf                    (zip with DCSS scripts)
//                      movies/<scene>.ogv            (the video files)
//                      movies/<scene>_<lang>.ogv     (localized variants)
//   GOG install:     similar, different root
//   Custom:          user-specified via df_remasterCutscenesPath
//
// DCSS scripts live inside DarkEX.kpf at cutscene_scripts/*.dcss. For TFE
// modding purposes we extract them to a filesystem sibling of movies/, so
// our on-disk layout looks like:
//
//   <root>/
//      movies/<scene>.ogv
//      cutscene_scripts/<scene>.dcss
//      Subtitles/<scene>_<lang>.srt     (optional)
//
// This file's job: given a CutsceneState from cutscene.lst, figure out the
// three concrete file paths for its OGV / DCSS / SRT.
//
namespace TFE_DarkForces
{
	// ------------------------------------------------------------------
	// Module state
	// ------------------------------------------------------------------
	// All singletons. Cutscenes are driven serially, so we don't need
	// per-caller state. The three static char buffers below get reused
	// across every call to getVideoPath / getDcssPath / getSubtitlePath,
	// so callers must consume the returned pointer before asking again.
	static bool s_initialized = false;
	static bool s_available = false;	

	// Directory paths (with trailing slash) where we found each kind of
	// file. Cached at init time so per-cutscene lookups are fast.
	static std::string s_videoBasePath;
	static std::string s_scriptBasePath;
	static std::string s_subtitleBasePath;

	// Return buffers. Static so we can hand a const char* back to the
	// caller without transferring ownership. A bit old-school, but
	// matches the rest of TFE's path-handling conventions.
	static char s_videoPathResult[TFE_MAX_PATH];
	static char s_scriptPathResult[TFE_MAX_PATH];
	static char s_subtitlePathResult[TFE_MAX_PATH];
	static char s_remasterBasePath[TFE_MAX_PATH];

	// ------------------------------------------------------------------
	// Name utilities
	// ------------------------------------------------------------------

	// ASCII lowercase, bounded to maxLen (the size of the source buffer).
	// We use this for filename normalization since the remaster's files
	// are all lowercase on disk but cutscene.lst's archive field is
	// uppercase (e.g. "ARCFLY.LFD").
	static std::string lower(const char* src, size_t maxLen)
	{
		std::string out;
		out.reserve(maxLen);
		for (size_t i = 0; i < maxLen && src[i]; i++)
		{
			out.push_back((char)tolower((u8)src[i]));
		}
		return out;
	}

	// "ARCFLY.LFD" or "arcfly" -> "arcfly".
	//
	// The remaster keys its lookups on the scene *name* (column 3 of
	// cutscene.lst), not the archive name. For stock data those are
	// always the same string anyway (ARCFLY.LFD holds the "arcfly"
	// scene), but they could diverge in a mod. Prefer scene name; fall
	// back to archive basename so we don't regress on mods that happen
	// to set scene="".
	static std::string sceneBaseName(const CutsceneState* scene)
	{
		if (!scene) { return {}; }
		if (scene->scene[0]) { return lower(scene->scene, sizeof(scene->scene)); }

		std::string name = lower(scene->archive, sizeof(scene->archive));
		size_t dot = name.rfind(".lfd");
		if (dot != std::string::npos) { name = name.substr(0, dot); }
		return name;
	}

	// ------------------------------------------------------------------
	// Video path detection
	// ------------------------------------------------------------------
	//
	// The remaster stores videos in either a "movies/" or "Cutscenes/"
	// subdirectory depending on which release you have. Try both.
	static const char* s_subdirNames[] = { "movies/", "Cutscenes/" };
	static const int s_subdirCount = 2;

	// Given a candidate root, check if either subdirectory exists and
	// looks like our video layout. Returns true on the first hit and
	// caches the full path (including trailing slash) in s_videoBasePath.
	static bool tryBasePath(const char* basePath)
	{
		char testPath[TFE_MAX_PATH];
		for (int i = 0; i < s_subdirCount; i++)
		{
			snprintf(testPath, TFE_MAX_PATH, "%s%s", basePath, s_subdirNames[i]);
			if (FileUtil::directoryExists(testPath))
			{
				s_videoBasePath = testPath;
				TFE_System::logWrite(LOG_MSG, "Remaster", "Found remaster cutscenes at: %s", testPath);
				return true;
			}
		}
		return false;
	}

	// Try every plausible location for the remaster data, in a defined
	// priority order. First hit wins.
	//
	// Priority rationale:	
	//   1. Get remaster path from Source Data you choose in the settings menu.
	//   2. PATH_REMASTER_DOCS is a platform-specific override TFE uses on
	//      consoles / packaged distributions.
	//   3. sourcePath lets the user install the remaster to whatever
	//      directory they want without hardcoding a registry lookup.
	//   4. Registry lookup catches the common Steam/GOG install locations
	//      on Windows without requiring config.
	//   5. Program directory is a last-ditch "they dropped files next to
	//      the EXE" case.
	static bool detectVideoPath()
	{
		// 1. Get remaster path from Source Data you choose in the settings menu.
		if (TFE_Paths::hasPath(PATH_SOURCE_DATA))
		{
			if (tryBasePath(TFE_Paths::getPath(PATH_SOURCE_DATA)))
				return true;
		}

		// 2. Platform-configured remaster docs path (currently unused on
		//    desktop; retained for console builds).
		if (TFE_Paths::hasPath(PATH_REMASTER_DOCS))
		{
			if (tryBasePath(TFE_Paths::getPath(PATH_REMASTER_DOCS)))
				return true;
		}

		// 3. Same sourcePath they use for the original Dark Forces. If
		//    they pointed it at the remaster install, movies/ will be
		//    right there.
		const char* sourcePath = TFE_Settings::getGameHeader("Dark Forces")->sourcePath;
		if (sourcePath && sourcePath[0])
		{
			if (tryBasePath(sourcePath))
				return true;
		}

#ifdef _WIN32
		// 4. Windows registry: check both the standard Steam install and
		//    the "TM" (trademark) variant that was briefly used. GOG has
		//    its own registry entries handled elsewhere.
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

		// 5. Last resort: right next to the TFE executable.
		if (tryBasePath(TFE_Paths::getPath(PATH_PROGRAM)))
			return true;

		return false;
	}

	// ------------------------------------------------------------------
	// Script path detection
	// ------------------------------------------------------------------
	//
	// Once we know where movies/ is, look for cutscene_scripts/ as its
	// sibling. That's how the remaster's DarkEX.kpf lays things out:
	//
	//     <remaster_root>/
	//       movies/
	//       cutscene_scripts/      <- we're looking for this
	//
	// If a modder drops everything in one directory, we also check for
	// cutscene_scripts/ as a child of movies/ as a fallback.
	static void detectScriptPath()
	{
		if (s_videoBasePath.empty()) { return; }

		// Walk back one directory. s_videoBasePath ends in "movies/" or
		// "Cutscenes/"; strip that component to get the parent.
		std::string remasterRoot = s_videoBasePath;
		if (!remasterRoot.empty() && (remasterRoot.back() == '/' || remasterRoot.back() == '\\')) { remasterRoot.pop_back(); }
		size_t slash = remasterRoot.find_last_of("/\\");
		if (slash != std::string::npos) { remasterRoot = remasterRoot.substr(0, slash + 1); }
		else { remasterRoot += '/'; }

		// Canonical location: sibling of movies/.
		char cutsceneScriptPath[TFE_MAX_PATH];
		sprintf(cutsceneScriptPath, "%scutscene_scripts/", s_videoBasePath.c_str());

		// Check if it exists 
		if (FileUtil::directoryExists(cutsceneScriptPath))
		{
			s_scriptBasePath = cutsceneScriptPath;
			TFE_System::logWrite(LOG_MSG, "Remaster", "Found cutscene scripts at: %s", cutsceneScriptPath);
			return;
		}

		else
		{
			// Ok then we must extract the folder from DarkEx.kpf which is a zip file.
			ZipArchive darkExFile;
			char darkExPath[TFE_MAX_PATH];
			sprintf(darkExPath, "%sDarkEX.kpf", TFE_Settings::getGameHeader("Dark Forces")->sourcePath);
			if (FileUtil::exists(darkExPath))
			{
				// sourcePath from registry is ANSI/extended-ASCII. miniz uses
				// MultiByteToWideChar(CP_UTF8) internally, so we must give it UTF-8.
				char darkExUtf8[TFE_MAX_PATH];
				convertExtendedAsciiToUtf8(darkExPath, darkExUtf8);
				if (darkExFile.open(darkExUtf8))
				{
					// Ok we found the Kex File now we can create a directory and save the DCSS
					FileUtil::makeDirectory(cutsceneScriptPath);
					// Look for the folder cutscene_scripts in the zip and extract 
					// the cutscene_script into the path moviesRoot 

					s32 foldIndex = -1;
					const u32 count = darkExFile.getFileCount();
					for (u32 i = 0; i < count; i++)
					{
						const char* name = darkExFile.getFileName(i);
						// Check if the name of the file starts with cutscene_scripts/
						if (strncasecmp(name, "cutscene_scripts/", strlen("cutscene_scripts/")) == 0)
						{
							u32 bufferLen = (u32)darkExFile.getFileLength(i);
							u8* buffer = (u8*)malloc(bufferLen);
							darkExFile.openFile(i);
							darkExFile.readFile(buffer, bufferLen);
							darkExFile.closeFile();

							FileStream file;
							char scriptFilePath[TFE_MAX_PATH];
							sprintf(scriptFilePath, "%s%s", s_videoBasePath.c_str(), name);
							if (file.open(scriptFilePath, Stream::MODE_WRITE))
							{
								file.writeBuffer(buffer, bufferLen);
								file.close();
							}
							free(buffer);
						}
					}
					darkExFile.close();
				}
				else
				{
					TFE_System::logWrite(LOG_WARNING, "Remaster", "Could not find DarkEx.kpf at: %s", darkExPath);
					return;
				}
			}
		}

		// Modder convenience: cutscene_scripts/ inside movies/.
		snprintf(cutsceneScriptPath, TFE_MAX_PATH, "%scutscene_scripts/", s_videoBasePath.c_str());
		if (FileUtil::directoryExists(cutsceneScriptPath))
		{
			s_scriptBasePath = cutsceneScriptPath;
			return;
		}

		// Last resort: look for DCSS files loose alongside the OGVs.
		// This rarely works but costs nothing to try, and lets a modder
		// hand-edit a single cutscene without making a new directory.
		s_scriptBasePath = s_videoBasePath;
	}

	// ------------------------------------------------------------------
	// Subtitle path detection
	// ------------------------------------------------------------------
	//
	// The remaster ships SRT files either in a dedicated Subtitles/
	// subdirectory (rare) or loose alongside the OGVs (typical). Check
	// dedicated first, fall back to loose.
	static void detectSubtitlePath()
	{
		if (s_videoBasePath.empty()) { return; }

		char testPath[TFE_MAX_PATH];
		snprintf(testPath, TFE_MAX_PATH, "%sSubtitles/", s_videoBasePath.c_str());
		if (FileUtil::directoryExists(testPath))
		{
			s_subtitleBasePath = testPath;
			return;
		}

		s_subtitleBasePath = s_videoBasePath;
	}

	// ------------------------------------------------------------------
	// Public API
	// ------------------------------------------------------------------

	void remasterCutscenes_init()
	{
		// Idempotent. cutscene_init might be called multiple times (e.g.
		// if the user reloads the game without restarting TFE), and we
		// only want to do path detection once. But force re-detection if the
		// remaster base path changed (e.g. user switched).
		if (s_initialized && strcmp(s_remasterBasePath, TFE_Paths::getPath(PATH_SOURCE_DATA)) == 0	) { return; }
		TFE_System::logWrite(LOG_MSG, "Remaster", "Initializing Remaster Cutscene System");
		s_initialized = true;
		s_available = false;

		// Preserve path in case the remaster base changes.
		sprintf(s_remasterBasePath, "%s", TFE_Paths::getPath(PATH_SOURCE_DATA));
		
		if (detectVideoPath())
		{
			s_available = true;
			detectScriptPath();
			detectSubtitlePath();
			TFE_System::logWrite(LOG_MSG, "Remaster", "Remaster OGV cutscene directory found.");
		}
		else
		{
			// This is the common case for people playing stock DOS Dark
			// Forces without the remaster. Not an error - just means the
			// LFD path stays in charge.
			TFE_System::logWrite(LOG_MSG, "Remaster", "No remaster cutscene directory found; using original LFD cutscenes.");
		}
	}

	bool remasterCutscenes_available()
	{
		return s_available;
	}

	// Load the cutscene path using the path string instead of scene
	const char* remasterCutscenes_getVideoPathFromBasename(string baseName)
	{
		// Try the language-specific variant first. The remaster only
		// localizes videos that have baked-in text (notably logo.ogv
		// which shows opening credits in English / German / etc.). Most
		// cutscenes are language-neutral and only the base file exists.
		const TFE_Settings_A11y* a11y = TFE_Settings::getA11ySettings();
		const char* lang = a11y->language.c_str();
		if (lang && lang[0])
		{
			snprintf(s_videoPathResult, TFE_MAX_PATH, "%s%s_%s.ogv",
				s_videoBasePath.c_str(), baseName.c_str(), lang);
			if (FileUtil::exists(s_videoPathResult)) { return s_videoPathResult; }
		}

		// Fall back to the default (no language suffix).
		snprintf(s_videoPathResult, TFE_MAX_PATH, "%s%s.ogv", s_videoBasePath.c_str(), baseName.c_str());
		if (FileUtil::exists(s_videoPathResult)) { return s_videoPathResult; }

		// No OGV for this scene. The caller will fall back to the LFD
		// FILM path.
		return nullptr;
	}

	// ------------------------------------------------------------------
	// Per-scene lookups
	// ------------------------------------------------------------------
	//
	// Each returns a pointer to one of our static buffers, or nullptr on
	// miss. These are called per-frame at the start of a cutscene (not
	// inside the hot loop), so performance isn't critical; readability
	// wins.

	const char* remasterCutscenes_getVideoPath(const CutsceneState* scene)
	{
		if (!s_available || !scene) { return nullptr; }

		std::string baseName = sceneBaseName(scene);
		if (baseName.empty()) { return nullptr; }
		return remasterCutscenes_getVideoPathFromBasename(baseName);
	}

	const char* remasterCutscenes_getDcssPath(const CutsceneState* scene)
	{
		if (!s_available || !scene || s_scriptBasePath.empty()) { return nullptr; }

		std::string baseName = sceneBaseName(scene);
		if (baseName.empty()) { return nullptr; }

		// DCSS files aren't localized - they're pure timing data. One
		// file per scene, used regardless of language.
		snprintf(s_scriptPathResult, TFE_MAX_PATH, "%s%s.dcss", s_scriptBasePath.c_str(), baseName.c_str());
		if (FileUtil::exists(s_scriptPathResult))
		{
			return s_scriptPathResult;
		}
		return nullptr;
	}

	const char* remasterCutscenes_getSubtitlePath(const CutsceneState* scene)
	{
		if (!s_available || !scene || s_subtitleBasePath.empty()) { return nullptr; }

		std::string baseName = sceneBaseName(scene);
		if (baseName.empty()) { return nullptr; }

		const TFE_Settings_A11y* a11y = TFE_Settings::getA11ySettings();
		const char* lang = a11y->language.c_str();

		// Lookup order for subtitles (most specific -> most generic):
		//   1. <scene>_<lang>.srt  (remaster convention, underscore)
		//   2. <scene>.<lang>.srt  (legacy TFE users who named files
		//                           differently before we matched the
		//                           remaster's convention)
		//   3. <scene>.srt         (default, usually English)
		if (lang && lang[0])
		{
			snprintf(s_subtitlePathResult, TFE_MAX_PATH, "%s%s_%s.srt",
				s_subtitleBasePath.c_str(), baseName.c_str(), lang);
			if (FileUtil::exists(s_subtitlePathResult)) { return s_subtitlePathResult; }

			snprintf(s_subtitlePathResult, TFE_MAX_PATH, "%s%s.%s.srt",
				s_subtitleBasePath.c_str(), baseName.c_str(), lang);
			if (FileUtil::exists(s_subtitlePathResult)) { return s_subtitlePathResult; }
		}

		snprintf(s_subtitlePathResult, TFE_MAX_PATH, "%s%s.srt",
			s_subtitleBasePath.c_str(), baseName.c_str());
		if (FileUtil::exists(s_subtitlePathResult)) { return s_subtitlePathResult; }

		return nullptr;
	}

	// Called from the settings UI or test harness to point at a
	// specific movies/ directory. Bypasses the priority-chain discovery
	// in detectVideoPath() entirely.
	void remasterCutscenes_setCustomPath(const char* path)
	{
		if (!path || !path[0])
		{
			// Empty path = "turn off the remaster path entirely and go
			// back to LFD." Reset all cached state.
			s_videoBasePath.clear();
			s_scriptBasePath.clear();
			s_subtitleBasePath.clear();
			s_available = false;
			return;
		}

		s_videoBasePath = path;
		if (s_videoBasePath.back() != '/' && s_videoBasePath.back() != '\\')
		{
			s_videoBasePath += '/';
		}

		s_available = FileUtil::directoryExists(s_videoBasePath.c_str());
		if (s_available)
		{
			// Re-detect scripts and subtitles relative to the new base.
			detectScriptPath();
			detectSubtitlePath();
		}
	}
}