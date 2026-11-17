#pragma once
#include <TFE_System/types.h>
#include <string>

///////////////////////////////////////////
// TFE Externalised Weapon Wheel Overrides
//
// Lets a mod replace the weapon wheel's display name and/or icons for
// individual weapons. Declared in a "weaponWheel.json" placed at the
// root of the mod zip, e.g.:
//
//   {
//       "weapon0": { "name": "Fists", "image_selected": "fists_selected.png", "image_unselected": "fists.png" },
//       "weapon1": { "name": "Bryar Pistol", "image_selected": "bryar_selected.png", "image_unselected": "bryar.png" }
//   }
//
// Keys are "weapon0".."weapon9", matching the WeaponID enum in
// TFE_DarkForces/weapon.h (0 = WPN_FIST .. 9 = WPN_CANNON) - not the
// wheel's own display order (see WheelSlot in weaponWheel.cpp), which
// is a separate, purely-visual ordering concern.
//
// Every field is optional and independent - a mod can override just a
// name, just one image, or all three; anything left out keeps the
// default. "image_selected"/"image_unselected" are filenames looked
// up at the root of the mod zip/folder (e.g. "image_unselected":
// "Stormtrooper_Rifle.png" means a Stormtrooper_Rifle.png sitting
// right at the top level of the mod, not inside any subfolder). If
// that file can't be found in the mod, the base game's own
// UI_Images/weaponWheel/ folder is checked for the same filename
// before finally falling back to that slot's own default icon - see
// loadOneWheelImage() in weaponWheel.cpp. All weapon wheel images
// should be 128x128.
///////////////////////////////////////////

namespace TFE_ExternalData
{
	struct WeaponWheelOverride
	{
		bool        hasName = false;
		std::string name;
		bool        hasImageSelected = false;
		std::string imageSelected;
		bool        hasImageUnselected = false;
		std::string imageUnselected;
	};

	void clearWeaponWheelOverrides();
	void loadWeaponWheelOverrides();
	void parseWeaponWheelOverrides(char* data, bool fromMod);

	// Returns the override entry for the given WeaponID, or nullptr if
	// that weapon has no weaponWheel.json entry at all. Callers should
	// still check the individual has* flags, since a present entry may
	// only override some of its fields.
	const WeaponWheelOverride* getWeaponWheelOverride(s32 wpnId);
}