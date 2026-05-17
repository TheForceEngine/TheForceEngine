#pragma once
#include <TFE_System/types.h>
#include <TFE_System/cJSON.h>
#include <TFE_DarkForces/player.h>

///////////////////////////////////////////
// TFE Externalised Weapon data
///////////////////////////////////////////

namespace TFE_ExternalData
{
	enum
	{
		WEAPON_NUM_TEXTURES = 16,
		WEAPON_NUM_ANIMFRAMES = 16,
		WEAPON_MAX_PROJECTILES = 4,
	};

	struct ExternalProjectile
	{
		const char* type = nullptr;
		
		// Projectile object
		const char* assetType = "spirit";
		const char* asset = "";
		bool fullBright = false;
		bool zeroWidth = false;
		bool autoAim = false;
		bool movable = false;
		
		// Projectile logic
		const char* updateFunc = "";
		u32 damage;
		u32 falloffAmount;
		u32 nextFalloffTick;
		u32 damageFalloffDelta;
		u32 minDamage;
		u32 force;
		u32 speed;
		u32 horzBounciness;
		u32 vertBounciness;
		s32 bounceCount;
		u32 reflectVariation;
		u32 duration;
		s32 homingAngularSpeed;
		const char* flightSound = "";
		const char* reflectSound = "";
		const char* cameraPassSound = "";
		s32 reflectEffectId = -1;
		s32 hitEffectId = -1;
		const char* reflectEffectName = "";
		const char* hitEffectName = "";
		bool explodeOnTimeout = false;

		bool doNotScale = false;
	};

	struct ExternalEffect
	{
		const char* type = nullptr;
		const char* wax = "";
		s32 force;
		s32 damage;
		s32 explosiveRange;
		s32 wakeupRange;
		const char* soundEffect = "";
		s32 soundPriority;
	};

	// Maps to TFE_DarkForces::WeaponAnimFrame
	struct WeaponAnimFrame
	{
		s32 texture = 0;
		s32 light = 0;
		u32 durationSupercharge = 0;
		u32 durationNormal = 0;
	};
	
	struct ExternalWeapon
	{
		const char* name = nullptr;
		s32 frameCount = 1;
		const char* textures[WEAPON_NUM_TEXTURES] = { "default.bm" };
		s32 xPos[WEAPON_NUM_TEXTURES] = { 0 };
		s32 yPos[WEAPON_NUM_TEXTURES] = { 0 };
		s32* ammo = &TFE_DarkForces::s_playerInfo.ammoEnergy;
		s32* secondaryAmmo = nullptr;
		s32 wakeupRange = 0;
		s32 variation = 0;
		
		s32 primaryFireConsumption = 1;
		s32 secondaryFireConsumption = 1;

		u32 primaryProjectile = 0;
		u32 secondaryProjectile = 0;
		const char* primaryProjectileName = "";
		const char* secondaryProjectileName = "";

		f32 pitchOffsets[WEAPON_MAX_PROJECTILES] = { 0, 0, 0, 0 };
		f32 yawOffsets[WEAPON_MAX_PROJECTILES] = { 0, 0, 0, 0 };
		f32 xOffsets[WEAPON_MAX_PROJECTILES] = { 0, 0, 0, 0 };
		f32 yOffsets[WEAPON_MAX_PROJECTILES] = { 0, 0, 0, 0 };
		f32 zOffsets[WEAPON_MAX_PROJECTILES] = { 0, 0, 0, 0 };
		f32 pitchOffsetsSecondary[WEAPON_MAX_PROJECTILES] = { 0, 0, 0, 0 };
		f32 yawOffsetsSecondary[WEAPON_MAX_PROJECTILES] = { 0, 0, 0, 0 };
		f32 xOffsetsSecondary[WEAPON_MAX_PROJECTILES] = { 0, 0, 0, 0 };
		f32 yOffsetsSecondary[WEAPON_MAX_PROJECTILES] = { 0, 0, 0, 0 };
		f32 zOffsetsSecondary[WEAPON_MAX_PROJECTILES] = { 0, 0, 0, 0 };

		s32 numAnimFrames = 1;
		WeaponAnimFrame animFrames[WEAPON_NUM_ANIMFRAMES];
		s32 numSecondaryAnimFrames = 1;
		WeaponAnimFrame animFramesSecondary[WEAPON_NUM_ANIMFRAMES];
	};

	struct ExternalGasmask
	{
		const char* texture = "gmask.bm";
		s32 xPos = 105;
		s32 yPos = 141;
	};

	ExternalProjectile* getExternalProjectiles();
	ExternalEffect* getExternalEffects();
	ExternalWeapon* getExternalWeapons();
	ExternalGasmask* getExternalGasmask();
	void clearExternalProjectiles();
	void clearExternalEffects();
	void clearExternalWeapons();
	void loadExternalProjectiles();
	void parseExternalProjectiles(char* data, bool fromMod);
	bool validateExternalProjectiles();
	bool tryAssignProjectileProperty(cJSON* data, ExternalProjectile& projectile);
	void loadExternalEffects();
	void parseExternalEffects(char* data, bool fromMod);
	bool validateExternalEffects();
	void loadExternalWeapons();
	void parseExternalWeapons(char* data, bool fromMod);
	bool validateExternalWeapons();
	bool tryAssignEffectProperty(cJSON* data, ExternalEffect& effect);
}