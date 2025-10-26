#include "sewer.h"
#include "actorModule.h"
#include "animTables.h"
#include "../logic.h"
#include "../gameMusic.h"
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

namespace TFE_DarkForces
{
	u32 sewerCreatureDie(DamageModule* module, MovementModule* moveMod);
		
	// Damage module function for sewer creature
	Tick sewerCreatureDamageFunc(ActorModule* module, MovementModule* moveMod)
	{
		DamageModule* damageMod = (DamageModule*)module;
		AttackModule* attackMod = &damageMod->attackMod;
		SecObject* obj = attackMod->header.obj;
		RSector* sector = obj->sector;
		LogicAnimation* anim = &attackMod->anim;

		if (!(anim->flags & AFLAG_READY))
		{
			if (obj->type == OBJ_TYPE_SPRITE)
			{
				actor_setCurAnimation(&attackMod->anim);
			}
			moveMod->updateTargetFunc(moveMod, &attackMod->target);
			return 0;
		}
		else if (damageMod->hp > 0)
		{
			return 0xffffffff;
		}

		// Creature die.
		s32 animIndex = actor_getAnimationIndex(ANIM_DEAD);
		if (animIndex != -1)
		{
			SecObject* corpse = allocateObject();
			sprite_setData(corpse, obj->wax);
			corpse->frame = 0;
			corpse->anim = animIndex;
			corpse->posWS.x = obj->posWS.x;
			corpse->posWS.z = obj->posWS.z;
			corpse->posWS.y = sector->floorHeight;
			corpse->entityFlags |= ETFLAG_CORPSE;
			corpse->worldWidth = 0;
			sector_addObject(sector, corpse);

			obj_addToRefList(corpse, ObjRefType_Corpse);	// scripting
		}
		actor_kill();
		return 0;
	}
	
	// Damage module message function for sewer creature
	Tick sewerCreatureDamageMsgFunc(s32 msg, ActorModule* module, MovementModule* moveMod)
	{
		DamageModule* damageMod = (DamageModule*)module;
		AttackModule* attackMod = &damageMod->attackMod;
		SecObject* obj = attackMod->header.obj;
		RSector* sector = obj->sector;
		LogicAnimation* anim = &attackMod->anim;

		if (msg == MSG_DAMAGE)
		{
			if (damageMod->hp <= 0)
			{
				if (obj->type == OBJ_TYPE_SPRITE)
				{
					actor_setCurAnimation(&attackMod->anim);
				}
				moveMod->updateTargetFunc(moveMod, &attackMod->target);
				return 0;
			}

			ProjectileLogic* proj = (ProjectileLogic*)s_msgEntity;
			damageMod->hp -= proj->dmg;
			if (damageMod->hp <= 0)
			{
				return sewerCreatureDie(damageMod, moveMod);
			}

			sound_stop(damageMod->hurtSndID);
			damageMod->hurtSndID = sound_playCued(damageMod->hurtSndSrc, obj->posWS);
			return 0xffffffff;
		}
		else if (msg == MSG_EXPLOSION)
		{
			if (damageMod->hp <= 0)
			{
				if (obj->type == OBJ_TYPE_SPRITE)
				{
					actor_setCurAnimation(&attackMod->anim);
				}
				moveMod->updateTargetFunc(moveMod, &attackMod->target);
				return 0;
			}

			fixed16_16 dmg = s_msgArg1;
			damageMod->hp -= dmg;
			if (damageMod->hp > 0)
			{
				sound_stop(damageMod->hurtSndID);
				damageMod->hurtSndID = sound_playCued(damageMod->hurtSndSrc, obj->posWS);
				return 0xffffffff;
			}
			return sewerCreatureDie(damageMod, moveMod);
		}
		return attackMod->header.nextTick;
	}

	// Attack module function for sewer creature
	// The return value is set to the module's nextTick
	Tick sewerCreatureEnemyFunc(ActorModule* module, MovementModule* moveMod)
	{
		DamageModule* damageMod = (DamageModule*)module;
		AttackModule* attackMod = &damageMod->attackMod;
		SecObject* obj = attackMod->header.obj;
		RSector* sector = obj->sector;

		switch (attackMod->anim.state)
		{
			case STATE_DELAY:
			{
				if (attackMod->anim.flags & AFLAG_READY)
				{
					obj->flags &= ~OBJ_FLAG_NEEDS_TRANSFORM;
					obj->worldWidth = 0;
					obj->posWS.y = sector->floorHeight + sector->secHeight;

					moveMod->collisionFlags |= ACTORCOL_NO_Y_MOVE;
					attackMod->target.flags &= ~TARGET_ALL;
					attackMod->anim.state = STATE_DECIDE;
					return s_curTick + random(attackMod->timing.delay);
				}
			} break;
			case STATE_DECIDE:
			{
				gameMusic_sustainFight();
				if (!actor_canSeeObjFromDist(obj, s_playerObject))
				{
					actor_updatePlayerVisiblity(JFALSE, 0, 0);
					attackMod->anim.flags |= AFLAG_READY;
					attackMod->anim.state = STATE_DELAY;
					if (s_curTick > attackMod->timing.nextTick)
					{
						attackMod->timing.delay = attackMod->timing.searchDelay;
						actor_setupInitAnimation();
					}
					return attackMod->timing.delay;
				}

				actor_updatePlayerVisiblity(JTRUE, s_eyePos.x, s_eyePos.z);
				moveMod->collisionFlags &= ~ACTORCOL_NO_Y_MOVE;

				obj->posWS.y = sector->floorHeight;
				fixed16_16 dy = TFE_Jedi::abs(obj->posWS.y - s_playerObject->posWS.y);
				fixed16_16 dist = dy + distApprox(s_playerObject->posWS.x, s_playerObject->posWS.z, obj->posWS.x, obj->posWS.z);
				if (dist <= attackMod->meleeRange)
				{
					attackMod->anim.state = STATE_ATTACK1;
					attackMod->timing.delay = attackMod->timing.meleeDelay;
					attackMod->target.pos.x = obj->posWS.x;
					attackMod->target.pos.z = obj->posWS.z;
					attackMod->target.flags |= TARGET_MOVE_XZ;

					fixed16_16 dx = s_eyePos.x - obj->posWS.x;
					fixed16_16 dz = s_eyePos.z - obj->posWS.z;
					attackMod->target.yaw   = vec2ToAngle(dx, dz);
					attackMod->target.pitch = obj->pitch;
					attackMod->target.roll  = obj->roll;
					attackMod->target.flags |= TARGET_MOVE_ROT;
				}
				else
				{
					attackMod->anim.state = STATE_DELAY;
					attackMod->timing.delay = attackMod->timing.searchDelay;
				}

				attackMod->timing.nextTick = s_curTick + attackMod->timing.losDelay;
				attackMod->target.flags |= TARGET_FREEZE;
				obj->worldWidth = FIXED(3);
				obj->flags |= OBJ_FLAG_NEEDS_TRANSFORM;
				if (obj->type == OBJ_TYPE_SPRITE)
				{
					if (attackMod->anim.state == STATE_ATTACK1)  // Attack animation
					{
						actor_setupAnimation(ANIM_ATTACK1, &attackMod->anim);
					}
					else // Look around animation
					{
						actor_setupAnimation(ANIM_SEARCH, &attackMod->anim);
					}
				}
			} break;
			case STATE_ATTACK1:
			{
				if (attackMod->anim.flags & AFLAG_READY)
				{
					attackMod->anim.state = STATE_ANIMATE1;
					sound_playCued(attackMod->attackSecSndSrc, obj->posWS);

					fixed16_16 dy = TFE_Jedi::abs(obj->posWS.y - s_playerObject->posWS.y);
					fixed16_16 dist = dy + distApprox(s_playerObject->posWS.x, s_playerObject->posWS.z, obj->posWS.x, obj->posWS.z);
					if (dist <= attackMod->meleeRange)
					{
						player_applyDamage(attackMod->meleeDmg, 0, JTRUE);
					}
				}
			} break;
			case STATE_ANIMATE1:
			{
				actor_setupAnimation(ANIM_ATTACK1_END, &attackMod->anim);
				attackMod->anim.state = STATE_DELAY;
			} break;
		}

		actor_setCurAnimation(&attackMod->anim);
		moveMod->updateTargetFunc(moveMod, &attackMod->target);
		return attackMod->timing.delay;
	}

	Logic* sewerCreature_setup(SecObject* obj, LogicSetupFunc* setupFunc)
	{
		obj->flags &= ~OBJ_FLAG_NEEDS_TRANSFORM;
		obj->entityFlags = ETFLAG_AI_ACTOR;

		ActorDispatch* dispatch = actor_createDispatch(obj, setupFunc);
		dispatch->alertSndSrc = s_alertSndSrc[ALERT_CREATURE];
		dispatch->fov = ANGLE_MAX;

		DamageModule* module = actor_createDamageModule(dispatch);
		module->attackMod.header.func = sewerCreatureDamageFunc;
		module->attackMod.header.msgFunc = sewerCreatureDamageMsgFunc;
		module->hp = FIXED(36);
		module->hurtSndSrc = s_agentSndSrc[AGENTSND_CREATURE_HURT];
		module->dieSndSrc = s_agentSndSrc[AGENTSND_CREATURE_DIE];
		actor_addModule(dispatch, (ActorModule*)module);

		AttackModule* attackMod = actor_createAttackModule(dispatch);
		s_actorState.attackMod = attackMod;
		attackMod->header.func = sewerCreatureEnemyFunc;
		attackMod->timing.searchDelay = 1165;
		attackMod->timing.meleeDelay = 1165;
		attackMod->meleeRange = FIXED(13);
		attackMod->meleeDmg = FIXED(20);
		attackMod->meleeRate = FIXED(360);
		attackMod->attackSecSndSrc = s_agentSndSrc[AGENTSND_CREATURE2];
		FLAGS_CLEAR_SET(attackMod->attackFlags, ATTFLAG_RANGED, ATTFLAG_MELEE);
		actor_addModule(dispatch, (ActorModule*)attackMod);

		ThinkerModule* thinkerMod = actor_createThinkerModule(dispatch);
		thinkerMod->target.speedRotation = 0x7fff;
		thinkerMod->target.speed = FIXED(18);
		thinkerMod->delay = 58;
		thinkerMod->startDelay = 72;
		thinkerMod->anim.flags &= ~AFLAG_PLAYONCE;
		actor_addModule(dispatch, (ActorModule*)thinkerMod);

		MovementModule* moveMod = actor_createMovementModule(dispatch);
		dispatch->moveMod = moveMod;
		dispatch->animTable = s_sewerCreatureAnimTable;
		obj->entityFlags &= ~ETFLAG_SMART_OBJ;

		moveMod->collisionFlags = (moveMod->collisionFlags | ACTORCOL_NO_Y_MOVE) & ~ACTORCOL_GRAVITY;	// gravity is removed so they remain on the surface of water (floor height) rather than sinking down (second height)
		moveMod->physics.stepDownHeight = 0;
		moveMod->physics.stepUpHeight = 0;
		moveMod->physics.width = obj->worldWidth;
		actor_setupInitAnimation();

		return (Logic*)dispatch;
	}

	u32 sewerCreatureDie(DamageModule* module, MovementModule* moveMod)
	{
		AttackModule* attackMod = &module->attackMod;
		SecObject* obj = attackMod->header.obj;
		RSector* sector = obj->sector;

		actor_setDeathCollisionFlags();
		ActorDispatch* logic = (ActorDispatch*)s_actorState.curLogic;
		sound_stop(logic->alertSndID);
		sound_playCued(module->dieSndSrc, obj->posWS);
		attackMod->target.flags |= TARGET_FREEZE;

		if ((obj->anim == ANIM_ATTACK1 || obj->anim == ANIM_ATTACK1_END) && obj->type == OBJ_TYPE_SPRITE)
		{
			actor_setupAnimation(ANIM_DIE1, &attackMod->anim);
		}
		else if (obj->type == OBJ_TYPE_SPRITE)
		{
			actor_setupAnimation(ANIM_DIE2, &attackMod->anim);
		}

		obj->posWS.y = sector->floorHeight;
		moveMod->collisionFlags &= ~ACTORCOL_GRAVITY;

		if (obj->type == OBJ_TYPE_SPRITE)
		{
			actor_setCurAnimation(&attackMod->anim);
		}
		moveMod->updateTargetFunc(moveMod, &attackMod->target);
		return 0;
	}
}  // namespace TFE_DarkForces