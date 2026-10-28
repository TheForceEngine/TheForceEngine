#pragma once
//////////////////////////////////////////////////////////////////////
// The Force Engine Editor
// A system built to view and edit Dark Forces data files.
// The viewing aspect needs to be put in place at the beginning
// in order to properly test elements in isolation without having
// to "play" the game as intended.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_Archive/archive.h>
#include <TFE_Game/igame.h>
#include "editorColormap.h"
#include "editorFrame.h"
#include "editorSound.h"
#include "editorSprite.h"
#include "editorTexture.h"
#include "editorObj3D.h"
#include "editorLevelPreview.h"
#include <vector>

namespace TFE_Editor
{
	enum AssetType : s32
	{
		TYPE_TEXTURE = 0,
		TYPE_SOUND,
		TYPE_SPRITE,
		TYPE_FRAME,
		TYPE_3DOBJ,
		TYPE_LEVEL,
		TYPE_PALETTE,
		TYPE_EXT_FRAME,
		TYPE_EXT_WAX,
		TYPE_EXT_TEXTURE,
		TYPE_COLORMAP,
		TYPE_COUNT,
		TYPE_NOT_SET = TYPE_COUNT
	};

	enum AssetSource
	{
		ASRC_VANILLA = 0,
		ASRC_EXTERNAL,
		ASRC_PROJECT,
		ASRC_COUNT
	};

	#define NULL_ASSET TFE_Editor::AssetHandle(0)

	static const char* c_assetType[] =
	{
		"Texture",    // TYPE_TEXTURE
		"Sound",	  // TYPE_SOUND
		"Sprite",     // TYPE_SPRITE
		"Frame",      // TYPE_FRAME
		"3D Object",  // TYPE_3DOBJ
		"Level",      // TYPE_LEVEL
		"Palette",    // TYPE_PALETTE
		"HD Frame",	  // TYPE_EXT_FRAME
		"HD Sprite",  // TYPE_EXT_WAX
		"HD Texture", // TYPE_EXT_TEXTURE
	};

	typedef u64 AssetHandle;

	struct AssetColorData
	{
		const u32* palette;
		const u8* colormap;
		s32 palIndex;
		s32 lightLevel;
	};

	struct Asset
	{
		AssetType type;
		GameID gameId;
		Archive* archive;

		std::string name;
		std::string filePath;

		AssetSource assetSource;
		AssetHandle handle;
	};
	typedef std::vector<Asset> AssetList;
		
	AssetHandle loadAssetData(AssetType type, Archive* archive, const AssetColorData* colorData, const char* name, const char* filePath);
	AssetHandle loadAssetData(const Asset* asset);
	void  reloadAssetData(AssetHandle handle, Archive* archive, const AssetColorData* colorData);
	void  freeAssetData(AssetHandle handle);
	void* getAssetData(AssetHandle handle);

	void freeAllAssetData();
	void freeAllThumbnails();
}
