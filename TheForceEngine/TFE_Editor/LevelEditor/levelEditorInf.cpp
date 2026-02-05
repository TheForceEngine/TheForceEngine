#include "levelEditorInf.h"
#include "levelEditor.h"
#include "levelEditorData.h"
#include "levelEditorHistory.h"
#include "entity.h"
#include "selection.h"
#include "infoPanel.h"
#include "browser.h"
#include "camera.h"
#include "error.h"
#include "sharedState.h"
#include <TFE_FrontEndUI/frontEndUi.h>
#include <TFE_Editor/AssetBrowser/assetBrowser.h>
#include <TFE_Asset/imageAsset.h>
#include <TFE_Editor/LevelEditor/Rendering/viewport.h>
#include <TFE_Editor/errorMessages.h>
#include <TFE_Editor/editorConfig.h>
#include <TFE_Editor/editorLevel.h>
#include <TFE_Editor/editorProject.h>
#include <TFE_Editor/editorResources.h>
#include <TFE_Editor/editor.h>
#include <TFE_Editor/editorComboBox.h>
#include <TFE_Editor/EditorAsset/editorAsset.h>
#include <TFE_Editor/EditorAsset/editorTexture.h>
#include <TFE_Editor/EditorAsset/editorFrame.h>
#include <TFE_Editor/EditorAsset/editorSprite.h>
#include <TFE_Editor/LevelEditor/Rendering/grid2d.h>
#include <TFE_Input/input.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_RenderShared/lineDraw2d.h>
#include <TFE_RenderShared/lineDraw3d.h>
#include <TFE_Jedi/Level/rwall.h>
#include <TFE_Jedi/Level/rsector.h>
#include <TFE_System/system.h>
#include <TFE_Settings/settings.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_Archive/archive.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Polygon/polygon.h>
#include <TFE_System/parser.h>
#include <TFE_System/math.h>
#include <TFE_Ui/ui.h>
#include <SDL.h>

#include <algorithm>
#include <vector>
#include <string>
#include <map>

using namespace TFE_Editor;

namespace LevelEditor
{
	Editor_LevelInf s_levelInf;
	static char s_infArg0[256], s_infArg1[256], s_infArg2[256], s_infArg3[256], s_infArg4[256], s_infArgExtra[256];

	enum InfEditorConst
	{
		POPUP_RESTORE_COUNT = 3,
	};

	enum SelectionType
	{
		SELTYPE_TRIGGER_CLIENT = 0,
		SELTYPE_ELEV_SLAVE,
		SELTYPE_ELEV_MSG_TARGET,
		SELTYPE_ELEV_ADJOIN0,
		SELTYPE_ELEV_ADJOIN1,
		SELTYPE_ELEV_DONOR_SECTOR,
		SELTYPE_ELEV_SECTOR_CENTER,
		SELTYPE_ELEV_POSITION,
		SELTYPE_ELEV_ANGLE,
		SELTYPE_TELEPORT_TARGET,
		SELTYPE_TELEPORT_POSITION,
	};

	enum InfEditorMode
	{
		INF_MODE_UI = 0,
		INF_MODE_CODE,
		INF_MODE_COUNT
	};

	struct InfEditor
	{
		InfEditorMode mode = INF_MODE_UI;
		EditorSector* sector = nullptr;
		Editor_InfItem* item = nullptr;
		s32 itemWallIndex = -1;

		s32 comboClassIndex = 0;
		s32 comboElevTypeIndex = 0;
		s32 comboElevVarIndex = 0;
		s32 comboElevAddContentIndex = 0;
		s32 comboElevCmdIndex = 0;
		s32 curClassIndex = -1;
		s32 curPropIndex = -1;
		s32 curContentIndex = -1;
		s32 curStopCmdIndex = -1;

		bool showAnim = false;
		bool playAnim = false;
		f32 time = 0.0f;
	};
	
	struct InfEditorState
	{
		Editor_InfClass* editClass;
		InfEditor editorState;
		SelectionType type;
		s32 index0;
		s32 index1;
	};
		
	struct InfSectorMod
	{
		Editor_InfItem* item = nullptr;
		EditorSector* sector = nullptr;
		Editor_InfElevator* elev = nullptr;
		s32 stopIndex = -1;
		f32 time = -1.0f;

		EditorSector prevState = {};
		std::vector<EditorSector> mirrorState;
	};

	const char* c_elevStopCmdName[] =
	{
		"Message", // ISC_MESSAGE
		"Adjoin",  // ISC_ADJOIN
		"Texture", // ISC_TEXTURE
		"Page",    // ISC_PAGE
		"ScriptCall", // ISC_SCRIPTCALL
	};

	const char* c_infElevTypeName[] =
	{
		// Special, "high level" elevators.
		"Basic",		// IET_BASIC
		"Basic_Auto",	// IET_BASIC_AUTO
		"Inv",			// IET_INV
		"Door",			// IET_DOOR
		"Door_Inv",		// IET_DOOR_INV
		"Door_Mid",		// IET_DOOR_MID
		"Morph_Spin1",	// IET_MORPH_SPIN1
		"Morph_Spin2",	// IET_MORPH_SPIN2
		"Morph_Move1",	// IET_MORPH_MOVE1
		"Morph_Move1",	// IET_MORPH_MOVE2

		// Standard Elevators
		"Move_Ceiling",			// IET_MOVE_CEILING
		"Move_Floor",			// IET_MOVE_FLOOR
		"Move_FC",				// IET_MOVE_FC
		"Move_Offset",			// IET_MOVE_OFFSET
		"Move_Wall",			// IET_MOVE_WALL
		"Rotate_Wall",			// IET_ROTATE_WALL
		"Scroll_Wall",			// IET_SCROLL_WALL
		"Scroll_Floor",			// IET_SCROLL_FLOOR
		"Scroll_Ceiling",		// IET_SCROLL_CEILING
		"Change_Light",			// IET_CHANGE_LIGHT
		"Change_Wall_Light",	// IET_CHANGE_WALL_LIGHT
	};

	const char* c_infTriggerTypeName[] =
	{
		"Standard", // ITRIGGER_WALL
		"Sector",   // ITRIGGER_SECTOR,
		"Switch1",  // ITRIGGER_SWITCH1,
		"Toggle",   // ITRIGGER_TOGGLE,
		"Single",   // ITRIGGER_SINGLE
	};

	const char* c_infTeleporterTypeName[] =
	{
		"Basic",   // TELEPORT_BASIC
		"Chute",   // TELEPORT_CHUTE,
	};

	const char* c_selectableTriggerTypes[] =
	{
		"Standard", // ITRIGGER_WALL
		"Switch1",  // ITRIGGER_SWITCH1,
		"Toggle",   // ITRIGGER_TOGGLE,
		"Single",   // ITRIGGER_SINGLE
	};

	const char* c_infWallFlags1Names[] =
	{
		"Mask Wall",
		"Illum Sign",
		"Horz Flip Tex",
		"Change WallLight",
		"Tex Anchored",
		"Wall Morphs",
		"Scroll Top Tex",
		"Scroll Mid Tex",
		"Scroll Bottom",
		"Scroll Sign",
		"Hide On Map",
		"Show On Map",
		"Sign Anchored",
		"Damage Wall",
		"Show As Ledge",
		"Show As Door",
	};
	const char* c_infWallFlags3Names[] =
	{
		"Always Walk",
		"Solid Wall",
		"Player Walk Only",
		"Cannot Shoot Thru",
	};
	const char* c_infSectorFlag1Names[] =
	{
		"Exterior",
		"Door",
		"Mag Seal",
		"Ext Ceil Adj",
		"Ice Floor",
		"Snow Floor",
		"Exploding Wall",
		"Pit",
		"Ext Floor Adj",
		"Crushing",
		"No Walls",
		"Low Damage",
		"High Damage",
		"No Smart Obj",
		"Smart Obj",
		"Subsector (Internal)",
		"Safe Sector",
		"Rendered (Internal)",
		"Player (Internal)",
		"Secret",
	};

	const TriggerType c_selectableTriggerTypeId[] =
	{
		ITRIGGER_WALL,
		ITRIGGER_SWITCH1,
		ITRIGGER_TOGGLE,
		ITRIGGER_SINGLE
	};

	static InfEditor s_infEditor = {};
	static InfEditorState s_infEditorState;

	static ImVec2 s_popupPos;
	static s32 s_restorePos = 0;

	// Animation
	static InfSectorMod s_sectorMod = {};
	static std::vector<f32> s_relTime;
	static std::vector<f32> s_stopValue;
	static std::vector<EditorSector*> s_sectorsToFixup;

	void editor_selectViewportFeature(Editor_InfClass* editClass, SelectMode mode, SelectionType type, s32 index0 = -1, s32 index1 = -1);
	void modifySectorGeometry(Editor_InfItem* item = nullptr, EditorSector* sector = nullptr, Editor_InfElevator* elev = nullptr, s32 stopIndex = -1);
	void modifySectorGeometryTime(Editor_InfItem* item = nullptr, EditorSector* sector = nullptr, Editor_InfElevator* elev = nullptr, f32 time = -1.0f);

	void editor_infInit()
	{
		s_levelInf.item.clear();
		s_levelInf.elevator.clear();
		s_levelInf.trigger.clear();
		s_levelInf.teleport.clear();
		editor_comboBoxInit();
	}

	void editor_infDestroy()
	{
		s32 count = (s32)s_levelInf.elevator.size();
		for (s32 i = 0; i < count; i++)
		{
			delete s_levelInf.elevator[i];
		}

		count = (s32)s_levelInf.trigger.size();
		for (s32 i = 0; i < count; i++)
		{
			delete s_levelInf.trigger[i];
		}

		count = (s32)s_levelInf.teleport.size();
		for (s32 i = 0; i < count; i++)
		{
			delete s_levelInf.teleport[i];
		}

		editor_infInit();
	}

	Editor_InfElevator* allocElev(Editor_InfItem* item)
	{
		Editor_InfElevator* elev = new Editor_InfElevator();
		s_levelInf.elevator.push_back(elev);
		elev->classId = IIC_ELEVATOR;
		item->classData.push_back((Editor_InfClass*)elev);
		return elev;
	}

	Editor_InfTrigger* allocTrigger(Editor_InfItem* item)
	{
		Editor_InfTrigger* trigger = new Editor_InfTrigger();
		s_levelInf.trigger.push_back(trigger);
		trigger->classId = IIC_TRIGGER;
		item->classData.push_back((Editor_InfClass*)trigger);
		return trigger;
	}

	Editor_InfTeleporter* allocTeleporter(Editor_InfItem* item)
	{
		Editor_InfTeleporter* teleport = new Editor_InfTeleporter();
		s_levelInf.teleport.push_back(teleport);
		teleport->classId = IIC_TELEPORTER;
		item->classData.push_back((Editor_InfClass*)teleport);
		return teleport;
	}

	void freeElevator(Editor_InfElevator* elev)
	{
		const s32 count = (s32)s_levelInf.elevator.size();
		s32 index = -1;
		for (s32 i = 0; i < count; i++)
		{
			if (s_levelInf.elevator[i] == elev)
			{
				index = i;
				break;
			}
		}
		if (index < 0) { return; }
		delete elev;
		for (s32 i = index; i < count - 1; i++)
		{
			s_levelInf.elevator[i] = s_levelInf.elevator[i + 1];
		}
		s_levelInf.elevator.pop_back();
	}

	void freeTrigger(Editor_InfTrigger* trigger)
	{
		const s32 count = (s32)s_levelInf.trigger.size();
		s32 index = -1;
		for (s32 i = 0; i < count; i++)
		{
			if (s_levelInf.trigger[i] == trigger)
			{
				index = i;
				break;
			}
		}
		if (index < 0) { return; }
		delete trigger;
		for (s32 i = index; i < count - 1; i++)
		{
			s_levelInf.trigger[i] = s_levelInf.trigger[i + 1];
		}
		s_levelInf.trigger.pop_back();
	}

	void freeTeleporter(Editor_InfTeleporter* teleporter)
	{
		const s32 count = (s32)s_levelInf.teleport.size();
		s32 index = -1;
		for (s32 i = 0; i < count; i++)
		{
			if (s_levelInf.teleport[i] == teleporter)
			{
				index = i;
				break;
			}
		}
		if (index < 0) { return; }
		delete teleporter;
		for (s32 i = index; i < count - 1; i++)
		{
			s_levelInf.teleport[i] = s_levelInf.teleport[i + 1];
		}
		s_levelInf.teleport.pop_back();
	}
		
	Editor_InfElevator* getElevFromClassData(Editor_InfClass* data)
	{
		if (data->classId != IIC_ELEVATOR) { return nullptr; }
		return (Editor_InfElevator*)data;
	}

	const Editor_InfElevator* getElevFromClassData(const Editor_InfClass* data)
	{
		if (data->classId != IIC_ELEVATOR) { return nullptr; }
		return (Editor_InfElevator*)data;
	}

	Editor_InfTrigger* getTriggerFromClassData(Editor_InfClass* data)
	{
		if (data->classId != IIC_TRIGGER) { return nullptr; }
		return (Editor_InfTrigger*)data;
	}

	const Editor_InfTrigger* getTriggerFromClassData(const Editor_InfClass* data)
	{
		if (data->classId != IIC_TRIGGER) { return nullptr; }
		return (Editor_InfTrigger*)data;
	}

	Editor_InfTeleporter* getTeleporterFromClassData(Editor_InfClass* data)
	{
		if (data->classId != IIC_TELEPORTER) { return nullptr; }
		return (Editor_InfTeleporter*)data;
	}

	const Editor_InfTeleporter* getTeleporterFromClassData(const Editor_InfClass* data)
	{
		if (data->classId != IIC_TELEPORTER) { return nullptr; }
		return (Editor_InfTeleporter*)data;
	}

	EditorSector* findSector(const char* itemName)
	{
		const s32 count = (s32)s_level.sectors.size();
		EditorSector* sector = s_level.sectors.data();
		for (s32 i = 0; i < count; i++, sector++)
		{
			// Note in vanilla DF, only the first 16 characters are compared.
			if (strncasecmp(itemName, sector->name.c_str(), 16) == 0)
			{
				return sector;
			}
		}
		return nullptr;
	}

	void editor_parseMessage(Editor_InfMessageType* type, u32* arg1, u32* arg2, u32* evt, const char* infArg0, const char* infArg1, const char* infArg2, bool elevator)
	{
		const KEYWORD msgId = getKeywordIndex(infArg0);
		char* endPtr = nullptr;

		switch (msgId)
		{
			case KW_NEXT_STOP:
				*type = IMT_NEXT_STOP;
				break;
			case KW_PREV_STOP:
				*type = IMT_PREV_STOP;
				break;
			case KW_GOTO_STOP:
				*type = IMT_GOTO_STOP;
				*arg1 = strtoul(infArg1, &endPtr, 10);
				if (evt) { *evt = INF_EVENT_NONE; }
				break;
			case KW_MASTER_ON:
				*type = IMT_MASTER_ON;
				break;
			case KW_MASTER_OFF:
				*type = IMT_MASTER_OFF;
				break;
			case KW_SET_BITS:
				*type = IMT_SET_BITS;
				*arg1 = strtoul(infArg1, &endPtr, 10);
				*arg2 = strtoul(infArg2, &endPtr, 10);
				if (evt) { *evt = INF_EVENT_NONE; }
				break;
			case KW_CLEAR_BITS:
				*type = IMT_CLEAR_BITS;
				*arg1 = strtoul(infArg1, &endPtr, 10);
				*arg2 = strtoul(infArg2, &endPtr, 10);
				if (evt) { *evt = INF_EVENT_NONE; }
				break;
			case KW_COMPLETE:
				*type = IMT_COMPLETE;
				*arg1 = strtoul(infArg1, &endPtr, 10);
				if (evt) { *evt = INF_EVENT_NONE; }
				break;
			case KW_LIGHTS:
				*type = IMT_LIGHTS;
				break;
			case KW_CAMERA:
				*type = IMT_CAMERA;
				break;
			case KW_M_TRIGGER:
			default:
				if (elevator)
				{
					// Elevators can use a few additional message types, but these are treated as M_TRIGGER for either trigger type.
					switch (msgId)
					{
						case KW_DONE:
							*type = IMT_DONE;
							break;
						case KW_WAKEUP:
							*type = IMT_WAKEUP;
							break;
						default:
							*type = IMT_TRIGGER;
					}
				}
				else // Trigger
				{
					*type = IMT_TRIGGER;
				}
		}
	}

	// TODO: Share code.
	void parseTarget(Editor_InfMessage* msg, char* arg)
	{
		char* endPtr = nullptr;
		msg->targetWall = -1;

		// There should be a variant of strstr() that returns a non-constant pointer, but in Visual Studio it is always constant.
		char* parenOpen = (char*)strstr(arg, "(");
		// This message targets a wall rather than a whole sector.
		if (parenOpen)
		{
			*parenOpen = 0;
			parenOpen++;

			char* parenClose = (char*)strstr(arg, ")");
			// This should never be true and this doesn't seem to be hit at runtime.
			// I wonder if this was meant to be { char* parenClose = (char*)strstr(parenOpen, ")"); } - which would make more sense.
			// Or it could have been check *before* the location at ")" was set to 0 above.
			if (parenClose)
			{
				*parenClose = 0;
			}
			// Finally parse the integer and set the wall index.
			msg->targetWall = strtol(parenOpen, &endPtr, 10);
		}
		msg->targetSector = arg;
	}

	void parseTarget(Editor_InfClient* client, char* arg)
	{
		char* endPtr = nullptr;
		client->targetWall = -1;

		// There should be a variant of strstr() that returns a non-constant pointer, but in Visual Studio it is always constant.
		char* parenOpen = (char*)strstr(arg, "(");
		// This message targets a wall rather than a whole sector.
		if (parenOpen)
		{
			*parenOpen = 0;
			parenOpen++;

			char* parenClose = (char*)strstr(arg, ")");
			// This should never be true and this doesn't seem to be hit at runtime.
			// I wonder if this was meant to be { char* parenClose = (char*)strstr(parenOpen, ")"); } - which would make more sense.
			// Or it could have been check *before* the location at ")" was set to 0 above.
			if (parenClose)
			{
				*parenClose = 0;
			}
			// Finally parse the integer and set the wall index.
			client->targetWall = strtol(parenOpen, &endPtr, 10);
		}
		client->targetSector = arg;
	}

	bool editor_parseElevatorCommand(s32 argCount, KEYWORD action, bool seqEnd, Editor_InfElevator* elev, s32& addon)
	{
		char* endPtr = nullptr;
		switch (action)
		{
			case KW_START:
			{
				elev->start = strtol(s_infArg0, &endPtr, 10);
				elev->overrideSet |= IEO_START;
			} break;
			case KW_STOP:
			{
				elev->stops.push_back({});
				Editor_InfStop* stop = &elev->stops.back();
				stop->overrideSet = ISO_NONE;

				// Calculate the stop value.
				if (s_infArg0[0] == '@')  // Relative Value
				{
					stop->relative = true;
					stop->value = strtof(&s_infArg0[1], &endPtr);
				}
				else if ((s_infArg0[0] >= '0' && s_infArg0[0] <= '9') || s_infArg0[0] == '-' || s_infArg0[0] == '.')  // Numeric Value
				{
					stop->relative = false;
					stop->value = strtof(s_infArg0, &endPtr);
				}
				else  // Value from named sector.
				{
					stop->relative = false;
					stop->value = 0.0f;
					stop->fromSectorFloor = s_infArg0;
				}

				if (argCount < 3)
				{
					return seqEnd;
				}

				stop->overrideSet |= ISO_DELAY;
				stop->delay = 0.0f;
				if ((s_infArg1[0] >= '0' && s_infArg1[0] <= '9') || s_infArg1[0] == '-' || s_infArg1[0] == '.')
				{
					stop->delayType = SDELAY_SECONDS;
					stop->delay = strtof(s_infArg1, &endPtr);
				}
				else if (strcasecmp(s_infArg1, "HOLD") == 0)
				{
					stop->delayType = SDELAY_HOLD;
				}
				else if (strcasecmp(s_infArg1, "TERMINATE") == 0)
				{
					stop->delayType = SDELAY_TERMINATE;
				}
				else if (strcasecmp(s_infArg1, "COMPLETE") == 0)
				{
					stop->delayType = SDELAY_COMPLETE;
				}
				else
				{
					// Note: this is actually a bug in the original game, but some mods rely on it so...
					stop->delayType = SDELAY_PREV_VALUE;
				}
				elev->overrideSet |= IEO_STOPS;
			} break;
			case KW_SPEED:
			{
				elev->speed = strtof(s_infArg0, &endPtr);
				elev->overrideSet |= IEO_SPEED;
			} break;
			case KW_MASTER:
			{
				elev->master = false;
				elev->overrideSet |= IEO_MASTER;
			} break;
			case KW_ANGLE:
			{
				elev->angle = strtof(s_infArg0, &endPtr) * PI / 180.0f;
				elev->overrideSet |= IEO_ANGLE;
			} break;
			case KW_ADJOIN:
			{
				s32 stopId = strtol(s_infArg0, &endPtr, 10);
				Editor_InfStop* stop = stopId >= 0 && stopId < (s32)elev->stops.size() ? &elev->stops[stopId] : nullptr;
				if (stop)
				{
					stop->adjoinCmd.push_back({});
					Editor_InfAdjoinCmd* adjoinCmd = &stop->adjoinCmd.back();

					adjoinCmd->sector0 = s_infArg1;
					adjoinCmd->sector1 = s_infArg3;
					adjoinCmd->wallIndex0 = strtol(s_infArg2, &endPtr, 10);
					adjoinCmd->wallIndex1 = strtol(s_infArg4, &endPtr, 10);
				}
				elev->overrideSet |= IEO_STOPS;
			} break;
			case KW_TEXTURE:
			{
				s32 stopId = strtol(s_infArg0, &endPtr, 10);
				Editor_InfStop* stop = stopId >= 0 && stopId < (s32)elev->stops.size() ? &elev->stops[stopId] : nullptr;
				if (stop)
				{
					stop->textureCmd.push_back({});
					Editor_InfTextureCmd* texCmd = &stop->textureCmd.back();

					texCmd->donorSector = s_infArg2;
					texCmd->fromCeiling = s_infArg1[0] == 'C' || s_infArg1[0] == 'c';
				}
				elev->overrideSet |= IEO_STOPS;
			} break;
			case KW_SLAVE:
			{
				elev->slaves.push_back({});
				Editor_InfSlave* slave = &elev->slaves.back();

				slave->name = s_infArg0;
				slave->angleOffset = 0.0f;
				if (argCount > 2)
				{
					slave->angleOffset = strtof(s_infArg1, &endPtr);
				}
				elev->overrideSet |= IEO_SLAVES;
			} break;
			case KW_MESSAGE:
			{
				s32 stopId = strtol(s_infArg0, &endPtr, 10);
				Editor_InfStop* stop = stopId >= 0 && stopId < (s32)elev->stops.size() ? &elev->stops[stopId] : nullptr;
				if (stop)
				{
					stop->msg.push_back({});
					Editor_InfMessage* msg = &stop->msg.back();
					parseTarget(msg, s_infArg1);
					msg->type = IMT_TRIGGER;
					msg->eventFlags = INF_EVENT_NONE;

					if (argCount >= 5)
					{
						msg->eventFlags = strtoul(s_infArg0, &endPtr, 10);
					}
					if (argCount > 3)
					{
						editor_parseMessage(&msg->type, &msg->arg[0], &msg->arg[1], &msg->eventFlags, s_infArg2, s_infArg3, s_infArg4, true);
					}
				}
				elev->overrideSet |= IEO_STOPS;
			} break;
			case KW_EVENT_MASK:
			{
				if (s_infArg0[0] == '*')
				{
					elev->eventMask = INF_EVENT_ANY;	// everything
				}
				else
				{
					elev->eventMask = strtoul(s_infArg0, &endPtr, 10);
				}
				elev->overrideSet |= IEO_EVENT_MASK;
			} break;
			case KW_ENTITY_MASK:
			case KW_OBJECT_MASK:
			{
				// Entity_mask and Object_mask are buggy for elevators...
				if (s_infArg0[0] == '*')
				{
					elev->eventMask = INF_ENTITY_ANY;
				}
				else
				{
					elev->eventMask = strtoul(s_infArg0, &endPtr, 10);
				}
				elev->overrideSet |= IEO_ENTITY_MASK;
			} break;
			case KW_CENTER:
			{
				const f32 centerX = strtof(s_infArg0, &endPtr);
				const f32 centerZ = strtof(s_infArg1, &endPtr);
				elev->center = { centerX, centerZ };
				elev->overrideSet |= IEO_CENTER;
			} break;
			case KW_KEY_COLON:
			{
				KEYWORD key = getKeywordIndex(s_infArg0);
				if (key == KW_RED)
				{
					elev->key[addon] = KEY_RED;
				}
				else if (key == KW_YELLOW)
				{
					elev->key[addon] = KEY_YELLOW;
				}
				else if (key == KW_BLUE)
				{
					elev->key[addon] = KEY_BLUE;
				}
				elev->overrideSet |= (addon > 0 ? IEO_KEY1 : IEO_KEY0);
			} break;
			// This entry is required for special cases, like IELEV_SP_DOOR_MID, where multiple elevators are created at once.
			// This way, we can modify the first created elevator as well as the last.
			// It allows allows us to modify previous classes... but this isn't recommended.
			case KW_ADDON:
			{
				addon = strtol(s_infArg0, &endPtr, 10);
			} break;
			case KW_FLAGS:
			{
				elev->flags = strtoul(s_infArg0, &endPtr, 10);
				elev->overrideSet |= IEO_FLAGS;
			} break;
			case KW_SOUND_COLON:
			{
				std::string soundName = s_infArg1;
				if (s_infArg1[0] >= '0' && s_infArg1[0] <= '9')
				{
					// Any numeric value means "no sound."
					soundName = "none";
				}

				// Determine which elevator sound to assign soundId to.
				s32 soundIdx = strtol(s_infArg0, &endPtr, 10) - 1;
				if (soundIdx >= 0 && soundIdx < 3)
				{
					elev->sounds[soundIdx] = soundName;
					elev->overrideSet |= (IEO_SOUND0 << soundIdx);
				}
			} break;
			case KW_PAGE:
			{
				s32 stopId = strtol(s_infArg0, &endPtr, 10);
				Editor_InfStop* stop = stopId >= 0 && stopId < (s32)elev->stops.size() ? &elev->stops[stopId] : nullptr;
				if (stop)
				{
					stop->page = s_infArg1;
					elev->overrideSet |= IEO_STOPS;
					stop->overrideSet |= ISO_PAGE;
				}
			} break;
			case KW_SEQEND:
			{
				seqEnd = true;
			} break;
		}

		return seqEnd;
	}

	bool parseElevator(TFE_Parser& parser, size_t& bufferPos, const char* itemName, Editor_InfItem* item)
	{
		EditorSector* itemSector = findSector(itemName);
		if (!itemSector)
		{
			parser.readLine(bufferPos);
			return false;
		}
						
		KEYWORD itemSubclass = getKeywordIndex(s_infArg1);
		// Special classes - these map to the 11 core elevator types but have special defaults and/or automatically setup stops.
		Editor_InfElevType type;
		if (itemSubclass <= KW_MORPH_MOVE2)
		{
			switch (itemSubclass)
			{
			case KW_BASIC:
				type = IET_BASIC;
				break;
			case KW_BASIC_AUTO:
				type = IET_BASIC_AUTO;
				break;
			case KW_INV:
				type = IET_INV;
				break;
			case KW_DOOR:
				type = IET_DOOR;
				break;
			case KW_DOOR_INV:
				type = IET_DOOR_INV;
				break;
			case KW_DOOR_MID:
				type = IET_DOOR_MID;
				break;
			case KW_MORPH_SPIN1:
				type = IET_MORPH_SPIN1;
				break;
			case KW_MORPH_SPIN2:
				type = IET_MORPH_SPIN2;
				break;
			case KW_MORPH_MOVE1:
				type = IET_MORPH_MOVE1;
				break;
			case KW_MORPH_MOVE2:
				type = IET_MORPH_MOVE2;
				break;
			default:
				// Invalid type.
				LE_WARNING("Unsupported INF elevator type '%s' (ignored by Dark Forces).", s_infArg1);
				parser.readLine(bufferPos);
				return false;
			};
		}
		// One of the core 11 types.
		else
		{
			switch (itemSubclass)
			{
			case KW_MOVE_CEILING:
				type = IET_MOVE_CEILING;
				break;
			case KW_MOVE_FLOOR:
				type = IET_MOVE_FLOOR;
				break;
			case KW_MOVE_FC:
				type = IET_MOVE_FC;
				break;
			case KW_MOVE_OFFSET:
				type = IET_MOVE_OFFSET;
				break;
			case KW_MOVE_WALL:
				type = IET_MOVE_WALL;
				break;
			case KW_ROTATE_WALL:
				type = IET_ROTATE_WALL;
				break;
			case KW_SCROLL_WALL:
				type = IET_SCROLL_WALL;
				break;
			case KW_SCROLL_FLOOR:
				type = IET_SCROLL_FLOOR;
				break;
			case KW_SCROLL_CEILING:
				type = IET_SCROLL_CEILING;
				break;
			case KW_CHANGE_LIGHT:
				type = IET_CHANGE_LIGHT;
				break;
			case KW_CHANGE_WALL_LIGHT:
				type = IET_CHANGE_WALL_LIGHT;
				break;
			default:
				// Invalid type.
				LE_WARNING("Unsupported INF elevator type '%s' (ignored by Dark Forces).", s_infArg1);
				parser.readLine(bufferPos);
				return false;
			}
		}
		Editor_InfElevator* elev = allocElev(item);
		elev->type = type;
		elev->overrideSet = IEO_NONE;
		
		s32 addon = 0;
		bool seqEnd = false;
		while (!seqEnd)
		{
			const char* line = parser.readLine(bufferPos);
			if (!line) { break; }
			// There is another class in this sequence, exit out.
			if (strstr(line, "CLASS")) { break; }

			char id[256];
			s32 argCount = sscanf(line, " %s %s %s %s %s %s %s", id, s_infArg0, s_infArg1, s_infArg2, s_infArg3, s_infArg4, s_infArgExtra);
			KEYWORD action = getKeywordIndex(id);
			if (action == KW_UNKNOWN)
			{
				LE_WARNING("Unknown elevator command - '%s'.", id);
			}
			seqEnd = editor_parseElevatorCommand(argCount, action, seqEnd, elev, addon);
		} // while (!seqEnd)

		return seqEnd;
	}

	bool parseSectorTrigger(TFE_Parser& parser, size_t& bufferPos, s32 argCount, const char* itemName, Editor_InfItem* item)
	{
		EditorSector* itemSector = findSector(itemName);
		if (!itemSector)
		{
			parser.readLine(bufferPos);
			return false;
		}

		Editor_InfTrigger* trigger = allocTrigger(item);
		trigger->overrideSet = ITO_NONE;
		trigger->type = ITRIGGER_SECTOR;

		bool seqEnd = false;
		char* endPtr = nullptr;
		while (!seqEnd)
		{
			const char* line = parser.readLine(bufferPos);
			if (!line) { break; }
			// There is another class in this sequence, exit out.
			if (strstr(line, "CLASS")) { break; }

			char id[256];
			s32 argCount = sscanf(line, " %s %s %s %s %s", id, s_infArg0, s_infArg1, s_infArg2, s_infArg3);
			KEYWORD itemId = getKeywordIndex(id);
			assert(itemId != KW_UNKNOWN);
			switch (itemId)
			{
				case KW_CLIENT:
				{
					Editor_InfClient client = {};
					parseTarget(&client, s_infArg0);
					if (argCount > 2)
					{
						client.eventMask = strtoul(s_infArg1, &endPtr, 10);
					}
					trigger->clients.push_back(client);
				} break;
				case KW_MASTER:
				{
					trigger->master = false;
					trigger->overrideSet |= ITO_MASTER;
				} break;
				case KW_TEXT:
				{
					if (s_infArg0[0] >= '0' && s_infArg0[0] <= '9')
					{
						trigger->textId = strtoul(s_infArg0, &endPtr, 10);
						trigger->overrideSet |= ITO_TEXT;
					}
				} break;
				case KW_MESSAGE:
				{
					trigger->overrideSet |= ITO_MSG;
					editor_parseMessage(&trigger->cmd, &trigger->arg[0], &trigger->arg[1], nullptr, s_infArg0, s_infArg1, s_infArg2, false);
				} break;
				case KW_EVENT_MASK:
				{
					trigger->overrideSet |= ITO_EVENT_MASK;
					if (s_infArg0[0] == '*')
					{
						trigger->eventMask = INF_EVENT_ANY;
					}
					else
					{
						trigger->eventMask = strtoul(s_infArg0, &endPtr, 10);
					}
				} break;
				case KW_ENTITY_MASK:
				case KW_OBJECT_MASK:
				{
					trigger->overrideSet |= ITO_ENTITY_MASK;
					if (s_infArg0[0] == '*')
					{
						trigger->entityMask = INF_ENTITY_ANY;
					}
					else
					{
						trigger->entityMask = strtoul(s_infArg0, &endPtr, 10);
					}
				} break;
				case KW_EVENT:
				{
					trigger->overrideSet |= ITO_EVENT;
					trigger->event = strtoul(s_infArg0, &endPtr, 10);
				} break;
				case KW_SEQEND:
				{
					// The sequence for this item has completed (no more classes).
					seqEnd = true;
				} break;
			}
		} // while (!seqEnd)
		return seqEnd;
	}

	bool parseLineTrigger(TFE_Parser& parser, size_t& bufferPos, s32 argCount, const char* itemName, s32 wallNum, Editor_InfItem* item)
	{
		EditorSector* itemSector = findSector(itemName);
		if (!itemSector)
		{
			parser.readLine(bufferPos);
			return false;
		}
		if (wallNum < 0 || wallNum >= itemSector->walls.size())
		{
			parser.readLine(bufferPos);
			return false;
		}

		Editor_InfTrigger* trigger = allocTrigger(item);
		trigger->overrideSet = ITO_NONE;

		KEYWORD typeId = getKeywordIndex(s_infArg0);
		assert(typeId == KW_TRIGGER);

		trigger->type = ITRIGGER_WALL;
		if (argCount > 2)
		{
			KEYWORD subTypeId = getKeywordIndex(s_infArg1);
			switch (subTypeId)
			{
				case KW_SWITCH1:
				{
					trigger->type = ITRIGGER_SWITCH1;
				} break;
				case KW_TOGGLE:
				{
					trigger->type = ITRIGGER_TOGGLE;
				} break;
				case KW_SINGLE:
				{
					trigger->type = ITRIGGER_SINGLE;
				} break;
				case KW_STANDARD:
				default:
				{
					trigger->type = ITRIGGER_WALL;
				}
			}
		}

		// Trigger parameters
		const char* line;
		char* endPtr = nullptr;
		bool seqEnd = false;
		while (!seqEnd)
		{
			line = parser.readLine(bufferPos);
			if (!line || strstr(line, "CLASS"))
			{
				break;
			}

			char id[256];
			argCount = sscanf(line, " %s %s %s %s %s", id, s_infArg0, s_infArg1, s_infArg2, s_infArg3);
			KEYWORD itemId = getKeywordIndex(id);
			if (itemId == KW_UNKNOWN)
			{
				LE_WARNING("Unknown trigger parameter - '%s'.", id);
			}
			switch (itemId)
			{
				case KW_SEQEND:
				{
					seqEnd = true;
				} break;
				case KW_CLIENT:
				{
					Editor_InfClient client = {};
					parseTarget(&client, s_infArg0);
					if (argCount > 2)
					{
						client.eventMask = strtoul(s_infArg1, &endPtr, 10);
					}
					trigger->clients.push_back(client);
				} break;
				case KW_EVENT_MASK:
				{
					trigger->overrideSet |= ITO_EVENT_MASK;
					if (s_infArg0[0] == '*')
					{
						trigger->eventMask = INF_EVENT_ANY;
					}
					else
					{
						trigger->eventMask = strtoul(s_infArg0, &endPtr, 10);
					}
				} break;
				case KW_MASTER:
				{
					trigger->master = false;
					trigger->overrideSet |= ITO_MASTER;
				} break;
				case KW_TEXT:
				{
					if (s_infArg0[0] >= '0' && s_infArg0[0] <= '9')
					{
						trigger->textId = strtoul(s_infArg0, &endPtr, 10);
						trigger->overrideSet |= ITO_TEXT;
					}
				} break;
				case KW_ENTITY_MASK:
				case KW_OBJECT_MASK:
				{
					trigger->overrideSet |= ITO_ENTITY_MASK;
					if (s_infArg0[0] == '*')
					{
						trigger->entityMask = INF_ENTITY_ANY;
					}
					else
					{
						trigger->entityMask = strtoul(s_infArg0, &endPtr, 10);
					}
				} break;
				case KW_EVENT:
				{
					trigger->overrideSet |= ITO_EVENT;
					trigger->event = strtoul(s_infArg0, &endPtr, 10);
				} break;
				case KW_SOUND_COLON:
				{
					// Not ascii
					if (s_infArg0[0] < '0' || s_infArg0[0] > '9')
					{
						trigger->overrideSet |= ITO_SOUND;
						trigger->sound = s_infArg0;
					}
				} break;
				case KW_MESSAGE:
				{
					trigger->overrideSet |= ITO_MSG;
					editor_parseMessage(&trigger->cmd, &trigger->arg[0], &trigger->arg[1], nullptr, s_infArg0, s_infArg1, s_infArg2, false);
				} break;
			}  // switch (itemId)
		}  // while (!seqEnd)

		return seqEnd;
	}

	bool parseTeleport(TFE_Parser& parser, size_t& bufferPos, const char* itemName, Editor_InfItem* item)
	{
		EditorSector* itemSector = findSector(itemName);
		if (!itemSector)
		{
			parser.readLine(bufferPos);
			return false;
		}

		Editor_InfTeleporter* teleporter = allocTeleporter(item);
		KEYWORD itemSubclass = getKeywordIndex(s_infArg1);
		// Special classes - these map to the 11 core elevator types but have special defaults and/or automatically setup stops.
		if (itemSubclass == KW_BASIC)
		{
			teleporter->type = TELEPORT_BASIC;
		}
		else if (itemSubclass == KW_CHUTE)
		{
			teleporter->type = TELEPORT_CHUTE;
		}
		else
		{
			// Invalid type.
			return false;
		}

		// Loop through trigger parameters.
		const char* line;
		bool seqEnd = false;
		while (!seqEnd)
		{
			line = parser.readLine(bufferPos);
			// There is another class in this sequence, so we are done with the trigger.
			if (!line || strstr(line, "CLASS"))
			{
				break;
			}

			char name[256];
			sscanf(line, " %s %s %s %s %s", name, s_infArg0, s_infArg1, s_infArg2, s_infArg3);
			KEYWORD kw = getKeywordIndex(name);

			if (kw == KW_TARGET)  // Target:
			{
				teleporter->target = s_infArg0;
			}
			else if (kw == KW_MOVE)  // Move:
			{
				char* endPtr;
				teleporter->dstPos.x = strtof(s_infArg0, &endPtr);
				teleporter->dstPos.y = strtof(s_infArg1, &endPtr);
				teleporter->dstPos.z = strtof(s_infArg2, &endPtr);
				teleporter->dstAngle = strtof(s_infArg3, &endPtr);
			}
			else if (kw == KW_SEQEND)
			{
				seqEnd = true;
				break;
			}
		}
		return true;
	}

	void mergeExistingInfItem()
	{
		Editor_InfItem* last = &s_levelInf.item.back();
		Editor_InfItem* item = s_levelInf.item.data();
		for (s32 i = 0; i < (s32)s_levelInf.item.size() - 1; i++, item++) // -1: dont match self
		{
			if (item->name == last->name && item->wallNum == last->wallNum)
			{
				LE_WARNING("Merging INF item - Name: %s - Wall number: %d", last->name.c_str(), last->wallNum);
				for (size_t j = 0; j < last->classData.size(); j++)
				{
					s_levelInf.item[i].classData.push_back(s_levelInf.item.back().classData[j]);
				}

				s_levelInf.item.pop_back();
				return;
			}
		}
	}

	bool loadLevelInfFromAsset(const Asset* asset)
	{
		char infFile[TFE_MAX_PATH];
		s_fileData.clear();
		if (asset->archive)
		{
			FileUtil::replaceExtension(asset->name.c_str(), "INF", infFile);

			if (asset->archive->openFile(infFile))
			{
				const size_t len = asset->archive->getFileLength();
				s_fileData.resize(len);
				asset->archive->readFile(s_fileData.data(), len);
				asset->archive->closeFile();
			}
		}
		else
		{
			FileUtil::replaceExtension(asset->filePath.c_str(), "INF", infFile);

			FileStream file;
			if (file.open(infFile, Stream::MODE_READ))
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
		parser.enableBlockComments();
		parser.addCommentString("//");
		parser.convertToUpperCase(true);

		const char* line;
		line = parser.readLine(bufferPos);

		// Keep looping until the version is found.
		while (strncasecmp(line, "INF", 3) != 0 && line)
		{
			line = parser.readLine(bufferPos);
		}
		if (!line)
		{
			TFE_System::logWrite(LOG_ERROR, "level_loadINF", "Cannot find INF version.");
			return false;
		}

		f32 version;
		if (sscanf(line, "INF %f", &version) != 1)
		{
			TFE_System::logWrite(LOG_ERROR, "level_loadINF", "Cannot read INF version.");
			return false;
		}
		if (version != 1.0f)
		{
			TFE_System::logWrite(LOG_ERROR, "level_loadINF", "Incorrect INF version %f, should be 1.0.", version);
			return false;
		}

		// Keep looping until ITEMS is found.
		// Note - the editor always ignores the INF limit.
		// TODO: Produce a warning if using the vanilla feature-set.
		s32 itemCount = 0;
		while (1)
		{
			line = parser.readLine(bufferPos);
			if (!line)
			{
				LE_ERROR("Cannot find ITEMS in INF: '%s'.", infFile);
				return false;
			}

			if (sscanf(line, "ITEMS %d", &itemCount) == 1)
			{
				break;
			}
		}
		// Warn about vanilla compatibility.
		if (itemCount > 512)
		{
			LE_WARNING("Too many INF items for vanilla compatibility %d / 512.\nExtra items will be ignored if loading in DOS.", itemCount);
		}

		// Then loop through all of the items and parse their classes.
		s32 wallNum = 0;
		for (s32 i = 0; i < itemCount; i++)
		{
			line = parser.readLine(bufferPos);
			if (!line)
			{
				LE_WARNING("Hit the end of INF '%s' before parsing all items: %d/%d", infFile, i, itemCount);
				return true;
			}

			char item[256], name[256];
			while (sscanf(line, " ITEM: %s NAME: %s NUM: %d", item, name, &wallNum) < 1)
			{
				line = parser.readLine(bufferPos);
				if (!line)
				{
					LE_WARNING("Hit the end of INF '%s' before parsing all items: %d/%d", infFile, i, itemCount);
					return true;
				}
				continue;
			}

			s_levelInf.item.push_back({});
			Editor_InfItem* infItem = &s_levelInf.item.back();
			infItem->name = name;
			infItem->wallNum = wallNum;

			KEYWORD itemType = getKeywordIndex(item);
			switch (itemType)
			{
				case KW_LEVEL:
				{
					infItem->wallNum = -1;

					line = parser.readLine(bufferPos);
					if (line && strstr(line, "SEQ"))
					{
						while ((line = parser.readLine(bufferPos)) != nullptr)
						{
							char itemName[256];
							s32 argCount = sscanf(line, " %s %s %s %s %s %s %s", itemName, s_infArg0, s_infArg1, s_infArg2, s_infArg3, s_infArgExtra, s_infArgExtra);
							KEYWORD levelItem = getKeywordIndex(itemName);
							switch (levelItem)
							{
								case KW_SEQEND:
									break;
								case KW_AMB_SOUND:
								{
									//level_addSound(s_infArg0, u32(atof(s_infArg1) * 145.65f), atoi(s_infArg2));
									continue;
								}
							}
							break;
						}
					}
				} break;
				case KW_SECTOR:
				{
					infItem->wallNum = -1;

					line = parser.readLine(bufferPos);
					if (!line || !strstr(line, "SEQ"))
					{
						continue;
					}

					line = parser.readLine(bufferPos);
					// Loop until seqend since an INF item may have multiple classes.
					while (1)
					{
						if (!line || !strstr(line, "CLASS") || strstr(line, "SEQEND"))
						{
							break;
						}

						char id[256];
						s32 argCount = sscanf(line, " %s %s %s %s %s %s %s", id, s_infArg0, s_infArg1, s_infArg2, s_infArg3, s_infArg4, s_infArgExtra);
						KEYWORD itemClass = getKeywordIndex(s_infArg0);
						assert(itemClass != KW_UNKNOWN);

						if (itemClass == KW_ELEVATOR)
						{
							if (parseElevator(parser, bufferPos, name, infItem))
							{
								break;
							}
						}
						else if (itemClass == KW_TRIGGER)
						{
							if (parseSectorTrigger(parser, bufferPos, argCount, name, infItem))
							{
								break;
							}
						}
						else if (itemClass == KW_TELEPORTER)
						{
							if (parseTeleport(parser, bufferPos, name, infItem))
							{
								break;
							}
						}
						else
						{
							// Invalid item class.
							line = parser.readLine(bufferPos);
						}
					}
				} break;
				case KW_LINE:
				{
					line = parser.readLine(bufferPos);
					if (!line || !strstr(line, "SEQ"))
					{
						continue;
					}

					line = parser.readLine(bufferPos);
					// Loop until seqend since an INF item may have multiple classes.
					while (1)
					{
						if (!line || !strstr(line, "CLASS"))
						{
							break;
						}

						char id[256];
						s32 argCount = sscanf(line, " %s %s %s %s %s", id, s_infArg0, s_infArg1, s_infArg2, s_infArg3);
						if (parseLineTrigger(parser, bufferPos, argCount, name, wallNum, infItem))
						{
							break;
						}
					}  // while (!seqEnd) - outer (Line Classes).
				} break;
			}
			
			// Fix up sibling classes split over multiple seq blocks if we can
			mergeExistingInfItem();
		}

		return true;
	}

	Editor_InfItem* editor_getInfItem(const char* sectorName, s32 wallIndex)
	{
		const s32 itemCount = (s32)s_levelInf.item.size();
		Editor_InfItem* item = s_levelInf.item.data();
		for (s32 i = 0; i < itemCount; i++, item++)
		{
			if (strcasecmp(sectorName, item->name.c_str()) == 0 && wallIndex == item->wallNum)
			{
				return item;
				break;
			}
		}
		return nullptr;
	}
		
	void editor_infEditBegin(const char* sectorName, s32 wallIndex)
	{
		s_infEditor = {};
		if (!sectorName || strlen(sectorName) < 1)
		{
			return;
		}
		s_infEditor.itemWallIndex = wallIndex;

		const s32 count = (s32)s_level.sectors.size();
		EditorSector* sector = s_level.sectors.data();
		for (s32 i = 0; i < count; i++, sector++)
		{
			if (sector->name.empty()) { continue; }
			if (strcasecmp(sectorName, sector->name.c_str()) == 0)
			{
				s_infEditor.sector = sector;
				break;
			}
		}

		const s32 itemCount = (s32)s_levelInf.item.size();
		Editor_InfItem* item = s_levelInf.item.data();
		for (s32 i = 0; i < itemCount; i++, item++)
		{
			if (strcasecmp(sectorName, item->name.c_str()) == 0 && wallIndex == item->wallNum)
			{
				s_infEditor.item = item;
				break;
			}
		}
	}

	void editor_infEditEnd()
	{
		modifySectorGeometry();
	}

	const char* c_infElevFlagNames[] =
	{
		"Move Floor",         // INF_EFLAG_MOVE_FLOOR = FLAG_BIT(0),	// Move on floor.
		"Move Second Height", //INF_EFLAG_MOVE_SECHT = FLAG_BIT(1),	// Move on second height.
		"Move Ceiling",       //INF_EFLAG_MOVE_CEIL = FLAG_BIT(2),	// Move on ceiling (head has to be close enough?)
	};

	const char* c_infClassName[]=
	{
		"Elevator",
		"Trigger",
		"Teleporter"
	};

	const char* c_infElevVarName[] =
	{
		"Start",		//IEV_START
		"Speed",		//IEV_SPEED
		"Master",		//IEV_MASTER
		"Angle",		//IEV_ANGLE
		"Flags",		//IEV_FLAGS
		"Key",			//IEV_KEY0
		"Key1",	        //IEV_KEY1
		"Center",		//IEV_CENTER
		"Sound 1",		//IEV_SOUND0
		"Sound 2",		//IEV_SOUND1
		"Sound 3",		//IEV_SOUND2
		"Event_Mask",	//IEV_EVENT_MASK
		"Entity_Mask",	//IEV_ENTITY_MASK
	};

	const char* c_infTriggerVarName[] =
	{
		"Sound",
		"Master",
		"Text",
		"Event_Mask",
		"Entity_Mask",
		"Event",
		"Message",
	};

	const char* c_infKeyNames[] =
	{
		"Red",
		"Yellow",
		"Blue"
	};

	const char* c_infEventMaskNames[] =
	{
		"Cross Line Front",	//INF_EVENT_CROSS_LINE_FRONT
		"Cross Line Back",	//INF_EVENT_CROSS_LINE_BACK
		"Enter Sector",		//INF_EVENT_ENTER_SECTOR
		"Leave Sector",		//INF_EVENT_LEAVE_SECTOR
		"Nudge Front",		//INF_EVENT_NUDGE_FRONT
		"Nudge Back",		//INF_EVENT_NUDGE_BACK
		"Explosion",		//INF_EVENT_EXPLOSION
		"Unused",			//INF_EVENT_UNUSED1
		"Shoot Line",		//INF_EVENT_SHOOT_LINE
		"Land",				//INF_EVENT_LAND
	};

	const char* c_infEntityMaskNames[] =
	{
		"Enemy",     // INF_ENTITY_ENEMY,
		"Weapon",    // INF_ENTITY_WEAPON,
		"Smart Obj", // INF_ENTITY_SMART_OBJ,
		"Player",    // INF_ENTITY_PLAYER,
	};

	const u32 c_infEntityMaskFlags[] =
	{
		u32(INF_ENTITY_ENEMY),
		u32(INF_ENTITY_WEAPON),
		u32(INF_ENTITY_SMART_OBJ),
		u32(INF_ENTITY_PLAYER),
	};

	const char* c_infStopDelayTypeName[] =
	{
		"Seconds",		// SDELAY_SECONDS = 0,
		"Hold",			// SDELAY_HOLD,
		"Terminate",	// SDELAY_TERMINATE,
		"Complete",		// SDELAY_COMPLETE,
		"Prev (Buggy)", // SDELAY_PREV_VALUE,
	};

	const char* c_editorInfMessageTypeName[] =
	{
		"Next_Stop",	// IMT_NEXT_STOP,
		"Prev_Stop",	// IMT_PREV_STOP,
		"Goto_Stop",	// IMT_GOTO_STOP,
		"Master_On",	// IMT_MASTER_ON,
		"Master_Off",	// IMT_MASTER_OFF,
		"Set_Bits",		// IMT_SET_BITS,
		"Clear_Bits",	// IMT_CLEAR_BITS,
		"Complete",		// IMT_COMPLETE,
		"Lights",		// IMT_LIGHTS,
		"M_Trigger",	// IMT_TRIGGER,
		"Done",			// IMT_DONE,
		"Wakeup",		// IMT_WAKEUP,
		"Camera",       // IMT_CAMERA,
	};
		
	const ImVec4 colorKeywordOuterSel = { 0.453f, 0.918f, 1.00f, 1.0f };
	const ImVec4 colorKeywordOuter = { 0.302f, 0.612f, 0.84f, 1.0f };
	const ImVec4 colorKeywordInner = { 0.306f, 0.788f, 0.69f, 1.0f };
	const ImVec4 colorKeywordArg = { 0.306f, 0.788f*0.5f, 0.69f, 1.0f };

	const ImVec4 colorInnerHeaderBase = { 0.98f, 0.49f, 0.26f, 1.0f };
	const ImVec4 colorInnerHeader = { colorInnerHeaderBase.x, colorInnerHeaderBase.y, colorInnerHeaderBase.z, 0.31f };
	const ImVec4 colorInnerHeaderActive = { colorInnerHeaderBase.x, colorInnerHeaderBase.y, colorInnerHeaderBase.z, 0.80f };
	const ImVec4 colorInnerHeaderHovered = { colorInnerHeaderBase.x, colorInnerHeaderBase.y, colorInnerHeaderBase.z, 0.60f };

	u32 countBits(u32 bits)
	{
		u32 count = 0;
		while (bits)
		{
			if (bits & 1) { count++; }
			bits >>= 1;
		}
		return count;
	}

	void editor_infSelectEditClass(s32 id)
	{
		if (id != s_infEditor.curClassIndex)
		{
			s_infEditor.curPropIndex = -1;
			s_infEditor.curContentIndex = -1;
			s_infEditor.curStopCmdIndex = -1;
			s_infEditor.showAnim = false;
			s_infEditor.playAnim = false;
			s_infEditor.time = 0.0f;
		}
		s_infEditor.curClassIndex = id;
	}

	void editor_infPropertySelectable(Editor_InfElevatorVar var, s32 classIndex)
	{
		bool sel = classIndex == s_infEditor.curClassIndex ? s_infEditor.curPropIndex == var : false;
		ImGui::PushStyleColor(ImGuiCol_Text, colorKeywordInner);

		char buffer[256];
		sprintf(buffer, "%s:", c_infElevVarName[var]);
		if (ImGui::Selectable(buffer, sel, 0, { 100.0f, 0.0f }))
		{
			editor_infSelectEditClass(classIndex);
			s_infEditor.curPropIndex = sel ? -1 : var;
		}
		ImGui::PopStyleColor();
		ImGui::SameLine(0.0f, 8.0f);
	}

	bool editor_infPropertySelectable(Editor_InfTriggerVar var, s32 classIndex)
	{
		bool sel = classIndex == s_infEditor.curClassIndex ? s_infEditor.curPropIndex == var : false;
		ImGui::PushStyleColor(ImGuiCol_Text, colorKeywordInner);

		char buffer[256];
		sprintf(buffer, "%s:", c_infTriggerVarName[var]);
		if (ImGui::Selectable(buffer, sel, 0, { 100.0f, 0.0f }))
		{
			editor_infSelectEditClass(classIndex);
			s_infEditor.curPropIndex = sel ? -1 : var;
		}
		ImGui::PopStyleColor();
		ImGui::SameLine(0.0f, 8.0f);

		return sel;
	}

	f32 computeSetOrClearBitsHeight(bool isWall, const u32* arg)
	{
		size_t rowCount = 0;
		if (isWall)
		{
			if (arg[0] <= 1) { rowCount = (TFE_ARRAYSIZE(c_infWallFlags1Names) + 3) >> 2; }
			else if (arg[0] == 3) { rowCount = (TFE_ARRAYSIZE(c_infWallFlags3Names) + 3) >> 2; }
		}
		else if (arg[0] <= 1)
		{
			rowCount = (TFE_ARRAYSIZE(c_infSectorFlag1Names) + 3) >> 2;
		}
		return 26.0f * f32(rowCount);
	}

	f32 computeChildHeight(const Editor_InfClass* data, s32 contentSel, bool curClass, f32* propHeight, f32* contentHeight)
	{
		f32 height = 600.0f;
		const f32 elemHeight = 26.0f;
		switch (data->classId)
		{
			case IIC_ELEVATOR:
			{
				const Editor_InfElevator* elev = getElevFromClassData(data);
				assert(elev);

				*propHeight = elemHeight * countBits(elev->overrideSet & IEO_VAR_MASK) + 16;
				// Expand if flags are selected.
				if (curClass && (s_infEditor.curPropIndex == IEV_FLAGS || s_infEditor.curPropIndex == IEV_ENTITY_MASK)) { *propHeight += elemHeight; }
				else if (curClass && s_infEditor.curPropIndex == IEV_EVENT_MASK) { *propHeight += elemHeight * 3.0f; }

				const s32 stopCount = (s32)elev->stops.size();
				const s32 slaveCount = (s32)elev->slaves.size();
				*contentHeight = elemHeight * f32(stopCount) + 16;
				// Now count the number of stop messages, etc.
				const Editor_InfStop* stop = elev->stops.data();
				for (s32 s = 0; s < stopCount; s++, stop++)
				{
					*contentHeight += elemHeight * f32(stop->msg.size());
					*contentHeight += elemHeight * f32(stop->scriptCall.size());
					*contentHeight += elemHeight * f32(stop->adjoinCmd.size());
					*contentHeight += elemHeight * f32(stop->textureCmd.size());
					*contentHeight += (stop->overrideSet & ISO_PAGE) ? elemHeight : 0.0f;
				}
				*contentHeight += elemHeight * f32(slaveCount);
				if (contentSel >= 0 && contentSel < (s32)elev->stops.size() && curClass)
				{
					// Room for buttons.
					*contentHeight += elemHeight;

					// Which stop command is selected if any?
					if (s_infEditor.curStopCmdIndex >= 0 && s_infEditor.curStopCmdIndex < elev->stops[contentSel].msg.size())
					{
						// Handle selected messages with set_bits or clear_bits set.
						const Editor_InfMessage* msg = &elev->stops[contentSel].msg[s_infEditor.curStopCmdIndex];
						if (msg->type == IMT_SET_BITS || msg->type == IMT_CLEAR_BITS)
						{
							*contentHeight += computeSetOrClearBitsHeight(msg->targetWall >= 0, msg->arg);
						}
					}
				}

				height = 140.0f + (*propHeight) + (*contentHeight);
				// Add space for the slider.
				height += elemHeight;
			} break;
			case IIC_TRIGGER:
			{
				const Editor_InfTrigger* trigger = getTriggerFromClassData(data);
				assert(trigger);

				*propHeight = elemHeight * countBits(trigger->overrideSet & ITO_VAR_MASK) + 16;
				// Expand if flags are selected.
				if (curClass && s_infEditor.curPropIndex == ITV_ENTITY_MASK) { *propHeight += elemHeight; }
				else if (curClass && (s_infEditor.curPropIndex == ITV_EVENT_MASK || s_infEditor.curPropIndex == ITV_EVENT)) { *propHeight += elemHeight * 3.0f; }

				const s32 clientCount = (s32)trigger->clients.size();
				*contentHeight = 16.0f + elemHeight * clientCount;

				if (clientCount)
				{
					if (curClass && s_infEditor.curPropIndex == ITV_MSG && (trigger->cmd == IMT_SET_BITS || trigger->cmd == IMT_CLEAR_BITS))
					{
						*propHeight += computeSetOrClearBitsHeight(trigger->clients[0].targetWall >= 0, trigger->arg);
					}
				}

				height = 140.0f + (*propHeight) + (*contentHeight);
			} break;
			case IIC_TELEPORTER:
			{
				const Editor_InfTeleporter* teleporter = getTeleporterFromClassData(data);
				assert(teleporter);

				height = 16.0f + elemHeight * (2.0f + (teleporter->type == TELEPORT_BASIC ? 1.0f : 0.0f));
			} break;
		}
		return height;
	}

	void editor_infSelectElevType(Editor_InfElevator* elev)
	{
		ImGui::SetNextItemWidth(180.0f);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
		if (ImGui::BeginCombo(editor_getUniqueLabel(""), c_infElevTypeName[elev->type]))
		{
			s32 count = (s32)TFE_ARRAYSIZE(c_infElevTypeName);
			for (s32 t = 0; t < count; t++)
			{
				if (ImGui::Selectable(c_infElevTypeName[t], t == s_infEditor.comboElevTypeIndex))
				{
					elev->type = Editor_InfElevType(t);
				}
				//setTooltip(c_infClassName[i].tooltip.c_str());
			}
			ImGui::EndCombo();
		}
	}
		
	void editor_infSelectTriggerType(Editor_InfTrigger* trigger)
	{
		if (trigger->type == ITRIGGER_SECTOR) { return; }

		ImGui::SetNextItemWidth(180.0f);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
		s32 index = trigger->type == ITRIGGER_WALL ? trigger->type : trigger->type - 1;
		if (ImGui::BeginCombo(editor_getUniqueLabel(""), c_selectableTriggerTypes[index]))
		{
			s32 count = (s32)TFE_ARRAYSIZE(c_selectableTriggerTypes);
			for (s32 t = 0; t < count; t++)
			{
				if (ImGui::Selectable(c_selectableTriggerTypes[t], t == s_infEditor.comboElevTypeIndex))
				{
					trigger->type = t == 0 ? TriggerType(t) : TriggerType(t+1);
				}
				//setTooltip(c_infClassName[i].tooltip.c_str());
			}
			ImGui::EndCombo();
		}
	}

	void editor_infSelectTelelporterType(Editor_InfTeleporter* teleporter)
	{
		ImGui::SetNextItemWidth(180.0f);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
		if (ImGui::BeginCombo(editor_getUniqueLabel(""), c_infTeleporterTypeName[teleporter->type]))
		{
			s32 count = (s32)TFE_ARRAYSIZE(c_infTeleporterTypeName);
			for (s32 t = 0; t < count; t++)
			{
				if (ImGui::Selectable(c_infTeleporterTypeName[t], t == s_infEditor.comboElevTypeIndex))
				{
					teleporter->type = TeleportType(t);
				}
				//setTooltip(c_infClassName[i].tooltip.c_str());
			}
			ImGui::EndCombo();
		}
	}
				
	void editor_infEditElevProperties(Editor_InfElevator* elev, f32 propHeight, s32 itemClassIndex, const f32* btnTint)
	{
		const TFE_Editor::AssetList& soundList = AssetBrowser::getAssetList(TYPE_SOUND);
		const s32 soundCount = (s32)soundList.size();
		const TFE_Editor::Asset* soundAssets = soundList.data();

		ImGui::Text("%s", "Properties");
		if (elev->overrideSet & IEO_VAR_MASK)
		{
			ImGui::BeginChild(editor_getUniqueLabel(""), { 0.0f, propHeight }, ImGuiChildFlags_Border);
			{
				const u32 overrides = elev->overrideSet;
				if (overrides & IEO_START)
				{
					editor_infPropertySelectable(IEV_START, itemClassIndex);

					ImGui::SetNextItemWidth(128.0f);
					ImGui::InputInt(editor_getUniqueLabel(""), &elev->start);
				}
				if (overrides & IEO_SPEED)
				{
					editor_infPropertySelectable(IEV_SPEED, itemClassIndex);

					ImGui::SetNextItemWidth(128.0f);
					ImGui::InputFloat(editor_getUniqueLabel(""), &elev->speed, 0.1f, 1.0f, "%.3f", ImGuiInputTextFlags_CharsDecimal);
				}
				if (overrides & IEO_MASTER)
				{
					editor_infPropertySelectable(IEV_MASTER, itemClassIndex);
					ImGui::Checkbox(editor_getUniqueLabel(""), &elev->master);
				}
				if (overrides & IEO_ANGLE)
				{
					editor_infPropertySelectable(IEV_ANGLE, itemClassIndex);

					ImGui::SetNextItemWidth(128.0f);
					ImGui::SliderAngle(editor_getUniqueLabel(""), &elev->angle);
					ImGui::SameLine(0.0f, 8.0f);
					ImGui::SetNextItemWidth(128.0f);
					f32 angle = elev->angle * 180.0f / PI;
					if (ImGui::InputFloat(editor_getUniqueLabel(""), &angle, 0.1f, 1.0f, "%.3f", ImGuiInputTextFlags_CharsDecimal))
					{
						elev->angle = angle * PI / 180.0f;
					}

					ImGui::SameLine(0.0f, 8.0f);
					if (iconButtonInline(ICON_SELECT, "Select a wall aligned with the direction of movement from the viewport.", btnTint, true))
					{
						editor_selectViewportFeature((Editor_InfClass*)elev, SELECTMODE_WALL, SELTYPE_ELEV_ANGLE);
					}
				}
				if (overrides & IEO_FLAGS)
				{
					editor_infPropertySelectable(IEV_FLAGS, itemClassIndex);

					ImGui::SetNextItemWidth(128.0f);
					ImGui::InputUInt(editor_getUniqueLabel(""), &elev->flags);
					if (s_infEditor.curPropIndex == IEV_FLAGS)
					{
						for (s32 i = 0; i < TFE_ARRAYSIZE(c_infElevFlagNames); i++)
						{
							if (i != 0) { ImGui::SameLine(0.0f, 8.0f); }
							ImGui::CheckboxFlags(editor_getUniqueLabel(c_infElevFlagNames[i]), &elev->flags, 1 << i);
						}
					}
				}
				if (overrides & IEO_KEY0)
				{
					editor_infPropertySelectable(IEV_KEY0, itemClassIndex);

					s32 keyIndex = elev->key[0] - KeyItem::KEY_RED;
					ImGui::SetNextItemWidth(128.0f);
					if (ImGui::Combo(editor_getUniqueLabel(""), &keyIndex, c_infKeyNames, TFE_ARRAYSIZE(c_infKeyNames)))
					{
						elev->key[0] = KeyItem(keyIndex + KeyItem::KEY_RED);
					}

				}
				if (overrides & IEO_KEY1)
				{
					editor_infPropertySelectable(IEV_KEY1, itemClassIndex);

					s32 keyIndex = elev->key[1] - KeyItem::KEY_RED;
					ImGui::SetNextItemWidth(128.0f);
					if (ImGui::Combo(editor_getUniqueLabel(""), &keyIndex, c_infKeyNames, TFE_ARRAYSIZE(c_infKeyNames)))
					{
						elev->key[1] = KeyItem(keyIndex + KeyItem::KEY_RED);
					}
				}
				if (overrides & IEO_CENTER)
				{
					editor_infPropertySelectable(IEV_CENTER, itemClassIndex);

					ImGui::SetNextItemWidth(160.0f);
					ImGui::InputFloat2(editor_getUniqueLabel(""), elev->center.m, "%.3f", ImGuiInputTextFlags_CharsDecimal);

					ImGui::SameLine(0.0f, 8.0f);
					if (iconButtonInline(ICON_SELECT, "Select position in viewport.", btnTint, true))
					{
						editor_selectViewportFeature((Editor_InfClass*)elev, SELECTMODE_POSITION, SELTYPE_ELEV_POSITION);
					}
					ImGui::SameLine(0.0f, 8.0f);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
					if (iconButtonInline(ICON_BOX_CENTER, "Select sector in viewport and use its center.", btnTint, true))
					{
						editor_selectViewportFeature((Editor_InfClass*)elev, SELECTMODE_SECTOR, SELTYPE_ELEV_SECTOR_CENTER);
					}
					ImGui::SameLine(0.0f, 8.0f);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
					if (iconButtonInline(ICON_CIRCLE_PLUS, "Calculate the center from current sector and slaves.", btnTint, true))
					{
						Vec2f bounds[2];
						bounds[0] = { s_infEditor.sector->bounds[0].x, s_infEditor.sector->bounds[0].z };
						bounds[1] = { s_infEditor.sector->bounds[1].x, s_infEditor.sector->bounds[1].z };
						const s32 slaveCount = (s32)elev->slaves.size();
						const Editor_InfSlave* slave = elev->slaves.data();
						for (s32 s = 0; s < slaveCount; s++, slave++)
						{
							const s32 id = findSectorByName(slave->name.c_str());
							if (id < 0) { continue; }
							const EditorSector* slaveSector = &s_level.sectors[id];
							bounds[0].x = min(bounds[0].x, slaveSector->bounds[0].x);
							bounds[0].z = min(bounds[0].z, slaveSector->bounds[0].z);
							bounds[1].x = max(bounds[1].x, slaveSector->bounds[1].x);
							bounds[1].z = max(bounds[1].z, slaveSector->bounds[1].z);
						}
						elev->center.x = (bounds[0].x + bounds[1].x) * 0.5f;
						elev->center.z = (bounds[0].z + bounds[1].z) * 0.5f;
					}
				}
				if (overrides & IEO_SOUND0)
				{
					editor_infPropertySelectable(IEV_SOUND0, itemClassIndex);

					char soundBuffer[256];
					strcpy(soundBuffer, elev->sounds[0].c_str());

					ImGui::SetNextItemWidth(160.0f);
					if (editor_assetEditComboBox(0, soundBuffer, 256, soundCount, soundAssets))
					{
						elev->sounds[0] = soundBuffer;
					}
				}
				if (overrides & IEO_SOUND1)
				{
					editor_infPropertySelectable(IEV_SOUND1, itemClassIndex);

					char soundBuffer[256];
					strcpy(soundBuffer, elev->sounds[1].c_str());

					ImGui::SetNextItemWidth(160.0f);
					if (editor_assetEditComboBox(1, soundBuffer, 256, soundCount, soundAssets))
					{
						elev->sounds[1] = soundBuffer;
					}
				}
				if (overrides & IEO_SOUND2)
				{
					editor_infPropertySelectable(IEV_SOUND2, itemClassIndex);

					char soundBuffer[256];
					strcpy(soundBuffer, elev->sounds[2].c_str());

					ImGui::SetNextItemWidth(160.0f);
					if (editor_assetEditComboBox(2, soundBuffer, 256, soundCount, soundAssets))
					{
						elev->sounds[2] = soundBuffer;
					}
				}
				if (overrides & IEO_EVENT_MASK)
				{
					editor_infPropertySelectable(IEV_EVENT_MASK, itemClassIndex);

					ImGui::SetNextItemWidth(128.0f);
					ImGui::InputInt(editor_getUniqueLabel(""), &elev->eventMask);

					if (s_infEditor.curPropIndex == IEV_EVENT_MASK && s_infEditor.curClassIndex == itemClassIndex)
					{
						for (s32 i = 0; i < TFE_ARRAYSIZE(c_infEventMaskNames); i++)
						{
							if ((i % 4) != 0) { ImGui::SameLine(200.0f * (i % 4), 0.0f); }
							ImGui::CheckboxFlags(editor_getUniqueLabel(c_infEventMaskNames[i]), (u32*)&elev->eventMask, 1 << i);
						}
					}
				}
				if (overrides & IEO_ENTITY_MASK)
				{
					editor_infPropertySelectable(IEV_ENTITY_MASK, itemClassIndex);

					ImGui::SetNextItemWidth(128.0f);
					ImGui::InputInt(editor_getUniqueLabel(""), &elev->entityMask);

					if (s_infEditor.curPropIndex == IEV_ENTITY_MASK && s_infEditor.curClassIndex == itemClassIndex)
					{
						for (s32 i = 0; i < TFE_ARRAYSIZE(c_infEntityMaskNames); i++)
						{
							if ((i % 4) != 0) { ImGui::SameLine(200.0f * (i % 4), 0.0f); }
							ImGui::CheckboxFlags(editor_getUniqueLabel(c_infEntityMaskNames[i]), (u32*)&elev->entityMask, c_infEntityMaskFlags[i]);
						}
					}
				}
			}
			ImGui::EndChild();
		}
	}

	void setOrClearBitsCheckboxes(bool wallTarget, u32* arg)
	{
		if (wallTarget)
		{
			if (arg[0] == 1)
			{
				for (s32 i = 0; i < TFE_ARRAYSIZE(c_infWallFlags1Names); i++)
				{
					if ((i % 4) != 0) { ImGui::SameLine(200.0f * (i % 4), 0.0f); }
					ImGui::CheckboxFlags(editor_getUniqueLabel(c_infWallFlags1Names[i]), (u32*)&arg[1], 1 << i);
				}
			}
			else if (arg[0] == 3)
			{
				for (s32 i = 0; i < TFE_ARRAYSIZE(c_infWallFlags3Names); i++)
				{
					if ((i % 4) != 0) { ImGui::SameLine(200.0f * (i % 4), 0.0f); }
					ImGui::CheckboxFlags(editor_getUniqueLabel(c_infWallFlags3Names[i]), (u32*)&arg[1], 1 << i);
				}
			}
		}
		else  // Sector
		{
			if (arg[0] == 1)
			{
				for (s32 i = 0; i < TFE_ARRAYSIZE(c_infSectorFlag1Names); i++)
				{
					if ((i % 4) != 0) { ImGui::SameLine(200.0f * (i % 4), 0.0f); }
					ImGui::CheckboxFlags(editor_getUniqueLabel(c_infSectorFlag1Names[i]), (u32*)&arg[1], 1 << i);
				}
			}
		}
	}

	void editor_infEditTriggerProperties(Editor_InfTrigger* trigger, f32 propHeight, s32 itemClassIndex, const f32* btnTint)
	{
		const TFE_Editor::AssetList& soundList = AssetBrowser::getAssetList(TYPE_SOUND);
		const s32 soundCount = (s32)soundList.size();
		const TFE_Editor::Asset* soundAssets = soundList.data();

		ImGui::Text("%s", "Properties");
		if (trigger->overrideSet & ITO_VAR_MASK)
		{
			ImGui::BeginChild(editor_getUniqueLabel(""), { 0.0f, propHeight }, ImGuiChildFlags_Border);
			{
				const u32 overrides = trigger->overrideSet;
				if (overrides & ITO_SOUND)
				{
					editor_infPropertySelectable(ITV_SOUND, itemClassIndex);

					char soundBuffer[256];
					strcpy(soundBuffer, trigger->sound.c_str());
					ImGui::SetNextItemWidth(160.0f);
					if (editor_assetEditComboBox(3, soundBuffer, 256, soundCount, soundAssets))
					{
						trigger->sound = soundBuffer;
					}
				}
				if (overrides & ITO_MASTER)
				{
					editor_infPropertySelectable(ITV_MASTER, itemClassIndex);
					ImGui::Checkbox(editor_getUniqueLabel(""), &trigger->master);
				}
				if (overrides & ITO_TEXT)
				{
					editor_infPropertySelectable(ITV_TEXT, itemClassIndex);
					ImGui::InputUInt(editor_getUniqueLabel(""), &trigger->textId);
				}
				if (overrides & ITO_EVENT_MASK)
				{
					editor_infPropertySelectable(ITV_EVENT_MASK, itemClassIndex);

					ImGui::SetNextItemWidth(128.0f);
					ImGui::InputInt(editor_getUniqueLabel(""), &trigger->eventMask);

					if (s_infEditor.curPropIndex == ITV_EVENT_MASK && s_infEditor.curClassIndex == itemClassIndex)
					{
						for (s32 i = 0; i < TFE_ARRAYSIZE(c_infEventMaskNames); i++)
						{
							if ((i % 4) != 0) { ImGui::SameLine(200.0f * (i % 4), 0.0f); }
							ImGui::CheckboxFlags(editor_getUniqueLabel(c_infEventMaskNames[i]), (u32*)&trigger->eventMask, 1 << i);
						}
					}
				}
				if (overrides & ITO_ENTITY_MASK)
				{
					editor_infPropertySelectable(ITV_ENTITY_MASK, itemClassIndex);

					ImGui::SetNextItemWidth(128.0f);
					ImGui::InputInt(editor_getUniqueLabel(""), &trigger->entityMask);

					if (s_infEditor.curPropIndex == IEV_ENTITY_MASK && s_infEditor.curClassIndex == itemClassIndex)
					{
						for (s32 i = 0; i < TFE_ARRAYSIZE(c_infEntityMaskNames); i++)
						{
							if ((i % 4) != 0) { ImGui::SameLine(200.0f * (i % 4), 0.0f); }
							ImGui::CheckboxFlags(editor_getUniqueLabel(c_infEntityMaskNames[i]), (u32*)&trigger->entityMask, c_infEntityMaskFlags[i]);
						}
					}
				}
				if (overrides & ITO_EVENT)
				{
					editor_infPropertySelectable(ITV_EVENT, itemClassIndex);

					ImGui::SetNextItemWidth(128.0f);
					ImGui::InputInt(editor_getUniqueLabel(""), &trigger->eventMask);

					if (s_infEditor.curPropIndex == ITV_EVENT && s_infEditor.curClassIndex == itemClassIndex)
					{
						for (s32 i = 0; i < TFE_ARRAYSIZE(c_infEventMaskNames); i++)
						{
							if ((i % 4) != 0) { ImGui::SameLine(200.0f * (i % 4), 0.0f); }
							ImGui::CheckboxFlags(editor_getUniqueLabel(c_infEventMaskNames[i]), (u32*)&trigger->eventMask, 1 << i);
						}
					}
				}
				if (overrides & ITO_MSG)
				{
					bool sel = editor_infPropertySelectable(ITV_MSG, itemClassIndex);

					// Note that "DONE" and "WAKEUP" are not available for triggers
					ImGui::SetNextItemWidth(128.0f);
					ImGui::Combo(editor_getUniqueLabel(""), (s32*)&trigger->cmd, c_editorInfMessageTypeName, IMT_TRIGGER+1);

					switch (trigger->cmd)
					{
						// 0-args
						case IMT_NEXT_STOP:
						case IMT_PREV_STOP:
						case IMT_MASTER_ON:
						case IMT_MASTER_OFF:
						case IMT_LIGHTS:
						case IMT_TRIGGER:
						case IMT_CAMERA:
						{
							// Nothing
						} break;
						// 1-arg
						case IMT_GOTO_STOP:
						case IMT_COMPLETE:
						{
							ImGui::SameLine(0.0f, 16.0f);
							ImGui::Text(trigger->cmd == IMT_GOTO_STOP ? "Stop ID" : "Goal ID"); ImGui::SameLine(0.0f, 8.0f);

							ImGui::SetNextItemWidth(128.0f);
							ImGui::InputUInt(editor_getUniqueLabel(""), &trigger->arg[0]);
						} break;
						// 2-args
						case IMT_SET_BITS:
						case IMT_CLEAR_BITS:
						{
							ImGui::SameLine(0.0f, 16.0f);

							ImGui::Text("Flags"); ImGui::SameLine(0.0f, 8.0f);
							ImGui::SetNextItemWidth(48.0f);
							trigger->arg[0] = min(3u, max(1u, trigger->arg[0]));
							s32 flag = min(2, max(0, (s32)trigger->arg[0] - 1));
							const char* flagNames[] = { "1", "2", "3" };
							if (ImGui::Combo(editor_getUniqueLabel(""), &flag, flagNames, TFE_ARRAYSIZE(flagNames)))
							{
								trigger->arg[0] = flag + 1;
							}

							ImGui::SameLine(0.0f, 16.0f);
							ImGui::Text("Bit"); ImGui::SameLine(0.0f, 8.0f);
							ImGui::SetNextItemWidth(128.0f);
							ImGui::InputUInt(editor_getUniqueLabel(""), &trigger->arg[1]);

							// Handle showing checkboxes if selected.
							if (sel && !trigger->clients.empty())
							{
								setOrClearBitsCheckboxes(trigger->clients[0].targetWall >= 0, trigger->arg);
							}
						} break;
					}
				}
			}
			ImGui::EndChild();
		}
	}

	void editor_infAddOrRemoveElevProperty(Editor_InfElevator* elev, s32 itemClassIndex)
	{
		if (editor_button("+"))
		{
			if (s_infEditor.comboElevVarIndex >= 0)
			{
				elev->overrideSet |= (1 << s_infEditor.comboElevVarIndex);
				if (s_infEditor.comboElevVarIndex == IEV_KEY0)
				{
					elev->key[0] = KEY_RED;
				}
				else if (s_infEditor.comboElevVarIndex == IEV_KEY1)
				{
					elev->key[1] = KEY_RED;
				}
			}
			editor_infSelectEditClass(itemClassIndex);
		}
		setTooltip("Add a new property.");
		ImGui::SameLine(0.0f, 4.0f);
		if (editor_button("-"))
		{
			if (s_infEditor.curPropIndex >= 0)
			{
				elev->overrideSet &= ~(1 << s_infEditor.curPropIndex);
				if (s_infEditor.comboElevVarIndex == IEV_KEY0)
				{
					elev->key[0] = KEY_NONE;
				}
				else if (s_infEditor.comboElevVarIndex == IEV_KEY1)
				{
					elev->key[1] = KEY_NONE;
				}
			}
			editor_infSelectEditClass(itemClassIndex);
			s_infEditor.curPropIndex = -1;
		}
		setTooltip("Remove the selected property.");

		ImGui::SameLine(0.0f, 16.0f);
		ImGui::SetNextItemWidth(128.0f);
		if (ImGui::BeginCombo(editor_getUniqueLabel(""), c_infElevVarName[s_infEditor.comboElevVarIndex]))
		{
			s32 count = (s32)TFE_ARRAYSIZE(c_infElevVarName);
			for (s32 i = 0; i < count; i++)
			{
				if (ImGui::Selectable(c_infElevVarName[i], i == s_infEditor.comboElevVarIndex))
				{
					s_infEditor.comboElevVarIndex = i;
				}
				//setTooltip(c_infClassName[i].tooltip.c_str());
			}
			ImGui::EndCombo();
			editor_infSelectEditClass(itemClassIndex);
		}
		setTooltip("Property to add.");
	}

	void editor_infAddOrRemoveTriggerProperty(Editor_InfTrigger* trigger, s32 itemClassIndex)
	{
		if (editor_button("+"))
		{
			if (s_infEditor.comboElevVarIndex >= 0)
			{
				trigger->overrideSet |= (1 << s_infEditor.comboElevVarIndex);
			}
			editor_infSelectEditClass(itemClassIndex);
		}
		setTooltip("Add a new property.");
		ImGui::SameLine(0.0f, 4.0f);
		if (editor_button("-"))
		{
			if (s_infEditor.curPropIndex >= 0)
			{
				trigger->overrideSet &= ~(1 << s_infEditor.curPropIndex);
			}
			editor_infSelectEditClass(itemClassIndex);
			s_infEditor.curPropIndex = -1;
		}
		setTooltip("Remove the selected property.");

		ImGui::SameLine(0.0f, 16.0f);
		ImGui::SetNextItemWidth(128.0f);
		s32 startIndex = trigger->type == ITRIGGER_SECTOR ? 1 : 0;
		s_infEditor.comboElevVarIndex = max(startIndex, s_infEditor.comboElevVarIndex);
		if (ImGui::BeginCombo(editor_getUniqueLabel(""), c_infTriggerVarName[s_infEditor.comboElevVarIndex]))
		{
			s32 count = (s32)TFE_ARRAYSIZE(c_infTriggerVarName);
			for (s32 i = startIndex; i < count; i++)
			{
				if (ImGui::Selectable(c_infTriggerVarName[i], i == s_infEditor.comboElevVarIndex))
				{
					s_infEditor.comboElevVarIndex = i;
				}
				//setTooltip(c_infClassName[i].tooltip.c_str());
			}
			ImGui::EndCombo();
			editor_infSelectEditClass(itemClassIndex);
		}
		setTooltip("Property to add.");
	}
		
	void editor_infAddOrRemoveStopCmd(Editor_InfElevator* elev, Editor_InfStop* stop, s32 itemClassIndex)
	{
		ImGui::Text("    "); ImGui::SameLine(0.0f, 0.0f);
		if (editor_button("+"))
		{
			switch (s_infEditor.comboElevCmdIndex)
			{
				case ISC_MESSAGE:
				{
					stop->msg.push_back({});
				} break;
				case ISC_ADJOIN:
				{
					stop->adjoinCmd.push_back({});
				} break;
				case ISC_TEXTURE:
				{
					stop->textureCmd.push_back({});
				} break;
				case ISC_PAGE:
				{
					stop->page = {};
					stop->overrideSet |= ISO_PAGE;
				} break;
				case ISC_SCRIPTCALL:
				{
					stop->scriptCall.push_back({});
				} break;
			}
		}
		setTooltip("Add a new command to the selected stop.");
		ImGui::SameLine(0.0f, 4.0f);
		if (editor_button("-") && s_infEditor.curStopCmdIndex >= 0)
		{
			const s32 msgCount    = (s32)stop->msg.size();
			const s32 adjoinCount = (s32)stop->adjoinCmd.size();
			const s32 texCount    = (s32)stop->textureCmd.size();
			const s32 scriptCallCount = (s32)stop->scriptCall.size();

			s32 index = -1;
			s32 cmdIndexOffset = 0;
			if (index < 0 && s_infEditor.curStopCmdIndex < msgCount + cmdIndexOffset)
			{
				index = s_infEditor.curStopCmdIndex - cmdIndexOffset;
				for (s32 i = index; i < msgCount - 1; i++)
				{
					stop->msg[i] = stop->msg[i + 1];
				}
				stop->msg.pop_back();
				s_infEditor.curStopCmdIndex = -1;
			}
			cmdIndexOffset += msgCount;

			if (index < 0 && s_infEditor.curStopCmdIndex < scriptCallCount + cmdIndexOffset)
			{
				index = s_infEditor.curStopCmdIndex - cmdIndexOffset;
				for (s32 i = index; i < scriptCallCount - 1; i++)
				{
					stop->scriptCall[i] = stop->scriptCall[i + 1];
				}
				stop->scriptCall.pop_back();
				s_infEditor.curStopCmdIndex = -1;
			}
			cmdIndexOffset += scriptCallCount;

			if (index < 0 && s_infEditor.curStopCmdIndex < adjoinCount + cmdIndexOffset)
			{
				index = s_infEditor.curStopCmdIndex - cmdIndexOffset;
				for (s32 i = index; i < adjoinCount - 1; i++)
				{
					stop->adjoinCmd[i] = stop->adjoinCmd[i + 1];
				}
				stop->adjoinCmd.pop_back();
				s_infEditor.curStopCmdIndex = -1;
			}
			cmdIndexOffset += adjoinCount;

			if (index < 0 && s_infEditor.curStopCmdIndex < texCount + cmdIndexOffset)
			{
				index = s_infEditor.curStopCmdIndex - cmdIndexOffset;
				for (s32 i = index; i < texCount - 1; i++)
				{
					stop->textureCmd[i] = stop->textureCmd[i + 1];
				}
				stop->textureCmd.pop_back();
				s_infEditor.curStopCmdIndex = -1;
			}
			cmdIndexOffset += texCount;

			if (index < 0 && s_infEditor.curStopCmdIndex <= cmdIndexOffset)
			{
				stop->page = {};
				stop->overrideSet &= ~ISO_PAGE;
				index = 0;
			}
			cmdIndexOffset++;
		}
		setTooltip("Remove the selected command from the stop.");
		ImGui::SameLine(0.0f, 16.0f);

		ImGui::SetNextItemWidth(128.0f);
		if (project_supportsFeature(FEATURE_FORCE_SCRIPT))
		{
			ImGui::Combo(editor_getUniqueLabel(""), &s_infEditor.comboElevCmdIndex, c_elevStopCmdName, TFE_ARRAYSIZE(c_elevStopCmdName));
		}
		else
		{
			ImGui::Combo(editor_getUniqueLabel(""), &s_infEditor.comboElevCmdIndex, c_elevStopCmdName, TFE_ARRAYSIZE(c_elevStopCmdName) - 1);
		}
		setTooltip("Type of stop command to add.");
	}

	void editor_infAddOrRemoveElevStopOrSlave(Editor_InfElevator* elev, s32 itemClassIndex)
	{
		if (editor_button("+"))
		{
			if (s_infEditor.comboElevAddContentIndex == 0)
			{
				// insert a stop after the selected stop.
				Editor_InfStop newStop = {};
				if (s_infEditor.curContentIndex >= 0 && s_infEditor.curContentIndex < elev->stops.size())
				{
					newStop.value = elev->stops[s_infEditor.curContentIndex].value + 1.0f;
					elev->stops.insert(elev->stops.begin() + s_infEditor.curContentIndex + 1, newStop);
				}
				else
				{
					elev->stops.push_back(newStop);
				}
			}
			else if (s_infEditor.comboElevAddContentIndex == 1)
			{
				// insert a slave.
				Editor_InfSlave newSlave = {};
				const s32 stopCount = (s32)elev->stops.size();
				const s32 slaveCount = (s32)elev->slaves.size();
				if (s_infEditor.curContentIndex >= stopCount && s_infEditor.curContentIndex < stopCount + slaveCount)
				{
					elev->slaves.insert(elev->slaves.begin() + s_infEditor.curContentIndex - stopCount + 1, newSlave);
				}
				else
				{
					elev->slaves.push_back(newSlave);
				}
			}
			editor_infSelectEditClass(itemClassIndex);
		}
		setTooltip("Add a new stop or slave.");
		ImGui::SameLine(0.0f, 4.0f);
		if (editor_button("-"))
		{
			const s32 stopCount = (s32)elev->stops.size();
			const s32 slaveCount = (s32)elev->slaves.size();
			if (s_infEditor.curContentIndex >= 0 && s_infEditor.curContentIndex < stopCount)
			{
				for (s32 s = s_infEditor.curContentIndex; s < stopCount - 1; s++)
				{
					elev->stops[s] = elev->stops[s + 1];
				}
				elev->stops.pop_back();
				s_infEditor.curContentIndex = -1;
				s_infEditor.curStopCmdIndex = -1;
			}
			else if (s_infEditor.curContentIndex >= stopCount && s_infEditor.curContentIndex < stopCount + slaveCount)
			{
				for (s32 s = s_infEditor.curContentIndex - stopCount; s < slaveCount - 1; s++)
				{
					elev->slaves[s] = elev->slaves[s + 1];
				}
				elev->slaves.pop_back();
				s_infEditor.curContentIndex = -1;
				s_infEditor.curStopCmdIndex = -1;
			}
			editor_infSelectEditClass(itemClassIndex);
		}
		setTooltip("Remove the selected elevator item.");
		ImGui::SameLine(0.0f, 16.0f);

		const char* c_elevAddTypes[] = { "Stop", "Slave" };
		ImGui::SetNextItemWidth(128.0f);
		ImGui::Combo(editor_getUniqueLabel(""), &s_infEditor.comboElevAddContentIndex, c_elevAddTypes, TFE_ARRAYSIZE(c_elevAddTypes));
		setTooltip("Type of elevator item to add - Stop or Slave.");
	}

	void editor_infAddOrRemoveTriggerClient(Editor_InfTrigger* trigger, s32 itemClassIndex)
	{
		if (editor_button("+"))
		{
			// insert a client after the selected client.
			if (s_infEditor.curContentIndex >= 0 && s_infEditor.curContentIndex < trigger->clients.size())
			{
				trigger->clients.insert(trigger->clients.begin() + s_infEditor.curContentIndex + 1, {});
			}
			else
			{
				trigger->clients.push_back({});
			}
			editor_infSelectEditClass(itemClassIndex);
		}
		setTooltip("Add a new client.");
		ImGui::SameLine(0.0f, 4.0f);
		if (editor_button("-"))
		{
			const s32 clientCount = (s32)trigger->clients.size();
			if (s_infEditor.curContentIndex >= 0 && s_infEditor.curContentIndex < clientCount)
			{
				for (s32 s = s_infEditor.curContentIndex; s < clientCount - 1; s++)
				{
					trigger->clients[s] = trigger->clients[s + 1];
				}
				trigger->clients.pop_back();
				s_infEditor.curContentIndex = -1;
				s_infEditor.curStopCmdIndex = -1;
			}
			editor_infSelectEditClass(itemClassIndex);
		}
		setTooltip("Remove the selected client.");
	}

	bool editor_stopCmdSelectable(Editor_InfElevator* elev, Editor_InfStop* stop, s32 itemClassIndex, s32 cmdIndex, const char* label)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, colorKeywordInner);
		ImGui::PushStyleColor(ImGuiCol_Header, colorInnerHeader);
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, colorInnerHeaderActive);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, colorInnerHeaderHovered);
		const s32 stopId = TFE_ARRAYPOS(stop, elev->stops.data());
		bool sel = s_infEditor.curClassIndex == itemClassIndex && s_infEditor.curContentIndex == stopId && s_infEditor.curStopCmdIndex == cmdIndex;
		ImGui::Text("    "); ImGui::SameLine(0.0f, 0.0f);
		if (ImGui::Selectable(editor_getUniqueLabel(label), sel, 0, { 80.0f, 0.0f }))
		{
			editor_infSelectEditClass(itemClassIndex);
			s_infEditor.curStopCmdIndex = sel ? -1 : cmdIndex;
			s_infEditor.curContentIndex = stopId;
		}
		ImGui::PopStyleColor(4);
		ImGui::SameLine(0.0f, 8.0f);
		return sel;
	}

	void editor_infEditElevStops(Editor_InfElevator* elev, f32 contentHeight, s32 itemClassIndex, const f32* btnTint)
	{
		const TFE_Editor::AssetList& soundList = AssetBrowser::getAssetList(TYPE_SOUND);
		const s32 soundCount = (s32)soundList.size();
		const TFE_Editor::Asset* soundAssets = soundList.data();

		char buffer[256];
		const s32 stopCount = (s32)elev->stops.size();
		if (!stopCount) { return; }
		Editor_InfStop* stop = elev->stops.data();
		for (s32 s = 0; s < stopCount; s++, stop++)
		{
			///////////////////////////////
			// Stop
			///////////////////////////////
			sprintf(buffer, "Stop: %d", s);
			ImGui::PushStyleColor(ImGuiCol_Text, colorKeywordOuter);
			bool sel = s_infEditor.curClassIndex == itemClassIndex && s_infEditor.curContentIndex == s;
			if (ImGui::Selectable(editor_getUniqueLabel(buffer), sel, 0, { 80.0f, 0.0f }))
			{
				editor_infSelectEditClass(itemClassIndex);
				s_infEditor.curContentIndex = sel ? -1 : s;
				s_infEditor.curStopCmdIndex = -1;
				// Update select.
				sel = s_infEditor.curClassIndex == itemClassIndex && s_infEditor.curContentIndex == s;
			}
			ImGui::PopStyleColor();
			ImGui::SameLine(0.0f, 16.0f);
			ImGui::Text("Value:");
			ImGui::SameLine(0.0f, 8.0f);

			ImGui::SetNextItemWidth(128.0f);
			if (stop->fromSectorFloor.empty())
			{
				// TODO: How to turn into a string?
				ImGui::InputFloat(editor_getUniqueLabel(""), &stop->value, 0.1f, 1.0f, "%.3f", ImGuiInputTextFlags_CharsDecimal);
			}
			else
			{
				// TODO: How to turn into a number?
				char nameBuffer[256];
				strcpy(nameBuffer, stop->fromSectorFloor.c_str());
				if (ImGui::InputText(editor_getUniqueLabel(""), nameBuffer, 256))
				{
					stop->fromSectorFloor = nameBuffer;
				}
			}
			ImGui::SameLine(0.0f, 16.0f);

			ImGui::Text("Relative:");
			ImGui::SameLine(0.0f, 8.0f);
			ImGui::Checkbox(editor_getUniqueLabel(""), &stop->relative);
			ImGui::SameLine(0.0f, 16.0f);

			ImGui::Text("Delay Type:");
			ImGui::SameLine(0.0f, 8.0f);
			ImGui::SetNextItemWidth(100.0f);
			if (ImGui::BeginCombo(editor_getUniqueLabel(""), c_infStopDelayTypeName[stop->delayType]))
			{
				s32 count = (s32)TFE_ARRAYSIZE(c_infStopDelayTypeName);
				for (s32 c = 0; c < count; c++)
				{
					if (ImGui::Selectable(editor_getUniqueLabel(c_infStopDelayTypeName[c]), c == stop->delayType))
					{
						stop->delayType = Editor_InfStopDelayType(c);
					}
					//setTooltip(c_infClassName[i].tooltip.c_str());
				}
				ImGui::EndCombo();
			}

			// Only show the delay time if needed.
			if (stop->delayType == SDELAY_SECONDS)
			{
				ImGui::SameLine(0.0f, 16.0f);
				ImGui::Text("Delay:");
				ImGui::SameLine(0.0f, 8.0f);
				ImGui::SetNextItemWidth(128.0f);
				ImGui::InputFloat(editor_getUniqueLabel(""), &stop->delay, 0.1f, 1.0f, "%.3f", ImGuiInputTextFlags_CharsDecimal);
			}

			///////////////////////////////
			// Commands.
			///////////////////////////////
			s32 cmdIndexOffset = 0;
			const s32 msgCount = (s32)stop->msg.size();
			Editor_InfMessage* msg = stop->msg.data();
			char targetBuffer[256];
			for (s32 m = 0; m < msgCount; m++, msg++)
			{
				bool cmdSelected = editor_stopCmdSelectable(elev, stop, itemClassIndex, m + cmdIndexOffset, "Message:");

				ImGui::Text("Target"); ImGui::SameLine(0.0f, 8.0f);
				if (msg->targetWall >= 0) { sprintf(targetBuffer, "%s(%d)", msg->targetSector.c_str(), msg->targetWall); }
				else { strcpy(targetBuffer, msg->targetSector.c_str()); }
				ImGui::SetNextItemWidth(128.0f);
				if (ImGui::InputText(editor_getUniqueLabel(""), targetBuffer, 256))
				{
					parseTarget(msg, targetBuffer);
				}

				ImGui::SameLine(0.0f, 8.0f);
				if (iconButtonInline(ICON_SELECT, "Select target sector or wall in the viewport.", btnTint, true))
				{
					editor_selectViewportFeature((Editor_InfClass*)elev, SELECTMODE_SECTOR_OR_WALL, SELTYPE_ELEV_MSG_TARGET, s, m);
				}

				ImGui::SameLine(0.0f, 16.0f);
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
				ImGui::Text("Type"); ImGui::SameLine(0.0f, 8.0f);
				ImGui::SetNextItemWidth(128.0f);
				ImGui::Combo(editor_getUniqueLabel(""), (s32*)&msg->type, c_editorInfMessageTypeName, IMT_COUNT);

				switch (msg->type)
				{
					// 0-args
					case IMT_NEXT_STOP:
					case IMT_PREV_STOP:
					case IMT_MASTER_ON:
					case IMT_MASTER_OFF:
					case IMT_LIGHTS:
					case IMT_TRIGGER:
					case IMT_DONE:
					case IMT_WAKEUP:
					case IMT_CAMERA:
					{
						// Nothing
					} break;
					// 1-arg
					case IMT_GOTO_STOP:
					case IMT_COMPLETE:
					{
						ImGui::SameLine(0.0f, 16.0f);
						ImGui::Text(msg->type == IMT_GOTO_STOP ? "Stop ID" : "Goal ID"); ImGui::SameLine(0.0f, 8.0f);

						ImGui::SetNextItemWidth(128.0f);
						ImGui::InputUInt(editor_getUniqueLabel(""), &msg->arg[0]);
					} break;
					// 2-args
					case IMT_SET_BITS:
					case IMT_CLEAR_BITS:
					{
						ImGui::SameLine(0.0f, 16.0f);

						ImGui::Text("Flags"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::SetNextItemWidth(48.0f);
						msg->arg[0] = min(3u, max(msg->arg[0], 1u));
						s32 flag = min(2, max(0, (s32)msg->arg[0] - 1));
						const char* flagNames[] = { "1", "2", "3" };
						if (ImGui::Combo(editor_getUniqueLabel(""), &flag, flagNames, TFE_ARRAYSIZE(flagNames)))
						{
							msg->arg[0] = flag + 1;
						}

						ImGui::SameLine(0.0f, 16.0f);
						ImGui::Text("Bit"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::SetNextItemWidth(128.0f);
						ImGui::InputUInt(editor_getUniqueLabel(""), &msg->arg[1]);

						// Handle showing checkboxes if selected.
						if (cmdSelected)
						{
							setOrClearBitsCheckboxes(msg->targetWall >= 0, msg->arg);
						}
					} break;
				}
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
			}
			cmdIndexOffset += msgCount;

			const s32 scriptCallCount = (s32)stop->scriptCall.size();
			Editor_ScriptCall* call = stop->scriptCall.data();
			for (s32 c = 0; c < scriptCallCount; c++, call++)
			{
				bool cmdSelected = editor_stopCmdSelectable(elev, stop, itemClassIndex, c + cmdIndexOffset, "ScriptCall:");

				ImGui::Text("Function"); ImGui::SameLine(0.0f, 8.0f);
				strcpy(targetBuffer, call->funcName.c_str());
				ImGui::SetNextItemWidth(128.0f);
				if (ImGui::InputText(editor_getUniqueLabel(""), targetBuffer, 256))
				{
					call->funcName = targetBuffer;
				}
				ImGui::SameLine(0.0f, 16.0f);

				ImGui::Text("Arguments"); ImGui::SameLine(0.0f, 8.0f);
				for (s32 a = 0; a < 4; a++)
				{
					strcpy(targetBuffer, call->arg[a].value.c_str());
					ImGui::SetNextItemWidth(96.0f);
					if (ImGui::InputText(editor_getUniqueLabel(""), targetBuffer, 256))
					{
						call->arg[a].value = targetBuffer;
					}
					if (a < 3) { ImGui::SameLine(0.0f, 8.0f); }
				}
			}
			cmdIndexOffset += scriptCallCount;

			const s32 adjoinCount = (s32)stop->adjoinCmd.size();
			Editor_InfAdjoinCmd* cmd = stop->adjoinCmd.data();
			for (s32 c = 0; c < adjoinCount; c++, cmd++)
			{
				editor_stopCmdSelectable(elev, stop, itemClassIndex, c + cmdIndexOffset, "Adjoin:");

				ImGui::Text("Sector 1"); ImGui::SameLine(0.0f, 8.0f);
				strcpy(targetBuffer, cmd->sector0.c_str());
				ImGui::SetNextItemWidth(128.0f);
				if (ImGui::InputText(editor_getUniqueLabel(""), targetBuffer, 256))
				{
					cmd->sector0 = targetBuffer;
				}
				ImGui::SameLine(0.0f, 8.0f);
				ImGui::SetNextItemWidth(80.0f);
				ImGui::InputInt(editor_getUniqueLabel(""), &cmd->wallIndex0);
				ImGui::SameLine(0.0f, 8.0f);
				if (iconButtonInline(ICON_SELECT, "Select wall to adjoin in the viewport.", btnTint, true))
				{
					editor_selectViewportFeature((Editor_InfClass*)elev, SELECTMODE_WALL, SELTYPE_ELEV_ADJOIN0, s, c);
				}

				ImGui::SameLine(0.0f, 16.0f);
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);

				ImGui::Text("Sector 2"); ImGui::SameLine(0.0f, 8.0f);
				strcpy(targetBuffer, cmd->sector1.c_str());
				ImGui::SetNextItemWidth(128.0f);
				if (ImGui::InputText(editor_getUniqueLabel(""), targetBuffer, 256))
				{
					cmd->sector1 = targetBuffer;
				}
				ImGui::SameLine(0.0f, 8.0f);
				ImGui::SetNextItemWidth(80.0f);
				ImGui::InputInt(editor_getUniqueLabel(""), &cmd->wallIndex1);
				ImGui::SameLine(0.0f, 8.0f);
				if (iconButtonInline(ICON_SELECT, "Select wall to adjoin in the viewport.", btnTint, true))
				{
					editor_selectViewportFeature((Editor_InfClass*)elev, SELECTMODE_WALL, SELTYPE_ELEV_ADJOIN1, s, c);
				}
			}
			cmdIndexOffset += adjoinCount;

			const s32 texCount = (s32)stop->textureCmd.size();
			Editor_InfTextureCmd* texCmd = stop->textureCmd.data();
			for (s32 t = 0; t < texCount; t++, texCmd++)
			{
				editor_stopCmdSelectable(elev, stop, itemClassIndex, t + cmdIndexOffset, "Texture:");

				ImGui::Text("Donor Sector"); ImGui::SameLine(0.0f, 8.0f);
				strcpy(targetBuffer, texCmd->donorSector.c_str());
				ImGui::SetNextItemWidth(128.0f);
				if (ImGui::InputText(editor_getUniqueLabel(""), targetBuffer, 256))
				{
					texCmd->donorSector = targetBuffer;
				}
				ImGui::SameLine(0.0f, 8.0f);
				if (iconButtonInline(ICON_SELECT, "Select donor sector in the viewport.", btnTint, true))
				{
					editor_selectViewportFeature((Editor_InfClass*)elev, SELECTMODE_SECTOR, SELTYPE_ELEV_DONOR_SECTOR, s, t);
				}

				ImGui::SameLine(0.0f, 16.0f);
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
				ImGui::Text("From Ceiling"); ImGui::SameLine(0.0f, 8.0f);
				ImGui::Checkbox(editor_getUniqueLabel(""), &texCmd->fromCeiling);
			}
			cmdIndexOffset += texCount;

			if (stop->overrideSet & ISO_PAGE)
			{
				editor_stopCmdSelectable(elev, stop, itemClassIndex, cmdIndexOffset, "Page:");

				strcpy(targetBuffer, stop->page.c_str());
				ImGui::SetNextItemWidth(128.0f);
				if (editor_assetEditComboBox(4 + s, targetBuffer, 256, soundCount, soundAssets))
				{
					stop->page = targetBuffer;
				}
			}

			// Show buttons to add new commands inline.
			if (sel)
			{
				editor_infAddOrRemoveStopCmd(elev, stop, itemClassIndex);
			}
		}
	}
		
	void editor_selectViewportFeature(Editor_InfClass* editClass, SelectMode mode, SelectionType type, s32 index0, s32 index1)
	{
		// Save Inf Editor State.
		s_infEditorState.editClass = editClass;
		s_infEditorState.editorState = s_infEditor;
		s_infEditorState.type = type;
		s_infEditorState.index0 = index0;
		s_infEditorState.index1 = index1;

		// Set special selection mode (aka nothing else works).
		hidePopup();
		setSelectMode(mode);
	}

	void editor_handleSelection(Vec3f pos)
	{
		// Restore Inf Editor State.
		s_infEditor = s_infEditorState.editorState;

		if (s_infEditorState.editClass->classId == IIC_ELEVATOR)
		{
			Editor_InfElevator* elev = getElevFromClassData(s_infEditorState.editClass);
			if (s_infEditorState.type == SELTYPE_ELEV_POSITION)
			{
				elev->center = { pos.x, pos.z };
			}
		}
		else if (s_infEditorState.editClass->classId == IIC_TELEPORTER)
		{
			Editor_InfTeleporter* teleporter = getTeleporterFromClassData(s_infEditorState.editClass);
			if (s_infEditorState.type == SELTYPE_TELEPORT_POSITION)
			{
				teleporter->dstPos = pos;
			}
		}

		// Restore the popup.
		showPopup();
		setSelectMode(SELECTMODE_NONE);
		s_restorePos = POPUP_RESTORE_COUNT;
	}

	void editor_handleSelection(EditorSector* sector, s32 wallIndex/* = -1*/)
	{
		// Restore Inf Editor State.
		s_infEditor = s_infEditorState.editorState;

		// Set the client name.
		if (s_infEditorState.editClass->classId == IIC_TRIGGER)
		{
			Editor_InfTrigger* trigger = getTriggerFromClassData(s_infEditorState.editClass);
			if (s_infEditorState.type == SELTYPE_TRIGGER_CLIENT)
			{
				trigger->clients[s_infEditorState.index0].targetSector = sector->name;
				trigger->clients[s_infEditorState.index0].targetWall = wallIndex;
			}
		}
		else if (s_infEditorState.editClass->classId == IIC_ELEVATOR)
		{
			Editor_InfElevator* elev = getElevFromClassData(s_infEditorState.editClass);
			if (s_infEditorState.type == SELTYPE_ELEV_SLAVE)
			{
				elev->slaves[s_infEditorState.index0].name = sector->name;
			}
			else if (s_infEditorState.type == SELTYPE_ELEV_MSG_TARGET)
			{
				elev->stops[s_infEditorState.index0].msg[s_infEditorState.index1].targetSector = sector->name;
				elev->stops[s_infEditorState.index0].msg[s_infEditorState.index1].targetWall = wallIndex;
			}
			else if (s_infEditorState.type == SELTYPE_ELEV_ADJOIN0)
			{
				elev->stops[s_infEditorState.index0].adjoinCmd[s_infEditorState.index1].sector0 = sector->name;
				elev->stops[s_infEditorState.index0].adjoinCmd[s_infEditorState.index1].wallIndex0 = wallIndex;
			}
			else if (s_infEditorState.type == SELTYPE_ELEV_ADJOIN1)
			{
				elev->stops[s_infEditorState.index0].adjoinCmd[s_infEditorState.index1].sector1 = sector->name;
				elev->stops[s_infEditorState.index0].adjoinCmd[s_infEditorState.index1].wallIndex1 = wallIndex;
			}
			else if (s_infEditorState.type == SELTYPE_ELEV_DONOR_SECTOR)
			{
				elev->stops[s_infEditorState.index0].textureCmd[s_infEditorState.index1].donorSector = sector->name;
			}
			else if (s_infEditorState.type == SELTYPE_ELEV_SECTOR_CENTER)
			{
				elev->center.x = (sector->bounds[0].x + sector->bounds[1].x) * 0.5f;
				elev->center.z = (sector->bounds[0].z + sector->bounds[1].z) * 0.5f;
			}
			else if (s_infEditorState.type == SELTYPE_ELEV_ANGLE && wallIndex >= 0)
			{
				const EditorWall* wall = &sector->walls[wallIndex];
				const Vec2f* v0 = &sector->vtx[wall->idx[0]];
				const Vec2f* v1 = &sector->vtx[wall->idx[1]];
				elev->angle = 2.0f * PI * vec2ToAngle(v1->x - v0->x, v1->z - v0->z) / 16384.0f;
			}
		}
		else if (s_infEditorState.editClass->classId == IIC_TELEPORTER)
		{
			Editor_InfTeleporter* teleporter = getTeleporterFromClassData(s_infEditorState.editClass);
			if (s_infEditorState.type == SELTYPE_TELEPORT_TARGET)
			{
				teleporter->target = sector->name;
			}
		}

		// Restore the popup.
		showPopup();
		setSelectMode(SELECTMODE_NONE);
		s_restorePos = POPUP_RESTORE_COUNT;
	}

	void editor_infEditTriggerClients(Editor_InfTrigger* trigger, f32 contentHeight, s32 itemClassIndex, const f32* btnTint)
	{
		const s32 clientCount = (s32)trigger->clients.size();
		if (!clientCount) { return; }

		Editor_InfClient* client = trigger->clients.data();
		char nameBuffer[256];
		for (s32 c = 0; c < clientCount; c++, client++)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, colorKeywordOuter);
			bool sel = s_infEditor.curClassIndex == itemClassIndex && s_infEditor.curContentIndex == c;
			if (ImGui::Selectable(editor_getUniqueLabel("Client:"), sel, 0, { 80.0f, 0.0f }))
			{
				editor_infSelectEditClass(itemClassIndex);
				s_infEditor.curContentIndex = sel ? -1 : c;
				s_infEditor.curStopCmdIndex = -1;
			}
			ImGui::PopStyleColor();

			ImGui::SameLine(0.0f, 16.0f);
			ImGui::Text("Target"); ImGui::SameLine(0.0f, 8.0f);
			ImGui::SetNextItemWidth(128.0f);
			if (client->targetWall >= 0)
			{
				sprintf(nameBuffer, "%s(%d)", client->targetSector.c_str(), client->targetWall);
			}
			else
			{
				strcpy(nameBuffer, client->targetSector.c_str());
			}
			if (ImGui::InputText(editor_getUniqueLabel(""), nameBuffer, 256))
			{
				parseTarget(client, nameBuffer);
			}
			ImGui::SameLine(0.0f, 8.0f);
			if (iconButtonInline(ICON_SELECT, "Select the client sector or wall in the viewport.", btnTint, true))
			{
				editor_selectViewportFeature((Editor_InfClass*)trigger, SELECTMODE_SECTOR_OR_WALL, SELTYPE_TRIGGER_CLIENT, c);
			}
			// Reset the cursor Y position so that controls line up after icon buttons.
			// This must be done after "same line" since it modifies the y position.
			ImGui::SameLine(0.0f, 16.0f);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);

			ImGui::Text("Event_Mask"); ImGui::SameLine(0.0f, 8.0f);
			ImGui::SetNextItemWidth(128.0f);
			ImGui::InputInt(editor_getUniqueLabel(""), &client->eventMask);

			// Avoid adding extra spacing due to inline icon buttons.
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.0f);
		}
	}

	void editor_infEditElevSlaves(Editor_InfElevator* elev, f32 contentHeight, s32 itemClassIndex, const f32* btnTint)
	{
		const s32 slaveCount = (s32)elev->slaves.size();
		const s32 stopCount = (s32)elev->stops.size();
		if (!slaveCount) { return; }

		Editor_InfSlave* slave = elev->slaves.data();
		char nameBuffer[256];
		for (s32 s = 0; s < slaveCount; s++, slave++)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, colorKeywordOuter);
			bool sel = s_infEditor.curClassIndex == itemClassIndex && s_infEditor.curContentIndex == s + stopCount;
			if (ImGui::Selectable(editor_getUniqueLabel("Slave:"), sel, 0, { 80.0f, 0.0f }))
			{
				editor_infSelectEditClass(itemClassIndex);
				s_infEditor.curContentIndex = sel ? -1 : s + stopCount;
				s_infEditor.curStopCmdIndex = -1;
			}
			ImGui::PopStyleColor();

			ImGui::SameLine(0.0f, 16.0f);
			ImGui::Text("Sector"); ImGui::SameLine(0.0f, 8.0f);
			ImGui::SetNextItemWidth(128.0f);
			strcpy(nameBuffer, slave->name.c_str());
			if (ImGui::InputText(editor_getUniqueLabel(""), nameBuffer, 256))
			{
				slave->name = nameBuffer;
			}
			ImGui::SameLine(0.0f, 8.0f);
			if (iconButtonInline(ICON_SELECT, "Select the slave sector in the viewport.", btnTint, true))
			{
				editor_selectViewportFeature((Editor_InfClass*)elev, SELECTMODE_SECTOR, SELTYPE_ELEV_SLAVE, s);
			}
			// Reset the cursor Y position so that controls line up after icon buttons.
			// This must be done after "same line" since it modifies the y position.
			ImGui::SameLine(0.0f, 16.0f);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);

			ImGui::Text("Angle Offset"); ImGui::SameLine(0.0f, 8.0f);
			ImGui::SetNextItemWidth(128.0f);
			ImGui::InputFloat(editor_getUniqueLabel(""), &slave->angleOffset);

			// Avoid adding extra spacing due to inline icon buttons.
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.0f);
		}
	}

	void selectableClassName(const char* className, ImVec2 itemStart, s32 classIndex)
	{
		ImVec2 itemEnd = ImGui::GetCursorPos();
		ImGui::TextColored(colorKeywordInner, className);
		itemEnd.x += ImGui::CalcTextSize(className).x;
		ImVec2 itemNext = ImGui::GetCursorPos();

		ImGui::SetCursorPos(itemStart);
		if (ImGui::InvisibleButton(editor_getUniqueLabel(""), ImVec2(itemEnd.x - itemStart.x, 28.0f)))
		{
			editor_infSelectEditClass(classIndex);
		}
		ImGui::SetCursorPos(itemNext);
	}

	void editor_infEdit_UI(s32 deleteIndex)
	{
		const f32 tint[] = { 103.0f / 255.0f, 122.0f / 255.0f, 139.0f / 255.0f, 1.0f };

		bool modifiedSector = false;
		const s32 count = (s32)s_infEditor.item->classData.size();
		Editor_InfClass** dataList = s_infEditor.item->classData.data();
		for (s32 i = 0; i < count; i++)
		{
			Editor_InfClass* data = dataList[i];
			f32 propHeight = 0.0f, contentHeight = 0.0f;
			f32 childHeight = computeChildHeight(data, s_infEditor.curContentIndex, s_infEditor.curClassIndex == i, &propHeight, &contentHeight);

			if (ImGui::BeginChild(editor_getUniqueLabel(""), { 0, childHeight }, ImGuiChildFlags_Border))
			{
				// Class label.
				ImVec2 itemStart = ImGui::GetCursorPos();

				ImGui::TextColored(i == s_infEditor.curClassIndex ? colorKeywordOuterSel : colorKeywordOuter, "Class:");
				ImGui::SameLine(0.0f, 8.0f);

				switch (data->classId)
				{
					case IIC_ELEVATOR:
					{
						// Class name.
						selectableClassName("Elevator", itemStart, i);
						ImGui::SameLine(0.0f, 8.0f);

						// Class data.
						Editor_InfElevator* elev = getElevFromClassData(data);
						assert(elev);
						// Elevator Type.
						editor_infSelectElevType(elev);
												
						// Properties.
						editor_infEditElevProperties(elev, propHeight, i, tint);
						editor_infAddOrRemoveElevProperty(elev, i);
						ImGui::Separator();

						// Content - stops and slaves.
						if (!elev->stops.empty() || !elev->slaves.empty())
						{
							const s32 stopCount = (s32)elev->stops.size();
							const s32 slaveCount = (s32)elev->slaves.size();
							if (stopCount && slaveCount) { ImGui::Text("Stops: %d    Slaves: %d", stopCount, slaveCount); }
							else if (stopCount)  { ImGui::Text("Stops: %d", stopCount); }
							else if (slaveCount) { ImGui::Text("Slaves: %d", slaveCount); }
							ImGui::BeginChild(editor_getUniqueLabel(""), { 0.0f, contentHeight }, ImGuiChildFlags_Border);
							{
								// Edit Stops.
								editor_infEditElevStops(elev, contentHeight, i, tint);
								// Edit Slaves.
								editor_infEditElevSlaves(elev, contentHeight, i, tint);
							}
							ImGui::EndChild();
						}
						editor_infAddOrRemoveElevStopOrSlave(elev, i);

						// A stop is selected.
						if (!s_infEditor.showAnim)
						{
							if (s_infEditor.curClassIndex == i && s_infEditor.curContentIndex >= 0 && s_infEditor.curContentIndex < elev->stops.size())
							{
								modifySectorGeometry(s_infEditor.item, s_infEditor.sector, elev, s_infEditor.curContentIndex);
								modifiedSector = true;
							}
							else
							{
								modifySectorGeometry();
							}
						}

						// Time slider.
						ImGui::Text("Show Animation"); ImGui::SameLine(0.0f, 8.0f);
						bool animActive = s_infEditor.curClassIndex == i && s_infEditor.showAnim;
						if (ImGui::Checkbox(editor_getUniqueLabel(""), &animActive))
						{
							s_infEditor.curClassIndex = i;
							s_infEditor.playAnim = false;
							s_infEditor.showAnim = animActive;
							s_infEditor.time = 0.0f;
						}
						if (animActive)
						{
							if (elev->stops.empty() && elev->type != IET_DOOR && elev->type != IET_DOOR_INV && elev->type != IET_DOOR_MID && elev->type != IET_BASIC_AUTO)
							{
								const char* tooltip = s_infEditor.playAnim ? "Stop the elevator animation and reset." : "Play the animation continuously.";
								const IconId icon = s_infEditor.playAnim ? ICON_STOP : ICON_PLAY;

								ImGui::SameLine(0.0f, 16.0f);
								if (iconButtonInline(icon, tooltip, nullptr, true))
								{
									s_infEditor.playAnim = !s_infEditor.playAnim;
									s_infEditor.time = 0.0f;
								}
								else if (s_infEditor.playAnim)
								{
									s_infEditor.time += (f32)TFE_System::getDeltaTime();
								}
							}

							ImGui::SameLine(0.0f, 16.0f);
							ImGui::SetNextItemWidth(256.0f);
							ImGui::SliderFloat(editor_getUniqueLabel(""), &s_infEditor.time, 0.0f, 1.0f);
							modifySectorGeometryTime(s_infEditor.item, s_infEditor.sector, elev, s_infEditor.time);
							s_infEditor.curClassIndex = i;
							modifiedSector = true;
						}
					} break;
					case IIC_TRIGGER:
					{
						// Class name.
						selectableClassName("Trigger", itemStart, i);

						// Class data.
						Editor_InfTrigger* trigger = getTriggerFromClassData(data);
						assert(trigger);
						
						// Type selection.
						if (s_infEditor.itemWallIndex >= 0)
						{
							ImGui::SameLine(0.0f, 8.0f);
							editor_infSelectTriggerType(trigger);
						}

						// Properties.
						editor_infEditTriggerProperties(trigger, propHeight, i, tint);
						editor_infAddOrRemoveTriggerProperty(trigger, i);
						ImGui::Separator();

						// Content - clients.
						if (!trigger->clients.empty())
						{
							const s32 clientCount = (s32)trigger->clients.size();
							ImGui::Text("Clients: %d", clientCount);
							ImGui::BeginChild(editor_getUniqueLabel(""), { 0.0f, contentHeight }, ImGuiChildFlags_Border);
							{
								editor_infEditTriggerClients(trigger, contentHeight, i, tint);
							}
							ImGui::EndChild();
						}
						else
						{
							ImGui::Text("Clients");
						}
						editor_infAddOrRemoveTriggerClient(trigger, i);
					} break;
					case IIC_TELEPORTER:
					{
						// Class name.
						selectableClassName("Teleporter", itemStart, i);

						// Class data.
						Editor_InfTeleporter* teleporter = getTeleporterFromClassData(data);
						assert(teleporter);

						// Type
						ImGui::SameLine(0.0f, 8.0f);
						editor_infSelectTelelporterType(teleporter);

						// Target
						ImGui::Text("Target"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::SetNextItemWidth(128.0f);
						char nameBuffer[256];
						strcpy(nameBuffer, teleporter->target.c_str());
						if (ImGui::InputText(editor_getUniqueLabel(""), nameBuffer, 256))
						{
							teleporter->target = nameBuffer;
						}
						ImGui::SameLine(0.0f, 8.0f);
						if (iconButtonInline(ICON_SELECT, "Select the target sector in the viewport.", tint, true))
						{
							editor_selectViewportFeature((Editor_InfClass*)teleporter, SELECTMODE_SECTOR, SELTYPE_TELEPORT_TARGET);
						}

						if (teleporter->type == TELEPORT_BASIC)
						{
							ImGui::Text("Move"); ImGui::SameLine(0.0f, 8.0f);
							// Position
							ImGui::SetNextItemWidth(128.0f * 3.0f);
							ImGui::InputFloat3(editor_getUniqueLabel(""), &teleporter->dstPos.x);
							ImGui::SameLine(0.0f, 8.0f);
							if (iconButtonInline(ICON_SELECT, "Select target position in the viewport.", tint, true))
							{
								editor_selectViewportFeature((Editor_InfClass*)teleporter, SELECTMODE_POSITION, SELTYPE_TELEPORT_POSITION);
							}

							// Angle
							ImGui::SameLine(0.0f, 16.0f);
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
							ImGui::Text("Angle"); ImGui::SameLine(0.0f, 8.0f);
							f32 angleRad = teleporter->dstAngle * PI / 180.0f;
							ImGui::SetNextItemWidth(128.0f);
							if (ImGui::SliderAngle(editor_getUniqueLabel(""), &angleRad))
							{
								teleporter->dstAngle = angleRad * 180.0f / PI;
							}

							ImGui::SameLine(0.0f, 8.0f);
							ImGui::SetNextItemWidth(128.0f);
							ImGui::InputFloat(editor_getUniqueLabel(""), &teleporter->dstAngle, 0.1f, 1.0f, "%.3f", ImGuiInputTextFlags_CharsDecimal);
						}
					} break;
				}
			}
			ImGui::EndChild();
		}

		if (deleteIndex >= 0 && deleteIndex < count)
		{
			switch (s_infEditor.item->classData[deleteIndex]->classId)
			{
				case IIC_ELEVATOR:
				{
					Editor_InfElevator* elev = getElevFromClassData(s_infEditor.item->classData[deleteIndex]);
					assert(elev);
					freeElevator(elev);
				} break;
				case IIC_TRIGGER:
				{
					Editor_InfTrigger* trigger = getTriggerFromClassData(s_infEditor.item->classData[deleteIndex]);
					assert(trigger);
					freeTrigger(trigger);
				} break;
				case IIC_TELEPORTER:
				{
					Editor_InfTeleporter* teleporter = getTeleporterFromClassData(s_infEditor.item->classData[deleteIndex]);
					assert(teleporter);
					freeTeleporter(teleporter);
				} break;
			}

			for (s32 i = deleteIndex; i < count - 1; i++)
			{
				s_infEditor.item->classData[i] = s_infEditor.item->classData[i + 1];
			}
			s_infEditor.item->classData.pop_back();
			
		}

		editor_handleOverlayList();

		if (!modifiedSector)
		{
			modifySectorGeometry();
		}
	}

	char s_floatToStrBuffer[256];
	char s_floatToStrBuffer1[256];
	const char* infFloatToString(f32 value, s32 index = 0);

	const char* infFloatToString(f32 value, s32 index)
	{
		char* outStr = index == 0 ? s_floatToStrBuffer : s_floatToStrBuffer1;
		const f32 eps = 0.0001f;
		// Integer value.
		if (fabsf(floorf(value) - value) < eps)
		{
			sprintf(outStr, "%d", s32(value));
		}
		// Is it a single digit?
		else if (fabsf(floorf(value * 10.0f) * 0.1f - value) < eps)
		{
			sprintf(outStr, "%0.1f", value);
		}
		// Two digits.
		else if (fabsf(floorf(value * 100.0f) * 0.01f - value) < eps)
		{
			sprintf(outStr, "%0.2f", value);
		}
		// Three digits.
		else
		{
			sprintf(outStr, "%0.3f", value);
		}
		return outStr;
	}

	void appendToBuffer(std::string& outStr, const char* str)
	{
		outStr += str;
		outStr += "\r\n";
	}

	void editor_writeInfItem(std::string& outStr, const Editor_InfItem* item, const char* curTab)
	{
		const char* tab = "    ";
		const s32 count = (s32)item->classData.size();
		const Editor_InfClass* const* dataList = item->classData.data();
		char buffer[256];
		for (s32 i = 0; i < count; i++)
		{
			const Editor_InfClass* data = dataList[i];
			switch (data->classId)
			{
				case IIC_ELEVATOR:
				{
					const Editor_InfElevator* elev = getElevFromClassData(data);

					// Class
					sprintf(buffer, "%sClass: Elevator %s", curTab, c_infElevTypeName[elev->type]);
					appendToBuffer(outStr, buffer);

					// Properties.
					const u32 overrides = elev->overrideSet;
					if (overrides & IEO_START)
					{
						sprintf(buffer, "%s%sStart: %d", curTab, tab, elev->start);
						appendToBuffer(outStr, buffer);
					}
					if (overrides & IEO_SPEED)
					{
						sprintf(buffer, "%s%sSpeed: %s", curTab, tab, infFloatToString(elev->speed));
						appendToBuffer(outStr, buffer);
					}
					if ((overrides & IEO_MASTER) && !elev->master)
					{
						sprintf(buffer, "%s%sMaster: Off", curTab, tab);
						appendToBuffer(outStr, buffer);
					}
					if (overrides & IEO_ANGLE)
					{
						f32 angle = elev->angle * 180.0f / PI;
						sprintf(buffer, "%s%sAngle: %s", curTab, tab, infFloatToString(angle));
						appendToBuffer(outStr, buffer);
					}
					if (overrides & IEO_FLAGS)
					{
						sprintf(buffer, "%s%sFlags: %d", curTab, tab, elev->flags);
						appendToBuffer(outStr, buffer);
					}
					if (overrides & IEO_KEY0)
					{
						if (elev->type == IET_DOOR_MID)
						{
							sprintf(buffer, "%s%sAddon: 0", curTab, tab);
							appendToBuffer(outStr, buffer);
						}
						sprintf(buffer, "%s%sKey: %s", curTab, tab, c_infKeyNames[elev->key[0] - KeyItem::KEY_RED]);
						appendToBuffer(outStr, buffer);
					}
					if (overrides & IEO_KEY1)
					{
						if (elev->type == IET_DOOR_MID)
						{
							sprintf(buffer, "%s%sAddon: 1", curTab, tab);
							appendToBuffer(outStr, buffer);

							sprintf(buffer, "%s%sKey: %s", curTab, tab, c_infKeyNames[elev->key[1] - KeyItem::KEY_RED]);
							appendToBuffer(outStr, buffer);
						}
					}
					if (overrides & IEO_CENTER)
					{
						sprintf(buffer, "%s%sCenter: %s %s", curTab, tab, infFloatToString(elev->center.x), infFloatToString(elev->center.z, 1));
						appendToBuffer(outStr, buffer);
					}
					if (overrides & IEO_SOUND0)
					{
						char soundName[256];
						if (elev->sounds[0].empty() || strcasecmp(elev->sounds[0].c_str(), "none") == 0)
						{
							strcpy(soundName, "0");
						}
						else
						{
							strcpy(soundName, elev->sounds[0].c_str());
						}

						sprintf(buffer, "%s%sSound: %d %s", curTab, tab, 1, soundName);
						appendToBuffer(outStr, buffer);
					}
					if (overrides & IEO_SOUND1)
					{
						char soundName[256];
						if (elev->sounds[1].empty() || strcasecmp(elev->sounds[1].c_str(), "none") == 0)
						{
							strcpy(soundName, "0");
						}
						else
						{
							strcpy(soundName, elev->sounds[1].c_str());
						}

						sprintf(buffer, "%s%sSound: %d %s", curTab, tab, 2, soundName);
						appendToBuffer(outStr, buffer);
					}
					if (overrides & IEO_SOUND2)
					{
						char soundName[256];
						if (elev->sounds[2].empty() || strcasecmp(elev->sounds[2].c_str(), "none") == 0)
						{
							strcpy(soundName, "0");
						}
						else
						{
							strcpy(soundName, elev->sounds[2].c_str());
						}

						sprintf(buffer, "%s%sSound: %d %s", curTab, tab, 3, soundName);
						appendToBuffer(outStr, buffer);
					}
					if (overrides & IEO_EVENT_MASK)
					{
						if (elev->eventMask == -1)
						{
							sprintf(buffer, "%s%sEvent_Mask: *", curTab, tab);
						}
						else
						{
							sprintf(buffer, "%s%sEvent_Mask: %d", curTab, tab, elev->eventMask);
						}
						appendToBuffer(outStr, buffer);
					}
					if (overrides & IEO_ENTITY_MASK)
					{
						if (elev->entityMask == -1)
						{
							sprintf(buffer, "%s%sEntity_Mask: *", curTab, tab);
						}
						else
						{
							sprintf(buffer, "%s%sEntity_Mask: %d", curTab, tab, elev->entityMask);
						}
						appendToBuffer(outStr, buffer);
					}

					const s32 stopCount = (s32)elev->stops.size();
					const Editor_InfStop* stop = elev->stops.data();
					for (s32 s = 0; s < stopCount; s++, stop++)
					{
						char value[256];
						if (!stop->fromSectorFloor.empty())
						{
							strcpy(value, stop->fromSectorFloor.c_str());
						}
						else if (stop->relative)
						{
							sprintf(value, "@%s", infFloatToString(stop->value));
						}
						else
						{
							sprintf(value, "%s", infFloatToString(stop->value));
						}

						char delay[256];
						if (stop->delayType == SDELAY_SECONDS)
						{
							sprintf(delay, "%s", infFloatToString(stop->delay));
						}
						else if (stop->delayType == SDELAY_HOLD)
						{
							sprintf(delay, "%s", "hold");
						}
						else if (stop->delayType == SDELAY_COMPLETE)
						{
							sprintf(delay, "%s", "complete");
						}
						else if (stop->delayType == SDELAY_TERMINATE)
						{
							sprintf(delay, "%s", "terminate");
						}
						else
						{
							sprintf(delay, "%s", "prev");
						}

						sprintf(buffer, "%s%sStop:%s%s %s", curTab, tab, tab, value, delay);
						appendToBuffer(outStr, buffer);

						// Stop commands.
						const s32 msgCount = (s32)stop->msg.size();
						const Editor_InfMessage* msg = stop->msg.data();
						for (s32 m = 0; m < msgCount; m++, msg++)
						{
							char target[256];
							if (msg->targetWall >= 0)
							{
								sprintf(target, "%s(%d)", msg->targetSector.c_str(), msg->targetWall); ImGui::SameLine(0.0f, 8.0f);
							}
							else
							{
								sprintf(target, "%s", msg->targetSector.c_str()); ImGui::SameLine(0.0f, 8.0f);
							}

							char typeArgs[256] = "";
							if (msg->type == IMT_GOTO_STOP || msg->type == IMT_COMPLETE)
							{
								sprintf(typeArgs, "%u", msg->arg[0]);
							}
							else if (msg->type == IMT_SET_BITS || msg->type == IMT_CLEAR_BITS)
							{
								sprintf(typeArgs, "%u %u", msg->arg[0], msg->arg[1]);
							}

							sprintf(buffer, "%s%s%sMessage: %d %s %s %s", curTab, tab, tab, s, target, c_editorInfMessageTypeName[msg->type], typeArgs);
							appendToBuffer(outStr, buffer);
						}

						const s32 scriptCallCount = (s32)stop->scriptCall.size();
						const Editor_ScriptCall* call = stop->scriptCall.data();
						for (s32 c = 0; c < scriptCallCount; c++, call++)
						{
							char argList[4096] = "";
							for (s32 a = 0; a < 4; a++)
							{
								if (call->arg[a].value.empty() || call->arg[a].value == "") { break; }
								strcat(argList, call->arg[a].value.c_str());
								strcat(argList, " ");
							}

							sprintf(buffer, "%s%s%sScriptCall: %d %s %s", curTab, tab, tab, s, call->funcName.c_str(), argList);
							appendToBuffer(outStr, buffer);
						}

						const s32 adjoinCount = (s32)stop->adjoinCmd.size();
						const Editor_InfAdjoinCmd* adjoinCmd = stop->adjoinCmd.data();
						for (s32 a = 0; a < adjoinCount; a++, adjoinCmd++)
						{
							sprintf(buffer, "%s%s%sAdjoin: %d %s %d %s %d", curTab, tab, tab, s,
								adjoinCmd->sector0.c_str(), adjoinCmd->wallIndex0, adjoinCmd->sector1.c_str(), adjoinCmd->wallIndex1);
							appendToBuffer(outStr, buffer);
						}

						const s32 texCount = (s32)stop->textureCmd.size();
						const Editor_InfTextureCmd* texCmd = stop->textureCmd.data();
						for (s32 t = 0; t < texCount; t++, texCmd++)
						{
							sprintf(buffer, "%s%s%sTexture: %d %s %s", curTab, tab, tab, s, texCmd->fromCeiling ? "C" : "F", texCmd->donorSector.c_str());
							appendToBuffer(outStr, buffer);
						}

						if (stop->overrideSet & ISO_PAGE)
						{
							sprintf(buffer, "%s%s%sPage: %d %s", curTab, tab, tab, s, stop->page.c_str());
							appendToBuffer(outStr, buffer);
						}
					}

					const s32 slaveCount = (s32)elev->slaves.size();
					const Editor_InfSlave* slave = elev->slaves.data();
					for (s32 s = 0; s < slaveCount; s++, slave++)
					{
						if (slave->angleOffset != 0.0f)
						{
							sprintf(buffer, "%s%sSlave: %s", curTab, tab, slave->name.c_str());
						}
						else
						{
							sprintf(buffer, "%s%sSlave: %s %s", curTab, tab, slave->name.c_str(), infFloatToString(slave->angleOffset));
						}
						appendToBuffer(outStr, buffer);
					}
				} break;
				case IIC_TRIGGER:
				{
					const Editor_InfTrigger* trigger = getTriggerFromClassData(data);

					// Class
					if (trigger->type != ITRIGGER_SECTOR)
					{
						sprintf(buffer, "%sClass: Trigger %s", curTab, c_infTriggerTypeName[trigger->type]);
					}
					else
					{
						sprintf(buffer, "%sClass: Trigger", curTab);
					}
					appendToBuffer(outStr, buffer);

					const u32 overrides = trigger->overrideSet;
					if ((overrides & ITO_SOUND) && trigger->type != ITRIGGER_SECTOR)
					{
						sprintf(buffer, "%s%sSound: %s", curTab, tab, trigger->sound.c_str());
						appendToBuffer(outStr, buffer);
					}
					if ((overrides & ITO_MASTER) && !trigger->master)
					{
						sprintf(buffer, "%s%sMaster: Off", curTab, tab);
						appendToBuffer(outStr, buffer);
					}
					if (overrides & ITO_TEXT)
					{
						sprintf(buffer, "%s%sText: %u", curTab, tab, trigger->textId);
						appendToBuffer(outStr, buffer);
					}
					if (overrides & ITO_EVENT_MASK)
					{
						if (trigger->eventMask == -1)
						{
							sprintf(buffer, "%s%sEvent_Mask: *", curTab, tab);
						}
						else
						{
							sprintf(buffer, "%s%sEvent_Mask: %d", curTab, tab, trigger->eventMask);
						}
						appendToBuffer(outStr, buffer);
					}
					if (overrides & ITO_ENTITY_MASK)
					{
						if (trigger->entityMask == -1)
						{
							sprintf(buffer, "%s%sEntity_Mask: *", curTab, tab);
						}
						else
						{
							sprintf(buffer, "%s%sEntity_Mask: %d", curTab, tab, trigger->entityMask);
						}
						appendToBuffer(outStr, buffer);
					}
					if (overrides & ITO_EVENT)
					{
						sprintf(buffer, "%s%sEvent: %u", curTab, tab, trigger->event);
						appendToBuffer(outStr, buffer);
					}
					if (overrides & ITO_MSG)
					{
						if (trigger->cmd == IMT_GOTO_STOP || trigger->cmd == IMT_COMPLETE || trigger->cmd == IMT_SET_BITS || trigger->cmd == IMT_CLEAR_BITS)
						{
							char args[256] = "";
							if (trigger->cmd == IMT_GOTO_STOP || trigger->cmd == IMT_COMPLETE)
							{
								sprintf(args, "%u", trigger->arg[0]);
							}
							else if (trigger->cmd == IMT_SET_BITS || trigger->cmd == IMT_CLEAR_BITS)
							{
								sprintf(args, "%u %u", trigger->arg[0], trigger->arg[1]);
							}
							sprintf(buffer, "%s%sMessage: %s %s", curTab, tab, c_editorInfMessageTypeName[trigger->cmd], args);
						}
						else
						{
							sprintf(buffer, "%s%sMessage: %s", curTab, tab, c_editorInfMessageTypeName[trigger->cmd]);
						}
						appendToBuffer(outStr, buffer);
					}
					const s32 clientCount = (s32)trigger->clients.size();
					const Editor_InfClient* client = trigger->clients.data();
					for (s32 c = 0; c < clientCount; c++, client++)
					{
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordInner, "Client:"); ImGui::SameLine(0.0f, 8.0f);
						char target[256];
						if (client->targetWall >= 0)
						{
							sprintf(target, "%s(%d)", client->targetSector.c_str(), client->targetWall);
						}
						else
						{
							sprintf(target, "%s", client->targetSector.c_str());
						}

						if (client->eventMask != INF_EVENT_ANY)
						{
							sprintf(buffer, "%s%sClient: %s %u", curTab, tab, target, client->eventMask);
						}
						else
						{
							sprintf(buffer, "%s%sClient: %s", curTab, tab, target);
						}
						appendToBuffer(outStr, buffer);
					}
				} break;
				case IIC_TELEPORTER:
				{
					const Editor_InfTeleporter* teleporter = getTeleporterFromClassData(data);

					// Class
					sprintf(buffer, "%sClass: Teleporter %s", curTab, c_infTeleporterTypeName[teleporter->type]);
					appendToBuffer(outStr, buffer);

					// Target
					sprintf(buffer, "%s%sTarget: %s", curTab, tab, teleporter->target.c_str());
					appendToBuffer(outStr, buffer);

					// Position/angle.
					if (teleporter->type == TELEPORT_BASIC)
					{
						char posX[256], posY[256], posZ[256], angle[256];
						strcpy(posX, infFloatToString(teleporter->dstPos.x));
						strcpy(posY, infFloatToString(teleporter->dstPos.y));
						strcpy(posZ, infFloatToString(teleporter->dstPos.z));
						strcpy(angle, infFloatToString(teleporter->dstAngle));

						sprintf(buffer, "%s%sMove: %s %s %s %s", curTab, tab, posX, posY, posZ, angle);
						appendToBuffer(outStr, buffer);
					}
				} break;
			}
		}
	}
		
	void editor_InfEdit_Code()
	{
		modifySectorGeometry();

		const char* tab = "    ";
		const s32 count = (s32)s_infEditor.item->classData.size();
		Editor_InfClass** dataList = s_infEditor.item->classData.data();
		for (s32 i = 0; i < count; i++)
		{
			if (i > 0)
			{
				ImGui::NewLine();
			}

			Editor_InfClass* data = dataList[i];
			switch (data->classId)
			{
				case IIC_ELEVATOR:
				{
					Editor_InfElevator* elev = getElevFromClassData(data);

					// Class
					ImGui::TextColored(colorKeywordOuter, "Class:"); ImGui::SameLine(0.0f, 8.0f);
					ImGui::TextColored(colorKeywordInner, "Elevator"); ImGui::SameLine(0.0f, 8.0f);
					ImGui::Text("%s", c_infElevTypeName[elev->type]);

					// Properties.
					const u32 overrides = elev->overrideSet;
					if (overrides & IEO_START)
					{
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordInner, "Start:"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::Text("%d", elev->start);
					}
					if (overrides & IEO_SPEED)
					{
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordInner, "Speed:"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::Text("%s", infFloatToString(elev->speed));
					}
					if ((overrides & IEO_MASTER) && !elev->master)
					{
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordInner, "Master:"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::Text("Off");
					}
					if (overrides & IEO_ANGLE)
					{
						f32 angle = elev->angle * 180.0f / PI;
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordInner, "Angle:"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::Text("%s", infFloatToString(angle));
					}
					if (overrides & IEO_FLAGS)
					{
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordInner, "Flags:"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::Text("%d", elev->flags);
					}
					if (overrides & IEO_KEY0)
					{
						if (elev->type == IET_DOOR_MID)
						{
							ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
							ImGui::TextColored(colorKeywordInner, "Addon:"); ImGui::SameLine(0.0f, 8.0f);
							ImGui::Text("%d", 0);
						}
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordInner, "Key:"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::Text("%s", c_infKeyNames[elev->key[0] - KeyItem::KEY_RED]);
					}
					if (overrides & IEO_KEY1)
					{
						if (elev->type == IET_DOOR_MID)
						{
							ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
							ImGui::TextColored(colorKeywordInner, "Addon:"); ImGui::SameLine(0.0f, 8.0f);
							ImGui::Text("%d", 1);
							ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
							ImGui::TextColored(colorKeywordInner, "Key:"); ImGui::SameLine(0.0f, 8.0f);
							ImGui::Text("%s", c_infKeyNames[elev->key[1] - KeyItem::KEY_RED]);
						}
					}
					if (overrides & IEO_CENTER)
					{
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordInner, "Center:"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::Text("%s %s", infFloatToString(elev->center.x), infFloatToString(elev->center.z, 1));
					}
					if (overrides & IEO_SOUND0)
					{
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordInner, "Sound:"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::Text("%d %s", 1, elev->sounds[0].c_str());
					}
					if (overrides & IEO_SOUND1)
					{
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordInner, "Sound:"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::Text("%d %s", 2, elev->sounds[1].c_str());
					}
					if (overrides & IEO_SOUND2)
					{
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordInner, "Sound:"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::Text("%d %s", 3, elev->sounds[2].c_str());
					}
					if (overrides & IEO_EVENT_MASK)
					{
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordInner, "Event_Mask:"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::Text("%d", elev->eventMask);
					}
					if (overrides & IEO_ENTITY_MASK)
					{
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordInner, "Entity_Mask:"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::Text("%d", elev->entityMask);
					}

					const s32 stopCount = (s32)elev->stops.size();
					const Editor_InfStop* stop = elev->stops.data();
					for (s32 s = 0; s < stopCount; s++, stop++)
					{
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordOuter, "Stop:"); ImGui::SameLine(0.0f, 8.0f);
						if (!stop->fromSectorFloor.empty())
						{
							ImGui::Text("%s", stop->fromSectorFloor.c_str()); ImGui::SameLine(0.0f, 8.0f);
						}
						else if (stop->relative)
						{
							ImGui::Text("@%s", infFloatToString(stop->value)); ImGui::SameLine(0.0f, 8.0f);
						}
						else
						{
							ImGui::Text("%s", infFloatToString(stop->value)); ImGui::SameLine(0.0f, 8.0f);
						}

						if (stop->delayType == SDELAY_SECONDS)
						{
							ImGui::Text("%s", infFloatToString(stop->delay));
						}
						else if (stop->delayType == SDELAY_HOLD)
						{
							ImGui::Text("%s", "hold");
						}
						else if (stop->delayType == SDELAY_COMPLETE)
						{
							ImGui::Text("%s", "complete");
						}
						else if (stop->delayType == SDELAY_TERMINATE)
						{
							ImGui::Text("%s", "terminate");
						}
						else
						{
							ImGui::Text("%s", "prev");
						}

						// Stop commands.
						const s32 msgCount = (s32)stop->msg.size();
						const Editor_InfMessage* msg = stop->msg.data();
						for (s32 m = 0; m < msgCount; m++, msg++)
						{
							ImGui::Text("%s%s", tab, tab); ImGui::SameLine(0.0f, 0.0f);
							ImGui::TextColored(colorKeywordInner, "Message:"); ImGui::SameLine(0.0f, 8.0f);
							ImGui::Text("%d", s); ImGui::SameLine(0.0f, 8.0f);
							if (msg->targetWall >= 0)
							{
								ImGui::Text("%s(%d)", msg->targetSector.c_str(), msg->targetWall); ImGui::SameLine(0.0f, 8.0f);
							}
							else
							{
								ImGui::Text("%s", msg->targetSector.c_str()); ImGui::SameLine(0.0f, 8.0f);
							}
							ImGui::Text("%s", c_editorInfMessageTypeName[msg->type]);

							if (msg->type == IMT_GOTO_STOP || msg->type == IMT_COMPLETE)
							{
								ImGui::SameLine(0.0f, 8.0f);
								ImGui::Text("%u", msg->arg[0]);
							}
							else if (msg->type == IMT_SET_BITS || msg->type == IMT_CLEAR_BITS)
							{
								ImGui::SameLine(0.0f, 8.0f);
								ImGui::Text("%u %u", msg->arg[0], msg->arg[1]);
							}
						}

						const s32 adjoinCount = (s32)stop->adjoinCmd.size();
						const Editor_InfAdjoinCmd* adjoinCmd = stop->adjoinCmd.data();
						for (s32 a = 0; a < adjoinCount; a++, adjoinCmd++)
						{
							ImGui::Text("%s%s", tab, tab); ImGui::SameLine(0.0f, 0.0f);
							ImGui::TextColored(colorKeywordInner, "Adjoin:"); ImGui::SameLine(0.0f, 8.0f);
							ImGui::Text("%d", s); ImGui::SameLine(0.0f, 8.0f);
							ImGui::Text("%s %d %s %d", adjoinCmd->sector0.c_str(), adjoinCmd->wallIndex0, adjoinCmd->sector1.c_str(), adjoinCmd->wallIndex1);
						}

						const s32 texCount = (s32)stop->textureCmd.size();
						const Editor_InfTextureCmd* texCmd = stop->textureCmd.data();
						for (s32 t = 0; t < texCount; t++, texCmd++)
						{
							ImGui::Text("%s%s", tab, tab); ImGui::SameLine(0.0f, 0.0f);
							ImGui::TextColored(colorKeywordInner, "Texture:"); ImGui::SameLine(0.0f, 8.0f);
							ImGui::Text("%d", s); ImGui::SameLine(0.0f, 8.0f);
							ImGui::Text("%s %s", texCmd->fromCeiling ? "C" : "F", texCmd->donorSector.c_str());
						}

						if (stop->overrideSet & ISO_PAGE)
						{
							ImGui::Text("%s%s", tab, tab); ImGui::SameLine(0.0f, 0.0f);
							ImGui::TextColored(colorKeywordInner, "Page:"); ImGui::SameLine(0.0f, 8.0f);
							ImGui::Text("%d", s); ImGui::SameLine(0.0f, 8.0f);
							ImGui::Text("%s", stop->page.c_str());
						}

						const s32 scriptCallCount = (s32)stop->scriptCall.size();
						const Editor_ScriptCall* call = stop->scriptCall.data();
						for (s32 c = 0; c < scriptCallCount; c++, call++)
						{
							ImGui::Text("%s%s", tab, tab); ImGui::SameLine(0.0f, 0.0f);
							ImGui::TextColored(colorKeywordInner, "ScriptCall:"); ImGui::SameLine(0.0f, 8.0f);
							ImGui::Text("%d", s); ImGui::SameLine(0.0f, 8.0f);

							ImGui::Text("%s", call->funcName.c_str()); ImGui::SameLine(0.0f, 8.0f);
							for (s32 a = 0; a < 4; a++)
							{
								if (call->arg[a].value == "") { break; }
								ImGui::TextColored(colorKeywordArg, "%s", call->arg[a].value.c_str()); ImGui::SameLine(0.0f, 8.0f);
							}
						}
					}

					const s32 slaveCount = (s32)elev->slaves.size();
					const Editor_InfSlave* slave = elev->slaves.data();
					for (s32 s = 0; s < slaveCount; s++, slave++)
					{
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordOuter, "Slave:"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::Text("%s", slave->name.c_str());
						if (slave->angleOffset != 0.0f)
						{
							ImGui::SameLine(0.0f, 8.0f);
							ImGui::Text("%s", infFloatToString(slave->angleOffset));
						}
					}
				} break;
				case IIC_TRIGGER:
				{
					Editor_InfTrigger* trigger = getTriggerFromClassData(data);

					// Class
					ImGui::TextColored(colorKeywordOuter, "Class:"); ImGui::SameLine(0.0f, 8.0f);
					ImGui::TextColored(colorKeywordInner, "Trigger");
					if (trigger->type != ITRIGGER_SECTOR)
					{
						ImGui::SameLine(0.0f, 8.0f);
						ImGui::Text("%s", c_infTriggerTypeName[trigger->type]);
					}

					const u32 overrides = trigger->overrideSet;
					if ((overrides & ITO_SOUND) && trigger->type != ITRIGGER_SECTOR)
					{
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordInner, "Sound:"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::Text("%s", trigger->sound.c_str());
					}
					if ((overrides & ITO_MASTER) && !trigger->master)
					{
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordInner, "Master:"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::Text("Off");
					}
					if (overrides & ITO_TEXT)
					{
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordInner, "Text:"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::Text("%u", trigger->textId);
					}
					if (overrides & ITO_EVENT_MASK)
					{
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordInner, "Event_Mask:"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::Text("%d", trigger->eventMask);
					}
					if (overrides & ITO_ENTITY_MASK)
					{
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordInner, "Entity_Mask:"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::Text("%d", trigger->entityMask);
					}
					if (overrides & ITO_EVENT)
					{
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordInner, "Event:"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::Text("%u", trigger->event);
					}
					if (overrides & ITO_MSG)
					{
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordInner, "Message:"); ImGui::SameLine(0.0f, 8.0f);
						ImGui::Text("%s", c_editorInfMessageTypeName[trigger->cmd]);

						if (trigger->cmd == IMT_GOTO_STOP || trigger->cmd == IMT_COMPLETE)
						{
							ImGui::SameLine(0.0f, 8.0f);
							ImGui::Text("%u", trigger->arg[0]);
						}
						else if (trigger->cmd == IMT_SET_BITS || trigger->cmd == IMT_CLEAR_BITS)
						{
							ImGui::SameLine(0.0f, 8.0f);
							ImGui::Text("%u %u", trigger->arg[0], trigger->arg[1]);
						}
					}
					const s32 clientCount = (s32)trigger->clients.size();
					const Editor_InfClient* client = trigger->clients.data();
					for (s32 c = 0; c < clientCount; c++, client++)
					{
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordInner, "Client:"); ImGui::SameLine(0.0f, 8.0f);
						if (client->targetWall >= 0)
						{
							ImGui::Text("%s(%d)", client->targetSector.c_str(), client->targetWall);
						}
						else
						{
							ImGui::Text("%s", client->targetSector.c_str());
						}
						if (client->eventMask != INF_EVENT_ANY)
						{
							ImGui::SameLine(0.0f, 8.0f);
							ImGui::Text("%u", client->eventMask);
						}
					}
				} break;
				case IIC_TELEPORTER:
				{
					Editor_InfTeleporter* teleporter = getTeleporterFromClassData(data);

					// Class
					ImGui::TextColored(colorKeywordOuter, "Class:"); ImGui::SameLine(0.0f, 8.0f);
					ImGui::TextColored(colorKeywordInner, "Teleporter"); ImGui::SameLine(0.0f, 8.0f);
					ImGui::Text("%s", c_infTeleporterTypeName[teleporter->type]);

					// Target
					ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
					ImGui::TextColored(colorKeywordInner, "Target:"); ImGui::SameLine(0.0f, 8.0f);
					ImGui::Text("%s", teleporter->target.c_str());

					// Position/angle.
					if (teleporter->type == TELEPORT_BASIC)
					{
						ImGui::Text("%s", tab); ImGui::SameLine(0.0f, 0.0f);
						ImGui::TextColored(colorKeywordInner, "Move:"); ImGui::SameLine(0.0f, 8.0f);

						char posX[256], posY[256], posZ[256], angle[256];
						strcpy(posX, infFloatToString(teleporter->dstPos.x));
						strcpy(posY, infFloatToString(teleporter->dstPos.y));
						strcpy(posZ, infFloatToString(teleporter->dstPos.z));
						strcpy(angle, infFloatToString(teleporter->dstAngle));
						ImGui::Text("%s %s %s %s", posX, posY, posZ, angle);
					}
				} break;
			}
		}
	}
		
	bool editor_infEdit()
	{
		DisplayInfo info;
		TFE_RenderBackend::getDisplayInfo(&info);
		const f32 winWidth  = min(940.0f, (f32)info.width - 16);
		const f32 winHeight = (f32)info.height - 16;
		s32 deleteIndex = -1;
				
		pushFont(TFE_Editor::FONT_SMALL);

		bool active = true;
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoResize;
		ImGui::SetNextWindowSize({ winWidth, winHeight });
		if (s_restorePos > 0)
		{
			// Restore the popup position, otherwise it will move back to the default after being hidden.
			ImGui::SetNextWindowPos(s_popupPos);
		}

		// Reduce the white-out effect for the INF editor since the background visuals can be important.
		ImGui::PushStyleColor(ImGuiCol_NavWindowingDimBg, { 0.80f, 0.80f, 0.80f, 0.10f });
		ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, { 0.80f, 0.80f, 0.80f, 0.15f });

		bool wasPopupOpen = ImGui::IsPopupOpen("Edit INF");
		if (ImGui::BeginPopupModal("Edit INF", &active, window_flags))
		{
			// Popups need multiple frames to "accept" the new position due to the way the imGui window position logic works.
			if (s_restorePos > 0) { s_restorePos--; }
			else { s_popupPos = ImGui::GetWindowPos(); }

			if (!s_infEditor.sector)
			{
				ImGui::TextColored({ 1.0f, 0.2f, 0.2f, 1.0f }, "Sectors with INF functionality must have names.\nPlease name the sector and try again.");
			}
			else if (!s_infEditor.item)
			{
				if (ImGui::Button("Create INF Item"))
				{
					s_levelInf.item.push_back({});
					s_infEditor.item = &s_levelInf.item.back();
					s_infEditor.item->name = s_infEditor.sector->name;
					s_infEditor.item->wallNum = s_infEditor.itemWallIndex;
				}
			}
			else if (s_infEditor.item)
			{
				// Display it for now.
				if (s_infEditor.itemWallIndex < 0)
				{
					ImGui::Text("Item: %s, Sector ID: %d", s_infEditor.item->name.c_str(), s_infEditor.sector->id);
					ImGui::Separator();

					if (editor_button("+"))
					{
						if (s_infEditor.comboClassIndex == IIC_ELEVATOR)
						{
							allocElev(s_infEditor.item);
						}
						else if (s_infEditor.comboClassIndex == IIC_TRIGGER)
						{
							Editor_InfTrigger* trigger = allocTrigger(s_infEditor.item);
							trigger->overrideSet = ITO_NONE;
							trigger->type = ITRIGGER_SECTOR;
						}
						else if (s_infEditor.comboClassIndex == IIC_TELEPORTER)
						{
							allocTeleporter(s_infEditor.item);
						}
					}
					ImGui::SameLine(0.0f, 4.0f);
					if (editor_button("-"))
					{
						deleteIndex = s_infEditor.curClassIndex;
					}
					ImGui::SameLine(0.0f, 16.0f);
					ImGui::SetNextItemWidth(128.0f);
					if (ImGui::BeginCombo("##SectorClassCombo", c_infClassName[s_infEditor.comboClassIndex]))
					{
						s32 count = (s32)TFE_ARRAYSIZE(c_infClassName);
						for (s32 i = 0; i < count; i++)
						{
							if (ImGui::Selectable(c_infClassName[i], i == s_infEditor.comboClassIndex))
							{
								s_infEditor.comboClassIndex = i;
							}
							//setTooltip(c_infClassName[i].tooltip.c_str());
						}
						ImGui::EndCombo();
					}
				}
				else
				{
					ImGui::Text("Item: %s, Sector ID: %d, Wall ID: %d", s_infEditor.item->name.c_str(), s_infEditor.sector->id, s_infEditor.itemWallIndex);
					ImGui::Separator();

					// No combobox here since only one option is available - trigger.
					if (editor_button("+"))
					{
						Editor_InfTrigger* trigger = allocTrigger(s_infEditor.item);
						trigger->overrideSet = ITO_NONE;
						trigger->type = ITRIGGER_WALL;
					}
					ImGui::SameLine(0.0f, 4.0f);
					if (editor_button("-"))
					{
						deleteIndex = s_infEditor.curClassIndex;
					}
				}

				ImGui::SameLine(winWidth - 128.0f);
				ImGui::SetNextItemWidth(128.0f);
				if (ImGui::Button(s_infEditor.mode == INF_MODE_UI ? "INF Code" : "INF UI"))
				{
					if (s_infEditor.mode == INF_MODE_UI)
					{
						// TODO: Generate code.
						s_infEditor.mode = INF_MODE_CODE;
					}
					else
					{
						s_infEditor.mode = INF_MODE_UI;
					}
				}

				ImGui::Separator();

				if (ImGui::BeginChild(editor_getUniqueLabel(""), { 0, 0 }))
				{
					if (s_infEditor.mode == INF_MODE_UI)
					{
						editor_infEdit_UI(deleteIndex);
					}
					else
					{
						editor_InfEdit_Code();
					}
					ImGui::EndChild();
				}

			}
			ImGui::EndPopup();
		}
		else if (!wasPopupOpen)
		{
			// The popup wasn't active before calling, so leave as active.
			active = true;
		}

		if (!active)
		{
			modifySectorGeometry();
		}

		ImGui::PopStyleColor(2);
		popFont();

		return !active;
	}

	void editor_loadInfBinary(FileStream& file, u32 version)
	{
		if (version < LEF_InfV1) { return; }

		s32 itemCount, elevCount, trigCount, teleCount;
		file.read(&itemCount);
		file.read(&elevCount);
		file.read(&trigCount);
		file.read(&teleCount);

		s_levelInf.item.resize(itemCount);
		s_levelInf.elevator.resize(elevCount);
		s_levelInf.trigger.resize(trigCount);
		s_levelInf.teleport.resize(teleCount);

		// First write the elevators, triggers, and teleporters so items can reference them.
		for (s32 i = 0; i < elevCount; i++)
		{
			Editor_InfElevator* elev = new Editor_InfElevator();
			s_levelInf.elevator[i] = elev;
			elev->classId = IIC_ELEVATOR;

			s32 stopCount, slaveCount;
			file.read((u32*)&elev->type);
			file.read(&elev->overrideSet);
			file.read(&stopCount);
			file.read(&slaveCount);

			elev->stops.resize(stopCount);
			elev->slaves.resize(slaveCount);

			// Properties.
			if (elev->overrideSet & IEO_START)
			{
				file.read(&elev->start);
			}
			if (elev->overrideSet & IEO_SPEED)
			{
				file.read(&elev->speed);
			}
			if (elev->overrideSet & IEO_ANGLE)
			{
				file.read(&elev->angle);
			}
			if (elev->overrideSet & IEO_FLAGS)
			{
				file.read(&elev->flags);
			}
			if (elev->overrideSet & IEO_KEY0)
			{
				u32 key;
				file.read(&key);
				elev->key[0] = KeyItem(key);
			}
			if (elev->overrideSet & IEO_KEY1)
			{
				u32 key;
				file.read(&key);
				elev->key[1] = KeyItem(key);
			}
			if (elev->overrideSet & IEO_CENTER)
			{
				file.readBuffer(&elev->center, sizeof(Vec2f));
			}
			if (elev->overrideSet & IEO_SOUND0)
			{
				file.read(&elev->sounds[0]);
			}
			if (elev->overrideSet & IEO_SOUND1)
			{
				file.read(&elev->sounds[1]);
			}
			if (elev->overrideSet & IEO_SOUND2)
			{
				file.read(&elev->sounds[2]);
			}
			if (elev->overrideSet & IEO_MASTER)
			{
				s32 master;
				file.read(&master);
				elev->master = master != 0;
			}
			if (elev->overrideSet & IEO_EVENT_MASK)
			{
				file.read(&elev->eventMask);
			}
			if (elev->overrideSet & IEO_ENTITY_MASK)
			{
				file.read(&elev->entityMask);
			}

			// Stops
			Editor_InfStop* stop = elev->stops.data();
			for (s32 s = 0; s < stopCount; s++, stop++)
			{
				s32 adjoinCount, texCount, msgCount;
				s32 scriptCallCount = 0;
				file.read(&adjoinCount);
				file.read(&texCount);
				file.read(&msgCount);
				if (version >= LEF_ScriptCall1)
				{
					file.read(&scriptCallCount);
				}
				stop->adjoinCmd.resize(adjoinCount);
				stop->textureCmd.resize(texCount);
				stop->msg.resize(msgCount);

				file.read(&stop->overrideSet);
				s32 rel;
				file.read(&rel);
				stop->relative = rel != 0;
				file.read(&stop->value);
				file.read(&stop->fromSectorFloor);

				u32 delayType;
				file.read(&delayType);
				stop->delayType = Editor_InfStopDelayType(delayType);

				file.read(&stop->delay);
				file.read(&stop->page);

				Editor_InfAdjoinCmd* adjoinCmd = stop->adjoinCmd.data();
				for (s32 a = 0; a < adjoinCount; a++, adjoinCmd++)
				{
					file.read(&adjoinCmd->sector0);
					file.read(&adjoinCmd->sector1);
					file.read(&adjoinCmd->wallIndex0);
					file.read(&adjoinCmd->wallIndex1);
				}

				Editor_InfTextureCmd* texCmd = stop->textureCmd.data();
				for (s32 t = 0; t < texCount; t++, texCmd++)
				{
					file.read(&texCmd->donorSector);
					s32 fromCeil;
					file.read(&fromCeil);
					texCmd->fromCeiling = fromCeil != 0;
				}

				Editor_InfMessage* msg = stop->msg.data();
				for (s32 m = 0; m < msgCount; m++, msg++)
				{
					file.read(&msg->targetSector);
					file.read(&msg->targetWall);
					u32 type;
					file.read(&type);
					msg->type = Editor_InfMessageType(type);

					file.read(&msg->eventFlags);
					file.read(msg->arg, 2);
				}

				if (version >= LEF_ScriptCall1)
				{
					stop->scriptCall.resize(scriptCallCount);
					Editor_ScriptCall* scriptCall = stop->scriptCall.data();
					for (s32 s = 0; s < scriptCallCount; s++, scriptCall++)
					{
						file.read(&scriptCall->funcName);
						for (s32 a = 0; a < 4; a++)
						{
							file.read(&scriptCall->arg[a].value);
						}
					}
				}
			}

			// Slaves
			Editor_InfSlave* slave = elev->slaves.data();
			for (s32 s = 0; s < slaveCount; s++, slave++)
			{
				file.read(&slave->name);
				file.read(&slave->angleOffset);
			}
		}

		for (s32 i = 0; i < trigCount; i++)
		{
			Editor_InfTrigger* trigger = new Editor_InfTrigger();
			s_levelInf.trigger[i] = trigger;
			trigger->classId = IIC_TRIGGER;

			s32 clientCount;
			file.read(&clientCount);
			trigger->clients.resize(clientCount);

			u32 type;
			file.read(&type);
			trigger->type = TriggerType(type);

			file.read(&trigger->overrideSet);
			if (trigger->overrideSet & ITO_MSG)
			{
				u32 cmd;
				file.read(&cmd);
				file.read(trigger->arg, 2);
				trigger->cmd = Editor_InfMessageType(cmd);
			}
			if (trigger->overrideSet & ITO_SOUND)
			{
				file.read(&trigger->sound);
			}
			if (trigger->overrideSet & ITO_MASTER)
			{
				s32 master;
				file.read(&master);
				trigger->master = master != 0;
			}
			if (trigger->overrideSet & ITO_TEXT)
			{
				file.read(&trigger->textId);
			}
			if (trigger->overrideSet & ITO_EVENT_MASK)
			{
				file.read(&trigger->eventMask);
			}
			if (trigger->overrideSet & ITO_ENTITY_MASK)
			{
				file.read(&trigger->entityMask);
			}
			if (trigger->overrideSet & ITO_EVENT)
			{
				file.read(&trigger->event);
			}

			Editor_InfClient* client = trigger->clients.data();
			for (s32 c = 0; c < clientCount; c++, client++)
			{
				file.read(&client->targetSector);
				file.read(&client->targetWall);
				file.read(&client->eventMask);
			}
		}

		for (s32 i = 0; i < teleCount; i++)
		{
			Editor_InfTeleporter* teleporter = new Editor_InfTeleporter();
			s_levelInf.teleport[i] = teleporter;
			teleporter->classId = IIC_TELEPORTER;

			u32 type;
			file.read(&type);
			teleporter->type = TeleportType(type);

			file.read(&teleporter->target);
			if (teleporter->type == TELEPORT_BASIC)
			{
				file.readBuffer(&teleporter->dstPos, sizeof(Vec3f));
				file.read(&teleporter->dstAngle);
			}
		}

		// Then read the items.
		Editor_InfItem* item = s_levelInf.item.data();
		for (s32 i = 0; i < itemCount; i++, item++)
		{
			s32 classCount;
			file.read(&classCount);
			item->classData.resize(classCount);

			file.read(&item->name);
			file.read(&item->wallNum);

			for (s32 c = 0; c < classCount; c++)
			{
				u32 id;
				s32 index;
				file.read(&id);
				file.read(&index);
				assert(index >= 0);

				if (id == IIC_ELEVATOR)
				{
					item->classData[c] = (Editor_InfClass*)s_levelInf.elevator[index];
				}
				else if (id == IIC_TRIGGER)
				{
					item->classData[c] = (Editor_InfClass*)s_levelInf.trigger[index];
				}
				else if (id == IIC_TELEPORTER)
				{
					item->classData[c] = (Editor_InfClass*)s_levelInf.teleport[index];
				}
			}
		}
	}

	s32 getClassIndex(Editor_InfItemClass classId, const void* classPtr)
	{
		s32 index = -1;
		if (classId == IIC_ELEVATOR)
		{
			const s32 count = (s32)s_levelInf.elevator.size();
			const Editor_InfElevator* const* list = s_levelInf.elevator.data();
			for (s32 i = 0; i < count; i++)
			{
				if (classPtr == list[i])
				{
					index = i;
					break;
				}
			}
		}
		else if (classId == IIC_TRIGGER)
		{
			const s32 count = (s32)s_levelInf.trigger.size();
			const Editor_InfTrigger* const* list = s_levelInf.trigger.data();
			for (s32 i = 0; i < count; i++)
			{
				if (classPtr == list[i])
				{
					index = i;
					break;
				}
			}
		}
		else if (classId == IIC_TELEPORTER)
		{
			const s32 count = (s32)s_levelInf.teleport.size();
			const Editor_InfTeleporter* const* list = s_levelInf.teleport.data();
			for (s32 i = 0; i < count; i++)
			{
				if (classPtr == list[i])
				{
					index = i;
					break;
				}
			}
		}
		assert(index >= 0);
		return index;
	}

	void editor_saveInfBinary(FileStream& file)
	{
		// version >= LEF_InfV1
		const s32 itemCount = (s32)s_levelInf.item.size();
		const s32 elevCount = (s32)s_levelInf.elevator.size();
		const s32 trigCount = (s32)s_levelInf.trigger.size();
		const s32 teleCount = (s32)s_levelInf.teleport.size();
		file.write(&itemCount);
		file.write(&elevCount);
		file.write(&trigCount);
		file.write(&teleCount);
		// First write the elevators, triggers, and teleporters so items can reference them.
		const Editor_InfElevator* const* elevList = s_levelInf.elevator.data();
		for (s32 i = 0; i < elevCount; i++)
		{
			const Editor_InfElevator* elev = elevList[i];
			const s32 stopCount = (s32)elev->stops.size();
			const s32 slaveCount = (s32)elev->slaves.size();

			file.write((u32*)&elev->type);
			file.write(&elev->overrideSet);
			file.write(&stopCount);
			file.write(&slaveCount);

			// Properties.
			if (elev->overrideSet & IEO_START)
			{
				file.write(&elev->start);
			}
			if (elev->overrideSet & IEO_SPEED)
			{
				file.write(&elev->speed);
			}
			if (elev->overrideSet & IEO_ANGLE)
			{
				file.write(&elev->angle);
			}
			if (elev->overrideSet & IEO_FLAGS)
			{
				file.write(&elev->flags);
			}
			if (elev->overrideSet & IEO_KEY0)
			{
				u32 key = elev->key[0];
				file.write(&key);
			}
			if (elev->overrideSet & IEO_KEY1)
			{
				u32 key = elev->key[1];
				file.write(&key);
			}
			if (elev->overrideSet & IEO_CENTER)
			{
				file.writeBuffer(&elev->center, sizeof(Vec2f));
			}
			if (elev->overrideSet & IEO_SOUND0)
			{
				file.write(&elev->sounds[0]);
			}
			if (elev->overrideSet & IEO_SOUND1)
			{
				file.write(&elev->sounds[1]);
			}
			if (elev->overrideSet & IEO_SOUND2)
			{
				file.write(&elev->sounds[2]);
			}
			if (elev->overrideSet & IEO_MASTER)
			{
				s32 master = elev->master ? 1 : 0;
				file.write(&master);
			}
			if (elev->overrideSet & IEO_EVENT_MASK)
			{
				file.write(&elev->eventMask);
			}
			if (elev->overrideSet & IEO_ENTITY_MASK)
			{
				file.write(&elev->entityMask);
			}

			// Stops
			const Editor_InfStop* stop = elev->stops.data();
			for (s32 s = 0; s < stopCount; s++, stop++)
			{
				const s32 adjoinCount = (s32)stop->adjoinCmd.size();
				const s32 texCount = (s32)stop->textureCmd.size();
				const s32 msgCount = (s32)stop->msg.size();
				const s32 scriptCallCount = (s32)stop->scriptCall.size();
				file.write(&adjoinCount);
				file.write(&texCount);
				file.write(&msgCount);
				// version >= LEF_ScriptCall1
				file.write(&scriptCallCount);

				file.write(&stop->overrideSet);
				s32 rel = stop->relative ? 1 : 0;
				file.write(&rel);
				file.write(&stop->value);
				file.write(&stop->fromSectorFloor);

				const u32 delayType = stop->delayType;
				file.write(&delayType);
				file.write(&stop->delay);
				file.write(&stop->page);

				const Editor_InfAdjoinCmd* adjoinCmd = stop->adjoinCmd.data();
				for (s32 a = 0; a < adjoinCount; a++, adjoinCmd++)
				{
					file.write(&adjoinCmd->sector0);
					file.write(&adjoinCmd->sector1);
					file.write(&adjoinCmd->wallIndex0);
					file.write(&adjoinCmd->wallIndex1);
				}

				const Editor_InfTextureCmd* texCmd = stop->textureCmd.data();
				for (s32 t = 0; t < texCount; t++, texCmd++)
				{
					file.write(&texCmd->donorSector);
					s32 fromCeil = texCmd->fromCeiling ? 1 : 0;
					file.write(&fromCeil);
				}

				const Editor_InfMessage* msg = stop->msg.data();
				for (s32 m = 0; m < msgCount; m++, msg++)
				{
					file.write(&msg->targetSector);
					file.write(&msg->targetWall);
					const u32 type = msg->type;
					file.write(&type);
					file.write(&msg->eventFlags);
					file.write(msg->arg, 2);
				}

				// version >= LEF_ScriptCall1
				const Editor_ScriptCall* scriptCall = stop->scriptCall.data();
				for (s32 s = 0; s < scriptCallCount; s++, scriptCall++)
				{
					file.write(&scriptCall->funcName);
					for (s32 a = 0; a < 4; a++)
					{
						file.write(&scriptCall->arg[a].value);
					}
				}
			}

			// Slaves
			const Editor_InfSlave* slave = elev->slaves.data();
			for (s32 s = 0; s < slaveCount; s++, slave++)
			{
				file.write(&slave->name);
				file.write(&slave->angleOffset);
			}
		}

		const Editor_InfTrigger* const* triggerList = s_levelInf.trigger.data();
		for (s32 i = 0; i < trigCount; i++)
		{
			const Editor_InfTrigger* trigger = triggerList[i];
			const s32 clientCount = (s32)trigger->clients.size();
			file.write(&clientCount);

			const u32 type = trigger->type;
			file.write(&type);
			file.write(&trigger->overrideSet);
			if (trigger->overrideSet & ITO_MSG)
			{
				const u32 cmd = trigger->cmd;
				file.write(&cmd);
				file.write(trigger->arg, 2);
			}
			if (trigger->overrideSet & ITO_SOUND)
			{
				file.write(&trigger->sound);
			}
			if (trigger->overrideSet & ITO_MASTER)
			{
				s32 master = trigger->master ? 1 : 0;
				file.write(&master);
			}
			if (trigger->overrideSet & ITO_TEXT)
			{
				file.write(&trigger->textId);
			}
			if (trigger->overrideSet & ITO_EVENT_MASK)
			{
				file.write(&trigger->eventMask);
			}
			if (trigger->overrideSet & ITO_ENTITY_MASK)
			{
				file.write(&trigger->entityMask);
			}
			if (trigger->overrideSet & ITO_EVENT)
			{
				file.write(&trigger->event);
			}

			const Editor_InfClient* client = trigger->clients.data();
			for (s32 c = 0; c < clientCount; c++, client++)
			{
				file.write(&client->targetSector);
				file.write(&client->targetWall);
				file.write(&client->eventMask);
			}
		}

		const Editor_InfTeleporter* const* teleporterList = s_levelInf.teleport.data();
		for (s32 i = 0; i < teleCount; i++)
		{
			const Editor_InfTeleporter* teleporter = teleporterList[i];
			const u32 type = teleporter->type;
			file.write(&type);
			file.write(&teleporter->target);
			if (teleporter->type == TELEPORT_BASIC)
			{
				file.writeBuffer(&teleporter->dstPos, sizeof(Vec3f));
				file.write(&teleporter->dstAngle);
			}
		}

		// Then write the items.
		const Editor_InfItem* item = s_levelInf.item.data();
		for (s32 i = 0; i < itemCount; i++, item++)
		{
			const s32 classCount = (s32)item->classData.size();
			file.write(&classCount);
			file.write(&item->name);
			file.write(&item->wallNum);

			const Editor_InfClass* const* classList = item->classData.data();
			for (s32 c = 0; c < classCount; c++)
			{
				const Editor_InfClass* classPtr = classList[c];
				const u32 id = classPtr->classId;
				file.write(&id);

				const s32 index = getClassIndex(classPtr->classId, classPtr);
				file.write(&index);
			}
		}
	}

	void editor_infGetViewportControl(Editor_InfVpControl* ctrl)
	{
		ctrl->type = InfVpControl_None;
		if (!s_infEditor.item || s_infEditor.item->classData.empty()) { return; }

		const s32 classIndex = min(max(0, s_infEditor.curClassIndex), (s32)s_infEditor.item->classData.size()-1);
		if (classIndex < 0) { return; }

		const Editor_InfClass* classData = s_infEditor.item->classData[classIndex];
		if (classData->classId == IIC_ELEVATOR)
		{
			const Editor_InfElevator* elev = getElevFromClassData(classData);
			if (elev->type == IET_MORPH_SPIN1 || elev->type == IET_MORPH_SPIN2 || elev->type == IET_ROTATE_WALL)
			{
				ctrl->type = InfVpControl_Center;
				ctrl->cen = { elev->center.x, s_infEditor.sector->floorHeight, elev->center.z };
			}
			else if (elev->type == IET_MORPH_MOVE1 || elev->type == IET_MORPH_MOVE2 || elev->type == IET_MOVE_WALL || 
				     elev->type == IET_SCROLL_FLOOR || elev->type == IET_SCROLL_CEILING)
			{
				ctrl->type = InfVpControl_AngleXZ;
				ctrl->cen.x = (s_infEditor.sector->bounds[0].x + s_infEditor.sector->bounds[1].x) * 0.5f;
				ctrl->cen.y = s_infEditor.sector->floorHeight;
				ctrl->cen.z = (s_infEditor.sector->bounds[0].z + s_infEditor.sector->bounds[1].z) * 0.5f;
				ctrl->dir = { sinf(elev->angle), 0.0f, cosf(elev->angle) };
			}
			else if (elev->type == IET_SCROLL_WALL)
			{
				// TODO: Find a wall to put it on.
				// Based on the wall, build a position and direction.
				s32 bestFitWall = -1;
				f32 bestFitWallLenSq = 0.0f;
				f32 bestFitWallHeight = 0.0f;
				Vec3f vtx[2] = { 0 };
				s32 count = (s32)s_infEditor.sector->walls.size();
				EditorWall* wall = s_infEditor.sector->walls.data();
				for (s32 w = 0; w < count; w++, wall++)
				{
					if (!(wall->flags[0] & (WF1_SCROLL_BOT_TEX | WF1_SCROLL_MID_TEX | WF1_SCROLL_TOP_TEX)))
					{
						continue;
					}
					const f32 dx = s_infEditor.sector->vtx[wall->idx[1]].x - s_infEditor.sector->vtx[wall->idx[0]].x;
					const f32 dz = s_infEditor.sector->vtx[wall->idx[1]].z - s_infEditor.sector->vtx[wall->idx[0]].z;
					const f32 lenSq = dx * dx + dz * dz;

					if ((wall->flags[0] & WF1_SCROLL_BOT_TEX) && wall->adjoinId >= 0)
					{
						EditorSector* next = &s_level.sectors[wall->adjoinId];
						const f32 height = next->floorHeight - s_infEditor.sector->floorHeight;
						if (height > 0.0f && height >= bestFitWallHeight && lenSq > bestFitWallLenSq)
						{
							bestFitWallHeight = height;
							bestFitWallLenSq = lenSq;
							bestFitWall = w;

							vtx[0] = { s_infEditor.sector->vtx[wall->idx[0]].x, (next->floorHeight + s_infEditor.sector->floorHeight)*0.5f, s_infEditor.sector->vtx[wall->idx[0]].z };
							vtx[1] = { s_infEditor.sector->vtx[wall->idx[1]].x, vtx[0].y, s_infEditor.sector->vtx[wall->idx[1]].z };
						}
					}
					if ((wall->flags[0] & WF1_SCROLL_TOP_TEX) && wall->adjoinId >= 0)
					{
						EditorSector* next = &s_level.sectors[wall->adjoinId];
						const f32 height = s_infEditor.sector->ceilHeight - next->ceilHeight;
						if (height > 0.0f && height >= bestFitWallHeight && lenSq > bestFitWallLenSq)
						{
							bestFitWallHeight = height;
							bestFitWallLenSq = lenSq;
							bestFitWall = w;

							vtx[0] = { s_infEditor.sector->vtx[wall->idx[0]].x, (s_infEditor.sector->ceilHeight + next->ceilHeight)*0.5f, s_infEditor.sector->vtx[wall->idx[0]].z };
							vtx[1] = { s_infEditor.sector->vtx[wall->idx[1]].x, vtx[0].y, s_infEditor.sector->vtx[wall->idx[1]].z };
						}
					}
					if (wall->flags[0] & WF1_SCROLL_MID_TEX)
					{
						const f32 height = s_infEditor.sector->ceilHeight - s_infEditor.sector->floorHeight;
						if (height > 0.0f && height >= bestFitWallHeight && lenSq > bestFitWallLenSq)
						{
							bestFitWallHeight = height;
							bestFitWallLenSq = lenSq;
							bestFitWall = w;

							vtx[0] = { s_infEditor.sector->vtx[wall->idx[0]].x, (s_infEditor.sector->ceilHeight + s_infEditor.sector->floorHeight)*0.5f, s_infEditor.sector->vtx[wall->idx[0]].z };
							vtx[1] = { s_infEditor.sector->vtx[wall->idx[1]].x, vtx[0].y, s_infEditor.sector->vtx[wall->idx[1]].z };
						}
					}
				}
				if (bestFitWall >= 0)
				{
					ctrl->type = InfVpControl_AngleXY;
					ctrl->cen = { (vtx[0].x + vtx[1].x) * 0.5f, vtx[0].y, (vtx[0].z + vtx[1].z) * 0.5f };

					Vec2f dir = { vtx[1].x - vtx[0].x, vtx[1].z - vtx[0].z };
					dir = TFE_Math::normalize(&dir);
					f32 xzScale = sinf(elev->angle);

					ctrl->dir = { dir.x*xzScale, -cosf(elev->angle), dir.z*xzScale };
					ctrl->nrm = { -dir.z, 0.0f, dir.x };
				}
			}
		}
		else if (classData->classId == IIC_TELEPORTER)
		{
			const Editor_InfTeleporter* teleporter = getTeleporterFromClassData(classData);
			if (teleporter->type == TELEPORT_BASIC)
			{
				ctrl->type = InfVpControl_TargetPos3d;
				ctrl->cen = teleporter->dstPos;
			}
		}
	}

	bool canBeRelative(Editor_InfElevType type)
	{
		return type == IET_BASIC || type == IET_BASIC_AUTO || type == IET_INV || type == IET_DOOR || type == IET_DOOR_INV || type == IET_DOOR_MID ||
			IET_MOVE_CEILING || IET_MOVE_FLOOR || IET_MOVE_FC;
	}

	f32 getValue(Editor_InfStop* stop, EditorSector* sector, Editor_InfElevType elevType)
	{
		f32 value = stop->value;
		if (!stop->fromSectorFloor.empty())
		{
			s32 sectorId = findSectorByName(stop->fromSectorFloor.c_str());
			if (sectorId >= 0)
			{
				value = s_level.sectors[sectorId].floorHeight;
			}
		}
		else if (stop->relative && canBeRelative(elevType))
		{
			value += sector->floorHeight;
		}
		return value;
	}

	EditorWall* getMirrorWall(EditorWall* wall)
	{
		return (wall->adjoinId >= 0 && wall->mirrorId >= 0) ? &s_level.sectors[wall->adjoinId].walls[wall->mirrorId] : nullptr;
	}

	const EditorWall* getMirrorWall(const EditorWall* wall)
	{
		return (wall->adjoinId >= 0 && wall->mirrorId >= 0) ? &s_level.sectors[wall->adjoinId].walls[wall->mirrorId] : nullptr;
	}

	void addSectorToModState(EditorSector* sector)
	{
		if (!sector || s_sectorMod.prevState.id == sector->id) { return; }
		const s32 count = (s32)s_sectorMod.mirrorState.size();
		EditorSector* mirrorSector = s_sectorMod.mirrorState.data();
		for (s32 i = 0; i < count; i++, mirrorSector++)
		{
			if (mirrorSector->id == sector->id) { return; }
		}
		s_sectorMod.mirrorState.push_back(*sector);
	}

	const EditorSector* getSourceSector(s32 id)
	{
		if (id < 0) { return nullptr; }
		const s32 count = (s32)s_sectorMod.mirrorState.size();
		const EditorSector* mirrorSector = s_sectorMod.mirrorState.data();
		for (s32 i = 0; i < count; i++, mirrorSector++)
		{
			if (mirrorSector->id == id) { return mirrorSector; }
		}
		return nullptr;
	}

	void addMirrorWallsToMirrorState(Editor_InfElevator* elev, EditorSector* sector)
	{
		// Add appropriate mirrors as well.
		if (elev->type == IET_MORPH_MOVE1 || elev->type == IET_MORPH_MOVE2 || elev->type == IET_MOVE_WALL ||
			elev->type == IET_MORPH_SPIN1 || elev->type == IET_MORPH_SPIN2 || elev->type == IET_ROTATE_WALL)
		{
			const s32 wallCount = (s32)sector->walls.size();
			const EditorWall* wall = sector->walls.data();
			for (s32 w = 0; w < wallCount; w++, wall++)
			{
				if (!(wall->flags[0] & WF1_WALL_MORPHS)) { continue; }
				const EditorWall* mirror = getMirrorWall(wall);
				if (mirror && (mirror->flags[0] & WF1_WALL_MORPHS))
				{
					addSectorToModState(&s_level.sectors[wall->adjoinId]);
				}
			}
		}
	}

	void addToFixupSectors(EditorSector* sector)
	{
		const s32 count = (s32)s_sectorsToFixup.size();
		EditorSector** list = s_sectorsToFixup.data();
		for (s32 i = 0; i < count; i++)
		{
			if (list[i] == sector) { return; }
		}
		s_sectorsToFixup.push_back(sector);
	}

	void fixupSectors()
	{
		const s32 count = (s32)s_sectorsToFixup.size();
		EditorSector** list = s_sectorsToFixup.data();
		for (s32 i = 0; i < count; i++)
		{
			sectorToPolygon(list[i]);
		}
	}

	void moveWalls(Editor_InfElevator* elev, EditorSector* sector, const EditorSector* srcSector, f32 value)
	{
		Vec2f dir = { sinf(elev->angle), cosf(elev->angle) };
		const s32 wallCount = (s32)sector->walls.size();
		EditorWall* wall = sector->walls.data();
		for (s32 w = 0; w < wallCount; w++, wall++)
		{
			if (!(wall->flags[0] & WF1_WALL_MORPHS)) { continue; }

			const Vec2f* src = &srcSector->vtx[wall->idx[0]];
			Vec2f* v0 = &sector->vtx[wall->idx[0]];

			v0->x = src->x + value * dir.x;
			v0->z = src->z + value * dir.z;

			EditorWall* mirror = getMirrorWall(wall);
			if (mirror && (mirror->flags[0] & WF1_WALL_MORPHS))
			{
				const EditorSector* srcSector = getSourceSector(wall->adjoinId);
				EditorSector* dstSector = &s_level.sectors[wall->adjoinId];
				src = &srcSector->vtx[mirror->idx[0]];
				v0 = &dstSector->vtx[mirror->idx[0]];

				v0->x = src->x + value * dir.x;
				v0->z = src->z + value * dir.z;

				addToFixupSectors(dstSector);
			}
		}
		sectorToPolygon(sector);
	}

	void rotateWalls(Editor_InfElevator* elev, EditorSector* sector, const EditorSector* srcSector, f32 value)
	{
		const s32 wallCount = (s32)sector->walls.size();
		EditorWall* wall = sector->walls.data();
		const f32 angle = value * PI / 180.0f;
		const f32 ca = cosf(angle);
		const f32 sa = sinf(angle);

		for (s32 w = 0; w < wallCount; w++, wall++)
		{
			if (!(wall->flags[0] & WF1_WALL_MORPHS)) { continue; }

			const Vec2f* src = &srcSector->vtx[wall->idx[0]];
			Vec2f* v0 = &sector->vtx[wall->idx[0]];

			const f32 xc = src->x - elev->center.x;
			const f32 zc = src->z - elev->center.z;

			v0->x = xc * ca - zc * sa + elev->center.x;
			v0->z = xc * sa + zc * ca + elev->center.z;

			EditorWall* mirror = getMirrorWall(wall);
			if (mirror && (mirror->flags[0] & WF1_WALL_MORPHS))
			{
				const EditorSector* srcSector = getSourceSector(wall->adjoinId);
				EditorSector* dstSector = &s_level.sectors[wall->adjoinId];
				src = &srcSector->vtx[mirror->idx[0]];
				v0 = &dstSector->vtx[mirror->idx[0]];

				const f32 xc = src->x - elev->center.x;
				const f32 zc = src->z - elev->center.z;

				v0->x = xc * ca - zc * sa + elev->center.x;
				v0->z = xc * sa + zc * ca + elev->center.z;

				addToFixupSectors(dstSector);
			}
		}
		addToFixupSectors(sector);
	}

	void scrollWalls(Editor_InfElevator* elev, EditorSector* sector, const EditorSector* srcSector, f32 value)
	{
		const u32 scrollFlags = WF1_SCROLL_SIGN_TEX | WF1_SCROLL_BOT_TEX | WF1_SCROLL_MID_TEX | WF1_SCROLL_TOP_TEX;
		const s32 wallCount = (s32)sector->walls.size();
		const Vec2f dir = { sinf(elev->angle), cosf(elev->angle) };
		const EditorWall* srcWall = srcSector->walls.data();
		EditorWall* wall = sector->walls.data();
		value /= 8.0f;
		for (s32 w = 0; w < wallCount; w++, wall++, srcWall++)
		{
			if (wall->flags[0] & scrollFlags)
			{
				if (wall->flags[0] & WF1_SCROLL_TOP_TEX)
				{
					wall->tex[HP_TOP].offset.x = srcWall->tex[HP_TOP].offset.x + value * dir.x;
					wall->tex[HP_TOP].offset.z = srcWall->tex[HP_TOP].offset.z + value * dir.z;
				}
				if (wall->flags[0] & WF1_SCROLL_MID_TEX)
				{
					wall->tex[HP_MID].offset.x = srcWall->tex[HP_MID].offset.x + value * dir.x;
					wall->tex[HP_MID].offset.z = srcWall->tex[HP_MID].offset.z + value * dir.z;
				}
				if (wall->flags[0] & WF1_SCROLL_BOT_TEX)
				{
					wall->tex[HP_BOT].offset.x = srcWall->tex[HP_BOT].offset.x + value * dir.x;
					wall->tex[HP_BOT].offset.z = srcWall->tex[HP_BOT].offset.z + value * dir.z;
				}
				if (wall->flags[0] & WF1_SCROLL_SIGN_TEX)
				{
					wall->tex[HP_SIGN].offset.x = srcWall->tex[HP_SIGN].offset.x + value * dir.x;
					wall->tex[HP_SIGN].offset.z = srcWall->tex[HP_SIGN].offset.z + value * dir.z;
				}
			}
		}
	}

	void scrollFloor(Editor_InfElevator* elev, EditorSector* sector, const EditorSector* srcSector, f32 value)
	{
		Vec2f dir = { sinf(elev->angle), cosf(elev->angle) };
		sector->floorTex.offset.x = srcSector->floorTex.offset.x + value * dir.x;
		sector->floorTex.offset.z = srcSector->floorTex.offset.z + value * dir.z;
	}

	void scrollCeiling(Editor_InfElevator* elev, EditorSector* sector, const EditorSector* srcSector, f32 value)
	{
		Vec2f dir = { sinf(elev->angle), cosf(elev->angle) };
		sector->ceilTex.offset.x = srcSector->ceilTex.offset.x + value * dir.x;
		sector->ceilTex.offset.z = srcSector->ceilTex.offset.z + value * dir.z;
	}

	void applySectorElevValue(EditorSector* sector, Editor_InfElevator* elev, f32 value, bool handleAutoStop)
	{
		// Apply the value based on the elevator type.
		const f32 baseFloorHeight = sector->floorHeight;
		const s32 mirrorCount = (s32)s_sectorMod.mirrorState.size();
		const EditorSector* mirrorSector;
		switch (elev->type)
		{
			case IET_BASIC_AUTO:
			case IET_DOOR:
			case IET_DOOR_INV:
			case IET_DOOR_MID:
			{
				if (handleAutoStop)
				{
					if (elev->type == IET_DOOR)
					{
						f32 delta = value - sector->ceilHeight;
						sector->ceilHeight = value;

						mirrorSector = s_sectorMod.mirrorState.data();
						for (s32 m = 0; m < mirrorCount; m++, mirrorSector++)
						{
							EditorSector* dstSector = &s_level.sectors[mirrorSector->id];
							dstSector->ceilHeight += delta;
						}
					}
					else if (elev->type == IET_DOOR_INV)
					{
						f32 delta = value - sector->floorHeight;
						sector->floorHeight = value;

						mirrorSector = s_sectorMod.mirrorState.data();
						for (s32 m = 0; m < mirrorCount; m++, mirrorSector++)
						{
							EditorSector* dstSector = &s_level.sectors[mirrorSector->id];
							dstSector->floorHeight += delta;
						}
					}
					else if (elev->type == IET_DOOR_MID)
					{
						const f32 delta = value - sector->ceilHeight;
						sector->ceilHeight += delta;
						sector->floorHeight -= delta;

						mirrorSector = s_sectorMod.mirrorState.data();
						for (s32 m = 0; m < mirrorCount; m++, mirrorSector++)
						{
							EditorSector* dstSector = &s_level.sectors[mirrorSector->id];
							dstSector->ceilHeight  += delta;
							dstSector->floorHeight -= delta;
						}
					}
				}
			} break;
			case IET_INV:
			case IET_MOVE_CEILING:
			{
				f32 delta = value - sector->ceilHeight;
				sector->ceilHeight = value;

				mirrorSector = s_sectorMod.mirrorState.data();
				for (s32 m = 0; m < mirrorCount; m++, mirrorSector++)
				{
					EditorSector* dstSector = &s_level.sectors[mirrorSector->id];
					dstSector->ceilHeight += delta;
				}
			} break;
			case IET_BASIC:
			case IET_MOVE_FLOOR:
			{
				f32 delta = value - sector->floorHeight;
				sector->floorHeight = value;

				mirrorSector = s_sectorMod.mirrorState.data();
				for (s32 m = 0; m < mirrorCount; m++, mirrorSector++)
				{
					EditorSector* dstSector = &s_level.sectors[mirrorSector->id];
					dstSector->floorHeight += delta;
				}
			} break;
			case IET_MOVE_FC:
			{
				sector->floorHeight = value;
				const f32 delta = sector->floorHeight - baseFloorHeight;
				sector->ceilHeight += delta;

				mirrorSector = s_sectorMod.mirrorState.data();
				for (s32 m = 0; m < mirrorCount; m++, mirrorSector++)
				{
					EditorSector* dstSector = &s_level.sectors[mirrorSector->id];
					dstSector->floorHeight += delta;
					dstSector->ceilHeight += delta;
				}
			} break;
			case IET_MOVE_OFFSET:
			{
				// TODO
			} break;
			case IET_MORPH_MOVE1:
			case IET_MORPH_MOVE2:
			case IET_MOVE_WALL:
			{
				moveWalls(elev, sector, &s_sectorMod.prevState, value);

				const s32 slaveCount = (s32)elev->slaves.size();
				const Editor_InfSlave* slave = elev->slaves.data();
				for (s32 s = 0; s < slaveCount; s++, slave++)
				{
					s32 sectorId = findSectorByName(slave->name.c_str());
					if (sectorId >= 0)
					{
						moveWalls(elev, &s_level.sectors[sectorId], getSourceSector(sectorId), value);
					}
				}
			} break;
			case IET_MORPH_SPIN1:
			case IET_MORPH_SPIN2:
			case IET_ROTATE_WALL:
			{
				rotateWalls(elev, sector, &s_sectorMod.prevState, value);

				const s32 slaveCount = (s32)elev->slaves.size();
				const Editor_InfSlave* slave = elev->slaves.data();
				for (s32 s = 0; s < slaveCount; s++, slave++)
				{
					s32 sectorId = findSectorByName(slave->name.c_str());
					if (sectorId >= 0)
					{
						rotateWalls(elev, &s_level.sectors[sectorId], getSourceSector(sectorId), value + slave->angleOffset);
					}
				}
			} break;
			case IET_SCROLL_WALL:
			{
				scrollWalls(elev, sector, &s_sectorMod.prevState, value);

				const s32 slaveCount = (s32)elev->slaves.size();
				const Editor_InfSlave* slave = elev->slaves.data();
				for (s32 s = 0; s < slaveCount; s++, slave++)
				{
					s32 sectorId = findSectorByName(slave->name.c_str());
					if (sectorId >= 0)
					{
						scrollWalls(elev, &s_level.sectors[sectorId], getSourceSector(sectorId), value);
					}
				}
			} break;
			case IET_SCROLL_FLOOR:
			{
				scrollFloor(elev, sector, &s_sectorMod.prevState, value);

				const s32 slaveCount = (s32)elev->slaves.size();
				const Editor_InfSlave* slave = elev->slaves.data();
				for (s32 s = 0; s < slaveCount; s++, slave++)
				{
					s32 sectorId = findSectorByName(slave->name.c_str());
					if (sectorId >= 0)
					{
						scrollFloor(elev, &s_level.sectors[sectorId], getSourceSector(sectorId), value);
					}
				}
			} break;
			case IET_SCROLL_CEILING:
			{
				scrollCeiling(elev, sector, &s_sectorMod.prevState, value);

				const s32 slaveCount = (s32)elev->slaves.size();
				const Editor_InfSlave* slave = elev->slaves.data();
				for (s32 s = 0; s < slaveCount; s++, slave++)
				{
					s32 sectorId = findSectorByName(slave->name.c_str());
					if (sectorId >= 0)
					{
						scrollCeiling(elev, &s_level.sectors[sectorId], getSourceSector(sectorId), value);
					}
				}
			} break;
			case IET_CHANGE_LIGHT:
			{
				sector->ambient = u32(value) & 31;

				const s32 slaveCount = (s32)elev->slaves.size();
				const Editor_InfSlave* slave = elev->slaves.data();
				for (s32 s = 0; s < slaveCount; s++, slave++)
				{
					s32 sectorId = findSectorByName(slave->name.c_str());
					if (sectorId >= 0)
					{
						s_level.sectors[sectorId].ambient = u32(value) & 31;
					}
				}
			} break;
			case IET_CHANGE_WALL_LIGHT:
			{
				const s32 wallCount = (s32)sector->walls.size();
				EditorWall* wall = sector->walls.data();
				for (s32 w = 0; w < wallCount; w++, wall++)
				{
					if (!(wall->flags[0] & WF1_CHANGE_WALL_LIGHT)) { continue; }
					wall->wallLight = s32(value) & 31;
				}

				const s32 slaveCount = (s32)elev->slaves.size();
				const Editor_InfSlave* slave = elev->slaves.data();
				for (s32 s = 0; s < slaveCount; s++, slave++)
				{
					s32 sectorId = findSectorByName(slave->name.c_str());
					if (sectorId >= 0)
					{
						EditorSector* slaveSector = &s_level.sectors[sectorId];
						const s32 slaveWallCount = (s32)slaveSector->walls.size();
						wall = slaveSector->walls.data();
						for (s32 w = 0; w < slaveWallCount; w++, wall++)
						{
							if (!(wall->flags[0] & WF1_CHANGE_WALL_LIGHT)) { continue; }
							wall->wallLight = s32(value) & 31;
						}
					}
				}
			} break;
		}
	}

	bool elevHasAutogeneratedStops(Editor_InfElevator* elev)
	{
		return elev->type == IET_BASIC_AUTO || elev->type == IET_DOOR || elev->type == IET_DOOR_INV || elev->type == IET_DOOR_MID;
	}
		
	void modifySectorGeometryTime(Editor_InfItem* item, EditorSector* sector, Editor_InfElevator* elev, f32 time)
	{
		if (item == s_sectorMod.item && sector == s_sectorMod.sector && elev == s_sectorMod.elev && time == s_sectorMod.time)
		{
			return;
		}

		// Revert the previous item.
		if (s_sectorMod.sector && (item != s_sectorMod.item || sector != s_sectorMod.sector || elev != s_sectorMod.elev))
		{
			*s_sectorMod.sector = s_sectorMod.prevState;
			sectorToPolygon(s_sectorMod.sector);

			const s32 mirrorStateCount = (s32)s_sectorMod.mirrorState.size();
			const EditorSector* mirrorSector = s_sectorMod.mirrorState.data();
			for (s32 s = 0; s < mirrorStateCount; s++, mirrorSector++)
			{
				s_level.sectors[mirrorSector->id] = *mirrorSector;
				sectorToPolygon(&s_level.sectors[mirrorSector->id]);
			}
		}
				
		if (sector && (item != s_sectorMod.item || sector != s_sectorMod.sector || elev != s_sectorMod.elev))
		{
			s_sectorMod.prevState = *sector;
			s_sectorMod.mirrorState.clear();
			addMirrorWallsToMirrorState(elev, sector);

			const s32 slaveCount = (s32)elev->slaves.size();
			if (slaveCount > 0)
			{
				for (s32 s = 0; s < slaveCount; s++)
				{
					s32 sectorId = findSectorByName(elev->slaves[s].name.c_str());
					if (sectorId >= 0)
					{
						EditorSector* slaveSector = &s_level.sectors[sectorId];
						addSectorToModState(slaveSector);
						addMirrorWallsToMirrorState(elev, slaveSector);
					}
				}
			}
		}
		else if (!sector)
		{
			s_sectorMod.prevState = {};
			s_sectorMod.mirrorState.clear();
			return;
		}

		// Copy over the new data.
		s_sectorMod.item = item;
		s_sectorMod.sector = sector;
		s_sectorMod.elev = elev;
		s_sectorMod.stopIndex = -1;
		s_sectorMod.time = time;
		s_sectorsToFixup.clear();

		f32 value;
		if (!elev->stops.empty())
		{
			const s32 stopCount = (s32)elev->stops.size();
			Editor_InfStop* stop = elev->stops.data();

			f32 totalTime = 0.0f;
			f32 prevValue = 0.0f;
			s_relTime.clear();
			s_stopValue.clear();
			for (s32 s = 0; s < stopCount; s++, stop++)
			{
				f32 curValue = getValue(stop, sector, elev->type);
				if (s > 0)
				{
					f32 delta = fabsf(curValue - prevValue);
					totalTime += delta;
				}
				s_relTime.push_back(totalTime);
				s_stopValue.push_back(curValue);
				prevValue = curValue;
			}

			f32 stopT = max(0.0f, min(1.0f, time)) * totalTime;
			f32* relT = s_relTime.data();
			f32* stopValue = s_stopValue.data();
			s32 stop0 = 0, stop1 = 0;
			f32 value0 = 0.0f, value1 = 0.0f;
			f32 blend = 0.0f;

			bool useInterpolation = true;
			if ((elev->overrideSet & IEO_SPEED) && elev->speed == 0.0f)
			{
				useInterpolation = false;
			}

			for (s32 s = 0; s < stopCount - 1; s++)
			{
				if ((stopT >= relT[s] && stopT <= relT[s+1]) || s == stopCount - 1)
				{
					if (useInterpolation)
					{
						value0 = stopValue[s];
						value1 = stopValue[s + 1];

						const f32 denom = relT[s + 1] - relT[s];
						blend = max(0.0f, min(1.0f, fabsf(denom) > FLT_EPSILON ? (stopT - relT[s]) / denom : 0.5f));
						value = value0 + blend * (value1 - value0);
					}
					else
					{
						value = stopValue[s];
					}
					break;
				}
			}
		}
		else if (elevHasAutogeneratedStops(elev)) // Auto-generated.
		{
			switch (elev->type)
			{
				case IET_BASIC_AUTO:
				{
					// TODO
				} break;
				case IET_DOOR:
				{
					const f32 value0 = s_sectorMod.prevState.floorHeight;
					const f32 value1 = s_sectorMod.prevState.ceilHeight;
					value = value0 + max(0.0f, min(1.0f, time)) * (value1 - value0);
				} break;
				case IET_DOOR_INV:
				{
					const f32 value0 = s_sectorMod.prevState.ceilHeight;
					const f32 value1 = s_sectorMod.prevState.floorHeight;
					value = value0 + max(0.0f, min(1.0f, time)) * (value1 - value0);
				} break;
				case IET_DOOR_MID:
				{
					const f32 value0 = (s_sectorMod.prevState.floorHeight + s_sectorMod.prevState.ceilHeight) * 0.5f;
					const f32 value1 = s_sectorMod.prevState.ceilHeight;
					value = value0 + max(0.0f, min(1.0f, time)) * (value1 - value0);
				} break;
			}
		}
		else  // Continuous
		{
			f32 speed = 8.0f;
			if (elev->overrideSet & IEO_SPEED)
			{
				speed = elev->speed;
			}
			else
			{
				if (elev->type == IET_MORPH_MOVE1 || elev->type == IET_MORPH_MOVE2 || elev->type == IET_MOVE_WALL)
				{
					speed = 15.0f;
				}
				else if (elev->type == IET_MORPH_SPIN1 || elev->type == IET_MORPH_SPIN2 || elev->type == IET_ROTATE_WALL)
				{
					speed = 22.5f;
				}
				else if (elev->type == IET_SCROLL_CEILING || elev->type == IET_SCROLL_FLOOR || elev->type == IET_SCROLL_WALL)
				{
					speed = 4.0f;
				}
				else if (elev->type == IET_CHANGE_LIGHT || elev->type == IET_CHANGE_WALL_LIGHT)
				{
					speed = 32.0f;
				}
			}
			value = elev->speed * time;
		}

		applySectorElevValue(sector, elev, value, true);
		fixupSectors();
	}
			
	void modifySectorGeometry(Editor_InfItem* item, EditorSector* sector, Editor_InfElevator* elev, s32 stopIndex)
	{
		if (item == s_sectorMod.item && sector == s_sectorMod.sector && elev == s_sectorMod.elev && stopIndex == s_sectorMod.stopIndex)
		{
			return;
		}

		// Revert the previous item.
		if (s_sectorMod.sector && (item != s_sectorMod.item || sector != s_sectorMod.sector || elev != s_sectorMod.elev))
		{
			*s_sectorMod.sector = s_sectorMod.prevState;
			sectorToPolygon(s_sectorMod.sector);

			const s32 mirrorStateCount = (s32)s_sectorMod.mirrorState.size();
			const EditorSector* mirrorSector = s_sectorMod.mirrorState.data();
			for (s32 s = 0; s < mirrorStateCount; s++, mirrorSector++)
			{
				s_level.sectors[mirrorSector->id] = *mirrorSector;
				sectorToPolygon(&s_level.sectors[mirrorSector->id]);
			}
		}

		// Save the current state if it is different.
		if (sector && (item != s_sectorMod.item || sector != s_sectorMod.sector || elev != s_sectorMod.elev))
		{
			s_sectorMod.prevState = *sector;
			s_sectorMod.mirrorState.clear();
			addMirrorWallsToMirrorState(elev, sector);

			const s32 slaveCount = (s32)elev->slaves.size();
			if (slaveCount > 0)
			{
				for (s32 s = 0; s < slaveCount; s++)
				{
					s32 sectorId = findSectorByName(elev->slaves[s].name.c_str());
					if (sectorId >= 0)
					{
						EditorSector* slaveSector = &s_level.sectors[sectorId];
						addSectorToModState(slaveSector);
						addMirrorWallsToMirrorState(elev, slaveSector);
					}
				}
			}
		}
		else if (!sector)
		{
			s_sectorMod.prevState = {};
			s_sectorMod.mirrorState.clear();
		}

		// Copy over the new data.
		s_sectorMod.item = item;
		s_sectorMod.sector = sector;
		s_sectorMod.elev = elev;
		s_sectorMod.stopIndex = stopIndex;
		s_sectorMod.time = -1.0f;
		s_sectorsToFixup.clear();
		if (!sector || elev->stops.empty() || stopIndex < 0) { return; }

		// Get the value from the stop or sector.
		const f32 value = getValue(&elev->stops[stopIndex], sector, elev->type);
		applySectorElevValue(sector, elev, value, false);
		fixupSectors();
	}
}
