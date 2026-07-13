#pragma once
//////////////////////////////////////////////////////////////////////
// Mod cutscene.txt override system
//////////////////////////////////////////////////////////////////////
//
// A custom mod can ship a "cutscene.txt" file (JSON) at the root of its
// zip to take full, explicit control of which OGV plays for the game's
// intro/outro and for each mission's intro/outro - completely replacing
// the stock cutscene.lst/scene-id driven system for those four slots.
//
// Format:
//
//   {
//     "game":    { "intro": "logo",      "outro": "credits"   },
//     "secbase": { "intro": "cutscene1", "outro": "cutscene2" },
//     "talay":   { "intro": "cutscene3" },
//     "arc":     { "outro": "cutscene4" }
//   }
//
// - "game".intro plays once, before the agent menu is first shown.
// - "game".outro plays once, after the last mission's own slots (if any)
//   have finished, right before returning to the agent menu with no more
//   missions to select (i.e. the game is "won").
// - "<mission>".intro plays after the mission is selected from the agent
//   menu, before the mission briefing screen.
// - "<mission>".outro plays after that mission is completed.
// - Mission keys are matched case-insensitively against the level name
//   (e.g. "secbase", matching agent_getLevelName()).
//
// Every name is a base filename with no extension - "logo" means
// "logo.ogv". Lookup order for each name: the mod zip first, then the
// remaster's own movies/ directory as a fallback. If neither has it,
// that slot plays nothing (it does NOT fall back to LFD/cutscene.lst -
// once cutscene.txt is active, it's the sole source of truth for these
// four slots). Likewise, a slot simply left out of the JSON (or the
// whole "game" or mission object being absent) plays nothing.
//
#include <TFE_System/types.h>

namespace TFE_DarkForces
{
	// Parses "cutscene.txt" from the currently loaded mod, if any. Safe to
	// call unconditionally (e.g. right after a mod's files are known) -
	// it's a no-op, and modCutscene_isActive() stays false, if no mod is
	// loaded or the mod didn't ship the file.
	void modCutscene_init();

	// Drops any parsed data. Call when leaving a mod (returning to a
	// vanilla session) so a stale config can't leak into the next game.
	void modCutscene_clear();

	// True only when a loaded mod shipped a valid cutscene.txt. While
	// true, callers should use ONLY this system for the four slots below
	// and must not fall back to cutscene.lst/scene ids for them.
	bool modCutscene_isActive();

	// Returns the configured base name (no extension) for each slot, or
	// nullptr if that slot isn't defined. levelName is matched
	// case-insensitively.
	const char* modCutscene_getGameIntro();
	const char* modCutscene_getGameOutro();
	const char* modCutscene_getMissionIntro(const char* levelName);
	const char* modCutscene_getMissionOutro(const char* levelName);
}
