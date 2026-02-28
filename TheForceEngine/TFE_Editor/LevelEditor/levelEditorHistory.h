#pragma once
#include <TFE_System/types.h>
#include <TFE_Editor/history.h>
#include <TFE_Editor/LevelEditor/selection.h>
#include <TFE_Editor/LevelEditor/levelEditorData.h>

namespace LevelEditor
{
	enum LevCommandName
	{
		LName_MoveVertex = 0,
		LName_SetVertex,
		LName_MoveWall,
		LName_MoveFlat,
		LName_MoveSector,
		LName_MoveObject,
		LName_MoveObjectToFloor,
		LName_MoveObjectToCeil,
		LName_InsertVertex,
		LName_DeleteVertex,
		LName_DeleteWall,
		LName_DeleteSector,
		LName_CreateSectorFromRect,
		LName_CreateSectorFromShape,
		LName_ExtrudeSectorFromWall,
		LName_MoveTexture,
		LName_ClearTextureOffset,
		LName_SetTexture,
		LName_CopyTexture,
		LName_ClearTexture,
		LName_Autoalign,
		LName_DeleteObject,
		LName_AddObject,
		LName_ReplaceObject,
		LName_RotateSector,
		LName_RotateWall,
		LName_RotateVertex,
		LName_RotateEntity,
		LName_ChangeWallAttrib,
		LName_ChangeSectorAttrib,
		LName_SetSectorGroup,
		LName_CleanSectors,
		LName_JoinSectors,
		LName_Connect,
		LName_Disconnect,
		LName_Guideline_Create,
		LName_Guideline_Delete,
		LName_Guideline_Edit,
		LName_LevelNote_Create,
		LName_LevelNote_Delete,
		LName_LevelNote_Move,
		LName_LevelNote_Change,
		LName_Count
	};

	void levHistory_init();
	void levHistory_destroy();

	void levHistory_clear();
	void levHistory_createSnapshot(const char* name);

	void levHistory_undo();
	void levHistory_redo();

	// Commands
	void cmd_sectorSnapshot(u32 name, std::vector<s32>& sectorIds);
	void cmd_sectorWallSnapshot(u32 name, std::vector<IndexPair>& sectorWallIds, bool idsChanged);
	void cmd_sectorAttributeSnapshot(u32 name, std::vector<IndexPair>& sectorIds, bool idsChanged);
	void cmd_objectListSnapshot(u32 name, s32 sectorId);
	void cmd_setTextures(u32 name, s32 count, FeatureId* features);
	void cmd_guidelineSnapshot(u32 name);
	void cmd_levelNoteSnapshot(u32 name, bool idChanged);
	void cmd_guidelineSingleSnapshot(u32 name, s32 index, bool idChanged);
}
