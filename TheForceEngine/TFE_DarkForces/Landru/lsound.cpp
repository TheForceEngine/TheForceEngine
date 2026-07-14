#include <cstring>
#include <cstdlib>

#include "lsound.h"
#include <TFE_A11y/accessibility.h>
#include <TFE_Game/igame.h>
#include <TFE_Jedi/IMuse/imuse.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_System/system.h>
#include <TFE_Audio/oggDecode.h>
#include <TFE_ExternalData/soundExternal.h>

using namespace TFE_Jedi;

namespace TFE_DarkForces
{
	#define DEFAULT_PRIORITY 64
	static LSound* s_soundList = nullptr;

	void purgeAllSounds();
	void freeSoundList(LSound* sound);
	void initSound(LSound* sound);

	/////////////////////////////////////////////////////////
	// System
	/////////////////////////////////////////////////////////
	void lSoundInit()
	{
		s_soundList = nullptr;
	}

	void lSoundDestroy()
	{
		purgeAllSounds();
	}

	LSound* lSoundGetList()
	{
		return s_soundList;
	}

	LSound* lSoundAlloc(u8* data)
	{
		LSound* sound = (LSound*)game_alloc(sizeof(LSound));
		if (sound)
		{
			initSound(sound);
			sound->data = data;

			// Add the sound to the head of the list.
			if (s_soundList)
			{
				sound->next = s_soundList;
			}
			s_soundList = sound;
		}
		return sound;
	}
		
	LSound* lSoundLoad(const char* name, LSoundType soundType)
	{
		// Read the data from the file.
		u8* data = readVocFileData(name);

		// Then allocate the sound itself.
		LSound* sound = lSoundAlloc(data);
		setSoundName(sound, soundType, name);
		discardSoundData(sound);
		sound->type = soundType;

		return sound;
	}

	void lSoundFree(LSound* sound)
	{
		if (!sound) { return; }
		lSoundFreePlaying(sound, JTRUE);
	}

	void lSoundFreePlaying(LSound* sound, JBool stop)
	{
		if (isSoundKeep(sound) || isSoundKeepable(sound))
		{
			clearSoundKeep(sound);
		}
		else
		{
			LSound* curSound = s_soundList;
			LSound* lastSound = nullptr;

			while (curSound && curSound != sound)
			{
				lastSound = curSound;
				curSound = curSound->next;
			}

			if (curSound == sound)
			{
				if (lastSound)
				{
					lastSound->next = sound->next;
				}
				else
				{
					s_soundList = sound->next;
				}
			}

			if (stop)
			{
				stopSound(sound);
			}

			if (isDiscardSoundData(sound) && sound->data)
			{
				game_free(sound->data);
				sound->data = nullptr;
			}
			if (sound->varptr)
			{
				game_free(sound->varptr);
				sound->varptr = nullptr;
			}
			if (sound->varhdl)
			{
				game_free(sound->varhdl);
				sound->varhdl = nullptr;
			}
			game_free(sound);
		}
	}

	/////////////////////////////////////////////////////////
	// Game Sound <-> iMuse interface.
	/////////////////////////////////////////////////////////
	void startSfx(LSound* sound)
	{
		TFE_A11Y::onSoundPlay(sound->name, TFE_A11Y::CaptionEnv::CC_CUTSCENE);
		ImStartSfx((ImSoundId)sound, DEFAULT_PRIORITY);
	}

	void startSpeech(LSound* sound)
	{
		TFE_A11Y::onSoundPlay(sound->name, TFE_A11Y::CaptionEnv::CC_CUTSCENE);
		ImStartVoice((ImSoundId)sound, DEFAULT_PRIORITY);
	}

	void stopSound(LSound* sound)
	{
		ImStopSound((ImSoundId)sound);
	}

	void setSoundVolume(LSound* sound, s32 volume)
	{
		ImSetParam((ImSoundId)sound, soundVol, volume);
	}

	void setSoundFade(LSound* sound, s32 volume, s32 time)
	{
		ImFadeParam((ImSoundId)sound, soundVol, volume, time);
	}

	void setSoundPan(LSound* sound, s32 pan)
	{
		ImSetParam((ImSoundId)sound, soundPan, pan);
	}

	void setSoundPanFade(LSound* sound, s32 pan, s32 time)
	{
		ImFadeParam((ImSoundId)sound, soundPan, pan, time);
	}

	/////////////////////////////////////////////////////////
	// Game Sound General Interface
	/////////////////////////////////////////////////////////
	void copySoundData(LSound* dstSound, LSound* srcSound)
	{
		if (isDiscardSoundData(dstSound) && dstSound->data)
		{
			game_free(dstSound->data);
		}
		nonDiscardSoundData(dstSound);
		setSoundName(dstSound, srcSound->resType, srcSound->name);
		dstSound->data = srcSound->data;
		dstSound->id   = srcSound->id;
		dstSound->type = srcSound->type;
	}
		
	LSound* findSoundType(const char* name, u32 type)
	{
		LSound* sound = s_soundList;
		s32 found = 0;
		while (sound && !found)
		{
			if (type == sound->resType)
			{
				found = 1;
				s32 i = 0;
				for (; i < LS_NAME_LEN && found && name[i]; i++)
				{
					found = (sound->name[i] == tolower(name[i]));
				}
				if (found && i < LS_NAME_LEN && sound->name[i]) { found = 0; }
			}

			if (!found)
			{
				sound = sound->next;
			}
		}
		return sound;
	}

	void discardSoundData(LSound* sound)
	{
		sound->flags |= LS_SOUND_DISCARD;
	}

	void nonDiscardSoundData(LSound* sound)
	{
		sound->flags &= ~LS_SOUND_DISCARD;
	}

	JBool isDiscardSoundData(LSound* sound)
	{
		return sound->flags & LS_SOUND_DISCARD;
	}

	void setSoundKeep(LSound* sound)
	{
		sound->flags |= LS_SOUND_KEEP;
	}

	void clearSoundKeep(LSound* sound)
	{
		sound->flags &= ~LS_SOUND_KEEP;
	}

	JBool isSoundKeep(LSound* sound)
	{
		return sound->flags & LS_SOUND_KEEP;
	}

	void setSoundKeepable(LSound* sound)
	{
		sound->flags |= LS_SOUND_KEEPABLE;
	}

	void clearSoundKeepable(LSound* sound)
	{
		sound->flags &= ~LS_SOUND_KEEPABLE;
	}

	JBool isSoundKeepable(LSound* sound)
	{
		return sound->flags & LS_SOUND_KEEPABLE;
	}

	void setSoundName(LSound* sound, u32 type, const char* name)
	{
		sound->resType = type;

		s32 i = 0;
		for (; i < LS_NAME_LEN && name[i]; i++)
		{
			sound->name[i] = tolower(name[i]);
		}
		while (i < LS_NAME_LEN)
		{
			sound->name[i] = 0;
			i++;
		}
	}

	void freeSoundList(LSound* sound)
	{
		while (sound)
		{
			LSound* next = sound->next;
			lSoundFree(sound);
			sound = next;
		}
	}

	static const s32 c_imuseDigitalSoundRate = 11000;

	u8* wrapPcmAsVoc(const u8* pcm, u32 sampleCount, s32 sampleRate, u32* outSize)
	{
		static const u8 c_desc[20] = { 'C','r','e','a','t','i','v','e',' ','V','o','i','c','e',' ','F','i','l','e', 0x1A };
		const u32 headerSize = 26;
		const u32 blockHeaderSize = 6; // blocktype(1) + size(3) + sr(1) + pack(1)
		const u32 totalSize = headerSize + blockHeaderSize + sampleCount + 1; // +1 terminator block

		u8* voc = (u8*)game_alloc(totalSize);
		if (!voc) { return nullptr; }

		u8* w = voc;
		memcpy(w, c_desc, 20); w += 20;

		const u16 datablockOffset = (u16)headerSize;
		const u16 version = 0x010A;
		const u16 checksum = (u16)(~version + 0x1234);
		memcpy(w, &datablockOffset, 2); w += 2;
		memcpy(w, &version, 2); w += 2;
		memcpy(w, &checksum, 2); w += 2;

		*w++ = 1; // blocktype = VOC_SOUND_DATA
		const u32 blockLen = 2 + sampleCount; // sr + pack bytes + pcm data
		*w++ = (u8)(blockLen & 0xff);
		*w++ = (u8)((blockLen >> 8) & 0xff);
		*w++ = (u8)((blockLen >> 16) & 0xff);

		s32 sr = 256 - (1000000 / sampleRate);
		if (sr < 0) { sr = 0; }
		if (sr > 255) { sr = 255; }
		*w++ = (u8)sr;
		*w++ = 0; // pack = CODEC_8BITS

		memcpy(w, pcm, sampleCount); w += sampleCount;
		*w++ = 0; // VOC_TERMINATOR

		if (outSize) { *outSize = totalSize; }
		return voc;
	}

	u8* readVocFileData(const char* name, u32* sizeOut)
	{
		// Does a mod want to replace this sound with a looping-free,
		// one-shot Ogg Vorbis file? See TFE_ExternalData/soundExternal.h.
		const char* oggOverride = TFE_ExternalData::getSoundOverride(name);
		if (oggOverride)
		{
			FilePath oggPath;
			if (TFE_Paths::getFilePath(oggOverride, &oggPath))
			{
				u32 sampleCount = 0;
				u8* pcm = TFE_OggDecode::decodeToMono8(oggPath.path, c_imuseDigitalSoundRate, &sampleCount);
				if (pcm)
				{
					u32 vocSize = 0;
					u8* voc = wrapPcmAsVoc(pcm, sampleCount, c_imuseDigitalSoundRate, &vocSize);
					free(pcm);
					if (voc)
					{
						if (sizeOut) { *sizeOut = vocSize; }
						return voc;
					}
				}
				TFE_System::logWrite(LOG_ERROR, "Sound", "Failed to decode sound override '%s' for '%s' - falling back to the VOC.", oggOverride, name);
			}
			else
			{
				TFE_System::logWrite(LOG_ERROR, "Sound", "Cannot find sound override file '%s' for '%s' - falling back to the VOC.", oggOverride, name);
			}
		}

		FilePath path;
		if (strstr(name, ".voc") || strstr(name, ".VOC"))
		{
			if (!TFE_Paths::getFilePath(name, &path))
			{
				return nullptr;
			}
		}
		else
		{
			char fileName[TFE_MAX_PATH];
			sprintf(fileName, "%s.VOIC", name);	// Prefer the version of a sound from the LFD.
			if (!TFE_Paths::getFilePath(fileName, &path))
			{
				sprintf(fileName, "%s.VOC", name);
				if (!TFE_Paths::getFilePath(fileName, &path))
				{
					return nullptr;
				}
			}
		}
		FileStream file;
		if (!file.open(&path, Stream::MODE_READ))
		{
			return nullptr;
		}
		u32 size = (u32)file.getSize();
		u8* data = (u8*)game_alloc(size);
		if (!data)
		{
			return nullptr;
		}
		file.readBuffer(data, size);
		file.close();

		if (sizeOut)
		{
			*sizeOut = size;
		}

		return data;
	}

	/////////////////////////////////////////////////////////
	// System Internal
	/////////////////////////////////////////////////////////
	void purgeAllSounds()
	{
		LSound* sound = s_soundList;

		while (sound)
		{
			clearSoundKeep(sound);
			clearSoundKeepable(sound);
			sound = sound->next;
		}

		freeSoundList(s_soundList);
		s_soundList = nullptr;
	}

	void initSound(LSound* sound)
	{
		memset(sound, 0, sizeof(LSound));
		sound->volume = 128;
	}
}  // TFE_JediSound