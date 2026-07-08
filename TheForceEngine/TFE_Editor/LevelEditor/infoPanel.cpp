#include "levelEditor.h"
#include "levelEditorData.h"
#include "levelEditorHistory.h"
#include "editVertex.h"
#include "editCommon.h"
#include "hotkeys.h"
#include "guidelines.h"
#include "tabControl.h"
#include "camera.h"
#include "infoPanel.h"
#include "groups.h"
#include "sharedState.h"
#include "selection.h"
#include <TFE_Input/input.h>
#include <TFE_Editor/editor.h>
#include <TFE_Editor/errorMessages.h>
#include <TFE_Editor/editorConfig.h>
#include <TFE_Editor/AssetBrowser/assetBrowser.h>
#include <TFE_Editor/EditorAsset/editorTexture.h>
#include <TFE_Editor/LevelEditor/Rendering/viewport.h>
#include <TFE_Editor/LevelEditor/Rendering/grid.h>
#include <TFE_Editor/LevelEditor/Scripting/levelEditorScripts.h>
#include <TFE_Jedi/Level/rwall.h>
#include <TFE_Jedi/Level/rsector.h>
#include <TFE_System/math.h>
#include <TFE_System/system.h>
#include <TFE_Ui/ui.h>

#include <algorithm>
#include <vector>
#include <string>
#include <cstring>

using namespace TFE_Editor;

namespace LevelEditor
{
	enum PanelConst
	{
		SCROLL_FALSE = 0,
		SCROLL_TRUE = 2,
	};
	enum InfoTab
	{
		TAB_INFO = 0,
		TAB_ITEM,
		TAB_COUNT
	};
	const char* c_infoTabs[TAB_COUNT] = { "Info", "Item" };
	const char* c_infoToolTips[TAB_COUNT] =
	{
		"Level Info.\nManual grid height setting.\nMessage, warning, and error output.",
		"Hovered or selected item property editor,\nblank when no item (sector, wall, object) is selected.",
	};
	const ImVec4 tabSelColor = { 0.25f, 0.75f, 1.0f, 1.0f };
	const ImVec4 tabStdColor = { 1.0f, 1.0f, 1.0f, 0.5f };

	struct LeMessage
	{
		LeMsgType type;
		std::string msg;
	};
	static std::vector<LeMessage> s_outputMsg;
	static std::vector<std::string> s_scriptCmdHistory;
	static s32 s_historyPtr = 0;
	static u32 s_outputFilter = LFILTER_DEFAULT;
	static s32 s_outputTabSel = 0;
	static f32 s_infoWith;
	static s32 s_infoHeight;
	static Vec2f s_infoPos;
	static InfoTab s_infoTab = TAB_INFO;

	static s32 s_outputHeight = 26 * 5;
	static s32 s_outputPrevHeight = 26 * 5;
	static bool s_collapsed = false;

	static Feature s_prevVertexFeature = {};
	static Feature s_prevWallFeature = {};
	static Feature s_prevSectorFeature = {};
	static Feature s_prevObjectFeature = {};
	static bool s_wallShownLast = false;
	static s32 s_prevCategoryFlags = 0;
	static bool s_textInputFocused = false;

	static s32 s_groupOpen = -1;
	static s32 s_scrollToBottom = SCROLL_FALSE;

	static char s_scriptLine[4096] = { 0 };
	static char s_script[65536] = { 0 };
	static bool s_allowScriptHistoryCallback = false;
	
	bool infoIntInput(const char* labelId, u32 width, s32* value, bool unset = false);
	bool infoUIntInput(const char* labelId, u32 width, u32* value, bool unset = false);
	bool infoFloatInput(const char* labelId, u32 width, f32* value, bool unset = false);
		
	void infoPanelClearMessages()
	{
		s_outputMsg.clear();
	}

	void infoPanelAddMsg(LeMsgType type, const char* msg, ...)
	{
		char fullStr[TFE_MAX_PATH * 2];
		va_list arg;
		va_start(arg, msg);
		vsprintf(fullStr, msg, arg);
		va_end(arg);

		s_outputMsg.push_back({ type, fullStr });
		s_scrollToBottom = SCROLL_TRUE;
	}

	void infoPanelSetMsgFilter(u32 filter/*LFILTER_DEFAULT*/)
	{
		s_outputFilter = filter;
		s_scrollToBottom = SCROLL_TRUE;
	}

	void infoPanelClearFeatures()
	{
		s_prevVertexFeature = {};
		s_prevWallFeature = {};
		s_prevSectorFeature = {};
		s_prevObjectFeature = {};
		s_wallShownLast = false;
	}

	s32 infoPanelGetHeight()
	{
		return s_infoHeight;
	}

	void infoToolBegin(s32 height)
	{
		bool infoTool = true;

		DisplayInfo displayInfo;
		TFE_RenderBackend::getDisplayInfo(&displayInfo);

		s_infoWith = 480.0f;
		s_infoPos = { (f32)displayInfo.width - 480.0f, 22.0f };

		ImGui::SetWindowPos("Info Panel", { s_infoPos.x, s_infoPos.z });
		ImGui::SetWindowSize("Info Panel", { s_infoWith, f32(height) });
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoTitleBar;

		ImGui::Begin("Info Panel", &infoTool, window_flags); ImGui::SameLine();
		s_infoTab = (InfoTab)handleTabs(s_infoTab, -12, -4, TAB_COUNT, c_infoTabs, c_infoToolTips, commitCurEntityChanges);

		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4);
		ImGui::Separator();
	}
		
	s32 outputScriptCmdCallback(ImGuiInputTextCallbackData* data)
	{
		if (!s_allowScriptHistoryCallback)
		{
			return 0;
		}
		s_allowScriptHistoryCallback = false;

		switch (data->EventFlag)
		{
			case ImGuiInputTextFlags_CallbackCompletion:
			{
				// TODO in Alpha-2 or later.
			} break;
			case ImGuiInputTextFlags_CallbackHistory:
			{
				s32 prevHistoryPtr = s_historyPtr;
				if (data->EventKey == ImGuiKey_UpArrow)
				{
					if (s_historyPtr > 0)
					{
						s_historyPtr--;
					}
				}
				else if (data->EventKey == ImGuiKey_DownArrow)
				{
					if (s_historyPtr < (s32)s_scriptCmdHistory.size())
					{
						s_historyPtr++;
					}
				}

				if (prevHistoryPtr != s_historyPtr)
				{
					data->DeleteChars(0, data->BufTextLen);
					if (s_historyPtr < (s32)s_scriptCmdHistory.size())
					{
						data->InsertChars(0, s_scriptCmdHistory[s_historyPtr].c_str());
					}
				}
			} break;
		}
		return 0;
	}
		
	s32 infoPanelOutput(s32 width)
	{
		s32 height = s_outputHeight;

		DisplayInfo info;
		TFE_RenderBackend::getDisplayInfo(&info);

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing;
		ImGui::SetNextWindowPos({ 0.0f, f32(info.height - height + 1) });
		ImGui::SetNextWindowSize({ f32(width), f32(height) });

		ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImGui::GetStyleColorVec4(ImGuiCol_TitleBg));
		if (ImGui::Begin("Output##MapInfo", nullptr, window_flags))
		{
			bool restoreHeight = s_collapsed;
			s_collapsed = false;

			const size_t count = s_outputMsg.size();
			const LeMessage* msg = s_outputMsg.data();
			const ImVec4 c_typeColor[] = { {1.0f, 1.0f, 1.0f, 0.7f}, {1.0f, 1.0f, 0.25f, 1.0f}, {1.0f, 0.25f, 0.25f, 1.0f}, {0.5f, 1.0f, 0.5f, 1.0f} };
						
			for (size_t i = 0; i < count; i++, msg++)
			{
				u32 typeFlag = 1 << msg->type;
				if (!(typeFlag & s_outputFilter)) { continue; }

				ImGui::TextColored(c_typeColor[msg->type], "%s", msg->msg.c_str());
			}

			if (s_outputFilter & LFILTER_SCRIPT)
			{
				// TODO (Alpha-2 or later):
				// * Allocate s_script and s_scriptLine at init-time to avoid bloating the EXE/statics.
				// * Add "clearHistory" command.
				// * Syntax coloring.
				// * Autocomplete.
				s_allowScriptHistoryCallback = true;  // Avoid issue with repeat keys, only accept one callback call per frame.
				if (ImGui::InputText("##ScriptInput", s_scriptLine, 4096, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory, outputScriptCmdCallback))
				{
					// Add the line to the output messages.
					if (s_scriptLine[0] != 0)
					{
						infoPanelAddMsg(LE_MSG_SCRIPT, "%s", s_scriptLine);
						s_scriptCmdHistory.push_back(s_scriptLine);
						s_historyPtr = (s32)s_scriptCmdHistory.size();
						strcat(s_script, s_scriptLine);
						memset(s_scriptLine, 0, 4096);
					}
					
					// Only execute the script if Enter is pressed.
					// If SHIFT+Enter is pressed, add a new line to the script but don't execute yet.
					if (!TFE_Input::keyModDown(KEYMOD_SHIFT))
					{
						executeLine(s_script);
						memset(s_script, 0, 65536);
					}

					// Scroll to the bottom and keep focus on the text input after pressing enter.
					s_scrollToBottom = SCROLL_TRUE;
					ImGui::FocusItem();
					ImGui::SetKeyboardFocusHere(-1);
				}
			}

			if (restoreHeight)
			{
				s_outputHeight = s_outputPrevHeight;
			}
			else
			{
				s_outputPrevHeight = s_outputHeight;
				s_outputHeight = max(26 * 3, (s32)ImGui::GetWindowSize().y);
			}

			// An extra frame is needed to handle adding a new item and scrolling to it.
			if (s_scrollToBottom > SCROLL_FALSE)
			{
				ImGui::SetScrollHereY(1.0f);
				s_scrollToBottom--;
			}
		}
		else
		{
			s_outputHeight = 26;
			s_collapsed = true;
		}
		ImGui::End();

		if (!s_collapsed)
		{
			const bool mousePressed = TFE_Input::mousePressed(MBUTTON_LEFT);
			
			ImGuiWindowFlags window_flags_tab = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoTitleBar |
				ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_Tooltip;
			ImGui::SetNextWindowPos({ 128.0f, f32(info.height - height + 1) });
			if (ImGui::Begin("##MapInfoTab", nullptr, window_flags_tab))
			{
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4);
				ImGui::PushStyleColor(ImGuiCol_Text, s_outputTabSel == 0 ? tabSelColor : tabStdColor);
				ImGui::Text("All");
				ImGui::PopStyleColor();
				if (mouseInsideItem() && mousePressed)
				{
					s_outputTabSel = 0;
					s_outputFilter = LFILTER_DEFAULT;
				}
				
				ImGui::SameLine(0.0f, 16.0f);
				ImGui::PushStyleColor(ImGuiCol_Text, s_outputTabSel == 1 ? tabSelColor : tabStdColor);
				ImGui::Text("Warnings");
				ImGui::PopStyleColor();
				if (mouseInsideItem() && mousePressed)
				{
					s_outputTabSel = 1;
					s_outputFilter = LFILTER_WARNING;
				}

				ImGui::SameLine(0.0f, 16.0f);
				ImGui::PushStyleColor(ImGuiCol_Text, s_outputTabSel == 2 ? tabSelColor : tabStdColor);
				ImGui::Text("Errors");
				ImGui::PopStyleColor();
				if (mouseInsideItem() && mousePressed)
				{
					s_outputTabSel = 2;
					s_outputFilter = LFILTER_ERROR;
				}

				ImGui::SameLine(0.0f, 16.0f);
				ImGui::PushStyleColor(ImGuiCol_Text, (s_outputFilter & LFILTER_SCRIPT) ? tabSelColor : tabStdColor);
				ImGui::Text("Script");
				ImGui::PopStyleColor();
				if (mouseInsideItem() && mousePressed)
				{
					if (s_outputFilter & LFILTER_SCRIPT) { s_outputFilter &= ~LFILTER_SCRIPT; }
					else { s_outputFilter |= LFILTER_SCRIPT; }
					memset(s_scriptLine, 0, 4096);
					memset(s_script, 0, 65536);
					s_scrollToBottom = SCROLL_TRUE;
				}
			}
			ImGui::End();
		}

		ImGui::PopStyleColor();

		return s_outputHeight;
	}

	void clearGroupSelection(Group* group)
	{
		// Clear the selection since some of it may be made non-interactable.
		selection_clear();
		selection_clearHovered();
	}

	void scrollToSector(EditorSector* sector)
	{
		// Set the correct layer.
		if (!(s_editFlags & LEF_SHOW_ALL_LAYERS))
		{
			s_curLayer = sector->layer;
		}
		// Center the view on the sector.
		if (s_view == EDIT_VIEW_2D)
		{
			const Vec2f mapPos =
			{
				 (sector->bounds[0].x + sector->bounds[1].x) * 0.5f,
				-(sector->bounds[0].z + sector->bounds[1].z) * 0.5f
			};
			const Vec2f target =
			{
				mapPos.x - (s_viewportSize.x / 2) * s_zoom2d,
				mapPos.z - (s_viewportSize.z / 2) * s_zoom2d
			};
			setViewportScrollTarget2d(target);
		}
		else if (s_view == EDIT_VIEW_3D)
		{
			// Compute the look at angles.
			const Vec3f mapPos =
			{
				(sector->bounds[0].x + sector->bounds[1].x) * 0.5f,
				(sector->bounds[0].y + sector->bounds[1].y) * 0.5f,
				(sector->bounds[0].z + sector->bounds[1].z) * 0.5f
			};
			f32 targetYaw, targetPitch;
			computeLookAt(mapPos, targetYaw, targetPitch);

			// Then move closer if needed.
			Vec3f cornerDelta = { sector->bounds[1].x - mapPos.x, sector->bounds[1].y - mapPos.y, sector->bounds[1].z - mapPos.z };
			f32 radius = sqrtf(cornerDelta.x*cornerDelta.x + cornerDelta.y*cornerDelta.y + cornerDelta.z*cornerDelta.z);
			f32 d = max(20.0f, radius * 2.0f);

			Vec3f posDelta = { mapPos.x - s_camera.pos.x, mapPos.y - s_camera.pos.y, mapPos.z - s_camera.pos.z };
			f32 dist = sqrtf(posDelta.x*posDelta.x + posDelta.y*posDelta.y + posDelta.z*posDelta.z);

			if (dist > d)
			{
				const f32 scale = (dist - d) / dist;
				const Vec3f targetPos =
				{
					s_camera.pos.x + posDelta.x * scale,
					s_camera.pos.y + posDelta.y * scale,
					s_camera.pos.z + posDelta.z * scale
				};
				setViewportScrollTarget3d(targetPos, targetYaw, targetPitch);
			}
			else
			{
				setViewportScrollTarget3d(s_camera.pos, targetYaw, targetPitch);
			}
		}
	}
		
	void selectAndScrollToSector(EditorSector* sector)
	{
		// Clear the selection, select the sector.
		selection_clear();

		// Set the edit mode to "Sector"
		edit_setEditMode(LEDIT_SECTOR);
		selection_sector(SA_SET, sector);
				
		scrollToSector(sector);
	}
		
	void listNamedSectorsInGroup(Group* group)
	{
		const s32 count = (s32)s_level.sectors.size();
		const s32 groupId = (s32)group->id;
		EditorSector* sector = s_level.sectors.data();

		Sublist sublist;
		sublist_begin(sublist, 32.0f/*tabSize*/, 256.0f/*itemWidth*/);
		for (s32 i = 0; i < count; i++, sector++)
		{
			if (sector->groupId != groupId || sector->name.empty())
			{
				continue;
			}
			if (sublist_item(sublist, sector->name.c_str(), selection_featureInCurrentSelection(sector)))
			{
				if (s_editMode == LEDIT_SECTOR)
				{
					const bool selToggle = TFE_Input::keyModDown(KEYMOD_CTRL);
					const bool setOrAdd = !selToggle || !selection_sector(SA_CHECK_INCLUSION, sector);
					if (selToggle)
					{
						selection_sector(setOrAdd ? SA_ADD : SA_REMOVE, sector);
					}
					else
					{
						selection_sector(SA_SET, sector);
					}

					if (setOrAdd)
					{
						scrollToSector(sector);
					}
				}
			}
		}
		sublist_end(sublist);
	}

	void infoPanelOpenGroup(s32 groupId)
	{
		Group* group = groups_getById(groupId);
		if (group)
		{
			s_groupOpen = group->index;
		}
	}

	void infoPanelMap()
	{
		const f32 iconBtnTint[] = { 103.0f / 255.0f, 122.0f / 255.0f, 139.0f / 255.0f, 1.0f };
		char name[64];
		strcpy(name, s_level.name.c_str());
		
		ImGui::Text("Level Name:"); ImGui::SameLine();

		if (ImGui::InputText("##InputLevelName", name, 64))
		{
			s_level.name = name;
		}
		s_textInputFocused |= ImGui::IsItemActive();

		ImGui::Text("Sector Count: %u", (u32)s_level.sectors.size());
		ImGui::Text("Bounds: (%0.3f, %0.3f, %0.3f)\n        (%0.3f, %0.3f, %0.3f)", s_level.bounds[0].x, s_level.bounds[0].y, s_level.bounds[0].z,
			s_level.bounds[1].x, s_level.bounds[1].y, s_level.bounds[1].z);
		ImGui::Text("Layer Range: [%d, %d]", s_level.layerRange[0], s_level.layerRange[1]);
		ImGui::LabelText("##GridLabel", "Grid Height");
		ImGui::SameLine(108.0f);
		ImGui::SetNextItemWidth(80.0f);

		ImGui::InputFloat("##GridHeight", &s_grid.height, 0.0f, 0.0f, "%0.2f", ImGuiInputTextFlags_CharsDecimal);
		s_textInputFocused |= ImGui::IsItemActive();

		ImGui::SameLine(0.0f, 16.0f);
		ImGui::SetNextItemWidth(86.0f);
		ImGui::LabelText("##DepthLabel", "View Depth");
		ImGui::SameLine(0.0f, 8.0f);
		ImGui::SetNextItemWidth(80.0f);

		ImGui::InputFloat("##ViewDepth0", &s_viewDepth[0], 0.0f, 0.0f, "%0.2f", ImGuiInputTextFlags_CharsDecimal);
		s_textInputFocused |= ImGui::IsItemActive();

		ImGui::SameLine(0.0f, 4.0f);
		ImGui::SetNextItemWidth(80.0f);
		ImGui::InputFloat("##ViewDepth1", &s_viewDepth[1], 0.0f, 0.0f, "%0.2f", ImGuiInputTextFlags_CharsDecimal);
		s_textInputFocused |= ImGui::IsItemActive();
			
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing;

		ImVec2 pos = ImGui::GetCursorPos();
		ImGui::SetNextWindowSize({ s_infoWith, s_infoHeight - pos.y - 43 });
		ImGui::SetNextWindowPos({ s_infoPos.x, pos.y + s_infoPos.z });
		ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImGui::GetStyleColorVec4(ImGuiCol_TitleBg));
		ImGui::Begin("Groups##MapInfo", nullptr, window_flags);
		{
			s32 groupCount = (s32)s_groups.size();
			Group* group = s_groups.data();
			for (s32 i = 0; i < groupCount; i++, group++)
			{
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4);
				if (iconButtonInline(s_groupOpen == i ? ICON_MINUS_SMALL : ICON_PLUS_SMALL, "Show named sectors in group.", iconBtnTint, true))
				{
					s_groupOpen = (s_groupOpen == i) ? -1 : i;
				}
				ImGui::SameLine(0.0f, 8.0f);
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);
				if (ImGui::Selectable(editor_getUniqueLabel(group->name.c_str()), s_groupCurrent == i, 0, ImVec2(264, 0)))
				{
					s_groupCurrent = i;
				}
				
				ImGui::SameLine(0.0f, 36.0f);
				if (iconButtonInline((group->flags & GRP_EXCLUDE) ? ICON_CIRCLE_BAN : ICON_CIRCLE, "Exclude group from export.", iconBtnTint, true))
				{
					if (group->flags & GRP_EXCLUDE) { group->flags &= ~GRP_EXCLUDE; }
					else { group->flags |= GRP_EXCLUDE; }
				}

				ImGui::SameLine(0.0f, 0.0f);
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
				if (iconButtonInline((group->flags & GRP_HIDDEN) ? ICON_EYE_CLOSED : ICON_EYE, "Hide or show the group.", iconBtnTint, true))
				{
					if (group->flags & GRP_HIDDEN) { group->flags &= ~GRP_HIDDEN; }
					else { group->flags |= GRP_HIDDEN; }
					clearGroupSelection(group);
				}

				ImGui::SameLine(0.0f, 0.0f);
				if (iconButtonInline((group->flags & GRP_LOCKED) ? ICON_LOCKED : ICON_UNLOCKED, "Lock or unlock the group for editing.", iconBtnTint, true))
				{
					if (group->flags & GRP_LOCKED) { group->flags &= ~GRP_LOCKED; }
					else { group->flags |= GRP_LOCKED; }
					clearGroupSelection(group);
				}

				ImGui::SameLine(0.0f, 0.0f);
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
				ImGui::ColorEdit3(editor_getUniqueLabel(""), group->color.m, ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoInputs);

				if (s_groupOpen == i)
				{
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
					listNamedSectorsInGroup(group);
				}
				else
				{
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 8);
				}

				ImGui::Separator();
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
			}
		}
		ImGui::End();
		ImGui::PopStyleColor();

		bool isDisabled = false;
		ImGui::SetCursorPosY(pos.y + s_infoPos.z + s_infoHeight - pos.y - 52);
		if (ImGui::Button("Add"))
		{
			groups_clearName();
			openEditorPopup(TFE_Editor::POPUP_GROUP_NAME);
		}
		ImGui::SameLine(0.0f, 8.0f);

		if (s_groupCurrent == 0)
		{
			disableNextItem();
			isDisabled = true;
		}
		if (ImGui::Button("Remove"))
		{
			groups_remove(s_groupCurrent);
		}

		ImGui::SameLine(0.0f, 8.0f);

		if (s_groupCurrent == 1 && !isDisabled)
		{
			disableNextItem();
			isDisabled = true;
		}
		if (ImGui::ArrowButton("##MoveUp", ImGuiDir_Up))
		{
			groups_moveUp(s_groupCurrent);
		}

		ImGui::SameLine(0.0f, 8.0f);

		if (s_groupCurrent == 0 || s_groupCurrent == (s32)s_groups.size() - 1)
		{
			if (!isDisabled)
			{
				disableNextItem();
			}
			isDisabled = true;
		}
		else if (isDisabled)
		{
			enableNextItem();
			isDisabled = false;
		}
		if (ImGui::ArrowButton("##MoveDown", ImGuiDir_Down))
		{
			groups_moveDown(s_groupCurrent);
		}
		if (isDisabled)
		{
			enableNextItem();
		}
	}

	// Manually created Vec2 control, so that the value is only updated when an input loses focus
	// or Return is pressed. This avoids vertices or other elements jittering as values are added, and
	// avoids many useless undo commands being created.
	// Return true if the value should be updated.
	bool inputVec2f(const char* id, Vec2f* value)
	{
		char id0[256], id1[256];
		sprintf(id0, "%sX", id);
		sprintf(id1, "%sZ", id);

		bool hadFocusX = false, hadFocusZ = false;
		Vec2f prevPos = *value;
		ImGui::PushItemWidth(98.0f);
		if (ImGui::InputFloat(id0, &value->x, 0.0, 0.0, "%0.6f", ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_NoUndoRedo))
		{
			if (value->x != prevPos.x)
			{
				hadFocusX = true;
			}
		}
		s_textInputFocused |= ImGui::IsItemActive();
		bool lostFocusX = !ImGui::IsItemFocused() && !ImGui::IsItemHovered() && !ImGui::IsItemActive();

		ImGui::SameLine();
		ImGui::PushItemWidth(98.0f);
		if (ImGui::InputFloat(id1, &value->z, 0.0, 0.0, "%0.6f", ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_NoUndoRedo))
		{
			if (value->z != prevPos.z)
			{
				hadFocusZ = true;
			}
		}
		s_textInputFocused |= ImGui::IsItemActive();
		bool lostFocusZ = !ImGui::IsItemFocused() && !ImGui::IsItemHovered() && !ImGui::IsItemActive();

		// Only update the value and add the edit when the input loses focus (clicking away or on another control) or
		// Enter/Return is pressed.
		bool retOrTabPressed = TFE_Input::keyPressed(KEY_RETURN) || TFE_Input::keyPressed(KEY_TAB);
		return ((lostFocusX || retOrTabPressed) && hadFocusX) ||
			((lostFocusZ || retOrTabPressed) && hadFocusZ);
	}

	void getPrevVertexFeature(EditorSector*& sector, s32& index)
	{
		// Make sure the previous selection is still valid.
		bool selectionInvalid = !s_prevVertexFeature.sector || s_prevVertexFeature.featureIndex < 0;
		if (s_prevVertexFeature.sector && s_prevVertexFeature.featureIndex >= (s32)s_prevVertexFeature.sector->vtx.size())
		{
			selectionInvalid = true;
		}

		if (selectionInvalid)
		{
			s_prevVertexFeature.sector = s_level.sectors.empty() ? nullptr : &s_level.sectors[0];
			if (s_prevVertexFeature.sector)
			{
				s_prevVertexFeature.featureIndex = 0;
			}
		}
		sector = s_prevVertexFeature.sector;
		index = s_prevVertexFeature.featureIndex;
	}

	void setPrevVertexFeature(EditorSector* sector, s32 index)
	{
		s_prevVertexFeature.sector = sector;
		s_prevVertexFeature.featureIndex = index;
	}

	bool editVertexPosition(EditorSector* sector, s32 index)
	{
		Vec2f curPos = sector->vtx[index];
		ImGui::Text("Vertex %d of Sector %d", index, sector->id);

		u32 compositeID = sector->id + (index << 16);
		char vec2Id[512];
		sprintf(vec2Id, "##VertexPosition%x", compositeID);
		if (inputVec2f(vec2Id, &curPos))
		{
			sector->vtx[index] = curPos;
			return true;
		}
		return false;
	}

	void infoPanelVertex()
	{
		s32 index = -1;
		EditorSector* sector = nullptr;

		ImGui::BeginChild("##VertexList");
		{
			const u32 count = selection_getCount();
			if (count <= 1)
			{
				if (count)
				{
					selection_getVertex(0, sector, index);
				}
				else if (selection_hasHovered())
				{
					selection_getVertex(SEL_INDEX_HOVERED, sector, index);
				}
				else
				{
					getPrevVertexFeature(sector, index);
				}
				if (index < 0 || !sector) { return; }
				setPrevVertexFeature(sector, index);
				if (editVertexPosition(sector, index))
				{
					sectorToPolygon(sector);

					s_idList.resize(1);
					s_idList[0] = sector->id;
					cmd_sectorSnapshot(LName_MoveVertex, s_idList);
				}
			}
			else
			{
				s_idList.clear();
				s_searchKey++;

				for (u32 i = 0; i < count; i++)
				{
					selection_getVertex(i, sector, index);
					if (i == 0)
					{
						setPrevVertexFeature(sector, index);
					}
					if (editVertexPosition(sector, index))
					{
						if (sector->searchKey != s_searchKey)
						{
							sector->searchKey = s_searchKey;
							s_idList.push_back(sector->id);
						}
					}
					if (i < count - 1)
					{
						ImGui::Separator();
					}
				}

				if (s_idList.empty())
				{
					// Take it back, no sectors added.
					s_searchKey--;
				}
				else
				{
					const s32 sectorCount = (s32)s_idList.size();
					const s32* idList = s_idList.data();
					for (s32 i = 0; i < sectorCount; i++)
					{
						sectorToPolygon(&s_level.sectors[idList[i]]);
					}
					cmd_sectorSnapshot(LName_MoveVertex, s_idList);
				}
			}
		}
		ImGui::EndChild();
	}

	void infoLabel(const char* labelId, const char* labelText, u32 width)
	{
		ImGui::PushItemWidth(f32(width));
		ImGui::LabelText(labelId, "%s", labelText);
		ImGui::PopItemWidth();
		ImGui::SameLine();
	}
		
	bool infoIntInput(const char* labelId, u32 width, s32* value, bool unset)
	{
		ImGui::PushItemWidth(f32(width));
		bool result = false;
		if (unset)
		{
			char buf[32] = { 0 };
			result = ImGui::InputTextWithHint(labelId, "unset", buf, 32, ImGuiInputTextFlags_CharsDecimal);
			if (result)
			{
				char* endPtr = nullptr;
				*value = strtol(buf, &endPtr, 10);
			}
			s_textInputFocused |= ImGui::IsItemActive();
		}
		else
		{
			result = ImGui::InputInt(labelId, value);
			s_textInputFocused |= ImGui::IsItemActive();
		}
		ImGui::PopItemWidth();
		return result;
	}

	bool infoUIntInput(const char* labelId, u32 width, u32* value, bool unset)
	{
		ImGui::PushItemWidth(f32(width));
		bool result = false;
		if (unset)
		{
			char buf[32] = { 0 };
			result = ImGui::InputTextWithHint(labelId, "unset", buf, 32, ImGuiInputTextFlags_CharsDecimal);
			if (result)
			{
				char* endPtr = nullptr;
				*value = strtoul(buf, &endPtr, 10);
			}
			s_textInputFocused |= ImGui::IsItemActive();
		}
		else
		{
			result = ImGui::InputUInt(labelId, value);
			s_textInputFocused |= ImGui::IsItemActive();
		}
		ImGui::PopItemWidth();
		return result;
	}

	bool infoFloatInput(const char* labelId, u32 width, f32* value, bool unset)
	{
		ImGui::PushItemWidth(f32(width));
		bool result = false;
		if (unset)
		{
			char buf[32] = { 0 };
			result = ImGui::InputTextWithHint(labelId, "unset", buf, 32, ImGuiInputTextFlags_CharsDecimal);
			if (result)
			{
				char* endPtr = nullptr;
				*value = (f32)strtod(buf, &endPtr);
			}
			s_textInputFocused |= ImGui::IsItemActive();
		}
		else
		{
			result = ImGui::InputFloat(labelId, value, 0.0f, 0.0f, "%.2f");
			s_textInputFocused |= ImGui::IsItemActive();
		}
		ImGui::PopItemWidth();
		return result;
	}

	void getPrevWallFeature(EditorSector*& sector, s32& index)
	{
		// Make sure the previous selection is still valid.
		bool selectionInvalid = !s_prevWallFeature.sector || s_prevWallFeature.featureIndex < 0;
		if (s_prevWallFeature.sector && s_prevWallFeature.featureIndex >= (s32)s_prevWallFeature.sector->walls.size())
		{
			selectionInvalid = true;
		}

		if (selectionInvalid)
		{
			s_prevWallFeature.sector = s_level.sectors.empty() ? nullptr : &s_level.sectors[0];
			if (s_prevWallFeature.sector)
			{
				s_prevWallFeature.featureIndex = 0;
			}
		}
		sector = s_prevWallFeature.sector;
		index = s_prevWallFeature.featureIndex;
	}

	struct WallInfo
	{
		EditorSector* sector;
		s32 wallIndex;
	};
	
	enum WallChangeFlags : u32
	{
		WCF_NONE = 0,
		WCF_LIGHT = FLAG_BIT(0),
		WCF_FLAG1_SET = FLAG_BIT(1),
		WCF_FLAG3_SET = FLAG_BIT(2),
	};
	enum SectorChangeFlags : u32
	{
		SCF_NONE = 0,
		SCF_LAYER = FLAG_BIT(0),
		SCF_AMBIENT = FLAG_BIT(1),
		SCF_FLOOR_HEIGHT = FLAG_BIT(2),
		SCF_SEC_HEIGHT = FLAG_BIT(3),
		SCF_CEIL_HEIGHT = FLAG_BIT(4),
		SCF_FLAG1_SET = FLAG_BIT(5),
		SCF_FLAG2_SET = FLAG_BIT(6),
		SCF_FLAG3_SET = FLAG_BIT(7),
	};

	struct WallChanges
	{
		bool applyChanges = false;
		u32 changes = WCF_NONE;
		s32 flags1Changed = 0;
		s32 flags3Changed = 0;
		s32 flags1Value = 0;
		s32 flags3Value = 0;
		s32 light = 0;
		s32 flags1_set = 0;
		s32 flags3_set = 0;
	};

	struct SectorChanges
	{
		bool applyChanges = false;
		u32 changes = SCF_NONE;
		s32 layer = 0;
		s32 ambient = 0;
		f32 floorHeight = 0.0f;
		f32 secHeight = 0.0f;
		f32 ceilHeight = 0.0f;
		s32 flags1Changed = 0;
		s32 flags1Value = 0;
		s32 flags1_set = 0;
		s32 flags2_set = 0;
		s32 flags3_set = 0;
	};

	static WallChanges s_wallChanges = {};
	static SectorChanges s_sectorChanges = {};
	std::vector<WallInfo> s_wallInfo;
	std::vector<EditorSector*> s_sectorInfo;
	static std::vector<s32> s_wallFlags;
	static std::vector<s32> s_sectorFlags;

	void addSectorInfo(EditorSector* sector)
	{
		const s32 count = (s32)s_sectorInfo.size();
		EditorSector** infoSector = s_sectorInfo.data();
		for (s32 i = 0; i < count; i++, infoSector++)
		{
			if (infoSector[i] == sector) { return; }
		}
		s_sectorInfo.push_back(sector);
		s_sectorFlags.push_back(sector->flags[0]);
		s_sectorFlags.push_back(sector->flags[1]);
		s_sectorFlags.push_back(sector->flags[2]);
	}

	void addWallInfo(EditorSector* sector, s32 wallIndex)
	{
		const s32 count = (s32)s_wallInfo.size();
		const WallInfo* info = s_wallInfo.data();
		for (s32 i = 0; i < count; i++, info++)
		{
			if (info->sector == sector && info->wallIndex == wallIndex) { return; }
		}
		s_wallInfo.push_back({ sector, wallIndex });
		s_wallFlags.push_back(sector->walls[wallIndex].flags[0]);
		s_wallFlags.push_back(sector->walls[wallIndex].flags[1]);
		s_wallFlags.push_back(sector->walls[wallIndex].flags[2]);
	}

	u32 getWallInfoList()
	{
		if (!s_wallInfo.empty())
		{
			return (u32)s_wallInfo.size();
		}

		const u32 count = selection_getCount(SEL_SURFACE);
		EditorSector* sector;
		s32 wallIndex;
		HitPart part;

		s_wallInfo.clear();
		s_wallFlags.clear();
		for (u32 i = 0; i < count; i++)
		{
			selection_getSurface(i, sector, wallIndex, &part);
			if (part != HP_FLOOR && part != HP_CEIL)
			{
				addWallInfo(sector, wallIndex);
			}
		}
		return (u32)s_wallInfo.size();
	}

	u32 getSectorInfoList()
	{
		if (!s_sectorInfo.empty())
		{
			return (u32)s_sectorInfo.size();
		}

		// If the mode selection changed suddenly - due to the INF editor - abort.
		SelectionListId selectType = selection_getCurrent();
		if (s_editMode == LEDIT_SECTOR && selectType != SEL_SECTOR ||
			s_editMode == LEDIT_WALL && selectType != SEL_SURFACE)
		{
			return 0;
		}

		s_sectorInfo.clear();
		s_sectorFlags.clear();
		const u32 count = selection_getCount();
		EditorSector* sector = nullptr;
		if (s_editMode == LEDIT_SECTOR)
		{
			s_sectorInfo.resize(count);
			for (s32 s = 0; s < (s32)count; s++)
			{
				selection_getSector(s, sector);
				s_sectorInfo[s] = sector;
				s_sectorFlags.push_back(sector->flags[0]);
				s_sectorFlags.push_back(sector->flags[1]);
				s_sectorFlags.push_back(sector->flags[2]);
			}
		}
		else if (s_editMode == LEDIT_WALL)
		{
			s32 wallIndex;
			HitPart part;
			for (s32 s = 0; s < (s32)count; s++)
			{
				selection_getSurface(s, sector, wallIndex, &part);
				if (part == HP_FLOOR || part == HP_CEIL)
				{
					addSectorInfo(sector);
				}
			}
		}
		return (u32)s_sectorInfo.size();
	}
		
	void infoPanel_addWallChangesToHistory()
	{
		// Add to history.
		const s32 count = (s32)s_wallInfo.size();
		if (count < 0) { return; }

		const s32 countPrev = (s32)s_prevPairs.size();
		bool pairsChanged = countPrev != count;

		s_pairs.resize(count);
		IndexPair* pair = s_pairs.data();
		const WallInfo* info = s_wallInfo.data();
		const IndexPair* prev = s_prevPairs.data();
		for (s32 i = 0; i < count; i++, info++)
		{
			pair[i] = { info->sector->id, info->wallIndex };
			if (!pairsChanged && (pair[i].i0 != prev[i].i0 || pair[i].i1 != prev[i].i1))
			{
				pairsChanged = true;
			}
		}

		// Collapse all attribute changes when possible.
		cmd_sectorWallSnapshot(LName_ChangeWallAttrib, s_pairs, pairsChanged);
		if (pairsChanged)
		{
			s_prevPairs.resize(count);
			memcpy(s_prevPairs.data(), s_pairs.data(), s_pairs.size() * sizeof(IndexPair));
		}
	}

	void infoPanel_addSectorChangesToHistory()
	{
		// Add to history.
		const s32 count = (s32)s_sectorInfo.size();
		if (count < 0) { return; }

		const s32 countPrev = (s32)s_prevPairs.size();
		bool pairsChanged = countPrev != count;

		s_pairs.resize(count);
		IndexPair* pair = s_pairs.data();
		EditorSector** info = s_sectorInfo.data();
		const IndexPair* prev = s_prevPairs.data();
		for (s32 i = 0; i < count; i++)
		{
			pair[i] = { info[i]->id, -1 };
			if (!pairsChanged && (pair[i].i0 != prev[i].i0 || pair[i].i1 != prev[i].i1))
			{
				pairsChanged = true;
			}
		}

		// Collapse all attribute changes when possible.
		cmd_sectorAttributeSnapshot(LName_ChangeSectorAttrib, s_pairs, pairsChanged);
		if (pairsChanged)
		{
			s_prevPairs.resize(count);
			memcpy(s_prevPairs.data(), s_pairs.data(), s_pairs.size() * sizeof(IndexPair));
		}
	}

	void applyWallChanges()
	{
		if (!s_wallChanges.applyChanges) { return; }
		s_wallChanges.applyChanges = false;
		if (s_wallInfo.empty()) { return; }

		s32 count = (s32)s_wallInfo.size();
		WallInfo* info = s_wallInfo.data();
		for (s32 i = 0; i < count; i++, info++)
		{
			EditorWall* wall = &info->sector->walls[info->wallIndex];
			if (s_wallChanges.changes & WCF_LIGHT)
			{
				wall->wallLight = s_wallChanges.light;
			}
			if (s_wallChanges.changes & WCF_FLAG1_SET)
			{
				wall->flags[0] = s_wallChanges.flags1_set;
			}
			else
			{
				wall->flags[0] = s_wallFlags[i * 3 + 0];
			}
			if (s_wallChanges.changes & WCF_FLAG3_SET)
			{
				wall->flags[2] = s_wallChanges.flags3_set;
			}
			else
			{
				wall->flags[2] = s_wallFlags[i * 3 + 2];
			}
			// flags.
			for (s32 f = 0; f < 31; f++)
			{
				s32 flagBit = 1 << f;
				if (s_wallChanges.flags1Changed & flagBit)
				{
					if (s_wallChanges.flags1Value & flagBit)
					{
						wall->flags[0] |= flagBit;
					}
					else
					{
						wall->flags[0] &= ~flagBit;
					}
				}

				if (s_wallChanges.flags3Changed & flagBit)
				{
					if (s_wallChanges.flags3Value & flagBit)
					{
						wall->flags[2] |= flagBit;
					}
					else
					{
						wall->flags[2] &= ~flagBit;
					}
				}
			}
		}
		infoPanel_addWallChangesToHistory();
	}

	void applySectorChanges()
	{
		if (!s_sectorChanges.applyChanges) { return; }
		s_sectorChanges.applyChanges = false;
		if (s_sectorInfo.empty()) { return; }

		s32 count = (s32)s_sectorInfo.size();
		EditorSector** info = s_sectorInfo.data();
		for (s32 i = 0; i < count; i++)
		{
			EditorSector* sector = info[i];
			if (s_sectorChanges.changes & SCF_LAYER)
			{
				sector->layer = s_sectorChanges.layer;
				// Adjust layer range.
				s_level.layerRange[0] = std::min(s_level.layerRange[0], sector->layer);
				s_level.layerRange[1] = std::max(s_level.layerRange[1], sector->layer);
				// Change the current layer so we can still see the sector.
				s_curLayer = sector->layer;
			}
			if (s_sectorChanges.changes & SCF_AMBIENT)
			{
				sector->ambient = (u32)s_sectorChanges.ambient;
			}
			if (s_sectorChanges.changes & SCF_FLOOR_HEIGHT)
			{
				sector->floorHeight = s_sectorChanges.floorHeight;
			}
			if (s_sectorChanges.changes & SCF_SEC_HEIGHT)
			{
				sector->secHeight = s_sectorChanges.secHeight;
			}
			if (s_sectorChanges.changes & SCF_CEIL_HEIGHT)
			{
				sector->ceilHeight = s_sectorChanges.ceilHeight;
			}
			if (s_sectorChanges.changes & SCF_FLAG1_SET)
			{
				sector->flags[0] = s_sectorChanges.flags1_set;
			}
			else
			{
				sector->flags[0] = s_sectorFlags[i * 3 + 0];
			}
			if (s_sectorChanges.changes & SCF_FLAG2_SET)
			{
				sector->flags[1] = s_sectorChanges.flags2_set;
			}
			else
			{
				sector->flags[1] = s_sectorFlags[i * 3 + 1];
			}
			if (s_sectorChanges.changes & SCF_FLAG3_SET)
			{
				sector->flags[2] = s_sectorChanges.flags3_set;
			}
			else
			{
				sector->flags[2] = s_sectorFlags[i * 3 + 2];
			}
			// flags.
			for (s32 f = 0; f < 31; f++)
			{
				const s32 flagBit = 1 << f;
				if (s_sectorChanges.flags1Changed & flagBit)
				{
					if (s_sectorChanges.flags1Value & flagBit)
					{
						sector->flags[0] |= flagBit;
					}
					else
					{
						sector->flags[0] &= ~flagBit;
					}
				}
			}
		}
		infoPanel_addSectorChangesToHistory();
	}
		
	void infoPanel_clearSelection()
	{
		// Clear out the wall change structure.
		s_wallChanges = {};
		s_sectorChanges = {};
		s_prevPairs.clear();
		s_wallInfo.clear();
		s_sectorInfo.clear();
	}

	enum InfoPanelId
	{
		IPANEL_VERTEX = 0,
		IPANEL_WALL,
		IPANEL_SECTOR,
	};

	void checkBoxMulti(const char* id, s32* flagsValue, s32* flagsChanged, s32 flagBit, InfoPanelId panelId)
	{
		const bool showGrayedOut = !((*flagsChanged) & flagBit);

		if (showGrayedOut) { ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
		{
			bool enable = true;
			if (!showGrayedOut && !((*flagsValue & flagBit)))
			{
				enable = false;
			}

			if (ImGui::CheckboxFlags(id, flagsValue, flagBit))
			{
				if (enable) { *flagsChanged |= flagBit; }
				else { *flagsChanged &= ~flagBit; *flagsValue &= ~flagBit; }
				if (panelId == IPANEL_WALL) { s_wallChanges.applyChanges = true; }
				else if (panelId == IPANEL_SECTOR) { s_sectorChanges.applyChanges = true; }
			}
		}
		if (showGrayedOut) { ImGui::PopStyleVar(); }
	}

	void textureTooltip(EditorTexture* tex)
	{
		if (!tex) { return; }
		char texInfo[256];
		sprintf(texInfo, "%s %dx%d", tex->name, tex->width, tex->height);
		setTooltip(texInfo);
	}

	void handleWallDragAndDrop(EditorSector* sector, s32 wallId, bool& changed, HitPart part)
	{
		if (ImGui::BeginDragDropTarget())
		{
			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Texture", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
			if (payload)
			{
				const s32 index = *((s32*)payload->Data);
				if (index >= 0)
				{
					FeatureId id = createFeatureId(sector, wallId, part);
					edit_setTexture(1, &id, index);
					changed = true;
				}
			}
			ImGui::EndDragDropTarget();
		}
	}

	void infoPanelWall()
	{
		s32 wallId = -1;
		EditorSector* sector = nullptr;
		u32 selWallCount = getWallInfoList();

		const bool insertTexture = s_selectedTexture >= 0 && isShortcutPressed(SHORTCUT_SET_TEXTURE, 0);
		const bool removeTexture = TFE_Input::mousePressed(MBUTTON_RIGHT);
		const s32 texIndex = insertTexture ? s_selectedTexture  : -1;

		if (selWallCount <= 1)
		{
			bool changed = false;
			if (selWallCount)
			{
				sector = s_wallInfo[0].sector;
				wallId = s_wallInfo[0].wallIndex;
			}
			else
			{
				if (selection_hasHovered())
				{
					HitPart part;
					selection_getSurface(SEL_INDEX_HOVERED, sector, wallId, &part);
					if (part == HP_FLOOR || part == HP_CEIL)
					{
						getPrevWallFeature(sector, wallId);
					}
				}
			}
			if (!sector || wallId < 0) { return; }

			s_prevWallFeature.prevSector = sector;
			s_prevWallFeature.featureIndex = wallId;
			s_wallShownLast = true;

			EditorWall* wall = sector->walls.data() + wallId;
			Vec2f* vertices = sector->vtx.data();
			f32 len = TFE_Math::distance(&vertices[wall->idx[0]], &vertices[wall->idx[1]]);

			ImGui::Text("Wall ID: %d  Sector ID: %d  Length: %2.2f", wallId, sector->id, len);
			ImGui::Text("Vertices: [%d](%2.2f, %2.2f), [%d](%2.2f, %2.2f)", wall->idx[0], vertices[wall->idx[0]].x, vertices[wall->idx[0]].z, wall->idx[1], vertices[wall->idx[1]].x, vertices[wall->idx[1]].z);
			ImGui::Separator();

			// Adjoin data (should be carefully and rarely edited directly).
			ImGui::LabelText("##SectorAdjoin", "Adjoin"); ImGui::SameLine(68.0f);
			changed |= infoIntInput("##SectorAdjoinInput", 96, &wall->adjoinId); ImGui::SameLine(180.0f);

			ImGui::LabelText("##SectorMirror", "Mirror"); ImGui::SameLine(240);
			changed |= infoIntInput("##SectorMirrorInput", 96, &wall->mirrorId);

			ImGui::SameLine(0.0f, 20.0f);
			if (ImGui::Button("Edit INF"))
			{
				TFE_Editor::openEditorPopup(POPUP_EDIT_INF, wallId, sector->name.empty() ? nullptr : (void*)sector->name.c_str());
			}

			s32 light = wall->wallLight;
			ImGui::LabelText("##SectorLight", "Light Adjustment"); ImGui::SameLine(148.0f);
			changed |= infoIntInput("##SectorLightInput", 96, &light);
			wall->wallLight = light;

			ImGui::Separator();

			// Flags
			infoLabel("##Flags1Label", "Flags1", 56);
			changed |= infoUIntInput("##Flags1", 128, &wall->flags[0]);
			ImGui::SameLine();

			infoLabel("##Flags3Label", "Flags3", 56);
			changed |= infoUIntInput("##Flags3", 128, &wall->flags[2]);

			const f32 column[] = { 0.0f, 174.0f, 324.0f };

			changed |= ImGui::CheckboxFlags("Mask Wall##WallFlag", &wall->flags[0], WF1_ADJ_MID_TEX); ImGui::SameLine(column[1]);
			changed |= ImGui::CheckboxFlags("Illum Sign##WallFlag", &wall->flags[0], WF1_ILLUM_SIGN); ImGui::SameLine(column[2]);
			changed |= ImGui::CheckboxFlags("Horz Flip Tex##SectorFlag", &wall->flags[0], WF1_FLIP_HORIZ);

			changed |= ImGui::CheckboxFlags("Change WallLight##WallFlag", &wall->flags[0], WF1_CHANGE_WALL_LIGHT); ImGui::SameLine(column[1]);
			changed |= ImGui::CheckboxFlags("Tex Anchored##WallFlag", &wall->flags[0], WF1_TEX_ANCHORED); ImGui::SameLine(column[2]);
			changed |= ImGui::CheckboxFlags("Wall Morphs##SectorFlag", &wall->flags[0], WF1_WALL_MORPHS);

			changed |= ImGui::CheckboxFlags("Scroll Top Tex##WallFlag", &wall->flags[0], WF1_SCROLL_TOP_TEX); ImGui::SameLine(column[1]);
			changed |= ImGui::CheckboxFlags("Scroll Mid Tex##WallFlag", &wall->flags[0], WF1_SCROLL_MID_TEX); ImGui::SameLine(column[2]);
			changed |= ImGui::CheckboxFlags("Scroll Bottom##SectorFlag", &wall->flags[0], WF1_SCROLL_BOT_TEX);

			changed |= ImGui::CheckboxFlags("Scroll Sign##WallFlag", &wall->flags[0], WF1_SCROLL_SIGN_TEX); ImGui::SameLine(column[1]);
			changed |= ImGui::CheckboxFlags("Hide On Map##WallFlag", &wall->flags[0], WF1_HIDE_ON_MAP); ImGui::SameLine(column[2]);
			changed |= ImGui::CheckboxFlags("Show On Map##SectorFlag", &wall->flags[0], WF1_SHOW_NORMAL_ON_MAP);

			changed |= ImGui::CheckboxFlags("Sign Anchored##WallFlag", &wall->flags[0], WF1_SIGN_ANCHORED); ImGui::SameLine(column[1]);
			changed |= ImGui::CheckboxFlags("Damage Wall##WallFlag", &wall->flags[0], WF1_DAMAGE_WALL); ImGui::SameLine(column[2]);
			changed |= ImGui::CheckboxFlags("Show As Ledge##SectorFlag", &wall->flags[0], WF1_SHOW_AS_LEDGE_ON_MAP);

			changed |= ImGui::CheckboxFlags("Show As Door##WallFlag", &wall->flags[0], WF1_SHOW_AS_DOOR_ON_MAP); ImGui::SameLine(column[1]);
			changed |= ImGui::CheckboxFlags("Always Walk##WallFlag", &wall->flags[2], WF3_ALWAYS_WALK); ImGui::SameLine(column[2]);
			changed |= ImGui::CheckboxFlags("Solid Wall##SectorFlag", &wall->flags[2], WF3_SOLID_WALL);

			changed |= ImGui::CheckboxFlags("Player Walk Only##WallFlag", &wall->flags[2], WF3_PLAYER_WALK_ONLY); ImGui::SameLine(column[1]);
			changed |= ImGui::CheckboxFlags("Cannot Shoot Thru##WallFlag", &wall->flags[2], WF3_CANNOT_FIRE_THROUGH);

			ImGui::Separator();

			EditorTexture* midTex = getTexture(wall->tex[WP_MID].texIndex);
			EditorTexture* topTex = getTexture(wall->tex[WP_TOP].texIndex);
			EditorTexture* botTex = getTexture(wall->tex[WP_BOT].texIndex);
			EditorTexture* sgnTex = getTexture(wall->tex[WP_SIGN].texIndex);

			const f32 texCol = 150.0f;
			// Labels
			ImGui::Text("Mid Texture"); ImGui::SameLine(texCol);
			ImGui::Text("Sign Texture");

			// Textures - Mid / Sign
			const f32 midScale = midTex ? 1.0f / (f32)std::max(midTex->width, midTex->height) : 0.0f;
			const f32 sgnScale = sgnTex ? 1.0f / (f32)std::max(sgnTex->width, sgnTex->height) : 0.0f;
			const f32 aspectMid[] = { midTex ? f32(midTex->width) * midScale : 1.0f, midTex ? f32(midTex->height) * midScale : 1.0f };
			const f32 aspectSgn[] = { sgnTex ? f32(sgnTex->width) * sgnScale : 1.0f, sgnTex ? f32(sgnTex->height) * sgnScale : 1.0f };

			ImGui::ImageButton(midTex ? TFE_RenderBackend::getGpuPtr(midTex->frames[0]) : nullptr, { 128.0f * aspectMid[0], 128.0f * aspectMid[1] }, { 0.0f, 1.0f }, { 1.0f, 0.0f });
			textureTooltip(midTex);
			if (texIndex >= 0 && ImGui::IsItemHovered())
			{
				FeatureId id = createFeatureId(sector, wallId, HP_MID);
				edit_setTexture(1, &id, texIndex);
				changed = true;
			}
			else
			{
				handleWallDragAndDrop(sector, wallId, changed, HP_MID);
			}

			ImGui::SameLine(texCol);
			ImGui::ImageButton(sgnTex ? TFE_RenderBackend::getGpuPtr(sgnTex->frames[0]) : nullptr, { 128.0f * aspectSgn[0], 128.0f * aspectSgn[1] }, { 0.0f, 1.0f }, { 1.0f, 0.0f });
			textureTooltip(sgnTex);
			if (texIndex >= 0 && ImGui::IsItemHovered())
			{
				FeatureId id = createFeatureId(sector, wallId, HP_SIGN);
				edit_setTexture(1, &id, texIndex);
				changed = true;
			}
			else if (removeTexture && ImGui::IsItemHovered())
			{
				FeatureId id = createFeatureId(sector, wallId, HP_SIGN);
				edit_clearTexture(1, &id);
				changed = true;
			}
			else
			{
				handleWallDragAndDrop(sector, wallId, changed, HP_SIGN);
			}

			const ImVec2 imageLeft0 = ImGui::GetItemRectMin();
			const ImVec2 imageRight0 = ImGui::GetItemRectMax();

			// Names
			ImGui::Text("%s", midTex ? midTex->name : "NONE"); ImGui::SameLine(texCol);
			ImGui::Text("%s", sgnTex ? sgnTex->name : "NONE");

			ImVec2 imageLeft1, imageRight1;
			if (wall->adjoinId >= 0)
			{
				ImGui::NewLine();

				// Textures - Top / Bottom
				// Labels
				ImGui::Text("Top Texture"); ImGui::SameLine(texCol); ImGui::Text("Bottom Texture");

				const f32 topScale = topTex ? 1.0f / (f32)std::max(topTex->width, topTex->height) : 0.0f;
				const f32 botScale = botTex ? 1.0f / (f32)std::max(botTex->width, botTex->height) : 0.0f;
				const f32 aspectTop[] = { topTex ? f32(topTex->width) * topScale : 1.0f, topTex ? f32(topTex->height) * topScale : 1.0f };
				const f32 aspectBot[] = { botTex ? f32(botTex->width) * botScale : 1.0f, botTex ? f32(botTex->height) * botScale : 1.0f };

				ImGui::ImageButton(topTex ? TFE_RenderBackend::getGpuPtr(topTex->frames[0]) : nullptr, { 128.0f * aspectTop[0], 128.0f * aspectTop[1] }, { 0.0f, 1.0f }, { 1.0f, 0.0f });
				textureTooltip(topTex);
				if (texIndex >= 0 && ImGui::IsItemHovered())
				{
					FeatureId id = createFeatureId(sector, wallId, HP_TOP);
					edit_setTexture(1, &id, texIndex);
					changed = true;
				}
				else
				{
					handleWallDragAndDrop(sector, wallId, changed, HP_TOP);
				}

				ImGui::SameLine(texCol);
				ImGui::ImageButton(botTex ? TFE_RenderBackend::getGpuPtr(botTex->frames[0]) : nullptr, { 128.0f * aspectBot[0], 128.0f * aspectBot[1] }, { 0.0f, 1.0f }, { 1.0f, 0.0f });
				textureTooltip(botTex);
				if (texIndex >= 0 && ImGui::IsItemHovered())
				{
					FeatureId id = createFeatureId(sector, wallId, HP_BOT);
					edit_setTexture(1, &id, texIndex);
					changed = true;
				}
				else
				{
					handleWallDragAndDrop(sector, wallId, changed, HP_BOT);
				}

				imageLeft1 = ImGui::GetItemRectMin();
				imageRight1 = ImGui::GetItemRectMax();

				// Names
				ImGui::Text("%s", topTex ? topTex->name : "NONE"); ImGui::SameLine(texCol);
				ImGui::Text("%s", botTex ? botTex->name : "NONE");
			}

			// Sign buttons.
			ImVec2 signLeft = { imageLeft0.x + texCol - 8.0f, imageLeft0.y + 2.0f };
			ImGui::SetNextWindowPos(signLeft);
			ImGui::BeginChild("##SignOptions", { 32, 48 });
			{
				if (ImGui::Button("X") && wall->tex[HP_SIGN].texIndex >= 0)
				{
					FeatureId id = createFeatureId(sector, wallId, HP_SIGN);
					edit_clearTexture(1, &id);
					changed = true;
				}
				if (ImGui::Button("C") && wall->tex[HP_SIGN].texIndex >= 0)
				{
					Vec2f prevOffset = wall->tex[HP_SIGN].offset;
					centerSignOnSurface(sector, wall);
					Vec2f delta = { wall->tex[HP_SIGN].offset.x - prevOffset.x, wall->tex[HP_SIGN].offset.z - prevOffset.z };

					if (delta.x || delta.z)
					{
						changed = true;
					}
				}
			}
			ImGui::EndChild();

			// Texture Offsets
			DisplayInfo displayInfo;
			TFE_RenderBackend::getDisplayInfo(&displayInfo);

			// Set 0
			ImVec2 offsetLeft = { imageLeft0.x + texCol + 8.0f + 16.0f, imageLeft0.y + 8.0f };
			ImVec2 offsetSize = { displayInfo.width - (imageLeft0.x + texCol + 16.0f - 32.0f), 128.0f };
			ImGui::SetNextWindowPos(offsetLeft);
			// A child window is used here in order to place the controls in the desired position - to the right of the image buttons.
			ImGui::BeginChild("##TextureOffsets0Wall", offsetSize);
			{
				ImGui::Text("Mid Offset");
				ImGui::PushItemWidth(136.0f);
				changed |= ImGui::InputFloat2("##MidOffsetInput", &wall->tex[WP_MID].offset.x, "%.3f");
				s_textInputFocused |= ImGui::IsItemActive();
				ImGui::PopItemWidth();

				ImGui::NewLine();

				ImGui::Text("Sign Offset");
				ImGui::PushItemWidth(136.0f);
				changed |= ImGui::InputFloat2("##SignOffsetInput", &wall->tex[WP_SIGN].offset.x, "%.3f");
				s_textInputFocused |= ImGui::IsItemActive();
				ImGui::PopItemWidth();
			}
			ImGui::EndChild();

			// Set 1
			if (wall->adjoinId >= 0)
			{
				offsetLeft = { imageLeft1.x + texCol + 8.0f, imageLeft1.y + 8.0f };
				offsetSize = { displayInfo.width - (imageLeft1.x + texCol + 16.0f), 128.0f };
				ImGui::SetNextWindowPos(offsetLeft);
				// A child window is used here in order to place the controls in the desired position - to the right of the image buttons.
				ImGui::BeginChild("##TextureOffsets1Wall", offsetSize);
				{
					ImGui::Text("Top Offset");
					ImGui::PushItemWidth(136.0f);
					changed |= ImGui::InputFloat2("##TopOffsetInput", &wall->tex[WP_TOP].offset.x, "%.3f");
					s_textInputFocused |= ImGui::IsItemActive();
					ImGui::PopItemWidth();

					ImGui::NewLine();

					ImGui::Text("Bottom Offset");
					ImGui::PushItemWidth(136.0f);
					changed |= ImGui::InputFloat2("##BotOffsetInput", &wall->tex[WP_BOT].offset.x, "%.3f");
					s_textInputFocused |= ImGui::IsItemActive();
					ImGui::PopItemWidth();
				}
				ImGui::EndChild();
			}

			if (changed)
			{
				infoPanel_addWallChangesToHistory();
			}
		}
		else
		{
			ImGui::Text("Multiple Walls Selected");
			ImGui::Separator();

			s32 light = s_wallChanges.light;
			ImGui::LabelText("##SectorLight", "Light Adjustment"); ImGui::SameLine(148.0f);
			if (infoIntInput("##SectorLightInput", 96, &light, !(s_wallChanges.changes & WCF_LIGHT)))
			{
				s_wallChanges.light = light;
				s_wallChanges.changes |= WCF_LIGHT;
				s_wallChanges.applyChanges = true;
			}
			ImGui::Separator();

			// Flags
			infoLabel("##Flags1Label", "Flags1", 56);
			if (infoIntInput("##Flags1", 128, &s_wallChanges.flags1_set, !(s_wallChanges.changes & WCF_FLAG1_SET)))
			{
				s_wallChanges.changes |= WCF_FLAG1_SET;
				s_wallChanges.applyChanges = true;
			}
			ImGui::SameLine();

			infoLabel("##Flags3Label", "Flags3", 56);
			if (infoIntInput("##Flags3", 128, &s_wallChanges.flags3_set, !(s_wallChanges.changes & WCF_FLAG3_SET)))
			{
				s_wallChanges.changes |= WCF_FLAG3_SET;
				s_wallChanges.applyChanges = true;
			}

			const f32 column[] = { 0.0f, 174.0f, 324.0f };

			checkBoxMulti("Mask Wall##WallFlag", &s_wallChanges.flags1Value, &s_wallChanges.flags1Changed, WF1_ADJ_MID_TEX, IPANEL_WALL); ImGui::SameLine(column[1]);
			checkBoxMulti("Illum Sign##WallFlag", &s_wallChanges.flags1Value, &s_wallChanges.flags1Changed, WF1_ILLUM_SIGN, IPANEL_WALL); ImGui::SameLine(column[2]);
			checkBoxMulti("Horz Flip Tex##SectorFlag", &s_wallChanges.flags1Value, &s_wallChanges.flags1Changed, WF1_FLIP_HORIZ, IPANEL_WALL);

			checkBoxMulti("Change WallLight##WallFlag", &s_wallChanges.flags1Value, &s_wallChanges.flags1Changed, WF1_CHANGE_WALL_LIGHT, IPANEL_WALL); ImGui::SameLine(column[1]);
			checkBoxMulti("Tex Anchored##WallFlag", &s_wallChanges.flags1Value, &s_wallChanges.flags1Changed, WF1_TEX_ANCHORED, IPANEL_WALL); ImGui::SameLine(column[2]);
			checkBoxMulti("Wall Morphs##SectorFlag", &s_wallChanges.flags1Value, &s_wallChanges.flags1Changed, WF1_WALL_MORPHS, IPANEL_WALL);

			checkBoxMulti("Scroll Top Tex##WallFlag", &s_wallChanges.flags1Value, &s_wallChanges.flags1Changed, WF1_SCROLL_TOP_TEX, IPANEL_WALL); ImGui::SameLine(column[1]);
			checkBoxMulti("Scroll Mid Tex##WallFlag", &s_wallChanges.flags1Value, &s_wallChanges.flags1Changed, WF1_SCROLL_MID_TEX, IPANEL_WALL); ImGui::SameLine(column[2]);
			checkBoxMulti("Scroll Bottom##SectorFlag", &s_wallChanges.flags1Value, &s_wallChanges.flags1Changed, WF1_SCROLL_BOT_TEX, IPANEL_WALL);

			checkBoxMulti("Scroll Sign##WallFlag", &s_wallChanges.flags1Value, &s_wallChanges.flags1Changed, WF1_SCROLL_SIGN_TEX, IPANEL_WALL); ImGui::SameLine(column[1]);
			checkBoxMulti("Hide On Map##WallFlag", &s_wallChanges.flags1Value, &s_wallChanges.flags1Changed, WF1_HIDE_ON_MAP, IPANEL_WALL); ImGui::SameLine(column[2]);
			checkBoxMulti("Show On Map##SectorFlag", &s_wallChanges.flags1Value, &s_wallChanges.flags1Changed, WF1_SHOW_NORMAL_ON_MAP, IPANEL_WALL);

			checkBoxMulti("Sign Anchored##WallFlag", &s_wallChanges.flags1Value, &s_wallChanges.flags1Changed, WF1_SIGN_ANCHORED, IPANEL_WALL); ImGui::SameLine(column[1]);
			checkBoxMulti("Damage Wall##WallFlag", &s_wallChanges.flags1Value, &s_wallChanges.flags1Changed, WF1_DAMAGE_WALL, IPANEL_WALL); ImGui::SameLine(column[2]);
			checkBoxMulti("Show As Ledge##SectorFlag", &s_wallChanges.flags1Value, &s_wallChanges.flags1Changed, WF1_SHOW_AS_LEDGE_ON_MAP, IPANEL_WALL);

			checkBoxMulti("Show As Door##WallFlag", &s_wallChanges.flags1Value, &s_wallChanges.flags1Changed, WF1_SHOW_AS_DOOR_ON_MAP, IPANEL_WALL); ImGui::SameLine(column[1]);
			checkBoxMulti("Always Walk##WallFlag", &s_wallChanges.flags3Value, &s_wallChanges.flags3Changed, WF3_ALWAYS_WALK, IPANEL_WALL); ImGui::SameLine(column[2]);
			checkBoxMulti("Solid Wall##SectorFlag", &s_wallChanges.flags3Value, &s_wallChanges.flags3Changed, WF3_SOLID_WALL, IPANEL_WALL);

			checkBoxMulti("Player Walk Only##WallFlag", &s_wallChanges.flags3Value, &s_wallChanges.flags3Changed, WF3_PLAYER_WALK_ONLY, IPANEL_WALL); ImGui::SameLine(column[1]);
			checkBoxMulti("Cannot Shoot Thru##WallFlag", &s_wallChanges.flags3Value, &s_wallChanges.flags3Changed, WF3_CANNOT_FIRE_THROUGH, IPANEL_WALL);

			ImGui::Separator();
			applyWallChanges();
		}
	}

	void fixupSectorName(char* sectorName)
	{
		const size_t len = strlen(sectorName);
		for (size_t i = 0; i < len; i++)
		{
			// Spaces are not allowed in sector names, allowing them is a big source of issues.
			if (sectorName[i] == ' ')
			{
				sectorName[i] = '-';
			}
		}
	}

	void infoPanelSector()
	{
		bool insertTexture = s_selectedTexture >= 0 && TFE_Input::keyPressed(KEY_T);
		s32 texIndex = -1;
		if (insertTexture)
		{
			texIndex = s_selectedTexture;
		}

		s32 wallId = -1;
		EditorSector* sector = nullptr;
		u32 selSectorCount = getSectorInfoList();
		if (selSectorCount <= 1)
		{
			bool changed = false;
			if (selSectorCount < 1) { selection_getSector(SEL_INDEX_HOVERED, sector); }
			else { sector = s_sectorInfo[0]; }
			if (!sector) { return; }
			s_wallShownLast = false;

			ImGui::Text("Sector ID: %d      Wall Count: %u", sector->id, (u32)sector->walls.size());
			ImGui::Separator();

			// Sector Name (optional, used by INF)
			char sectorName[64];
			char inputName[256];
			strcpy(sectorName, sector->name.c_str());
			sprintf(inputName, "##NameSector%d", sector->id);

			infoLabel("##NameLabel", "Name", 42);
			ImGui::PushItemWidth(240.0f);

			// Turn the name red if it matches another sector.
			s32 otherNameId = findSectorByName(sectorName, sector->id);
			if (otherNameId >= 0) { ImGui::PushStyleColor(ImGuiCol_Text, { 1.0f, 0.2f, 0.2f, 1.0f }); }
			if (ImGui::InputText(inputName, sectorName, getSectorNameLimit()))
			{
				fixupSectorName(sectorName);
				sector->name = sectorName;
				changed = true;
			}
			s_textInputFocused |= ImGui::IsItemActive();
			ImGui::PopItemWidth();
			if (otherNameId >= 0) { ImGui::PopStyleColor(); }

			ImGui::SameLine(0.0f, 28.0f);
			if (ImGui::Button("Edit INF"))
			{
				TFE_Editor::openEditorPopup(POPUP_EDIT_INF, 0xffffffff, sector->name.empty() ? nullptr : (void*)sector->name.c_str());
			}

			// Layer and Ambient
			s32 layer = sector->layer;
			infoLabel("##LayerLabel", "Layer", 42);
			changed |= infoIntInput("##LayerSector", 96, &layer);
			if (layer != sector->layer)
			{
				sector->layer = layer;
				// Adjust layer range.
				s_level.layerRange[0] = std::min(s_level.layerRange[0], (s32)layer);
				s_level.layerRange[1] = std::max(s_level.layerRange[1], (s32)layer);
				// Change the current layer so we can still see the sector.
				s_curLayer = layer;
			}

			ImGui::SameLine(0.0f, 8.0f);

			s32 ambient = (s32)sector->ambient;
			infoLabel("##AmbientLabel", "Ambient", 64);
			changed |= infoIntInput("##AmbientSector", 96, &ambient);
			sector->ambient = std::max(0, std::min(31, ambient));

			// Heights
			infoLabel("##FloorHeightLabel", "Floor", 42);
			changed |= infoFloatInput("##FloorHeight", 64 + 8, &sector->floorHeight);
			ImGui::SameLine();

			infoLabel("##SecondHeightLabel", "Second", 52);
			changed |= infoFloatInput("##SecondHeight", 64, &sector->secHeight);
			ImGui::SameLine();

			infoLabel("##CeilHeightLabel", "Ceiling", 60);
			changed |= infoFloatInput("##CeilHeight", 64, &sector->ceilHeight);

			ImGui::Separator();

			// Flags
			infoLabel("##Flags1Label", "Flags1", 56);
			changed |= infoUIntInput("##Flags1", 128, &sector->flags[0]);
			ImGui::SameLine();

			infoLabel("##Flags2Label", "Flags2", 56);
			changed |= infoUIntInput("##Flags2", 128, &sector->flags[1]);

			infoLabel("##Flags3Label", "Flags3", 56);
			changed |= infoUIntInput("##Flags3", 128, &sector->flags[2]);

			const f32 column[] = { 0.0f, 160.0f, 320.0f };

			changed |= ImGui::CheckboxFlags("Exterior##SectorFlag", &sector->flags[0], SEC_FLAGS1_EXTERIOR); ImGui::SameLine(column[1]);
			changed |= ImGui::CheckboxFlags("Pit##SectorFlag", &sector->flags[0], SEC_FLAGS1_PIT); ImGui::SameLine(column[2]);
			changed |= ImGui::CheckboxFlags("No Walls##SectorFlag", &sector->flags[0], SEC_FLAGS1_NOWALL_DRAW);

			changed |= ImGui::CheckboxFlags("Ext Ceil Adj##SectorFlag", &sector->flags[0], SEC_FLAGS1_EXT_ADJ); ImGui::SameLine(column[1]);
			changed |= ImGui::CheckboxFlags("Ext Floor Adj##SectorFlag", &sector->flags[0], SEC_FLAGS1_EXT_FLOOR_ADJ);  ImGui::SameLine(column[2]);
			changed |= ImGui::CheckboxFlags("Secret##SectorFlag", &sector->flags[0], SEC_FLAGS1_SECRET);

			changed |= ImGui::CheckboxFlags("Door##SectorFlag", &sector->flags[0], SEC_FLAGS1_DOOR); ImGui::SameLine(column[1]);
			changed |= ImGui::CheckboxFlags("Ice Floor##SectorFlag", &sector->flags[0], SEC_FLAGS1_ICE_FLOOR); ImGui::SameLine(column[2]);
			changed |= ImGui::CheckboxFlags("Snow Floor##SectorFlag", &sector->flags[0], SEC_FLAGS1_SNOW_FLOOR);

			changed |= ImGui::CheckboxFlags("Crushing##SectorFlag", &sector->flags[0], SEC_FLAGS1_CRUSHING); ImGui::SameLine(column[1]);
			changed |= ImGui::CheckboxFlags("Low Damage##SectorFlag", &sector->flags[0], SEC_FLAGS1_LOW_DAMAGE); ImGui::SameLine(column[2]);
			changed |= ImGui::CheckboxFlags("High Damage##SectorFlag", &sector->flags[0], SEC_FLAGS1_HIGH_DAMAGE);

			changed |= ImGui::CheckboxFlags("No Smart Obj##SectorFlag", &sector->flags[0], SEC_FLAGS1_NO_SMART_OBJ); ImGui::SameLine(column[1]);
			changed |= ImGui::CheckboxFlags("Smart Obj##SectorFlag", &sector->flags[0], SEC_FLAGS1_SMART_OBJ); ImGui::SameLine(column[2]);
			changed |= ImGui::CheckboxFlags("Safe Sector##SectorFlag", &sector->flags[0], SEC_FLAGS1_SAFESECTOR);

			changed |= ImGui::CheckboxFlags("Mag Seal##SectorFlag", &sector->flags[0], SEC_FLAGS1_MAG_SEAL); ImGui::SameLine(column[1]);
			changed |= ImGui::CheckboxFlags("Exploding Wall##SectorFlag", &sector->flags[0], SEC_FLAGS1_EXP_WALL);

			ImGui::Separator();

			// Textures
			EditorTexture* floorTex = getTexture(sector->floorTex.texIndex);
			EditorTexture* ceilTex = getTexture(sector->ceilTex.texIndex);

			void* floorPtr = floorTex ? TFE_RenderBackend::getGpuPtr(floorTex->frames[0]) : nullptr;
			void* ceilPtr = ceilTex ? TFE_RenderBackend::getGpuPtr(ceilTex->frames[0]) : nullptr;

			const f32 texCol = 150.0f;
			// Labels
			ImGui::Text("Floor Texture"); ImGui::SameLine(texCol);
			ImGui::Text("Ceiling Texture");

			// Textures
			const f32 floorScale = floorTex ? 1.0f / (f32)std::max(floorTex->width, floorTex->height) : 1.0f;
			const f32 ceilScale = ceilTex ? 1.0f / (f32)std::max(ceilTex->width, ceilTex->height) : 1.0f;
			const f32 aspectFloor[] = { floorTex ? f32(floorTex->width) * floorScale : 1.0f, floorTex ? f32(floorTex->height) * floorScale : 1.0f };
			const f32 aspectCeil[] = { ceilTex ? f32(ceilTex->width) * ceilScale : 1.0f, ceilTex ? f32(ceilTex->height) * ceilScale : 1.0f };

			ImGui::ImageButton(floorPtr, { 128.0f * aspectFloor[0], 128.0f * aspectFloor[1] }, { 0.0f, 1.0f }, { 1.0f, 0.0f });
			textureTooltip(floorTex);
			if (texIndex >= 0 && ImGui::IsItemHovered())
			{
				FeatureId id = createFeatureId(sector, 0, HP_FLOOR);
				edit_setTexture(1, &id, texIndex);
				changed = true;
			}
			else if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Texture", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
				if (payload)
				{
					const s32 index = *((s32*)payload->Data);
					if (index >= 0)
					{
						FeatureId id = createFeatureId(sector, 0, HP_FLOOR);
						edit_setTexture(1, &id, index);
						changed = true;
					}
				}
				ImGui::EndDragDropTarget();
			}

			ImGui::SameLine(texCol);
			ImGui::ImageButton(ceilPtr, { 128.0f * aspectCeil[0], 128.0f * aspectCeil[1] }, { 0.0f, 1.0f }, { 1.0f, 0.0f });
			textureTooltip(ceilTex);
			if (texIndex >= 0 && ImGui::IsItemHovered())
			{
				FeatureId id = createFeatureId(sector, 0, HP_CEIL);
				edit_setTexture(1, &id, texIndex);
				changed = true;
			}
			else if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Texture", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
				if (payload)
				{
					const s32 index = *((s32*)payload->Data);
					if (index >= 0)
					{
						FeatureId id = createFeatureId(sector, 0, HP_CEIL);
						edit_setTexture(1, &id, index);
						changed = true;
					}
				}
				ImGui::EndDragDropTarget();
			}

			const ImVec2 imageLeft = ImGui::GetItemRectMin();
			const ImVec2 imageRight = ImGui::GetItemRectMax();

			// Names
			if (floorTex)
			{
				ImGui::Text("%s", floorTex->name); ImGui::SameLine(texCol);
			}
			if (ceilTex)
			{
				ImGui::Text("%s", ceilTex->name); ImGui::SameLine();
			}

			// Texture Offsets
			DisplayInfo displayInfo;
			TFE_RenderBackend::getDisplayInfo(&displayInfo);

			ImVec2 offsetLeft = { imageLeft.x + texCol + 8.0f, imageLeft.y + 8.0f };
			ImVec2 offsetSize = { displayInfo.width - (imageLeft.x + texCol + 16.0f), 128.0f };
			ImGui::SetNextWindowPos(offsetLeft);
			// A child window is used here in order to place the controls in the desired position - to the right of the image buttons.
			ImGui::BeginChild("##TextureOffsetsSector", offsetSize);
			{
				ImGui::Text("Floor Offset");
				ImGui::PushItemWidth(136.0f);
				changed |= ImGui::InputFloat2("##FloorOffsetInput", &sector->floorTex.offset.x, "%.3f");
				s_textInputFocused |= ImGui::IsItemActive();
				ImGui::PopItemWidth();

				ImGui::NewLine();

				ImGui::Text("Ceil Offset");
				ImGui::PushItemWidth(136.0f);
				changed |= ImGui::InputFloat2("##CeilOffsetInput", &sector->ceilTex.offset.x, "%.3f");
				s_textInputFocused |= ImGui::IsItemActive();
				ImGui::PopItemWidth();
			}
			ImGui::EndChild();

			if (project_supportsFeature(FEATURE_SLOPES))
			{
				ImGui::Separator();
				u32 prevFlags = sector->flags[0];
				changed |= ImGui::CheckboxFlags("Floor Slope##SectorFlag", &sector->flags[0], SEC_FLAGS1_SLOPEDFLOOR); ImGui::SameLine(column[1]);
				changed |= ImGui::CheckboxFlags("Ceiling Slope##SectorFlag", &sector->flags[0], SEC_FLAGS1_SLOPEDCEILING);
				if (sector->flags[0] & SEC_FLAGS1_SLOPEDFLOOR)
				{
					if (!(prevFlags & SEC_FLAGS1_SLOPEDFLOOR))
					{
						sector->slope.floorAnchorSectorId = sector->id;
					}

					ImGui::Separator();
					ImGui::Text("Floor Slope");
					ImGui::PushItemWidth(128.0f);
					ImGui::InputUInt("Wall Anchor Sector ID##FloorSlope", &sector->slope.floorAnchorSectorId);
					ImGui::PushItemWidth(128.0f);
					ImGui::InputUInt("Wall Anchor Wall ID##FloorSlope", &sector->slope.floorAnchorWallId);
					ImGui::PushItemWidth(128.0f);
					ImGui::InputFloat("Slope Angle##FloorSlope", &sector->slope.floorSlopeAngle);
				}
				if (sector->flags[0] & SEC_FLAGS1_SLOPEDCEILING)
				{
					if (!(prevFlags & SEC_FLAGS1_SLOPEDCEILING))
					{
						sector->slope.ceilAnchorSectorId = sector->id;
					}

					ImGui::Separator();
					ImGui::Text("Ceiling Slope");
					ImGui::PushItemWidth(128.0f);
					ImGui::InputUInt("Wall Anchor Sector ID##CeilSlope", &sector->slope.ceilAnchorSectorId);
					ImGui::PushItemWidth(128.0f);
					ImGui::InputUInt("Wall Anchor Wall ID##CeilSlope", &sector->slope.ceilAnchorWallId);
					ImGui::PushItemWidth(128.0f);
					ImGui::InputFloat("Slope Angle##CeilSlope", &sector->slope.ceilSlopeAngle);
				}
			}

			if (changed)
			{
				infoPanel_addSectorChangesToHistory();
			}
		}
		else  // selSectorCount > 1
		{
			ImGui::Text("Multiple Sectors Selected");
			ImGui::Separator();

			// Layer and Ambient
			s32 layer = s_sectorChanges.layer;
			infoLabel("##LayerLabel", "Layer", 42);
			if (infoIntInput("##LayerSector", 96, &layer, !(s_sectorChanges.changes & SCF_LAYER)))
			{
				s_sectorChanges.layer = layer;
				s_sectorChanges.changes |= SCF_LAYER;
				s_sectorChanges.applyChanges = true;
			}

			ImGui::SameLine(0.0f, 8.0f);

			s32 ambient = (s32)s_sectorChanges.ambient;
			infoLabel("##AmbientLabel", "Ambient", 64);
			if (infoIntInput("##AmbientSector", 96, &ambient, !(s_sectorChanges.changes & SCF_AMBIENT)))
			{
				s_sectorChanges.ambient = std::max(0, std::min(31, ambient));
				s_sectorChanges.changes |= SCF_AMBIENT;
				s_sectorChanges.applyChanges = true;
			}

			// Heights
			infoLabel("##FloorHeightLabel", "Floor", 42);
			if (infoFloatInput("##FloorHeight", 64 + 8, &s_sectorChanges.floorHeight, !(s_sectorChanges.changes & SCF_FLOOR_HEIGHT)))
			{
				s_sectorChanges.changes |= SCF_FLOOR_HEIGHT;
				s_sectorChanges.applyChanges = true;
			}
			ImGui::SameLine();

			infoLabel("##SecondHeightLabel", "Second", 52);
			if (infoFloatInput("##SecondHeight", 64, &s_sectorChanges.secHeight, !(s_sectorChanges.changes & SCF_SEC_HEIGHT)))
			{
				s_sectorChanges.changes |= SCF_SEC_HEIGHT;
				s_sectorChanges.applyChanges = true;
			}
			ImGui::SameLine();

			infoLabel("##CeilHeightLabel", "Ceiling", 60);
			if (infoFloatInput("##CeilHeight", 64, &s_sectorChanges.ceilHeight, !(s_sectorChanges.changes & SCF_CEIL_HEIGHT)))
			{
				s_sectorChanges.changes |= SCF_CEIL_HEIGHT;
				s_sectorChanges.applyChanges = true;
			}
			ImGui::Separator();

			// Flags
			infoLabel("##Flags1Label", "Flags1", 56);
			u32 flags1 = s_sectorChanges.flags1_set;
			if (infoUIntInput("##Flags1", 128, &flags1, !(s_sectorChanges.changes & SCF_FLAG1_SET)))
			{
				s_sectorChanges.flags1_set = flags1;
				s_sectorChanges.changes |= SCF_FLAG1_SET;
				s_sectorChanges.applyChanges = true;
			}
			ImGui::SameLine();

			infoLabel("##Flags2Label", "Flags2", 56);
			u32 flags2 = s_sectorChanges.flags2_set;
			if (infoUIntInput("##Flags2", 128, &flags2, !(s_sectorChanges.changes & SCF_FLAG1_SET)))
			{
				s_sectorChanges.flags1_set = flags2;
				s_sectorChanges.changes |= SCF_FLAG2_SET;
				s_sectorChanges.applyChanges = true;
			}

			infoLabel("##Flags3Label", "Flags3", 56);
			u32 flags3 = s_sectorChanges.flags3_set;
			if (infoUIntInput("##Flags3", 128, &flags3, !(s_sectorChanges.changes & SCF_FLAG1_SET)))
			{
				s_sectorChanges.flags1_set = flags3;
				s_sectorChanges.changes |= SCF_FLAG3_SET;
				s_sectorChanges.applyChanges = true;
			}

			const f32 column[] = { 0.0f, 160.0f, 320.0f };

			checkBoxMulti("Exterior##SectorFlag", &s_sectorChanges.flags1Value, &s_sectorChanges.flags1Changed, SEC_FLAGS1_EXTERIOR, IPANEL_SECTOR); ImGui::SameLine(column[1]);
			checkBoxMulti("Pit##SectorFlag", &s_sectorChanges.flags1Value, &s_sectorChanges.flags1Changed, SEC_FLAGS1_PIT, IPANEL_SECTOR); ImGui::SameLine(column[2]);
			checkBoxMulti("No Walls##SectorFlag", &s_sectorChanges.flags1Value, &s_sectorChanges.flags1Changed, SEC_FLAGS1_NOWALL_DRAW, IPANEL_SECTOR);

			checkBoxMulti("Ext Ceil Adj##SectorFlag", &s_sectorChanges.flags1Value, &s_sectorChanges.flags1Changed, SEC_FLAGS1_EXT_ADJ, IPANEL_SECTOR); ImGui::SameLine(column[1]);
			checkBoxMulti("Ext Floor Adj##SectorFlag", &s_sectorChanges.flags1Value, &s_sectorChanges.flags1Changed, SEC_FLAGS1_EXT_FLOOR_ADJ, IPANEL_SECTOR);  ImGui::SameLine(column[2]);
			checkBoxMulti("Secret##SectorFlag", &s_sectorChanges.flags1Value, &s_sectorChanges.flags1Changed, SEC_FLAGS1_SECRET, IPANEL_SECTOR);

			checkBoxMulti("Door##SectorFlag", &s_sectorChanges.flags1Value, &s_sectorChanges.flags1Changed, SEC_FLAGS1_DOOR, IPANEL_SECTOR); ImGui::SameLine(column[1]);
			checkBoxMulti("Ice Floor##SectorFlag", &s_sectorChanges.flags1Value, &s_sectorChanges.flags1Changed, SEC_FLAGS1_ICE_FLOOR, IPANEL_SECTOR); ImGui::SameLine(column[2]);
			checkBoxMulti("Snow Floor##SectorFlag", &s_sectorChanges.flags1Value, &s_sectorChanges.flags1Changed, SEC_FLAGS1_SNOW_FLOOR, IPANEL_SECTOR);

			checkBoxMulti("Crushing##SectorFlag", &s_sectorChanges.flags1Value, &s_sectorChanges.flags1Changed, SEC_FLAGS1_CRUSHING, IPANEL_SECTOR); ImGui::SameLine(column[1]);
			checkBoxMulti("Low Damage##SectorFlag", &s_sectorChanges.flags1Value, &s_sectorChanges.flags1Changed, SEC_FLAGS1_LOW_DAMAGE, IPANEL_SECTOR); ImGui::SameLine(column[2]);
			checkBoxMulti("High Damage##SectorFlag", &s_sectorChanges.flags1Value, &s_sectorChanges.flags1Changed, SEC_FLAGS1_HIGH_DAMAGE, IPANEL_SECTOR);

			checkBoxMulti("No Smart Obj##SectorFlag", &s_sectorChanges.flags1Value, &s_sectorChanges.flags1Changed, SEC_FLAGS1_NO_SMART_OBJ, IPANEL_SECTOR); ImGui::SameLine(column[1]);
			checkBoxMulti("Smart Obj##SectorFlag", &s_sectorChanges.flags1Value, &s_sectorChanges.flags1Changed, SEC_FLAGS1_SMART_OBJ, IPANEL_SECTOR); ImGui::SameLine(column[2]);
			checkBoxMulti("Safe Sector##SectorFlag", &s_sectorChanges.flags1Value, &s_sectorChanges.flags1Changed, SEC_FLAGS1_SAFESECTOR, IPANEL_SECTOR);

			checkBoxMulti("Mag Seal##SectorFlag", &s_sectorChanges.flags1Value, &s_sectorChanges.flags1Changed, SEC_FLAGS1_MAG_SEAL, IPANEL_SECTOR); ImGui::SameLine(column[1]);
			checkBoxMulti("Exploding Wall##SectorFlag", &s_sectorChanges.flags1Value, &s_sectorChanges.flags1Changed, SEC_FLAGS1_EXP_WALL, IPANEL_SECTOR);

			ImGui::Separator();
			applySectorChanges();
		}
	}

	Entity s_objEntity = {};
	EditorObject* s_prevObj = nullptr;

	bool hasObjectSelectionChanged(EditorSector* sector, s32 index)
	{
		return s_prevObjectFeature.sector != sector || s_prevObjectFeature.featureIndex != index;
	}

	void getPrevObjectFeature(EditorSector*& sector, s32& index)
	{
		// Make sure the previous selection is still valid.
		bool selectionInvalid = !s_prevObjectFeature.sector || s_prevObjectFeature.featureIndex < 0;
		if (s_prevObjectFeature.sector && s_prevObjectFeature.featureIndex >= (s32)s_prevObjectFeature.sector->obj.size())
		{
			selectionInvalid = true;
			s_objEntity = {};
		}

		if (selectionInvalid)
		{
			s_prevObjectFeature.sector = s_level.sectors.empty() ? nullptr : &s_level.sectors[0];
			s_prevObjectFeature.featureIndex = !s_prevObjectFeature.sector || s_prevObjectFeature.sector->obj.empty() ? -1 : 0;
		}
		sector = s_prevObjectFeature.sector;
		index = s_prevObjectFeature.featureIndex;
	}

	void setPrevObjectFeature(EditorSector* sector, s32 index)
	{
		s_prevObjectFeature.sector = sector;
		s_prevObjectFeature.featureIndex = index;
	}

	static const char* c_entityClassName[] =
	{
		"Spirit", // ETYPE_SPIRIT = 0,
		"Safe",   // ETYPE_SAFE,
		"Sprite", // ETYPE_SPRITE,
		"Frame",  // ETYPE_FRAME,
		"3D",     // ETYPE_3D,
	};

	static const char* c_objDifficulty[] =
	{
		"Easy, Medium, Hard",	//  0
		"Easy, Medium",         // -2
		"Easy",                 // -1
		"Medium, Hard",         // 2
		"Hard"                  // 3
	};
	static const s32 c_objDiffValue[] =
	{
		1, -2, -1, 2, 3
	};
	static const s32 c_objDiffStart = -3;
	static const s32 c_objDiffToIndex[] =
	{
		0, 1, 2, 0, 0, 3, 4
	};
	static s32 s_logicIndex = 0;
	static s32 s_varIndex = 0;
	static s32 s_logicSelect = -1;

	bool doesEntityExistInProject(const Entity* newEntityDef)
	{
		const s32 count = (s32)s_projEntityDefList.size();
		const Entity* entity = s_projEntityDefList.data();
		for (s32 e = 0; e < count; e++, entity++)
		{
			if (entityDefsEqual(newEntityDef, entity))
			{
				return true;
			}
		}
		return false;
	}
	
	void commitEntityChanges(EditorSector* sector, s32 objIndex, const Entity* newEntityDef)
	{
		// 1. Has the entity data actually changed? If not, then just leave it alone.
		EditorObject* obj = &sector->obj[objIndex];
		const Entity* curEntity = &s_level.entities[obj->entityId];
		if (entityDefsEqual(newEntityDef, curEntity))
		{
			return;
		}

		// 2. Search through existing entities and see if this already exists.
		const s32 count = (s32)s_level.entities.size();
		const Entity* entity = s_level.entities.data();
		s32 entityIndex = -1;
		for (s32 e = 0; e < count; e++, entity++)
		{
			if (entityDefsEqual(newEntityDef, entity))
			{
				entityIndex = e;
				break;
			}
		}
		if (entityIndex >= 0)
		{
			obj->entityId = entityIndex;
			return;
		}

		// 3. Create a new entity.
		obj->entityId = (s32)s_level.entities.size();
		s_level.entities.push_back(*newEntityDef);
		loadSingleEntityData(&s_level.entities.back());
	}

	void clearEntityChanges()
	{
		s_prevObjectFeature.sector = nullptr;
		s_prevObjectFeature.featureIndex = -1;
		s_objEntity = {};
	}

	void commitCurEntityChanges(void)
	{
		if (!s_prevObjectFeature.sector || s_prevObjectFeature.featureIndex < 0 ||
			s_prevObjectFeature.featureIndex >= (s32)s_prevObjectFeature.sector->obj.size() || s_objEntity.id < 0)
		{
			s_objEntity = {};
			return;
		}

		commitEntityChanges(s_prevObjectFeature.sector, s_prevObjectFeature.featureIndex, &s_objEntity);
		s_objEntity = {};
	}

	void addVariableToList(s32 varId, s32* varList, s32& varCount)
	{
		for (s32 v = 0; v < varCount; v++)
		{
			if (varList[v] == varId) { return; }
		}
		varList[varCount++] = varId;
	}

	void addLogicVariables(const std::string& logicName, s32* varList, s32& varCount)
	{
		const s32 id = getLogicId(logicName.c_str());
		if (id >= 0)
		{
			const LogicDef* def = &s_logicDefList[id];
			const s32 count = (s32)def->var.size();

			const LogicVar* var = def->var.data();
			for (s32 v = 0; v < count; v++, var++)
			{
				addVariableToList(var->varId, varList, varCount);
			}
		}

		// Add common variables.
		const LogicDef* commonDef = &s_logicDefList.back();
		const s32 commonCount = (s32)commonDef->var.size();
		const LogicVar* commonVar = commonDef->var.data();
		for (s32 v = 0; v < commonCount; v++, commonVar++)
		{
			addVariableToList(commonVar->varId, varList, varCount);
		}
	}

	void addAllLogicVariables(s32* varList, s32& varCount)
	{
		// Add variables for each logic.
		s32 count = (s32)s_objEntity.logic.size();
		EntityLogic* list = s_objEntity.logic.data();
		for (s32 i = 0; i < count; i++)
		{
			addLogicVariables(list[i].name, varList, varCount);
		}
		// Finally add common variables.
		const LogicDef* def = &s_logicDefList.back();
		const s32 commonCount = (s32)def->var.size();
		const LogicVar* var = def->var.data();
		for (s32 v = 0; v < commonCount; v++, var++)
		{
			addVariableToList(var->varId, varList, varCount);
		}
	}

	s32 getStringListIndex(const std::string& sValue, const std::vector<std::string>& strList)
	{
		const char* src = sValue.c_str();
		const s32 count = (s32)strList.size();
		const std::string* list = strList.data();
		for (s32 i = 0; i < count; i++, list++)
		{
			if (strcasecmp(src, list->c_str()) == 0)
			{
				return i;
			}
		}
		return -1;
	}

	static bool s_browsing = false;

	const Entity* getFirstEntityOfType(EntityType type)
	{
		const s32 count = (s32)s_entityDefList.size();
		const Entity* entity = s_entityDefList.data();
		for (s32 i = 0; i < count; i++, entity++)
		{
			if (entity->type == type)
			{
				return entity;
			}
		}
		return nullptr;
	}

	void infoPanelObject()
	{
		s32 objIndex = -1;
		EditorSector* sector = nullptr;
		selection_getEntity(selection_getCount() ? 0 : SEL_INDEX_HOVERED, sector, objIndex);
				
		if (hasObjectSelectionChanged(sector, objIndex))
		{
			commitCurEntityChanges();
		}
		setPrevObjectFeature(sector, objIndex);

		if (!sector || objIndex < 0 || objIndex >= (s32)sector->obj.size()) { return; }

		if (selection_getCount() <= 1)
		{
			EditorObject* obj = &sector->obj[objIndex];
			if (s_objEntity.id < 0)
			{
				s_objEntity = s_level.entities[obj->entityId];
			}

			if (s_browsing && !isPopupOpen())
			{
				s_browsing = false;
				const char* newAssetName = AssetBrowser::getSelectedAssetName();
				if (newAssetName)
				{
					s_objEntity.assetName = newAssetName;
				}
			}

			ImGui::Text("Sector ID: %d, Object Index: %d", sector->id, objIndex);
			ImGui::Separator();
			// Entity Name
			ImGui::Text("%s", "Name"); ImGui::SameLine(0.0f, 17.0f);
			char name[256];
			strcpy(name, s_objEntity.name.c_str());
			ImGui::SetNextItemWidth(196.0f);

			char inputName[256];
			sprintf(inputName, "##NameInput_%d_%d", sector->id, objIndex);
			if (ImGui::InputText(inputName, name, 256))
			{
				s_objEntity.name = name;
			}
			s_textInputFocused |= ImGui::IsItemActive();
			ImGui::SameLine(0.0f, 16.0f);
			// Entity Class/Type
			s32 classId = s_objEntity.type;
			ImGui::Text("%s", "Class"); ImGui::SameLine(0.0f, 8.0f);
			ImGui::SetNextItemWidth(96.0f);

			EntityType prev = s_objEntity.type;
			if (ImGui::Combo("##Class", (s32*)&classId, c_entityClassName, ETYPE_COUNT))
			{
				s_objEntity.type = EntityType(classId);
				// We need a default asset if the class type has changed, since the current asset will not work.
				if (prev != s_objEntity.type)
				{
					// So just search for the first entity in the template list of that type and use that asset.
					const Entity* defEntity = getFirstEntityOfType(s_objEntity.type);
					assert(defEntity);
					if (defEntity)
					{
						s_objEntity.assetName = defEntity->assetName;
					}
				}
			}
			// Entity Asset
			ImGui::Text("%s", "Asset"); ImGui::SameLine(0.0f, 8.0f);
			char assetName[256];
			strcpy(assetName, s_objEntity.assetName.c_str());
			if (ImGui::InputText("##AssetName", assetName, 256))
			{
				s_objEntity.assetName = assetName;
			}
			s_textInputFocused |= ImGui::IsItemActive();
			ImGui::SameLine(0.0f, 8.0f);
			if (ImGui::Button("Browse"))
			{
				AssetType assetType = TYPE_NOT_SET;
				if (s_objEntity.type == ETYPE_SPRITE)
				{
					assetType = TYPE_SPRITE;
				}
				else if (s_objEntity.type == ETYPE_FRAME)
				{
					assetType = TYPE_FRAME;
				}
				else if (s_objEntity.type == ETYPE_3D)
				{
					assetType = TYPE_3DOBJ;
				}
				if (assetType != TYPE_NOT_SET)
				{
					openEditorPopup(TFE_Editor::POPUP_BROWSE, u32(assetType), (void*)s_objEntity.assetName.c_str());
					s_browsing = true;
				}
			}
			ImGui::Separator();

			ImGui::Text("%s", "Position"); ImGui::SameLine(0.0f, 8.0f);
			ImGui::InputFloat3("##Position", &obj->pos.x);
			s_textInputFocused |= ImGui::IsItemActive();

			bool orientAdjusted = false;
			ImGui::Text("%s", "Angle"); ImGui::SameLine(0.0f, 32.0f);
			ImGui::SetNextItemWidth(206.0f);
			if (ImGui::SliderAngle("##Angle", &obj->angle, 0.0f))
			{
				orientAdjusted = true;
			}
			ImGui::SameLine(0.0f, 4.0f);
			ImGui::SetNextItemWidth(96.0f);

			f32 angle = obj->angle * 180.0f / PI;
			if (ImGui::InputFloat("##AngleEdit", &angle))
			{
				obj->angle = angle * PI / 180.0f;
				orientAdjusted = true;
			}
			s_textInputFocused |= ImGui::IsItemActive();

			// Show pitch and roll.
			if (s_objEntity.type == ETYPE_3D)
			{
				f32 pitch = obj->pitch * 180.0f / PI;
				f32 roll = obj->roll  * 180.0f / PI;

				ImGui::Text("%s", "Pitch"); ImGui::SameLine(0.0f, 32.0f);
				ImGui::SetNextItemWidth(128.0f);
				if (ImGui::InputFloat("##PitchEdit", &pitch))
				{
					obj->pitch = pitch * PI / 180.0f;
					orientAdjusted = true;
				}
				s_textInputFocused |= ImGui::IsItemActive();
				ImGui::SameLine(0.0f, 32.0f);
				ImGui::Text("%s", "Roll"); ImGui::SameLine(0.0f, 8.0f);
				ImGui::SetNextItemWidth(128.0f);
				if (ImGui::InputFloat("##RollEdit", &roll))
				{
					obj->roll = roll * PI / 180.0f;
					orientAdjusted = true;
				}
				s_textInputFocused |= ImGui::IsItemActive();

				if (orientAdjusted)
				{
					compute3x3Rotation(&obj->transform, obj->angle, obj->pitch, obj->roll);
				}
			}

			// Difficulty.
			ImGui::Text("%s", "Difficulty"); ImGui::SameLine(0.0f, 8.0f);
			s32 index = c_objDiffToIndex[obj->diff - c_objDiffStart];
			ImGui::SetNextItemWidth(256.0f);
			if (ImGui::Combo("##DiffCombo", (s32*)&index, c_objDifficulty, TFE_ARRAYSIZE(c_objDifficulty)))
			{
				obj->diff = c_objDiffValue[index];
			}
			ImGui::Separator();

			const s32 listWidth = (s32)s_infoWith - 32;

			// Logics
			if (s_logicSelect >= (s32)s_objEntity.logic.size())
			{
				s_logicSelect = -1;
			}
			// Always select one of the them, if there are any to select.
			if (s_logicSelect < 0 && !s_objEntity.logic.empty())
			{
				s_logicSelect = 0;
			}

			ImGui::Text("%s", "Logics");
			ImGui::BeginChild("##LogicList", { (f32)min(listWidth, 400), 64 }, ImGuiChildFlags_Border);
			{
				s32 count = (s32)s_objEntity.logic.size();
				EntityLogic* list = s_objEntity.logic.data();
				for (s32 i = 0; i < count; i++)
				{
					char name[256];
					sprintf(name, "%s##%d", list[i].name.c_str(), i);
					bool sel = s_logicSelect == i;
					ImGui::Selectable(name, &sel);
					if (sel) { s_logicSelect = i; }
					else if (s_logicSelect == i) { s_logicSelect = -1; }
				}
			}
			ImGui::EndChild();
			if (ImGui::Button("Add"))
			{
				LogicDef* def = &s_logicDefList[s_logicIndex];
				EntityLogic newLogic = {};
				newLogic.name = def->name;
				s_objEntity.logic.push_back(newLogic);

				EntityLogic* logic = &s_objEntity.logic.back();

				// Add Variables automatically.
				const s32 varCount = (s32)def->var.size();
				const LogicVar* var = def->var.data();
				for (s32 v = 0; v < varCount; v++, var++)
				{
					const EntityVarDef* varDef = &s_varDefList[var->varId];
					if (var->type == VAR_TYPE_DEFAULT || var->type == VAR_TYPE_REQUIRED)
					{
						EntityVar newVar;
						newVar.defId = var->varId;
						newVar.value = varDef->defValue;
						logic->var.push_back(newVar);
					}
				}
			}

			ImGui::SameLine(0.0f, 8.0f);
			ImGui::SetNextItemWidth(128.0f);
			if (ImGui::BeginCombo("##LogicCombo", s_logicDefList[s_logicIndex].name.c_str()))
			{
				s32 count = (s32)s_logicDefList.size() - 1;
				for (s32 i = 0; i < count; i++)
				{
					if (ImGui::Selectable(s_logicDefList[i].name.c_str(), i == s_logicIndex))
					{
						s_logicIndex = i;
					}
					setTooltip(s_logicDefList[i].tooltip.c_str());
				}
				ImGui::EndCombo();
			}

			ImGui::SameLine(0.0f, 16.0f);
			if (ImGui::Button("Remove") && s_logicSelect >= 0)
			{
				s32 count = (s32)s_objEntity.logic.size();
				for (s32 i = s_logicSelect; i < count - 1; i++)
				{
					s_objEntity.logic[i] = s_objEntity.logic[i + 1];
				}
				s_objEntity.logic.pop_back();
				s_logicSelect = -1;
			}

			ImGui::Separator();

			// Variables
			static s32 varSel = -1;

			// Select the variable list.
			std::vector<EntityVar>* varList = &s_objEntity.var;
			if (s_logicSelect >= 0 && s_logicSelect < (s32)s_objEntity.logic.size())
			{
				varList = &s_objEntity.logic[s_logicSelect].var;
			}

			const f32 varListHeight = s_objEntity.type == ETYPE_3D ? 114.0f : 140.0f;
			ImGui::Text("%s", "Variables");
			ImGui::BeginChild("##VariableList", { (f32)min(listWidth, 400), varListHeight }, ImGuiChildFlags_Border);
			{
				s32 count = (s32)varList->size();
				EntityVar* list = varList->data();
				for (s32 i = 0; i < count; i++)
				{
					EntityVarDef* def = getEntityVar(list[i].defId);

					char name[256];
					sprintf(name, "%s##%d", def->name.c_str(), i);
					bool sel = varSel == i;
					ImGui::Selectable(name, &sel, 0, { 100.0f,0.0f });
					if (sel) { varSel = i; }
					else if (varSel == i) { varSel = -1; }

					if (def->type != EVARTYPE_FLAGS) { ImGui::SameLine(0.0f, 8.0f); }
					switch (def->type)
					{
					case EVARTYPE_BOOL:
					case EVARTYPE_BOOL2:
					{
						sprintf(name, "##VarBool%d", i);
						ImGui::Checkbox(name, &list[i].value.bValue);
					} break;
					case EVARTYPE_FLOAT:
					{
						sprintf(name, "##VarFloat%d", i);
						ImGui::InputFloat(name, &list[i].value.fValue);
						s_textInputFocused |= ImGui::IsItemActive();
					} break;
					case EVARTYPE_INT:
					{
						sprintf(name, "##VarInt%d", i);
						ImGui::InputInt(name, &list[i].value.iValue);
						s_textInputFocused |= ImGui::IsItemActive();
					} break;
					case EVARTYPE_FLAGS:
					{
						const s32 flagCount = (s32)def->flags.size();
						const EntityVarFlag* flag = def->flags.data();
						for (s32 f = 0; f < flagCount; f++, flag++)
						{
							ImGui::CheckboxFlags(flag->name.c_str(), (u32*)&list[i].value.iValue, (u32)flag->value);
							if (f < flagCount - 1) { ImGui::SameLine(0.0f, 12.0f); }
						}
					} break;
					case EVARTYPE_STRING_LIST:
					{
						const s32 index = getStringListIndex(list[i].value.sValue, def->strList);
						sprintf(name, "##VarCombo%d", i);
						if (ImGui::BeginCombo(name, list[i].value.sValue.c_str()))
						{
							const s32 listCount = (s32)def->strList.size();
							for (s32 s = 0; s < listCount; s++)
							{
								if (ImGui::Selectable(def->strList[s].c_str(), s == index))
								{
									list[i].value.sValue = def->strList[s];
								}
							}
							ImGui::EndCombo();
						}
					} break;
					case EVARTYPE_INPUT_STRING_PAIR:
					{
						char pair1[256], pair2[256];
						strcpy(pair1, list[i].value.sValue.c_str());
						strcpy(pair2, list[i].value.sValue1.c_str());
						ImGui::SetNextItemWidth(128.0f);
						sprintf(name, "##Pair1_%d", i);
						if (ImGui::InputText(name, pair1, 256))
						{
							list[i].value.sValue = pair1;
						}
						s_textInputFocused |= ImGui::IsItemActive();
						ImGui::SameLine(0.0f, 8.0f);
						ImGui::SetNextItemWidth(128.0f);
						sprintf(name, "##Pair2_%d", i);
						if (ImGui::InputText(name, pair2, 256))
						{
							list[i].value.sValue1 = pair2;
						}
						s_textInputFocused |= ImGui::IsItemActive();
					} break;
					case EVARTYPE_INPUT_STRING:
					{
						char str[256];
						strcpy(str, list[i].value.sValue.c_str());
						ImGui::SetNextItemWidth(192.0f);
						sprintf(name, "##VarStr%d", i);
						if (ImGui::InputText(name, str, 256))
						{
							list[i].value.sValue = str;
						}
						s_textInputFocused |= ImGui::IsItemActive();
					}
					}
				}
			}
			ImGui::EndChild();

			s32 varComboList[256];
			s32 varComboCount = 0;
			if (s_logicSelect >= 0 && s_logicSelect < (s32)s_objEntity.logic.size())
			{
				addLogicVariables(s_objEntity.logic[s_logicSelect].name, varComboList, varComboCount);
			}
			else
			{
				addLogicVariables("", varComboList, varComboCount);
			}

			if (ImGui::Button("Add##Variable") && s_varIndex >= 0 && s_varIndex < varComboCount)
			{
				EntityVar var;
				var.defId = varComboList[s_varIndex];
				var.value = s_varDefList[var.defId].defValue;
				varList->push_back(var);
			}
			ImGui::SameLine(0.0f, 8.0f);
			ImGui::SetNextItemWidth(128.0f);

			if (s_varIndex < 0 || s_varIndex >= varComboCount) { s_varIndex = 0; }

			if (ImGui::BeginCombo("##VarCombo", varComboCount ? s_varDefList[varComboList[s_varIndex]].name.c_str() : ""))
			{
				for (s32 i = 0; i < varComboCount; i++)
				{
					if (ImGui::Selectable(s_varDefList[varComboList[i]].name.c_str(), i == s_varIndex))
					{
						s_varIndex = i;
					}
					// setTooltip(s_varDefList[varList[s_varIndex]].tooltip.c_str());
				}
				ImGui::EndCombo();
			}

			ImGui::SameLine(0.0f, 16.0f);
			if (ImGui::Button("Remove##Variable") && varSel >= 0 && varSel < (s32)varList->size())
			{
				s32 count = (s32)varList->size();
				for (s32 i = varSel; i < count - 1; i++)
				{
					(*varList)[i] = (*varList)[i + 1];
				}
				varList->pop_back();
				varSel = -1;
			}
			ImGui::Separator();

			// Buttons.
			if (ImGui::Button("Category"))
			{
				openEditorPopup(TFE_Editor::POPUP_CATEGORY);
				s_prevCategoryFlags = s_objEntity.categories;
			}
			ImGui::SameLine(0.0f, 8.0f);
			if (ImGui::Button("Add to Entity Def"))
			{
				s_objEntity.categories |= 1;	// custom.
				if (!doesEntityExistInProject(&s_objEntity))
				{
					s_projEntityDefList.push_back(s_objEntity);
					s_entityDefList.push_back(s_objEntity);
					saveProjectEntityList();
				}
			}
			ImGui::SameLine(0.0f, 8.0f);
			if (ImGui::Button("Commit"))
			{
				commitCurEntityChanges();
			}
			ImGui::SameLine(0.0f, 8.0f);
			if (ImGui::Button("Reset"))
			{
				s_objEntity = {};
			}
		}
		else  // select count > 1
		{
			ImGui::Text("Multiple objects selected.");
			ImGui::Separator();
			ImGui::Text("Support for limited multi-select editing");
			ImGui::Text("  coming in version Alpha-2 or later.");
		}
	}

	Vec4f packedColorToVec4(u32 color)
	{
		const f32 scale = 1.0f / 255.0f;
		const f32 a = f32((color >> 24u) & 0xffu) * scale;
		const f32 b = f32((color >> 16u) & 0xffu) * scale;
		const f32 g = f32((color >>  8u) & 0xffu) * scale;
		const f32 r = f32((color       ) & 0xffu) * scale;

		return { r, g, b, a };
	}

	u32 colorVec4ToPacked(Vec4f color)
	{
		u32 a = u32(color.w * 255.0f);
		u32 r = u32(color.x * 255.0f);
		u32 g = u32(color.y * 255.0f);
		u32 b = u32(color.z * 255.0f);

		return r | (g << 8u) | (b << 16u) | (a << 24u);
	}

	bool optionFloatInput(const char* label, f32* value, f32 step, s32 colWidth = 0, s32 width = 0, const char* prec = "%.3f");

	bool optionFloatInput(const char* label, f32* value, f32 step, s32 colWidth, s32 width, const char* prec)
	{
		const f32 textWidth = (colWidth == 0) ? ImGui::CalcTextSize(label).x + 8.0f : f32(colWidth);

		ImGui::SetNextItemWidth(textWidth);
		ImGui::LabelText("##Label", "%s", label); ImGui::SameLine();
		if (width) { ImGui::SetNextItemWidth((f32)width); }
		bool changed = ImGui::InputFloat(editor_getUniqueLabel(""), value, step, 10.0f * step, prec);
		s_textInputFocused |= ImGui::IsItemActive();
		return changed;
	}

	static s32 s_selectedOffset = -1;
	static s32 s_prevGuidelineId = -1;
	
	void infoPanelGuideline()
	{
		const s32 id = s_curGuideline >= 0 ? s_curGuideline : s_hoveredGuideline;
		if (id < 0) { return; }

		Guideline* guideline = &s_level.guidelines[id];
		bool guidelineEdited = false;

		sectionHeader("Settings");
		const s32 checkWidth = (s32)ImGui::CalcTextSize("Limit Height Shown").x + 8;
		const u32 prevFlags = guideline->flags;
		optionCheckbox("Limit Height Shown", &guideline->flags, GLFLAG_LIMIT_HEIGHT, checkWidth);
		optionCheckbox("Disable Snapping", &guideline->flags, GLFLAG_DISABLE_SNAPPING, checkWidth);
		if (guideline->flags != prevFlags) { guidelineEdited = true; }
		ImGui::Separator();

		if ((guideline->flags & GLFLAG_LIMIT_HEIGHT) || !(guideline->flags & GLFLAG_DISABLE_SNAPPING))
		{
			sectionHeader("Values");
			s32 colWidth = s32(ImGui::CalcTextSize("Min Height Shown").x + 8.0f);
			if (guideline->flags & GLFLAG_LIMIT_HEIGHT)
			{
				guidelineEdited |= optionFloatInput("Min Height Shown", &guideline->minHeight, 0.1f, colWidth, 128, "%.2f");
				guidelineEdited |= optionFloatInput("Max Height Shown", &guideline->maxHeight, 0.1f, colWidth, 128, "%.2f");
			}
			if (!(guideline->flags & GLFLAG_DISABLE_SNAPPING))
			{
				guidelineEdited |= optionFloatInput("Max Snap Range", &guideline->maxSnapRange, 0.1f, colWidth, 128, "%.2f");
			}
			ImGui::Separator();
		}

		sectionHeader("Offsets");
		s32 itemToRemove = -1;
		s32 count = (s32)guideline->offsets.size();
		f32* offsetValue = guideline->offsets.data();
		guideline->maxOffset = 0.0f;
		for (s32 i = 0; i < count; i++)
		{
			char label[256];
			sprintf(label, "Offset %d", i + 1);
			guidelineEdited |= optionFloatInput(label, &offsetValue[i], 0.1f, 0, 128, "%.2f");
			guideline->maxOffset = std::max(guideline->maxOffset, fabsf(offsetValue[i]));

			ImGui::SameLine();
			if (ImGui::Button(editor_getUniqueLabel("Remove")))
			{
				itemToRemove = i;
			}
		}
		guideline_computeBounds(guideline);
		guideline_computeKnots(guideline);
		ImGui::Separator();
		if (count >= 4) { disableNextItem(); }
		if (ImGui::Button("Add"))
		{
			guideline->offsets.push_back({ 0.0f });
			guidelineEdited = true;
		}
		if (count >= 4) { enableNextItem(); }

		if (itemToRemove >= 0 && itemToRemove < count)
		{
			for (s32 i = itemToRemove; i < count - 1; i++)
			{
				guideline->offsets[i] = guideline->offsets[i + 1];
			}
			guideline->offsets.pop_back();
			guidelineEdited = true;
		}

		ImGui::Separator();
		sectionHeader("Subdivision");
		ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.75f, 0.5f), "Subdivide into equal length segments,\ndefault (0) = no subdivision.");
		s32 colWidth = s32(ImGui::CalcTextSize("Segment Length").x + 8.0f);
		if (optionFloatInput("Segment Length", &guideline->subDivLen, 0.1f, colWidth, 128, "%.2f"))
		{
			guidelineEdited = true;
			guideline_computeSubdivision(guideline);
		}
		
		if (guidelineEdited)
		{
			cmd_guidelineSingleSnapshot(LName_Guideline_Edit, id, s_prevGuidelineId != id);
		}
		s_prevGuidelineId = id;
	}

	void infoPanelNote()
	{
		s32 id = s_curLevelNote >= 0 ? s_curLevelNote : s_hoveredLevelNote;
		if (id < 0) { return; }
		
		LevelNote* note = &s_level.notes[id];
		bool changed = false;
		
		changed |= ImGui::CheckboxFlags("2D Only", &note->flags, LNF_2D_ONLY); ImGui::SameLine();
		changed |= ImGui::CheckboxFlags("No Fade in 3D", &note->flags, LNF_3D_NO_FADE); ImGui::SameLine();
		changed |= ImGui::CheckboxFlags("Always Show Text", &note->flags, LNF_TEXT_ALWAYS_SHOW);
		ImGui::Spacing();
		ImGui::SetNextItemWidth(128.0f);
		ImGui::LabelText("##Label", "Start Fade (3D)"); ImGui::SameLine();
		ImGui::SetNextItemWidth(128.0f);
		changed |= ImGui::InputFloat("##StartFade", &note->fade.x, 1.0f, 10.0f, "%.1f");
		s_textInputFocused |= ImGui::IsItemActive();
		ImGui::SetNextItemWidth(128.0f);
		ImGui::LabelText("##Label", "End Fade (3D)"); ImGui::SameLine();
		ImGui::SetNextItemWidth(128.0f);
		changed |= ImGui::InputFloat("##EndFade", &note->fade.z, 1.0f, 10.0f, "%.1f");
		s_textInputFocused |= ImGui::IsItemActive();
		ImGui::Separator();
		ImGui::SetNextItemWidth(128.0f);
		ImGui::LabelText("##Label", "Icon Color"); ImGui::SameLine();
		Vec4f iconColor = packedColorToVec4(note->iconColor);
		if (ImGui::ColorEdit4(editor_getUniqueLabel(""), iconColor.m, ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoInputs))
		{
			changed = true;
			note->iconColor = colorVec4ToPacked(iconColor);
		}
		ImGui::SetNextItemWidth(128.0f);
		ImGui::LabelText("##Label", "Text Color"); ImGui::SameLine();
		Vec4f textColor = packedColorToVec4(note->textColor);
		if (ImGui::ColorEdit4(editor_getUniqueLabel(""), textColor.m, ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoInputs))
		{
			changed = true;
			note->textColor = colorVec4ToPacked(textColor);
		}
		ImGui::Separator();
		char textInputId[256];
		char tmpBuffer[4096];
		// Key the Input by ID since ImGUI keeps its own copy internally, which can cause issues when changing IDs.
		sprintf(textInputId, "##TextBox%d", id);
		strcpy(tmpBuffer, note->note.c_str());

		if (ImGui::InputTextMultiline(textInputId, tmpBuffer, 4096, { s_infoWith - 16.0f, 354.0f }))
		{
			changed = true;
			note->note = tmpBuffer;
		}
		s_textInputFocused |= ImGui::IsItemActive();

		if (changed)
		{
			if (s_prevLevelNote == s_curLevelNote)
			{
				cmd_levelNoteSnapshot(LName_LevelNote_Change, false);
			}
			else
			{
				cmd_levelNoteSnapshot(LName_LevelNote_Change, true);
			}
			s_prevLevelNote = s_curLevelNote;
		}
	}
		
	bool drawInfoPanel(EditorView view)
	{
		bool show = getSelectMode() == SELECTMODE_NONE;
		s_textInputFocused = false;

		// Draw the info bars.
		s_infoHeight = 486 + 44;
		infoToolBegin(s_infoHeight);
		{
			if (s_infoTab == TAB_ITEM && show)
			{
				if (s_editMode == LEDIT_VERTEX)
				{
					infoPanelVertex();
				}
				else if (s_editMode == LEDIT_WALL || s_editMode == LEDIT_SECTOR)
				{
					const bool is2d = view == EDIT_VIEW_2D;
					const bool showWall = s_editMode == LEDIT_WALL && (is2d || s_wallShownLast);

					// Prioritize selected wall, selected sector, hovered wall, hovered sector.
					// Allow sector views in wall mode, but NOT wall views in sector mode.
					EditorSector* curSector = nullptr;
					EditorSector* hoveredSector = nullptr;
					s32 curFeatureIndex = -1, hoveredFeatureIndex = -1;
					HitPart curPart = HP_NONE, hoveredPart = HP_NONE;
					if (s_editMode == LEDIT_SECTOR)
					{
						selection_getSector(SEL_INDEX_HOVERED, hoveredSector);
						selection_getSector(0, curSector);
					}
					else // LEDIT_WALL
					{
						selection_getSurface(SEL_INDEX_HOVERED, hoveredSector, hoveredFeatureIndex, &hoveredPart);
						selection_getSurface(0, curSector, curFeatureIndex, &curPart);
					}

					const bool curFeatureIsFlat   = s_editMode == LEDIT_SECTOR || (curPart == HP_FLOOR || curPart == HP_CEIL);
					const bool hoverFeatureIsFlat = s_editMode == LEDIT_SECTOR || (hoveredPart == HP_FLOOR || hoveredPart == HP_CEIL);

					if (s_editMode == LEDIT_WALL && curFeatureIndex >= 0 && !curFeatureIsFlat) { infoPanelWall(); }
					else if (curSector) { infoPanelSector(); }
					else if (s_editMode == LEDIT_WALL && hoveredFeatureIndex >= 0 && !hoverFeatureIsFlat) { infoPanelWall(); }
					else if (hoveredSector) { infoPanelSector(); }
					else if (showWall) { infoPanelWall(); }
					else { infoPanelSector(); }
				}
				else if (s_editMode == LEDIT_ENTITY)
				{
					infoPanelObject();
				}
				else if (s_editMode == LEDIT_NOTES)
				{
					infoPanelNote();
				}
				else if (s_editMode == LEDIT_GUIDELINES)
				{
					infoPanelGuideline();
				}
			}
			else if (s_infoTab == TAB_INFO && show)
			{
				infoPanelMap();
			}
		}
		infoToolEnd();
		return s_textInputFocused;
	}
		
	void infoToolEnd()
	{
		ImGui::End();
	}
		
	bool categoryPopupUI()
	{
		f32 winWidth = 600.0f;
		f32 winHeight = 146.0f;
		bool btn = false;
		pushFont(TFE_Editor::FONT_SMALL);

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoResize;
		ImGui::SetNextWindowSize({ winWidth, winHeight });
		if (ImGui::BeginPopupModal("Category", nullptr, window_flags))
		{
			//s_objEntity;
			s32 count = (s32)s_categoryList.size();
			const Category* cat = s_categoryList.data() + 1;
			for (s32 i = 1; i < count; i++, cat++)
			{
				const s32 flag = 1 << i;
				ImGui::CheckboxFlags(cat->name.c_str(), (u32*)&s_objEntity.categories, (u32)flag);

				s32 rowIdx = (i & 3);
				if (rowIdx != 0 && i < count - 1)
				{
					ImGui::SameLine(150.0f * rowIdx, 0.0f);
				}
			}

			ImGui::Separator();

			if (ImGui::Button("Confirm"))
			{
				btn = true;
			}
			ImGui::SameLine(0.0f, 32.0f);
			if (ImGui::Button("Cancel"))
			{
				btn = true;
				s_objEntity.categories = s_prevCategoryFlags;
			}
			ImGui::EndPopup();
		}
		popFont();

		return btn;
	}
}
