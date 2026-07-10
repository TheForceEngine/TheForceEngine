#include "customProjectile.h"
#include <cstring>


namespace TFE_ExternalData
{
	std::vector<ExternalProjectile> s_customProjectiles;


	std::vector<ExternalProjectile>* getCustomProjectiles()
	{
		return &s_customProjectiles;
	}
	
	void clearCustomProjectiles()
	{
		s_customProjectiles.clear();
	}

	void parseCustomProjectiles(cJSON* data)
	{
		while (data)
		{
			cJSON* projectileType = data->child;

			// get the projectile type
			if (projectileType && cJSON_IsString(projectileType) && strcasecmp(projectileType->string, "type") == 0)
			{
				ExternalProjectile custProjectile = {};
				custProjectile.type = projectileType->valuestring;	// This will be the name/identifier of the custom projectile

				cJSON* projectileData = projectileType->next;
				if (projectileData && cJSON_IsObject(projectileData))
				{
					cJSON* dataItem = projectileData->child;

					// iterate through the data and assign properties
					while (dataItem)
					{
						tryAssignProjectileProperty(dataItem, custProjectile);
						dataItem = dataItem->next;
					}
				}

				s_customProjectiles.push_back(custProjectile);
			}

			data = data->next;
		}
	}

	s32 getCustomProjectileIndex(const char* name)
	{
		for (u32 i = 0; i < s_customProjectiles.size(); i++)
		{
			if (strcasecmp(name, s_customProjectiles[i].type) == 0)
			{
				return i;
			}
		}

		return -1;
	}
}