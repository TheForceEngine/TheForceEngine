//============================================================================
// Dark Forces cutscene dispatch
//============================================================================
//
// Every cutscene request funnels through here. For each scene id we get
// asked to play, we pick one of two rendering paths:
//
//   1. The remastered OGV path — if the feature is enabled, the remaster
//      data is available, and an <scene>.ogv exists for this scene. Plays
//      the Theora video with Vorbis audio mixed in, and dispatches MIDI
//      cue points from a DCSS text script against the video's clock.
//
//   2. The original LFD FILM path (cutscenePlayer_*) — the legacy Landru-
//      based cutscene player. Unchanged from stock TFE; this is what DOS
//      Dark Forces played.
//
// The OGV path lives in this file; the LFD path lives in cutscene_player.*.
// Both funnel into lmusic_setSequence / lmusic_setCuePoint for MIDI, so the
// music layer is shared.
//
// On cutscene.cpp's design choices:
//
//   - We prefer OGV when available. The remaster's cutscenes are the
//     authoritative version (higher fidelity, properly timed, subtitled).
//     Falling back to LFD is a graceful degradation, not a user choice.
//
//   - Cue dispatch uses the video's *intrinsic* clock (getVideoTime), not
//     wall-clock. If the game hitches, wall-clock races ahead of the
//     visible frame; dispatching against wall-clock would make music cues
//     fire before the visual moment they're meant to accompany. See
//     DESIGN NOTE #1 near ogvCutscene_dispatchCues() for the numbers.
//
//   - DCSS is optional. If a scene has an OGV but no DCSS, we fall back to
//     the scene's cutscene.lst music_seq so there's still *some* MIDI.
//     Modders get the easy path without having to author a DCSS.
//
#include "cutscene.h"
#include "cutscene_player.h"
#include "lsystem.h"
#include "lcanvas.h"
#include <TFE_Game/igame.h>
#include <TFE_System/system.h>
#include <TFE_Audio/audioSystem.h>
#include <TFE_Audio/midiPlayer.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/Renderer/virtualFramebuffer.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_Settings/settings.h>
#include <TFE_System/parser.h>
#ifdef ENABLE_OGV_CUTSCENES
#include <TFE_DarkForces/Remaster/ogvPlayer.h>
#include <TFE_DarkForces/Remaster/remasterCutscenes.h>
#include <TFE_DarkForces/Remaster/srtParser.h>
#include <TFE_DarkForces/Remaster/dcssParser.h>
#include <TFE_A11y/accessibility.h>
#include <TFE_Input/input.h>
#include "lmusic.h"
#endif

using namespace TFE_Jedi;

namespace TFE_DarkForces
{
	// ----------------------------------------------------------------------
	// Shared state (both paths)
	// ----------------------------------------------------------------------

	// True while *either* path has an active cutscene. Drives the dispatch
	// in cutscene_update().
	static JBool s_playing = JFALSE;

	// The scene catalog from cutscene.lst. Non-owning; loaded at Dark Forces
	// game init and handed to us via cutscene_init.
	CutsceneState* s_playSeq = nullptr;

	// These four are preserved from the original code. s_enabled lets higher
	// layers disable cutscenes entirely (e.g. during demo playback), while
	// the volume globals are deprecated shadows of the settings system.
	s32 s_soundVolume = 0;
	s32 s_musicVolume = 0;
	s32 s_enabled = 1;

#ifdef ENABLE_OGV_CUTSCENES
	// ----------------------------------------------------------------------
	// OGV-path state
	// ----------------------------------------------------------------------

	// True while an OGV is actively playing. When this is true, cutscene_
	// update() dispatches to ogvCutscene_update() instead of the LFD player.
	static bool s_ogvPlaying = false;

	// Parsed subtitles for the current cutscene. Empty if the user has
	// captions off, or if no SRT was found for this scene.
	static std::vector<SrtEntry> s_ogvSubtitles;

	// DCSS cue script for the current cutscene. Entries are sorted by
	// timeMs; s_ogvNextCueIdx is the index of the next-to-fire entry.
	// Dispatch walks forward only; we never rewind.
	static DcssScript s_ogvScript;
	static size_t s_ogvNextCueIdx = 0;

	// Timing-test instrumentation. Flip this to 1 in a local build when
	// authoring a new DCSS or diagnosing a sync issue: every cue fire logs
	// expected-vs-actual timestamps, and teardown logs the total video
	// duration. Off in production to keep the default log quiet.
	#define DCSS_TIMING_TRACE 0
	#if DCSS_TIMING_TRACE
	static f64 s_ogvStartWallTime = 0.0;
	static f64 s_ogvLastVideoTime = 0.0;
	static const char* s_ogvTraceSceneName = "";
	#endif
#endif

	// ----------------------------------------------------------------------
	// Initialization
	// ----------------------------------------------------------------------

	// Called by darkForcesMain at game boot, once cutscene.lst has been
	// loaded. This is also where we kick off the remaster path detection
	// so the subsequent cutscene_play() calls don't have to lazy-probe.
	void cutscene_init(CutsceneState* cutsceneList)
	{
		s_playSeq = cutsceneList;
		s_playing = JFALSE;
#ifdef ENABLE_OGV_CUTSCENES
		remasterCutscenes_init();
#endif
	}

#ifdef ENABLE_OGV_CUTSCENES
	// ======================================================================
	// OGV path helpers
	// ======================================================================

	// Linear scan for a scene by id. The list is short (<50 entries in
	// stock data) and cutscene playback is infrequent, so we skip building
	// a hash table and just walk the array. SCENE_EXIT (0) is the
	// sentinel terminator.
	static CutsceneState* findScene(s32 sceneId)
	{
		if (!s_playSeq) { return nullptr; }
		for (s32 i = 0; s_playSeq[i].id != SCENE_EXIT; i++)
		{
			if (s_playSeq[i].id == sceneId) { return &s_playSeq[i]; }
		}
		return nullptr;
	}

	// Apply a DCSS "musicvol: N" override to the MIDI player.
	//
	// CUTSCENE.LST's header says the volume field is "110 = 10% higher
	// than normal," so 100 is the unity point. Matches what the LFD path
	// does at cutscene_player.cpp:151 (vol/100). Earlier versions of this
	// code incorrectly divided by 127, which made every cutscene 20%
	// quieter than intended.
	//
	// 127 is a soft practical ceiling - iMuse's internal MIDI volume
	// scale is 0..127, and going above that doesn't get you anything.
	static void ogvCutscene_applyMusicVolume(s32 volPercent)
	{
		const TFE_Settings_Sound* soundSettings = TFE_Settings::getSoundSettings();
		f32 scalar = (f32)clamp(volPercent, 0, 127) / 100.0f;
		TFE_MidiPlayer::setVolume(soundSettings->cutsceneMusicVolume * soundSettings->masterVolume * scalar);
	}

	// Release all per-cutscene resources and reset OGV state. Called when
	// the user skips (ESC/Enter/Space), when the OGV decoder reports end-
	// of-stream, or when playback fails.
	//
	// Order matters: we stop the MIDI *before* restoring volume so the
	// fade-out doesn't audibly clip. The OgvPlayer::close() is also safe
	// to call even if the player already closed itself internally (it's
	// idempotent).
	static void ogvCutscene_teardown()
	{
	#if DCSS_TIMING_TRACE
		{
			f64 wallSec  = TFE_System::getTime() - s_ogvStartWallTime;
			// getVideoTime() returns 0 once the player has closed, so
			// capture the last value we saw during dispatch instead. This
			// gives us an accurate duration figure for the END log.
			f64 videoSec = s_ogvLastVideoTime;
			size_t fired = s_ogvNextCueIdx;
			size_t total = s_ogvScript.entries.size();
			TFE_System::logWrite(LOG_MSG, "DcssTiming",
				"[%s] END videoDuration~=%.3fs wallDuration=%.3fs cuesFired=%zu/%zu",
				s_ogvTraceSceneName, videoSec, wallSec, fired, total);
		}
	#endif
		TFE_OgvPlayer::close();
		s_ogvPlaying = false;

		// Clear cutscene-scoped parse data. No memory to free directly;
		// std::vector/string destructors handle it.
		s_ogvSubtitles.clear();
		s_ogvScript.entries.clear();
		s_ogvScript.creditsFlag = false;
		s_ogvScript.openingCreditsFlag = false;
		s_ogvNextCueIdx = 0;
		TFE_A11Y::clearActiveCaptions();

		// Match the remaster's teardown: setSequence(0) unloads all MIDI
		// state and stops any in-flight notes.
		lmusic_setSequence(0);

		// If a DCSS entry set a musicvol override during playback, the
		// next cutscene (or ambient game music) would inherit it. Reset
		// to the user's configured base so future MIDI plays at the
		// right level.
		const TFE_Settings_Sound* soundSettings = TFE_Settings::getSoundSettings();
		TFE_MidiPlayer::setVolume(soundSettings->cutsceneMusicVolume * soundSettings->masterVolume);
	}

	// ----------------------------------------------------------------------
	// OGV path - scene startup
	// ----------------------------------------------------------------------
	//
	// Decide whether this scene has an OGV we can play. Returns true on
	// success (caller should set s_playing=true); false means "no dice,
	// fall back to LFD." Not actually playing the video yet - that happens
	// in ogvCutscene_update() frame-by-frame.
	//
	// Every bail-out here is silent-false (no log) except when we
	// specifically opened a file and it failed partway - those warrant a
	// warning because the user might be debugging a bad OGV.
	//
	static bool tryPlayOgvCutscene(s32 sceneId)
	{
		// Two opt-outs: the feature toggle (user pref) and the "we found
		// no remaster install" detection result.
		TFE_Settings_Game* gameSettings = TFE_Settings::getGameSettings();
		if (!gameSettings->df_enableRemasterCutscenes) { return false; }
		if (!remasterCutscenes_available()) { return false; }

		CutsceneState* scene = findScene(sceneId);
		if (!scene) { return false; }

		// OGV lookup handles locale variants (e.g. logo_de.ogv). Returns
		// nullptr if neither variant exists - that's a legitimate "not
		// all scenes have OGVs" case; the LFD path handles it.
		const char* videoPath = remasterCutscenes_getVideoPath(scene);
		if (!videoPath) { return false; }

		if (!TFE_OgvPlayer::open(videoPath))
		{
			// File exists but the Theora decoder rejected it. Could be a
			// corrupted OGV or an unusual codec config. Don't crash - the
			// LFD path is still a viable fallback.
			TFE_System::logWrite(LOG_WARNING, "Cutscene", "Failed to open OGV file: %s, falling back to LFD.", videoPath);
			return false;
		}

		// Subtitles are best-effort: if captions are off or the SRT is
		// missing, we silently play without them.
		s_ogvSubtitles.clear();
		if (TFE_A11Y::cutsceneCaptionsEnabled())
		{
			const char* srtPath = remasterCutscenes_getSubtitlePath(scene);
			if (srtPath) { srt_loadFromFile(srtPath, s_ogvSubtitles); }
		}

		// Reset DCSS state before loading a fresh one.
		s_ogvScript.entries.clear();
		s_ogvScript.creditsFlag = false;
		s_ogvScript.openingCreditsFlag = false;
		s_ogvNextCueIdx = 0;

		// The DCSS drives every music cue for this scene. Two paths:
		//
		//   a) DCSS found + parsed: reset MIDI to a clean state so the
		//      first DCSS entry's "seq: N" actually causes a sequence
		//      change (lmusic_setSequence is a no-op if asked to set the
		//      current sequence again).
		//
		//   b) No DCSS (modder didn't write one, or a test): fall back
		//      to the scene's cutscene.lst music_seq. This gives modders
		//      a zero-effort path - just ship an OGV and the original
		//      MIDI sequence keeps playing.
		//
		const char* dcssPath = remasterCutscenes_getDcssPath(scene);
		if (dcssPath && dcss_loadFromFile(dcssPath, s_ogvScript))
		{
			lmusic_setSequence(0);
		}
		else if (scene->music > 0)
		{
			TFE_System::logWrite(LOG_MSG, "Cutscene",
				"No DCSS script for scene '%s'; using cutscene.lst music=%d only.",
				scene->scene, (s32)scene->music);
			lmusic_setSequence(scene->music);
		}

		s_ogvPlaying = true;
	#if DCSS_TIMING_TRACE
		s_ogvStartWallTime = TFE_System::getTime();
		s_ogvTraceSceneName = scene->scene;
		TFE_System::logWrite(LOG_MSG, "DcssTiming",
			"[%s] START scene=%d entries=%zu", s_ogvTraceSceneName, sceneId, s_ogvScript.entries.size());
	#endif
		TFE_System::logWrite(LOG_MSG, "Cutscene", "Playing remastered OGV cutscene for scene %d ('%s').",
			sceneId, scene->scene);
		return true;
	}

	// ----------------------------------------------------------------------
	// OGV path - per-frame dispatch
	// ----------------------------------------------------------------------
	//
	// DESIGN NOTE #1 — Why we use video time, not wall-clock:
	//
	// TFE_OgvPlayer exposes two clocks. getPlaybackTime() is wall-clock
	// since open(). getVideoTime() is the *intrinsic* video clock that
	// advances by 1/fps per decoded, presented frame.
	//
	// If the game loop hitches (asset load, GC, stutter, whatever),
	// wall-clock keeps running but the visible frame doesn't. Dispatching
	// music cues off wall-clock would fire them ahead of the image they
	// accompany - subtle but noticeable, and really ugly when the hitch
	// is near a cue point.
	//
	// Using video time, cues stay locked to the frame. Measured drift
	// over a full 1:53 logo.ogv playback: 0–33 ms (≤ one frame at 30fps),
	// never growing.
	//
	// DESIGN NOTE #2 — Firing rules per DCSS entry:
	//
	// Each entry fields each have a "don't change" sentinel (0 or
	// non-positive). We fire the directive only if its value is
	// non-default. This matches the remaster's dispatch (decompiled from
	// khonsu around offset 262555 - "if (v36) setSequence(v36);").
	//
	// Critically, leaving seq=0 means "the sequence keeps playing." Most
	// stock DCSS files use this pattern: entry #1 sets seq, entries #2+
	// only set cue. That way we don't reload the MIDI mid-cutscene.
	//
	static void ogvCutscene_dispatchCues()
	{
		if (s_ogvScript.entries.empty()) { return; }

		// Intrinsic video clock. Converted to whole milliseconds for
		// comparison against DCSS's u64 timestamps (which are in ms too).
		const f64 videoTimeSec = TFE_OgvPlayer::getVideoTime();
	#if DCSS_TIMING_TRACE
		// Capture the last non-zero video time for the teardown log.
		// (getVideoTime returns 0 once the player closes, which would
		// make our "END videoDuration" log report 0 otherwise.)
		if (videoTimeSec > 0.0) { s_ogvLastVideoTime = videoTimeSec; }
	#endif
		const u64 nowMs = (u64)(videoTimeSec * 1000.0);

		// Walk forward through the sorted cue list, firing every entry
		// whose time has arrived. The `while` (not `if`) matters: if the
		// game hitched and we skipped a frame, two cues might come due
		// in the same update() call and we need to fire them both.
		while (s_ogvNextCueIdx < s_ogvScript.entries.size() &&
			   nowMs >= s_ogvScript.entries[s_ogvNextCueIdx].timeMs)
		{
			const DcssEntry& e = s_ogvScript.entries[s_ogvNextCueIdx];
		#if DCSS_TIMING_TRACE
			f64 wallSec = TFE_System::getTime() - s_ogvStartWallTime;
			TFE_System::logWrite(LOG_MSG, "DcssTiming",
				"[%s] cue #%d expected=%.3fs video=%.3fs wall=%.3fs videoDrift=%+.3fs wallDrift=%+.3fs seq=%d cue=%d musicvol=%d",
				s_ogvTraceSceneName, e.index,
				e.timeMs / 1000.0, videoTimeSec, wallSec,
				videoTimeSec - (e.timeMs / 1000.0),
				wallSec - (e.timeMs / 1000.0),
				e.seq, e.cue, e.musicVol);
		#endif

			// Order matters here: sequence changes reload MIDI, so we do
			// that first, then fire the cue within the new sequence, then
			// apply any volume override. A cue against the *old* sequence
			// would be wrong.
			if (e.seq > 0)        { lmusic_setSequence(e.seq); }
			if (e.cue > 0)        { lmusic_setCuePoint(e.cue); }
			if (e.musicVol > 0)   { ogvCutscene_applyMusicVolume(e.musicVol); }
			s_ogvNextCueIdx++;
		}
	}

	// Pull the active SRT entry for the current playback time and push it
	// to the caption system. Called per frame; the caption system itself
	// handles the on-screen rendering.
	//
	// We clearActiveCaptions() before enqueueing a fresh one, so the
	// previous caption's timer doesn't linger past its actual end. In the
	// "no active entry" case (between lines of dialogue), we just clear.
	static void ogvCutscene_updateCaptions()
	{
		if (s_ogvSubtitles.empty() || !TFE_A11Y::cutsceneCaptionsEnabled()) { return; }

		// Video time (not wall-clock) so captions stay in sync with the
		// visible frame, same as music dispatch.
		f64 time = TFE_OgvPlayer::getVideoTime();
		const SrtEntry* entry = srt_getActiveEntry(s_ogvSubtitles, time);
		if (entry)
		{
			TFE_A11Y::Caption caption;
			caption.text = entry->text;
			caption.env  = TFE_A11Y::CC_CUTSCENE;
			caption.type = TFE_A11Y::CC_VOICE;
			// microseconds remaining = how long the caption system should
			// display this. Computed from SRT's end time minus now.
			caption.microsecondsRemaining = (s64)((entry->endTime - time) * 1000000.0);
			TFE_A11Y::clearActiveCaptions();
			TFE_A11Y::enqueueCaption(caption);
		}
		else
		{
			TFE_A11Y::clearActiveCaptions();
		}
	}

	// The OGV path's per-frame update. Return value has the same meaning
	// as cutscenePlayer_update(): true = keep playing, false = we're done.
	static JBool ogvCutscene_update()
	{
		// Skip check comes first so the user's "get me out of here" press
		// is responsive even if the decoder is busy. We special-case
		// Alt+Enter (fullscreen toggle) so it doesn't double as a skip.
		if (TFE_Input::keyPressed(KEY_ESCAPE) ||
			(TFE_Input::keyPressed(KEY_RETURN) && !TFE_Input::keyDown(KEY_LALT) && !TFE_Input::keyDown(KEY_RALT)) ||
			TFE_Input::keyPressed(KEY_SPACE))
		{
			ogvCutscene_teardown();
			return JFALSE;
		}

		// Decode the next frame(s) as needed and render to the backbuffer.
		// Returns false when the decoder hits EOF or fails; either way we
		// teardown and report "cutscene ended."
		if (!TFE_OgvPlayer::update())
		{
			ogvCutscene_teardown();
			return JFALSE;
		}

		// Cue dispatch AFTER the frame update, so videoTime reflects the
		// frame we just presented.
		ogvCutscene_dispatchCues();
		ogvCutscene_updateCaptions();
		return JTRUE;
	}
#endif

	// ======================================================================
	// Public entry points
	// ======================================================================

	// Start playing cutscene `sceneId`. Returns true if playback began;
	// false means the scene wasn't found or cutscenes are disabled.
	//
	// On success, subsequent cutscene_update() calls drive the playback.
	// This is the same contract as the original TFE code - we just added
	// the OGV attempt as a first-choice path before the LFD fallback.
	JBool cutscene_play(s32 sceneId)
	{
		if (!s_enabled || !s_playSeq) { return JFALSE; }

		// Apply the user's configured volumes to both audio paths. The
		// DCSS musicvol: directive (if any) will layer on top of this.
		TFE_Settings_Sound* soundSettings = TFE_Settings::getSoundSettings();
		TFE_Audio::setVolume(soundSettings->cutsceneSoundFxVolume * soundSettings->masterVolume);
		TFE_MidiPlayer::setVolume(soundSettings->cutsceneMusicVolume * soundSettings->masterVolume);

		// Validate the scene id exists in our catalog before we commit
		// to either path. (findScene does this too, but for the OGV
		// check we want to fail fast if the id is bogus.)
		s32 found = 0;
		for (s32 i = 0; !found && s_playSeq[i].id != SCENE_EXIT; i++)
		{
			if (s_playSeq[i].id == sceneId) { found = 1; break; }
		}
		if (!found) return JFALSE;

#ifdef ENABLE_OGV_CUTSCENES
		// Try the remastered path first. If it returns false, no OGV is
		// available for this scene (or the feature is disabled) - fall
		// through to the LFD path.
		if (tryPlayOgvCutscene(sceneId))
		{
			s_playing = JTRUE;
			return JTRUE;
		}
#endif

		// Reset the Landru canvas to 320x200. The game might have
		// switched to a higher resolution for gameplay, but the LFD
		// FILM assets are all 320x200 and expect to draw into that
		// virtual framebuffer.
		lcanvas_init(320, 200);

		// Kick off the LFD path. Future cutscene_update() calls will
		// route through cutscenePlayer_update() based on s_ogvPlaying
		// being false.
		s_playing = JTRUE;
		cutscenePlayer_start(sceneId);
		return JTRUE;
	}

	// Per-frame update. Returns true while a cutscene is still playing.
	// The outer game loop calls this every frame until it returns false,
	// at which point control returns to the next game mode.
	JBool cutscene_update()
	{
		if (!s_playing) { return JFALSE; }

#ifdef ENABLE_OGV_CUTSCENES
		if (s_ogvPlaying)
		{
			s_playing = ogvCutscene_update();
			return s_playing;
		}
#endif

		s_playing = cutscenePlayer_update();
		return s_playing;
	}

	// ======================================================================
	// Legacy settings interface (unchanged from stock TFE)
	// ======================================================================

	void cutscene_enable(s32 enable)
	{
		s_enabled = enable;
	}

	s32 cutscene_isEnabled()
	{
		return s_enabled;
	}

	void cutscene_setSoundVolume(s32 volume)
	{
		s_soundVolume = clamp(volume, 0, 127);
	}

	void cutscene_setMusicVolume(s32 volume)
	{
		s_musicVolume = clamp(volume, 0, 127);
	}

	s32 cutscene_getSoundVolume()
	{
		return s_soundVolume;
	}

	s32 cutscene_getMusicVolume()
	{
		return s_musicVolume;
	}
}  // TFE_DarkForces
