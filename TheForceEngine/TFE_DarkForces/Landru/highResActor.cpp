#include "highResActor.h"
#include "lsystem.h"
#include "lactor.h"
#include "lcanvas.h"
#include "ldraw.h"
#include <TFE_System/system.h>
#include <TFE_Asset/imageAsset.h>
#include <TFE_Memory/memoryRegion.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_Settings/settings.h>

namespace TFE_DarkForces
{
	SDL_Surface* getPngFromFile(const char* filePath)
	{
		if (!FileUtil::exists(filePath))
		{
			return nullptr;
		}

		return TFE_Image::get(filePath);
	}

	HighResActor* highResActor_loadFromPng(const char* lfdName, const char* actorName, u16 count, bool anim/*default true*/)
	{
		HighResActor* actor = actorHighRes_alloc();
		if (!actor)
		{
			return nullptr;
		}

		u32** array = (u32**)TFE_Memory::region_alloc(s_alloc, sizeof(u32*) * count);
		s32 arraySize = 0;

		const char* actorType = anim ? "ANIM" : "DELT";
		const char* formatStr = anim ? "%slfd/enhanced/%s/%s%s_%d.png" : "%slfd/enhanced/%s/%s%s.png";

		for (s32 i = 0; i < count; i++)
		{
			char path[TFE_MAX_PATH];
			sprintf(
				path,
				formatStr,
				TFE_Settings::getGameHeader("Dark Forces")->sourcePath,
				lfdName,
				actorType,
				actorName,
				i);
			
			SDL_Surface* pngData = getPngFromFile(path);
			if (!pngData)
			{ 
				TFE_System::logWrite(LOG_ERROR, "loadFromPng", "Unable to load PNG %s", path);
				continue;
			}

			array[i] = (u32*)pngData->pixels;
			actor->imageSizes[i] = pngData->w * pngData->h;
			arraySize++;
		}

		if (arraySize != count)
		{
			TFE_System::logWrite(LOG_ERROR, "loadFromPng", "Error - Number of PNGs does not match number of DELTs");
		}

		actor->array = array;
		actor->arraySize = arraySize;
		return actor;
	}
}