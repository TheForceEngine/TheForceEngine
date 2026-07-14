#include "gameMusic.h"
#include "util.h"
#include "random.h"
#include <TFE_Settings/settings.h>
#include <TFE_Jedi/IMuse/imuse.h>
#include <TFE_Game/igame.h>
#include <TFE_System/system.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_System/parser.h>
#include <TFE_Audio/oggMusicPlayer.h>
#include <TFE_ExternalData/musicExternal.h>
#include <cstring>

#define SHOW_GAME_MUSIC_MSG 0

#if SHOW_GAME_MUSIC_MSG
#define GMUSIC_MSG(fmt, ...) TFE_System::logWrite(LOG_MSG, "Music", fmt, __VA_ARGS__)
#else
#define GMUSIC_MSG(fmt, ...)
#endif

namespace TFE_DarkForces
{
	// Internally, iMuse stores opcodes as integers (ptrdiff_t in TFE, so it is big enough to hold a pointer).
	// So this macro is a nice way of signifying the intent in the code.
	#define MUSIC_CALLBACK(c) std::ptrdiff_t(c)
	#define GAME_INVALID_TRIGGER 16

	enum MusicConstants
	{
		MUS_LEVEL_COUNT = 14,
		MUS_STATE_COUNT = 5,
		MUS_FIGHT_TIME  = TICKS(3),
	};

	static const char c_levelMusic[MUS_LEVEL_COUNT][MUS_STATE_COUNT][10] =
	{
		// LEVEL 1
		{
			"",
			"fight-01",
			"",
			"stalk-01",
			"",
		},
		// LEVEL 2
		{
			"",
			"fight-02",
			"",
			"stalk-02",
			"",
		},
		// LEVEL 3
		{
			"",
			"fight-03",
			"",
			"stalk-03",
			"",
		},
		// LEVEL 4
		{
			"",
			"fight-04",
			"",
			"stalk-04",
			"",
		},
		// LEVEL 5
		{
			"boss-05",
			"fight-05",
			"",
			"stalk-05",
			"",
		},
		// LEVEL 6
		{
			"",
			"fight-06",
			"",
			"stalk-06",
			"",
		},
		// LEVEL 7
		{
			"",
			"fight-07",
			"",
			"stalk-07",
			"",
		},
		// LEVEL 8
		{
			"boss-08",
			"fight-08",
			"",
			"stalk-08",
			"",
		},
		// LEVEL 9
		{
			"",
			"fight-09",
			"",
			"stalk-09",
			"",
		},
		// LEVEL 10
		{
			"boss-10",
			"fight-10",
			"",
			"stalk-10",
			"",
		},
		// LEVEL 11
		{
			"boss-11",
			"fight-11",
			"",
			"stalk-11",
			"",
		},
		// LEVEL 12
		{
			"",
			"fight-12",
			"",
			"stalk-12",
			"",
		},
		// LEVEL 13
		{
			"",
			"fight-13",
			"",
			"stalk-13",
			"",
		},
		// LEVEL 14
		{
			"boss-14",
			"fight-14",
			"",
			"stalk-14",
			"",
		},
	};

	MusicState s_currentState = MUS_STATE_NULLSTATE;
	MusicState s_oldState = MUS_STATE_NULLSTATE;
	static s32 s_stateEntrances[MUS_STATE_UNDEFINED] = { 0 };
	static s32 s_currentLevel = 0;
	static ImSoundId s_oldSong = IM_NULL_SOUNDID;
	static ImSoundId s_newSong = IM_NULL_SOUNDID;
	static s32 s_transChunk = 0;
	static u32 s_savedMeasure = 0;
	static u32 s_savedBeat = 0;
	static Task* s_musicTask = nullptr;
	static JBool s_desiredFightState = JFALSE;
	static bool s_oggOverrideActive = false;
	
	s32  gameMusic_setLevel(s32 level);
	s32  gameMusic_random(s32 low, s32 high);
	void gameMusic_taskFunc(MessageType msg);
	void iMuseCallback1(char* marker);
	void iMuseCallback2(char* marker);
	void iMuseCallback3(char* marker);

	s32   readNumber(char** ptrptr);
	char* parseEvent(char* ptr);

	void gameMusic_start(s32 level)
	{
		memset(s_stateEntrances, 0, MUS_STATE_UNDEFINED * sizeof(s32));
		s_musicTask = createSubTask("iMuse", gameMusic_taskFunc);

		gameMusic_setLevel(level);
		gameMusic_setState(MUS_STATE_STALK);
		s_desiredFightState = JFALSE;
	}

	void gameMusic_stop()
	{
		gameMusic_setState(MUS_STATE_NULLSTATE);
		TFE_OggMusicPlayer::stop();
		s_oggOverrideActive = false;
		s_musicTask = nullptr;
		s_currentLevel = -1;
	}

	s32 gameMusic_setLevel(s32 level)
	{
		s_oldSong = IM_NULL_SOUNDID;

		// In the original code, there are two values - a 'number' and 'value'
		// But the only number that is used is 0, which is the current level.
		// No other "attribute" is supported.
		if (level <= MUS_LEVEL_COUNT)
		{
			s32 oldLevel = s_currentLevel;
			s_currentLevel = level;

			if (s_currentLevel != oldLevel)
			{
				ImStopAllSounds();
				ImUnloadAll();
				TFE_OggMusicPlayer::stop();
				s_oggOverrideActive = false;
				s_currentState = MUS_STATE_NULLSTATE;
				if (s_currentLevel)
				{
					for (s32 i = 1; i < MUS_STATE_UNDEFINED; i++)
					{
						if (c_levelMusic[s_currentLevel - 1][i - 1][0])
						{
							ImLoadMidi(c_levelMusic[s_currentLevel-1][i-1]);
						}
					}
				}
			}
			return s_currentLevel;
		}
		return 0;
	}
		
	void gameMusic_setState(MusicState state)
	{
		ImPause();

		if (s_currentLevel && state < MUS_STATE_UNDEFINED && state != s_currentState)
		{
			if (state == MUS_STATE_NULLSTATE)
			{
				s_currentState = state;
				ImStopAllSounds();
				TFE_OggMusicPlayer::stop();
				s_oggOverrideActive = false;
			}
			else
			{
				// Does this state have a mod-supplied looping Vorbis override?
				// Keyed on the same track name iMuse would otherwise load
				// (e.g. "fight-01") - see TFE_ExternalData/musicExternal.h.
				const char* trackName = c_levelMusic[s_currentLevel - 1][state - 1];
				const char* oggTrack = trackName[0] ? TFE_ExternalData::getMusicOverride(trackName) : nullptr;
				FilePath resolvedOgg;
				bool haveOggOverride = oggTrack && TFE_Paths::getFilePath(oggTrack, &resolvedOgg);

				if (haveOggOverride)
				{
					// Halt iMuse audio.
					ImStopAllSounds();
					TFE_OggMusicPlayer::play(resolvedOgg.path, true);
					s_oggOverrideActive = true;

					s_oldState = s_currentState;
					s_currentState = state;
				}
				else if (ImFindMidi(trackName))
				{
					if (s_oggOverrideActive)
					{
						// Coming from an override track - there's no iMuse
						// transition state to resume from, so start the GMD
						// track fresh, same as the initial-state case below.
						TFE_OggMusicPlayer::stop();
						s_oggOverrideActive = false;
						ImStartSound(ImFindMidi(trackName), 64);
					}
					else if (s_currentState)
					{
						// If there is no trigger set yet, set callback 1, otherwise we keep the current trigger.
						if (!ImCheckTrigger(-1, -1, -1))
						{
							s_oldSong = ImFindMidi(c_levelMusic[s_currentLevel - 1][s_currentState - 1]);
							ImSetTrigger(s_oldSong, 0, MUSIC_CALLBACK(iMuseCallback1));
						}
					}
					else
					{
						// Start the sound from scratch since this is the initial state.
						ImStartSound(ImFindMidi(trackName), 64);
					}

					s_oldState = s_currentState;
					s_currentState = state;
				}
				else
				{
					TFE_System::logWrite(LOG_ERROR, "Music", "Cannot start music.");
				}
			}
		}

		ImResume();
	}

	void gameMusic_update()
	{
		TFE_OggMusicPlayer::update();
	}

	MusicState gameMusic_getState()
	{
		return s_currentState;
	}
				
	void gameMusic_startFight()
	{
		static s32 fightTimer = 0;
		// Allow the user to disable fight music transitions.
		// If this is changed while in the fight state or already transitioning, then nothing will
		// happen until the next available transition point.
		TFE_Settings_Game* gameSettings = TFE_Settings::getGameSettings();
		if (gameSettings->df_disableFightMusic)
		{
			s_desiredFightState = JFALSE;
			fightTimer = 0;
			if (gameMusic_getState() != MUS_STATE_STALK)
			{
				gameMusic_setState(MUS_STATE_STALK);
			}
			return;
		}

		if (s_curTick - fightTimer > TICKS(8))
		{
			fightTimer = s_curTick - TICKS(6);
		}
		else
		{
			fightTimer += TICKS(3);
		}

		if (fightTimer > (s32)s_curTick)
		{
			task_setNextTick(s_musicTask, s_curTick + MUS_FIGHT_TIME);
			if (!s_desiredFightState)
			{
				s_desiredFightState = JTRUE;
				gameMusic_setState(MUS_STATE_FIGHT);
			}
		}
	}

	void gameMusic_sustainFight()
	{
		if (s_desiredFightState)
		{
			gameMusic_startFight();
		}
	}

	void gameMusic_taskFunc(MessageType msg)
	{
		task_begin;
		while (msg != MSG_FREE_TASK)
		{
			task_yield(TASK_SLEEP);

			gameMusic_setState(MUS_STATE_STALK);
			s_desiredFightState = JFALSE;
		}
		task_end;
	}
		
	// Initial callback.
	void iMuseCallback1(char* marker)
	{
		if (!strncmp(marker, "to ", 3))
		{
			s32 punt = 0;
			if (!strncmp(&marker[4], "slow", 4))
			{
				if (s_currentState <= s_oldState || s_currentState == MUS_STATE_FIGHT)
				{
					punt = 1;
				}
			}
			else if ((s_currentState > s_oldState && s_currentState != MUS_STATE_FIGHT) || (s_currentState == s_oldState))
			{
				punt = 1;
			}

			if (!punt)
			{
				// Get the current measure and beat so we can return to it later.
				ImPause();
				s_savedMeasure = ImGetParam(s_oldSong, midiMeasure);
				s_savedBeat = ImGetParam(s_oldSong, midiBeat);
				ImResume();

				// This is a transition, so update the trigger.
				s_transChunk = marker[3] - 'A';	// "to A", "to B", etc.
				ImJumpMidi(s_oldSong, 1 + s_transChunk, 1, 1, 0, 1);
				ImSetTrigger(s_oldSong, 0, MUSIC_CALLBACK(iMuseCallback2));
			}
			else
			{
				// Reset the trigger.
				GMUSIC_MSG("Punt!...");
				ImSetTrigger(s_oldSong, 0, MUSIC_CALLBACK(iMuseCallback1));
			}
		}
		else
		{
			GMUSIC_MSG("Callback1 got bogus marker...");
		}
	}
		
	// Callback after transition.
	void iMuseCallback2(char* marker)
	{
		char* lptr = parseEvent(marker);
		s32 count = 0;
		if (lptr)
		{
			// Jump to transition piece.
			while (lptr[count]) { count++; }
			s32 r = gameMusic_random(0, count - 1);

			GMUSIC_MSG("trans%lu=%lu...", s_oldState, lptr[r]);
			ImJumpMidi(s_oldSong, 1 + s_transChunk, lptr[r] - 1, 4, 300, 1);
			ImSetTrigger(s_oldSong, 0, MUSIC_CALLBACK(iMuseCallback2));
		}
		else if (size_t(marker) >= GAME_INVALID_TRIGGER && !strcmp(marker, "start new")) // TFE: handle invalid integral markers from mods.
		{
			s_newSong = ImFindMidi(c_levelMusic[s_currentLevel - 1][s_currentState - 1]);
			if (s_oldSong != s_newSong)
			{
				ImDeferCommand(15, imStopSound, s_oldSong);
				ImStartSound(s_newSong, 64);
				ImShareParts(s_oldSong, s_newSong);
				ImSetTrigger(s_newSong, 0, MUSIC_CALLBACK(iMuseCallback3));
			}
			else
			{
				GMUSIC_MSG("Reset position!...");
				ImJumpMidi(s_newSong, 1, s_savedMeasure, s_savedBeat, 300, 1);
			}
		}
		else
		{
			ImSetTrigger(s_oldSong, 0, MUSIC_CALLBACK(iMuseCallback2));
		}
	}

	void iMuseCallback3(char* marker)
	{
		char* lptr = parseEvent(marker);
		if (lptr)
		{
			s32 count = 0;
			while (lptr[count]) { count++; }
			s32 r = gameMusic_random(0, count - 1);

			if (lptr[r] == s_stateEntrances[s_currentState])
			{
				r = gameMusic_random(0, count - 1);
			}
			if (lptr[r] == s_stateEntrances[s_currentState])
			{
				r = gameMusic_random(0, count - 1);
			}
			s_stateEntrances[s_currentState] = lptr[r];

			GMUSIC_MSG("entry%lu=%lu...", s_currentState, lptr[r]);
			ImJumpMidi(s_newSong, 1, lptr[r] - 1, 4, 400, 0);
		}
		else if (strcmp(marker, "clear callback"))	// Keep going until the "clear callback" marker is set.
		{
			ImSetTrigger(s_newSong, 0, MUSIC_CALLBACK(iMuseCallback3));
		}
	}

	s32 readNumber(char** ptrptr)
	{
		s32 num = 0;
		char* ptr = *ptrptr;

		while ((*ptr) && ((*ptr) < '0' || (*ptr) > '9'))
		{
			ptr++;
		}

		if (!(*ptr)) { return 0; }

		while ((*ptr) >= '0' && (*ptr) <= '9')
		{
			num *= 10;
			num += (*ptr) - '0';
			ptr++;
		}

		*ptrptr = ptr;
		return num;
	}

	char* parseEvent(char* ptr)
	{
		static char buf[32];
		// TFE: Some mods have an invalid integral value here.
		if (size_t(ptr) < GAME_INVALID_TRIGGER)
		{
			return nullptr;
		}

		s32 i = 0;
		if (!strncmp(ptr, "from ", 5))	// "from" markers
		{
			ptr += strlen("from ");

			if (!strncmp(ptr, "boss ", 5) && (s_oldState == MUS_STATE_BOSS))
			{
				ptr += 5;
				while (0 != (buf[i++] = readNumber(&ptr)));
				return buf[0] ? buf : nullptr;
			}
			if (!strncmp(ptr, "fight ", 6) && (s_oldState == MUS_STATE_FIGHT))
			{
				ptr += 6;
				while (0 != (buf[i++] = readNumber(&ptr)));
				return buf[0] ? buf : nullptr;
			}
			if (!strncmp(ptr, "engage ", 7) && (s_oldState == MUS_STATE_ENGAGE))
			{
				ptr += 7;
				while (0 != (buf[i++] = readNumber(&ptr)));
				return buf[0] ? buf : nullptr;
			}
			if (!strncmp(ptr, "stalk ", 6) && (s_oldState == MUS_STATE_STALK))
			{
				ptr += 6;
				while (0 != (buf[i++] = readNumber(&ptr)));
				return buf[0] ? buf : nullptr;
			}
			if (!strncmp(ptr, "explore ", 8) && (s_oldState == MUS_STATE_EXPLORE))
			{
				ptr += 8;
				while (0 != (buf[i++] = readNumber(&ptr)));
				return buf[0] ? buf : nullptr;
			}
		}
		else  // "to" markers
		{
			if (!strncmp(ptr, "boss ", 5) && (s_currentState == MUS_STATE_BOSS))
			{
				ptr += 5;
				ptr += strlen("trans ");
				while (0 != (buf[i++] = readNumber(&ptr)));
				return buf[0] ? buf : nullptr;
			}
			if (!strncmp(ptr, "fight ", 6) && (s_currentState == MUS_STATE_FIGHT))
			{
				ptr += 6;
				while (0 != (buf[i++] = readNumber(&ptr)));
				return buf[0] ? buf : nullptr;
			}
			if (!strncmp(ptr, "engage ", 7) && (s_currentState == MUS_STATE_ENGAGE))
			{
				ptr += 7;
				while (0 != (buf[i++] = readNumber(&ptr)));
				return buf[0] ? buf : nullptr;
			}
			if (!strncmp(ptr, "stalk ", 6) && (s_currentState == MUS_STATE_STALK))
			{
				ptr += 6;
				while (0 != (buf[i++] = readNumber(&ptr)));
				return buf[0] ? buf : nullptr;
			}
			if (!strncmp(ptr, "explore ", 8) && (s_currentState == MUS_STATE_EXPLORE))
			{
				ptr += 8;
				while (0 != (buf[i++] = readNumber(&ptr)));
				return buf[0] ? buf : nullptr;
			}
		}
		return nullptr;
	}

	s32 gameMusic_random(s32 low, s32 high)
	{
		static s32 rseed1, rseed2, initFlag = 0;
		if (!initFlag)
		{
			initFlag = 1;
			rseed1 =  (s32)(size_t)(&rseed2);
			rseed2 = ~(s32)(size_t)(&rseed1);
		}

		for (s32 i = 0; i < 23; i++)
		{
			s32 c = ((rseed1 & 0x40000000) != 0) ^ ((rseed2 & 0x20000000) == 0);
			rseed1 <<= 1;
			rseed1 += c;
		}
		for (s32 i = 0; i < 37; i++)
		{
			s32 c = ((rseed2 & 0x40000000) != 0) ^ ((rseed1 & 0x20000000) == 0);
			rseed1 <<= 1;
			rseed1 += c;
		}

		s32 range = high - low;
		return min(high, ((u16(rseed1 + rseed2) * s32(range + 1)) >> 16) + low);
	}
}  // TFE_DarkForces