#pragma once
#include "weaponExternal.h"
#include <TFE_System/cJSON.h>

namespace TFE_ExternalData
{
	enum
	{
		CUSTOM_PROJ_STARTNUM = 100,
	};
	
	std::vector<ExternalProjectile>* getCustomProjectiles();
	void clearCustomProjectiles();
	void parseCustomProjectiles(cJSON* data);
	s32 getCustomProjectileIndex(const char* name);
}