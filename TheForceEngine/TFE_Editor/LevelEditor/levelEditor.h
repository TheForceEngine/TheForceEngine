#pragma once
//////////////////////////////////////////////////////////////////////
// The Force Engine Editor
// A system built to view and edit Dark Forces data files.
// The viewing aspect needs to be put in place at the beginning
// in order to properly test elements in isolation without having
// to "play" the game as intended.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_Editor/EditorAsset/editorAsset.h>
#include "levelEditorData.h"
#include "selection.h"

namespace LevelEditor
{
	struct EditorSector;

	struct AppendTexList
	{
		s32 startIndex = -1;
		std::vector<std::string> list;
	};

	enum LevelEditFlags
	{
		LEF_NONE = 0,
		LEF_SHOW_GRID = FLAG_BIT(0),
		LEF_SHOW_LOWER_LAYERS = FLAG_BIT(1),
		LEF_SHOW_ALL_LAYERS = FLAG_BIT(2),
		LEF_SHOW_INF_COLORS = FLAG_BIT(3),
		LEF_FULLBRIGHT = FLAG_BIT(4),

		LEF_SECTOR_EDGES = FLAG_BIT(16),

		LEF_DEFAULT = LEF_SHOW_GRID | LEF_SHOW_LOWER_LAYERS | LEF_SHOW_INF_COLORS
	};

	// Level Windows open.
	enum LevelWindows
	{
		LWIN_NONE = 0,
		LWIN_HISTORY = FLAG_BIT(0),
		LWIN_VIEW_SETTINGS = FLAG_BIT(1),
	};

	enum SelectMode
	{
		SELECTMODE_NONE = 0,
		SELECTMODE_SECTOR,
		SELECTMODE_WALL,
		SELECTMODE_SECTOR_OR_WALL,
		SELECTMODE_POSITION,
		SELECTMODE_COUNT
	};

	bool init(TFE_Editor::Asset* asset);
	void destroy();

	void update();
	bool menu();

	bool levelIsDirty();
	void levelSetClean();

	bool levelLighting();
	bool userPreferences();
	bool testOptions();

	void selectNone();
	void selectAll();
	void selectInvert();
	s32 getSectorNameLimit();

	void setSelectMode(SelectMode mode = SELECTMODE_NONE);
	SelectMode getSelectMode();
	void handleSelectMode(Vec3f pos);
	void handleSelectMode(EditorSector* sector, s32 wallIndex);

	void getGridOrientedRect(const Vec2f p0, const Vec2f p1, Vec2f* rect);

	void setViewportScrollTarget2d(Vec2f target, f32 speed = 0.0f);
	void setViewportScrollTarget3d(Vec3f target, f32 targetYaw, f32 targetPitch, f32 speed = 0.0f);
	Vec4f viewportBoundsWS2d(f32 padding = 0.0f);

	// Shared Edit Commands
	void edit_moveSelectedFlats(f32 delta);
	bool edit_splitWall(s32 sectorId, s32 wallIndex, Vec2f newPos);
	void edit_deleteSectors(std::vector<s32>& sectorIds, bool addToHistory = true);
	void edit_deleteSector(s32 sectorId, bool addToHistory = true);
	bool edit_tryAdjoin(s32 sectorId, s32 wallId, bool exactMatch = false);
	void edit_removeAdjoin(s32 sectorId, s32 wallId);
	void edit_clearSelections(bool endTransform = true);
	bool edit_createSectorFromRect(const f32* heights, const Vec2f* vtx, bool allowSubsectorExtrude=true);
	bool edit_createSectorFromShape(const f32* heights, s32 vertexCount, const Vec2f* vtx, bool allowSubsectorExtrude=true);
	// Return true if the assigned texture is new.
	AppendTexList& edit_getTextureAppendList();
	void edit_clearTextureAppendList();
	bool edit_setTexture(s32 count, const FeatureId* feature, s32 texIndex, Vec2f* offset = nullptr);
	void edit_clearTexture(s32 count, const FeatureId* feature);
	void edit_deleteObject(EditorSector* sector, s32 index);
	void edit_deleteLevelNote(s32 index);
	void edit_setEditMode(LevelEditMode mode);
	void edit_cleanSectorList(std::vector<s32>& selectedSectors, bool addToHistory = true);
	void edit_placeEntity(s32 id);
	void editor_reloadLevel();
	void editor_closeLevel(bool saveBeforeClosing);
	EditorSector* edit_getHoverSector2dAtCursor();
	Vec3f edit_viewportCoordToWorldDir3d(Vec2i vCoord);
	bool edit_viewportHovered(s32 mx, s32 my);

	bool edit_hasItemsInClipboard();
	void edit_clearClipboard();

	// Drag Select: TODO Move?
	void startDragSelect(s32 mx, s32 my, DragSelectMode mode);
	void updateDragSelect(s32 mx, s32 my);

	// TODO: Move?
	Vec2f mouseCoordToWorldPos2d(s32 mx, s32 my);
	void edit_endTransform();
	
	// Grid?
	void adjustGridHeight(EditorSector* sector);

	s32 getDefaultTextureIndex(WallPart part);
	Vec3f moveAlongRail(Vec3f dir, bool adjustPosByView = true);
	Vec3f moveAlongXZPlane(f32 yHeight);
	bool isUiModal();
	bool isTextureAssignDirty();
	void setTextureAssignDirty(bool dirty = true);

	bool edit_closeLevelCheckSave();
}
