#include "levelEditorData.h"
#include "levelDataSnapshot.h"
#include "levelEditorHistory.h"
#include "levelEditor.h"
#include "editGeometry.h"
#include "selection.h"
#include "entity.h"
#include "error.h"
#include "shell.h"
#include "levelEditorInf.h"
#include "sharedState.h"
#include "guidelines.h"
#include <TFE_Editor/snapshotReaderWriter.h>
#include <TFE_Editor/history.h>
#include <TFE_Editor/errorMessages.h>
#include <TFE_Editor/editorConfig.h>
#include <TFE_Editor/editorLevel.h>
#include <TFE_Editor/editorProject.h>
#include <TFE_Editor/editorResources.h>
#include <TFE_Editor/editor.h>
#include <TFE_Editor/editorMath.h>
#include <TFE_Editor/EditorAsset/editorAsset.h>
#include <TFE_Editor/EditorAsset/editorTexture.h>
#include <TFE_Editor/EditorAsset/editorFrame.h>
#include <TFE_Editor/EditorAsset/editorSprite.h>
#include <TFE_Editor/AssetBrowser/assetBrowser.h>
#include <TFE_Editor/LevelEditor/Rendering/grid.h>
#include <TFE_Archive/zipArchive.h>
#include <TFE_Jedi/Level/rwall.h>
#include <TFE_Jedi/Level/rsector.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Level/levelData.h>
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
#include <TFE_System/math.h>
#include <TFE_Ui/ui.h>

#include <climits>
#include <algorithm>
#include <vector>
#include <string>
#include <map>
#include <set>

using namespace TFE_Editor;
using namespace TFE_Jedi;

namespace TFE_DarkForces
{
	extern char s_colormapName[TFE_MAX_PATH];
	extern u8* s_levelColorMap;
	extern void mission_loadColormap();
}

namespace LevelEditor
{
	const char* c_newLine = "\r\n";
	const f32 adjoinEps = 0.0001f;

	// Sector attributes, geometry not included.
	struct SectorAttrib
	{
		u32 groupId = 0;
		f32 floorHeight = 0.0f;
		f32 ceilHeight = 0.0f;
		f32 secHeight = 0.0f;

		u32 ambient = 0;
		s32 layer = 0;
		u32 flags[3] = { 0 };

		LevelTexture floorTex = {};
		LevelTexture ceilTex = {};
	};
	std::vector<s32> s_sectorIds;

	std::vector<EditorSector> s_sectorSnapshot;
	std::vector<UniqueTexture> s_uniqueTextures;
	std::vector<UniqueEntity> s_uniqueEntities;
	
	std::vector<u8> s_fileData;
	std::vector<IndexPair> s_pairs;
	std::vector<IndexPair> s_prevPairs;
	SelectionList s_featureList;
	static u32* s_palette = nullptr;
	static s32 s_palIndex = 0;

	static s32 s_curSnapshotId = -1;
	static EditorLevel s_curSnapshot;

	EditorLevel s_level = {};

	extern AssetList s_levelTextureList;
	extern u32 s_viewFrame;

	enum Constants
	{
		LevVersionMin = 15,
		LevVersionMax = 21,
		LevVersion_Layers_WallLight = 21,
	};
	
	EditorSector* findSectorDf(const Vec3f pos);
	EditorSector* findSectorDf(const Vec2f pos);
	void gatherAssetList(const char* name, const char* levFile, const char* objFile, std::vector<Asset*>& assetList);

	AssetHandle loadTexture(const char* bmTextureName)
	{
		Asset* texAsset = AssetBrowser::findAsset(bmTextureName, TYPE_TEXTURE);
		if (!texAsset) { return NULL_ASSET; }
		return AssetBrowser::loadAssetData(texAsset);
	}
		
	AssetHandle loadPalette(const char* paletteName)
	{
		Asset* palAsset = AssetBrowser::findAsset(paletteName, TYPE_PALETTE);
		if (!palAsset) { return NULL_ASSET; }
		return AssetBrowser::loadAssetData(palAsset);
	}

	AssetHandle loadColormap(const char* colormapName)
	{
		Asset* colormapAsset = AssetBrowser::findAsset(colormapName, TYPE_COLORMAP);
		if (!colormapAsset) { return NULL_ASSET; }
		return AssetBrowser::loadAssetData(colormapAsset);
	}

	bool editor_objectParseSeq(Entity* entity, TFE_Parser* parser, size_t* bufferPos)
	{
		const char* line = parser->readLine(*bufferPos);
		if (!line || !strstr(line, "SEQ"))
		{
			return false;
		}

		EntityLogic* newLogic = nullptr;
		char objSeqArg0[256], objSeqArg1[256], objSeqArg2[256], objSeqArg3[256], objSeqArg4[256], objSeqArg5[256];
		while (1)
		{
			line = parser->readLine(*bufferPos);
			if (!line) { return false; }

			objSeqArg2[0] = 0;
			s32 objSeqArgCount = sscanf(line, " %s %s %s %s %s %s", objSeqArg0, objSeqArg1, objSeqArg2, objSeqArg3, objSeqArg4, objSeqArg5);
			KEYWORD key = getKeywordIndex(objSeqArg0);
			if (key == KW_TYPE || key == KW_LOGIC)
			{
				entity->logic.push_back({});
				newLogic = &entity->logic.back();

				KEYWORD logicId = getKeywordIndex(objSeqArg1);
				if (logicId == KW_PLAYER)  // Player Logic.
				{
					newLogic->name = s_logicDefList[0].name;
				}
				else if (logicId == KW_ANIM)	// Animated Sprites Logic.
				{
					newLogic->name = s_logicDefList[1].name;
				}
				else if (logicId == KW_UPDATE)	// "Update" logic is usually used for rotating 3D objects, like the Death Star.
				{
					newLogic->name = s_logicDefList[2].name;
				}
				else if (logicId >= KW_TROOP && logicId <= KW_SCENERY)	// Enemies, explosives barrels, land mines, and scenery.
				{
					newLogic->name = s_logicDefList[3 + logicId - KW_TROOP].name;
				}
				else if (logicId == KW_KEY)         // Vue animation logic.
				{
					newLogic->name = "Key"; // 38
				}
				else if (logicId == KW_GENERATOR)	// Enemy generator, used for in-level enemy spawning.
				{
					newLogic->name = "Generator"; // 39
					// Add a variable for the type.
					EntityVar genType;
					genType.defId = getVariableId("GenType");
					genType.value.sValue = objSeqArg2;
					newLogic->var.push_back(genType);
				}
				else if (logicId == KW_DISPATCH)
				{
					newLogic->name = "Dispatch"; // 40
				}
				else if ((logicId >= KW_BATTERY && logicId <= KW_AUTOGUN) || logicId == KW_ITEM)
				{
					if (logicId >= KW_BATTERY && logicId <= KW_AUTOGUN)
					{
						strcpy(objSeqArg2, objSeqArg1);
					}
					size_t len = strlen(objSeqArg2);
					for (size_t i = 1; i < len; i++)
					{
						objSeqArg2[i] = tolower(objSeqArg2[i]);
					}
					newLogic->name = objSeqArg2;
				}
			}
			else if (key == KW_SEQEND)
			{
				return true;
			}
			else // Variable?
			{
				char varName[256];
				strcpy(varName, objSeqArg0);
				s32 len = (s32)strlen(varName);
				while (varName[len - 1] == ':')
				{
					varName[len - 1] = 0;
					len--;
				}

				const s32 varId = getVariableId(varName);
				if (varId >= 0)
				{
					const EntityVarDef* def = getEntityVar(varId);
					EntityVar* var = nullptr;
					if (newLogic)
					{
						newLogic->var.push_back({});
						var = &newLogic->var.back();
					}
					else
					{
						entity->var.push_back({});
						var = &entity->var.back();
					}
					var->defId = varId;
					
					TokenList tokens;
					tokens.push_back(varName);
					tokens.push_back(objSeqArg1);
					tokens.push_back(objSeqArg2);
					parseValue(tokens, def->type, &var->value);
				}
			}
		}

		return true;
	}

	void entityNameFromAssetName(const char* assetName, char* name)
	{
		FileUtil::stripExtension(assetName, name);
		size_t len = strlen(name);
		for (size_t i = 1; i < len; i++)
		{
			name[i] = tolower(name[i]);
		}
	}
		
	bool loadLevelObjFromAsset(const Asset* asset)
	{
		char objFile[TFE_MAX_PATH];
		s_fileData.clear();
		if (asset->archive)
		{
			FileUtil::replaceExtension(asset->name.c_str(), "O", objFile);

			if (asset->archive->openFile(objFile))
			{
				const size_t len = asset->archive->getFileLength();
				s_fileData.resize(len);
				asset->archive->readFile(s_fileData.data(), len);
				asset->archive->closeFile();
			}
		}
		else
		{
			FileUtil::replaceExtension(asset->filePath.c_str(), "O", objFile);

			FileStream file;
			if (file.open(objFile, Stream::MODE_READ))
			{
				const size_t len = file.getSize();
				s_fileData.resize(len);
				file.readBuffer(s_fileData.data(), (u32)len);
				file.close();
			}
		}

		if (s_fileData.empty()) { return false; }
		TFE_Parser parser;
		size_t bufferPos = 0;
		parser.init((char*)s_fileData.data(), s_fileData.size());
		parser.addCommentString("#");
		parser.convertToUpperCase(true);

		const char* line;
		line = parser.readLine(bufferPos);
		s32 versionMajor, versionMinor;
		if (sscanf(line, "O %d.%d", &versionMajor, &versionMinor) != 2)
		{
			LE_WARNING("Cannot open existing DF level object file '%s'", objFile);
			return false;
		}

		std::vector<std::string> pods;
		std::vector<std::string> sprites;
		std::vector<std::string> frames;
		std::vector<std::string> sounds;
		s32 podCount = 0;
		s32 spriteCount = 0;
		s32 frameCount = 0;
		s32 soundCount = 0;
		s32 objectCount = 0;

		while ((line = parser.readLine(bufferPos)) != nullptr)
		{
			if (sscanf(line, "PODS %d", &podCount) == 1)
			{
				pods.reserve(podCount);
				for (s32 p = 0; p < podCount; p++)
				{
					line = parser.readLine(bufferPos);
					if (line)
					{
						char podName[32] = "default.3do";
						sscanf(line, " POD: %s ", podName);
						pods.push_back(podName);
					}
				}
			}
			else if (sscanf(line, "SPRS %d", &spriteCount) == 1)
			{
				sprites.reserve(spriteCount);
				for (s32 s = 0; s < spriteCount; s++)
				{
					line = parser.readLine(bufferPos);
					if (line)
					{
						char spriteName[32] = "default.wax";
						sscanf(line, " SPR: %s ", spriteName);
						sprites.push_back(spriteName);
					}
				}
			}
			else if (sscanf(line, "FMES %d", &frameCount) == 1)
			{
				frames.reserve(frameCount);
				for (s32 f = 0; f < frameCount; f++)
				{
					line = parser.readLine(bufferPos);
					if (line)
					{
						char frameName[32] = "default.fme";
						sscanf(line, " FME: %s ", frameName);
						frames.push_back(frameName);
					}
				}
			}
			else if (sscanf(line, "SOUNDS %d", &soundCount) == 1)
			{
				sounds.reserve(soundCount);
				for (s32 s = 0; s < soundCount; s++)
				{
					line = parser.readLine(bufferPos);
					if (line)
					{
						char soundName[32] = "";
						sscanf(line, " SOUND: %s ", soundName);
						sounds.push_back(soundName);
					}
				}
			}
			else if (sscanf(line, "OBJECTS %d", &objectCount) == 1)
			{
				bool readNextLine = true;
				for (s32 objIndex = 0; objIndex < objectCount;)
				{
					if (readNextLine)
					{
						line = parser.readLine(bufferPos);
						if (!line) { break; }
					}
					else
					{
						readNextLine = true;
					}

					s32 objDiff = 0;
					s32 dataIndex = 0;
					f32 x, y, z, pch, yaw, rol;
					char objClass[32];

					if (sscanf(line, " CLASS: %s DATA: %d X: %f Y: %f Z: %f PCH: %f YAW: %f ROL: %f DIFF: %d", objClass, &dataIndex, &x, &y, &z, &pch, &yaw, &rol, &objDiff) > 5)
					{
						objIndex++;
						Vec3f pos = { x, -y, z };
						EditorSector* sector = findSectorDf(pos);
						if (!sector)
						{
							continue;
						}

						sector->obj.push_back({});
						EditorObject* obj = &sector->obj.back();
						obj->pos = pos;
						obj->diff = objDiff;
						obj->angle = yaw * PI / 180.0f;
						obj->pitch = pch * PI / 180.0f;
						obj->roll  = rol * PI / 180.0f;
						compute3x3Rotation(&obj->transform, obj->angle, obj->pitch, obj->roll);

						Entity objEntity = {};

						KEYWORD classType = getKeywordIndex(objClass);
						switch (classType)
						{
							case KW_3D:
							{
								if (podCount)
								{
									objEntity.type = ETYPE_3D;
									objEntity.assetName = pods[dataIndex];

									char name[256];
									entityNameFromAssetName(objEntity.assetName.c_str(), name);
									objEntity.name = name;
								}
							} break;
							case KW_SPRITE:
							{
								if (spriteCount)
								{
									objEntity.type = ETYPE_SPRITE;
									objEntity.assetName = sprites[dataIndex];
									
									char name[256];
									entityNameFromAssetName(objEntity.assetName.c_str(), name);
									objEntity.name = name;
								}
							} break;
							case KW_FRAME:
							{
								if (frameCount)
								{
									objEntity.type = ETYPE_FRAME;
									objEntity.assetName = frames[dataIndex];

									char name[256];
									entityNameFromAssetName(objEntity.assetName.c_str(), name);
									objEntity.name = name;
								}
							} break;
							case KW_SPIRIT:
							{
								objEntity.name = "Spirit";
								objEntity.type = ETYPE_SPIRIT;
								objEntity.assetName = "SpiritObject.png";
							} break;
							case KW_SOUND:
							{
								//objEntity.type = ETYPE_SOUND;
								//objEntity.assetName = "SpiritObject.png";
							} break;
							case KW_SAFE:
							{
								objEntity.name = "Safe";
								objEntity.type = ETYPE_SAFE;
								objEntity.assetName = "SafeObject.png";
							} break;
						}

						// Invalid or unknown object or object type, just discard.
						if (objEntity.type == ETYPE_UNKNOWN)
						{
							sector->obj.pop_back();
							continue;
						}

						readNextLine = editor_objectParseSeq(&objEntity, &parser, &bufferPos);

						// Does this entity exist as a loaded definition? If so, take that name.
						const s32 editorDefId = getEntityDefId(&objEntity);
						if (editorDefId >= 0)
						{
							objEntity = s_entityDefList[editorDefId];
						}
						obj->entityId = addEntityToLevel(&objEntity);
					}
				}
			}
		}

		const s32 entityCount = (s32)s_level.entities.size();
		Entity* entity = s_level.entities.data();
		for (s32 i = 0; i < entityCount; i++, entity++)
		{
			loadSingleEntityData(entity);
		}

		return true;
	}

	void levelClear()
	{
		// Clear the INF data.
		s_levelInf.item.clear();
		s_levelInf.elevator.clear();
		s_levelInf.teleport.clear();
		s_levelInf.trigger.clear();

		// Clear selection state.
		selection_clear();
		selection_clearHovered();
		s_featureTex = {};

		// Clear notes.
		s_level.notes.clear();
		levelSetClean();
	}
		
	bool loadLevelFromAsset(const Asset* asset)
	{
		EditorLevel* level = &s_level;
		char slotName[256];
		FileUtil::stripExtension(asset->name.c_str(), slotName);

		// Clear the INF data.
		s_levelInf.item.clear();
		s_levelInf.elevator.clear();
		s_levelInf.teleport.clear();
		s_levelInf.trigger.clear();

		// Clear selection state.
		selection_clear();
		selection_clearHovered();
		s_featureTex = {};

		// Clear notes.
		s_level.notes.clear();
		levelSetClean();

		// First check to see if there is a "tfl" version of the level.
		if (loadFromTFL(slotName))
		{
			return true;
		}
		level->slot = slotName;

		s_fileData.clear();
		if (asset->archive)
		{
			if (asset->archive->openFile(asset->name.c_str()))
			{
				const size_t len = asset->archive->getFileLength();
				s_fileData.resize(len);
				asset->archive->readFile(s_fileData.data(), len);
				asset->archive->closeFile();
			}
		}
		else
		{
			FileStream file;
			if (file.open(asset->filePath.c_str(), Stream::MODE_READ))
			{
				const size_t len = file.getSize();
				s_fileData.resize(len);
				file.readBuffer(s_fileData.data(), (u32)len);
				file.close();
			}
		}

		if (s_fileData.empty()) { return false; }

		TFE_Parser parser;
		size_t bufferPos = 0;
		parser.init((char*)s_fileData.data(), s_fileData.size());
		parser.addCommentString("#");
		parser.convertToUpperCase(true);

		const char* line;
		line = parser.readLine(bufferPos);
		s32 versionMajor, versionMinor;
		if (sscanf(line, " LEV %d.%d", &versionMajor, &versionMinor) != 2)
		{
			return false;
		}
		// Check the version number. The editor actually supports a range of versions.
		s32 version = versionMajor * 10 + versionMinor;
		if (version < LevVersionMin || version > LevVersionMax)
		{
			return false;
		}

		char readBuffer[256];
		line = parser.readLine(bufferPos);
		if (sscanf(line, " LEVELNAME %s", readBuffer) != 1)
		{
			return false;
		}
		level->name = readBuffer;
		
		line = parser.readLine(bufferPos);
		if (sscanf(line, " PALETTE %s", readBuffer) != 1)
		{
			return false;
		}
		// Fixup the palette, strip any path.
		char filename[TFE_MAX_PATH];
		FileUtil::getFileNameFromPath(readBuffer, filename, true);
		level->palette = filename;

		// Another value that is ignored.
		line = parser.readLine(bufferPos);
		if (sscanf(line, " MUSIC %s", readBuffer) == 1)
		{
			line = parser.readLine(bufferPos);
		}

		// Sky Parallax - this option until version 1.9, so handle its absence.
		if (sscanf(line, " PARALLAX %f %f", &level->parallax.x, &level->parallax.z) != 2)
		{
			level->parallax = { 1024.0f, 1024.0f };
		}
		else
		{
			line = parser.readLine(bufferPos);
		}

		// Number of textures used by the level.
		s32 textureCount = 0;
		if (sscanf(line, " TEXTURES %d", &textureCount) != 1)
		{
			return false;
		}
		level->textures.resize(textureCount);

		// Read texture names.
		char textureName[256];
		for (s32 i = 0; i < textureCount; i++)
		{
			line = parser.readLine(bufferPos);
			if (sscanf(line, " TEXTURE: %s ", textureName) != 1)
			{
				strcpy(textureName, "DEFAULT.BM");
			}

			char bmTextureName[32];
			FileUtil::replaceExtension(textureName, "BM", bmTextureName);
			level->textures[i] = { bmTextureName, loadTexture(bmTextureName) };
		}
		// Sometimes there are extra textures, just add them - they will be compacted later.
		bool readNext = true;
		while (sscanf(line, " TEXTURE: %s ", textureName) == 1)
		{
			char bmTextureName[32];
			FileUtil::replaceExtension(textureName, "BM", bmTextureName);
			level->textures.push_back({ bmTextureName, loadTexture(bmTextureName) });

			line = parser.readLine(bufferPos);
			readNext = false;
		}
		
		// Load Sectors.
		if (readNext)
		{
			line = parser.readLine(bufferPos);
		}
		s32 sectorCount = 0;
		if (sscanf(line, "NUMSECTORS %d", &sectorCount) != 1)
		{
			return false;
		}
		level->sectors.resize(sectorCount);

		u32 mainId = groups_getMainId();
		EditorSector* sector = level->sectors.data();
		for (s32 i = 0; i < sectorCount; i++, sector++)
		{
			*sector = {};
			sector->groupId = mainId;

			// Sector ID and Name
			line = parser.readLine(bufferPos);
			if (sscanf(line, " SECTOR %d", &sector->id) != 1)
			{
				return false;
			}

			// Allow names to have '#' in them.
			line = parser.readLine(bufferPos, false, true);
			// Sectors missing a name are valid but do not get "addresses" - and thus cannot be
			// used by the INF system (except in the case of doors and exploding walls, see the flags section below).
			char name[256];
			if (sscanf(line, " NAME %s", name) == 1)
			{
				sector->name = name;
			}

			// Lighting
			line = parser.readLine(bufferPos);
			if (sscanf(line, " AMBIENT %d", &sector->ambient) != 1)
			{
				return false;
			}

			s32 floorTexId, ceilTexId;
			// Floor Texture & Offset
			line = parser.readLine(bufferPos);
			s32 tmp;
			if (sscanf(line, " FLOOR TEXTURE %d %f %f %d", &floorTexId, &sector->floorTex.offset.x, &sector->floorTex.offset.z, &tmp) != 4)
			{
				return false;
			}
			line = parser.readLine(bufferPos);
			if (sscanf(line, " FLOOR ALTITUDE %f", &sector->floorHeight) != 1)
			{
				return false;
			}
			sector->floorTex.texIndex = floorTexId;
			
			// Ceiling Texture & Offset
			line = parser.readLine(bufferPos);
			if (sscanf(line, " CEILING TEXTURE %d %f %f %d", &ceilTexId, &sector->ceilTex.offset.x, &sector->ceilTex.offset.z, &tmp) != 4)
			{
				return false;
			}
			line = parser.readLine(bufferPos);
			if (sscanf(line, " CEILING ALTITUDE %f", &sector->ceilHeight) != 1)
			{
				return false;
			}
			sector->ceilTex.texIndex = ceilTexId;

			// Second Altitude
			line = parser.readLine(bufferPos);
			if (sscanf(line, " SECOND ALTITUDE %f", &sector->secHeight) == 1)
			{
				// Second altitude was added in version 1.7, so is optional before then.
				line = parser.readLine(bufferPos);
			}

			// Note: the editor works with +Y up, so negate heights.
			if (sector->floorHeight != 0.0f) { sector->floorHeight = -sector->floorHeight; }
			if (sector->ceilHeight  != 0.0f) { sector->ceilHeight  = -sector->ceilHeight; }
			if (sector->secHeight   != 0.0f) { sector->secHeight   = -sector->secHeight; }

			// Sector flags
			if (sscanf(line, " FLAGS %d %d %d", &sector->flags[0], &sector->flags[1], &sector->flags[2]) != 3)
			{
				return false;
			}

			// Optional layer
			line = parser.readLine(bufferPos);
			if (sscanf(line, " LAYER %d", &sector->layer) == 1)
			{
				// Not all versions have a layer.
				line = parser.readLine(bufferPos);
			}

			// Vertices
			s32 vertexCount = 0;
			if (sscanf(line, " VERTICES %d", &vertexCount) != 1)
			{
				return false;
			}

			sector->bounds[0] = {  FLT_MAX,  FLT_MAX,  FLT_MAX };
			sector->bounds[1] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
			sector->bounds[0].y = min(sector->floorHeight, sector->ceilHeight);
			sector->bounds[1].y = max(sector->floorHeight, sector->ceilHeight);

			sector->vtx.resize(vertexCount);
			Vec2f* vtx = sector->vtx.data();
			for (s32 v = 0; v < vertexCount; v++, vtx++)
			{
				line = parser.readLine(bufferPos);
				sscanf(line, " X: %f Z: %f ", &vtx->x, &vtx->z);

				sector->bounds[0].x = min(sector->bounds[0].x, vtx->x);
				sector->bounds[0].z = min(sector->bounds[0].z, vtx->z);

				sector->bounds[1].x = max(sector->bounds[1].x, vtx->x);
				sector->bounds[1].z = max(sector->bounds[1].z, vtx->z);
			}

			line = parser.readLine(bufferPos);
			s32 wallCount = 0;
			if (sscanf(line, " WALLS %d", &wallCount) != 1)
			{
				return false;
			}
			sector->walls.resize(wallCount);
			EditorWall* wall = sector->walls.data();
			for (s32 w = 0; w < wallCount; w++, wall++)
			{
				memset(wall, 0, sizeof(EditorWall));

				s32 unused, walk;
				// wallLight is optional, so there are 24 parameters, but 23 are required.
				line = parser.readLine(bufferPos);
				s32 texId[WP_COUNT] = { 0 };
				if (sscanf(line, " WALL LEFT: %d RIGHT: %d MID: %d %f %f %d TOP: %d %f %f %d BOT: %d %f %f %d SIGN: %d %f %f ADJOIN: %d MIRROR: %d WALK: %d FLAGS: %d %d %d LIGHT: %d",
					&wall->idx[0], &wall->idx[1], &texId[WP_MID], &wall->tex[WP_MID].offset.x, &wall->tex[WP_MID].offset.z, &unused,
					&texId[WP_TOP], &wall->tex[WP_TOP].offset.x, &wall->tex[WP_TOP].offset.z, &unused,
					&texId[WP_BOT], &wall->tex[WP_BOT].offset.x, &wall->tex[WP_BOT].offset.z, &unused,
					&texId[WP_SIGN], &wall->tex[WP_SIGN].offset.x, &wall->tex[WP_SIGN].offset.z,
					&wall->adjoinId, &wall->mirrorId, &walk, &wall->flags[0], &wall->flags[1], &wall->flags[2], &wall->wallLight) < 23)
				{
					return false;
				}

				if (wall->wallLight >= 32768)
				{
					wall->wallLight -= 65536;
				}

				wall->tex[WP_MID].texIndex = texId[WP_MID] >= 0 ? texId[WP_MID] : -1;
				wall->tex[WP_TOP].texIndex = texId[WP_TOP] >= 0 ? texId[WP_TOP] : -1;
				wall->tex[WP_BOT].texIndex = texId[WP_BOT] >= 0 ? texId[WP_BOT] : -1;
				wall->tex[WP_SIGN].texIndex = texId[WP_SIGN] >= 0 ? texId[WP_SIGN] : -1;

				if (wall->tex[WP_SIGN].texIndex < 0)
				{
					wall->tex[WP_SIGN].offset = { 0 };
				}
			}
		}

		// Original format level, so default to vanilla.
		level->featureSet = FSET_VANILLA;
		
		// Compute the bounds.
		level->bounds[0] = {  FLT_MAX,  FLT_MAX,  FLT_MAX };
		level->bounds[1] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
		level->layerRange[0] =  INT_MAX;
		level->layerRange[1] = -INT_MAX;
		const size_t count = level->sectors.size();
		sector = level->sectors.data();
		for (size_t i = 0; i < count; i++, sector++)
		{
			level->bounds[0].x = min(level->bounds[0].x, sector->bounds[0].x);
			level->bounds[0].y = min(level->bounds[0].y, sector->bounds[0].y);
			level->bounds[0].z = min(level->bounds[0].z, sector->bounds[0].z);

			level->bounds[1].x = max(level->bounds[1].x, sector->bounds[1].x);
			level->bounds[1].y = max(level->bounds[1].y, sector->bounds[1].y);
			level->bounds[1].z = max(level->bounds[1].z, sector->bounds[1].z);

			level->layerRange[0] = min(level->layerRange[0], sector->layer);
			level->layerRange[1] = max(level->layerRange[1], sector->layer);

			sectorToPolygon(sector);
		}
		loadLevelObjFromAsset(asset);
		loadLevelInfFromAsset(asset);
		fixupLevel(false);

		return true;
	}

	// Adds an entity definition to the list of level entities are returns an index.
	// Returns -1 on error.
	s32 addEntityToLevel(const Entity* newEntity)
	{
		if (!newEntity) { return -1; }

		const s32 count = (s32)s_level.entities.size();
		const Entity* entity = s_level.entities.data();
		for (s32 i = 0; i < count; i++, entity++)
		{
			if (entityDefsEqual(newEntity, entity))
			{
				return i;
			}
		}

		s32 index = (s32)s_level.entities.size();
		s_level.entities.push_back(*newEntity);
		s_level.entities.back().id = index;

		return index;
	}

	s32 addLevelNoteToLevel(const LevelNote* newNote)
	{
		if (!newNote) { return -1; }

		LevelNote note = *newNote;
		note.id = (s32)s_level.notes.size();
		s_level.notes.push_back(note);
		return note.id;
	}

	void updateBoundsWithSector(EditorSector* sector)
	{
		s_level.bounds[0].x = min(s_level.bounds[0].x, sector->bounds[0].x);
		s_level.bounds[0].y = min(s_level.bounds[0].y, sector->bounds[0].y);
		s_level.bounds[0].z = min(s_level.bounds[0].z, sector->bounds[0].z);

		s_level.bounds[1].x = max(s_level.bounds[1].x, sector->bounds[1].x);
		s_level.bounds[1].y = max(s_level.bounds[1].y, sector->bounds[1].y);
		s_level.bounds[1].z = max(s_level.bounds[1].z, sector->bounds[1].z);

		s_level.layerRange[0] = min(s_level.layerRange[0], sector->layer);
		s_level.layerRange[1] = max(s_level.layerRange[1], sector->layer);
	}

	void updateLevelBounds(EditorSector* sector)
	{
		if (!sector)
		{
			s_level.bounds[0] = { FLT_MAX,  FLT_MAX,  FLT_MAX };
			s_level.bounds[1] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
			s_level.layerRange[0] = INT_MAX;
			s_level.layerRange[1] = -INT_MAX;
			const size_t count = s_level.sectors.size();
			sector = s_level.sectors.data();
			for (size_t i = 0; i < count; i++, sector++)
			{
				updateBoundsWithSector(sector);
			}
		}
		else
		{
			updateBoundsWithSector(sector);
		}
	}

	bool loadFromTFLWithPath(const char* filePath)
	{
		// Then try to open it based on the path, if it fails load the LEV file.
		FileStream file;
		if (!file.open(filePath, FileStream::MODE_READ))
		{
			return false;
		}

		// Check the version.
		u32 version;
		file.read(&version);
		if (version < LEF_MinVersion || version > LEF_CurVersion)
		{
			file.close();
			return false;
		}

		// Load the level.
		file.read(&s_level.name);
		file.read(&s_level.slot);
		file.read(&s_level.palette);
		file.readBuffer(&s_level.featureSet, sizeof(TFE_Editor::FeatureSet));
		file.readBuffer(&s_level.parallax, sizeof(Vec2f));
		file.readBuffer(&s_level.bounds, sizeof(Vec3f) * 2);
		file.readBuffer(&s_level.layerRange, sizeof(s32) * 2);

		// Textures.
		u32 textureCount;
		file.read(&textureCount);
		s_level.textures.resize(textureCount);
		LevelTextureAsset* tex = s_level.textures.data();
		for (u32 i = 0; i < textureCount; i++)
		{
			file.read(&tex[i].name);
			tex[i].handle = loadTexture(tex[i].name.c_str());
		}

		// Setup initial groups so the main ID is accessible.
		groups_init();
		const u32 mainGroupId = groups_getMainId();

		// Sectors.
		u32 sectorCount;
		file.read(&sectorCount);
		s_level.sectors.resize(sectorCount);
		EditorSector* sector = s_level.sectors.data();
		for (u32 i = 0; i < sectorCount; i++, sector++)
		{
			file.read(&sector->id);
			if (version >= LEF_Groups)
			{
				file.read(&sector->groupId);
			}
			else
			{
				sector->groupId = mainGroupId;
			}

			file.read(&sector->name);
			file.readBuffer(&sector->floorTex, sizeof(LevelTexture));
			file.readBuffer(&sector->ceilTex, sizeof(LevelTexture));
			file.read(&sector->floorHeight);
			file.read(&sector->ceilHeight);
			file.read(&sector->secHeight);
			file.read(&sector->ambient);
			file.read(sector->flags, 3);

			u32 vtxCount;
			file.read(&vtxCount);
			sector->vtx.resize(vtxCount);
			file.readBuffer(sector->vtx.data(), sizeof(Vec2f), vtxCount);

			u32 wallCount;
			file.read(&wallCount);
			sector->walls.resize(wallCount);
			file.readBuffer(sector->walls.data(), sizeof(EditorWall), wallCount);

			file.readBuffer(sector->bounds, sizeof(Vec3f), 2);
			file.read(&sector->layer);

			if (version >= LEF_EntityV1)
			{
				s32 objCount;
				file.read(&objCount);
				sector->obj.resize(objCount);

				EditorObject* obj = sector->obj.data();
				for (s32 o = 0; o < objCount; o++, obj++)
				{
					file.read(&obj->entityId);
					file.read(&obj->angle);

					// Defaults
					obj->pitch = 0.0f;
					obj->roll = 0.0f;
					if (version >= LEF_EntityV3)
					{
						file.read(&obj->pitch);
						file.read(&obj->roll);
					}
					compute3x3Rotation(&obj->transform, obj->angle, obj->pitch, obj->roll);

					obj->diff = 1; // default
					if (version >= LEF_EntityV2)
					{
						file.read(&obj->diff);
					}
					file.readBuffer(&obj->pos, sizeof(Vec3f));
				}
			}

			sector->searchKey = 0;
			sectorToPolygon(sector);
		}

		// Entity Definitions.
		if (version >= LEF_EntityList)
		{
			s32 entityCount = 0;
			file.read(&entityCount);
			s_level.entities.resize(entityCount);
			Entity* entity = s_level.entities.data();
			for (s32 i = 0; i < entityCount; i++, entity++)
			{
				readEntityDataBinary(&file, entity, version);
				loadSingleEntityData(entity);
				entity->id = i;
			}
		}
		else
		{
			// Clear out entities since the order changed in the def file.
			// Levels should no longer be sensitive to such changes.
			sector = s_level.sectors.data();
			for (u32 i = 0; i < sectorCount; i++, sector++)
			{
				sector->obj.clear();
			}
		}
		// Handle INF
		editor_loadInfBinary(file, version);
		groups_loadBinary(file, version);

		s_level.notes.clear();
		if (version >= LEF_LevelNotes)
		{
			s32 levelNoteCount;
			file.read(&levelNoteCount);
			s_level.notes.resize(levelNoteCount);

			LevelNote* note = s_level.notes.data();
			for (s32 i = 0; i < levelNoteCount; i++, note++)
			{
				note->id = i;
				file.read(&note->flags);
				file.read(&note->groupId);
				file.read(note->pos.m, 3);
				file.read(note->fade.m, 2);
				file.read(&note->iconColor);
				file.read(&note->textColor);
				file.read(&note->note);
			}
		}

		s_level.guidelines.clear();
		if (version >= LEF_Guidelines)
		{
			s32 guidelineCount = 0;
			file.read(&guidelineCount);
			s_level.guidelines.resize(guidelineCount);

			Guideline* guidelines = s_level.guidelines.data();
			s32 validGuidelineCount = 0;
			for (s32 i = 0; i < guidelineCount; i++)
			{
				Guideline* guideline = &guidelines[validGuidelineCount];

				// Counts
				s32 vtxCount = 0;
				s32 knotCount = 0;
				s32 edgeCount = 0;
				s32 offsetCount = 0;
				file.read(&vtxCount);
				file.read(&knotCount);
				file.read(&edgeCount);
				file.read(&offsetCount);
								
				guideline->id = i;
				guideline->vtx.resize(vtxCount);
				guideline->knots.resize(knotCount);
				guideline->edge.resize(edgeCount);
				guideline->offsets.resize(offsetCount);

				// Values
				if (vtxCount > 0) { file.readBuffer(guideline->vtx.data(), sizeof(Vec2f) * vtxCount); }
				if (knotCount > 0) { file.readBuffer(guideline->knots.data(), sizeof(Vec4f) * knotCount); }
				if (edgeCount > 0) { file.readBuffer(guideline->edge.data(), sizeof(GuidelineEdge) * edgeCount); }
				if (offsetCount > 0) { file.readBuffer(guideline->offsets.data(), sizeof(f32) * offsetCount); }

				// Settings
				file.read(guideline->bounds.m, 4);
				file.read(&guideline->maxOffset);
				file.read(&guideline->flags);
				file.read(&guideline->maxHeight);
				file.read(&guideline->minHeight);
				file.read(&guideline->maxSnapRange);

				guideline->subDivLen = 0.0f;
				if (version >= LEF_GuidelineV2)
				{
					file.read(&guideline->subDivLen);
				}

				// Deal with broken guidelines from earlier versions...
				if (vtxCount > 0 && edgeCount > 0)
				{
					validGuidelineCount++;
				}
				// Compute the subdivion data.
				guideline_computeSubdivision(guideline);
			}
			if (validGuidelineCount != guidelineCount)
			{
				s_level.guidelines.resize(validGuidelineCount);
			}
		}

		file.close();
		fixupLevel(false);
		
		return true;
	}

	bool loadFromTFL(const char* name)
	{
		// If there is no project, then the TFL can't exist.
		Project* project = project_get();
		if (!project) { return false; }

		char filePath[TFE_MAX_PATH];
		sprintf(filePath, "%s/%s.tfl", project->path, name);
		return loadFromTFLWithPath(filePath);
	}

	bool saveLevelToPath(const char* filePath, bool cleanLevel)
	{
		if (cleanLevel)
		{
			levelSetClean();
		}
		LE_INFO("Saving level to '%s'", filePath);

		FileStream file;
		if (!file.open(filePath, FileStream::MODE_WRITE))
		{
			LE_ERROR("Cannot open '%s' for writing.", filePath);
			return false;
		}

		// Update the level bounds.
		updateLevelBounds();

		// Version.
		const u32 version = LEF_CurVersion;
		file.write(&version);

		// Level data.
		file.write(&s_level.name);
		file.write(&s_level.slot);
		file.write(&s_level.palette);
		file.writeBuffer(&s_level.featureSet, sizeof(TFE_Editor::FeatureSet));
		file.writeBuffer(&s_level.parallax, sizeof(Vec2f));
		file.writeBuffer(&s_level.bounds, sizeof(Vec3f) * 2);
		file.writeBuffer(&s_level.layerRange, sizeof(s32) * 2);

		// Textures.
		const u32 textureCount = (u32)s_level.textures.size();
		file.write(&textureCount);
		LevelTextureAsset* tex = s_level.textures.data();
		for (u32 i = 0; i < textureCount; i++)
		{
			file.write(&tex[i].name);
		}

		// Sectors.
		const u32 sectorCount = (u32)s_level.sectors.size();
		file.write(&sectorCount);
		EditorSector* sector = s_level.sectors.data();
		for (u32 i = 0; i < sectorCount; i++, sector++)
		{
			file.write(&sector->id);
			file.write(&sector->groupId);
			file.write(&sector->name);
			file.writeBuffer(&sector->floorTex, sizeof(LevelTexture));
			file.writeBuffer(&sector->ceilTex, sizeof(LevelTexture));
			file.write(&sector->floorHeight);
			file.write(&sector->ceilHeight);
			file.write(&sector->secHeight);
			file.write(&sector->ambient);
			file.write(sector->flags, 3);

			u32 vtxCount = (u32)sector->vtx.size();
			file.write(&vtxCount);
			file.writeBuffer(sector->vtx.data(), sizeof(Vec2f), vtxCount);

			u32 wallCount = (u32)sector->walls.size();
			file.write(&wallCount);
			file.writeBuffer(sector->walls.data(), sizeof(EditorWall), wallCount);

			file.writeBuffer(sector->bounds, sizeof(Vec3f), 2);
			file.write(&sector->layer);
			// Polygon is derived on load.
			// Searchkey is reset on load.

			// LEF_EntityV1
			s32 objCount = (s32)sector->obj.size();
			file.write(&objCount);
			EditorObject* obj = sector->obj.data();
			for (s32 o = 0; o < objCount; o++, obj++)
			{
				file.write(&obj->entityId);
				file.write(&obj->angle);
				file.write(&obj->pitch);
				file.write(&obj->roll);
				file.write(&obj->diff);
				file.writeBuffer(&obj->pos, sizeof(Vec3f));
			}
		}

		// Entity Definitions.
		// version >= LEF_EntityList
		const s32 entityCount = (s32)s_level.entities.size();
		file.write(&entityCount);

		const Entity* entity = s_level.entities.data();
		for (s32 i = 0; i < entityCount; i++, entity++)
		{
			writeEntityDataBinary(entity, &file);
		}
		// Handle INF
		editor_saveInfBinary(file);
		groups_saveBinary(file);

		// version >= LEF_LevelNotes
		const s32 levelNoteCount = (s32)s_level.notes.size();
		file.write(&levelNoteCount);

		const LevelNote* note = s_level.notes.data();
		for (s32 i = 0; i < levelNoteCount; i++, note++)
		{
			file.write(&note->flags);
			file.write(&note->groupId);
			file.write(note->pos.m, 3);
			file.write(note->fade.m, 2);
			file.write(&note->iconColor);
			file.write(&note->textColor);
			file.write(&note->note);
		}

		// version >= LEF_Guidelines
		const s32 guidelineCount = (s32)s_level.guidelines.size();
		file.write(&guidelineCount);

		const Guideline* guideline = s_level.guidelines.data();
		for (s32 i = 0; i < guidelineCount; i++, guideline++)
		{
			// Counts
			s32 vtxCount = (s32)guideline->vtx.size();
			s32 knotCount = (s32)guideline->knots.size();
			s32 edgeCount = (s32)guideline->edge.size();
			s32 offsetCount = (s32)guideline->offsets.size();
			file.write(&vtxCount);
			file.write(&knotCount);
			file.write(&edgeCount);
			file.write(&offsetCount);

			// Values
			if (vtxCount > 0) { file.writeBuffer(guideline->vtx.data(), sizeof(Vec2f) * vtxCount); }
			if (knotCount > 0) { file.writeBuffer(guideline->knots.data(), sizeof(Vec4f) * knotCount); }
			if (edgeCount > 0) { file.writeBuffer(guideline->edge.data(), sizeof(GuidelineEdge) * edgeCount); }
			if (offsetCount > 0) { file.writeBuffer(guideline->offsets.data(), sizeof(f32) * offsetCount); }

			// Settings
			file.write(guideline->bounds.m, 4);
			file.write(&guideline->maxOffset);
			file.write(&guideline->flags);
			file.write(&guideline->maxHeight);
			file.write(&guideline->minHeight);
			file.write(&guideline->maxSnapRange);
			file.write(&guideline->subDivLen);
		}

		file.close();

		LE_INFO("Save Complete");
		return true;
	}
			
	// Save in the binary editor format.
	bool saveLevel()
	{
		Project* project = project_get();
		if (!project)
		{
			LE_ERROR("Cannot save if no project is open.");
			return false;
		}
		fixupLevel();

		char filePath[TFE_MAX_PATH];
		sprintf(filePath, "%s/%s.tfl", project->path, s_level.slot.c_str());
		return saveLevelToPath(filePath);
	}

	// Export the level to the game format.
	#define WRITE_LINE(...) \
		sprintf(buffer, __VA_ARGS__); \
		file.writeBuffer(buffer, (u32)strlen(buffer));
	#define NEW_LINE() file.writeBuffer(c_newLine, (u32)strlen(c_newLine))

	void exportWriteTFEHeader(char* buffer, FileStream& file)
	{
		Vec2i editorVersion = getEditorVersion();
		WRITE_LINE("/*******************************************\r\n");
		WRITE_LINE(" * Generated by TFE Editor version %d.%02d\r\n", editorVersion.x, editorVersion.z);
		WRITE_LINE(" *******************************************/\r\n\r\n");
	}

	void exportWriteTFEHeader_LEV(char* buffer, FileStream& file)
	{
		Vec2i editorVersion = getEditorVersion();
		WRITE_LINE("#*******************************************\r\n");
		WRITE_LINE("#* Generated by TFE Editor version %d.%02d\r\n", editorVersion.x, editorVersion.z);
		WRITE_LINE("#*******************************************\r\n\r\n");
	}

	bool exportDfLevel(const char* levFile)
	{
		FileStream file;
		if (!file.open(levFile, FileStream::MODE_WRITE))
		{
			return false;
		}

		char buffer[256];
		exportWriteTFEHeader_LEV(buffer, file);
		WRITE_LINE("%s", "LEV 2.1\r\n");
		WRITE_LINE("LEVELNAME %s\r\n", s_level.name.c_str());
		WRITE_LINE("PALETTE %s\r\n", s_level.palette.c_str());
		WRITE_LINE("%s", "MUSIC surfin.GMD\r\n");
		WRITE_LINE("%s", "PARALLAX 1024.0 1024.0\r\n");
		NEW_LINE();

		const s32 textureCount = (s32)s_level.textures.size();
		WRITE_LINE("TEXTURES %d\r\n", textureCount);
		const LevelTextureAsset* tex = s_level.textures.data();
		for (s32 i = 0; i < textureCount; i++, tex++)
		{
			tex->name.c_str();
			WRITE_LINE("  TEXTURE: %s\t\t#  %d\r\n", tex->name.c_str(), i);
		}
		NEW_LINE();

		const s32 sectorCount = (s32)s_level.sectors.size();
		s32 writeSectorCount = 0;
		EditorSector* sector = s_level.sectors.data();

		std::vector<s32> writeSectorId;
		writeSectorId.resize(sectorCount);
		for (s32 i = 0; i < sectorCount; i++, sector++)
		{
			if (sector_excludeFromExport(sector))
			{
				writeSectorId[i] = -1;
				continue;
			}
			writeSectorId[i] = writeSectorCount;
			writeSectorCount++;
		}

		WRITE_LINE("NUMSECTORS %d\r\n", writeSectorCount);
		NEW_LINE();
		NEW_LINE();

		sector = s_level.sectors.data();
		for (s32 i = 0; i < sectorCount; i++, sector++)
		{
			if (sector_excludeFromExport(sector)) { continue; }
			WRITE_LINE("SECTOR %d\r\n", writeSectorId[i]);

			if (sector->name.empty()) { WRITE_LINE("  NAME\r\n"); }
			else { WRITE_LINE("  NAME\t%s\r\n", sector->name.c_str()); }

			WRITE_LINE("  AMBIENT\t%d\r\n", (s32)sector->ambient);
			WRITE_LINE("  FLOOR TEXTURE\t%d\t%0.2f\t%0.2f\t%d\r\n", sector->floorTex.texIndex, sector->floorTex.offset.x, sector->floorTex.offset.z, 2);
			WRITE_LINE("  FLOOR ALTITUDE\t%0.2f\r\n", -sector->floorHeight);
			WRITE_LINE("  CEILING TEXTURE\t%d\t%0.2f\t%0.2f\t%d\r\n", sector->ceilTex.texIndex, sector->ceilTex.offset.x, sector->ceilTex.offset.z, 2);
			WRITE_LINE("  CEILING ALTITUDE\t%0.2f\r\n", -sector->ceilHeight);
			WRITE_LINE("  SECOND ALTITUDE\t%0.2f\r\n", -sector->secHeight);
			WRITE_LINE("  FLAGS %u %u %u\r\n", sector->flags[0], sector->flags[1], sector->flags[2]);
			WRITE_LINE("  LAYER %d\r\n", sector->layer);
			NEW_LINE();

			// Vertices.
			s32 vtxCount = (s32)sector->vtx.size();
			WRITE_LINE("  VERTICES %05d\r\n", vtxCount);
			const Vec2f* vtx = sector->vtx.data();
			for (s32 v = 0; v < vtxCount; v++, vtx++)
			{
				WRITE_LINE("    X: %0.2f\tZ: %0.2f\t#  %d\r\n", vtx->x, vtx->z, v);
			}
			NEW_LINE();

			// Walls
			s32 wallCount = (s32)sector->walls.size();
			WRITE_LINE("  WALLS %d\r\n", wallCount);
			const EditorWall* wall = sector->walls.data();
			for (s32 w = 0; w < wallCount; w++, wall++)
			{
				s32 adjoinId = wall->adjoinId >= 0 ? writeSectorId[wall->adjoinId] : -1;
				s32 mirrorId = wall->mirrorId;
				// If adjoined sectors are not exported, then remove the adjoin to avoid crashes.
				if (wall->adjoinId >= 0 && sector_excludeFromExport(&s_level.sectors[wall->adjoinId]))
				{
					adjoinId = -1;
					mirrorId = -1;
				}
				if (adjoinId < 0)
				{
					mirrorId = -1;
				}
				assert((mirrorId >= 0 && adjoinId >= 0) || (mirrorId < 0 && adjoinId < 0));

				WRITE_LINE("    WALL LEFT:\t%d  RIGHT:\t%d  MID:\t%d\t%0.2f\t%0.2f\t%d  TOP:\t%d\t%0.2f\t%0.2f\t%d  BOT:\t%d\t%0.2f\t%0.2f\t%d  "
					"SIGN:\t%d\t%0.2f\t%0.2f  ADJOIN:\t%d  MIRROR:\t%d  WALK:\t%d  FLAGS: %d %d %d  LIGHT: %d\r\n", 
					wall->idx[0], wall->idx[1],
					wall->tex[WP_MID].texIndex, wall->tex[WP_MID].offset.x, wall->tex[WP_MID].offset.z, 0,
					wall->tex[WP_TOP].texIndex, wall->tex[WP_TOP].offset.x, wall->tex[WP_TOP].offset.z, 0,
					wall->tex[WP_BOT].texIndex, wall->tex[WP_BOT].offset.x, wall->tex[WP_BOT].offset.z, 0,
					wall->tex[WP_SIGN].texIndex, wall->tex[WP_SIGN].offset.x, wall->tex[WP_SIGN].offset.z,
					adjoinId, mirrorId, adjoinId, wall->flags[0], wall->flags[1], wall->flags[2], wall->wallLight);
			}
			NEW_LINE();
		}

		file.close();
		return true;
	}

	s32 addObjAsset(const std::string& name, std::vector<std::string>& list)
	{
		// Does it already exist?
		const size_t count = list.size();
		const std::string* data = list.data();
		for (size_t i = 0; i < count; i++, data++)
		{
			if (strcasecmp(name.c_str(), data->c_str()) == 0)
			{
				return s32(i);
			}
		}

		s32 newId = (s32)list.size();
		list.push_back(name);
		return newId;
	}

	void writeVariables(const std::vector<EntityVar>& var, FileStream& file, char* buffer)
	{
		s32 varCount = (s32)var.size();
		// Variables.
		for (s32 v = 0; v < varCount; v++)
		{
			const EntityVarDef* def = getEntityVar(var[v].defId);
			if (strcasecmp(def->name.c_str(), "GenType") == 0)
			{
				continue;
			}
			switch (def->type)
			{
				case EVARTYPE_BOOL:
				{
					// If the bool doesn't match the "default" value - then don't write it at all.
					if (var[v].value.bValue == def->defValue.bValue)
					{
						WRITE_LINE("            %s:     %s\r\n", def->name.c_str(), var[v].value.bValue ? "TRUE" : "FALSE");
					}
				} break;
				case EVARTYPE_BOOL2:
				{
					// If the bool doesn't match the "default" value - then don't write it at all.
					if (var[v].value.bValue == def->defValue.bValue)
					{
						WRITE_LINE("            %s\r\n", def->name.c_str());
					}
				} break;
				case EVARTYPE_FLOAT:
				{
					WRITE_LINE("            %s:     %f\r\n", def->name.c_str(), var[v].value.fValue);
				} break;
				case EVARTYPE_INT:
				case EVARTYPE_FLAGS:
				{
					WRITE_LINE("            %s:     %d\r\n", def->name.c_str(), var[v].value.iValue);
				} break;
				case EVARTYPE_STRING_LIST:
				{
					WRITE_LINE("            %s:     \"%s\"\r\n", def->name.c_str(), var[v].value.sValue.c_str());
				} break;
				case EVARTYPE_INPUT_STRING_PAIR:
				{
					if (!var[v].value.sValue.empty())
					{
						const char* varName = def->name.c_str();
						const char* sValue1 = var[v].value.sValue1.c_str();
						
						if ((strcasecmp(varName, "Vue") == 0 || strcasecmp(varName, "Vue_Append") == 0) && strcasecmp(sValue1, "camera") == 0)
						{
							// Special case for vue camera, don't write quote marks
							WRITE_LINE("            %s:     %s %s\r\n", varName, var[v].value.sValue.c_str(), sValue1);
						}
						else
						{
							WRITE_LINE("            %s:     %s \"%s\"\r\n", varName, var[v].value.sValue.c_str(), sValue1);
						}
					}
				} break;
				case EVARTYPE_INPUT_STRING:
				{
					WRITE_LINE("            %s:     %s\r\n", def->name.c_str(), var[v].value.sValue.c_str());
				} break;
			}
		}
	}

	void writeObjSequence(const EditorObject* obj, const Entity* entity, FileStream& file)
	{
		char buffer[256];
		if (entity->logic.empty() && entity->var.empty()) { return; }

		WRITE_LINE("        SEQ\r\n");
		const s32 logicCount = (s32)entity->logic.size();
		if (logicCount)
		{
			for (s32 l = 0; l < logicCount; l++)
			{
				const std::vector<EntityVar>& var = entity->logic[l].var;
				const s32 varCount = (s32)var.size();

				if (strcasecmp(entity->logic[l].name.c_str(), "generator") == 0)
				{
					for (s32 v = 0; v < varCount; v++)
					{
						if (strcasecmp(getEntityVarName(var[v].defId), "GenType") == 0)
						{
							WRITE_LINE("            LOGIC:     %s %s\r\n", entity->logic[l].name.c_str(), var[v].value.sValue.c_str());
							break;
						}
					}
				}
				else
				{
					WRITE_LINE("            LOGIC:     %s\r\n", entity->logic[l].name.c_str());
				}
				// Write logic variables.
				writeVariables(var, file, buffer);
			}
		}
		else if (!entity->var.empty())
		{
			writeVariables(entity->var, file, buffer);
		}
		WRITE_LINE("        SEQEND\r\n");
	}
			
	bool exportDfObj(const char* oFile, const StartPoint* start)
	{
		FileStream file;
		if (!file.open(oFile, FileStream::MODE_WRITE))
		{
			return false;
		}

		char buffer[256];
		exportWriteTFEHeader(buffer, file);
		WRITE_LINE("%s", "O 1.1\r\n");
		NEW_LINE();

		std::vector<std::string> pods;
		std::vector<std::string> sprites;
		std::vector<std::string> frames;
		std::vector<const EditorObject*> objList;
		std::vector<s32> objData;
		const s32 sectorCount = (s32)s_level.sectors.size();
		EditorSector* sector = s_level.sectors.data();
		s32 startPointId = -1;
		for (s32 s = 0; s < sectorCount; s++, sector++)
		{
			if (sector_excludeFromExport(sector)) { continue; }

			const s32 objCount = (s32)sector->obj.size();
			const EditorObject* obj = sector->obj.data();
			for (s32 o = 0; o < objCount; o++, obj++)
			{
				const Entity* entity = &s_level.entities[obj->entityId];
				if ((entity->categories & s_enemyCategoryFlag) && (s_editorConfig.levelEditorFlags & LEVEDITOR_FLAG_NO_ENEMIES))
				{
					continue;
				}

				s32 index = (s32)objList.size();
				objList.push_back(obj);
				s32 dataIndex = 0;
				// TODO: Handle other types.
				if (entity->type == ETYPE_FRAME || entity->type == ETYPE_SPRITE || entity->type == ETYPE_3D)
				{
					dataIndex = addObjAsset(entity->assetName, entity->type == ETYPE_FRAME ? frames : entity->type == ETYPE_SPRITE ? sprites : pods);
				}
				else if (entity->type == ETYPE_SPIRIT)
				{
					const s32 logicCount = (s32)entity->logic.size();
					for (s32 i = 0; i < logicCount; i++)
					{
						if (strcasecmp(entity->logic[i].name.c_str(), "Player") == 0)
						{
							startPointId = index;
							break;
						}
					}
				}
				objData.push_back(dataIndex);
			}
		}

		WRITE_LINE("LEVELNAME %s\r\n", s_level.name.c_str());
		NEW_LINE();

		const s32 podCount = (s32)pods.size();
		WRITE_LINE("PODS %d\r\n", podCount);
		for (s32 i = 0; i < podCount; i++)
		{
			WRITE_LINE("    POD: %s\r\n", pods[i].c_str());
		}
		NEW_LINE();

		const s32 spriteCount = (s32)sprites.size();
		WRITE_LINE("SPRS %d\r\n", spriteCount);
		for (s32 i = 0; i < spriteCount; i++)
		{
			WRITE_LINE("    SPR: %s\r\n", sprites[i].c_str());
		}
		NEW_LINE();

		const s32 frameCount = (s32)frames.size();
		WRITE_LINE("FMES %d\r\n", frameCount);
		for (s32 i = 0; i < frameCount; i++)
		{
			WRITE_LINE("    FME: %s\r\n", frames[i].c_str());
		}
		NEW_LINE();

		WRITE_LINE("SOUNDS %d\r\n", 0);
		NEW_LINE();

		// For now just put in a start point.
		const f32 radToDeg = 360.0f / (2.0f * PI);
		const f32 yaw   = start ? fmodf(start->yaw * radToDeg + 180.0f, 360.0f) : 0.0f;
		const f32 pitch = start ? start->pitch * radToDeg : 0.0f;
		const f32 y = start ? std::max(start->sector->floorHeight, start->pos.y - 5.8f) : 0.0f;

		s32 objCount = (s32)objList.size();
		s32 finalObjCount = objCount;
		if (startPointId < 0)
		{
			// If there is no start point, then we need to be provided one.
			if (!start)
			{
				return false;
			}
			finalObjCount++;
		}

		WRITE_LINE("OBJECTS %d\r\n", finalObjCount);

		// Add the start point if needed.
		if (startPointId < 0)
		{
			WRITE_LINE("    CLASS: SPIRIT     DATA: 0   X: %0.2f Y: %0.2f Z: %0.2f PCH: %0.2f   YAW: %0.2f ROL: 0.00   DIFF: 1\r\n", start->pos.x, -y, start->pos.z, pitch, yaw);
			WRITE_LINE("        SEQ\r\n");
			WRITE_LINE("            LOGIC:     PLAYER\r\n");
			WRITE_LINE("            EYE:       TRUE\r\n");
			WRITE_LINE("        SEQEND\r\n");
		}

		for (s32 i = 0; i < objCount; i++)
		{
			const EditorObject* obj = objList[i];
			const Entity* entity = &s_level.entities[obj->entityId];
			const f32 objYaw = fmodf(obj->angle * radToDeg, 360.0f);
			const f32 objPitch = fmodf(obj->pitch * radToDeg, 360.0f);
			const f32 objRoll = fmodf(obj->roll * radToDeg, 360.0f);
			const s32 logicCount = (s32)entity->logic.size();

			switch (entity->type)
			{
				case ETYPE_SPIRIT:
				{
					if (i == startPointId)
					{
						if (start)
						{
							WRITE_LINE("    CLASS: SPIRIT     DATA: 0   X: %0.2f Y: %0.2f Z: %0.2f PCH: %0.2f   YAW: %0.2f ROL: 0.00   DIFF: %d\r\n", start->pos.x, -y, start->pos.z, pitch, yaw, obj->diff);
						}
						else
						{
							WRITE_LINE("    CLASS: SPIRIT     DATA: 0   X: %0.2f Y: %0.2f Z: %0.2f PCH: %0.2f   YAW: %0.2f ROL: %0.2f   DIFF: %d\r\n", obj->pos.x, -obj->pos.y, obj->pos.z, objPitch, objYaw, objRoll, obj->diff);
						}
						WRITE_LINE("        SEQ\r\n");
						WRITE_LINE("            LOGIC:     PLAYER\r\n");
						WRITE_LINE("            EYE:       TRUE\r\n");
						WRITE_LINE("        SEQEND\r\n");
					}
					else
					{
						WRITE_LINE("    CLASS: SPIRIT     DATA: 0   X: %0.2f Y: %0.2f Z: %0.2f PCH: %0.2f   YAW: %0.2f ROL: %0.2f   DIFF: %d\r\n", obj->pos.x, -obj->pos.y, obj->pos.z, objPitch, objYaw, objRoll, obj->diff);
						writeObjSequence(obj, entity, file);
					}
				} break;
				case ETYPE_SAFE:
				{
					WRITE_LINE("    CLASS: SAFE     DATA: 0   X: %0.2f Y: %0.2f Z: %0.2f PCH: %0.2f   YAW: %0.2f ROL: %0.2f   DIFF: %d\r\n", obj->pos.x, -obj->pos.y, obj->pos.z, objPitch, objYaw, objRoll, obj->diff);
					writeObjSequence(obj, entity, file);
				} break;
				case ETYPE_FRAME:
				{
					WRITE_LINE("    CLASS: FRAME     DATA: %d   X: %0.2f Y: %0.2f Z: %0.2f PCH: %0.2f   YAW: %0.2f ROL: %0.2f   DIFF: %d\r\n", objData[i], obj->pos.x, -obj->pos.y, obj->pos.z, objPitch, objYaw, objRoll, obj->diff);
					writeObjSequence(obj, entity, file);
				} break;
				case ETYPE_SPRITE:
				{
					WRITE_LINE("    CLASS: SPRITE     DATA: %d   X: %0.2f Y: %0.2f Z: %0.2f PCH: %0.2f   YAW: %0.2f ROL: %0.2f   DIFF: %d\r\n", objData[i], obj->pos.x, -obj->pos.y, obj->pos.z, objPitch, objYaw, objRoll, obj->diff);
					writeObjSequence(obj, entity, file);
				} break;
				case ETYPE_3D:
				{
					WRITE_LINE("    CLASS: 3D     DATA: %d   X: %0.2f Y: %0.2f Z: %0.2f PCH: %0.2f   YAW: %0.2f ROL: %0.2f   DIFF: %d\r\n", objData[i], obj->pos.x, -obj->pos.y, obj->pos.z, objPitch, objYaw, objRoll, obj->diff);
					writeObjSequence(obj, entity, file);
				} break;
			}
		}
		NEW_LINE();

		file.close();
		return true;
	}

	bool exportDfInf(const char* infFile)
	{
		FileStream file;
		if (!file.open(infFile, FileStream::MODE_WRITE))
		{
			return false;
		}

		char buffer[256];
		exportWriteTFEHeader(buffer, file);
		WRITE_LINE("%s", "INF 1.0\r\n");
		NEW_LINE();

		WRITE_LINE("LEVELNAME %s\r\n", s_level.name.c_str());

		const s32 itemCount = (s32)s_levelInf.item.size();
		Editor_InfItem* item = s_levelInf.item.data();
		s32 itemWriteCount = 0;
		for (s32 i = 0; i < itemCount; i++, item++)
		{
			const s32 id = findSectorByName(item->name.c_str());
			if (id >= 0 && sector_excludeFromExport(&s_level.sectors[id])) { continue; }
			itemWriteCount++;
		}
		WRITE_LINE("items %d\r\n", itemWriteCount);

		if (s_levelInf.item.empty())
		{
			NEW_LINE();
			file.close();
			return true;
		}

		const char* tab = "    ";
		char curTab[256] = "";
		// Write out INF items.
		char seqStart[256], seqEnd[256];
		sprintf(seqStart, "%s%sseq\r\n", tab, tab);
		sprintf(seqEnd, "%s%sseqend\r\n", tab, tab);

		std::string itemBuffer;
		item = s_levelInf.item.data();
		for (s32 s = 0; s < itemCount; s++, item++)
		{
			const s32 id = findSectorByName(item->name.c_str());
			if (id >= 0 && sector_excludeFromExport(&s_level.sectors[id])) { continue; }

			char buffer[256];
			strcpy(curTab, tab);

			if (item->wallNum < 0)
			{
				WRITE_LINE("%sitem: sector%sname: %s\r\n", curTab, tab, item->name.c_str());
			}
			else
			{
				WRITE_LINE("%sitem: line%sname: %s%snum: %d\r\n", curTab, tab, item->name.c_str(), tab, item->wallNum);
			}

			strcat(curTab, tab);
			WRITE_LINE("%s", seqStart);
			{
				strcat(curTab, tab);

				itemBuffer.clear();
				editor_writeInfItem(itemBuffer, item, curTab);
				file.writeBuffer(itemBuffer.c_str(), (u32)strlen(itemBuffer.c_str()));
			}
			WRITE_LINE("%s", seqEnd);
		}

		NEW_LINE();
		file.close();
		return true;
	}

	// TODO: Factor this code out.
	// Gob Stuff
#pragma pack(push)
#pragma pack(1)
	struct GobHeader
	{
		char GOB_MAGIC[4];
		u32 MASTERX;	// offset to GOB_Index_t
	};

	struct GobEntry
	{
		u32 IX;		// offset to the start of the file.
		u32 LEN;		// length of the file.
		char NAME[13];	// file name.
	};

	struct GobIndex
	{
		u32 MASTERN;	// num files
		GobEntry* entries;
	};
#pragma pack(pop)
	static std::vector<GobEntry> s_directory;
	static std::vector<u8> s_workBuffer;

	u32 getFileLength(const char* path)
	{
		FileStream file;
		u32 len = 0;
		if (file.open(path, FileStream::MODE_READ))
		{
			len = (u32)file.getSize();
			file.close();
		}
		return len;
	}

	bool writeGob(const char* path, FileList& fileList)
	{
		if (!path || fileList.empty()) { return false; }

		FILE* gob = fopen(path, "wb");
		if (!gob) { return false; }

		GobHeader header = { 0 };
		header.GOB_MAGIC[0] = 'G';
		header.GOB_MAGIC[1] = 'O';
		header.GOB_MAGIC[2] = 'B';
		header.GOB_MAGIC[3] = '\n';
		header.MASTERX = sizeof(GobHeader);

		// Fill out the directory.
		u32 fileCount = (u32)fileList.size();
		s_directory.resize(fileCount);
		GobEntry* entry = s_directory.data();
		for (u32 i = 0; i < fileCount; i++, entry++)
		{
			// Otherwise read from the passed in fileList.
			entry->LEN = getFileLength(fileList[i].c_str());

			char name[TFE_MAX_PATH];
			FileUtil::getFileNameFromPath(fileList[i].c_str(), name, true);
			strcpy(entry->NAME, name);

			entry->IX = header.MASTERX;
			header.MASTERX += entry->LEN;
		}

		// Write the header and file count.
		fwrite(&header, sizeof(GobHeader), 1, gob);

		// Write the files.
		entry = s_directory.data();
		for (u32 i = 0; i < fileCount; i++, entry++)
		{
			fseek(gob, entry->IX, SEEK_SET);

			// Write the files in fileList.
			FileStream file;
			file.open(fileList[i].c_str(), FileStream::MODE_READ);
			u32 len = (u32)file.getSize();
			s_workBuffer.resize(len);
			file.readBuffer(s_workBuffer.data(), len);
			file.close();

			assert(len == entry->LEN);
			fwrite(s_workBuffer.data(), len, 1, gob);
		}

		// Write the directory.
		fseek(gob, header.MASTERX, SEEK_SET);
		fwrite(&fileCount, sizeof(u32), 1, gob);
		fwrite(s_directory.data(), sizeof(GobEntry), fileCount, gob);

		fclose(gob);
		return true;
	}

	bool writeZip(const char* path, FileList& fileList)
	{
		ZipArchive archive;
		if (!archive.create(path)) { return false; }
		const size_t count = fileList.size();
		for (size_t i = 0; i < count; i++)
		{
			archive.addFile(fileList[i].c_str(), fileList[i].c_str());
		}
		archive.close();
		return true;
	}

	const char* c_modHeaderTemplate =
		"================================================================\r\n"
		"Title:\t\t %s\r\n"
		"Author:\t\t %s\r\n"
		"Description: \r\n"
		"%s\r\n"
		"\r\n"
		"Additional Credits:\r\n"
		"================================================================\r\n"
		"%s\r\n";

	bool exportGob(const char* workPath, const char* exportPath, const char* gobName, const std::vector<LevelExportInfo>& levelList, char* gobTempPath)
	{
		const size_t count = levelList.size();
		char baseName[TFE_MAX_PATH];
		char levFile[TFE_MAX_PATH];
		char infFile[TFE_MAX_PATH];
		char objFile[TFE_MAX_PATH];
		char jediLine[TFE_MAX_PATH];

		// Load definitions.
		// TODO: Handle different games...
		const char* gameLocalDir = "DarkForces";
		loadVariableData(gameLocalDir);
		loadEntityData(gameLocalDir, false);
		loadLogicData(gameLocalDir);
		groups_init();

		Project* project = project_get();
		if (project && project->active)
		{
			char projEntityDataPath[TFE_MAX_PATH];
			sprintf(projEntityDataPath, "%s/%s.ini", project->path, "CustomEntityDef");
			loadEntityData(projEntityDataPath, true);
		}

		const LevelExportInfo* levelInfo = levelList.data();
		std::string jediLevel;
		FileList fileList;
		s32 levelCount = 0;

		std::vector<Asset*> assetList;
		WorkBuffer buffer;

		for (size_t i = 0; i < count; i++, levelInfo++)
		{
			FileUtil::getFileNameFromPath(levelInfo->slot.c_str(), baseName);

			sprintf(levFile, "%s/%s.LEV", workPath, baseName);
			sprintf(infFile, "%s/%s.INF", workPath, baseName);
			sprintf(objFile, "%s/%s.O", workPath, baseName);

			// Load the level.
			if (!loadLevelFromAsset(levelInfo->asset))
			{
				return false;
			}

			if (!exportDfLevel(levFile)) { return false; }
			if (!exportDfInf(infFile)) { return false; }
			if (!exportDfObj(objFile, nullptr)) { return false; }

			fileList.push_back(levFile);
			fileList.push_back(infFile);
			fileList.push_back(objFile);

			sprintf(jediLine, "%s,\t%s,\t%s\r\n", s_level.name.c_str(), baseName, "L:\\LEVELS\\");
			jediLevel += jediLine;
			levelCount++;

			gatherAssetList(s_level.slot.c_str(), levFile, objFile, assetList);
		}
		sprintf(jediLine, "LEVELS %d\r\n", levelCount);
		jediLevel = jediLine + jediLevel;
		jediLevel += "\r\n\r\n";
		jediLevel += "//      (Comments must be at the end of this file)\r\n";
		jediLevel += "//      This file contains the list of all the levels in Jedi,\r\n";
		jediLevel += "//      and the DOS names for those levels.\r\n";

		if (!assetList.empty())
		{
			char tmpDir[TFE_MAX_PATH];
			getTempDirectory(tmpDir);

			const size_t count = assetList.size();
			Asset** list = assetList.data();
			for (size_t i = 0; i < count; i++)
			{
				Asset* asset = list[i];
				// Export
				char exportPath[TFE_MAX_PATH];
				sprintf(exportPath, "%s/%s", tmpDir, asset->name.c_str());
				if (AssetBrowser::readWriteFile(exportPath, asset->name.c_str(), asset->archive, buffer))
				{
					fileList.push_back(exportPath);
				}
			}
		}

		char jediLevelPath[TFE_MAX_PATH];
		sprintf(jediLevelPath, "%s/jedi.lvl", workPath);
		FileStream jediLvlFile;
		if (jediLvlFile.open(jediLevelPath, FileStream::MODE_WRITE))
		{
			jediLvlFile.writeBuffer(jediLevel.c_str(), (u32)jediLevel.length());
			jediLvlFile.close();
			fileList.push_back(jediLevelPath);
		}

		char gobPath[TFE_MAX_PATH];
		const size_t len = strlen(exportPath);
		if (exportPath[len - 1] == '/' || exportPath[len - 1] == '\\')
		{
			sprintf(gobPath, "%s%s.GOB", workPath, gobName);
		}
		else
		{
			sprintf(gobPath, "%s/%s.GOB", workPath, gobName);
		}
		strcpy(gobTempPath, gobPath);
		return writeGob(gobPath, fileList);
	}

	// TODO: Support "gobx" format - basically storing data in "loose" format if vanilla support is not required.
	// TODO: Go through levels and include any resources not included in the base game.
	bool exportLevels(const char* workPath, const char* exportPath, const char* gobName, const std::vector<LevelExportInfo>& levelList)
	{
		char gobTempPath[TFE_MAX_PATH];
		if (!exportGob(workPath, exportPath, gobName, levelList, gobTempPath))
		{
			return false;
		}
		FileList fileList;
		fileList.push_back(gobTempPath);

		Project* project = project_get();

		char readme[4096];
		sprintf(readme, c_modHeaderTemplate, project->name, project->authors.c_str(), project->desc.c_str(), project->credits.c_str());

		char readmePath[TFE_MAX_PATH];
		sprintf(readmePath, "%s/%s.txt", workPath, project->name);
		FileStream readmeFile;
		if (readmeFile.open(readmePath, FileStream::MODE_WRITE))
		{
			readmeFile.writeBuffer(readme, (u32)strlen(readme) + 1);
			readmeFile.close();
			fileList.push_back(readmePath);
		}

		// Search for any files in the project directory that include the following extensions:
		const char* c_includeExt[] =
		{
			"txt",
			"fs",
			"lfd",
			"png",
			"jpg",
			"webp",
		};
		const size_t extLen = TFE_ARRAYSIZE(c_includeExt);
		FileList list;
		char dir[TFE_MAX_PATH];
		char filePath[TFE_MAX_PATH];
		sprintf(dir, "%s/", project->path);

		for (size_t i = 0; i < extLen; i++)
		{
			list.clear();
			FileUtil::readDirectory(dir, c_includeExt[i], list);
			for (size_t j = 0; j < list.size(); j++)
			{
				sprintf(filePath, "%s%s", dir, list[j].c_str());
				fileList.push_back(filePath);
			}
		}

		char zipPath[TFE_MAX_PATH];
		const size_t len = strlen(exportPath);
		if (exportPath[len - 1] == '/' || exportPath[len - 1] == '\\')
		{
			sprintf(zipPath, "%s%s.zip", exportPath, gobName);
		}
		else
		{
			sprintf(zipPath, "%s/%s.zip", exportPath, gobName);
		}
		return writeZip(zipPath, fileList);
	}

	void addAssetToList(Asset* asset, std::vector<Asset*>& assetList)
	{
		const size_t count = assetList.size();
		const Asset* const* list = assetList.data();
		for (size_t i = 0; i < count; i++)
		{
			if (list[i] == asset) { return; }
		}
		assetList.push_back(asset);
	}

	void readSourceFileAndSetupParser(FileStream& file, TFE_Parser& parser)
	{
		size_t len = file.getSize();
		s_fileData.resize(len + 1);
		char* buffer = (char*)s_fileData.data();
		file.readBuffer(buffer, (u32)len);
		buffer[len] = 0;
		file.close();

		parser.init(buffer, len);
		parser.enableBlockComments();
		parser.addCommentString("//");
		parser.addCommentString("#");
	}

	void gatherAssetList(const char* name, const char* levFile, const char* objFile, std::vector<Asset*>& assetList)
	{
		TFE_Parser parser;
		FileStream file;
		if (file.open(levFile, FileStream::MODE_READ))
		{
			readSourceFileAndSetupParser(file, parser);

			std::vector<std::string> textureList;
			std::string palette;
			size_t bufferPos = 0;
			const char* line = parser.readLine(bufferPos, true);
			TokenList tokens;
			char* endPtr = nullptr;
			while (line)
			{
				parser.tokenizeLine(line, tokens);
				const s32 tokenCount = (s32)tokens.size();

				if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "PALETTE", strlen("PALETTE")) == 0)
				{
					palette = tokens[1];
				}
				else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "TEXTURES", strlen("TEXTURES")) == 0)
				{
					const s32 count = strtol(tokens[1].c_str(), &endPtr, 10);
					if (count > 0)
					{
						textureList.reserve(count);
					}
				}
				else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "TEXTURE:", strlen("TEXTURE:")) == 0)
				{
					textureList.push_back(tokens[1]);
				}
				else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "NUMSECTORS", strlen("NUMSECTORS")) == 0)
				{
					break;
				}

				line = parser.readLine(bufferPos, true);
			}

			if (!textureList.empty())
			{
				const size_t textureCount = textureList.size();
				const std::string* list = textureList.data();
				for (size_t t = 0; t < textureCount; t++)
				{
					Asset* asset = AssetBrowser::findAsset(list[t].c_str(), TYPE_TEXTURE);
					if (asset && asset->assetSource != ASRC_VANILLA)
					{
						addAssetToList(asset, assetList);
					}
				}
			}

			// Load the colormap.
			if (!palette.empty())
			{
				char colorMap[TFE_MAX_PATH];
				FileUtil::replaceExtension(palette.c_str(), "CMP", colorMap);
				Asset* asset = AssetBrowser::findAsset(colorMap, TYPE_COLORMAP);
				if (asset && asset->assetSource != ASRC_VANILLA)
				{
					addAssetToList(asset, assetList);
				}
			}

			// Try to load the palette as well.
			char paletteFile[TFE_MAX_PATH];
			FileUtil::replaceExtension(name, "PAL", paletteFile);
			Asset* asset = AssetBrowser::findAsset(paletteFile, TYPE_PALETTE);
			if (asset && asset->assetSource != ASRC_VANILLA)
			{
				addAssetToList(asset, assetList);
			}
		}
		if (file.open(objFile, FileStream::MODE_READ))
		{
			readSourceFileAndSetupParser(file, parser);

			std::vector<std::string> podList;
			std::vector<std::string> spriteList;
			std::vector<std::string> frameList;
			size_t bufferPos = 0;
			const char* line = parser.readLine(bufferPos, true);
			TokenList tokens;
			char* endPtr = nullptr;
			while (line)
			{
				parser.tokenizeLine(line, tokens);
				const s32 tokenCount = (s32)tokens.size();

				if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "PODS", strlen("PODS")) == 0)
				{
					const s32 count = strtol(tokens[1].c_str(), &endPtr, 10);
					if (count > 0)
					{
						podList.reserve(count);
					}
				}
				else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "POD:", strlen("POD:")) == 0)
				{
					podList.push_back(tokens[1]);
				}
				else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "SPRS", strlen("SPRS")) == 0)
				{
					const s32 count = strtol(tokens[1].c_str(), &endPtr, 10);
					if (count > 0)
					{
						spriteList.reserve(count);
					}
				}
				else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "SPR:", strlen("SPR:")) == 0)
				{
					spriteList.push_back(tokens[1]);
				}
				else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "FMES", strlen("FMES")) == 0)
				{
					const s32 count = strtol(tokens[1].c_str(), &endPtr, 10);
					if (count > 0)
					{
						frameList.reserve(count);
					}
				}
				else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "FME:", strlen("FME:")) == 0)
				{
					frameList.push_back(tokens[1]);
				}
				else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "OBJECTS", strlen("OBJECTS")) == 0)
				{
					break;
				}

				line = parser.readLine(bufferPos, true);
			}

			if (!podList.empty())
			{
				const size_t count = podList.size();
				const std::string* list = podList.data();
				for (size_t t = 0; t < count; t++)
				{
					Asset* asset = AssetBrowser::findAsset(list[t].c_str(), TYPE_3DOBJ);
					if (asset && asset->assetSource != ASRC_VANILLA)
					{
						addAssetToList(asset, assetList);
					}
				}
			}
			if (!spriteList.empty())
			{
				const size_t count = spriteList.size();
				const std::string* list = spriteList.data();
				for (size_t t = 0; t < count; t++)
				{
					Asset* asset = AssetBrowser::findAsset(list[t].c_str(), TYPE_SPRITE);
					if (asset && asset->assetSource != ASRC_VANILLA)
					{
						addAssetToList(asset, assetList);
					}
				}
			}
			if (!frameList.empty())
			{
				const size_t count = frameList.size();
				const std::string* list = frameList.data();
				for (size_t t = 0; t < count; t++)
				{
					Asset* asset = AssetBrowser::findAsset(list[t].c_str(), TYPE_FRAME);
					if (asset && asset->assetSource != ASRC_VANILLA)
					{
						addAssetToList(asset, assetList);
					}
				}
			}
		}
	}

	bool exportLevel(const char* path, const char* name, const StartPoint* start)
	{
		char levFile[TFE_MAX_PATH];
		char infFile[TFE_MAX_PATH];
		char objFile[TFE_MAX_PATH];
		sprintf(levFile, "%s/%s.LEV", path, name);
		sprintf(infFile, "%s/%s.INF", path, name);
		sprintf(objFile, "%s/%s.O", path, name);

		if (!exportDfLevel(levFile)) { return false; }
		if (!exportDfInf(infFile)) { return false; }
		if (!exportDfObj(objFile, start)) { return false; }

		// Build a file list for the gob writer.
		FileList fileList;
		fileList.push_back(levFile);
		fileList.push_back(infFile);
		fileList.push_back(objFile);

		// Now go through all of the assets and gather a unique list (only including non-vanilla).
		std::vector<Asset*> assetList;
		WorkBuffer buffer;
		gatherAssetList(name, levFile, objFile, assetList);
		if (!assetList.empty())
		{
			char tmpDir[TFE_MAX_PATH];
			getTempDirectory(tmpDir);

			const size_t count = assetList.size();
			Asset** list = assetList.data();
			for (size_t i = 0; i < count; i++)
			{
				Asset* asset = list[i];
				// Export
				char exportPath[TFE_MAX_PATH];
				sprintf(exportPath, "%s/%s", tmpDir, asset->name.c_str());
				if (AssetBrowser::readWriteFile(exportPath, asset->name.c_str(), asset->archive, buffer))
				{
					fileList.push_back(exportPath);
				}
			}
		}

		char gobPath[TFE_MAX_PATH];
		const char* testPath = TFE_Paths::getPath(PATH_SOURCE_DATA);
		if (!testPath || !testPath[0]) { return false; }
		const size_t len = strlen(testPath);
		if (testPath[len - 1] == '/' || testPath[len - 1] == '\\')
		{
			sprintf(gobPath, "%sTFE_TEST.GOB", testPath);
		}
		else
		{
			sprintf(gobPath, "%s/TFE_TEST.GOB", testPath);
		}
		writeGob(gobPath, fileList);

		// Now run "TFE"
		// Get the app directory and GOB name.
		char appDir[TFE_MAX_PATH], gobName[TFE_MAX_PATH];
		FileUtil::getFilePath(s_editorConfig.darkForcesPort, appDir);
		FileUtil::getFileNameFromPath(gobPath, gobName, true);

		// Build the Commandline.
		char cmdLine[TFE_MAX_PATH];
		if (s_editorConfig.levelEditorFlags & LEVEDITOR_FLAG_RUN_TFE)
		{
			sprintf(cmdLine, "-gDark -skip_load_delay -u%s -c0 -l%s", gobName, s_level.slot.c_str());
		}
		else
		{
			sprintf(cmdLine, "-u%s -c0 -l%s", gobName, s_level.slot.c_str());
		}
		if (s_editorConfig.darkForcesAddCmdLine[0])
		{
			strcat(cmdLine, " ");
			strcat(cmdLine, s_editorConfig.darkForcesAddCmdLine);
		}
		const bool waitForCompletion = s_editorConfig.waitForPlayCompletion;

		// Run the test app (TFE, etc.), this will block until finished.
		osShellExecute(s_editorConfig.darkForcesPort, appDir, cmdLine, waitForCompletion);
		// Then cleanup by deleting the test GOB.
		if (waitForCompletion)
		{
			FileUtil::deleteFile(gobPath);
		}
		
		return true;
	}

	bool addFeatureToSectorList(s32 index, std::vector<s32>& sectorList)
	{
		EditorSector* sector = nullptr;
		s32 featureIndex = -1;
		HitPart part = HP_FLOOR;
		if (!selection_get(index, sector, featureIndex, &part)) { return false; }
		if (!sector) { return false; }
		if (featureIndex != -1 && part != HP_FLOOR && part != HP_CEIL) { return false; }

		insertIntoIntList(sector->id, &sectorList);
		return true;
	}

#define WRITE_TO_BUFFER(...) \
		sprintf(appendBuffer, __VA_ARGS__); \
		buffer.append(appendBuffer)

	void writeVariablesToBuffer(const std::vector<EntityVar>& var, std::string& buffer)
	{
		char appendBuffer[1024];
		s32 varCount = (s32)var.size();
		// Variables.
		for (s32 v = 0; v < varCount; v++)
		{
			const EntityVarDef* def = getEntityVar(var[v].defId);
			if (strcasecmp(def->name.c_str(), "GenType") == 0)
			{
				continue;
			}
			switch (def->type)
			{
				case EVARTYPE_BOOL:
				{
					// If the bool doesn't match the "default" value - then don't write it at all.
					if (var[v].value.bValue == def->defValue.bValue)
					{
						WRITE_TO_BUFFER("      %s: %s\r\n", def->name.c_str(), var[v].value.bValue ? "TRUE" : "FALSE");
					}
				} break;
				case EVARTYPE_BOOL2:
				{
					// If the bool doesn't match the "default" value - then don't write it at all.
					if (var[v].value.bValue == def->defValue.bValue)
					{
						WRITE_TO_BUFFER("      %s\r\n", def->name.c_str());
					}
				} break;
				case EVARTYPE_FLOAT:
				{
					WRITE_TO_BUFFER("      %s: %f\r\n", def->name.c_str(), var[v].value.fValue);
				} break;
				case EVARTYPE_INT:
				case EVARTYPE_FLAGS:
				{
					WRITE_TO_BUFFER("      %s: %d\r\n", def->name.c_str(), var[v].value.iValue);
				} break;
				case EVARTYPE_STRING_LIST:
				{
					WRITE_TO_BUFFER("      %s: \"%s\"\r\n", def->name.c_str(), var[v].value.sValue.c_str());
				} break;
				case EVARTYPE_INPUT_STRING_PAIR:
				{
					if (!var[v].value.sValue.empty())
					{
						const char* varName = def->name.c_str();
						const char* sValue1 = var[v].value.sValue1.c_str();

						if ((strcasecmp(varName, "Vue") == 0 || strcasecmp(varName, "Vue_Append") == 0) && strcasecmp(sValue1, "camera") == 0)
						{
							// Special case for vue camera, don't write quote marks
							WRITE_TO_BUFFER("      %s: %s %s\r\n", varName, var[v].value.sValue.c_str(), sValue1);
						}
						else
						{
							WRITE_TO_BUFFER("      %s: %s \"%s\"\r\n", varName, var[v].value.sValue.c_str(), sValue1);
						}
					}
				} break;
				case EVARTYPE_INPUT_STRING:
				{
					WRITE_TO_BUFFER("      %s: %s\r\n", def->name.c_str(), var[v].value.sValue.c_str());
				} break;
			}
		}
	}

	void writeObjSequenceToBuffer(const EditorObject* obj, const Entity* entity, std::string& buffer)
	{
		char appendBuffer[256];
		if (entity->logic.empty() && entity->var.empty()) { return; }

		WRITE_TO_BUFFER("    SEQ\r\n");
		const s32 logicCount = (s32)entity->logic.size();
		if (logicCount)
		{
			for (s32 l = 0; l < logicCount; l++)
			{
				const std::vector<EntityVar>& var = entity->logic[l].var;
				const s32 varCount = (s32)var.size();

				if (strcasecmp(entity->logic[l].name.c_str(), "generator") == 0)
				{
					for (s32 v = 0; v < varCount; v++)
					{
						if (strcasecmp(getEntityVarName(var[v].defId), "GenType") == 0)
						{
							WRITE_TO_BUFFER("      LOGIC: %s %s\r\n", entity->logic[l].name.c_str(), var[v].value.sValue.c_str());
							break;
						}
					}
				}
				else
				{
					WRITE_TO_BUFFER("      LOGIC: %s\r\n", entity->logic[l].name.c_str());
				}
				// Write logic variables.
				writeVariablesToBuffer(var, buffer);
			}
		}
		else if (!entity->var.empty())
		{
			writeVariablesToBuffer(entity->var, buffer);
		}
		WRITE_TO_BUFFER("    SEQEND\r\n");
	}

	// TODO:
	//   INF?
	bool exportSelectionToText(std::string& buffer)
	{
		std::vector<s32> sectorList;
		std::vector<const EditorObject*> objList;
		char appendBuffer[1024];
		const s32 count = selection_getCount();
		if (s_editMode == LEDIT_ENTITY)
		{
			if (count == 0)
			{
				EditorSector* sector = nullptr;
				s32 featureIndex = -1;
				if (!selection_get(SEL_INDEX_HOVERED, sector, featureIndex)) { return false; }
				if (!sector || featureIndex < 0) { return false; }

				objList.push_back(&sector->obj[featureIndex]);
			}
			else
			{
				EditorSector* sector = nullptr;
				s32 featureIndex = -1;
				for (s32 s = 0; s < count; s++)
				{
					if (!selection_get(s, sector, featureIndex)) { return false; }
					if (!sector || featureIndex < 0) { return false; }

					objList.push_back(&sector->obj[featureIndex]);
				}
			}
			if (objList.empty()) { return false; }
		}
		else
		{
			if (count == 0)
			{
				addFeatureToSectorList(SEL_INDEX_HOVERED, sectorList);
			}
			else
			{
				for (s32 s = 0; s < count; s++)
				{
					addFeatureToSectorList(s, sectorList);
				}
			}
			if (sectorList.empty()) { return false; }
		}

		// Gather texture indices.
		std::set<s32> textureListIndices;
		const s32 sectorCount = (s32)sectorList.size();
		const s32* sectorIndex = sectorList.data();
		for (s32 s = 0; s < sectorCount; s++)
		{
			EditorSector* sector = &s_level.sectors[sectorIndex[s]];
			textureListIndices.insert(sector->floorTex.texIndex);
			textureListIndices.insert(sector->ceilTex.texIndex);

			const s32 wallCount = (s32)sector->walls.size();
			const EditorWall* wall = sector->walls.data();
			for (s32 w = 0; w < wallCount; w++, wall++)
			{
				for (s32 t = 0; t < WP_COUNT; t++)
				{
					if (wall->tex[t].texIndex >= 0)
					{
						textureListIndices.insert(wall->tex[t].texIndex);
					}
				}
			}
		}

		// Gather objects and entities.
		std::vector<std::string> pods;
		std::vector<std::string> sprites;
		std::vector<std::string> frames;
		std::vector<s32> objData;

		if (s_editMode == LEDIT_ENTITY)
		{
			const s32 objCount = (s32)objList.size();
			for (s32 i = 0; i < objCount; i++)
			{
				Entity* entity = &s_level.entities[objList[i]->entityId];
				s32 dataIndex = 0;
				// TODO: Handle other types.
				if (entity->type == ETYPE_FRAME || entity->type == ETYPE_SPRITE || entity->type == ETYPE_3D)
				{
					dataIndex = addObjAsset(entity->assetName, entity->type == ETYPE_FRAME ? frames : entity->type == ETYPE_SPRITE ? sprites : pods);
				}
				objData.push_back(dataIndex);
			}
		}
		else
		{
			sectorIndex = sectorList.data();
			for (s32 s = 0; s < sectorCount; s++)
			{
				EditorSector* sector = &s_level.sectors[sectorIndex[s]];
				const s32 objCount = (s32)sector->obj.size();
				const EditorObject* obj = sector->obj.data();
				for (s32 o = 0; o < objCount; o++, obj++)
				{
					Entity* entity = &s_level.entities[obj->entityId];

					s32 index = (s32)objList.size();
					objList.push_back(obj);
					s32 dataIndex = 0;
					// TODO: Handle other types.
					if (entity->type == ETYPE_FRAME || entity->type == ETYPE_SPRITE || entity->type == ETYPE_3D)
					{
						dataIndex = addObjAsset(entity->assetName, entity->type == ETYPE_FRAME ? frames : entity->type == ETYPE_SPRITE ? sprites : pods);
					}
					objData.push_back(dataIndex);
				}
			}
		}
				
		// Next add Texture list.
		const s32 textureCount = (s32)textureListIndices.size();
		if (textureCount > 0)
		{
			WRITE_TO_BUFFER("TEXTURES %d\r\n", textureCount);

			std::set<s32>::iterator iIndex = textureListIndices.begin();
			for (; iIndex != textureListIndices.end(); ++iIndex)
			{
				const s32 index = *iIndex;
				WRITE_TO_BUFFER("  TEXTURE: %s\r\n", s_level.textures[index].name.c_str());
			}
			buffer.append("\r\n");
		}
		
		// Add object data list.
		if (!objList.empty())
		{
			const s32 podCount = (s32)pods.size();
			WRITE_TO_BUFFER("PODS %d\r\n", podCount);

			const std::string* pod = pods.data();
			for (s32 p = 0; p < podCount; p++, pod++)
			{
				WRITE_TO_BUFFER("  POD: %s\r\n", pod->c_str());
			}
			buffer.append("\r\n");

			const s32 sprCount = (s32)sprites.size();
			WRITE_TO_BUFFER("SPRS %d\r\n", sprCount);

			const std::string* wax = sprites.data();
			for (s32 w = 0; w < sprCount; w++, wax++)
			{
				WRITE_TO_BUFFER("  SPR: %s\r\n", wax->c_str());
			}
			buffer.append("\r\n");

			const s32 fmeCount = (s32)frames.size();
			WRITE_TO_BUFFER("FMES %d\r\n", fmeCount);

			const std::string* frame = frames.data();
			for (s32 f = 0; f < fmeCount; f++, frame++)
			{
				WRITE_TO_BUFFER("  FME: %s\r\n", frame->c_str());
			}
			buffer.append("\r\n");
		}

		// Write out the sector data.
		// Note that texture indices will need to be remapped.
		sectorIndex = sectorList.data();
		WRITE_TO_BUFFER("NUMSECTORS %d\r\n", sectorCount);
		for (s32 s = 0; s < sectorCount; s++)
		{
			EditorSector* sector = &s_level.sectors[sectorIndex[s]];
			WRITE_TO_BUFFER("SECTOR %d\r\n", sector->id);
			WRITE_TO_BUFFER("  NAME %s\r\n", sector->name.c_str());
			WRITE_TO_BUFFER("  AMBIENT %u\r\n", sector->ambient);

			std::set<s32>::iterator iTex = textureListIndices.find(sector->floorTex.texIndex);
			s32 floorIndex = (s32)std::distance(textureListIndices.begin(), iTex);
			WRITE_TO_BUFFER("  FLOOR TEXTURE %d %f %f %d\r\n", floorIndex, sector->floorTex.offset.x, sector->floorTex.offset.z, 0);
			WRITE_TO_BUFFER("  FLOOR ALTITUDE %f\r\n", -sector->floorHeight);

			iTex = textureListIndices.find(sector->ceilTex.texIndex);
			s32 ceilIndex = (s32)std::distance(textureListIndices.begin(), iTex);
			WRITE_TO_BUFFER("  CEILING TEXTURE %d %f %f %d\r\n", ceilIndex, sector->ceilTex.offset.x, sector->ceilTex.offset.z, 0);
			WRITE_TO_BUFFER("  CEILING ALTITUDE %f\r\n", -sector->ceilHeight);
			WRITE_TO_BUFFER("  SECOND ALTITUDE %f\r\n", -sector->secHeight);
			WRITE_TO_BUFFER("  FLAGS %d %d %d\r\n", sector->flags[0], sector->flags[1], sector->flags[2]);
			WRITE_TO_BUFFER("  LAYER %d\r\n", sector->layer);
			buffer.append("\r\n");

			const s32 vtxCount = (s32)sector->vtx.size();
			const Vec2f* vtx = sector->vtx.data();
			WRITE_TO_BUFFER("  VERTICES %d\r\n", vtxCount);
			for (s32 v = 0; v < vtxCount; v++)
			{
				WRITE_TO_BUFFER("    X: %f Z: %f\r\n", vtx[v].x, vtx[v].z);
			}
			buffer.append("\r\n");

			const s32 wallCount = (s32)sector->walls.size();
			const EditorWall* wall = sector->walls.data();
			WRITE_TO_BUFFER("  WALLS %d\r\n", wallCount);
			for (s32 w = 0; w < wallCount; w++, wall++)
			{
				// Get the texture index.
				s32 texIndex[WP_COUNT];
				for (s32 t = 0; t < WP_COUNT; t++)
				{
					if (wall->tex[t].texIndex < 0)
					{
						texIndex[t] = -1;
					}
					else
					{
						std::set<s32>::iterator iTex = textureListIndices.find(wall->tex[t].texIndex);
						texIndex[t] = (s32)std::distance(textureListIndices.begin(), iTex);
					}
				}

				WRITE_TO_BUFFER("    WALL LEFT: %d RIGHT: %d MID: %d %f %f %d TOP: %d %f %f %d BOT: %d %f %f %d SIGN: %d %f %f ADJOIN: %d MIRROR: %d WALK: %d FLAGS: %d %d %d LIGHT: %d\r\n",
					wall->idx[0], wall->idx[1],
					texIndex[WP_MID], wall->tex[WP_MID].offset.x, wall->tex[WP_MID].offset.z, 0,
					texIndex[WP_TOP], wall->tex[WP_TOP].offset.x, wall->tex[WP_TOP].offset.z, 0,
					texIndex[WP_BOT], wall->tex[WP_BOT].offset.x, wall->tex[WP_BOT].offset.z, 0,
					texIndex[WP_SIGN], wall->tex[WP_SIGN].offset.x, wall->tex[WP_SIGN].offset.z,
					wall->adjoinId, wall->mirrorId, wall->adjoinId, wall->flags[0], wall->flags[1], wall->flags[2], wall->wallLight);
			}
			buffer.append("\r\n");
		}

		// Write out the object data.
		const s32 objectCount = (s32)objList.size();
		if (objectCount)
		{
			WRITE_TO_BUFFER("OBJECTS %d\r\n", objectCount);
			const f32 radToDeg = 360.0f / (2.0f * PI);
			for (s32 o = 0; o < objectCount; o++)
			{
				const EditorObject* obj = objList[o];
				const Entity* entity = &s_level.entities[obj->entityId];
				const f32 objYaw = fmodf(obj->angle * radToDeg, 360.0f);
				const f32 objPitch = fmodf(obj->pitch * radToDeg, 360.0f);
				const f32 objRoll = fmodf(obj->roll * radToDeg, 360.0f);
				const s32 logicCount = (s32)entity->logic.size();

				switch (entity->type)
				{
					case ETYPE_SPIRIT:
					{
						WRITE_TO_BUFFER("  CLASS: SPIRIT DATA: 0 X: %0.2f Y: %0.2f Z: %0.2f PCH: %0.2f YAW: %0.2f ROL: %0.2f DIFF: %d\r\n", obj->pos.x, -obj->pos.y, obj->pos.z, objPitch, objYaw, objRoll, obj->diff);
						writeObjSequenceToBuffer(obj, entity, buffer);
					} break;
					case ETYPE_SAFE:
					{
						WRITE_TO_BUFFER("  CLASS: SAFE DATA: 0 X: %0.2f Y: %0.2f Z: %0.2f PCH: %0.2f YAW: %0.2f ROL: %0.2f DIFF: %d\r\n", obj->pos.x, -obj->pos.y, obj->pos.z, objPitch, objYaw, objRoll, obj->diff);
						writeObjSequenceToBuffer(obj, entity, buffer);
					} break;
					case ETYPE_FRAME:
					{
						WRITE_TO_BUFFER("  CLASS: FRAME DATA: %d X: %0.2f Y: %0.2f Z: %0.2f PCH: %0.2f YAW: %0.2f ROL: %0.2f DIFF: %d\r\n", objData[o], obj->pos.x, -obj->pos.y, obj->pos.z, objPitch, objYaw, objRoll, obj->diff);
						writeObjSequenceToBuffer(obj, entity, buffer);
					} break;
					case ETYPE_SPRITE:
					{
						WRITE_TO_BUFFER("  CLASS: SPRITE DATA: %d X: %0.2f Y: %0.2f Z: %0.2f PCH: %0.2f YAW: %0.2f ROL: %0.2f DIFF: %d\r\n", objData[o], obj->pos.x, -obj->pos.y, obj->pos.z, objPitch, objYaw, objRoll, obj->diff);
						writeObjSequenceToBuffer(obj, entity, buffer);
					} break;
					case ETYPE_3D:
					{
						WRITE_TO_BUFFER("  CLASS: 3D DATA: %d X: %0.2f Y: %0.2f Z: %0.2f PCH: %0.2f YAW: %0.2f ROL: %0.2f DIFF: %d\r\n", objData[o], obj->pos.x, -obj->pos.y, obj->pos.z, objPitch, objYaw, objRoll, obj->diff);
						writeObjSequenceToBuffer(obj, entity, buffer);
					} break;
				}
			}
			buffer.append("\r\n");
		}

		return true;
	}

	void parseTexture(const TokenList& tokens, s32 offset, const std::vector<std::string>& textureList, LevelTexture* outTex, s32 defaultTexIndex)
	{
		char* endPtr = nullptr;
		const s32 texId = strtol(tokens[offset].c_str(), &endPtr, 10);
		const f32 offsetX = strtof(tokens[offset + 1].c_str(), &endPtr);
		const f32 offsetY = strtof(tokens[offset + 2].c_str(), &endPtr);

		outTex->texIndex = defaultTexIndex;
		outTex->offset = { offsetX, offsetY };
		if (texId >= 0 && texId < (s32)textureList.size())
		{
			bool isNewTexture = false;
			const s32 texIndex = getTextureIndex(textureList[texId].c_str(), &isNewTexture);
			if (texIndex >= 0)
			{
				outTex->texIndex = texIndex;
			}
		}
	}

	// TODO:
	//   Object data list.
	//   Objects.
	//   INF items?
	bool importFromText(const std::string& buffer, bool centerOnMouse)
	{
		const size_t len = buffer.length();
		const char* data = buffer.data();
		if (!len || !data) { return false; }
				
		TFE_Parser parser;
		parser.init(data, len);
		parser.enableBlockComments();
		parser.addCommentString("//");
		parser.addCommentString("#");

		std::vector<std::string> textureList;
		std::vector<std::string> podList;
		std::vector<std::string> spriteList;
		std::vector<std::string> frameList;
		std::vector<EditorSector> sectorList;
		std::vector<EditorObject> objList;
		EditorSector* curSector = nullptr;
		EditorObject* curObj = nullptr;
		EntityLogic* curLogic = nullptr;
		Entity objEntity = {};
				
		size_t bufferPos = 0;
		const char* line = parser.readLine(bufferPos, true);
		TokenList tokens;
		char* endPtr = nullptr;
		bool isNewTexture = false;
		while (line)
		{
			parser.tokenizeLine(line, tokens);
			const s32 tokenCount = (s32)tokens.size();

			if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "TEXTURES", strlen("TEXTURES")) == 0)
			{
				const s32 count = strtol(tokens[1].c_str(), &endPtr, 10);
				if (count > 0)
				{
					textureList.reserve(count);
				}
			}
			else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "TEXTURE:", strlen("TEXTURE:")) == 0)
			{
				textureList.push_back(tokens[1]);
			}
			else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "PODS", strlen("PODS")) == 0)
			{
				const s32 count = strtol(tokens[1].c_str(), &endPtr, 10);
				if (count > 0)
				{
					podList.reserve(count);
				}
			}
			else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "POD:", strlen("POD:")) == 0)
			{
				podList.push_back(tokens[1]);
			}
			else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "SPRS", strlen("SPRS")) == 0)
			{
				const s32 count = strtol(tokens[1].c_str(), &endPtr, 10);
				if (count > 0)
				{
					spriteList.reserve(count);
				}
			}
			else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "SPR:", strlen("SPR:")) == 0)
			{
				spriteList.push_back(tokens[1]);
			}
			else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "FMES", strlen("FMES")) == 0)
			{
				const s32 count = strtol(tokens[1].c_str(), &endPtr, 10);
				if (count > 0)
				{
					frameList.reserve(count);
				}
			}
			else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "FME:", strlen("FME:")) == 0)
			{
				frameList.push_back(tokens[1]);
			}
			else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "NUMSECTORS", strlen("NUMSECTORS")) == 0)
			{
				const s32 count = strtol(tokens[1].c_str(), &endPtr, 10);
				if (count > 0)
				{
					sectorList.reserve(count);
				}
			}
			else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "SECTOR", strlen("SECTOR")) == 0)
			{
				curObj = nullptr;

				sectorList.push_back({});
				curSector = &sectorList.back();
				curSector->id = strtol(tokens[1].c_str(), &endPtr, 10);
			}
			else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "OBJECTS", strlen("OBJECTS")) == 0)
			{
				const s32 count = strtol(tokens[1].c_str(), &endPtr, 10);
				if (count > 0)
				{
					objList.reserve(count);
				}
			}
			else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "CLASS:", strlen("CLASS:")) == 0)
			{
				// The previous object had no SEQEND.
				if (curObj)
				{
					// Does this entity exist as a loaded definition? If so, take that name.
					const s32 editorDefId = getEntityDefId(&objEntity);
					if (editorDefId >= 0)
					{
						objEntity = s_entityDefList[editorDefId];
					}
					else
					{
						loadSingleEntityData(&objEntity);
					}
					curObj->entityId = addEntityToLevel(&objEntity);
					curObj = nullptr;
				}

				curSector = nullptr;
				std::string className = tokens[1];
				objList.push_back({});
				curObj = &objList.back();

				const f32 degToRad = (2.0f * PI) / 360.0f;

				s32 dataIndex = 0;
				for (s32 t = 2; t < tokenCount - 1;)
				{
					const char* token = tokens[t].c_str();
					if (strncasecmp(token, "DATA:", strlen("DATA:")) == 0)
					{
						dataIndex = strtol(tokens[t + 1].c_str(), &endPtr, 10);
						t += 2;
					}
					else if (strncasecmp(token, "X:", strlen("X:")) == 0)
					{
						curObj->pos.x = strtof(tokens[t + 1].c_str(), &endPtr);
						t += 2;
					}
					else if (strncasecmp(token, "Y:", strlen("Y:")) == 0)
					{
						curObj->pos.y = -strtof(tokens[t + 1].c_str(), &endPtr);
						t += 2;
					}
					else if (strncasecmp(token, "Z:", strlen("Z:")) == 0)
					{
						curObj->pos.z = strtof(tokens[t + 1].c_str(), &endPtr);
						t += 2;
					}
					else if (strncasecmp(token, "PCH:", strlen("PCH:")) == 0)
					{
						curObj->pitch = strtof(tokens[t + 1].c_str(), &endPtr) * degToRad;
						t += 2;
					}
					else if (strncasecmp(token, "YAW:", strlen("YAW:")) == 0)
					{
						curObj->angle = strtof(tokens[t + 1].c_str(), &endPtr) * degToRad;
						t += 2;
					}
					else if (strncasecmp(token, "ROL:", strlen("ROL:")) == 0)
					{
						curObj->roll = strtof(tokens[t + 1].c_str(), &endPtr) * degToRad;
						t += 2;
					}
					else if (strncasecmp(token, "DIFF:", strlen("DIFF:")) == 0)
					{
						curObj->diff = strtol(tokens[t + 1].c_str(), &endPtr, 10);
						t += 2;
					}
					else
					{
						t++;
					}
				}
				compute3x3Rotation(&curObj->transform, curObj->angle, curObj->pitch, curObj->roll);

				objEntity = {};
				objEntity.logic.clear();
				KEYWORD classType = getKeywordIndex(className.c_str());
				switch (classType)
				{
					case KW_3D:
					{
						if (!podList.empty())
						{
							objEntity.type = ETYPE_3D;
							objEntity.assetName = podList[dataIndex];

							char name[256];
							entityNameFromAssetName(objEntity.assetName.c_str(), name);
							objEntity.name = name;
						}
					} break;
					case KW_SPRITE:
					{
						if (!spriteList.empty())
						{
							objEntity.type = ETYPE_SPRITE;
							objEntity.assetName = spriteList[dataIndex];

							char name[256];
							entityNameFromAssetName(objEntity.assetName.c_str(), name);
							objEntity.name = name;
						}
					} break;
					case KW_FRAME:
					{
						if (!frameList.empty())
						{
							objEntity.type = ETYPE_FRAME;
							objEntity.assetName = frameList[dataIndex];

							char name[256];
							entityNameFromAssetName(objEntity.assetName.c_str(), name);
							objEntity.name = name;
						}
					} break;
					case KW_SPIRIT:
					{
						objEntity.name = "Spirit";
						objEntity.type = ETYPE_SPIRIT;
						objEntity.assetName = "SpiritObject.png";
					} break;
					case KW_SOUND:
					{
						//objEntity.type = ETYPE_SOUND;
						//objEntity.assetName = "SpiritObject.png";
					} break;
					case KW_SAFE:
					{
						objEntity.name = "Safe";
						objEntity.type = ETYPE_SAFE;
						objEntity.assetName = "SafeObject.png";
					} break;
				}
			}
			else if (curSector)
			{
				if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "NAME", strlen("NAME")) == 0)
				{
					curSector->name = tokens[1];
				}
				else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "AMBIENT", strlen("AMBIENT")) == 0)
				{
					curSector->ambient = strtol(tokens[1].c_str(), &endPtr, 10);
				}
				else if (tokenCount >= 5 && strncasecmp(tokens[0].c_str(), "FLOOR", strlen("FLOOR")) == 0 && strncasecmp(tokens[1].c_str(), "TEXTURE", strlen("TEXTURE")) == 0)
				{
					parseTexture(tokens, 2, textureList, &curSector->floorTex, 0);
				}
				else if (tokenCount >= 5 && strncasecmp(tokens[0].c_str(), "CEILING", strlen("CEILING")) == 0 && strncasecmp(tokens[1].c_str(), "TEXTURE", strlen("TEXTURE")) == 0)
				{
					parseTexture(tokens, 2, textureList, &curSector->ceilTex, 0);
				}
				else if (tokenCount >= 3 && strncasecmp(tokens[0].c_str(), "FLOOR", strlen("FLOOR")) == 0 && strncasecmp(tokens[1].c_str(), "ALTITUDE", strlen("ALTITUDE")) == 0)
				{
					curSector->floorHeight = -strtof(tokens[2].c_str(), &endPtr);
				}
				else if (tokenCount >= 3 && strncasecmp(tokens[0].c_str(), "CEILING", strlen("CEILING")) == 0 && strncasecmp(tokens[1].c_str(), "ALTITUDE", strlen("ALTITUDE")) == 0)
				{
					curSector->ceilHeight = -strtof(tokens[2].c_str(), &endPtr);
				}
				else if (tokenCount >= 3 && strncasecmp(tokens[0].c_str(), "SECOND", strlen("SECOND")) == 0 && strncasecmp(tokens[1].c_str(), "ALTITUDE", strlen("ALTITUDE")) == 0)
				{
					curSector->secHeight = -strtof(tokens[2].c_str(), &endPtr);
				}
				else if (tokenCount >= 4 && strncasecmp(tokens[0].c_str(), "FLAGS", strlen("FLAGS")) == 0)
				{
					curSector->flags[0] = strtol(tokens[1].c_str(), &endPtr, 10);
					curSector->flags[1] = strtol(tokens[2].c_str(), &endPtr, 10);
					curSector->flags[2] = strtol(tokens[3].c_str(), &endPtr, 10);
				}
				else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "LAYER", strlen("LAYER")) == 0)
				{
					curSector->layer = strtol(tokens[1].c_str(), &endPtr, 10);
					if (s_level.layerRange[0] > curSector->layer) { s_level.layerRange[0] = curSector->layer; }
					if (s_level.layerRange[1] < curSector->layer) { s_level.layerRange[1] = curSector->layer; }
				}
				else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "VERTICES", strlen("VERTICES")) == 0)
				{
					s32 count = strtol(tokens[1].c_str(), &endPtr, 10);
					if (count > 0)
					{
						curSector->vtx.reserve(count);
					}
				}
				else if (tokenCount >= 4 && strncasecmp(tokens[0].c_str(), "X:", strlen("X:")) == 0 && strncasecmp(tokens[2].c_str(), "Z:", strlen("Z:")) == 0)
				{
					Vec2f vtx = { 0 };
					vtx.x = strtof(tokens[1].c_str(), &endPtr);
					vtx.z = strtof(tokens[3].c_str(), &endPtr);
					curSector->vtx.push_back(vtx);
				}
				else if (tokenCount >= 2 && strncasecmp(tokens[0].c_str(), "WALLS", strlen("WALLS")) == 0)
				{
					s32 count = strtol(tokens[1].c_str(), &endPtr, 10);
					if (count > 0)
					{
						curSector->walls.reserve(count);
					}
				}
				else if (tokenCount >= 5 && strncasecmp(tokens[0].c_str(), "WALL", strlen("WALL")) == 0)
				{
					EditorWall wall = {};
					for (s32 t = 1; t < tokenCount - 1;)
					{
						const char* token = tokens[t].c_str();
						if (strncasecmp(token, "LEFT:", strlen("LEFT:")) == 0)
						{
							wall.idx[0] = strtol(tokens[t + 1].c_str(), &endPtr, 10);
							t += 2;
						}
						else if (strncasecmp(token, "RIGHT:", strlen("RIGHT:")) == 0)
						{
							wall.idx[1] = strtol(tokens[t + 1].c_str(), &endPtr, 10);
							t += 2;
						}
						else if (strncasecmp(token, "MID:", strlen("MID:")) == 0 && t < tokenCount - 5)
						{
							parseTexture(tokens, t + 1, textureList, &wall.tex[WP_MID], 0);
							t += 5;
						}
						else if (strncasecmp(token, "TOP:", strlen("TOP:")) == 0 && t < tokenCount - 5)
						{
							parseTexture(tokens, t + 1, textureList, &wall.tex[WP_TOP], 0);
							t += 5;
						}
						else if (strncasecmp(token, "BOT:", strlen("BOT:")) == 0 && t < tokenCount - 5)
						{
							parseTexture(tokens, t + 1, textureList, &wall.tex[WP_BOT], 0);
							t += 5;
						}
						else if (strncasecmp(token, "SIGN:", strlen("SIGN:")) == 0 && t < tokenCount - 4)
						{
							parseTexture(tokens, t + 1, textureList, &wall.tex[WP_SIGN], -1);
							t += 4;
						}
						else if (strncasecmp(token, "ADJOIN:", strlen("ADJOIN:")) == 0)
						{
							wall.adjoinId = strtol(tokens[t + 1].c_str(), &endPtr, 10);
							t += 2;
						}
						else if (strncasecmp(token, "MIRROR:", strlen("MIRROR:")) == 0)
						{
							wall.mirrorId = strtol(tokens[t + 1].c_str(), &endPtr, 10);
							t += 2;
						}
						else if (strncasecmp(token, "WALK:", strlen("WALK:")) == 0)
						{
							// Unused
							t += 2;
						}
						else if (strncasecmp(token, "FLAGS:", strlen("FLAGS:")) == 0 && t < tokenCount - 4)
						{
							wall.flags[0] = strtol(tokens[t + 1].c_str(), &endPtr, 10);
							wall.flags[1] = strtol(tokens[t + 2].c_str(), &endPtr, 10);
							wall.flags[2] = strtol(tokens[t + 3].c_str(), &endPtr, 10);
							t += 4;
						}
						else if (strncasecmp(token, "LIGHT:", strlen("LIGHT:")) == 0)
						{
							wall.wallLight = strtol(tokens[t + 1].c_str(), &endPtr, 10);
							t += 2;
						}
						else
						{
							// Unknown, keep stepping through the list.
							t++;
						}
					}
					curSector->walls.push_back(wall);
				}
			}
			else if (curObj)
			{
				if (tokenCount >= 1 && strncasecmp(tokens[0].c_str(), "SEQEND", strlen("SEQEND")) == 0)
				{
					// Does this entity exist as a loaded definition? If so, take that name.
					const s32 editorDefId = getEntityDefId(&objEntity);
					if (editorDefId >= 0)
					{
						objEntity = s_entityDefList[editorDefId];
					}
					else
					{
						loadSingleEntityData(&objEntity);
					}
					curObj->entityId = addEntityToLevel(&objEntity);
					curObj = nullptr;
					curLogic = nullptr;
				}
				else if (tokenCount >= 1 && strncasecmp(tokens[0].c_str(), "SEQ", strlen("SEQ")) == 0)
				{
					// Start the sequence.
					curLogic = nullptr;
				}
				else if (tokenCount >= 2 && (strncasecmp(tokens[0].c_str(), "LOGIC:", strlen("LOGIC:")) == 0 || strncasecmp(tokens[0].c_str(), "TYPE:", strlen("TYPE:")) == 0))
				{
					objEntity.logic.push_back({});
					curLogic = &objEntity.logic.back();

					KEYWORD logicId = getKeywordIndex(tokens[1].c_str());
					if (logicId == KW_PLAYER)  // Player Logic.
					{
						curLogic->name = s_logicDefList[0].name;
						objEntity.assetName = "PlayerStart.png";
					}
					else if (logicId == KW_ANIM)	// Animated Sprites Logic.
					{
						curLogic->name = s_logicDefList[1].name;
					}
					else if (logicId == KW_UPDATE)	// "Update" logic is usually used for rotating 3D objects, like the Death Star.
					{
						curLogic->name = s_logicDefList[2].name;
					}
					else if (logicId >= KW_TROOP && logicId <= KW_SCENERY)	// Enemies, explosives barrels, land mines, and scenery.
					{
						curLogic->name = s_logicDefList[3 + logicId - KW_TROOP].name;
					}
					else if (logicId == KW_KEY)         // Vue animation logic.
					{
						curLogic->name = "Key"; // 38
					}
					else if (logicId == KW_GENERATOR && tokenCount >= 3)	// Enemy generator, used for in-level enemy spawning.
					{
						curLogic->name = "Generator"; // 39
						objEntity.name = "Generator";
						objEntity.assetName = "STORMFIN.WAX";
						// Add a variable for the type.
						EntityVar genType;
						genType.defId = getVariableId("GenType");
						genType.value.sValue = tokens[2];
						curLogic->var.push_back(genType);
					}
					else if (logicId == KW_DISPATCH)
					{
						curLogic->name = "Dispatch"; // 40
					}
					else if (logicId == KW_ITEM && tokenCount >= 3)
					{
						// Item LogicName
						curLogic->name = tokens[2];
					}
					else  // Everything else.
					{
						curLogic->name = tokens[1];
					}
				}
				else if (tokenCount >= 2)
				{
					char varName[256];
					strcpy(varName, tokens[0].c_str());
					s32 len = (s32)strlen(varName);
					while (varName[len - 1] == ':')
					{
						varName[len - 1] = 0;
						len--;
					}

					const s32 varId = getVariableId(varName);
					if (varId >= 0)
					{
						const EntityVarDef* def = getEntityVar(varId);
						EntityVar* var = nullptr;
						if (curLogic)
						{
							curLogic->var.push_back({});
							var = &curLogic->var.back();
						}
						else
						{
							objEntity.var.push_back({});
							var = &objEntity.var.back();
						}
						var->defId = varId;

						TokenList varTokens;
						varTokens.push_back(varName);
						varTokens.push_back(tokens[1]);
						if (tokenCount >= 3) { varTokens.push_back(tokens[2]); }
						parseValue(varTokens, def->type, &var->value);
					}
				}
			}

			line = parser.readLine(bufferPos);
		}

		// Final object?
		if (curObj)
		{
			// Does this entity exist as a loaded definition? If so, take that name.
			const s32 editorDefId = getEntityDefId(&objEntity);
			if (editorDefId >= 0)
			{
				objEntity = s_entityDefList[editorDefId];
			}
			else
			{
				loadSingleEntityData(&objEntity);
			}
			curObj->entityId = addEntityToLevel(&objEntity);
			curObj = nullptr;
		}

		// Parse the results.
		// 1. Create an ID map between new sector IDs and IDs in the data.
		std::map<s32, s32> idMapping;
		const s32 sectorCount = (s32)sectorList.size();
		EditorSector* sector = sectorList.data();
		s32 newId = (s32)s_level.sectors.size();
		for (s32 s = 0; s < sectorCount; s++, sector++)
		{
			std::map<s32, s32>::iterator idMap = idMapping.find(sector->id);
			if (idMap == idMapping.end())
			{
				idMapping[sector->id] = newId;
				sector->id = newId;
				newId++;
			}
			else
			{
				sector->id = idMap->second;
			}
		}

		// 2. Iterate through adjoins, clear any with no mapping, update IDs based on mapping.
		sector = sectorList.data();
		for (s32 s = 0; s < sectorCount; s++, sector++)
		{
			const s32 wallCount = (s32)sector->walls.size();
			EditorWall* wall = sector->walls.data();
			for (s32 w = 0; w < wallCount; w++, wall++)
			{
				if (wall->adjoinId < 0) { continue; }
				std::map<s32, s32>::iterator idMap = idMapping.find(wall->adjoinId);
				if (idMap == idMapping.end())
				{
					// Adjoins to outside of the data, so clear.
					wall->adjoinId = -1;
					wall->mirrorId = -1;
				}
				else
				{
					// Fixup the adjoin to use the new ID.
					wall->adjoinId = idMap->second;
				}
			}
		}

		// 3. Move sectors so they are centered on the mouse position.
		const s32 objCount = (s32)objList.size();
		sector = sectorList.data();
		Vec3f offset = { 0 };
		if (centerOnMouse)
		{
			Vec3f bounds[2] = { {FLT_MAX, FLT_MAX, FLT_MAX}, {-FLT_MAX, -FLT_MAX, -FLT_MAX} };
			if (sectorCount > 0)
			{
				for (s32 s = 0; s < sectorCount; s++, sector++)
				{
					const s32 vtxCount = (s32)sector->vtx.size();
					const Vec2f* vtx = sector->vtx.data();
					for (s32 v = 0; v < vtxCount; v++, vtx++)
					{
						bounds[0].x = std::min(bounds[0].x, vtx->x);
						bounds[0].z = std::min(bounds[0].z, vtx->z);

						bounds[1].x = std::max(bounds[1].x, vtx->x);
						bounds[1].z = std::max(bounds[1].z, vtx->z);
					}
					bounds[0].y = std::min(bounds[0].y, sector->floorHeight);
					bounds[1].y = std::max(bounds[1].y, sector->ceilHeight);
				}
			}
			else if (objCount > 0)
			{
				EditorObject* obj = objList.data();
				for (s32 s = 0; s < objCount; s++, obj++)
				{
					bounds[0].x = std::min(bounds[0].x, obj->pos.x);
					bounds[0].z = std::min(bounds[0].z, obj->pos.z);

					bounds[1].x = std::max(bounds[1].x, obj->pos.x);
					bounds[1].z = std::max(bounds[1].z, obj->pos.z);

					bounds[0].y = std::min(bounds[0].y, obj->pos.y);
					bounds[1].y = std::max(bounds[1].y, obj->pos.y);
				}
			}
			else
			{
				bounds[0] = { 0 };
				bounds[1] = { 0 };
			}

			Vec3f center = { (bounds[0].x + bounds[1].x) * 0.5f, bounds[0].y, (bounds[0].z + bounds[1].z) * 0.5f };
			snapToGrid(&center);

			f32 yBase = s_view == EDIT_VIEW_3D ? s_cursor3d.y : s_grid.height;
			offset = { s_cursor3d.x - center.x, yBase - center.y, s_cursor3d.z - center.z };
		}
		// Snap to grid.
		sector = sectorList.data();
		for (s32 s = 0; s < sectorCount; s++, sector++)
		{
			sector->floorHeight += offset.y;
			sector->ceilHeight += offset.y;
			sector->groupId = groups_getCurrentId();
			sector->groupIndex = 0;

			const s32 vtxCount = (s32)sector->vtx.size();
			Vec2f* vtx = sector->vtx.data();
			for (s32 v = 0; v < vtxCount; v++, vtx++)
			{
				vtx->x += offset.x;
				vtx->z += offset.z;
			}

			sectorToPolygon(sector);
		}

		// 4. Add the new sectors to the level.
		selection_clear();
		selection_clearHovered();

		if (s_editMode == LEDIT_WALL || s_editMode == LEDIT_SECTOR)
		{
			sector = sectorList.data();
			for (s32 s = 0; s < sectorCount; s++, sector++)
			{
				s_level.sectors.push_back(*sector);
				selection_action(SA_ADD, sector);
			}
		}

		// 5. Add objects.
		s32 validObjCount = 0;
		EditorObject* obj = objList.data();
		for (s32 i = 0; i < objCount; i++, obj++)
		{
			obj->pos.x += offset.x;
			obj->pos.y += offset.y;
			obj->pos.z += offset.z;

			EditorSector* sector = findSectorDf(obj->pos);
			if (!sector)
			{
				obj->pos.y = s_view == EDIT_VIEW_3D ? s_cursor3d.y : s_grid.height;
				sector = findSectorDf(obj->pos);
			}
			if (!sector)
			{
				const Vec2f pos = { obj->pos.x, obj->pos.z };
				sector = findSectorDf(pos);
				if (sector)
				{
					obj->pos.y = sector->floorHeight;
				}
			}

			if (sector)
			{
				if (s_editMode == LEDIT_ENTITY)
				{
					if (s_view == EDIT_VIEW_3D)
					{
						obj->pos.y = s_cursor3d.y;
					}
					else
					{
						obj->pos.y = sector->floorHeight;
					}
				}

				sector->obj.push_back(*obj);
				validObjCount++;

				if (s_editMode == LEDIT_ENTITY)
				{
					selection_action(SA_ADD, sector, (s32)sector->obj.size() - 1);
				}
			}
		}
				
		// Create a snapshot.
		if (sectorCount || validObjCount)
		{
			levHistory_createSnapshot("Paste");
		}
		return true;
	}

	EditorTexture* getTexture(s32 index)
	{
		if (index < 0 || index >= (s32)s_level.textures.size()) { return nullptr; }
		return (EditorTexture*)getAssetData(s_level.textures[index].handle);
	}

	Asset* getTextureAssetByName(const char* name)
	{
		const s32 count = (s32)s_levelTextureList.size();
		Asset* asset = s_levelTextureList.data();
		for (s32 i = 0; i < count; i++)
		{
			if (strcasecmp(asset[i].name.c_str(), name) == 0)
			{
				return &asset[i];
			}
		}
		return nullptr;
	}

	s32 getTextureIndex(const char* name, bool* isNewTexture)
	{
		if (isNewTexture) { *isNewTexture = false; }

		const s32 count = (s32)s_level.textures.size();
		const LevelTextureAsset* texAsset = s_level.textures.data();
		for (s32 i = 0; i < count; i++, texAsset++)
		{
			if (strcasecmp(texAsset->name.c_str(), name) == 0)
			{
				return i;
			}
		}
		Asset* asset = getTextureAssetByName(name);
		if (asset)
		{
			s32 newId = count;
			LevelTextureAsset newTex;
			newTex.name = name;
			newTex.handle = asset->handle;
			s_level.textures.push_back(newTex);
			if (isNewTexture) { *isNewTexture = true; }
			return newId;
		}
		return -1;
	}

	// Update the sector's polygon from the sector data.
	void sectorToPolygon(EditorSector* sector)
	{
		Polygon& poly = sector->poly;
		poly.edge.resize(sector->walls.size());
		poly.vtx.resize(sector->vtx.size());

		poly.bounds[0] = {  FLT_MAX,  FLT_MAX };
		poly.bounds[1] = { -FLT_MAX, -FLT_MAX };

		const size_t vtxCount = sector->vtx.size();
		const Vec2f* vtx = sector->vtx.data();
		for (size_t v = 0; v < vtxCount; v++, vtx++)
		{
			poly.vtx[v] = *vtx;
			poly.bounds[0].x = std::min(poly.bounds[0].x, vtx->x);
			poly.bounds[0].z = std::min(poly.bounds[0].z, vtx->z);
			poly.bounds[1].x = std::max(poly.bounds[1].x, vtx->x);
			poly.bounds[1].z = std::max(poly.bounds[1].z, vtx->z);
		}

		const size_t wallCount = sector->walls.size();
		const EditorWall* wall = sector->walls.data();
		for (size_t w = 0; w < wallCount; w++, wall++)
		{
			poly.edge[w] = { wall->idx[0], wall->idx[1] };
		}

		// Clear out cached triangle data.
		poly.triVtx.clear();
		poly.triIdx.clear();

		TFE_Polygon::computeTriangulation(&sector->poly);

		// Update the sector bounds.
		sector->bounds[0] = { poly.bounds[0].x, 0.0f, poly.bounds[0].z };
		sector->bounds[1] = { poly.bounds[1].x, 0.0f, poly.bounds[1].z };
		sector->bounds[0].y = min(sector->floorHeight, sector->ceilHeight);
		sector->bounds[1].y = max(sector->floorHeight, sector->ceilHeight);
	}

	// Update the sector itself from the sector's polygon.
	void polygonToSector(EditorSector* sector)
	{
		// TODO
	}

	s32 findSectorByName(const char* name, s32 excludeId)
	{
		if (s_level.sectors.empty() || !name || name[0] == 0) { return -1; }
		const s32 sectorCount = (s32)s_level.sectors.size();
		EditorSector* sector = s_level.sectors.data();
		for (s32 i = 0; i < sectorCount; i++, sector++)
		{
			if (sector->name.empty() || excludeId == sector->id) { continue; }
			if (strcasecmp(name, sector->name.c_str()) == 0)
			{
				return i;
			}
		}
		return -1;
	}

	EditorSector* findSector2d(Vec2f pos)
	{
		if (s_level.sectors.empty()) { return nullptr; }

		const s32 sectorCount = (s32)s_level.sectors.size();
		EditorSector* sectors = s_level.sectors.data();

		for (s32 i = 0; i < sectorCount; i++)
		{
			if (!sector_isInteractable(&sectors[i]) || !sector_onActiveLayer(&sectors[i])) { continue; }
			if (TFE_Polygon::pointInsidePolygon(&sectors[i].poly, pos))
			{
				return &sectors[i];
			}
		}
		return nullptr;
	}

	EditorSector* findSector2d_closestHeight(Vec2f pos, f32 height)
	{
		if (s_level.sectors.empty()) { return nullptr; }

		const s32 sectorCount = (s32)s_level.sectors.size();
		EditorSector* sector = s_level.sectors.data();

		EditorSector* firstHit = nullptr;
		EditorSector* closestInside = nullptr;
		EditorSector* closestOutside = nullptr;
		f32 distFromFloorInside = FLT_MAX;
		f32 distFromFloorOutside = FLT_MAX;

		for (s32 i = 0; i < sectorCount; i++, sector++)
		{
			if (!sector_isInteractable(sector) || !sector_onActiveLayer(sector)) { continue; }
			if (TFE_Polygon::pointInsidePolygon(&sector->poly, pos))
			{
				if (!firstHit) { firstHit = sector; }
				if (height >= sector->floorHeight && height <= sector->ceilHeight)
				{
					const f32 dH = height - sector->floorHeight;
					if (dH < distFromFloorInside)
					{
						closestInside = sector;
						distFromFloorInside = dH;
					}
				}
				else
				{
					const f32 dH = fabsf(height - sector->floorHeight);
					if (dH < distFromFloorOutside)
					{
						closestOutside = sector;
						distFromFloorOutside = dH;
					}
				}
			}
		}

		if (closestInside) { return closestInside; }
		if (closestOutside) { return closestOutside; }
		return firstHit;
	}

	bool rayHitAABB(const Ray* ray, const Vec3f* bounds)
	{
		enum
		{
			LEFT = 0,
			RIGHT,
			MID,
		};

		// Pick representative planes.
		s32 quadrant[3];
		f32 candidatePlane[3];
		bool inside = true;
		for (s32 i = 0; i < 3; i++)
		{
			quadrant[i] = MID;
			if (ray->origin.m[i] < bounds[0].m[i])
			{
				quadrant[i] = LEFT;
				candidatePlane[i] = bounds[0].m[i];
				inside = false;
			}
			else if (ray->origin.m[i] > bounds[0].m[i])
			{
				quadrant[i] = RIGHT;
				candidatePlane[i] = bounds[1].m[i];
				inside = false;
			}
		}
		// The ray starts inside the bounds, so we're done.
		if (inside) { return true; }

		// Calcuate the distance to the candidate planes.
		f32 maxT[3];
		for (s32 i = 0; i < 3; i++)
		{
			maxT[i] = -1.0f;
			if (quadrant[i] != MID && ray->dir.m[i] != 0.0f)
			{
				maxT[i] = (candidatePlane[i] - ray->origin.m[i]) / ray->dir.m[i];
			}
		}

		// Get the largest dist
		s32 planeId = 0;
		for (s32 i = 1; i < 3; i++)
		{
			if (maxT[planeId] < maxT[i]) { planeId = i; }
		}

		// Make sure it is really inside.
		if (maxT[planeId] < 0.0f) { return false; }
		Vec3f coord = { 0 };
		for (s32 i = 0; i < 3; i++)
		{
			if (planeId != i)
			{
				coord.m[i] = ray->origin.m[i] + maxT[planeId] * ray->dir.m[i];
				if (coord.m[i] < bounds[0].m[i] || coord.m[i] > bounds[1].m[i])
				{
					return false;
				}
			}
			else
			{
				coord.m[i] = candidatePlane[i];
			}
		}
		return true;
	}

	f32 getWallLength(const EditorSector* sector, const EditorWall* wall)
	{
		const Vec2f* v0 = &sector->vtx[wall->idx[0]];
		const Vec2f* v1 = &sector->vtx[wall->idx[1]];
		const Vec2f delta = { v1->x - v0->x, v1->z - v0->z };
		return sqrtf(delta.x*delta.x + delta.z*delta.z);
	}

	bool getSignExtents(const EditorSector* sector, const EditorWall* wall, Vec2f ext[2])
	{
		if (wall->tex[WP_SIGN].texIndex < 0) { return false; }

		f32 uOffset = wall->tex[WP_MID].offset.x;
		f32 vOffset = sector->floorHeight;
		if (wall->adjoinId >= 0)
		{
			const EditorSector* next = &s_level.sectors[wall->adjoinId];
			if (next->floorHeight > sector->floorHeight)
			{
				uOffset = wall->tex[WP_BOT].offset.x;
			}
			else if (next->ceilHeight < sector->ceilHeight)
			{
				uOffset = wall->tex[WP_TOP].offset.x;
				vOffset = next->ceilHeight;
			}
		}

		bool hasSign = false;
		const EditorTexture* tex = getTexture(wall->tex[WP_SIGN].texIndex);
		if (tex)
		{
			ext[0].x = wall->tex[WP_SIGN].offset.x - uOffset;
			ext[1].x = ext[0].x + f32(tex->width) / 8.0f;
			ext[0].z = vOffset - wall->tex[WP_SIGN].offset.z;
			ext[1].z = ext[0].z + f32(tex->height) / 8.0f;
			hasSign = true;
		}
		return hasSign;
	}

	void centerSignOnSurface(const EditorSector* sector, EditorWall* wall)
	{
		if (wall->tex[WP_SIGN].texIndex < 0) { return; }
		const EditorTexture* signTex = getTexture(wall->tex[WP_SIGN].texIndex);
		if (!signTex) { return; }

		f32 uOffset = wall->tex[WP_MID].offset.x;
		f32 baseY = sector->floorHeight;
		f32 partHeight = std::max(0.0f, sector->ceilHeight - sector->floorHeight);
		if (wall->adjoinId >= 0)
		{
			const EditorSector* next = &s_level.sectors[wall->adjoinId];
			if (next->floorHeight > sector->floorHeight)
			{
				uOffset = wall->tex[WP_BOT].offset.x;
				partHeight = next->floorHeight - sector->floorHeight;
			}
			else if (next->ceilHeight < sector->ceilHeight)
			{
				uOffset = wall->tex[WP_TOP].offset.x;
				baseY = next->ceilHeight;
				partHeight = sector->ceilHeight - next->ceilHeight;
			}
		}

		f32 wallLen = getWallLength(sector, wall);
		wall->tex[WP_SIGN].offset.x = uOffset + std::max(0.0f, (wallLen - signTex->width/8.0f)*0.5f);
		wall->tex[WP_SIGN].offset.z = -std::max(0.0f, partHeight - signTex->height/8.0f) * 0.5f;
	}

	bool rayAABBIntersection(const Ray* ray, const Vec3f* bounds, f32* hitDist)
	{
		const f32 ix = fabsf(ray->dir.x) > FLT_EPSILON ? 1.0f/ray->dir.x : FLT_MAX;
		const f32 iy = fabsf(ray->dir.y) > FLT_EPSILON ? 1.0f/ray->dir.y : FLT_MAX;
		const f32 iz = fabsf(ray->dir.z) > FLT_EPSILON ? 1.0f/ray->dir.z : FLT_MAX;

		const f32 tx1 = (bounds[0].x - ray->origin.x) * ix;
		const f32 tx2 = (bounds[1].x - ray->origin.x) * ix;
		const f32 ty1 = (bounds[0].y - ray->origin.y) * iy;
		const f32 ty2 = (bounds[1].y - ray->origin.y) * iy;
		const f32 tz1 = (bounds[0].z - ray->origin.z) * iz;
		const f32 tz2 = (bounds[1].z - ray->origin.z) * iz;

		f32 tmin = -FLT_MAX;
		tmin = max(tmin, min(tx1, tx2));
		tmin = max(tmin, min(ty1, ty2));
		tmin = max(tmin, min(tz1, tz2));

		f32 tmax = FLT_MAX;
		tmax = min(tmax, max(tx1, tx2));
		tmax = min(tmax, max(ty1, ty2));
		tmax = min(tmax, max(tz1, tz2));

		*hitDist = tmin;
		return tmax >= tmin && tmax >= 0.0f;
	}

	bool rayOBBIntersection(const Ray* ray, const Vec3f* bounds, const Vec3f* pos, const Mat3* mtx, f32* hitDist)
	{
		// Transform the ray into AABB space using the inverse transform matrix.
		// For an affine transform, this is simply the transpose. Note: this will break with non-uniform scale.
		Ray aabbRay;
				
		// Transform the origin, relative to the position.
		Vec3f relOrigin = { ray->origin.x - pos->x, ray->origin.y - pos->y, ray->origin.z - pos->z };
		aabbRay.origin.x = relOrigin.x * mtx->m0.x + relOrigin.y * mtx->m1.x + relOrigin.z * mtx->m2.x;
		aabbRay.origin.y = relOrigin.x * mtx->m0.y + relOrigin.y * mtx->m1.y + relOrigin.z * mtx->m2.y;
		aabbRay.origin.z = relOrigin.x * mtx->m0.z + relOrigin.y * mtx->m1.z + relOrigin.z * mtx->m2.z;
		// Transform the direction.
		aabbRay.dir.x = ray->dir.x * mtx->m0.x + ray->dir.y * mtx->m1.x + ray->dir.z * mtx->m2.x;
		aabbRay.dir.y = ray->dir.x * mtx->m0.y + ray->dir.y * mtx->m1.y + ray->dir.z * mtx->m2.y;
		aabbRay.dir.z = ray->dir.x * mtx->m0.z + ray->dir.y * mtx->m1.z + ray->dir.z * mtx->m2.z;
		aabbRay.dir = TFE_Math::normalize(&aabbRay.dir);

		return rayAABBIntersection(&aabbRay, bounds, hitDist);
	}

	bool planeIntersectionInPolygon(EditorSector* sector, Vec3f hitPoint, f32 dist, HitPart part, f32& overallClosestHit, RayHitInfo* hitInfo)
	{
		bool rayInSector = false;
		// The ray hit the plane, but is it inside of the sector polygon?
		Vec2f testPt = { hitPoint.x, hitPoint.z };
		if (TFE_Polygon::pointInsidePolygon(&sector->poly, testPt))
		{
			overallClosestHit = dist;
			hitInfo->hitSectorId = sector->id;
			hitInfo->hitWallId = -1;
			hitInfo->hitObjId = -1;
			hitInfo->hitPart = part;
			hitInfo->hitPos = hitPoint;
			hitInfo->dist = overallClosestHit;
			rayInSector = true;
		}
		return rayInSector;
	}
			
	// Return true if a hit is found.
	bool traceRay(const Ray* ray, RayHitInfo* hitInfo, bool flipFaces, bool canHitSigns, bool canHitObjects)
	{
		EditorLevel* level = &s_level;
		if (level->sectors.empty()) { return false; }
		const s32 sectorCount = (s32)level->sectors.size();
		EditorSector* sector = level->sectors.data();

		f32 maxDist  = ray->maxDist;
		Vec3f origin = ray->origin;
		Vec2f p0xz = { origin.x, origin.z };
		Vec2f p1xz = { origin.x + ray->dir.x * maxDist, origin.z + ray->dir.z * maxDist };
		Vec2f dirxz = { ray->dir.x, ray->dir.z };

		f32 overallClosestHit = FLT_MAX;
		hitInfo->hitSectorId = -1;
		hitInfo->hitWallId = -1;
		hitInfo->hitObjId = -1;
		hitInfo->hitPart = HP_MID;
		hitInfo->hitPos = { 0 };
		hitInfo->dist = FLT_MAX;

		// Loop through sectors in the world.
		for (s32 s = 0; s < sectorCount; s++, sector++)
		{
			if (!sector_isInteractable(sector) || !sector_onActiveLayer(sector)) { continue; }

			// Check the bounds.
			//if (!rayHitAABB(ray, sector->bounds)) { continue; }

			const bool isSectorSloped = (sector->flags[0] & SEC_FLAGS1_SLOPEDFLOOR) != 0 || (sector->flags[0] & SEC_FLAGS1_SLOPEDCEILING) != 0;

			// Now check against the walls.
			const u32 wallCount = (u32)sector->walls.size();
			const EditorWall* wall = sector->walls.data();
			const Vec2f* vtx = sector->vtx.data();
			f32 closestHit = FLT_MAX;
			s32 closestWallId = -1;
			bool rayInSector = false;
			HitPart closestPart = HP_MID;
			for (u32 w = 0; w < wallCount; w++, wall++)
			{
				const Vec2f* v0 = &vtx[wall->idx[0]];
				const Vec2f* v1 = &vtx[wall->idx[1]];
				Vec2f nrm = { -(v1->z - v0->z), v1->x - v0->x };
				if (flipFaces && TFE_Math::dot(&dirxz, &nrm) > 0.0f) { continue; }
				else if (!flipFaces && TFE_Math::dot(&dirxz, &nrm) < 0.0f) { continue; }

				EditorSector* next = nullptr;
				bool isSloped = isSectorSloped;
				if (wall->adjoinId >= 0 && wall->adjoinId < (s32)s_level.sectors.size())
				{
					next = &s_level.sectors[wall->adjoinId];
					isSloped = isSloped || (next->flags[0] & SEC_FLAGS1_SLOPEDFLOOR) != 0 || (next->flags[0] & SEC_FLAGS1_SLOPEDCEILING) != 0;
				}

				f32 s, t;
				if (TFE_Math::lineSegmentIntersect(&p0xz, &p1xz, v0, v1, &s, &t))
				{
					if (s < closestHit)
					{
						const f32 yAtHit = origin.y + ray->dir.y * s * maxDist;
						if (isSloped)
						{
							const f32 f00 = getFloorAtXZ(sector, *v0);
							const f32 f01 = getFloorAtXZ(sector, *v1);
							const f32 c00 = getCeilAtXZ(sector, *v0);
							const f32 c01 = getCeilAtXZ(sector, *v1);

							const f32 dx = v1->x - v0->x;
							const f32 dz = v1->z - v0->z;
							f32 u = 0.0f;
							if (fabsf(dx) >= fabsf(dz))
							{
								u = (origin.x + ray->dir.x * s * maxDist - v0->x) / dx;
							}
							else
							{
								u = (origin.z + ray->dir.z * s * maxDist - v0->z) / dz;
							}
							u = std::max(0.0f, std::min(1.0f, u));
							const f32 floor = f00 * (1.0f - u) + f01 * u;
							const f32 ceil = c00 * (1.0f - u) + c01 * u;
							if (yAtHit > floor - FLT_EPSILON && yAtHit < ceil + FLT_EPSILON)
							{
								bool canHit = true;
								HitPart part = HP_MID;
								if (wall->adjoinId >= 0 && next)
								{
									canHit = false;
									const f32 f10 = getFloorAtXZ(next, *v0);
									const f32 f11 = getFloorAtXZ(next, *v1);
									const f32 c10 = getCeilAtXZ(next, *v0);
									const f32 c11 = getCeilAtXZ(next, *v1);
									const f32 nextFloor = f10 * (1.0f - u) + f11 * u;
									const f32 nextCeil = c10 * (1.0f - u) + c11 * u;
									if (yAtHit <= nextFloor)
									{
										part = HP_BOT;
										canHit = true;
									}
									else if (yAtHit >= nextCeil)
									{
										part = HP_TOP;
										canHit = true;
									}
									else if (wall->flags[0] & WF1_ADJ_MID_TEX)
									{
										part = HP_MID;
										canHit = true;
									}
								}
								if (canHit)
								{
									closestHit = s;
									closestWallId = w;
									closestPart = part;
								}
							}
						}
						else if (yAtHit > sector->floorHeight - FLT_EPSILON && yAtHit < sector->ceilHeight + FLT_EPSILON)
						{
							bool canHit = true;
							HitPart part = HP_MID;
							if (wall->adjoinId >= 0 && next)
							{
								canHit = false;
								if (yAtHit <= next->floorHeight)
								{
									part = HP_BOT;
									canHit = true;
								}
								else if (yAtHit >= next->ceilHeight)
								{
									part = HP_TOP;
									canHit = true;
								}
								else if (wall->flags[0] & WF1_ADJ_MID_TEX)
								{
									part = HP_MID;
									canHit = true;
								}
							}
							if (canHit)
							{
								closestHit = s;
								closestWallId = w;
								closestPart = part;
							}
						}
						rayInSector = true;
					}
				}
			}

			// Test the closest wall
			wall = sector->walls.data();
			if (closestWallId >= 0)
			{
				closestHit *= maxDist;
				const Vec3f hitPoint = { origin.x + ray->dir.x*closestHit, origin.y + ray->dir.y*closestHit, origin.z + ray->dir.z*closestHit };

				Vec2f signExt[2];
				f32 hitU = 0.0f;
				const bool hasSign = canHitSigns && getSignExtents(sector, &wall[closestWallId], signExt);
				bool hitSign = false;
				if (hasSign)
				{
					const Vec2f* v0 = &sector->vtx[wall[closestWallId].idx[0]];
					const Vec2f* v1 = &sector->vtx[wall[closestWallId].idx[1]];
					Vec2f wallDir = { v1->x - v0->x, v1->z - v0->z };
					wallDir = TFE_Math::normalize(&wallDir);

					if (fabsf(wallDir.x) >= fabsf(wallDir.z))
					{
						hitU = (hitPoint.x - v0->x) / wallDir.x;
					}
					else
					{
						hitU = (hitPoint.z - v0->z) / wallDir.z;
					}

					hitSign = hitU >= signExt[0].x && hitU < signExt[1].x && 
						hitPoint.y >= signExt[0].z && hitPoint.y < signExt[1].z;
				}

				if (closestHit < overallClosestHit)
				{
					overallClosestHit = closestHit;
					hitInfo->hitSectorId = sector->id;
					hitInfo->hitWallId = closestWallId;
					hitInfo->hitObjId = -1;
					hitInfo->hitPart = hitSign ? HP_SIGN : closestPart;
					hitInfo->hitPos = hitPoint;
					hitInfo->dist = closestHit;
				}
			}

			// Test the floor and ceiling planes.
			const Vec3f planeTest = { origin.x + ray->dir.x*maxDist, origin.y + ray->dir.y*maxDist, origin.z + ray->dir.z*maxDist };
			Vec3f hitPoint;

			if (sector->flags[0] & SEC_FLAGS1_SLOPEDFLOOR)
			{
				updateSlopeData(sector);

				const bool canHitFloor = (!flipFaces && origin.y > sector->floorPlane.minHeight && ray->dir.y < 0.0f) ||
					(flipFaces && origin.y < sector->floorPlane.maxHeight && ray->dir.y > 0.0f);

				f32 dist;
				if (canHitFloor && rayPlaneIntersection(origin, ray->dir, sector->floorPlane.plane, dist))
				{
					if (overallClosestHit == FLT_MAX || dist < overallClosestHit)
					{
						// The ray hit the plane, but is it inside of the sector polygon?
						hitPoint = Vec3f{ origin.x + ray->dir.x*dist, origin.y + ray->dir.y*dist, origin.z + ray->dir.z*dist };
						rayInSector |= planeIntersectionInPolygon(sector, hitPoint, dist, HP_FLOOR, overallClosestHit, hitInfo);
					}
				}
			}
			else
			{
				const bool canHitFloor = (!flipFaces && origin.y > sector->floorHeight && ray->dir.y < 0.0f) ||
					(flipFaces && origin.y < sector->floorHeight && ray->dir.y > 0.0f);
				if (canHitFloor && TFE_Math::lineYPlaneIntersect(&origin, &planeTest, sector->floorHeight, &hitPoint))
				{
					const Vec3f offset = { hitPoint.x - origin.x, hitPoint.y - origin.y, hitPoint.z - origin.z };
					const f32 distSq = TFE_Math::dot(&offset, &offset);
					if (overallClosestHit == FLT_MAX || distSq < overallClosestHit*overallClosestHit)
					{
						// The ray hit the plane, but is it inside of the sector polygon?
						rayInSector |= planeIntersectionInPolygon(sector, hitPoint, sqrtf(distSq), HP_FLOOR, overallClosestHit, hitInfo);
					}
				}
			}

			if (sector->flags[0] & SEC_FLAGS1_SLOPEDCEILING)
			{
				updateSlopeData(sector);

				const bool canHitCeil = (!flipFaces && origin.y < sector->ceilPlane.maxHeight && ray->dir.y > 0.0f) ||
					(flipFaces && origin.y > sector->ceilPlane.minHeight && ray->dir.y < 0.0f);

				f32 dist;
				if (canHitCeil && rayPlaneIntersection(origin, ray->dir, sector->ceilPlane.plane, dist))
				{
					if (overallClosestHit == FLT_MAX || dist < overallClosestHit)
					{
						// The ray hit the plane, but is it inside of the sector polygon?
						hitPoint = Vec3f{ origin.x + ray->dir.x*dist, origin.y + ray->dir.y*dist, origin.z + ray->dir.z*dist };
						rayInSector |= planeIntersectionInPolygon(sector, hitPoint, dist, HP_CEIL, overallClosestHit, hitInfo);
					}
				}
			}
			else
			{
				const bool canHitCeil = (!flipFaces && origin.y < sector->ceilHeight && ray->dir.y > 0.0f) ||
					(flipFaces && origin.y > sector->ceilHeight && ray->dir.y < 0.0f);
				if (canHitCeil && TFE_Math::lineYPlaneIntersect(&origin, &planeTest, sector->ceilHeight, &hitPoint))
				{
					const Vec3f offset = { hitPoint.x - origin.x, hitPoint.y - origin.y, hitPoint.z - origin.z };
					const f32 distSq = TFE_Math::dot(&offset, &offset);
					if (overallClosestHit == FLT_MAX || distSq < overallClosestHit*overallClosestHit)
					{
						// The ray hit the plane, but is it inside of the sector polygon?
						rayInSector |= planeIntersectionInPolygon(sector, hitPoint, sqrtf(distSq), HP_CEIL, overallClosestHit, hitInfo);
					}
				}
			}

			// Objects
			if (rayInSector && canHitObjects)
			{
				const s32 objCount = (s32)sector->obj.size();
				const EditorObject* obj = sector->obj.data();
				for (s32 o = 0; o < objCount; o++, obj++)
				{
					const Entity* entity = &s_level.entities[obj->entityId];
					Vec3f pos = obj->pos;

					f32 dist;
					bool hitBounds = false;
					if (entity->type == ETYPE_3D)
					{
						hitBounds = rayOBBIntersection(ray, entity->obj3d->bounds, &pos, &obj->transform, &dist);
					}
					else
					{
						// If the entity is on the floor, make sure it doesn't stick through it for editor selection.
						if (entity->type != ETYPE_SPIRIT && entity->type != ETYPE_SAFE)
						{
							f32 offset = -(entity->offset.y + fabsf(entity->st[1].z - entity->st[0].z)) * 0.1f;
							if (fabsf(pos.y - sector->floorHeight) < 0.5f) { offset = max(0.0f, offset); }
							pos.y += offset;
						}
						const f32 width = entity->size.x * 0.5f;
						const f32 height = entity->size.z;
						const Vec3f bounds[2] = { {pos.x - width, pos.y, pos.z - width}, {pos.x + width, pos.y + height, pos.z + width} };
						hitBounds = rayAABBIntersection(ray, bounds, &dist);
					}

					if (hitBounds && dist < overallClosestHit)
					{
						overallClosestHit = dist;
						hitInfo->hitSectorId = sector->id;
						hitInfo->hitWallId = -1;
						hitInfo->dist = overallClosestHit;
						hitInfo->hitObjId = o;
						hitInfo->hitPos = { ray->origin.x + ray->dir.x*dist, ray->origin.y + ray->dir.y*dist, ray->origin.z + ray->dir.z*dist };
					}
				}
			}
		}

		return hitInfo->hitSectorId >= 0;
	}
	
	bool pointInsideAABB3d(const Vec3f* aabb, const Vec3f* pt)
	{
		return (pt->x >= aabb[0].x && pt->x <= aabb[1].x &&
			pt->y >= aabb[0].y && pt->y <= aabb[1].y &&
			pt->z >= aabb[0].z && pt->z <= aabb[1].z);
	}

	bool pointInsideAABB2d(const Vec3f* aabb, const Vec3f* pt)
	{
		return (pt->x >= aabb[0].x && pt->x <= aabb[1].x &&
			pt->z >= aabb[0].z && pt->z <= aabb[1].z);
	}

	bool aabbOverlap3d(const Vec3f* aabb0, const Vec3f* aabb1)
	{
		// X
		if (aabb0[0].x > aabb1[1].x || aabb0[1].x < aabb1[0].x ||
			aabb1[0].x > aabb0[1].x || aabb1[1].x < aabb0[0].x)
		{
			return false;
		}

		// Y
		if (aabb0[0].y > aabb1[1].y || aabb0[1].y < aabb1[0].y ||
			aabb1[0].y > aabb0[1].y || aabb1[1].y < aabb0[0].y)
		{
			return false;
		}

		// Z
		if (aabb0[0].z > aabb1[1].z || aabb0[1].z < aabb1[0].z ||
			aabb1[0].z > aabb0[1].z || aabb1[1].z < aabb0[0].z)
		{
			return false;
		}

		return true;
	}

	bool aabbOverlap2d(const Vec3f* aabb0, const Vec3f* aabb1)
	{
		// Ignore the Y axis.
		// X
		if (aabb0[0].x > aabb1[1].x || aabb0[1].x < aabb1[0].x ||
			aabb1[0].x > aabb0[1].x || aabb1[1].x < aabb0[0].x)
		{
			return false;
		}

		// Z
		if (aabb0[0].z > aabb1[1].z || aabb0[1].z < aabb1[0].z ||
			aabb1[0].z > aabb0[1].z || aabb1[1].z < aabb0[0].z)
		{
			return false;
		}

		return true;
	}

	bool isPointInsideSector2d(EditorSector* sector, Vec2f pos)
	{
		const f32 eps = 0.0001f;
		// The layers need to match.
		if (!sector_isInteractable(sector) || !sector_onActiveLayer(sector)) { return false; }
		// The point has to be within the bounding box.
		if (pos.x < sector->bounds[0].x - eps || pos.x > sector->bounds[1].x + eps ||
			pos.z < sector->bounds[0].z - eps || pos.z > sector->bounds[1].z + eps)
		{
			return false;
		}
		// Jitter the z position if needed.
		bool inside = TFE_Polygon::pointInsidePolygon(&sector->poly, pos);
		if (!inside) { inside = TFE_Polygon::pointInsidePolygon(&sector->poly, { pos.x, pos.z + eps }); }
		return inside;
	}

	bool isPointInsideSector3d(EditorSector* sector, Vec3f pos)
	{
		const f32 eps = 0.0001f;
		// The layers need to match.
		if (!sector_isInteractable(sector) || !sector_onActiveLayer(sector)) { return false; }
		// The point has to be within the bounding box.
		if (pos.x < sector->bounds[0].x - eps || pos.x > sector->bounds[1].x + eps ||
			pos.y < sector->bounds[0].y - eps || pos.y > sector->bounds[1].y + eps ||
			pos.z < sector->bounds[0].z - eps || pos.z > sector->bounds[1].z + eps)
		{
			return false;
		}
		// Jitter the z position if needed.
		bool inside = TFE_Polygon::pointInsidePolygon(&sector->poly, { pos.x, pos.z });
		if (!inside) { inside = TFE_Polygon::pointInsidePolygon(&sector->poly, { pos.x, pos.z + eps }); }
		return inside;
	}

	bool sector_inViewRange(const EditorSector* sector)
	{
		if (s_view == EDIT_VIEW_2D && (sector->floorHeight > s_viewDepth[0] || sector->floorHeight < s_viewDepth[1])) { return false; }
		return true;
	}

	bool sector_onActiveLayer(const EditorSector* sector)
	{
		if (!sector_inViewRange(sector)) { return false; }
		if (s_editFlags & LEF_SHOW_ALL_LAYERS) { return true; }
		return sector->layer == s_curLayer;
	}

	EditorSector* findSector3d(Vec3f pos)
	{
		const size_t sectorCount = s_level.sectors.size();
		EditorSector* sector = s_level.sectors.data();
		for (size_t s = 0; s < sectorCount; s++, sector++)
		{
			if (isPointInsideSector3d(sector, pos))
			{
				return sector;
			}
		}
		return nullptr;
	}

	s32 findClosestWallInSector(const EditorSector* sector, const Vec2f* pos, f32 maxDistSq, f32* minDistToWallSq)
	{
		const u32 count = (u32)sector->walls.size();
		f32 minDistSq = FLT_MAX;
		s32 closestId = -1;
		const EditorWall* walls = sector->walls.data();
		const Vec2f* vertices = sector->vtx.data();
		for (u32 w = 0; w < count; w++)
		{
			const Vec2f* v0 = &vertices[walls[w].idx[0]];
			const Vec2f* v1 = &vertices[walls[w].idx[1]];

			Vec2f pointOnSeg;
			TFE_Polygon::closestPointOnLineSegment(*v0, *v1, *pos, &pointOnSeg);
			const Vec2f diff = { pointOnSeg.x - pos->x, pointOnSeg.z - pos->z };
			const f32 distSq = diff.x*diff.x + diff.z*diff.z;

			if (distSq < maxDistSq && distSq < minDistSq && (!minDistToWallSq || distSq < *minDistToWallSq))
			{
				minDistSq = distSq;
				closestId = s32(w);
			}
		}
		if (minDistToWallSq)
		{
			*minDistToWallSq = std::min(*minDistToWallSq, minDistSq);
		}
		return closestId;
	}

	// TODO: Spatial data structure to handle cases where there are 10k, 100k, etc. sectors.
	bool getOverlappingSectorsPt(const Vec3f* pos, SectorList* result, f32 padding)
	{
		if (!pos || !result) { return false; }

		result->clear();
		const s32 count = (s32)s_level.sectors.size();
		EditorSector* sector = s_level.sectors.data();
		for (s32 i = 0; i < count; i++, sector++)
		{
			if (!sector_isInteractable(sector) || !sector_onActiveLayer(sector)) { continue; }
			// The position has to be within the bounds of the sector.
			// TODO: Increase the bounds range?
			if (pos->x < sector->bounds[0].x-padding || pos->x > sector->bounds[1].x+padding ||
				pos->y < sector->bounds[0].y-padding || pos->y > sector->bounds[1].y+padding ||
				pos->z < sector->bounds[0].z-padding || pos->z > sector->bounds[1].z+padding)
			{
				continue;
			}
			// Add the sector as a potential result.
			result->push_back(sector);
		}
		// Are there any potential results?
		return !result->empty();
	}
	   
	bool getOverlappingSectorsBounds(const Vec3f bounds[2], SectorList* result)
	{
		if (!bounds || !result) { return false; }

		result->clear();
		const f32 padding = 0.1f;
		const s32 count = (s32)s_level.sectors.size();
		EditorSector* sector = s_level.sectors.data();
		for (s32 i = 0; i < count; i++, sector++)
		{
			if (boundsOverlap3D(sector->bounds, bounds, padding)) // Add padding for sectors that are just touching.
			{
				result->push_back(sector);
			}
		}

		return !result->empty();
	}

	// Snapshot of a full entity list.
	void level_createEntiyListSnapshot(SnapshotBuffer* buffer, s32 sectorId)
	{
		s_uniqueEntities.clear();
		setSnapshotWriteBuffer(buffer);

		const std::vector<EditorObject>* objList = nullptr;
		if (sectorId >= 0)
		{
			objList = &s_level.sectors[sectorId].obj;
		}
		else
		{
			// TODO: Handle entites outside of a sector.
			assert(0);
		}

		const u32 objCount = (u32)objList->size();
		const EditorObject* srcObj = objList->data();

		std::vector<EditorObject> dstObjList = *objList;
		EditorObject* dstObject = dstObjList.data();
		for (u32 e = 0; e < objCount; e++, srcObj++, dstObject++)
		{
			dstObject->entityId = addUniqueEntity(srcObj->entityId, s_uniqueEntities);
		}

		u32 uniqueEntityCount = (u32)s_uniqueEntities.size();
		writeS32(sectorId);
		writeU32(uniqueEntityCount);
		writeU32(objCount);

		// Write unique entities.
		UniqueEntity* uentity = s_uniqueEntities.data();
		for (u32 e = 0; e < uniqueEntityCount; e++, uentity++)
		{
			uentity->entity.id = uentity->newId;
			writeEntityToSnapshot(&uentity->entity);
		}

		// Write objects.
		writeData(dstObjList.data(), u32(sizeof(EditorObject) * objCount));
	}

	void level_unpackEntiyListSnapshot(u32 size, void* data)
	{
		setSnapshotReadBuffer((u8*)data, size);

		const s32 sectorId = readS32();
		const u32 entityCount = readU32();      // Number of unique entities from sectors in snapshot.
		const u32 objCount = readU32();

		std::vector<EditorObject>* objList = nullptr;
		if (sectorId >= 0)
		{
			assert(sectorId < (s32)s_level.sectors.size());
			objList = &s_level.sectors[sectorId].obj;
		}
		else
		{
			// TODO: Handle entites outside of a sector.
			assert(0);
		}

		std::vector<s32> remapTableEntity(entityCount);
		for (u32 e = 0; e < entityCount; e++)
		{
			Entity entity;
			readEntityFromSnapshot(&entity);

			// Search the entity list for a match.
			// If not, add a new entity.
			const s32 id = addEntityToLevel(&entity);
			assert(id >= 0);

			loadSingleEntityData(&s_level.entities[id]);
			remapTableEntity[e] = id;
		}

		objList->resize(objCount);
		readData(objList->data(), u32(sizeof(EditorObject) * objCount));

		EditorObject* obj = objList->data();
		for (u32 i = 0; i < objCount; i++, obj++)
		{
			if (obj->entityId < 0) { continue; }
			assert(obj->entityId < (s32)entityCount);
			obj->entityId = remapTableEntity[obj->entityId];
		}
	}

	bool levelTextureEq(const LevelTexture& a, const LevelTexture& b)
	{
		return a.texIndex == b.texIndex && a.offset.x == b.offset.x && a.offset.z == b.offset.z;
	}

	bool vec3Eq(const Vec3f& a, const Vec3f& b)
	{
		return a.x == b.x && a.y == b.y && a.z == b.z;
	}
		
	// Take a sector snapshot assuming the assets are going to stay the same.
	void level_createLevelSectorSnapshotSameAssets(std::vector<EditorSector>& sectors)
	{
		sectors = s_level.sectors;
	}
		
	void level_getLevelSnapshotDelta(std::vector<s32>& modifiedSectors, const std::vector<EditorSector>& sectorSnapshot)
	{
		modifiedSectors.clear();
		s32 count = (s32)std::min(s_level.sectors.size(), sectorSnapshot.size());
		const EditorSector* curSector = s_level.sectors.data();
		const EditorSector* prevSector = sectorSnapshot.data();
		for (s32 i = 0; i < count; i++, curSector++, prevSector++)
		{
			// Check the sizes and values.
			// Assume that if the bounds match and all of the sizes/counts match - then the sectors match.
			if (curSector->id != prevSector->id || curSector->groupId != prevSector->groupId || curSector->name != prevSector->name ||
			   !levelTextureEq(curSector->floorTex, prevSector->floorTex) || !levelTextureEq(curSector->ceilTex, prevSector->ceilTex) ||
			    curSector->floorHeight != prevSector->floorHeight || curSector->ceilHeight != prevSector->ceilHeight || curSector->secHeight != prevSector->secHeight ||
			    curSector->ambient != prevSector->ambient || curSector->flags[0] != prevSector->flags[0] || curSector->flags[1] != prevSector->flags[1] ||
				curSector->flags[2] != prevSector->flags[2] || !vec3Eq(curSector->bounds[0], prevSector->bounds[0]) ||
				!vec3Eq(curSector->bounds[1], prevSector->bounds[1]) || curSector->layer != prevSector->layer ||
				curSector->vtx.size() != prevSector->vtx.size() || curSector->walls.size() != prevSector->walls.size() ||
				curSector->obj.size() != prevSector->obj.size())
			{
				modifiedSectors.push_back(i);
				// Continue the loop and don't check the walls.
				continue;
			}

			// However we still need to check the walls because the mirrors and adjoin values may have changed.
			const s32 wallCount = (s32)curSector->walls.size();
			const EditorWall* curWall = curSector->walls.data();
			const EditorWall* prevWall = prevSector->walls.data();
			for (s32 w = 0; w < wallCount; w++, curWall++, prevWall++)
			{
				if (curWall->idx[0] != prevWall->idx[0] || curWall->idx[1] != prevWall->idx[1] || curWall->adjoinId != prevWall->adjoinId ||
					curWall->mirrorId != prevWall->mirrorId || curWall->flags[0] != prevWall->flags[0] || curWall->flags[1] != prevWall->flags[1] ||
					curWall->flags[2] != prevWall->flags[2] || curWall->wallLight != prevWall->wallLight)
				{
					modifiedSectors.push_back(i);
					// Break out of the wall checking loop.
					break;
				}
			}
		}
	}

	void level_appendTextureList()
	{
		// Texture List.
		const AppendTexList& appendTex = edit_getTextureAppendList();
		writeS32(appendTex.startIndex);
		if (appendTex.startIndex >= 0)
		{
			s32 texCount = (s32)appendTex.list.size();
			assert(texCount > 0);
			writeS32(texCount);

			const std::string* texAsset = appendTex.list.data();
			for (s32 i = 0; i < texCount; i++, texAsset++)
			{
				s32 len = (s32)texAsset->length();
				writeS32(len);
				if (len > 0)
				{
					writeData(texAsset->c_str(), len);
				}
			}
			edit_clearTextureAppendList();
		}
	}

	void level_createFeatureTextureSnapshot(SnapshotBuffer* buffer, s32 count, const FeatureId* feature)
	{
		if (count < 1) { return; }
		setSnapshotWriteBuffer(buffer);
		// Texture List.
		level_appendTextureList();
		// Feature texture info.
		s32 featureIndex = -1;
		HitPart part = HP_FLOOR;
		writeS32(count);
		writeData(feature, sizeof(FeatureId) * count);
		for (s32 f = 0; f < count; f++)
		{
			EditorSector* sector = unpackFeatureId(feature[f], &featureIndex, (s32*)&part);

			switch (part)
			{
				case HP_FLOOR:
				{
					writeData(&sector->floorTex, sizeof(LevelTexture));
				} break;
				case HP_CEIL:
				{
					writeData(&sector->ceilTex, sizeof(LevelTexture));
				} break;
				case HP_MID:
				case HP_TOP:
				case HP_BOT:
				case HP_SIGN:
				{
					writeData(&sector->walls[featureIndex].tex[part], sizeof(LevelTexture));
				} break;
				default:
				{
					// Invalid part!
					assert(0);
				}
			};
		}
	}

	void level_createSectorWallSnapshot(SnapshotBuffer* buffer, std::vector<IndexPair>& sectorWallIds)
	{
		const u32 count = (u32)sectorWallIds.size();
		assert(count > 0);
		setSnapshotWriteBuffer(buffer);
		// Texture List.
		level_appendTextureList();

		// Wall attributes.
		writeU32(count);

		// Write sectors walls only.
		const IndexPair* pair = sectorWallIds.data();
		writeData(pair, sizeof(IndexPair) * count);

		for (u32 s = 0; s < count; s++, pair++)
		{
			const s32 i0 = pair->i0, i1 = pair->i1;
			assert(i0 >= 0 && i0 < (s32)s_level.sectors.size());
			assert(i1 >= 0 && i1 < (s32)s_level.sectors[i0].walls.size());
			writeData(&s_level.sectors[i0].walls[i1], (u32)sizeof(EditorWall));
		}
	}
		
	void level_createSectorAttribSnapshot(SnapshotBuffer* buffer, std::vector<IndexPair>& sectorIds)
	{
		const u32 count = (u32)sectorIds.size();
		if (count < 1) { return; }

		assert(count > 0);
		setSnapshotWriteBuffer(buffer);
		// Texture List.
		level_appendTextureList();

		writeU32(count);

		// Write sectors only.
		const IndexPair* pair = sectorIds.data();
		s_sectorIds.resize(count);
		for (u32 i = 0; i < count; i++)
		{
			s_sectorIds[i] = pair[i].i0;
		}
		writeData(s_sectorIds.data(), sizeof(s32) * count);

		for (u32 s = 0; s < count; s++, pair++)
		{
			const s32 i0 = pair->i0;
			assert(i0 >= 0 && i0 < (s32)s_level.sectors.size());
			EditorSector* sector = &s_level.sectors[i0];
			writeU32((u32)sector->name.length());
			if (sector->name.length())
			{
				writeData(sector->name.data(), (u32)sector->name.length());
			}
			const SectorAttrib attrib = 
			{
				sector->groupId,
				sector->floorHeight,
				sector->ceilHeight,
				sector->secHeight,
				sector->ambient,
				sector->layer,
				{ sector->flags[0], sector->flags[1], sector->flags[2] },
				sector->floorTex,
				sector->ceilTex
			};
			writeData(&attrib, (u32)sizeof(SectorAttrib));
		}
	}
							
	void level_createSectorSnapshot(SnapshotBuffer* buffer, std::vector<s32>& sectorIds)
	{
		s_uniqueEntities.clear();
		s_uniqueTextures.clear();

		setSnapshotWriteBuffer(buffer);
		// Sort by id from smallest to largest.
		std::sort(sectorIds.begin(), sectorIds.end());
		// Get the new sector count, which must be large enough to hold the largest sector ID.
		u32 newSectorCount = sectorIds.empty() ? (s32)s_level.sectors.size() : std::max((s32)s_level.sectors.size(), sectorIds.back() + 1);
		// Extract unique textures and entities.
		// Create a set of sectors to serialize.
		const s32 idCount = (s32)sectorIds.size();
		const s32* idList = sectorIds.data();
		s_sectorSnapshot.resize(idCount);
		EditorSector* dstSector = s_sectorSnapshot.data();
		for (s32 i = 0; i < idCount; i++, dstSector++)
		{
			const EditorSector* srcSector = &s_level.sectors[idList[i]];
			*dstSector = *srcSector;
			dstSector->floorTex.texIndex = addUniqueTexture(srcSector->floorTex.texIndex, s_uniqueTextures);
			dstSector->ceilTex.texIndex = addUniqueTexture(srcSector->ceilTex.texIndex, s_uniqueTextures);

			// Wall textures.
			const s32 wallCount = (s32)srcSector->walls.size();
			const EditorWall* srcWall = srcSector->walls.data();
			EditorWall* dstWall = dstSector->walls.data();
			for (s32 w = 0; w < wallCount; w++, srcWall++, dstWall++)
			{
				for (s32 t = 0; t < 4; t++)
				{
					if (dstWall->tex[t].texIndex < 0) { continue; }
					dstWall->tex[t].texIndex = addUniqueTexture(srcWall->tex[t].texIndex, s_uniqueTextures);
				}
			}

			// Entities.
			const s32 entityCount = (s32)srcSector->obj.size();
			const EditorObject* srcEntity = srcSector->obj.data();
			EditorObject* dstEntity = dstSector->obj.data();
			for (s32 e = 0; e < entityCount; e++, srcEntity++, dstEntity++)
			{
				dstEntity->entityId = addUniqueEntity(srcEntity->entityId, s_uniqueEntities);
			}
		}

		const u32 texCount = (u32)s_uniqueTextures.size();
		const u32 sectorCount = (u32)s_sectorSnapshot.size();
		const u32 entityCount = (u32)s_uniqueEntities.size();
		writeU32(newSectorCount);
		writeU32(texCount);
		writeU32(sectorCount);
		writeU32(entityCount);

		// Textures.
		const UniqueTexture* texture = s_uniqueTextures.data();
		for (u32 i = 0; i < texCount; i++, texture++)
		{
			writeString(texture->name);
		}

		// Entities.
		UniqueEntity* uentity = s_uniqueEntities.data();
		for (u32 e = 0; e < entityCount; e++, uentity++)
		{
			uentity->entity.id = uentity->newId;
			writeEntityToSnapshot(&uentity->entity);
		}

		// Write sectors.
		const EditorSector* sector = s_sectorSnapshot.data();
		for (u32 s = 0; s < sectorCount; s++, sector++)
		{
			writeSectorToSnapshot(sector);
		}
	}

	void level_unpackSectorSnapshot(u32 size, void* data)
	{
		setSnapshotReadBuffer((u8*)data, size);
		
		const u32 newSectorCount = readU32();   // Total sectors in level after snapshot.
		const u32 texCount = readU32();         // Number of unique textures from sectors in snapshot.
		const u32 sectorCount = readU32();      // Number of sectors *in* snapshot.
		const u32 entityCount = readU32();      // Number of unique entities from sectors in snapshot.

		// Resize to post-snapshot sector total.
		s_level.sectors.resize(newSectorCount);

		std::string texName;
		std::vector<s32> remapTableTex(texCount);
		for (u32 i = 0; i < texCount; i++)
		{
			readString(texName);
			// Find the name in the texture list.
			// If it doesn't exist, then add a new texture.
			remapTableTex[i] = getTextureIndex(texName.c_str());
		}

		std::vector<s32> remapTableEntity(entityCount);
		for (u32 e = 0; e < entityCount; e++)
		{
			Entity entity;
			readEntityFromSnapshot(&entity);

			// Search the entity list for a match.
			// If not, add a new entity.
			remapTableEntity[e] = addEntityToLevel(&entity);
			loadSingleEntityData(&entity);
		}

		// Sectors.
		for (u32 s = 0; s < sectorCount; s++)
		{
			EditorSector tmp;
			readSectorFromSnapshot(&tmp);
			assert(tmp.id < s_level.sectors.size());

			EditorSector* sector = &s_level.sectors[tmp.id];
			*sector = tmp;

			// Fix-up sector textures.
			if (sector->floorTex.texIndex >= 0)
			{
				sector->floorTex.texIndex = remapTableTex[sector->floorTex.texIndex];
			}
			if (sector->ceilTex.texIndex >= 0)
			{
				sector->ceilTex.texIndex = remapTableTex[sector->ceilTex.texIndex];
			}

			// Fix up wall textures.
			const u32 wallCount = (u32)sector->walls.size();
			EditorWall* wall = sector->walls.data();
			for (u32 i = 0; i < wallCount; i++, wall++)
			{
				for (s32 t = 0; t < WP_COUNT; t++)
				{
					if (wall->tex[t].texIndex < 0) { continue; }
					wall->tex[t].texIndex = remapTableTex[wall->tex[t].texIndex];
				}
			}

			// Fix up objects.
			const u32 objCount = (u32)sector->obj.size();
			EditorObject* obj = sector->obj.data();
			for (u32 i = 0; i < objCount; i++, obj++)
			{
				if (obj->entityId < 0) { continue; }
				obj->entityId = remapTableEntity[obj->entityId];
			}

			// Build the sector polygon for the editor.
			sectorToPolygon(sector);
			sector->searchKey = 0;
		}
	}

	void level_readTextureList()
	{
		const s32 startIndex = readS32();
		if (startIndex >= 0)
		{
			assert(edit_getTextureAppendList().list.empty());
			const s32 texCount = readS32();
			assert(texCount > 0);
			s_level.textures.resize(startIndex + texCount);

			char name[256];
			LevelTextureAsset* texAsset = s_level.textures.data() + startIndex;
			for (s32 i = 0; i < texCount; i++, texAsset++)
			{
				const s32 len = readS32();
				if (len > 0)
				{
					readData(name, len);
					name[len] = 0;
					texAsset->name = name;
				}
				else
				{
					texAsset->name.clear();
				}
				texAsset->handle = loadTexture(texAsset->name.c_str());
			}
			edit_clearTextureAppendList();
		}
	}

	void level_unpackFeatureTextureSnapshot(u32 size, void* data)
	{
		setSnapshotReadBuffer((u8*)data, size);
		level_readTextureList();

		s32 count = readS32();
		if (count < 1) { return; }
		// Feature texture info.
		s32 featureIndex = -1;
		HitPart part = HP_FLOOR;
		s_featureList.resize(count);
		readData(s_featureList.data(), sizeof(FeatureId) * count);
		const FeatureId* feature = s_featureList.data();
		for (s32 f = 0; f < count; f++)
		{
			EditorSector* sector = unpackFeatureId(feature[f], &featureIndex, (s32*)&part);
			switch (part)
			{
				case HP_FLOOR:
				{
					readData(&sector->floorTex, sizeof(LevelTexture));
				} break;
				case HP_CEIL:
				{
					readData(&sector->ceilTex, sizeof(LevelTexture));
				} break;
				case HP_MID:
				case HP_TOP:
				case HP_BOT:
				case HP_SIGN:
				{
					assert(featureIndex >= 0 && featureIndex < (s32)sector->walls.size());
					readData(&sector->walls[featureIndex].tex[part], sizeof(LevelTexture));
				} break;
				default:
				{
					// Invalid part!
					assert(0);
				}
			};
		}
	}

	void level_unpackSectorWallSnapshot(u32 size, void* data)
	{
		setSnapshotReadBuffer((u8*)data, size);
		level_readTextureList();

		const u32 count = readU32();
		assert(count > 0);
		s_pairs.resize(count);
		readData(s_pairs.data(), sizeof(IndexPair) * count);

		// Read sectors walls only.
		IndexPair* pair = s_pairs.data();
		for (u32 s = 0; s < count; s++, pair++)
		{
			s32 sectorId = pair->i0;
			s32 wallId = pair->i1;
			assert(sectorId >= 0 && sectorId < (s32)s_level.sectors.size());
			assert(wallId >= 0 && wallId < (s32)s_level.sectors[sectorId].walls.size());
			readData(&s_level.sectors[sectorId].walls[wallId], (u32)sizeof(EditorWall));
		}
	}

	void level_unpackSectorAttribSnapshot(u32 size, void* data)
	{
		setSnapshotReadBuffer((u8*)data, size);
		level_readTextureList();

		const u32 count = readU32();
		assert(count > 0);

		s_sectorIds.resize(count);
		readData(s_sectorIds.data(), sizeof(s32) * count);

		// Read sectors walls only.
		s32* id = s_sectorIds.data();
		char name[256];
		for (u32 s = 0; s < count; s++)
		{
			const s32 sectorId = id[s];
			assert(sectorId >= 0 && sectorId < (s32)s_level.sectors.size());
			EditorSector* sector = &s_level.sectors[sectorId];
			SectorAttrib attrib;
			s32 nameLen = readS32();
			if (nameLen > 0)
			{
				readData(name, nameLen);
				name[nameLen] = 0;
				sector->name = name;
			}
			else
			{
				sector->name.clear();
			}
			readData(&attrib, (u32)sizeof(SectorAttrib));

			sector->groupId = attrib.groupId;
			if (groups_isIdValid(attrib.groupId))
			{
				Group* group = groups_getById(sector->groupId);
				assert(group);
				sector->groupIndex = group->index;
			}
			else
			{
				LE_ERROR("level_unpackSectorAttribSnapshot - Invalid Group ID %d", attrib.groupId);

				sector->groupId = groups_getMainId();
				Group* group = groups_getById(sector->groupId);
				assert(group);

				sector->groupIndex = group->index;
			}
			sector->floorHeight = attrib.floorHeight;
			sector->ceilHeight = attrib.ceilHeight;
			sector->secHeight = attrib.secHeight;
			sector->ambient = attrib.ambient;
			sector->layer = attrib.layer;
			for (s32 i = 0; i < 3; i++) { sector->flags[i] = attrib.flags[i]; }
			sector->bounds[0].y = std::min(sector->floorHeight, sector->ceilHeight);
			sector->bounds[1].y = std::max(sector->floorHeight, sector->ceilHeight);
			sector->floorTex = attrib.floorTex;
			sector->ceilTex = attrib.ceilTex;
		}
	}
		
	void level_createSnapshot(SnapshotBuffer* buffer)
	{
		assert(buffer);
		setSnapshotWriteBuffer(buffer);

		// Serialize the group data.
		groups_saveToSnapshot();

		// Serialize the level into a buffer.
		writeString(s_level.name);
		writeString(s_level.slot);
		writeString(s_level.palette);
		writeS32((s32)s_level.featureSet);
		writeData(&s_level.parallax, sizeof(Vec2f));
		writeData(s_level.bounds, sizeof(Vec3f) * 2);
		writeData(s_level.layerRange, sizeof(s32) * 2);

		const u32 texCount = (u32)s_level.textures.size();
		const u32 sectorCount = (u32)s_level.sectors.size();
		const u32 entityCount = (u32)s_level.entities.size();
		const u32 levelNoteCount = (s32)s_level.notes.size();
		const u32 guidelineCount = (s32)s_level.guidelines.size();
		writeU32(texCount);
		writeU32(sectorCount);
		writeU32(entityCount);
		writeU32(levelNoteCount);
		writeU32(guidelineCount);
		
		// Textures.
		const LevelTextureAsset* texture = s_level.textures.data();
		for (u32 i = 0; i < texCount; i++, texture++)
		{
			writeString(texture->name);
		}

		// Sectors.
		const EditorSector* sector = s_level.sectors.data();
		for (u32 s = 0; s < sectorCount; s++, sector++)
		{
			writeSectorToSnapshot(sector);
		}

		// Entities.
		const Entity* entity = s_level.entities.data();
		for (u32 e = 0; e < entityCount; e++, entity++)
		{
			writeEntityToSnapshot(entity);
		}

		// Level Notes.
		const LevelNote* note = s_level.notes.data();
		for (u32 n = 0; n < levelNoteCount; n++, note++)
		{
			writeLevelNoteToSnapshot(note);
		}

		// Guidelines.
		const Guideline* guideline = s_level.guidelines.data();
		for (u32 g = 0; g < guidelineCount; g++, guideline++)
		{
			writeGuidelineToSnapshot(guideline);
		}
	}

	void level_unpackSnapshot(s32 id, u32 size, void* data)
	{
		// Clear the current snapshot ID.
		if (id < 0)
		{
			s_curSnapshotId = -1;
			return;
		}

		// Only unpack the snapshot if its not already cached.
		if (s_curSnapshotId != id)
		{
			s_curSnapshotId = id;
			setSnapshotReadBuffer((u8*)data, size);

			// Load the group data.
			groups_loadFromSnapshot();

			// Load the level data.
			readString(s_curSnapshot.name);
			readString(s_curSnapshot.slot);
			readString(s_curSnapshot.palette);
			s_curSnapshot.featureSet = (FeatureSet)readS32();
			readData(&s_curSnapshot.parallax, sizeof(Vec2f));
			readData(s_curSnapshot.bounds, sizeof(Vec3f) * 2);
			readData(s_curSnapshot.layerRange, sizeof(s32) * 2);

			const u32 texCount = readU32();
			const u32 sectorCount = readU32();
			const u32 entityCount = readU32();
			const u32 levelNoteCount = readU32();
			const u32 guidelineCount = readU32();

			s_curSnapshot.textures.resize(texCount);
			for (u32 i = 0; i < texCount; i++)
			{
				readString(s_curSnapshot.textures[i].name);
				s_curSnapshot.textures[i].handle = loadTexture(s_curSnapshot.textures[i].name.c_str());
			}

			s_curSnapshot.sectors.resize(sectorCount);
			EditorSector* sector = s_curSnapshot.sectors.data();
			for (u32 s = 0; s < sectorCount; s++, sector++)
			{
				readSectorFromSnapshot(sector);
				// Compute derived data.
				sectorToPolygon(sector);
				sector->searchKey = 0;
			}

			s_curSnapshot.entities.resize(entityCount);
			Entity* entity = s_curSnapshot.entities.data();
			for (u32 e = 0; e < entityCount; e++, entity++)
			{
				readEntityFromSnapshot(entity);
				// Sprite and obj data derived from type + assetName
				loadSingleEntityData(entity);
			}

			// Level Notes.
			s_curSnapshot.notes.resize(levelNoteCount);
			LevelNote* note = s_curSnapshot.notes.data();
			for (u32 n = 0; n < levelNoteCount; n++, note++)
			{
				readLevelNoteFromSnapshot(note);
			}

			// Guidelines.
			s_curSnapshot.guidelines.resize(guidelineCount);
			Guideline* guideline = s_curSnapshot.guidelines.data();
			for (u32 g = 0; g < guidelineCount; g++, guideline++)
			{
				readGuidelineFromSnapshot(guideline);
			}
		}
		// Then copy the snapshot to the level data itself. Its the new state.
		s_level = s_curSnapshot;

		// For now until the way snapshot memory is handled is refactored, to avoid duplicate code that will be removed later.
		// TODO: Handle edit state properly here too.
		edit_clearSelections();
	}
		
	void level_createGuidelineSnapshot(SnapshotBuffer* buffer)
	{
		setSnapshotWriteBuffer(buffer);
		const u32 guidelineCount = (u32)s_level.guidelines.size();
		const Guideline* guideline = s_level.guidelines.data();

		writeU32(guidelineCount);
		for (u32 g = 0; g < guidelineCount; g++, guideline++)
		{
			writeGuidelineToSnapshot(guideline);
		}
	}

	void level_unpackGuidelineSnapshot(u32 size, void* data)
	{
		setSnapshotReadBuffer((u8*)data, size);

		const u32 guidelineCount = readU32();
		s_level.guidelines.resize(guidelineCount);

		Guideline* guideline = s_level.guidelines.data();
		for (u32 g = 0; g < guidelineCount; g++, guideline++)
		{
			readGuidelineFromSnapshot(guideline);
		}
	}
		
	void level_createSingleGuidelineSnapshot(SnapshotBuffer* buffer, s32 index)
	{
		assert(index >= 0 && index < (s32)s_level.guidelines.size());
		setSnapshotWriteBuffer(buffer);

		Guideline* guideline = &s_level.guidelines[index];
		writeS32(index);
		writeGuidelineToSnapshot(guideline);
	}

	void level_unpackSingleGuidelineSnapshot(u32 size, void* data)
	{
		setSnapshotReadBuffer((u8*)data, size);

		const s32 index = readS32();
		if (index < 0 || index >= (s32)s_level.guidelines.size())
		{
			return;
		}

		Guideline* guideline = &s_level.guidelines[index];
		readGuidelineFromSnapshot(guideline);
	}

	// Find a sector based on DF rules.
	EditorSector* findSectorDf(const Vec3f pos)
	{
		const s32 count = (s32)s_level.sectors.size();
		EditorSector* sector = s_level.sectors.data();
		EditorSector* foundSector = nullptr;
		s32 sectorUnitArea = 0;
		s32 prevSectorUnitArea = INT_MAX;

		for (s32 i = 0; i < count; i++, sector++)
		{
			if (pos.y <= sector->ceilHeight && pos.y >= sector->floorHeight)
			{
				const f32 sectorMaxX = sector->bounds[1].x;
				const f32 sectorMinX = sector->bounds[0].x;
				const f32 sectorMaxZ = sector->bounds[1].z;
				const f32 sectorMinZ = sector->bounds[0].z;

				const s32 dxInt = (s32)floorf(sectorMaxX - sectorMinX) + 1;
				const s32 dzInt = (s32)floorf(sectorMaxZ - sectorMinZ) + 1;
				sectorUnitArea = dzInt * dxInt;

				s32 insideBounds = 0;
				if (pos.x >= sectorMinX && pos.x <= sectorMaxX && pos.z >= sectorMinZ && pos.z <= sectorMaxZ)
				{
					// pick the containing sector with the smallest area.
					if (sectorUnitArea < prevSectorUnitArea && TFE_Polygon::pointInsidePolygon(&sector->poly, { pos.x, pos.z }))
					{
						prevSectorUnitArea = sectorUnitArea;
						foundSector = sector;
					}
				}
			}
		}

		return foundSector;
	}

	// Find a sector based on DF rules.
	EditorSector* findSectorDf(const Vec2f pos)
	{
		const s32 count = (s32)s_level.sectors.size();
		EditorSector* sector = s_level.sectors.data();
		EditorSector* foundSector = nullptr;
		s32 sectorUnitArea = 0;
		s32 prevSectorUnitArea = INT_MAX;

		for (s32 i = 0; i < count; i++, sector++)
		{
			const f32 sectorMaxX = sector->bounds[1].x;
			const f32 sectorMinX = sector->bounds[0].x;
			const f32 sectorMaxZ = sector->bounds[1].z;
			const f32 sectorMinZ = sector->bounds[0].z;

			const s32 dxInt = (s32)floorf(sectorMaxX - sectorMinX) + 1;
			const s32 dzInt = (s32)floorf(sectorMaxZ - sectorMinZ) + 1;
			sectorUnitArea = dzInt * dxInt;

			s32 insideBounds = 0;
			if (pos.x >= sectorMinX && pos.x <= sectorMaxX && pos.z >= sectorMinZ && pos.z <= sectorMaxZ)
			{
				// pick the containing sector with the smallest area.
				if (sectorUnitArea < prevSectorUnitArea && TFE_Polygon::pointInsidePolygon(&sector->poly, { pos.x, pos.z }))
				{
					prevSectorUnitArea = sectorUnitArea;
					foundSector = sector;
				}
			}
		}

		return foundSector;
	}

	bool rayPlaneIntersection(const Vec3f& origin, const Vec3f& dir, const Vec4f& plane, f32& dist)
	{
		const f32 d = plane.x*dir.x + plane.y*dir.y + plane.z*dir.z;
		if (fabsf(d) < FLT_MIN) { return false; }

		dist = -(plane.x*origin.x + plane.y*origin.y + plane.z*origin.z + plane.w) / d;
		return dist >= FLT_MIN;
	}
		
	void updateSlopeData(EditorSector* sector)
	{
		if (s_viewFrame == sector->slopeFrame) { return; }
		sector->slopeFrame = s_viewFrame;

		if (sector->flags[0] & SEC_FLAGS1_SLOPEDFLOOR)
		{
			sector->floorPlane = slope_calculatePlane(sector->slope.floorAnchorSectorId, sector->slope.floorAnchorWallId, sector->id, sector->floorHeight,
				TFE_Math::degToRad(sector->slope.floorSlopeAngle));
		}
		if (sector->flags[0] & SEC_FLAGS1_SLOPEDCEILING)
		{
			sector->ceilPlane = slope_calculatePlane(sector->slope.ceilAnchorSectorId, sector->slope.ceilAnchorWallId, sector->id, sector->ceilHeight,
				TFE_Math::degToRad(sector->slope.ceilSlopeAngle));
		}
	}

	f32 getFloorAtXZ(const EditorSector* sector, Vec2f pos)
	{
		updateSlopeData((EditorSector*)sector);
		if (sector->flags[0] & SEC_FLAGS1_SLOPEDFLOOR)
		{
			return slope_getHeightAtXZ(&sector->floorPlane, pos);
		}
		return sector->floorHeight;
	}

	f32 getCeilAtXZ(const EditorSector* sector, Vec2f pos)
	{
		updateSlopeData((EditorSector*)sector);
		if (sector->flags[0] & SEC_FLAGS1_SLOPEDCEILING)
		{
			return slope_getHeightAtXZ(&sector->ceilPlane, pos);
		}
		return sector->ceilHeight;
	}

	u32 getAdjoinType(const EditorSector* sector, const EditorWall* wall)
	{
		const EditorSector* next = (wall->adjoinId >= 0 && wall->adjoinId < (s32)s_level.sectors.size()) ? &s_level.sectors[wall->adjoinId] : nullptr;
		u32 adjType = ADJ_TYPE_NONE;
		if (!next) { return adjType; }

		const Vec2f v0 = sector->vtx[wall->idx[0]];
		const Vec2f v1 = sector->vtx[wall->idx[1]];

		// Calculate the heights at the adjoin vertices.
		f32 f00 = getFloorAtXZ(sector, v0);
		f32 f01 = getFloorAtXZ(sector, v1);
		f32 f10 = getFloorAtXZ(next, v0);
		f32 f11 = getFloorAtXZ(next, v1);

		f32 c00 = getCeilAtXZ(sector, v0);
		f32 c01 = getCeilAtXZ(sector, v1);
		f32 c10 = getCeilAtXZ(next, v0);
		f32 c11 = getCeilAtXZ(next, v1);

		// Handle numerical error.
		if (fabsf(f00 - f10) < adjoinEps) { f10 = f00; }
		if (fabsf(f01 - f11) < adjoinEps) { f11 = f01; }
		if (fabsf(c00 - c10) < adjoinEps) { c10 = c00; }
		if (fabsf(c01 - c11) < adjoinEps) { c11 = c01; }

		// Outlaws generates an error if slopes cross on adjoins.
		if ((c00 >= c10 && c01 > c11) || (c00 > c10 && c01 >= c11))
		{
			adjType |= ADJ_TYPE_TOP;
		}
		if ((f00 <= f10 && f01 < f11) || (f00 < f10 && f01 <= f11))
		{
			adjType |= ADJ_TYPE_BOT;
		}

		return adjType;
	}

	// General level fix-up:
	//   * Remove Zero-Wall and Zero-Vertex sectors.
	//   * TODO: Other issues that may cause crashes, etc.
	// This should be done when:
	//   * Saving.
	//   * Loading.
	//   * When testing (on Play).
	// If any changes are required:
	//   * Post a warning.
	//   * Create a snapshot in the history.
	void fixupLevel(bool createSnapshot)
	{
		const s32 sectorCount = (s32)s_level.sectors.size();
		const EditorSector* sector = s_level.sectors.data();
		std::vector<s32> deleteList;
		for (s32 s = 0; s < sectorCount; s++, sector++)
		{
			if (sector->walls.empty() || sector->vtx.empty())
			{
				deleteList.push_back(s);
			}
		}

		// Sectors to delete.
		if (!deleteList.empty())
		{
			const s32 delCount = (s32)deleteList.size();
			const s32* delIndices = deleteList.data();
			
			LE_WARNING("%d Sectors found with zero(0) walls and/or vertices. Removing erroneous sectors.", delCount);
			for (s32 i = delCount - 1; i >= 0; i--)
			{
				edit_deleteSector(delIndices[i], false);
			}
			if (createSnapshot)
			{
				levHistory_createSnapshot("Clean Zero-Sectors");
			}
		}
	}
}
