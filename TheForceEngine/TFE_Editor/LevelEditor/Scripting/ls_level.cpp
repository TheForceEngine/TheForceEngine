#include "ls_level.h"
#include <TFE_System/system.h>
#include <TFE_Editor/LevelEditor/infoPanel.h>
#include <TFE_Editor/LevelEditor/sharedState.h>
#include <TFE_Jedi/Level/rwall.h>
#include <TFE_Jedi/Level/rsector.h>
#include <angelscript.h>
namespace LevelEditor
{
	#define Vec2f_to_float2(v) TFE_ForceScript::float2(v.x, v.z)
	#define float2_to_Vec2f(v) Vec2f{v.x, v.y};

	// Creation
	// TODO Sector drawing.
	// TODO Guideline drawing.
	// Insert Entity
	// Insert Level Note

	// Other
	void LS_Level::findSector(std::string& name)
	{
		s32 index = findSectorByName(name.c_str());
		if (index < 0)
		{
			infoPanelAddMsg(LE_MSG_WARNING, "Cannot find sector '%s'", name.c_str());
		}
		else
		{
			selectAndScrollToSector(&s_level.sectors[index]);
		}
	}

	void LS_Level::findSectorById(s32 id)
	{
		if (id < 0 || id >= (s32)s_level.sectors.size())
		{
			infoPanelAddMsg(LE_MSG_WARNING, "ID %d is out of range.", id);
		}
		else
		{
			selectAndScrollToSector(&s_level.sectors[id]);
		}
	}

	bool LS_Level::scriptRegister(ScriptAPI api)
	{
		ScriptClassBegin("Level", "level", api);
		{
			// Functions
			ScriptObjMethod("void findSector(const string &in)", findSector);
			ScriptObjMethod("void findSectorById(int)", findSectorById);
			// -- Getters --
			ScriptLambdaPropertyGet("string get_name()", std::string, { return s_level.name; });
			ScriptLambdaPropertyGet("string get_slot()", std::string, { return s_level.slot; });
			ScriptLambdaPropertyGet("string get_palette()", std::string, { return s_level.palette; });
			ScriptLambdaPropertyGet("int get_sectorCount()", s32, { return (s32)s_level.sectors.size(); });
			ScriptLambdaPropertyGet("int get_entityCount()", s32, { return (s32)s_level.entities.size(); });
			ScriptLambdaPropertyGet("int get_levelNoteCount()", s32, { return (s32)s_level.notes.size(); });
			ScriptLambdaPropertyGet("int get_guidelineCount()", s32, { return (s32)s_level.guidelines.size(); });
			ScriptLambdaPropertyGet("int get_minLayer()", s32, { return s_level.layerRange[0]; });
			ScriptLambdaPropertyGet("int get_maxLayer()", s32, { return s_level.layerRange[1]; });
			ScriptLambdaPropertyGet("float2 get_parallax()", TFE_ForceScript::float2, { return Vec2f_to_float2(s_level.parallax); });
			// TODO Vec3f s_level.bounds[2]
			//      Types: Sector, Entity, LevelNote, Guideline
			// -- Setters --
			ScriptLambdaPropertySet("void set_name(const string &in)", (std::string& name), { s_level.name = name; });
			ScriptLambdaPropertySet("void set_palette(const string &in)", (std::string& palette), { s_level.palette = palette; });
			
			// Setup a script variables.
			ScriptMemberVariable("int index", m_index);
		}
		ScriptClassEnd();
	}
}
