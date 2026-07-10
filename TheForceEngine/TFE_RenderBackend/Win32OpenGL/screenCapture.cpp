#include <cstring>

#include "screenCapture.h"
#include "openGL_Caps.h"
#include "../renderBackend.h"
#include <TFE_System/system.h>
#include <TFE_Asset/imageAsset.h>	// For image saving, this should be refactored...
#include <TFE_Asset/gifWriter.h>
#include "gl.h"
#include <assert.h>
#include <TFE_Settings/settings.h>
#include <TFE_Ui/imGUI/imgui.h>

#ifdef _DEBUG
	#define CHECK_GL_ERROR checkGlError();
#else
	#define CHECK_GL_ERROR
#endif
#define CAPTURE_FRAME_DELAY 3
#define FLUSH_READ_COUNT 1
#define RECORD_FLUSH_COUNT 1

namespace
{
	void checkGlError()
	{
		GLenum error = glGetError();
		if (error == GL_NO_ERROR) { return; }

		TFE_System::logWrite(LOG_ERROR, "Dynamic Texture", "GL Error = %x", error);
		assert(error == GL_NO_ERROR);
	}
}

ScreenCapture::~ScreenCapture()
{
	freeBuffers();
}

bool ScreenCapture::create(u32 width, u32 height, u32 bufferCount)
{
	m_width  = width;
	m_height = height;

	return changeBufferCount(bufferCount);
}

void ScreenCapture::resize(u32 newWidth, u32 newHeight)
{
	if (newWidth == m_width && newHeight == m_height) { return; }
	// Flush any pending screenshots.
	update(true);

	m_width = newWidth;
	m_height = newHeight;
	changeBufferCount(m_bufferCount, true);
}

bool ScreenCapture::changeBufferCount(u32 newBufferCount, bool forceRealloc/* = false*/)
{
	if (newBufferCount == m_bufferCount && !forceRealloc) { return false; }
	freeBuffers();

	m_bufferCount = newBufferCount;
	m_writeBuffer = 0;

	const size_t bufferSize = m_width * m_height * 4;
	if (OpenGL_Caps::supportsPbo())
	{
		m_stagingBuffers = new u32[m_bufferCount];
		glGenBuffers(m_bufferCount, m_stagingBuffers);
		for (u32 i = 0; i < m_bufferCount; i++)
		{
			glBindBuffer(GL_PIXEL_PACK_BUFFER, m_stagingBuffers[i]);
			glBufferData(GL_PIXEL_PACK_BUFFER, bufferSize, nullptr, GL_STREAM_READ);
		}
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		CHECK_GL_ERROR
	}

	delete[] m_captures;
	m_captures = new Capture[m_bufferCount];
	m_captureHead = 0;
	m_captureCount = 0;
	for (u32 i = 0; i < m_bufferCount; i++)
	{
		m_captures[i].bufferIndex = 0;
		m_captures[i].frame = -1;
		m_captures[i].imageData.resize(bufferSize);
	}

	delete[] m_readIndex;
	m_readIndex = new u32[m_bufferCount];
	m_readCount = 0;

	return m_bufferCount;
}

static void flipVert32bpp(void* mem, u32 w, u32 h)
{
	const u32 stride = w * 4;
	const u32 size = stride * h;
	char* upper = (char *)mem;
	char* lower = (char *)mem + size - stride;
	char* tmpb = (char *)malloc(stride);
	while (tmpb && (upper < lower)) {
		memcpy(tmpb, upper, stride);
		memcpy(upper, lower, stride);
		memcpy(lower, tmpb, stride);
		upper += stride;
		lower -= stride;
	}
	free(tmpb);
}

void ScreenCapture::captureFrontBufferToMemory(u32* mem)
{
	glReadBuffer(GL_FRONT);
	glReadPixels(0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, mem);

	// Need to flip image upside-down. OpenGL has (0|0) at lower left
	// corner, while the rest of the world places it in the upper left.
	flipVert32bpp(mem, m_width, m_height);
}

void ScreenCapture::update(bool flush)
{
	// Automatically capture frames when recording.
	if (m_state == RECORDING)
	{
		DisplayInfo displayInfo;
		TFE_RenderBackend::getDisplayInfo(&displayInfo);

		f64 recordingTime = 0.0;
		if (displayInfo.refreshRate)
		{
			recordingTime = f64(m_frame - m_recordingFrameStart) / f64(displayInfo.refreshRate);
		}
		else if (m_recordingTimeStart == 0.0f)
		{
			m_recordingTimeStart = TFE_System::getTime();
			recordingTime = 0.0f;
		}
		else
		{
			recordingTime = f32(TFE_System::getTime() - m_recordingTimeStart);
		}

		f64 recordingFrame = floor(recordingTime * TFE_Settings::getSystemSettings()->gifRecordingFramerate);
		if (m_recordingFrameLast != recordingFrame)
		{
			captureFrame("");
			m_recordingFrameLast = recordingFrame;
		}
	}

	// Handle capture
	m_frame++;
	if (!m_captureCount) { return; }
	
	bool popHead = false;
	if (!OpenGL_Caps::supportsPbo())
	{
		glReadBuffer(GL_BACK);
		glReadPixels(0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, m_captures[m_captureHead].imageData.data());
		popHead = true;
	}
	else if (flush || m_frame > (m_captures[m_captureHead].frame + CAPTURE_FRAME_DELAY))
	{
		// Copy from staging data to read buffer [readBuffer].
		glBindBuffer(GL_PIXEL_PACK_BUFFER, m_stagingBuffers[m_captures[m_captureHead].bufferIndex]);
		void* imageData = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, m_captures[m_captureHead].imageData.size(), GL_MAP_READ_BIT);
		memcpy(m_captures[m_captureHead].imageData.data(), imageData, m_captures[m_captureHead].imageData.size());
		glUnmapBuffer(GL_PIXEL_PACK_BUFFER);

		// Cleanup.
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		CHECK_GL_ERROR

		popHead = true;
	}

	if (popHead)
	{
		m_readIndex[m_readCount++] = m_captureHead;
		m_captureHead = (m_captureHead + 1) % m_bufferCount;
		m_captureCount--;
	}

	// Handle write to disk or adding a frame to the recording.
	if (m_state == RECORDING && (m_readCount >= RECORD_FLUSH_COUNT || flush))
	{
		// Save the images.
		recordImages();
	}
	else if (m_state != RECORDING && (m_readCount >= FLUSH_READ_COUNT || flush))
	{
		writeFramesToDisk();
	}
}

void ScreenCapture::captureFrame(const char* outputPath)
{
	if (m_bufferCount > 1 && OpenGL_Caps::supportsPbo())
	{
		// Async copy from GPU data to staging buffer [writeBuffer].
		glBindBuffer(GL_PIXEL_PACK_BUFFER, m_stagingBuffers[m_writeBuffer]);
		glReadPixels(0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	}
	const u32 index = (m_captureHead + m_captureCount) % m_bufferCount;
	m_captures[index].bufferIndex = m_writeBuffer;
	m_captures[index].outputPath = outputPath;
	m_captures[index].frame = m_frame;
	m_captureCount++;

	// Update buffer indices.
	m_writeBuffer = (m_writeBuffer + 1) % m_bufferCount;
}

void ScreenCapture::beginRecording(const char* path, bool skipCountdown)
{
	m_capturePath = string(path);
	if (skipCountdown)
	{
		// This will flash the countdown timer UI for an instant before
		// capture starts, so the user still sees some indication that
		// capture is working.
		startCountdown(false);
		f32 framerateLimit = (f32)TFE_Settings::getGraphicsSettings()->frameRateLimit;
		if (framerateLimit <= 0.0f) { framerateLimit = 20.0f; }
		f32 messageDelay = 2.0f / framerateLimit;
		if (messageDelay < 0.1f) { messageDelay = 0.1f; }
		m_countdownTimeStart -= (COUNTDOWN_DURATION - messageDelay);
	}
	else
	{
		startCountdown(true);
	}
}

void ScreenCapture::startCountdown(bool fullCountdown)
{
	m_countdownTimeStart = TFE_System::getTime();
	m_state = COUNTDOWN;
	m_showFullCountdown = fullCountdown;
	m_countdownFinished = false;
}

void ScreenCapture::startCapture()
{
	m_state = RECORDING;
	m_recordingFrame = 0;
	m_recordingFrameStart = m_frame;
	m_recordingTimeStart = 0.0;
	m_recordingFrameLast = -1.0;

	u32 framerate = (u32)TFE_Settings::getSystemSettings()->gifRecordingFramerate;
	TFE_GIF::startGif(m_capturePath.c_str(), m_width, m_height, framerate);
}

void ScreenCapture::endRecording()
{
	if (m_state == RECORDING)
	{
		update(true);

		TFE_GIF::write();

		m_state = CONFIRMATION;
		m_confirmationTimeStart = TFE_System::getTime();
		m_confirmationMessage = string("GIF saved to " + m_capturePath);
	}
	else 
	{
		m_state = IDLE;
	}
}

ScreenCapture::ScreenCaptureState ScreenCapture::getState()
{
	return m_state;
}

bool ScreenCapture::wantsToDrawGui()
{
	return m_state == COUNTDOWN || m_state == CONFIRMATION;
}

void ScreenCapture::drawGui()
{
	if (m_state == COUNTDOWN)
	{
		f64 elapsed = TFE_System::getTime() - m_countdownTimeStart;
		if (elapsed > COUNTDOWN_DURATION) {
			if (m_countdownFinished) { startCapture(); }
			else { m_countdownFinished = true; }
			return;
		}

		//Create window.
		ImVec2 windowSize = ImVec2(0, 0); // Auto-size.
		ImGui::SetNextWindowSize(windowSize);
		ImGui::SetNextWindowPos(ImVec2(10, 10));
		ImGui::Begin("##CaptureStart", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1)); // Font color.

		// Draw text.
		if (m_showFullCountdown)
		{
			if (elapsed < COUNTDOWN_DURATION / 3.0) { ImGui::Text("3..."); }
			else if (elapsed < COUNTDOWN_DURATION * (2.0 / 3.0)) { ImGui::Text("2..."); }
			else if (elapsed <= COUNTDOWN_DURATION) { ImGui::Text("1..."); }
		}
		else
		{
			ImGui::Text("REC");
		}

		// End window.
		ImGui::PopStyleColor(); // Font color.
		ImGui::End();
	}
	else if (m_state == CONFIRMATION)
	{
		f64 elapsed = TFE_System::getTime() - m_confirmationTimeStart;
		if (elapsed > CONFIRMATION_DURATION)
		{
			m_state = IDLE;
			return;
		}

		ImVec2 windowSize;

		// Fixed-width with wrap if we show full path; otherwise, auto-size.
		if (TFE_Settings::getSystemSettings()->showGifPathConfirmation) { windowSize = ImVec2(CONFIRMATION_WIDTH, 0); }
		else { windowSize = ImVec2(0, 0); }

		// Create window.
		ImGui::SetNextWindowSize(windowSize);
		ImGui::SetNextWindowPos(ImVec2(10, 10));
		ImGui::Begin("##CaptureEnd", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);

		// Draw text.
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0, 1));
		if (TFE_Settings::getSystemSettings()->showGifPathConfirmation)
		{
			ImGui::TextWrapped("%s", m_confirmationMessage.c_str());
		}
		else
		{
			ImGui::Text("Recording saved");
		}

		// End window.
		ImGui::PopStyleColor(); // Font color
		ImGui::End();
	}
}

void ScreenCapture::writeFramesToDisk()
{
	if (!m_readCount) { return; }

	for (u32 i = 0; i < m_readCount; i++)
	{
		const u32 index = m_readIndex[i];
		TFE_Image::writeImage(m_captures[index].outputPath.c_str(), m_width, m_height, (u32*)m_captures[index].imageData.data());
	}
	m_readCount = 0;
}

void ScreenCapture::recordImages()
{
	if (!m_readCount) { return; }

	for (u32 i = 0; i < m_readCount; i++)
	{
		const u32 index = m_readIndex[i];
		TFE_GIF::addFrame(m_captures[index].imageData.data());
	}
	m_readCount = 0;
}

void ScreenCapture::freeBuffers()
{
	if (OpenGL_Caps::supportsPbo())
	{
		if (m_bufferCount)
		{
			glDeleteBuffers(m_bufferCount, m_stagingBuffers);
		}
		delete[] m_stagingBuffers;
	}
	delete[] m_readIndex;
	m_readIndex = nullptr;

	m_bufferCount = 0;
}
