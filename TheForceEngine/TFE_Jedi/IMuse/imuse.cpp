#include "imuse.h"
#include <TFE_Audio/midi.h>
#include <TFE_Audio/midiPlayer.h>
#include <TFE_System/system.h>
#include <TFE_Memory/memoryRegion.h>
#include <TFE_Jedi/Math/fixedPoint.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_FileSystem/paths.h>
#include <cstring>
#include <assert.h>
#include "imList.h"
#include "imTrigger.h"
#include "imSoundFader.h"
#include "imMidiPlayer.h"
#include "imDigitalSound.h"
#include "midiData.h"

namespace TFE_Jedi
{
	#define IM_MAX_SOUNDS 32
	#define IM_MIDI_FILE_COUNT 6
	#define IM_MIDI_PLAYER_COUNT 2
	#define imuse_alloc(size) TFE_Memory::region_alloc(s_memRegion, size)
	#define imuse_realloc(ptr, size) TFE_Memory::region_realloc(s_memRegion, ptr, size)
	#define imuse_free(ptr) TFE_Memory::region_free(s_memRegion, ptr)
	
	////////////////////////////////////////////////////
	// Structures
	////////////////////////////////////////////////////
	struct ImSound
	{
		ImSound* prev;
		ImSound* next;
		ImSoundId id;
		char name[10];
		// 14-bytes unused by Dark Forces/iMuse in struct, removed in TFE.
		s32 refCount;
	};

	struct ImResource
	{
		u32 resType;
		char name[8];

		ImResource* next;
		s32 type;
		s32 id;

		s32 flags;
		s32 volume;

		s32 var1;
		s32 var2;
		u8* varptr;
		u8* varhdl;

		u8* data;
	};
						
	/////////////////////////////////////////////////////
	// Forward Declarations
	/////////////////////////////////////////////////////
	void ImUpdate();
	void ImUpdateMidi();
	void ImUpdateSustainedNotes();
		
	void ImAdvanceMidiPlayer(ImPlayerData* playerData);
	void ImJumpSustain(ImMidiPlayer* player, u8* sndData, ImPlayerData* playerPrevData, ImPlayerData* playerNextData);
	void ImMidiGetInstruments(ImMidiPlayer* player, u32* soundMidiInstrumentMask, s32* midiInstrumentCount);
	s32  ImMidiGetTickDelta(ImPlayerData* playerData, u32 prevTick, u32 tick);
	void ImMidiProcessSustain(ImPlayerData* playerData, u8* sndData, MidiCmdFuncUnion* midiCmdFunc, ImMidiPlayer* player);
		
	void ImPrintMidiState();
	s32  ImJumpMidiInternal(ImPlayerData* data, s32 chunk, s32 measure, s32 beat, s32 tick, s32 sustain);
	s32  ImPauseMidi();
	s32  ImResumeMidi();
	s32  ImHandleChannelGroupVolume();
	s32  ImGetGroupVolume(s32 group);
	void ImHandleChannelVolumeChange(ImMidiPlayer* player, ImMidiOutChannel* channel);
	void ImMidiSetupParts();
	void ImAssignMidiChannel(ImMidiPlayer* player, ImMidiOutChannel* channel, ImMidiChannel* midiChannel);
	u8*  ImGetSoundData(ImSoundId id);
	u8*  ImInternalGetSoundData(ImSoundId soundId);
	ImMidiChannel* ImGetFreeMidiChannel();
	ImMidiPlayer* ImGetFreePlayer(s32 priority);
	s32 ImStartMidiPlayerInternal(ImPlayerData* data, ImSoundId soundId);
	s32 ImSetupMidiPlayer(ImSoundId soundId, s32 priority);
	s32 ImFreeMidiPlayer(ImSoundId soundId);
	s32 ImReleaseAllPlayers();
	s32 ImGetSoundType(ImSoundId soundId);
	s32 ImSetMidiParam(ImSoundId soundId, s32 param, s32 value);
	s32 ImGetMidiTimeParam(ImPlayerData* data, s32 param);
	s32 ImGetMidiParam(ImSoundId soundId, s32 param);
	s32 ImGetPendingSoundCount(ImSoundId soundId);
	s32 ImWrapValue(s32 value, s32 a, s32 b);
	void ImHandleChannelPriorityChange(ImMidiPlayer* player, ImMidiOutChannel* channel);
	ImSoundId ImFindNextMidiSound(ImSoundId soundId);
	ImMidiPlayer* ImGetMidiPlayer(ImSoundId soundId);
	s32 ImSetHookMidi(ImSoundId soundId, s32 value);
	s32 ImSetSequence(ImPlayerData* data, u8* sndData, s32 seqIndex);

	void ImMidiLock();
	void ImMidiUnlock();
	s32  ImGetDeltaTime();
	s32  ImMidiSetSpeed(ImPlayerData* data, u32 value);
	void ImSetTempo(ImPlayerData* data, u32 tempo);
	void ImSetMidiTicksPerBeat(ImPlayerData* data, s32 ticksPerBeat, s32 beatsPerMeasure);;

	ImSoundId ImOpenMidi(const char* name);
	s32 ImCloseMidi(char* name);
	s32 ImMidiCall(s32 index, s32 id, s32 arg1, s32 arg2, s32 arg3);

	// Midi channel commands
	void ImMidiChannelSetVolume(ImMidiChannel* midiChannel, s32 volume);
	void ImHandleChannelDetuneChange(ImMidiPlayer* player, ImMidiOutChannel* channel);
	void ImHandleChannelPitchBend(ImMidiPlayer* player, s32 channelIndex, s32 fractValue, s32 intValue);
	void ImMidiChannelSetPgm(ImMidiChannel* midiChannel, s32 pgm);
	void ImMidiChannelSetPriority(ImMidiChannel* midiChannel, s32 priority);
	void ImMidiChannelSetPartNoteReq(ImMidiChannel* midiChannel, s32 noteReq);
	void ImMidiChannelSetPan(ImMidiChannel* midiChannel, s32 pan);
	void ImMidiChannelSetModulation(ImMidiChannel* midiChannel, s32 modulation);
	void ImHandleChannelPan(ImMidiChannel* midiChannel, s32 pan);
	void ImSetChannelSustain(ImMidiChannel* midiChannel, s32 sustain);

	void ImMidiStopAllNotes(ImMidiPlayer* player);
	
	void ImHandleNoteOff(ImMidiChannel* midiChannel, s32 instrumentId);
	void ImHandleNoteOn(ImMidiChannel* channel, s32 instrumentId, s32 velocity);
	void ImMidi_DrumOut_NoteOn(s32 priority, s32 partNoteReq, s32 volume, s32 instrumentId, s32 velocity);

	// Initialization
	s32 ImSetupFilesModule(iMuseInitData* initData);
	s32 ImInitializeGroupVolume();
	s32 ImInitializeSlots(iMuseInitData* initData);
	s32 ImInitializeSustain();
	s32 ImInitializePlayers();
	s32 ImInitializeMidiEngine(iMuseInitData* initData);
	s32 ImInitializeInterrupt(iMuseInitData* initData);
			
	/////////////////////////////////////////////////////
	// Internal State
	/////////////////////////////////////////////////////
	// These need to be updated across threads.
	atomic_s32 s_imPause;
	atomic_s32 s_midiPaused;
	atomic_s32 s_midiLock;

	static iMuseInitData s_imInitData = { 0, IM_WAVE_11kHz, 8, 6944 };

	// TODO: Split modules into files.
	// Files module.
	static iMuseInitData* s_imFilesData;

	const char* c_midi = "MIDI";
	const char* c_crea = "Crea";
	static s32 s_iMuseTimestepMicrosec = 6944;

	static MemoryRegion* s_memRegion = nullptr;
	static s32 s_iMuseTimeInMicrosec = 0;
	static s32 s_iMuseTimeLong = 0;
	static s32 s_iMuseSystemTime = 0;

	static ImMidiChannel s_midiChannels[MIDI_CHANNEL_COUNT - 1];
	static s32 s_midiFrame = 0;
	static s32 s_trackTicksRemaining;
	static s32 s_midiTickDelta;
	static bool s_imEnableMusic = true;

	static ImMidiPlayer s_midiPlayers[IM_MIDI_PLAYER_COUNT];
	static ImPlayerData s_playerData[IM_MIDI_PLAYER_COUNT];
	static ImPlayerData s_imSoundNextState;
	static ImPlayerData s_imSoundPrevState;
	static s32 s_midiDriverNotReady = 1;

	static s32 s_groupVolume[groupMaxCount] = { 0 };
	static s32 s_soundGroupVolume[groupMaxCount] =
	{
		imMaxVolume, imMaxVolume, imMaxVolume, imMaxVolume,
		imMaxVolume, imMaxVolume, imMaxVolume, imMaxVolume,
		imMaxVolume, imMaxVolume, imMaxVolume, imMaxVolume,
		imMaxVolume, imMaxVolume, imMaxVolume, imMaxVolume,
	};

	static u32 s_midiSustainChannelMask[MIDI_CHANNEL_COUNT] = { 0 };
	static u32 s_midiInstrumentChannelMask[MIDI_INSTRUMENT_COUNT];
	static u32 s_midiInstrumentChannelMask1[MIDI_INSTRUMENT_COUNT];
	static u32 s_midiInstrumentChannelMask3[MIDI_INSTRUMENT_COUNT];
	static u32 s_midiInstrumentChannelMaskShared[MIDI_INSTRUMENT_COUNT];
	static u32 s_curMidiInstrumentMask[MIDI_INSTRUMENT_COUNT];
	static s32 s_curInstrumentCount;

	static ImSound s_sounds[IM_MAX_SOUNDS];
	static ImSound* s_soundList = nullptr;

	static s32 s_ImDrumOutCh_priority;
	static s32 s_ImDrumOutCh_partNoteReq;
	static s32 s_ImDrumOutCh_volume;

	// Midi files loaded.
	static u32 s_midiFileCount = 0;
	static u8* s_midiFiles[IM_MIDI_FILE_COUNT];

	static ImResourceCallback s_resourceCallback = nullptr;

	/////////////////////////////////////////////////////////// 
	// Main API
	// -----------------------------------------------------
	// This includes both high level functions and low level
	// Note:
	// The original DOS code wrapped calls to an internal
	// dispatch function which is removed for TFE, calling
	// the functions directly instead.
	/////////////////////////////////////////////////////////// 

	////////////////////////////////////////////////////
	// High level functions
	////////////////////////////////////////////////////
	s32 ImSetMasterVol(s32 vol)
	{
		ImSetGroupVol(groupMaster, vol);
		return imSuccess;
	}

	s32 ImGetMasterVol(void)
	{
		return ImGetGroupVolume(groupMaster);
	}

	s32 ImSetMusicVol(s32 vol)
	{
		ImSetGroupVol(groupMusic, vol);
		return imSuccess;
	}

	s32 ImGetMusicVol(void)
	{
		return ImGetGroupVolume(groupMusic);
	}

	s32 ImSetSfxVol(s32 vol)
	{
		ImSetGroupVol(groupSfx, vol);
		return imSuccess;
	}

	s32 ImGetSfxVol(void)
	{
		return ImGetGroupVolume(groupSfx);
	}

	s32 ImSetVoiceVol(s32 vol)
	{
		ImSetGroupVol(groupVoice, vol);
		return imSuccess;
	}

	s32 ImGetVoiceVol(void)
	{
		return ImGetGroupVolume(groupVoice);
	}

	s32 ImStartSfx(ImSoundId soundId, s32 priority)
	{
		if (ImStartSound(soundId, priority) == imSuccess)
		{
			return ImSetParam(soundId, soundGroup, groupSfx);
		}
		return imFail;
	}

	s32 ImStartVoice(ImSoundId soundId, s32 priority)
	{
		if (ImStartSound(soundId, priority) == imSuccess)
		{
			return ImSetParam(soundId, soundGroup, groupVoice);
		}
		return imFail;
	}

	s32 ImStartMusic(ImSoundId soundId, s32 priority)
	{
		// Not used in Dark Forces.
		assert(0);
		return imNotImplemented;
	}

	void ImUnloadAll(void)
	{
		for (s32 n = 0; n < 10; n++)
		{
			ImSound* sound = &s_sounds[n];
			if (sound->id)
			{
				ImCloseMidi(sound->name);
			}
		}
	}
	
	// Called ImLoadSound() in the original code, but it only loads midi.
	ImSoundId ImLoadMidi(const char *name)
	{
		if (!s_imEnableMusic || !name || !name[0])
		{
			return imFail;
		}

		ImSoundId soundId = ImFindMidi(name);
		if (soundId)
		{
			ImSound* sound = s_soundList;
			while (sound)
			{
				if (sound->id == soundId)
				{
					sound->refCount++;
					return soundId;
				}
				sound = sound->next;
			}
		}

		for (s32 n = 0; n < 10; n++)
		{
			ImSound* sound = &s_sounds[n];
			if (!sound->id)
			{
				if (strlen(name) >= 17)
				{
					IM_LOG_ERR("Name too long: %s", name);
					return imFail;
				}
				soundId = ImOpenMidi(name);
				if (!soundId)
				{
					IM_LOG_ERR("%s", "Host couldn't load sound");
					return imFail;
				}

				strcpy(sound->name, name);
				sound->id = soundId;
				sound->refCount = 1;
				IM_LIST_ADD(s_soundList, sound);
				return soundId;
			}
		}

		IM_LOG_ERR("%s", "Sound List FULL!");
		return imFail;
	}

	////////////////////////////////////////////////////
	// Low level functions
	////////////////////////////////////////////////////
	s32 ImInitialize(MemoryRegion* memRegion)
	{
		//////////////////////////////////////////////////////////////////////////////////////////
		// Notes:
		//   * In DOS, a memory region is setup for iMuse (the descriptor setup), for TFE this
		//     is replicated by using the passed in memory region.
		//   * Initialize Debug skipped since it is a no-op for Dark Forces due to settings.
		//   * The host interrupt handler is NULL for Dark Forces.
		//   * Memory descriptors and PIC/PIT setup are skipped since they are DOS specific.
		//   * Init data is hardcoded to Dark Forces values and reduced to relevant fields.
		//////////////////////////////////////////////////////////////////////////////////////////
		s_memRegion = memRegion;

		s_iMuseTimeInMicrosec = 0;
		s_iMuseTimeLong = 0;
		IM_LOG_MSG("Initializing...COMMANDS module...");

		if (ImSetupFilesModule(&s_imInitData) == imSuccess)
		{
			if (ImInitializeGroupVolume() != imSuccess) { return imFail; }
			if (ImClearAllSoundFaders()   != imSuccess) { return imFail; }
			if (ImClearTriggersAndCmds()  != imSuccess) { return imFail; }
			if (ImInitializeMidiEngine(&s_imInitData)   != imSuccess) { return imFail; }
			if (ImInitializeDigitalAudio(&s_imInitData) != imSuccess) { return imFail; }
			if (ImInitializeInterrupt(&s_imInitData)    != imSuccess) { return imFail; }

			s_imPause = 0;
			IM_LOG_MSG("Initialization complete...");
		}
		return imSuccess;
	}

	s32 ImTerminate(void)
	{
		ImStopAllSounds();
		ImUnloadAll();
		ImTerminateDigitalAudio();
		TFE_MidiPlayer::midiClearCallback();
		TFE_MidiPlayer::setMaximumNoteLength();

		s_iMuseTimeInMicrosec = 0;
		s_iMuseTimeLong = 0;
		s_iMuseSystemTime = 0;
		s_midiFrame = 0;
		s_trackTicksRemaining = 0;
		s_midiTickDelta = 0;
		s_soundList = nullptr;
		s_midiFileCount = 0;
		s_resourceCallback = nullptr;

		return imSuccess;
	}
		
	s32 ImSetResourceCallback(ImResourceCallback callback)
	{
		s_resourceCallback = callback;
		return imSuccess;
	}

	s32 ImPause(void)
	{
		s32 res = imSuccess;
		if (!s_imPause)
		{
			res  = ImPauseMidi();
			res |= ImPauseDigitalSound();
		}
		s_imPause++;
		s32 pause = s_imPause;
		return (res == imSuccess) ? pause : res;
	}

	s32 ImResume(void)
	{
		s32 res = imSuccess;
		if (s_imPause == 1)
		{
			res  = ImResumeMidi();
			res |= ImResumeDigitalSound();
		}
		if (s_imPause)
		{
			s_imPause--;
		}
		s32 pause = s_imPause;
		return (res == imSuccess) ? pause : res;
	}

	s32 ImSetGroupVol(s32 group, s32 volume)
	{
		if (group >= groupMaxCount || volume > imMaxVolume)
		{
			return imArgErr;
		}
		else if (volume == imGetValue)
		{
			return s_groupVolume[group];
		}

		s32 groupVolume = s_groupVolume[group];
		if (group == groupMaster)
		{
			s_groupVolume[groupMaster] = volume;
			s_soundGroupVolume[groupMaster] = 0;
			for (s32 i = 1; i < groupMaxCount; i++)
			{
				s_soundGroupVolume[i] = ((s_groupVolume[i] + 1) * volume) >> imVolumeShift;
			}
		}
		else
		{
			s_groupVolume[group] = volume;
			s_soundGroupVolume[group] = ((s_groupVolume[groupMaster] + 1) * volume) >> imVolumeShift;
		}
		ImHandleChannelGroupVolume();

		return groupVolume;
	}

	s32 ImStartSound(ImSoundId soundId, s32 priority)
	{
		u8* data = ImInternalGetSoundData(soundId);
		if (!data)
		{
			IM_LOG_ERR("%s", "null sound addr in StartSound()");
			return imFail;
		}

		s32 i = 0;
		for (; i < 4; i++)
		{
			if (data[i] != c_midi[i])
			{
				break;
			}
		}
		if (i == 4)  // Midi
		{
			return ImSetupMidiPlayer(soundId, priority);
		}
		else  // Digital Sound
		{
			const u8* creative = (u8*)c_crea;
			for (i = 0; i < 4; i++)
			{
				if (data[i] != creative[i])
				{
					return imFail;
				}
			}
			return ImStartDigitalSound(soundId, priority);
		}
	}

	s32 ImStopSound(ImSoundId soundId)
	{
		s32 type = ImGetSoundType(soundId);
		if (type == typeMidi)
		{
			return ImFreeMidiPlayer(soundId);
		}
		else if (type == typeWave)
		{
			return ImFreeWaveSoundById(soundId);
		}
		return imFail;
	}

	s32 ImStopAllSounds(void)
	{
		s32 res = ImClearAllSoundFaders();
		res |= ImClearTriggersAndCmds();
		res |= ImReleaseAllPlayers();
		res |= ImFreeAllWaveSounds();

		TFE_MidiPlayer::stopMidiSound();
		return res;
	}

	ImSoundId ImGetNextSound(ImSoundId soundId)
	{
		ImSoundId nextMidiId = ImFindNextMidiSound(soundId);
		ImSoundId nextWaveId = ImFindNextWaveSound(soundId);
		if (nextMidiId == 0 || (nextWaveId && u64(nextMidiId) >= u64(nextWaveId)))
		{
			return nextWaveId;
		}
		return nextMidiId;
	}

	s32 ImSetParam(ImSoundId soundId, s32 param, s32 value)
	{
		iMuseSoundType type = (iMuseSoundType)ImGetSoundType(soundId);
		if (type == typeMidi)
		{
			return ImSetMidiParam(soundId, param, value);
		}
		else if (type == typeWave)
		{
			return ImSetWaveParam(soundId, param, value);
		}
		return imFail;
	}

	s32 ImGetParam(ImSoundId soundId, s32 param)
	{
		iMuseSoundType type = (iMuseSoundType)ImGetSoundType(soundId);
		if (param == soundType)
		{
			return type;
		}
		else if (param == soundPendCount)
		{
			return ImGetPendingSoundCount(soundId);
		}
		else if (type == typeMidi)
		{
			return ImGetMidiParam(soundId, param);
		}
		else if (type == typeWave)
		{
			return ImGetWaveParam(soundId, param);
		}
		else if (param == soundPlayCount)
		{
			// This is zero here, since there is no valid type - 
			// so no sounds are that type are playing.
			return 0;
		}
		return imFail;
	}

	s32 ImSetHook(ImSoundId soundId, s32 value)
	{
		iMuseSoundType type = (iMuseSoundType)ImGetSoundType(soundId);
		if (type == typeMidi)
		{
			return ImSetHookMidi(soundId, value);
		}
		else if (type == typeWave)
		{
			// IM_TODO
		}
		return imFail;
	}

	s32 ImGetHook(ImSoundId soundId)
	{
		// Not called from Dark Forces.
		return imSuccess;
	}
		
	s32 ImJumpMidi(ImSoundId soundId, s32 chunk, s32 measure, s32 beat, s32 tick, s32 sustain)
	{
		ImMidiPlayer* player = ImGetMidiPlayer(soundId);
		if (!player) { return imInvalidSound; }

		IM_DBG_MSG("JMP(C)");
		return ImJumpMidiInternal(player->data, chunk, measure, beat, tick, sustain);
	}

	s32 ImSendMidiMsg(ImSoundId soundId, s32 arg1, s32 arg2, s32 arg3)
	{
		// Not called by Dark Forces.
		IM_DBG_MSG("MSG [s 0x%x, a1 0x%x, a2 0x%x, a3 0x%x]", soundId, arg1, arg2, arg3);
		assert(0);
		return imNotImplemented;
	}

	s32 ImShareParts(ImSoundId sound1, ImSoundId sound2)
	{
		ImMidiPlayer* player1 = ImGetMidiPlayer(sound1);
		ImMidiPlayer* player2 = ImGetMidiPlayer(sound2);
		if (!player1 || player1->sharedPart || !player2 || player2->sharedPart)
		{
			return imFail;
		}

		player1->sharedPart = player2;
		player2->sharedPart = player1;
		player1->sharedPartId = sound2;
		player2->sharedPartId = sound1;
		ImMidiSetupParts();
		return imSuccess;
	}

	///////////////////////////////////////////////////////////
	// Internal "main loop"
	///////////////////////////////////////////////////////////
	// Main update entry point which is called at a fixed rate (see ImGetDeltaTime).
	// This is responsible for updating the midi players and updating digital audio playback.
	void ImUpdate()
	{
		const s32 dtInMicrosec = ImGetDeltaTime();

		// Update Midi and Audio
		ImUpdateMidi();
		if (s_imPause)
		{
			return;
		}

		// Update faders and deferred commands that update at a rate of 60 fps.
		s_iMuseTimeInMicrosec += dtInMicrosec;
		while (s_iMuseTimeInMicrosec >= 16667)
		{
			s_iMuseTimeInMicrosec -= 16667;
			s32 prevTime = s_iMuseSystemTime;
			s_iMuseSystemTime++;	// increment the system time every 1/60th of a second.

			ImUpdateSoundFaders();
			ImHandleDeferredCommands();
		}

		// Update Group Volumes every 6 "frames".
		s_iMuseTimeLong += dtInMicrosec;
		while (s_iMuseTimeLong >= 100000)	// ~6 frames @ 60Hz
		{
			s_iMuseTimeLong -= 100000;

			s32 musicVolume = ImSetGroupVol(groupMusic, imGetValue);
			ImSoundId soundId = IM_NULL_SOUNDID;
			do
			{
				soundId = ImGetNextSound(soundId);
				if (soundId)
				{
					if (ImGetParam(soundId, soundGroup) == groupVoice)
					{
						musicVolume = (musicVolume * 82) >> imVolumeShift;
						break;
					}
				}
			} while (soundId);

			s32 dippedMusicVolume = ImSetGroupVol(groupDippedMusic, imGetValue);
			if (dippedMusicVolume > musicVolume)
			{
				dippedMusicVolume -= 6;
				if (dippedMusicVolume <= musicVolume)
				{
					dippedMusicVolume = musicVolume;
				}
				ImSetGroupVol(groupDippedMusic, dippedMusicVolume);
			}
			else if (dippedMusicVolume != musicVolume)
			{
				dippedMusicVolume += 3;
				if (dippedMusicVolume >= musicVolume)
				{
					dippedMusicVolume = musicVolume;
				}
				ImSetGroupVol(groupDippedMusic, dippedMusicVolume);
			}
		}
	}

	void ImUpdateMidi()
	{
		if (s_midiLock)
		{
			return;
		}

		ImMidiLock();
		s_midiFrame++;
		if (s_midiFrame > 75)  // Prints the iMuse state every 75 "frames".
		{
			s_midiFrame = 0;
			ImPrintMidiState();
		}
		if (!s_midiPaused)  // Update the midi state and instruments.
		{
			ImMidiPlayer* player = s_midiPlayerList;
			while (player)
			{
				ImAdvanceMidiPlayer(player->data);
				player = player->next;
			}
			ImUpdateSustainedNotes();
		}
		ImMidiUnlock();
	}

	/////////////////////////////////////////////////////////// 
	// Internal Implementation
	/////////////////////////////////////////////////////////// 
	void ImPrintMidiState()
	{
		// Compiled out in the original code.
		// TODO: Replace with TFE debug data.
	}

	s32 ImJumpMidiInternal(ImPlayerData* data, s32 chunk, s32 measure, s32 beat, s32 tick, s32 sustain)
	{
		u8* sndData = ImInternalGetSoundData(data->soundId);
		if (!sndData) { return imInvalidSound; }
				
		measure--;
		beat--;
		chunk--;
		if (measure >= 1000 || beat >= 12 || tick >= 480)
		{
			return imArgErr;
		}

		ImMidiLock();
		memcpy(&s_imSoundPrevState, data, sizeof(ImPlayerData));
		memcpy(&s_imSoundNextState, data, sizeof(ImPlayerData));

		IM_DBG_MSG("JMP c:%d t:%03d.%02d.%03d -> c:%d t:%03d.%02d.%03d [s:%d]", s_imSoundNextState.seqIndex, ImTime_getMeasure(data->nextTick), ImTime_getBeat(data->nextTick), ImTime_getTicks(data->nextTick), chunk, measure, beat, tick, sustain);
		u32 nextTick = ImFixupSoundTick(data, ImTime_setMeasure(measure) + ImTime_setBeat(beat) + ImTime_setTick(tick));
		// If we either change chunks or jump backwards, the sequence needs to be setup/reset.
		if (chunk != s_imSoundNextState.seqIndex || nextTick < (u32)s_imSoundNextState.nextTick)
		{
			if (ImSetSequence(&s_imSoundNextState, sndData, chunk))
			{
				IM_LOG_ERR("%s", "sq jump to invalid chunk.");
				ImMidiUnlock();
				return imFail;
			}
		}
		s_imSoundNextState.nextTick = nextTick;
		ImAdvanceMidi(&s_imSoundNextState, sndData, s_jumpMidiCmdFunc);
		if (s_imEndOfTrack)
		{
			IM_LOG_WRN("sq jump to invalid ms:bt:tk...", NULL);
			ImMidiUnlock();
			return imFail;
		}

		if (sustain)
		{
			// Loop through the channels.
			for (s32 c = 0; c < MIDI_CHANNEL_COUNT; c++)
			{
				ImMidiCommand(data->player, c, MID_SUSTAIN_SWITCH, 0);
				ImMidiCommand(data->player, c, MID_MODULATIONWHEEL_MSB, 0);
				ImHandleChannelPitchBend(data->player, c, 0, imPanCenter);
			}
			ImJumpSustain(data->player, sndData, &s_imSoundPrevState, &s_imSoundNextState);
		}
		else
		{
			ImMidiStopAllNotes(data->player);
		}

		memcpy(data, &s_imSoundNextState, sizeof(ImPlayerData));
		s_imEndOfTrack = 1;
		ImMidiUnlock();

		return imSuccess;
	}

	void ImUpdateSustainedNotes()
	{
		ImSustainedSound* sustainedSound = s_imActiveSustainedSounds;
		while (sustainedSound)
		{
			ImSustainedSound* next = sustainedSound->next;
			ImMidiPlayer* player   = sustainedSound->midiPlayer;
			ImPlayerData* data     = player->data;

			sustainedSound->curTickFixed += data->stepFixed;
			sustainedSound->curTick      -= floor16(sustainedSound->curTickFixed);
			sustainedSound->curTickFixed  = fract16(sustainedSound->curTickFixed);

			if (sustainedSound->curTick < 0)
			{
				ImMidiNoteOff(sustainedSound->midiPlayer, sustainedSound->channelId, sustainedSound->instrumentId, 0);
				IM_LIST_REM(s_imActiveSustainedSounds, sustainedSound);
				IM_LIST_ADD(s_imFreeSustainedSounds,   sustainedSound);
			}
			sustainedSound = next;
		}
	}

	s32 ImPauseMidi()
	{
		s_midiPaused = 1;
		return imSuccess;
	}

	s32 ImResumeMidi()
	{
		s_midiPaused = 0;
		return imSuccess;
	}
		
	s32 ImHandleChannelGroupVolume()
	{
		ImMidiPlayer* player = s_midiPlayerList;
		while (player)
		{
			s32 sndVol = player->volume + 1;
			s32 groupVol = ImGetGroupVolume(player->group);
			player->groupVolume = (sndVol * groupVol) >> imVolumeShift;

			for (s32 i = 0; i < MIDI_CHANNEL_COUNT; i++)
			{
				ImMidiOutChannel* channel = &player->channels[i];
				ImHandleChannelVolumeChange(player, channel);
			}
			player = player->next;
		}
		return imSuccess;
	}

	s32 ImGetGroupVolume(s32 group)
	{
		if (group >= groupMaxCount)
		{
			return imArgErr;
		}
		return s_soundGroupVolume[group];
	}

	void ImHandleChannelVolumeChange(ImMidiPlayer* player, ImMidiOutChannel* channel)
	{
		if (!channel->outChannelCount)
		{
			// This should never be hit.
			IM_LOG_ERR("%s", "Sound channel has 0 output channels.");
		}
		s32 partTrim   = channel->partTrim   + 1;
		s32 partVolume = channel->partVolume + 1;
		channel->groupVolume = (partVolume * partTrim * player->groupVolume) >> (2*imVolumeShift);

		// These checks seem redundant. If groupVolume is != 0, then by definition partTrim and partVolume are != 0.
		if (!player->groupVolume || !channel->partTrim || !channel->partVolume)
		{
			if (!channel->data) { return; }
			ImResetMidiOutChannel(channel);
		}
		else if (channel->data) // has volume.
		{
			ImMidiChannelSetVolume(channel->data, channel->groupVolume);
		}
		ImMidiSetupParts();
	}

	void ImMidiSetupParts()
	{
		ImMidiPlayer* player = s_midiPlayerList;
		ImMidiPlayer* newPlayer  = nullptr;
		ImMidiPlayer* prevPlayer = nullptr;
		ImMidiOutChannel* newChannel  = nullptr;
		ImMidiOutChannel* prevChannel = nullptr;
		s32 cont = 0;

		while (player)
		{
			ImMidiPlayer* sharedPart = player->sharedPart;
			for (s32 m = 0; m < MIDI_CHANNEL_COUNT; m++)
			{
				ImMidiOutChannel* channel = &player->channels[m];
				ImMidiOutChannel* sharedChannel = sharedPart ? &sharedPart->channels[m] : nullptr;

				if (sharedChannel && sharedChannel->partStatus)
				{
					if (channel->data && !sharedChannel->data)
					{
						if (sharedPart->groupVolume && sharedChannel->partTrim && sharedChannel->partVolume)
						{
							ImAssignMidiChannel(sharedPart, sharedChannel, channel->data);
						}
					}
					else if (!channel->data && sharedChannel->data && channel->partStatus && player->groupVolume &&
						      channel->partTrim && channel->partVolume)
					{
						ImAssignMidiChannel(player, channel, sharedChannel->data->sharedMidiChannel);
					}
				}

				if (channel->data)
				{
					if (sharedChannel && sharedChannel->data)
					{
						if (sharedChannel->priority > channel->priority) { continue; }
					}
					if (prevChannel)
					{
						if (channel->priority > prevChannel->priority) { continue; }
					}
					prevPlayer  = player;
					prevChannel = channel;
					cont = 1;
				}
				else if (channel->partStatus && player->groupVolume && channel->partTrim && channel->partVolume)
				{
					if (!newChannel || channel->priority > newChannel->priority)
					{
						newPlayer  = player;
						newChannel = channel;
						cont = 0;
					}
				}
			}
			player = player->next;

			// This will only start once all of the players has been exhausted.
			if (newChannel && !player)
			{
				ImMidiChannel* midiChannel = ImGetFreeMidiChannel();
				if (!midiChannel)
				{
					s32 newPriority  = newChannel->priority;
					s32 prevPriority = prevChannel->priority;
					if (newPriority <= prevPriority && (prevPriority != newPriority || !cont || newPlayer == prevPlayer))
					{
						return;
					}
					
					// The new channel has higher priority, so reset the previous and reassign.
					ImMidiChannel* prev = prevChannel->data;
					ImResetMidiOutChannel(prev->channel);

					ImMidiChannel* shared = prev->sharedMidiChannel;
					if (shared && shared->channel)
					{
						ImResetMidiOutChannel(shared->channel);
					}
					midiChannel = prev;
				}
				ImAssignMidiChannel(newPlayer, newChannel, midiChannel);

				player      = s_midiPlayerList;
				newPlayer   = nullptr;
				prevPlayer  = nullptr;
				newChannel  = nullptr;
				prevChannel = nullptr;
			}
		}
	}

	ImMidiChannel* ImGetFreeMidiChannel()
	{
		for (s32 i = 0; i < MIDI_CHANNEL_COUNT - 1; i++)
		{
			if (!s_midiChannels[i].player && !s_midiChannels[i].sharedPart)
			{
				return &s_midiChannels[i];
			}
		}
		return nullptr;
	}

	void ImAssignMidiChannel(ImMidiPlayer* player, ImMidiOutChannel* channel, ImMidiChannel* midiChannel)
	{
		if (!channel || !midiChannel)
		{
			return;
		}

		channel->data = midiChannel;
		midiChannel->player  = player;
		midiChannel->channel = channel;

		// FIX: a raw MIDI capture of the original engine's actual output showed that,
		// the first time a channel is assigned to a part, it sends a full GM/GS
		// "channel reset" sequence that this reimplementation was missing entirely:
		// Bank Select, Pitch Bend Sensitivity (RPN), an explicit Sustain-off, All
		// Notes Off, Reverb Send, and Chorus Send. A later Program Change on an
		// already-assigned channel (an instrument switch mid-song) does NOT repeat
		// this - only a fresh assignment does (see ImMidiProgramChange()). Without
		// this, a receiving device is left at whatever bank/reverb/chorus/pitch-bend-
		// range state it happened to already be in, which can audibly differ from
		// the intended sound (e.g. missing the intended reverb/chorus send entirely).
		ImControlChange(midiChannel->channelId, MID_BANK_SELECT_MSB, 0);
		ImMidiChannelSetPgm(midiChannel, channel->partPgm);
		ImSetPitchBendRange(midiChannel->channelId, 16);
		ImControlChange(midiChannel->channelId, MID_SUSTAIN_SWITCH, 0);
		ImControlChange(midiChannel->channelId, MID_ALL_NOTES_OFF, 0);
		ImControlChange(midiChannel->channelId, MID_FX_REVERB, 64);
		ImControlChange(midiChannel->channelId, MID_FX_CHORUS, 0);

		ImMidiChannelSetPriority(midiChannel, channel->priority);
		ImMidiChannelSetPartNoteReq(midiChannel, channel->partNoteReq);
		ImMidiChannelSetVolume(midiChannel, channel->groupVolume);
		ImMidiChannelSetPan(midiChannel, channel->partPan);
		ImMidiChannelSetModulation(midiChannel, channel->modulation);
		// FIX: this used to call ImHandleChannelPan(midiChannel, channel->pan), but
		// channel->pan here is not a pan value at all - see ImHandleChannelDetuneChange()
		// above. At this point (freshly assigned channel) it is always 0, so the
		// intended effect is a Pitch Bend reset to center, matching what the original
		// engine's capture showed (an explicit Pitch Bend = 8192 message right here).
		ImHandleChannelDetuneChange(player, channel);
		ImSetChannelSustain(midiChannel, channel->sustain);
	}
		
	u8* ImGetSoundData(ImSoundId id)
	{
		if (id & imMidiFlag)
		{
			const ImSoundId index = id & imMidiMask;
			assert(index < IM_MIDI_FILE_COUNT);
			return index < IM_MIDI_FILE_COUNT ? s_midiFiles[index] : nullptr;
		}
		else
		{
			u8* data = nullptr;
			if (s_resourceCallback)
			{
				data = s_resourceCallback(id);
			}
			else
			{
				// Cast ImSoundId as a pointer.
				ImResource* res = (ImResource*)id;
				data = res->data;
			}
			return data;
		}
		return nullptr;
	}

	s32 ImInternal_SoundValid(ImSoundId soundId)
	{
		// Since soundId can be a pointer, we can't use the original imValidMask since the pointer may be 64-bit.
		// if (soundId && soundId < imValidMask)
		if (soundId)
		{
			return 1;
		}
		return 0;
	}

	u8* ImInternalGetSoundData(ImSoundId soundId)
	{
		if (ImInternal_SoundValid(soundId))
		{
			return ImGetSoundData(soundId);
		}
		return nullptr;
	}

	ImMidiPlayer* ImGetFreePlayer(s32 priority)
	{
		ImMidiPlayer* midiPlayer = s_midiPlayers;
		for (s32 i = 0; i < 2; i++, midiPlayer++)
		{
			if (!midiPlayer->soundId)
			{
				return midiPlayer;
			}
		}
		IM_LOG_WRN("no spare players", NULL);
		return nullptr;
	}
		
	s32 ImStartMidiPlayerInternal(ImPlayerData* data, ImSoundId soundId)
	{
		u8* sndData = ImInternalGetSoundData(soundId);
		if (!sndData)
		{
			return imInvalidSound;
		}

		data->soundId = soundId;
		data->tickAccum = 0;
		ImSetTempo(data, 500000); // microseconds per beat, 500000 = 120 beats per minute
		ImMidiSetSpeed(data, 128);
		ImSetMidiTicksPerBeat(data, 480, 4);
		return ImSetSequence(data, sndData, 0);
	}

	ImSoundId ImFindMidi(const char* name)
	{
		ImSound* sound = s_soundList;
		while (sound)
		{
			if (!strcasecmp(name, sound->name) && sound->refCount)
			{
				return sound->id;
			}
			sound = sound->next;
		}
		return IM_NULL_SOUNDID;
	}

	ImSoundId loadMidiFile(char* midiFile)
	{
		FilePath filePath;
		if (!TFE_Paths::getFilePath(midiFile, &filePath))
		{
			IM_LOG_ERR("Cannot find midi file '%s'.", midiFile);
			return IM_NULL_SOUNDID;
		}
		FileStream file;
		if (!file.open(&filePath, Stream::MODE_READ))
		{
			IM_LOG_ERR("Cannot open midi file '%s'.", midiFile);
			return IM_NULL_SOUNDID;
		}
		assert(s_midiFileCount < IM_MIDI_FILE_COUNT);

		size_t len = file.getSize();
		s_midiFiles[s_midiFileCount] = (u8*)imuse_alloc(len);
		file.readBuffer(s_midiFiles[s_midiFileCount], u32(len));
		file.close();

		ImSoundId id = ImSoundId(s_midiFileCount | imMidiFlag);
		s_midiFileCount++;
		return id;
	}

	ImSoundId ImOpenMidi(const char* name)
	{
		char filename[32];
		strcpy(filename, name);
		strcat(filename, ".gmd");
		return loadMidiFile(filename);
	}

	s32 ImCloseMidi(char* name)
	{
		ImSound* sound = s_soundList;
		while (sound)
		{
			if (!strcasecmp(sound->name, name) && sound->refCount)
			{
				sound->refCount--;
				// The ref count seems to be ignored here.
				{
					const ImSoundId index = sound->id & imMidiMask;
					assert(index < IM_MIDI_FILE_COUNT);
					if (index < IM_MIDI_FILE_COUNT)
					{
						imuse_free(s_midiFiles[index]);
						s_midiFiles[index] = nullptr;
						s_midiFileCount--;
					}

					sound->id = IM_NULL_SOUNDID;
					sound->refCount = 0;

					IM_LIST_REM(s_soundList, sound);
				}
				break;
			}
			sound = sound->next;
		}
		return imSuccess;
	}
		
	s32 ImSetupMidiPlayer(ImSoundId soundId, s32 priority)
	{
		s32 clampedPriority = clamp(priority, 0, imMaxPriority);
		ImMidiPlayer* player = ImGetFreePlayer(clampedPriority);
		if (!player)
		{
			return imAllocErr;
		}

		player->sharedPart = nullptr;
		player->sharedPartId = 0;
		player->marker = -1;
		player->group = groupMusic;
		player->priority = clampedPriority;
		player->volume = imMaxVolume;
		player->groupVolume = ImGetGroupVolume(player->group);
		player->pan = imPanCenter;

		player->detune = 0;
		player->transpose = 0;
		player->mailbox = 0;
		IM_DBG_MSG("Init: Hook = 0");
		player->hook = 0;

		for (s32 i = 0; i < MIDI_CHANNEL_COUNT; i++)
		{
			ImMidiOutChannel* channel = &player->channels[i];

			channel->data = nullptr;
			channel->partStatus = 0;
			channel->partPgm = 128;
			channel->partTrim = imMaxVolume;
			channel->partPriority = 0;
			channel->priority = player->priority;
			channel->partNoteReq = 1;
			channel->partVolume = imMaxVolume;
			channel->groupVolume = player->groupVolume;
			channel->partPan = imPanCenter;
			channel->modulation = 0;
			channel->sustain = 0;
			channel->pitchBend = 0;
			channel->outChannelCount = 2;
			channel->pan = 0;
		}

		ImPlayerData* playerData = player->data;
		if (ImStartMidiPlayerInternal(playerData, soundId))
		{
			return imFail;
		}

		player->soundId = soundId;
		IM_LIST_ADD(s_midiPlayerList, player);
		return imSuccess;
	}
		
	s32 ImFreeMidiPlayer(ImSoundId soundId)
	{
		ImMidiPlayer* player = ImGetMidiPlayer(soundId);
		if (player)
		{
			ImReleaseMidiPlayer(player);
			// This should be null - but the original code handles duplicates.
			do
			{
				player = ImGetMidiPlayer(soundId);
				if (player)
				{
					ImReleaseMidiPlayer(player);
				}
			} while (player);
			return imSuccess;
		}
		return imFail;
	}

	s32 ImReleaseAllPlayers()
	{
		ImMidiPlayer* player = nullptr;
		do
		{
			player = s_midiPlayerList;
			if (player)
			{
				ImReleaseMidiPlayer(player);
			}
		} while (player);
		return imSuccess;
	}
		
	s32 ImGetSoundType(ImSoundId soundId)
	{
		ImSoundId foundId = IM_NULL_SOUNDID;
		// First check midi sounds.
		do
		{
			foundId = ImFindNextMidiSound(foundId);
			if (foundId && foundId == soundId)
			{
				return typeMidi;
			}
		} while (foundId);

		// Then check for wave sounds.
		foundId = IM_NULL_SOUNDID;
		do
		{
			foundId = ImFindNextWaveSound(foundId);
			if (foundId && foundId == soundId)
			{
				return typeWave;
			}
		} while (foundId);

		// The sound doesn't exist.
		return imFail;
	}

	void ImHandleChannelPriorityChange(ImMidiPlayer* player, ImMidiOutChannel* channel)
	{
		channel->priority = min(channel->partPriority + player->priority, 127);
		if (channel->data)
		{
			ImMidiChannelSetPriority(channel->data, channel->priority);
		}
	}

	s32 ImWrapValue(s32 value, s32 a, s32 b)
	{
		while (value < a)
		{
			value += 12;
		}
		while (value > b)
		{
			value -= 12;
		}
		return value;
	}

	s32 ImSetMidiParam(ImSoundId soundId, s32 param, s32 value)
	{
		ImMidiPlayer* player = ImGetMidiPlayer(soundId);
		if (!player)
		{
			return imInvalidSound;
		}

		s32 midiChannel = param & 0x00ff;
		iMuseParameter imParam = iMuseParameter(param & 0xff00);
		ImMidiOutChannel* channels = player->channels;

		if (imParam == soundGroup)
		{
			if (value >= groupMaxCount) { return imArgErr; }

			player->group = value;
			s32 playerVol = player->volume + 1;
			s32 groupVol = ImGetGroupVolume(value);
			player->groupVolume = (playerVol * groupVol) >> 7;

			for (s32 m = 0; m < MIDI_CHANNEL_COUNT; m++)
			{
				ImHandleChannelVolumeChange(player, &channels[m]);
			}
		}
		else if (imParam == soundPriority && value <= imMaxPriority)
		{
			player->priority = value;
			for (s32 m = 0; m < MIDI_CHANNEL_COUNT; m++)
			{
				ImHandleChannelPriorityChange(player, &channels[m]);
			}
		}
		else if (imParam == soundVol && value <= imMaxVolume)
		{
			player->volume = value;
			s32 groupVol = ImGetGroupVolume(player->group);
			player->groupVolume = ((value + 1) * groupVol) >> imVolumeShift;

			for (s32 m = 0; m < MIDI_CHANNEL_COUNT; m++)
			{
				ImHandleChannelVolumeChange(player, &channels[m]);
			}
		}
		else if (imParam == soundPan && value <= imPanMax)
		{
			player->pan = value;
			for (s32 m = 0; m < MIDI_CHANNEL_COUNT; m++)
			{
				s32 pan = clamp(channels[m].partPan + value - imPanCenter, 0, imPanMax);
				ImMidiChannelSetPan(channels[m].data, pan);
			}
		}
		else if (imParam == soundDetune)
		{
			if (value < -9216 || value > 9216) { return imArgErr; }
			player->detune = value;
			for (s32 m = 0; m < MIDI_CHANNEL_COUNT; m++)
			{
				ImHandleChannelDetuneChange(player, &channels[m]);
			}
		}
		else if (imParam == soundTranspose)
		{
			if (value < -12 || value > 12) { return imArgErr; }
			player->transpose = (value == 0) ? 0 : ImWrapValue(value + player->transpose, -12, 12);

			for (s32 m = 0; m < MIDI_CHANNEL_COUNT; m++)
			{
				ImHandleChannelDetuneChange(player, &channels[m]);
			}
		}
		else if (imParam == soundMailbox)
		{
			player->mailbox = value;
		}
		else if (imParam == midiSpeed)
		{
			return ImMidiSetSpeed(player->data, value);
		}
		else if (imParam == midiPartTrim)
		{
			if (midiChannel >= MIDI_CHANNEL_COUNT || value > imMaxVolume) { return imArgErr; }

			channels[midiChannel].partTrim = value;
			ImHandleChannelVolumeChange(player, &channels[midiChannel]);
		}
		else
		{
			IM_LOG_WRN("PlSetParam() couldn't set param %lu.", param);
			return imArgErr;
		}

		return imSuccess;
	}

	s32 ImGetMidiTimeParam(ImPlayerData* data, s32 param)
	{
		if (param == midiChunk)
		{
			return data->seqIndex + 1;
		}
		else if (param == midiMeasure)
		{
			return ImTime_getMeasure(data->nextTick) + 1;
		}
		else if (param == midiBeat)
		{
			return ImTime_getBeat(data->nextTick) + 1;
		}
		else if (param == midiTick)
		{
			return ImTime_getTicks(data->nextTick);
		}
		else if (param == midiSpeed)
		{
			return data->speed;
		}
		return imArgErr;
	}

	s32 ImGetMidiParam(ImSoundId soundId, s32 param)
	{
		s32 playCount = 0;
		ImMidiPlayer* player = s_midiPlayerList;
		while (player)
		{
			if (player->soundId == soundId)
			{
				s32 midiChannel = param & 0x00ff;
				iMuseParameter imParam = iMuseParameter(param & 0xff00);
				ImMidiOutChannel* channel = &player->channels[midiChannel];

				switch (imParam)
				{
					case soundPlayCount:
					{
						playCount++;
					} break;
					case soundPendCount:
					{
						return imFail;
					} break;
					case soundMarker:
					{
						return player->marker;
					} break;
					case soundGroup:
					{
						return player->group;
					} break;
					case soundPriority:
					{
						return player->priority;
					} break;
					case soundVol:
					{
						return player->volume;
					} break;
					case soundPan:
					{
						return player->pan;
					} break;
					case soundDetune:
					{
						return player->detune;
					} break;
					case soundTranspose:
					{
						return player->transpose;
					} break;
					case soundMailbox:
					{
						return player->mailbox;
					} break;
					case midiChunk:
					case midiMeasure:
					case midiBeat:
					case midiTick:
					case midiSpeed:
					{
						return ImGetMidiTimeParam(player->data, param);
					} break;
					case midiPartTrim:
					{
						return channel->partTrim;
					} break;
					case midiPartStatus:
					{
						return channel->partStatus ? 1 : 0;
					} break;
					case midiPartPriority:
					{
						return channel->partPriority;
					} break;
					case midiPartNoteReq:
					{
						return channel->partNoteReq;
					} break;
					case midiPartVol:
					{
						return channel->partVolume;
					} break;
					case midiPartPan:
					{
						return channel->partPan;
					} break;
					case midiPartPgm:
					{
						return channel->partPgm;
					} break;
					default:
					{
						return imArgErr;
					}
				};
			}
			player = player->next;
		}
		if (param == soundPlayCount)
		{
			return playCount;
		}

		return imInvalidSound;
	}

	s32 ImGetPendingSoundCount(ImSoundId soundId)
	{
		// Not called by Dark Forces.
		assert(0);
		return 0;
	}

	// Search a list for the smallest non-zero value that is atleast minValue.
	ImSoundId ImFindNextMidiSound(ImSoundId soundId)
	{
		ImSoundId value = IM_NULL_SOUNDID;
		ImMidiPlayer* player = s_midiPlayerList;
		while (player)
		{
			if (u64(soundId) < u64(player->soundId))
			{
				if (!value || u64(value) > u64(player->soundId))
				{
					value = player->soundId;
				}
			}
			player = player->next;
		}
		return value;
	}
		
	ImMidiPlayer* ImGetMidiPlayer(ImSoundId soundId)
	{
		ImMidiPlayer* player = s_midiPlayerList;
		while (player)
		{
			if (player->soundId == soundId)
			{
				break;
			}
			player = player->next;
		}
		return player;
	}

	s32 ImSetHookMidi(ImSoundId soundId, s32 value)
	{
		if (value > 0x80)
		{
			return imArgErr;
		}
		ImMidiPlayer* player = ImGetMidiPlayer(soundId);
		if (!player)
		{
			return imInvalidSound;
		}
		IM_DBG_MSG("Hook = %d", value);
		player->hook = value;
		return imSuccess;
	}

	void ImMidiLock()
	{
		s_midiLock++;
	}

	void ImMidiUnlock()
	{
		if (s_midiLock)
		{
			s_midiLock--;
		}
	}

	// Gets the deltatime in microseconds (i.e. 1,000 ms / 1 microsec).
	s32 ImGetDeltaTime()
	{
		return s_iMuseTimestepMicrosec;
	}
		
	s32 ImMidiSetSpeed(ImPlayerData* data, u32 value)
	{
		if (value > 255)
		{
			return imArgErr;
		}

		ImMidiLock();
		data->speed = value;
		data->stepFixed = (value * data->step) >> 7;
		ImMidiUnlock();
		return imSuccess;
	}

	void ImSetTempo(ImPlayerData* data, u32 tempo)
	{
		s32 ticks = ImGetDeltaTime() * 480;
		data->tempo = tempo;

		// Keep reducing the tick scale? until it is less than one?
		// This also reduces the tempo as well.
		while (1)
		{
			if (!(ticks & 0xffff0000))
			{
				if (!(tempo & 0xffff0000))
				{
					break;
				}
				ticks >>= 1;
				tempo >>= 1;
			}
			else
			{
				ticks >>= 1;
				tempo >>= 1;
			}
		}

		data->step = intToFixed16(ticks) / tempo;
		ImMidiSetSpeed(data, data->speed);
	}

	void ImSetMidiTicksPerBeat(ImPlayerData* data, s32 ticksPerBeat, s32 beatsPerMeasure)
	{
		ImMidiLock();
		data->ticksPerBeat = ticksPerBeat;
		data->beatsPerMeasure = intToFixed16(beatsPerMeasure);
		ImMidiUnlock();
	}

	s32 ImSetSequence(ImPlayerData* data, u8* sndData, s32 seqIndex)
	{
		u8* chunk = midi_gotoHeader(sndData, "MTrk", seqIndex);
		if (!chunk)
		{
			IM_LOG_ERR("Sq couldn't find chunk %d", seqIndex + 1);
			return imFail;
		}

		// Skip the header and size to the data.
		u8* chunkData = &chunk[8];
		data->seqIndex = seqIndex;
		// offset of the chunk data from the base sound data.
		data->chunkOffset = s32(chunkData - sndData);

		u32 dt = midi_getVariableLengthValue(&chunkData);
		data->curTick = ImFixupSoundTick(data, dt);
		data->nextTick = 0;
		data->chunkPtr = s32(chunkData - (sndData + data->chunkOffset));
		return imSuccess;
	}
		
	// This gets called at a fixed rate, where each step = 'stepFixed' ticks.
	void ImAdvanceMidiPlayer(ImPlayerData* playerData)
	{
		playerData->tickAccum += playerData->stepFixed;
		playerData->nextTick += floor16(playerData->tickAccum);
		// Keep the fractional part.
		playerData->tickAccum = fract16(playerData->tickAccum);

		if ((playerData->nextTick & 0xffff) >= playerData->ticksPerBeat)
		{
			playerData->nextTick = ImFixupSoundTick(playerData, playerData->nextTick);
		}
		if (playerData->curTick < playerData->nextTick)
		{
			u8* sndData = ImInternalGetSoundData(playerData->soundId);
			if (sndData)
			{
				ImAdvanceMidi(playerData, sndData, s_midiCmdFunc);
				return;
			}
			IM_LOG_ERR("%s", "sq int handler got null addr");
			assert(0);
			// TODO: ERROR handling
		}
	}
		
	void ImMidiGetInstruments(ImMidiPlayer* player, u32* soundMidiInstrumentMask, s32* midiInstrumentCount)
	{
		*midiInstrumentCount = 0;

		// Build a mask for all active midi channels.
		u32 channelMask = 0;
		for (s32 i = 0; i < MIDI_CHANNEL_COUNT - 1; i++)
		{
			ImMidiChannel* midiChannel = &s_midiChannels[i];
			if (player == midiChannel->player)
			{
				channelMask |= c_channelMask[midiChannel->channelId];
			}
		}

		// Given the instrument channel mask and active channels,
		// count the number of instruments playing.
		for (s32 i = 0; i < MIDI_INSTRUMENT_COUNT; i++)
		{
			u32 value = s_midiInstrumentChannelMask[i] & channelMask;
			soundMidiInstrumentMask[i] = value;
			// Count the number of channels currently playing the instrument.
			// This is counting the bits.
			while (value)
			{
				(*midiInstrumentCount) += (value & 1);
				value >>= 1;
			}
		}

		// Build a mask for all shared midi channels.
		channelMask = 0;
		for (s32 i = 0; i < MIDI_CHANNEL_COUNT - 1; i++)
		{
			ImMidiChannel* midiChannel = &s_midiChannels[i];
			if (player == midiChannel->sharedPart)
			{
				channelMask |= c_channelMask[midiChannel->sharedId];
			}
		}

		// Given the instrument channel mask and shared channels,
		// count the number of instruments playing.
		for (s32 i = 0; i < MIDI_INSTRUMENT_COUNT; i++)
		{
			u32 value = s_midiInstrumentChannelMaskShared[i] & channelMask;
			soundMidiInstrumentMask[i] |= value;

			while (value)
			{
				(*midiInstrumentCount) += (value & 1);
				value >>= 1;
			}
		}
	}
		
	s32 ImMidiGetTickDelta(ImPlayerData* playerData, u32 prevTick, u32 tick)
	{
		const u32 ticksPerMeasure = playerData->ticksPerBeat * playerData->beatsPerMeasure;

		// Compute the previous midi tick.
		u32 prevMeasure = ImTime_getMeasure(prevTick);
		u32 prevMeasureTick = mul16(prevMeasure, ticksPerMeasure);

		u32 prevBeat = ImTime_getBeat(prevTick);
		u32 prevBeatTick = prevBeat * playerData->ticksPerBeat;
		u32 prevMidiTick = prevMeasureTick + prevBeatTick + ImTime_getTicks(prevTick);

		// Compute the current midi tick.
		u32 curMeasure = ImTime_getMeasure(tick);
		u32 curMeasureTick = mul16(curMeasure, ticksPerMeasure);

		u32 curBeat = ImTime_getBeat(tick);
		u32 curBeatTick = curBeat * playerData->ticksPerBeat;
		u32 curMidiTick = curMeasureTick + curBeatTick + ImTime_getTicks(tick);

		return curMidiTick - prevMidiTick;
	}

	void ImMidiProcessSustain(ImPlayerData* playerData, u8* sndData, MidiCmdFuncUnion* midiCmdFunc, ImMidiPlayer* player)
	{
		s_midiTrackEnd = 0;

		u8* data = sndData + playerData->chunkOffset + playerData->chunkPtr;
		s_midiTickDelta = ImMidiGetTickDelta(playerData, playerData->curTick, playerData->nextTick);

		while (!s_midiTrackEnd)
		{
			u8 value = data[0];
			u8 msgFuncIndex;
			if (value < 0xf0 && (value & 0x80))
			{
				u8 msgType = (value & 0x70) >> 4;
				u8 channel = (value & 0x0f);
				if (midiCmdFunc[msgType].cmdFunc)
				{
					midiCmdFunc[msgType].cmdFunc(player, channel, data[1], data[2]);
				}
				data += c_midiMsgSize[msgType];
			}
			else
			{
				if (value == MID_EXCLUSIVE_START)
				{
					msgFuncIndex = IM_MID_SYS_FUNC;
				}
				else
				{
					if (value != 0xff)
					{
						IM_LOG_ERR("su unknown msg type 0x%x.", value);
						return;
					}
					msgFuncIndex = IM_MID_EVENT;
					data++;
				}
				if (midiCmdFunc[msgFuncIndex].evtFunc)
				{
					midiCmdFunc[msgFuncIndex].evtFunc(playerData, data);
				}
				data++;
				data += midi_getVariableLengthValue(&data);
			}
			// Length of message in "ticks"
			if (!s_midiTrackEnd)  // Avoid reading past the end of the buffer.
			{
				s_midiTickDelta += midi_getVariableLengthValue(&data);
			}
		}
	}

	void ImJumpSustain(ImMidiPlayer* player, u8* sndData, ImPlayerData* playerPrevData, ImPlayerData* playerNextData)
	{
		for (s32 i = 0; i < MIDI_CHANNEL_COUNT; i++)
		{
			ImMidiChannel* channel = player->channels[i].data;
			s_midiSustainChannelMask[i] = (channel) ? (1u << u32(channel->channelId)) : 0;
		}

		// Remove active sustained from the midi instrument list, these should already be tracked.
		ImMidiGetInstruments(player, s_curMidiInstrumentMask, &s_curInstrumentCount);
		ImSustainedSound* sustainedSound = s_imActiveSustainedSounds;
		while (sustainedSound && s_curInstrumentCount)
		{
			if (sustainedSound->midiPlayer == player)
			{
				u32 instrMask   = s_curMidiInstrumentMask[sustainedSound->instrumentId];
				u32 channelMask = s_midiSustainChannelMask[sustainedSound->channelId];
				if (channelMask & instrMask)
				{
					s_curMidiInstrumentMask[sustainedSound->instrumentId] &= ~channelMask;
					s_curInstrumentCount--;
				}
			}
			sustainedSound = sustainedSound->next;
		}

		if (s_curInstrumentCount)
		{
			// Capture future "note off" events and add them to the active sounds.
			// These sounds will continue to play during the transition.
			ImMidiProcessSustain(playerPrevData, sndData, s_jumpMidiSustainOpenCmdFunc, player);

			// Make sure all "note off" events have been captured. If uncaptured notes still exist, we have to turn them off manually.
			if (s_curInstrumentCount)
			{
				IM_LOG_WRN("su couldn't find all note-offs...", NULL);
				for (s32 i = 0; i < MIDI_INSTRUMENT_COUNT; i++)
				{
					if (!s_curMidiInstrumentMask[i]) { continue; }
					for (s32 c = 0; c < MIDI_CHANNEL_COUNT; c++)
					{
						if (s_curMidiInstrumentMask[i] & s_midiSustainChannelMask[c])
						{
							IM_LOG_WRN("missing note %d on chan %d...", i, c);
							ImMidiNoteOff(player, c, i, 0);
							s_curInstrumentCount--;
						}
					}
				}
				assert(s_curInstrumentCount == 0);
			}
		}

		// Go through active sounds and calculate the time remaining for the sustain.
		s_trackTicksRemaining = 0;
		sustainedSound = s_imActiveSustainedSounds;
		while (sustainedSound)
		{
			s_trackTicksRemaining = max(s_trackTicksRemaining, sustainedSound->curTick);
			sustainedSound = sustainedSound->next;
		}
		// Get the current instruments again.
		ImMidiGetInstruments(player, s_curMidiInstrumentMask, &s_curInstrumentCount);
		ImMidiProcessSustain(playerNextData, sndData, s_jumpMidiSustainCmdFunc, player);
	}

	////////////////////////////////////////
	// Midi Commands
	////////////////////////////////////////
	void ImMidiChannelSetPgm(ImMidiChannel* midiChannel, s32 pgm)
	{
		if (midiChannel && pgm != midiChannel->pgm)
		{
			midiChannel->sharedMidiChannel->pgm = pgm;
			midiChannel->pgm = pgm;
			ImProgramChange(midiChannel->channelId, pgm);
		}
	}

	void ImMidiChannelSetPriority(ImMidiChannel* midiChannel, s32 priority)
	{
		if (midiChannel && priority != midiChannel->priority)
		{
			midiChannel->sharedMidiChannel->priority = priority;
			midiChannel->priority = priority;
			ImControlChange(midiChannel->channelId, MID_GPC3_MSB, priority);
		}
	}

	void ImMidiChannelSetPartNoteReq(ImMidiChannel* midiChannel, s32 noteReq)
	{
		if (midiChannel && noteReq != midiChannel->noteReq)
		{
			midiChannel->sharedMidiChannel->noteReq = noteReq;
			midiChannel->noteReq = noteReq;
			ImControlChange(midiChannel->channelId, MID_GPC2_MSB, noteReq);
		}
	}

	void ImMidiChannelSetPan(ImMidiChannel* midiChannel, s32 pan)
	{
		if (midiChannel && pan != midiChannel->pan)
		{
			midiChannel->sharedMidiChannel->pan = pan;
			midiChannel->pan = pan;
			ImControlChange(midiChannel->channelId, MID_PAN_MSB, pan);
		}
	}

	void ImMidiChannelSetModulation(ImMidiChannel* midiChannel, s32 modulation)
	{
		if (midiChannel && modulation != midiChannel->modulation)
		{
			midiChannel->sharedMidiChannel->modulation = modulation;
			midiChannel->modulation = modulation;
			ImControlChange(midiChannel->channelId, MID_MODULATIONWHEEL_MSB, modulation);
		}
	}

	void ImHandleChannelPan(ImMidiChannel* midiChannel, s32 pan)
	{
		if (midiChannel && pan != midiChannel->finalPan)
		{
			midiChannel->sharedMidiChannel->finalPan = pan;
			midiChannel->finalPan = pan;
			ImSetPanFine(midiChannel->channelId, 2*pan + 8192);
		}
	}

	void ImSetChannelSustain(ImMidiChannel* midiChannel, s32 sustain)
	{
		if (midiChannel)
		{
			midiChannel->sustain = sustain;
			if (!sustain)
			{
				const u32 channelMask = c_channelMask[midiChannel->channelId];
				for (s32 i = 0; i < MIDI_INSTRUMENT_COUNT; i++)
				{
					if (midiChannel->instrumentMask2[i] & channelMask)
					{
						midiChannel->instrumentMask2[i] &= ~channelMask;
						ImNoteOff(midiChannel->channelId, i);
					}
				}
			}
		}
	}

	void ImMidiChannelSetVolume(ImMidiChannel* midiChannel, s32 volume)
	{
		if (midiChannel && volume != midiChannel->volume)
		{
			midiChannel->sharedMidiChannel->volume = volume;
			midiChannel->volume = volume;
			ImControlChange(midiChannel->channelId, MID_VOLUME_MSB, volume);
		}
	}
		
	void ImHandleChannelDetuneChange(ImMidiPlayer* player, ImMidiOutChannel* channel)
	{
		// FIX (confirmed upstream bug: "Pitch bend is being routed to the wrong function"):
		// despite the field name, channel->pan here has never held a stereo pan value - it is
		// a scratch field holding the combined pitch offset (transpose + detune + incoming
		// pitch bend), in units of 1/256th of a semitone (see ImMidiPitchBend below). The call
		// this used to make, ImHandleChannelPan(), is for genuine stereo pan (a completely
		// different, unrelated per-channel value on a different struct), and its underlying
		// ImSetPanFine() is a no-op - so every transpose/detune/pitch-bend change was silently
		// discarded and never reached any synth. A melody's pitch-bent notes (e.g. a sustained
		// note bent to sound like the next note in a run/legato phrase) would then just hold at
		// their original pitch, which can sound like the "bent-to" note is missing entirely.
		channel->pan = (player->transpose << 8) + player->detune + channel->pitchBend;

		ImMidiChannel* data = channel->data;
		if (data)
		{
			// channel->pan is in units of 1/256th of a semitone. A raw MIDI capture of
			// the original engine's actual output showed it configures each channel's
			// Pitch Bend Sensitivity to +/-16 semitones (RPN 0, Data Entry MSB = 16 -
			// see ImSetPitchBendRange(), called from ImAssignMidiChannel()), NOT +/-1 as
			// an earlier version of this fix assumed from ImMidiPitchBend()'s own math
			// alone. With a full 14-bit pitch wheel sweep (0..16383, center 8192)
			// spanning 16 semitones, 1 semitone = 8192/16 = 512 raw units, and since
			// channel->pan is in 1/256-semitone units, the conversion is
			// rawOffset = pan/256 * 512 = pan*2, i.e. a left shift of 1 (not 5 - that
			// earlier, incorrect shift applied pitch bends roughly 16x too deep).
			const s32 bend14 = clamp((imPanCenter << 7) + (channel->pan << 1), 0, 16383);
			ImSendPitchBend(data->channelId, bend14);
		}
	}

	void ImHandleChannelPitchBend(ImMidiPlayer* player, s32 channelIndex, s32 fractValue, s32 intValue)
	{
		ImMidiOutChannel* channel = &player->channels[channelIndex];
		s32 pitchBend = ((intValue << 7) | fractValue) - (imPanCenter << 7);
		if (channel->outChannelCount)
		{
			// Fix: this used to write to channel->pan (a completely different, unrelated field -
			// see ImHandleChannelDetuneChange() below), so this reset/update never actually took
			// effect. channel->pitchBend is what ImHandleChannelDetuneChange() actually reads.
			channel->pitchBend = (channel->outChannelCount * pitchBend) >> 5;	// range -256, 256
			ImHandleChannelDetuneChange(player, channel);
		}
	}

	///////////////////////////////////
	// Midi Call Functions
	///////////////////////////////////
	s32 ImMidiCall(s32 index, s32 id, s32 arg1, s32 arg2, s32 arg3)
	{
		// This is used to interface with the midi driver in DOS.
		// In TFE, midi commands are sent directly to the low-level midi system.
		return imSuccess;
	}

	//////////////////////////////////
	// Midi Advance Functions
	//////////////////////////////////
	void ImMidiJumpSustainOpen_NoteOff(ImMidiPlayer* player, u8 channelId, u8 arg1, u8 arg2)
	{
		const u8 instrumentId = arg1;
		const u32 channelMask = s_midiSustainChannelMask[channelId];
		if (s_curMidiInstrumentMask[instrumentId] & channelMask)
		{
			s_curMidiInstrumentMask[instrumentId] &= ~channelMask;
			s_curInstrumentCount--;
			if (s_curInstrumentCount == 0)
			{
				s_midiTrackEnd = 1;
			}
			ImSustainedSound* sound = s_imFreeSustainedSounds;
			if (!sound)
			{
				IM_LOG_ERR("%s", "su unable to alloc Sustain...");
				return;
			}
			IM_LIST_REM(s_imFreeSustainedSounds,   sound);
			IM_LIST_ADD(s_imActiveSustainedSounds, sound);

			sound->midiPlayer   = player;
			sound->instrumentId = instrumentId;
			sound->channelId    = channelId;
			sound->curTick      = s_midiTickDelta;
			sound->curTickFixed = 0;
		}
	}

	void ImMidiJumpSustainOpen_NoteOn(ImMidiPlayer* player, u8 channelId, u8 arg1, u8 arg2)
	{
		const u8 instrumentId = arg1;
		const u8 velocity = arg2;
		if (velocity == 0)
		{
			ImMidiJumpSustainOpen_NoteOff(player, channelId, instrumentId, 0);
		}
	}

	void ImMidiJumpSustain_NoteOn(ImMidiPlayer* player, u8 channelId, u8 arg1, u8 arg2)
	{
		const s32 instrumentId = arg1;
		const u32 channelMask  = s_midiSustainChannelMask[channelId];
		if (s_midiTickDelta > s_trackTicksRemaining)
		{
			s_midiTrackEnd = 1;
		}
		else if (s_curMidiInstrumentMask[instrumentId] & channelMask)
		{
			s_curMidiInstrumentMask[instrumentId] &= ~channelMask;
			ImSustainedSound* sustainedSound = s_imActiveSustainedSounds;
			while (sustainedSound)
			{
				s32 delta = s_midiTickDelta - 10;
				if (sustainedSound->midiPlayer == player && sustainedSound->instrumentId == instrumentId && sustainedSound->channelId == channelId &&
					delta <= sustainedSound->curTick)
				{
					sustainedSound->curTick = delta;
					break;
				}
				sustainedSound = sustainedSound->next;
			}
		}
	}

	void ImMidiStopAllNotes(ImMidiPlayer* player)
	{
		for (s32 i = 0; i < MIDI_CHANNEL_COUNT; i++)
		{
			ImMidiCommand(player, i, MID_ALL_NOTES_OFF, 0);
		}
	}

	void ImMidiCommand(ImMidiPlayer* player, s32 channelIndex, s32 midiCmd, s32 value)
	{
		ImMidiOutChannel* channel = &player->channels[channelIndex];

		if (midiCmd == MID_MODULATIONWHEEL_MSB)
		{
			channel->modulation = value;
			if (channel->data)
			{
				ImMidiChannelSetModulation(channel->data, value);
			}
		}
		else if (midiCmd == MID_VOLUME_MSB)
		{
			channel->partVolume = value;
			ImHandleChannelVolumeChange(player, channel);
		}
		else if (midiCmd == MID_PAN_MSB)
		{
			channel->partPan = value;
			if (channel->data)
			{
				s32 pan = clamp(value + player->pan - imPanCenter, 0, imPanMax);
				ImMidiChannelSetPan(channel->data, pan);
			}
		}
		else if (midiCmd == MID_GPC1_MSB)
		{
			if (value < 12)
			{
				//channel->outChannelCount = value;
				ImMidiPitchBend(player, channelIndex, 0, imPanCenter);
			}
		}
		else if (midiCmd == MID_GPC2_MSB)
		{
			channel->partNoteReq = value;
			if (channel->data)
			{
				ImMidiChannelSetPartNoteReq(channel->data, value);
			}
		}
		else if (midiCmd == MID_GPC3_MSB)
		{
			channel->partPriority = value;
			ImHandleChannelPriorityChange(player, channel);
			ImMidiSetupParts();
		}
		else if (midiCmd == MID_SUSTAIN_SWITCH)
		{
			channel->sustain = value;
			if (channel->data)
			{
				ImSetChannelSustain(channel->data, value);
			}
		}
		else if (midiCmd == MID_ALL_NOTES_OFF)
		{
			channel->sustain = 0;
			if (channel->data)
			{
				ImFreeMidiChannel(channel->data);
			}
		}
		else
		{
			// FIX: forward any Control Change TFE doesn't need to intercept for its own
			// internal bookkeeping (e.g. Expression #11, Reverb Send #91, Chorus Send #93,
			// Bank Select #0/#32, Portamento/Sostenuto/Soft Pedal switches, and any other
			// standard GM/GS controller) unmodified to the device, instead of silently
			// dropping it. This is the same class of bug as the pitch bend routing fix above
			// (see ImHandleChannelDetuneChange): this if/else chain had no fallback case, so
			// any controller number not explicitly listed here never reached any backend at
			// all - matching reports that general GM effects (reverb, chorus, etc.) didn't
			// seem to work. MID_GPC1_MSB/MID_GPC2_MSB/MID_GPC3_MSB are deliberately excluded
			// from this fallback - they're repurposed by iMuse itself as internal, in-band
			// signaling (see MID_GPC1_MSB above triggering a pitch bend reset, not anything
			// resembling a real "General Purpose Controller") and must stay intercepted, not
			// forwarded as literal CC16/17/18 values.
			ImControlChange(channelIndex, (MidiController)midiCmd, value);
		}
	}

	void ImMidiNoteOff(ImMidiPlayer* player, u8 channelId, u8 arg1, u8 arg2)
	{
		s32 instrumentId = arg1;
		ImMidiOutChannel* channel = &player->channels[channelId];
		if (channel->partStatus)
		{
			if (channel->data)
			{
				ImHandleNoteOff(channel->data, instrumentId);
			}
		}
		else
		{
			ImNoteOff(imOutDrumChannel, instrumentId);
		}
	}

	void ImMidiNoteOn(ImMidiPlayer* player, u8 channelId, u8 arg1, u8 arg2)
	{
		s32 instrumentId = arg1;
		s32 velocity = arg2;
		ImMidiOutChannel* channel = &player->channels[channelId];
		if (!velocity)
		{
			ImMidiNoteOff(player, channelId, instrumentId, 0);
		}
		else if (channel->partStatus)
		{
			if (channel->data)
			{
				ImHandleNoteOn(channel->data, instrumentId, velocity);
			}
		}
		else
		{
			ImMidi_DrumOut_NoteOn(channel->priority, channel->partNoteReq, channel->groupVolume, instrumentId, velocity);
		}
	}
			
	void ImMidiProgramChange(ImMidiPlayer* player, u8 channelId, u8 arg1, u8 arg2)
	{
		const s32 pgm = arg1;
		ImMidiOutChannel* channel = &player->channels[channelId];
		channel->partPgm = pgm;
		if (!channel->partStatus)
		{
			channel->partStatus = 1;
			ImMidiSetupParts();
		}
		else if (channel->data)
		{
			ImMidiChannelSetPgm(channel->data, pgm);
		}
		// Note: pitch bend range and the rest of a channel's GM/GS "reset" state
		// (Bank Select, Reverb/Chorus Send, Sustain, All Notes Off, ...) are NOT
		// (re-)sent here. A raw MIDI capture of the original engine confirmed that
		// full reset sequence is sent exactly once, when a channel is first assigned
		// to a part (see ImAssignMidiChannel()) - a later Program Change on an
		// already-assigned channel (e.g. a song switching instruments mid-track) only
		// ever sends the bare Program Change, nothing else.
	}

	void ImMidiPitchBend(ImMidiPlayer* player, u8 channelId, u8 arg1, u8 arg2)
	{
		const s32 pitchBendFract = arg1;
		const s32 pitchBendInt = arg2;

		ImMidiOutChannel* channel = &player->channels[channelId];
		s32 pitchBend = ((pitchBendInt << 7) | pitchBendFract) - (imPanCenter << 7);
		s32 channelCount = channel->outChannelCount;

		// There should always be channels.
		if (channelCount)
		{
			channel->pitchBend = (pitchBend * channelCount) >> 5;
			ImHandleChannelDetuneChange(player, channel);
		}
	}

	void ImMidiEvent(ImPlayerData* playerData, u8* chunkData)
	{
		u8* data = chunkData;
		MetaType metaType = MetaType(*data);
		// Skip past the metaType and length.
		data += 2;

		if (metaType == META_END_OF_TRACK)
		{
			s_imEndOfTrack = 1;
		}
		else if (metaType == META_TEMPO)
		{
			u32 tempo = (data[0] << 16) | (data[1] << 8) | data[2];
			ImSetTempo(playerData, tempo);
		}
		else if (metaType == META_TIME_SIG)
		{
			ImSetMidiTicksPerBeat(playerData, 960 >> (data[1] - 1), data[0]);
		}
	}
				
	void ImHandleNoteOn(ImMidiChannel* channel, s32 instrumentId, s32 velocity)
	{
		if (!channel) { return; }

		u32 channelMask = c_channelMask[channel->channelId];
		if (channel->instrumentMask[instrumentId] & channelMask)
		{
			ImNoteOff(channel->channelId, instrumentId);
		}
		else
		{
			if (channel->instrumentMask2[instrumentId] & channelMask)
			{
				channel->instrumentMask2[instrumentId] &= ~channelMask;
				ImNoteOff(channel->channelId, instrumentId);
			}
		}

		channel->instrumentMask[instrumentId] |= channelMask;
		ImNoteOn(channel->channelId, instrumentId, velocity);
	}

	void ImMidi_DrumOut_NoteOn(s32 priority, s32 partNoteReq, s32 volume, s32 instrumentId, s32 velocity)
	{
		if (s_ImDrumOutCh_priority != priority)
		{
			s_ImDrumOutCh_priority = priority;
			ImControlChange(imOutDrumChannel, MID_GPC3_MSB, priority);
		}
		if (s_ImDrumOutCh_partNoteReq != partNoteReq)
		{
			s_ImDrumOutCh_partNoteReq = partNoteReq;
			ImControlChange(imOutDrumChannel, MID_GPC2_MSB, partNoteReq);
		}
		if (s_ImDrumOutCh_volume != volume)
		{
			s_ImDrumOutCh_volume = volume;
			ImControlChange(imOutDrumChannel, MID_VOLUME_MSB, volume);
		}
		ImNoteOn(imOutDrumChannel, instrumentId, velocity);
	}

	void ImHandleNoteOff(ImMidiChannel* midiChannel, s32 instrumentId)
	{
		if (midiChannel)
		{
			const u32 channelMask = c_channelMask[midiChannel->channelId];
			if (midiChannel->instrumentMask[instrumentId] & channelMask)
			{
				midiChannel->instrumentMask[instrumentId] &= ~channelMask;
				if (!midiChannel->sustain)
				{
					ImNoteOff(midiChannel->channelId, instrumentId);
				}
			}
		}
	}

	///////////////////////////////////////////////////////////
	// Initialization Functions
	///////////////////////////////////////////////////////////
	s32 ImSetupFilesModule(iMuseInitData* initData)
	{
		IM_LOG_MSG("FILES module...");
		s_imFilesData = initData;
		return s_imFilesData ? imSuccess : imFail;
	}

	s32 ImInitializeGroupVolume()
	{
		for (s32 i = 0; i < MIDI_CHANNEL_COUNT; i++)
		{
			s_groupVolume[i] = imMaxVolume;
			s_soundGroupVolume[i] = imMaxVolume;
		}
		return imSuccess;
	}

	// TFE: Used to reinitialize Midi when devices are changed.
	s32 ImReintializeMidi()
	{
		for (s32 i = 0; i < MIDI_INSTRUMENT_COUNT; i++)
		{
			s_midiInstrumentChannelMask[i] = 0;
			s_midiInstrumentChannelMaskShared[i] = 0;
			s_midiInstrumentChannelMask1[i] = 0;
			s_midiInstrumentChannelMask3[i] = 0;
		}
		for (s32 i = 0; i < MIDI_CHANNEL_COUNT - 1; i++)
		{
			ImMidiChannel* channel = &s_midiChannels[i];
			channel->player = nullptr;
			channel->channel = nullptr;

			// There are two channels embedded in the structure that point to each other.
			channel->sharedMidiChannel = (ImMidiChannel*)&channel->sharedPart;
			// Initialize values.
			channel->channelId = (i < imOutDrumChannel) ? i : i + 1;
			channel->pgm = 0;
			channel->priority = 0;
			channel->noteReq = 0;
			channel->volume = imMaxVolume;
			channel->pan = imPanCenter;
			channel->modulation = 0;
			channel->finalPan = 0;
			channel->sustain = 0;
			channel->instrumentMask = s_midiInstrumentChannelMask;
			channel->instrumentMask2 = s_midiInstrumentChannelMask1;

			ImMidiChannel* sharedChannel = (ImMidiChannel*)&channel->sharedPart;
			sharedChannel->player = nullptr;
			sharedChannel->channel = nullptr;
			sharedChannel->sharedMidiChannel = channel;
			sharedChannel->channelId = (i < imOutDrumChannel) ? i : i + 1;
			sharedChannel->pgm = 0;
			sharedChannel->priority = 0;
			sharedChannel->noteReq = 0;
			sharedChannel->volume = imMaxVolume;
			sharedChannel->pan = imPanCenter;
			sharedChannel->modulation = 0;
			sharedChannel->finalPan = 0;
			sharedChannel->sustain = 0;
			sharedChannel->instrumentMask = s_midiInstrumentChannelMaskShared;
			sharedChannel->instrumentMask2 = s_midiInstrumentChannelMask3;

			ImProgramChange(sharedChannel->channelId, sharedChannel->pgm);
			ImControlChange(sharedChannel->channelId, MID_GPC3_MSB, sharedChannel->priority);
			ImControlChange(sharedChannel->channelId, MID_GPC2_MSB, sharedChannel->noteReq);
			ImControlChange(sharedChannel->channelId, MID_VOLUME_MSB, sharedChannel->volume);
			ImControlChange(sharedChannel->channelId, MID_PAN_MSB, sharedChannel->pan);
			ImControlChange(sharedChannel->channelId, MID_MODULATIONWHEEL_MSB, sharedChannel->modulation);
			ImSetPanFine(sharedChannel->channelId, sharedChannel->finalPan * 2 + 8192);
		}
		// Channel 15 maps to 9 and only uses 3 parameters.
		s_ImDrumOutCh_priority = 0;
		s_ImDrumOutCh_partNoteReq = 1;
		s_ImDrumOutCh_volume = imMaxVolume;
		ImControlChange(imOutDrumChannel, MID_GPC3_MSB, s_ImDrumOutCh_priority);
		ImControlChange(imOutDrumChannel, MID_GPC2_MSB, s_ImDrumOutCh_partNoteReq);
		ImControlChange(imOutDrumChannel, MID_VOLUME_MSB, s_ImDrumOutCh_volume);
		return imSuccess;
	}

	s32 ImInitializeSlots(iMuseInitData* initData)
	{
		IM_LOG_MSG("SLOTS module...");

		ImMidiCall(0, 0, 0, 0, 0);
		s_midiDriverNotReady = 0;
		return ImReintializeMidi();
	}

	s32 ImInitializeSustain()
	{
		IM_LOG_MSG("SUSTAIN module...");
		s_imFreeSustainedSounds = nullptr;
		s_imActiveSustainedSounds = nullptr;

		ImSustainedSound* sound = s_sustainedSounds;
		for (s32 i = 0; i < 24; i++, sound++)
		{
			sound->prev = nullptr;
			sound->next = nullptr;
			IM_LIST_ADD(s_imFreeSustainedSounds, sound);
		}
		return imSuccess;
	}

	s32 ImInitializePlayers()
	{
		IM_LOG_MSG("PLAYERS module...");
		ImMidiPlayer* player = s_midiPlayers;
		s_midiPlayerList = nullptr;
		s_soundList = nullptr;
		for (s32 i = 0; i < IM_MIDI_PLAYER_COUNT; i++, player++)
		{
			player->prev = nullptr;
			player->next = nullptr;

			ImPlayerData* data = &s_playerData[i];
			player->data = data;
			data->player = player;
			player->soundId = 0;
		}
		return imSuccess;
	}

	s32 ImInitializeMidiEngine(iMuseInitData* initData)
	{
		IM_LOG_MSG("MIDI engine....");
		if (ImInitializeSlots(initData) != imSuccess)
		{
			IM_LOG_ERR("%s", "SL: MIDI driver init failed...");
			return imFail;
		}
		if (ImInitializeSustain() != imSuccess)
		{
			IM_LOG_ERR("%s", "SL: MIDI driver init failed...");
			return imFail;
		}
		if (ImInitializePlayers() != imSuccess)
		{
			IM_LOG_ERR("%s", "SL: MIDI driver init failed...");
			return imFail;
		}

		s_midiPaused = 0;
		s_midiLock = 0;
		return imSuccess;
	}
	
	s32 ImInitializeInterrupt(iMuseInitData* initData)
	{
		// This function has been simplified compared to DOS, since TFE does not use
		// interrupt handlers.
		if (initData->imuseIntUsecCount)
		{
			s_iMuseTimestepMicrosec = initData->imuseIntUsecCount;
		}

		// In the original code, the interrupt is setup here, TFE uses a thread to simulate this.
		const f64 timeStep = TFE_System::microsecondsToSeconds((f64)s_iMuseTimestepMicrosec) / TFE_System::c_gameTimeScale;
		TFE_MidiPlayer::midiSetCallback(ImUpdate, timeStep);
		TFE_MidiPlayer::setMaximumNoteLength(8.0f);

		return imSuccess;
	}
}
