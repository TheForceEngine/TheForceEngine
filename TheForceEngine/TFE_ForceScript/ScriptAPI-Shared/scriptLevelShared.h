#pragma once
#include <TFE_System/system.h>
#include <TFE_ForceScript/scriptInterface.h>

enum SectorProperty
{
	SECTORPROP_NONE = 0,
	SECTORPROP_FLOOR_HEIGHT = FLAG_BIT(0),
	SECTORPROP_CEIL_HEIGHT = FLAG_BIT(1),
	SECTORPROP_SECOND_HEIGHT = FLAG_BIT(2),
	SECTORPROP_FLOOR_TEX = FLAG_BIT(3),
	SECTORPROP_CEIL_TEX = FLAG_BIT(4),
	SECTORPROP_AMBIENT = FLAG_BIT(5),
	SECTORPROP_START = SECTORPROP_FLOOR_HEIGHT,
	SECTORPROP_END = SECTORPROP_AMBIENT << 1,
};

enum WallPart
{
	WP_MIDDLE = 0,
	WP_TOP,
	WP_BOTTOM,
	WP_SIGN,

	// see levelEditorData.h
	WP_MID = 0,
	WP_BOT = 2,
};

namespace TFE_ForceScript
{
	class ScriptLevelShared
	{
	public:
		// System
		void scriptRegister();
	};
}