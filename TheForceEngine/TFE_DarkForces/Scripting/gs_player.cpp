#include "gs_player.h"
#include "scriptSector.h"
#include "assert.h"
#include <TFE_DarkForces/mission.h>
#include <TFE_DarkForces/pickup.h>
#include <TFE_DarkForces/weapon.h>
#include <TFE_Jedi/Collision/collision.h>
#include <TFE_Jedi/Level/levelData.h>
#include <TFE_FrontEndUI/frontEndUi.h>
#include <TFE_System/system.h>
#include <angelscript.h>

namespace TFE_DarkForces
{
	enum AmmoType
	{
		AMMO_ENERGY,
		AMMO_POWER,
		AMMO_DETONATOR,
		AMMO_SHELL,
		AMMO_PLASMA,
		AMMO_MINE,
		AMMO_MISSILE,
	};

	enum ActionFlags : u32
	{
		ACTION_NONE        = 0u,
		ACTION_MOVE        = FLAG_BIT(0),
		ACTION_ROTATE      = FLAG_BIT(1),
		ACTION_FIRE        = FLAG_BIT(2),
		ACTION_ALL         = ACTION_MOVE | ACTION_ROTATE | ACTION_FIRE,
		
		// Others that could be implemented if desired?
		// ACTION_PITCH
		// ACTION_CROUCH
		// ACTION_USE
		// ACTION_HEADLAMP
	};

	void killPlayer()
	{
		sound_play(s_playerDeathSoundSource);
		if (s_gasSectorTask)
		{
			task_free(s_gasSectorTask);
		}
		s_playerInfo.health = 0;
		s_gasSectorTask = nullptr;
		s_playerDying = JTRUE;
		s_reviveTick = s_curTick + 436;
	}

	ScriptSector getPlayerSector()
	{
		RSector* sec = s_playerObject->sector;
		ScriptSector sector(sec->index);
		return sector;
	}

	vec3_float getPlayerPosition()
	{
		return
		{
			fixed16ToFloat(s_playerObject->posWS.x),
			-fixed16ToFloat(s_playerObject->posWS.y),
			fixed16ToFloat(s_playerObject->posWS.z),
		};
	}

	void setPlayerPosition(vec3_float position)
	{
		fixed16_16 newX = floatToFixed16(position.x);
		fixed16_16 newY = -floatToFixed16(position.y);
		fixed16_16 newZ = floatToFixed16(position.z);

		// Check if player object has moved out of its current sector
		// Start with a bounds check
		bool outOfHorzBounds = newX < s_playerObject->sector->boundsMin.x || newX > s_playerObject->sector->boundsMax.x ||
			newZ < s_playerObject->sector->boundsMin.z || newZ > s_playerObject->sector->boundsMax.z;
		bool outOfVertBounds = newY < s_playerObject->sector->ceilingHeight || newY > s_playerObject->sector->floorHeight;

		// If within bounds, check if it has passed through a wall
		RWall* collisionWall = nullptr;
		if (!outOfHorzBounds && !outOfVertBounds)
		{
			collisionWall = collision_wallCollisionFromPath(s_playerObject->sector, s_playerObject->posWS.x, s_playerObject->posWS.z, newX, newZ);
		}

		if (collisionWall || outOfHorzBounds || outOfVertBounds)
		{
			// Try to find a new sector for the player object; if there is none, don't move it
			RSector* sector = sector_which3D(newX, newY, newZ);
			if (sector)
			{
				if (sector != s_playerObject->sector)
				{
					sector_addObject(sector, s_playerObject);
				}
			}
			else
			{
				TFE_FrontEndUI::logToConsole("Cannot find a sector to move player to.");
				return;
			}
		}

		s_playerObject->posWS.x = newX;
		s_playerObject->posWS.y = newY;
		s_playerObject->posWS.z = newZ;
		s_playerYPos = newY;
	}

	float getPlayerYaw()
	{
		return angleToFloat(s_playerObject->yaw);
	}

	void setPlayerYaw(float value)
	{
		angle14_32 yaw = floatToAngle(value);
		s_playerObject->yaw = yaw;
		s_playerYaw = yaw;
	}

	vec3_float getPlayerVelocity()
	{
		vec3_fixed velFixed;
		TFE_DarkForces::player_getVelocity(&velFixed);

		return {
			fixed16ToFloat(velFixed.x),
			-fixed16ToFloat(velFixed.y),
			fixed16ToFloat(velFixed.z),
		};
	}
	
	void setPlayerVelocity(vec3_float vel)
	{
		s_playerVelX = floatToFixed16(vel.x);
		s_playerUpVel  = -floatToFixed16(vel.y);
		s_playerUpVel2 = -floatToFixed16(vel.y);
		s_playerVelZ = floatToFixed16(vel.z);
	}

	s32 getBatteryPercent()
	{
		return (s32)(100.0 * s_batteryPower / FIXED(2) );
	}

	s32 getAmmo(s32 ammoType)
	{
		switch (ammoType)
		{
			case AMMO_ENERGY:
				return s_playerInfo.ammoEnergy;
			
			case AMMO_POWER:
				return s_playerInfo.ammoPower;

			case AMMO_DETONATOR:
				return s_playerInfo.ammoDetonator;

			case AMMO_SHELL:
				return s_playerInfo.ammoShell;

			case AMMO_PLASMA:
				return s_playerInfo.ammoPlasma;

			case AMMO_MINE:
				return s_playerInfo.ammoMine;

			case AMMO_MISSILE:
				return s_playerInfo.ammoMissile;

			default:
				return -1;
		}
	}

	void setHealth(s32 value)
	{
		s_playerInfo.health = value;

		if (s_playerInfo.health <= 0)
		{
			killPlayer();
		}
	}

	void setShields(s32 value)
	{
		s_playerInfo.shields = value;
	}

	void setBattery(s32 value)
	{
		s_batteryPower = (fixed16_16)((value / 100.0) * FIXED(2));
	}

	void setAmmo(s32 ammoType, s32 value)
	{
		switch (ammoType)
		{
			case AMMO_ENERGY:
				s_playerInfo.ammoEnergy = value;
				return;

			case AMMO_POWER:
				s_playerInfo.ammoPower = value;
				return;

			case AMMO_DETONATOR:
				s_playerInfo.ammoDetonator = value;
				return;

			case AMMO_SHELL:
				s_playerInfo.ammoShell = value;
				return;

			case AMMO_PLASMA:
				s_playerInfo.ammoPlasma = value;
				return;

			case AMMO_MINE:
				s_playerInfo.ammoMine = value;
				return;

			case AMMO_MISSILE:
				s_playerInfo.ammoMissile = value;
				return;

			default:
				return;
		}
	}

	void addToHealth(s32 value)
	{
		s_playerInfo.health += value;

		if (s_playerInfo.health <= 0)
		{
			killPlayer();
		}
	}

	void addToShields(s32 value)
	{
		s_playerInfo.shields += value;
		if (s_playerInfo.shields < 0)
		{
			s_playerInfo.shields = 0;
		}
	}

	bool hasRedKey()
	{
		return s_playerInfo.itemRedKey == JTRUE;
	}

	bool hasBlueKey()
	{
		return s_playerInfo.itemBlueKey == JTRUE;
	}

	bool hasYellowKey()
	{
		return s_playerInfo.itemYellowKey == JTRUE;
	}

	bool hasGoggles()
	{
		return s_playerInfo.itemGoggles == JTRUE;
	}

	bool hasCleats()
	{
		return s_playerInfo.itemCleats == JTRUE;
	}

	bool hasMask()
	{
		return s_playerInfo.itemMask == JTRUE;
	}

	void giveRedKey()
	{
		s_playerInfo.itemRedKey = JTRUE;
	}

	void giveBlueKey()
	{
		s_playerInfo.itemBlueKey = JTRUE;
	}

	void giveYellowKey()
	{
		s_playerInfo.itemYellowKey = JTRUE;
	}

	void giveGoggles()
	{
		s_playerInfo.itemGoggles = JTRUE;
	}

	void giveCleats()
	{
		s_playerInfo.itemCleats = JTRUE;
	}

	void giveMask()
	{
		s_playerInfo.itemMask = JTRUE;
	}

	void removeRedKey()
	{
		s_playerInfo.itemRedKey = JFALSE;
	}

	void removeBlueKey()
	{
		s_playerInfo.itemBlueKey = JFALSE;
	}

	void removeYellowKey()
	{
		s_playerInfo.itemYellowKey = JFALSE;
	}

	void removeGoggles()
	{
		if (s_nightVisionActive)
		{
			disableNightVision();
		}
		
		s_playerInfo.itemGoggles = JFALSE;
	}

	void removeCleats()
	{
		if (s_wearingCleats)
		{
			disableCleats();
		}
		
		s_playerInfo.itemCleats = JFALSE;
	}

	void removeMask()
	{
		if (s_wearingGasmask)
		{
			disableMask();
		}
		
		s_playerInfo.itemMask = JFALSE;
	}

	void giveCodeKey(s32 num)
	{
		switch (num)
		{
			case 1:
				s_playerInfo.itemCode1 = JTRUE;
				return;

			case 2:
				s_playerInfo.itemCode2 = JTRUE;
				return;

			case 3:
				s_playerInfo.itemCode3 = JTRUE;
				return;

			case 4:
				s_playerInfo.itemCode4 = JTRUE;
				return;

			case 5:
				s_playerInfo.itemCode5 = JTRUE;
				return;

			case 6:
				s_playerInfo.itemCode6 = JTRUE;
				return;

			case 7:
				s_playerInfo.itemCode7 = JTRUE;
				return;

			case 8:
				s_playerInfo.itemCode8 = JTRUE;
				return;

			case 9:
				s_playerInfo.itemCode9 = JTRUE;
				return;

			default:
				return;
		}
	}

	void giveItem(s32 item)
	{
		switch (item)
		{
			case ITEM_PLANS:
				s_playerInfo.itemPlans = JTRUE;
				return;

			case ITEM_PHRIK:
				s_playerInfo.itemPhrik = JTRUE;
				return;

			case ITEM_NAVA:
				s_playerInfo.itemNava = JTRUE;
				return;

			case ITEM_DT_WEAPON:
				s_playerInfo.itemDtWeapon = JTRUE;
				return;

			case ITEM_DATATAPE:
				s_playerInfo.itemDatatape = JTRUE;
				return;

			case ITEM_UNUSED:
				s_playerInfo.itemUnused = JTRUE;
				return;

			default:
				return;
		}
	}

	bool hasWeapon(s32 weapon)
	{
		switch (weapon)
		{
			case WPN_PISTOL:
				return s_playerInfo.itemPistol == JTRUE;

			case WPN_RIFLE:
				return s_playerInfo.itemRifle == JTRUE;

			case WPN_REPEATER:
				return s_playerInfo.itemAutogun == JTRUE;

			case WPN_FUSION:
				return s_playerInfo.itemFusion == JTRUE;

			case WPN_MORTAR:
				return s_playerInfo.itemMortar == JTRUE;

			case WPN_CONCUSSION:
				return s_playerInfo.itemConcussion == JTRUE;

			case WPN_CANNON:
				return s_playerInfo.itemCannon == JTRUE;

			default:
				return false;
		}
	}

	void giveWeapon(s32 weapon)
	{
		switch (weapon)
		{
			case WPN_PISTOL:
				s_playerInfo.itemPistol = JTRUE;
				return;

			case WPN_RIFLE:
				s_playerInfo.itemRifle = JTRUE;
				return;

			case WPN_REPEATER:
				s_playerInfo.itemAutogun = JTRUE;
				return;

			case WPN_FUSION:
				s_playerInfo.itemFusion = JTRUE;
				return;

			case WPN_MORTAR:
				s_playerInfo.itemMortar = JTRUE;
				return;

			case WPN_CONCUSSION:
				s_playerInfo.itemConcussion = JTRUE;
				return;

			case WPN_CANNON:
				s_playerInfo.itemCannon = JTRUE;
				return;

			default:
				return;
		}
	}

	void removeWeapon(s32 weapon)
	{
		switch (weapon)
		{
			case WPN_PISTOL:
				if (s_playerInfo.curWeapon == WPN_PISTOL)
				{
					s_playerInfo.newWeapon = s_playerInfo.itemRifle ? WPN_RIFLE : WPN_FIST;
				}
				s_playerInfo.itemPistol = JFALSE;
				return;

			case WPN_RIFLE:
				if (s_playerInfo.curWeapon == WPN_RIFLE)
				{
					s_playerInfo.newWeapon = s_playerInfo.itemPistol ? WPN_PISTOL : WPN_FIST;
				}
				s_playerInfo.itemRifle = JFALSE;
				return;

			case WPN_REPEATER:
				if (s_playerInfo.curWeapon == WPN_REPEATER)
				{
					s_playerInfo.newWeapon = s_playerInfo.itemPistol ? WPN_PISTOL : WPN_FIST;
				}
				s_playerInfo.itemAutogun = JFALSE;
				return;

			case WPN_FUSION:
				if (s_playerInfo.curWeapon == WPN_FUSION)
				{
					s_playerInfo.newWeapon = s_playerInfo.itemPistol ? WPN_PISTOL : WPN_FIST;
				}
				s_playerInfo.itemFusion = JFALSE;
				return;

			case WPN_MORTAR:
				if (s_playerInfo.curWeapon == WPN_MORTAR)
				{
					s_playerInfo.newWeapon = s_playerInfo.itemPistol ? WPN_PISTOL : WPN_FIST;
				}
				s_playerInfo.itemMortar = JFALSE;
				return;

			case WPN_CONCUSSION:
				if (s_playerInfo.curWeapon == WPN_CONCUSSION)
				{
					s_playerInfo.newWeapon = s_playerInfo.itemPistol ? WPN_PISTOL : WPN_FIST;
				}
				s_playerInfo.itemConcussion = JFALSE;
				return;

			case WPN_CANNON:
				if (s_playerInfo.curWeapon == WPN_CANNON)
				{
					s_playerInfo.newWeapon = s_playerInfo.itemPistol ? WPN_PISTOL : WPN_FIST;
				}
				s_playerInfo.itemCannon = JFALSE;
				return;

			default:
				return;
		}
	}

	void disableActions(u32 flags)
	{
		if (flags & ACTION_MOVE)
		{
			s_disablePlayerMovement = JTRUE;
		}
		if (flags & ACTION_ROTATE)
		{
			s_disablePlayerRotation = JTRUE;
		}
		if (flags & ACTION_FIRE)
		{
			s_disablePlayerFire = JTRUE;
		}
	}

	void enableActions(u32 flags)
	{
		if (flags & ACTION_MOVE)
		{
			s_disablePlayerMovement = JFALSE;
		}
		if (flags & ACTION_ROTATE)
		{
			s_disablePlayerRotation = JFALSE;
		}
		if (flags & ACTION_FIRE)
		{
			s_disablePlayerFire = JFALSE;
		}
	}

	u32 getDisabledActions()
	{
		u32 result = 0;
		if (s_disablePlayerMovement) { result |= ACTION_MOVE; }
		if (s_disablePlayerRotation) { result |= ACTION_ROTATE; }
		if (s_disablePlayerFire)     { result |= ACTION_FIRE; }
		
		return result;
	}

	void setCamera()
	{
		player_setupEyeObject(s_playerObject);
		s_externalCameraMode = JFALSE;
	}

	bool GS_Player::scriptRegister(ScriptAPI api)
	{
		ScriptClassBegin("Player", "player", api);
		{
			// Enums
			ScriptEnumRegister("Items");
			ScriptEnum("PLANS", ITEM_PLANS);
			ScriptEnum("PHRIK", ITEM_PHRIK);
			ScriptEnum("NAVA", ITEM_NAVA);
			ScriptEnum("DT_WEAPON", ITEM_DT_WEAPON);
			ScriptEnum("DATATAPE", ITEM_DATATAPE);
			ScriptEnum("ITEM10", ITEM_UNUSED);

			ScriptEnumRegister("Weapons");
			ScriptEnum("PISTOL", WPN_PISTOL);
			ScriptEnum("RIFLE", WPN_RIFLE);
			ScriptEnum("REPEATER", WPN_REPEATER);
			ScriptEnum("FUSION", WPN_FUSION);
			ScriptEnum("MORTAR", WPN_MORTAR);
			ScriptEnum("CONCUSSION", WPN_CONCUSSION);
			ScriptEnum("CANNON", WPN_CANNON);

			ScriptEnumRegister("Ammo");
			ScriptEnumStr(AMMO_ENERGY);
			ScriptEnumStr(AMMO_POWER);
			ScriptEnumStr(AMMO_DETONATOR);
			ScriptEnumStr(AMMO_SHELL);
			ScriptEnumStr(AMMO_PLASMA);
			ScriptEnumStr(AMMO_MINE);
			ScriptEnumStr(AMMO_MISSILE);

			ScriptEnumRegister("PlayerActions");
			ScriptEnumStr(ACTION_MOVE);
			ScriptEnumStr(ACTION_ROTATE);
			ScriptEnumStr(ACTION_FIRE);
			ScriptEnumStr(ACTION_ALL);

			ScriptObjFunc("void kill()", killPlayer);
			ScriptObjFunc("void disableActions(uint)", disableActions);
			ScriptObjFunc("void enableActions(uint)", enableActions);
			ScriptPropertyGetFunc("uint get_disabledActions()", getDisabledActions);
			ScriptObjFunc("void setCamera()", setCamera);

			// Position and velocity
			ScriptPropertyGetFunc("Sector get_sector()", getPlayerSector);
			ScriptPropertyGetFunc("float3 get_position()", getPlayerPosition);
			ScriptPropertySetFunc("void set_position(float3)", setPlayerPosition);
			ScriptPropertyGetFunc("float get_yaw()", getPlayerYaw);
			ScriptPropertySetFunc("void set_yaw(float)", setPlayerYaw);
			ScriptPropertyGetFunc("float3 get_velocity()", getPlayerVelocity);
			ScriptPropertySetFunc("void set_velocity(float3)", setPlayerVelocity);
			
			// Health / ammo getters
			ScriptLambdaPropertyGet("int get_health()", s32, { return s_playerInfo.health; });
			ScriptLambdaPropertyGet("int get_shields()", s32, { return s_playerInfo.shields; });
			ScriptPropertyGetFunc("int get_battery()", getBatteryPercent);
			ScriptObjFunc("int getAmmo(uint)", getAmmo);

			// Health / ammo setters
			ScriptPropertySetFunc("void set_health(int)", setHealth);
			ScriptPropertySetFunc("void set_shields(int)", setShields);
			ScriptPropertySetFunc("void set_battery(int)", setBattery);
			ScriptObjFunc("void setAmmo(int, int)", setAmmo);

			ScriptObjFunc("void addToHealth(int)", addToHealth);
			ScriptObjFunc("void addToShields(int)", addToShields);

			// Items
			ScriptObjFunc("bool hasRedKey()", hasRedKey);
			ScriptObjFunc("bool hasBlueKey()", hasBlueKey);
			ScriptObjFunc("bool hasYellowKey()", hasYellowKey);
			ScriptObjFunc("bool hasGoggles()", hasGoggles);
			ScriptObjFunc("bool hasCleats()", hasCleats);
			ScriptObjFunc("bool hasMask()", hasMask);

			ScriptObjFunc("void giveRedKey()", giveRedKey);
			ScriptObjFunc("void giveBlueKey()", giveBlueKey);
			ScriptObjFunc("void giveYellowKey()", giveYellowKey);
			ScriptObjFunc("void giveGoggles()", giveGoggles);
			ScriptObjFunc("void giveCleats()", giveCleats);
			ScriptObjFunc("void giveMask()", giveMask);

			ScriptObjFunc("void removeRedKey()", removeRedKey);
			ScriptObjFunc("void removeBlueKey()", removeBlueKey);
			ScriptObjFunc("void removeYellowKey()", removeYellowKey);
			ScriptObjFunc("void removeGoggles()", removeGoggles);
			ScriptObjFunc("void removeCleats()", removeCleats);
			ScriptObjFunc("void removeMask()", removeMask);

			ScriptObjFunc("void giveCodeKey(int)", giveCodeKey);
			ScriptObjFunc("void giveItem(int)", giveItem);

			// Weapons
			ScriptObjFunc("bool hasWeapon(int)", hasWeapon);
			ScriptObjFunc("void giveWeapon(int)", giveWeapon);
			ScriptObjFunc("void removeWeapon(int)", removeWeapon);
		}
		ScriptClassEnd();
	}
}