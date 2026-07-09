#include <algorithm>
#include <cstring>
#include <SDL_mutex.h>
#include "midi.h"
#include "systemMidiDevice.h"

#include <libremidi/libremidi.hpp>
#include <libremidi/configurations.hpp>

#include <TFE_System/system.h>

#ifdef _WIN32
// Windows library required to access the midi device(s).
#pragma comment( lib, "winmm.lib" )
#endif

namespace TFE_Audio
{
	static const char* c_SystemMidi_Name = "System Midi";
	enum
	{
		SMID_DRUM_CHANNEL = 9,
	};

	SystemMidiDevice::SystemMidiDevice()
	{
		m_outputId = 0;		// "Disabled" device.

#ifdef __linux__
		// Linux: request ALSA Sequencer API to find software synths as well
		m_midiout = new libremidi::midi_out {
			libremidi::output_configuration {},
			libremidi::alsa_seq::output_configuration {
				.client_name = "TheForceEngine"
			}};
		m_observer = new libremidi::observer {
			libremidi::observer_configuration {.track_any = true },
			libremidi::alsa_seq::observer_configuration {
				.client_name = "TheForceEngine",
				.poll_period = std::chrono::milliseconds{1000}
			}};
#else
		m_midiout = new libremidi::midi_out();
		m_observer = new libremidi::observer();
#endif

		portLock = SDL_CreateMutex();

		getOutputCount();
	}

	SystemMidiDevice::~SystemMidiDevice()
	{
		exit();
		SDL_DestroyMutex(portLock);
		portLock = nullptr;
	}

	void SystemMidiDevice::exit()
	{
		if (m_outputId > 0) { m_midiout->close_port(); }

		if (m_observer)
		{
			delete m_observer;
			m_observer = nullptr;
		}

		if (m_midiout)
		{
			delete m_midiout;
			m_midiout = nullptr;
		}
		m_outputId = 0;
	}

	const char* SystemMidiDevice::getName()
	{
		return c_SystemMidi_Name;
	}

	void SystemMidiDevice::message(const u8* msg, u32 len)
	{
		if (m_outputId > 0)
		{
			SDL_LockMutex(portLock);
			m_midiout->send_message(msg, len);
			SDL_UnlockMutex(portLock);
		}
	}

	void SystemMidiDevice::message(u8 type, u8 arg1, u8 arg2)
	{
		const u8 msg[3] = { type, arg1, arg2 };
		message(msg, 3);
	}

	void SystemMidiDevice::noteAllOff()
	{
		for (u32 c = 0; c < MIDI_CHANNEL_COUNT; c++)
		{
			message(MID_CONTROL_CHANGE + c, MID_ALL_NOTES_OFF, 0);
		}
	}

	void SystemMidiDevice::setVolume(f32 volume)
	{
		// No-op.
	}

	u32 SystemMidiDevice::getOutputCount()
	{
		m_outputs.clear();
		if (m_observer)
		{
			auto ports = m_observer->get_output_ports();
			u32 count = (u32)ports.size();

			m_outputs.push_back(count ? "Disabled" : "Disabled: no ports found");

			for (const auto& port : ports)
			{
				m_outputs.push_back(port.port_name);
			}
		} else {
			m_outputs.push_back("Disabled: internal MIDI error");
		}
		return (u32)m_outputs.size();
	}

	void SystemMidiDevice::getOutputName(s32 index, char* buffer, u32 maxLength)
	{
		if (index < 0 || index >= (s32)getOutputCount()) { return; }

		const std::string& name = m_outputs[index];
		const u32 copyLength = std::min((u32)name.length(), maxLength - 1);
		strncpy(buffer, name.c_str(), copyLength);
		buffer[copyLength] = 0;
	}

	bool SystemMidiDevice::selectOutput(s32 index)
	{
		if (index < 0 || index >= (s32)getOutputCount())
		{
			index = 0;	// "disabled" device
		}
		if (index != m_outputId && m_midiout && m_observer)
		{
			noteAllOff();
			if (m_outputId > 0)
				m_midiout->close_port();

			if (index > 0)	// real Device
			{
				auto ports = m_observer->get_output_ports();
				if ((size_t)(index - 1) < ports.size())
				{
					// Pass the exact port_information object to open_port
					m_midiout->open_port(ports[index - 1]);

					for (s32 i = 0; i < MIDI_CHANNEL_COUNT; i++)
					{
						u8 msg[2] = { u8(MID_PROGRAM_CHANGE | i), 0 };
						message(msg, 2);
					}
				}
			}
		}
		m_outputId = index;
		return true;
	}

	s32 SystemMidiDevice::getActiveOutput(void)
	{
		return m_outputId;
	}
}
