#pragma once
//////////////////////////////////////////////////////////////////////
// Level Note
// Display colored icons and text in 2D and 3D that exist in 3D space.
// This is only used for the Editor and is not exported or viewable
// in-game.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_System/parser.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Editor/EditorAsset/editorObj3D.h>
#include <vector>
#include <string>

namespace LevelEditor
{
	// Default = 2D + 3D + fade in 3D.
	enum LevelNoteFlags
	{
		LNF_NONE       = 0,
		LNF_2D_ONLY    = FLAG_BIT(0),
		LNF_3D_NO_FADE = FLAG_BIT(1),
		LNF_TEXT_ALWAYS_SHOW = FLAG_BIT(2),
	};

	struct LevelNote
	{
		s32 id = 0;
		// Group & Flags.
		u32 groupId = 0;
		u32 groupIndex = 0;
		u32 flags = LNF_NONE;

		// 3D position.
		Vec3f pos = { 0 };
		Vec2f fade = { 32.0f, 48.0f };

		u32 iconColor = 0xff80ff80;
		u32 textColor = 0xffffffff;

		// Note text in Markdown.
		std::string note;
	};

	extern const f32 c_levelNoteRadius2d;
	extern const f32 c_levelNoteRadius3d;
	extern const u32 c_noteColors[3];
	extern s32 s_hoveredLevelNote;
	extern s32 s_curLevelNote;
	extern s32 s_prevLevelNote;

	void clearLevelNoteSelection();
}
