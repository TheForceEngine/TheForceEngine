#pragma once
//////////////////////////////////////////////////////////////////////
// The Force Engine Editor
// A system built to view and edit Dark Forces data files.
// The viewing aspect needs to be put in place at the beginning
// in order to properly test elements in isolation without having
// to "play" the game as intended.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include "entity.h"
#include "groups.h"
#include "note.h"
#include "levelEditorInf.h"
#include <TFE_Editor/EditorAsset/editorAsset.h>
#include <TFE_Editor/EditorAsset/editorTexture.h>
#include <TFE_Editor/editorProject.h>
#include <TFE_Polygon/polygon.h>
#include <TFE_Editor/history.h>

namespace LevelEditor
{
	struct UniqueTexture
	{
		s32 originalId;
		s32 newId;
		std::string name;
	};
	struct UniqueEntity
	{
		s32 originalId;
		s32 newId;
		Entity entity;
	};

	s32 addUniqueTexture(s32 id, std::vector<UniqueTexture>& uniqueTex);
	s32 addUniqueEntity(s32 id, std::vector<UniqueEntity>& uniqueEntity);

	void writeEntityToSnapshot(const Entity* entity);
	void writeSectorToSnapshot(const EditorSector* sector);
	void writeSectorAttribSnapshot(const EditorSector* sector);
	void writeLevelNoteToSnapshot(const LevelNote* note);
	void writeGuidelineToSnapshot(const Guideline* guideline);
	void writeInfElevatorToSnapshot(const Editor_InfElevator* infElevator);
	void writeInfTriggerToSnapshot(const Editor_InfTrigger* infTrigger);
	void writeInfTeleporterToSnapshot(const Editor_InfTeleporter* infTeleporter);
	void writeInfItemToSnapshot(const Editor_InfItem* infItem);

	void readEntityFromSnapshot(Entity* entity);
	void readSectorFromSnapshot(EditorSector* sector);
	void readFromSectorAttribSnapshot(EditorSector* sector);
	void readLevelNoteFromSnapshot(LevelNote* note);
	void readGuidelineFromSnapshot(Guideline* guideline);
	void readInfElevatorFromSnapshot(Editor_InfElevator* infElevator);
	void readInfTriggerFromSnapshot(Editor_InfTrigger* infTrigger);
	void readInfTeleporterFromSnapshot(Editor_InfTeleporter* infTeleporter);
	void readInfItemFromSnapshot(Editor_InfItem* infItem);
}
