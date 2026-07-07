#pragma once
//////////////////////////////////////////////////////////////////////
// The Force Engine Editor
// A system built to view and edit Dark Forces data files.
// The viewing aspect needs to be put in place at the beginning
// in order to properly test elements in isolation without having
// to "play" the game as intended.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/system.h>
#include <TFE_System/types.h>
#include <TFE_Jedi/Level/rwall.h>

namespace TFE_DarkForces
{
	class ScriptWall
	{
	public:
		ScriptWall() : m_sectorId(-1), m_wallId(-1) {};
		ScriptWall(s32 sectorId, s32 wallId) : m_sectorId(sectorId), m_wallId(wallId) {};
		void registerType();

	public:
		s32 m_sectorId;
		s32 m_wallId;
	};
}
