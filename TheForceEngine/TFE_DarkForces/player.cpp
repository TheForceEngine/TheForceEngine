#include <cstring>

#include "player.h"
#include "playerLogic.h"
#include "playerCollision.h"
#include "projectile.h"
#include "automap.h"
#include "agent.h"
#include "config.h"
#include "hud.h"
#include "mission.h"
#include "pickup.h"
#include "weapon.h"
#include <TFE_System/system.h>
#include <TFE_FrontEndUI/console.h>
#include <TFE_Settings/settings.h>
#include <TFE_ExternalData/pickupExternal.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_Game/igame.h>
#include <TFE_Jedi/Level/level.h>
#include <TFE_Jedi/InfSystem/infSystem.h>
#include <TFE_Jedi/Renderer/rlimits.h>
#include <TFE_Jedi/Serialization/serialization.h>
// Internal types need to be included in this case.
#include <TFE_Jedi/InfSystem/infTypesInternal.h>
#include <TFE_Jedi/Renderer/jediRenderer.h>

// TFE
#include <TFE_System/tfeMessage.h>

using namespace TFE_Input;

namespace TFE_Jedi
{
	extern s32 s_flatAmbient;
	extern JBool s_flatLighting;
}

namespace TFE_DarkForces
{
	///////////////////////////////////////////
	// Constants
	///////////////////////////////////////////
	enum PlayerConstants
	{
		PLAYER_HEIGHT                  = 0x5cccc,	  // 5.8 units
		PLAYER_WIDTH                   = 0x1cccc,	  // 1.8 units
		PLAYER_PICKUP_ADJ              = 0x18000,	  // 1.5 units
		PLAYER_SIZE_SMALL              = 0x4ccc,	  // 0.3 units
		PLAYER_STEP                    = 0x38000,	  // 3.5 units
		PLAYER_INF_STEP                = COL_INFINITY,
		PLAYER_HEADWAVE_VERT_SPD       = 0xc000,	  // 0.75   units/sec.
		PLAYER_HEADWAVE_VERT_WATER_SPD = 0x3000,	  // 0.1875 units/sec.
		PLAYER_DMG_FLOOR_LOW           = FIXED(5),	  // Low  damage floors apply  5 dmg/sec.
		PLAYER_DMG_FLOOR_HIGH          = FIXED(10),	  // High damage floors apply 10 dmg/sec.
		PLAYER_DMG_WALL				   = FIXED(20),	  // Damage walls apply 20 dmg/sec.
		PLAYER_DMG_CRUSHING            = FIXED(10),   // The player suffers 10 dmg/sec. from crushing damage.
		PLAYER_FALL_SCREAM_VEL         = FIXED(60),	  // Fall speed when the player starts screaming.
		PLAYER_FALL_SCREAM_MINHEIGHT   = FIXED(55),   // The player needs to be at least 55 units from the ground before screaming.
		PLAYER_FALL_HIT_SND_HEIGHT     = FIXED(4),	  // 4.0 units, fall height for player to make a sound when hitting the ground.
		PLAYER_LAND_VEL_CHANGE		   = FIXED(60),	  // Point wear landing velocity to head change velocity changes slope.
		PLAYER_LAND_VEL_MAX            = FIXED(1000), // Maximum head change landing velocity.

		PLAYER_FORWARD_SPEED           = FIXED(256),  // Forward speed in units/sec.
		PLAYER_STRAFE_SPEED            = FIXED(192),  // Strafe speed in units/sec.
		PLAYER_CROUCH_SPEED            = 747108,      // 11.4 units/sec
		PLAYER_MOUSE_TURN_SPD          = 8,           // Mouse position delta to turn speed multiplier.
		PLAYER_KB_TURN_SPD             = 3413,        // Keyboard turn speed in angles/sec.
		PLAYER_CONTROLLER_TURN_SPD     = 3413*3,	  // Controller turn speed in angles/sec.
		PLAYER_CONTROLLER_PITCH_SPD    = 3413*2,	  // Controller turn speed in angles/sec.
		PLAYER_JUMP_IMPULSE            = -2850816,    // Jump vertical impuse: -43.5 units/sec.

		PLAYER_MAX_MOVE_DIST           = FIXED(32),
		PLAYER_STOP_ACCEL              = -540672,	  // -8.25
		PLAYER_MIN_EYE_DIST_FLOOR      = FIXED(2),
		PLAYER_GRAVITY_ACCEL           = FIXED(150),

		HEADLAMP_ENERGY_CONSUMPTION = 0x111,    // fraction of energy consumed per second = ~0.004
		GASMASK_ENERGY_CONSUMPTION  = 0x444,    // fraction of energy consumed per second = ~0.0167
		GOGGLES_ENERGY_CONSUMPTION  = 0x444,    // fraction of energy consumed per second = ~0.0167
		SUPERCHARGE_DURATION        = 6554,     // 45 seconds
	};

	static const s32 c_pitchLimits[] =
	{
		2047, // PITCH_VANILLA
		2730, // PITCH_VANILLA_PLUS
		3413, // PITCH_HIGH
		4050, // PITCH_MAXIMUM
	};
	static s32 PITCH_LIMIT = c_pitchLimits[PITCH_VANILLA];

	// Lower = Less sliding/stops faster,
	// Higher = More sliding, takes longer to stop.
	enum Friction
	{
		FRICTION_CLEATS  = 0xf333, // 0.950
		FRICTION_DEFAULT = 0xf999, // 0.975
		FRICTION_FALLING = 0xfef9, // 0.996
		FRICTION_ICE     = 0xffbe, // 0.999
	};

	enum SpecialSpeeds
	{
		ICE_SPEED_MUL     = 0x11eb, // 0.070
		MOVING_SURFACE_MUL = 0xcccc, // 0.8
	};

	static const s32 c_healthDmgToFx[] =
	{
		4, 8, 12, 16, 20, 24, 28, 32,
		36, 40, 44, 48, 52, 56, 60, 63,
	};

	static const s32 c_shieldDmgToFx[] =
	{
		2, 4, 6, 8, 10, 12, 14, 16,
		18, 20, 22, 24, 26, 28, 30, 32
	};

	static const s32 c_flashAmtToFx[] =
	{
		1, 2, 3, 4, 5, 6, 7, 8, 9,
		10, 11, 12, 13, 14, 15, 16
	};

	const fixed16_16 c_flashDelta  = 39321 * 145;	// 0.6 seconds in fixed point ticks (i.e. FIXED(0.6) * TICKS_PER_SEC)
	const fixed16_16 c_damageDelta =  6553 * 145;	// 0.1
	const fixed16_16 c_shieldDelta = 13107 * 145;	// 0.2

	///////////////////////////////////////////
	// Internal State
	///////////////////////////////////////////
	// Player Controller
	static fixed16_16 s_externalYawSpd;
	static angle14_32 s_playerPitch;
	static angle14_32 s_playerRoll;
	static fixed16_16 s_forwardSpd;
	static fixed16_16 s_strafeSpd;
	static fixed16_16 s_maxMoveDist;
	static fixed16_16 s_playerStopAccel;
	static fixed16_16 s_minEyeDistFromFloor;
	static fixed16_16 s_postLandVel;
	static fixed16_16 s_landUpVel = 0;
	static fixed16_16 s_externalVelX;
	static fixed16_16 s_externalVelZ;
	static fixed16_16 s_playerCrouchSpd;
	static fixed16_16 s_playerSpeedAve;
	static fixed16_16 s_prevDistFromFloor = 0;
	static fixed16_16 s_wpnSin = 0;
	static fixed16_16 s_wpnCos = 0;
	static fixed16_16 s_moveDirX;
	static fixed16_16 s_moveDirZ;
	static fixed16_16 s_dist;
	static fixed16_16 s_distScale;
	// Misc
	static s32 s_levelAtten = 0;
	static s32 s_prevCollisionFrameWall;
	static Safe* s_curSafe = nullptr;
	// Actions
	static JBool s_playerUse = JFALSE;
	static JBool s_playerActionUse = JFALSE;
	static JBool s_playerPrimaryFire = JFALSE;
	static JBool s_playerSecFire = JFALSE;
	static JBool s_playerJumping = JFALSE;
	static JBool s_playerInWater = JFALSE;
	JBool s_aiActive = JTRUE;
	// Currently playing sound effects.
	static SoundEffectId s_crushSoundId = 0;
	static SoundEffectId s_kyleScreamSoundId = 0;
	// Position and orientation.
	static vec3_fixed s_playerPos;
	static fixed16_16 s_playerObjHeight;
	static angle14_32 s_playerObjPitch;
	static angle14_32 s_playerObjYaw;
	static RSector* s_playerObjSector;
	static RWall* s_playerSlideWall;

	// TFE - constants which can be overridden
	static s32 s_lowFloorDamage = PLAYER_DMG_FLOOR_LOW;
	static s32 s_highFloorDamage = PLAYER_DMG_FLOOR_HIGH;
	static s32 s_gasDamage = PLAYER_DMG_FLOOR_LOW;
	static s32 s_wallDamage = PLAYER_DMG_WALL;
	static s32 s_headlampConsumption = HEADLAMP_ENERGY_CONSUMPTION;
	static s32 s_gogglesConsumption = GOGGLES_ENERGY_CONSUMPTION;
	static s32 s_gasmaskConsumption = GASMASK_ENERGY_CONSUMPTION;
	
	///////////////////////////////////////////
	// Shared State
	///////////////////////////////////////////
	PlayerInfo s_playerInfo = { 0 };
	PlayerLogic s_playerLogic = {};
	fixed16_16 s_batteryPower = 2 * ONE_16;
	s32 s_lifeCount;
	s32 s_playerLight = 0;
	s32 s_headwaveVerticalOffset;
	JBool s_onFloor = JTRUE;
	s32 s_weaponLight = 0;
	s32 s_baseAtten = 0;
	fixed16_16 s_gravityAccel;

	s32   s_invincibility = 0;
	JBool s_weaponFiring = JFALSE;
	JBool s_weaponFiringSec = JFALSE;
	JBool s_wearingCleats = JFALSE;
	JBool s_wearingGasmask = JFALSE;
	JBool s_nightVisionActive = JFALSE;
	JBool s_headlampActive = JFALSE;
	JBool s_superCharge   = JFALSE;
	JBool s_superChargeHud= JFALSE;
	JBool s_playerSecMoved = JFALSE;
	JBool s_limitStepHeight = JTRUE;
	JBool s_smallModeEnabled = JFALSE;
	JBool s_flyMode = JFALSE;
	JBool s_noclip = JFALSE;
	JBool s_oneHitKillEnabled = JFALSE;
	JBool s_instaDeathEnabled = JFALSE;
	u32*  s_playerInvSaved = nullptr;

	RSector* s_playerSector = nullptr;
	SecObject* s_playerObject = nullptr;
	SecObject* s_playerEye = nullptr;
	vec3_fixed s_eyePos = { 0 };	// s_camX, s_camY, s_camZ in the DOS code.
	angle14_32 s_eyePitch = 0, s_eyeYaw = 0, s_eyeRoll = 0;
	JBool s_externalCameraMode = JFALSE;
	u32 s_playerEyeFlags = OBJ_FLAG_NEEDS_TRANSFORM;
	Tick s_playerTick;
	Tick s_prevPlayerTick;
	Tick s_nextShieldDmgTick;
	Tick s_reviveTick = 0;
	Tick s_nextPainSndTick = 0;
	Task* s_playerTask = nullptr;

	fixed16_16 s_playerYPos;
	vec3_fixed s_camOffset = { 0 };
	angle14_32 s_camOffsetPitch = 0;
	angle14_32 s_camOffsetYaw = 0;
	angle14_32 s_camOffsetRoll = 0;
	angle14_32 s_playerYaw;

	// Expose player velocity for scripting
	fixed16_16 s_playerVelX;
	fixed16_16 s_playerUpVel;
	fixed16_16 s_playerUpVel2;
	fixed16_16 s_playerVelZ;

	// Based on positioning in inventory, these were probably meant to represent thermal
	// detonators and mines - see player_readInfo() and player_writeInfo()
	JBool s_itemUnknown1;	// 0x282428	
	JBool s_itemUnknown2;	// 0x28242c 

	SoundSourceId s_landSplashSound;
	SoundSourceId s_landSolidSound;
	SoundSourceId s_gasDamageSoundSource;
	SoundSourceId s_maskSoundSource1;
	SoundSourceId s_maskSoundSource2;
	SoundSourceId s_invCountdownSound;
	SoundSourceId s_jumpSoundSource;
	SoundSourceId s_nightVisionActiveSoundSource;
	SoundSourceId s_nightVisionDeactiveSoundSource;
	SoundSourceId s_playerDeathSoundSource;
	SoundSourceId s_crushSoundSource;
	SoundSourceId s_playerHealthHitSoundSource;
	SoundSourceId s_kyleScreamSoundSource;
	SoundSourceId s_playerShieldHitSoundSource;

	fixed16_16 s_playerHeight;
	// Speed Modifiers
	s32 s_playerRun = 0;
	s32 s_jumpScale = 0;
	s32 s_playerSlow = 0;
	s32 s_onMovingSurface = 0;
	// Other
	s32 s_playerCrouch = 0;

	// TFE - Pointers to player ammo stores
	s32* s_playerAmmoEnergy;
	s32* s_playerAmmoPower;
	s32* s_playerAmmoPlasma;
	s32* s_playerAmmoShell;
	s32* s_playerAmmoDetonators;
	s32* s_playerAmmoMines;
	s32* s_playerAmmoMissiles;
	s32* s_playerShields;
	s32* s_playerHealth;
	fixed16_16* s_playerBatteryPower;

	// TFE - Maximum values for ammo, etc.
	s32 s_ammoEnergyMax;
	s32 s_ammoPowerMax;
	s32 s_ammoShellMax;
	s32 s_ammoPlasmaMax;
	s32 s_ammoDetonatorMax;
	s32 s_ammoMineMax;
	s32 s_ammoMissileMax;
	s32 s_shieldsMax;
	fixed16_16 s_batteryPowerMax;
	s32 s_healthMax;

	// TFE - constants which can be overridden
	s32 s_weaponSuperchargeDuration;
	s32 s_shieldSuperchargeDuration;

	// Scripting
	JBool s_disablePlayerMovement = JFALSE;
	JBool s_disablePlayerRotation = JFALSE;
	JBool s_disablePlayerFire = JFALSE;
			   
	///////////////////////////////////////////
	// Forward Declarations
	///////////////////////////////////////////
	void playerControlTaskFunc(MessageType msg);
	void playerControlMsgFunc(MessageType msg);
	void setPlayerLight(s32 atten);
	void setCameraOffset(fixed16_16 offsetX, fixed16_16 offsetY, fixed16_16 offsetZ);
	void setCameraAngleOffset(angle14_32 offsetPitch, angle14_32 offsetYaw, angle14_32 offsetRoll);
	void gasSectorTaskFunc(MessageType msg);

	void handlePlayerMoveControls();
	void handlePlayerPhysics();
	void handlePlayerActions();
	void handlePlayerScreenFx();

	void tfe_showSecretFoundMsg();

	// TFE
	void player_warp(const ConsoleArgList& args);
	void player_sector(const ConsoleArgList& args);
		
	///////////////////////////////////////////
	// API Implentation
	///////////////////////////////////////////
	void player_init()
	{
		s_landSplashSound                = sound_load("swim-in.voc",  SOUND_PRIORITY_HIGH4);
		s_landSolidSound                 = sound_load("land-1.voc",   SOUND_PRIORITY_HIGH4);
		s_gasDamageSoundSource           = sound_load("choke.voc",    SOUND_PRIORITY_MED2);
		s_maskSoundSource1               = sound_load("mask1.voc",    SOUND_PRIORITY_MED2);
		s_maskSoundSource2               = sound_load("mask2.voc",    SOUND_PRIORITY_MED2);
		s_invCountdownSound              = sound_load("quarter.voc",  SOUND_PRIORITY_HIGH4);
		s_jumpSoundSource                = sound_load("jump-1.voc",   SOUND_PRIORITY_HIGH4);
		s_nightVisionActiveSoundSource   = sound_load("goggles1.voc", SOUND_PRIORITY_MED2);
		s_nightVisionDeactiveSoundSource = sound_load("goggles2.voc", SOUND_PRIORITY_MED2);
		s_playerDeathSoundSource         = sound_load("kyledie1.voc", SOUND_PRIORITY_HIGH5);
		s_crushSoundSource               = sound_load("crush.voc",    SOUND_PRIORITY_HIGH4);
		s_playerHealthHitSoundSource     = sound_load("health1.voc",  SOUND_PRIORITY_HIGH4);
		s_kyleScreamSoundSource          = sound_load("fall.voc",     SOUND_PRIORITY_MED4);
		s_playerShieldHitSoundSource     = sound_load("shield1.voc",  SOUND_PRIORITY_MED5);

		s_playerAmmoEnergy = &s_playerInfo.ammoEnergy;
		s_playerAmmoPower = &s_playerInfo.ammoPower;
		s_playerAmmoPlasma = &s_playerInfo.ammoPlasma;
		s_playerAmmoShell = &s_playerInfo.ammoShell;
		s_playerAmmoDetonators = &s_playerInfo.ammoDetonator;
		s_playerAmmoMines = &s_playerInfo.ammoMine;
		s_playerAmmoMissiles = &s_playerInfo.ammoMissile;
		s_playerShields = &s_playerInfo.shields;
		s_playerHealth = &s_playerInfo.health;
		s_playerBatteryPower = &s_batteryPower;

		TFE_ExternalData::MaxAmounts* maxAmounts = TFE_ExternalData::getMaxAmounts();
		s_ammoEnergyMax = maxAmounts->ammoEnergyMax;
		s_ammoPowerMax = maxAmounts->ammoPowerMax;
		s_ammoShellMax = maxAmounts->ammoShellMax;
		s_ammoPlasmaMax = maxAmounts->ammoPlasmaMax;
		s_ammoDetonatorMax = maxAmounts->ammoDetonatorMax;
		s_ammoMineMax = maxAmounts->ammoMineMax;
		s_ammoMissileMax = maxAmounts->ammoMissileMax;
		s_shieldsMax = maxAmounts->shieldsMax;
		s_batteryPowerMax = maxAmounts->batteryPowerMax;
		s_healthMax = maxAmounts->healthMax;

		s_playerInfo = { 0 }; // Make sure this is clear...
		s_playerInfo.ammoEnergy  = pickup_addToValue(0, 100, s_ammoEnergyMax);
		s_playerInfo.ammoPower   = pickup_addToValue(0, 100, s_ammoPowerMax);
		s_playerInfo.ammoPlasma  = pickup_addToValue(0, 100, s_ammoPlasmaMax);
		s_playerInfo.shields     = pickup_addToValue(0, 100, s_shieldsMax);
		s_playerInfo.health      = pickup_addToValue(0, 100, s_healthMax);
		s_playerInfo.healthFract = 0;
		s_batteryPower = s_batteryPowerMax;
		// Always reset player ticks on init for replay consistency
		s_playerTick = 0;
		s_prevPlayerTick = 0;
		s_reviveTick = 0;

		s_automapLocked = JTRUE;

		CCMD("warp", player_warp, 3, "Warp to the specific x, y, z position.");
		CCMD_NOREPEAT("sector", player_sector, 0, "Get the current sector ID.");

		initPlayerCollision();
	}

	void player_setPitchLimit(PitchLimit limit)
	{
		if (limit < PITCH_VANILLA || limit > PITCH_COUNT)
		{
			PITCH_LIMIT = c_pitchLimits[PITCH_VANILLA];
		}
		else
		{
			PITCH_LIMIT = c_pitchLimits[limit];
		}
		s_playerPitch = clamp(s_playerPitch, -PITCH_LIMIT, PITCH_LIMIT);
	}

	void player_readInfo(u8* inv, s32* ammo)
	{
		// Read the inventory.
		s_playerInfo.itemPistol    = inv[0];
		s_playerInfo.itemRifle     = inv[1];
		s_itemUnknown1             = inv[2];
		s_playerInfo.itemAutogun   = inv[3];
		s_playerInfo.itemMortar    = inv[4];
		s_playerInfo.itemFusion    = inv[5];
		s_itemUnknown2             = inv[6];
		s_playerInfo.itemConcussion= inv[7];
		s_playerInfo.itemCannon    = inv[8];
		s_playerInfo.itemRedKey    = inv[9];
		s_playerInfo.itemYellowKey = inv[10];
		s_playerInfo.itemBlueKey   = inv[11];
		s_playerInfo.itemGoggles   = inv[12];
		s_playerInfo.itemCleats    = inv[13];
		s_playerInfo.itemMask      = inv[14];
		s_playerInfo.itemPlans     = inv[15];
		s_playerInfo.itemPhrik     = inv[16];
		s_playerInfo.itemNava      = inv[17];
		s_playerInfo.itemDatatape  = inv[18];
		s_playerInfo.itemUnused    = inv[19];
		s_playerInfo.itemDtWeapon  = inv[20];
		s_playerInfo.itemCode1 = inv[21];
		s_playerInfo.itemCode2 = inv[22];
		s_playerInfo.itemCode3 = inv[23];
		s_playerInfo.itemCode4 = inv[24];
		s_playerInfo.itemCode5 = inv[25];
		s_playerInfo.itemCode6 = inv[26];
		s_playerInfo.itemCode7 = inv[27];
		s_playerInfo.itemCode8 = inv[28];
		s_playerInfo.itemCode9 = inv[29];

		// Handle current weapon.
		s32 curWeapon = inv[30];
		weapon_setNext(curWeapon);
		s_playerInfo.maxWeapon = max(curWeapon, s_playerInfo.maxWeapon);
		s_playerInfo.newWeapon = -1;

		// Life Count.
		s_lifeCount = inv[31];

		// Read Ammo, shields, health and energy.
		s_playerInfo.ammoEnergy    = ammo[0];
		s_playerInfo.ammoPower     = ammo[1];
		s_playerInfo.ammoPlasma    = ammo[2];
		s_playerInfo.ammoDetonator = ammo[3];
		s_playerInfo.ammoShell     = ammo[4];
		s_playerInfo.ammoMine      = ammo[5];
		s_playerInfo.ammoMissile   = ammo[6];
		s_playerInfo.shields = ammo[7];
		s_playerInfo.health  = ammo[8];
		s_batteryPower = ammo[9];
	}

	void player_writeInfo(u8* inv, s32* ammo)
	{
		// Write the inventory.
		inv[0]  = s_playerInfo.itemPistol;
		inv[1]  = s_playerInfo.itemRifle;
		inv[2]  = s_itemUnknown1;
		inv[3]  = s_playerInfo.itemAutogun;
		inv[4]  = s_playerInfo.itemMortar;
		inv[5]  = s_playerInfo.itemFusion;
		inv[6]  = s_itemUnknown2;
		inv[7]  = s_playerInfo.itemConcussion;
		inv[8]  = s_playerInfo.itemCannon;
		inv[9]  = s_playerInfo.itemRedKey;
		inv[10] = s_playerInfo.itemYellowKey;
		inv[11] = s_playerInfo.itemBlueKey;
		inv[12] = s_playerInfo.itemGoggles;
		inv[13] = s_playerInfo.itemCleats;
		inv[14] = s_playerInfo.itemMask;
		inv[15] = s_playerInfo.itemPlans;
		inv[16] = s_playerInfo.itemPhrik;
		inv[17] = s_playerInfo.itemNava;
		inv[18] = s_playerInfo.itemDatatape;
		inv[19] = s_playerInfo.itemUnused;
		inv[20] = s_playerInfo.itemDtWeapon;
		inv[21] = s_playerInfo.itemCode1;
		inv[22] = s_playerInfo.itemCode2;
		inv[23] = s_playerInfo.itemCode3;
		inv[24] = s_playerInfo.itemCode4;
		inv[25] = s_playerInfo.itemCode5;
		inv[26] = s_playerInfo.itemCode6;
		inv[27] = s_playerInfo.itemCode7;
		inv[28] = s_playerInfo.itemCode8;
		inv[29] = s_playerInfo.itemCode9;
		inv[30] = s_playerInfo.curWeapon;
		inv[31] = s_lifeCount;

		// Write Ammo, Shields, Health, and Energy.
		ammo[0] = s_playerInfo.ammoEnergy;
		ammo[1] = s_playerInfo.ammoPower;
		ammo[2] = s_playerInfo.ammoPlasma;
		ammo[3] = s_playerInfo.ammoDetonator;
		ammo[4] = s_playerInfo.ammoShell;
		ammo[5] = s_playerInfo.ammoMine;
		ammo[6] = s_playerInfo.ammoMissile;
		ammo[7] = s_playerInfo.shields;
		ammo[8] = s_playerInfo.health;
		ammo[9] = s_batteryPower;
	}

	void player_setNextWeapon(s32 nextWpn)
	{
		weapon_setNext(nextWpn);
		if (nextWpn > s_playerInfo.maxWeapon)
		{
			s_playerInfo.maxWeapon = nextWpn;
		}
	}

	void player_handleLevelOverrides(ModSettingLevelOverride modLevelOverride)
	{
		// Handle Numeric Overrides
		std::map<std::string, int> intMap = modLevelOverride.intOverrideMap;
		std::map<std::string, float> floatMap = modLevelOverride.floatOverrideMap;

		// Handle Ammo/Shields/Lives/Battery
		if (intMap.find("energy") != intMap.end())
		{
			s_playerInfo.ammoEnergy = pickup_addToValue(0, intMap["energy"], 999);
		}
		if (intMap.find("power") != intMap.end())
		{
			s_playerInfo.ammoPower = pickup_addToValue(0, intMap["power"], 999);
		}
		if (intMap.find("plasma") != intMap.end())
		{
			s_playerInfo.ammoPlasma = pickup_addToValue(0, intMap["plasma"], 999);
		}
		if (intMap.find("detonator") != intMap.end())
		{
			s_playerInfo.ammoDetonator = pickup_addToValue(0, intMap["detonator"], 999);
		}
		if (intMap.find("shell") != intMap.end())
		{
			s_playerInfo.ammoShell = pickup_addToValue(0, intMap["shell"], 999);
		}
		if (intMap.find("mine") != intMap.end())
		{
			s_playerInfo.ammoMine = pickup_addToValue(0, intMap["mine"], 999);
		}
		if (intMap.find("missile") != intMap.end())
		{
			s_playerInfo.ammoMissile = pickup_addToValue(0, intMap["missile"], 99);
		}
		if (intMap.find("shields") != intMap.end())
		{
			s_playerInfo.shields = pickup_addToValue(0, intMap["shields"], 999);
		}
		if (intMap.find("health") != intMap.end())
		{
			s_playerInfo.health = pickup_addToValue(0, intMap["health"], 999);
		}
		if (intMap.find("lives") != intMap.end())
		{
			s_lifeCount = pickup_addToValue(0, intMap["lives"], 9);
		}
		if (intMap.find("battery") != intMap.end())
		{
			fixed16_16 batteryPower = FIXED(2);
			int batteryPowerPercent = pickup_addToValue(0, intMap["battery"], 100);
			s_batteryPower = (batteryPower * batteryPowerPercent) / 100;
		}

		// Note - this doesn't check if the weapon is in the inventory!
		if (intMap.find("defaultWeapon") != intMap.end())
		{
			int weaponSwitchID = pickup_addToValue(0, intMap["defaultWeapon"], 9);

			// Map it to the weapon index on the KEYBOARD (Ex: 1 = fists, 2  = bryar etc..)
			if (weaponSwitchID == 0)
			{
				player_setNextWeapon(9);
			}
			else
			{
				player_setNextWeapon(weaponSwitchID - 1);
			}
		}

		// Handle attenuation such as Gromas
		if (intMap.find("fogLevel") != intMap.end())
		{
			s_levelAtten = pickup_addToValue(0, intMap["fogLevel"], 100);
		}

		// Constants
		if (intMap.find("floorDamageLow") != intMap.end())
		{
			s_lowFloorDamage = FIXED(intMap["floorDamageLow"]);
		}
		if (intMap.find("floorDamageHigh") != intMap.end())
		{
			s_highFloorDamage = FIXED(intMap["floorDamageHigh"]);
		}
		if (intMap.find("gasDamage") != intMap.end())
		{
			s_gasDamage = FIXED(intMap["gasDamage"]);
		}
		if (intMap.find("wallDamage") != intMap.end())
		{
			s_wallDamage = FIXED(intMap["wallDamage"]);
		}
		if (intMap.find("gravity") != intMap.end())
		{
			s_gravityAccel = FIXED(intMap["gravity"]);
		}
		if (intMap.find("projectileGravity") != intMap.end())
		{
			setProjectileGravityAccel(FIXED(intMap["projectileGravity"]));
		}

		if (intMap.find("shieldSuperchargeDuration") != intMap.end())
		{
			s_shieldSuperchargeDuration = intMap["shieldSuperchargeDuration"] * TICKS_PER_SECOND;
		}
		if (intMap.find("weaponSuperchargeDuration") != intMap.end())
		{
			s_weaponSuperchargeDuration = intMap["weaponSuperchargeDuration"] * TICKS_PER_SECOND;
		}

		if (floatMap.find("headlampBatteryConsumption") != floatMap.end())
		{
			float* value = &floatMap["headlampBatteryConsumption"];
			s_headlampConsumption = (s32) ((*value / 100.00) * FIXED(2));
		}

		if (floatMap.find("gogglesBatteryConsumption") != floatMap.end())
		{
			float* value = &floatMap["gogglesBatteryConsumption"];
			s_gogglesConsumption = (s32)((*value / 100.00) * FIXED(2));
		}

		if (floatMap.find("maskBatteryConsumption") != floatMap.end())
		{
			float* value = &floatMap["maskBatteryConsumption"];
			s_gasmaskConsumption = (s32)((*value / 100.00) * FIXED(2));
		}

		// Handle Boolean Overrides
		std::map<std::string, bool> boolMap = modLevelOverride.boolOverrideMap;

		// Handle Weapons
		if (boolMap.find("pistol") != boolMap.end())
		{
			s_playerInfo.itemPistol = boolMap["pistol"];
		}
		if (boolMap.find("rifle") != boolMap.end())
		{
			s_playerInfo.itemRifle = boolMap["rifle"];
		}
		if (boolMap.find("autogun") != boolMap.end())
		{
			s_playerInfo.itemAutogun = boolMap["autogun"];
		}
		if (boolMap.find("mortar") != boolMap.end())
		{
			s_playerInfo.itemMortar = boolMap["mortar"];
		}
		if (boolMap.find("fusion") != boolMap.end())
		{
			s_playerInfo.itemFusion = boolMap["fusion"];
		}
		if (boolMap.find("concussion") != boolMap.end())
		{
			s_playerInfo.itemConcussion = boolMap["concussion"];
		}
		if (boolMap.find("cannon") != boolMap.end())
		{
			s_playerInfo.itemCannon = boolMap["cannon"];
		}

		// Handle Activatable Items

		if (boolMap.find("mask") != boolMap.end())
		{
			s_playerInfo.itemMask = boolMap["mask"];
		}
		if (boolMap.find("goggles") != boolMap.end())
		{
			s_playerInfo.itemGoggles = boolMap["goggles"];
		}
		if (boolMap.find("cleats") != boolMap.end())
		{
			s_playerInfo.itemCleats = boolMap["cleats"];
		}

		// Handle Items 

		if (boolMap.find("plans") != boolMap.end())
		{
			s_playerInfo.itemPlans = boolMap["plans"];
		}
		if (boolMap.find("phrik") != boolMap.end())
		{
			s_playerInfo.itemPhrik = boolMap["phrik"];
		}
		if (boolMap.find("nava") != boolMap.end())
		{
			s_playerInfo.itemNava = boolMap["nava"];
		}
		if (boolMap.find("datatape") != boolMap.end())
		{
			s_playerInfo.itemDatatape = boolMap["datatape"];
		}
		if (boolMap.find("dtWeapon") != boolMap.end())
		{
			s_playerInfo.itemDtWeapon = boolMap["dtWeapon"];
		}
		if (boolMap.find("code1") != boolMap.end())
		{
			s_playerInfo.itemCode1 = boolMap["code1"];
		}
		if (boolMap.find("code2") != boolMap.end())
		{
			s_playerInfo.itemCode2 = boolMap["code2"];
		}
		if (boolMap.find("code3") != boolMap.end())
		{
			s_playerInfo.itemCode3 = boolMap["code3"];
		}
		if (boolMap.find("code4") != boolMap.end())
		{
			s_playerInfo.itemCode4 = boolMap["code4"];
		}
		if (boolMap.find("code5") != boolMap.end())
		{
			s_playerInfo.itemCode5 = boolMap["code5"];
		}

		// Handle Keys
		if (boolMap.find("redKey") != boolMap.end())
		{
			s_playerInfo.itemRedKey = boolMap["redKey"];
		}
		if (boolMap.find("yellowKey") != boolMap.end())
		{
			s_playerInfo.itemYellowKey = boolMap["yellowKey"];
		}
		if (boolMap.find("blueKey") != boolMap.end())
		{
			s_playerInfo.itemBlueKey = boolMap["blueKey"];
		}

		// Enable Activatable items

		if (boolMap.find("enableMask") != boolMap.end())
		{
			if (boolMap["enableMask"])
			{
				s_playerInfo.itemMask = JTRUE;
				enableMask();
			}
		}
		if (boolMap.find("enableCleats") != boolMap.end())
		{
			if (boolMap["enableCleats"])
			{
				s_playerInfo.itemCleats = JTRUE;
				enableCleats();
			}
		}
		if (boolMap.find("enableNightVision") != boolMap.end())
		{
			if (boolMap["enableNightVision"])
			{
				s_playerInfo.itemGoggles = JTRUE;
				enableNightVision();
			}
		}
		if (boolMap.find("enableHeadlamp") != boolMap.end())
		{
			if (boolMap["enableHeadlamp"])
			{
				enableHeadlamp();
			}
		}

		// Wipe everything and start only with Bryar Pistol Only

		if (boolMap.find("bryarOnly") != boolMap.end() && boolMap["bryarOnly"] == MSO_TRUE)
		{

			// Wipe the player inventory settings.
			u8* src = (u8*)&s_playerInfo;
			size_t size = (size_t)&s_playerInfo.pileSaveMarker - (size_t)&s_playerInfo;
			assert(size == 140);
			memset(src, 0, size);

			// Give player 100 blaster ammo and a Bryar Pistol
			s_playerInfo.ammoEnergy = pickup_addToValue(0, 100, 999);
			s_playerInfo.itemPistol = JTRUE;
			player_setNextWeapon(1);

			// Disable all activatable items
			disableMask();
			disableCleats();
			disableNightVision();
			hud_clearMessage();
		}
	}
		
	void player_createController(JBool clearData)
	{
		s_playerTask = createTask("player control", playerControlTaskFunc, JFALSE, playerControlMsgFunc);
		task_setNextTick(s_playerTask, TASK_SLEEP);
		if (!clearData) { return; }

		// Clear out inventory items that the player shouldn't start a level with, such as objectives and keys
		s_playerInfo.itemPlans    = JFALSE;
		s_playerInfo.itemPhrik    = JFALSE;
		s_playerInfo.itemNava     = JFALSE;
		s_playerInfo.itemDatatape = JFALSE;
		s_playerInfo.itemUnused   = JFALSE;
		s_playerInfo.itemDtWeapon = JFALSE;
		s_playerInfo.itemCode1    = JFALSE;
		s_playerInfo.itemCode2    = JFALSE;
		s_playerInfo.itemCode3    = JFALSE;
		s_playerInfo.itemCode4    = JFALSE;
		s_playerInfo.itemCode5    = JFALSE;
		s_playerInfo.itemCode6    = JFALSE;
		s_playerInfo.itemCode7    = JFALSE;
		s_playerInfo.itemCode8    = JFALSE;
		s_playerInfo.itemCode9    = JFALSE;
		s_playerInfo.itemRedKey   = JFALSE;
		s_playerInfo.itemYellowKey= JFALSE;
		s_playerInfo.itemBlueKey  = JFALSE;

		// Clear out any lingering damage or flash effects.
		s_healthDamageFx = 0;
		s_shieldDamageFx = 0;
		s_flashEffect = 0;

		s_playerDying = 0;
		s_externalYawSpd = 0;
		s_playerPitch = 0;
		s_playerRoll = 0;
		s_forwardSpd = 0;
		s_strafeSpd = 0;

		// Set constants.
		s_maxMoveDist         = PLAYER_MAX_MOVE_DIST;
		s_playerStopAccel     = PLAYER_STOP_ACCEL;
		s_minEyeDistFromFloor = PLAYER_MIN_EYE_DIST_FLOOR;
		s_gravityAccel        = PLAYER_GRAVITY_ACCEL;

		// TFE - reset constants that may have been previously overridden
		s_lowFloorDamage            = PLAYER_DMG_FLOOR_LOW;
		s_highFloorDamage           = PLAYER_DMG_FLOOR_HIGH;
		s_gasDamage                 = PLAYER_DMG_FLOOR_LOW;
		s_wallDamage                = PLAYER_DMG_WALL;
		s_headlampConsumption       = HEADLAMP_ENERGY_CONSUMPTION;
		s_gogglesConsumption        = GOGGLES_ENERGY_CONSUMPTION;
		s_gasmaskConsumption        = GASMASK_ENERGY_CONSUMPTION;
		s_shieldSuperchargeDuration = SUPERCHARGE_DURATION;
		s_weaponSuperchargeDuration = SUPERCHARGE_DURATION;
		resetProjectileGravityAccel();

		// Initialize values.
		s_postLandVel = 0;
		s_playerRun   = 0;
		s_jumpScale   = 0;
		s_playerSlow  = 0;
		s_onMovingSurface = 0;
		s_reviveTick = 0;
		s_playerCrouch = 0;

		s_playerVelX   = 0;
		s_playerUpVel  = 0;
		s_playerUpVel2 = 0;
		s_playerVelZ   = 0;
		s_externalVelX = 0;
		s_externalVelZ = 0;
		s_playerCrouchSpd = 0;
		s_playerSpeedAve = 0;

		s_weaponLight    = 0;
		s_levelAtten     = 0;
		s_prevPlayerTick = 0;
		s_headwaveVerticalOffset = 0;

		s_playerUse         = JFALSE;
		s_playerActionUse   = JFALSE;
		s_weaponFiring      = JFALSE;
		s_weaponFiringSec   = JFALSE;
		s_playerPrimaryFire = JFALSE;
		s_playerSecFire     = JFALSE;
		s_playerJumping     = JFALSE;
		s_disablePlayerMovement = JFALSE;
		s_disablePlayerRotation = JFALSE;
		s_disablePlayerFire     = JFALSE;

		s_externalCameraMode = JFALSE;

		s_crushSoundId = 0;
		s_kyleScreamSoundId = 0;

		s_curSafe = nullptr;
		// The player will always start a level with at least 3 lives.
		s_lifeCount = max(3, s_lifeCount);

		s_playerInvSaved    = nullptr;
		s_invincibilityTask = nullptr;
		s_gasmaskTask       = nullptr;
		s_gasSectorTask     = nullptr;
		s_nextShieldDmgTick = 0;
		s_invincibility = 0;
		s_flyMode = JFALSE;
		s_noclip = JFALSE;
		s_oneHitKillEnabled = JFALSE;
		s_instaDeathEnabled = JFALSE;

		// The player will always start a level with at least 100 shields, though if they have more it carries over.
		s_playerInfo.shields = min(max(100, s_playerInfo.shields), s_shieldsMax);
		// The player starts a new level with full health and energy.
		s_playerInfo.health = s_healthMax;
		s_playerInfo.healthFract = 0;
		s_batteryPower = s_batteryPowerMax;

		s_wearingGasmask    = JFALSE;
		s_wearingCleats     = JFALSE;
		s_nightVisionActive = JFALSE;
		s_headlampActive    = JFALSE;

		// Handle level-specific hacks.
		const char* levelName = agent_getLevelName();
		TFE_System::logWrite(LOG_MSG, "Player", "Setting up level '%s'", levelName);

		// Handle custom level player overrides
		ModSettingLevelOverride* modLevelOverride = TFE_Settings::getLevelOverrides(levelName);
		if (modLevelOverride && !modLevelOverride->levName.empty())
		{
			player_handleLevelOverrides(*modLevelOverride);
		}
		else if (!strcasecmp(levelName, "jabship"))
		{
			u8* src  = (u8*)&s_playerInfo;
			size_t size = (size_t)&s_playerInfo.pileSaveMarker - (size_t)&s_playerInfo;
			assert(size == 140);
			s_playerInvSaved = nullptr;	// This should already be null, but...
			if (!s_playerInvSaved)
			{
				u8* dst = (u8*)level_alloc(size);
				if (!dst)
				{
					TFE_System::logWrite(LOG_ERROR, "Player", "Cannot allocate player inventory space - %u bytes.", size);
					dst = (u8*)level_alloc(140);
				}

				s_playerInvSaved = (u32*)dst;
				memcpy(dst, src, size);
				memset(src, 0, size);

				TFE_System::logWrite(LOG_MSG, "Player", "Player Inventory copied, size = %u", size);

				player_setNextWeapon(0);
				disableMask();
				disableCleats();
				disableNightVision();
				hud_clearMessage();
			}
		}
		else if (!strcasecmp(levelName, "gromas"))
		{
			// Adjust the lighting on gromas in order to accenuate the fog effect.
			s_levelAtten = 30;
		}
	}

	void player_clearEyeObject()
	{
		s_playerEye = nullptr;
	}

	void playerLogicCleanupFunc(Logic* logic)
	{
		// TODO(Core Game Loop Release)
	}

	void player_getVelocity(vec3_fixed* vel)
	{
		vel->x = s_playerVelX;
		vel->y = s_playerUpVel;
		vel->z = s_playerVelZ;
	}

	fixed16_16 player_getSquaredDistance(SecObject* obj)
	{
		fixed16_16 dx = obj->posWS.x - s_playerObject->posWS.x;
		fixed16_16 dy = obj->posWS.y - s_playerObject->posWS.y;
		fixed16_16 dz = obj->posWS.z - s_playerObject->posWS.z;

		// distSq overflows if dist > 181 units.
		return mul16(dx, dx) + mul16(dy, dy) + mul16(dz, dz);
	}

	void player_setupObject(SecObject* obj)
	{
		s_playerObject = obj;
		obj_addLogic(obj, (Logic*)&s_playerLogic, LOGIC_PLAYER, s_playerTask, playerLogicCleanupFunc);

		// Wipe out the player logic for replay consistency
		s_playerLogic.move = {};
		s_playerLogic.dir = {};

		s_playerObject->entityFlags|= ETFLAG_PLAYER;
		s_playerObject->flags      |= OBJ_FLAG_MOVABLE;
		s_playerObject->worldHeight = PLAYER_HEIGHT;
		s_playerObject->worldWidth  = PLAYER_WIDTH;

		s_playerYaw   = s_playerObject->yaw;
		s_playerPitch = 0;
		s_playerRoll  = 0;

		s_playerYPos = s_playerObject->posWS.y;
		s_playerLogic.stepHeight = PLAYER_STEP;

		task_makeActive(s_playerTask);

		weapon_setNext(s_playerInfo.curWeapon);
		s_playerInfo.maxWeapon = max(s_playerInfo.curWeapon, s_playerInfo.maxWeapon);

		s_playerSecMoved = JFALSE;
		s_playerSector = obj->sector;
	}

	void player_setupEyeObject(SecObject* obj)
	{
		if (s_playerEye)
		{
			s_playerEye->flags &= ~OBJ_FLAG_EYE;
			s_playerEye->flags |= s_playerEyeFlags;
		}
		s_playerEye = obj;
		player_setupCamera();

		s_playerEye->flags |= OBJ_FLAG_EYE;
		s_playerEyeFlags = s_playerEye->flags & OBJ_FLAG_NEEDS_TRANSFORM;
		s_playerEye->flags &= ~OBJ_FLAG_NEEDS_TRANSFORM;

		s_eyePos.x = s_playerEye->posWS.x;
		s_eyePos.y = s_playerEye->posWS.y;
		s_eyePos.z = s_playerEye->posWS.z;

		s_eyePitch = s_playerEye->pitch;
		s_eyeYaw   = s_playerEye->yaw;
		s_eyeRoll  = s_playerEye->roll;

		setCameraOffset(0, 0, 0);
		setCameraAngleOffset(0, 0, 0);
	}

	void player_revive()
	{
		// player_revive() is called when the player respawns, which is why it sets 100 for shields here.
		// In the case of picking up the item, the value is then set to 200 after the function call.
		s_playerInfo.shields = min(100, s_shieldsMax);
		s_playerInfo.health = s_healthMax;
		s_playerInfo.healthFract = 0;
		s_playerDying = 0;
	}

	s32 player_getLifeCount()
	{
		return s_lifeCount;
	}
					
	void setCameraOffset(fixed16_16 offsetX, fixed16_16 offsetY, fixed16_16 offsetZ)
	{
		s_camOffset = { offsetX, offsetY, offsetZ };
	}

	void setCameraAngleOffset(angle14_32 offsetPitch, angle14_32 offsetYaw, angle14_32 offsetRoll)
	{
		s_camOffsetPitch = offsetPitch;
		s_camOffsetYaw = offsetYaw;
		s_camOffsetRoll = offsetRoll;
	}

	void giveAllWeaponsAndHealth()
	{
		s_playerInfo.health = s_healthMax;
		s_playerInfo.healthFract = 0;
		s_playerDying = 0;
		s_playerInfo.shields = s_shieldsMax;

		s_playerInfo.itemPistol = JTRUE;
		s_playerInfo.itemRifle = JTRUE;
		// s_282428 = JTRUE;
		s_playerInfo.itemAutogun = JTRUE;
		s_playerInfo.itemMortar = JTRUE;
		s_playerInfo.itemFusion = JTRUE;
		// s_28242c = JTRUE;
		s_playerInfo.itemConcussion = JTRUE;
		s_playerInfo.itemCannon = JTRUE;
		s_playerInfo.itemGoggles = JTRUE;
		s_playerInfo.itemCleats = JTRUE;
		s_playerInfo.itemMask = JTRUE;

		s_playerInfo.ammoEnergy = s_ammoEnergyMax;
		s_playerInfo.ammoPower = s_ammoPowerMax;
		s_playerInfo.ammoDetonator = s_ammoDetonatorMax;
		s_playerInfo.ammoShell = s_ammoShellMax;
		s_playerInfo.ammoPlasma = s_ammoPlasmaMax;
		s_playerInfo.ammoMine = s_ammoMineMax;
		s_playerInfo.ammoMissile = s_ammoMissileMax;

		s_batteryPower = s_batteryPowerMax;
		weapon_emptyAnim();
	}

	void giveHealthAndFullAmmo()
	{
		s_playerInfo.health = s_healthMax;
		s_playerInfo.healthFract = 0;
		s_playerDying = 0;
		s_playerInfo.shields = s_shieldsMax;
		s_playerInfo.ammoEnergy = s_ammoEnergyMax;

		if (s_playerInfo.itemAutogun || s_playerInfo.itemFusion || s_playerInfo.itemConcussion)
		{
			s_playerInfo.ammoPower = s_ammoPowerMax;
		}
		if (s_playerInfo.itemCannon)
		{
			s_playerInfo.ammoPlasma = s_ammoPlasmaMax;
		}
		s_playerInfo.ammoDetonator = s_ammoDetonatorMax;
		if (s_playerInfo.itemMortar)
		{
			s_playerInfo.ammoShell = s_ammoShellMax;
		}
		s_playerInfo.ammoMine = s_ammoMineMax;
		if (s_playerInfo.itemCannon)
		{
			s_playerInfo.ammoMissile = s_ammoMissileMax;
		}
		s_batteryPower = s_batteryPowerMax;
		weapon_emptyAnim();
	}

	void giveAllInventoryAndHealth()
	{
		s_playerInfo.health = s_healthMax;
		s_playerInfo.healthFract = 0;
		s_playerDying = 0;
		s_playerInfo.shields = s_shieldsMax;
		s_playerInfo.itemPistol = JTRUE;
		s_playerInfo.itemRifle = JTRUE;
		// s_282428 = JTRUE;
		s_playerInfo.itemAutogun = JTRUE;
		s_playerInfo.itemMortar = JTRUE;
		s_playerInfo.itemFusion = JTRUE;
		// s_28242c = JTRUE;
		s_playerInfo.itemConcussion = JTRUE;
		s_playerInfo.itemCannon = JTRUE;
		s_playerInfo.itemRedKey = JTRUE;
		s_playerInfo.itemYellowKey = JTRUE;
		s_playerInfo.itemBlueKey = JTRUE;
		s_playerInfo.itemGoggles = JTRUE;
		s_playerInfo.itemCleats = JTRUE;
		s_playerInfo.itemMask = JTRUE;
		s_playerInfo.itemPlans = JTRUE;
		s_playerInfo.itemPhrik = JTRUE;
		s_playerInfo.itemNava = JTRUE;
		s_playerInfo.itemDatatape = JTRUE;
		s_playerInfo.itemUnused = JTRUE;
		s_playerInfo.itemDtWeapon = JTRUE;
		s_playerInfo.itemCode1 = JTRUE;
		s_playerInfo.itemCode2 = JTRUE;
		s_playerInfo.itemCode3 = JTRUE;
		s_playerInfo.itemCode4 = JTRUE;
		s_playerInfo.itemCode5 = JTRUE;
		s_playerInfo.itemCode6 = JTRUE;
		s_playerInfo.itemCode7 = JTRUE;
		s_playerInfo.itemCode8 = JTRUE;
		s_playerInfo.itemCode9 = JTRUE;
		s_playerInfo.ammoEnergy = s_ammoEnergyMax;
		s_playerInfo.ammoPower = s_ammoPowerMax;
		s_playerInfo.ammoDetonator = s_ammoDetonatorMax;
		s_playerInfo.ammoShell = s_ammoShellMax;
		s_playerInfo.ammoMissile = s_ammoMissileMax;
		s_playerInfo.ammoPlasma = s_ammoPlasmaMax;
		s_playerInfo.ammoMine = s_ammoMineMax;
		s_batteryPower = s_batteryPowerMax;

		weapon_emptyAnim();
	}

	void giveKeys()
	{
		s_playerInfo.itemRedKey = JTRUE;
		s_playerInfo.itemYellowKey = JTRUE;
		s_playerInfo.itemBlueKey = JTRUE;
		s_playerInfo.itemGoggles = JTRUE;
		s_playerInfo.itemCleats = JTRUE;
		s_playerInfo.itemMask = JTRUE;
		s_playerInfo.itemPlans = JTRUE;
		s_playerInfo.itemPhrik = JTRUE;
		s_playerInfo.itemNava = JTRUE;
		s_playerInfo.itemDatatape = JTRUE;
		s_playerInfo.itemUnused = JTRUE;
		s_playerInfo.itemDtWeapon = JTRUE;
		s_playerInfo.itemCode1 = JTRUE;
		s_playerInfo.itemCode2 = JTRUE;
		s_playerInfo.itemCode3 = JTRUE;
		s_playerInfo.itemCode4 = JTRUE;
		s_playerInfo.itemCode5 = JTRUE;
		s_playerInfo.itemCode6 = JTRUE;
		s_playerInfo.itemCode7 = JTRUE;
		s_playerInfo.itemCode8 = JTRUE;
		s_playerInfo.itemCode9 = JTRUE;
	}

	void cheat_teleport()
	{
		RSector* sector = sector_which3D_Map(s_mapX0, s_mapZ0, s_mapLayer);
		if (sector)
		{
			s_playerEye->posWS.x = s_mapX0;
			s_playerEye->posWS.z = s_mapZ0;
			s_playerEye->posWS.y = sector->floorHeight;
			s_playerObject->posWS = s_playerEye->posWS;
			s_playerPos = s_playerEye->posWS;
			s_playerYPos = sector->floorHeight;
			
			// Reset gravity.
			s_playerUpVel2 = 0;
			s_playerUpVel  = 0;

			sector_addObject(sector, s_playerEye);
			s_playerSector = sector;

			player_setupEyeObject(s_playerEye);
			player_setupCamera();
		}
	}

	void cheat_toggleHeightCheck()
	{
		s_limitStepHeight = ~s_limitStepHeight;
		hud_sendTextMessage(700);
	}

	void cheat_bugMode()
	{
		s_smallModeEnabled = ~s_smallModeEnabled;
		hud_sendTextMessage(705);
	}

	void cheat_pauseAI()
	{
		s_aiActive = ~s_aiActive;
		hud_sendTextMessage(706);
	}

	void cheat_postal()
	{
		giveAllWeaponsAndHealth();
		hud_sendTextMessage(703);
	}

	void cheat_fullAmmo()
	{
		giveHealthAndFullAmmo();
		hud_sendTextMessage(708);
	}

	void cheat_unlock()
	{
		giveKeys();
		hud_sendTextMessage(709);
	}

	void cheat_maxout()
	{
		giveAllInventoryAndHealth();
		hud_sendTextMessage(710);
	}
		
	void cheat_godMode()
	{
		if (!s_invincibility)
		{
			s_invincibility = -2;
		}
		else
		{
			s_invincibility = 0;
		}
		hud_sendTextMessage(702);
	}

	// New TFE Cheats.
	void cheat_fly()
	{
		// Write the cheat message.
		const char* msg = TFE_System::getMessage(TFE_MSG_FLYMODE);
		if (msg) { hud_sendTextMessage(msg, 1); }

		s_flyMode = ~s_flyMode;
	}

	void cheat_noclip()
	{
		// Write the cheat message.
		const char* msg = TFE_System::getMessage(TFE_MSG_NOCLIP);
		if (msg) { hud_sendTextMessage(msg, 1); }

		s_noclip = ~s_noclip;
	}

	void cheat_tester()
	{
		// Write the cheat message.
		const char* msg = TFE_System::getMessage(TFE_MSG_TESTER);
		if (msg) { hud_sendTextMessage(msg, 1); }

		if (s_noclip)
		{
			if (s_invincibility == -2) { s_invincibility = 0; }
			s_aiActive = JTRUE;
			s_noclip   = JFALSE;
			s_flyMode  = JFALSE;
		}
		else
		{
			s_invincibility = -2;
			s_aiActive = JFALSE;
			s_noclip   = JTRUE;
			s_flyMode  = JTRUE;
		}
	}

	void cheat_addLife()
	{
		if (s_lifeCount < 9)
		{
			s_lifeCount++;
			const char* msg = TFE_System::getMessage(TFE_MSG_ADDLIFE);
			if (msg) { hud_sendTextMessage(msg, 1); }
		}
		else
		{
			const char* msg = TFE_System::getMessage(TFE_MSG_ADDLIFEFAIL);
			if (msg) { hud_sendTextMessage(msg, 1); }
		}
	}
	
	void cheat_subLife()
	{
		if (s_lifeCount > 0)
		{
			s_lifeCount--;
			const char* msg = TFE_System::getMessage(TFE_MSG_SUBLIFE);
			if (msg) { hud_sendTextMessage(msg, 1); }
		}
	}
	
	void cheat_maxLives()
	{
		s_lifeCount = 9;
		const char* msg = TFE_System::getMessage(TFE_MSG_CAT);
		if (msg) { hud_sendTextMessage(msg, 1); }
	}

	void cheat_die()
	{
		if (!s_playerDying)
		{
			player_applyDamage(FIXED(999), FIXED(999), JFALSE);
			const char* msg = TFE_System::getMessage(TFE_MSG_DIE);
			if (msg) { hud_sendTextMessage(msg, 1); }
		}
	}
	
	void cheat_oneHitKill()
	{
		s_oneHitKillEnabled = ~s_oneHitKillEnabled;
		const char* msg = TFE_System::getMessage(TFE_MSG_ONEHITKILL);
		if (msg) { hud_sendTextMessage(msg, 1); }
	}
	
	void cheat_instaDeath()
	{
		// This way if we already had one-hit-kill enabled, we don't end
		// up with one-hit kill off and insta-death on - though if the
		// player wants that to happen, they can type LA_IMDEATH again
		// after using this cheat.
		s_instaDeathEnabled = ~s_instaDeathEnabled;
		const char* msg = TFE_System::getMessage(TFE_MSG_HARDCORE);
		if (msg) { hud_sendTextMessage(msg, 1); }
	}

	void player_setupCamera()
	{
		if (s_playerEye)
		{
			s_eyePos.x = s_playerEye->posWS.x + s_camOffset.x;
			s_eyePos.y = s_playerEye->posWS.y + s_camOffset.y - s_playerEye->worldHeight;
			s_eyePos.z = s_playerEye->posWS.z + s_camOffset.z;

			s_eyePitch = s_playerEye->pitch + s_camOffsetPitch;
			s_eyeYaw   = s_playerEye->yaw   + s_camOffsetYaw;
			s_eyeRoll  = s_playerEye->roll  + s_camOffsetRoll;

			if (s_playerEye->sector)
			{
				renderer_computeCameraTransform(s_playerEye->sector, s_eyePitch, s_eyeYaw, s_eyePos.x, s_eyePos.y, s_eyePos.z);
			}
			renderer_setWorldAmbient(s_externalCameraMode ? 0 : s_playerLight);		// don't apply headlamp attentuation when camera is away from player
		}
	}

	void computeDamagePushVelocity(ProjectileLogic* proj, vec3_fixed* vel)
	{
		fixed16_16 push = mul16(proj->projForce, proj->speed);
		vel->x = mul16(push, proj->dir.x);
		vel->z = mul16(push, proj->dir.z);

		if (proj->dir.y < 0)
		{
			vel->y = mul16(push, proj->dir.y) * 2;
		}
		else
		{
			vel->y = 0;
		}
	}

	void computeExplosionPushDir(vec3_fixed* pos, vec3_fixed* pushDir)
	{
		vec3_fixed delta = { pos->x - s_explodePos.x, pos->y - s_explodePos.y, pos->z - s_explodePos.z };

		fixed16_16 dirZ, dirX;
		fixed16_16 len = computeDirAndLength(delta.x, delta.z, &dirX, &dirZ);

		fixed16_16 dirY;
		computeDirAndLength(len, delta.y, &dirY, &pushDir->y);
		pushDir->x = mul16(dirY, dirX);
		pushDir->z = mul16(dirY, dirZ);
	}

	void playerControlMsgFunc(MessageType msg)
	{
		fixed16_16 shieldDmg = 0;
		JBool playHitSound = JFALSE;

		if (msg == MSG_DAMAGE)
		{
			ProjectileLogic* proj = (ProjectileLogic*)s_msgEntity;
			vec3_fixed pushVel;
			
			// TFE: Don't push the player if movement disabled
			if (!s_disablePlayerMovement)
			{
				computeDamagePushVelocity(proj, &pushVel);

				s_playerVelX += pushVel.x;
				s_playerUpVel2 += pushVel.y;
				s_playerVelZ += pushVel.z;
			}

			if (s_invincibility || s_config.superShield)
			{
				// TODO

				// Return because no damage is applied.
				return;
			}
			else
			{
				playHitSound = JTRUE;
				shieldDmg = proj->dmg;
			}
		}
		else if (msg == MSG_EXPLOSION)
		{
			vec3_fixed pos = { s_playerObject->posWS.x, s_playerObject->posWS.y - s_playerObject->worldHeight, s_playerObject->posWS.z };
			vec3_fixed pushDir;
			computeExplosionPushDir(&pos, &pushDir);

			// TFE: Don't push the player if movement disabled
			if (!s_disablePlayerMovement)
			{
				fixed16_16 force = s_msgArg2;
				s_playerVelX += mul16(force, pushDir.x);
				s_playerUpVel2 += mul16(force, pushDir.y);
				s_playerVelZ += mul16(force, pushDir.z);
			}

			if (s_invincibility || s_config.superShield)
			{
				// Return because no damage is applied.
				return;
			}

			playHitSound = JTRUE;
			if (s_curEffectData->type == HEFFECT_THERMDET_EXP)
			{
				shieldDmg = s_msgArg1 >> 1;
			}
			else
			{
				shieldDmg = s_msgArg1;
			}
		}

		if (s_instaDeathEnabled) // "Hardcore" cheat
		{
			shieldDmg = FIXED(999);
		}

		player_applyDamage(0, shieldDmg, playHitSound);
	}

	void player_reset()
	{
		s_externalYawSpd = 0;
		s_playerPitch = 0;
		s_playerRoll = 0;
		s_forwardSpd = 0;
		s_strafeSpd = 0;
		s_maxMoveDist = FIXED(2);
		// s_282344 = 34;
		s_minEyeDistFromFloor = FIXED(2);
		s_postLandVel = 0;
		s_landUpVel = 0;
		s_playerVelX = 0;
		s_playerUpVel = 0;
		s_playerUpVel2 = 0;
		s_playerVelZ = 0;
		s_externalVelX = 0;
		// s_282380 = 0;
		s_externalVelZ = 0;
		s_playerSpeedAve = 0;
		s_weaponLight = 0;
		s_headwaveVerticalOffset = 0;
		s_playerUse = JFALSE;
		s_playerActionUse = JFALSE;
		s_onFloor = JFALSE;

		s_playerStopAccel = -540672;	// -8.25
		s_playerCrouchSpd = 0;
		s_prevDistFromFloor = 0;
		s_playerObject->worldHeight = 0x5cccc;	// 5.8
		s_disablePlayerMovement = JFALSE;
		s_disablePlayerRotation = JFALSE;
		s_disablePlayerFire = JFALSE;

		s_externalCameraMode = JFALSE;
		player_setupEyeObject(s_playerObject);
	}

	void player_changeSector(RSector* newSector)
	{
		if (!newSector) { return; }

		RSector* prevSector = s_playerObject->sector;
		if (newSector != prevSector)
		{
			if (newSector->layer != prevSector->layer)
			{
				automap_setLayer(newSector->layer);
			}
			sector_addObject(newSector, s_playerObject);
		}
	}

	void player_clearSuperCharge()
	{
		if (s_superchargeTask)
		{
			task_free(s_superchargeTask);
		}
		s_superCharge = JFALSE;
		s_superChargeHud = JFALSE;
		s_superchargeTask = nullptr;
		weapon_setFireRate();
	}

	void handlePlayerDying()
	{
		s_minEyeDistFromFloor = ONE_16;
		s_playerCrouchSpd = 0x5b332;	// 5.7
		s_playerPitch += mul16(0x6aa, s_deltaTime);
		s_playerRoll  -= mul16(0xd55, s_deltaTime);

		s_playerPitch = clamp(s_playerPitch, -1023, 1023);
		s_playerRoll  = clamp(s_playerRoll, -1592, 1592);

		s_playerSecFire = JFALSE;
		s_playerPrimaryFire = JFALSE;

		if (inputMapping_getActionState(IADF_JUMP) || inputMapping_getActionState(IADF_MENU_TOGGLE) || inputMapping_getActionState(IADF_USE))
		{
			inputMapping_removeState(IADF_JUMP);
			inputMapping_removeState(IADF_MENU_TOGGLE);
			inputMapping_removeState(IADF_USE);

			if (s_lifeCount != 0 && s_curSafe)
			{
				s_lifeCount -= 1;
				player_revive();
				player_reset();
				s_headlampActive = JFALSE;
				s_nightVisionActive = JFALSE;
				disableNightVisionInternal();

				s_playerObject->yaw = s_curSafe->yaw;
				s_playerYaw = s_curSafe->yaw;
				s_playerObject->posWS.x = s_curSafe->x;
				s_playerObject->posWS.z = s_curSafe->z;

				RSector* sector = s_curSafe->sector;
				fixed16_16 floorHeight = sector->floorHeight + sector->secHeight;
				s_playerObject->posWS.y = floorHeight;
				s_playerYPos = s_playerObject->posWS.y;
				player_changeSector(sector);

				s_nextShieldDmgTick = s_curTick + 436;
				if (s_invincibilityTask)
				{
					task_free(s_invincibilityTask);
				}
				s_invincibilityTask = nullptr;
				s_invincibility = 0;
				s_flyMode = JFALSE;
				s_noclip = JFALSE;
				player_clearSuperCharge();
			}
			else
			{
				player_revive();
				player_reset();
				s_levelComplete = JFALSE;
				mission_exitLevel();
			}
		}
	}

	void playerControlTaskFunc(MessageType msg)
	{
		task_begin;
		if (!s_curSafe)
		{
			s_curSafe = level_getSafeFromSector(s_playerObject->sector);
		}

		s_playerPos.x = s_playerObject->posWS.x;
		s_playerPos.y = s_playerObject->posWS.y;
		s_playerPos.z = s_playerObject->posWS.z;
		s_playerObjHeight = s_playerObject->worldHeight;
		s_playerObjPitch  = s_playerObject->pitch;
		s_playerObjYaw    = s_playerObject->yaw;
		s_playerObjSector = s_playerObject->sector;
		s_playerTick = s_curTick;
		s_prevPlayerTick  = s_curTick;

		while (msg != MSG_FREE_TASK)
		{
			if (msg == MSG_RUN_TASK)
			{
				if (!s_gamePaused)
				{
					// TFE: Add pitch limit setting.
					// Note that if running with the software renderer, it is always forced to the vanilla limit.
					if (TFE_Jedi::getSubRenderer() == TFE_SubRenderer::TSR_CLASSIC_GPU)
					{
						TFE_Settings_Game* gameSettings = TFE_Settings::getGameSettings();
						player_setPitchLimit(gameSettings->df_pitchLimit);
					}
					else
					{
						player_setPitchLimit(PITCH_VANILLA);
					}

					handlePlayerMoveControls();
					handlePlayerPhysics();
					handlePlayerActions();
					handlePlayerScreenFx();
					if (s_playerDying)
					{
						handlePlayerDying();
					}
				}
			}

			s_prevPlayerTick = s_playerTick;
			task_yield(TASK_NO_DELAY);
		}
		task_end;
	}
	
	///////////////////////////////////////////
	// Internal Implentation
	///////////////////////////////////////////
	void setPlayerLight(s32 atten)
	{
		s_playerLight = atten;
	}

	void handlePlayerMoveControls()
	{
		TFE_Settings_Game* settings = TFE_Settings::getGameSettings();

		s_externalYawSpd = 0;
		s_forwardSpd = 0;
		s_strafeSpd = 0;
		s_playerRun = 0;
		s_jumpScale = 0;
		s_playerSlow = 0;

		if (s_playerDying)
		{
			return;
		}

		s_playerCrouchSpd = s_playerStopAccel;

		s32 mdx, mdy;
		TFE_Input::getAccumulatedMouseMove(&mdx, &mdy);
		InputConfig* inputConfig = TFE_Input::inputMapping_get();

		// Yaw change
		if ((inputConfig->mouseMode == MMODE_TURN || inputConfig->mouseMode == MMODE_LOOK) && !s_disablePlayerRotation)
		{
			s_playerYaw += s32(f32(mdx * PLAYER_MOUSE_TURN_SPD) * inputMapping_getHorzMouseSensitivity());
			s_playerYaw &= ANGLE_MASK;
		}
		// Pitch change
		if (inputConfig->mouseMode == MMODE_LOOK)
		{
			f32 pitchDelta = f32(mdy * PLAYER_MOUSE_TURN_SPD) * inputMapping_getVertMouseSensitivity();
			// Counteract the tan() call later in the delta in order to make the movement perceptually linear.
			pitchDelta = atanf(pitchDelta/2047.0f * PI) / PI * 2047.0f;
			s_playerPitch = clamp(s_playerPitch - s32(pitchDelta), -PITCH_LIMIT, PITCH_LIMIT);
		}

		// Controls
		if (s_automapLocked && !s_disablePlayerMovement)
		{
			if (inputMapping_getActionState(IADF_FORWARD))
			{
				fixed16_16 speed = mul16(PLAYER_FORWARD_SPEED, s_deltaTime);
				s_forwardSpd = max(speed, s_forwardSpd);
			}
			else if (inputMapping_getActionState(IADF_BACKWARD))
			{
				fixed16_16 speed = -mul16(PLAYER_FORWARD_SPEED, s_deltaTime);
				s_forwardSpd = min(speed, s_forwardSpd);
			}
			else if (inputMapping_getAnalogAxis(AA_MOVE))
			{
				fixed16_16 speed = mul16(mul16(PLAYER_FORWARD_SPEED, s_deltaTime), floatToFixed16(clamp(inputMapping_getAnalogAxis(AA_MOVE), -1.0f, 1.0f)));
				if (speed < 0)
				{
					s_forwardSpd = min(speed, s_forwardSpd);
				}
				else
				{
					s_forwardSpd = max(speed, s_forwardSpd);
				}
			}
		}

		if (settings->df_autorun) // TFE: Optional feature.
		{
			s_playerRun = 1;
			if (inputMapping_getActionState(IADF_RUN))
			{
				s_playerRun = 0;
			}
			else if (inputMapping_getActionState(IADF_SLOW))
			{
				s_playerSlow |= 1;
				s_playerRun = 0;
			}
		}
		else // Normal vanilla behavior.
		{
			if (inputMapping_getActionState(IADF_RUN))
			{
				s_playerRun |= 1;
			}
			else if (inputMapping_getActionState(IADF_SLOW))
			{
				s_playerSlow |= 1;
			}
		}

		if (settings->df_crouchToggle)	// TFE: Optional feature.
		{
			if (inputMapping_getActionState(IADF_CROUCH) == STATE_PRESSED)
			{
				s_playerCrouch = !s_playerCrouch;
			}
		}
		else
		{
			s_playerCrouch = inputMapping_getActionState(IADF_CROUCH) ? 1 : 0;
		}
		if (s_flyMode)
		{
			if ((!s_onFloor || s_noclip) && s_playerCrouch)
			{
				fixed16_16 speed = -(PLAYER_JUMP_IMPULSE << s_jumpScale);
				s_playerUpVel = speed;
				s_playerUpVel2 = speed;
			}
			else if (s_playerUpVel > 0)
			{
				s_playerUpVel = 0;
				s_playerUpVel2 = 0;
			}
			s_playerCrouchSpd = 0;
		}

		if (s_onFloor & s_playerCrouch)
		{
			fixed16_16 speed = PLAYER_CROUCH_SPEED;
			speed <<= s_playerRun;			// this will be '1' if running.
			speed >>= s_playerSlow;			// this will be '1' if moving slowly.
			s_playerCrouchSpd = speed;
		}

		JBool wasJumping = s_playerJumping;
		s_playerJumping = JFALSE;
		if (inputMapping_getActionState(IADF_JUMP) && !s_disablePlayerMovement)
		{
			if (!s_onFloor || wasJumping)
			{
				s_playerJumping = wasJumping;
				if (s_flyMode)
				{
					fixed16_16 speed = PLAYER_JUMP_IMPULSE << s_jumpScale;
					s_playerUpVel = speed;
					s_playerUpVel2 = speed;
				}
			}
			else
			{
				sound_play(s_jumpSoundSource);

				fixed16_16 speed = PLAYER_JUMP_IMPULSE << s_jumpScale;
				s_playerJumping = JTRUE;
				s_playerUpVel  = speed;
				s_playerUpVel2 = speed;
			}
		}
		else if (s_flyMode && s_playerUpVel < 0)
		{
			s_playerUpVel = 0;
			s_playerUpVel2 = 0;
		}

		// Pitch and Roll controls.
		if (s_automapLocked)
		{
			if (inputMapping_getActionState(IADF_TURN_LT) && !s_disablePlayerRotation)
			{
				fixed16_16 turnSpeed = PLAYER_KB_TURN_SPD;	// angle units per second.
				fixed16_16 dYaw = mul16(turnSpeed, s_deltaTime);
				dYaw <<= s_playerRun;		// double for "run"
				dYaw >>= s_playerSlow;		// half for "slow"

				s_playerYaw -= dYaw;
				s_playerYaw &= ANGLE_MASK;
			}
			else if (inputMapping_getActionState(IADF_TURN_RT) && !s_disablePlayerRotation)
			{
				fixed16_16 turnSpeed = PLAYER_KB_TURN_SPD;	// angle units per second.
				fixed16_16 dYaw = mul16(turnSpeed, s_deltaTime);
				dYaw <<= s_playerRun;		// double for "run"
				dYaw >>= s_playerSlow;		// half for "slow"

				s_playerYaw += dYaw;
				s_playerYaw &= ANGLE_MASK;
			}
			else if (inputMapping_getAnalogAxis(AA_LOOK_HORZ) && !s_disablePlayerRotation)
			{
				fixed16_16 turnSpeed = mul16(mul16(PLAYER_CONTROLLER_TURN_SPD, s_deltaTime), floatToFixed16(inputMapping_getAnalogAxis(AA_LOOK_HORZ)));
				s_playerYaw += turnSpeed;
				s_playerYaw &= ANGLE_MASK;
			}

			if (inputMapping_getActionState(IADF_LOOK_UP))
			{
				fixed16_16 turnSpeed = PLAYER_KB_TURN_SPD;	// angle units per second.
				fixed16_16 dPitch = mul16(turnSpeed, s_deltaTime);
				dPitch <<= s_playerRun;		// double for "run"
				dPitch >>= s_playerSlow;	// half for "slow"
				s_playerPitch = clamp(s_playerPitch + dPitch, -PITCH_LIMIT, PITCH_LIMIT);
			}
			else if (inputMapping_getActionState(IADF_LOOK_DN))
			{
				fixed16_16 turnSpeed = PLAYER_KB_TURN_SPD;	// angle units per second.
				fixed16_16 dPitch = mul16(turnSpeed, s_deltaTime);
				dPitch <<= s_playerRun;		// double for "run"
				dPitch >>= s_playerSlow;	// half for "slow"
				s_playerPitch = clamp(s_playerPitch - dPitch, -PITCH_LIMIT, PITCH_LIMIT);
			}
			else if (inputMapping_getAnalogAxis(AA_LOOK_VERT))
			{
				fixed16_16 turnSpeed = mul16(mul16(PLAYER_CONTROLLER_PITCH_SPD, s_deltaTime), floatToFixed16(inputMapping_getAnalogAxis(AA_LOOK_VERT)));
				// Counteract the tan() call later in the delta in order to make the movement perceptually linear.
				turnSpeed = (fixed16_16)floatToFixed16(atanf((f32)fixed16ToFloat(turnSpeed) / 2047.0f * PI) / PI * 2047.0f);

				s_playerPitch = clamp(s_playerPitch + turnSpeed, -PITCH_LIMIT, PITCH_LIMIT);
			}

			if (inputMapping_getActionState(IADF_CENTER_VIEW))
			{
				s_playerPitch = 0;
				s_playerRoll = 0;
			}

			// Strafe movement
			if (inputMapping_getActionState(IADF_STRAFE_RT) && !s_disablePlayerMovement)
			{
				fixed16_16 speed = mul16(PLAYER_STRAFE_SPEED, s_deltaTime);
				s_strafeSpd = max(speed, s_strafeSpd);
			}
			else if (inputMapping_getActionState(IADF_STRAFE_LT) && !s_disablePlayerMovement)
			{
				fixed16_16 speed = -mul16(PLAYER_STRAFE_SPEED, s_deltaTime);
				s_strafeSpd = min(speed, s_strafeSpd);
			}
			else if (inputMapping_getAnalogAxis(AA_STRAFE) && !s_disablePlayerMovement)
			{
				fixed16_16 speed = mul16(mul16(PLAYER_STRAFE_SPEED, s_deltaTime), floatToFixed16(clamp(inputMapping_getAnalogAxis(AA_STRAFE), -1.0f, 1.0f)));
				if (speed < 0)
				{
					s_strafeSpd = min(speed, s_strafeSpd);
				}
				else
				{
					s_strafeSpd = max(speed, s_strafeSpd);
				}
			}
		}

		if (inputMapping_getActionState(IADF_USE))
		{
			s_playerUse = JTRUE;
		}

		if (inputMapping_getActionState(IADF_PRIMARY_FIRE) && !s_disablePlayerFire)
		{
			s_playerPrimaryFire = JTRUE;
		}
		else if (inputMapping_getActionState(IADF_SECONDARY_FIRE) && !s_disablePlayerFire)
		{
			s_playerSecFire = JTRUE;
		}

		// Reduce the players ability to adjust the velocity while they have vertical velocity.
		if (s_playerUpVel && !s_flyMode)
		{
			// TFE specific
			const s32 airControl = 8 - TFE_Settings::getGameSettings()->df_airControl;
			// In the original DOS code, airControl = 8.
			s_forwardSpd >>= airControl;
			s_strafeSpd  >>= airControl;
		}
	}

	fixed16_16 adjustForwardSpeed(fixed16_16 spd)
	{
		spd <<= s_playerRun;
		spd >>= s_playerSlow;
		spd >>= s_onMovingSurface;	// slows down the player movement on a moving surface.
		spd <<= s_jumpScale;
		return spd;
	}

	fixed16_16 adjustStrafeSpeed(fixed16_16 spd)
	{
		spd <<= s_playerRun;
		spd >>= s_playerSlow;
		spd >>= s_onMovingSurface;	// slows down the player movement on a moving surface.
		return spd;
	}
		
	void computeDirectionFromAngle(angle14_32 yaw, fixed16_16* xDir, fixed16_16* zDir)
	{
		sinCosFixed(yaw, xDir, zDir);
	}

	// Rescales the vector if it is longer than maxDist.
	void limitVectorLength(fixed16_16* dx, fixed16_16* dz, fixed16_16 maxDist)
	{
		fixed16_16 halfDx = TFE_Jedi::abs(*dx) >> 1;
		fixed16_16 halfDz = TFE_Jedi::abs(*dz) >> 1;
		fixed16_16 distSq = mul16(halfDx, halfDx) + mul16(halfDz, halfDz);

		fixed16_16 dist = fixedSqrt(distSq);
		s_dist = dist * 2;

		if (s_dist > maxDist)
		{
			s_distScale = div16(maxDist, s_dist);
			*dx = mul16(*dx, s_distScale);
			*dz = mul16(*dz, s_distScale);
		}
	}

	void computeMoveFromAngleAndSpeed(fixed16_16* outMoveX, fixed16_16* outMoveZ, angle14_32 yaw, fixed16_16 speed)
	{
		computeDirectionFromAngle(yaw, &s_moveDirX, &s_moveDirZ);
		*outMoveX += mul16(s_moveDirX, speed);
		*outMoveZ += mul16(s_moveDirZ, speed);
	}
				
	void handlePlayerPhysics()
	{
		SecObject* player = s_playerObject;
		RSector* origSector = player->sector;
		fixed16_16 yVel = s_playerUpVel;
		s_playerInWater = JFALSE;

		// Calculate the friction coefficient.
		fixed16_16 friction;
		if (yVel)
		{
			friction = FRICTION_FALLING;
		}
		else
		{
			JBool wearingCleats = s_wearingCleats;
			friction = FRICTION_DEFAULT;
			if ((s_playerSector->flags1 & SEC_FLAGS1_ICE_FLOOR) && !wearingCleats)	// On Ice without cleats.
			{
				s_playerRun = JFALSE;
				friction = FRICTION_ICE;
			}
			else if ((s_playerSector->flags1 & SEC_FLAGS1_SNOW_FLOOR) || wearingCleats)	// On snow or wearing cleats.
			{
				// Lower friction means the player will stop sliding sooner.
				friction = FRICTION_CLEATS;
			}
			else if (s_playerSector->secHeight - 1 >= 0 && !s_flyMode)	// In water
			{
				friction = FRICTION_DEFAULT;
				s_playerRun = wearingCleats;		// Once you get cleats, you move faster through water.
				s_playerInWater = JTRUE;
			}
		}
 
		// Apply friction to existing velocity.
		const bool useSmoothDt = TFE_Settings::getGraphicsSettings()->useSmoothDeltaTime;
		if (s_playerVelX || s_playerVelZ)
		{
			Tick dt = s_playerTick - s_prevPlayerTick;
			if (dt || !useSmoothDt)
			{
				// Exponential friction, this is the same as vel * friction^dt
				for (Tick i = 0; i < dt; i++)
				{
					s_playerVelX = mul16(friction, s_playerVelX);
					s_playerVelZ = mul16(friction, s_playerVelZ);
				}
				// Just stop moving if the velocity is low enough, to avoid jittering forever.
				// Note: The jitter threshold has been halved in TFE in order to work at higher framerates.
				const fixed16_16 threshold = useSmoothDt ? HALF_16 / 8 : HALF_16 / 2;
				if (distApprox(0, 0, s_playerVelX, s_playerVelZ) < threshold)
				{
					s_playerVelX = 0;
					s_playerVelZ = 0;
				}
			}
		}

		// Handle moving surfaces.
		JBool onMovingSurface = JFALSE;
		fixed16_16 floorHeight    = s_playerSector->floorHeight;
		fixed16_16 secHeight      = s_playerSector->secHeight;
		fixed16_16 secFloorHeight = floorHeight + secHeight;
		s_onMovingSurface = 0;
		if (player->posWS.y == floorHeight || player->posWS.y == secFloorHeight)
		{
			Allocator* links = s_playerSector->infLink;
			InfLink* link = (InfLink*)allocator_getHead(links);
			while (link)
			{
				if (link->type == LTYPE_SECTOR)
				{
					InfElevator* elev = link->elev;
					JBool moved = JFALSE;

					if (player->posWS.y == s_playerSector->floorHeight)
					{
						if (elev->flags & INF_EFLAG_MOVE_FLOOR) { moved = JTRUE; }
					}
					else if (elev->flags & INF_EFLAG_MOVE_SECHT)
					{
						moved = JTRUE;
					}

					if (moved)
					{
						fixed16_16 angularSpd;
						vec3_fixed vel;
						inf_getMovingElevatorVelocity(elev, &vel, &angularSpd);

						if (!angularSpd && secHeight > 0)
						{
							// liquid behavior - dampens velocity.
							vel.x = mul16(vel.x, MOVING_SURFACE_MUL);
							vel.z = mul16(vel.z, MOVING_SURFACE_MUL);
						}
						else if (angularSpd)
						{
							// This is never hit in Dark Forces because it appears that the angularSpd update in
							// inf_getMovingElevatorVelocity() was removed for some reason.
							assert(0);
						}

						if (vel.x || vel.z)
						{
							if (!onMovingSurface)
							{
								s_externalVelX = vel.x;
								s_externalVelZ = vel.z;
								onMovingSurface = JTRUE;
							}
							else
							{
								s_externalVelX += vel.x;
								// This is a bug in Dark Forces, and should have been += vel.z;
								// However mods rely on this bug, so we have to keep it.
								s_externalVelZ += angularSpd;
							}
							s_onMovingSurface = 1;
						}
					}
				}

				link = (InfLink*)allocator_getNext(links);
			}
		}

		// Handle player movement.
		if (s_forwardSpd || s_strafeSpd)
		{
			RSector* sector = player->sector;
			if (!s_wearingCleats && (s_playerSector->flags1 & SEC_FLAGS1_ICE_FLOOR))
			{
				s_forwardSpd = mul16(s_forwardSpd, ICE_SPEED_MUL);
				s_strafeSpd  = mul16(s_strafeSpd,  ICE_SPEED_MUL);
			}
			fixed16_16 speed = adjustForwardSpeed(s_forwardSpd);
			computeMoveFromAngleAndSpeed(&s_playerVelX, &s_playerVelZ, player->yaw, speed);

			// Add 90 degrees to get the strafe direction.
			speed = adjustStrafeSpeed(s_strafeSpd);
			computeMoveFromAngleAndSpeed(&s_playerVelX, &s_playerVelZ, player->yaw + 4095, speed);
			limitVectorLength(&s_playerVelX, &s_playerVelZ, s_maxMoveDist);
		}

		// Then convert from player velocity to per-frame movement.
		fixed16_16 moveX = adjustForwardSpeed(s_playerVelX);
		fixed16_16 moveZ = adjustForwardSpeed(s_playerVelZ);
		if (s_disablePlayerMovement) // TFE scripting option
		{
			s_playerLogic.move.x = 0;
			s_playerLogic.move.z = 0;
		}
		else // normal behaviour
		{
			s_playerLogic.move.x = mul16(moveX, s_deltaTime) + mul16(s_externalVelX, s_deltaTime);
			s_playerLogic.move.z = mul16(moveZ, s_deltaTime) + mul16(s_externalVelZ, s_deltaTime);
		}
		s_playerLogic.stepHeight = s_limitStepHeight ? PLAYER_STEP : PLAYER_INF_STEP;
		fixed16_16 width = (s_smallModeEnabled) ? PLAYER_SIZE_SMALL : PLAYER_WIDTH;
		player->worldWidth = width;

		// Handle collision detection and response.
		JBool collided = JFALSE;
		JBool moved = JFALSE;
		JBool prevResponseStep = JFALSE;
		fixed16_16 slideDirX = 0;
		fixed16_16 slideDirZ = 0;
		s_playerSlideWall = nullptr;
		// Up to 4 iterations, to handle sliding on walls.
		if (s_noclip)
		{
			if (s_playerLogic.move.x || s_playerLogic.move.y)
			{
				moved = JTRUE;
			}
			if (s_playerSector)
			{
				s_colCurLowestFloor = s_playerSector->floorHeight;
				s_colCurHighestCeil = s_playerSector->ceilingHeight;
				s_colExtFloorHeight = s_playerSector->colFloorHeight;
				s_colExtCeilHeight = s_playerSector->colCeilHeight;
			}
		}
		else
		{
			for (s32 collisionIter = 4; collisionIter != 0; collisionIter--)
			{
				if (!handlePlayerCollision(&s_playerLogic, s_playerUpVel))
				{
					if (s_playerLogic.move.x || s_playerLogic.move.y)
					{
						moved = JTRUE;
					}
					break;
				}

				// Determine if a collision response is required.
				collided = JTRUE;
				if (!s_colResponseStep)
				{
					break;
				}
				prevResponseStep = s_colResponseStep;

				if (s_colWall0)
				{
					s_playerSlideWall = s_colWall0;
				}
				slideDirX = s_colResponseDir.x;
				slideDirZ = s_colResponseDir.z;
			}
		}

		// If the player moved after collision, handle that here.
		if (moved)
		{
			// Adjust velocity based on collision response.
			if (collided)
			{
				// Adjust the velocity based on the collision response.
				fixed16_16 projVel = mul16(s_playerVelX, slideDirX) + mul16(s_playerVelZ, slideDirZ);
				s_playerVelX = mul16(projVel, slideDirX);
				s_playerVelZ = mul16(projVel, slideDirZ);

				// Adjust the external velocity based on the collision response.
				if (s_externalVelX || s_externalVelZ)
				{
					projVel = mul16(s_externalVelX, slideDirX) + mul16(s_externalVelZ, slideDirZ);
					s_externalVelX = mul16(projVel, slideDirX);
					s_externalVelZ = mul16(projVel, slideDirZ);
				}
			}

			// If this fails, the system will treat it as if the player didn't move (see below).
			// However, it should succeed since the original collision detection did.
			moved = playerMove(s_noclip);
			if (moved)
			{
				s_colWidth += PLAYER_PICKUP_ADJ;	// s_colWidth + 1.5
				playerHandleCollisionFunc(player->sector, collision_checkPickups, nullptr);
				s_colWidth -= PLAYER_PICKUP_ADJ;
				if (s_noclip)
				{
					s_colMinSector = player->sector;
				}
			}
		}

		if (!moved)
		{
			s_colDstPosX = player->posWS.x;
			s_colDstPosZ = player->posWS.z;
			col_computeCollisionResponse(player->sector, s_playerUpVel);
			s_playerLogic.move.x = 0;
			s_playerLogic.move.z = 0;
			s_playerVelX = 0;
			s_playerVelZ = 0;
		}
		s_playerSector = s_colMinSector;

		if (s_externalVelX || s_externalVelZ)
		{
			fixed16_16 friction = FRICTION_DEFAULT;
			Tick dt = s_playerTick - s_prevPlayerTick;
			for (Tick i = 0; i < dt; i++)
			{
				s_externalVelX = mul16(s_externalVelX, friction);
				s_externalVelZ = mul16(s_externalVelZ, friction);
			}
			fixed16_16 approxLen = distApprox(0, 0, s_externalVelX, s_externalVelZ);
			// Reduce the friction min length by half to account for higher framerates.
			if (approxLen < HALF_16/2)
			{
				s_externalVelX = 0;
				s_externalVelZ = 0;
			}
		}

		if (s_playerSecMoved)
		{
			RSector* newSector = sector_which3D(player->posWS.x, player->posWS.y, player->posWS.z);
			RSector* curSector = player->sector;
			// Handle the case where the player changed sectors due to moving sectors.
			if (newSector && newSector != curSector)
			{
				if (newSector->layer != curSector->layer)
				{
					automap_setLayer(newSector->layer);
				}
				sector_addObject(newSector, player);
			}
			s_playerSecMoved = JFALSE;
		}

		if (origSector != player->sector)
		{
			RSector* newSector = player->sector;
			if (newSector->flags1 & SEC_FLAGS1_SAFESECTOR)	
			{
				Safe* safe = level_getSafeFromSector(newSector);
				if (safe)
				{
					s_curSafe = safe;
				}
			}

			if (newSector->flags1 & SEC_FLAGS1_SECRET)
			{
				// If enabled, show the secret found message.
				tfe_showSecretFoundMsg();

				// Remove the flag so the secret isn't counted twice.
				newSector->flags1 &= ~SEC_FLAGS1_SECRET;
				s_secretsFound++;
				level_updateSecretPercent();
			}
		}

		// Adjust angles.
		player->yaw = s_playerYaw & ANGLE_MASK;
		player->pitch = s_playerPitch;
		if (s_externalYawSpd)
		{
			s_externalYawSpd = mul16(s_externalYawSpd, s_deltaTime);
			fixed16_16 speed = s_externalYawSpd << s_playerRun;
			speed >>= s_playerSlow;
			player->yaw += speed;
		}
		sinCosFixed(player->yaw, &s_playerLogic.dir.x, &s_playerLogic.dir.z);

		// Handle falling and jumping.
		fixed16_16 ceilHeight;
		sector_getObjFloorAndCeilHeight(s_playerSector, player->posWS.y, &floorHeight, &ceilHeight);
		fixed16_16 floorRelHeight = floorHeight - player->posWS.y;
		if (floorRelHeight > PLAYER_FALL_SCREAM_MINHEIGHT && s_playerUpVel2 > PLAYER_FALL_SCREAM_VEL && !s_kyleScreamSoundId && !s_playerDying)
		{
			s_kyleScreamSoundId = sound_play(s_kyleScreamSoundSource);
		}
		sector_getObjFloorAndCeilHeight(player->sector, player->posWS.y, &floorHeight, &ceilHeight);

		// Gravity.
		fixed16_16 gravityAccelDt = mul16(s_gravityAccel, s_deltaTime);
		// This happens in an interrupt in the original DOS code.
		if (!s_flyMode)
		{
			s_playerUpVel2 += gravityAccelDt;
		}
		s_playerUpVel  = s_playerUpVel2;
		s_playerYPos += mul16(s_playerUpVel, s_deltaTime);
		s_playerLogic.move.y = s_playerYPos - player->posWS.y;
		player->posWS.y = s_playerYPos;

		if (s_playerYPos >= s_colCurLowestFloor && (!s_noclip || !s_flyMode))
		{
			if (s_kyleScreamSoundId)
			{
				sound_stop(s_kyleScreamSoundId);
				s_kyleScreamSoundId = NULL_SOUND;
			}
			// Handle player land event - this both plays a sound effect and sends an INF message.
			if (s_prevDistFromFloor)
			{
				if (s_colMinSector->secHeight - 1 >= 0)
				{
					// Second height is below ground, so this is liquid.
					sound_play(s_landSplashSound);
				}
				else if (s_prevDistFromFloor > PLAYER_FALL_HIT_SND_HEIGHT)
				{
					// Second height is at or above ground.
					sound_play(s_landSolidSound);
				}
				message_sendToSector(s_colMinSector, player, INF_EVENT_LAND, MSG_TRIGGER);

				// 's_playerUpVel' determines how much the view collapses to the ground based on hit velocity.
				if (s_playerUpVel < PLAYER_LAND_VEL_CHANGE)
				{
					s_postLandVel = min(s_playerUpVel >> 2, PLAYER_LAND_VEL_MAX);
				}
				else
				{
					s_postLandVel = min((s_playerUpVel >> 2) - ((s_playerUpVel - PLAYER_LAND_VEL_CHANGE) >> 3), PLAYER_LAND_VEL_MAX);
				}
				s_landUpVel = s_playerUpVel;
			}
			else
			{
				fixed16_16 yPos = player->posWS.y;
				fixed16_16 yMove = s_playerLogic.move.y;
				fixed16_16 distFromFloor = player->posWS.y - s_colCurLowestFloor;
				// This adjusts the world height to compensate for the step height.
				// Over time the world height will be restored.
				if (distFromFloor > yMove)
				{
					player->worldHeight += (s_colCurBot - yPos + yMove);
				}
			}

			s_playerYPos = s_colCurLowestFloor;
			player->posWS.y = s_colCurLowestFloor;
			fixed16_16 newUpVel = min(0, s_playerUpVel2);
			s_playerUpVel = newUpVel;
			s_playerUpVel2 = newUpVel;
		}
		else
		{
			fixed16_16 playerTop = s_playerYPos - player->worldHeight - ONE_16;
			if (playerTop < s_colCurHighestCeil && (!s_noclip || !s_flyMode))
			{
				fixed16_16 yVel = max(0, s_playerUpVel2);
				s_playerUpVel = yVel;
				s_playerUpVel2 = yVel;

				fixed16_16 newPlayerBot = s_colCurHighestCeil + player->worldHeight + ONE_16;
				s_playerYPos = min(s_colCurLowestFloor, newPlayerBot);
				player->posWS.y = s_playerYPos;
			}
		}

		// Crouch
		player->worldHeight -= mul16(s_playerCrouchSpd, s_deltaTime);

		// Land animation.
		if (s_postLandVel)
		{
			// Modify the player height by post land velocity.
			player->worldHeight -= mul16(s_postLandVel, s_deltaTime);
			// Reduce the post land velocity by 20 units / second.
			s_postLandVel -= mul16(FIXED(20), s_deltaTime);
			s_postLandVel = max(0, s_postLandVel);
		}
		else
		{
			s_landUpVel = 0;
		}

		fixed16_16 eyeToCeil = max(0, player->posWS.y - s_colCurHighestCeil);
		fixed16_16 eyeHeight = eyeToCeil;
		eyeToCeil = min(ONE_16, eyeToCeil);	// the player eye should be clamped to 1 unit below the ceiling if possible.
		eyeHeight -= eyeToCeil;
		eyeHeight = min(PLAYER_HEIGHT, eyeHeight);

		RSector* sector = player->sector;
		fixed16_16 minEyeDistFromFloor = (s_smallModeEnabled) ? PLAYER_SIZE_SMALL : s_minEyeDistFromFloor;
		secHeight = sector->secHeight;
		// The base min distance from the floor is (0) for solid floors and (depth + 0.25) for liquids.
		fixed16_16 minDistToFloor = (sector->secHeight >= 0) ? secHeight + 0x4000 : 0;
		// Then the base is adjusted so it is never smaller than s_minEyeDistFromFloor.
		minDistToFloor = max(minEyeDistFromFloor, minDistToFloor);

		// Adjust world height over time to stand straight up and be the correct distance from the floor.
		if (player->worldHeight < minDistToFloor)
		{
			if (s_postLandVel)
			{
				// Fall Damage if moving at faster than 107 units / second.
				if (s_landUpVel > FIXED(107))
				{
					fixed16_16 dmg = 2 * (s_landUpVel - FIXED(107));
					player_applyDamage(dmg, 0, JFALSE);
					s_landUpVel = 0;
				}
			}
			player->worldHeight = minDistToFloor;
		}
		// Make sure eye height is clamped.
		if (!s_noclip || !s_flyMode)
		{
			player->worldHeight = min(eyeHeight, player->worldHeight);
		}

		// Crushing Damage.
		if (player->worldHeight < minEyeDistFromFloor && (!s_noclip || !s_flyMode))
		{
			if (!s_crushSoundId)
			{
				s_crushSoundId = sound_play(s_crushSoundSource);
			}
			// Crushing damage is 10 damage/second
			fixed16_16 crushingDmg = mul16(PLAYER_DMG_CRUSHING, s_deltaTime);
			player_applyDamage(0, crushingDmg, JFALSE);
			player->worldHeight = minDistToFloor;
			playerHandleCollisionFunc(player->sector, collision_handleCrushing, nullptr);
		}
		else if (s_crushSoundId)
		{
			sound_stop(s_crushSoundId);
			s_crushSoundId = NULL_SOUND;
		}

		if (!s_smallModeEnabled)
		{
			fixed16_16 dH = PLAYER_HEIGHT - player->worldHeight;
			fixed16_16 maxMove = (s_playerInWater) ? FIXED(25) : FIXED(32);
			s_maxMoveDist = maxMove - dH * 4;
		}
		else
		{
			s_maxMoveDist = (s_playerInWater) ? FIXED(25) : FIXED(32);
		}

		// Headwave
		s32 xWpnWaveOffset = 0;
		s_headwaveVerticalOffset = 0;
		if (TFE_Settings::getA11ySettings()->enableHeadwave && (player->flags & OBJ_FLAG_EYE))
		{
			fixed16_16 playerSpeedAve = distApprox(0, 0, s_playerVelX, s_playerVelZ);
			if (!moved)
			{
				playerSpeedAve = 0;
			}
			if ((s_playerSector->flags1 & SEC_FLAGS1_ICE_FLOOR) && !s_wearingCleats)
			{
				playerSpeedAve = 0;
			}

			if (s_playerSpeedAve != playerSpeedAve)
			{
				fixed16_16 maxFrameChange = mul16(FIXED(32), s_deltaTime);
				fixed16_16 speedDelta = clamp(playerSpeedAve - s_playerSpeedAve, -maxFrameChange, maxFrameChange);
				s_playerSpeedAve += speedDelta;
			}

			sinCosFixed(s_curTick << 7, &s_wpnSin, &s_wpnCos);
			fixed16_16 playerSpeedFract = div16(s_playerSpeedAve, FIXED(32));
			s_headwaveVerticalOffset = mul16(mul16(s_wpnCos, PLAYER_HEADWAVE_VERT_SPD), playerSpeedFract);

			sinCosFixed(s_curTick << 6, &s_wpnSin, &s_wpnCos);
			xWpnWaveOffset = mul16(playerSpeedFract, s_wpnCos) >> 12;

			// Water...
			if (s_playerSector->secHeight - 1 >= 0 && (s_externalVelX || s_externalVelZ))
			{
				fixed16_16 externSpd = distApprox(0, 0, s_externalVelX, s_externalVelZ) >> 17;
				sinCosFixed(s_curTick << 4, &s_wpnSin, &s_wpnCos);
				s_headwaveVerticalOffset += mul16(s_wpnCos, PLAYER_HEADWAVE_VERT_WATER_SPD);
			}
		}
		setCameraOffset(0, s_headwaveVerticalOffset, 0);

		// Apply the weapon motion.
		PlayerWeapon* weapon = s_curPlayerWeapon;
		weapon->xWaveOffset = xWpnWaveOffset;					// the x offset has 4 bits of sub-texel precision.
		weapon->yWaveOffset = s_headwaveVerticalOffset >> 13;	// the y offset is probably the same 4 bits of precision & multiplied by half.

		// The moves the player can make are restricted based on whether they are on the floor or not.
		if (s_colCurLowestFloor == player->posWS.y)
		{
			s_onFloor = JTRUE;
			s_prevDistFromFloor = 0;
		}
		else
		{
			// Player is *not* on the floor, so things like crouching and jumping are not available.
			s_onFloor = JFALSE;
		}

		// Apply sector or floor damage.
		u32 lowAndHighFlag = SEC_FLAGS1_LOW_DAMAGE | SEC_FLAGS1_HIGH_DAMAGE;
		u32 dmgFlags = s_playerSector->flags1 & lowAndHighFlag;
		// Handle damage floors.
		if (dmgFlags == SEC_FLAGS1_LOW_DAMAGE && s_onFloor)
		{
			fixed16_16 dmg = mul16(s_lowFloorDamage, s_deltaTime);
			player_applyDamage(dmg, 0, JTRUE);
		}
		else if (dmgFlags == SEC_FLAGS1_HIGH_DAMAGE && s_onFloor)
		{
			fixed16_16 dmg = mul16(s_highFloorDamage, s_deltaTime);
			player_applyDamage(dmg, 0, JTRUE);
		}
		else if (dmgFlags == lowAndHighFlag && !s_wearingGasmask && !s_playerDying)
		{
			fixed16_16 dmg = mul16(s_gasDamage, s_deltaTime);
			player_applyDamage(dmg, 0, JFALSE);

			if (!s_gasSectorTask)
			{
				s_gasSectorTask = createSubTask("gas sector", gasSectorTaskFunc);
			}
		}
		// Free the gas damage task if the gas mask is worn or if the damage flags no longer match up.
		if (s_gasSectorTask && (s_wearingGasmask || dmgFlags != lowAndHighFlag || s_playerDying))
		{
			task_free(s_gasSectorTask);
			s_gasSectorTask = nullptr;
		}

		// Handle damage walls.
		if (s_playerSlideWall && (s_playerSlideWall->flags1 & WF1_DAMAGE_WALL))
		{
			fixed16_16 dmg = mul16(s_wallDamage, s_deltaTime);
			player_applyDamage(dmg, 0, JTRUE);
		}

		// Double check the player Y position against the floor and ceiling.
		if (!s_noclip || !s_flyMode)
		{
			if (player->posWS.y > s_colCurLowestFloor)
			{
				s_playerYPos = s_colCurLowestFloor;
				player->posWS.y = s_colCurLowestFloor;
			}
			if (player->posWS.y < s_colCurHighestCeil)
			{
				s_playerYPos = s_colCurHighestCeil;
				player->posWS.y = s_colCurHighestCeil;
			}
		}

		// Clamp the height if needed.
		fixed16_16 yTop = player->posWS.y - s_colExtCeilHeight;
		fixed16_16 yBot = player->posWS.y - s_colExtFloorHeight + 0x4000;
		if (!s_noclip || !s_flyMode)
		{
			if (player->worldHeight - s_camOffset.y > yTop)
			{
				player->worldHeight = yTop + s_camOffset.y;
			}
			if (player->worldHeight - s_camOffset.y < yBot)
			{
				player->worldHeight = yBot + s_camOffset.y;
			}
		}

		weapon->rollOffset = -s_playerRoll / 13;
		weapon->pchOffset  = s_playerPitch / 64;

		// Handle camera lighting and night vision.
		if (player->flags & OBJ_FLAG_EYE)
		{
			// This code is incorrect but it doesn't actually matter...
			// roll is always 0.
			setCameraAngleOffset(0, s_playerRoll, 0);

			s32 headlamp = 0;
			if (s_headlampActive)
			{
				fixed16_16 batteryPower = min(ONE_16, s_batteryPower);
				headlamp = floor16(mul16(batteryPower, FIXED(64)));
				headlamp = min(MAX_LIGHT_LEVEL, headlamp);
			}
			s32 atten;
			if (TFE_Settings::getA11ySettings()->disablePlayerWeaponLighting)
			{
				atten = max(headlamp, s_levelAtten);
			}
			else
			{
				atten = max(headlamp, s_weaponLight + s_levelAtten);
			}
			s_baseAtten = atten;
			if (s_nightVisionActive)
			{
				atten = 0;
			}
			setPlayerLight(atten);
		}

		fixed16_16 distFromFloor = s_colCurLowestFloor - player->posWS.y;
		if (distFromFloor <= s_prevDistFromFloor)
		{
			distFromFloor = s_prevDistFromFloor;
		}
		s_prevDistFromFloor = max(distFromFloor, s_prevDistFromFloor);
	}

	void player_applyDamage(fixed16_16 healthDmg, fixed16_16 shieldDmg, JBool playHitSound)
	{
		fixed16_16 shields = intToFixed16(s_playerInfo.shields);
		fixed16_16 health  = intToFixed16(s_playerInfo.health);
		health += s_playerInfo.healthFract;

		s32 applyDmg = s_invincibility ? 0 : 1;
		if (applyDmg && health && shieldDmg >= 0)
		{
			if (shieldDmg && s_curTick > s_nextShieldDmgTick && !s_config.superShield)
			{
				// The amount of shield energy left after taking *half* damage.
				fixed16_16 halfShieldDmg = TFE_Jedi::abs(shieldDmg) >> 1;
				shields = max(0, shields - halfShieldDmg);
				if (shields < FIXED(50))
				{
					// healthDmg += shieldDmg * (1 - shields/50.0)
					fixed16_16 fracDmgToHealth = ONE_16 - div16(shields, FIXED(50));
					healthDmg += mul16(fracDmgToHealth, shieldDmg);
				}
				// Now take the other half away.
				shields = max(0, shields - halfShieldDmg);
				s_playerInfo.shields = pickup_addToValue(floor16(shields), 0, s_shieldsMax);
				if (playHitSound)
				{
					sound_play(s_playerShieldHitSoundSource);
				}

				s_shieldDamageFx += (shieldDmg << 2);
				s_shieldDamageFx = min(FIXED(17), s_shieldDamageFx);
			}
		}
		applyDmg = (s_invincibility == -2) ? 0 : 1;
		if (applyDmg && healthDmg && health)
		{
			health -= healthDmg;
			if (health < ONE_16)
			{
				s_playerInfo.healthFract = 0;
				// We could just set the health to 0 here...
				s_playerInfo.health = pickup_addToValue(0, 0, s_healthMax);
				if (playHitSound)
				{
					sound_play(s_playerDeathSoundSource);
				}
				if (s_gasSectorTask)
				{
					task_free(s_gasSectorTask);
				}
				health = 0;
				s_gasSectorTask = nullptr;
				s_playerDying = JTRUE;
				s_reviveTick = s_curTick + 436;
			}
			else
			{
				if (playHitSound && s_curTick > s_nextPainSndTick)
				{
					sound_play(s_playerHealthHitSoundSource);
					s_nextPainSndTick = s_curTick + 72;	// half a second.
				}
				health = max(0, health);
				s32 healthInt  = floor16(health);
				s32 healthFrac = fract16(health);
				s_playerInfo.health = healthInt;
				s_playerInfo.healthFract = healthFrac;
			}
			s_healthDamageFx += TFE_Jedi::abs(healthDmg) >> 1;
			s_healthDamageFx = clamp(s_healthDamageFx, ONE_16, FIXED(17));
		}
	}

	void handlePlayerUseAction()
	{
		SecObject* player = s_playerObject;
		RSector* sector = player->sector;

		// Trigger the sector the player is standing in.
		message_sendToSector(sector, s_playerObject, INF_EVENT_NUDGE_FRONT, MSG_TRIGGER);

		// Then cast a ray and see if the wall in front of the player is close enough to trigger.
		fixed16_16 x0 = player->posWS.x;
		fixed16_16 z0 = player->posWS.z;
		fixed16_16 x1 = x0 + mul16(FIXED(4), s_playerLogic.dir.x);
		fixed16_16 z1 = z0 + mul16(FIXED(4), s_playerLogic.dir.z);
		RWall* wall = collision_wallCollisionFromPath(sector, x0, z0, x1, z1);
		if (wall)
		{
			RSector* nextSector = wall->nextSector;
			fixed16_16 eyeHeight = player->posWS.y - player->worldHeight;
			if (nextSector)
			{
				s_prevCollisionFrameWall = s_collisionFrameWall;
				fixed16_16 x, z;
				collision_getHitPoint(&x, &z);

				// Nudge the wall.
				fixed16_16 distFromW0 = vec2Length(wall->w0->x - x, wall->w0->z - z);
				inf_wallAndMirrorSendMessageAtPos(wall, s_playerObject, INF_EVENT_NUDGE_FRONT, distFromW0, eyeHeight);
				// Nudge the next sector from the outside.
				message_sendToSector(nextSector, s_playerObject, INF_EVENT_NUDGE_BACK, MSG_TRIGGER);
				// Set the sector collision frame to avoid infinite loops.
				nextSector->collisionFrame = s_collisionFrameWall;

				// The eye ray can go through the opening.
				if (eyeHeight > nextSector->ceilingHeight && eyeHeight < nextSector->floorHeight)
				{
					// Loop as the ray goes from sector to sector.
					do
					{
						wall = collision_pathWallCollision(nextSector);
						if (wall)
						{
							// Trigger the next wall.
							collision_getHitPoint(&x, &z);
							distFromW0 = vec2Length(wall->w0->x - x, wall->w0->z - z);
							inf_wallAndMirrorSendMessageAtPos(wall, s_playerObject, INF_EVENT_NUDGE_FRONT, distFromW0, eyeHeight);

							nextSector = wall->nextSector;
							if (nextSector)
							{
								if (s_collisionFrameWall != nextSector->collisionFrame)
								{
									// Nudge the next sector from the outside.
									message_sendToSector(nextSector, s_playerObject, INF_EVENT_NUDGE_BACK, MSG_TRIGGER);
									nextSector->collisionFrame = s_collisionFrameWall;
								}
								// Make sure the eye ray fits in the opening.
								if (eyeHeight < nextSector->ceilingHeight || eyeHeight > nextSector->floorHeight)
								{
									break;
								}
							}
						}
					} while (wall && nextSector);
				}
				if (s_collisionFrameWall != s_prevCollisionFrameWall)
				{
					TFE_System::logWrite(LOG_ERROR, "Player Use", "MARK HAS UNEXPECTEDLY CHANGED.");
				}
			}
			else
			{
				fixed16_16 x, z;
				collision_getHitPoint(&x, &z);

				s32 distFromW0 = vec2Length(wall->w0->x - x, wall->w0->z - z);
				inf_wallAndMirrorSendMessageAtPos(wall, s_playerObject, INF_EVENT_NUDGE_FRONT, distFromW0, eyeHeight);
			}
		}
	}
				
	void handlePlayerActions()
	{
		if (s_batteryPower)
		{
			if (s_headlampActive)
			{
				fixed16_16 powerDelta = mul16(s_headlampConsumption, s_deltaTime);
				s_batteryPower -= powerDelta;
			}
			if (s_wearingGasmask)
			{
				fixed16_16 powerDelta = mul16(s_gasmaskConsumption, s_deltaTime);
				s_batteryPower -= powerDelta;
			}
			if (s_nightVisionActive)
			{
				fixed16_16 powerDelta = mul16(s_gogglesConsumption, s_deltaTime);
				s_batteryPower -= powerDelta;
			}
			if (s_batteryPower <= 0)
			{
				if (s_nightVisionActive)
				{
					s_nightVisionActive = JFALSE;
					disableNightVisionInternal();
					hud_sendTextMessage(9);
				}
				if (s_headlampActive)
				{
					hud_sendTextMessage(12);
					s_headlampActive = JFALSE;
				}
				if (s_wearingGasmask)
				{
					hud_sendTextMessage(19);
					s_wearingGasmask = JFALSE;
					if (s_gasmaskTask)
					{
						task_free(s_gasmaskTask);
						s_gasmaskTask = nullptr;
					}
				}
				s_batteryPower = 0;
			}
		}
		if (s_playerUse)
		{
			s_playerUse = JFALSE;
			if (!s_playerActionUse)
			{
				handlePlayerUseAction();
				s_playerActionUse = JTRUE;
			}
		}
		else
		{
			s_playerActionUse = JFALSE;
		}

		if (s_playerPrimaryFire)
		{
			s_playerPrimaryFire = JFALSE;
			if (!s_weaponFiring && s_playerWeaponTask)
			{
				s_weaponFiring = JTRUE;
				// This causes the weapon to fire.
				s_msgArg1 = WFIRETYPE_PRIMARY;
				task_runAndReturn(s_playerWeaponTask, MSG_START_FIRING);
			}
		}
		// This happens when the player *stops* firing.
		else if (s_weaponFiring && s_playerWeaponTask)
		{
			s_weaponFiring = JFALSE;
			if (!s_weaponFiringSec)
			{
				// Spin down.
				task_runAndReturn(s_playerWeaponTask, MSG_STOP_FIRING);
			}
			else
			{
				// Start secondary fire.
				s_msgArg1 = WFIRETYPE_SECONDARY;
				task_runAndReturn(s_playerWeaponTask, MSG_START_FIRING);
			}
		}

		if (s_playerSecFire)
		{
			s_playerSecFire = JFALSE;
			if (!s_weaponFiringSec && s_playerWeaponTask)
			{
				s_weaponFiringSec = JTRUE;
				s_msgArg1 = WFIRETYPE_SECONDARY;
				task_runAndReturn(s_playerWeaponTask, MSG_START_FIRING);
			}
		}
		// This happens when the player *stops* firing.
		else if (s_weaponFiringSec && s_playerWeaponTask) 
		{
			s_weaponFiringSec = JFALSE;
			if (!s_weaponFiring)
			{
				// Spin down
				task_runAndReturn(s_playerWeaponTask, MSG_STOP_FIRING);
			}
			else
			{
				// Start primary fire.
				s_msgArg1 = WFIRETYPE_PRIMARY;
				task_runAndReturn(s_playerWeaponTask, MSG_START_FIRING);
			}
		}

		if (!s_playerDying)
		{
			// Weapon select.
			if (inputMapping_getActionState(IADF_WEAPON_1) == STATE_PRESSED)
			{
				s_playerInfo.newWeapon = WPN_FIST;
			}
			if (inputMapping_getActionState(IADF_WEAPON_2) == STATE_PRESSED && s_playerInfo.itemPistol)
			{
				s_playerInfo.newWeapon = WPN_PISTOL;
			}
			if (inputMapping_getActionState(IADF_WEAPON_3) == STATE_PRESSED && s_playerInfo.itemRifle)
			{
				s_playerInfo.newWeapon = WPN_RIFLE;
			}
			if (inputMapping_getActionState(IADF_WEAPON_4) == STATE_PRESSED)
			{
				s_playerInfo.newWeapon = WPN_THERMAL_DET;
			}
			if (inputMapping_getActionState(IADF_WEAPON_5) == STATE_PRESSED && s_playerInfo.itemAutogun)
			{
				s_playerInfo.newWeapon = WPN_REPEATER;
			}
			if (inputMapping_getActionState(IADF_WEAPON_6) == STATE_PRESSED && s_playerInfo.itemFusion)
			{
				s_playerInfo.newWeapon = WPN_FUSION;
			}
			if (inputMapping_getActionState(IADF_WEAPON_7) == STATE_PRESSED)
			{
				s_playerInfo.newWeapon = WPN_MINE;
			}
			if (inputMapping_getActionState(IADF_WEAPON_8) == STATE_PRESSED && s_playerInfo.itemMortar)
			{
				s_playerInfo.newWeapon = WPN_MORTAR;
			}
			if (inputMapping_getActionState(IADF_WEAPON_9) == STATE_PRESSED && s_playerInfo.itemConcussion)
			{
				s_playerInfo.newWeapon = WPN_CONCUSSION;
			}
			if (inputMapping_getActionState(IADF_WEAPON_10) == STATE_PRESSED && s_playerInfo.itemCannon)
			{
				s_playerInfo.newWeapon = WPN_CANNON;
			}
			if (inputMapping_getActionState(IADF_WPN_PREV) == STATE_PRESSED)
			{
				s_playerInfo.newWeapon = WPN_COUNT;
			}

			s32 newWeapon = s_playerInfo.newWeapon;
			if (newWeapon != -1)
			{
				s32 curWeapon = s_playerInfo.curWeapon;
				if (newWeapon != curWeapon)
				{
					if (s_playerWeaponTask)
					{
						// Change weapon
						s_msgArg1 = newWeapon < WPN_COUNT ? newWeapon : -1;
						task_runAndReturn(s_playerWeaponTask, MSG_SWITCH_WPN);

						if (s_playerInfo.curWeapon > s_playerInfo.maxWeapon)
						{
							s_playerInfo.maxWeapon = s_playerInfo.curWeapon;
						}
					}
					else
					{
						s_playerInfo.saveWeapon = newWeapon;
					}
				}
				s_playerInfo.newWeapon = -1;
			}
		}
	}
				
	void handlePlayerScreenFx()
	{
		if (TFE_Settings::getA11ySettings()->disableScreenFlashes) { return; }

		s32 healthFx, shieldFx, flashFx;
		if (s_healthDamageFx)
		{
			s32 effectIndex = min(15, s_healthDamageFx >> 16);
			healthFx = c_healthDmgToFx[effectIndex];
			s_healthDamageFx = max(0, s_healthDamageFx - mul16(c_damageDelta, s_deltaTime));
		}
		else
		{
			healthFx = 0;
		}
		if (s_shieldDamageFx)
		{
			s32 effectIndex = min(15, s_shieldDamageFx >> 16);
			shieldFx = c_shieldDmgToFx[effectIndex];
			s_shieldDamageFx = max(0, s_shieldDamageFx - mul16(c_shieldDelta, s_deltaTime));
		}
		else
		{
			shieldFx = 0;
		}
		if (s_flashEffect)
		{
			s32 effectIndex = min(15, s_flashEffect >> 16);
			flashFx = c_flashAmtToFx[effectIndex];
			s_flashEffect = max(0, s_flashEffect - mul16(c_flashDelta, s_deltaTime));
		}
		else
		{
			flashFx = 0;
		}
		setScreenFxLevels(healthFx, shieldFx, flashFx);
	}

	void gasSectorTaskFunc(MessageType msg)
	{
		task_begin;
		while (1)
		{
			sound_play(s_gasDamageSoundSource);
			task_yield(291);	// wait for 2 seconds.
		}
		task_end;
	}
		
	JBool player_hasWeapon(s32 weaponIndex)
	{
		JBool retval = JFALSE;
		switch (weaponIndex)
		{
		case 2:	// WPN_PISTOL + 1
			retval = s_playerInfo.itemPistol;
			break;
		case 3:
			retval = s_playerInfo.itemRifle;
			break;
		case 4:
			retval = s_playerInfo.ammoDetonator > 0 ? JTRUE : JFALSE;
			break;
		case 5:
			retval = s_playerInfo.itemAutogun;
			break;
		case 6:
			retval = s_playerInfo.itemFusion;
			break;
		case 7:
			retval = s_playerInfo.ammoMine > 0 ? JTRUE : JFALSE;
			break;
		case 8:
			retval = s_playerInfo.itemMortar;
			break;
		case 9:
			retval = s_playerInfo.itemConcussion;
			break;
		case 10:
			retval = s_playerInfo.itemCannon;
			break;
		}
		return retval;
	}

	JBool player_hasItem(s32 itemIndex)
	{
		JBool retval = JFALSE;
		switch (itemIndex)
		{
		case 0:
			retval = s_playerInfo.itemRedKey;
			break;
		case 1:
			retval = s_playerInfo.itemYellowKey;
			break;
		case 2:
			retval = s_playerInfo.itemBlueKey;
			break;
		case 3:
			retval = s_playerInfo.itemGoggles;
			break;
		case 4:
			retval = s_playerInfo.itemCleats;
			break;
		case 5:
			retval = s_playerInfo.itemMask;
			break;
		case 6:
			retval = s_playerInfo.itemPlans;
			break;
		case 7:
			retval = s_playerInfo.itemPhrik;
			break;
		case 8:
			retval = s_playerInfo.itemNava;
			break;
		case 9:
			retval = s_playerInfo.itemDatatape;
			break;
		case 10:
			retval = s_playerInfo.itemUnused;
			break;
		case 11:
			retval = s_playerInfo.itemDtWeapon;
			break;
		case 12:
			retval = s_playerInfo.itemCode1;
			break;
		case 13:
			retval = s_playerInfo.itemCode2;
			break;
		case 14:
			retval = s_playerInfo.itemCode3;
			break;
		case 15:
			retval = s_playerInfo.itemCode4;
			break;
		case 16:
			retval = s_playerInfo.itemCode5;
			break;
		case 17:
			retval = s_playerInfo.itemCode6;
			break;
		case 18:
			retval = s_playerInfo.itemCode7;
			break;
		case 19:
			retval = s_playerInfo.itemCode8;
			break;
		case 20:
			retval = s_playerInfo.itemCode9;
			break;
		}
		return retval;
	}

	// TFE
	void player_warp(const ConsoleArgList& args)
	{
		if (args.size() < 4) { return; }
		fixed16_16 x =  floatToFixed16(TFE_Console::getFloatArg(args[1]));
		fixed16_16 y = -floatToFixed16(TFE_Console::getFloatArg(args[2]));
		fixed16_16 z =  floatToFixed16(TFE_Console::getFloatArg(args[3]));
		RSector* sector = sector_which3D(x, y, z);

		if (sector)
		{
			s_playerObject->posWS = { x, y, z };
			s_playerPos = s_playerObject->posWS;

			sector_addObject(sector, s_playerObject);
			s_playerSector = s_playerObject->sector;
		}
	}

	void player_sector(const ConsoleArgList& args)
	{
		RSector* sector = s_playerObject->sector;
		if (sector)
		{
			char sectorIdStr[256];
			sprintf(sectorIdStr, "Sector ID: %d", sector->id);
			TFE_Console::addToHistory(sectorIdStr);
		}
		else
		{
			TFE_Console::addToHistory("Invalid sector");
		}
	}
		
	// Serialization
	void playerLogic_serialize(Logic*& logic, SecObject* obj, Stream* stream)
	{
		PlayerLogic* playerLogic;
		if (serialization_getMode() == SMODE_WRITE)
		{
			playerLogic = (PlayerLogic*)logic;
		}
		else
		{
			playerLogic = &s_playerLogic;
			logic = (Logic*)playerLogic;
			task_makeActive(s_playerTask);

			playerLogic->logic.task = s_playerTask;
			playerLogic->logic.cleanupFunc = playerLogicCleanupFunc;
		}
		assert(playerLogic == &s_playerLogic);

		const vec2_fixed defV2 = { 0 };
		const vec3_fixed defV3 = { 0 };
		SERIALIZE(ObjState_InitVersion, playerLogic->dir, defV2);
		SERIALIZE(ObjState_InitVersion, playerLogic->move, defV3);
		SERIALIZE(ObjState_InitVersion, playerLogic->stepHeight, 0);

		SERIALIZE(ObjState_InitVersion, s_externalYawSpd, 0);
		SERIALIZE(ObjState_InitVersion, s_playerPitch, 0);
		SERIALIZE(ObjState_InitVersion, s_playerRoll, 0);
		SERIALIZE(ObjState_InitVersion, s_forwardSpd, 0);
		SERIALIZE(ObjState_InitVersion, s_strafeSpd, 0);
		SERIALIZE(ObjState_InitVersion, s_maxMoveDist, 0);
		SERIALIZE(ObjState_InitVersion, s_playerStopAccel, 0);
		SERIALIZE(ObjState_InitVersion, s_minEyeDistFromFloor, 0);
		SERIALIZE(ObjState_InitVersion, s_postLandVel, 0);
		SERIALIZE(ObjState_InitVersion, s_landUpVel, 0);
		SERIALIZE(ObjState_InitVersion, s_playerVelX, 0);
		SERIALIZE(ObjState_InitVersion, s_playerUpVel, 0);
		SERIALIZE(ObjState_InitVersion, s_playerUpVel2, 0);
		SERIALIZE(ObjState_InitVersion, s_playerVelZ, 0);
		SERIALIZE(ObjState_InitVersion, s_externalVelX, 0);
		SERIALIZE(ObjState_InitVersion, s_externalVelZ, 0);
		SERIALIZE(ObjState_InitVersion, s_playerCrouchSpd, 0);
		SERIALIZE(ObjState_InitVersion, s_playerSpeedAve, 0);
		SERIALIZE(ObjState_InitVersion, s_prevDistFromFloor, 0);
		SERIALIZE(ObjState_InitVersion, s_wpnSin, 0);
		SERIALIZE(ObjState_InitVersion, s_wpnCos, 0);
		SERIALIZE(ObjState_InitVersion, s_moveDirX, 0);
		SERIALIZE(ObjState_InitVersion, s_moveDirZ, 0);
		SERIALIZE(ObjState_InitVersion, s_dist, 0);
		SERIALIZE(ObjState_InitVersion, s_distScale, 0);

		SERIALIZE(ObjState_InitVersion, s_levelAtten, 0);
		SERIALIZE(ObjState_InitVersion, s_prevCollisionFrameWall, 0);
		
		// Store safe sector, derive the safe itself from it on load.
		RSector* safeSector = nullptr;
		if (serialization_getMode() == SMODE_WRITE) { safeSector = s_curSafe ? s_curSafe->sector : nullptr; }
		serialization_serializeSectorPtr(stream, ObjState_InitVersion, safeSector);
		if (serialization_getMode() == SMODE_READ) 	{ s_curSafe = safeSector ? level_getSafeFromSector(safeSector) : nullptr; }
		// There should always be a safe the level start always acts as one.
		assert(s_curSafe);
		
		SERIALIZE(ObjState_InitVersion, s_playerUse, 0);
		SERIALIZE(ObjState_InitVersion, s_playerActionUse, 0);
		SERIALIZE(ObjState_InitVersion, s_playerPrimaryFire, 0);
		SERIALIZE(ObjState_InitVersion, s_playerSecFire, 0);
		SERIALIZE(ObjState_InitVersion, s_playerJumping, 0);
		SERIALIZE(ObjState_InitVersion, s_playerInWater, 0);
		SERIALIZE(ObjState_InitVersion, s_limitStepHeight, 0);
		SERIALIZE(ObjState_InitVersion, s_smallModeEnabled, 0);
		SERIALIZE(ObjState_InitVersion, s_aiActive, 0);

		// Clear sound effects - s_crushSoundId, s_kyleScreamSoundId
		if (serialization_getMode() == SMODE_READ)
		{
			s_crushSoundId = 0;
			s_kyleScreamSoundId = 0;
		}

		SERIALIZE(ObjState_InitVersion, s_playerPos, defV3);
		SERIALIZE(ObjState_InitVersion, s_playerObjHeight, 0);
		SERIALIZE(ObjState_InitVersion, s_playerObjPitch, 0);
		SERIALIZE(ObjState_InitVersion, s_playerObjYaw, 0);
		serialization_serializeSectorPtr(stream, ObjState_InitVersion, s_playerObjSector);

		// Save wall data.
		RSector* slideWallSector = nullptr;
		s32 wallId = -1;
		if (serialization_getMode() == SMODE_WRITE)
		{
			slideWallSector = s_playerSlideWall ? s_playerSlideWall->sector : nullptr;
			wallId = s_playerSlideWall ? s_playerSlideWall->id : -1;
		}
		serialization_serializeSectorPtr(stream, ObjState_InitVersion, slideWallSector);
		SERIALIZE(ObjState_InitVersion, wallId, -1);
		if (serialization_getMode() == SMODE_READ)
		{
			s_playerSlideWall = slideWallSector ? &slideWallSector->walls[wallId] : nullptr;
		}

		SERIALIZE(ObjState_InitVersion, s_playerInfo, { 0 });
		SERIALIZE(ObjState_InitVersion, s_batteryPower, 0);
		SERIALIZE(ObjState_InitVersion, s_lifeCount, 0);
		SERIALIZE(ObjState_InitVersion, s_playerLight, 0);
		SERIALIZE(ObjState_InitVersion, s_headwaveVerticalOffset, 0);
		SERIALIZE(ObjState_InitVersion, s_onFloor, 0);
		SERIALIZE(ObjState_InitVersion, s_weaponLight, 0);
		SERIALIZE(ObjState_InitVersion, s_baseAtten, 0);
		SERIALIZE(ObjState_InitVersion, s_gravityAccel, 0);
		SERIALIZE(ObjState_InitVersion, s_invincibility, 0);
		SERIALIZE(ObjState_InitVersion, s_weaponFiring, 0);
		SERIALIZE(ObjState_InitVersion, s_weaponFiringSec, 0);
		SERIALIZE(ObjState_InitVersion, s_wearingCleats, 0);
		SERIALIZE(ObjState_InitVersion, s_wearingGasmask, 0);
		SERIALIZE(ObjState_InitVersion, s_nightVisionActive, 0);
		SERIALIZE(ObjState_InitVersion, s_headlampActive, 0);
		SERIALIZE(ObjState_InitVersion, s_superCharge, 0);
		SERIALIZE(ObjState_InitVersion, s_superChargeHud, 0);
		SERIALIZE(ObjState_InitVersion, s_playerSecMoved, 0);
		SERIALIZE(ObjState_FlyModeAdded, s_flyMode, JFALSE);
		SERIALIZE(ObjState_OneHitCheats, s_oneHitKillEnabled, JFALSE);
		SERIALIZE(ObjState_OneHitCheats, s_instaDeathEnabled, JFALSE);
		SERIALIZE(ObjState_CrouchToggle, s_playerCrouch, 0);
		SERIALIZE(ObjState_ConstOverrides, s_lowFloorDamage, PLAYER_DMG_FLOOR_LOW);
		SERIALIZE(ObjState_ConstOverrides, s_highFloorDamage, PLAYER_DMG_FLOOR_HIGH);
		SERIALIZE(ObjState_ConstOverrides, s_gasDamage, PLAYER_DMG_FLOOR_LOW);
		SERIALIZE(ObjState_ConstOverrides, s_wallDamage, PLAYER_DMG_WALL);
		SERIALIZE(ObjState_ConstOverrides, s_headlampConsumption, HEADLAMP_ENERGY_CONSUMPTION);
		SERIALIZE(ObjState_ConstOverrides, s_gogglesConsumption, GOGGLES_ENERGY_CONSUMPTION);
		SERIALIZE(ObjState_ConstOverrides, s_gasmaskConsumption, GASMASK_ENERGY_CONSUMPTION);
		SERIALIZE(ObjState_ConstOverrides, s_shieldSuperchargeDuration, SUPERCHARGE_DURATION);
		SERIALIZE(ObjState_ConstOverrides, s_weaponSuperchargeDuration, SUPERCHARGE_DURATION);
		
		s32 projectileGravity = 0;
		if (serialization_getMode() == SMODE_WRITE)
		{
			projectileGravity = getProjectileGravityAccel();
		}
		SERIALIZE(ObjState_ConstOverrides, projectileGravity, FIXED(120));
		if (serialization_getMode() == SMODE_READ)
		{
			setProjectileGravityAccel(projectileGravity);
		}

		SERIALIZE(ObjState_DisablePlayerMovement, s_disablePlayerMovement, JFALSE);
		SERIALIZE(ObjState_DisablePlayerMovement, s_disablePlayerRotation, JFALSE);
		SERIALIZE(ObjState_DisablePlayerMovement, s_disablePlayerFire, JFALSE);
		SERIALIZE(ObjState_ExternalCamera, s_externalCameraMode, JFALSE);

		s32 invSavedSize = 0;
		if (serialization_getMode() == SMODE_WRITE && s_playerInvSaved)
		{
			invSavedSize = s32((size_t)&s_playerInfo.pileSaveMarker - (size_t)&s_playerInfo);
			assert(invSavedSize == 140);
		}
		else if (serialization_getMode() == SMODE_READ)
		{
			s_playerInvSaved = nullptr;
		}
		SERIALIZE(ObjState_InitVersion, invSavedSize, 0);
		if (invSavedSize)
		{
			if (serialization_getMode() == SMODE_READ)
			{
				s_playerInvSaved = (u32*)level_alloc(invSavedSize);
			}
			SERIALIZE_BUF(ObjState_InitVersion, s_playerInvSaved, invSavedSize);
		}

		serialization_serializeSectorPtr(stream, ObjState_InitVersion, s_playerSector);

		s32 playerObjId = -1, playerEyeId = -1;
		if (serialization_getMode() == SMODE_WRITE)
		{
			playerObjId = s_playerObject ? s_playerObject->serializeIndex : -1;
			playerEyeId = s_playerEye ? s_playerEye->serializeIndex : -1;
		}
		SERIALIZE(ObjState_InitVersion, playerObjId, -1);
		SERIALIZE(ObjState_InitVersion, playerEyeId, -1);
		if (serialization_getMode() == SMODE_READ)
		{
			s_playerObject = playerObjId < 0 ? nullptr : objData_getObjectBySerializationId(playerObjId);
			s_playerEye    = playerEyeId < 0 ? nullptr : objData_getObjectBySerializationId_NoValidation(playerEyeId);

			if (s_nightVisionActive)
			{
				TFE_Jedi::s_flatAmbient = 16;
				TFE_Jedi::s_flatLighting = JTRUE;
			}
			else
			{
				TFE_Jedi::s_flatLighting = JFALSE;
			}
		}

		SERIALIZE(ObjState_InitVersion, s_eyePos, defV3);
		SERIALIZE(ObjState_InitVersion, s_eyePitch, 0);
		SERIALIZE(ObjState_InitVersion, s_eyeYaw, 0);
		SERIALIZE(ObjState_InitVersion, s_eyeRoll, 0);
		SERIALIZE(ObjState_InitVersion, s_playerEyeFlags, 0);
		SERIALIZE(ObjState_InitVersion, s_playerTick, 0);
		SERIALIZE(ObjState_InitVersion, s_prevPlayerTick, 0);
		SERIALIZE(ObjState_InitVersion, s_nextShieldDmgTick, 0);
		SERIALIZE(ObjState_InitVersion, s_reviveTick, 0);
		SERIALIZE(ObjState_InitVersion, s_nextPainSndTick, 0);

		SERIALIZE(ObjState_InitVersion, s_playerYPos, 0);
		SERIALIZE(ObjState_InitVersion, s_camOffset, defV3);
		SERIALIZE(ObjState_InitVersion, s_camOffsetPitch, 0);
		SERIALIZE(ObjState_InitVersion, s_camOffsetYaw, 0);
		SERIALIZE(ObjState_InitVersion, s_camOffsetRoll, 0);
		SERIALIZE(ObjState_InitVersion, s_playerYaw, 0);

		SERIALIZE(ObjState_InitVersion, s_itemUnknown1, 0);
		SERIALIZE(ObjState_InitVersion, s_itemUnknown2, 0);

		SERIALIZE(ObjState_InitVersion, s_playerHeight, 0);
		SERIALIZE(ObjState_InitVersion, s_playerRun, 0);
		SERIALIZE(ObjState_InitVersion, s_jumpScale, 0);
		SERIALIZE(ObjState_InitVersion, s_playerSlow, 0);
		SERIALIZE(ObjState_InitVersion, s_onMovingSurface, 0);

		// Handle the player saving as they die and then reloading...
		if (serialization_getMode() == SMODE_READ)
		{
			fixed16_16 health = intToFixed16(s_playerInfo.health);
			health += s_playerInfo.healthFract;

			if (health < ONE_16)
			{
				s_playerInfo.healthFract = 0;
				// We could just set the health to 0 here...
				s_playerInfo.health = pickup_addToValue(0, 0, s_healthMax);
				if (s_gasSectorTask)
				{
					task_free(s_gasSectorTask);
				}
				s_gasSectorTask = nullptr;
				s_playerDying = JTRUE;
				s_reviveTick = s_curTick + 436;
			}
		}
	}

	// TFE Specific
	void tfe_showSecretFoundMsg()
	{
		TFE_Settings_Game* settings = TFE_Settings::getGameSettings();
		if (settings->df_showSecretFoundMsg)
		{
			const char* msg = TFE_System::getMessage(TFE_MSG_SECRET);
			if (msg)
			{
				hud_sendTextMessage(msg, 1);	// high priority.
			}
		}
	}
}  // TFE_DarkForces
