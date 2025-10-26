#include <cstring>

#include "turret.h"
#include "actorModule.h"
#include "actorSerialization.h"
#include "../logic.h"
#include <TFE_DarkForces/player.h>
#include <TFE_DarkForces/hitEffect.h>
#include <TFE_DarkForces/projectile.h>
#include <TFE_DarkForces/item.h>
#include <TFE_DarkForces/random.h>
#include <TFE_DarkForces/pickup.h>
#include <TFE_DarkForces/weapon.h>
#include <TFE_DarkForces/sound.h>
#include <TFE_Game/igame.h>
#include <TFE_Asset/modelAsset_jedi.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_Jedi/Memory/list.h>
#include <TFE_Jedi/Memory/allocator.h>
#include <TFE_Jedi/Serialization/serialization.h>

namespace TFE_DarkForces
{
	enum TurretState
	{
		TURRETSTATE_DEFAULT        = 0,
		TURRETSTATE_FIRING         = 1,
		TURRETSTATE_ATTACKING      = 2,
		TURRETSTATE_AIMING         = 3,
		TURRETSTATE_OUT_OF_CONTROL = 4,
		TURRETSTATE_DYING          = 5,
		TURRETSTATE_COUNT
	};

	enum TurretConstants
	{
		TURRET_HP            = FIXED(60),  // Starting HP.
		TURRET_WIDTH         = FIXED(3),
		TURRET_HEIGHT        =-FIXED(2),
		//TURRET_FIRE_Z_OFFSET = 19660,	   // ~0.3
		TURRET_FIRE_Z_OFFSET = 98300,	   // ~1.5  -- TODO: This seems to be required for mods like Dark Tide 4, but I'm not sure why.
		TURRET_FIRE_ZMAX_OFFSET = 294900,   // ~1.5 * 3
		TURRET_YAW_TOLERANCE = 3185,	   // How close the turret aim has to be in order to fire.
		TURRET_PITCH_RANGE   = 1820,	   // How much pitch can deviate from the current value when aiming (+/- 40 degrees)
		TURRET_MAX_DIST      = FIXED(140), // Maximum distance that the turret will shoot the player.
		TURRET_MIN_DIST      = FIXED(70),  // Minimum distance where the turret no longer cares about the relative angle when spotting the player.
		TURRET_ATTACK_DELAY  = 72,         // 0.5 seconds - the minimum delay from entering the attacking state and transitioning to the firing state.
		TURRET_ATTACK_TRANS  = 436,        // ~3 seconds - the delay from the attacking state to the aiming state, since the last sight of the player.
		TURRET_ACCURACY_RAND = FIXED(10),  // These determine how accurate the turret is. The higher the value, the more the bolt velocities are randomized.
		TURRET_ACCURACY_OFFSET = FIXED(5), // These is generally TURRET_ACCURACY_RAND/2
		TURRET_ROTATION_SPD  = 7736,       // ~170 degrees per second.
		TURRET_OUT_OF_CONTROL_ROTATE_SPD = 12288,   // ~270 degrees per second - the rotation speed when the turret is out of control.

		TURRET_OVERLAP_MIN = FIXED(5),
	};
	
	struct TurretResources
	{
		SoundSourceId sound1 = NULL_SOUND;
	};
	struct Turret
	{
		Logic logic;
		PhysicsActor actor;

		angle14_16 pitch;
		angle14_16 yaw;
		Tick nextTick;
	};

	static TurretResources s_turretRes = {};
	static Turret* s_curTurret;
	static s32 s_turretNum = 0;

	void turret_exit()
	{
		s_turretRes = {};
	}

	void turret_precache()
	{
		s_turretRes.sound1 = sound_load("TURRET-1.VOC", SOUND_PRIORITY_MED2);
	}

	MessageType turret_handleDamage(MessageType msg, Turret* turret)
	{
		PhysicsActor* physicsActor = &turret->actor;
		if (physicsActor->alive)
		{
			ProjectileLogic* proj = (ProjectileLogic*)s_msgEntity;
			physicsActor->hp -= proj->dmg;
			if (physicsActor->hp <= 0)
			{
				physicsActor->state = TURRETSTATE_DYING;
				msg = MSG_RUN_TASK;
			}
			else if (physicsActor->hp < FIXED(8))
			{
				physicsActor->state = TURRETSTATE_OUT_OF_CONTROL;
				msg = MSG_RUN_TASK;
			}
			else
			{
				msg = MSG_DAMAGE;
			}
		}
		return msg;
	}

	MessageType turret_handleExplosion(MessageType msg, Turret* turret)
	{
		PhysicsActor* physicsActor = &turret->actor;
		if (physicsActor->alive)
		{
			fixed16_16 dmg = s_msgArg1;
			physicsActor->hp -= dmg;
			if (physicsActor->hp <= 0)
			{
				physicsActor->state = TURRETSTATE_DYING;
				msg = MSG_RUN_TASK;
			}
			else if (physicsActor->hp < FIXED(8))
			{
				physicsActor->state = TURRETSTATE_OUT_OF_CONTROL;
				msg = MSG_RUN_TASK;
			}
			else
			{
				msg = MSG_EXPLOSION;
			}
		}
		return msg;
	}

	void turret_handleAttackingState(MessageType msg)
	{
		struct LocalContext
		{
			Turret* turret;
			PhysicsActor* physicsActor;
			ActorTarget* target;
			SecObject* obj;
		};
		task_begin_ctx;
		task_localBlockBegin;

		local(turret) = s_curTurret;
		local(physicsActor) = &local(turret)->actor;
		local(target) = &local(physicsActor)->moveMod.target;
		local(obj) = local(physicsActor)->moveMod.header.obj;
		fixed16_16 dz = s_playerObject->posWS.z - local(obj)->posWS.z;
		fixed16_16 dx = s_playerObject->posWS.x - local(obj)->posWS.x;
		local(target)->yaw = vec2ToAngle(dx, dz) & ANGLE_MASK;

		fixed16_16 dist = distApprox(local(obj)->posWS.x, local(obj)->posWS.z, s_playerObject->posWS.x, s_playerObject->posWS.z);
		angle14_32 aimPitch = vec2ToAngle(local(obj)->posWS.y - (s_eyePos.y + ONE_16), dist);
		angle14_32 pitchDiff = TFE_Jedi::clamp(getAngleDifference(local(turret)->pitch, aimPitch), -TURRET_PITCH_RANGE, TURRET_PITCH_RANGE);
		local(target)->pitch = (local(turret)->pitch + pitchDiff) & ANGLE_MASK;
		local(target)->flags = (local(target)->flags | TARGET_MOVE_ROT) & (~TARGET_MOVE_XZ);

		task_localBlockEnd;
		local(turret)->nextTick = s_curTick + TURRET_ATTACK_TRANS;
		while (local(physicsActor)->state == TURRETSTATE_ATTACKING)
		{
			do
			{
				entity_yield(TURRET_ATTACK_DELAY);
				if (msg == MSG_DAMAGE)
				{
					msg = turret_handleDamage(msg, local(turret));
				}
				else if (msg == MSG_EXPLOSION)
				{
					msg = turret_handleExplosion(msg, local(turret));
				}
			} while (msg != MSG_RUN_TASK);

			if (local(physicsActor)->state == TURRETSTATE_ATTACKING && local(target)->yaw == local(obj)->yaw && local(target)->pitch == local(obj)->pitch)
			{
				if (!s_playerDying && actor_isObjectVisible(local(obj), s_playerObject, ANGLE_MAX, TURRET_MIN_DIST))
				{
					fixed16_16 dy = TFE_Jedi::abs(local(obj)->posWS.y - s_playerObject->posWS.y);
					fixed16_16 dist = dy + distApprox(s_playerObject->posWS.x, s_playerObject->posWS.z, local(obj)->posWS.x, local(obj)->posWS.z);
					if (dist > TURRET_MAX_DIST)
					{
						continue;
					}

					fixed16_16 dz = s_playerObject->posWS.z - local(obj)->posWS.z;
					fixed16_16 dx = s_playerObject->posWS.x - local(obj)->posWS.x;
					angle14_32 yaw = vec2ToAngle(dx, dz);
					angle14_32 yawDiff = getAngleDifference(local(obj)->yaw, yaw) & ANGLE_MASK;
					if (yawDiff < TURRET_YAW_TOLERANCE)
					{
						local(turret)->actor.state = TURRETSTATE_FIRING;
						continue;
					}

					local(target)->yaw = yaw & ANGLE_MASK;
					dist = distApprox(local(obj)->posWS.x, local(obj)->posWS.z, s_playerObject->posWS.x, s_playerObject->posWS.z);
					angle14_32 pitch = vec2ToAngle(local(obj)->posWS.y - (s_eyePos.y + ONE_16), dist);
					angle14_32 pitchDiff = clamp(getAngleDifference(local(turret)->pitch, pitch), -TURRET_PITCH_RANGE, TURRET_PITCH_RANGE);
					local(target)->pitch = (local(turret)->pitch + pitchDiff) & ANGLE_MASK;
					local(target)->flags = (local(target)->flags | TARGET_MOVE_ROT) & (~TARGET_MOVE_XZ);
					local(turret)->nextTick = s_curTick + TURRET_ATTACK_TRANS;
				}
				else if (s_curTick >= local(turret)->nextTick)
				{
					local(turret)->actor.state = TURRETSTATE_AIMING;
				}
			}
		}
		task_end;
	}

	fixed16_16 turret_getZOffset(SecObject* srcObj)
	{
		RSector* sector = srcObj->sector;
		for (s32 i = 0, objIdx = 0; i < sector->objectCapacity && objIdx < sector->objectCount; i++)
		{
			SecObject* obj = sector->objectList[i];
			if (obj)
			{
				const JBool isOverlapping3D = obj->type == OBJ_TYPE_3D && obj->worldWidth > TURRET_OVERLAP_MIN && !obj->projectileLogic;
				if (obj != srcObj && isOverlapping3D)
				{
					// Hack to make AT-ST turrets work in the Dark Tide mods.
					// TODO: DOS shouldn't work in this case, figure out why it does (probably related to low framerate and cycles setting).
					const fixed16_16 dx = obj->posWS.x - srcObj->posWS.x;
					const fixed16_16 dz = obj->posWS.z - srcObj->posWS.z;
					if (dx < ONE_16 && dz < ONE_16)
					{
						return TURRET_FIRE_ZMAX_OFFSET;
					}
				}
				objIdx++;
			}
		}
		return TURRET_FIRE_Z_OFFSET;
	}
		
	void turrent_handleFiringState(MessageType msg)
	{
		Turret* turret = s_curTurret;
		PhysicsActor* physicsActor = &turret->actor;
		SecObject* obj = physicsActor->moveMod.header.obj;

		fixed16_16 mtx[9];
		weapon_computeMatrix(mtx, -obj->pitch, -obj->yaw);

		RSector* sector = obj->sector;
		vec3_fixed inVec = { 0, 0, turret_getZOffset(obj) };
		vec3_fixed outVec;
		rotateVectorM3x3(&inVec, &outVec, mtx);

		vec3_fixed pos = { obj->posWS.x + outVec.x, obj->posWS.y + outVec.y, obj->posWS.z + outVec.z };
		ProjectileLogic* proj = (ProjectileLogic*)createProjectile(PROJ_TURRET_BOLT, sector, pos.x, pos.y, pos.z, obj);
		sound_playCued(s_turretRes.sound1, pos);

		proj->prevColObj = obj;
		proj->prevObj = obj;
		proj->excludeObj = obj;

		SecObject* projObj = proj->logic.obj;
		projObj->pitch = obj->pitch;
		projObj->yaw = obj->yaw;

		vec3_fixed targetPos = { s_eyePos.x, s_eyePos.y + ONE_16, s_eyePos.z };
		proj_aimAtTarget(proj, targetPos);

		proj->vel.x += random(TURRET_ACCURACY_RAND) - TURRET_ACCURACY_OFFSET;
		proj->vel.y += random(TURRET_ACCURACY_RAND) - TURRET_ACCURACY_OFFSET;
		proj->vel.z += random(TURRET_ACCURACY_RAND) - TURRET_ACCURACY_OFFSET;

		physicsActor->state = TURRETSTATE_ATTACKING;
	}

	void turret_handleHighlyDamagedState(MessageType msg)
	{
		struct LocalContext
		{
			Turret* turret;
			PhysicsActor* physicsActor;
			ActorTarget* target;
			SecObject* obj;
		};
		task_begin_ctx;

		local(turret) = s_curTurret;
		local(physicsActor) = &local(turret)->actor;
		local(obj) = local(turret)->logic.obj;
		while (local(physicsActor)->state == TURRETSTATE_OUT_OF_CONTROL)
		{
			do
			{
				entity_yield(TASK_NO_DELAY);
				if (msg == MSG_DAMAGE)
				{
					msg = turret_handleDamage(msg, local(turret));
				}
				else if (msg == MSG_EXPLOSION)
				{
					msg = turret_handleExplosion(msg, local(turret));
				}
			} while (msg != MSG_RUN_TASK);

			ActorTarget* target = &local(physicsActor)->moveMod.target;
			if (local(physicsActor)->state == TURRETSTATE_OUT_OF_CONTROL && target->yaw == local(obj)->yaw && target->pitch == local(obj)->pitch)
			{
				fixed16_16 mtx[9];
				weapon_computeMatrix(mtx, -local(obj)->pitch, -local(obj)->yaw);

				vec3_fixed inVec = { 0, 0, TURRET_FIRE_Z_OFFSET };
				vec3_fixed outVec;
				rotateVectorM3x3(&inVec, &outVec, mtx);

				vec3_fixed pos = { local(obj)->posWS.x + outVec.x, local(obj)->posWS.y + outVec.y, local(obj)->posWS.z + outVec.z };
				ProjectileLogic* proj = (ProjectileLogic*)createProjectile(PROJ_TURRET_BOLT, local(obj)->sector, pos.x, pos.y, pos.z, local(obj));
				sound_playCued(s_turretRes.sound1, pos);

				proj->prevColObj = local(obj);
				proj->excludeObj = local(obj);
				proj_setTransform(proj, local(obj)->pitch, local(obj)->yaw);

				// Set a new random target.
				target->yaw   = random(ANGLE_MAX);
				target->pitch = (-random(TURRET_PITCH_RANGE)) & ANGLE_MASK;
				target->flags = (target->flags | TARGET_MOVE_ROT) & (~TARGET_MOVE_XZ);
				target->speedRotation = TURRET_OUT_OF_CONTROL_ROTATE_SPD;
			}
		}
		task_end;
	}

	MessageType turret_handleMessages(MessageType msg, Turret* turret)
	{
		if (msg == MSG_DAMAGE)
		{
			msg = turret_handleDamage(msg, turret);
		}
		else if (msg == MSG_EXPLOSION)
		{
			msg = turret_handleExplosion(msg, turret);
		}
		return msg;
	}

	void turret_handleDefaultState(MessageType msg)
	{
		struct LocalContext
		{
			Turret* turret;
			PhysicsActor* physicsActor;
			ActorTarget* target;
			SecObject* obj;
		};
		task_begin_ctx;

		local(turret) = s_curTurret;
		local(physicsActor) = &local(turret)->actor;
		local(obj) = local(turret)->logic.obj;

		while (local(physicsActor)->state == TURRETSTATE_DEFAULT)
		{
			do
			{
				entity_yield(145);
				if (msg == MSG_DAMAGE)
				{
					msg = turret_handleDamage(msg, local(turret));
				}
				else if (msg == MSG_EXPLOSION)
				{
					msg = turret_handleDamage(msg, local(turret));
				}

				if (msg == MSG_DAMAGE || msg == MSG_EXPLOSION)
				{
					local(physicsActor)->state = TURRETSTATE_ATTACKING;
					task_makeActive(local(physicsActor)->actorTask);
					task_yield(TASK_NO_DELAY);
				}
			} while (msg != MSG_RUN_TASK);

			// Check to see if the player is visible and in range to be shot.
			// If so, switch to the shooting state.
			if (local(physicsActor)->state == TURRETSTATE_DEFAULT && !s_playerDying && actor_isObjectVisible(local(obj), s_playerObject, ANGLE_MAX, TURRET_MIN_DIST))
			{
				fixed16_16 dy = TFE_Jedi::abs(local(obj)->posWS.y - s_playerObject->posWS.y);
				fixed16_16 dist = dy + distApprox(s_playerObject->posWS.x, s_playerObject->posWS.z, local(obj)->posWS.x, local(obj)->posWS.z);
				if (dist <= TURRET_MAX_DIST)
				{
					local(physicsActor)->state = TURRETSTATE_ATTACKING;
				}
			}
		}
		task_end;
	}

	void turretLocalMsgFunc(MessageType msg)
	{
		Turret* turret = (Turret*)task_getUserData();

		if (msg == MSG_DAMAGE)
		{
			turret_handleDamage(msg, turret);
		}
		else if (msg == MSG_EXPLOSION)
		{
			turret_handleExplosion(msg, turret);
		}
	}

	void turretTaskFunc(MessageType msg)
	{
		struct LocalContext
		{
			Turret* turret;
			PhysicsActor* physicsActor;
			SecObject* obj;
			ActorTarget* target;
		};
		task_begin_ctx;
		local(turret) = (Turret*)task_getUserData();
		local(physicsActor) = &local(turret)->actor;
		local(target) = &local(physicsActor)->moveMod.target;
		local(obj) = local(turret)->logic.obj;

		while (local(physicsActor)->alive)
		{
			if (local(physicsActor)->state == TURRETSTATE_FIRING)
			{
				s_curTurret = local(turret);
				turrent_handleFiringState(msg);
			}
			else if (local(physicsActor)->state == TURRETSTATE_ATTACKING)
			{
				s_curTurret = local(turret);
				task_callTaskFunc(turret_handleAttackingState);
			}
			else if (local(physicsActor)->state == TURRETSTATE_AIMING)
			{
				local(target)->yaw = local(turret)->yaw;
				local(target)->pitch = local(turret)->pitch;
				local(target)->flags = (local(target)->flags | TARGET_MOVE_ROT) & (~TARGET_MOVE_XZ);

				while (local(physicsActor)->state == TURRETSTATE_AIMING)
				{
					do
					{
						entity_yield(TASK_NO_DELAY);
						msg = turret_handleMessages(msg, local(turret));
					} while (msg != MSG_RUN_TASK);

					// Set to state 0 if the turret has reached its target alignment.
					if (local(physicsActor)->state == TURRETSTATE_AIMING && local(target)->yaw == local(obj)->yaw && local(target)->pitch == local(obj)->pitch)
					{
						local(physicsActor)->state = TURRETSTATE_DEFAULT;
					}
				}
			}
			else if (local(physicsActor)->state == TURRETSTATE_OUT_OF_CONTROL)
			{
				s_curTurret = local(turret);
				task_callTaskFunc(turret_handleHighlyDamagedState);
			}
			else if (local(physicsActor)->state == TURRETSTATE_DYING)
			{
				spawnHitEffect(HEFFECT_EXP_NO_DMG, local(obj)->sector, local(obj)->posWS, local(obj));
				entity_yield(TASK_NO_DELAY);
				local(physicsActor)->alive = JFALSE;
			}
			else
			{
				s_curTurret = local(turret);
				task_callTaskFunc(turret_handleDefaultState);
			}
		}

		// The Turret is dead.
		while (msg != MSG_RUN_TASK)
		{
			task_yield(TASK_NO_DELAY);
		}
		actor_removePhysicsActorFromWorld(local(physicsActor));
		deleteLogicAndObject(&local(turret)->logic);
		level_free(local(turret));

		task_end;
	}

	void turretCleanupFunc(Logic* logic)
	{
		Turret* turret = (Turret*)logic;

		Task* task = turret->actor.actorTask;
		actor_removePhysicsActorFromWorld(&turret->actor);
		deleteLogicAndObject(&turret->logic);
		level_free(turret);
		task_free(task);
	}

	void turret_serialize(Logic*& logic, SecObject* obj, Stream* stream)
	{
		Turret* turret = nullptr;
		bool write = serialization_getMode() == SMODE_WRITE;
		PhysicsActor* physicsActor = nullptr;
		Task* turretTask = nullptr;
		if (write)
		{
			turret = (Turret*)logic;
			physicsActor = &turret->actor;
		}
		else
		{
			turret = (Turret*)level_alloc(sizeof(Turret));
			memset(turret, 0, sizeof(Turret));
			physicsActor = &turret->actor;
			logic = (Logic*)turret;
						
			// Task
			char name[32];
			sprintf(name, "turret%d", s_turretNum);
			s_turretNum++;

			turretTask = createSubTask(name, turretTaskFunc, turretLocalMsgFunc);
			task_setUserData(turretTask, turret);

			// Logic
			logic->task = turretTask;
			logic->cleanupFunc = turretCleanupFunc;
			logic->type = LOGIC_TURRET;
			logic->obj = obj;
		}
		actor_serializeMovementModuleBase(stream, &physicsActor->moveMod);
		actor_serializeLogicAnim(stream, &physicsActor->anim);
		if (!write)
		{
			// Clear out functions, the mousebot handles all of this internally.
			physicsActor->moveMod.header.obj = obj;
			physicsActor->moveMod.header.func = nullptr;
			physicsActor->moveMod.header.freeFunc = nullptr;
			physicsActor->moveMod.header.attribFunc = nullptr;
			physicsActor->moveMod.header.msgFunc = nullptr;
			physicsActor->moveMod.header.type = ACTMOD_MOVE;
			physicsActor->moveMod.updateTargetFunc = nullptr;

			actor_addPhysicsActorToWorld(physicsActor);

			physicsActor->moveMod.header.obj = obj;
			physicsActor->moveMod.physics.obj = obj;
			physicsActor->actorTask = turretTask;
		}
		SERIALIZE(SaveVersionInit, physicsActor->vel, { 0 });
		SERIALIZE(SaveVersionInit, physicsActor->lastPlayerPos, { 0 });
		SERIALIZE(SaveVersionInit, physicsActor->alive, JTRUE);
		SERIALIZE(SaveVersionInit, physicsActor->hp, TURRET_HP);
		SERIALIZE(SaveVersionInit, physicsActor->state, TURRETSTATE_DEFAULT);

		SERIALIZE(SaveVersionInit, turret->pitch, 0);
		SERIALIZE(SaveVersionInit, turret->yaw, 0);
		SERIALIZE(SaveVersionInit, turret->nextTick, 0);
	}

	Logic* turret_setup(SecObject* obj, LogicSetupFunc* setupFunc)
	{
		Turret* turret = (Turret*)level_alloc(sizeof(Turret));
		memset(turret, 0, sizeof(Turret));

		// Give the name of the task a number so I can tell them apart when debugging.
		char name[32];
		sprintf(name, "turret%d", s_turretNum);
		s_turretNum++;
		Task* turretTask = createSubTask(name, turretTaskFunc, turretLocalMsgFunc);
		task_setUserData(turretTask, turret);

		obj->entityFlags = ETFLAG_AI_ACTOR;
		obj->worldWidth  = TURRET_WIDTH;
		obj->worldHeight = TURRET_HEIGHT;

		turret->actor.alive = JTRUE;
		turret->actor.hp    = TURRET_HP;
		turret->logic.obj   = obj;
		turret->actor.state = TURRETSTATE_DEFAULT;
		turret->actor.actorTask = turretTask;
		turret->yaw   = obj->yaw;
		turret->pitch = obj->pitch;

		PhysicsActor* physicsActor = &turret->actor;
		actor_addPhysicsActorToWorld(physicsActor);

		CollisionInfo* physics = &physicsActor->moveMod.physics;
		physics->obj    = obj;
		physics->width  = obj->worldWidth;
		physics->height = obj->worldHeight + HALF_16;
		physics->wall   = nullptr;
		physics->u24    = 0;
		physics->stepUpHeight = 0;
		physics->stepDownHeight = 0;

		MovementModule* moveMod = &physicsActor->moveMod;
		moveMod->header.obj = obj;
		moveMod->delta = { 0, 0, 0 };
		moveMod->collisionFlags &= ~ACTORCOL_ALL;

		ActorTarget* target = &physicsActor->moveMod.target;
		target->speed = 0;
		target->flags &= ~TARGET_ALL;
		target->speedRotation = TURRET_ROTATION_SPD;
		target->pitch = obj->pitch;
		target->yaw   = obj->yaw;
		target->roll  = obj->roll;
		obj_addLogic(obj, (Logic*)turret, LOGIC_TURRET, turretTask, turretCleanupFunc);
		
		if (setupFunc)
		{
			*setupFunc = nullptr;
		}
		return (Logic*)turret;
	}
}  // namespace TFE_DarkForces