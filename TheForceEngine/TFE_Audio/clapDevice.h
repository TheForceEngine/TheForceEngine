#pragma once
//////////////////////////////////////////////////////////////////////
// Midi Device that hosts a CLAP instrument plugin to synthesize the
// music, in place of a MIDI hardware device, an SF2 soundfont or the
// OPL3 emulator. Uses the official CLAP SDK (https://github.com/free-audio/clap).
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <SDL_mutex.h>
#include <string>
#include <vector>
#include <clap/clap.h>
#include "midiDevice.h"

namespace TFE_Audio
{
	class ClapDevice : public MidiDevice
	{
	public:
		ClapDevice();
		~ClapDevice() override;

		MidiDeviceType getType() override { return MIDI_TYPE_CLAP; }

		void exit() override;
		// Volume is applied by the host by scaling the rendered output, since not
		// all CLAP instruments expose a standard "master volume" parameter.
		bool hasGlobalVolumeCtrl() override { return true; }
		const char* getName() override;

		bool render(f32* buffer, u32 sampleCount) override;
		bool canRender() override { return true; }

		// Everything TFE can send through the MidiDevice interface is forwarded to the
		// plugin unchanged: standard 1-3 byte channel voice/system messages become
		// CLAP_EVENT_MIDI events, and anything longer (SysEx and any other multi-byte
		// data) becomes a CLAP_EVENT_MIDI_SYSEX event carrying the raw bytes. Nothing
		// is filtered, reinterpreted, or dropped based on message content.
		void message(u8 type, u8 arg1, u8 arg2) override;
		void message(const u8* msg, u32 len) override;

		void noteAllOff() override;
		void setVolume(f32 volume) override;

		// The "output" list for the CLAP device is the list of CLAP plugins found
		// at the standard install locations. Index 0 is always "Disabled", exactly
		// like SystemMidiDevice's port list.
		u32  getOutputCount() override;
		void getOutputName(s32 index, char* buffer, u32 maxLength) override;
		bool selectOutput(s32 index) override;
		s32  getActiveOutput(void) override;
		// Re-scans the standard CLAP install locations for newly added/removed plugins.
		void rescanOutputs() override;

	private:
		void unloadPlugin();
		bool loadPlugin(s32 index);
		void pushMidiEvent(const u8* msg, u32 len);
		void resetEventQueue();

		static const void* hostGetExtension(const clap_host_t* host, const char* extensionId);
		static void hostRequestRestart(const clap_host_t* host);
		static void hostRequestProcess(const clap_host_t* host);
		static void hostRequestCallback(const clap_host_t* host);

		static uint32_t inputEventsSize(const clap_input_events_t* list);
		static const clap_event_header_t* inputEventsGet(const clap_input_events_t* list, uint32_t index);
		static bool outputEventsTryPush(const clap_output_events_t* list, const clap_event_header_t* event);

		void* m_module = nullptr;
		const clap_plugin_entry_t* m_entry = nullptr;
		const clap_plugin_t* m_plugin = nullptr;
		clap_host_t m_host = {};
		bool m_processing = false;

		s32 m_activeOutput = 0;	// 0 = "Disabled".
		f32 m_volume = 1.0f;
		std::string m_activeName = "Disabled";

		// Event queue design: queued events are split into two backing arrays (short
		// 1-3 byte channel voice/system messages, and longer SysEx-style messages),
		// plus a small ordered index so process() can hand them to the plugin in the
		// order they were received. This avoids per-message heap allocation on what
		// may be a real-time-sensitive calling thread.
		enum
		{
			MAX_QUEUED_EVENTS = 1024,		// Total events (short + sysex) queued between two render() calls.
			MAX_QUEUED_SYSEX_EVENTS = 32,	// Max number of individual SysEx-style messages queued at once.
			SYSEX_BUFFER_SIZE = 16 * 1024	// Shared backing storage (bytes) for queued SysEx payloads.
		};

		struct EventRef
		{
			bool isSysex;
			u32  index;	// Index into m_midiEvents or m_sysexEvents, depending on isSysex.
		};

		EventRef m_eventOrder[MAX_QUEUED_EVENTS];
		u32 m_eventOrderCount = 0;

		clap_event_midi_t m_midiEvents[MAX_QUEUED_EVENTS];
		u32 m_midiEventCount = 0;

		clap_event_midi_sysex_t m_sysexEvents[MAX_QUEUED_SYSEX_EVENTS];
		u32 m_sysexEventCount = 0;

		u8 m_sysexBuffer[SYSEX_BUFFER_SIZE];
		u32 m_sysexWritePos = 0;

		SDL_mutex* m_eventLock = nullptr;

		std::vector<f32> m_scratchL;
		std::vector<f32> m_scratchR;
	};
}
