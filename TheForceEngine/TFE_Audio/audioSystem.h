#pragma once
#include <TFE_System/types.h>
#include <TFE_FileSystem/paths.h>
#include "audioOutput.h"
#include "audioFilters.h"

// Source data type. The sound data will be converted to float time during mixing.
enum SoundDataType
{
	SOUND_DATA_8BIT = 0,
	SOUND_DATA_16BIT,
	SOUND_DATA_FLOAT,
};

// Optional sound buffer flags, not used by the audio system directly but may be set by the assets - 
// VOC files have loops points embedded for example.
enum SoundBufferFlags
{
	SBUFFER_FLAG_NONE = 0,
	SBUFFER_FLAG_LOOPING = (1 << 0),
};

struct SoundBuffer
{
	SoundDataType type;
	u32 id = 0;			// Used by the asset system.
	u32 flags;
	u32 size;
	u32 sampleRate;
	u32 loopStart;
	u32 loopEnd;

	u8* data;
};

struct SoundSource;
// Type of sound effect, used for sources.
// This determines how source simulation is handled.
enum SoundType
{
	SOUND_2D = 0,	// Mono 2D sound effect.
	SOUND_3D,		// 3D positional sound effect.
};

#define MONO_SEPERATION 0.5f
#define MAX_SOUND_SOURCES 128

typedef void(*SoundFinishedCallback)(void* userData, s32 arg);
typedef void(*AudioThreadCallback)(f32* buffer, u32 bufferSize, f32 systemVolume);
// Direct callback writes stereo interleaved f32 at the full output rate (44100 Hz).
// frameCount = number of stereo frames to fill.
typedef void(*AudioDirectCallback)(f32* buffer, u32 frameCount, f32 systemVolume);

namespace TFE_Audio
{
	// constants
	// Attenuation factors. Sounds closer than c_closeDistance are played at full volume and sounds farther than c_clipDistance are clipped.
	// The system smoothly interpolates between the extremes.
	static const f32 c_closeDistance = 20.0f;
	static const f32 c_clipDistance = 140.0f;

	// functions
	bool init(bool useNullDevice = false, s32 outputId = -1);
	void shutdown();
	void stopAllSounds();
	void selectDevice(s32 id);

	void setUpsampleFilter(AudioUpsampleFilter filter = AUF_DEFAULT);
	AudioUpsampleFilter getUpsampleFilter();

	void setVolume(f32 volume);
	f32  getVolume();
	void pause();
	void resume();

	void lock();
	void unlock();

	void bufferedAudioClear();

	void setAudioThreadCallback(AudioThreadCallback callback = nullptr);
	void setDirectCallback(AudioDirectCallback callback = nullptr);
	const OutputDeviceInfo* getOutputDeviceList(s32& count, s32& curOutput);

	// One shot, play and forget. Only do this if the client needs no control until stopAllSounds() is called.
	// Note that looping one shots are valid though may generate too many sound sources if not used carefully.
	bool playOneShot(SoundType type, f32 volume, const SoundBuffer* buffer, bool looping,
		SoundFinishedCallback finishedCallback = nullptr, void* cbUserData = nullptr, s32 cbArg = 0);

	// Sound source that the client holds onto.
	SoundSource* createSoundSource(SoundType type, f32 volume, const SoundBuffer* buffer, SoundFinishedCallback callback = nullptr, void* userData = nullptr);
	void playSource(SoundSource* source, bool looping = false);
	void stopSource(SoundSource* source);
	void freeSource(SoundSource* source);
	void setSourceVolume(SoundSource* source, f32 volume);
	// This will restart the sound and change the buffer.
	void setSourceBuffer(SoundSource* source, const SoundBuffer* buffer);

	bool isSourcePlaying(SoundSource* source);
	f32  getSourceVolume(SoundSource* source);
	s32  getSourceSlot(SoundSource* source);
	SoundSource* getSourceFromSlot(s32 slot);
}
