#include "levelEditorHistory.h"
#include "sharedState.h"
#include "error.h"
#include <TFE_System/system.h>
#include <TFE_Editor/LevelEditor/levelEditor.h>
#include <TFE_Editor/LevelEditor/levelEditorData.h>
#include <TFE_Archive/zstdCompression.h>
#include <TFE_System/system.h>
#include <assert.h>
#include <algorithm>
#include <cstring>
#include <cstddef>

using namespace TFE_Editor;

// TODO: Capture edit mode and selection data when creating a delta/snapshot.

namespace LevelEditor
{
	#define CMD_BEGIN(cmdId, cmdName) if (!history_createCommand(cmdId, cmdName)) { return; }
	#define CMD_END()

	enum LevCommand
	{
		LCmd_Sector_Snapshot = CMD_START,
		LCmd_Sector_Wall_Snapshot,
		LCmd_Sector_Attrib_Snapshot,
		LCmd_ObjList_Snapshot,
		LCmd_Set_Textures,
		LCmd_Guideline_Snapshot,
		LCmd_Guideline_Snapshot_Single,
		LCmd_Count
	};

	// Used to merge certain commands.
	static TFE_Editor::SnapshotBuffer s_workBuffer[2];

	// Helper Functions.
	u32 compressBuffer();
				
	// Command Functions
	void cmd_applySectorSnapshot();
	void cmd_applySectorWallSnapshot();
	void cmd_applySectorAttribSnapshot();
	void cmd_applyObjListSnapshot();
	void cmd_applySetTextures();
	void cmd_applyGuidelineSnapshot();
	void cmd_applyGuidelineSingleSnapshot();

	///////////////////////////////////
	// API
	///////////////////////////////////
	void levHistory_init()
	{
		history_init(level_unpackSnapshot, level_createSnapshot);

		history_registerCommand(LCmd_Sector_Snapshot, cmd_applySectorSnapshot);
		history_registerCommand(LCmd_Sector_Wall_Snapshot, cmd_applySectorWallSnapshot);
		history_registerCommand(LCmd_Sector_Attrib_Snapshot, cmd_applySectorAttribSnapshot);
		history_registerCommand(LCmd_ObjList_Snapshot, cmd_applyObjListSnapshot);
		history_registerCommand(LCmd_Set_Textures, cmd_applySetTextures);
		history_registerCommand(LCmd_Guideline_Snapshot, cmd_applyGuidelineSnapshot);
		history_registerCommand(LCmd_Guideline_Snapshot_Single, cmd_applyGuidelineSingleSnapshot);

		history_registerName(LName_MoveVertex, "Move Vertice(s)");
		history_registerName(LName_SetVertex, "Set Vertex Position");
		history_registerName(LName_MoveWall, "Move Wall(s)");
		history_registerName(LName_MoveFlat, "Move Flat(s)");
		history_registerName(LName_MoveSector, "Move Sector(s)");
		history_registerName(LName_MoveObject, "Move Objects(s)");
		history_registerName(LName_MoveObjectToFloor, "Move Object(s) to Floor");
		history_registerName(LName_MoveObjectToCeil, "Move Object(s) to Ceiling");
		history_registerName(LName_InsertVertex, "Insert Vertex");
		history_registerName(LName_DeleteVertex, "Delete Vertex");
		history_registerName(LName_DeleteWall, "Delete Wall");
		history_registerName(LName_DeleteSector, "Delete Sector");
		history_registerName(LName_CreateSectorFromRect, "Create Sector (Rect)");
		history_registerName(LName_CreateSectorFromShape, "Create Sector (Shape)");
		history_registerName(LName_ExtrudeSectorFromWall, "Extrude Sectors from Wall");
		history_registerName(LName_MoveTexture, "Move Texture");
		history_registerName(LName_ClearTextureOffset, "Reset Texture Offsets");
		history_registerName(LName_SetTexture, "Set Texture");
		history_registerName(LName_CopyTexture, "Copy Texture");
		history_registerName(LName_ClearTexture, "Clear Texture");
		history_registerName(LName_Autoalign, "Autoalign Textures");
		history_registerName(LName_DeleteObject, "Delete Object(s)");
		history_registerName(LName_AddObject, "Added Object(s)");
		history_registerName(LName_ReplaceObject, "Replace Object");
		history_registerName(LName_RotateSector, "Rotate Sector(s)");
		history_registerName(LName_RotateWall, "Rotate Wall(s)");
		history_registerName(LName_RotateVertex, "Rotate Vertices(s)");
		history_registerName(LName_RotateEntity, "Rotate Object(s)");
		history_registerName(LName_ChangeWallAttrib, "Change Wall Attributes");
		history_registerName(LName_ChangeSectorAttrib, "Change Sector Attributes");
		history_registerName(LName_SetSectorGroup, "Add Sector(s) to Group");
		history_registerName(LName_CleanSectors, "Clean Sectors");
		history_registerName(LName_JoinSectors, "Join Sectors");
		history_registerName(LName_Connect, "Connect Sectors");
		history_registerName(LName_Disconnect, "Disconnect Sectors");
		history_registerName(LName_Guideline_Create, "Create Guidelines");
		history_registerName(LName_Guideline_Delete, "Delete Guidelines");
		history_registerName(LName_Guideline_Edit, "Edit Guidelines");
	}

	void levHistory_destroy()
	{
		history_destroy();
	}

	void levHistory_clear()
	{
		history_clear();
	}

	void levHistory_createSnapshot(const char* name)
	{
		history_createSnapshot(name);
	}
		
	void levHistory_undo()
	{
		s32 oldLayerRange[2] = { s_level.layerRange[0], s_level.layerRange[1] };
		history_step(-1);
		edit_clearSelections(false);
		updateLevelBounds(nullptr);
		updateCurrentLayer(oldLayerRange);

	}

	void levHistory_redo()
	{
		s32 oldLayerRange[2] = { s_level.layerRange[0], s_level.layerRange[1] };
		history_step(1);
		edit_clearSelections(false);
		updateLevelBounds(nullptr);
		updateCurrentLayer(oldLayerRange);
	}
		
	////////////////////////////////
	// Editor API
	////////////////////////////////
	void cmd_sectorSnapshot(u32 name, std::vector<s32>& sectorIds)
	{
		s_workBuffer[0].clear();
		s_workBuffer[1].clear();
		level_createSectorSnapshot(&s_workBuffer[0], sectorIds);
		if (s_workBuffer[0].empty()) { return; }

		const u32 uncompressedSize = (u32)s_workBuffer[0].size();
		const u32 compressedSize = compressBuffer();
		if (!compressedSize) { return; }

		CMD_BEGIN(LCmd_Sector_Snapshot, name);
		{
			hBuffer_addU32(uncompressedSize);
			hBuffer_addU32(compressedSize);
			hBuffer_addArrayU8(compressedSize, s_workBuffer[1].data());
		}
		CMD_END();
	}

	void cmd_objectListSnapshot(u32 name, s32 sectorId)
	{
		s_workBuffer[0].clear();
		s_workBuffer[1].clear();
		level_createEntiyListSnapshot(&s_workBuffer[0], sectorId);
		if (s_workBuffer[0].empty()) { return; }

		const u32 uncompressedSize = (u32)s_workBuffer[0].size();
		const u32 compressedSize = compressBuffer();
		if (!compressedSize) { return; }

		CMD_BEGIN(LCmd_ObjList_Snapshot, name);
		{
			hBuffer_addU32(uncompressedSize);
			hBuffer_addU32(compressedSize);
			hBuffer_addArrayU8(compressedSize, s_workBuffer[1].data());
		}
		CMD_END();
	}

	void cmd_sectorWallSnapshot(u32 name, std::vector<IndexPair>& sectorWallIds, bool idsChanged)
	{
		if (sectorWallIds.empty()) { return; }
		const AppendTexList& texList = edit_getTextureAppendList();

		u16 prevCmd, prevName;
		history_getPrevCmdAndName(prevCmd, prevName);
		// Only merge together attributes, not textures unless the level texture list is unchanged.
		if (prevCmd == LCmd_Sector_Wall_Snapshot && texList.list.empty() && prevName == name && !idsChanged)
		{
			history_removeLast();
		}

		s_workBuffer[0].clear();
		s_workBuffer[1].clear();
		level_createSectorWallSnapshot(&s_workBuffer[0], sectorWallIds);
		if (s_workBuffer[0].empty()) { return; }

		const u32 uncompressedSize = (u32)s_workBuffer[0].size();
		const u32 compressedSize = compressBuffer();
		if (!compressedSize) { return; }

		CMD_BEGIN(LCmd_Sector_Wall_Snapshot, name);
		{
			hBuffer_addU32(uncompressedSize);
			hBuffer_addU32(compressedSize);
			hBuffer_addArrayU8(compressedSize, s_workBuffer[1].data());
		}
		CMD_END();
	}

	void cmd_sectorAttributeSnapshot(u32 name, std::vector<IndexPair>& sectorIds, bool idsChanged)
	{
		if (sectorIds.empty()) { return; }
		const AppendTexList& texList = edit_getTextureAppendList();

		u16 prevCmd, prevName;
		history_getPrevCmdAndName(prevCmd, prevName);
		// Cannot combine commands if new textures are added to the level.
		if (prevCmd == LCmd_Sector_Attrib_Snapshot && texList.list.empty() && prevName == name && !idsChanged)
		{
			history_removeLast();
		}

		s_workBuffer[0].clear();
		s_workBuffer[1].clear();
		level_createSectorAttribSnapshot(&s_workBuffer[0], sectorIds);
		if (s_workBuffer[0].empty()) { return; }

		const u32 uncompressedSize = (u32)s_workBuffer[0].size();
		const u32 compressedSize = compressBuffer();
		if (!compressedSize) { return; }

		CMD_BEGIN(LCmd_Sector_Attrib_Snapshot, name);
		{
			hBuffer_addU32(uncompressedSize);
			hBuffer_addU32(compressedSize);
			hBuffer_addArrayU8(compressedSize, s_workBuffer[1].data());
		}
		CMD_END();
	}

	void cmd_setTextures(u32 name, s32 count, FeatureId* features)
	{
		if (count < 1 || !features) { return; }

		s_workBuffer[0].clear();
		s_workBuffer[1].clear();
		level_createFeatureTextureSnapshot(&s_workBuffer[0], count, features);
		if (s_workBuffer[0].empty()) { return; }

		const u32 uncompressedSize = (u32)s_workBuffer[0].size();
		const u32 compressedSize = compressBuffer();
		if (!compressedSize) { return; }

		CMD_BEGIN(LCmd_Set_Textures, name);
		{
			hBuffer_addU32(uncompressedSize);
			hBuffer_addU32(compressedSize);
			hBuffer_addArrayU8(compressedSize, s_workBuffer[1].data());
		}
		CMD_END();
	}

	void cmd_guidelineSnapshot(u32 name)
	{
		s_workBuffer[0].clear();
		s_workBuffer[1].clear();
		level_createGuidelineSnapshot(&s_workBuffer[0]);
		if (s_workBuffer[0].empty()) { return; }

		const u32 uncompressedSize = (u32)s_workBuffer[0].size();
		const u32 compressedSize = compressBuffer();
		if (!compressedSize) { return; }

		CMD_BEGIN(LCmd_Guideline_Snapshot, name);
		{
			hBuffer_addU32(uncompressedSize);
			hBuffer_addU32(compressedSize);
			hBuffer_addArrayU8(compressedSize, s_workBuffer[1].data());
		}
		CMD_END();
	}

	void cmd_guidelineSingleSnapshot(u32 name, s32 index, bool idChanged)
	{
		if (index < 0 || index >= (s32)s_level.guidelines.size()) { return; }

		// Merge together single snapshot commands to make editing attributes easier.
		u16 prevCmd, prevName;
		history_getPrevCmdAndName(prevCmd, prevName);
		if (prevCmd == LCmd_Guideline_Snapshot_Single && prevName == name && !idChanged)
		{
			history_removeLast();
		}

		s_workBuffer[0].clear();
		s_workBuffer[1].clear();
		level_createSingleGuidelineSnapshot(&s_workBuffer[0], index);
		if (s_workBuffer[0].empty()) { return; }

		const u32 uncompressedSize = (u32)s_workBuffer[0].size();
		const u32 compressedSize = compressBuffer();
		if (!compressedSize) { return; }

		CMD_BEGIN(LCmd_Guideline_Snapshot_Single, name);
		{
			hBuffer_addU32(uncompressedSize);
			hBuffer_addU32(compressedSize);
			hBuffer_addArrayU8(compressedSize, s_workBuffer[1].data());
		}
		CMD_END();
	}
		
	////////////////////////////////
	// History Commands
	////////////////////////////////
	void cmd_applySectorSnapshot()
	{
		const u32 uncompressedSize = hBuffer_getU32();
		const u32 compressedSize = hBuffer_getU32();
		const u8* compressedData = hBuffer_getArrayU8(compressedSize);

		s_workBuffer[0].resize(uncompressedSize);
		if (zstd_decompress(s_workBuffer[0].data(), uncompressedSize, compressedData, compressedSize))
		{
			level_unpackSectorSnapshot(uncompressedSize, s_workBuffer[0].data());
		}
	}

	void cmd_applyObjListSnapshot()
	{
		const u32 uncompressedSize = hBuffer_getU32();
		const u32 compressedSize = hBuffer_getU32();
		const u8* compressedData = hBuffer_getArrayU8(compressedSize);

		s_workBuffer[0].resize(uncompressedSize);
		if (zstd_decompress(s_workBuffer[0].data(), uncompressedSize, compressedData, compressedSize))
		{
			level_unpackEntiyListSnapshot(uncompressedSize, s_workBuffer[0].data());
		}
	}

	void cmd_applySectorWallSnapshot()
	{
		const u32 uncompressedSize = hBuffer_getU32();
		const u32 compressedSize = hBuffer_getU32();
		const u8* compressedData = hBuffer_getArrayU8(compressedSize);

		s_workBuffer[0].resize(uncompressedSize);
		if (zstd_decompress(s_workBuffer[0].data(), uncompressedSize, compressedData, compressedSize))
		{
			level_unpackSectorWallSnapshot(uncompressedSize, s_workBuffer[0].data());
		}
	}

	void cmd_applySectorAttribSnapshot()
	{
		const u32 uncompressedSize = hBuffer_getU32();
		const u32 compressedSize = hBuffer_getU32();
		const u8* compressedData = hBuffer_getArrayU8(compressedSize);

		s_workBuffer[0].resize(uncompressedSize);
		if (zstd_decompress(s_workBuffer[0].data(), uncompressedSize, compressedData, compressedSize))
		{
			level_unpackSectorAttribSnapshot(uncompressedSize, s_workBuffer[0].data());
		}
	}

	void cmd_applySetTextures()
	{
		const u32 uncompressedSize = hBuffer_getU32();
		const u32 compressedSize = hBuffer_getU32();
		const u8* compressedData = hBuffer_getArrayU8(compressedSize);

		s_workBuffer[0].resize(uncompressedSize);
		if (zstd_decompress(s_workBuffer[0].data(), uncompressedSize, compressedData, compressedSize))
		{
			level_unpackFeatureTextureSnapshot(uncompressedSize, s_workBuffer[0].data());
		}
	}

	void cmd_applyGuidelineSnapshot()
	{
		const u32 uncompressedSize = hBuffer_getU32();
		const u32 compressedSize = hBuffer_getU32();
		const u8* compressedData = hBuffer_getArrayU8(compressedSize);

		s_workBuffer[0].resize(uncompressedSize);
		if (zstd_decompress(s_workBuffer[0].data(), uncompressedSize, compressedData, compressedSize))
		{
			level_unpackGuidelineSnapshot(uncompressedSize, s_workBuffer[0].data());
		}
	}

	void cmd_applyGuidelineSingleSnapshot()
	{
		const u32 uncompressedSize = hBuffer_getU32();
		const u32 compressedSize = hBuffer_getU32();
		const u8* compressedData = hBuffer_getArrayU8(compressedSize);

		s_workBuffer[0].resize(uncompressedSize);
		if (zstd_decompress(s_workBuffer[0].data(), uncompressedSize, compressedData, compressedSize))
		{
			level_unpackSingleGuidelineSnapshot(uncompressedSize, s_workBuffer[0].data());
		}
	}

	//////////////////////////////////////////////////
	// Internal
	//////////////////////////////////////////////////
	u32 compressBuffer()
	{
		u32 compressedSize = 0;
		if (zstd_compress(s_workBuffer[1], s_workBuffer[0].data(), (u32)s_workBuffer[0].size(), 4))
		{
			compressedSize = (u32)s_workBuffer[1].size();
		}
		if (!compressedSize)
		{
			LE_ERROR("Snapshot compression failed.");
			return 0;
		}
		return compressedSize;
	}
}
