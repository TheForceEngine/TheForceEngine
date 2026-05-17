#pragma once
//////////////////////////////////////////////////////////////////////
// Dark Forces Projectile - 
// The projectile Logic handles the creation, update and effect of
// projectiles in the world. The same system is used by both player
// and enemy projectiles.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_Asset/dfKeywords.h>
#include <TFE_Jedi/Level/rsector.h>
#include <TFE_Jedi/Level/robject.h>
#include "logic.h"
#include "hitEffect.h"
#include "sound.h"
#include "time.h"

namespace TFE_DarkForces
{
	enum ProjectileType
	{
		PROJ_PUNCH = 0,
		PROJ_PISTOL_BOLT,
		PROJ_RIFLE_BOLT,
		PROJ_THERMAL_DET,
		PROJ_REPEATER,
		PROJ_PLASMA,
		PROJ_MORTAR,
		PROJ_LAND_MINE,
		PROJ_LAND_MINE_PROX,
		PROJ_LAND_MINE_PLACED,
		PROJ_CONCUSSION,
		PROJ_CANNON,
		PROJ_MISSILE,
		PROJ_TURRET_BOLT,
		PROJ_REMOTE_BOLT,
		PROJ_EXP_BARREL,
		PROJ_HOMING_MISSILE,
		PROJ_PROBE_PROJ,
		PROJ_BOBAFET_BALL,
		PROJ_COUNT
	};

	enum ProjectileHitType
	{
		PHIT_SKY          = -1,
		PHIT_NONE         = 0,
		PHIT_SOLID        = 4,
		PHIT_OUT_OF_RANGE = 5,
		PHIT_WATER        = 6,
	};

	enum WallHitFlag
	{
		WH_IGNORE = 0,
		WH_NOEXP,
		WH_STDEXP,
		WH_CUSTEXP,
		WH_BOUNCE,
		WM_COUNT
	};
		
	enum ProjectileFlags
	{
		PROJFLAG_CAMERA_PASS_SOUND = FLAG_BIT(0),
		PROJFLAG_EXPLODE           = FLAG_BIT(1),		// Explodes when reaches end of life.
		
		// TFE
		PROJFLAG_DONOT_SCALE       = FLAG_BIT(2),       // Disable framerate based scaling on 3DOs
	};

	struct ProjectileLogic;
	typedef ProjectileHitType(*ProjectileFunc)(ProjectileLogic*);

	// This is a type of "Logic" and can be safety cast to Logic {}.
	struct ProjectileLogic
	{
		Logic logic;            // Logic header.
		ProjectileType type;
		// Damage & Damage falloff.
		fixed16_16 dmg;         // Base damage
		fixed16_16 falloffAmt;  // Amount to reduce damage when falloff occurs.
		Tick nextFalloffTick;   // Next tick that falloff occurs.
		Tick dmgFalloffDelta;   // Ticks between falloff changes.
		fixed16_16 minDmg;      // Minimum damage

		fixed16_16 projForce;	// How much of the projectile speed translates into force on hit.
		vec3_fixed delta;       // Movement amount in the current timeslice in units.
		vec3_fixed vel;         // Projectile velocity in units / second.
		fixed16_16 speed;       // Projectile speed in units / second.
		fixed16_16 col_speed;	// Added for TFE to adjust for initial speed for collision detection purposes with small timesteps.
		vec3_fixed dir;         // Projectile facing direction.
		fixed16_16 horzBounciness;
		fixed16_16 vertBounciness;
		SecObject* prevColObj;
		SecObject* prevObj;
		SecObject* excludeObj;			  // Object which is excluded from damage (used to prevent AI from being hurt by their own weapons)
		Tick duration;                    // How long the projectile continues to move before going out of range (and being destroyed).
		angle14_32 homingAngleSpd;		  // How quickly a homing projectile lines up in angle units / second.
		s32 bounceCnt;					  // Number of remaining bounces / reflections. Used for magseal (positive numbers) and thermal detonator secondary fire (set to -1)
		s32 reflVariation;
		SoundEffectId flightSndId;        // Looping sound instance played while the projectile moves.
		SoundSourceId flightSndSource;    // Source looping sound.
		SoundSourceId cameraPassSnd;      // Sound effect played when the projectile passes near the camera.
		SoundSourceId reflectSnd;         // Sound effect played when the projectile is reflected off of the floor, ceiling or wall.
		ProjectileFunc updateFunc;        // Projectile update function, this determines how the projectile moves.
		HitEffectID reflectEffectId;
		HitEffectID hitEffectId;          // The effect to play when the projectile hits a solid surface.
		u32 flags;
	};

	// Startup the projectile system.
	void projectile_startup();
	void projectile_createTask();

	// Create a new projectile.
	Logic* createProjectile(ProjectileType type, RSector* sector, fixed16_16 x, fixed16_16 y, fixed16_16 z, SecObject* obj);

	// Trigger a landmine.
	void triggerLandMine(ProjectileLogic* logic, Tick delay);

	void proj_setYawPitch(ProjectileLogic* projLogic, fixed16_16 sinPitch, fixed16_16 cosPitch, fixed16_16 sinYaw, fixed16_16 cosYaw);
	void proj_setTransform(ProjectileLogic* projLogic, angle14_32 pitch, angle14_32 yaw);
	ProjectileHitType proj_handleMovement(ProjectileLogic* logic);
	JBool handleProjectileHit(ProjectileLogic* logic, ProjectileHitType hitType);

	ProjectileHitType arcingProjectileUpdateFunc(ProjectileLogic* logic);

	void proj_aimAtTarget(ProjectileLogic* proj, vec3_fixed target);
	void proj_aimArcing(ProjectileLogic* proj, vec3_fixed target, fixed16_16 speed);

	// TFE: Set projectile gravity
	void resetProjectileGravityAccel();
	void setProjectileGravityAccel(s32 accel);
	s32 getProjectileGravityAccel();

	// TFE: Serialization functionality.
	s32 proj_getLogicIndex(ProjectileLogic* logic);
	ProjectileLogic* proj_getByLogicIndex(s32 index);

	// Serialization
	void projLogic_serialize(Logic*& logic, SecObject* obj, Stream* stream);

	extern WallHitFlag s_hitWallFlag;
	extern angle14_32 s_projReflectOverrideYaw;
	void transformFireOffsets(angle14_16 yaw, vec3_fixed* sourceOffset, vec3_fixed* offset);	// New in TFE
}  // namespace TFE_DarkForces