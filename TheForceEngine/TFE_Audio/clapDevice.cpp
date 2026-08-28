#include <algorithm>
#include <cstring>
#include <SDL_mutex.h>
#include "clapDevice.h"
#include "clapModule.h"
#include "clapPluginScanner.h"
#include "midi.h"
#include <TFE_System/system.h>

namespace TFE_Audio
{
	static const char* c_clapDeviceName = "CLAP";
	static const f64 c_sampleRate = 44100.0; // Matches the engine's fixed audio output rate.

	ClapDevice::ClapDevice()
	{
		m_eventLock = SDL_CreateMutex();

		m_host.clap_version = CLAP_VERSION_INIT;
		m_host.host_data = this;
		m_host.name = "The Force Engine";
		m_host.vendor = "TFE";
		m_host.url = "https://github.com/luciusDXL/TheForceEngine";
		m_host.version = "1.0";
		m_host.get_extension = hostGetExtension;
		m_host.request_restart = hostRequestRestart;
		m_host.request_process = hostRequestProcess;
		m_host.request_callback = hostRequestCallback;
	}

	ClapDevice::~ClapDevice()
	{
		exit();
		SDL_DestroyMutex(m_eventLock);
		m_eventLock = nullptr;
	}

	void ClapDevice::exit()
	{
		unloadPlugin();
		m_activeOutput = 0;
		m_activeName = "Disabled";
	}

	const char* ClapDevice::getName()
	{
		return c_clapDeviceName;
	}

	void ClapDevice::message(u8 type, u8 arg1, u8 arg2)
	{
		const u8 msg[3] = { type, arg1, arg2 };
		pushMidiEvent(msg, 3);
	}

	void ClapDevice::message(const u8* msg, u32 len)
	{
		// Forward everything - short channel/system messages as well as SysEx and any
		// other longer data TFE wants to send. pushMidiEvent() decides the CLAP event
		// type based on length; nothing is filtered here.
		pushMidiEvent(msg, len);
	}

	void ClapDevice::pushMidiEvent(const u8* msg, u32 len)
	{
		if (!m_plugin || !msg || len == 0) { return; }

		SDL_LockMutex(m_eventLock);
		if (m_eventOrderCount >= MAX_QUEUED_EVENTS)
		{
			// Queue full; drop rather than block or allocate on what may be a
			// real-time-sensitive calling thread.
			TFE_System::logWrite(LOG_WARNING, "Clap", "Dropped a queued MIDI event: event queue is full.");
			SDL_UnlockMutex(m_eventLock);
			return;
		}

		if (len <= 3)
		{
			// Standard 1-3 byte channel voice/system message -> CLAP_EVENT_MIDI.
			if (m_midiEventCount < MAX_QUEUED_EVENTS)
			{
				clap_event_midi_t& ev = m_midiEvents[m_midiEventCount];
				ev.header.size = sizeof(clap_event_midi_t);
				ev.header.time = 0;
				ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
				ev.header.type = CLAP_EVENT_MIDI;
				ev.header.flags = 0;
				ev.port_index = 0;
				ev.data[0] = msg[0];
				ev.data[1] = len > 1 ? msg[1] : 0;
				ev.data[2] = len > 2 ? msg[2] : 0;

				m_eventOrder[m_eventOrderCount].isSysex = false;
				m_eventOrder[m_eventOrderCount].index = m_midiEventCount;
				m_eventOrderCount++;
				m_midiEventCount++;
			}
			else
			{
				TFE_System::logWrite(LOG_WARNING, "Clap", "Dropped a queued MIDI event: short-message queue is full.");
			}
		}
		else
		{
			// Anything longer than 3 bytes (SysEx and any other multi-byte data TFE
			// might send) is forwarded as-is via CLAP_EVENT_MIDI_SYSEX, which carries an
			// arbitrary raw MIDI 1.0 byte buffer. Per the CLAP spec, the buffer must not
			// include the leading 0xF0 / trailing 0xF7 SysEx framing bytes, so strip
			// them here if present; any other payload is passed through unmodified.
			const u8* payload = msg;
			u32 payloadLen = len;
			if (payloadLen >= 2 && payload[0] == MID_EXCLUSIVE_START && payload[payloadLen - 1] == MID_EXCLUSIVE_END)
			{
				payload++;
				payloadLen -= 2;
			}

			if (m_sysexEventCount >= MAX_QUEUED_SYSEX_EVENTS)
			{
				TFE_System::logWrite(LOG_WARNING, "Clap", "Dropped a queued SysEx message: SysEx event queue is full.");
			}
			else if (m_sysexWritePos + payloadLen > SYSEX_BUFFER_SIZE)
			{
				TFE_System::logWrite(LOG_WARNING, "Clap", "Dropped a queued SysEx message: SysEx byte buffer is full (%u bytes).", (u32)SYSEX_BUFFER_SIZE);
			}
			else
			{
				u8* dst = &m_sysexBuffer[m_sysexWritePos];
				if (payloadLen) { memcpy(dst, payload, payloadLen); }

				clap_event_midi_sysex_t& ev = m_sysexEvents[m_sysexEventCount];
				ev.header.size = sizeof(clap_event_midi_sysex_t);
				ev.header.time = 0;
				ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
				ev.header.type = CLAP_EVENT_MIDI_SYSEX;
				ev.header.flags = 0;
				ev.port_index = 0;
				ev.buffer = dst;
				ev.size = payloadLen;

				m_eventOrder[m_eventOrderCount].isSysex = true;
				m_eventOrder[m_eventOrderCount].index = m_sysexEventCount;
				m_eventOrderCount++;
				m_sysexEventCount++;
				m_sysexWritePos += payloadLen;
			}
		}
		SDL_UnlockMutex(m_eventLock);
	}

	void ClapDevice::resetEventQueue()
	{
		// Caller must hold m_eventLock.
		m_eventOrderCount = 0;
		m_midiEventCount = 0;
		m_sysexEventCount = 0;
		m_sysexWritePos = 0;
	}

	void ClapDevice::noteAllOff()
	{
		for (u32 c = 0; c < MIDI_CHANNEL_COUNT; c++)
		{
			for (u32 n = 0; n < MIDI_INSTRUMENT_COUNT; n++)
			{
				message(u8(MID_NOTE_OFF | c), (u8)n, 0);
			}
		}
	}

	void ClapDevice::setVolume(f32 volume)
	{
		m_volume = volume;
	}

	bool ClapDevice::render(f32* buffer, u32 sampleCount)
	{
		if (!m_plugin || !m_processing) { return false; }

		if (m_scratchL.size() < sampleCount)
		{
			m_scratchL.resize(sampleCount);
			m_scratchR.resize(sampleCount);
		}
		float* channels[2] = { m_scratchL.data(), m_scratchR.data() };

		clap_audio_buffer_t outBus = {};
		outBus.data32 = channels;
		outBus.channel_count = 2;

		clap_input_events_t inEvents = {};
		inEvents.ctx = this;
		inEvents.size = inputEventsSize;
		inEvents.get = inputEventsGet;

		clap_output_events_t outEvents = {};
		outEvents.ctx = this;
		outEvents.try_push = outputEventsTryPush;

		clap_process_t process = {};
		process.frames_count = sampleCount;
		process.audio_outputs = &outBus;
		process.audio_outputs_count = 1;
		process.in_events = &inEvents;
		process.out_events = &outEvents;

		SDL_LockMutex(m_eventLock);
		m_plugin->process(m_plugin, &process);
		resetEventQueue(); // Consumed.
		SDL_UnlockMutex(m_eventLock);

		for (u32 i = 0; i < sampleCount; i++)
		{
			buffer[i * 2 + 0] = m_scratchL[i] * m_volume;
			buffer[i * 2 + 1] = m_scratchR[i] * m_volume;
		}
		return true;
	}

	u32 ClapDevice::getOutputCount()
	{
		return (u32)scanClapPlugins().size() + 1; // +1 for "Disabled".
	}

	void ClapDevice::getOutputName(s32 index, char* buffer, u32 maxLength)
	{
		if (index < 0 || index >= (s32)getOutputCount()) { return; }

		std::string name = "Disabled";
		if (index > 0)
		{
			const auto& plugins = scanClapPlugins();
			name = plugins[index - 1].name;
		}
		const u32 copyLength = std::min((u32)name.length(), maxLength - 1);
		strncpy(buffer, name.c_str(), copyLength);
		buffer[copyLength] = 0;
	}

	bool ClapDevice::selectOutput(s32 index)
	{
		if (index < 0 || index >= (s32)getOutputCount()) { index = 0; }
		if (index == m_activeOutput) { return true; }

		unloadPlugin();
		if (index > 0 && !loadPlugin(index))
		{
			m_activeOutput = 0;
			m_activeName = "Disabled";
			return false;
		}
		m_activeOutput = index;
		return true;
	}

	s32 ClapDevice::getActiveOutput(void)
	{
		return m_activeOutput;
	}

	void ClapDevice::rescanOutputs()
	{
		scanClapPlugins(true);
		// If the previously selected plugin is no longer present, fall back to "Disabled"
		// rather than risk pointing at a stale/out-of-range index.
		if (m_activeOutput >= (s32)getOutputCount())
		{
			unloadPlugin();
			m_activeOutput = 0;
			m_activeName = "Disabled";
		}
	}

	bool ClapDevice::loadPlugin(s32 index)
	{
		const auto& plugins = scanClapPlugins();
		const ClapPluginInfo& info = plugins[index - 1];

		m_module = clapModuleOpen(info.bundlePath);
		if (!m_module)
		{
			TFE_System::logWrite(LOG_ERROR, "Clap", "Failed to load CLAP bundle '%s'", info.bundlePath.c_str());
			return false;
		}

		m_entry = (const clap_plugin_entry_t*)clapModuleGetSymbol(m_module, "clap_entry");
		if (!m_entry || !clap_version_is_compatible(m_entry->clap_version) || !m_entry->init(info.bundlePath.c_str()))
		{
			TFE_System::logWrite(LOG_ERROR, "Clap", "Bundle '%s' has no valid CLAP entry point", info.bundlePath.c_str());
			clapModuleClose(m_module);
			m_module = nullptr;
			m_entry = nullptr;
			return false;
		}

		const auto* factory = (const clap_plugin_factory_t*)m_entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
		m_plugin = factory ? factory->create_plugin(factory, &m_host, info.pluginId.c_str()) : nullptr;
		if (!m_plugin || !m_plugin->init(m_plugin))
		{
			TFE_System::logWrite(LOG_ERROR, "Clap", "Failed to instantiate CLAP plugin '%s'", info.pluginId.c_str());
			unloadPlugin();
			return false;
		}

		if (!m_plugin->activate(m_plugin, c_sampleRate, 1, 4096) || !m_plugin->start_processing(m_plugin))
		{
			TFE_System::logWrite(LOG_ERROR, "Clap", "Failed to activate CLAP plugin '%s'", info.pluginId.c_str());
			unloadPlugin();
			return false;
		}

		m_processing = true;
		m_activeName = info.name;
		TFE_System::logWrite(LOG_MSG, "Clap", "Loaded CLAP plugin '%s' from '%s'", info.name.c_str(), info.bundlePath.c_str());
		return true;
	}

	void ClapDevice::unloadPlugin()
	{
		if (m_plugin)
		{
			if (m_processing) { m_plugin->stop_processing(m_plugin); }
			m_plugin->deactivate(m_plugin);
			m_plugin->destroy(m_plugin);
			m_plugin = nullptr;
		}
		m_processing = false;
		if (m_entry) { m_entry->deinit(); m_entry = nullptr; }
		if (m_module) { clapModuleClose(m_module); m_module = nullptr; }

		SDL_LockMutex(m_eventLock);
		resetEventQueue();
		SDL_UnlockMutex(m_eventLock);
	}

	const void* ClapDevice::hostGetExtension(const clap_host_t* host, const char* extensionId) { return nullptr; }
	void ClapDevice::hostRequestRestart(const clap_host_t* host) {}
	void ClapDevice::hostRequestProcess(const clap_host_t* host) {}
	void ClapDevice::hostRequestCallback(const clap_host_t* host) {}

	uint32_t ClapDevice::inputEventsSize(const clap_input_events_t* list)
	{
		const ClapDevice* self = (const ClapDevice*)list->ctx;
		return self->m_eventOrderCount;
	}

	const clap_event_header_t* ClapDevice::inputEventsGet(const clap_input_events_t* list, uint32_t index)
	{
		const ClapDevice* self = (const ClapDevice*)list->ctx;
		if (index >= self->m_eventOrderCount) { return nullptr; }

		const EventRef& ref = self->m_eventOrder[index];
		return ref.isSysex ? &self->m_sysexEvents[ref.index].header : &self->m_midiEvents[ref.index].header;
	}

	bool ClapDevice::outputEventsTryPush(const clap_output_events_t* list, const clap_event_header_t* event)
	{
		return false; // Output events (e.g. parameter changes) are not consumed by this minimal host.
	}
}
