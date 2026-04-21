#include "assetBrowser.h"
#include <TFE_Editor/errorMessages.h>
#include <TFE_Editor/editorConfig.h>
#include <TFE_Editor/editorLevel.h>
#include <TFE_Editor/editorProject.h>
#include <TFE_Editor/editorResources.h>
#include <TFE_Editor/editor.h>
#include <TFE_Editor/EditorAsset/editor3dThumbnails.h>
#include <TFE_Editor/EditorAsset/editorAsset.h>
#include <TFE_Editor/EditorAsset/editorTexture.h>
#include <TFE_Editor/EditorAsset/editorFrame.h>
#include <TFE_Editor/EditorAsset/editorSprite.h>
#include <TFE_Asset/imageAsset.h>
#include <TFE_DarkForces/mission.h>
#include <TFE_Input/input.h>
#include <TFE_FrontEndUI/frontEndUi.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_System/system.h>
#include <TFE_Settings/settings.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_Archive/archive.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_System/parser.h>
#include <TFE_Ui/ui.h>

#include <algorithm>
#include <vector>
#include <string>
#include <map>

using namespace TFE_Editor;

namespace AssetBrowser
{
	enum UIConst : u32
	{
		INFO_PANEL_WIDTH = 524u,
	};
	const char* c_games[] =
	{
		"Dark Forces",
		"Outlaws",
	};

	typedef std::vector<s32> SelectionList;

	struct Palette
	{
		std::string name;
		u32 data[256];
		// Colormap
		bool hasColormap;
		u8 colormap[256 * 32];
	};

	struct LevelAssets
	{
		std::string levelName;
		std::string paletteName;
		std::vector<std::string> textures;
		std::vector<std::string> frames;
		std::vector<std::string> sprites;
		std::vector<std::string> pods;
	};

	struct ViewerInfo
	{
		std::string exportPath;

		GameID game = Game_Dark_Forces;
		GameID prevGame = Game_Count;
		AssetType type = TYPE_TEXTURE;
		AssetType prevType = TYPE_NOT_SET;

		char assetFilter[64] = "";
		char prevAssetFilter[64] = "";
		s32 levelSource = -1;
		u32 filterLen = 0;
		bool showOnlyModLevels = true;
	};

	std::vector<LevelAssets> s_levelAssets;

	static bool s_reloadProjectAssets = true;
	static bool s_assetsNeedProcess = true;
	static std::vector<Palette> s_palettes;
	static std::map<std::string, s32> s_assetPalette;
	static std::map<std::string, u32> s_assetLevels;
	static s32 s_defaultPal = 0;

	static s32 s_hovered = -1;
	static s32 s_menuHeight = 20;
	static s32 s_selectedPalette = 0;

	static SelectionList s_selected;
	static s32 s_selectRange[2];
	   	
	static ViewerInfo s_viewInfo = {};
	static AssetList s_viewAssetList;
	static AssetList s_projectAssetList[TYPE_COUNT];

	// Forward Declarations
	void updateAssetList();
	void reloadAsset(Asset* asset, s32 palId, s32 lightLevel = 32);
	bool isSelected(s32 index);
	void unselect(s32 index);
	void select(s32 index);
	void exportSelected();
	void importSelected();
	s32 getAssetPalette(const char* name);
	void drawAssetList(s32 w, s32 h);

	void init()
	{
		updateAssetList();
	}

	void destroy()
	{
		s_reloadProjectAssets = true;
		s_assetsNeedProcess = true;
		s_palettes.clear();
		s_assetPalette.clear();
		s_assetLevels.clear();
		s_defaultPal = 0;

		s_viewInfo = ViewerInfo{};
		s_viewAssetList.clear();
		for (s32 i = 0; i < TYPE_COUNT; i++)
		{
			s_projectAssetList[i].clear();
		}
		freeAllAssetData();
	}

	void label(const char* label)
	{
		ImGui::PushFont(TFE_FrontEndUI::getDialogFont());
		ImGui::LabelText("##Label", "%s", label);
		ImGui::PopFont();
	}

	bool paletteItemGetter(void* data, int idx, const char** out_text)
	{
		Palette* palette = (Palette*)data;
		*out_text = palette[idx].name.c_str();
		return true;
	}

	bool levelTextureGetter(void* data, int idx, const char** out_text)
	{
		static const char* allLevels = "Any";
		if (idx == 0)
		{
			*out_text = allLevels;
		}
		else
		{
			LevelAssets* textures = (LevelAssets*)data;
			*out_text = textures[idx - 1].levelName.c_str();
		}
		return true;
	}

	void listSelectionPalette(const char* labelText, std::vector<Palette>& listValues, s32* index)
	{
		ImGui::SetNextItemWidth(UI_SCALE(256));
		ImGui::LabelText("##Label", "%s", labelText); ImGui::SameLine(UI_SCALE(96));

		char comboId[256];
		sprintf(comboId, "##%s", labelText);
		ImGui::Combo(comboId, index, paletteItemGetter, listValues.data(), (s32)listValues.size());
	}

	void listSelectionLevel(const char* labelText, std::vector<LevelAssets>& listValues, s32* index)
	{
		ImGui::SetNextItemWidth(UI_SCALE(256));
		ImGui::LabelText("##Label", "%s", labelText); ImGui::SameLine(UI_SCALE(96));

		char comboId[256];
		sprintf(comboId, "##%s", labelText);
		s32 lvlIndex = (*index) + 1;
		if (ImGui::Combo(comboId, &lvlIndex, levelTextureGetter, listValues.data(), (s32)listValues.size()))
		{
			*index = lvlIndex - 1;
		}
	}
		
	void infoPanel(u32 infoWidth, u32 infoHeight)
	{
		DisplayInfo displayInfo;
		TFE_RenderBackend::getDisplayInfo(&displayInfo);
		u32 w = infoWidth;
		u32 h = infoHeight;

		Project* project = project_get();
		bool projActive = project->active;
		if (projActive && s_viewInfo.type == TYPE_LEVEL)
		{
			h += (u32)UI_SCALE(40);
		}

		ImGui::SetWindowPos("Asset Browser##Settings", { 0.0f, f32(s_menuHeight) });
		ImGui::SetWindowSize("Asset Browser##Settings", { f32(w), f32(h) });
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

		bool active = true;
		s32 levelSource = s_viewInfo.levelSource;
		ImGui::Begin("Asset Browser##Settings", &active, window_flags);
		LIST_SELECT("Game", c_games, s_viewInfo.game);
		LIST_SELECT("Asset Type", c_assetType, s_viewInfo.type);
		listSelectionLevel("Level", s_levelAssets, &levelSource);
		ImGui::Separator();

		bool listChanged = false;
		if (s_viewInfo.game != s_viewInfo.prevGame || s_viewInfo.type != s_viewInfo.prevType || levelSource != s_viewInfo.levelSource)
		{ 
			listChanged = true;
			s_viewInfo.levelSource = levelSource;
			s_selected.clear();
			s_hovered = -1;
			s_selectRange[0] = -1;
			s_selectRange[1] = -1;
		}
		s_viewInfo.prevGame = s_viewInfo.game;
		s_viewInfo.prevType = s_viewInfo.type;

		// Filter
		ImGui::LabelText("##Label", "Filter"); ImGui::SameLine(UI_SCALE(96));
		ImGui::SetNextItemWidth((f32)UI_SCALE(256));
		ImGui::InputText("##FilterText", s_viewInfo.assetFilter, 64);
		ImGui::SameLine(UI_SCALE(358));
		if (ImGui::Button("CLEAR"))
		{
			s_viewInfo.assetFilter[0] = 0;
			s_viewInfo.filterLen = 0;
		}
		else
		{
			s_viewInfo.filterLen = (u32)strlen(s_viewInfo.assetFilter);
		}
		if (s_viewInfo.prevAssetFilter[0] != s_viewInfo.assetFilter[0] || strlen(s_viewInfo.prevAssetFilter) != s_viewInfo.filterLen ||
			strcasecmp(s_viewInfo.prevAssetFilter, s_viewInfo.assetFilter))
		{
			strcpy(s_viewInfo.prevAssetFilter, s_viewInfo.assetFilter);
			listChanged = true;
		}

		// Add new level.
		if (projActive && s_viewInfo.type == TYPE_LEVEL)
		{
			ImGui::Separator();
			if (ImGui::Button("Create New Level"))
			{
				openEditorPopup(POPUP_NEW_LEVEL);
				level_prepareNew();
			}
			ImGui::SameLine(0.0f, UI_SCALE(64));
			if (ImGui::Checkbox("Show Only Mod Levels", &s_viewInfo.showOnlyModLevels))
			{
				listChanged = true;
			}
		}

		ImGui::End();

		if (listChanged)
		{
			updateAssetList();
		}
	}

	void drawInfoPanel(Asset* asset, u32 infoWidth, u32 infoHeight, bool multiselect)
	{
		DisplayInfo displayInfo;
		TFE_RenderBackend::getDisplayInfo(&displayInfo);

		u32 w = std::min(infoWidth,  displayInfo.width);
		u32 h = std::max(256u, displayInfo.height - (infoHeight + s_menuHeight));

		Project* project = project_get();
		bool projActive = project->active;
		if (projActive && s_viewInfo.type == TYPE_LEVEL)
		{
			infoHeight += (u32)UI_SCALE(40);
		}

		ImGui::SetWindowPos("Asset Info", { 0.0f, f32(infoHeight + s_menuHeight) });
		ImGui::SetWindowSize("Asset Info", { f32(w), f32(h) });
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

		bool active = true;
		ImGui::Begin("Asset Info", &active, window_flags);
		if (multiselect)
		{
			AssetType type = s_viewAssetList[s_selected[0]].type;
			ImGui::LabelText("##SelectedCount", "Selected Count: %d", (s32)s_selected.size());
			ImGui::LabelText("##Type", "Type: %s", c_assetType[type]);

			if (type == TYPE_TEXTURE || type == TYPE_SPRITE || type == TYPE_FRAME)
			{
				listSelectionPalette("Palette", s_palettes, &s_selectedPalette);
				if (ImGui::Button("Force Palette"))
				{
					const size_t count = s_selected.size();
					for (size_t i = 0; i < count; i++)
					{
						Asset* asset = &s_viewAssetList[s_selected[i]];
						reloadAsset(asset, s_selectedPalette, 32);
					}
				}
				ImGui::SameLine();
				if (ImGui::Button("Reset To Default"))
				{
					const size_t count = s_selected.size();
					for (size_t i = 0; i < count; i++)
					{
						Asset* asset = &s_viewAssetList[s_selected[i]];
						s32 palId = getAssetPalette(asset->name.c_str());
						reloadAsset(asset, palId, 32);
					}
				}
				ImGui::Separator();
			}

			// Import
			if (type == TYPE_LEVEL)
			{
				Project* project = project_get();
				bool canImport = project && project->active && s_viewAssetList[s_selected[0]].assetSource != ASRC_PROJECT;
				if (!canImport) { disableNextItem(); }
				if (ImGui::Button("Import"))
				{
					importSelected();
				}
				if (!canImport) { enableNextItem(); }
				ImGui::SameLine();
			}

			if (ImGui::Button("Export"))
			{
				exportSelected();
			}
			ImGui::SameLine();
			// Only one asset can be edited at a time, so just pick the first one.
			if (ImGui::Button("Editor"))
			{
				enableAssetEditor(&s_viewAssetList[s_selected[0]]);
			}
		}
		else if (asset)
		{
			ImGui::LabelText("##Name", "Name: %s", asset->name.c_str());
			ImGui::LabelText("##Type", "Type: %s", c_assetType[asset->type]);

			if (!s_selected.empty())
			{
				ImGui::Separator();

				// Import
				if (asset->type == TYPE_LEVEL)
				{
					Project* project = project_get();
					bool canImport = project && project->active && asset->assetSource != ASRC_PROJECT;
					if (!canImport) { disableNextItem(); }
					if (ImGui::Button("Import"))
					{
						importSelected();
					}
					if (!canImport) { enableNextItem(); }
					ImGui::SameLine();
				}

				if (ImGui::Button("Export"))
				{
					exportSelected();
				}
				ImGui::SameLine();
				if (ImGui::Button("Editor"))
				{
					enableAssetEditor(asset);
				}
				ImGui::Separator();
			}

			if (asset->type == TYPE_TEXTURE || asset->type == TYPE_EXT_TEXTURE)
			{
				EditorTexture* tex = (EditorTexture*)getAssetData(asset->handle);

				ImGui::LabelText("##Size", "Size: %d x %d", tex->width, tex->height);

				bool reloadRequired = false;
				if (asset->type == TYPE_TEXTURE)
				{
					s32 newPaletteIndex = tex->paletteIndex;
					listSelectionPalette("Palette", s_palettes, &newPaletteIndex);
					if (newPaletteIndex != tex->paletteIndex)
					{
						reloadRequired = true;
					}

					s32 newLightLevel = tex->lightLevel;
					if (s_palettes[newPaletteIndex].hasColormap)
					{
						ImGui::SliderInt("Light Level", &newLightLevel, 0, 32);
					}
					else
					{
						newLightLevel = 32;
					}
					if (newLightLevel != tex->lightLevel)
					{
						reloadRequired = true;
					}

					if (reloadRequired)
					{
						// Make sure the frame isn't reset when reloading the texture.
						s32 curFrame = tex->curFrame;
						reloadAsset(asset, newPaletteIndex, newLightLevel);
						tex = (EditorTexture*)getAssetData(asset->handle);
						tex->curFrame = curFrame;
					}
				}

				ImGui::LabelText("##Frames", "Frame Count: %d", tex->frameCount);
				if (tex->frameCount > 1)
				{
					ImGui::SliderInt("Frame", &tex->curFrame, 0, tex->frameCount - 1);
				}
				else
				{
					tex->curFrame = 0;
				}

				s32 texW = tex->width;
				s32 texH = tex->height;
				s32 scale = max(1, min(4, s32(w) / texW));
				texW *= scale;
				texH *= scale;

				ImGui::Image(TFE_RenderBackend::getGpuPtr(tex->frames[tex->curFrame]),
					ImVec2((f32)texW, (f32)texH), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
			}
			else if (asset->type == TYPE_FRAME)
			{
				EditorFrame* frame = (EditorFrame*)getAssetData(asset->handle);

				ImGui::LabelText("##Size", "Size: %d x %d", frame->width, frame->height);
				ImGui::LabelText("##Dim",  "World Dim: %0.3f x %0.3f", frame->worldWidth, frame->worldHeight);

				bool reloadRequired = false;
				s32 newPaletteIndex = frame->paletteIndex;
				listSelectionPalette("Palette", s_palettes, &newPaletteIndex);
				if (newPaletteIndex != frame->paletteIndex)
				{
					reloadRequired = true;
				}

				s32 newLightLevel = frame->lightLevel;
				if (s_palettes[newPaletteIndex].hasColormap)
				{
					ImGui::SliderInt("Light Level", &newLightLevel, 0, 32);
				}
				else
				{
					newLightLevel = 32;
				}
				if (newLightLevel != frame->lightLevel)
				{
					reloadRequired = true;
				}

				if (reloadRequired)
				{
					reloadAsset(asset, newPaletteIndex, newLightLevel);
					frame = (EditorFrame*)getAssetData(asset->handle);
				}

				s32 texW = frame->width;
				s32 texH = frame->height;
				s32 scale = max(1, min(4, s32(w) / texW));
				texW *= scale;
				texH *= scale;

				ImGui::Image(TFE_RenderBackend::getGpuPtr(frame->texGpu),
					ImVec2((f32)texW, (f32)texH), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
			}
			else if (asset->type == TYPE_SPRITE)
			{
				EditorSprite* sprite = (EditorSprite*)getAssetData(asset->handle);
				ImGui::LabelText("##Dim", "World Dim: %0.3f x %0.3f", sprite->frame[0].widthWS, sprite->frame[0].heightWS);
				ImGui::LabelText("##AnimCount", "Animation Count: %d", (s32)sprite->anim.size());
				ImGui::LabelText("##CellCount", "Cell Count: %d", (s32)sprite->cell.size());
				ImGui::Separator();
				bool reloadRequired = false;
				s32 newPaletteIndex = sprite->paletteIndex;
				listSelectionPalette("Palette", s_palettes, &newPaletteIndex);
				if (newPaletteIndex != sprite->paletteIndex)
				{
					reloadRequired = true;
				}

				s32 newLightLevel = sprite->lightLevel;
				if (s_palettes[newPaletteIndex].hasColormap)
				{
					ImGui::SliderInt("Light Level", &newLightLevel, 0, 32);
				}
				else
				{
					newLightLevel = 32;
				}
				if (newLightLevel != sprite->lightLevel)
				{
					reloadRequired = true;
				}

				if (reloadRequired)
				{
					reloadAsset(asset, newPaletteIndex, newLightLevel);
					sprite = (EditorSprite*)getAssetData(asset->handle);
				}
				ImGui::Separator();
				if (sprite->anim.size() > 1)
				{
					s32 animId = (s32)sprite->animId;
					if (ImGui::SliderInt("Anim", &animId, 0, (s32)sprite->anim.size() - 1))
					{
						sprite->frameId = 0;
					}
					sprite->animId = (u8)animId;
				}
				const SpriteAnim* anim = &sprite->anim[sprite->animId];
				if (anim->frameCount > 1)
				{
					s32 frameId = (s32)sprite->frameId;
					ImGui::SliderInt("Frame", &frameId, 0, (s32)anim->frameCount - 1);
					sprite->frameId = (u8)frameId;
				}
				// View
				{
					s32 viewId = (s32)sprite->viewId;
					ImGui::SliderInt("View Angle", &viewId, 0, 31);
					sprite->viewId = (u8)viewId;
				}
				ImGui::Separator();
				// Get the actual cell...
				s32 frameIndex = sprite->anim[sprite->animId].views[sprite->viewId].frameIndex[sprite->frameId];
				SpriteFrame* frame = &sprite->frame[frameIndex];
				SpriteCell* cell = &sprite->cell[frame->cellIndex];

				s32 texW = cell->w;
				s32 texH = cell->h;
				s32 scale = max(1, min(4, s32(w) / (sprite->rect[2] - sprite->rect[0])));
				texW *= scale;
				texH *= scale;

				f32 u0 = f32(cell->u) / (f32)sprite->texGpu->getWidth();
				f32 v1 = f32(cell->v) / (f32)sprite->texGpu->getHeight();
				f32 u1 = f32(cell->u + cell->w) / (f32)sprite->texGpu->getWidth();
				f32 v0 = f32(cell->v + cell->h) / (f32)sprite->texGpu->getHeight();
				if (frame->flip) { std::swap(u0, u1); }

				// Handle the offset.
				ImVec2 cursor = ImGui::GetCursorPos();
				cursor.x += f32((frame->offsetX - sprite->rect[0]) * scale);
				cursor.y += f32((frame->offsetY - sprite->rect[1]) * scale);
				ImGui::SetCursorPos(cursor);

				ImGui::Image(TFE_RenderBackend::getGpuPtr(sprite->texGpu),
					ImVec2((f32)texW, (f32)texH), ImVec2(u0, v0), ImVec2(u1, v1));
			}
			else if (asset->type == TYPE_PALETTE)
			{
				EditorTexture* tex = (EditorTexture*)getAssetData(asset->handle);
				ImGui::Image(TFE_RenderBackend::getGpuPtr(tex->frames[0]),
					ImVec2(128.0f, 128.0f), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));

				// Show the colormap if it exists.
				if (tex->frameCount == 2)
				{
					ImGui::Separator();
					ImGui::Image(TFE_RenderBackend::getGpuPtr(tex->frames[1]),
						ImVec2(512.0f, 64.0f), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
				}
			}
		}
		ImGui::End();
	}

	bool isSelected(s32 index)
	{
		if(s_selected.empty()) { return false; }

		const size_t count = s_selected.size();
		const s32* selected = s_selected.data();
		for (size_t i = 0; i < count; i++)
		{
			if (selected[i] == index) { return true; }
		}
		return false;
	}
				
	void unselect(s32 index)
	{
		if (index < 0 || index >= s_viewAssetList.size()) { return; }
		const size_t count = s_selected.size();
		const s32* selected = s_selected.data();
		size_t selectedIndex = count;
		for (size_t i = 0; i < count; i++)
		{
			if (selected[i] == index)
			{
				selectedIndex = i;
				break;
			}
		}
		if (selectedIndex < count)
		{
			s_selected.erase(s_selected.begin() + selectedIndex);
		}
	}

	void select(s32 index)
	{
		if (index < 0 || index >= s_viewAssetList.size()) { return; }
		const size_t count = s_selected.size();
		const s32* selected = s_selected.data();
		size_t selectedIndex = count;
		for (size_t i = 0; i < count; i++)
		{
			if (selected[i] == index)
			{
				// Already selected.
				return;
			}
		}
		s_selected.push_back(index);
	}

	void selectRange()
	{
		s_selected.clear();
		if (s_selectRange[1] < 0)
		{
			s_selected.push_back(s_selectRange[0]);
			return;
		}
		if (s_selectRange[0] > s_selectRange[1])
		{
			std::swap(s_selectRange[0], s_selectRange[1]);
		}
		for (s32 i = s_selectRange[0]; i <= s_selectRange[1]; i++)
		{
			s_selected.push_back(i);
		}
	}
		
	ImVec4 getBorderColor(s32 index)
	{
		if (isSelected(index))
		{
			return ImVec4(1.0f, 1.0f, 0.5f, 1.0f);
		}
		else if (index == s_hovered)
		{
			return ImVec4(0.5f, 1.0f, 1.0f, 1.0f);
		}
		return ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
	}

	void drawAssetList(s32 w, s32 h)
	{
		const s32 count = (s32)s_viewAssetList.size();
		const Asset* asset = s_viewAssetList.data();

		s32 textWidth = (s32)ImGui::CalcTextSize("12345678.123").x;
		s32 fontSize = (s32)ImGui::GetFontSize();

		char buttonLabel[32];
		s32 itemWidth = 16 + max(textWidth, s_editorConfig.thumbnailSize);
		s32 itemHeight = 4 + fontSize + s_editorConfig.thumbnailSize;

		s32 columnCount = max(1, s32(w - 16) / itemWidth);
		f32 topPos = ImGui::GetCursorPosY();

		bool mouseClicked = false;
		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
		{
			if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
			{
				mouseClicked = true;
				if (!TFE_Input::keyModDown(KEYMOD_CTRL) && !TFE_Input::keyModDown(KEYMOD_SHIFT))
				{
					s_selected.clear();
					s_selectRange[0] = -1;
					s_selectRange[1] = -1;
				}
			}
		}
		else
		{
			s_hovered = -1;
		}

		s32 a = 0, yOffset = 0;
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 0.0f, 0.0f });
		AssetSource src = s_viewAssetList[0].assetSource;
		for (s32 y = 0; a < count; y++)
		{
			for (s32 x = 0; x < columnCount && a < count; x++, a++)
			{
				// Seperate Out asset sources.
				if (src != s_viewAssetList[a].assetSource)
				{
					src = s_viewAssetList[a].assetSource;
					ImGui::Separator();

					yOffset += 10;
					if (x != 0) { y++; }
					x = 0;
				}

				sprintf(buttonLabel, "###asset%d", a);
				ImVec2 cursor((8.0f + x * itemWidth), topPos + y * itemHeight + yOffset);
				ImGui::SetCursorPos(cursor);

				ImGui::PushStyleColor(ImGuiCol_Border, getBorderColor(a));
				if (ImGui::BeginChild(buttonLabel, ImVec2(f32(itemWidth), f32(itemHeight)), ImGuiChildFlags_Border, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollWithMouse))
				{
					s32 offsetX = 0, offsetY = 0;
					s32 width = s_editorConfig.thumbnailSize, height = s_editorConfig.thumbnailSize;

					const TextureGpu* textureGpu = nullptr;
					f32 u0 = 0.0f, v0 = 1.0f;
					f32 u1 = 1.0f, v1 = 0.0f;
					if (s_viewAssetList[a].type == TYPE_TEXTURE || s_viewAssetList[a].type == TYPE_EXT_TEXTURE || s_viewAssetList[a].type == TYPE_PALETTE)
					{
						EditorTexture* tex = (EditorTexture*)getAssetData(s_viewAssetList[a].handle);
						textureGpu = tex ? tex->frames[0] : nullptr;
						if (textureGpu)
						{
							// Preserve the image aspect ratio.
							if (tex->width >= tex->height)
							{
								height = tex->height * s_editorConfig.thumbnailSize / tex->width;
								offsetY = (width - height) / 2;
							}
							else
							{
								width = tex->width * s_editorConfig.thumbnailSize / tex->height;
								offsetX = (height - width) / 2;
							}
						}
					}
					else if (s_viewAssetList[a].type == TYPE_FRAME)
					{
						EditorFrame* frame = (EditorFrame*)getAssetData(s_viewAssetList[a].handle);
						textureGpu = frame ? frame->texGpu : nullptr;
						if (textureGpu)
						{
							// Preserve the image aspect ratio.
							if (frame->width >= frame->height)
							{
								height = frame->height * s_editorConfig.thumbnailSize / frame->width;
								offsetY = (width - height) / 2;
							}
							else
							{
								width = frame->width * s_editorConfig.thumbnailSize / frame->height;
								offsetX = (height - width) / 2;
							}
						}
					}
					else if (s_viewAssetList[a].type == TYPE_SPRITE)
					{
						EditorSprite* sprite = (EditorSprite*)getAssetData(s_viewAssetList[a].handle);
						textureGpu = sprite ? sprite->texGpu : nullptr;
						if (textureGpu)
						{
							SpriteCell* cell = &sprite->cell[0];
							// Preserve the image aspect ratio.
							if (cell->w >= cell->h)
							{
								height = cell->h * s_editorConfig.thumbnailSize / cell->w;
								offsetY = (width - height) / 2;
							}
							else
							{
								width = cell->w * s_editorConfig.thumbnailSize / cell->h;
								offsetX = (height - width) / 2;
							}

							u0 = f32(cell->u) / (f32)textureGpu->getWidth();
							v1 = f32(cell->v) / (f32)textureGpu->getHeight();

							u1 = f32(cell->u + cell->w) / (f32)textureGpu->getWidth();
							v0 = f32(cell->v + cell->h) / (f32)textureGpu->getHeight();
						}
					}
					else if (s_viewAssetList[a].type == TYPE_3DOBJ)
					{
						EditorObj3D* obj3D = (EditorObj3D*)getAssetData(s_viewAssetList[a].handle);
						Vec2f uv0, uv1;
						textureGpu = obj3D ? getThumbnail(obj3D, &uv0, &uv1, false) : nullptr;
						u0 = uv0.x;
						u1 = uv1.x;
						v0 = uv0.z;
						v1 = uv1.z;
					}
					else if (s_viewAssetList[a].type == TYPE_LEVEL)
					{
						EditorLevelPreview* lev = (EditorLevelPreview*)getAssetData(s_viewAssetList[a].handle);
						textureGpu = lev ? lev->thumbnail : nullptr;
						if (textureGpu)
						{
							// Preserve the image aspect ratio.
							if (textureGpu->getWidth() >= textureGpu->getHeight())
							{
								height = textureGpu->getHeight() * s_editorConfig.thumbnailSize / textureGpu->getWidth();
								offsetY = (width - height) / 2;
							}
							else
							{
								width = textureGpu->getWidth() * s_editorConfig.thumbnailSize / textureGpu->getHeight();
								offsetX = (height - width) / 2;
							}
						}
					}
					// Center image.
					offsetX += (itemWidth - s_editorConfig.thumbnailSize) / 2;

					// Draw the image.
					ImGui::SetCursorPos(ImVec2((f32)offsetX, (f32)offsetY));
					if (textureGpu)
					{
						ImGui::Image(TFE_RenderBackend::getGpuPtr(textureGpu),
							ImVec2((f32)width, (f32)height), ImVec2(u0, v0), ImVec2(u1, v1));
					}

					// Draw the label.
					ImGui::SetCursorPos(ImVec2(8.0f, (f32)s_editorConfig.thumbnailSize));
					ImGui::SetNextItemWidth(f32(itemWidth - 16));
					ImGui::LabelText("###", "%s", asset[a].name.c_str());

					if (ImGui::IsWindowHovered())
					{
						s_hovered = a;
						if (mouseClicked && TFE_Input::keyModDown(KEYMOD_CTRL))
						{
							if (isSelected(a))
							{
								unselect(a);
							}
							else
							{
								select(a);
							}

							s_selectRange[0] = a;
							s_selectRange[1] = -1;
						}
						else if (mouseClicked && TFE_Input::keyModDown(KEYMOD_SHIFT))
						{
							if (s_selectRange[0] < 0)
							{
								s_selectRange[0] = a;
								s_selectRange[1] = -1;
							}
							else
							{
								s_selectRange[1] = a;
							}
							selectRange();
						}
						else if (mouseClicked)
						{
							s_selected.resize(1);
							s_selected[0] = a;

							s_selectRange[0] = a;
							s_selectRange[1] = -1;
						}
					}
				}
				ImGui::EndChild();
				ImGui::PopStyleColor();
			}
		}
		ImGui::PopStyleVar();
	}
		
	void listPanel(u32 infoWidth, u32 infoHeight)
	{
		DisplayInfo displayInfo;
		TFE_RenderBackend::getDisplayInfo(&displayInfo);
		if (displayInfo.width < infoWidth) { return; }

		s32 w = displayInfo.width - (s32)infoWidth;
		s32 h = displayInfo.height - s_menuHeight;
		if (w <= 0 || h <= 0) { return; }

		ImGui::SetWindowPos("Asset List", { f32(infoWidth), f32(s_menuHeight) });
		ImGui::SetWindowSize("Asset List", { f32(w), f32(h) });
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

		bool active = true;
		ImGui::Begin("Asset List", &active, window_flags);

		// Draw items
		if (!s_viewAssetList.empty())
		{
			drawAssetList(w, h);

			// Info Panel
			Asset* selectedAsset = nullptr;
			bool multiselect = false;
			if (s_selected.size() == 1 && s_selected[0] < s_viewAssetList.size())
			{
				selectedAsset = &s_viewAssetList[s_selected[0]];
			}
			else if (s_selected.size() > 1)
			{
				multiselect = true;
			}
			else if (s_hovered >= 0 && s_hovered < s_viewAssetList.size())
			{
				selectedAsset = &s_viewAssetList[s_hovered];
			}
			drawInfoPanel(selectedAsset, infoWidth, infoHeight, multiselect);
		}

		ImGui::End();
	}

	void selectByAssetName(const char* selectName)
	{
		if (!selectName) { return; }
		const u32 count = (u32)s_projectAssetList[s_viewInfo.type].size();
		const Asset* projAsset = s_projectAssetList[s_viewInfo.type].data();
		for (u32 i = 0; i < count; i++, projAsset++)
		{
			const char* name = projAsset->name.c_str();
			if (strcasecmp(selectName, name) == 0)
			{
				select(i);
				return;
			}
		}
	}

	const char* getSelectedAssetName()
	{
		if (s_selected.empty()) { return nullptr; }
		const Asset* projAsset = s_projectAssetList[s_viewInfo.type].data();
		return projAsset[s_selected[0]].name.c_str();
	}

	void initPopup(TFE_Editor::AssetType type, const char* selectName)
	{
		s_viewInfo.type = type;
		s_viewInfo.assetFilter[0] = 0;
		s_viewInfo.filterLen = 0;
		s_selected.clear();
		s_hovered = -1;
		s_selectRange[0] = -1;
		s_selectRange[1] = -1;

		updateAssetList();
		selectByAssetName(selectName);
	}
	
	bool popup()
	{
		f32 winWidth = 1044.0f;
		f32 winHeight = 700.0f;
		f32 width = 1024.0f;
		f32 height = 640.0f;

		bool exitPopup = false;
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoResize;
		ImGui::SetNextWindowSize({ winWidth, winHeight });
		if (ImGui::BeginPopupModal("Browse", nullptr, window_flags))
		{
			ImGui::BeginChild(0x303, { width, height }, ImGuiChildFlags_Border);
			drawAssetList((s32)width, (s32)height);
			ImGui::EndChild();

			if (ImGui::Button("Select"))
			{
				exitPopup = true;
			}
			ImGui::SameLine(0.0f, 32.0f);
			if (ImGui::Button("Cancel"))
			{
				s_selected.clear();
				exitPopup = true;
			}
			ImGui::EndPopup();
		}
		return exitPopup;
	}

	void update()
	{
		if (resources_listChanged())
		{
			s_reloadProjectAssets = true;
			s_assetsNeedProcess = true;
			updateAssetList();
		}

		DisplayInfo displayInfo;
		TFE_RenderBackend::getDisplayInfo(&displayInfo);

		// Figure out how to scale the info column and panel based on the font scale.
		s32 wScale = 100 + max(0, s_editorConfig.fontScale - 125);
		s32 hScale = 100 + (s_editorConfig.fontScale - 100) / 2;

		u32 infoWidth = std::min((u32)(INFO_PANEL_WIDTH * wScale / 100), displayInfo.width);
		u32 infoHeight = (u32)(150 * hScale / 100);
				
		// Push the "small" font and draw the panels.
		pushFont(TFE_Editor::FONT_SMALL);
		s_menuHeight = 6 + (s32)ImGui::GetFontSize();

		infoPanel(infoWidth, infoHeight);
		listPanel(infoWidth, infoHeight);

		popFont();
	}

	void render()
	{
	}

	void selectAll()
	{
		const s32 count = (s32)s_viewAssetList.size();
		s_selected.resize(count);
		for (s32 i = 0; i < count; i++)
		{
			s_selected[i] = i;
		}

		s_selectRange[0] = 0;
		s_selectRange[1] = -1;
	}

	void selectNone()
	{
		s_selected.clear();
		s_selectRange[0] = -1;
		s_selectRange[1] = -1;
	}

	void invertSelection()
	{
		const s32 count = (s32)s_viewAssetList.size();
		SelectionList list;
		s_selectRange[0] = -1;
		s_selectRange[1] = -1;
		for (s32 i = 0; i < count; i++)
		{
			if (!isSelected(i))
			{
				list.push_back(i);
			}
		}
		s_selected = list;
	}

	bool showOnlyModLevels()
	{
		return s_viewInfo.showOnlyModLevels;
	}

	void rebuildAssets()
	{
		s_reloadProjectAssets = true;
		s_assetsNeedProcess = true;
		updateAssetList();
	}

	Asset* findAsset(const char* name, AssetType type)
	{
		const size_t len = s_projectAssetList[type].size();
		Asset* asset = s_projectAssetList[type].data();
		for (size_t i = 0; i < len; i++, asset++)
		{
			if (strcasecmp(asset->name.c_str(), name) == 0)
			{
				return asset;
			}
		}
		return nullptr;
	}

	////////////////////////////////////////////////
	// Internal
	////////////////////////////////////////////////
	Archive* getArchive(const char* name, GameID gameId)
	{
		TFE_Settings_Game* game = TFE_Settings::getGameSettings();
		char filePath[TFE_MAX_PATH];
		sprintf(filePath, "%s%s", game->header[gameId].sourcePath, name);

		return Archive::getArchive(ARCHIVE_GOB, name, filePath);
	}
		
	void addPalette(const char* name, Archive* archive)
	{
		s32 id = -1;
		const size_t count = s_palettes.size();
		for (size_t i = 0; i < count; i++)
		{
			if (strcasecmp(s_palettes[i].name.c_str(), name) == 0)
			{
				id = s32(i);
				break;
			}
		}

		if (!archive->openFile(name)) { return; }
		WorkBuffer& buffer = getWorkBuffer();
		const size_t len = archive->getFileLength();
		buffer.resize(len);
		archive->readFile(buffer.data(), len);
		archive->closeFile();

		const u8* pal666 = buffer.data();
		Palette dstPalette;
		dstPalette.name = name;
		dstPalette.hasColormap = false;
		for (s32 i = 0; i < 256; i++)
		{
			const u8* rgb6 = &pal666[i * 3];
			dstPalette.data[i] = (i == 0) ? 0 : 0xff000000 | CONV_6bitTo8bit(rgb6[0]) | (CONV_6bitTo8bit(rgb6[1]) << 8) | (CONV_6bitTo8bit(rgb6[2]) << 16);
		}

		// Does it have a colormap?
		// For now we assume the names match.
		char colorMapPath[256];
		FileUtil::replaceExtension(name, "CMP", colorMapPath);

		const size_t cmapCount = s_projectAssetList[TYPE_COLORMAP].size();
		const Asset* projAsset = s_projectAssetList[TYPE_COLORMAP].data();
		for (size_t i = 0; i < cmapCount; i++, projAsset++)
		{
			if (strcasecmp(projAsset->name.c_str(), colorMapPath) == 0)
			{
				Archive* cmapArchive = projAsset->archive;
				if (cmapArchive && cmapArchive->openFile(colorMapPath))
				{
					WorkBuffer& buffer = getWorkBuffer();
					const size_t len = cmapArchive->getFileLength();
					buffer.resize(len);
					cmapArchive->readFile(buffer.data(), len);
					cmapArchive->closeFile();

					memcpy(dstPalette.colormap, buffer.data(), 8192);
					dstPalette.hasColormap = true;

					cmapArchive->closeFile();
					break;
				}
			}
		}
		if (id < 0) { s_palettes.push_back(dstPalette); }
		else { s_palettes[id] = dstPalette; }

		if (strcasecmp(name, "SECBASE.PAL") == 0)
		{
			s_defaultPal = (s32)s_palettes.size() - 1;
		}
	}

	s32 getPaletteId(const char* name)
	{
		const size_t count = s_palettes.size();
		for (size_t i = 0; i < count; i++)
		{
			if (strcasecmp(s_palettes[i].name.c_str(), name) == 0)
			{
				return s32(i);
			}
		}
		return -1;
	}

	void setAssetPalette(const char* name, s32 paletteId)
	{
		std::map<std::string, s32>::iterator iAssetPal = s_assetPalette.find(name);
		if (iAssetPal == s_assetPalette.end())
		{
			s_assetPalette[name] = paletteId;
		}
	}

	s32 getAssetPalette(const char* name)
	{
		std::map<std::string, s32>::iterator iAssetPal = s_assetPalette.find(name);
		if (iAssetPal != s_assetPalette.end())
		{
			return iAssetPal->second;
		}
		// Not sure how to automatically figure this out yet.
		if (strcasecmp(name, "WAIT.BM") == 0)
		{
			return 0;
		}
		return s_defaultPal;
	}
		
	void setAssetLevel(const char* name, u32 levelIndex)
	{
		std::map<std::string, u32>::iterator iAssetPal = s_assetLevels.find(name);
		if (iAssetPal == s_assetLevels.end())
		{
			s_assetLevels[name] = (1u << levelIndex);
		}
		else
		{
			iAssetPal->second |= (1u << levelIndex);
		}
	}

	bool isAssetInLevel(const char* name, u32 levelIndex)
	{
		std::map<std::string, u32>::iterator iAssetPal = s_assetLevels.find(name);
		if (iAssetPal != s_assetLevels.end())
		{
			return (iAssetPal->second & (1 << levelIndex)) != 0u;
		}
		return false;
	}
		
	void addToLevelAssets(std::vector<std::string>& assetList, const char* assetName)
	{
		const size_t count = assetList.size();
		for (size_t i = 0; i < count; i++)
		{
			if (assetList[i] == assetName)
			{
				return;
			}
		}
		assetList.push_back(assetName);
	}

	void preprocessAssets()
	{
		if (!s_assetsNeedProcess) { return; }
		s_assetsNeedProcess = false;
		s_levelAssets.clear();

		const u32 count = (u32)s_projectAssetList[TYPE_PALETTE].size();

		// First search for palettes.
		const Asset* projAsset = s_projectAssetList[TYPE_PALETTE].data();
		for (u32 f = 0; f < count; f++, projAsset++)
		{
			addPalette(projAsset->name.c_str(), projAsset->archive);
		}

		// Then for levels.
		WorkBuffer buffer;
		const u32 levelCount = (u32)s_projectAssetList[TYPE_LEVEL].size();
		projAsset = s_projectAssetList[TYPE_LEVEL].data();
		for (u32 f = 0; f < levelCount; f++, projAsset++)
		{
			Archive* archive = projAsset->archive;
			// TODO: Handle non-archive levels.
			if (!archive) { continue; }

			// Parse the level for 1) the palette, 2) the data lists.
			if (archive->openFile(projAsset->name.c_str()))
			{
				LevelAssets levelAssets;
				levelAssets.levelName = projAsset->name;
				s32 paletteId = s_defaultPal;

				// .LEV
				{
					size_t len = archive->getFileLength();
					buffer.resize(len);
					archive->readFile(buffer.data(), len);
					archive->closeFile();

					TFE_Parser parser;
					size_t bufferPos = 0;
					parser.init((char*)buffer.data(), buffer.size());
					parser.addCommentString("#");
					parser.convertToUpperCase(true);

					// Read just enough of the file...
					const char* line;
					line = parser.readLine(bufferPos);
					s32 versionMajor, versionMinor;
					if (sscanf(line, " LEV %d.%d", &versionMajor, &versionMinor) != 2)
					{
						continue;
					}

					char readBuffer[256];
					line = parser.readLine(bufferPos);
					if (sscanf(line, " LEVELNAME %s", readBuffer) != 1)
					{
						continue;
					}

					// This gets read here just to be overwritten later... so just ignore for now.
					line = parser.readLine(bufferPos);
					if (sscanf(line, " PALETTE %s", readBuffer) != 1)
					{
						continue;
					}
					// Fixup the palette, strip any path.
					char paletteName[TFE_MAX_PATH];
					FileUtil::getFileNameFromPath(readBuffer, paletteName, true);
					paletteId = getPaletteId(paletteName);
					if (paletteId < 0) { paletteId = 0; }

					levelAssets.paletteName = paletteName;

					// Another value that is ignored.
					line = parser.readLine(bufferPos);
					if (sscanf(line, " MUSIC %s", readBuffer) == 1)
					{
						line = parser.readLine(bufferPos);
					}

					// Sky Parallax - this option until version 1.9, so handle its absence.
					f32 x, z;
					if (sscanf(line, " PARALLAX %f %f", &x, &z) == 2)
					{
						line = parser.readLine(bufferPos);
					}

					// Number of textures used by the level.
					s32 textureCount = 0;
					if (sscanf(line, " TEXTURES %d", &textureCount) != 1)
					{
						continue;
					}

					// Read texture names.
					char textureName[256];
					for (s32 i = 0; i < textureCount; i++)
					{
						line = parser.readLine(bufferPos);
						if (sscanf(line, " TEXTURE: %s ", textureName) == 1)
						{
							setAssetPalette(textureName, paletteId);
							setAssetLevel(textureName, f);
							addToLevelAssets(levelAssets.textures, textureName);
						}
					}
					// Sometimes there are extra textures, just add them - they will be compacted later.
					bool readNext = true;
					while (sscanf(line, " TEXTURE: %s ", textureName) == 1)
					{
						setAssetPalette(textureName, paletteId);
						setAssetLevel(textureName, f);
						addToLevelAssets(levelAssets.textures, textureName);
						line = parser.readLine(bufferPos);
						readNext = false;
					}
				}

				// .O File
				char objFile[TFE_MAX_PATH];
				FileUtil::replaceExtension(projAsset->name.c_str(), "O", objFile);
				if (archive->openFile(objFile))
				{
					size_t len = archive->getFileLength();
					buffer.resize(len);
					archive->readFile(buffer.data(), len);
					archive->closeFile();

					TFE_Parser parser;
					size_t bufferPos = 0;
					parser.init((char*)buffer.data(), buffer.size());
					parser.addCommentString("#");
					parser.convertToUpperCase(true);

					enum ProcessFlags
					{
						PFLAG_POD = (1 << 0),
						PFLAG_SPRITE = (1 << 1),
						PFLAG_FRAME = (1 << 2),
						PFLAG_PROCESS_DONE = PFLAG_POD | PFLAG_SPRITE | PFLAG_FRAME
					};
					u32 processFlags = 0;
					const char* line;
					while ((line = parser.readLine(bufferPos)) && (processFlags != PFLAG_PROCESS_DONE))
					{
						// Search for "PODS"
						s32 count = 0;
						char assetName[32];
						if (sscanf(line, "PODS %d", &count) == 1)
						{
							for (s32 p = 0; p < count; p++)
							{
								line = parser.readLine(bufferPos);
								if (line)
								{
									if (sscanf(line, " POD: %s", assetName) == 1)
									{
										setAssetPalette(assetName, paletteId);
										addToLevelAssets(levelAssets.pods, assetName);
									}
								}
							}
							processFlags |= PFLAG_POD;
						}
						else if (sscanf(line, "SPRS %d", &count) == 1)
						{
							for (s32 p = 0; p < count; p++)
							{
								line = parser.readLine(bufferPos);
								if (line)
								{
									if (sscanf(line, " SPR: %s", assetName) == 1)
									{
										setAssetPalette(assetName, paletteId);
										addToLevelAssets(levelAssets.sprites, assetName);
									}
								}
							}
							processFlags |= PFLAG_SPRITE;
						}
						else if (sscanf(line, "FMES %d", &count) == 1)
						{
							for (s32 p = 0; p < count; p++)
							{
								line = parser.readLine(bufferPos);
								if (line)
								{
									if (sscanf(line, " FME: %s", assetName) == 1)
									{
										setAssetPalette(assetName, paletteId);
										addToLevelAssets(levelAssets.frames, assetName);
									}
								}
							}
							processFlags |= PFLAG_FRAME;
						}
					}
				}

				s_levelAssets.push_back(levelAssets);
			}
		}
	}

	void reloadAsset(Asset* asset, s32 palId, s32 lightLevel)
	{
		if (asset && asset->archive)
		{
			AssetColorData colorData = { s_palettes[palId].data, s_palettes[palId].colormap, palId, lightLevel };
			reloadAssetData(asset->handle, asset->archive, &colorData);
		}
	}

	// For now only handle textures and palettes.
	AssetType getAssetType(const char* ext)
	{
		if (strcasecmp(ext, "BM") == 0)
		{
			return TYPE_TEXTURE;
		}
		else if (strcasecmp(ext, "LEV") == 0)
		{
			return TYPE_LEVEL;
		}
		else if (strcasecmp(ext, "PAL") == 0)
		{
			return TYPE_PALETTE;
		}
		else if (strcasecmp(ext, "CMP") == 0)
		{
			return TYPE_COLORMAP;
		}
		else if (strcasecmp(ext, "FME") == 0)
		{
			return TYPE_FRAME;
		}
		else if (strcasecmp(ext, "WAX") == 0)
		{
			return TYPE_SPRITE;
		}
		else if (strcasecmp(ext, "3DO") == 0)
		{
			return TYPE_3DOBJ;
		}
		else if (strcasecmp(ext, "LEV") == 0)
		{
			return TYPE_LEVEL;
		}
		else if (strcasecmp(ext, "VOC") == 0)
		{
			return TYPE_SOUND;
		}
		// Extended formats (used by the DF Remaster)
		else if (strcasecmp(ext, "fxx") == 0)
		{
			return TYPE_EXT_FRAME;
		}
		else if (strcasecmp(ext, "wxx") == 0)
		{
			return TYPE_EXT_WAX;
		}
		else if (strcasecmp(ext, "raw") == 0)
		{
			return TYPE_EXT_TEXTURE;
		}
		return TYPE_COUNT;
	}

	s32 findAsset(Asset& asset)
	{
		s32 foundId = -1;
		const AssetList& list = s_projectAssetList[asset.type];
		const s32 count = (s32)list.size();
		const Asset* assetList = list.data();
		for (s32 i = 0; i < count; i++)
		{
			if (strcasecmp(assetList[i].name.c_str(), asset.name.c_str()) == 0)
			{
				foundId = i;
				break;
			}
		}
		return foundId;
	}
	
	bool extractArchive(Archive* srcArchive, const char* filename, char* outPath)
	{
		if (!srcArchive->openFile(filename)) { return false; }

		char tmpDir[TFE_MAX_PATH];
		getTempDirectory(tmpDir);

		// Copy the file into a temporary location and then add it.
		char newPath[TFE_MAX_PATH];
		sprintf(newPath, "%s/%s", tmpDir, filename);

		WorkBuffer& buffer = getWorkBuffer();
		const size_t len = srcArchive->getFileLength();
		buffer.resize(len);
		srcArchive->readFile(buffer.data(), len);

		FileStream newArchive;
		if (newArchive.open(newPath, Stream::MODE_WRITE))
		{
			newArchive.writeBuffer(buffer.data(), (u32)len);
			newArchive.close();
		}
		srcArchive->closeFile();

		if (outPath) { strcpy(outPath, newPath); }
		return true;
	}

	void addArchiveFiles(Archive* archive, GameID gameId, const char* name, AssetSource assetSource)
	{
		if (!archive) { return; }
		u32 fileCount = archive->getFileCount();
		for (u32 f = 0; f < fileCount; f++)
		{
			const char* fileName = archive->getFileName(f);
			char ext[16];
			FileUtil::getFileExtension(fileName, ext);

			// Handle archives inside of directories.
			ArchiveType archiveType = getArchiveType(fileName);
			if (archiveType != ARCHIVE_UNKNOWN)
			{
				char newPath[TFE_MAX_PATH];
				if (extractArchive(archive, fileName, newPath))
				{
					// Add the temporary archive.
					Archive* archive = Archive::getArchive(archiveType, fileName, newPath);
					addArchiveFiles(archive, gameId, fileName, ASRC_EXTERNAL);
				}
				continue;
			}

			AssetType type = getAssetType(ext);
			if (type < TYPE_COUNT)
			{
				Asset newAsset =
				{
					type,		// type
					gameId,		// gameId
					archive,	// Archive
					fileName,	// name
					"",			// filepath
					assetSource,// flags
				};

				// Check to see if the asset already exists, if so replace it.
				s32 id = findAsset(newAsset);
				AssetList& list = s_projectAssetList[type];
				if (id < 0)
				{
					list.push_back(newAsset);
				}
				else
				{
					list[id] = newAsset;
				}
			}
		}
	}
	
	void addDirectoryFiles(const char* path, GameID gameId, AssetSource assetSource)
	{
		if (!path || path[0] == 0) { return; }
		char pathOS[TFE_MAX_PATH];
		sprintf(pathOS, "%s/", path);
		FileUtil::fixupPath(pathOS);

		std::vector<std::string> searchStack;
		searchStack.push_back(pathOS);

		while (!searchStack.empty())
		{
			const std::string dir = searchStack.back();
			searchStack.pop_back();

			// First add any subdirectories to the search path.
			FileList dirList;
			FileUtil::readSubdirectories(dir.c_str(), dirList);
			if (!dirList.empty())
			{
				const size_t count = dirList.size();
				char subDir[1024];
				for (size_t i = 0; i < count; i++)
				{
					strcpy(subDir, dirList[i].c_str());
					FileUtil::fixupPath(subDir);

					searchStack.push_back(subDir);
				}
			}

			// Then handle the files.
			FileList fileList;
			FileUtil::readDirectory(dir.c_str(), "*", fileList);
			const size_t count = fileList.size();
			for (size_t i = 0; i < count; i++)
			{
				const char* fileName = fileList[i].c_str();
				if (!fileName || fileName[0] == '.') { continue; }

				char ext[16];
				FileUtil::getFileExtension(fileName, ext);

				char filepath[TFE_MAX_PATH];
				sprintf(filepath, "%s%s", pathOS, fileName);
				FileUtil::fixupPath(filepath);

				// Exported results are archives too, so this doesn't work correctly for project directories.
				if (assetSource != ASRC_PROJECT)
				{
					ArchiveType archiveType = getArchiveType(fileName);
					if (archiveType != ARCHIVE_UNKNOWN)
					{
						// Add the archive.
						Archive* archive = Archive::getArchive(archiveType, fileName, filepath);
						addArchiveFiles(archive, gameId, fileName, ASRC_EXTERNAL);
						continue;
					}
				}

				AssetType type = getAssetType(ext);
				if (type < TYPE_COUNT)
				{
					Asset newAsset =
					{
						type,		// Type
						gameId,		// Game ID
						nullptr,	// Archive
						fileName,	// Name
						filepath,	// File path
						assetSource,// Asset Source
					};

					// Check to see if the asset already exists, if so replace it.
					s32 id = findAsset(newAsset);
					AssetList& list = s_projectAssetList[type];
					if (id < 0)
					{
						list.push_back(newAsset);
					}
					else
					{
						list[id] = newAsset;
					}
				}
			}
		}
	}

	bool sortByAssetSource(Asset& a, Asset& b)
	{
		const s32 order[] =
		{
			2, // ASRC_VANILLA
			1, // ASRC_EXTERNAL
			0, // ASRC_PROJECT
		};
		return order[a.assetSource] < order[b.assetSource];
	}

	void sortAssetList()
	{
		for (u32 i = 0; i < TYPE_COUNT; i++)
		{
			std::sort(s_projectAssetList[i].begin(), s_projectAssetList[i].end(), sortByAssetSource);
		}
	}

	void buildProjectAssetList(GameID gameId)
	{
		if (!s_reloadProjectAssets) { return; }
		s_reloadProjectAssets = false;

		resources_setGame(gameId);
		for (u32 i = 0; i < TYPE_COUNT; i++)
		{
			s_projectAssetList[i].clear();
		}

		u32 resCount = 0;
		// Add the base game resources first.
		EditorResource* res = resources_getBaseGame(resCount);
		for (u32 i = 0; i < resCount; i++, res++)
		{
			assert(res->type == RES_ARCHIVE);
			addArchiveFiles(res->archive, gameId, res->name, ASRC_VANILLA);
		}
		// Then add external resources.
		res = resources_getExternal(resCount);
		for (u32 i = 0; i < resCount; i++, res++)
		{
			if (res->type == RES_ARCHIVE)
			{
				addArchiveFiles(res->archive, gameId, res->name, ASRC_EXTERNAL);
			}
			else if (res->type == RES_DIRECTORY)
			{
				addDirectoryFiles(res->path, gameId, ASRC_EXTERNAL);
			}
		}
		// Add resources from the mod itself (if loaded).
		Project* project = project_get();
		if (project->active)
		{
			addDirectoryFiles(project->path, gameId, ASRC_PROJECT);
		}
		// Then sort from Mod -> Resources -> Vanilla.
		sortAssetList();
	}

	bool isLevelTexture(const char* name)
	{
		if (s_viewInfo.levelSource < 0) { return true; }

		const size_t count = s_levelAssets[s_viewInfo.levelSource].textures.size();
		const std::string* texName = s_levelAssets[s_viewInfo.levelSource].textures.data();
		for (size_t i = 0; i < count; i++, texName++)
		{
			if (strcasecmp(name, texName->c_str()) == 0)
			{
				return true;
			}
		}
		return false;
	}

	bool isLevelFrame(const char* name)
	{
		if (s_viewInfo.levelSource < 0) { return true; }

		const size_t count = s_levelAssets[s_viewInfo.levelSource].frames.size();
		const std::string* frameName = s_levelAssets[s_viewInfo.levelSource].frames.data();
		for (size_t i = 0; i < count; i++, frameName++)
		{
			if (strcasecmp(name, frameName->c_str()) == 0)
			{
				return true;
			}
		}
		return false;
	}

	bool isLevelSprite(const char* name)
	{
		if (s_viewInfo.levelSource < 0) { return true; }

		const size_t count = s_levelAssets[s_viewInfo.levelSource].sprites.size();
		const std::string* spriteName = s_levelAssets[s_viewInfo.levelSource].sprites.data();
		for (size_t i = 0; i < count; i++, spriteName++)
		{
			if (strcasecmp(name, spriteName->c_str()) == 0)
			{
				return true;
			}
		}
		return false;
	}

	bool isLevel3D(const char* name)
	{
		if (s_viewInfo.levelSource < 0) { return true; }

		const size_t count = s_levelAssets[s_viewInfo.levelSource].pods.size();
		const std::string* podName = s_levelAssets[s_viewInfo.levelSource].pods.data();
		for (size_t i = 0; i < count; i++, podName++)
		{
			if (strcasecmp(name, podName->c_str()) == 0)
			{
				return true;
			}
		}
		return false;
	}

	bool isLevelPalette(const char* name)
	{
		if (s_viewInfo.levelSource < 0) { return true; }
		return strcasecmp(name, s_levelAssets[s_viewInfo.levelSource].paletteName.c_str()) == 0;
	}

	AssetHandle loadAssetData(const Asset* asset)
	{
		s32 palId = getAssetPalette(asset->name.c_str());
		AssetColorData colorData = { s_palettes[palId].data, nullptr, palId, 32 };
		return loadAssetData(asset->type, asset->archive, &colorData, asset->name.c_str());
	}

	void loadAsset(const Asset* projAsset)
	{
		// Filter out vanilla assets if desired.
		if ((projAsset->assetSource == ASRC_VANILLA) && resources_ignoreVanillaAssets()) { return; }

		Asset asset;
		asset.type = projAsset->type;
		asset.name = projAsset->name;
		asset.gameId = projAsset->gameId;
		asset.archive = projAsset->archive;
		asset.filePath = projAsset->filePath;
		asset.assetSource = projAsset->assetSource;
		s32 palId = getAssetPalette(projAsset->name.c_str());

		AssetColorData colorData = { s_palettes[palId].data, nullptr, palId, 32 };
		asset.handle = loadAssetData(projAsset->type, projAsset->archive, &colorData, projAsset->name.c_str());
		// For now allow stubbed types to squeak through...
		// TODO: Make this more strict once those are properly loaded.
		if (asset.handle != NULL_ASSET || asset.type == TYPE_3DOBJ || asset.type == TYPE_LEVEL)
		{
			s_viewAssetList.push_back(asset);
		}
	}
		
	// Returns true if it passes the filter.
	bool editorFilter(const char* name)
	{
		// No filter applied.
		if (s_viewInfo.assetFilter[0] == 0 || s_viewInfo.filterLen == 0) { return true; }

		// Make sure the name is at least as long as the filter.
		const size_t nameLength = strlen(name);
		if (nameLength < s_viewInfo.filterLen) { return false; }

		// See if there is a match...
		const size_t validLength = nameLength - s_viewInfo.filterLen + 1;
		for (size_t i = 0; i < validLength; i++)
		{
			if (editorStringFilter(&name[i], s_viewInfo.assetFilter, s_viewInfo.filterLen))
			{
				return true;
			}
		}
		return false;
	}

	const TFE_Editor::AssetList& getAssetList(AssetType type)
	{
		return s_projectAssetList[type];
	}

	void getLevelTextures(AssetList& list, const char* levelName)
	{
		list.clear();
		const u32 count = (u32)s_projectAssetList[TYPE_TEXTURE].size();
		const Asset* projAsset = s_projectAssetList[TYPE_TEXTURE].data();
		for (u32 i = 0; i < count; i++, projAsset++)
		{
			const char* name = projAsset->name.c_str();
			if (!isLevelTexture(name)) { continue; }
			
			Asset asset;
			asset.type = projAsset->type;
			asset.name = projAsset->name;
			asset.gameId = projAsset->gameId;
			asset.archive = projAsset->archive;
			asset.filePath = projAsset->filePath;
			asset.assetSource = projAsset->assetSource;
			s32 palId = getAssetPalette(projAsset->name.c_str());

			AssetColorData colorData = { s_palettes[palId].data, nullptr, palId, 32 };
			asset.handle = loadAssetData(projAsset->type, projAsset->archive, &colorData, projAsset->name.c_str());
			list.push_back(asset);
		}
	}

	void updateAssetList()
	{
		// Only Dark Forces for now.
		// TODO
		if (s_viewInfo.game != Game_Dark_Forces)
		{
			return;
		}
		Project* project = project_get();
		buildProjectAssetList(s_viewInfo.game);

		preprocessAssets();
		s_viewAssetList.clear();
		if (s_viewInfo.type == TYPE_TEXTURE)
		{
			const u32 count = (u32)s_projectAssetList[TYPE_TEXTURE].size();
			const Asset* projAsset = s_projectAssetList[TYPE_TEXTURE].data();
			for (u32 i = 0; i < count; i++, projAsset++)
			{
				const char* name = projAsset->name.c_str();
				if (!editorFilter(name) || !isLevelTexture(name)) { continue; }
				loadAsset(projAsset);
			}
		}
		else if (s_viewInfo.type == TYPE_EXT_TEXTURE)
		{
			const u32 count = (u32)s_projectAssetList[TYPE_EXT_TEXTURE].size();
			const Asset* projAsset = s_projectAssetList[TYPE_EXT_TEXTURE].data();
			for (u32 i = 0; i < count; i++, projAsset++)
			{
				const char* name = projAsset->name.c_str();
				if (!editorFilter(name) || !isLevelTexture(name)) { continue; }
				loadAsset(projAsset);
			}
		}
		else if (s_viewInfo.type == TYPE_FRAME)
		{
			const u32 count = (u32)s_projectAssetList[TYPE_FRAME].size();
			const Asset* projAsset = s_projectAssetList[TYPE_FRAME].data();
			for (u32 i = 0; i < count; i++, projAsset++)
			{
				const char* name = projAsset->name.c_str();
				if (!editorFilter(name) || !isLevelFrame(name)) { continue; }
				loadAsset(projAsset);
			}
		}
		else if (s_viewInfo.type == TYPE_SPRITE)
		{
			const u32 count = (u32)s_projectAssetList[TYPE_SPRITE].size();
			const Asset* projAsset = s_projectAssetList[TYPE_SPRITE].data();
			for (u32 i = 0; i < count; i++, projAsset++)
			{
				const char* name = projAsset->name.c_str();
				if (!editorFilter(name) || !isLevelSprite(name)) { continue; }
				loadAsset(projAsset);
			}
		}
		else if (s_viewInfo.type == TYPE_3DOBJ)
		{
			const u32 count = (u32)s_projectAssetList[TYPE_3DOBJ].size();
			const Asset* projAsset = s_projectAssetList[TYPE_3DOBJ].data();
			for (u32 i = 0; i < count; i++, projAsset++)
			{
				const char* name = projAsset->name.c_str();
				if (!editorFilter(name) || !isLevel3D(name)) { continue; }
				loadAsset(projAsset);
			}
		}
		else if (s_viewInfo.type == TYPE_LEVEL)
		{
			const u32 count = (u32)s_projectAssetList[TYPE_LEVEL].size();
			const Asset* projAsset = s_projectAssetList[TYPE_LEVEL].data();
			for (u32 i = 0; i < count; i++, projAsset++)
			{
				const char* name = projAsset->name.c_str();
				if (!editorFilter(name)) { continue; }

				// Only show levels from the project.
				if (project->active && s_viewInfo.showOnlyModLevels && projAsset->assetSource != ASRC_PROJECT)
				{
					continue;
				}

				loadAsset(projAsset);
			}
		}
		else if (s_viewInfo.type == TYPE_PALETTE)
		{
			const u32 count = (u32)s_projectAssetList[TYPE_PALETTE].size();
			const Asset* projAsset = s_projectAssetList[TYPE_PALETTE].data();
			for (u32 i = 0; i < count; i++, projAsset++)
			{
				const char* name = projAsset->name.c_str();
				if (!editorFilter(name) || !isLevelPalette(name)) { continue; }

				// Filter out vanilla assets if desired.
				if ((projAsset->assetSource == ASRC_VANILLA) && resources_ignoreVanillaAssets()) { continue; }

				Archive* archive = projAsset->archive;
				Asset asset;
				asset.type = projAsset->type;
				asset.name = projAsset->name;
				asset.gameId = projAsset->gameId;
				asset.archive = projAsset->archive;
				asset.filePath = projAsset->filePath;
				asset.assetSource = projAsset->assetSource;

				// Does it have a colormap too?
				// For now we assume the names match.
				char colorMapPath[256];
				FileUtil::replaceExtension(projAsset->name.c_str(), "CMP", colorMapPath);

				const size_t cmapCount = s_projectAssetList[TYPE_COLORMAP].size();
				const Asset* projAssetCM = s_projectAssetList[TYPE_COLORMAP].data();
				u8 colormapData[256 * 32];
				bool hasColormap = false;
				for (size_t i = 0; i < cmapCount; i++, projAssetCM++)
				{
					if (strcasecmp(projAssetCM->name.c_str(), colorMapPath) == 0)
					{
						Archive* cmapArchive = projAssetCM->archive;
						if (cmapArchive && cmapArchive->openFile(colorMapPath))
						{
							const size_t len = cmapArchive->getFileLength();
							cmapArchive->readFile(colormapData, min(8192u, (u32)len));
							cmapArchive->closeFile();
							hasColormap = true;
							break;
						}
					}
				}

				AssetColorData colorData = { nullptr, hasColormap ? colormapData : nullptr, 0, 32 };
				asset.handle = loadAssetData(TYPE_PALETTE, archive, &colorData, projAsset->name.c_str());
				s_viewAssetList.push_back(asset);
			}
		}
		else if (s_viewInfo.type == TYPE_SOUND)
		{
			const u32 count = (u32)s_projectAssetList[TYPE_SOUND].size();
			const Asset* projAsset = s_projectAssetList[TYPE_SOUND].data();
			for (u32 i = 0; i < count; i++, projAsset++)
			{
				const char* name = projAsset->name.c_str();
				if (!editorFilter(name)) { continue; }
				loadAsset(projAsset);
			}
		}
	}

	void writeGpuTextureAsPng(TextureGpu* tex, const char* writePath)
	{
		WorkBuffer& buffer = getWorkBuffer();
		buffer.resize(tex->getWidth() * tex->getHeight() * 4);
		tex->readCpu(buffer.data());
		TFE_Image::writeImage(writePath, tex->getWidth(), tex->getHeight(), (u32*)buffer.data());
	}

	void exportSelected()
	{
		if (!FileUtil::directoryExists(s_editorConfig.exportPath))
		{
			showMessageBox("ERROR", getErrorMsg(ERROR_INVALID_EXPORT_PATH), s_editorConfig.exportPath);
			return;
		}

		char path[TFE_MAX_PATH];
		strcpy(path, s_editorConfig.exportPath);
		size_t len = strlen(path);
		if (path[len - 1] != '/' && path[len - 1] != '\\')
		{
			path[len] = '/';
			path[len + 1] = 0;
		}
		FileUtil::fixupPath(path);

		const char* assetSubPath[] =
		{
			"Textures",    // TYPE_TEXTURE
			"Sounds",	   // TYPE_SOUND
			"Sprites",     // TYPE_SPRITE
			"Frames",      // TYPE_FRAME
			"Models",      // TYPE_3DOBJ
			"Levels",      // TYPE_LEVEL
			"Palettes",    // TYPE_PALETTE
		};

		const s32 count = (s32)s_selected.size();
		const s32* index = s_selected.data();
		for (s32 i = 0; i < count; i++)
		{
			Asset* asset = &s_viewAssetList[index[i]];

			char subDir[TFE_MAX_PATH];
			sprintf(subDir, "%s%s", path, assetSubPath[asset->type]);
			if (!FileUtil::directoryExists(subDir))
			{
				FileUtil::makeDirectory(subDir);
			}

			// Read the full contents.
			Archive* archive = asset->archive;
			if (!archive) { continue; }
			if (!archive->openFile(asset->name.c_str())) { continue;}

			WorkBuffer buffer;
			size_t len = archive->getFileLength();
			buffer.resize(len);
			archive->readFile(buffer.data(), len);
			archive->closeFile();

			char fullPath[TFE_MAX_PATH];
			sprintf(fullPath, "%s/%s", subDir, asset->name.c_str());
			FileStream outFile;
			if (outFile.open(fullPath, FileStream::MODE_WRITE))
			{
				outFile.writeBuffer(buffer.data(), (u32)len);
				outFile.close();
			}

			char pngFile[TFE_MAX_PATH];
			if (asset->type == TYPE_LEVEL)
			{
				char objPath[TFE_MAX_PATH];
				char infPath[TFE_MAX_PATH];
				FileUtil::replaceExtension(fullPath, "O", objPath);
				FileUtil::replaceExtension(fullPath, "INF", infPath);

				char assetObj[TFE_MAX_PATH];
				char assetInf[TFE_MAX_PATH];
				FileUtil::replaceExtension(asset->name.c_str(), "O", assetObj);
				FileUtil::replaceExtension(asset->name.c_str(), "INF", assetInf);
				if (archive->openFile(assetObj))
				{ 
					len = archive->getFileLength();
					buffer.resize(len);
					archive->readFile(buffer.data(), len);
					archive->closeFile();

					FileStream outFile;
					if (outFile.open(objPath, FileStream::MODE_WRITE))
					{
						outFile.writeBuffer(buffer.data(), (u32)len);
						outFile.close();
					}
				}
				if (archive->openFile(assetInf))
				{
					len = archive->getFileLength();
					buffer.resize(len);
					archive->readFile(buffer.data(), len);
					archive->closeFile();

					FileStream outFile;
					if (outFile.open(infPath, FileStream::MODE_WRITE))
					{
						outFile.writeBuffer(buffer.data(), (u32)len);
						outFile.close();
					}
				}
			}
			if (asset->type == TYPE_TEXTURE)
			{
				EditorTexture* texture = (EditorTexture*)getAssetData(asset->handle);
				if (texture && texture->frameCount == 1)
				{
					FileUtil::replaceExtension(fullPath, "PNG", pngFile);
					writeGpuTextureAsPng(texture->frames[0], pngFile);
				}
				else if (texture && texture->frameCount > 1)
				{
					FileUtil::stripExtension(fullPath, pngFile);
					for (u32 f = 0; f < texture->frameCount; f++)
					{
						sprintf(fullPath, "%s_%d.png", pngFile, f);
						writeGpuTextureAsPng(texture->frames[f], fullPath);
					}
				}
			}
			else if (asset->type == TYPE_FRAME)
			{
				EditorFrame* frame = (EditorFrame*)getAssetData(asset->handle);
				FileUtil::replaceExtension(fullPath, "PNG", pngFile);
				writeGpuTextureAsPng(frame->texGpu, pngFile);
			}
			else if (asset->type == TYPE_SPRITE)
			{
				EditorSprite* sprite = (EditorSprite*)getAssetData(asset->handle);
				FileUtil::replaceExtension(fullPath, "PNG", pngFile);
				writeGpuTextureAsPng(sprite->texGpu, pngFile);
			}
		}
	}

	bool readWriteFile(const char* outFilePath, const char* assetName, Archive* archive, WorkBuffer& buffer)
	{
		if (!archive->openFile(assetName)) { return false; }

		bool success = false;
		size_t len = archive->getFileLength();
		buffer.resize(len);
		archive->readFile(buffer.data(), len);
		archive->closeFile();

		FileStream outFile;
		if (outFile.open(outFilePath, FileStream::MODE_WRITE))
		{
			outFile.writeBuffer(buffer.data(), (u32)len);
			outFile.close();
			success = true;
		}
		return success;
	}

	void importSelected()
	{
		if (!FileUtil::directoryExists(s_editorConfig.editorPath))
		{
			showMessageBox("ERROR", getErrorMsg(ERROR_INVALID_EXPORT_PATH), s_editorConfig.editorPath);
			return;
		}

		Project* project = project_get();
		if (!project || !project->active) { return; }

		char tmpDir[TFE_MAX_PATH];
		getTempDirectory(tmpDir);

		const s32 count = (s32)s_selected.size();
		const s32* index = s_selected.data();
		for (s32 i = 0; i < count; i++)
		{
			Asset* asset = &s_viewAssetList[index[i]];
			if (asset->type != TYPE_LEVEL || asset->assetSource == ASRC_PROJECT) { continue; }

			char levPath[TFE_MAX_PATH];
			sprintf(levPath, "%s/%s", tmpDir, asset->name.c_str());

			// Read the full contents.
			Archive* archive = asset->archive;
			if (!archive) { continue; }

			// Write files to the temp directory.
			WorkBuffer buffer;
			if (!readWriteFile(levPath, asset->name.c_str(), archive, buffer)) { continue; }

			char objPath[TFE_MAX_PATH];
			char infPath[TFE_MAX_PATH];
			FileUtil::replaceExtension(levPath, "O", objPath);
			FileUtil::replaceExtension(levPath, "INF", infPath);

			char assetObj[TFE_MAX_PATH];
			char assetInf[TFE_MAX_PATH];
			FileUtil::replaceExtension(asset->name.c_str(), "O", assetObj);
			FileUtil::replaceExtension(asset->name.c_str(), "INF", assetInf);

			// These are mostly optional, so we continue even if they don't exist.
			readWriteFile(objPath, assetObj, archive, buffer);
			readWriteFile(infPath, assetInf, archive, buffer);

			// Then copy them into the project directory.
			char dstLevPath[TFE_MAX_PATH];
			char dstObjPath[TFE_MAX_PATH];
			char dstInfPath[TFE_MAX_PATH];
			sprintf(dstLevPath, "%s/%s", project->path, asset->name.c_str());
			sprintf(dstObjPath, "%s/%s", project->path, assetObj);
			sprintf(dstInfPath, "%s/%s", project->path, assetInf);

			FileUtil::copyFile(levPath, dstLevPath);
			FileUtil::copyFile(objPath, dstObjPath);
			FileUtil::copyFile(infPath, dstInfPath);
		}

		if (count) { resources_dirty(); }
	}
}