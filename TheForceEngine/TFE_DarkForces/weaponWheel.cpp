#include "weaponWheel.h"
#include "TFE_FrontEndUI/frontEndUi.h"
#include <TFE_Ui/ui.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_DarkForces/weapon.h>
#include <TFE_DarkForces/player.h>
#include <TFE_DarkForces/mission.h>
#include <TFE_DarkForces/automap.h>
#include <TFE_DarkForces/GameUI/pda.h>
#include <TFE_DarkForces/GameUI/escapeMenu.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_System/system.h>
#include <TFE_ExternalData/weaponWheelExternal.h>
#include <TFE_Asset/imageAsset.h>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace TFE_Input;
using namespace TFE_DarkForces;

namespace TFE_DarkForces
{
	// Wheel slot order. This is intentionally separate from WeaponID -
	// it controls purely how weapons are laid out around the wheel, and
	// does not need to match either the internal WeaponID enum order or
	// the keyboard weapon-select order (IADF_WEAPON_1..10 in player.cpp).
	// Mine is placed before Mortar here, per request.

	// The reason we even have this enum is because the MORTAR and MINE are 
	// swapped in the WeaponID enum - this fixes it. Why? Go ask Lucasarts
	enum WheelSlot
	{
		WHEEL_FIST = 0,
		WHEEL_PISTOL,
		WHEEL_RIFLE,
		WHEEL_THERMAL_DET,
		WHEEL_REPEATER,
		WHEEL_FUSION,
		WHEEL_MINE,
		WHEEL_MORTAR,
		WHEEL_CONCUSSION,
		WHEEL_CANNON,
		WHEEL_SLOT_COUNT
	};

	struct WheelSlotDef
	{
		s32         wpnId;
		const char* name;
		const char* iconFile;
	};

	// Slot -> WeaponID/name/icon mapping, in wheel display order.
	// The iconFile is the default fallback, used when no mod override is present.
	static const WheelSlotDef c_wheelSlot[WHEEL_SLOT_COUNT] =
	{
		{ WPN_FIST,        "Fists",             "Fist"                },
		{ WPN_PISTOL,      "Bryar Pistol",      "Bryar_Pistol"        },
		{ WPN_RIFLE,       "E-11 Blaster",      "Stormtrooper_Rifle"  },
		{ WPN_THERMAL_DET, "Thermal Detonator", "Thermal_Detonator"   },
		{ WPN_REPEATER,    "Repeater Gun",      "Autogun"             },
		{ WPN_FUSION,      "Fusion Cutter",     "Fusion_Cutter"       },
		{ WPN_MINE,        "I.M. Mines",        "IM_Mine"             },
		{ WPN_MORTAR,      "Mortar Gun",        "Mortar"              },
		{ WPN_CONCUSSION,  "Concussion Rifle",  "Concussion_Rifle"    },
		{ WPN_CANNON,      "Assault Cannon",    "Assault_Cannon"      },
	};

	static bool    s_iconsLoaded = false;
	static TFE_FrontEndUI::UiImage s_icon[WHEEL_SLOT_COUNT];
	static TFE_FrontEndUI::UiImage s_iconSelected[WHEEL_SLOT_COUNT];

	static bool s_wheelOpen = false;
	static f32  s_cursorX = 0.0f;
	static f32  s_cursorY = 0.0f;
	static s32  s_hoveredSlot = -1;

	static const f32 c_maxCursorRadius = 400.0f;	// px the virtual cursor can travel from center.
	static const f32 c_deadzoneRadius = 48.0f;	// below this, no slot is considered "hovered".
	static const f32 c_stickSpeed = 1400.0f;	// px/sec of cursor travel at full right-stick deflection.

	// Loads a weapon wheel image, checking three places in order:
	//  1. A mod-provided file matching 'overrideFileName' exactly, i.e.
	//     sitting at the root of the mod zip/folder (via getFilePath,
	//     which is mod-aware: mod zip registrations and loose-mod
	//     search paths).
	//  2. The base game's own UI_Images/weaponWheel/ folder, still using
	//     'overrideFileName' - this lets a mod's weaponWheel.json
	//     reference one of the game's existing icons by name (e.g.
	//     reusing "Stormtrooper_Rifle.png" for a different slot)
	//     without needing to duplicate that file into the mod at all.
	//  3. 'baseFallbackPath' - this slot's own default asset, used when
	//     there's no override, or the override's filename couldn't be
	//     found anywhere above.
	static bool loadOneWheelImage(const char* overrideFileName, const char* baseFallbackPath, TFE_FrontEndUI::UiImage* icon)
	{ 
		if (overrideFileName)
		{
			// Check the mod zip/folder first for an exact match to the override name.
			FilePath resolved;
			if (TFE_Paths::getFilePath(overrideFileName, &resolved))
			{
				SDL_Surface* image = TFE_Image::get(resolved.path);
				if (image)
				{
					TextureGpu* tex = TFE_RenderBackend::createTexture(image->w, image->h, (u32*)image->pixels, MAG_FILTER_LINEAR);
					if (tex)
					{
						icon->image = TFE_RenderBackend::getGpuPtr(tex);
						icon->width = image->w;
						icon->height = image->h;
						return true;
					}
				}
			}

			// Otherwise just fall back to the base game's own UI_Images/weaponWheel/<name>[_select].png.
			char baseNamedPath[TFE_MAX_PATH];
			sprintf(baseNamedPath, "UI_Images/weaponWheel/%s", overrideFileName);
			TFE_FrontEndUI::UiImage baseNamed;
			if (loadGpuImage(baseNamedPath, &baseNamed))
			{
				*icon = baseNamed;
				return true;
			}
		}
		return loadGpuImage(baseFallbackPath, icon);
	}

	// Loads both icon variants for every wheel slot
	void loadWeaponIcons()
	{
		if (s_iconsLoaded) { return; }
		s_iconsLoaded = true;

		char basePath[TFE_MAX_PATH], baseSelectedPath[TFE_MAX_PATH];
		for (s32 i = 0; i < WHEEL_SLOT_COUNT; i++)
		{
			const s32 wpnId = c_wheelSlot[i].wpnId;
			const TFE_ExternalData::WeaponWheelOverride* ov = TFE_ExternalData::getWeaponWheelOverride(wpnId);

			sprintf(basePath, "UI_Images/weaponWheel/%s.png", c_wheelSlot[i].iconFile);
			sprintf(baseSelectedPath, "UI_Images/weaponWheel/%s_select.png", c_wheelSlot[i].iconFile);

			const char* unselectedOverride = (ov && ov->hasImageUnselected) ? ov->imageUnselected.c_str() : nullptr;
			const char* selectedOverride = (ov && ov->hasImageSelected) ? ov->imageSelected.c_str() : nullptr;

			loadOneWheelImage(unselectedOverride, basePath, &s_icon[i]);
			loadOneWheelImage(selectedOverride, baseSelectedPath, &s_iconSelected[i]);
		}
	}

	// Returns the display name for a wheel slot, honoring a
	// weaponWheel.json name override if present.
	static const char* getWheelSlotName(s32 slot)
	{
		const TFE_ExternalData::WeaponWheelOverride* ov = TFE_ExternalData::getWeaponWheelOverride(c_wheelSlot[slot].wpnId);
		return (ov && ov->hasName) ? ov->name.c_str() : c_wheelSlot[slot].name;
	}

	void weaponWheel_freeIcons()
	{
		s_iconsLoaded = false;
		for (s32 i = 0; i < WHEEL_SLOT_COUNT; i++)
		{
			s_icon[i] = TFE_FrontEndUI::UiImage();
			s_iconSelected[i] = TFE_FrontEndUI::UiImage();
		}
	}

	// Builds the list of wheel slots the player currently owns, in wheel
	// display order (see c_wheelSlot above). 
	static void buildOwnedSlotList(std::vector<s32>& owned)
	{
		owned.clear();
		for (s32 slot = 0; slot < WHEEL_SLOT_COUNT; slot++)
		{
			const s32 wpnId = c_wheelSlot[slot].wpnId;
			bool hasIt = (wpnId == WPN_FIST) || player_hasWeapon(wpnId + 1);
			if (hasIt) { owned.push_back(slot); }
		}
	}

	// Just make sure you don't open the wweaponWheel while doing non-gameplay activities. 
	static bool wheelCanOpen()
	{
		return !TFE_DarkForces::s_gamePaused && !pda_isOpen() 
			&& !TFE_DarkForces::s_drawAutomap && !escapeMenu_isOpen();
	}

	void weaponWheel_update()
	{
		// Check if button is held.
		const bool held = inputMapping_getActionState(IADF_WEAPON_WHEEL) != STATE_UP;

		if (!s_wheelOpen)
		{
			// Just exit the function if the wheel is not open and the button is not held.
			if (!(held && wheelCanOpen())) return;

			s_wheelOpen = true;
			s_cursorX = 0.0f;
			s_cursorY = 0.0f;
			s_hoveredSlot = -1;
			s_disablePlayerFire = JTRUE;
			s_disablePlayerRotation = JTRUE;
		}

		// Once open, keep it open while held
		std::vector<s32> owned;
		buildOwnedSlotList(owned);
		const s32 count = (s32)owned.size();

		// Figure out the cursor position 

		// CHECK MOUSE movement
		s32 mdx = 0, mdy = 0;
		TFE_Input::getMouseMove(&mdx, &mdy);
		s_cursorX += (f32)mdx;
		s_cursorY += (f32)mdy;

		// CHECK CONTROLLER movement		
		const f32 stickX = inputMapping_getAnalogAxis(AA_LOOK_HORZ);
		const f32 stickY = inputMapping_getAnalogAxis(AA_LOOK_VERT);
		if (stickX != 0.0f || stickY != 0.0f)
		{
			const f32 dt = (f32)TFE_System::getDeltaTime();
			s_cursorX += stickX * c_stickSpeed * dt;
			s_cursorY -= stickY * c_stickSpeed * dt;
		}

		// Is the cursos in the right place (outside dead zone). 
		const f32 dist = sqrtf(s_cursorX * s_cursorX + s_cursorY * s_cursorY);
		if (dist > c_maxCursorRadius)
		{
			const f32 scale = c_maxCursorRadius / dist;
			s_cursorX *= scale;
			s_cursorY *= scale;
		}

		// Figure out the slot the cursor is hovering over based on its angle 
		if (dist < c_deadzoneRadius)
		{
			s_hoveredSlot = -1;
		}
		else
		{
			const f32 sliceWidth = 360.0f / count;
			const f32 cursorDeg = atan2f(s_cursorY, s_cursorX) * 180.0f / 3.14159265f;	// range: -180 to 180

			// Slot i is centered at (-90 + i*sliceWidth). Shift so slot 0's
			// center lands on 0, plus half a slice so integer division
			// buckets into the correct slot rather than its neighbor.
			f32 shifted = fmodf(cursorDeg + 90.0f + sliceWidth * 0.5f, 360.0f);
			if (shifted < 0.0f) { shifted += 360.0f; }

			s32 idx = (s32)(shifted / sliceWidth);
			if (idx < 0) { idx = 0; }
			if (idx >= count) { idx = count - 1; }
			s_hoveredSlot = idx;
		}
		

		// Draw the wheel overlay
		DisplayInfo display;
		TFE_RenderBackend::getDisplayInfo(&display);
		const f32 cx = display.width * 0.5f;
		const f32 cy = display.height * 0.5f;
		const f32 wheelRadius = 462.0f;
		const f32 iconRadius = 360.0f;

		const u32 windowFlags = ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
			ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;

		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2((f32)display.width, (f32)display.height));
		ImGui::Begin("##WeaponWheel", nullptr, windowFlags);
		ImDrawList* draw = ImGui::GetWindowDrawList();

		// Calculate the font size
		ImFont* font = ImGui::GetFont();
		const f32 baseFontSize = ImGui::GetFontSize();
		const f32 labelFontSize = baseFontSize * 2.0f;
		const f32 centerFontSize = baseFontSize * 2.6f;

		// Backdrop - this is the dark circle background to make it easier see the contrast
		draw->AddCircleFilled(ImVec2(cx, cy), wheelRadius, IM_COL32(10, 10, 15, 160), 96);

		// For each weapon figure out which one to draw based on what you've covered. 
		for (s32 i = 0; i < count; i++)
		{
			const s32 slot = owned[i];
			const s32 wpnId = c_wheelSlot[slot].wpnId;
			const f32 slotDeg = -90.0f + i * (360.0f / count);
			const f32 slotRad = slotDeg * 3.14159265f / 180.0f;
			const f32 sx = cx + cosf(slotRad) * iconRadius;
			const f32 sy = cy + sinf(slotRad) * iconRadius;
			const bool isHovered = (i == s_hoveredSlot);

			const TFE_FrontEndUI::UiImage& normalIcon = s_icon[slot];
			const TFE_FrontEndUI::UiImage& selectedIcon = s_iconSelected[slot];
			const TFE_FrontEndUI::UiImage& icon = (isHovered && selectedIcon.image) ? selectedIcon : normalIcon;

			// The icons should be 128x128 but we recalculate by scale just in case...
			if (icon.image)
			{
				const f32 maxDim = 128.0f;
				const f32 scale = maxDim / (f32)max(icon.width, icon.height);
				const f32 iw = icon.width * scale;
				const f32 ih = icon.height * scale;
				draw->AddImage(icon.image, ImVec2(sx - iw * 0.5f, sy - ih * 0.5f), ImVec2(sx + iw * 0.5f, sy + ih * 0.5f));
			}
			else
			{
				// Missing/failed-to-load icon - fall back to a text label.
				const char* label = getWheelSlotName(slot);
				ImVec2 ts = font->CalcTextSizeA(labelFontSize, 1024.0f, 0.0f, label);
				draw->AddText(font, labelFontSize, ImVec2(sx - ts.x * 0.5f, sy - ts.y * 0.5f), IM_COL32(255, 200, 120, 255), label);
			}

			// Draw the ammo count
			const s32 ammo = weapon_getAmmoCount(wpnId);
			if (ammo >= 0)
			{
				// Fun fact - we can add secondary ammo for any weapon! 
				const s32 secondaryAmmo = weapon_getSecondaryAmmoCount(wpnId);
				char ammoStr[32];
				if (secondaryAmmo >= 0) { sprintf(ammoStr, "%d (%d)", ammo, secondaryAmmo); }
				else { sprintf(ammoStr, "%d", ammo); }
				ImVec2 ts = font->CalcTextSizeA(labelFontSize, 1024.0f, 0.0f, ammoStr);
				draw->AddText(font, labelFontSize, ImVec2(sx - ts.x * 0.5f, sy + 68.0f), IM_COL32(255, 210, 90, 255), ammoStr);
			}
		}

		// Center label. Color matches the classic HUD pickup/notification
		const char* centerName = (s_hoveredSlot >= 0) ? getWheelSlotName(s_hoveredSlot) : "";
		{
			ImVec2 ts = font->CalcTextSizeA(centerFontSize, 1024.0f, 0.0f, centerName);
			draw->AddText(font, centerFontSize, ImVec2(cx - ts.x * 0.5f + 2, cy - ts.y * 0.5f + 2), IM_COL32(0, 0, 0, 220), centerName);
			draw->AddText(font, centerFontSize, ImVec2(cx - ts.x * 0.5f, cy - ts.y * 0.5f), IM_COL32(255, 213, 65, 255), centerName);
		}

		ImGui::End();

		// Teardown
		if (!held)
		{
			s_wheelOpen = false;
			s_disablePlayerFire = JFALSE;
			s_disablePlayerRotation = JFALSE;
			if (s_hoveredSlot >= 0 && s_hoveredSlot < count)
			{
				weapon_queueWeaponSwitch(c_wheelSlot[owned[s_hoveredSlot]].wpnId);
			}
			s_hoveredSlot = -1;
		}
	}

	void weaponWheel_init()
	{
		loadWeaponIcons();
	}
}