#include "editorAsset.h"
#include <TFE_Editor/editor.h>
#include <TFE_System/system.h>
#include <TFE_Asset/spriteAsset_Jedi.h>
#include <TFE_DarkForces/mission.h>
#include <TFE_System/system.h>
#include <TFE_Archive/archive.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Renderer/rcommon.h>
#include <algorithm>
#include <vector>
#include <map>

namespace TFE_Editor
{
	typedef std::map<std::string, AssetHandle> AssetMap;
	enum AssetConst
	{
		ASSET_TYPE_SHIFT = 32,
		ASSET_TYPE_BITS  = 16,
		ASSET_ACTIVE_BIT = 63
	};
	static AssetMap s_assetsLoaded;

	//////////////////////////////////////////
	// Forward Declarations
	//////////////////////////////////////////
	AssetHandle findAsset(const char* name);
	AssetHandle buildHandle(AssetType type, u32 index);
	void addAsset(AssetHandle handle, const char* name);
	bool unpackHandle(AssetHandle handle, AssetType& type, u32& index);
		
	//////////////////////////////////////////
	// API Implementation
	//////////////////////////////////////////
	// Load the full asset data.
	AssetHandle loadAssetData(AssetType type, Archive* archive, const AssetColorData* colorData, const char* name, const char* filePath = "")
	{
		// First check if the asset has already been loaded.
		AssetHandle handle = findAsset(name);
		if (handle != NULL_ASSET)
		{
			return handle;
		}

		// If not, then load a new asset.
		s32 index = -1;
		switch (type)
		{
			case TYPE_TEXTURE:
			{
				index = loadEditorTexture(TEX_BM, archive, name, filePath, colorData->palette, colorData->palIndex);
			} break;
			case TYPE_EXT_TEXTURE:
			{
				index = loadEditorTexture(TEX_RAW, archive, name, filePath, nullptr, 0);
			} break;
			case TYPE_PALETTE:
			{
				index = loadPaletteAsTexture(archive, name, filePath, colorData->colormap);
			} break;
			case TYPE_FRAME:
			{
				index = loadEditorFrame(FRAME_FME, archive, name, filePath, colorData->palette, colorData->palIndex);
			} break;
			case TYPE_SPRITE:
			{
				index = loadEditorSprite(SPRITE_WAX, archive, name, filePath, colorData->palette, colorData->palIndex);
			} break;
			case TYPE_3DOBJ:
			{
				index = loadEditorObj3D(OBJ3D_3DO, archive, name, filePath, colorData->palette, colorData->palIndex);
			} break;
			case TYPE_LEVEL:
			{
				index = loadEditorLevelPreview(LEV_LEV, archive, name);
			} break;
			case TYPE_SOUND:
			{
				index = loadEditorSound(SOUND_VOC, archive, name, filePath);
			} break;
			case TYPE_EXT_FRAME: // todo
			case TYPE_EXT_WAX: // todo
			default:
			{
				TFE_System::logWrite(LOG_WARNING, "Editor", "Asset data loading not implemented, or bad type: %d", type);
			}
		}

		if (index >= 0)
		{
			handle = buildHandle(type, index);
			addAsset(handle, name);
		}
		return handle;
	}

	// Reload the full asset data.
	void reloadAssetData(AssetHandle handle, Archive* archive, const AssetColorData* colorData)
	{
		if (!handle) { return; }

		AssetType type;
		u32 index = 0;
		if (!unpackHandle(handle, type, index))
		{
			return;
		}

		switch (type)
		{
			case TYPE_TEXTURE:
			{
				loadEditorTextureLit(TEX_BM, archive, colorData->palette, colorData->palIndex, colorData->colormap, colorData->lightLevel, index);
			} break;
			case TYPE_PALETTE:
			case TYPE_3DOBJ:
			case TYPE_LEVEL:
			case TYPE_SOUND:
			case TYPE_EXT_TEXTURE:
			case TYPE_EXT_FRAME:
			case TYPE_EXT_WAX:
			{
				// This doesn't make sense for these asset types, so do nothing.
			} break;
			case TYPE_FRAME:
			{
				loadEditorFrameLit(FRAME_FME, archive, colorData->palette, colorData->palIndex, colorData->colormap, colorData->lightLevel, index);
			} break;
			case TYPE_SPRITE:
			{
				loadEditorSpriteLit(SPRITE_WAX, archive, colorData->palette, colorData->palIndex, colorData->colormap, colorData->lightLevel, index);
			} break;
			default:
			{
				TFE_System::logWrite(LOG_WARNING, "Editor", "Asset data loading not implemented, or bad type: %d", type);
			}
		}
	}

	// Free asset data.
	void freeAssetData(AssetHandle)
	{
	}

	// Get a pointer to the asset data.
	void* getAssetData(AssetHandle handle)
	{
		AssetType type;
		u32 index = 0;
		void* data = nullptr;
		if (!unpackHandle(handle, type, index))
		{
			return data;
		}

		switch (type)
		{
			case TYPE_TEXTURE:
			case TYPE_EXT_TEXTURE:
			case TYPE_PALETTE:
			{
				data = getTextureData(index);
			} break;
			case TYPE_FRAME:
			{
				data = getFrameData(index);
			} break;
			case TYPE_SPRITE:
			{
				data = getSpriteData(index);
			} break;
			case TYPE_3DOBJ:
			{
				data = getObj3DData(index);
			} break;
			case TYPE_LEVEL:
			{
				data = getLevelPreviewData(index);
			} break;
			case TYPE_SOUND:
			{
				data = getSoundData(index);
			} break;
			default:
			{
				TFE_System::logWrite(LOG_WARNING, "Editor", "Asset data loading not implemented, or bad type: %d", type);
			}
		}
		return data;
	}
		
	void freeAllAssetData()
	{
		freeCachedTextures();
		freeCachedFrames();
		freeCachedSprites();
		freeCachedSounds();
		freeCachedObj3D();
		freeCachedLevelPreview();
		s_assetsLoaded.clear();
	}

	void freeAllThumbnails()
	{
	}

	//////////////////////////////////////////
	// Internal
	//////////////////////////////////////////
	AssetHandle findAsset(const char* name)
	{
		AssetMap::iterator iAsset = s_assetsLoaded.find(name);
		if (iAsset != s_assetsLoaded.end())
		{
			return iAsset->second;
		}
		return NULL_ASSET;
	}

	void addAsset(AssetHandle handle, const char* name)
	{
		s_assetsLoaded[name] = handle;
	}

	AssetHandle buildHandle(AssetType type, u32 index)
	{
		AssetHandle handle = u64(index) | (u64(type) << u64(32)) | (u64(1) << u64(ASSET_ACTIVE_BIT));
		return handle;
	}

	bool unpackHandle(AssetHandle handle, AssetType& type, u32& index)
	{
		if (handle == NULL_ASSET) { return false; }
		index = u32(handle & ((u64(1) << ASSET_TYPE_SHIFT) - u64(1)));
		type = AssetType((handle >> u64(ASSET_TYPE_SHIFT)) & ((u64(1) << ASSET_TYPE_BITS) - u64(1)));
		return true;
	}
}