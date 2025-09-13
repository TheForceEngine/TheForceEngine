#include "gs_level.h"
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

namespace TFE_DarkForces
{
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

	void ScriptObject::registerType()
	{
		s32 res = 0;
		asIScriptEngine* engine = (asIScriptEngine*)TFE_ForceScript::getEngine();

		ScriptValueType("Object");
		// Variables
		ScriptMemberVariable("int id", m_id);

		// Checks
		ScriptObjFunc("bool isValid()", isScriptObjectValid);
		ScriptObjFunc("bool exists()", doesObjectExist);
		ScriptObjFunc("bool isPlayer()", isPlayer);

		// Getters
		ScriptObjFunc("Sector getSector()", getSector);
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
		ScriptObjFunc("void sendMessage(int)", sendMessageToObject)
	}
}
