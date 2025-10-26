#include "collision.h"
#include <TFE_Jedi/Level/levelData.h>
#include <TFE_Jedi/Level/rsector.h>
#include <TFE_Jedi/Level/rwall.h>
#include <TFE_Jedi/Level/robject.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/InfSystem/infSystem.h>
// Merge player collision into collision
#include <TFE_DarkForces/playerCollision.h>
using namespace TFE_DarkForces;

// These will be moved over from player collision.
namespace TFE_DarkForces
{
	extern SecObject* s_collidedObj;
	extern ColObject s_colObject;
	extern ColObject s_colObj1;
	extern RWall* s_colWallCollided;
	extern fixed16_16 s_colSrcPosX;
	extern fixed16_16 s_colSrcPosY;
	extern fixed16_16 s_colSrcPosZ;
	extern fixed16_16 s_colHeight;
	extern fixed16_16 s_colDoubleRadius;
	extern fixed16_16 s_colHeightBase;
	extern fixed16_16 s_colRealFloorHeight;
	extern fixed16_16 s_colRealCeilHeight;
	extern fixed16_16 s_colBottom;
	extern fixed16_16 s_colTop;
	extern fixed16_16 s_colY1;
	extern fixed16_16 s_colCurTop;
	extern angle14_32 s_colResponseAngle;
	extern vec2_fixed s_colResponsePos;
	extern vec2_fixed s_colWallV0;
	extern s32 s_collisionFrameSector;

	extern CollisionObjFunc s_objCollisionFunc;
	extern CollisionProxFunc s_objCollisionProxFunc;
}

namespace TFE_Jedi
{
	////////////////////////////////////////////////////////
	// Structures and Constants
	////////////////////////////////////////////////////////
	struct ColPath
	{
		fixed16_16 x0;
		fixed16_16 x1;
		fixed16_16 z0;
		fixed16_16 z1;
	};

	static const fixed16_16 c_maxCollisionDist = COL_INFINITY;
	static const fixed16_16 c_minTraversableOpening = HALF_16;

	enum IntersectionResult
	{
		INTERSECT = 0xffffffff,
		NO_INTERSECT = 0,
	};

	////////////////////////////////////////////////////////
	// Internal State
	////////////////////////////////////////////////////////
	s32 s_collisionFrameWall;
	JBool s_collision_wallHit = JFALSE;
	u32 s_collision_excludeEntityFlags = 0;
	static ColPath s_col_path;

	static fixed16_16 s_col_hitX;
	static fixed16_16 s_col_hitDist;
	static fixed16_16 s_col_hitZ;
	static fixed16_16 s_col_ceilingHeight;
	static fixed16_16 s_col_floorHeight;

	static vec2_fixed s_col_intersectPos;
	static fixed16_16 s_col_intersectParam;
	static fixed16_16 s_col_wallZ1;
	static fixed16_16 s_col_intersectNum;
	static fixed16_16 s_col_intersectNum2;
	static fixed16_16 s_col_intersectDen;

	static fixed16_16 s_col_adjPathZ0;
	static fixed16_16 s_col_adjPathX0;
	static RWall* s_col_curWall;
	static fixed16_16 s_col_pathDx;
	static fixed16_16 s_col_pathDz;
	static fixed16_16 s_col_adjPathX1;
	static fixed16_16 s_col_intersect;
	static fixed16_16 s_col_adjPathZ1;
	static fixed16_16 s_col_wallDx;
	static fixed16_16 s_col_wallDz;

	static fixed16_16 s_col_XOffset;
	static fixed16_16 s_col_ZOffset;

	static fixed16_16 s_col_pathX0;
	static fixed16_16 s_col_pathX1;
	static fixed16_16 s_col_pathZ0;
	static fixed16_16 s_col_pathZ1;

	static fixed16_16 s_col_wallX0;
	static fixed16_16 s_col_wallX1;
	static fixed16_16 s_col_wallZ0;
		
	// Object Collision
	static fixed16_16 s_colObjOffsetX;
	static fixed16_16 s_colObjOffsetZ;
	static fixed16_16 s_colObjDirX;
	static fixed16_16 s_colObjDirZ;
	static fixed16_16 s_colObjMinY;
	static fixed16_16 s_colObjMaxY;
	static fixed16_16 s_colObjX0;
	static fixed16_16 s_colObjX1;
	static fixed16_16 s_colObjY0;
	static fixed16_16 s_colObjY1;
	static fixed16_16 s_colObjZ0;
	static fixed16_16 s_colObjZ1;
	static fixed16_16 s_colObjMove;

	static vec2_fixed s_colWallV0;

	static SecObject*  s_colObjPrev;
	static SecObject** s_colObjList;
	static CollisionInterval* s_colObjInterval;
	
	static s32 s_colObjCount;
	fixed16_16 s_colObjOverlap;
	
	////////////////////////////////////////////////////////
	// Forward Declarations
	////////////////////////////////////////////////////////
	IntersectionResult pathIntersectsWall(ColPath* path, RWall* wall);
	vec2_fixed* computeIntersectPos();
	SecObject* internal_getObjectCollision();
			
	////////////////////////////////////////////////////////
	// API Implementation
	////////////////////////////////////////////////////////
	void collision_getHitPoint(fixed16_16* x, fixed16_16* z)
	{
		*x = s_col_hitX;
		*z = s_col_hitZ;
	}

	fixed16_16 collision_getHitDistance()
	{
		return s_col_hitDist;
	}

	RWall* collision_pathWallCollision(RSector* sector)
	{
		RWall* wall = sector->walls;
		s32 count = sector->wallCount;
		RWall* hitWall = nullptr;
		s_col_hitDist = c_maxCollisionDist;
		for (s32 i = 0; i < sector->wallCount; i++, wall++)
		{
			if (s_collisionFrameWall == wall->collisionFrame)
			{
				continue;
			}

			// returns INTERSECT if s_col_path intersects wall, else returns NO_INTERSECT
			IntersectionResult intersect = pathIntersectsWall(&s_col_path, wall);
			if (intersect == INTERSECT)
			{
				vec2_fixed* pos = computeIntersectPos();
				if (pos->x != s_col_path.x0 || pos->z != s_col_path.z0)
				{
					fixed16_16 dx = s_col_path.x1 - s_col_path.x0;
					fixed16_16 dz = s_col_path.z1 - s_col_path.z0;

					// Essentially a dot product with the (negative) wall normal, which given the wall direction (dir), normal = (-dir.z, dir.x), negative = (dir.z, -dir.x)
					// So (dx, dz).(dir.z, -dir.x) = dx*dir.z - dz*dir.x
					// If the path and the wall are parallel or facing opposite directions (i.e. the object is moving away), then there is no intersection.
					if (mul16(dx, wall->wallDir.z) - mul16(dz, wall->wallDir.x) >= 0)
					{
						intersect = NO_INTERSECT;
					}
				}
				if (intersect == INTERSECT)
				{
					fixed16_16 dist = distApprox(s_col_path.x0, s_col_path.z0, pos->x, pos->z);
					if (dist < s_col_hitDist)
					{
						s_col_hitX = pos->x;
						s_col_hitZ = pos->z;
						s_col_hitDist = dist;
						hitWall = wall;
					}
				}
			}
		}
		if (hitWall)
		{
			hitWall->collisionFrame = s_collisionFrameWall;
			RWall* hitMirror = hitWall->mirrorWall;
			if (hitMirror)
			{
				hitMirror->collisionFrame = s_collisionFrameWall;
			}
		}
		return hitWall;
	}
		
	RSector* collision_tryMove(RSector* sector, fixed16_16 x0, fixed16_16 z0, fixed16_16 x1, fixed16_16 z1)
	{
		RSector* curSector = sector;
		fixed16_16 ceilHeight, floorHeight;
		fixed16_16 secHeight = curSector->secHeight;
		if (secHeight < 0)
		{
			floorHeight = sector->floorHeight + secHeight;
			if (floorHeight >= -COL_SEC_HEIGHT_OFFSET)
			{
				ceilHeight = curSector->ceilingHeight;
			}
			else
			{
				floorHeight = sector->floorHeight;
				ceilHeight = sector->floorHeight + secHeight;
			}
		}
		else
		{
			floorHeight = curSector->floorHeight + secHeight;
			ceilHeight = curSector->ceilingHeight;
		}
		if (curSector->flags1 & SEC_FLAGS1_PIT)
		{
			floorHeight += SEC_SKY_HEIGHT;
		}
		if (curSector->flags1 & SEC_FLAGS1_EXTERIOR)
		{
			ceilHeight -= SEC_SKY_HEIGHT;
		}
		s_col_ceilingHeight = ceilHeight;
		s_col_floorHeight   = floorHeight;

		// Abort early if there is no movement.
		fixed16_16 dx = x1 - x0;
		fixed16_16 dz = z1 - z0;
		if (dx == 0 && dz == 0)
		{
			return curSector;
		}

		s_col_path.x0 = x0;
		s_col_path.x1 = x1;
		s_col_path.z0 = z0;
		s_col_path.z1 = z1;
		s_collisionFrameWall++;

		RWall* wall = collision_pathWallCollision(curSector);
		fixed16_16 curFloorHeight = s_col_floorHeight;
		if (!wall)
		{
			return curSector;
		}

		while (wall)
		{
			RSector* nextSector = wall->nextSector;
			if (!nextSector)
			{
				return nullptr;
			}
			else
			{
				fixed16_16 nextSecHeight = nextSector->secHeight;
				if (nextSecHeight < 0)
				{
					floorHeight = nextSector->floorHeight + nextSecHeight;
					if (floorHeight >= -COL_SEC_HEIGHT_OFFSET)
					{
						ceilHeight = nextSector->ceilingHeight;
					}
					else
					{
						floorHeight = nextSector->floorHeight;
						ceilHeight = nextSector->floorHeight + nextSecHeight;
					}
				}
				else
				{
					floorHeight = nextSector->floorHeight + nextSecHeight;
					ceilHeight  = nextSector->ceilingHeight;
				}

				if (nextSector->flags1 & SEC_FLAGS1_PIT)
				{
					floorHeight += SEC_SKY_HEIGHT;
				}
				if (nextSector->flags1 & SEC_FLAGS1_EXTERIOR)
				{
					ceilHeight -= SEC_SKY_HEIGHT;
				}

				if (ceilHeight <= s_col_ceilingHeight)
				{
					ceilHeight = s_col_ceilingHeight;
				}
				s_col_ceilingHeight = ceilHeight;

				if (curFloorHeight > floorHeight)
				{
					curFloorHeight = floorHeight;
				}
				s_col_floorHeight = curFloorHeight;

				wall = collision_pathWallCollision(nextSector);
			}
			curSector = nextSector;
		}
		return curSector;
	}

	RWall* collision_wallCollisionFromPath(RSector* sector, fixed16_16 srcX, fixed16_16 srcZ, fixed16_16 dstX, fixed16_16 dstZ)
	{
		fixed16_16 dx = dstX - srcX;
		fixed16_16 dz = dstZ - srcZ;
		if (dx == 0 && dz == 0)
		{
			return nullptr;
		}

		s_col_path.x0 = srcX;
		s_col_path.x1 = dstX;
		s_col_path.z1 = dstZ;
		s_col_path.z0 = srcZ;
		s_collisionFrameWall++;

		return collision_pathWallCollision(sector);
	}

	RSector* collision_moveObj(SecObject* obj, fixed16_16 dx, fixed16_16 dz)
	{
		RSector* sector = obj->sector;
		fixed16_16 x0 = obj->posWS.x;
		fixed16_16 z0 = obj->posWS.z;
		fixed16_16 x1 = x0 + dx;
		fixed16_16 z1 = z0 + dz;

		RSector* newSector = collision_tryMove(sector, x0, z0, x1, z1);
		if (newSector)
		{
			obj->posWS.x = x1;
			obj->posWS.z = z1;
			if (newSector != sector)
			{
				sector_addObject(newSector, obj);
			}
		}
		return newSector;
	}
		
	SecObject* collision_getObjectCollision(RSector* sector, CollisionInterval* interval, SecObject* prevObj)
	{
		if (!sector) { return nullptr; }

		//s_infCurSector = sector;
		s_colObjPrev = prevObj;

		s_colObjX0 = interval->x0;
		s_colObjX1 = interval->x1;
		s_colObjZ0 = interval->z0;
		s_colObjZ1 = interval->z1;
		s_colObjInterval = interval;
		s_colObjList = sector->objectList;
		s_colObjCount = sector->objectCount;
		s_colObjMove = interval->move;
		s_colObjDirX = interval->dirX;
		s_colObjDirZ = interval->dirZ;

		fixed16_16 y0 = interval->y0;
		fixed16_16 y1 = interval->y1;
		s_colObjMaxY = max(y0, y1);
		s_colObjMinY = min(y0, y1);
		s_colObjY0 = y0;
		s_colObjY1 = y1;

		return internal_getObjectCollision();
	}

	void sector_calculateFloor(RSector* sector, fixed16_16 y, fixed16_16* floorHeight, fixed16_16* ceilHeight)
	{
		y -= COL_SEC_HEIGHT_OFFSET;	// adjust the y value to handle second heights.
		if (sector->secHeight < 0 && y > sector->floorHeight + sector->secHeight)
		{
			*floorHeight = sector->floorHeight;
			*ceilHeight  = sector->floorHeight + sector->secHeight;
		}
		else
		{
			*floorHeight = sector->floorHeight + sector->secHeight;
			*ceilHeight = sector->ceilingHeight;
		}

		if (sector->flags1 & SEC_FLAGS1_PIT) { *floorHeight += SEC_SKY_HEIGHT; }
		if (sector->flags1 & SEC_FLAGS1_EXTERIOR) { *ceilHeight -= SEC_SKY_HEIGHT; }
	}

	JBool collision_lineOfSight(RSector* sector0, RSector* sector1, vec3_fixed pos0, vec3_fixed pos1, u32 wallFlags3)
	{
		fixed16_16 len = distApprox(pos0.x, pos0.z, pos1.x, pos1.z);
		fixed16_16 dy  = pos1.y - pos0.y;
		fixed16_16 slope = len ? div16(dy, len) : dy;

		RSector* sector = sector0;
		RWall* hitWall = collision_wallCollisionFromPath(sector, pos0.x, pos0.z, pos1.x, pos1.z);
		while (hitWall)
		{
			if (!hitWall->nextSector || (hitWall->flags3 & wallFlags3))
			{
				return JFALSE;
			}

			len = collision_getHitDistance();
			fixed16_16 y = pos0.y + mul16(len, slope);

			RSector* hitSector = hitWall->sector;
			RSector* hitNextSector = hitWall->nextSector;
			if (y < hitSector->ceilingHeight || y < hitNextSector->ceilingHeight) { return JFALSE; }
			if (y > hitSector->floorHeight || y > hitNextSector->floorHeight) { return JFALSE; }

			sector = hitNextSector;
			hitWall = collision_pathWallCollision(sector);
		}

		return (sector == sector1) ? JTRUE : JFALSE;
	}

	// Determines if an object with the correct entityFlag(s) is in range (radius) of (x,y,z) in sector and is not skipObj.
	// Note only objects with a clear line-of-sight are accepted.
	JBool collision_isAnyObjectInRange(RSector* sector, fixed16_16 radius, vec3_fixed origin, SecObject* skipObj, u32 entityFlags)
	{
		if (!sector)
		{
			return JFALSE;
		}

		fixed16_16 x0 = origin.x - radius;
		fixed16_16 y0 = origin.y - radius;
		fixed16_16 z0 = origin.z - radius;

		fixed16_16 x1 = origin.x + radius;
		fixed16_16 y1 = origin.y + radius;
		fixed16_16 z1 = origin.z + radius;

		fixed16_16 secHeightThreshold = origin.y - COL_SEC_HEIGHT_OFFSET;
		RSector* curSector = s_levelState.sectors;

		for (u32 i = 0; i < s_levelState.sectorCount; i++, curSector++)
		{
			///////////////////////////////////////////////
			// These tests should only happen once I think,
			// unless x0, x1, z0, z1 change over time.
			///////////////////////////////////////////////
			if (x0 > sector->boundsMax.x || x1 < sector->boundsMin.x || z0 > sector->boundsMax.z || z1 < sector->boundsMin.z)
			{
				continue;
			}

			fixed16_16 floorHeight, ceilHeight;
			sector_calculateFloor(sector, origin.y, &floorHeight, &ceilHeight);
			if (floorHeight < y0 || ceilHeight > y1)
			{
				continue;
			}
			/////////////////////////////////////////////
			// End of checks to pull out of the loop.
			/////////////////////////////////////////////

			s32 objCapacity = curSector->objectCapacity;
			s32 objCount = curSector->objectCount;
			for (s32 objListIndex = 0, objIndex = 0; objIndex < objCount && objListIndex < objCapacity; objListIndex++)
			{
				SecObject* obj = curSector->objectList[objListIndex];
				if (!obj) { continue; }

				objIndex++;
				if (skipObj && skipObj == obj) { continue; }
				if (!(obj->entityFlags & entityFlags)) { continue; }
				if (obj->posWS.x < x0 || obj->posWS.x > x1 || obj->posWS.z < z0 || obj->posWS.z > z1 || obj->posWS.y < y0 || obj->posWS.y > y1)
				{
					continue;
				}

				RSector* curSector = sector;
				RWall* hitWall = collision_wallCollisionFromPath(sector, origin.x, origin.z, obj->posWS.x, obj->posWS.z);
				while (hitWall && curSector && curSector != obj->sector)
				{
					curSector = hitWall->nextSector;
					if (curSector)
					{
						if (curSector->floorHeight - curSector->ceilingHeight < HALF_16)
						{
							break;
						}
						hitWall = collision_pathWallCollision(curSector);
					}
				}

				if (curSector == obj->sector)
				{
					return JTRUE;
				}
			}
		}
		return JFALSE;
	}
		
	// Call the effectFunc() for each object within 'range' of point (x,y,z). This will only be called for objects in range and that have a valid collision path.
	// Note the collision path is 3D (XYZ), in that it takes into account collision based on height.
	void collision_effectObjectsInRange3D(RSector* startSector, fixed16_16 range, vec3_fixed origin, CollisionEffectFunc effectFunc, SecObject* excludeObj, u32 entityFlags)
	{
		const fixed16_16 x0 = origin.x - range;
		const fixed16_16 y0 = origin.y - range;
		const fixed16_16 z0 = origin.z - range;

		const fixed16_16 x1 = origin.x + range;
		const fixed16_16 y1 = origin.y + range;
		const fixed16_16 z1 = origin.z + range;

		const fixed16_16 secHeightThreshold = origin.y - COL_SEC_HEIGHT_OFFSET;
		RSector* sector = s_levelState.sectors;
		for (u32 i = 0; i < s_levelState.sectorCount; i++, sector++)
		{
			// Checks the start sector, should be pulled out of the loop.
			if (x0 > startSector->boundsMax.x || x1 < startSector->boundsMin.x || z0 > startSector->boundsMax.z || z1 < startSector->boundsMin.z)
			{
				continue;
			}

			fixed16_16 floor, ceil;
			sector_calculateFloor(sector, origin.y, &floor, &ceil);
			if (y0 > floor || y1 < ceil) { continue; }
			// End of start sector check.

			for (s32 objIndex = 0, objListIndex = 0; objIndex < sector->objectCount && objListIndex < sector->objectCapacity; objListIndex++)
			{
				SecObject* obj = sector->objectList[objListIndex];
				if (!obj) { continue; }
				objIndex++;

				if (excludeObj && excludeObj == obj) { continue; }
				if (!(obj->entityFlags & entityFlags)) { continue; }
				if (obj->posWS.x < x0 || obj->posWS.x > x1 || obj->posWS.z < z0 || obj->posWS.z > z1 || obj->posWS.y < y0 || obj->posWS.y > y1)
				{
					continue;
				}
								
				JBool canHit = collision_lineOfSight(startSector, obj->sector, origin, obj->posWS, WF3_CANNOT_FIRE_THROUGH);
				if (!canHit)
				{
					vec3_fixed topPos = { obj->posWS.x, obj->posWS.y - obj->worldHeight, obj->posWS.z };
					canHit = collision_lineOfSight(startSector, obj->sector, origin, topPos, WF3_CANNOT_FIRE_THROUGH);
				}
				// Finally the object can be hit, so call the effect function.
				if (canHit)
				{
					effectFunc(obj);
				}
			}  // Object Loop.
		}  // Sector loop.
	}

	// Call the effectFunc() for each object within 'range' of point (x,y,z). This will only be called for objects in range and that have a valid collision path.
	// Note the collision path is 2D (XZ) but cannot pass through sectors with a gap smaller than 0.5 units.
	void collision_effectObjectsInRangeXZ(RSector* startSector, fixed16_16 range, vec3_fixed origin, CollisionEffectFunc effectFunc, SecObject* excludeObj, u32 entityFlags)
	{
		if (!startSector)
		{
			return;
		}

		const fixed16_16 x0 = origin.x - range;
		const fixed16_16 y0 = origin.y - range;
		const fixed16_16 z0 = origin.z - range;

		const fixed16_16 x1 = origin.x + range;
		const fixed16_16 y1 = origin.y + range;
		const fixed16_16 z1 = origin.z + range;

		const fixed16_16 secHeightThreshold = origin.y - COL_SEC_HEIGHT_OFFSET;
		RSector* sector = s_levelState.sectors;
		for (u32 i = 0; i < s_levelState.sectorCount; i++, sector++)
		{
			// Checks the start sector, should be pulled out of the loop.
			if (x0 > startSector->boundsMax.x || x1 < startSector->boundsMin.x || z0 > startSector->boundsMax.z || z1 < startSector->boundsMin.z)
			{
				continue;
			}
			fixed16_16 floor, ceil;
			sector_calculateFloor(startSector, origin.y, &floor, &ceil);
			if (y0 > floor || y1 < ceil)
			{
				continue;
			}
			// End of start sector check.

			for (s32 objIndex = 0, objListIndex = 0; objIndex < sector->objectCount && objListIndex < sector->objectCapacity; objListIndex++)
			{
				SecObject* obj = sector->objectList[objListIndex];
				if (!obj) { continue; }
				objIndex++;

				if (excludeObj && excludeObj == obj) { continue; }
				if (!(obj->entityFlags & entityFlags)) { continue; }
				if (obj->posWS.x < x0 || obj->posWS.x > x1 || obj->posWS.z < z0 || obj->posWS.z > z1 || obj->posWS.y < y0 || obj->posWS.y > y1)
				{
					continue;
				}

				fixed16_16 dx = obj->posWS.x - origin.x;
				fixed16_16 dz = obj->posWS.z - origin.z;
				RWall* hitWall = nullptr;
				if (dx || dz)
				{
					s_col_path.x0 = origin.x;
					s_col_path.z0 = origin.z;
					s_col_path.x1 = obj->posWS.x;
					s_col_path.z1 = obj->posWS.z;
					s_collisionFrameWall++;
					hitWall = collision_pathWallCollision(startSector);
				}

				RSector* nextSector = startSector;
				while (hitWall && nextSector && nextSector != obj->sector)
				{
					nextSector = hitWall->nextSector;
					if (nextSector)
					{
						const fixed16_16 height = nextSector->floorHeight - nextSector->ceilingHeight;
						if (height < c_minTraversableOpening)
						{
							break;
						}
						hitWall = collision_pathWallCollision(nextSector);
					}
				}

				// If there is a clear path from the source position to the object in range, call the specified function.
				if (nextSector == obj->sector)
				{
					effectFunc(obj);
				}
			}  // Object Loop.
		}  // Sector Loop.
	}
		
	static RSector*   s_hcolSector;
	static vec3_fixed s_hcolDstPos;
	static vec3_fixed s_hcolSrcPos;
	static SecObject* s_hcolObj;

	// Gets the dot product of the movement vector and response direction vector, and applies it to the response direction
	// vector to obtain the resulting movement vector
	// Causes NPCs to slide against a wall that they collide with
	void handleCollisionResponseSimple(fixed16_16 dirX, fixed16_16 dirZ, fixed16_16* moveX, fixed16_16* moveZ)
	{
		fixed16_16 proj = mul16(*moveX, dirX) + mul16(*moveZ, dirZ);
		*moveX = mul16(proj, dirX);
		*moveZ = mul16(proj, dirZ);
	}

	// Returns JTRUE if there is no collision.
	JBool handleCollision(CollisionInfo* colInfo)
	{
		SecObject* obj = colInfo->obj;
		RWall* colWall = nullptr;
		s32 b = 0;
		SecObject* colObj = nullptr;
		s_hcolSector = obj->sector;
		RSector* curSector = s_hcolSector;
		s_hcolSrcPos = obj->posWS;

		s_hcolObj = obj;
		s_hcolDstPos.x = s_hcolSrcPos.x + colInfo->offsetX;
		s_hcolDstPos.y = s_hcolSrcPos.y + colInfo->offsetY;
		s_hcolDstPos.z = s_hcolSrcPos.z + colInfo->offsetZ;

		fixed16_16 top = s_hcolSrcPos.y - colInfo->height;
		fixed16_16 bot = s_hcolSrcPos.y - colInfo->stepUpHeight;

		colInfo->responseStep = JFALSE;
		fixed16_16 yMin = (colInfo->flags & COLINFO_INFINITE_DROP) ? (s_hcolSrcPos.y + COL_INFINITY) : (s_hcolSrcPos.y + colInfo->stepDownHeight);
		colInfo->flags &= ~COLINFO_INFINITE_DROP;

		// Cross walls until the collision path hits something solid.
		// Build a list of crossed walls in order to send INF events later.
		// Note the number is based on the stack allocations, i.e. the next used offset is 0x80 and the size is 4 per element.
		RWall* wallCrossList[32];
		RWall* wall = collision_wallCollisionFromPath(curSector, s_hcolSrcPos.x, s_hcolSrcPos.z, s_hcolDstPos.x, s_hcolDstPos.z);
		s32 wallCrossCount = 0;
		while (wall)
		{
			RSector* next = wall->nextSector;
			if (next && !(wall->flags3 & WF3_SOLID_WALL))
			{
				if ((s_hcolObj->entityFlags & ETFLAG_AI_ACTOR) && (wall->flags3 & WF3_PLAYER_WALK_ONLY))
				{
					break;	// AI actors cannot cross a wall with the WF3_PLAYER_WALK_ONLY flag
				}

				fixed16_16 ceilHeight, floorHeight;
				sector_getObjFloorAndCeilHeight(next, s_hcolSrcPos.y, &floorHeight, &ceilHeight);
				
				if (floorHeight < bot          // next sector's floor is too high (factoring in the entity's stepUpHeight)
					|| floorHeight > yMin      // next sector's floor is too low (factoring in the entity's stepDownHeight) - this prevents actors from walking off cliffs
					|| ceilHeight > top)       // next sector's ceiling is too low
				{
					break;
				}

				// Successfully crossed the wall - add to the list and move on to the next sector
				wallCrossList[wallCrossCount++] = wall;
				wall = collision_pathWallCollision(next);
				curSector = next;
			}
			else
			{
				break;  // wall is unadjoined or has the WF3_SOLID_WALL flag
			}
		}

		// Collision above hit a solid wall.
		if (wall)
		{
			colInfo->responseStep = JTRUE;
			colInfo->responseDir.x = wall->wallDir.x;
			colInfo->responseDir.z = wall->wallDir.z;

			vec2_fixed* w0 = wall->w0;
			colInfo->responsePos.x = w0->x;
			colInfo->responsePos.z = w0->z;
			colInfo->responseAngle = wall->angle;

			s_hcolDstPos.x = s_hcolSrcPos.x;
			s_hcolDstPos.z = s_hcolSrcPos.z;

			curSector = s_hcolSector;
			colWall = wall;
		}
		else if (colInfo->width)
		{
			s_colDstPosX = s_hcolDstPos.x;
			s_colDstPosY = s_hcolDstPos.y;
			s_colDstPosZ = s_hcolDstPos.z;
			s_colWidth = colInfo->width;

			s_colSrcPosX = s_hcolSrcPos.x;
			s_colSrcPosY = s_hcolSrcPos.y;
			s_colSrcPosZ = s_hcolSrcPos.z;

			s_colHeightBase = s_hcolObj->worldHeight;
			s_colBottom = bot;
			s_colTop = top;
			s_colY1 = yMin;

			s_colWall0 = wall;
			s_colObj1.wall = wall;
			s_colHeight = colInfo->height;
			if (s_hcolObj->entityFlags & ETFLAG_PLAYER)
			{
				s_colObject.obj = s_hcolObj;
			}
			else
			{
				s_colObject.wall = wall;
			}

			if (!col_computeCollisionResponse(curSector, 0))
			{
				colWall = s_colWall0;
				colObj = s_collidedObj;
				// Rewind the destination position to the start.
				s_hcolDstPos.x = s_hcolSrcPos.x;
				s_hcolDstPos.z = s_hcolSrcPos.z;
				colInfo->responseStep  = s_colResponseStep;
				colInfo->responseDir = s_colResponseDir;
				colInfo->responsePos = s_colResponsePos;
				colInfo->responseAngle = s_colResponseAngle;

				curSector = s_hcolSector;
			}
		}

		if (curSector != s_hcolSector)
		{
			sector_addObject(curSector, s_hcolObj);  // object has crossed into a new sector
		}

		// Update the object XZ position.
		s_hcolObj->posWS.x = s_hcolDstPos.x;
		s_hcolObj->posWS.z = s_hcolDstPos.z;

		// Determine the floor and ceiling height for the current sector based on the object position.
		fixed16_16 floorHeight, ceilHeight;
		sector_getObjFloorAndCeilHeight(s_hcolObj->sector, s_hcolSrcPos.y, &floorHeight, &ceilHeight);

		// Clip the y position to the floor and ceiling heights.
		top = s_hcolDstPos.y - colInfo->height;
		if (top <= ceilHeight)	// is the top clipped by the ceiling?
		{
			// The object collides with the ceiling.
			s_hcolDstPos.y = ceilHeight + colInfo->height;
		}
		else if (s_hcolDstPos.y > floorHeight)
		{
			// The object collides with the floor.
			s_hcolDstPos.y = floorHeight;
		}
		s_hcolObj->posWS.y = s_hcolDstPos.y;

		// These values are probably filled in at 2346a6, 23474f, and/or 23487f - which need to be figured out first.
		colInfo->wall = colWall;
		colInfo->u24 = b;
		colInfo->collidedObj = colObj;
		if (colWall || b || colObj)
		{
			return JFALSE;
		}

		// Go through all passable walls the object crossed and send cross line events.
		for (; wallCrossCount; wallCrossCount--)
		{
			RWall* wall = wallCrossList[wallCrossCount - 1];
			inf_triggerWallEvent(wall, s_hcolObj, INF_EVENT_CROSS_LINE_FRONT);

			// Note: This is actually a bug... since inf_triggerWallEvent() automatically calls the event*2 on the backside
			// This not only causes that to be called twice but calls INF_EVENT_ENTER_SECTOR on wall.
			// This doesn't seem to hurt things but I doubt it was actually intended and lead to odd behaviors.
			// This should not be called here (but it is in the original Dark Forces code).
			if (wall->mirrorWall)
			{
				inf_triggerWallEvent(wall->mirrorWall, s_hcolObj, INF_EVENT_CROSS_LINE_BACK);
			}
		}
		return JTRUE;
	}

	////////////////////////////////////////////////////////
	// Internal
	////////////////////////////////////////////////////////
	IntersectionResult pathIntersectsWall(ColPath* path, RWall* wall)
	{
		s_col_pathX0 = path->x0;
		s_col_pathX1 = path->x1;
		s_col_pathZ0 = path->z0;
		s_col_pathZ1 = path->z1;

		vec2_fixed* v0 = wall->w0;
		s_col_wallX0 = v0->x;
		s_col_wallZ0 = v0->z;

		vec2_fixed* v1 = wall->w1;
		s_col_wallX1 = v1->x;
		s_col_wallZ1 = v1->z;

		s_col_curWall = wall;
		s_col_pathDx = s_col_pathX1 - s_col_pathX0;
		s_col_intersect = NO_INTERSECT;
		s_col_wallDx = s_col_wallX0 - s_col_wallX1;
		if (s_col_pathDx < 0)
		{
			s_col_adjPathX1 = s_col_pathX0;
			s_col_adjPathX0 = s_col_pathX1;
		}
		else
		{
			s_col_adjPathX0 = s_col_pathX0;
			s_col_adjPathX1 = s_col_pathX1;
		}
		if (s_col_wallDx > 0 && (s_col_adjPathX1 < s_col_wallX1 || s_col_wallX0 < s_col_adjPathX0))
		{
			return NO_INTERSECT;
		}
		else if (s_col_wallDx <= 0 && (s_col_adjPathX1 < s_col_wallX0 || s_col_wallX1 < s_col_adjPathX0))
		{
			return NO_INTERSECT;
		}

		s_col_pathDz = s_col_pathZ1 - s_col_pathZ0;
		s_col_wallDz = s_col_wallZ0 - s_col_wallZ1;
		if (s_col_pathDz < 0)
		{
			s_col_adjPathZ1 = s_col_pathZ0;
			s_col_adjPathZ0 = s_col_pathZ1;
		}
		else
		{
			s_col_adjPathZ0 = s_col_pathZ0;
			s_col_adjPathZ1 = s_col_pathZ1;
		}

		if (s_col_wallDz > 0 && s_col_adjPathZ1 < s_col_wallZ1)
		{
			return NO_INTERSECT;
		}
		if (s_col_wallZ0 < s_col_adjPathZ0 && (s_col_adjPathZ1 < s_col_wallZ0 || s_col_wallZ1 < s_col_adjPathZ0))
		{
			return NO_INTERSECT;
		}

		s_col_XOffset = s_col_pathX0 - s_col_wallX0;
		s_col_ZOffset = s_col_pathZ0 - s_col_wallZ0;
		s_col_intersectNum = mul16(s_col_wallDz, s_col_XOffset) - mul16(s_col_wallDx, s_col_ZOffset);
		s_col_intersectDen = mul16(s_col_pathDz, s_col_wallDx) - mul16(s_col_pathDx, s_col_wallDz);

		if (s_col_intersectDen <= 0 && (s_col_intersectNum > 0 || s_col_intersectNum < s_col_intersectDen))
		{
			return NO_INTERSECT;
		}
		else if (s_col_intersectDen > 0 && s_col_intersectDen > s_col_intersectNum)
		{
			return NO_INTERSECT;
		}

		s_col_intersectNum2 = mul16(s_col_pathDx, s_col_ZOffset) - mul16(s_col_pathDz, s_col_XOffset);
		if (s_col_intersectDen > 0 && s_col_intersectNum2 > s_col_intersectDen)
		{
			return NO_INTERSECT;
		}
		if (s_col_intersectNum2 > 0 || s_col_intersectNum2 < s_col_intersectDen)
		{
			return NO_INTERSECT;
		}
		if (s_col_intersectDen == 0)
		{
			return NO_INTERSECT;
		}

		s_col_intersect = INTERSECT;
		return INTERSECT;
	}

	vec2_fixed* computeIntersectPos()
	{
		if (s_col_intersect == INTERSECT)
		{
			s_col_intersectParam = div16(s_col_intersectNum, s_col_intersectDen);
			s_col_intersectPos.x = s_col_pathX0 + mul16(s_col_intersectParam, s_col_pathDx);
			s_col_intersectPos.z = s_col_pathZ0 + mul16(s_col_intersectParam, s_col_pathDz);
		}
		else
		{
			s_col_intersectPos.x = 0;
			s_col_intersectPos.z = 0;
		}
		return &s_col_intersectPos;
	}

	// This seems to get the first collision hit, regardless if it is the closest.
	// This means projectile/object collisions may get wonky with a high time interval (low framerate).
	SecObject* internal_getObjectCollision()
	{
		for (; s_colObjCount > 0; s_colObjList++)
		{
			SecObject* obj = *s_colObjList;
			if (!obj) { continue; }

			s_colObjCount--;
			if (!obj->worldWidth || obj == s_colObjPrev) { continue; }

			if (!(obj->entityFlags & s_collision_excludeEntityFlags) && !(obj->entityFlags & ETFLAG_PICKUP))
			{
				// Check height overlap.
				if (obj->worldHeight >= 0 && (s_colObjMinY > obj->posWS.y || obj->posWS.y - obj->worldHeight > s_colObjMaxY))
				{
					continue;
				}
				else if (obj->worldHeight < 0 && (obj->posWS.y > s_colObjMaxY || obj->posWS.y - obj->worldHeight < s_colObjMinY))
				{
					// Since height < 0, this is the same as y + abs(height), i.e. this is hanging from the ceiling.
					continue;
				}

				s_colObjOffsetX = obj->posWS.x - s_colObjX0;
				s_colObjOffsetZ = obj->posWS.z - s_colObjZ0;
				s_colWallV0.x = mul16(s_colObjOffsetX, s_colObjDirZ) - mul16(s_colObjOffsetZ, s_colObjDirX);
				if (TFE_Jedi::abs(s_colWallV0.x) > obj->worldWidth) { continue; }

				s_colWallV0.z = mul16(s_colObjOffsetX, s_colObjDirX) + mul16(s_colObjOffsetZ, s_colObjDirZ);

				fixed16_16 pathRadius = s_colObjMove + obj->worldWidth;
				// the projectile hasn't move far enough to hit the object.
				if (pathRadius < s_colWallV0.z) { continue; }
				// the projectile is past the object far enough that it doesn't hit.
				if (-obj->worldWidth > s_colWallV0.z) { continue; }

				s_colObjOverlap = s_colWallV0.z - obj->worldWidth;
				return obj;
			}
		}
		return nullptr;
	}

	// Treat walls with flags3 that includes 'exclWallFlags3' as solid.
	JBool collision_canHitObject(RSector* startSector, RSector* endSector, vec3_fixed p0, vec3_fixed p1, u32 exclWallFlags3)
	{
		s_collision_wallHit = JFALSE;
		fixed16_16 approxDist = distApprox(p0.x, p0.z, p1.x, p1.z);
		fixed16_16 dy = p1.y - p0.y;
		fixed16_16 yStep = approxDist ? div16(dy, approxDist) : dy;

		RSector* sector = startSector;
		RWall* hitWall = nullptr;

		// If there is no horizontal movement, there is no possible wall collision.
		if (p1.x - p0.x != 0 || p1.z - p0.z != 0)
		{
			s_col_path.x0 = p0.x;
			s_col_path.z0 = p0.z;
			s_col_path.x1 = p1.x;
			s_col_path.z1 = p1.z;
			s_collisionFrameWall++;
			hitWall = collision_pathWallCollision(sector);
		}
		while (hitWall)
		{
			RSector* nextSector = hitWall->nextSector;
			if (!nextSector)
			{
				s_collision_wallHit = JTRUE;
				return JFALSE;
			}
			if (hitWall->flags3 & exclWallFlags3)
			{
				return JFALSE;
			}
			fixed16_16 yHit = p0.y + mul16(s_col_hitDist, yStep);
			RSector* hitSector = hitWall->sector;
			if (yHit < hitSector->ceilingHeight || yHit < nextSector->ceilingHeight || yHit > hitSector->floorHeight || yHit > nextSector->floorHeight)
			{
				return JFALSE;
			}
			sector = nextSector;
			hitWall = collision_pathWallCollision(nextSector);
		}
		return (sector == endSector) ? JTRUE : JFALSE;
	}

	fixed16_16 computeYIntersect(const vec3_fixed p0, const vec3_fixed p1, f32 scaleXZ)
	{
		const f32 dx = fixed16ToFloat(p1.x - p0.x);
		const f32 dy = fixed16ToFloat(p1.y - p0.y);
		const f32 dz = fixed16ToFloat(p1.z - p0.z);
		const f32 u = sqrtf(dx*dx + dz * dz) * scaleXZ;
		return p0.y + floatToFixed16(u * dy);
	}

	bool hitSign(RWall* hitWall, fixed16_16 paramPos, fixed16_16 yPos)
	{
		fixed16_16 baseHeight = 0;
		fixed16_16 uOffset = 0;
		switch (hitWall->drawFlags)
		{
		case WDF_MIDDLE:
		{
			baseHeight = hitWall->sector->floorHeight;
			uOffset = hitWall->midOffset.x;
		} break;
		case WDF_TOP:
		{
			baseHeight = hitWall->nextSector->ceilingHeight;
			uOffset = hitWall->topOffset.x;
		} break;
		case WDF_BOT:
		case WDF_TOP_AND_BOT:
		{
			baseHeight = hitWall->sector->floorHeight;
			uOffset = hitWall->botOffset.x;
		} break;
		}

		if (yPos)
		{
			fixed16_16 base = baseHeight + (hitWall->signOffset.z >> 3);
			// base + 0.5
			fixed16_16 y0 = base + HALF_16;
			// base - texHeight/8 - 0.5
			fixed16_16 y1 = base - ((*hitWall->signTex)->height << 13) - HALF_16;
			if (y0 < yPos || y1 > yPos)
			{
				return false;
			}
		}

		if (paramPos)
		{
			fixed16_16 base = (hitWall->signOffset.x - uOffset) >> 3;
			// base - 0.5
			fixed16_16 left = base - HALF_16;
			// base + texWidth/8 + 0.5
			fixed16_16 right = base + ((*hitWall->signTex)->width << 13) + HALF_16;
			if (paramPos < left || paramPos > right)
			{
				return false;
			}
		}

		return true;
	}

	RayHitInfo collision_rayCast3d(RSector* sector, vec3_fixed p0, vec3_fixed p1, bool stopAtMidTex)
	{
		RayHitInfo outRayHit = { RHit_None, nullptr, nullptr };
		fixed16_16 ceilHeight, floorHeight;

		// Use existing code to raytrace through the scene.
		RWall* wall = collision_wallCollisionFromPath(sector, p0.x, p0.z, p1.x, p1.z);

		// No walls are hit, so just test against the floor and ceiling in the current sector.
		if (!wall)
		{
			fixed16_16 y0 = p0.y;
			fixed16_16 y1 = p1.y;
			floorHeight = sector->floorHeight;
			ceilHeight = sector->ceilingHeight;

			fixed16_16 nextY = y1;
			if (y1 >= floorHeight)
			{
				outRayHit.hit = RHit_Floor;
				outRayHit.sector = sector;
			}
			else if (y1 <= ceilHeight)
			{
				outRayHit.hit = RHit_Ceiling;
				outRayHit.sector = sector;
			}
			return outRayHit;
		}

		// Compute the ray length and direction using floats to avoid overflow.
		const vec2_float offset = { fixed16ToFloat(p1.x) - fixed16ToFloat(p0.x),
									fixed16ToFloat(p1.z) - fixed16ToFloat(p0.z) };
		const f32 lenXZ = sqrtf(offset.x * offset.x + offset.z * offset.z);

		if (lenXZ < FLT_EPSILON) { return outRayHit; }
		const f32 scaleXZ = 1.0f / lenXZ;

		// Trace through the sectors.
		while (wall)
		{
			fixed16_16 bot, top;
			if (wall->nextSector && (!(wall->flags1 & WF1_ADJ_MID_TEX) || !stopAtMidTex))
			{
				// Determine the opening in the wall.
				wall_getOpeningHeightRange(wall, &top, &bot);

				// Determine if the ray trivially passes through.
				if (p0.y <= bot && p0.y >= top && p1.y <= bot && p1.y >= top)
				{
					sector = wall->nextSector;
					wall = collision_pathWallCollision(sector);
					continue;
				}
			}
			else
			{
				// There is no opening in the wall.
				bot = -SEC_SKY_HEIGHT;
				top = SEC_SKY_HEIGHT;
			}

			collision_getHitPoint(&p1.x, &p1.z);

			const fixed16_16 y = computeYIntersect(p0, p1, scaleXZ);
			floorHeight = sector->floorHeight;
			ceilHeight = sector->ceilingHeight;

			if (y >= floorHeight)
			{
				outRayHit.hit = RHit_Floor;
				outRayHit.sector = sector;
				break;
			}
			else if (y <= ceilHeight)
			{
				outRayHit.hit = RHit_Ceiling;
				outRayHit.sector = sector;
				break;
			}

			// Check to see if it can pass through the opening.
			if (y <= bot && y >= top)
			{
				sector = wall->nextSector;
				wall = collision_pathWallCollision(sector);
			}
			// Wall Hit!
			else
			{
				outRayHit.hit = RHit_WallMid;
				if (wall->signTex && *wall->signTex)
				{
					fixed16_16 distFromW0 = vec2Length(wall->w0->x - p1.x, wall->w0->z - p1.z);
					if (hitSign(wall, distFromW0, y))
					{
						outRayHit.hit = RHit_WallSign;
						outRayHit.sector = sector;
						outRayHit.wall = wall;
						break;
					}
				}

				if (wall->nextSector && wall->nextSector->ceilingHeight > sector->ceilingHeight && y < wall->nextSector->ceilingHeight)
				{
					outRayHit.hit = RHit_WallTop;
				}
				else if (wall->nextSector && wall->nextSector->floorHeight < sector->floorHeight && y > wall->nextSector->floorHeight)
				{
					outRayHit.hit = RHit_WallBot;
				}
				else if (wall->nextSector) // Is this a transparent mid texture?
				{
					outRayHit.hit = RHit_WallMid_Trans;
				}

				outRayHit.sector = sector;
				outRayHit.wall = wall;
				break;
			}
		}
		return outRayHit;
	}

	JBool collision_propogateExplosion(RSector* sector)
	{
		message_sendToSector(sector, nullptr, INF_EVENT_EXPLOSION, MSG_TRIGGER);
		return JTRUE;
	}

	JBool inf_handleExplosion(RSector* sector, fixed16_16 x, fixed16_16 z, fixed16_16 range)
	{
		message_sendToSector(sector, nullptr, INF_EVENT_EXPLOSION, MSG_TRIGGER);

		s_colDstPosX = x;
		s_colDstPosZ = z;
		s_colWidth = range;
		s_objCollisionFunc = collision_propogateExplosion;
		s_objCollisionProxFunc = nullptr;
		s_colDoubleRadius = range * 2;
		s_collisionFrameSector += 2;

		return handleCollisionFunc(sector);
	}
}