#include "dcssParser.h"
#include <TFE_FileSystem/filestream.h>
#include <TFE_System/system.h>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstring>

//============================================================================
// DCSS parser implementation
//============================================================================
//
// The remaster's parser (decompiled from khonsu at sub_140073940) is a small
// state machine that walks the file a line at a time. We mirror that
// structure here rather than, say, using TFE_Parser, because:
//
//   1. The DCSS format is block-oriented (index / timestamp / directives /
//      blank line), not key-value. TFE_Parser is great for INI-ish data; it
//      would fight us on the blank-line-as-separator rule.
//
//   2. We want to match the remaster's quirks exactly, including timestamps
//      with typos that parse correctly (e.g. "00:00:58:827" where the ms
//      separator should have been a comma). A stricter parser would reject
//      those and desync the stock .dcss files.
//
//   3. The whole thing is <500 lines of state. No framework needed.
//
namespace TFE_DarkForces
{
	// ----------------------------------------------------------------------
	// Timestamp parser
	// ----------------------------------------------------------------------
	//
	// Canonical form is SRT's "HH:MM:SS,mmm". Real life is messier: the
	// stock remaster ships kflyby.dcss with "00:00:58:827" (colon before ms)
	// and logo.dcss with "00:1:50,487" (minute field missing a leading
	// zero). Neither of these would parse with a strict sscanf("%d:%d:%d,%d")
	// pattern.
	//
	// So we walk character by character. Any of ':' ',' '.' ends the current
	// field. Digit counts per field are free-form (we cap ms at 3 digits to
	// avoid overflow, but that's the only limit).
	//
	// Returns false on total garbage, including:
	//   - A separator before any digit ("unexpected punctuation")
	//   - Too few fields (< 3 colon-separated groups; i.e. no hours field)
	//   - Any non-digit, non-separator, non-whitespace character
	//
	static bool parseTimestampMs(const char* line, size_t len, u64& outMs)
	{
		u64 fields[4] = {};       // hours, minutes, seconds, milliseconds
		s32 fieldIdx = 0;         // which of the four we're currently filling
		s32 digits = 0;           // digits seen in current field (for ms cap)
		bool anyDigit = false;    // have we seen *any* digit yet?

		for (size_t i = 0; i < len && fieldIdx < 4; i++)
		{
			char c = line[i];
			if (c >= '0' && c <= '9')
			{
				// Drop extra ms digits rather than overflow. "123456" ms -> 123.
				if (fieldIdx == 3 && digits >= 3) { continue; }
				fields[fieldIdx] = fields[fieldIdx] * 10 + (u64)(c - '0');
				digits++;
				anyDigit = true;
			}
			else if (c == ':' || c == ',' || c == '.')
			{
				// Any of these ends the current field. This is what lets us
				// accept "00:00:58:827" (all colons) as the same thing as
				// "00:00:58,827" - the remaster's parser does the same.
				if (!anyDigit) { return false; }  // "::01" is garbage
				fieldIdx++;
				digits = 0;
			}
			else if (c == ' ' || c == '\t')
			{
				// Silent-tolerate whitespace, primarily for trailing space at
				// end of line. Inline whitespace between digits of a single
				// field would still break since we'd keep reading digits.
			}
			else
			{
				return false;
			}
		}

		// Need at least HH:MM:SS (fieldIdx advances on separators, so three
		// fields means we saw at least two separators = fieldIdx >= 2). A
		// bare "12:34" gets rejected here, as intended.
		if (fieldIdx < 2) { return false; }

		outMs = fields[3] + 1000 * (fields[2] + 60 * (fields[1] + 60 * fields[0]));
		return true;
	}

	// ----------------------------------------------------------------------
	// Line reader
	// ----------------------------------------------------------------------
	//
	// Returns a pointer to the next line's first character and writes its
	// length (excluding the EOL bytes) into lineLen. Advances `pos` past the
	// line terminator (handles LF, CRLF, or lone CR). Returns nullptr at EOF.
	//
	// The returned pointer is NOT null-terminated - it's a view into the
	// caller's buffer. Downstream consumers must respect lineLen.
	//
	static const char* readLine(const char* buffer, size_t size, size_t& pos, size_t& lineLen)
	{
		if (pos >= size) { return nullptr; }
		const char* start = buffer + pos;
		const char* end = buffer + size;
		const char* p = start;
		while (p < end && *p != '\n' && *p != '\r') { p++; }
		lineLen = (size_t)(p - start);

		// Consume whichever EOL style is present. CRLF is two chars, LF or
		// lone CR is one.
		if (p < end && *p == '\r') { p++; }
		if (p < end && *p == '\n') { p++; }
		pos = (size_t)(p - buffer);
		return start;
	}

	// ----------------------------------------------------------------------
	// Helpers
	// ----------------------------------------------------------------------

	// "Is this line nothing but whitespace?" Blank lines are the entry
	// separator in DCSS, so we need a strict definition.
	static bool lineIsBlank(const char* line, size_t len)
	{
		for (size_t i = 0; i < len; i++)
		{
			if (line[i] != ' ' && line[i] != '\t') { return false; }
		}
		return true;
	}

	// Comments start at the first non-whitespace character if it's '#' or
	// '//'. This isn't in the remaster's format - we added it for modder
	// convenience (so you can annotate what each cue is for).
	static bool lineIsComment(const char* line, size_t len)
	{
		size_t i = 0;
		while (i < len && (line[i] == ' ' || line[i] == '\t')) { i++; }
		if (i >= len) { return false; }
		if (line[i] == '#') { return true; }
		if (i + 1 < len && line[i] == '/' && line[i + 1] == '/') { return true; }
		return false;
	}

	// "%.*s" expects an int for its length argument. size_t is usually wider
	// than int on 64-bit, so clamp it to avoid implementation-defined
	// narrowing when logging user-controlled line data.
	static int logLen(size_t n)
	{
		return (n > (size_t)INT_MAX) ? INT_MAX : (int)n;
	}

	// Parse an integer from a non-null-terminated buffer view. We copy into
	// a tiny local buffer and null-terminate, then strtol it. Done this way
	// because strtol needs a null-terminated string and we can't safely
	// assume there's one after (line + len) in the caller's buffer.
	//
	// 32 bytes is plenty - any integer we care about fits in 11 digits plus
	// sign, and DCSS integers are in range [0, 127] at the extremes.
	static s32 parseIntBounded(const char* line, size_t len)
	{
		char buf[32];
		size_t n = (len < sizeof(buf) - 1) ? len : sizeof(buf) - 1;
		memcpy(buf, line, n);
		buf[n] = 0;
		return (s32)strtol(buf, nullptr, 10);
	}

	// Directive prefix match: "seq: " / "cue: " / "musicvol: ". The trailing
	// space is part of the prefix to avoid false matches (e.g. "sequel: 5"
	// wouldn't match "seq: ").
	static bool startsWith(const char* line, size_t len, const char* prefix)
	{
		size_t plen = strlen(prefix);
		return len >= plen && memcmp(line, prefix, plen) == 0;
	}

	// After a prefix match, parse the trailing integer. Skips the prefix
	// bytes and runs the same bounded-int routine over what's left.
	static s32 parseIntAfter(const char* line, size_t len, const char* prefix)
	{
		size_t plen = strlen(prefix);
		if (len <= plen) { return 0; }
		char buf[32];
		size_t n = len - plen;
		if (n >= sizeof(buf)) { n = sizeof(buf) - 1; }
		memcpy(buf, line + plen, n);
		buf[n] = 0;
		return (s32)strtol(buf, nullptr, 10);
	}

	// ----------------------------------------------------------------------
	// Main parse loop
	// ----------------------------------------------------------------------
	//
	// State machine (rough):
	//
	//   start ---[+credits]---> header_flags
	//   start ---[+openingcredits]---> header_flags
	//   start ---[digit]---> expect_timestamp ---> expect_directives ---> (blank) ---> start
	//
	// We're lenient about:
	//   - Extra blank lines between entries
	//   - Comments anywhere (top, between entries, inside an entry's
	//     directive block)
	//   - Unknown directives (silently ignored for forward compatibility)
	//   - Out-of-order entry indices (logged, but accepted; we re-sort at
	//     the end by timestamp)
	//
	bool dcss_parse(const char* buffer, size_t size, DcssScript& out)
	{
		out.creditsFlag = false;
		out.openingCreditsFlag = false;
		out.entries.clear();
		if (!buffer || size == 0) { return false; }

		size_t pos = 0;

		// Skip a UTF-8 BOM if present. The remaster's own parser does this
		// (khonsu:250830-ish); some text editors insert a BOM by default on
		// Windows, so it's worth handling gracefully.
		if (size >= 3 && (u8)buffer[0] == 0xEF && (u8)buffer[1] == 0xBB && (u8)buffer[2] == 0xBF)
		{
			pos = 3;
		}

		s32 expectedIndex = 1;   // For out-of-order warnings only.
		const char* line = nullptr;
		size_t lineLen = 0;

		while (pos < size)
		{
			// Skip leading blank lines and comments between blocks. The
			// do/while is so we always read at least one line - the outer
			// while(pos<size) might still have a trailing blank to consume.
			do
			{
				line = readLine(buffer, size, pos, lineLen);
				if (!line) { goto done; }
			} while (lineIsBlank(line, lineLen) || lineIsComment(line, lineLen));

			// Header flags may precede the first numbered entry. Once we've
			// started parsing entries, "+credits" mid-file would be weird
			// and we let it fail as an invalid index below.
			if (out.entries.empty())
			{
				if (startsWith(line, lineLen, "+credits"))         { out.creditsFlag = true; continue; }
				if (startsWith(line, lineLen, "+openingcredits"))  { out.openingCreditsFlag = true; continue; }
			}

			// Entry starts. First line should be the index.
			s32 index = parseIntBounded(line, lineLen);
			if (index <= 0)
			{
				// Not a number (or zero/negative). This is a user mistake -
				// log what we saw and resume scanning. Most common cause:
				// a typo or a directive that leaked outside an entry.
				TFE_System::logWrite(LOG_WARNING, "DcssParser", "Expected numeric index, got '%.*s'",
					logLen(lineLen), line);
				continue;
			}

			// SRT-style indices should be 1, 2, 3, ... A skip or duplicate
			// is almost certainly a mistake; warn but don't reject - the
			// final sort by timestamp will put things in dispatch order
			// regardless.
			if (index != expectedIndex)
			{
				TFE_System::logWrite(LOG_WARNING, "DcssParser", "Out-of-order index %d (expected %d)",
					index, expectedIndex);
			}
			expectedIndex = index + 1;

			// Second line: the timestamp.
			line = readLine(buffer, size, pos, lineLen);
			if (!line) { break; }  // Truncated file.

			DcssEntry entry = {};
			entry.index = index;
			if (!parseTimestampMs(line, lineLen, entry.timeMs))
			{
				TFE_System::logWrite(LOG_WARNING, "DcssParser", "Bad timestamp '%.*s' at index %d",
					logLen(lineLen), line, index);
				continue;
			}

			// Then zero or more directive lines, until a blank line or EOF.
			// Comments inside the directive block are skipped silently.
			while (pos < size)
			{
				line = readLine(buffer, size, pos, lineLen);
				if (!line || lineIsBlank(line, lineLen)) { break; }
				if (lineIsComment(line, lineLen)) { continue; }

				if      (startsWith(line, lineLen, "seq: "))      { entry.seq      = parseIntAfter(line, lineLen, "seq: "); }
				else if (startsWith(line, lineLen, "cue: "))      { entry.cue      = parseIntAfter(line, lineLen, "cue: "); }
				else if (startsWith(line, lineLen, "musicvol: ")) { entry.musicVol = parseIntAfter(line, lineLen, "musicvol: "); }
				// Unknown directive? Silently ignored. This is intentional:
				// if a future TFE (or the remaster) adds a new directive,
				// we want old files to still parse and new files to still
				// work with old builds. The price is that a typo like
				// "ceu:" silently does nothing, but that's acceptable given
				// warnings for index/timestamp errors above.
			}

			out.entries.push_back(entry);
		}

done:
		// Runtime dispatch expects entries in strict ascending time order
		// (it just walks forward). Every stock file is already sorted, but
		// a hand-edited DCSS might not be - sort defensively here so the
		// dispatcher can stay dumb and fast.
		std::stable_sort(out.entries.begin(), out.entries.end(),
			[](const DcssEntry& a, const DcssEntry& b) { return a.timeMs < b.timeMs; });

		// "Parsed something useful" covers: at least one entry, OR at least
		// one header flag. An empty file with just "+credits" is a valid
		// (if unusual) DCSS.
		return !out.entries.empty() || out.creditsFlag || out.openingCreditsFlag;
	}

	// ----------------------------------------------------------------------
	// File loader
	// ----------------------------------------------------------------------
	//
	// Read the whole file into memory, hand it to dcss_parse. DCSS files are
	// tiny (< 1 KB in stock data, a few KB tops even for a long cutscene) so
	// we don't bother with streaming I/O.
	//
	bool dcss_loadFromFile(const char* path, DcssScript& out)
	{
		FileStream file;
		if (!file.open(path, Stream::MODE_READ))
		{
			// Silent: "no DCSS for this scene" is a legitimate state for
			// modders who just want the OGV to play without timed music.
			// The caller will see false and fall back to the scene's
			// cutscene.lst music_seq.
			return false;
		}

		size_t size = file.getSize();
		if (size == 0) { file.close(); return false; }

		char* buffer = (char*)malloc(size);
		if (!buffer) { file.close(); return false; }

		file.readBuffer(buffer, (u32)size);
		file.close();

		bool ok = dcss_parse(buffer, size, out);
		free(buffer);

		if (ok)
		{
			// One line per successfully-loaded DCSS, captured at INFO so
			// it's visible in the default log. Helpful when diagnosing
			// "why isn't my cue firing" - the number of entries here
			// should match what you wrote.
			TFE_System::logWrite(LOG_MSG, "DcssParser",
				"Loaded %zu cue entries from %s (credits=%d openingCredits=%d)",
				out.entries.size(), path, out.creditsFlag ? 1 : 0, out.openingCreditsFlag ? 1 : 0);
		}
		return ok;
	}
}
