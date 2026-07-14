#pragma once
#include <TFE_System/types.h>
#include <string>
#include <map>

///////////////////////////////////////////
// TFE Externalised Music Overrides
//
// Lets a mod replace individual iMuse/GMD mission music tracks with
// looping Ogg Vorbis files. Declared in a "music.json" placed at the
// root of the mod zip, e.g.:
//
//   {
//       "tracks": {
//           "fight-01": { "file": "music/fight-01.ogg" },
//           "stalk-01": { "file": "music/stalk-01.ogg" },
//           "boss-05":  { "file": "music/boss-05.ogg" }
//       }
//   }
//
// Keys must match the GMD track names Dark Forces uses internally
// (see c_levelMusic in TFE_DarkForces/gameMusic.cpp - "fight-01",
// "stalk-01", "boss-05", etc). Values are the zip-relative path to
// the Vorbis file; that same string is used as the lookup key with
// TFE_Paths::getFilePath() once darkForcesMain.cpp has extracted and
// registered the file, mirroring how mod OGV cutscenes are resolved.
//
// Any state without an entry here keeps playing the stock GMD track
// through iMuse as normal, so overrides can be partial (e.g. only
// replace the fight track and leave stalk/boss alone).
///////////////////////////////////////////

namespace TFE_ExternalData
{
	void clearExternalMusic();
	void loadExternalMusic();
	void parseExternalMusic(char* data, bool fromMod);

	// Returns the zip-relative Vorbis file path registered for the
	// given track name (e.g. "fight-01"), or nullptr if that track
	// has no override and should keep using the GMD/iMuse path.
	const char* getMusicOverride(const char* trackName);
}
