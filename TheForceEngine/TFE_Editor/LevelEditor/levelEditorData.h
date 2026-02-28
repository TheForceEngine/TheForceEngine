#pragma once
//////////////////////////////////////////////////////////////////////
// The Force Engine Editor
// A system built to view and edit Dark Forces data files.
// The viewing aspect needs to be put in place at the beginning
// in order to properly test elements in isolation without having
// to "play" the game as intended.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include "entity.h"
#include "groups.h"
#include "note.h"
#include "featureId.h"
#include <TFE_Editor/EditorAsset/editorAsset.h>
#include <TFE_Editor/EditorAsset/editorTexture.h>
#include <TFE_Editor/editorProject.h>
#include <TFE_Polygon/polygon.h>
#include <TFE_Editor/history.h>

namespace LevelEditor
{
	enum LevelEditorFormat
	{
		LEF_MinVersion = 1,
		LEF_EntityV1   = 2,
		LEF_EntityV2   = 3,
		LEF_EntityList = 4,
		LEF_EntityV3   = 5,
		LEF_EntityV4   = 6,
		LEF_InfV1      = 7,
		LEF_Groups     = 8,
		LEF_LevelNotes =10,
		LEF_Guidelines =11,
		LEF_ScriptCall1=12,
		LEF_GuidelineV2=13,
		LEF_CurVersion =13,
	};

	enum LevelEditMode
	{
		LEDIT_DRAW = 1,
		//LEDIT_SMART,	// vertex + wall + height "smart" edit.
		LEDIT_VERTEX,	// vertex only
		LEDIT_WALL,		// wall only in 2D, wall + floor/ceiling in 3D
		LEDIT_SECTOR,
		LEDIT_ENTITY,
		// Special
		LEDIT_GUIDELINES,
		LEDIT_NOTES,
		LEDIT_UNKNOWN,
	};
		
	enum DrawMode
	{
		DMODE_RECT = 0,
		DMODE_SHAPE,
		DMODE_RECT_VERT,
		DMODE_SHAPE_VERT,
		DMODE_CURVE_CONTROL,
		DMODE_COUNT
	};

	enum WallPart
	{
		WP_MID = 0,
		WP_TOP,
		WP_BOT,
		WP_SIGN,
		WP_COUNT
	};

	enum HitPart
	{
		HP_MID = 0,
		HP_TOP,
		HP_BOT,
		HP_SIGN,
		HP_FLOOR,
		HP_CEIL,
		HP_COUNT,
		HP_NONE = HP_COUNT
	};

	enum RayConst
	{
		LAYER_ANY = -256,
	};

	struct LevelTextureAsset
	{
		std::string name;
		TFE_Editor::AssetHandle handle = NULL_ASSET;
	};

	struct LevelTexture
	{
		s32 texIndex = -1;
		Vec2f offset = { 0 };
	};

	struct EditorWall
	{
		LevelTexture tex[WP_COUNT] = {};

		s32 idx[2] = { 0 };
		s32 adjoinId = -1;
		s32 mirrorId = -1;

		u32 flags[3] = { 0 };
		s32 wallLight = 0;
	};

	struct EditorSlope
	{
		u32 floorAnchorSectorId = 0;
		u32 floorAnchorWallId = 0;
		f32 floorSlopeAngle = 0.0f;

		u32 ceilAnchorSectorId = 0;
		u32 ceilAnchorWallId = 0;
		f32 ceilSlopeAngle = 0.0f;
	};

	// This structure closely mirrors the same structure in Outlaws, with a few editor tweaks.
	struct SlopedPlane
	{
		s32 hingeSector = -1;   // Indices since pointers can change with editing.
		s32 hingeWall = -1;
		f32 angle = 0.0f;		// Radians.
		f32 minHeight = 0.0f;
		f32 maxHeight = 0.0f;
		f32 dp = 0.0f;
		f32 sDotRightPoint = 0.0f;
		f32 tDotRightPoint = 0.0f;
		Vec2f normal = { 0 };	// Assumes normal.y = 1.0
		Vec3f hinge[2] = { 0 };	// Two hinge points.
		Vec3f s = { 0 };
		Vec3f t = { 0 };		// coordinate space.
		Vec4f plane = { 0 };	// actual plane for intersections.
	};

	struct EditorSector
	{
		s32 id = 0;
		u32 groupId = 0;
		u32 groupIndex = 0;
		std::string name;	// may be empty.

		LevelTexture floorTex = {};
		LevelTexture ceilTex = {};

		f32 floorHeight = 0.0f;
		f32 ceilHeight = 0.0f;
		f32 secHeight = 0.0f;

		u32 ambient = 0;
		u32 flags[3] = { 0 };

		// Geometry
		std::vector<Vec2f> vtx;
		std::vector<EditorWall> walls;
		std::vector<EditorObject> obj;
		EditorSlope slope = {};

		// Bounds
		Vec3f bounds[2];
		s32 layer = 0;

		// Polygon
		Polygon poly;

		// Slopes - Derived data, do not serialize.
		u32 slopeFrame = 0;
		SlopedPlane floorPlane = {};
		SlopedPlane ceilPlane = {};

		// For searches.
		u32 searchKey = 0;
	};

	struct IndexPair
	{
		s32 i0, i1;
	};

	enum GuidelineFlags
	{
		GLFLAG_NONE = 0,
		GLFLAG_LIMIT_HEIGHT = FLAG_BIT(0),
		GLFLAG_DISABLE_SNAPPING = FLAG_BIT(1),
	};

	enum AdjType
	{
		ADJ_TYPE_NONE = 0,
		ADJ_TYPE_TOP = FLAG_BIT(0),
		ADJ_TYPE_BOT = FLAG_BIT(1),
	};

	struct GuidelineEdge
	{
		s32 idx[3] = { -1, -1, -1 };	// curve if idx[2] >= 0
	};

	struct GuidelineSubDiv
	{
		s32 edge;
		f32 param;
	};
		
	struct Guideline
	{
		s32 id = 0;
		std::vector<Vec2f> vtx;
		// selectable knots on the path/curve, ordered by t value.
		// (x,z) = position, y = t value, w = edge index.
		std::vector<Vec4f> knots;
		std::vector<GuidelineEdge> edge;
		std::vector<f32> offsets;
		Vec4f bounds = { 0 };
		f32 maxOffset = 0.0f;

		// Derived (don't serialize).
		std::vector<GuidelineSubDiv> subdiv;

		// Settings
		u32 flags = GLFLAG_NONE;		// GuidelineFlags
		f32 maxHeight = 0.0f;
		f32 minHeight = 0.0f;
		f32 maxSnapRange = 0.0f;		// Set based on the grid at creation time.
		f32 subDivLen = 0.0f;			// Default = no subdivision.
	};

	typedef std::vector<EditorSector*> SectorList;

	struct EditorLevel
	{
		std::string name;
		std::string slot;
		std::string palette;
		TFE_Editor::FeatureSet featureSet = TFE_Editor::FSET_VANILLA;

		// Sky Parallax.
		Vec2f parallax = { 1024.0f, 1024.0f };

		// Texture data.
		std::vector<LevelTextureAsset> textures;

		// Sector data.
		std::vector<EditorSector> sectors;

		// Entity data.
		std::vector<Entity> entities;

		// Level Notes.
		std::vector<LevelNote> notes;

		// Guidelines.
		std::vector<Guideline> guidelines;

		// Level bounds.
		Vec3f bounds[2] = { 0 };
		s32 layerRange[2] = { 0 };
	};

	// Collision
	struct Ray
	{
		Vec3f origin;
		Vec3f dir;
		f32 maxDist;
	};

	struct RayHitInfo
	{
		// What was hit.
		s32 hitSectorId;
		s32 hitWallId;
		s32 hitObjId;
		HitPart hitPart;

		// Actual hit position.
		Vec3f hitPos;
		f32 dist;
	};

	struct StartPoint
	{
		Vec3f pos;
		f32 yaw;
		f32 pitch;
		EditorSector* sector;
	};

	struct LevelExportInfo
	{
		std::string slot;
		const TFE_Editor::Asset* asset;
	};

	void levelClear();
	bool loadLevelFromAsset(const TFE_Editor::Asset* asset);
	TFE_Editor::AssetHandle loadTexture(const char* bmTextureName);
	TFE_Editor::AssetHandle loadPalette(const char* paletteName);
	TFE_Editor::AssetHandle loadColormap(const char* colormapName);

	bool exportLevels(const char* workPath, const char* exportPath, const char* gobName, const std::vector<LevelExportInfo>& levelList);
	
	bool saveLevel();
	bool saveLevelToPath(const char* filePath, bool cleanLevel = true);
	bool loadFromTFL(const char* name);
	bool loadFromTFLWithPath(const char* filePath);
	void updateLevelBounds(EditorSector* sector = nullptr);

	bool exportLevel(const char* path, const char* name, const StartPoint* start);
	bool exportSelectionToText(std::string& buffer);
	bool importFromText(const std::string& buffer, bool centerOnMouse = true);
	void sectorToPolygon(EditorSector* sector);
	void polygonToSector(EditorSector* sector);

	s32 addEntityToLevel(const Entity* newEntity);
	s32 addLevelNoteToLevel(const LevelNote* newNote);
		
	TFE_Editor::EditorTexture* getTexture(s32 index);
	s32 getTextureIndex(const char* name, bool* isNewTexture = nullptr);
		
	f32 getWallLength(const EditorSector* sector, const EditorWall* wall);
	bool getSignExtents(const EditorSector* sector, const EditorWall* wall, Vec2f ext[2]);
	void centerSignOnSurface(const EditorSector* sector, EditorWall* wall);

	void level_createSnapshot(TFE_Editor::SnapshotBuffer* buffer);
	void level_createSectorSnapshot(TFE_Editor::SnapshotBuffer* buffer, std::vector<s32>& sectorIds);
	void level_createSectorWallSnapshot(TFE_Editor::SnapshotBuffer* buffer, std::vector<IndexPair>& sectorWallIds);
	void level_createSectorAttribSnapshot(TFE_Editor::SnapshotBuffer* buffer, std::vector<IndexPair>& sectorIds);
	void level_createFeatureTextureSnapshot(TFE_Editor::SnapshotBuffer* buffer, s32 count, const FeatureId* feature);
	void level_createEntiyListSnapshot(TFE_Editor::SnapshotBuffer* buffer, s32 sectorId);
	void level_createGuidelineSnapshot(TFE_Editor::SnapshotBuffer* buffer);
	void level_createLevelNoteSnapshot(TFE_Editor::SnapshotBuffer* buffer);
	void level_createSingleGuidelineSnapshot(TFE_Editor::SnapshotBuffer* buffer, s32 index);

	void level_createLevelSectorSnapshotSameAssets(std::vector<EditorSector>& sectors);
	void level_getLevelSnapshotDelta(std::vector<s32>& modifiedSectors, const std::vector<EditorSector>& sectorSnapshot);

	void level_unpackSnapshot(s32 id, u32 size, void* data);
	void level_unpackSectorSnapshot(u32 size, void* data);
	void level_unpackSectorWallSnapshot(u32 size, void* data);
	void level_unpackSectorAttribSnapshot(u32 size, void* data);
	void level_unpackFeatureTextureSnapshot(u32 size, void* data);
	void level_unpackEntiyListSnapshot(u32 size, void* data);
	void level_unpackGuidelineSnapshot(u32 size, void* data);
	void level_unpackSingleGuidelineSnapshot(u32 size, void* data);
	void level_unpackLevelNoteSnapshot(u32 size, void* data);
	
	// Spatial Queries
	s32  findSectorByName(const char* name, s32 excludeId = -1);
	EditorSector* findSector2d(Vec2f pos);
	EditorSector* findSector2d_closestHeight(Vec2f pos, f32 height);
	bool traceRay(const Ray* ray, RayHitInfo* hitInfo, bool flipFaces, bool canHitSigns, bool canHitObjects = false);
	// Get all sectors that have bounds that contain the point.
	bool getOverlappingSectorsPt(const Vec3f* pos, SectorList* result, f32 padding = 0.0f);
	// Get all sectors that have bounds that overlap the input bounds.
	bool getOverlappingSectorsBounds(const Vec3f bounds[2], SectorList* result);
	void fixupLevel(bool createSnapshot = true);
	// Helpers
	bool aabbOverlap3d(const Vec3f* aabb0, const Vec3f* aabb1);
	bool aabbOverlap2d(const Vec3f* aabb0, const Vec3f* aabb1);
	bool pointInsideAABB3d(const Vec3f* aabb, const Vec3f* pt);
	bool pointInsideAABB2d(const Vec3f* aabb, const Vec3f* pt);
	bool isPointInsideSector2d(EditorSector* sector, Vec2f pos);
	bool isPointInsideSector3d(EditorSector* sector, Vec3f pos);
	s32 findClosestWallInSector(const EditorSector* sector, const Vec2f* pos, f32 maxDistSq, f32* minDistToWallSq);
	EditorSector* findSector3d(Vec3f pos);

	bool rayAABBIntersection(const Ray* ray, const Vec3f* bounds, f32* hitDist);
		
	// Groups
	inline Group* sector_getGroup(EditorSector* sector)
	{
		Group* group = groups_getByIndex(sector->groupIndex);
		if (!group || group->id != sector->groupId)
		{
			group = groups_getById(sector->groupId);
			sector->groupIndex = group->index;
		}
		assert(group);
		return group;
	}

	bool sector_inViewRange(const EditorSector* sector);
	bool sector_onActiveLayer(const EditorSector* sector);

	void updateSlopeData(EditorSector* sector);
	f32 getFloorAtXZ(const EditorSector* sector, Vec2f pos);
	f32 getCeilAtXZ(const EditorSector* sector, Vec2f pos);
	u32 getAdjoinType(const EditorSector* sector, const EditorWall* wall);

	bool rayPlaneIntersection(const Vec3f& origin, const Vec3f& dir, const Vec4f& plane, f32& dist);

	inline bool sector_isHidden(EditorSector* sector)
	{
		return (sector_getGroup(sector)->flags & GRP_HIDDEN) != 0;
	}

	inline bool sector_isLocked(EditorSector* sector)
	{
		return (sector_getGroup(sector)->flags & GRP_LOCKED) != 0;
	}

	inline bool sector_isInteractable(EditorSector* sector)
	{
		const Group* group = sector_getGroup(sector);
		return !(group->flags & GRP_HIDDEN) && !(group->flags & GRP_LOCKED);
	}

	inline bool sector_excludeFromExport(EditorSector* sector)
	{
		const Group* group = sector_getGroup(sector);
		return (group->flags & GRP_EXCLUDE) != 0;
	}

	inline u32 sector_getGroupColor(EditorSector* sector)
	{
		const Group* group = sector_getGroup(sector);
		const u32 r = u32(group->color.x * 255.0f);
		const u32 g = u32(group->color.y * 255.0f);
		const u32 b = u32(group->color.z * 255.0f);
		return (0x80 << 24) | (b << 16) | (g << 8) | (r);
	}

	inline Group* levelNote_getGroup(LevelNote* note)
	{
		Group* group = groups_getByIndex(note->groupIndex);
		if (!group || group->id != note->groupId)
		{
			group = groups_getById(note->groupId);
			note->groupIndex = group->index;
		}
		assert(group);
		return group;
	}

	inline bool levelNote_isInteractable(LevelNote* note)
	{
		const Group* group = levelNote_getGroup(note);
		return !(group->flags & GRP_HIDDEN) && !(group->flags & GRP_LOCKED);
	}

	extern std::vector<u8> s_fileData;
	extern std::vector<IndexPair> s_pairs;
	extern std::vector<IndexPair> s_prevPairs;
}
