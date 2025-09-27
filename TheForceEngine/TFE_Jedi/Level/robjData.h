#pragma once
//////////////////////////////////////////////////////////////////////
// Wall
// Dark Forces Derived Renderer - Wall functions
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_Asset/spriteAsset_Jedi.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_FileSystem/stream.h>

using namespace TFE_Jedi;

struct JediModel;
struct RSector;
	
enum ObjectType
{
	OBJ_TYPE_SPIRIT = 0,
	OBJ_TYPE_SPRITE = 1,
	OBJ_TYPE_3D     = 2,
	OBJ_TYPE_FRAME  = 3,
};

enum ObjectFlags
{
	OBJ_FLAG_NONE = 0,
	OBJ_FLAG_AIM             = FLAG_BIT(0),  // An object that can be aimed at.
	OBJ_FLAG_EYE             = FLAG_BIT(1),  // Eye
	OBJ_FLAG_NEEDS_TRANSFORM = FLAG_BIT(2),  // Object needs to be transformed and rendered.
	OBJ_FLAG_FULLBRIGHT      = FLAG_BIT(3),  // Rendered as fullbright (not lit).
	OBJ_FLAG_MOVABLE         = FLAG_BIT(4),  // Object is movable.
	OBJ_FLAG_BOSS            = FLAG_BIT(5),  // Boss enemy.
	OBJ_FLAG_NO_REMOVE       = FLAG_BIT(6),  // Do not remove when crushed
	OBJ_FLAG_CAMERA			 = FLAG_BIT(7),  // New in TFE
};

enum EntityTypeFlags
{
	ETFLAG_AI_ACTOR       = FLAG_BIT(0),	// AI Actor - moves and acts on its own.
	ETFLAG_HAS_GRAVITY    = FLAG_BIT(1),	// This entity is effected by gravity.
	ETFLAG_FLYING         = FLAG_BIT(2),	// This entity is flying (can change Y height).
	ETFLAG_PROJECTILE     = FLAG_BIT(3),	// The entity is a projectile.
	ETFLAG_CAN_WAKE       = FLAG_BIT(6),	// An inactive object or animation waiting to be "woken up" - such as Vues waiting to play.
	ETFLAG_PICKUP         = FLAG_BIT(7),	// An item pickup.
	ETFLAG_SCENERY        = FLAG_BIT(8),	// Set for scenery.
	ETFLAG_CORPSE         = FLAG_BIT(9),	// Corpse
	ETFLAG_CAN_DISABLE    = FLAG_BIT(10),	// An object that can be enabled or disabled by INF.
	ETFLAG_SMART_OBJ      = FLAG_BIT(11),	// An object that can manipulate the level, such as opening doors.
	ETFLAG_REMOTE         = FLAG_BIT(12),	// Unknown, used by AI.
	ETFLAG_LANDMINE       = FLAG_BIT(13),	// Specifically set if a pre-placed landmine.
	ETFLAG_KEEP_CORPSE    = FLAG_BIT(14),	// Do not free this corpse.
	ETFLAG_GENERAL_MOHC   = FLAG_BIT(15),	// Used by AI.
	ETFLAG_LANDMINE_WPN   = FLAG_BIT(16),	// A landmine weapon - i.e. player placed landmines.
	ETFLAG_PLAYER         = FLAG_BIT(31),	// This is the player object.

	ETFLAG_NONE = 0,
};

enum ObjStateVersion : u32
{
	ObjState_InitVersion = 1,
	ObjState_FlyModeAdded = 2,
	ObjState_VueSmoothing = 3,
	ObjState_OneHitCheats = 4,
	ObjState_CrouchToggle = 5,
	ObjState_CustomLogics = 6,
	ObjState_ConstOverrides = 7,
	ObjState_DisablePlayerMovement = 8,
	ObjState_RefList = 9,
	ObjState_ExternalCamera = 10,
	ObjState_LogicScriptCallV1 = 11,	// adds ScriptCalls on pickup, death, alert
	ObjState_BurstFire = 12,
	ObjState_CurVersion = ObjState_BurstFire,
};

// TFE Scripting
enum ObjectRefType : s32
{
	ObjRefType_Startup           = 0,		// Present at level start
	ObjRefType_Projectile        = 1,
	ObjRefType_Effect            = 2,
	ObjRefType_SpawnItem         = 3,		// Item spawned at runtime
	ObjRefType_Corpse            = 4,
	ObjRefType_GeneratorSpawn    = 5,
	ObjRefType_ConsoleSpawn      = 6,
	ObjRefType_Removed           = -1,		// Object no longer exists
};

#define SPRITE_SCALE_FIXED FIXED(10)

struct SecObject
{
	SecObject* self;
	ObjectType type;
	u32 entityFlags;    // see EntityTypeFlags above.
	void* projectileLogic;	// projectile logic.

	// Position
	vec3_fixed posWS;
	vec3_fixed posVS;

	// World Size
	fixed16_16 worldWidth;
	fixed16_16 worldHeight;

	// 3x3 transformation matrix.
	fixed16_16 transform[9];

	// Rendering data.
	union
	{
		JediModel* model;
		Wax*   wax;
		WaxFrame* fme;
		void* ptr;
	};
	s32 frame;
	s32 anim;
	RSector* sector;
	void* logic;

	// See ObjectFlags above.
	u32 flags;

	// Orientation.
	angle14_16 pitch;
	angle14_16 yaw;
	angle14_16 roll;
	// index in containing sector object list.
	s16 index;

	// TFE
	u32 serializeIndex;
};

namespace TFE_Jedi
{
	// TFE - reference list of objects for scripting
	struct ObjectRef
	{
		SecObject* object;
		ObjectRefType type;
		char name[32];
	};
	extern std::vector<ObjectRef> s_objectRefList;
	
	void objData_clear();
	SecObject* objData_allocFromArray();
	void objData_freeToArray(SecObject* obj);

	void objData_serialize(Stream* stream);

	// Used for downstream serialization, to get the object from the serialized object ID.
	SecObject* objData_getObjectBySerializationId(u32 id);
	SecObject* objData_getObjectBySerializationId_NoValidation(u32 id);

	// TFE - scripting
	s32 obj_addToRefList(SecObject* obj, ObjectRefType refType);
	void obj_removeFromRefList(SecObject* obj);
	void obj_addName(const char* name, SecObject* obj);
	s32 obj_getRefIndex(SecObject* obj);
}
