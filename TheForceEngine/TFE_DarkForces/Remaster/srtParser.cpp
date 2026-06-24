#include "srtParser.h"
#include <TFE_FileSystem/filestream.h>
#include <TFE_System/system.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace TFE_DarkForces
{
	// HH:MM:SS,mmm -> seconds
	static bool parseTimestamp(const char* str, f64& outSeconds)
	{
		s32 hours, minutes, seconds, millis;
		if (sscanf(str, "%d:%d:%d,%d", &hours, &minutes, &seconds, &millis) != 4)
		{
			// Some SRT files use period instead of comma.
			if (sscanf(str, "%d:%d:%d.%d", &hours, &minutes, &seconds, &millis) != 4)
			{
				return false;
			}
		}
		outSeconds = hours * 3600.0 + minutes * 60.0 + seconds + millis / 1000.0;
		return true;
	}

	static const char* skipWhitespace(const char* p, const char* end)
	{
		while (p < end && (*p == ' ' || *p == '\t'))
		{
			p++;
		}
		return p;
	}

	static const char* readLine(const char* buffer, size_t size, size_t& pos, size_t& lineLen)
	{
		if (pos >= size) { return nullptr; }
		const char* start = buffer + pos;
		const char* end = buffer + size;
		const char* p = start;

		while (p < end && *p != '\n' && *p != '\r')
		{
			p++;
		}
		lineLen = (size_t)(p - start);

		if (p < end && *p == '\r') { p++; }
		if (p < end && *p == '\n') { p++; }
		pos = (size_t)(p - buffer);

		return start;
	}

	bool srt_parse(const char* buffer, size_t size, std::vector<SrtEntry>& entries)
	{
		entries.clear();
		if (!buffer || size == 0) { return false; }

		// Skip UTF-8 BOM.
		size_t pos = 0;
		if (size >= 3 && (u8)buffer[0] == 0xEF && (u8)buffer[1] == 0xBB && (u8)buffer[2] == 0xBF)
		{
			pos = 3;
		}

		while (pos < size)
		{
			const char* line;
			size_t lineLen;
			do
			{
				line = readLine(buffer, size, pos, lineLen);
				if (!line) { return !entries.empty(); }
			} while (lineLen == 0);

			SrtEntry entry = {};
			entry.index = atoi(line);
			if (entry.index <= 0) { continue; }

			line = readLine(buffer, size, pos, lineLen);
			if (!line || lineLen == 0) { break; }

			char startTs[32] = {};
			char endTs[32] = {};
			const char* arrow = strstr(line, "-->");
			if (!arrow || arrow < line) { continue; }

			size_t startLen = (size_t)(arrow - line);
			if (startLen > 31) startLen = 31;
			memcpy(startTs, line, startLen);
			startTs[startLen] = 0;

			const char* endStart = arrow + 3;
			const char* lineEnd = line + lineLen;
			endStart = skipWhitespace(endStart, lineEnd);
			size_t endLen = (size_t)(lineEnd - endStart);
			if (endLen > 31) endLen = 31;
			memcpy(endTs, endStart, endLen);
			endTs[endLen] = 0;

			if (!parseTimestamp(startTs, entry.startTime)) { continue; }
			if (!parseTimestamp(endTs, entry.endTime)) { continue; }

			entry.text.clear();
			while (pos < size)
			{
				line = readLine(buffer, size, pos, lineLen);
				if (!line || lineLen == 0) { break; }

				if (!entry.text.empty()) { entry.text += '\n'; }
				entry.text.append(line, lineLen);
			}

			if (!entry.text.empty())
			{
				entries.push_back(entry);
			}
		}

		return !entries.empty();
	}

	bool srt_loadFromFile(const char* path, std::vector<SrtEntry>& entries)
	{
		FileStream file;
		if (!file.open(path, Stream::MODE_READ))
		{
			TFE_System::logWrite(LOG_WARNING, "SrtParser", "Cannot open SRT file: %s", path);
			return false;
		}

		size_t size = file.getSize();
		if (size == 0)
		{
			file.close();
			return false;
		}

		char* buffer = (char*)malloc(size);
		if (!buffer)
		{
			file.close();
			return false;
		}

		file.readBuffer(buffer, (u32)size);
		file.close();

		bool result = srt_parse(buffer, size, entries);
		free(buffer);

		if (result)
		{
			TFE_System::logWrite(LOG_MSG, "SrtParser", "Loaded %zu subtitle entries from %s", entries.size(), path);
		}
		return result;
	}

	const SrtEntry* srt_getActiveEntry(const std::vector<SrtEntry>& entries, f64 timeInSeconds)
	{
		for (size_t i = 0; i < entries.size(); i++)
		{
			if (timeInSeconds >= entries[i].startTime && timeInSeconds < entries[i].endTime)
			{
				return &entries[i];
			}
		}
		return nullptr;
	}
}
