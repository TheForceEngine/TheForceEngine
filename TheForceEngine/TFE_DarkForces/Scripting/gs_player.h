#pragma once
#include <TFE_System/system.h>
#include <TFE_System/types.h>
#include <TFE_DarkForces/player.h>
#include <TFE_ForceScript/scriptInterface.h>
#include <string>

namespace TFE_DarkForces
{
	class GS_Player : public ScriptAPIClass
	{
	public:
		bool scriptRegister(ScriptAPI api) override;
	};

	vec3_float getPlayerVelocity();
	void setPlayerVelocity(vec3_float vel);
}