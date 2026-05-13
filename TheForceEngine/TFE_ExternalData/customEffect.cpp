#include "customEffect.h"


namespace TFE_ExternalData
{
	std::vector<ExternalEffect> s_customEffects;

	
	std::vector<ExternalEffect>* getCustomEffects()
	{
		return &s_customEffects;
	}

	void clearCustomEffects()
	{
		s_customEffects.clear();
	}

	void parseCustomEffects(cJSON* data)
	{
		while (data)
		{
			cJSON* effectType = data->child;

			// get the effect type
			if (effectType && cJSON_IsString(effectType) && strcasecmp(effectType->string, "type") == 0)
			{
				ExternalEffect custEffect = {};
				custEffect.type = effectType->valuestring;	// This will be the name/identifier of the custom effect

				cJSON* effectData = effectType->next;
				if (effectData && cJSON_IsObject(effectData))
				{
					cJSON* dataItem = effectData->child;

					// iterate through the data and assign properties
					while (dataItem)
					{
						tryAssignEffectProperty(dataItem, custEffect);
						dataItem = dataItem->next;
					}
				}

				s_customEffects.push_back(custEffect);
			}

			data = data->next;
		}
	}

	s32 getCustomEffectIndex(const char* name)
	{
		for (u32 i = 0; i < s_customEffects.size(); i++)
		{
			if (strcasecmp(name, s_customEffects[i].type) == 0)
			{
				return i;
			}
		}

		return -1;
	}
}