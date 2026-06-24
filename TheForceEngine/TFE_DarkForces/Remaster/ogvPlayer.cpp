#include "ogvPlayer.h"

#ifdef ENABLE_OGV_CUTSCENES

#include <TFE_System/system.h>
#include <TFE_System/profiler.h>
#include <TFE_Input/input.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_RenderBackend/renderState.h>
#include <TFE_RenderBackend/shader.h>
#include <TFE_RenderBackend/textureGpu.h>
#include <TFE_RenderBackend/vertexBuffer.h>
#include <TFE_RenderBackend/indexBuffer.h>
#include <TFE_RenderBackend/Win32OpenGL/gl.h>
#include <TFE_Audio/audioSystem.h>

#include <theora/theoradec.h>
#include <vorbis/codec.h>
#include <ogg/ogg.h>

#include <cstring>
#include <cstdio>
#include <vector>

namespace TFE_OgvPlayer
{
	static const u32 AUDIO_BUFFER_SIZE = 32768;  // per-channel ring buffer size
	static const u32 OGG_BUFFER_SIZE   = 4096;

	// Ogg / Theora / Vorbis state
	static FILE* s_file = nullptr;

	static ogg_sync_state   s_syncState;
	static ogg_stream_state s_theoraStream;
	static ogg_stream_state s_vorbisStream;

	static th_info       s_theoraInfo;
	static th_comment    s_theoraComment;
	static th_setup_info* s_theoraSetup = nullptr;
	static th_dec_ctx*   s_theoraDec = nullptr;

	static vorbis_info    s_vorbisInfo;
	static vorbis_comment s_vorbisComment;
	static vorbis_dsp_state s_vorbisDsp;
	static vorbis_block   s_vorbisBlock;

	static bool s_hasTheora = false;
	static bool s_hasVorbis = false;
	static bool s_theoraStreamInited = false;
	static bool s_vorbisStreamInited = false;

	// Playback state
	static bool s_playing = false;
	static bool s_initialized = false;
	static f64  s_videoTime    = 0.0;
	static f64  s_audioTime    = 0.0;
	static f64  s_playbackStart = 0.0;
	static bool s_firstFrame   = true;
	static bool s_holdLastFrame = false;
	static bool s_holdingActive = false;

	// Theora page and granule tracking 
	ogg_int64_t granuleOffset = 0;

	// GPU resources
	static Shader       s_yuvShader;
	static TextureGpu*  s_texY  = nullptr;
	static TextureGpu*  s_texCb = nullptr;
	static TextureGpu*  s_texCr = nullptr;
	static VertexBuffer s_vertexBuffer;
	static IndexBuffer  s_indexBuffer;
	static s32 s_scaleOffsetId = -1;
	static s32 s_uvScaleId     = -1;

	// Audio ring buffer (stereo interleaved f32)
	static f32* s_audioRingBuffer = nullptr;
	static volatile u32 s_audioWritePos = 0;
	static volatile u32 s_audioReadPos  = 0;
	static u32 s_audioRingSize = 0;
	static f64 s_resampleAccum = 0.0;

	// Forward declarations
	static bool readOggHeaders();
	static bool decodeVideoFrame();
	static void decodeAudioPackets();
	static bool demuxPage(ogg_page* page);
	static bool bufferOggData();
	static void uploadYuvFrame(th_ycbcr_buffer ycbcr);
	static void renderFrame();
	static void audioCallback(f32* buffer, u32 bufferSize, f32 systemVolume);
	static void freeGpuResources();
	static void freeOggResources();

	// Fullscreen quad vertex layout
	struct QuadVertex
	{
		f32 x, y;
		f32 u, v;
	};

	static const AttributeMapping c_quadAttrMapping[] =
	{
		{ATTR_POS, ATYPE_FLOAT, 2, 0, false},
		{ATTR_UV,  ATYPE_FLOAT, 2, 0, false},
	};
	static const u32 c_quadAttrCount = TFE_ARRAYSIZE(c_quadAttrMapping);

	bool init()
	{
		if (s_initialized) { return true; }

		if (!s_yuvShader.load("Shaders/yuv2rgb.vert", "Shaders/yuv2rgb.frag"))
		{
			TFE_System::logWrite(LOG_ERROR, "OgvPlayer", "Failed to load YUV->RGB shader.");
			return false;
		}
		s_yuvShader.bindTextureNameToSlot("TexY",  0);
		s_yuvShader.bindTextureNameToSlot("TexCb", 1);
		s_yuvShader.bindTextureNameToSlot("TexCr", 2);
		s_scaleOffsetId = s_yuvShader.getVariableId("ScaleOffset");
		s_uvScaleId     = s_yuvShader.getVariableId("UVScale");

		const QuadVertex vertices[] =
		{
			{0.0f, 0.0f, 0.0f, 0.0f},
			{1.0f, 0.0f, 1.0f, 0.0f},
			{1.0f, 1.0f, 1.0f, 1.0f},
			{0.0f, 1.0f, 0.0f, 1.0f},
		};
		const u16 indices[] = { 0, 1, 2, 0, 2, 3 };
		s_vertexBuffer.create(4, sizeof(QuadVertex), c_quadAttrCount, c_quadAttrMapping, false, (void*)vertices);
		s_indexBuffer.create(6, sizeof(u16), false, (void*)indices);

		s_initialized = true;
		return true;
	}

	void shutdown()
	{
		if (!s_initialized) { return; }
		close();

		s_yuvShader.destroy();
		s_vertexBuffer.destroy();
		s_indexBuffer.destroy();
		s_initialized = false;
	}

	bool open(const char* filepath)
	{
		if (!s_initialized)
		{
			if (!init()) { return false; }
		}
		if (s_playing) { close(); }
		s_file = fopen(filepath, "rb");
		if (!s_file)
		{
			TFE_System::logWrite(LOG_ERROR, "OgvPlayer", "Cannot open file: %s", filepath);
			return false;
		}

		ogg_sync_init(&s_syncState);
		th_info_init(&s_theoraInfo);
		th_comment_init(&s_theoraComment);
		vorbis_info_init(&s_vorbisInfo);
		vorbis_comment_init(&s_vorbisComment);

		s_hasTheora = false;
		s_hasVorbis = false;
		s_theoraStreamInited = false;
		s_vorbisStreamInited = false;
		s_theoraSetup = nullptr;
		s_theoraDec = nullptr;
		s_holdingActive = false;
		s_holdLastFrame = false;

		if (!readOggHeaders())
		{
			TFE_System::logWrite(LOG_ERROR, "OgvPlayer", "Failed to read OGV headers from: %s", filepath);
			close();
			return false;
		}

		if (!s_hasTheora)
		{
			TFE_System::logWrite(LOG_ERROR, "OgvPlayer", "No Theora stream found in: %s", filepath);
			close();
			return false;
		}

		s_theoraDec = th_decode_alloc(&s_theoraInfo, s_theoraSetup);
		if (!s_theoraDec)
		{
			TFE_System::logWrite(LOG_ERROR, "OgvPlayer", "Failed to create Theora decoder.");
			close();
			return false;
		}

		u32 yW = s_theoraInfo.frame_width;
		u32 yH = s_theoraInfo.frame_height;
		u32 cW = yW, cH = yH;
		if (s_theoraInfo.pixel_fmt == TH_PF_420)
		{
			cW = (yW + 1) / 2;
			cH = (yH + 1) / 2;
		}
		else if (s_theoraInfo.pixel_fmt == TH_PF_422)
		{
			cW = (yW + 1) / 2;
		}

		s_texY  = new TextureGpu();
		s_texCb = new TextureGpu();
		s_texCr = new TextureGpu();
		s_texY->create(yW, yH, TEX_R8, false, MAG_FILTER_LINEAR);
		s_texCb->create(cW, cH, TEX_R8, false, MAG_FILTER_LINEAR);
		s_texCr->create(cW, cH, TEX_R8, false, MAG_FILTER_LINEAR);
		s_texY->setFilter(MAG_FILTER_LINEAR, MIN_FILTER_LINEAR);
		s_texCb->setFilter(MAG_FILTER_LINEAR, MIN_FILTER_LINEAR);
		s_texCr->setFilter(MAG_FILTER_LINEAR, MIN_FILTER_LINEAR);

		if (s_hasVorbis)
		{
			vorbis_synthesis_init(&s_vorbisDsp, &s_vorbisInfo);
			vorbis_block_init(&s_vorbisDsp, &s_vorbisBlock);

			s_audioRingSize = AUDIO_BUFFER_SIZE * 2;  // stereo
			s_audioRingBuffer = new f32[s_audioRingSize];
			memset(s_audioRingBuffer, 0, s_audioRingSize * sizeof(f32));
			s_audioWritePos = 0;
			s_audioReadPos = 0;
			s_resampleAccum = 0.0;

			TFE_Audio::setDirectCallback(audioCallback);
		}

		s_videoTime = 0.0;
		s_audioTime = 0.0;
		s_playbackStart = TFE_System::getTime();
		s_firstFrame = true;
		s_playing = true;

		TFE_System::logWrite(LOG_MSG, "OgvPlayer", "Opened OGV: %ux%u, %.2f fps, %s audio (rate=%ld, channels=%d)",
			s_theoraInfo.frame_width, s_theoraInfo.frame_height,
			(f64)s_theoraInfo.fps_numerator / (f64)s_theoraInfo.fps_denominator,
			s_hasVorbis ? "with" : "no",
			s_hasVorbis ? s_vorbisInfo.rate : 0,
			s_hasVorbis ? s_vorbisInfo.channels : 0);

		return true;
	}

	// Replace the body of TFE_OgvPlayer::close() (keep the function signature already present).
	// Ensure you keep the calls that free GPU/OGG resources — only add clearing of the new flags.
	void close()
	{
		// If Vorbis was active we were feeding the audio thread; clear the callback and
		// wait for the audio thread to finish as the original code did.
		if (s_hasVorbis)
		{
			TFE_Audio::setDirectCallback(nullptr);
			TFE_Audio::lock();
			TFE_Audio::unlock();
		}

		freeGpuResources();
		freeOggResources();

		if (s_audioRingBuffer)
		{
			delete[] s_audioRingBuffer;
			s_audioRingBuffer = nullptr;
		}
		s_audioRingSize = 0;
		s_audioWritePos = 0;
		s_audioReadPos = 0;

		if (s_file)
		{
			fclose(s_file);
			s_file = nullptr;
		}

		s_playing = false;
	}

	bool update()
	{
		// If we're in hold-last-frame mode, render the last-decoded frame each tick
		// but return false so the cutscene code can detect EOF/holding and start credits.
		if (s_holdingActive)
		{
			renderFrame();
			return false; 
		}

		if (!s_playing) { return false; }

		if (TFE_Input::keyPressed(KEY_ESCAPE) || TFE_Input::keyPressed(KEY_RETURN) || TFE_Input::keyPressed(KEY_SPACE))
		{
			close();
			return false;
		}

		f64 elapsed = TFE_System::getTime() - s_playbackStart;

		bool gotFrame = false;
		while (s_videoTime <= elapsed)
		{
			if (!decodeVideoFrame())
			{
				// Decoder reported EOF or fatal error.
				if (s_holdLastFrame)
				{
					// Enter holding state: stop decoding but keep the textures and last frame.
					s_playing = false;
					s_holdingActive = true;
					break;
				}
				close();
				return false;
			}
			gotFrame = true;
		}

		if (s_hasVorbis)
		{
			decodeAudioPackets();
		}

		s_firstFrame = false;
		renderFrame();

		return s_playing;
	}

	bool isPlaying()
	{
		return s_playing;
	}

	// Wall-clock seconds since open(). Useful for profiling playback cost
	// and for the cutscene.cpp DCSS timing trace that compares wall-clock
	// drift against the video clock.
	f64 getPlaybackTime()
	{
		if (!s_playing) { return 0.0; }
		return TFE_System::getTime() - s_playbackStart;
	}

	f64 getVideoTime()
	{
		f64 frameDuration = (f64)s_theoraInfo.fps_denominator / (f64)s_theoraInfo.fps_numerator;
		f64 t = s_videoTime - frameDuration;
		return t > 0.0 ? t : 0.0;
	}

	static bool bufferOggData()
	{
		if (!s_file) { return false; }
		char* buffer = ogg_sync_buffer(&s_syncState, OGG_BUFFER_SIZE);
		size_t bytesRead = fread(buffer, 1, OGG_BUFFER_SIZE, s_file);
		ogg_sync_wrote(&s_syncState, (int)bytesRead);
		return bytesRead > 0;
	}

	static bool demuxPage(ogg_page* page)
	{
		if (s_theoraStreamInited)
		{
			ogg_stream_pagein(&s_theoraStream, page);
		}
		if (s_vorbisStreamInited)
		{
			ogg_stream_pagein(&s_vorbisStream, page);
		}
		return true;
	}

	static bool readOggHeaders()
	{
		ogg_page page;
		ogg_packet packet;
		int theoraHeadersNeeded = 0;
		int vorbisHeadersNeeded = 0;

		while (true)
		{
			while (ogg_sync_pageout(&s_syncState, &page) != 1)
			{
				if (!bufferOggData()) { return s_hasTheora;}
			}

			if (ogg_page_bos(&page))
			{
				ogg_stream_state test;
				ogg_stream_init(&test, ogg_page_serialno(&page));
				ogg_stream_pagein(&test, &page);
				ogg_stream_packetpeek(&test, &packet);

				if (!s_hasTheora && th_decode_headerin(&s_theoraInfo, &s_theoraComment, &s_theoraSetup, &packet) >= 0)
				{
					memcpy(&s_theoraStream, &test, sizeof(test));
					s_theoraStreamInited = true;
					s_hasTheora = true;
					theoraHeadersNeeded = 3;
					ogg_stream_packetout(&s_theoraStream, &packet);
					theoraHeadersNeeded--;
				}
				else if (!s_hasVorbis && vorbis_synthesis_headerin(&s_vorbisInfo, &s_vorbisComment, &packet) >= 0)
				{
					memcpy(&s_vorbisStream, &test, sizeof(test));
					s_vorbisStreamInited = true;
					s_hasVorbis = true;
					vorbisHeadersNeeded = 3;
					ogg_stream_packetout(&s_vorbisStream, &packet);
					vorbisHeadersNeeded--;
				}
				else
				{
					ogg_stream_clear(&test);
				}
				continue;
			}

			if (s_theoraStreamInited)
			{
				ogg_stream_pagein(&s_theoraStream, &page);
			}
			if (s_vorbisStreamInited)
			{
				ogg_stream_pagein(&s_vorbisStream, &page);
			}

			while (theoraHeadersNeeded > 0)
			{
				if (ogg_stream_packetout(&s_theoraStream, &packet) != 1) { break; }
				if (th_decode_headerin(&s_theoraInfo, &s_theoraComment, &s_theoraSetup, &packet) < 0)
				{
					TFE_System::logWrite(LOG_ERROR, "OgvPlayer", "Bad Theora header packet.");
					return false;
				}
				theoraHeadersNeeded--;
			}

			while (vorbisHeadersNeeded > 0)
			{
				if (ogg_stream_packetout(&s_vorbisStream, &packet) != 1) { break; }
				if (vorbis_synthesis_headerin(&s_vorbisInfo, &s_vorbisComment, &packet) < 0)
				{
					TFE_System::logWrite(LOG_ERROR, "OgvPlayer", "Bad Vorbis header packet.");
					return false;
				}
				vorbisHeadersNeeded--;
			}

			if (theoraHeadersNeeded <= 0 && vorbisHeadersNeeded <= 0)
			{
				break;
			}
		}

		return s_hasTheora;
	}

	// Theora keeps the keyframe and frame offset stored in the granule position
	// You need to kshift them apart to get the actual frame number.
	ogg_int64_t granulePosToFrameIndex(ogg_int64_t granulePos)
	{
		int kshift = s_theoraInfo.keyframe_granule_shift;
		int kshiftMask = (1 << kshift) - 1; 
		return (granulePos >> kshift) + (granulePos & kshiftMask);
	}

	static bool decodeVideoFrame()
	{
		ogg_packet packet;
		ogg_page page;

		while (ogg_stream_packetout(&s_theoraStream, &packet) != 1)
		{
			while (ogg_sync_pageout(&s_syncState, &page) != 1)
			{
				if (!bufferOggData()) { return false; }
			}
			if (s_theoraStreamInited && ogg_page_serialno(&page) == s_theoraStream.serialno)
			{
				ogg_stream_pagein(&s_theoraStream, &page);
			}
			if (s_vorbisStreamInited && ogg_page_serialno(&page) == s_vorbisStream.serialno)
			{
				ogg_stream_pagein(&s_vorbisStream, &page);
			}
		}

		ogg_int64_t granulePos = -1;
		ogg_int64_t frameIndex = 0;

		if (th_decode_packetin(s_theoraDec, &packet, &granulePos) == 0)
		{
			f64 frameDuration = (f64)s_theoraInfo.fps_denominator / (f64)s_theoraInfo.fps_numerator;

			// Compute the frame index by kshifting
			frameIndex = granulePosToFrameIndex(granulePos);

			// There are two types of cases depending on the packet.
			// If it is the LAST one, the timestamp is known from the granule position.
			// If it is a MID-PAGE packet, then we have to calculate the offset ourselves.

			// LAST ONE 
			if (packet.granulepos >= 0)
			{
				// This is the last packet - we just load the sequence number. 
				ogg_int64_t frameSequence = granulePosToFrameIndex(packet.granulepos);

				// Once you load the frame you need to know how long to display it based on the fps. 
				s_videoTime = frameSequence * frameDuration;

				// Store the last position so we can use it if we are in the MID-PAGE case.
				granuleOffset = frameSequence - frameIndex;
			}

			// MID-PAGE 
			else
			{
				// TLDR explanation
				// 
				// Theora sometimes allows you to freeze a frame
				// by encoding a gap in the granule sequence. When that happens, the decoder's
				// frameIndex can drift behind the raw page-level sequence. So we apply a granuleOffset

				// Long explanation
				// 
				// Ok so the original code just incremented the video time by each frame. The issue is
				// the part where vader looks out into space at frame 627 only lasts for 10 frames until 637
				// That's 10 frames played at 30 fps so it shows up really for 1/3 second. 
				// However when you play the video in the DF Remaster or any player (Ex: VLC) it will show
				// Vader looking out into space for almost 2 seconds. What gives? That's almost 60 frames ! 			 
				// 
				// The original code just incremented the video time by each frame, which would not account for theora gaps,
				// so it would show the freeze frame for much less time than intended. Which caused audio and video desync.
				// 
				// By calculating the granuleOffset when we encounter a packet with a valid granule position,
				// we can then apply that offset to subsequent packets that don't have a valid granule position,
				// ensuring that s_videoTime correctly reflects the intended timing of the frames, 
				// including any freeze frames.
				// 
				// So with this fix you find out that you freeze the Vader frames without popping new ones
				// for about 55 frames total (10 normal frames + 45 freeze frames) which at 30 fps is about 1.8 seconds
				// Whoo... isn't Theora "fun"? That was fun to debug.... At least Vader looks good now. 
				s_videoTime = (frameIndex + 1 + granuleOffset) * frameDuration;
			}

			th_ycbcr_buffer ycbcr;
			th_decode_ycbcr_out(s_theoraDec, ycbcr);
			uploadYuvFrame(ycbcr);
		}

		return true;
	}

	static u32 audioRingAvailable()
	{
		u32 w = s_audioWritePos;
		u32 r = s_audioReadPos;
		return (w >= r) ? (w - r) : (s_audioRingSize - r + w);
	}

	// Resample pending vorbis PCM into the ring buffer. Returns false if full.
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
				u32 w = s_audioWritePos;
				u32 r = s_audioReadPos;
				u32 used = (w >= r) ? (w - r) : (s_audioRingSize - r + w);
				if (used >= s_audioRingSize - 2) { return false; }  // Ring full

				s32 idx = (s32)s_resampleAccum;
				s_audioRingBuffer[w] = pcm[0][idx];
				w = (w + 1) % s_audioRingSize;
				s_audioRingBuffer[w] = (channels > 1) ? pcm[1][idx] : pcm[0][idx];
				w = (w + 1) % s_audioRingSize;
				s_audioWritePos = w;
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

	static void decodeAudioPackets()
	{
		if (!s_hasVorbis) { return; }

		drainVorbisPackets();

		// Keep at least ~0.19s of audio buffered.
		ogg_page page;
		while (audioRingAvailable() < 8192 * 2)
		{
			while (ogg_sync_pageout(&s_syncState, &page) != 1)
			{
				if (!bufferOggData()) { return; }  // EOF
			}
			if (s_vorbisStreamInited && ogg_page_serialno(&page) == s_vorbisStream.serialno)
			{
				ogg_stream_pagein(&s_vorbisStream, &page);
			}
			if (s_theoraStreamInited && ogg_page_serialno(&page) == s_theoraStream.serialno)
			{
				ogg_stream_pagein(&s_theoraStream, &page);
			}
			drainVorbisPackets();
		}
	}

	// Audio thread callback - mixes OGV audio into the output buffer.
	static void audioCallback(f32* buffer, u32 frameCount, f32 systemVolume)
	{
		u32 samplesToFill = frameCount * 2;

		for (u32 i = 0; i < samplesToFill; i++)
		{
			if (s_audioReadPos != s_audioWritePos)
			{
				buffer[i] += s_audioRingBuffer[s_audioReadPos] * systemVolume;
				s_audioReadPos = (s_audioReadPos + 1) % s_audioRingSize;
			}
		}
	}

	static void uploadYuvFrame(th_ycbcr_buffer ycbcr)
	{
		if (!s_texY || !s_texCb || !s_texCr) { return; }

		{  // Y plane
			u32 w = ycbcr[0].width;
			u32 h = ycbcr[0].height;
			if (ycbcr[0].stride == (s32)w)
			{
				s_texY->update(ycbcr[0].data, w * h);
			}
			else
			{
				std::vector<u8> temp(w * h);
				for (u32 row = 0; row < h; row++)
				{
					memcpy(&temp[row * w], ycbcr[0].data + row * ycbcr[0].stride, w);
				}
				s_texY->update(temp.data(), w * h);
			}
		}

		{  // Cb plane
			u32 w = ycbcr[1].width;
			u32 h = ycbcr[1].height;
			if (ycbcr[1].stride == (s32)w)
			{
				s_texCb->update(ycbcr[1].data, w * h);
			}
			else
			{
				std::vector<u8> temp(w * h);
				for (u32 row = 0; row < h; row++)
				{
					memcpy(&temp[row * w], ycbcr[1].data + row * ycbcr[1].stride, w);
				}
				s_texCb->update(temp.data(), w * h);
			}
		}

		{  // Cr plane
			u32 w = ycbcr[2].width;
			u32 h = ycbcr[2].height;
			if (ycbcr[2].stride == (s32)w)
			{
				s_texCr->update(ycbcr[2].data, w * h);
			}
			else
			{
				std::vector<u8> temp(w * h);
				for (u32 row = 0; row < h; row++)
				{
					memcpy(&temp[row * w], ycbcr[2].data + row * ycbcr[2].stride, w);
				}
				s_texCr->update(temp.data(), w * h);
			}
		}
	}

	static void renderFrame()
	{
		TFE_RenderBackend::unbindRenderTarget();
		DisplayInfo display;
		TFE_RenderBackend::getDisplayInfo(&display);
		TFE_RenderBackend::setViewport(0, 0, display.width, display.height);
		glClear(GL_COLOR_BUFFER_BIT);

		TFE_RenderState::setStateEnable(false, STATE_CULLING | STATE_BLEND | STATE_DEPTH_TEST);

		f32 dispW = (f32)display.width;
		f32 dispH = (f32)display.height;
		f32 vidW  = (f32)s_theoraInfo.pic_width;
		f32 vidH  = (f32)s_theoraInfo.pic_height;

		f32 scaleX, scaleY, offsetX, offsetY;
		f32 vidAspect = vidW / vidH;
		f32 dispAspect = dispW / dispH;

		if (vidAspect > dispAspect)
		{
			scaleX = 2.0f;
			scaleY = 2.0f * (dispAspect / vidAspect);
			offsetX = -1.0f;
			offsetY = -scaleY * 0.5f;
		}
		else
		{
			scaleX = 2.0f * (vidAspect / dispAspect);
			scaleY = 2.0f;
			offsetX = -scaleX * 0.5f;
			offsetY = -1.0f;
		}

		const f32 scaleOffset[] = { scaleX, scaleY, offsetX, offsetY };

		s_yuvShader.bind();
		s_yuvShader.setVariable(s_scaleOffsetId, SVT_VEC4, scaleOffset);

		// Crop V to the picture region: Theora frames are padded to the
		// next multiple of 16 (e.g. 1088 for a 1080-line video). Sampling
		// beyond pic_height/frame_height reads garbage padding rows.
		const f32 uvScale = (f32)s_theoraInfo.pic_height / (f32)s_theoraInfo.frame_height;
		s_yuvShader.setVariable(s_uvScaleId, SVT_SCALAR, &uvScale);

		s_texY->bind(0);
		s_texCb->bind(1);
		s_texCr->bind(2);

		s_vertexBuffer.bind();
		s_indexBuffer.bind();

		TFE_RenderBackend::drawIndexedTriangles(2, sizeof(u16));

		s_vertexBuffer.unbind();
		s_indexBuffer.unbind();

		TextureGpu::clearSlots(3, 0);
		Shader::unbind();

		// Skip the normal virtual display blit since we drew directly to the backbuffer.
		TFE_RenderBackend::setSkipDisplayAndClear(true);
	}

	static void freeGpuResources()
	{
		if (s_texY)  { delete s_texY;  s_texY  = nullptr; }
		if (s_texCb) { delete s_texCb; s_texCb = nullptr; }
		if (s_texCr) { delete s_texCr; s_texCr = nullptr; }
	}

	static void freeOggResources()
	{
		if (s_theoraDec)
		{
			th_decode_free(s_theoraDec);
			s_theoraDec = nullptr;
		}
		if (s_theoraSetup)
		{
			th_setup_free(s_theoraSetup);
			s_theoraSetup = nullptr;
		}

		if (s_hasVorbis)
		{
			vorbis_block_clear(&s_vorbisBlock);
			vorbis_dsp_clear(&s_vorbisDsp);
		}

		if (s_vorbisStreamInited)
		{
			ogg_stream_clear(&s_vorbisStream);
			s_vorbisStreamInited = false;
		}
		if (s_theoraStreamInited)
		{
			ogg_stream_clear(&s_theoraStream);
			s_theoraStreamInited = false;
		}

		th_comment_clear(&s_theoraComment);
		th_info_clear(&s_theoraInfo);
		vorbis_comment_clear(&s_vorbisComment);
		vorbis_info_clear(&s_vorbisInfo);

		ogg_sync_clear(&s_syncState);

		s_hasTheora = false;
		s_hasVorbis = false;
	}

	void setHoldLastFrame(bool enable)
	{
		s_holdLastFrame = enable;
	}

	bool isHoldingLastFrameActive()
	{
		return s_holdingActive;
	}

}  // TFE_OgvPlayer

#endif // ENABLE_OGV_CUTSCENES
