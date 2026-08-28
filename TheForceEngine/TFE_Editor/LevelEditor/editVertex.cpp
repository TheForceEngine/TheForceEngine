#include "editVertex.h"
#include "editCommon.h"
#include "editSurface.h"
#include "editTransforms.h"
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

#include <climits>
#include <algorithm>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>

using namespace TFE_Editor;
using namespace TFE_Jedi;

namespace LevelEditor
{
	struct VertexWallGroup
	{
		EditorSector* sector;
		s32 vertexIndex;
		s32 leftWall, rightWall;
	};

	struct StrandResult
	{
		std::vector<s32> strand;
		bool isCircuit;
	};

	static std::vector<VertexWallGroup> s_vertexWallGroups;
	static std::vector<EditorSector> s_sectorSnapshot;
	static std::vector<s32> s_deltaSectors;

	void findHoveredVertexOutside(Vec3f pos, f32 maxDist, bool use3dCheck);
	void selectVerticesToDelete(EditorSector* root, s32 featureIndex, const Vec2f* rootVtx);
	void deleteInvalidSectors(SectorList& list);

	////////////////////////////////////////
	// API
	////////////////////////////////////////
	void handleVertexInsert(Vec2f worldPos, RayHitInfo* info/*=nullptr*/)
	{
		const bool mousePressed = s_singleClick;
		const bool mouseDown = TFE_Input::mouseDown(MouseButton::MBUTTON_LEFT);
		// TODO: Hotkeys.
		const bool insertVtx = isShortcutPressed(SHORTCUT_PLACE);
		const bool snapToGridEnabled = !TFE_Input::keyModDown(KEYMOD_ALT) && s_grid.size > 0.0f;

		// Early out.
		if (!insertVtx || s_editMove || mouseDown || mousePressed) { return; }

		EditorSector* sector = nullptr;
		s32 wallIndex = -1;
		if (s_editMode == LEDIT_WALL)
		{
			HitPart part;
			bool hasHovered = selection_getSurface(SEL_INDEX_HOVERED, sector, wallIndex, &part);
			if (!hasHovered || part == HP_FLOOR || part == HP_CEIL)
			{
				sector = nullptr;
				wallIndex = -1;
			}
		}
		else if (s_editMode == LEDIT_VERTEX)
		{
			// If info is non-null, pull the information from that.
			if (info && info->hitSectorId >= 0)
			{
				sector = &s_level.sectors[info->hitSectorId];
				if (info->hitWallId >= 0 && info->hitPart != HP_FLOOR && info->hitPart != HP_CEIL)
				{
					wallIndex = info->hitWallId;
				}
				else
				{
					HitPart part = HP_NONE;
					edit_checkForWallHit3d(info, sector, wallIndex, part, sector);
					if (!sector || wallIndex < 0 || part == HP_FLOOR || part == HP_CEIL)
					{
						return;
					}
				}
			}
			else if (s_view == EDIT_VIEW_2D)
			{
				sector = edit_getHoverSector2dAtCursor();
				if (sector)
				{
					HitPart part = HP_NONE;
					edit_checkForWallHit2d(worldPos, sector, wallIndex, part, sector);
					if (!sector || wallIndex < 0 || part == HP_FLOOR || part == HP_CEIL)
					{
						return;
					}
				}
			}
		}

		// Return if no wall is found in range.
		if (!sector || wallIndex < 0) { return; }

		EditorWall* wall = &sector->walls[wallIndex];
		Vec2f v0 = sector->vtx[wall->idx[0]];
		Vec2f v1 = sector->vtx[wall->idx[1]];
		Vec2f newPos = v0;

		f32 s = TFE_Polygon::closestPointOnLineSegment(v0, v1, worldPos, &newPos);
		if (snapToGridEnabled)
		{
			s = snapToEdgeGrid(v0, v1, newPos);
		}

		if (s > FLT_EPSILON && s < 1.0f - FLT_EPSILON)
		{
			level_createLevelSectorSnapshotSameAssets(s_sectorSnapshot);
			if (edit_splitWall(sector->id, wallIndex, newPos))
			{
				level_getLevelSnapshotDelta(s_deltaSectors, s_sectorSnapshot);
				if (!s_deltaSectors.empty())
				{
					cmd_sectorSnapshot(LName_InsertVertex, s_deltaSectors);
				}
			}
		}
	}
		
	void findHoveredVertex3d(EditorSector* hoveredSector, RayHitInfo* info)
	{
		const f32 distFromCam = TFE_Math::distance(&info->hitPos, &s_camera.pos);
		const f32 maxDist = distFromCam * 64.0f / f32(s_viewportSize.z);

		// See if we are close enough to "hover" a vertex
		if (hoveredSector)
		{
			const size_t vtxCount = hoveredSector->vtx.size();
			const Vec2f* vtx = hoveredSector->vtx.data();
			const Vec3f* refPos = &info->hitPos;

			const f32 floor = hoveredSector->floorHeight;
			const f32 ceil = hoveredSector->ceilHeight;

			f32 closestDistSq = maxDist * maxDist;
			s32 closestVtx = -1;
			HitPart closestPart = HP_FLOOR;
			Vec3f finalPos;
			for (size_t v = 0; v < vtxCount; v++)
			{
				// Check against the floor and ceiling vertex of each vertex.
				const Vec3f floorVtx = { vtx[v].x, floor, vtx[v].z };
				const Vec3f ceilVtx = { vtx[v].x, ceil,  vtx[v].z };
				const f32 floorDistSq = TFE_Math::distanceSq(&floorVtx, refPos);
				const f32 ceilDistSq = TFE_Math::distanceSq(&ceilVtx, refPos);

				if (floorDistSq < closestDistSq || ceilDistSq < closestDistSq)
				{
					closestDistSq = min(floorDistSq, ceilDistSq);
					closestPart = floorDistSq <= ceilDistSq ? HP_FLOOR : HP_CEIL;
					closestVtx = (s32)v;
					finalPos = (closestPart == HP_FLOOR) ? floorVtx : ceilVtx;
				}
			}

			if (closestVtx >= 0)
			{
				selection_vertex(SA_SET_HOVERED, hoveredSector, closestVtx, closestPart);
				s_hoveredVtxPos = finalPos;
			}
		}
	}

	void findHoveredVertex2d(EditorSector* hoveredSector, Vec2f worldPos)
	{
		if (hoveredSector && (!sector_isInteractable(hoveredSector) || !sector_onActiveLayer(hoveredSector)))
		{
			hoveredSector = nullptr;
			selection_clearHovered();
		}

		// See if we are close enough to "hover" a vertex
		if (hoveredSector)
		{
			const size_t vtxCount = hoveredSector->vtx.size();
			const Vec2f* vtx = hoveredSector->vtx.data();

			f32 closestDistSq = FLT_MAX;
			s32 closestVtx = -1;
			for (size_t v = 0; v < vtxCount; v++, vtx++)
			{
				Vec2f offset = { worldPos.x - vtx->x, worldPos.z - vtx->z };
				f32 distSq = offset.x*offset.x + offset.z*offset.z;
				if (distSq < closestDistSq)
				{
					closestDistSq = distSq;
					closestVtx = (s32)v;
				}
			}

			const f32 maxDist = s_zoom2d * 16.0f;
			if (closestDistSq <= maxDist * maxDist)
			{
				selection_vertex(SA_SET_HOVERED, hoveredSector, closestVtx);
			}
		}
		else
		{
			// Search for other sectors based on the range.
			findHoveredVertexOutside({ worldPos.x, s_grid.height, worldPos.z }, s_zoom2d * 16.0f, false);
		}
	}

	inline s32 getPrevWall(EditorSector* sector, s32 wallId)
	{
		s32 targetVtx = sector->walls[wallId].idx[0];
		for (s32 i = 0; i < sector->walls.size(); i++)
		{
			//if (i == wallId) { continue; }
			if (sector->walls[i].idx[1] == targetVtx)
			{
				return i;
			}
		}

		return -1;
	}

	inline s32 getNextWall(EditorSector* sector, s32 wallId)
	{
		s32 targetVtx = sector->walls[wallId].idx[1];
		for (s32 i = 0; i < sector->walls.size(); i++)
		{
			//if (i == wallId) { continue; }
			if (sector->walls[i].idx[0] == targetVtx)
			{
				return i;
			}
		}

		return -1;
	}

	// Return the loops of vtx that make up exterior and interior walls
	//WallVertMap getSectorWallLoopsOrdered(EditorSector* sector)
	//{
	//	WallVertMap map = { sector, std::vector<s32>(), std::vector<s32>(), std::vector<s32>(), std::vector<s32>() };
	//	map.walls.reserve(sector->walls.size());
	//	map.vtx.reserve(sector->vtx.size());

	//	EditorWall* wall = &sector->walls[0];
	//	map.walls.push_back(0);
	//	map.wallStarts.push_back(0);
	//	map.vtx.push_back(wall->idx[0]);
	//	map.vtx.push_back(wall->idx[1]);
	//	map.vtxStarts.push_back(wall->idx[0]);

	//	for (s32 i = 1; i < sector->walls.size(); i++)
	//	{
	//		s32 nextWall = getNextWall(sector, map.walls.back());
	//		if (nextWall == map.wallStarts.back()) // Enclosed a loop
	//		{
	//			map.vtx.pop_back(); // This will be a dupe
	//			// Is this the exterior wall?
	//			std::vector<Vec2f> verts;
	//			for (const s32 index : map.vtx) { verts.push_back(sector->vtx[index]); }
	//			if (!(TFE_Polygon::signedArea((s32)map.vtx.size(), verts.data()) > 0.0f))
	//			{
	//				// Interior

	//			}

	//		}
	//		else if (nextWall == -1) // Dead end. Loop not enclosed, work back from last start?
	//		{

	//		}
	//		else
	//		{
	//			map.walls.push_back(nextWall);
	//			map.vtx.push_back(sector->walls[nextWall].idx[1]);
	//		}
	//	}

	//	if (getNextWall(sector, map.walls.back()) == map.wallStarts.back()) // The wall loop completes
	//	{
	//		map.vtx.pop_back();
	//	}
	//	else // Dead end, but no more data to work with
	//	{

	//	}
	//	return map;
	//}

	s32 findWallByVertId(EditorSector* sector, s32 vert)
	{
		EditorWall* wall = sector->walls.data();
		for (s32 i = 0; i < sector->walls.size(); i++, wall++)
		{
			if (wall->idx[0] == vert)
			{
				return i;
			}
		}
		return -1;
	}

	s32 findPrevVtx(EditorSector* sector, s32 vert)
	{
		EditorWall* wall = sector->walls.data();
		size_t wallCount = sector->walls.size();
		for (size_t i = 0; i < wallCount; i++, wall++)
		{
			if (wall->idx[1] == vert)
			{
				return wall->idx[0];
			}
		}

		return -1;
	}

	s32 findNextVtx(EditorSector* sector, s32 vert)
	{
		EditorWall* wall = sector->walls.data();
		size_t wallCount = sector->walls.size();
		for (size_t i = 0; i < wallCount; i++, wall++)
		{
			if (wall->idx[0] == vert)
			{
				return wall->idx[1];
			}
		}

		return -1;
	}

	bool isIdInList(std::vector<s32>& list, s32 id)
	{
		for (size_t i = 0; i < list.size(); i++)
		{
			if (list[i] == id) { return true; }
		}
		return false;
	}

	StrandResult createStrandOfLinkedVtx(EditorSector* sector, std::vector<s32>& vertIds, s32 startVert)
	{
		s32 currentVtx = startVert;
		s32 leftmostVtx = startVert;
		s32 rightmostVtx = startVert;

		size_t vtxCount = sector->vtx.size();
		size_t vidCount = vertIds.size();
		size_t wallCount = sector->walls.size();

		StrandResult result = {};
		result.strand.push_back(startVert);

		// Work leftward
		for (size_t left = 0; left < vtxCount; left++)
		{
			leftmostVtx = findPrevVtx(sector, currentVtx);
			if (leftmostVtx == -1)
			{
				// Dead end
				break;
			}
			else if (leftmostVtx == startVert)
			{
				// Completed a circuit. Return early no need to go forward.
				result.isCircuit = true;
				return result;
			}
			else
			{
				if (isIdInList(vertIds, leftmostVtx))
				{
					currentVtx = leftmostVtx;
					result.strand.insert(result.strand.begin(), leftmostVtx);
				}
				else
				{
					break;
				}
			}
		}

		// Work rightward
		currentVtx = rightmostVtx;
		for (size_t right = 0; right < vtxCount; right++)
		{
			rightmostVtx = findNextVtx(sector, currentVtx);
			if (rightmostVtx == -1)
			{
				break;
			}
			else
			{
				if (isIdInList(vertIds, rightmostVtx))
				{
					currentVtx = rightmostVtx;
					result.strand.push_back(rightmostVtx);
				}
				else
				{
					break;
				}
			}
		}

		return result;
	}

	void edit_multidelVerts(EditorSector* sector, std::vector<s32>& vertIds, std::vector<s32>& deletedSectors)
	{
		std::vector<s32> strand;

		//std::vector<s32> extraSectorsForDelete;

		std::vector<IndexPair> vertIdMap;

		for (const s32 id : vertIds) { vertIdMap.push_back({ id, 0 }); }

		// find an available id, search backwards to find the wall that will be re-idx1'd, then forwards to find the end

		while (strand.size() < vertIds.size())
		{
			for (size_t i = 0; i < vertIdMap.size(); i++)
			{
				if (vertIdMap[i].i1 == 0) // We have a free id
				{
					s32 currentVtx = vertIdMap[i].i0;
					StrandResult returnedStrand = createStrandOfLinkedVtx(sector, vertIds, currentVtx);
					strand.insert(strand.end(), returnedStrand.strand.begin(), returnedStrand.strand.end());

					assert(returnedStrand.strand.size() > 0);

					// Update the geometry based on this strand
					// 1. Find the walls that attach to the start and ends of the strands

					if (returnedStrand.isCircuit)
					{
						// If exterior wall, delete sector. Else, degen the walls
						std::vector<Vec2f> vtx;
						for (size_t j = 0; j < returnedStrand.strand.size(); j++)
						{
							vtx.push_back(sector->vtx[returnedStrand.strand[j]]);
						}

						s32 vtxCount = (s32)vtx.size();
						if (!(TFE_Polygon::signedArea(vtxCount, vtx.data()) > 0.0f))
						{
							EditorWall* wall = sector->walls.data();
							size_t wallCount = sector->walls.size();

							for (size_t j = 0; j < wallCount; j++, wall++)
							{
								if (wall->idx[0] == currentVtx)
								{
									s32 currentWall = (s32)j;
									std::vector<s32> wallsToDelete;
									for (size_t k = 0; k < returnedStrand.strand.size(); k++)
									{
										currentWall = getNextWall(sector, currentWall);
										assert(currentWall >= 0);
										wallsToDelete.push_back(currentWall);
									}

									for (const s32 id : wallsToDelete) { sector->walls[id].idx[0] = sector->walls[id].idx[1]; }

									break;
								}
							}
						}
						else
						{
							deletedSectors.push_back(sector->id);
						}
					}
					else
					{
						s32 startWall = -1;
						s32 alternateWall = -1;
						s32 endWall = -1;
						bool deadEndStart = false;
						bool deadEndEnd = false;
						size_t wallCount = sector->walls.size();
						EditorWall* wall = sector->walls.data();
						for (size_t start = 0; start < wallCount; start++, wall++)
						{
							if (wall->idx[0] == returnedStrand.strand.front())
							{
								alternateWall = (s32)start;
							}
							if (wall->idx[1] == returnedStrand.strand.front())
							{
								startWall = (s32)start;
								break;
							}
						}
						if (startWall == -1)
						{
							// Vert is strand start deadend
							deadEndStart = true;
							startWall = alternateWall;
						}

						wall = sector->walls.data();
						for (size_t end = 0; end < wallCount; end++, wall++)
						{
							if (wall->idx[1] == returnedStrand.strand.back())
							{
								alternateWall = (s32)end;
							}
							if (wall->idx[0] == returnedStrand.strand.back())
							{
								endWall = (s32)end;
								break;
							}
						}
						if (endWall == -1)
						{
							// Vert is strand end deadend
							deadEndEnd = true;
						}

						// 1a test if the strand is a circuit
						if (!deadEndStart && !deadEndEnd)
						{
							if (sector->walls[startWall].idx[0] == sector->walls[endWall].idx[1])
							{
								LE_WARNING("Circuit");
							}
						}

						// 2. Make any interim walls degenerate. Compile a list to avoid editing idx data while walking
						s32 currentWall = startWall;
						std::vector<s32> removeWalls;
						for (size_t w = 0; w < returnedStrand.strand.size() - 1; w++)
						{
							currentWall = getNextWall(sector, currentWall);
							if (currentWall >= 0) { removeWalls.push_back(currentWall); }
						}
						for (const s32 id : removeWalls)
						{
							sector->walls[id].idx[0] = sector->walls[id].idx[1];
						}

						// 3. Connect start and end walls dependant on deadends detected
						if (deadEndEnd || deadEndStart)
						{
							// No end wall to stitch to, so kill startwall
							// or start wall is part of the cull
							sector->walls[startWall].idx[0] = sector->walls[startWall].idx[1];
						}
						else
						{
							sector->walls[startWall].idx[1] = sector->walls[endWall].idx[1];
							sector->walls[endWall].idx[1] = sector->walls[endWall].idx[0];
						}
					}

					// 4. mark found ids in the map
					for (size_t strandIndex = 0; strandIndex < returnedStrand.strand.size(); strandIndex++)
					{
						for (size_t mapIndex = 0; mapIndex < vertIdMap.size(); mapIndex++)
						{
							if (returnedStrand.strand[strandIndex] == vertIdMap[mapIndex].i0) { vertIdMap[mapIndex].i1 = 1; }
						}
					}

				}
			}
		}
	}

	// Delete selected vertices 
	void edit_deleteVertices()
	{
		// Save a snapshot *before* delete.
		level_createLevelSectorSnapshotSameAssets(s_sectorSnapshot);

		std::vector<s32> deletedSectors;
		std::unordered_map<s32, std::vector<s32>> sectorVtxMap;

		s32 featureIndex;
		FeatureId* id;
		u32 vtxCount = selection_getList(id, SEL_VERTEX);

		// Fill the hashmap with sectors and feature (vtx) ids.
		for (size_t i = 0; i < vtxCount; i++, id++)
		{
			EditorSector* sector = unpackFeatureId(*id, &featureIndex);
			if (sectorVtxMap.count(sector->id) > 0)
			{
				sectorVtxMap.at(sector->id).push_back(featureIndex);
			}
			else
			{
				sectorVtxMap.emplace(sector->id, std::vector<s32>());
				sectorVtxMap.at(sector->id).push_back(featureIndex);
			}
		}


		for (auto& entry : sectorVtxMap)
		{
			s32 sectorId = entry.first;
			if (entry.second.size() > s_level.sectors[sectorId].vtx.size() - 2)
			{
				// Two or less verts would be left in the sector. Delete it.
				deletedSectors.push_back(sectorId);
			}
		}

		std::vector<s32> cleanSectorList;
		std::vector<EditorSector*> changedSectorList;
		// Clear out any keys for deleted sectors
		for (const s32 id : deletedSectors) { sectorVtxMap.erase(id); }

		// Test some deletes
		for (auto entry : sectorVtxMap)
		{
			EditorSector* sector = &s_level.sectors[entry.first];
			// WallVertMap wvMap = getSectorWallLoopsOrdered(sector);
			if (entry.second.size() > 0)
			{
				edit_multidelVerts(sector, entry.second, deletedSectors);
				cleanSectorList.push_back(sector->id);
				changedSectorList.push_back(sector);
			}
		}

		edit_cleanSectorList(cleanSectorList, false);

		edit_deleteSectors(deletedSectors, false);

		// Remove any invalid sectors.
		deleteInvalidSectors(changedSectorList);

		// When deleting features, we need to clear out the selections.
		edit_clearSelections();

		// Get the sectors that changed due to deletion and build a limited snapshot from that.
		level_getLevelSnapshotDelta(s_deltaSectors, s_sectorSnapshot);
		cmd_sectorSnapshot(LName_DeleteVertex, s_deltaSectors);
	}
	
	// Delete a vertex.
	void edit_deleteVertex(s32 sectorId, s32 vertexIndex, u32 nameId)
	{
		// Save a snapshot *before* delete.
		level_createLevelSectorSnapshotSameAssets(s_sectorSnapshot);

		// Then get the position.
		EditorSector* sector = &s_level.sectors[sectorId];
		const Vec2f* pos = &sector->vtx[vertexIndex];

		// Gather a list of walls and vertices.
		s_vertexWallGroups.clear();
		selectVerticesToDelete(sector, vertexIndex, pos);

		s_searchKey++;
		s_sectorChangeList.clear();
				
		const s32 wallGroupCount = (s32)s_vertexWallGroups.size();
		VertexWallGroup* group = s_vertexWallGroups.data();
		for (s32 i = 0; i < wallGroupCount; i++, group++)
		{
			EditorSector* sector = group->sector;
			if (sector->searchKey != s_searchKey)
			{
				sector->searchKey = s_searchKey;
				s_sectorChangeList.push_back(sector);
			}

			EditorWall* left  = group->leftWall  >= 0 ? &sector->walls[group->leftWall]  : nullptr;
			EditorWall* right = group->rightWall >= 0 ? &sector->walls[group->rightWall] : nullptr;
			if (left && right)
			{
				// Merge the left and right walls.
				left->idx[1] = right->idx[1];
				// Make the right wall degenerate - this will be removed during the "clean" pass.
				right->idx[0] = right->idx[1];
			}
			else if (left)
			{
				// Make the wall degenerate - this will be removed during the "clean" pass.
				left->idx[1] = left->idx[0];
			}
			else if (right)
			{
				// Make the wall degenerate - this will be removed during the "clean" pass.
				right->idx[1] = right->idx[0];
			}
		}

		// Clean sectors - this will remove collapsed walls and clean up the sectors.
		std::vector<s32> selectedSectors;
		const s32 count = (s32)s_sectorChangeList.size();
		EditorSector** sectorList = s_sectorChangeList.data();
		for (s32 i = 0; i < count; i++)
		{
			selectedSectors.push_back(sectorList[i]->id);
		}
		edit_cleanSectorList(selectedSectors);

		// Remove any invalid sectors.
		deleteInvalidSectors(s_sectorChangeList);

		// When deleting features, we need to clear out the selections.
		edit_clearSelections();

		// Get the sectors that changed due to deletion and build a limited snapshot from that.
		level_getLevelSnapshotDelta(s_deltaSectors, s_sectorSnapshot);
		cmd_sectorSnapshot(nameId, s_deltaSectors);
	}

	void vertexComputeDragSelect()
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

				const size_t vtxCount = sector->vtx.size();
				const Vec2f* vtx = sector->vtx.data();
				for (size_t v = 0; v < vtxCount; v++, vtx++)
				{
					if (vtx->x >= aabb[0].x && vtx->x < aabb[1].x && vtx->z >= aabb[0].z && vtx->z < aabb[1].z)
					{
						selection_vertex(s_dragSelect.mode == DSEL_REM ? SA_REMOVE : SA_ADD, sector, (s32)v);
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
				const size_t vtxCount = sector->vtx.size();
				const Vec2f* vtx = sector->vtx.data();
				for (size_t v = 0; v < vtxCount; v++, vtx++)
				{
					bool insideFloor = true;
					bool insideCeil = false;
					for (s32 p = 0; p < 6; p++)
					{
						if (plane[p].x*vtx->x + plane[p].y*sector->floorHeight + plane[p].z*vtx->z + plane[p].w > 0.0f)
						{
							insideFloor = false;
							break;
						}
					}
					if (!insideFloor)
					{
						insideCeil = true;
						for (s32 p = 0; p < 6; p++)
						{
							if (plane[p].x*vtx->x + plane[p].y*sector->ceilHeight + plane[p].z*vtx->z + plane[p].w > 0.0f)
							{
								insideCeil = false;
								break;
							}
						}
					}

					if (insideFloor || insideCeil)
					{
						selection_vertex(s_dragSelect.mode == DSEL_REM ? SA_REMOVE : SA_ADD, sector, (s32)v, insideFloor ? HP_FLOOR : HP_CEIL);
					}
				}
			}
		}
	}

	/////////////////////////////////////////
	// Vertex transforms
	/////////////////////////////////////////
	void edit_setVertexPos(FeatureId id, Vec2f pos)
	{
		s32 featureIndex;
		EditorSector* sector = unpackFeatureId(id, &featureIndex);
		sector->vtx[featureIndex] = pos;

		sectorToPolygon(sector);
	}

	void edit_moveSelectedVertices(Vec2f delta)
	{
		s_searchKey++;
		s_sectorChangeList.clear();

		EditorSector* sector = nullptr;
		s32 featureIndex = -1;
		s32 count = (s32)selection_getCount(SEL_VERTEX);
		for (s32 i = 0; i < count; i++)
		{
			selection_getVertex(i, sector, featureIndex);

			sector->vtx[featureIndex].x += delta.x;
			sector->vtx[featureIndex].z += delta.z;

			// Only update a sector once.
			if (sector->searchKey != s_searchKey)
			{
				s_sectorChangeList.push_back(sector);
				sector->searchKey = s_searchKey;
			}
		}

		// Update sectors after changes.
		const size_t changeCount = s_sectorChangeList.size();
		EditorSector** list = s_sectorChangeList.data();
		for (size_t i = 0; i < changeCount; i++)
		{
			EditorSector* sector = list[i];
			sectorToPolygon(sector);
		}
	}

	void edit_moveVertices(Vec2f worldPos2d)
	{
		// Snap to the grid.
		worldPos2d = snapToGridOrGuidelines2d(worldPos2d);

		// Overall movement since mousedown, for the history.
		if (!s_moveStarted)
		{
			EditorSector* sector = nullptr;
			s32 vertexIndex = -1;
			if (!selection_getVertex(SEL_INDEX_HOVERED, sector, vertexIndex))
			{
				return;
			}

			s_moveStarted = true;
			s_moveStartPos = sector->vtx[vertexIndex];
			s_moveVertexPivot = &sector->vtx[vertexIndex];
			edit_setTransformAnchor({ s_moveStartPos.x, s_cursor3d.y, s_moveStartPos.z });
		}
		// Current movement.
		u32 moveAxis = edit_getMoveAxis();
		Vec2f delta = { worldPos2d.x - s_moveVertexPivot->x, worldPos2d.z - s_moveVertexPivot->z };
		const Vec3f pos = edit_getTransformPos();
		Vec3f transPos = { worldPos2d.x, pos.y, worldPos2d.z };
				
		if (s_view == EDIT_VIEW_3D)
		{
			// Avoid singularities.
			const Vec2f cameraDelta = { transPos.x - s_camera.pos.x, transPos.z - s_camera.pos.z };
			if (cameraDelta.x*s_rayDir.x + cameraDelta.z*s_rayDir.z < 0.0f || (s_cursor3d.x == 0.0f && s_cursor3d.z == 0.0f))
			{
				// Do not allow the object to be moved behind the camera.
				return;
			}
		}

		if (!(moveAxis & AXIS_X))
		{
			delta.x = 0.0f;
			transPos.x = pos.x;
		}
		if (!(moveAxis & AXIS_Z))
		{
			delta.z = 0.0f;
			transPos.z = pos.z;
		}

		edit_setTransformPos(transPos);
		edit_moveSelectedVertices(delta);

		s_curVtxPos.x = s_moveVertexPivot->x;
		s_curVtxPos.z = s_moveVertexPivot->z;
	}

	////////////////////////////////////////
	// Internal
	////////////////////////////////////////
	void findHoveredVertexOutside(Vec3f pos, f32 maxDist, bool use3dCheck)
	{
		// TODO: Spatial partitioning.
		const s32 count = (s32)s_level.sectors.size();
		EditorSector* sector = s_level.sectors.data();

		f32 minDist = maxDist * maxDist;
		s32 vtxIndex = -1;
		EditorSector* vtxSector = nullptr;

		for (s32 i = 0; i < count; i++, sector++)
		{
			if (!sector_isInteractable(sector) || !sector_onActiveLayer(sector)) { continue; }
			if (use3dCheck && (pos.y < sector->bounds[0].y || pos.y > sector->bounds[1].y)) { continue; }

			if (pos.x + maxDist < sector->bounds[0].x || pos.x - maxDist > sector->bounds[1].x ||
				pos.z + maxDist < sector->bounds[0].z || pos.z - maxDist > sector->bounds[1].z)
			{
				continue;
			}

			// position potentially inside of sector, check the vertices...
			const s32 vtxCount = (s32)sector->vtx.size();
			const Vec2f* vtx = sector->vtx.data();
			for (s32 v = 0; v < vtxCount; v++)
			{
				Vec2f offset = { vtx[v].x - pos.x, vtx[v].z - pos.z };
				f32 distSq = offset.x*offset.x + offset.z*offset.z;
				if (distSq < minDist)
				{
					minDist = distSq;
					vtxSector = sector;
					vtxIndex = v;
				}
			}
		}

		if (vtxSector && vtxIndex >= 0)
		{
			selection_vertex(SA_SET_HOVERED, vtxSector, vtxIndex);
			s_hoveredVtxPos = { vtxSector->vtx[vtxIndex].x, vtxSector->floorHeight, vtxSector->vtx[vtxIndex].z };
		}
	}

	void selectVerticesToDelete(EditorSector* root, s32 featureIndex, const Vec2f* rootVtx)
	{
		s_searchKey++;

		// Which walls share the vertex?
		std::vector<EditorSector*> stack;

		root->searchKey = s_searchKey;
		{
			const s32 wallCount = (s32)root->walls.size();
			EditorWall* wall = root->walls.data();
			s32 leftIdx = -1, rightIdx = -1;
			for (s32 w = 0; w < wallCount; w++, wall++)
			{
				if (wall->idx[0] == featureIndex || wall->idx[1] == featureIndex)
				{
					if (wall->idx[0] == featureIndex) { rightIdx = w; }
					else { leftIdx = w; }

					if (wall->adjoinId >= 0)
					{
						EditorSector* next = &s_level.sectors[wall->adjoinId];
						if (next->searchKey != s_searchKey)
						{
							stack.push_back(next);
							next->searchKey = s_searchKey;
						}
					}
				}
			}

			if (leftIdx >= 0 || rightIdx >= 0)
			{
				s_vertexWallGroups.push_back({ root, featureIndex, leftIdx, rightIdx });
			}
		}

		while (!stack.empty())
		{
			EditorSector* sector = stack.back();
			stack.pop_back();

			const s32 wallCount = (s32)sector->walls.size();
			EditorWall* wall = sector->walls.data();
			s32 leftIdx = -1, rightIdx = -1;
			s32 vtxId = -1;
			for (s32 w = 0; w < wallCount; w++, wall++)
			{
				s32 i = -1;
				if (TFE_Polygon::vtxEqual(rootVtx, &sector->vtx[wall->idx[0]]))
				{
					i = 0;
					rightIdx = w;
					vtxId = wall->idx[0];
				}
				else if (TFE_Polygon::vtxEqual(rootVtx, &sector->vtx[wall->idx[1]]))
				{
					i = 1;
					leftIdx = w;
					vtxId = wall->idx[1];
				}

				if (i >= 0)
				{
					// Add the next sector to the stack, if it hasn't already been processed.
					EditorSector* next = wall->adjoinId < 0 ? nullptr : &s_level.sectors[wall->adjoinId];
					if (next && next->searchKey != s_searchKey)
					{
						stack.push_back(next);
						next->searchKey = s_searchKey;
					}
				}
			}

			if ((leftIdx >= 0 || rightIdx >= 0) && vtxId >= 0)
			{
				s_vertexWallGroups.push_back({ sector, vtxId, leftIdx, rightIdx });
			}
		}
	}

	bool sortBySectorId(EditorSector*& a, EditorSector*& b)
	{
		return a->id > b->id;
	}

	void deleteInvalidSectors(SectorList& list)
	{
		// Sort from highest id to lowest.
		std::sort(list.begin(), list.end(), sortBySectorId);

		// Then loop through the list and delete invalid sectors.
		//   * Adjoins referencing a deleted sector must be removed.
		//   * Adjoins referencing sectors by a higher ID must be modified.
		//   * Deleted sectors must be removed from the list.
		std::vector<s32> itemsToDelete;

		const s32 count = (s32)list.size();
		for (s32 i = 0; i < count; i++)
		{
			EditorSector* sector = list[i];

			// *TODO*
			// Is the sector degenerate (aka, has no area)?
			if (sector->vtx.size() >= 3) { continue; }

			// Get the ID and then erase it from the level.
			s32 delId = sector->id;
			s_level.sectors.erase(s_level.sectors.begin() + delId);

			// Update Sector IDs
			const s32 levSectorCount = (s32)s_level.sectors.size();
			sector = s_level.sectors.data() + delId;
			for (s32 s = delId; s < levSectorCount; s++, sector++)
			{
				sector->id--;
			}

			// Update Adjoins.
			sector = s_level.sectors.data();
			for (s32 s = 0; s < levSectorCount; s++, sector++)
			{
				const s32 wallCount = (s32)sector->walls.size();
				EditorWall* wall = sector->walls.data();
				for (s32 w = 0; w < wallCount; w++, wall++)
				{
					if (wall->adjoinId == delId)
					{
						// Remove adjoin since the sector no longer exists.
						wall->adjoinId = -1;
						wall->mirrorId = -1;
					}
					else if (wall->adjoinId > delId)
					{
						// All of the sector IDs after delID shifted down by 1.
						wall->adjoinId--;
					}
				}
			}

			// Add to the erase list.
			itemsToDelete.push_back(i);
		}

		// Erase sectors from the list since later fixup is not required.
		const s32 delCount = (s32)itemsToDelete.size();
		const s32* delList = itemsToDelete.data();
		for (s32 i = delCount - 1; i >= 0; i--)
		{
			list.erase(list.begin() + delList[i]);
		}
	}
}
