#pragma once
//////////////////////////
// TFE Custom Logics
//////////////////////////

#include <TFE_System/types.h>

namespace TFE_ExternalData
{
	// Numeric defaults are based on what is set by default in the original code
	struct CustomActorLogic
	{
		const char* logicName;
		bool hasGravity = true;
		bool isFlying = false;
		u32 fov = 210;			// 9557 in DF angle
		u32 awareRange = 20;

		// Sound effects
		const char* alertSound = "";
		const char* painSound = "";
		const char* attack1Sound = "";
		const char* attack2Sound = "";
		const char* dieSound = "";

		bool officerAlerts = false;
		bool troopAlerts = false;

		// Damage
		u32 hitPoints = 4;
		s32 dropItem = -1;
		s32 dieEffect = -1;
		bool stopOnDamage = true;

		// Attack
		bool hasMeleeAttack = false;
		bool hasRangedAttack = true;
		bool litWithMeleeAttack = false;
		bool litWithRangedAttack = true;
		s32 projectile = 2;
		u32 rangedAttackDelay = 291;
		u32 meleeAttackDelay = 0;
		u32 losDelay = 43695;
		u32 meleeRange = 0;
		u32 meleeDamage = 0;
		u32 meleeRate = 230;
		u32 minAttackDist = 0;
		u32 maxAttackDist = 160;
		u32 fireSpread = 30;
		vec3_float fireOffset = { 0, -1000, 0 };	// (y = -1000) will be treated as default

		// Thinker and movement
		u32 speed = 4;
		u32 verticalSpeed = 10;
		u32 rotationSpeed = 720;	// 0x7fff in DF angle
		u32 approachVariation = 90;		// 4096 in DF angle
		u32 approachOffset = 3;
		u32 thinkerDelay = 2;

		f32 collisionWidth = -1;
		f32 collisionHeight = -1;
		f32 stepUpHeight = 3.5;
		f32 stepDownHeight = 4;
		s32 slideOnCollision = -1;	// -1 = "null", 0 = false, 1 = true
	};

	struct ExternalLogics
	{
		std::vector<CustomActorLogic> actorLogics;
	};

	ExternalLogics* getExternalLogics();
	void loadCustomLogics(); 
	void parseLogicData(char* data, const char* filename, std::vector<CustomActorLogic>& actorLogics);
}
