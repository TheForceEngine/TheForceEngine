#pragma once
#include <TFE_System/types.h>
#include <string>
#include <map>

///////////////////////////////////////////
// TFE Externalised Sound Overrides
//
// Lets a mod replace individual VOC sound effects with an Ogg Vorbis
// file. Declared in a "sounds.json" placed at the root of the mod zip,
// e.g.:
//
//   {
//       "sounds": {
//           "door.voc": "door.ogg",
//           "switch1.voc": "sfx/switch1.ogg"
//       }
//   }
//
// Keys are VOC names as Dark Forces requests them (case-insensitive,
// with or without the .voc extension - both "door.voc" and "door" are
// accepted and normalized the same way). Values are the zip-relative
// path to the Vorbis file.
//
// The override is applied in TFE_DarkForces/Landru/lsound.cpp -
// readVocFileData() decodes the Vorbis file and repackages it as an
// in-memory VOC buffer, so the rest of the sound pipeline (iMuse
// digital sound playback, positional cueing, priorities, looping,
// serialization) is unaffected - it just sees VOC bytes as usual.
///////////////////////////////////////////

namespace TFE_ExternalData
{
	void clearExternalSounds();
	void loadExternalSounds();
	void parseExternalSounds(char* data, bool fromMod);

	// Normalizes a requested sound name (strips any path/extension,
	// lowercases, re-appends ".voc") so lookups are consistent
	// regardless of how the name was requested.
	std::string normalizeSoundName(const char* name);

	// Returns the zip-relative Vorbis file path registered for the
	// given sound name, or nullptr if that sound has no override and
	// should be loaded from its real VOC file as normal.
	const char* getSoundOverride(const char* name);
}
