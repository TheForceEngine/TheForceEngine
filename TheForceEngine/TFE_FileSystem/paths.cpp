#pragma once
#include "paths.h"
#include "fileutil.h"
#include "filestream.h"
#include <TFE_System/system.h>
#include <TFE_Archive/archive.h>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>
#ifdef min
#undef min
#undef max
#endif

#include <shlwapi.h>
#pragma comment(lib,"shlwapi.lib")
#include "shlobj.h"
#endif

namespace TFE_Paths
{
	struct FileMapping
	{
		std::string fileName;
		std::string realPath;
	};

	static std::string s_paths[PATH_COUNT];
	static std::vector<Archive*> s_localArchives;
	static std::vector<std::string> s_searchPaths;
	static std::vector<FileMapping> s_fileMappings;

	bool insertString(char* text, const char* newFragment, const char* pattern);
	bool isPortableInstall();

	void setPath(TFE_PathType pathType, const char* path)
	{
		s_paths[pathType] = path;
	}

	bool setProgramDataPath(const char* append)
	{
		// Support "Portable" methods as well.
		if (isPortableInstall())
		{
			s_paths[PATH_PROGRAM_DATA] = s_paths[PATH_PROGRAM];
			return !s_paths[PATH_PROGRAM_DATA].empty();
		}

#ifdef _WIN32
		char path[TFE_MAX_PATH];
		// Get path for each computer, non-user specific and non-roaming data.
		if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_COMMON_APPDATA, NULL, 0, path)))
		{
			// Append product-specific path
			PathAppend(path, append);

			// Only setup the path if successful.
			s_paths[PATH_PROGRAM_DATA] = path;
			s_paths[PATH_PROGRAM_DATA] += "\\";

			FileUtil::makeDirectory(path);
			return true;
		}
		else
		{
			// If getting the program data folder fails, then revert back to using
			// the program path if possible.
			s_paths[PATH_PROGRAM_DATA] = s_paths[PATH_PROGRAM];
			return !s_paths[PATH_PROGRAM].empty();
		}
#endif
		return false;
	}

	bool setUserDocumentsPath(const char* append)
	{
		// Support "Portable" methods as well.
		if (isPortableInstall())
		{
			s_paths[PATH_USER_DOCUMENTS] = s_paths[PATH_PROGRAM];
			return !s_paths[PATH_USER_DOCUMENTS].empty();
		}

#ifdef _WIN32
		// Otherwise attempt to use the Windows User "Documents/" directory.
		char path[TFE_MAX_PATH];
		// Get path for each computer, non-user specific and non-roaming data.
		if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_MYDOCUMENTS, NULL, 0, path)))
		{
			// Append product-specific path
			PathAppend(path, append);

			// Only setup the path if successful.
			s_paths[PATH_USER_DOCUMENTS] = path;
			s_paths[PATH_USER_DOCUMENTS] += "\\";

			if (FileUtil::makeDirectory(path))
			{
				return true;
			}
			else
			{
				// Try OneDrive.
				insertString(path, "OneDrive\\", "Documents");
				s_paths[PATH_USER_DOCUMENTS] = path;
				s_paths[PATH_USER_DOCUMENTS] += "\\";

				if (FileUtil::makeDirectory(path))
				{
					return true;
				}
			}
		}

		// If getting the documents folder fails, then revert back to using
		// the program path if possible.
		s_paths[PATH_USER_DOCUMENTS] = s_paths[PATH_PROGRAM];
		return !s_paths[PATH_PROGRAM].empty();
#endif
		return false;
	}

	// This is the path to the Remaster documents folder.
	bool setRemasterDocsPath(GameID game)	
	{
	#ifdef _WIN32
		char path[TFE_MAX_PATH];
		
		if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_PROFILE, NULL, 0, path)))
		{
			// Append product-specific path
			if (game == Game_Dark_Forces)
			{
				PathAppend(path, "Saved Games\\Nightdive Studios\\Dark Forces Remaster");
			}
			else if (game == Game_Outlaws)
			{
				PathAppend(path, "Saved Games\\Nightdive Studios\\Outlaws Remaster");
			}
			else
			{
				return false;
			}
			s_paths[PATH_REMASTER_DOCS] = path;
			s_paths[PATH_REMASTER_DOCS] += "\\";
		}
		return !s_paths[PATH_REMASTER_DOCS].empty();
	#endif
		return false;
	}

	bool setProgramPath()
	{
		char path[TFE_MAX_PATH];
		FileUtil::getCurrentDirectory(path);
		// This is here for ease of use in Visual Studio.
		// Check to see if the current directory is valid.
		char testPath[TFE_MAX_PATH];
		sprintf(testPath, "%s/%s", path, "SDL2.dll");
		if (!FileUtil::exists(testPath))
		{
			FileUtil::getExecutionDirectory(path);
		}

		s_paths[PATH_PROGRAM] = path;
		s_paths[PATH_PROGRAM] += "\\";
		FileUtil::setCurrentDirectory(s_paths[PATH_PROGRAM].c_str());
		return true;
	}

	bool mapSystemPath(char* path)
	{
		// on windows, all of TFE's support files (Shaders, Fonts, ...)
		// are located where TFE binary is installed, which is the default
		// search location for relative paths.
		return false;  // no mapping was done.
	}

	const char* getPath(TFE_PathType pathType)
	{
		return s_paths[pathType].c_str();
	}

	bool hasPath(TFE_PathType pathType)
	{
		return !s_paths[pathType].empty();
	}

	void appendPath(TFE_PathType pathType, const char* filename, char* path, size_t bufferLen/* = TFE_MAX_PATH*/)
	{
		snprintf(path, bufferLen, "%s%s", getPath(pathType), filename);
	}

	void fixupPathAsDirectory(char* fullPath)
	{
		size_t len = strlen(fullPath);
		// Fix-up slashes.
		for (size_t i = 0; i < len; i++)
		{
			if (fullPath[i] == '\\')
			{
				fullPath[i] = '/';
			}
		}
		// Make sure it ends with a slash.
		if (fullPath[len - 1] != '/')
		{
			fullPath[len] = '/';
			fullPath[len + 1] = 0;
		}
	}

	void addSearchPath(const char* fullPath)
	{
		if (FileUtil::directoryExists(fullPath))
		{
			const size_t count = s_searchPaths.size();
			const std::string* path = s_searchPaths.data();
			for (size_t i = 0; i < count; i++, path++)
			{
				// If the path already exists, then don't add it again.
				if (!strcasecmp(path->c_str(), fullPath))
				{
					return;
				}
			}

			s_searchPaths.push_back(fullPath);
		}
	}

	void addSearchPathToHead(const char* fullPath)
	{
		if (FileUtil::directoryExists(fullPath))
		{
			const size_t count = s_searchPaths.size();
			const std::string* path = s_searchPaths.data();
			for (size_t i = 0; i < count; i++, path++)
			{
				// If the path already exists, then don't add it again.
				if (!strcasecmp(path->c_str(), fullPath))
				{
					return;
				}
			}

			s_searchPaths.insert(s_searchPaths.begin(), fullPath);
		}
	}

	void clearSearchPaths()
	{
		s_searchPaths.clear();
		s_fileMappings.clear();
	}

	void clearLocalArchives()
	{
		const size_t count = s_localArchives.size();
		Archive** archive = s_localArchives.data();
		for (size_t i = 0; i < count; i++)
		{
			Archive::freeArchive(archive[i]);
		}
		s_localArchives.clear();
	}

	// Add a single file that can be referenced by 'fileName' even though the real name may be different.
	void addSingleFilePath(const char* fileName, const char* filePath)
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

	void addLocalSearchPath(const char* localSearchPath)
	{
		char fullPath[TFE_MAX_PATH];
		snprintf(fullPath, TFE_MAX_PATH, "%s%s", getPath(PATH_SOURCE_DATA), localSearchPath);
		fixupPathAsDirectory(fullPath);

		addSearchPath(fullPath);
	}

	void addAbsoluteSearchPathToHead(const char* absoluteSearchPath)
	{
		char fullPath[TFE_MAX_PATH];
		strcpy(fullPath, absoluteSearchPath);
		fixupPathAsDirectory(fullPath);

		addSearchPathToHead(fullPath);
	}

	void addAbsoluteSearchPath(const char* absoluteSearchPath)
	{
		char fullPath[TFE_MAX_PATH];
		strcpy(fullPath, absoluteSearchPath);
		fixupPathAsDirectory(fullPath);

		addSearchPath(fullPath);
	}
		
	void addLocalArchiveToFront(Archive* archive)
	{
		s_localArchives.insert(s_localArchives.begin(), archive);
	}

	void removeFirstArchive()
	{
		s_localArchives.erase(s_localArchives.begin());
	}

	void addLocalArchive(Archive* archive)
	{
		s_localArchives.push_back(archive);
	}

	void removeLastArchive()
	{
		s_localArchives.pop_back();
	}

	bool getFilePath(const char* fileName, FilePath* outPath)
	{
		outPath->archive = nullptr;
		outPath->index = INVALID_FILE;
		outPath->path[0] = 0;

		// Search for any filemappings.
		// This is usually only used with mods and usually limited to 0-3 files.
		const size_t mappingCount  = s_fileMappings.size();
		const FileMapping* mapping = s_fileMappings.data();
		for (size_t i = 0; i < mappingCount; i++, mapping++)
		{
			// Early out if the first character doesn't match to avoid testing the entire string.
			if (mapping->fileName[0] == tolower(fileName[0]) && strcasecmp(mapping->fileName.c_str(), fileName) == 0)
			{
				strcpy(outPath->path, mapping->realPath.c_str());
				return true;
			}
		}

		// Search in the local search paths before local archives: s_searchPaths.
		const size_t pathCount = s_searchPaths.size();
		const std::string* localPath = s_searchPaths.data();
		for (size_t i = 0; i < pathCount; i++, localPath++)
		{
			char fullName[TFE_MAX_PATH];
			sprintf(fullName, "%s%s", localPath->c_str(), fileName);

			FileStream file;
			if (file.exists(fullName))
			{
				strncpy(outPath->path, fullName, TFE_MAX_PATH);
				return true;
			}
		}

		// Then archives: s_localArchives.
		const size_t archiveCount = s_localArchives.size();
		Archive** archive = s_localArchives.data();
		for (size_t i = 0; i < archiveCount; i++, archive++)
		{
			if (!(*archive)) { continue; }	// Avoid crashing if an archive is null.

			u32 index = (*archive)->getFileIndex(fileName);
			if (index != INVALID_FILE)
			{
				outPath->archive = *archive;
				outPath->index = index;
				return true;
			}
		}

		// Finally admit defeat.
		return false;
	}
		
	bool insertString(char* text, const char* newFragment, const char* pattern)
	{
		if (!text || !newFragment || !pattern)
		{
			return false;
		}

		char* loc = strstr(text, pattern);
		if (!loc)
		{
			return false;
		}

		char workStr[TFE_MAX_PATH];
		strncpy(workStr, text, size_t(loc - text));
		workStr[size_t(loc - text)] = 0;

		strcat(workStr, newFragment);
		strcat(workStr, text + size_t(loc - text));
		strcpy(text, workStr);

		return true;
	}

	bool isPortableInstall()
	{
		// Support "Portable" methods as well.
		char portableSettingsPath[TFE_MAX_PATH];
		TFE_Paths::appendPath(PATH_PROGRAM, "settings.ini", portableSettingsPath);
		if (FileUtil::exists(portableSettingsPath))
		{
			return true;
		}
		return false;
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
