#pragma once
//============================================================================
// Remastered cutscene file resolution
//============================================================================
//
// When Dark Forces wants to play cutscene N, the LFD-based path reads
// cutscene.lst to find the FILM archive and scene name, then plays the
// FILM's animation frames. The remastered path hooks in at the same point
// but resolves <scene>.ogv / <scene>.dcss / <scene>.srt on disk instead.
//
// This module is responsible for:
//
//   1. Finding WHERE the remaster's cutscene files live. There are several
//      plausible locations (Steam install, GOG install, user-configured
//      custom path, TFE program dir) and we try each in a defined order.
//
//   2. Translating a CutsceneState* (from cutscene.lst) into a concrete
//      file path, with localized variants where they exist.
//
// The actual video playback / cue dispatch lives in cutscene.cpp; this
// module just answers "where's the file?".
//
// Keyed on scene->scene (the scene name), not the archive name. For stock
// Dark Forces data those are the same (ARCFLY.LFD -> "arcfly"), but the
// remaster keys on scene name and modders may reuse an archive across
// multiple scenes, so scene name is the right key.
//
#include <TFE_System/types.h>
#include <string>

struct CutsceneState;

namespace TFE_DarkForces
{
	// Called once from cutscene_init(). Probes for the cutscene directory;
	// after this, remasterCutscenes_available() returns the result.
	void remasterCutscenes_init();

	// True if we found a usable remaster install. Returns false after init
	// if no candidate directory contained a "movies/" subdirectory.
	bool remasterCutscenes_available();

	// Returns a pointer to a static buffer containing the path, or nullptr
	// if the file doesn't exist. The buffer is reused across calls, so
	// don't hold the pointer past the next lookup.
	const char* remasterCutscenes_getVideoPathFromBasename(std::string baseName);
	const char* remasterCutscenes_getVideoPath(const CutsceneState* scene);
	const char* remasterCutscenes_getDcssPath(const CutsceneState* scene);
	const char* remasterCutscenes_getSubtitlePath(const CutsceneState* scene);

	// Manually override the base path (typically from the settings UI).
	// Passing empty/null disables the remaster path entirely.
	void remasterCutscenes_setCustomPath(const char* path);
}
