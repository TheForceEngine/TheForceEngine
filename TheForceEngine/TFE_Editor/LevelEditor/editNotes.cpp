#include "editNotes.h"
#include "hotkeys.h"
#include "levelEditor.h"
#include "levelEditorData.h"
#include "levelEditorHistory.h"
#include "sharedState.h"
#include <TFE_Editor/editorConfig.h>
#include <TFE_Editor/LevelEditor/Rendering/viewport.h>
#include <TFE_Editor/LevelEditor/Rendering/grid.h>
#include <TFE_Editor/LevelEditor/userPreferences.h>
#include <TFE_System/math.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Editor/errorMessages.h>
#include <TFE_Editor/editor.h>

#include <algorithm>
#include <vector>
#include <string>
#include <map>

using namespace TFE_Editor;
using namespace TFE_Jedi;

namespace LevelEditor
{
	static Vec3f s_moveBasePos3d = { 0 };
	static Vec3f s_moveStartPos3d = { 0 };
	
	void handleLevelNoteEdit(RayHitInfo* hitInfo, const Vec3f& rayDir)
	{
		if (s_editorConfig.interfaceFlags & PIF_HIDE_NOTES)
		{
			return;
		}

		const s32 levelNoteCount = (s32)s_level.notes.size();
		LevelNote* note = s_level.notes.data();
		s_hoveredLevelNote = -1;

		if (s_view == EDIT_VIEW_2D) // Get hovered note 2D
		{
			f32 maxY = -FLT_MAX;
			for (s32 n = 0; n < levelNoteCount; n++, note++)
			{
				if (!levelNote_isInteractable(note)) { continue; }

				const f32 dx = fabsf(note->pos.x - s_cursor3d.x);
				const f32 dz = fabsf(note->pos.z - s_cursor3d.z);
				const f32 w = c_levelNoteRadius2d;

				bool cursorInside = dx <= w && dz <= w;
				f32 yTop = note->pos.y + c_levelNoteRadius2d;

				if (cursorInside && yTop > maxY)
				{
					s_hoveredLevelNote = n;
					maxY = yTop;
				}
			}
		}
		else // Get hovered note 3D
		{
			// See if we can select an object.
			Ray ray = { s_camera.pos, rayDir, 1000.0f };
			f32 closestHit = FLT_MAX;
			for (s32 n = 0; n < levelNoteCount; n++, note++)
			{
				if (!levelNote_isInteractable(note)) { continue; }

				const Vec3f bounds[2] = { {note->pos.x - c_levelNoteRadius3d, note->pos.y - c_levelNoteRadius3d, note->pos.z - c_levelNoteRadius3d},
				                          {note->pos.x + c_levelNoteRadius3d, note->pos.y + c_levelNoteRadius3d, note->pos.z + c_levelNoteRadius3d} };
				f32 dist;
				bool hitBounds = rayAABBIntersection(&ray, bounds, &dist);

				f32 maxDist = 1000.0f;
				if (!(note->flags & LNF_3D_NO_FADE))
				{
					maxDist = note->fade.z;
				}

				if (hitBounds && dist < closestHit && dist < maxDist)
				{
					s_hoveredLevelNote = n;
					closestHit = dist;
				}
			}
		}

		// Notes cannot be selected or edited unless in the Level Notes mode.
		if (s_editMode != LEDIT_NOTES)
		{
			// No note can be selected.
			s_curLevelNote = -1;
			return;
		}

		if (isShortcutPressed(SHORTCUT_PLACE))
		{
			Vec3f pos = s_cursor3d;
			snapToGrid(&pos);

			if (s_view == EDIT_VIEW_2D)
			{
				// Place on the floor.
				pos.y = s_grid.height;
			}
			LevelNote newNote = {};
			newNote.pos = pos;
			newNote.groupId = groups_getCurrentId();
			newNote.groupIndex = 0;
			// Test
			newNote.note = "Test Note\n  * Item 1\n  * Item 2\n";
			//
			s32 resultId = addLevelNoteToLevel(&newNote);
			if (resultId >= 0)
			{
				cmd_levelNoteSnapshot(LName_LevelNote_Create);
			}
		}
		else if (s_singleClick && s_hoveredLevelNote >= 0)
		{
			s_curLevelNote = s_hoveredLevelNote;

			LevelNote* note = &s_level.notes[s_curLevelNote];
			s_editMove = true;
			s_moveBasePos3d = note->pos;

			if (s_view == EDIT_VIEW_2D)
			{
				s_cursor3d.y = note->pos.y;
			}
			else
			{
				// Do level trace, ignoring objects.

				f32 gridHeight = s_grid.height;
				s_grid.height = s_cursor3d.y;

				s_cursor3d = rayGridPlaneHit(s_camera.pos, rayDir);
				s_grid.height = gridHeight;
			}
			s_moveStartPos3d = s_cursor3d;
		}
		else if (s_leftMouseDown)
		{
			if (s_editMove && s_curLevelNote >= 0)
			{
				LevelNote* note = &s_level.notes[s_curLevelNote];
				if (s_view == EDIT_VIEW_2D)
				{
					s_cursor3d.y = note->pos.y;
				}
				else
				{
					f32 gridHeight = s_grid.height;
					s_grid.height = s_moveStartPos3d.y;

					s_cursor3d = rayGridPlaneHit(s_camera.pos, rayDir);
					s_grid.height = gridHeight;
				}

				if (isShortcutHeld(SHORTCUT_MOVE_Y))
				{
					s_curVtxPos = s_moveStartPos3d;
					f32 yNew = moveAlongRail({ 0.0f, 1.0f, 0.0f }).y;

					f32 yPrev = note->pos.y;
					note->pos.y = s_moveBasePos3d.y + (yNew - s_moveStartPos3d.y);
					f32 yDelta = note->pos.y - yPrev;
					snapToGridY(&note->pos.y);

					if (s_view == EDIT_VIEW_3D)
					{
						Vec3f rail[] = { note->pos, { note->pos.x, note->pos.y + 1.0f, note->pos.z},
							{ note->pos.x, note->pos.y - 1.0f, note->pos.z } };
						Vec3f moveDir = { 0.0f, yDelta, 0.0f };
						moveDir = TFE_Math::normalize(&moveDir);
						viewport_setRail(rail, 2, &moveDir);

						s_moveStartPos3d.x = s_curVtxPos.x;
						s_moveStartPos3d.z = s_curVtxPos.z;
					}
				}
				else if (s_moveStartPos3d.x != s_cursor3d.x || s_moveStartPos3d.z != s_cursor3d.z)
				{
					Vec3f prevPos = note->pos;

					if (isShortcutHeld(SHORTCUT_MOVE_X))
					{
						note->pos.x = (s_cursor3d.x - s_moveStartPos3d.x) + s_moveBasePos3d.x;
					}
					else if (isShortcutHeld(SHORTCUT_MOVE_Z))
					{
						note->pos.z = (s_cursor3d.z - s_moveStartPos3d.z) + s_moveBasePos3d.z;
					}
					else
					{
						note->pos.x = (s_cursor3d.x - s_moveStartPos3d.x) + s_moveBasePos3d.x;
						note->pos.z = (s_cursor3d.z - s_moveStartPos3d.z) + s_moveBasePos3d.z;
					}
					Vec3f moveDir = { note->pos.x - prevPos.x, 0.0f, note->pos.z - prevPos.z };
					snapToGrid(&note->pos);

					if (s_view == EDIT_VIEW_3D)
					{
						Vec3f boxPos = { note->pos.x, s_cursor3d.y, note->pos.z };
						Vec3f rail[] = { boxPos, { boxPos.x + 1.0f, boxPos.y, boxPos.z}, { boxPos.x, boxPos.y, boxPos.z + 1.0f },
							{ boxPos.x - 1.0f, boxPos.y, boxPos.z}, { boxPos.x, boxPos.y, boxPos.z - 1.0f } };
						moveDir = TFE_Math::normalize(&moveDir);
						viewport_setRail(rail, 4, &moveDir);
					}
				}
			}
			else
			{
				s_editMove = false;
			}
		}
		else if (!s_leftMouseDown && s_editMove)
		{
			// Only record in history if moved, not clicked
			if (s_moveBasePos3d.x != s_level.notes[s_curLevelNote].pos.x ||
				s_moveBasePos3d.y != s_level.notes[s_curLevelNote].pos.y ||
				s_moveBasePos3d.z != s_level.notes[s_curLevelNote].pos.z)
			{
				cmd_levelNoteSnapshot(LName_LevelNote_Move);
			}
			s_editMove = false;
		}
		else
		{
			s_editMove = false;
		}

		if (isShortcutPressed(SHORTCUT_DELETE))
		{
			bool deleted = false;
			if (s_curLevelNote >= 0)
			{
				edit_deleteLevelNote(s_curLevelNote);
				deleted = true;
			}
			else if (s_hoveredLevelNote >= 0)
			{
				edit_deleteLevelNote(s_hoveredLevelNote);
				deleted = true;
			}
			if (deleted)
			{
				clearLevelNoteSelection();
			}
		}
	}
}
