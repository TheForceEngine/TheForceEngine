#include "shell.h"
#ifdef _WIN32
#include <Windows.h>
#include <io.h>
#endif

void osShellOpenUrl(const char* url)
{
#ifdef _WIN32
	ShellExecute(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#endif
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
	ShExecInfo.nShow = SW_SHOWNORMAL;
	ShExecInfo.hInstApp = NULL;

	// Get window handle for hide/show
	HWND tfeWindow = GetActiveWindow();

	// Execute the file with the parameters
	if (!ShellExecuteEx(&ShExecInfo))
	{
		return false;
	}

	if (waitForCompletion)
	{
		ShowWindow(tfeWindow, SW_MINIMIZE);
		WaitForSingleObject(ShExecInfo.hProcess, INFINITE);
		CloseHandle(ShExecInfo.hProcess);
		ShowWindow(tfeWindow, SW_RESTORE);
	}
	return true;
#endif
	return false;
}