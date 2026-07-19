#pragma once
#include "lactor.h"

namespace TFE_DarkForces
{
	HighResActor* highResActor_loadFromPng(const char* lfdName, const char* animName, u16 count, bool anim = true);
	void highResActor_free(HighResActor* actor);
}