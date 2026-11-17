#pragma once
//////////////////////////////////////////////////////////////////////
// The Force Engine Editor
// A system built to view and edit Dark Forces data files.
// The viewing aspect needs to be put in place at the beginning
// in order to properly test elements in isolation without having
// to "play" the game as intended.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_Editor/editor.h>
#include <TFE_Editor/EditorAsset/editorAsset.h>

namespace AssetBrowser
{
	void init();
	void destroy();

	void update();
	void render();

	void selectAll();
	void selectNone();
	void invertSelection();
	const char* getSelectedAssetName();

	bool showOnlyModLevels();
	void rebuildAssets();

	void initPopup(TFE_Editor::AssetType type, const char* selectName);
	bool popup();

	TFE_Editor::Asset* findAsset(const char* name, TFE_Editor::AssetType type);
	TFE_Editor::AssetHandle loadAssetData(const TFE_Editor::Asset* asset);
	void getLevelTextures(TFE_Editor::AssetList& list, const char* levelName);
	const TFE_Editor::AssetList& getAssetList(TFE_Editor::AssetType type);

	void setAssetLevel(const char* name, u32 levelIndex);
	bool isAssetInLevel(const char* name, u32 levelIndex);

	bool readWriteFile(const char* outFilePath, const char* assetName, const char* assetPath, Archive* archive, TFE_Editor::WorkBuffer& buffer);
	void setBrowseLevels(bool value);
}
