#include "imMidiCmd.h"
#include "imMidiPlayer.h"
#include "imuse.h"
#include "imTrigger.h"
#include "imList.h"
#include "midiData.h"
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Audio/midi.h>
#include <TFE_Audio/midiPlayer.h>
#include <TFE_System/system.h>
#include <assert.h>

namespace TFE_Jedi
{
	extern s32 ImJumpMidiInternal(ImPlayerData* data, s32 chunk, s32 measure, s32 beat, s32 tick, s32 sustain);

	#define MIDI_FUNC(f) (MidiEventFunc)f
	void ImCheckForTrackEnd(ImPlayerData* playerData, u8* data);
	void ImMidiSystemFunc(ImPlayerData* playerData, u8* chunkData);
	void ImMidiPressure(ImMidiPlayer* player, u8 channel, u8 arg1, u8 arg2);

	// Midi functions for each message type - see ImMidiMessageIndex{} above.
	MidiCmdFuncUnion s_jumpMidiCmdFunc[IM_MID_COUNT] =
	{
		MIDI_FUNC(nullptr),        // IM_MID_NOTE_OFF
		MIDI_FUNC(nullptr),        // IM_MID_NOTE_ON
		MIDI_FUNC(nullptr),        // IM_MID_KEY_PRESSURE
		MIDI_FUNC(nullptr),        // IM_MID_CONTROL_CHANGE
		MIDI_FUNC(nullptr),        // IM_MID_PROGRAM_CHANGE
		MIDI_FUNC(nullptr),        // IM_MID_CHANNEL_PRESSURE
		MIDI_FUNC(nullptr),        // IM_MID_PITCH_BEND
		MIDI_FUNC(nullptr),        // IM_MID_SYS_FUNC
		MIDI_FUNC(ImMidiEvent),    // IM_MID_EVENT
	};
	MidiCmdFuncUnion s_jumpMidiSustainOpenCmdFunc[IM_MID_COUNT] =
	{
		MIDI_FUNC(ImMidiJumpSustainOpen_NoteOff),// IM_MID_NOTE_OFF
		MIDI_FUNC(ImMidiJumpSustainOpen_NoteOn), // IM_MID_NOTE_ON
		MIDI_FUNC(nullptr),                      // IM_MID_KEY_PRESSURE
		MIDI_FUNC(nullptr),                      // IM_MID_CONTROL_CHANGE
		MIDI_FUNC(nullptr),                      // IM_MID_PROGRAM_CHANGE
		MIDI_FUNC(nullptr),                      // IM_MID_CHANNEL_PRESSURE
		MIDI_FUNC(nullptr),                      // IM_MID_PITCH_BEND
		MIDI_FUNC(nullptr),                      // IM_MID_SYS_FUNC
		MIDI_FUNC(ImCheckForTrackEnd)            // IM_MID_EVENT
	};
	MidiCmdFuncUnion s_jumpMidiSustainCmdFunc[IM_MID_COUNT] =
	{
		MIDI_FUNC(nullptr),                  // IM_MID_NOTE_OFF
		MIDI_FUNC(ImMidiJumpSustain_NoteOn), // IM_MID_NOTE_ON
		MIDI_FUNC(nullptr),                  // IM_MID_KEY_PRESSURE
		MIDI_FUNC(nullptr),                  // IM_MID_CONTROL_CHANGE
		MIDI_FUNC(nullptr),                  // IM_MID_PROGRAM_CHANGE
		MIDI_FUNC(nullptr),                  // IM_MID_CHANNEL_PRESSURE
		MIDI_FUNC(nullptr),                  // IM_MID_PITCH_BEND
		MIDI_FUNC(nullptr),                  // IM_MID_SYS_FUNC
		MIDI_FUNC(ImCheckForTrackEnd)        // IM_MID_EVENT
	};
	MidiCmdFuncUnion s_midiCmdFunc[IM_MID_COUNT] =
	{
		MIDI_FUNC(ImMidiNoteOff),         // IM_MID_NOTE_OFF
		MIDI_FUNC(ImMidiNoteOn),          // IM_MID_NOTE_ON
		MIDI_FUNC(ImMidiPressure),        // IM_MID_KEY_PRESSURE
		MIDI_FUNC(ImMidiCommand),         // IM_MID_CONTROL_CHANGE
		MIDI_FUNC(ImMidiProgramChange),   // IM_MID_PROGRAM_CHANGE
		MIDI_FUNC(ImMidiPressure),        // IM_MID_CHANNEL_PRESSURE
		MIDI_FUNC(ImMidiPitchBend),       // IM_MID_PITCH_BEND
		MIDI_FUNC(ImMidiSystemFunc),      // IM_MID_SYS_FUNC
		MIDI_FUNC(ImMidiEvent),           // IM_MID_EVENT
	};

	void ImCheckForTrackEnd(ImPlayerData* playerData, u8* data)
	{
		if (data[0] == META_END_OF_TRACK)
		{
			s_midiTrackEnd = 1;
		}
	}

	s32 ImUpdateHook(s32* hook, u8 newValue)
	{
		if (newValue)
		{
			if ((s32)newValue != (*hook))
			{
				return imFail;
			}
			IM_DBG_MSG("Set Hook = 0, Hook = %d, New Value = %d", *hook, newValue);
			*hook = 0;
			return imSuccess;
		}

		if ((*hook) == 0x80)
		{
			IM_DBG_MSG("Hook = %d", newValue);
			*hook = (s32)newValue;
			return imFail;
		}

		return imSuccess;
	}

	void ImMidiSystemFunc(ImPlayerData* playerData, u8* chunkData)
	{
		if (*chunkData != 0xf0)
		{
			return;
		}

		u8* data = chunkData + 1;
		if (data[0] > 0x7f || data[1] != 0x7d)
		{
			return;
		}
		data += 2;

		u8 value = *data;
		data++;
		if (value == 0)
		{
			// Set Marker
			ImMidiPlayer* player = playerData->player;
			u8 marker = *data;
			player->marker = marker;

			IM_DBG_MSG("Set Marker: %d", marker);
			ImSetSoundTrigger(player->soundId, (void*)data);
		}
		else if (value == 1)
		{
			value = *data;
			data++;

			ImMidiPlayer* player = playerData->player;
			if (ImUpdateHook(&player->hook, value) == imSuccess) 
			{
				IM_DBG_MSG("JMP(I) h:%d", value);
				ImJumpMidiInternal(playerData, data[0], (data[1] << 7) | data[2], data[3], (data[4] << 7) | data[5], data[6]);
			}
			else
			{
				IM_DBG_MSG("JMP(I) Skipped - h:%d", value);
			}
		}
		else if (value == 3)
		{
			ImMidiPlayer* player = playerData->player;
			IM_DBG_MSG("Marker: 0x80");
			player->marker = 0x80;
			ImSetSoundTrigger(player->soundId, (void*)data);
		}
		else
		{
			IM_LOG_ERR("%s", "mp got bad sysex msg...");
		}
	}

	void ImMidiPressure(ImMidiPlayer* player, u8 channel, u8 arg1, u8 arg2)
	{
		// Not used by Dark Forces.
		IM_LOG_ERR("%s", "ImMidiPressure() unimplemented.");
		assert(0);
	}

	//////////////////////////////////////////////
	// Low-level Midi Commands
	//////////////////////////////////////////////
	void ImNoteOff(s32 channelId, s32 instrId)
	{
		TFE_MidiPlayer::sendMessageDirect(MID_NOTE_OFF | channelId, instrId);
	}

	void ImNoteOn(s32 channelId, s32 instrId, s32 velocity)
	{
		TFE_MidiPlayer::sendMessageDirect(MID_NOTE_ON | channelId, instrId, velocity);
	}

	void ImControlChange(s32 channelId, MidiController ctrl, s32 value)
	{
		TFE_MidiPlayer::sendMessageDirect(MID_CONTROL_CHANGE | channelId, ctrl, value);
	}

	void ImProgramChange(u8 channel, u8 msg)
	{
		TFE_MidiPlayer::sendMessageDirect(MID_PROGRAM_CHANGE | channel, msg);
	}

	// For Pan, "Fine" resolution is 14-bit where 8192 (0x2000) is center - MID_PAN_LSB
	// Most devices use coarse adjustment instead (7 bits, 64 is center) - MID_PAN_MSB
	void ImSetPanFine(s32 channel, s32 pan)
	{
		// General Midi uses MID_PAN_MSB, so this is a no-op.
	}

	void ImSendPitchBend(s32 channelId, s32 bend14)
	{
		bend14 = clamp(bend14, 0, 16383);
		TFE_MidiPlayer::sendMessageDirect(u8(MID_PITCH_BEND | channelId), u8(bend14 & 0x7f), u8((bend14 >> 7) & 0x7f));
	}

	void ImSetPitchBendRange(s32 channelId, s32 semitones)
	{
		// Standard RPN 0 (Pitch Bend Sensitivity): select RPN 0/0, then write the range
		// in semitones via Data Entry MSB. This exact 3-message sequence (RPN LSB, RPN
		// MSB, Data Entry MSB - no Data Entry LSB/cents, no RPN Null reset afterward)
		// was verified against a raw MIDI capture of the original engine's actual
		// output, byte for byte, so it is intentionally NOT "more thorough" than that.
		ImControlChange(channelId, MID_RPN_LSB, 0);
		ImControlChange(channelId, MID_RPN_MSB, 0);
		ImControlChange(channelId, MID_DATA_ENTRY_MSB, semitones);
	}

}  // namespace TFE_Jedi
