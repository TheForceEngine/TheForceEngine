#pragma once
//////////////////////////////////////////////////////////////////////
// The Force Engine System Library
// System functionality, such as timers and logging.
//////////////////////////////////////////////////////////////////////

#include <ctype.h>
#include "types.h"

#define TFE_MAJOR_VERSION 1
#define TFE_MINOR_VERSION 10
#define TFE_BUILD_VERSION 0

#if defined(_WIN32) || defined(BUILD_EDITOR)
#define ENABLE_EDITOR 1
#endif

enum LogWriteType
{
	LOG_MSG = 0,
	LOG_WARNING,
	LOG_ERROR,
	LOG_CRITICAL,		//Critical log messages are CRITICAL errors that also act as asserts when attached to a debugger.
	LOG_COUNT
};

namespace TFE_System
{
	void init(f32 refreshRate, bool synced, const char* versionString);
	void shutdown();
	void resetStartTime();
	void setVsync(bool sync);
	bool getVSync();

	void update();
	f64 updateThreadLocal(u64* localTime);

	// Timing
	// --- The current time and delta time are determined once per frame, during the update() function.
	//     In other words an entire frame operates on a single instance of time.
	// Return the delta time.
	f64 getDeltaTime();
	f64 getDeltaTimeRaw();
	// Get the absolute time since the last start time, in seconds.
	f64 getTime();

	u64 getCurrentTimeInTicks();
	f64 convertFromTicksToSeconds(u64 ticks);
	f64 convertFromTicksToMillis(u64 ticks);
	f64 microsecondsToSeconds(f64 mu);
	u64 getStartTime();
	void setStartTime(u64 startTime);

	void getDateTimeString(char* output);
	void getDateTimeStringForFile(char* output);
	bool parseDateTimeString(const char* str, s64* outKey);

	// Log
	void logTimeToggle();
	bool logOpen(const char* filename, bool append=false);
	void logClose();
	void logWrite(LogWriteType type, const char* tag, const char* str, ...);
	void openRotatingLog(const char* fileName, bool append=false);

	// Lighter weight debug output (only useful when running in a terminal or debugger).
	void debugWrite(const char* tag, const char* str, ...);

	// System
	bool osShellExecute(const char* pathToExe, const char* exeDir, const char* param, bool waitForCompletion);
	void postErrorMessageBox(const char* msg, const char* title);
	void sleep(u32 sleepDeltaMS);

	void postQuitMessage();
	bool quitMessagePosted();

	void postSystemUiRequest();
	bool systemUiRequestPosted();

	const char* getVersionString();

	extern f64 c_gameTimeScale;

	void setFrame(u32 frame);
	u32 getFrame();
}

// _strlwr/_strupr exist on Windows; roll our own
// and use them throghout. This works on Windows
// and Linux/unix-like.
static inline void __strlwr(char *c)
{
	while (*c) {
		*c = tolower(*c);
		c++;
	}
}

static inline void __strupr(char *c)
{
	while (*c) {
		*c = toupper(*c);
		c++;
	}
}