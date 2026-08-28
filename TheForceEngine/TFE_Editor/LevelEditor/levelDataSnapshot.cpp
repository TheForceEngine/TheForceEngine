#include "levelEditorData.h"
#include "levelDataSnapshot.h"
#include "sharedState.h"
#include "guidelines.h"
#include "levelEditorInf.h"
#include <TFE_Editor/snapshotReaderWriter.h>
#include <TFE_Editor/history.h>
#include <TFE_Editor/errorMessages.h>

#include <climits>
#include <algorithm>
#include <vector>
#include <string>

using namespace TFE_Editor;

namespace LevelEditor
{
	void writeEntityVar(const std::vector<EntityVar>& varList)
	{
		const s32 varCount = (s32)varList.size();
		const EntityVar* var = varList.data();

		writeS32(varCount);
		for (s32 v = 0; v < varCount; v++, var++)
		{
			writeS32(var->defId);
			writeS32(var->value.iValue);
			writeString(var->value.sValue);
			writeString(var->value.sValue1);
		}
	}

	void writeEntityLogic(const std::vector<EntityLogic>& logicList)
	{
		const s32 count = (s32)logicList.size();
		const EntityLogic* logic = logicList.data();
		writeS32(count);
		for (s32 i = 0; i < count; i++, logic++)
		{
			writeString(logic->name);
			writeEntityVar(logic->var);
		}
	}

	void readEntityVar(std::vector<EntityVar>& varList)
	{
		const s32 varCount = readS32();
		varList.resize(varCount);

		EntityVar* var = varList.data();
		for (s32 v = 0; v < varCount; v++, var++)
		{
			var->defId = readS32();
			var->value.iValue = readS32();
			readString(var->value.sValue);
			readString(var->value.sValue1);
		}
	}

	void readEntityLogic(std::vector<EntityLogic>& logicList)
	{
		const s32 count = readS32();
		logicList.resize(count);

		EntityLogic* logic = logicList.data();
		for (s32 i = 0; i < count; i++, logic++)
		{
			readString(logic->name);
			readEntityVar(logic->var);
		}
	}
		
	s32 addUniqueTexture(s32 id, std::vector<UniqueTexture>& uniqueTex)
	{
		const s32 count = (s32)uniqueTex.size();
		UniqueTexture* tex = uniqueTex.data();
		for (s32 i = 0; i < count; i++, tex++)
		{
			if (tex->originalId == id)
			{
				return tex->newId;
			}
		}

		UniqueTexture newTex;
		newTex.originalId = id;
		newTex.newId = (s32)uniqueTex.size();
		newTex.name = s_level.textures[id].name;
		uniqueTex.push_back(newTex);
		return newTex.newId;
	}

	s32 addUniqueEntity(s32 id, std::vector<UniqueEntity>& uniqueEntity)
	{
		const s32 count = (s32)uniqueEntity.size();
		UniqueEntity* uentity = uniqueEntity.data();
		for (s32 i = 0; i < count; i++, uentity++)
		{
			if (uentity->originalId == id)
			{
				return uentity->newId;
			}
		}

		UniqueEntity newEntity;
		newEntity.originalId = id;
		newEntity.newId = (s32)uniqueEntity.size();
		newEntity.entity = s_level.entities[id];
		uniqueEntity.push_back(newEntity);
		return newEntity.newId;
	}
		
	void writeEntityToSnapshot(const Entity* entity)
	{
		writeS32(entity->id);
		writeS32(entity->categories);
		writeString(entity->name);
		writeString(entity->assetName);
		writeS32((s32)entity->type);

		writeEntityLogic(entity->logic);
		writeEntityVar(entity->var);
		writeData(entity->bounds, sizeof(Vec3f) * 2);
		writeData(&entity->offset, sizeof(Vec3f));
		writeData(&entity->offsetAdj, sizeof(Vec3f));

		// Sprite and obj data derived from type + assetName
	}

	void writeSectorToSnapshot(const EditorSector* sector)
	{
		writeU32(sector->id);
		writeU32(sector->groupId);
		writeU32(sector->groupIndex);
		writeString(sector->name);
		writeData(&sector->floorTex, sizeof(LevelTexture));
		writeData(&sector->ceilTex, sizeof(LevelTexture));
		writeF32(sector->floorHeight);
		writeF32(sector->ceilHeight);
		writeF32(sector->secHeight);
		writeU32(sector->ambient);
		writeS32(sector->layer);
		writeData(sector->flags, sizeof(u32) * 3);
		writeU32((u32)sector->vtx.size());
		writeU32((u32)sector->walls.size());
		writeU32((u32)sector->obj.size());
		writeData(sector->vtx.data(), u32(sizeof(Vec2f) * sector->vtx.size()));
		writeData(sector->walls.data(), u32(sizeof(EditorWall) * sector->walls.size()));
		writeData(sector->obj.data(), u32(sizeof(EditorObject) * sector->obj.size()));
	}

	void writeINFWallTrackToSnapshot(const INFWallTrack* wt)
	{
		writeString(wt->name);
		writeS32(wt->oldWall);
		writeS32(wt->newWall);
	}

	void writeSectorAttribSnapshot(const EditorSector* sector)
	{
		writeU32(sector->groupId);
		writeU32(sector->groupIndex);
		writeString(sector->name);
		writeData(&sector->floorTex, sizeof(LevelTexture));
		writeData(&sector->ceilTex, sizeof(LevelTexture));
		writeF32(sector->floorHeight);
		writeF32(sector->ceilHeight);
		writeF32(sector->secHeight);
		writeU32(sector->ambient);
		writeS32(sector->layer);
		writeData(sector->flags, sizeof(u32) * 3);
	}

	void writeLevelNoteToSnapshot(const LevelNote* note)
	{
		writeS32(note->id);
		writeU32(note->groupId);
		writeU32(note->groupIndex);
		writeU32(note->flags);
		writeU32(note->iconColor);
		writeU32(note->textColor);

		writeData(&note->pos, sizeof(Vec3f));
		writeData(&note->fade, sizeof(Vec2f));
		writeString(note->note);
	}

	void writeGuidelineToSnapshot(const Guideline* guideline)
	{
		const s32 vtxCount = (s32)guideline->vtx.size();
		const s32 knotCount = (s32)guideline->knots.size();
		const s32 edgeCount = (s32)guideline->edge.size();
		const s32 offsetCount = (s32)guideline->offsets.size();
		
		writeS32(guideline->id);
		writeS32(vtxCount);
		writeS32(knotCount);
		writeS32(edgeCount);
		writeS32(offsetCount);
		
		writeU32(guideline->flags);
		writeF32(guideline->maxOffset);
		writeF32(guideline->maxHeight);
		writeF32(guideline->minHeight);
		writeF32(guideline->maxSnapRange);
		writeF32(guideline->subDivLen);

		writeData(guideline->bounds.m, sizeof(Vec4f));
		writeData(guideline->vtx.data(), sizeof(Vec2f) * vtxCount);
		writeData(guideline->knots.data(), sizeof(Vec4f) * knotCount);
		writeData(guideline->edge.data(), sizeof(GuidelineEdge) * edgeCount);
		writeData(guideline->offsets.data(), sizeof(f32) * offsetCount);
	}

	void writeInfElevatorToSnapshot(const Editor_InfElevator* infElevator)
	{
		const s32 slaveCount = (s32)infElevator->slaves.size();
		const s32 stopCount = (s32)infElevator->stops.size();

		writeU32(infElevator->classId);
		writeU32(infElevator->type);
		writeU32(infElevator->overrideSet);
		writeS32(infElevator->start);
		writeF32(infElevator->speed);
		writeF32(infElevator->angle);
		writeU32(infElevator->flags);
		writeData(infElevator->key, sizeof(KeyItem) * 2);
		writeData(&infElevator->center, sizeof(Vec2f));
		writeString(infElevator->sounds[0]);
		writeString(infElevator->sounds[1]);
		writeString(infElevator->sounds[2]);
		writeU8(infElevator->master);
		writeS32(infElevator->eventMask);
		writeS32(infElevator->entityMask);
		writeS32(slaveCount);
		writeS32(stopCount);

		// Each slave & stop is of variable length
		for (s32 i = 0; i < slaveCount; i++)
		{
			writeString(infElevator->slaves[i].name);
			writeF32(infElevator->slaves[i].angleOffset);
		}

		for (s32 i = 0; i < stopCount; i++)
		{
			const s32 adjoinCount = (s32)infElevator->stops[i].adjoinCmd.size();
			const s32 textureCount = (s32)infElevator->stops[i].textureCmd.size();
			const s32 messageCount = (s32)infElevator->stops[i].msg.size();
			const s32 scriptCount = (s32)infElevator->stops[i].scriptCall.size();
			writeU32(infElevator->stops[i].overrideSet);
			writeU8(infElevator->stops[i].relative);
			writeF32(infElevator->stops[i].value);
			writeString(infElevator->stops[i].fromSectorFloor);
			writeU32(infElevator->stops[i].delayType);
			writeF32(infElevator->stops[i].delay);
			writeString(infElevator->stops[i].page);
			writeS32(adjoinCount);
			writeS32(textureCount);
			writeS32(messageCount);
			writeS32(scriptCount);
			for (s32 j = 0; j < adjoinCount; j++)
			{
				writeString(infElevator->stops[i].adjoinCmd[j].sector0);
				writeString(infElevator->stops[i].adjoinCmd[j].sector1);
				writeS32(infElevator->stops[i].adjoinCmd[j].wallIndex0);
				writeS32(infElevator->stops[i].adjoinCmd[j].wallIndex1);
			}
			for (s32 j = 0; j < textureCount; j++)
			{
				writeString(infElevator->stops[i].textureCmd[j].donorSector);
				writeU8(infElevator->stops[i].textureCmd[j].fromCeiling);
			}
			for (s32 j = 0; j < messageCount; j++)
			{
				writeString(infElevator->stops[i].msg[j].targetSector);
				writeS32(infElevator->stops[i].msg[j].targetWall);
				writeU32(infElevator->stops[i].msg[j].type);
				writeU32(infElevator->stops[i].msg[j].eventFlags);
				writeU32(infElevator->stops[i].msg[j].arg[0]);
				writeU32(infElevator->stops[i].msg[j].arg[1]);
			}
			for (s32 j = 0; j < scriptCount; j++)
			{
				writeString(infElevator->stops[i].scriptCall[j].funcName);
				writeString(infElevator->stops[i].scriptCall[j].arg[0].value);
				writeString(infElevator->stops[i].scriptCall[j].arg[1].value);
				writeString(infElevator->stops[i].scriptCall[j].arg[2].value);
				writeString(infElevator->stops[i].scriptCall[j].arg[3].value);
			}
		}
	}

	void writeInfTriggerToSnapshot(const Editor_InfTrigger* infTrigger)
	{
		writeU32(infTrigger->classId);
		writeU32(infTrigger->type);
		writeU32(infTrigger->overrideSet);
		const s32 clientCount = (s32)infTrigger->clients.size();
		writeS32(clientCount);
		for (s32 i = 0; i < clientCount; i++)
		{
			writeString(infTrigger->clients[i].targetSector);
			writeS32(infTrigger->clients[i].targetWall);
			writeS32(infTrigger->clients[i].eventMask);
		}
		writeU32(infTrigger->cmd);
		writeData(&infTrigger->arg, sizeof(u32) * 2);
		writeString(infTrigger->sound);
		writeU8(infTrigger->master);
		writeU32(infTrigger->textId);
		writeS32(infTrigger->eventMask);
		writeS32(infTrigger->entityMask);
		writeU32(infTrigger->event);
	}

	void writeInfTeleporterToSnapshot(const Editor_InfTeleporter* infTeleporter)
	{
		writeU32(infTeleporter->classId);
		writeU32(infTeleporter->type);
		writeString(infTeleporter->target);
		writeData(&infTeleporter->dstPos, sizeof(Vec3f));
		writeF32(infTeleporter->dstAngle);
	}

	void writeInfItemToSnapshot(const Editor_InfItem* infItem)
	{
		const s32 classCount = (s32)infItem->classData.size();

		writeString(infItem->name);
		writeS32(infItem->wallNum);
		writeS32(classCount);
		for (s32 i = 0; i < classCount; i++)
		{
			// classData are ptrs to heap data. Store the index for snapshotting, ref new ptr addresses when unpacking.
			Editor_InfClass* classPtr = infItem->classData[i];
			s32 classIndex = getClassIndex(classPtr->classId, classPtr);
			assert(classIndex >= 0);
			writeU32(infItem->classData[i]->classId);
			writeS32(classIndex);
		}
	}

	void readEntityFromSnapshot(Entity* entity)
	{
		entity->id = readS32();
		entity->categories = readS32();
		readString(entity->name);
		readString(entity->assetName);
		entity->type = (EntityType)readS32();

		readEntityLogic(entity->logic);
		readEntityVar(entity->var);
		readData(entity->bounds, sizeof(Vec3f) * 2);
		readData(&entity->offset, sizeof(Vec3f));
		readData(&entity->offsetAdj, sizeof(Vec3f));
	}

	void readSectorFromSnapshot(EditorSector* sector)
	{
		sector->id = readU32();
		sector->groupId = readU32();
		sector->groupIndex = readU32();
		readString(sector->name);
		readData(&sector->floorTex, sizeof(LevelTexture));
		readData(&sector->ceilTex, sizeof(LevelTexture));
		sector->floorHeight = readF32();
		sector->ceilHeight = readF32();
		sector->secHeight = readF32();
		sector->ambient = readU32();
		sector->layer = readS32();
		readData(sector->flags, sizeof(u32) * 3);

		const u32 vtxCount = readU32();
		const u32 wallCount = readU32();
		const u32 objCount = readU32();
		sector->vtx.resize(vtxCount);
		sector->walls.resize(wallCount);
		sector->obj.resize(objCount);

		readData(sector->vtx.data(), u32(sizeof(Vec2f) * sector->vtx.size()));
		readData(sector->walls.data(), u32(sizeof(EditorWall) * sector->walls.size()));
		readData(sector->obj.data(), u32(sizeof(EditorObject) * sector->obj.size()));
	}

	void readINFWallTrackFromSnapshot(INFWallTrack* wallTrack)
	{
		readString(wallTrack->name);
		wallTrack->oldWall = readS32();
		wallTrack->newWall = readS32();
	}

	void readFromSectorAttribSnapshot(EditorSector* sector)
	{
		sector->groupId = readU32();
		sector->groupIndex = readU32();
		readString(sector->name);
		readData(&sector->floorTex, sizeof(LevelTexture));
		readData(&sector->ceilTex, sizeof(LevelTexture));
		sector->floorHeight = readF32();
		sector->ceilHeight = readF32();
		sector->secHeight = readF32();
		sector->ambient = readU32();
		sector->layer = readS32();
		readData(sector->flags, sizeof(u32) * 3);
	}

	void readLevelNoteFromSnapshot(LevelNote* note)
	{
		note->id = readS32();
		note->groupId = readU32();
		note->groupIndex = readU32();
		note->flags = readU32();
		note->iconColor = readU32();
		note->textColor = readU32();

		readData(&note->pos, sizeof(Vec3f));
		readData(&note->fade, sizeof(Vec2f));
		readString(note->note);
	}

	void readGuidelineFromSnapshot(Guideline* guideline)
	{
		guideline->id = readS32();
		const s32 vtxCount = readS32();
		const s32 knotCount = readS32();
		const s32 edgeCount = readS32();
		const s32 offsetCount = readS32();
		guideline->vtx.resize(vtxCount);
		guideline->knots.resize(knotCount);
		guideline->edge.resize(edgeCount);
		guideline->offsets.resize(offsetCount);

		guideline->flags = readU32();
		guideline->maxOffset = readF32();
		guideline->maxHeight = readF32();
		guideline->minHeight = readF32();
		guideline->maxSnapRange = readF32();
		guideline->subDivLen = readF32();

		readData(guideline->bounds.m, sizeof(Vec4f));
		readData(guideline->vtx.data(), sizeof(Vec2f) * vtxCount);
		readData(guideline->knots.data(), sizeof(Vec4f) * knotCount);
		readData(guideline->edge.data(), sizeof(GuidelineEdge) * edgeCount);
		readData(guideline->offsets.data(), sizeof(f32) * offsetCount);

		guideline_computeSubdivision(guideline);
	}

	void readInfItemFromSnapshot(Editor_InfItem* infItem)
	{
		readString(infItem->name);
		infItem->wallNum = readS32();
		s32 classCount = readS32();
		infItem->classData.resize(classCount);
	}

	void readInfElevatorFromSnapshot(Editor_InfElevator* infElevator)
	{
		infElevator->classId = (Editor_InfItemClass)readU32();
		infElevator->type = (Editor_InfElevType)readU32();
		infElevator->overrideSet = (Editor_InfElevatorOverride)readU32();
		infElevator->start = readS32();
		infElevator->speed = readF32();
		infElevator->angle = readF32();
		infElevator->flags = readU32();
		readData(infElevator->key, sizeof(KeyItem) * 2);
		readData(&infElevator->center, sizeof(Vec2f));
		readString(infElevator->sounds[0]);
		readString(infElevator->sounds[1]);
		readString(infElevator->sounds[2]);
		infElevator->master = (bool)readU8();
		infElevator->eventMask = readS32();
		infElevator->entityMask = readS32();
		const s32 slaveCount = readS32();
		const s32 stopCount = readS32();
		infElevator->slaves.resize(slaveCount);
		infElevator->stops.resize(stopCount);
		
		// Each slave & stop is of variable length
		for (s32 i = 0; i < slaveCount; i++)
		{
			readString(infElevator->slaves[i].name);
			infElevator->slaves[i].angleOffset = readF32();
		}

		for (s32 i = 0; i < stopCount; i++)
		{
			infElevator->stops[i].overrideSet = readU32();
			infElevator->stops[i].relative = (bool)readU8();
			infElevator->stops[i].value = readF32();
			readString(infElevator->stops[i].fromSectorFloor);
			infElevator->stops[i].delayType = (Editor_InfStopDelayType)readU32();
			infElevator->stops[i].delay = readF32();
			readString(infElevator->stops[i].page);
			const s32 adjoinCount = readS32();
			const s32 textureCount = readS32();
			const s32 messageCount = readS32();
			const s32 scriptCount = readS32();
			infElevator->stops[i].adjoinCmd.resize(adjoinCount);
			infElevator->stops[i].textureCmd.resize(textureCount);
			infElevator->stops[i].msg.resize(messageCount);
			infElevator->stops[i].scriptCall.resize(scriptCount);
			for (s32 j = 0; j < adjoinCount; j++)
			{
				readString(infElevator->stops[i].adjoinCmd[j].sector0);
				readString(infElevator->stops[i].adjoinCmd[j].sector1);
				infElevator->stops[i].adjoinCmd[j].wallIndex0 = readS32();
				infElevator->stops[i].adjoinCmd[j].wallIndex1 = readS32();
			}
			for (s32 j = 0; j < textureCount; j++)
			{
				readString(infElevator->stops[i].textureCmd[j].donorSector);
				infElevator->stops[i].textureCmd[j].fromCeiling = (bool)readU8();
			}
			for (s32 j = 0; j < messageCount; j++)
			{
				readString(infElevator->stops[i].msg[j].targetSector);
				infElevator->stops[i].msg[j].targetWall = readS32();
				infElevator->stops[i].msg[j].type = (Editor_InfMessageType)readU32();
				infElevator->stops[i].msg[j].eventFlags = readU32();
				infElevator->stops[i].msg[j].arg[0] = readU32();
				infElevator->stops[i].msg[j].arg[1] = readU32();
			}
			for (s32 j = 0; j < scriptCount; j++)
			{
				readString(infElevator->stops[i].scriptCall[j].funcName);
				readString(infElevator->stops[i].scriptCall[j].arg[0].value);
				readString(infElevator->stops[i].scriptCall[j].arg[1].value);
				readString(infElevator->stops[i].scriptCall[j].arg[2].value);
				readString(infElevator->stops[i].scriptCall[j].arg[3].value);
			}
		}
	}

	void readInfTriggerFromSnapshot(Editor_InfTrigger* infTrigger)
	{
		infTrigger->classId = (Editor_InfItemClass)readU32();
		infTrigger->type = (TriggerType)readU32();
		infTrigger->overrideSet = readU32();
		const s32 clientCount = readS32();
		infTrigger->clients.resize(clientCount);
		for (s32 i = 0; i < clientCount; i++)
		{
			readString(infTrigger->clients[i].targetSector);
			infTrigger->clients[i].targetWall = readS32();
			infTrigger->clients[i].eventMask = readS32();
		}
		infTrigger->cmd = (Editor_InfMessageType)readU32();
		readData(&infTrigger->arg, sizeof(u32) * 2);
		readString(infTrigger->sound);
		infTrigger->master = (bool)readU8();
		infTrigger->textId = readU32();
		infTrigger->eventMask = readS32();
		infTrigger->entityMask = readS32();
		infTrigger->event = readU32();
	}

	void readInfTeleporterFromSnapshot(Editor_InfTeleporter* infTeleporter)
	{
		infTeleporter->classId = (Editor_InfItemClass)readU32();
		infTeleporter->type = (TeleportType)readU32();
		readString(infTeleporter->target);
		readData(&infTeleporter->dstPos, sizeof(Vec3f));
		infTeleporter->dstAngle = readF32();
	}
}