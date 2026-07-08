#include "jediRenderer.h"
#include <TFE_Jedi/Math/fixedPoint.h>
#include <TFE_Jedi/Level/robject.h>
#include <TFE_Jedi/Level/level.h>
#include "rcommon.h"
#include "rsectorRender.h"
#include "screenDraw.h"
#include "RClassic_Fixed/rclassicFixedSharedState.h"
#include "RClassic_Fixed/rclassicFixed.h"
#include "RClassic_Fixed/rsectorFixed.h"

#include "RClassic_Float/rclassicFloat.h"
#include "RClassic_Float/rsectorFloat.h"
#include "RClassic_Float/rclassicFloatSharedState.h"

#include "RClassic_GPU/rclassicGPU.h"
#include "RClassic_GPU/rsectorGPU.h"
#include "RClassic_GPU/screenDrawGPU.h"

#include <TFE_System/profiler.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Settings/settings.h>
#include <TFE_Asset/spriteAsset_Jedi.h>
#include <TFE_Asset/modelAsset_jedi.h>
#include <TFE_FrontEndUI/console.h>

namespace TFE_Jedi
{
	static bool s_init = false;
	static TFE_SubRenderer s_subRenderer = TSR_CLASSIC_FIXED;
	static std::vector<TextureListCallback> s_hudTextureCallbacks;
	static TFE_Sectors* s_sectorRendererCache[TSR_COUNT] = { nullptr };
	static bool s_trueColor = false;
	static bool s_enableMips = false;
	static Vec3f s_lumMask = { 0 };
	static Vec3f s_palFx = { 0 };
	static u32 s_sourcePalette[256];
	bool s_showWireframe = false;
	TFE_Sectors* s_sectorRenderer = nullptr;
	RendererType s_rendererType = RENDERER_SOFTWARE;

	/////////////////////////////////////////////
	// Forward Declarations
	/////////////////////////////////////////////
	void clear1dDepth();
	void console_setSubRenderer(const std::vector<std::string>& args);
	void console_getSubRenderer(const std::vector<std::string>& args);

	/////////////////////////////////////////////
	// Implementation
	/////////////////////////////////////////////
	void renderer_resetState()
	{
		RClassic_Fixed::resetState();
		RClassic_Float::resetState();
		RClassic_GPU::resetState();
		s_hudTextureCallbacks.clear();
		screen_clear();

		for (s32 i = 0; i < TSR_COUNT; i++)
		{
			if (s_sectorRendererCache[i])
			{
				s_sectorRendererCache[i]->destroy();
				delete s_sectorRendererCache[i];
			}
			s_sectorRendererCache[i] = nullptr;
		}

		s_sectorRenderer = nullptr;
		s_subRenderer = TSR_INVALID;
		s_init = false;
		s_trueColor = false;
		s_enableMips = false;
		vfb_setMode();
	}

	TFE_Sectors* renderer_getSectorRenderer(TFE_SubRenderer renderer)
	{
		switch (renderer)
		{
			case TSR_CLASSIC_FIXED:
				if (!s_sectorRendererCache[renderer])
				{
					s_sectorRendererCache[renderer] = new TFE_Sectors_Fixed();
				}
				return s_sectorRendererCache[renderer];
				break;
			case TSR_CLASSIC_FLOAT:
				if (!s_sectorRendererCache[renderer])
				{
					s_sectorRendererCache[renderer] = new TFE_Sectors_Float();
				}
				return s_sectorRendererCache[renderer];
				break;
			case TSR_CLASSIC_GPU:
				if (!s_sectorRendererCache[renderer])
				{
					s_sectorRendererCache[renderer] = new TFE_Sectors_GPU();
				}
				return s_sectorRendererCache[renderer];
				break;
		}
		TFE_System::logWrite(LOG_ERROR, "Jedi Renderer", "Invalid Sector Renderer.");
		return nullptr;
	}
	
	void renderer_init()
	{
		if (s_init) { return; }
		s_init = true;
		// Setup Debug CVars.
		s_maxWallCount = 0xffff;
		s_maxDepthCount = 0xffff;
		CVAR_INT(s_maxWallCount,  "d_maxWallCount",  CVFLAG_DO_NOT_SERIALIZE, "Maximum wall count for a given sector.");
		CVAR_INT(s_maxDepthCount, "d_maxDepthCount", CVFLAG_DO_NOT_SERIALIZE, "Maximum adjoin depth count.");
		CVAR_INT(s_sectorAmbient, "d_sectorAmbient", CVFLAG_DO_NOT_SERIALIZE, "Current Sector Ambient.");
		CVAR_BOOL(s_showWireframe, "d_enableWireframe", CVFLAG_DO_NOT_SERIALIZE, "Enable wireframe rendering.");

		// Remove temporarily until they do something useful again.
		CCMD("rsetSubRenderer", console_setSubRenderer, 1, "Set the sub-renderer - valid values are: Classic_Fixed, Classic_Float, Classic_GPU.");
		CCMD("rgetSubRenderer", console_getSubRenderer, 0, "Get the current sub-renderer.");

		// Setup performance counters.
		TFE_COUNTER(s_maxAdjoinDepth, "Maximum Adjoin Depth");
		TFE_COUNTER(s_maxAdjoinIndex, "Maximum Adjoin Count");
		TFE_COUNTER(s_sectorIndex,    "Sector Count");
		TFE_COUNTER(s_flatCount,      "Flat Count");
		TFE_COUNTER(s_curWallSeg,     "Wall Segment Count");
		TFE_COUNTER(s_adjoinSegCount, "Adjoin Segment Count");

		s_sectorRenderer = renderer_getSectorRenderer(TSR_CLASSIC_FIXED);
		renderer_setLimits();
	}

	void renderer_destroy()
	{
		renderer_resetState();
		screenGPU_destroy();
	}

	void renderer_reset()
	{
		// Reset all allocated renderers.
		for (s32 i = 0; i < TSR_COUNT; i++)
		{
			if (s_sectorRendererCache[i])
			{
				s_sectorRendererCache[i]->reset();
			}
		}
	}

	void renderer_setLimits()
	{
		if (TFE_Settings::extendAdjoinLimits())
		{
			s_maxSegCount = MAX_SEG_EXT;
			s_maxAdjoinSegCount = MAX_ADJOIN_SEG_EXT;
			s_maxAdjoinDepthRecursion = MAX_ADJOIN_DEPTH_EXT;
		}
		else
		{
			s_maxSegCount = MAX_SEG;
			s_maxAdjoinSegCount = MAX_ADJOIN_SEG;
			s_maxAdjoinDepthRecursion = MAX_ADJOIN_DEPTH;
		}
	}

	void renderer_setType(RendererType type)
	{
		s_rendererType = type;
		render_setResolution();
	}

	RendererType renderer_getType()
	{
		return s_rendererType;
	}

	void setupInitCameraAndLights()
	{
		u32 dispWidth, dispHeight;
		vfb_getResolution(&dispWidth, &dispHeight);
		dispWidth  = max((s32)dispWidth,  320);
		dispHeight = max((s32)dispHeight, 200);

		RClassic_Fixed::setupInitCameraAndLights();
		RClassic_Float::setupInitCameraAndLights(dispWidth, dispHeight);
		RClassic_GPU::setupInitCameraAndLights(dispWidth, dispHeight);
	}

	void blitTextureToScreen(TextureData* texture, s32 x0, s32 y0)
	{
		RClassic_Fixed::blitTextureToScreen(texture, x0, y0);
	}

	void clear3DView(u8* framebuffer)
	{
		RClassic_Fixed::clear3DView(framebuffer);
	}
		
	void renderer_addHudTextureCallback(TextureListCallback hudTextureCallback)
	{
		s_hudTextureCallbacks.push_back(hudTextureCallback);
	}

	void console_setSubRenderer(const std::vector<std::string>& args)
	{
		if (args.size() < 2) { return; }
		const char* value = args[1].c_str();

		if (strcasecmp(value, "Classic_Fixed") == 0)
		{
			setSubRenderer(TSR_CLASSIC_FIXED);
		}
		else if (strcasecmp(value, "Classic_Float") == 0)
		{
			setSubRenderer(TSR_CLASSIC_FLOAT);
		}
		else if (strcasecmp(value, "Classic_GPU") == 0)
		{
			setSubRenderer(TSR_CLASSIC_GPU);
		}
	}

	void console_getSubRenderer(const std::vector<std::string>& args)
	{
		const char* c_subRenderers[] =
		{
			"Classic_Fixed",	// TSR_CLASSIC_FIXED
			"Classic_Float",	// TSR_CLASSIC_FLOAT
			"Classic_GPU",		// TSR_CLASSIC_GPU
		};
		TFE_Console::addToHistory(c_subRenderers[s_subRenderer]);
	}

	static s32 s_fov = -1;
	static bool s_clearCachedTextures = false;

	void render_clearCachedTextures()
	{
		s_clearCachedTextures = true;
	}
				
	JBool render_setResolution(bool forceTextureUpdate)
	{
		TFE_Settings_Graphics* graphics = TFE_Settings::getGraphicsSettings();
		DisplayInfo info;
		TFE_RenderBackend::getDisplayInfo(&info);

		const bool fovChanged = (graphics->fov != s_fov);
		s_fov = graphics->fov;

		s32 width  = graphics->gameResolution.x;
		s32 height = graphics->gameResolution.z;
		if (graphics->widescreen && (height == 200 || height == 400))
		{
			width = (height * info.width / info.height) * 12 / 10;
		}
		else if (graphics->widescreen)
		{
			width = height * info.width / info.height;
		}
		// Make sure the adjustedWidth is divisible by 4.
		width = 4 * ((width + 3) >> 2);

		TFE_SubRenderer subRenderer = s_rendererType == RENDERER_HARDWARE ? TSR_CLASSIC_GPU : (width == 320 && height == 200) ? TSR_CLASSIC_FIXED : TSR_CLASSIC_FLOAT;
		vfb_setMode(subRenderer == TSR_CLASSIC_GPU ? VFB_RENDER_TRAGET : VFB_TEXTURE);
		bool updateTexturePacking = forceTextureUpdate;
		bool enableMips = s_trueColor && graphics->useMipmapping;
		if (s_trueColor != (graphics->colorMode == COLORMODE_TRUE_COLOR))
		{
			s_trueColor = (graphics->colorMode == COLORMODE_TRUE_COLOR);
			updateTexturePacking = true;
		}
		if (enableMips != s_enableMips)
		{
			s_enableMips = enableMips;
			updateTexturePacking = true;
		}
		// Update the full packing on level change if true color.
		if (s_clearCachedTextures)
		{
			s_clearCachedTextures = false;
			updateTexturePacking = true;
		}

		if (!vfb_setResolution(width, height) && !fovChanged && !updateTexturePacking)
		{
			return JFALSE;
		}

		if (s_subRenderer != subRenderer)
		{
			s_subRenderer = subRenderer;
			if (s_sectorRenderer)
			{
				s_sectorRenderer->subrendererChanged();
			}
			s_sectorRenderer = nullptr;
		}

		if (width == 320 && height == 200 && subRenderer == TSR_CLASSIC_FIXED)
		{
			if (!s_sectorRenderer)
			{
				s_sectorRenderer = renderer_getSectorRenderer(TSR_CLASSIC_FIXED);
			}
			screen_enableGPU(false);
			RClassic_Fixed::changeResolution(width, height);
		}
		else if (s_rendererType == RENDERER_SOFTWARE)
		{
			if (!s_sectorRenderer)
			{
				s_sectorRenderer = renderer_getSectorRenderer(TSR_CLASSIC_FLOAT);
			}
			screen_enableGPU(false);
			RClassic_Float::changeResolution(width, height);
		}
		else
		{
			if (forceTextureUpdate)
			{
				TFE_Sectors_GPU* sectorRendererGpu = (TFE_Sectors_GPU*)renderer_getSectorRenderer(TSR_CLASSIC_GPU);
				if (sectorRendererGpu)
				{
					sectorRendererGpu->flushTextureCache();
				}
			}

			if (!s_sectorRenderer)
			{
				TFE_Sectors_GPU* sectorRendererGpu = (TFE_Sectors_GPU*)renderer_getSectorRenderer(TSR_CLASSIC_GPU);
				screenGPU_setHudTextureCallbacks((s32)s_hudTextureCallbacks.size(), s_hudTextureCallbacks.data(), updateTexturePacking);
				s_sectorRenderer = sectorRendererGpu;
				sectorRendererGpu->flushCache();
				screenGPU_init();
			}
			else if (updateTexturePacking)
			{
				screenGPU_setHudTextureCallbacks((s32)s_hudTextureCallbacks.size(), s_hudTextureCallbacks.data(), true);
			}
			screen_enableGPU(true);
			RClassic_GPU::changeResolution(width, height);
		}
		return JTRUE;
	}

	void renderer_setVisionEffect(s32 effect)
	{
		if (s_subRenderer == TSR_CLASSIC_FIXED) { RClassic_Fixed::setVisionEffect(effect); }
	}

	TFE_SubRenderer getSubRenderer()
	{
		return s_subRenderer;
	}

	JBool setSubRenderer(TFE_SubRenderer subRenderer/* = TSR_CLASSIC_FIXED*/)
	{
		if (subRenderer == TSR_HIGH_RESOLUTION)
		{
			subRenderer = s_rendererType == RENDERER_HARDWARE ? TSR_CLASSIC_GPU : TSR_CLASSIC_FLOAT;
		}

		if (subRenderer == s_subRenderer)
		{
			return JFALSE;
		}

		s_subRenderer = subRenderer;
		if (s_sectorRenderer)
		{
			s_sectorRenderer->subrendererChanged();
		}
		s_sectorRenderer = nullptr;

		vfb_setMode(subRenderer == TSR_CLASSIC_GPU ? VFB_RENDER_TRAGET : VFB_TEXTURE);
		switch (subRenderer)
		{
			case TSR_CLASSIC_FIXED:
			{
				vfb_setResolution(320, 200);
				s_sectorRenderer = renderer_getSectorRenderer(TSR_CLASSIC_FIXED);
				screen_enableGPU(false);
				RClassic_Fixed::setupInitCameraAndLights();
			} break;
			case TSR_CLASSIC_FLOAT:
			{
				s_sectorRenderer = renderer_getSectorRenderer(TSR_CLASSIC_FLOAT);

				u32 width, height;
				vfb_getResolution(&width, &height);
				screen_enableGPU(false);
				RClassic_Float::setupInitCameraAndLights(width, height);
			} break;
			case TSR_CLASSIC_GPU:
			{
				TFE_Sectors_GPU* sectorRendererGpu = (TFE_Sectors_GPU*)renderer_getSectorRenderer(TSR_CLASSIC_GPU);
				screenGPU_setHudTextureCallbacks((s32)s_hudTextureCallbacks.size(), s_hudTextureCallbacks.data());
				s_sectorRenderer = sectorRendererGpu;
				
				u32 width, height;
				vfb_getResolution(&width, &height);
				screen_enableGPU(true);
				screenGPU_init();
				RClassic_GPU::setupInitCameraAndLights(width, height);
			} break;
		}
		return JTRUE;
	}

	void renderer_setWorldAmbient(s32 value)
	{
		s_worldAmbient = MAX_LIGHT_LEVEL - value;
	}
		
	void renderer_setSourcePalette(const u32* srcPalette)
	{
		memcpy(s_sourcePalette, srcPalette, sizeof(u32)*256);
	}
				
	void renderer_setPalFx(const Vec3f* lumMask, const Vec3f* palFx)
	{
		s_lumMask = *lumMask;
		s_palFx = *palFx;
	}

	void renderer_getPalFx(Vec3f* lumMask, Vec3f* palFx)
	{
		*lumMask = s_lumMask;
		*palFx = s_palFx;
	}

	const u32* renderer_getSourcePalette()
	{
		return s_sourcePalette;
	}

	void renderer_setupCameraLight(JBool flatShading, JBool headlamp)
	{
		s_enableFlatShading = flatShading;
		s_cameraLightSource = headlamp;
	}

	void renderer_computeCameraTransform(RSector* sector, angle14_32 pitch, angle14_32 yaw, fixed16_16 camX, fixed16_16 camY, fixed16_16 camZ)
	{
	#if 0
		if (s_subRenderer == TSR_CLASSIC_FIXED)
		{
			RClassic_Fixed::computeCameraTransform(sector, pitch, yaw, camX, camY, camZ);
		}
		else if (s_subRenderer == TSR_CLASSIC_FLOAT)
		{
			RClassic_Float::computeCameraTransform(sector, f32(pitch), f32(yaw), fixed16ToFloat(camX), fixed16ToFloat(camY), fixed16ToFloat(camZ));
		}
	#endif

		// Clamp the pitch to 60 degrees (vanilla plus) for software renderer
		// Higher values may be passed in if the camera is being moved by a VUE or is attached to a non-player object
		if (pitch > 8192) { pitch -= ANGLE_MAX; }
		angle14_32 clampedPitch = clamp(pitch, -2730, 2730);

		// For now compute both fixed-point and floating-point camera transforms so that it is easier to swap between sub-renderers.
		// TODO: Find a cleaner alternative.
		RClassic_Fixed::computeCameraTransform(sector, clampedPitch, yaw, camX, camY, camZ);
		RClassic_Float::computeCameraTransform(sector, f32(clampedPitch), f32(yaw), fixed16ToFloat(camX), fixed16ToFloat(camY), fixed16ToFloat(camZ));
		RClassic_GPU::computeCameraTransform(sector, f32(pitch), f32(yaw), fixed16ToFloat(camX), fixed16ToFloat(camY), fixed16ToFloat(camZ));
	}
		
	void beginRender()
	{
		if (!s_sectorRenderer)
		{
			TFE_SubRenderer subRenderer = s_subRenderer;
			s_subRenderer = TSR_INVALID;
			setSubRenderer(subRenderer);
		}
		if (s_subRenderer == TSR_CLASSIC_GPU)
		{
			vfb_bindRenderTarget(/*clearColor*/s_showWireframe);

			u32 width, height;
			vfb_getResolution(&width, &height);
			screenDraw_beginQuads(width, height);
		}
	}

	void endRender()
	{
		if (s_subRenderer == TSR_CLASSIC_GPU)
		{
			screenDraw_endQuads();
			screenDraw_endLines();
			vfb_unbindRenderTarget();
		}
	}

	void drawWorld(u8* display, RSector* sector, const u8* colormap, const u8* lightSourceRamp)
	{
		// Clear the top pixel row.
		if (s_subRenderer != TSR_CLASSIC_GPU)
		{
			memset(display, 0, s_width);
			memset(display + (s_height - 1) * s_width, 0, s_width);
		}

		s_drawFrame++;
		if (s_subRenderer == TSR_CLASSIC_FIXED)
		{
			RClassic_Fixed::computeSkyOffsets();
		}
		else if (s_subRenderer == TSR_CLASSIC_FLOAT)
		{
			RClassic_Float::computeSkyOffsets();
		}
		else if (s_subRenderer == TSR_CLASSIC_GPU)
		{
			RClassic_GPU::computeSkyOffsets();
		}

		s_display = display;
		s_colorMap = colormap;
		s_lightSourceRamp = lightSourceRamp;
		if (s_subRenderer != TSR_CLASSIC_GPU)
		{
			clear1dDepth();
		}

		s_windowMinX_Pixels = s_minScreenX_Pixels;
		s_windowMaxX_Pixels = s_maxScreenX_Pixels;
		s_windowMinY_Pixels = 1;
		s_windowMaxY_Pixels = s_height - 1;
		s_windowMaxCeil  = s_minScreenY;
		s_windowMinFloor = s_maxScreenY;
		s_flatCount  = 0;
		s_nextWall   = 0;
		s_curWallSeg = 0;
		s_drawnObjCount = 0;

		s_prevSector = nullptr;
		s_sectorIndex = 0;
		s_maxAdjoinIndex = 0;
		s_adjoinSegCount = 1;
		s_adjoinIndex = 0;

		s_adjoinDepth = 1;
		s_maxAdjoinDepth = 1;

		if (s_subRenderer != TSR_CLASSIC_GPU)
		{
			for (s32 i = 0; i < s_width; i++)
			{
				s_columnTop[i] = s_minScreenY;
				s_columnBot[i] = s_maxScreenY;
				s_windowTop_all[i] = s_minScreenY;
				s_windowBot_all[i] = s_maxScreenY;
			}
		}
				
		// Recursively draws sectors and their contents (sprites, 3D objects).
		{
			TFE_ZONE("Sector Draw");
			s_sectorRenderer->prepare();
			s_sectorRenderer->draw(sector);
		}
	}

	/////////////////////////////////////////////
	// Internal
	/////////////////////////////////////////////
	void clear1dDepth()
	{
		if (s_subRenderer == TSR_CLASSIC_FIXED)
		{
			memset(s_rcfState.depth1d_all, 0, s_width * sizeof(s32));
			s_rcfState.windowMinZ = 0;
		}
		else if (s_subRenderer == TSR_CLASSIC_FLOAT)
		{
			memset(s_rcfltState.depth1d_all, 0, s_width * sizeof(f32));
			s_rcfltState.windowMinZ = 0.0f;
		}
	}
}
