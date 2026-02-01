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
#include "lmusic.h"
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

		// Start the MIDI music track for this cutscene.
		if (scene->music > 0)
		{
			lmusic_setSequence(scene->music);
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
			return JFALSE;
		}

		if (!TFE_OgvPlayer::update())
		{
			TFE_OgvPlayer::close();
			s_ogvPlaying = false;
			s_ogvSubtitles.clear();
			TFE_A11Y::clearActiveCaptions();
			return JFALSE;
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
				// No subtitle active right now.
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
