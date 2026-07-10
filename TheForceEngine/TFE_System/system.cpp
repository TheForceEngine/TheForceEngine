#include <TFE_System/system.h>
#include <TFE_System/profiler.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Settings/settings.h>
#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h> 
#include <algorithm>
#include <TFE_Input/input.h>
#include <TFE_Input/replay.h>

#ifdef _WIN32
// Following includes for Windows LinkCallback
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "Shellapi.h"
#include <synchapi.h>
#undef min
#undef max
#elif defined __linux__
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace TFE_System
{
	f64 c_gameTimeScale = 1.02;	// Adjust game time to match DosBox.

	static u64 s_time;
	static u64 s_startTime;
	static u64 s_lastSyncCheck;
	static u64 s_syncCheckDelay;
	static f64 s_freq;
	static f64 s_refreshRate;
	
	static f64 s_dt = 1.0 / 60.0;		// This is just to handle the first frame, so any reasonable value will work.
	static f64 s_dtRaw = 1.0 / 60.0;
	static const f64 c_maxDt = 0.05;	// 20 fps

	static bool s_synced = false;
	static bool s_resetStartTime = false;
	static bool s_quitMessagePosted = false;
	static bool s_systemUiRequestPosted = false;

	static s32 s_missedFrameCount = 0;

	static char s_versionString[64];
	static u32 s_frame = 0;

	void setFrame(u32 frame)
	{
		s_frame = frame;
	}

	u32 getFrame()
	{
		return s_frame;
	}

	void init(f32 refreshRate, bool synced, const char* versionString)
	{
		TFE_System::logWrite(LOG_MSG, "Startup", "TFE_System::init");
		s_time = SDL_GetPerformanceCounter();
		s_lastSyncCheck = s_time;
		s_startTime = s_time;
		TFE_Input::recordReplayTime(s_startTime);

		const u64 timerFreq = SDL_GetPerformanceFrequency();
		s_syncCheckDelay = timerFreq * 10;	// 10 seconds.
		s_freq = 1.0 / (f64)timerFreq;

		s_refreshRate = f64(refreshRate);
		s_synced = synced;

		TFE_COUNTER(s_missedFrameCount, "Miss-Predicted Vysnc Intervals");
		strcpy(s_versionString, versionString);
	}

	void shutdown()
	{
	}

	void setVsync(bool sync)
	{
		s_synced = sync;
		TFE_Settings::getGraphicsSettings()->vsync = sync;
		TFE_RenderBackend::enableVsync(sync);
		s_lastSyncCheck = SDL_GetPerformanceCounter();
	}

	bool getVSync()
	{
		return s_synced;
	}

	const char* getVersionString()
	{
		return s_versionString;
	}

	void resetStartTime()
	{
		s_resetStartTime = true;
	}

	f64 updateThreadLocal(u64* localTime)
	{
		const u64 curTime = SDL_GetPerformanceCounter();
		const u64 uDt = (*localTime) > 0u ? curTime - (*localTime) : 0u;

		const f64 dt = f64(uDt) * s_freq;
		*localTime = curTime;

		return dt;
	}

	u64 getStartTime()
	{
		return s_startTime;
	}

	void setStartTime(u64 startTime)
	{
		s_startTime = startTime;
	}

	u64 getTimePerf()
	{
		return SDL_GetPerformanceCounter();
	}
	
	void update()
	{
		// This assumes that SDL_GetPerformanceCounter() is monotonic.
		// However if errors do occur, the dt clamp later should limit the side effects.
		const u64 curTime = SDL_GetPerformanceCounter();
		const u64 uDt = (curTime > s_time) ? (curTime - s_time) : 1;	// Make sure time is monotonic.		
		s_time = curTime;
		if (s_resetStartTime)
		{
			s_startTime = s_time;
			s_resetStartTime = false;
		}

		// Delta time since the previous frame.
		f64 dt = f64(uDt) * s_freq;

		// If vsync is enabled, then round up to the nearest vsync interval as our delta time.
		// Ideally, if we are hitting the correct framerate, this should always be 
		// 1 vsync interval.
		if (s_synced && s_refreshRate > 0.0f && s_refreshRate < 120.0f)	// If the refresh rate is too high, rounding becomes unreliable.
		{
			const f64 intervals = std::max(1.0, floor(dt * s_refreshRate + 0.1));
			f64 newDt = intervals / s_refreshRate;

			f64 roundDiff = fabs(newDt - dt);
			if (roundDiff > 0.25 / s_refreshRate)
			{
				// Something went wrong, so use the original delta time and check if the refresh rate has changed.
				// Also verify that vsync is still set.
				// Note that checking vsync state and refresh are costly, so don't do it constantly.
				if (curTime - s_lastSyncCheck > s_syncCheckDelay)
				{
					f64 newRate = (f64)TFE_RenderBackend::getDisplayRefreshRate();
					s_refreshRate = newRate ? newRate : s_refreshRate;
					s_synced = TFE_RenderBackend::getVsyncEnabled();
					s_lastSyncCheck = curTime;
				}
				s_missedFrameCount++;
			}
			else
			{
				dt = newDt;
			}
		}

		s_dtRaw = dt;
		// Next make sure that if the current fps is too low, that the game just slows down.
		// This avoids the "spiral of death" when using fixed time steps and avoids issues
		// during loading spikes.
		// This caps the low end framerate before slowdown to 20 fps.
		s_dt = std::min(dt, c_maxDt);
	}

	// Timing
	// Return the delta time.
	f64 getDeltaTime()
	{
		return s_dt;
	}

	f64 getDeltaTimeRaw()
	{
		return s_dtRaw;
	}

	// Get time since "start time", in seconds.
	f64 getTime()
	{
		const u64 uDt = s_time - s_startTime;
		return f64(uDt) * s_freq;
	}
	
	u64 getCurrentTimeInTicks()
	{
		return SDL_GetPerformanceCounter() - s_startTime;
	}

	f64 convertFromTicksToSeconds(u64 ticks)
	{
		return f64(ticks) * s_freq;
	}

	f64 convertFromTicksToMillis(u64 ticks)
	{
		return f64(ticks) * 1000.0 * s_freq;
	}

	f64 microsecondsToSeconds(f64 mu)
	{
		return mu / 1000000.0;
	}

	void getDateTimeString(char* output)
	{
		time_t _tm = time(NULL);
		struct tm* curtime = localtime(&_tm);
		strcpy(output, asctime(curtime));
	}

	void getDateTimeStringForFile(char* output)
	{
		time_t _tm = time(NULL);
		struct tm* curtime = localtime(&_tm);
		strftime(output, 16, "%m-%d-%Y-%H-%M", curtime);
	}

	bool osShellExecute(const char* pathToExe, const char* exeDir, const char* param, bool waitForCompletion)
	{
#ifdef _WIN32
		// Prepare shellExecutInfo
		SHELLEXECUTEINFO ShExecInfo = { 0 };
		ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
		ShExecInfo.fMask = waitForCompletion ? SEE_MASK_NOCLOSEPROCESS : 0;
		ShExecInfo.hwnd = NULL;
		ShExecInfo.lpVerb = NULL;
		ShExecInfo.lpFile = pathToExe;
		ShExecInfo.lpParameters = param;
		ShExecInfo.lpDirectory = exeDir;
		ShExecInfo.nShow = SW_SHOW;
		ShExecInfo.hInstApp = NULL;

		// Execute the file with the parameters
		if (!ShellExecuteEx(&ShExecInfo))
		{
			return false;
		}

		if (waitForCompletion)
		{
			WaitForSingleObject(ShExecInfo.hProcess, INFINITE);
			CloseHandle(ShExecInfo.hProcess);
		}
		return true;
#else
		// On Linux/macOS, use xdg-open / open to delegate to the user's default
		// file manager or application. pathToExe is treated as the path to open.
		pid_t pid = fork();
		if (pid < 0)
		{
			return false;
		}
		if (pid == 0)
		{
			execlp("xdg-open", "xdg-open", pathToExe, nullptr);
			_exit(1); // execlp failed
		}
		if (waitForCompletion)
		{
			int status;
			waitpid(pid, &status, 0);
		}
		return true;
#endif
	}

	void postErrorMessageBox(const char* msg, const char* title)
	{
		TFE_System::logWrite(LOG_ERROR, title, msg);
#ifdef _WIN32
		// Output to a popup message box.
		MessageBoxA(NULL, (LPCSTR)msg, (LPCSTR)title, MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
#endif
	}

	void sleep(u32 sleepDeltaMS)
	{
		SDL_Delay(sleepDeltaMS);
	}

	void postQuitMessage()
	{
		s_quitMessagePosted = true;
	}
		
	void postSystemUiRequest()
	{
		s_systemUiRequestPosted = true;
	}

	bool quitMessagePosted()
	{
		return s_quitMessagePosted;
	}

	bool systemUiRequestPosted()
	{
		bool systemUiPostReq = s_systemUiRequestPosted;
		s_systemUiRequestPosted = false;
		return systemUiPostReq;
	}
}
