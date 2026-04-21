#include "entity.h"
#include "error.h"
#include "levelEditor.h"
#include "infoPanel.h"
#include "sharedState.h"
#include <TFE_Editor/editor.h>
#include <TFE_Editor/errorMessages.h>
#include <TFE_Editor/editorConfig.h>
#include <TFE_Editor/AssetBrowser/assetBrowser.h>
#include <TFE_Editor/EditorAsset/editorTexture.h>
#include <TFE_Editor/EditorAsset/editorFrame.h>
#include <TFE_Editor/EditorAsset/editorSprite.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_System/system.h>
#include <TFE_System/parser.h>
#include <TFE_System/math.h>
#include <TFE_Input/input.h>

#include <algorithm>
#include <vector>
#include <string>
#include <map>

using namespace TFE_Editor;

namespace LevelEditor
{
	enum EntityKey
	{
		EKEY_ENTITY = 0,
		EKEY_CLASS,
		EKEY_LOGIC,
		EKEY_ICON,
		EKEY_ASSET,
		EKEY_ASSET_OFFSET_Y,
		EKEY_CATEGORY,
		EKEY_TOOLTIP,
		EKEY_COUNT,
		EKEY_UNKNOWN = EKEY_COUNT
	};

	enum LogicKey
	{
		LKEY_LOGIC = 0,
		LKEY_TOOLTIP,
		LKEY_VAR,
		LKEY_DEFVAR,
		LKEY_REQVAR,
		LKEY_COUNT,
		LKEY_UNKNOWN = LKEY_COUNT
	};

	enum VarKey
	{
		VKEY_VAR = 0,
		VKEY_TYPE,
		VKEY_DEFAULT,
		VKEY_FLAG,
		VKEY_VALUE,
		VKEY_NAME0,
		VKEY_NAME1,
		VKEY_COUNT,
		VKEY_UNKNOWN = VKEY_COUNT
	};

	const char* c_entityKeyStr[EKEY_COUNT] =
	{
		"Entity",          // EKEY_ENTITY
		"Class",           // EKEY_CLASS
		"Logic",           // EKEY_LOGIC
		"Icon",            // EKEY_ICON
		"Asset",           // EKEY_ASSET
		"Asset_Offset_Y",  // EKEY_ASSET_OFFSET_Y
		"Category",        // EKEY_CATEGORY
		"Tooltip",         // EKEY_TOOLTIP
	};

	const char* c_logicKeyStr[LKEY_COUNT] =
	{
		"Logic",   // LKEY_LOGIC
		"Tooltip", // LKEY_TOOLTIP
		"Var",     // LKEY_VAR
		"DefVar",  // LKEY_DEFVAR
		"ReqVar",  // LKEY_REQVAR
	};

	const char* c_varKeyStr[VKEY_COUNT] =
	{
		"Var",      // VKEY_VAR
		"Type",     // VKEY_TYPE
		"Default",  // VKEY_DEFAULT
		"Flag",     // VKEY_FLAG
		"Value",    // VKEY_VALUE
		"Name0",    // VKEY_NAME0
		"Name1"     // VKEY_NAME1
	};

	const char* c_entityTypeStr[ETYPE_COUNT] =
	{
		"Spirit", // ETYPE_SPIRIT
		"Safe",   // ETYPE_SAFE
		"Sprite", // ETYPE_SPRITE
		"Frame",  // ETYPE_FRAME
		"3D",     // ETYPE_3D
	};

	const char* c_entityVarStr[EVARID_COUNT] =
	{
		"Eye",    // EVARID_EYE
		"Radius", // EVARID_RADIUS
		"Height", // EVARID_HEIGHT
	};

	std::vector<Entity> s_entityDefList;		// Editor provided entity list.
	std::vector<Entity> s_projEntityDefList;	// Project custom entity list.

	std::vector<LogicDef> s_logicDefList;
	std::vector<EntityVarDef> s_varDefList;
	std::vector<Category> s_categoryList;
	u32 s_enemyCategoryFlag = 0;

	void parseValue(const TokenList& tokens, EntityVarType type, EntityVarValue* value);

	s32 getEntityVariableId(const char* key)
	{
		const s32 count = (s32)s_varDefList.size();
		const EntityVarDef* def = s_varDefList.data();
		for (s32 i = 0; i < count; i++, def++)
		{
			if (strcasecmp(def->name.c_str(), key) == 0)
			{
				return i;
			}
		}
		return -1;
	}

	EntityKey getEntityKey(const char* key)
	{
		for (s32 k = 0; k < EKEY_COUNT; k++)
		{
			if (strcasecmp(c_entityKeyStr[k], key) == 0)
			{
				return EntityKey(k);
			}
		}
		return EKEY_UNKNOWN;
	}

	LogicKey getLogicKey(const char* key)
	{
		for (s32 k = 0; k < LKEY_COUNT; k++)
		{
			if (strcasecmp(c_logicKeyStr[k], key) == 0)
			{
				return LogicKey(k);
			}
		}
		return LKEY_UNKNOWN;
	}

	VarKey getVarKey(const char* key)
	{
		for (s32 k = 0; k < VKEY_COUNT; k++)
		{
			if (strcasecmp(c_varKeyStr[k], key) == 0)
			{
				return VarKey(k);
			}
		}
		return VKEY_UNKNOWN;
	}

	EntityType getEntityType(const char* type)
	{
		for (s32 t = 0; t < ETYPE_COUNT; t++)
		{
			if (strcasecmp(c_entityTypeStr[t], type) == 0)
			{
				return EntityType(t);
			}
		}
		return ETYPE_UNKNOWN;
	}

	const char* getEntityVarName(s32 id)
	{
		if (id < 0 || id >= (s32)s_varDefList.size()) { return ""; }
		return s_varDefList[id].name.c_str();
	}

	EntityVarDef* getEntityVar(s32 id)
	{
		if (id < 0 || id >= (s32)s_varDefList.size()) { return nullptr; }
		return &s_varDefList[id];
	}

	s32 getVariableId(const char* varName)
	{
		const s32 count = (s32)s_varDefList.size();
		const EntityVarDef* varDef = s_varDefList.data();
		for (s32 v = 0; v < count; v++, varDef++)
		{
			if (strcasecmp(varDef->name.c_str(), varName) == 0)
			{
				return v;
			}
		}
		return -1;
	}

	SpriteFrame* getUvDataForSpriteView(EditorSprite* sprite, s32 viewIndex, Vec2f* uv, Vec2f* st, Vec2f* size)
	{
		assert(!sprite->anim.empty());
		s32 frameIndex = sprite->anim[0].views[viewIndex].frameIndex[0];
		SpriteFrame* frame = &sprite->frame[frameIndex];
		SpriteCell* cell = &sprite->cell[frame->cellIndex];

		f32 u0 = f32(cell->u) / (f32)sprite->texGpu->getWidth();
		f32 v1 = f32(cell->v) / (f32)sprite->texGpu->getHeight();
		f32 u1 = f32(cell->u + cell->w) / (f32)sprite->texGpu->getWidth();
		f32 v0 = f32(cell->v + cell->h) / (f32)sprite->texGpu->getHeight();
		if (frame->flip) { std::swap(u0, u1); }

		uv[0] = { u0, v0 };
		uv[1] = { u1, v1 };

		st[0] = { (f32)cell->u, (f32)cell->v };
		st[1] = { f32(cell->u + cell->w), f32(cell->v + cell->h) };

		if (size)
		{
			size->x = frame->widthWS;
			size->z = frame->heightWS;
		}
		return frame;
	}

	bool loadSingleEntityData(Entity* entity)
	{
		// First figure out the display asset.
		if (entity->type == ETYPE_SPIRIT || entity->type == ETYPE_SAFE)
		{
			// Load as a PNG.
			char pngPath[TFE_MAX_PATH];
			sprintf(pngPath, "UI_Images/%s", entity->assetName.c_str());
			entity->image = loadGpuImage(pngPath);
			if (entity->image)
			{
				entity->st[1] = { (f32)entity->image->getWidth(), (f32)entity->image->getHeight() };
			}
			else
			{
				entity->st[1] = entity->st[0];
			}

			entity->size.x = 4.0f;
			entity->size.z = 4.0f;
		}
		else if (entity->type == ETYPE_FRAME)
		{
			// Load a texture from the frame.
			Asset* asset = AssetBrowser::findAsset(entity->assetName.c_str(), TYPE_FRAME);
			if (asset)
			{
				if (!asset->handle)
				{
					asset->handle = AssetBrowser::loadAssetData(asset);
				}

				EditorFrame* frame = (EditorFrame*)getAssetData(asset->handle);
				entity->image = frame->texGpu;
				entity->uv[0].z = 1.0f;
				entity->uv[1].z = 0.0f;
				entity->st[1] = { (f32)entity->image->getWidth(), (f32)entity->image->getHeight() };

				entity->size.x = frame->worldWidth;
				entity->size.z = frame->worldHeight;

				// Handle the offset.
				entity->offset = { frame->offsetX + entity->offsetAdj.x, frame->offsetY + entity->offsetAdj.y, 0.0f };
			}
		}
		else if (entity->type == ETYPE_SPRITE)
		{
			// Load a texture from the wax.
			Asset* asset = AssetBrowser::findAsset(entity->assetName.c_str(), TYPE_SPRITE);
			if (asset)
			{
				if (!asset->handle)
				{
					asset->handle = AssetBrowser::loadAssetData(asset);
				}

				EditorSprite* sprite = (EditorSprite*)getAssetData(asset->handle);
				entity->image = sprite->texGpu;
								
				// Get the actual cell...
				SpriteFrame* frame = getUvDataForSpriteView(sprite, 0, entity->uv, entity->st, &entity->size);

				// Entity views.
				entity->views.resize(32);
				SpriteView* view = entity->views.data();
				for (s32 i = 0; i < 32; i++, view++)
				{
					getUvDataForSpriteView(sprite, i, view->uv, view->st, nullptr);
				}

				// Handle the offset.
				entity->offset = { (f32)frame->offsetX + entity->offsetAdj.x, (f32)frame->offsetY + entity->offsetAdj.y, 0.0f };
			}
		}
		else if (entity->type == ETYPE_3D)
		{
			// Load a texture from the wax.
			Asset* asset = AssetBrowser::findAsset(entity->assetName.c_str(), TYPE_3DOBJ);
			if (asset)
			{
				if (!asset->handle)
				{
					asset->handle = AssetBrowser::loadAssetData(asset);
				}
				entity->obj3d = (EditorObj3D*)getAssetData(asset->handle);
			}
		}
		else
		{
			// TODO: other types.
		}

		return true;
	}

	void writeVariableValue(const EntityVar* var, const EntityVarDef* def, char* varBuffer)
	{
		varBuffer[0] = 0;
		switch (def->type)
		{
			case EVARTYPE_BOOL:
			case EVARTYPE_BOOL2:
			{
				sprintf(varBuffer, "%s", var->value.bValue ? "True" : "False");
			} break;
			case EVARTYPE_FLOAT:
			{
				sprintf(varBuffer, "%f", var->value.fValue);
			} break;
			case EVARTYPE_INT:
			case EVARTYPE_FLAGS:
			{
				sprintf(varBuffer, "%d", var->value.iValue);
			} break;
			case EVARTYPE_STRING_LIST:
			{
				sprintf(varBuffer, "\"%s\"", var->value.sValue.c_str());
			} break;
			case EVARTYPE_INPUT_STRING_PAIR:
			{
				sprintf(varBuffer, "\"%s\" \"%s\"", var->value.sValue.c_str(), var->value.sValue1.c_str());
			} break;
			case EVARTYPE_INPUT_STRING:
			{
				sprintf(varBuffer, "\"%s\"", var->value.sValue.c_str());
			} break;
		}
	}

	// Copy text into a buffer, validating that there is enough room first.
	bool writeToBuffer(const char* srcBuffer, char* buffer, size_t bufferSize)
	{
		const size_t srcLen = strlen(srcBuffer);
		const size_t dstLen = strlen(buffer);
		if (dstLen + srcLen >= bufferSize)
		{
			return false;
		}
		strcat(buffer, srcBuffer);
		return true;
	}

	bool writeVariablesToString(const std::vector<EntityVar>* varList, char* buffer, size_t bufferSize)
	{
		char fmtBuffer[256];
		char varBuffer[256];

		const s32 varCount = (s32)varList->size();
		const EntityVar* var = varList->data();
		for (s32 v = 0; v < varCount; v++, var++)
		{
			const EntityVarDef* def = &s_varDefList[var->defId];
			writeVariableValue(var, def, varBuffer);

			sprintf(fmtBuffer, "\t%s: %s\r\n", def->name.c_str(), varBuffer);
			if (!writeToBuffer(fmtBuffer, buffer, bufferSize)) { return false; }
		}
		return true;
	}

	// Write entity data out to a text buffer.
	bool writeEntityDataToString(const Entity* entity, char* buffer, size_t bufferSize)
	{
		char fmtBuffer[256];
		buffer[0] = 0;

		sprintf(fmtBuffer, "Entity: \"%s\"\r\n", entity->name.c_str());
		if (!writeToBuffer(fmtBuffer, buffer, bufferSize)) { return false; }

		sprintf(fmtBuffer, "\tClass: \"%s\"\r\n", c_entityTypeStr[entity->type]);
		if (!writeToBuffer(fmtBuffer, buffer, bufferSize)) { return false; }

		const s32 logicCount = (s32)entity->logic.size();
		const s32 varCount = (s32)entity->var.size();
		if (logicCount > 0)
		{
			const EntityLogic* logic = entity->logic.data();
			for (s32 l = 0; l < logicCount; l++, logic++)
			{
				sprintf(fmtBuffer, "\tLogic: \"%s\"\r\n", logic->name.c_str());
				if (!writeToBuffer(fmtBuffer, buffer, bufferSize)) { return false; }
				if (!writeVariablesToString(&logic->var, buffer, bufferSize)) { return false; }
			}
		}
		else if (varCount > 0)
		{
			if (!writeVariablesToString(&entity->var, buffer, bufferSize)) { return false; }
		}

		if (entity->type < ETYPE_SPRITE)
		{
			sprintf(fmtBuffer, "\tIcon: \"%s\"\r\n", entity->assetName.c_str());
			if (!writeToBuffer(fmtBuffer, buffer, bufferSize)) { return false; }
		}
		else
		{
			sprintf(fmtBuffer, "\tAsset: \"%s\"\r\n", entity->assetName.c_str());
			if (!writeToBuffer(fmtBuffer, buffer, bufferSize)) { return false; }
		}

		if (entity->offsetAdj.y != 0.0f)
		{
			sprintf(fmtBuffer, "\tAsset_Offset_Y: %f\r\n", entity->offsetAdj.y);
			if (!writeToBuffer(fmtBuffer, buffer, bufferSize)) { return false; }
		}

		if (entity->categories)
		{
			//Category: Special
			const s32 catCount = (s32)s_categoryList.size();
			char catList[1024] = "";
			s32 catFlags = entity->categories;
			bool firstCat = true;
			for (s32 i = 0; i < catCount; i++)
			{
				if (catFlags & (1 << i))
				{
					if (!firstCat)
					{
						strcpy(catList, ", ");
					}
					firstCat = false;
					strcpy(catList, s_categoryList[i].name.c_str());
				}
			}
			sprintf(fmtBuffer, "\tCategory: %s\r\n", catList);
			if (!writeToBuffer(fmtBuffer, buffer, bufferSize)) { return false; }
		}

		return true;
	}

	void writeVariableValueBinary(const EntityVar* var, FileStream* file)
	{
		const EntityVarDef* def = &s_varDefList[var->defId];
		switch (def->type)
		{
			case EVARTYPE_BOOL:
			case EVARTYPE_BOOL2:
			{
				const s32 bValue = var->value.bValue ? 1 : 0;
				file->write(&bValue);
			} break;
			case EVARTYPE_FLOAT:
			{
				file->write(&var->value.fValue);
			} break;
			case EVARTYPE_INT:
			case EVARTYPE_FLAGS:
			{
				file->write(&var->value.iValue);
			} break;
			case EVARTYPE_STRING_LIST:
			{
				file->write(&var->value.sValue);
			} break;
			case EVARTYPE_INPUT_STRING_PAIR:
			{
				file->write(&var->value.sValue);
				file->write(&var->value.sValue1);
			} break;
			case EVARTYPE_INPUT_STRING:
			{
				file->write(&var->value.sValue);
			} break;
		}
	}

	void readVariableValueBinary(EntityVar* var, FileStream* file)
	{
		const EntityVarDef* def = &s_varDefList[var->defId];
		// Clear out the values.
		var->value.iValue = 0;
		var->value.sValue = "";
		var->value.sValue1 = "";
		switch (def->type)
		{
			case EVARTYPE_BOOL:
			case EVARTYPE_BOOL2:
			{
				s32 bValue = 0;
				file->read(&bValue);
				var->value.bValue = bValue != 0;
			} break;
			case EVARTYPE_FLOAT:
			{
				file->read(&var->value.fValue);
			} break;
			case EVARTYPE_INT:
			case EVARTYPE_FLAGS:
			{
				file->read(&var->value.iValue);
			} break;
			case EVARTYPE_STRING_LIST:
			{
				file->read(&var->value.sValue);
			} break;
			case EVARTYPE_INPUT_STRING_PAIR:
			{
				file->read(&var->value.sValue);
				file->read(&var->value.sValue1);
			} break;
			case EVARTYPE_INPUT_STRING:
			{
				file->read(&var->value.sValue);
			} break;
		}
	}

	// Write entity data out to a binary buffer.
	bool writeEntityVarListBinary(const std::vector<EntityVar>* varList, FileStream* file)
	{
		if (!varList || !file) { return false; }

		const s32 varCount = (s32)varList->size();
		file->write(&varCount);

		const EntityVar* var = varList->data();
		for (s32 i = 0; i < varCount; i++, var++)
		{
			file->write(&var->defId);
			writeVariableValueBinary(var, file);
		}
		return true;
	}

	bool readEntityVarListBinary(std::vector<EntityVar>* varList, FileStream* file)
	{
		if (!varList || !file) { return false; }

		s32 varCount = 0;
		file->read(&varCount);
		varList->resize(varCount);

		EntityVar* var = varList->data();
		for (s32 i = 0; i < varCount; i++, var++)
		{
			file->read(&var->defId);
			readVariableValueBinary(var, file);
		}
		return true;
	}

	bool writeEntityDataBinary(const Entity* entity, FileStream* file)
	{
		file->write(&entity->name);
		s32 type = s32(entity->type);
		file->write(&type);

		// LEF_EntityV4
		file->write(&entity->categories);

		file->write(&entity->assetName);
		file->writeBuffer(&entity->offsetAdj, sizeof(Vec3f));

		const s32 logicCount = (s32)entity->logic.size();
		file->write(&logicCount);

		const EntityLogic* logic = entity->logic.data();
		for (s32 i = 0; i < logicCount; i++, logic++)
		{
			file->write(&logic->name);
			writeEntityVarListBinary(&logic->var, file);
		}

		writeEntityVarListBinary(&entity->var, file);
		return true;
	}

	// Read entity data from a binary buffer.
	// Once all entities are loaded, call loadSingleEntityData(entity) for each one
	// to load asset data.
	bool readEntityDataBinary(FileStream* file, Entity* entity, s32 version)
	{
		*entity = {};
		file->read(&entity->name);
		s32 type = 0;
		file->read(&type);
		entity->type = EntityType(type);
		if (version >= LEF_EntityV4)
		{
			file->read(&entity->categories);
		}
		file->read(&entity->assetName);
		file->readBuffer(&entity->offsetAdj, sizeof(Vec3f));

		s32 logicCount = 0;
		file->read(&logicCount);
		entity->logic.resize(logicCount);
		EntityLogic* logic = entity->logic.data();
		for (s32 i = 0; i < logicCount; i++, logic++)
		{
			file->read(&logic->name);
			readEntityVarListBinary(&logic->var, file);
		}

		readEntityVarListBinary(&entity->var, file);
		return true;
	}

	void saveProjectEntityList()
	{
		if (s_projEntityDefList.empty()) { return; }

		char projEntityDataPath[TFE_MAX_PATH];
		Project* project = project_get();
		if (project && project->active)
		{
			// Save the custom templates to the project directory.
			sprintf(projEntityDataPath, "%s/%s.ini", project->path, "CustomEntityDef");
		}
		else
		{
			// Save the custom templates to the editor export path, if it exists.
			if (FileUtil::directoryExists(s_editorConfig.exportPath))
			{
				char path[TFE_MAX_PATH];
				strcpy(path, s_editorConfig.exportPath);
				size_t len = strlen(path);
				if (path[len - 1] != '/' && path[len - 1] != '\\')
				{
					path[len] = '/';
					path[len + 1] = 0;
				}
				FileUtil::fixupPath(path);
				sprintf(projEntityDataPath, "%s%s.ini", path, "CustomEntityDef");
			}
		}

		FileStream file;
		if (!file.open(projEntityDataPath, FileStream::MODE_WRITE))
		{
			return;
		}

		char buffer[4096];
		const s32 count = (s32)s_projEntityDefList.size();
		const Entity* entity = s_projEntityDefList.data();
		for (s32 i = 0; i < count; i++, entity++)
		{
			if (writeEntityDataToString(entity, buffer, 4096))
			{
				file.writeBuffer(buffer, (u32)strlen(buffer));
				file.writeBuffer("\r\n", (u32)strlen("\r\n"));
			}
		}

		file.close();
	}

	s32 getCategoryFlag(const char* name)
	{
		const s32 count = (s32)s_categoryList.size();
		const Category* cat = s_categoryList.data();
		for (s32 i = 0; i < count; i++, cat++)
		{
			if (strcasecmp(name, cat->name.c_str()) == 0)
			{
				return 1 << i;
			}
		}
		return 0;
	}

	bool loadEntityData(const char* localDir, bool projectList)
	{
		std::vector<Entity>* entityDefList = nullptr;
		char entityDefPath[TFE_MAX_PATH];
		if (projectList)
		{
			s_projEntityDefList.clear();
			entityDefList = &s_projEntityDefList;
			strcpy(entityDefPath, localDir);
		}
		else
		{
			s_entityDefList.clear();
			s_categoryList.clear();
			entityDefList = &s_entityDefList;

			const char* progPath = TFE_Paths::getPath(TFE_PathType::PATH_PROGRAM);
			sprintf(entityDefPath, "%sEditorDef/%s/EntityDef.ini", progPath, localDir);
		}
		assert(entityDefList);
		FileUtil::fixupPath(entityDefPath);

		FileStream file;
		if (!file.open(entityDefPath, FileStream::MODE_READ))
		{
			if (!projectList) { LE_ERROR("Cannot open Entity Definitions - '%s'.", entityDefPath); }
			return false;
		}
		size_t len = file.getSize();
		s_fileData.resize(len);
		file.readBuffer(s_fileData.data(), (u32)len);
		file.close();

		TFE_Parser parser;
		size_t bufferPos = 0;
		parser.init((char*)s_fileData.data(), s_fileData.size());
		parser.addCommentString("#");
		parser.enableColonSeperator();

		const char* line;
		char* endPtr = nullptr;
		Entity* curEntity = nullptr;
		Category* curCat = nullptr;
		std::vector<EntityVar>* varList = nullptr;
		TokenList tokens;
		line = parser.readLine(bufferPos);
		while (line)
		{
			parser.tokenizeLine(line, tokens);
			if (tokens.size() >= 2)
			{
				const s32 varId = getEntityVariableId(tokens[0].c_str());
				if (varId >= 0)
				{
					const EntityVarDef* def = &s_varDefList[varId];
					const char* valueStr = tokens[1].c_str();
					const char* valueStr1 = tokens.size() >= 3 ? tokens[2].c_str() : "";

					EntityVar var = {};
					var.defId = varId;
					parseValue(tokens, def->type, &var.value);
					varList->push_back(var);
				}
				else
				{
					EntityKey key = getEntityKey(tokens[0].c_str());
					switch (key)
					{
						case EKEY_ENTITY:
						{
							entityDefList->push_back({});
							curEntity = &entityDefList->back();
							curEntity->id = s32(entityDefList->size()) - 1;
							curEntity->name = tokens[1];
							curEntity->categories = 0;
							varList = &curEntity->var;
						} break;
						case EKEY_CLASS:
						{
							curEntity->type = getEntityType(tokens[1].c_str());
						} break;
						case EKEY_LOGIC:
						{
							EntityLogic newLogic = {};
							newLogic.name = tokens[1];
							curEntity->logic.push_back(newLogic);
							varList = &curEntity->logic.back().var;
						} break;
						case EKEY_ICON:
						case EKEY_ASSET:
						{
							curEntity->assetName = tokens[1];
						} break;
						case EKEY_ASSET_OFFSET_Y:
						{
							curEntity->offsetAdj.y += strtof(tokens[1].c_str(), &endPtr);
						} break;
						case EKEY_CATEGORY:
						{
							if (!curEntity)
							{
								// New category.
								s32 index = (s32)s_categoryList.size();
								s_categoryList.push_back({});
								curCat = &s_categoryList.back();

								curCat->flag = 1 << index;
								curCat->name = tokens[1];
								curCat->tooltip = "";

								if (curCat->name == "Enemy")
								{
									s_enemyCategoryFlag = curCat->flag;
								}
							}
							else
							{
								// Category list for the current entity.
								s32 catCount = (s32)tokens.size() - 1;
								s32 catFlags = 0;
								for (s32 c = 0; c < catCount; c++)
								{
									catFlags |= getCategoryFlag(tokens[c + 1].c_str());
								}
								curEntity->categories = catFlags;
							}
						} break;
						case EKEY_TOOLTIP:
						{
							if (!curEntity && curCat)
							{
								// Category tooltip.
								curCat->tooltip = tokens[1];
							}
						} break;
						case EKEY_UNKNOWN:
						default:
						{
							LE_WARNING("Invalid Entity Key '%s'.", tokens[0].c_str());
						}
					}
				}
			}
			else
			{
				LE_WARNING("Invalid key/value pair '%s'.", line);
			}

			line = parser.readLine(bufferPos);
		}
		LE_INFO("Loaded %d entity definitions from '%s'.", (s32)entityDefList->size(), entityDefPath);

		// Now that the definitions are loaded, get the actual data...
		const s32 count = (s32)entityDefList->size();
		Entity* entity = entityDefList->data();
		for (s32 i = 0; i < count; i++, entity++)
		{
			loadSingleEntityData(entity);
		}

		// If this is the project list, append it to the master list to simply the code.
		if (projectList)
		{
			const s32 dstCount = (s32)s_entityDefList.size();
			const s32 srcCount = (s32)s_projEntityDefList.size();
			Entity* src = s_projEntityDefList.data();
			for (s32 i = 0; i < srcCount; i++, src++)
			{
				// Make sure to add the "custom" category.
				src->categories |= 1;
				s_entityDefList.push_back(*src);
			}
		}

		return true;
	}

	bool loadLogicData(const char* localDir)
	{
		s_logicDefList.clear();

		char logicDefPath[TFE_MAX_PATH];
		const char* progPath = TFE_Paths::getPath(TFE_PathType::PATH_PROGRAM);
		sprintf(logicDefPath, "%sEditorDef/%s/LogicDef.ini", progPath, localDir);
		FileUtil::fixupPath(logicDefPath);

		FileStream file;
		if (!file.open(logicDefPath, FileStream::MODE_READ))
		{
			LE_ERROR("Cannot open Logic Definitions - '%s'.", logicDefPath);
			return false;
		}
		size_t len = file.getSize();
		s_fileData.resize(len);
		file.readBuffer(s_fileData.data(), (u32)len);
		file.close();

		TFE_Parser parser;
		size_t bufferPos = 0;
		parser.init((char*)s_fileData.data(), s_fileData.size());
		parser.addCommentString("#");
		parser.enableColonSeperator();

		const char* line;
		char* endPtr = nullptr;
		LogicDef* curLogic = nullptr;
		TokenList tokens;
		line = parser.readLine(bufferPos);
		while (line)
		{
			parser.tokenizeLine(line, tokens);
			if (tokens.size() >= 2)
			{
				LogicKey key = getLogicKey(tokens[0].c_str());
				switch (key)
				{
					case LKEY_LOGIC:
					{
						s_logicDefList.push_back({});
						curLogic = &s_logicDefList.back();
						curLogic->id = s32(s_logicDefList.size()) - 1;
						curLogic->name = tokens[1];
					} break;
					case LKEY_TOOLTIP:
					{
						// Fix-up line-endings.
						char fixup[512];
						const s32 len = (s32)tokens[1].length();
						const char* src = tokens[1].c_str();
						s32 outLen = 0;
						for (s32 i = 0; i < len; i++)
						{
							if (src[i] != '\\' || src[i + 1] != 'n')
							{
								fixup[outLen++] = src[i];
							}
							else
							{
								fixup[outLen++] = '\n';
								i++;
							}
						}
						fixup[outLen] = 0;
						curLogic->tooltip = fixup;
					} break;
					case LKEY_VAR:
					{
						curLogic->var.push_back({ getVariableId(tokens[1].c_str()), VAR_TYPE_STD });
					} break;
					case LKEY_DEFVAR:
					{
						curLogic->var.push_back({ getVariableId(tokens[1].c_str()), VAR_TYPE_DEFAULT });
					} break;
					case LKEY_REQVAR:
					{
						curLogic->var.push_back({ getVariableId(tokens[1].c_str()), VAR_TYPE_REQUIRED });
					} break;
					default:
					{
						LE_WARNING("Invalid key '%s' in LogicDef.ini", tokens[0].c_str());
					}
				}
			}
			line = parser.readLine(bufferPos);
		}
		LE_INFO("Loaded %d logic definitions from '%s'.", (s32)s_logicDefList.size(), logicDefPath);

		return true;
	}

	s32 getLogicId(const char* name)
	{
		const s32 count = (s32)s_logicDefList.size();
		const LogicDef* def = s_logicDefList.data();
		for (s32 i = 0; i < count; i++, def++)
		{
			if (strcasecmp(def->name.c_str(), name) == 0)
			{
				return def->id;
			}
		}
		return -1;
	}

	bool loadVariableData(const char* localDir)
	{
		s_varDefList.clear();

		char varDefPath[TFE_MAX_PATH];
		const char* progPath = TFE_Paths::getPath(TFE_PathType::PATH_PROGRAM);
		sprintf(varDefPath, "%sEditorDef/%s/VarDef.ini", progPath, localDir);
		FileUtil::fixupPath(varDefPath);

		FileStream file;
		if (!file.open(varDefPath, FileStream::MODE_READ))
		{
			LE_ERROR("Cannot open Variable Definitions - '%s'.", varDefPath);
			return false;
		}
		size_t len = file.getSize();
		s_fileData.resize(len);
		file.readBuffer(s_fileData.data(), (u32)len);
		file.close();

		TFE_Parser parser;
		size_t bufferPos = 0;
		parser.init((char*)s_fileData.data(), s_fileData.size());
		parser.addCommentString("#");
		parser.enableColonSeperator();

		const char* line;
		char* endPtr = nullptr;
		EntityVarDef* curVar = nullptr;
		TokenList tokens;
		line = parser.readLine(bufferPos);
		while (line)
		{
			parser.tokenizeLine(line, tokens);
			if (tokens.size() >= 2)
			{
				VarKey key = getVarKey(tokens[0].c_str());
				switch (key)
				{
					case VKEY_VAR:
					{
						s_varDefList.push_back({});
						curVar = &s_varDefList.back();
						curVar->id = s32(s_varDefList.size()) - 1;
						curVar->name = tokens[1];
					} break;
					case VKEY_TYPE:
					{
						const char* typeStr = tokens[1].c_str();
						curVar->defValue.iValue = 0;
						curVar->defValue1.iValue = 0;
						if (strcasecmp(typeStr, "Bool") == 0)
						{
							curVar->type = EVARTYPE_BOOL;
						}
						else if (strcasecmp(typeStr, "Int") == 0)
						{
							curVar->type = EVARTYPE_INT;
						}
						else if (strcasecmp(typeStr, "Float") == 0)
						{
							curVar->type = EVARTYPE_FLOAT;
						}
						else if (strcasecmp(typeStr, "Flags") == 0)
						{
							curVar->type = EVARTYPE_FLAGS;
						}
						else if (strcasecmp(typeStr, "StringList") == 0)
						{
							curVar->type = EVARTYPE_STRING_LIST;
						}
						else if (strcasecmp(typeStr, "InputStringPair") == 0)
						{
							curVar->type = EVARTYPE_INPUT_STRING_PAIR;
						}
						else if (strcasecmp(typeStr, "InputString") == 0)
						{
							curVar->type = EVARTYPE_INPUT_STRING;
						}
						else if (strcasecmp(typeStr, "Bool2") == 0)
						{
							curVar->type = EVARTYPE_BOOL2;
						}
						else
						{
							curVar->type = EVARTYPE_BOOL; // Just assume bool.
							LE_WARNING("Variable Parsing Error: Unknown variable type '%s'", typeStr);
						}
					} break;
					case VKEY_DEFAULT:
					{
						parseValue(tokens, curVar->type, &curVar->defValue);
					} break;
					case VKEY_FLAG:
					{
						if (tokens.size() >= 3)
						{
							s32 val = strtol(tokens[2].c_str(), &endPtr, 10);
							curVar->flags.push_back({ tokens[1], val });
						}
						else
						{
							LE_WARNING("Variable Parsing Error: Invalid 'Flag' format, it should be 'Flag: \"FlagName\", FlagIntValue' : '%s'", line);
						}
					} break;
					case VKEY_VALUE:
					{
						if (curVar->type == EVARTYPE_STRING_LIST)
						{
							curVar->strList.push_back(tokens[1]);
						}
						else
						{
							LE_WARNING("Variable Parsing Error: Invalid use of 'Value', it is only valid for the 'StringList' type: '%s'", line);
						}
					} break;
					case VKEY_NAME0:
					{
						curVar->name0 = tokens[1];
					} break;
					case VKEY_NAME1:
					{
						curVar->name1 = tokens[1];
					} break;
				}
			}
			line = parser.readLine(bufferPos);
		}
		LE_INFO("Loaded %d variable definitions from '%s'.", (s32)s_varDefList.size(), varDefPath);

		return true;
	}

	void removeQuotes(const char* src, char* dst)
	{
		s32 firstNonQuote = -1;
		s32 lastNonQuote = -1;
		const size_t len = strlen(src);
		for (size_t i = 0; i < len; i++)
		{
			if (firstNonQuote < 0 && src[i] != '\"')
			{
				firstNonQuote = s32(i);
				lastNonQuote = firstNonQuote;
			}
			else if (src[i] != '\"')
			{
				lastNonQuote = s32(i);
			}
		}
		if (firstNonQuote < 0 || lastNonQuote < firstNonQuote)
		{
			dst[0] = 0;
			return;
		}
		for (s32 i = firstNonQuote; i <= lastNonQuote; i++)
		{
			dst[i - firstNonQuote] = src[i];
		}
		dst[lastNonQuote-firstNonQuote+1] = 0;
	}

	void parseValue(const TokenList& tokens, EntityVarType type, EntityVarValue* value)
	{
		char* endPtr = nullptr;
		const char* valueStr = tokens[1].c_str();
		const char* valueStr1 = tokens.size() >= 3 ? tokens[2].c_str() : "";
		switch (type)
		{
			case EVARTYPE_BOOL:
			case EVARTYPE_BOOL2:
			{
				value->bValue = strcasecmp(valueStr, "True") == 0 || strcasecmp(valueStr, "1") == 0;
			} break;
			case EVARTYPE_FLOAT:
			{
				value->fValue = strtof(valueStr, &endPtr);
			} break;
			case EVARTYPE_INT:
			case EVARTYPE_FLAGS:
			{
				value->iValue = strtol(valueStr, &endPtr, 10);
			} break;
			case EVARTYPE_STRING_LIST:
			{
				value->sValue = valueStr;
			} break;
			case EVARTYPE_INPUT_STRING_PAIR:
			{
				value->sValue = valueStr;

				char fixedStr1[256];
				removeQuotes(valueStr1, fixedStr1);
				value->sValue1 = fixedStr1;
			} break;
			case EVARTYPE_INPUT_STRING:
			{
				value->sValue = valueStr;
			} break;
		}
	}

	bool varListsMatch(const std::vector<EntityVar>& list0, const std::vector<EntityVar>& list1)
	{
		if (list0.size() != list1.size()) { return false; }
		const s32 count = (s32)list0.size();
		const EntityVar* var0 = list0.data();
		const EntityVar* var1 = list1.data();
		for (s32 i = 0; i < count; i++, var0++, var1++)
		{
			if (var0->defId != var1->defId) { return false; }
			const EntityVarDef* def = &s_varDefList[var0->defId];
			switch (def->type)
			{
				case EVARTYPE_BOOL:
				case EVARTYPE_BOOL2:
				{
					if (var0->value.bValue != var1->value.bValue) { return false; }
				} break;
				case EVARTYPE_FLOAT:
				{
					if (var0->value.fValue != var1->value.fValue) { return false; }
				} break;
				case EVARTYPE_INT:
				case EVARTYPE_FLAGS:
				{
					if (var0->value.iValue != var1->value.iValue) { return false; }
				} break;
				case EVARTYPE_STRING_LIST:
				{
					if (strcasecmp(var0->value.sValue.c_str(), var1->value.sValue.c_str()) != 0) { return false; }
				} break;
				case EVARTYPE_INPUT_STRING_PAIR:
				{
					if (strcasecmp(var0->value.sValue.c_str(), var1->value.sValue.c_str()) != 0) { return false; }
					if (strcasecmp(var0->value.sValue1.c_str(), var1->value.sValue1.c_str()) != 0) { return false; }
				} break;
				case EVARTYPE_INPUT_STRING:
				{
					if (strcasecmp(var0->value.sValue.c_str(), var1->value.sValue.c_str()) != 0) { return false; }
				} break;
			}
		}
		return true;
	}

	bool logicListsMatch(const std::vector<EntityLogic>& list0, const std::vector<EntityLogic>& list1)
	{
		if (list0.size() != list1.size()) { return false; }
		const s32 count = (s32)list0.size();
		const EntityLogic* logic0 = list0.data();
		const EntityLogic* logic1 = list1.data();
		for (s32 i = 0; i < count; i++, logic0++, logic1++)
		{
			if (strcasecmp(logic0->name.c_str(), logic1->name.c_str()) != 0)
			{
				return false;
			}
			if (!varListsMatch(logic0->var, logic1->var))
			{
				return false;
			}
		}
		return true;
	}

	bool entityDefsEqual(const Entity* e0, const Entity* e1)
	{
		return (strcasecmp(e0->name.c_str(), e1->name.c_str()) == 0 && strcasecmp(e0->assetName.c_str(), e1->assetName.c_str()) == 0 && e0->type == e1->type &&
			logicListsMatch(e0->logic, e1->logic) && varListsMatch(e0->var, e1->var) && e0->categories == e1->categories);
	}

	bool entityDefsEqualIgnoreName(const Entity* e0, const Entity* e1)
	{
		return (strcasecmp(e0->assetName.c_str(), e1->assetName.c_str()) == 0 && e0->type == e1->type &&
			logicListsMatch(e0->logic, e1->logic) && varListsMatch(e0->var, e1->var));
	}

	s32 getEntityDefId(const Entity* srcEntity)
	{
		const s32 count = (s32)s_entityDefList.size();
		const Entity* entity = s_entityDefList.data();
		for (s32 i = 0; i < count; i++, entity++)
		{
			if (entityDefsEqualIgnoreName(entity, srcEntity))
			{
				return i;
			}
		}
		return -1;
	}
}
