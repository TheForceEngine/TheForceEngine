#include <TFE_Input/replay.h>
#include <sstream>
#include <string>
#include <TFE_A11y/accessibility.h>
#include <TFE_Settings/settings.h>
#include <TFE_DarkForces/agent.h>
#include <TFE_DarkForces/hud.h>
#include <TFE_DarkForces/random.h>
#include <TFE_DarkForces/time.h>
#include <TFE_DarkForces/player.h>
#include <TFE_DarkForces/playerCollision.h>
#include <TFE_DarkForces/darkForcesMain.h>
#include <TFE_DarkForces/weaponFireFunc.h>
#include <TFE_DarkForces/Actor/mousebot.h>
#include <TFE_DarkForces/Actor/turret.h>
#include <TFE_DarkForces/GameUI/agentMenu.h>
#include <TFE_DarkForces/GameUI/pda.h>
#include <TFE_DarkForces/mission.h>
#include <TFE_DarkForces/time.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_FrontEndUI/frontEndUi.h>
#include <TFE_FrontEndUI/modLoader.h>
#include <TFE_Game/saveSystem.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_Jedi/Renderer/rcommon.h>
#include <TFE_Jedi/Serialization/serialization.h>
#include <TFE_System/frameLimiter.h>
#include <TFE_System/system.h>
#include <algorithm>

namespace TFE_Input
{
	// Handle replay File Paths
	FileStream s_replayFile;
	char s_replayDir[TFE_MAX_PATH];
	char s_replayAgentPath[TFE_MAX_PATH];
	static char s_replayPath[TFE_MAX_PATH];

	static char s_headerName[256];
	string replayFileName = "";

	// Recording States
	static bool s_recording = false;
	static bool s_playback = false;

	// Agent and Header values
	LevelSaveData replayAgent;
	TFE_Settings_Game replayGameSettings = {};
	
	// Mouse and Keyboard States
	static int s_mouseMode = 0; 
	static int s_mouseFlags = 0;
	static f32 s_mouseSensitivity[2] = { 0, 0 };

	// Settings preserved when replaying
	int replayGraphicsType = 0;
	bool playerHeadwave = false;
	bool playerAutoAim = false; 
	bool autoEndMission = false;
	bool vsyncEnabled = false;
	bool replayInitialized = false;
	bool cutscenesEnabled = true;
	bool smoothDeltaTime = true;
	bool pauseReplay = false; 
	bool showReplayMsgFrame = false;
	bool alwaysRecord = false;
	
	extern f64 gameFrameLimit = 0;	
	extern f64 replayFrameLimit = 0;

	// Notification for replay system initialization. 
	bool demoStartNotified = false;

	// Main storage of event structures. 
	std::unordered_map<int, ReplayEvent> inputEvents;
	u64 replayStartTime = 0;
	s32 replay_seed = 0;

	int replayFilehandler = -1;
	int replayLogCounter = 0;
	
	std::vector<char> settingBuffer;

	enum ReplayVersion : u32
	{
		ReplayVersionInit = 1,
		ReplayVersionCur = ReplayVersionInit
	};

	void initReplays()
	{
		// TO DO - combine replays from both sources. 

		sprintf(s_replayDir, "%sReplays/", TFE_Paths::getPath(PATH_PROGRAM));
		TFE_Paths::fixupPathAsDirectory(s_replayDir);

		// Check TFE/Replays first 
		if (!FileUtil::directoryExists(s_replayDir))
		{
			sprintf(s_replayDir, "%sReplays/", TFE_Paths::getPath(PATH_USER_DOCUMENTS));
			TFE_Paths::fixupPathAsDirectory(s_replayDir);
			// Otherwise check <USER>/TheForceEngine/Replays
			if (!FileUtil::directoryExists(s_replayDir))
			{
				FileUtil::makeDirectory(s_replayDir);
			}
		}
		else
		{
			sprintf(s_replayAgentPath, "%sreplay.agent", TFE_Paths::getPath(PATH_USER_DOCUMENTS));
		}
		TFE_System::logWrite(LOG_MSG, "Replay", "Loading Replays from %s ...", s_replayDir);

		if (TFE_Settings::getGameSettings()->df_enableRecordingAll)
		{
			TFE_Settings::getGameSettings()->df_enableRecording = true;
		}
	}

	bool shouldLogReplay()
	{
		return TFE_Settings::getGameSettings()->df_demologging || TFE_Settings::getTempSettings()->df_demologging;
	}	

	void getAgentPath(char * agentPath)
	{
		agentPath = s_replayAgentPath;
	}

	bool isRecording()
	{
		return s_recording;
	}

	void setRecording(bool recording)
	{
		s_recording = recording;
	}

	bool isDemoPlayback()
	{
		return s_playback;
	}

	// Are you recording or playing back a demo
	bool isReplaySystemLive()
	{
		return s_recording || s_playback;
	}

	void setDemoPlayback(bool playback)
	{
		s_playback = playback;
	}

	void saveTick()
	{
		int inputCounter = inputMapping_getCounter();		
		inputEvents[inputCounter].curTick = TFE_DarkForces::s_curTick;		
	}	

	void loadTick()
	{
		int inputCounter = inputMapping_getCounter();
		TFE_DarkForces::s_curTick = inputEvents[inputCounter].curTick;				
	}

	void saveInitTime()
	{
		inputEvents[0].curTick = TFE_DarkForces::s_curTick;
		inputEvents[0].prevTick = TFE_DarkForces::s_prevTick;
		inputEvents[0].deltaTime = TFE_DarkForces::s_deltaTime;
		inputEvents[0].timeAccum = TFE_DarkForces::s_timeAccum;
		inputEvents[0].plTick = TFE_DarkForces::s_playerTick;
		inputEvents[0].plPrevTick = TFE_DarkForces::s_prevPlayerTick;
		memcpy(inputEvents[0].frameTicks, TFE_DarkForces::s_frameTicks, sizeof(fixed16_16) * TFE_ARRAYSIZE(TFE_DarkForces::s_frameTicks));
	}
	
	void loadInitTime()
	{
		TFE_DarkForces::s_curTick = inputEvents[0].curTick;
		TFE_DarkForces::s_prevTick = inputEvents[0].prevTick;
		TFE_DarkForces::s_deltaTime = inputEvents[0].deltaTime;
		TFE_DarkForces::s_timeAccum = inputEvents[0].timeAccum;
		TFE_DarkForces::s_playerTick = inputEvents[0].plTick;
		TFE_DarkForces::s_prevPlayerTick = inputEvents[0].plPrevTick;
		memcpy(TFE_DarkForces::s_frameTicks, inputEvents[0].frameTicks, sizeof(fixed16_16) * TFE_ARRAYSIZE(inputEvents[0].frameTicks));
	}

	void recordReplaySeed()
	{
		replay_seed = TFE_DarkForces::getSeed();
		TFE_System::logWrite(LOG_MSG, "Replay", "Recording Seed: %d", replay_seed);
	}
	
	void restoreReplaySeed()
	{
		TFE_DarkForces::setSeed(replay_seed);
		TFE_System::logWrite(LOG_MSG, "Replay", "Loading Seed: %d", replay_seed);
	}

	// Wipe all event history
	void clearEvents()
	{
		size_t iSize = inputEvents.size();
		for (int i = 0; i < iSize; i++)
		{
			inputEvents[i].clear();
		}
		inputEvents.clear();
	}

	void recordEvent(int action, KeyboardCode keyCode, bool isPress)
	{		
		// When recording, normally ignore the escape key unless the PDA is open
		// Don't allow saving/reloading either as the state will become messed up.
		if (isRecording() && (keyCode != KEY_ESCAPE || TFE_DarkForces::pda_isOpen()) 
			&& !(isBindingPressed(IAS_QUICK_SAVE) || isBindingPressed(IAS_QUICK_LOAD)))
		{
			int updateCounter = inputMapping_getCounter();
			if (isPress)
			{
				inputEvents[updateCounter].keysPressed.push_back((InputAction)action);
			}
			else
			{
				inputEvents[updateCounter].keysDown.push_back((InputAction)action);
			}
		}
	}

	void replayEvent()
	{
		// Plays back the event from the inputEvents map
		int updateCounter = inputMapping_getCounter();
		ReplayEvent event = inputEvents[updateCounter-1];

		// Handle key presses
		for (int i = 0; i < event.keysPressed.size(); i++)
		{
			s32 act = event.keysPressed[i];
			inputMapping_setStatePress((InputAction)act);
		}

		// Handle key downs
		for (int i = 0; i < event.keysDown.size(); i++)
		{
			s32 act = event.keysDown[i];
			inputMapping_setStateDown((InputAction)act);
		}
	}

	// Stores the vectors as strings for writings
	string convertToString(vector<s32> keysDown)
	{
		std::ostringstream oss;
		for (int i = 0; i < keysDown.size(); ++i) {
			if (i != 0) oss << ",";
			oss << keysDown[i];
		}
		return oss.str();
	}

	// Loads the strings int values as vectors for replays
	vector<s32> convertFromString(string keyStr)
	{
		std::vector<s32> intVector;
		std::istringstream iss(keyStr);
		std::string token;
		while (std::getline(iss, token, ',')) {
			intVector.push_back(std::stoi(token));
		};
		return intVector;
	}

	// Serializes the input events into the demo file from vectors to strings
	vector<s32> serializeInputs(Stream* stream, vector<s32> inputList, bool writeFlag)
	{
		s32 keySize = 0;
		string keyString;

		// The Vector would consist of the key codes for the input events
		// Or it will contain the mouse positions along with yaw/pitch/roll

		if (writeFlag)
		{
			keyString = convertToString(inputList);
			keySize = (s32)keyString.size();
		}

		SERIALIZE(ReplayVersionInit, keySize, 0);
		if (!writeFlag)
		{
			keyString.resize(keySize);
		}

		SERIALIZE_BUF(ReplayVersionInit, &keyString[0], keySize);

		if (!writeFlag && keySize > 0)
		{
			return convertFromString(keyString);
		}
		return inputList;
	}

	void storePDAPosition(Vec2i pos)
	{
		int updateCounter = inputMapping_getCounter();
		inputEvents[updateCounter].pdaPosition = pos;
	}

	Vec2i getPDAPosition()
	{
		int updateCounter = inputMapping_getCounter();
		return inputEvents[updateCounter - 1].pdaPosition;
	}

	void copyGameSettings(TFE_Settings_Game* source, TFE_Settings_Game* dest)
	{
		dest->df_airControl = source->df_airControl;
		dest->df_enableAutoaim = source->df_enableAutoaim;
		dest->df_smoothVUEs = source->df_smoothVUEs;
		dest->df_autorun = source->df_autorun;
		dest->df_crouchToggle = source->df_crouchToggle;
		dest->df_ignoreInfLimit = source->df_ignoreInfLimit;
		dest->df_stepSecondAlt = source->df_stepSecondAlt;
		dest->df_enableUnusedItem = source->df_enableUnusedItem;
		dest->df_solidWallFlagFix = source->df_solidWallFlagFix;
		dest->df_jsonAiLogics = source->df_jsonAiLogics;
		dest->df_pitchLimit = source->df_pitchLimit;
		dest->df_recordFrameRate = source->df_recordFrameRate;
		dest->df_autoEndMission = source->df_autoEndMission;
	}

	// Loads the demo files from the replay folder. 
	void populateReplayDirectory(std::vector<TFE_SaveSystem::SaveHeader>& dir)
	{
		dir.clear();
		FileList fileList;
		FileUtil::readDirectory(s_replayDir, "demo", fileList);

		// Sort the files in alphabetical order
		sort(fileList.begin(), fileList.end());

		size_t replayCount = fileList.size();
		dir.resize(replayCount);

		const std::string* filenames = fileList.data();
		TFE_SaveSystem::SaveHeader* headers = dir.data();
		for (size_t i = 0; i < replayCount; i++)
		{
			char filePath[TFE_MAX_PATH];
			sprintf(filePath, "%s%s", s_replayDir, filenames[i].c_str());
			loadReplayHeader(filePath, &headers[i]);
		}
	}

	bool setupPath()
	{
		// Replay Initialization - it will set the current game and level name
		IGame* s_game = TFE_SaveSystem::getCurrentGame();
		
		char timeDate[256];
		TFE_System::getDateTimeString(timeDate);

		char levelName[256];
		s_game->getLevelName(levelName);

		char levelId[256];
		s_game->getLevelId(levelId);
		strcpy(levelId, TFE_A11Y::toLower(string(levelId)).c_str());

		char modPath[TFE_MAX_PATH];
		s_game->getModList(modPath);
		strcpy(modPath, TFE_A11Y::toLower(string(modPath)).c_str());

		char modName[TFE_MAX_PATH];
		
		// If you are not using a mod
		if (strlen(modPath) == 0 )
		{
			// For now assume all replays are Dark Forces replays
			strcpy(modName, "dark_forces");
		}
		else
		{
			FileUtil::getFileNameFromPath(modPath, modName, true);
		}

		int offset = 0;
		int offsetMax = 10000;

		// Create the demo file path
		sprintf(s_replayPath, "%s%s_%s.demo", s_replayDir, modName, levelId);

		while (FileUtil::exists(s_replayPath) || offset > offsetMax)
		{
			offset++;
			sprintf(s_replayPath, "%s%s_%s_%d.demo", s_replayDir, modName, levelId, offset);
			if (offset > offsetMax)
			{
				TFE_System::logWrite(LOG_MSG, "REPLAY", "Failed to create replay file. Too many files with the same level name.");
				return false;
			}
		}
		
		// Store the demo name in the header
		sprintf(s_headerName, "%s.demo", levelName);
		return true;
	}

	s32 getFileHandler(Stream* stream, bool writeFlag)
	{
		s32 fileHandler = 0;
		if (writeFlag)
		{
			serialization_setMode(SMODE_WRITE);
			fileHandler = s_replayFile.open(s_replayPath, Stream::MODE_WRITE);
		}
		else
		{
			serialization_setMode(SMODE_READ);
			fileHandler = s_replayFile.open(s_replayPath, Stream::MODE_READ);
		}
		return fileHandler;
	}

	bool loadReplayHeader(const char* filePath, TFE_SaveSystem::SaveHeader* header)
	{
		bool ret = false;
		FileStream stream;
		char fileName[TFE_MAX_PATH];
		FileUtil::getFileNameFromPath(filePath, fileName, true);
		if (stream.open(filePath, Stream::MODE_READ))
		{
			loadHeader(&stream, header, filePath);
			strcpy(header->fileName, fileName);
			stream.close();
			ret = true;
		}
		return ret;
	}

	// Handles the header and agent data for the replay
	int serializeHeaderAgentInfo(Stream* stream, bool writeFlag)
	{

		// Reuse an existing file handler if it already exists
		if (replayFilehandler > 0)
		{
			return replayFilehandler;
		}

		// Load the Header and agent information
		int fileHandler = getFileHandler(stream, writeFlag);
		if (fileHandler > 0)
		{

			// HEADER INFORMATION

			// Store the replay header information including the mod name and level name
			// As well as the final image of the demo
			if (writeFlag)
			{
				TFE_SaveSystem::saveHeader(stream, s_headerName);
			}
			else
			{
				TFE_SaveSystem::SaveHeader* header = new TFE_SaveSystem::SaveHeader();
				TFE_SaveSystem::loadHeader(stream, header, s_headerName);
			}
			SERIALIZE_VERSION(ReplayVersionInit);

			// AGENT INFORMATION
			
			// This is where we save / load the agent inventory and difficulty 
			LevelSaveData data;
			if (writeFlag)
			{
				// Store the agent data from the current agent ID
				agent_readSavedData(s_agentId, &data);
				data.agentData.difficulty = s_agentData[s_agentId].difficulty;
			}

			SERIALIZE(SaveVersionInit, data, { 0 });
			if (!writeFlag)
			{	
				// Create new agent if you don't have any 
				s32 curCount = agentMenu_getAgentCount();
				if (curCount == 0)
				{
					agentMenu_createNewAgent();
					agentMenu_setAgentName("replay");
				}

				// Back up current player from the first slot
				agent_readSavedData(0, &replayAgent);

				// Set the replay agent to the first slot and load the data to it
				s_agentData[0] = data.agentData;
				agent_writeSavedData(0, &data);
			}
		}	
		return fileHandler;
	}

	// Main replay serialization function
	// The demo file contains all the information needed to replay the game
	// -----------------------------------------------------------
	// 
	// 1. It will contain the metadata of the replay such as then name and modname
	// 2. It will contain the agent data for the replay
	// 3. It will contain the input events for the replay
	// 4. It will contain the game and graphical settings 
	// 5. It will contain the seed and tick timing data
	// 
	// -----------------------------------------------------------
	void serializeDemo(FileStream* stream, bool writeFlag)
	{
		// Initalize the events and frametick obj
		int eventCounter = 0;
		fixed16_16 frameTicks[13];

		// Pause everything while we serialize
		TFE_DarkForces::time_pause(JTRUE);

		// Handle the header and agent data
		int fileHandler = serializeHeaderAgentInfo(stream, writeFlag);

		if (fileHandler > 0)
		{

			// Handle Tick timing
			SERIALIZE(ReplayVersionInit, inputEvents[0].prevTick, 0);
			SERIALIZE(ReplayVersionInit, inputEvents[0].deltaTime, 0);
			SERIALIZE(ReplayVersionInit, inputEvents[0].timeAccum, 0);
			SERIALIZE(ReplayVersionInit, inputEvents[0].plTick, 0);
			SERIALIZE(ReplayVersionInit, inputEvents[0].plPrevTick, 0);

			// Handle the frame ticks
			if (writeFlag) 
			{
				replayStartTime = TFE_System::getStartTime();
				memcpy(frameTicks, inputEvents[0].frameTicks, sizeof(fixed16_16) * TFE_ARRAYSIZE(inputEvents[0].frameTicks));
			}

			// Store the replay seed and start time 
			SERIALIZE(ReplayVersionInit, replayStartTime, 0);
			SERIALIZE(ReplayVersionInit, replay_seed, 0);

			SERIALIZE_BUF(SaveVersionInit, frameTicks, sizeof(fixed16_16) * TFE_ARRAYSIZE(frameTicks));

			// Handle events list size
			s32 eventListsSize = inputMapping_getCounter();
			SERIALIZE(ReplayVersionInit, eventListsSize, 0);

			// Settings and Input Handling 
			TFE_Settings_Game* gameSettings = TFE_Settings::getGameSettings();
			TFE_Settings_A11y* allySettings = TFE_Settings::getA11ySettings();
			InputConfig* inputConfig = inputMapping_get();
			int mouseMode = inputConfig->mouseMode;

			// If you are not recording first store all the original values
			if (!writeFlag)
			{
				copyGameSettings(gameSettings , &replayGameSettings);
				playerHeadwave = allySettings->enableHeadwave;
				s_mouseMode = inputConfig->mouseMode;
				s_mouseFlags = inputConfig->mouseFlags;
				s_mouseSensitivity[0] = inputConfig->mouseSensitivity[0];
				s_mouseSensitivity[1] = inputConfig->mouseSensitivity[1];
			}

			// Serialize game and input settings
			SERIALIZE(ReplayVersionInit, gameSettings->df_airControl, 0);
			SERIALIZE(ReplayVersionInit, gameSettings->df_enableAutoaim, 0);
			SERIALIZE(ReplayVersionInit, gameSettings->df_smoothVUEs, 0);
			SERIALIZE(ReplayVersionInit, gameSettings->df_autorun, 0);
			SERIALIZE(ReplayVersionInit, gameSettings->df_crouchToggle, 0);
			SERIALIZE(ReplayVersionInit, gameSettings->df_ignoreInfLimit, 0);
			SERIALIZE(ReplayVersionInit, gameSettings->df_stepSecondAlt, 0);
			SERIALIZE(ReplayVersionInit, gameSettings->df_enableUnusedItem, 0);
			SERIALIZE(ReplayVersionInit, gameSettings->df_solidWallFlagFix, 0);
			SERIALIZE(ReplayVersionInit, gameSettings->df_jsonAiLogics, 0);
			SERIALIZE(ReplayVersionInit, gameSettings->df_bobaFettFacePlayer, 0);
			SERIALIZE(ReplayVersionInit, gameSettings->df_recordFrameRate, 0);			
			int pitchLimit = gameSettings->df_pitchLimit;
			SERIALIZE(ReplayVersionInit, pitchLimit, 0);

			SERIALIZE(ReplayVersionInit, allySettings->enableHeadwave, 0);

			SERIALIZE(ReplayVersionInit, inputConfig->mouseFlags, 0);			
			SERIALIZE(ReplayVersionInit, mouseMode, 0);
			SERIALIZE(ReplayVersionInit, inputConfig->mouseSensitivity[0], 0);
			SERIALIZE(ReplayVersionInit, inputConfig->mouseSensitivity[1], 0);
			
			// Need to cast to the correct type for replays
			if (!writeFlag)
			{
				gameSettings->df_pitchLimit = (PitchLimit)pitchLimit;
				inputConfig->mouseMode = (MouseMode)mouseMode;
			}

			// Handle writing the events
			if (writeFlag)
			{
				// Loop through inputEvents	Map				
				for (const auto& pair : inputEvents)
				{
					eventCounter = pair.first;

					// Since the data structure is a Map it may have more values than allocated data
					// Skip it if it is the case
					if (eventCounter > eventListsSize) continue;

					// Serialize the event 
					SERIALIZE(ReplayVersionInit, eventCounter, 0);

					// Serialize all the key and mouse inputs
					serializeInputs(stream, pair.second.keysDown, writeFlag);
					serializeInputs(stream, pair.second.keysPressed, writeFlag);
					serializeInputs(stream, pair.second.mousePos, writeFlag);
					
					// Critical tick timing data per event
					u32 curTick = pair.second.curTick;
					SERIALIZE(ReplayVersionInit, curTick, 0);

					// Store the PDA positioning data
					s32 pdaXpos = pair.second.pdaPosition.x;
					s32 pdaZpos = pair.second.pdaPosition.z;
					SERIALIZE(ReplayVersionInit, pdaXpos, 0);
					SERIALIZE(ReplayVersionInit, pdaZpos, 0);
				}

			}
			else
			{
				// Wipe the events and load them from the demo
				clearEvents();

				for (s32 i = 0; i < eventListsSize + 1; i++)
				{
					SERIALIZE(ReplayVersionInit, eventCounter, 0);

					// Create a new ReplayEvent object 
					ReplayEvent event = {};

					// Load the key and mouse inputs
					event.keysDown = serializeInputs(stream, event.keysDown, writeFlag);
					event.keysPressed = serializeInputs(stream, event.keysPressed, writeFlag);
					event.mousePos = serializeInputs(stream, event.mousePos, writeFlag);

					// Critical tick timing data per event
					u32 curTick = 0;
					SERIALIZE(ReplayVersionInit, curTick, 0);
					event.curTick = curTick;

					// Load the PDA positioning data	
					Vec2i pdaPos;
					SERIALIZE(ReplayVersionInit, pdaPos.x, 0);
					SERIALIZE(ReplayVersionInit, pdaPos.z, 0);
					event.pdaPosition = pdaPos;

					// Load the event into the map for playback
					inputEvents[eventCounter] = event;
				}

				// Load the frame ticks 
				memcpy(inputEvents[0].frameTicks, frameTicks, sizeof(fixed16_16) * TFE_ARRAYSIZE(frameTicks));
				
				// Wipe the event counter and set the max input counter
				inputMapping_resetCounter();				
				inputMapping_setMaxCounter((s32)inputEvents.size());

				// Set the new start time
				TFE_System::setStartTime(replayStartTime);
			}

			s_replayFile.close();
		}
		// Resume the game
		TFE_DarkForces::time_pause(JFALSE);
	}
	void disableReplayCheats()
	{
		TFE_DarkForces::s_invincibility = JFALSE;
		TFE_DarkForces::s_aiActive = JTRUE;
		TFE_DarkForces::s_smallModeEnabled = JFALSE;
		TFE_DarkForces::s_flyMode = JFALSE;
		TFE_DarkForces::s_noclip = JFALSE;
		TFE_DarkForces::s_oneHitKillEnabled = JFALSE;
		TFE_DarkForces::s_instaDeathEnabled = JFALSE;
		TFE_DarkForces::s_limitStepHeight = JTRUE;
		TFE_Jedi::s_fullBright = JFALSE;
	}

	void setStartHudMessage(bool notify)
	{
		demoStartNotified = notify;
	}

	// Just used to create the initial Recording/Replaying notification.
	bool sendHudStartMessage()
	{
		// Don't bother sending messages if the eye isn't present yet. 
		if (!TFE_DarkForces::s_playerEye)
		{
			return false; 
		}

		if (demoStartNotified)
		{
			demoStartNotified = false;
			if (isRecording())
			{
				TFE_DarkForces::hud_sendTextMessage("Recording started...", 0, false);
				return true;
			}
			else if (isDemoPlayback())
			{
				TFE_DarkForces::hud_sendTextMessage("Playback started...", 0, false);
				return true;
			}
		}
		return false;
	}

	void sendHudPauseMessage()
	{
		string msg = "Replay Paused";
		TFE_System::logWrite(LOG_MSG, "Replay", msg.c_str());
		TFE_DarkForces::hud_sendTextMessage(msg.c_str(), 0, false);
		showReplayMsgFrame = true;
	}

	bool isReplayPaused()
	{
		if (showReplayMsgFrame)
		{
			showReplayMsgFrame = false;
			return false;
		}
		return pauseReplay;
	}

	void handleFrameRate()
	{
		// Set the frame rate for replay playback.
		string framePlaybackStr = TFE_FrontEndUI::getPlaybackFramerate();

		if (strcmp(framePlaybackStr.c_str(), "Original") == 0)
		{
			s32 frameRate = TFE_FrontEndUI::getRecordFramerate();
			TFE_System::frameLimiter_set(frameRate);
			TFE_System::setVsync(true);
		}
		else if (strcmp(framePlaybackStr.c_str(), "Unlimited") == 0)
		{
			TFE_System::setVsync(false);
		}
		else
		{
			s32 playbackFrameRate = std::atoi(framePlaybackStr.c_str());
			TFE_System::frameLimiter_set(playbackFrameRate);
			TFE_System::setVsync(true);
		}
	}

	void increaseReplayFrameRate()
	{
		TFE_Settings_Game* gameSettings = TFE_Settings::getGameSettings();
		if (gameSettings->df_playbackFrameRate == 0)
		{
			pauseReplay = false;
			clearAccumulatedMouseMove();
		}

		if (gameSettings->df_playbackFrameRate < 6)
		{
			gameSettings->df_playbackFrameRate++;
			string msg = "Setting Replay Speed to " + TFE_FrontEndUI::getPlaybackFramerate();
			TFE_DarkForces::hud_sendTextMessage(msg.c_str(), 0, false);
			handleFrameRate();
		}
	}

	void decreaseReplayFrameRate()
	{
		TFE_Settings_Game* gameSettings = TFE_Settings::getGameSettings();
		if (gameSettings->df_playbackFrameRate > 0)
		{
			gameSettings->df_playbackFrameRate--;
		}

		if (gameSettings->df_playbackFrameRate == 0 && !pauseReplay)
		{
			pauseReplay = true;
			sendHudPauseMessage();
		}
		else
		{
			handleFrameRate();
			string msg = "Setting Replay Speed to " + TFE_FrontEndUI::getPlaybackFramerate();
			TFE_DarkForces::hud_sendTextMessage(msg.c_str(), 0, false);
		}
	}

	// This handles a lot of the common logic to ensure the replay is in a consistent state
	void startCommonReplayStates()
	{
		// Wipe all the objects and player data.
		objData_clear();
		player_clearEyeObject();

		// Wipe all the previous events
		clearEvents();

		// Wipe the weapon settings 
		TFE_DarkForces::resetWeaponFunc();

		// Player frame collisions affect logic and must be reset
		initPlayerCollision();

		// Wipes the frame counter so it is consistent 
		inputMapping_resetCounter();

		// Wipe all inputs
		inputMapping_endFrame();

		// Ensure that the PDA previous location is identical
		TFE_DarkForces::pda_resetTab();

		// Disable autoaim as window size will affect it
		TFE_Settings_Game* gameSettings = TFE_Settings::getGameSettings();
		playerAutoAim = gameSettings->df_enableAutoaim;
		gameSettings->df_enableAutoaim = false;

		// Disable auto mission ending - we don't support it for replays yet
		autoEndMission = gameSettings->df_autoEndMission;
		gameSettings->df_autoEndMission = false;

		// Enable VSYNC for replay playback - can be overriden during replays
		vsyncEnabled = TFE_System::getVSync();
		TFE_System::setVsync(true);

		// Ensure we are always in GPU mode for consistency
		TFE_Settings_Graphics* graphicSetting = TFE_Settings::getGraphicsSettings();
		replayGraphicsType = graphicSetting->rendererIndex;
		graphicSetting->rendererIndex = 1;

		// Preserve the original smoothDeltaTime option
		// Disable useSmoothDeltaTime for now.
		// TODO: Add a new version that allows this setting?
		smoothDeltaTime = graphicSetting->useSmoothDeltaTime;
		graphicSetting->useSmoothDeltaTime = false;
		
		// Preserve the original frame rate
		gameFrameLimit = TFE_System::frameLimiter_get();
		
		// Disable cheats that could affect the replay
		disableReplayCheats();

		// Used 
		setStartHudMessage(true);
	}

	void endCommonReplayStates()
	{
		// Restore settings to their original state
		TFE_Settings::getGameSettings()->df_enableAutoaim = playerAutoAim;
		TFE_Settings::getGameSettings()->df_autoEndMission = autoEndMission;
		TFE_System::setVsync(vsyncEnabled);
		TFE_System::frameLimiter_set(gameFrameLimit);
		TFE_Settings::getGraphicsSettings()->rendererIndex = replayGraphicsType;
		TFE_Settings::getGraphicsSettings()->useSmoothDeltaTime = smoothDeltaTime;

		if (TFE_Settings::getGameSettings()->df_enableRecordingAll)
		{
			TFE_Settings::getGameSettings()->df_enableRecording = true;
		}

		if (shouldLogReplay())
		{
			TFE_System::logClose();
			TFE_System::logOpen("the_force_engine_log.txt", true);
			TFE_System::logTimeToggle();
		}
	}

	void logReplayPosition(int counter)
	{
		if (shouldLogReplay() && TFE_DarkForces::s_playerEye)
		{
			if (isDemoPlayback()) counter--;

			ReplayEvent event = TFE_Input::inputEvents[counter];			
			string keys, keysPressed, mouse, hudData;

			keys = convertToString(event.keysDown);
			keysPressed = convertToString(event.keysPressed);
			mouse = convertToString(event.mousePos);
			s32 xPos = s_eyePos.x;
			s32 yPos = -s_playerEye->posWS.y;
			s32 zPos = s_eyePos.z;
			angle14_16 yaw = s_playerEye->yaw;
			angle14_16 pitch = s_playerEye->pitch;

			string logMsg = "Update %d: Tick = %d X:%04d Y:%04d Z:%04d, yaw: %d, pitch: %d, keysDown: %s, keysPressed: %s, mouse: %s";
			TFE_System::logWrite(LOG_MSG, "Replay", logMsg.c_str(), counter, s_curTick, xPos, yPos, zPos, yaw, pitch,
				                                    keys.c_str(), keysPressed.c_str(), mouse.c_str());
		}
	}

	void startRecording()
	{
		// Handle recording initialization
		startCommonReplayStates();
		recordReplaySeed();
		setRecording(true);
		setDemoPlayback(false);
		saveTick();
		saveInitTime();

		if (shouldLogReplay())
		{
			TFE_System::logClose();
			TFE_System::logOpen("record.log");;
			TFE_System::logTimeToggle();
		}
	}

	void endRecording()
	{
		// Disable recording if it is not enabled for all when finished
		if (!TFE_Settings::getGameSettings()->df_enableRecordingAll)
		{
			TFE_Settings::getGameSettings()->df_enableRecording = false;
		}
		
		setRecording(false);

		// Seriaize the demo events
		if (setupPath())
		{
			serializeDemo(&s_replayFile, true);
		}

		// Re-enable cutscenes while recording
		enableCutscenes(JTRUE);

		// Wipe any latent input
		inputMapping_endFrame();

		TFE_DarkForces::hud_sendTextMessage("Recording ended.", 0, false);

		endCommonReplayStates();
		TFE_System::logWrite(LOG_MSG, "Replay", "Writing demo to %s", s_replayPath);
	}

	void recordReplayTime(u64 startTime)
	{
		replayStartTime = startTime;
	}

	bool startReplayStatus()
	{
		return replayInitialized;
	}

	void sendEndPlaybackMsg()
	{
		string msg = "Playback Complete!";
		TFE_DarkForces::hud_sendTextMessage(msg.c_str(), 1, false);
	}

	void sendEndRecordingMsg()
	{
		string msg = "Recording Ended...";
		TFE_DarkForces::hud_sendTextMessage(msg.c_str(), 1, false);
	}

	void loadReplayFromPath(const char * sourceReplayPath)
	{
		char replayPath[TFE_MAX_PATH];
		sprintf(replayPath, "%s", sourceReplayPath);
		
		FileUtil::fixupPath(replayPath);

		if (!FileUtil::exists(replayPath))
		{
			TFE_System::logWrite(LOG_MSG, "Replay", "Replay file does not exist: %s . Exiting...", replayPath);
			TFE_FrontEndUI::setState(APP_STATE_QUIT);
			return;
		}

		// Always set this to false as we are loading a replay from path
		alwaysRecord = TFE_Settings::getGameSettings()->df_enableRecordingAll;
		TFE_Settings::getGameSettings()->df_enableRecordingAll = false;

		TFE_SaveSystem::SaveHeader header;
		loadReplayHeader(replayPath, &header);

		TFE_DarkForces::enableCutscenes(false);

		loadReplayWrapper(replayPath, header.modNames, header.levelId);
	}

	// This is a replay wrapper that handles agents and replay configuration
	void loadReplayWrapper(string replayFile, string modName, string levelId)
	{
		TFE_System::logWrite(LOG_MSG, "Replay", "Loading Replay File = %s Modname = %s Levelid = %s", replayFile.c_str(), modName.c_str(), levelId.c_str());

		// If you are replaying a demo, you should not be recording.

		TFE_Settings::getGameSettings()->df_enableRecording = false;
		TFE_Settings::getGameSettings()->df_enableReplay = true;

		// Ensure you exit the current game in case you are running
		task_freeAll();
		TFE_FrontEndUI::exitToMenu();

		// Handle Agent 
		agentMenu_setAgentCount(agent_loadData());

		// Handle starting parameters
		char selectedModCmd[TFE_MAX_PATH];
		sprintf(selectedModCmd, "-u%s", modName.c_str());

		std::vector<std::string> modOverrides;
		modOverrides.push_back("-c0"); // No cutscenes		

		// Override level
		if (strlen(levelId.c_str()) > 0)
		{
			string levelOverride = "-l" + levelId;
			modOverrides.push_back(levelOverride);
		}

		// Set the replay parameters
		TFE_FrontEndUI::setModOverrides(modOverrides);


		sprintf(s_replayPath, "%s", replayFile.c_str());


		// Load the replay header so we can get the agent data
		replayFilehandler = serializeHeaderAgentInfo(&s_replayFile, false);		

		// Make sure you read all the mods before setting the selected mod
		TFE_FrontEndUI::modLoader_read();

		// Set the selected mod and GAME state
		TFE_FrontEndUI::setSelectedMod(selectedModCmd);
		TFE_FrontEndUI::setState(APP_STATE_GAME);

		// Preserve cutscene setting and disable them for the replay
		cutscenesEnabled = getCutscenesEnabled();
		enableCutscenes(JFALSE);

		// Wipe any current game that may be running and wipe the menu
		IGame* curGame = TFE_FrontEndUI::getCurrentGame();
		curGame = nullptr;
		TFE_FrontEndUI::clearMenuState();

		// This is needed to check if the replay is initialized in Dark Forces Main
		replayInitialized = true;

		TFE_System::logWrite(LOG_MSG, "Replay", "Loading demo % s", s_replayPath);
	}

	// This is called from the DarkForcesMain 
	void loadReplay()
	{	
		startCommonReplayStates();
	
		// Start replaying with the first event
		inputMapping_setReplayCounter(1);

		serializeDemo(&s_replayFile, false);

		// Setup frame rate for replay playback
		handleFrameRate();

		// Load the timing and seeds 
		loadInitTime();
		restoreReplaySeed();

		// Initialize Demo Playback
		setDemoPlayback(true);

		// Reset the eye
		TFE_DarkForces::player_clearEyeObject();

		// Ensure you always load the first agent. 
		s_agentId = 0;

		if (shouldLogReplay())
		{
			TFE_System::logClose();
			if (replayLogCounter == 0)
			{
				TFE_System::logOpen("replay.log");
			}
			else
			{
				char logPath[256];
				sprintf(logPath, "replay_%d.log", replayLogCounter);
				TFE_System::logOpen(logPath);
			}
			replayLogCounter++;
			TFE_System::logTimeToggle();
		}		
	}

	void restoreAgent()
	{
		// We always restore the first agent in the list
		s_agentId = 0;
		s_agentData[s_agentId] = replayAgent.agentData;
		agentMenu_setAgentName(replayAgent.agentData.name);
		agent_writeSavedData(s_agentId, &replayAgent);
	}

	void restoreGameSettings()
	{
		TFE_Settings_Game* gameSettings = TFE_Settings::getGameSettings();
		copyGameSettings(&replayGameSettings, gameSettings);
		TFE_Settings::getA11ySettings()->enableHeadwave = playerHeadwave;
		TFE_Settings::getGameSettings()->df_enableReplay = false;
	}

	void restoreInputs()
	{
		InputConfig* inputConfig = inputMapping_get();
		inputConfig->mouseMode = (MouseMode)s_mouseMode;
		inputConfig->mouseFlags = s_mouseFlags;
		inputConfig->mouseSensitivity[0] = s_mouseSensitivity[0];
		inputConfig->mouseSensitivity[1] = s_mouseSensitivity[1];
	}

	void endReplay()
	{
		replayInitialized = false;
		replayFilehandler = -1;
		setDemoPlayback(false);

		restoreAgent();
		restoreGameSettings();
		restoreInputs();

		enableCutscenes(cutscenesEnabled);
		endCommonReplayStates();

		TFE_System::logWrite(LOG_MSG, "Replay", "Finished playing back demo...");

		if (TFE_Settings::getTempSettings()->exit_after_replay)
		{
			// Reset the recording settings
			TFE_Settings::getGameSettings()->df_enableRecordingAll = alwaysRecord;
			TFE_FrontEndUI::setState(APP_STATE_QUIT);
		}
		else
		{
			// End mission and go back to Menu after replay. 
			TFE_DarkForces::mission_exitLevel();
			task_freeAll();
			TFE_FrontEndUI::exitToMenu();
		}
	}
}