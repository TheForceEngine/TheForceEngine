#pragma once
//////////////////////////////////////////////////////////////////////
// Collision System
// Classic Dark Forces (DOS) Jedi derived Collision system. This is
// the core collision system used by Dark Forces.
//
// Copyright note:
// While the project as a whole is licensed under GPL-2.0, some of the
// code under TFE_Collision/ was derived from reverse-engineered
// code from "Dark Forces" (DOS) which is copyrighted by LucasArts.
//
// I consider the reverse-engineering to be "Fair Use" - a means of 
// supporting the games on other platforms and to improve support on
// existing platforms without claiming ownership of the games
// themselves or their IPs.
//
// That said using this code in a commercial project is risky without
// permission of the original copyright holders (LucasArts).
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_Jedi/Level/level.h>
#include <TFE_Jedi/Math/core_math.h>

struct RSector;
struct SecObject;
struct RWall;

struct CollisionInterval
{
	fixed16_16 x0;
	fixed16_16 x1;
	fixed16_16 y0;
	fixed16_16 y1;
	fixed16_16 z0;
	fixed16_16 z1;
	fixed16_16 move;
	fixed16_16 dirX;
	fixed16_16 dirZ;
};

enum CollisionInfoFlags : u32
{
	COLINFO_INFINITE_DROP = FLAG_BIT(0),	// when set, the entity can move over an "infinitely" high cliff edge (up to 9999 units)
};

struct CollisionInfo
{
	SecObject* obj;
	fixed16_16 offsetX;
	fixed16_16 offsetY;
	fixed16_16 offsetZ;
	fixed16_16 stepUpHeight;    // entity will be able to "climb" up this height (same as player's stepHeight)
	fixed16_16 stepDownHeight;  // entity will be able to "step down" from this height
	fixed16_16 height;          // entity height for the purposes of collision detection
	s32 unused;

	RWall* wall;
	s32 u24;
	SecObject* collidedObj;

	fixed16_16 width;           // entity width (half-width, i.e. radius) for the purposes of collision detection
	u32 flags;
	JBool responseStep;
	vec2_fixed responseDir;
	vec2_fixed responsePos;
	angle14_32 responseAngle;
};

struct ColObject
{
	union
	{
		SecObject* obj;
		RSector* sector;
		RWall* wall;
		void* ptr;
	};
};

enum RayHitType
{
	RHit_None = 0,
	RHit_Floor,
	RHit_Ceiling,
	RHit_WallMid,
	RHit_WallMid_Trans,
	RHit_WallBot,
	RHit_WallTop,
	RHit_WallSign,
};
struct RayHitInfo
{
	RayHitType hit;
	RWall* wall;
	RSector* sector;
};

typedef void(*CollisionEffectFunc)(SecObject*);

#define COL_INFINITY FIXED(9999)
#define COL_SEC_HEIGHT_OFFSET FIXED(2)

namespace TFE_Jedi
{
	void collision_getHitPoint(fixed16_16* x, fixed16_16* z);
	fixed16_16 collision_getHitDistance();
	RSector* collision_tryMove(RSector* sector, fixed16_16 x0, fixed16_16 z0, fixed16_16 x1, fixed16_16 z1);
	RSector* collision_moveObj(SecObject* obj, fixed16_16 dx, fixed16_16 dz);
	RWall* collision_pathWallCollision(RSector* sector);
	RWall* collision_wallCollisionFromPath(RSector* sector, fixed16_16 srcX, fixed16_16 srcZ, fixed16_16 dstX, fixed16_16 dstZ);
	JBool collision_canHitObject(RSector* startSector, RSector* endSector, vec3_fixed p0, vec3_fixed p1, u32 exclWallFlags3);

	SecObject* collision_getObjectCollision(RSector* sector, CollisionInterval* interval, SecObject* prevObj);
	JBool collision_isAnyObjectInRange(RSector* sector, fixed16_16 radius, vec3_fixed origin, SecObject* skipObj, u32 entityFlags);

	void collision_effectObjectsInRange3D(RSector* startSector, fixed16_16 range, vec3_fixed origin, CollisionEffectFunc effectFunc, SecObject* excludeObj, u32 entityFlags);
	void collision_effectObjectsInRangeXZ(RSector* startSector, fixed16_16 range, vec3_fixed origin, CollisionEffectFunc effectFunc, SecObject* excludeObj, u32 entityFlags);

	JBool handleCollision(CollisionInfo* colInfo);
	void handleCollisionResponseSimple(fixed16_16 dirX, fixed16_16 dirZ, fixed16_16* moveX, fixed16_16* moveZ);

	JBool inf_handleExplosion(RSector* sector, fixed16_16 x, fixed16_16 z, fixed16_16 range);
	RayHitInfo collision_rayCast3d(RSector* sector, vec3_fixed p0, vec3_fixed p1, bool stopAtMidTex);

	// Variables
	extern fixed16_16 s_colObjOverlap;
	extern s32 s_collisionFrameWall;
	extern JBool s_collision_wallHit;
	extern u32 s_collision_excludeEntityFlags;
}
