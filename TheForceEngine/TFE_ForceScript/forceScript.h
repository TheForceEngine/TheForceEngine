#pragma once
//////////////////////////////////////////////////////////////////////
// Dark Forces Config
// Replacement for the Jedi Config for TFE.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_System/system.h>
#include <TFE_FileSystem/stream.h>
#include <cstring>
#include <string>

namespace TFE_ForceScript
{
	enum ScriptConst
	{
		TFE_MAX_SCRIPT_ARG = 16,
	};

	enum FS_BuiltInType
	{
		FSTYPE_STRING = 0,
		FSTYPE_ARRAY,
		FSTYPE_FLOAT2,
		FSTYPE_FLOAT3,
		FSTYPE_FLOAT4,
		FSTYPE_FLOAT2x2,
		FSTYPE_FLOAT3x3,
		FSTYPE_FLOAT4x4,
		FSTYPE_COUNT
	};

	enum ScriptArgType
	{
		ARG_S32 = 0,
		ARG_U32,
		ARG_F32,
		ARG_BOOL,
		ARG_OBJECT,
		ARG_STRING,
		ARG_FLOAT2,
		ARG_FLOAT3,
		ARG_FLOAT4,
	};

	struct ScriptArg
	{
		ScriptArgType type;
		union
		{
			s32 iValue;
			u32 uValue;
			f32 fValue;
			bool bValue;
			void* objPtr;
			Vec2f float2Value;
			Vec3f float3Value;
			Vec4f float4Value;
			char strValue[32];
		};
	};

	// Opaque Handles.
	typedef void* ModuleHandle;
	typedef void* FunctionHandle;
	typedef void(*ScriptMessageCallback)(LogWriteType type, const char* section, s32 row, s32 col, const char* msg);

	// Initialize and destroy script system.
	void init();
	void destroy();
	void stopAllFunc();
	void overrideCallback(ScriptMessageCallback callback = nullptr);
	// Run any active script functions.
	void update(f32 dt = 0.0f);
	// Stop all running script functions.
	void stopAllFunc();

	// Allow other systems to access the underlying engine.
	void* getEngine();

	// Compile module.
	ModuleHandle getModule(const char* moduleName);
	ModuleHandle createModule(const char* moduleName, const char* sectionName, const char* srcCode, u32 accessMask);
	ModuleHandle createModule(const char* moduleName, const char* filePath, bool allowReadFromArchive, u32 accessMask);
	void deleteModule(const char* moduleName);
	// Find a specific script function in a module.
	FunctionHandle findScriptFuncByDecl(ModuleHandle modHandle, const char* funcDecl);
	FunctionHandle findScriptFuncByName(ModuleHandle modHandle, const char* funcName);
	// Normally function names are case-sensitive, this function ignores case.
	FunctionHandle findScriptFuncByNameNoCase(ModuleHandle modHandle, const char* funcName);

	// Types
	s32 getObjectTypeId(FS_BuiltInType type);

	// Execute a script function.
	// It won't be executed immediately, it will happen on the next TFE_ForceScript update.
	// ---------------------------------------
	// Returns -1 on failure or id on success.
	// Note id is only valid as long as the function is running.
	s32 execFunc(FunctionHandle funcHandle, s32 argCount = 0, const ScriptArg* arg = nullptr);
	// Resume a suspended script function given by id.
	void resume(s32 id);

	void serialize(Stream* stream);

	///////////////////////////////////////////////////
	// Helpers for handle variable number of arguments
	// of different types for calling script functions.
	///////////////////////////////////////////////////
	inline ScriptArg scriptArg(s32 value) { ScriptArg arg; arg.type = ARG_S32; arg.iValue = value; return arg; }
	inline ScriptArg scriptArg(u32 value) { ScriptArg arg; arg.type = ARG_U32; arg.uValue = value; return arg; }
	inline ScriptArg scriptArg(f32 value) { ScriptArg arg; arg.type = ARG_F32; arg.fValue = value; return arg; }
	inline ScriptArg scriptArg(bool value) { ScriptArg arg; arg.type = ARG_BOOL; arg.bValue = value; return arg; }
	inline ScriptArg scriptArg(const std::string& value) { ScriptArg arg; arg.type = ARG_STRING; strcpy(arg.strValue, value.c_str()); return arg; }
	inline ScriptArg scriptArg(const char* value) { ScriptArg arg; arg.type = ARG_STRING; strcpy(arg.strValue, value); return arg; }
	inline ScriptArg scriptArg(Vec2f value) { ScriptArg arg; arg.type = ARG_FLOAT2; arg.float2Value = value; return arg; }
	inline ScriptArg scriptArg(Vec3f value) { ScriptArg arg; arg.type = ARG_FLOAT3; arg.float3Value = value; return arg; }
	inline ScriptArg scriptArg(Vec4f value) { ScriptArg arg; arg.type = ARG_FLOAT4; arg.float4Value = value; return arg; }
	inline ScriptArg scriptArg(void* value) { ScriptArg arg; arg.type = ARG_OBJECT; arg.objPtr = value; return arg; }

	template<typename T>
	inline void scriptArgIterator(s32& index, ScriptArg* outArg, T v)
	{
		if (index < TFE_MAX_SCRIPT_ARG)
		{
			outArg[index++] = scriptArg(v);
		}
	}

	template<typename T, typename... Args>
	inline void scriptArgIterator(s32& index, ScriptArg* outArg, T first, Args... args)
	{
		if (index < TFE_MAX_SCRIPT_ARG)
		{
			outArg[index++] = scriptArg(first);
		}
		scriptArgIterator(index, outArg, args...);
	}
}  // TFE_ForceScript