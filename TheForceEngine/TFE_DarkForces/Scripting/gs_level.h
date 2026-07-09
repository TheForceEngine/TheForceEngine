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
#include <TFE_ForceScript/scriptInterface.h>
#include <string>

class CScriptArray;

namespace TFE_DarkForces
{
	class ScriptSector;
	class ScriptElev;
	class ScriptObject;
	
	class GS_Level : public ScriptAPIClass
	{
	public:
		// System
		bool scriptRegister(ScriptAPI api) override;

		ScriptSector getSectorById(s32 id);
		ScriptSector getSectorByName(std::string name);
		ScriptElev   getElevator(s32 id);
		ScriptObject getObjectById(s32 id);
		static ScriptObject getObjectByName(std::string name);
		static void getAllObjectsByName(std::string name, CScriptArray& result);
		void findConnectedSectors(ScriptSector initSector, u32 matchProp, CScriptArray& results);
		void setGravity(s32 grav);
		void setProjectileGravity(s32 grav);
	};
}
