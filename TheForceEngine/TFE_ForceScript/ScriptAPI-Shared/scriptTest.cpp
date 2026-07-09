#include "scriptTest.h"
#include <TFE_System/system.h>
#include <TFE_System/math.h>
#include <TFE_Jedi/Level/levelData.h>
#include <TFE_Jedi/Level/rwall.h>
#include <TFE_Jedi/Level/rsector.h>
#include <angelscript.h>
#include <cinttypes>

using namespace TFE_Jedi;

namespace TFE_ForceScript
{
	std::string ScriptTest::s_system;
	std::vector<ScriptTest::Test> ScriptTest::s_tests;

	#define WRITE_ERROR(err) TFE_System::logWrite(LOG_ERROR, "Force Script", "[%s] Error: %s", s_tests.back().name.c_str(), err)

	bool ScriptTest::checkInTest()
	{
		if (s_tests.empty())
		{
			TFE_System::logWrite(LOG_ERROR, "Force Script", "No valid test in system %s", s_system.c_str());
			return false;
		}
		return true;
	}

	bool ScriptTest::compareValues(s32 typeId, void* ref0, void* ref1, bool equal, std::string& errorMsg)
	{
		char errMsg[4096];

		if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_STRING))
		{
			std::string v0 = *(std::string*)ref0;
			std::string v1 = *(std::string*)ref1;
			if ((equal && v0 != v1) || (!equal && v0 == v1))
			{
				if (equal) { sprintf(errMsg, "'%s' != '%s'", v0.c_str(), v1.c_str()); }
				else { sprintf(errMsg, "'%s' == '%s'", v0.c_str(), v1.c_str()); }
				errorMsg = errMsg;
				return false;
			}
			return true;
		}
		else if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_FLOAT2))
		{
			TFE_ForceScript::float2* v0 = (TFE_ForceScript::float2*)ref0;
			TFE_ForceScript::float2* v1 = (TFE_ForceScript::float2*)ref1;
			if ((equal && *v0 != *v1) || (!equal && *v0 == *v1))
			{
				if (equal) { sprintf(errMsg, "%s != %s", toString(*v0).c_str(), toString(*v1).c_str()); }
				else { sprintf(errMsg, "%s == %s", toString(*v0).c_str(), toString(*v1).c_str()); }
				errorMsg = errMsg;
				return false;
			}
			return true;
		}
		else if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_FLOAT3))
		{
			TFE_ForceScript::float3* v1 = (TFE_ForceScript::float3*)ref0;
			TFE_ForceScript::float3* v0 = (TFE_ForceScript::float3*)ref1;
			if ((equal && *v0 != *v1) || (!equal && *v0 == *v1))
			{
				if (equal) { sprintf(errMsg, "%s != %s", toStringF3(*v0).c_str(), toStringF3(*v1).c_str()); }
				else { sprintf(errMsg, "%s == %s", toStringF3(*v0).c_str(), toStringF3(*v1).c_str()); }
				errorMsg = errMsg;
				return false;
			}
			return true;
		}
		else if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_FLOAT4))
		{
			TFE_ForceScript::float4* v1 = (TFE_ForceScript::float4*)ref0;
			TFE_ForceScript::float4* v0 = (TFE_ForceScript::float4*)ref1;
			if ((equal && *v0 != *v1) || (!equal && *v0 == *v1))
			{
				if (equal) { sprintf(errMsg, "%s != %s", toStringF4(*v0).c_str(), toStringF4(*v1).c_str()); }
				else { sprintf(errMsg, "%s == %s", toStringF4(*v0).c_str(), toStringF4(*v1).c_str()); }
				errorMsg = errMsg;
				return false;
			}
			return true;
		}
		else if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_FLOAT2x2))
		{
			TFE_ForceScript::float2x2* v1 = (TFE_ForceScript::float2x2*)ref0;
			TFE_ForceScript::float2x2* v0 = (TFE_ForceScript::float2x2*)ref1;
			if ((equal && *v0 != *v1) || (!equal && *v0 == *v1))
			{
				if (equal) { sprintf(errMsg, "%s != %s", toStringF2x2(*v0).c_str(), toStringF2x2(*v1).c_str()); }
				else { sprintf(errMsg, "%s == %s", toStringF2x2(*v0).c_str(), toStringF2x2(*v1).c_str()); }
				errorMsg = errMsg;
				return false;
			}
			return true;
		}
		else if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_FLOAT3x3))
		{
			TFE_ForceScript::float3x3* v1 = (TFE_ForceScript::float3x3*)ref0;
			TFE_ForceScript::float3x3* v0 = (TFE_ForceScript::float3x3*)ref1;
			if ((equal && *v0 != *v1) || (!equal && *v0 == *v1))
			{
				if (equal) { sprintf(errMsg, "%s != %s", toStringF3x3(*v0).c_str(), toStringF3x3(*v1).c_str()); }
				else { sprintf(errMsg, "%s == %s", toStringF3x3(*v0).c_str(), toStringF3x3(*v1).c_str()); }
				errorMsg = errMsg;
				return false;
			}
			return true;
		}
		else if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_FLOAT4x4))
		{
			TFE_ForceScript::float4x4* v1 = (TFE_ForceScript::float4x4*)ref0;
			TFE_ForceScript::float4x4* v0 = (TFE_ForceScript::float4x4*)ref1;
			if ((equal && *v0 != *v1) || (!equal && *v0 == *v1))
			{
				if (equal) { sprintf(errMsg, "%s != %s", toStringF4x4(*v0).c_str(), toStringF4x4(*v1).c_str()); }
				else { sprintf(errMsg, "%s == %s", toStringF4x4(*v0).c_str(), toStringF4x4(*v1).c_str()); }
				errorMsg = errMsg;
				return false;
			}
			return true;
		}
		else
		{
			switch (typeId)
			{
				case 1:
				{
					bool v0 = *(bool*)(ref0);
					bool v1 = *(bool*)(ref1);
					if ((equal && v0 != v1) || (!equal && v0 == v1))
					{
						if (equal) { sprintf(errMsg, "%s != %s", v0 ? "true" : "false", v1 ? "true" : "false"); }
						else { sprintf(errMsg, "%s == %s", v0 ? "true" : "false", v1 ? "true" : "false"); }
						errorMsg = errMsg;
						return false;
					}
					return true;
					break;
				}
				case 2:
				{
					char v0 = *(char*)(ref0);
					char v1 = *(char*)(ref1);
					if ((equal && v0 != v1) || (!equal && v0 == v1))
					{
						if (equal) { sprintf(errMsg, "%c != %c", v0, v1); }
						else { sprintf(errMsg, "%c == %c", v0, v1); }
						errorMsg = errMsg;
						return false;
					}
					return true;
					break;
				}
				case 3:
				{
					s16 v0 = *(s16*)(ref0);
					s16 v1 = *(s16*)(ref1);
					if ((equal && v0 != v1) || (!equal && v0 == v1))
					{
						if (equal) { sprintf(errMsg, "%d != %d", v0, v1); }
						else { sprintf(errMsg, "%d == %d", v0, v1); }
						errorMsg = errMsg;
						return false;
					}
					return true;
					break;
				}
				case 4:
				{
					s32 v0 = *(s32*)(ref0);
					s32 v1 = *(s32*)(ref1);
					if ((equal && v0 != v1) || (!equal && v0 == v1))
					{
						if (equal) { sprintf(errMsg, "%d != %d", v0, v1); }
						else { sprintf(errMsg, "%d == %d", v0, v1); }
						errorMsg = errMsg;
						return false;
					}
					return true;
					break;
				}
				case 5:
				{
					s64 v0 = *(s64*)(ref0);
					s64 v1 = *(s64*)(ref1);
					if ((equal && v0 != v1) || (!equal && v0 == v1))
					{
						if (equal) { sprintf(errMsg, "%" PRIi64 " != %" PRIi64, v0, v1); }
						else { sprintf(errMsg, "%" PRIi64 " ==  %" PRIi64, v0, v1); }
						errorMsg = errMsg;
						return false;
					}
					return true;
					break;
				}
				case 6:
				{
					u8 v0 = *(u8*)(ref0);
					u8 v1 = *(u8*)(ref1);
					if ((equal && v0 != v1) || (!equal && v0 == v1))
					{
						if (equal) { sprintf(errMsg, "%u != %u", v0, v1); }
						else { sprintf(errMsg, "%u == %u", v0, v1); }
						errorMsg = errMsg;
						return false;
					}
					return true;
					break;
				}
				case 7:
				{
					u16 v0 = *(u16*)(ref0);
					u16 v1 = *(u16*)(ref1);
					if ((equal && v0 != v1) || (!equal && v0 == v1))
					{
						if (equal) { sprintf(errMsg, "%u != %u", v0, v1); }
						else { sprintf(errMsg, "%u == %u", v0, v1); }
						errorMsg = errMsg;
						return false;
					}
					return true;
					break;
				}
				case 8:
				{
					u32 v0 = *(u32*)(ref0);
					u32 v1 = *(u32*)(ref1);
					if ((equal && v0 != v1) || (!equal && v0 == v1))
					{
						if (equal) { sprintf(errMsg, "%u != %u", v0, v1); }
						else { sprintf(errMsg, "%u == %u", v0, v1); }
						errorMsg = errMsg;
						return false;
					}
					return true;
					break;
				}
				case 9:
				{
					u64 v0 = *(u64*)(ref0);
					u64 v1 = *(u64*)(ref1);
					if ((equal && v0 != v1) || (!equal && v0 == v1))
					{
						if (equal) { sprintf(errMsg, "%" PRIu64 " !=  %" PRIu64, v0, v1); }
						else { sprintf(errMsg, "%" PRIu64 " ==  %" PRIu64, v0, v1); }
						errorMsg = errMsg;
						return false;
					}
					return true;
					break;
				}
				case 10:
				{
					f32 v0 = *(f32*)(ref0);
					f32 v1 = *(f32*)(ref1);
					if ((equal && v0 != v1) || (!equal && v0 == v1))
					{
						if (equal) { sprintf(errMsg, "%f != %f", v0, v1); }
						else { sprintf(errMsg, "%f == %f", v0, v1); }
						errorMsg = errMsg;
						return false;
					}
					return true;
					break;
				}
				case 11:
				{
					f64 v0 = *(f64*)(ref0);
					f64 v1 = *(f64*)(ref1);
					if ((equal && v0 != v1) || (!equal && v0 == v1))
					{
						if (equal) { sprintf(errMsg, "%f != %f", v0, v1); }
						else { sprintf(errMsg, "%f == %f", v0, v1); }
						errorMsg = errMsg;
						return false;
					}
					return true;
					break;
				}
				default:
				{
					WRITE_ERROR("Unsupported type used.");
				}
			}
		}
		return false;
	}

	bool ScriptTest::checkBoolean(s32 typeId, void* ref, bool testForTrue, std::string& errorMsg)
	{
		char errMsg[4096];

		if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_STRING))
		{
			std::string v = *(std::string*)ref;
			if ((testForTrue && v.empty()) || (!testForTrue && !v.empty()))
			{
				if (testForTrue) { sprintf(errMsg, "'%s' empty", v.c_str()); }
				else { sprintf(errMsg, "'%s' is not empty", v.c_str()); }
				errorMsg = errMsg;
				return false;
			}
			return true;
		}
		else if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_FLOAT2))
		{
			TFE_ForceScript::float2* v = (TFE_ForceScript::float2*)ref;
			TFE_ForceScript::float2 zero(0.0f, 0.0f);
			if ((testForTrue && (*v) == zero) || (!testForTrue && (*v) != zero))
			{
				if (testForTrue) { sprintf(errMsg, "%s is zero", toString(*v).c_str()); }
				else { sprintf(errMsg, "%s is non-zero", toString(*v).c_str()); }
				errorMsg = errMsg;
				return false;
			}
			return true;
		}
		else if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_FLOAT3))
		{
			TFE_ForceScript::float3* v = (TFE_ForceScript::float3*)ref;
			TFE_ForceScript::float3 zero(0.0f, 0.0f, 0.0f);
			if ((testForTrue && (*v) == zero) || (!testForTrue && (*v) != zero))
			{
				if (testForTrue) { sprintf(errMsg, "%s is zero", toStringF3(*v).c_str()); }
				else { sprintf(errMsg, "%s is non-zero", toStringF3(*v).c_str()); }
				errorMsg = errMsg;
				return false;
			}
			return true;
		}
		else if (typeId == TFE_ForceScript::getObjectTypeId(TFE_ForceScript::FSTYPE_FLOAT4))
		{
			TFE_ForceScript::float4* v = (TFE_ForceScript::float4*)ref;
			TFE_ForceScript::float4 zero(0.0f, 0.0f, 0.0f, 0.0f);
			if ((testForTrue && (*v) == zero) || (!testForTrue && (*v) != zero))
			{
				if (testForTrue) { sprintf(errMsg, "%s is zero", toStringF4(*v).c_str()); }
				else { sprintf(errMsg, "%s is non-zero", toStringF4(*v).c_str()); }
				errorMsg = errMsg;
				return false;
			}
			return true;
		}
		// Skip matrix types.
		else
		{
			switch (typeId)
			{
				case 1:
				{
					bool v = *(bool*)(ref);
					if ((testForTrue && !v) || (!testForTrue && v))
					{
						if (testForTrue) { sprintf(errMsg, "%s is false, true expected", v ? "true" : "false"); }
						else { sprintf(errMsg, "%s is true, false expected", v ? "true" : "false"); }
						errorMsg = errMsg;
						return false;
					}
					return true;
					break;
				}
				case 2:
				{
					char v = *(char*)(ref);
					if ((testForTrue && v == 0) || (!testForTrue && v != 0))
					{
						if (testForTrue) { sprintf(errMsg, "%d is false, true expected", v); }
						else { sprintf(errMsg, "%d is true, false expected", v); }
						errorMsg = errMsg;
						return false;
					}
					return true;
					break;
				}
				case 3:
				{
					s16 v = *(s16*)(ref);
					if ((testForTrue && v == 0) || (!testForTrue && v != 0))
					{
						if (testForTrue) { sprintf(errMsg, "%d is false, true expected", v); }
						else { sprintf(errMsg, "%d is true, false expected", v); }
						errorMsg = errMsg;
						return false;
					}
					return true;
					break;
				}
				case 4:
				{
					s32 v = *(s32*)(ref);
					if ((testForTrue && v == 0) || (!testForTrue && v != 0))
					{
						if (testForTrue) { sprintf(errMsg, "%d is false, true expected", v); }
						else { sprintf(errMsg, "%d is true, false expected", v); }
						errorMsg = errMsg;
						return false;
					}
					return true;
					break;
				}
				case 5:
				{
					s64 v = *(s64*)(ref);
					if ((testForTrue && v == 0) || (!testForTrue && v != 0))
					{
						if (testForTrue) { sprintf(errMsg, "%" PRIi64 " is false, true expected", v); }
						else { sprintf(errMsg, "%" PRIi64 " is true, false expected", v); }
						errorMsg = errMsg;
						return false;
					}
					return true;
					break;
				}
				case 6:
				{
					u8 v = *(u8*)(ref);
					if ((testForTrue && v == 0) || (!testForTrue && v != 0))
					{
						if (testForTrue) { sprintf(errMsg, "%u is false, true expected", v); }
						else { sprintf(errMsg, "%u is true, false expected", v); }
						errorMsg = errMsg;
						return false;
					}
					return true;
					break;
				}
				case 7:
				{
					u16 v = *(u16*)(ref);
					if ((testForTrue && v == 0) || (!testForTrue && v != 0))
					{
						if (testForTrue) { sprintf(errMsg, "%u is false, true expected", v); }
						else { sprintf(errMsg, "%u is true, false expected", v); }
						errorMsg = errMsg;
						return false;
					}
					return true;
					break;
				}
				case 8:
				{
					u32 v = *(u32*)(ref);
					if ((testForTrue && v == 0) || (!testForTrue && v != 0))
					{
						if (testForTrue) { sprintf(errMsg, "%u is false, true expected", v); }
						else { sprintf(errMsg, "%u is true, false expected", v); }
						errorMsg = errMsg;
						return false;
					}
					return true;
					break;
				}
				case 9:
				{
					u64 v = *(u64*)(ref);
					if ((testForTrue && v == 0) || (!testForTrue && v != 0))
					{
						if (testForTrue) { sprintf(errMsg, "%" PRIu64 " is false, true expected", v); }
						else { sprintf(errMsg, "%" PRIu64 " is true, false expected", v); }
						errorMsg = errMsg;
						return false;
					}
					return true;
					break;
				}
				case 10:
				{
					f32 v = *(f32*)(ref);
					if ((testForTrue && v == 0) || (!testForTrue && v != 0))
					{
						if (testForTrue) { sprintf(errMsg, "%f is false, true expected", v); }
						else { sprintf(errMsg, "%f is true, false expected", v); }
						errorMsg = errMsg;
						return false;
					}
					return true;
					break;
				}
				case 11:
				{
					f64 v = *(f64*)(ref);
					if ((testForTrue && v == 0) || (!testForTrue && v != 0))
					{
						if (testForTrue) { sprintf(errMsg, "%f is false, true expected", v); }
						else { sprintf(errMsg, "%f is true, false expected", v); }
						errorMsg = errMsg;
						return false;
					}
					return true;
					break;
				}
				default:
				{
					WRITE_ERROR("Unsupported type used.");
				}
			}
		}
		return false;
	}

	bool ScriptTest::validateExpect(asIScriptGeneric* gen)
	{
		if (!checkInTest())
		{
			return false;
		}

		const s32 argCount = gen->GetArgCount();
		if (argCount != 2)
		{
			WRITE_ERROR("expectEq() - invalid number of arguments.");
			s_tests.back().errorCount++;
			return false;
		}

		if (gen->GetArgTypeId(0) != gen->GetArgTypeId(1))
		{
			WRITE_ERROR("expectEq() - arguments are not of the same type.");
			s_tests.back().errorCount++;
			return false;
		}
		return true;
	}

	bool ScriptTest::validateBoolean(asIScriptGeneric* gen)
	{
		if (!checkInTest())
		{
			return false;
		}

		const s32 argCount = gen->GetArgCount();
		if (argCount != 1)
		{
			WRITE_ERROR("expectFalse() - invalid number of arguments.");
			s_tests.back().errorCount++;
			return false;
		}

		return true;
	}

	void ScriptTest::expectEq(asIScriptGeneric* gen)
	{
		if (!validateExpect(gen)) { return; }

		std::string errorMsg;
		if (!compareValues(gen->GetArgTypeId(0), gen->GetArgAddress(0), gen->GetArgAddress(1), true/*values must be equal*/, errorMsg))
		{
			WRITE_ERROR(errorMsg.c_str());
			s_tests.back().errorCount++;
		}
	}

	void ScriptTest::expectNotEq(asIScriptGeneric* gen)
	{
		if (!validateExpect(gen)) { return; }

		std::string errorMsg;
		if (!compareValues(gen->GetArgTypeId(0), gen->GetArgAddress(0), gen->GetArgAddress(1), false/*values must be not equal*/, errorMsg))
		{
			WRITE_ERROR(errorMsg.c_str());
			s_tests.back().errorCount++;
		}
	}

	void ScriptTest::expectTrue(asIScriptGeneric* gen)
	{
		if (!validateBoolean(gen)) { return; }

		std::string errorMsg;
		if (!checkBoolean(gen->GetArgTypeId(0), gen->GetArgAddress(0), true/*value must be true*/, errorMsg))
		{
			WRITE_ERROR(errorMsg.c_str());
			s_tests.back().errorCount++;
		}
	}

	void ScriptTest::expectFalse(asIScriptGeneric* gen)
	{
		if (!validateBoolean(gen)) { return; }

		std::string errorMsg;
		if (!checkBoolean(gen->GetArgTypeId(0), gen->GetArgAddress(0), false/*value must be false*/, errorMsg))
		{
			WRITE_ERROR(errorMsg.c_str());
			s_tests.back().errorCount++;
		}
	}

	void ScriptTest::setTestSystem(const std::string& name)
	{
		s_system = name;
		s_tests.clear();
		TFE_System::logWrite(LOG_MSG, "Force Script", "----- Script Tests Begin %s -----", s_system.c_str());
	}

	void ScriptTest::report()
	{
		char results[4096];
		if (s_tests.empty())
		{
			sprintf(results, "FAILURE! No tests ran.");
		}
		else
		{
			const s32 count = (s32)s_tests.size();
			s32 errorCount = 0;
			for (s32 i = 0; i < count; i++)
			{
				errorCount += s_tests[i].errorCount;
			}
			if (errorCount == 0)
			{
				sprintf(results, "SUCCESS!\nTests Ran: %d", count);
			}
			else
			{
				sprintf(results, "FAILURE!\nTests Ran: %d, Error Count: %d", count, errorCount);
			}
		}
		TFE_System::logWrite(LOG_MSG, "Force Script", "---Results---");
		TFE_System::logWrite(LOG_MSG, "Force Script", results);
		TFE_System::logWrite(LOG_MSG, "Force Script", "-------------");
	}

	void ScriptTest::setTestName(const std::string& name)
	{
		Test newTest = {};
		newTest.name = name;
		newTest.errorCount = 0;
		s_tests.push_back(newTest);
	}
	   
	bool ScriptTest::scriptRegister(ScriptAPI api)
	{
		ScriptClassBegin("Test", "test", api);
		{
			ScriptGenericMethod("void expectEq(?&in, ?&in)", expectEq);
			ScriptGenericMethod("void expectNotEq(?&in, ?&in)", expectNotEq);
			ScriptGenericMethod("void expectTrue(?&in)", expectTrue);
			ScriptGenericMethod("void expectFalse(?&in)", expectFalse);

			ScriptObjMethod("void beginSystem(const string &in)", setTestSystem);
			ScriptObjMethod("void beginTest(const string &in)", setTestName);
			ScriptObjMethod("void report()", report);
		}
		ScriptClassEnd();
	}
}
