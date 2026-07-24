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
#include <TFE_Archive/zipArchive.h>
#include <TFE_Settings/settings.h>
#include <TFE_Settings/gameSourceData.h>
#include <TFE_System/utf8.h>
#include <TFE_FileSystem/fileutil.h>
#include <cmath>
#include <cstdio>
#include <cstring>
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
		s32         wpnId;		// WeaponID this slot switches to.
		const char* name;		// Display name (center label).
		const char* iconFile;	// Base filename (no _select suffix, no extension) under UI_Images/weaponWheel/.
	};

	// Slot -> WeaponID/name/icon mapping, in wheel display order.
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
	static TFE_FrontEndUI::UiImage s_icon[WHEEL_SLOT_COUNT];			// Unselected/normal variant.
	static TFE_FrontEndUI::UiImage s_iconSelected[WHEEL_SLOT_COUNT];	// Highlighted variant, shown while hovered.

	// WeaponID -> filename inside the remaster's DarkEX.kpf, under
	// gfx/wheel/. Indexed by WeaponID (not wheel slot), matching how
	// TFE_ExternalData::getWeaponWheelOverride() is also keyed. There's
	// no separate "selected" art in the remaster's set - see
	// buildRemasterIconPair() below, which dims the unselected variant
	// and brightens the selected one from this same source image.
	static const char* c_remasterIconFile[WPN_COUNT] =
	{
		"FIST.PNG",       // WPN_FIST
		"BLASTER.PNG",    // WPN_PISTOL
		"RIFLE.PNG",      // WPN_RIFLE
		"THERMAL.PNG",    // WPN_THERMAL_DET
		"AUTOGUN.PNG",    // WPN_REPEATER
		"FUSION.PNG",     // WPN_FUSION
		"MORTAR.PNG",     // WPN_MORTAR
		"CLAYMORE.PNG",   // WPN_MINE
		"CONCUSSION.PNG", // WPN_CONCUSSION
		"CANNON.PNG",     // WPN_CANNON
	};

	static bool s_remasterChecked = false;	// Have we tried extracting from DarkEX.kpf yet this session?
	static bool s_remasterAvailable = false;	// True if DarkEX.kpf existed and had a gfx/wheel/ folder.

	static bool s_wheelOpen = false;
	static f32  s_cursorX = 0.0f;
	static f32  s_cursorY = 0.0f;
	static s32  s_hoveredSlot = -1;	// index into the current frame's owned-slot list, not a WheelSlot.

	static const f32 c_maxCursorRadius = 400.0f;	// px the virtual cursor can travel from center.
	static const f32 c_deadzoneRadius = 48.0f;	// below this, no slot is considered "hovered".
	static const f32 c_stickSpeed = 1400.0f;	// px/sec of cursor travel at full right-stick deflection.

	// Uploads a decoded SDL_Surface as a GPU texture into 'icon'.
	static bool uploadImage(SDL_Surface* image, TFE_FrontEndUI::UiImage* icon)
	{
		if (!image) { return false; }
		TextureGpu* tex = TFE_RenderBackend::createTexture(image->w, image->h, (u32*)image->pixels, MAG_FILTER_LINEAR);
		if (!tex) { return false; }
		icon->image = TFE_RenderBackend::getGpuPtr(tex);
		icon->width = image->w;
		icon->height = image->h;
		return true;
	}

	// Loads an explicit, mod-specified filename, checking two places:
	//  1. The root of the mod zip/folder (via getFilePath, which is
	//     mod-aware: mod zip registrations and loose-mod search paths).
	//  2. The base game's own UI_Images/weaponWheel/ folder, still using
	//     the same filename - lets a mod's weaponWheel.json reference
	//     one of the game's existing icons by name (e.g. reusing
	//     "Stormtrooper_Rifle.png" for a different slot) without
	//     needing to duplicate that file into the mod at all.
	// Returns false if the filename can't be found in either place.
	static bool loadNamedImage(const char* fileName, TFE_FrontEndUI::UiImage* icon)
	{
		if (!fileName) { return false; }

		FilePath resolved;
		if (TFE_Paths::getFilePath(fileName, &resolved) && uploadImage(TFE_Image::get(resolved.path), icon))
		{
			return true;
		}

		char baseNamedPath[TFE_MAX_PATH];
		sprintf(baseNamedPath, "UI_Images/weaponWheel/%s", fileName);
		return loadGpuImage(baseNamedPath, icon);
	}

	// If the STAR WARS: Dark Forces Remaster is installed, its DarkEX.kpf
	// (the same archive TFE already reads DCSS cutscene scripts from -
	// see TFE_DarkForces/Remaster/remasterCutscenes.cpp) also contains a
	// set of weapon wheel icons under gfx/wheel/*.PNG. This extracts
	// those (once per session) into the same Temp/ cache used for mod
	// asset extraction, and registers each under "gfx/wheel/<name>" so
	// loadNamedImage()-style lookups can find them.
	//
	// Lazy and one-shot; s_remasterAvailable stays false (no crash, just
	// no remaster icons) if DarkEX.kpf isn't present or has no gfx/wheel/
	// folder - e.g. on a non-remaster install, or an older remaster
	// version that doesn't ship these.
	static void extractRemasterIcons()
	{
		if (s_remasterChecked) { return; }
		s_remasterChecked = true;

		TFE_GameHeader* darkForces = TFE_Settings::getGameHeader("Dark Forces");
		if (!darkForces) { return; }

		char darkExPath[TFE_MAX_PATH];
		sprintf(darkExPath, "%sDarkEX.kpf", darkForces->sourcePath);
		if (!FileUtil::exists(darkExPath)) { return; }

		// sourcePath may be extended-ASCII (e.g. from the Windows registry);
		// miniz needs UTF-8 - same conversion remasterCutscenes.cpp does
		// before opening this same file.
		char darkExUtf8[TFE_MAX_PATH];
		convertExtendedAsciiToUtf8(darkExPath, darkExUtf8);

		ZipArchive darkEx;
		if (!darkEx.open(darkExUtf8))
		{
			TFE_System::logWrite(LOG_WARNING, "WeaponWheel", "Could not open DarkEX.kpf at: %s", darkExPath);
			return;
		}

		char tempPath[TFE_MAX_PATH];
		sprintf(tempPath, "%sTemp/", TFE_Paths::getPath(PATH_PROGRAM_DATA));
		FileUtil::makeDirectory(tempPath);

		static const char* c_wheelPrefix = "gfx/wheel/";
		const size_t prefixLen = strlen(c_wheelPrefix);
		const u32 count = darkEx.getFileCount();
		for (u32 i = 0; i < count; i++)
		{
			const char* name = darkEx.getFileName(i);
			if (strncasecmp(name, c_wheelPrefix, prefixLen) != 0 || strlen(name) <= prefixLen) { continue; }

			u32 bufferLen = (u32)darkEx.getFileLength(i);
			if (bufferLen == 0) { continue; }
			u8* buffer = (u8*)malloc(bufferLen);
			darkEx.openFile(i);
			darkEx.readFile(buffer, bufferLen);
			darkEx.closeFile();

			char baseName[TFE_MAX_PATH];
			FileUtil::getFileNameFromPath(name, baseName, true);

			char outPath[TFE_MAX_PATH];
			sprintf(outPath, "%swheelicon_%s", tempPath, baseName);
			FileStream file;
			if (file.open(outPath, Stream::MODE_WRITE))
			{
				file.writeBuffer(buffer, bufferLen);
				file.close();
			}
			free(buffer);

			TFE_Paths::addSingleFilePath(name, outPath);
			s_remasterAvailable = true;
		}
		darkEx.close();
	}

	// The wheel's icons are meant to be ~128x128 (see the doc comment in
	// TFE_ExternalData/weaponWheelExternal.h) but the remaster's own
	// gfx/wheel/*.PNG are 640x400 stills - downscale them before
	// generating the dim/bright pair, both for GPU memory (a 640x400
	// RGBA8 texture is ~1MB; twenty of those adds up for no visual
	// benefit at this size) and to avoid relying on the GPU sampler to
	// do a 5x minify well.
	//
	// Uses SDL_SoftStretch, the same resize SDL call
	// TFE_Image::writeImageToMemory() already uses elsewhere in the
	// engine (see TFE_Asset/imageAsset.cpp - used there for save-game
	// thumbnails), rather than a separate hand-rolled filter.
	static const s32 c_remasterIconSize = 128;

	static bool resizeRGBA(const u32* src, s32 srcW, s32 srcH, std::vector<u32>& dst, s32 dstW, s32 dstH)
	{
		SDL_Surface* srcSurf = SDL_CreateRGBSurfaceFrom((void*)src, srcW, srcH, 32, srcW * sizeof(u32),
			0xFF, 0xFF00, 0xFF0000, 0xFF000000);
		if (!srcSurf) { return false; }

		SDL_Surface* dstSurf = SDL_CreateRGBSurface(0, dstW, dstH, 32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000);
		if (!dstSurf)
		{
			SDL_FreeSurface(srcSurf);
			return false;
		}

		const SDL_Rect rs = { 0, 0, srcW, srcH };
		const SDL_Rect rd = { 0, 0, dstW, dstH };
		const bool ok = (SDL_SoftStretch(srcSurf, &rs, dstSurf, &rd) == 0);
		if (ok)
		{
			dst.resize((size_t)dstW * dstH);
			memcpy(dst.data(), dstSurf->pixels, (size_t)dstW * dstH * sizeof(u32));
		}

		SDL_FreeSurface(srcSurf);
		SDL_FreeSurface(dstSurf);
		return ok;
	}

	// Finds the tight bounding box of non-fully-transparent pixels in an
	// RGBA8 buffer. Returns false (leaving the box untouched) if every
	// pixel is fully transparent.
	// Finds the tight bounding box of pixels above 'alphaThreshold'. Low
	// but nonzero alpha (anti-aliased edge fringe) is excluded on
	// purpose - including it in the crop, then bilinear-sampling it at
	// a texture edge, is what produces a faint rectangular halo/border
	// around the icon.
	static bool computeOpaqueBounds(const u32* pixels, s32 w, s32 h, s32* outLeft, s32* outTop, s32* outRight, s32* outBottom, u8 alphaThreshold = 24)
	{
		s32 left = w, top = h, right = -1, bottom = -1;
		for (s32 y = 0; y < h; y++)
		{
			for (s32 x = 0; x < w; x++)
			{
				const u8 a = (u8)((pixels[y * w + x] >> 24) & 0xff);
				if (a < alphaThreshold) { continue; }
				if (x < left) { left = x; }
				if (x > right) { right = x; }
				if (y < top) { top = y; }
				if (y > bottom) { bottom = y; }
			}
		}
		if (right < left || bottom < top) { return false; }
		*outLeft = left; *outTop = top; *outRight = right; *outBottom = bottom;
		return true;
	}

	// The remaster's per-weapon glyphs are wildly different sizes within
	// their own 640x400 canvas (e.g. the Autogun's glyph is ~112px
	// across, the Thermal Detonator's is only ~30px) - that's the art
	// correctly conveying relative real-world size. Scaling each icon
	// to independently fill its own 128px box would erase that
	// relationship (a grenade ending up the same size as a rifle), so
	// every icon needs to share ONE scale factor, sized so the single
	// largest glyph among all ten just fits 128px and everything else
	// scales down from there, preserving how they relate to each other.
	//
	// Computed once per session (bounds-only, no texture work) the
	// first time any remaster icon is needed.
	static f32 s_remasterIconScale = 0.0f;

	static f32 getRemasterIconScale()
	{
		if (s_remasterIconScale > 0.0f) { return s_remasterIconScale; }

		s32 maxGlyphDim = 0;
		for (s32 i = 0; i < WPN_COUNT; i++)
		{
			const char* fileName = c_remasterIconFile[i];
			if (!fileName) { continue; }

			char key[TFE_MAX_PATH];
			sprintf(key, "gfx/wheel/%s", fileName);
			FilePath resolved;
			if (!TFE_Paths::getFilePath(key, &resolved)) { continue; }

			SDL_Surface* image = TFE_Image::get(resolved.path);
			if (!image) { continue; }

			s32 l, t, r, b;
			if (computeOpaqueBounds((const u32*)image->pixels, image->w, image->h, &l, &t, &r, &b))
			{
				const s32 dim = max(r - l + 1, b - t + 1);
				TFE_System::logWrite(LOG_MSG, "WeaponWheel", "glyph bounds for %s: (%d,%d)-(%d,%d) size=%dx%d maxdim=%d",
					fileName, l, t, r, b, r - l + 1, b - t + 1, dim);
				if (dim > maxGlyphDim) { maxGlyphDim = dim; }
			}
		}

		s_remasterIconScale = (maxGlyphDim > 0) ? ((f32)c_remasterIconSize / (f32)maxGlyphDim) : 1.0f;
		TFE_System::logWrite(LOG_MSG, "WeaponWheel", "maxGlyphDim=%d, shared scale=%f", maxGlyphDim, s_remasterIconScale);
		return s_remasterIconScale;
	}

	// The remaster ships one icon per weapon, no separate selected/
	// unselected art. Per request: dim the unselected variant, brighten
	// the selected one, both derived from the same source image.
	static bool buildRemasterIconPair(s32 wpnId, TFE_FrontEndUI::UiImage* unselected, TFE_FrontEndUI::UiImage* selected)
	{
		const char* fileName = c_remasterIconFile[wpnId];
		if (!fileName) { return false; }

		char key[TFE_MAX_PATH];
		sprintf(key, "gfx/wheel/%s", fileName);

		FilePath resolved;
		if (!TFE_Paths::getFilePath(key, &resolved)) { return false; }

		SDL_Surface* image = TFE_Image::get(resolved.path);
		if (!image) { return false; }

		s32 w = image->w, h = image->h;
		const u32* srcPixels = (const u32*)image->pixels;

		// The remaster's gfx/wheel/*.PNG canvas (640x400) has the weapon
		// art centered with a lot of transparent padding around it -
		// fitting the full canvas into a 128px box makes the actual
		// weapon glyph tiny. Crop to its opaque bounding box first, so
		// what gets resized is just the artwork itself.
		std::vector<u32> cropped;
		s32 cl, ct, cr, cb;
		if (computeOpaqueBounds(srcPixels, w, h, &cl, &ct, &cr, &cb) && (cl > 0 || ct > 0 || cr < w - 1 || cb < h - 1))
		{
			const s32 cw = cr - cl + 1, ch = cb - ct + 1;
			cropped.resize((size_t)cw * ch);
			for (s32 y = 0; y < ch; y++)
			{
				memcpy(&cropped[(size_t)y * cw], &srcPixels[(size_t)(y + ct) * w + cl], (size_t)cw * sizeof(u32));
			}
			srcPixels = cropped.data();
			w = cw;
			h = ch;
		}

		std::vector<u32> resized;
		const u32* src = srcPixels;
		{
			// Use the shared scale (see getRemasterIconScale() above) so
			// this icon's size relative to the other nine is preserved,
			// not just fit to its own bounding box.
			const f32 scale = getRemasterIconScale();
			const s32 dstW = max(1, (s32)(w * scale + 0.5f));
			const s32 dstH = max(1, (s32)(h * scale + 0.5f));

			if (resizeRGBA(srcPixels, w, h, resized, dstW, dstH))
			{
				src = resized.data();
				w = dstW;
				h = dstH;
			}
		}

		std::vector<u32> dim((size_t)w * h), bright((size_t)w * h);

		const f32 c_dimScale = 0.55f;
		const f32 c_brightScale = 1.25f;
		for (s32 p = 0; p < w * h; p++)
		{
			const u32 px = src[p];
			const u8 r = (u8)(px & 0xff), g = (u8)((px >> 8) & 0xff), b = (u8)((px >> 16) & 0xff);
			u8 a = (u8)((px >> 24) & 0xff);
			if (a < 24) { a = 0; }	// snap faint edge fringe fully transparent - see computeOpaqueBounds().

			const u8 dr = (u8)(r * c_dimScale), dg = (u8)(g * c_dimScale), db = (u8)(b * c_dimScale);
			dim[p] = (u32)dr | ((u32)dg << 8) | ((u32)db << 16) | ((u32)a << 24);

			const s32 brR = min(255, (s32)(r * c_brightScale));
			const s32 brG = min(255, (s32)(g * c_brightScale));
			const s32 brB = min(255, (s32)(b * c_brightScale));
			bright[p] = (u32)brR | ((u32)brG << 8) | ((u32)brB << 16) | ((u32)a << 24);
		}

		TextureGpu* texDim = TFE_RenderBackend::createTexture((u32)w, (u32)h, dim.data(), MAG_FILTER_LINEAR);
		TextureGpu* texBright = TFE_RenderBackend::createTexture((u32)w, (u32)h, bright.data(), MAG_FILTER_LINEAR);
		if (!texDim || !texBright) { return false; }

		TFE_System::logWrite(LOG_MSG, "WeaponWheel", "%s final texture size: %dx%d", fileName, w, h);

		unselected->image = TFE_RenderBackend::getGpuPtr(texDim);
		unselected->width = w;
		unselected->height = h;
		selected->image = TFE_RenderBackend::getGpuPtr(texBright);
		selected->width = w;
		selected->height = h;
		return true;
	}

	// Loads both icon variants for every wheel slot, in priority order:
	//  1. A weaponWheel.json override (see TFE_ExternalData/weaponWheelExternal.h).
	//  2. The Dark Forces Remaster's own icon set from DarkEX.kpf, if
	//     the remaster is installed (dimmed/brightened - see
	//     buildRemasterIconPair() above).
	//  3. This mod's own UI_Images/weaponWheel/<name>[_select].png.
	// Lazy and one-shot; a slot with no image at all (all three missing)
	// just falls back to a text label at draw time.
	static void loadWeaponIcons()
	{
		if (s_iconsLoaded) { return; }
		s_iconsLoaded = true;	// Don't retry every frame even on failure.

		extractRemasterIcons();

		char basePath[TFE_MAX_PATH], baseSelectedPath[TFE_MAX_PATH];
		for (s32 i = 0; i < WHEEL_SLOT_COUNT; i++)
		{
			const s32 wpnId = c_wheelSlot[i].wpnId;
			const TFE_ExternalData::WeaponWheelOverride* ov = TFE_ExternalData::getWeaponWheelOverride(wpnId);

			bool gotUnselected = false, gotSelected = false;
			if (ov && ov->hasImageUnselected) { gotUnselected = loadNamedImage(ov->imageUnselected.c_str(), &s_icon[i]); }
			if (ov && ov->hasImageSelected) { gotSelected = loadNamedImage(ov->imageSelected.c_str(), &s_iconSelected[i]); }

			if ((!gotUnselected || !gotSelected) && s_remasterAvailable)
			{
				TFE_FrontEndUI::UiImage dim, bright;
				if (buildRemasterIconPair(wpnId, &dim, &bright))
				{
					if (!gotUnselected) { s_icon[i] = dim; gotUnselected = true; }
					if (!gotSelected) { s_iconSelected[i] = bright; gotSelected = true; }
				}
			}

			if (!gotUnselected)
			{
				sprintf(basePath, "UI_Images/weaponWheel/%s.png", c_wheelSlot[i].iconFile);
				loadGpuImage(basePath, &s_icon[i]);
			}
			if (!gotSelected)
			{
				sprintf(baseSelectedPath, "UI_Images/weaponWheel/%s_select.png", c_wheelSlot[i].iconFile);
				loadGpuImage(baseSelectedPath, &s_iconSelected[i]);
			}
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
		s_remasterIconScale = 0.0f;
		for (s32 i = 0; i < WHEEL_SLOT_COUNT; i++)
		{
			s_icon[i] = TFE_FrontEndUI::UiImage();
			s_iconSelected[i] = TFE_FrontEndUI::UiImage();
		}
	}

	// Builds the list of wheel slots the player currently owns, in wheel
	// display order (see c_wheelSlot above). This is what actually
	// populates the wheel - unowned weapons are skipped entirely,
	// per-slot count varies accordingly.
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

	static bool wheelCanOpen()
	{
		return !TFE_DarkForces::s_gamePaused && !pda_isOpen() && !TFE_DarkForces::s_drawAutomap && !escapeMenu_isOpen();
	}

	void weaponWheel_update()
	{
		const ActionState state = inputMapping_getActionState(IADF_WEAPON_WHEEL);
		const bool held = (state & STATE_ACTIVE) != 0;

		if (!s_wheelOpen)
		{
			if (held && wheelCanOpen())
			{
				loadWeaponIcons();
				s_wheelOpen = true;
				s_cursorX = 0.0f;
				s_cursorY = 0.0f;
				s_hoveredSlot = -1;
				s_disablePlayerFire = JTRUE;
				s_disablePlayerRotation = JTRUE;
			}
			else
			{
				return;
			}
		}

		// Once open, keep it open (even if e.g. the PDA somehow opened
		// mid-hold) until the key is released, so a switch always
		// commits predictably.
		std::vector<s32> owned;
		buildOwnedSlotList(owned);
		const s32 count = (s32)owned.size();

		if (held && count > 0)
		{
			s32 mdx = 0, mdy = 0;
			TFE_Input::getMouseMove(&mdx, &mdy);
			s_cursorX += (f32)mdx;
			s_cursorY += (f32)mdy;

			// Right stick - same axes normally used for camera look
			// (see AA_LOOK_HORZ/AA_LOOK_VERT in player.cpp), repurposed
			// here to steer the wheel cursor instead while it's open.
			const f32 stickX = inputMapping_getAnalogAxis(AA_LOOK_HORZ);
			const f32 stickY = inputMapping_getAnalogAxis(AA_LOOK_VERT);
			if (stickX != 0.0f || stickY != 0.0f)
			{
				const f32 dt = (f32)TFE_System::getDeltaTime();
				s_cursorX += stickX * c_stickSpeed * dt;
				// Positive AA_LOOK_VERT means "look up" (see player.cpp),
				// which should move the cursor up on screen, i.e. toward
				// negative Y.
				s_cursorY -= stickY * c_stickSpeed * dt;
			}

			const f32 dist = sqrtf(s_cursorX * s_cursorX + s_cursorY * s_cursorY);
			if (dist > c_maxCursorRadius)
			{
				const f32 scale = c_maxCursorRadius / dist;
				s_cursorX *= scale;
				s_cursorY *= scale;
			}

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
		}

		// Draw.
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

		// Font size is queried here, inside this window's Begin/End scope,
		// and passed explicitly to every AddText/CalcTextSizeA call below -
		// this window's own scale is never changed via SetWindowFontScale,
		// so none of this should affect any other window's text size.
		ImFont* font = ImGui::GetFont();
		const f32 baseFontSize = ImGui::GetFontSize();
		const f32 labelFontSize = baseFontSize * 2.0f;
		const f32 centerFontSize = baseFontSize * 2.6f;

		// Backdrop.
		draw->AddCircleFilled(ImVec2(cx, cy), wheelRadius, IM_COL32(10, 10, 15, 160), 96);

		const s32 hoveredSlot = (s_hoveredSlot >= 0 && s_hoveredSlot < count) ? owned[s_hoveredSlot] : -1;
		const char* centerName = (hoveredSlot >= 0) ? getWheelSlotName(hoveredSlot) : "";

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

			if (icon.image)
			{
				// Only scale down if this icon exceeds the display box -
				// never scale up. Custom art is already ~128x128 so this
				// is a no-op for it either way, but remaster icons are
				// intentionally different sizes from each other (see
				// getRemasterIconScale() above) and re-normalizing each
				// one to fill 128px here would erase that again.
				const f32 maxDim = 128.0f;
				const f32 largest = (f32)max(icon.width, icon.height);
				const f32 scale = (largest > maxDim) ? (maxDim / largest) : 1.0f;
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

			const s32 ammo = weapon_getAmmoCount(wpnId);
			if (ammo >= 0)
			{
				const s32 secondaryAmmo = weapon_getSecondaryAmmoCount(wpnId);
				char ammoStr[32];
				if (secondaryAmmo >= 0) { sprintf(ammoStr, "%d (%d)", ammo, secondaryAmmo); }
				else { sprintf(ammoStr, "%d", ammo); }
				ImVec2 ts = font->CalcTextSizeA(labelFontSize, 1024.0f, 0.0f, ammoStr);
				draw->AddText(font, labelFontSize, ImVec2(sx - ts.x * 0.5f, sy + 68.0f), IM_COL32(255, 210, 90, 255), ammoStr);
			}
		}

		// Center label. Color matches the classic HUD pickup/notification
		// message text (see s_hudFont / displayHudMessage() in hud.cpp -
		// that color comes from a level-palette-driven bitmap font rather
		// than a simple RGB constant, so this is a close visual match
		// rather than a byte-for-byte sample; let me know if it needs
		// nudging once you see it in-game).
		{
			ImVec2 ts = font->CalcTextSizeA(centerFontSize, 1024.0f, 0.0f, centerName);
			draw->AddText(font, centerFontSize, ImVec2(cx - ts.x * 0.5f + 2, cy - ts.y * 0.5f + 2), IM_COL32(0, 0, 0, 220), centerName);
			draw->AddText(font, centerFontSize, ImVec2(cx - ts.x * 0.5f, cy - ts.y * 0.5f), IM_COL32(255, 213, 65, 255), centerName);
		}

		ImGui::End();

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