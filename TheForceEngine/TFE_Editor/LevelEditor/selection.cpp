#include "levelEditor.h"
#include "levelEditorData.h"
#include "editCommon.h"
#include "editTransforms.h"
#include "selection.h"
#include "sharedState.h"
#include "infoPanel.h"

#include <unordered_map>

using namespace TFE_Editor;

namespace LevelEditor
{
	SelectionList s_selectionList2[SEL_COUNT];
	SelectionList s_savedSelections[SEL_GEO_COUNT];
	SelectionListId s_currentSelection = SEL_VERTEX;
	FeatureId s_hovered = FEATUREID_NULL;
	bool s_derivedBuildNeeded = false;

	bool selection_insertVertex(FeatureId id, Vec2f value, EditorSector* root = nullptr, HitPart part = HP_FLOOR);
	bool selection_removeVertex(FeatureId id, Vec2f value);
	bool selection_isVertexInSet(FeatureId id);
	bool selection_insertFeatureId(SelectionList& list, FeatureId id);
	bool selection_removeFeatureId(SelectionList& list, FeatureId id);
	bool selection_isFeatureIdInSet(SelectionList& list, FeatureId id, s32* index = nullptr);
	bool selection_featureId(SelectAction action, FeatureId id);
	void selection_buildDerived();
	void selection_derivedBuildNeeded();
	FeatureId selection_getFeatureId(s32 index, SelectionListId listId);

	// General Interface
	void selection_clear(u32 selections, bool clearDragSelect)
	{
		for (s32 i = 0; i < SEL_COUNT; i++)
		{
			if ((1 << i) & selections)
			{
				s_selectionList2[i].clear();
			}
		}
		if (clearDragSelect)
		{
			s_dragSelect.active = false;
		}
		edit_setTransformChange();
		infoPanel_clearSelection();
	}

	void selection_setCurrent(SelectionListId id)
	{
		assert(id < SEL_CURRENT);
		selection_clearHovered();
		s_currentSelection = id;
		edit_setTransformChange();
		infoPanel_clearSelection();
	}

	SelectionListId selection_getCurrent()
	{
		return s_currentSelection;
	}

	void selection_clearHovered()
	{
		s_hovered = FEATUREID_NULL;
	}

	// Is the feature valid in the current selection mode?
	bool selection_isFeatureValidInMode(FeatureId featureId)
	{
		s32 featureIndex = -1;
		HitPart part = HP_NONE;
		EditorSector* sector = unpackFeatureId(featureId, &featureIndex, (s32*)&part);

		switch (s_currentSelection)
		{
			case SEL_VERTEX:
			{
				if (!sector || featureIndex < 0 || featureIndex >= (s32)sector->vtx.size()) { return false; }
			} break;
			case SEL_SURFACE:
			{
				if (!sector) { return false; }
				if (part != HP_FLOOR && part != HP_CEIL) 
				{
					if (featureIndex < 0 || featureIndex >= (s32)sector->walls.size())
					{
						return false;
					}
				}
			} break;
			case SEL_SECTOR:
			{
				if (!sector) { return false; }
			} break;
			case SEL_ENTITY:
			{
				if (!sector || featureIndex < 0 || featureIndex >= (s32)sector->obj.size())
				{
					return false;
				}
			} break;
			case SEL_GUIDELINE:
			{
				if (featureIndex < 0 || featureIndex >= (s32)s_level.guidelines.size())
				{
					return false;
				}
			} break;
			case SEL_NOTE:
			{
				if (featureIndex < 0 || featureIndex >= (s32)s_level.notes.size())
				{
					return false;
				}
			} break;
		};
		return true;
	}

	bool selection_hasHovered()
	{
		// Check to see if the hovered feature is NULL.
		if (s_hovered == FEATUREID_NULL) { return false; }
		// Verify that the hovered feature is still valid.
		if (!selection_isFeatureValidInMode(s_hovered))
		{
			// And if not, clear the hovered feature.
			selection_clearHovered();
			return false;
		}
		// Has valid hovered feature for the selection mode.
		return true;
	}

	bool selection_hasSelection(SelectionListId id)
	{
		return !s_selectionList2[id >= SEL_CURRENT ? s_currentSelection : id].empty();
	}

	void selection_saveGeoSelections()
	{
		for (s32 i = 0; i < SEL_GEO_COUNT; i++)
		{
			s_savedSelections[i] = s_selectionList2[i];
		}
	}

	void selection_restoreGeoSelections()
	{
		for (s32 i = 0; i < SEL_GEO_COUNT; i++)
		{
			s_selectionList2[i] = s_savedSelections[i];
			s_savedSelections[i].clear();
		}
	}
		
	// Selection interface.
	bool selection_vertex(SelectAction action, EditorSector* sector, s32 index, HitPart part, u32 flags)
	{
		if (!sector || index < 0 || index >= (s32)sector->vtx.size()) { return false; }

		bool buildDerived = false;
		bool actionDone = false;
		FeatureId id = createFeatureId(sector, index, part);
		switch (action)
		{
			case SA_SET:
			{
				s_selectionList2[SEL_VERTEX].clear();
				actionDone = selection_insertVertex(id, sector->vtx[index], sector, part);
				buildDerived = actionDone;
				infoPanel_clearSelection();
			} break;
			case SA_ADD:
			{
				actionDone = selection_insertVertex(id, sector->vtx[index], sector, part);
				buildDerived = actionDone;
				infoPanel_clearSelection();
			} break;
			case SA_REMOVE:
			{
				actionDone = selection_removeVertex(id, sector->vtx[index]);
				buildDerived = actionDone;
				infoPanel_clearSelection();
			} break;
			case SA_TOGGLE:
			{
				if (selection_isVertexInSet(id))
				{
					actionDone = selection_removeVertex(id, sector->vtx[index]);
				}
				else
				{
					actionDone = selection_insertVertex(id, sector->vtx[index], sector, part);
				}
				buildDerived = actionDone;
				infoPanel_clearSelection();
			} break;
			case SA_CHECK_INCLUSION:
			{
				actionDone = selection_isVertexInSet(id);
			} break;
			case SA_SET_HOVERED:
			{
				s_hovered = id;
				actionDone = true;
			} break;
		}

		if (buildDerived)
		{
			edit_setTransformChange();
			selection_derivedBuildNeeded();
		}
		return actionDone;
	}
				
	bool selection_surface(SelectAction action, EditorSector* sector, s32 index, HitPart part, u32 flags)
	{
		if (!sector) { return false; }
		if (part != HP_FLOOR && part != HP_CEIL && (index < 0 || index >= (s32)sector->walls.size())) { return false; }

		const FeatureId id = createFeatureId(sector, index, (s32)part);
		return selection_featureId(action, id);
	}

	bool selection_entity(SelectAction action, EditorSector* sector, s32 index, u32 flags)
	{
		// TODO: Handle entity's outside of sectors.
		if (!sector) { return false; }
		const FeatureId id = createFeatureId(sector, index);
		return selection_featureId(action, id);
	}

	bool selection_sector(SelectAction action, EditorSector* sector, u32 flags)
	{
		if (!sector) { return false; }
		const FeatureId id = createFeatureId(sector);
		return selection_featureId(action, id);
	}

	bool selection_guideline(SelectAction action, Guideline* guideline, u32 flags)
	{
		if (!guideline) { return false; }
		const FeatureId id = createFeatureId(nullptr, guideline->id);
		return selection_featureId(action, id);
	}

	bool selection_levelNote(SelectAction action, LevelNote* note, u32 flags)
	{
		if (!note) { return false; }
		const FeatureId id = createFeatureId(nullptr, note->id);
		return selection_featureId(action, id);
	}

	// Read interface.
	// Pass in SEL_GET_HOVERED for the index to get the hovered feature (or false is none).
	u32 selection_getCount(SelectionListId id)
	{
		if (id != SEL_CURRENT && id != s_currentSelection) { selection_buildDerived(); }
		return (u32)s_selectionList2[id >= SEL_CURRENT ? s_currentSelection : id].size();
	}
		
	bool selection_getVertex(s32 index, EditorSector*& sector, s32& featureIndex, HitPart* part, bool* isOverlapped)
	{
		if (s_currentSelection != SEL_VERTEX) { selection_buildDerived(); }
		const FeatureId id = selection_getFeatureId(index, SEL_VERTEX);
		if (id == FEATUREID_NULL) { return false; }
		// Unpack the feature ID based on the type.
		sector = unpackFeatureId(id, &featureIndex, (s32*)part, isOverlapped);
		return true;
	}

	bool selection_getSurface(s32 index, EditorSector*& sector, s32& featureIndex, HitPart* part, bool* isOverlapped)
	{
		if (s_currentSelection != SEL_SURFACE) { selection_buildDerived(); }
		const FeatureId id = selection_getFeatureId(index, SEL_SURFACE);
		if (id == FEATUREID_NULL) { return false; }
		// Unpack the feature ID based on the type.
		sector = unpackFeatureId(id, &featureIndex, (s32*)part, isOverlapped);
		return true;
	}

	bool selection_getEntity(s32 index, EditorSector*& sector, s32& featureIndex)
	{
		const FeatureId id = selection_getFeatureId(index, SEL_ENTITY);
		if (id == FEATUREID_NULL) { return false; }
		// Unpack the feature ID based on the type.
		sector = unpackFeatureId(id, &featureIndex);
		return true;
	}

	bool selection_getSector(s32 index, EditorSector*& sector)
	{
		if (s_currentSelection != SEL_SECTOR) { selection_buildDerived(); }
		const FeatureId id = selection_getFeatureId(index, SEL_SECTOR);
		if (id == FEATUREID_NULL) { return false; }
		// Unpack the feature ID based on the type.
		sector = unpackFeatureId(id);
		return true;
	}

	bool selection_getGuideline(s32 index, Guideline*& guideline)
	{
		const FeatureId id = selection_getFeatureId(index, SEL_GUIDELINE);
		if (id == FEATUREID_NULL) { return false; }
		// Unpack the feature ID based on the type.
		s32 featureIndex = -1;
		unpackFeatureId(id, &featureIndex);
		if (featureIndex >= 0 && featureIndex < (s32)s_level.guidelines.size())
		{
			guideline = &s_level.guidelines[featureIndex];
			return true;
		}
		return false;
	}

	bool selection_getLevelNote(s32 index, LevelNote*& levelNote)
	{
		const FeatureId id = selection_getFeatureId(index, SEL_NOTE);
		if (id == FEATUREID_NULL) { return false; }
		// Unpack the feature ID based on the type.
		s32 featureIndex = -1;
		unpackFeatureId(id, &featureIndex);
		if (featureIndex >= 0 && featureIndex < (s32)s_level.notes.size())
		{
			levelNote = &s_level.notes[featureIndex];
			return true;
		}
		return false;
	}

	bool selection_featureInCurrentSelection(EditorSector* sector, s32 featureIndex/* = -1*/, HitPart part/* = HP_NONE*/)
	{
		switch (s_currentSelection)
		{
			case SEL_VERTEX:
			{
				return selection_vertex(SA_CHECK_INCLUSION, sector, featureIndex);
			} break;
			case SEL_SURFACE:
			{
				return selection_surface(SA_CHECK_INCLUSION, sector, featureIndex, part);
			} break;
			case SEL_SECTOR:
			{
				return selection_sector(SA_CHECK_INCLUSION, sector);
			} break;
			case SEL_ENTITY:
			{
				return selection_entity(SA_CHECK_INCLUSION, sector, featureIndex);
			} break;
			case SEL_GUIDELINE:
			{
				return featureIndex >= 0 && selection_guideline(SA_CHECK_INCLUSION, &s_level.guidelines[featureIndex]);
			} break;
			case SEL_NOTE:
			{
				return featureIndex >= 0 && selection_levelNote(SA_CHECK_INCLUSION, &s_level.notes[featureIndex]);
			} break;
		};
		return false;
	}

	bool selection_featureHovered(EditorSector* sector, s32 featureIndex/* = -1*/, HitPart part/* = HP_NONE*/)
	{
		if (!selection_hasHovered()) { return false; }

		s32 hoveredFeatureIndex = -1;
		HitPart hoveredPart = HP_NONE;
		EditorSector* hoveredSector = unpackFeatureId(s_hovered, &hoveredFeatureIndex, (s32*)&hoveredPart);

		switch (s_currentSelection)
		{
			case SEL_VERTEX:
			{
				return sector == hoveredSector && featureIndex == hoveredFeatureIndex;
			} break;
			case SEL_SURFACE:
			{
				return sector == hoveredSector && featureIndex == hoveredFeatureIndex && (part == hoveredPart || part == HP_NONE);
			} break;
			case SEL_SECTOR:
			{
				return sector == hoveredSector;
			} break;
			case SEL_ENTITY:
			{
				return sector == hoveredSector && featureIndex == hoveredFeatureIndex;
			} break;
			case SEL_GUIDELINE:
			{
				return featureIndex == hoveredFeatureIndex;
			} break;
			case SEL_NOTE:
			{
				return featureIndex == hoveredFeatureIndex;
			} break;
		};
		return false;
	}

	u32 selection_getList(FeatureId*& list, SelectionListId id)
	{
		if (id != SEL_CURRENT && id != s_currentSelection) { selection_buildDerived(); }
		SelectionList& selList = s_selectionList2[id >= SEL_CURRENT ? s_currentSelection : id];
		u32 count = (u32)selList.size();
		list = count > 0 ? selList.data() : nullptr;
		return count;
	}
		
	// TODO: Finish
	bool selection_get(s32 index, EditorSector*& sector, s32& featureIndex, HitPart* part)
	{
		bool result = false;
		sector = nullptr;
		featureIndex = -1;
		if (part) { *part = HP_FLOOR; }
		switch (s_currentSelection)
		{
			case SEL_VERTEX:
			{
				result = selection_getVertex(index, sector, featureIndex, part);
			} break;
			case SEL_SURFACE:
			{
				result = selection_getSurface(index, sector, featureIndex, part);
			} break;
			case SEL_SECTOR:
			{
				result = selection_getSector(index, sector);
			} break;
			case SEL_ENTITY:
			{
				result = selection_getEntity(index, sector, featureIndex);
			} break;
		}
		return result;
	}

	bool selection_action(SelectAction action, EditorSector* sector, s32 featureIndex, HitPart part)
	{
		bool result = false;
		switch (s_currentSelection)
		{
			case SEL_VERTEX:
			{
				result = selection_vertex(action, sector, featureIndex, part);
			} break;
			case SEL_SURFACE:
			{
				result = selection_surface(action, sector, featureIndex, part);
			} break;
			case SEL_SECTOR:
			{
				result = selection_sector(action, sector);
			} break;
			case SEL_ENTITY:
			{
				result = selection_entity(action, sector, featureIndex);
			} break;
		}
		return result;
	}

	////////////////////////////////////////////////////////////
	// Internal
	////////////////////////////////////////////////////////////
	bool selection_insertVertex(FeatureId id, Vec2f value, EditorSector* root/*=nullptr*/, HitPart part/*=HP_FLOOR*/)
	{
		// Insert the vertex if not found.
		const s32 count = (s32)s_selectionList2[SEL_VERTEX].size();
		const FeatureId* featureId = s_selectionList2[SEL_VERTEX].data();
		s32 matchId = -1;
		for (s32 i = 0; i < count; i++)
		{
			if (featuresEqualNoData(featureId[i], id)) { return false; }

			s32 index;
			EditorSector* sector = unpackFeatureId(featureId[i], &index);
			const Vec2f& srcValue = sector->vtx[index];

			if (TFE_Polygon::vtxEqual(&value, &srcValue))
			{
				// Keep looking after this, the feature may still be in here...
				matchId = i;
			}
		}

		id = setIsOverlapped(id, matchId >= 0);
		s_selectionList2[SEL_VERTEX].push_back(id);

		// If root is non-null, then start from this sector and include potential overlaps from other sectors.
		if (root)
		{
			s_searchKey++;

			// Which walls share the vertex?
			SectorList& stack = s_sectorChangeList;
			stack.clear();

			s32 featureIndex;
			unpackFeatureId(id, &featureIndex);

			root->searchKey = s_searchKey;
			{
				const size_t wallCount = root->walls.size();
				EditorWall* wall = root->walls.data();
				for (size_t w = 0; w < wallCount; w++, wall++)
				{
					if ((wall->idx[0] == featureIndex || wall->idx[1] == featureIndex) && wall->adjoinId >= 0)
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

			while (!stack.empty())
			{
				EditorSector* sector = stack.back();
				stack.pop_back();

				const size_t wallCount = sector->walls.size();
				EditorWall* wall = sector->walls.data();
				for (size_t w = 0; w < wallCount; w++, wall++)
				{
					s32 idx = -1;
					if (TFE_Polygon::vtxEqual(&value, &sector->vtx[wall->idx[0]]))
					{
						idx = wall->idx[0];
					}
					else if (TFE_Polygon::vtxEqual(&value, &sector->vtx[wall->idx[1]]))
					{
						idx = wall->idx[1];
					}

					if (idx >= 0)
					{
						// Insert.
						FeatureId newId = createFeatureId(sector, idx, part);
						selection_insertVertex(newId, value, nullptr, part);

						// Add the next sector to the stack, if it hasn't already been processed.
						EditorSector* next = wall->adjoinId < 0 ? nullptr : &s_level.sectors[wall->adjoinId];
						if (next && next->searchKey != s_searchKey)
						{
							stack.push_back(next);
							next->searchKey = s_searchKey;
						}
					}
				}
			}
		}

		return true;
	}

	// Remove all vertices in the list at 'value'.
	bool selection_removeVertex(FeatureId id, Vec2f value)
	{
		const s32 count = (s32)s_selectionList2[SEL_VERTEX].size();
		const FeatureId* featureId = s_selectionList2[SEL_VERTEX].data();
		std::vector<s32> idxToRemove;
		for (s32 i = 0; i < count; i++)
		{
			if (featuresEqualNoData(featureId[i], id))
			{
				idxToRemove.push_back(i);
			}
			else
			{
				s32 index;
				EditorSector* sector = unpackFeatureId(featureId[i], &index);
				const Vec2f& srcValue = sector->vtx[index];

				if (TFE_Polygon::vtxEqual(&value, &srcValue))
				{
					idxToRemove.push_back(i);
				}
			}
		}
		if (idxToRemove.empty()) { return false; }
		const s32 delCount = (s32)idxToRemove.size();
		const s32* idxList = idxToRemove.data();
		for (s32 i = delCount - 1; i >= 0; i--)
		{
			s_selectionList2[SEL_VERTEX].erase(s_selectionList2[SEL_VERTEX].begin() + idxList[i]);
		}
		return true;
	}

	bool selection_isVertexInSet(FeatureId id)
	{
		// Insert the vertex if not found.
		const s32 count = (s32)s_selectionList2[SEL_VERTEX].size();
		const FeatureId* featureId = s_selectionList2[SEL_VERTEX].data();
		for (s32 i = 0; i < count; i++)
		{
			if (featuresEqualNoData(featureId[i], id)) { return true; }
		}
		return false;
	}
		
	bool selection_insertFeatureId(SelectionList& list, FeatureId id)
	{
		if (selection_isFeatureIdInSet(list, id)) { return false; }
		list.push_back(id);
		return true;
	}

	bool selection_removeFeatureId(SelectionList& list, FeatureId id)
	{
		s32 index;
		selection_isFeatureIdInSet(list, id, &index);
		if (index < 0) { return false; }

		list.erase(list.begin() + index);
		return true;
	}

	bool selection_isFeatureIdInSet(SelectionList& list, FeatureId id, s32* index)
	{
		const s32 count = (s32)list.size();
		const FeatureId* listId = list.data();
		if (index) { *index = -1; }
		for (s32 i = 0; i < count; i++)
		{
			if (featuresEqual(listId[i], id))
			{
				if (index) { *index = i; }
				return true;
			}
		}
		return false;
	}

	bool selection_featureId(SelectAction action, FeatureId id)
	{
		bool buildDerived = false;
		bool actionDone = false;
		switch (action)
		{
			case SA_SET:
			{
				s_selectionList2[s_currentSelection].clear();
				actionDone = selection_insertFeatureId(s_selectionList2[s_currentSelection], id);
				buildDerived = actionDone;
				infoPanel_clearSelection();
			} break;
			case SA_ADD:
			{
				actionDone = selection_insertFeatureId(s_selectionList2[s_currentSelection], id);
				buildDerived = actionDone;
				infoPanel_clearSelection();
			} break;
			case SA_REMOVE:
			{
				actionDone = selection_removeFeatureId(s_selectionList2[s_currentSelection], id);
				buildDerived = actionDone;
				infoPanel_clearSelection();
			} break;
			case SA_TOGGLE:
			{
				if (selection_isFeatureIdInSet(s_selectionList2[s_currentSelection], id))
				{
					actionDone = selection_removeFeatureId(s_selectionList2[s_currentSelection], id);
				}
				else
				{
					actionDone = selection_insertFeatureId(s_selectionList2[s_currentSelection], id);
				}
				buildDerived = actionDone;
				infoPanel_clearSelection();
			} break;
			case SA_CHECK_INCLUSION:
			{
				actionDone = selection_isFeatureIdInSet(s_selectionList2[s_currentSelection], id);
			} break;
			case SA_SET_HOVERED:
			{
				s_hovered = id;
				actionDone = true;
			} break;
		}

		if (buildDerived)
		{
			edit_setTransformChange();
			selection_derivedBuildNeeded();
		}
		return actionDone;
	}

	FeatureId selection_getFeatureId(s32 index, SelectionListId listId)
	{
		FeatureId id = FEATUREID_NULL;
		if (index == SEL_INDEX_HOVERED && s_currentSelection == listId)
		{
			id = s_hovered;
		}
		else if (index >= 0 && index < (s32)s_selectionList2[listId].size())
		{
			id = s_selectionList2[listId][index];
		}
		return id;
	}

	void selection_insertWallVertices(EditorSector* sector, const EditorWall* wall)
	{
		FeatureId v0 = createFeatureId(sector, wall->idx[0]);
		FeatureId v1 = createFeatureId(sector, wall->idx[1]);
		selection_insertVertex(v0, sector->vtx[wall->idx[0]], sector);
		selection_insertVertex(v1, sector->vtx[wall->idx[1]], sector);
	}

	void selection_derivedBuildNeeded()
	{
		if (s_currentSelection != SEL_VERTEX && s_currentSelection != SEL_SURFACE && s_currentSelection != SEL_SECTOR)
		{
			return;
		}
		// Clear any geometry selection that does not match the current selection.
		if (s_currentSelection != SEL_VERTEX) { s_selectionList2[SEL_VERTEX].clear(); }
		if (s_currentSelection != SEL_SURFACE) { s_selectionList2[SEL_SURFACE].clear(); }
		if (s_currentSelection != SEL_SECTOR) { s_selectionList2[SEL_SECTOR].clear(); }

		s_derivedBuildNeeded = true;
	}

	void selection_buildDerived()
	{
		if (!s_derivedBuildNeeded || s_dragSelect.active) { return; }
		s_derivedBuildNeeded = false;

		if (s_currentSelection != SEL_VERTEX && s_currentSelection != SEL_SURFACE && s_currentSelection != SEL_SECTOR)
		{
			return;
		}

		// Setup derived selections.
		if (s_currentSelection == SEL_VERTEX)
		{
			const s32 count = (s32)s_selectionList2[SEL_VERTEX].size();
			const FeatureId* vtx = s_selectionList2[SEL_VERTEX].data();

			s32 featureIndex = -1;
			std::unordered_map<s32, std::vector<s32>> sectorVtxMap;

			// Fill the hashmap with sectors and feature (vtx) ids.
			for (size_t i = 0; i < count; i++)
			{
				EditorSector* sector = unpackFeatureId(vtx[i], &featureIndex);
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

			// walk hashmap keys (sectorIds)
			for (const auto entry : sectorVtxMap)
			{
				s32 sectorId = entry.first;
				std::vector<s32> vtxIds = entry.second;
				EditorSector* sector = &s_level.sectors[sectorId];
				if (vtxIds.size() == sector->vtx.size())
				{
					// All vtx captured. Add the sector then all its walls
					FeatureId id = createFeatureId(sector);
					selection_insertFeatureId(s_selectionList2[SEL_SECTOR], id);
					for (s32 wallIndex = 0; wallIndex < (s32)sector->walls.size(); wallIndex++)
					{
						id = createFeatureId(sector, wallIndex, HP_MID);
						selection_insertFeatureId(s_selectionList2[SEL_SURFACE], id); // add walls
					}
				}
				else if (vtxIds.size() > 1)
				{
					// find which walls are captured
					s32 wallCount = (s32)sector->walls.size();
					EditorWall* w = sector->walls.data();
					for (size_t wallIndex = 0; wallIndex < wallCount; wallIndex++, w++)
					{
						for (size_t v0 = 0; v0 < vtxIds.size(); v0++)
						{
							if (vtxIds[v0] == w->idx[0])
							{
								for (size_t v1 = 0; v1 < vtxIds.size(); v1++)
								{
									if (vtxIds[v1] == w->idx[1])
									{
										FeatureId id = createFeatureId(sector, (s32)wallIndex, HP_MID);
										selection_insertFeatureId(s_selectionList2[SEL_SURFACE], id);
									}
								}
							}
						}
					}

				}
			}
		}
		else if (s_currentSelection == SEL_SURFACE)
		{
			// Surfaces select vertices.
			const s32 count = (s32)s_selectionList2[SEL_SURFACE].size();
			const FeatureId* list = s_selectionList2[SEL_SURFACE].data();
			std::unordered_map<s32, std::vector<s32>> sectorWallMap;

			for (s32 i = 0; i < count; i++)
			{
				s32 featureIndex;
				HitPart part;
				EditorSector* sector = unpackFeatureId(list[i], &featureIndex, (s32*)&part);

				// Skip flats.
				if (part == HP_FLOOR || part == HP_CEIL) { continue; }

				// Populate hashmap for sector selection based on MID walls
				if (sectorWallMap.count(sector->id) > 0)
				{
					sectorWallMap.at(sector->id).push_back(featureIndex);
				}
				else
				{
					sectorWallMap.emplace(sector->id, std::vector<s32>());
					sectorWallMap.at(sector->id).push_back(featureIndex);
				}

				EditorWall* wall = &sector->walls[featureIndex];
				selection_insertWallVertices(sector, wall);
			}

			// Do select sectors on MID wall tests
			for (const auto entry : sectorWallMap)
			{
				s32 sectorId = entry.first;
				std::vector<s32> wallIds = entry.second;
				EditorSector* sector = &s_level.sectors[sectorId];
				if (sector->walls.size() == wallIds.size())
				{
					FeatureId id = createFeatureId(sector);
					selection_insertFeatureId(s_selectionList2[SEL_SECTOR], id);
				}
			}

			// Surfaces may also select sectors, if floor and ceiling surfaces are selected.
			for (s32 i = 0; i < count; i++)
			{
				s32 featureIndex;
				HitPart part;
				EditorSector* sector = unpackFeatureId(list[i], &featureIndex, (s32*)&part);
				if (part != HP_FLOOR && part != HP_CEIL) { continue; }

				FeatureId id = createFeatureId(sector);
				selection_insertFeatureId(s_selectionList2[SEL_SECTOR], id);
			}

		}
		else if (s_currentSelection == SEL_SECTOR)
		{
			// Sectors select surfaces and vertices.
			const s32 count = (s32)s_selectionList2[SEL_SECTOR].size();
			const FeatureId* list = s_selectionList2[SEL_SECTOR].data();
			for (s32 i = 0; i < count; i++)
			{
				EditorSector* sector = unpackFeatureId(list[i]);
				if (!sector) { continue; }

				const s32 wallCount = (s32)sector->walls.size();
				const EditorWall* wall = sector->walls.data();
				for (s32 w = 0; w < wallCount; w++, wall++)
				{
					FeatureId id = createFeatureId(sector, w, HP_MID);
					selection_insertFeatureId(s_selectionList2[SEL_SURFACE], id);
					selection_insertWallVertices(sector, wall);
				}
			}
		}
	}
}
