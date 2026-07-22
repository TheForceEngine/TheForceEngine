#pragma once
//////////////////////////////////////////////////////////////////////
// Dark Forces
// Handles basic enemies that have no custom code beyond the setup.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_DarkForces/logic.h>
#include "actor.h"

namespace TFE_DarkForces
{
	Logic* reeyees_setup(SecObject* obj, LogicSetupFunc* setupFunc);
	Logic* reeyees2_setup(SecObject* obj, LogicSetupFunc* setupFunc);
	Logic* bossk_setup(SecObject* obj, LogicSetupFunc* setupFunc);
	Logic* gamor_setup(SecObject* obj, LogicSetupFunc* setupFunc);
}  // namespace TFE_DarkForces