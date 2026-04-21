#pragma once

#include <TFE_System/system.h>
#include <TFE_System/types.h>

namespace TFE_DarkForces
{
	class ScriptObject
	{
	public:
		ScriptObject() : m_id(-1) {};
		ScriptObject(s32 id) : m_id(id) {};
		void registerType();
		void registerFunctions();

	public:
		s32 m_id;
	};

	extern bool isScriptObjectValid(ScriptObject* object);
}