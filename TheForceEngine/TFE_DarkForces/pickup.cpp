#include "pickup.h"
#include "player.h"
#include "hud.h"
#include "weapon.h"
#include "sound.h"
#include <TFE_Game/igame.h>
#include <TFE_Jedi/InfSystem/message.h>
#include <TFE_Jedi/Level/level.h>
#include <TFE_Jedi/Level/levelData.h>
#include <TFE_Jedi/Task/task.h>
#include <TFE_Jedi/Serialization/serialization.h>
#include <TFE_ExternalData/pickupExternal.h>
#include <cstring>

using namespace TFE_Jedi;

namespace TFE_DarkForces
{
	//////////////////////////////////////////////////////////////
	// Structures and Constants
	//////////////////////////////////////////////////////////////

	//////////////////////////////////////////////////////////////
	// Internal State
	//////////////////////////////////////////////////////////////
	Task* s_pickupTask = nullptr;
	Task* s_superchargeTask = nullptr;
	Task* s_invincibilityTask = nullptr;
	Task* s_gasmaskTask = nullptr;
	Task* s_gasSectorTask = nullptr;

	enum { MAX_PICKUP_FREE_ITEMS = 128 };
	static Pickup* s_listToFree[MAX_PICKUP_FREE_ITEMS];
	static s32 s_listToFreeCnt = 0;

	//////////////////////////////////////////////////////////////
	// Forward Declarations
	//////////////////////////////////////////////////////////////
	void pickTaskFunc(MessageType msg);
	void pickupInvincibility();
	void pickupInventory();
	void pickupItem(MessageType msg);

	void gasmaskTaskFunc(MessageType msg);
	void invincibilityTaskFunc(MessageType msg);
	void superchargeTaskFunc(MessageType msg);

	//////////////////////////////////////////////////////////////
	// API Implementation
	//////////////////////////////////////////////////////////////
	void pickup_clearState()
	{
		s_playerDying = 0;
		s_listToFreeCnt = 0;
		// Pointer to memory where player inventory is saved.
		s_pickupTask = nullptr;
		s_superchargeTask = nullptr;
		s_invincibilityTask = nullptr;
		s_gasmaskTask = nullptr;
		s_gasSectorTask = nullptr;
	}

	// TODO: Move keyword to pickup mapping to a datafile to avoid hardcoding.
	ItemId getPickupItemId(const char* keyword)
	{
		const KEYWORD kw = getKeywordIndex(keyword);
		ItemId id;
		switch (kw)
		{
			case KW_BATTERY:    id = ITEM_BATTERY;    break;
			case KW_BLUE:       id = ITEM_BLUE_KEY;   break;
			case KW_CANNON:     id = ITEM_CANNON;     break;
			case KW_CLEATS:     id = ITEM_CLEATS;     break;
			case KW_CODE1:      id = ITEM_CODE1;      break;
			case KW_CODE2:      id = ITEM_CODE2;      break;
			case KW_CODE3:      id = ITEM_CODE3;      break;
			case KW_CODE4:      id = ITEM_CODE4;      break;
			case KW_CODE5:      id = ITEM_CODE5;      break;
			case KW_CODE6:      id = ITEM_CODE6;      break;
			case KW_CODE7:      id = ITEM_CODE7;      break;
			case KW_CODE8:      id = ITEM_CODE8;      break;
			case KW_CODE9:      id = ITEM_CODE9;      break;
			case KW_CONCUSSION: id = ITEM_CONCUSSION; break;
			case KW_DETONATOR:  id = ITEM_DETONATOR;  break;
			case KW_DETONATORS: id = ITEM_DETONATORS; break;
			case KW_DT_WEAPON:  id = ITEM_DT_WEAPON;  break;
			case KW_DATATAPE:   id = ITEM_DATATAPE;   break;
			case KW_ENERGY:     id = ITEM_ENERGY;     break;
			case KW_FUSION:     id = ITEM_FUSION;     break;
			case KW_GOGGLES:    id = ITEM_GOGGLES;    break;
			case KW_MASK:       id = ITEM_MASK;       break;
			case KW_MINE:       id = ITEM_MINE;       break;
			case KW_MINES:      id = ITEM_MINES;      break;
			case KW_MISSILE:    id = ITEM_MISSILE;    break;
			case KW_MISSILES:   id = ITEM_MISSILES;   break;
			case KW_MORTAR:     id = ITEM_MORTAR;     break;
			case KW_NAVA:       id = ITEM_NAVA;       break;
			case KW_PHRIK:      id = ITEM_PHRIK;      break;
			case KW_PLANS:      id = ITEM_PLANS;      break;
			case KW_PLASMA:     id = ITEM_PLASMA;     break;
			case KW_POWER:      id = ITEM_POWER;      break;
			case KW_RED:        id = ITEM_RED_KEY;    break;
			case KW_RIFLE:      id = ITEM_RIFLE;      break;
			case KW_SHELL:      id = ITEM_SHELL;      break;
			case KW_SHELLS:     id = ITEM_SHELLS;     break;
			case KW_SHIELD:     id = ITEM_SHIELD;     break;
			case KW_INVINCIBLE: id = ITEM_INVINCIBLE; break;
			case KW_REVIVE:     id = ITEM_REVIVE;     break;
			case KW_SUPERCHARGE:id = ITEM_SUPERCHARGE;break;
			case KW_LIFE:       id = ITEM_LIFE;       break;
			case KW_MEDKIT:     id = ITEM_MEDKIT;     break;
			case KW_PILE:       id = ITEM_PILE;       break;
			case KW_YELLOW:     id = ITEM_YELLOW_KEY; break;
			case KW_AUTOGUN:    id = ITEM_AUTOGUN;    break;
			default:            id = ITEM_PLANS;
		}
		return id;
	}

	void pickup_cleanupFunc(Logic* logic)
	{
		deleteLogicAndObject(logic);
	}

	void pickup_createTask()
	{
		pickup_clearState();
		s_pickupTask = createSubTask("pickups", pickTaskFunc, pickupItem);
	}
		
	void pickupItem(MessageType msg)
	{
		Pickup* pickup = (Pickup*)s_msgTarget;
		SecObject* entity = (SecObject*)s_msgEntity;
		if (msg == MSG_DAMAGE || msg == MSG_EXPLOSION)
		{
			return;
		}
		// Only the player can pickup an item.
		if (entity && !(entity->entityFlags & ETFLAG_PLAYER))
		{
			return;
		}

		JBool pickedUpItem = s_playerDying ? JFALSE : JTRUE;
		// Cannot pickup items while dying!
		if (!pickedUpItem)
		{
			return;
		}

		// Handle pickup based on type.
		if (pickup->type == ITYPE_WEAPON)
		{
			s32 maxAmount = pickup->maxAmount;
			if (!(*pickup->playerItem) || *pickup->playerAmmo < maxAmount)
			{
				*pickup->playerAmmo = pickup_addToValue(*pickup->playerAmmo, pickup->amount, maxAmount);
				if (*pickup->playerItem)
				{
					// Player already has the weapon, so show ammo message
					hud_sendTextMessage(pickup->msgId[1]);
				}
				else
				{
					// Add the weapon
					*pickup->playerItem = JTRUE;
					hud_sendTextMessage(pickup->msgId[0]);
					s_playerInfo.newWeapon = pickup->weaponIndex;
				}
			}
			else
			{
				pickedUpItem = JFALSE;
			}
		}
		else if (pickup->type == ITYPE_AMMO)
		{
			// Shield powerups cannot be picked up while invincibility is enabled even if shields are at less than maximum.
			if (pickup->id == ITEM_SHIELD && s_invincibility)
			{
				pickedUpItem = JFALSE;
			}
			s32 curValue = *pickup->playerAmmo;
			if (pickedUpItem && curValue < pickup->maxAmount)
			{
				*pickup->playerAmmo = pickup_addToValue(*pickup->playerAmmo, pickup->amount, pickup->maxAmount);
				hud_sendTextMessage(pickup->msgId[0]);
			}
			else
			{
				pickedUpItem = JFALSE;
			}
		}
		else if (pickup->type == ITYPE_USABLE)
		{
			*pickup->playerItem = JTRUE;
			hud_sendTextMessage(pickup->msgId[0]);
			if (pickup->playerAmmo)
			{
				*pickup->playerAmmo = pickup_addToValue(*pickup->playerAmmo, pickup->amount, pickup->maxAmount);
			}
			
			// Wear cleats immediately
			if (pickup->id == ITEM_CLEATS && s_wearingCleats == 0)
			{
				s_wearingCleats = JTRUE;
			}
		}
		else if (pickup->type == ITYPE_OBJECTIVE)
		{
			*pickup->playerItem = JTRUE;
			hud_sendTextMessage(pickup->msgId[0]);
			
			// Trigger complete elevator
			if (s_levelState.completeSector)
			{
				message_sendToSector(s_levelState.completeSector, nullptr, 0, MSG_TRIGGER);
			}

			// Mark objective as complete
			switch (pickup->id)
			{
				case ITEM_PLANS:
				{
					s_levelState.complete[1][0] = JTRUE;
				} break;
				case ITEM_PHRIK:
				{
					s_levelState.complete[1][1] = JTRUE;
				} break;
				case ITEM_NAVA:
				{
					s_levelState.complete[1][2] = JTRUE;
				} break;
				case ITEM_DT_WEAPON:
				{
					s_levelState.complete[1][5] = JTRUE;
				} break;
				case ITEM_DATATAPE:
				{
					s_levelState.complete[1][4] = JTRUE;
				} break;
				case ITEM_UNUSED:
				{
					s_levelState.complete[1][3] = JTRUE;
				} break;
			}
		}
		else if (pickup->type == ITYPE_CODEKEY)
		{
			*pickup->playerItem = JTRUE;
			hud_sendTextMessage(pickup->msgId[0]);
		}
		else if (pickup->type == ITYPE_POWERUP)
		{
			switch (pickup->id)
			{
				case ITEM_INVINCIBLE:
				{
					pickupInvincibility();
				} break;
				case ITEM_SUPERCHARGE:
				{
					pickupSupercharge();
				} break;
				case ITEM_REVIVE:
				{
					if (s_playerInfo.health < s_healthMax || s_playerInfo.shields < s_shieldsMax)
					{
						player_revive();
						// The function sets shields to 100, so set it to the proper value here.
						s_playerInfo.shields = s_shieldsMax;
					}
					else
					{
						pickedUpItem = JFALSE;
					}
				} break;
				case ITEM_LIFE:
				{
					if (s_lifeCount < 9)
					{
						s_lifeCount++;
					}
					else
					{
						pickedUpItem = 0;
					}
				} break;
			}
			if (pickedUpItem)
			{
				hud_sendTextMessage(pickup->msgId[0]);
			}
		}
		else if (pickup->type == ITYPE_SPECIAL)
		{
			hud_sendTextMessage(312);
			pickupInventory();
			s_levelState.complete[1][6] = JTRUE;

			if (s_levelState.completeSector)
			{
				message_sendToSector(s_levelState.completeSector, nullptr, 0, MSG_TRIGGER);
			}
		}

		// Finish
		if (!pickedUpItem)
		{
			return;
		}
		// Set the world width to 0 so the object cannot be picked up even if it isn't fully deleted.
		SecObject* pickupObj = pickup->logic.obj;
		pickupObj->worldWidth = 0;

		// Play pickup sound.
		if (pickup->type == ITYPE_USABLE || pickup->type == ITYPE_POWERUP)
		{
			sound_play(s_powerupPickupSnd);
		}
		else if (pickup->type == ITYPE_OBJECTIVE || pickup->type == ITYPE_SPECIAL)
		{
			sound_play(s_objectivePickupSnd);
		}
		else
		{
			sound_play(s_itemPickupSnd);
		}

		// TFE - Call Pickup script
		if (pickup->pickupScriptCall >= 0)
		{
			LogicScriptCall* scriptCall = logic_getScriptCall(pickup->pickupScriptCall);
			if (scriptCall && scriptCall->funcPtr)
			{
				TFE_ForceScript::execFunc(scriptCall->funcPtr, scriptCall->argCount, scriptCall->args);
			}
		}

		// Initialize effect
		s_flashEffect = FIXED(15);
		task_makeActive(s_pickupTask);

		// Instead of setting up a task, I just add it to a free list that will run next time the pickup task runs - which will be next frame since it is made active.
		{
			s_listToFree[s_listToFreeCnt++] = pickup;
		}
	}

	void freeItems()
	{
		for (s32 i = s_listToFreeCnt - 1; i >= 0; i--)
		{
			pickup_cleanupFunc((Logic*)s_listToFree[i]);
		}
		s_listToFreeCnt = 0;
	}
					
	// The current pickup being processed is stored in Message::s_msgTarget
	// The object "picking up" the item is stored in Message::s_msgEntity
	void pickTaskFunc(MessageType msg)
	{
		struct LocalContext
		{
			SecObject* pickupObj;
			SecObject* entity;
			Pickup* pickup;
			JBool pickedUpItem;
		};
		task_begin_ctx;
		while (msg != MSG_FREE_TASK)
		{
			// Sleep until called.
			task_yield(TASK_SLEEP);
			if (msg == MSG_RUN_TASK)
			{
				freeItems();
			}

			if (msg == MSG_FREE)
			{
				pickup_cleanupFunc((Logic*)s_msgTarget);
			}
			else if (msg == MSG_PICKUP)
			{
				pickupItem(msg);
			}  // if (id == PICKUP_ACQUIRE)
		}  // while (id != -1)
		task_end;
	}

	// TFE: Pickups are set from external data instead of hardcoded as in vanilla DF
	void setPickup(Pickup*& pickup, SecObject* obj, ItemId itemId, TFE_ExternalData::ExternalPickup* externalPickups)
	{
		// itemId will correspond to position in externalPickups array
		pickup->type = ItemType(externalPickups[itemId].type);
		pickup->weaponIndex = externalPickups[itemId].weaponIndex;
		pickup->playerItem = externalPickups[itemId].playerItem;
		pickup->playerAmmo = externalPickups[itemId].playerAmmo;
		pickup->amount = pickup->playerAmmo == s_playerBatteryPower
			? s32(externalPickups[itemId].amount / 100.0 * 2 * ONE_16)	// convert battery from a percentage
			: externalPickups[itemId].amount;
		
		pickup->msgId[0] = externalPickups[itemId].message1;
		pickup->msgId[1] = externalPickups[itemId].message2;

		if (externalPickups[itemId].fullBright)
		{
			obj->flags |= OBJ_FLAG_FULLBRIGHT;
		}

		if (externalPickups[itemId].noRemove)
		{
			obj->flags |= OBJ_FLAG_NO_REMOVE;
		}

		// Set max amounts based on ammo type
		TFE_ExternalData::MaxAmounts* maxAmounts = TFE_ExternalData::getMaxAmounts();
		if (externalPickups[itemId].playerAmmo == s_playerAmmoEnergy)
		{
			pickup->maxAmount = maxAmounts->ammoEnergyMax;
		}
		else if (externalPickups[itemId].playerAmmo == s_playerAmmoPower)
		{
			pickup->maxAmount = maxAmounts->ammoPowerMax;
		}
		else if (externalPickups[itemId].playerAmmo == s_playerAmmoPlasma)
		{
			pickup->maxAmount = maxAmounts->ammoPlasmaMax;
		}
		else if (externalPickups[itemId].playerAmmo == s_playerAmmoShell)
		{
			pickup->maxAmount = maxAmounts->ammoShellMax;
		}
		else if (externalPickups[itemId].playerAmmo == s_playerAmmoDetonators)
		{
			pickup->maxAmount = maxAmounts->ammoDetonatorMax;
		}
		else if (externalPickups[itemId].playerAmmo == s_playerAmmoMines)
		{
			pickup->maxAmount = maxAmounts->ammoMineMax;
		}
		else if (externalPickups[itemId].playerAmmo == s_playerAmmoMissiles)
		{
			pickup->maxAmount = maxAmounts->ammoMissileMax;
		}
		else if (externalPickups[itemId].playerAmmo == s_playerShields)
		{
			pickup->maxAmount = maxAmounts->shieldsMax;
		}
		else if (externalPickups[itemId].playerAmmo == s_playerHealth)
		{
			pickup->maxAmount = maxAmounts->healthMax;
		}
		else if (externalPickups[itemId].playerAmmo == s_playerBatteryPower)
		{
			pickup->maxAmount = maxAmounts->batteryPowerMax;
		}
	}

	Logic* obj_createPickup(SecObject* obj, ItemId id)
	{
		Pickup* pickup = (Pickup*)level_alloc(sizeof(Pickup));
		obj_addLogic(obj, (Logic*)pickup, LOGIC_PICKUP, s_pickupTask, pickup_cleanupFunc);

		obj->entityFlags |= ETFLAG_PICKUP;
		obj->worldHeight = 16384;	// 0.25

		// Setup the pickup based on the ItemId.
		pickup->id = id;
		pickup->weaponIndex = -1;
		pickup->playerItem = nullptr;
		pickup->playerAmmo = nullptr;
		pickup->amount = 0;
		pickup->msgId[0] = -1;
		pickup->msgId[1] = -1;
		pickup->maxAmount = 999;
		
		pickup->pickupScriptCall = -1;

		setPickup(pickup, obj, id, TFE_ExternalData::getExternalPickups());
		return (Logic*)pickup;
	}

	s32 pickup_addToValue(s32 curValue, s32 amountToAdd, s32 maxAmount)
	{
		s32 newValue = curValue + amountToAdd;
		if (newValue < 0)
		{
			newValue = 0;
		}
		else if (newValue > maxAmount)
		{
			newValue = maxAmount;
		}
		return newValue;
	}
		
	// Serialization
	void pickupLogic_setItemValue(Pickup* pickup)
	{
		pickup->playerItem = nullptr;
		pickup->playerAmmo = nullptr;

		// So long as the JSON hasn't been changed since the game was saved, this will work
		TFE_ExternalData::ExternalPickup* extPickups = TFE_ExternalData::getExternalPickups();
		pickup->playerItem = extPickups[pickup->id].playerItem;
		pickup->playerAmmo = extPickups[pickup->id].playerAmmo;
	}

	void pickupLogic_serializeTasks(Stream* stream)
	{
		enum PickupTasks
		{
			PT_GASMASK = FLAG_BIT(0),
			PT_INV = FLAG_BIT(1),
			PT_SUPERCHARGE = FLAG_BIT(2),
		};

		u8 pickupTasks = 0;
		if (serialization_getMode() == SMODE_WRITE)
		{
			pickupTasks |= s_gasmaskTask ? PT_GASMASK : 0;
			pickupTasks |= s_invincibilityTask ? PT_INV : 0;
			pickupTasks |= s_superCharge ? PT_SUPERCHARGE : 0;
		}
		SERIALIZE(SaveVersionInit, pickupTasks, 0);
		if (pickupTasks & PT_GASMASK)
		{
			if (serialization_getMode() == SMODE_READ)
			{
				s_gasmaskTask = createSubTask("gasmask", gasmaskTaskFunc);
			}
			task_serializeState(stream, s_gasmaskTask);
		}
		if (pickupTasks & PT_INV)
		{
			if (serialization_getMode() == SMODE_READ)
			{
				s_invincibilityTask = createSubTask("invincibility", invincibilityTaskFunc);
			}
			task_serializeState(stream, s_invincibilityTask);
		}
		if (pickupTasks & PT_SUPERCHARGE)
		{
			if (serialization_getMode() == SMODE_READ)
			{
				s_superchargeTask = createSubTask("supercharge", superchargeTaskFunc);
			}
			task_serializeState(stream, s_superchargeTask);
		}
	}

	void pickupLogic_serialize(Logic*& logic, SecObject* obj, Stream* stream)
	{
		Pickup* pickup;
		if (serialization_getMode() == SMODE_WRITE)
		{
			pickup = (Pickup*)logic;
		}
		else
		{
			pickup = (Pickup*)level_alloc(sizeof(Pickup));
			logic = (Logic*)pickup;
		}
		SERIALIZE(ObjState_InitVersion, pickup->id, ITEM_NONE);
		SERIALIZE(ObjState_InitVersion, pickup->weaponIndex, 0);
		SERIALIZE(ObjState_InitVersion, pickup->type, ITYPE_NONE);
		// item and value need to be setup based on type.
		SERIALIZE(ObjState_InitVersion, pickup->amount, 0);
		SERIALIZE_BUF(ObjState_InitVersion, pickup->msgId, sizeof(pickup->msgId[0]) * 2);
		SERIALIZE(ObjState_InitVersion, pickup->maxAmount, 0);

		// Index to ScriptCall
		SERIALIZE(ObjState_LogicScriptCallV1, pickup->pickupScriptCall, -1);

		if (serialization_getMode() == SMODE_READ)
		{
			pickup->logic.task = s_pickupTask;
			pickup->logic.cleanupFunc = pickup_cleanupFunc;
			pickupLogic_setItemValue(pickup);
		}
	}

	//////////////////////////////////////////////////////////////
	// Internal Implementation
	//////////////////////////////////////////////////////////////
	void gasmaskTaskFunc(MessageType msg)
	{
		struct LocalContext
		{
			SoundEffectId soundId;
		};
		task_begin_ctx;

		while (1)
		{
			task_yield(291);	// ~2 seconds.

			taskCtx->soundId = sound_play(s_maskSoundSource1);
			sound_pitchShift(taskCtx->soundId, -16);

			task_yield(291);	// ~2 seconds.

			taskCtx->soundId = sound_play(s_maskSoundSource2);
			sound_pitchShift(taskCtx->soundId, 16);
		}

		task_end;
	}
		
	void invincibilityTaskFunc(MessageType msg)
	{
		struct LocalContext
		{
			s32 i;
		};
		task_begin_ctx;
		task_waitWhileIdNotZero(s_shieldSuperchargeDuration);

		for (taskCtx->i = 4; taskCtx->i >= 0; taskCtx->i--)
		{
			sound_play(s_invCountdownSound);
			s_playerInfo.shields = s_shieldsMax;
			task_waitWhileIdNotZero(87);	// 0.6 seconds.

			s_playerInfo.shields = 0xffffffff;
			task_waitWhileIdNotZero(87);	// 0.6 seconds.
		}
		s_playerInfo.shields = s_shieldsMax;
		s_invincibility = 0;
		s_invincibilityTask = nullptr;

		task_end;
	}

	void superchargeTaskFunc(MessageType msg)
	{
		struct LocalContext
		{
			s32 i;
		};
		task_begin_ctx;

		s_superChargeHud = JTRUE;
		s_superCharge = JTRUE;

		// This causes the task to be suspended for 45 seconds - which is the time the supercharge is active
		// without the running out warning.
		task_waitWhileIdNotZero(s_weaponSuperchargeDuration);

		for (taskCtx->i = 4; taskCtx->i >= 0; taskCtx->i--)
		{
			sound_play(s_superchargeCountdownSound);
			s_superChargeHud = JFALSE;
			task_waitWhileIdNotZero(87);	// 0.6 seconds.

			s_superChargeHud = JTRUE;
			task_waitWhileIdNotZero(87);	// 0.6 seconds.
		}
		s_superCharge = JFALSE;
		s_superChargeHud = JFALSE;
		s_superchargeTask = nullptr;

		task_end;
	}
		
	void pickupInvincibility()
	{
		s_invincibility = -1;
		s_playerInfo.shields = -1;		// When set to -1 the HUD will display shields as a special colour (eg. bright yellow)
		// Free old invincibility task and create a new invincibility task.
		if (s_invincibilityTask)
		{
			task_free(s_invincibilityTask);
		}
		s_invincibilityTask = createSubTask("invincibility", invincibilityTaskFunc);
	}

	void pickupSupercharge()
	{
		// Free old supercharge task and create a new supercharge task.
		if (s_superchargeTask)
		{
			task_free(s_superchargeTask);
		}
		s_superchargeTask = createSubTask("supercharge", superchargeTaskFunc);
	}

	void pickupInventory()
	{
		// Get the size of the PlayerInfo structure up to but not including s_playerInfo.stateUnknown.
		size_t size = (size_t)&s_playerInfo.pileSaveMarker - (size_t)&s_playerInfo;
		if (s_playerInvSaved)
		{
			// Copy the saved player info and add it to the current player info.
			u32* dst = (u32*)&s_playerInfo;
			u32* src = s_playerInvSaved;
			for (s32 sizeCopied = 0; sizeCopied < size; sizeCopied += 4, src++, dst++)
			{
				(*dst) += (*src);
			}

			// Clear out the nava card item since it is one of the current objectives.
			s_playerInfo.itemNava = JFALSE;
			if (s_playerInfo.maxWeapon != s_playerInfo.curWeapon)
			{
				if (s_playerWeaponTask)
				{
					weapon_queueWeaponSwitch(s_playerInfo.maxWeapon);
					s_playerInfo.maxWeapon = max(s_playerInfo.curWeapon, s_playerInfo.maxWeapon);
				}
				else
				{
					s_playerInfo.saveWeapon = s_playerInfo.maxWeapon;
				}
			}

			// Free saved inventory.
			level_free(s_playerInvSaved);
			s_playerInvSaved = nullptr;
			// Clamp ammo values.
			s_playerInfo.ammoEnergy    = pickup_addToValue(s_playerInfo.ammoEnergy,    0, s_ammoEnergyMax);
			s_playerInfo.ammoPower     = pickup_addToValue(s_playerInfo.ammoPower,     0, s_ammoPowerMax);
			s_playerInfo.ammoPlasma    = pickup_addToValue(s_playerInfo.ammoPlasma,    0, s_ammoPlasmaMax);
			s_playerInfo.ammoDetonator = pickup_addToValue(s_playerInfo.ammoDetonator, 0, s_ammoDetonatorMax);
			s_playerInfo.ammoShell     = pickup_addToValue(s_playerInfo.ammoShell,     0, s_ammoShellMax);
			s_playerInfo.ammoMine      = pickup_addToValue(s_playerInfo.ammoMine,      0, s_ammoMineMax);
			s_playerInfo.ammoMissile   = pickup_addToValue(s_playerInfo.ammoMissile,   0, s_ammoMissileMax);
		}
	}
}  // TFE_DarkForces