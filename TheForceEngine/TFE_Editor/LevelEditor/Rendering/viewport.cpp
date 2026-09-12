#include "viewport.h"
#include "grid.h"
#include "grid2d.h"
#include "grid3d.h"
#include "gizmo.h"
#include <TFE_System/math.h>
#include <TFE_Editor/editor.h>
#include <TFE_Editor/editorMath.h>
#include <TFE_Editor/editorConfig.h>
#include <TFE_Editor/LevelEditor/editCommon.h>
#include <TFE_Editor/LevelEditor/editGeometry.h>
#include <TFE_Editor/LevelEditor/editGuidelines.h>
#include <TFE_Editor/LevelEditor/editTransforms.h>
#include <TFE_Editor/LevelEditor/levelEditor.h>
#include <TFE_Editor/LevelEditor/levelEditorData.h>
#include <TFE_Editor/LevelEditor/sharedState.h>
#include <TFE_Editor/LevelEditor/selection.h>
#include <TFE_Editor/LevelEditor/userPreferences.h>
#include <TFE_Editor/LevelEditor/levelEditorInf.h>
#include <TFE_Editor/LevelEditor/groups.h>
#include <TFE_Editor/EditorAsset/editorTexture.h>
#include <TFE_Jedi/Level/rwall.h>
#include <TFE_Jedi/Level/rsector.h>
#include <TFE_RenderBackend/shader.h>
#include <TFE_RenderBackend/vertexBuffer.h>
#include <TFE_RenderBackend/indexBuffer.h>
#include <TFE_RenderShared/lineDraw2d.h>
#include <TFE_RenderShared/triDraw2d.h>
#include <TFE_RenderShared/triDraw3d.h>
#include <TFE_RenderShared/modelDraw.h>
#include <TFE_System/system.h>
#include <TFE_RenderBackend/renderBackend.h>

// Jedi GPU Renderer.
#include <TFE_Jedi/Level/levelData.h>
#include <TFE_Jedi/Renderer/RClassic_GPU/rsectorGPU.h>

#include <algorithm>
#include <vector>

using namespace TFE_RenderShared;
using namespace TFE_Editor;
using namespace TFE_Jedi;

namespace LevelEditor
{
	const f32 c_vertexSize = 2.0f;
	
	enum Highlight
	{
		HL_NONE = 0,
		HL_HOVERED,
		HL_SELECTED,
		HL_LOCKED,
		HL_COUNT
	};
			
	enum SectorColor
	{
		SCOLOR_POLY_NORM     = 0x60402020,
		SCOLOR_POLY_HOVERED  = 0x60404020,
		SCOLOR_POLY_SELECTED = 0x60053060,

		SCOLOR_LINE_NORM     = 0xffffffff,
		SCOLOR_LINE_HOVERED  = 0xffffff80,
		SCOLOR_LINE_SELECTED = 0xff1080ff,

		SCOLOR_LINE_NORM_ADJ     = 0xff808080,
		SCOLOR_LINE_HOVERED_ADJ  = 0xffa0a060,
		SCOLOR_LINE_SELECTED_ADJ = 0xff0060a0,

		SCOLOR_LOCKED = 0xff202020,
		SCOLOR_LOCKED_VTX = 0xff404040,
		SCOLOR_LOCKED_TEXTURE = 0xffb06060,
	};
	enum VertexColor
	{
		VCOLOR_NORM     = 0xffae8653,
		VCOLOR_HOVERED  = 0xffffff20,
		VCOLOR_SELECTED = 0xff4a76ff,
		VCOLOR_HOV_AND_SEL = 0xff94ec8f,
	};
	enum EntityColor
	{
		ECOLOR_NORM = 0xffae8653,
		ECOLOR_HOVERED = 0xffffff20,
		ECOLOR_SELECTED = 0xff4a76ff,
		ECOLOR_HOV_AND_SEL = 0xff00ffff,
	};
	enum InfColor
	{
		INF_COLOR_EDIT_HELPER = 0xffc000ff,
	};

	const u32 c_connectMsgColor = 0xc0ff80ff;
	const u32 c_connectClientColor = 0xc080ff80;
	const u32 c_connectTargetColor = 0xc080ffff;

	static const SectorColor c_sectorPolyClr[] = { SCOLOR_POLY_NORM, SCOLOR_POLY_HOVERED, SCOLOR_POLY_SELECTED, SCOLOR_LOCKED };
	static const SectorColor c_sectorLineClr[] = { SCOLOR_LINE_NORM, SCOLOR_LINE_HOVERED, SCOLOR_LINE_SELECTED, SCOLOR_LOCKED_VTX };
	static const SectorColor c_sectorLineClrAdjoin[] = { SCOLOR_LINE_NORM_ADJ, SCOLOR_LINE_HOVERED_ADJ, SCOLOR_LINE_SELECTED_ADJ, SCOLOR_LOCKED };
	static const VertexColor c_vertexClr[] = { VCOLOR_NORM, VCOLOR_HOVERED, VCOLOR_SELECTED, VCOLOR_HOV_AND_SEL, (VertexColor)SCOLOR_LOCKED };
	static const EntityColor c_entityClr[] = { ECOLOR_NORM, ECOLOR_HOVERED, ECOLOR_SELECTED, ECOLOR_HOV_AND_SEL, (EntityColor)SCOLOR_LOCKED };

	#define AMBIENT(x) (u32)(x * 255/31) | ((x * 255/31)<<8) | ((x * 255/31)<<16) | (0xff << 24)
	static const u32 c_sectorTexClr[] =
	{
		AMBIENT(0), AMBIENT(1), AMBIENT(2), AMBIENT(3),
		AMBIENT(4), AMBIENT(5), AMBIENT(6), AMBIENT(7),
		AMBIENT(8), AMBIENT(9), AMBIENT(10), AMBIENT(11),
		AMBIENT(12), AMBIENT(13), AMBIENT(14), AMBIENT(15),
		AMBIENT(16), AMBIENT(17), AMBIENT(18), AMBIENT(19),
		AMBIENT(20), AMBIENT(21), AMBIENT(22), AMBIENT(23),
		AMBIENT(24), AMBIENT(25), AMBIENT(26), AMBIENT(27),
		AMBIENT(28), AMBIENT(29), AMBIENT(30), AMBIENT(31)
	};

	struct Rail
	{
		bool active = false;
		bool hasMoveDir = false;
		bool wasActive = false;
		s32 dirCount = 0;
		f32 moveFade = 0.0f;
		Vec3f origin = { 0 };
		Vec3f moveDir = { 0 };
		Vec3f dir[4] = { 0 };
	};

	static RenderTargetHandle s_viewportRt = 0;
	static std::vector<Vec2f> s_transformedVtx;
	static std::vector<Vec2f> s_bufferVec2;
	static std::vector<Vec3f> s_bufferVec3;

	SectorDrawMode s_sectorDrawMode = SDM_WIREFRAME;
	Vec2i s_viewportSize = { 0 };
	Vec3f s_viewportPos = { 0 };
	Vec4f s_viewportTrans2d = { 0 };
	f32 s_gridOpacity = 0.5f;
	f32 s_zoom2d = 0.25f;			// current zoom level in 2D.
	f32 s_viewDepth[2] = { 65536.0f, -65536.0f };
	u32 s_viewportRenderFlags = VRF_NONE;
	u32 s_viewFrame = 0;

	Rail s_rail = {};
	TextureGpu* s_noteIcon3dImage = nullptr;

	void renderLevel2D();
	void renderLevel3D();
	void renderLevel3DGame();
	void renderSectorWalls2d(s32 layerStart, s32 layerEnd);
	void renderGuidelines2d(const Vec4f viewportBoundsWS);
	void renderGuidelines3d();
	void renderSectorPreGrid();
	void drawSector2d(const EditorSector* sector, Highlight highlight);
	void drawVertex2d(const Vec2f* pos, f32 scale, Highlight highlight);
	void drawVertex2d(const EditorSector* sector, s32 id, f32 extraScale, Highlight highlight);
	void drawWall2d(const EditorSector* sector, const EditorWall* wall, f32 extraScale, Highlight highlight, bool drawNormal = false);
	void drawEntity2d(const EditorSector* sector, const EditorObject* obj, s32 id, u32 objColor, bool drawEntityBounds);
	void drawNoteIcon2d(LevelNote* note, s32 id, u32 objColor);
	void renderSectorVertices2d();
	void drawBounds(const Vec3f* center, Vec3f size, f32 lineWidth, u32 color);
	void drawOBB(const Vec3f* bounds, const Mat3* mtx, const Vec3f* pos, f32 lineWidth, u32 color);
	void drawBounds2d(const Vec2f* center, Vec2f size, f32 lineWidth, u32 color, u32 fillColor);
	void drawOBB2d(const Vec3f* bounds, const Mat3* mtx, const Vec3f* pos, f32 lineWidth, u32 color);
	void drawSolidBox(const Vec3f* center, f32 side, u32 color);
	void drawEditCursor2D(Vec2f* cursorPos);
	void drawSectorShape2D();
	void drawGuidelineShape2D();
	void drawWallLines3D_Highlighted(const EditorSector* sector, const EditorSector* next, const EditorWall* wall, f32 width, Highlight highlight, bool halfAlpha, bool showSign = false);
	void drawPosition2d(f32 width, Vec2f pos, u32 color);
	void drawPositionKnot2d(f32 width, Vec2f pos, u32 color);
	void drawArrow2d(f32 width, f32 lenInPixels, Vec2f pos, Vec2f dir, u32 color);
	void drawArrow2d_Segment(f32 width, f32 lenInPixels, Vec2f pos0, Vec2f pos1, u32 color);
	void drawPosition3d(f32 width, Vec3f pos, u32 color);
	void drawArrow3d(f32 width, f32 lenInPixels, Vec3f pos, Vec3f dir, Vec3f nrm, u32 color);
	void drawArrow3d_Segment(f32 width, f32 lenInPixels, Vec3f pos0, Vec3f pos1, Vec3f nrm, u32 color);
	void drawEntity3D(const EditorSector* sector, const EditorObject* obj, s32 id, u32 objColor, const Vec3f& cameraRgtXZ, bool drawEntityBounds, bool drawHighlights);
	void drawNoteIcon3d(LevelNote* note, s32 id, u32 objColor, const Vec3f& cameraRgt, const Vec3f& cameraUp);
	void drawTransformGizmo();
	bool computeSignCorners(const EditorSector* sector, const EditorWall* wall, Vec3f* corners);
	void computeFlatUv(const Vec2f* pos, const Vec2f* offset, Vec2f* uv);

	void viewport_init()
	{
		grid2d_init();
		tri2d_init();
		tri3d_init();
		grid3d_init();
		TFE_RenderShared::line3d_init();
	}

	void viewport_destroy()
	{
		TFE_RenderBackend::freeRenderTarget(s_viewportRt);
		grid2d_destroy();
		tri2d_destroy();
		tri3d_destroy();
		grid3d_destroy();
		TFE_RenderShared::line3d_destroy();
		s_viewportRt = 0;
	}

	void viewport_render(EditorView view, u32 flags)
	{
		if (!s_viewportRt) { return; }
		s_viewportRenderFlags = flags;
		s_viewFrame++;

		const f32 clearColor[] = { 0.05f, 0.06f, 0.1f, 1.0f };
		TFE_RenderBackend::bindRenderTarget(s_viewportRt);
		TFE_RenderBackend::clearRenderTarget(s_viewportRt, clearColor);

		if (view == EDIT_VIEW_2D) { renderLevel2D(); }
		else if (view == EDIT_VIEW_3D) { renderLevel3D(); }
		else if (view == EDIT_VIEW_3D_GAME) { renderLevel3DGame(); }

		TFE_RenderBackend::unbindRenderTarget();
	}

	void viewport_update(s32 resvWidth, s32 resvHeight)
	{
		DisplayInfo info;
		TFE_RenderBackend::getDisplayInfo(&info);
		Vec2i newSize = { (s32)info.width - resvWidth, (s32)info.height - resvHeight };
		if (newSize.x != s_viewportSize.x || newSize.z != s_viewportSize.z || !s_viewportRt)
		{
			s_viewportSize = newSize;
			TFE_RenderBackend::freeRenderTarget(s_viewportRt);
			s_viewportRt = TFE_RenderBackend::createRenderTarget(s_viewportSize.x, s_viewportSize.z, true);
		}
	}

	void viewport_setNoteIcon3dImage(TextureGpu* image)
	{
		s_noteIcon3dImage = image;
	}

	const TextureGpu* viewport_getTexture()
	{
		return TFE_RenderBackend::getRenderTargetTexture(s_viewportRt);
	}
		
	void viewport_clearRail()
	{
		s_rail.wasActive = false;
		if (s_rail.moveFade == 0.0f)
		{
			s_rail.active = false;
			s_rail.dirCount = 0;
		}
	}

	void viewport_setRail(const Vec3f* rail, s32 dirCount, Vec3f* moveDir)
	{
		s_rail.active = true;
		s_rail.dirCount = dirCount;
		s_rail.origin = rail[0];
		for (s32 i = 0; i < dirCount; i++)
		{
			s_rail.dir[i] = { rail[1+i].x - rail[0].x, rail[1+i].y - rail[0].y, rail[1+i].z - rail[0].z };
			s_rail.dir[i] = TFE_Math::normalize(&s_rail.dir[i]);
		}

		if (moveDir && fabsf(moveDir->x) + fabsf(moveDir->y) + fabsf(moveDir->z) < FLT_EPSILON)
		{
			moveDir = nullptr;
		}

		s_rail.hasMoveDir = moveDir != nullptr;
		s_rail.wasActive = true;
		if (moveDir)
		{
			s_rail.moveDir = *moveDir;
			s_rail.moveFade = 0.25f;
		}
	}

	void viewport_updateRail()
	{
		if (s_rail.moveFade > 0.0f)
		{
			s_rail.moveFade = std::max(0.0f, s_rail.moveFade - (f32)TFE_System::getDeltaTime());
		}
		if (s_rail.moveFade == 0.0f && !s_rail.wasActive)
		{
			s_rail.active = false;
			s_rail.dirCount = 0;
		}
	}

	//////////////////////////////////////////
	// Internal
	//////////////////////////////////////////
	void computeViewportTransform2d()
	{
		// Compute the scale and offset.
		f32 pixelsToWorldUnits = s_zoom2d;
		s_viewportTrans2d.x = 1.0f / pixelsToWorldUnits;
		s_viewportTrans2d.z = -1.0f / pixelsToWorldUnits;

		s_viewportTrans2d.y = -s_viewportPos.x * s_viewportTrans2d.x;
		s_viewportTrans2d.w = s_viewportPos.z * s_viewportTrans2d.z;
	}

	void compute2dCamera(Camera3d& camera)
	{
		camera = s_camera;
		camera.pos = { -s_viewportTrans2d.y / s_viewportTrans2d.x, -1000.0f, -s_viewportTrans2d.w / s_viewportTrans2d.z };
		camera.viewMtx = { 0 };
		camera.viewMtx.m0.x = 1.0f;
		camera.viewMtx.m1.y = -1.0f;
		camera.viewMtx.m2.z = 1.0f;
		camera.projMtx = { 0 };
		camera.projMtx.m0.x = 2.0f * s_viewportTrans2d.x / s_viewportSize.x;
		camera.projMtx.m1.z = 2.0f * s_viewportTrans2d.z / s_viewportSize.z;
		camera.projMtx.m0.w = -1.0f;
		camera.projMtx.m1.w = -1.0f;

		camera.projMtx.m2.y = 1.0f / 4096.0f;
		camera.projMtx.m2.w = 0.0f;
		camera.projMtx.m3.w = 1.0f;
	}

	void drawTransformGizmo()
	{
		if (!edit_isTransformToolActive()) { return; }
		switch (edit_getTransformMode())
		{
			case TRANS_MOVE:
			{
				const Vec3f p0 = edit_getTransformAnchor();
				const Vec3f p1 = edit_getTransformPos();
				if (s_view == EDIT_VIEW_2D) { gizmo_drawMove2d(p0, p1); }
				else { gizmo_drawMove3d(p0, p1); }
			} break;
			case TRANS_ROTATE:
			{
				const Vec3f centerWS = edit_getTransformAnchor();
				if (s_view == EDIT_VIEW_2D) { gizmo_drawRotation2d({ centerWS.x, centerWS.z }); }
				else { gizmo_drawRotation3d(centerWS); }
			} break;
		}
	}
		
	void renderLevel2D()
	{
		// Prepare for drawing.
		computeViewportTransform2d();
		TFE_RenderShared::lineDraw2d_begin(s_viewportSize.x, s_viewportSize.z);
		TFE_RenderShared::triDraw2d_begin(s_viewportSize.x, s_viewportSize.z);
		TFE_RenderShared::modelDraw_begin();

		// Compute the world space bounds.
		const Vec4f viewportBoundsWS = viewportBoundsWS2d(1.0f);

		// Draw lower layers, if enabled.
		if ((s_editFlags & LEF_SHOW_LOWER_LAYERS) && !(s_editFlags & LEF_SHOW_ALL_LAYERS))
		{
			renderSectorWalls2d(s_level.layerRange[0], s_curLayer - 1);
		}

		// Draw pre-grid polygons
		renderSectorPreGrid();

		// Draw the grid layer.
		if (s_editFlags & LEF_SHOW_GRID)
		{
			grid2d_computeScale(s_viewportSize, s_grid.size, s_zoom2d, s_viewportPos);
			grid2d_blitToScreen(s_gridOpacity);
		}

		const EditorObject* visObj[1024];
		const EditorSector* visObjSector[1024];
		s32 visObjId[1024];
		s32 visObjCount = 0;

		// Draw guidelines.
		renderGuidelines2d(viewportBoundsWS);

		// Draw the current layer.
		s32 startLayer = s_curLayer, endLayer = s_curLayer;
		if (s_editFlags & LEF_SHOW_ALL_LAYERS)
		{
			startLayer = s_level.layerRange[0];
			endLayer = s_level.layerRange[1];
		}
		renderSectorWalls2d(startLayer, endLayer);

		// Gather objects
		const size_t count = s_level.sectors.size();
		EditorSector* sector = s_level.sectors.data();
		for (size_t s = 0; s < count; s++, sector++)
		{
			if (!sector_onActiveLayer(sector)) { continue; }
			if (sector_isHidden(sector)) { continue; }
			// TODO: Cull
			const s32 objCount = (s32)sector->obj.size();
			const EditorObject* obj = sector->obj.data();
			for (s32 o = 0; o < objCount; o++, obj++)
			{
				visObjId[visObjCount] = o;
				visObjSector[visObjCount] = sector;
				visObj[visObjCount++] = &sector->obj[o];
			}
		}

		// Draw the hovered and selected sectors.
		// Note they intentionally overlap if s_featureHovered.sector == s_featureCur.sector.
		const bool hasHovered = selection_hasHovered();
		if (selection_hasHovered() && s_editMode == LEDIT_SECTOR)
		{
			if (selection_getSector(SEL_INDEX_HOVERED, sector))
			{
				drawSector2d(sector, HL_HOVERED);
			}
		}
		if (selection_getCount() > 0 && s_editMode == LEDIT_SECTOR)
		{
			uint32_t count = selection_getCount();
			for (u32 i = 0; i < count; i++)
			{
				if (selection_getSector(i, sector))
				{
					drawSector2d(sector, HL_SELECTED);
				}
			}
		}

		// Draw the hovered and selected walls.
		if (hasHovered && s_editMode == LEDIT_WALL)
		{
			EditorSector* sector = nullptr;
			s32 featureIndex = -1;

			if (selection_getSurface(SEL_INDEX_HOVERED, sector, featureIndex))
			{
				drawWall2d(sector, &sector->walls[featureIndex], 1.5f, HL_HOVERED, /*draw normal*/true);
			}
		}
		if (s_editMode == LEDIT_WALL)
		{
			FeatureId* list = nullptr;
			const u32 count = selection_getList(list);
			assert(count == 0 || list != nullptr);

			for (size_t i = 0; i < count; i++)
			{
				s32 featureIndex;
				HitPart part;
				bool overlapped;
				EditorSector* sector = unpackFeatureId(list[i], &featureIndex, (s32*)&part, &overlapped);
				if (overlapped || !sector || part == HP_FLOOR || part == HP_CEIL) { continue; }

				drawWall2d(sector, &sector->walls[featureIndex], 1.5f, HL_SELECTED, true);
			}
		}

		// Draw vertices.
		renderSectorVertices2d();

		// Draw the hovered and selected vertices.
		if (s_editMode == LEDIT_VERTEX)
		{
			bool alsoHovered = false;
			const u32 count = selection_getCount();

			EditorSector* hoveredSector = nullptr;
			s32 hoveredFeatureIndex = -1;
			if (selection_hasHovered())
			{
				selection_getVertex(SEL_INDEX_HOVERED, hoveredSector, hoveredFeatureIndex);
			}

			if (count)
			{
				FeatureId* list = nullptr;
				selection_getList(list);
				assert(list);

				for (size_t i = 0; i < count; i++)
				{
					s32 featureIndex, featureData;
					bool overlapped;
					EditorSector* sector = unpackFeatureId(list[i], &featureIndex, &featureData, &overlapped);
					if (overlapped || !sector) { continue; }

					const bool thisAlsoHovered = featureIndex == hoveredFeatureIndex && sector == hoveredSector;
					if (thisAlsoHovered) { alsoHovered = true; }
					drawVertex2d(sector, featureIndex, 1.5f, thisAlsoHovered ? Highlight(HL_SELECTED + 1) : HL_SELECTED);
				}
			}
			if (hoveredSector && !alsoHovered)
			{
				drawVertex2d(hoveredSector, hoveredFeatureIndex, 1.5f, HL_HOVERED);
			}
		}

		if (s_editMode == LEDIT_DRAW)
		{
			drawSectorShape2D();
		}
		else if (s_editMode == LEDIT_GUIDELINES)
		{
			drawGuidelineShape2D();
		}

		// Draw objects.
		for (s32 o = 0; o < visObjCount; o++)
		{
			bool locked = sector_isLocked((EditorSector*)visObjSector[o]);
			u32 objColor = (s_editMode == LEDIT_ENTITY && !locked) ? 0xffffffff : 0x80ffffff;

			drawEntity2d(visObjSector[o], visObj[o], visObjId[o], objColor, !locked);
		}

		// Draw level notes.
		if (!(s_editorConfig.interfaceFlags & PIF_HIDE_NOTES))
		{
			const s32 levelNoteCount = (s32)s_level.notes.size();
			LevelNote* note = s_level.notes.data();
			for (s32 o = 0; o < levelNoteCount; o++, note++)
			{
				Group* group = levelNote_getGroup(note);
				if (group->flags & GRP_HIDDEN) { continue; }

				bool locked = (group->flags & GRP_LOCKED) != 0u;
				u32 objColor = locked ? 0x80ffffff : note->iconColor;
				drawNoteIcon2d(note, o, objColor);
			}
		}
		
		// Draw drag select, if active.
		if (s_dragSelect.active)
		{
			Vec2f vtx[] =
			{
				{ (f32)s_dragSelect.startPos.x, (f32)s_dragSelect.startPos.z },
				{ (f32)s_dragSelect.curPos.x,   (f32)s_dragSelect.startPos.z },
				{ (f32)s_dragSelect.curPos.x,   (f32)s_dragSelect.curPos.z },
				{ (f32)s_dragSelect.startPos.x, (f32)s_dragSelect.curPos.z },
				{ (f32)s_dragSelect.startPos.x, (f32)s_dragSelect.startPos.z },
			};
			s32 idx[6] = { 0, 1, 2, 0, 2, 3 };

			triDraw2d_addColored(6, 4, vtx, idx, 0x40ff0000);

			u32 colors[2] = { 0xffff0000, 0xffff0000 };
			lineDraw2d_addLine(2.0f, &vtx[0], colors);
			lineDraw2d_addLine(2.0f, &vtx[1], colors);
			lineDraw2d_addLine(2.0f, &vtx[2], colors);
			lineDraw2d_addLine(2.0f, &vtx[3], colors);
		}

		// Transform Gizmos
		drawTransformGizmo();

		// Compute the camera and projection for the model draw.
		Camera3d camera;
		compute2dCamera(camera);

		// Submit.
		TFE_RenderShared::modelDraw_draw(&camera, (f32)s_viewportSize.x, (f32)s_viewportSize.z, false);
		TFE_RenderShared::triDraw2d_draw();
		TFE_RenderShared::lineDraw2d_drawLines();

		// Determine is special controls need to be drawn.
		const EditorPopup popup = getCurrentPopup();
		if (popup == POPUP_EDIT_INF)
		{
			Editor_InfVpControl ctrl;
			editor_infGetViewportControl(&ctrl);
			switch (ctrl.type)
			{
				case InfVpControl_Center:
				case InfVpControl_TargetPos3d:
				{
					TFE_RenderShared::lineDraw2d_begin(s_viewportSize.x, s_viewportSize.z);
					{
						drawPosition2d(1.5f, { ctrl.cen.x, ctrl.cen.z }, INF_COLOR_EDIT_HELPER);
					}
					TFE_RenderShared::lineDraw2d_drawLines();
				} break;
				case InfVpControl_AngleXZ:
				{
					TFE_RenderShared::lineDraw2d_begin(s_viewportSize.x, s_viewportSize.z);
					{
						drawArrow2d(1.5f, 32.0f, { ctrl.cen.x, ctrl.cen.z }, { ctrl.dir.x, ctrl.dir.z }, INF_COLOR_EDIT_HELPER);
					}
					TFE_RenderShared::lineDraw2d_drawLines();
				} break;
				// InfVpControl_AngleXY isn't visible in 2D.
			}
		}
		else if ((s_editMode == LevelEditMode::LEDIT_SECTOR || s_editMode == LevelEditMode::LEDIT_WALL) && selection_getCount() <= 1)
		{
			// Handle drawing INF debugging helpers.
			EditorSector* sector = nullptr;
			HitPart part = HP_NONE;
			s32 featureIndex = -1;
			const bool hasHovered = selection_hasHovered();
			if (s_editMode == LevelEditMode::LEDIT_SECTOR)
			{
				selection_getSector(hasHovered ? SEL_INDEX_HOVERED : 0, sector);
			}
			else
			{
				selection_getSurface(hasHovered ? SEL_INDEX_HOVERED : 0, sector, featureIndex, &part);
			}
			if (!sector) { return; }

			TFE_RenderShared::lineDraw2d_begin(s_viewportSize.x, s_viewportSize.z);
			Editor_InfItem* item = editor_getInfItem(sector->name.c_str(), featureIndex);
			if (item)
			{
				const s32 classCount = (s32)item->classData.size();
				const Editor_InfClass* const* classList = item->classData.data();
				for (s32 i = 0; i < classCount; i++)
				{
					const Editor_InfClass* classData = classList[i];
					switch (classData->classId)
					{
						case IIC_ELEVATOR:
						{
							Vec2f startPoint = {};
							startPoint.x = (sector->bounds[0].x + sector->bounds[1].x) * 0.5f;
							startPoint.z = (sector->bounds[0].z + sector->bounds[1].z) * 0.5f;

							const Editor_InfElevator* elev = getElevFromClassData(classData);
							// Elevators interact with other items (sectors/walls) via stop messages.
							const s32 stopCount = (s32)elev->stops.size();
							const Editor_InfStop* stop = elev->stops.data();
							for (s32 s = 0; s < stopCount; s++, stop++)
							{
								const s32 msgCount = (s32)stop->msg.size();
								const Editor_InfMessage* msg = stop->msg.data();
								for (s32 m = 0; m < msgCount; m++, msg++)
								{
									const char* targetSectorName = msg->targetSector.c_str();
									const s32 targetWall = msg->targetWall;
									const s32 id = findSectorByName(targetSectorName);
									const EditorSector* targetSector = id >= 0 ? &s_level.sectors[id] : nullptr;
									if (!targetSector) { continue; }
									if (targetWall >= (s32)targetSector->walls.size()) { continue; }

									Vec2f endPoint = { 0 };
									if (targetWall < 0)
									{
										endPoint.x = (targetSector->bounds[0].x + targetSector->bounds[1].x) * 0.5f;
										endPoint.z = (targetSector->bounds[0].z + targetSector->bounds[1].z) * 0.5f;
									}
									else
									{
										const EditorWall* wall = &targetSector->walls[targetWall];
										const Vec2f* v0 = &targetSector->vtx[wall->idx[0]];
										const Vec2f* v1 = &targetSector->vtx[wall->idx[1]];
										endPoint.x = (v0->x + v1->x) * 0.5f;
										endPoint.z = (v0->z + v1->z) * 0.5f;
									}

									drawArrow2d_Segment(1.5f, 32.0f, startPoint, endPoint, c_connectMsgColor);
								}
							}
						} break;
						case IIC_TRIGGER:
						{
							const Editor_InfTrigger* trigger = getTriggerFromClassData(classData);
							// Triggers interact with other items (sectors/walls) via clients.
							Vec2f startPoint = {};
							if (item->wallNum >= 0)
							{
								const EditorWall* wall = &sector->walls[item->wallNum];
								const Vec2f* v0 = &sector->vtx[wall->idx[0]];
								const Vec2f* v1 = &sector->vtx[wall->idx[1]];
								startPoint.x = (v0->x + v1->x) * 0.5f;
								startPoint.z = (v0->z + v1->z) * 0.5f;
							}
							else
							{
								startPoint.x = (sector->bounds[0].x + sector->bounds[1].x) * 0.5f;
								startPoint.z = (sector->bounds[0].z + sector->bounds[1].z) * 0.5f;
							}

							const s32 clientCount = (s32)trigger->clients.size();
							const Editor_InfClient* client = trigger->clients.data();
							for (s32 c = 0; c < clientCount; c++, client++)
							{
								const char* targetSectorName = client->targetSector.c_str();
								const s32 targetWall = client->targetWall;
								const s32 id = findSectorByName(targetSectorName);
								const EditorSector* targetSector = id >= 0 ? &s_level.sectors[id] : nullptr;
								if (!targetSector) { continue; }
								if (targetWall >= (s32)targetSector->walls.size()) { continue; }

								Vec2f endPoint = { 0 };
								if (targetWall < 0)
								{
									endPoint.x = (targetSector->bounds[0].x + targetSector->bounds[1].x) * 0.5f;
									endPoint.z = (targetSector->bounds[0].z + targetSector->bounds[1].z) * 0.5f;
								}
								else
								{
									const EditorWall* wall = &targetSector->walls[targetWall];
									const Vec2f* v0 = &targetSector->vtx[wall->idx[0]];
									const Vec2f* v1 = &targetSector->vtx[wall->idx[1]];
									endPoint.x = (v0->x + v1->x) * 0.5f;
									endPoint.z = (v0->z + v1->z) * 0.5f;
								}

								drawArrow2d_Segment(1.5f, 32.0f, startPoint, endPoint, c_connectClientColor);
							}
						} break;
						case IIC_TELEPORTER:
						{
							Vec2f startPoint = {};
							startPoint.x = (sector->bounds[0].x + sector->bounds[1].x) * 0.5f;
							startPoint.z = (sector->bounds[0].z + sector->bounds[1].z) * 0.5f;

							const Editor_InfTeleporter* teleporter = getTeleporterFromClassData(classData);
							const s32 id = findSectorByName(teleporter->target.c_str());
							const EditorSector* targetSector = id >= 0 ? &s_level.sectors[id] : nullptr;

							// Teleport interact with other items (sectors) by way of target sector.
							Vec2f endPoint = { 0 };
							if (teleporter->type == TELEPORT_BASIC)
							{
								endPoint = { teleporter->dstPos.x, teleporter->dstPos.z };
							}
							else if (targetSector)
							{
								endPoint.x = (targetSector->bounds[0].x + targetSector->bounds[1].x) * 0.5f;
								endPoint.z = (targetSector->bounds[0].z + targetSector->bounds[1].z) * 0.5f;
							}

							drawArrow2d_Segment(1.5f, 32.0f, startPoint, endPoint, c_connectTargetColor);
						} break;
					}
				}
			}
			TFE_RenderShared::lineDraw2d_drawLines();
		}
	}

	void renderCoordinateAxis()
	{
		const f32 len = 4096.0f;
		const f32 lenY = 50.0f;
		const u32 xAxisClr = 0xc00000ff;
		const u32 yAxisClr = 0xc000ff00;
		const u32 zAxisClr = 0xc0ff0000;

		const u32 axisColor[] = { /*left*/xAxisClr, yAxisClr, zAxisClr, /*right*/xAxisClr, yAxisClr, zAxisClr };
		const Vec3f axis[] =
		{
			// Left
			{0.1f, 0.0f, 0.0f}, {len, 0.0f, 0.0f},
			{0.0f, 0.1f, 0.0f}, {0.0f, lenY, 0.0f},
			{0.0f, 0.0f, 0.1f}, {0.0f, 0.0f, len},
			// Right
			{-len, 0.0f, 0.0f}, {0.1f, 0.0f, 0.0f},
			{0.0f,-lenY, 0.0f}, {0.0f, 0.1f, 0.0f},
			{0.0f, 0.0f, -len}, {0.0f, 0.0f, 0.1f},
		};
		TFE_RenderShared::lineDraw3d_addLines(6, 3.0f, axis, axisColor);
	}

	static bool s_gridAutoAdjust = true;

	const EditorTexture* calculateTextureCoords(const EditorWall* wall, const LevelTexture* ltex, f32 wallLengthTexels, f32 partHeight, bool flipHorz, Vec2f* uvCorners)
	{
		const EditorTexture* tex = getTexture(ltex->texIndex);
		if (!tex) { return nullptr; }
		Vec2f texScale = { 1.0f / f32(tex->width), 1.0f / f32(tex->height) };

		uvCorners[0] = { ltex->offset.x * 8.0f, (ltex->offset.z + partHeight) * 8.0f };
		uvCorners[1] = { uvCorners[0].x + wallLengthTexels, ltex->offset.z * 8.0f };
		uvCorners[0].x *= texScale.x;
		uvCorners[1].x *= texScale.x;
		uvCorners[0].z *= texScale.z;
		uvCorners[1].z *= texScale.z;
		if (flipHorz)
		{
			uvCorners[0].x = 1.0f - uvCorners[0].x;
			uvCorners[1].x = 1.0f - uvCorners[1].x;
		}
		return tex;
	}

	const EditorTexture* calculateTextureCoordsSlope(const EditorWall* wall, const LevelTexture* ltex, f32 wallLengthTexels, f32 floorH, f32 y0, f32 y1, f32 y2, f32 y3, bool flipHorz, Vec2f* uvVtx)
	{
		const EditorTexture* tex = getTexture(ltex->texIndex);
		if (!tex) { return nullptr; }
		Vec2f texScale = { 1.0f / f32(tex->width), 1.0f / f32(tex->height) };

		uvVtx[0] = { ltex->offset.x * 8.0f, (ltex->offset.z + y0 - floorH) * 8.0f };
		uvVtx[1] = { ltex->offset.x * 8.0f, (ltex->offset.z + y1 - floorH) * 8.0f };
		uvVtx[2] = { ltex->offset.x * 8.0f + wallLengthTexels, (ltex->offset.z + y3 - floorH) * 8.0f };
		uvVtx[3] = { ltex->offset.x * 8.0f + wallLengthTexels, (ltex->offset.z + y2 - floorH) * 8.0f };

		for (s32 i = 0; i < 4; i++)
		{
			uvVtx[i].x *= texScale.x;
			uvVtx[i].z *= texScale.z;

			if (flipHorz)
			{
				uvVtx[i].x = 1.0f - uvVtx[i].x;
			}
		}
		return tex;
	}

	const EditorTexture* calculateSignTextureCoords(const EditorWall* wall, const LevelTexture* baseTex, const LevelTexture* ltex, f32 wallLengthTexels, f32 partHeight, bool flipHorz, Vec2f* uvCorners)
	{
		const EditorTexture* tex = getTexture(ltex->texIndex);
		if (!tex) { return nullptr; }

		const Vec2f texScale = { 1.0f / f32(tex->width), 1.0f / f32(tex->height) };
		const Vec2f offset = { baseTex->offset.x - ltex->offset.x, ltex->offset.z };

		uvCorners[0] = { offset.x * 8.0f, (offset.z + partHeight) * 8.0f };
		uvCorners[1] = { uvCorners[0].x + wallLengthTexels, offset.z * 8.0f };
		uvCorners[0].x *= texScale.x;
		uvCorners[1].x *= texScale.x;
		uvCorners[0].z *= texScale.z;
		uvCorners[1].z *= texScale.z;
		if (flipHorz)
		{
			uvCorners[0].x = 1.0f - uvCorners[0].x;
			uvCorners[1].x = 1.0f - uvCorners[1].x;
		}
		return tex;
	}

	void drawWallLines3D_Highlighted(const EditorSector* sector, const EditorSector* next, const EditorWall* wall, f32 width, Highlight highlight, bool halfAlpha, bool showSign)
	{
		const Vec2f& v0 = sector->vtx[wall->idx[0]];
		const Vec2f& v1 = sector->vtx[wall->idx[1]];

		u32 color = c_sectorLineClr[highlight];
		if (halfAlpha)
		{
			color &= 0x00ffffff;
			color |= 0x80000000;
		}

		Vec3f lines[] =
		{
			{ v0.x, getFloorAtXZ(sector, v0), v0.z },
			{ v1.x, getFloorAtXZ(sector, v1), v1.z },
			{ v0.x, getCeilAtXZ(sector, v0), v0.z },
			{ v1.x, getCeilAtXZ(sector, v1), v1.z },

			{ v0.x, getFloorAtXZ(sector, v0), v0.z },
			{ v0.x, getCeilAtXZ(sector, v0),  v0.z },
			{ v1.x, getFloorAtXZ(sector, v1), v1.z },
			{ v1.x, getCeilAtXZ(sector, v1),  v1.z },
		};
		u32 colors[4] = { color, color, color, color };
		TFE_RenderShared::lineDraw3d_addLines(4, width, lines, colors);
				
		if (next)
		{
			u32 adjType = getAdjoinType(sector, wall);

			// Top
			if (adjType & ADJ_TYPE_TOP)
			{
				Vec3f line0[] =
				{ { v0.x, getCeilAtXZ(next, v0), v0.z },
				  { v1.x, getCeilAtXZ(next, v1), v1.z } };
				TFE_RenderShared::lineDraw3d_addLine(width, line0, &color);
			}
			// Bottom
			if (adjType & ADJ_TYPE_BOT)
			{
				Vec3f line0[] =
				{ { v0.x, getFloorAtXZ(next, v0), v0.z },
				  { v1.x, getFloorAtXZ(next, v1), v1.z } };
				TFE_RenderShared::lineDraw3d_addLine(width, line0, &color);
			}
		}

		Vec3f sgnCorners[2];
		if (showSign && computeSignCorners(sector, wall, sgnCorners))
		{
			Vec3f sgnLines[] =
			{
				{ sgnCorners[0].x, sgnCorners[0].y, sgnCorners[0].z },
				{ sgnCorners[1].x, sgnCorners[0].y, sgnCorners[1].z },
				{ sgnCorners[0].x, sgnCorners[1].y, sgnCorners[0].z },
				{ sgnCorners[1].x, sgnCorners[1].y, sgnCorners[1].z },

				{ sgnCorners[0].x, sgnCorners[0].y, sgnCorners[0].z },
				{ sgnCorners[0].x, sgnCorners[1].y, sgnCorners[0].z },
				{ sgnCorners[1].x, sgnCorners[0].y, sgnCorners[1].z },
				{ sgnCorners[1].x, sgnCorners[1].y, sgnCorners[1].z },
			};
			u32 colors[4] = { color, color, color, color };
			TFE_RenderShared::lineDraw3d_addLines(4, width, sgnLines, colors);
		}
	}

	void drawFlat3D_Highlighted(const EditorSector* sector, HitPart part, f32 width, Highlight highlight, bool halfAlpha, bool skipLines)
	{
		if (part != HP_FLOOR && part != HP_CEIL) { return; }

		u32 color = c_sectorLineClr[highlight];
		if (halfAlpha)
		{
			color &= 0x00ffffff;
			color |= 0x80000000;
		}

		f32 height = part == HP_FLOOR ? sector->floorHeight : sector->ceilHeight;
		if (!skipLines)
		{
			const size_t wallCount = sector->walls.size();
			const EditorWall* wall = sector->walls.data();
			for (size_t w = 0; w < wallCount; w++, wall++)
			{
				const Vec2f* v0 = &sector->vtx[wall->idx[0]];
				const Vec2f* v1 = &sector->vtx[wall->idx[1]];

				Vec3f lines[2] =
				{
					{ v0->x, part == HP_FLOOR ? getFloorAtXZ(sector, *v0) : getCeilAtXZ(sector, *v0), v0->z },
					{ v1->x, part == HP_FLOOR ? getFloorAtXZ(sector, *v1) : getCeilAtXZ(sector, *v1), v1->z },
				};
				TFE_RenderShared::lineDraw3d_addLine(width, lines, &color);
			}
		}

		const size_t vtxCount = sector->poly.triVtx.size();
		const Vec2f* vtx = sector->poly.triVtx.data();

		s_bufferVec3.resize(vtxCount);
		Vec3f* flatVtx = s_bufferVec3.data();
		for (size_t v = 0; v < vtxCount; v++)
		{
			flatVtx[v] = { vtx[v].x, part == HP_FLOOR ? getFloorAtXZ(sector, vtx[v]) : getCeilAtXZ(sector, vtx[v]), vtx[v].z };
		}
		triDraw3d_addColored(TRIMODE_BLEND, (u32)sector->poly.triIdx.size(), (u32)vtxCount, flatVtx, sector->poly.triIdx.data(), c_sectorPolyClr[highlight], part == HP_CEIL);
	}

	bool computeSignCorners(const EditorSector* sector, const EditorWall* wall, Vec3f* corners)
	{
		Vec2f ext[2];
		bool hasSign = false;
		if (getSignExtents(sector, wall, ext))
		{
			hasSign = true;
			const Vec2f* v0 = &sector->vtx[wall->idx[0]];
			const Vec2f* v1 = &sector->vtx[wall->idx[1]];
			Vec2f wallDir = { v1->x - v0->x, v1->z - v0->z };
			Vec2f wallNrm = { -(v1->z - v0->z), v1->x - v0->x };
			wallDir = TFE_Math::normalize(&wallDir);
			wallNrm = TFE_Math::normalize(&wallNrm);

			const f32 zBias = 0.01f;
			corners[1] = { v0->x + wallDir.x * ext[1].x - wallNrm.x*zBias, ext[0].z, v0->z + wallDir.z * ext[1].x - wallNrm.z*zBias };
			corners[0] = { v0->x + wallDir.x * ext[0].x - wallNrm.x*zBias, ext[1].z, v0->z + wallDir.z * ext[0].x - wallNrm.z*zBias };
		}
		return hasSign;
	}

	void drawGuidelineCurve3d(const Vec2f* vtx, const Guideline* guideline, const GuidelineEdge* edge, const u32* drawColor)
	{
		const Vec2f* v0 = &vtx[edge->idx[0]];
		const Vec2f* v1 = &vtx[edge->idx[1]];
		const Vec2f* c = &vtx[edge->idx[2]];

		// Cull the curve if the bounds are outside of the view.
		const f32 maxOffset = guideline->maxOffset;
		const Vec4f curveBounds = { min3(v0->x, v1->x, c->x) - maxOffset, min3(v0->z, v1->z, c->z) - maxOffset,
									max3(v0->x, v1->x, c->x) + maxOffset, max3(v0->z, v1->z, c->z) + maxOffset };
		// TODO: Culling
		// Curve.
		const Vec2f curveVtx[] =
		{
			*v0, *v1, *c
		};
		const s32 count = std::min(4, (s32)guideline->offsets.size());
		const f32* offset = guideline->offsets.data();
		TFE_RenderShared::lineDraw3d_addCurve(curveVtx, s_grid.height, drawColor[0], count, offset);
	}

	void drawGuidelineOffset3d(const Vec2f* vtx, const GuidelineEdge* edge, u32 drawColor, f32 offset)
	{
		const Vec2f* v0 = &vtx[edge->idx[0]];
		const Vec2f* v1 = &vtx[edge->idx[1]];

		Vec2f p0 = *v0;
		Vec2f p1 = *v1;
		if (offset)
		{
			Vec2f d = { v1->x - v0->x, v1->z - v0->z };
			Vec2f n = { -d.z, d.x };
			n = TFE_Math::normalize(&n);

			p0.x = v0->x + n.x*offset;
			p0.z = v0->z + n.z*offset;
			p1.x = v1->x + n.x*offset;
			p1.z = v1->z + n.z*offset;
		}

		// Cull the line if the bounds are outside of the view.
		const Vec4f lineBounds = { std::min(p0.x, p1.x), std::min(p0.z, p1.z), std::max(p0.x, p1.x), std::max(p0.z, p1.z) };
		// TODO: Cull
		// Straight line.
		const Vec3f line[] =
		{
			{ p0.x, s_grid.height, p0.z },
			{ p1.x, s_grid.height, p1.z },
		};
		u32 color = drawColor;
		if (offset != 0.0f)
		{
			u32 alpha = color >> 24u;
			alpha >>= 1; // use the same scale value as the curve shader.
			color &= 0x00ffffff;
			color |= (alpha << 24u);
		}
		TFE_RenderShared::lineDraw3d_addLine(3.0f, line, &color);
	}

	void drawPositionKnot3d(f32 width, Vec3f pos, u32 color)
	{
		Vec3f offset = { pos.x - s_camera.pos.x, pos.y - s_camera.pos.y, pos.z - s_camera.pos.z };
		f32 dist = sqrtf(offset.x*offset.x + offset.y*offset.y + offset.z*offset.z);
		f32 step = 0.005f*dist;

		Vec2f p0 = { pos.x - step, pos.z - step };
		Vec2f p1 = { pos.x + step, pos.z - step };
		Vec2f p2 = { pos.x + step, pos.z + step };
		Vec2f p3 = { pos.x - step, pos.z + step };
		Vec3f vtx[] =
		{
			{ p0.x, pos.y, p0.z },
			{ p1.x, pos.y, p1.z },
			{ p2.x, pos.y, p2.z },
			{ p3.x, pos.y, p3.z },
			{ p0.x, pos.y, p0.z },
		};

		// Draw lines through the center point.
		TFE_RenderShared::lineDraw3d_addLine(width, &vtx[0], &color);
		TFE_RenderShared::lineDraw3d_addLine(width, &vtx[1], &color);
		TFE_RenderShared::lineDraw3d_addLine(width, &vtx[2], &color);
		TFE_RenderShared::lineDraw3d_addLine(width, &vtx[3], &color);
	}

	// Draw guidelines.
	void renderGuidelines3d()
	{
		if (s_editorConfig.interfaceFlags & PIF_HIDE_GUIDELINES)
		{
			return;
		}

		const u32 colors[] = { 0xc000a5ff, 0xc000a5ff };
		const u32 hovered[] = { 0xffffa500, 0xffffa500 };
		const u32 selected[] = { 0xff0030ff, 0xff0030ff };
		const u32 hoveredSelected[] = { 0xffff00ff, 0xffff00ff };

		const u32 detail[] = { 0xc0ffa500, 0xc0ffa500 };
		const Vec4f pointBoundsWS = { s_cursor3d.x, s_cursor3d.z, s_cursor3d.x, s_cursor3d.z };

		const size_t count = s_level.guidelines.size();
		Guideline* guideLine = s_level.guidelines.data();
		lineDraw3d_setLineDrawMode();
		for (size_t i = 0; i < count; i++, guideLine++)
		{
			// Cull the entire guideline set.
			// TODO: Culling
			if (guideLine->flags & GLFLAG_LIMIT_HEIGHT)
			{
				if (s_grid.height < guideLine->minHeight || s_grid.height > guideLine->maxHeight)
				{
					continue;
				}
			}

			const size_t edgeCount = guideLine->edge.size();
			const Vec2f* vtx = guideLine->vtx.data();
			const GuidelineEdge* edge = guideLine->edge.data();
			const size_t offsetCount = guideLine->offsets.size();

			const u32* drawColor = colors;
			if (s_hoveredGuideline == i && s_curGuideline == i)
			{
				drawColor = hoveredSelected;
			}
			else if (s_hoveredGuideline == i)
			{
				drawColor = hovered;
			}
			else if (s_curGuideline == i)
			{
				drawColor = selected;
			}

			for (size_t e = 0; e < edgeCount; e++, edge++)
			{
				if (edge->idx[2] >= 0) // Curve.
				{
					drawGuidelineCurve3d(vtx, guideLine, edge, drawColor);
				}
				else
				{
					drawGuidelineOffset3d(vtx, edge, drawColor[0], 0.0f);

					const f32* offset = guideLine->offsets.data();
					for (size_t o = 0; o < offsetCount; o++)
					{
						if (offset[o] == 0.0f) { continue; }
						drawGuidelineOffset3d(vtx, edge, drawColor[0], offset[o]);
					}
				}
			}
		}
		lineDraw3d_setLineDrawMode();

		// Draw guideline knots.
		guideLine = s_level.guidelines.data();
		for (size_t g = 0; g < count; g++, guideLine++)
		{
			// Cull the entire guideline set.
			// TODO: Culling.
			if (guideLine->flags & GLFLAG_DISABLE_SNAPPING) { continue; }  // Don't show knots if snapping is disabled.
			if ((guideLine->flags & GLFLAG_LIMIT_HEIGHT) && (s_grid.height < guideLine->minHeight || s_grid.height > guideLine->maxHeight))
			{
				continue;
			}
			const size_t knotCount = guideLine->knots.size();
			const Vec4f* knot = guideLine->knots.data();
			for (size_t k = 0; k < knotCount; k++, knot++)
			{
				const Vec3f pos = { knot->x, s_grid.height, knot->z };
				drawPositionKnot3d(2.0f, pos, 0xffffa500);
			}

			const size_t subdivCount = guideLine->subdiv.size();
			const size_t offsetCount = guideLine->offsets.size();
			const GuidelineSubDiv* subdiv = guideLine->subdiv.data();
			const f32* offsets = guideLine->offsets.data();
			const Vec2f* vtx = guideLine->vtx.data();
			for (size_t k = 0; k < subdivCount; k++, subdiv++)
			{
				const s32 e = subdiv->edge;
				const f32 u = subdiv->param;
				const GuidelineEdge* edge = &guideLine->edge[e];
				const Vec2f v0 = vtx[edge->idx[0]];
				const Vec2f v1 = vtx[edge->idx[1]];
				Vec2f pos, nrm;
				if (edge->idx[2] >= 0) // Curve
				{
					const Vec2f c = vtx[edge->idx[2]];
					evaluateQuadraticBezier(v0, v1, c, u, &pos, &nrm);
				}
				else // Line
				{
					pos.x = v0.x*(1.0f - u) + v1.x*u;
					pos.z = v0.z*(1.0f - u) + v1.z*u;
					nrm.x = -(v1.z - v0.z);
					nrm.z = v1.x - v0.x;
					nrm = TFE_Math::normalize(&nrm);
				}

				drawPositionKnot3d(1.5f, { pos.x, s_grid.height, pos.z }, 0x8000a5ff);
				for (size_t o = 0; o < offsetCount; o++)
				{
					const Vec2f offsetPos = { pos.x + nrm.x * offsets[o], pos.z + nrm.z * offsets[o] };
					drawPositionKnot3d(1.5f, { offsetPos.x, s_grid.height, offsetPos.z }, 0x8000a5ff);
				}
			}
		}
	}
		
	void drawWallColor3d_Highlighted(const EditorSector* sector, const Vec2f* vtx, const EditorWall* wall, u32 color, HitPart part)
	{
		const Vec2f* v0 = &vtx[wall->idx[0]];
		const Vec2f* v1 = &vtx[wall->idx[1]];

		Vec3f sgnCorners[2];
		if (part == HP_SIGN && computeSignCorners(sector, wall, sgnCorners))
		{
			triDraw3d_addQuadColored(TRIMODE_BLEND, sgnCorners, color);
		}
		else if (wall->adjoinId < 0 && (part == HP_COUNT || part == HP_MID))
		{
			Vec3f vtx[] =
			{
				{ v0->x, getFloorAtXZ(sector, *v0), v0->z },
				{ v0->x, getCeilAtXZ(sector, *v0), v0->z },
				{ v1->x, getCeilAtXZ(sector, *v1), v1->z },
				{ v1->x, getFloorAtXZ(sector, *v1), v1->z },
			};
			// No adjoin - a single quad.
			TFE_RenderShared::triDraw3d_addDeformedQuadColored(TRIMODE_BLEND, vtx, color);
		}
		else if (wall->adjoinId >= 0)
		{
			const u32 adjType = getAdjoinType(sector, wall);
			EditorSector* next = &s_level.sectors[wall->adjoinId];

			// lower
			f32 nextFloor = s_level.sectors[wall->adjoinId].floorHeight;
			if ((adjType & ADJ_TYPE_BOT) && (part == HP_COUNT || part == HP_BOT))
			{
				Vec3f vtx[] =
				{
					{ v0->x, getFloorAtXZ(sector, *v0), v0->z },
					{ v0->x, getFloorAtXZ(next,   *v0), v0->z },
					{ v1->x, getFloorAtXZ(next,   *v1), v1->z },
					{ v1->x, getFloorAtXZ(sector, *v1), v1->z },
				};
				TFE_RenderShared::triDraw3d_addDeformedQuadColored(TRIMODE_BLEND, vtx, color);
			}

			// upper
			f32 nextCeil = s_level.sectors[wall->adjoinId].ceilHeight;
			if ((adjType & ADJ_TYPE_TOP) && (part == HP_COUNT || part == HP_TOP))
			{
				Vec3f vtx[] =
				{
					{ v0->x, getCeilAtXZ(next,   *v0), v0->z },
					{ v0->x, getCeilAtXZ(sector, *v0), v0->z },
					{ v1->x, getCeilAtXZ(sector, *v1), v1->z },
					{ v1->x, getCeilAtXZ(next,   *v1), v1->z },
				};
				TFE_RenderShared::triDraw3d_addDeformedQuadColored(TRIMODE_BLEND, vtx, color);
			}

			// mask wall or highlight.
			if (part == HP_FLOOR || part == HP_CEIL || part == HP_MID)
			{
				Vec3f vtx[] =
				{
					{ v0->x, std::max(getFloorAtXZ(next, *v0), getFloorAtXZ(sector, *v0)), v0->z },
					{ v0->x, std::min(getCeilAtXZ(next, *v0), getCeilAtXZ(sector, *v0)), v0->z },
					{ v1->x, std::min(getCeilAtXZ(next, *v1), getCeilAtXZ(sector, *v1)), v1->z },
					{ v1->x, std::max(getFloorAtXZ(next, *v1), getFloorAtXZ(sector, *v1)), v1->z },
				};
				TFE_RenderShared::triDraw3d_addDeformedQuadColored(TRIMODE_BLEND, vtx, color);
			}
		}
	}

	void drawWallLines3D(const EditorSector* sector, const EditorSector* next, const EditorWall* wall, f32 width, Highlight highlight, bool halfAlpha)
	{
		f32 bias = 1.0f / 1024.0f;
		f32 floorBias = (s_camera.pos.y >= sector->floorHeight) ? bias : -bias;
		f32 ceilBias  = (s_camera.pos.y <= sector->ceilHeight) ? -bias : bias;

		const Vec2f& v0 = sector->vtx[wall->idx[0]];
		const Vec2f& v1 = sector->vtx[wall->idx[1]];

		const Vec3f vtxSlope[] =
		{
			{ v0.x, getFloorAtXZ(sector, v0) + floorBias, v0.z },
			{ v0.x, getCeilAtXZ(sector, v0) + ceilBias, v0.z },
			{ v1.x, getCeilAtXZ(sector, v1) + ceilBias, v1.z },
			{ v1.x, getFloorAtXZ(sector, v1) + floorBias, v1.z },
		};
				
		u32 color = c_sectorLineClr[highlight];
		if (halfAlpha)
		{
			color &= 0x00ffffff;
			color |= 0x80000000;
		}

		if ((s_editFlags & LEF_SECTOR_EDGES) || !next || next->floorHeight != sector->floorHeight || s_camera.pos.y > sector->floorHeight)
		{
			Vec3f line0[] = { vtxSlope[0], vtxSlope[3] };
			TFE_RenderShared::lineDraw3d_addLine(width, line0, &color);
		}
		if ((s_editFlags & LEF_SECTOR_EDGES) || !next || next->ceilHeight != sector->ceilHeight || s_camera.pos.y < sector->ceilHeight)
		{
			Vec3f line1[] = { vtxSlope[1], vtxSlope[2] };
			TFE_RenderShared::lineDraw3d_addLine(width, line1, &color);
		}

		const u32 adjType = getAdjoinType(sector, wall);
		
		Vec2f vtx[] = { v0, v1 };
		s32 count = highlight == HL_NONE ? 1 : 2;
		for (s32 v = 0; v < count; v++)
		{
			if (s_editFlags & LEF_SECTOR_EDGES)
			{
				Vec3f line2[] =
				{ { vtx[v].x, getFloorAtXZ(sector, vtx[v]) + floorBias, vtx[v].z },
				  { vtx[v].x, getCeilAtXZ(sector, vtx[v]) + ceilBias, vtx[v].z } };
				TFE_RenderShared::lineDraw3d_addLine(width, line2, &color);
			}
			else
			{
				// Show edges only where geometry exists.
				if (wall->adjoinId < 0)
				{
					Vec3f line2[] =
					{ { vtx[v].x, getFloorAtXZ(sector, vtx[v]) + floorBias, vtx[v].z },
					  { vtx[v].x, getCeilAtXZ(sector, vtx[v]) + ceilBias, vtx[v].z } };
					TFE_RenderShared::lineDraw3d_addLine(width, line2, &color);
				}
				else if (next)
				{
					// Top
					if (adjType & ADJ_TYPE_TOP)
					{
						f32 topBias = (s_camera.pos.y <= next->ceilHeight) ? -bias : bias;

						Vec3f line0[] =
						{ { vtx[v].x, getCeilAtXZ(next, vtx[v]) + topBias, vtx[v].z },
						  { vtx[v].x, getCeilAtXZ(next, vtx[v]) + topBias, vtx[v].z } };
						TFE_RenderShared::lineDraw3d_addLine(width, line0, &color);
					}
					// Bottom
					if (adjType & ADJ_TYPE_BOT)
					{
						f32 botBias = (s_camera.pos.y >= next->floorHeight) ? bias : -bias;

						Vec3f line0[] =
						{ { vtx[v].x, getFloorAtXZ(next, vtx[v]) + botBias, vtx[v].z },
						  { vtx[v].x, getFloorAtXZ(next, vtx[v]) + botBias, vtx[v].z } };
						TFE_RenderShared::lineDraw3d_addLine(width, line0, &color);
					}
				}
			}
		}

		if (next)
		{
			// Top
			if ((adjType & ADJ_TYPE_TOP) && next->layer != s_curLayer)
			{
				f32 topBias = (s_camera.pos.y <= next->ceilHeight) ? -bias : bias;

				Vec3f line0[] =
				{ { v0.x, getCeilAtXZ(next, v0) + topBias, v0.z },
				  { v1.x, getCeilAtXZ(next, v1) + topBias, v1.z } };
				TFE_RenderShared::lineDraw3d_addLine(width, line0, &color);
			}
			// Bottom
			if ((adjType & ADJ_TYPE_BOT) && next->layer != s_curLayer)
			{
				f32 botBias = (s_camera.pos.y >= next->floorHeight) ? bias : -bias;

				Vec3f line0[] =
				{ { v0.x, getFloorAtXZ(next, v0) + botBias, v0.z },
				  { v1.x, getFloorAtXZ(next, v1) + botBias, v1.z } };
				TFE_RenderShared::lineDraw3d_addLine(width, line0, &color);
			}
		}
	}
			
	void drawSectorShape2D()
	{
		u32 color[] = { 0xffffffff, 0xffffffff };
		u32 colorDark[] = { 0x80ffffff, 0x80ffffff };
		u32 colorDarkOrange[] = { 0x8000a5ff, 0x8000a5ff };
		u32 curveColor[] = { 0xffff00ff, 0xffff00ff };
		f32 width = 1.5f;
		if (s_geoEdit.drawStarted && s_geoEdit.drawMode == DMODE_RECT)
		{
			Vec2f p[4];
			getGridOrientedRect(s_geoEdit.shape[0], s_geoEdit.shape[1], p);

			Vec2f vtx[] =
			{
				{ p[0].x * s_viewportTrans2d.x + s_viewportTrans2d.y, p[0].z * s_viewportTrans2d.z + s_viewportTrans2d.w },
				{ p[1].x * s_viewportTrans2d.x + s_viewportTrans2d.y, p[1].z * s_viewportTrans2d.z + s_viewportTrans2d.w },
				{ p[2].x * s_viewportTrans2d.x + s_viewportTrans2d.y, p[2].z * s_viewportTrans2d.z + s_viewportTrans2d.w },
				{ p[3].x * s_viewportTrans2d.x + s_viewportTrans2d.y, p[3].z * s_viewportTrans2d.z + s_viewportTrans2d.w },
				{ p[0].x * s_viewportTrans2d.x + s_viewportTrans2d.y, p[0].z * s_viewportTrans2d.z + s_viewportTrans2d.w }
			};
			TFE_RenderShared::lineDraw2d_addLine(width, &vtx[0], color);
			TFE_RenderShared::lineDraw2d_addLine(width, &vtx[1], color);
			TFE_RenderShared::lineDraw2d_addLine(width, &vtx[2], color);
			TFE_RenderShared::lineDraw2d_addLine(width, &vtx[3], color);
		}
		else if (s_geoEdit.drawStarted && s_geoEdit.drawMode == DMODE_SHAPE)
		{
			const s32 vtxCount = (s32)s_geoEdit.shape.size();
			const Vec2f* vtx = s_geoEdit.shape.data();
			for (s32 v = 0; v < vtxCount - 1; v++)
			{
				const Vec2f lineVtx[] =
				{
					{ vtx[v].x * s_viewportTrans2d.x + s_viewportTrans2d.y, vtx[v].z * s_viewportTrans2d.z + s_viewportTrans2d.w },
					{ vtx[v + 1].x * s_viewportTrans2d.x + s_viewportTrans2d.y, vtx[v + 1].z * s_viewportTrans2d.z + s_viewportTrans2d.w }
				};
				TFE_RenderShared::lineDraw2d_addLine(width, lineVtx, color);
			}
			// Draw from the last vertex to curPos.
			const Vec2f lineVtx[] =
			{
				{vtx[vtxCount - 1].x * s_viewportTrans2d.x + s_viewportTrans2d.y, vtx[vtxCount - 1].z * s_viewportTrans2d.z + s_viewportTrans2d.w},
			    {s_geoEdit.drawCurPos.x * s_viewportTrans2d.x + s_viewportTrans2d.y,  s_geoEdit.drawCurPos.z * s_viewportTrans2d.z + s_viewportTrans2d.w}
			};
			TFE_RenderShared::lineDraw2d_addLine(width, lineVtx, (s_viewportRenderFlags & VRF_CURVE_MOD) ? curveColor : color);
		}
		else if (s_geoEdit.drawStarted && s_geoEdit.drawMode == DMODE_CURVE_CONTROL)
		{
			// Base shape.
			const s32 vtxCount = (s32)s_geoEdit.shape.size();
			const Vec2f* vtx = s_geoEdit.shape.data();
			for (s32 v = 0; v < vtxCount - 1; v++)
			{
				const Vec2f lineVtx[] =
				{
					{ vtx[v].x * s_viewportTrans2d.x + s_viewportTrans2d.y, vtx[v].z * s_viewportTrans2d.z + s_viewportTrans2d.w },
					{ vtx[v + 1].x * s_viewportTrans2d.x + s_viewportTrans2d.y, vtx[v + 1].z * s_viewportTrans2d.z + s_viewportTrans2d.w }
				};
				TFE_RenderShared::lineDraw2d_addLine(width, lineVtx, v == vtxCount - 2 ? colorDark : color);
			}
			// Support triangle.
			{
				const Vec2f lineVtx0[] =
				{
					{ vtx[vtxCount - 2].x * s_viewportTrans2d.x + s_viewportTrans2d.y, vtx[vtxCount - 2].z * s_viewportTrans2d.z + s_viewportTrans2d.w },
					{ s_geoEdit.drawCurPos.x * s_viewportTrans2d.x + s_viewportTrans2d.y, s_geoEdit.drawCurPos.z * s_viewportTrans2d.z + s_viewportTrans2d.w }
				};
				TFE_RenderShared::lineDraw2d_addLine(width, lineVtx0, colorDarkOrange);

				const Vec2f lineVtx1[] =
				{
					{ vtx[vtxCount - 1].x * s_viewportTrans2d.x + s_viewportTrans2d.y, vtx[vtxCount - 1].z * s_viewportTrans2d.z + s_viewportTrans2d.w },
					{ s_geoEdit.drawCurPos.x * s_viewportTrans2d.x + s_viewportTrans2d.y, s_geoEdit.drawCurPos.z * s_viewportTrans2d.z + s_viewportTrans2d.w }
				};
				TFE_RenderShared::lineDraw2d_addLine(width, lineVtx1, colorDarkOrange);
			}
			// Curve.
			std::vector<Vec2f> curve;
			curve.push_back(vtx[vtxCount - 2]);
			buildCurve(vtx[vtxCount - 2], vtx[vtxCount - 1], s_geoEdit.drawCurPos, &curve);

			const s32 vtxCountCurve = (s32)curve.size();
			const Vec2f* vtxCurve = curve.data();
			for (s32 v = 0; v < vtxCountCurve - 1; v++)
			{
				const Vec2f lineVtx[] =
				{
					{ vtxCurve[v].x * s_viewportTrans2d.x + s_viewportTrans2d.y, vtxCurve[v].z * s_viewportTrans2d.z + s_viewportTrans2d.w },
					{ vtxCurve[v + 1].x * s_viewportTrans2d.x + s_viewportTrans2d.y, vtxCurve[v + 1].z * s_viewportTrans2d.z + s_viewportTrans2d.w }
				};
				TFE_RenderShared::lineDraw2d_addLine(width, lineVtx, curveColor);
			}
		}

		// Draw the cursor.
		drawEditCursor2D(&s_geoEdit.drawCurPos);
	}
	
	void drawExtrudeShape3D()
	{
		const u32 color = 0xffffffff;
		const u32 colorError = 0xff2020ff;
		const Project* project = project_get();
		const bool allowSlopes = project->featureSet != FSET_VANILLA || project->game != Game_Dark_Forces;

		if (s_geoEdit.drawStarted && s_geoEdit.drawMode == DMODE_RECT)
		{
			const Vec3f p0 = extrudePoint2dTo3d(s_geoEdit.shape[0]);
			const Vec3f p1 = extrudePoint2dTo3d(s_geoEdit.shape[1]);

			Vec3f vtx[] =
			{
				{ p0.x, p0.y, p0.z },
				{ p1.x, p0.y, p1.z },
				{ p1.x, p1.y, p1.z },
				{ p0.x, p1.y, p0.z },
				{ p0.x, p0.y, p0.z }
			};
			TFE_RenderShared::lineDraw3d_addLine(3.0f, &vtx[0], &color);
			TFE_RenderShared::lineDraw3d_addLine(3.0f, &vtx[1], &color);
			TFE_RenderShared::lineDraw3d_addLine(3.0f, &vtx[2], &color);
			TFE_RenderShared::lineDraw3d_addLine(3.0f, &vtx[3], &color);
		}
		else if (s_geoEdit.drawStarted && s_geoEdit.drawMode == DMODE_RECT_VERT)
		{
			const Vec2f vtx[] =
			{
				{ s_geoEdit.shape[0].x, s_geoEdit.shape[0].z },
				{ s_geoEdit.shape[1].x, s_geoEdit.shape[0].z },
				{ s_geoEdit.shape[1].x, s_geoEdit.shape[1].z },
				{ s_geoEdit.shape[0].x, s_geoEdit.shape[1].z }
			};

			for (s32 v = 0; v < 4; v++)
			{
				const s32 a = v;
				const s32 b = (v + 1) & 3;
				// Base
				const Vec3f base[] =
				{
					extrudePoint2dTo3d(vtx[a], s_geoEdit.drawHeight[0]),
					extrudePoint2dTo3d(vtx[b], s_geoEdit.drawHeight[0]),
				};
				TFE_RenderShared::lineDraw3d_addLine(3.0f, base, &color);
				// Top
				const Vec3f top[] =
				{
					extrudePoint2dTo3d(vtx[a], s_geoEdit.drawHeight[1]),
					extrudePoint2dTo3d(vtx[b], s_geoEdit.drawHeight[1]),
				};
				TFE_RenderShared::lineDraw3d_addLine(3.0f, top, &color);
				// Edge
				const Vec3f edge[] =
				{
					extrudePoint2dTo3d(vtx[a], s_geoEdit.drawHeight[0]),
					extrudePoint2dTo3d(vtx[a], s_geoEdit.drawHeight[1]),
				};
				TFE_RenderShared::lineDraw3d_addLine(3.0f, edge, &color);
			}

			// Draw solid bottom.
			const Vec3f p0 = extrudePoint2dTo3d(s_geoEdit.shape[0], s_geoEdit.drawHeight[0]);
			const Vec3f p1 = extrudePoint2dTo3d(s_geoEdit.shape[1], s_geoEdit.drawHeight[0]);
			const Vec3f baseVtx[4] =
			{
				{ p0.x, p0.y, p0.z },
				{ p1.x, p0.y, p1.z },
				{ p1.x, p1.y, p1.z },
				{ p0.x, p1.y, p0.z }
			};
			const s32 idx[6] =
			{
				0, 1, 2,
				0, 2, 3
			};
			triDraw3d_addColored(TRIMODE_BLEND, 6, 4, baseVtx, idx, SCOLOR_POLY_NORM, false);
		}
		else if (s_geoEdit.drawStarted && s_geoEdit.drawMode == DMODE_SHAPE)
		{
			const s32 vtxCount = (s32)s_geoEdit.shape.size();
			const Vec2f* vtx = s_geoEdit.shape.data();
			for (s32 v = 0; v < vtxCount - 1; v++)
			{
				const Vec3f lineVtx[] =
				{
					extrudePoint2dTo3d(vtx[v]),
					extrudePoint2dTo3d(vtx[v + 1])
				};
				const bool error = !allowSlopes && vtx[v].x != vtx[v+1].x && vtx[v].z != vtx[v+1].z;
				TFE_RenderShared::lineDraw3d_addLine(3.0f, lineVtx, error ? &colorError : &color);
			}
			// Draw from the last vertex to curPos.
			const Vec3f lineVtx[] =
			{
				extrudePoint2dTo3d(vtx[vtxCount-1]),
				extrudePoint2dTo3d(s_geoEdit.drawCurPos)
			};
			const bool error = !allowSlopes && vtx[vtxCount-1].x != s_geoEdit.drawCurPos.x && vtx[vtxCount-1].z != s_geoEdit.drawCurPos.z;
			TFE_RenderShared::lineDraw3d_addLine(3.0f, lineVtx, error ? &colorError : &color);
		}
		else if (s_geoEdit.drawStarted && s_geoEdit.drawMode == DMODE_SHAPE_VERT)
		{
			const s32 vtxCount = (s32)s_geoEdit.shape.size();
			const Vec2f* vtx = s_geoEdit.shape.data();
			for (s32 v = 0; v < vtxCount; v++)
			{
				const s32 a = v;
				const s32 b = (v + 1) % vtxCount;
				// Draw the faces.
				const Vec3f lineVtx0[] =
				{
					extrudePoint2dTo3d(vtx[a], s_geoEdit.drawHeight[0]),
					extrudePoint2dTo3d(vtx[b], s_geoEdit.drawHeight[0])
				};
				const Vec3f lineVtx1[] =
				{
					extrudePoint2dTo3d(vtx[a], s_geoEdit.drawHeight[1]),
					extrudePoint2dTo3d(vtx[b], s_geoEdit.drawHeight[1])
				};
				TFE_RenderShared::lineDraw3d_addLine(3.0f, lineVtx0, &color);
				TFE_RenderShared::lineDraw3d_addLine(3.0f, lineVtx1, &color);
				// Draw the edges.
				const Vec3f lineVtxEdge[] =
				{
					extrudePoint2dTo3d(vtx[a], s_geoEdit.drawHeight[0]),
					extrudePoint2dTo3d(vtx[a], s_geoEdit.drawHeight[1])
				};
				TFE_RenderShared::lineDraw3d_addLine(3.0f, lineVtxEdge, &color);
			}
			// Draw the polygon
			const s32 triVtxCount = (s32)s_geoEdit.shapePolygon.triVtx.size();
			const Vec2f* triVtx = s_geoEdit.shapePolygon.triVtx.data();
			s_bufferVec3.resize(triVtxCount);
			Vec3f* flatVtx = s_bufferVec3.data();
			for (size_t v = 0; v < triVtxCount; v++)
			{
				flatVtx[v] = extrudePoint2dTo3d(triVtx[v], s_geoEdit.drawHeight[0]);
			}
			triDraw3d_addColored(TRIMODE_BLEND, (u32)s_geoEdit.shapePolygon.triIdx.size(), (u32)triVtxCount, flatVtx, s_geoEdit.shapePolygon.triIdx.data(), SCOLOR_POLY_NORM, false);
		}
	}

	void drawSectorShape3D()
	{
		u32 color = 0xffffffff;
		u32 colorDark = 0x80ffffff;
		u32 colorDarkOrange = 0x8000a5ff;
		u32 curveColor = 0xffff00ff;
		if (s_geoEdit.drawStarted && s_geoEdit.drawMode == DMODE_RECT)
		{
			Vec2f rect[4];
			getGridOrientedRect(s_geoEdit.shape[0], s_geoEdit.shape[1], rect);

			// Draw the rect on the grid itself.
			Vec3f vtx[] =
			{
				{ rect[0].x, s_geoEdit.drawHeight[0], rect[0].z },
				{ rect[1].x, s_geoEdit.drawHeight[0], rect[1].z },
				{ rect[2].x, s_geoEdit.drawHeight[0], rect[2].z },
				{ rect[3].x, s_geoEdit.drawHeight[0], rect[3].z },
				{ rect[0].x, s_geoEdit.drawHeight[0], rect[0].z }
			};
			TFE_RenderShared::lineDraw3d_addLine(3.0f, &vtx[0], &color);
			TFE_RenderShared::lineDraw3d_addLine(3.0f, &vtx[1], &color);
			TFE_RenderShared::lineDraw3d_addLine(3.0f, &vtx[2], &color);
			TFE_RenderShared::lineDraw3d_addLine(3.0f, &vtx[3], &color);
		}
		else if (s_geoEdit.drawStarted && s_geoEdit.drawMode == DMODE_RECT_VERT)
		{
			Vec2f vtx[4];
			getGridOrientedRect(s_geoEdit.shape[0], s_geoEdit.shape[1], vtx);

			for (s32 v = 0; v < 4; v++)
			{
				s32 a = v;
				s32 b = (v + 1) & 3;

				// Floor
				const Vec3f floor[] =
				{
					{ vtx[a].x, s_geoEdit.drawHeight[0], vtx[a].z },
					{ vtx[b].x, s_geoEdit.drawHeight[0], vtx[b].z },
				};
				TFE_RenderShared::lineDraw3d_addLine(3.0f, floor, &color);

				// Ceiling
				const Vec3f ceil[] =
				{
					{ vtx[a].x, s_geoEdit.drawHeight[1], vtx[a].z },
					{ vtx[b].x, s_geoEdit.drawHeight[1], vtx[b].z },
				};
				TFE_RenderShared::lineDraw3d_addLine(3.0f, ceil, &color);

				// Edge
				const Vec3f edge[] =
				{
					{ vtx[a].x, s_geoEdit.drawHeight[0], vtx[a].z },
					{ vtx[a].x, s_geoEdit.drawHeight[1], vtx[a].z },
				};
				TFE_RenderShared::lineDraw3d_addLine(3.0f, edge, &color);
			}

			// Draw solid bottom.
			Vec3f baseVtx[4] =
			{
				{ vtx[0].x, s_geoEdit.drawHeight[0], vtx[0].z },
				{ vtx[1].x, s_geoEdit.drawHeight[0], vtx[1].z },
				{ vtx[2].x, s_geoEdit.drawHeight[0], vtx[2].z },
				{ vtx[3].x, s_geoEdit.drawHeight[0], vtx[3].z }
			};
			s32 idx[6]=
			{
				0, 1, 2,
				0, 2, 3
			};
			triDraw3d_addColored(TRIMODE_BLEND, 6, 4, baseVtx, idx, SCOLOR_POLY_NORM, false);
		}
		else if (s_geoEdit.drawStarted && s_geoEdit.drawMode == DMODE_SHAPE)
		{
			const s32 vtxCount = (s32)s_geoEdit.shape.size();
			const Vec2f* vtx = s_geoEdit.shape.data();
			for (s32 v = 0; v < vtxCount - 1; v++)
			{
				const Vec3f lineVtx[] = { {vtx[v].x, s_geoEdit.drawHeight[0], vtx[v].z}, {vtx[v+1].x, s_geoEdit.drawHeight[0], vtx[v+1].z} };
				TFE_RenderShared::lineDraw3d_addLine(3.0f, lineVtx, &color);
			}
			// Draw from the last vertex to curPos.
			const Vec3f lineVtx[] = { {vtx[vtxCount-1].x, s_geoEdit.drawHeight[0], vtx[vtxCount-1].z}, {s_geoEdit.drawCurPos.x, s_geoEdit.drawHeight[0], s_geoEdit.drawCurPos.z} };
			TFE_RenderShared::lineDraw3d_addLine(3.0f, lineVtx, (s_viewportRenderFlags & VRF_CURVE_MOD) ? &curveColor : &color);
		}
		else if (s_geoEdit.drawStarted && s_geoEdit.drawMode == DMODE_CURVE_CONTROL)
		{
			// Base shape.
			const s32 vtxCount = (s32)s_geoEdit.shape.size();
			const Vec2f* vtx = s_geoEdit.shape.data();
			for (s32 v = 0; v < vtxCount - 1; v++)
			{
				const Vec3f lineVtx[] = { {vtx[v].x, s_geoEdit.drawHeight[0], vtx[v].z}, {vtx[v + 1].x, s_geoEdit.drawHeight[0], vtx[v + 1].z} };
				TFE_RenderShared::lineDraw3d_addLine(3.0f, lineVtx, (v == vtxCount - 2) ? &colorDark : &color);
			}
			// Support triangle.
			{
				const Vec3f lineVtx0[] = { {vtx[vtxCount - 2].x, s_geoEdit.drawHeight[0], vtx[vtxCount - 2].z}, {s_geoEdit.drawCurPos.x, s_geoEdit.drawHeight[0], s_geoEdit.drawCurPos.z} };
				const Vec3f lineVtx1[] = { {vtx[vtxCount - 1].x, s_geoEdit.drawHeight[0], vtx[vtxCount - 1].z}, {s_geoEdit.drawCurPos.x, s_geoEdit.drawHeight[0], s_geoEdit.drawCurPos.z} };
				TFE_RenderShared::lineDraw3d_addLine(3.0f, lineVtx0, &colorDarkOrange);
				TFE_RenderShared::lineDraw3d_addLine(3.0f, lineVtx1, &colorDarkOrange);
			}
			// Curve.
			std::vector<Vec2f> curve;
			curve.push_back(vtx[vtxCount - 2]);
			buildCurve(vtx[vtxCount - 2], vtx[vtxCount - 1], s_geoEdit.drawCurPos, &curve);

			const s32 vtxCountCurve = (s32)curve.size();
			const Vec2f* vtxCurve = curve.data();
			for (s32 v = 0; v < vtxCountCurve - 1; v++)
			{
				const Vec3f lineVtx[] = { {vtxCurve[v].x, s_geoEdit.drawHeight[0], vtxCurve[v].z}, {vtxCurve[v + 1].x, s_geoEdit.drawHeight[0], vtxCurve[v + 1].z} };
				TFE_RenderShared::lineDraw3d_addLine(3.0f, lineVtx, &curveColor);
			}
		}
		else if (s_geoEdit.drawStarted && s_geoEdit.drawMode == DMODE_SHAPE_VERT)
		{
			const s32 vtxCount = (s32)s_geoEdit.shape.size();
			const Vec2f* vtx = s_geoEdit.shape.data();
			for (s32 v = 0; v < vtxCount; v++)
			{
				s32 a = v;
				s32 b = (v + 1) % vtxCount;

				// Draw the faces.
				const Vec3f lineVtx0[] = { {vtx[a].x, s_geoEdit.drawHeight[0], vtx[a].z}, {vtx[b].x, s_geoEdit.drawHeight[0], vtx[b].z} };
				const Vec3f lineVtx1[] = { {vtx[a].x, s_geoEdit.drawHeight[1], vtx[a].z}, {vtx[b].x, s_geoEdit.drawHeight[1], vtx[b].z} };
				TFE_RenderShared::lineDraw3d_addLine(3.0f, lineVtx0, &color);
				TFE_RenderShared::lineDraw3d_addLine(3.0f, lineVtx1, &color);

				// Draw the edges.
				const Vec3f lineVtxEdge[] = { {vtx[a].x, s_geoEdit.drawHeight[0], vtx[a].z}, {vtx[a].x, s_geoEdit.drawHeight[1], vtx[a].z} };
				TFE_RenderShared::lineDraw3d_addLine(3.0f, lineVtxEdge, &color);
			}
			// Draw the polygon
			const s32 triVtxCount = (s32)s_geoEdit.shapePolygon.triVtx.size();
			const Vec2f* triVtx = s_geoEdit.shapePolygon.triVtx.data();

			s_bufferVec3.resize(triVtxCount);
			Vec3f* flatVtx = s_bufferVec3.data();

			for (size_t v = 0; v < triVtxCount; v++)
			{
				flatVtx[v] = { triVtx[v].x, s_geoEdit.drawHeight[0], triVtx[v].z };
			}
			triDraw3d_addColored(TRIMODE_BLEND, (u32)s_geoEdit.shapePolygon.triIdx.size(), (u32)triVtxCount, flatVtx, s_geoEdit.shapePolygon.triIdx.data(), SCOLOR_POLY_NORM, false);
		}
	}

	void drawSelectedSector3D()
	{
		FeatureId* list = nullptr;
		const u32 count = selection_getList(list);

		EditorSector* hoveredSector = nullptr;
		selection_getSector(SEL_INDEX_HOVERED, hoveredSector);

		bool hoverAndSelect = false;
		for (u32 i = 0; i < count; i++)
		{
			EditorSector* featureSector = unpackFeatureId(list[i]);
			hoverAndSelect = featureSector == hoveredSector;

			drawFlat3D_Highlighted(featureSector, HP_FLOOR, 3.5f, HL_SELECTED, false, true);
			drawFlat3D_Highlighted(featureSector, HP_CEIL, 3.5f, HL_SELECTED, false, true);
			const size_t wallCount = featureSector->walls.size();
			const EditorWall* wall = featureSector->walls.data();
			for (size_t w = 0; w < wallCount; w++, wall++)
			{
				EditorSector* next = wall->adjoinId < 0 ? nullptr : &s_level.sectors[wall->adjoinId];
				drawWallLines3D_Highlighted(featureSector, next, wall, 3.5f, HL_SELECTED, false);
			}
		}
		if (hoveredSector)
		{
			drawFlat3D_Highlighted(hoveredSector, HP_FLOOR, 3.5f, HL_HOVERED, hoverAndSelect, true);
			drawFlat3D_Highlighted(hoveredSector, HP_CEIL, 3.5f, HL_HOVERED, hoverAndSelect, true);
			const size_t wallCount = hoveredSector->walls.size();
			const EditorWall* wall = hoveredSector->walls.data();
			for (size_t w = 0; w < wallCount; w++, wall++)
			{
				EditorSector* next = wall->adjoinId < 0 ? nullptr : &s_level.sectors[wall->adjoinId];
				drawWallLines3D_Highlighted(hoveredSector, next, wall, 3.5f, HL_HOVERED, hoverAndSelect);
			}
		}
	}

	void drawSelectedSurface3D()
	{
		FeatureId* list = nullptr;
		const u32 count = selection_getList(list);

		EditorSector* hoveredSector = nullptr;
		s32 hoveredFeatureIndex = -1;
		HitPart hoveredPart = HP_NONE;
		if (selection_hasHovered())
		{
			selection_getSurface(SEL_INDEX_HOVERED, hoveredSector, hoveredFeatureIndex, &hoveredPart);
		}

		bool hoverAndSelectFlat = false;
		bool hoverAndSelect = false;
		for (size_t i = 0; i < count; i++)
		{
			s32 featureIndex;
			HitPart featureData;
			EditorSector* featureSector = unpackFeatureId(list[i], &featureIndex, (s32*)&featureData);

			// In 3D, the floor and ceiling are surfaces too.
			hoverAndSelectFlat = featureSector == hoveredSector;
			hoverAndSelect = featureIndex == hoveredFeatureIndex && featureSector == hoveredSector;

			if (featureSector && (featureData == HP_FLOOR || featureData == HP_CEIL))
			{
				drawFlat3D_Highlighted(featureSector, featureData, 3.5f, HL_SELECTED, false, false);
			}

			bool curFlat = featureData == HP_FLOOR || featureData == HP_CEIL;
			// Draw thicker lines.
			if (featureIndex >= 0 && featureSector && !curFlat)
			{
				EditorWall* wall = &featureSector->walls[featureIndex];
				EditorSector* next = wall->adjoinId < 0 ? nullptr : &s_level.sectors[wall->adjoinId];
				drawWallLines3D_Highlighted(featureSector, next, wall, 3.5f, HL_SELECTED, false, featureData == HP_SIGN);
			}
			
			// Draw translucent surfaces.
			if (featureIndex >= 0 && featureSector && !curFlat)
			{
				u32 color = SCOLOR_POLY_SELECTED;
				drawWallColor3d_Highlighted(featureSector, featureSector->vtx.data(), featureSector->walls.data() + featureIndex, color, featureData);
			}
		}

		const bool hoveredFlat = hoveredPart == HP_FLOOR || hoveredPart == HP_CEIL;
		if (hoveredSector && hoveredFlat)
		{
			drawFlat3D_Highlighted(hoveredSector, hoveredPart, 3.5f, HL_HOVERED, hoverAndSelectFlat, false);
		}
		if (hoveredFeatureIndex >= 0 && hoveredSector && !hoveredFlat)
		{
			u32 color = SCOLOR_POLY_HOVERED;
			drawWallColor3d_Highlighted(hoveredSector, hoveredSector->vtx.data(), hoveredSector->walls.data() + hoveredFeatureIndex, color, hoveredPart);
		}
		if (hoveredFeatureIndex >= 0 && hoveredSector && !hoveredFlat)
		{
			EditorWall* wall = &hoveredSector->walls[hoveredFeatureIndex];
			EditorSector* next = wall->adjoinId < 0 ? nullptr : &s_level.sectors[wall->adjoinId];
			drawWallLines3D_Highlighted(hoveredSector, next, wall, 3.5f, HL_HOVERED, hoverAndSelect, hoveredPart == HP_SIGN);
		}
	}

	void drawVertex3d(const Vec3f* vtx, const EditorSector* sector, u32 color)
	{
		const f32 scale = TFE_Math::distance(vtx, &s_camera.pos) / f32(s_viewportSize.z);
		const f32 size = 8.0f * scale;
		drawBox3d(vtx, size, 3.0f, color);
	}

	// Render highlighted 3d elements.
	void renderHighlighted3d(s32 visObjCount, const EditorSector** visObjSector, const EditorObject** visObj, const s32* visObjId, Vec3f cameraRgtXZ)
	{
		lineDraw3d_begin(s_viewportSize.x, s_viewportSize.z);
		triDraw3d_begin(&s_grid);

		if (s_editMode == LEDIT_DRAW)
		{
			if (s_geoEdit.extrudeEnabled)
			{
				drawExtrudeShape3D();
			}
			else
			{
				drawSectorShape3D();
			}
		}
		else if (s_editMode == LEDIT_SECTOR)
		{
			drawSelectedSector3D();
		}
		else if (s_editMode == LEDIT_WALL)
		{
			drawSelectedSurface3D();
		}
		else if (s_editMode == LEDIT_VERTEX)
		{
			bool alsoHovered = false;
			FeatureId* list = nullptr;
			const u32 count = selection_getList(list);

			EditorSector* hoveredSector = nullptr;
			s32 hoveredFeatureIndex = -1;
			if (selection_hasHovered())
			{
				selection_getVertex(SEL_INDEX_HOVERED, hoveredSector, hoveredFeatureIndex);
			}

			if (count && list)
			{
				for (u32 i = 0; i < count; i++)
				{
					s32 featureIndex;
					HitPart part = HP_FLOOR;
					bool overlapped;
					EditorSector* sector = unpackFeatureId(list[i], &featureIndex, (s32*)&part, &overlapped);
					if (overlapped || !sector) { continue; }

					bool thisAlsoHovered = hoveredFeatureIndex == featureIndex && hoveredSector == sector;
					if (thisAlsoHovered) { alsoHovered = true; }

					Vec3f pos = { sector->vtx[featureIndex].x, part == HP_FLOOR ? getFloorAtXZ(sector, sector->vtx[featureIndex]) : getCeilAtXZ(sector, sector->vtx[featureIndex]), sector->vtx[featureIndex].z };
					drawVertex3d(&pos, sector, thisAlsoHovered ? c_vertexClr[HL_SELECTED + 1] : c_vertexClr[HL_SELECTED]);
				}
			}
			if (hoveredFeatureIndex >= 0 && !alsoHovered)
			{
				drawVertex3d(&s_hoveredVtxPos, hoveredSector, c_vertexClr[HL_HOVERED]);
			}
		}
		else if (s_editMode == LEDIT_ENTITY)
		{
			// Draw objects.
			for (s32 o = 0; o < visObjCount; o++)
			{
				bool locked = sector_isLocked((EditorSector*)visObjSector[o]);
				if (locked) { continue; }
				drawEntity3D(visObjSector[o], visObj[o], visObjId[o], 0xffffffff, cameraRgtXZ, true, true);
			}
		}

		// Draw level notes so the borders show up on top.
		if (!(s_editorConfig.interfaceFlags & PIF_HIDE_NOTES))
		{
			Vec3f cameraRgt = { s_camera.viewMtx.m0.x, s_camera.viewMtx.m0.y, s_camera.viewMtx.m0.z };
			Vec3f cameraUp = { s_camera.viewMtx.m1.x, s_camera.viewMtx.m1.y, s_camera.viewMtx.m1.z };

			const s32 noteCount = (s32)s_level.notes.size();
			LevelNote* note = s_level.notes.data();
			for (s32 o = 0; o < noteCount; o++, note++)
			{
				if (note->flags & LNF_2D_ONLY) { continue; }
				Group* group = levelNote_getGroup(note);
				if (group->flags & GRP_HIDDEN) { continue; }

				bool locked = (group->flags & GRP_LOCKED);
				u32 objColor = (!locked) ? note->iconColor : 0x80ffffff;
				drawNoteIcon3d(note, o, objColor, cameraRgt, cameraUp);
			}
		}

		triDraw3d_draw(&s_camera, (f32)s_viewportSize.x, (f32)s_viewportSize.z, s_grid.size, 0.0f);
		lineDraw3d_drawLines(&s_camera, false, false);

		// Draw transform gizmo, on top of other geometry.
		triDraw3d_begin(&s_grid);
		lineDraw3d_begin(s_viewportSize.x, s_viewportSize.z);

		drawTransformGizmo();

		triDraw3d_draw(&s_camera, (f32)s_viewportSize.x, (f32)s_viewportSize.z, s_grid.size, 0.0f, false, false);
		lineDraw3d_drawLines(&s_camera, false, false);
	}

	void drawGrid3D(bool gridOver)
	{
		f32 opacity = gridOver ? 1.5f * s_gridOpacity : s_gridOpacity;
		if (s_camera.pos.y >= s_grid.height)
		{
			grid3d_draw(s_grid.size, opacity, s_grid.height);
			renderCoordinateAxis();
		}
		else
		{
			renderCoordinateAxis();
			grid3d_draw(s_grid.size, opacity, s_grid.height);
		}
	}

	void draw3dObject(const EditorObject* obj, const Entity* entity, const EditorSector* sector, u32 objColor)
	{
		EditorObj3D* obj3d = entity->obj3d;
		const bool showLighting = s_sectorDrawMode == SDM_TEXTURED_FLOOR || s_sectorDrawMode == SDM_TEXTURED_CEIL || s_sectorDrawMode == SDM_LIGHTING;
		const f32 alpha = ((objColor >> 24) & 255) / 255.0f;
		const f32 ambient = !showLighting || (s_editFlags & LEF_FULLBRIGHT) ? 1.0f : sector->ambient / 31.0f;
		modelDraw_addModel(&obj3d->vtxGpu, &obj3d->idxGpu, obj->pos, obj->transform, { ambient, ambient, ambient, alpha });

		const s32 mtlCount = (s32)obj3d->mtl.size();
		const EditorMaterial* mtl = obj3d->mtl.data();
		for (s32 m = 0; m < mtlCount; m++, mtl++)
		{
			ModelDrawMode mode;
			// for now just do colored or texture uv.
			mode = mtl->texture ? MDLMODE_TEX_UV : MDLMODE_COLORED;
			TexProjection proj = { 0 };
			if (mtl->flatProj && mtl->texture)
			{
				mode = MDLMODE_TEX_PROJ;
				proj.offset = s_camera.pos.y >= obj->pos.y ? sector->floorTex.offset : sector->ceilTex.offset;
			}

			modelDraw_addMaterial(mode, mtl->idxStart, mtl->idxCount, mtl->texture, proj);
		}
	}

	u32 fadeAlpha(f32 fade, u32 color)
	{
		if (fade >= 1.0f) { return color; }
		f32 alpha = f32(color >> 24u) * fade;
		color &= 0x00ffffff;
		color |= (u32(alpha) << 24u);
		return color;
	}

	void drawNoteIcon3d(LevelNote* note, s32 id, u32 objColor, const Vec3f& cameraRgt, const Vec3f& cameraUp)
	{
		if (!s_noteIcon3dImage) { return; }

		const Vec3f pos = note->pos;
		const f32 width = c_levelNoteRadius3d;
				
		f32 fade = 1.0f;
		if (!(note->flags & LNF_3D_NO_FADE))
		{
			Vec3f cameraDelta = { note->pos.x - s_camera.pos.x, note->pos.y - s_camera.pos.y, note->pos.z - s_camera.pos.z };
			f32 distSq = cameraDelta.x*cameraDelta.x + cameraDelta.y*cameraDelta.y + cameraDelta.z*cameraDelta.z;
			if (distSq >= note->fade.z*note->fade.z)
			{
				return;
			}

			const f32 dist = sqrtf(distSq);
			fade = clamp(1.0f - (dist - note->fade.x) / (note->fade.z - note->fade.x), 0.0f, 1.0f);
		}

		u32 borderColor = 0;// fadeAlpha(fade, objColor);
		if (s_curLevelNote == id)
		{
			borderColor = c_noteColors[s_hoveredLevelNote == id ? 2 : 1];
		}
		else if (s_hoveredLevelNote == id)
		{
			borderColor = c_noteColors[0];
		}
		objColor = fadeAlpha(fade, objColor);

		const Vec3f deltaX = { width * cameraRgt.x, width * cameraRgt.y, width * cameraRgt.z };
		const Vec3f deltaY = { width * cameraUp.x,  width * cameraUp.y,  width * cameraUp.z };
		const f32 x0 = -1.0f, x1 = 1.0f;
		const f32 y0 = -1.0f, y1 = 1.0f;
		const Vec3f corners[] =
		{
			{ pos.x + x0*deltaX.x + y1*deltaY.x, pos.y + y1*deltaY.y + x0*deltaX.y, pos.z + x0*deltaX.z + y1*deltaY.z },
			{ pos.x + x1*deltaX.x + y1*deltaY.x, pos.y + y1*deltaY.y + x1*deltaX.y, pos.z + x1*deltaX.z + y1*deltaY.z },
			{ pos.x + x1*deltaX.x + y0*deltaY.x, pos.y + y0*deltaY.y + x1*deltaX.y, pos.z + x1*deltaX.z + y0*deltaY.z },
			{ pos.x + x0*deltaX.x + y0*deltaY.x, pos.y + y0*deltaY.y + x0*deltaX.y, pos.z + x0*deltaX.z + y0*deltaY.z },
			// Repeat the first corner again for line drawing later.
			{ pos.x + x0*deltaX.x + y1*deltaY.x, pos.y + y1*deltaY.y + x0*deltaX.y, pos.z + x0*deltaX.z + y1*deltaY.z }
		};
		const f32 u0 = 0.0f, u1 = 1.0f;
		const f32 v0 = 1.0f, v1 = 0.0f;
		const Vec2f uv[] =
		{
			{ u0, v1 },
			{ u1, v1 },
			{ u1, v0 },
			{ u0, v0 }
		};
		s32 idx[] = { 0, 1, 2, 0, 2, 3 };
		triDraw3d_addTextured(TRIMODE_BLEND, 6, 4, corners, uv, idx, objColor, false, s_noteIcon3dImage, false, false);

		if (borderColor)
		{
			for (s32 i = 0; i < 4; i++)
			{
				lineDraw3d_addLine(3.0f, &corners[i], &borderColor);
			}
		}
	}

	f32 adjustWidthForView(f32 width, const EditorObject* obj, const Entity* entity, const Vec2f** srcUv)
	{
		// Which direction?
		f32 dir = fmodf(obj->angle / (2.0f * PI), 1.0f);
		if (dir < 0.0f) { dir += 1.0f; }

		f32 angleDiff = fmodf((s_camera.yaw - obj->angle) / (2.0f * PI), 1.0f);
		if (angleDiff < 0.0f) { angleDiff += 1.0f; }

		const s32 viewIndex = 31 - (s32(angleDiff * 32.0f) & 31);
		assert(viewIndex >= 0 && viewIndex < 32);

		const SpriteView* view = &entity->views[viewIndex];
		if (srcUv) { *srcUv = view->uv; }

		// Adjust the width.
		width *= (view->st[1].x - view->st[0].x) / (entity->views[0].st[1].x - entity->views[0].st[0].x);
		return width;
	}

	void computePlaneFromVtx(Vec3f v0, Vec3f v1, Vec3f v2, Vec4f* outPlane)
	{
		Vec3f s = { v1.x - v0.x, v1.y - v0.y, v1.z - v0.z };
		Vec3f t = { v2.x - v0.x, v2.y - v0.y, v2.z - v0.z };
		Vec3f n = TFE_Math::cross(&s, &t);
		n = TFE_Math::normalize(&n);

		outPlane->x = n.x;
		outPlane->y = n.y;
		outPlane->z = n.z;
		outPlane->w = -TFE_Math::dot(&v0, &n);
	}

	// Compute planes from world positions.
	void computePlanesFromOBBCorners(const Vec3f* vtxWorld, Vec4f* outPlanes)
	{
		// Top/Bottom
		computePlaneFromVtx(vtxWorld[4], vtxWorld[5], vtxWorld[6], &outPlanes[0]);
		computePlaneFromVtx(vtxWorld[0], vtxWorld[2], vtxWorld[1], &outPlanes[1]);
		// Sides
		for (s32 i = 0; i < 4; i++)
		{
			const s32 i0 = i, i1 = (i + 1) & 3, i2 = 4 + i1;
			computePlaneFromVtx(vtxWorld[i0], vtxWorld[i1], vtxWorld[i2], &outPlanes[i + 2]);
		}
	}

	// Planes are pointing inward.
	void computeOBB(const Vec3f* bounds, const Mat3* mtx, const Vec3f* pos, Vec4f* outPlanes)
	{
		const Vec3f vtxLocal[8] =
		{
			{ bounds[0].x, bounds[0].y, bounds[0].z },
			{ bounds[1].x, bounds[0].y, bounds[0].z },
			{ bounds[1].x, bounds[0].y, bounds[1].z },
			{ bounds[0].x, bounds[0].y, bounds[1].z },

			{ bounds[0].x, bounds[1].y, bounds[0].z },
			{ bounds[1].x, bounds[1].y, bounds[0].z },
			{ bounds[1].x, bounds[1].y, bounds[1].z },
			{ bounds[0].x, bounds[1].y, bounds[1].z },
		};
		Vec3f vtxWorld[8];
		for (s32 v = 0; v < 8; v++)
		{
			vtxWorld[v].x = vtxLocal[v].x * mtx->m0.x + vtxLocal[v].y * mtx->m0.y + vtxLocal[v].z * mtx->m0.z + pos->x;
			vtxWorld[v].y = vtxLocal[v].x * mtx->m1.x + vtxLocal[v].y * mtx->m1.y + vtxLocal[v].z * mtx->m1.z + pos->y;
			vtxWorld[v].z = vtxLocal[v].x * mtx->m2.x + vtxLocal[v].y * mtx->m2.y + vtxLocal[v].z * mtx->m2.z + pos->z;
		}

		computePlanesFromOBBCorners(vtxWorld, outPlanes);
	}

	void computeOBB(const Vec3f* center, const Vec3f* size, Vec4f* outPlanes)
	{
		const f32 w = size->x * 0.5f;
		const f32 h = size->y * 0.5f;
		const f32 d = size->z * 0.5f;
		const Vec3f vtxWorld[] =
		{
			{center->x - w, center->y - h, center->z - d},
			{center->x + w, center->y - h, center->z - d},
			{center->x + w, center->y - h, center->z + d},
			{center->x - w, center->y - h, center->z + d},

			{center->x - w, center->y + h, center->z - d},
			{center->x + w, center->y + h, center->z - d},
			{center->x + w, center->y + h, center->z + d},
			{center->x - w, center->y + h, center->z + d},
		};

		computePlanesFromOBBCorners(vtxWorld, outPlanes);
	}

	void viewport_computeEntityBoundingPlanes(const EditorSector* sector, const EditorObject* obj, Vec4f* boundingPlanes)
	{
		const Entity* entity = &s_level.entities[obj->entityId];
		const Vec3f pos = obj->pos;

		f32 width = entity->size.x * 0.5f;
		f32 height = entity->size.z;
		f32 y = pos.y;
		if (entity->type != ETYPE_SPIRIT && entity->type != ETYPE_SAFE)
		{
			f32 offset = -(entity->offset.y + fabsf(entity->st[1].z - entity->st[0].z)) * 0.1f;
			// If the entity is on the floor, make sure it doesn't stick through it for editor visualization.
			if (fabsf(pos.y - sector->floorHeight) < 0.5f) { offset = max(0.0f, offset); }
			y = pos.y + offset;
		}

		// Adjust the width based on the view
		if (!entity->obj3d && entity->views.size() >= 32)
		{
			width = adjustWidthForView(width, obj, entity, nullptr);
		}

		if (entity->obj3d)
		{
			computeOBB(entity->obj3d->bounds, &obj->transform, &obj->pos, boundingPlanes);
		}
		else
		{
			Vec3f center = { pos.x, y + height * 0.5f, pos.z };
			Vec3f size = { width*2.0f, height, width*2.0f };
			computeOBB(&center, &size, boundingPlanes);
		}
	}

	void drawEntity3D(const EditorSector* sector, const EditorObject* obj, s32 id, u32 objColor, const Vec3f& cameraRgtXZ, bool drawEntityBounds, bool drawHighlights)
	{
		const Entity* entity = &s_level.entities[obj->entityId];
		const Vec3f pos = obj->pos;

		f32 width  = entity->size.x * 0.5f;
		f32 height = entity->size.z;
		f32 y = pos.y;
		if (entity->type != ETYPE_SPIRIT && entity->type != ETYPE_SAFE)
		{
			f32 offset = -(entity->offset.y + fabsf(entity->st[1].z - entity->st[0].z)) * 0.1f;
			// If the entity is on the floor, make sure it doesn't stick through it for editor visualization.
			if (fabsf(pos.y - sector->floorHeight) < 0.5f) { offset = max(0.0f, offset); }
			y = pos.y + offset;
		}

		if (!drawHighlights)
		{
			if (entity->obj3d)
			{
				draw3dObject(obj, entity, sector, objColor);
			}
			else
			{
				const Vec2f* srcUv = entity->uv;
				if (entity->views.size() >= 32)
				{
					width = adjustWidthForView(width, obj, entity, &srcUv);
				}

				Vec3f corners[] =
				{
					{ pos.x - width * cameraRgtXZ.x, y + height, pos.z - width * cameraRgtXZ.z },
					{ pos.x + width * cameraRgtXZ.x, y + height, pos.z + width * cameraRgtXZ.z },
					{ pos.x + width * cameraRgtXZ.x, y,          pos.z + width * cameraRgtXZ.z },
					{ pos.x - width * cameraRgtXZ.x, y,          pos.z - width * cameraRgtXZ.z }
				};
				Vec2f uv[] =
				{
					{ srcUv[0].x, srcUv[0].z },
					{ srcUv[1].x, srcUv[0].z },
					{ srcUv[1].x, srcUv[1].z },
					{ srcUv[0].x, srcUv[1].z }
				};
				s32 idx[] = { 0, 1, 2, 0, 2, 3 };
				triDraw3d_addTextured(TRIMODE_BLEND, 6, 4, corners, uv, idx, objColor, false, entity->image, false, false);
			}
		}
		else if (!entity->obj3d && entity->views.size() >= 32)
		{
			width = adjustWidthForView(width, obj, entity, nullptr);
		}

		if (s_editMode == LEDIT_ENTITY && drawEntityBounds)
		{
			EditorSector* hoveredSector = nullptr;
			s32 hoveredFeatureIndex = -1;
			selection_getEntity(SEL_INDEX_HOVERED, hoveredSector, hoveredFeatureIndex);

			Highlight hl = HL_NONE;
			if (selection_entity(SA_CHECK_INCLUSION, (EditorSector*)sector, id))
			{
				hl = HL_SELECTED;
			}
			if (sector == hoveredSector && hoveredFeatureIndex == id)
			{
				hl = (hl == HL_SELECTED) ? Highlight(HL_SELECTED + 1) : HL_HOVERED;
			}
			if ((drawHighlights && hl == HL_NONE) || (!drawHighlights && hl != HL_NONE)) { return; }

			// Draw bounds.
			if (entity->obj3d)
			{
				drawOBB(entity->obj3d->bounds, &obj->transform, &obj->pos, 3.0f, c_entityClr[hl]);
			}
			else
			{
				Vec3f center = { pos.x, y + height * 0.5f, pos.z };
				Vec3f size = { width*2.0f, height, width*2.0f };
				drawBounds(&center, size, 3.0f, c_entityClr[hl]);
			}

			// Draw direction arrows if it has a facing.
			// Do not draw the arrow if this is a 3D object.
			if (!entity->obj3d)
			{
				f32 angle = obj->angle;
				Vec3f dir = { sinf(angle), 0.0f, cosf(angle) };
				Vec3f N = { -dir.z, 0.0f, dir.x };

				// Find vertices for the arrow shape.
				f32 yArrow = y;
				if (fabsf(pos.y - sector->floorHeight) < 0.5f) { yArrow = sector->floorHeight; }
				else if (fabsf(pos.y - sector->ceilHeight) < 0.5f) { yArrow = sector->ceilHeight; }

				const f32 D = width + 1.0f;
				const f32 S = 1.0f;
				const f32 T = 1.0f;

				Vec3f endPt = { pos.x + dir.x*D, yArrow, pos.z + dir.z*D };
				Vec3f base = { endPt.x - dir.x*T, yArrow, endPt.z - dir.z*T };
				Vec3f A = { base.x + N.x*S, yArrow, base.z + N.z*S };
				Vec3f B = { base.x - N.x*S, yArrow, base.z - N.z*S };

				Vec3f dirLines[] =
				{
					{ endPt.x, yArrow, endPt.z }, A,
					{ endPt.x, yArrow, endPt.z }, B,
				};
				u32 colors[] = { 0xffff2020, 0xffff2020, 0xffff2020, 0xffff2020 };
				lineDraw3d_addLines(2, 3.0f, dirLines, colors);
			}
		}
	}

	u32 getDirColor(Vec3f dir, f32 alpha, Vec3f* alignDir, f32 fade)
	{
		const Vec3f colorDir[] =
		{
			{ 1.0f, 0.0f, 0.0f },
			{ 0.0f, 1.0f, 0.0f },
			{ 0.0f, 0.0f, 1.0f },
			{-1.0f, 0.0f, 0.0f },
			{ 0.0f,-1.0f, 0.0f },
			{ 0.0f, 0.0f,-1.0f }
		};
		const Vec3f colorAlign = { 1.0f, 1.0f, 0.0f };
		const Vec3f color[] =
		{
			// Positive
			{ 1.0f, 0.0f, 0.0f },
			{ 0.0f, 1.0f, 0.0f },
			{ 0.0f, 0.0f, 1.0f },
			// Negative
			{ 1.00f, 0.25f, 0.25f },
			{ 0.50f, 1.00f, 0.50f },
			{ 0.25f, 0.25f, 1.00f }
		};

		f32 sum = 0.0f;
		Vec3f colorSum = { 0 };
		for (s32 i = 0; i < TFE_ARRAYSIZE(colorDir); i++)
		{
			f32 w = std::max(0.0f, colorDir[i].x*dir.x + colorDir[i].y*dir.y + colorDir[i].z*dir.z);
			colorSum.x += w * color[i].x;
			colorSum.y += w * color[i].y;
			colorSum.z += w * color[i].z;
			sum += w;
		}
		if (sum > 0.0f)
		{
			f32 scale = 1.0f / sum;
			colorSum.x *= scale;
			colorSum.y *= scale;
			colorSum.z *= scale;
		}

		if (alignDir)
		{
			f32 alignWeight = fade * std::max(0.0f, dir.x*alignDir->x + dir.y*alignDir->y + dir.z*alignDir->z);
			colorSum.x = colorSum.x + alignWeight * (colorAlign.x - colorSum.x);
			colorSum.y = colorSum.y + alignWeight * (colorAlign.y - colorSum.y);
			colorSum.z = colorSum.z + alignWeight * (colorAlign.z - colorSum.z);
		}

		return u32(colorSum.x * 255.0f) | (u32(colorSum.y * 255.0f) << 8) | (u32(colorSum.z * 255.0f) << 16) | (u32(alpha * 255.0f) << 24);
	}

	bool isCameraAboveFloor(EditorSector* sector)
	{
		const Vec3f& pos = s_camera.pos;
		bool isAboveFloor = pos.y > sector->floorHeight;
		if (sector->flags[0] & SEC_FLAGS1_SLOPEDFLOOR)
		{
			const Vec3f& pos   = s_camera.pos;
			const Vec4f& plane = sector->floorPlane.plane;
			const f32 dist = pos.x*plane.x + pos.y*plane.y + pos.z*plane.z + plane.w;
			isAboveFloor = dist > 0.0f;
		}
		return isAboveFloor;
	}

	bool isCameraBelowCeil(EditorSector* sector)
	{
		const Vec3f& pos = s_camera.pos;
		bool isBelowCeil = pos.y < sector->ceilHeight;
		if (sector->flags[0] & SEC_FLAGS1_SLOPEDCEILING)
		{
			const Vec4f& plane = sector->ceilPlane.plane;
			const f32 dist = pos.x*plane.x + pos.y*plane.y + pos.z*plane.z + plane.w;
			isBelowCeil = dist < 0.0f;
		}
		return isBelowCeil;
	}

	void renderLevel3D()
	{
		viewport_updateRail();

		// Prepare for drawing.
		TFE_RenderShared::lineDraw3d_begin(s_viewportSize.x, s_viewportSize.z);
		TFE_RenderShared::triDraw3d_begin(&s_grid);
		TFE_RenderShared::modelDraw_begin();

		if (!(s_gridFlags & GFLAG_OVER))
		{
			drawGrid3D(false);
		}

		Vec3f cameraDirXZ = { s_camera.viewMtx.m2.x, 0.0f, s_camera.viewMtx.m2.z };
		cameraDirXZ = TFE_Math::normalize(&cameraDirXZ);
		Vec3f cameraRgtXZ = { -cameraDirXZ.z, 0.0f, cameraDirXZ.x };

		// Draw guidelines.
		renderGuidelines3d();

		const EditorObject* visObj[1024];
		const EditorSector* visObjSector[1024];
		s32 visObjId[1024];
		s32 visObjCount = 0;

		EditorSector* hoveredSector = nullptr;
		EditorSector* curSector = nullptr;
		s32 hoveredFeatureIndex = -1, curFeatureIndex = -1;
		HitPart hoveredPart, curPart;
		if (s_editMode == LEDIT_SECTOR)
		{
			selection_getSector(SEL_INDEX_HOVERED, hoveredSector);
			selection_getSector(0, curSector);
		}
		else if (s_editMode == LEDIT_WALL)
		{
			selection_getSurface(SEL_INDEX_HOVERED, hoveredSector, hoveredFeatureIndex, &hoveredPart);
			selection_getSurface(0, curSector, curFeatureIndex, &curPart);
		}

		const f32 width = 2.5f;
		const size_t count = s_level.sectors.size();
		EditorSector* sector = s_level.sectors.data();
		for (size_t s = 0; s < count; s++, sector++)
		{
			// Skip other layers unless all layers is enabled.
			if (!sector_onActiveLayer(sector)) { continue; }
			if (sector_isHidden(sector)) { continue; }

			// Add objects...
			// TODO: Frustum and distance culling.
			const s32 objCount = (s32)sector->obj.size();
			const EditorObject* obj = sector->obj.data();
			for (s32 o = 0; o < objCount && visObjCount < 1024; o++, obj++)
			{
				visObjSector[visObjCount] = sector;
				visObjId[visObjCount] = o;
				visObj[visObjCount++] = obj;
			}

			Highlight highlight = sector_isLocked(sector) ? HL_LOCKED : HL_NONE;

			// Sector lighting.
			const u32 colorIndex = (s_editFlags & LEF_FULLBRIGHT) && s_sectorDrawMode != SDM_LIGHTING ? 31 : sector->ambient;

			// Floor/ceiling line bias.
			f32 bias = 1.0f / 1024.0f;
			f32 floorBias = (s_camera.pos.y >= sector->floorHeight) ?  bias : -bias;
			f32 ceilBias  = (s_camera.pos.y <= sector->ceilHeight)  ? -bias :  bias;

			// Draw lines.
			const s32 wallCount = (s32)sector->walls.size();
			const Vec2f* vtx = sector->vtx.data();
			EditorWall* wall = sector->walls.data();
			for (s32 w = 0; w < wallCount; w++, wall++)
			{
				// Skip hovered or selected walls.
				bool skipLines = false;
				if (s_editMode == LEDIT_WALL && ((hoveredSector == sector && hoveredFeatureIndex == w) ||
					selection_action(SA_CHECK_INCLUSION, sector, w)))
				{
					skipLines = true;
				}

				EditorSector* next = (wall->adjoinId < 0 || wall->adjoinId >= (s32)count) ? nullptr : &s_level.sectors[wall->adjoinId];
				const Vec2f& v0 = sector->vtx[wall->idx[0]];
				const Vec2f& v1 = sector->vtx[wall->idx[1]];

				if (!skipLines)
				{
					drawWallLines3D(sector, next, wall, width, highlight, true);
				}

				s32 wallColorIndex = (s32)colorIndex;
				if (wallColorIndex < 31)
				{
					wallColorIndex = std::max(0, std::min(31, wallColorIndex + wall->wallLight));
				}

				u32 wallColor = 0xff1a0f0d;
				if (sector_isLocked(sector))
				{
					wallColor = (s_sectorDrawMode == SDM_TEXTURED_FLOOR || s_sectorDrawMode == SDM_TEXTURED_CEIL) ? SCOLOR_LOCKED_TEXTURE : SCOLOR_LOCKED;
				}
				else if (s_sectorDrawMode == SDM_GROUP_COLOR)
				{
					wallColor = sector_getGroupColor(sector);
				}
				else if (s_sectorDrawMode != SDM_WIREFRAME)
				{
					wallColor = c_sectorTexClr[wallColorIndex];
				}
												
				// Wall Parts
				const Vec2f wallOffset = { v1.x - v0.x, v1.z - v0.z };
				const f32 wallLengthTexels = sqrtf(wallOffset.x*wallOffset.x + wallOffset.z*wallOffset.z) * 8.0f;
				const f32 sectorHeight = sector->ceilHeight - sector->floorHeight;
				const bool flipHorz = (wall->flags[0] & WF1_FLIP_HORIZ) != 0u;
				Vec2f uvCorners[2];

				// Skip backfacing walls.
				const Vec2f cameraOffset = { v0.x - s_camera.pos.x, v0.z - s_camera.pos.z };
				const f32 facing = -wallOffset.z * cameraOffset.x + wallOffset.x * cameraOffset.z;
				if (facing < 0.0f) { continue; }

				if (wall->adjoinId < 0)
				{
					Vec3f corners[] = { {v0.x, sector->ceilHeight,  v0.z},
										{v1.x, sector->floorHeight, v1.z} };
					Vec3f vtx[] =
					{
						{ v0.x, getFloorAtXZ(sector, v0), v0.z },
						{ v0.x, getCeilAtXZ(sector,  v0), v0.z },
						{ v1.x, getCeilAtXZ(sector,  v1), v1.z },
						{ v1.x, getFloorAtXZ(sector, v1), v1.z },
					};

					if (s_sectorDrawMode == SDM_TEXTURED_FLOOR || s_sectorDrawMode == SDM_TEXTURED_CEIL)
					{
						Vec2f uvVtx[4];
						f32 fh = sector->floorHeight;
						const EditorTexture* tex = calculateTextureCoordsSlope(wall, &wall->tex[WP_MID], wallLengthTexels, fh, vtx[0].y, vtx[1].y, vtx[3].y, vtx[2].y, flipHorz, uvVtx);
						TFE_RenderShared::triDraw3d_addDeformedQuadTextured(TRIMODE_OPAQUE, vtx, uvVtx, wallColor, tex ? tex->frames[0] : nullptr);
					}
					else
					{
						TFE_RenderShared::triDraw3d_addDeformedQuadColored(TRIMODE_OPAQUE, vtx, wallColor);
					}

					// Sign?
					if (wall->tex[WP_SIGN].texIndex >= 0 && (s_sectorDrawMode == SDM_TEXTURED_FLOOR || s_sectorDrawMode == SDM_TEXTURED_CEIL))
					{
						const EditorTexture* tex = calculateSignTextureCoords(wall, &wall->tex[WP_MID], &wall->tex[WP_SIGN], wallLengthTexels, sectorHeight, false, uvCorners);
						if (tex)
						{
							const Vec2f uvVtx[] = { {uvCorners[0].x, uvCorners[1].z}, {uvCorners[0].x, uvCorners[0].z}, {uvCorners[1].x, uvCorners[0].z}, {uvCorners[1].x, uvCorners[1].z} };
							TFE_RenderShared::triDraw3d_addDeformedQuadTextured(TRIMODE_CLAMP, vtx, uvVtx, wallColor, tex->frames[0]);
						}
					}
				}
				else
				{
					const u32 adjType = getAdjoinType(sector, wall);
					EditorSector* next = &s_level.sectors[wall->adjoinId];
					bool botSign = false;
					// Bottom
					if (adjType & ADJ_TYPE_BOT)
					{
						bool sky = (sector->flags[0] & SEC_FLAGS1_PIT) != 0 &&
							       (next->flags[0] & SEC_FLAGS1_EXT_FLOOR_ADJ) != 0;

						const f32 botHeight = next->floorHeight - sector->floorHeight;
						Vec3f corners[] = { {v0.x, next->floorHeight,   v0.z},
											{v1.x, sector->floorHeight, v1.z} };
						Vec3f vtx[] =
						{
							{ v0.x, getFloorAtXZ(sector, v0), v0.z },
							{ v0.x, getFloorAtXZ(next,   v0), v0.z },
							{ v1.x, getFloorAtXZ(next,   v1), v1.z },
							{ v1.x, getFloorAtXZ(sector, v1), v1.z },
						};

						if (s_sectorDrawMode == SDM_TEXTURED_FLOOR || s_sectorDrawMode == SDM_TEXTURED_CEIL)
						{
							LevelTexture* texPtr = sky ? &sector->floorTex : &wall->tex[WP_BOT];
							if (texPtr->texIndex < 0) { texPtr->texIndex = getTextureIndex("DEFAULT.BM"); }

							Vec2f uvVtx[4];
							f32 fh = sector->floorHeight;
							const EditorTexture* tex = calculateTextureCoordsSlope(wall, texPtr, wallLengthTexels, fh, vtx[0].y, vtx[1].y, vtx[3].y, vtx[2].y, flipHorz, uvVtx);
							TFE_RenderShared::triDraw3d_addDeformedQuadTextured(TRIMODE_OPAQUE, vtx, uvVtx, wallColor, tex ? tex->frames[0] : nullptr, sky);
						}
						else
						{
							TFE_RenderShared::triDraw3d_addDeformedQuadColored(TRIMODE_OPAQUE, vtx, wallColor);
						}

						// Sign?
						if (wall->tex[WP_SIGN].texIndex >= 0 && (s_sectorDrawMode == SDM_TEXTURED_FLOOR || s_sectorDrawMode == SDM_TEXTURED_CEIL))
						{
							const EditorTexture* tex = calculateSignTextureCoords(wall, &wall->tex[WP_BOT], &wall->tex[WP_SIGN], wallLengthTexels, botHeight, false, uvCorners);
							if (tex)
							{
								const Vec2f uvVtx[] = { {uvCorners[0].x, uvCorners[1].z}, {uvCorners[0].x, uvCorners[0].z}, {uvCorners[1].x, uvCorners[0].z}, {uvCorners[1].x, uvCorners[1].z} };
								TFE_RenderShared::triDraw3d_addDeformedQuadTextured(TRIMODE_CLAMP, vtx, uvVtx, wallColor, tex->frames[0]);
								botSign = true;
							}
						}
					}
					// Top
					if (adjType & ADJ_TYPE_TOP)
					{
						bool sky = (sector->flags[0] & SEC_FLAGS1_EXTERIOR) != 0 &&
							       (next->flags[0] & SEC_FLAGS1_EXT_ADJ) != 0;

						const f32 topHeight = sector->ceilHeight - next->ceilHeight;
						Vec3f corners[] = { {v0.x, sector->ceilHeight, v0.z},
										    {v1.x, next->ceilHeight,   v1.z} };
						Vec3f vtx[] =
						{
							{ v0.x, getCeilAtXZ(next,   v0), v0.z },
							{ v0.x, getCeilAtXZ(sector, v0), v0.z },
							{ v1.x, getCeilAtXZ(sector, v1), v1.z },
							{ v1.x, getCeilAtXZ(next,   v1), v1.z },
						};

						if (s_sectorDrawMode == SDM_TEXTURED_FLOOR || s_sectorDrawMode == SDM_TEXTURED_CEIL)
						{
							LevelTexture* texPtr = sky ? &sector->ceilTex : &wall->tex[WP_TOP];
							if (texPtr->texIndex < 0) { texPtr->texIndex = getTextureIndex("DEFAULT.BM"); }

							Vec2f uvVtx[4];
							f32 fh = next->ceilHeight;
							const EditorTexture* tex = calculateTextureCoordsSlope(wall, texPtr, wallLengthTexels, fh, vtx[0].y, vtx[1].y, vtx[3].y, vtx[2].y, flipHorz, uvVtx);
							TFE_RenderShared::triDraw3d_addDeformedQuadTextured(TRIMODE_OPAQUE, vtx, uvVtx, wallColor, tex ? tex->frames[0] : nullptr, sky);
						}
						else
						{
							TFE_RenderShared::triDraw3d_addDeformedQuadColored(TRIMODE_OPAQUE, vtx, wallColor);
						}

						// Sign?
						if (!botSign && wall->tex[WP_SIGN].texIndex >= 0 && (s_sectorDrawMode == SDM_TEXTURED_FLOOR || s_sectorDrawMode == SDM_TEXTURED_CEIL))
						{
							const EditorTexture* tex = calculateSignTextureCoords(wall, &wall->tex[WP_TOP], &wall->tex[WP_SIGN], wallLengthTexels, topHeight, false, uvCorners);
							if (tex)
							{
								const Vec2f uvVtx[] = { {uvCorners[0].x, uvCorners[1].z}, {uvCorners[0].x, uvCorners[0].z}, {uvCorners[1].x, uvCorners[0].z}, {uvCorners[1].x, uvCorners[1].z} };
								TFE_RenderShared::triDraw3d_addDeformedQuadTextured(TRIMODE_CLAMP, vtx, uvVtx, wallColor, tex->frames[0]);
							}
						}
					}
					// Mid only for mask textures.
					if (next && (wall->flags[0] & WF1_ADJ_MID_TEX) && (s_sectorDrawMode == SDM_TEXTURED_FLOOR || s_sectorDrawMode == SDM_TEXTURED_CEIL))
					{
						Vec3f vtx[] =
						{
							{ v0.x, std::max(getFloorAtXZ(next, v0), getFloorAtXZ(sector, v0)), v0.z },
							{ v0.x, std::min(getCeilAtXZ(next,  v0), getCeilAtXZ(sector,  v0)), v0.z },
							{ v1.x, std::min(getCeilAtXZ(next,  v1), getCeilAtXZ(sector,  v1)), v1.z },
							{ v1.x, std::max(getFloorAtXZ(next, v1), getFloorAtXZ(sector, v1)), v1.z },
						};
						Vec2f uvVtx[4];
						f32 fh = max(next->floorHeight, sector->floorHeight);
						const EditorTexture* tex = calculateTextureCoordsSlope(wall, &wall->tex[WP_MID], wallLengthTexels, fh, vtx[0].y, vtx[1].y, vtx[3].y, vtx[2].y, flipHorz, uvVtx);
						if (tex) { TFE_RenderShared::triDraw3d_addDeformedQuadTextured(TRIMODE_BLEND, vtx, uvVtx, wallColor, tex->frames[0]); }
					}
				}
			}

			// Draw the floor and ceiling.
			u32 floorColor = 0xff402020;
			if (sector_isLocked(sector))
			{
				floorColor = (s_sectorDrawMode == SDM_TEXTURED_FLOOR || s_sectorDrawMode == SDM_TEXTURED_CEIL) ? SCOLOR_LOCKED_TEXTURE : SCOLOR_LOCKED;
			}
			else if (s_sectorDrawMode == SDM_GROUP_COLOR)
			{
				floorColor = sector_getGroupColor(sector);
			}
			else if (s_sectorDrawMode == SDM_TEXTURED_FLOOR || s_sectorDrawMode == SDM_TEXTURED_CEIL || s_sectorDrawMode == SDM_LIGHTING)
			{
				floorColor = c_sectorTexClr[colorIndex];
			}

			const u32 idxCount = (u32)sector->poly.triIdx.size();
			const u32 vtxCount = (u32)sector->poly.triVtx.size();
			const Vec2f* triVtx = sector->poly.triVtx.data();

			s_bufferVec3.resize(vtxCount * 2);
			Vec3f* vtxDataFlr = s_bufferVec3.data();
			Vec3f* vtxDataCeil = vtxDataFlr + vtxCount;

			s_bufferVec2.resize(vtxCount * 2);
			Vec2f* uvFlr = s_bufferVec2.data();
			Vec2f* uvCeil = uvFlr + vtxCount;

			EditorTexture* floorTex = getTexture(sector->floorTex.texIndex);
			EditorTexture* ceilTex  = getTexture(sector->ceilTex.texIndex);
			const Vec2f& floorOffset = sector->floorTex.offset;
			const Vec2f& ceilOffset = sector->ceilTex.offset;

			for (u32 v = 0; v < vtxCount; v++)
			{
				vtxDataFlr[v] = { triVtx[v].x, getFloorAtXZ(sector, triVtx[v]), triVtx[v].z };
				vtxDataCeil[v] = { triVtx[v].x, getCeilAtXZ(sector, triVtx[v]),  triVtx[v].z };
			}
						
			if (s_sectorDrawMode == SDM_TEXTURED_FLOOR || s_sectorDrawMode == SDM_TEXTURED_CEIL)
			{
				for (u32 v = 0; v < vtxCount; v++)
				{
					computeFlatUv(&triVtx[v], &floorOffset, &uvFlr[v]);
					computeFlatUv(&triVtx[v], &ceilOffset, &uvCeil[v]);
				}
			}

			bool showGridOnFlats = !(s_gridFlags & GFLAG_OVER);
			if (isCameraAboveFloor(sector))
			{
				if (s_sectorDrawMode == SDM_TEXTURED_FLOOR || s_sectorDrawMode == SDM_TEXTURED_CEIL)
				{
					bool sky = (sector->flags[0] & SEC_FLAGS1_PIT) != 0;
					triDraw3d_addTextured(TRIMODE_OPAQUE, idxCount, vtxCount, vtxDataFlr, uvFlr, sector->poly.triIdx.data(), floorColor, false, floorTex ? floorTex->frames[0] : nullptr, showGridOnFlats, sky);
				}
				else if (s_sectorDrawMode == SDM_LIGHTING)
				{
					triDraw3d_addColored(TRIMODE_OPAQUE, idxCount, vtxCount, vtxDataFlr, sector->poly.triIdx.data(), floorColor, false, showGridOnFlats);
				}
				else
				{
					triDraw3d_addColored(TRIMODE_OPAQUE, idxCount, vtxCount, vtxDataFlr, sector->poly.triIdx.data(), floorColor, false, showGridOnFlats);
				}
			}
			if (isCameraBelowCeil(sector))
			{
				if (s_sectorDrawMode == SDM_TEXTURED_FLOOR || s_sectorDrawMode == SDM_TEXTURED_CEIL)
				{
					bool sky = (sector->flags[0] & SEC_FLAGS1_EXTERIOR) != 0;
					triDraw3d_addTextured(TRIMODE_OPAQUE, idxCount, vtxCount, vtxDataCeil, uvCeil, sector->poly.triIdx.data(), floorColor, true, ceilTex ? ceilTex->frames[0]: nullptr, showGridOnFlats, sky);
				}
				else if (s_sectorDrawMode == SDM_LIGHTING)
				{
					triDraw3d_addColored(TRIMODE_OPAQUE, idxCount, vtxCount, vtxDataCeil, sector->poly.triIdx.data(), floorColor, true, showGridOnFlats);
				}
				else
				{
					triDraw3d_addColored(TRIMODE_OPAQUE, idxCount, vtxCount, vtxDataCeil, sector->poly.triIdx.data(), floorColor, true, showGridOnFlats);
				}
			}
		}

		// Draw objects.
		for (s32 o = 0; o < visObjCount; o++)
		{
			bool locked = sector_isLocked((EditorSector*)visObjSector[o]);
			u32 objColor = (s_editMode == LEDIT_ENTITY && !locked) ? 0xffffffff : 0x80ffffff;
			drawEntity3D(visObjSector[o], visObj[o], visObjId[o], objColor, cameraRgtXZ, !locked, false);
		}
								
		// Draw the 3D cursor.
		{
			const f32 distFromCam = TFE_Math::distance(&s_cursor3d, &s_camera.pos);
			const f32 size = distFromCam * 16.0f / f32(s_viewportSize.z);
			const f32 sizeExp = distFromCam * 18.0f / f32(s_viewportSize.z);
			drawBox3d(&s_cursor3d, size, 3.0f, 0x80ff8020);
		}

		TFE_RenderShared::triDraw3d_draw(&s_camera, (f32)s_viewportSize.x, (f32)s_viewportSize.z, s_grid.size, s_gridOpacity);
		TFE_RenderShared::modelDraw_draw(&s_camera, (f32)s_viewportSize.x, (f32)s_viewportSize.z);
		TFE_RenderShared::lineDraw3d_drawLines(&s_camera, true, false);

		renderHighlighted3d(visObjCount, visObjSector, visObj, visObjId, cameraRgtXZ);
												
		// Movement "rail", if active.
		if (s_rail.active && s_rail.dirCount > 0)
		{
			// For now just draw an arrow.
			TFE_RenderShared::lineDraw3d_begin(s_viewportSize.x, s_viewportSize.z);
			Vec3f* alignDir = (s_rail.hasMoveDir || s_rail.moveFade > 0.0f) ? &s_rail.moveDir : nullptr;
			for (s32 i = 0; i < s_rail.dirCount; i++)
			{
				u32 color = getDirColor(s_rail.dir[i], 0.75f, alignDir, std::min(1.0f, s_rail.moveFade * 4.0f));
				drawArrow3d(3.0f, 0.1f, s_rail.origin, s_rail.dir[i], s_camera.viewMtx.m2, color);
			}
			TFE_RenderShared::lineDraw3d_drawLines(&s_camera, false, false);
		}
				
		// Draw drag select, if active.
		if (s_dragSelect.active)
		{
			lineDraw2d_begin(s_viewportSize.x, s_viewportSize.z);
			triDraw2d_begin(s_viewportSize.x, s_viewportSize.z);

			Vec2f vtx[] =
			{
				{ (f32)s_dragSelect.startPos.x, (f32)s_dragSelect.startPos.z },
				{ (f32)s_dragSelect.curPos.x,   (f32)s_dragSelect.startPos.z },
				{ (f32)s_dragSelect.curPos.x,   (f32)s_dragSelect.curPos.z },
				{ (f32)s_dragSelect.startPos.x, (f32)s_dragSelect.curPos.z },
				{ (f32)s_dragSelect.startPos.x, (f32)s_dragSelect.startPos.z },
			};
			s32 idx[6] = { 0, 1, 2, 0, 2, 3 };

			triDraw2d_addColored(6, 4, vtx, idx, 0x40ff0000);

			u32 colors[2] = { 0xffff0000, 0xffff0000 };
			lineDraw2d_addLine(2.0f, &vtx[0], colors);
			lineDraw2d_addLine(2.0f, &vtx[1], colors);
			lineDraw2d_addLine(2.0f, &vtx[2], colors);
			lineDraw2d_addLine(2.0f, &vtx[3], colors);

			triDraw2d_draw();
			lineDraw2d_drawLines();
		}
						
		if (s_gridFlags & GFLAG_OVER)
		{
			drawGrid3D(true);
		}

		// Determine is special controls need to be drawn.
		const EditorPopup popup = getCurrentPopup();
		if (popup == POPUP_EDIT_INF)
		{
			Editor_InfVpControl ctrl;
			editor_infGetViewportControl(&ctrl);
			switch (ctrl.type)
			{
				case InfVpControl_Center:
				case InfVpControl_TargetPos3d:
				{
					TFE_RenderShared::lineDraw3d_begin(s_viewportSize.x, s_viewportSize.z);
					{
						drawPosition3d(3.0f, ctrl.cen, INF_COLOR_EDIT_HELPER);
					}
					TFE_RenderShared::lineDraw3d_drawLines(&s_camera, false, false);
				} break;
				case InfVpControl_AngleXZ:
				{
					TFE_RenderShared::lineDraw3d_begin(s_viewportSize.x, s_viewportSize.z);
					{
						drawArrow3d(3.0f, 0.1f, ctrl.cen, ctrl.dir, {0.0f, 1.0f, 0.0f}, INF_COLOR_EDIT_HELPER);
					}
					TFE_RenderShared::lineDraw3d_drawLines(&s_camera, false, false);
				} break;
				case InfVpControl_AngleXY:
				{
					TFE_RenderShared::lineDraw3d_begin(s_viewportSize.x, s_viewportSize.z);
					{
						drawArrow3d(3.0f, 0.1f, ctrl.cen, ctrl.dir, ctrl.nrm, INF_COLOR_EDIT_HELPER);
					}
					TFE_RenderShared::lineDraw3d_drawLines(&s_camera, false, false);
				} break;
			}
		}
		else if ((s_editMode == LevelEditMode::LEDIT_SECTOR || s_editMode == LevelEditMode::LEDIT_WALL) && selection_getCount() <= 1)
		{
			// Handle drawing INF debugging helpers.
			EditorSector* sector = nullptr;
			HitPart part = HP_NONE;
			s32 featureIndex = -1;
			bool hasHovered = selection_hasHovered();
			if (s_editMode == LevelEditMode::LEDIT_SECTOR)
			{
				selection_getSector(hasHovered ? SEL_INDEX_HOVERED : 0, sector);
			}
			else
			{
				selection_getSurface(hasHovered ? SEL_INDEX_HOVERED : 0, sector, featureIndex, &part);
			}
			if (!sector) { return; }

			TFE_RenderShared::lineDraw3d_begin(s_viewportSize.x, s_viewportSize.z);
			Editor_InfItem* item = editor_getInfItem(sector->name.c_str(), featureIndex);
			if (item)
			{
				const s32 classCount = (s32)item->classData.size();
				const Editor_InfClass* const* classList = item->classData.data();
				for (s32 i = 0; i < classCount; i++)
				{
					const Editor_InfClass* classData = classList[i];
					switch (classData->classId)
					{
						case IIC_ELEVATOR:
						{
							Vec3f startPoint = {};
							startPoint.x = (sector->bounds[0].x + sector->bounds[1].x) * 0.5f;
							startPoint.y = (sector->bounds[0].y + sector->bounds[1].y) * 0.5f;
							startPoint.z = (sector->bounds[0].z + sector->bounds[1].z) * 0.5f;

							const Editor_InfElevator* elev = getElevFromClassData(classData);
							// Elevators interact with other items (sectors/walls) via stop messages.
							const s32 stopCount = (s32)elev->stops.size();
							const Editor_InfStop* stop = elev->stops.data();
							for (s32 s = 0; s < stopCount; s++, stop++)
							{
								const s32 msgCount = (s32)stop->msg.size();
								const Editor_InfMessage* msg = stop->msg.data();
								for (s32 m = 0; m < msgCount; m++, msg++)
								{
									const char* targetSectorName = msg->targetSector.c_str();
									const s32 targetWall = msg->targetWall;
									const s32 id = findSectorByName(targetSectorName);
									const EditorSector* targetSector = id >= 0 ? &s_level.sectors[id] : nullptr;
									if (!targetSector) { continue; }

									Vec3f endPoint = { 0 };
									if (targetWall < 0 || targetWall >= (s32)targetSector->walls.size())
									{
										endPoint.x = (targetSector->bounds[0].x + targetSector->bounds[1].x) * 0.5f;
										endPoint.y = (targetSector->bounds[0].y + targetSector->bounds[1].y) * 0.5f;
										endPoint.z = (targetSector->bounds[0].z + targetSector->bounds[1].z) * 0.5f;
									}
									else
									{
										const EditorWall* wall = &targetSector->walls[targetWall];
										const Vec2f* v0 = &targetSector->vtx[wall->idx[0]];
										const Vec2f* v1 = &targetSector->vtx[wall->idx[1]];
										endPoint.x = (v0->x + v1->x) * 0.5f;
										endPoint.y = (targetSector->bounds[0].y + targetSector->bounds[1].y) * 0.5f;
										endPoint.z = (v0->z + v1->z) * 0.5f;
									}

									const Vec3f nrm = s_camera.viewMtx.m2;
									drawArrow3d_Segment(3.0f, 0.16f, startPoint, endPoint, nrm, c_connectMsgColor);
								}
							}
						} break;
						case IIC_TRIGGER:
						{
							const Editor_InfTrigger* trigger = getTriggerFromClassData(classData);
							// Triggers interact with other items (sectors/walls) via clients.
							Vec3f startPoint = {};
							if (item->wallNum >= 0)
							{
								const EditorWall* wall = &sector->walls[item->wallNum];
								const Vec2f* v0 = &sector->vtx[wall->idx[0]];
								const Vec2f* v1 = &sector->vtx[wall->idx[1]];
								startPoint.x = (v0->x + v1->x) * 0.5f;
								startPoint.y = (sector->bounds[0].y + sector->bounds[1].y) * 0.5f;
								startPoint.z = (v0->z + v1->z) * 0.5f;
							}
							else
							{
								startPoint.x = (sector->bounds[0].x + sector->bounds[1].x) * 0.5f;
								startPoint.y = (sector->bounds[0].y + sector->bounds[1].y) * 0.5f;
								startPoint.z = (sector->bounds[0].z + sector->bounds[1].z) * 0.5f;
							}

							const s32 clientCount = (s32)trigger->clients.size();
							const Editor_InfClient* client = trigger->clients.data();
							for (s32 c = 0; c < clientCount; c++, client++)
							{
								const char* targetSectorName = client->targetSector.c_str();
								const s32 targetWall = client->targetWall;
								const s32 id = findSectorByName(targetSectorName);
								const EditorSector* targetSector = id >= 0 ? &s_level.sectors[id] : nullptr;
								if (!targetSector) { continue; }

								Vec3f endPoint = { 0 };
								if (targetWall < 0)
								{
									endPoint.x = (targetSector->bounds[0].x + targetSector->bounds[1].x) * 0.5f;
									endPoint.y = (targetSector->bounds[0].y + targetSector->bounds[1].y) * 0.5f;
									endPoint.z = (targetSector->bounds[0].z + targetSector->bounds[1].z) * 0.5f;
								}
								else
								{
									const EditorWall* wall = &targetSector->walls[targetWall];
									const Vec2f* v0 = &targetSector->vtx[wall->idx[0]];
									const Vec2f* v1 = &targetSector->vtx[wall->idx[1]];
									endPoint.x = (v0->x + v1->x) * 0.5f;
									endPoint.y = (targetSector->bounds[0].y + targetSector->bounds[1].y) * 0.5f;
									endPoint.z = (v0->z + v1->z) * 0.5f;
								}

								const Vec3f nrm = s_camera.viewMtx.m2;
								drawArrow3d_Segment(3.0f, 0.16f, startPoint, endPoint, nrm, c_connectClientColor);
							}
						} break;
						case IIC_TELEPORTER:
						{
							Vec3f startPoint = {};
							startPoint.x = (sector->bounds[0].x + sector->bounds[1].x) * 0.5f;
							startPoint.y = (sector->bounds[0].y + sector->bounds[1].y) * 0.5f;
							startPoint.z = (sector->bounds[0].z + sector->bounds[1].z) * 0.5f;

							const Editor_InfTeleporter* teleporter = getTeleporterFromClassData(classData);
							const s32 id = findSectorByName(teleporter->target.c_str());
							const EditorSector* targetSector = id >= 0 ? &s_level.sectors[id] : nullptr;

							// Teleport interact with other items (sectors) by way of target sector.
							Vec3f endPoint = { 0 };
							if (teleporter->type == TELEPORT_BASIC)
							{
								endPoint = teleporter->dstPos;
							}
							else if (targetSector)
							{
								endPoint.x = (targetSector->bounds[0].x + targetSector->bounds[1].x) * 0.5f;
								endPoint.y = (targetSector->bounds[0].y + targetSector->bounds[1].y) * 0.5f;
								endPoint.z = (targetSector->bounds[0].z + targetSector->bounds[1].z) * 0.5f;
							}

							const Vec3f nrm = s_camera.viewMtx.m2;
							drawArrow3d_Segment(3.0f, 0.16f, startPoint, endPoint, nrm, c_connectTargetColor);
						} break;
					}
				}
			}
			TFE_RenderShared::lineDraw3d_drawLines(&s_camera, false, false);
		}
	}
		
	void renderLevel3DGame()
	{
	#if 0
		if (!s_sectorDraw)
		{
			s_sectorDraw = new TFE_Sectors_GPU();
		}
		generateDfLevelState();
		s_sectorDraw->prepare();
		s_sectorDraw->draw(&s_levelState.sectors[0]);
	#endif
	}

	void renderSectorPolygon2d(const Polygon* poly, u32 color)
	{
		const size_t idxCount = poly->triIdx.size();
		if (idxCount)
		{
			const s32*    idxData = poly->triIdx.data();
			const Vec2f*  vtxData = poly->triVtx.data();
			const size_t vtxCount = poly->triVtx.size();

			s_transformedVtx.resize(vtxCount);
			Vec2f* transVtx = s_transformedVtx.data();
			for (size_t v = 0; v < vtxCount; v++, vtxData++)
			{
				transVtx[v] = { vtxData->x * s_viewportTrans2d.x + s_viewportTrans2d.y, vtxData->z * s_viewportTrans2d.z + s_viewportTrans2d.w };
			}
			triDraw2d_addColored((u32)idxCount, (u32)vtxCount, transVtx, idxData, color);
		}
	}

	void computeFlatUv(const Vec2f* pos, const Vec2f* offset, Vec2f* uv)
	{
		uv->x =  (offset->x - pos->x) / 8.0f;
		uv->z = -(offset->z - pos->z) / 8.0f;
	}

	void renderTexturedSectorPolygon2d(const Polygon* poly, u32 color, EditorTexture* tex, const Vec2f& offset)
	{
		const size_t idxCount = poly->triIdx.size();
		if (idxCount)
		{
			const s32*    idxData = poly->triIdx.data();
			const Vec2f*  vtxData = poly->triVtx.data();
			const size_t vtxCount = poly->triVtx.size();

			s_bufferVec2.resize(vtxCount);
			s_transformedVtx.resize(vtxCount);
			Vec2f* transVtx = s_transformedVtx.data();
			Vec2f* uv = s_bufferVec2.data();
			for (size_t v = 0; v < vtxCount; v++, vtxData++)
			{
				transVtx[v] = { vtxData->x * s_viewportTrans2d.x + s_viewportTrans2d.y, vtxData->z * s_viewportTrans2d.z + s_viewportTrans2d.w };
				computeFlatUv(vtxData, &offset, &uv[v]);
			}
			triDraw2D_addTextured((u32)idxCount, (u32)vtxCount, transVtx, uv, idxData, color, tex ? tex->frames[0] : nullptr);
		}
	}

	void drawWall2d(const EditorSector* sector, const EditorWall* wall, f32 extraScale, Highlight highlight, bool drawNormal)
	{
		Vec2f w0 = sector->vtx[wall->idx[0]];
		Vec2f w1 = sector->vtx[wall->idx[1]];

		// Transformed positions.
		s32 lineCount = 1;
		Vec2f line[4];
		line[0] = { w0.x * s_viewportTrans2d.x + s_viewportTrans2d.y, w0.z * s_viewportTrans2d.z + s_viewportTrans2d.w };
		line[1] = { w1.x * s_viewportTrans2d.x + s_viewportTrans2d.y, w1.z * s_viewportTrans2d.z + s_viewportTrans2d.w };

		if (drawNormal)
		{
			lineCount++;

			Vec2f midPoint = { (line[0].x + line[1].x)*0.5f, (line[0].z + line[1].z)*0.5f };
			Vec2f dir = { -(line[1].z - line[0].z), line[1].x - line[0].x };
			dir = TFE_Math::normalize(&dir);

			line[2] = midPoint;
			line[3] = { midPoint.x + dir.x * 8.0f, midPoint.z + dir.z * 8.0f };
		}

		u32 baseColor;
		if (s_curLayer != sector->layer && !(s_editFlags & LEF_SHOW_ALL_LAYERS))
		{
			u32 alpha = 0x40 / (s_curLayer - sector->layer);
			baseColor = 0x00808000 | (alpha << 24);
		}
		else
		{
			baseColor = wall->adjoinId < 0 ? c_sectorLineClr[highlight] : c_sectorLineClrAdjoin[highlight];
		}
		u32 color[4] = { baseColor, baseColor, baseColor, baseColor };

		TFE_RenderShared::lineDraw2d_addLines(lineCount, 1.25f * extraScale, line, color);
	}

	void drawSector2d(const EditorSector* sector, Highlight highlight)
	{
		EditorSector* hoveredSector = nullptr;
		s32 hoveredFeatureIndex = -1;
		selection_get(SEL_INDEX_HOVERED, hoveredSector, hoveredFeatureIndex);

		// Draw a background polygon to help sectors stand out a bit.
		if (sector_onActiveLayer(sector))
		{
			if (s_sectorDrawMode == SDM_GROUP_COLOR)
			{
				const u32 color = sector_getGroupColor((EditorSector*)sector);
				renderSectorPolygon2d(&sector->poly, color);
			}
			else if (s_sectorDrawMode == SDM_WIREFRAME || (highlight != HL_NONE && highlight != HL_LOCKED))
			{
				u32 color = c_sectorPolyClr[highlight];
				if (highlight == HL_LOCKED) { color &= 0x00ffffff; color |= 0x60000000; }

				renderSectorPolygon2d(&sector->poly, color);
			}
		}

		// Draw lines.
		const size_t wallCount = (s32)sector->walls.size();
		const EditorWall* wall = sector->walls.data();
		const Vec2f* vtx = sector->vtx.data();
		for (s32 w = 0; w < wallCount; w++, wall++)
		{
			// Skip hovered or selected walls.
			if (s_editMode == LEDIT_WALL && ((hoveredSector == sector && hoveredFeatureIndex == w) ||
				selection_action(SA_CHECK_INCLUSION, (EditorSector*)sector, w, HP_MID)))
			{
				continue;
			}
			drawWall2d(sector, wall, 1.0f, highlight);
		}
	}

	void drawSprite2d(const Entity* entity, f32 angle, const Vec3f pos, u32 objColor)
	{
		f32 width = entity->size.x * 0.5f;

		const Vec2f* srcUv = entity->uv;
		const Vec2f* srcSt = entity->st;
		if (entity->views.size() >= 32)
		{
			// Which direction?
			f32 dir = fmodf(angle/(2.0f * PI), 1.0f);
			if (dir < 0.0f) { dir += 1.0f; }

			const s32 viewIndex = s32(dir * 32.0f + 16.0f) & 31;
			assert(viewIndex >= 0 && viewIndex < 32);

			srcUv = entity->views[viewIndex].uv;
			srcSt = entity->views[viewIndex].st;
		}

		f32 dx = fabsf(srcSt[1].x - srcSt[0].x);
		f32 dz = fabsf(srcSt[1].z - srcSt[0].z);
		f32 wI = width, hI = width;
		if (dx > dz)
		{
			hI *= dz / dx;
		}
		else if (dz > dx)
		{
			wI *= dx / dz;
		}

		Vec2f cornersImage[] =
		{
			{ (pos.x - wI) * s_viewportTrans2d.x + s_viewportTrans2d.y, (pos.z - hI) * s_viewportTrans2d.z + s_viewportTrans2d.w },
			{ (pos.x + wI) * s_viewportTrans2d.x + s_viewportTrans2d.y, (pos.z - hI) * s_viewportTrans2d.z + s_viewportTrans2d.w },
			{ (pos.x + wI) * s_viewportTrans2d.x + s_viewportTrans2d.y, (pos.z + hI) * s_viewportTrans2d.z + s_viewportTrans2d.w },
			{ (pos.x - wI) * s_viewportTrans2d.x + s_viewportTrans2d.y, (pos.z + hI) * s_viewportTrans2d.z + s_viewportTrans2d.w }
		};
		Vec2f uv[] =
		{
			{ srcUv[0].x, srcUv[1].z },
			{ srcUv[1].x, srcUv[1].z },
			{ srcUv[1].x, srcUv[0].z },
			{ srcUv[0].x, srcUv[0].z }
		};
		s32 idx[] = { 0, 1, 2, 0, 2, 3 };

		triDraw2D_addTextured(6, 4, cornersImage, uv, idx, objColor, entity->image);
	}

	void drawNoteIcon2d(LevelNote* note, s32 id, u32 objColor)
	{
		const Vec3f pos = note->pos;
		const f32 width = c_levelNoteRadius2d;

		u32 borderColor = objColor;
		if (s_curLevelNote == id)
		{
			borderColor = c_noteColors[s_hoveredLevelNote == id ? 2 : 1];
		}
		else if (s_hoveredLevelNote == id)
		{
			borderColor = c_noteColors[0];
		}

		Vec2f center = { pos.x, pos.z };
		Vec2f size = { width*2.0f, width*2.0f };
		drawBounds2d(&center, size, 1.5f, borderColor, 0);

		Vec2f cornersImage[] =
		{
			{ (pos.x - width) * s_viewportTrans2d.x + s_viewportTrans2d.y, (pos.z - width) * s_viewportTrans2d.z + s_viewportTrans2d.w },
			{ (pos.x + width) * s_viewportTrans2d.x + s_viewportTrans2d.y, (pos.z - width) * s_viewportTrans2d.z + s_viewportTrans2d.w },
			{ (pos.x + width) * s_viewportTrans2d.x + s_viewportTrans2d.y, (pos.z + width) * s_viewportTrans2d.z + s_viewportTrans2d.w },
			{ (pos.x - width) * s_viewportTrans2d.x + s_viewportTrans2d.y, (pos.z + width) * s_viewportTrans2d.z + s_viewportTrans2d.w }
		};

		const f32 cellSize = 34.0f / 272.0f;
		const f32 u0 = 7.0f*cellSize, u1 = u0 + cellSize;
		const f32 v0 = 3.0f*cellSize, v1 = v0 + cellSize;
		const Vec2f uv[] =
		{
			{ u0, v1 },
			{ u1, v1 },
			{ u1, v0 },
			{ u0, v0 }
		};
		const s32 idx[] = { 0, 1, 2, 0, 2, 3 };
		triDraw2D_addTextured(6, 4, cornersImage, uv, idx, objColor, TFE_Editor::getIconAtlas());
	}
		
	void drawEntity2d(const EditorSector* sector, const EditorObject* obj, s32 id, u32 objColor, bool drawEntityBounds)
	{
		const Entity* entity = &s_level.entities[obj->entityId];
		const Vec3f pos = obj->pos;
		const f32 width = entity->size.x * 0.5f;
				
		if (s_editMode == LEDIT_ENTITY && drawEntityBounds)
		{
			EditorSector* hoveredSector = nullptr;
			EditorSector* curSector = nullptr;
			s32 hoveredFeatureIndex = -1, curFeatureIndex = -1;

			selection_getEntity(SEL_INDEX_HOVERED, hoveredSector, hoveredFeatureIndex);
			selection_getEntity(0, curSector, curFeatureIndex);

			if (entity->type == ETYPE_3D)
			{
				draw3dObject(obj, entity, sector, objColor);

				// Draw bounds.
				Highlight hl = HL_NONE;
				if (selection_entity(SA_CHECK_INCLUSION, (EditorSector*)sector, id))
				{
					hl = HL_SELECTED;
				}
				if (sector == hoveredSector && hoveredFeatureIndex == id)
				{
					hl = (hl == HL_SELECTED) ? Highlight(HL_SELECTED + 1) : HL_HOVERED;
				}

				// Draw bounds.
				drawOBB2d(entity->obj3d->bounds, &obj->transform, &obj->pos, 1.5f, c_entityClr[hl]);
			}
			else
			{
				// Draw bounds.
				Vec2f center = { pos.x, pos.z };
				Vec2f size = { width*2.0f, width*2.0f };

				Highlight hl = HL_NONE;
				if (selection_entity(SA_CHECK_INCLUSION, (EditorSector*)sector, id))
				{
					hl = HL_SELECTED;
				}
				if (sector == hoveredSector && hoveredFeatureIndex == id)
				{
					hl = (hl == HL_SELECTED) ? Highlight(HL_SELECTED + 1) : HL_HOVERED;
				}
				drawBounds2d(&center, size, 1.5f, c_entityClr[hl], 0x80403020);
				drawSprite2d(entity, obj->angle, pos, objColor);

				// Draw direction arrows if it has a facing.
				f32 angle = obj->angle;
				Vec2f dir = { sinf(angle), cosf(angle) };
				Vec2f N = { -dir.z, dir.x };

				// Find vertices for the arrow shape.
				const f32 D = width * 0.9f;
				const f32 S = min(0.5f, width);
				const f32 T = min(0.5f, width);

				Vec2f endPt = { pos.x + dir.x*D, pos.z + dir.z*D };
				Vec2f base = { endPt.x - dir.x*T, endPt.z - dir.z*T };
				Vec2f A = { base.x + N.x*S, base.z + N.z*S };
				Vec2f B = { base.x - N.x*S, base.z - N.z*S };

				A.x = A.x * s_viewportTrans2d.x + s_viewportTrans2d.y;
				A.z = A.z * s_viewportTrans2d.z + s_viewportTrans2d.w;
				B.x = B.x * s_viewportTrans2d.x + s_viewportTrans2d.y;
				B.z = B.z * s_viewportTrans2d.z + s_viewportTrans2d.w;
				endPt.x = endPt.x * s_viewportTrans2d.x + s_viewportTrans2d.y;
				endPt.z = endPt.z * s_viewportTrans2d.z + s_viewportTrans2d.w;

				Vec2f dirLines[] =
				{
					{ endPt.x, endPt.z }, A,
					{ endPt.x, endPt.z }, B,
				};

				u32 colorsBg[] = { 0xff402020, 0xff402020, 0xff402020, 0xff402020 };
				lineDraw2d_addLines(2, 2.5f, dirLines, colorsBg);

				u32 colors[] = { 0xffff6060, 0xffff6060, 0xffff6060, 0xffff6060 };
				lineDraw2d_addLines(2, 1.5f, dirLines, colors);
			}
		}
		else if (entity->type == ETYPE_3D)
		{
			draw3dObject(obj, entity, sector, objColor);
		}
		else
		{
			drawSprite2d(entity, obj->angle, pos, objColor);
		}
	}

	static std::vector<EditorSector*> s_sortedSectors;
	bool sortSectorByHeight(const EditorSector* a, const EditorSector* b)
	{
		return a->floorHeight < b->floorHeight;
	}

	void sortSectorPolygons()
	{
		s_sortedSectors.clear();
		const size_t count = s_level.sectors.size();
		EditorSector* sector = s_level.sectors.data();
		for (size_t s = 0; s < count; s++, sector++)
		{
			if (!sector_onActiveLayer(sector)) { continue; }
			if (sector_isHidden(sector)) { continue; }

			s_sortedSectors.push_back(sector);
		}
		std::sort(s_sortedSectors.begin(), s_sortedSectors.end(), sortSectorByHeight);
	}

	void renderSectorPreGrid()
	{
		if (s_sectorDrawMode != SDM_TEXTURED_FLOOR && s_sectorDrawMode != SDM_TEXTURED_CEIL && s_sectorDrawMode != SDM_LIGHTING) { return; }

		// Sort polygons.
		sortSectorPolygons();

		// Draw them bottom to top.
		const size_t count = s_sortedSectors.size();
		EditorSector** sectorList = s_sortedSectors.data();
		for (size_t s = 0; s < count; s++)
		{
			EditorSector* sector = sectorList[s];
			if (!sector_inViewRange(sector)) { continue; }
			bool locked = sector_isLocked(sector);
			
			const u32 colorIndex = (s_editFlags & LEF_FULLBRIGHT) && s_sectorDrawMode != SDM_LIGHTING ? 31 : sector->ambient;
			u32 color = c_sectorTexClr[colorIndex];
			if (locked)
			{
				color = s_sectorDrawMode == SDM_LIGHTING ? SCOLOR_LOCKED : SCOLOR_LOCKED_TEXTURE;
				color &= 0x00ffffff;
				color |= 0x60000000;
			}

			if (s_sectorDrawMode == SDM_LIGHTING)
			{
				renderSectorPolygon2d(&sector->poly, color);
			}
			else if (s_sectorDrawMode == SDM_TEXTURED_FLOOR)
			{
				renderTexturedSectorPolygon2d(&sector->poly, color, getTexture(sector->floorTex.texIndex), sector->floorTex.offset);
			}
			else if (s_sectorDrawMode == SDM_TEXTURED_CEIL)
			{
				renderTexturedSectorPolygon2d(&sector->poly, color, getTexture(sector->ceilTex.texIndex), sector->ceilTex.offset);
			}
		}

		TFE_RenderShared::triDraw2d_draw();
		TFE_RenderShared::triDraw2d_begin(s_viewportSize.x, s_viewportSize.z);
	}

	void drawEditCursor2D(Vec2f* cursorPos)
	{
		// Draw the cursor.
		const u32 cursorColor[] = { 0x80ff8020, 0x80ff8020 };
		const f32 scale = std::min(1.0f, 1.0f / s_zoom2d) * c_vertexSize;
		const Vec2f p0 = { cursorPos->x * s_viewportTrans2d.x + s_viewportTrans2d.y, cursorPos->z * s_viewportTrans2d.z + s_viewportTrans2d.w };
		const Vec2f vtx[] =
		{
			{ p0.x - scale, p0.z - scale },
			{ p0.x + scale, p0.z - scale },
			{ p0.x + scale, p0.z + scale },
			{ p0.x - scale, p0.z + scale },
			{ p0.x - scale, p0.z - scale },
		};
		TFE_RenderShared::lineDraw2d_addLine(1.5f, &vtx[0], cursorColor);
		TFE_RenderShared::lineDraw2d_addLine(1.5f, &vtx[1], cursorColor);
		TFE_RenderShared::lineDraw2d_addLine(1.5f, &vtx[2], cursorColor);
		TFE_RenderShared::lineDraw2d_addLine(1.5f, &vtx[3], cursorColor);
	}

	void drawGuidelineShape2D()
	{
		if (s_editorConfig.interfaceFlags & PIF_HIDE_GUIDELINES)
		{
			return;
		}

		TFE_RenderShared::lineDraw2d_setLineDrawMode(LINE_DRAW_DASHED);

		Guideline* guideline = &s_editGuidelines.guidelines;
		const Vec2f* srcVtx = guideline->vtx.data();
		const f32 width = 2.0f;
		const u32 color[] = { 0xc000a5ff, 0xc000a5ff };
		const u32 curveColor[] = { 0x80a500ff, 0x80a500ff };
		const u32 colorSupportEdge[] = { 0x80ffffff, 0x80ffffff };
		if (s_editGuidelines.drawStarted && s_editGuidelines.drawMode == DMODE_RECT)
		{
			Vec2f p[4];
			getGridOrientedRect(srcVtx[0], srcVtx[1], p);

			Vec2f vtx[] =
			{
				{ p[0].x * s_viewportTrans2d.x + s_viewportTrans2d.y, p[0].z * s_viewportTrans2d.z + s_viewportTrans2d.w },
				{ p[1].x * s_viewportTrans2d.x + s_viewportTrans2d.y, p[1].z * s_viewportTrans2d.z + s_viewportTrans2d.w },
				{ p[2].x * s_viewportTrans2d.x + s_viewportTrans2d.y, p[2].z * s_viewportTrans2d.z + s_viewportTrans2d.w },
				{ p[3].x * s_viewportTrans2d.x + s_viewportTrans2d.y, p[3].z * s_viewportTrans2d.z + s_viewportTrans2d.w },
				{ p[0].x * s_viewportTrans2d.x + s_viewportTrans2d.y, p[0].z * s_viewportTrans2d.z + s_viewportTrans2d.w }
			};
			TFE_RenderShared::lineDraw2d_addLine(width, &vtx[0], color);
			TFE_RenderShared::lineDraw2d_addLine(width, &vtx[1], color);
			TFE_RenderShared::lineDraw2d_addLine(width, &vtx[2], color);
			TFE_RenderShared::lineDraw2d_addLine(width, &vtx[3], color);
		}
		else if (s_editGuidelines.drawStarted && s_editGuidelines.drawMode == DMODE_SHAPE)
		{
			const size_t count = guideline->edge.size();
			const Vec2f* vtx = guideline->vtx.data();
			const GuidelineEdge* edge = guideline->edge.data();

			for (size_t e = 0; e < count; e++, edge++)
			{
				const Vec2f* v0 = &vtx[edge->idx[0]];
				const Vec2f* v1 = &vtx[edge->idx[1]];
				if (edge->idx[2] >= 0)
				{
					const Vec2f* c = &vtx[edge->idx[2]];
					// Curve.
					const Vec2f curveVtx[] =
					{
						{ v0->x*s_viewportTrans2d.x + s_viewportTrans2d.y, v0->z * s_viewportTrans2d.z + s_viewportTrans2d.w },
						{ v1->x*s_viewportTrans2d.x + s_viewportTrans2d.y, v1->z * s_viewportTrans2d.z + s_viewportTrans2d.w },
						{ c->x*s_viewportTrans2d.x + s_viewportTrans2d.y, c->z * s_viewportTrans2d.z + s_viewportTrans2d.w },
					};
					TFE_RenderShared::lineDraw2d_addCurve(curveVtx, color[0]);
				}
				else
				{
					// Straight line.
					const Vec2f line[] =
					{
						{ v0->x * s_viewportTrans2d.x + s_viewportTrans2d.y, v0->z * s_viewportTrans2d.z + s_viewportTrans2d.w },
						{ v1->x * s_viewportTrans2d.x + s_viewportTrans2d.y, v1->z * s_viewportTrans2d.z + s_viewportTrans2d.w }
					};
					TFE_RenderShared::lineDraw2d_addLine(2.0f, line, color);
				}
			}
			// Draw from the last vertex to curPos.
			const Vec2f* v0 = guideline->edge.empty() ? &vtx[0] : &vtx[guideline->edge.back().idx[1]];
			const Vec2f lineVtx[] =
			{
				{v0->x * s_viewportTrans2d.x + s_viewportTrans2d.y, v0->z * s_viewportTrans2d.z + s_viewportTrans2d.w},
				{s_editGuidelines.drawCurPos.x * s_viewportTrans2d.x + s_viewportTrans2d.y,  s_editGuidelines.drawCurPos.z * s_viewportTrans2d.z + s_viewportTrans2d.w}
			};
			TFE_RenderShared::lineDraw2d_addLine(width, lineVtx, (s_viewportRenderFlags & VRF_CURVE_MOD) ? curveColor : color);
		}
		else if (s_editGuidelines.drawStarted && s_editGuidelines.drawMode == DMODE_CURVE_CONTROL)
		{
			const size_t count = guideline->edge.size();
			const Vec2f* vtx = guideline->vtx.data();
			const GuidelineEdge* edge = guideline->edge.data();

			for (size_t e = 0; e < count - 1; e++, edge++)
			{
				const Vec2f* v0 = &vtx[edge->idx[0]];
				const Vec2f* v1 = &vtx[edge->idx[1]];
				if (edge->idx[2] >= 0)
				{
					const Vec2f* c = &vtx[edge->idx[2]];
					// Curve.
					const Vec2f curveVtx[] =
					{
						{ v0->x*s_viewportTrans2d.x + s_viewportTrans2d.y, v0->z * s_viewportTrans2d.z + s_viewportTrans2d.w },
						{ v1->x*s_viewportTrans2d.x + s_viewportTrans2d.y, v1->z * s_viewportTrans2d.z + s_viewportTrans2d.w },
						{ c->x*s_viewportTrans2d.x + s_viewportTrans2d.y, c->z * s_viewportTrans2d.z + s_viewportTrans2d.w },
					};
					TFE_RenderShared::lineDraw2d_addCurve(curveVtx, color[0]);
				}
				else
				{
					// Straight line.
					const Vec2f line[] =
					{
						{ v0->x * s_viewportTrans2d.x + s_viewportTrans2d.y, v0->z * s_viewportTrans2d.z + s_viewportTrans2d.w },
						{ v1->x * s_viewportTrans2d.x + s_viewportTrans2d.y, v1->z * s_viewportTrans2d.z + s_viewportTrans2d.w }
					};
					TFE_RenderShared::lineDraw2d_addLine(2.0f, line, color);
				}
			}
			// Support triangle.
			edge = &guideline->edge.back();
			{
				const Vec2f lineVtx0[] =
				{
					{ vtx[edge->idx[0]].x * s_viewportTrans2d.x + s_viewportTrans2d.y, vtx[edge->idx[0]].z * s_viewportTrans2d.z + s_viewportTrans2d.w },
					{ s_editGuidelines.drawCurPos.x * s_viewportTrans2d.x + s_viewportTrans2d.y, s_editGuidelines.drawCurPos.z * s_viewportTrans2d.z + s_viewportTrans2d.w }
				};
				TFE_RenderShared::lineDraw2d_addLine(width, lineVtx0, colorSupportEdge);

				const Vec2f lineVtx1[] =
				{
					{ vtx[edge->idx[1]].x * s_viewportTrans2d.x + s_viewportTrans2d.y, vtx[edge->idx[1]].z * s_viewportTrans2d.z + s_viewportTrans2d.w },
					{ s_editGuidelines.drawCurPos.x * s_viewportTrans2d.x + s_viewportTrans2d.y, s_editGuidelines.drawCurPos.z * s_viewportTrans2d.z + s_viewportTrans2d.w }
				};
				TFE_RenderShared::lineDraw2d_addLine(width, lineVtx1, colorSupportEdge);
			}
			// Curve.
			const Vec2f* c = &s_editGuidelines.drawCurPos;
			// Curve.
			const Vec2f curveVtx[] =
			{
				{ vtx[edge->idx[0]].x*s_viewportTrans2d.x + s_viewportTrans2d.y, vtx[edge->idx[0]].z * s_viewportTrans2d.z + s_viewportTrans2d.w },
				{ vtx[edge->idx[1]].x*s_viewportTrans2d.x + s_viewportTrans2d.y, vtx[edge->idx[1]].z * s_viewportTrans2d.z + s_viewportTrans2d.w },
				{ c->x*s_viewportTrans2d.x + s_viewportTrans2d.y, c->z * s_viewportTrans2d.z + s_viewportTrans2d.w },
			};
			TFE_RenderShared::lineDraw2d_addCurve(curveVtx, curveColor[0]);
		}

		TFE_RenderShared::lineDraw2d_setLineDrawMode();
		drawEditCursor2D(&s_editGuidelines.drawCurPos);
	}

	void drawGuidelineCurve2d(const Vec4f viewportBoundsWS, const Vec2f* vtx, const Guideline* guideline, const GuidelineEdge* edge, const u32* drawColor)
	{
		const Vec2f* v0 = &vtx[edge->idx[0]];
		const Vec2f* v1 = &vtx[edge->idx[1]];
		const Vec2f* c = &vtx[edge->idx[2]];

		// Cull the curve if the bounds are outside of the view.
		const f32 maxOffset = guideline->maxOffset;
		const Vec4f curveBounds = { min3(v0->x, v1->x, c->x)-maxOffset, min3(v0->z, v1->z, c->z)-maxOffset,
			                        max3(v0->x, v1->x, c->x)+maxOffset, max3(v0->z, v1->z, c->z)+maxOffset };
		if (!boundsOverlap(curveBounds, viewportBoundsWS)) { return; }
		// Curve.
		const Vec2f curveVtx[] =
		{
			{ v0->x*s_viewportTrans2d.x + s_viewportTrans2d.y, v0->z * s_viewportTrans2d.z + s_viewportTrans2d.w },
			{ v1->x*s_viewportTrans2d.x + s_viewportTrans2d.y, v1->z * s_viewportTrans2d.z + s_viewportTrans2d.w },
			{ c->x*s_viewportTrans2d.x + s_viewportTrans2d.y,  c->z * s_viewportTrans2d.z + s_viewportTrans2d.w },
		};
		const s32 count = std::min(4, (s32)guideline->offsets.size());
		const f32* offsetBase = guideline->offsets.data();
		f32 offset[4];
		for (s32 i = 0; i < count; i++)
		{
			offset[i] = offsetBase[i] * s_viewportTrans2d.x;
		}
		TFE_RenderShared::lineDraw2d_addCurve(curveVtx, drawColor[0], count, offset);
	}

	void drawGuidelineOffset2d(const Vec4f viewportBoundsWS, const Vec2f* vtx, const GuidelineEdge* edge, u32 drawColor, f32 offset)
	{
		const Vec2f* v0 = &vtx[edge->idx[0]];
		const Vec2f* v1 = &vtx[edge->idx[1]];

		Vec2f p0 = *v0;
		Vec2f p1 = *v1;
		if (offset)
		{
			Vec2f d = { v1->x - v0->x, v1->z - v0->z };
			Vec2f n = { -d.z, d.x };
			n = TFE_Math::normalize(&n);

			p0.x = v0->x + n.x*offset;
			p0.z = v0->z + n.z*offset;
			p1.x = v1->x + n.x*offset;
			p1.z = v1->z + n.z*offset;
		}

		// Cull the line if the bounds are outside of the view.
		const Vec4f lineBounds = { std::min(p0.x, p1.x), std::min(p0.z, p1.z), std::max(p0.x, p1.x), std::max(p0.z, p1.z) };
		if (!boundsOverlap(lineBounds, viewportBoundsWS)) { return; }
		// Straight line.
		const Vec2f line[] =
		{
			{ p0.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p0.z * s_viewportTrans2d.z + s_viewportTrans2d.w },
			{ p1.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p1.z * s_viewportTrans2d.z + s_viewportTrans2d.w }
		};
		u32 color = drawColor;
		if (offset != 0.0f)
		{
			u32 alpha = color >> 24u;
			alpha >>= 1; // use the same scale value as the curve shader.
			color &= 0x00ffffff;
			color |= (alpha << 24u);
		}
		u32 colorList[] = { color, color };

		TFE_RenderShared::lineDraw2d_addLine(2.0f, line, colorList);
	}

	void renderGuidelines2d(const Vec4f viewportBoundsWS)
	{
		if (s_editorConfig.interfaceFlags & PIF_HIDE_GUIDELINES)
		{
			return;
		}

		const u32 colors[] = { 0xc000a5ff, 0xc000a5ff };
		const u32 hovered[] = { 0xffffa500, 0xffffa500 };
		const u32 selected[] = { 0xff0030ff, 0xff0030ff };
		const u32 hoveredSelected[] = { 0xffff00ff, 0xffff00ff };

		const u32 detail[] = { 0xc0ffa500, 0xc0ffa500 };
		const Vec4f pointBoundsWS = { s_cursor3d.x, s_cursor3d.z, s_cursor3d.x, s_cursor3d.z };

		const size_t count = s_level.guidelines.size();
		Guideline* guideLine = s_level.guidelines.data();
		lineDraw2d_setLineDrawMode(LINE_DRAW_DASHED);
		for (size_t i = 0; i < count; i++, guideLine++)
		{
			// Cull the entire guideline set.
			if (!boundsOverlap(guideLine->bounds, viewportBoundsWS)) { continue; }
			if (guideLine->flags & GLFLAG_LIMIT_HEIGHT)
			{
				if (s_grid.height < guideLine->minHeight || s_grid.height > guideLine->maxHeight)
				{
					continue;
				}
			}

			const size_t edgeCount = guideLine->edge.size();
			const Vec2f* vtx = guideLine->vtx.data();
			const GuidelineEdge* edge = guideLine->edge.data();

			const size_t offsetCount = guideLine->offsets.size();

			const u32* drawColor = colors;
			if (s_hoveredGuideline == i && s_curGuideline == i)
			{
				drawColor = hoveredSelected;
			}
			else if (s_hoveredGuideline == i)
			{
				drawColor = hovered;
			}
			else if (s_curGuideline == i)
			{
				drawColor = selected;
			}

			for (size_t e = 0; e < edgeCount; e++, edge++)
			{
				if (edge->idx[2] >= 0) // Curve.
				{
					drawGuidelineCurve2d(viewportBoundsWS, vtx, guideLine, edge, drawColor);
				}
				else
				{
					drawGuidelineOffset2d(viewportBoundsWS, vtx, edge, drawColor[0], 0.0f);

					const f32* offset = guideLine->offsets.data();
					for (size_t o = 0; o < offsetCount; o++)
					{
						if (offset[o] == 0.0f) { continue; }
						drawGuidelineOffset2d(viewportBoundsWS, vtx, edge, drawColor[0], offset[o]);
					}
				}
			}
		}
		lineDraw2d_setLineDrawMode();

		// Draw guideline knots.
		guideLine = s_level.guidelines.data();
		for (size_t g = 0; g < count; g++, guideLine++)
		{
			// Cull the entire guideline set.
			if (!boundsOverlap(guideLine->bounds, viewportBoundsWS)) { continue; }
			if (guideLine->flags & GLFLAG_DISABLE_SNAPPING) { continue; }  // Don't show knots if snapping is disabled.
			if ((guideLine->flags & GLFLAG_LIMIT_HEIGHT) && (s_grid.height < guideLine->minHeight || s_grid.height > guideLine->maxHeight))
			{
				continue;
			}
			const size_t knotCount = guideLine->knots.size();
			const Vec4f* knot = guideLine->knots.data();
			for (size_t k = 0; k < knotCount; k++, knot++)
			{
				const Vec2f pos = { knot->x, knot->z };
				drawPositionKnot2d(2.0f, pos, 0xffffa500);
			}

			const size_t subdivCount = guideLine->subdiv.size();
			const size_t offsetCount = guideLine->offsets.size();
			const GuidelineSubDiv* subdiv = guideLine->subdiv.data();
			const f32* offsets = guideLine->offsets.data();
			const Vec2f* vtx = guideLine->vtx.data();
			for (size_t k = 0; k < subdivCount; k++, subdiv++)
			{
				const s32 e = subdiv->edge;
				const f32 u = subdiv->param;
				const GuidelineEdge* edge = &guideLine->edge[e];
				const Vec2f v0 = vtx[edge->idx[0]];
				const Vec2f v1 = vtx[edge->idx[1]];
				Vec2f pos, nrm;
				if (edge->idx[2] >= 0) // Curve
				{
					const Vec2f c = vtx[edge->idx[2]];
					evaluateQuadraticBezier(v0, v1, c, u, &pos, &nrm);
				}
				else // Line
				{
					pos.x = v0.x*(1.0f - u) + v1.x*u;
					pos.z = v0.z*(1.0f - u) + v1.z*u;
					nrm.x = -(v1.z - v0.z);
					nrm.z =   v1.x - v0.x;
					nrm = TFE_Math::normalize(&nrm);
				}

				drawPositionKnot2d(1.5f, pos, 0x8000a5ff);
				for (size_t o = 0; o < offsetCount; o++)
				{
					const Vec2f offsetPos = { pos.x + nrm.x * offsets[o], pos.z + nrm.z * offsets[o] };
					drawPositionKnot2d(1.5f, offsetPos, 0x8000a5ff);
				}
			}
		}
	}
	
	void renderSectorWalls2d(s32 layerStart, s32 layerEnd)
	{
		if (layerEnd < layerStart) { return; }

		EditorSector* hoveredSector = nullptr;
		if (s_editMode == LEDIT_SECTOR)
		{
			selection_getSector(SEL_INDEX_HOVERED, hoveredSector);
		}
				
		const size_t count = s_level.sectors.size();
		EditorSector* sector = s_level.sectors.data();
		for (size_t s = 0; s < count; s++, sector++)
		{
			if (sector->layer < layerStart || sector->layer > layerEnd) { continue; }
			if (sector_isHidden(sector)) { continue; }
			if (s_editMode == LEDIT_SECTOR && (sector == hoveredSector || selection_sector(SA_CHECK_INCLUSION, sector))) { continue; }
			if (!sector_inViewRange(sector)) { continue; }

			drawSector2d(sector, sector_isLocked(sector) ? HL_LOCKED : HL_NONE);
		}
	}

	void drawVertex2d(const Vec2f* pos, f32 scale, Highlight highlight)
	{
		u32 color = highlight == HL_LOCKED ? (u32)SCOLOR_LOCKED_VTX : c_vertexClr[highlight];
		u32 colors[] = { color, color };

		const Vec2f p0 = { pos->x * s_viewportTrans2d.x + s_viewportTrans2d.y, pos->z * s_viewportTrans2d.z + s_viewportTrans2d.w };
		const Vec2f vtx[]=
		{
			{ p0.x - scale, p0.z - scale },
			{ p0.x + scale, p0.z - scale },
			{ p0.x + scale, p0.z + scale },
			{ p0.x - scale, p0.z + scale },
			{ p0.x - scale, p0.z - scale },
		};
		TFE_RenderShared::lineDraw2d_addLine(1.5f, &vtx[0], colors);
		TFE_RenderShared::lineDraw2d_addLine(1.5f, &vtx[1], colors);
		TFE_RenderShared::lineDraw2d_addLine(1.5f, &vtx[2], colors);
		TFE_RenderShared::lineDraw2d_addLine(1.5f, &vtx[3], colors);
	}

	void drawVertex2d(const EditorSector* sector, s32 id, f32 extraScale, Highlight highlight)
	{
		const Vec2f* pos = &sector->vtx[id];
		const f32 scale = std::min(1.0f, 1.0f / s_zoom2d) * c_vertexSize * extraScale;
		drawVertex2d(pos, scale, highlight);
	}
		
	void renderSectorVertices2d()
	{
		const u32 color[4] = { 0xffae8653, 0xffae8653, 0xff51331a, 0xff51331a };
		const u32 colorSelected[4] = { 0xffffc379, 0xffffc379, 0xff764a26, 0xff764a26 };
		const f32 scale = std::min(1.0f, 1.0f/s_zoom2d) * c_vertexSize;

		EditorSector* hoveredSector = nullptr;
		EditorSector* curSector = nullptr;
		s32 hoveredFeatureIndex = -1, curFeatureIndex = -1;
		if (s_editMode == LEDIT_VERTEX)
		{
			selection_getVertex(SEL_INDEX_HOVERED, hoveredSector, hoveredFeatureIndex);
			selection_getVertex(0, curSector, curFeatureIndex);
		}

		const size_t sectorCount = s_level.sectors.size();
		EditorSector* sector = s_level.sectors.data();
		for (size_t s = 0; s < sectorCount; s++, sector++)
		{
			if (!sector_onActiveLayer(sector)) { continue; }
			if (sector_isHidden(sector)) { continue; }

			const size_t vtxCount = sector->vtx.size();
			const Vec2f* vtx = sector->vtx.data();
			for (size_t v = 0; v < vtxCount; v++, vtx++)
			{
				// Skip drawing hovered/selected vertices.
				if (s_editMode == LEDIT_VERTEX && ((sector == hoveredSector && v == hoveredFeatureIndex) ||
					(sector == curSector && v == curFeatureIndex)))
				{
					continue;
				}
				drawVertex2d(vtx, scale, sector_isLocked(sector) ? HL_LOCKED : HL_NONE);
			}
		}
	}

	void drawSolidBox(const Vec3f* center, f32 side, u32 color)
	{
		const f32 w = side * 0.5f;
		const Vec3f vtx[8] =
		{
			{center->x - w, center->y - w, center->z - w},
			{center->x + w, center->y - w, center->z - w},
			{center->x + w, center->y - w, center->z + w},
			{center->x - w, center->y - w, center->z + w},

			{center->x - w, center->y + w, center->z - w},
			{center->x + w, center->y + w, center->z - w},
			{center->x + w, center->y + w, center->z + w},
			{center->x - w, center->y + w, center->z + w},
		};
		const s32 idx[36] =
		{
			0, 1, 2, 0, 2, 3,	// bot
			4, 6, 5, 4, 7, 6,	// top
			0, 5, 1, 0, 4, 5,
			1, 6, 2, 1, 5, 6,
			2, 7, 3, 2, 6, 7,
			3, 4, 0, 3, 7, 4
		};
		triDraw3d_addColored(TRIMODE_OPAQUE, 36, 8, vtx, idx, color, false);
	}
	
	void drawBounds(const Vec3f* center, Vec3f size, f32 lineWidth, u32 color)
	{
		const f32 w = size.x * 0.5f;
		const f32 h = size.y * 0.5f;
		const f32 d = size.z * 0.5f;
		const Vec3f lines[] =
		{
			{center->x - w, center->y - h, center->z - d}, {center->x + w, center->y - h, center->z - d},
			{center->x + w, center->y - h, center->z - d}, {center->x + w, center->y - h, center->z + d},
			{center->x + w, center->y - h, center->z + d}, {center->x - w, center->y - h, center->z + d},
			{center->x - w, center->y - h, center->z + d}, {center->x - w, center->y - h, center->z - d},

			{center->x - w, center->y + h, center->z - d}, {center->x + w, center->y + h, center->z - d},
			{center->x + w, center->y + h, center->z - d}, {center->x + w, center->y + h, center->z + d},
			{center->x + w, center->y + h, center->z + d}, {center->x - w, center->y + h, center->z + d},
			{center->x - w, center->y + h, center->z + d}, {center->x - w, center->y + h, center->z - d},

			{center->x - w, center->y - h, center->z - d}, {center->x - w, center->y + h, center->z - d},
			{center->x + w, center->y - h, center->z - d}, {center->x + w, center->y + h, center->z - d},
			{center->x + w, center->y - h, center->z + d}, {center->x + w, center->y + h, center->z + d},
			{center->x - w, center->y - h, center->z + d}, {center->x - w, center->y + h, center->z + d},
		};
		u32 colors[12];
		for (u32 i = 0; i < 12; i++)
		{
			colors[i] = color;
		}

		lineDraw3d_addLines(12, lineWidth, lines, colors);
	}

	void drawOBB(const Vec3f* bounds, const Mat3* mtx, const Vec3f* pos, f32 lineWidth, u32 color)
	{
		const Vec3f vtxLocal[8] =
		{
			{ bounds[0].x, bounds[0].y, bounds[0].z },
			{ bounds[1].x, bounds[0].y, bounds[0].z },
			{ bounds[1].x, bounds[0].y, bounds[1].z },
			{ bounds[0].x, bounds[0].y, bounds[1].z },

			{ bounds[0].x, bounds[1].y, bounds[0].z },
			{ bounds[1].x, bounds[1].y, bounds[0].z },
			{ bounds[1].x, bounds[1].y, bounds[1].z },
			{ bounds[0].x, bounds[1].y, bounds[1].z },
		};
		Vec3f vtxWorld[10];
		for (s32 v = 0; v < 4; v++)
		{
			vtxWorld[v].x = vtxLocal[v].x * mtx->m0.x + vtxLocal[v].y * mtx->m0.y + vtxLocal[v].z * mtx->m0.z + pos->x;
			vtxWorld[v].y = vtxLocal[v].x * mtx->m1.x + vtxLocal[v].y * mtx->m1.y + vtxLocal[v].z * mtx->m1.z + pos->y;
			vtxWorld[v].z = vtxLocal[v].x * mtx->m2.x + vtxLocal[v].y * mtx->m2.y + vtxLocal[v].z * mtx->m2.z + pos->z;
		}
		vtxWorld[4] = vtxWorld[0];
		for (s32 i = 0; i < 4; i++)
		{
			lineDraw3d_addLine(lineWidth, &vtxWorld[i], &color);
		}
		
		for (s32 v = 4; v < 8; v++)
		{
			vtxWorld[v].x = vtxLocal[v].x * mtx->m0.x + vtxLocal[v].y * mtx->m0.y + vtxLocal[v].z * mtx->m0.z + pos->x;
			vtxWorld[v].y = vtxLocal[v].x * mtx->m1.x + vtxLocal[v].y * mtx->m1.y + vtxLocal[v].z * mtx->m1.z + pos->y;
			vtxWorld[v].z = vtxLocal[v].x * mtx->m2.x + vtxLocal[v].y * mtx->m2.y + vtxLocal[v].z * mtx->m2.z + pos->z;
		}
		vtxWorld[8] = vtxWorld[4];
		for (s32 i = 0; i < 4; i++)
		{
			lineDraw3d_addLine(lineWidth, &vtxWorld[i+4], &color);
		}

		Vec3f vertLine[2];
		for (s32 i = 0; i < 4; i++)
		{
			vertLine[0] = vtxWorld[i];
			vertLine[1] = vtxWorld[i+4];
			lineDraw3d_addLine(lineWidth, vertLine, &color);
		}
	}

	void drawBounds2d(const Vec2f* center, Vec2f size, f32 lineWidth, u32 color, u32 fillColor)
	{
		const f32 w = size.x * 0.5f;
		const f32 d = size.z * 0.5f;
		const f32 x0 = (center->x - w)*s_viewportTrans2d.x + s_viewportTrans2d.y;
		const f32 x1 = (center->x + w)*s_viewportTrans2d.x + s_viewportTrans2d.y;
		const f32 z0 = (center->z - w)*s_viewportTrans2d.z + s_viewportTrans2d.w;
		const f32 z1 = (center->z + w)*s_viewportTrans2d.z + s_viewportTrans2d.w;

		const Vec2f lines[] =
		{
			{x0, z0}, {x1, z0},
			{x1, z0}, {x1, z1},
			{x1, z1}, {x0, z1},
			{x0, z1}, {x0, z0},
		};
		u32 colors[8];
		for (u32 i = 0; i < 8; i++)
		{
			colors[i] = color;
		}

		lineDraw2d_addLines(4, lineWidth, lines, colors);

		if (fillColor)
		{
			Vec2f vtx[] = { lines[0], lines[2], lines[4], lines[6] };
			s32 idx[6] = { 0, 1, 2, 0, 2, 3 };
			triDraw2d_addColored(6, 4, vtx, idx, fillColor);
		}
	}

	void drawOBB2d(const Vec3f* bounds, const Mat3* mtx, const Vec3f* pos, f32 lineWidth, u32 color)
	{
		const Vec3f vtxLocal[8] =
		{
			{ bounds[0].x, bounds[0].y, bounds[0].z },
			{ bounds[1].x, bounds[0].y, bounds[0].z },
			{ bounds[1].x, bounds[0].y, bounds[1].z },
			{ bounds[0].x, bounds[0].y, bounds[1].z },

			{ bounds[0].x, bounds[1].y, bounds[0].z },
			{ bounds[1].x, bounds[1].y, bounds[0].z },
			{ bounds[1].x, bounds[1].y, bounds[1].z },
			{ bounds[0].x, bounds[1].y, bounds[1].z },
		};
		Vec2f vtxWorld[10];
		Vec2f vtxWorld2d[10];
		for (s32 v = 0; v < 4; v++)
		{
			vtxWorld[v].x = vtxLocal[v].x * mtx->m0.x + vtxLocal[v].y * mtx->m0.y + vtxLocal[v].z * mtx->m0.z + pos->x;
			vtxWorld[v].z = vtxLocal[v].x * mtx->m2.x + vtxLocal[v].y * mtx->m2.y + vtxLocal[v].z * mtx->m2.z + pos->z;

			vtxWorld2d[v].x = vtxWorld[v].x*s_viewportTrans2d.x + s_viewportTrans2d.y;
			vtxWorld2d[v].z = vtxWorld[v].z*s_viewportTrans2d.z + s_viewportTrans2d.w;
		}
		vtxWorld2d[4] = vtxWorld2d[0];
		for (s32 i = 0; i < 4; i++)
		{
			lineDraw2d_addLine(lineWidth, &vtxWorld2d[i], &color);
		}

		for (s32 v = 4; v < 8; v++)
		{
			vtxWorld[v].x = vtxLocal[v].x * mtx->m0.x + vtxLocal[v].y * mtx->m0.y + vtxLocal[v].z * mtx->m0.z + pos->x;
			vtxWorld[v].z = vtxLocal[v].x * mtx->m2.x + vtxLocal[v].y * mtx->m2.y + vtxLocal[v].z * mtx->m2.z + pos->z;

			vtxWorld2d[v].x = vtxWorld[v].x*s_viewportTrans2d.x + s_viewportTrans2d.y;
			vtxWorld2d[v].z = vtxWorld[v].z*s_viewportTrans2d.z + s_viewportTrans2d.w;
		}
		vtxWorld2d[8] = vtxWorld2d[4];
		u32 colors[] = { color, color };
		for (s32 i = 0; i < 4; i++)
		{
			lineDraw2d_addLine(lineWidth, &vtxWorld2d[i + 4], colors);
		}

		Vec2f vertLine[2];
		for (s32 i = 0; i < 4; i++)
		{
			vertLine[0] = vtxWorld2d[i];
			vertLine[1] = vtxWorld2d[i + 4];
			lineDraw2d_addLine(lineWidth, vertLine, colors);
		}
	}

	void drawPositionKnot2d(f32 width, Vec2f pos, u32 color)
	{
		f32 step = 3.0f / s_viewportTrans2d.x;
		Vec2f p0 = { pos.x - step, pos.z - step };
		Vec2f p1 = { pos.x + step, pos.z - step };
		Vec2f p2 = { pos.x + step, pos.z + step };
		Vec2f p3 = { pos.x - step, pos.z + step };
		Vec2f vtx[] =
		{
			{ p0.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p0.z * s_viewportTrans2d.z + s_viewportTrans2d.w },
			{ p1.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p1.z * s_viewportTrans2d.z + s_viewportTrans2d.w },
			{ p2.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p2.z * s_viewportTrans2d.z + s_viewportTrans2d.w },
			{ p3.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p3.z * s_viewportTrans2d.z + s_viewportTrans2d.w },
			{ p0.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p0.z * s_viewportTrans2d.z + s_viewportTrans2d.w },
		};

		// Draw lines through the center point.
		const u32 clr[] = { color, color };
		TFE_RenderShared::lineDraw2d_addLine(width, &vtx[0], clr);
		TFE_RenderShared::lineDraw2d_addLine(width, &vtx[1], clr);
		TFE_RenderShared::lineDraw2d_addLine(width, &vtx[2], clr);
		TFE_RenderShared::lineDraw2d_addLine(width, &vtx[3], clr);
	}
		
	void drawPosition2d(f32 width, Vec2f pos, u32 color)
	{
		f32 step = 5.0f / s_viewportTrans2d.x;
		Vec2f p0 = { pos.x - step, pos.z - step };
		Vec2f p1 = { pos.x + step, pos.z + step };
		Vec2f p2 = { pos.x + step, pos.z - step };
		Vec2f p3 = { pos.x - step, pos.z + step };
		Vec2f vtx[] =
		{
			{ p0.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p0.z * s_viewportTrans2d.z + s_viewportTrans2d.w },
			{ p1.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p1.z * s_viewportTrans2d.z + s_viewportTrans2d.w },

			{ p2.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p2.z * s_viewportTrans2d.z + s_viewportTrans2d.w },
			{ p3.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p3.z * s_viewportTrans2d.z + s_viewportTrans2d.w },
		};

		// Draw lines through the center point.
		const u32 clr[] = { color, color };
		TFE_RenderShared::lineDraw2d_addLine(width, &vtx[0], clr);
		TFE_RenderShared::lineDraw2d_addLine(width, &vtx[2], clr);
	}

	void drawArrow2d(f32 width, f32 lenInPixels, Vec2f pos, Vec2f dir, u32 color)
	{
		f32 step = lenInPixels / s_viewportTrans2d.x;
		f32 partStep = step * 0.25f;
		Vec2f tan = { -dir.z, dir.x };

		Vec2f p0 = { pos.x, pos.z };
		Vec2f p1 = { p0.x + dir.x*step, p0.z + dir.z*step };
		Vec2f p2 = { p1.x - dir.x*partStep - tan.x*partStep, p1.z - dir.z*partStep - tan.z*partStep };
		Vec2f p3 = { p1.x - dir.x*partStep + tan.x*partStep, p1.z - dir.z*partStep + tan.z*partStep };

		Vec2f vtx[] =
		{
			{ p0.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p0.z * s_viewportTrans2d.z + s_viewportTrans2d.w },
			{ p1.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p1.z * s_viewportTrans2d.z + s_viewportTrans2d.w },

			{ p1.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p1.z * s_viewportTrans2d.z + s_viewportTrans2d.w },
			{ p2.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p2.z * s_viewportTrans2d.z + s_viewportTrans2d.w },

			{ p1.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p1.z * s_viewportTrans2d.z + s_viewportTrans2d.w },
			{ p3.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p3.z * s_viewportTrans2d.z + s_viewportTrans2d.w },
		};

		// Draw lines through the center point.
		const u32 clr[] = { color, color };
		TFE_RenderShared::lineDraw2d_addLine(width, &vtx[0], clr);
		TFE_RenderShared::lineDraw2d_addLine(width, &vtx[2], clr);
		TFE_RenderShared::lineDraw2d_addLine(width, &vtx[4], clr);
	}

	void drawArrow2d_Segment(f32 width, f32 lenInPixels, Vec2f pos0, Vec2f pos1, u32 color)
	{
		f32 step = lenInPixels / s_viewportTrans2d.x;
		f32 partStep = step * 0.25f;

		Vec2f dir = { pos1.x - pos0.x, pos1.z - pos0.z };
		dir = TFE_Math::normalize(&dir);
		Vec2f tan = { -dir.z, dir.x };

		Vec2f p0 = pos0;
		Vec2f p1 = pos1;
		Vec2f p2 = { p1.x - dir.x*partStep - tan.x*partStep, p1.z - dir.z*partStep - tan.z*partStep };
		Vec2f p3 = { p1.x - dir.x*partStep + tan.x*partStep, p1.z - dir.z*partStep + tan.z*partStep };

		Vec2f vtx[] =
		{
			{ p0.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p0.z * s_viewportTrans2d.z + s_viewportTrans2d.w },
			{ p1.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p1.z * s_viewportTrans2d.z + s_viewportTrans2d.w },

			{ p1.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p1.z * s_viewportTrans2d.z + s_viewportTrans2d.w },
			{ p2.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p2.z * s_viewportTrans2d.z + s_viewportTrans2d.w },

			{ p1.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p1.z * s_viewportTrans2d.z + s_viewportTrans2d.w },
			{ p3.x * s_viewportTrans2d.x + s_viewportTrans2d.y, p3.z * s_viewportTrans2d.z + s_viewportTrans2d.w },
		};

		// Draw lines through the center point.
		const u32 clr[] = { color, color };
		TFE_RenderShared::lineDraw2d_addLine(width, &vtx[0], clr);
		TFE_RenderShared::lineDraw2d_addLine(width, &vtx[2], clr);
		TFE_RenderShared::lineDraw2d_addLine(width, &vtx[4], clr);
	}
		
	void drawPosition3d(f32 width, Vec3f pos, u32 color)
	{
		Vec3f offset = { pos.x - s_camera.pos.x, pos.y - s_camera.pos.y, pos.z - s_camera.pos.z };
		f32 dist = sqrtf(offset.x*offset.x + offset.y*offset.y + offset.z*offset.z);
		f32 scale = dist;

		f32 step = 0.02f*scale;
		Vec3f p0 = { pos.x - step, pos.y, pos.z - step };
		Vec3f p1 = { pos.x + step, pos.y, pos.z + step };
		Vec3f p2 = { pos.x + step, pos.y, pos.z - step };
		Vec3f p3 = { pos.x - step, pos.y, pos.z + step };
		Vec3f vtx[] =
		{
			p0, p1,
			p2, p3,
		};

		// Draw lines through the center point.
		const u32 clr[] = { color, color };
		TFE_RenderShared::lineDraw3d_addLine(width, &vtx[0], clr);
		TFE_RenderShared::lineDraw3d_addLine(width, &vtx[2], clr);
	}

	void drawArrow3d(f32 width, f32 lenInPixels, Vec3f pos, Vec3f dir, Vec3f nrm, u32 color)
	{
		Vec3f offset = { pos.x - s_camera.pos.x, pos.y - s_camera.pos.y, pos.z - s_camera.pos.z };
		f32 dist = sqrtf(offset.x*offset.x + offset.y*offset.y + offset.z*offset.z);

		f32 step = lenInPixels * dist;
		f32 partStep = step * 0.25f;

		Vec3f tan = TFE_Math::cross(&dir, &nrm);
		tan = TFE_Math::normalize(&tan);

		Vec3f p0 = pos;
		Vec3f p1 = { p0.x + dir.x*step, p0.y + dir.y*step, p0.z + dir.z*step };
		Vec3f p2 = { p1.x - dir.x*partStep - tan.x*partStep, p1.y - dir.y*partStep - tan.y*partStep, p1.z - dir.z*partStep - tan.z*partStep };
		Vec3f p3 = { p1.x - dir.x*partStep + tan.x*partStep, p1.y - dir.y*partStep + tan.y*partStep, p1.z - dir.z*partStep + tan.z*partStep };

		Vec3f vtx[] =
		{
			p0, p1,
			p1, p2,
			p1, p3
		};

		// Draw lines through the center point.
		const u32 clr[] = { color, color };
		TFE_RenderShared::lineDraw3d_addLine(width, &vtx[0], clr);
		TFE_RenderShared::lineDraw3d_addLine(width, &vtx[2], clr);
		TFE_RenderShared::lineDraw3d_addLine(width, &vtx[4], clr);
	}

	void drawArrow3d_Segment(f32 width, f32 lenInPixels, Vec3f pos0, Vec3f pos1, Vec3f nrm, u32 color)
	{
		Vec3f offset = { pos0.x - s_camera.pos.x, pos0.y - s_camera.pos.y, pos0.z - s_camera.pos.z };
		f32 dist = sqrtf(offset.x*offset.x + offset.y*offset.y + offset.z*offset.z);

		f32 step = lenInPixels * dist;
		f32 partStep = step * 0.25f;

		Vec3f dir = { pos1.x - pos0.x, pos1.y - pos0.y, pos1.z - pos0.z };
		dir = TFE_Math::normalize(&dir);

		Vec3f tan = TFE_Math::cross(&dir, &nrm);
		tan = TFE_Math::normalize(&tan);

		Vec3f p0 = pos0;
		Vec3f p1 = pos1;
		Vec3f p2 = { p1.x - dir.x*partStep - tan.x*partStep, p1.y - dir.y*partStep - tan.y*partStep, p1.z - dir.z*partStep - tan.z*partStep };
		Vec3f p3 = { p1.x - dir.x*partStep + tan.x*partStep, p1.y - dir.y*partStep + tan.y*partStep, p1.z - dir.z*partStep + tan.z*partStep };

		Vec3f vtx[] =
		{
			p0, p1,
			p1, p2,
			p1, p3
		};

		// Draw lines through the center point.
		const u32 clr[] = { color, color };
		TFE_RenderShared::lineDraw3d_addLine(width, &vtx[0], clr);
		TFE_RenderShared::lineDraw3d_addLine(width, &vtx[2], clr);
		TFE_RenderShared::lineDraw3d_addLine(width, &vtx[4], clr);
	}
}
