#include "enemies.h"
#include "flyers.h"
#include "actorModule.h"
#include "animTables.h"
#include "../logic.h"
#include <TFE_DarkForces/player.h>
#include <TFE_DarkForces/hitEffect.h>
#include <TFE_DarkForces/projectile.h>
#include <TFE_DarkForces/item.h>
#include <TFE_DarkForces/random.h>
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
	static const s32 s_reeyeeMinDist = FIXED(30);

	Logic* reeyees_setup(SecObject* obj, LogicSetupFunc* setupFunc)
	{
		ActorDispatch* dispatch = actor_createDispatch(obj, setupFunc);
		dispatch->alertSndSrc = s_alertSndSrc[ALERT_REEYEE];

		DamageModule* module = actor_createDamageModule(dispatch);
		module->hp = FIXED(36);
		module->itemDropId = ITEM_DETONATORS;
		module->hurtSndSrc = s_agentSndSrc[AGENTSND_REEYEE_2];
		module->dieSndSrc = s_agentSndSrc[AGENTSND_REEYEE_3];
		actor_addModule(dispatch, (ActorModule*)module);

		AttackModule* attackMod = actor_createAttackModule(dispatch);
		attackMod->projType = PROJ_THERMAL_DET;
		attackMod->attackPrimSndSrc = 0;
		attackMod->attackSecSndSrc = 0;
		attackMod->timing.rangedDelay = 436;
		attackMod->maxDist = FIXED(100);
		attackMod->meleeRange = FIXED(10);
		attackMod->meleeDmg = FIXED(15);
		FLAGS_CLEAR_SET(attackMod->attackFlags, ATTFLAG_LIT_RNG, ATTFLAG_MELEE | ATTFLAG_RANGED);
		attackMod->minDist = s_reeyeeMinDist;
		s_actorState.attackMod = attackMod;
		actor_addModule(dispatch, (ActorModule*)attackMod);

		ThinkerModule* thinkerMod = actor_createThinkerModule(dispatch);
		thinkerMod->target.speedRotation = 0x7fff;
		thinkerMod->target.speed = FIXED(13);
		thinkerMod->anim.flags &= ~AFLAG_PLAYONCE;
		thinkerMod->startDelay = TICKS(2);
		actor_addModule(dispatch, (ActorModule*)thinkerMod);

		MovementModule* moveMod = actor_createMovementModule(dispatch);
		dispatch->moveMod = moveMod;
		dispatch->animTable = s_reeyeesAnimTable;

		moveMod->collisionFlags |= ACTORCOL_NO_Y_MOVE;
		moveMod->physics.width = obj->worldWidth;
		actor_setupInitAnimation();

		return (Logic*)dispatch;
	}

	Logic* reeyees2_setup(SecObject* obj, LogicSetupFunc* setupFunc)
	{
		ActorDispatch* dispatch = actor_createDispatch(obj, setupFunc);
		dispatch->alertSndSrc = s_alertSndSrc[ALERT_REEYEE];

		DamageModule* module = actor_createDamageModule(dispatch);
		module->hp = FIXED(36);
		module->hurtSndSrc = s_agentSndSrc[AGENTSND_REEYEE_2];
		module->dieSndSrc = s_agentSndSrc[AGENTSND_REEYEE_3];
		actor_addModule(dispatch, (ActorModule*)module);

		AttackModule* attackMod = actor_createAttackModule(dispatch);
		attackMod->attackSecSndSrc = 0;
		attackMod->maxDist = FIXED(100);
		FLAGS_CLEAR_SET(attackMod->attackFlags, ATTFLAG_RANGED | ATTFLAG_LIT_RNG, ATTFLAG_MELEE);
		attackMod->meleeDmg = FIXED(15);
		attackMod->meleeRange = FIXED(10);
		s_actorState.attackMod = attackMod;
		actor_addModule(dispatch, (ActorModule*)attackMod);

		ThinkerModule* thinkerMod = actor_createThinkerModule(dispatch);
		thinkerMod->target.speedRotation = 0x7fff;
		thinkerMod->target.speed = FIXED(13);
		thinkerMod->anim.flags &= ~AFLAG_PLAYONCE;
		thinkerMod->startDelay = TICKS(2);
		actor_addModule(dispatch, (ActorModule*)thinkerMod);

		MovementModule* moveMod = actor_createMovementModule(dispatch);
		dispatch->moveMod = moveMod;
		dispatch->animTable = s_reeyeesAnimTable;

		moveMod->collisionFlags |= ACTORCOL_NO_Y_MOVE;
		moveMod->physics.width = obj->worldWidth;
		actor_setupInitAnimation();

		return (Logic*)dispatch;
	}

	Logic* gamor_setup(SecObject* obj, LogicSetupFunc* setupFunc)
	{
		ActorDispatch* dispatch = actor_createDispatch(obj, setupFunc);
		dispatch->alertSndSrc = s_alertSndSrc[ALERT_GAMOR];

		DamageModule* module = actor_createDamageModule(dispatch);
		module->hp = FIXED(90);
		module->stopOnHit = JFALSE;
		module->dieSndSrc = s_agentSndSrc[AGENTSND_GAMOR_1];
		module->hurtSndSrc = s_agentSndSrc[AGENTSND_GAMOR_2];
		actor_addModule(dispatch, (ActorModule*)module);

		AttackModule* attackMod = actor_createAttackModule(dispatch);
		s_actorState.attackMod = attackMod;
		attackMod->timing.meleeDelay = 233;	// 1.6 seconds
		attackMod->meleeRange = FIXED(13);
		attackMod->meleeDmg = FIXED(40);
		attackMod->meleeRate = FIXED(360);
		attackMod->attackSecSndSrc = s_agentSndSrc[AGENTSND_AXE_1];
		FLAGS_CLEAR_SET(attackMod->attackFlags, ATTFLAG_RANGED, ATTFLAG_MELEE);
		actor_addModule(dispatch, (ActorModule*)attackMod);

		ThinkerModule* thinkerMod = actor_createThinkerModule(dispatch);
		thinkerMod->approachVariation = 2730;
		thinkerMod->delay = 0;
		thinkerMod->target.speedRotation = 0x3fff;
		thinkerMod->target.speed = FIXED(12);
		thinkerMod->anim.flags &= ~AFLAG_PLAYONCE;
		thinkerMod->startDelay = TICKS(2);
		actor_addModule(dispatch, (ActorModule*)thinkerMod);

		MovementModule* moveMod = actor_createMovementModule(dispatch);
		dispatch->moveMod = moveMod;
		dispatch->animTable = s_gamorAnimTable;

		moveMod->collisionFlags |= ACTORCOL_NO_Y_MOVE;
		moveMod->physics.width = obj->worldWidth;
		actor_setupInitAnimation();

		return (Logic*)dispatch;
	}

	Logic* bossk_setup(SecObject* obj, LogicSetupFunc* setupFunc)
	{
		ActorDispatch* dispatch = actor_createDispatch(obj, setupFunc);
		dispatch->alertSndSrc = s_alertSndSrc[ALERT_BOSSK];

		DamageModule* module = actor_createDamageModule(dispatch);
		module->hp = FIXED(45);
		module->dieSndSrc = s_agentSndSrc[AGENTSND_BOSSK_DIE];
		module->hurtSndSrc = s_agentSndSrc[AGENTSND_BOSSK_3];
		module->itemDropId = ITEM_CONCUSSION;
		actor_addModule(dispatch, (ActorModule*)module);

		AttackModule* attackMod = actor_createAttackModule(dispatch);
		s_actorState.attackMod = attackMod;
		attackMod->projType = PROJ_CONCUSSION;
		attackMod->maxDist = FIXED(100);
		attackMod->minDist = FIXED(10);
		attackMod->attackPrimSndSrc = s_concussion5SndSrc;
		attackMod->fireSpread = 0;
		FLAGS_CLEAR_SET(attackMod->attackFlags, ATTFLAG_MELEE, ATTFLAG_RANGED);
		actor_addModule(dispatch, (ActorModule*)attackMod);

		ThinkerModule* thinkerMod = actor_createThinkerModule(dispatch);
		thinkerMod->target.speedRotation = 0x7fff;
		thinkerMod->target.speed = FIXED(9);
		thinkerMod->delay = 0;
		thinkerMod->anim.flags &= ~AFLAG_PLAYONCE;
		thinkerMod->startDelay = TICKS(2);
		actor_addModule(dispatch, (ActorModule*)thinkerMod);

		MovementModule* moveMod = actor_createMovementModule(dispatch);
		dispatch->moveMod = moveMod;
		dispatch->animTable = s_bosskAnimTable;

		moveMod->collisionFlags |= ACTORCOL_NO_Y_MOVE;
		moveMod->physics.width = obj->worldWidth;
		actor_setupInitAnimation();

		return (Logic*)dispatch;
	}
}  // namespace TFE_DarkForces