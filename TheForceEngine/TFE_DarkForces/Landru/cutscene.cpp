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
#include <TFE_A11y/accessibility.h>
#include <TFE_Input/input.h>
#include <TFE_Archive/lfdArchive.h>
#include "lmusic.h"
#include "cutscene_film.h"
#include "lsound.h"
#include <TFE_Jedi/IMuse/imuse.h>
#endif

using namespace TFE_Jedi;

namespace TFE_DarkForces
{
	static JBool s_playing = JFALSE;

	CutsceneState* s_playSeq = nullptr;
	s32 s_soundVolume = 0;
	s32 s_musicVolume = 0;
	s32 s_enabled = 1;

#ifdef ENABLE_OGV_CUTSCENES
	static bool s_ogvPlaying = false;
	static std::vector<SrtEntry> s_ogvSubtitles;

	// Pre-computed cue schedule: (ogvTime, cueValue) pairs.
	struct OgvCueEntry { f64 ogvTime; s32 cueValue; };
	static std::vector<OgvCueEntry> s_ogvCueSchedule;
	static s32 s_ogvNextCueIdx = 0;

	// Frame rate delay table (ticks at 240 Hz) indexed by (speed - 4).
	// Duplicated from cutscene_player.cpp since it's a small constant table.
	static const s32 c_ogvFrameRateDelay[] =
	{
		42, 49, 40, 35, 31, 28, 25, 23, 20, 19, 17, 16, 15, 14, 13, 12, 12,
	};
	enum { OGV_MIN_FPS = 4, OGV_MAX_FPS = 20, OGV_TICKS_PER_SEC = 240 };
#endif

	void cutscene_init(CutsceneState* cutsceneList)
	{
		s_playSeq = cutsceneList;
		s_playing = JFALSE;
#ifdef ENABLE_OGV_CUTSCENES
		remasterCutscenes_init();
#endif
	}

#ifdef ENABLE_OGV_CUTSCENES
	// Look up a scene by ID in the cutscene list.
	static CutsceneState* findScene(s32 sceneId)
	{
		if (!s_playSeq) { return nullptr; }
		for (s32 i = 0; s_playSeq[i].id != SCENE_EXIT; i++)
		{
			if (s_playSeq[i].id == sceneId)
			{
				return &s_playSeq[i];
			}
		}
		return nullptr;
	}

	// Scan callback: captures the cue point value set by CUST actors.
	static s32 s_scanCueValue = 0;

	static void ogv_scanCueCallback(LActor* actor, s32 time)
	{
		if (actor->var1 > 0) { s_scanCueValue = actor->var1; }
	}

	static JBool ogv_scanLoadCallback(Film* film, FilmObject* obj)
	{
		if (obj->id == CF_FILE_ACTOR)
		{
			LActor* actor = (LActor*)obj->data;
			if (actor->resType == CF_TYPE_CUSTOM_ACTOR)
			{
				lactor_setCallback(actor, ogv_scanCueCallback);
			}
		}
		return JFALSE;
	}

	static void ogvFilm_cleanup()
	{
		s_ogvCueSchedule.clear();
		s_ogvNextCueIdx = 0;
		lmusic_stop();
	}

	// Pre-scan the entire FILM chain to build a cue schedule.
	// Loads each FILM briefly, ticks frame 0 to capture the CUST cue value,
	// records accumulated FILM time, then unloads. Finally scales all times
	// to match OGV duration.
	static void ogvFilm_buildCueSchedule(s32 startSceneId, f64 ogvDuration)
	{
		s_ogvCueSchedule.clear();
		s_ogvNextCueIdx = 0;

		struct RawCue { f64 filmTime; s32 cueValue; };
		std::vector<RawCue> rawCues;
		f64 accumulatedTime = 0.0;

		lcanvas_init(320, 200);
		lsystem_setAllocator(LALLOC_CUTSCENE);

		s32 sceneId = startSceneId;
		while (sceneId != SCENE_EXIT)
		{
			CutsceneState* scene = findScene(sceneId);
			if (!scene) { break; }

			FilePath path;
			if (!TFE_Paths::getFilePath(scene->archive, &path)) { break; }

			Archive* lfd = new LfdArchive();
			if (!lfd->open(path.path))
			{
				delete lfd;
				break;
			}

			TFE_Paths::addLocalArchiveToFront(lfd);
			LRect rect;
			lcanvas_getBounds(&rect);

			s_scanCueValue = 0;
			Film* film = cutsceneFilm_load(scene->scene, &rect, 0, 0, 0, ogv_scanLoadCallback);

			if (film)
			{
				// Tick frame 0 to trigger the CUST actor callback.
				cutsceneFilm_updateFilms(0);
				cutsceneFilm_updateCallbacks(0);
				lactor_updateCallbacks(0);

				if (s_scanCueValue > 0)
				{
					rawCues.push_back({ accumulatedTime, s_scanCueValue });
				}

				// Compute this scene's duration.
				s32 speed = clamp((s32)scene->speed, (s32)OGV_MIN_FPS, (s32)OGV_MAX_FPS);
				s32 tickDelay = c_ogvFrameRateDelay[speed - OGV_MIN_FPS];
				f64 secsPerCell = (f64)tickDelay / (f64)OGV_TICKS_PER_SEC;
				accumulatedTime += film->cellCount * secsPerCell;

				cutsceneFilm_remove(film);
				cutsceneFilm_free(film);
			}

			TFE_Paths::removeFirstArchive();
			delete lfd;

			sceneId = scene->nextId;
		}

		lsystem_clearAllocator(LALLOC_CUTSCENE);
		lsystem_setAllocator(LALLOC_PERSISTENT);

		// Scale FILM times to OGV duration.
		// The OGV video has a short lead-in (~1s) before the FILM content begins,
		// so we offset all cues after the first by this amount.
		const f64 c_ogvLeadInOffset = 1.0;
		f64 totalFilmTime = accumulatedTime;
		f64 scale = (totalFilmTime > 0.0) ? (ogvDuration / totalFilmTime) : 1.0;

		for (const auto& raw : rawCues)
		{
			f64 ogvTime = raw.filmTime * scale;
			if (ogvTime > 0.0) { ogvTime += c_ogvLeadInOffset; }
			s_ogvCueSchedule.push_back({ ogvTime, raw.cueValue });
			TFE_System::logWrite(LOG_MSG, "Cutscene", "OGV cue schedule: cue %d at %.2fs (film=%.2fs, scale=%.3f)",
				raw.cueValue, ogvTime, raw.filmTime, scale);
		}

		TFE_System::logWrite(LOG_MSG, "Cutscene", "OGV cue schedule: %d cues, totalFilmTime=%.1fs, ogvDuration=%.1fs, scale=%.3f",
			(s32)s_ogvCueSchedule.size(), totalFilmTime, ogvDuration, scale);
	}

	// Try the remastered OGV version of a cutscene; returns false to fall back to LFD.
	static bool tryPlayOgvCutscene(s32 sceneId)
	{
		TFE_Settings_Game* gameSettings = TFE_Settings::getGameSettings();
		if (!gameSettings->df_enableRemasterCutscenes) { return false; }
		if (!remasterCutscenes_available()) { return false; }

		CutsceneState* scene = findScene(sceneId);
		if (!scene) { return false; }

		const char* videoPath = remasterCutscenes_getVideoPath(scene);
		if (!videoPath) { return false; }

		if (!TFE_OgvPlayer::open(videoPath))
		{
			TFE_System::logWrite(LOG_WARNING, "Cutscene", "Failed to open OGV file: %s, falling back to LFD.", videoPath);
			return false;
		}

		// Load subtitles if captions are on.
		s_ogvSubtitles.clear();
		if (TFE_A11Y::cutsceneCaptionsEnabled())
		{
			const char* srtPath = remasterCutscenes_getSubtitlePath(scene);
			if (srtPath)
			{
				srt_loadFromFile(srtPath, s_ogvSubtitles);
			}
		}

		// Pre-scan the original FILM chain to build a cue schedule.
		// Each scene's FILM has a CUST actor that sets a music cue point.
		// We extract all cue values and their FILM timestamps, then scale
		// to OGV duration so cues fire at the right visual moments.
		s_ogvCueSchedule.clear();
		s_ogvNextCueIdx = 0;

		if (scene->music > 0)
		{
			lmusic_setSequence(scene->music);
			f64 ogvDuration = TFE_OgvPlayer::getDuration();
			ogvFilm_buildCueSchedule(sceneId, ogvDuration);
		}

		s_ogvPlaying = true;
		TFE_System::logWrite(LOG_MSG, "Cutscene", "Playing remastered OGV cutscene for scene %d (%s).", sceneId, scene->archive);
		return true;
	}

	static JBool ogvCutscene_update()
	{
		// Skip on ESC/Enter/Space (ignore Alt+Enter which toggles fullscreen).
		if (TFE_Input::keyPressed(KEY_ESCAPE) ||
			(TFE_Input::keyPressed(KEY_RETURN) && !TFE_Input::keyDown(KEY_LALT) && !TFE_Input::keyDown(KEY_RALT)) ||
			TFE_Input::keyPressed(KEY_SPACE))
		{
			TFE_OgvPlayer::close();
			s_ogvPlaying = false;
			s_ogvSubtitles.clear();
			TFE_A11Y::clearActiveCaptions();
			ogvFilm_cleanup();
			return JFALSE;
		}

		if (!TFE_OgvPlayer::update())
		{
			TFE_OgvPlayer::close();
			s_ogvPlaying = false;
			s_ogvSubtitles.clear();
			TFE_A11Y::clearActiveCaptions();
			ogvFilm_cleanup();
			return JFALSE;
		}

		// Fire cue points from the pre-computed schedule.
		if (s_ogvNextCueIdx < (s32)s_ogvCueSchedule.size())
		{
			f64 ogvTime = TFE_OgvPlayer::getPlaybackTime();
			while (s_ogvNextCueIdx < (s32)s_ogvCueSchedule.size() &&
				   ogvTime >= s_ogvCueSchedule[s_ogvNextCueIdx].ogvTime)
			{
				s32 cue = s_ogvCueSchedule[s_ogvNextCueIdx].cueValue;
				TFE_System::logWrite(LOG_MSG, "Cutscene", "OGV firing cue %d at OGV time %.2fs", cue, ogvTime);
				lmusic_setCuePoint(cue);
				s_ogvNextCueIdx++;
			}
		}

		// Update subtitle captions.
		if (!s_ogvSubtitles.empty() && TFE_A11Y::cutsceneCaptionsEnabled())
		{
			f64 time = TFE_OgvPlayer::getPlaybackTime();
			const SrtEntry* entry = srt_getActiveEntry(s_ogvSubtitles, time);
			if (entry)
			{
				TFE_A11Y::Caption caption;
				caption.text = entry->text;
				caption.env = TFE_A11Y::CC_CUTSCENE;
				caption.type = TFE_A11Y::CC_VOICE;
				caption.microsecondsRemaining = (s64)((entry->endTime - time) * 1000000.0);
				TFE_A11Y::clearActiveCaptions();
				TFE_A11Y::enqueueCaption(caption);
			}
			else
			{
				TFE_A11Y::clearActiveCaptions();
			}
		}

		return JTRUE;
	}
#endif

	JBool cutscene_play(s32 sceneId)
	{
		if (!s_enabled || !s_playSeq) { return JFALSE; }
		TFE_Settings_Sound* soundSettings = TFE_Settings::getSoundSettings();
		TFE_Audio::setVolume(soundSettings->cutsceneSoundFxVolume * soundSettings->masterVolume);
		TFE_MidiPlayer::setVolume(soundSettings->cutsceneMusicVolume * soundSettings->masterVolume);

		// Search for the requested scene.
		s32 found = 0;
		for (s32 i = 0; !found && s_playSeq[i].id != SCENE_EXIT; i++)
		{
			if (s_playSeq[i].id == sceneId)
			{
				found = 1;
				break;
			}
		}
		if (!found) return JFALSE;

#ifdef ENABLE_OGV_CUTSCENES
		// Prefer the remastered OGV if available.
		if (tryPlayOgvCutscene(sceneId))
		{
			s_playing = JTRUE;
			return JTRUE;
		}
#endif

		// Re-initialize the canvas, so cutscenes run at the correct resolution even if it was changed for gameplay
		// (i.e. high resolution support).
		lcanvas_init(320, 200);

		// The original code then starts the cutscene loop here, and then returns when done.
		// Instead we set a bool and then the calling code will call 'update' until it returns false.
		s_playing = JTRUE;
		cutscenePlayer_start(sceneId);
		return JTRUE;
	}

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
