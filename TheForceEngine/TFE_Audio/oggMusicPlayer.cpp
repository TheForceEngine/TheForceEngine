#include "oggMusicPlayer.h"

#ifdef ENABLE_OGV_CUTSCENES

#include "audioSystem.h"
#include <TFE_System/system.h>
#include <TFE_FileSystem/paths.h>

#include <vorbis/codec.h>
#include <ogg/ogg.h>

#include <cstring>
#include <cstdio>

namespace TFE_OggMusicPlayer
{
	static const u32 OGG_BUFFER_SIZE  = 4096;
	static const u32 RING_SIZE_FRAMES = 32768; // ~0.74s per channel at 44.1kHz

	static FILE* s_file = nullptr;
	static char  s_filePath[TFE_MAX_PATH] = { 0 };

	static ogg_sync_state   s_syncState;
	static ogg_stream_state s_vorbisStream;
	static vorbis_info      s_vorbisInfo;
	static vorbis_comment   s_vorbisComment;
	static vorbis_dsp_state s_vorbisDsp;
	static vorbis_block     s_vorbisBlock;

	static bool s_streamOpen = false; // true once headers are parsed and dsp/block are inited.
	static bool s_playing    = false;
	static bool s_paused     = false;
	static bool s_looping    = true;
	static f32  s_volume     = 1.0f;

	// Ring buffer, stereo interleaved f32.
	static f32* s_ringBuffer  = nullptr;
	static u32  s_ringSize    = 0; // in samples (frames * 2)
	static volatile u32 s_writePos = 0;
	static volatile u32 s_readPos  = 0;
	static f64  s_resampleAccum = 0.0;

	static void audioCallback(f32* buffer, u32 frameCount, f32 systemVolume);
	static bool bufferOggData();
	static bool openStreamFromCurrentFilePosition();
	static void closeStreamOnly();
	static bool drainPendingPcm();
	static void drainVorbisPackets();
	static void decodeAhead();
	static bool restartLoop();

	static bool bufferOggData()
	{
		char* buffer = ogg_sync_buffer(&s_syncState, OGG_BUFFER_SIZE);
		size_t bytesRead = fread(buffer, 1, OGG_BUFFER_SIZE, s_file);
		ogg_sync_wrote(&s_syncState, (long)bytesRead);
		return bytesRead > 0;
	}

	// Assumes s_file is positioned at the start of an Ogg stream. Parses
	// the three Vorbis header packets and initializes the DSP/block
	// state so decoding can begin. Standalone single-logical-stream
	// case only (no muxed video track).
	static bool openStreamFromCurrentFilePosition()
	{
		ogg_sync_init(&s_syncState);
		vorbis_info_init(&s_vorbisInfo);
		vorbis_comment_init(&s_vorbisComment);

		ogg_page page;
		while (ogg_sync_pageout(&s_syncState, &page) != 1)
		{
			if (!bufferOggData()) { return false; }
		}
		if (!ogg_page_bos(&page)) { return false; }

		ogg_stream_init(&s_vorbisStream, ogg_page_serialno(&page));
		if (ogg_stream_pagein(&s_vorbisStream, &page) < 0) { return false; }

		s32 headersNeeded = 3;
		ogg_packet packet;
		while (headersNeeded > 0)
		{
			s32 res = ogg_stream_packetout(&s_vorbisStream, &packet);
			if (res < 0) { return false; }
			if (res == 0)
			{
				while (ogg_sync_pageout(&s_syncState, &page) != 1)
				{
					if (!bufferOggData()) { return false; }
				}
				ogg_stream_pagein(&s_vorbisStream, &page);
				continue;
			}
			if (vorbis_synthesis_headerin(&s_vorbisInfo, &s_vorbisComment, &packet) < 0)
			{
				return false;
			}
			headersNeeded--;
		}

		vorbis_synthesis_init(&s_vorbisDsp, &s_vorbisInfo);
		vorbis_block_init(&s_vorbisDsp, &s_vorbisBlock);
		s_resampleAccum = 0.0;
		s_streamOpen = true;
		return true;
	}

	// Tears down the ogg/vorbis stream-level state (not the file handle
	// or ring buffer) so it can be rebuilt, either on close or ahead of
	// a loop restart.
	static void closeStreamOnly()
	{
		if (s_streamOpen)
		{
			vorbis_block_clear(&s_vorbisBlock);
			vorbis_dsp_clear(&s_vorbisDsp);
			ogg_stream_clear(&s_vorbisStream);
		}
		vorbis_comment_clear(&s_vorbisComment);
		vorbis_info_clear(&s_vorbisInfo);
		ogg_sync_clear(&s_syncState);
		s_streamOpen = false;
	}

	static bool restartLoop()
	{
		closeStreamOnly();
		if (fseek(s_file, 0, SEEK_SET) != 0) { return false; }
		return openStreamFromCurrentFilePosition();
	}

	static bool drainPendingPcm()
	{
		const f64 resampleStep = (f64)s_vorbisInfo.rate / 44100.0;
		f32** pcm;
		s32 samples;
		while ((samples = vorbis_synthesis_pcmout(&s_vorbisDsp, &pcm)) > 0)
		{
			s32 channels = s_vorbisInfo.channels;
			while (s_resampleAccum < (f64)samples)
			{
				u32 w = s_writePos;
				u32 r = s_readPos;
				u32 used = (w >= r) ? (w - r) : (s_ringSize - r + w);
				if (used >= s_ringSize - 2) { return false; } // Ring full - back off, try again next update().

				s32 idx = (s32)s_resampleAccum;
				s_ringBuffer[w] = pcm[0][idx];
				w = (w + 1) % s_ringSize;
				s_ringBuffer[w] = (channels > 1) ? pcm[1][idx] : pcm[0][idx];
				w = (w + 1) % s_ringSize;
				s_writePos = w;
				s_resampleAccum += resampleStep;
			}
			s_resampleAccum -= (f64)samples;
			vorbis_synthesis_read(&s_vorbisDsp, samples);
		}
		return true;
	}

	static void drainVorbisPackets()
	{
		if (!drainPendingPcm()) { return; }

		ogg_packet packet;
		while (ogg_stream_packetout(&s_vorbisStream, &packet) == 1)
		{
			if (vorbis_synthesis(&s_vorbisBlock, &packet) == 0)
			{
				vorbis_synthesis_blockin(&s_vorbisDsp, &s_vorbisBlock);
			}
			if (!drainPendingPcm()) { return; }
		}
	}

	static u32 ringAvailable()
	{
		u32 w = s_writePos, r = s_readPos;
		return (w >= r) ? (w - r) : (s_ringSize - r + w);
	}

	// Keeps the ring buffer topped up; loops or stops at EOF.
	static void decodeAhead()
	{
		drainVorbisPackets();

		ogg_page page;
		while (ringAvailable() < RING_SIZE_FRAMES)
		{
			bool gotPage = false;
			while (ogg_sync_pageout(&s_syncState, &page) == 1)
			{
				ogg_stream_pagein(&s_vorbisStream, &page);
				gotPage = true;
				break;
			}
			if (!gotPage)
			{
				if (!bufferOggData())
				{
					// End of file.
					if (s_looping && restartLoop())
					{
						continue;
					}
					s_playing = false;
					return;
				}
				continue;
			}
			drainVorbisPackets();
		}
	}

	static void audioCallback(f32* buffer, u32 frameCount, f32 systemVolume)
	{
		if (s_paused) { return; }

		u32 samplesToFill = frameCount * 2;
		f32 vol = systemVolume * s_volume;
		for (u32 i = 0; i < samplesToFill; i++)
		{
			if (s_readPos != s_writePos)
			{
				buffer[i] += s_ringBuffer[s_readPos] * vol;
				s_readPos = (s_readPos + 1) % s_ringSize;
			}
		}
	}

	bool play(const char* filepath, bool loop)
	{
		stop();
		if (!filepath || !filepath[0]) { return false; }

		s_file = fopen(filepath, "rb");
		if (!s_file)
		{
			TFE_System::logWrite(LOG_ERROR, "OggMusicPlayer", "Cannot open file: %s", filepath);
			return false;
		}

		if (!openStreamFromCurrentFilePosition())
		{
			TFE_System::logWrite(LOG_ERROR, "OggMusicPlayer", "Failed to read Vorbis stream from: %s", filepath);
			stop();
			return false;
		}

		strncpy(s_filePath, filepath, TFE_MAX_PATH - 1);
		s_filePath[TFE_MAX_PATH - 1] = 0;

		s_ringSize = RING_SIZE_FRAMES * 2; // stereo
		s_ringBuffer = new f32[s_ringSize];
		memset(s_ringBuffer, 0, s_ringSize * sizeof(f32));
		s_writePos = 0;
		s_readPos  = 0;

		s_looping = loop;
		s_paused  = false;
		s_playing = true;

		// Pre-fill the ring buffer before handing off to the audio thread.
		decodeAhead();

		TFE_Audio::setDirectCallback(audioCallback);

		TFE_System::logWrite(LOG_MSG, "OggMusicPlayer", "Playing music: %s (rate=%ld, channels=%d, loop=%s)",
			filepath, s_vorbisInfo.rate, s_vorbisInfo.channels, loop ? "true" : "false");
		return true;
	}

	void stop()
	{
		if (s_playing || s_streamOpen)
		{
			TFE_Audio::setDirectCallback(nullptr);
			// Make sure the audio thread isn't mid-callback before freeing.
			TFE_Audio::lock();
			TFE_Audio::unlock();
		}

		closeStreamOnly();

		if (s_ringBuffer)
		{
			delete[] s_ringBuffer;
			s_ringBuffer = nullptr;
		}
		s_ringSize = 0;
		s_writePos = 0;
		s_readPos  = 0;

		if (s_file)
		{
			fclose(s_file);
			s_file = nullptr;
		}

		s_playing = false;
		s_paused  = false;
	}

	void pause()
	{
		s_paused = true;
	}

	void resume()
	{
		s_paused = false;
	}

	void update()
	{
		if (!s_playing || s_paused) { return; }
		decodeAhead();
	}

	bool isPlaying()
	{
		return s_playing;
	}

	void setVolume(f32 volume)
	{
		s_volume = volume;
	}
}

#else // !ENABLE_OGV_CUTSCENES

// Stub implementation for builds without libogg/libvorbis, so callers
// (gameMusic.cpp) don't need to conditionally compile around this.
namespace TFE_OggMusicPlayer
{
	bool play(const char*, bool) { return false; }
	void stop() { }
	void pause() { }
	void resume() { }
	void update() { }
	bool isPlaying() { return false; }
	void setVolume(f32) { }
}

#endif // ENABLE_OGV_CUTSCENES
