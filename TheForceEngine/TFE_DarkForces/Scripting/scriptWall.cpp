#include "scriptWall.h"
#include "scriptTexture.h"
#include <TFE_ForceScript/ScriptAPI-Shared/scriptMath.h>
#include <TFE_ForceScript/ScriptAPI-Shared/scriptLevelShared.h>
#include <TFE_Jedi/Level/levelData.h>
#include <angelscript.h>

using namespace TFE_Jedi;

namespace TFE_DarkForces
{
	bool isScriptWallValid(ScriptWall* wall)
	{
		if (wall->m_sectorId < 0 || wall->m_sectorId >= (s32)s_levelState.sectorCount) { return false; }
		if (wall->m_wallId < 0 || wall->m_wallId >= s_levelState.sectors[wall->m_sectorId].wallCount) { return false; }
		return true;
	}
	bool isWallFlagSet(s32 index, u32 flag, ScriptWall* wall)
	{
		if (!isScriptWallValid(wall)) { return false; }
		if (index == 1) { return (s_levelState.sectors[wall->m_sectorId].walls[wall->m_wallId].flags1 & flag) != 0u; }
		else if (index == 2) { return (s_levelState.sectors[wall->m_sectorId].walls[wall->m_wallId].flags2 & flag) != 0u; }
		else if (index == 3) { return (s_levelState.sectors[wall->m_sectorId].walls[wall->m_wallId].flags3 & flag) != 0u; }
		return false;
	}
	void clearWallFlag(s32 index, u32 flag, ScriptWall* wall)
	{
		if (!isScriptWallValid(wall)) { return; }
		if (index == 1) { s_levelState.sectors[wall->m_sectorId].walls[wall->m_wallId].flags1 &= ~flag; }
		else if (index == 2) { s_levelState.sectors[wall->m_sectorId].walls[wall->m_wallId].flags2 &= ~flag; }
		else if (index == 3) { s_levelState.sectors[wall->m_sectorId].walls[wall->m_wallId].flags3 &= ~flag; }
	}
	void setWallFlag(s32 index, u32 flag, ScriptWall* wall)
	{
		if (!isScriptWallValid(wall)) { return; }
		if (index == 1) { s_levelState.sectors[wall->m_sectorId].walls[wall->m_wallId].flags1 |= flag; }
		else if (index == 2) { s_levelState.sectors[wall->m_sectorId].walls[wall->m_wallId].flags2 |= flag; }
		else if (index == 3) { s_levelState.sectors[wall->m_sectorId].walls[wall->m_wallId].flags3 |= flag; }
	}
	s32 getAdjoin(ScriptWall* wall)
	{
		if (!isScriptWallValid(wall)) { return -1; }
		RSector* next = s_levelState.sectors[wall->m_sectorId].walls[wall->m_wallId].nextSector;
		return next ? next->id : -1;
	}
	s32 getMirror(ScriptWall* wall)
	{
		if (!isScriptWallValid(wall)) { return -1; }
		RWall* next = s_levelState.sectors[wall->m_sectorId].walls[wall->m_wallId].mirrorWall;
		return next ? next->id : -1;
	}
	f32 getWallLight(ScriptWall* wall)
	{
		if (!isScriptWallValid(wall)) { return 0; }
		return fixed16ToFloat(s_levelState.sectors[wall->m_sectorId].walls[wall->m_wallId].wallLight);
	}
	ScriptTexture getWallTexture(WallPart part, ScriptWall* wall)
	{
		ScriptTexture tex(-1);
		if (isScriptWallValid(wall))
		{
			RWall* lvlWall = &s_levelState.sectors[wall->m_sectorId].walls[wall->m_wallId];
			switch (part)
			{
			case WP_MIDDLE: tex.m_id = ScriptTexture::getTextureIdFromData(&lvlWall->midTex); break;
			case WP_TOP:    tex.m_id = ScriptTexture::getTextureIdFromData(&lvlWall->topTex); break;
			case WP_BOTTOM: tex.m_id = ScriptTexture::getTextureIdFromData(&lvlWall->botTex); break;
			case WP_SIGN:   tex.m_id = ScriptTexture::getTextureIdFromData(lvlWall->signTex); break;
			}
		}
		return tex;
	}
	TFE_ForceScript::float2 getWallTextureOffset(WallPart part, ScriptWall* wall)
	{
		TFE_ForceScript::float2 offset(0.0f, 0.0f);
		if (isScriptWallValid(wall))
		{
			RWall* lvlWall = &s_levelState.sectors[wall->m_sectorId].walls[wall->m_wallId];
			switch (part)
			{
			case WP_MIDDLE:
			{
				offset.x = fixed16ToFloat(lvlWall->midOffset.x);
				offset.y = fixed16ToFloat(lvlWall->midOffset.z);
			} break;
			case WP_TOP:
			{
				offset.x = fixed16ToFloat(lvlWall->topOffset.x);
				offset.y = fixed16ToFloat(lvlWall->topOffset.z);
			} break;
			case WP_BOTTOM:
			{
				offset.x = fixed16ToFloat(lvlWall->botOffset.x);
				offset.y = fixed16ToFloat(lvlWall->botOffset.z);
			} break;
			case WP_SIGN:
			{
				offset.x = fixed16ToFloat(lvlWall->signOffset.x);
				offset.y = fixed16ToFloat(lvlWall->signOffset.z);
			} break;
			}
		}
		return offset;
	}
	TFE_ForceScript::float2 getVertex(s32 index, ScriptWall* wall)
	{
		TFE_ForceScript::float2 vtx(0.0f, 0.0f);
		if (!isScriptWallValid(wall)) { return vtx; }

		RWall* lvlWall = &s_levelState.sectors[wall->m_sectorId].walls[wall->m_wallId];
		vtx.x = fixed16ToFloat(index == 0 ? lvlWall->w0->x : lvlWall->w1->x);
		vtx.y = fixed16ToFloat(index == 0 ? lvlWall->w0->z : lvlWall->w1->z);
		return vtx;
	}
	TFE_ForceScript::float2 getVertexBase(ScriptWall* wall)
	{
		TFE_ForceScript::float2 vtx(0.0f, 0.0f);
		if (!isScriptWallValid(wall)) { return vtx; }

		RWall* lvlWall = &s_levelState.sectors[wall->m_sectorId].walls[wall->m_wallId];
		vtx.x = fixed16ToFloat(lvlWall->worldPos0.x);
		vtx.y = fixed16ToFloat(lvlWall->worldPos0.z);
		return vtx;
	}
	void setAdjoin(s32 id, ScriptWall* wall)
	{
		if (!isScriptWallValid(wall)) { return; }
		RSector* sector = &s_levelState.sectors[wall->m_sectorId];
		RWall* lvlWall = &sector->walls[wall->m_wallId];
		if (id < 0 || id >= (s32)s_levelState.sectorCount)
		{
			lvlWall->nextSector = nullptr;

			sector_setupWallDrawFlags(sector);
		}
		else
		{
			lvlWall->nextSector = &s_levelState.sectors[id];

			sector_setupWallDrawFlags(sector);
			sector_setupWallDrawFlags(lvlWall->nextSector);
		}
	}
	void setMirror(s32 id, ScriptWall* wall)
	{
		if (!isScriptWallValid(wall)) { return; }
		RSector* sector = &s_levelState.sectors[wall->m_sectorId];
		RWall* lvlWall = &sector->walls[wall->m_wallId];
		RSector* next = lvlWall->nextSector;
		if (!next) { return; }

		if (id < 0 || id >= next->wallCount)
		{
			lvlWall->mirror = -1;
			lvlWall->mirrorWall = nullptr;
		}
		else
		{
			lvlWall->mirror = id;
			lvlWall->mirrorWall = &next->walls[id];
			sector_setupWallDrawFlags(lvlWall->nextSector);
		}
		sector_setupWallDrawFlags(sector);
	}
	void setWallLight(f32 light, ScriptWall* wall)
	{
		if (!isScriptWallValid(wall)) { return; }
		RSector* sector = &s_levelState.sectors[wall->m_sectorId];
		sector->walls[wall->m_wallId].wallLight = floatToFixed16(light);
		sector->dirtyFlags |= SDF_AMBIENT;
	}
	void setWallTexture(WallPart part, ScriptTexture tex, ScriptWall* wall)
	{
		if (isScriptWallValid(wall) && ScriptTexture::isTextureValid(&tex))
		{
			RSector* sector = &s_levelState.sectors[wall->m_sectorId];
			RWall* lvlWall = &sector->walls[wall->m_wallId];
			TextureData** data = &s_levelState.textures[tex.m_id];
			switch (part)
			{
			case WP_MIDDLE: lvlWall->midTex = *data; break;
			case WP_TOP:    lvlWall->topTex = *data; break;
			case WP_BOTTOM: lvlWall->botTex = *data; break;
			case WP_SIGN:   lvlWall->signTex = data; break;
			}
		}
	}
	void setWallTextureOffset(WallPart part, TFE_ForceScript::float2 offset, ScriptWall* wall)
	{
		if (isScriptWallValid(wall))
		{
			RWall* lvlWall = &s_levelState.sectors[wall->m_sectorId].walls[wall->m_wallId];
			const vec2_fixed fixedOffset = { floatToFixed16(offset.x), floatToFixed16(offset.y) };
			switch (part)
			{
			case WP_MIDDLE: lvlWall->midOffset = fixedOffset; break;
			case WP_TOP:    lvlWall->topOffset = fixedOffset; break;
			case WP_BOTTOM: lvlWall->botOffset = fixedOffset; break;
			case WP_SIGN:   lvlWall->signOffset = fixedOffset; break;
			}
		}
	}
	void setVertex(TFE_ForceScript::float2 vtx, s32 index, ScriptWall* wall)
	{
		if (!isScriptWallValid(wall)) { return; }

		RSector* sector = &s_levelState.sectors[wall->m_sectorId];
		RWall* lvlWall = &sector->walls[wall->m_wallId];
		if (index == 0)
		{
			lvlWall->w0->x = floatToFixed16(vtx.x);
			lvlWall->w0->z = floatToFixed16(vtx.y);
		}
		else
		{
			lvlWall->w1->x = floatToFixed16(vtx.x);
			lvlWall->w1->z = floatToFixed16(vtx.y);
		}
		sector->dirtyFlags |= (SDF_VERTICES | SDF_WALL_SHAPE);
	}

	void ScriptWall::registerType()
	{
		s32 res = 0;
		asIScriptEngine* engine = (asIScriptEngine*)TFE_ForceScript::getEngine();

		ScriptValueType("Wall");
		// Variables
		ScriptMemberVariable("int sectorId", m_sectorId);
		ScriptMemberVariable("int wallId", m_wallId);
		// Functions
		ScriptObjFunc("bool isValid()", isScriptWallValid);
		ScriptObjFunc("bool isFlagSet(int, uint)", isWallFlagSet);
		ScriptObjFunc("void clearFlag(int, uint)", clearWallFlag);
		ScriptObjFunc("void setFlag(int, uint)", setWallFlag);
		ScriptObjFunc("float2 getVertex(int index = 0)", getVertex);
		ScriptObjFunc("float2 getVertexBase()", getVertexBase);
		ScriptObjFunc("void setVertex(float2, int index = 0)", setVertex);
		ScriptObjFunc("LevelTexture getTexture(WallPart)", getWallTexture);
		ScriptObjFunc("float2 getTextureOffset(WallPart)", getWallTextureOffset);
		ScriptObjFunc("void setTexture(WallPart, LevelTexture)", setWallTexture);
		ScriptObjFunc("void setTextureOffset(WallPart, float2)", setWallTextureOffset);
		// Properties
		ScriptPropertyGetFunc("int get_adjoin()", getAdjoin);
		ScriptPropertyGetFunc("int get_mirror()", getMirror);
		ScriptPropertyGetFunc("float get_wallLight()", getWallLight);
		ScriptPropertySetFunc("void set_adjoin(int)", setAdjoin);
		ScriptPropertySetFunc("void set_mirror(int)", setMirror);
		ScriptPropertySetFunc("void set_wallLight(float)", setWallLight);
	}
}
