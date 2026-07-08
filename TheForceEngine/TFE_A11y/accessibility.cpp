#include <cstring>
#include <chrono>
#include <map>

#include "accessibility.h"
#include <TFE_A11y/filePathList.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_FrontEndUI/console.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Settings/settings.h>
#include <TFE_System/parser.h>
#include <TFE_System/system.h>
#include <TFE_Ui/ui.h>

using namespace std::chrono;
using std::string;

namespace TFE_A11Y  // a11y is industry slang for accessibility
{
	///////////////////////////////////////////
	// Forward Declarations
	///////////////////////////////////////////
	
	// Clear the list of caption and font files. Note that this does not unload fonts
	// from ImGui (IIRC it's not practical to do so).
	void clearFiles();
	// Find caption and font files.
	void findFiles();
	// Get all font file names from the Fonts directories; we will use this to populate the
	// dropdown in the Accessibility settings menu.
	void findFontFiles();
	bool isFontLoaded();
	void loadDefaultFont(bool clearAtlas);
	void tryLoadFont(const string path, bool clearAtlas);
	void loadFont(const string path, bool clearAtlas);
	void findCaptionFiles();
	bool filterCaptionFile(const string fileName);
	void onFileError(const string path);
	void enqueueCaption(const ConsoleArgList& args);
	Vec2f drawCaptions(std::vector<Caption>* captions);
	string toUpper(string input);
	string toLower(string input);
	string toFileName(string language);
	void loadScreenSize();
	ImVec2 calcWindowSize(f32* fontScale, CaptionEnv env);
	s64 secondsToMicroseconds(f32 seconds);
	s64 calculateDuration(const string text);

	///////////////////////////////////////////
	// Constants
	///////////////////////////////////////////

	const char* DEFAULT_FONT = "Fonts/NotoSans-Regular.ttf";
	const f32 MAX_CAPTION_WIDTH = 1200;
	const f32 LINE_PADDING = 5;
	// Base duration of a caption
	const s64 BASE_DURATION_MICROSECONDS = secondsToMicroseconds(0.9f);
	// How many microseconds are added to the caption duration per character
	const s64 MICROSECONDS_PER_CHAR = secondsToMicroseconds(0.07f);
	const s32 MAX_CAPTION_CHARS[] = // Keyed by font size
	{
		160, 160, 120, 78
	};
	const s32 CUTSCENE_MAX_LINES[] = // Keyed by font size
	{
		5, 5, 4, 3
	};

	///////////////////////////////////////////
	// Static vars
	///////////////////////////////////////////

	static A11yStatus s_captionsStatus = CC_NOT_LOADED;
	static FilePathList s_captionFileList;
	static FilePathList s_fontFileList;
	static FilePath s_currentCaptionFile;
	static FilePath s_currentFontFile;
	static s64 s_maxDuration = secondsToMicroseconds(10.0f);
	static DisplayInfo s_display;
	static u32 s_screenWidth;
	static u32 s_screenHeight;
	static bool s_active = true; // Used by ImGui
	static bool s_logSFXNames = false;
	static system_clock::duration s_lastTime;

	static FileStream s_captionsStream;
	static char* s_captionsBuffer;
	static TFE_Parser s_parser;
	static std::map<string, Caption> s_captionMap;
	static std::vector<Caption> s_activeCaptions;
	static std::vector<Caption> s_exampleCaptions;
	static std::map<string, ImFont*> s_fontMap;
	static ImFont* s_currentCaptionFont;
	static string s_pendingFontPath;

	void init()
	{
		if (TFE_Settings::getA11ySettings()->captionSystemEnabled())
		{
			assert(s_captionsStatus == CC_NOT_LOADED);

			TFE_System::logWrite(LOG_MSG, "a11y", "Initializing caption system...");
			CCMD("showCaption", enqueueCaption, 1, "Display a test caption. Example: showCaption \"Hello, world\"");
			findFiles();
		}
		CVAR_BOOL(s_logSFXNames, "d_logSFXNames", CVFLAG_DO_NOT_SERIALIZE, "If enabled, log the name of each sound effect that plays.");
	}
	
	void refreshFiles()
	{
		if (TFE_Settings::getA11ySettings()->captionSystemEnabled())
		{
			clearFiles();
			findFiles();
		}
	}

	void clearFiles()
	{
		s_captionFileList.clear();
		s_fontFileList.clear();
		s_currentCaptionFile = FilePath();
		s_currentFontFile = FilePath();
		s_captionsStatus = CC_NOT_LOADED;
	}

	void findFiles()
	{
		assert(s_captionsStatus == CC_NOT_LOADED);

		findCaptionFiles();
		findFontFiles();
	}

	//////////////////////////////////////////////////////
	// Fonts
	//////////////////////////////////////////////////////

	// Get the list of all font files we detect in the Fonts directories.
	FilePathList getFontFiles() { return s_fontFileList; }

	// The name and path of the currently selected Font file
	FilePath getCurrentFontFile() { return s_currentFontFile; }

	// True if we are waiting to load a font after we render ImGui.
	bool hasPendingFont() { return !s_pendingFontPath.empty(); }

	bool isFontLoaded() { return s_currentCaptionFont != nullptr && s_currentCaptionFont->IsLoaded(); }

	const ImWchar* GetGlyphRanges()
	{
		static const ImWchar ranges[] =
		{
			0x0020, 0xEEFF, // All glyphs in font
			0,
		};
		return &ranges[0];
	}

	// Get all font file names from the Fonts directories; we will use this to populate the
	// dropdown in the Accessibility settings menu.
	void findFontFiles()
	{
		// First we check the User Documents directory for custom font files added by the user.
		char docsFontsDir[TFE_MAX_PATH];
		const char* docsDir = TFE_Paths::getPath(PATH_USER_DOCUMENTS);
		sprintf(docsFontsDir, "%sFonts/", docsDir);
		if (!FileUtil::directoryExists(docsFontsDir))
		{
			FileUtil::makeDirectory(docsFontsDir);
		}
		s_fontFileList.addFiles(docsFontsDir, "ttf", nullptr);

		// Then we check the Program directory for font files that shipped with TFE.
		char programFontsDir[TFE_MAX_PATH];
		sprintf(programFontsDir, "Fonts/");
		TFE_Paths::mapSystemPath(programFontsDir);
		s_fontFileList.addFiles(programFontsDir, "ttf", nullptr);

		// Try to load the previously selected font.
		string lastFontPath = TFE_Settings::getA11ySettings()->lastFontPath;

		if (!lastFontPath.empty() && ImGui::GetIO().Fonts->Locked)	{ setPendingFont(lastFontPath);	}
		else { tryLoadFont(lastFontPath, false); }
	}

	// Specify a font to load after ImGui finishes rendering.
	void setPendingFont(const string path)
	{
		s_pendingFontPath = path;
	}

	void loadDefaultFont(bool clearAtlas)
	{
		char fontpath[TFE_MAX_PATH];
		snprintf(fontpath, TFE_MAX_PATH, "%s", DEFAULT_FONT);
		TFE_Paths::mapSystemPath(fontpath);
		loadFont(fontpath, clearAtlas);
	}

	// Try to load the font at the given path. If the font doesn't exist or can't be read,
	// we automatically fall back to the default font.
	void tryLoadFont(const string path, bool clearAtlas)
	{
		if (!path.empty() && FileUtil::exists(path.c_str())) 
		{
			try
			{
				loadFont(path, clearAtlas);
			}
			catch (...)
			{
				TFE_System::logWrite(LOG_ERROR, "a11y", string("Couldn't read font file at " + path
					+ "; falling back to default font").c_str());
				loadDefaultFont(clearAtlas);
			}
		}
		else
		{
			TFE_System::logWrite(LOG_ERROR, "a11y", string("Couldn't find font file at " + path
				+ "; falling back to default font").c_str());
			loadDefaultFont(clearAtlas);
		}
	}

	// Load the font at the given path. The font will be added to the ImGui font atlas if it hasn't
	// been loaded before.
	void loadFont(const string path, bool clearAtlas)
	{
		ImGuiIO& io = ImGui::GetIO();

		if (s_fontMap.count(path) > 0) // Font has already been loaded.
		{ 
			s_currentCaptionFont = s_fontMap.at(path);	
		} 
		else // Font hasn't been loaded before.
		{
			s_currentCaptionFont = io.Fonts->AddFontFromFileTTF(path.c_str(), 32, nullptr, GetGlyphRanges());
			s_fontMap[path] = s_currentCaptionFont;
			// If we're loading a new font after the game first initializes, we'll need to clear the 
			// ImGui font atlas so that it is automatically regenerated at the start of the next frame.
			if (clearAtlas) { TFE_Ui::invalidateFontAtlas(); }
		}
		assert(s_currentCaptionFont != nullptr);

		char name[TFE_MAX_PATH];
		FileUtil::getFileNameFromPath(path.c_str(), name);
		s_currentFontFile.name = string(name);
		s_currentFontFile.path = path;

		TFE_Settings::getA11ySettings()->lastFontPath = path;
	}

	void loadPendingFont()
	{
		tryLoadFont(s_pendingFontPath, true);
		s_pendingFontPath.clear();
	}

	//////////////////////////////////////////////////////
	// Captions
	//////////////////////////////////////////////////////
	
	A11yStatus getCaptionSystemStatus() { return s_captionsStatus; }

	// Get the list of all caption files we detect in the Captions directories.
	FilePathList getCaptionFiles() { return s_captionFileList; }

	// The name and path of the currently selected Caption file
	FilePath getCurrentCaptionFile() { return s_currentCaptionFile; }

	// Get all caption file names from the Captions directories; we will use this to populate the
	// dropdown in the Accessibility settings menu.
	void findCaptionFiles()
	{
		// First we check the User Documents directory for custom subtitle files added by the user.
		char docsCaptionsDir[TFE_MAX_PATH];
		const char* docsDir = TFE_Paths::getPath(PATH_USER_DOCUMENTS);
		sprintf(docsCaptionsDir, "%sCaptions/", docsDir);
		if (!FileUtil::directoryExists(docsCaptionsDir))
		{
			FileUtil::makeDirectory(docsCaptionsDir);
		}
		s_captionFileList.addFiles(docsCaptionsDir, "txt", filterCaptionFile);

		// Then we check the Program directory for subtitle files that shipped with TFE.
		char programCaptionsDir[TFE_MAX_PATH];
		const char* programDir = TFE_Paths::getPath(PATH_PROGRAM);
		sprintf(programCaptionsDir, "%s", "Captions/");
		if (!TFE_Paths::mapSystemPath(programCaptionsDir))
			sprintf(programCaptionsDir, "%sCaptions/", programDir);
		s_captionFileList.addFiles(programCaptionsDir, "txt", filterCaptionFile);

		// Try to load captions for the previously selected language.
		string search = toFileName(TFE_Settings::getA11ySettings()->language);
		vector<string>* captionFilePaths = s_captionFileList.getFilePaths();
		for (size_t i = 0; i < captionFilePaths->size(); i++)
		{
			string path = captionFilePaths->at(i);
			if (path.find(search) != string::npos) {
				loadCaptions(path);
				break;
			}
		}

		// If the language didn't load, default to English.
		if (s_captionsStatus != CC_LOADED)
		{
			string fileName = programCaptionsDir + toFileName("en");
			loadCaptions(fileName);
		}
	}

	bool filterCaptionFile(const string fileName)
	{
		return fileName.substr(0, FILE_NAME_START.length()) == FILE_NAME_START;
	}

	// Load the caption file with the given name and parse the subs/captions from it
	void loadCaptions(const string path)
	{
		s_captionMap.clear();

		// Try to open the file
		s_currentCaptionFile.path = path;
		if (!s_captionsStream.open(path.c_str(), Stream::AccessMode::MODE_READ))
		{
			onFileError(path);
			return;
		}

		// Parse language name; for example, if file name is "subtitles-de.txt", the language is "de".
		// The idea is for the language name to be an ISO 639-1 two-letter code, but for now the system
		// doesn't actually care how long the language name is or whether it's a valid 639-1 code.
		size_t start = path.find(FILE_NAME_START);
		if (start != string::npos)
		{
			s_currentCaptionFile.name = path.substr(start);
			string language = path.substr(start + FILE_NAME_START.length());
			language = language.substr(0, language.length() - FILE_NAME_EXT.length());
			TFE_Settings::getA11ySettings()->language = language;
		}

		// Read file into buffer.
		auto size = (u32)s_captionsStream.getSize();
		s_captionsBuffer = (char*)malloc(size);
		s_captionsStream.readBuffer(s_captionsBuffer, size);

		// Init parser (configured to ignore comment lines).
		s_parser.init(s_captionsBuffer, size);
		s_parser.addCommentString("#");
		s_parser.addCommentString("//");

		// Parse each line from the caption file.
		size_t bufferPos = 0;
		while (bufferPos < size)
		{
			const char* line = s_parser.readLine(bufferPos);
			if (!line) { break; }

			TokenList tokens;
			s_parser.tokenizeLine(line, tokens);
			if (tokens.size() < 2) { continue; }

			Caption caption = Caption();
			caption.text = tokens[1];

			// Optional third field is duration in seconds, mainly useful for cutscenes.
			if (tokens.size() > 2)
			{
				try
				{
					caption.microsecondsRemaining = secondsToMicroseconds(std::stof(tokens[2]));
				}
				catch(...) {
				}
			}
			if (caption.microsecondsRemaining <= 0)
			{
				// Calculate caption duration based on text length.
				caption.microsecondsRemaining = calculateDuration(caption.text);
				if (caption.microsecondsRemaining > s_maxDuration) caption.microsecondsRemaining = (s64)s_maxDuration;
			}
			assert(caption.microsecondsRemaining > 0);
			
			if (caption.text[0] == '[') { caption.type = CC_EFFECT; }
			else { caption.type = CC_VOICE; }

			string name = toLower(tokens[0]);
			s_captionMap[name] = caption;
		};

		s_captionsStatus = CC_LOADED;
		s_captionsStream.close();
		free(s_captionsBuffer);
	}

	void onFileError(const string path)
	{
		string error = "Couldn't find caption file at " + path;
		TFE_System::logWrite(LOG_ERROR, "a11y", error.c_str());
		s_captionsStatus = CC_ERROR;
		// TODO: display an error dialog
	}

	void clearActiveCaptions() 
	{
		s_activeCaptions.clear();
		s_exampleCaptions.clear();
	}

	void onSoundPlay(char* name, CaptionEnv env)
	{
		const TFE_Settings_A11y* settings = TFE_Settings::getA11ySettings();
		// Don't add caption if captions are disabled for the current env
		if (env == CC_CUTSCENE && !settings->showCutsceneCaptions && !settings->showCutsceneSubtitles) { return; }
		if (env == CC_GAMEPLAY && !settings->showGameplayCaptions && !settings->showGameplaySubtitles) { return; }

		if (s_logSFXNames) { TFE_System::logWrite(LOG_ERROR, "a11y", name); }

		string nameLower = toLower(name);

		if (s_captionMap.count(nameLower))
		{
			Caption caption = s_captionMap[nameLower]; // Copy
			caption.env = env;
			// Don't add caption if the last caption has the same text
			if (s_activeCaptions.size() > 0 && s_activeCaptions.back().text == caption.text) { return; }

			if (env == CC_CUTSCENE)
			{
				// Don't add caption if this type of caption is disabled
				if (caption.type == CC_EFFECT && !settings->showCutsceneCaptions) { return; }
				else if (caption.type == CC_VOICE && !settings->showCutsceneSubtitles) { return; }

				s32 maxLines = CUTSCENE_MAX_LINES[settings->cutsceneFontSize];

				// Split caption into chunks if it's very long and would take up too much screen
				// real estate at the selected font size (e.g. narration before Talay).
				loadScreenSize();
				f32 fontScale;
				auto windowSize = calcWindowSize(&fontScale, env);
				assert(fontScale > 0);

				size_t idx = 0;   // Index of current character in string.
				string line = ""; // Current line of the current chunk.
				string chunk;
				s32 chunkLineCount = 0;
				while (idx < caption.text.length())
				{
					// Extend the line one character at a time until we exceed the width of the panel
					// or run out of text.
					line += caption.text.at(idx);
					idx++;
					ImGui::PushFont(s_currentCaptionFont);
					auto textSize = ImGui::CalcTextSize(line.c_str(), 0, false, -1.0f);
					ImGui::PopFont();
					// If we exceed the width of the panel...
					if (textSize.x * fontScale > windowSize.x * 0.95f)
					{
						size_t spaceIndex = line.rfind(' ');
						// ...and the line has a space in it, add the line to the chunk.
						if (spaceIndex > 0)
						{
							chunk += line.substr(0, spaceIndex) + "\n";
							idx -= (line.length() - spaceIndex - 1);
							line = string("");
							chunkLineCount++;
						}
						else { break; }

						// If the chunk has reached the maximum number of lines, add it as a new caption.
						if (chunkLineCount >= maxLines)
						{
							s32 length = (s32)chunk.length();
							f32 ratio = length / (f32)caption.text.length();
							Caption next = caption; // Copy
							next.text = chunk;
							next.microsecondsRemaining = s64(next.microsecondsRemaining * ratio);
							enqueueCaption(next);

							// Start a new chunk if we have text remaining.
							size_t newStart = length;
							if (newStart < caption.text.length())
							{
								caption.text = caption.text.substr(newStart);
							}
							else { break; }

							caption.microsecondsRemaining -= next.microsecondsRemaining;
							idx = 0;
							chunkLineCount = 0;
							chunk = "";
						}
					}
				}
			}
			else if (env == CC_GAMEPLAY)
			{
				// Don't add caption if this type of caption is disabled
				if (caption.type == CC_EFFECT && !settings->showGameplayCaptions) { return; }
				else if (caption.type == CC_VOICE && !settings->showGameplaySubtitles) { return; }
			}

			enqueueCaption(caption);
		}
	}

	void enqueueCaption(const ConsoleArgList& args)
	{
		if (args.size() < 2) { return; }

		Caption caption;
		caption.text = args[1];
		caption.env = CC_CUTSCENE;
		caption.microsecondsRemaining = calculateDuration(caption.text);
		caption.type = CC_VOICE;

		enqueueCaption(caption);
	}

	void enqueueCaption(Caption caption)
	{
		if (s_activeCaptions.size() == 0) {
			s_lastTime = system_clock::now().time_since_epoch();
		}
		s_activeCaptions.push_back(caption);
		//TFE_System::logWrite(LOG_ERROR, "a11y", std::to_string(active.size()).c_str());
	}

	Vec2f drawCaptions()
	{
		if (s_activeCaptions.size() > 0)
		{
			return drawCaptions(&s_activeCaptions);
		}
		// We need to always return a value.
		return { 0 };
	}

	Vec2f drawCaptions(std::vector<Caption>* captions)
	{
		if (isFontLoaded()) { ImGui::PushFont(s_currentCaptionFont); }

		TFE_Settings_A11y* settings = TFE_Settings::getA11ySettings();

		// Track time elapsed since last update
		system_clock::duration time = system_clock::now().time_since_epoch();
		system_clock::duration elapsed = time - s_lastTime;
		// Microseconds, not milliseconds
		s64 elapsedMS = duration_cast<microseconds>(elapsed).count();

		loadScreenSize();
		s32 maxLines;

		// Calculate font size and window dimensions
		if (captions->at(0).env == CC_GAMEPLAY)
		{
			maxLines = settings->gameplayMaxTextLines;
			// If too many captions, remove oldest captions
			while (captions->size() > maxLines)	captions->erase(captions->begin());
		}
		else // Cutscene
		{
			maxLines = maxLines = CUTSCENE_MAX_LINES[settings->cutsceneFontSize];
		}

		// Init ImGui window
		f32 fontScale;
		ImVec2 windowSize = calcWindowSize(&fontScale, captions->at(0).env);
		ImGui::SetNextWindowSize(windowSize);
		s32 lineHeight = (s32)(DEFAULT_LINE_HEIGHT * fontScale);

		RGBA* fontColor;
		//TFE_System::logWrite(LOG_ERROR, "a11y", (std::to_string(screenWidth) + " " + std::to_string(subtitleWindowSize.x) + ", " + std::to_string(screenHeight) + " " + std::to_string(subtitleWindowSize.y)).c_str());
		if (captions->at(0).env == CC_GAMEPLAY)
		{
			ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, settings->gameplayTextBackgroundAlpha)); // Window bg
			f32 borderAlpha = settings->showGameplayTextBorder ? settings->gameplayTextBackgroundAlpha : 0;
			ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.5f, 0.5f, 0.5f, borderAlpha)); // Window border
			ImGui::SetNextWindowPos(ImVec2((f32)((s_screenWidth - windowSize.x) / 2), DEFAULT_LINE_HEIGHT * 2.0f));
			ImGui::Begin("##Captions", &s_active, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
			fontColor = &settings->gameplayFontColor;

			elapsedMS *= (s64)settings->gameplayTextSpeed;
		}
		else // Cutscenes
		{
			ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, settings->cutsceneTextBackgroundAlpha)); // Window bg
			f32 borderAlpha = settings->showCutsceneTextBorder ? settings->cutsceneTextBackgroundAlpha : 0;
			ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.5f, 0.5f, 0.5f, borderAlpha)); // Window border
			ImGui::SetNextWindowPos(ImVec2((f32)((s_screenWidth - windowSize.x) / 2), s_screenHeight - DEFAULT_LINE_HEIGHT), 0, ImVec2(0, 1));
			ImGui::Begin("##Captions", &s_active, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
			fontColor = &settings->cutsceneFontColor;

			elapsedMS = (s64)(elapsedMS * settings->cutsceneTextSpeed);
		}
		ImGui::SetWindowFontScale(fontScale);
		ImGui::PopStyleColor(); // Window border
		ImGui::PopStyleColor(); // Window bg

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(fontColor->getRedF(), fontColor->getGreenF(), fontColor->getBlueF(), fontColor->getAlphaF()));

		// Display each caption
		s32 totalLines = 0;
		for (size_t i = 0; i < captions->size() && totalLines < maxLines; i++)
		{
			Caption* title = &captions->at(i);
			bool wrapText = (title->env == CC_CUTSCENE); // Wrapped for cutscenes, centered for gameplay
			f32 wrapWidth = wrapText ? windowSize.x : -1.0f;
			auto textSize = ImGui::CalcTextSize(title->text.c_str(), 0, false, wrapWidth);
			if (!wrapText && textSize.x > windowSize.x)
			{
				wrapText = true;
				wrapWidth = windowSize.x;
				textSize = ImGui::CalcTextSize(title->text.c_str(), 0, false, wrapWidth);
			}

			s32 lines = (s32)round(textSize.y / lineHeight);
			totalLines += lines;

			//TFE_System::logWrite(LOG_ERROR, "a11y", (std::to_string(i) + " " + std::to_string(textSize.y) + ", " + std::to_string(lineHeight) + " " + std::to_string(lines) + " " + std::to_string(totalLines) + " " + std::to_string(windowSize.y)).c_str());
			if (wrapText)
			{
				ImGui::TextWrapped("%s", title->text.c_str());
			}
			else
			{
				ImGui::SetCursorPosX((windowSize.x - textSize.x) * 0.5f);
				ImGui::Text("%s", title->text.c_str());
			}

			// Reduce the caption's time remaining, removing it from the list if it's out of time
			if (i == 0)
			{
				if (!TFE_Console::isOpen()) // Don't advance timer while console is open
				{
					title->microsecondsRemaining -= elapsedMS;
					if (title->microsecondsRemaining <= 0) {
						captions->erase(captions->begin() + i);
						i--;
					}
				}
			}
		}

		ImVec2 finalWindowSize = ImGui::GetWindowSize();
		ImGui::PopStyleColor();
		ImGui::End();

		if (isFontLoaded()) { ImGui::PopFont(); }

		s_lastTime = time;

		return { finalWindowSize.x, finalWindowSize.y };
	}

	Vec2f drawExampleCaptions()
	{
		if (s_exampleCaptions.size() == 0)
		{
			Caption caption = s_captionMap["example_cutscene"]; // Copy
			caption.env = CC_CUTSCENE;
			s_exampleCaptions.push_back(caption);
		}
		return drawCaptions(&s_exampleCaptions);
	}

	bool cutsceneCaptionsEnabled()
	{
		auto settings = TFE_Settings::getA11ySettings();
		return (settings->showCutsceneCaptions || settings->showCutsceneSubtitles);
	}	
	
	bool gameplayCaptionsEnabled()
	{
		auto settings = TFE_Settings::getA11ySettings();
		return (settings->showGameplayCaptions || settings->showGameplaySubtitles);
	}

	void focusCaptions()
	{
		ImGui::SetWindowFocus("##Captions");
	}

	///////////////////////////////////////////
	// Helpers
	///////////////////////////////////////////
	string toUpper(string input)
	{
		u8* p = (u8*)input.c_str();
		while (*p)
		{
			*p = toupper((u8)*p);
			p++;
		}
		return input;
	}

	string toLower(string input)
	{
		u8* p = (u8*)input.c_str();
		while (*p)
		{
			*p = tolower((u8)*p);
			p++;
		}
		return input;
	}
	
	string toFileName(string language)
	{
		return FILE_NAME_START + language + FILE_NAME_EXT;
	}

	void loadScreenSize()
	{
		TFE_RenderBackend::getDisplayInfo(&s_display);
		s_screenWidth = s_display.width;
		s_screenHeight = s_display.height;
	}

	ImVec2 calcWindowSize(f32* fontScale, CaptionEnv env)
	{
		TFE_Settings_A11y* settings = TFE_Settings::getA11ySettings();
		*fontScale = s_screenHeight / 1024.0f; // Scale based on window resolution
		*fontScale = (f32)fmax(*fontScale, 1);

		f32 maxWidth = MAX_CAPTION_WIDTH;
		if (env == CC_GAMEPLAY)
		{
			*fontScale += (f32)(settings->gameplayFontSize * s_screenHeight / 1280.0f);
			maxWidth += 100 * settings->gameplayFontSize;
		}
		else
		{
			*fontScale += (f32)(settings->cutsceneFontSize * s_screenHeight / 1280.0f);
			maxWidth += 100 * settings->cutsceneFontSize;
		}
		*fontScale = (f32)fmax(*fontScale, 1);
		if (s_currentCaptionFont != nullptr) *fontScale *= .7f;

		f32 windowWidth = s_screenWidth * 0.8f;
		if (windowWidth > maxWidth) windowWidth = maxWidth;
		ImVec2 windowSize = ImVec2(windowWidth, 0); // Auto-size vertically
		return windowSize;
	}

	s64 secondsToMicroseconds(f32 seconds)
	{
		return (s64)(seconds * 1000000);
	}

	s64 calculateDuration(const string text) {
		return BASE_DURATION_MICROSECONDS + text.length() * MICROSECONDS_PER_CHAR;
	}
}
