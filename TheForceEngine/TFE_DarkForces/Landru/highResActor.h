#pragma once
#include "lactor.h"

namespace TFE_DarkForces
{
	HighResActor* highResActor_loadFromPng(const char* lfdName, const char* actorName, u16 frameCount, bool anim = true);
	void highResActor_free(HighResActor* actor);
}