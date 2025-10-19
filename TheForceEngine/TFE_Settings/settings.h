#pragma once
//////////////////////////////////////////////////////////////////////
// The Force Engine Settings
// This is a global repository for program settings in an INI like
// format.
//
// This includes reading and writing settings as well as storing an
// in-memory cache to get accessed at runtime.
//////////////////////////////////////////////////////////////////////

#include <TFE_System/types.h>
#include <TFE_System/iniParser.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_Audio/midiDevice.h>
#include "gameSourceData.h"
#include <map>

enum SkyMode
{
	SKYMODE_VANILLA = 0,
	SKYMODE_CYLINDER,
	SKYMODE_COUNT
};

enum ColorMode
{
	COLORMODE_8BIT = 0,		// Default vanilla
	COLORMODE_8BIT_INTERP,	// Interpolate between colormap values.
	COLORMODE_TRUE_COLOR,	// Will be enabled when the feature comes online.
	COLORMODE_COUNT,
};

static const char* c_tfeSkyModeStrings[] =
{
	"Vanilla",		// SKYMODE_VANILLA
	"Cylinder",		// SKYMODE_CYLINDER
};

// Special temporary settings, which are not serialized.
struct TFE_Settings_Temp
{
	bool skipLoadDelay = false;
	bool forceFullscreen = false;
	bool df_demologging = false;
	bool exit_after_replay = false;
};

struct TFE_Settings_Window
{
	s32 x = 0;
	s32 y = 64;
	u32 width = 1280;
	u32 height = 720;
	u32 baseWidth = 1280;
	u32 baseHeight = 720;
	bool fullscreen = true;
};

struct TFE_Settings_Graphics
{
	Vec2i gameResolution = { 320, 200 };
	bool  widescreen = false;
	bool  asyncFramebuffer = true;
	bool  gpuColorConvert = true;
	bool  colorCorrection = false;
	bool  perspectiveCorrectTexturing = false;
	bool  extendAjoinLimits = true;
	bool  vsync = true;
	bool  showFps = false;
	bool  fix3doNormalOverflow = true;
	bool  ignore3doLimits = true;
	bool  forceGouraudShading = false;
	bool  overrideLighting = false;
	bool  useSmoothDeltaTime = true;
	s32   frameRateLimit = 240;
	f32   brightness = 1.0f;
	f32   contrast = 1.0f;
	f32   saturation = 1.0f;
	f32   gamma = 1.0f;
	s32   fov = 90;
	s32   rendererIndex = 0;
	s32   colorMode = COLORMODE_8BIT;

	// 8-bit options.
	bool ditheredBilinear = false;

	// True-color options.
	bool useBilinear = false;
	bool useMipmapping = false;
	f32  bilinearSharpness = 1.0f;	// 0 disables and uses pure hardware bilinear.
	f32  anisotropyQuality = 1.0f;	// quality of anisotropic filtering, 0 disables.

	// Reticle
	bool reticleEnable  = false;
	s32  reticleIndex   = 6;
	f32  reticleRed     = 0.25f;
	f32  reticleGreen   = 1.00f;
	f32  reticleBlue    = 0.25f;
	f32  reticleOpacity = 1.00f;
	f32  reticleScale   = 1.0f;

	// Bloom options
	bool bloomEnabled = false;
	f32  bloomStrength = 0.4f;
	f32  bloomSpread = 0.6f;

	// Sky (Ignored when using the software renderer)
	s32  skyMode = SKYMODE_CYLINDER;
};

struct TFE_Settings_Enhancements
{
	bool enableHdTextures = false;
	bool enableHdSprites = false;
	bool enableHdHud = false;
};

enum TFE_HudScale
{
	TFE_HUDSCALE_PROPORTIONAL = 0,
	TFE_HUDSCALE_SCALED,
};

enum TFE_HudPosition
{
	TFE_HUDPOS_EDGE = 0,	// Hud elements on the edges of the screen.
	TFE_HUDPOS_4_3,			// Hud elements locked to 4:3 (even in widescreen).
};

enum PitchLimit
{
	PITCH_VANILLA = 0,
	PITCH_VANILLA_PLUS,
	PITCH_HIGH,
	PITCH_MAXIMUM,
	PITCH_COUNT
};

enum FontSize
{
	FONT_SMALL,
	FONT_MEDIUM,
	FONT_LARGE,
	FONT_XL
};

static const char* c_tfeHudScaleStrings[] =
{
	"Proportional",		// TFE_HUDSCALE_PROPORTIONAL
	"Scaled",			// TFE_HUDSCALE_SCALED
};

static const char* c_tfeHudPosStrings[] =
{
	"Edge",		// TFE_HUDPOS_EDGE
	"4:3",		// TFE_HUDPOS_4_3
};

static const char* c_tfePitchLimit[] =
{
	"Vanilla  (45 degrees)",
	"Vanilla+ (60 degrees)",
	"High     (75 degrees)",
	"Maximum"
};

struct TFE_Settings_Hud
{
	// Determines whether the HUD stays the same size on screen regardless of resolution or if it gets smaller with higher resolution.
	TFE_HudScale hudScale = TFE_HUDSCALE_PROPORTIONAL;
	// This setting determines how the left and right corners are calculated, which have an offset of (0,0).
	TFE_HudPosition hudPos = TFE_HUDPOS_EDGE;

	// Scale of the HUD, ignored if HudScale is TFE_HUDSCALE_PROPORTIONAL.
	f32 scale = 1.0f;
	// Pixel offset from the left hud corner, right is (-leftX, leftY)
	s32 pixelOffset[3] = { 0 };
};

struct TFE_Settings_Sound
{
	f32 masterVolume = 1.0f;
	f32 soundFxVolume = 0.75f;
	f32 musicVolume = 1.0f;
	f32 cutsceneSoundFxVolume = 0.9f;
	f32 cutsceneMusicVolume = 1.0f;
	s32 audioDevice = -1;			// Use the audio device default.
	s32 midiOutput  = -1;			// Use the midi type default.
	s32 midiType = MIDI_TYPE_DEFAULT;
	bool use16Channels = false;
	bool disableSoundInMenus = false;
};

struct TFE_Game
{
	char game[64] = "Dark Forces";
	GameID id;
};

struct TFE_GameHeader
{
	char gameName[64]="";
	char sourcePath[TFE_MAX_PATH]="";
	char emulatorPath[TFE_MAX_PATH]="";
};

struct TFE_Settings_Game
{
	TFE_GameHeader header[Game_Count];

	// Dark Forces
	s32  df_airControl = 0;				// Air control, default = 0, where 0 = speed/256 and 8 = speed; range = [0, 8]
	bool df_bobaFettFacePlayer = false;	// Make Boba Fett try to face the player in all his attack phases.
	bool df_smoothVUEs = false;			// Smooths VUE animations (e.g. the Moldy Crow entering and exiting levels)
	bool df_disableFightMusic  = false;	// Set to true to disable fight music and music transitions during gameplay.
	bool df_enableAutoaim      = true;  // Set to true to enable autoaim, false to disable.
	bool df_showSecretFoundMsg = true;  // Show a message when the player finds a secret.
	bool df_autorun = false;			// Run by default instead of walk.
	bool df_crouchToggle = false;		// Use toggle instead of hold for crouch.
	bool df_ignoreInfLimit = true;		// Ignore the vanilla INF limit.
	bool df_stepSecondAlt = false;		// Allow the player to step up onto second heights, similar to the way normal stairs work.
	bool df_solidWallFlagFix = true;	// Solid wall flag is enforced for collision with moving walls.
	bool df_enableUnusedItem = true;	// Enables the unused item in the inventory (delt 10).
	bool df_jsonAiLogics = true;		// AI logics can be loaded from external JSON files
	bool df_enableRecording = false;    // Enable recording of gameplay
	bool df_enableRecordingAll = false; // Always record gameplay. 
	bool df_enableReplay = false;       // Enable replay of gameplay.
	bool df_showReplayCounter = false;  // Show the replay counter on the HUD.
	bool df_demologging = false;        // Log the record/playback logging
	bool df_autoEndMission = false;     // Automatically skip to the next mission
  bool df_showKeyColors = false;      // Shows the door key color on the minimap
	s32  df_recordFrameRate = 4;        // Recording Framerate value
	s32  df_playbackFrameRate = 2;      // Playback Framerate value
	bool df_showKeyUsed = true; 	    // Show a message when a key is used.
	PitchLimit df_pitchLimit  = PITCH_VANILLA_PLUS;
};

struct TFE_Settings_System
{
	bool gameQuitExitsToMenu = true;		// Quitting from the game returns to the main menu instead.
	bool returnToModLoader = true;			// Return to the Mod Loader if running a mod.
	f32 gifRecordingFramerate = 18;			// Used with GIF recording (Alt-F2)
	bool showGifPathConfirmation = true;	// Used with GIF recording (Alt-F2)
};

struct TFE_Settings_A11y
{
	string language = "en"; //ISO 639-1 two-letter code
	string lastFontPath;

	bool showCutsceneSubtitles; // Voice
	bool showCutsceneCaptions;  // Descriptive (e.g. "[Mine beeping]", "[Engine roaring]"
	FontSize cutsceneFontSize;
	RGBA cutsceneFontColor = RGBA::fromFloats(1.0f, 1.0f, 1.0f);
	f32 cutsceneTextBackgroundAlpha = 0.75f;
	bool showCutsceneTextBorder = true;
	f32 cutsceneTextSpeed = 1.0f;

	bool showGameplaySubtitles; // Voice
	bool showGameplayCaptions;  // Descriptive
	FontSize gameplayFontSize;
	RGBA gameplayFontColor = RGBA::fromFloats(1.0f, 1.0f, 1.0f);
	int gameplayMaxTextLines = 3;
	f32 gameplayTextBackgroundAlpha = 0.0f;
	bool showGameplayTextBorder = false;
	f32 gameplayTextSpeed = 1.0f;
	s32 gameplayCaptionMinVolume = 32; // In range 0 - 127

	bool captionSystemEnabled()
	{
		return showCutsceneSubtitles || showCutsceneCaptions || showGameplaySubtitles || showGameplayCaptions;
	}

	// Motion sickness settings
	bool enableHeadwave = true;
	// Photosensitivity settings
	bool disableScreenFlashes = false;
	bool disablePlayerWeaponLighting = false;
};

enum ModSettingOverride
{
	MSO_NOT_SET = 0,
	MSO_TRUE,
	MSO_FALSE,
	MSO_COUNT
};

enum HdAssetType
{
	HD_ASSET_TYPE_BM = 0,
	HD_ASSET_TYPE_FME,
	HD_ASSET_TYPE_WAX,
	HD_ASSET_TYPE_COUNT
};

struct ModHdIgnoreList
{
	std::string levName;
	std::vector<std::string> bmIgnoreList;
	std::vector<std::string> fmeIgnoreList;
	std::vector<std::string> waxIgnoreList;
};

static const char* modIntOverrides[] =
{
	"energy",
	"power",
	"plasma",
	"detonator",
	"shell",
	"mine",
	"missile",
	"shields",
	"health",
	"lives",
	"battery",

	// Custom int overrides
	"defaultWeapon",
	"fogLevel",

	"floorDamageLow",
	"floorDamageHigh",
	"gasDamage",
	"wallDamage",
	"gravity",
	"projectileGravity",
	"shieldSuperchargeDuration",
	"weaponSuperchargeDuration",
};

// Float overrides for mod levels
static const char* modFloatOverrides[] =
{
	"headlampBatteryConsumption",
	"gogglesBatteryConsumption",
	"maskBatteryConsumption",
};

// Boolean overrides for mod levels 
static const char* modBoolOverrides[] =
{
	// Enable inventory items on start
	"enableMask",
	"enableCleats",
	"enableNightVision",
	"enableHeadlamp",

	// Add/Remove Weapons
	"pistol",
	"rifle",
	"autogun",
	"mortar",
	"fusion",
	"concussion",
	"cannon",

	// Toggleable items
	"mask",
	"goggles",
	"cleats",

	// Inventory items
	"plans",
	"phrik",
	"datatape",
	"nava",
	"dtWeapon",
	"code1",
	"code2",
	"code3",
	"code4",
	"code5",

	// Keys
	"yellowKey",
	"redKey",
	"blueKey",

	// Resets everything to only use bryar like the first mission.
	"bryarOnly"
};

static const char* modTextureOverrides[] =
{
	"loadScreen",
};

struct ModSettingLevelOverride
{
	std::string levName;
	std::map<std::string, int>  intOverrideMap = {};
	std::map<std::string, float> floatOverrideMap = {};
	std::map<std::string, bool> boolOverrideMap = {};
	std::map<std::string, TextureData*> textureOverrideMap = {};
};

struct TFE_ModSettings
{
	ModSettingOverride ignoreInfLimits   = MSO_NOT_SET;
	ModSettingOverride stepSecondAlt     = MSO_NOT_SET;
	ModSettingOverride solidWallFlagFix  = MSO_NOT_SET;
	ModSettingOverride extendAjoinLimits = MSO_NOT_SET;
	ModSettingOverride ignore3doLimits   = MSO_NOT_SET;
	ModSettingOverride normalFix3do      = MSO_NOT_SET;
	ModSettingOverride enableUnusedItem  = MSO_NOT_SET;
	ModSettingOverride jsonAiLogics      = MSO_NOT_SET;

	std::map<std::string, ModSettingLevelOverride> levelOverrides;
	std::vector<ModHdIgnoreList> ignoreList;
};

namespace TFE_Settings
{
	bool init(bool& firstRun);
	void shutdown();

	bool writeToDisk(bool writeDefaultSettings = false);

	// Get and set settings.
	TFE_Settings_Window* getWindowSettings();
	TFE_Settings_Graphics* getGraphicsSettings();
	TFE_Settings_Enhancements* getEnhancementsSettings();
	TFE_Settings_Hud* getHudSettings();
	TFE_Settings_Sound* getSoundSettings();
	TFE_Settings_System* getSystemSettings();
	TFE_Settings_Temp* getTempSettings();
	TFE_Game* getGame();
	TFE_GameHeader* getGameHeader(const char* gameName);
	TFE_Settings_Game* getGameSettings();
	TFE_Settings_A11y* getA11ySettings();
	TFE_ModSettings* getModSettings();

	// Helper functions.
	void setLevelName(const char* levelName);
	bool isHdAssetValid(const char* assetName, HdAssetType type);

	// Settings factoring in mod overrides.
	bool ignoreInfLimits();
	bool stepSecondAlt();
	bool solidWallFlagFix();
	bool extendAdjoinLimits();
	bool ignore3doLimits();
	bool normalFix3do();
	bool enableUnusedItem();
	bool jsonAiLogics();

	// Settings for level mod overrides.
	ModSettingLevelOverride* getLevelOverrides(string levelName);

	bool validatePath(const char* path, const char* sentinel);
	void autodetectGamePaths();
	void clearModSettings();
	void loadCustomModSettings();
	void parseIniFile(const char* buffer, size_t len);
	void writeDarkForcesGameSettings(FileStream& settings);
	void resetGameSettings();
	void resetAllSettings();
}
