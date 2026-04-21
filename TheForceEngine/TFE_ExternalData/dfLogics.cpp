#include <cstring>
#include <vector>
#include <TFE_System/cJSON.h>
#include <TFE_System/system.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_DarkForces/time.h>
#include "dfLogics.h"
#include "logicTables.h"

namespace TFE_ExternalData
{
	static ExternalLogics s_externalLogics = {};

	///////////////////////////
	/// Forward declarations 
	///////////////////////////
	void parseLogicData(char* data, const char* filename, std::vector<CustomActorLogic>& actorLogics);
	bool tryAssignProperty(cJSON* data, CustomActorLogic& customLogic);


	ExternalLogics* getExternalLogics()
	{
		return &s_externalLogics;
	}

	void loadCustomLogics()
	{
		std::vector<string> jsons;
		TFE_Paths::getAllFilesFromSearchPaths("logics", "json", jsons);

		TFE_System::logWrite(LOG_MSG, "LOGICS", "Parsing logic JSON(s)");
		for (u32 i = 0; i < jsons.size(); i++)
		{
			FileStream file;
			const char* fileName = jsons[i].c_str();
			if (!file.open(fileName, FileStream::MODE_READ)) { return; }

			const size_t size = file.getSize();
			char* data = (char*)malloc(size + 1);
			if (!data || size == 0)
			{
				TFE_System::logWrite(LOG_ERROR, "LOGICS", "JSON found but is %u bytes in size and cannot be read.", size);
				return;
			}
			file.readBuffer(data, (u32)size);
			data[size] = 0;
			file.close();

			parseLogicData(data, fileName, s_externalLogics.actorLogics);
			free(data);
		}
	}

	void parseLogicData(char* data, const char* filename, std::vector<CustomActorLogic>& actorLogics)
	{
		cJSON* root = cJSON_Parse(data);
		if (root)
		{
			cJSON* section = root->child;
			if (section && cJSON_IsArray(section) && strcasecmp(section->string, "logics") == 0)
			{
				cJSON* logic = section->child;
				while (logic)
				{
					cJSON* logicName = logic->child;

					// get the logic name
					if (logicName && cJSON_IsString(logicName) && strcasecmp(logicName->string, "logicname") == 0)
					{
						CustomActorLogic customLogic;
						customLogic.logicName = logicName->valuestring;

						cJSON* logicData = logicName->next;
						if (logicData && cJSON_IsObject(logicData))
						{
							cJSON* dataItem = logicData->child;

							// iterate through the data and assign properties
							while (dataItem)
							{
								tryAssignProperty(dataItem, customLogic);
								dataItem = dataItem->next;
							}
						}

						actorLogics.push_back(customLogic);
					}

					logic = logic->next;
				}
			}
		}
		else
		{
			const char* error = cJSON_GetErrorPtr();
			if (error)
			{
				TFE_System::logWrite(LOG_ERROR, "LOGICS", "Failed to parse %s before\n%s", filename, error);
			}
			else
			{
				TFE_System::logWrite(LOG_ERROR, "LOGICS", "Failed to parse %s", filename);
			}
		}
	}

	bool tryAssignProperty(cJSON* data, CustomActorLogic &customLogic)
	{
		if (!data)
		{
			return false;
		}

		if (cJSON_IsBool(data) && strcasecmp(data->string, "hasGravity") == 0)
		{
			customLogic.hasGravity = cJSON_IsTrue(data);
			return true;
		}

		if (cJSON_IsBool(data) && strcasecmp(data->string, "isFlying") == 0)
		{
			customLogic.isFlying = cJSON_IsTrue(data);
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "fieldOfView") == 0)
		{
			customLogic.fov = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "awareRange") == 0)
		{
			customLogic.awareRange = data->valueint;
			return true;
		}

		if (cJSON_IsString(data) && strcasecmp(data->string, "alertSound") == 0)
		{
			customLogic.alertSound = data->valuestring;
			return true;
		}

		if (cJSON_IsString(data) && strcasecmp(data->string, "painSound") == 0)
		{
			customLogic.painSound = data->valuestring;
			return true;
		}

		if (cJSON_IsString(data) && strcasecmp(data->string, "dieSound") == 0)
		{
			customLogic.dieSound = data->valuestring;
			return true;
		}

		if (cJSON_IsString(data) && strcasecmp(data->string, "attack1Sound") == 0)
		{
			customLogic.attack1Sound = data->valuestring;
			return true;
		}

		if (cJSON_IsString(data) && strcasecmp(data->string, "attack2Sound") == 0)
		{
			customLogic.attack2Sound = data->valuestring;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "hitPoints") == 0)
		{
			customLogic.hitPoints = data->valueint;
			return true;
		}

		// Drop item as number
		if (cJSON_IsNumber(data) && strcasecmp(data->string, "dropItem") == 0)
		{
			customLogic.dropItem = data->valueint;
			return true;
		}

		// Drop item as string
		if (cJSON_IsString(data) && strcasecmp(data->string, "dropItem") == 0)
		{
			for (int i = 0; i <= 44; i++)
			{
				if (strcasecmp(data->valuestring, df_dropItemTable[i]) == 0)
				{
					customLogic.dropItem = i;
					return true;
				}
			}

			return false;
		}

		// Die effect as a number
		if (cJSON_IsNumber(data) && strcasecmp(data->string, "dieEffect") == 0)
		{
			customLogic.dieEffect = data->valueint;
			return true;
		}

		// Die effect as a string
		if (cJSON_IsString(data) && strcasecmp(data->string, "dieEffect") == 0)
		{
			for (int i = 0; i <= 17; i++)
			{
				if (strcasecmp(data->valuestring, df_effectTable[i]) == 0)
				{
					customLogic.dieEffect = i;
					return true;
				}
			}

			return false;
		}

		if (cJSON_IsBool(data) && strcasecmp(data->string, "hasMeleeAttack") == 0)
		{
			customLogic.hasMeleeAttack = cJSON_IsTrue(data);
			return true;
		}

		if (cJSON_IsBool(data) && strcasecmp(data->string, "hasRangedAttack") == 0)
		{
			customLogic.hasRangedAttack = cJSON_IsTrue(data);
			return true;
		}

		if (cJSON_IsBool(data) && strcasecmp(data->string, "litWithMeleeAttack") == 0)
		{
			customLogic.litWithMeleeAttack = cJSON_IsTrue(data);
			return true;
		}

		if (cJSON_IsBool(data) && strcasecmp(data->string, "litWithRangedAttack") == 0)
		{
			customLogic.litWithRangedAttack = cJSON_IsTrue(data);
			return true;
		}

		// Projectile as number
		if (cJSON_IsNumber(data) && strcasecmp(data->string, "projectile") == 0)
		{
			customLogic.projectile = data->valueint;
			return true;
		}

		// Projectile as string
		if (cJSON_IsString(data) && strcasecmp(data->string, "projectile") == 0)
		{
			for (int i = 0; i <= 18; i++)
			{
				if (strcasecmp(data->valuestring, df_projectileTable[i]) == 0)
				{
					customLogic.projectile = i;
					return true;
				}
			}

			return false;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "rangedAttackDelay") == 0)
		{
			customLogic.rangedAttackDelay = u32(data->valuedouble * TICKS_PER_SECOND);
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "meleeAttackDelay") == 0)
		{
			customLogic.meleeAttackDelay = u32(data->valuedouble * TICKS_PER_SECOND);
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "wanderTime") == 0)
		{
			customLogic.losDelay = data->valueint * TICKS_PER_SECOND;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "meleeRange") == 0)
		{
			customLogic.meleeRange = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "meleeDamage") == 0)
		{
			customLogic.meleeDamage = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "meleeRate") == 0)
		{
			customLogic.meleeRate = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "minAttackDist") == 0)
		{
			customLogic.minAttackDist = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "maxAttackDist") == 0)
		{
			customLogic.maxAttackDist = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "fireSpread") == 0)
		{
			customLogic.fireSpread = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "speed") == 0)
		{
			customLogic.speed = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "verticalSpeed") == 0)
		{
			customLogic.verticalSpeed = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "rotationSpeed") == 0)
		{
			customLogic.rotationSpeed = data->valueint;
			return true;
		}

		if (cJSON_IsBool(data) && strcasecmp(data->string, "stopOnDamage") == 0)
		{
			customLogic.stopOnDamage = cJSON_IsTrue(data);
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "approachVariation") == 0)
		{
			customLogic.approachVariation = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "approachOffset") == 0)
		{
			customLogic.approachOffset = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "thinkerDelay") == 0)
		{
			customLogic.thinkerDelay = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "collisionWidth") == 0)
		{
			customLogic.collisionWidth = data->valuedouble;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "collisionHeight") == 0)
		{
			customLogic.collisionHeight = data->valuedouble;
			return true;
		}

		if (cJSON_IsBool(data) && strcasecmp(data->string, "officerAlerts") == 0)
		{
			customLogic.officerAlerts = cJSON_IsTrue(data);
			return true;
		}

		if (cJSON_IsBool(data) && strcasecmp(data->string, "troopAlerts") == 0)
		{
			customLogic.troopAlerts = cJSON_IsTrue(data);
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "stepUpHeight") == 0)
		{
			customLogic.stepUpHeight = data->valuedouble;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "stepDownHeight") == 0)
		{
			customLogic.stepDownHeight = data->valuedouble;
			return true;
		}

		if (cJSON_IsBool(data) && strcasecmp(data->string, "slideOnCollision") == 0)
		{
			customLogic.slideOnCollision = cJSON_IsTrue(data) ? 1 : 0;
			return true;
		}

		if (cJSON_IsBool(data) && strcasecmp(data->string, "hasBurstFire") == 0)
		{
			customLogic.hasBurstFire = cJSON_IsTrue(data);
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "burstNumber") == 0)
		{
			customLogic.burstNumber = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "burstVariation") == 0)
		{
			customLogic.burstVariation = data->valueint;
			return true;
		}

		if (cJSON_IsNumber(data) && strcasecmp(data->string, "burstInterval") == 0)
		{
			customLogic.burstInterval = (u32)(data->valuedouble * TICKS_PER_SECOND);
			return true;
		}

		// When it comes to offsets these are considered from the perspective of the actor.
		//
		// Projectile spawn details guide.
		// 
		// X value positive =  projectile spawns to the LEFT of actor
		// X value negative =  projectile spawns to the RIGHT of actor 
		// Z value postivie =  projectile spawns in FRONT of actor
		// Z value negative =  projectile spawns BEHIND the actor
		// Y value positive =  projectile spawns ABOVE the actor
		// Y value negative =  projectile spawns BELOW the actor
		//
		if (cJSON_IsArray(data) && strcasecmp(data->string, "fireOffset") == 0)
		{
			if (cJSON_GetArraySize(data) == 3)
			{
				customLogic.fireOffset.x = -cJSON_GetArrayItem(data, 0)->valuedouble;
				customLogic.fireOffset.y = -cJSON_GetArrayItem(data, 1)->valuedouble;
				customLogic.fireOffset.z = cJSON_GetArrayItem(data, 2)->valuedouble;
				return true;
			}
			return false;
		}

		return false;
	}
}
