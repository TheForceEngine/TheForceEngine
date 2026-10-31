#include "editSurface.h"
#include "editTransforms.h"
#include "editVertex.h"
#include "editCommon.h"
#include "browser.h"
#include "levelEditor.h"
#include "hotkeys.h"
#include "error.h"
#include "infoPanel.h"
#include "levelEditor.h"
#include "levelEditorData.h"
#include "levelEditorHistory.h"
#include "sharedState.h"
#include "selection.h"
#include "guidelines.h"
#include <TFE_System/math.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Editor/errorMessages.h>
#include <TFE_Editor/editor.h>
#include <TFE_Editor/editorConfig.h>
#include <TFE_Editor/LevelEditor/Rendering/grid.h>
#include <TFE_Input/input.h>
#include <TFE_Jedi/Level/rwall.h>

#include <climits>
#include <algorithm>
#include <vector>
#include <string>
#include <map>

using namespace TFE_Editor;
using namespace TFE_Jedi;

namespace LevelEditor
{
	extern SelectionList s_featureList;
	static std::vector<s32>* s_adjoinExcludeList = nullptr;
	static Vec2f s_copiedTextureOffset = { 0 };

	void snapSignToCursor(EditorSector* sector, EditorWall* wall, s32 signTexIndex, Vec2f* signOffset);
	void splitWall(EditorSector* sector, s32 wallIndex, Vec2f newPos, EditorWall* outWalls[]);
	bool canSplitWall(EditorSector* sector, s32 wallIndex, Vec2f newPos);

	////////////////////////////////////////
	// API
	////////////////////////////////////////
	void edit_setAdjoinExcludeList(std::vector<s32>* excludeList)
	{
		s_adjoinExcludeList = excludeList;
	}

	bool edit_isInAdjoinExcludeList(s32 sectorId)
	{
		if (!s_adjoinExcludeList || sectorId < 0) { return false; }
		const size_t count = s_adjoinExcludeList->size();
		const s32* idx = s_adjoinExcludeList->data();
		for (size_t i = 0; i < count; i++)
		{
			if (sectorId == idx[i]) { return true; }
		}
		return false;
	}

	void edit_clearCopiedTextureOffset()
	{
		s_copiedTextureOffset = { 0 };
	}

	// Hacky version for now, clean up once it works... (separate selection lists).
	void edit_autoAlign(s32 sectorId, s32 wallIndex, HitPart part)
	{
		// Save the selection and clear.
		selection_saveGeoSelections();
		selection_clear(SEL_GEO);

		// Find similar walls.
		EditorSector* rootSector = &s_level.sectors[sectorId];
		if (wallIndex >= 0 && wallIndex < (s32)rootSector->walls.size())
		{
			selectSimilarWalls(rootSector, wallIndex, part, true);
		}

		// Add to the history.
		FeatureId* list = nullptr;
		const s32 count = (s32)selection_getList(list, SEL_SURFACE);
		cmd_setTextures(LName_Autoalign, count, list);

		// Restore the selection.
		selection_restoreGeoSelections();
	}

	void edit_applySurfaceTextures()
	{
		if (!selection_hasHovered()) { return; }
		EditorSector* sector = nullptr;
		HitPart part = HP_MID;
		s32 index = -1;
		if (s_editMode == LEDIT_WALL)
		{
			selection_getSurface(SEL_INDEX_HOVERED, sector, index, &part);
		}
		else if (s_editMode == LEDIT_SECTOR)
		{
			selection_getSector(SEL_INDEX_HOVERED, sector);
			part = (s_sectorDrawMode == SDM_TEXTURED_FLOOR) ? HP_FLOOR : HP_CEIL;
		}
		if (!sector) { return; }

		if (isShortcutPressed(SHORTCUT_COPY_TEXTURE, 0))
		{
			const LevelTextureAsset* textureAsset = nullptr;
			if (s_view == EDIT_VIEW_2D)
			{
				if (s_sectorDrawMode == SDM_TEXTURED_FLOOR)
				{
					textureAsset = sector->floorTex.texIndex < 0 ? nullptr : &s_level.textures[sector->floorTex.texIndex];
					s_copiedTextureOffset = sector->floorTex.offset;
				}
				else if (s_sectorDrawMode == SDM_TEXTURED_CEIL)
				{
					textureAsset = sector->ceilTex.texIndex < 0 ? nullptr : &s_level.textures[sector->ceilTex.texIndex];
					s_copiedTextureOffset = sector->ceilTex.offset;
				}
			}
			else
			{
				if (part == HP_FLOOR)
				{
					textureAsset = sector->floorTex.texIndex < 0 ? nullptr : &s_level.textures[sector->floorTex.texIndex];
					s_copiedTextureOffset = sector->floorTex.offset;
				}
				else if (part == HP_CEIL)
				{
					textureAsset = sector->ceilTex.texIndex < 0 ? nullptr : &s_level.textures[sector->ceilTex.texIndex];
					s_copiedTextureOffset = sector->ceilTex.offset;
				}
				else
				{
					const EditorWall* wall = &sector->walls[index];
					if (part == HP_MID)
					{
						textureAsset = wall->tex[WP_MID].texIndex < 0 ? nullptr : &s_level.textures[wall->tex[WP_MID].texIndex];
						s_copiedTextureOffset = wall->tex[WP_MID].offset;
					}
					else if (part == HP_TOP)
					{
						textureAsset = wall->tex[HP_TOP].texIndex < 0 ? nullptr : &s_level.textures[wall->tex[HP_TOP].texIndex];
						s_copiedTextureOffset = wall->tex[HP_TOP].offset;
					}
					else if (part == HP_BOT)
					{
						textureAsset = wall->tex[HP_BOT].texIndex < 0 ? nullptr : &s_level.textures[wall->tex[HP_BOT].texIndex];
						s_copiedTextureOffset = wall->tex[HP_BOT].offset;
					}
					else if (part == HP_SIGN)
					{
						textureAsset = wall->tex[HP_SIGN].texIndex < 0 ? nullptr : &s_level.textures[wall->tex[HP_SIGN].texIndex];
						s_copiedTextureOffset = wall->tex[HP_SIGN].offset;
					}
				}
			}

			if (textureAsset)
			{
				// Find the texture in the list.
				const s32 count = (s32)s_levelTextureList.size();
				const Asset* levelTexAsset = s_levelTextureList.data();
				for (s32 i = 0; i < count; i++, levelTexAsset++)
				{
					if (strcasecmp(levelTexAsset->name.c_str(), textureAsset->name.c_str()) == 0)
					{
						s_selectedTexture = i;
						browserScrollToSelection();
						break;
					}
				}
				// Clear the clipboard.
				edit_clearClipboard();
			}
		}
		else if (isShortcutPressed(SHORTCUT_SET_SIGN, 0) && s_selectedTexture >= 0)
		{
			edit_applySignToSelection(s_selectedTexture);
		}
		else if (isShortcutPressed(SHORTCUT_SET_TEXTURE, 0) && s_selectedTexture >= 0)
		{
			edit_applyTextureToSelection(s_selectedTexture, nullptr);
		}
		else if (isShortcutPressed(SHORTCUT_PASTE) && !edit_hasItemsInClipboard() && s_selectedTexture >= 0)
		{
			edit_applyTextureToSelection(s_selectedTexture, &s_copiedTextureOffset);
		}
		else if (isShortcutPressed(SHORTCUT_AUTO_ALIGN))
		{
			edit_autoAlign(sector->id, index, part);
		}
	}

	// Convert wall selections into vertex selections and pass it onto edit_deleteVertices()
	void edit_deleteWalls(FeatureId* walls, u32 wallCount)
	{
		selection_clear(SEL_VERTEX);
		selection_buildDerived();
		FeatureId* vtxList;
		u32 vtxCount = selection_getList(vtxList, SEL_VERTEX);

		edit_deleteVertices(vtxList, vtxCount, LName_DeleteWall);
	}

	void edit_applyTextureToSelection(s32 texIndex, Vec2f* offset)
	{
		if (!selection_hasHovered()) { return; }
		
		bool doesItemExist = false;
		EditorSector* sector = nullptr;
		s32 featureIndex = -1;
		HitPart part = HP_NONE;
		if (s_editMode == LEDIT_WALL)
		{
			selection_getSurface(SEL_INDEX_HOVERED, sector, featureIndex, &part);
			doesItemExist = selection_surface(SA_CHECK_INCLUSION, sector, featureIndex, part);
		}
		else if (s_editMode == LEDIT_SECTOR)
		{
			selection_getSector(SEL_INDEX_HOVERED, sector);
			doesItemExist = selection_sector(SA_CHECK_INCLUSION, sector);
		}

		if (selection_getCount() < 1 || doesItemExist)
		{
			if (doesItemExist)
			{
				FeatureId* list = nullptr;
				s32 count = (s32)selection_getList(list);
				if (count && list)
				{
					// We need to fix up the sector list.
					if (s_view == EDIT_VIEW_2D)
					{
						s_featureList.resize(count);
						FeatureId* output = s_featureList.data();
						for (s32 i = 0; i < count; i++)
						{
							EditorSector* sector = unpackFeatureId(list[i]);
							output[i] = createFeatureId(sector, 0, s32((s_sectorDrawMode == SDM_TEXTURED_CEIL) ? HP_CEIL : HP_FLOOR));
						}
						list = output;
					}

					edit_setTexture(count, list, texIndex, offset);
					cmd_setTextures(LName_SetTexture, count, list);
				}
			}
			else
			{
				FeatureId id;
				if (s_view == EDIT_VIEW_2D)
				{
					id = createFeatureId(sector, 0, s32((s_sectorDrawMode == SDM_TEXTURED_CEIL) ? HP_CEIL : HP_FLOOR));
				}
				else
				{
					id = createFeatureId(sector, featureIndex, part);
				}
				edit_setTexture(1, &id, texIndex, offset);
				cmd_setTextures(LName_SetTexture, 1, &id);
			}
			setTextureAssignDirty();
		}
	}

	void edit_applySignToSelection(s32 signIndex)
	{
		s32 texIndex = getTextureIndex(s_levelTextureList[signIndex].name.c_str());
		if (texIndex < 0) { return; }
		EditorTexture* signTex = (EditorTexture*)getAssetData(s_level.textures[texIndex].handle);
		if (!signTex) { return; }

		// For now, this only works for hovered features.
		//FeatureId id = createFeatureId(s_featureHovered.sector, s_featureHovered.featureIndex, WP_SIGN);
		if (!selection_hasHovered() || s_editMode != LEDIT_WALL)
		{
			return;
		}
		EditorSector* sector;
		s32 featureIndex;
		if (!selection_getSurface(SEL_INDEX_HOVERED, sector, featureIndex))
		{
			return;
		}
		setTextureAssignDirty();

		// Center the sign on the mouse cursor.
		EditorWall* wall = &sector->walls[featureIndex];

		Vec2f offset = { 0 };
		snapSignToCursor(sector, wall, texIndex, &offset);

		FeatureId id = createFeatureId(sector, featureIndex, WP_SIGN);
		edit_setTexture(1, &id, signIndex, &offset);
		cmd_setTextures(LName_SetTexture, 1, &id);
	}

	void wallComputeDragSelect()
	{
		if (s_dragSelect.curPos.x == s_dragSelect.startPos.x && s_dragSelect.curPos.z == s_dragSelect.startPos.z)
		{
			return;
		}
		if (s_dragSelect.mode == DSEL_SET)
		{
			selection_clear(SEL_GEO, false);
		}

		if (s_view == EDIT_VIEW_2D)
		{
			// Convert drag select coordinates to world space.
			// World position from viewport position, relative mouse position and zoom.
			Vec2f worldPos[2];
			worldPos[0].x = s_viewportPos.x + f32(s_dragSelect.curPos.x) * s_zoom2d;
			worldPos[0].z = -(s_viewportPos.z + f32(s_dragSelect.curPos.z) * s_zoom2d);
			worldPos[1].x = s_viewportPos.x + f32(s_dragSelect.startPos.x) * s_zoom2d;
			worldPos[1].z = -(s_viewportPos.z + f32(s_dragSelect.startPos.z) * s_zoom2d);

			Vec3f aabb[] =
			{
				{ std::min(worldPos[0].x, worldPos[1].x), 0.0f, std::min(worldPos[0].z, worldPos[1].z) },
				{ std::max(worldPos[0].x, worldPos[1].x), 0.0f, std::max(worldPos[0].z, worldPos[1].z) }
			};

			const size_t sectorCount = s_level.sectors.size();
			EditorSector* sector = s_level.sectors.data();
			for (size_t s = 0; s < sectorCount; s++, sector++)
			{
				if (!sector_isInteractable(sector) || !sector_onActiveLayer(sector)) { continue; }
				if (!aabbOverlap2d(sector->bounds, aabb)) { continue; }

				const size_t wallCount = sector->walls.size();
				const EditorWall* wall = sector->walls.data();
				const Vec2f* vtx = sector->vtx.data();
				for (size_t w = 0; w < wallCount; w++, wall++)
				{
					const Vec2f* v0 = &vtx[wall->idx[0]];
					const Vec2f* v1 = &vtx[wall->idx[1]];

					if (v0->x >= aabb[0].x && v0->x < aabb[1].x && v0->z >= aabb[0].z && v0->z < aabb[1].z &&
						v1->x >= aabb[0].x && v1->x < aabb[1].x && v1->z >= aabb[0].z && v1->z < aabb[1].z)
					{
						selection_surface(s_dragSelect.mode == DSEL_REM ? SA_REMOVE : SA_ADD, sector, (s32)w);
					}
				}
			}
		}
		else if (s_view == EDIT_VIEW_3D)
		{
			// In 3D, build a frustum and select point inside the frustum.
			// The near plane corners of the frustum are derived from the screenspace rectangle.
			const s32 x0 = std::min(s_dragSelect.startPos.x, s_dragSelect.curPos.x);
			const s32 x1 = std::max(s_dragSelect.startPos.x, s_dragSelect.curPos.x);
			const s32 z0 = std::min(s_dragSelect.startPos.z, s_dragSelect.curPos.z);
			const s32 z1 = std::max(s_dragSelect.startPos.z, s_dragSelect.curPos.z);
			// Directions to the near-plane frustum corners.
			const Vec3f d0 = edit_viewportCoordToWorldDir3d({ x0, z0 });
			const Vec3f d1 = edit_viewportCoordToWorldDir3d({ x1, z0 });
			const Vec3f d2 = edit_viewportCoordToWorldDir3d({ x1, z1 });
			const Vec3f d3 = edit_viewportCoordToWorldDir3d({ x0, z1 });
			// Camera forward vector.
			const Vec3f fwd = { -s_camera.viewMtx.m2.x, -s_camera.viewMtx.m2.y, -s_camera.viewMtx.m2.z };
			// Trace forward at the screen center to get the likely focus sector.
			Vec3f centerViewDir = edit_viewportCoordToWorldDir3d({ s_viewportSize.x / 2, s_viewportSize.z / 2 });
			RayHitInfo hitInfo;
			Ray ray = { s_camera.pos, centerViewDir, 1000.0f };

			f32 nearDist = 1.0f;
			f32 farDist = 100.0f;
			if (traceRay(&ray, &hitInfo, false, false) && hitInfo.hitSectorId >= 0)
			{
				EditorSector* hitSector = &s_level.sectors[hitInfo.hitSectorId];
				Vec3f bbox[] =
				{
					{hitSector->bounds[0].x, hitSector->floorHeight, hitSector->bounds[0].z},
					{hitSector->bounds[1].x, hitSector->floorHeight, hitSector->bounds[0].z},
					{hitSector->bounds[1].x, hitSector->floorHeight, hitSector->bounds[1].z},
					{hitSector->bounds[0].x, hitSector->floorHeight, hitSector->bounds[1].z},

					{hitSector->bounds[0].x, hitSector->ceilHeight, hitSector->bounds[0].z},
					{hitSector->bounds[1].x, hitSector->ceilHeight, hitSector->bounds[0].z},
					{hitSector->bounds[1].x, hitSector->ceilHeight, hitSector->bounds[1].z},
					{hitSector->bounds[0].x, hitSector->ceilHeight, hitSector->bounds[1].z},
				};
				f32 maxDist = 0.0f;
				for (s32 i = 0; i < 8; i++)
				{
					Vec3f offset = { bbox[i].x - s_camera.pos.x, bbox[i].y - s_camera.pos.y, bbox[i].z - s_camera.pos.z };
					f32 dist = TFE_Math::dot(&offset, &fwd);
					if (dist > maxDist)
					{
						maxDist = dist;
					}
				}
				if (maxDist > 0.0f) { farDist = maxDist; }
			}
			// Build Planes.
			Vec3f near = { s_camera.pos.x + nearDist * fwd.x, s_camera.pos.y + nearDist * fwd.y, s_camera.pos.z + nearDist * fwd.z };
			Vec3f far = { s_camera.pos.x + farDist * fwd.x, s_camera.pos.y + farDist * fwd.y, s_camera.pos.z + farDist * fwd.z };

			Vec3f left = TFE_Math::cross(&d3, &d0);
			Vec3f right = TFE_Math::cross(&d1, &d2);
			Vec3f top = TFE_Math::cross(&d0, &d1);
			Vec3f bot = TFE_Math::cross(&d2, &d3);

			left = TFE_Math::normalize(&left);
			right = TFE_Math::normalize(&right);
			top = TFE_Math::normalize(&top);
			bot = TFE_Math::normalize(&bot);

			const Vec4f plane[] =
			{
				{ -fwd.x, -fwd.y, -fwd.z,  TFE_Math::dot(&fwd, &near) },
				{  fwd.x,  fwd.y,  fwd.z, -TFE_Math::dot(&fwd, &far) },
				{ left.x, left.y, left.z, -TFE_Math::dot(&left, &s_camera.pos) },
				{ right.x, right.y, right.z, -TFE_Math::dot(&right, &s_camera.pos) },
				{ top.x, top.y, top.z, -TFE_Math::dot(&top, &s_camera.pos) },
				{ bot.x, bot.y, bot.z, -TFE_Math::dot(&bot, &s_camera.pos) }
			};

			const size_t sectorCount = s_level.sectors.size();
			EditorSector* sector = s_level.sectors.data();
			for (size_t s = 0; s < sectorCount; s++, sector++)
			{
				if (!sector_isInteractable(sector) || !sector_onActiveLayer(sector)) { continue; }

				// For now, do them all...
				const size_t wallCount = sector->walls.size();
				const EditorWall* wall = sector->walls.data();
				const Vec2f* vtx = sector->vtx.data();
				for (size_t w = 0; w < wallCount; w++, wall++)
				{
					const Vec2f* v0 = &vtx[wall->idx[0]];
					const Vec2f* v1 = &vtx[wall->idx[1]];

					struct Vec3f wallVtx[] =
					{
						{ v0->x, sector->floorHeight, v0->z },
						{ v1->x, sector->floorHeight, v1->z },
						{ v1->x, sector->ceilHeight, v1->z },
						{ v0->x, sector->ceilHeight, v0->z }
					};

					bool inside = true;
					for (s32 p = 0; p < 6; p++)
					{
						// Check the wall vertices.
						for (s32 v = 0; v < 4; v++)
						{
							if (plane[p].x*wallVtx[v].x + plane[p].y*wallVtx[v].y + plane[p].z*wallVtx[v].z + plane[p].w > 0.0f)
							{
								inside = false;
								break;
							}
						}
					}

					if (inside)
					{
						selection_surface(s_dragSelect.mode == DSEL_REM ? SA_REMOVE : SA_ADD, sector, (s32)w);
					}
				}
				// TODO: Check the floor and ceiling?
			}
		}
	}

	// Algorithm:
	// * Insert new wall after current wall.
	// * Find any references to sector with mirror > currentWall and fix-up (+1).
	// * If current wall has an adjoin, split the mirror wall.
	// * Find any references to the adjoined sector with mirror > currentWall mirror.
	// Return true if a split was required.
	bool edit_splitWall(s32 sectorId, s32 wallIndex, Vec2f newPos)
	{
		EditorSector* sector = &s_level.sectors[sectorId];
		// If a vertex at the desired position already exists, do not split the wall (early-out).
		if (!canSplitWall(sector, wallIndex, newPos)) { return false; }

		// Split the wall, insert the new wall after the original.
		// Example, split B into {B, N} : {A, B, C, D} -> {A, B, N, C, D}.
		EditorWall* outWalls[2] = { nullptr };
		splitWall(sector, wallIndex, newPos, outWalls);

		// If the current wall is an adjoin, split the matching adjoin.
		if (outWalls[0]->adjoinId >= 0 && outWalls[0]->mirrorId >= 0)
		{
			EditorSector* nextSector = &s_level.sectors[outWalls[0]->adjoinId];
			const s32 mirrorWallIndex = outWalls[0]->mirrorId;

			// Split the mirror wall.
			EditorWall* outWallsAdjoin[2] = { nullptr };
			splitWall(nextSector, mirrorWallIndex, newPos, outWallsAdjoin);

			// Connect the split edges together.
			outWalls[0]->mirrorId = mirrorWallIndex + 1;
			outWalls[1]->mirrorId = mirrorWallIndex;
			outWallsAdjoin[0]->mirrorId = wallIndex + 1;
			outWallsAdjoin[1]->mirrorId = wallIndex;

			// Fix-up the adjoined sector polygon.
			sectorToPolygon(nextSector);
		}
		// Fix-up the sector polygon.
		sectorToPolygon(sector);
		return true;
	}
		
	// Try to adjoin wallId of sectorId to a matching wall.
	// if *exactMatch* is true, than the other wall vertices must match,
	// otherwise if the walls overlap, vertices will be inserted.
	// Return true if wall split, otherwise false.
	bool edit_tryAdjoin(s32 sectorId, s32 wallId, bool exactMatch)
	{
		if (sectorId < 0 || sectorId >= (s32)s_level.sectors.size() || wallId < 0) { return false; }
		EditorSector* srcSector = &s_level.sectors[sectorId];
		const Vec3f* srcBounds = srcSector->bounds;
		const Vec2f* srcVtx = srcSector->vtx.data();

		if (wallId >= (s32)srcSector->walls.size()) { return false; }
		EditorWall* srcWall = &srcSector->walls[wallId];
		const Vec2f s0 = srcVtx[srcWall->idx[0]];
		const Vec2f s1 = srcVtx[srcWall->idx[1]];
		const Vec2f srcDir = { s1.x - s0.x, s1.z - s0.z };

		SectorList overlaps;
		getOverlappingSectorsBounds(srcBounds, &overlaps);
		const s32 sectorCount = (s32)overlaps.size();
		EditorSector** overlap = overlaps.data();

		const f32 onLineEps = 1e-3f;
		for (s32 s = 0; s < sectorCount; s++)
		{
			EditorSector* conSector = overlap[s];
			// Cannot connect to the same sector or to a sector part of a hidden or locked group.
			if (conSector->id == sectorId) { continue; }
			if (!sector_isInteractable(conSector) || !sector_onActiveLayer(conSector)) { continue; }
			if (edit_isInAdjoinExcludeList(conSector->id)) { continue; }

			EditorWall* conWall = conSector->walls.data();
			const Vec2f* conVtx = conSector->vtx.data();
			const s32 wallCount = (s32)conSector->walls.size();

			for (s32 w = 0; w < wallCount; w++, conWall++)
			{
				const Vec2f t0 = conVtx[conWall->idx[0]];
				const Vec2f t1 = conVtx[conWall->idx[1]];
				const Vec2f curDir = { t1.x - t0.x, t1.z - t0.z };

				// Walls must have the same vertex positions AND be going in opposite directions.
				if (TFE_Polygon::vtxEqual(&s0, &t1) && TFE_Polygon::vtxEqual(&s1, &t0))
				{
					srcWall->adjoinId = conSector->id;
					srcWall->mirrorId = w;

					conWall->adjoinId = sectorId;
					conWall->mirrorId = wallId;
					// Only one adjoin is possible.
					return false;
				}
				else if (!exactMatch)
				{
					// Verify that the walls are going in opposite directions.
					if (TFE_Math::dot(&srcDir, &curDir) >= 0.0f) { continue; }

					Vec2f p0, p1;
					// First assume (s0,s1) is smaller than (t0,t1)
					{
						const f32 u = TFE_Polygon::closestPointOnLineSegment(t0, t1, s0, &p0);
						const f32 v = TFE_Polygon::closestPointOnLineSegment(t0, t1, s1, &p1);
						if (u >= -FLT_EPSILON && u <= 1.0f + FLT_EPSILON && v >= -FLT_EPSILON && v <= 1.0f + FLT_EPSILON)
						{
							// Make sure the closest points are *on* the wall t0 -> t1.
							const Vec2f offset0 = { p0.x - s0.x, p0.z - s0.z };
							const Vec2f offset1 = { p1.x - s1.x, p1.z - s1.z };
							if (offset0.x*offset0.x + offset0.z*offset0.z <= onLineEps && offset1.x*offset1.x + offset1.z*offset1.z <= onLineEps)
							{
								// We found a good candidate, so split and add.
								EditorWall* outWalls[2];
								s32 wSplit = w;
								bool splitNeeded = false;
								if (v >= FLT_EPSILON)
								{
									splitWall(conSector, wSplit, p1, outWalls);
									splitNeeded = true;
									wSplit++;
								}
								if (u <= 1.0f - FLT_EPSILON)
								{
									splitWall(conSector, wSplit, p0, outWalls);
									splitNeeded = true;
								}
								// Then try again, but expecting an exact match.
								splitNeeded |= edit_tryAdjoin(sectorId, wallId, true);
								// Wall was split, indices are no longer valid.
								return splitNeeded;
							}
						}
					}
					// If that fails, assume (s0,s1) is larger than (t0,t1)
					{
						const f32 u = TFE_Polygon::closestPointOnLineSegment(s0, s1, t0, &p0);
						const f32 v = TFE_Polygon::closestPointOnLineSegment(s0, s1, t1, &p1);
						if (u >= -FLT_EPSILON && u <= 1.0f + FLT_EPSILON && v >= -FLT_EPSILON && v <= 1.0f + FLT_EPSILON)
						{
							// Make sure the closest points are *on* the wall t0 -> t1.
							const Vec2f offset0 = { p0.x - t0.x, p0.z - t0.z };
							const Vec2f offset1 = { p1.x - t1.x, p1.z - t1.z };
							if (offset0.x*offset0.x + offset0.z*offset0.z <= onLineEps && offset1.x*offset1.x + offset1.z*offset1.z <= onLineEps)
							{
								// We found a good candidate, so split and add.
								EditorWall* outWalls[2];
								s32 wSplit = wallId;
								bool splitNeeded = false;
								if (v >= FLT_EPSILON)
								{
									splitWall(srcSector, wSplit, p1, outWalls);
									splitNeeded = true;
									wSplit++;
								}
								if (u <= 1.0f - FLT_EPSILON)
								{
									splitWall(srcSector, wSplit, p0, outWalls);
									splitNeeded = true;
								}
								// Then try again, but expecting an exact match.
								splitNeeded |= edit_tryAdjoin(sectorId, wSplit, true);
								// Wall was split, indices are no longer valid.
								return splitNeeded;
							}
						}
					}
				}
			}
		}
		return false;
	}

	// If the wall is adjoined, remove the adjoin and the mirror.
	void edit_removeAdjoin(s32 sectorId, s32 wallId)
	{
		if (sectorId < 0 || sectorId >= (s32)s_level.sectors.size() || wallId < 0) { return; }
		EditorSector* sector = &s_level.sectors[sectorId];

		if (wallId >= (s32)sector->walls.size()) { return; }
		EditorWall* wall = &sector->walls[wallId];
		if (wall->adjoinId >= 0 && wall->adjoinId < (s32)s_level.sectors.size())
		{
			EditorSector* next = &s_level.sectors[wall->adjoinId];
			if (wall->mirrorId >= 0 && wall->mirrorId < (s32)next->walls.size())
			{
				next->walls[wall->mirrorId].adjoinId = -1;
				next->walls[wall->mirrorId].mirrorId = -1;
			}
		}
		wall->adjoinId = -1;
		wall->mirrorId = -1;
	}

	void edit_checkForWallHit2d(Vec2f& worldPos, EditorSector*& wallSector, s32& wallIndex, HitPart& part, EditorSector* hoverSector)
	{
		// See if we are close enough to "hover" a wall
		wallIndex = -1;
		wallSector = nullptr;
		if (hoverSector)
		{
			const f32 maxDist = s_zoom2d * 16.0f;
			wallIndex = findClosestWallInSector(hoverSector, &worldPos, maxDist * maxDist, nullptr);
			if (wallIndex >= 0)
			{
				wallSector = hoverSector;
				part = HP_MID;
			}
			else
			{
				wallSector = nullptr;
			}
		}
	}

	void edit_checkForWallHit3d(RayHitInfo* info, EditorSector*& wallSector, s32& wallIndex, HitPart& part, const EditorSector* hoverSector)
	{
		// See if we are close enough to "hover" a wall
		wallIndex = -1;
		if (info->hitWallId >= 0 && info->hitSectorId >= 0)
		{
			wallSector = &s_level.sectors[info->hitSectorId];
			wallIndex = info->hitWallId;
			part = info->hitPart;
		}
		else
		{
			// Are we close enough to an edge?
			// Project the point onto the floor.
			const Vec2f pos2d = { info->hitPos.x, info->hitPos.z };
			const f32 distFromCam = TFE_Math::distance(&info->hitPos, &s_camera.pos);
			const f32 maxDist = distFromCam * 16.0f / f32(s_viewportSize.z);
			const f32 maxDistSq = maxDist * maxDist;
			wallIndex = findClosestWallInSector(hoverSector, &pos2d, maxDist * maxDist, nullptr);
			wallSector = (EditorSector*)hoverSector;
			if (wallIndex >= 0)
			{
				const EditorWall* wall = &hoverSector->walls[wallIndex];
				const EditorSector* next = wall->adjoinId >= 0 ? &s_level.sectors[wall->adjoinId] : nullptr;
				const u32 adjType = getAdjoinType(hoverSector, wall);
				if (!next)
				{
					part = HP_MID;
				}
				else if (info->hitPart == HP_FLOOR)
				{
					part = (adjType & ADJ_TYPE_BOT) ? HP_BOT : HP_MID;
				}
				else if (info->hitPart == HP_CEIL)
				{
					part = (adjType & ADJ_TYPE_TOP) ? HP_TOP : HP_MID;
				}
			}
			else
			{
				// Otherwise, select the floor or ceiling...
				if (info->hitPart == HP_FLOOR)
				{
					part = HP_FLOOR;
					wallIndex = 0;
				}
				else if (info->hitPart == HP_CEIL)
				{
					part = HP_CEIL;
					wallIndex = 0;
				}
			}
		}
	}

	s32 wallHoveredOrSelected(EditorSector*& sector)
	{
		s32 wallId = -1;
		if (s_editMode == LEDIT_WALL)
		{
			EditorSector* curSector = nullptr;
			EditorSector* hoveredSector = nullptr;
			s32 curFeatureIndex = -1, hoveredFeatureIndex = -1;
			HitPart curPart = HP_NONE, hoveredPart = HP_NONE;
			selection_getSurface(0, curSector, curFeatureIndex, &curPart);
			selection_getSurface(SEL_INDEX_HOVERED, hoveredSector, hoveredFeatureIndex, &hoveredPart);

			if (curSector && (curPart != HP_CEIL && curPart != HP_FLOOR))
			{
				sector = curSector;
				wallId = curFeatureIndex;
			}
			else if (hoveredSector && (hoveredPart != HP_CEIL && hoveredPart != HP_FLOOR))
			{
				sector = hoveredSector;
				wallId = hoveredFeatureIndex;
			}
		}
		return wallId;
	}

	////////////////////////////////////////
	// Internal
	////////////////////////////////////////
	struct Adjoin
	{
		s32 adjoinId;
		s32 mirrorId;
		s32 side;
		Vec2f offset[2];
	};
	std::vector<Adjoin> s_adjoinList;

	void addToAdjoinList(Adjoin adjoin)
	{
		s32 count = (s32)s_adjoinList.size();
		Adjoin* adj = s_adjoinList.data();
		for (s32 i = 0; i < count; i++, adj++)
		{
			if (adjoin.adjoinId == adj->adjoinId && adjoin.mirrorId == adj->mirrorId && adjoin.side == adj->side)
			{
				return;
			}
		}
		s_adjoinList.push_back(adjoin);
	}

	s32 getLeftWall(EditorSector* sector, s32 wallIndex)
	{
		EditorWall* baseWall = &sector->walls[wallIndex];
		s32 leftIndex = baseWall->idx[0];

		const s32 wallCount = (s32)sector->walls.size();
		const EditorWall* wall = sector->walls.data();
		for (s32 w = 0; w < wallCount; w++, wall++)
		{
			if (wall->idx[1] == leftIndex)
			{
				return w;
			}
		}
		return -1;
	}

	s32 getRightWall(EditorSector* sector, s32 wallIndex)
	{
		EditorWall* baseWall = &sector->walls[wallIndex];
		s32 rightIndex = baseWall->idx[1];

		const s32 wallCount = (s32)sector->walls.size();
		const EditorWall* wall = sector->walls.data();
		for (s32 w = 0; w < wallCount; w++, wall++)
		{
			if (wall->idx[0] == rightIndex)
			{
				return w;
			}
		}
		return -1;
	}

	s32 getPartTextureIndex(EditorWall* wall, HitPart part)
	{
		s32 texId = -1;
		switch (part)
		{
			case HP_MID:
			{
				texId = wall->tex[WP_MID].texIndex;
			} break;
			case HP_TOP:
			{
				texId = wall->tex[WP_TOP].texIndex;
			} break;
			case HP_BOT:
			{
				texId = wall->tex[WP_BOT].texIndex;
			} break;
			case HP_SIGN:
			{
				texId = wall->tex[WP_SIGN].texIndex;
			} break;
		}
		return texId;
	}

	Vec2f getPartTextureOffset(EditorWall* wall, HitPart part)
	{
		Vec2f offset = { 0 };
		switch (part)
		{
			case HP_MID:
			{
				offset = wall->tex[WP_MID].offset;
			} break;
			case HP_TOP:
			{
				offset = wall->tex[WP_TOP].offset;
			} break;
			case HP_BOT:
			{
				offset = wall->tex[WP_BOT].offset;
			} break;
			case HP_SIGN:
			{
				offset = wall->tex[WP_SIGN].offset;
			} break;
		}
		return offset;
	}

	Vec2f getTexOffsetMod(s32 texIndex, Vec2f offset)
	{
		EditorTexture* tex = getTexture(texIndex);
		f32 xmod = tex->width  / 8.0f;
		return { fmodf(offset.x, xmod), offset.z };
	}

	void setPartTextureOffset(EditorWall* wall, HitPart part, Vec2f offset)
	{
		switch (part)
		{
			case HP_MID:
			{
				wall->tex[WP_MID].offset = getTexOffsetMod(wall->tex[WP_MID].texIndex, offset);
			} break;
			case HP_TOP:
			{
				wall->tex[WP_TOP].offset = getTexOffsetMod(wall->tex[WP_TOP].texIndex, offset);
			} break;
			case HP_BOT:
			{
				wall->tex[WP_BOT].offset = getTexOffsetMod(wall->tex[WP_BOT].texIndex, offset);
			} break;
			case HP_SIGN:
			{
				wall->tex[WP_SIGN].offset = offset;
			} break;
		}
	}

	void selectSimilarTraverse(EditorSector* sector, s32 wallIndex, s32 texId, s32 fixedDir, Vec2f* texOffset, f32 baseHeight)
	{
		s32 start = 0;
		s32 end = 2;
		if (fixedDir == 0) { end = 1; }
		else if (fixedDir == 1) { start = 1; }

		for (s32 dir = start; dir < end; dir++)
		{
			// Walk right until we either loop around again or stop.
			// If we stop, we need to loop left.
			s32 nextIndex = dir == 0 ? getRightWall(sector, wallIndex) : getLeftWall(sector, wallIndex);
			while (nextIndex >= 0)
			{
				EditorWall* wall = &sector->walls[nextIndex];
				if (wall->adjoinId < 0)
				{
					s32 curTexId = getPartTextureIndex(wall, HP_MID);
					if (texId < 0 || curTexId == texId)
					{
						if (!selection_surface(SA_ADD, sector, nextIndex))
						{
							break;
						}
						if (texOffset)
						{
							if (dir == 1) { texOffset[dir].x -= getWallLength(sector, wall); }
							setPartTextureOffset(wall, HP_MID, { texOffset[dir].x, texOffset[dir].z + sector->floorHeight - baseHeight });
							if (dir == 0) { texOffset[dir].x += getWallLength(sector, wall); }
						}
					}
					else
					{
						break;
					}
				}
				else
				{
					bool hasMatch = false;
					EditorSector* next = &s_level.sectors[wall->adjoinId];

					f32 offsetX = 0.0f, offsetZ = 0.0f;
					if (texOffset)
					{
						offsetX = dir == 1 ? texOffset[dir].x - getWallLength(sector, wall) : texOffset[dir].x;
						offsetZ = texOffset[dir].z;
					}

					// Add adjoin to the list.
					// Then go left and right from the adjoin.
					if (sector_onActiveLayer(next) && sector_isInteractable(next))
					{
						if (texOffset)
						{
							addToAdjoinList({ wall->adjoinId, wall->mirrorId, dir, {texOffset[0], texOffset[1]} });
						}
						else
						{
							addToAdjoinList({ wall->adjoinId, wall->mirrorId, dir, {0} });
						}
					}

					if (next->ceilHeight < sector->ceilHeight)
					{
						// top
						s32 curTexId = getPartTextureIndex(wall, HP_TOP);
						if (texId < 0 || curTexId == texId)
						{
							if (selection_surface(SA_ADD, sector, nextIndex, HP_TOP))
							{
								hasMatch = true;
								if (texOffset)
								{
									setPartTextureOffset(wall, HP_TOP, { offsetX, offsetZ + next->ceilHeight - baseHeight });
								}
							}
						}
					}
					if (next->floorHeight > sector->floorHeight)
					{
						// bottom
						s32 curTexId = getPartTextureIndex(wall, HP_BOT);
						if (texId < 0 || curTexId == texId)
						{
							if (selection_surface(SA_ADD, sector, nextIndex, HP_BOT))
							{
								hasMatch = true;
								if (texOffset)
								{
									setPartTextureOffset(wall, HP_BOT, { offsetX, offsetZ + sector->floorHeight - baseHeight });
								}
							}
						}
					}
					if (wall->flags[0] & WF1_ADJ_MID_TEX)
					{
						// mid
						s32 curTexId = getPartTextureIndex(wall, HP_MID);
						if (texId < 0 || curTexId == texId)
						{
							if (selection_surface(SA_ADD, sector, nextIndex, HP_MID))
							{
								hasMatch = true;
								if (texOffset)
								{
									setPartTextureOffset(wall, HP_MID, { offsetX, offsetZ + sector->floorHeight - baseHeight });
								}
							}
						}
					}

					if (!hasMatch)
					{
						break;
					}
					else
					{
						// Add adjoin to the list.
						// Then go left and right from the adjoin.
						if (sector_onActiveLayer(next) && sector_isInteractable(next))
						{
							if (texOffset)
							{
								addToAdjoinList({ wall->adjoinId, wall->mirrorId, !dir, { texOffset[0], texOffset[1] } });
							}
							else
							{
								addToAdjoinList({ wall->adjoinId, wall->mirrorId, !dir, {0} });
							}
						}
						if (texOffset)
						{
							if (dir == 1) { texOffset[dir].x -= getWallLength(sector, wall); }
							if (dir == 0) { texOffset[dir].x += getWallLength(sector, wall); }
						}
					}
				}

				nextIndex = dir == 0 ? getRightWall(sector, nextIndex) : getLeftWall(sector, nextIndex);
			}
		}
	}
		
	void selectSimilarWalls(EditorSector* rootSector, s32 wallIndex, HitPart part, bool autoAlign/*=false*/)
	{
		s32 rootWallIndex = wallIndex;
		EditorWall* rootWall = &rootSector->walls[wallIndex];
		s32 texId = -1;
		if (s_sectorDrawMode == SDM_TEXTURED_CEIL || s_sectorDrawMode == SDM_TEXTURED_FLOOR)
		{
			texId = getPartTextureIndex(rootWall, part);
		}

		f32 leftAlign = 0.0f;
		f32 rightAlign = autoAlign ? getWallLength(rootSector, rootWall) : 0.0f;
		Vec2f texOffsetLeft = getPartTextureOffset(rootWall, part);
		Vec2f texOffset[2] = { { texOffsetLeft.x + rightAlign, texOffsetLeft.z }, texOffsetLeft };
		f32 baseHeight = rootSector->floorHeight;
		if (part == HP_TOP)
		{
			EditorSector* next = &s_level.sectors[rootWall->adjoinId];
			baseHeight = next->ceilHeight;
		}

		// Add the matching parts from the start wall.
		if (rootWall->adjoinId < 0)
		{
			s32 curTexId = getPartTextureIndex(rootWall, HP_MID);
			if (texId < 0 || curTexId == texId)
			{
				selection_surface(SA_ADD, rootSector, wallIndex, HP_MID);
			}
		}
		else
		{
			EditorSector* next = &s_level.sectors[rootWall->adjoinId];

			if (next->ceilHeight < rootSector->ceilHeight)
			{
				s32 curTexId = getPartTextureIndex(rootWall, HP_TOP);
				if (texId < 0 || curTexId == texId)
				{
					selection_surface(SA_ADD, rootSector, wallIndex, HP_TOP);
					if (autoAlign) { setPartTextureOffset(rootWall, HP_TOP, { texOffset[1].x, texOffset[1].z + next->ceilHeight - baseHeight }); }
				}
			}
			if (next->floorHeight > rootSector->floorHeight)
			{
				s32 curTexId = getPartTextureIndex(rootWall, HP_BOT);
				if (texId < 0 || curTexId == texId)
				{
					selection_surface(SA_ADD, rootSector, wallIndex, HP_BOT);
					if (autoAlign) { setPartTextureOffset(rootWall, HP_BOT, { texOffset[1].x, texOffset[1].z + rootSector->floorHeight - baseHeight }); }
				}
			}
			if (rootWall->flags[0] & WF1_ADJ_MID_TEX)
			{
				s32 curTexId = getPartTextureIndex(rootWall, HP_MID);
				if (texId < 0 || curTexId == texId)
				{
					selection_surface(SA_ADD, rootSector, wallIndex, HP_MID);
					if (autoAlign) { setPartTextureOffset(rootWall, HP_MID, { texOffset[1].x, texOffset[1].z + rootSector->floorHeight - baseHeight }); }
				}
			}
		}

		s_adjoinList.clear();
		selectSimilarTraverse(rootSector, wallIndex, texId, -1, autoAlign ? texOffset : nullptr, baseHeight);

		// Only traverse adjoins if using textures as a separator.
		// Otherwise we often select too many walls (most or all of the level).
		if (texId >= 0)
		{
			size_t adjoinIndex = 0;
			while (adjoinIndex < s_adjoinList.size())
			{
				EditorSector* sector = &s_level.sectors[s_adjoinList[adjoinIndex].adjoinId];
				Vec2f adjOffset[] = { s_adjoinList[adjoinIndex].offset[0], s_adjoinList[adjoinIndex].offset[1] };
				selectSimilarTraverse(sector, s_adjoinList[adjoinIndex].mirrorId, texId, s_adjoinList[adjoinIndex].side, autoAlign ? adjOffset : nullptr, baseHeight);
				adjoinIndex++;
			}
		}
	}

	// We are looking for other flats of the same height, texture, and part.
	void selectSimilarFlats(EditorSector* rootSector, HitPart part)
	{
		s_searchKey++;
		s_sectorChangeList.clear();
		s_sectorChangeList.push_back(rootSector);

		s32 texId = -1;
		if (s_sectorDrawMode == SDM_TEXTURED_CEIL || s_sectorDrawMode == SDM_TEXTURED_FLOOR)
		{
			texId = part == HP_FLOOR ? rootSector->floorTex.texIndex : rootSector->ceilTex.texIndex;
		}

		while (!s_sectorChangeList.empty())
		{
			EditorSector* sector = s_sectorChangeList.back();
			s_sectorChangeList.pop_back();

			selection_surface(SA_ADD, sector, 0, part);

			const s32 wallCount = (s32)sector->walls.size();
			const EditorWall* wall = sector->walls.data();
			for (s32 w = 0; w < wallCount; w++, wall++)
			{
				if (wall->adjoinId < 0) { continue; }
				EditorSector* next = &s_level.sectors[wall->adjoinId];

				if (next && next->searchKey != s_searchKey)
				{
					next->searchKey = s_searchKey;
					if ((part == HP_FLOOR && next->floorHeight == rootSector->floorHeight) || (part == HP_CEIL && next->ceilHeight == rootSector->ceilHeight))
					{
						bool texMatches = true;
						if (texId >= 0)
						{
							s32 curTexId = part == HP_FLOOR ? next->floorTex.texIndex : next->ceilTex.texIndex;
							texMatches = curTexId == texId;
						}
						if (texMatches)
						{
							s_sectorChangeList.push_back(next);
						}
					}
				}
			}
		}
	}

	void snapSignToCursor(EditorSector* sector, EditorWall* wall, s32 signTexIndex, Vec2f* signOffset)
	{
		EditorTexture* signTex = (EditorTexture*)getAssetData(s_level.textures[signTexIndex].handle);
		if (!signTex) { return; }

		// Center the sign on the mouse cursor.
		const Vec2f* v0 = &sector->vtx[wall->idx[0]];
		const Vec2f* v1 = &sector->vtx[wall->idx[1]];
		Vec2f wallDir = { v1->x - v0->x, v1->z - v0->z };
		wallDir = TFE_Math::normalize(&wallDir);

		f32 uOffset = wall->tex[WP_MID].offset.x;
		f32 vOffset = sector->floorHeight;
		if (wall->adjoinId >= 0)
		{
			const EditorSector* next = &s_level.sectors[wall->adjoinId];
			if (next->floorHeight > sector->floorHeight)
			{
				uOffset = wall->tex[WP_BOT].offset.x;
			}
			else if (next->ceilHeight < sector->ceilHeight)
			{
				uOffset = wall->tex[WP_TOP].offset.x;
				vOffset = next->ceilHeight;
			}
		}

		Vec3f mousePos = s_cursor3d;
		snapToSurfaceGrid(sector, wall, mousePos);

		f32 wallIntersect;
		if (fabsf(wallDir.x) >= fabsf(wallDir.z)) { wallIntersect = (mousePos.x - v0->x) / wallDir.x; }
		else { wallIntersect = (mousePos.z - v0->z) / wallDir.z; }

		signOffset->x = uOffset + wallIntersect - 0.5f*signTex->width / 8.0f;
		signOffset->z = vOffset - mousePos.y + 0.5f*signTex->height / 8.0f;
	}
		
	void splitWall(EditorSector* sector, s32 wallIndex, Vec2f newPos, EditorWall* outWalls[])
	{
		// TODO: Is it worth it to insert the vertex right after i0 as well instead of at the end?
		const s32 newIdx = (s32)sector->vtx.size();
		sector->vtx.push_back(newPos);

		EditorWall* srcWall = &sector->walls[wallIndex];
		const s32 idx[3] = { srcWall->idx[0], newIdx, srcWall->idx[1] };

		// Compute the u offset.
		Vec2f v0 = sector->vtx[srcWall->idx[0]];
		Vec2f v1 = sector->vtx[srcWall->idx[1]];
		Vec2f dir = { v1.x - v0.x, v1.z - v0.z };
		f32 len = sqrtf(dir.x*dir.x + dir.z*dir.z);
		f32 uSplit = 0.0f;
		if (fabsf(dir.x) >= fabsf(dir.z))
		{
			uSplit = (newPos.x - v0.x) / dir.x;
		}
		else
		{
			uSplit = (newPos.z - v0.z) / dir.z;
		}
		uSplit *= len;

		// Copy wall attributes (textures, flags, etc.).
		EditorWall newWall = *srcWall;
		// Then setup indices.
		srcWall->idx[0] = idx[0];
		srcWall->idx[1] = idx[1];
		newWall.idx[0] = idx[1];
		newWall.idx[1] = idx[2];
		for (s32 i = 0; i < WP_COUNT; i++)
		{
			if (newWall.tex[i].texIndex >= 0)
			{
				newWall.tex[i].offset.x += uSplit;
			}
		}

		// Insert the new wall right after the wall being split.
		// This makes the process a bit more complicated, but keeps things clean.
		sector->walls.insert(sector->walls.begin() + wallIndex + 1, newWall);

		// Fixup adjoin mirrors.
		const s32 wallCount = (s32)sector->walls.size();
		const s32 levelSectorCount = (s32)s_level.sectors.size();
		EditorWall* walls = sector->walls.data();
		for (s32 w = wallIndex + 2; w < wallCount; w++)
		{
			if (walls[w].adjoinId >= 0 && walls[w].adjoinId < levelSectorCount && walls[w].mirrorId >= 0)
			{
				EditorSector* next = &s_level.sectors[walls[w].adjoinId];
				if (next->walls[walls[w].mirrorId].adjoinId == sector->id)
				{
					next->walls[walls[w].mirrorId].mirrorId = w;
				}
			}
		}

		// Pointers to the new walls (note that since the wall array was resized, the source wall
		// pointer might have changed as well).
		outWalls[0] = &sector->walls[wallIndex];
		outWalls[1] = &sector->walls[wallIndex + 1];
	}

	bool canSplitWall(EditorSector* sector, s32 wallIndex, Vec2f newPos)
	{
		// If a vertex at the desired position already exists, do not split the wall (early-out).
		const size_t vtxCount = sector->vtx.size();
		const Vec2f* vtx = sector->vtx.data();
		for (size_t v = 0; v < vtxCount; v++)
		{
			if (TFE_Polygon::vtxEqual(&newPos, &vtx[v]))
			{
				return false;
			}
		}
		return true;
	}
}
