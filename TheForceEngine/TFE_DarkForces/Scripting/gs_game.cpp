#include "gs_game.h"
#include <TFE_DarkForces/hud.h>
#include <TFE_DarkForces/random.h>
#include <TFE_DarkForces/time.h>
#include <TFE_System/system.h>
#include <string>
#include <angelscript.h>

using namespace TFE_Jedi;

namespace TFE_DarkForces
{
	const f32 c_timeScale = 1.0f / f32(TICKS_PER_SECOND);

	f32 GS_Game::getGameTime()
	{
		return f32(s_curTick) * c_timeScale;
	}

	s32 GS_Game::scriptRandom(s32 value)
	{
		return random(value);
	}

	void GS_Game::text(string msg)
	{
		TFE_DarkForces::hud_sendTextMessage(msg.c_str(), 0, false);
	}

	bool GS_Game::scriptRegister(ScriptAPI api)
	{
		ScriptClassBegin("Game", "game", api);
		{
			// Enums
			ScriptEnumRegister("MessageType");
			ScriptEnum("M_TRIGGER", MSG_TRIGGER);
			ScriptEnum("NEXT_STOP", MSG_NEXT_STOP);
			ScriptEnum("PREV_STOP", MSG_PREV_STOP);
			// ScriptEnum("GOTO_STOP",   MSG_GOTO_STOP);	// GOTO_STOP requires a parameter, not yet implemented
			ScriptEnum("DONE", MSG_DONE);
			ScriptEnum("WAKEUP", MSG_WAKEUP);
			ScriptEnum("MASTER_ON", MSG_MASTER_ON);
			ScriptEnum("MASTER_OFF", MSG_MASTER_OFF);
			ScriptEnum("CRUSH", MSG_CRUSH);

			// Functions
			ScriptObjMethod("float getGameTime()", getGameTime);
			ScriptObjMethod("int random(int)", scriptRandom);
			ScriptObjMethod("void text(string)", text);
		}
		ScriptClassEnd();
	}
}

