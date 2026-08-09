#pragma once
//////////////////////////////////////////////////////////////////////
// The Force Engine Editor
// A system built to view and edit Dark Forces data files.
// The viewing aspect needs to be put in place at the beginning
// in order to properly test elements in isolation without having
// to "play" the game as intended.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_Jedi/InfSystem/infSystem.h>
#include <TFE_Jedi/InfSystem/infTypesInternal.h>
#include <TFE_Editor/EditorAsset/editorAsset.h>
#include "levelEditorData.h"
#include "selection.h"

namespace LevelEditor
{
	enum Editor_InfItemClass : u32
	{
		IIC_ELEVATOR,	// "Elevator"
		IIC_TRIGGER,	// "Trigger"
		IIC_TELEPORTER,	// "Teleporter"
	};
		
	enum Editor_InfElevType : u32
	{
		// Special, "high level" elevators.
		IET_BASIC,				// "Basic"
		IET_BASIC_AUTO, 		// "BASIC_AUTO"
		IET_INV, 				// "INV"
		IET_DOOR, 				// "DOOR"
		IET_DOOR_INV, 			// "DOOR_INV"
		IET_DOOR_MID, 			// "DOOR_MID"
		IET_MORPH_SPIN1, 		// "MORPH_SPIN1"
		IET_MORPH_SPIN2, 		// "MORPH_SPIN2"
		IET_MORPH_MOVE1, 		// "MORPH_MOVE1"
		IET_MORPH_MOVE2, 		// "MORPH_MOVE2"

		// Standard Elevators
		IET_MOVE_CEILING,		// "MOVE_CEILING"
		IET_MOVE_FLOOR,			// "MOVE_FLOOR"
		IET_MOVE_FC,			// "MOVE_FC"
		IET_MOVE_OFFSET,		// "MOVE_OFFSET"
		IET_MOVE_WALL,			// "MOVE_WALL"
		IET_ROTATE_WALL,		// "ROTATE_WALL",
		IET_SCROLL_WALL,		// "SCROLL_WALL",
		IET_SCROLL_FLOOR,		// "SCROLL_FLOOR",
		IET_SCROLL_CEILING,		// "SCROLL_CEILING",
		IET_CHANGE_LIGHT,		// "CHANGE_LIGHT",
		IET_CHANGE_WALL_LIGHT,	// "CHANGE_WALL_LIGHT"
	};

	enum Editor_InfMessageType : u32
	{
		// Derived from MessageType
		IMT_NEXT_STOP,
		IMT_PREV_STOP,
		IMT_GOTO_STOP,
		IMT_MASTER_ON,
		IMT_MASTER_OFF,
		IMT_SET_BITS,
		IMT_CLEAR_BITS,
		IMT_COMPLETE,
		IMT_LIGHTS,
		IMT_TRIGGER,
		// Elevator only
		IMT_DONE,
		IMT_WAKEUP,
		IMT_CAMERA,
		IMT_COUNT,
	};
		
	enum Editor_InfStopDelayType : u32
	{
		SDELAY_SECONDS = 0,
		SDELAY_HOLD,
		SDELAY_TERMINATE,
		SDELAY_COMPLETE,
		SDELAY_PREV_VALUE,
	};

	enum Editor_InfElevatorVar : u32
	{
		IEV_START = 0,
		IEV_SPEED,
		IEV_MASTER,
		IEV_ANGLE,
		IEV_FLAGS,
		IEV_KEY0,
		IEV_KEY1,
		IEV_CENTER,
		IEV_SOUND0,
		IEV_SOUND1,
		IEV_SOUND2,
		IEV_EVENT_MASK,
		IEV_ENTITY_MASK,
	};

	enum Editor_InfElevatorOverride : u32
	{
		IEO_NONE		= 0,
		IEO_START		= FLAG_BIT(0),
		IEO_SPEED		= FLAG_BIT(1),
		IEO_MASTER		= FLAG_BIT(2),
		IEO_ANGLE		= FLAG_BIT(3),
		IEO_FLAGS		= FLAG_BIT(4),
		IEO_KEY0		= FLAG_BIT(5),
		IEO_KEY1		= FLAG_BIT(6),
		IEO_CENTER      = FLAG_BIT(7),
		IEO_SOUND0		= FLAG_BIT(8),
		IEO_SOUND1		= FLAG_BIT(9),
		IEO_SOUND2		= FLAG_BIT(10),
		IEO_EVENT_MASK	= FLAG_BIT(11),
		IEO_ENTITY_MASK = FLAG_BIT(12),
		IEO_SLAVES		= FLAG_BIT(13),
		IEO_STOPS		= FLAG_BIT(14),
		IEO_VAR_MASK    = IEO_SLAVES - 1
	};

	enum Editor_InfTriggerOverride : u32
	{
		ITO_NONE        = 0,
		ITO_SOUND       = FLAG_BIT(0),
		ITO_MASTER      = FLAG_BIT(1),
		ITO_TEXT        = FLAG_BIT(2),
		ITO_EVENT_MASK  = FLAG_BIT(3),
		ITO_ENTITY_MASK = FLAG_BIT(4),
		ITO_EVENT       = FLAG_BIT(5),
		ITO_MSG         = FLAG_BIT(6),
		ITO_VAR_MASK    = (ITO_MSG << 1) - 1
	};

	enum Editor_InfTriggerVar : u32
	{
		ITV_SOUND = 0,
		ITV_MASTER,
		ITV_TEXT,
		ITV_EVENT_MASK,
		ITV_ENTITY_MASK,
		ITV_EVENT,
		ITV_MSG,
	};

	enum Editor_InfStopOverride : u32
	{
		ISO_NONE  = 0,
		ISO_DELAY = FLAG_BIT(0),
		ISO_PAGE  = FLAG_BIT(1),
	};

	enum Editor_InfStopCmd
	{
		ISC_MESSAGE = 0,
		ISC_ADJOIN,
		ISC_TEXTURE,
		ISC_PAGE,
		ISC_SCRIPTCALL,
		ISC_COUNT
	};

	struct Editor_InfClass
	{
		Editor_InfItemClass classId;
	};

	struct Editor_InfItem
	{
		std::string name;
		s32 wallNum = -1;
		std::vector<Editor_InfClass*> classData;
	};

	// Commands
	struct Editor_InfAdjoinCmd
	{
		std::string sector0;
		std::string sector1;
		s32 wallIndex0;
		s32 wallIndex1;
	};

	struct Editor_InfTextureCmd
	{
		std::string donorSector;
		bool fromCeiling;	// ceiling if true, otherwise floor.
	};

	struct Editor_InfSlave
	{
		std::string name;
		f32 angleOffset = 0.0f;
	};

	struct Editor_InfMessage
	{
		std::string targetSector;
		s32 targetWall = -1;
		Editor_InfMessageType type;
		u32 eventFlags = 0;		// InfEventMask
		u32 arg[2] = { 0 };
	};

	struct Editor_ScriptCallArg
	{
		std::string value = "";
	};

	struct Editor_ScriptCall
	{
		std::string funcName;
		Editor_ScriptCallArg arg[4];
	};

	struct Editor_InfStop
	{
		u32 overrideSet = ISO_NONE;		// InfStopOverride - Which values were overriden from the defaults.

		bool relative = false;
		f32 value = 0.0f;
		std::string fromSectorFloor;	// if empty, use value.

		Editor_InfStopDelayType delayType = SDELAY_SECONDS;
		f32 delay = 4.0f;

		std::vector<Editor_InfAdjoinCmd> adjoinCmd;
		std::vector<Editor_InfTextureCmd> textureCmd;
		std::vector<Editor_InfMessage> msg;
		std::vector<Editor_ScriptCall> scriptCall;
		std::string page;
	};

	struct Editor_InfClient
	{
		std::string targetSector;
		s32 targetWall = -1;
		s32 eventMask = INF_EVENT_ANY;
	};

	// Specific classes.
	struct Editor_InfElevator
	{
		Editor_InfItemClass classId = IIC_ELEVATOR;
		Editor_InfElevType type;
		u32 overrideSet = IEO_NONE;		// InfElevatorOverride - Which values were overriden from the defaults.

		s32 start = 0;								// "START:"
		f32 speed = 0.0f;							// "SPEED:"
		f32 angle = 0.0f;							// "ANGLE:"
		u32 flags = 0;								// "FLAGS:"	-- InfElevatorFlags
		KeyItem key[2] = { KEY_NONE, KEY_NONE };	// "KEY:"	-- Second entry used for door_mid, addon
		Vec2f center = { 0 };						// "CENTER:"
		std::string sounds[3];						// "none" or "0" for no sound.
		bool master = true;							// "MASTER:"
		s32 eventMask = INF_EVENT_ANY;				// InfEventMask
		s32 entityMask = INF_ENTITY_ANY;			// InfEntityMask
		std::vector<Editor_InfSlave> slaves;		// "SLAVE:"
		std::vector<Editor_InfStop> stops;			// "STOP:"
	};

	struct Editor_InfTrigger
	{
		Editor_InfItemClass classId = IIC_TRIGGER;
		TriggerType type;
		u32 overrideSet = ITO_NONE;
		std::vector<Editor_InfClient> clients;
				
		Editor_InfMessageType cmd = IMT_TRIGGER;
		u32 arg[2] = { 0 };

		std::string sound;  // Ignored with sector triggers.
		bool master = true;	// "MASTER:"
		u32  textId = 0;	// "TEXT:"
		s32  eventMask = INF_EVENT_ANY;				// InfEventMask
		s32  entityMask = INF_ENTITY_ANY;			// InfEntityMask
		u32  event = 0;
	};

	struct Editor_InfTeleporter
	{
		Editor_InfItemClass classId = IIC_TELEPORTER;
		TeleportType type = TELEPORT_BASIC;
		std::string target;
		Vec3f dstPos = { 0 };
		f32 dstAngle = 0.0f;
	};

	struct Editor_LevelInf
	{
		std::vector<Editor_InfItem> item;
		std::vector<Editor_InfElevator*>      elevator;
		std::vector<Editor_InfTrigger*>       trigger;
		std::vector<Editor_InfTeleporter*>    teleport;
	};

	struct Editor_InfItemHistory
	{
		std::string name;
		s32 wallNum = -1;
		s32 infCount = -1;
		std::vector<Editor_InfClass*> classData;
		std::vector<void*> classPtr; // Pointers to original data for testing equality
	};

	// Modify INF history, index tracking
	struct InfModified
	{
		std::vector<IndexPair>matches;
		std::vector<s32>deleteIndexes; // temp var refs
		std::vector<s32>modifiedIndexes; // s_levelInf refs
		std::vector<s32>newIndexes; //s_levelInf refs
	};

	enum InfVpControlType
	{
		InfVpControl_None = 0,
		InfVpControl_Center,	// Show the center.
		InfVpControl_AngleXZ,
		InfVpControl_AngleXY,
		InfVpControl_TargetPos3d,
		InfVpControl_Count
	};

	struct Editor_InfVpControl
	{
		InfVpControlType type;
		Vec3f cen;			// center -or- arrow start.
		Vec3f dir;			// direction derived from angle -or- direction on wall.
		Vec3f nrm;			// face normal.
	};

	extern Editor_LevelInf s_levelInf;
	extern InfModified s_infModified;

	void editor_infDestroy();
	bool loadLevelInfFromAsset(const TFE_Editor::Asset* asset);
	void editor_infEditBegin(const char* sectorName, s32 wallIndex = -1);
	void editor_infEditEnd();
	bool editor_infEdit();
	Editor_InfItem* editor_getInfItem(const char* sectorName, s32 wallIndex);

	void editor_writeInfItem(std::string& outStr, const Editor_InfItem* item, const char* curTab);

	void editor_loadInfBinary(FileStream& file, u32 version);
	void editor_saveInfBinary(FileStream& file);

	void editor_handleSelection(EditorSector* sector, s32 wallIndex = -1);
	void editor_handleSelection(Vec3f pos);

	void editor_infGetViewportControl(Editor_InfVpControl* ctrl);

	s32 getClassIndex(Editor_InfItemClass classId, const void* classPtr);

	bool compareLevelInfs(Editor_LevelInf& left, Editor_LevelInf& right);

	const Editor_InfElevator* getElevFromClassData(const Editor_InfClass* data);
	const Editor_InfTrigger* getTriggerFromClassData(const Editor_InfClass* data);
	const Editor_InfTeleporter* getTeleporterFromClassData(const Editor_InfClass* data);

	void freeInfClass(Editor_InfClass* infClass);
	Editor_InfElevator* allocElev(Editor_InfItem* item);
	Editor_InfTrigger* allocTrigger(Editor_InfItem* item);
	Editor_InfTeleporter* allocTeleporter(Editor_InfItem* item);
}
