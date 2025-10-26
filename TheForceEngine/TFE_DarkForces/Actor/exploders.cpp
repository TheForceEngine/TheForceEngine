#include "exploders.h"
#include "../logic.h"
#include "actorModule.h"
#include "animTables.h"
#include <TFE_DarkForces/player.h>
#include <TFE_DarkForces/sound.h>
#include <TFE_Jedi/Memory/list.h>
#include <TFE_Jedi/Memory/allocator.h>

namespace TFE_DarkForces
{
	// Actor function for exploders (i.e. landmines and exploding barrels).
	Tick exploderFunc(ActorModule* module, MovementModule* moveMod)
	{
		DamageModule* damageMod = (DamageModule*)module;
		LogicAnimation* anim = &damageMod->attackMod.anim;
		if (!(anim->flags & AFLAG_READY))
		{
			s_actorState.curAnimation = anim;
			return 0;
		}
		else if ((anim->flags & AFLAG_PLAYONCE) && damageMod->hp <= 0)
		{
			actor_kill();
			return 0;
		}
		return 0xffffffff;
	}

	// Actor message function for exploders, this is responsible for processing messages such as 
	// projectile damage and explosions. For other AI message functions, it would also process
	// "wake up" messages, but those messages are ignored for exploders.
	Tick exploderMsgFunc(s32 msg, ActorModule* module, MovementModule* moveMod)
	{
		DamageModule* damageMod = (DamageModule*)module;
		Tick retValue = 0;
		SecObject* obj = damageMod->attackMod.header.obj;
		LogicAnimation* anim = &damageMod->attackMod.anim;

		if (msg == MSG_DAMAGE)
		{
			if (damageMod->hp > 0)
			{
				ProjectileLogic* proj = (ProjectileLogic*)s_msgEntity;
				damageMod->hp -= proj->dmg;
				Tick retValue;
				if (damageMod->hp > 0)
				{
					retValue = 0xffffffff;
				}
				else
				{
					ProjectileLogic* proj = (ProjectileLogic*)createProjectile(PROJ_EXP_BARREL, obj->sector, obj->posWS.x, obj->posWS.y, obj->posWS.z, obj);
					proj->vel = { 0, 0, 0 };
					obj->flags |= OBJ_FLAG_FULLBRIGHT;

					// I have to remove the logics here in order to get this to work, but this doesn't actually happen here in the original code.
					// TODO: Move to the correct location.
					actor_removeLogics(obj);

					actor_setupAnimation(ANIM_DIE1/*animIndex*/, anim);
					moveMod->updateTargetFunc(moveMod, &damageMod->attackMod.target);
					retValue = 0;
				}
			}
		}
		else if (msg == MSG_EXPLOSION)
		{
			if (damageMod->hp <= 0)
			{
				return 0xffffffff;
			}

			fixed16_16 dmg = s_msgArg1;
			fixed16_16 force = s_msgArg2;
			damageMod->hp -= dmg;

			vec3_fixed pushDir;
			vec3_fixed pos = { obj->posWS.x, obj->posWS.y - obj->worldHeight, obj->posWS.z };
			computeExplosionPushDir(&pos, &pushDir);

			fixed16_16 pushX = mul16(force, pushDir.x);
			fixed16_16 pushZ = mul16(force, pushDir.z);
			if (damageMod->hp > 0)
			{
				actor_addVelocity(pushX >> 1, 0, pushZ >> 1);
				return 0xffffffff;
			}

			obj->flags |= OBJ_FLAG_FULLBRIGHT;
			actor_addVelocity(pushX, 0, pushZ);

			ProjectileLogic* proj = (ProjectileLogic*)createProjectile(PROJ_EXP_BARREL, obj->sector, obj->posWS.x, obj->posWS.y, obj->posWS.z, obj);
			proj->vel = { 0, 0, 0 };

			ProjectileLogic* proj2 = (ProjectileLogic*)createProjectile(PROJ_EXP_BARREL, obj->sector, obj->posWS.x, obj->posWS.y, obj->posWS.z, obj);
			proj2->vel = { 0, 0, 0 };
			proj2->hitEffectId = HEFFECT_EXP_INVIS;
			proj2->duration = s_curTick + 36;

			ActorDispatch* actorLogic = (ActorDispatch*)s_actorState.curLogic;
			CollisionInfo colInfo = { 0 };
			colInfo.obj = proj2->logic.obj;
			colInfo.offsetY = 0;
			colInfo.offsetX = mul16(actorLogic->vel.x, 0x4000);
			colInfo.offsetZ = mul16(actorLogic->vel.z, 0x4000);
			colInfo.stepUpHeight = ONE_16;
			colInfo.stepDownHeight = COL_INFINITY;
			colInfo.height = ONE_16;
			colInfo.unused = 0;
			colInfo.width = colInfo.obj->worldWidth;
			handleCollision(&colInfo);

			// I have to remove the logics here in order to get this to work, but this doesn't actually happen here in the original code.
			// TODO: Move to the correct location.
			actor_removeLogics(obj);
			actor_setupAnimation(ANIM_DIE1/*animIndex*/, anim);
			retValue = 0;
		}
		return retValue;
	}

	Logic* barrel_setup(SecObject* obj, LogicSetupFunc* setupFunc)
	{
		ActorDispatch* dispatch = actor_createDispatch(obj, setupFunc);

		dispatch->flags &= ~(ACTOR_IDLE | ACTOR_NPC);
		dispatch->animTable = s_mineBarrelAnimTable;

		DamageModule* module = actor_createDamageModule(dispatch);
		module->attackMod.header.func = exploderFunc;
		module->attackMod.header.msgFunc = exploderMsgFunc;
		module->hp = FIXED(11);
		module->attackMod.anim.flags |= AFLAG_READY;
		actor_addModule(dispatch, (ActorModule*)module);

		MovementModule* moveMod = actor_createMovementModule(dispatch);
		dispatch->moveMod = moveMod;
		moveMod->collisionFlags |= ACTORCOL_NO_Y_MOVE;
		moveMod->physics.width = obj->worldWidth;
		moveMod->target.flags = (moveMod->target.flags & ~TARGET_ALL_MOVE) | TARGET_FREEZE;
		moveMod->target.speed = 0;
		moveMod->target.speedRotation = 0;

		ActorTarget* target = &module->attackMod.target;
		target->flags = (target->flags & ~TARGET_ALL_MOVE) | TARGET_FREEZE;
		target->speed = 0;
		target->speedRotation = 0;

		return (Logic*)dispatch;
	}

	Logic* landmine_setup(SecObject* obj, LogicSetupFunc* setupFunc)
	{
		ActorDispatch* dispatch = actor_createDispatch(obj, setupFunc);
		dispatch->flags &= ~ACTOR_NPC;
		dispatch->flags &= ~ACTOR_IDLE;
		dispatch->animTable = s_mineBarrelAnimTable;

		DamageModule* module = actor_createDamageModule(dispatch);
		module->attackMod.header.func = exploderFunc;
		module->attackMod.header.msgFunc = exploderMsgFunc;
		module->attackMod.anim.flags |= AFLAG_READY;
		module->hp = FIXED(20);
		actor_addModule(dispatch, (ActorModule*)module);

		MovementModule* moveMod = actor_createMovementModule(dispatch);
		dispatch->moveMod = moveMod;
		moveMod->collisionFlags |= ACTORCOL_NO_Y_MOVE;
		// This was cleared to 0 in createProjectile()
		moveMod->physics.width = obj->worldWidth;

		ActorTarget* target = &module->attackMod.target;
		target->flags = (target->flags | TARGET_FREEZE) & ~TARGET_ALL_MOVE;
		target->speed = 0;
		target->speedRotation = 0;

		moveMod->target.flags = TARGET_FREEZE;
		moveMod->target.speed = 0;
		moveMod->target.speedRotation = 0;

		dispatch->flags &= ~ACTOR_IDLE;
		dispatch->animTable = s_mineBarrelAnimTable;

		return (Logic*)dispatch;
	}
}  // namespace TFE_DarkForces