#pragma once
//////////////////////////////////////////////////////////////////////
// Dark Forces
// Core Actor/AI functionality.
// -----------------------------
// AI in Dark Forces is handled by a central "dispatcher" with a
// a number of "modules" attached that handle specific functionality.
// This is basically a primitive ECS-style system.
// 
// Each module has function pointers to handle updates, message, damage,
// and destruction. During the AI update, the dispatch will loop over
// each module and call its update function. There is also some communication
// between modules to keep redundant data up to date.
// 
// Dispatcher (ActorDispatcher in TFE):
//   + ActorModule modules[]
//   + MovementModule moveMod  -- this could have been added to the list
//                                but every AI actor has it.
// 
// For example, a Stormtrooper is:
//  Dispatcher
//    + Damage Module -
//        Holds HP, drop item, hurt and die sounds. This is the
//        getting hurt and dying functionality.
//    + Attack Module -
//        Handles attacking functionality, and includes the
//        projectile type, melee range, etc.
//    + Thinker Module -
//        Handles overall state and sets targets for movement.
//    + Movement Module -
//        Handles the actual movement, collision detection and
//        response, basic animation.
//////////////////////////////////////////////////////////////////////
#include "actorModule.h"
#include "../sound.h"
#include <TFE_System/types.h>
#include <TFE_DarkForces/logic.h>
#include <TFE_Jedi/Collision/collision.h>

///////////////////////////////////////////
// Constants
///////////////////////////////////////////
enum ActorAlert
{
	ALERT_GAMOR = 0,
	ALERT_REEYEE,
	ALERT_BOSSK,
	ALERT_CREATURE,
	ALERT_PROBE,
	ALERT_INTDROID,
	ALERT_COUNT
};
enum AgentActionSounds
{
	AGENTSND_REMOTE_2 = 0,
	AGENTSND_AXE_1,
	AGENTSND_INTSTUN,
	AGENTSND_PROBFIRE_11,
	AGENTSND_PROBFIRE_12,
	AGENTSND_CREATURE2,
	AGENTSND_PROBFIRE_13,
	AGENTSND_STORM_HURT,
	AGENTSND_GAMOR_2,
	AGENTSND_REEYEE_2,
	AGENTSND_BOSSK_3,
	AGENTSND_CREATURE_HURT,
	AGENTSND_STORM_DIE,
	AGENTSND_REEYEE_3,
	AGENTSND_BOSSK_DIE,
	AGENTSND_GAMOR_1,
	AGENTSND_CREATURE_DIE,
	AGENTSND_EEEK_3,
	AGENTSND_SMALL_EXPLOSION,
	AGENTSND_PROBE_ALM,
	AGENTSND_TINY_EXPLOSION,
	AGENTSND_COUNT
};

enum
{
	OFFICER_ALERT_COUNT = 4,
	STORM_ALERT_COUNT = 8,
	ACTOR_MIN_VELOCITY = 0x1999,	// < 0.1
	ACTOR_MAX_MODULES = 6,
};

enum ActorDispatchFlags
{
	ACTOR_IDLE				= FLAG_BIT(0),
	ACTOR_MOVING			= FLAG_BIT(1),
	ACTOR_NPC				= FLAG_BIT(2),	// set for AI characters, unset for scenery and exploders (these also use ActorDispatch)
	ACTOR_PLAYER_VISIBLE	= FLAG_BIT(3),
	ACTOR_OFFIC_ALERT		= FLAG_BIT(4),	// use officer alert sounds
	ACTOR_TROOP_ALERT		= FLAG_BIT(5),	// use stormtrooper alert sounds

	// Added for TFE
	ACTOR_DYING             = FLAG_BIT(6),
};

// Forward Declarations.
struct ActorModule;
struct MovementModule;
struct AttackModule;
struct DamageModule;
struct ThinkerModule;
struct LogicAnimation;
struct PhysicsActor;
struct ActorTarget;

// Logic for 'actors' -
// an Actor is something with animated 'actions' that can move around in the world.
// The "Dispatch" logic is the core actor used for ordinary enemies, as well as scenery and exploders.
struct ActorDispatch
{
	Logic logic;

	ActorModule* modules[ACTOR_MAX_MODULES];
	MovementModule* moveMod;
	const s32* animTable;

	Tick delay;
	Tick nextTick;

	SoundSourceId alertSndSrc;
	SoundEffectId alertSndID;

	angle14_32 fov;
	fixed16_16 awareRange;

	vec3_fixed vel;
	vec2_fixed lastPlayerPos;

	Task* freeTask;
	u32 flags;

	// Scriptcall indexes
	s32 deathScriptCall;
	s32 alertScriptCall;
};

struct ActorState
{
	LogicAnimation* curAnimation;
	Logic* curLogic;
	AttackModule* attackMod;
	Tick nextAlertTick;
	u32 officerAlertIndex;
	u32 stormtrooperAlertIndex;
};

// Clear and set flags in a visually simple way.
#define FLAGS_CLEAR_SET(flags, flagsToClear, flagsToSet) \
flags &= ~(flagsToClear);	\
flags |=  (flagsToSet)

namespace TFE_DarkForces
{
	void actor_clearState();
	void actor_exitState();

	void actor_loadSounds();
	void actor_allocatePhysicsActorList();
	void actor_addPhysicsActorToWorld(PhysicsActor* actor);
	void actor_removePhysicsActorFromWorld(PhysicsActor* phyActor);
	void actor_createTask();

	ActorDispatch* actor_createDispatch(SecObject* obj, LogicSetupFunc* setupFunc);
	DamageModule* actor_createDamageModule(ActorDispatch* dispatch);
	MovementModule* actor_createMovementModule(ActorDispatch* dispatch);
	void actor_addModule(ActorDispatch* dispatch, ActorModule* module);

	void actor_messageFunc(MessageType msg, void* logic);
	void actor_sendWakeupMsg(SecObject* obj);
	void actor_kill();
	void actor_initModule(ActorModule* module, Logic* logic);
	JBool actor_canDie(PhysicsActor* phyActor);
	JBool actor_handleSteps(MovementModule* moveMod, ActorTarget* target);
	JBool actor_arrivedAtTarget(ActorTarget* target, SecObject* obj);
	JBool actor_canSeeObjFromDist(SecObject* actorObj, SecObject* obj);
	JBool actor_canSeeObject(SecObject* actorObj, SecObject* obj);

	s32 actor_getAnimationIndex(s32 action);
	void actor_setupAnimation(s32 animIdx, LogicAnimation* aiAnim);
	void actor_setupBossAnimation(SecObject* obj, s32 animId, LogicAnimation* anim);
	void actor_setAnimFrameRange(LogicAnimation* anim, s32 startFrame, s32 endFrame);
	void actor_addVelocity(fixed16_16 pushX, fixed16_16 pushY, fixed16_16 pushZ);
	void actor_removeLogics(SecObject* obj);
	void actor_setupSmartObj(MovementModule* moveMod);
	void actor_setCurAnimation(LogicAnimation* aiAnim);
	void actor_updatePlayerVisiblity(JBool playerVis, fixed16_16 posX, fixed16_16 posZ);
	void actor_changeDirFromCollision(MovementModule* moveMod, ActorTarget* target, Tick* prevColTick);
	void actor_jumpToTarget(PhysicsActor* physicsActor, SecObject* obj, vec3_fixed target, fixed16_16 speed, angle14_32 angleOffset);
	void actor_leadTarget(ProjectileLogic* proj);
	void actor_handleBossDeath(PhysicsActor* physicsActor);
	void actor_removeRandomCorpse();

	angle14_32 actor_offsetTarget(fixed16_16* targetX, fixed16_16* targetZ, fixed16_16 targetOffset, fixed16_16 targetVariation, angle14_32 angle, angle14_32 angleVariation);

	AttackModule* actor_createAttackModule(ActorDispatch* dispatch);
	ThinkerModule* actor_createThinkerModule(ActorDispatch* dispatch);
	void actor_thinkerModuleInit(ThinkerModule* thinkerMod);
	void actor_setupInitAnimation();
	void actor_setDeathCollisionFlags();

	// Returns JTRUE if 'actorObj' can see 'obj'
	// The object must be as close or closer than 'closeDist' or be within the fov of the 'actorObj'.
	// Also the distance between actorObj and obj must be less than 200 - 256 units (randomized), and this value is 
	// adjusted based on the darkness level of the area where 'obj' is (so it is harder to see objects in the dark).
	// Conversely the headlamp will make 'obj' visible from further away.
	JBool actor_isObjectVisible(SecObject* actorObj, SecObject* obj, angle14_32 fov, fixed16_16 closeDist);

	extern ActorState s_actorState;
	extern SoundSourceId s_alertSndSrc[ALERT_COUNT];
	extern SoundSourceId s_officerAlertSndSrc[OFFICER_ALERT_COUNT];
	extern SoundSourceId s_stormAlertSndSrc[STORM_ALERT_COUNT];
	extern SoundSourceId s_agentSndSrc[AGENTSND_COUNT];

	extern JBool s_aiActive;
	// Special yield macro for entities so that LAREDLITE works.
	#define entity_yield(t) \
	do \
	{ \
		task_yield(s_aiActive ? t : 290); \
	} while (!s_aiActive);

	// Helper macro to get local object distance from player
	#define playerDist(obj) fixedSqrt(player_getSquaredDistance(local(obj)))
}  // namespace TFE_DarkForces