#pragma once
// SubRip (.srt) subtitle parser for OGV cutscenes.
#include <TFE_System/types.h>
#include <string>
#include <vector>

namespace TFE_Subtitles
{
	struct SrtEntry
	{
		s32 index;
		f64 startTime;  // seconds
		f64 endTime;    // seconds
		std::string text;
	};

	bool srt_parse(const char* buffer, size_t size, std::vector<SrtEntry>& entries);
	bool srt_loadFromFile(const char* path, std::vector<SrtEntry>& entries);
	// Returns the subtitle active at the given time, or nullptr.
	const SrtEntry* srt_getActiveEntry(const std::vector<SrtEntry>& entries, f64 timeInSeconds);
}
