#include "snapshotUI.h"
#include "levelEditor.h"
#include "levelEditorData.h"
#include "levelEditorHistory.h"
#include "tabControl.h"
#include "sharedState.h"
#include <TFE_Editor/editor.h>
#include <TFE_Editor/editorConfig.h>
#include <TFE_Editor/editorProject.h>
#include <TFE_System/math.h>
#include <TFE_System/utf8.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_Ui/ui.h>
#include <algorithm>

using namespace TFE_Editor;

namespace LevelEditor
{
	struct SnapshotInfo
	{
		std::string name;
		std::string fileName;
		std::string notes;
	};
	std::vector<SnapshotInfo> s_snapshotInfo;

	const char c_invalidFilenameChar[] = { '/', '<', '>', ':', '\"', '\\', '|', '?', '*' };
	const s32 c_invalidLen = TFE_ARRAYSIZE(c_invalidFilenameChar);

	const char* c_manifestFilename = "manifest.ini";
	static char s_snapshotDir[TFE_MAX_PATH];
	static char s_snapshotManifestPath[TFE_MAX_PATH];
	static char s_snapshotName[256];
	static char s_snapshotNotes[256];
	static s32 s_selectedItem = -1;
	std::vector<char> s_buffer;

	void readManifest();
	void writeManifest();
	void sanitizeName(char* name);
	void sanitizeFileName(const char* name, char* filename);
				
	void snapshotUI_Begin()
	{
		s_snapshotInfo.clear();
		s_selectedItem = -1;
		s_snapshotName[0] = 0;
		s_snapshotNotes[0] = 0;

		Project* project = project_get();
		if (!project->active) { return; }

		sprintf(s_snapshotDir, "%s/Snapshots/", project->path);
		if (!FileUtil::directoryExists(s_snapshotDir))
		{
			FileUtil::makeDirectory(s_snapshotDir);
		}
		strcat(s_snapshotDir, s_level.slot.c_str());
		strcat(s_snapshotDir, "/");
		if (!FileUtil::directoryExists(s_snapshotDir))
		{
			FileUtil::makeDirectory(s_snapshotDir);
		}

		sprintf(s_snapshotManifestPath, "%s%s", s_snapshotDir, c_manifestFilename);
		readManifest();
	}

	bool snapshotUI()
	{
		pushFont(FONT_SMALL);
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize;
		bool exit = false;
		char filename[256];
		char filepath[TFE_MAX_PATH];
		if (ImGui::BeginPopupModal("Snapshots", nullptr, window_flags))
		{
			ImGui::SetNextItemWidth(64.0f);
			ImGui::LabelText("###NameLabel", "%s", "Name: ");
			ImGui::SameLine();
			ImGui::InputText("##Name", s_snapshotName, 256);

			ImGui::SetNextItemWidth(64.0f);
			ImGui::LabelText("###NotesLabel", "%s", "Notes: ");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(512.0f);
			ImGui::InputText("##Label", s_snapshotNotes, 256);

			if (ImGui::Button("Create Snapshot"))
			{
				sanitizeName(s_snapshotName);
				sanitizeFileName(s_snapshotName, filename);
				if (s_snapshotName[0] > 32)
				{
					s_snapshotInfo.push_back({ s_snapshotName, filename, s_snapshotNotes });
					// Save level to file.
					sprintf(filepath, "%s%s.tfl", s_snapshotDir, filename);
					saveLevelToPath(filepath, false);
					// Save to the manifest.
					writeManifest();
					// Select the new item.
					s_selectedItem = (s32)s_snapshotInfo.size() - 1;
				}
				s_snapshotName[0] = 0;
				s_snapshotNotes[0] = 0;
			}
			ImGui::Separator();

			bool disableButtons = s_selectedItem < 0 || s_selectedItem >= (s32)s_snapshotInfo.size();
			if (disableButtons) { disableNextItem(); }
			if (ImGui::Button("Load"))
			{
				// Load level to file.
				sprintf(filepath, "%s%s.tfl", s_snapshotDir, s_snapshotInfo[s_selectedItem].fileName.c_str());
				history_clear();
				levelClear();
				loadFromTFLWithPath(filepath);
				// Create the initial snapshot.
				char snapshotName[TFE_MAX_PATH];
				sprintf(snapshotName, "Load Snapshot {%s}", s_snapshotInfo[s_selectedItem].name.c_str());
				levHistory_createSnapshot(snapshotName);
				// Exit.
				exit = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Delete"))
			{
				const s32 count = (s32)s_snapshotInfo.size();
				if (s_selectedItem >= 0 && s_selectedItem < count)
				{
					// Delete the file.
					sprintf(filepath, "%s%s.tfl", s_snapshotDir, s_snapshotInfo[s_selectedItem].fileName.c_str());
					FileUtil::deleteFile(filepath);
					// Delete from the snapshot info list.
					for (s32 i = s_selectedItem; i < count - 1; i++)
					{
						s_snapshotInfo[i] = s_snapshotInfo[i + 1];
					}
					s_snapshotInfo.pop_back();
					// Save to the manifest.
					writeManifest();
				}

				s_selectedItem = -1;
			}
			if (disableButtons) { enableNextItem(); }
			ImGui::Separator();

			// Scroll area with selectable snapshots.
			if (ImGui::BeginChild("###SnapshotList", ImVec2(256.0f, 256.0f)))
			{
				const s32 count = (s32)s_snapshotInfo.size();
				SnapshotInfo* info = s_snapshotInfo.data();
				char label[256];
				for (s32 i = 0; i < count; i++, info++)
				{
					sprintf(label, "%s###%d", info->name.c_str(), i);
					if (ImGui::Selectable(label, s_selectedItem == i))
					{
						s_selectedItem = i;
					}
					if (!info->notes.empty())
					{
						setTooltip("%s", info->notes.c_str());
					}
				}
			}
			ImGui::EndChild();
			ImGui::Separator();

			if (ImGui::Button("Exit"))
			{
				exit = true;
			}
			ImGui::EndPopup();
		}
		popFont();

		return exit;
	}

	void readManifest()
	{
		s_snapshotInfo.clear();
		s_buffer.clear();
		FileStream manifest;
		if (manifest.open(s_snapshotManifestPath, FileStream::MODE_READ))
		{
			const size_t size = manifest.getSize();
			s_buffer.resize(size + 1);
			manifest.readBuffer(s_buffer.data(), (u32)size);
			s_buffer[size] = 0;
			manifest.close();
		}
		if (s_buffer.empty()) { return; }

		TFE_Parser parser;
		parser.init(s_buffer.data(), s_buffer.size());
		parser.enableBlockComments();
		parser.addCommentString("//");
		parser.addCommentString("#");

		size_t bufferPos = 0;
		const char* line = parser.readLine(bufferPos, true);
		TokenList tokens;
		while (line)
		{
			parser.tokenizeLine(line, tokens);
			if (tokens.size() >= 3)
			{
				s_snapshotInfo.push_back({ tokens[0], tokens[1], tokens[2] });
			}
			else if (tokens.size() >= 2)
			{
				s_snapshotInfo.push_back({ tokens[0], tokens[1], "" });
			}
			line = parser.readLine(bufferPos, true);
		}
	}

	void writeManifest()
	{
		FileStream manifest;
		if (manifest.open(s_snapshotManifestPath, FileStream::MODE_WRITE))
		{
			const size_t count = s_snapshotInfo.size();
			const SnapshotInfo* info = s_snapshotInfo.data();
			for (size_t i = 0; i < count; i++, info++)
			{
				manifest.writeString("\"%s\" \"%s\" \"%s\"\r\n", info->name.c_str(), info->fileName.c_str(), info->notes.c_str());
			}
			manifest.close();
		}
	}

	void sanitizeName(char* name)
	{
		char newName[TFE_MAX_PATH];
		const size_t len = strlen(name);
		// Remove quotes.
		size_t newIndex = 0;
		for (size_t i = 0; i < len; i++)
		{
			if (name[i] != '\"')
			{
				newName[newIndex++] = name[i];
			}
		}
		newName[newIndex] = 0;
		strcpy(name, newName);
	}

	bool isInvalidChar(char c)
	{
		if (c < 32 || c > 127) { return true; }

		for (s32 i = 0; i < c_invalidLen; i++)
		{
			if (c == c_invalidFilenameChar[i]) { return true; }
		}
		return false;
	}

	void sanitizeFileName(const char* name, char* filename)
	{
		const size_t len = strlen(name);
		for (size_t i = 0; i < len; i++)
		{
			filename[i] = name[i];
			// Replace any invalid characters for Linux or Windows with underscore (_).
			if (isInvalidChar(name[i]))
			{
				filename[i] = '_';
			}
		}
		// Filenames cannot end with '.' on Windows.
		if (filename[len - 1] == '.')
		{
			filename[len - 1] = '_';
		}
		// Filenames shouldn't start with space.
		if (filename[0] == ' ')
		{
			filename[0] = '_';
		}
		filename[len] = 0;
	}
}  // namespace LevelEditor