#pragma once
//////////////////////////////////////////////////////////////////////
// Jedi Renderer
// Classic Dark Forces (DOS) Jedi derived renderer.
//
// Copyright note:
// While the project as a whole is licensed under GPL-2.0, some of the
// code under TFE_JediRenderer/ was derived from reverse-engineered
// code from "Dark Forces" (DOS) which is copyrighted by LucasArts.
// The original renderer is contained in RClassic_Fixed/ and the
// RClassic_Float/ and RClassic_GPU/ sub-renderers are derived from
// that code.
//
// I consider the reverse-engineering to be "Fair Use" - a means of 
// supporting the games on other platforms and to improve support on
// existing platforms without claiming ownership of the games
// themselves or their IPs.
//
// That said using this code in a commercial project is risky without
// permission of the original copyright holders (LucasArts).
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Renderer/virtualFramebuffer.h>
#include <TFE_Jedi/Renderer/textureInfo.h>

enum TFE_SubRenderer
{
	TSR_CLASSIC_FIXED = 0,	// The Reverse-Engineered DOS Jedi Renderer, using 16.16 fixed point.
	TSR_CLASSIC_FLOAT,		// Derived from the Reverse-Engineered Jedi Renderer but using floating point.
	TSR_CLASSIC_GPU,		// Derived from the Reverse-Engineered Jedi Renderer but using the GPU for low-level rendering.
	TSR_COUNT,
	TSR_HIGH_RESOLUTION,	// Pick a high resolution renderer based on the type.
	TSR_INVALID,			// Invalid sub-renderer.
};

enum RendererType
{
	RENDERER_SOFTWARE = 0,
	RENDERER_HARDWARE,
	RENDERER_COUNT
};

namespace TFE_Jedi
{
	void renderer_resetState();
	void renderer_init();
	void renderer_destroy();
	void renderer_reset();
	void renderer_setLimits();
	void renderer_setType(RendererType type = RENDERER_SOFTWARE);
	void setupInitCameraAndLights();
	void renderer_computeCameraTransform(RSector* sector, angle14_32 pitch, angle14_32 yaw, fixed16_16 camX, fixed16_16 camY, fixed16_16 camZ);
		
	// Set the current sub-renderer.
	// Note that changing the sub-renderer at runtime may result in re-initialization of rendering data, causing a hitch.
	JBool setSubRenderer(TFE_SubRenderer subRenderer = TSR_CLASSIC_FIXED);
	TFE_SubRenderer getSubRenderer();
	RendererType renderer_getType();

	// Camera parameters: yaw, pitch, position (x, y, z)
	//                    sectorId containing the camera.
	//                    ambient base or offset (default = MAX_LIGHT_LEVEL).
	//                    camera light source - true if there is a camera based light source, in which case worldAmbient is treated as an offset.
	//											false if there is no camera light source, worldAmbient is then the base ambient.
	//void setCamera(f32 yaw, f32 pitch, f32 x, f32 y, f32 z, s32 sectorId, s32 worldAmbient = 0, bool cameraLightSource = false);
	// Draw the scene to the passed in display using the colormap for shading.
	void drawWorld(u8* display, RSector* sector, const u8* colormap, const u8* lightSourceRamp);

	// Added for TFE so the GPU renderer knows the beginning and end of the drawing frame.
	void beginRender();
	void endRender();

	JBool render_setResolution(bool forceUpdate = false);
	void render_clearCachedTextures();
	void renderer_setVisionEffect(s32 effect);
	void renderer_setupCameraLight(JBool flatShading, JBool headlamp);
	void renderer_setWorldAmbient(s32 value);
	void renderer_setSourcePalette(const u32* srcPalette);
	void renderer_setPalFx(const Vec3f* lumMask, const Vec3f* palFx);
	
	const u32* renderer_getSourcePalette();
	void renderer_getPalFx(Vec3f* lumMask, Vec3f* palFx);

	// 2D
	void blitTextureToScreen(TextureData* texture, s32 x0, s32 y0);
	void clear3DView(u8* framebuffer);

	// TFE
	// Add a hud texture callback, these will be called when setting up the GPU renderer
	void renderer_addHudTextureCallback(TextureListCallback hudTextureCallback);

	extern s32 s_drawnObjCount;
	extern bool s_showWireframe;
	extern SecObject* s_drawnObj[];
	extern bool s_drawQuadsFirst;
	extern bool s_clipLinesToRect;
	extern s32 s_clipX;
	extern s32 s_clipY;
	extern s32 s_clipW;
	extern s32 s_clipH;
}