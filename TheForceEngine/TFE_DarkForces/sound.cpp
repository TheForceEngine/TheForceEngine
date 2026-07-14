#include <cstring>

#include "sound.h"
#include "player.h"
#include <TFE_A11y/accessibility.h>
#include <TFE_DarkForces/Landru/lsound.h>
#include <TFE_Settings/settings.h>
#include <TFE_Game/igame.h>
#include <TFE_Asset/vocAsset.h>
#include <TFE_Audio/audioSystem.h>
#include <TFE_Audio/midiPlayer.h>
#include <TFE_Audio/oggMusicPlayer.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/IMuse/imuse.h>
#include <TFE_Jedi/Memory/allocator.h>
#include <TFE_Jedi/Serialization/serialization.h>
#include <TFE_DarkForces/time.h>
#include <TFE_System/system.h>

namespace TFE_DarkForces
{
	struct GameSound
	{
		SoundEffectId id;
		u32 time;
		u8* data;
		u32 size;

		s32 volume;
		s32 refCount;

		s32 priority;
		s32 mark;
		char name[13];
	};
	struct SoundState
	{
		Allocator* gameSoundList = nullptr;
		s32 instance = 0;
		s32 soundLevelStart = -1;
	};

#define MAX_LEVEL_SOUNDS 300
#define CUE_RING1 FIXED(30)
#define CUE_RING2 FIXED(150)

	// SoundID is structed as:
	// <- Higher .... Lower ->
	// 1-bit: always zero (this is the midi flag bit) | 15-bits: instance | 48-bits: pointer offset
	static const s64 c_soundIdMask = 0xffffffffffffll;	// (1 << 48) - 1
	static const s64 c_soundInstanceShift = 48ll;
	static const s32 c_soundInstanceMask = 0x7fff;
	static const s32 s_tPan[32] = { 00,-06,-12,-18,-24,-30,-36,-42,-48,-42,-36,-30,-24,-18,-12,-06,00,06,12,18,24,30,36,42,48,42,36,30,24,18,12,06 };

	static SoundState sound_state = {};
	s32 s_lastMaintainVolume;

	SoundEffectId soundInstance(SoundSourceId soundId, s32 instance);
	GameSound* getSoundPtr(SoundSourceId id);
	void soundCalculateCue(GameSound* sound, fixed16_16 x, fixed16_16 y, fixed16_16 z, s32* vol, s32* pan);
	u8* sound_getResource(SoundEffectId id);
	void sound_alwaysFree(GameSound* sound);
	void sound_clearLevelSounds();

	// Called at game startup and shutdown.
	void sound_open(MemoryRegion* memRegion)
	{
		sound_state = {};
		sound_state.gameSoundList = allocator_create(sizeof(GameSound), s_gameRegion);
		ImInitialize(memRegion);

		TFE_Settings_Sound* sound = TFE_Settings::getSoundSettings();
		if (sound->use16Channels)
		{
			ImSetDigitalChannelCount(16);
		}
	}

	void sound_close()
	{
		sound_levelStop();
		allocator_free(sound_state.gameSoundList);
		ImTerminate();
		sound_state = {};
	}

	// Called at level startup and shutdown.
	void sound_levelStart()
	{
		TFE_Settings_Sound* soundSettings = TFE_Settings::getSoundSettings();
		TFE_Audio::setVolume(soundSettings->soundFxVolume * soundSettings->masterVolume);
		TFE_MidiPlayer::setVolume(soundSettings->musicVolume * soundSettings->masterVolume);
		TFE_OggMusicPlayer::setVolume(soundSettings->musicVolume * soundSettings->masterVolume);

		ImSetResourceCallback(sound_getResource);
		sound_state.instance = 1;

		// Unload any existing level sounds.
		sound_clearLevelSounds();
	}

	void sound_levelStop()
	{
		sound_stopAll();
		ImSetResourceCallback(nullptr);
	}

	s32 sound_getIndexFromId(SoundSourceId id)
	{
		if (!id) { return -1; }
		GameSound* sound = getSoundPtr(id);
		return allocator_getIndex(sound_state.gameSoundList, sound);
	}

	SoundSourceId sound_getSoundFromIndex(s32 index, bool refCount)
	{
		GameSound* sound = (GameSound*)allocator_getByIndex(sound_state.gameSoundList, index);
		if (sound)
		{
			if (refCount) { sound->refCount++; }
			return soundInstance((SoundSourceId)sound, 0);
		}
		return NULL_SOUND;
	}

	void sound_setLevelStart()
	{
		sound_state.soundLevelStart = allocator_getCount(sound_state.gameSoundList);
	}

	void sound_serializeLevelSounds(Stream* stream)
	{
		s32 count;
		if (serialization_getMode() == SMODE_WRITE)
		{
			count = max(0, allocator_getCount(sound_state.gameSoundList) - sound_state.soundLevelStart);
		}
		SERIALIZE(SaveVersionInit, count, 0);
		if (serialization_getMode() == SMODE_WRITE)
		{
			allocator_saveIter(sound_state.gameSoundList);
			allocator_setPos(sound_state.gameSoundList, sound_state.soundLevelStart);
			GameSound* sound = (GameSound*)allocator_getIter(sound_state.gameSoundList);
			while (sound)
			{
				u8 length = (u8)strlen(sound->name);
				SERIALIZE(SaveVersionInit, length, 0);
				SERIALIZE_BUF(SaveVersionInit, sound->name, length);
				SERIALIZE(SaveVersionInit, sound->priority, 64);
				sound = (GameSound*)allocator_getNext(sound_state.gameSoundList);
			}
			allocator_restoreIter(sound_state.gameSoundList);
		}
		else
		{
			sound_clearLevelSounds();
			for (s32 i = 0; i < count; i++)
			{
				u32 priority;
				u8 length;
				char name[32];
				SERIALIZE(SaveVersionInit, length, 0);
				SERIALIZE_BUF(SaveVersionInit, name, length);
				SERIALIZE(SaveVersionInit, priority, 64);
				name[length] = 0;
				sound_load(name, priority);
			}
		}
	}

	void sound_clearLevelSounds()
	{
		if (sound_state.soundLevelStart < 0) { return; }
		allocator_setPos(sound_state.gameSoundList, sound_state.soundLevelStart);
		GameSound* sound = (GameSound*)allocator_getIter(sound_state.gameSoundList);
		while (sound)
		{
			sound_alwaysFree(sound);
			sound = (GameSound*)allocator_getNext(sound_state.gameSoundList);
		}
	}

	SoundSourceId sound_load(const char* fileName, u32 priority)
	{
		SoundSourceId newId = NULL_SOUND;
		GameSound* sound = (GameSound*)allocator_getHead(sound_state.gameSoundList);
		while (sound)
		{
			if (!strcasecmp(fileName, sound->name))
			{
				sound->refCount++;
				return soundInstance((SoundSourceId)sound, 0);
			}
			sound = (GameSound*)allocator_getNext(sound_state.gameSoundList);
		}

		u32 size = 0;
		u8* data = readVocFileData(fileName, &size);
		if (data)
		{
			sound = (GameSound*)allocator_newItem(sound_state.gameSoundList);
			if (!sound)
				return NULL_SOUND;
			sound->id = (SoundSourceId)sound;
			sound->time = s_curTick;
			sound->data = data;
			strncpy(sound->name, fileName, 13);
			sound->priority = priority;
			sound->size = size;
			sound->volume = 127;
			sound->refCount = 1;
			newId = soundInstance(sound->id, 0);
		}

		return newId;
	}

	void sound_free(SoundSourceId id)
	{
		if (id)
		{
			GameSound* sound = getSoundPtr(id);
			sound->refCount--;

			if (sound->refCount == 0)
			{
				if (sound->data)
				{
					game_free(sound->data);
				}
				sound->data = nullptr;
				allocator_deleteItem(sound_state.gameSoundList, sound);
			}
		}
	}

	void sound_alwaysFree(GameSound* sound)
	{
		if (sound)
		{
			if (sound->data)
			{
				game_free(sound->data);
			}
			sound->data = nullptr;
			allocator_deleteItem(sound_state.gameSoundList, sound);
		}
	}

	void sound_setBaseVolume(SoundSourceId id, s32 volume)
	{
		GameSound* sound = getSoundPtr(id);
		if (sound)
		{
			sound->volume = volume;
		}
	}

	void sound_setVolume(SoundEffectId id, s32 volume)
	{
		if (id)
		{
			ImSetParam(id, soundVol, volume & 0x7f);
		}
	}

	void sound_setPan(SoundEffectId id, s32 pan)
	{
		if (id)
		{
			ImSetParam(id, soundPan, (pan + 64) & 0x7f);
		}
	}

	void sound_pitchShift(SoundEffectId soundId, s32 shift)
	{
		// no-op.
	}

	// TFE: added `withCaptions` argument, for non-cued sounds like player weaps and mission VO
	SoundEffectId sound_playPriority(SoundSourceId id, s32 priority, JBool withCaptions)
	{
		if (!id) { return 0; }

		GameSound* sound = getSoundPtr(id);
		SoundEffectId idInstance = soundInstance(sound->id, sound_state.instance++);

		if (!sound->data)
		{
			return 0;
		}

		if (priority < 0)
		{
			priority = sound->priority;
		}
		ImStartSfx(idInstance, priority);
		sound_setVolume(idInstance, sound->volume);
		if (withCaptions && TFE_A11Y::gameplayCaptionsEnabled())
		{
			TFE_A11Y::onSoundPlay(sound->name, TFE_A11Y::CaptionEnv::CC_GAMEPLAY);
		}

		return idInstance;
	}

	SoundEffectId sound_play(SoundSourceId id)
	{
		return sound_playPriority(id, -1, JTRUE);
	}

	// TFE
	SoundEffectId sound_play_noCaptions(SoundSourceId id)
	{
		return sound_playPriority(id, -1, JFALSE);
	}

	SoundEffectId sound_playCued(SoundSourceId id, vec3_fixed pos)
	{
		SoundEffectId idInstance = 0;
		if (id)
		{
			GameSound* sound = getSoundPtr(id);
			s32 vol, pan;
			soundCalculateCue(sound, pos.x, pos.y, pos.z, &vol, &pan);

			idInstance = sound_play_noCaptions(id); // TFE; captions for cued sounds come from soundCalculateCue
			sound_setVolume(idInstance, vol);
			sound_setPan(idInstance, pan);
		}
		return idInstance;
	}

	void sound_adjustCued(SoundEffectId id, vec3_fixed pos)
	{
		if (id)
		{
			s32 vol, pan;
			GameSound* sound = getSoundPtr(id);
			soundCalculateCue(sound, pos.x, pos.y, pos.z, &vol, &pan);
			sound_setVolume(id, vol);
			sound_setPan(id, pan);
		}
	}

	void sound_stop(SoundEffectId id)
	{
		if (id)
		{
			ImStopSound(id);
		}
	}

	void sound_stopAll()
	{
		ImStopAllSounds();
	}

	SoundEffectId sound_maintain(SoundEffectId idInstance, SoundSourceId idSound, vec3_fixed pos)
	{
		if (idSound)
		{
			s32 vol, pan;
			GameSound* sound = getSoundPtr(idSound);
			soundCalculateCue(sound, pos.x, pos.y, pos.z, &vol, &pan);
			s_lastMaintainVolume = vol;

			if (vol)
			{
				// Check to see if the sound is still playing at specific intervals.
				if (idInstance && !(s_curTick & 0x0f))
				{
					if (ImGetParam(idInstance, soundPlayCount) == 0)
					{
						sound_stop(idInstance);
						idInstance = 0;
					}
				}

				if (!idInstance)
				{
					idInstance = sound_play(idSound);
				}
				sound_setVolume(idInstance, vol);
				sound_setPan(idInstance, pan);
			}
			else if (idInstance)
			{
				sound_stop(idInstance);
				idInstance = 0;
			}
			return idInstance;
		}
		return 0;
	}

	////////////////////////////////////////////////////////////////////
	// Internal
	////////////////////////////////////////////////////////////////////
	void soundCalculateCue(GameSound* sound, fixed16_16 x, fixed16_16 y, fixed16_16 z, s32* vol, s32* pan)
	{
		fixed16_16 dist = TFE_Jedi::abs(s_eyePos.y - y) + distApprox(x, z, s_eyePos.x, s_eyePos.z);

		// Calculate the volume.
		s32 volume = 0;
		if (dist < CUE_RING2)
		{
			if (dist < CUE_RING1)
			{
				// The sound is within the full volume radius.
				volume = sound->volume;
			}
			else
			{
				// The sound will be attenuated based on distance.
				fixed16_16 ratio = div16(CUE_RING2 - dist, CUE_RING2 - CUE_RING1);
				assert(ratio >= 0 && ratio <= ONE_16);
				volume = floor16(mul16(ratio, intToFixed16(sound->volume)));
				volume = min(127, volume);
			}
		}
		*vol = volume;

		// Calculate the pan.
		angle14_32 angle = vec2ToAngle(x - s_eyePos.x, z - s_eyePos.z);
		angle = getAngleDifference(s_eyeYaw, angle);
		*pan = s_tPan[((angle + 8192) / 512) & 31];

		// TFE subtitles/captions
		if (TFE_A11Y::gameplayCaptionsEnabled() && *vol > TFE_Settings::getA11ySettings()->gameplayCaptionMinVolume)
		{
			TFE_A11Y::onSoundPlay(sound->name, TFE_A11Y::CaptionEnv::CC_GAMEPLAY);
		}
	}

	SoundEffectId soundInstance(SoundSourceId soundId, s32 instance)
	{
		SoundEffectId id = soundId;
		assert(id >= 0 && id <= c_soundIdMask);
		id |= s64(instance & c_soundInstanceMask) << c_soundInstanceShift;
		return id;
	}

	GameSound* getSoundPtr(SoundSourceId id)
	{
		assert((id & c_soundIdMask) >= 0);
		return (GameSound*)((u8*)(id & c_soundIdMask));
	}

	u8* sound_getResource(SoundEffectId id)
	{
		GameSound* sound = getSoundPtr(id);
		return sound ? sound->data : nullptr;
	}
}  // TFE_DarkForces