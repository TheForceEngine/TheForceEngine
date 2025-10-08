#include <cstring>

#include "bobaFett.h"
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
#include <TFE_Jedi/Level/rwall.h>
#include <TFE_Jedi/Memory/list.h>
#include <TFE_Jedi/Memory/allocator.h>
#include <TFE_Jedi/Serialization/serialization.h>
#include <TFE_Settings/settings.h>

namespace TFE_DarkForces
{
	enum BobaFettStates
	{
		BOBASTATE_DEFAULT         = 0,
		BOBASTATE_MOVE_AND_ATTACK = 1,
		BOBASTATE_DYING           = 2,
		BOBASTATE_SEARCH          = 3,
		BOBASTATE_COUNT
	};

	enum AttackPhase : u32
	{
		ATTACK_POINT     = 0,
		ATTACK_CIRCLE    = 1,
		ATTACK_FIGURE8   = 2,
		ATTACK_COUNT     = 3,
		ATTACK_NEWMODE   = 4,
		ATTACK_POINT1    = 5,
		ATTACK_CIRCLE1   = 6,
		ATTACK_FIGURE8_1 = 7,
	};

	enum SearchPhase : u32
	{
		SEARCH_FIND = 0,
		SEARCH_RAND,
		SEARCH_CONTINUE
	};

	enum Timing : u32
	{
		SHOOT_DELAY_RANGE_VANILLA = 291,
		SHOOT_DELAY_RANGE_ENH = 291,
		ATTACK_POINT_DURATION = 728,    // 5 seconds
		ATTACK_CIRCLE_DURATION = 1456,  // 10 seconds
		ATTACK_FIGURE8_DURATION = 2184, // 15 seconds
		FIGURE8_INTERVAL = 291,         // 2 seconds
		CHANGE_HEIGHT_INTERVAL = 728,   // 5 seconds
		SEARCH_DURATION = 8739,         // 60 seconds
		SEARCH_PHASE_INTERVAL = 1456,   // 10 seconds
	};

	struct BobaFettMoveState
	{
		vec3_fixed target;
		fixed16_16 thrustFactor;
		SoundSourceId soundSrc;
		angle14_32 thrustPitchRange;
		angle14_32 thrustPitch;
		fixed16_16 thrustScale;
		fixed16_16 lateralAccel;
	};

	struct BobaFett
	{
		Logic logic;
		PhysicsActor actor;

		s32 unused;
		BobaFettMoveState moveState;
		SoundEffectId hitSndId;
	};

	struct BobaFettShared
	{
		SoundSourceId boba1SndID = NULL_SOUND;
		SoundSourceId boba3SndID = NULL_SOUND;
		SoundSourceId boba2SndID = NULL_SOUND;
		SoundSourceId boba4SndID = NULL_SOUND;
		SoundSourceId bobaRocket2SndID = NULL_SOUND;
		s32 bobaFettNum = 0;
	};

	static BobaFett* s_curBobaFett = nullptr;
	static BobaFettShared s_shared = {};
	extern s32 s_lastMaintainVolume;

	void bobaFett_exit()
	{
		s_shared = {};
	}

	void bobaFett_precache()
	{
		s_shared.boba1SndID = sound_load("boba-1.voc", SOUND_PRIORITY_MED5);
		s_shared.boba3SndID = sound_load("boba-3.voc", SOUND_PRIORITY_LOW0);
		s_shared.boba2SndID = sound_load("boba-2.voc", SOUND_PRIORITY_LOW0);
		s_shared.boba4SndID = sound_load("boba-4.voc", SOUND_PRIORITY_MED5);
		s_shared.bobaRocket2SndID = sound_load("rocket-2.voc", SOUND_PRIORITY_LOW5);
	}

	void bobaFett_handleDamage(MessageType msg)
	{
		struct LocalContext
		{
			BobaFett* bobaFett;
			SecObject* obj;
			PhysicsActor* physicsActor;
			LogicAnimation* anim;
			LogicAnimation tmp;
		};
		task_begin_ctx;

		local(bobaFett) = s_curBobaFett;
		local(obj) = local(bobaFett)->logic.obj;
		local(physicsActor) = &local(bobaFett)->actor;
		local(anim) = &local(physicsActor)->anim;

		if (local(physicsActor)->alive)
		{
			task_localBlockBegin;
			ProjectileLogic* proj = (ProjectileLogic*)s_msgEntity;
			local(physicsActor)->hp -= proj->dmg;
			task_localBlockEnd;

			if (local(physicsActor)->hp <= 0)
			{
				local(physicsActor)->state = BOBASTATE_DYING;
				msg = MSG_RUN_TASK;
				task_setMessage(msg);
			}
			else
			{
				sound_stop(local(bobaFett)->hitSndId);
				local(bobaFett)->hitSndId = sound_playCued(s_shared.boba3SndID, local(obj)->posWS);

				// Save Animation
				memcpy(&local(tmp), local(anim), sizeof(LogicAnimation) - 4);

				local(anim)->flags |= AFLAG_PLAYONCE;
				local(anim)->frameRate = 6;
				actor_setupBossAnimation(local(obj), 12, local(anim));

				// Wait for animation to finish.
				do
				{
					entity_yield(TASK_NO_DELAY);
				} while (msg != MSG_RUN_TASK || !(local(anim)->flags & AFLAG_READY));

				memcpy(local(anim), &local(tmp), sizeof(LogicAnimation) - 4);
				actor_setupBossAnimation(local(obj), local(anim)->animId, local(anim));

				msg = MSG_DAMAGE;
				task_setMessage(msg);
			}
		}  // if (alive)
		task_end;
	}

	void bobaFett_handleExplosion(MessageType msg)
	{
		struct LocalContext
		{
			BobaFett* bobaFett;
			SecObject* obj;
			PhysicsActor* physicsActor;
			LogicAnimation* anim;
			LogicAnimation tmp;
		};
		task_begin_ctx;

		local(bobaFett) = s_curBobaFett;
		local(obj) = local(bobaFett)->logic.obj;
		local(physicsActor) = &local(bobaFett)->actor;
		local(anim) = &local(physicsActor)->anim;

		if (local(physicsActor)->alive)
		{
			task_localBlockBegin;
				fixed16_16 dmg = s_msgArg1;
				local(physicsActor)->hp -= dmg;
			task_localBlockEnd;

			if (local(physicsActor)->hp <= 0)
			{
				local(physicsActor)->state = BOBASTATE_DYING;
				msg = MSG_RUN_TASK;
				task_setMessage(msg);
			}
			else
			{
				sound_stop(local(bobaFett)->hitSndId);
				local(bobaFett)->hitSndId = sound_playCued(s_shared.boba3SndID, local(obj)->posWS);

				// Save Animation
				memcpy(&local(tmp), local(anim), sizeof(LogicAnimation) - 4);

				local(anim)->flags |= AFLAG_PLAYONCE;
				local(anim)->frameRate = 6;
				actor_setupBossAnimation(local(obj), 12, local(anim));

				// Wait for animation to finish.
				do
				{
					entity_yield(TASK_NO_DELAY);
				} while (msg != MSG_RUN_TASK || !(local(anim)->flags & AFLAG_READY));

				memcpy(local(anim), &local(tmp), sizeof(LogicAnimation) - 4);
				actor_setupBossAnimation(local(obj), local(anim)->animId, local(anim));

				msg = MSG_EXPLOSION;
				task_setMessage(msg);
			}
		}  // if (alive)
		task_end;
	}

	void bobaFett_handleMsg(MessageType msg)
	{
		struct LocalContext
		{
			BobaFett* bobaFett;
		};
		task_begin_ctx;
		local(bobaFett) = s_curBobaFett;
		
		if (msg == MSG_DAMAGE)
		{
			s_curBobaFett = local(bobaFett);
			task_callTaskFunc(bobaFett_handleDamage);
		}
		else if (msg == MSG_EXPLOSION)
		{
			s_curBobaFett = local(bobaFett);
			task_callTaskFunc(bobaFett_handleExplosion);
		}
		task_end;
	}

	fixed16_16 bobaFett_thrust(fixed16_16 yTarget, fixed16_16 yPos, fixed16_16 yVel, fixed16_16 curThrust, angle14_32* thrustPitchRange)
	{
		fixed16_16 target = -yTarget;
		fixed16_16 pos = -yPos;
		fixed16_16 vel = -yVel;
		if (pos != target)
		{
			vel += mul16(s_gravityAccel, s_deltaTime);
			fixed16_16 dy = target - pos;
			if (vel != dy)
			{
				fixed16_16 targetThrust = max(0, dy - vel);
				*thrustPitchRange = clamp(ONE_16 - (vel - 0x13333), 0, ONE_16);
				
				s32 maxDelta = mul16(TFE_Jedi::abs(dy) + FIXED(4), s_deltaTime);
				fixed16_16 deltaThrust = min(ONE_16, targetThrust) - curThrust;
				fixed16_16 newThrust = clamp(deltaThrust, -maxDelta, maxDelta) + curThrust;
				return newThrust;
			}
		}
		return 0;
	}

	void bobaFett_handleForces(PhysicsActor* physicsActor, fixed16_16 thrustScale, angle14_32 thrustPitch, fixed16_16 thrustFactor, SoundSourceId soundSrc)
	{
		SecObject* obj = physicsActor->moveMod.header.obj;
		if (thrustScale)
		{
			// Set the sound effect volume
			physicsActor->moveSndId = sound_maintain(physicsActor->moveSndId, soundSrc, obj->posWS);
			if (s_lastMaintainVolume)
			{
				s32 vol = floor16(mul16(mul16(thrustScale, thrustScale), intToFixed16(s_lastMaintainVolume)));
				sound_setVolume(physicsActor->moveSndId, vol);
			}

			const fixed16_16 thrust = mul16(mul16(thrustScale, thrustFactor), s_deltaTime);

			// Vertical thrust.
			fixed16_16 sinPitch, cosPitch;
			sinCosFixed(thrustPitch, &sinPitch, &cosPitch);
			fixed16_16 yThrust = mul16(cosPitch, thrust);
			fixed16_16 hThrust = mul16(sinPitch, thrust);

			// Horizontal thrust.
			fixed16_16 sinYaw, cosYaw;
			sinCosFixed(obj->yaw, &sinYaw, &cosYaw);
			fixed16_16 xThrust = mul16(hThrust, sinYaw);
			fixed16_16 zThrust = mul16(hThrust, cosYaw);

			// Velocity adjustment.
			physicsActor->vel.x += xThrust;
			physicsActor->vel.y -= yThrust;
			physicsActor->vel.z += zThrust;
		}
		else if (physicsActor->moveSndId)
		{
			sound_stop(physicsActor->moveSndId);
			physicsActor->moveSndId = NULL_SOUND;
		}
	}

	void bobaFett_handleFlying(PhysicsActor* physicsActor, BobaFettMoveState* moveState)
	{
		SecObject* obj = physicsActor->moveMod.header.obj;

		fixed16_16 dx = moveState->target.x - obj->posWS.x;
		fixed16_16 dz = moveState->target.z - obj->posWS.z;
		angle14_32 targetAngle = vec2ToAngle(dx, dz);
		angle14_32 velAngle = vec2ToAngle(physicsActor->vel.x, physicsActor->vel.z);

		angle14_32 adjustedTargetAngle = targetAngle - velAngle;
		if (adjustedTargetAngle != 0)
		{
			angle14_32 maxFrameRotation = floor16(mul16(FIXED(4551), s_deltaTime));
			angle14_32 angleDelta = clamp(getAngleDifference(obj->yaw, targetAngle), -maxFrameRotation, maxFrameRotation);
			obj->yaw = (obj->yaw + angleDelta) & ANGLE_MASK;
		}

		// Adjust the thrust pitch.
		fixed16_16 dist = distApprox(obj->posWS.x, obj->posWS.z, moveState->target.x, moveState->target.z);
		fixed16_16 scaledDist = min(ONE_16, dist >> 7);
		fixed16_16 newThrustPitch = mul16(scaledDist, FIXED(100));

		fixed16_16 cosVelAngle, sinVelAngle;
		sinCosFixed(velAngle, &cosVelAngle, &sinVelAngle);

		newThrustPitch -= (mul16(physicsActor->vel.z, cosVelAngle) + mul16(physicsActor->vel.x, sinVelAngle));
		newThrustPitch = clamp(div16(newThrustPitch, FIXED(100)), -moveState->thrustPitchRange, moveState->thrustPitchRange);

		angle14_32 targetThrustPitch = floor16(mul16(newThrustPitch, FIXED(2048))); // 2048 = 45 degrees
		angle14_32 maxThrustPitchDelta = floor16(mul16(FIXED(1365), s_deltaTime)); // 1365 = 30 degrees
		angle14_32 thrustPitchDelta    = clamp(targetThrustPitch - moveState->thrustPitch, -maxThrustPitchDelta, maxThrustPitchDelta);
		moveState->thrustPitch += thrustPitchDelta;

		// Change the thrust scale and thrust pitch range.
		moveState->thrustScale = bobaFett_thrust(moveState->target.y, obj->posWS.y, physicsActor->vel.y, moveState->thrustScale, &moveState->thrustPitchRange);
		
		// Apply lateral acceleration
		if (moveState->lateralAccel)
		{
			fixed16_16 sinYaw, cosYaw;
			sinCosFixed(obj->yaw + 4095, &sinYaw, &cosYaw);  // 4095 = 90 degrees

			fixed16_16 velDelta = mul16(moveState->lateralAccel, s_deltaTime);
			physicsActor->vel.x += mul16(velDelta, sinYaw);
			physicsActor->vel.z += mul16(velDelta, cosYaw);
		}

		// Handle velocity adjustments based on forces.
		bobaFett_handleForces(physicsActor, moveState->thrustScale, moveState->thrustPitch, moveState->thrustFactor, moveState->soundSrc);
	}

	void bobaFett_handleMovement(BobaFett* bobaFett)
	{
		PhysicsActor* physicsActor = &bobaFett->actor;
		SecObject* obj = bobaFett->logic.obj;
		RSector* sector = obj->sector;
		fixed16_16 targetHeight = sector->ceilingHeight + obj->worldHeight;
		fixed16_16 minHeight = sector->floorHeight;
		RWall* wall = collision_wallCollisionFromPath(sector, obj->posWS.x, obj->posWS.z, bobaFett->moveState.target.x, bobaFett->moveState.target.z);
		if (wall)
		{
			RSector* nextSector = wall->nextSector;
			if (nextSector)
			{
				targetHeight = max(targetHeight, nextSector->ceilingHeight + obj->worldHeight);
				minHeight = min(minHeight, nextSector->floorHeight);
			}
		}

		if (targetHeight < bobaFett->moveState.target.y)
		{
			targetHeight = bobaFett->moveState.target.y;
		}
		if (targetHeight >= minHeight)
		{
			targetHeight = minHeight;
		}
		bobaFett->moveState.target.y = targetHeight;

		bobaFett_handleFlying(physicsActor, &bobaFett->moveState);
	}

	void bobaFett_handleMoveAndAttack(MessageType msg)
	{
		struct LocalContext
		{
			BobaFett* bobaFett;
			SecObject* obj;
			PhysicsActor* physicsActor;
			ActorTarget* target;
			LogicAnimation* anim;
			BobaFettMoveState* moveState;
			
			AttackPhase phase;
			Tick nextAimedShotTick;
			Tick nextShootTick;
			Tick nextChangeHeightTick;
			Tick nextPhaseChangeTick;
			Tick nextSwapAccelTick;
			fixed16_16 lateralAccel;
			fixed16_16 heightTarget;
		};
		task_begin_ctx;

		local(bobaFett) = s_curBobaFett;
		local(obj) = local(bobaFett)->logic.obj;
		local(physicsActor) = &local(bobaFett)->actor;
		local(target) = &local(physicsActor)->moveMod.target;
		local(anim) = &local(physicsActor)->anim;
		local(moveState) = &local(bobaFett)->moveState;
		
		local(phase) = ATTACK_NEWMODE;
		local(nextAimedShotTick) = s_curTick +
			floor16(random(FIXED(TFE_Settings::getGameSettings()->df_bobaFettFacePlayer ? SHOOT_DELAY_RANGE_ENH : SHOOT_DELAY_RANGE_VANILLA)));
		local(nextChangeHeightTick) = 0;
		local(nextShootTick) = 0;
		local(nextPhaseChangeTick) = 0;
		local(nextSwapAccelTick) = 0;
		local(heightTarget) = 0;
		local(lateralAccel) = 0;

		while (local(physicsActor)->state == BOBASTATE_MOVE_AND_ATTACK)
		{
			do
			{
				entity_yield(TASK_NO_DELAY);
				s_curBobaFett = local(bobaFett);
				task_callTaskFunc(bobaFett_handleMsg);
			} while (msg != MSG_RUN_TASK);
			if (local(physicsActor)->state != BOBASTATE_MOVE_AND_ATTACK) { break; }
			
			task_localBlockBegin;
				fixed16_16 dx = s_playerObject->posWS.x - local(obj)->posWS.x;
				fixed16_16 dz = s_playerObject->posWS.z - local(obj)->posWS.z;
				angle14_32 angle = vec2ToAngle(dx, dz) & ANGLE_MASK;

				if (local(phase) == ATTACK_POINT || local(phase) == ATTACK_POINT1)
				{
					local(target)->yaw = angle;
					local(target)->flags |= TARGET_MOVE_ROT;

					fixed16_16 sinYaw, cosYaw;
					sinCosFixed(local(obj)->yaw, &sinYaw, &cosYaw);

					if (local(phase) == ATTACK_POINT)
					{
						local(nextPhaseChangeTick)   = s_curTick + ATTACK_POINT_DURATION;
						local(moveState)->thrustScale = ONE_16;
						local(phase) = ATTACK_POINT1;

						local(physicsActor)->vel.y -= FIXED(15);
						local(physicsActor)->vel.x += mul16(FIXED(80), sinYaw);
						local(physicsActor)->vel.z += mul16(FIXED(80), cosYaw);
					}

					// Phase == ATTACK_POINT1
					local(moveState)->target.x = s_playerObject->posWS.x + mul16(FIXED(10), sinYaw);
					local(moveState)->target.y = s_playerObject->posWS.y - FIXED(5);
					local(moveState)->target.z = s_playerObject->posWS.z + mul16(FIXED(10), cosYaw);
				}
				else if (local(phase) == ATTACK_CIRCLE || local(phase) == ATTACK_CIRCLE1)
				{
					// TFE: Optional feature.
					if (TFE_Settings::getGameSettings()->df_bobaFettFacePlayer)
					{
						local(target)->yaw = angle;
						local(target)->flags |= TARGET_MOVE_ROT;
					}

					if (local(phase) == ATTACK_CIRCLE)
					{
						local(nextPhaseChangeTick) = s_curTick + ATTACK_CIRCLE_DURATION;
						local(lateralAccel) = (s_curTick & 1) ? -FIXED(15) : FIXED(15);
						local(phase) = ATTACK_CIRCLE1;
					}

					// Phase == ATTACK_CIRCLE1
					local(moveState)->lateralAccel    = FIXED(15);		// this doesn't seem right - it should probably have been: local(moveState)->lateralAccel = local(lateralAccel)
					local(moveState)->target.x = s_playerObject->posWS.x;
					local(moveState)->target.y = local(heightTarget);
					local(moveState)->target.z = s_playerObject->posWS.z;
				}
				else if (local(phase) == ATTACK_FIGURE8 || local(phase) == ATTACK_FIGURE8_1)
				{
					// TFE: Optional feature.
					if (TFE_Settings::getGameSettings()->df_bobaFettFacePlayer)
					{
						local(target)->yaw = angle;
						local(target)->flags |= TARGET_MOVE_ROT;
					}

					if (local(phase) == ATTACK_FIGURE8)
					{
						local(nextPhaseChangeTick) = s_curTick + ATTACK_FIGURE8_DURATION;
						local(nextSwapAccelTick)   = s_curTick + FIGURE8_INTERVAL;
						local(phase) = ATTACK_FIGURE8_1;
						local(lateralAccel) = FIXED(50);
					}

					// Phase == ATTACK_FIGURE8_1
					local(moveState)->lateralAccel    = local(lateralAccel);
					local(moveState)->target.x = s_playerObject->posWS.x;
					local(moveState)->target.y = local(heightTarget);
					local(moveState)->target.z = s_playerObject->posWS.z;

					if (local(nextSwapAccelTick) < s_curTick)
					{
						local(lateralAccel) = -local(lateralAccel);
						local(nextSwapAccelTick) = s_curTick + FIGURE8_INTERVAL;
					}
				}
				else if (local(phase) == ATTACK_NEWMODE)
				{
					local(phase) = (s_curTick & 1) ? ATTACK_POINT : (AttackPhase)floor16(random(FIXED(ATTACK_COUNT)));
					local(nextPhaseChangeTick) = 0xffffffff;
				}

				bobaFett_handleMovement(local(bobaFett));
				local(moveState)->lateralAccel = 0;

				if (local(nextPhaseChangeTick) < s_curTick)
				{
					local(phase) = ATTACK_NEWMODE;
				}

				const angle14_32 angleDiff = getAngleDifference(angle, local(obj)->yaw);
				if (angleDiff < 1592) // 35 degrees
				{
					// Standard projectile attack
					if (local(nextAimedShotTick) < s_curTick && actor_canSeeObject(local(obj), s_playerObject))
					{
						local(nextAimedShotTick) = s_curTick + floor16(random(FIXED(TFE_Settings::getGameSettings()->df_bobaFettFacePlayer ? SHOOT_DELAY_RANGE_ENH : SHOOT_DELAY_RANGE_VANILLA)));
						ProjectileLogic* proj = (ProjectileLogic*)createProjectile(PROJ_BOBAFET_BALL, local(obj)->sector, local(obj)->posWS.x, local(obj)->posWS.y - FIXED(4), local(obj)->posWS.z, local(obj));
						proj->prevColObj = local(obj);
						proj->excludeObj = local(obj);

						sound_playCued(s_shared.boba2SndID, local(obj)->posWS);
						SecObject* projObj = proj->logic.obj;
						projObj->yaw = angle;
						actor_leadTarget(proj);
					}

					// Close range attack. Occurs at distance < 25 units and projectiles last 1 second only
					fixed16_16 dist = distApprox(s_playerObject->posWS.x, s_playerObject->posWS.z, local(obj)->posWS.x, local(obj)->posWS.z);
					if (dist < FIXED(25) && local(nextShootTick) < s_curTick && actor_canSeeObject(local(obj), s_playerObject))
					{
						local(nextShootTick) = s_curTick + 36;
						ProjectileLogic* proj = (ProjectileLogic*)createProjectile(PROJ_BOBAFET_BALL, local(obj)->sector, local(obj)->posWS.x, local(obj)->posWS.y - FIXED(4), local(obj)->posWS.z, local(obj));
						proj->prevColObj = local(obj);
						proj->excludeObj = local(obj);

						sound_playCued(s_shared.boba2SndID, local(obj)->posWS);
						SecObject* projObj = proj->logic.obj;
						projObj->yaw = angle;
						proj_aimAtTarget(proj, s_playerObject->posWS);
						proj->duration = s_curTick + 145; // 1 second
					}
				}  // if (angleDiff < 1592)

				if (local(nextChangeHeightTick) < s_curTick)  // Pick a target height
				{
					local(nextChangeHeightTick) = s_curTick + floor16(random(FIXED(CHANGE_HEIGHT_INTERVAL)));
					RSector* sector = local(obj)->sector;
					fixed16_16 height = sector->floorHeight - sector->ceilingHeight;
					local(heightTarget) = sector->floorHeight - random(height);
				}

				fixed16_16 top = s_playerObject->posWS.y - s_playerObject->worldHeight;
				if (top > local(heightTarget))
				{
					top = local(heightTarget);
				}
				if (top <= s_playerObject->posWS.y - FIXED(25))
				{
					top = s_playerObject->posWS.y - FIXED(25);
				}
				local(heightTarget) = top;
			task_localBlockEnd;
		}  // while (state == BOBASTATE_MOVE_AND_ATTACK)
		task_end;
	}

	void bobaFett_handleDyingState(MessageType msg)
	{
		struct LocalContext
		{
			SecObject* obj;
			PhysicsActor* physicsActor;
			ActorTarget* target;
			LogicAnimation* anim;
		};
		task_begin_ctx;

		local(obj) = s_curBobaFett->logic.obj;
		local(physicsActor) = &s_curBobaFett->actor;
		local(target) = &local(physicsActor)->moveMod.target;
		local(anim) = &local(physicsActor)->anim;

		local(target)->flags |= TARGET_FREEZE;
		sound_stop(local(physicsActor)->moveSndId);
		sound_playCued(s_shared.boba4SndID, local(obj)->posWS);

		local(anim)->flags |= AFLAG_PLAYONCE;
		local(anim)->frameRate = 8;
		actor_setupBossAnimation(local(obj), 2, local(anim));

		// Wait for the animation to finish.
		do
		{
			entity_yield(TASK_NO_DELAY);
		} while (msg != MSG_RUN_TASK || !(local(anim)->flags & AFLAG_READY));

		task_localBlockBegin;
			SecObject* corpse = allocateObject();
			sprite_setData(corpse, local(obj)->wax);
			corpse->frame = 0;
			corpse->anim  = 4;
			corpse->posWS = local(obj)->posWS;
			corpse->worldWidth  = 0;
			corpse->worldHeight = 0;
			corpse->entityFlags |= (ETFLAG_CORPSE | ETFLAG_KEEP_CORPSE);
			sector_addObject(local(obj)->sector, corpse);
			obj_addToRefList(corpse, ObjRefType_Corpse);	// scripting

			local(physicsActor)->alive = JFALSE;
			actor_handleBossDeath(local(physicsActor));
		task_localBlockEnd;

		task_end;
	}

	void bobaFett_handleSearch(MessageType msg)
	{
		struct LocalContext
		{
			BobaFett* bobaFett;
			SecObject* obj;
			PhysicsActor* physicsActor;
			ActorTarget* target;
			LogicAnimation* anim;
			SearchPhase phase;
			Tick nextCheckForPlayerTick;
			Tick changeStateTick;
			Tick nextChangePhaseTick;
		};
		task_begin_ctx;

		local(bobaFett) = s_curBobaFett;
		local(obj) = local(bobaFett)->logic.obj;
		local(physicsActor) = &local(bobaFett)->actor;
		local(target) = &local(physicsActor)->moveMod.target;
		local(anim) = &local(physicsActor)->anim;

		local(phase) = SEARCH_FIND;
		local(nextCheckForPlayerTick) = 0;
		local(changeStateTick) = s_curTick + SEARCH_DURATION;
		local(nextChangePhaseTick) = s_curTick + SEARCH_PHASE_INTERVAL;
		local(physicsActor)->moveMod.collisionFlags |= ACTORCOL_BIT2;

		while (local(physicsActor)->state == BOBASTATE_SEARCH)
		{
			do
			{
				entity_yield(TASK_NO_DELAY);
				s_curBobaFett = local(bobaFett);
				task_callTaskFunc(bobaFett_handleMsg);
			} while (msg != MSG_RUN_TASK);
			if (local(physicsActor)->state != BOBASTATE_SEARCH) { break; }

			if (local(phase) == SEARCH_FIND)
			{
				local(bobaFett)->moveState.target.x = s_playerObject->posWS.x;
				local(bobaFett)->moveState.target.y = s_eyePos.y + FIXED(3);
				local(bobaFett)->moveState.target.z = s_playerObject->posWS.z;
			}
			else if (local(phase) == SEARCH_RAND)
			{
				local(bobaFett)->moveState.target.x = local(obj)->posWS.x;
				local(bobaFett)->moveState.target.z = local(obj)->posWS.z;
				actor_offsetTarget(&local(bobaFett)->moveState.target.x, &local(bobaFett)->moveState.target.z, FIXED(80), FIXED(40), random_next(), 0x1fff);
				local(phase) = SEARCH_CONTINUE;
			}
			// else SEARCH_CONTINUE : Continue with current target.

			bobaFett_handleMovement(local(bobaFett));

			if (local(nextChangePhaseTick) < s_curTick)
			{
				local(phase) = (s_curTick & 1) ? SEARCH_FIND : SEARCH_RAND;
				local(nextChangePhaseTick) = s_curTick + SEARCH_PHASE_INTERVAL;
			}

			if (local(nextCheckForPlayerTick) < s_curTick)
			{
				if (!s_playerDying && actor_canSeeObject(local(obj), s_playerObject))
				{
					local(physicsActor)->state = BOBASTATE_MOVE_AND_ATTACK;
				}
				else
				{
					local(nextCheckForPlayerTick) = s_curTick + 145;
				}
			}
			if (local(changeStateTick) < s_curTick)
			{
				local(physicsActor)->state = BOBASTATE_DEFAULT;
			}
		}  // while (state == BOBASTATE_SEARCH)

		local(physicsActor)->moveMod.collisionFlags |= ACTORCOL_BIT2;
		task_end;
	}

	void bobaFett_handleDefault(MessageType msg)
	{
		struct LocalContext
		{
			BobaFett* bobaFett;
			SecObject* obj;
			PhysicsActor* physicsActor;
			ActorTarget* target;
			LogicAnimation* anim;
			Tick nextCheckForPlayerTick;
			Tick changeStateTick;
		};
		task_begin_ctx;

		local(bobaFett) = s_curBobaFett;
		local(obj) = local(bobaFett)->logic.obj;
		local(physicsActor) = &local(bobaFett)->actor;
		local(target) = &local(physicsActor)->moveMod.target;
		local(anim) = &local(physicsActor)->anim;

		while (local(physicsActor)->state == BOBASTATE_DEFAULT)
		{
			do
			{
				entity_yield(145);
				s_curBobaFett = local(bobaFett);
				task_callTaskFunc(bobaFett_handleMsg);

				if (msg == MSG_DAMAGE || msg == MSG_EXPLOSION)
				{
					task_makeActive(local(physicsActor)->actorTask);
					local(physicsActor)->state = BOBASTATE_MOVE_AND_ATTACK;
					task_yield(0);
					break;
				}
			} while (msg != MSG_RUN_TASK);
			if (local(physicsActor)->state != BOBASTATE_DEFAULT) { break; }

			if (actor_canSeeObject(local(obj), s_playerObject))
			{
				sound_playCued(s_shared.boba1SndID, local(obj)->posWS);
				local(physicsActor)->state = BOBASTATE_SEARCH;
				actor_setupBossAnimation(local(obj), 0, local(anim));
			}
		}  // while (state == BOBASTATE_DEFAULT)
		task_end;
	}

	void bobaFettTaskFunc(MessageType msg)
	{
		struct LocalContext
		{
			BobaFett* bobaFett;
			SecObject* obj;
			PhysicsActor* physicsActor;
			ActorTarget* target;
			LogicAnimation* anim;
		};
		task_begin_ctx;

		local(bobaFett) = (BobaFett*)task_getUserData();
		local(obj) = local(bobaFett)->logic.obj;
		local(physicsActor) = &local(bobaFett)->actor;
		local(target) = &local(physicsActor)->moveMod.target;
		local(anim) = &local(physicsActor)->anim;

		while (local(physicsActor)->alive)
		{
			msg = MSG_RUN_TASK;
			task_setMessage(msg);

			if (local(physicsActor)->state == BOBASTATE_MOVE_AND_ATTACK)
			{
				s_curBobaFett = local(bobaFett);
				task_callTaskFunc(bobaFett_handleMoveAndAttack);
			}
			else if (local(physicsActor)->state == BOBASTATE_DYING)
			{
				s_curBobaFett = local(bobaFett);
				task_callTaskFunc(bobaFett_handleDyingState);
			}
			else if (local(physicsActor)->state == BOBASTATE_SEARCH)
			{
				s_curBobaFett = local(bobaFett);
				task_callTaskFunc(bobaFett_handleSearch);
			}
			else  // Default
			{
				s_curBobaFett = local(bobaFett);
				task_callTaskFunc(bobaFett_handleDefault);
			}
		}  // while (alive)

		// Dead
		while (msg != MSG_RUN_TASK)
		{
			task_yield(TASK_NO_DELAY);
		}

		actor_removePhysicsActorFromWorld(local(physicsActor));
		deleteLogicAndObject(&local(bobaFett)->logic);
		level_free(local(bobaFett));
		
		while (msg != MSG_RUN_TASK)
		{
			task_yield(TASK_NO_DELAY);
		}
		task_end;
	}

	void bobaFettCleanupFunc(Logic* logic)
	{
		BobaFett* bobaFett = (BobaFett*)logic;
		PhysicsActor* physicsActor = &bobaFett->actor;

		actor_removePhysicsActorFromWorld(physicsActor);
		deleteLogicAndObject(&bobaFett->logic);
		level_free(bobaFett);
		task_free(physicsActor->actorTask);
	}

	void bobaFett_serialize(Logic*& logic, SecObject* obj, Stream* stream)
	{
		BobaFett* bobaFett = nullptr;
		bool write = serialization_getMode() == SMODE_WRITE;
		PhysicsActor* physicsActor = nullptr;
		Task* bobaFettTask = nullptr;
		if (write)
		{
			bobaFett = (BobaFett*)logic;
			physicsActor = &bobaFett->actor;
		}
		else
		{
			bobaFett = (BobaFett*)level_alloc(sizeof(BobaFett));
			memset(bobaFett, 0, sizeof(BobaFett));
			physicsActor = &bobaFett->actor;
			logic = (Logic*)bobaFett;

			// Task
			char name[32];
			sprintf(name, "BobaFett%d", s_shared.bobaFettNum);
			s_shared.bobaFettNum++;

			bobaFettTask = createSubTask(name, bobaFettTaskFunc);
			task_setUserData(bobaFettTask, bobaFett);

			// Logic
			logic->task = bobaFettTask;
			logic->cleanupFunc = bobaFettCleanupFunc;
			logic->type = LOGIC_BOBA_FETT;
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
			physicsActor->actorTask = bobaFettTask;

			bobaFett->hitSndId = NULL_SOUND;
			bobaFett->unused = 0;
			bobaFett->moveState.soundSrc = s_shared.bobaRocket2SndID;
		}
		SERIALIZE(SaveVersionInit, physicsActor->vel, { 0 });
		SERIALIZE(SaveVersionInit, physicsActor->lastPlayerPos, { 0 });
		SERIALIZE(SaveVersionInit, physicsActor->alive, JTRUE);
		SERIALIZE(SaveVersionInit, physicsActor->hp, FIXED(200));
		SERIALIZE(SaveVersionInit, physicsActor->state, BOBASTATE_DEFAULT);

		SERIALIZE(SaveVersionInit, bobaFett->moveState.target, { 0 });
		SERIALIZE(SaveVersionInit, bobaFett->moveState.thrustFactor, 0);
		SERIALIZE(SaveVersionInit, bobaFett->moveState.thrustPitchRange, 0);
		SERIALIZE(SaveVersionInit, bobaFett->moveState.thrustPitch, 0);
		SERIALIZE(SaveVersionInit, bobaFett->moveState.thrustScale, 0);
		SERIALIZE(SaveVersionInit, bobaFett->moveState.lateralAccel, 0);
	}
		
	Logic* bobaFett_setup(SecObject* obj, LogicSetupFunc* setupFunc)
	{
		BobaFett* bobaFett = (BobaFett*)level_alloc(sizeof(BobaFett));
		memset(bobaFett, 0, sizeof(BobaFett));

		PhysicsActor* physicsActor = &bobaFett->actor;

		// Give the name of the task a number so I can tell them apart when debugging.
		char name[32];
		sprintf(name, "BobaFett%d", s_shared.bobaFettNum);
		s_shared.bobaFettNum++;
		Task* task = createSubTask(name, bobaFettTaskFunc);
		task_setUserData(task, bobaFett);

		obj->entityFlags = ETFLAG_AI_ACTOR;
		obj->worldHeight = FIXED(7);

		physicsActor->alive = JTRUE;
		physicsActor->hp    = FIXED(200);
		physicsActor->state = BOBASTATE_DEFAULT;
		physicsActor->actorTask = task;

		bobaFett->unused   = 0;
		bobaFett->hitSndId = 0;
		bobaFett->moveState.thrustFactor = FIXED(220);
		bobaFett->moveState.thrustPitchRange = ONE_16;
		bobaFett->moveState.thrustPitch = 0;
		bobaFett->moveState.thrustScale = 0;
		bobaFett->moveState.soundSrc   = s_shared.bobaRocket2SndID;
		bobaFett->moveState.lateralAccel = 0;
		bobaFett->logic.obj = obj;
		actor_addPhysicsActorToWorld(physicsActor);

		physicsActor->moveMod.header.obj  = obj;
		physicsActor->moveMod.physics.obj = obj;
		actor_setupSmartObj(&physicsActor->moveMod);

		physicsActor->moveMod.physics.width = FIXED(2);
		physicsActor->moveMod.physics.botOffset = 0;

		physicsActor->moveMod.collisionFlags &= ~ACTORCOL_ALL;
		physicsActor->moveMod.collisionFlags |= (ACTORCOL_GRAVITY | ACTORCOL_BIT2);
		physicsActor->moveMod.physics.yPos = COL_INFINITY;
		physicsActor->moveMod.physics.height = obj->worldHeight;

		LogicAnimation* anim = &physicsActor->anim;
		anim->frameRate = 5;
		anim->frameCount = ONE_16;
		anim->prevTick = 0;
		anim->flags |= AFLAG_READY;
		anim->flags &= ~AFLAG_PLAYONCE;
		actor_setupBossAnimation(obj, 5, anim);

		ActorTarget* target = &physicsActor->moveMod.target;
		target->speedRotation = 6826; // 150 degrees

		obj_addLogic(obj, (Logic*)bobaFett, LOGIC_BOBA_FETT, task, bobaFettCleanupFunc);

		if (setupFunc)
		{
			*setupFunc = nullptr;
		}
		return (Logic*)bobaFett;
	}
}  // namespace TFE_DarkForces