#include "filewriterAsync.h"
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#define NOGDI 1
#include <Windows.h>
#endif

namespace FileWriterAsync
{
#ifdef _WIN32
	#define MAX_REQUEST_COUNT 32

	struct WriteRequest
	{
		std::vector<u8> buffer;
		OVERLAPPED file;

		FileWriteCompletionCallback callback;
		void* userData;
	};
	WriteRequest s_requests[MAX_REQUEST_COUNT];
	s32 s_freeRequests[MAX_REQUEST_COUNT];
	s32 s_processRequest[MAX_REQUEST_COUNT];

	std::atomic<s32> s_requestCount{0};
	std::atomic<s32> s_freeRequestCount{0};
	std::atomic<s32> s_processRequestCount{0};

	void CALLBACK fileWrittenCallback(DWORD dwErrorCode, DWORD dwBytesTransferred, LPOVERLAPPED lpOverlapped)
	{
		// Note that this won't be called unless the calling thread is waiting in an "alertable" state.
		const size_t id = (size_t)lpOverlapped->hEvent;
		WriteRequest& request = s_requests[id];

		if (dwErrorCode == 0 && request.callback)	// Success
		{
			request.callback(size_t(dwBytesTransferred), request.userData, u32(dwErrorCode));
		}
		else if (request.callback)  // Failure
		{
			request.callback(0, request.userData, u32(dwErrorCode));
		}

		request.buffer.clear();
		s_freeRequests[s_freeRequestCount++] = id;
	}

	bool writeFileToDisk(const char* path, u8* data, size_t dataSize, FileWriteCompletionCallback completionCallback, void* userData)
	{
		size_t index = 0;
		if (s_freeRequestCount)
		{
			index = s_freeRequests[s_freeRequestCount - 1];
			s_freeRequestCount--;
		}
		else if (s_requestCount < MAX_REQUEST_COUNT)
		{
			index = s_requestCount;
			s_requestCount++;
		}
		else
		{
			return false;
		}

		WriteRequest* request = &s_requests[index];
		request->buffer.resize(dataSize);
		memcpy(request->buffer.data(), data, dataSize);

		request->callback = completionCallback;
		request->userData = userData;

		HANDLE hFile = CreateFile(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_FLAG_OVERLAPPED, NULL);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			TFE_System::logWrite(LOG_ERROR, "AsyncFileWrite", "Cannot create file handle for: %s", path);
			return false;
		}

		memset(&request->file, 0, sizeof(OVERLAPPED));
		request->file.Offset = 0;
		request->file.OffsetHigh = 0;
		request->file.hEvent = HANDLE(index);

		if (!WriteFileEx(hFile, request->buffer.data(), DWORD(dataSize), &request->file, fileWrittenCallback))
		{
			TFE_System::logWrite(LOG_ERROR, "AsyncFileWrite", "Cannot write file: %s", path);
			return false;
		}

		CloseHandle(hFile);
		return true;
	}
#endif
};
