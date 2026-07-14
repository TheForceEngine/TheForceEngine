#pragma once
//////////////////////////////////////////////////////////////////////
// Dark Forces Game Music
// Handles music states and iMuse callbacks.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_FileSystem/paths.h>

enum MusicState
{
	MUS_STATE_NULLSTATE = 0,
	MUS_STATE_BOSS,    // Partially implemented and then removed from the game.
	MUS_STATE_FIGHT,
	MUS_STATE_ENGAGE,  // Removed from the game.
	MUS_STATE_STALK,
	MUS_STATE_EXPLORE, // Removed from the game.
	MUS_STATE_UNDEFINED
};

namespace TFE_DarkForces
{
	void gameMusic_start(s32 level);
	void gameMusic_stop();

	void gameMusic_setState(MusicState state);
	void gameMusic_startFight();
	void gameMusic_sustainFight();

	MusicState gameMusic_getState();
	void gameMusic_update();
}  // TFE_DarkForces