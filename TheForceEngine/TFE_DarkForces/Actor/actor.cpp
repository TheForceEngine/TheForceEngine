#include <cstring>

#include "actor.h"
#include "actorInternal.h"
#include "../logic.h"
#include "../gameMusic.h"
#include "../sound.h"
#include "animTables.h"
#include "actorModule.h"
#include "mousebot.h"
#include "dragon.h"
#include "bobaFett.h"
#include "welder.h"
#include "turret.h"
#include "phaseOne.h"
#include "phaseTwo.h"
#include "phaseThree.h"
#include <TFE_Game/igame.h>
#include <TFE_DarkForces/random.h>
#include <TFE_DarkForces/player.h>
#include <TFE_DarkForces/hitEffect.h>
#include <TFE_DarkForces/projectile.h>
#include <TFE_DarkForces/pickup.h>
#include <TFE_DarkForces/player.h>
#include <TFE_Jedi/Level/rsector.h>
#include <TFE_Jedi/Level/rwall.h>
#include <TFE_Jedi/Level/levelData.h>
#include <TFE_Jedi/InfSystem/message.h>
#include <TFE_Jedi/Memory/list.h>
#include <TFE_Jedi/Memory/allocator.h>
#include <TFE_Jedi/Serialization/serialization.h>
#include <TFE_Settings/settings.h>

using namespace TFE_Jedi;

namespace TFE_DarkForces
{
	///////////////////////////////////////////
	// Internal State
	///////////////////////////////////////////
	ActorInternalState s_istate = { 0 };
	List* s_physicsActors = nullptr;

	///////////////////////////////////////////
	// Shared State
	///////////////////////////////////////////
	ActorState s_actorState = { 0 };
	SoundSourceId s_alertSndSrc[ALERT_COUNT];
	SoundSourceId s_officerAlertSndSrc[OFFICER_ALERT_COUNT];
	SoundSourceId s_stormAlertSndSrc[STORM_ALERT_COUNT];
	SoundSourceId s_agentSndSrc[AGENTSND_COUNT];

	///////////////////////////////////////////
	// Forward Declarations
	///////////////////////////////////////////
	void actorLogicTaskFunc(MessageType msg);
	void actorLogicMsgFunc(MessageType msg);
	void actorPhysicsTaskFunc(MessageType msg);
	u32  actorLogicSetupFunc(Logic* logic, KEYWORD key);

	extern ThinkerModule* actor_createFlyingModule(Logic* logic);
	extern ThinkerModule* actor_createFlyingModule_Remote(Logic* logic);
	
	///////////////////////////////////////////
	// API Implementation
	///////////////////////////////////////////
	void actor_clearState()
	{
		memset(&s_istate, 0, sizeof(ActorInternalState));
		memset(&s_actorState, 0, sizeof(ActorState));
		s_istate.objCollisionEnabled = JTRUE;
		list_clear(s_physicsActors);

		// Clear specific actor state.
		mousebot_clear();
		welder_clear();
	}

	void actor_exitState()
	{
		actor_clearState();
		phaseOne_exit();
		phaseTwo_exit();
		phaseThree_exit();
		bobaFett_exit();
		kellDragon_exit();
		mousebot_exit();
		turret_exit();
		welder_exit();
	}

	void actor_loadSounds()
	{
		s_alertSndSrc[ALERT_GAMOR]    = sound_load("gamor-3.voc",  SOUND_PRIORITY_MED5);
		s_alertSndSrc[ALERT_REEYEE]   = sound_load("reeyee-1.voc", SOUND_PRIORITY_MED5);
		s_alertSndSrc[ALERT_BOSSK]    = sound_load("bossk-1.voc",  SOUND_PRIORITY_MED5);
		s_alertSndSrc[ALERT_CREATURE] = sound_load("creatur1.voc", SOUND_PRIORITY_MED5);
		s_alertSndSrc[ALERT_PROBE]    = sound_load("probe-1.voc",  SOUND_PRIORITY_MED5);
		s_alertSndSrc[ALERT_INTDROID] = sound_load("intalert.voc", SOUND_PRIORITY_MED5);

		s_officerAlertSndSrc[0] = sound_load("ranofc02.voc", SOUND_PRIORITY_MED5);
		s_officerAlertSndSrc[1] = sound_load("ranofc04.voc", SOUND_PRIORITY_MED5);
		s_officerAlertSndSrc[2] = sound_load("ranofc05.voc", SOUND_PRIORITY_MED5);
		s_officerAlertSndSrc[3] = sound_load("ranofc06.voc", SOUND_PRIORITY_MED5);

		s_stormAlertSndSrc[0] = sound_load("ransto01.voc", SOUND_PRIORITY_MED5);
		s_stormAlertSndSrc[1] = sound_load("ransto02.voc", SOUND_PRIORITY_MED5);
		s_stormAlertSndSrc[2] = sound_load("ransto03.voc", SOUND_PRIORITY_MED5);
		s_stormAlertSndSrc[3] = sound_load("ransto04.voc", SOUND_PRIORITY_MED5);
		s_stormAlertSndSrc[4] = sound_load("ransto05.voc", SOUND_PRIORITY_MED5);
		s_stormAlertSndSrc[5] = sound_load("ransto06.voc", SOUND_PRIORITY_MED5);
		s_stormAlertSndSrc[6] = sound_load("ransto07.voc", SOUND_PRIORITY_MED5);
		s_stormAlertSndSrc[7] = sound_load("ransto08.voc", SOUND_PRIORITY_MED5);

		// Attacks, hurt, death.
		s_agentSndSrc[AGENTSND_REMOTE_2]        = sound_load("remote-2.voc", SOUND_PRIORITY_LOW0);
		s_agentSndSrc[AGENTSND_AXE_1]           = sound_load("axe-1.voc",    SOUND_PRIORITY_LOW0);
		s_agentSndSrc[AGENTSND_INTSTUN]         = sound_load("intstun.voc",  SOUND_PRIORITY_LOW0);
		s_agentSndSrc[AGENTSND_PROBFIRE_11]     = sound_load("probfir1.voc", SOUND_PRIORITY_LOW0);
		s_agentSndSrc[AGENTSND_PROBFIRE_12]     = sound_load("probfir1.voc", SOUND_PRIORITY_LOW0);
		s_agentSndSrc[AGENTSND_CREATURE2]       = sound_load("creatur2.voc", SOUND_PRIORITY_LOW0);
		s_agentSndSrc[AGENTSND_PROBFIRE_13]     = sound_load("probfir1.voc", SOUND_PRIORITY_LOW0);
		s_agentSndSrc[AGENTSND_STORM_HURT]      = sound_load("st-hrt-1.voc", SOUND_PRIORITY_MED5);
		s_agentSndSrc[AGENTSND_GAMOR_2]         = sound_load("gamor-2.voc",  SOUND_PRIORITY_MED5);
		s_agentSndSrc[AGENTSND_REEYEE_2]        = sound_load("reeyee-2.voc", SOUND_PRIORITY_MED5);
		s_agentSndSrc[AGENTSND_BOSSK_3]         = sound_load("bossk-3.voc",  SOUND_PRIORITY_MED5);
		s_agentSndSrc[AGENTSND_CREATURE_HURT]   = sound_load("creathrt.voc", SOUND_PRIORITY_MED5);
		s_agentSndSrc[AGENTSND_STORM_DIE]       = sound_load("st-die-1.voc", SOUND_PRIORITY_MED5);
		s_agentSndSrc[AGENTSND_REEYEE_3]        = sound_load("reeyee-3.voc", SOUND_PRIORITY_MED5);
		s_agentSndSrc[AGENTSND_BOSSK_DIE]       = sound_load("bosskdie.voc", SOUND_PRIORITY_MED5);
		s_agentSndSrc[AGENTSND_GAMOR_1]         = sound_load("gamor-1.voc",  SOUND_PRIORITY_MED5);
		s_agentSndSrc[AGENTSND_CREATURE_DIE]    = sound_load("creatdie.voc", SOUND_PRIORITY_MED5);
		s_agentSndSrc[AGENTSND_EEEK_3]          = sound_load("eeek-3.voc",   SOUND_PRIORITY_MED5);
		s_agentSndSrc[AGENTSND_SMALL_EXPLOSION] = sound_load("ex-small.voc", SOUND_PRIORITY_MED5);
		s_agentSndSrc[AGENTSND_PROBE_ALM]       = sound_load("probalm.voc",  SOUND_PRIORITY_MED5);
		s_agentSndSrc[AGENTSND_TINY_EXPLOSION]  = sound_load("ex-tiny1.voc", SOUND_PRIORITY_MED5);

		sound_setBaseVolume(s_agentSndSrc[AGENTSND_REMOTE_2], 40);

		// Pre-cache all sounds, to make sure sound order is consistent.
		bobaFett_precache();
		kellDragon_precache();
		mousebot_precache();
		phaseOne_precache();
		phaseTwo_precache();
		phaseThree_precache();
		turret_precache();
		welder_precache();
	}

	void actor_allocatePhysicsActorList()
	{
		s_physicsActors = list_allocate(sizeof(PhysicsActor*), 80);
	}

	void actor_addPhysicsActorToWorld(PhysicsActor* actor)
	{
		PhysicsActor** actorPtr = (PhysicsActor**)list_addItem(s_physicsActors);
		*actorPtr = actor;

		actor->vel = { 0, 0, 0 };
		actor->moveSndId = NULL_SOUND;
		actor->parent = actorPtr;
	}

	void actor_removePhysicsActorFromWorld(PhysicsActor* phyActor)
	{
		list_removeItem(s_physicsActors, phyActor->parent);
	}
		
	void actor_createTask()
	{
		s_istate.actorDispatch = allocator_create(sizeof(ActorDispatch));
		s_istate.actorTask = createSubTask("actor", actorLogicTaskFunc, actorLogicMsgFunc);
		s_istate.actorPhysicsTask = createSubTask("physics", actorPhysicsTaskFunc);
	}

	ActorDispatch* actor_createDispatch(SecObject* obj, LogicSetupFunc* setupFunc)
	{
		ActorDispatch* dispatch = (ActorDispatch*)allocator_newItem(s_istate.actorDispatch);
		if (!dispatch)
			return nullptr;
		memset(dispatch->modules, 0, sizeof(ActorModule*) * 6);

		dispatch->moveMod = nullptr;
		dispatch->animTable = nullptr;
		dispatch->delay = 72;			// Delay in ticks
		dispatch->alertSndSrc = NULL_SOUND;
		dispatch->alertSndID = NULL_SOUND;
		dispatch->fov = 9557;			// ~210 degrees
		dispatch->awareRange = FIXED(20);
		dispatch->vel = { 0 };
		dispatch->lastPlayerPos = { 0 };
		dispatch->freeTask = nullptr;
		dispatch->flags = ACTOR_NPC;	// this is later removed for barrels and scenery

		dispatch->deathScriptCall = -1;
		dispatch->alertScriptCall = -1;

		if (obj)
		{
			obj_addLogic(obj, (Logic*)dispatch, LOGIC_DISPATCH, s_istate.actorTask, actorLogicCleanupFunc);
		}
		if (setupFunc)
		{
			*setupFunc = actorLogicSetupFunc;
		}

		s_actorState.curLogic = (Logic*)dispatch;
		return dispatch;
	}
		
	JBool actorLogicSetupFunc(Logic* logic, KEYWORD key)
	{
		ActorDispatch* dispatch = (ActorDispatch*)logic;
		ActorModule* module = nullptr;

		if (key == KW_PLUGIN)
		{
			KEYWORD pluginType = getKeywordIndex(s_objSeqArg1);
			KEYWORD thinkerType = getKeywordIndex(s_objSeqArg2);

			switch (pluginType)
			{
				case KW_MOVER:
				{
					dispatch->moveMod = actor_createMovementModule(dispatch);
				} break;
				case KW_SHAKER:
				{
					module = (ActorModule*)actor_createAttackModule(dispatch);
				} break;
				case KW_THINKER:
				{
					if (thinkerType == KW_FOLLOW_Y)
					{
						module = (ActorModule*)actor_createFlyingModule((Logic*)dispatch);
					}
					else if (thinkerType == KW_RANDOM_YAW)
					{
						module = (ActorModule*)actor_createFlyingModule_Remote((Logic*)dispatch);
					}
					else if (thinkerType != KW_FOLLOW)
					{
						return JFALSE;
					}
				} break;
				default:
				{
					return JFALSE;
				}
			}

			actor_addModule(dispatch, module);
			return JTRUE;
		}
		return JFALSE;
	}

	void actor_initModule(ActorModule* module, Logic* logic)
	{
		module->func = nullptr;
		module->msgFunc = nullptr;
		module->attribFunc = nullptr;
		module->freeFunc = nullptr;
		module->nextTick = 0;
		module->obj = logic->obj;
	}

	fixed16_16 actor_initAttackModule(AttackModule* attackMod, Logic* logic)
	{
		actor_initModule(&attackMod->header, logic);

		attackMod->target.speedRotation = 0;
		attackMod->target.speed = 0;
		attackMod->target.speedVert = 0;
		attackMod->timing.delay = 218;
		attackMod->timing.searchDelay = 218;
		attackMod->timing.meleeDelay = 0;
		attackMod->timing.rangedDelay = 291;
		attackMod->timing.losDelay = 43695;
		attackMod->anim.frameRate = 5;
		attackMod->anim.frameCount = ONE_16;
		attackMod->anim.prevTick = 0;
		attackMod->anim.state = STATE_DELAY;
		attackMod->fireSpread = FIXED(30);
		attackMod->accuracyNextTick = 0;
		attackMod->fireOffset.x = 0;
		attackMod->fireOffset.z = 0;

		attackMod->target.flags &= ~TARGET_ALL;
		attackMod->anim.flags |= (AFLAG_PLAYONCE | AFLAG_READY);	// attack and damage module animations do not loop
		attackMod->timing.nextTick = s_curTick + 0x4446;	// ~120 seconds

		SecObject* obj = attackMod->header.obj;
		// world width and height was set from the sprite data.
		attackMod->fireOffset.y = -((TFE_Jedi::abs(obj->worldHeight) >> 1) + ONE_16);

		attackMod->projType = PROJ_RIFLE_BOLT;
		attackMod->attackSecSndSrc = 0;
		attackMod->attackPrimSndSrc = 0;
		attackMod->meleeRange = 0;
		attackMod->minDist = 0;
		attackMod->maxDist = FIXED(160);
		attackMod->meleeDmg = 0;
		attackMod->meleeRate = FIXED(230);
		attackMod->attackFlags = ATTFLAG_RANGED | ATTFLAG_LIT_RNG;

		// Why is this being returned? This function maybe should be a void? 
		return attackMod->fireOffset.y;
	}

	// Cause flying enemies fall to the ground when killed
	void actor_setDeathCollisionFlags()
	{
		ActorDispatch* logic = (ActorDispatch*)s_actorState.curLogic;
		MovementModule* moveMod = logic->moveMod;
		moveMod->collisionFlags |= ACTORCOL_GRAVITY;
		// Added to disable auto-aim when dying.
		logic->logic.obj->flags &= ~OBJ_FLAG_AIM;
	}

	// Returns JTRUE if the object is on the floor, or JFALSE is not on the floor or moving too fast.
	JBool actor_onFloor()
	{
		ActorDispatch* logic = (ActorDispatch*)s_actorState.curLogic;
		SecObject* obj = logic->logic.obj;
		RSector* sector = obj->sector;

		if (sector->secHeight > 0 && obj->posWS.y > sector->floorHeight)
		{
			return JTRUE;
		}
		fixed16_16 absVelX = TFE_Jedi::abs(logic->vel.x);
		fixed16_16 absVelZ = TFE_Jedi::abs(logic->vel.z);
		if (absVelX >= FIXED(5) && absVelZ >= FIXED(5))
		{
			return JFALSE;
		}

		fixed16_16 ceilHeight, floorHeight;
		sector_getObjFloorAndCeilHeight(sector, obj->posWS.y, &floorHeight, &ceilHeight);
		return (obj->posWS.y < floorHeight) ? JFALSE : JTRUE;
	}

	JBool actor_arrivedAtTarget(ActorTarget* target, SecObject* obj)
	{
		if ((target->flags & TARGET_MOVE_XZ) && (target->pos.x != obj->posWS.x || target->pos.z != obj->posWS.z))
		{
			return JFALSE;
		}
		if ((target->flags & TARGET_MOVE_Y) && (target->pos.y != obj->posWS.y))
		{
			return JFALSE;
		}
		if ((target->flags & TARGET_MOVE_ROT) && (target->yaw != obj->yaw || target->pitch != obj->pitch || target->roll != obj->roll))
		{
			return JFALSE;
		}
		return JTRUE;
	}

	angle14_32 actor_offsetTarget(fixed16_16* targetX, fixed16_16* targetZ, fixed16_16 targetOffset, fixed16_16 targetVariation, angle14_32 angle, angle14_32 angleVariation)
	{
		if (angleVariation)
		{
			angle += floor16(random(intToFixed16(angleVariation) * 2) - intToFixed16(angleVariation));
		}
		angle &= ANGLE_MASK;

		fixed16_16 dist;
		if (targetVariation)
		{
			dist = targetOffset + random(targetVariation * 2) - targetVariation;
		}
		else
		{
			dist = targetOffset;
		}

		fixed16_16 cosAngle, sinAngle;
		sinCosFixed(angle, &sinAngle, &cosAngle);
		*targetX += mul16(dist, sinAngle);
		*targetZ += mul16(dist, cosAngle);
		return angle;
	}

	// returns JTRUE on collision else JFALSE.
	JBool actor_handleSteps(MovementModule* moveMod, ActorTarget* target)
	{
		SecObject* obj = moveMod->header.obj;
		if (moveMod->physics.responseStep || moveMod->collisionWall)
		{
			if (!(moveMod->collisionFlags & ACTORCOL_NO_Y_MOVE))		// i.e. actor is capable of vertical movement
			{
				RWall* wall = moveMod->physics.wall ? moveMod->physics.wall : moveMod->collisionWall;
				if (!wall)
				{
					return moveMod->physics.collidedObj ? JTRUE : JFALSE;
				}

				RSector* nextSector = wall->nextSector;
				if (nextSector)
				{
					// Actor has collided with adjoined wall; test if it can fit (vertically) through the adjoin
					RSector* sector = wall->sector;
					fixed16_16 floorHeight = min(nextSector->floorHeight, sector->floorHeight);
					fixed16_16 ceilHeight  = max(nextSector->ceilingHeight, sector->ceilingHeight);
					fixed16_16 gap = floorHeight - ceilHeight;
					fixed16_16 objHeight = obj->worldHeight + ONE_16;
					// If the object can fit.
					if (gap > objHeight)
					{
						// Move vertically to the middle of the gap
						target->pos.y = floorHeight - (TFE_Jedi::abs(gap) >> 1) + (TFE_Jedi::abs(obj->worldHeight) >> 1);
						target->flags |= TARGET_MOVE_Y;
						return JFALSE;
					}
				}
			}
			return JTRUE;
		}
		return JFALSE;
	}

	JBool actorLogic_isVisibleFlagSet()
	{
		ActorDispatch* logic = (ActorDispatch*)s_actorState.curLogic;
		return (logic->flags & ACTOR_PLAYER_VISIBLE) ? JTRUE : JFALSE;
	}

	void actor_changeDirFromCollision(MovementModule* moveMod, ActorTarget* target, Tick* prevColTick)
	{
		SecObject* obj = moveMod->header.obj;
		Tick delta = Tick(s_curTick - (*prevColTick));
		angle14_32 newAngle;
		if (delta < 145)
		{
			newAngle = random_next() & ANGLE_MASK;
		}
		else
		{
			angle14_32 rAngle = moveMod->physics.responseAngle + (s_curTick & 0xff) - 128;
			angle14_32 angleDiff = getAngleDifference(obj->yaw, rAngle) & 8191;
			if (angleDiff > 4095) // 90 degrees
			{
				newAngle = moveMod->physics.responseAngle - 8191;  // 180 degrees
			}
			else
			{
				newAngle = moveMod->physics.responseAngle;
			}
			newAngle &= ANGLE_MASK;
		}

		fixed16_16 dirX, dirZ;
		sinCosFixed(newAngle, &dirX, &dirZ);
		target->pos.x = obj->posWS.x + (dirX << 7);
		target->pos.z = obj->posWS.z + (dirZ << 7);

		target->flags = (target->flags | TARGET_MOVE_XZ | TARGET_MOVE_ROT) & (~TARGET_MOVE_Y);
		target->yaw   = newAngle;
		target->pitch = 0;
		target->roll  = 0;

		*prevColTick = s_curTick;
	}

	void actor_jumpToTarget(PhysicsActor* physicsActor, SecObject* obj, vec3_fixed target, fixed16_16 speed, angle14_32 angleOffset)
	{
		fixed16_16 dist = distApprox(obj->posWS.x, obj->posWS.z, target.x, target.z);
		fixed16_16 distOverSpeed = div16(dist, speed);
		fixed16_16 impulse = mul16(s_gravityAccel, distOverSpeed >> 1);

		fixed16_16 dy = obj->posWS.y - target.y;
		angle14_32 pitch = vec2ToAngle(dy, dist);
		angle14_32 finalPitch = (angleOffset + pitch + arcCosFixed(div16(impulse, speed), pitch)) & ANGLE_MASK;

		// This simplifies to: (dy/dist + 1.0) * speed
		fixed16_16 dyOverDistBySpeed = div16(dy + dist, distOverSpeed);

		fixed16_16 sinYaw, cosYaw;
		fixed16_16 sinPitch, cosPitch;
		sinCosFixed(obj->yaw, &sinYaw, &cosYaw);
		sinCosFixed(finalPitch, &sinPitch, &cosPitch);

		physicsActor->vel.x = mul16(sinYaw, dyOverDistBySpeed);
		physicsActor->vel.z = mul16(cosYaw, dyOverDistBySpeed);
		physicsActor->vel.y = -mul16(sinPitch, dyOverDistBySpeed);

		physicsActor->vel.x = mul16(cosPitch, physicsActor->vel.x);
		physicsActor->vel.z = mul16(cosPitch, physicsActor->vel.z);
	}

	void actor_leadTarget(ProjectileLogic* proj)
	{
		SecObject* projObj = proj->logic.obj;

		fixed16_16 dist = distApprox(projObj->posWS.x, projObj->posWS.z, s_playerObject->posWS.x, s_playerObject->posWS.z);
		fixed16_16 travelTime = div16(dist, proj->speed);
		vec3_fixed playerVel;
		player_getVelocity(&playerVel);

		vec3_fixed target =
		{
			s_playerObject->posWS.x + mul16(playerVel.x, travelTime),
			s_playerObject->posWS.y + mul16(playerVel.y, travelTime) + ONE_16 - s_playerObject->worldHeight,
			s_playerObject->posWS.z + mul16(playerVel.z, travelTime)
		};
		proj_aimAtTarget(proj, target);
	}

	void actor_removeRandomCorpse()
	{
		if (s_levelState.sectorCount <= 0)
		{
			return;
		}
		s32 randomSect = floor16(random(intToFixed16(s_levelState.sectorCount - 1)));
		RSector* sector;
		for (u32 attempt = 0; attempt < s_levelState.sectorCount; attempt++, randomSect++)
		{
			if (randomSect >= (s32)s_levelState.sectorCount)
			{
				sector = &s_levelState.sectors[0];
				randomSect = 0;
			}
			else
			{
				sector = &s_levelState.sectors[randomSect];
			}

			SecObject** objList = sector->objectList;
			for (s32 i = 0, idx = 0; i < sector->objectCount && idx < sector->objectCapacity; idx++)
			{
				SecObject* obj = objList[idx];
				if (obj)
				{
					if ((obj->entityFlags & ETFLAG_CORPSE) && !(obj->entityFlags & ETFLAG_KEEP_CORPSE) && !actor_canSeeObject(obj, s_playerObject))
					{
						freeObject(obj);
						return;
					}
					i++;
				}
			}
		}
	}

	void actor_handleBossDeath(PhysicsActor* physicsActor)
	{
		SecObject* obj = physicsActor->moveMod.header.obj;
		if (obj->flags & OBJ_FLAG_BOSS)
		{
			if (obj->entityFlags & ETFLAG_GENERAL_MOHC)
			{
				if (s_levelState.mohcSector)
				{
					message_sendToSector(s_levelState.mohcSector, nullptr, 0, MSG_TRIGGER);
				}
			}
			else if (s_levelState.bossSector)
			{
				message_sendToSector(s_levelState.bossSector, nullptr, 0, MSG_TRIGGER);
			}
		}
	}

	void actor_updatePlayerVisiblity(JBool playerVis, fixed16_16 posX, fixed16_16 posZ)
	{
		ActorDispatch* logic = (ActorDispatch*)s_actorState.curLogic;

		// Update player visibility flag.
		logic->flags &= ~ACTOR_PLAYER_VISIBLE;
		logic->flags |= ((playerVis & 1) << 3);	// flag 8 = player visible.

		if (playerVis)
		{
			logic->lastPlayerPos.x = posX;
			logic->lastPlayerPos.z = posZ;
		}
	}

	ActorDispatch* actor_getCurrentLogic()
	{
		return (ActorDispatch*)s_actorState.curLogic;
	}

	// Default damage module function for "dispatch" actors
	Tick defaultDamageFunc(ActorModule* module, MovementModule* moveMod)
	{
		DamageModule* damageMod = (DamageModule*)module;
		AttackModule* attackMod = &damageMod->attackMod;
		SecObject* obj = attackMod->header.obj;
		RSector* sector = obj->sector;

		if (!(attackMod->anim.flags & AFLAG_READY))
		{
			if (obj->type == OBJ_TYPE_SPRITE)
			{
				actor_setCurAnimation(&attackMod->anim);
			}
			moveMod->updateTargetFunc(moveMod, &attackMod->target);
			return 0;
		}

		if (damageMod->hp <= 0)
		{
			if (!actor_onFloor())
			{
				if (obj->type == OBJ_TYPE_SPRITE)
				{
					actor_setCurAnimation(&attackMod->anim);
				}
				moveMod->updateTargetFunc(moveMod, &attackMod->target);
				return 0;
			}
			spawnHitEffect(damageMod->dieEffect, sector, obj->posWS, obj);

			s32 corpseId = -1;	// corpseId to be passed to a deathscriptcall

			// If the secHeight is <= 0, then it is not a water sector.
			if (sector->secHeight - 1 < 0)
			{
				if (damageMod->itemDropId != ITEM_NONE)
				{
					SecObject* item = item_create(damageMod->itemDropId);
					item->posWS = obj->posWS;
					sector_addObject(sector, item);

					CollisionInfo colInfo =
					{
						item,	// obj
						FIXED(2), 0, FIXED(2),	// offset
						ONE_16, COL_INFINITY, ONE_16, 0,	// stepUpHeight, stepDownHeight, height, u1c
						nullptr, 0, nullptr,
						item->worldWidth, 0,
						JFALSE,
						0, 0, 0, 0, 0
					};
					handleCollision(&colInfo);

					fixed16_16 ceilHeight, floorHeight;
					sector_getObjFloorAndCeilHeight(item->sector, item->posWS.y, &floorHeight, &ceilHeight);
					item->posWS.y = floorHeight;

					// Free the object if it winds in a water sector.
					if (item->sector->secHeight - 1 >= 0)
					{
						freeObject(item);
					}
				}
				s32 animIndex = actor_getAnimationIndex(ANIM_DEAD);
				if (animIndex != -1)
				{
					RSector* sector = obj->sector;
					SecObject* corpse = allocateObject();
					sprite_setData(corpse, obj->wax);
					corpse->frame = 0;
					corpse->anim = animIndex;
					corpse->posWS.x = obj->posWS.x;
					// The original code here was:
					// corpse->posWS.y = (sector->colSecHeight < obj->posWS.y) ? sector->colSecHeight : obj->posWS.y;
					// This causes corpses to always end up on top of bridges in vanilla DF, even if the actor dies under the bridge
					corpse->posWS.y = obj->posWS.y;
					corpse->posWS.z = obj->posWS.z;
					corpse->worldHeight = 0;
					corpse->worldWidth = 0;
					corpse->entityFlags |= ETFLAG_CORPSE;
					sector_addObject(obj->sector, corpse);

					corpseId = obj_addToRefList(corpse, ObjRefType_Corpse);	// scripting
				}
			}

			// TFE - call deathscript, add corpse's ObjectId as the final arg
			s32 scriptCallIndex = ((ActorDispatch*)s_actorState.curLogic)->deathScriptCall;
			if (scriptCallIndex >= 0)
			{
				LogicScriptCall* scriptCall = logic_getScriptCall(scriptCallIndex);
				if (scriptCall && scriptCall->funcPtr)
				{
					scriptCall->args[scriptCall->argCount].type = TFE_ForceScript::ARG_S32;
					scriptCall->args[scriptCall->argCount].iValue = corpseId;
					scriptCall->argCount++;
					TFE_ForceScript::execFunc(scriptCall->funcPtr, scriptCall->argCount, scriptCall->args);
				}
			}

			actor_kill();
			return 0;
		}
		return 0xffffffff;
	}

	// Default damage module message function for "dispatch" actors
	// Delivers a message to the damage module
	Tick defaultDamageMsgFunc(s32 msg, ActorModule* module, MovementModule* moveMod)
	{
		DamageModule* damageMod = (DamageModule*)module;
		AttackModule* attackMod = &damageMod->attackMod;
		SecObject* obj = attackMod->header.obj;
		RSector* sector = obj->sector;

		if (msg == MSG_DAMAGE)
		{
			if (damageMod->hp > 0)
			{
				ProjectileLogic* proj = (ProjectileLogic*)s_msgEntity;
				fixed16_16 dmg = proj->dmg;
				// Reduce damage by half if an enemy is shooting another enemy.
				if (proj->prevObj)
				{
					u32 moduleProj = proj->prevObj->entityFlags & ETFLAG_AI_ACTOR;
					u32 moduleCur = obj->entityFlags & ETFLAG_AI_ACTOR;
					if (moduleProj == moduleCur)
					{
						dmg = proj->dmg >> 1;
					}
				}
				damageMod->hp -= dmg;
				if (damageMod->stopOnHit || damageMod->hp <= 0)
				{
					attackMod->target.flags |= TARGET_FREEZE;
				}

				LogicAnimation* anim = &attackMod->anim;
				vec3_fixed pushVel;
				computeDamagePushVelocity(proj, &pushVel);
				if (damageMod->hp <= 0)
				{
					ActorDispatch* logic = (ActorDispatch*)s_actorState.curLogic;
					actor_addVelocity(pushVel.x*4, pushVel.y*2, pushVel.z*4);
					actor_setDeathCollisionFlags();
					sound_stop(logic->alertSndID);
					sound_playCued(damageMod->dieSndSrc, obj->posWS);
					attackMod->anim.flags |= AFLAG_BIT3;
					if (proj->type == PROJ_PUNCH && obj->type == OBJ_TYPE_SPRITE)
					{
						actor_setupAnimation(ANIM_DIE1, anim);
					}
					else if (obj->type == OBJ_TYPE_SPRITE)
					{
						actor_setupAnimation(ANIM_DIE2, anim);
					}
				}
				else
				{
					// Keep the y velocity from accumulating forever.
					ActorDispatch* logic = (ActorDispatch*)s_actorState.curLogic;
					if (logic->vel.y < 0)
					{
						pushVel.y = 0;
					}

					actor_addVelocity(pushVel.x*2, pushVel.y, pushVel.z*2);
					sound_stop(damageMod->hurtSndID);
					damageMod->hurtSndID = sound_playCued(damageMod->hurtSndSrc, obj->posWS);
					if (obj->type == OBJ_TYPE_SPRITE)
					{
						actor_setupAnimation(ANIM_PAIN, anim);
					}
				}
			}

			if (obj->type == OBJ_TYPE_SPRITE)
			{
				actor_setCurAnimation(&attackMod->anim);
			}
			moveMod->updateTargetFunc(moveMod, &attackMod->target);
			return 0;
		}
		else if (msg == MSG_EXPLOSION)
		{
			if (damageMod->hp > 0)
			{
				fixed16_16 dmg   = s_msgArg1;
				fixed16_16 force = s_msgArg2;
				damageMod->hp -= dmg;
				if (damageMod->stopOnHit || damageMod->hp <= 0)
				{
					attackMod->target.flags |= TARGET_FREEZE;
				}

				vec3_fixed dir;
				vec3_fixed pos = { obj->posWS.x, obj->posWS.y - obj->worldHeight, obj->posWS.z };
				computeExplosionPushDir(&pos, &dir);

				vec3_fixed vel = { mul16(force, dir.x), mul16(force, dir.y), mul16(force, dir.z) };
				LogicAnimation* anim = &attackMod->anim;
				ActorDispatch* logic = (ActorDispatch*)s_actorState.curLogic;
				if (damageMod->hp <= 0)
				{
					actor_addVelocity(vel.x, vel.y, vel.z);
					actor_setDeathCollisionFlags();
					sound_stop(logic->alertSndID);
					sound_playCued(damageMod->dieSndSrc, obj->posWS);
					attackMod->target.flags |= 8;			// possibly an error? FREEZE (bit 3) has already been added to target flags above
					if (obj->type == OBJ_TYPE_SPRITE)
					{
						actor_setupAnimation(ANIM_DIE2, anim);
					}
				}
				else
				{
					actor_addVelocity(vel.x>>1, vel.y>>1, vel.z>>1);
					sound_stop(damageMod->hurtSndID);
					damageMod->hurtSndID = sound_playCued(damageMod->hurtSndSrc, obj->posWS);
					if (obj->type == OBJ_TYPE_SPRITE)
					{
						actor_setupAnimation(ANIM_PAIN, anim);
					}
				}
			}
			if (obj->type == OBJ_TYPE_SPRITE)
			{
				actor_setCurAnimation(&attackMod->anim);
			}
			moveMod->updateTargetFunc(moveMod, &attackMod->target);
			return 0;
		}
		else if (msg == MSG_TERMINAL_VEL || msg == MSG_CRUSH)
		{
			if (damageMod->hp > 0)
			{
				damageMod->hp = 0;
				attackMod->target.flags |= TARGET_FREEZE;

				LogicAnimation* anim = &attackMod->anim;
				ActorDispatch* logic = (ActorDispatch*)s_actorState.curLogic;

				actor_setDeathCollisionFlags();
				sound_stop(logic->alertSndID);
				sound_playCued(damageMod->dieSndSrc, obj->posWS);
				attackMod->anim.flags |= AFLAG_BIT3;
				if (obj->type == OBJ_TYPE_SPRITE)
				{
					actor_setupAnimation(ANIM_DIE2, anim);
				}
			}
		}

		return 0;
	}

	DamageModule* actor_createDamageModule(ActorDispatch* dispatch)
	{
		DamageModule* damageMod = (DamageModule*)level_alloc(sizeof(DamageModule));
		memset(damageMod, 0, sizeof(DamageModule));
		
		AttackModule* attackMod = &damageMod->attackMod;
		actor_initAttackModule(attackMod, (Logic*)dispatch);
		attackMod->header.func    = defaultDamageFunc;
		attackMod->header.msgFunc = defaultDamageMsgFunc;
		attackMod->header.nextTick = 0xffffffff;
		attackMod->header.type = ACTMOD_DAMAGE;

		// Default values.
		damageMod->hp = FIXED(4);
		damageMod->itemDropId = ITEM_NONE;
		damageMod->hurtSndSrc = NULL_SOUND;
		damageMod->dieSndSrc  = NULL_SOUND;
		damageMod->hurtSndID  = NULL_SOUND;
		damageMod->stopOnHit  = JTRUE;
		damageMod->dieEffect  = HEFFECT_NONE;

		return damageMod;
	}
		
	// Default attack module function for "dispatch" actors
	// The returned value is set to the module's nextTick
	Tick defaultAttackFunc(ActorModule* module, MovementModule* moveMod)
	{
		// Note: the module passed into this function is an AttackModule*, not a DamageModule*, and should be cast directly to an AttackModule*
		// however, the code casts it to a DamageModule* first (why?)
		DamageModule* damageMod = (DamageModule*)module;
		AttackModule* attackMod = &damageMod->attackMod;
		ActorDispatch* logic = (ActorDispatch*)s_actorState.curLogic;
		SecObject* obj = attackMod->header.obj;
		LogicAnimation* anim = &attackMod->anim;
		s32 state = attackMod->anim.state;

		switch (state)
		{
			case STATE_DELAY:
			{
				if (attackMod->anim.flags & AFLAG_READY)
				{
					// Clear attack based lighting.
					obj->flags &= ~OBJ_FLAG_FULLBRIGHT;
					attackMod->anim.state = STATE_DECIDE;

					// Update the shooting accuracy.
					if (attackMod->accuracyNextTick < s_curTick)
					{
						if (attackMod->fireSpread)
						{
							attackMod->fireSpread -= mul16(FIXED(2), s_deltaTime);
							if (attackMod->fireSpread < FIXED(9))
							{
								attackMod->fireSpread = FIXED(9);
							}
							attackMod->accuracyNextTick = s_curTick + 145;
						}
						else
						{
							attackMod->accuracyNextTick = 0xffffffff;
						}
					}
					attackMod->target.flags &= ~TARGET_ALL_MOVE;
					// Next AI update.
					return s_curTick + random(attackMod->timing.delay);
				}
			} break;
			case STATE_DECIDE:
			{
				gameMusic_sustainFight();
				if (s_playerDying)
				{
					if (s_curTick >= s_reviveTick)
					{
						attackMod->anim.flags |= AFLAG_READY;
						attackMod->anim.state = STATE_DELAY;
						return attackMod->timing.delay;
					}
				}

				// Check for player visibility
				if (!actor_canSeeObjFromDist(obj, s_playerObject))
				{
					actor_updatePlayerVisiblity(JFALSE, 0, 0);
					attackMod->anim.flags |= AFLAG_READY;
					attackMod->anim.state = STATE_DELAY;
					if (attackMod->timing.nextTick < s_curTick)
					{
						// Lost sight of player for sufficient length of time (losDelay) - return to idle state
						attackMod->timing.delay = attackMod->timing.searchDelay;
						actor_setupInitAnimation();
					}
					return attackMod->timing.delay;
				}
				else  // Player is visible
				{
					actor_updatePlayerVisiblity(JTRUE, s_eyePos.x, s_eyePos.z);
					attackMod->timing.nextTick = s_curTick + attackMod->timing.losDelay;
					fixed16_16 dist = distApprox(s_playerObject->posWS.x, s_playerObject->posWS.z, obj->posWS.x, obj->posWS.z);
					fixed16_16 yDiff = TFE_Jedi::abs(obj->posWS.y - obj->worldHeight - s_eyePos.y);
					angle14_32 vertAngle = vec2ToAngle(yDiff, dist);

					fixed16_16 baseYDiff = TFE_Jedi::abs(s_playerObject->posWS.y - obj->posWS.y);
					dist += baseYDiff;

					if (vertAngle < 2275 && dist <= attackMod->maxDist)	// ~50 degrees
					{
						if (attackMod->attackFlags & ATTFLAG_MELEE)
						{
							if (dist <= attackMod->meleeRange)
							{
								// Within melee range => Perform melee (primary attack)
								attackMod->anim.state = STATE_ATTACK1;
								attackMod->timing.delay = attackMod->timing.meleeDelay;
							}
							else if (!(attackMod->attackFlags & ATTFLAG_RANGED) || dist < attackMod->minDist)
							{
								// Outside melee range && cannot do ranged attack => quit out
								attackMod->anim.flags |= AFLAG_READY;
								attackMod->anim.state = STATE_DELAY;
								return attackMod->timing.delay;
							}
							else
							{
								// Do ranged attack (secondary)
								attackMod->anim.state = STATE_ATTACK2;
								attackMod->timing.delay = attackMod->timing.rangedDelay;
							}
						}
						else  // Actor does not have melee attack
						{
							if (dist < attackMod->minDist)
							{
								// Too close for ranged attack => quit out
								attackMod->anim.flags |= AFLAG_READY;
								attackMod->anim.state = STATE_DELAY;
								return attackMod->timing.delay;
							}
							
							// Do ranged attack (primary)
							attackMod->anim.state = STATE_ATTACK1;
							attackMod->timing.delay = attackMod->timing.rangedDelay;
						}

						if (obj->type == OBJ_TYPE_SPRITE)
						{
							if (attackMod->anim.state == STATE_ATTACK1)
							{
								// Use primary attack animation
								actor_setupAnimation(ANIM_ATTACK1, &attackMod->anim);
							}
							else
							{
								// Use secondary attack animation
								actor_setupAnimation(ANIM_ATTACK2, &attackMod->anim);
							}
						}

						attackMod->target.pos.x = obj->posWS.x;
						attackMod->target.pos.z = obj->posWS.z;
						attackMod->target.yaw   = vec2ToAngle(s_eyePos.x - obj->posWS.x, s_eyePos.z - obj->posWS.z);
						attackMod->target.pitch = obj->pitch;
						attackMod->target.roll  = obj->roll;
						attackMod->target.flags |= (TARGET_MOVE_XZ | TARGET_MOVE_ROT);
					}
					else   // Too far away or too high or too low to attack
					{
						attackMod->anim.flags |= AFLAG_READY;
						attackMod->anim.state = STATE_DELAY;
						return attackMod->timing.delay;
					}
				}
			} break;
			case STATE_ATTACK1:
			{
				if (!(attackMod->anim.flags & AFLAG_READY))
				{
					break;
				}

				// Melee Attack
				if (attackMod->attackFlags & ATTFLAG_MELEE)
				{
					attackMod->anim.state = STATE_ANIMATE1;
					fixed16_16 dy = TFE_Jedi::abs(obj->posWS.y - s_playerObject->posWS.y);
					fixed16_16 dist = dy + distApprox(s_playerObject->posWS.x, s_playerObject->posWS.z, obj->posWS.x, obj->posWS.z);
					if (dist < attackMod->meleeRange)
					{
						sound_playCued(attackMod->attackSecSndSrc, obj->posWS);
						player_applyDamage(attackMod->meleeDmg, 0, JTRUE);
						if (attackMod->attackFlags & ATTFLAG_LIT_MELEE)
						{
							obj->flags |= OBJ_FLAG_FULLBRIGHT;
						}
					}
					break;
				}

				// Ranged Attack!
				if (attackMod->attackFlags & ATTFLAG_LIT_RNG)
				{
					obj->flags |= OBJ_FLAG_FULLBRIGHT;
				}

				attackMod->anim.state = STATE_ANIMATE1;
				vec3_fixed fireOffset = {};

				// Calculate the X,Z fire offsets based on where the enemy is facing. It doesn't matter for Y. 
				transformFireOffsets(obj->yaw, &attackMod->fireOffset, &fireOffset);

				ProjectileLogic* proj = (ProjectileLogic*)createProjectile(attackMod->projType, obj->sector, fireOffset.x + obj->posWS.x, fireOffset.y + obj->posWS.y, fireOffset.z + obj->posWS.z, obj);
				sound_playCued(attackMod->attackPrimSndSrc, obj->posWS);
				
				proj->prevColObj = obj;
				proj->prevObj = obj;
				proj->excludeObj = obj;

				SecObject* projObj = proj->logic.obj;
				projObj->yaw = obj->yaw;
				
				// Vanilla DF did not handle arcing projectiles with STATE_ATTACK1; this has been added
				if (attackMod->projType == PROJ_THERMAL_DET || attackMod->projType == PROJ_MORTAR)
				{
					// TDs are lobbed at an angle that depends on distance from target
					proj->bounceCnt = 0;
					proj->duration = 0xffffffff;
					vec3_fixed target = { s_playerObject->posWS.x, s_eyePos.y + ONE_16, s_playerObject->posWS.z };
					proj_aimArcing(proj, target, proj->speed);

					// This code was never hit in original DF because x and z fire offsets were always 0
					// It causes strange collision effects to happen, so commenting out.
					//if (fireOffset.x | fireOffset.z)
					//{
					//	proj->delta.x = fireOffset.x;
					//	proj->delta.z = fireOffset.z;
					//	proj_handleMovement(proj);
					//}
				}
				else
				{
					// Handle x and z fire offset.
					// Commenting out - see note above
					//if (fireOffset.x | fireOffset.z)
					//{
					//	proj->delta.x = fireOffset.x;
					//	proj->delta.z = fireOffset.z;
					//	proj_handleMovement(proj);
					//}

					// Aim at the target.
					vec3_fixed target = { s_eyePos.x, s_eyePos.y + ONE_16, s_eyePos.z };
					proj_aimAtTarget(proj, target);
					if (attackMod->fireSpread)
					{
						proj->vel.x += random(attackMod->fireSpread * 2) - attackMod->fireSpread;
						proj->vel.y += random(attackMod->fireSpread * 2) - attackMod->fireSpread;
						proj->vel.z += random(attackMod->fireSpread * 2) - attackMod->fireSpread;
					}
				}
			} break;
			case STATE_ANIMATE1:
			{
				if (obj->type == OBJ_TYPE_SPRITE)
				{
					actor_setupAnimation(ANIM_ATTACK1_END, anim);
				}
				attackMod->anim.state = STATE_DELAY;
			} break;
			case STATE_ATTACK2:
			{
				if (!(attackMod->anim.flags & AFLAG_READY))
				{
					break;
				}
				if (attackMod->attackFlags & ATTFLAG_LIT_RNG)
				{
					obj->flags |= OBJ_FLAG_FULLBRIGHT;
				}

				attackMod->anim.state = STATE_ANIMATE2;

				vec3_fixed fireOffset = {};
				
				// Calculate the fire offsets based on where the enemy is facing. It doesn't matter for Y. 
				transformFireOffsets(obj->yaw, &attackMod->fireOffset, &fireOffset);

				ProjectileLogic* proj = (ProjectileLogic*)createProjectile(attackMod->projType, obj->sector, fireOffset.x + obj->posWS.x, fireOffset.y + obj->posWS.y, fireOffset.z + obj->posWS.z, obj);
				sound_playCued(attackMod->attackPrimSndSrc, obj->posWS);
				proj->prevColObj = obj;
				proj->excludeObj = obj;

				SecObject* projObj = proj->logic.obj;
				projObj->yaw = obj->yaw;
				if (attackMod->projType == PROJ_THERMAL_DET || attackMod->projType == PROJ_MORTAR)
				{
					proj->bounceCnt = 0;
					proj->duration = 0xffffffff;
					vec3_fixed target = { s_playerObject->posWS.x, s_eyePos.y + ONE_16, s_playerObject->posWS.z };
					proj_aimArcing(proj, target, proj->speed);

					// Commenting out - see note above
					//if (fireOffset.x | fireOffset.z)
					//{
					//	proj->delta.x = fireOffset.x;
					//	proj->delta.z = fireOffset.z;
					//	proj_handleMovement(proj);
					//}
				}
				else
				{
					// Commenting out - see note above
					//if (fireOffset.x | fireOffset.z)
					//{
					//	proj->delta.x = fireOffset.x;
					//	proj->delta.z = fireOffset.z;
					//	proj_handleMovement(proj);
					//}
					vec3_fixed target = { s_eyePos.x, s_eyePos.y + ONE_16, s_eyePos.z };
					proj_aimAtTarget(proj, target);
					if (attackMod->fireSpread)
					{
						proj->vel.x += random(attackMod->fireSpread*2) - attackMod->fireSpread;
						proj->vel.y += random(attackMod->fireSpread*2) - attackMod->fireSpread;
						proj->vel.z += random(attackMod->fireSpread*2) - attackMod->fireSpread;
					}
				}
			} break;
			case STATE_ANIMATE2:
			{
				if (obj->type == OBJ_TYPE_SPRITE)
				{
					actor_setupAnimation(ANIM_ATTACK2_END, anim);
				}
				attackMod->anim.state = STATE_DELAY;
			} break;
		}

		if (obj->type == OBJ_TYPE_SPRITE)
		{
			actor_setCurAnimation(&attackMod->anim);
		}
		moveMod->updateTargetFunc(moveMod, &attackMod->target);
		return attackMod->timing.delay;
	}

	// Default attack module message function for "dispatch" actors
	// Delivers a message to the attack module
	Tick defaultAttackMsgFunc(s32 msg, ActorModule* module, MovementModule* moveMod)
	{
		// Note: the module passed into this function is an AttackModule*, not a DamageModule*, and should be cast directly to an AttackModule*
		// however, the code casts it to a DamageModule* first (why?)
		DamageModule* damageMod = (DamageModule*)module;
		AttackModule* attackMod = &damageMod->attackMod;
		ActorDispatch* logic = (ActorDispatch*)s_actorState.curLogic;
		if (msg == MSG_WAKEUP)
		{
			attackMod->timing.nextTick = s_curTick + attackMod->timing.losDelay;
		}
		if (logic->flags & ACTOR_IDLE)
		{
			return attackMod->timing.delay;
		}
		return 0xffffffff;
	}

	AttackModule* actor_createAttackModule(ActorDispatch* dispatch)
	{
		AttackModule* attackMod = (AttackModule*)level_alloc(sizeof(AttackModule));
		memset(attackMod, 0, sizeof(AttackModule));
		
		actor_initAttackModule(attackMod, (Logic*)dispatch);
		attackMod->header.type = ACTMOD_ATTACK;
		attackMod->header.func = defaultAttackFunc;
		attackMod->header.msgFunc = defaultAttackMsgFunc;
		return attackMod;
	}

	// Default thinker module function for "dispatch" actors
	Tick defaultThinkerFunc(ActorModule* module, MovementModule* moveMod)
	{
		ThinkerModule* thinkerMod = (ThinkerModule*)module;
		SecObject* obj = thinkerMod->header.obj;

		if (thinkerMod->anim.state == STATE_MOVE)
		{
			ActorTarget* target = &thinkerMod->target;
			JBool arrivedAtTarget = actor_arrivedAtTarget(target, obj);
			if (thinkerMod->nextTick < s_curTick || arrivedAtTarget)
			{
				if (arrivedAtTarget)
				{
					thinkerMod->playerLastSeen = 0xffffffff;
				}
				thinkerMod->anim.state = STATE_TURN;
			}
			else
			{
				if (actorLogic_isVisibleFlagSet())
				{
					if (thinkerMod->playerLastSeen != 0xffffffff)
					{
						thinkerMod->nextTick = 0;
						thinkerMod->maxWalkTime = thinkerMod->startDelay;
						thinkerMod->playerLastSeen = 0xffffffff;
					}
				}
				else
				{
					thinkerMod->playerLastSeen = s_curTick + 0x1111;
				}

				ActorTarget* target = &thinkerMod->target;
				if (actor_handleSteps(moveMod, target))
				{
					actor_changeDirFromCollision(moveMod, target, &thinkerMod->prevColTick);
					if (!actorLogic_isVisibleFlagSet())
					{
						thinkerMod->maxWalkTime += 218;
						if (thinkerMod->maxWalkTime > 1456)
						{
							thinkerMod->maxWalkTime = 291;
						}
						thinkerMod->nextTick = s_curTick + thinkerMod->maxWalkTime;
					}
				}
			}
		}
		else if (thinkerMod->anim.state == STATE_TURN)
		{
			ActorDispatch* logic = actor_getCurrentLogic();
			fixed16_16 targetX, targetZ;
			if (thinkerMod->playerLastSeen < s_curTick)
			{
				targetX = logic->lastPlayerPos.x;
				targetZ = logic->lastPlayerPos.z;
			}
			else
			{
				targetX = s_eyePos.x;
				targetZ = s_eyePos.z;
			}

			fixed16_16 targetOffset;
			if (!actorLogic_isVisibleFlagSet())
			{
				// Offset the target by |dx| / 4
				// This is obviously a typo and bug in the DOS code and should be min(|dx|, |dz|)
				// but the original code is min(|dx|, |dx|) => |dx|
				fixed16_16 dx = TFE_Jedi::abs(s_playerObject->posWS.x - obj->posWS.x);
				targetOffset = dx >> 2;
			}
			else
			{
				// Offset the target by the targetOffset.
				targetOffset = thinkerMod->targetOffset;
			}

			fixed16_16 dx = obj->posWS.x - targetX;
			fixed16_16 dz = obj->posWS.z - targetZ;
			angle14_32 angle = vec2ToAngle(dx, dz);

			thinkerMod->target.pos.x = targetX;
			thinkerMod->target.pos.z = targetZ;
			thinkerMod->target.flags = (thinkerMod->target.flags | TARGET_MOVE_XZ) & (~TARGET_MOVE_Y);
			actor_offsetTarget(&thinkerMod->target.pos.x, &thinkerMod->target.pos.z, targetOffset, thinkerMod->targetVariation, angle, thinkerMod->approachVariation);

			dx = thinkerMod->target.pos.x - obj->posWS.x;
			dz = thinkerMod->target.pos.z - obj->posWS.z;
			thinkerMod->target.pitch = 0;
			thinkerMod->target.roll = 0;
			thinkerMod->target.yaw = vec2ToAngle(dx, dz);
			thinkerMod->target.flags |= TARGET_MOVE_ROT;
						
			if (!(logic->flags & ACTOR_MOVING))
			{
				if (obj->type == OBJ_TYPE_SPRITE)
				{
					actor_setupAnimation(ANIM_MOVE, &thinkerMod->anim);
				}
				logic->flags |= ACTOR_MOVING;
			}
			thinkerMod->anim.state = STATE_MOVE;
			thinkerMod->nextTick = s_curTick + thinkerMod->maxWalkTime;

			if (obj->entityFlags & ETFLAG_REMOTE)
			{
				if (!(logic->flags & ACTOR_IDLE) && (logic->flags & ACTOR_MOVING))
				{
					sound_playCued(s_agentSndSrc[AGENTSND_REMOTE_2], obj->posWS);
				}
			}
		}

		if (obj->type == OBJ_TYPE_SPRITE)
		{
			actor_setCurAnimation(&thinkerMod->anim);
		}
		moveMod->updateTargetFunc(moveMod, &thinkerMod->target);
		return 0;
	}

	void actor_thinkerModuleInit(ThinkerModule* thinkerMod)
	{
		memset(thinkerMod, 0, sizeof(ThinkerModule));

		thinkerMod->target.speedRotation = 0;
		thinkerMod->target.speed = FIXED(4);
		thinkerMod->target.speedVert = FIXED(10);
		thinkerMod->delay = 72;
		thinkerMod->nextTick = 0;
		thinkerMod->playerLastSeen = 0xffffffff;
		thinkerMod->anim.state = STATE_TURN;
		thinkerMod->maxWalkTime = 728;	// ~5 seconds between decision points.
		thinkerMod->anim.frameRate = 5;
		thinkerMod->anim.frameCount = ONE_16;

		thinkerMod->anim.prevTick = 0;
		thinkerMod->prevColTick = 0;
		thinkerMod->target.flags = 0;
		thinkerMod->anim.flags = 0;
	}

	ThinkerModule* actor_createThinkerModule(ActorDispatch* dispatch)
	{
		ThinkerModule* thinkerMod = (ThinkerModule*)level_alloc(sizeof(ThinkerModule));
		actor_thinkerModuleInit(thinkerMod);
		actor_initModule((ActorModule*)thinkerMod, (Logic*)dispatch);
		
		thinkerMod->header.func = defaultThinkerFunc;
		thinkerMod->header.type = ACTMOD_THINKER;
		thinkerMod->targetOffset = FIXED(3);
		thinkerMod->targetVariation = 0;
		thinkerMod->approachVariation = 4096;	// 90 degrees.

		return thinkerMod;
	}

	void actor_setupInitAnimation()
	{
		ActorDispatch* logic = (ActorDispatch*)s_actorState.curLogic;
		logic->flags = (logic->flags | ACTOR_IDLE) & ~ACTOR_MOVING;		// remove flag bit 1 (ACTOR_MOVING)
		logic->nextTick = s_curTick + logic->delay;

		SecObject* obj = logic->logic.obj;
		obj->anim = actor_getAnimationIndex(ANIM_IDLE);
		obj->frame = 0;

		// TFE: Find the thinker module and set its animation to the idle animation
		if (TFE_Settings::jsonAiLogics())
		{
			for (s32 i = 0; i < ACTOR_MAX_MODULES; i++)
			{
				ActorModule* module = logic->modules[ACTOR_MAX_MODULES - 1 - i];
				if (module && module->type == ACTMOD_THINKER)
				{
					ThinkerModule* thinkerMod = (ThinkerModule*)module;
					actor_setupAnimation(ANIM_IDLE, &thinkerMod->anim);
					break;
				}
			}
		}
	}

	void actor_addModule(ActorDispatch* dispatch, ActorModule* module)
	{
		if (!module) { return; }
		for (s32 i = 0; i < ACTOR_MAX_MODULES; i++)
		{
			if (!dispatch->modules[i])
			{
				dispatch->modules[i] = module;
				return;
			}
		}
	}

	JBool actor_handleMovementAndCollision(MovementModule* moveMod)
	{
		SecObject* obj = moveMod->header.obj;
		vec3_fixed desiredMove = { 0, 0, 0 };
		vec3_fixed move = { 0, 0, 0 };

		// First, handle active movement - movement that the actor "intends" to do
		moveMod->collisionWall = nullptr;
		if (!(moveMod->target.flags & TARGET_FREEZE))
		{
			if (moveMod->target.flags & TARGET_MOVE_XZ)
			{
				desiredMove.x = moveMod->target.pos.x - obj->posWS.x;
				desiredMove.z = moveMod->target.pos.z - obj->posWS.z;
			}
			if (!(moveMod->collisionFlags & ACTORCOL_NO_Y_MOVE))		// i.e. actor is capable of vertical movement
			{
				if (moveMod->target.flags & TARGET_MOVE_Y)
				{
					desiredMove.y = moveMod->target.pos.y - obj->posWS.y;
				}
			}
			move.z = move.y = move.x = 0;
			if (desiredMove.x | desiredMove.z)
			{
				fixed16_16 dirZ, dirX;
				fixed16_16 frameMove = mul16(moveMod->target.speed, s_deltaTime);
				computeDirAndLength(desiredMove.x, desiredMove.z, &dirX, &dirZ);

				if (desiredMove.x && dirX)
				{
					fixed16_16 deltaX = mul16(frameMove, dirX);
					fixed16_16 absDx = (desiredMove.x < 0) ? -desiredMove.x : desiredMove.x;
					move.x = clamp(deltaX, -absDx, absDx);
				}
				if (desiredMove.z && dirZ)
				{
					fixed16_16 deltaZ = mul16(frameMove, dirZ);
					fixed16_16 absDz = (desiredMove.z < 0) ? -desiredMove.z : desiredMove.z;
					move.z = clamp(deltaZ, -absDz, absDz);
				}
			}
			if (desiredMove.y)
			{
				fixed16_16 deltaY = mul16(moveMod->target.speedVert, s_deltaTime);
				if (desiredMove.y < 0) { deltaY = -deltaY; }

				fixed16_16 absDy = (desiredMove.y < 0) ? -desiredMove.y : desiredMove.y;
				move.y = clamp(deltaY, -absDy, absDy);
			}
		}

		if (moveMod->delta.y)
		{
			move.y += moveMod->delta.y;
		}
		CollisionInfo* physics = &moveMod->physics;
		fixed16_16 dirX, dirZ;
		if (move.x | move.y | move.z)
		{
			physics->offsetX = move.x;
			physics->offsetY = move.y;
			physics->offsetZ = move.z;
			handleCollision(physics);

			if (physics->responseStep)
			{
				RWall* wall = physics->wall;
				if (wall)
				{
					RSector* nextSector = wall->nextSector;
					RSector* triggerSector = (nextSector) ? nextSector : wall->sector;
					if (obj->entityFlags & ETFLAG_SMART_OBJ)
					{
						message_sendToSector(triggerSector, obj, 0, MSG_TRIGGER);   // smart object will try to open a door or activate an elevator that it collides with
					}
				}
				// Handles a single collision response + resolution step.
				if (moveMod->collisionFlags & ACTORCOL_SLIDE_RESPONSE)
				{
					moveMod->collisionWall = wall;
					dirX = physics->responseDir.x;
					dirZ = physics->responseDir.z;
					handleCollisionResponseSimple(dirX, dirZ, &physics->offsetX, &physics->offsetZ);
					handleCollision(physics);
				}
			}
		}

		// Now handle passive movement - movement caused by being pushed, eg. by explosions or projectile impacts
		// Apply the per-frame delta computed from the actor's velocity.
		if (moveMod->delta.x | moveMod->delta.z)
		{
			physics->flags |= COLINFO_INFINITE_DROP;  // actor can be pushed off a cliff
			physics->offsetX = moveMod->delta.x;
			physics->offsetY = 0;
			physics->offsetZ = moveMod->delta.z;
			handleCollision(physics);

			// Handles a single collision response + resolution step from velocity delta.
			if ((moveMod->collisionFlags & ACTORCOL_SLIDE_RESPONSE) && physics->responseStep)
			{
				moveMod->collisionWall = physics->wall;
				dirX = physics->responseDir.x;
				dirZ = physics->responseDir.z;
				handleCollisionResponseSimple(dirX, dirZ, &physics->offsetX, &physics->offsetZ);
				handleCollision(physics);
			}
		}

		return JTRUE;
	}

	angle14_32 computeAngleChange(angle14_32 desiredChange, angle14_32 maxChange)
	{
		if (!desiredChange) { return 0; }
		angle14_32 change = desiredChange;
		if (change > maxChange)
		{
			change = maxChange;
		}
		else
		{
			change = -maxChange;
			if (change <= desiredChange)
			{
				change = desiredChange;
			}
		}
		return change;
	}

	void actor_applyTransform(MovementModule* moveMod)
	{
		SecObject* obj = moveMod->header.obj;
		if (moveMod->target.flags & TARGET_FREEZE)
		{
			return;
		}

		const angle14_32 speedRotation = moveMod->target.speedRotation & 0xffff;
		if (speedRotation == 0)
		{
			obj->pitch = moveMod->target.pitch;
			obj->yaw   = moveMod->target.yaw;
			obj->roll  = moveMod->target.roll;
			if (obj->type & OBJ_TYPE_3D)
			{
				obj3d_computeTransform(obj);
			}
		}
		else
		{
			const angle14_32 pitchDiff = getAngleDifference(obj->pitch, moveMod->target.pitch);
			const angle14_32 yawDiff   = getAngleDifference(obj->yaw,   moveMod->target.yaw);
			const angle14_32 rollDiff  = getAngleDifference(obj->roll,  moveMod->target.roll);
			if (yawDiff | pitchDiff | rollDiff)
			{
				const fixed16_16 angularSpd = mul16(intToFixed16(speedRotation), s_deltaTime);
				const angle14_32 angleChange = floor16(angularSpd);
				obj->yaw = (obj->yaw + computeAngleChange(yawDiff, angleChange)) & ANGLE_MASK;

				if (obj->type & OBJ_TYPE_3D)
				{
					obj->pitch = (obj->pitch + computeAngleChange(pitchDiff, angleChange)) & ANGLE_MASK;
					obj->roll  = (obj->roll  + computeAngleChange(rollDiff,  angleChange)) & ANGLE_MASK;
					obj3d_computeTransform(obj);
				}
			}
		}
	}

	// Default movement module function for "dispatch" actors
	Tick defaultActorFunc(ActorModule* module, MovementModule* moveMod)
	{
		moveMod->physics.wall = nullptr;
		moveMod->physics.u24 = 0;

		if (moveMod->target.flags & TARGET_MOVE_ROT)
		{
			actor_applyTransform(moveMod);
		}

		if ((moveMod->target.flags & TARGET_MOVE_XZ) || (moveMod->target.flags & TARGET_MOVE_Y) || moveMod->delta.x || moveMod->delta.y || moveMod->delta.z)
		{
			actor_handleMovementAndCollision(moveMod);
		}
		moveMod->target.flags &= ~TARGET_ALL_MOVE;
		return 0;
	}

	// Updates the actor target with the passed in target based on the flags.
	JBool defaultUpdateTargetFunc(MovementModule* moveMod, ActorTarget* target)
	{
		if (target->flags & TARGET_FREEZE)
		{
			moveMod->target.flags |= TARGET_FREEZE;
		}
		else
		{
			moveMod->target.flags &= ~TARGET_FREEZE;
			if (target->flags & TARGET_MOVE_XZ)
			{
				moveMod->target.pos.x = target->pos.x;
				moveMod->target.pos.z = target->pos.z;
				moveMod->target.speed = target->speed;
				moveMod->target.flags |= TARGET_MOVE_XZ;
			}
			if (target->flags & TARGET_MOVE_Y)
			{
				moveMod->target.pos.y = target->pos.y;
				moveMod->target.speedVert = target->speedVert;
				moveMod->target.flags |= TARGET_MOVE_Y;
			}
			if (target->flags & TARGET_MOVE_ROT)
			{
				moveMod->target.pitch = target->pitch;
				moveMod->target.yaw   = target->yaw;
				moveMod->target.roll  = target->roll;
				moveMod->target.speedRotation = target->speedRotation;
				moveMod->target.flags |= TARGET_MOVE_ROT;
			}
		}
		return JFALSE;
	}

	void actor_setupSmartObj(MovementModule* moveMod)
	{
		SecObject* obj = moveMod->header.obj;

		moveMod->physics.stepUpHeight = 0x38000;	// 3.5 units
		moveMod->physics.stepDownHeight = FIXED(4); // 4 units
		moveMod->physics.height = obj->worldHeight;
		moveMod->physics.width = obj->worldWidth;
		moveMod->physics.responseStep = JFALSE;
		moveMod->target.speed = ONE_16;
		moveMod->target.speedVert = ONE_16;
		moveMod->target.speedRotation = FIXED(45);
		moveMod->target.flags &= ~TARGET_ALL;
		moveMod->delta = { 0, 0, 0 };
		moveMod->collisionWall = nullptr;
		moveMod->unused = 0;

		moveMod->collisionFlags = (moveMod->collisionFlags | (ACTORCOL_NO_Y_MOVE | ACTORCOL_GRAVITY)) & ~ACTORCOL_SLIDE_RESPONSE;	// Set bits 0, 1 and clear bit 2. This creates a non-flying AI by default.
		obj->entityFlags |= ETFLAG_SMART_OBJ;
	}

	JBool actor_canSeeObject(SecObject* actorObj, SecObject* obj)
	{
		vec3_fixed p0 = { actorObj->posWS.x, actorObj->posWS.y - actorObj->worldHeight, actorObj->posWS.z };
		vec3_fixed p1 = { obj->posWS.x, obj->posWS.y, obj->posWS.z };
		if (collision_canHitObject(actorObj->sector, obj->sector, p0, p1, 0))
		{
			return JTRUE;
		}
		if (s_collision_wallHit)
		{
			return JFALSE;
		}

		vec3_fixed p2 = { obj->posWS.x, obj->posWS.y - obj->worldHeight, obj->posWS.z };
		return collision_canHitObject(actorObj->sector, obj->sector, p0, p2, 0);
	}
	   
	JBool actor_canSeeObjFromDist(SecObject* actorObj, SecObject* obj)
	{
		fixed16_16 approxDist = distApprox(actorObj->posWS.x, actorObj->posWS.z, obj->posWS.x, obj->posWS.z);
		// Crouching makes the target harder to see.
		if (obj->worldHeight < FIXED(3))
		{
			approxDist *= 2;
		}
		if (!s_headlampActive)
		{
			RSector* sector1 = obj->sector;
			fixed16_16 ambient = sector1->ambient;
			if (ambient > FIXED(22))
			{
				ambient = FIXED(31);
			}
			// Adds up to 248 units to the approximate distande: 248 when ambient = 0, 0 when ambient = 31
			approxDist += FIXED(248) - (ambient << 3);
		}
		else
		{
			approxDist -= s_baseAtten * 2;
		}

		if (approxDist < FIXED(256))
		{
			// Since random() is unsigned, the real visible range is [200, 256) because of the conditional above.
			fixed16_16 rndDist = random(FIXED(256)) + FIXED(200);
			if (approxDist < rndDist)
			{
				return actor_canSeeObject(actorObj, obj);
			}
		}
		return JFALSE;
	}

	JBool actor_isObjectVisible(SecObject* actorObj, SecObject* obj, angle14_32 fov, fixed16_16 closeDist)
	{
		fixed16_16 approxDist = distApprox(actorObj->posWS.x, actorObj->posWS.z, obj->posWS.x, obj->posWS.z);
		if (approxDist <= closeDist)
		{
			return actor_canSeeObjFromDist(actorObj, obj);
		}

		fixed16_16 dx = obj->posWS.x - actorObj->posWS.x;
		fixed16_16 dz = obj->posWS.z - actorObj->posWS.z;
		angle14_32 yaw0 = actorObj->yaw;
		angle14_32 obj0To1Angle = vec2ToAngle(dx, dz);
		angle14_32 angleDiff = getAngleDifference(obj0To1Angle, yaw0);
		angle14_32 right = fov >> 1;
		angle14_32 left = -(fov >> 1);
		if (angleDiff > left && angleDiff < right)
		{
			return actor_canSeeObjFromDist(actorObj, obj);
		}
		return JFALSE;
	}

	MovementModule* actor_createMovementModule(ActorDispatch* dispatch)
	{
		MovementModule* moveMod = (MovementModule*)level_alloc(sizeof(MovementModule));
		memset(moveMod, 0, sizeof(MovementModule));
		
		actor_initModule((ActorModule*)moveMod, (Logic*)dispatch);
		actor_setupSmartObj(moveMod);

		moveMod->header.func = defaultActorFunc;
		moveMod->header.freeFunc = nullptr;
		moveMod->header.type = ACTMOD_MOVE;
		moveMod->updateTargetFunc = defaultUpdateTargetFunc;
		// Overwrites width even though it was set in actor_setupSmartObj()
		moveMod->physics.width = 0x18000;	// 1.5 units
		moveMod->physics.wall = nullptr;
		moveMod->physics.u24 = 0;
		moveMod->physics.obj = moveMod->header.obj;

		return moveMod;
	}

	void actorLogicCleanupFunc(Logic* logic)
	{
		ActorDispatch* dispatch = (ActorDispatch*)logic;
		for (s32 i = 0; i < ACTOR_MAX_MODULES; i++)
		{
			ActorModule* module = dispatch->modules[ACTOR_MAX_MODULES - 1 - i];
			if (module)
			{
				if (module->freeFunc)
				{
					module->freeFunc(module);
				}
				else
				{
					level_free(module);
				}
			}
		}

		if (dispatch->freeTask)
		{
			s_msgEntity = dispatch->logic.obj;
			task_runAndReturn(dispatch->freeTask, MSG_FREE);
		}

		ActorModule* moveMod = (ActorModule*)dispatch->moveMod;
		if (moveMod && moveMod->freeFunc)
		{
			moveMod->freeFunc(moveMod);
		}
		deleteLogicAndObject((Logic*)dispatch);
		allocator_deleteItem(s_istate.actorDispatch, dispatch);
	}
	
	// Returns JTRUE when animation is finished, otherwise JFALSE
	JBool actor_advanceAnimation(LogicAnimation* anim, SecObject* obj)
	{
		if (!anim->prevTick)
		{
			// Start the animation
			anim->prevTick = s_frameTicks[anim->frameRate];
			anim->flags &= ~AFLAG_READY;

			obj->frame = floor16(anim->startFrame);
			anim->frame = anim->startFrame;
		}
		else
		{
			anim->frame += s_frameTicks[anim->frameRate] - anim->prevTick;
			anim->prevTick = s_frameTicks[anim->frameRate];

			fixed16_16 endFrame = anim->startFrame + anim->frameCount;
			if (anim->frame >= endFrame)
			{
				if (anim->flags & AFLAG_PLAYONCE)
				{
					// Non-looping animation; animation will end at the final frame
					endFrame -= ONE_16;
					anim->frame = endFrame;
					obj->frame = floor16(endFrame);
					return JTRUE;
				}

				while (anim->frame >= endFrame)
				{
					// Rewind to start of animation (looping animation)
					anim->frame -= anim->frameCount;
				}
			}
			obj->frame = floor16(anim->frame);
		}
		return JFALSE;
	}

	// Get the animation index for the current 'action' from s_curLogic.
	s32 actor_getAnimationIndex(s32 action)
	{
		ActorDispatch* logic = (ActorDispatch*)s_actorState.curLogic;
		SecObject* obj = logic->logic.obj;
		if (obj->type == OBJ_TYPE_SPRITE && logic->animTable)
		{
			return logic->animTable[action];
		}
		return -1;
	}

	void actor_setupAnimation(s32 animIdx, LogicAnimation* aiAnim)
	{
		ActorDispatch* actorLogic = (ActorDispatch*)s_actorState.curLogic;
		const s32* animTable = actorLogic->animTable;
		SecObject* obj = actorLogic->logic.obj;

		aiAnim->flags |= AFLAG_READY;
		if (obj->type & OBJ_TYPE_SPRITE)
		{
			aiAnim->prevTick = 0;
			aiAnim->animId = animTable[animIdx];
			aiAnim->startFrame = 0;
			aiAnim->frame = 0;
			assert(obj->wax);
			if (aiAnim->animId != -1 && obj->wax)
			{
				// In DOS there is no check but for some reason it doesn't crash.
				// On Windows this crashes if animId is too large, so clamp.
				s32 animId = min(aiAnim->animId, obj->wax->animCount - 1);
				WaxAnim* anim = WAX_AnimPtr(obj->wax, animId);
				assert(anim);
				if (anim)
				{
					aiAnim->frameCount = intToFixed16(anim->frameCount);
					aiAnim->frameRate  = anim->frameRate;
					if (anim->frameRate >= 12)
					{
						aiAnim->frameRate = 12;
					}
					aiAnim->flags &= ~AFLAG_READY;
				}
			}
		}
	}

	void actor_setupBossAnimation(SecObject* obj, s32 animId, LogicAnimation* anim)
	{
		anim->flags |= AFLAG_READY;
		if (obj->type == OBJ_TYPE_SPRITE)
		{
			const Wax* wax = obj->wax;
			// In DOS there is no check but for some reason it doesn't crash with an animId that is too large.
			// On Windows this crashes if animId is too large, so clamp.
			animId = wax ? min(animId, wax->animCount - 1) : -1;

			anim->prevTick = 0;
			anim->animId = animId;
			anim->startFrame = 0;
			if (animId != -1)
			{
				WaxAnim* waxAnim = WAX_AnimPtr(wax, animId);
				assert(waxAnim);

				anim->frameCount = intToFixed16(waxAnim->frameCount);
				anim->frameRate = waxAnim->frameRate;
				if (anim->frameRate >= 12)
				{
					anim->frameRate = 12;
				}
				anim->flags &= ~AFLAG_READY;
			}
		}
	}

	void actor_setAnimFrameRange(LogicAnimation* anim, s32 startFrame, s32 endFrame)
	{
		anim->frameCount = intToFixed16(endFrame - startFrame + 1);
		anim->startFrame = intToFixed16(startFrame);
	}

	void actor_setCurAnimation(LogicAnimation* aiAnim)
	{
		if (aiAnim->animId != -1)
		{
			s_actorState.curAnimation = aiAnim;
		}
	}

	JBool actor_canDie(PhysicsActor* phyActor)
	{
		SecObject* obj = phyActor->moveMod.header.obj;
		RSector* sector = obj->sector;
		if (sector->secHeight <= 0 && obj->posWS.y <= sector->floorHeight)
		{
			if (TFE_Jedi::abs(phyActor->vel.x) >= FIXED(5) || TFE_Jedi::abs(phyActor->vel.z) >= FIXED(5))
			{
				return JFALSE;
			}

			fixed16_16 ceilHeight, floorHeight;
			sector_getObjFloorAndCeilHeight(sector, obj->posWS.y, &floorHeight, &ceilHeight);
			if (obj->posWS.y < floorHeight)
			{
				return JFALSE;
			}
		}
		return JTRUE;
	}

	// Kill the actor, this clears the current logic so the rest of the task function proceeds correctly.
	void actor_kill()
	{
		actorLogicCleanupFunc(s_actorState.curLogic);
		s_actorState.curLogic = nullptr;
	}

	// Removes logics other than the current Logic.
	void actor_removeLogics(SecObject* obj)
	{
		Logic** logicList = (Logic**)allocator_getHead((Allocator*)obj->logic);
		while (logicList)
		{
			Logic* logic = *logicList;
			if (logic != s_actorState.curLogic)
			{
				if (logic->cleanupFunc) { logic->cleanupFunc(logic); }
			}
			logicList = (Logic**)allocator_getNext((Allocator*)obj->logic);
		};
	}

	void actor_addVelocity(fixed16_16 pushX, fixed16_16 pushY, fixed16_16 pushZ)
	{
		ActorDispatch* actorLogic = (ActorDispatch*)s_actorState.curLogic;
		actorLogic->vel.x += pushX;
		actorLogic->vel.y += pushY;
		actorLogic->vel.z += pushZ;
	}
	
	void actor_messageFunc(MessageType msg, void* logic)
	{
		ActorDispatch* dispatch = (ActorDispatch*)logic;
		s_actorState.curLogic = (Logic*)logic;
		SecObject* obj = s_actorState.curLogic->obj;
	
		// Send the message to each module via the module's message function
		for (s32 i = 0; i < ACTOR_MAX_MODULES; i++)
		{
			ActorModule* module = dispatch->modules[ACTOR_MAX_MODULES - 1 - i];
			if (module && module->msgFunc)
			{
				const Tick nextTick = module->msgFunc(msg, module, dispatch->moveMod);
				if (nextTick != 0xffffffff)
				{
					module->nextTick = nextTick;
				}
			}
		}

		// WAKEUP, DAMAGE and EXPLOSION messages will wake (alert) the actor from an idle state
		if (msg == MSG_WAKEUP)
		{
			if (dispatch->flags & ACTOR_IDLE)
			{
				gameMusic_startFight();
			}

			if ((dispatch->flags & ACTOR_NPC) && (dispatch->flags & ACTOR_IDLE))
			{
				if (s_actorState.nextAlertTick < s_curTick)
				{
					if (dispatch->flags & ACTOR_OFFIC_ALERT)  // Officer alert list.
					{
						dispatch->alertSndID = sound_playCued(s_officerAlertSndSrc[s_actorState.officerAlertIndex], obj->posWS);
						s_actorState.officerAlertIndex++;
						if (s_actorState.officerAlertIndex >= OFFICER_ALERT_COUNT)
						{
							s_actorState.officerAlertIndex = 0;
						}
					}
					else if (dispatch->flags & ACTOR_TROOP_ALERT)  // Storm trooper alert list
					{
						dispatch->alertSndID = sound_playCued(s_stormAlertSndSrc[s_actorState.stormtrooperAlertIndex], obj->posWS);
						s_actorState.stormtrooperAlertIndex++;
						if (s_actorState.stormtrooperAlertIndex >= STORM_ALERT_COUNT)
						{
							s_actorState.stormtrooperAlertIndex = 0;
						}
					}
					else // Single alert.
					{
						dispatch->alertSndID = sound_playCued(dispatch->alertSndSrc, obj->posWS);
					}
					s_actorState.nextAlertTick = s_curTick + 291;	// ~2 seconds between alerts
				}
				dispatch->flags &= ~ACTOR_IDLE;		// remove flag bit 0 (ACTOR_IDLE)

				// TFE - call alertscript, add the ObjectId as the final arg
				if (dispatch->alertScriptCall >= 0)
				{
					LogicScriptCall* scriptCall = logic_getScriptCall(dispatch->alertScriptCall);
					if (scriptCall && scriptCall->funcPtr)
					{
						scriptCall->args[scriptCall->argCount].type = TFE_ForceScript::ARG_S32;
						scriptCall->args[scriptCall->argCount].iValue = obj_getRefIndex(obj);
						scriptCall->argCount++;
						TFE_ForceScript::execFunc(scriptCall->funcPtr, scriptCall->argCount, scriptCall->args);
					}
				}
			}
		}
		else if (msg == MSG_DAMAGE || msg == MSG_EXPLOSION)
		{
			if (dispatch->flags & ACTOR_IDLE)
			{
				gameMusic_startFight();

				// TFE - call alertscript, add the ObjectId as the final arg
				if (dispatch->alertScriptCall >= 0)
				{
					LogicScriptCall* scriptCall = logic_getScriptCall(dispatch->alertScriptCall);
					if (scriptCall && scriptCall->funcPtr)
					{
						scriptCall->args[scriptCall->argCount].type = TFE_ForceScript::ARG_S32;
						scriptCall->args[scriptCall->argCount].iValue = obj_getRefIndex(obj);
						scriptCall->argCount++;
						TFE_ForceScript::execFunc(scriptCall->funcPtr, scriptCall->argCount, scriptCall->args);
					}
				}
			}
			dispatch->flags &= ~ACTOR_IDLE;
			s_actorState.curAnimation = nullptr;
		}
	}

	void actor_sendTerminalVelMsg(SecObject* obj)
	{
		message_sendToObj(obj, MSG_TERMINAL_VEL, actor_messageFunc);
	}

	void actor_sendWakeupMsg(SecObject* obj)
	{
		message_sendToObj(obj, MSG_WAKEUP, actor_messageFunc);
	}

	void actor_handlePhysics(MovementModule* moveMod, vec3_fixed* vel)
	{
		SecObject* obj = moveMod->physics.obj;
		assert(obj && obj->sector);

		moveMod->delta = { 0, 0, 0 };
		if (vel->x) { moveMod->delta.x = mul16(vel->x, s_deltaTime); }
		if (vel->y) { moveMod->delta.y = mul16(vel->y, s_deltaTime); }
		if (vel->z) { moveMod->delta.z = mul16(vel->z, s_deltaTime); }

		fixed16_16 friction = ONE_16 - s_deltaTime*2;
		if (moveMod->collisionFlags & ACTORCOL_GRAVITY)
		{
			fixed16_16 ceilHeight, floorHeight;
			sector_getObjFloorAndCeilHeight(obj->sector, obj->posWS.y, &floorHeight, &ceilHeight);

			// Add gravity to our velocity if the object is not on the floor.
			if (obj->posWS.y < floorHeight)
			{
				vel->y += mul16(s_gravityAccel, s_deltaTime);
			}

			JBool vertCollision = JFALSE;
			JBool hitCeiling = JFALSE;
			// Did the object hit the ceiling?
			if (vel->y < 0 && obj->posWS.y <= ceilHeight + moveMod->physics.height)
			{
				vertCollision = JTRUE;
				hitCeiling = JTRUE;
			}
			// Or the floor?
			else if (obj->posWS.y >= floorHeight)
			{
				vertCollision = JTRUE;
				hitCeiling = JFALSE;
			}

			// Die once falling fast enough.
			if (vel->y > FIXED(160))
			{
				actor_sendTerminalVelMsg(obj);
			}
			
			if (vertCollision)
			{
				// Clear out the yVel if the object hits the floor or ceiling.
				if ((hitCeiling && vel->y < 0) || (!hitCeiling && vel->y >= 0))
				{
					vel->y = 0;
				}
			}
			else if (vel->y < 0 || obj->posWS.y < floorHeight)
			{
				friction = ONE_16 - s_deltaTime / 2;
			}
		}
		else
		{
			vel->y = mul16(vel->y, friction);
		}
		vel->x = mul16(vel->x, friction);
		vel->z = mul16(vel->z, friction);

		if (TFE_Jedi::abs(vel->x) < ACTOR_MIN_VELOCITY) { vel->x = 0; }
		if (TFE_Jedi::abs(vel->y) < ACTOR_MIN_VELOCITY) { vel->y = 0; }
		if (TFE_Jedi::abs(vel->z) < ACTOR_MIN_VELOCITY) { vel->z = 0; }
	}

	// Local run func for actor task
	void actorLogicMsgFunc(MessageType msg)
	{
		if (msg == MSG_FREE)
		{
			actorLogicCleanupFunc((Logic*)s_msgTarget);
		}
		else
		{
			actor_messageFunc(msg, s_msgTarget);
		}
	}

	// Task function for dispatch actors
	// Iterates through all actors in s_istate.actorDispatch and updates them
	void actorLogicTaskFunc(MessageType msg)
	{
		task_begin;
		while (msg != MSG_FREE_TASK)
		{
			entity_yield(TASK_NO_DELAY);
			if (msg == MSG_RUN_TASK)
			{
				ActorDispatch* dispatch = (ActorDispatch*)allocator_getHead(s_istate.actorDispatch);
				while (dispatch)
				{
					SecObject* obj = dispatch->logic.obj;
					const u32 flags = dispatch->flags;
					if ((flags & ACTOR_IDLE) && (flags & ACTOR_NPC))
					{
						if (dispatch->nextTick < s_curTick)
						{
							dispatch->nextTick = s_curTick + dispatch->delay;
							if (actor_isObjectVisible(obj, s_playerObject, dispatch->fov, dispatch->awareRange))
							{
								// Wake up, and alert other actors within a 150 unit range
								message_sendToObj(obj, MSG_WAKEUP, actor_messageFunc);
								gameMusic_startFight();
								collision_effectObjectsInRangeXZ(obj->sector, FIXED(150), obj->posWS, actor_sendWakeupMsg, obj, ETFLAG_AI_ACTOR);
							}
						}

						// TFE: Animate in idle state
						if (TFE_Settings::jsonAiLogics())
						{
							s_actorState.curAnimation = nullptr;
							if (obj->type & OBJ_TYPE_SPRITE)
							{
								// Find the thinker module and set its animation as the current animation
								for (s32 i = 0; i < ACTOR_MAX_MODULES; i++)
								{
									ActorModule* module = dispatch->modules[ACTOR_MAX_MODULES - 1 - i];
									if (module && module->type == ACTMOD_THINKER)
									{
										ThinkerModule* thinkerMod = (ThinkerModule*)module;
										actor_setCurAnimation(&thinkerMod->anim);
										break;
									}
								}

								if (s_actorState.curAnimation)
								{
									obj->anim = s_actorState.curAnimation->animId;
									if (actor_advanceAnimation(s_actorState.curAnimation, obj))
									{
										// The animation has finished.
										s_actorState.curAnimation->flags |= AFLAG_READY;
									}
								}
							}
						}
					}
					else  // actor is not idle
					{
						s_actorState.curLogic = (Logic*)dispatch;
						s_actorState.curAnimation = nullptr;
						for (s32 i = 0; i < ACTOR_MAX_MODULES; i++)
						{
							ActorModule* module = dispatch->modules[ACTOR_MAX_MODULES - 1 - i];
							if (module && module->func && module->nextTick < s_curTick)
							{
								module->nextTick = module->func(module, dispatch->moveMod);
							}
						}

						if (s_actorState.curLogic && !(dispatch->flags & ACTOR_IDLE))
						{
							MovementModule* moveMod = dispatch->moveMod;
							if (moveMod && moveMod->header.func)
							{
								actor_handlePhysics(moveMod, &dispatch->vel);
								moveMod->header.func((ActorModule*)moveMod, moveMod);
							}

							if ((obj->type & OBJ_TYPE_SPRITE) && s_actorState.curAnimation)
							{
								obj->anim = s_actorState.curAnimation->animId;
								if (actor_advanceAnimation(s_actorState.curAnimation, obj))
								{
									// The animation has finished.
									s_actorState.curAnimation->flags |= AFLAG_READY;
								}
							}
						}
					}

					dispatch = (ActorDispatch*)allocator_getNext(s_istate.actorDispatch);
				}
			}
		}
		task_end;
	}

	// Task function for bosses, mousebots, welders, turrets
	// Iterates through all actors in s_physicsActors and updates them
	// TODO: Rename "physics" actor as appopriate.
	void actorPhysicsTaskFunc(MessageType msg)
	{
		task_begin;
		while (msg != MSG_FREE_TASK)
		{
			task_localBlockBegin;
			PhysicsActor** phyObjPtr = (PhysicsActor**)list_getHead(s_physicsActors);
			while (phyObjPtr)
			{
				PhysicsActor* phyObj = *phyObjPtr;
				phyObj->moveMod.physics.wall = nullptr;
				phyObj->moveMod.physics.u24 = 0;
				if (phyObj->moveMod.target.flags & TARGET_MOVE_ROT)
				{
					actor_applyTransform(&phyObj->moveMod);
				}
				actor_handlePhysics(&phyObj->moveMod, &phyObj->vel);

				if ((phyObj->moveMod.target.flags & TARGET_MOVE_XZ) || (phyObj->moveMod.target.flags & TARGET_MOVE_Y) || phyObj->vel.x || phyObj->vel.y || phyObj->vel.z)
				{
					actor_handleMovementAndCollision(&phyObj->moveMod);
					CollisionInfo* physics = &phyObj->moveMod.physics;
					if (physics->responseStep)
					{
						handleCollisionResponseSimple(physics->responseDir.x, physics->responseDir.z, &phyObj->vel.x, &phyObj->vel.z);
					}
				}

				SecObject* obj = phyObj->moveMod.header.obj;
				LogicAnimation* anim = &phyObj->anim;
				if (obj->type & OBJ_TYPE_SPRITE)
				{
					if (!(anim->flags & AFLAG_READY))
					{
						obj->anim = anim->animId;
						if (actor_advanceAnimation(anim, obj))
						{
							anim->flags |= AFLAG_READY;
						}
					}
				}

				phyObjPtr = (PhysicsActor**)list_getNext(s_physicsActors);
			}
			task_localBlockEnd;
			do
			{
				entity_yield(TASK_NO_DELAY);
			} while (msg != MSG_RUN_TASK);
		}
		task_end;
	}
}  // namespace TFE_DarkForces
