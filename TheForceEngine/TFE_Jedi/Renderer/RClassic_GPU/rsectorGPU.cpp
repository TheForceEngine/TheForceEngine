#include <cstring>

#include <TFE_System/profiler.h>
#include <TFE_System/math.h>
#include <TFE_Asset/modelAsset_jedi.h>
#include <TFE_Game/igame.h>
#include <TFE_Jedi/Level/level.h>
#include <TFE_Jedi/Level/levelData.h>
#include <TFE_Jedi/Level/rsector.h>
#include <TFE_Jedi/Level/robject.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Level/levelTextures.h>
#include <TFE_Jedi/Math/fixedPoint.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/Renderer/jediRenderer.h>
#include <TFE_Settings/settings.h>

#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_RenderBackend/vertexBuffer.h>
#include <TFE_RenderBackend/indexBuffer.h>
#include <TFE_RenderBackend/shader.h>
#include <TFE_RenderBackend/shaderBuffer.h>
#include <TFE_RenderShared/texturePacker.h>

// Temp
#include <TFE_Asset/imageAsset.h>

#include <TFE_FrontEndUI/console.h>

#include "rclassicGPU.h"
#include "rsectorGPU.h"
#include "modelGPU.h"
#include "renderDebug.h"
#include "debug.h"
#include "frustum.h"
#include "sbuffer.h"
#include "objectPortalPlanes.h"
#include "sectorDisplayList.h"
#include "spriteDisplayList.h"
#include "../rcommon.h"

// TODO: FIx
#include "../RClassic_Float/rclassicFloatSharedState.h"

#define SHOW_TRUE_COLOR_COMPARISION 0
#define ACCURATE_MAPPING_ENABLE 0	// TODO: Still work in progress - more accurate mapping without color errors...

#define PTR_OFFSET(ptr, base) size_t((u8*)ptr - (u8*)base)
using namespace TFE_RenderBackend;

namespace TFE_Jedi
{
	enum UploadFlags
	{
		UPLOAD_NONE     = 0,
		UPLOAD_SECTORS  = FLAG_BIT(0),
		UPLOAD_VERTICES = FLAG_BIT(1),
		UPLOAD_WALLS    = FLAG_BIT(2),
		UPLOAD_ALL      = UPLOAD_SECTORS | UPLOAD_VERTICES | UPLOAD_WALLS
	};
	enum Constants
	{
		SPRITE_PASS = SECTOR_PASS_COUNT
	};
	static const f32 c_wallPlaneEps = 0.1f;	// was 0.01f

	struct GPUSourceData
	{
		Vec4f* sectors;
		Vec4f* walls;
		u32 sectorSize;
		u32 wallSize;
	};
	struct Portal
	{
		Vec2f v0, v1;
		f32   y0, y1;
		RSector* next;
		Frustum  frustum;
		RWall*   wall;
	};
	struct ShaderInputsGPU
	{
		s32 cameraPosId;
		s32 cameraViewId;
		s32 cameraProjId;
		s32 cameraDirId;
		s32 lightDataId;
		s32 globalLightingId;
		s32 texSamplingParamId;
		s32 palFxLumMask;
		s32 palFxFlash;
		s32 textureSettings;
	};
	struct ShaderSkyInputs
	{
		s32 skyParallaxId;
		s32 skyParam0Id;
		s32 skyParam1Id;
	};

	static GPUSourceData s_gpuSourceData = { 0 };

	TextureGpu* s_trueColorMapping = nullptr;
	static TextureGpu*  s_colormapTex = nullptr;
	static ShaderBuffer s_sectorGpuBuffer;
	static ShaderBuffer s_wallGpuBuffer;
	static Shader s_spriteShader;
	static Shader s_wallShader[SECTOR_PASS_COUNT];
	static ShaderInputsGPU s_shaderInputs[SECTOR_PASS_COUNT + 1];
	static ShaderSkyInputs s_shaderSkyInputs[SECTOR_PASS_COUNT];
	static s32 s_cameraRightId;

#if ACCURATE_MAPPING_ENABLE   // Future work.
	static TextureGpu* s_trueColorToPal = nullptr;
#endif

	static IndexBuffer s_indexBuffer;
	static GPUCachedSector* s_cachedSectors;
	static bool s_enableDebug = false;

	static s32 s_gpuFrame;
	static s32 s_portalListCount = 0;
	static s32 s_rangeCount;

	static Portal* s_portalList = nullptr;
	static Vec2f  s_range[2];
	static Vec2f  s_rangeSrc[2];
	static Segment s_wallSegments[2048];

	static bool s_trueColor = false;
	static bool s_mipmapping = false;
	static bool s_forceTextureUpdate = false;

	struct ShaderSettingsSGPU
	{
		SkyMode skyMode = SKYMODE_CYLINDER;
		bool colormapInterp = false;
		bool ditheredBilinear = false;
		bool bloom = false;
		bool trueColor = false;
	};

	static ShaderSettingsSGPU s_shaderSettings = {};
	static RSector* s_clipSector;
	static Vec3f s_clipObjPos;

	static JBool s_flushCache = JFALSE;
	u32 s_textureSettings = 1u;

	extern Mat3  s_cameraMtx;
	extern Mat4  s_cameraProj;
	extern Vec3f s_cameraPos;
	extern Vec3f s_cameraDir;
	extern Vec3f s_cameraDirXZ;
	extern Vec3f s_cameraRight;
	extern s32   s_displayCurrentPortalId;
	extern ShaderBuffer s_displayListPlanesGPU;
		
	bool loadSpriteShader(s32 defineCount, ShaderDefine* defines)
	{
		if (!s_spriteShader.load("Shaders/gpu_render_sprite.vert", "Shaders/gpu_render_sprite.frag", defineCount, defines, SHADER_VER_STD))
		{
			return false;
		}
		s_spriteShader.enableClipPlanes(MAX_PORTAL_PLANES);

		s_shaderInputs[SPRITE_PASS].cameraPosId  = s_spriteShader.getVariableId("CameraPos");
		s_shaderInputs[SPRITE_PASS].cameraViewId = s_spriteShader.getVariableId("CameraView");
		s_shaderInputs[SPRITE_PASS].cameraProjId = s_spriteShader.getVariableId("CameraProj");
		s_shaderInputs[SPRITE_PASS].cameraDirId  = s_spriteShader.getVariableId("CameraDir");
		s_shaderInputs[SPRITE_PASS].lightDataId  = s_spriteShader.getVariableId("LightData");
		s_shaderInputs[SPRITE_PASS].globalLightingId = s_spriteShader.getVariableId("GlobalLightData");
		s_shaderInputs[SPRITE_PASS].texSamplingParamId = s_spriteShader.getVariableId("TexSamplingParam");
		s_shaderInputs[SPRITE_PASS].palFxLumMask = s_spriteShader.getVariableId("PalFxLumMask");
		s_shaderInputs[SPRITE_PASS].palFxFlash = s_spriteShader.getVariableId("PalFxFlash");
		s_shaderInputs[SPRITE_PASS].textureSettings = s_spriteShader.getVariableId("TextureSettings");
		s_cameraRightId = s_spriteShader.getVariableId("CameraRight");

		s_spriteShader.bindTextureNameToSlot("DrawListPosXZ_Texture", 0);
		s_spriteShader.bindTextureNameToSlot("DrawListPosYU_Texture", 1);
		s_spriteShader.bindTextureNameToSlot("DrawListTexId_Texture", 2);

		s_spriteShader.bindTextureNameToSlot("Colormap",       3);
		s_spriteShader.bindTextureNameToSlot("Palette",        4);
		s_spriteShader.bindTextureNameToSlot("Textures",       5);
		s_spriteShader.bindTextureNameToSlot("TextureTable",   6);
		s_spriteShader.bindTextureNameToSlot("DrawListPlanes", 7);

		return true;
	}
	
	static bool loadShaderVariant(s32 index, s32 defineCount, ShaderDefine* defines)
	{
		// Destroy the existing shader so we aren't duplicating shaders if new settings are used.
		s_wallShader[index].destroy();

		if (!s_wallShader[index].load("Shaders/gpu_render_wall.vert", "Shaders/gpu_render_wall.frag", defineCount, defines, SHADER_VER_STD))
		{
			return false;
		}
		s_wallShader[index].enableClipPlanes(MAX_PORTAL_PLANES);

		s_shaderInputs[index].cameraPosId  = s_wallShader[index].getVariableId("CameraPos");
		s_shaderInputs[index].cameraViewId = s_wallShader[index].getVariableId("CameraView");
		s_shaderInputs[index].cameraProjId = s_wallShader[index].getVariableId("CameraProj");
		s_shaderInputs[index].cameraDirId  = s_wallShader[index].getVariableId("CameraDir");
		s_shaderInputs[index].lightDataId  = s_wallShader[index].getVariableId("LightData");
		s_shaderInputs[index].globalLightingId = s_wallShader[index].getVariableId("GlobalLightData");
		s_shaderInputs[index].texSamplingParamId = s_wallShader[index].getVariableId("TexSamplingParam");
		s_shaderInputs[index].palFxLumMask = s_wallShader[index].getVariableId("PalFxLumMask");
		s_shaderInputs[index].palFxFlash = s_wallShader[index].getVariableId("PalFxFlash");
		s_shaderInputs[index].textureSettings = s_wallShader[index].getVariableId("TextureSettings");

		s_shaderSkyInputs[index].skyParallaxId = s_wallShader[index].getVariableId("SkyParallax");
		s_shaderSkyInputs[index].skyParam0Id = s_wallShader[index].getVariableId("SkyParam0");
		s_shaderSkyInputs[index].skyParam1Id = s_wallShader[index].getVariableId("SkyParam1");
		
		s_wallShader[index].bindTextureNameToSlot("Sectors",        0);
		s_wallShader[index].bindTextureNameToSlot("Walls",          1);
		s_wallShader[index].bindTextureNameToSlot("DrawListPos",    2);
		s_wallShader[index].bindTextureNameToSlot("DrawListData",   3);
		s_wallShader[index].bindTextureNameToSlot("DrawListPlanes", 4);
		s_wallShader[index].bindTextureNameToSlot("Colormap",       5);
		s_wallShader[index].bindTextureNameToSlot("Palette",        6);
		s_wallShader[index].bindTextureNameToSlot("Textures",       7);
		s_wallShader[index].bindTextureNameToSlot("TextureTable",   8);
		s_wallShader[index].bindTextureNameToSlot("TrueColorMapping", 9);

		return true;
	}

	void TFE_Sectors_GPU::destroy()
	{
		free(s_portalList);
		s_spriteShader.destroy();
		s_wallShader[0].destroy();
		s_wallShader[1].destroy();
		s_indexBuffer.destroy();
		s_sectorGpuBuffer.destroy();
		s_wallGpuBuffer.destroy();
		TFE_RenderBackend::freeTexture(s_colormapTex);
		TFE_RenderBackend::freeTexture(s_trueColorMapping);

	#if ACCURATE_MAPPING_ENABLE   // Future work.
		TFE_RenderBackend::freeTexture(s_trueColorToPal);
		s_trueColorToPal = nullptr;
	#endif
		
		s_portalList = nullptr;
		s_cachedSectors = nullptr;
		s_colormapTex = nullptr;
		s_trueColorMapping = nullptr;

		sdisplayList_destroy();
		sprdisplayList_destroy();
		objectPortalPlanes_destroy();
		model_destroy();

		s_flushCache = JFALSE;
	}

	void TFE_Sectors_GPU::reset()
	{
		m_levelInit = false;
		s_flushCache = JFALSE;
	}

	void TFE_Sectors_GPU::flushCache()
	{
		s_flushCache = JTRUE;
	}

	void TFE_Sectors_GPU::flushTextureCache()
	{
		s_forceTextureUpdate = true;
	}

	s32 getColormapWhiterampColor(s32 invLightLevel)
	{
		const u8 whitePoint = 32;
		const u8* level = &s_colorMap[(MAX_LIGHT_LEVEL - invLightLevel)<<8];
		return level[whitePoint];
	}

	Vec3f getPaletteColor(u8 palColor, const u32* pal)
	{
		const f32 scale = 1.0f / 255.0f;
		const u32 rgba = pal[palColor];
		Vec3f color = { f32((rgba) & 0xff) * scale, f32((rgba >> 8) & 0xff) * scale, f32((rgba >> 16) & 0xff) * scale };
		return color;
	}

	u32 convertToRGBA(Vec4f colorf)
	{
		u32 r = clamp(u32(colorf.x * 255.0f), 0, 255);
		u32 g = clamp(u32(colorf.y * 255.0f), 0, 255);
		u32 b = clamp(u32(colorf.z * 255.0f), 0, 255);
		u32 a = clamp(u32(colorf.w * 255.0f), 0, 255);

		return r | (g << 8u) | (b << 16u) | (a << 24u);
	}

	bool isFogRegion(s32 light, const u32* pal)
	{
		// Is this a "fog" region?
		const u8 whitePoint = 32;
		const f32 eps = 3.0f / 255.0f;
		const u8* level = &s_colorMap[(MAX_LIGHT_LEVEL - light) << 8];

		Vec3f clr0 = getPaletteColor(level[whitePoint], pal);
		f32 max0 = max(clr0.x, max(clr0.y, clr0.z));

		Vec3f clr1 = getPaletteColor(level[whitePoint+15], pal);
		f32 max1 = max(clr1.x, max(clr1.y, clr1.z));

		return fabsf(max0 - max1) < eps;
	}

	f32 dotF(Vec3f a, Vec3f b)
	{
		return a.x*b.x + a.y*b.y + a.z*b.z;
	}

	f32 rgb2Hue(Vec3f c)
	{
		const f32 minComp = min(min(c.x, c.y), c.z);
		const f32 maxComp = max(max(c.x, c.y), c.z);
		const f32 delta = maxComp - minComp;

		if (fabsf(maxComp) < FLT_EPSILON || fabsf(delta) < FLT_EPSILON)
		{
			return 0.0f;
		}
		const f32 scale = 1.0f / delta;
		const f32 normFactor = 1.0f / 6.0f;

		f32 hue;
		if (c.x == maxComp) { hue = (c.y - c.z) * scale; }
		else if (c.y == maxComp) { hue = 2.0f + (c.z - c.x) * scale; }
		else { hue = 4.0f + (c.x - c.y) * scale; }

		// Convert to 0..1 range.
		hue *= normFactor;
		hue = fmodf(hue, 1.0f);
		if (hue < 0.0f) { hue += 1.0f; }

		return hue;
	}

	f32 computeHueMatch(u8 baseIndex, u8 curIndex, const u32* pal)
	{
		const Vec3f baseColor = getPaletteColor(baseIndex, pal);
		const Vec3f curColor = getPaletteColor(curIndex, pal);

		const f32 baseHue = rgb2Hue(baseColor);
		const f32 curHue = rgb2Hue(curColor);

		return fabsf(baseHue - curHue);
	}

	bool useColorShift(s32 light, const u32* pal)
	{
		// check colored regions to see if they color shift.
		// Green 80  +32 = 112
		// Blue  120 +32 = 152
		// Red   128 +32 = 160
		enum ColorPoints
		{
			CP_Green = 112,
			CP_Blue = 152,
			CP_Red = 160
		};
		const f32 eps = 0.1f;

		const u8* baseLevel = &s_colorMap[MAX_LIGHT_LEVEL << 8];
		const u8* curLevel = &s_colorMap[(MAX_LIGHT_LEVEL - light) << 8];
		
		// Compare the hue of the base level vs the current level.
		f32 greenMatch = computeHueMatch(baseLevel[CP_Green], curLevel[CP_Green], pal);
		f32 blueMatch = computeHueMatch(baseLevel[CP_Blue], curLevel[CP_Blue], pal);
		f32 redMatch = computeHueMatch(baseLevel[CP_Red], curLevel[CP_Red], pal);

		// Then compare to each other.
		f32 blueMatchB = computeHueMatch(curLevel[CP_Green], curLevel[CP_Blue], pal);
		f32 redMatchB = computeHueMatch(curLevel[CP_Green], curLevel[CP_Red], pal);

		s32 shiftCount = 0;
		if (greenMatch > eps) { shiftCount++; }
		if (blueMatch  > eps) { shiftCount++; }
		if (redMatch   > eps) { shiftCount++; }
		return (blueMatchB < eps && redMatchB < eps && shiftCount > 1);
	}

#if ACCURATE_MAPPING_ENABLE
	void generateTrueColorMapping2();
#endif

	void generateTrueColorMapping()
	{
		struct FogRegion
		{
			u8 start, end;
			Vec3f startColor;
			Vec3f endColor;
			bool isFog;
			bool colorShift;
		};
		int fogRegionCount = 1;
		FogRegion regions[16];

		// Determine fog regions.
		const u32* pal = TFE_Jedi::renderer_getSourcePalette();
		u8 palIndex = getColormapWhiterampColor(0);
		Vec3f prevColor = getPaletteColor(palIndex, pal);
		f32 prevMax = max(prevColor.x, max(prevColor.y, prevColor.z));

		FogRegion* curRegion = &regions[0];
		curRegion->start = 31;
		curRegion->end = 0;
		curRegion->startColor = prevColor;
		curRegion->isFog = false;
		curRegion->colorShift = false;

		// Generate an image...
#if SHOW_TRUE_COLOR_COMPARISION
		u32 outImage[(256-32) * 32 * 2];
		for (s32 x = 32; x < 256; x++)
		{
			for (s32 y = 0; y < 32; y++)
			{
				u8 palIndex = s_colorMap[y * 256 + x];
				outImage[y * (256 - 32) + x - 32] = pal[palIndex];
			}
		}
		TFE_Image::writeImage("ColorMap.png", 256-32, 32, outImage);
#endif

		for (s32 i = 1; i < 32; i++)
		{
			palIndex = getColormapWhiterampColor(i);
			Vec3f curColor = getPaletteColor(palIndex, pal);
			f32 curMax = max(curColor.x, max(curColor.y, curColor.z));

			const f32 stepScale = prevMax > 0.0f ? curMax / prevMax : 1.0f;

			// Create new fog region.
			if (stepScale > 1.5f)
			{
				// End the ramp on the previous texel.
				curRegion->end = 31 - (i - 1);
				curRegion->endColor = prevColor;
				curRegion->isFog = isFogRegion(i - 1, pal);
				
				// Add a new region.
				curRegion = &regions[fogRegionCount];
				fogRegionCount++;

				curRegion->start = 31 - i;
				curRegion->end = 0;
				curRegion->startColor = curColor;
				curRegion->endColor = curColor;
				curRegion->isFog = false;
				curRegion->colorShift = useColorShift(i, pal);
				if (i == 31)
				{
					curRegion->isFog = isFogRegion(i, pal);
				}
			}
			else if (i == 31)
			{
				curRegion->end = 0;
				curRegion->endColor = curColor;
				curRegion->isFog = isFogRegion(i, pal);
			}

			prevMax = curMax;
			prevColor = curColor;
		}

		// Process regions and use them to generate a mapping.
		// Two values:
		//   RGB = Multiply color, A = ?
		//   RGB = Fog color, A = blendFactor
		Vec4f mulRamp[32], fogRamp[32];
		bool ignoreTextureTint = false;
		for (s32 i = 0; i < fogRegionCount; i++)
		{
			curRegion = &regions[i];
			if (curRegion->isFog)
			{
				Vec3f white = { 1.0f, 1.0f, 1.0f };
				Vec3f mulColor = curRegion->start == 31 ? white : curRegion->startColor;
				Vec3f fogColor = curRegion->endColor;

				if (fogColor.x > 0.1f || fogColor.y > 0.1f || fogColor.z > 0.1f)
				{
					ignoreTextureTint = true;
				}

				for (s32 j = curRegion->start; j >= curRegion->end; j--)
				{
					f32 fogFactor = f32(curRegion->start - j) / f32(curRegion->start - curRegion->end);
					mulRamp[j] = { mulColor.x, mulColor.y, mulColor.z, curRegion->colorShift ? 1.0f : 0.0f };
					fogRamp[j] = { fogColor.x, fogColor.y, fogColor.z, fogFactor };
				}
			}
			else
			{
				for (s32 j = curRegion->start; j >= curRegion->end; j--)
				{
					f32 range = f32(curRegion->start - curRegion->end);
					f32 mulFactor = range > 0.0f ? f32(curRegion->start - j) / f32(curRegion->start - curRegion->end) : 1.0f;

					const Vec3f white = { 1.0f, 1.0f, 1.0f };
					const Vec3f startColor = curRegion->start == 31 ? white : curRegion->startColor;
					const Vec3f endColor = curRegion->endColor;

					Vec3f mulColor;
					mulColor.x = startColor.x + mulFactor * (endColor.x - startColor.x);
					mulColor.y = startColor.y + mulFactor * (endColor.y - startColor.y);
					mulColor.z = startColor.z + mulFactor * (endColor.z - startColor.z);

					mulRamp[j] = { mulColor.x, mulColor.y, mulColor.z, curRegion->colorShift ? 1.0f : 0.0f };
					fogRamp[j] = { 0.0f, 0.0f, 0.0f, 0.0f };
				}
			}
		}
		u32 mappingTable[64];
		for (s32 i = 0; i < 32; i++)
		{
			mappingTable[i] = convertToRGBA(mulRamp[i]);
			mappingTable[i+32] = convertToRGBA(fogRamp[i]);
		}
	#if SHOW_TRUE_COLOR_COMPARISION
		TFE_Image::writeImage("TrueColorMapping.png", 64, 1, mappingTable);
	#endif
		TFE_RenderBackend::freeTexture(s_trueColorMapping);
		s_trueColorMapping = TFE_RenderBackend::createTexture(64, 1, mappingTable);
		s_textureSettings = (ignoreTextureTint) ? 0 : 1;

		// Generate a comparison color map.
#if SHOW_TRUE_COLOR_COMPARISION
		u32* outImage2 = &outImage[(256 - 32) * 32];
		for (s32 x = 32; x < 256; x++)
		{
			for (s32 y = 0; y < 32; y++)
			{
				Vec3f baseColor = getPaletteColor(x, pal);

				// Multiply
				if (mulRamp[y].w > 0.5f)
				{
					const f32 lum = max(baseColor.x, max(baseColor.y, baseColor.z));
					baseColor.x = mulRamp[y].x * lum;
					baseColor.y = mulRamp[y].y * lum;
					baseColor.z = mulRamp[y].z * lum;
				}
				else
				{
					baseColor.x *= mulRamp[y].x;
					baseColor.y *= mulRamp[y].y;
					baseColor.z *= mulRamp[y].z;
				}

				// Fog
				baseColor.x = (1.0f - fogRamp[y].w) * baseColor.x + fogRamp[y].w * fogRamp[y].x;
				baseColor.y = (1.0f - fogRamp[y].w) * baseColor.y + fogRamp[y].w * fogRamp[y].y;
				baseColor.z = (1.0f - fogRamp[y].w) * baseColor.z + fogRamp[y].w * fogRamp[y].z;

				Vec4f color = { baseColor.x, baseColor.y, baseColor.z, 1.0f };
				outImage2[y * (256 - 32) + x - 32] = convertToRGBA(color);
			}
		}
		TFE_Image::writeImage("ColorMap_TC.png", 256 - 32, 64, outImage);
#endif

	// TODO: Continue work on improved colormap remapping.
	#if ACCURATE_MAPPING_ENABLE
		generateTrueColorMapping2();
	#endif
	}

#if ACCURATE_MAPPING_ENABLE
	// Concept: 
	// Build a 64^3 (RGB) * 32 (Ambient Levels) table.
	Vec3f computeLinearColor(Vec3f srgb)
	{
		const f32 gamma = 2.2f;
		Vec3f linear;
		linear.x = powf(srgb.x, gamma);
		linear.y = powf(srgb.y, gamma);
		linear.z = powf(srgb.z, gamma);

		return linear;
	}

	Vec3f computeSrgbColor(u32 inColor)
	{
		const f32 scale = 1.0f / 255.0f;

		Vec3f srgb;
		srgb.x = f32((inColor) & 0xff) * scale;
		srgb.y = f32((inColor >> 8) & 0xff) * scale;
		srgb.z = f32((inColor >> 16) & 0xff) * scale;

		return srgb;
	}

	f32 getColorDistSq(Vec3f c0, Vec3f c1)
	{
		// Colour metric - Thiadmer Riemersma - https://www.compuphase.com/cmetric.htm
		f32 rmean = (c0.x + c1.x) * 0.5f;
		Vec3f delta = { c1.x - c0.x, c1.y - c0.y, c1.z - c0.z };
		Vec3f deltaSq = { delta.x*delta.x, delta.y*delta.y, delta.z*delta.z };
		Vec3f scaleMetric = { 2.0f + rmean, 4.0f, 3.0f - rmean };
		return deltaSq.x * scaleMetric.x + deltaSq.y * scaleMetric.y + deltaSq.z * scaleMetric.z;
	}

	s32 getClosestColor(const u8 rgb6[3], const Vec3f* linPal)
	{
		const f32 scale = 1.0f / 63.0f;
		Vec3f srgb = { f32(rgb6[0]) * scale, f32(rgb6[1]) * scale, f32(rgb6[2]) * scale };
		Vec3f lin = computeLinearColor(srgb);
		bool nonZero = lin.x > 0.0f || lin.y > 0.0f || lin.z > 0.0f;

		// First closest
		f32 closestDist = FLT_MAX;
		s32 closestIndex = -1;
		for (s32 i = 32; i < 256; i++)
		//for (s32 i = 0; i < 256; i++)
		{
			// Make sure that non-zero colors map to non-zero palette entries.
			if (nonZero && (lin.x*linPal[i].x + lin.y*linPal[i].y + lin.z*linPal[i].z == 0.0f)) { continue; }

			// Make sure that the palette entries have all components required for mapping.
			if ((lin.x > FLT_EPSILON && linPal[i].x <= FLT_EPSILON) || (lin.y > FLT_EPSILON && linPal[i].y <= FLT_EPSILON) ||
				(lin.z > FLT_EPSILON && linPal[i].z <= FLT_EPSILON))
			{
				continue;
			}
			
			// Find the closest color.
			const f32 dist = getColorDistSq(lin, linPal[i]);
			if (dist < closestDist)
			{
				closestDist = dist;
				closestIndex = i;
			}
		}
		return closestIndex;
	}

	void generateTrueColorMapping2()
	{
		// First generate colors from the palette.
		Vec3f srgbPal[256] = { 0 };
		Vec3f linPal[256] = { 0 };
		const u32* pal = TFE_Jedi::renderer_getSourcePalette();
		for (s32 i = 32; i < 256; i++)
		{
			srgbPal[i] = computeSrgbColor(pal[i]);
			linPal[i] = computeLinearColor(srgbPal[i]);
		}

		// Now build the table itself...
		const u32 count = 64 * 64 * 64;
		static Vec4f table[count];
		static u32 colorTable[count];
		for (u32 i = 0; i < count; i++)
		{
			const u8 rgb[4] =
			{
				i & 63,
				(i >> 6) & 63,
				(i >> 12) & 63,
				0
			};

			const f32 scale = 1.0f / 63.0f;
			s32 index = getClosestColor(rgb, linPal);
			Vec3f p = srgbPal[index];
			Vec3f c = { f32(rgb[0]) * scale, f32(rgb[1]) * scale, f32(rgb[2]) * scale };
			Vec3f m = { 1.0f, 1.0f, 1.0f };
			if (p.x > 0.0f) { m.x = c.x / p.x; }
			if (p.y > 0.0f) { m.y = c.y / p.y; }
			if (p.z > 0.0f) { m.z = c.z / p.z; }

			table[i] = { m.x, m.y, m.z, f32(index) };
			colorTable[i] = pal[index];
		}

		if (s_trueColorToPal)
		{
			TFE_RenderBackend::freeTexture(s_trueColorToPal);
			s_trueColorToPal = nullptr;
		}
		s_trueColorToPal = TFE_RenderBackend::createTextureArray(64, 64, 64, 4);
		s_trueColorToPal->update(colorTable, count * sizeof(u32));

		// Setup custom filtering.
		s_trueColorToPal->bind(0);
		s_trueColorToPal->setFilter(MAG_FILTER_LINEAR, MIN_FILTER_LINEAR, true);
		s_trueColorToPal->clearSlots(1);
	}
#endif

	void TFE_Sectors_GPU::updateColorMap()
	{
		// Load the colormap based on the data.
		// Build the color map.
		if (s_colorMap && s_lightSourceRamp)
		{
			u32 colormapData[256 * 32];
			if (s_shaderSettings.colormapInterp || s_shaderSettings.trueColor)
			{
				f32 filter0[128];
				f32 filter1[128];
				for (s32 i = 0; i < 128; i++)
				{
					filter0[i] = f32(s_lightSourceRamp[i]);
				}
				// 2 passes...
				for (s32 i = 0; i < 128; i++)
				{
					filter1[i] = (filter0[max(0, i - 1)] + filter0[min(127, i + 1)]) * 0.5f;
				}
				for (s32 i = 0; i < 128; i++)
				{
					filter0[i] = (filter1[max(0, i - 1)] + filter1[min(127, i + 1)]) * 0.5f;
				}
				for (s32 i = 0; i < 128; i++)
				{
					filter0[i] = max(0.0f, min(31.0f, filter0[i]));
				}

				for (s32 i = 0; i < 256 * 32; i++)
				{
					u8* data = (u8*)&colormapData[i];
					data[0] = s_colorMap[i];
					if (i < 128)
					{
						data[1] = u8(filter0[i] * 8.23f);
					}
					else
					{
						data[1] = 0;
					}
					data[2] = data[3] = 0;
				}
			}
			else
			{
				for (s32 i = 0; i < 256 * 32; i++)
				{
					u8* data = (u8*)&colormapData[i];
					data[0] = s_colorMap[i];
					if (i < 128)
					{
						data[1] = s_lightSourceRamp[i];
					}
					else
					{
						data[1] = 0;
					}
					data[2] = data[3] = 0;
				}
			}

			TFE_RenderBackend::freeTexture(s_colormapTex);
			s_colormapTex = TFE_RenderBackend::createTexture(256, 32, colormapData);
		}

		// Build a color ramp for true-color...
		if (s_shaderSettings.trueColor)
		{
			generateTrueColorMapping();
		}
	}

	bool TFE_Sectors_GPU::updateShaderSettings(bool initialize)
	{
		TFE_Settings_Graphics* graphics = TFE_Settings::getGraphicsSettings();
		// First check to see if an update is needed. Generally this happens when initializing or if settings are changed at runtime.
		bool needsUpdate = initialize ||
			s_shaderSettings.skyMode != SkyMode(graphics->skyMode) ||
			s_shaderSettings.ditheredBilinear != graphics->ditheredBilinear ||
			s_shaderSettings.bloom != graphics->bloomEnabled ||
			s_shaderSettings.colormapInterp != (graphics->colorMode == COLORMODE_8BIT_INTERP) ||
			s_shaderSettings.trueColor != (graphics->colorMode == COLORMODE_TRUE_COLOR);
		if (!needsUpdate) { return true; }

		// Then update the settings.
		s_shaderSettings.skyMode = SkyMode(graphics->skyMode);
		s_shaderSettings.ditheredBilinear = graphics->ditheredBilinear;
		s_shaderSettings.bloom = graphics->bloomEnabled;
		s_shaderSettings.colormapInterp = (graphics->colorMode == COLORMODE_8BIT_INTERP);
		s_shaderSettings.trueColor = (graphics->colorMode == COLORMODE_TRUE_COLOR);

		// Update the color map based on interpolation or true color settings.
		updateColorMap();

		// Update the shaders.
		bool result = updateShaders();
		assert(result);
		return result;
	}
		
	void TFE_Sectors_GPU::prepare()
	{
		if (!m_gpuInit)
		{
			TFE_COUNTER(s_wallSegGenerated, "Wall Segments");
			
			m_gpuInit = true;
			s_gpuFrame = 1;
			if (!s_portalList)
			{
				s_portalList = (Portal*)malloc(sizeof(Portal) * MAX_DISP_ITEMS);
			}

			// Update the shaders
			updateShaderSettings(true);

			// Handles up to MAX_DISP_ITEMS sector quads in the view.
			u32* indices = (u32*)level_alloc(sizeof(u32) * 6u * MAX_DISP_ITEMS);
			u32* index = indices;
			for (u32 q = 0; q < MAX_DISP_ITEMS; q++, index += 6u)
			{
				const u32 i = q * 4u;
				index[0] = i + 0;
				index[1] = i + 1;
				index[2] = i + 2;

				index[3] = i + 1;
				index[4] = i + 3;
				index[5] = i + 2;
			}
			s_indexBuffer.create(6u * MAX_DISP_ITEMS, sizeof(u32), false, (void*)indices);
			level_free(indices);

			// Initialize the display list with the GPU buffers.
			s32 posIndex[] = { 2, 2 };
			s32 dataIndex[] = { 3, 3 };
			sdisplayList_init(posIndex, dataIndex, 4);

			// Sprite Shader and buffers...
			sprdisplayList_init(0);
			objectPortalPlanes_init();
			model_init();
		}
		if (!m_levelInit)
		{
			m_levelInit = true;

			// Let's just cache the current data.
			s_cachedSectors = (GPUCachedSector*)level_alloc(sizeof(GPUCachedSector) * s_levelState.sectorCount);
			memset(s_cachedSectors, 0, sizeof(GPUCachedSector) * s_levelState.sectorCount);

			s_gpuSourceData.sectorSize = sizeof(Vec4f) * s_levelState.sectorCount * 2;
			s_gpuSourceData.sectors = (Vec4f*)level_alloc(s_gpuSourceData.sectorSize);
			memset(s_gpuSourceData.sectors, 0, s_gpuSourceData.sectorSize);

			s32 wallCount = 0;
			for (u32 s = 0; s < s_levelState.sectorCount; s++)
			{
				RSector* curSector = &s_levelState.sectors[s];
				GPUCachedSector* cachedSector = &s_cachedSectors[s];
				cachedSector->floorHeight = fixed16ToFloat(curSector->floorHeight);
				cachedSector->ceilingHeight = fixed16ToFloat(curSector->ceilingHeight);
				cachedSector->wallStart = wallCount;

				s_gpuSourceData.sectors[s * 2].x = cachedSector->floorHeight;
				s_gpuSourceData.sectors[s * 2].y = cachedSector->ceilingHeight;
				s_gpuSourceData.sectors[s * 2].z = clamp(fixed16ToFloat(curSector->ambient), 0.0f, 31.0f);
				s_gpuSourceData.sectors[s * 2].w = f32(cachedSector->wallStart);
				assert(s32(s_gpuSourceData.sectors[s * 2].w) - cachedSector->wallStart == 0);

				s_gpuSourceData.sectors[s * 2 + 1].x = fixed16ToFloat(curSector->floorOffset.x);
				s_gpuSourceData.sectors[s * 2 + 1].y = fixed16ToFloat(curSector->floorOffset.z);
				s_gpuSourceData.sectors[s * 2 + 1].z = fixed16ToFloat(curSector->ceilOffset.x);
				s_gpuSourceData.sectors[s * 2 + 1].w = fixed16ToFloat(curSector->ceilOffset.z);

				wallCount += curSector->wallCount;
			}

			s_gpuSourceData.wallSize = sizeof(Vec4f) * wallCount * 3;
			s_gpuSourceData.walls = (Vec4f*)level_alloc(s_gpuSourceData.wallSize);
			memset(s_gpuSourceData.walls, 0, s_gpuSourceData.wallSize);

			for (u32 s = 0; s < s_levelState.sectorCount; s++)
			{
				RSector* curSector = &s_levelState.sectors[s];
				GPUCachedSector* cachedSector = &s_cachedSectors[s];

				Vec4f* wallData = &s_gpuSourceData.walls[cachedSector->wallStart * 3];
				const RWall* srcWall = curSector->walls;
				for (s32 w = 0; w < curSector->wallCount; w++, wallData += 3, srcWall++)
				{
					wallData[0].x = fixed16ToFloat(srcWall->w0->x);
					wallData[0].y = fixed16ToFloat(srcWall->w0->z);

					Vec2f offset = { fixed16ToFloat(srcWall->w1->x) - wallData->x, fixed16ToFloat(srcWall->w1->z) - wallData->y };
					wallData[0].z = fixed16ToFloat(srcWall->length) / sqrtf(offset.x*offset.x + offset.z*offset.z);
					//wallData[0].w = 0.0f;
					wallData[0].w = sqrtf(offset.x*offset.x + offset.z*offset.z);

					// Texture offsets.
					wallData[1].x = fixed16ToFloat(srcWall->midOffset.x);
					wallData[1].y = fixed16ToFloat(srcWall->midOffset.z);
					wallData[1].z = fixed16ToFloat(srcWall->signOffset.x);
					wallData[1].w = fixed16ToFloat(srcWall->signOffset.z);

					wallData[2].x = fixed16ToFloat(srcWall->botOffset.x);
					wallData[2].y = fixed16ToFloat(srcWall->botOffset.z);
					wallData[2].z = fixed16ToFloat(srcWall->topOffset.x);
					wallData[2].w = fixed16ToFloat(srcWall->topOffset.z);

					// Now handle the sign offset.
					if (srcWall->signTex)
					{
						if (srcWall->drawFlags & WDF_BOT)
						{
							wallData[1].z = wallData[2].x - wallData[1].z;
						}
						else if (srcWall->drawFlags & WDF_TOP)
						{
							wallData[1].z = wallData[2].z - wallData[1].z;
						}
						else
						{
							wallData[1].z = wallData[1].x - wallData[1].z;
						}
					}
				}
			}

			if (m_prevSectorCount < (s32)s_levelState.sectorCount || m_prevWallCount < wallCount || !m_gpuBuffersAllocated)
			{
				// Recreate the GPU sector buffers so they are large enough to hold the new data.
				if (m_gpuBuffersAllocated)
				{
					s_sectorGpuBuffer.destroy();
					s_wallGpuBuffer.destroy();
				}

				const s32 maxBufferSize = ShaderBuffer::getMaxSize();
				if ((s32)s_levelState.sectorCount * 2 > maxBufferSize)
				{
					TFE_System::logWrite(LOG_ERROR, "GPU Renderer", "Too many sectors, only %d is supported by your hardware.", maxBufferSize / 2);
				}
				else if (wallCount * 3 > maxBufferSize)
				{
					TFE_System::logWrite(LOG_ERROR, "GPU Renderer", "Too many walls, only %d is supported by your hardware.", maxBufferSize / 3);
				}

				const ShaderBufferDef bufferDefSectors = { 4, sizeof(f32), BUF_CHANNEL_FLOAT };
				s_sectorGpuBuffer.create(s_levelState.sectorCount * 2, bufferDefSectors, true, s_gpuSourceData.sectors);
				s_wallGpuBuffer.create(wallCount * 3, bufferDefSectors, true, s_gpuSourceData.walls);

				m_gpuBuffersAllocated = true;
				m_prevSectorCount = s_levelState.sectorCount;
				m_prevWallCount = wallCount;
			}
			else
			{
				// Update the GPU sector buffers since they are already large enough.
				s_sectorGpuBuffer.update(s_gpuSourceData.sectors, s_gpuSourceData.sectorSize);
				s_wallGpuBuffer.update(s_gpuSourceData.walls, s_gpuSourceData.wallSize);
			}
			m_prevSectorCount = s_levelState.sectorCount;
			m_prevWallCount = wallCount;

			// Load textures into GPU memory.
			s_trueColor  = (TFE_Settings::getGraphicsSettings()->colorMode == COLORMODE_TRUE_COLOR);
			s_mipmapping = s_trueColor && TFE_Settings::getGraphicsSettings()->useMipmapping;
			if (texturepacker_getGlobal())
			{
				texturepacker_discardUnreservedPages(texturepacker_getGlobal());

				texturepacker_pack(level_getLevelTextures, POOL_LEVEL);
				texturepacker_pack(level_getObjectTextures, POOL_LEVEL);
				texturepacker_commit();
			}

			model_loadGpuModels();
			// Update the color map based on interpolation or true color settings.
			updateColorMap();
		}
 		else
		{
			if (s_flushCache)
			{
				for (u32 i = 0; i < s_levelState.sectorCount; i++)
				{
					RSector* sector = &s_levelState.sectors[i];
					sector->dirtyFlags = SDF_ALL;
				}
			}
			bool useMips = s_trueColor && TFE_Settings::getGraphicsSettings()->useMipmapping;
			if (s_trueColor != (TFE_Settings::getGraphicsSettings()->colorMode == COLORMODE_TRUE_COLOR) || s_mipmapping != useMips || s_forceTextureUpdate)
			{
				s_trueColor = (TFE_Settings::getGraphicsSettings()->colorMode == COLORMODE_TRUE_COLOR);
				s_mipmapping = s_trueColor && TFE_Settings::getGraphicsSettings()->useMipmapping;
				s_forceTextureUpdate = false;
				// Load textures into GPU memory.
				if (texturepacker_getGlobal())
				{
					texturepacker_discardUnreservedPages(texturepacker_getGlobal());

					texturepacker_pack(level_getLevelTextures, POOL_LEVEL);
					texturepacker_pack(level_getObjectTextures, POOL_LEVEL);
					texturepacker_commit();
				}
				// Reload models.
				model_loadGpuModels();
			}
			s_gpuFrame++;
		}

		s_flushCache = JFALSE;
		renderDebug_enable(s_enableDebug);
	}
	
	void updateCachedWalls(RSector* srcSector, u32 flags, u32& uploadFlags)
	{
		GPUCachedSector* cached = &s_cachedSectors[srcSector->index];
		if (flags & (SDF_HEIGHTS | SDF_AMBIENT))
		{
			uploadFlags |= UPLOAD_WALLS;
		}
		if (flags & (SDF_VERTICES | SDF_WALL_CHANGE | SDF_WALL_OFFSETS | SDF_WALL_SHAPE))
		{
			uploadFlags |= UPLOAD_WALLS;
			Vec4f* wallData = &s_gpuSourceData.walls[cached->wallStart*3];
			const RWall* srcWall = srcSector->walls;
			for (s32 w = 0; w < srcSector->wallCount; w++, wallData+=3, srcWall++)
			{
				wallData[0].x = fixed16ToFloat(srcWall->w0->x);
				wallData[0].y = fixed16ToFloat(srcWall->w0->z);

				Vec2f offset = { fixed16ToFloat(srcWall->w1->x) - wallData->x, fixed16ToFloat(srcWall->w1->z) - wallData->y };
				wallData->z = fixed16ToFloat(srcWall->length) / sqrtf(offset.x*offset.x + offset.z*offset.z);

				// Texture offsets.
				wallData[1].x = fixed16ToFloat(srcWall->midOffset.x);
				wallData[1].y = fixed16ToFloat(srcWall->midOffset.z);
				wallData[1].z = fixed16ToFloat(srcWall->signOffset.x);
				wallData[1].w = fixed16ToFloat(srcWall->signOffset.z);

				wallData[2].x = fixed16ToFloat(srcWall->botOffset.x);
				wallData[2].y = fixed16ToFloat(srcWall->botOffset.z);
				wallData[2].z = fixed16ToFloat(srcWall->topOffset.x);
				wallData[2].w = fixed16ToFloat(srcWall->topOffset.z);

				// Now handle the sign offset.
				if (srcWall->signTex)
				{
					if (srcWall->drawFlags & WDF_BOT)
					{
						wallData[1].z = wallData[2].x - wallData[1].z;
					}
					else if (srcWall->drawFlags & WDF_TOP)
					{
						wallData[1].z = wallData[2].z - wallData[1].z;
					}
					else
					{
						wallData[1].z = wallData[1].x - wallData[1].z;
					}
				}
			}
		}
	}

	void updateCachedSector(RSector* srcSector, u32& uploadFlags)
	{
		u32 flags = srcSector->dirtyFlags;
		if (!flags) { return; }  // Nothing to do.

		GPUCachedSector* cached = &s_cachedSectors[srcSector->index];
		if (flags & (SDF_HEIGHTS | SDF_FLAT_OFFSETS | SDF_AMBIENT))
		{
			cached->floorHeight   = fixed16ToFloat(srcSector->floorHeight);
			cached->ceilingHeight = fixed16ToFloat(srcSector->ceilingHeight);
			s_gpuSourceData.sectors[srcSector->index*2].x = cached->floorHeight;
			s_gpuSourceData.sectors[srcSector->index*2].y = cached->ceilingHeight;
			s_gpuSourceData.sectors[srcSector->index*2].z = clamp(fixed16ToFloat(srcSector->ambient), 0.0f, 31.0f);
			// w = wallStart doesn't change.

			s_gpuSourceData.sectors[srcSector->index*2+1].x = fixed16ToFloat(srcSector->floorOffset.x);
			s_gpuSourceData.sectors[srcSector->index*2+1].y = fixed16ToFloat(srcSector->floorOffset.z);
			s_gpuSourceData.sectors[srcSector->index*2+1].z = fixed16ToFloat(srcSector->ceilOffset.x);
			s_gpuSourceData.sectors[srcSector->index*2+1].w = fixed16ToFloat(srcSector->ceilOffset.z);

			uploadFlags |= UPLOAD_SECTORS;
		}
		updateCachedWalls(srcSector, flags, uploadFlags);
		srcSector->dirtyFlags = SDF_NONE;
	}

	s32 traversal_addPortals(RSector* curSector)
	{
		// Add portals to the list to process for the sector.
		SegmentClipped* segment = sbuffer_get();
		s32 count = 0;
		while (segment)
		{
			if (!segment->seg->portal)
			{
				segment = segment->next;
				continue;
			}
			
			SegmentClipped* portal = segment;
			RWall* wall = &curSector->walls[portal->seg->id];
			RSector* next = wall->nextSector;
			assert(next);

			Vec3f p0 = { portal->v0.x, portal->seg->portalY0, portal->v0.z };
			Vec3f p1 = { portal->v1.x, portal->seg->portalY1, portal->v1.z };

			// Clip the portal by the current frustum, and return if it is culled.
			// Note that the near plane is ignored since portals can overlap and intersect in vanilla.
			Polygon clippedPortal;
			if (frustum_clipQuadToFrustum(p0, p1, &clippedPortal, true/*ignoreNearPlane*/))
			{
				Portal* portalOut = &s_portalList[s_portalListCount];
				s_portalListCount++;

				frustum_buildFromPolygon(&clippedPortal, &portalOut->frustum);
				portalOut->v0 = portal->v0;
				portalOut->v1 = portal->v1;
				portalOut->y0 = p0.y;
				portalOut->y1 = p1.y;
				portalOut->next = next;
				portalOut->wall = &curSector->walls[portal->seg->id];
				assert(portalOut->next);

				count++;
			}
			segment = segment->next;
		}
		return count;
	}

	void buildSegmentBuffer(bool initSector, RSector* curSector, u32 segCount, Segment* wallSegments, bool forceTreatAsSolid)
	{
		// Next insert solid segments into the segment buffer one at a time.
		sbuffer_clear();
		for (u32 i = 0; i < segCount; i++)
		{
			sbuffer_insertSegment(&wallSegments[i]);
		}
		sbuffer_mergeSegments();

		// Build the display list.
		SegmentClipped* segment = sbuffer_get();
		while (segment && s_wallSegGenerated < s_maxWallSeg)
		{
			// DEBUG
			if (s_enableDebug)
			{
				debug_addQuad(segment->v0, segment->v1, segment->seg->y0, segment->seg->y1,
					segment->seg->portalY0, segment->seg->portalY1, segment->seg->portal);
			}

			sdisplayList_addSegment(curSector, &s_cachedSectors[curSector->index], segment, forceTreatAsSolid);
			s_wallSegGenerated++;
			segment = segment->next;
		}
	}

	bool createNewSegment(Segment* seg, s32 id, bool isPortal, Vec2f v0, Vec2f v1, Vec2f heights, Vec2f portalHeights, Vec3f normal)
	{
		seg->id = id;
		seg->portal = isPortal;
		seg->v0 = v0;
		seg->v1 = v1;
		seg->x0 = sbuffer_projectToUnitSquare(seg->v0);
		seg->x1 = sbuffer_projectToUnitSquare(seg->v1);

		// This means both vertices map to the same point on the unit square, in other words, the edge isn't actually visible.
		if (fabsf(seg->x0 - seg->x1) < FLT_EPSILON)
		{
			return false;
		}

		// Project the edge.
		sbuffer_handleEdgeWrapping(seg->x0, seg->x1);
		// Check again for zero-length walls in case the fix-ups above caused it (for example, x0 = 0.0, x1 = 4.0).
		if (seg->x0 >= seg->x1 || seg->x1 - seg->x0 < FLT_EPSILON)
		{
			return false;
		}
		assert(seg->x1 - seg->x0 > 0.0f && seg->x1 - seg->x0 <= 2.0f);

		seg->normal = normal;
		seg->portal = isPortal;
		seg->y0 = heights.x;
		seg->y1 = heights.z;
		seg->portalY0 = isPortal ? portalHeights.x : heights.x;
		seg->portalY1 = isPortal ? portalHeights.z : heights.z;
		return true;
	}
		
	void splitSegment(bool initSector, Segment* segList, u32& segCount, Segment* seg, Vec2f* range, Vec2f* points, s32 rangeCount)
	{
		const f32 sx1 = seg->x1;
		const Vec2f sv1 = seg->v1;

		// Split the segment at the modulus border.
		seg->v1 = sbuffer_clip(seg->v0, seg->v1, { 1.0f + s_cameraPos.x, -1.0f + s_cameraPos.z });
		seg->x1 = 4.0f;
		Vec2f newV1 = seg->v1;

		if (!initSector && !sbuffer_splitByRange(seg, range, points, rangeCount))
		{
			segCount--;
		}
		else
		{
			assert(seg->x0 >= 0.0f && seg->x1 <= 4.0f);
		}

		Segment* seg2;
		seg2 = &segList[segCount];
		segCount++;

		*seg2 = *seg;
		seg2->x0 = 0.0f;
		seg2->x1 = sx1 - 4.0f;
		seg2->v0 = newV1;
		seg2->v1 = sv1;

		if (!initSector && !sbuffer_splitByRange(seg2, range, points, rangeCount))
		{
			segCount--;
		}
		else
		{
			assert(seg2->x0 >= 0.0f && seg2->x1 <= 4.0f);
		}
	}
		
	bool isWallInFrontOfPlane(Vec2f w0, Vec2f w1, Vec2f p0, Vec2f p1)
	{
		const f32 side0 = (w0.x - p0.x)*(p1.z - p0.z) - (w0.z - p0.z)*(p1.x - p0.x);
		const f32 side1 = (w1.x - p0.x)*(p1.z - p0.z) - (w1.z - p0.z)*(p1.x - p0.x);
		return side0 <= c_wallPlaneEps || side1 <= c_wallPlaneEps;
	}
		
	void addPortalAsSky(RSector* curSector, RWall* wall)
	{
		u32 segCount = 0;
		GPUCachedSector* cached = &s_cachedSectors[curSector->index];
		cached->builtFrame = s_gpuFrame;

		// Calculate the vertices.
		const f32 x0 = fixed16ToFloat(wall->w0->x);
		const f32 x1 = fixed16ToFloat(wall->w1->x);
		const f32 z0 = fixed16ToFloat(wall->w0->z);
		const f32 z1 = fixed16ToFloat(wall->w1->z);
		f32 y0 = cached->ceilingHeight;
		f32 y1 = cached->floorHeight;
		f32 portalY0 = y0, portalY1 = y1;

		// Add a new segment.
		Segment* seg = &s_wallSegments[segCount];
		const Vec3f wallNormal = { -(z1 - z0), 0.0f, x1 - x0 };
		Vec2f v0 = { x0, z0 }, v1 = { x1, z1 }, heights = { y0, y1 }, portalHeights = { portalY0, portalY1 };
		if (!createNewSegment(seg, wall->id, false, v0, v1, heights, portalHeights, wallNormal))
		{
			return;
		}
		segCount++;

		// Split segments that cross the modulo boundary.
		if (seg->x1 > 4.0f)
		{
			splitSegment(false, s_wallSegments, segCount, seg, s_range, s_rangeSrc, s_rangeCount);
		}
		else if (!sbuffer_splitByRange(seg, s_range, s_rangeSrc, s_rangeCount))
		{
			// Out of the range, so cancel the segment.
			segCount--;
		}
		else
		{
			assert(seg->x0 >= 0.0f && seg->x1 <= 4.0f);
		}

		buildSegmentBuffer(false, curSector, segCount, s_wallSegments, true/*forceTreatAsSolid*/);
	}
		
	// Build world-space wall segments.
	bool buildSectorWallSegments(RSector* curSector, RSector* prevSector, RWall* portalWall, u32& uploadFlags, bool initSector, Vec2f p0, Vec2f p1, u32& segCount)
	{
		segCount = 0;
		GPUCachedSector* cached = &s_cachedSectors[curSector->index];
		cached->builtFrame = s_gpuFrame;

		// Compute the "minimum Z" of the portal in 2D for culling in order to emulate the software renderer.
		// This is the "loose" portal near plane culling that Dark Forces uses - without emulating it the visuals
		// will break in various areas in the vanilla levels (and mods).
		const f32 pz0 = (p0.x - s_cameraPos.x) * s_cameraDirXZ.x + (p0.z - s_cameraPos.z) * s_cameraDirXZ.z;
		const f32 pz1 = (p1.x - s_cameraPos.x) * s_cameraDirXZ.x + (p1.z - s_cameraPos.z) * s_cameraDirXZ.z;
		const f32 portalMinZ = min(pz0, pz1);

		// Portal range, all segments must be clipped to this.
		// The actual clip vertices are p0 and p1.
		s_rangeSrc[0] = p0;
		s_rangeSrc[1] = p1;
		s_rangeCount = 0;
		if (!initSector)
		{
			s_range[0].x = sbuffer_projectToUnitSquare(p0);
			s_range[0].z = sbuffer_projectToUnitSquare(p1);
			sbuffer_handleEdgeWrapping(s_range[0].x, s_range[0].z);
			s_rangeCount = 1;

			if (fabsf(s_range[0].x - s_range[0].z) < FLT_EPSILON)
			{
				sbuffer_clear();
				return false;
			}

			if (s_range[0].z > 4.0f)
			{
				s_range[1].x = 0.0f;
				s_range[1].z = s_range[0].z - 4.0f;
				s_range[0].z = 4.0f;
				s_rangeCount = 2;
			}
		}
			
		// Build segments, skipping any backfacing walls or any that are outside of the camera frustum.
		// Identify walls as solid or portals.
		for (s32 w = 0; w < curSector->wallCount; w++)
		{
			RWall* wall = &curSector->walls[w];
			RSector* next = wall->nextSector;

			// Wall already processed.
			if (wall->drawFrame == s_gpuFrame)
			{
				continue;
			}
			
			// Calculate the vertices.
			const f32 x0 = fixed16ToFloat(wall->w0->x);
			const f32 x1 = fixed16ToFloat(wall->w1->x);
			const f32 z0 = fixed16ToFloat(wall->w0->z);
			const f32 z1 = fixed16ToFloat(wall->w1->z);
			f32 y0 = cached->ceilingHeight;
			f32 y1 = cached->floorHeight;
			f32 portalY0 = y0, portalY1 = y1;

			// Check if the wall is backfacing.
			const Vec3f wallNormal = { -(z1 - z0), 0.0f, x1 - x0 };
			const Vec3f cameraVec = { x0 - s_cameraPos.x, 0.0f, z0 - s_cameraPos.z };
			if (wallNormal.x*cameraVec.x + wallNormal.z*cameraVec.z < 0.0f)
			{
				continue;
			}

			// Emulate software culling based on portal min Z.
			// Note that this can be problematic when looking up and down with proper perspective, which is why
			// it is only enabled if portal min Z > 1
			if (portalMinZ > 1.0f && !initSector)
			{
				const f32 vz0 = (x0 - s_cameraPos.x) * s_cameraDirXZ.x + (z0 - s_cameraPos.z) * s_cameraDirXZ.z;
				const f32 vz1 = (x1 - s_cameraPos.x) * s_cameraDirXZ.x + (z1 - s_cameraPos.z) * s_cameraDirXZ.z;
				if (vz0 < portalMinZ && vz1 < portalMinZ) { continue; }
			}
			// If that fails (invalid portal min Z), fall back to the portal plane test.
			else if (!initSector && !isWallInFrontOfPlane({ x0, z0 }, { x1, z1 }, p0, p1))
			{
				continue;
			}

			// Is the wall a portal or is it effectively solid?
			bool isPortal = false;
			if (next)
			{
				bool nextNoWall = (next->flags1 & SEC_FLAGS1_NOWALL_DRAW) != 0;

				// Update any potential adjoins even if they are not traversed to make sure the
				// heights and walls settings are handled correctly.
				updateCachedSector(next, uploadFlags);

				fixed16_16 openTop, openBot;
				// Sky handling
				if ((curSector->flags1 & SEC_FLAGS1_EXTERIOR) && (next->flags1 & SEC_FLAGS1_EXT_ADJ))
				{
					openTop = curSector->ceilingHeight - intToFixed16(100);
					y0 = fixed16ToFloat(openTop);
				}
				// If the next sector has the "NoWall" flag AND this is an exterior adjoin - make the portal opening as large as the
				// the larger sector.
				else if (nextNoWall && (next->flags1 & SEC_FLAGS1_EXT_ADJ))
				{
					openTop = min(next->ceilingHeight, curSector->ceilingHeight);
					y0 = fixed16ToFloat(openTop);
				}
				// If the current sector is adjoined to an exterior, then use the current sector ceiling height for the adjoin top.
				else if (!(curSector->flags1 & SEC_FLAGS1_EXTERIOR) && (next->flags1 & SEC_FLAGS1_EXTERIOR))
				{
					openTop = min(curSector->floorHeight, curSector->ceilingHeight);
					y0 = fixed16ToFloat(openTop);
				}
				else
				{
					openTop = min(curSector->floorHeight, max(curSector->ceilingHeight, next->ceilingHeight));
				}

				if ((curSector->flags1 & SEC_FLAGS1_PIT) && (next->flags1 & SEC_FLAGS1_EXT_FLOOR_ADJ))
				{
					openBot = curSector->floorHeight + intToFixed16(100);
					y1 = fixed16ToFloat(openBot);
				}
				// If the next sector has the "NoWall" flag AND this is an exterior adjoin - make the portal opening as large as the
				// the larger sector.
				else if (nextNoWall && (next->flags1 & SEC_FLAGS1_EXT_FLOOR_ADJ))
				{
					openBot = max(next->floorHeight, curSector->floorHeight);
					y1 = fixed16ToFloat(openTop);
				}
				else
				{
					openBot = max(curSector->ceilingHeight, min(curSector->floorHeight, next->floorHeight));
				}
				
				fixed16_16 openSize = openBot - openTop;
				portalY0 = fixed16ToFloat(openTop);
				portalY1 = fixed16ToFloat(openBot);

				if (openSize > 0)
				{
					// Is the portal inside the view frustum?
					// Cull the portal but potentially keep the edge.
					Vec3f qv0 = { x0, portalY0, z0 }, qv1 = { x1, portalY1, z1 };
					isPortal = frustum_quadInside(qv0, qv1);
				}
			}

			// Add a new segment.
			Segment* seg = &s_wallSegments[segCount];
			Vec2f v0 = { x0, z0 }, v1 = { x1, z1 }, heights = { y0, y1 }, portalHeights = { portalY0, portalY1 };
			if (!createNewSegment(seg, w, isPortal, v0, v1, heights, portalHeights, wallNormal))
			{
				continue;
			}
			segCount++;

			// Split segments that cross the modulo boundary.
			if (seg->x1 > 4.0f)
			{
				splitSegment(initSector, s_wallSegments, segCount, seg, s_range, s_rangeSrc, s_rangeCount);
			}
			else if (!initSector && !sbuffer_splitByRange(seg, s_range, s_rangeSrc, s_rangeCount))
			{
				// Out of the range, so cancel the segment.
				segCount--;
			}
			else
			{
				assert(seg->x0 >= 0.0f && seg->x1 <= 4.0f);
			}
		}

		buildSegmentBuffer(initSector, curSector, segCount, s_wallSegments, false/*forceTreatAsSolid*/);
		return true;
	}
		
	// Clip rule called on portal segments.
	// Return true if the segment should clip the incoming segment like a regular wall.
	bool clipRule(s32 id)
	{
		// for now always return false for adjoins.
		assert(id >= 0 && id < s_clipSector->wallCount);
		RWall* wall = &s_clipSector->walls[id];
		assert(wall->nextSector);	// we shouldn't get in here if nextSector is null.
		if (!wall->nextSector)
		{
			return true;
		}
		
		// next verify that there is an opening, if not then treat it as a regular wall.
		RSector* next = wall->nextSector;
		fixed16_16 opening = min(s_clipSector->floorHeight, next->floorHeight) - max(s_clipSector->ceilingHeight, next->ceilingHeight);
		if (opening <= 0)
		{
			return true;
		}

		// if the camera is below the floor, treat it as a wall.
		const f32 floorHeight = fixed16ToFloat(next->floorHeight);
		if (s_cameraPos.y > floorHeight && s_clipObjPos.y <= floorHeight)
		{
			return true;
		}
		const f32 ceilHeight = fixed16ToFloat(next->ceilingHeight);
		if (s_cameraPos.y < ceilHeight && s_clipObjPos.y >= ceilHeight)
		{
			return true;
		}

		return false;
	}

	void clipSpriteToView(RSector* curSector, Vec3f posWS, WaxFrame* frame, void* basePtr, void* objPtr, bool fullbright, u32 portalInfo)
	{
		if (!frame) { return; }
		s_clipSector = curSector;
		s_clipObjPos = posWS;
		
		if (s_fullBright) { fullbright = true; } // TFE fullbright cheat (LABRIGHT)

		// Compute the (x,z) extents of the frame.
		const f32 widthWS  = fixed16ToFloat(frame->widthWS);
		const f32 heightWS = fixed16ToFloat(frame->heightWS);
		const f32 fOffsetX = fixed16ToFloat(frame->offsetX);
		const f32 fOffsetY = fixed16ToFloat(frame->offsetY);

		Vec3f corner0 = { posWS.x - s_cameraRight.x*fOffsetX,  posWS.y + fOffsetY,   posWS.z - s_cameraRight.z*fOffsetX };
		Vec3f corner1 = { corner0.x + s_cameraRight.x*widthWS, corner0.y - heightWS, corner0.z + s_cameraRight.z*widthWS };
		Vec2f points[] =
		{
			{ corner0.x, corner0.z },
			{ corner1.x, corner1.z }
		};
		// Cull sprites outside of the view before clipping.
		if (!frustum_quadInside(corner0, corner1)) { return; }

		// Cull sprites too close to the camera.
		// 2D culling to match the software.
		if (s_cameraDirXZ.x != 0.0f || s_cameraDirXZ.z != 0.0f)
		{
			const Vec2f relPos = { posWS.x - s_cameraPos.x, posWS.z - s_cameraPos.z };
			const f32 z = relPos.x*s_cameraDirXZ.x + relPos.z*s_cameraDirXZ.z;
			if (z < 1.0f) { return; }
		}
		// Fallback to 3D culling if necessary.
		else
		{
			const Vec3f relPos = { posWS.x - s_cameraPos.x, posWS.y - s_cameraPos.y, posWS.z - s_cameraPos.z };
			const f32 z = relPos.x*s_cameraDir.x + relPos.y*s_cameraDir.y + relPos.z*s_cameraDir.z;
			if (z < 1.0f) { return; }
		}

		// Clip against the current wall segments and the portal XZ extents.
		SegmentClipped dstSegs[1024];
		const s32 segCount = sbuffer_clipSegmentToBuffer(points[0], points[1], s_rangeCount, s_range, s_rangeSrc, 1024, dstSegs, clipRule);
		if (!segCount) { return; }

		// Then add the segments to the list.
		SpriteDrawFrame drawFrame =
		{
			basePtr, frame, objPtr,
			points[0], points[1],
			dstSegs[0].v0, dstSegs[0].v1,
			posWS.y,
			curSector,
			fullbright,
			portalInfo
		};
		sprdisplayList_addFrame(&drawFrame);

		for (s32 s = 1; s < segCount; s++)
		{
			drawFrame.c0 = dstSegs[s].v0;
			drawFrame.c1 = dstSegs[s].v1;
			sprdisplayList_addFrame(&drawFrame);
		}
	}
		
	void addSectorObjects(RSector* curSector, RSector* prevSector, s32 portalId, s32 prevPortalId)
	{
		// Decide how to clip objects.
		// Which top and bottom edges are we going to use to clip objects?
		s32 topPortal = portalId;
		s32 botPortal = portalId;

		if (prevSector)
		{
			fixed16_16 nextTop = curSector->ceilingHeight;
			fixed16_16 curTop = min(prevSector->floorHeight, max(nextTop, prevSector->ceilingHeight));
			f32 top = fixed16ToFloat(curTop);
			if (top < s_cameraPos.y && prevSector && prevSector->ceilingHeight <= curSector->ceilingHeight)
			{
				topPortal = prevPortalId;
			}

			fixed16_16 nextBot = curSector->floorHeight;
			fixed16_16 curBot = max(prevSector->ceilingHeight, min(nextBot, prevSector->floorHeight));
			f32 bot = fixed16ToFloat(curBot);
			if (bot > s_cameraPos.y && prevSector && prevSector->floorHeight >= curSector->floorHeight)
			{
				botPortal = prevPortalId;
			}
		}

		// Add the object portals.
		u32 portalInfo = 0u;
		if (topPortal || botPortal)
		{
			Vec4f outPlanes[MAX_PORTAL_PLANES * 2];
			u32 planeCount = 0;
			if (topPortal == botPortal)
			{
				planeCount = sdisplayList_getPlanesFromPortal(topPortal, PLANE_TYPE_BOTH, outPlanes);
			}
			else
			{
				planeCount  = sdisplayList_getPlanesFromPortal(topPortal, PLANE_TYPE_TOP, outPlanes);
				planeCount += sdisplayList_getPlanesFromPortal(botPortal, PLANE_TYPE_BOT, outPlanes + planeCount);
				planeCount = min((s32)MAX_PORTAL_PLANES, (s32)planeCount);
			}
			portalInfo = objectPortalPlanes_add(planeCount, outPlanes);
		}

		f32 ambient = (s_flatLighting) ? f32(s_flatAmbient) : fixed16ToFloat(curSector->ambient);
		const Vec2f floorOffset = { fixed16ToFloat(curSector->floorOffset.x), fixed16ToFloat(curSector->floorOffset.z) };
		const Vec2f ceilOffset = { fixed16ToFloat(curSector->ceilOffset.x), fixed16ToFloat(curSector->ceilOffset.z) };
		if (s_fullBright) { ambient = MAX_LIGHT_LEVEL - 1; } // TFE fullbright cheat (LABRIGHT)

		SecObject** objIter = curSector->objectList;
		for (s32 i = 0; i < curSector->objectCount; objIter++)
		{
			SecObject* obj = *objIter;
			if (!obj) { continue; }
			i++;

			if ((obj->flags & OBJ_FLAG_NEEDS_TRANSFORM) && obj->ptr)
			{
				const s32 type = obj->type;
				Vec3f posWS = { fixed16ToFloat(obj->posWS.x), fixed16ToFloat(obj->posWS.y), fixed16ToFloat(obj->posWS.z) };
				if (type == OBJ_TYPE_SPRITE || type == OBJ_TYPE_FRAME)
				{
					if (type == OBJ_TYPE_SPRITE)
					{
						f32 dx = s_cameraPos.x - posWS.x;
						f32 dz = s_cameraPos.z - posWS.z;
						angle14_16 angle = vec2ToAngle(dx, dz);

						// Angles range from [0, 16384), divide by 512 to get 32 even buckets.
						s32 angleDiff = (angle - obj->yaw) >> 9;
						angleDiff &= 31;	// up to 32 views

						// Get the animation based on the object state.
						Wax* wax = obj->wax;
						WaxAnim* anim = WAX_AnimPtr(wax, obj->anim & 31);
						if (anim)
						{
							// Then get the Sequence from the angle difference.
							WaxView* view = WAX_ViewPtr(wax, anim, 31 - angleDiff);
							// And finally the frame from the current sequence.
							WaxFrame* frame = WAX_FramePtr(wax, view, obj->frame & 31);
							clipSpriteToView(curSector, posWS, frame, wax, obj, (obj->flags & OBJ_FLAG_FULLBRIGHT) != 0, portalInfo);
						}
					}
					else if (type == OBJ_TYPE_FRAME)
					{
						clipSpriteToView(curSector, posWS, obj->fme, obj->fme, obj, (obj->flags & OBJ_FLAG_FULLBRIGHT) != 0, portalInfo);
					}
				}
				else if (type == OBJ_TYPE_3D)
				{
					model_add(obj, obj->model, posWS, obj->transform, ambient, floorOffset, ceilOffset, portalInfo);
				}
			}
		}
	}
		
	void traverseSector(RSector* curSector, RSector* prevSector, RWall* portalWall, s32 prevPortalId, s32& level, u32& uploadFlags, Vec2f p0, Vec2f p1)
	{
		if (level > MAX_ADJOIN_DEPTH_EXT)
		{
			return;
		}
		
		// Mark sector as being rendered for the automap.
		curSector->flags1 |= SEC_FLAGS1_RENDERED;

		// Build the world-space wall segments.
		u32 segCount = 0;
		if (!buildSectorWallSegments(curSector, prevSector, portalWall, uploadFlags, level == 0, p0, p1, segCount))
		{
			return;
		}

		// There is a portal but the sector beyond is degenerate but has a sky.
		// In this case the software renderer will still fill in the sky even though no walls are visible, so the GPU
		// renderer needs to emulate the same behavior.
		const u32 extAndPit = SEC_FLAGS1_EXTERIOR | SEC_FLAGS1_PIT;
		const JBool canTreatPortalAsSky = (curSector->flags1 & extAndPit) && prevSector && (prevSector->flags1 & extAndPit);
		if (segCount == 0 && canTreatPortalAsSky)
		{
			addPortalAsSky(prevSector, portalWall);
			return;
		}

		// Determine which objects are visible and add them.
		addSectorObjects(curSector, prevSector, s_displayCurrentPortalId, prevPortalId);

		// Traverse through visible portals.
		s32 parentPortalId = s_displayCurrentPortalId;

		const s32 portalStart = s_portalListCount;
		const s32 portalCount = traversal_addPortals(curSector);
		Portal* portal = &s_portalList[portalStart];
		for (s32 p = 0; p < portalCount && s_portalsTraversed < s_maxPortals; p++, portal++)
		{
			frustum_push(portal->frustum);
			level++;
			s_portalsTraversed++;

			// Add a portal to the display list.
			Vec3f corner0 = { portal->v0.x, portal->y0, portal->v0.z };
			Vec3f corner1 = { portal->v1.x, portal->y1, portal->v1.z };
			if (sdisplayList_addPortal(corner0, corner1, parentPortalId))
			{
				portal->wall->drawFrame = s_gpuFrame;
				traverseSector(portal->next, curSector, portal->wall, parentPortalId, level, uploadFlags, portal->v0, portal->v1);
				portal->wall->drawFrame = 0;
			}

			frustum_pop();
			level--;
		}
	}
						
	bool traverseScene(RSector* sector)
	{
#if 0
		debug_update();
#endif

		// First build the camera frustum and push it onto the stack.
		frustum_buildFromCamera();

		s32 level = 0;
		u32 uploadFlags = UPLOAD_NONE;
		s_portalsTraversed = 0;
		s_portalListCount = 0;
		s_wallSegGenerated = 0;
		Vec2f startView[] = { {0,0}, {0,0} };

		// Compute an XZ direction for sprite culling.
		const f32 cameraDirMag = s_cameraDir.x*s_cameraDir.x + s_cameraDir.z*s_cameraDir.z;
		if (cameraDirMag > FLT_EPSILON)
		{
			const f32 scale = 1.0f / sqrtf(cameraDirMag);
			s_cameraDirXZ.x = s_cameraDir.x * scale;
			s_cameraDirXZ.z = s_cameraDir.z * scale;
		}
		else
		{
			s_cameraDirXZ.x = 0.0f;
			s_cameraDirXZ.z = 0.0f;
		}

		sdisplayList_clear();
		sprdisplayList_clear();
		model_drawListClear();
		objectPortalPlanes_clear();

		updateCachedSector(sector, uploadFlags);
		traverseSector(sector, nullptr, nullptr, 0, level, uploadFlags, startView[0], startView[1]);
		frustum_pop();

		// Fixup the transparencies if using bilinear filtering.
		const TFE_Settings_Graphics* graphics = TFE_Settings::getGraphicsSettings();
		if (graphics->colorMode == COLORMODE_TRUE_COLOR && graphics->useBilinear)
		{
			sdisplayList_fixupTrans();
		}

		sdisplayList_finish();
		sprdisplayList_finish();
		model_drawListFinish();
		objectPortalPlanes_finish();

		// Set the sector ambient for future lighting.
		if (s_flatLighting)
		{
			s_sectorAmbient = s_flatAmbient;
		}
		else
		{
			s_sectorAmbient = round16(sector->ambient);
		}
		s_scaledAmbient = (s_sectorAmbient >> 1) + (s_sectorAmbient >> 2) + (s_sectorAmbient >> 3);
		s_sectorAmbientFraction = s_sectorAmbient << 11;	// fraction of ambient compared to max.

		if (uploadFlags & UPLOAD_SECTORS)
		{
			s_sectorGpuBuffer.update(s_gpuSourceData.sectors, s_gpuSourceData.sectorSize);
		}
		if (uploadFlags & UPLOAD_WALLS)
		{
			s_wallGpuBuffer.update(s_gpuSourceData.walls, s_gpuSourceData.wallSize);
		}

		return sdisplayList_getSize() > 0;
	}

	void handleTextureFiltering(const TextureGpu* texture)
	{
		if (s_shaderSettings.trueColor)
		{
			const TFE_Settings_Graphics* settings = TFE_Settings::getGraphicsSettings();
			MagFilter magFilter = MAG_FILTER_NONE;
			MinFilter minFilter = MIN_FILTER_NONE;
			if (settings->useBilinear)
			{
				magFilter = MAG_FILTER_LINEAR;
				minFilter = MIN_FILTER_LINEAR;
			}
			if (settings->useMipmapping)
			{
				minFilter = MIN_FILTER_MIPMAP;
			}
			texture->setFilter(magFilter, minFilter, true);
		}
	}

	void drawPass(SectorPass pass)
	{
		if (!sdisplayList_getSize(pass)) { return; }
		const TFE_Settings_Graphics* settings = TFE_Settings::getGraphicsSettings();

		TexturePacker* texturePacker = texturepacker_getGlobal();
		const TextureGpu* palette  = TFE_RenderBackend::getPaletteTexture();
		const TextureGpu* textures = texturePacker->texture;
		const ShaderInputsGPU* inputs = &s_shaderInputs[pass];
		const ShaderSkyInputs* skyInputs = &s_shaderSkyInputs[pass];
		const ShaderBuffer* textureTable = &texturePacker->textureTableGPU;

		TFE_RenderState::setStateEnable(true, STATE_DEPTH_WRITE | STATE_DEPTH_TEST);
		TFE_RenderState::setDepthFunction(CMP_LEQUAL);
				
		Shader* shader = &s_wallShader[pass];
		shader->bind();

		s_indexBuffer.bind();
		s_sectorGpuBuffer.bind(0);
		s_wallGpuBuffer.bind(1);
		s_colormapTex->bind(5);
		if (s_shaderSettings.trueColor)
		{
			s_trueColorMapping->bind(6);
		}
		else
		{
			palette->bind(6);
		}
		textures->bind(7);
		handleTextureFiltering(textures);

		textureTable->bind(8);

	#if ACCURATE_MAPPING_ENABLE // Future work
		if (s_trueColorToPal)
		{
			s_trueColorToPal->bind(9);
		}
	#endif

		// Camera and lighting.
		const Vec4f lightData = { f32(s_worldAmbient), s_cameraLightSource ? 1.0f : 0.0f, 0.0f, s_showWireframe ? 1.0f : 0.0f };
		shader->setVariable(inputs->cameraPosId,  SVT_VEC3,   s_cameraPos.m);
		shader->setVariable(inputs->cameraViewId, SVT_MAT3x3, s_cameraMtx.data);
		shader->setVariable(inputs->cameraProjId, SVT_MAT4x4, s_cameraProj.data);
		shader->setVariable(inputs->cameraDirId,  SVT_VEC3,   s_cameraDir.m);
		shader->setVariable(inputs->lightDataId,  SVT_VEC4,   lightData.m);

		// Calculte the sky parallax.
		fixed16_16 p0, p1;
		TFE_Jedi::getSkyParallax(&p0, &p1);
		// The values are scaled by 4 to convert from angle to fixed in the original code.
		const f32 parallax[2] = { fixed16ToFloat(p0), fixed16ToFloat(p1) };
		shader->setVariable(skyInputs->skyParallaxId, SVT_VEC2, parallax);
		if (skyInputs->skyParam0Id >= 0)
		{
			u32 dispWidth, dispHeight;
			vfb_getResolution(&dispWidth, &dispHeight);
			const f32 ssFactor = f32(TFE_RenderBackend::getVirtualDisplaySupersampleFactor());

			// Compute the camera yaw from the camera direction and rotate it 90 degrees.
			// This generates a value from 0 to 1.
			f32 cameraYaw = fmodf(-atan2f(s_cameraDir.z, s_cameraDir.x) / (2.0f*PI) + 0.25f, 1.0f);
			cameraYaw = cameraYaw < 0.0f ? cameraYaw + 1.0f : cameraYaw;

			f32 cameraPitch = asinf(s_cameraDir.y);

			const f32 oneOverTwoPi = 1.0f / 6.283185f;
			const f32 rad45 = 0.785398f;	// 45 degrees in radians.
			const f32 skyParam0[4] =
			{
				cameraYaw * parallax[0],
				clamp(cameraPitch, -rad45, rad45) * parallax[1] * oneOverTwoPi,
				parallax[0] * oneOverTwoPi,
				200.0f / (f32(dispHeight) * ssFactor),
			};
			const f32 skyParam1[2] =
			{
			   -s_rcfltState.nearPlaneHalfLen,
				s_rcfltState.nearPlaneHalfLen * 2.0f / (f32(dispWidth) * ssFactor),
			};
			shader->setVariable(skyInputs->skyParam0Id, SVT_VEC4, skyParam0);
			shader->setVariable(skyInputs->skyParam1Id, SVT_VEC2, skyParam1);
		}

		if (s_shaderInputs[pass].globalLightingId >= 0)
		{
			const f32 globalLighting[] = { s_flatLighting ? 1.0f : 0.0f, (f32)s_flatAmbient, 0.0f, 0.0f };
			shader->setVariable(s_shaderInputs[pass].globalLightingId, SVT_VEC4, globalLighting);
		}
		if (s_shaderInputs[pass].texSamplingParamId >= 0)
		{
			const f32 texSamplingParam[] = { settings->useBilinear ? settings->bilinearSharpness : 0.0f, 0.0f, 0.0f, 0.0f };
			shader->setVariable(s_shaderInputs[pass].texSamplingParamId, SVT_VEC4, texSamplingParam);
		}
		if (s_shaderInputs[pass].palFxLumMask >= 0 && s_shaderInputs[pass].palFxFlash >= 0)
		{
			Vec3f lumMask, palFx;
			renderer_getPalFx(&lumMask, &palFx);

			shader->setVariable(s_shaderInputs[pass].palFxLumMask, SVT_VEC3, lumMask.m);
			shader->setVariable(s_shaderInputs[pass].palFxFlash, SVT_VEC3, palFx.m);
		}
		if (s_shaderInputs[pass].textureSettings >= 0)
		{
			shader->setVariable(s_shaderInputs[pass].textureSettings, SVT_USCALAR, &s_textureSettings);
		}

		// Draw the sector display list.
		sdisplayList_draw(pass);

		// Reset
		textures->bind(7);
		if (settings->useBilinear)
		{
			textures->setFilter(MagFilter::MAG_FILTER_NONE, MinFilter::MIN_FILTER_NONE, true);
		}
	}

	void drawSprites()
	{
		if (!sprdisplayList_getSize()) { return; }
		const TFE_Settings_Graphics* settings = TFE_Settings::getGraphicsSettings();
		// For some reason depth test is required to write, so set the comparison function to always instead.
		TFE_RenderState::setStateEnable(true,  STATE_DEPTH_WRITE | STATE_DEPTH_TEST);
		TFE_RenderState::setDepthFunction(CMP_ALWAYS);

		s_spriteShader.bind();
		s_indexBuffer.bind();
		s_colormapTex->bind(3);

		const TextureGpu* palette = TFE_RenderBackend::getPaletteTexture();
		//palette->bind(4);
		if (s_shaderSettings.trueColor)
		{
			s_trueColorMapping->bind(4);
		}
		else
		{
			palette->bind(4);
		}

		TexturePacker* texturePacker = texturepacker_getGlobal();
		const TextureGpu* textures = texturePacker->texture;
		textures->bind(5);
		if (settings->useBilinear)
		{
			textures->setFilter(MagFilter::MAG_FILTER_LINEAR, MinFilter::MIN_FILTER_LINEAR, true);
		}

		ShaderBuffer* textureTable = &texturePacker->textureTableGPU;
		textureTable->bind(6);

		// Camera and lighting.
		Vec4f lightData = { f32(s_worldAmbient), s_cameraLightSource ? 1.0f : 0.0f, 0.0f, s_showWireframe ? 1.0f : 0.0f };
		s_spriteShader.setVariable(s_cameraRightId, SVT_VEC3, s_cameraRight.m);
		s_spriteShader.setVariable(s_shaderInputs[SPRITE_PASS].cameraPosId,  SVT_VEC3,   s_cameraPos.m);
		s_spriteShader.setVariable(s_shaderInputs[SPRITE_PASS].cameraViewId, SVT_MAT3x3, s_cameraMtx.data);
		s_spriteShader.setVariable(s_shaderInputs[SPRITE_PASS].cameraProjId, SVT_MAT4x4, s_cameraProj.data);
		s_spriteShader.setVariable(s_shaderInputs[SPRITE_PASS].cameraDirId,  SVT_VEC3,   s_cameraDir.m);
		s_spriteShader.setVariable(s_shaderInputs[SPRITE_PASS].lightDataId,  SVT_VEC4,   lightData.m);

		if (s_shaderInputs[SPRITE_PASS].globalLightingId >= 0)
		{
			const f32 globalLighting[] = { s_flatLighting ? 1.0f : 0.0f, (f32)s_flatAmbient, 0.0f, 0.0f };
			s_spriteShader.setVariable(s_shaderInputs[SPRITE_PASS].globalLightingId, SVT_VEC4, globalLighting);
		}
		if (s_shaderInputs[SPRITE_PASS].texSamplingParamId >= 0)
		{
			const f32 texSamplingParam[] = { settings->useBilinear ? settings->bilinearSharpness : 0.0f, 0.0f, 0.0f, 0.0f };
			s_spriteShader.setVariable(s_shaderInputs[SPRITE_PASS].texSamplingParamId, SVT_VEC4, texSamplingParam);
		}
		if (s_shaderInputs[SPRITE_PASS].palFxLumMask >= 0 && s_shaderInputs[SPRITE_PASS].palFxFlash >= 0)
		{
			Vec3f lumMask, palFx;
			renderer_getPalFx(&lumMask, &palFx);

			s_spriteShader.setVariable(s_shaderInputs[SPRITE_PASS].palFxLumMask, SVT_VEC3, lumMask.m);
			s_spriteShader.setVariable(s_shaderInputs[SPRITE_PASS].palFxFlash, SVT_VEC3, palFx.m);
		}
		if (s_shaderInputs[SPRITE_PASS].textureSettings >= 0)
		{
			s_spriteShader.setVariable(s_shaderInputs[SPRITE_PASS].textureSettings, SVT_USCALAR, &s_textureSettings);
		}

		// Draw the sector display list.
		sprdisplayList_draw();

		s_spriteShader.unbind();

		textures->bind(5);
		if (settings->useBilinear)
		{
			textures->setFilter(MagFilter::MAG_FILTER_NONE, MinFilter::MIN_FILTER_NONE, true);
		}
	}

	void draw3d()
	{
		const TFE_Settings_Graphics* settings = TFE_Settings::getGraphicsSettings();

		TFE_RenderState::setStateEnable(true, STATE_DEPTH_WRITE | STATE_DEPTH_TEST);
		TFE_RenderState::setDepthFunction(CMP_LEQUAL);

		const TextureGpu* palette = TFE_RenderBackend::getPaletteTexture();

		if (s_shaderSettings.trueColor)
		{
			s_trueColorMapping->bind(0);
			palette->bind(5);
		}
		else
		{
			palette->bind(0);
		}

		s_colormapTex->bind(1);

		TexturePacker* texturePacker = texturepacker_getGlobal();
		const TextureGpu* textures = texturePacker->texture;
		textures->bind(2);
		handleTextureFiltering(textures);

		ShaderBuffer* textureTable = &texturePacker->textureTableGPU;
		textureTable->bind(3);
		objectPortalPlanes_bind(4);
		model_drawList();

		// Cleanup
		textures->bind(2);
		if (settings->useBilinear)
		{
			textures->setFilter(MagFilter::MAG_FILTER_NONE, MinFilter::MIN_FILTER_NONE, true);
		}

		TextureGpu::clearSlots(3);
	}
		
	void TFE_Sectors_GPU::draw(RSector* sector)
	{
		// Check to see if a rendering setting has changed
		// (this may require a shader recompile)
		updateShaderSettings(false);

		// Build the draw list.
		if (!traverseScene(sector))
		{
			return;
		}

		// State
		TFE_RenderState::setStateEnable(false, STATE_BLEND);
		TFE_RenderState::setStateEnable(true, STATE_DEPTH_WRITE | STATE_DEPTH_TEST | STATE_CULLING);
		if (s_showWireframe)
		{
			TFE_RenderState::setStateEnable(true, STATE_WIREFRAME);
		}

		for (s32 i = 0; i < SECTOR_PASS_COUNT - 1; i++)
		{
			drawPass(SectorPass(i));
		}
				
		// Draw Sprites.
		drawSprites();

		// Draw transparent pass.
		drawPass(SECTOR_PASS_TRANS);

		// Draw 3D Objects.
		draw3d();

		// TODO: Alpha blended passes afterward, discard alpha < 0.5 for passes above.
		// The idea is to reduce the size of the area where issues can occur but avoid clear gaps.
		// OR: Split the transparent mid-texture and sprite passes and interleave (most accurate).
				
		// Cleanup
		s_indexBuffer.unbind();
		s_sectorGpuBuffer.unbind(0);
		s_wallGpuBuffer.unbind(1);
		TextureGpu::clear(5);
		TextureGpu::clear(6);

		TFE_RenderState::setStateEnable(false, STATE_WIREFRAME);
		
		// Debug
		if (s_enableDebug)
		{
			renderDebug_draw();
		}
	}

	TextureGpu* TFE_Sectors_GPU::getColormap()
	{
		return s_colormapTex;
	}

	void TFE_Sectors_GPU::subrendererChanged()
	{
	}

	bool TFE_Sectors_GPU::updateShaders()
	{
		// Base Pass
		ShaderDefine defines[16] = {};

		s32 defineCount = 0;
		if (s_shaderSettings.skyMode == SKYMODE_VANILLA)
		{
			defines[0].name = "SKYMODE_VANILLA";
			defines[0].value = "1";
			defineCount = 1;
		}
		if (s_shaderSettings.ditheredBilinear)
		{
			defines[defineCount].name = "OPT_BILINEAR_DITHER";
			defines[defineCount].value = "1";
			defineCount++;
		}
		if (s_shaderSettings.bloom)
		{
			defines[defineCount].name = "OPT_BLOOM";
			defines[defineCount].value = "1";
			defineCount++;
		}
		if (s_shaderSettings.colormapInterp || s_shaderSettings.trueColor)
		{
			defines[defineCount].name = "OPT_COLORMAP_INTERP";
			defines[defineCount].value = "1";
			defineCount++;

			defines[defineCount].name = "OPT_SMOOTH_LIGHTRAMP";
			defines[defineCount].value = "1";
			defineCount++;
		}
		if (s_shaderSettings.trueColor)
		{
			defines[defineCount].name = "OPT_TRUE_COLOR";
			defines[defineCount].value = "1";
			defineCount++;
		}
		bool success = loadShaderVariant(0, defineCount, defines);

		// Load the transparent version of the shader.
		defines[0].name = "SECTOR_TRANSPARENT_PASS";
		defines[0].value = "1";
		defineCount = 1;
		if (s_shaderSettings.ditheredBilinear)
		{
			defines[defineCount].name = "OPT_BILINEAR_DITHER";
			defines[defineCount].value = "1";
			defineCount++;
		}
		if (s_shaderSettings.bloom)
		{
			defines[defineCount].name = "OPT_BLOOM";
			defines[defineCount].value = "1";
			defineCount++;
		}
		if (s_shaderSettings.colormapInterp || s_shaderSettings.trueColor)
		{
			defines[defineCount].name = "OPT_COLORMAP_INTERP";
			defines[defineCount].value = "1";
			defineCount++;

			defines[defineCount].name = "OPT_SMOOTH_LIGHTRAMP";
			defines[defineCount].value = "1";
			defineCount++;
		}
		if (s_shaderSettings.trueColor)
		{
			defines[defineCount].name = "OPT_TRUE_COLOR";
			defines[defineCount].value = "1";
			defineCount++;
		}
		success |= loadShaderVariant(1, defineCount, defines);

		defineCount = 0;
		if (s_shaderSettings.ditheredBilinear)
		{
			defines[defineCount].name = "OPT_BILINEAR_DITHER";
			defines[defineCount].value = "1";
			defineCount++;
		}
		if (s_shaderSettings.bloom)
		{
			defines[defineCount].name = "OPT_BLOOM";
			defines[defineCount].value = "1";
			defineCount++;
		}
		if (s_shaderSettings.colormapInterp || s_shaderSettings.trueColor)
		{
			defines[defineCount].name = "OPT_COLORMAP_INTERP";
			defines[defineCount].value = "1";
			defineCount++;

			defines[defineCount].name = "OPT_SMOOTH_LIGHTRAMP";
			defines[defineCount].value = "1";
			defineCount++;
		}
		if (s_shaderSettings.trueColor)
		{
			defines[defineCount].name = "OPT_TRUE_COLOR";
			defines[defineCount].value = "1";
			defineCount++;
		}
		success |= loadSpriteShader(defineCount, defines);

		assert(success);
		return success;
	}
}
