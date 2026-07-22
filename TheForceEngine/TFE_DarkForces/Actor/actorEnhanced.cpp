#include <cstring>

#include "actorEnhanced.h"
#include "actor.h"
#include "actorInternal.h"
#include "animTables.h"
#include "flyers.h"
#include "../gameMusic.h"
#include <TFE_DarkForces/random.h>
#include <TFE_DarkForces/player.h>
#include <TFE_Game/igame.h>


namespace TFE_DarkForces
{
	///////////////////////////////////////////
	// Forward Declarations
	///////////////////////////////////////////
	Tick enhancedAttackFunc(ActorModule* module, MovementModule* moveMod);


	void actor_initBurstFireDefaults(AttackModule* attackMod, Logic* logic)
	{
		// new to TFE - burstfire
		attackMod->hasBurstFire = JFALSE;
		attackMod->burstFire.burstNumber = 5;
		attackMod->burstFire.variation = 2;
		attackMod->burstFire.interval = 29;
		attackMod->burstFire.shotCount = 5;
		attackMod->burstFire.lastShot = 0;
	}

	AttackModule* actor_createEnhancedAttackModule(ActorDispatch* dispatch)
	{
		AttackModule* attackMod = (AttackModule*)level_alloc(sizeof(AttackModule));
		memset(attackMod, 0, sizeof(AttackModule));

		actor_initAttackModule(attackMod, (Logic*)dispatch);
		actor_initBurstFireDefaults(attackMod, (Logic*)dispatch);
		attackMod->header.type = ACTMOD_ATTACK;
		attackMod->header.func = enhancedAttackFunc;
		attackMod->header.msgFunc = defaultAttackMsgFunc;
		return attackMod;
	}

	// This is the AttackFunc used by custom logics. It builds on the defaultAttackFunc
	Tick enhancedAttackFunc(ActorModule* module, MovementModule* moveMod)
	{
		AttackModule* attackMod = (AttackModule*)module;
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

							if (attackMod->hasBurstFire)
							{
								attackMod->anim.flags &= ~AFLAG_PLAYONCE;	// if logic has burst fire, allow attack anim to loop
							}
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

						if (attackMod->hasBurstFire)
						{
							attackMod->anim.flags &= ~AFLAG_PLAYONCE;	// if logic has burst fire, allow attack anim to loop
						}
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
					attackMod->target.yaw = vec2ToAngle(s_eyePos.x - obj->posWS.x, s_eyePos.z - obj->posWS.z);
					attackMod->target.pitch = obj->pitch;
					attackMod->target.roll = obj->roll;
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
			if (!(attackMod->anim.flags & AFLAG_READY) && (!attackMod->hasBurstFire || attackMod->attackFlags & ATTFLAG_MELEE))
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

			// Burst fire option - set via custom logics
			if (attackMod->hasBurstFire)
			{
				if (s_curTick < attackMod->burstFire.lastShot + attackMod->burstFire.interval)
				{
					break;
				}

				if (logic->flags & ACTOR_DYING) { break; }

				if (attackMod->burstFire.shotCount <= 1)
				{
					// Burst is finished, end the looping & reset the shot count
					attackMod->anim.state = STATE_ANIMATE1;
					attackMod->anim.flags |= AFLAG_PLAYONCE;

					s32 var = random(attackMod->burstFire.variation * 2);
					s32 nextBurstNumber = attackMod->burstFire.burstNumber - attackMod->burstFire.variation + var;
					attackMod->burstFire.shotCount = max(nextBurstNumber, 2);
				}
				else
				{
					// Reorient towards the player
					attackMod->target.yaw = vec2ToAngle(s_playerObject->posWS.x - obj->posWS.x, s_playerObject->posWS.z - obj->posWS.z);

					// Fire the next shot in the burst
					attackMod->burstFire.lastShot = s_curTick;
					attackMod->burstFire.shotCount--;
				}
			}
			else
			{
				// No burst fire -- vanilla logic
				attackMod->anim.state = STATE_ANIMATE1;
			}

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
			if (proj->updateFunc == arcingProjectileUpdateFunc)
			{
				// TDs are lobbed at an angle that depends on distance from target
				proj->bounceCnt = 0;
				proj->duration = 0xffffffff;
				vec3_fixed target = { s_playerObject->posWS.x, s_eyePos.y + ONE_16, s_playerObject->posWS.z };
				proj_aimArcing(proj, target, proj->speed);
			}
			else
			{
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
			if (!(attackMod->anim.flags & AFLAG_READY) && !attackMod->hasBurstFire)
			{
				break;
			}
			if (attackMod->attackFlags & ATTFLAG_LIT_RNG)
			{
				obj->flags |= OBJ_FLAG_FULLBRIGHT;
			}

			// Burst fire option - set via custom logics
			if (attackMod->hasBurstFire)
			{
				if (s_curTick < attackMod->burstFire.lastShot + attackMod->burstFire.interval)
				{
					break;
				}

				if (logic->flags & ACTOR_DYING) { break; }

				if (attackMod->burstFire.shotCount <= 1)
				{
					// Burst is finished, end the looping & reset the shot count
					attackMod->anim.state = STATE_ANIMATE2;
					attackMod->anim.flags |= AFLAG_PLAYONCE;

					s32 var = random(attackMod->burstFire.variation * 2);
					s32 nextBurstNumber = attackMod->burstFire.burstNumber - attackMod->burstFire.variation + var;
					attackMod->burstFire.shotCount = max(nextBurstNumber, 2);
				}
				else
				{
					// Reorient towards the player
					attackMod->target.yaw = vec2ToAngle(s_playerObject->posWS.x - obj->posWS.x, s_playerObject->posWS.z - obj->posWS.z);

					// Fire the next shot in the burst
					attackMod->burstFire.lastShot = s_curTick;
					attackMod->burstFire.shotCount--;
				}
			}
			else
			{
				// No burst fire -- vanilla logic
				attackMod->anim.state = STATE_ANIMATE2;
			}

			vec3_fixed fireOffset = {};

			// Calculate the fire offsets based on where the enemy is facing. It doesn't matter for Y. 
			transformFireOffsets(obj->yaw, &attackMod->fireOffset, &fireOffset);

			ProjectileLogic* proj = (ProjectileLogic*)createProjectile(attackMod->projType, obj->sector, fireOffset.x + obj->posWS.x, fireOffset.y + obj->posWS.y, fireOffset.z + obj->posWS.z, obj);
			sound_playCued(attackMod->attackPrimSndSrc, obj->posWS);
			proj->prevColObj = obj;
			proj->excludeObj = obj;

			SecObject* projObj = proj->logic.obj;
			projObj->yaw = obj->yaw;

			// The original test here was projType == PROJ_THERMAL_DET. In TFE we want to generalise it to all arcing projectiles.
			if (proj->updateFunc == arcingProjectileUpdateFunc)
			{
				proj->bounceCnt = 0;
				proj->duration = 0xffffffff;
				vec3_fixed target = { s_playerObject->posWS.x, s_eyePos.y + ONE_16, s_playerObject->posWS.z };
				proj_aimArcing(proj, target, proj->speed);
			}
			else
			{
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

	// SETUP CUSTOM LOGIC
	Logic* custom_actor_setup(SecObject* obj, TFE_ExternalData::CustomActorLogic* cust, LogicSetupFunc* setupFunc)
	{
		ActorDispatch* dispatch = actor_createDispatch(obj, setupFunc);
		dispatch->alertSndSrc = sound_load(cust->alertSound, SOUND_PRIORITY_MED5);
		dispatch->fov = floatToAngle((f32)cust->fov);
		dispatch->awareRange = FIXED(cust->awareRange);

		if (cust->officerAlerts)
		{
			dispatch->flags |= ACTOR_OFFIC_ALERT;
		}
		if (cust->troopAlerts)
		{
			dispatch->flags |= ACTOR_TROOP_ALERT;
		}

		// Damage Module
		DamageModule* damageMod = actor_createDamageModule(dispatch);
		damageMod->hp = FIXED(cust->hitPoints);
		damageMod->itemDropId = (ItemId)cust->dropItem;
		damageMod->dieEffect = (HitEffectID)cust->dieEffect;
		damageMod->hurtSndSrc = sound_load(cust->painSound, SOUND_PRIORITY_MED5);
		damageMod->dieSndSrc = sound_load(cust->dieSound, SOUND_PRIORITY_MED5);
		damageMod->stopOnHit = cust->stopOnDamage ? JTRUE : JFALSE;
		actor_addModule(dispatch, (ActorModule*)damageMod);

		// Attack Module
		AttackModule* attackMod = actor_createEnhancedAttackModule(dispatch);
		attackMod->attackFlags = 0;
		if (cust->hasMeleeAttack) { attackMod->attackFlags |= ATTFLAG_MELEE; }
		if (cust->hasRangedAttack) { attackMod->attackFlags |= ATTFLAG_RANGED; }
		if (cust->litWithMeleeAttack) { attackMod->attackFlags |= ATTFLAG_LIT_MELEE; }
		if (cust->litWithRangedAttack) { attackMod->attackFlags |= ATTFLAG_LIT_RNG; }
		attackMod->projType = (ProjectileType)cust->projectile;
		attackMod->attackPrimSndSrc = sound_load(cust->attack1Sound, SOUND_PRIORITY_LOW0);
		attackMod->attackSecSndSrc = sound_load(cust->attack2Sound, SOUND_PRIORITY_LOW0);
		attackMod->timing.rangedDelay = cust->rangedAttackDelay;
		attackMod->timing.meleeDelay = cust->meleeAttackDelay;
		attackMod->timing.losDelay = cust->losDelay;
		attackMod->maxDist = FIXED(cust->maxAttackDist);
		attackMod->meleeRange = FIXED(cust->meleeRange);
		attackMod->meleeDmg = FIXED(cust->meleeDamage);
		attackMod->meleeRate = FIXED(cust->meleeRate);
		attackMod->minDist = FIXED(cust->minAttackDist);
		attackMod->fireSpread = FIXED(cust->fireSpread);
		attackMod->fireOffset.x = floatToFixed16(cust->fireOffset.x);
		attackMod->fireOffset.y = cust->fireOffset.y < -999 ? attackMod->fireOffset.y : floatToFixed16(cust->fireOffset.y);		// if -1000 use the default value 
		attackMod->fireOffset.z = floatToFixed16(cust->fireOffset.z);

		attackMod->hasBurstFire = cust->hasBurstFire ? JTRUE : JFALSE;
		attackMod->burstFire.burstNumber = cust->burstNumber;
		attackMod->burstFire.shotCount = cust->burstNumber;
		attackMod->burstFire.variation = cust->burstVariation;
		attackMod->burstFire.interval = cust->burstInterval;

		s_actorState.attackMod = attackMod;
		actor_addModule(dispatch, (ActorModule*)attackMod);

		// Thinker Module
		ThinkerModule* thinkerMod = actor_createThinkerModule(dispatch);
		thinkerMod->target.speedRotation = floatToAngle((f32)cust->rotationSpeed);
		thinkerMod->target.speed = FIXED(cust->speed);
		thinkerMod->approachVariation = floatToAngle((f32)cust->approachVariation);
		thinkerMod->targetOffset = FIXED(cust->approachOffset);
		thinkerMod->startDelay = TICKS(cust->thinkerDelay);
		thinkerMod->anim.flags &= ~AFLAG_PLAYONCE;		// Ensures that walking animations will loop
		actor_addModule(dispatch, (ActorModule*)thinkerMod);

		// Flying Thinker Module (if flying enemy)
		if (cust->isFlying)
		{
			ThinkerModule* flyingMod = actor_createFlyingModule((Logic*)dispatch);
			flyingMod->target.speedRotation = floatToAngle((f32)cust->rotationSpeed);
			flyingMod->target.speed = FIXED(cust->speed);
			flyingMod->target.speedVert = FIXED(cust->verticalSpeed);
			actor_addModule(dispatch, (ActorModule*)flyingMod);
		}

		// Movement Module
		MovementModule* moveMod = actor_createMovementModule(dispatch);
		dispatch->moveMod = moveMod;
		moveMod->physics.width = cust->collisionWidth < 0 ? obj->worldWidth : floatToFixed16(cust->collisionWidth);
		moveMod->physics.height = cust->collisionHeight < 0 ? moveMod->physics.height : floatToFixed16(cust->collisionHeight);
		moveMod->physics.stepUpHeight = floatToFixed16(cust->stepUpHeight);
		moveMod->physics.stepDownHeight = floatToFixed16(cust->stepDownHeight);

		if (cust->isFlying)
		{
			moveMod->collisionFlags = (moveMod->collisionFlags & ~ACTORCOL_ALL) | ACTORCOL_SLIDE_RESPONSE;	// Remove bits 0, 1 and set bit 2
			moveMod->physics.stepDownHeight = FIXED(200);
		}
		else
		{
			moveMod->collisionFlags |= ACTORCOL_NO_Y_MOVE;
		}

		if (cust->slideOnCollision == 0)
		{
			moveMod->collisionFlags &= ~ACTORCOL_SLIDE_RESPONSE;
		}
		else if (cust->slideOnCollision == 1)
		{
			moveMod->collisionFlags |= ACTORCOL_SLIDE_RESPONSE;
		}

		dispatch->animTable = s_customAnimTable;
		actor_setupInitAnimation();

		return (Logic*)dispatch;
	}
}
