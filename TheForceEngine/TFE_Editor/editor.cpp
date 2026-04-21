#include "editor.h"
#include "editorConfig.h"
#include "editorLevel.h"
#include "editorResources.h"
#include "editorProject.h"
#include "historyView.h"

#include <TFE_Asset/imageAsset.h>
#include <TFE_Settings/settings.h>
#include <TFE_Editor/AssetBrowser/assetBrowser.h>
#include <TFE_Editor/LevelEditor/levelEditor.h>
#include <TFE_Editor/LevelEditor/infoPanel.h>
#include <TFE_Editor/LevelEditor/levelEditorInf.h>
#include <TFE_Editor/LevelEditor/groups.h>
#include <TFE_Editor/LevelEditor/lighting.h>
#include <TFE_Editor/LevelEditor/findSectorUI.h>
#include <TFE_Editor/LevelEditor/snapshotUI.h>
#include <TFE_Editor/LevelEditor/browser.h>
#include <TFE_Editor/LevelEditor/confirmDialogs.h>
#include <TFE_Editor/EditorAsset/editorAsset.h>
#include <TFE_Editor/EditorAsset/editor3dThumbnails.h>
#include <TFE_Input/input.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_RenderShared/modelDraw.h>
#include <TFE_Polygon/polygon.h>
#include <TFE_System/system.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_Archive/archive.h>
#include <TFE_Ui/ui.h>

#include <map>
#include <string>

namespace TFE_Editor
{
	enum EditorMode
	{
		EDIT_ASSET_BROWSER = 0,
		EDIT_ASSET,
	};
	
	const ImVec4 c_textColors[] =
	{
		ImVec4(1.0f, 1.0f, 1.0f, 1.0f), // TEXTCLR_NORMAL
		ImVec4(0.5f, 1.0f, 0.5f, 1.0f), // TEXTCLR_TITLE_ACTIVE
		ImVec4(1.0f, 0.8f, 0.5f, 1.0f), // TEXTCLR_TITLE_INACTIVE
	};

	const char* c_readOnly = "<Read Only Mode>";

	struct MessageBox
	{
		char id[512];
		char msg[TFE_MAX_PATH * 2] = "";
	};

	static Vec2i s_editorVersion = { 0, 50 };
	static std::vector<RecentProject> s_recents;

	static s32 s_uid = 0;
	static char s_uidBuffer[256];
	
	static bool s_showPerf = true;
	static bool s_showEditor = true;
	static AssetType s_editorAssetType = TYPE_NOT_SET;
	static EditorMode s_editorMode = EDIT_ASSET_BROWSER;
	static EditorPopup s_editorPopup = POPUP_NONE;
	static bool s_hidePopup = false;
	static u32 s_editorPopupUserData = 0;
	static void* s_editorPopupUserPtr = nullptr;
	static bool s_exitEditor = false;
	static bool s_programExiting = false;
	static bool s_configView = false;
	static WorkBuffer s_workBuffer;
	static char s_projectPath[TFE_MAX_PATH] = "";

	static bool s_menuActive = false;
	static MessageBox s_msgBox = {};
	static ImFont* s_fonts[FONT_COUNT * FONT_SIZE_COUNT] = { 0 };
	static ImFont* s_fontLarge = nullptr;

	static f64 s_tooltipTime = 0.0;
	static Vec2i s_prevMousePos = { 0 };
	static const f64 c_tooltipDelay = 0.2;
	static bool s_canShowTooltips = false;
	static PopupEndCallback s_popupEndCallback = nullptr;

	static std::map<std::string, TextureGpu*> s_gpuImages;

	static TextureGpu* s_iconAtlas = nullptr;
	
	void menu();
	void loadFonts();
	void updateTooltips();
	bool loadIcons();
	void freeGpuImages();

	WorkBuffer& getWorkBuffer()
	{
		return s_workBuffer;
	}

	Vec2i getEditorVersion()
	{
		return s_editorVersion;
	}

	TextureGpu* getIconAtlas()
	{
		return s_iconAtlas;
	}

	void enable()
	{
		// Ui begin/render is called so we have a "UI Frame" in which to setup UI state.
		s_editorMode = EDIT_ASSET_BROWSER;
		s_exitEditor = false;
		s_programExiting = false;
		loadFonts();
		loadIcons();
		loadConfig();
		AssetBrowser::init();
		TFE_RenderShared::modelDraw_init();
		thumbnail_init(64);
		TFE_Polygon::clipInit();
		s_msgBox = MessageBox{};
		s_gpuImages.clear();
	}

	bool disable()
	{
		if (s_editorAssetType == TYPE_LEVEL && LevelEditor::levelIsDirty() && s_editorPopup != POPUP_EXIT_SAVE_CONFIRM)
		{
			openEditorPopup(POPUP_EXIT_SAVE_CONFIRM);
		}

		if (!isPopupOpen() || s_editorPopup != POPUP_EXIT_SAVE_CONFIRM)
		{
			AssetBrowser::destroy();
			thumbnail_destroy();
			TFE_RenderShared::modelDraw_destroy();
			freeGpuImages();
			TFE_Polygon::clipDestroy();
			return true;
		}
		return false;
	}
		
	bool loadIcons()
	{
		s_iconAtlas = loadGpuImage("UI_Images/IconAtlas.png");
		return s_iconAtlas != nullptr;
	}

	void freeGpuImages()
	{
		for (std::map<std::string, TextureGpu*>::iterator iImage = s_gpuImages.begin(); iImage != s_gpuImages.end(); ++iImage)
		{
			TFE_RenderBackend::freeTexture(iImage->second);
		}
		s_gpuImages.clear();
		s_iconAtlas = nullptr;
	}
		
	bool iconButton(IconId icon, const char* tooltip/*=nullptr*/, bool highlight/*=false*/, const f32* tint/*=nullptr*/)
	{
		void* gpuPtr = TFE_RenderBackend::getGpuPtr(s_iconAtlas);
		const s32 x = s32(icon) & 7;
		const s32 y = s32(icon) >> 3;

		const f32 imageScale = 1.0f / 272.0f;
		const f32 x0 = f32(x) * 34.0f + 1.0f;
		const f32 x1 = x0 + 32.0f;
		const f32 y0 = f32(y) * 34.0f + 1.0f;
		const f32 y1 = y0 + 32.0f;
		const ImVec4 tintColor = tint ? ImVec4(tint[0], tint[1], tint[2], tint[3]) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		const ImVec4 bgColor = ImVec4(0, 0, 0, highlight ? 0.75f : 0.25f);
		const s32 padding = 1;

		// A unique ID formed from: icon + instance*ICON_COUNT + ptr&0xffffffff
		const s32 id = s_uid + s32(size_t(s_iconAtlas));
		const ImVec2 size = ImVec2(32, 32);
		s_uid++;
			
		ImGui::PushID(id);
		bool res = ImGui::ImageButton(gpuPtr, size, ImVec2(x0*imageScale, y0*imageScale),
			ImVec2(x1*imageScale, y1*imageScale), padding, bgColor, tintColor);
		if (tooltip) { setTooltip(tooltip); }
		ImGui::PopID();

		return res;
	}

	bool iconButtonInline(IconId icon, const char* tooltip/*=nullptr*/, const f32* tint/*=nullptr*/, bool small/*=false*/)
	{
		void* gpuPtr = TFE_RenderBackend::getGpuPtr(s_iconAtlas);
		const s32 x = s32(icon) & 7;
		const s32 y = s32(icon) >> 3;

		const f32 imageScale = 1.0f / 272.0f;
		const f32 x0 = f32(x) * 34.0f + 1.0f;
		const f32 x1 = x0 + 32.0f;
		const f32 y0 = f32(y) * 34.0f + 1.0f;
		const f32 y1 = y0 + 32.0f;
		const ImVec4 tintColor  = tint ? ImVec4(tint[0], tint[1], tint[2], tint[3]) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		const ImVec4 tintColor2 = tint ? ImVec4(tint[0]*2.0f, tint[1]*2.0f, tint[2]*2.0f, tint[3]) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		const ImVec4 bgColor = ImVec4(0, 0, 0, 0);

		// A unique ID formed from: icon + instance*ICON_COUNT + ptr&0xffffffff
		const s32 id = s_uid + s32(size_t(s_iconAtlas));
		const ImVec2 size = small ? ImVec2(24, 24) : ImVec2(32, 32);
		s_uid++;

		ImGui::PushStyleColor(ImGuiCol_Button, { 0,0,0,0 });
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0,0,0,0 });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, { 0,0,0,0 });
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.0f);

		ImGui::PushID(id);
		bool res = ImGui::ImageButtonDualTint(gpuPtr, size, ImVec2(x0*imageScale, y0*imageScale),
			ImVec2(x1*imageScale, y1*imageScale), bgColor, tintColor, tintColor2);
		if (tooltip) { setTooltip(tooltip); }
		ImGui::PopID();
		ImGui::PopStyleColor(3);

		return res;
	}

	void sectionHeader(const char* text)
	{
		ImGui::TextColored({ 0.306f, 0.788f, 0.69f, 1.0f }, "%s", text);
	}
		
	void optionCheckbox(const char* name, u32* flags, u32 flagValue, s32 width)
	{
		ImGui::SetNextItemWidth(width ? width : ImGui::CalcTextSize(name).x + 16);
		ImGui::LabelText("##Label", "%s", name); ImGui::SameLine();
		ImGui::CheckboxFlags(editor_getUniqueLabel(""), flags, flagValue);
	}

	void optionSliderEditFloat(const char* name, const char* precision, f32* value, f32 minValue, f32 maxValue, f32 step)
	{
		ImGui::SetNextItemWidth(160);
		ImGui::LabelText("##Label", "%s", name); ImGui::SameLine();

		char labelSlider[256];
		char labelInput[256];
		sprintf(labelSlider, "##%s_Slider", name);
		sprintf(labelInput, "##%s_Input", name);

		ImGui::SliderFloat(labelSlider, value, minValue, maxValue, precision);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(128);
		ImGui::InputFloat(labelInput, value, step, 10.0f * step, precision);
	}

	bool messageBoxUi()
	{
		bool finished = false;

		pushFont(FONT_SMALL);
		if (ImGui::BeginPopupModal(s_msgBox.id, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGuiStyle& style = ImGui::GetStyle();
			f32 textWidth = ImGui::CalcTextSize(s_msgBox.msg).x + style.FramePadding.x;
			f32 buttonWidth = ImGui::CalcTextSize("OK").x;

			ImGui::Text("%s", s_msgBox.msg);
			ImGui::Separator();

			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (textWidth - buttonWidth) * 0.5f);
			if (ImGui::Button("OK"))
			{
				finished = true;
			}
			ImGui::EndPopup();
		}
		popFont();
		return finished;
	}

	bool isPopupOpen()
	{
		return s_editorPopup != POPUP_NONE && !s_hidePopup;
	}

	void handlePopupBegin()
	{
		if (s_editorPopup == POPUP_NONE || s_hidePopup) { return; }
		switch (s_editorPopup)
		{
			case POPUP_MSG_BOX:
			{
				ImGui::OpenPopup(s_msgBox.id);
			} break;
			case POPUP_CONFIG:
			{
				ImGui::OpenPopup("Editor Config");
			} break;
			case POPUP_RESOURCES:
			{
				ImGui::OpenPopup("Editor Resources");
			} break;
			case POPUP_NEW_PROJECT:
			case POPUP_EDIT_PROJECT:
			{
				ImGui::OpenPopup("Project");
			} break;
			case POPUP_EXPORT_PROJECT:
			{
				ImGui::OpenPopup("Export Project");
			} break;
			case POPUP_NEW_LEVEL:
			{
				ImGui::OpenPopup("New Level");
			} break;
			case POPUP_BROWSE:
			{
				ImGui::OpenPopup("Browse");
			} break;
			case POPUP_CATEGORY:
			{
				ImGui::OpenPopup("Category");
			} break;
			case POPUP_EDIT_INF:
			{
				ImGui::OpenPopup("Edit INF");
			} break;
			case POPUP_FIND_SECTOR:
			{
				ImGui::OpenPopup("Find Sector");
			} break;
			case POPUP_SNAPSHOTS:
			{
				ImGui::OpenPopup("Snapshots");
			} break;
			case POPUP_TEX_SOURCES:
			{
				ImGui::OpenPopup("Texture Sources");
			} break;
			case POPUP_RELOAD_CONFIRM:
			{
				ImGui::OpenPopup("Reload Confirmation");
			} break;
			case POPUP_EXIT_SAVE_CONFIRM:
			{
				ImGui::OpenPopup("Exit Without Saving");
			} break;
			case POPUP_GROUP_NAME:
			{
				ImGui::OpenPopup("Choose Name");
			} break;
			case POPUP_LIGHTING:
			{
				ImGui::OpenPopup("Level Lighting");
			} break;
			case POPUP_LEV_USER_PREF:
			{
				ImGui::OpenPopup("User Preferences");
			} break;
			case POPUP_LEV_TEST_OPTIONS:
			{
				ImGui::OpenPopup("Test Options");
			} break;
			case POPUP_HISTORY_VIEW:
			{
				ImGui::OpenPopup("History View");
			} break;
		}
	}
		
	void handlePopupEnd()
	{
		if (s_editorPopup == POPUP_NONE || s_hidePopup) { return; }

		const EditorPopup popupId = s_editorPopup;
		switch (s_editorPopup)
		{
			case POPUP_MSG_BOX:
			{
				if (messageBoxUi())
				{
					ImGui::CloseCurrentPopup();
					s_editorPopup = POPUP_NONE;
				}
			} break;
			case POPUP_CONFIG:
			{
				if (configUi())
				{
					ImGui::CloseCurrentPopup();
					s_editorPopup = POPUP_NONE;
				}
			} break;
			case POPUP_RESOURCES:
			{
				if (resources_ui())
				{
					ImGui::CloseCurrentPopup();
					s_editorPopup = POPUP_NONE;
				}
			} break;
			case POPUP_NEW_PROJECT:
			{
				if (project_editUi(true))
				{
					ImGui::CloseCurrentPopup();
					s_editorPopup = POPUP_NONE;
				}
			} break;
			case POPUP_EDIT_PROJECT:
			{
				if (project_editUi(false))
				{
					ImGui::CloseCurrentPopup();
					s_editorPopup = POPUP_NONE;
				}
			} break;
			case POPUP_EXPORT_PROJECT:
			{
				if (project_exportUi())
				{
					ImGui::CloseCurrentPopup();
					s_editorPopup = POPUP_NONE;
				}
			} break;
			case POPUP_NEW_LEVEL:
			{
				if (level_newLevelUi())
				{
					ImGui::CloseCurrentPopup();
					s_editorPopup = POPUP_NONE;
				}
			} break;
			case POPUP_BROWSE:
			{
				if (AssetBrowser::popup())
				{
					ImGui::CloseCurrentPopup();
					s_editorPopup = POPUP_NONE;
				}
			} break;
			case POPUP_CATEGORY:
			{
				if (LevelEditor::categoryPopupUI())
				{
					ImGui::CloseCurrentPopup();
					s_editorPopup = POPUP_NONE;
				}
			} break;
			case POPUP_EDIT_INF:
			{
				if (LevelEditor::editor_infEdit())
				{
					LevelEditor::editor_infEditEnd();
					ImGui::CloseCurrentPopup();
					s_editorPopup = POPUP_NONE;
				}
			} break;
			case POPUP_FIND_SECTOR:
			{
				if (LevelEditor::findSectorUI())
				{
					ImGui::CloseCurrentPopup();
					s_editorPopup = POPUP_NONE;
				}
			} break;
			case POPUP_SNAPSHOTS:
			{
				if (LevelEditor::snapshotUI())
				{
					ImGui::CloseCurrentPopup();
					s_editorPopup = POPUP_NONE;
				}
			} break;
			case POPUP_TEX_SOURCES:
			{
				if (LevelEditor::textureSourcesUI())
				{
					ImGui::CloseCurrentPopup();
					s_editorPopup = POPUP_NONE;
				}
			} break;
			case POPUP_RELOAD_CONFIRM:
			{
				if (LevelEditor::reloadConfirmation())
				{
					ImGui::CloseCurrentPopup();
					s_editorPopup = POPUP_NONE;
				}
			} break;
			case POPUP_EXIT_SAVE_CONFIRM:
			{
				if (LevelEditor::exitSaveConfirmation())
				{
					ImGui::CloseCurrentPopup();
					s_editorPopup = POPUP_NONE;
				}
			} break;
			case POPUP_GROUP_NAME:
			{
				if (LevelEditor::groups_chooseName())
				{
					ImGui::CloseCurrentPopup();
					s_editorPopup = POPUP_NONE;
				}
			} break;
			case POPUP_LIGHTING:
			{
				if (LevelEditor::levelLighting())
				{
					ImGui::CloseCurrentPopup();
					s_editorPopup = POPUP_NONE;
				}
			} break;
			case POPUP_LEV_USER_PREF:
			{
				if (LevelEditor::userPreferences())
				{
					ImGui::CloseCurrentPopup();
					s_editorPopup = POPUP_NONE;
				}
			} break;
			case POPUP_LEV_TEST_OPTIONS:
			{
				if (LevelEditor::testOptions())
				{
					ImGui::CloseCurrentPopup();
					s_editorPopup = POPUP_NONE;
				}
			} break;
			case POPUP_HISTORY_VIEW:
			{
				if (historyView())
				{
					ImGui::CloseCurrentPopup();
					s_editorPopup = POPUP_NONE;
				}
			} break;
		}

		if (TFE_Input::keyPressed(KEY_ESCAPE))
		{
			if (s_editorPopup != POPUP_NONE)
			{
				// Make sure the config is saved.
				if (s_editorPopup == POPUP_CONFIG)
				{
					saveConfig();
				}
				else if (s_editorPopup == POPUP_EDIT_INF)
				{
					LevelEditor::editor_infEditEnd();
				}

				s_editorPopup = POPUP_NONE;
				ImGui::CloseCurrentPopup();
			}
			else
			{
				s_exitEditor = true;
			}
		}
		if (s_popupEndCallback && s_editorPopup == POPUP_NONE && s_editorPopup != popupId)
		{
			s_popupEndCallback(popupId);
			s_popupEndCallback = nullptr;
		}
	}

	bool update(bool consoleOpen, bool minimized, bool exiting)
	{
		// If the program is minimized, the editor should skip any processing.
		if (minimized)
		{
			return s_exitEditor;
		}
		s_programExiting = exiting;

		editor_clearUid();
		thumbnail_update();

		TFE_RenderBackend::clearWindow();

		updateTooltips();
		handlePopupBegin();
		menu();

		if (s_editorMode == EDIT_ASSET_BROWSER)
		{
			AssetBrowser::update();
		}
		else if (s_editorMode == EDIT_ASSET)
		{
			switch (s_editorAssetType)
			{
				case TYPE_LEVEL:
				{
					LevelEditor::update();
				} break;
				default:
				{
					// Invalid mode, this code should never be reached.
					assert(0);
					s_editorMode = EDIT_ASSET_BROWSER;
				}
			}
		}

		handlePopupEnd();
		
		return s_exitEditor;
	}

	bool render()
	{
		if (s_editorMode == EDIT_ASSET_BROWSER)
		{
			AssetBrowser::render();
		}
		return true;
	}

	ImVec4 getTextColor(EditorTextColor color)
	{
		return c_textColors[color];
	}

	bool getMenuActive()
	{
		return s_menuActive;
	}

	bool beginMenuBar()
	{
		bool menuActive = true;

		DisplayInfo displayInfo;
		TFE_RenderBackend::getDisplayInfo(&displayInfo);

		ImGui::SetWindowPos("MainMenuBar", { 0.0f, 0.0f });
		ImGui::SetWindowSize("MainMenuBar", { (f32)displayInfo.width, 32.0f });
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus;

		ImGui::Begin("MainMenuBar", &menuActive, window_flags);
		return ImGui::BeginMenuBar();
	}

	void endMenuBar()
	{
		ImGui::EndMenuBar();
		ImGui::End();
	}

	void disableNextItem()
	{
		ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
	}

	void enableNextItem()
	{
		ImGui::PopItemFlag();
		ImGui::PopStyleVar();
	}

	bool isInAssetEditor()
	{
		return s_editorMode == EDIT_ASSET;
	}

	bool isProgramExiting()
	{
		return s_programExiting;
	}

	void drawTitle()
	{
		DisplayInfo info;
		TFE_RenderBackend::getDisplayInfo(&info);
		const s32 winWidth = info.width;

		const Project* project = project_get();
		char fullTitle[1024];
		const char* title = project->active ? project->name : c_readOnly;
		if (s_editorAssetType == TYPE_LEVEL)
		{
			sprintf(fullTitle, "%s - %s", title, LevelEditor::s_level.name.c_str());
			if (LevelEditor::levelIsDirty())
			{
				strcat(fullTitle, "*");
			}
		}
		else
		{
			strcpy(fullTitle, title);
		}
		const s32 titleWidth = (s32)ImGui::CalcTextSize(fullTitle).x;

		const ImVec4 titleColor = getTextColor(project->active ? TEXTCLR_TITLE_ACTIVE : TEXTCLR_TITLE_INACTIVE);
		ImGui::SameLine(f32((winWidth - titleWidth)/2));
		ImGui::TextColored(titleColor, "%s", fullTitle);
	}

	void popupCallback_openAssetBrowser(EditorPopup popupId)
	{
		s_editorMode = EDIT_ASSET_BROWSER;
	}

	void popupCallback_exitEditor(EditorPopup popupId)
	{
		s_exitEditor = true;
	}

	void popupCallback_newProject(EditorPopup popupId)
	{
		s_editorPopup = POPUP_NEW_PROJECT;
		project_prepareNew();
	}

	void popupCallback_openProject(EditorPopup popupId)
	{
		FileResult res = TFE_Ui::openFileDialog("Open Project", s_projectPath, { "Project", "*.INI *.ini" });
		if (!res.empty())
		{
			char filePath[TFE_MAX_PATH];
			strcpy(filePath, res[0].c_str());
			FileUtil::fixupPath(filePath);
			project_load(filePath);
		}
	}

	void popupCallback_closeProject(EditorPopup popupId)
	{
		project_close();
	}

	void menu()
	{
		pushFont(FONT_SMALL);

		s_menuActive = false;
		beginMenuBar();
		{
			// General menu items.
			if (ImGui::BeginMenu("Editor"))
			{
				s_menuActive = true;
				if (ImGui::MenuItem("Editor Config", NULL, (bool*)NULL))
				{
					s_editorPopup = POPUP_CONFIG;
				}
				if (ImGui::MenuItem("Resources", NULL, (bool*)NULL))
				{
					s_editorPopup = POPUP_RESOURCES;
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Asset Browser", NULL, s_editorMode == EDIT_ASSET_BROWSER))
				{
					bool exitSavePopup = false;
					if (s_editorMode == EDIT_ASSET && s_editorAssetType == TYPE_LEVEL)
					{
						exitSavePopup = LevelEditor::edit_closeLevelCheckSave();
					}
					if (exitSavePopup)
					{
						setPopupEndCallback(popupCallback_openAssetBrowser);
					}
					else
					{
						s_editorMode = EDIT_ASSET_BROWSER;
					}
				}

				// Disable the Asset Editor menu item, unless actually in an Asset Editor.
				// This allows the item to be highlighted, and to be able to switch back to
				// the Asset Browser using the menu.
				const bool disable = s_editorMode != EDIT_ASSET;
				if (disable) { disableNextItem(); }
				if (ImGui::MenuItem("Asset Editor", NULL, s_editorMode == EDIT_ASSET))
				{
					s_editorMode = EDIT_ASSET;
				}
				if (disable) { enableNextItem(); }

				ImGui::Separator();
				if (ImGui::MenuItem("Return to Game", NULL, (bool*)NULL))
				{
					bool exitSavePopup = false;
					if (s_editorMode == EDIT_ASSET && s_editorAssetType == TYPE_LEVEL)
					{
						exitSavePopup = LevelEditor::edit_closeLevelCheckSave();
					}
					if (exitSavePopup)
					{
						setPopupEndCallback(popupCallback_exitEditor);
					}
					else
					{
						s_exitEditor = true;
					}
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Select"))
			{
				s_menuActive = true;
				if (ImGui::MenuItem("Select All", NULL, (bool*)NULL))
				{
					if (s_editorMode == EDIT_ASSET_BROWSER)
					{
						AssetBrowser::selectAll();
					}
					else if (s_editorMode == EDIT_ASSET)
					{
						if (s_editorAssetType == TYPE_LEVEL)
						{
							LevelEditor::selectAll();
						}
					}
				}
				if (ImGui::MenuItem("Select None", NULL, (bool*)NULL))
				{
					if (s_editorMode == EDIT_ASSET_BROWSER)
					{
						AssetBrowser::selectNone();
					}
					else if (s_editorMode == EDIT_ASSET)
					{
						if (s_editorAssetType == TYPE_LEVEL)
						{
							LevelEditor::selectNone();
						}
					}
				}
				if (ImGui::MenuItem("Invert Selection", NULL, (bool*)NULL))
				{
					if (s_editorMode == EDIT_ASSET_BROWSER)
					{
						AssetBrowser::invertSelection();
					}
					else if (s_editorMode == EDIT_ASSET)
					{
						if (s_editorAssetType == TYPE_LEVEL)
						{
							LevelEditor::selectInvert();
						}
					}
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Project"))
			{
				s_menuActive = true;
				if (ImGui::MenuItem("New", NULL, (bool*)NULL))
				{
					bool exitSavePopup = false;
					if (s_editorMode == EDIT_ASSET && s_editorAssetType == TYPE_LEVEL)
					{
						exitSavePopup = LevelEditor::edit_closeLevelCheckSave();
					}
					if (exitSavePopup)
					{
						setPopupEndCallback(popupCallback_newProject);
					}
					else
					{
						s_editorPopup = POPUP_NEW_PROJECT;
						project_prepareNew();
					}
				}
				if (ImGui::MenuItem("Open", NULL, (bool*)NULL))
				{
					bool exitSavePopup = false;
					if (s_editorMode == EDIT_ASSET && s_editorAssetType == TYPE_LEVEL)
					{
						exitSavePopup = LevelEditor::edit_closeLevelCheckSave();
					}
					if (exitSavePopup)
					{
						setPopupEndCallback(popupCallback_openProject);
					}
					else
					{
						FileResult res = TFE_Ui::openFileDialog("Open Project", s_projectPath, { "Project", "*.INI *.ini" });
						if (!res.empty())
						{
							char filePath[TFE_MAX_PATH];
							strcpy(filePath, res[0].c_str());
							FileUtil::fixupPath(filePath);
							project_load(filePath);
						}
					}
				}
				ImGui::Separator();
				bool projectActive = project_get()->active;
				if (!projectActive) { disableNextItem(); }
				if (ImGui::MenuItem("Edit", NULL, (bool*)NULL))
				{
					s_editorPopup = POPUP_EDIT_PROJECT;
					project_prepareEdit();
				}
				if (ImGui::MenuItem("Close", NULL, (bool*)NULL))
				{
					bool exitSavePopup = false;
					if (s_editorMode == EDIT_ASSET && s_editorAssetType == TYPE_LEVEL)
					{
						exitSavePopup = LevelEditor::edit_closeLevelCheckSave();
					}
					if (exitSavePopup)
					{
						setPopupEndCallback(popupCallback_closeProject);
					}
					else
					{
						project_close();
					}
				}
				if (!projectActive) { enableNextItem(); }
				ImGui::Separator();
				if (ImGui::MenuItem("Export", NULL, (bool*)NULL))
				{
					s_editorPopup = POPUP_EXPORT_PROJECT;
					project_prepareExportUi();
				}
				ImGui::Separator();
				if (s_recents.empty()) { disableNextItem(); }
				if (ImGui::BeginMenu("Recent Projects"))
				{
					const size_t count = s_recents.size();
					const RecentProject* recents = s_recents.data();
					s32 removeId = -1;
					for (size_t i = 0; i < count; i++)
					{
						char item[TFE_MAX_PATH];
						sprintf(item, "%d %s", (s32)i + 1, recents[i].name.c_str());
						if (ImGui::MenuItem(item, NULL, (bool*)NULL))
						{
							bool exitSavePopup = false;
							if (s_editorMode == EDIT_ASSET && s_editorAssetType == TYPE_LEVEL)
							{
								exitSavePopup = LevelEditor::edit_closeLevelCheckSave();
							}

							if (!exitSavePopup && !project_load(recents[i].path.c_str()))
							{
								// Remove from recents if it is no longer valid.
								removeId = s32(i);
							}
						}
					}
					ImGui::EndMenu();

					if (removeId >= 0)
					{
						s_recents.erase(s_recents.begin() + removeId);
						saveConfig();
					}
				}
				if (s_recents.empty()) { enableNextItem(); }
				ImGui::EndMenu();
			}
			// Allow the current Asset Editor to add its own menus.
			if (s_editorMode == EDIT_ASSET)
			{
				switch (s_editorAssetType)
				{
					case TYPE_LEVEL:
					{
						if (LevelEditor::menu())
						{
							s_menuActive = true;
						}
					} break;
					default:
					{
						// Invalid mode, this code should never be reached.
						assert(0);
					}
				}
			}

			drawTitle();
		}
		endMenuBar();

		popFont();
	}
		
	void loadFonts()
	{
		// Assume if any fonts are loaded, they all are.
		if (s_fonts[0]) { return; }

		char fontPath[TFE_MAX_PATH];
		sprintf(fontPath, "%s", "Fonts/DroidSansMono.ttf");
		TFE_Paths::mapSystemPath(fontPath);

		ImGuiIO& io = ImGui::GetIO();
		ImFont** fontSmall = &s_fonts[FONT_SMALL * FONT_SIZE_COUNT];
		fontSmall[0] = io.Fonts->AddFontFromFileTTF(fontPath, 16 * 100 / 100);
		fontSmall[1] = io.Fonts->AddFontFromFileTTF(fontPath, 16 * 125 / 100);
		fontSmall[2] = io.Fonts->AddFontFromFileTTF(fontPath, 16 * 150 / 100);
		fontSmall[3] = io.Fonts->AddFontFromFileTTF(fontPath, 16 * 175 / 100);
		fontSmall[4] = io.Fonts->AddFontFromFileTTF(fontPath, 16 * 200 / 100);
		s_fontLarge = io.Fonts->AddFontFromFileTTF(fontPath, 48);
		TFE_Ui::invalidateFontAtlas();
	}
		
	void pushFont(FontType type)
	{
		if (type == FONT_LARGE)
		{
			ImGui::PushFont(s_fontLarge);
		}
		else
		{
			const s32 index = fontScaleToIndex(s_editorConfig.fontScale);
			ImFont** fonts = &s_fonts[type * FONT_SIZE_COUNT];
			ImGui::PushFont(fonts[index]);
		}
	}

	void popFont()
	{
		ImGui::PopFont();
	}

	void setPopupEndCallback(PopupEndCallback callback)
	{
		s_popupEndCallback = callback;
	}
		
	void showMessageBox(const char* type, const char* msg, ...)
	{
		char fullStr[TFE_MAX_PATH * 2];
		va_list arg;
		va_start(arg, msg);
		vsprintf(fullStr, msg, arg);
		va_end(arg);

		s_editorPopup = POPUP_MSG_BOX;
		strcpy(s_msgBox.msg, fullStr);
		sprintf(s_msgBox.id, "%s##MessageBox", type);
	}

	void hidePopup()
	{
		s_hidePopup = true;
	}

	void showPopup()
	{
		s_hidePopup = false;
	}

	EditorPopup getCurrentPopup()
	{
		return s_editorPopup;
	}

	void openEditorPopup(EditorPopup popup, u32 userData, void* userPtr)
	{
		if (s_hidePopup) { return; }

		s_editorPopup = popup;
		s_editorPopupUserData = userData;
		s_editorPopupUserPtr = userPtr;

		// Initialization if needed.
		switch (popup)
		{
			case POPUP_BROWSE:
			{
				AssetBrowser::initPopup(AssetType(userData), (char*)userPtr);
			} break;
			case POPUP_EDIT_INF:
			{
				LevelEditor::editor_infEditBegin((char*)userPtr, userData == 0xffffffff ? -1 : userData);
			} break;
			case POPUP_FIND_SECTOR:
			{
				LevelEditor::findSectorUI_Begin();
			} break;
			case POPUP_SNAPSHOTS:
			{
				LevelEditor::snapshotUI_Begin();
			} break;
		}
	}
		
	ArchiveType getArchiveType(const char* filename)
	{
		ArchiveType type = ARCHIVE_UNKNOWN;

		char ext[TFE_MAX_PATH];
		FileUtil::getFileExtension(filename, ext);
		if (strcasecmp(ext, "GOB") == 0)
		{
			type = ARCHIVE_GOB;
		}
		else if (strcasecmp(ext, "LAB") == 0)
		{
			type = ARCHIVE_LAB;
		}
		else if (strcasecmp(ext, "LFD") == 0)
		{
			type = ARCHIVE_LFD;
		}
		else if (strcasecmp(ext, "ZIP") == 0)
		{
			type = ARCHIVE_ZIP;
		}

		return type;
	}

	Archive* getArchive(const char* name, GameID gameId)
	{
		TFE_Settings_Game* game = TFE_Settings::getGameSettings();
		char filePath[TFE_MAX_PATH];
		sprintf(filePath, "%s%s", game->header[gameId].sourcePath, name);

		ArchiveType type = getArchiveType(name);
		return type != ARCHIVE_UNKNOWN ? Archive::getArchive(type, name, filePath) : nullptr;
	}

	void getTempDirectory(char* tmpDir)
	{
		sprintf(tmpDir, "%s/Temp", s_editorConfig.editorPath);
		FileUtil::fixupPath(tmpDir);
		if (!FileUtil::directoryExists(tmpDir))
		{
			FileUtil::makeDirectory(tmpDir);
		}
	}

	void clearRecents()
	{
		s_recents.clear();
	}
		
	void addToRecents(const char* path, bool _saveConfig)
	{
		char name[TFE_MAX_PATH];
		FileUtil::getFileNameFromPath(path, name);

		// Does it already exist?
		const size_t count = s_recents.size();
		s32 foundId = -1;
		for (size_t i = 0; i < count; i++)
		{
			if (strcasecmp(s_recents[i].path.c_str(), path) == 0)
			{
				foundId = s32(i);
				break;
			}
		}
		if (foundId > 0)
		{
			// Move it to the front.
			RecentProject curRecent = s_recents[foundId];
			s_recents.erase(s_recents.begin() + foundId);
			s_recents.insert(s_recents.begin(), curRecent);
		}
		else if (foundId < 0 && count < 10)	// Only have up to 10.
		{
			// Otherwise add it.
			s_recents.push_back({name, path});
		}
		if (_saveConfig)
		{
			saveConfig();
		}
	}

	void removeFromRecents(const char* path)
	{
		// Does it already exist?
		const size_t count = s_recents.size();
		s32 foundId = -1;
		for (size_t i = 0; i < count; i++)
		{
			if (strcasecmp(s_recents[i].path.c_str(), path) == 0)
			{
				foundId = s32(i);
				break;
			}
		}
		// Remove if it exists.
		if (foundId >= 0)
		{
			s_recents.erase(s_recents.begin() + foundId);
		}
	}

	std::vector<RecentProject>* getRecentProjects()
	{
		return &s_recents;
	}

	void listSelection(const char* labelText, const char** listValues, size_t listLen, s32* index, s32 comboOffset/*=96*/, s32 comboWidth/*=0*/)
	{
		ImGui::SetNextItemWidth(UI_SCALE(256));
		ImGui::LabelText("##Label", "%s", labelText); ImGui::SameLine(UI_SCALE(comboOffset));

		char comboId[256];
		sprintf(comboId, "##%s", labelText);
		if (comboWidth) { ImGui::SetNextItemWidth(UI_SCALE(comboWidth)); }
		ImGui::Combo(comboId, index, listValues, (s32)listLen);
	}

	void enableAssetEditor(Asset* asset)
	{
		if (!asset) { return; }

		// For now, only the levels have an asset editor.
		// TODO: Other asset editors.
		if (asset->type == TYPE_LEVEL)
		{
			s_editorMode = EDIT_ASSET;
			s_editorAssetType = asset->type;
			LevelEditor::init(asset);
		}
		else
		{
			showMessageBox("Warning", "The selected asset '%s' does not yet have an Asset Editor.", asset->name.c_str());
		}
	}

	void disableAssetEditor()
	{
		s_editorMode = EDIT_ASSET_BROWSER;
		s_editorAssetType = TYPE_NOT_SET;
	}

	void updateTooltips()
	{
		Vec2i mouse;
		TFE_Input::getMousePos(&mouse.x, &mouse.z);
		bool mouseStationary = mouse.x == s_prevMousePos.x && mouse.z == s_prevMousePos.z;
		s_canShowTooltips = mouseStationary && s_tooltipTime >= c_tooltipDelay;
		if (mouseStationary)
		{
			s_tooltipTime += TFE_System::getDeltaTime();
		}
		else
		{
			s_tooltipTime = 0.0;
		}
		s_prevMousePos = mouse;
	}

	void setTooltip(const char* msg, ...)
	{
		if (!ImGui::IsItemHovered() || !s_canShowTooltips) { return; }

		char fullStr[TFE_MAX_PATH];
		va_list arg;
		va_start(arg, msg);
		vsprintf(fullStr, msg, arg);
		va_end(arg);

		ImGui::SetTooltip("%s", fullStr);
	}

	// Return true if 'str' matches the 'filter', taking into account special symbols.
	bool editorStringFilter(const char* str, const char* filter, size_t filterLength)
	{
		for (size_t i = 0; i < filterLength; i++)
		{
			if (filter[i] == '?') { continue; }
			if (tolower(str[i]) != tolower(filter[i])) { return false; }
		}
		return true;
	}
		
	TextureGpu* loadGpuImage(const char* path)
	{
		std::map<std::string, TextureGpu*>::iterator iImage = s_gpuImages.find(path);
		if (iImage != s_gpuImages.end())
		{
			return iImage->second;
		}

		char imagePath[TFE_MAX_PATH];
		strcpy(imagePath, path);
		if (!TFE_Paths::mapSystemPath(imagePath))
		{
			memset(imagePath, 0, TFE_MAX_PATH);
			TFE_Paths::appendPath(TFE_PathType::PATH_PROGRAM, path, imagePath, TFE_MAX_PATH);
			FileUtil::fixupPath(imagePath);
		}

		TextureGpu* gpuImage = nullptr;
		SDL_Surface* image = TFE_Image::get(imagePath);
		if (image)
		{
			gpuImage = TFE_RenderBackend::createTexture(image->w, image->h, (u32*)image->pixels, MAG_FILTER_LINEAR);
			if (gpuImage)
			{
				s_gpuImages[path] = gpuImage;
			}
		}
		return gpuImage;
	}
				
	void editor_clearUid()
	{
		s_uid = 0;
	}

	const char* editor_getUniqueLabel(const char* label)
	{
		sprintf(s_uidBuffer, "%s##%d", label, s_uid);
		s_uid++;
		return s_uidBuffer;
	}

	s32 editor_getUniqueId()
	{
		s32 id = s_uid;
		s_uid++;
		return id;
	}

	bool editor_button(const char* label)
	{
		return ImGui::Button(editor_getUniqueLabel(label));
	}

	bool mouseInsideItem()
	{
		ImVec2 mousePos = ImGui::GetMousePos();
		ImVec2 minCoord = ImGui::GetItemRectMin();
		ImVec2 maxCoord = ImGui::GetItemRectMax();

		return mousePos.x >= minCoord.x && mousePos.x < maxCoord.x && mousePos.y >= minCoord.y && mousePos.y < maxCoord.y;
	}

	void sublist_begin(Sublist& subList, f32 tabSize, f32 itemWidth)
	{
		const ImVec4 c_colorSublistBase = { 0.98f, 0.49f, 0.26f, 1.0f };
		const ImVec4 c_colorSublistItem = { c_colorSublistBase.x, c_colorSublistBase.y, c_colorSublistBase.z, 0.80f };
		const ImVec4 c_colorSublistItemActive = { c_colorSublistBase.x, c_colorSublistBase.y, c_colorSublistBase.z, 0.60f };
		const ImVec4 c_colorSublistItemHover = { c_colorSublistBase.x, c_colorSublistBase.y, c_colorSublistBase.z, 0.31f };

		ImGui::PushStyleColor(ImGuiCol_Text, { 1.0f, 1.0f, 1.0f, 0.6f });
		ImGui::PushStyleColor(ImGuiCol_Header, c_colorSublistItem);
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, c_colorSublistItemActive);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, c_colorSublistItemHover);

		subList.xOrigin = ImGui::GetCursorPosX();
		subList.tabSize = tabSize;
		subList.width = itemWidth;
	}

	bool sublist_item(const Sublist& subList, const char* name, bool selected)
	{
		ImGui::SetCursorPosX(subList.xOrigin + subList.tabSize);
		return ImGui::Selectable(name, selected, 0, { subList.width, 0.0f });
	}

	void sublist_end(const Sublist& subList)
	{
		ImGui::PopStyleColor(4);
		ImGui::SetCursorPosX(subList.xOrigin);
	}
}
