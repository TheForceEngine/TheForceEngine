#pragma once
//////////////////////////////////////////////////////////////////////
// Enhanced Actor (TFE)
// --------------------
// This is where new actor functionality will reside.
// The aim is to keep new functionality as separate as possible from
// vanilla code.
//
//////////////////////////////////////////////////////////////////////

#include "actorModule.h"

namespace TFE_DarkForces
{
	Logic* custom_actor_setup(SecObject* obj, TFE_ExternalData::CustomActorLogic* customEnemy, LogicSetupFunc* setupFunc);
	Tick enhancedAttackFunc(ActorModule* module, MovementModule* moveMod);
}