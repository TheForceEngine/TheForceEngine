#include "gs_level.h"
#include "gs_player.h"
#include "scriptObject.h"
#include "scriptSector.h"
#include <angelscript.h>
#include <TFE_FrontEndUI/frontEndUi.h>
#include <TFE_Asset/dfKeywords.h>
#include <TFE_DarkForces/logic.h>
#include <TFE_DarkForces/animLogic.h>
#include <TFE_DarkForces/pickup.h>
#include <TFE_DarkForces/player.h>
#include <TFE_DarkForces/mission.h>
#include <TFE_DarkForces/Actor/actor.h>
#include <TFE_ForceScript/forceScript.h>
#include <TFE_ForceScript/scriptAPI.h>
#include <TFE_Jedi/Level/levelData.h>
#include <TFE_Jedi/Collision/collision.h>
#include <TFE_Jedi/Memory/allocator.h>

namespace TFE_DarkForces
{
	// The "special" actors can all be cast to this struct (eg. bosses, dark troopers, mousebot, turret)
	struct SpecialActor	
	{
		Logic logic;
		PhysicsActor actor;
	};

	bool isScriptObjectValid(ScriptObject* sObject)
	{
		return sObject->m_id >= 0 && sObject->m_id < s_objectRefList.size();
	}

	bool doesObjectExist(ScriptObject* sObject)
	{
		if (!isScriptObjectValid(sObject))
		{
			return false;
		}
		
		SecObject* obj = s_objectRefList[sObject->m_id].object;
		if (!obj || !obj->self)
		{
			return false;
		}

		return true;
	}

	void deleteObject(ScriptObject* sObject)
	{
		if (!doesObjectExist(sObject)) { return; }
	
		SecObject* obj = s_objectRefList[sObject->m_id].object;
		if (obj)
		{
			if (obj->entityFlags & ETFLAG_PLAYER || obj->flags & OBJ_FLAG_EYE)
			{
				TFE_FrontEndUI::logToConsole("Deleting the player or eye object is not allowed.");
				return;
			}
			
			freeObject(obj);
		}
	}

	ScriptSector getSector(ScriptObject* sObject)
	{
		if (doesObjectExist(sObject))
		{
			SecObject* obj = s_objectRefList[sObject->m_id].object;
			if (obj && obj->sector)
			{
				ScriptSector sSector(obj->sector->index);
				return sSector;
			}
		}

		// not found
		ScriptSector sSector(-1);
		return sSector;
	}

	bool isPlayer(ScriptObject* sObject)
	{
		if (!doesObjectExist(sObject)) { return false; }

		SecObject* obj = TFE_Jedi::s_objectRefList[sObject->m_id].object;
		if (obj && obj->entityFlags & ETFLAG_PLAYER)
		{
			return true;
		}

		return false;
	}

	vec3_float getPosition(ScriptObject* sObject)
	{
		if (!doesObjectExist(sObject)) { return { 0, 0, 0 }; }

		SecObject* obj = TFE_Jedi::s_objectRefList[sObject->m_id].object;
		if (obj)
		{
			return
			{
				fixed16ToFloat(obj->posWS.x),
				-fixed16ToFloat(obj->posWS.y),
				fixed16ToFloat(obj->posWS.z),
			};
		}

		return { 0, 0, 0 };
	}

	float getYaw(ScriptObject* sObject)
	{
		if (!doesObjectExist(sObject)) { return 0; }

		SecObject* obj = TFE_Jedi::s_objectRefList[sObject->m_id].object;
		if (obj)
		{
			return angleToFloat(obj->yaw);
		}

		return 0;
	}

	float getWorldWidth(ScriptObject* sObject)
	{
		if (!doesObjectExist(sObject)) { return 0; }

		SecObject* obj = TFE_Jedi::s_objectRefList[sObject->m_id].object;
		if (obj)
		{
			return fixed16ToFloat(obj->worldWidth);
		}

		return 0;
	}

	float getWorldHeight(ScriptObject* sObject)
	{
		if (!doesObjectExist(sObject)) { return 0; }

		SecObject* obj = TFE_Jedi::s_objectRefList[sObject->m_id].object;
		if (obj)
		{
			return fixed16ToFloat(obj->worldHeight);
		}

		return 0;
	}

	void setPosition(vec3_float position, ScriptObject* sObject)
	{
		if (!doesObjectExist(sObject)) { return; }

		SecObject* obj = TFE_Jedi::s_objectRefList[sObject->m_id].object;
		if (!obj) { return; }

		fixed16_16 newX = floatToFixed16(position.x);
		fixed16_16 newY = -floatToFixed16(position.y);
		fixed16_16 newZ = floatToFixed16(position.z);
		
		// Check if object has moved out of its current sector
		// Start with a bounds check
		bool outOfHorzBounds = newX < obj->sector->boundsMin.x || newX > obj->sector->boundsMax.x || 
			newZ < obj->sector->boundsMin.z || newZ > obj->sector->boundsMax.z;
		bool outOfVertBounds = newY < obj->sector->ceilingHeight || newY > obj->sector->floorHeight;

		// If within bounds, check if it has passed through a wall
		RWall* collisionWall = nullptr;
		if (!outOfHorzBounds && !outOfVertBounds)
		{
			collisionWall = collision_wallCollisionFromPath(obj->sector, obj->posWS.x, obj->posWS.z, newX, newZ);
		}

		if (collisionWall || outOfHorzBounds || outOfVertBounds)
		{
			// Try to find a new sector for the object
			RSector* sector = sector_which3D(newX, newY, newZ);
			if (sector && (sector != obj->sector))
			{
				sector_addObject(sector, obj);
			}
		}

		obj->posWS.x = newX;
		obj->posWS.y = newY;
		obj->posWS.z = newZ;
	}

	void matchPositionToTargetObj(ScriptObject targetSObject, ScriptObject* sObject)
	{
		if (!doesObjectExist(sObject) || !doesObjectExist(&targetSObject)) { return; }

		SecObject* obj = TFE_Jedi::s_objectRefList[sObject->m_id].object;
		SecObject* targetObj = TFE_Jedi::s_objectRefList[(targetSObject.m_id)].object;
		if (!obj || !targetObj) { return; }

		obj->posWS.x = targetObj->posWS.x;
		obj->posWS.y = targetObj->posWS.y;
		obj->posWS.z = targetObj->posWS.z;

		if (targetObj->sector && targetObj->sector != obj->sector)
		{
			sector_addObject(targetObj->sector, obj);
		}
	}
	
	void matchPositionToTargetObjByName(std::string targetObjName, ScriptObject* sObject)
	{
		ScriptObject targetScriptObj = GS_Level::getObjectByName(targetObjName);
		matchPositionToTargetObj(targetScriptObj, sObject);
	}

	void setYaw(float yaw, ScriptObject* sObject)
	{
		if (!doesObjectExist(sObject)) { return; }

		SecObject* obj = TFE_Jedi::s_objectRefList[sObject->m_id].object;
		if (!obj) { return; }

		if (yaw > 360.0) { yaw -= 360.0; }
		if (yaw < -360.0) { yaw += 360.0; }
		obj->yaw = floatToAngle(yaw);

		if (obj->type == OBJ_TYPE_3D)
		{
			obj3d_computeTransform(obj);
		}
	}

	void setWorldWidth(float wWidth, ScriptObject* sObject)
	{
		if (!doesObjectExist(sObject)) { return; }

		SecObject* obj = TFE_Jedi::s_objectRefList[sObject->m_id].object;
		if (obj)
		{
			wWidth = max(0.0f, wWidth);
			obj->worldWidth = floatToFixed16(wWidth);
		}
	}

	void setWorldHeight(float wHeight, ScriptObject* sObject)
	{
		if (!doesObjectExist(sObject)) { return; }

		SecObject* obj = TFE_Jedi::s_objectRefList[sObject->m_id].object;
		if (obj)
		{
			obj->worldHeight = floatToFixed16(wHeight);
		}
	}

	void addLogicToObject(std::string logic, ScriptObject* sObject)
	{
		if (!doesObjectExist(sObject)) { return; }

		SecObject* obj = TFE_Jedi::s_objectRefList[sObject->m_id].object;
		KEYWORD logicId = getKeywordIndex(logic.c_str());

		if (logicId == KW_UNKNOWN) { return; }
		if (logicId == KW_ANIM)
		{
			obj_setSpriteAnim(obj);
		}
		else if (logicId >= KW_TROOP && logicId <= KW_SCENERY)
		{
			obj_setEnemyLogic(obj, logicId);
		}
		else if (logicId >= KW_BATTERY && logicId <= KW_AUTOGUN)
		{
			obj_createPickup(obj, getPickupItemId(logic.c_str()));
		}
		// TODO enable adding custom logic
	}

	void setCamera(ScriptObject* sObject)
	{
		if (!doesObjectExist(sObject)) { return; }

		SecObject* obj = TFE_Jedi::s_objectRefList[sObject->m_id].object;
		player_setupEyeObject(obj);
		s_externalCameraMode = obj == s_playerObject ? JFALSE : JTRUE;
		if (s_nightVisionActive) { disableNightVision(); }
	}


	void sendMessageToObject(MessageType messageType, ScriptObject* sObject)
	{
		if (!doesObjectExist(sObject)) { return; }
		
		SecObject* obj = TFE_Jedi::s_objectRefList[sObject->m_id].object;
		message_sendToObj(obj, messageType, actor_messageFunc);
	}

	//////////////////////////////////////////////////////
	// Logic related functionality
	//////////////////////////////////////////////////////

	// Helper function - gets the first Dispatch actor linked to an object
	ActorDispatch* getDispatch(SecObject* obj)
	{
		Logic** logicPtr = (Logic**)allocator_getHead((Allocator*)obj->logic);
		while (logicPtr)
		{
			Logic* logic = *logicPtr;
			if (logic->type == LOGIC_DISPATCH)
			{
				return (ActorDispatch*)logic;
			}
			logicPtr = (Logic**)allocator_getNext((Allocator*)obj->logic);
		}

		return nullptr;
	}

	// Helper function - gets the damage module from a dispatch logic
	DamageModule* getDamageModule(ActorDispatch* dispatch)
	{
		for (s32 i = 0; i < ACTOR_MAX_MODULES; i++)
		{
			ActorModule* module = dispatch->modules[ACTOR_MAX_MODULES - 1 - i];
			if (module && module->type == ACTMOD_DAMAGE)
			{
				return (DamageModule*)module;
			}
		}

		return nullptr;
	}

	// Helper function - gets the attack module from a dispatch logic
	AttackModule* getAttackModule(ActorDispatch* dispatch)
	{
		for (s32 i = 0; i < ACTOR_MAX_MODULES; i++)
		{
			ActorModule* module = dispatch->modules[ACTOR_MAX_MODULES - 1 - i];
			if (module && module->type == ACTMOD_ATTACK)
			{
				return (AttackModule*)module;
			}
		}

		return nullptr;
	}

	int getHitPoints(ScriptObject* sObject)
	{
		if (!doesObjectExist(sObject)) { return -1; }
		SecObject* obj = TFE_Jedi::s_objectRefList[sObject->m_id].object;

		// First try to find a dispatch logic
		ActorDispatch* dispatch = getDispatch(obj);
		if (dispatch)
		{
			DamageModule* damageMod = getDamageModule(dispatch);
			if (damageMod)
			{
				return floor16(damageMod->hp);
			}
		}

		// Then try other logics
		Logic** logicPtr = (Logic**)allocator_getHead((Allocator*)obj->logic);
		while (logicPtr)
		{
			Logic* logic = *logicPtr;
			PhysicsActor* actor = nullptr;

			switch (logic->type)
			{
				case LOGIC_BOBA_FETT:
				case LOGIC_DRAGON:
				case LOGIC_PHASE_ONE:
				case LOGIC_PHASE_TWO:
				case LOGIC_PHASE_THREE:
				case LOGIC_TURRET:
				case LOGIC_WELDER:
				case LOGIC_MOUSEBOT:
					SpecialActor* data = (SpecialActor*)logic;
					actor = &data->actor;
					break;
			}
			
			if (actor)
			{
				return floor16(actor->hp);
			}
			
			logicPtr = (Logic**)allocator_getNext((Allocator*)obj->logic);
		}

		return -1;
	}

	void setHitPoints(int hitpoints, ScriptObject* sObject)
	{
		if (!doesObjectExist(sObject)) { return; }
		SecObject* obj = TFE_Jedi::s_objectRefList[sObject->m_id].object;

		// First try to find a dispatch logic
		ActorDispatch* dispatch = getDispatch(obj);
		if (dispatch)
		{
			DamageModule* damageMod = getDamageModule(dispatch);
			if (damageMod)
			{
				damageMod->hp = FIXED(hitpoints);
				return;
			}
		}

		// Then try other logics
		Logic** logicPtr = (Logic**)allocator_getHead((Allocator*)obj->logic);
		while (logicPtr)
		{
			Logic* logic = *logicPtr;
			PhysicsActor* actor = nullptr;

			switch (logic->type)
			{
				case LOGIC_BOBA_FETT:
				case LOGIC_DRAGON:
				case LOGIC_PHASE_ONE:
				case LOGIC_PHASE_TWO:
				case LOGIC_PHASE_THREE:
				case LOGIC_TURRET:
				case LOGIC_WELDER:
				case LOGIC_MOUSEBOT:
					SpecialActor* data = (SpecialActor*)logic;
					actor = &data->actor;
					break;
			}

			if (actor)
			{
				actor->hp = FIXED(hitpoints);
				return;
			}

			logicPtr = (Logic**)allocator_getNext((Allocator*)obj->logic);
		}
	}

	void setProjectile(ProjectileType projectile, ScriptObject* sObject)
	{
		if (!doesObjectExist(sObject)) { return; }
		SecObject* obj = TFE_Jedi::s_objectRefList[sObject->m_id].object;

		ActorDispatch* dispatch = getDispatch(obj);
		if (dispatch)
		{
			AttackModule* attackMod = getAttackModule(dispatch);
			if (attackMod)
			{
				attackMod->projType = projectile;
			}
		}
	}
	
	vec3_float getVelocity(ScriptObject* sObject)
	{
		if (!doesObjectExist(sObject)) { return { 0, 0, 0,}; }
		SecObject* obj = TFE_Jedi::s_objectRefList[sObject->m_id].object;

		// Is it the player?
		if (obj->entityFlags & ETFLAG_PLAYER)
		{
			return getPlayerVelocity();
		}

		// First try to find a dispatch logic
		ActorDispatch* dispatch = getDispatch(obj);
		if (dispatch)
		{
			return
			{
				fixed16ToFloat(dispatch->vel.x),
				-fixed16ToFloat(dispatch->vel.y),
				fixed16ToFloat(dispatch->vel.z)
			};
		}

		// Then try other logics
		Logic** logicPtr = (Logic**)allocator_getHead((Allocator*)obj->logic);
		while (logicPtr)
		{
			Logic* logic = *logicPtr;
			PhysicsActor* actor = nullptr;

			switch (logic->type)
			{
			case LOGIC_BOBA_FETT:
			case LOGIC_DRAGON:
			case LOGIC_PHASE_ONE:
			case LOGIC_PHASE_TWO:
			case LOGIC_PHASE_THREE:
			case LOGIC_TURRET:
			case LOGIC_WELDER:
			case LOGIC_MOUSEBOT:
				SpecialActor* data = (SpecialActor*)logic;
				actor = &data->actor;
				break;
			}

			if (actor)
			{
				return
				{
					fixed16ToFloat(actor->vel.x),
					-fixed16ToFloat(actor->vel.y),
					fixed16ToFloat(actor->vel.z)
				};
			}

			logicPtr = (Logic**)allocator_getNext((Allocator*)obj->logic);
		}

		return { 0, 0, 0 };
	}

	void setVelocity(vec3_float vel, ScriptObject* sObject)
	{
		if (!doesObjectExist(sObject)) { return; }
		SecObject* obj = TFE_Jedi::s_objectRefList[sObject->m_id].object;

		// Is it the player?
		if (obj->entityFlags & ETFLAG_PLAYER)
		{
			setPlayerVelocity(vel);
			return;
		}
		
		// First try to find a dispatch logic
		ActorDispatch* dispatch = getDispatch(obj);
		if (dispatch)
		{
			dispatch->vel.x = floatToFixed16(vel.x);
			dispatch->vel.y = -floatToFixed16(vel.y);
			dispatch->vel.z = floatToFixed16(vel.z);
			return;
		}

		// Then try other logics
		Logic** logicPtr = (Logic**)allocator_getHead((Allocator*)obj->logic);
		while (logicPtr)
		{
			Logic* logic = *logicPtr;
			PhysicsActor* actor = nullptr;

			switch (logic->type)
			{
			case LOGIC_BOBA_FETT:
			case LOGIC_DRAGON:
			case LOGIC_PHASE_ONE:
			case LOGIC_PHASE_TWO:
			case LOGIC_PHASE_THREE:
			case LOGIC_TURRET:
			case LOGIC_WELDER:
			case LOGIC_MOUSEBOT:
				SpecialActor* data = (SpecialActor*)logic;
				actor = &data->actor;
				break;
			}

			if (actor)
			{
				actor->vel.x = floatToFixed16(vel.x);
				actor->vel.y = -floatToFixed16(vel.y);
				actor->vel.z = floatToFixed16(vel.z);
				return;
			}

			logicPtr = (Logic**)allocator_getNext((Allocator*)obj->logic);
		}
	}

	void ScriptObject::registerType()
	{
		s32 res = 0;
		asIScriptEngine* engine = (asIScriptEngine*)TFE_ForceScript::getEngine();

		ScriptValueType("Object");
		// Variables
		ScriptMemberVariable("int id", m_id);

		// Enums
		ScriptEnumRegister("Projectiles");
		ScriptEnumStr(PROJ_PUNCH);
		ScriptEnumStr(PROJ_PISTOL_BOLT);
		ScriptEnumStr(PROJ_RIFLE_BOLT);
		ScriptEnumStr(PROJ_THERMAL_DET);
		ScriptEnumStr(PROJ_REPEATER);
		ScriptEnumStr(PROJ_PLASMA);
		ScriptEnumStr(PROJ_MORTAR);
		ScriptEnumStr(PROJ_LAND_MINE);
		ScriptEnumStr(PROJ_LAND_MINE_PROX);
		ScriptEnumStr(PROJ_LAND_MINE_PLACED);
		ScriptEnumStr(PROJ_CONCUSSION);
		ScriptEnumStr(PROJ_CANNON);
		ScriptEnumStr(PROJ_MISSILE);
		ScriptEnumStr(PROJ_TURRET_BOLT);
		ScriptEnumStr(PROJ_REMOTE_BOLT);
		ScriptEnumStr(PROJ_EXP_BARREL);
		ScriptEnumStr(PROJ_HOMING_MISSILE);
		ScriptEnumStr(PROJ_PROBE_PROJ);
		ScriptEnum("PROJ_BOBAFETT_BALL", PROJ_BOBAFET_BALL);
	}

	// Function registration has been separated from Type registration
	// The ScriptObject type needs to be registered before registering ScriptSector functions
	// ScriptObject functions need to be registered after registering ScriptSector
	void ScriptObject::registerFunctions()
	{
		s32 res = 0;
		asIScriptEngine* engine = (asIScriptEngine*)TFE_ForceScript::getEngine();
		s_typeName = "Object";

		// Checks
		ScriptObjFunc("bool isValid()", isScriptObjectValid);
		ScriptObjFunc("bool exists()", doesObjectExist);
		ScriptObjFunc("bool isPlayer()", isPlayer);

		// Getters
		ScriptObjFunc("Sector getSector()", getSector);
		ScriptPropertyGetFunc("Sector get_sector()", getSector);
		ScriptPropertyGetFunc("float3 get_position()", getPosition);
		ScriptPropertyGetFunc("float get_yaw()", getYaw);
		ScriptPropertyGetFunc("float get_radius()", getWorldWidth);
		ScriptPropertyGetFunc("float get_height()", getWorldHeight);		// Let's allow both sets of terminology here
		ScriptPropertyGetFunc("float get_worldWidth()", getWorldWidth);		// radius = world width
		ScriptPropertyGetFunc("float get_worldHeight()", getWorldHeight);	// height = world height

		// Setters
		ScriptPropertySetFunc("void set_position(float3)", setPosition);
		ScriptPropertySetFunc("void set_yaw(float)", setYaw);
		ScriptPropertySetFunc("void set_radius(float)", setWorldWidth);
		ScriptPropertySetFunc("void set_height(float)", setWorldHeight);
		ScriptPropertySetFunc("void set_worldWidth(float)", setWorldWidth);
		ScriptPropertySetFunc("void set_worldHeight(float)", setWorldHeight);
		ScriptObjFunc("void matchPosition(Object)", matchPositionToTargetObj);
		ScriptObjFunc("void matchPosition(string)", matchPositionToTargetObjByName);
		
		// Other functions
		ScriptObjFunc("void delete()", deleteObject);
		ScriptObjFunc("void addLogic(string)", addLogicToObject);
		ScriptObjFunc("void setCamera()", setCamera);
		ScriptObjFunc("void sendMessage(int)", sendMessageToObject);

		// Logic getters & setters
		ScriptPropertyGetFunc("int get_hitPoints()", getHitPoints);
		ScriptPropertySetFunc("void set_hitPoints(int)", setHitPoints);
		ScriptPropertySetFunc("void set_projectile(int)", setProjectile);
		ScriptPropertyGetFunc("float3 get_velocity()", getVelocity);
		ScriptPropertySetFunc("void set_velocity(float3)", setVelocity);
	}
}
