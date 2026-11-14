#include "editCommon.h"
#include "editEntity.h"
#include "editVertex.h"
#include "editSurface.h"
#include "editTransforms.h"
#include "hotkeys.h"
#include "levelEditor.h"
#include "levelEditorHistory.h"
#include "selection.h"
#include <TFE_Input/input.h>

using namespace TFE_Editor;

namespace LevelEditor
{
	SectorList s_sectorChangeList;
	std::vector<s32> s_idList;

	bool s_moveStarted = false;
	EditorSector* s_transformWallSector = nullptr;
	s32 s_transformWallIndex = -1;
	Vec2f s_moveStartPos;
	Vec2f s_moveStartPos1;
	Vec2f* s_moveVertexPivot = nullptr;
	Vec3f s_prevPos = { 0 };
	Vec2f s_wallNrm;
	Vec2f s_wallTan;
	Vec3f s_rayDir = { 0.0f, 0.0f, 1.0f };

	void handleDelete(bool hasFeature)
	{
		EditorSector* sector = nullptr;
		s32 featureIndex = -1;
		HitPart part = HP_FLOOR;
		bool found = selection_get(hasFeature ? 0 : SEL_INDEX_HOVERED, sector, featureIndex, &part);
		if (!found || !sector)
		{
			return;
		}

		// Specific code for feature type.
		switch (s_editMode)
		{
			case LEDIT_VERTEX:
			{
				if (hasFeature)
				{
					// Selected item(s)
					FeatureId* vertices = nullptr;
					u32 vtxSelectionCount = selection_getList(vertices, SEL_VERTEX);
					if (vertices == nullptr) { return; }
					edit_deleteVertices(vertices, vtxSelectionCount, LName_DeleteVertex);
				}
				else
				{
					// Hovered item
					FeatureId singleVtx = createFeatureId(sector, featureIndex);
					selection_clearHovered();
					edit_deleteVertices(&singleVtx, 1, LName_DeleteVertex);
				}
			} break;
			case LEDIT_WALL:
			{
				if (part == HP_FLOOR || part == HP_CEIL)
				{
					selection_clearHovered();
					edit_deleteSector(sector->id);
				}
				else if (part == HP_SIGN)
				{
					if (featureIndex >= 0)
					{
						selection_clearHovered();
						// Clear the selections when deleting a sign -
						// otherwise the source wall will still be selected.
						edit_clearSelections();

						FeatureId id = createFeatureId(sector, featureIndex, HP_SIGN);
						edit_clearTexture(1, &id);
					}
				}
				else
				{
					if (hasFeature)
					{
						// Selected item(s)
						FeatureId* walls;
						u32 wallSelectionCount = selection_getList(walls, SEL_SURFACE);
						edit_deleteWalls(walls, wallSelectionCount);
					}
					else
					{
						// Hovered item
						FeatureId wallVerts[2];
						EditorWall* wall = &sector->walls[featureIndex];
						wallVerts[0] = createFeatureId(sector, wall->idx[0]);
						wallVerts[1] = createFeatureId(sector, wall->idx[1]);
						edit_deleteVertices(wallVerts, 2, LName_DeleteWall);
					}
				}
			} break;
			case LEDIT_SECTOR:
			{
				if (hasFeature)
				{
					// Selected item(s)
					FeatureId* sectors;
					u32 sectorSelectionCount = selection_getList(sectors, SEL_SECTOR);
					std::vector<s32> sectorIds;
					for (u32 i = 0; i < sectorSelectionCount; i++, sectors++)
					{
						sectorIds.push_back((s32)*sectors); // First 32 bits of FeatureIds are the sector Id (unsigned). We assume not going over 2M sectors?
					}
					edit_deleteSectors(sectorIds);
				}
				else
				{
					// Hovered item
					std::vector<s32> sectorId = { sector->id };
					selection_clearHovered();
					edit_deleteSectors(sectorId);
				}
			} break;
			case LEDIT_ENTITY:
			{
				if (hasFeature)
				{
					// Selected item(s)
					FeatureId* objects;
					u32 objectCount = selection_getList(objects, SEL_ENTITY);

					edit_deleteObjects(objects, objectCount);
					selection_clear();
				}
				else if (featureIndex >= 0)
				{
					// Hovered item
					edit_deleteObject(sector, featureIndex);

					selection_clearHovered();
					selection_clear(SEL_ENTITY);
					cmd_objectListSnapshot(LName_DeleteObject, sector->id);
				}
			} break;
			case LEDIT_NOTES:
			{
				return; // TODO
			} break;
			case LEDIT_GUIDELINES:
			{
				return; // TODO
			} break;
		}
	}

	void handleFeatureEditInput(Vec2f worldPos, RayHitInfo* info)
	{
		s32 mx, my;
		TFE_Input::getMousePos(&mx, &my);

		// Short names to make the logic easier to follow.
		const bool selAdd = TFE_Input::keyModDown(KEYMOD_SHIFT);
		const bool selRem = TFE_Input::keyModDown(KEYMOD_ALT);
		const bool selToggle = TFE_Input::keyModDown(KEYMOD_CTRL);
		const bool selToggleDrag = selAdd && selToggle;

		const bool moveToFloor = isShortcutPressed(SHORTCUT_MOVE_TO_FLOOR);
		const bool moveToCeil = isShortcutPressed(SHORTCUT_MOVE_TO_CEIL);

		const bool mousePressed = s_singleClick;
		const bool mouseDown = TFE_Input::mouseDown(MouseButton::MBUTTON_LEFT);

		const bool del = isShortcutPressed(SHORTCUT_DELETE);
		const bool hasHovered = selection_hasHovered();
		const bool hasFeature = selection_getCount() > 0;

		s32 featureIndex = -1;
		HitPart part = HP_FLOOR;
		EditorSector* sector = nullptr;

		if (del && (hasHovered || hasFeature))
		{
			handleDelete(hasFeature);
			return;
		}

		if (mousePressed)
		{
			assert(!s_dragSelect.active);
			if (!selToggle && (selAdd || selRem))
			{
				startDragSelect(mx, my, selAdd ? DSEL_ADD : DSEL_REM);
			}
			else if (selToggle)
			{
				if (hasHovered && selection_get(SEL_INDEX_HOVERED, sector, featureIndex, &part))
				{
					s_editMove = true;
					adjustGridHeight(sector);
					selection_action(SA_TOGGLE, sector, featureIndex, part);
				}
			}
			else
			{
				if (hasHovered && selection_get(SEL_INDEX_HOVERED, sector, featureIndex, &part))
				{
					s32 modeIndex = featureIndex;
					if (part == HP_FLOOR || part == HP_CEIL || s_editMode == LEDIT_VERTEX || s_editMode == LEDIT_SECTOR)
					{
						modeIndex = -1;
					}
					handleSelectMode(sector, modeIndex);
					if (!selection_action(SA_CHECK_INCLUSION, sector, featureIndex, part))
					{
						selection_action(SA_SET, sector, featureIndex, part);
						edit_applyTransformChange();
					}

					// Set this to the 3D cursor position, so the wall doesn't "pop" when grabbed.
					s_curVtxPos = s_cursor3d;
					adjustGridHeight(sector);
					s_editMove = true;
				}
				else if (!s_editMove)
				{
					startDragSelect(mx, my, DSEL_SET);
				}
			}
		}
		else if (s_doubleClick && s_editMode == LEDIT_WALL) // functionality for vertices, sectors, etc.?
		{
			if (hasHovered && selection_get(SEL_INDEX_HOVERED, sector, featureIndex, &part))
			{
				if (!TFE_Input::keyModDown(KEYMOD_SHIFT))
				{
					selection_clear(SEL_GEO);
				}
				if (part == HP_FLOOR || part == HP_CEIL)
				{
					selectSimilarFlats(sector, part);
				}
				else
				{
					selectSimilarWalls(sector, featureIndex, part);
				}
			}
		}
		else if (mouseDown)
		{
			if (!s_dragSelect.active)
			{
				if (selToggleDrag && hasHovered && selection_get(SEL_INDEX_HOVERED, sector, featureIndex, &part))
				{
					adjustGridHeight(sector);
					selection_action(SA_ADD, sector, featureIndex, part);
				}
			}
			// Draw select continue.
			else if (!selToggle && !s_editMove)
			{
				updateDragSelect(mx, my);
			}
			else
			{
				s_dragSelect.active = false;
				edit_setTransformChange();
			}
		}
		else if (s_dragSelect.active)
		{
			s_dragSelect.active = false;
			edit_setTransformChange();
		}

		if (!s_dragSelect.active)
		{
			// Handle texture copy and paste.
			if (hasHovered)
			{
				if ((s_editMode == LEDIT_WALL && s_view == EDIT_VIEW_3D) ||
					(s_editMode == LEDIT_SECTOR && s_view == EDIT_VIEW_2D))
				{
					edit_applySurfaceTextures();
				}
			}
			// Vertex insertion.
			if (s_editMode == LEDIT_VERTEX || s_editMode == LEDIT_WALL)
			{
				handleVertexInsert(worldPos, info);
			}
			else if (s_editMode == LEDIT_ENTITY)
			{
				handleEntityInsert({ worldPos.x, s_cursor3d.y, worldPos.z }, info);
				if (moveToFloor || moveToCeil) { edit_entityMoveTo(moveToFloor ? MOVETO_FLOOR : MOVETO_CEIL); }
			}
		}
	}
}
