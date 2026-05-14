#include "weaponFireFunc.h"
#include "weapon.h"
#include "random.h"
#include "player.h"
#include "pickup.h"
#include "projectile.h"
#include "Actor/actor.h"
#include <TFE_Jedi/Collision/collision.h>
#include <TFE_Jedi/Renderer/jediRenderer.h>
// TFE
#include <TFE_Settings/settings.h>

namespace TFE_DarkForces
{
	enum WeaponFireConst
	{
		MAX_AUTOAIM_DIST = COL_INFINITY,
		WPN_NUM_ANIMFRAMES = 16,
	};

	static SoundEffectId s_punchSwingSndId = 0;
	static SoundEffectId s_pistolSndId = 0;
	static SoundEffectId s_rifleSndId = 0;
	static SoundEffectId s_mortarFireSndID = 0;
	static SoundEffectId s_mortarFireSndID2 = 0;
	static SoundEffectId s_outOfAmmoSndId = 0;
	static SoundEffectId s_mortarOutofAmmoSndId = 0;
	static SoundEffectId s_repeaterFireSndID1 = 0;
	static SoundEffectId s_repeaterOutOfAmmoSndId = 0;
	static SoundEffectId s_fusionFireSndID = 0;
	static SoundEffectId s_fusionOutOfAmmoSndID = 0;
	static SoundEffectId s_mineSndId = 0;
	static SoundEffectId s_concussionFireSndID = 0;
	static SoundEffectId s_concussionFireSndID1 = 0;
	static SoundEffectId s_concussionOutOfAmmoSndID = 0;
	static SoundEffectId s_cannonOutOfAmmoSndID = 0;
	static SoundEffectId s_cannonFireSndID = 0;
	static SoundEffectId s_cannonFireSndID1 = 0;
	static fixed16_16 s_autoAimDirX;
	static fixed16_16 s_autoAimDirZ;
	static fixed16_16 s_wpnPitchSin;
	static fixed16_16 s_wpnPitchCos;
	static angle14_32 s_weaponFirePitch;
	static angle14_32 s_weaponFireYaw;

	extern WeaponAnimState s_weaponAnimState;
	extern SoundEffectId s_repeaterFireSndID;

	extern JBool s_weaponAutoMount2;
	extern JBool s_secondaryFire;
	extern JBool s_weaponOffAnim;
	extern JBool s_isShooting;
	extern s32 s_canFireWeaponSec;
	extern s32 s_canFireWeaponPrim;
	extern u32 s_fireFrame;

	extern s32 s_prevWeapon;
	extern s32 s_curWeapon;
	extern s32 s_nextWeapon;
	extern s32 s_lastWeapon;

	struct WeaponAnimFrame
	{
		s32 waxFrame;
		s32 weaponLight;
		Tick delaySupercharge;
		Tick delayNormal;
	};

	static s32 s_punchNumFrames = 0;
	static s32 s_pistolNumFrames = 0;
	static s32 s_rifleNumFrames = 0;
	static s32 s_thermalDetNumFrames = 0;
	static s32 s_repeaterPrimaryNumFrames = 0;
	static s32 s_repeaterSecondaryNumFrames = 0;
	static s32 s_fusionPrimaryNumFrames = 0;
	static s32 s_fusionSecondaryNumFrames = 0;
	static s32 s_mortarNumFrames = 0;
	static s32 s_concussionNumFrames = 0;
	static s32 s_cannonPrimaryNumFrames = 0;
	static s32 s_cannonSecondaryNumFrames = 0;

	static WeaponAnimFrame s_punchAnim[WPN_NUM_ANIMFRAMES];
	static WeaponAnimFrame s_pistolAnim[WPN_NUM_ANIMFRAMES];
	static WeaponAnimFrame s_rifleAnim[WPN_NUM_ANIMFRAMES];
	static WeaponAnimFrame s_thermalDetAnim[WPN_NUM_ANIMFRAMES];
	static WeaponAnimFrame s_repeaterPrimaryAnim[WPN_NUM_ANIMFRAMES];
	static WeaponAnimFrame s_repeaterSecondaryAnim[WPN_NUM_ANIMFRAMES];
	static WeaponAnimFrame s_fusionPrimaryAnim[WPN_NUM_ANIMFRAMES];
	static WeaponAnimFrame s_fusionSecondaryAnim[WPN_NUM_ANIMFRAMES];
	static WeaponAnimFrame s_mortarAnim[WPN_NUM_ANIMFRAMES];
	static WeaponAnimFrame s_concussionAnim[WPN_NUM_ANIMFRAMES];
	static WeaponAnimFrame s_cannonPrimaryAnim[WPN_NUM_ANIMFRAMES];
	static WeaponAnimFrame s_cannonSecondaryAnim[WPN_NUM_ANIMFRAMES];

	static const angle14_32 c_repeaterYawOffset[3] = { 0, 136, -136 };
	static const angle14_32 c_repeaterPitchOffset[3] = { 136, -136, -136 };
	// Deltas for repeater - note the order is ZXY
	static fixed16_16 c_repeaterDelta[3 * 3] =
	{
		0x50000, 0xe666,  0xfae1,   // 5.0, 0.9, 0.98
		0x50000, 0x8000,  0x18000,  // 5.0, 0.5, 1.5
		0x50000, 0x18000, 0x18000,  // 5.0, 1.5, 1.5
	};
	static s32 s_fusionCylinder = 1;
	static fixed16_16 s_fusionOffset[] =
	{
		FIXED(3), -ONE_16, 0x8000,	// 3.0, -1.0, 0.5
		FIXED(3),  -19660, 0xcccc,	// 3.0, -0.3, 0.8
		FIXED(3),  0x4ccc, 0x11999,	// 3.0,  0.3, 1.1
		FIXED(3),  0xcccc, 0x14ccc, // 3.0,  0.8, 1.3
	};
	static angle14_32 s_fusionYawOffset[] =
	{
		-375, -125, 125, 375
	};
	static JBool s_fusionCycleForward = JTRUE;

	extern void weapon_handleState(MessageType msg);
	extern void weapon_handleState2(MessageType msg);
	extern void weapon_handleOffAnimation(MessageType msg);
	extern void weapon_handleOnAnimation(MessageType msg);
	extern void weapon_animateOnOrOffscreen(MessageType msg);
	JBool computeAutoaim(fixed16_16 xPos, fixed16_16 yPos, fixed16_16 zPos, angle14_32 pitch, angle14_32 yaw, s32 variation);

	// Ensure that the weapon is reset to the default states for replays
	void resetWeaponFunc()
	{
		s_weaponFirePitch = 0;
		s_weaponFireYaw = 0;
	}
	
	// TFE: Set up animation frames from external data (these were hardcoded in vanilla DF)
	void setupAnimationFrames(WeaponID weaponId, s32 numPrimFrames, TFE_ExternalData::WeaponAnimFrame* extPrimFrames, s32 numSecFrames, TFE_ExternalData::WeaponAnimFrame* extSecFrames)
	{
		WeaponAnimFrame* primaryFrames = nullptr;
		WeaponAnimFrame* secondaryFrames = nullptr;

		switch (weaponId)
		{
			case WPN_FIST:
				s_punchNumFrames = numPrimFrames;
				primaryFrames = s_punchAnim;
				break;

			case WPN_PISTOL:
				s_pistolNumFrames = numPrimFrames;
				primaryFrames = s_pistolAnim;
				break;

			case WPN_RIFLE:
				s_rifleNumFrames = numPrimFrames;
				primaryFrames = s_rifleAnim;
				break;

			case WPN_THERMAL_DET:
				s_thermalDetNumFrames = numPrimFrames;
				primaryFrames = s_thermalDetAnim;
				break;

			case WPN_REPEATER:
				s_repeaterPrimaryNumFrames = numPrimFrames;
				s_repeaterSecondaryNumFrames = numSecFrames;
				primaryFrames = s_repeaterPrimaryAnim;
				secondaryFrames = s_repeaterSecondaryAnim;
				break;

			case WPN_FUSION:
				s_fusionPrimaryNumFrames = numPrimFrames;
				s_fusionSecondaryNumFrames = numSecFrames;
				primaryFrames = s_fusionPrimaryAnim;
				secondaryFrames = s_fusionSecondaryAnim;
				break;

			case WPN_MORTAR:
				s_mortarNumFrames = numPrimFrames;
				primaryFrames = s_mortarAnim;
				break;

			case WPN_CONCUSSION:
				s_concussionNumFrames = numPrimFrames;
				primaryFrames = s_concussionAnim;
				break;

			case WPN_CANNON:
				s_cannonPrimaryNumFrames = numPrimFrames;
				s_cannonSecondaryNumFrames = numSecFrames;
				primaryFrames = s_cannonPrimaryAnim;
				secondaryFrames = s_cannonSecondaryAnim;
				break;
		}

		if (primaryFrames)
		{
			for (s32 i = 0; i < numPrimFrames; i++)
			{
				primaryFrames[i].waxFrame = extPrimFrames[i].texture;
				primaryFrames[i].weaponLight = extPrimFrames[i].light;
				primaryFrames[i].delaySupercharge = extPrimFrames[i].durationSupercharge;
				primaryFrames[i].delayNormal = extPrimFrames[i].durationNormal;
			}
		}

		if (secondaryFrames)
		{
			for (s32 i = 0; i < numSecFrames; i++)
			{
				secondaryFrames[i].waxFrame = extSecFrames[i].texture;
				secondaryFrames[i].weaponLight = extSecFrames[i].light;
				secondaryFrames[i].delaySupercharge = extSecFrames[i].durationSupercharge;
				secondaryFrames[i].delayNormal = extSecFrames[i].durationNormal;
			}
		}
	}

	// Adjust the speed - in TFE the framerate may be higher than expected by the original code, which means that this
	// step (proj->delta) is too large for the 'speed'.
	void tfe_adjustWeaponCollisionSpeed(ProjectileLogic* proj)
	{
		fixed16_16 initSpeed = div16(vec3Length(proj->delta.x, proj->delta.y, proj->delta.z), max(1, s_deltaTime));
		proj->col_speed = proj->speed;
		if (initSpeed > proj->speed)
		{
			proj->col_speed = initSpeed;
		}
	}

	void tfe_restoreWeaponCollisionSpeed(ProjectileLogic* proj)
	{
		proj->col_speed = proj->speed;
	}

	void applyOneHitKillCheat(ProjectileLogic* proj)
	{
		proj->dmg = FIXED(9999);
	}

	s32 getMaxAmmo(s32* ammo)
	{
		if (ammo == s_playerAmmoEnergy)
		{
			return s_ammoEnergyMax;
		}

		if (ammo == s_playerAmmoPower)
		{
			return s_ammoPowerMax;
		}

		if (ammo == s_playerAmmoPlasma)
		{
			return s_ammoPlasmaMax;
		}

		if (ammo == s_playerAmmoShell)
		{
			return s_ammoShellMax;
		}

		if (ammo == s_playerAmmoDetonators)
		{
			return s_ammoDetonatorMax;
		}

		if (ammo == s_playerAmmoMines)
		{
			return s_ammoMineMax;
		}

		if (ammo == s_playerAmmoMissiles)
		{
			return s_ammoMissileMax;
		}

		return 0;
	}

	void weaponFire_fist(MessageType msg)
	{
		struct LocalContext
		{
			Tick delay;
			s32  iFrame;
		};
		task_begin_ctx;

		// Initial animation frame
		taskCtx->delay = (s_superCharge) ? s_punchAnim[0].delaySupercharge : s_punchAnim[0].delayNormal;
		s_curPlayerWeapon->frame = s_punchAnim[0].waxFrame;
		s_weaponLight = s_punchAnim[0].weaponLight;
		do
		{
			task_yield(taskCtx->delay);
			task_callTaskFunc(weapon_handleState);
		} while (msg != MSG_RUN_TASK);

		// Sound effect
		if (s_punchSwingSndId)
		{
			sound_stop(s_punchSwingSndId);
		}
		s_punchSwingSndId = sound_play(s_punchSwingSndSrc);

		// Wakeup AI within range
		if (s_curPlayerWeapon->wakeupRange)
		{
			vec3_fixed origin = { s_playerObject->posWS.x, s_playerObject->posWS.y, s_playerObject->posWS.z };
			collision_effectObjectsInRangeXZ(s_playerObject->sector, s_curPlayerWeapon->wakeupRange, origin, actor_sendWakeupMsg, s_playerObject, ETFLAG_AI_ACTOR);
		}

		task_localBlockBegin;
		// Aim and spawn projectiles
		fixed16_16 mtx[9];
		weapon_computeMatrix(mtx, -s_playerObject->pitch, -s_playerObject->yaw);

		fixed16_16 xOffset = -114688;	// -1.75
		angle14_32 yawOffset = -910;
		for (s32 i = 0; i < 3; i++, xOffset += 0x1c000, yawOffset += 910)
		{
			ProjectileLogic* proj = (ProjectileLogic*)createProjectile(s_curPlayerWeapon->primaryProjectile, s_playerObject->sector, s_playerObject->posWS.x, s_playerObject->posWS.y - s_playerObject->worldHeight + s_headwaveVerticalOffset, s_playerObject->posWS.z, s_playerObject);
			if (s_oneHitKillEnabled) {	applyOneHitKillCheat(proj); }

			s_weaponFirePitch = s_playerObject->pitch + 0x638;
			s_weaponFireYaw   = s_playerObject->yaw + yawOffset;
			proj_setTransform(proj, s_weaponFirePitch, s_weaponFireYaw);

			proj->hitEffectId = HEFFECT_PUNCH;
			proj->prevColObj = s_playerObject;
			proj->excludeObj = s_playerObject;

			vec3_fixed inVec =
			{
				xOffset, // -1.75, 0.0, +1.75
				0x18000, // 1.5
				0x60000  // 6.0
			};
			rotateVectorM3x3(&inVec, &proj->delta, mtx);

			tfe_adjustWeaponCollisionSpeed(proj);

			ProjectileHitType hitType = proj_handleMovement(proj);
			handleProjectileHit(proj, hitType);

			tfe_restoreWeaponCollisionSpeed(proj);
		}
		task_localBlockEnd;

		// Animation.
		for (taskCtx->iFrame = 1; taskCtx->iFrame < s_punchNumFrames; taskCtx->iFrame++)
		{
			s_curPlayerWeapon->frame = s_punchAnim[taskCtx->iFrame].waxFrame;
			s_weaponLight = s_punchAnim[taskCtx->iFrame].weaponLight;
			taskCtx->delay = (s_superCharge) ? s_punchAnim[taskCtx->iFrame].delaySupercharge : s_punchAnim[taskCtx->iFrame].delayNormal;

			do
			{
				task_yield(taskCtx->delay);
				task_callTaskFunc(weapon_handleState);
			} while (msg != MSG_RUN_TASK);
		}
		s_canFireWeaponPrim = 1;
		s_canFireWeaponSec = 1;

		task_end;
	}

	void weaponFire_pistol(MessageType msg)
	{
		struct LocalContext
		{
			Tick delay;
			s32  iFrame;
		};
		task_begin_ctx;

		if (*s_curPlayerWeapon->ammo)
		{
			task_localBlockBegin;
			// Sound effect
			if (s_pistolSndId)
			{
				sound_stop(s_pistolSndId);
			}
			s_pistolSndId = sound_play(s_pistolSndSrc);

			// Wakeup AI within range
			if (s_curPlayerWeapon->wakeupRange)
			{
				vec3_fixed origin = { s_playerObject->posWS.x, s_playerObject->posWS.y, s_playerObject->posWS.z };
				collision_effectObjectsInRangeXZ(s_playerObject->sector, s_curPlayerWeapon->wakeupRange, origin, actor_sendWakeupMsg, s_playerObject, ETFLAG_AI_ACTOR);
			}

			// Aim projectile
			fixed16_16 mtx[9];
			weapon_computeMatrix(mtx, -s_playerObject->pitch, -s_playerObject->yaw);

			vec3_fixed inVec =
			{
				0x6666,	// 0.4
				0x9999, // 0.6
				0x33333 // 3.2
			};
			vec3_fixed outVec;
			rotateVectorM3x3(&inVec, &outVec, mtx);

			fixed16_16 xPos = s_playerObject->posWS.x + outVec.x;
			fixed16_16 yPos = outVec.y + s_playerObject->posWS.y - s_playerObject->worldHeight + s_headwaveVerticalOffset;
			fixed16_16 zPos = s_playerObject->posWS.z + outVec.z;

			JBool targetFound = computeAutoaim(xPos, yPos, zPos, s_playerObject->pitch, s_playerObject->yaw, s_curPlayerWeapon->variation & 0xffff);
			if (!targetFound)
			{
				s32 variation = s_curPlayerWeapon->variation & 0xffff;
				variation = random(variation * 2) - variation;
				s_weaponFirePitch = s_playerObject->pitch + variation;

				variation = s_curPlayerWeapon->variation & 0xffff;
				variation = random(variation * 2) - variation;
				s_weaponFireYaw = s_playerObject->yaw + variation;
			}

			s32 canFire = s_canFireWeaponPrim;
			fixed16_16 mtx2[9];
			s_canFireWeaponPrim = 0;
			if (canFire > 1)
			{
				weapon_computeMatrix(mtx2, -s_weaponFirePitch, -s_weaponFireYaw);
			}

			// Spawn projectiles.
			fixed16_16 fire = intToFixed16(canFire);
			while (canFire)
			{
				if (*s_curPlayerWeapon->ammo == 0)
				{
					break;
				}
				fire -= ONE_16;
				canFire--;
				s32 superChargeFrame = s_superCharge ? 0 : 1;
				// This is always true if super charge is *not* active otherwise
				// it is true every other frame.
				if (superChargeFrame | (s_fireFrame & 1))
				{
					*s_curPlayerWeapon->ammo = pickup_addToValue(
						*s_curPlayerWeapon->ammo,
						-s_curPlayerWeapon->primaryFireConsumption,
						getMaxAmmo(s_curPlayerWeapon->ammo));
				}

				ProjectileLogic* projLogic;
				fixed16_16 yPos = s_playerObject->posWS.y - s_playerObject->worldHeight + s_headwaveVerticalOffset;
				projLogic = (ProjectileLogic*)createProjectile(s_curPlayerWeapon->primaryProjectile, s_playerObject->sector, s_playerObject->posWS.x, yPos, s_playerObject->posWS.z, s_playerObject);
				projLogic->flags &= ~PROJFLAG_CAMERA_PASS_SOUND;
				projLogic->prevColObj = s_playerObject;
				if (s_oneHitKillEnabled) { applyOneHitKillCheat(projLogic); }

				if (targetFound)
				{
					proj_setYawPitch(projLogic, s_wpnPitchSin, s_wpnPitchCos, s_autoAimDirX, s_autoAimDirZ);

					SecObject* projObj = projLogic->logic.obj;
					projObj->pitch = s_weaponFirePitch;
					projObj->yaw = s_weaponFireYaw;
				}
				else
				{
					proj_setTransform(projLogic, s_weaponFirePitch, s_weaponFireYaw);
				}

				if (canFire)
				{
					vec3_fixed inVec2 = { 0 };
					if (canFire > 1)
					{
						// TODO: This doesn't seem to be hit in the base game.
						assert(0);
					}

					Tick v0 = (s_superCharge) ? 21 : 43;
					Tick v1 = (s_superCharge) ? 7 : 14;
					Tick v2 = (s_superCharge) ? 7 : 14;
					inVec2.z = intToFixed16(s32(float(v0 + v1 + v2) * 1.71f));

					vec3_fixed outVec2;
					rotateVectorM3x3(&inVec2, &outVec2, mtx2);
					// First try using the outVec value computed above.
					projLogic->delta = outVec;
					tfe_adjustWeaponCollisionSpeed(projLogic);

					ProjectileHitType hitType = proj_handleMovement(projLogic);
					if (!handleProjectileHit(projLogic, hitType))
					{
						// If that fails, then try the new value.
						projLogic->delta = outVec2;
						tfe_adjustWeaponCollisionSpeed(projLogic);

						hitType = proj_handleMovement(projLogic);
						fire = handleProjectileHit(projLogic, hitType);
					}
				}
				else
				{
					projLogic->delta = outVec;
					tfe_adjustWeaponCollisionSpeed(projLogic);

					ProjectileHitType hitType = proj_handleMovement(projLogic);
					handleProjectileHit(projLogic, hitType);
				}
				tfe_restoreWeaponCollisionSpeed(projLogic);
			}
			task_localBlockEnd;
			
			// Animation.
			for (taskCtx->iFrame = 0; taskCtx->iFrame < s_pistolNumFrames; taskCtx->iFrame++)
			{
				s_curPlayerWeapon->frame = s_pistolAnim[taskCtx->iFrame].waxFrame;
				s_weaponLight = s_pistolAnim[taskCtx->iFrame].weaponLight;
				taskCtx->delay = (s_superCharge) ? s_pistolAnim[taskCtx->iFrame].delaySupercharge : s_pistolAnim[taskCtx->iFrame].delayNormal;

				do
				{
					task_yield(taskCtx->delay);
					task_callTaskFunc(weapon_handleState);
				} while (msg != MSG_RUN_TASK);
			}

			s_canFireWeaponPrim = 1;
			s_canFireWeaponSec = 1;
		}
		else  // Out of Ammo
		{
			if (s_outOfAmmoSndId)
			{
				sound_stop(s_outOfAmmoSndId);
			}
			s_outOfAmmoSndId = sound_play(s_pistolEmptySndSrc);

			taskCtx->delay = (s_superCharge) ? 3 : 7;
			s_curPlayerWeapon->frame = 0;
			do
			{
				task_yield(taskCtx->delay);
				task_callTaskFunc(weapon_handleState);
			} while (msg != MSG_RUN_TASK);

			s_canFireWeaponPrim = 0;
			s_canFireWeaponSec = 0;
			// if (s_weaponAutoMount2)
			// {
				// func_1ece78();
			// }
		}

		task_end;
	}

	void weaponFire_rifle(MessageType msg)
	{
		struct LocalContext
		{
			Tick delay;
			s32  iFrame;
		};
		task_begin_ctx;

		if (*s_curPlayerWeapon->ammo)
		{
			task_localBlockBegin;
			// Sound effect
			if (s_rifleSndId)
			{
				sound_stop(s_rifleSndId);
			}
			s_rifleSndId = sound_play(s_rifleSndSrc);

			// Wakeup AI within range
			if (s_curPlayerWeapon->wakeupRange)
			{
				vec3_fixed origin = { s_playerObject->posWS.x, s_playerObject->posWS.y, s_playerObject->posWS.z };
				collision_effectObjectsInRangeXZ(s_playerObject->sector, s_curPlayerWeapon->wakeupRange, origin, actor_sendWakeupMsg, s_playerObject, ETFLAG_AI_ACTOR);
			}

			// Aim projectile
			fixed16_16 mtx[9];
			weapon_computeMatrix(mtx, -s_playerObject->pitch, -s_playerObject->yaw);

			vec3_fixed inVec =
			{
				0x6666,	// 0.4
				0xc000, // 0.75
				0x40000 // 4.0
			};
			vec3_fixed outVec;
			rotateVectorM3x3(&inVec, &outVec, mtx);

			fixed16_16 xPos = s_playerObject->posWS.x + outVec.x;
			fixed16_16 yPos = outVec.y + s_playerObject->posWS.y - s_playerObject->worldHeight + s_headwaveVerticalOffset;
			fixed16_16 zPos = s_playerObject->posWS.z + outVec.z;

			JBool targetFound = computeAutoaim(xPos, yPos, zPos, s_playerObject->pitch, s_playerObject->yaw, s_curPlayerWeapon->variation & 0xffff);
			if (!targetFound)
			{
				s32 variation = s_curPlayerWeapon->variation & 0xffff;
				variation = random(variation * 2) - variation;
				s_weaponFirePitch = s_playerObject->pitch + variation;

				variation = s_curPlayerWeapon->variation & 0xffff;
				variation = random(variation * 2) - variation;
				s_weaponFireYaw = s_playerObject->yaw + variation;
			}

			s32 canFire = s_canFireWeaponPrim;
			fixed16_16 mtx2[9];
			s_canFireWeaponPrim = 0;
			if (canFire > 1)
			{
				weapon_computeMatrix(mtx2, -s_weaponFirePitch, -s_weaponFireYaw);
			}

			// Spawn projectiles.
			fixed16_16 fire = intToFixed16(canFire);
			while (canFire)
			{
				if (*s_curPlayerWeapon->ammo == 0)
				{
					break;
				}
				fire -= ONE_16;
				canFire--;
				s32 superChargeFrame = s_superCharge ? 0 : 1;
				// This is always true if super charge is *not* active otherwise
				// it is true every other frame.
				if (superChargeFrame | (s_fireFrame & 1))
				{
					*s_curPlayerWeapon->ammo = pickup_addToValue(
						*s_curPlayerWeapon->ammo,
						-s_curPlayerWeapon->primaryFireConsumption,
						getMaxAmmo(s_curPlayerWeapon->ammo));
				}

				ProjectileLogic* projLogic;
				fixed16_16 yPos = s_playerObject->posWS.y - s_playerObject->worldHeight + s_headwaveVerticalOffset;
				projLogic = (ProjectileLogic*)createProjectile(s_curPlayerWeapon->primaryProjectile, s_playerObject->sector, s_playerObject->posWS.x, yPos, s_playerObject->posWS.z, s_playerObject);
				projLogic->flags &= ~PROJFLAG_CAMERA_PASS_SOUND;
				projLogic->prevColObj = s_playerObject;
				if (s_oneHitKillEnabled) { applyOneHitKillCheat(projLogic); }

				if (targetFound)
				{
					proj_setYawPitch(projLogic, s_wpnPitchSin, s_wpnPitchCos, s_autoAimDirX, s_autoAimDirZ);

					SecObject* projObj = projLogic->logic.obj;
					projObj->pitch = s_weaponFirePitch;
					projObj->yaw = s_weaponFireYaw;
				}
				else
				{
					proj_setTransform(projLogic, s_weaponFirePitch, s_weaponFireYaw);
				}

				if (canFire)
				{
					vec3_fixed inVec2 = { 0 };
					if (canFire > 1)
					{
						// TODO: This doesn't seem to be hit in the base game.
						assert(0);
					}

					Tick v0 = (s_superCharge) ? 7 : 14;
					Tick v1 = (s_superCharge) ? 3 : 7;
					Tick v2 = (s_superCharge) ? 3 : 7;
					inVec2.z = intToFixed16(s32(float(v0 + v1 + v2) * 1.71f));

					vec3_fixed outVec2;
					rotateVectorM3x3(&inVec2, &outVec2, mtx2);
					// First try using the outVec value computed above.
					projLogic->delta = outVec;
					tfe_adjustWeaponCollisionSpeed(projLogic);

					ProjectileHitType hitType = proj_handleMovement(projLogic);
					if (!handleProjectileHit(projLogic, hitType))
					{
						// If that fails, then try the new value.
						projLogic->delta = outVec2;
						tfe_adjustWeaponCollisionSpeed(projLogic);

						hitType = proj_handleMovement(projLogic);
						fire = handleProjectileHit(projLogic, hitType);
					}
				}
				else
				{
					projLogic->delta = outVec;
					tfe_adjustWeaponCollisionSpeed(projLogic);

					ProjectileHitType hitType = proj_handleMovement(projLogic);
					handleProjectileHit(projLogic, hitType);
				}
				tfe_restoreWeaponCollisionSpeed(projLogic);
			}
			task_localBlockEnd;

			// Animation.
			for (taskCtx->iFrame = 0; taskCtx->iFrame < s_rifleNumFrames; taskCtx->iFrame++)
			{
				s_curPlayerWeapon->frame = s_rifleAnim[taskCtx->iFrame].waxFrame;
				s_weaponLight = s_rifleAnim[taskCtx->iFrame].weaponLight;
				taskCtx->delay = (s_superCharge) ? s_rifleAnim[taskCtx->iFrame].delaySupercharge : s_rifleAnim[taskCtx->iFrame].delayNormal;

				do
				{
					task_yield(taskCtx->delay);
					task_callTaskFunc(weapon_handleState);
				} while (msg != MSG_RUN_TASK);
			}

			s_canFireWeaponPrim = 1;
			s_canFireWeaponSec = 1;
		}
		else  // Out of Ammo
		{
			if (s_outOfAmmoSndId)
			{
				sound_stop(s_outOfAmmoSndId);
			}
			s_outOfAmmoSndId = sound_play(s_pistolEmptySndSrc);

			taskCtx->delay = (s_superCharge) ? 3 : 7;
			s_curPlayerWeapon->frame = 0;
			do
			{
				task_yield(taskCtx->delay);
				task_callTaskFunc(weapon_handleState);
			} while (msg != MSG_RUN_TASK);

			s_canFireWeaponPrim = 0;
			s_canFireWeaponSec = 0;
			// if (s_weaponAutoMount2)
			// {
				// func_1ece78();
			// }
		}

		task_end;
	}

	void weaponFire_thermalDetonator(MessageType msg)
	{
		struct LocalContext
		{
			Tick delay;
			Tick startTick;
			s32  iFrame;
		};
		task_begin_ctx;

		if (*s_curPlayerWeapon->ammo)
		{
			s_canFireWeaponPrim = 0;
			s_canFireWeaponSec = 0;
			*s_curPlayerWeapon->ammo = pickup_addToValue(
				*s_curPlayerWeapon->ammo,
				-s_curPlayerWeapon->primaryFireConsumption,
				getMaxAmmo(s_curPlayerWeapon->ammo));

			// Weapon Frame 0.
			s_curPlayerWeapon->frame = s_thermalDetAnim[0].waxFrame;
			do
			{
				task_yield(s_thermalDetAnim[0].delayNormal);
				task_callTaskFunc(weapon_handleState);
			} while (msg != MSG_RUN_TASK);

			s_curPlayerWeapon->xOffset = 100;
			s_curPlayerWeapon->yOffset = 100;
			taskCtx->startTick = s_curTick;
			task_callTaskFunc(weapon_handleState2);

			// Wait until the player is no longer "shooting"
			// This holds the thermal detonator in place.
			while (s_isShooting)
			{
				task_yield(TASK_NO_DELAY);
				task_callTaskFunc(weapon_handleState);
			};

			task_localBlockBegin;
			task_callTaskFunc(weapon_handleState2);
			fixed16_16 dt = intToFixed16(s_curTick - taskCtx->startTick);

			// Wakeup AI within range
			if (s_curPlayerWeapon->wakeupRange)
			{
				vec3_fixed origin = { s_playerObject->posWS.x, s_playerObject->posWS.y, s_playerObject->posWS.z };
				collision_effectObjectsInRangeXZ(s_playerObject->sector, s_curPlayerWeapon->wakeupRange, origin, actor_sendWakeupMsg, s_playerObject, ETFLAG_AI_ACTOR);
			}

			fixed16_16 yPos = s_playerObject->posWS.y - s_playerObject->worldHeight + s_headwaveVerticalOffset;
			ProjectileLogic* proj = (ProjectileLogic*)createProjectile(s_curPlayerWeapon->primaryProjectile, s_playerObject->sector, s_playerObject->posWS.x, yPos, s_playerObject->posWS.z, s_playerObject);
			proj->flags &= ~PROJFLAG_CAMERA_PASS_SOUND;
			proj->prevColObj = s_playerObject;

			// Calculate projectile speed and duration.
			if (s_secondaryFire)
			{
				dt = max(dt, FIXED(23));
			}
			else
			{
				proj->duration = 0xffffffff;
				proj->bounceCnt = 0;
				dt = max(dt, FIXED(58));
			}
			dt = min(dt, FIXED(116));
			proj->speed = mul16(div16(dt, FIXED(116)), FIXED(100));

			// Calculate projectile transform.
			s32 baseVariation = s_curPlayerWeapon->variation & 0xffff;
			s32 pitchVariation = random(baseVariation * 2) - baseVariation;
			s_weaponFirePitch = (s_playerObject->pitch >> 1) + 0x638 + pitchVariation;

			s32 yawVariation = random(baseVariation * 2) - baseVariation;
			s_weaponFireYaw = s_playerObject->yaw + yawVariation;

			proj_setTransform(proj, s_weaponFirePitch, s_weaponFireYaw);

			// Calculate initial projectile delta and handle up close collision.
			fixed16_16 mtx[9];
			weapon_computeMatrix(mtx, -(s_playerObject->pitch + 0x638), -s_playerObject->yaw);

			vec3_fixed inVec = { FIXED(2), FIXED(2), FIXED(2) };
			rotateVectorM3x3(&inVec, &proj->delta, mtx);
			tfe_adjustWeaponCollisionSpeed(proj);

			ProjectileHitType hitType = proj_handleMovement(proj);
			handleProjectileHit(proj, hitType);

			tfe_restoreWeaponCollisionSpeed(proj);
			task_localBlockEnd;

			// Handle animation
			s_curPlayerWeapon->xOffset = 0;
			s_curPlayerWeapon->yOffset = 0;

			// The initial frame is the same regardless of ammo.
			s_curPlayerWeapon->frame = s_thermalDetAnim[1].waxFrame;
			s_weaponLight = s_thermalDetAnim[1].weaponLight;
			taskCtx->delay = s_thermalDetAnim[1].delayNormal;
			do
			{
				task_yield(taskCtx->delay);
				task_callTaskFunc(weapon_handleState);
			} while (msg != MSG_RUN_TASK);

			if (*s_curPlayerWeapon->ammo)
			{
				for (taskCtx->iFrame = 2; taskCtx->iFrame <= 3; taskCtx->iFrame++)
				{
					s_curPlayerWeapon->frame = s_thermalDetAnim[taskCtx->iFrame].waxFrame;
					s_weaponLight = s_thermalDetAnim[taskCtx->iFrame].weaponLight;
					taskCtx->delay = s_thermalDetAnim[taskCtx->iFrame].delayNormal;
					do
					{
						task_yield(taskCtx->delay);
						task_callTaskFunc(weapon_handleState);
					} while (msg != MSG_RUN_TASK);
				}
			}
			else // no more ammo, show empty hand
			{
				s_curPlayerWeapon->frame = s_thermalDetAnim[4].waxFrame;
				s_weaponLight = s_thermalDetAnim[4].weaponLight;
				taskCtx->delay = s_thermalDetAnim[4].delayNormal;
				do
				{
					task_yield(taskCtx->delay);
					task_callTaskFunc(weapon_handleState);
				} while (msg != MSG_RUN_TASK);
			}

			s_canFireWeaponPrim = 1;
			s_canFireWeaponSec = 1;
		}
		else  // Out of Ammo
		{
			if (s_weaponAutoMount2)
			{
				//func_1ece78();
				//return;
			}
			
			s_lastWeapon = s_curWeapon;
			s_curWeapon = WPN_FIST;
			s_playerInfo.curWeapon = s_prevWeapon;
			if (s_weaponOffAnim)
			{
				task_callTaskFunc(weapon_handleOffAnimation);
			}
			weapon_setNext(s_curWeapon);
			task_callTaskFunc(weapon_handleOnAnimation);
		}

		task_end;
	}

	void weaponFire_repeater(MessageType msg)
	{
		struct LocalContext
		{
			Tick delay;
			s32  iFrame;
		};
		task_begin_ctx;

		if (s_secondaryFire)
		{
			if (*s_curPlayerWeapon->ammo)
			{
				// Sound effect
				if (s_repeaterFireSndID1)
				{
					sound_stop(s_repeaterFireSndID1);
				}
				s_repeaterFireSndID1 = sound_play(s_repeater1SndSrc);

				// Wakeup AI within range
				if (s_curPlayerWeapon->wakeupRange)
				{
					vec3_fixed origin = { s_playerObject->posWS.x, s_playerObject->posWS.y, s_playerObject->posWS.z };
					collision_effectObjectsInRangeXZ(s_playerObject->sector, s_curPlayerWeapon->wakeupRange, origin, actor_sendWakeupMsg, s_playerObject, ETFLAG_AI_ACTOR);
				}

				// Initial animation frame
				taskCtx->delay = s_superCharge ? s_repeaterSecondaryAnim[0].delaySupercharge : s_repeaterSecondaryAnim[0].delayNormal;
				s_curPlayerWeapon->frame = s_repeaterSecondaryAnim[0].waxFrame;
				do
				{
					task_yield(taskCtx->delay);
					task_callTaskFunc(weapon_handleState);
				} while (msg != MSG_RUN_TASK);

				task_localBlockBegin;
				// Aim projectiles
				fixed16_16 mtx[9];
				weapon_computeMatrix(mtx, -s_playerObject->pitch, -s_playerObject->yaw);

				s32 baseVariation = s_curPlayerWeapon->variation & 0xffff;
				s32 variation = random(baseVariation * 2) - baseVariation;
				s_weaponFirePitch = s_playerObject->pitch + variation;

				variation = random(variation * 2) - variation;
				s_weaponFireYaw = s_playerObject->yaw + variation;

				s32 canFire = s_canFireWeaponSec;
				s_canFireWeaponSec = 0;

				fixed16_16 mtx2[9];
				if (canFire > 1)
				{
					weapon_computeMatrix(mtx2, -s_weaponFirePitch, -s_weaponFireYaw);
				}

				// Spawn projectiles (x3)
				fixed16_16 fire = intToFixed16(canFire);
				while (canFire)
				{
					if (!*s_curPlayerWeapon->ammo)
					{
						break;
					}
					canFire--;
					fire -= ONE_16;
					s32 superChargeFrame = s_superCharge ? 0 : 1;
					// This is always true if super charge is *not* active otherwise
					// it is true every other frame.
					if (superChargeFrame | (s_fireFrame & 1))
					{
						*s_curPlayerWeapon->ammo = pickup_addToValue(
							*s_curPlayerWeapon->ammo,
							-s_curPlayerWeapon->secondaryFireConsumption,
							getMaxAmmo(s_curPlayerWeapon->ammo));
					}
					
					fixed16_16 yPos = s_playerObject->posWS.y - s_playerObject->worldHeight + s_headwaveVerticalOffset;
					ProjectileLogic* proj[3];
					for (s32 i = 0; i < 3; i++)
					{
						proj[i] = (ProjectileLogic*)createProjectile(s_curPlayerWeapon->secondaryProjectile, s_playerObject->sector, s_playerObject->posWS.x, yPos, s_playerObject->posWS.z, s_playerObject);
						proj[i]->flags &= ~PROJFLAG_CAMERA_PASS_SOUND;
						proj_setTransform(proj[i], s_weaponFirePitch + c_repeaterPitchOffset[i], s_weaponFireYaw + c_repeaterYawOffset[i]);
						proj[i]->prevColObj = s_playerObject;
						if (s_oneHitKillEnabled) { applyOneHitKillCheat(proj[i]); }
					}

					for (s32 i = 0; i < 3; i++)
					{
						vec3_fixed inVec = { c_repeaterDelta[1 + i*3], c_repeaterDelta[2 + i*3], c_repeaterDelta[i*3] };
						vec3_fixed outVec;
						rotateVectorM3x3(&inVec, &outVec, mtx);

						if (canFire)
						{
							// TODO
							assert(0);
						}
						else
						{
							proj[i]->delta = outVec;
							tfe_adjustWeaponCollisionSpeed(proj[i]);

							// Initial movement to make sure the player isn't too close to a wall or adjoin.
							ProjectileHitType hitType = proj_handleMovement(proj[i]);
							handleProjectileHit(proj[i], hitType);
							tfe_restoreWeaponCollisionSpeed(proj[i]);
						}
					}
				}
				task_localBlockEnd;

				// Animation
				for (taskCtx->iFrame = 1; taskCtx->iFrame < s_repeaterSecondaryNumFrames; taskCtx->iFrame++)
				{
					s_curPlayerWeapon->frame = s_repeaterSecondaryAnim[taskCtx->iFrame].waxFrame;
					s_weaponLight = s_repeaterSecondaryAnim[taskCtx->iFrame].weaponLight;
					taskCtx->delay = (s_superCharge) ? s_repeaterSecondaryAnim[taskCtx->iFrame].delaySupercharge : s_repeaterSecondaryAnim[taskCtx->iFrame].delayNormal;
					do
					{
						task_yield(taskCtx->delay);
						task_callTaskFunc(weapon_handleState);
					} while (msg != MSG_RUN_TASK);
				}
				s_canFireWeaponSec = 1;
			}
			else	// out of ammo
			{
				if (s_repeaterFireSndID)
				{
					sound_stop(s_repeaterFireSndID);
					s_repeaterFireSndID = 0;
				}
				if (s_repeaterOutOfAmmoSndId)
				{
					sound_stop(s_repeaterOutOfAmmoSndId);
				}
				s_repeaterOutOfAmmoSndId = sound_play(s_repeaterEmptySndSrc);

				taskCtx->delay = (s_superCharge) ? 3 : 7;
				s_curPlayerWeapon->frame = 0;
				do
				{
					task_yield(taskCtx->delay);
					task_callTaskFunc(weapon_handleState);
				} while (msg != MSG_RUN_TASK);

				s_canFireWeaponPrim = 0;
				s_canFireWeaponSec = 0;

				/* TODO:
				if (s_weaponAutoMount2)
				{
					func_1ece78();
				}*/
			}
		}
		else  // Primary Fire.
		{
			if (*s_curPlayerWeapon->ammo)
			{
				// Sound effect (includes looping sound effect)
				if (!s_repeaterFireSndID)
				{
					if (s_repeaterFireSndID1)
					{
						sound_stop(s_repeaterFireSndID1);
					}
					s_repeaterFireSndID1 = sound_play(s_repeater1SndSrc);
					s_repeaterFireSndID  = sound_play(s_repeaterSndSrc);
				}

				// Wakeup AI within range
				if (s_curPlayerWeapon->wakeupRange)
				{
					vec3_fixed origin = { s_playerObject->posWS.x, s_playerObject->posWS.y, s_playerObject->posWS.z };
					collision_effectObjectsInRangeXZ(s_playerObject->sector, s_curPlayerWeapon->wakeupRange, origin, actor_sendWakeupMsg, s_playerObject, ETFLAG_AI_ACTOR);
				}

				// Initial animation frame
				taskCtx->delay = s_superCharge ? s_repeaterPrimaryAnim[0].delaySupercharge : s_repeaterPrimaryAnim[0].delayNormal;
				s_curPlayerWeapon->frame = s_repeaterPrimaryAnim[0].waxFrame;
				do
				{
					task_yield(taskCtx->delay);
					task_callTaskFunc(weapon_handleState);
				} while (msg != MSG_RUN_TASK);

				task_localBlockBegin;
				// Aim projectile
				fixed16_16 mtx[9];
				weapon_computeMatrix(mtx, -s_playerObject->pitch, -s_playerObject->yaw);

				vec3_fixed inVec =
				{
					0xcccc,	// 0.8
					0xfae1, // 0.98
					0x50000 // 5.0
				};
				vec3_fixed outVec;
				rotateVectorM3x3(&inVec, &outVec, mtx);

				fixed16_16 xPos = s_playerObject->posWS.x + outVec.x;
				fixed16_16 yPos = outVec.y + s_playerObject->posWS.y - s_playerObject->worldHeight + s_headwaveVerticalOffset;
				fixed16_16 zPos = s_playerObject->posWS.z + outVec.z;

				s32 baseVariation = s_curPlayerWeapon->variation & 0xffff;
				JBool targetFound = computeAutoaim(xPos, yPos, zPos, s_playerObject->pitch, s_playerObject->yaw, baseVariation);
				if (!targetFound)
				{
					s32 variation = random(baseVariation * 2) - baseVariation;
					s_weaponFirePitch = s_playerObject->pitch + variation;

					variation = random(variation * 2) - variation;
					s_weaponFireYaw = s_playerObject->yaw + variation;
				}

				s32 canFire = s_canFireWeaponPrim;
				fixed16_16 mtx2[9];
				s_canFireWeaponPrim = 0;
				if (canFire > 1)
				{
					weapon_computeMatrix(mtx2, -s_weaponFirePitch, -s_weaponFireYaw);
				}

				// Spawn projectile
				fixed16_16 fire = intToFixed16(canFire);
				while (canFire)
				{
					if (!*s_curPlayerWeapon->ammo)
					{
						break;
					}
					canFire--;
					fire -= ONE_16;
					s32 superChargeFrame = s_superCharge ? 0 : 1;
					// This is always true if super charge is *not* active otherwise
					// it is true every other frame.
					if (superChargeFrame | (s_fireFrame & 1))
					{
						*s_curPlayerWeapon->ammo = pickup_addToValue(
							*s_curPlayerWeapon->ammo,
							-s_curPlayerWeapon->primaryFireConsumption,
							getMaxAmmo(s_curPlayerWeapon->ammo));
					}

					fixed16_16 yPos = s_playerObject->posWS.y - s_playerObject->worldHeight + s_headwaveVerticalOffset;
					ProjectileLogic* projLogic = (ProjectileLogic*)createProjectile(s_curPlayerWeapon->primaryProjectile, s_playerObject->sector, s_playerObject->posWS.x, yPos, s_playerObject->posWS.z, s_playerObject);
					projLogic->flags &= ~PROJFLAG_CAMERA_PASS_SOUND;
					projLogic->prevColObj = s_playerObject;
					if (s_oneHitKillEnabled) { applyOneHitKillCheat(projLogic); }

					if (targetFound)
					{
						proj_setYawPitch(projLogic, s_wpnPitchSin, s_wpnPitchCos, s_autoAimDirX, s_autoAimDirZ);

						SecObject* projObj = projLogic->logic.obj;
						projObj->pitch = s_weaponFirePitch;
						projObj->yaw = s_weaponFireYaw;
					}
					else
					{
						proj_setTransform(projLogic, s_weaponFirePitch, s_weaponFireYaw);
					}

					if (canFire)
					{
						// TODO
						assert(0);
					}
					else
					{
						projLogic->delta = outVec;
						tfe_adjustWeaponCollisionSpeed(projLogic);

						// Initial movement to make sure the player isn't too close to a wall or adjoin.
						ProjectileHitType hitType = proj_handleMovement(projLogic);
						handleProjectileHit(projLogic, hitType);
						tfe_restoreWeaponCollisionSpeed(projLogic);
					}
				}
				task_localBlockEnd;

				// Animation
				for (taskCtx->iFrame = 1; taskCtx->iFrame < s_repeaterPrimaryNumFrames; taskCtx->iFrame++)
				{
					taskCtx->delay = s_superCharge ? s_repeaterPrimaryAnim[taskCtx->iFrame].delaySupercharge : s_repeaterPrimaryAnim[taskCtx->iFrame].delayNormal;
					s_weaponLight = s_repeaterPrimaryAnim[taskCtx->iFrame].weaponLight;
					s_curPlayerWeapon->frame = s_repeaterPrimaryAnim[taskCtx->iFrame].waxFrame;
					do
					{
						task_yield(taskCtx->delay);
						task_callTaskFunc(weapon_handleState);
					} while (msg != MSG_RUN_TASK);
				}

				s_weaponLight = 0;

				// The repeater has an extra delay before it can be fired again.
				if (s_isShooting)
				{
					taskCtx->delay = (s_superCharge) ? 5 : 10;
					do
					{
						task_yield(taskCtx->delay);
						task_callTaskFunc(weapon_handleState);
					} while (msg != MSG_RUN_TASK);
				}

				s_canFireWeaponPrim = 1;
			}
			else	// out of ammo
			{
				if (s_repeaterFireSndID)
				{
					sound_stop(s_repeaterFireSndID);
					s_repeaterFireSndID = 0;
				}
				if (s_repeaterOutOfAmmoSndId)
				{
					sound_stop(s_repeaterOutOfAmmoSndId);
				}
				s_repeaterOutOfAmmoSndId = sound_play(s_repeaterEmptySndSrc);

				taskCtx->delay = (s_superCharge) ? 3 : 7;
				s_curPlayerWeapon->frame = 0;
				do
				{
					task_yield(taskCtx->delay);
					task_callTaskFunc(weapon_handleState);
				} while (msg != MSG_RUN_TASK);

				s_canFireWeaponPrim = 0;
				s_canFireWeaponSec = 0;

				/* TODO:
				if (s_weaponAutoMount2)
				{
					func_1ece78();
				}*/
			}
		}

		task_end;
	}

	void weaponFire_fusion(MessageType msg)
	{
		struct LocalContext
		{
			Tick delay;
			s32  iFrame;
		};
		task_begin_ctx;

		if (s_secondaryFire)
		{
			if (*s_curPlayerWeapon->ammo)
			{
				// Sound effect
				if (s_fusionFireSndID)
				{
					sound_stop(s_fusionFireSndID);
				}
				s_fusionFireSndID = sound_play(s_fusion1SndSrc);

				// Wakeup AI within range
				if (s_curPlayerWeapon->wakeupRange)
				{
					vec3_fixed origin = { s_playerObject->posWS.x, s_playerObject->posWS.y, s_playerObject->posWS.z };
					collision_effectObjectsInRangeXZ(s_playerObject->sector, s_curPlayerWeapon->wakeupRange, origin, actor_sendWakeupMsg, s_playerObject, ETFLAG_AI_ACTOR);
				}

				task_localBlockBegin;
				// Aim projectiles
				fixed16_16 mtx[9];
				weapon_computeMatrix(mtx, -s_playerObject->pitch, -s_playerObject->yaw);

				s32 baseVariation = s_curPlayerWeapon->variation & 0xffff;
				s32 variation = random(baseVariation * 2) - baseVariation;
				s_weaponFirePitch = s_playerObject->pitch + variation;

				variation = random(variation * 2) - variation;
				s_weaponFireYaw = s_playerObject->yaw + variation;

				s32 canFire = s_canFireWeaponSec;
				s_canFireWeaponSec = 0;

				fixed16_16 mtx2[9];
				if (canFire > 1)
				{
					weapon_computeMatrix(mtx2, -s_weaponFirePitch, -s_weaponFireYaw);
				}

				// Spawn projectiles
				fixed16_16 fire = intToFixed16(canFire);
				while (canFire)
				{
					if (!*s_curPlayerWeapon->ammo)
					{
						break;
					}
					canFire--;
					fire -= ONE_16;
					s32 superChargeFrame = s_superCharge ? 0 : 1;
					// This is always true if super charge is *not* active otherwise
					// it is true every other frame.
					if (superChargeFrame | (s_fireFrame & 1))
					{
						*s_curPlayerWeapon->ammo = pickup_addToValue(
							*s_curPlayerWeapon->ammo,
							-s_curPlayerWeapon->secondaryFireConsumption,
							getMaxAmmo(s_curPlayerWeapon->ammo));
					}

					fixed16_16 yPos = s_playerObject->posWS.y - s_playerObject->worldHeight + s_headwaveVerticalOffset;
					ProjectileLogic* proj[4];
					for (s32 i = 0; i < 4; i++)
					{
						proj[i] = (ProjectileLogic*)createProjectile(s_curPlayerWeapon->secondaryProjectile, s_playerObject->sector, s_playerObject->posWS.x, yPos, s_playerObject->posWS.z, s_playerObject);
						proj[i]->flags &= ~PROJFLAG_CAMERA_PASS_SOUND;
						proj_setTransform(proj[i], s_weaponFirePitch, s_weaponFireYaw + s_fusionYawOffset[i]);
						proj[i]->prevColObj = s_playerObject;
						if (s_oneHitKillEnabled) { applyOneHitKillCheat(proj[i]); }
					}

					for (s32 i = 0; i < 4; i++)
					{
						vec3_fixed inVec = { s_fusionOffset[1 + i*3], s_fusionOffset[2 + i*3], s_fusionOffset[i*3] };
						vec3_fixed outVec;
						rotateVectorM3x3(&inVec, &outVec, mtx);

						if (canFire)
						{
							// TODO
							assert(0);
						}
						else
						{
							proj[i]->delta = outVec;
							tfe_adjustWeaponCollisionSpeed(proj[i]);

							// Initial movement to make sure the player isn't too close to a wall or adjoin.
							ProjectileHitType hitType = proj_handleMovement(proj[i]);
							handleProjectileHit(proj[i], hitType);
							tfe_restoreWeaponCollisionSpeed(proj[i]);
						}
					}
				}
				task_localBlockEnd;

				// Animation
				for (taskCtx->iFrame = 0; taskCtx->iFrame < s_fusionSecondaryNumFrames; taskCtx->iFrame++)
				{
					s_curPlayerWeapon->frame = s_fusionSecondaryAnim[taskCtx->iFrame].waxFrame;
					s_weaponLight = s_fusionSecondaryAnim[taskCtx->iFrame].weaponLight;
					taskCtx->delay = (s_superCharge) ? s_fusionSecondaryAnim[taskCtx->iFrame].delaySupercharge : s_fusionSecondaryAnim[taskCtx->iFrame].delayNormal;
					do
					{
						task_yield(taskCtx->delay);
						task_callTaskFunc(weapon_handleState);
					} while (msg != MSG_RUN_TASK);
				}

				s_canFireWeaponSec = 1;
			}
			else	// out of ammo
			{
				if (s_fusionOutOfAmmoSndID)
				{
					sound_stop(s_fusionOutOfAmmoSndID);
				}
				s_fusionOutOfAmmoSndID = sound_play(s_fusion2SndSrc);

				taskCtx->delay = (s_superCharge) ? 3 : 7;
				s_curPlayerWeapon->frame = 0;
				do
				{
					task_yield(taskCtx->delay);
					task_callTaskFunc(weapon_handleState);
				} while (msg != MSG_RUN_TASK);

				s_canFireWeaponPrim = 0;
				s_canFireWeaponSec = 0;

				/* TODO:
				if (s_weaponAutoMount2)
				{
					func_1ece78();
				}*/
			}
		}
		else  // Primary Fire.
		{
			if (*s_curPlayerWeapon->ammo)
			{
				// Sound effect
				if (s_fusionFireSndID)
				{
					sound_stop(s_fusionFireSndID);
				}
				s_fusionFireSndID = sound_play(s_fusion1SndSrc);

				// Wakeup AI within range
				if (s_curPlayerWeapon->wakeupRange)
				{
					vec3_fixed origin = { s_playerObject->posWS.x, s_playerObject->posWS.y, s_playerObject->posWS.z };
					collision_effectObjectsInRangeXZ(s_playerObject->sector, s_curPlayerWeapon->wakeupRange, origin, actor_sendWakeupMsg, s_playerObject, ETFLAG_AI_ACTOR);
				}

				task_localBlockBegin;
				// Aim projectiles
				fixed16_16 mtx[9];
				weapon_computeMatrix(mtx, -s_playerObject->pitch, -s_playerObject->yaw);

				s32 canFire = s_canFireWeaponPrim;
				fixed16_16 mtx2[9];
				s_canFireWeaponPrim = 0;
				if (canFire > 1)
				{
					weapon_computeMatrix(mtx2, -s_weaponFirePitch, -s_weaponFireYaw);
				}

				// Spawn projectiles
				fixed16_16 fire = intToFixed16(canFire);
				while (canFire)
				{
					if (!*s_curPlayerWeapon->ammo)
					{
						break;
					}
					canFire--;
					fire -= ONE_16;

					vec3_fixed inVec =
					{
						s_fusionOffset[(s_fusionCylinder-1)*3 + 1],
						s_fusionOffset[(s_fusionCylinder-1)*3 + 2],
						s_fusionOffset[(s_fusionCylinder-1)*3 + 0]
					};
					vec3_fixed outVec;
					rotateVectorM3x3(&inVec, &outVec, mtx);

					fixed16_16 xPos = s_playerObject->posWS.x + outVec.x;
					fixed16_16 yPos = outVec.y + s_playerObject->posWS.y - s_playerObject->worldHeight + s_headwaveVerticalOffset;
					fixed16_16 zPos = s_playerObject->posWS.z + outVec.z;

					s32 baseVariation = s_curPlayerWeapon->variation & 0xffff;
					JBool targetFound = computeAutoaim(xPos, yPos, zPos, s_playerObject->pitch, s_playerObject->yaw, baseVariation);
					if (!targetFound)
					{
						s32 variation = random(baseVariation * 2) - baseVariation;
						s_weaponFirePitch = s_playerObject->pitch + variation;

						variation = random(baseVariation * 2) - baseVariation;
						s_weaponFireYaw = s_playerObject->yaw + variation;
					}

					s32 superChargeFrame = s_superCharge ? 0 : 1;
					// This is always true if super charge is *not* active otherwise
					// it is true every other frame.
					if (superChargeFrame | (s_fireFrame & 1))
					{
						*s_curPlayerWeapon->ammo = pickup_addToValue(
							*s_curPlayerWeapon->ammo,
							-s_curPlayerWeapon->primaryFireConsumption,
							getMaxAmmo(s_curPlayerWeapon->ammo));
					}

					fixed16_16 yPlayerPos = s_playerObject->posWS.y - s_playerObject->worldHeight + s_headwaveVerticalOffset;
					ProjectileLogic* projLogic = (ProjectileLogic*)createProjectile(s_curPlayerWeapon->primaryProjectile, s_playerObject->sector, s_playerObject->posWS.x, yPlayerPos, s_playerObject->posWS.z, s_playerObject);
					projLogic->flags &= ~PROJFLAG_CAMERA_PASS_SOUND;
					projLogic->prevColObj = s_playerObject;
					if (s_oneHitKillEnabled) { applyOneHitKillCheat(projLogic); }

					if (targetFound)
					{
						proj_setYawPitch(projLogic, s_wpnPitchSin, s_wpnPitchCos, s_autoAimDirX, s_autoAimDirZ);

						SecObject* projObj = projLogic->logic.obj;
						projObj->pitch = s_weaponFirePitch;
						projObj->yaw = s_weaponFireYaw;
					}
					else
					{
						proj_setTransform(projLogic, s_weaponFirePitch, s_weaponFireYaw);
					}

					if (canFire)
					{
						// TODO
						assert(0);
					}
					else
					{
						projLogic->delta = outVec;
						tfe_adjustWeaponCollisionSpeed(projLogic);

						// Initial movement to make sure the player isn't too close to a wall or adjoin.
						ProjectileHitType hitType = proj_handleMovement(projLogic);
						handleProjectileHit(projLogic, hitType);
						tfe_restoreWeaponCollisionSpeed(projLogic);
					}
				}
				task_localBlockEnd;

				// Animation
				taskCtx->delay = s_superCharge ? s_fusionPrimaryAnim[s_fusionCylinder].delaySupercharge : s_fusionPrimaryAnim[s_fusionCylinder].delayNormal;
				s_weaponLight = s_fusionPrimaryAnim[s_fusionCylinder].weaponLight;
				s_curPlayerWeapon->frame = s_fusionPrimaryAnim[s_fusionCylinder].waxFrame;
				do
				{
					task_yield(taskCtx->delay);
					task_callTaskFunc(weapon_handleState);
				} while (msg != MSG_RUN_TASK);

				if (s_fusionCycleForward)
				{
					s_fusionCylinder++;
					if (s_fusionCylinder > 4)
					{
						s_fusionCylinder = 3;
						s_fusionCycleForward = JFALSE;
					}
				}
				else
				{
					s_fusionCylinder--;
					if (s_fusionCylinder < 1)
					{
						s_fusionCylinder = 2;
						s_fusionCycleForward = JTRUE;
					}
				}

				taskCtx->delay = s_superCharge ? s_fusionPrimaryAnim[0].delaySupercharge : s_fusionPrimaryAnim[0].delayNormal;
				s_weaponLight = s_fusionPrimaryAnim[0].weaponLight;
				s_curPlayerWeapon->frame = s_fusionPrimaryAnim[0].waxFrame;
				do
				{
					task_yield(taskCtx->delay);
					task_callTaskFunc(weapon_handleState);
				} while (msg != MSG_RUN_TASK);

				s_canFireWeaponPrim = 1;
			}
			else	// out of ammo
			{
				if (s_fusionOutOfAmmoSndID)
				{
					sound_stop(s_fusionOutOfAmmoSndID);
				}
				s_fusionOutOfAmmoSndID = sound_play(s_fusion2SndSrc);

				taskCtx->delay = (s_superCharge) ? 3 : 7;
				s_curPlayerWeapon->frame = 0;
				do
				{
					task_yield(taskCtx->delay);
					task_callTaskFunc(weapon_handleState);
				} while (msg != MSG_RUN_TASK);

				s_canFireWeaponPrim = 0;
				s_canFireWeaponSec = 0;

				/* TODO:
				if (s_weaponAutoMount2)
				{
					func_1ece78();
				}*/
			}
		}

		task_end;
	}

	void weaponFire_mortar(MessageType msg)
	{
		struct LocalContext
		{
			Tick delay;
			s32  iFrame;
		};
		task_begin_ctx;

		if (*s_curPlayerWeapon->ammo)
		{
			// Sound effect
			task_localBlockBegin;
			if (s_mortarFireSndID)
			{
				sound_stop(s_mortarFireSndID);
			}
			s_mortarFireSndID = sound_play(s_mortarFireSndSrc);

			// Wakeup AI within range
			if (s_curPlayerWeapon->wakeupRange)
			{
				vec3_fixed origin = { s_playerObject->posWS.x, s_playerObject->posWS.y, s_playerObject->posWS.z };
				collision_effectObjectsInRangeXZ(s_playerObject->sector, s_curPlayerWeapon->wakeupRange, origin, actor_sendWakeupMsg, s_playerObject, ETFLAG_AI_ACTOR);
			}

			// Aim projectile
			fixed16_16 mtx[9];
			weapon_computeMatrix(mtx, -s_playerObject->pitch, -s_playerObject->yaw);

			s32 canFire = s_canFireWeaponPrim;
			s_canFireWeaponPrim = 0;

			s32 wpnVariation = s_curPlayerWeapon->variation & 0xffff;
			s32 variation;
			if (canFire > 1)
			{
				variation = random(wpnVariation * 2) - wpnVariation + 0x333;
				s_weaponFirePitch = s_playerObject->pitch + (variation >> 2);
			}
			else
			{
				variation = random(wpnVariation * 2) - wpnVariation + 0x333;
				s_weaponFirePitch = s_playerObject->pitch + variation;
			}

			variation = random(wpnVariation * 2) - wpnVariation;
			s_weaponFireYaw = s_playerObject->yaw + variation;

			vec3_fixed inVec =
			{
				0xb333,	 // 0.7
				0x1b333, // 1.7
				0x20000  // 2.0
			};
			vec3_fixed outVec;
			rotateVectorM3x3(&inVec, &outVec, mtx);

			fixed16_16 mtx2[9];
			if (canFire > 1)
			{
				weapon_computeMatrix(mtx2, -s_weaponFirePitch, -s_weaponFireYaw);
			}

			// Spawn projectiles.
			fixed16_16 fire = intToFixed16(canFire);
			while (canFire)
			{
				if (*s_curPlayerWeapon->ammo == 0)
				{
					break;
				}
				fire -= ONE_16;
				canFire--;
				s32 superChargeFrame = s_superCharge ? 0 : 1;
				// This is always true if super charge is *not* active otherwise
				// it is true every other frame.
				if (superChargeFrame | (s_fireFrame & 1))
				{
					*s_curPlayerWeapon->ammo = pickup_addToValue(
						*s_curPlayerWeapon->ammo,
						-s_curPlayerWeapon->primaryFireConsumption,
						getMaxAmmo(s_curPlayerWeapon->ammo));
				}

				ProjectileLogic* projLogic;
				fixed16_16 yPos = s_playerObject->posWS.y - s_playerObject->worldHeight + s_headwaveVerticalOffset;
				projLogic = (ProjectileLogic*)createProjectile(s_curPlayerWeapon->primaryProjectile, s_playerObject->sector, s_playerObject->posWS.x, yPos, s_playerObject->posWS.z, s_playerObject);
				projLogic->flags &= ~PROJFLAG_CAMERA_PASS_SOUND;
				projLogic->prevColObj = s_playerObject;
				proj_setTransform(projLogic, s_weaponFirePitch, s_weaponFireYaw);
				
				if (canFire)
				{
					vec3_fixed worldUp = { 0 };
					if (canFire > 1)
					{
						// TODO: This doesn't seem to be hit in the base game.
						assert(0);
					}

					Tick v0 = (s_superCharge) ?  7 : 14;
					Tick v1 = (s_superCharge) ? 29 : 58;
					Tick v2 = (s_superCharge) ?  7 : 14;
					Tick v3 = (s_superCharge) ?  7 : 14;
					worldUp.z = intToFixed16(s32(float(v0 + v1 + v2 + v3) * 0.75f));
					
					vec3_fixed outVec2;
					rotateVectorM3x3(&worldUp, &outVec2, mtx2);
					// First try using the outVec value computed above.
					projLogic->delta = outVec;
					tfe_adjustWeaponCollisionSpeed(projLogic);

					ProjectileHitType hitType = proj_handleMovement(projLogic);
					if (!handleProjectileHit(projLogic, hitType))
					{
						// If that fails, then try the new value.
						projLogic->delta = outVec2;
						tfe_adjustWeaponCollisionSpeed(projLogic);

						hitType = proj_handleMovement(projLogic);
						fire = handleProjectileHit(projLogic, hitType);
					}
				}
				else
				{
					projLogic->delta = outVec;
					tfe_adjustWeaponCollisionSpeed(projLogic);

					ProjectileHitType hitType = proj_handleMovement(projLogic);
					handleProjectileHit(projLogic, hitType);
				}
				tfe_restoreWeaponCollisionSpeed(projLogic);
			}
			task_localBlockEnd;

			// Animation.
			for (taskCtx->iFrame = 0; taskCtx->iFrame < s_mortarNumFrames; taskCtx->iFrame++)
			{
				if (taskCtx->iFrame == 1)
				{
					if (s_mortarFireSndID2)
					{
						sound_stop(s_mortarFireSndID2);
					}
					s_mortarFireSndID2 = sound_play(s_mortarFireSndSrc2);
				}

				s_curPlayerWeapon->frame = s_mortarAnim[taskCtx->iFrame].waxFrame;
				s_weaponLight = s_mortarAnim[taskCtx->iFrame].weaponLight;
				taskCtx->delay = (s_superCharge) ? s_mortarAnim[taskCtx->iFrame].delaySupercharge : s_mortarAnim[taskCtx->iFrame].delayNormal;

				do
				{
					task_yield(taskCtx->delay);
					task_callTaskFunc(weapon_handleState);
				} while (msg != MSG_RUN_TASK);
			}

			s_canFireWeaponPrim = 1;
			s_canFireWeaponSec = 1;
		}
		else  // Out of Ammo
		{
			if (s_mortarOutofAmmoSndId)
			{
				sound_stop(s_mortarOutofAmmoSndId);
			}
			s_mortarOutofAmmoSndId = sound_play(s_mortarEmptySndSrc);

			taskCtx->delay = (s_superCharge) ? 3 : 7;
			s_curPlayerWeapon->frame = 0;
			do
			{
				task_yield(taskCtx->delay);
				task_callTaskFunc(weapon_handleState);
			} while (msg != MSG_RUN_TASK);

			s_canFireWeaponPrim = 0;
			s_canFireWeaponSec = 0;
			// if (s_weaponAutoMount2)
			// {
				// func_1ece78();
			// }
		}

		task_end;
	}

	void weaponFire_mine(MessageType msg)
	{
		task_begin;
		if (*s_curPlayerWeapon->ammo)
		{
			*s_curPlayerWeapon->ammo = pickup_addToValue(
				*s_curPlayerWeapon->ammo,
				-s_curPlayerWeapon->primaryFireConsumption,
				getMaxAmmo(s_curPlayerWeapon->ammo));
			
			// Wakeup AI within range
			if (s_curPlayerWeapon->wakeupRange)
			{
				vec3_fixed origin = { s_playerObject->posWS.x, s_playerObject->posWS.y, s_playerObject->posWS.z };
				collision_effectObjectsInRangeXZ(s_playerObject->sector, s_curPlayerWeapon->wakeupRange, origin, actor_sendWakeupMsg, s_playerObject, ETFLAG_AI_ACTOR);
			}

			// Animate (going down)
			s_weaponAnimState =
			{
				1,		// frame
				0, 0,	// offset
				0, 20,	// xSpeed, ySpeed
				5, 7	// frameCount, ticksPerFrame
			};
			task_callTaskFunc(weapon_animateOnOrOffscreen);

			task_localBlockBegin;
			fixed16_16 floorHeight, ceilHeight;
			sector_getObjFloorAndCeilHeight(s_playerObject->sector, s_playerObject->posWS.y, &floorHeight, &ceilHeight);

			ProjectileType type = (s_secondaryFire) ? s_curPlayerWeapon->secondaryProjectile : s_curPlayerWeapon->primaryProjectile;
			ProjectileLogic* mine = (ProjectileLogic*)createProjectile(type, s_playerObject->sector, s_playerObject->posWS.x, floorHeight, s_playerObject->posWS.z, s_playerObject);
			mine->vel = { 0, 0, 0 };

			// Sound effect
			if (s_mineSndId)
			{
				sound_stop(s_mineSndId);
			}
			s_mineSndId = sound_play(s_mineSndSrc);

			// Animate (come back up; empty hand if no more ammo)
			s32 frame = (*s_curPlayerWeapon->ammo) ? 0 : 2;
			s_weaponAnimState =
			{
				frame,	// frame
				0, 100,	// offset
				0, -20,	// xSpeed, ySpeed
				5, 7	// frameCount, ticksPerFrame
			};
			task_localBlockEnd;
			task_callTaskFunc(weapon_animateOnOrOffscreen);

			task_yield(2);
		}
		else	// out of ammo
		{
			if (s_weaponAutoMount2)
			{
				// func_1ece78();
				// return;
			}

			s_lastWeapon = s_curWeapon;
			s_curWeapon = WPN_FIST;
			s_playerInfo.curWeapon = s_prevWeapon;
			if (s_weaponOffAnim)
			{
				task_callTaskFunc(weapon_handleOffAnimation);
			}
			weapon_setNext(s_curWeapon);
			task_callTaskFunc(weapon_handleOnAnimation);
		}
		task_end;
	}

	void weaponFire_concussion(MessageType msg)
	{
		struct LocalContext
		{
			Tick delay;
			s32  iFrame;
		};
		task_begin_ctx;

		if (*s_curPlayerWeapon->ammo)
		{
			task_localBlockBegin;
			// Sound effect
			if (s_concussionFireSndID)
			{
				sound_stop(s_concussionFireSndID);
			}
			s_concussionFireSndID = sound_play(s_concussion6SndSrc);

			// Wakeup AI within range
			if (s_curPlayerWeapon->wakeupRange)
			{
				vec3_fixed origin = { s_playerObject->posWS.x, s_playerObject->posWS.y, s_playerObject->posWS.z };
				collision_effectObjectsInRangeXZ(s_playerObject->sector, s_curPlayerWeapon->wakeupRange, origin, actor_sendWakeupMsg, s_playerObject, ETFLAG_AI_ACTOR);
			}

			// Aim projectiles
			fixed16_16 mtx[9];
			weapon_computeMatrix(mtx, -s_playerObject->pitch, -s_playerObject->yaw);

			vec3_fixed inVec =
			{
				0x10000, // 1.0
				0x8000,  // 0.5
				0x30000  // 3.0
			};
			vec3_fixed outVec;
			rotateVectorM3x3(&inVec, &outVec, mtx);

			fixed16_16 xPos = s_playerObject->posWS.x + outVec.x;
			fixed16_16 yPos = s_playerObject->posWS.y + outVec.y - s_playerObject->worldHeight + s_headwaveVerticalOffset;
			fixed16_16 zPos = s_playerObject->posWS.z + outVec.z;

			s32 baseVariation = s_curPlayerWeapon->variation & 0xffff;
			JBool targetFound = computeAutoaim(xPos, yPos, zPos, s_playerObject->pitch, s_playerObject->yaw, s_curPlayerWeapon->variation & 0xffff);
			if (!targetFound)
			{
				s32 variation = random(baseVariation * 2) - baseVariation;
				s_weaponFirePitch = s_playerObject->pitch + variation;

				variation = random(baseVariation * 2) - baseVariation;
				s_weaponFireYaw = s_playerObject->yaw + variation;
			}

			s32 canFire = s_canFireWeaponPrim;
			fixed16_16 mtx2[9];
			s_canFireWeaponPrim = 0;
			if (canFire > 1)
			{
				weapon_computeMatrix(mtx2, -s_weaponFirePitch, -s_weaponFireYaw);
			}

			// Spawn projectiles.
			fixed16_16 fire = intToFixed16(canFire);
			while (canFire)
			{
				if (*s_curPlayerWeapon->ammo == 0)
				{
					break;
				}
				fire -= ONE_16;
				canFire--;
				s32 superChargeFrame = s_superCharge ? 0 : 1;
				// This is always true if super charge is *not* active otherwise
				// it is true every other frame.
				if (superChargeFrame | (s_fireFrame & 1))
				{
					*s_curPlayerWeapon->ammo = pickup_addToValue(
						*s_curPlayerWeapon->ammo,
						-s_curPlayerWeapon->primaryFireConsumption,
						getMaxAmmo(s_curPlayerWeapon->ammo));
				}

				ProjectileLogic* projLogic;
				fixed16_16 yPos = s_playerObject->posWS.y - s_playerObject->worldHeight + s_headwaveVerticalOffset;
				projLogic = (ProjectileLogic*)createProjectile(s_curPlayerWeapon->primaryProjectile, s_playerObject->sector, s_playerObject->posWS.x, yPos, s_playerObject->posWS.z, s_playerObject);
				projLogic->flags &= ~PROJFLAG_CAMERA_PASS_SOUND;
				projLogic->prevColObj = s_playerObject;

				if (targetFound)
				{
					proj_setYawPitch(projLogic, s_wpnPitchSin, s_wpnPitchCos, s_autoAimDirX, s_autoAimDirZ);

					SecObject* projObj = projLogic->logic.obj;
					projObj->pitch = s_weaponFirePitch;
					projObj->yaw = s_weaponFireYaw;
				}
				else
				{
					proj_setTransform(projLogic, s_weaponFirePitch, s_weaponFireYaw);
				}

				if (canFire)
				{
					assert(0);
					// TODO
				}
				else
				{
					projLogic->delta = outVec;
					tfe_adjustWeaponCollisionSpeed(projLogic);

					ProjectileHitType hitType = proj_handleMovement(projLogic);
					handleProjectileHit(projLogic, hitType);

					tfe_restoreWeaponCollisionSpeed(projLogic);
				}
			}
			task_localBlockEnd;

			// Animation.
			if (s_concussionFireSndID1)
			{
				sound_stop(s_concussionFireSndID1);
			}
			s_concussionFireSndID1 = sound_play(s_concussion5SndSrc);

			for (taskCtx->iFrame = 0; taskCtx->iFrame < s_concussionNumFrames; taskCtx->iFrame++)
			{
				s_curPlayerWeapon->frame = s_concussionAnim[taskCtx->iFrame].waxFrame;
				s_weaponLight = s_concussionAnim[taskCtx->iFrame].weaponLight;
				taskCtx->delay = (s_superCharge) ? s_concussionAnim[taskCtx->iFrame].delaySupercharge : s_concussionAnim[taskCtx->iFrame].delayNormal;

				do
				{
					task_yield(taskCtx->delay);
					task_callTaskFunc(weapon_handleState);
				} while (msg != MSG_RUN_TASK);
			}

			s_canFireWeaponPrim = 1;
			s_canFireWeaponSec = 1;
		}
		else  // Out of Ammo
		{
			if (s_concussionOutOfAmmoSndID)
			{
				sound_stop(s_concussionOutOfAmmoSndID);
			}
			s_concussionOutOfAmmoSndID = sound_play(s_concussion1SndSrc);

			taskCtx->delay = (s_superCharge) ? 3 : 7;
			s_curPlayerWeapon->frame = 0;
			do
			{
				task_yield(taskCtx->delay);
				task_callTaskFunc(weapon_handleState);
			} while (msg != MSG_RUN_TASK);

			s_canFireWeaponPrim = 0;
			s_canFireWeaponSec = 0;
			// if (s_weaponAutoMount2)
			// {
				// func_1ece78();
			// }
		}

		task_end;
	}

	void weaponFire_cannon(MessageType msg)
	{
		struct LocalContext
		{
			Tick delay;
			s32  iFrame;
		};
		task_begin_ctx;

		if (s_secondaryFire)
		{
			if (*s_curPlayerWeapon->secondaryAmmo)
			{
				// Initial animation frame
				taskCtx->delay = (s_superCharge) ? s_cannonSecondaryAnim[0].delaySupercharge : s_cannonSecondaryAnim[0].delayNormal;
				s_weaponLight = s_cannonSecondaryAnim[0].weaponLight;
				s_curPlayerWeapon->frame = s_cannonSecondaryAnim[0].waxFrame;
				do
				{
					task_yield(taskCtx->delay);
					task_callTaskFunc(weapon_handleState);
				} while (msg != MSG_RUN_TASK);

				// Sound effect
				if (s_cannonFireSndID1)
				{
					sound_stop(s_cannonFireSndID1);
				}
				s_cannonFireSndID1 = sound_play(s_missile1SndSrc);

				// Wakeup AI within range
				if (s_curPlayerWeapon->wakeupRange)
				{
					vec3_fixed origin = { s_playerObject->posWS.x, s_playerObject->posWS.y, s_playerObject->posWS.z };
					collision_effectObjectsInRangeXZ(s_playerObject->sector, s_curPlayerWeapon->wakeupRange, origin, actor_sendWakeupMsg, s_playerObject, ETFLAG_AI_ACTOR);
				}

				task_localBlockBegin;
				// Aim projectile
				fixed16_16 mtx[9];
				weapon_computeMatrix(mtx, -s_playerObject->pitch, -s_playerObject->yaw);

				vec3_fixed outVec;
				vec3_fixed inVec =
				{
					0x20000,	// 2.0
					0x4000,		// 0.25
					0x40000,	// 4.0
				};
				rotateVectorM3x3(&inVec, &outVec, mtx);

				fixed16_16 xPos = s_playerObject->posWS.x + outVec.x;
				fixed16_16 yPos = s_playerObject->posWS.y + outVec.y - s_playerObject->worldHeight + s_headwaveVerticalOffset;
				fixed16_16 zPos = s_playerObject->posWS.z + outVec.z;

				s32 baseVariation = s_curPlayerWeapon->variation & 0xffff;
				JBool targetFound = computeAutoaim(xPos, yPos, zPos, s_playerObject->pitch, s_playerObject->yaw, baseVariation);
				if (!targetFound)
				{
					s32 variation = random(baseVariation * 2) - baseVariation;
					s_weaponFirePitch = s_playerObject->pitch + variation;

					variation = random(baseVariation * 2) - baseVariation;
					s_weaponFireYaw = s_playerObject->yaw + variation;
				}

				s32 canFire = s_canFireWeaponSec;
				s_canFireWeaponSec = 0;

				fixed16_16 mtx2[9];
				if (canFire > 1)
				{
					weapon_computeMatrix(mtx2, -s_weaponFirePitch, -s_weaponFireYaw);
				}

				// Spawn projectile
				fixed16_16 fire = intToFixed16(canFire);
				while (canFire && *s_curPlayerWeapon->secondaryAmmo)
				{
					canFire--;
					fire -= ONE_16;

					s32 superChargeFrame = s_superCharge ? 0 : 1;
					if (superChargeFrame | (s_fireFrame & 1))
					{
						*s_curPlayerWeapon->secondaryAmmo = pickup_addToValue(
							*s_curPlayerWeapon->secondaryAmmo,
							-s_curPlayerWeapon->secondaryFireConsumption,
							getMaxAmmo(s_curPlayerWeapon->secondaryAmmo));
					}

					fixed16_16 yPos = s_playerObject->posWS.y - s_playerObject->worldHeight + s_headwaveVerticalOffset;
					ProjectileLogic* projLogic = (ProjectileLogic*)createProjectile(s_curPlayerWeapon->secondaryProjectile, s_playerObject->sector, s_playerObject->posWS.x, yPos, s_playerObject->posWS.z, s_playerObject);
					projLogic->flags &= ~PROJFLAG_CAMERA_PASS_SOUND;
					projLogic->prevColObj = s_playerObject;
					if (targetFound)
					{
						proj_setYawPitch(projLogic, s_wpnPitchSin, s_wpnPitchCos, s_autoAimDirX, s_autoAimDirZ);

						SecObject* projObj = projLogic->logic.obj;
						projObj->pitch = s_weaponFirePitch;
						projObj->yaw = s_weaponFireYaw;
					}
					else
					{
						proj_setTransform(projLogic, s_weaponFirePitch, s_weaponFireYaw);
					}

					if (canFire)
					{
						// TODO
						assert(0);
					}
					else
					{
						// Initial movement and collision.
						projLogic->delta = outVec;
						tfe_adjustWeaponCollisionSpeed(projLogic);

						ProjectileHitType hit = proj_handleMovement(projLogic);
						handleProjectileHit(projLogic, hit);

						tfe_restoreWeaponCollisionSpeed(projLogic);
					}
				}
				task_localBlockEnd;

				// Animation
				for (taskCtx->iFrame = 1; taskCtx->iFrame < s_cannonSecondaryNumFrames; taskCtx->iFrame++)
				{
					s_curPlayerWeapon->frame = s_cannonSecondaryAnim[taskCtx->iFrame].waxFrame;
					s_weaponLight = s_cannonSecondaryAnim[taskCtx->iFrame].weaponLight;
					taskCtx->delay = (s_superCharge) ? s_cannonSecondaryAnim[taskCtx->iFrame].delaySupercharge : s_cannonSecondaryAnim[taskCtx->iFrame].delayNormal;
					do
					{
						task_yield(taskCtx->delay);
						task_callTaskFunc(weapon_handleState);
					} while (msg != MSG_RUN_TASK);
				}

				s_canFireWeaponSec = 1;
			}
			else	// out of ammo
			{
				s_curPlayerWeapon->frame = 0;
				taskCtx->delay = (s_superCharge) ? 3 : 7;
				do
				{
					task_yield(taskCtx->delay);
					task_callTaskFunc(weapon_handleState);
				} while (msg != MSG_RUN_TASK);

				s_canFireWeaponSec = 0;
			}
		}
		else  // Primary Fire.
		{
			if (*s_curPlayerWeapon->ammo)
			{
				// Sound effect
				if (s_cannonFireSndID)
				{
					sound_stop(s_cannonFireSndID);
				}
				s_cannonFireSndID = sound_play(s_plasma4SndSrc);

				// Wakeup AI within range
				if (s_curPlayerWeapon->wakeupRange)
				{
					vec3_fixed origin = { s_playerObject->posWS.x, s_playerObject->posWS.y, s_playerObject->posWS.z };
					collision_effectObjectsInRangeXZ(s_playerObject->sector, s_curPlayerWeapon->wakeupRange, origin, actor_sendWakeupMsg, s_playerObject, ETFLAG_AI_ACTOR);
				}

				task_localBlockBegin;
				// Aim projectiles
				fixed16_16 mtx[9];
				weapon_computeMatrix(mtx, -s_playerObject->pitch, -s_playerObject->yaw);

				vec3_fixed inVec =
				{
					0x10000,	// 1.0
					0x9999,		// 0.6
					0x30000		// 3.0
				};
				vec3_fixed outVec;
				rotateVectorM3x3(&inVec, &outVec, mtx);

				fixed16_16 xPos = s_playerObject->posWS.x + outVec.x;
				fixed16_16 yPos = s_playerObject->posWS.y + outVec.y - s_playerObject->worldHeight + s_headwaveVerticalOffset;
				fixed16_16 zPos = s_playerObject->posWS.z + outVec.z;

				s32 baseVariation = s_curPlayerWeapon->variation & 0xffff;
				JBool targetFound = computeAutoaim(xPos, yPos, zPos, s_playerObject->pitch, s_playerObject->yaw, baseVariation);
				if (!targetFound)
				{
					s32 variation = random(baseVariation * 2) - baseVariation;
					s_weaponFirePitch = s_playerObject->pitch + variation;

					variation = random(variation * 2) - variation;
					s_weaponFireYaw = s_playerObject->yaw + variation;
				}

				s32 canFire = s_canFireWeaponPrim;
				fixed16_16 mtx2[9];
				s_canFireWeaponPrim = 0;
				if (canFire > 1)
				{
					weapon_computeMatrix(mtx2, -s_weaponFirePitch, -s_weaponFireYaw);
				}

				// Spawn projectiles
				fixed16_16 fire = intToFixed16(canFire);
				while (canFire && *s_curPlayerWeapon->ammo)
				{
					canFire--;
					fire -= ONE_16;

					s32 superChargeFrame = s_superCharge ? 0 : 1;
					if (superChargeFrame | (s_fireFrame & 1))
					{
						*s_curPlayerWeapon->ammo = pickup_addToValue(
							*s_curPlayerWeapon->ammo,
							-s_curPlayerWeapon->primaryFireConsumption,
							getMaxAmmo(s_curPlayerWeapon->ammo));
					}

					fixed16_16 yPos = s_playerObject->posWS.y - s_playerObject->worldHeight + s_headwaveVerticalOffset;
					ProjectileLogic* projLogic = (ProjectileLogic*)createProjectile(s_curPlayerWeapon->primaryProjectile, s_playerObject->sector, s_playerObject->posWS.x, yPos, s_playerObject->posWS.z, s_playerObject);
					projLogic->flags &= ~PROJFLAG_CAMERA_PASS_SOUND;
					projLogic->prevColObj = s_playerObject;
					if (s_oneHitKillEnabled) { applyOneHitKillCheat(projLogic); }

					if (targetFound)
					{
						proj_setYawPitch(projLogic, s_wpnPitchSin, s_wpnPitchCos, s_autoAimDirX, s_autoAimDirZ);

						SecObject* projObj = projLogic->logic.obj;
						projObj->pitch = s_weaponFirePitch;
						projObj->yaw = s_weaponFireYaw;
					}
					else
					{
						proj_setTransform(projLogic, s_weaponFirePitch, s_weaponFireYaw);
					}

					if (canFire)
					{
						// TODO
						assert(0);
					}
					else
					{
						projLogic->delta = outVec;
						tfe_adjustWeaponCollisionSpeed(projLogic);

						// Initial movement to make sure the player isn't too close to a wall or adjoin.
						ProjectileHitType hitType = proj_handleMovement(projLogic);
						handleProjectileHit(projLogic, hitType);

						tfe_restoreWeaponCollisionSpeed(projLogic);
					}
				}
				task_localBlockEnd;

				// Animate
				for (taskCtx->iFrame = 0; taskCtx->iFrame < s_cannonPrimaryNumFrames; taskCtx->iFrame++)
				{
					s_curPlayerWeapon->frame = s_cannonPrimaryAnim[taskCtx->iFrame].waxFrame;
					s_weaponLight = s_cannonPrimaryAnim[taskCtx->iFrame].weaponLight;
					taskCtx->delay = (s_superCharge) ? s_cannonPrimaryAnim[taskCtx->iFrame].delaySupercharge : s_cannonPrimaryAnim[taskCtx->iFrame].delayNormal;
					do
					{
						task_yield(taskCtx->delay);
						task_callTaskFunc(weapon_handleState);
					} while (msg != MSG_RUN_TASK);
				}

				s_weaponLight = 0;
				if (s_isShooting)
				{
					taskCtx->delay = ((s_superCharge) ? 14 : 29) >> 1;	// this extra delay is equal to the delay of the single animation frame; it should be preserved if the animation is modded
					do
					{
						task_yield(taskCtx->delay);
						task_callTaskFunc(weapon_handleState);
					} while (msg != MSG_RUN_TASK);
				}

				s_canFireWeaponPrim = 1;
			}
			else	// out of ammo
			{
				if (s_cannonOutOfAmmoSndID)
				{
					sound_stop(s_cannonOutOfAmmoSndID);
				}
				s_cannonOutOfAmmoSndID = sound_play(s_plasmaEmptySndSrc);

				taskCtx->delay = (s_superCharge) ? 3 : 7;
				s_curPlayerWeapon->frame = 0;
				do
				{
					task_yield(taskCtx->delay);
					task_callTaskFunc(weapon_handleState);
				} while (msg != MSG_RUN_TASK);

				s_canFireWeaponPrim = 0;

				/* TODO:
				if (s_weaponAutoMount2)
				{
					func_1ece78();
				}*/
			}
		}

		task_end;
	}

	JBool computeAutoaim(fixed16_16 xPos, fixed16_16 yPos, fixed16_16 zPos, angle14_32 pitch, angle14_32 yaw, s32 variation)
	{
		if (!s_drawnObjCount || !TFE_Settings::getGameSettings()->df_enableAutoaim)
		{
			return JFALSE;
		}
		fixed16_16 closest = MAX_AUTOAIM_DIST;
		for (s32 i = 0; i < s_drawnObjCount; i++)
		{
			SecObject* obj = s_drawnObj[i];
			if (obj && (obj->flags & OBJ_FLAG_AIM))
			{
				const fixed16_16 height = (obj->worldHeight >> 1) + (obj->worldHeight >> 2);	// 3/4 object height.

				fixed16_16 dx = obj->posWS.x - xPos;
				fixed16_16 dy = obj->posWS.y - height - yPos;
				fixed16_16 dz = obj->posWS.z - zPos;
				
				angle14_32 angle = vec2ToAngle(dx, dz) & ANGLE_MASK;
				angle14_32 yawDelta = (angle - yaw) & ANGLE_MASK;
				if (yawDelta > 227 && yawDelta < 16111)
				{
					continue;
				}

				fixed16_16 dirX, dirZ;
				fixed16_16 dist = computeDirAndLength(dx, dz, &dirX, &dirZ);
				if (dist < closest)
				{
					closest = dist;
					s_weaponFireYaw = angle;
					s_weaponFirePitch = vec2ToAngle(-dy, dist) & ANGLE_MASK;

					s_autoAimDirX = dirX;
					s_autoAimDirZ = dirZ;
				}
			}
		}
		if (closest == MAX_AUTOAIM_DIST)
		{
			return JFALSE;
		}
		if (variation)
		{
			s32 angleVar = random(variation << 6) - (variation << 5);
			s_autoAimDirX += angleVar;
			s_autoAimDirZ += angleVar;
			s_weaponFirePitch += random(variation * 2) + (pitch >> 3) - variation;
		}
		sinCosFixed(s_weaponFirePitch, &s_wpnPitchSin, &s_wpnPitchCos);
		return JTRUE;
	}
}  // namespace TFE_DarkForces