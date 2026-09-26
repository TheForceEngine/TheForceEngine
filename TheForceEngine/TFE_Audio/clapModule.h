#pragma once
//////////////////////////////////////////////////////////////////////
// Minimal cross platform dynamic library loader used to host CLAP
// plugin bundles (.clap).
//////////////////////////////////////////////////////////////////////
#include <string>

namespace TFE_Audio
{
	// Loads a CLAP plugin bundle and returns an opaque module handle, or nullptr on failure.
	// Handles the platform specific bundle layout (a single shared library on
	// Windows/Linux, a bundle directory containing Contents/MacOS/<binary> on macOS).
	void* clapModuleOpen(const std::string& bundlePath);
	void* clapModuleGetSymbol(void* module, const char* name);
	void  clapModuleClose(void* module);
}
