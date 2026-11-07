#include <climits>

#include <TFE_Settings/settings.h>
#include <TFE_System/profiler.h>
#include <TFE_Jedi/Level/robject.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Math/fixedPoint.h>
#include <TFE_Jedi/Math/core_math.h>

#include "robj3dFloat_TransformAndLighting.h"
#include "robj3dFloat_PolygonSetup.h"
#include "robj3dFloat_Clipping.h"
#include "../fixedPoint20.h"
#include "../rsectorFloat.h"
#include "../rflatFloat.h"
#include "../rclassicFloatSharedState.h"
#include "../rlightingFloat.h"
#include "../../rcommon.h"

namespace TFE_Jedi
{

namespace RClassic_Float
{
	struct vec2_fixed20
	{
		fixed44_20 x, z;
	};

	////////////////////////////////////////////////
	// Polygon Drawing
	////////////////////////////////////////////////
	// Polygon
	static u8  s_polyColorIndex;
	static s32 s_polyVertexCount;
	static s32 s_polyMaxIndex;
	static f32* s_polyIntensity;
	static vec2_float* s_polyUv;
	static vec3_float* s_polyProjVtx;
	static const u8*   s_polyColorMap;
	static TextureData* s_polyTexture;
	static s32 s_polyTexFlags;		// TFE

	// Column
	static s32 s_columnX;
	static s32 s_rowY;
	static s32 s_columnHeight;
	static s32 s_dither;
	static u8* s_pcolumnOut;
		
	static fixed44_20 s_col_I0;
	static fixed44_20 s_col_dIdY;
	static vec2_fixed20 s_col_Uv0;
	static vec2_fixed20 s_col_dUVdY;

	// Polygon Edges
	static fixed44_20  s_ditherOffset;
	// Bottom Edge
	static f32  s_edgeBot_Z0;
	static f32  s_edgeBot_dZdX;
	static f32  s_edgeBot_dIdX;
	static f32  s_edgeBot_I0;
	static vec2_float  s_edgeBot_dUVdX;
	static vec2_float  s_edgeBot_Uv0;
	static f32  s_edgeBot_dYdX;
	static f32  s_edgeBot_Y0;
	// Top Edge
	static f32  s_edgeTop_dIdX;
	static vec2_float  s_edgeTop_dUVdX;
	static vec2_float  s_edgeTop_Uv0;
	static f32  s_edgeTop_dYdX;
	static f32  s_edgeTop_Z0;
	static f32  s_edgeTop_Y0;
	static f32  s_edgeTop_dZdX;
	static f32  s_edgeTop_I0;
	// Left Edge
	static f32  s_edgeLeft_X0;
	static f32  s_edgeLeft_Z0;
	static f32  s_edgeLeft_dXdY;
	static f32  s_edgeLeft_dZmdY;
	// Right Edge
	static f32  s_edgeRight_X0;
	static f32  s_edgeRight_Z0;
	static f32  s_edgeRight_dXdY;
	static f32  s_edgeRight_dZmdY;
	// Edge Pixels & Indices
	static s32 s_edgeBotY0_Pixel;
	static s32 s_edgeTopY0_Pixel;
	static s32 s_edgeLeft_X0_Pixel;
	static s32 s_edgeRight_X0_Pixel;
	static s32 s_edgeBotIndex;
	static s32 s_edgeTopIndex;
	static s32 s_edgeLeftIndex;
	static s32 s_edgeRightIndex;
	static s32 s_edgeTopLength;
	static s32 s_edgeBotLength;
	static s32 s_edgeLeftLength;
	static s32 s_edgeRightLength;

	u8 robj3d_computePolygonColor(vec3_float* normal, u8 color, f32 z)
	{
		TFE_Settings_Graphics* graphicsSettings = TFE_Settings::getGraphicsSettings();
		bool overrideLighting = graphicsSettings->overrideLighting;
		s32 sectorAmbient = (overrideLighting ? 0 : s_sectorAmbient);
		f32 sectorAmbientFraction = (overrideLighting) ? 1 : fixed16ToFloat(s_sectorAmbientFraction);
		s32 worldAmbient = (overrideLighting ? 0 : s_worldAmbient);
		s32 scaledAmbient = (overrideLighting ? 0 : s_scaledAmbient);

		if (sectorAmbient >= MAX_LIGHT_LEVEL) { return color; }
		s_polyColorMap = s_colorMap;
		s32 lightLevel = 0;
		
		f32 lighting = 0.0f;
		for (s32 i = 0; i < s_lightCount; i++)
		{
			const CameraLightFlt* light = &s_cameraLight[i];
			const f32 L = dot(normal, &light->lightVS);

			if (L > 0.0f)
			{
				const f32 brightness = light->brightness * VSHADE_MAX_INTENSITY_FLT;
				lighting += L * brightness;
			}
		}
		lightLevel += floorFloat(lighting * sectorAmbientFraction);
		if (lightLevel >= MAX_LIGHT_LEVEL) { return color; }

		if (worldAmbient < MAX_LIGHT_LEVEL || s_cameraLightSource)
		{
			const s32 depthScaled = (s32)min(z * 4.0f, 127.0f);
			const s32 cameraSource = MAX_LIGHT_LEVEL - (s_lightSourceRamp[depthScaled] + worldAmbient);
			if (cameraSource > 0)
			{
				lightLevel += cameraSource;
			}
		}
		lightLevel = max(lightLevel, sectorAmbient);

		z = max(z, 0.0f);
		const s32 falloff = s32(z / 16.0f) + s32(z / 32.0f);
		lightLevel = max(lightLevel - falloff, scaledAmbient);

		if (lightLevel >= 31) { return color; }
		if (lightLevel <= 0) { return s_polyColorMap[color]; }

		return s_polyColorMap[lightLevel*256 + color];
	}

	u8 robj3d_computePolygonLightLevel(vec3_float* normal, f32 z)
	{
		TFE_Settings_Graphics* graphicsSettings = TFE_Settings::getGraphicsSettings();
		bool overrideLighting = graphicsSettings->overrideLighting;
		s32 sectorAmbient = (overrideLighting ? 0 : s_sectorAmbient);
		f32 sectorAmbientFraction = (overrideLighting) ? 1 : fixed16ToFloat(s_sectorAmbientFraction);
		s32 worldAmbient = (overrideLighting ? 0 : s_worldAmbient);
		s32 scaledAmbient = (overrideLighting ? 0 : s_scaledAmbient);

		if (sectorAmbient >= MAX_LIGHT_LEVEL) { return MAX_LIGHT_LEVEL; }
		s32 lightLevel = 0;

		f32 lighting = 0.0f;
		for (s32 i = 0; i < s_lightCount; i++)
		{
			const CameraLightFlt* light = &s_cameraLight[i];
			const f32 L = dot(normal, &light->lightVS);

			if (L > 0.0f)
			{
				const f32 brightness = light->brightness * VSHADE_MAX_INTENSITY_FLT;
				lighting += L * brightness;
			}
		}
		lightLevel += floorFloat(lighting * sectorAmbientFraction);
		if (lightLevel >= MAX_LIGHT_LEVEL) { return MAX_LIGHT_LEVEL; }

		if (worldAmbient < MAX_LIGHT_LEVEL || s_cameraLightSource)
		{
			const s32 depthScaled = (s32)min(z * 4.0f, 127.0f);
			const s32 cameraSource = MAX_LIGHT_LEVEL - (s_lightSourceRamp[depthScaled] + worldAmbient);
			if (cameraSource > 0)
			{
				lightLevel += cameraSource;
			}
		}
		lightLevel = max(lightLevel, sectorAmbient);

		z = max(z, 0.0f);
		const s32 falloff = s32(z / 16.0f) + s32(z / 32.0f);
		lightLevel = max(lightLevel - falloff, scaledAmbient);

		return clamp(lightLevel, 0, MAX_LIGHT_LEVEL);
	}
		
	////////////////////////////////////////////////
	// Instantiate Polygon Draw Routines.
	// This abuses C-Macros to build 4 versions of
	// the drawing functions based on the defines.
	//
	// This avoids duplicating a lot of the code 4
	// times and is probably pretty similar to what
	// the original developers did at the time.
	//
	// This is similar to modern shader variants.
	////////////////////////////////////////////////
	// Flat color
	#include "robj3dFloat_PolyRenderFunc.h"

	// Shaded
	#define POLY_INTENSITY
	#include "robj3dFloat_PolyRenderFunc.h"

	// Flat Texture
	#define POLY_UV
	#undef POLY_INTENSITY
	#include "robj3dFloat_PolyRenderFunc.h"

	// Shaded Texture
	#define POLY_INTENSITY
	#include "robj3dFloat_PolyRenderFunc.h"

	////////////////////////////////////////////
	// Polygon Draw Routine for Shading = PLANE
	// and support functions.
	////////////////////////////////////////////
	s32 robj3d_findRightEdge(s32 minIndex)
	{
		s32 len = s_edgeRightLength;
		if (minIndex == s_polyMaxIndex)
		{
			s_edgeRightLength = len;
			return -1;
		}

		s32 curIndex = minIndex;
		while (1)
		{
			s32 nextIndex = curIndex + 1;
			if (nextIndex >= s_polyVertexCount) { nextIndex = 0; }
			else if (nextIndex < 0) { nextIndex = s_polyVertexCount - 1; }

			const vec3_float* cur  = &s_polyProjVtx[curIndex];
			const vec3_float* next = &s_polyProjVtx[nextIndex];
			const s32 y0 = s32(cur->y + 0.5f);
			const s32 y1 = s32(next->y + 0.5f);

			s32 dy = y1 - y0;
			if (y1 == s_maxScreenY) { dy++; }

			if (dy > 0)
			{
				const s32 x0 = s32(cur->x + 0.5f);
				const s32 x1 = s32(next->x + 0.5f);
				const f32 dX = f32(x1 - x0);
				const f32 dY = f32(dy);
				const f32 dXdY = dX / dY;

				s_edgeRight_X0_Pixel = x0;
				s_edgeRight_X0 = f32(x0);
				s_edgeRightLength = dy;

				s_edgeRight_dXdY = dXdY;
				s_edgeRight_Z0 = cur->z;

				s_edgeRight_dZmdY = (next->z - cur->z) * dY;
				s_edgeRightIndex = nextIndex;
				return 0;
			}
			else
			{
				curIndex = nextIndex;
				if (nextIndex == s_polyMaxIndex)
				{
					break;
				}
			}
		}

		s_edgeRightLength = len;
		return -1;
	}

	s32 robj3d_findLeftEdge(s32 minIndex)
	{
		s32 len = s_edgeLeftLength;
		if (minIndex == s_polyMaxIndex)
		{
			s_edgeLeftLength = len;
			return -1;
		}

		s32 curIndex = minIndex;
		while (1)
		{
			s32 prevIndex = curIndex - 1;
			if (prevIndex >= s_polyVertexCount) { prevIndex = 0; }
			else if (prevIndex < 0) { prevIndex = s_polyVertexCount - 1; }

			const vec3_float* cur  = &s_polyProjVtx[curIndex];
			const vec3_float* prev = &s_polyProjVtx[prevIndex];
			const s32 y0 = s32(cur->y  + 0.5f);
			const s32 y1 = s32(prev->y + 0.5f);

			s32 dy = y1 - y0;
			if (y1 == s_maxScreenY) { dy++; }

			if (dy > 0)
			{
				const s32 x0 = s32(cur->x  + 0.5f);
				const s32 x1 = s32(prev->x + 0.5f);
				const f32 dX = f32(x1 - x0);
				const f32 dY = f32(dy);
				const f32 dXdY = dX / dY;

				s_edgeLeft_X0_Pixel = x0;
				s_edgeLeft_X0 = f32(x0);
				s_edgeLeftLength = dy;

				s_edgeLeft_dXdY = dXdY;
				s_edgeLeft_Z0 = cur->z;

				s_edgeLeft_dZmdY = (prev->z - cur->z) * dY;
				s_edgeLeftIndex = prevIndex;
				return 0;
			}
			else
			{
				curIndex = prevIndex;
				if (prevIndex == s_polyMaxIndex)
				{
					break;
				}
			}
		}

		s_edgeLeftLength = len;
		return -1;
	}

	void robj3d_drawPlaneTexturePolygon(vec3_float* projVertices, s32 vertexCount, TextureData* texture, f32 planeY, f32 ceilOffsetX, f32 ceilOffsetZ, f32 floorOffsetX, f32 floorOffsetZ)
	{
		if (vertexCount <= 0) { return; }

		s32 yMin = INT_MAX;
		s32 yMax = INT_MIN;
		s32 minIndex;

		s_polyProjVtx = projVertices;
		s_polyVertexCount = vertexCount;
		
		vec3_float* vertex = projVertices;
		for (s32 i = 0; i < s_polyVertexCount; i++, vertex++)
		{
			if (vertex->y < yMin)
			{
				yMin = s32(vertex->y + 0.5f);
				minIndex = i;
			}
			if (vertex->y > yMax)
			{
				yMax = s32(vertex->y + 0.5f);
				s_polyMaxIndex = i;
			}
		}
		if (yMin >= yMax || yMin > s_windowMaxY_Pixels || yMax < s_windowMinY_Pixels)
		{
			return;
		}

		bool trans = (texture->flags & OPACITY_TRANS) != 0;
		s_rowY = yMin;

		if (robj3d_findLeftEdge(minIndex) != 0 || robj3d_findRightEdge(minIndex) != 0)
		{
			return;
		}

		f32 heightOffset = planeY - s_rcfltState.eyeHeight;
		// TODO: Figure out why s_heightInPixels has the wrong sign here.
		if (yMax <= s_screenYMidFlt)
		{
			flat_preparePolygon(heightOffset, ceilOffsetX, ceilOffsetZ, texture);
		}
		else
		{
			flat_preparePolygon(heightOffset, floorOffsetX, floorOffsetZ, texture);
		}

		s32 edgeFound = 0;
		for (; edgeFound == 0 && s_rowY <= s_maxScreenY; s_rowY++)
		{
			if (s_rowY >= s_windowMinY_Pixels && s_windowMaxY_Pixels != 0 && s_edgeLeft_X0_Pixel <= s_windowMaxX_Pixels && s_edgeRight_X0_Pixel >= s_windowMinX_Pixels)
			{
				flat_drawPolygonScanline(s_edgeLeft_X0_Pixel, s_edgeRight_X0_Pixel, s_rowY, trans);
			}

			s_edgeLeftLength--;
			if (s_edgeLeftLength <= 0)
			{
				if (robj3d_findLeftEdge(s_edgeLeftIndex) != 0) { return; }
			}
			else
			{
				s_edgeLeft_X0 += s_edgeLeft_dXdY;
				s_edgeLeft_Z0 += s_edgeLeft_dZmdY;
				s_edgeLeft_X0_Pixel = roundFloat(s_edgeLeft_X0);

				// Right Z0 increment in the wrong place again.
				// TODO: Figure out the consequences of this bug.
				//s_edgeRight_Z0 += s_edgeRight_dZmdY;
			}
			s_edgeRightLength--;
			if (s_edgeRightLength <= 0)
			{
				if (robj3d_findRightEdge(s_edgeRightIndex) != 0) { return; }
			}
			else
			{
				s_edgeRight_X0 += s_edgeRight_dXdY;
				s_edgeRight_X0_Pixel = roundFloat(s_edgeRight_X0);

				// This is the proper place for this.
				s_edgeRight_Z0 += s_edgeRight_dZmdY;
			}
		}
	}

	void robj3d_drawPolygon(JmPolygon* polygon, s32 polyVertexCount, SecObject* obj, JediModel* model)
	{
		switch (polygon->shading)
		{
			case PSHADE_FLAT:
			{
				u8 color = polygon->color;
				if (s_enableFlatShading)
				{
					color = robj3d_computePolygonColor(&s_polygonNormalsVS[polygon->index], color, polygon->zAvef);
				}
				robj3d_drawFlatColorPolygon(s_polygonVerticesProj, polyVertexCount, color);
			} break;
			case PSHADE_GOURAUD:
			{
				robj3d_drawShadedColorPolygon(s_polygonVerticesProj, s_polygonIntensity, polyVertexCount, polygon->color);
			} break;
			case PSHADE_TEXTURE:
			{
				u8 lightLevel = 0;
				if (s_enableFlatShading)
				{
					lightLevel = robj3d_computePolygonLightLevel(&s_polygonNormalsVS[polygon->index], polygon->zAvef);
				}
				robj3d_drawFlatTexturePolygon(s_polygonVerticesProj, s_polygonUv, polyVertexCount, polygon->texture, lightLevel, polygon->textureFlags);
			} break;
			case PSHADE_GOURAUD_TEXTURE:
			{
				robj3d_drawShadedTexturePolygon(s_polygonVerticesProj, s_polygonUv, s_polygonIntensity, polyVertexCount, polygon->texture, polygon->textureFlags);
			} break;
			case PSHADE_PLANE:
			{
				const RSector* sector = obj->sector;
				const f32 planeY = fixed16ToFloat(model->vertices[polygon->indices[0]].y + obj->posWS.y);
				// TODO: Caching.
				robj3d_drawPlaneTexturePolygon(s_polygonVerticesProj, polyVertexCount, polygon->texture, planeY,
					fixed16ToFloat(sector->ceilOffset.x), fixed16ToFloat(sector->ceilOffset.z), fixed16ToFloat(sector->floorOffset.x), fixed16ToFloat(sector->floorOffset.z));
			} break;
			default:
			{
				TFE_System::logWrite(LOG_ERROR, "Object3D Render Fixed", "Invalid shading mode: %d", polygon->shading);
				assert(0);
			}
		}
	}

}}  // TFE_Jedi