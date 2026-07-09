#pragma once
#include <TFE_System/types.h>
#include <SDL_mutex.h>
#include "midiDevice.h"

namespace libremidi
{
	class midi_out;
	class observer;
}

namespace TFE_Audio
{
	class SystemMidiDevice : public MidiDevice
	{
	public:
		SystemMidiDevice();
		~SystemMidiDevice() override;

		MidiDeviceType getType() override { return MIDI_TYPE_SYSTEM; }

		void exit() override;
		// The system device does not have proper global volume control.
		bool hasGlobalVolumeCtrl() override { return false; }
		const char* getName() override;

		// The System Midi device outputs commands to midi hardware or external programs and so never renders sound to the audio thread.
		bool render(f32* buffer, u32 sampleCount) override { return false; }
		bool canRender() override { return false; }

		void message(u8 type, u8 arg1, u8 arg2) override;
		void message(const u8* msg, u32 len) override;
		void noteAllOff() override;
		void setVolume(f32 volume) override;

		u32  getOutputCount() override;
		void getOutputName(s32 index, char* buffer, u32 maxLength) override;
		bool selectOutput(s32 index) override;
		s32  getActiveOutput(void) override;

	private:
		libremidi::midi_out* m_midiout;
		libremidi::observer* m_observer;

		// serialize access to the physical MIDI port, to at least
		// prevent a buffer overrun in the Linux ALSA MIDI parser.
		SDL_mutex* portLock = nullptr;

		s32  m_outputId;
		FileList m_outputs;
	};
}
