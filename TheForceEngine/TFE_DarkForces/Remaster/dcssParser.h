#pragma once
//============================================================================
// DCSS (Dark Cutscene Script) parser
//============================================================================
//
// DCSS is the text script format that the Dark Forces Remaster uses to time
// MIDI music changes against its remastered OGV cutscenes. The actual files
// live inside the remaster's DarkEX.kpf (which is a plain zip archive) at
// cutscene_scripts/<sceneName>.dcss, one per cutscene.
//
// We reverse-engineered the format from the remaster binary (khonsu, the
// Kex4 engine). The parser there lives around sub_140073940; we match its
// behavior closely enough that every stock .dcss file in the remaster
// parses here identically.
//
// Why we keep using the remaster's format rather than inventing our own:
//   - Zero-translation compatibility: a user who points TFE at the remaster
//     install gets timed MIDI cues with no extra authoring.
//   - Modders can follow published guides / look at stock files / hand-edit
//     with a text editor. No pipeline tooling required.
//   - If the remaster ever adds new directives, forward-compat is easy
//     (we silently ignore unknown lines).
//
// ---------------------------------------------------------------------------
// FORMAT OVERVIEW (see dcss-format.md for the complete spec)
// ---------------------------------------------------------------------------
//
// The file is a list of blocks, blank-line separated. Each block is:
//
//     <1-based index>
//     <HH:MM:SS,mmm timestamp>     (tolerates ','/':'/'.' as ms separator)
//     seq: <N>                     (optional, 1..20)
//     cue: <N>                     (optional, 1..20)
//     musicvol: <0..127>           (optional, percentage where 100 = base)
//
// Two optional header flags can appear before the first block:
//     +credits
//     +openingcredits
//
// Example (arcfly.dcss, verbatim from the remaster):
//
//     1
//     00:00:00,327
//     seq: 5
//     cue: 1
//
//     2
//     00:00:06,213
//     cue: 2
//
// Comments start with '#' or '//' and may appear on their own line.
//
// ---------------------------------------------------------------------------
// FOR MODDERS
// ---------------------------------------------------------------------------
//
// Drop a plain-text <scene>.dcss next to your <scene>.ogv. You can author
// these in Notepad; no tools required. See Documentation/markdown/
// remaster-cutscenes/modding-guide.md for a walkthrough.
//
#include <TFE_System/types.h>
#include <vector>

namespace TFE_DarkForces
{
	// A single cue point parsed from a .dcss file. At runtime, when the OGV's
	// playback time reaches timeMs, any fields that are "set" get dispatched:
	//
	//   - seq > 0       -> lmusic_setSequence(seq)
	//   - cue > 0       -> lmusic_setCuePoint(cue)
	//   - musicVol > 0  -> TFE_MidiPlayer::setVolume(base * musicVol/100)
	//
	// Zero / negative means "leave it alone," so a typical mid-scene entry
	// has seq=0 and just changes the cue.
	struct DcssEntry
	{
		u64 timeMs;    // Absolute playback time in ms (relative to scene start).
		s32 seq;       // iMuse sequence id to (re-)start. 0 = no change.
		s32 cue;       // iMuse cue point to fire.        0 = no change.
		s32 musicVol;  // Music volume override, as a %.  <=0 = no change.
		s32 index;     // 1-based entry number. Informational; not dispatched.
	};

	// Top-level parse result. The entries vector is sorted ascending by timeMs
	// after parse, so the runtime dispatcher can just walk it forward.
	struct DcssScript
	{
		bool creditsFlag;          // "+credits" flag was present in the header
		bool openingCreditsFlag;   // "+openingcredits" flag was present
		std::vector<DcssEntry> entries;
	};

	// Parse a DCSS file that's already in memory. Returns true if anything
	// useful was recovered (at least one entry, or at least one header flag).
	// Malformed entries are skipped with a warning; never throws.
	bool dcss_parse(const char* buffer, size_t size, DcssScript& out);

	// Convenience wrapper that reads the file from disk. Silent-false on
	// "file doesn't exist" (a common legitimate case when a modder ships an
	// OGV but no DCSS); logs a warning only for other I/O problems.
	bool dcss_loadFromFile(const char* path, DcssScript& out);
}
