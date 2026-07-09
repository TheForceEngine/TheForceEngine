#include "sharedScriptAPI.h"
#include "scriptMath.h"
#include "scriptTest.h"
#include "scriptLevelShared.h"
#include <TFE_System/math.h>
#include <TFE_System/system.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Editor/editor.h>
#include <TFE_Editor/errorMessages.h>
#include <TFE_ForceScript/scriptInterface.h>
#include <TFE_Editor/LevelEditor/infoPanel.h>
#include <TFE_Editor/EditorAsset/editorTexture.h>
#include <TFE_Editor/EditorAsset/editorFrame.h>
#include <TFE_Editor/EditorAsset/editorSprite.h>
#include <TFE_Ui/ui.h>

#include <algorithm>
#include <vector>
#include <string>
#include <map>
#include <angelscript.h>

using namespace TFE_Jedi;

namespace TFE_ForceScript
{
	bool s_sharedScriptRegistered = false;

	ScriptMath s_math;
	ScriptTest s_test;
	ScriptLevelShared s_levelShared;
		
	void registerSharedScriptAPI(ScriptAPI api)
	{
		if (s_sharedScriptRegistered) { return; }
		s_sharedScriptRegistered = true;

		s_math.scriptRegister(api);
		s_test.scriptRegister(api);
		s_levelShared.scriptRegister();
	}
}
