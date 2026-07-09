#include "gs_system.h"
#include "gameScripts.h"
#include <TFE_System/system.h>
#include <TFE_FrontEndUI/frontEndUi.h>
#include <TFE_Editor/LevelEditor/infoPanel.h>
#include <TFE_ForceScript/scriptInterface.h>
#include <TFE_ForceScript/float2.h>
#include <TFE_ForceScript/float3.h>
#include <TFE_ForceScript/Angelscript/add_on/scriptarray/scriptarray.h>
#include <angelscript.h>
#include <cinttypes>

namespace TFE_DarkForces
{
	namespace  // Static helper functions.
	{
		std::string toString(CScriptArray* arr)
		{
			char output[4096] = "";
			s32 typeId = arr->GetElementTypeId();
			char tmp[256];

			if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_ARRAY))
			{
				// Can't handle multi-dimensional arrays yet.
				return "?";
			}

			const u32 len = arr->GetSize();
			for (u32 i = 0; i < len; i++)
			{
				const void* data = arr->At(i);
				std::string value;
				if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_STRING))
				{
					value = *(std::string*)data;
				}
				else if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_FLOAT2))
				{
					value = toString(*(TFE_ForceScript::float2*)data);
				}
				else if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_FLOAT3))
				{
					value = toStringF3(*(TFE_ForceScript::float3*)data);
				}
				else if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_FLOAT4))
				{
					value = toStringF4(*(TFE_ForceScript::float4*)data);
				}
				else if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_FLOAT2x2))
				{
					value = toStringF2x2(*(TFE_ForceScript::float2x2*)data);
				}
				else if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_FLOAT3x3))
				{
					value = toStringF3x3(*(TFE_ForceScript::float3x3*)data);
				}
				else if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_FLOAT4x4))
				{
					value = toStringF4x4(*(TFE_ForceScript::float4x4*)data);
				}
				else
				{
					switch (typeId)
					{
						case 1:
						{
							bool local = *(bool*)(data);
							value = local ? "true" : "false";
							break;
						}
						case 2:
						{
							char local = *(char*)(data);
							sprintf(tmp, "%c", local);
							value = tmp;
							break;
						}
						case 3:
						{
							s16 local = *(s16*)(data);
							sprintf(tmp, "%d", local);
							value = tmp;
							break;
						}
						case 4:
						{
							s32 local = *(s32*)(data);
							sprintf(tmp, "%d", local);
							value = tmp;
							break;
						}
						case 5:
						{
							s64 local = *(s64*)(data);
							sprintf(tmp, "%" PRIi64, local);
							value = tmp;
							break;
						}
						case 6:
						{
							u8 local = *(u8*)(data);
							sprintf(tmp, "%u", local);
							value = tmp;
							break;
						}
						case 7:
						{
							u16 local = *(u16*)(data);
							sprintf(tmp, "%u", local);
							value = tmp;
							break;
						}
						case 8:
						{
							u32 local = *(u32*)(data);
							sprintf(tmp, "%u", local);
							value = tmp;
							break;
						}
						case 9:
						{
							u64 local = *(u64*)(data);
							sprintf(tmp, "%" PRIu64, local);
							value = tmp;
							break;
						}
						case 10:
						{
							f32 local = *(f32*)(data);
							sprintf(tmp, "%g", local);
							value = tmp;
							break;
						}
						case 11:
						{
							f64 local = *(f64*)(data);
							sprintf(tmp, "%g", local);
							value = tmp;
							break;
						}
						default:
						{
							return "?";
						}
					}
				}

				char fmt[256];
				if (len == 1)
				{
					sprintf(fmt, "(%s)", value.c_str());
				}
				else if (i == 0)
				{
					sprintf(fmt, "(%s, ", value.c_str());
				}
				else if (i < len - 1)
				{
					sprintf(fmt, "%s, ", value.c_str());
				}
				else
				{
					sprintf(fmt, "%s)", value.c_str());
				}
				strcat(output, fmt);
			}
			return output;
		}

		std::string formatValue(s32 typeId, void* ref)
		{
			std::string value;
			char tmp[256];

			if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_STRING))
			{
				value = *(std::string*)ref;
			}
			else if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_ARRAY))
			{
				value = toString((CScriptArray*)ref);
			}
			else if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_FLOAT2))
			{
				value = toString(*(TFE_ForceScript::float2*)ref);
			}
			else if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_FLOAT3))
			{
				value = toStringF3(*(TFE_ForceScript::float3*)ref);
			}
			else if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_FLOAT4))
			{
				value = toStringF4(*(TFE_ForceScript::float4*)ref);
			}
			else if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_FLOAT2x2))
			{
				value = toStringF2x2(*(TFE_ForceScript::float2x2*)ref);
			}
			else if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_FLOAT3x3))
			{
				value = toStringF3x3(*(TFE_ForceScript::float3x3*)ref);
			}
			else if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_FLOAT4x4))
			{
				value = toStringF4x4(*(TFE_ForceScript::float4x4*)ref);
			}
			else
			{
				switch (typeId)
				{
					case 1:
					{
						bool local = *(bool*)(ref);
						value = local ? "true" : "false";
						break;
					}
					case 2:
					{
						char local = *(char*)(ref);
						sprintf(tmp, "%c", local);
						value = tmp;
						break;
					}
					case 3:
					{
						s16 local = *(s16*)(ref);
						sprintf(tmp, "%d", local);
						value = tmp;
						break;
					}
					case 4:
					{
						s32 local = *(s32*)(ref);
						sprintf(tmp, "%d", local);
						value = tmp;
						break;
					}
					case 5:
					{
						s64 local = *(s64*)(ref);
						sprintf(tmp, "%" PRIi64, local);
						value = tmp;
						break;
					}
					case 6:
					{
						u8 local = *(u8*)(ref);
						sprintf(tmp, "%u", local);
						value = tmp;
						break;
					}
					case 7:
					{
						u16 local = *(u16*)(ref);
						sprintf(tmp, "%u", local);
						value = tmp;
						break;
					}
					case 8:
					{
						u32 local = *(u32*)(ref);
						sprintf(tmp, "%u", local);
						value = tmp;
						break;
					}
					case 9:
					{
						u64 local = *(u64*)(ref);
						sprintf(tmp, "%" PRIu64, local);
						value = tmp;
						break;
					}
					case 10:
					{
						f32 local = *(f32*)(ref);
						sprintf(tmp, "%g", local);
						value = tmp;
						break;
					}
					case 11:
					{
						f64 local = *(f64*)(ref);
						sprintf(tmp, "%g", local);
						value = tmp;
						break;
					}
					default:
					{
						value = "?";
					}
				}
			}
			return value;
		}

		void outputPrintFormat(asIScriptGeneric *gen)
		{
			const s32 argCount = gen->GetArgCount();

			void* ref = gen->GetArgAddress(0);
			s32 typeId = gen->GetArgTypeId(0);
			std::string fmt = *((std::string*)ref);
			std::string output = fmt;

			// parse the string for {} pairs.
			const char* key = "{}";
			const size_t keyLen = strlen(key);
			size_t len = output.length();

			s32 index = 1;
			for (size_t pos = 0; pos < len && index < argCount;)
			{
				pos = output.find(key, pos);
				if (pos == std::string::npos)
				{
					break;
				}

				ref = gen->GetArgAddress(index);
				typeId = gen->GetArgTypeId(index);
				index++;

				output.replace(pos, keyLen, formatValue(typeId, ref));
				len = output.length();
				pos += 2;
			}
			TFE_FrontEndUI::logToConsole(output.c_str());
		}
	}

	void GS_System::error(asIScriptGeneric *gen)
	{
		outputPrintFormat(gen);
	}

	void GS_System::warning(asIScriptGeneric *gen)
	{
		outputPrintFormat(gen);
	}
	
	void GS_System::print(asIScriptGeneric *gen)
	{
		outputPrintFormat(gen);
	}

	bool GS_System::scriptRegister(ScriptAPI api)
	{
		ScriptClassBegin("System", "system", api);
		{
			// Member Variables
			ScriptMemberVariable("int version", m_version);

			// Functions
			// Currently support up to 10 arguments + format string.
			// This has to be a static member of the class since it use the GENERIC calling convention, but it is still added as an object method
			// so the script API follows the desired `system.func()` pattern.
			ScriptGenericMethod(
				"void error(const string &in, ?&in arg0 = 0, ?&in arg1 = 0, ?&in arg2 = 0, ?&in arg3 = 0, ?&in arg4 = 0, ?&in arg5 = 0, ?&in arg6 = 0, ?&in arg7 = 0, ?&in arg8 = 0, ?&in arg9 = 0)",
				error);
			ScriptGenericMethod(
				"void warning(const string &in, ?&in arg0 = 0, ?&in arg1 = 0, ?&in arg2 = 0, ?&in arg3 = 0, ?&in arg4 = 0, ?&in arg5 = 0, ?&in arg6 = 0, ?&in arg7 = 0, ?&in arg8 = 0, ?&in arg9 = 0)",
				warning);
			ScriptGenericMethod(
				"void print(const string &in, ?&in arg0 = 0, ?&in arg1 = 0, ?&in arg2 = 0, ?&in arg3 = 0, ?&in arg4 = 0, ?&in arg5 = 0, ?&in arg6 = 0, ?&in arg7 = 0, ?&in arg8 = 0, ?&in arg9 = 0)",
				print);
		}
		ScriptClassEnd();
	}
}
