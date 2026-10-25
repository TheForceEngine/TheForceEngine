#pragma once
#include <TFE_System/types.h>
#include <SDL_mutex.h>
#include "midiDevice.h"

class RtMidiOut;

namespace TFE_Audio
{
	class SystemMidiDevice : public MidiDevice
	{
	public:
		SystemMidiDevice();
		~SystemMidiDevice() override;

		MidiDeviceType getType() override { return MIDI_TYPE_SYSTEM; }

		void exit() override;
		// true: volume is sent as a standard GM/GS "Master Volume" Universal Real Time
		// SysEx message (see setVolume() below) rather than by rewriting every outgoing
		// Channel Volume (CC7) byte. The old approach (hasGlobalVolumeCtrl() == false)
		// made TFE_MidiPlayer::sendMessageDirect() rewrite each outgoing CC7 value to
		// u8(originalCC7 * masterVolumeScaled) before sending it - a multiply-and-truncate
		// that disproportionately crushed deliberately quiet channels (e.g. a soft
		// background/echo layer) toward zero, especially once the music volume slider was
		// below 100%. That data corruption is gone: real/external MIDI devices now receive
		// exactly the same untouched CC7 bytes SF2 and CLAP always have.

		bool hasGlobalVolumeCtrl() override { return true; }
		const char* getName() override;

		// The System Midi device outputs commands to midi hardware or external programs and so never renders sound to the audio thread.
		bool render(f32* buffer, u32 sampleCount) override { return false; }
		bool canRender() override { return false; }

		void message(u8 type, u8 arg1, u8 arg2) override;
		void message(const u8* msg, u32 len) override;
		void noteAllOff() override;
		// Sends the GM/GS Master Volume Universal Real Time SysEx (device ID 0x7F,
		// i.e. broadcast to all devices) instead of touching any channel's Volume CC7 -
		// see hasGlobalVolumeCtrl() above.
		void setVolume(f32 volume) override;

		u32  getOutputCount() override;
		void getOutputName(s32 index, char* buffer, u32 maxLength) override;
		bool selectOutput(s32 index) override;
		s32  getActiveOutput(void) override;

	private:
		RtMidiOut* m_midiout;

		// serialize access to the physical MIDI port, to at least
		// prevent a buffer overrun in the Linux ALSA MIDI parser.
		SDL_mutex* portLock = nullptr;
		
		s32  m_outputId;
		FileList m_outputs;

		// Last Master Volume value (0-127) actually sent via setVolume()'s SysEx, or
		// -1 if none has been sent yet. Used to avoid re-sending an unchanged value -
		// see setVolume() in the .cpp for why that matters.
		s32  m_lastSentVolume14 = -1;
	};
}
