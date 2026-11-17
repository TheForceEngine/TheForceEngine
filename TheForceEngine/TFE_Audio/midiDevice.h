#pragma once
#include <TFE_System/types.h>
#include <TFE_FileSystem/fileutil.h>

enum MidiDeviceType
{
	MIDI_TYPE_SF2 = 0,	// Use the Sound Font 2 (SF2) midi synthesizer.
	MIDI_TYPE_OPL3,		// Use OPL3 emulation (like DosBox).
#ifdef BUILD_SYSMIDI
	MIDI_TYPE_SYSTEM,	// System midi device (hardware, midi server, GM midi on Windows).
#endif
#ifdef BUILD_CLAP
	MIDI_TYPE_CLAP,		// Use a CLAP instrument plugin found at a standard install location.
#endif
	MIDI_TYPE_COUNT,
	MIDI_TYPE_DEFAULT = MIDI_TYPE_OPL3
};

namespace TFE_Audio
{
	class MidiDevice
	{
	public:
		MidiDevice() {}
		virtual ~MidiDevice() {}

		virtual MidiDeviceType getType() = 0;

		virtual void exit() = 0;
		virtual bool hasGlobalVolumeCtrl() = 0;
		virtual const char* getName() = 0;

		virtual u32  getOutputCount() = 0;
		virtual void getOutputName(s32 index, char* buffer, u32 maxLength) = 0;
		virtual bool selectOutput(s32 index) = 0;
		virtual s32  getActiveOutput(void) = 0;
		// Optional: refresh the output list (e.g. re-scan for newly installed CLAP plugins
		// or newly connected MIDI ports). No-op by default.
		virtual void rescanOutputs() {}

		virtual bool render(f32* buffer, u32 sampleCount) = 0;
		virtual bool canRender() = 0;

		virtual void message(u8 type, u8 arg1, u8 arg2 = 0) = 0;
		virtual void message(const u8* msg, u32 len) = 0;

		virtual void noteAllOff() = 0;
		virtual void setVolume(f32 volume) = 0;
	};
};
