#include "clapPluginScanner.h"
#include "clapModule.h"
#include <clap/clap.h>
#include <TFE_System/system.h>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cctype>

#if defined(_WIN32)
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
#else
	#include <dirent.h>
	#include <sys/stat.h>
#endif

namespace TFE_Audio
{
	static std::vector<ClapPluginInfo> s_plugins;
	static bool s_scanned = false;

	// Case-insensitive check for a ".clap" suffix. Deliberately avoids std::filesystem,
	// which is not enabled/available in every configuration of this project (e.g. the
	// Windows Visual Studio build does not set /std:c++17 project wide).
	static bool hasClapExt(const std::string& name)
	{
		static const size_t extLen = 5; // ".clap"
		if (name.size() < extLen) { return false; }

		std::string ext = name.substr(name.size() - extLen);
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return ext == ".clap";
	}

	static void splitEnvPath(const char* value, char sep, std::vector<std::string>& out)
	{
		if (!value) { return; }
		std::string s(value);
		size_t start = 0;
		while (start <= s.size())
		{
			size_t pos = s.find(sep, start);
			if (pos == std::string::npos) { pos = s.size(); }
			if (pos > start) { out.push_back(s.substr(start, pos - start)); }
			start = pos + 1;
		}
	}

	static void getSearchRoots(std::vector<std::string>& roots)
	{
	#if defined(_WIN32)
		if (const char* common = std::getenv("COMMONPROGRAMFILES")) { roots.push_back(std::string(common) + "\\CLAP"); }
		if (const char* local = std::getenv("LOCALAPPDATA")) { roots.push_back(std::string(local) + "\\Programs\\Common\\CLAP"); }
		splitEnvPath(std::getenv("CLAP_PATH"), ';', roots);
	#elif defined(__APPLE__)
		roots.push_back("/Library/Audio/Plug-Ins/CLAP");
		if (const char* home = std::getenv("HOME")) { roots.push_back(std::string(home) + "/Library/Audio/Plug-Ins/CLAP"); }
		splitEnvPath(std::getenv("CLAP_PATH"), ':', roots);
	#else
		roots.push_back("/usr/lib/clap");
		roots.push_back("/usr/local/lib/clap");
		if (const char* home = std::getenv("HOME")) { roots.push_back(std::string(home) + "/.clap"); }
		splitEnvPath(std::getenv("CLAP_PATH"), ':', roots);
	#endif
	}

	// Loads a bundle just long enough to read its plugin descriptors.
	static void inspectBundle(const std::string& bundlePath)
	{
		void* module = clapModuleOpen(bundlePath);
		if (!module) { return; }

		auto entry = (const clap_plugin_entry_t*)clapModuleGetSymbol(module, "clap_entry");
		if (entry && clap_version_is_compatible(entry->clap_version) && entry->init(bundlePath.c_str()))
		{
			const auto* factory = (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
			if (factory)
			{
				const uint32_t count = factory->get_plugin_count(factory);
				for (uint32_t i = 0; i < count; i++)
				{
					const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, i);
					if (!desc || !desc->id) { continue; }

					ClapPluginInfo info;
					info.bundlePath = bundlePath;
					info.pluginId = desc->id;
					info.name = desc->name ? desc->name : desc->id;
					info.vendor = desc->vendor ? desc->vendor : "";
					s_plugins.push_back(info);
				}
			}
			entry->deinit();
		}
		clapModuleClose(module);
	}

#if defined(_WIN32)
	static void scanDirectory(const std::string& root)
	{
		const std::string pattern = root + "\\*";
		WIN32_FIND_DATAA fd;
		HANDLE find = FindFirstFileA(pattern.c_str(), &fd);
		if (find == INVALID_HANDLE_VALUE) { return; }

		do
		{
			const std::string name = fd.cFileName;
			if (name == "." || name == "..") { continue; }

			const std::string fullPath = root + "\\" + name;
			const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

			if (isDir && hasClapExt(name))
			{
				TFE_System::logWrite(LOG_MSG, "Clap", "Found CLAP bundle '%s'", fullPath.c_str());
				inspectBundle(fullPath);
				// A .clap bundle directory contains the plugin binary, not more bundles - don't recurse into it.
			}
			else if (isDir)
			{
				scanDirectory(fullPath);
			}
			else if (hasClapExt(name))
			{
				TFE_System::logWrite(LOG_MSG, "Clap", "Found CLAP bundle '%s'", fullPath.c_str());
				inspectBundle(fullPath);
			}
		} while (FindNextFileA(find, &fd));
		FindClose(find);
	}
#else
	static void scanDirectory(const std::string& root)
	{
		DIR* dir = opendir(root.c_str());
		if (!dir) { return; }

		struct dirent* entry;
		while ((entry = readdir(dir)) != nullptr)
		{
			const std::string name = entry->d_name;
			if (name == "." || name == "..") { continue; }

			const std::string fullPath = root + "/" + name;
			struct stat st;
			if (stat(fullPath.c_str(), &st) != 0) { continue; }
			const bool isDir = S_ISDIR(st.st_mode);

			if (isDir && hasClapExt(name))
			{
				TFE_System::logWrite(LOG_MSG, "Clap", "Found CLAP bundle '%s'", fullPath.c_str());
				inspectBundle(fullPath);
				// A .clap bundle directory contains the plugin binary, not more bundles - don't recurse into it.
			}
			else if (isDir)
			{
				scanDirectory(fullPath);
			}
			else if (hasClapExt(name))
			{
				TFE_System::logWrite(LOG_MSG, "Clap", "Found CLAP bundle '%s'", fullPath.c_str());
				inspectBundle(fullPath);
			}
		}
		closedir(dir);
	}
#endif

	const std::vector<ClapPluginInfo>& scanClapPlugins(bool forceRescan)
	{
		if (s_scanned && !forceRescan) { return s_plugins; }
		s_plugins.clear();

		std::vector<std::string> roots;
		getSearchRoots(roots);
		for (const std::string& root : roots)
		{
			scanDirectory(root);
		}

		s_scanned = true;
		return s_plugins;
	}
}
