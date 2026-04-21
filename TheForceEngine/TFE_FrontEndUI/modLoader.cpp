#include "modLoader.h"
#include "frontEndUi.h"
#include "console.h"
#include "uiTexture.h"
#include "profilerView.h"
#include <TFE_DarkForces/config.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_System/system.h>
#include <TFE_System/parser.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_Archive/archive.h>
#include <TFE_Settings/settings.h>
#include <TFE_Asset/imageAsset.h>
#include <TFE_Archive/zipArchive.h>
#include <TFE_Archive/gobMemoryArchive.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_Asset/imageAsset.h>
#include <TFE_Ui/ui.h>
#include <TFE_Ui/markdown.h>
// Game
#include <TFE_DarkForces/mission.h>
#include <TFE_Jedi/Renderer/jediRenderer.h>
#include <map>
#include <algorithm>

using namespace TFE_Input;

namespace TFE_FrontEndUI
{
	enum QueuedReadType
	{
		QREAD_DIR = 0,
		QREAD_ZIP,
		QREAD_COUNT
	};
	const u32 c_itemsPerFrame = 1;

	struct QueuedRead
	{
		QueuedReadType type;
		std::string path;
		std::string fileName;
	};

	struct ModData
	{
		std::vector<std::string> gobFiles;
		std::string textFile;
		std::string imageFile;
		UiTexture image;

		std::string name;
		std::string relativePath;
		std::string text;

		bool invertImage = true;
	};
	static std::vector<ModData> s_mods;
	static std::vector<ModData*> s_filteredMods;

	static std::vector<char> s_fileBuffer;
	static std::vector<u8> s_readBuffer[2];
	static s32 s_selectedMod;

	static std::vector<QueuedRead> s_readQueue;
	static std::vector<u8> s_imageBuffer;
	static size_t s_readIndex = 0;

	static ViewMode s_viewMode = VIEW_IMAGES;

	static char programDirModDir[TFE_MAX_PATH];
	static char s_modFilter[256] = { 0 };
	static char s_prevModFilter[256] = { 0 };
	static size_t s_filterLen = 0;
	static bool s_filterUpdated = false;
	static bool s_modsRead = false;

	void fixupName(char* name);
	void readFromQueue(size_t itemsPerFrame);
	bool parseNameFromText(const char* textFileName, const char* path, char* name, std::string* fullText);
	void extractPosterFromImage(const char* baseDir, const char* zipFile, const char* imageFileName, UiTexture* poster);
	bool extractPosterFromMod(const char* baseDir, const char* archiveFileName, UiTexture* poster);
	void filterMods(bool filterByName, bool sort = true);

	bool sortQueueByName(QueuedRead& a, QueuedRead& b)
	{
		return strcasecmp(a.fileName.c_str(), b.fileName.c_str()) < 0;
	}

	void createModDirIfNeeded(char * directory)
	{
		TFE_Paths::fixupPathAsDirectory(directory);
		if (!FileUtil::directoryExists(directory))
		{
			FileUtil::makeDirectory(directory);
		}
	}

	
	void modLoader_read()
	{
		// Only read the mods once unless you are looking at the mod UI and there are not mods.
		if (s_modsRead && (s_mods.size() > 0 || !isModUI())) { return; }
		s_modsRead = true;

		s_mods.clear();
		s_filteredMods.clear();
		s_selectedMod = -1;
		clearSelectedMod();

		s_readQueue.clear();
		s_readIndex = 0;

		// There are 3 possible mod directory locations:
		// In the TFE directory,
		// In the original source data.
		// In ProgramData/
		char sourceDataModDir[TFE_MAX_PATH];
		snprintf(sourceDataModDir, TFE_MAX_PATH, "%sMods/", TFE_Paths::getPath(PATH_SOURCE_DATA));
		TFE_Paths::fixupPathAsDirectory(sourceDataModDir);

		// Add Mods/ paths to the program data directory and local executable directory.
		// Note only directories that exist are actually added.
		const char* programData = TFE_Paths::getPath(PATH_PROGRAM_DATA);
		const char* programDir  = TFE_Paths::getPath(PATH_PROGRAM);

		char programDataModDir[TFE_MAX_PATH];
		sprintf(programDataModDir, "%sMods/", programData);
		TFE_Paths::fixupPathAsDirectory(programDataModDir);

		sprintf(programDirModDir, "%sMods/", programDir);
		TFE_Paths::fixupPathAsDirectory(programDirModDir);
		createModDirIfNeeded(programDirModDir);

		s32 modPathCount = 0;
		char modPaths[3][TFE_MAX_PATH];
		if (FileUtil::directoryExists(sourceDataModDir))
		{
			strcpy(modPaths[modPathCount], sourceDataModDir);
			modPathCount++;
		}
		if (FileUtil::directoryExists(programDataModDir))
		{
			strcpy(modPaths[modPathCount], programDataModDir);
			modPathCount++;
		}
		if (FileUtil::directoryExists(programDirModDir))
		{
			strcpy(modPaths[modPathCount], programDirModDir);
			modPathCount++;
		}

		if (!modPathCount)
		{
			s_modsRead = false;
			return;
		}
		
		FileList dirList, zipList;
		for (s32 i = 0; i < modPathCount; i++)
		{
			dirList.clear();
			FileUtil::readSubdirectories(modPaths[i], dirList);
			if (dirList.empty()) { continue; }

			const size_t count = dirList.size();
			const std::string* dir = dirList.data();
			for (size_t d = 0; d < count; d++)
			{
				s_readQueue.push_back({ QREAD_DIR, dir[d], "" });
			}
		}
		// Read Zip Files.
		for (s32 i = 0; i < modPathCount; i++)
		{
			zipList.clear();
			FileUtil::readDirectory(modPaths[i], "zip", zipList);
			size_t count = zipList.size();
			for (size_t z = 0; z < count; z++)
			{
				s_readQueue.push_back({ QREAD_ZIP, modPaths[i], zipList[z] });
			}
		}

		// De-dup the read queue since mods can come from different directories.
		std::map<std::string, s32> nameMap;
		std::vector<QueuedRead>::iterator iEntry = s_readQueue.begin();
		for (; iEntry != s_readQueue.end();)
		{
			std::string modDirOrZip;
			// If this is a zip file, we already have the file name.
			if (iEntry->type == QREAD_ZIP)
			{
				modDirOrZip = iEntry->fileName;
			}
			// If this is a directory, then extract after the last slash.
			else if (iEntry->path.length())  // QREAD_DIR
			{
				modDirOrZip = iEntry->path;

				size_t len = modDirOrZip.length();
				const char* str = modDirOrZip.data();
				s32 slashIndex = -1;
				for (size_t i = 0; i < len - 1; i++)
				{
					if ((str[i] == '/' || str[i] == '\\') && (str[i+1] != '/' && str[i+1] != '\\'))
					{
						slashIndex = s32(i);
					}
				}
				if (slashIndex >= 0)
				{
					modDirOrZip = &iEntry->path[slashIndex + 1];
				}
			}

			std::map<std::string, s32>::iterator iExistingEntry = nameMap.find(modDirOrZip);
			if (iExistingEntry != nameMap.end())
			{
				// This already exists and should be removed.
				iEntry = s_readQueue.erase(iEntry);
			}
			else
			{
				// Add it to the map.
				nameMap[modDirOrZip] = 1;
				++iEntry;
			}
		}

		std::sort(s_readQueue.begin(), s_readQueue.end(), sortQueueByName);
	}

	void modLoader_cleanupResources()
	{
		for (size_t i = 0; i < s_mods.size(); i++)
		{
			if (s_mods[i].image.texture)
			{
				TFE_RenderBackend::freeTexture(s_mods[i].image.texture);
			}
		}
		s_mods.clear();
		s_filteredMods.clear();
	}
		
	ViewMode modLoader_getViewMode()
	{
		return s_viewMode;
	}

	void modLoader_imageListUI(f32 uiScale)
	{
		DisplayInfo dispInfo;
		TFE_RenderBackend::getDisplayInfo(&dispInfo);
		s32 columns = max(1, (s32)((dispInfo.width - s32(16*uiScale)) / s32(268*uiScale)));

		f32 y = ImGui::GetCursorPosY();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		for (size_t i = 0; i < s_filteredMods.size();)
		{
			for (s32 x = 0; x < columns && i < s_filteredMods.size(); x++, i++)
			{
				char label[32];
				sprintf(label, "###%zd", i);
				ImGui::SetCursorPos(ImVec2((f32(x) * 268.0f + 16.0f)*uiScale, y));
				ImGui::InvisibleButton(label, ImVec2(256*uiScale, 192*uiScale));
				if (ImGui::IsItemClicked() && s_selectedMod < 0)
				{
					s_selectedMod = s32(i);
					TFE_System::logWrite(LOG_MSG, "Mods", "Selected Mod = %d", i);
				}

				f32 yScrolled = y - ImGui::GetScrollY();

				if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				{
					drawList->AddImageRounded(TFE_RenderBackend::getGpuPtr(s_filteredMods[i]->image.texture), ImVec2((f32(x) * 268 + 16 - 2)*uiScale, yScrolled - 2*uiScale),
						ImVec2((f32(x) * 268 + 16 + 256 + 2)*uiScale, yScrolled + (192 + 2)*uiScale), ImVec2(0.0f, s_filteredMods[i]->invertImage ? 1.0f : 0.0f),
						ImVec2(1.0f, s_filteredMods[i]->invertImage ? 0.0f : 1.0f), 0xffffffff, 8.0f, ImDrawFlags_RoundCornersAll);
					drawList->AddImageRounded(getGradientTexture(), ImVec2((f32(x) * 268 + 16 - 2)*uiScale, yScrolled - 2*uiScale),
						ImVec2((f32(x) * 268 + 16 + 256 + 2)*uiScale, yScrolled + (192 + 2)*uiScale), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
						0x40ffb080, 8.0f, ImDrawFlags_RoundCornersAll);
				}
				else
				{
					drawList->AddImageRounded(TFE_RenderBackend::getGpuPtr(s_filteredMods[i]->image.texture), ImVec2((f32(x) * 268 + 16)*uiScale, yScrolled),
						ImVec2((f32(x) * 268 + 16 + 256)*uiScale, yScrolled + 192*uiScale), ImVec2(0.0f, s_filteredMods[i]->invertImage ? 1.0f : 0.0f),
						ImVec2(1.0f, s_filteredMods[i]->invertImage ? 0.0f : 1.0f), 0xffffffff, 8.0f, ImDrawFlags_RoundCornersAll);
				}

				ImGui::SetCursorPos(ImVec2((f32(x) * 268 + 20)*uiScale, y + 192*uiScale));

				// Limit the name to 36 characters to avoid going into the next cell.
				if (s_filteredMods[i]->name.length() <= 36)
				{
					ImGui::LabelText("###Label", "%s", s_filteredMods[i]->name.c_str());
				}
				else
				{
					char name[TFE_MAX_PATH];
					strcpy(name, s_filteredMods[i]->name.c_str());
					name[33] = '.';
					name[34] = '.';
					name[35] = '.';
					name[36] = 0;
					ImGui::LabelText("###Label", "%s", name);
				}
			}
			y += 232*uiScale;
		}
	}

	void modLoader_NameListUI(f32 uiScale)
	{
		DisplayInfo dispInfo;
		TFE_RenderBackend::getDisplayInfo(&dispInfo);
		f32 topPos = ImGui::GetCursorPosY();
		s32 rowCount = (dispInfo.height - s32(topPos + 24 * uiScale)) / s32(28*uiScale);

		char buttonLabel[32];
		ImGui::PushFont(getDialogFont());
		size_t i = 0;
		for (s32 x = 0; i < s_filteredMods.size(); x++)
		{
			for (s32 y = 0; y < rowCount && i < s_filteredMods.size(); y++, i++)
			{
				sprintf(buttonLabel, "###mod%zd", i);
				ImVec2 cursor((8.0f + x * 410)*uiScale, (topPos + y * 28)*uiScale);
				ImGui::SetCursorPos(cursor);
				if (ImGui::Button(buttonLabel, ImVec2(400*uiScale, 24*uiScale)) && s_selectedMod < 0)
				{
					s_selectedMod = s32(i);
					TFE_System::logWrite(LOG_MSG, "Mods", "Selected Mod = %d", i);
				}
				
				ImGui::SetCursorPos(ImVec2(cursor.x + 8.0f*uiScale, cursor.y - 2.0f*uiScale));
				char name[TFE_MAX_PATH];
				strcpy(name, s_filteredMods[i]->name.c_str());
				size_t len = strlen(name);
				if (len > 36)
				{
					name[33] = '.';
					name[34] = '.';
					name[35] = '.';
					name[36] = 0;
				}

				ImGui::LabelText("###", "%s", name);
			}
		}
		ImGui::PopFont();
	}

	void modLoader_FileListUI(f32 uiScale)
	{
		DisplayInfo dispInfo;
		TFE_RenderBackend::getDisplayInfo(&dispInfo);
		f32 topPos = ImGui::GetCursorPosY();
		s32 rowCount = (dispInfo.height - s32(topPos + 24 * uiScale)) / s32(28*uiScale);

		char buttonLabel[32];
		ImGui::PushFont(getDialogFont());
		size_t i = 0;
		for (s32 x = 0; i < s_filteredMods.size(); x++)
		{
			for (s32 y = 0; y < rowCount && i < s_filteredMods.size(); y++, i++)
			{
				sprintf(buttonLabel, "###mod%zd", i);
				ImVec2 cursor((8.0f + x * 410)*uiScale, (topPos + y * 28)*uiScale);
				ImGui::SetCursorPos(cursor);
				if (ImGui::Button(buttonLabel, ImVec2(400*uiScale, 24*uiScale)) && s_selectedMod < 0)
				{
					s_selectedMod = s32(i);
					TFE_System::logWrite(LOG_MSG, "Mods", "Selected Mod = %d", i);
				}

				ImGui::SetCursorPos(ImVec2(cursor.x + 8.0f*uiScale, cursor.y - 2.0f*uiScale));
				char name[TFE_MAX_PATH];
				strcpy(name, s_filteredMods[i]->gobFiles[0].c_str());
				size_t len = strlen(name);
				if (len > 36)
				{
					name[33] = '.';
					name[34] = '.';
					name[35] = '.';
					name[36] = 0;
				}

				ImGui::LabelText("###", "%s", name);
			}
		}
		ImGui::PopFont();
	}

	// Checks whether a mod exists
	bool modLoader_exist(const char* modName)
	{
		// If you are not passing in a mod (ie: base game level) then this is always true
		if (strlen(modName) == 0)
		{
			return true;
		}

		char programDirModDir[TFE_MAX_PATH];
		sprintf(programDirModDir, "%sMods/%s", TFE_Paths::getPath(PATH_PROGRAM), modName);
		TFE_Paths::fixupPathAsDirectory(programDirModDir);

		if (FileUtil::exists(programDirModDir))
		{
			return true;
		}

		return false;
	}

	void modLoader_preLoad()
	{
		readFromQueue(c_itemsPerFrame);
	}
		
	bool modLoader_selectionUI()
	{
		bool stayOpen = true;
		f32 uiScale = (f32)TFE_Ui::getUiScale() * 0.01f;	

		// Load in the mod data a few at a time so to limit waiting for loading.
		readFromQueue(c_itemsPerFrame);
		clearSelectedMod();

		ImGui::Separator();
		ImGui::PushFont(getDialogFont());

		ImGui::LabelText("###", "VIEW");
		ImGui::SameLine(128.0f*uiScale);

		bool viewImages   = s_viewMode == VIEW_IMAGES;
		bool viewNameList = s_viewMode == VIEW_NAME_LIST;
		bool viewFileList = s_viewMode == VIEW_FILE_LIST;
		if (ImGui::Checkbox("Images", &viewImages))
		{
			if (viewImages)
			{
				s_viewMode = VIEW_IMAGES;
			}
			else
			{
				s_viewMode = VIEW_NAME_LIST;
			}
			filterMods(s_viewMode != VIEW_FILE_LIST);
		}
		ImGui::SameLine(236.0f*uiScale);
		if (ImGui::Checkbox("Name List", &viewNameList))
		{
			if (viewNameList)
			{
				s_viewMode = VIEW_NAME_LIST;
			}
			else
			{
				s_viewMode = VIEW_FILE_LIST;
			}
			filterMods(s_viewMode != VIEW_FILE_LIST);
		}
		ImGui::SameLine(380.0f*uiScale);
		if (ImGui::Checkbox("File List", &viewFileList))
		{
			if (viewFileList)
			{
				s_viewMode = VIEW_FILE_LIST;
			}
			else
			{
				s_viewMode = VIEW_IMAGES;
			}
			filterMods(s_viewMode != VIEW_FILE_LIST);
		}

		ImGui::SameLine(510.0f * uiScale);
		if (ImGui::Button("Refresh Mod Listing"))
		{
			s_modsRead = false;
			modLoader_read();
		}

		#ifdef _WIN32
		ImGui::SameLine(730.0f * uiScale);
		if (ImGui::Button("Open Mod Folder"))
		{
			// Create the empty folder is needed
			createModDirIfNeeded(programDirModDir);
			if (!TFE_System::osShellExecute(programDirModDir, NULL, NULL, false))
			{
				TFE_System::logWrite(LOG_ERROR, "ModLoader", "Failed to open the directory: '%s'", programDirModDir);
			}
		}
		#endif
		
		ImGui::Separator();

		// Filter
		ImGui::Text("Filter"); ImGui::SameLine(128.0f * uiScale);
		ImGui::PopFont();
		ImGui::SetNextItemWidth(256.0f * uiScale);
		ImGui::InputText("##FilterText", s_modFilter, 64);
		ImGui::SameLine(400.0f * uiScale);
		if (ImGui::Button("CLEAR"))
		{
			s_modFilter[0] = 0;
			s_filterLen = 0;
		}
		else
		{
			s_filterLen = strlen(s_modFilter);
		}
		if (s_prevModFilter[0] != s_modFilter[0] || strlen(s_prevModFilter) != s_filterLen || strcasecmp(s_prevModFilter, s_modFilter))
		{
			s_prevModFilter[0] = 0;
			strcpy(s_prevModFilter, s_modFilter);
			filterMods(s_viewMode != VIEW_FILE_LIST);
		}

		ImVec4 infoColor = ImVec4(0.2f, 0.8f, 0.4f, 1.0f);  // RGBA

		ImGui::TextColored(infoColor, "Showing %d out of %d mods.", s_filteredMods.size(), s_mods.size());

		ImGui::Separator();
					   
		if (s_viewMode == VIEW_IMAGES)
		{
			modLoader_imageListUI(uiScale);
		}
		else if (s_viewMode == VIEW_NAME_LIST)
		{
			modLoader_NameListUI(uiScale);
		}
		else if (s_viewMode == VIEW_FILE_LIST)
		{
			modLoader_FileListUI(uiScale);
		}

		// Always show the menu and filter before exiting
		if (s_mods.empty()) { return stayOpen; }

		if (s_selectedMod >= 0)
		{
			DisplayInfo dispInfo;
			TFE_RenderBackend::getDisplayInfo(&dispInfo);

			bool open = true;
			bool retFromLoader = false;
			s32 infoWidth  = dispInfo.width  - s32(120*uiScale);
			s32 infoHeight = dispInfo.height - s32(120*uiScale);

			const u32 window_flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
			ImGui::SetCursorPos(ImVec2(10*uiScale, 10*uiScale));
			ImGui::Begin("Mod Info", &open, /*ImVec2(f32(infoWidth), f32(infoHeight)), 1.0f,*/ window_flags);
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImVec2 cursor = ImGui::GetCursorPos();
			drawList->AddImageRounded(TFE_RenderBackend::getGpuPtr(s_filteredMods[s_selectedMod]->image.texture), ImVec2(cursor.x + 64, cursor.y + 64), ImVec2(cursor.x + 64 + 320*uiScale, cursor.y + 64 + 200*uiScale),
				ImVec2(0.0f, s_filteredMods[s_selectedMod]->invertImage ? 1.0f : 0.0f), ImVec2(1.0f, s_filteredMods[s_selectedMod]->invertImage ? 0.0f : 1.0f), 0xffffffff, 8.0f, ImDrawFlags_RoundCornersAll);

			ImGui::PushFont(getDialogFont());
			ImGui::SetCursorPosX(cursor.x + (320 + 70)*uiScale);
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.9f, 1.0f, 1.0f));
			ImGui::LabelText("###", "%s", s_filteredMods[s_selectedMod]->name.c_str());
			ImGui::PopStyleColor();

			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.75f));
			ImGui::SetCursorPos(ImVec2(cursor.x + 10*uiScale, cursor.y + 220*uiScale));
			ImGui::Text("Game: Dark Forces");
			ImGui::SetCursorPosX(cursor.x + 10*uiScale);
			ImGui::Text("Type: Vanilla Compatible");
			ImGui::SetCursorPosX(cursor.x + 10*uiScale);
			ImGui::Text("File: %s", s_filteredMods[s_selectedMod]->gobFiles[0].c_str());
			ImGui::PopStyleColor();

			ImGui::SetCursorPos(ImVec2(cursor.x + 90*uiScale, cursor.y + 320*uiScale));
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.0f, 0.0f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.0f, 0.0f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
			if (ImGui::Button("PLAY", ImVec2(128*uiScale, 32*uiScale)))
			{
				char selectedModCmd[TFE_MAX_PATH];
				sprintf(selectedModCmd, "-u%s%s", s_filteredMods[s_selectedMod]->relativePath.c_str(), s_filteredMods[s_selectedMod]->gobFiles[0].c_str());
				setSelectedMod(selectedModCmd);

				setState(APP_STATE_GAME);
				clearMenuState();
				open = false;
				retFromLoader = true;
			}
			ImGui::PopStyleColor(3);

			ImGui::SetCursorPos(ImVec2(cursor.x + 90*uiScale, cursor.y + 360*uiScale));
			if (ImGui::Button("CANCEL", ImVec2(128*uiScale, 32*uiScale)) || TFE_Input::keyPressed(KEY_ESCAPE))
			{
				open = false;
			}

			ImGui::PopFont();

			ImGui::SetCursorPos(ImVec2(cursor.x + 328*uiScale, cursor.y + 30*uiScale));
			ImGui::BeginChild("###Mod Info Text", ImVec2(f32(infoWidth - 344*uiScale), f32(infoHeight - 68*uiScale)), true, ImGuiWindowFlags_NoBringToFrontOnFocus);
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.75f));
			// TODO: Modify imGUI to make the internal "temp" text buffer at least as large as the input text.
			ImGui::TextWrapped("%s", s_filteredMods[s_selectedMod]->text.c_str());
			ImGui::PopStyleColor();
			ImGui::EndChild();

			if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_RootWindow))
			{
				open = false;
			}
			ImGui::End();

			if (!open)
			{
				s_selectedMod = -1;
			}
		}
		else if (TFE_Input::keyPressed(KEY_ESCAPE))
		{
			stayOpen = false;
		}
		return stayOpen;
	}

	void fixupName(char* name)
	{
		size_t len = strlen(name);
		name[0] = toupper(name[0]);
		for (size_t i = 1; i < len; i++)
		{
			name[i] = tolower(name[i]);
		}
	}

	bool parseNameFromText(const char* textFileName, const char* path, char* name, std::string* fullText)
	{
		if (!textFileName || textFileName[0] == 0) { return false; }

		const size_t len = strlen(textFileName);
		const char* ext = &textFileName[len - 3];
		size_t textLen = 0;
		if (strcasecmp(ext, "zip") == 0)
		{
			ZipArchive zipArchive;
			char zipPath[TFE_MAX_PATH];
			sprintf(zipPath, "%s%s", path, textFileName);
			if (zipArchive.open(zipPath))
			{
				s32 txtIndex = -1;
				const u32 count = zipArchive.getFileCount();
				for (u32 i = 0; i < count; i++)
				{
					const char* name = zipArchive.getFileName(i);
					const size_t nameLen = strlen(name);
					const char* zext = &name[nameLen - 3];
					if (strcasecmp(zext, "txt") == 0)
					{
						txtIndex = i;
						break;
					}
				}

				if (txtIndex >= 0 && zipArchive.openFile(txtIndex))
				{
					textLen = zipArchive.getFileLength();
					s_fileBuffer.resize(textLen + 1);
					s_fileBuffer[0] = 0;
					zipArchive.readFile(s_fileBuffer.data(), textLen);
					zipArchive.closeFile();
				}
			}
		}
		else
		{
			char fullPath[TFE_MAX_PATH];
			sprintf(fullPath, "%s%s", path, textFileName);

			FileStream textFile;
			if (!textFile.open(fullPath, Stream::MODE_READ))
			{
				return false;
			}
			textLen = textFile.getSize();
			s_fileBuffer.resize(textLen + 1);
			s_fileBuffer[0] = 0;
			textFile.readBuffer(s_fileBuffer.data(), (u32)textLen);
			textFile.close();
		}
		if (!textLen || s_fileBuffer[0] == 0)
		{
			return false;
		}

		// Some files start with garbage at the beginning...
		// So try a small probe first to see if such fixup is reqiured.
		bool needsFixup = false;
		for (size_t i = 0; i < 10 && i < s_fileBuffer.size(); i++)
		{
			if (s_fileBuffer[i] == 0)
			{
				needsFixup = true;
				break;
			}
		}
		// If it is, try to find a valid start.
		size_t lastZero = 0;
		if (needsFixup)
		{
			size_t len = s_fileBuffer.size();
			const char* text = s_fileBuffer.data();
			for (size_t i = 0; i < len - 1 && i < 128; i++)
			{
				if (text[i] == 0)
				{
					lastZero = i;
				}
			}
			if (lastZero) { lastZero++; }
		}
		*fullText = std::string(s_fileBuffer.data() + lastZero, s_fileBuffer.data() + s_fileBuffer.size());

		TFE_Parser parser;
		parser.init(fullText->c_str(), fullText->length());
		// First pass - look for "Title" followed by ':'
		size_t bufferPos = 0;
		size_t titleLen = strlen("Title");
		bool foundTitle = false;
		while (!foundTitle)
		{
			const char* line = parser.readLine(bufferPos, true);
			if (!line)
			{
				break;
			}

			if (strncasecmp("Title", line, titleLen) == 0)
			{
				size_t lineLen = strlen(line);
				for (size_t c = titleLen; c < lineLen; c++)
				{
					if (line[c] == ':' && (line[c + 1] == ' ' || line[c + 1] == '\t'))
					{
						// Next, skip past white space.
						for (size_t c2 = c + 1; c2 < lineLen; c2++)
						{
							if (line[c2] != ' ' && line[c2] != '\t')
							{
								strcpy(name, &line[c2]);
								foundTitle = true;
								break;
							}
						}

						// Found it.
						if (foundTitle) { break; }
					}
				}
			}
		}

		// Second pass - look for "name": - JSon syntax.
		const char* jsonName = "\"name\":";
		bufferPos = 0;
		size_t nameLen = strlen(jsonName);
		while (!foundTitle)
		{
			const char* line = parser.readLine(bufferPos, true);
			if (!line)
			{
				break;
			}
			if (strncasecmp(jsonName, line, nameLen) == 0)
			{
				size_t lineLen = strlen(line);
				for (size_t c = nameLen + 1; c < lineLen && !foundTitle; c++)
				{
					if (line[c] == '\"')
					{
						for (size_t c2 = c + 1; c2 < lineLen; c2++)
						{
							if (line[c2] == '\"')
							{
								// Found it.
								memcpy(name, &line[c + 1], c2 - c - 1);
								name[c2 - c - 1] = 0;
								foundTitle = true;
								break;
							}
						}
					}
				}
			}
		}

		if (!foundTitle)
		{
			// Looking for "Title" failed, try reading the first 'valid' line.
			bufferPos = 0;
			while (!foundTitle)
			{
				const char* line = parser.readLine(bufferPos, true);
				if (!line)
				{
					break;
				}
				if (line[0] == '<' || line[0] == '>' || line[0] == '=' || line[0] == '|')
				{
					continue;
				}
				if (strncasecmp(line, "PRESENTING", strlen("PRESENTING")) == 0)
				{
					continue;
				}

				strcpy(name, line);
				foundTitle = true;
				break;
			}
		}

		if (foundTitle)
		{
			size_t titleLen = strlen(name);
			size_t lastValid = 0;
			for (size_t c = 0; c < titleLen; c++)
			{
				if (name[c] != ' ' && name[c] != '-')
				{
					lastValid = c;
				}
				if (name[c] == '-')
				{
					break;
				}
			}
			if (lastValid > 0)
			{
				name[lastValid + 1] = 0;
			}
			if (lastValid && name[0] != '/')
			{
				return true;
			}
		}
		return false;
	}

	// Return true if 'str' matches the 'filter', taking into account special symbols.
	bool stringFilter(const char* str, const char* filter, size_t filterLength)
	{
		for (size_t i = 0; i < filterLength; i++)
		{
			if (filter[i] == '?') { continue; }
			if (tolower(str[i]) != tolower(filter[i])) { return false; }
		}
		return true;
	}

	bool filter(const std::string& name)
	{
		// No filter applied.
		if (s_modFilter[0] == 0 || s_filterLen == 0) { return true; }

		// Make sure the name is at least as long as the filter.
		const size_t nameLength = name.length();
		if (nameLength < s_filterLen) { return false; }

		// See if there is a match...
		const char* nameStr = name.c_str();
		const size_t validLength = nameLength - s_filterLen + 1;
		for (size_t i = 0; i < validLength; i++)
		{
			if (stringFilter(&nameStr[i], s_modFilter, s_filterLen))
			{
				return true;
			}
		}
		return false;
	}

	bool sortFilteredByName(ModData*& a, ModData*& b)
	{
		return strcasecmp(a->name.c_str(), b->name.c_str()) < 0;
	}

	bool sortFilteredByFile(ModData*& a, ModData*& b)
	{
		return strcasecmp(a->gobFiles[0].c_str(), b->gobFiles[0].c_str()) < 0;
	}

	void filterMods(bool filterByName, bool sort)
	{
		const size_t count = s_mods.size();
		s_filteredMods.clear();
		s_filteredMods.reserve(count);

		// Filter by Mod name or by filename.
		// This is done by looping through all of the mods in the list and then adding them to
		// filtered mods if the filter passes.
		for (size_t i = 0; i < count; i++)
		{
			if (filter(filterByName ? s_mods[i].name : s_mods[i].gobFiles[0]))
			{
				s_filteredMods.push_back(&s_mods[i]);
			}
		}

		// Then apply an alphabetical sorting to the filtered result.
		// Note - other methods of sorting can be implemented if needed in the future by changing the sort functions.
		if (sort)
		{
			std::sort(s_filteredMods.begin(), s_filteredMods.end(), filterByName ? sortFilteredByName : sortFilteredByFile);
		}
	}

	void readFromQueue(size_t itemsPerFrame)
	{
		FileList gobFiles, txtFiles, imgFiles;
		const size_t readEnd = min(s_readIndex + itemsPerFrame, s_readQueue.size());
		const QueuedRead* reads = s_readQueue.data();
		bool updateFilter = false;
		for (size_t i = s_readIndex; i < readEnd; i++, s_readIndex++)
		{
			updateFilter = true;
			if (reads[i].type == QREAD_DIR)
			{
				// Clear doesn't deallocate in most implementations, so doing it this way should reduce memory allocations.
				gobFiles.clear();
				txtFiles.clear();
				imgFiles.clear();

				const char* subDir = reads[i].path.c_str();
				FileUtil::readDirectory(subDir, "gob", gobFiles);
				FileUtil::readDirectory(subDir, "txt", txtFiles);
				FileUtil::readDirectory(subDir, "jpg", imgFiles);

				// No gob files = no mod.
				if (gobFiles.size() != 1)
				{
					continue;
				}
				s_mods.push_back({});
				ModData& mod = s_mods.back();

				mod.gobFiles = gobFiles;
				mod.textFile = txtFiles.empty() ? "" : txtFiles[0];
				mod.imageFile = imgFiles.empty() ? "" : imgFiles[0];
				mod.text = "";

				size_t fullDirLen = strlen(subDir);
				for (size_t i = 0; i < fullDirLen; i++)
				{
					if (strncasecmp("Mods", &subDir[i], 4) == 0)
					{
						mod.relativePath = &subDir[i + 5];
						break;
					}
				}

				if (mod.imageFile.empty())
				{
					if (!extractPosterFromMod(subDir, mod.gobFiles[0].c_str(), &mod.image))
					{
						s_mods.pop_back();
						continue;
					}
					mod.invertImage = true;
				}
				else
				{
					extractPosterFromImage(subDir, nullptr, mod.imageFile.c_str(), &mod.image);
					mod.invertImage = false;
				}

				char name[TFE_MAX_PATH];
				if (!parseNameFromText(mod.textFile.c_str(), subDir, name, &mod.text))
				{
					const char* gobFileName = mod.gobFiles[0].c_str();
					memcpy(name, gobFileName, strlen(gobFileName) - 4);
					name[strlen(gobFileName) - 4] = 0;
					fixupName(name);
				}

				mod.name = name;
			}
			else
			{
				ZipArchive zipArchive;
				const char* modPath = reads[i].path.c_str();
				const char* zipName = reads[i].fileName.c_str();

				char zipPath[TFE_MAX_PATH];
				sprintf(zipPath, "%s%s", modPath, zipName);
				if (!zipArchive.open(zipPath)) { continue; }

				s32 gobFileIndex = -1;
				s32 txtFileIndex = -1;
				s32 jpgFileIndex = -1;

				// Look for the following:
				// 1. Gob File.
				// 2. Text File.
				// 3. JPG
				for (u32 f = 0; f < zipArchive.getFileCount(); f++)
				{
					const char* fileName = zipArchive.getFileName(f);
					size_t len = strlen(fileName);
					if (len <= 4)
					{
						continue;
					}
					const char* ext = &fileName[len - 3];
					if (strcasecmp(ext, "gob") == 0)
					{
						gobFileIndex = s32(f);
					}
					else if (strcasecmp(ext, "txt") == 0)
					{
						txtFileIndex = s32(f);
					}
					else if (strcasecmp(ext, "jpg") == 0)
					{
						jpgFileIndex = s32(f);
					}
				}
				if (gobFileIndex >= 0)
				{
					s_mods.push_back({});
					ModData& mod = s_mods.back();
					mod.gobFiles.push_back(zipName);
					mod.text = "";

					char name[TFE_MAX_PATH];
					if (!parseNameFromText(mod.gobFiles[0].c_str(), modPath, name, &mod.text))
					{
						const char* gobFileName = mod.gobFiles[0].c_str();
						memcpy(name, gobFileName, strlen(gobFileName) - 4);
						name[strlen(gobFileName) - 4] = 0;
						fixupName(name);
					}
					mod.name = name;

					if (jpgFileIndex < 0)
					{
						if (!extractPosterFromMod(modPath, mod.gobFiles[0].c_str(), &mod.image))
						{
							s_mods.pop_back();
						}
						else
						{
							mod.invertImage = true;
						}
					}
					else
					{
						extractPosterFromImage(modPath, mod.gobFiles[0].c_str(), zipArchive.getFileName(jpgFileIndex), &mod.image);
						mod.invertImage = false;
					}
				}

				zipArchive.close();
			}
		}

		// Update the filtered list.
		if (updateFilter)
		{
			// Only sort once the full list is loaded, otherwise the entries constantly suffle around since the name sorting doesn't
			// match file name sorting very well.
			filterMods(s_viewMode != VIEW_FILE_LIST, /*sort*/s_readIndex == s_readQueue.size() || s_viewMode == VIEW_FILE_LIST);
		}
	}

	void extractPosterFromImage(const char* baseDir, const char* zipFile, const char* imageFileName, UiTexture* poster)
	{
		if (zipFile && zipFile[0])
		{
			char zipPath[TFE_MAX_PATH];
			sprintf(zipPath, "%s%s", baseDir, zipFile);

			ZipArchive zipArchive;
			if (!zipArchive.open(zipPath)) { return; }
			if (zipArchive.openFile(imageFileName))
			{
				size_t imageSize = zipArchive.getFileLength();
				s_imageBuffer.resize(imageSize);
				zipArchive.readFile(s_imageBuffer.data(), imageSize);
				zipArchive.closeFile();

				SDL_Surface* image = TFE_Image::loadFromMemory((u8*)s_imageBuffer.data(), imageSize);
				if (image)
				{
					TextureGpu* gpuImage = TFE_RenderBackend::createTexture(image->w, image->h, (u32*)image->pixels, MAG_FILTER_LINEAR);
					poster->texture = gpuImage;
					poster->width = image->w;
					poster->height = image->h;

					TFE_Image::free(image);
				}
			}
			zipArchive.close();
		}
		else
		{
			char imagePath[TFE_MAX_PATH];
			sprintf(imagePath, "%s%s", baseDir, imageFileName);

			SDL_Surface* image = TFE_Image::get(imagePath);
			if (image)
			{
				TextureGpu* gpuImage = TFE_RenderBackend::createTexture(image->w, image->h, (u32*)image->pixels, MAG_FILTER_LINEAR);
				poster->texture = gpuImage;
				poster->width = image->w;
				poster->height = image->h;
			}
		}
	}

	bool extractPosterFromMod(const char* baseDir, const char* archiveFileName, UiTexture* poster)
	{
		// Extract a "poster", if possible, from the GOB file.
		// And then save it as a JPG in /ProgramData/TheForceEngine/ModPosters/NAME.jpg
		char modPath[TFE_MAX_PATH], srcPath[TFE_MAX_PATH], srcPathTex[TFE_MAX_PATH];
		sprintf(modPath, "%s%s", baseDir, archiveFileName);
		sprintf(srcPath, "%s%s", TFE_Paths::getPath(PATH_SOURCE_DATA), "DARK.GOB");
		sprintf(srcPathTex, "%s%s", TFE_Paths::getPath(PATH_SOURCE_DATA), "TEXTURES.GOB");

		GobMemoryArchive gobMemArchive;
		const size_t len = strlen(archiveFileName);
		const char* archiveExt = &archiveFileName[len - 3];
		Archive* archiveMod = nullptr;
		bool archiveIsGob = true;
		bool validGob = false;
		if (strcasecmp(archiveExt, "zip") == 0)
		{
			archiveIsGob = false;

			ZipArchive zipArchive;
			if (zipArchive.open(modPath))
			{
				// Find the gob...
				s32 gobIndex = -1;
				const u32 count = zipArchive.getFileCount();
				for (u32 i = 0; i < count; i++)
				{
					const char* name = zipArchive.getFileName(i);
					const size_t nameLen = strlen(name);
					const char* zext = &name[nameLen - 3];
					if (strcasecmp(zext, "gob") == 0)
					{
						gobIndex = i;
						break;
					}
				}

				if (gobIndex >= 0)
				{
					size_t bufferLen = zipArchive.getFileLength(gobIndex);
					u8* buffer = (u8*)malloc(bufferLen);
					zipArchive.openFile(gobIndex);
					const size_t lengthRead = zipArchive.readFile(buffer, bufferLen);
					zipArchive.closeFile();

					bool archiveRead = false;
					if (lengthRead > 0)
					{
						archiveRead = gobMemArchive.open(buffer, bufferLen);
						if (archiveRead)
						{
							archiveMod = &gobMemArchive;
							validGob = true;
						}
					}
					
					if (!archiveRead)
					{
						TFE_System::logWrite(LOG_ERROR, "ModLoader", "Cannot open zip: '%s'", modPath);
					}
				}

				zipArchive.close();
			}
		}
		else
		{
			archiveMod = Archive::getArchive(ARCHIVE_GOB, archiveFileName, modPath);
			validGob = archiveMod != nullptr;
		}
		Archive* archiveTex = Archive::getArchive(ARCHIVE_GOB, "TEXTURES.GOB", srcPathTex);
		Archive* archiveBase = Archive::getArchive(ARCHIVE_GOB, "DARK.GOB", srcPath);

		s_readBuffer[0].clear();
		s_readBuffer[1].clear();
		if (archiveMod || archiveBase)
		{
			if (archiveMod && archiveMod->fileExists("wait.bm") && archiveMod->openFile("wait.bm"))
			{
				s_readBuffer[0].resize(archiveMod->getFileLength());
				archiveMod->readFile(s_readBuffer[0].data(), archiveMod->getFileLength());
				archiveMod->closeFile();
			}
			else if (archiveTex->openFile("wait.bm"))
			{
				s_readBuffer[0].resize(archiveTex->getFileLength());
				archiveTex->readFile(s_readBuffer[0].data(), archiveTex->getFileLength());
				archiveTex->closeFile();
			}

			if (archiveMod && archiveMod->fileExists("wait.pal") && archiveMod->openFile("wait.pal"))
			{
				s_readBuffer[1].resize(archiveMod->getFileLength());
				archiveMod->readFile(s_readBuffer[1].data(), archiveMod->getFileLength());
				archiveMod->closeFile();
			}
			else if (archiveBase->openFile("wait.pal"))
			{
				s_readBuffer[1].resize(archiveBase->getFileLength());
				archiveBase->readFile(s_readBuffer[1].data(), archiveBase->getFileLength());
				archiveBase->closeFile();
			}
		}

		if (!s_readBuffer[0].empty() && !s_readBuffer[1].empty())
		{
			TextureData* imageData = bitmap_loadFromMemory(s_readBuffer[0].data(), s_readBuffer[0].size(), 1);
			u32 palette[256];
			convertPalette(s_readBuffer[1].data(), palette);
			createTexture(imageData, palette, poster, MAG_FILTER_LINEAR);
		}

		if (archiveMod && archiveIsGob)
		{
			Archive::freeArchive(archiveMod);
		}

		return validGob;
	}
}