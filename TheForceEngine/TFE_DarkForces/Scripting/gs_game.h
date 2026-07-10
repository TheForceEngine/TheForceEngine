#pragma once
//////////////////////////////////////////////////////////////////////
// The Force Engine Editor
// A system built to view and edit Dark Forces data files.
// The viewing aspect needs to be put in place at the beginning
// in order to properly test elements in isolation without having
// to "play" the game as intended.
//////////////////////////////////////////////////////////////////////
#include <string>
#include <TFE_System/system.h>
#include <TFE_System/types.h>
#include <TFE_ForceScript/scriptInterface.h>

namespace TFE_DarkForces
{
	class GS_Game : public ScriptAPIClass
	{
	public:
		// System
		bool scriptRegister(ScriptAPI api) override;

		f32 getGameTime();
		s32 scriptRandom(s32 value);
		void text(std::string msg);
		int playCutscene(std::string cutsceneName);
	};
}

