#pragma once
//////////////////////////////////////////////////////////////////////
// The Force Engine Editor
// A system built to view and edit Dark Forces data files.
// The viewing aspect needs to be put in place at the beginning
// in order to properly test elements in isolation without having
// to "play" the game as intended.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include "levelEditorData.h"
#include "selection.h"
#include "sharedState.h"

namespace LevelEditor
{
	void handleVertexInsert(Vec2f worldPos, RayHitInfo* info = nullptr);
	void findHoveredVertex3d(EditorSector* hoveredSector, RayHitInfo* info);
	void findHoveredVertex2d(EditorSector* hoveredSector, Vec2f worldPos);
	void vertexComputeDragSelect();
	void edit_deleteVertex(s32 sectorId, s32 vertexIndex, u32 nameId);
	void edit_deleteVertices();

	// Transforms
	void edit_setVertexPos(FeatureId id, Vec2f pos);
	void edit_moveSelectedVertices(Vec2f delta);
	void edit_moveVertices(Vec2f worldPos2d);
}
