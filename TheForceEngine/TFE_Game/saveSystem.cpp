#include "saveSystem.h"
#include <TFE_Asset/imageAsset.h>
#include <TFE_DarkForces/hud.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_ExternalData/dfLogics.h>
#include <TFE_ExternalData/weaponExternal.h>
#include <TFE_ExternalData/pickupExternal.h>
#include <TFE_Settings/gameSourceData.h>
#include <TFE_System/system.h>
#include <cassert>
#include <cstring>

using namespace TFE_Input;

namespace TFE_SaveSystem
{
	enum SaveRequest
	{
		SF_REQ_NONE = 0,
		SF_REQ_SAVE,
		SF_REQ_LOAD,
	};

	enum SaveMasterVersion
	{
		SVER_INIT = 1,
		SVER_REPLAY = 7,
		SVER_CUR = SVER_REPLAY
	};

	const int TFE_MAX_SAVES = 1024; 

	static SaveRequest s_req = SF_REQ_NONE;
	static char s_reqFilename[TFE_MAX_PATH];
	static char s_reqSavename[TFE_MAX_PATH];
	static char s_gameSavePath[TFE_MAX_PATH];
	static IGame* s_game = nullptr;
	static s32 s_saveDelay = 0;

	static u32* s_imageBuffer[2] = { nullptr, nullptr };
	static size_t s_imageBufferSize[2] = { 0 };

	bool versionValid(s32 version)
	{
		return version == SVER_CUR;
	}

	void saveHeader(Stream* stream, const char* saveName)
	{
		// Generate a screenshot.
		DisplayInfo displayInfo;
		TFE_RenderBackend::getDisplayInfo(&displayInfo);
		size_t size = displayInfo.width * displayInfo.height * 4;
		if (size > s_imageBufferSize[0])
		{
			s_imageBuffer[0] = (u32*)realloc(s_imageBuffer[0], size);
			s_imageBufferSize[0] = size;
		}
		TFE_RenderBackend::captureScreenToMemory(s_imageBuffer[0]);
		// Save to memory.
		u8* png = (u8*)malloc(SAVE_IMAGE_WIDTH * SAVE_IMAGE_HEIGHT * 4);
		u32 pngSize = 0;
		if (png)
		{
			pngSize = (u32)TFE_Image::writeImageToMemory(png, displayInfo.width, displayInfo.height,
								 SAVE_IMAGE_WIDTH, SAVE_IMAGE_HEIGHT,
								 s_imageBuffer[0]);
		}
		else
		{
			pngSize = 0;
			png = (u8*)s_imageBuffer[0];
		}

		// Master version.
		u32 version = SVER_CUR;
		stream->write(&version);

		// Save Name.
		size_t saveNameLen = strlen(saveName);
		if (saveNameLen > SAVE_MAX_NAME_LEN - 1) { saveNameLen = SAVE_MAX_NAME_LEN - 1; }
		u8 len = (u8)saveNameLen;
		stream->write(&len);
		stream->writeBuffer(saveName, len);

		// Time and Date of Save.
		char timeDate[256];
		TFE_System::getDateTimeString(timeDate);
		len = (u8)strlen(timeDate);
		stream->write(&len);
		stream->writeBuffer(timeDate, len);

		// Level Name
		char levelName[256];
		s_game->getLevelName(levelName);
		len = (u8)strlen(levelName);
		stream->write(&len);
		stream->writeBuffer(levelName, len);

		//Level ID
		char levelId[256];
		s_game->getLevelId(levelId);
		len = (u8)strlen(levelId);
		stream->write(&len);
		stream->writeBuffer(levelId, len);

		// For Replays - Counter ID
		int counter = inputMapping_getCounter();		
		len = sizeof(counter);
		stream->write(&len);
		stream->writeBuffer(&counter, len);

		// Mod List
		char modList[256];
		s_game->getModList(modList);
		len = (u8)strlen(modList);
		stream->write(&len);
		stream->writeBuffer(modList, len);

		// Image.
		stream->write(&pngSize);
		stream->writeBuffer(png, pngSize);
		free(png);
	}

	void loadHeader(Stream* stream, SaveHeader* header, const char* fileName)
	{
		// Master version.
		u32 version;
		stream->read(&version);
		header->saveVersion = version;

		// Save Name.
		u8 len;
		stream->read(&len);
		stream->readBuffer(header->saveName, len);
		header->saveName[len] = 0;
		// Fix existing invalid save names.
		header->saveName[SAVE_MAX_NAME_LEN - 1] = 0;

		// Handle the case when there is no save name.
		if (header->saveName[0] == 0 || header->saveName[0] == ' ')
		{
			FileUtil::getFileNameFromPath(fileName, header->saveName);
		}

		// Time and Date of Save.
		stream->read(&len);
		stream->readBuffer(header->dateTime, len);
		header->dateTime[len] = 0;

		// Level Name
		stream->read(&len);
		stream->readBuffer(header->levelName, len);
		header->levelName[len] = 0;

		if (version >= SVER_REPLAY)
		{
			// Level ID
			stream->read(&len);
			stream->readBuffer(header->levelId, len);
			header->levelId[len] = 0;

			// Counter
			stream->read(&len);
			stream->readBuffer(&header->replayCounter, len);
		}

		// Mod List
		stream->read(&len);
		stream->readBuffer(header->modNames, len);
		header->modNames[len] = 0;

		// Image, re-use buffer 0 for the PNG.
		u32 pngSize;
		stream->read(&pngSize);
		if (pngSize > s_imageBufferSize[0])
		{
			s_imageBuffer[0] = (u32*)realloc(s_imageBuffer[0], pngSize);
			s_imageBufferSize[0] = pngSize;
		}
		stream->readBuffer(s_imageBuffer[0], pngSize);

		SDL_Surface* image;
		TFE_Image::readImageFromMemory(&image, pngSize, s_imageBuffer[0]);
		if (image)
		{
			const u32 sz = SAVE_IMAGE_WIDTH * SAVE_IMAGE_HEIGHT * sizeof(u32);
			memcpy(header->imageData, image->pixels, sz);
			TFE_Image::free(image);
		}
	}

	void populateSaveDirectory(std::vector<SaveHeader>& dir)
	{
		dir.clear();
		FileList fileList;
		FileUtil::readDirectory(s_gameSavePath, "tfe", fileList);
		size_t saveCount = fileList.size();
		dir.resize(saveCount);

		const std::string* filenames = fileList.data();
		SaveHeader* headers = dir.data();
		for (size_t i = 0; i < saveCount; i++)
		{
			loadGameHeader(filenames[i].c_str(), &headers[i]);
		}
	}

	void init()
	{
	}

	void destroy()
	{
		for (s32 i = 0; i < 2; i++)
		{
			free(s_imageBuffer[i]);
			s_imageBufferSize[i] = 0;
			s_imageBuffer[i] = nullptr;
		}
	}

	bool saveGame(const char* filename, const char* saveName)
	{
		char filePath[TFE_MAX_PATH];
		sprintf(filePath, "%s%s", s_gameSavePath, filename);

		bool ret = false;
		FileStream stream;
		if (stream.open(filePath, Stream::MODE_WRITE))
		{
			saveHeader(&stream, saveName);
			ret = s_game->serializeGameState(&stream, filename, true);
			stream.close();
		}
		return ret;
	}

	bool loadGame(const char* filename)
	{
		char filePath[TFE_MAX_PATH];
		sprintf(filePath, "%s%s", s_gameSavePath, filename);

		bool ret = false;
		FileStream stream;
		if (stream.open(filePath, Stream::MODE_READ))
		{
			SaveHeader header;
			loadHeader(&stream, &header, filename);
			
			// Clear out custom logics and external data before loading
			TFE_ExternalData::getExternalLogics()->actorLogics.clear();
			TFE_ExternalData::clearExternalWeapons();
			TFE_ExternalData::clearExternalProjectiles();
			TFE_ExternalData::clearExternalEffects();
			TFE_ExternalData::clearExternalPickups();

			ret = s_game->serializeGameState(&stream, filename, false);
			stream.close();
		}
		return ret;
	}

	bool loadGameHeader(const char* filename, SaveHeader* header)
	{
		char filePath[TFE_MAX_PATH];
		sprintf(filePath, "%s%s", s_gameSavePath, filename);

		bool ret = false;
		FileStream stream;
		if (stream.open(filePath, Stream::MODE_READ))
		{
			loadHeader(&stream, header, filename);
			strcpy(header->fileName, filename);
			stream.close();
			ret = true;
		}
		return ret;
	}
		
	void postLoadRequest(const char* filename)
	{
		s_req = SF_REQ_LOAD;
		strcpy(s_reqFilename, filename);
	}

	void postSaveRequest(const char* filename, const char* saveName, s32 delay)
	{
		s_req = SF_REQ_SAVE;
		strcpy(s_reqFilename, filename);
		strcpy(s_reqSavename, saveName);
		s_saveDelay = delay;
	}

	const char* loadRequestFilename()
	{
		if (s_req == SF_REQ_LOAD)
		{
			s_req = SF_REQ_NONE;
			return s_reqFilename;
		}
		return nullptr;
	}

	const char* saveRequestFilename()
	{
		if (s_req == SF_REQ_SAVE && s_saveDelay <= 0)
		{
			s_req = SF_REQ_NONE;
			return s_reqFilename;
		}
		if (s_saveDelay > 0) { s_saveDelay--; }
		return nullptr;
	}

	void getSaveFilenameFromIndex(s32 index, char* name)
	{
		if (index == 0)
		{
			strcpy(name, c_quickSaveName);
		}
		else
		{
			sprintf(name, "save%03d.tfe", index - 1);
		}
	}

	void setCurrentGame(GameID id)
	{
		char relativeBasePath[TFE_MAX_PATH];
		TFE_Paths::appendPath(PATH_USER_DOCUMENTS, "Saves/", relativeBasePath);
		if (!FileUtil::directoryExists(s_gameSavePath))
		{
			FileUtil::makeDirectory(relativeBasePath);
		}

		char relativePath[TFE_MAX_PATH];
		sprintf(relativePath, "Saves/%s/", TFE_Settings::c_gameName[id]);

		TFE_Paths::appendPath(PATH_USER_DOCUMENTS, relativePath, s_gameSavePath);
		if (!FileUtil::directoryExists(s_gameSavePath))
		{
			FileUtil::makeDirectory(s_gameSavePath);
		}
	}
		
	void setCurrentGame(IGame* game)
	{
		s_game = game;
		setCurrentGame(game->id);
	}

	IGame* getCurrentGame()
	{
		return s_game;
	}

	void update()
	{
		if (!s_game) { return; }

		static s32 lastState = 0;
		const char* saveFilename = saveRequestFilename();

		bool canSave = !lastState && s_game->canSave();
		if (isReplaySystemLive())
		{
			// no saving or loading during replay system
			return;
		}
		else if (saveFilename && canSave)
		{
			saveGame(saveFilename, s_reqSavename);
			lastState = 1;
		}
		else if (inputMapping_getActionState(IAS_QUICK_SAVE) == STATE_PRESSED && canSave)
		{
			saveGame(c_quickSaveName, "Quicksave");
			lastState = 1;
		}
		else if (inputMapping_getActionState(IAS_QUICK_LOAD) == STATE_PRESSED && !lastState)
		{
			char filePath[TFE_MAX_PATH];
			sprintf(filePath, "%s%s", s_gameSavePath, c_quickSaveName);
			if (FileUtil::exists(filePath))
			{
				postLoadRequest(c_quickSaveName);
				lastState = 1;
			}
			else
			{
				TFE_DarkForces::hud_sendTextMessage("No Quicksave Found", 0, false);
				lastState = 0;
			}
		}
		else
		{
			lastState = 0;
		}
	}

	void getSaveFilename(char* filename, s32 index)
	{
		char saveFilePath[TFE_MAX_PATH];
		TFE_SaveSystem::getSaveFilenameFromIndex(index, filename);
		sprintf(saveFilePath, "%s%s", s_gameSavePath, filename);
		
		// If the file doesn't exist or we are overwriting, use the saveFilePath - ex: save015.tfe
		if (!FileUtil::exists(saveFilePath))
		{
			filename = saveFilePath;
			return;
		}
		else
		{
			// If the file already exists you must have deleted an older one so lets find the right index
			// Ex: save000.tfe save001.tfe save003.tfe (skipped 2) or you have custom save names. 
			for (int i = 0; i < TFE_MAX_SAVES; i++)
			{
				TFE_SaveSystem::getSaveFilenameFromIndex(i, filename);
				sprintf(saveFilePath, "%s%s", s_gameSavePath, filename);

				if (!FileUtil::exists(saveFilePath))
				{
					filename = saveFilePath;
					return;
				}
			}
		}

		TFE_System::logWrite(LOG_MSG, "SaveSystem", "Unable to create a save file after %d attempts", TFE_MAX_SAVES);
		assert(0);				
	}
}