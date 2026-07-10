#pragma once
//////////////////////////////////////////////////////////////////////
// Dark Forces Weapon Fire Functions
// Logic for player weapons (not enemy weapons)
//////////////////////////////////////////////////////////////////////
#include "weapon.h"
#include <TFE_System/types.h>
#include <TFE_Jedi/InfSystem/message.h>
#include <TFE_ExternalData/weaponExternal.h>

namespace TFE_DarkForces
{
	typedef void(*WeaponFireFunc)(MessageType msg);

	void setupAnimationFrames(WeaponID weaponId, s32 numPrimFrames, TFE_ExternalData::WeaponAnimFrame* extPrimFrames, s32 numSecondaryFrames, TFE_ExternalData::WeaponAnimFrame* extSecFrames);
	void setupWeaponOffsets(WeaponID weaponId, TFE_ExternalData::ExternalWeapon* extWeapon);

	void resetWeaponFunc();
	void weaponFire_fist(MessageType msg);
	void weaponFire_pistol(MessageType msg);
	void weaponFire_rifle(MessageType msg);
	void weaponFire_thermalDetonator(MessageType msg);
	void weaponFire_repeater(MessageType msg);
	void weaponFire_fusion(MessageType msg);
	void weaponFire_mortar(MessageType msg);
	void weaponFire_mine(MessageType msg);
	void weaponFire_concussion(MessageType msg);
	void weaponFire_cannon(MessageType msg);
}  // namespace TFE_DarkForces