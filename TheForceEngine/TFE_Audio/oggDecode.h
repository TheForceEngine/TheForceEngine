#pragma once
//////////////////////////////////////////////////////////////////////
// One-shot Ogg Vorbis decoder.
//
// Unlike TFE_OggMusicPlayer (streaming/looping, mixed live via the
// TFE_Audio direct callback), this decodes a whole file to completion
// in one call. Used to convert mod-supplied .ogg sound effects into
// the mono 8-bit PCM format Dark Forces' VOC sound effects use, so
// they can be dropped into the existing iMuse digital sound pipeline
// unmodified (see TFE_DarkForces/Landru/lsound.cpp - readVocFileData()
// and wrapPcmAsVoc()).
//
// Requires the same build configuration as TFE_OgvPlayer/TFE_OggMusicPlayer
// (libogg / libvorbis available, ENABLE_OGV_CUTSCENES defined), since it
// shares those libraries. Returns false/nullptr harmlessly otherwise.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>

namespace TFE_OggDecode
{
	// Fully decodes 'filepath' to mono, 8-bit unsigned PCM at
	// 'targetSampleRate' (down-mixing stereo by averaging channels and
	// resampling as needed). Returns a malloc'd buffer the caller must
	// free() - not the TFE_Jedi game_alloc allocator - and writes the
	// sample count to 'outSampleCount'. Returns nullptr if the file
	// can't be opened or isn't a valid Vorbis stream.
	u8* decodeToMono8(const char* filepath, s32 targetSampleRate, u32* outSampleCount);
}
