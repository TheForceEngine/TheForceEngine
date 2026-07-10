#pragma once
//============================================================================
// OGV cutscene player
//============================================================================
//
// Plays Ogg Theora (video) + Vorbis (audio) files, decoded with libtheora
// and libvorbis, rendered via a GPU YUV->RGB shader (progs/yuv2rgb.shader).
// Handles a single stream at a time - we never run two cutscenes concurrent.
//
// Used exclusively by cutscene.cpp's OGV path. If you want to play a movie
// from some other game context, that wiring doesn't exist yet.
//
#include <TFE_System/types.h>

#ifdef ENABLE_OGV_CUTSCENES

namespace TFE_OgvPlayer
{
	// Create GPU resources (shader, VBO/IBO, Y/Cb/Cr texture slots). Must
	// be called after the render backend is up. Called lazily on first
	// open() if not invoked explicitly.
	bool init();

	// Destroy GPU resources. Safe to call multiple times.
	void shutdown();

	// Open a file and start playback. Returns false on decoder setup
	// failure (bad codec, corrupt headers, missing audio stream we can't
	// tolerate, etc.); the caller should fall back to an alternate
	// cutscene path.
	bool open(const char* filepath);

	// Stop playback and release per-stream state. Idempotent - calling
	// close() on an already-closed player is a no-op.
	void close();

	// Decode the next video frame (if one is due), pump audio, and render
	// the current frame to the backbuffer. Returns false when the stream
	// ends or the user presses a skip key. Driven once per game-loop
	// iteration while a cutscene is playing.
	bool update();

	bool isPlaying();

	// Wall-clock time since open(), in seconds. This advances in real
	// time regardless of whether frames are being decoded - useful for
	// measuring playback cost, not for syncing.
	f64  getPlaybackTime();

	// Intrinsic video time: advances by 1/fps each time a frame is
	// decoded and presented. This is what you want for synchronizing
	// anything to the visible frame (music cues, captions, etc.),
	// because if the game loop hitches, this clock stays locked to
	// the image on screen while wall-clock races ahead.
	f64  getVideoTime();

	// Enable/disable the "hold last frame at EOF" behaviour.
	// Call this before playback if you want the player to keep the last
	// decoded frame and GPU resources alive when the OGV hits EOF.
	void setHoldLastFrame(bool enable);

	// Returns true while the player has hit EOF and is holding the last
	// frame (i.e. we should draw credits overlay on top).
	bool isHoldingLastFrameActive();
}

#endif // ENABLE_OGV_CUTSCENES
