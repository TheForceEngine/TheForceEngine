#include <cstring>
#include "pda.h"
#include "menu.h"
#include "missionBriefing.h"
#include <TFE_DarkForces/Landru/lactorDelt.h>
#include <TFE_DarkForces/Landru/lactorAnim.h>
#include <TFE_DarkForces/Landru/lpalette.h>
#include <TFE_DarkForces/Landru/lcanvas.h>
#include <TFE_DarkForces/Landru/ldraw.h>
#include <TFE_DarkForces/Landru/lfont.h>
#include <TFE_DarkForces/automap.h>
#include <TFE_DarkForces/player.h>
#include <TFE_DarkForces/hud.h>
#include <TFE_Archive/lfdArchive.h>
#include <TFE_DarkForces/agent.h>
#include <TFE_DarkForces/util.h>
#include <TFE_Game/reticle.h>
#include <TFE_Archive/archive.h>
#include <TFE_Settings/settings.h>
#include <TFE_Input/input.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/Renderer/virtualFramebuffer.h>
#include <TFE_Jedi/Renderer/screenDraw.h>
#include <TFE_Jedi/Level/levelData.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Level/level.h>
#include <TFE_Jedi/Level/rfont.h>
#include <TFE_Jedi/Renderer/jediRenderer.h>
#include <TFE_Settings/settings.h>
#include <TFE_Input/replay.h>
#include <TFE_Input/inputMapping.h>

using namespace TFE_Jedi;
using namespace TFE_Input;

namespace TFE_DarkForces
{
	///////////////////////////////////////////
	// Constants
	///////////////////////////////////////////
	enum PdaMode
	{
		PDA_MODE_MAP = 0,
		PDA_MODE_WEAPONS,
		PDA_MODE_INV,
		PDA_MODE_GOALS,
		PDA_MODE_BRIEF,
		PDA_MODE_COUNT
	};

	enum PdaButton
	{
		PDA_BTN_MAP = 0,
		PDA_BTN_WEAPONS,
		PDA_BTN_INV,
		PDA_BTN_GOALS,
		PDA_BTN_BRIEF,
		PDA_BTN_EXIT,

		PDA_BTN_PANUP,
		PDA_BTN_PANRIGHT,
		PDA_BTN_PANDOWN,
		PDA_BTN_PANLEFT,
		PDA_BTN_ZOOMOUT,
		PDA_BTN_ZOOMIN,
		PDA_BTN_LAYERUP,
		PDA_BTN_LAYERDOWN,

		PDA_BTN_COUNT,
		PDA_RADIO_BTN_COUNT = PDA_BTN_BRIEF - PDA_BTN_MAP + 1
	};

	enum PdaFrames
	{
		PFRAME_BACK = 0,
		PFRAME_SECRET_LABEL = 30,
		PFRAME_SECRET_BOX = 31,
	};

	///////////////////////////////////////////
	// State
	///////////////////////////////////////////
	static JBool s_pdaOpen   = JFALSE;
	static JBool s_pdaLoaded = JFALSE;
	static LActor* s_briefing = nullptr;
	static LActor* s_pdaArt  = nullptr;
	static LActor* s_goalsActor   = nullptr;
	static LActor* s_weapons = nullptr;
	static LActor* s_items   = nullptr;
	static LPalette* s_palette = nullptr;
	static LRect s_pdaRect   = { 12, 15, 168, 305 };
	static LRect s_viewBounds;
	static LRect s_overlayRect;
	static u8* s_framebuffer;

	static JBool s_frameReady = true;
	static Vec2i s_mouseAccum = { 0 };

	static PdaMode s_pdaMode = PDA_MODE_MAP;
	static s32 s_simulatePressed = -1;

	static s32 s_briefingMaxY;
	static s16 s_briefY;
	static s32 mouseOldPosX = 0, mouseOldPosZ = 0;
	static bool mouseMoveReset = true;
		
	void pda_handleInput();
	void pda_drawCommonButtons();
	void pda_drawMapButtons();
	void pdaDrawBriefingButtons();
	void pda_drawOverlay();
	void pda_clearToBlack();
	void pda_displayFontString(Font* font, s32 x, s32 y, char* msg);

	extern void pauseLevelSound();
	// From DarkForcesMain
	// TODO: Refactor.
	extern Font* getMapNumFont();
		
	///////////////////////////////////////////
	// API Implementation
	///////////////////////////////////////////
	void pda_start(const char* levelName)
	{
		// TFE
		reticle_enable(false);
		s_mouseAccum = { 0 };
		mouseOldPosX = 0, mouseOldPosZ = 0;
		pauseLevelSound();
		if (!s_pdaLoaded)
		{
			if (!menu_openResourceArchive("dfbrief.lfd"))
			{
				return;
			}
			LRect rect;

			s_briefing = lactorDelt_load(levelName, &s_pdaRect, 0, 0, 0);
			s_goalsActor = lactorAnim_load(levelName, &rect, 0, 0, 0);
			s_weapons  = lactorAnim_load("guns",  &rect, 0, 0, 0);
			s_items    = lactorAnim_load("items", &rect, 0, 0, 0);
			menu_closeResourceArchive();

			lactor_setTime(s_goalsActor, -1, -1);
			lactor_setTime(s_weapons, -1, -1);
			lactor_setTime(s_items,   -1, -1);

			if (s_briefing)
			{
				lactor_setTime(s_briefing, -1, -1);
				lactorDelt_getFrame(s_briefing, &rect);

				s_briefingMaxY = (rect.bottom - rect.top) - (s_pdaRect.bottom - s_pdaRect.top) + BRIEF_VERT_MARGIN;
				// Round to a factor of the line scrolling value.
				s_briefingMaxY = s_briefingMaxY + BRIEF_LINE_SCROLL - (s_briefingMaxY % BRIEF_LINE_SCROLL);

				if (s_briefingMaxY < -BRIEF_VERT_MARGIN)
				{
					s_briefingMaxY = -BRIEF_VERT_MARGIN;
				}
				s_briefY = -BRIEF_VERT_MARGIN;
				
			}
			s_overlayRect = s_pdaRect;
			
			if (!menu_openResourceArchive("menu.lfd"))
			{
				return;
			}
			s_framebuffer = ldraw_getBitmap();
			lcanvas_getBounds(&s_viewBounds);
				
			s_pdaArt = lactorAnim_load("pda", &s_viewBounds, 0, 0, 0);
			s_palette = lpalette_load("menu");
			lactor_setTime(s_pdaArt, -1, -1);
			
			menu_closeResourceArchive();

			s_pdaLoaded = JTRUE;
		}
		lpalette_setScreenPal(s_palette);

		s_pdaOpen = JTRUE;
		s_buttonPressed = -1;
		s_simulatePressed = -1;
		s_buttonHover = JFALSE;
		automap_updateMapData(MAP_CENTER_PLAYER);
		automap_updateMapData(MAP_ENABLE_AUTOCENTER);
		automap_resetScale();
		ltime_setFrameDelay(10);

		TFE_Input::endFrame();
	}

	void pda_cleanup()
	{
		lactor_removeActor(s_briefing);
		lactor_removeActor(s_pdaArt);
		lactor_removeActor(s_goalsActor);
		lactor_removeActor(s_weapons);
		lactor_removeActor(s_items);

		lactor_free(s_briefing);
		lactor_free(s_pdaArt);
		lactor_free(s_goalsActor);
		lactor_free(s_weapons);
		lactor_free(s_items);
		lpalette_free(s_palette);

		pda_resetState();
	}

	// Reset the tab to the map to support replay consistency. 
	void pda_resetTab()
	{
		s_pdaMode = PDA_MODE_MAP;
	}

	void pda_resetState()
	{
		s_pdaOpen   = JFALSE;
		s_pdaLoaded = JFALSE;
		s_briefing   = nullptr;
		s_pdaArt     = nullptr;
		s_goalsActor = nullptr;
		s_weapons    = nullptr;
		s_items      = nullptr;
		s_palette    = nullptr;
	}

	JBool pda_isOpen()
	{
		return s_pdaOpen;
	}
		
	void pda_close()
	{
		// Clear the screen to black during the palette transition.
		pda_clearToBlack();

		// Reset automap
		automap_updateMapData(MAP_ENABLE_AUTOCENTER);
		s_pdaOpen = JFALSE;

		// TFE
		reticle_enable(true);

		// Convert back to level rendering.
		TFE_Settings_Graphics* graphics = TFE_Settings::getGraphicsSettings();
		if (graphics->rendererIndex != RENDERER_SOFTWARE)
		{
			TFE_Jedi::renderer_setType(RendererType(graphics->rendererIndex));
			TFE_Jedi::renderer_setLimits();
		}

	}
			
	void pda_update()
	{
		if (!s_pdaOpen)
		{
			return;
		}
		
		// Special case to support Escape during demo playback
		if (inputMapping_getActionState(IADF_PDA_TOGGLE) == STATE_PRESSED 
			|| TFE_Input::keyPressed(KEY_ESCAPE) 
			|| inputMapping_getAction(IADF_MENU_TOGGLE) == STATE_PRESSED)
		{
			pda_close();
			return;
		}
		else if (TFE_Jedi::renderer_getType() != RENDERER_SOFTWARE)
		{
			// Convert back to CPU rendering.
			TFE_Jedi::renderer_setType(RENDERER_SOFTWARE);
			TFE_Jedi::renderer_setLimits();
		}
		
		tfe_updateLTime();
		s_frameReady = ltime_isFrameReady();

		// Input
		pda_handleInput();

		// Main view
		u32 outWidth, outHeight;
		vfb_getResolution(&outWidth, &outHeight);
		memset(vfb_getCpuBuffer(), 0, outWidth * outHeight);
		
		// Draw the overlay *behind* the main view - which means we must blit it to the view.
		pda_drawOverlay();
		
		// Finally draw the PDA background and UI controls.
		lcanvas_eraseRect(&s_viewBounds);
		lactor_setState(s_pdaArt, 0, 0);
		lactorAnim_draw(s_pdaArt, &s_viewBounds, &s_viewBounds, 0, 0, JTRUE);
				
		// Common buttons
		pda_drawCommonButtons();
		if (s_pdaMode == PDA_MODE_MAP)
		{
			pda_drawMapButtons();

			// Draw the floor number
			Font* mapNumFont = getMapNumFont();
			if (mapNumFont)
			{
				char str[10];
				memset(str, 0, 10);	// just to be safe
				snprintf(str, 10, "%d", automap_getLayer());
				// This shows up as an 'S' in the font.
				if (str[0] == '-') { str[0] = ':'; }
				// In TFE the map covers the whole screen, so no additional offset is needed.
				s32 leftAdj = font_getStringLength(mapNumFont, str) / 2;
				pda_displayFontString(mapNumFont, 275 - leftAdj, 127, str);
			}
		}
		else if (s_pdaMode == PDA_MODE_BRIEF)
		{
			pdaDrawBriefingButtons();
		}

		// Blit the canvas to the screen as a transparent image, using 79 as the transparent color.
		// This allows the PDA border to overlay the map, briefing, and other "overlay" elements.
		screenDraw_setTransColor(79);
		menu_blitToScreen(nullptr, JTRUE/*transparent*/, JFALSE/*swap*/);
		
		// Doing that we need to restore the transparent color before blitting the mouse cursor, otherwise its black edges will 
		// show up incorrectly.
		screenDraw_setTransColor(0);
		menu_blitCursorScaled(s_cursorPos.x, s_cursorPos.z, vfb_getCpuBuffer());
		vfb_swap();
	}
	
	///////////////////////////////////////////
	// Internal Implementation
	///////////////////////////////////////////
	void pda_handleMouseAccum(s32* wdx, s32* wdy)
	{
		if (!s_frameReady)
		{
			s_mouseAccum.x += *wdx;
			s_mouseAccum.z += *wdy;
			*wdx += s_mouseAccum.x;
			*wdy += s_mouseAccum.z;
		}
		else
		{
			*wdx += s_mouseAccum.x;
			*wdy += s_mouseAccum.z;
			s_mouseAccum = { 0 };
		}
	}

	void pda_handleButtons()
	{
		// Add support for mouse wheel scrolling.
		s32 wdx, wdy;
		TFE_Input::getMouseWheel(&wdx, &wdy);
		// Ensure that mouse movements are not lost between updates.
		pda_handleMouseAccum(&wdx, &wdy);

		// Ensure PDA key presses handle replay data
		bool mouseDown = TFE_Input::isDemoPlayback() && inputMapping_getAction(IADF_PRIMARY_FIRE) == STATE_DOWN || TFE_Input::mouseDown(MBUTTON_LEFT);
		bool mousePressed = TFE_Input::isDemoPlayback() && inputMapping_getAction(IADF_PRIMARY_FIRE) == STATE_PRESSED || TFE_Input::mousePressed(MBUTTON_LEFT);
		
		// Handle Mouse Panning
		if (mouseDown)
		{
			s32 mouseX, mouseZ;
			TFE_Input::getMousePos(&mouseX, &mouseZ);
			
			// Calculate deltas
			fixed16_16 deltaX = (mouseX - mouseOldPosX) << 16;
			fixed16_16 deltaZ = (mouseZ - mouseOldPosZ) << 16;

			mouseOldPosX = mouseX;
			mouseOldPosZ = mouseZ;

			// Don't bother if this is the first time 
			if (!mouseMoveReset)
			{
				automap_updateDeltaCoords(deltaX, deltaZ);
			}
			mouseMoveReset = false;
		}
		else
		{
			// Mouse Up - reset the offsets.
			mouseMoveReset = true;
		}

		if (s_simulatePressed >= 0 || wdy || mousePressed)
		{
			s_buttonPressed = -1;
			s32 count = (s_pdaMode == PDA_MODE_MAP || s_pdaMode == PDA_MODE_BRIEF) ? PDA_BTN_COUNT : PDA_BTN_EXIT + 1;
			for (s32 i = PDA_BTN_MAP; i < count; i++)
			{
				lactor_setState(s_pdaArt, 2 * (1 + i), 0);
				LRect buttonRect;
				lactorAnim_getFrame(s_pdaArt, &buttonRect);
				
				if (!TFE_Input::mousePressed(MBUTTON_LEFT) && !mousePressed)
				{
					if (i == s_simulatePressed)
					{
						s_buttonPressed = s32(i);
						s_buttonHover = JTRUE;
						s_simulatePressed = 0;
						break;
					}
				}
				else if ((s_cursorPos.x >= buttonRect.left && s_cursorPos.x < buttonRect.right &&
					      s_cursorPos.z >= buttonRect.top && s_cursorPos.z < buttonRect.bottom) ||
					      i == s_simulatePressed)
				{
					s_buttonPressed = s32(i);
					s_buttonHover = JTRUE;
					if (i == s_simulatePressed) { s_simulatePressed = 0; }
					break;
				}
			}
						
			if (s_pdaMode == PDA_MODE_MAP && (s_buttonPressed || wdy || mousePressed) && s_frameReady)
			{
				// Add support for mouse wheel scrolling.
				if (wdy < 0)
				{
					automap_updateMapData(MAP_ZOOM_IN);
				}
				else if (wdy > 0)
				{
					automap_updateMapData(MAP_ZOOM_OUT);
				}

				switch (s_buttonPressed)
				{
					case PDA_BTN_PANUP:
					{
						automap_updateMapData(MAP_MOVE1_UP);
					} break;
					case PDA_BTN_PANRIGHT:
					{
						automap_updateMapData(MAP_MOVE1_RIGHT);
					} break;
					case PDA_BTN_PANDOWN:
					{
						automap_updateMapData(MAP_MOVE1_DN);
					} break;
					case PDA_BTN_PANLEFT:
					{
						automap_updateMapData(MAP_MOVE1_LEFT);
					} break;
					case PDA_BTN_ZOOMOUT:
					{
						automap_updateMapData(MAP_ZOOM_IN);
					} break;
					case PDA_BTN_ZOOMIN:
					{
						automap_updateMapData(MAP_ZOOM_OUT);
					} break;
					case PDA_BTN_LAYERUP:
					{
						if (s_buttonPressed == s_simulatePressed)
						{
							automap_updateMapData(MAP_LAYER_UP);
							s_simulatePressed = 0;
						}
					} break;
					case PDA_BTN_LAYERDOWN:
					{
						if (s_buttonPressed == s_simulatePressed)
						{
							automap_updateMapData(MAP_LAYER_DOWN);
							s_simulatePressed = 0;
						}
					} break;
				}
			}
			else if (s_pdaMode == PDA_MODE_BRIEF && (s_buttonPressed || wdy || mousePressed) && s_frameReady)
			{
				// Add support for mouse wheel scrolling.
				if (wdy > 0) // up
				{
					if (s_briefY > -BRIEF_VERT_MARGIN)
					{
						s_briefY -= BRIEF_LINE_SCROLL * wdy;
						if (s_briefY < -BRIEF_VERT_MARGIN) s_briefY = -BRIEF_VERT_MARGIN;
					}
				}
				else if (wdy < 0) // down
				{
					if (s_briefY != s_briefingMaxY)
					{
						s_briefY -= BRIEF_LINE_SCROLL * wdy;
						if (s_briefY > s_briefingMaxY) s_briefY = s_briefingMaxY;
					}
				}

				switch (s_buttonPressed)
				{
					case PDA_BTN_PANUP:
					{
						if (s_briefY > -BRIEF_VERT_MARGIN)
						{
							s_briefY -= BRIEF_LINE_SCROLL;
							if (s_briefY < -BRIEF_VERT_MARGIN) s_briefY = -BRIEF_VERT_MARGIN;
						}
					} break;
					case PDA_BTN_PANDOWN:
					{
						if (s_briefY != s_briefingMaxY)
						{
							s_briefY += BRIEF_LINE_SCROLL;

							if (s_briefY > s_briefingMaxY) s_briefY = s_briefingMaxY;
						}
					} break;
				}
			}
		}
		else if (mouseDown && s_buttonPressed >= 0)
		{
			lactor_setState(s_pdaArt, 2 * (1 + s_buttonPressed), 0);
			LRect buttonRect;
			lactorAnim_getFrame(s_pdaArt, &buttonRect);

			// Verify that the mouse is still over the button.
			if (s_cursorPos.x >= buttonRect.left && s_cursorPos.x < buttonRect.right &&
				s_cursorPos.z >= buttonRect.top && s_cursorPos.z < buttonRect.bottom)
			{
				s_buttonHover = JTRUE;
			}
			else
			{
				s_buttonHover = JFALSE;
			}

			if (s_buttonHover && s_pdaMode == PDA_MODE_MAP && s_frameReady)
			{
				switch (s_buttonPressed)
				{
					case PDA_BTN_PANUP:
					{
						automap_updateMapData(MAP_MOVE1_UP);
					} break;
					case PDA_BTN_PANRIGHT:
					{
						automap_updateMapData(MAP_MOVE1_RIGHT);
					} break;
					case PDA_BTN_PANDOWN:
					{
						automap_updateMapData(MAP_MOVE1_DN);
					} break;
					case PDA_BTN_PANLEFT:
					{
						automap_updateMapData(MAP_MOVE1_LEFT);
					} break;
					case PDA_BTN_ZOOMOUT:
					{
						automap_updateMapData(MAP_ZOOM_IN);
					} break;
					case PDA_BTN_ZOOMIN:
					{
						automap_updateMapData(MAP_ZOOM_OUT);
					} break;
				}
			}
			else if (s_buttonHover && s_pdaMode == PDA_MODE_BRIEF && s_frameReady)
			{
				switch (s_buttonPressed)
				{
					case PDA_BTN_PANUP:
					{
						if (s_briefY > -BRIEF_VERT_MARGIN)
						{
							s_briefY -= BRIEF_LINE_SCROLL;
							if (s_briefY < -BRIEF_VERT_MARGIN) s_briefY = -BRIEF_VERT_MARGIN;
						}
					} break;
					case PDA_BTN_PANDOWN:
					{
						if (s_briefY != s_briefingMaxY)
						{
							s_briefY += BRIEF_LINE_SCROLL;

							if (s_briefY > s_briefingMaxY) s_briefY = s_briefingMaxY;
						}
					} break;
				}
			}
		}
		else
		{
			if (s_buttonPressed >= 0 && s_buttonHover && s_simulatePressed < 0)
			{
				switch (s_buttonPressed)
				{
					case PDA_BTN_MAP:
					{
						s_pdaMode = PDA_MODE_MAP;
					} break;
					case PDA_BTN_WEAPONS:
					{
						s_pdaMode = PDA_MODE_WEAPONS;
					} break;
					case PDA_BTN_INV:
					{
						s_pdaMode = PDA_MODE_INV;
					} break;
					case PDA_BTN_GOALS:
					{
						s_pdaMode = PDA_MODE_GOALS;
					} break;
					case PDA_BTN_BRIEF:
					{
						s_pdaMode = PDA_MODE_BRIEF;
					} break;
					case PDA_BTN_EXIT:
					{
						pda_close();
					} break;
					case PDA_BTN_LAYERUP:
					{
						automap_updateMapData(MAP_LAYER_UP);
					} break;
					case PDA_BTN_LAYERDOWN:
					{
						automap_updateMapData(MAP_LAYER_DOWN);
					} break;
				}
			}
			// Reset.
			s_buttonPressed = -1;
			s_buttonHover = JFALSE;
		}
		s_simulatePressed = -1;
	}
		
	void pda_handleInput()
	{
		menu_handleMousePosition();
		automap_setPdaActive(JTRUE);

		if (TFE_Input::keyPressed(KEY_SPACE))
		{
			if (TFE_Input::keyDown(KEY_LSHIFT) || TFE_Input::keyDown(KEY_RSHIFT))
			{
				s_pdaMode = (s_pdaMode == PDA_MODE_MAP) ? PDA_MODE_BRIEF : PdaMode(s_pdaMode - 1);
			}
			else
			{
				s_pdaMode = (s_pdaMode == PDA_MODE_BRIEF) ? PDA_MODE_MAP : PdaMode(s_pdaMode + 1);
			}
		}

		if (s_pdaMode == PDA_MODE_MAP)
		{
			if (TFE_Input::keyDown(KEY_UP))
			{
				s_simulatePressed = PDA_BTN_PANUP;
			}
			else if (TFE_Input::keyDown(KEY_DOWN))
			{
				s_simulatePressed = PDA_BTN_PANDOWN;
			}
			if (TFE_Input::keyDown(KEY_LEFT))
			{
				s_simulatePressed = PDA_BTN_PANLEFT;
			}
			else if (TFE_Input::keyDown(KEY_RIGHT))
			{
				s_simulatePressed = PDA_BTN_PANRIGHT;
			}

			if (TFE_Input::keyDown(KEY_EQUALS))
			{
				s_simulatePressed = PDA_BTN_ZOOMIN;
			}
			else if (TFE_Input::keyDown(KEY_MINUS))
			{
				s_simulatePressed = PDA_BTN_ZOOMOUT;
			}

			if (TFE_Input::keyPressed(KEY_LEFTBRACKET))
			{
				s_simulatePressed = PDA_BTN_LAYERDOWN;
			}
			else if (TFE_Input::keyPressed(KEY_RIGHTBRACKET))
			{
				s_simulatePressed = PDA_BTN_LAYERUP;
			}
		}
		else if (s_pdaMode == PDA_MODE_BRIEF)
		{
			if (TFE_Input::keyDown(KEY_UP))
			{
				s_simulatePressed = PDA_BTN_PANUP;
			}
			else if (TFE_Input::keyDown(KEY_DOWN))
			{
				s_simulatePressed = PDA_BTN_PANDOWN;
			}
		}

		pda_handleButtons();
	}

	void pda_drawButton(PdaButton id)
	{
		s32 pressed = 0;
		if ((s_buttonHover && id == s_buttonPressed) || (id == (PdaButton)s_pdaMode))
		{
			pressed = 1;
		}

		lactor_setState(s_pdaArt, 2 * (1 + id) + (pressed ? 0 : 1), 0);
		lactorAnim_draw(s_pdaArt, &s_viewBounds, &s_viewBounds, 0, 0, JTRUE);
	}
		
	void pda_drawCommonButtons()
	{
		for (s32 i = PDA_BTN_MAP; i <= PDA_BTN_EXIT; i++)
		{
			pda_drawButton(PdaButton(i));
		}
	}

	void pda_drawMapButtons()
	{
		for (s32 i = PDA_BTN_PANUP; i <= PDA_BTN_LAYERDOWN; i++)
		{
			pda_drawButton(PdaButton(i));
		}
	}

	void pdaDrawBriefingButtons()
	{
		pda_drawButton(PDA_BTN_PANUP);
		pda_drawButton(PDA_BTN_PANDOWN);
	}
		
	void pda_drawOverlay()
	{
		if (s_pdaMode == PDA_MODE_MAP)
		{
			ScreenRect* uiRect = vfb_getScreenRect(VFB_RECT_UI);
			fixed16_16 xScale = vfb_getXScale();
			fixed16_16 yScale = vfb_getYScale();

			s32 virtualWidth = floor16(mul16(intToFixed16(320), xScale));
			s32 offset = max(0, ((uiRect->right - uiRect->left + 1) - virtualWidth) / 2);

			ScreenRect clipRect;
			clipRect.left = offset + floor16(mul16(intToFixed16(s_pdaRect.left), xScale) + xScale);
			clipRect.right = offset + floor16(mul16(intToFixed16(s_pdaRect.right), xScale) - xScale);
			clipRect.top = floor16(mul16(intToFixed16(s_pdaRect.top), yScale) + yScale);
			clipRect.bot = floor16(mul16(intToFixed16(s_pdaRect.bottom), yScale) - yScale);

			vfb_setScreenRect(VFB_RECT_RENDER, &clipRect);
			automap_draw(vfb_getCpuBuffer());
			vfb_restoreScreenRect(VFB_RECT_RENDER);
		}
		else if (s_pdaMode == PDA_MODE_WEAPONS && s_weapons)
		{
			lcanvas_eraseRect(&s_overlayRect);
			lcanvas_setClip(&s_overlayRect);

			for (s32 i = 0; i < s_weapons->arraySize; i++)
			{
				if (player_hasWeapon(i + 2))
				{
					lactor_setState(s_weapons, i, 0);
					lactorAnim_draw(s_weapons, &s_overlayRect, &s_viewBounds, 0, 0, JTRUE);
				}
			}

			lcanvas_clearClipRect();
			menu_blitToScreen(nullptr, JFALSE);
		}
		else if (s_pdaMode == PDA_MODE_INV && s_items)
		{
			lcanvas_eraseRect(&s_overlayRect);
			lcanvas_setClip(&s_overlayRect);

			for (s32 i = 0; i < s_items->arraySize; i++)
			{
				if (player_hasItem(i))
				{
					lactor_setState(s_items, i, 0);
					lactorAnim_draw(s_items, &s_overlayRect, &s_viewBounds, 0, 0, JTRUE);
				}
			}

			lcanvas_clearClipRect();
			menu_blitToScreen(nullptr, JFALSE);
		}
		else if (s_pdaMode == PDA_MODE_BRIEF && s_briefing)
		{
			s32 diff = (s_overlayRect.right - s_overlayRect.left - s_briefing->w) >> 1;
			s32 x = diff + s_overlayRect.left - s_briefing->x;
			s32 y = s_overlayRect.top - s_briefing->y - s_briefY;

			lcanvas_eraseRect(&s_overlayRect);
			lcanvas_setClip(&s_overlayRect);
			lactorDelt_draw(s_briefing, &s_overlayRect, &s_overlayRect, x, y, JTRUE);
			lcanvas_clearClipRect();
			menu_blitToScreen(nullptr, JFALSE);
		}
		else if (s_pdaMode == PDA_MODE_GOALS && s_goalsActor)
		{
			lcanvas_eraseRect(&s_overlayRect);
			lcanvas_setClip(&s_overlayRect);

			lactor_setState(s_goalsActor, 0, 0);
			lactorAnim_draw(s_goalsActor, &s_overlayRect, &s_overlayRect, 0, 0, JTRUE);

			s32 goalCount = s_goalsActor->arraySize - 1;
			for (s32 i = 0; i < goalCount; i++)
			{
				if (level_isGoalComplete(i))
				{
					lactor_setState(s_goalsActor, i + 1, 0);
					lactorAnim_draw(s_goalsActor, &s_overlayRect, &s_overlayRect, 0, 0, JTRUE);
				}
			}

			s32 secretPercentage = s_levelState.secretCount ? floor16(mul16(FIXED(100), div16(intToFixed16(s_secretsFound), intToFixed16(s_levelState.secretCount)))) : 0;
			lactor_setState(s_pdaArt, 30, 0);
			lactorAnim_draw(s_pdaArt, &s_overlayRect, &s_overlayRect, 0, 0, JTRUE);

			char secretStr[32];
			LRect rect;

			bool showCount = TFE_Settings::getGameSettings()->df_showSecretCount;
			if (showCount)
			{
				sprintf(secretStr, "%d/%d", s_secretsFound, s_levelState.secretCount);
			}
			else
			{
				sprintf(secretStr, "%2d%%", secretPercentage);
			}
			lactor_setState(s_pdaArt, 31, 0);
			lactorAnim_getFrame(s_pdaArt, &rect);

			// Shadow
			rect.top++;
			lfont_setColor(91);
			lfont_setFrame(&rect);
			lfont_drawTextClipped(secretStr);

			rect.top--;
			rect.left++;
			lfont_setFrame(&rect);
			lfont_drawTextClipped(secretStr);

			// Text
			rect.left--;
			lfont_setColor(19);
			lfont_setFrame(&rect);
			lfont_drawTextClipped(secretStr);

			lcanvas_clearClipRect();
			menu_blitToScreen(nullptr, JFALSE);
		}
	}

	void pda_clearToBlack()
	{
		u32 outWidth, outHeight;
		vfb_getResolution(&outWidth, &outHeight);
		memset(vfb_getCpuBuffer(), 0, outWidth * outHeight);
		
		u32 palette[256] = { 0 };
		vfb_setPalette(palette);
	}

	// Simplified font drawing which assumes 320x200.
	// Does not handle clipping to avoid code duplication.
	void pda_drawFontGlyph(TextureData* glyph, s32 x0, s32 y0, u8* framebuffer)
	{
		u8* col = glyph->image;
		for (s32 x = x0; x < x0 + glyph->width; x++, col += glyph->height)
		{
			u8* output = framebuffer + x + y0 * 320;

			// columns
			for (s32 y = y0; y < y0 + glyph->height; y++, output += 320)
			{
				u8 color = col[glyph->height - y + y0 - 1];
				if (color) { *output = color; }
			}
		}
	}

	void pda_displayFontString(Font* font, s32 x, s32 y, char* msg)
	{
		if (!font) { return; }

		u8* framebuffer = ldraw_getBitmap();

		s32 xi = x;
		s32 x0 = x;
		for (char c = *msg; c != 0;)
		{
			if (c == '\n')
			{
				xi = x0;
				y += font->height + font->vertSpacing;
			}
			else if (c == ' ')
			{
				xi += font->width;
			}
			else if (c >= font->minChar && c <= font->maxChar)
			{
				s32 charIndex = c - font->minChar;
				TextureData* glyph = &font->glyphs[charIndex];
				pda_drawFontGlyph(glyph, xi, y, framebuffer);
				xi += font->horzSpacing + glyph->width;
			}
			msg++;
			c = *msg;
		}
	}
}