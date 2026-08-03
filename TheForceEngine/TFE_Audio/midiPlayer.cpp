#include "midiPlayer.h"
#include "midiDevice.h"
#include "audioDevice.h"
#ifdef BUILD_SYSMIDI
#include "systemMidiDevice.h"
#endif
#include <SDL_mutex.h>
#include <SDL_thread.h>
#include <TFE_Asset/gmidAsset.h>
#include <TFE_System/system.h>
#include <TFE_Settings/settings.h>
#include <TFE_FrontEndUI/console.h>
#include <TFE_Audio/MidiSynth/soundFontDevice.h>
#include <TFE_Audio/MidiSynth/fm4Opl3Device.h>
#include <algorithm>
#include <assert.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>
#undef min
#undef max
#endif

using namespace TFE_Audio;

namespace TFE_MidiPlayer
{
	enum MidiPlayerCmd
	{
		MIDI_PAUSE,
		MIDI_RESUME,
		MIDI_CHANGE_VOL,
		MIDI_STOP_NOTES,
		MIDI_COUNT
	};

	struct MidiCmd
	{
		MidiPlayerCmd cmd;
		f32 newVolume;

		static inline MidiCmd pause()
		{
			return { MIDI_PAUSE };
		}

		static inline MidiCmd resume()
		{
			return { MIDI_RESUME };
		}

		static inline MidiCmd stop()
		{
			return { MIDI_STOP_NOTES };
		}

		static inline MidiCmd changeVol(f32 vol)
		{
			return { MIDI_CHANGE_VOL, vol };
		}
	};

	enum { MAX_MIDI_CMD = 256 };
	static MidiCmd s_midiCmdBuffer[MAX_MIDI_CMD];
	static u32 s_midiCmdCount = 0;
	static f64 s_maxNoteLength = 16.0;		// defaults to 16 seconds.

	struct MidiCallback
	{
		void(*callback)(void) = nullptr;	// callback function to call.
		f64 timeStep = 0.0;					// delay between calls, this acts like an interrupt handler.
		f64 accumulator = 0.0;				// current accumulator.
	};
		
	static const f32 c_musicVolumeScale = 0.75f;
	static f32 s_masterVolume = 1.0f;
	static f32 s_masterVolumeScaled = s_masterVolume * c_musicVolumeScale;
	static SDL_Thread* s_thread = nullptr;
	static bool s_tPaused = false;

	static bool s_threadAlive = true;
	static u8 s_channelSrcVolume[MIDI_CHANNEL_COUNT] = { 0 };
	static SDL_mutex* s_midiThreadMutex = nullptr;
	static SDL_mutex* s_deviceChangeMutex = nullptr;
	static SDL_cond* s_midiCmdCond;

	static MidiDevice* s_midiDevice = nullptr;
	static MidiCallback s_midiCallback = {};

	static std::vector<f32> s_sampleBuffer;
	static f32* s_sampleBufferPtr = nullptr;

	// Hanging note detection.
	struct Instrument
	{
		u32 channelMask;
		f64 time[MIDI_CHANNEL_COUNT];
	};
	static Instrument s_instrOn[MIDI_INSTRUMENT_COUNT] = { 0 };
	static f64 s_curNoteTime = 0.0;

	int midiUpdateFunc(void* userData);
	void stopAllNotes();
	void changeVolume();
	void allocateMidiDevice(MidiDeviceType type);

	// Console Functions
	void setMusicVolumeConsole(const ConsoleArgList& args);
	void getMusicVolumeConsole(const ConsoleArgList& args);

	static const char* c_midiDeviceTypes[] =
	{
		"SF2 Synthesized Midi", // MIDI_TYPE_SF2
		"OPL3 Synthesized Midi",// MIDI_TYPE_OPL3
#ifdef BUILD_SYSMIDI
		"System Midi",		// MIDI_TYPE_SYSTEM
#endif
	};

	bool init(s32 midiDeviceIndex, MidiDeviceType type)
	{
		TFE_System::logWrite(LOG_MSG, "Startup", "TFE_MidiPlayer::init");
		bool res = false;

		s_midiThreadMutex = SDL_CreateMutex();
		if (!s_midiThreadMutex)
		{
			TFE_System::logWrite(LOG_ERROR, "Midi", "cannot initialize SDL midi thread mutex");
			return false;
		}
		s_midiCmdCond = SDL_CreateCond();
		if (!s_midiCmdCond)
		{
			SDL_DestroyMutex(s_midiThreadMutex);
			TFE_System::logWrite(LOG_ERROR, "Midi", "cannot initialize SDL condition");
			return false;
		}

		s_deviceChangeMutex = SDL_CreateMutex();
		if (!s_deviceChangeMutex)
		{
			SDL_DestroyMutex(s_midiThreadMutex);
			SDL_DestroyCond(s_midiCmdCond);
			TFE_System::logWrite(LOG_ERROR, "Midi", "cannot initialize SDL device change mutex");
			return false;
		}

		SDL_LockMutex(s_deviceChangeMutex);
		{
			allocateMidiDevice(type);
			if (s_midiDevice)
			{
				res = true;
				if (!s_midiDevice->selectOutput(midiDeviceIndex))
				{
					if (!s_midiDevice->selectOutput(0))
					{
						TFE_System::logWrite(LOG_ERROR, "Midi", "Cannot load soundfont '%s'.", "SoundFonts/SYNTHGM.sf2");
						res = false;
					}
				}
			}
		}
		SDL_UnlockMutex(s_deviceChangeMutex);

		s_thread = SDL_CreateThread(midiUpdateFunc, "TFE_MidiThread", nullptr);
		if (!s_thread)
		{
			TFE_System::logWrite(LOG_ERROR, "Midi", "cannot create Midi Thread!");
			res = false;
		}

		CCMD("setMusicVolume", setMusicVolumeConsole, 1, "Sets the music volume, range is 0.0 to 1.0");
		CCMD("getMusicVolume", getMusicVolumeConsole, 0, "Get the current music volume where 0 = silent, 1 = maximum.");

		TFE_Settings_Sound* soundSettings = TFE_Settings::getSoundSettings();
		setVolume(soundSettings->musicVolume);
		setMaximumNoteLength();

		return res && s_thread;
	}

	void destroy()
	{
		s32 i;

		TFE_System::logWrite(LOG_MSG, "MidiPlayer", "Shutdown");
		// Destroy the thread before shutting down the Midi Device.

		SDL_LockMutex(s_midiThreadMutex);
		s_threadAlive = false;
		SDL_CondSignal(s_midiCmdCond);
		SDL_UnlockMutex(s_midiThreadMutex);
		SDL_WaitThread(s_thread, &i);

		delete s_midiDevice;

		SDL_DestroyCond(s_midiCmdCond);
		SDL_DestroyMutex(s_midiThreadMutex);
		SDL_DestroyMutex(s_deviceChangeMutex);
	}

	MidiDevice* getMidiDevice()
	{
		return s_midiDevice;
	}

	const char* getMidiDeviceTypeName(MidiDeviceType type)
	{
		return c_midiDeviceTypes[type];
	}

	void setDeviceType(MidiDeviceType type)
	{
		SDL_LockMutex(s_deviceChangeMutex);
		{
			allocateMidiDevice(type);
			if (s_midiDevice)
			{
				if (!s_midiDevice->selectOutput(-1))	// -1 will select the default.
				{
					TFE_System::logWrite(LOG_ERROR, "Midi", "Cannot select midi output.");
				}
			}
		}
		SDL_UnlockMutex(s_deviceChangeMutex);
	}

	void selectDeviceOutput(s32 output)
	{
		SDL_LockMutex(s_deviceChangeMutex);
		{
			if (s_midiDevice)
			{
				if (!s_midiDevice->selectOutput(output))
				{
					TFE_System::logWrite(LOG_ERROR, "Midi", "Cannot select midi output.");
				}
			}
		}
		SDL_UnlockMutex(s_deviceChangeMutex);
	}

	MidiDeviceType getDeviceType()
	{
		if (s_midiDevice)
		{
			return s_midiDevice->getType();
		}
		return MIDI_TYPE_DEFAULT;
	}

	//////////////////////////////////////////////////
	// Command Buffer
	//////////////////////////////////////////////////
	static void tryQueueMidiCmd(MidiCmd cmd)
	{
		SDL_LockMutex(s_midiThreadMutex);
		if (s_midiCmdCount >= MAX_MIDI_CMD)
		{
			SDL_UnlockMutex(s_midiThreadMutex);
			return;
		}
		s_midiCmdBuffer[s_midiCmdCount++] = cmd;
		SDL_CondSignal(s_midiCmdCond);
		SDL_UnlockMutex(s_midiThreadMutex);
	}

	void midiClearCmdBuffer()
	{
		s_midiCmdCount = 0;
	}

	//////////////////////////////////////////////////
	// Command Interface
	//////////////////////////////////////////////////
	void setVolume(f32 volume)
	{
		tryQueueMidiCmd(MidiCmd::changeVol(volume));
	}
	
	// Set the length in seconds that a note is allowed to play for in seconds.
	void setMaximumNoteLength(f32 dt)
	{
		s_maxNoteLength = f64(dt);
	}

	void pauseThread()
	{
		if (!s_tPaused && s_midiThreadMutex)
		{
			SDL_LockMutex(s_midiThreadMutex);
			s_tPaused = true;
		}
	}

	void resumeThread()
	{
		if (s_tPaused && s_midiThreadMutex)
		{
			SDL_UnlockMutex(s_midiThreadMutex);
			s_tPaused = false;
		}
	}

	void pause()
	{
		tryQueueMidiCmd(MidiCmd::pause());
	}

	void resume()
	{
		tryQueueMidiCmd(MidiCmd::resume());
	}

	void stopMidiSound()
	{
		tryQueueMidiCmd(MidiCmd::stop());
	}

	void synthesizeMidi(f32* buffer, u32 stereoSampleCount, bool updateBuffer)
	{
		// In some cases, such as when using the System Midi Device, the midi audio is generated externally so
		// rendering is not required.
		SDL_LockMutex(s_deviceChangeMutex);  // Make sure we don't synthesize when the device is being changed.
		if (s_midiDevice && s_midiDevice->canRender())
		{
			// Stereo samples -> actual samples.
			const s32 linearSampleCount = (s32)stereoSampleCount * 2;
			// Make sure the sample buffer is large enough, this should only happen once.
			if (linearSampleCount > (s32)s_sampleBuffer.size() || !s_sampleBufferPtr)
			{
				s_sampleBuffer.resize(linearSampleCount);
				s_sampleBufferPtr = s_sampleBuffer.data();
			}

			// The midi device takes the number of stereo samples.
			s_midiDevice->render(s_sampleBufferPtr, stereoSampleCount);
			// Accumulate midi samples with existing audio samples (from soundFX).
			if (updateBuffer)
			{
				for (s32 i = 0; i < linearSampleCount; i++)
				{
					buffer[i] += s_sampleBufferPtr[i];
				}
			}
		}
		SDL_UnlockMutex(s_deviceChangeMutex);
	}

	f32 getVolume()
	{
		return s_masterVolume;
	}

	void midiSetCallback(void(*callback)(void), f64 timeStep)
	{
		SDL_LockMutex(s_midiThreadMutex);
		s_midiCallback.callback = callback;
		s_midiCallback.timeStep = timeStep;
		s_midiCallback.accumulator = 0.0;

		for (u32 i = 0; i < MIDI_CHANNEL_COUNT; i++)
		{
			s_channelSrcVolume[i] = CHANNEL_MAX_VOLUME;
		}
		// Changing the volume signals the command condition and
		// wakes up the thread.
		changeVolume();
		SDL_UnlockMutex(s_midiThreadMutex);
	}

	void midiClearCallback()
	{
		SDL_LockMutex(s_midiThreadMutex);
		s_midiCallback.callback = nullptr;
		s_midiCallback.timeStep = 0.0;
		s_midiCallback.accumulator = 0.0;
		// If the thread is waiting on the condition, nothing changes.
		// Otherwise the thread will discover that we cleared the
		// callback and will go to sleep.
		SDL_UnlockMutex(s_midiThreadMutex);
	}

	//////////////////////////////////////////////////
	// Internal
	//////////////////////////////////////////////////
	void changeVolume()
	{
		if (s_midiDevice && s_midiDevice->hasGlobalVolumeCtrl())
		{
			s_midiDevice->setVolume(s_masterVolumeScaled);
		}
		else if (s_midiDevice)
		{
			for (u32 i = 0; i < MIDI_CHANNEL_COUNT; i++)
			{
				s_midiDevice->message(MID_CONTROL_CHANGE + i, MID_VOLUME_MSB, u8(s_channelSrcVolume[i] * s_masterVolumeScaled));
			}
		}
	}

	void stopAllNotes()
	{
		// Some devices don't support "all notes off" - so do it manually.
		for (s32 i = 0; i < MIDI_INSTRUMENT_COUNT; i++)
		{
			// Skip any instruments not being used.
			if (!s_instrOn[i].channelMask) { continue; }

			// Look for used channels.
			for (u32 c = 0; c < MIDI_CHANNEL_COUNT; c++)
			{
				const u32 channelMask = 1u << c;
				if (s_instrOn[i].channelMask & channelMask)
				{
					// Turn off the note.
					if (s_midiDevice) { s_midiDevice->message(MID_NOTE_OFF | c, i); }

					// Reset the instrument channel information.
					s_instrOn[i].channelMask &= ~channelMask;
					s_instrOn[i].time[c] = 0.0;
				}
			}
		}

		if (s_midiDevice) { s_midiDevice->noteAllOff(); }
		memset(s_instrOn, 0, sizeof(Instrument) * MIDI_INSTRUMENT_COUNT);
		s_curNoteTime = 0.0;
	}
		
	void sendMessageDirect(u8 type, u8 arg1, u8 arg2)
	{
		u8 msg[] = { type, arg1, arg2 };
		u8 msgType = (type & 0xf0);
		u8 len;

		len = (msgType == MID_PROGRAM_CHANGE) ? 2 : 3;

		if (msgType == MID_CONTROL_CHANGE && arg1 == MID_VOLUME_MSB && s_midiDevice && !s_midiDevice->hasGlobalVolumeCtrl())
		{
			const s32 channelIndex = type & 0x0f;
			s_channelSrcVolume[channelIndex] = arg2;
			msg[2] = u8(s_channelSrcVolume[channelIndex] * s_masterVolumeScaled);
		}
		if (s_midiDevice) { s_midiDevice->message(msg, len); }

		// Record currently playing instruments and the note-on times.
		if (msgType == MID_NOTE_OFF || msgType == MID_NOTE_ON)
		{
			const u8 instr   = arg1;
			const u8 channel = type & 0x0f;
			if (msgType == MID_NOTE_OFF || (msgType == MID_NOTE_ON && arg2 == 0))	// note on + velocity = 0 is the same as note off.
			{
				s_instrOn[instr].channelMask  &= ~(1 << channel);
				s_instrOn[instr].time[channel] = 0.0;
			}
			else  // MID_NOTE_ON
			{
				s_instrOn[instr].channelMask  |= (1 << channel);
				s_instrOn[instr].time[channel] = s_curNoteTime;
			}
		}
	}
	
	void detectHangingNotes()
	{
		for (s32 i = 0; i < MIDI_INSTRUMENT_COUNT; i++)
		{
			// Skip any instruments not being used.
			if (!s_instrOn[i].channelMask) { continue; }

			// Look for used channels.
			for (u32 c = 0; c < MIDI_CHANNEL_COUNT; c++)
			{
				const u32 channelMask = 1u << c;
				if ((s_instrOn[i].channelMask & channelMask) && (s_curNoteTime - s_instrOn[i].time[c] > s_maxNoteLength))
				{
					// Turn off the note.
					sendMessageDirect(MID_NOTE_OFF | c, i);

					// Reset the instrument channel information.
					s_instrOn[i].channelMask &= ~channelMask;
					s_instrOn[i].time[c] = 0.0;
				}
			}
		}
	}

	// Thread Function
	int midiUpdateFunc(void* userData)
	{
		bool isPaused = false;
		u64 localTime = 0;
		f64 dt = 0.0;
		while (true)
		{
			SDL_LockMutex(s_midiThreadMutex);

			if (!s_threadAlive)
			{
				SDL_UnlockMutex(s_midiThreadMutex);
				break;
			}

			// Read from the command buffer.
			MidiCmd* midiCmd = s_midiCmdBuffer;
			for (u32 i = 0; i < s_midiCmdCount; i++, midiCmd++)
			{
				switch (midiCmd->cmd)
				{
					case MIDI_PAUSE:
					{
						localTime = 0;
						isPaused = true;
						stopAllNotes();
					} break;
					case MIDI_RESUME:
					{
						isPaused = false;
					} break;
					case MIDI_CHANGE_VOL:
					{
						s_masterVolume = midiCmd->newVolume;
						s_masterVolumeScaled = s_masterVolume * c_musicVolumeScale;
						changeVolume();
					} break;
					case MIDI_STOP_NOTES:
					{
						stopAllNotes();
						// Reset local time.
						localTime = 0;
						s_midiCallback.accumulator = 0.0;
					} break;
				}
			}
			s_midiCmdCount = 0;

			// Process the midi callback, if it exists.
			if (s_midiCallback.callback && !isPaused)
			{
				s_midiCallback.accumulator += TFE_System::updateThreadLocal(&localTime);
				while (s_midiCallback.accumulator >= s_midiCallback.timeStep)
				{
					s_midiCallback.callback();
					s_midiCallback.accumulator -= s_midiCallback.timeStep;
					s_curNoteTime += s_midiCallback.timeStep;
				}

				// Check for hanging notes.
				detectHangingNotes();

				// Sleep until the next playback window to
				// avoid a busy waiting scenario.
				auto timeout = (s_midiCallback.timeStep - s_midiCallback.accumulator) * 1000;
				timeout = timeout > SDL_MUTEX_MAXWAIT ? SDL_MUTEX_MAXWAIT : timeout;
				SDL_CondWaitTimeout(s_midiCmdCond, s_midiThreadMutex, timeout);
			}
			else
			{
				SDL_CondWait(s_midiCmdCond, s_midiThreadMutex);
			}

			SDL_UnlockMutex(s_midiThreadMutex);
		};
		
		return 0;
	}

	// Console Functions
	void setMusicVolumeConsole(const ConsoleArgList& args)
	{
		if (args.size() < 2) { return; }
		f32 volume = TFE_Console::getFloatArg(args[1]);
		setVolume(volume);

		TFE_Settings_Sound* soundSettings = TFE_Settings::getSoundSettings();
		soundSettings->musicVolume = volume;
		TFE_Settings::writeToDisk();
	}

	void getMusicVolumeConsole(const ConsoleArgList& args)
	{
		char res[256];
		sprintf(res, "Sound Volume: %2.3f", s_masterVolume);
		TFE_Console::addToHistory(res);
	}

	void allocateMidiDevice(MidiDeviceType type)
	{
		if (s_midiDevice && s_midiDevice->getType() == type) { return; }
		delete s_midiDevice;
		s_midiDevice = nullptr;

		switch (type)
		{
#ifdef BUILD_SYSMIDI
			case MIDI_TYPE_SYSTEM:
				s_midiDevice = new SystemMidiDevice();
				break;
#endif
			case MIDI_TYPE_SF2:
				s_midiDevice = new SoundFontDevice();
				break;
			case MIDI_TYPE_OPL3:
				s_midiDevice = new Fm4Opl3Device();
				break;
			default:
				TFE_System::logWrite(LOG_ERROR, "Midi", "Invalid midi type selected: %d", (s32)type);
				s_midiDevice = new Fm4Opl3Device();
				break;
		}
	}
}
