#include <cstdarg>
#include <cinttypes>
#include <cstring>

#include <TFE_System/system.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_FrontEndUI/frontEndUi.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctime>
#include <chrono>

#ifdef _WIN32
	#include <Windows.h>
	#include <io.h>
#endif

namespace TFE_System
{
	static FileStream s_logFile;
	static char s_workStr[32768];
	static char s_msgStr[32768];
	static const char* c_typeNames[]=
	{
		"",			//LOG_MSG = 0,
		"Warning",	//LOG_WARNING,
		"Error",	//LOG_ERROR,
		"Critical", //LOG_CRITICAL,
	};

	bool includeTime = true;
	int maxLogRotations = 3;

	void logTimeToggle()
	{
		includeTime = !includeTime;
	}

	bool logOpen(const char* filename, bool append)
	{
		char logPath[TFE_MAX_PATH];
		TFE_Paths::appendPath(PATH_USER_DOCUMENTS, filename, logPath);
		if (append)
		{
			return s_logFile.open(logPath, Stream::MODE_APPEND);
		}
		else
		{
			return s_logFile.open(logPath, Stream::MODE_WRITE);
		}
	}

	void logClose()
	{
		s_logFile.close();
	}

	void debugWrite(const char* tag, const char* str, ...)
	{
		if (!tag || !str) { return; }

		//Handle the variable input, "printf" style messages
		va_list arg;
		va_start(arg, str);
		vsprintf(s_msgStr, str, arg);
		va_end(arg);

		sprintf(s_workStr, "[%s] %s\r\n", tag, s_msgStr);

		//Write to the debugger or terminal output.
#ifdef _WIN32
		OutputDebugStringA(s_workStr);
#else
		fprintf(stderr, "%s", s_workStr);
#endif
	}

	void logWrite(LogWriteType type, const char* tag, const char* str, ...)
	{
		if (type >= LOG_COUNT || !s_logFile.isOpen() || !tag || !str) { return; }

		auto now = std::chrono::system_clock::now();
		std::time_t now_c = std::chrono::system_clock::to_time_t(now);
		std::tm now_tm;

#ifdef _WIN32
		localtime_s(&now_tm, &now_c);  // For thread safety on Windows
#else
		localtime_r(&now_c, &now_tm);  // For thread safety on Linux
#endif

		auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
			now.time_since_epoch()) % 1000;

		char timeStr[40];
		if (includeTime)
		{
			strftime(timeStr, sizeof(timeStr) - 4, "%Y-%b-%d %H:%M:%S", &now_tm); // Leave space for milliseconds

			// Add milliseconds to the formatted time
			snprintf(timeStr + strlen(timeStr), 8, ".%03" PRIu64 " - ", milliseconds.count());
		}
		else
		{
			timeStr[0] = 0;
		}

		//Handle the variable input, "printf" style messages
		va_list arg;
		va_start(arg, str);
		vsprintf(s_msgStr, str, arg);
		va_end(arg);

		//Format the message		
		if (type != LOG_MSG)
		{
			sprintf(s_workStr, "%s[%s : %s] %s\r\n", timeStr, c_typeNames[type], tag, s_msgStr);
		}
		else
		{
			sprintf(s_workStr, "%s[%s] %s\r\n", timeStr, tag, s_msgStr);
		}

		//Write to disk
		s_logFile.writeBuffer(s_workStr, (u32)strlen(s_workStr));
		//Make sure to flush the file to disk if a crash is likely.
		//if (type == LOG_ERROR || type == LOG_CRITICAL)
		{
			s_logFile.flush();
		}
		//Write to the debugger or terminal output.
#ifdef _WIN32
		OutputDebugStringA(s_workStr);
#else
		fprintf(stderr, "%s", s_workStr);
#endif
		//Critical log messages also act as asserts in the debugger.
		if (type == LOG_CRITICAL)
		{
			assert(0);
		}

		sprintf(s_workStr, "%s - [%s] %s", timeStr, tag, s_msgStr);
		size_t len = strlen(s_msgStr);
		char* msg = s_msgStr;
		char* msgStart = msg;
		for (size_t i = 0; i < len; i++)
		{
			if (msg[i] == '\n')
			{
				msg[i] = 0;
				TFE_FrontEndUI::logToConsole(msgStart);

				msgStart = msg + i + 1;
			}
		}
		if (msgStart < s_msgStr + len)
		{
			TFE_FrontEndUI::logToConsole(msgStart);
		}
	}

	void openRotatingLog(const char* fileName, bool append)
	{
		if (!fileName) { return; }

		char logPath[TFE_MAX_PATH];
		TFE_Paths::appendPath(PATH_USER_DOCUMENTS, fileName, logPath);

		// Handle rotations
		for (int i = maxLogRotations; i > 0; i--)
		{
			char oldLogPath[TFE_MAX_PATH];
			char newLogPath[TFE_MAX_PATH];
			if (i == 1)
			{
				sprintf(oldLogPath, "%s", logPath);
			}
			else
			{
				sprintf(oldLogPath, "%s.%d", logPath, i - 1);
			}

			if (FileUtil::exists(oldLogPath))
			{
				sprintf(newLogPath, "%s.%d", logPath, i);
				FileUtil::copyFile(oldLogPath, newLogPath);
			}
		}

		// Open file as normal
		logOpen(fileName, append);
	}
}
	