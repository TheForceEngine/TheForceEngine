#include "profilerView.h"
#include <TFE_Input/input.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_System/system.h>
#include <TFE_System/profiler.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_Archive/archive.h>
#include <TFE_Ui/ui.h>
#include <TFE_Ui/markdown.h>
#include <TFE_System/parser.h>

#include <algorithm>

namespace TFE_ProfilerView
{
	static bool s_open = false;

	bool init()
	{
		return true;
	}

	void destroy()
	{
	}

	void update()
	{
		if (!s_open) { return; }

		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(800, 768));
		ImGui::Begin("Profiler View", &s_open);

		ImGui::LabelText("##Label", "Counters");
		ImGui::Separator();
		u32 counterCount = TFE_Profiler::getCounterCount();
		ImGui::Indent();
		for (u32 c = 0; c < counterCount; c++)
		{
			TFE_CounterInfo info;
			TFE_Profiler::getCounterInfo(c, &info);
			ImGui::Text("%d", info.value); ImGui::SameLine(80);
			ImGui::Text("%s", info.name);
		}
		ImGui::Unindent();

		ImGui::Spacing();
		ImGui::LabelText("##Label", "Zones");
		ImGui::Separator();

		const f64 timeInFrame = TFE_Profiler::getTimeInFrame();
		ImGui::Indent();
		ImGui::Text("%0.3fms", timeInFrame * 1000.0);
		ImGui::SameLine(f32(128));
		ImGui::Text("Frame");

		u32 zoneCount = TFE_Profiler::getZoneCount();
		ImGui::Indent();
		for (u32 z = 0; z < zoneCount; z++)
		{
			TFE_ZoneInfo info;
			TFE_Profiler::getZoneInfo(z, &info);

			for (u32 l = 0; l < info.level; l++)
			{
				ImGui::Indent();
			}

			ImGui::Text("%0.3fms (%6.03f%%)", info.timeInZoneAve * 1000.0, info.fractOfParentAve * 100.0);
			ImGui::SameLine(f32(220 + 16*(info.level + 1)));
			ImGui::Text("%s  [%s:%u]", info.name, info.func, info.lineNumber);

			for (u32 l = 0; l < info.level; l++)
			{
				ImGui::Unindent();
			}
		}
		ImGui::Unindent();
		ImGui::Unindent();

		ImGui::End();
	}

	bool isEnabled()
	{
		return s_open;
	}

	void enable(bool enable)
	{
		s_open = enable;
	}
}
