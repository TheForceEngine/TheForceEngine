#include <cstring>

#include "generator.h"
#include "time.h"
#include <TFE_Game/igame.h>
#include <TFE_DarkForces/Actor/actor.h>
#include <TFE_DarkForces/random.h>
#include <TFE_DarkForces/util.h>
#include <TFE_DarkForces/player.h>
#include <TFE_DarkForces/Actor/actor.h>
#include <TFE_Jedi/Memory/allocator.h>
#include <TFE_Jedi/InfSystem/message.h>
#include <TFE_Jedi/Level/level.h>
#include <TFE_Jedi/Level/rwall.h>
#include <TFE_Jedi/Collision/collision.h>
#include <TFE_System/system.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_Jedi/Serialization/serialization.h>
#include <TFE_DarkForces/logic.h>
#include <TFE_Settings/settings.h>

using namespace TFE_Jedi;

namespace TFE_DarkForces
{
	struct Generator
	{
		Logic logic;

		KEYWORD    type;
		Tick       delay;
		Tick       interval;
		s32        maxAlive;
		fixed16_16 minDist;
		fixed16_16 maxDist;
		Allocator* entities;
		s32        aliveCount;
		s32        numTerminate;
		Tick       wanderTime;
		Wax*       wax;
		JBool      active;

		char	   logicName[64];		// JK: added to store a custom logic name
	};

	void generatorTaskFunc(MessageType msg)
	{
		struct LocalContext
		{
			Generator* gen;
			SecObject* obj;
		};
		task_begin_ctx;
		local(gen) = (Generator*)task_getUserData();
		local(obj) = local(gen)->logic.obj;

		// Initial delay before it starts working.
		do
		{
			entity_yield(local(gen)->delay);
		} while (msg != MSG_RUN_TASK);

		while (1)
		{
			entity_yield(local(gen)->interval);
			while (local(gen)->numTerminate == 0 && msg == MSG_RUN_TASK)
			{
				entity_yield(TASK_SLEEP);
			}

			if (msg == MSG_FREE)
			{
				Generator* gen = local(gen);
				assert(gen && gen->entities && allocator_validate(gen->entities));

				SecObject* entity = (SecObject*)s_msgEntity;
				SecObject** entityList = (SecObject**)allocator_getHead(gen->entities);
				while (entityList)
				{
					if (entity == *entityList)
					{
						allocator_deleteItem(gen->entities, entityList);
						gen->aliveCount--;
						break;
					}
					entityList = (SecObject**)allocator_getNext(gen->entities);
				}
			}
			else if (msg == MSG_MASTER_ON)
			{
				local(gen)->active |= 1;
			}
			else if (msg == MSG_MASTER_OFF)
			{
				local(gen)->active &= ~1;
			}
			else if (msg == MSG_RUN_TASK && (local(gen)->active & 1) && local(gen)->aliveCount < local(gen)->maxAlive)
			{
				Generator* gen = local(gen);
				SecObject* obj = local(obj);
				SecObject* spawn = allocateObject();
				assert(gen->numTerminate != 0);
				spawn->posWS = obj->posWS;
				spawn->yaw   = random_next() & ANGLE_MASK;
				spawn->worldHeight = FIXED(7);
				sector_addObject(obj->sector, spawn);
				obj_addToRefList(spawn, ObjRefType_GeneratorSpawn);	// scripting

				assert(gen->entities && allocator_validate(gen->entities));

				fixed16_16 dy = TFE_Jedi::abs(s_playerObject->posWS.y - spawn->posWS.y);
				fixed16_16 dist = dy + distApprox(spawn->posWS.x, spawn->posWS.z, s_playerObject->posWS.x, s_playerObject->posWS.z);
				if (dist >= gen->minDist && dist <= gen->maxDist && !actor_canSeeObject(spawn, s_playerObject))
				{
					sprite_setData(spawn, gen->wax);
					
					// Search the externally defined logics for a match
					TFE_ExternalData::CustomActorLogic* customLogic;
					customLogic = tryFindCustomActorLogic(gen->logicName);
					if (customLogic && TFE_Settings::jsonAiLogics())
					{
						obj_setCustomActorLogic(spawn, customLogic);
					}
					else if (gen->type != -1)
					{
						obj_setEnemyLogic(spawn, gen->type);
					}
					
					Logic** head = (Logic**)allocator_getHead_noIterUpdate((Allocator*)spawn->logic);
					if (!head) { break; }		// JK: This is to prevent a crash happening when an invalid logic is set to a generator
					ActorDispatch* actorLogic = *((ActorDispatch**)head);

					actorLogic->flags &= ~ACTOR_IDLE;
					actorLogic->freeTask = task_getCurrent();
					gen->aliveCount++;
					gen->numTerminate--;
					// attackMod may be null, in DOS a global is used and it may reference the previous pointer -
					// so it works by "accident" in DOS.
					if (s_actorState.attackMod)
					{
						if (gen->wanderTime == 0xffffffff)
						{
							s_actorState.attackMod->timing.nextTick = 0xffffffff;
						}
						else
						{
							s32 randomWanderOffset = floor16(random(intToFixed16(gen->wanderTime >> 1)));
							s_actorState.attackMod->timing.nextTick = s_curTick + gen->wanderTime + floor16(randomWanderOffset);
						}
					}

					SecObject** entityPtr = (SecObject**)allocator_newItem(gen->entities);
					if (!entityPtr)
						return;
					*entityPtr = spawn;
					actor_removeRandomCorpse();
				}
				else
				{
					freeObject(spawn);
				}
			}
		}

		task_end;
	}

	void generatorLogicCleanupFunc(Logic* logic)
	{
	}

	JBool generatorLogicSetupFunc(Logic* logic, KEYWORD key)
	{
		Generator* genLogic = (Generator*)logic;

		if (key == KW_MASTER)
		{
			genLogic->active &= ~1;
			return JTRUE;
		}
		else if (key == KW_DELAY)
		{
			genLogic->delay = strToInt(s_objSeqArg1) * 145;
			return JTRUE;
		}
		else if (key == KW_INTERVAL)
		{
			Tick interval = strToInt(s_objSeqArg1) * 145;
			Tick intInSec = strToInt(s_objSeqArg1);
			fixed16_16 halfIntervalInSec = (intInSec - (intInSec < 0 ? 1 : 0)) << 15;
			// Adds a random interval offset: interval = Seconds*145 + floor(random(HalfSeconds))
			genLogic->interval = interval + (Tick)floor16(random(halfIntervalInSec));
			return JTRUE;
		}
		else if (key == KW_MAX_ALIVE)
		{
			genLogic->maxAlive = strToInt(s_objSeqArg1);
			return JTRUE;
		}
		else if (key == KW_MIN_DIST)
		{
			genLogic->minDist = strToInt(s_objSeqArg1) << 16;
			return JTRUE;
		}
		else if (key == KW_MAX_DIST)
		{
			genLogic->maxDist = strToInt(s_objSeqArg1) << 16;
			return JTRUE;
		}
		else if (key == KW_NUM_TERMINATE)
		{
			genLogic->numTerminate = strToInt(s_objSeqArg1);
			return JTRUE;
		}
		else if (key == KW_WANDER_TIME)
		{
			s32 wanderTime = strToInt(s_objSeqArg1);
			if (wanderTime == -1)
			{
				genLogic->wanderTime = 0xffffffffu;
			}
			else 
			{
				genLogic->wanderTime = Tick(wanderTime * 145);
			}
			return JTRUE;
		}
		return JFALSE;
	}

	Logic* obj_createGenerator(SecObject* obj, LogicSetupFunc* setupFunc, KEYWORD genType, const char* logicName)
	{
		Generator* generator = (Generator*)level_alloc(sizeof(Generator));
		memset(generator, 0, sizeof(Generator));

		generator->type   = genType;
		strncpy(generator->logicName, logicName, 63);
		generator->active = 1;
		generator->delay  = 0;

		generator->maxAlive = 4;
		generator->minDist  = FIXED(60);
		generator->interval = floor16(random(FIXED(728))) + 2913;
		generator->maxDist  = FIXED(200);
		generator->entities = allocator_create(sizeof(SecObject**));
		generator->aliveCount   = 0;
		generator->numTerminate = -1;
		generator->wax = obj->wax;
		generator->wanderTime = 17478;

		spirit_setData(obj);

		obj->worldWidth  = 0;
		obj->worldHeight = 0;
		obj->entityFlags |= ETFLAG_CAN_DISABLE;
		*setupFunc = generatorLogicSetupFunc;

		Task* task = createSubTask("Generator", generatorTaskFunc);
		task_setUserData(task, generator);
		obj_addLogic(obj, (Logic*)generator, LOGIC_GENERATOR, task, generatorLogicCleanupFunc);

		return (Logic*)generator;
	}

	// Fixup
	void generatorLogic_fixup(Logic* logic)
	{
		Generator* gen = (Generator*)logic;
		allocator_saveIter(gen->entities);
			SecObject** entityList = (SecObject**)allocator_getHead(gen->entities);
			while (entityList)
			{
				SecObject* obj = *entityList;
				if (obj && obj->logic)
				{
					// Now find the dispatch logic and link it back to the generator.
					allocator_saveIter((Allocator*)obj->logic);
						Logic** logicPtr = (Logic**)allocator_getHead((Allocator*)obj->logic);
						while (logicPtr)
						{
							Logic* logic = *logicPtr;
							if (logic->type == LOGIC_DISPATCH)
							{
								ActorDispatch* actorLogic = (ActorDispatch*)logic;
								actorLogic->freeTask = gen->logic.task;
								break;
							}
							logicPtr = (Logic**)allocator_getNext((Allocator*)obj->logic);
						}
					allocator_restoreIter((Allocator*)obj->logic);
				}
				entityList = (SecObject**)allocator_getNext(gen->entities);
			}
		allocator_restoreIter(gen->entities);
	}

	// Serialization
	void generatorLogic_serialize(Logic*& logic, SecObject* obj, Stream* stream)
	{
		Generator* gen;
		if (serialization_getMode() == SMODE_WRITE)
		{
			gen = (Generator*)logic;
		}
		else
		{
			gen = (Generator*)level_alloc(sizeof(Generator));
			gen->entities = allocator_create(sizeof(SecObject**));
			logic = (Logic*)gen;

			Task* task = createSubTask("Generator", generatorTaskFunc);
			task_setUserData(task, gen);
			gen->logic.task = task;
			gen->logic.cleanupFunc = generatorLogicCleanupFunc;
		}

		SERIALIZE(ObjState_InitVersion, gen->type, KW_UNKNOWN);
		SERIALIZE(ObjState_InitVersion, gen->delay, 0);
		SERIALIZE(ObjState_InitVersion, gen->interval, 0);
		SERIALIZE(ObjState_InitVersion, gen->maxAlive, 0);
		SERIALIZE(ObjState_InitVersion, gen->minDist, 0);
		SERIALIZE(ObjState_InitVersion, gen->maxDist, 0);

		if (serialization_getMode() == SMODE_WRITE)
		{
			s32 count = allocator_getCount(gen->entities);
			SERIALIZE(ObjState_InitVersion, count, 0);
			if (count)
			{
				allocator_saveIter(gen->entities);
					SecObject** entityList = (SecObject**)allocator_getHead(gen->entities);
					while (entityList)
					{
						SecObject* obj = *entityList;
						s32 entityId = (obj) ? obj->serializeIndex : -1;
						SERIALIZE(ObjState_InitVersion, entityId, -1);
						entityList = (SecObject**)allocator_getNext(gen->entities);
					}
				allocator_restoreIter(gen->entities);
			}
		}
		else
		{
			s32 count;
			SERIALIZE(ObjState_InitVersion, count, 0);
			for (s32 i = 0; i < count; i++)
			{
				s32 entityId;
				SERIALIZE(ObjState_InitVersion, entityId, -1);
				if (entityId < 0) { continue; }

				SecObject** entityPtr = (SecObject**)allocator_newItem(gen->entities);
				if (!entityPtr)
					return;
				*entityPtr = objData_getObjectBySerializationId_NoValidation(entityId);
			}
		}

		SERIALIZE(ObjState_InitVersion, gen->aliveCount, 0);
		SERIALIZE(ObjState_InitVersion, gen->numTerminate, 0);
		SERIALIZE(ObjState_InitVersion, gen->wanderTime, 0);
		serialization_serializeWaxPtr(stream, ObjState_InitVersion, gen->wax);
		SERIALIZE(ObjState_InitVersion, gen->active, 0);
		
		u32 len = 0;
		if (serialization_getMode() == SMODE_WRITE)
		{
			len = (u32)strlen(gen->logicName);
		}
		SERIALIZE(ObjState_CustomLogics, len, 0);
		SERIALIZE_BUF(ObjState_CustomLogics, gen->logicName, len);
		gen->logicName[len] = 0;
	}
}  // TFE_DarkForces