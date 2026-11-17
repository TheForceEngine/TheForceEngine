#pragma once
#include <TFE_System/types.h>
#include <string>
#include <vector>

namespace TFE_Audio
{
	struct ClapPluginInfo
	{
		std::string bundlePath;	// Full path to the .clap bundle.
		std::string pluginId;	// clap_plugin_descriptor::id (a bundle can contain several plugins).
		std::string name;		// Display name.
		std::string vendor;
	};

	// Scans the standard CLAP install locations (plus the CLAP_PATH environment
	// variable) for .clap plugin bundles and lists every plugin found inside them.
	// Results are cached after the first call; pass forceRescan to refresh.
	const std::vector<ClapPluginInfo>& scanClapPlugins(bool forceRescan = false);
}
