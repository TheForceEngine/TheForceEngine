#include "oggDecode.h"

#ifdef ENABLE_OGV_CUTSCENES

#include <TFE_System/system.h>

#include <vorbis/codec.h>
#include <ogg/ogg.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace TFE_OggDecode
{
	static const u32 OGG_BUFFER_SIZE = 4096;
	static const u32 GROW_SAMPLES    = 1 << 16;

	static bool bufferOggData(FILE* file, ogg_sync_state* sync)
	{
		char* buffer = ogg_sync_buffer(sync, OGG_BUFFER_SIZE);
		size_t bytesRead = fread(buffer, 1, OGG_BUFFER_SIZE, file);
		ogg_sync_wrote(sync, (long)bytesRead);
		return bytesRead > 0;
	}

	u8* decodeToMono8(const char* filepath, s32 targetSampleRate, u32* outSampleCount)
	{
		if (outSampleCount) { *outSampleCount = 0; }
		if (!filepath || targetSampleRate <= 0) { return nullptr; }

		FILE* file = fopen(filepath, "rb");
		if (!file)
		{
			TFE_System::logWrite(LOG_ERROR, "OggDecode", "Cannot open file: %s", filepath);
			return nullptr;
		}

		ogg_sync_state   sync;
		ogg_stream_state stream;
		vorbis_info      info;
		vorbis_comment   comment;
		vorbis_dsp_state dsp;
		vorbis_block     block;
		bool streamInited = false, dspInited = false;

		ogg_sync_init(&sync);
		vorbis_info_init(&info);
		vorbis_comment_init(&comment);

		u8* result = nullptr;

		// Parse the three Vorbis header packets (single-logical-stream case).
		bool ok = true;
		{
			ogg_page page;
			while (ogg_sync_pageout(&sync, &page) != 1)
			{
				if (!bufferOggData(file, &sync)) { ok = false; break; }
			}
			if (ok && !ogg_page_bos(&page)) { ok = false; }
			if (ok)
			{
				ogg_stream_init(&stream, ogg_page_serialno(&page));
				streamInited = true;
				if (ogg_stream_pagein(&stream, &page) < 0) { ok = false; }
			}

			s32 headersNeeded = 3;
			ogg_packet packet;
			while (ok && headersNeeded > 0)
			{
				s32 res = ogg_stream_packetout(&stream, &packet);
				if (res < 0) { ok = false; break; }
				if (res == 0)
				{
					while (ogg_sync_pageout(&sync, &page) != 1)
					{
						if (!bufferOggData(file, &sync)) { ok = false; break; }
					}
					if (!ok) { break; }
					ogg_stream_pagein(&stream, &page);
					continue;
				}
				if (vorbis_synthesis_headerin(&info, &comment, &packet) < 0) { ok = false; break; }
				headersNeeded--;
			}
		}

		if (ok)
		{
			vorbis_synthesis_init(&dsp, &info);
			vorbis_block_init(&dsp, &block);
			dspInited = true;
		}

		if (ok)
		{
			// Decode-ahead: growable mono, 8-bit unsigned output buffer.
			u32 capacity = GROW_SAMPLES;
			u8* pcm8 = (u8*)malloc(capacity);
			u32 count = 0;

			const f64 resampleStep = (f64)info.rate / (f64)targetSampleRate;
			f64 resampleAccum = 0.0;
			s32 channels = info.channels;

			bool eof = false;
			ogg_page page;
			while (!eof)
			{
				f32** pcmOut;
				s32 samples;
				while ((samples = vorbis_synthesis_pcmout(&dsp, &pcmOut)) > 0)
				{
					while (resampleAccum < (f64)samples)
					{
						if (count >= capacity)
						{
							capacity += GROW_SAMPLES;
							u8* newBuf = (u8*)realloc(pcm8, capacity);
							if (!newBuf) { free(pcm8); pcm8 = nullptr; ok = false; break; }
							pcm8 = newBuf;
						}
						if (!ok) { break; }

						s32 idx = (s32)resampleAccum;
						f32 sample = pcmOut[0][idx];
						if (channels > 1) { sample = 0.5f * (sample + pcmOut[1][idx]); }

						f32 scaled = sample * 127.0f + 128.0f;
						if (scaled < 0.0f) scaled = 0.0f;
						if (scaled > 255.0f) scaled = 255.0f;
						pcm8[count++] = (u8)scaled;

						resampleAccum += resampleStep;
					}
					if (!ok) { break; }
					resampleAccum -= (f64)samples;
					vorbis_synthesis_read(&dsp, samples);
				}
				if (!ok) { break; }

				ogg_packet packet;
				bool gotPacket = false;
				while (ogg_stream_packetout(&stream, &packet) == 1)
				{
					if (vorbis_synthesis(&block, &packet) == 0)
					{
						vorbis_synthesis_blockin(&dsp, &block);
					}
					gotPacket = true;
					break;
				}
				if (gotPacket) { continue; }

				// Need another page.
				if (ogg_sync_pageout(&sync, &page) == 1)
				{
					ogg_stream_pagein(&stream, &page);
					continue;
				}
				if (!bufferOggData(file, &sync))
				{
					eof = true;
				}
			}

			if (ok && pcm8)
			{
				result = pcm8;
				if (outSampleCount) { *outSampleCount = count; }
			}
			else if (pcm8)
			{
				free(pcm8);
			}
		}

		if (dspInited)
		{
			vorbis_block_clear(&block);
			vorbis_dsp_clear(&dsp);
		}
		if (streamInited) { ogg_stream_clear(&stream); }
		vorbis_comment_clear(&comment);
		vorbis_info_clear(&info);
		ogg_sync_clear(&sync);
		fclose(file);

		if (!result)
		{
			TFE_System::logWrite(LOG_ERROR, "OggDecode", "Failed to decode Vorbis stream from: %s", filepath);
		}
		return result;
	}
}

#else // !ENABLE_OGV_CUTSCENES

namespace TFE_OggDecode
{
	u8* decodeToMono8(const char*, s32, u32* outSampleCount)
	{
		if (outSampleCount) { *outSampleCount = 0; }
		return nullptr;
	}
}

#endif // ENABLE_OGV_CUTSCENES
