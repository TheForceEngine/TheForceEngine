#include <cstring>

#include "virtualFramebuffer.h"
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Settings/settings.h>

namespace TFE_Jedi
{
	static u8  s_frameBuffer320x200[320 * 200];
	static u8* s_frameBuffer = nullptr;
	static u8* s_curFrameBuffer = nullptr;

	static u32 s_width  = 0;
	static u32 s_height = 0;
	static u32 s_prevWidth = 0;
	static u32 s_prevHeight = 0;
	static s32 s_widescreenOffset = 0;
	static s32 s_supersampling = 1;
	static bool s_widescreen = false;

	static u32 s_palette[256];

	static fixed16_16 s_xScale = ONE_16;
	static fixed16_16 s_yScale = ONE_16;

	static ScreenRect s_screenRect[VFB_RECT_COUNT];
	static ScreenRect s_screenPrev[VFB_RECT_COUNT];

	static FramebufferMode s_mode = VFB_TEXTURE;
	static FramebufferMode s_nextMode = VFB_TEXTURE;

	void vfb_createVirtualDisplay(u32 width, u32 height);
		
	////////////////////////////////////////////////////////////////////////
	// Setup
	// -----------------
	// Note that the current graphics settings are
	// used internally when setting up (GPU palette conversion,etc.)
	////////////////////////////////////////////////////////////////////////
	JBool vfb_setResolution(u32 width, u32 height)
	{
		TFE_Settings_Graphics* graphics = TFE_Settings::getGraphicsSettings();
		const s32 supersampling = s_nextMode == VFB_RENDER_TRAGET ? graphics->supersampling : 1;
		if (width == s_width && height == s_height && s_widescreen == graphics->widescreen && s_supersampling == supersampling && s_mode == s_nextMode)
		{
			return JFALSE;
		}
		s_widescreen = graphics->widescreen;
		s_supersampling = supersampling;
		s_mode = s_nextMode;

		if (width == 320 && height == 200)
		{
			s_width = width;
			s_height = height;

			s_curFrameBuffer = s_frameBuffer320x200;
			vfb_createVirtualDisplay(width, height);

			s_xScale = ONE_16;
			s_yScale = ONE_16;
			s_widescreenOffset = 0;
		}
		else
		{
			s_width = width;
			s_height = height;
			s_prevWidth = s_width;
			s_prevHeight = s_height;

			free(s_frameBuffer);
			s_frameBuffer = (u8*)malloc(s_width * s_height);
			s_curFrameBuffer = s_frameBuffer;
			
			vfb_createVirtualDisplay(width, height);

			// Square or rectangular pixels?
			if (s_height == 200 || s_height == 400)
			{
				// Rectangular pixels
				s_yScale = div16(intToFixed16(s_height), intToFixed16(200));
				s_xScale = s_yScale;

				if (s_height == 200)
				{
					s_widescreenOffset = (s_width - 320) / 2;
				}
				else if (s_height == 400)
				{
					s_widescreenOffset = (s_width - 640) / 2;
				}
			}
			else
			{
				// Square pixels
				s_yScale = div16(intToFixed16(s_height), intToFixed16(200));
				// yScale / 1.2
				s_xScale = div16(s_yScale, 78643);

				s_widescreenOffset = max(0, ((s32)s_width - (s32)s_height*4/3) / 2);
			}
		}
		memset(s_curFrameBuffer, 0, s_width * s_height);

		s_screenRect[VFB_RECT_UI] =
		{
			0,
			0,
			s32(s_width) - 1,
			s32(s_height) - 1,
		};

		s_screenRect[VFB_RECT_RENDER] =
		{
			0,
			1,
			s32(s_width) - 1,
			s32(s_height) - 2,
		};

		// Avoid flashing the previous buffer when swapping.
		vfb_swap();
		vfb_swap();

		return JTRUE;
	}
		
	u32* vfb_getPalette()
	{
		return s_palette;
	}

	void vfb_setPalette(const u32* palette)
	{
		memcpy(s_palette, palette, sizeof(u32) * 256);
		TFE_RenderBackend::setPalette(palette);
	}

	void vfb_setMode(FramebufferMode mode)
	{
		s_nextMode = mode;
	}

	////////////////////////////
	// Get Scale Factors
	////////////////////////////
	fixed16_16 vfb_getXScale()
	{
		return s_xScale;
	}

	fixed16_16 vfb_getYScale()
	{
		return s_yScale;
	}

	s32 vfb_getWidescreenOffset()
	{
		return s_widescreenOffset;
	}

	void vfb_forceToBlack()
	{
		if (s_mode == VFB_TEXTURE)
		{
			memset(vfb_getCpuBuffer(), 0, s_width * s_height);
			// Black for two frames.
			vfb_swap();
			vfb_swap();
		}
		else
		{
			f32 color[] = { 0.0f, 0.0f, 0.0f, 1.0f };
			TFE_RenderBackend::clearVirtualDisplay(color);
		}
	}

	void vfb_bindRenderTarget(bool clearColor)
	{
		TFE_RenderBackend::bindVirtualDisplay();

		// For now, in the future disable again (but clear depth).
		f32 color[] = { 0.0f, 0.0f, 0.0f, 1.0f };
		TFE_RenderBackend::clearVirtualDisplay(color, clearColor);
	}

	void vfb_unbindRenderTarget()
	{
		TFE_RenderBackend::unbindRenderTarget();
	}

	////////////////////////////
	// Per-Frame
	////////////////////////////
	// Frame rendering is done, copy the results to GPU memory.
	void vfb_swap()
	{
		TFE_RenderBackend::updateVirtualDisplay(s_curFrameBuffer, s_width * s_height);
	}

	////////////////////////////
	// Query
	////////////////////////////
	// Get the CPU buffer for rendering.
	u8* vfb_getCpuBuffer()
	{
		return s_curFrameBuffer;
	}
		
	// Set the clip rect.
	void vfb_setScreenRect(ScreenRectType type, ScreenRect* rect)
	{
		s_screenPrev[type] = s_screenRect[type];
		s_screenRect[type] = *rect;
	}

	void vfb_restoreScreenRect(ScreenRectType type)
	{
		s_screenRect[type] = s_screenPrev[type];
	}

	// Get the valid screen rect for the current mode.
	ScreenRect* vfb_getScreenRect(ScreenRectType type)
	{
		return &s_screenRect[type];
	}

	// Returns 'true' if using square pixels, otherwise 'false'.
	void vfb_getResolution(u32* width, u32* height)
	{
		*width  = s_width;
		*height = s_height;
	}

	// Returns the stride for rendering stride
	u32 vfb_getStride()
	{
		return s_width;
	}

	////////////////////////////
	// Internal
	////////////////////////////
	void vfb_createVirtualDisplay(u32 width, u32 height)
	{
		// Setup or update the virtual display.
		TFE_Settings_Graphics* graphics = TFE_Settings::getGraphicsSettings();
		u32 vdispFlags = 0;
		if (graphics->asyncFramebuffer)
		{
			vdispFlags |= VDISP_ASYNC_FRAMEBUFFER;
		}
		if (graphics->gpuColorConvert)
		{
			vdispFlags |= VDISP_GPU_COLOR_CONVERT;
		}
		if (graphics->widescreen && (width != 320 || height != 200))
		{
			vdispFlags |= VDISP_WIDESCREEN;
		}
		if (s_mode == VFB_RENDER_TRAGET)
		{
			vdispFlags |= VDISP_RENDER_TARGET;
		}

		// TFE Specific: always use 320x200 when rendering in-game UI (for now).
		VirtualDisplayInfo vdisp =
		{
			DMODE_ASPECT_CORRECT,	// Output display mode.
			vdispFlags,				// See VirtualDisplayFlags.

			width,	// full width
			height,	// full height
			width,	// width for 2D game UI and cutscenes.
			height,	// width for 3D drawing.
		};
		TFE_RenderBackend::createVirtualDisplay(vdisp);
	}
}  // namespace TFE_Jedi
