#include <cstring>
#include <vector>
#include <TFE_System/system.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_DarkForces/projectile.h>
#include <TFE_DarkForces/weapon.h>
#include "weaponExternal.h"
#include "logicTables.h"

namespace TFE_ExternalData
{
	static ExternalProjectile s_externalProjectiles[TFE_DarkForces::PROJ_COUNT];
	static ExternalEffect s_externalEffects[HEFFECT_COUNT];
	static ExternalWeapon s_externalWeapons[TFE_DarkForces::WPN_COUNT];
	static ExternalGasmask s_externalGasmask;

	static bool s_externalProjectilesFromMod = false;
	static bool s_externalEffectsFromMod = false;
	static bool s_externalWeaponsFromMod = false;

	//////////////////////////////
	// Forward Declarations
	//////////////////////////////
	int getProjectileIndex(char* type);
	int getEffectIndex(char* type);
	int getWeaponIndex(char* name);
	bool tryAssignGasmaskProperty(cJSON* data);
	bool tryAssignWeaponProperty(cJSON* data, ExternalWeapon& weapon);
	void parseAnimationFrame(cJSON* element, WeaponAnimFrame& animFrame);


	ExternalProjectile* getExternalProjectiles()
	{
		return s_externalProjectiles;
	}

	ExternalEffect* getExternalEffects()
	{
		return s_externalEffects;
	}

	ExternalWeapon* getExternalWeapons()
	{
		return s_externalWeapons;
	}

	ExternalGasmask* getExternalGasmask()
	{
		return &s_externalGasmask;
	}

	void clearExternalProjectiles()
	{
		s_externalProjectilesFromMod = false;
		for (int i = 0; i < TFE_DarkForces::PROJ_COUNT; i++)
		{
			s_externalProjectiles[i].type = nullptr;
		}
	}

	void clearExternalEffects()
	{
		s_externalEffectsFromMod = false;
		for (int i = 0; i < HEFFECT_COUNT; i++)
		{
			s_externalEffects[i].type = nullptr;
		}
	}

	void clearExternalWeapons()
	{
		s_externalWeaponsFromMod = false;
		for (int i = 0; i < TFE_DarkForces::WPN_COUNT; i++)
		{
			s_externalWeapons[i].name = nullptr;
		}
	}

	void loadExternalProjectiles()
	{
		char extDataFile[TFE_MAX_PATH];
		strcpy(extDataFile, "ExternalData/DarkForces/projectiles.json");
		if (!TFE_Paths::mapSystemPath(extDataFile))
		{
			const char* programDir = TFE_Paths::getPath(PATH_PROGRAM);
			sprintf(extDataFile, "%sExternalData/DarkForces/projectiles.json", programDir);
		}

		TFE_System::logWrite(LOG_MSG, "EXTERNAL_DATA", "Loading projectile data");
		FileStream file;
		if (!file.open(extDataFile, FileStream::MODE_READ)) { return; }

		const size_t size = file.getSize();
		char* data = (char*)malloc(size + 1);
		if (!data || size == 0)
		{
			TFE_System::logWrite(LOG_ERROR, "EXTERNAL_DATA", "Projectiles.json is %u bytes in size and cannot be read.", size);
			return;
		}
		file.readBuffer(data, (u32)size);
		data[size] = 0;
		file.close();

		parseExternalProjectiles(data, false);
		free(data);
	}

	void parseExternalProjectiles(char* data, bool fromMod)
	{
		// If projectiles have already been loaded from a mod, don't replace them
		if (s_externalProjectilesFromMod)
		{
			return;
		}
		
		cJSON* root = cJSON_Parse(data);
		if (root)
		{
			cJSON* section = root->child;
			if (section && cJSON_IsArray(section) && strcasecmp(section->string, "projectiles") == 0)
			{
				cJSON* projectile = section->child;
				while (projectile)
				{
					cJSON* projectileType = projectile->child;

					// get the projectile type
					if (projectileType && cJSON_IsString(projectileType) && strcasecmp(projectileType->string, "type") == 0)
					{
						// For now stick with the hardcoded DF list
						int index = getProjectileIndex(projectileType->valuestring);

						if (index >= 0)
						{
							ExternalProjectile extProjectile = {};
							extProjectile.type = projectileType->valuestring;

							cJSON* projectileData = projectileType->next;
							if (projectileData && cJSON_IsObject(projectileData))
							{
								cJSON* dataItem = projectileData->child;

								// iterate through the data and assign properties
								while (dataItem)
								{
									tryAssignProjectileProperty(dataItem, extProjectile);
									dataItem = dataItem->next;
								}
							}

							s_externalProjectiles[index] = extProjectile;
						}
					}

					projectile = projectile->next;
				}
			}

			s_externalProjectilesFromMod = fromMod && validateExternalProjectiles();
		}
		else
		{
			const char* error = cJSON_GetErrorPtr();
			if (error)
			{
				TFE_System::logWrite(LOG_ERROR, "EXTERNAL_DATA", "Failed to parse json before\n%s", error);
			}
			else
			{
				TFE_System::logWrite(LOG_ERROR, "EXTERNAL_DATA", "Failed to parse json");
			}
		}
	}

	void loadExternalEffects()
	{
		char extDataFile[TFE_MAX_PATH];
		strcpy(extDataFile, "ExternalData/DarkForces/effects.json");
		if (!TFE_Paths::mapSystemPath(extDataFile))
		{
			const char* programDir = TFE_Paths::getPath(PATH_PROGRAM);
			sprintf(extDataFile, "%sExternalData/DarkForces/effects.json", programDir);
		}

		TFE_System::logWrite(LOG_MSG, "EXTERNAL_DATA", "Loading effects data");
		FileStream file;
		if (!file.open(extDataFile, FileStream::MODE_READ)) { return; }

		const size_t size = file.getSize();
		char* data = (char*)malloc(size + 1);
		if (!data || size == 0)
		{
			TFE_System::logWrite(LOG_ERROR, "EXTERNAL_DATA", "Effects.json is %u bytes in size and cannot be read.", size);
			return;
		}
		file.readBuffer(data, (u32)size);
		data[size] = 0;
		file.close();

		parseExternalEffects(data, false);
		free(data);
	}

	void parseExternalEffects(char* data, bool fromMod)
	{
		// If effects have already been loaded from a mod, don't replace them
		if (s_externalEffectsFromMod)
		{
			return;
		}

		cJSON* root = cJSON_Parse(data);
		if (root)
		{
			cJSON* section = root->child;
			if (section && cJSON_IsArray(section) && strcasecmp(section->string, "effects") == 0)
			{
				cJSON* effect = section->child;
				while (effect)
				{
					cJSON* effectType = effect->child;

					// get the effect type
					if (effectType && cJSON_IsString(effectType) && strcasecmp(effectType->string, "type") == 0)
					{
						// For now stick with the hardcoded DF list
						int index = getEffectIndex(effectType->valuestring);

						if (index >= 0)
						{
							ExternalEffect extEffect = {};
							extEffect.type = effectType->valuestring;

							cJSON* effectData = effectType->next;
							if (effectData && cJSON_IsObject(effectData))
							{
								cJSON* dataItem = effectData->child;

								// iterate through the data and assign properties
								while (dataItem)
								{
									tryAssignEffectProperty(dataItem, extEffect);
									dataItem = dataItem->next;
								}
							}

							s_externalEffects[index] = extEffect;
						}
					}

					effect = effect->next;
				}
			}

			s_externalEffectsFromMod = fromMod && validateExternalEffects();
		}
		else
		{
			const char* error = cJSON_GetErrorPtr();
			if (error)
			{
				TFE_System::logWrite(LOG_ERROR, "EXTERNAL_DATA", "Failed to parse json before\n%s", error);
			}
			else
			{
				TFE_System::logWrite(LOG_ERROR, "EXTERNAL_DATA", "Failed to parse json");
			}
		}
	}

	void loadExternalWeapons()
	{
		char extDataFile[TFE_MAX_PATH];
		strcpy(extDataFile, "ExternalData/DarkForces/weapons.json");
		if (!TFE_Paths::mapSystemPath(extDataFile))
		{
			const char* programDir = TFE_Paths::getPath(PATH_PROGRAM);
			sprintf(extDataFile, "%sExternalData/DarkForces/weapons.json", programDir);
		}

		TFE_System::logWrite(LOG_MSG, "EXTERNAL_DATA", "Loading weapon data");
		FileStream file;
		if (!file.open(extDataFile, FileStream::MODE_READ)) { return; }

		const size_t size = file.getSize();
		char* data = (char*)malloc(size + 1);
		if (!data || size == 0)
		{
			TFE_System::logWrite(LOG_ERROR, "EXTERNAL_DATA", "Weapons.json is %u bytes in size and cannot be read.", size);
			return;
		}
		file.readBuffer(data, (u32)size);
		data[size] = 0;
		file.close();

		parseExternalWeapons(data, false);
		free(data);
	}

	void parseExternalWeapons(char* data, bool fromMod)
	{
		// If weapons have already been loaded from a mod, don't replace them
		if (s_externalWeaponsFromMod)
		{
			return;
		}

		cJSON* root = cJSON_Parse(data);
		if (root)
		{
			cJSON* section = root->child;
			while (section)
			{
				if (cJSON_IsObject(section) && strcasecmp(section->string, "gasmask") == 0)
				{
					// parse the gas mask data
					cJSON* dataItem = section->child;
					while (dataItem)
					{
						tryAssignGasmaskProperty(dataItem);
						dataItem = dataItem->next;
					}
				}
				else if (cJSON_IsArray(section) && strcasecmp(section->string, "weapons") == 0)
				{
					cJSON* weapon = section->child;
					while (weapon)
					{
						cJSON* weaponName = weapon->child;

						// get the weapon name
						if (weaponName && cJSON_IsString(weaponName) && strcasecmp(weaponName->string, "name") == 0)
						{
							// For now stick with the hardcoded DF list
							int index = getWeaponIndex(weaponName->valuestring);

							if (index >= 0)
							{
								ExternalWeapon extWeapon = {};
								extWeapon.name = weaponName->valuestring;

								cJSON* weaponData = weaponName->next;
								if (weaponData && cJSON_IsObject(weaponData))
								{
									cJSON* dataItem = weaponData->child;

									// iterate through the data and assign properties
									while (dataItem)
									{
										tryAssignWeaponProperty(dataItem, extWeapon);
										dataItem = dataItem->next;
									}
								}

								s_externalWeapons[index] = extWeapon;
							}
						}

						weapon = weapon->next;
					}
				}

				section = section->next;
			}

			s_externalWeaponsFromMod = fromMod && validateExternalWeapons();
		}
		else
		{
			const char* error = cJSON_GetErrorPtr();
			if (error)
			{
				TFE_System::logWrite(LOG_ERROR, "EXTERNAL_DATA", "Failed to parse json before\n%s", error);
			}
			else
			{
				TFE_System::logWrite(LOG_ERROR, "EXTERNAL_DATA", "Failed to parse json");
			}
		}
	}

	bool validateExternalProjectiles()
	{
		// If the type field is null, we can assume the projectile's data hasn't loaded properly
		for (int i = 0; i < TFE_DarkForces::PROJ_COUNT; i++)
		{
			if (!s_externalProjectiles[i].type)
			{
				return false;
			}
		}

		return true;
	}

	bool validateExternalEffects()
	{
		// If the type field is null, we can assume the effect's data hasn't loaded properly
		for (int i = 0; i < HEFFECT_COUNT; i++)
		{
			if (!s_externalEffects[i].type)
			{
				return false;
			}
		}

		return true;
	}

	bool validateExternalWeapons()
	{
		// If the name field is null, we can assume the weapon's data hasn't loaded properly
		for (int i = 0; i < TFE_DarkForces::WPN_COUNT; i++)
		{
			if (!s_externalWeapons[i].name)
			{
				return false;
			}
		}

		return true;
	}

	int getProjectileIndex(char* type)
	{
		for (int i = 0; i < TFE_DarkForces::PROJ_COUNT; i++)
		{
			if (strcasecmp(type, TFE_ExternalData::df_projectileTable[i]) == 0)
			{
				return i;
			}
		}

		return -1;
	}

	int getEffectIndex(char* type)
	{
		for (int i = 0; i < HEFFECT_COUNT; i++)
		{
			if (strcasecmp(type, TFE_ExternalData::df_effectTable[i]) == 0)
			{
				return i;
			}
		}

		return -1;
	}

	int getWeaponIndex(char* name)
	{
		for (int i = 0; i < TFE_DarkForces::WPN_COUNT; i++)
		{
			if (strcasecmp(name, TFE_ExternalData::df_weaponTable[i]) == 0)
			{
				return i;
			}
		}

		return -1;
	}

	bool tryAssignProjectileProperty(cJSON* data, ExternalProjectile &projectile)
	{
		if (!data)
		{
			return false;
		}

		if (cJSON_IsString(data) && strcasecmp(data->string, "assetType") == 0)
		{
			projectile.assetType = data->valuestring;
			return true;
		}

		if (cJSON_IsString(data) && strcasecmp(data->string, "asset") == 0)
		{
			projectile.asset = data->valuestring;
			return true;
		}

		if (cJSON_IsBool(data) && strcasecmp(data->string, "fullBright") == 0)
		{
			projectile.fullBright = cJSON_IsTrue(data);
			return true;
		}

		if (cJSON_IsBool(data) && strcasecmp(data->string, "zeroWidth") == 0)
		{
			projectile.zeroWidth = cJSON_IsTrue(data);
			return true;
		}

		if (cJSON_IsBool(data) && strcasecmp(data->string, "autoAim") == 0)
		{
			projectile.autoAim = cJSON_IsTrue(data);
			return true;
		}

		if (cJSON_IsBool(data) && strcasecmp(data->string, "movable") == 0)
		{
			projectile.movable = cJSON_IsTrue(data);
			return true;
		}

		if (cJSON_IsString(data) && strcasecmp(data->string, "updateFunc") == 0)
		{
			projectile.updateFunc = data->valuestring;
			return true;
		}
		
		if (cJSON_IsNumber(data) && strcasecmp(data->string, "damage") == 0)
		{
			projectile.damage = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "falloffAmount") == 0)
		{
			projectile.falloffAmount = u32(data->valuedouble * ONE_16);
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "nextFalloffTick") == 0)
		{
			projectile.nextFalloffTick = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "damageFalloffDelta") == 0)
		{
			projectile.damageFalloffDelta = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "minDamage") == 0)
		{
			projectile.minDamage = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "force") == 0)
		{
			projectile.force = u32(data->valuedouble * ONE_16);
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "speed") == 0)
		{
			projectile.speed = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "horzBounciness") == 0)
		{
			projectile.horzBounciness = u32(data->valuedouble * ONE_16);
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "vertBounciness") == 0)
		{
			projectile.vertBounciness = u32(data->valuedouble * ONE_16);
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "bounceCount") == 0)
		{
			projectile.bounceCount = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "reflectVariation") == 0)
		{
			projectile.reflectVariation = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "duration") == 0)
		{
			projectile.duration = u32(data->valuedouble * 145.65);	// 145.65 ticks per second
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "angularSpeed") == 0)
		{
			projectile.homingAngularSpeed = floatToAngle((f32)data->valueint);
			return true;
		}

		if (cJSON_IsString(data) && strcasecmp(data->string, "flightSound") == 0)
		{
			projectile.flightSound = data->valuestring;
			return true;
		}

		if (cJSON_IsString(data) && strcasecmp(data->string, "reflectSound") == 0)
		{
			projectile.reflectSound = data->valuestring;
			return true;
		}

		if (cJSON_IsString(data) && strcasecmp(data->string, "cameraPassSound") == 0)
		{
			projectile.cameraPassSound = data->valuestring;
			return true;
		}

		if (cJSON_IsString(data) && strcasecmp(data->string, "reflectEffect") == 0)
		{
			for (int i = 0; i < HEFFECT_COUNT; i++)
			{
				if (strcasecmp(data->valuestring, TFE_ExternalData::df_effectTable[i]) == 0)
				{
					projectile.reflectEffectId = i;
					return true;
				}
			}

			// Save the effect name to check against custom effects
			projectile.reflectEffectName = data->valuestring;

			return false;
		}

		if (cJSON_IsString(data) && strcasecmp(data->string, "hitEffect") == 0)
		{
			for (int i = 0; i < HEFFECT_COUNT; i++)
			{
				if (strcasecmp(data->valuestring, TFE_ExternalData::df_effectTable[i]) == 0)
				{
					projectile.hitEffectId = i;
					return true;
				}
			}

			// Save the effect name to check against custom effects
			projectile.hitEffectName = data->valuestring;

			return false;
		}

		if (cJSON_IsBool(data) && strcasecmp(data->string, "explodeOnTimeout") == 0)
		{
			projectile.explodeOnTimeout = cJSON_IsTrue(data);
			return true;
		}

		return false;
	}

	bool tryAssignEffectProperty(cJSON* data, ExternalEffect& effect)
	{
		if (!data)
		{
			return false;
		}

		if (cJSON_IsString(data) && strcasecmp(data->string, "wax") == 0)
		{
			effect.wax = data->valuestring;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "force") == 0)
		{
			effect.force = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "damage") == 0)
		{
			effect.damage = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "explosiveRange") == 0)
		{
			effect.explosiveRange = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "wakeupRange") == 0)
		{
			effect.wakeupRange = data->valueint;
			return true;
		}

		if (cJSON_IsString(data) && strcasecmp(data->string, "soundEffect") == 0)
		{
			effect.soundEffect = data->valuestring;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "soundPriority") == 0)
		{
			effect.soundPriority = data->valueint;
			return true;
		}

		return false;
	}

	bool tryAssignGasmaskProperty(cJSON* data)
	{
		if (cJSON_IsString(data) && strcasecmp(data->string, "texture") == 0)
		{
			s_externalGasmask.texture = data->valuestring;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "xPos") == 0)
		{
			s_externalGasmask.xPos = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "yPos") == 0)
		{
			s_externalGasmask.yPos = data->valueint;
			return true;
		}

		return false;
	}

	bool tryAssignWeaponProperty(cJSON* data, ExternalWeapon& weapon)
	{
		if (cJSON_IsNumber(data) && strcasecmp(data->string, "frameCount") == 0)
		{
			weapon.frameCount = data->valueint;
			return true;
		}

		if (cJSON_IsArray(data) && strcasecmp(data->string, "textures") == 0)
		{
			cJSON* element;
			s32 index = 0;
			cJSON_ArrayForEach(element, data)
			{
				if (cJSON_IsString(element))
				{
					weapon.textures[index] = element->valuestring;
					index++;
				}
				if (index >= WEAPON_NUM_TEXTURES) { break; }
			}
		}

		if (cJSON_IsArray(data) && strcasecmp(data->string, "xPos") == 0)
		{
			cJSON* element;
			s32 index = 0;
			cJSON_ArrayForEach(element, data)
			{
				if (cJSON_IsNumber(element))
				{
					weapon.xPos[index] = element->valueint;
					index++;
				}
				if (index >= WEAPON_NUM_TEXTURES) { break; }
			}
		}

		if (cJSON_IsArray(data) && strcasecmp(data->string, "yPos") == 0)
		{
			cJSON* element;
			s32 index = 0;
			cJSON_ArrayForEach(element, data)
			{
				if (cJSON_IsNumber(element))
				{
					weapon.yPos[index] = element->valueint;
					index++;
				}
				if (index >= WEAPON_NUM_TEXTURES) { break; }
			}
		}
		
		if (cJSON_IsString(data) && strcasecmp(data->string, "ammo") == 0)
		{
			if (strcasecmp(data->valuestring, "") == 0)
			{
				weapon.ammo = nullptr;
				return true;
			}
			
			if (strcasecmp(data->valuestring, "ammoEnergy") == 0)
			{
				weapon.ammo = &TFE_DarkForces::s_playerInfo.ammoEnergy;
				return true;
			}

			if (strcasecmp(data->valuestring, "ammoPower") == 0)
			{
				weapon.ammo = &TFE_DarkForces::s_playerInfo.ammoPower;
				return true;
			}

			if (strcasecmp(data->valuestring, "ammoPlasma") == 0)
			{
				weapon.ammo = &TFE_DarkForces::s_playerInfo.ammoPlasma;
				return true;
			}

			if (strcasecmp(data->valuestring, "ammoDetonator") == 0)
			{
				weapon.ammo = &TFE_DarkForces::s_playerInfo.ammoDetonator;
				return true;
			}

			if (strcasecmp(data->valuestring, "ammoShell") == 0)
			{
				weapon.ammo = &TFE_DarkForces::s_playerInfo.ammoShell;
				return true;
			}

			if (strcasecmp(data->valuestring, "ammoMine") == 0)
			{
				weapon.ammo = &TFE_DarkForces::s_playerInfo.ammoMine;
				return true;
			}

			if (strcasecmp(data->valuestring, "ammoMissile") == 0)
			{
				weapon.ammo = &TFE_DarkForces::s_playerInfo.ammoMissile;
				return true;
			}

			return false;
		}

		if (cJSON_IsString(data) && strcasecmp(data->string, "secondaryAmmo") == 0)
		{
			if (strcasecmp(data->valuestring, "ammoEnergy") == 0)
			{
				weapon.secondaryAmmo = &TFE_DarkForces::s_playerInfo.ammoEnergy;
				return true;
			}

			if (strcasecmp(data->valuestring, "ammoPower") == 0)
			{
				weapon.secondaryAmmo = &TFE_DarkForces::s_playerInfo.ammoPower;
				return true;
			}

			if (strcasecmp(data->valuestring, "ammoPlasma") == 0)
			{
				weapon.secondaryAmmo = &TFE_DarkForces::s_playerInfo.ammoPlasma;
				return true;
			}

			if (strcasecmp(data->valuestring, "ammoDetonator") == 0)
			{
				weapon.secondaryAmmo = &TFE_DarkForces::s_playerInfo.ammoDetonator;
				return true;
			}

			if (strcasecmp(data->valuestring, "ammoShell") == 0)
			{
				weapon.secondaryAmmo = &TFE_DarkForces::s_playerInfo.ammoShell;
				return true;
			}

			if (strcasecmp(data->valuestring, "ammoMine") == 0)
			{
				weapon.secondaryAmmo = &TFE_DarkForces::s_playerInfo.ammoMine;
				return true;
			}

			if (strcasecmp(data->valuestring, "ammoMissile") == 0)
			{
				weapon.secondaryAmmo = &TFE_DarkForces::s_playerInfo.ammoMissile;
				return true;
			}

			return false;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "wakeupRange") == 0)
		{
			weapon.wakeupRange = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "variation") == 0)
		{
			weapon.variation = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "primaryFireConsumption") == 0)
		{
			weapon.primaryFireConsumption = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "secondaryFireConsumption") == 0)
		{
			weapon.secondaryFireConsumption = data->valueint;
			return true;
		}

		if (cJSON_IsArray(data) && strcasecmp(data->string, "animFrames") == 0)
		{
			cJSON* element;
			s32 index = 0;
			cJSON_ArrayForEach(element, data)
			{
				if (cJSON_IsObject(element))
				{
					parseAnimationFrame(element, weapon.animFrames[index]);
					index++;
				}
				if (index >= WEAPON_NUM_ANIMFRAMES) { break; }
			}

			weapon.numAnimFrames = index;
			return true;
		}

		if (cJSON_IsArray(data) && strcasecmp(data->string, "animFramesSecondary") == 0)
		{
			cJSON* element;
			s32 index = 0;
			cJSON_ArrayForEach(element, data)
			{
				if (cJSON_IsObject(element))
				{
					parseAnimationFrame(element, weapon.animFramesSecondary[index]);
					index++;
				}
				if (index >= WEAPON_NUM_ANIMFRAMES) { break; }
			}

			weapon.numSecondaryAnimFrames = index;
			return true;
		}
		
		return false;
	}

	void parseAnimationFrame(cJSON* element, WeaponAnimFrame& animFrame)
	{
		cJSON* data = element->child;
		while (data)
		{
			if (cJSON_IsNumber(data) && strcasecmp(data->string, "texture") == 0)
			{
				animFrame.texture = data->valueint;
			}

			if (cJSON_IsNumber(data) && strcasecmp(data->string, "light") == 0)
			{
				animFrame.light = data->valueint;
			}

			if (cJSON_IsNumber(data) && strcasecmp(data->string, "durationNormal") == 0)
			{
				animFrame.durationNormal = data->valueint;
			}

			if (cJSON_IsNumber(data) && strcasecmp(data->string, "durationSupercharge") == 0)
			{
				animFrame.durationSupercharge = data->valueint;
			}

			data = data->next;
		}
	}
}