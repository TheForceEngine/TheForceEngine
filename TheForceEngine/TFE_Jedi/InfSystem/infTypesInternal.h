#pragma once
#include <TFE_System/types.h>
#include <TFE_DarkForces/sound.h>
#include <TFE_Jedi/Level/rsector.h>
#include <TFE_Jedi/Level/rwall.h>
#include <TFE_Jedi/Level/robject.h>
#include <TFE_ForceScript/scriptInterface.h>
#include "infPublicTypes.h"

struct TextureData;
struct AnimatedTexture;
struct Allocator;

namespace TFE_Jedi
{
	struct InfLink;
		
	// How an elevator moves if triggered.
	enum ElevTrigMove
	{
		TRIGMOVE_HOLD = -1,	// The elevator is holding, so the trigger message has no effect.
		TRIGMOVE_CONT = 0,	// Continue to the next stop if NOT already moving.
		TRIGMOVE_LAST = 1,	// Set next (target) stop to the last stop whether moving or not.
		TRIGMOVE_NEXT = 2,	// Set next (target) stop to the next stop if NOT waiting for a timed delay.
		TRIGMOVE_PREV = 3,	// Set next (target) stop to the prev stop if waiting for a timed delay or goes back 2 stops if NOT waiting for a timed delay.
	};

	enum ElevUpdateFlags
	{
		ELEV_MOVING     = FLAG_BIT(0),	// elevator should be moving.
		ELEV_MASTER_ON  = FLAG_BIT(1),	// master on
		ELEV_CRUSH      = FLAG_BIT(2),	// the elevator is moving in reverse.
	};

	enum InfDelay
	{
		// IDELAY_SECONDS < IDELAY_COMPLETE
		IDELAY_HOLD      = 0xffffffff,
		IDELAY_TERMINATE = 0xfffffffe,
		IDELAY_COMPLETE  = 0xfffffffd,
	};
		
	enum TriggerType
	{
		ITRIGGER_WALL = 0,
		ITRIGGER_SECTOR,
		ITRIGGER_SWITCH1,
		ITRIGGER_TOGGLE,
		ITRIGGER_SINGLE
	};
		
	enum InfEntityMask
	{
		INF_ENTITY_ENEMY     = FLAG_BIT(0),
		INF_ENTITY_WEAPON    = FLAG_BIT(3),
		INF_ENTITY_SMART_OBJ = FLAG_BIT(11),
		INF_ENTITY_PLAYER    = FLAG_BIT(31),
		INF_ENTITY_ANY = 0xffffffffu
	};

	enum InfElevatorFlags
	{
		INF_EFLAG_MOVE_FLOOR = FLAG_BIT(0),	// Move on floor.
		INF_EFLAG_MOVE_SECHT = FLAG_BIT(1),	// Move on second height.
		INF_EFLAG_MOVE_CEIL  = FLAG_BIT(2),	// Move on ceiling (head has to be close enough?)
		INF_EFLAG_DOOR       = FLAG_BIT(3),	// This is a door auto-created from the sector flag.
		// New TFE Flags - TODO: Implement them.
		INF_EFLAG_ROTATE_FLOOR = FLAG_BIT(4),	// Rotate the floor if the sector rotates.
		INF_EFLAG_ROTATE_CEIL  = FLAG_BIT(5),	// Rotate the ceiling if the sector rotates.
	};

	enum KeyItem
	{
		KEY_NONE   = 0,
		KEY_RED    = 23,
		KEY_YELLOW = 24,
		KEY_BLUE   = 25,
	};

	enum TeleportType
	{
		TELEPORT_BASIC = 0,
		TELEPORT_CHUTE = 1
	};

	enum ScriptCallConst
	{
		MAX_SCRIPT_CALL_NAME_LEN = 64,
		MAX_SCRIPT_CALL_ARG = 5,
	};

	struct Teleport
	{
		RSector* sector;
		RSector* target;
		TeleportType type;

		vec3_fixed dstPosition;
		angle14_16  dstAngle[3];
	};

	struct TriggerTarget
	{
		RSector* sector;
		RWall*   wall;
		u32      eventMask;
	};

	struct InfTrigger
	{
		TriggerType type;
		InfLink* link;
		AnimatedTexture* animTex;
		Allocator* targets;
		Allocator* scriptCalls;
		MessageType cmd;
		u32 event;
		u32 arg0;
		u32 arg1;

		// Unused
		// Some of these values are set, such as timer and time but are not referenced.
		s32 arg2;
		s32 arg3;
		s32 arg4;
		s32 arg5;

		s32 timer;
		s32 time;
		// End Unused

		JBool master;
		TextureData* tex;
		SoundSourceId soundId;
		s32 state;
		char* text;	// This is also unreferenced - probably debug code.
		u32 textId;

		// TFE
		JBool deleted;
		void* parent;
	};

	struct InfMessage
	{
		RSector* sector;
		RWall* wall;
		MessageType msgType;
		u32 event;
		u32 arg1;
		u32 arg2;
	};

	// TFE: Add "scriptcall" as an INF function, similar to message.
	struct InfScriptCall
	{
		void* funcPtr;
		s32 argCount;
		TFE_ForceScript::ScriptArg args[MAX_SCRIPT_CALL_ARG];
		char funcName[MAX_SCRIPT_CALL_NAME_LEN];	// Needed for serialization.
	};

	struct Slave
	{
		RSector* sector;
		s32 value;
	};

	struct AdjoinCmd
	{
		RWall* wall0;
		RWall* wall1;
		RSector* sector0;
		RSector* sector1;
	};

	struct Stop
	{
		fixed16_16 value;
		Tick delay;				// delay in 'ticks' (see TICKS_PER_SECOND in TFE_DarkForces/time.h)
		Allocator* messages;
		Allocator* adjoinCmds;
		Allocator* scriptCalls;
		SoundSourceId pageId;
		TextureData** floorTex;
		TextureData** ceilTex;

		// TFE:
		s32 floorTexSecId = -1;
		s32 ceilTexSecId = -1;
		s32 index = -1;
	};

	struct InfElevator
	{
		InfElevator* self;
		InfElevatorType type;
		ElevTrigMove trigMove;
		RSector* sector;
		KeyItem key;
		s32 fixedStep;
		Tick nextTick;
		Tick timer;
		Allocator* stops;
		Allocator* slaves;
		Stop* nextStop;
		fixed16_16 speed;
		fixed16_16* value;
		fixed16_16 iValue;
		vec2_fixed dirOrCenter;
		u32 flags;
		SoundSourceId sound0;
		SoundSourceId sound1;
		SoundSourceId sound2;
		SoundEffectId loopingSoundID;
		s32 updateFlags;
		// TFE
		fixed16_16 prevValue;
		JBool deleted;
	};
}