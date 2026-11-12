#include <cstring>

#include "missionBriefing.h"
#include "menu.h"
#include "pda.h"
#include "uiDraw.h"
#include <TFE_DarkForces/Landru/lactorDelt.h>
#include <TFE_DarkForces/Landru/lactorAnim.h>
#include <TFE_DarkForces/Landru/lpalette.h>
#include <TFE_DarkForces/Landru/lcanvas.h>
#include <TFE_DarkForces/Landru/ldraw.h>
#include <TFE_DarkForces/Landru/highResActor.h>
#include <TFE_Archive/lfdArchive.h>
#include <TFE_DarkForces/agent.h>
#include <TFE_DarkForces/util.h>
#include <TFE_Archive/archive.h>
#include <TFE_Settings/settings.h>
#include <TFE_Input/input.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Renderer/jediRenderer.h>
#include <TFE_Jedi/Renderer/RClassic_GPU/screenDrawGPU.h>
#include <TFE_System/system.h>
#include <TFE_Jedi/Renderer/virtualFramebuffer.h>

using namespace TFE_Jedi;

namespace TFE_DarkForces
{
	///////////////////////////////////////////
	// Constants
	///////////////////////////////////////////
	enum BriefingButton
	{
		BRIEF_BTN_OK = 0,
		BRIEF_BTN_UP,
		BRIEF_BTN_DOWN,
		BRIEF_BTN_CANCEL,
		BRIEF_BTN_EASY,
		BRIEF_BTN_MEDIUM,
		BRIEF_BTN_HARD,
		BRIEF_BTN_COUNT,
	};
	static s32 s_keyPressed = -1;
	static JBool s_briefingOpen = JFALSE;
	static s32 s_skill = 0;
	static LRect s_briefRect = { 25, 15, 155, 305 };
	static LRect s_missionTextRect;
	static LRect s_viewBounds;
	static LActor* s_briefActor = nullptr;
	static LActor* s_menuActor = nullptr;
	static LPalette* s_palette = nullptr;
	static u8* s_framebuffer = nullptr;
	static LangHotkeys* s_langKeys;

	// High res graphics
	static TextureGpu* s_brfHighResTex;
	static u32 s_highResBitmap[640 * 400];
	static HighResActor* s_briefHigh = nullptr;
	static HighResActor* s_menuHigh = nullptr;

	s16 s_briefY;
	s32 s_briefingMaxY;
	LRect s_overlayRect;

	enum
	{
		MENU_TYPE_BACK = 200,
		MENU_TYPE_OVERLAY = 201,
	};

	static const s16 c_frameTypes[] =
	{
		MENU_TYPE_BACK,
		MENU_TYPE_OVERLAY,
		-1, //		MENU_TYPE_BUTTON,
		-1, //		MENU_TYPE_BUTTON_UP,
		-1, //		MENU_TYPE_REPEAT_BUTTON,
		-1, //		MENU_TYPE_BUTTON_UP,
		-1, //		MENU_TYPE_REPEAT_BUTTON,
		-1, //		MENU_TYPE_BUTTON_UP,
		-1, //		MENU_TYPE_BUTTON,
		-1, //		MENU_TYPE_BUTTON_UP,
		-1, //		MENU_TYPE_BUTTON,
		-1, //		MENU_TYPE_BUTTON_UP,
		-1, //		MENU_TYPE_BUTTON,
		-1, //		MENU_TYPE_BUTTON_UP,
		-1, //		MENU_TYPE_BUTTON,
		-1, //		MENU_TYPE_BUTTON_UP,
	};

	///////////////////////////////////////////
	// Forward Declarations
	///////////////////////////////////////////
	void missionBriefing_drawHighRes();

	///////////////////////////////////////////
	// API Implementation
	///////////////////////////////////////////
	void missionBriefing_start(const char* archive, const char* bgAnim, const char* mission, const char* palette, s32 skill, LangHotkeys* langKeys)
	{
		s_langKeys = langKeys;

		menu_init();
		menu_startupDisplay();

		s_briefingOpen = JFALSE;
		s_skill = skill;

		if (!menu_openResourceArchive(archive))
		{
			// Try the dfbrief.lfd file.
			if (!menu_openResourceArchive("dfbrief.lfd"))
			{
				return;
			}
		}

		// Mission specific text and images.
		s_briefActor = lactorDelt_load(mission, &s_briefRect, 0, 0, 0);
		if (!s_briefActor)
		{
			menu_closeResourceArchive();
			return;
		}

		// Menu Items
		LRect bounds;
		lcanvas_getBounds(&bounds);
		s_menuActor = lactorAnim_load(bgAnim, &bounds, 0, 0, 0);
		lactor_setTime(s_menuActor, -1, -1);
		s_palette = lpalette_load(palette);
		lpalette_setScreenPal(s_palette);

		if (!s_menuActor)
		{
			menu_closeResourceArchive();
			s_briefingOpen = JFALSE;
			s_skill = 1;
			vfb_forceToBlack();
			lcanvas_clear();
			return;
		}

		// High res assets
		bool highRes = TFE_Settings::getEnhancementsSettings()->enableHdPda &&
			TFE_Settings::getGraphicsSettings()->colorMode == COLORMODE_TRUE_COLOR;

		if (highRes)
		{
			s_brfHighResTex = TFE_RenderBackend::createTexture(640, 400, TEX_RGBA8);
			s_briefHigh = highResActor_loadFromPng("dfbrief", mission, 1, false);
			s_menuHigh = highResActor_loadFromPng("dfbrief", bgAnim, s_menuActor->arraySize);
			TFE_Jedi::renderer_setType(RENDERER_HARDWARE);
			TFE_RenderBackend::bloomPostEnable(false); // disable bloom in briefing screen
		}

		s16 state_btn_index = 600;
		s16 button_index = 0;
		s16 slider_index = 0;
		s16 back_state = -1;
		s16 overlay_state = -1;
			   
		// Buttons
		const s32 count = s_menuActor ? s_menuActor->arraySize : 0;
		for (s32 i = 0; i < count; i++)
		{
			LRect rect;
			lactor_setState(s_menuActor, i, 0);
			lactor_getFrame(s_menuActor, &rect);

			if (c_frameTypes[i] == MENU_TYPE_BACK)
			{
				back_state = i;
			}
			else if (c_frameTypes[i] == MENU_TYPE_OVERLAY)
			{
				overlay_state = i;
				lactorAnim_getFrame(s_menuActor, &s_missionTextRect);
			}
		}
		menu_closeResourceArchive();
		
		LRect rect;
		lactor_setTime(s_briefActor, -1, -1);
		lactorDelt_getFrame(s_briefActor, &rect);

		s_briefingMaxY = (rect.bottom - rect.top) - (s_briefRect.bottom - s_briefRect.top) + BRIEF_VERT_MARGIN;
		// Round to a factor of the line scrolling value.
		s_briefingMaxY = s_briefingMaxY + BRIEF_LINE_SCROLL - (s_briefingMaxY % BRIEF_LINE_SCROLL);

		if (s_briefingMaxY < -BRIEF_VERT_MARGIN)
		{
			s_briefingMaxY = -BRIEF_VERT_MARGIN;
		}
		s_overlayRect = s_briefRect;
		s_briefY = -BRIEF_VERT_MARGIN;

		s_briefingOpen = JTRUE;

		s_framebuffer = ldraw_getBitmap();
		lcanvas_getBounds(&s_viewBounds);

		ltime_setFrameDelay(20);
	}

	void missionBriefing_cleanup()
	{
		lactor_removeActor(s_briefActor);
		lactor_removeActor(s_menuActor);

		lactor_free(s_briefActor);
		lactor_free(s_menuActor);
		lpalette_free(s_palette);

		s_briefActor = nullptr;
		s_menuActor = nullptr;
		s_palette = nullptr;

		// High res
		highResActor_free(s_briefHigh);
		highResActor_free(s_menuHigh);
		s_briefHigh = nullptr;
		s_menuHigh = nullptr;
		s_brfHighResTex = nullptr;
	}
		
	void drawButton(BriefingButton id)
	{
		s32 pressed = 0;
		if ((s_buttonHover && id == s_buttonPressed) || (id == s_keyPressed))
		{
			pressed = 1;
		}
		else if (id >= BRIEF_BTN_EASY && s_skill == id - BRIEF_BTN_EASY)
		{
			pressed = 1;
		}

		lactor_setState(s_menuActor, 2*(1+id) + (pressed ? 0 : 1), 0);
		lactorAnim_draw(s_menuActor, &s_viewBounds, &s_viewBounds, 0, 0, JTRUE);
	}

	void missionBriefing_scroll(s32 amt)
	{
		if (amt < 0 && s_briefY > -BRIEF_VERT_MARGIN)
		{
			s_briefY += amt;
			if (s_briefY < -BRIEF_VERT_MARGIN) { s_briefY = -BRIEF_VERT_MARGIN; }
		}
		else if (amt > 0 && s_briefY != s_briefingMaxY)
		{
			s_briefY += amt;
			if (s_briefY > s_briefingMaxY) { s_briefY = s_briefingMaxY; }
		}
	}
		
	JBool missionBriefing_handleInput(JBool* abort)
	{
		JBool exitBriefing = JFALSE;

		// Add support for mouse wheel scrolling.
		s32 wdx, wdy;
		TFE_Input::getMouseWheel(&wdx, &wdy);
		
		// Mouse interactions.
		menu_handleMousePosition();
		if (TFE_Input::mousePressed(MBUTTON_LEFT))
		{
			s_buttonPressed = -1;
			for (s32 i = 0; i < BRIEF_BTN_COUNT; i++)
			{
				lactor_setState(s_menuActor, 2 * (1 + i), 0);
				LRect buttonRect;
				lactorAnim_getFrame(s_menuActor, &buttonRect);

				if (s_cursorPos.x >= buttonRect.left && s_cursorPos.x < buttonRect.right &&
					s_cursorPos.z >= buttonRect.top && s_cursorPos.z < buttonRect.bottom)
				{
					s_buttonPressed = s32(i);
					s_buttonHover = JTRUE;
					break;
				}
			}
		}
		else if (TFE_Input::mouseDown(MBUTTON_LEFT) && s_buttonPressed >= 0)
		{
			lactor_setState(s_menuActor, 2 * (1 + s_buttonPressed), 0);
			LRect buttonRect;
			lactorAnim_getFrame(s_menuActor, &buttonRect);

			// Verify that the mouse is still over the button.
			if (s_cursorPos.x >= buttonRect.left && s_cursorPos.x < buttonRect.right &&
				s_cursorPos.z >= buttonRect.top && s_cursorPos.z < buttonRect.bottom)
			{
				s_buttonHover = JTRUE;
				if (ltime_isFrameReady())
				{
					if (s_buttonPressed == BRIEF_BTN_UP)
					{
						missionBriefing_scroll(-BRIEF_LINE_SCROLL);
					}
					else if (s_buttonPressed == BRIEF_BTN_DOWN)
					{
						missionBriefing_scroll(BRIEF_LINE_SCROLL);
					}
				}
			}
			else
			{
				s_buttonHover = JFALSE;
			}
		}
		else
		{
			// Mouse wheel support.
			if (wdy > 0) // up
			{
				missionBriefing_scroll(-BRIEF_LINE_SCROLL * wdy);
			}
			else if (wdy < 0) // down
			{
				missionBriefing_scroll(-BRIEF_LINE_SCROLL * wdy);
			}

			if (s_buttonPressed >= 0 && s_buttonHover)
			{
				switch (s_buttonPressed)
				{
					case BRIEF_BTN_OK:
					{
						*abort = JFALSE;
						exitBriefing = JTRUE;
					} break;
					case BRIEF_BTN_UP:
					{
						missionBriefing_scroll(-BRIEF_LINE_SCROLL);
					} break;
					case BRIEF_BTN_DOWN:
					{
						missionBriefing_scroll(BRIEF_LINE_SCROLL);
					} break;
					case BRIEF_BTN_CANCEL:
					{
						*abort = JTRUE;
						exitBriefing = JTRUE;
					} break;
					case BRIEF_BTN_EASY:
					{
						s_skill = 0;
					} break;
					case BRIEF_BTN_MEDIUM:
					{
						s_skill = 1;
					} break;
					case BRIEF_BTN_HARD:
					{
						s_skill = 2;
					} break;
				}
			}
			// Reset.
			s_buttonPressed = -1;
			s_buttonHover = JFALSE;
		}

		// Keyboard shortcuts.

		// These need to be timer limited so that the scrolling works correctly.
		if (ltime_isFrameReady())
		{
			s_keyPressed = -1;
			if (TFE_Input::keyDown(KEY_UP))
			{
				s_keyPressed = BRIEF_BTN_UP;
				missionBriefing_scroll(-BRIEF_LINE_SCROLL);
			}
			else if (TFE_Input::keyDown(KEY_DOWN))
			{
				s_keyPressed = BRIEF_BTN_DOWN;
				missionBriefing_scroll(BRIEF_LINE_SCROLL);
			}
			
			if (TFE_Input::keyDown(KEY_PAGEUP))
			{
				missionBriefing_scroll(-BRIEF_PAGE_SCROLL);
			}
			else if (TFE_Input::keyDown(KEY_PAGEDOWN))
			{
				missionBriefing_scroll(BRIEF_PAGE_SCROLL);
			}
		}

		if (TFE_Input::keyPressed(s_langKeys->k_easy))
		{
			s_keyPressed = BRIEF_BTN_EASY;
			s_skill = 0;
		}
		else if (TFE_Input::keyPressed(s_langKeys->k_med))
		{
			s_keyPressed = BRIEF_BTN_MEDIUM;
			s_skill = 1;
		}
		else if (TFE_Input::keyPressed(s_langKeys->k_hard))
		{
			s_keyPressed = BRIEF_BTN_HARD;
			s_skill = 2;
		}

		if (TFE_Input::keyPressed(s_langKeys->k_canc) || TFE_Input::keyPressed(KEY_ESCAPE))
		{
			*abort = JTRUE;
			exitBriefing = JTRUE;
		}
		else if (TFE_Input::keyPressed(KEY_O) || TFE_Input::keyPressed(KEY_RETURN))
		{
			*abort = JFALSE;
			exitBriefing = JTRUE;
		}

		return exitBriefing;
	}

	JBool missionBriefing_update(s32* skill, JBool* abort)
	{
		if (!s_briefingOpen)
		{
			*skill = s_skill;
			*abort = JFALSE;
			return JFALSE;
		}
		tfe_updateLTime();

		// Input
		if (missionBriefing_handleInput(abort))
		{
			s_briefingOpen = JFALSE;
			*skill = s_skill;
			vfb_forceToBlack();
			lcanvas_clear();
			return JFALSE;
		}

		bool highRes = TFE_Settings::getEnhancementsSettings()->enableHdPda &&
			TFE_Settings::getGraphicsSettings()->colorMode == COLORMODE_TRUE_COLOR;

		if (highRes && TFE_Jedi::renderer_getType() == RENDERER_HARDWARE)
		{
			memset(s_highResBitmap, 0, sizeof(s_highResBitmap));
			missionBriefing_drawHighRes();
			return JTRUE;
		}

		// Background
		lcanvas_eraseRect(&s_viewBounds);
		lactor_setState(s_menuActor, 0, 0);
		lactorAnim_draw(s_menuActor, &s_viewBounds, &s_viewBounds, 0, 0, JTRUE);

		// Buttons
		for (s32 i = 0; i < BRIEF_BTN_COUNT; i++)
		{
			drawButton(BriefingButton(i));
		}

		// Briefing Text.
		LRect rect = s_missionTextRect;
		s16 diff = ((rect.right - rect.left) - s_briefActor->w) >> 1;
		s16 x = diff + rect.left - s_briefActor->x;
		s16 y = (rect.top - s_briefActor->y) - s_briefY;

		lcanvas_setClip(&s_missionTextRect);
		lactorDelt_draw(s_briefActor, &rect, &s_missionTextRect, x, y, JTRUE);
		lcanvas_clearClipRect();

		menu_blitCursor(s_cursorPos.x, s_cursorPos.z, s_framebuffer);
		menu_blitToScreen();
		return JTRUE;
	}
	
	///////////////////////////////////////////
	// Internal Implementation
	///////////////////////////////////////////

	//////////////////////
	// High res mode
	//////////////////////

	void missionBriefing_copyHighResImageToBitmap(LActor* lactor, HighResActor* hiResActor)
	{
		s16 index = lactor->state;

		// This may happen if any PNGs failed to load
		if (!hiResActor || hiResActor->arraySize <= index)
		{
			return;
		}

		u32 width = min((u32)lactor->w * 2, hiResActor->imageWidths[index]);
		u32 height = min((u32)lactor->h * 2, hiResActor->imageHeights[index]);
		s16* deltData = (s16*)lactor->array[index];
		s16 xOffset = deltData[0] * 2;
		s16 yOffset = deltData[1] * 2;

		for (s32 y = 0; y < height; y++)
		{
			for (s32 x = 0; x < width; x++)
			{
				u32 srcPixel = hiResActor->array[index][y * hiResActor->imageWidths[index] + x];
				u32 dstPixel = s_highResBitmap[(yOffset + y) * 640 + xOffset + x];
				s_highResBitmap[(yOffset + y) * 640 + xOffset + x] = pda_blendRgbPixels(srcPixel, dstPixel);
			}
		}
	}

	void missionBriefing_copyHighResBriefingToBitmap()
	{
		if (!s_briefHigh || s_briefHigh->arraySize < 1)
		{
			return;
		}

		// We have to add 1 here to get the correct dimensions, see the DELT format
		s32 origWidth = s_briefActor->w + 1;
		s32 origHeight = s_briefActor->h + 1;

		s32 margin = (s_missionTextRect.right - s_missionTextRect.left - origWidth) >> 1;
		s32 xOffset = (margin + s_missionTextRect.left) * 2;
		s32 yOffset = s_missionTextRect.top * 2;
		u32 width = min((u32)origWidth * 2, s_briefHigh->imageWidths[0]);
		u32 height = min((u32)origHeight * 2, s_briefHigh->imageHeights[0]);
		s32 clipTop = min((u32)s_briefY * 2, height);
		s32 clipBot = min((u32)(s_missionTextRect.bottom - s_missionTextRect.top + s_briefY) * 2, height);

		for (s32 y = clipTop; y < clipBot; y++)
		{
			for (s32 x = 0; x < width; x++)
			{
				u32 srcPixel = s_briefHigh->array[0][y * s_briefHigh->imageWidths[0] + x];
				u32 dstPixel = s_highResBitmap[(yOffset + y - clipTop) * 640 + xOffset + x];
				s_highResBitmap[(yOffset + y - clipTop) * 640 + xOffset + x] = pda_blendRgbPixels(srcPixel, dstPixel);
			}
		}
	}

	void missionBriefing_drawHighResButton(BriefingButton id)
	{
		s32 pressed = 0;
		if ((s_buttonHover && id == s_buttonPressed) || (id == s_keyPressed))
		{
			pressed = 1;
		}
		else if (id >= BRIEF_BTN_EASY && s_skill == id - BRIEF_BTN_EASY)
		{
			pressed = 1;
		}

		lactor_setState(s_menuActor, 2 * (1 + id) + (pressed ? 0 : 1), 0);
		missionBriefing_copyHighResImageToBitmap(s_menuActor, s_menuHigh);
	}

	void missionBriefing_drawHighRes()
	{
		ScreenRect* uiRect = vfb_getScreenRect(VFB_RECT_UI);
		fixed16_16 xScale = vfb_getXScale();
		fixed16_16 yScale = vfb_getYScale();
		s32 uiWidth = uiRect->right - uiRect->left + 1;
		s32 virtualWidth = floor16(mul16(intToFixed16(320), xScale));
		s32 virtualHeight = floor16(mul16(intToFixed16(200), yScale));
		s32 xOffset = max(0, (uiWidth - virtualWidth) / 2);

		// Background
		lactor_setState(s_menuActor, 0, 0);
		missionBriefing_copyHighResImageToBitmap(s_menuActor, s_menuHigh);

		// Buttons
		for (s32 i = 0; i < BRIEF_BTN_COUNT; i++)
		{
			missionBriefing_drawHighResButton(BriefingButton(i));
		}

		// Briefing text
		missionBriefing_copyHighResBriefingToBitmap();

		// Cursor (hack)
		TextureData* cursorTex = &s_cursor.texture;
		for (s32 y = 0; y < cursorTex->height; y++)
		{
			u32 yPos = s_cursorPos.z * 2 + y;
			if (yPos >= 400) { continue; }

			for (s32 x = 0; x < cursorTex->width; x++)
			{
				u32 xPos = s_cursorPos.x * 2 + x;
				if (xPos >= 640) { continue; }

				u8 pixel = cursorTex->image[y * cursorTex->width + x];
				if (pixel == 0) { continue; }
				u32 r = s_palette->colors[pixel].r << 2;
				u32 g = s_palette->colors[pixel].g << 2;
				u32 b = s_palette->colors[pixel].b << 2;
				s_highResBitmap[yPos * 640 + xPos] = r | (g << 8u) | (b << 16u) | (0xff << 24);
			}
		}

		// Draw the bitmap
		TFE_Jedi::beginRender();
		s_brfHighResTex->update(s_highResBitmap, sizeof(s_highResBitmap));
		vfb_forceToBlack();
		screenGPU_addImageQuad(xOffset, 0, xOffset + virtualWidth, virtualHeight, s_brfHighResTex);
		TFE_Jedi::endRender();
	}
}