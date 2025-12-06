#include <cstring>
#include <cassert>
#include "paths.h"
#include "fileutil.h"
#include "filestream.h"
#include <TFE_Settings/gameSourceData.h>
#include <TFE_System/system.h>
#include <TFE_Archive/archive.h>
#include <algorithm>
#include <deque>
#include <string>
#include <SDL.h>

#ifdef __APPLE__
#include <CoreServices/CoreServices.h>
#endif

namespace FileUtil {
	extern bool existsNoCase(const char *filename);
	extern char *findDirNoCase(const char *dn);
}

namespace TFE_Paths
{
	struct FileMapping
	{
		std::string fileName;
		std::string realPath;
	};

	static std::string s_paths[PATH_COUNT];
	static std::deque<Archive*> s_localArchives;
	static std::deque<std::string> s_searchPaths;
	static std::deque<FileMapping> s_fileMappings;
	static std::deque<std::string> s_systemPaths;	// TFE Support data paths

	bool isPortableInstall();

	void setPath(TFE_PathType pathType, const char* path)
	{
		s_paths[pathType] = path;
	}


	// Where to store TFE settings, saves, screenshots.
	// If the envvar "TFE_DATA_HOME" is set, and it is
	// an absolute path, then this path defines where TFE data
	// is stored (without the usual "TheForceEngine" string added);
	// if it is a relative path, then this path is used relative
	// to the cwd; otherwise store in $HOME/.local/share/<tfe>/
	static void setTFEPath(const char *tfe, u32 pathid)
	{
		char *home = getenv("HOME"), *tdh = getenv("TFE_DATA_HOME");
		char path[TFE_MAX_PATH], cwd[TFE_MAX_PATH];
		int i;

		if (!home || strlen(home) < 1) {
			// whoa, how did that happen?
			fprintf(stderr, "[Error] %s\n", "$HOME is undefined! Defaulting to /tmp");
			home = strdup("/tmp");
		}

		if (!tdh || strlen(tdh) < 1) {
			if (strcmp(SDL_GetPlatform(), "Mac OS X") == 0) {
				snprintf(path, TFE_MAX_PATH, "%s/Library/Application Support/%s/", home, tfe);
			} else {
				snprintf(path, TFE_MAX_PATH, "%s/.local/share/%s/", home, tfe);
			}
		} else {
			// remove trailing slashes in custom dir
			i = strlen(tdh);
			while ((i > 0) && (tdh[i - 1] == '/')) {
				tdh[i - 1] = 0;
				i--;
			}

			// no, we don't touch root. Use the standard path.
			if (tdh[0] == 0) {
				if (strcmp(SDL_GetPlatform(), "Mac OS X") == 0) {
					snprintf(path, TFE_MAX_PATH, "%s/Library/Application Support/%s/", home, tfe);
				} else {
					snprintf(path, TFE_MAX_PATH, "%s/.local/share/%s/", home, tfe);
				}
			} else if (tdh[0] == '/') {
				snprintf(path, TFE_MAX_PATH, "%s/", tdh);
			} else {
				FileUtil::getCurrentDirectory(cwd);
				snprintf(path, TFE_MAX_PATH, "%s/%s/", cwd, tdh);
			}
		}
		s_paths[pathid] = path;
		FileUtil::makeDirectory(path);
	}

	// System Paths: where all the support data is located, which is
	// accessed mainly via relative paths. We look at, in this order:
	// directory of executable
	// /usr/local/share/
	// /usr/share/
	// $HOME/.local/share/
	//  the above last because it is not touched when a new version
	//  is installed via e.g. package manager or 'make install',
	//  and user might end up with incompatible data causing hard
	//  to debug (non-)issues.
	bool setProgramDataPath(const char *append)
	{
		s_systemPaths.push_back(getPath(PATH_PROGRAM));
		if (isPortableInstall())
		{
			s_paths[PATH_PROGRAM_DATA] = s_paths[PATH_PROGRAM];
			return true;
		}

		std::string s;
		if (strcmp(SDL_GetPlatform(), "Mac OS X") == 0) {
			s = std::string("/Library/Application Support/") + append + "/";
			s_systemPaths.push_back(s);
			s = std::string("/System/Library/Application Support/") + append + "/";
			s_systemPaths.push_back(s);
		} else {
			s = std::string("/usr/local/share/") + append + "/";
			s_systemPaths.push_back(s);
			s = std::string("/usr/share/") + append + "/";
			s_systemPaths.push_back(s);
		}
		setTFEPath(append, PATH_PROGRAM_DATA);
		s_systemPaths.push_back(getPath(PATH_PROGRAM_DATA));

		return true;
	}

	bool setUserDocumentsPath(const char *append)
	{
		if (isPortableInstall())
		{
			s_paths[PATH_USER_DOCUMENTS] = s_paths[PATH_PROGRAM];
			return true;
		}

		// ensure SetProgramDataPath() was called before
		assert(!s_paths[PATH_PROGRAM_DATA].empty());

		s_paths[PATH_USER_DOCUMENTS] = s_paths[PATH_PROGRAM_DATA];
		s_systemPaths.push_back(getPath(PATH_USER_DOCUMENTS));
		// set cwd to user documents path, because Dear ImGUI drops
		// its ini file at the cwd, we don't want it to litter somewhere
		// else.  Also, unlike the windows version, we don't expect
		// support data to reside at where the executable was launched
		// from.
		FileUtil::setCurrentDirectory(s_paths[PATH_USER_DOCUMENTS].c_str());
		return true;
	}

	// There is no official remaster support on linux
	// Lets approximate proton save locations. 
	bool setRemasterDocsPath(GameID game)
	{
		if (isPortableInstall())
		{
			s_paths[PATH_REMASTER_DOCS] = s_paths[PATH_PROGRAM];
			return true;
		}
		string strId = to_string((int)TFE_Settings::c_steamRemasterProductId[game]);
		string append = "~/.steam/steam/steamapps/compatdata/" + strId + "/pfx/drive_c/users/steamuser/Saved Games/Nightdive Studios";

		if (game == Game_Dark_Forces)
		{
			append += "/Dark Forces Remaster/";
		}
		else if (game == Game_Outlaws)
		{
			append += "/Outlaws Remaster/";
		}
		else
		{
			return false;
		}
		s_paths[PATH_REMASTER_DOCS] = append;
		return !s_paths[PATH_REMASTER_DOCS].empty();
	}

	bool setProgramPath(void)
	{
		char p[TFE_MAX_PATH];
		memset(p, 0, TFE_MAX_PATH);
#ifdef __APPLE__
		FileUtil::getExecutionDirectory(p);
#else
		FileUtil::getCurrentDirectory(p);
#endif
		s_paths[PATH_PROGRAM] = p;
		s_paths[PATH_PROGRAM] += "/";
		return true;
	}

	const char* getPath(TFE_PathType pathType)
	{
		return s_paths[pathType].c_str();
	}

	bool hasPath(TFE_PathType pathType)
	{
		return !s_paths[pathType].empty();
	}

	void appendPath(TFE_PathType pathType, const char *filename, char *path, size_t bufferLen/* = TFE_MAX_PATH*/)
	{
		snprintf(path, bufferLen, "%s%s", getPath(pathType), filename);
	}

	void fixupPathAsDirectory(char *fullPath)
	{
		char *c = fullPath;
		int len = strlen(fullPath);

		while (NULL != (c = strchr(c, '\\'))) {
			*c = '/';
		}

		// Make sure it ends with a slash.
		if (fullPath[len - 1] != '/')
		{
			fullPath[len] = '/';
			fullPath[len + 1] = 0;
		}
	}

	// take a relative input filepath, try to find it anywhere in the
	// system paths, and return an absolute path to the file if found.
	// report whether a change was made to the input.
	bool mapSystemPath(char *fname)
	{
		char fullname[TFE_MAX_PATH];
		for (auto it = s_systemPaths.begin(); it != s_systemPaths.end(); it++) {
			sprintf(fullname, "%s%s", it->c_str(), fname);
			if (FileUtil::existsNoCase(fullname)) {
				strncpy(fname, fullname, TFE_MAX_PATH);
				return true;
			}
			// is it a dir?
			if (FileUtil::findDirNoCase(fullname)) {
				strncpy(fname, fullname, TFE_MAX_PATH);
				return true;
			}
		}
		return false;
	}

	void addSearchPath(const char *fullPath)
	{
		char workpath[TFE_MAX_PATH];

		if (!FileUtil::directoryExits(fullPath, workpath))
			return;

		for (auto it = s_searchPaths.begin(); it != s_searchPaths.end(); it++)
		{
			// If the path already exists, then don't add it again.
			if (!strcasecmp(it->c_str(), workpath))
			{
				return;
			}
		}
		s_searchPaths.push_back(workpath);
	}

	void addSearchPathToHead(const char *fullPath)
	{
		char workpath[TFE_MAX_PATH];

		if (!FileUtil::directoryExits(fullPath, workpath))
			return;

		for (auto it = s_searchPaths.begin(); it != s_searchPaths.end(); it++)
		{
			// If the path already exists, then don't add it again.
			if (!strcasecmp(it->c_str(), workpath))
			{
				return;
			}
		}
		s_searchPaths.push_front(workpath);
	}

	void clearSearchPaths(void)
	{
		s_searchPaths.clear();
		s_fileMappings.clear();
	}

	void clearLocalArchives(void)
	{
		std::for_each(s_localArchives.begin(), s_localArchives.end(),
				[](Archive *a) { Archive::freeArchive(a); });
		s_localArchives.clear();
	}

	// Add a single file that can be referenced by 'fileName' even though the real name may be different.
	void addSingleFilePath(const char *fileName, const char *filePath)
	{
		char fileNameLC[TFE_MAX_PATH];
		strcpy(fileNameLC, fileName);
		__strlwr(fileNameLC);

		char filePathFixed[TFE_MAX_PATH];
		strcpy(filePathFixed, filePath);
		FileUtil::fixupPath(filePathFixed);

		FileMapping mapping = { fileNameLC, filePathFixed };
		s_fileMappings.push_back(mapping);
	}

	void addLocalSearchPath(const char *locpath)
	{
		char p[TFE_MAX_PATH];
		snprintf(p, TFE_MAX_PATH, "%s%s", getPath(PATH_SOURCE_DATA), locpath);
		fixupPathAsDirectory(p);

		addSearchPath(p);
	}

	void addAbsoluteSearchPathToHead(const char *abspath)
	{
		char p[TFE_MAX_PATH];
		strcpy(p, abspath);
		fixupPathAsDirectory(p);

		addSearchPathToHead(p);
	}

	void addAbsoluteSearchPath(const char *abspath)
	{
		char p[TFE_MAX_PATH];
		strcpy(p, abspath);
		fixupPathAsDirectory(p);

		addSearchPath(p);
	}

	void addLocalArchiveToFront(Archive *a)
	{
		s_localArchives.push_front(a);
	}

	void removeFirstArchive(void)
	{
		s_localArchives.pop_front();
	}

	void addLocalArchive(Archive *a)
	{
		s_localArchives.push_back(a);
	}

	void removeLastArchive(void)
	{
		s_localArchives.pop_back();
	}

	bool getFilePath(const char *fileName, FilePath *outPath)
	{
		char fullname[TFE_MAX_PATH];

		outPath->archive = nullptr;
		outPath->index = INVALID_FILE;
		outPath->path[0] = 0;

		// Search for any filemappings.
		// This is usually only used with mods and usually limited to 0-3 files.
		for (auto it = s_fileMappings.begin(); it != s_fileMappings.end(); it++) {
			if (0 == strcasecmp(it->fileName.c_str(), fileName)) {
				strcpy(outPath->path, it->realPath.c_str());
				return true;
			}
		}

		// Search in the local search paths before local archives: s_searchPaths.
		for (auto it = s_searchPaths.begin(); it != s_searchPaths.end(); it++) {
			sprintf(fullname, "%s%s", it->c_str(), fileName);
			if (FileUtil::existsNoCase(fullname)) {
				strncpy(outPath->path, fullname, TFE_MAX_PATH);
				return true;
			}
		}

		// Then archives: s_localArchives.
		for (auto it = s_localArchives.begin(); it != s_localArchives.end(); it++) {
			u32 index = (*it)->getFileIndex(fileName);
			if (index != INVALID_FILE) {
				outPath->archive = *it;
				outPath->index = index;
				return true;
			}
		}

		// Finally admit defeat.
		return false;
	}

	// Return true if we want to use a "portable" install - 
	// aka all data such as screenshots, settings, etc. are stored in the
	// TFE directory.
	bool isPortableInstall()
	{
		return FileUtil::exists("settings.ini");
	}

	void getAllFilesFromSearchPaths(const char* subdirectory, const char* ext, FileList& allFiles)
	{
		size_t pathCount = s_searchPaths.size();
		for (size_t p = 0; p < pathCount; p++)
		{
			std::vector<string> fileList;
			char dir[TFE_MAX_PATH];
			sprintf(dir, "%s%s%s", s_searchPaths[p].c_str(), subdirectory, "/");
			FileUtil::readDirectory(dir, ext, fileList);

			// Add each file in the fileList to the result
			for (int f = 0; f < fileList.size(); f++)
			{
				char filePath[TFE_MAX_PATH];
				string file = fileList[f];
				sprintf(filePath, "%s%s", dir, file.c_str());
				allFiles.push_back(filePath);
			}
		}
	}
}
