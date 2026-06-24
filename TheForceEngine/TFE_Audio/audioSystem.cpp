#include <cstring>
#include "audioSystem.h"
#include "audioDevice.h"
#include "midiPlayer.h"
#include <SDL_mutex.h>
#include <TFE_System/system.h>
#include <TFE_System/math.h>
#include <TFE_Settings/settings.h>
#include <TFE_FrontEndUI/console.h>
#include <TFE_System/profiler.h>
#include <assert.h>
#include <algorithm>

// Comment out the desired sigmoid function and comment all of the others.
//#define AUDIO_SIGMOID_CLIP 1
#define AUDIO_SIGMOID_TANH 1
//#define AUDIO_SIGMOID_RCP_SQRT 1

// Volume level below which sound processing can be skipped.
#define SND_CULL_VOLUME 0.0001f

// Set to 1 to enable audio timing counters.
#define AUDIO_TIMING 0

enum SoundSourceFlags
{
	SND_FLAG_ONE_SHOT = (1 << 0),
	SND_FLAG_ACTIVE   = (1 << 1),
	SND_FLAG_LOOPING  = (1 << 2),
	SND_FLAG_PLAYING  = (1 << 3),
	SND_FLAG_FINISHED = (1 << 4),
};

struct SoundSource
{
	SoundType type;
	f32 volume;
	u32 sampleIndex;
	u32 flags;
	s32 slot;

	// Sound data.
	const SoundBuffer* buffer;

	// Callback.
	SoundFinishedCallback finishedCallback = nullptr;
	void* finishedUserData = nullptr;
	s32 finishedArg = 0;
};

namespace TFE_Audio
{
	static const f32 c_channelLimit  = 1.0f;
	static const f32 c_soundHeadroom = 0.7f;

	enum
	{
		AUDIO_FREQ = 44100,
		AUDIO_CHANNEL_COUNT = 2,
		AUDIO_FRAME_SIZE = 1024,
		AUDIO_CALLBACK_BUFFER_SIZE = 256,	// 256
		BUFFERED_SILENT_FRAME_COUNT = 16,
	};

	// Client volume controls, ranging from [0, 1]
	static f32 s_soundFxVolume = 1.0f;

	static u32 s_sourceCount;
	static SoundSource s_sources[MAX_SOUND_SOURCES];
	static SDL_mutex* s_mutex;
	static bool s_paused = false;
	static bool s_nullDevice = false;
	static volatile s32 s_silentAudioFrames = 0;

	static AudioUpsampleFilter s_upsampleFilter = AUF_DEFAULT;
	static AudioThreadCallback s_audioThreadCallback = nullptr;
	static AudioDirectCallback s_directCallback = nullptr;

	static void audioCallback(void*, unsigned char*, int);
	void setSoundVolumeConsole(const ConsoleArgList& args);
	void getSoundVolumeConsole(const ConsoleArgList& args);

#if AUDIO_TIMING == 1
	static f64 s_soundIterMaxF = 0.0;
	static f64 s_soundIterAveF = 0.0;
	static s32 s_soundIterMax = 0;
	static s32 s_soundIterAve = 0;
#endif

	bool init(bool useNullDevice/*=false*/, s32 outputId/*=-1*/)
	{
		TFE_System::logWrite(LOG_MSG, "Startup", "TFE_AudioSystem::init");
		s_sourceCount = 0u;

		CCMD("setSoundVolume", setSoundVolumeConsole, 1, "Sets the sound volume, range is 0.0 to 1.0");
		CCMD("getSoundVolume", getSoundVolumeConsole, 0, "Get the current sound volume.");

	#if AUDIO_TIMING == 1
		TFE_COUNTER(s_soundIterMax, "SoundIterMax-MicroSec");
		TFE_COUNTER(s_soundIterAve, "SoundIterAve-MicroSec");
	#endif

		TFE_Settings_Sound* soundSettings = TFE_Settings::getSoundSettings();
		setVolume(soundSettings->soundFxVolume);

		memset(s_sources, 0, sizeof(SoundSource) * MAX_SOUND_SOURCES);
		for (s32 i = 0; i < MAX_SOUND_SOURCES; i++)
		{
			s_sources[i].slot = i;
		}

		bool audDev = TFE_AudioDevice::init(AUDIO_FRAME_SIZE, outputId, useNullDevice);
		if (!audDev)
		{
			TFE_System::logWrite(LOG_ERROR, "Audio", "Cannot start audio device.");
			s_nullDevice = true;
			return false;
		}

		bool audStream = TFE_AudioDevice::startOutput(audioCallback, nullptr, AUDIO_CHANNEL_COUNT, AUDIO_FREQ);
		if (!audStream)
		{
			TFE_System::logWrite(LOG_ERROR, "Audio", "Cannot start audio stream.");
			s_nullDevice = true;
			return false;
		}

		s_mutex = SDL_CreateMutex();
		if (!s_mutex)
		{
			TFE_System::logWrite(LOG_ERROR, "Audio", "Cannot init SDL_mutex.");
			TFE_AudioDevice::destroy();
			s_nullDevice = true;
			return false;
		}

		s_nullDevice = false;
		return true;
	}

	void shutdown()
	{
		TFE_System::logWrite(LOG_MSG, "Audio", "Shutdown");
		if (s_nullDevice) { return; }

		stopAllSounds();

		TFE_AudioDevice::destroy();
		SDL_DestroyMutex(s_mutex);
	}

	void stopAllSounds()
	{
		if (s_nullDevice) { return; }

		SDL_LockMutex(s_mutex);
		s_sourceCount = 0u;
		memset(s_sources, 0, sizeof(SoundSource) * MAX_SOUND_SOURCES);
		for (s32 i = 0; i < MAX_SOUND_SOURCES; i++)
		{
			s_sources[i].slot = i;
		}
		SDL_UnlockMutex(s_mutex);
	}

	void selectDevice(s32 id)
	{
		if (id < 0)
		{
			id = TFE_AudioDevice::getDefaultOutputDevice();
		}

		if (id != TFE_AudioDevice::getOutputDeviceId() && id >= 0 && id < TFE_AudioDevice::getOutputDeviceCount())
		{
			shutdown();
			init(false, id);
		}
	}
		
	void setUpsampleFilter(AudioUpsampleFilter filter)
	{
		s_upsampleFilter = filter;
	}

	AudioUpsampleFilter getUpsampleFilter()
	{
		return s_upsampleFilter;
	}

	void setVolume(f32 volume)
	{
		s_soundFxVolume = volume;
	}

	f32 getVolume()
	{
		return s_soundFxVolume;
	}

	void pause()
	{
		SDL_LockMutex(s_mutex);
		s_paused = true;
		SDL_UnlockMutex(s_mutex);
	}

	void resume()
	{
		SDL_LockMutex(s_mutex);
		s_paused = false;
		SDL_UnlockMutex(s_mutex);
	}

	// Really the buffered audio will continue to process so time advances properly.
	// But for 'BUFFERED_SILENT_FRAME_COUNT' audio will be silent.
	// This allows the buffered data to be consumed without audio hitches.
	void bufferedAudioClear()
	{
		s_silentAudioFrames = BUFFERED_SILENT_FRAME_COUNT;
	}
		
	void setAudioThreadCallback(AudioThreadCallback callback)
	{
		if (s_nullDevice) { return; }

		SDL_LockMutex(s_mutex);
		s_audioThreadCallback = callback;
		SDL_UnlockMutex(s_mutex);
	}

	void setDirectCallback(AudioDirectCallback callback)
	{
		if (s_nullDevice) { return; }

		SDL_LockMutex(s_mutex);
		s_directCallback = callback;
		SDL_UnlockMutex(s_mutex);
	}

	const OutputDeviceInfo* getOutputDeviceList(s32& count, s32& curOutput)
	{
		return TFE_AudioDevice::getOutputDeviceList(count, curOutput);
	}

	void lock()
	{
		if (s_nullDevice) { return; }
		SDL_LockMutex(s_mutex);
	}

	void unlock()
	{
		if (s_nullDevice) { return; }
		SDL_UnlockMutex(s_mutex);
	}

	// One shot, play and forget. Only do this if the client needs no control until stopAllSounds() is called.
	// Note that looping one shots are valid.
	bool playOneShot(SoundType type, f32 volume, const SoundBuffer* buffer, bool looping, SoundFinishedCallback finishedCallback, void* cbUserData, s32 cbArg)
	{
		if (!buffer || s_nullDevice) { return false; }

		SDL_LockMutex(s_mutex);
		// Find the first inactive source.
		SoundSource* snd = s_sources;
		SoundSource* newSource = nullptr;
		for (u32 s = 0; s < s_sourceCount; s++, snd++)
		{
			if (!(snd->flags&SND_FLAG_ACTIVE))
			{
				newSource = snd;
				break;
			}
		}
		if (!newSource && s_sourceCount < MAX_SOUND_SOURCES)
		{
			newSource = &s_sources[s_sourceCount];
			s_sourceCount++;
		}

		if (newSource)
		{
			newSource->type = type;
			newSource->flags = SND_FLAG_ACTIVE | SND_FLAG_PLAYING | SND_FLAG_ONE_SHOT;
			if (looping)
			{
				newSource->flags |= SND_FLAG_LOOPING;
			}
			newSource->volume = type == SOUND_3D ? 0.0f : volume;
			newSource->buffer = buffer;
			newSource->sampleIndex = 0u;
			newSource->finishedCallback = finishedCallback;
			newSource->finishedUserData = cbUserData;
			newSource->finishedArg = cbArg;
		}
		SDL_UnlockMutex(s_mutex);

		return newSource != nullptr;
	}

	// Sound source that the client holds onto.
	SoundSource* createSoundSource(SoundType type, f32 volume, const SoundBuffer* buffer, SoundFinishedCallback callback, void* userData)
	{
		if (!buffer || s_nullDevice) { return nullptr; }
		assert(volume >= 0.0f && volume <= 1.0f);

		SDL_LockMutex(s_mutex);
		// Find the first inactive source.
		SoundSource* snd = s_sources;
		SoundSource* newSource = nullptr;
		for (u32 s = 0; s < s_sourceCount; s++, snd++)
		{
			if (!(snd->flags&SND_FLAG_ACTIVE))
			{
				newSource = snd;
				break;
			}
		}
		if (!newSource && s_sourceCount < MAX_SOUND_SOURCES)
		{
			newSource = &s_sources[s_sourceCount];
			s_sourceCount++;
		}

		if (newSource)
		{
			newSource->type = type;
			newSource->flags = SND_FLAG_ACTIVE;
			newSource->volume = volume;
			newSource->buffer = buffer;
			newSource->sampleIndex = 0u;
			newSource->finishedCallback = callback;
			newSource->finishedUserData = userData;
		}
		SDL_UnlockMutex(s_mutex);

		return newSource;
	}

	s32 getSourceSlot(SoundSource* source)
	{
		if (!source) { return -1; }
		return s_nullDevice ? -1 : source->slot;
	}

	SoundSource* getSourceFromSlot(s32 slot)
	{
		if (s_nullDevice) { return nullptr; }

		if (slot < 0 || slot >= MAX_SOUND_SOURCES)
		{
			return nullptr;
		}
		if (!(s_sources[slot].flags & SND_FLAG_ACTIVE))
		{
			return nullptr;
		}

		return &s_sources[slot];
	}

	void playSource(SoundSource* source, bool looping)
	{
		if (!source || (source->flags & SND_FLAG_PLAYING) || s_nullDevice)
		{
			return;
		}
		
		SDL_LockMutex(s_mutex);
			source->flags |= SND_FLAG_PLAYING;
			if (looping) { source->flags |= SND_FLAG_LOOPING; }
			source->sampleIndex = 0u;
		SDL_UnlockMutex(s_mutex);
	}

	void stopSource(SoundSource* source)
	{
		if (!source || s_nullDevice) { return; }
		SDL_LockMutex(s_mutex);
			source->flags &= ~SND_FLAG_PLAYING;
		SDL_UnlockMutex(s_mutex);
	}
	
	void freeSource(SoundSource* source)
	{
		if (!source || s_nullDevice) { return; }
		SDL_LockMutex(s_mutex);
			source->flags &= ~SND_FLAG_PLAYING;
			source->flags &= ~SND_FLAG_ACTIVE;
			source->buffer = nullptr;
		SDL_UnlockMutex(s_mutex);
	}

	void setSourceVolume(SoundSource* source, f32 volume)
	{
		if (s_nullDevice) { return; }
		source->volume = std::max(0.0f, std::min(1.0f, volume));
	}

	// This will restart the sound and change the buffer.
	void setSourceBuffer(SoundSource* source, const SoundBuffer* buffer)
	{
		if (s_nullDevice) { return; }
		SDL_LockMutex(s_mutex);
			source->sampleIndex = 0u;
			source->buffer = buffer;
		SDL_UnlockMutex(s_mutex);
	}

	bool isSourcePlaying(SoundSource* source)
	{
		if (s_nullDevice) { return false; }
		return (source->flags & SND_FLAG_PLAYING) != 0u;
	}

	f32 getSourceVolume(SoundSource* source)
	{
		if (s_nullDevice) { return 0.0f; }
		return source->volume;
	}

	// Internal
	static const f32 c_scale[] = { 2.0f / 255.0f, 2.0f / 65535.0f, 1.0f };
	static const f32 c_offset[] = { -1.0f, -1.0f, 0.0f };

	void cleanupSources()
	{
		// call any finished callbacks.
		for (u32 s = 0; s < s_sourceCount; s++)
		{
			if (s_sources[s].flags&SND_FLAG_FINISHED)
			{
				s_sources[s].flags = 0;
				s_sources[s].buffer = nullptr;
				if (s_sources[s].finishedCallback)
				{
					s_sources[s].finishedCallback(s_sources[s].finishedUserData, s_sources[s].finishedArg);
				}
			}
		}

		const s32 end = (s32)s_sourceCount - 1;
		//shrink the number of sources until an active source is found.
		for (s32 s = end; s >= 0; s--)
		{
			if (s_sources[s].flags&SND_FLAG_ACTIVE)
			{
				break;
			}
			s_sourceCount--;
		}
	}
		
	f32 sampleBuffer(u32 index, SoundDataType type, const u8* data)
	{
		f32 sampleValue = 0.0f;
		switch (type)
		{
			case SOUND_DATA_8BIT:  { sampleValue = (f32)data[index]; } break;
			case SOUND_DATA_16BIT: { sampleValue = (f32)(*((u16*)data + index)); } break;
			case SOUND_DATA_FLOAT: { sampleValue = *((f32*)data + index); } break;
		};

		return sampleValue * c_scale[type] + c_offset[type];
	}
			
	// Audio callback
	static void audioCallback(void* userData, unsigned char* outputBuffer, int bufsize)
	{
		f32* buffer = (f32*)outputBuffer;
		u32 bufferSize = (u32)bufsize;
		u32 frames = bufferSize / (AUDIO_CHANNEL_COUNT * sizeof(f32));

	#if AUDIO_TIMING == 1
		u64 soundIterStart = TFE_System::getCurrentTimeInTicks();
	#endif

		// First clear samples
		memset(buffer, 0, bufferSize);

		SDL_LockMutex(s_mutex);
		// Call the audio thread callback (iMuse/game audio).
		if (s_audioThreadCallback && !s_paused)
		{
			static f32 callbackBuffer[(AUDIO_CALLBACK_BUFFER_SIZE + 2)*AUDIO_CHANNEL_COUNT];	// 256 stereo + oversampling.
			s_audioThreadCallback(callbackBuffer, AUDIO_CALLBACK_BUFFER_SIZE, s_soundFxVolume * c_soundHeadroom);
			// The audio buffer is 1/4 as large as it should be.
			// This means that in-between samples must be interpolated.
			if (!s_silentAudioFrames)
			{
				if (s_upsampleFilter == AUF_NONE)
				{
					upsample4x_point(buffer, callbackBuffer, AUDIO_CALLBACK_BUFFER_SIZE*AUDIO_CHANNEL_COUNT);
				}
				else if (s_upsampleFilter == AUF_LINEAR)
				{
					upsample4x_linear(buffer, callbackBuffer, AUDIO_CALLBACK_BUFFER_SIZE*AUDIO_CHANNEL_COUNT);
				}
			}
		}
		// Direct callback adds at the full output rate (e.g. OGV video audio), mixing on top.
		if (s_directCallback && !s_paused)
		{
			s_directCallback(buffer, frames, s_soundFxVolume * c_soundHeadroom);
		}

		// Then loop through the sources.
		// Note: this is no longer used by Dark Forces. However I decided to keep direct sound support around
		// so it can be used for tools.
		SoundSource* snd = s_sources;
		for (u32 s = 0; s < s_sourceCount && !s_paused; s++, snd++)
		{
			if (!(snd->flags&SND_FLAG_PLAYING)) { continue; }
			assert(snd->buffer->data);

			// Skip sound sample processing the sound is too quiet...
			const u32 sndBufferSize = snd->buffer->size;
			if (snd->volume < SND_CULL_VOLUME)
			{
				// Pretend we played the sound and handle looping.
				snd->sampleIndex += frames;
				if (snd->sampleIndex >= sndBufferSize)
				{
					if (snd->flags&SND_FLAG_LOOPING)
					{
						snd->sampleIndex = (snd->sampleIndex % sndBufferSize) + snd->buffer->loopStart;
					}
					else
					{
						snd->flags &= ~SND_FLAG_PLAYING;
						snd->flags |= SND_FLAG_FINISHED;
						snd->sampleIndex = 0u;
					}
				}
				continue;
			}

			// Sample loop.
			buffer = (f32*)outputBuffer;
			// The sound may be split into multiple iterations if it loops or the loop
			// may end early, once we reach the end.
			for (u32 i = 0; i < frames;)
			{
				if (snd->sampleIndex >= sndBufferSize)
				{
					if (snd->flags&SND_FLAG_LOOPING)
					{
						snd->sampleIndex = snd->buffer->loopStart;
					}
					else
					{
						snd->flags &= ~SND_FLAG_PLAYING;
						snd->flags |= SND_FLAG_FINISHED;
						snd->sampleIndex = 0u;
						break;
					}
				}

				const SoundDataType type = snd->buffer->type;
				const u8* data = snd->buffer->data;
				const u32 end = std::min(sndBufferSize, snd->sampleIndex + frames - i);
				u32 sIndex = snd->sampleIndex;
				for (; sIndex < end; i++, sIndex++, buffer += 2)
				{
					const f32 sample = sampleBuffer(sIndex, type, data) * snd->volume;
					buffer[0] += sample;
					buffer[1] += sample;
				}
				snd->sampleIndex = sIndex;
			}
		}
		// Cleanup sound sources while we are still in the mutex.
		cleanupSources();
		
		// Handle midi synthesis results.
		if (!s_paused)
		{
			TFE_MidiPlayer::synthesizeMidi((f32*)outputBuffer, frames, !s_silentAudioFrames);
		}
		if (s_silentAudioFrames > 0) { s_silentAudioFrames--; }
		SDL_UnlockMutex(s_mutex);

		// Handle out of range audio samples.
		buffer = (f32*)outputBuffer;
		for (u32 i = 0; i < frames; i++, buffer += 2)
		{
			const f32 valueLeft  = buffer[0];
			const f32 valueRight = buffer[1];

			// Audio outside of the [-1, 1] range will cause overflow, which is a major artifact.
			// Instead the audio needs to be limited in range, which can be done in several ways.
			// Sigmoid functions map an arbitrary range into [-1, 1] generall along an S-Curve, allowing us to avoid overflow.
		#if defined(AUDIO_SIGMOID_CLIP)		// Not really a Sigmoid function but acts in a similar way, naively mapping to the required range.
			buffer[0] = std::max(-c_channelLimit, std::min(valueLeft,  c_channelLimit));
			buffer[1] = std::max(-c_channelLimit, std::min(valueRight, c_channelLimit));
		#elif defined(AUDIO_SIGMOID_TANH)	// Considered one of the most "musical sounding" sigmoid functions, it avoids hard clipping.
			// Note the usable range is approximately -4.8 to 4.8 so the volumes should be adjusted to stay within those ranges when possible.
			// Still much better than the effect -1 to 1 range with hard clipping and cheaper than the more accurate library tanh(). :)
			buffer[0] = TFE_Math::tanhf_series(valueLeft);
			buffer[1] = TFE_Math::tanhf_series(valueRight);
		#elif defined(AUDIO_SIGMOID_RCP_SQRT)
			buffer[0] = valueLeft  / sqrtf(1.0f + valueLeft * valueLeft);
			buffer[1] = valueRight / sqrtf(1.0f + valueRight * valueRight);
		#endif
		}

		// Timing
	#if AUDIO_TIMING == 1
		u64 soundIterEnd = TFE_System::getCurrentTimeInTicks();
		f64 soundIterDeltaMS = 1000000.0 * TFE_System::convertFromTicksToSeconds(soundIterEnd - soundIterStart);
		s_soundIterAveF = soundIterDeltaMS * 0.01 + s_soundIterAveF * 0.99;
		s_soundIterMaxF = std::max(s_soundIterMaxF, soundIterDeltaMS);
		s_soundIterAve = s32(s_soundIterAveF);
		s_soundIterMax = s32(s_soundIterMaxF);
	#endif
	}

	// Console functions.
	void setSoundVolumeConsole(const ConsoleArgList& args)
	{
		if (args.size() < 2) { return; }

		s_soundFxVolume = TFE_Console::getFloatArg(args[1]);
		
		TFE_Settings_Sound* soundSettings = TFE_Settings::getSoundSettings();
		soundSettings->soundFxVolume = s_soundFxVolume;
		TFE_Settings::writeToDisk();
	}

	void getSoundVolumeConsole(const ConsoleArgList& args)
	{
		char res[256];
		sprintf(res, "Sound Volume: %2.3f", s_soundFxVolume);
		TFE_Console::addToHistory(res);
	}
}
