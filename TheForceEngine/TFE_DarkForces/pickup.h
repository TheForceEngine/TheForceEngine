#pragma once
//////////////////////////////////////////////////////////////////////
// Dark Forces Pickup - 
// Items that can be picked up such as ammo, keys, extra lives,
// and weapons.
//////////////////////////////////////////////////////////////////////

#include <TFE_System/types.h>
#include <TFE_Asset/dfKeywords.h>
#include <TFE_Jedi/Level/rsector.h>
#include <TFE_Jedi/Level/robject.h>
#include "logic.h"
#include "item.h"

namespace TFE_DarkForces
{
	// This is a type of "Logic" and can be safety cast to Logic {}.
	struct Pickup
	{
		Logic logic;		// Logic header.
		ItemId id;
		s32 weaponIndex;
		ItemType type;
		JBool* playerItem;	// Points to s_playerInfo inventory item
		s32* playerAmmo;	// Points to s_playerInfo ammo type, shields, or health 
		s32 amount;
		s32 msgId[2];
		s32 maxAmount;

		// Scriptcall index
		s32 pickupScriptCall;
	};

	ItemId getPickupItemId(const char* keyword);
	Logic* obj_createPickup(SecObject* obj, ItemId id);
	s32 pickup_addToValue(s32 curValue, s32 amountToAdd, s32 maxAmount);
	void gasmaskTaskFunc(MessageType msg);

	// Logic update function, called when pickups are handled.
	void pickup_createTask();
	// Called when the player respawns.
	void pickupSupercharge();

	// Serialization
	void pickupLogic_serializeTasks(Stream* stream);
	void pickupLogic_serialize(Logic*& logic, SecObject* obj, Stream* stream);
	
	extern Task* s_superchargeTask;
	extern Task* s_invincibilityTask;
	extern Task* s_gasmaskTask;
	extern Task* s_gasSectorTask;
}  // namespace TFE_DarkForces