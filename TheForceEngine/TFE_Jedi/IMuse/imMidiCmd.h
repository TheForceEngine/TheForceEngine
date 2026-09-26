#pragma once
//////////////////////////////////////////////////////////////////////
// iMuse System
// The IMuse system used by Dark Forces.
//
// Copyright note:
// While the project as a whole is licensed under GPL-2.0, some of the
// code under TFE_Jedi/ was derived from reverse-engineered
// code from "Dark Forces" (DOS) which is copyrighted by LucasArts.
// The original renderer is contained in RClassic_Fixed/ and the
// RClassic_Float/ and RClassic_GPU/ sub-renderers are derived from
// that code.
//
// I consider the reverse-engineering to be "Fair Use" - a means of 
// supporting the games on other platforms and to improve support on
// existing platforms without claiming ownership of the games
// themselves or their IPs.
//
// That said using this code in a commercial project is risky without
// permission of the original copyright holders (LucasArts).
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_Audio/midi.h>
#include "imConst.h"

namespace TFE_Jedi
{
	struct ImPlayerData;
	struct ImMidiPlayer;

	enum ImMidiMessageIndex
	{
		IM_MID_NOTE_OFF         = 0,
		IM_MID_NOTE_ON          = 1,
		IM_MID_KEY_PRESSURE     = 2,
		IM_MID_CONTROL_CHANGE   = 3,
		IM_MID_PROGRAM_CHANGE   = 4,
		IM_MID_CHANNEL_PRESSURE = 5,
		IM_MID_PITCH_BEND       = 6,
		IM_MID_SYS_FUNC         = 7,
		IM_MID_EVENT            = 8,
		IM_MID_COUNT            = 9,
	};

	typedef void(*MidiEventFunc)(ImPlayerData* playerData, u8* chunkData);
	typedef void(*MidiCmdFunc)(ImMidiPlayer* player, u8 channel, u8 arg1, u8 arg2);
	union MidiCmdFuncUnion
	{
		MidiEventFunc evtFunc;
		MidiCmdFunc   cmdFunc;
	};

	extern MidiCmdFuncUnion s_jumpMidiCmdFunc[IM_MID_COUNT];
	extern MidiCmdFuncUnion s_jumpMidiSustainOpenCmdFunc[IM_MID_COUNT];
	extern MidiCmdFuncUnion s_jumpMidiSustainCmdFunc[IM_MID_COUNT];
	extern MidiCmdFuncUnion s_midiCmdFunc[IM_MID_COUNT];

	// Low level midi commands.
	void ImNoteOff(s32 channelId, s32 instrId);
	void ImNoteOn(s32 channelId, s32 instrId, s32 velocity);
	void ImControlChange(s32 channelId, MidiController ctrl, s32 value);
	void ImProgramChange(u8 channel, u8 msg);
	void ImSetPanFine(s32 channel, s32 pan);
	// Sends a raw 14-bit MIDI Pitch Bend message (0 - 16383, center = 8192).
	void ImSendPitchBend(s32 channelId, s32 bend14);
	// Configures the receiving device's Pitch Bend Sensitivity via the standard RPN 0
	// (Pitch Bend Range), then sends RPN Null to guard against later Data Entry messages
	// accidentally altering it. See ImHandleChannelDetuneChange() in imuse.cpp for why
	// iMuse's own pitch bend math requires this to be set to a specific value.
	void ImSetPitchBendRange(s32 channelId, s32 semitones);

	// TODO: These are currently defined in imuse.cpp, move them over.
	extern void ImMidiJumpSustainOpen_NoteOff(ImMidiPlayer* player, u8 channelId, u8 arg1, u8 arg2);
	extern void ImMidiJumpSustainOpen_NoteOn(ImMidiPlayer* player, u8 channelId, u8 arg1, u8 arg2);
	extern void ImMidiJumpSustain_NoteOn(ImMidiPlayer* player, u8 channelId, u8 arg1, u8 arg2);
	extern void ImMidiCommand(ImMidiPlayer* player, s32 channelIndex, s32 midiCmd, s32 value);
	extern void ImMidiNoteOff(ImMidiPlayer* player, u8 channel, u8 arg1, u8 arg2);
	extern void ImMidiNoteOn(ImMidiPlayer* player, u8 channel, u8 arg1, u8 arg2);
	extern void ImMidiProgramChange(ImMidiPlayer* player, u8 channel, u8 arg1, u8 arg2);
	extern void ImMidiPitchBend(ImMidiPlayer* player, u8 channel, u8 arg1, u8 arg2);
	extern void ImMidiEvent(ImPlayerData* playerData, u8* chunkData);
}  // namespace TFE_Jedi