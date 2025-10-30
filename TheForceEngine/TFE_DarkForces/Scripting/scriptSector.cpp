#include "scriptSector.h"
#include "scriptWall.h"
#include "scriptTexture.h"
#include "scriptObject.h"
#include <TFE_ForceScript/ScriptAPI-Shared/scriptMath.h>
#include <TFE_ForceScript/Angelscript/add_on/scriptarray/scriptarray.h>
#include <TFE_Jedi/Level/levelData.h>
#include <TFE_Jedi/Level/rwall.h>
#include <TFE_Jedi/Level/rsector.h>
#include <TFE_Jedi/Level/robjData.h>
#include <TFE_Jedi/InfSystem/message.h>
#include <angelscript.h>

using namespace TFE_Jedi;

namespace TFE_DarkForces
{
	f32 getFloorHeight(ScriptSector* sector) { return sector->m_id >= 0 ? -fixed16ToFloat(s_levelState.sectors[sector->m_id].floorHeight) : 0; }
	f32 getCeilHeight(ScriptSector* sector) { return sector->m_id >= 0 ? -fixed16ToFloat(s_levelState.sectors[sector->m_id].ceilingHeight) : 0; }
	f32 getSecondHeight(ScriptSector* sector) { return sector->m_id >= 0 ? -fixed16ToFloat(s_levelState.sectors[sector->m_id].secHeight) : 0; }
	s32 getWallCount(ScriptSector* sector) { return sector->m_id >= 0 ? s_levelState.sectors[sector->m_id].wallCount : 0; }
	f32 getAmbient(ScriptSector* sector) { return sector->m_id >= 0 ? fixed16ToFloat(s_levelState.sectors[sector->m_id].ambient) : 0; }

	void setFloorHeight(f32 height, ScriptSector* sector)
	{
		if (sector->m_id < 0) { return; }
		height = -height;

		RSector* lvlSector = &s_levelState.sectors[sector->m_id];
		const fixed16_16 offset = floatToFixed16(height) - lvlSector->floorHeight;
		if (offset != 0)
		{
			sector_adjustTextureWallOffsets_Floor(lvlSector, offset);
			sector_adjustTextureMirrorOffsets_Floor(lvlSector, offset);
			sector_adjustHeights(lvlSector, offset, 0, 0);
		}
	}
	void setCeilHeight(f32 height, ScriptSector* sector)
	{
		if (sector->m_id < 0) { return; }
		height = -height;

		const fixed16_16 offset = floatToFixed16(height) - s_levelState.sectors[sector->m_id].ceilingHeight;
		sector_adjustHeights(&s_levelState.sectors[sector->m_id], 0, offset, 0);
	}
	void setSecondHeight(f32 height, ScriptSector* sector)
	{
		if (sector->m_id < 0) { return; }
		height = -height;

		const fixed16_16 offset = floatToFixed16(height) - s_levelState.sectors[sector->m_id].secHeight;
		sector_adjustHeights(&s_levelState.sectors[sector->m_id], 0, 0, offset);
	}
	void setAmbient(f32 ambient, ScriptSector* sector)
	{
		if (sector->m_id < 0) { return; }
		s_levelState.sectors[sector->m_id].ambient = floatToFixed16(ambient);
		s_levelState.sectors[sector->m_id].dirtyFlags |= SDF_AMBIENT;
	}
	bool isScriptSectorValid(ScriptSector* sector)
	{
		return sector->m_id >= 0 && sector->m_id < (s32)s_levelState.sectorCount;
	}
	bool isSectorFlagSet(s32 index, u32 flag, ScriptSector* sector)
	{
		if (!isScriptSectorValid(sector)) { return false; }
		if (index == 1) { return (s_levelState.sectors[sector->m_id].flags1 & flag) != 0u; }
		else if (index == 2) { return (s_levelState.sectors[sector->m_id].flags2 & flag) != 0u; }
		else if (index == 3) { return (s_levelState.sectors[sector->m_id].flags3 & flag) != 0u; }
		return false;
	}
	void clearSectorFlag(s32 index, u32 flag, ScriptSector* sector)
	{
		if (!isScriptSectorValid(sector)) { return; }
		if (index == 1) { s_levelState.sectors[sector->m_id].flags1 &= ~flag; }
		else if (index == 2) { s_levelState.sectors[sector->m_id].flags2 &= ~flag; }
		else if (index == 3) { s_levelState.sectors[sector->m_id].flags3 &= ~flag; }
	}
	void setSectorFlag(s32 index, u32 flag, ScriptSector* sector)
	{
		if (!isScriptSectorValid(sector)) { return; }
		if (index == 1) { s_levelState.sectors[sector->m_id].flags1 |= flag; }
		else if (index == 2) { s_levelState.sectors[sector->m_id].flags2 |= flag; }
		else if (index == 3) { s_levelState.sectors[sector->m_id].flags3 |= flag; }
	}
	TFE_ForceScript::float2 getCenterXZ(ScriptSector* sector)
	{
		TFE_ForceScript::float2 cen(0.0f, 0.0f);
		if (!isScriptSectorValid(sector)) { return cen; }
		cen.x = fixed16ToFloat(s_levelState.sectors[sector->m_id].boundsMin.x + s_levelState.sectors[sector->m_id].boundsMax.x) * 0.5f;
		cen.y = fixed16ToFloat(s_levelState.sectors[sector->m_id].boundsMin.z + s_levelState.sectors[sector->m_id].boundsMax.z) * 0.5f;
		return cen;
	}
	ScriptWall getWall(s32 index, ScriptSector* sector)
	{
		ScriptWall wall(-1, -1);
		if (isScriptSectorValid(sector))
		{
			if (index >= 0 && index < s_levelState.sectors[sector->m_id].wallCount)
			{
				wall.m_sectorId = sector->m_id;
				wall.m_wallId = index;
			}
		}
		return wall;
	}
	ScriptTexture getFloorTexture(ScriptSector* sector)
	{
		ScriptTexture tex(-1);
		if (isScriptSectorValid(sector))
		{
			tex.m_id = ScriptTexture::getTextureIdFromData(s_levelState.sectors[sector->m_id].floorTex);
		}
		return tex;
	}
	ScriptTexture getCeilTexture(ScriptSector* sector)
	{
		ScriptTexture tex(-1);
		if (isScriptSectorValid(sector))
		{
			tex.m_id = ScriptTexture::getTextureIdFromData(s_levelState.sectors[sector->m_id].ceilTex);
		}
		return tex;
	}
	TFE_ForceScript::float2 getFloorTextureOffset(ScriptSector* sector)
	{
		TFE_ForceScript::float2 offset(0.0f, 0.0f);
		if (isScriptSectorValid(sector))
		{
			offset.x = fixed16ToFloat(s_levelState.sectors[sector->m_id].floorOffset.x);
			offset.y = fixed16ToFloat(s_levelState.sectors[sector->m_id].floorOffset.z);
		}
		return offset;
	}
	TFE_ForceScript::float2 getCeilTextureOffset(ScriptSector* sector)
	{
		TFE_ForceScript::float2 offset(0.0f, 0.0f);
		if (isScriptSectorValid(sector))
		{
			offset.x = fixed16ToFloat(s_levelState.sectors[sector->m_id].ceilOffset.x);
			offset.y = fixed16ToFloat(s_levelState.sectors[sector->m_id].ceilOffset.z);
		}
		return offset;
	}
	void setFloorTexture(ScriptTexture tex, ScriptSector* sector)
	{
		if (isScriptSectorValid(sector) && ScriptTexture::isTextureValid(&tex))
		{
			s_levelState.sectors[sector->m_id].floorTex = &s_levelState.textures[tex.m_id];
		}
	}
	void setCeilTexture(ScriptTexture tex, ScriptSector* sector)
	{
		if (isScriptSectorValid(sector) && ScriptTexture::isTextureValid(&tex))
		{
			s_levelState.sectors[sector->m_id].ceilTex = &s_levelState.textures[tex.m_id];
		}
	}
	void setFloorTextureOffset(TFE_ForceScript::float2 offset, ScriptSector* sector)
	{
		if (isScriptSectorValid(sector))
		{
			s_levelState.sectors[sector->m_id].floorOffset.x = floatToFixed16(offset.x);
			s_levelState.sectors[sector->m_id].floorOffset.z = floatToFixed16(offset.y);
		}
	}
	void setCeilTextureOffset(TFE_ForceScript::float2 offset, ScriptSector* sector)
	{
		if (isScriptSectorValid(sector))
		{
			s_levelState.sectors[sector->m_id].ceilOffset.x = floatToFixed16(offset.x);
			s_levelState.sectors[sector->m_id].ceilOffset.z = floatToFixed16(offset.y);
		}
	}

	// Message with event and arg, eg. GOTO_STOP 131072 2
	void sendMessageToSector(MessageType messageType, u32 evt, u32 msgArg1, ScriptSector* sSector)
	{
		if (!isScriptSectorValid(sSector))
		{
			return;
		}

		// TODO: investigate how to do this safely
		// s_msgArg1 = msgArg1;

		RSector* sector = &s_levelState.sectors[sSector->m_id];
		message_sendToSector(sector, nullptr, evt, messageType);
	}

	// Message with no event and no arg, eg. NEXT_STOP
	void sendMessageToSector1(MessageType messageType, ScriptSector* sSector)
	{
		sendMessageToSector(messageType, 0, 0, sSector);
	}

	// Message with event, eg. NEXT_STOP 131072
	void sendMessageToSector2(MessageType messageType, u32 evt, ScriptSector* sSector)
	{
		sendMessageToSector(messageType, evt, 0, sSector);
	}

	void getSectorObjects(CScriptArray& results, ScriptSector* sSector)
	{
		results.Resize(0);
		if (!isScriptSectorValid(sSector)) { return; }
		
		RSector* sector = &s_levelState.sectors[sSector->m_id];
		SecObject** objList = sector->objectList;
		for (s32 i = 0, idx = 0; i < sector->objectCount && idx < sector->objectCapacity; idx++)
		{
			SecObject* obj = objList[idx];
			if (obj)
			{
				s32 objIndex = obj_getRefIndex(obj);
				ScriptObject sObj(objIndex);
				results.InsertLast(&sObj);
			}
		}
	}

	void ScriptSector::registerType()
	{
		s32 res = 0;
		asIScriptEngine* engine = (asIScriptEngine*)TFE_ForceScript::getEngine();
		// TODO: Objects

		ScriptValueType("Sector");
		// Variables
		ScriptMemberVariable("int id", m_id);
		// Functions
		ScriptObjFunc("bool isValid()", isScriptSectorValid);
		ScriptObjFunc("bool isFlagSet(int, uint)", isSectorFlagSet);
		ScriptObjFunc("void clearFlag(int, uint)", clearSectorFlag);
		ScriptObjFunc("void setFlag(int, uint)", setSectorFlag);
		ScriptObjFunc("float2 getCenterXZ()", getCenterXZ);
		ScriptObjFunc("Wall getWall(int)", getWall);
		ScriptObjFunc("void getObjects(array<Object>&)", getSectorObjects);

		ScriptObjFunc("void sendMessage(int)", sendMessageToSector1);
		ScriptObjFunc("void sendMessage(int, uint)", sendMessageToSector2);
		//ScriptObjFunc("void sendMessage(int, uint, uint)", sendMessageToSector);		// For now do not expose the ability to pass an arg

		// Properties
		ScriptPropertyGetFunc("float get_floorHeight()", getFloorHeight);
		ScriptPropertyGetFunc("float get_ceilHeight()", getCeilHeight);
		ScriptPropertyGetFunc("float get_secondHeight()", getSecondHeight);
		ScriptPropertyGetFunc("float get_ambient()", getAmbient);
		ScriptPropertyGetFunc("int get_wallCount()", getWallCount);
		ScriptPropertyGetFunc("LevelTexture get_floorTexture()", getFloorTexture);
		ScriptPropertyGetFunc("LevelTexture get_ceilTexture()", getCeilTexture);
		ScriptPropertyGetFunc("float2 get_floorTextureOffset()", getFloorTextureOffset);
		ScriptPropertyGetFunc("float2 get_ceilTextureOffset()", getCeilTextureOffset);
		ScriptPropertySetFunc("void set_floorHeight(float)", setFloorHeight);
		ScriptPropertySetFunc("void set_ceilHeight(float)", setCeilHeight);
		ScriptPropertySetFunc("void set_secondHeight(float)", setSecondHeight);
		ScriptPropertySetFunc("void set_ambient(float)", setAmbient);
		ScriptPropertySetFunc("void set_floorTexture(LevelTexture)", setFloorTexture);
		ScriptPropertySetFunc("void set_ceilTexture(LevelTexture)", setCeilTexture);
		ScriptPropertySetFunc("void set_floorTextureOffset(float2)", setFloorTextureOffset);
		ScriptPropertySetFunc("void set_ceilTextureOffset(float2)", setCeilTextureOffset);
	}
}
