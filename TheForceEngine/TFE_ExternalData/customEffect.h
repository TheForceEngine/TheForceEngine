#pragma once
#include "weaponExternal.h"
#include <TFE_System/cJSON.h>

namespace TFE_ExternalData
{
	enum
	{
		CUSTOM_EFFECT_STARTNUM = 100,
	};

	std::vector<ExternalEffect>* getCustomEffects();
	void clearCustomEffects();
	void parseCustomEffects(cJSON* data);
	s32 getCustomEffectIndex(const char* name);
}