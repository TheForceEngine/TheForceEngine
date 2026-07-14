#pragma once
//////////////////////////////////////////////////////////////////////
// Ogg Vorbis music player.
//
// Plays a single standalone .ogg (Vorbis) file on a seamless loop.
// This is the audio-only sibling of TFE_OgvPlayer (TFE_Asset/ogvPlayer.*)
// - same low-level libogg/libvorbis decode-ahead-into-a-ring-buffer
// approach and the same TFE_Audio direct-callback mixing path, just
// without the Theora video half.
//
// Used by TFE_DarkForces/gameMusic.cpp to let mods replace individual
// iMuse/GMD mission music tracks with a looping Vorbis file (see
// TFE_ExternalData/musicExternal.h for how the override is declared).
//
// Requires the same build configuration as TFE_OgvPlayer (libogg /
// libvorbis available, ENABLE_OGV_CUTSCENES defined) since it shares
// those libraries.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>

namespace TFE_OggMusicPlayer
{
	// Opens 'filepath' and starts playback. If 'loop' is true, playback
	// restarts from the beginning each time the file ends. Stops
	// whatever was previously playing first. Returns false if the file
	// can't be opened or doesn't contain a Vorbis stream.
	bool play(const char* filepath, bool loop = true);

	// Stops playback and releases decoder resources.
	void stop();

	// Pause/resume without losing decode position - mirrors
	// TFE_MidiPlayer::pause()/resume(), for use while cutscenes or
	// menus are active.
	void pause();
	void resume();

	// Pumps the streaming decoder, keeping the ring buffer topped up
	// and handling the loop restart at end-of-file. Call once per game
	// tick; a no-op when nothing is playing.
	void update();

	bool isPlaying();
	void setVolume(f32 volume);
}
