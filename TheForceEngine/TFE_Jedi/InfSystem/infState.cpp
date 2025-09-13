#include "infState.h"
#include "infTypesInternal.h"
#include "infSystem.h"
#include <TFE_DarkForces/sound.h>
#include <TFE_Jedi/Serialization/serialization.h>
#include <TFE_Jedi/Memory/allocator.h>
#include <TFE_Jedi/Level/level.h>
#include <TFE_Jedi/Level/levelData.h>
#include <cstring>

using namespace TFE_DarkForces;

namespace TFE_Jedi
{
	enum InfStateVersion : u32
	{
		InfState_InitVersion = 1,
		InfState_ScriptCall,
		InfState_CurVersion = InfState_ScriptCall,
	};

	// INF State
	InfSerializableState s_infSerState = { };
	InfState s_infState = { };

	static std::vector<InfScriptCall*> s_infScriptCallsToFixup;

	/////////////////////////////////////////////
	// Forward Declarations
	/////////////////////////////////////////////
	void inf_serializeStop(Stream* stream, Stop* stop);
	void inf_serializeSlave(Stream* stream, Slave* slave);
	void inf_serializeLink(Stream* stream, InfLink* link, Allocator* parent);
	void inf_serializeFixupLinks();

	void inf_computeElevValuePointer(InfElevator* elev);
	extern void inf_deleteElevator(InfElevator* elev);
	extern void inf_deleteTrigger(InfTrigger* trigger);

	/////////////////////////////////////////////
	// Implementation
	/////////////////////////////////////////////
	void inf_clearState()
	{
		s_infSerState = { 0 };
		s_infState = { 0 };
	}

	void inf_serializeElevator(Stream* stream, InfElevator* elev)
	{
		SERIALIZE(InfState_InitVersion, elev->deleted, 0);
		if (elev->deleted)
		{
			if (serialization_getMode() == SMODE_READ)
			{
				memset(elev, 0, sizeof(InfElevator));
				elev->deleted = JTRUE;
			}
			return;
		}

		const vec2_fixed def = { 0 };
		if (serialization_getMode() == SMODE_READ)
		{
			elev->deleted = JFALSE;
			elev->self = elev;
		}
				
		RSector* sector = elev->sector;
		SERIALIZE(InfState_InitVersion, elev->type, IELEV_COUNT);
		SERIALIZE(InfState_InitVersion, elev->trigMove, TRIGMOVE_HOLD);
		serialization_serializeSectorPtr(stream, InfState_InitVersion, sector);
		if (serialization_getMode() == SMODE_READ)
		{
			elev->sector = sector;
		}

		SERIALIZE(InfState_InitVersion, elev->key, KEY_NONE);
		SERIALIZE(InfState_InitVersion, elev->fixedStep, 0);
		SERIALIZE(InfState_InitVersion, elev->nextTick, 0);
		SERIALIZE(InfState_InitVersion, elev->timer, 0);

		// Matching sector link.
		InfLink* elevLink = nullptr;
		if (serialization_getMode() == SMODE_WRITE)
		{
			if (sector && sector->infLink)
			{
				allocator_saveIter(sector->infLink);
				InfLink* link = (InfLink*)allocator_getHead(sector->infLink);
				while (link)
				{
					if (link->type == LTYPE_SECTOR && link->elev == elev)
					{
						elevLink = link;
						break;
					}
					link = (InfLink*)allocator_getNext(sector->infLink);
				}
				allocator_restoreIter(sector->infLink);
			}
			RSector* linkSector = elevLink ? sector : nullptr;
			serialization_serializeSectorPtr(stream, InfState_InitVersion, linkSector);
		}
		else
		{
			RSector* linkSector;
			serialization_serializeSectorPtr(stream, InfState_InitVersion, linkSector);
			if (linkSector)
			{
				assert(linkSector == sector);
				if (!linkSector->infLink)
				{
					linkSector->infLink = allocator_create(sizeof(InfLink));
				}
				elevLink = (InfLink*)allocator_newItem(linkSector->infLink);
				if (!elevLink)
					return;
			}
		}
		if (elevLink)
		{
			inf_serializeLink(stream, elevLink, sector->infLink);
		}

		// Serialize the current stop allocator position.
		s32 stopPos, stopPrev;
		if (serialization_getMode() == SMODE_WRITE)
		{
			stopPos = allocator_getCurPos(elev->stops);
			stopPrev = allocator_getPrevPos(elev->stops);
		}
		SERIALIZE(InfState_InitVersion, stopPos, -1);
		SERIALIZE(InfState_InitVersion, stopPrev, -1);

		// Stops
		s32 stopCount;
		if (serialization_getMode() == SMODE_WRITE)
		{
			allocator_saveIter(elev->stops);
			stopCount = allocator_getCount(elev->stops);
			assert(!elev->stops || stopCount);

			SERIALIZE(InfState_InitVersion, stopCount, 0);
			Stop* stop = (Stop*)allocator_getHead(elev->stops);
			while (stop)
			{
				inf_serializeStop(stream, stop);
				stop = (Stop*)allocator_getNext(elev->stops);
			}
			allocator_restoreIter(elev->stops);
		}
		else
		{
			SERIALIZE(InfState_InitVersion, stopCount, 0);
			elev->stops = stopCount ? allocator_create(sizeof(Stop)) : nullptr;
			for (s32 s = 0; s < stopCount; s++)
			{
				Stop* stop = (Stop*)allocator_newItem(elev->stops);
				if (!stop)
					return;
				inf_serializeStop(stream, stop);
			}

			// Set stop position.
			if (stopPos >= 0)
			{
				assert(stopCount);
				allocator_setPos(elev->stops, stopPos);
			}
			if (stopPrev >= 0)
			{
				assert(stopCount);
				allocator_setPrevPos(elev->stops, stopPrev);
			}
		}

		// Slaves
		s32 slaveCount;
		if (serialization_getMode() == SMODE_WRITE)
		{
			allocator_saveIter(elev->slaves);
			slaveCount = allocator_getCount(elev->slaves);
			SERIALIZE(InfState_InitVersion, slaveCount, 0);
			Slave* slave = (Slave*)allocator_getHead(elev->slaves);
			while (slave)
			{
				inf_serializeSlave(stream, slave);
				slave = (Slave*)allocator_getNext(elev->slaves);
			}
			allocator_restoreIter(elev->slaves);
		}
		else
		{
			SERIALIZE(InfState_InitVersion, slaveCount, 0);
			elev->slaves = allocator_create(sizeof(Slave));
			for (s32 s = 0; s < slaveCount; s++)
			{
				Slave* slave = (Slave*)allocator_newItem(elev->slaves);
				if (!slave)
					return;
				inf_serializeSlave(stream, slave);
			}
		}

		// Next Stop
		s32 stopIndex;
		if (serialization_getMode() == SMODE_WRITE)
		{
			stopIndex = elev->nextStop ? elev->nextStop->index : -1;
		}
		SERIALIZE(InfState_InitVersion, stopIndex, -1);
		if (serialization_getMode() == SMODE_READ)
		{
			elev->nextStop = (stopIndex < 0) ? nullptr : (Stop*)allocator_getByIndex(elev->stops, stopIndex);
		}

		SERIALIZE(InfState_InitVersion, elev->speed, 0);
		SERIALIZE(InfState_InitVersion, elev->iValue, 0);
		SERIALIZE(InfState_InitVersion, elev->dirOrCenter, def);
		SERIALIZE(InfState_InitVersion, elev->flags, 0);
		// Compute the elev->value pointer.
		if (serialization_getMode() == SMODE_READ)
		{
			inf_computeElevValuePointer(elev);
		}

		serialization_serializeDfSound(stream, InfState_InitVersion, &elev->sound0);
		serialization_serializeDfSound(stream, InfState_InitVersion, &elev->sound1);
		serialization_serializeDfSound(stream, InfState_InitVersion, &elev->sound2);

		if (serialization_getMode() == SMODE_READ)
		{
			// elev->loopingSoundID is not serialized, the sound will resume on the next update.
			elev->loopingSoundID = NULL_SOUND;
		}

		SERIALIZE(InfState_InitVersion, elev->updateFlags, 0);
		SERIALIZE(InfState_InitVersion, elev->prevValue, 0);
	}

	void inf_serializeTeleport(Stream* stream, Teleport* teleport)
	{
		const vec3_fixed def = { 0 };

		serialization_serializeSectorPtr(stream, InfState_InitVersion, teleport->sector);
		serialization_serializeSectorPtr(stream, InfState_InitVersion, teleport->target);
		SERIALIZE(InfState_InitVersion, teleport->type, TELEPORT_BASIC);
		SERIALIZE(InfState_InitVersion, teleport->dstPosition, def);
		SERIALIZE_BUF(InfState_InitVersion, teleport->dstAngle, 3 * sizeof(angle14_16));

		// Matching sector link.
		InfLink* teleportLink = nullptr;
		RSector* sector = teleport->sector;
		RSector* linkSector = nullptr;
		if (serialization_getMode() == SMODE_WRITE)
		{
			if (sector->infLink)
			{
				allocator_saveIter(sector->infLink);
				InfLink* link = (InfLink*)allocator_getHead(sector->infLink);
				while (link)
				{
					if (link->type == LTYPE_TELEPORT && link->teleport == teleport)
					{
						teleportLink = link;
						linkSector = sector;
						break;
					}
					link = (InfLink*)allocator_getNext(sector->infLink);
				}
				allocator_restoreIter(sector->infLink);
			}
			serialization_serializeSectorPtr(stream, InfState_InitVersion, linkSector);
			if (linkSector)
			{
				inf_serializeLink(stream, teleportLink, linkSector->infLink);
			}
		}
		else  // SMODE_READ
		{
			RSector* linkSector;
			serialization_serializeSectorPtr(stream, InfState_InitVersion, linkSector);
			if (linkSector)
			{
				if (!linkSector->infLink)
				{
					linkSector->infLink = allocator_create(sizeof(InfLink));
				}
				Allocator* parent = linkSector->infLink;
				InfLink* link = (InfLink*)allocator_newItem(parent);
				if (!link)
					return;
				inf_serializeLink(stream, link, parent);
			}
		}
	}

	void inf_serializeTarget(Stream* stream, TriggerTarget* target)
	{
		RSector* targetSector = nullptr;
		s32 wallIndex = -1;
		if (serialization_getMode() == SMODE_WRITE)
		{
			if (target->sector)
			{
				targetSector = target->sector;
			}
			else if (target->wall && target->wall->sector)
			{
				targetSector = target->wall->sector;
			}
			if (target->wall)
			{
				wallIndex = target->wall->id;
			}
		}
		serialization_serializeSectorPtr(stream, InfState_InitVersion, targetSector);
		SERIALIZE(InfState_InitVersion, wallIndex, -1);
		if (serialization_getMode() == SMODE_READ)
		{
			target->sector = nullptr;
			target->wall = nullptr;
			if (wallIndex >= 0 && targetSector)
			{
				target->wall = &targetSector->walls[wallIndex];
			}
			else if (targetSector)
			{
				target->sector = targetSector;
			}
		}
		SERIALIZE(InfState_InitVersion, target->eventMask, 0);
	}

	void inf_serializeTrigger(Stream* stream, InfTrigger* trigger)
	{
		SERIALIZE(InfState_InitVersion, trigger->deleted, 0);
		if (trigger->deleted)
		{
			if (serialization_getMode() == SMODE_READ)
			{
				memset(trigger, 0, sizeof(InfTrigger));
				trigger->deleted = JTRUE;
			}
			return;
		}
		SERIALIZE(InfState_InitVersion, trigger->type, ITRIGGER_WALL);

		// Inf Link
		s32 parentWallIndex = -1;
		RSector* parentSector = nullptr;
		if (serialization_getMode() == SMODE_WRITE)
		{
			if (trigger->type == ITRIGGER_SECTOR)
			{
				parentSector = (RSector*)trigger->parent;
				assert(parentSector && parentSector->id == parentSector->index);
			}
			else
			{
				RWall* wall = (RWall*)trigger->parent;
				parentSector = wall->sector;
				parentWallIndex = wall->id;
				assert(parentSector && parentSector->id == parentSector->index && parentWallIndex >= 0 && parentWallIndex < parentSector->wallCount);
			}
		}
		serialization_serializeSectorPtr(stream, InfState_InitVersion, parentSector);
		SERIALIZE(InfState_InitVersion, parentWallIndex, -1);
		if (serialization_getMode() == SMODE_READ)
		{
			assert(parentSector && parentSector->id == parentSector->index);
			trigger->parent = (trigger->type == ITRIGGER_SECTOR) ? (void*)parentSector : (void*)&parentSector->walls[parentWallIndex];
		}
		InfLink* link = nullptr;
		Allocator* parent = nullptr;
		RWall* triggerWall = nullptr;
		if (serialization_getMode() == SMODE_WRITE)
		{
			link = trigger->link;
			assert(link->type == LTYPE_TRIGGER);
		}
		else  // SMODE_READ
		{
			if (trigger->type == ITRIGGER_SECTOR && parentSector)
			{
				if (!parentSector->infLink)
				{
					parentSector->infLink = allocator_create(sizeof(InfLink));
				}
				parent = parentSector->infLink;
				link = (InfLink*)allocator_newItem(parentSector->infLink);
				if (!link)
					return;
			}
			else if (parentSector && parentWallIndex >= 0)
			{
				RWall* wall = &parentSector->walls[parentWallIndex];
				if (!wall->infLink)
				{
					wall->infLink = allocator_create(sizeof(InfLink));
				}
				parent = wall->infLink;
				triggerWall = wall;
				link = (InfLink*)allocator_newItem(wall->infLink);
				if (!link)
					return;
			}
			trigger->link = link;
		}
		inf_serializeLink(stream, trigger->link, parent);

		// Animated Texture
		serialization_serializeAnimatedTexturePtr(stream, InfState_InitVersion, trigger->animTex);

		// targets
		if (serialization_getMode() == SMODE_WRITE)
		{
			allocator_saveIter(trigger->targets);
				s32 targetCount = allocator_getCount(trigger->targets);
				SERIALIZE(InfState_InitVersion, targetCount, 0);
				TriggerTarget* target = (TriggerTarget*)allocator_getHead(trigger->targets);
				while (target)
				{
					inf_serializeTarget(stream, target);
					target = (TriggerTarget*)allocator_getNext(trigger->targets);
				}
			allocator_restoreIter(trigger->targets);
		}
		else  // SMODE_READ
		{
			s32 targetCount;
			SERIALIZE(InfState_InitVersion, targetCount, 0);
			trigger->targets = allocator_create(sizeof(TriggerTarget));
			for (s32 i = 0; i < targetCount; i++)
			{
				TriggerTarget* target = (TriggerTarget*)allocator_newItem(trigger->targets);
				if (!target)
					return;
				inf_serializeTarget(stream, target);
			}
		}

		SERIALIZE(InfState_InitVersion, trigger->cmd, MSG_RUN_TASK);
		SERIALIZE(InfState_InitVersion, trigger->event, 0);
		SERIALIZE(InfState_InitVersion, trigger->arg0, 0);
		SERIALIZE(InfState_InitVersion, trigger->arg1, 0);
		SERIALIZE(InfState_InitVersion, trigger->master, 0);

		// trigger->tex
		s32 frameIndex = -1;
		if (serialization_getMode() == SMODE_WRITE)
		{
			AnimatedTexture* animTex = trigger->animTex;
			if (trigger->tex && animTex)
			{
				// Which frame is it currently on?
				for (s32 f = 0; f < animTex->count; f++)
				{
					if (trigger->tex == animTex->frameList[f])
					{
						frameIndex = f;
						break;
					}
				}
				assert(frameIndex >= 0);
			}
		}
		SERIALIZE(InfState_InitVersion, frameIndex, -1);
		if (serialization_getMode() == SMODE_READ)
		{
			if (frameIndex >= 0)
			{
				AnimatedTexture* animTex = trigger->animTex;
				if (animTex)
				{
					assert(frameIndex < animTex->count);
					trigger->tex = animTex->frameList[frameIndex];
					if (triggerWall)
					{
						triggerWall->signTex = &trigger->tex;
					}
				}
			}
			else
			{
				assert(!trigger->animTex);
				trigger->animTex = nullptr;
			}
		}

		serialization_serializeDfSound(stream, InfState_InitVersion, &trigger->soundId);
		SERIALIZE(InfState_InitVersion, trigger->state, 0);
		SERIALIZE(InfState_InitVersion, trigger->textId, 0);
	}
		
	void inf_serialize(Stream* stream)
	{
		s_infScriptCallsToFixup.clear();

		SERIALIZE_VERSION(InfState_CurVersion);
		
		s32 elevCount, teleCount, trigCount;
		if (serialization_getMode() == SMODE_WRITE)
		{
			elevCount = allocator_getCount(s_infSerState.infElevators);
			teleCount = allocator_getCount(s_infSerState.infTeleports);
			trigCount = allocator_getCount(s_infSerState.infTriggers);
		}
		// Counts.
		SERIALIZE(InfState_InitVersion, s_infSerState.activeTriggerCount, 0);
		SERIALIZE(InfState_InitVersion, elevCount, 0);
		SERIALIZE(InfState_InitVersion, teleCount, 0);
		SERIALIZE(InfState_InitVersion, trigCount, 0);

		// Elevators.
		const vec2_fixed def = { 0 };
		if (serialization_getMode() == SMODE_WRITE)
		{
			allocator_saveIter(s_infSerState.infElevators);
			InfElevator* elev = (InfElevator*)allocator_getHead(s_infSerState.infElevators);
			while (elev)
			{
				inf_serializeElevator(stream, elev);
				elev = (InfElevator*)allocator_getNext(s_infSerState.infElevators);
			}
			allocator_restoreIter(s_infSerState.infElevators);
		}
		else // Read
		{
			for (s32 i = 0; i < elevCount; i++)
			{
				InfElevator* elev = (InfElevator*)allocator_newItem(s_infSerState.infElevators);
				if (!elev)
					return;
				inf_serializeElevator(stream, elev);
			}
		}

		// Teleports
		if (serialization_getMode() == SMODE_WRITE)
		{
			allocator_saveIter(s_infSerState.infTeleports);
			Teleport* teleport = (Teleport*)allocator_getHead(s_infSerState.infTeleports);
			while (teleport)
			{
				inf_serializeTeleport(stream, teleport);
				teleport = (Teleport*)allocator_getNext(s_infSerState.infTeleports);
			}
			allocator_restoreIter(s_infSerState.infTeleports);
		}
		else  // SMODE_READ
		{
			for (s32 i = 0; i < teleCount; i++)
			{
				Teleport* teleport = (Teleport*)allocator_newItem(s_infSerState.infTeleports);
				if (!teleport)
					return;
				inf_serializeTeleport(stream, teleport);
			}
		}

		// Triggers
		if (serialization_getMode() == SMODE_WRITE)
		{
			allocator_saveIter(s_infSerState.infTriggers);
			InfTrigger* trigger = (InfTrigger*)allocator_getHead(s_infSerState.infTriggers);
			while (trigger)
			{
				inf_serializeTrigger(stream, trigger);
				trigger = (InfTrigger*)allocator_getNext(s_infSerState.infTriggers);
			}
			allocator_restoreIter(s_infSerState.infTriggers);
		}
		else  // SMODE_READ
		{
			for (s32 i = 0; i < trigCount; i++)
			{
				InfTrigger* trigger = (InfTrigger*)allocator_newItem(s_infSerState.infTriggers);
				if (!trigger)
					return;
				inf_serializeTrigger(stream, trigger);
			}
		}

		// Fixup
		if (serialization_getMode() == SMODE_READ)
		{
			inf_serializeFixupLinks();
		}
	}

	/////////////////////////////////////////////
	// Internal - Serialize
	/////////////////////////////////////////////
	void inf_serializeMessage(Stream* stream, InfMessage* msg)
	{
		RSector* sector = nullptr;
		s32 wallIndex = -1;
		if (serialization_getMode() == SMODE_WRITE)
		{
			if (msg->sector)
			{
				sector = msg->sector;
			}
			else if (msg->wall)
			{
				sector = msg->wall->sector;
			}
			wallIndex = (msg->wall) ? msg->wall->id : -1;
		}
		// It is possible for both wallIndex and sectorIndex to be null.
		serialization_serializeSectorPtr(stream, InfState_InitVersion, sector);
		SERIALIZE(InfState_InitVersion, wallIndex, -1);
		if (serialization_getMode() == SMODE_READ)
		{
			msg->wall = nullptr;
			msg->sector = nullptr;
			if (wallIndex >= 0)
			{
				msg->wall = sector ? &sector->walls[wallIndex] : nullptr;
			}
			else
			{
				msg->sector = sector;
			}
		}

		SERIALIZE(InfState_InitVersion, msg->msgType, MSG_COUNT);
		SERIALIZE(InfState_InitVersion, msg->event, 0);
		SERIALIZE(InfState_InitVersion, msg->arg1, 0);
		SERIALIZE(InfState_InitVersion, msg->arg2, 0);
	}

	void inf_serializeSciptCall(Stream* stream, Stop* stop, InfScriptCall* call)
	{
		SERIALIZE(InfState_ScriptCall, call->argCount, 0);
		for (s32 i = 0; i < call->argCount; i++)
		{
			serialization_serializeScriptArg(stream, InfState_ScriptCall, &call->args[i]);
		}
		SERIALIZE_CSTRING(InfState_ScriptCall, call->funcName);
		if (serialization_getMode() == SMODE_READ)
		{
			call->funcPtr = nullptr;
			s_infScriptCallsToFixup.push_back(call);
		}
	}

	void inf_fixupScriptCalls()
	{
		const s32 count = (s32)s_infScriptCallsToFixup.size();
		for (s32 i = 0; i < count; i++)
		{
			InfScriptCall* call = s_infScriptCallsToFixup[i];
			call->funcPtr = getLevelScriptFunc(call->funcName);
		}
		s_infScriptCallsToFixup.clear();
	}

	void inf_serializeAdjoin(Stream* stream, Stop* stop, AdjoinCmd* adjCmd)
	{
		serialization_serializeSectorPtr(stream, InfState_InitVersion, adjCmd->sector0);
		serialization_serializeSectorPtr(stream, InfState_InitVersion, adjCmd->sector1);

		if (serialization_getMode() == SMODE_WRITE)
		{
			s32 id0 = adjCmd->wall0 ? adjCmd->wall0->id : -1;
			s32 id1 = adjCmd->wall1 ? adjCmd->wall1->id : -1;
			SERIALIZE(InfState_InitVersion, id0, 0);
			SERIALIZE(InfState_InitVersion, id1, 0);
		}
		else // SMODE_READ
		{
			s32 wall0Id, wall1Id;
			SERIALIZE(InfState_InitVersion, wall0Id, 0);
			SERIALIZE(InfState_InitVersion, wall1Id, 0);
			adjCmd->wall0 = wall0Id >= 0 ? &adjCmd->sector0->walls[wall0Id] : nullptr;
			adjCmd->wall1 = wall1Id >= 0 ? &adjCmd->sector1->walls[wall1Id] : nullptr;
		}
	}

	void inf_serializeStop(Stream* stream, Stop* stop)
	{
		SERIALIZE(InfState_InitVersion, stop->value, 0);
		SERIALIZE(InfState_InitVersion, stop->delay, 0);

		// Messages
		s32 msgCount;
		if (serialization_getMode() == SMODE_WRITE)
		{
			msgCount = allocator_getCount(stop->messages);
		}
		SERIALIZE(InfState_InitVersion, msgCount, 0);
		if (serialization_getMode() == SMODE_WRITE)
		{
			allocator_saveIter(stop->messages);
			InfMessage* msg = (InfMessage*)allocator_getHead(stop->messages);
			while (msg)
			{
				inf_serializeMessage(stream, msg);
				msg = (InfMessage*)allocator_getNext(stop->messages);
			}
			allocator_restoreIter(stop->messages);
		}
		else // SMODE_READ
		{
			stop->messages = allocator_create(sizeof(InfMessage));
			for (s32 m = 0; m < msgCount; m++)
			{
				InfMessage* msg = (InfMessage*)allocator_newItem(stop->messages);
				if (!msg)
					return;
				inf_serializeMessage(stream, msg);
			}
		}

		// Script Calls
		s32 scriptCallCount = 0;
		if (serialization_getMode() == SMODE_WRITE)
		{
			scriptCallCount = allocator_getCount(stop->scriptCalls);
		}
		SERIALIZE(InfState_ScriptCall, scriptCallCount, 0);
		if (serialization_getMode() == SMODE_WRITE)
		{
			allocator_saveIter(stop->scriptCalls);
			InfScriptCall* call = (InfScriptCall*)allocator_getHead(stop->scriptCalls);
			while (call)
			{
				inf_serializeSciptCall(stream, stop, call);
				call = (InfScriptCall*)allocator_getNext(stop->scriptCalls);
			}
			allocator_restoreIter(stop->scriptCalls);
		}
		else if (scriptCallCount > 0) // SMODE_READ
		{
			stop->scriptCalls = allocator_create(sizeof(InfScriptCall));
			for (s32 c = 0; c < scriptCallCount; c++)
			{
				InfScriptCall* call = (InfScriptCall*)allocator_newItem(stop->scriptCalls);
				if (!call) { return; } 
				inf_serializeSciptCall(stream, stop, call);
			}
		}

		// Adjoin Commands
		s32 adjCount;
		if (serialization_getMode() == SMODE_WRITE)
		{
			adjCount = allocator_getCount(stop->adjoinCmds);
		}
		SERIALIZE(InfState_InitVersion, adjCount, 0);
		if (serialization_getMode() == SMODE_WRITE)
		{
			allocator_saveIter(stop->adjoinCmds);
			AdjoinCmd* adjCmd = (AdjoinCmd*)allocator_getHead(stop->adjoinCmds);
			while (adjCmd)
			{
				inf_serializeAdjoin(stream, stop, adjCmd);
				adjCmd = (AdjoinCmd*)allocator_getNext(stop->adjoinCmds);
			}
			allocator_restoreIter(stop->adjoinCmds);
		}
		else  // SMODE_READ
		{
			stop->adjoinCmds = allocator_create(sizeof(AdjoinCmd));
			for (s32 a = 0; a < adjCount; a++)
			{
				AdjoinCmd* adjCmd = (AdjoinCmd*)allocator_newItem(stop->adjoinCmds);
				if (!adjCmd) { return; }
				inf_serializeAdjoin(stream, stop, adjCmd);
			}
		}

		serialization_serializeDfSound(stream, InfState_InitVersion, &stop->pageId);
		SERIALIZE(InfState_InitVersion, stop->floorTexSecId, -1);
		SERIALIZE(InfState_InitVersion, stop->ceilTexSecId, -1);
		SERIALIZE(InfState_InitVersion, stop->index, -1);

		if (serialization_getMode() == SMODE_READ)
		{
			stop->floorTex = nullptr;
			stop->ceilTex  = nullptr;
			if (stop->floorTexSecId >= 0)
			{
				stop->floorTex = s_levelState.sectors[stop->floorTexSecId].floorTex;
			}
			if (stop->ceilTexSecId >= 0)
			{
				stop->ceilTex = s_levelState.sectors[stop->ceilTexSecId].ceilTex;
			}
		}
	}

	void inf_serializeSlave(Stream* stream, Slave* slave)
	{
		serialization_serializeSectorPtr(stream, InfState_InitVersion, slave->sector);
		SERIALIZE(InfState_InitVersion, slave->value, 0);
	}

	void inf_serializeFixupLink(InfLink* link)
	{
		const s32 index = link->serializeIndex;
		switch (link->type)
		{
			case LTYPE_SECTOR:
			{
				link->elev = (InfElevator*)allocator_getByIndex(s_infSerState.infElevators, index);
			} break;
			case LTYPE_TRIGGER:
			{
				link->trigger = (InfTrigger*)allocator_getByIndex(s_infSerState.infTriggers, index);
			} break;
			case LTYPE_TELEPORT:
			{
				link->teleport = (Teleport*)allocator_getByIndex(s_infSerState.infTeleports, index);
			} break;
		}
	}

	void inf_serializeFixupLinks()
	{
		RSector* sector = s_levelState.sectors;
		for (u32 s = 0; s < s_levelState.sectorCount; s++, sector++)
		{
			allocator_saveIter(sector->infLink);
				InfLink* link = (InfLink*)allocator_getHead(sector->infLink);
				while (link)
				{
					inf_serializeFixupLink(link);
					link = (InfLink*)allocator_getNext(sector->infLink);
				}
			allocator_restoreIter(sector->infLink);

			s32 wallCount = sector->wallCount;
			RWall* wall = sector->walls;
			for (s32 w = 0; w < wallCount; w++, wall++)
			{
				allocator_saveIter(wall->infLink);
					link = (InfLink*)allocator_getHead(wall->infLink);
					while (link)
					{
						inf_serializeFixupLink(link);
						link = (InfLink*)allocator_getNext(wall->infLink);
					}
				allocator_restoreIter(wall->infLink);
			}
		}
	}

	void inf_serializeLink(Stream* stream, InfLink* link, Allocator* parent)
	{
		SERIALIZE(InfState_InitVersion, link->type, LTYPE_SECTOR);
		// Task function is derived from the type.
		s32 index = -1;
		if (serialization_getMode() == SMODE_WRITE)
		{
			switch (link->type)
			{
				case LTYPE_SECTOR:
				{
					index = allocator_getIndex(s_infSerState.infElevators, link->elev);
				} break;
				case LTYPE_TRIGGER:
				{
					index = allocator_getIndex(s_infSerState.infTriggers, link->trigger);
				} break;
				case LTYPE_TELEPORT:
				{
					index = allocator_getIndex(s_infSerState.infTeleports, link->teleport);
				} break;
			}
		}
		SERIALIZE(InfState_InitVersion, index, -1);
		if (serialization_getMode() == SMODE_READ)
		{
			link->serializeIndex = index;
			switch (link->type)
			{
				case LTYPE_SECTOR:
				{
					link->elev = nullptr;
					link->task = s_infState.infElevTask;
					link->freeFunc = (InfFreeFunc)inf_deleteElevator;
				} break;
				case LTYPE_TRIGGER:
				{
					link->trigger = nullptr;
					link->task = s_infState.infTriggerTask;
					link->freeFunc = (InfFreeFunc)inf_deleteTrigger;
				} break;
				case LTYPE_TELEPORT:
				{
					link->teleport = nullptr;
					link->task = s_infState.teleportTask;
					link->freeFunc = nullptr;
				} break;
			}
		}

		SERIALIZE(InfState_InitVersion, link->eventMask, 0);
		SERIALIZE(InfState_InitVersion, link->entityMask, 0);

		if (serialization_getMode() == SMODE_READ)
		{
			link->parent = parent;
		}
	}


	/////////////////////////////////////////////
	// Internal - Deserialize
	/////////////////////////////////////////////
	void inf_computeElevValuePointer(InfElevator* elev)
	{
		RSector* sector = elev->sector;
		switch (elev->type)
		{
			case IELEV_MOVE_CEILING:
				elev->value = &sector->ceilingHeight;
				break;
			case IELEV_MOVE_FLOOR:
			case IELEV_MOVE_FC:
				elev->value = &sector->floorHeight;
				break;
			case IELEV_MOVE_OFFSET:
				elev->value = &sector->secHeight;
				break;
			case IELEV_CHANGE_LIGHT:
				elev->value = &sector->ambient;
				break;
			case IELEV_MOVE_WALL:
			case IELEV_ROTATE_WALL:
			case IELEV_SCROLL_WALL:
			case IELEV_SCROLL_FLOOR:
			case IELEV_SCROLL_CEILING:
			case IELEV_CHANGE_WALL_LIGHT:
				elev->value = &elev->iValue;
				break;
			default:
				// It shouldn't reach here.
				assert(0);
		};
	}
}
