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
	void wallComputeDragSelect();
	void selectSimilarFlats(EditorSector* rootSector, HitPart part);
	void selectSimilarWalls(EditorSector* rootSector, s32 wallIndex, HitPart part, bool autoAlign = false);
	s32 wallHoveredOrSelected(EditorSector*& sector);

	void edit_setAdjoinExcludeList(std::vector<s32>* excludeList = nullptr);
	bool edit_isInAdjoinExcludeList(s32 sectorId);

	void edit_autoAlign(s32 sectorId, s32 wallIndex, HitPart part);
	void edit_applySurfaceTextures();
	void edit_clearCopiedTextureOffset();
	void edit_applyTextureToSelection(s32 texIndex, Vec2f* offset);
	void edit_applySignToSelection(s32 signIndex);
	bool edit_splitWall(s32 sectorId, s32 wallIndex, Vec2f newPos);
	bool edit_tryAdjoin(s32 sectorId, s32 wallId, bool exactMatch);
	void edit_removeAdjoin(s32 sectorId, s32 wallId);
	void edit_checkForWallHit2d(Vec2f& worldPos, EditorSector*& wallSector, s32& wallIndex, HitPart& part, EditorSector* hoverSector);
	void edit_checkForWallHit3d(RayHitInfo* info, EditorSector*& wallSector, s32& wallIndex, HitPart& part, const EditorSector* hoverSector);
	void edit_deleteWalls(FeatureId* walls, u32 wallCount);

	// Transforms
	
}
