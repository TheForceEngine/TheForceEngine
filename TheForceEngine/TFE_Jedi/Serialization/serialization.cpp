#include <cstring>

#include "serialization.h"
#include <TFE_Jedi/Level/levelData.h>
#include <TFE_System/system.h>

using namespace TFE_DarkForces;
using namespace TFE_Memory;

namespace TFE_Jedi
{
	// Asset IDs from pool + index.
	#define GEN_ID(index, pool) (index) | ((pool) << 24)
	#define ID_GET_INDEX(id) ((id) & 0xffffff)
	#define ID_GET_POOL(id) AssetPool((id) >> 24)

	u32 s_sVersion = 0;
	SerializationMode s_sMode = SMODE_UNKNOWN;
		
	void serialization_serializeDfSound(Stream* stream, u32 version, SoundSourceId* id)
	{
		s32 soundDataId = -1;
		if (s_sMode == SMODE_WRITE)
		{
			soundDataId = sound_getIndexFromId(*id);
		}
		SERIALIZE(version, soundDataId, -1);
		if (s_sMode == SMODE_READ)
		{
			*id = (soundDataId < 0) ? NULL_SOUND : sound_getSoundFromIndex(soundDataId, false);
		}
	}

	void serialization_serializeSectorPtr(Stream* stream, u32 version, RSector*& sector)
	{
		s32 sectorIndex = -1;
		if (s_sMode == SMODE_WRITE && sector)
		{
			sectorIndex = sector->index;
		}
		SERIALIZE(version, sectorIndex, -1);
		assert(sectorIndex == -1 || (sectorIndex >= 0 && sectorIndex <= (s32)s_levelState.sectorCount));
		if (s_sMode == SMODE_READ)
		{
			if (sectorIndex == s_levelState.sectorCount)
			{
				sector = s_levelState.controlSector;
			}
			else
			{
				sector = sectorIndex < 0 ? nullptr : &s_levelState.sectors[sectorIndex];
			}
		}
	}
		
	void serialization_serializeAnimatedTexturePtr(Stream* stream, u32 version, AnimatedTexture*& animTex)
	{
		s32 index = -1;
		if (s_sMode == SMODE_WRITE && animTex)
		{
			Allocator* animTexAlloc = bitmap_getAnimTextureAlloc();
			if (animTexAlloc)
			{
				index = allocator_getIndex(animTexAlloc, animTex);
			}
		}
		SERIALIZE(version, index, -1);
		if (s_sMode == SMODE_READ)
		{
			animTex = nullptr;
			Allocator* animTexAlloc = bitmap_getAnimTextureAlloc();
			if (animTexAlloc && index >= 0)
			{
				animTex = (AnimatedTexture*)allocator_getByIndex(animTexAlloc, index);
			}
		}
	}
		
	void serialization_serializeTexturePtr(Stream* stream, u32 version, TextureData*& texture)
	{
		s32 id = -1;
		if (s_sMode == SMODE_WRITE && texture)
		{
			s32 index;
			AssetPool pool;
			if (bitmap_getTextureIndex(texture, &index, &pool))
			{
				id = GEN_ID(index, pool);
			}
		}
		SERIALIZE(version, id, -1);
		if (s_sMode == SMODE_READ)
		{
			texture = (id < 0) ? nullptr : bitmap_getTextureByIndex(ID_GET_INDEX(id), ID_GET_POOL(id));
		}
	}

	void serialization_serialize3doPtr(Stream* stream, u32 version, JediModel*& model)
	{
		s32 id = -1;
		if (s_sMode == SMODE_WRITE && model)
		{
			s32 index;
			AssetPool pool;
			if (TFE_Model_Jedi::getModelIndex(model, &index, &pool))
			{
				id = GEN_ID(index, pool);
			}
		}
		SERIALIZE(version, id, -1);
		if (s_sMode == SMODE_READ)
		{
			model = (id < 0) ? nullptr : TFE_Model_Jedi::getModelByIndex(ID_GET_INDEX(id), ID_GET_POOL(id));
		}
	}

	void serialization_serializeWaxPtr(Stream* stream, u32 version, JediWax*& wax)
	{
		s32 id = -1;
		if (s_sMode == SMODE_WRITE && wax)
		{
			s32 index;
			AssetPool pool;
			if (TFE_Sprite_Jedi::getWaxIndex(wax, &index, &pool))
			{
				id = GEN_ID(index, pool);
			}
		}
		SERIALIZE(version, id, -1);
		if (s_sMode == SMODE_READ)
		{
			wax = (id < 0) ? nullptr : TFE_Sprite_Jedi::getWaxByIndex(ID_GET_INDEX(id), ID_GET_POOL(id));
		}
	}

	void serialization_serializeFramePtr(Stream* stream, u32 version, JediFrame*& frame)
	{
		s32 id = -1;
		if (s_sMode == SMODE_WRITE && frame)
		{
			s32 index;
			AssetPool pool;
			if (TFE_Sprite_Jedi::getFrameIndex(frame, &index, &pool))
			{
				id = GEN_ID(index, pool);
			}
		}
		SERIALIZE(version, id, -1);
		if (s_sMode == SMODE_READ)
		{
			frame = (id < 0) ? nullptr : TFE_Sprite_Jedi::getFrameByIndex(ID_GET_INDEX(id), ID_GET_POOL(id));
		}
	}

	void serialization_serializeScriptArg(Stream* stream, u32 version, TFE_ForceScript::ScriptArg* arg)
	{
		const bool write = s_sMode == SMODE_WRITE;
		const bool read = !write;

		// Use a fixed type rather than relying on the enum type.
		s32 type = write ? s32(arg->type) : 0;
		SERIALIZE(version, type, 0);
		if (read) { arg->type = TFE_ForceScript::ScriptArgType(type); }

		switch (arg->type)
		{
		case TFE_ForceScript::ARG_S32:
			SERIALIZE(version, arg->iValue, 0);
			break;
		case TFE_ForceScript::ARG_U32:
			SERIALIZE(version, arg->uValue, 0);
			break;
		case TFE_ForceScript::ARG_F32:
			SERIALIZE(version, arg->fValue, 0.0f);
			break;
		case TFE_ForceScript::ARG_BOOL:
			SERIALIZE(version, arg->bValue, false);
			break;
		case TFE_ForceScript::ARG_OBJECT:
			// Not yet supported.
			TFE_System::logWrite(LOG_ERROR, "ScriptArg Serialization", "Cannot serialize type \"object\"");
			break;
		case TFE_ForceScript::ARG_STRING:
			SERIALIZE_CSTRING(version, arg->strValue);
			break;
		case TFE_ForceScript::ARG_FLOAT2:
			SERIALIZE_BUF(version, arg->float2Value.m, sizeof(Vec2f));
			break;
		case TFE_ForceScript::ARG_FLOAT3:
			SERIALIZE_BUF(version, arg->float3Value.m, sizeof(Vec3f));
			break;
		case TFE_ForceScript::ARG_FLOAT4:
			SERIALIZE_BUF(version, arg->float4Value.m, sizeof(Vec4f));
			break;
		}
	}
}