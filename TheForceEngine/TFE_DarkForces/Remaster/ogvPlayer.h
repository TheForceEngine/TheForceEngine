#pragma once
// OGV cutscene player - decodes Ogg Theora video with Vorbis audio
// and renders frames via GPU YUV->RGB conversion.
#include <TFE_System/types.h>

#ifdef ENABLE_OGV_CUTSCENES

namespace TFE_OgvPlayer
{
	bool init();
	void shutdown();

	bool open(const char* filepath);
	void close();

	// Decode and render the next frame. Returns false when playback ends.
	bool update();

	bool isPlaying();
	f64  getPlaybackTime();
}

#endif // ENABLE_OGV_CUTSCENES
