#pragma once
//////////////////////////////////////////////////////////////////////
// Dark Forces Player -
// Player information and object data.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/Task/task.h>
#include "sound.h"
#include "projectile.h"

namespace TFE_Jedi
{
	struct DrawRect;
}

namespace TFE_DarkForces
{
	enum
	{
		WEAPON_NUM_TEXTURES = 16,		// Originally 8 in vanilla DF
	};

	struct PlayerWeapon
	{
		s32 frameCount;
		TextureData* frames[WEAPON_NUM_TEXTURES];
		s32 frame;
		s32 xPos[WEAPON_NUM_TEXTURES];
		s32 yPos[WEAPON_NUM_TEXTURES];
		u32 flags;
		s32 rollOffset;
		s32 pchOffset;
		s32 xWaveOffset;
		s32 yWaveOffset;
		s32 xOffset;
		s32 yOffset;
		s32* ammo;
		s32* secondaryAmmo;
		s32 u8c;
		s32 u90;
		fixed16_16 wakeupRange;
		s32 variation;

		// TFE
		s32 primaryFireConsumption;
		s32 secondaryFireConsumption;
		ProjectileType primaryProjectile;
		ProjectileType secondaryProjectile;
	};

	struct WeaponAnimState
	{
		s32 frame;
		s32 startOffsetX;
		s32 startOffsetY;
		s32 xSpeed;
		s32 ySpeed;
		s32 frameCount;
		u32 ticksPerFrame;
	};

	///////////////////////////////////////////
	// Constants
	///////////////////////////////////////////
	enum WeaponID
	{
		WPN_FIST = 0,
		WPN_PISTOL,
		WPN_RIFLE,
		WPN_THERMAL_DET,
		WPN_REPEATER,
		WPN_FUSION,
		WPN_MORTAR,
		WPN_MINE,
		WPN_CONCUSSION,
		WPN_CANNON,
		WPN_COUNT
	};

	enum WeaponFireMode
	{
		WFIRE_PRIMARY = 0,
		WFIRE_SECONDARY,
	};

	enum WeaponFireType
	{
		WFIRETYPE_PRIMARY = JFALSE,		// Primary fire.
		WFIRETYPE_SECONDARY = JTRUE,	// Secondary fire.
	};

	void weapon_resetState();
	void weapon_startup();
	void weapon_enableAutomount(JBool enable);
	void weapon_setNext(s32 wpnIndex);
	void weapon_setFireRate();
	void weapon_clearFireRate();
	void weapon_createPlayerWeaponTask();
	void weapon_holster();
	void weapon_draw(u8* display, DrawRect* rect);
	void weapon_emptyAnim();
	void weapon_stopFiring();
	void player_cycleWeapons(s32 change);
	void weapon_computeMatrix(fixed16_16* mtx, angle14_32 pitch, angle14_32 yaw);

	void weapon_queueWeaponSwitch(s32 wpnId);

	s32 weapon_getAmmoCount(s32 wpnId);
	s32 weapon_getSecondaryAmmoCount(s32 wpnId);

	// Serialization
	void weapon_serialize(Stream* stream);

	extern PlayerWeapon* s_curPlayerWeapon;
	extern SoundSourceId s_superchargeCountdownSound;
	extern Task* s_playerWeaponTask;

	// Weapon sounds (external since AI uses it).
	extern SoundSourceId s_punchSwingSndSrc;
	extern SoundSourceId s_pistolSndSrc;
	extern SoundSourceId s_pistolEmptySndSrc;
	extern SoundSourceId s_rifleSndSrc;
	extern SoundSourceId s_rifleEmptySndSrc;
	extern SoundSourceId s_fusion1SndSrc;
	extern SoundSourceId s_fusion2SndSrc;
	extern SoundSourceId s_repeaterSndSrc;
	extern SoundSourceId s_repeater1SndSrc;
	extern SoundSourceId s_repeaterEmptySndSrc;
	extern SoundSourceId s_mortarFireSndSrc;
	extern SoundSourceId s_mortarFireSndSrc2;
	extern SoundSourceId s_mortarEmptySndSrc;
	extern SoundSourceId s_mineSndSrc;
	extern SoundSourceId s_concussion6SndSrc;
	extern SoundSourceId s_concussion5SndSrc;
	extern SoundSourceId s_concussion1SndSrc;
	extern SoundSourceId s_plasma4SndSrc;
	extern SoundSourceId s_plasmaEmptySndSrc;
	extern SoundSourceId s_missile1SndSrc;
	extern SoundSourceId s_weaponChangeSnd;
}  // namespace TFE_DarkForces