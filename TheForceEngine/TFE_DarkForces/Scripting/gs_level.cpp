#include <cstring>
#include "gs_level.h"
#include "scriptTexture.h"
#include "scriptElev.h"
#include "scriptWall.h"
#include "scriptSector.h"
#include "scriptObject.h"
#include <TFE_DarkForces/agent.h>
#include <TFE_DarkForces/player.h>
#include <TFE_DarkForces/projectile.h>
#include <TFE_System/system.h>
#include <TFE_ForceScript/ScriptAPI-Shared/scriptMath.h>
#include <TFE_ForceScript/Angelscript/add_on/scriptarray/scriptarray.h>
#include <TFE_Jedi/InfSystem/infState.h>
#include <TFE_Jedi/InfSystem/infTypesInternal.h>
#include <TFE_Jedi/InfSystem/infSystem.h>
#include <TFE_Jedi/Level/levelData.h>
#include <TFE_Jedi/Level/rwall.h>
#include <TFE_Jedi/Level/rsector.h>
#include <TFE_Jedi/Level/rtexture.h>

#include <angelscript.h>

using namespace TFE_Jedi;

namespace TFE_DarkForces
{
	static u32 s_lsSearchKey = 1002; // any non-zero value really.

	enum SectorProperty
	{
		SECTORPROP_NONE = 0,
		SECTORPROP_FLOOR_HEIGHT = FLAG_BIT(0),
		SECTORPROP_CEIL_HEIGHT = FLAG_BIT(1),
		SECTORPROP_SECOND_HEIGHT = FLAG_BIT(2),
		SECTORPROP_FLOOR_TEX = FLAG_BIT(3),
		SECTORPROP_CEIL_TEX = FLAG_BIT(4),
		SECTORPROP_AMBIENT = FLAG_BIT(5),
		SECTORPROP_START = SECTORPROP_FLOOR_HEIGHT,
		SECTORPROP_END = SECTORPROP_AMBIENT << 1,
	};

	ScriptSector GS_Level::getSectorById(s32 id)
	{
		if (id < 0 || id >= (s32)s_levelState.sectorCount)
		{
			TFE_System::logWrite(LOG_ERROR, "Level Script", "Runtime error, invalid sectorID %d.", id);
			id = -1; // Invalid ID, sector.isValid() == false
		}
		ScriptSector sector(id);
		return sector;
	}

	ScriptSector GS_Level::getSectorByName(std::string name)
	{
		const char* sectorName = name.c_str();
		
		MessageAddress* msgAddr = message_getAddress(sectorName);
		if (msgAddr)
		{
			u32 sectorCount = s_levelState.sectorCount;
			for (u32 s = 0; s < sectorCount; s++)
			{
				RSector* sec = &s_levelState.sectors[s];
				if (sec == msgAddr->sector)
				{
					ScriptSector sector(s);
					return sector;
				}
			}
		}

		// Could not find the sector
		ScriptSector sector(-1);
		return sector;
	}

	ScriptElev GS_Level::getElevator(s32 id)
	{
		if (id < 0 || id >= allocator_getCount(s_infSerState.infElevators))
		{
			TFE_System::logWrite(LOG_ERROR, "Level Script", "Runtime error, invalid elevator ID %d.", id);
			id = -1; // Invalid ID, elevator.isValid() == false
		}
		ScriptElev elev(id);
		return elev;
	}

	bool doSectorPropMatch(RSector* s0, RSector* s1, u32 prop)
	{
		switch (prop)
		{
			case SECTORPROP_FLOOR_HEIGHT:
			{
				return s0->floorHeight == s1->floorHeight;
			} break;
			case SECTORPROP_CEIL_HEIGHT:
			{
				return s0->ceilingHeight == s1->ceilingHeight;
			} break;
			case SECTORPROP_SECOND_HEIGHT:
			{
				return s0->secHeight == s1->secHeight;
			} break;
			case SECTORPROP_FLOOR_TEX:
			{
				return s0->floorTex == s1->floorTex;
			} break;
			case SECTORPROP_CEIL_TEX:
			{
				return s0->ceilTex == s1->ceilTex;
			} break;
			case SECTORPROP_AMBIENT:
			{
				return s0->ambient == s1->ambient;
			} break;
			default:
			{
				// Invalid property.
				assert(0);
			}
		}
		return false;
	}

	void GS_Level::findConnectedSectors(ScriptSector initSector, u32 matchProp, CScriptArray& results)
	{
		results.Resize(0);
		// Return an empty array if the init sector is invalid.
		if (!isScriptSectorValid(&initSector)) { return; }

		// Push the initial sector onto the array as the first element.
		results.InsertLast(&initSector);

		s_lsSearchKey++;
		std::vector<RSector*> stack;
		RSector* baseSector = &s_levelState.sectors[initSector.m_id];
		baseSector->searchKey = s_lsSearchKey;

		stack.push_back(baseSector);
		while (!stack.empty())
		{
			RSector* sector = stack.back();
			stack.pop_back();

			const s32 wallCount = sector->wallCount;
			RWall* wall = sector->walls;
			for (s32 w = 0; w < wallCount; w++, wall++)
			{
				if (!wall->nextSector) { continue; }
							   
				RSector* next = wall->nextSector;
				// Don't search sectors already touched.
				if (next->searchKey == s_lsSearchKey) { continue; }
				next->searchKey = s_lsSearchKey;

				bool propMatch = true;
				u32 prop = SECTORPROP_START;
				while (prop < SECTORPROP_END && propMatch)
				{
					if (prop & matchProp)
					{
						propMatch = propMatch && doSectorPropMatch(next, baseSector, prop);
					}
					prop <<= 1;
				}

				if (propMatch)
				{
					stack.push_back(next);

					ScriptSector nextSector(next->id);
					results.InsertLast(&nextSector);
				}
			}
		}
	}

	void GS_Level::setGravity(s32 grav)
	{
		s_gravityAccel = FIXED(grav);
	}

	void GS_Level::setProjectileGravity(s32 pGrav)
	{
		setProjectileGravityAccel(FIXED(pGrav));
	}

	ScriptObject GS_Level::getObjectById(s32 id)
	{
		if (id < 0 || id >= s_objectRefList.size())
		{
			TFE_System::logWrite(LOG_ERROR, "Level Script", "Runtime error, invalid objectID %d.", id);
			id = -1;
		}
		
		ScriptObject object(id);
		return object;
	}

	ScriptObject GS_Level::getObjectByName(std::string name)
	{
		const char* cname = name.c_str();
		if (!cname || name.empty() || s_objectRefList.empty())
		{
			ScriptObject object(-1);
			return object;
		}

		s32 objectId = -1;
		for (s32 i = 0; i < s_objectRefList.size(); i++)
		{
			if (strcasecmp(cname, s_objectRefList[i].name) == 0)
			{
				objectId = i;
				break;
			}
		}
		
		ScriptObject object(objectId);
		return object;
	}

	// Objects will be contained in the results array
	void GS_Level::getAllObjectsByName(std::string name, CScriptArray& results)
	{
		const char* cname = name.c_str();
		if (!cname || name.empty() || s_objectRefList.empty())
		{
			return;
		}
		
		results.Resize(0);
		for (s32 i = 0; i < s_objectRefList.size(); i++)
		{
			if (strcasecmp(cname, s_objectRefList[i].name) == 0)
			{
				ScriptObject sObject(i);
				results.InsertLast(&sObject);
			}
		}
	}

	bool GS_Level::scriptRegister(ScriptAPI api)
	{
		ScriptElev scriptElev;
		ScriptTexture scriptTex;
		ScriptSector scriptSector;
		ScriptWall scriptWall;
		ScriptObject scriptObject;
		scriptElev.registerType();
		scriptTex.registerType();
		scriptWall.registerType();
		scriptObject.registerType();
		scriptSector.registerType();
		scriptObject.registerFunctions();
		
		ScriptClassBegin("Level", "level", api);
		{
			// Enums
			ScriptEnumRegister("SectorFlags1");
			ScriptEnum("SECTOR_FLAG1_EXTERIOR",        SEC_FLAGS1_EXTERIOR);
			ScriptEnum("SECTOR_FLAG1_DOOR",            SEC_FLAGS1_DOOR);
			ScriptEnum("SECTOR_FLAG1_MAG_SEAL",        SEC_FLAGS1_MAG_SEAL);
			ScriptEnum("SECTOR_FLAG1_EXTERIOR_ADJOIN", SEC_FLAGS1_EXT_ADJ);
			ScriptEnum("SECTOR_FLAG1_ICE_FLOOR",       SEC_FLAGS1_ICE_FLOOR);
			ScriptEnum("SECTOR_FLAG1_SNOW_FLOOR",      SEC_FLAGS1_SNOW_FLOOR);
			ScriptEnum("SECTOR_FLAG1_EXPLODING_WALL",  SEC_FLAGS1_EXP_WALL);
			ScriptEnum("SECTOR_FLAG1_PIT",             SEC_FLAGS1_PIT);
			ScriptEnum("SECTOR_FLAG1_PIT_ADJOIN",      SEC_FLAGS1_EXT_FLOOR_ADJ);
			ScriptEnum("SECTOR_FLAG1_CRUSHING",        SEC_FLAGS1_CRUSHING);
			ScriptEnum("SECTOR_FLAG1_NOWALL_DRAW",     SEC_FLAGS1_NOWALL_DRAW);
			ScriptEnum("SECTOR_FLAG1_LOW_DAMAGE",      SEC_FLAGS1_LOW_DAMAGE);
			ScriptEnum("SECTOR_FLAG1_HIGH_DAMAGE",     SEC_FLAGS1_HIGH_DAMAGE);
			ScriptEnum("SECTOR_FLAG1_NO_SMART_OBJ",    SEC_FLAGS1_NO_SMART_OBJ);
			ScriptEnum("SECTOR_FLAG1_SMART_OBJ",       SEC_FLAGS1_SMART_OBJ);
			ScriptEnum("SECTOR_FLAG1_SAFE_SECTOR",     SEC_FLAGS1_SAFESECTOR);
			ScriptEnum("SECTOR_FLAG1_PLAYER",          SEC_FLAGS1_PLAYER);
			ScriptEnum("SECTOR_FLAG1_SECRET",          SEC_FLAGS1_SECRET);
			// Outlaws
			ScriptEnum("SECTOR_FLAG1_SLOPEDFLOOR",	   SEC_FLAGS1_SLOPEDFLOOR);
			ScriptEnum("SECTOR_FLAG1_SLOPEDCEILING",   SEC_FLAGS1_SLOPEDCEILING);

			ScriptEnumRegister("WallFlags1");
			ScriptEnum("WALL_FLAGS1_TRANSPARENT_MIDTEX", WF1_ADJ_MID_TEX);
			ScriptEnum("WALL_FLAGS1_ILLUM_SIGN",         WF1_ILLUM_SIGN);
			ScriptEnum("WALL_FLAGS1_TEX_FLIP",           WF1_FLIP_HORIZ);
			ScriptEnum("WALL_FLAGS1_CHANGE_WALL_LIGHT",  WF1_CHANGE_WALL_LIGHT);
			ScriptEnum("WALL_FLAGS1_TEX_ANCHORED",       WF1_TEX_ANCHORED);
			ScriptEnum("WALL_FLAGS1_WALL_MORPHS",        WF1_WALL_MORPHS);
			ScriptEnum("WALL_FLAGS1_SCROLL_TOP_TEX",     WF1_SCROLL_TOP_TEX);
			ScriptEnum("WALL_FLAGS1_SCROLL_MID_TEX",     WF1_SCROLL_MID_TEX);
			ScriptEnum("WALL_FLAGS1_SCROLL_BOT_TEX",     WF1_SCROLL_BOT_TEX);
			ScriptEnum("WALL_FLAGS1_SCROLL_SIGN_TEX",    WF1_SCROLL_SIGN_TEX);
			ScriptEnum("WALL_FLAGS1_HIDE_ON_MAP",        WF1_HIDE_ON_MAP);
			ScriptEnum("WALL_FLAGS1_SHOW_NORMAL_ON_MAP", WF1_SHOW_NORMAL_ON_MAP);
			ScriptEnum("WALL_FLAGS1_SIGN_ANCHORED",      WF1_SIGN_ANCHORED);
			ScriptEnum("WALL_FLAGS1_DAMAGE_WALL",        WF1_DAMAGE_WALL);
			ScriptEnum("WALL_FLAGS1_SHOW_AS_LEDGE_ON_MAP", WF1_SHOW_AS_LEDGE_ON_MAP);
			ScriptEnum("WALL_FLAGS1_SHOW_AS_DOOR_ON_MAP", WF1_SHOW_AS_DOOR_ON_MAP);

			ScriptEnumRegister("WallFlags3");
			ScriptEnum("WALL_FLAGS3_ALWAYS_WALK",      WF3_ALWAYS_WALK);
			ScriptEnum("WALL_FLAGS3_SOLID",            WF3_SOLID_WALL);
			ScriptEnum("WALL_FLAGS3_PLAYER_WALK_ONLY", WF3_PLAYER_WALK_ONLY);
			ScriptEnum("WALL_FLAGS3_CANNOT_FIRE_THRU", WF3_CANNOT_FIRE_THROUGH);

			ScriptEnumRegister("SectorProp");
			ScriptEnumStr(SECTORPROP_FLOOR_HEIGHT);
			ScriptEnumStr(SECTORPROP_CEIL_HEIGHT);
			ScriptEnumStr(SECTORPROP_SECOND_HEIGHT);
			ScriptEnumStr(SECTORPROP_FLOOR_TEX);
			ScriptEnumStr(SECTORPROP_CEIL_TEX);
			ScriptEnumStr(SECTORPROP_AMBIENT);

			// Functions
			ScriptObjMethod("Sector getSector(int)", getSectorById);
			ScriptObjMethod("Sector getSector(string)", getSectorByName);
			ScriptObjMethod("Elevator getElevator(int)", getElevator);
			ScriptObjMethod("void findConnectedSectors(Sector initSector, uint, array<Sector>&)", findConnectedSectors);
			ScriptObjMethod("Object getObject(int)", getObjectById);
			ScriptObjFunc("Object getObject(string)", getObjectByName);
			ScriptObjFunc("int getObjectsByName(string, array<Object>&)", getAllObjectsByName);

			ScriptPropertySet("void set_gravity(int)", setGravity);
			ScriptPropertySet("void set_projectileGravity(int)", setProjectileGravity);

			// -- Getters --
			ScriptLambdaPropertyGet("int get_minLayer()", s32, { return s_levelState.minLayer; });
			ScriptLambdaPropertyGet("int get_maxLayer()", s32, { return s_levelState.maxLayer; });
			ScriptLambdaPropertyGet("int get_sectorCount()", s32, { return (s32)s_levelState.sectorCount; });
			ScriptLambdaPropertyGet("int get_secretCount()", s32, { return s_levelState.secretCount; });
			ScriptLambdaPropertyGet("int get_textureCount()", s32, { return s_levelState.textureCount; });
			ScriptLambdaPropertyGet("int get_elevatorCount()", s32, { return allocator_getCount(s_infSerState.infElevators); });
			ScriptLambdaPropertyGet("float2 get_parallax()", TFE_ForceScript::float2, { return TFE_ForceScript::float2(fixed16ToFloat(s_levelState.parallax0), fixed16ToFloat(s_levelState.parallax1)); });
			
			ScriptLambdaPropertyGet("int get_difficulty()", u8, { return s_agentData[s_agentId].difficulty; });

			// Gameplay sector pointers.
			ScriptLambdaPropertyGet("Sector get_bossSector()", ScriptSector, { ScriptSector sector(-1); if (s_levelState.bossSector) { sector.m_id = s_levelState.bossSector->id; } return sector; });
			ScriptLambdaPropertyGet("Sector get_mohcSector()", ScriptSector, { ScriptSector sector(-1); if (s_levelState.mohcSector) { sector.m_id = s_levelState.mohcSector->id; } return sector; });
			ScriptLambdaPropertyGet("Sector get_completeSector()", ScriptSector, { ScriptSector sector(-1); if (s_levelState.completeSector) { sector.m_id = s_levelState.completeSector->id; } return sector; });
		}
		ScriptClassEnd();
	}
}
