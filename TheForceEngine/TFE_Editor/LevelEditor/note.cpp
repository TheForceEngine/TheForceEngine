#include "note.h"
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
	const f32 c_levelNoteRadius2d = 1.0f;
	const f32 c_levelNoteRadius3d = 1.0f;
	const u32 c_noteColors[3] =
	{
		0xffffff80,
		0xff80ffff,
		0xffffffff,
	};

	s32 s_hoveredLevelNote = -1;
	s32 s_curLevelNote = -1;
	s32 s_prevLevelNote = -1; // For bundling consecutive attrib changes as one undo

	void clearLevelNoteSelection()
	{
		s_hoveredLevelNote = -1;
		s_curLevelNote = -1;
	}
}
