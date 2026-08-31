#include "clapModule.h"

#if defined(_WIN32)
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
#elif defined(__APPLE__)
	#include <dlfcn.h>
	#include <dirent.h>
#else
	#include <dlfcn.h>
#endif

namespace TFE_Audio
{
#if defined(__APPLE__)
	// A .clap on macOS is a bundle (directory). Resolve it to the actual Mach-O
	// binary inside Contents/MacOS before handing it to dlopen().
	// Uses dirent.h rather than std::filesystem to match the rest of this project,
	// which does not universally build with C++17 filesystem enabled.
	static std::string resolveMacBinary(const std::string& bundlePath)
	{
		const std::string root = bundlePath + "/Contents/MacOS";
		DIR* dir = opendir(root.c_str());
		if (!dir) { return bundlePath; }

		std::string result = bundlePath;
		struct dirent* entry;
		while ((entry = readdir(dir)) != nullptr)
		{
			const std::string name = entry->d_name;
			if (name == "." || name == "..") { continue; }
			// The first non-"." entry is expected to be the plugin's Mach-O binary.
			result = root + "/" + name;
			break;
		}
		closedir(dir);
		return result;
	}
#endif

	void* clapModuleOpen(const std::string& bundlePath)
	{
	#if defined(_WIN32)
		return (void*)LoadLibraryA(bundlePath.c_str());
	#elif defined(__APPLE__)
		const std::string binPath = resolveMacBinary(bundlePath);
		return dlopen(binPath.c_str(), RTLD_NOW | RTLD_LOCAL);
	#else
		return dlopen(bundlePath.c_str(), RTLD_NOW | RTLD_LOCAL);
	#endif
	}

	void* clapModuleGetSymbol(void* module, const char* name)
	{
		if (!module) { return nullptr; }
	#if defined(_WIN32)
		return (void*)GetProcAddress((HMODULE)module, name);
	#else
		return dlsym(module, name);
	#endif
	}

	void clapModuleClose(void* module)
	{
		if (!module) { return; }
	#if defined(_WIN32)
		FreeLibrary((HMODULE)module);
	#else
		dlclose(module);
	#endif
	}
}
