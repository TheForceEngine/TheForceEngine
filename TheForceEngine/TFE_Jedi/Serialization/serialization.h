#pragma once
//////////////////////////////////////////////////////////////////////
// Serialization
// This was added for TFE and is not directly based on
// reverse-engineered code. However, it was built to handle JEDI and
// Dark Forces types.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_FileSystem/stream.h>
#include <TFE_DarkForces/sound.h>
#include <TFE_Jedi/Level/level.h>
#include <TFE_Jedi/Level/rtexture.h>

struct AnimatedTexture;

namespace TFE_Jedi
{
	enum SaveVersion : u32
	{
		SaveVersionInit = 1,
		SaveVersionLevelScriptV1,
		SaveVersionHitEffectTaskUpdate,
		SaveVersionSmoothDeltatime,
		SavVersionTFEMessages,
		SaveVersionCur = SavVersionTFEMessages,
	};

	enum SerializationMode
	{
		SMODE_WRITE = 0,
		SMODE_READ,
		SMODE_UNKNOWN,
	};

	extern u32 s_sVersion;
	extern SerializationMode s_sMode;

	// This will generate an signed 32-bit index from a pointer, given a base pointer (start of an array, for example) and size of each element.
	// Note this will produce invalid results if index > INT_MAX (~2 billion).
	#define PTR_INDEX_S32(ptr, base, stride)   s32((std::ptrdiff_t(ptr) - std::ptrdiff_t(base)) / stride)
	#define INDEX_PTR_S32(index, base, stride) ((u8*)base + index * stride)

	#define SERIALIZE_VERSION(curVer) \
        { \
			u32 ver = curVer; \
			if (s_sMode == SMODE_WRITE) { stream->writeBuffer(&ver, sizeof(ver)); } \
			else if (s_sMode == SMODE_READ) { stream->readBuffer(&ver, sizeof(ver)); } \
			serialization_setVersion(ver); \
		}

	#define SERIALIZE(v, x, def) \
		if (s_sMode == SMODE_WRITE && s_sVersion >= v) { stream->writeBuffer(&x, sizeof(x)); } \
		else if (s_sMode == SMODE_READ) \
		{ \
			if (s_sVersion >= v) { stream->readBuffer(&x, sizeof(x)); } \
			else { x = def; } \
		}
	#define SERIALIZE_STRING(v, x) \
		if (s_sMode == SMODE_WRITE && s_sVersion >= v) { stream->write(&x); } \
		else if (s_sMode == SMODE_READ) \
		{ \
			if (s_sVersion >= v) { stream->read(&x); } \
		}
	#define SERIALIZE_BUF(v, x, s) if (s_sMode == SMODE_WRITE && s_sVersion >= v) { stream->writeBuffer(x, s); } \
		else if (s_sMode == SMODE_READ) \
		{ \
			if (s_sVersion >= v) { stream->readBuffer(x, s); } \
			else { memset(x, 0, s); } \
		}

	#define SERIALIZE_CSTRING_WRITE(v, x) \
		{ \
			u32 len = x ? (u32)strlen(x) : 0; \
			SERIALIZE(v, len, 0); \
			SERIALIZE_BUF(v, (void*)x, len); \
		}

	#define SERIALIZE_CSTRING(v, x) \
		{ \
			u32 len = s_sMode == SMODE_WRITE ? (u32)strlen(x) : 0; \
			SERIALIZE(v, len, 0); \
			SERIALIZE_BUF(v, (void*)x, len); \
			if (s_sMode == SMODE_READ) { x[len] = 0; } \
		}

	#define SERIALIZE_CSTRING_GAME_ALLOC(v, x) \
		{ \
			u32 len = s_sMode == SMODE_WRITE ? (u32)strlen(x) : 0; \
			SERIALIZE(v, len, 0); \
			if (s_sMode == SMODE_READ) { x = (char*)game_alloc(len + 1); } \
			SERIALIZE_BUF(v, x, len); \
			if (s_sMode == SMODE_READ) { x[len] = 0; } \
		}

	// Discard values that were previously added. This might mean skipping over data in the stream and ignoring it.
	// This is done by advancing the stream by the size of the type or buffer.
	// v0 = version added, v1 = version removed.
	#define SERIALIZE_DISCARD(v0, v1, type)  if (s_sVersion >= v0 && s_sVersion < v1 && s_sMode == SMODE_READ) { stream->seek(sizeof(type), ORIGIN_CURRENT); }
	#define SERIALIZE_BUF_DISCARD(v0, v1, s) if (s_sVersion >= v0 && s_sVersion < v1 && s_sMode == SMODE_READ) { stream->seek(s, ORIGIN_CURRENT); }
		
	inline void serialization_setVersion(u32 version) { s_sVersion = version; }
	inline void serialization_setMode(SerializationMode mode) { s_sMode = mode; }
	inline SerializationMode serialization_getMode() { return s_sMode; }
	inline u32 serialization_getVersion() { return s_sVersion; }
		
	void serialization_serializeDfSound(Stream* stream, u32 version, SoundSourceId* id);
	void serialization_serializeSectorPtr(Stream* stream, u32 version, RSector*& sector);
	void serialization_serializeAnimatedTexturePtr(Stream* stream, u32 version, AnimatedTexture*& animTex);
	void serialization_serializeTexturePtr(Stream* stream, u32 version, TextureData*& texture);
	void serialization_serialize3doPtr(Stream* stream, u32 version, JediModel*& model);
	void serialization_serializeWaxPtr(Stream* stream, u32 version, JediWax*& wax);
	void serialization_serializeFramePtr(Stream* stream, u32 version, JediFrame*& frame);
	void serialization_serializeScriptArg(Stream* stream, u32 version, TFE_ForceScript::ScriptArg* arg);
}