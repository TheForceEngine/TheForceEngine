#pragma once
//////////////////////////////////////////////////////////////////////
// Dark Forces
// AI Actor structures - see actor.h for a full explaination.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_DarkForces/time.h>
#include <TFE_DarkForces/item.h>
#include <TFE_DarkForces/hitEffect.h>
#include <TFE_DarkForces/projectile.h>
#include <TFE_DarkForces/sound.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/Math/fixedPoint.h>
#include <TFE_Jedi/Collision/collision.h>
#include "actor.h"

using namespace TFE_Jedi;
using namespace TFE_DarkForces;

struct ActorModule;
struct MovementModule;
struct ActorTarget;
struct Task;

enum LogicAnimFlags
{
	AFLAG_PLAYONCE = FLAG_BIT(0),	// Indicates an animation will play through once only and not loop back to first frame
	AFLAG_READY    = FLAG_BIT(1),
	AFLAG_BIT3	   = FLAG_BIT(3),	// Unknown, seems to be set but never read
};

enum LogicAnimState : u32
{
	//GENERAL
	STATE_DELAY = 0u,
	//ATTACK MODULE
	STATE_DECIDE,			// Decide whether to attack and which attack to use
	STATE_ATTACK1,			// Primary attack (can be melee or ranged)
	STATE_ANIMATE1,			// Animation played after attack (eg. weapon recoil)
	STATE_ATTACK2,			// Secondary attack (always ranged)
	STATE_ANIMATE2,			// Animation played after secondary attack
	STATE_COUNT,
	//THINKER MODULE
	// Move towards our destination
	STATE_MOVE = 1u,
	// Update our destination (generally moving towards the player).
	STATE_TURN = 2u,
};

typedef JBool(*ActorFunc)(ActorModule*, MovementModule*);
typedef JBool(*ActorMsgFunc)(s32 msg, ActorModule*, MovementModule*);
typedef void(*ActorAttribFunc)(ActorModule*);
typedef u32(*ActorTargetFunc)(MovementModule*, ActorTarget*);
typedef void(*ActorFreeFunc)(void*);

struct LogicAnimation
{
	s32 frameRate;
	fixed16_16 frameCount;
	Tick prevTick;
	fixed16_16 frame;
	fixed16_16 startFrame;
	u32 flags;			// see LogicAnimFlags enum
	s32 animId;
	LogicAnimState state;
};

struct ActorTiming
{
	Tick delay;
	Tick searchDelay;
	Tick meleeDelay;
	Tick rangedDelay;
	Tick losDelay;		// time to search for line of sight (LOS).
	Tick nextTick;
};

enum ActorModuleType : u32
{
	ACTMOD_MOVE = 0,
	ACTMOD_ATTACK,
	ACTMOD_DAMAGE,
	ACTMOD_THINKER,
	ACTMOD_FLYER,
	ACTMOD_FLYER_REMOTE,
	ACTMOD_COUNT,
	ACTMOD_INVALID = ACTMOD_COUNT
};

enum TargetFlags
{
	TARGET_MOVE_XZ  = FLAG_BIT(0),
	TARGET_MOVE_Y   = FLAG_BIT(1),
	TARGET_MOVE_ROT = FLAG_BIT(2),
	TARGET_FREEZE   = FLAG_BIT(3),
	TARGET_ALL_MOVE = TARGET_MOVE_XZ | TARGET_MOVE_Y | TARGET_MOVE_ROT,
	TARGET_ALL      = TARGET_ALL_MOVE | TARGET_FREEZE
};

enum AttackFlags
{
	ATTFLAG_NONE      = 0,
	ATTFLAG_MELEE     = FLAG_BIT(0), // Has melee attacks
	ATTFLAG_RANGED    = FLAG_BIT(1), // Has ranged attacks
	ATTFLAG_LIT_MELEE = FLAG_BIT(2), // Lights up when melee attacks
	ATTFLAG_LIT_RNG   = FLAG_BIT(3), // Lights up when range attacks
	ATTFLAG_ALL       = ATTFLAG_MELEE | ATTFLAG_RANGED | ATTFLAG_LIT_MELEE | ATTFLAG_LIT_RNG
};

enum ActorCollisionFlags
{
	ACTORCOL_NO_Y_MOVE       = FLAG_BIT(0),  // When _not_ set, an actor can move vertically. Set for non-flying enemies.
	ACTORCOL_GRAVITY         = FLAG_BIT(1),
	ACTORCOL_SLIDE_RESPONSE  = FLAG_BIT(2),  // Actor will "slide" along a wall that they collide with. This is generally set for flying enemies and bosses.
	ACTORCOL_ALL             = ACTORCOL_NO_Y_MOVE | ACTORCOL_GRAVITY | ACTORCOL_SLIDE_RESPONSE
};

struct ActorModule
{
	ActorModuleType type;

	ActorFunc       func;
	ActorMsgFunc    msgFunc;
	ActorAttribFunc attribFunc;		// Never used?
	ActorFreeFunc   freeFunc;

	Tick nextTick;
	SecObject* obj;
};

struct ActorTarget
{
	vec3_fixed pos;
	angle14_16 pitch;
	angle14_16 yaw;
	angle14_16 roll;
	s16 pad;

	fixed16_16 speed;
	fixed16_16 speedVert;
	fixed16_16 speedRotation;
	u32 flags;
};

struct ActorBurstFire
{
	u32 burstNumber;
	u32 variation;
	Tick interval;
	Tick lastShot;
	s32 shotCount;
};

struct AttackModule
{
	ActorModule header;
	ActorTarget target;
	ActorTiming timing;
	LogicAnimation anim;

	fixed16_16 fireSpread;
	Tick accuracyNextTick;
	vec3_fixed fireOffset;

	ProjectileType projType;
	SoundSourceId attackSecSndSrc;
	SoundSourceId attackPrimSndSrc;
	fixed16_16 meleeRange;
	fixed16_16 minDist;
	fixed16_16 maxDist;
	fixed16_16 meleeDmg;
	fixed16_16 meleeRate;
	u32 attackFlags;		// see AttackFlags above.

	// New in TFE - Burst fire properties
	JBool hasBurstFire;
	ActorBurstFire burstFire;
};

struct MovementModule
{
	ActorModule   header;
	ActorTargetFunc updateTargetFunc;
	CollisionInfo physics;
	ActorTarget   target;

	vec3_fixed delta;
	RWall* collisionWall;
	u32 unused;	// member in structure but not actually used, other than being initialized.
	u32 collisionFlags;		// see ActorCollisionFlags
};

struct ThinkerModule
{
	ActorModule header;
	ActorTarget target;
	
	Tick delay;
	Tick maxWalkTime;
	Tick startDelay;
	LogicAnimation anim;

	Tick nextTick;
	Tick playerLastSeen;
	Tick prevColTick;

	fixed16_16 targetOffset;
	fixed16_16 targetVariation;
	angle14_32 approachVariation;
};

struct DamageModule
{
	AttackModule attackMod;		// A DamageModule starts with an AttackModule, which it uses for animation but the remainder of the struct is unused.
	fixed16_16 hp;
	ItemId itemDropId;

	// Sound source IDs
	SoundSourceId hurtSndSrc;
	SoundSourceId dieSndSrc;
	// Currently playing hurt sound effect ID.
	SoundEffectId hurtSndID;

	JBool stopOnHit;
	HitEffectID dieEffect;
};

// "PhysicsActor" is the core actor used for bosses, mousebots, turrets and welders
// It only has a movement module
// Damage, attack and other behaviour is coded specifically for each AI
// Consider changing to a more suitable name?
struct PhysicsActor
{
	MovementModule moveMod;
	LogicAnimation anim;
	PhysicsActor** parent;

	vec3_fixed vel;
	SoundEffectId moveSndId;

	vec2_fixed lastPlayerPos;

	fixed16_16 hp;
	Task* actorTask;
	JBool alive;
	s32 state;
};

