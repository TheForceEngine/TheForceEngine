#pragma once
// Detects remastered OGV cutscene files and maps CutsceneState archive
// names to video/subtitle paths (e.g. "ARCFLY.LFD" -> "arcfly.ogv").
#include <TFE_System/types.h>

struct CutsceneState;

namespace TFE_DarkForces
{
	void remasterCutscenes_init();
	bool remasterCutscenes_available();

	// Maps a scene's archive name to its OGV path, or nullptr if not found.
	const char* remasterCutscenes_getVideoPath(const CutsceneState* scene);
	// Returns the SRT subtitle path for a scene (language-specific, then default).
	const char* remasterCutscenes_getSubtitlePath(const CutsceneState* scene);

	void remasterCutscenes_setCustomPath(const char* path);
}
