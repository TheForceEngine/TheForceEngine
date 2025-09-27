#include <cstring>

#include "actor.h"
#include "actorInternal.h"
#include "actorSerialization.h"
#include "../logic.h"
#include "../gameMusic.h"
#include "../sound.h"
#include "animTables.h"
#include "actorModule.h"
#include "mousebot.h"
#include "dragon.h"
#include "bobaFett.h"
#include "welder.h"
#include "turret.h"
#include "phaseOne.h"
#include "phaseTwo.h"
#include "phaseThree.h"
#include <TFE_Game/igame.h>
#include <TFE_DarkForces/random.h>
#include <TFE_DarkForces/player.h>
#include <TFE_DarkForces/hitEffect.h>
#include <TFE_DarkForces/projectile.h>
#include <TFE_DarkForces/pickup.h>
#include <TFE_DarkForces/player.h>
#include <TFE_Jedi/Level/rsector.h>
#include <TFE_Jedi/Level/rwall.h>
#include <TFE_Jedi/Level/levelData.h>
#include <TFE_Jedi/InfSystem/message.h>
#include <TFE_Jedi/Memory/list.h>
#include <TFE_Jedi/Memory/allocator.h>
#include <TFE_Jedi/Serialization/serialization.h>
#include <TFE_System/system.h>

using namespace TFE_Jedi;

namespace TFE_DarkForces
{
	typedef void(*ActorModuleSerializeFn)(Stream*, ActorModule*&, ActorDispatch*);
	static const ActorModuleSerializeFn c_moduleSerFn[]=
	{
		actor_serializeMovementModule,    // ACTMOD_MOVE
		actor_serializeAttackModule,      // ACTMOD_ATTACK
		actor_serializeDamageModule,      // ACTMOD_DAMAGE
		actor_serializeThinkerModule,     // ACTMOD_THINKER
		actor_serializeFlyerModule,       // ACTMOD_FLYER
		actor_serializeFlyerRemoteModule, // ACTMOD_FLYER_REMOTE
		nullptr,						  // ACTMOD_INVALID
	};
	   
	void actorDispatch_serialize(Logic*& logic, SecObject* obj, Stream* stream)
	{
		ActorDispatch* dispatch = nullptr;
		if (serialization_getMode() == SMODE_WRITE)
		{
			dispatch = (ActorDispatch*)logic;
		}
		else
		{
			dispatch = (ActorDispatch*)allocator_newItem(s_istate.actorDispatch);
			if (!dispatch)
				return;
			memset(dispatch, 0, sizeof(ActorDispatch));

			logic = (Logic*)dispatch;
			logic->task = s_istate.actorTask;
			logic->cleanupFunc = actorLogicCleanupFunc;
			// This is needed for the modules.
			logic->obj = obj;
		}

		serialization_serializeDfSound(stream, SaveVersionInit, &dispatch->alertSndSrc);
		SERIALIZE(SaveVersionInit, dispatch->delay, 72);
		SERIALIZE(SaveVersionInit, dispatch->nextTick, 0);
		SERIALIZE(SaveVersionInit, dispatch->fov, 9557);			// ~210 degrees
		SERIALIZE(SaveVersionInit, dispatch->awareRange, FIXED(20));
		SERIALIZE(SaveVersionInit, dispatch->vel, {0});
		SERIALIZE(SaveVersionInit, dispatch->lastPlayerPos, {0});
		SERIALIZE(SaveVersionInit, dispatch->flags, 4);

		// Indexes to ScriptCalls
		SERIALIZE(ObjState_LogicScriptCallV1, dispatch->deathScriptCall, -1);
		SERIALIZE(ObjState_LogicScriptCallV1, dispatch->alertScriptCall, -1);

		// Animation Table.
		s32 animTableIndex = -1;
		if (serialization_getMode() == SMODE_WRITE)
		{
			animTableIndex = animTables_getIndex(dispatch->animTable);
		}
		SERIALIZE(SaveVersionInit, animTableIndex, -1);
		if (serialization_getMode() == SMODE_READ)
		{
			dispatch->animTable = animTables_getTable(animTableIndex);
		}
		// Movement module.
		u8 hasMoveMod = dispatch->moveMod ? 1 : 0;
		ActorModule* moveMod = (serialization_getMode() == SMODE_WRITE) ? (ActorModule*)dispatch->moveMod : nullptr;
		SERIALIZE(SaveVersionInit, hasMoveMod, 0);
		if (hasMoveMod)
		{
			actor_serializeMovementModule(stream, moveMod, dispatch);
		}
		if (serialization_getMode() == SMODE_READ)
		{
			dispatch->moveMod = (MovementModule*)moveMod;
		}
		// Module list.
		s32 moduleCount = 0;
		if (serialization_getMode() == SMODE_WRITE)
		{
			for (s32 i = 0; i < ACTOR_MAX_MODULES; i++)
			{
				if (!dispatch->modules[i]) { break; }
				moduleCount++;
			}
		}
		SERIALIZE(SaveVersionInit, moduleCount, 0);
		for (s32 i = 0; i < moduleCount; i++)
		{
			ActorModule* mod = dispatch->modules[i];
			ActorModuleType type;
			if (serialization_getMode() == SMODE_WRITE)
			{
				type = mod->type;
			}
			SERIALIZE(SaveVersionInit, type, ACTMOD_INVALID);
			type = type < ACTMOD_INVALID ? type : ACTMOD_INVALID;
			if (c_moduleSerFn[type])
			{
				c_moduleSerFn[type](stream, dispatch->modules[i], dispatch);
			}
			else
			{
				TFE_System::logWrite(LOG_ERROR, "Actor", "Actor serialization failed - unknown module %d.", type);
				assert(0);
			}
			if (serialization_getMode() == SMODE_READ && dispatch->modules[i])
			{
				dispatch->modules[i]->type = type;
			}
		}
		if (serialization_getMode() == SMODE_READ)
		{
			// This will be filled in a fixup pass if needed.
			dispatch->freeTask = nullptr;
		}
	}
		
	void actor_serializeObject(Stream* stream, SecObject*& obj)
	{
		s32 objId;
		if (serialization_getMode() == SMODE_WRITE)
		{
			objId = obj ? obj->serializeIndex : -1;
		}
		SERIALIZE(SaveVersionInit, objId, -1);
		if (serialization_getMode() == SMODE_READ)
		{
			obj = objId >= 0 ? objData_getObjectBySerializationId(objId) : nullptr;
		}
	}

	void actor_serializeWall(Stream* stream, RWall*& wall)
	{
		RSector* wallSector = nullptr;
		s32 wallId = -1;
		if (serialization_getMode() == SMODE_WRITE)
		{
			wallSector = wall ? wall->sector : nullptr;
			wallId = wall ? wall->id : -1;
		}
		serialization_serializeSectorPtr(stream, SaveVersionInit, wallSector);
		SERIALIZE(SaveVersionInit, wallId, -1);
		if (serialization_getMode() == SMODE_READ)
		{
			wall = nullptr;
			if (wallSector && wallId >= 0)
			{
				wall = &wallSector->walls[wallId];
			}
		}
	}

	void actor_serializeCollisionInfo(Stream* stream, CollisionInfo* colInfo)
	{
		actor_serializeObject(stream, colInfo->obj);
		actor_serializeObject(stream, colInfo->collidedObj);
		actor_serializeWall(stream, colInfo->wall);

		SERIALIZE(SaveVersionInit, colInfo->offsetX, 0);
		SERIALIZE(SaveVersionInit, colInfo->offsetY, 0);
		SERIALIZE(SaveVersionInit, colInfo->offsetZ, 0);
		SERIALIZE(SaveVersionInit, colInfo->stepUpHeight, 0);
		SERIALIZE(SaveVersionInit, colInfo->stepDownHeight, 0);
		SERIALIZE(SaveVersionInit, colInfo->height, 0);
		SERIALIZE(SaveVersionInit, colInfo->u24, 0);
		SERIALIZE(SaveVersionInit, colInfo->width, 0);
		SERIALIZE(SaveVersionInit, colInfo->flags, 0);
		SERIALIZE(SaveVersionInit, colInfo->responseStep, 0);
		SERIALIZE(SaveVersionInit, colInfo->responseDir, {0});
		SERIALIZE(SaveVersionInit, colInfo->responsePos, {0});
		SERIALIZE(SaveVersionInit, colInfo->responseAngle, 0);

		if (serialization_getMode() == SMODE_READ)
		{
			colInfo->unused = 0;
		}
	}

	void actor_serializeTarget(Stream* stream, ActorTarget* target)
	{
		SERIALIZE(SaveVersionInit, target->pos, {0});
		SERIALIZE(SaveVersionInit, target->pitch, 0);
		SERIALIZE(SaveVersionInit, target->yaw, 0);
		SERIALIZE(SaveVersionInit, target->roll, 0);
		SERIALIZE(SaveVersionInit, target->speed, 0);
		SERIALIZE(SaveVersionInit, target->speedVert, 0);
		SERIALIZE(SaveVersionInit, target->speedRotation, 0);
		SERIALIZE(SaveVersionInit, target->flags, 0);

		if (serialization_getMode() == SMODE_READ)
		{
			target->pad = 0;
		}
	}
		
	void actor_serializeTiming(Stream* stream, ActorTiming* timing)
	{
		SERIALIZE(SaveVersionInit, timing->delay, 0);
		SERIALIZE(SaveVersionInit, timing->searchDelay, 0);
		SERIALIZE(SaveVersionInit, timing->meleeDelay,  0);
		SERIALIZE(SaveVersionInit, timing->rangedDelay, 0);
		SERIALIZE(SaveVersionInit, timing->losDelay, 0);
		SERIALIZE(SaveVersionInit, timing->nextTick, 0);
	}

	void actor_serializeLogicAnim(Stream* stream, LogicAnimation* anim)
	{
		SERIALIZE(SaveVersionInit, anim->frameRate, 0);
		SERIALIZE(SaveVersionInit, anim->frameCount, 0);
		SERIALIZE(SaveVersionInit, anim->prevTick, 0);
		SERIALIZE(SaveVersionInit, anim->frame, 0);
		SERIALIZE(SaveVersionInit, anim->startFrame, 0);
		SERIALIZE(SaveVersionInit, anim->flags, 0);
		SERIALIZE(SaveVersionInit, anim->animId, 0);
		SERIALIZE(SaveVersionInit, anim->state, STATE_DELAY);
	}

	void actor_serializeMovementModuleBase(Stream* stream, MovementModule* moveMod)
	{
		actor_serializeCollisionInfo(stream, &moveMod->physics);
		actor_serializeTarget(stream, &moveMod->target);
		SERIALIZE(SaveVersionInit, moveMod->delta, { 0 });
		actor_serializeWall(stream, moveMod->collisionWall);
		SERIALIZE(SaveVersionInit, moveMod->collisionFlags, 0);
	}

	void actor_serializeMovementModule(Stream* stream, ActorModule*& mod, ActorDispatch* dispatch)
	{
		MovementModule* moveMod;
		if (serialization_getMode() == SMODE_READ)
		{
			moveMod = (MovementModule*)level_alloc(sizeof(MovementModule));
			memset(moveMod, 0, sizeof(MovementModule));
			actor_initModule((ActorModule*)moveMod, (Logic*)dispatch);
			mod = (ActorModule*)moveMod;

			moveMod->header.type = ACTMOD_MOVE;
			moveMod->header.func = defaultActorFunc;
			moveMod->header.freeFunc = nullptr;
			moveMod->updateTargetFunc = defaultUpdateTargetFunc;
		}
		else
		{
			moveMod = (MovementModule*)mod;
			assert(moveMod->header.func == defaultActorFunc || !moveMod->header.func);
			assert(moveMod->updateTargetFunc == defaultUpdateTargetFunc || !moveMod->updateTargetFunc);
			assert(moveMod->header.freeFunc == nullptr);
		}
		actor_serializeMovementModuleBase(stream, moveMod);
	}

	void actor_serializeAttackModuleBase(Stream* stream, AttackModule* attackMod, ActorDispatch* dispatch)
	{
		actor_serializeTarget(stream, &attackMod->target);
		actor_serializeTiming(stream, &attackMod->timing);
		actor_serializeLogicAnim(stream, &attackMod->anim);
		SERIALIZE(SaveVersionInit, attackMod->fireSpread, 0);
		SERIALIZE(SaveVersionInit, attackMod->accuracyNextTick, 0);
		SERIALIZE(SaveVersionInit, attackMod->fireOffset, {0});
		SERIALIZE(SaveVersionInit, attackMod->projType, PROJ_COUNT);
		serialization_serializeDfSound(stream, SaveVersionInit, &attackMod->attackSecSndSrc);
		serialization_serializeDfSound(stream, SaveVersionInit, &attackMod->attackPrimSndSrc);
		SERIALIZE(SaveVersionInit, attackMod->meleeRange, 0);
		SERIALIZE(SaveVersionInit, attackMod->minDist, 0);
		SERIALIZE(SaveVersionInit, attackMod->maxDist, 0);
		SERIALIZE(SaveVersionInit, attackMod->meleeDmg, 0);
		SERIALIZE(SaveVersionInit, attackMod->meleeRate, 0);
		SERIALIZE(SaveVersionInit, attackMod->attackFlags, 0);

		SERIALIZE(ObjState_BurstFire, attackMod->hasBurstFire, JFALSE);
		SERIALIZE(ObjState_BurstFire, attackMod->burstFire.burstNumber, 5);
		SERIALIZE(ObjState_BurstFire, attackMod->burstFire.variation, 2);
		SERIALIZE(ObjState_BurstFire, attackMod->burstFire.interval, 29);
		SERIALIZE(ObjState_BurstFire, attackMod->burstFire.shotCount, 5);
		SERIALIZE(ObjState_BurstFire, attackMod->burstFire.lastShot, 0);
	}

	void actor_serializeAttackModule(Stream* stream, ActorModule*& mod, ActorDispatch* dispatch)
	{
		AttackModule* attackMod = nullptr;
		s32 funcIdx = 0;
		if (serialization_getMode() == SMODE_WRITE)
		{
			attackMod = (AttackModule*)mod;
			funcIdx = attackMod->header.func == defaultAttackFunc ? 0 : 1;
		}
		SERIALIZE(SaveVersionInit, funcIdx, 0);
		if (serialization_getMode() == SMODE_READ)
		{
			attackMod = (AttackModule*)level_alloc(sizeof(AttackModule));
			mod = (ActorModule*)attackMod;
			memset(attackMod, 0, sizeof(AttackModule));
			actor_initModule(mod, (Logic*)dispatch);
			
			attackMod->header.func = funcIdx == 0 ? defaultAttackFunc : sewerCreatureEnemyFunc;
			attackMod->header.freeFunc = nullptr;
			attackMod->header.msgFunc = defaultAttackMsgFunc;
			attackMod->header.type = ACTMOD_ATTACK;
		}
		else if (attackMod)
		{
			assert(attackMod->header.func == defaultAttackFunc || attackMod->header.func == sewerCreatureEnemyFunc);
			assert(attackMod->header.msgFunc == defaultAttackMsgFunc);
			assert(attackMod->header.freeFunc == nullptr);
		}
		assert(attackMod);

		actor_serializeAttackModuleBase(stream, attackMod, dispatch);
	}

	const ActorFunc c_actorDamageFunc[] = 
	{
		defaultDamageFunc,
		sceneryLogicFunc,
		exploderFunc,
		sewerCreatureDamageFunc,
	};

	const ActorMsgFunc c_actorDamageMsgFunc[] =
	{
		defaultDamageMsgFunc,
		sceneryMsgFunc,
		exploderMsgFunc,
		sewerCreatureDamageMsgFunc,
	};

	s32 actor_getDamageFuncIndex(ActorFunc func)
	{
		for (s32 i = 0; i < TFE_ARRAYSIZE(c_actorDamageFunc); i++)
		{
			if (c_actorDamageFunc[i] == func) { return i; }
		}
		return -1;
	}

	s32 actor_getDamageMsgFuncIndex(ActorMsgFunc func)
	{
		for (s32 i = 0; i < TFE_ARRAYSIZE(c_actorDamageMsgFunc); i++)
		{
			if (c_actorDamageMsgFunc[i] == func) { return i; }
		}
		return -1;
	}

	ActorFunc actor_getDamageFunc(s32 index)
	{
		if (index < 0 || index >= TFE_ARRAYSIZE(c_actorDamageFunc)) { return nullptr; }
		return c_actorDamageFunc[index];
	}

	ActorMsgFunc actor_getDamageMsgFunc(s32 index)
	{
		if (index < 0 || index >= TFE_ARRAYSIZE(c_actorDamageMsgFunc)) { return nullptr; }
		return c_actorDamageMsgFunc[index];
	}

	void actor_serializeDamageModule(Stream* stream, ActorModule*& mod, ActorDispatch* dispatch)
	{
		DamageModule* damageMod = nullptr;
		s32 dmgFuncIndex = -1, dmgFuncMsgIndex = -1;
		if (serialization_getMode() == SMODE_WRITE)
		{
			damageMod = (DamageModule*)mod;
			dmgFuncIndex = actor_getDamageFuncIndex(damageMod->attackMod.header.func);
			dmgFuncMsgIndex = actor_getDamageMsgFuncIndex(damageMod->attackMod.header.msgFunc);
			assert(dmgFuncIndex >= 0 && dmgFuncMsgIndex >= 0);
		}
		SERIALIZE(SaveVersionInit, dmgFuncIndex, -1);
		SERIALIZE(SaveVersionInit, dmgFuncMsgIndex, -1);
		if (serialization_getMode() == SMODE_READ)
		{
			damageMod = (DamageModule*)level_alloc(sizeof(DamageModule));
			mod = (ActorModule*)damageMod;
			memset(damageMod, 0, sizeof(DamageModule));
			actor_initModule(mod, (Logic*)dispatch);

			damageMod->attackMod.header.func = actor_getDamageFunc(dmgFuncIndex);
			damageMod->attackMod.header.msgFunc = actor_getDamageMsgFunc(dmgFuncMsgIndex);
			damageMod->attackMod.header.freeFunc = nullptr;
			damageMod->attackMod.header.type = ACTMOD_DAMAGE;
			assert(damageMod->attackMod.header.func && damageMod->attackMod.header.msgFunc);
		}
		assert(damageMod);

		actor_serializeAttackModuleBase(stream, &damageMod->attackMod, dispatch);
		SERIALIZE(SaveVersionInit, damageMod->hp, 0);
		SERIALIZE(SaveVersionInit, damageMod->itemDropId, ITEM_AUTOGUN);
		serialization_serializeDfSound(stream, SaveVersionInit, &damageMod->hurtSndSrc);
		serialization_serializeDfSound(stream, SaveVersionInit, &damageMod->dieSndSrc);
		SERIALIZE(SaveVersionInit, damageMod->stopOnHit, JFALSE);
		SERIALIZE(SaveVersionInit, damageMod->dieEffect, HEFFECT_CANNON_EXP);
	}
		
	void actor_serializeThinkerModule(Stream* stream, ActorModule*& mod, ActorDispatch* dispatch)
	{
		ThinkerModule* thinkerMod;
		if (serialization_getMode() == SMODE_READ)
		{
			thinkerMod = (ThinkerModule*)level_alloc(sizeof(ThinkerModule));
			mod = (ActorModule*)thinkerMod;
			memset(thinkerMod, 0, sizeof(ThinkerModule));
			actor_initModule(mod, (Logic*)dispatch);

			thinkerMod->header.func = defaultThinkerFunc;
			thinkerMod->header.type = ACTMOD_THINKER;
		}
		else
		{
			thinkerMod = (ThinkerModule*)mod;
			assert(thinkerMod->header.func == defaultThinkerFunc || thinkerMod->header.func == flyingModuleFunc
				|| thinkerMod->header.func == flyingModuleFunc_Remote);
			assert(thinkerMod->header.msgFunc == nullptr);
			assert(thinkerMod->header.freeFunc == nullptr);
		}

		actor_serializeTarget(stream, &thinkerMod->target);
		SERIALIZE(SaveVersionInit, thinkerMod->delay, 0);
		SERIALIZE(SaveVersionInit, thinkerMod->maxWalkTime, 0);
		SERIALIZE(SaveVersionInit, thinkerMod->startDelay, 0);
		actor_serializeLogicAnim(stream, &thinkerMod->anim);

		SERIALIZE(SaveVersionInit, thinkerMod->nextTick, 0);
		SERIALIZE(SaveVersionInit, thinkerMod->playerLastSeen, 0);
		SERIALIZE(SaveVersionInit, thinkerMod->prevColTick, 0);

		SERIALIZE(SaveVersionInit, thinkerMod->targetOffset, 0);
		SERIALIZE(SaveVersionInit, thinkerMod->targetVariation, 0);
		SERIALIZE(SaveVersionInit, thinkerMod->approachVariation, 0);
	}
		
	void actor_serializeFlyerModule(Stream* stream, ActorModule*& mod, ActorDispatch* dispatch)
	{
		actor_serializeThinkerModule(stream, mod, dispatch);
		if (serialization_getMode() == SMODE_READ)
		{
			ThinkerModule* thinkerMod = (ThinkerModule*)mod;
			thinkerMod->header.func = flyingModuleFunc;
			thinkerMod->header.type = ACTMOD_FLYER;
		}
	}

	void actor_serializeFlyerRemoteModule(Stream* stream, ActorModule*& mod, ActorDispatch* dispatch)
	{
		actor_serializeThinkerModule(stream, mod, dispatch);
		if (serialization_getMode() == SMODE_READ)
		{
			ThinkerModule* thinkerMod = (ThinkerModule*)mod;
			thinkerMod->header.func = flyingModuleFunc_Remote;
			thinkerMod->header.type = ACTMOD_FLYER_REMOTE;
		}
	}
}  // namespace TFE_DarkForces