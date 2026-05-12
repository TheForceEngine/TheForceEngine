#include "projectile.h"
#include "animLogic.h"
#include "random.h"
#include "player.h"
#include "sound.h"
#include <cstring>
#include <map>
#include <TFE_Asset/modelAsset_jedi.h>
#include <TFE_Asset/spriteAsset_Jedi.h>
#include <TFE_Jedi/Collision/collision.h>
#include <TFE_Jedi/InfSystem/infSystem.h>
#include <TFE_Jedi/InfSystem/message.h>
#include <TFE_Jedi/Task/task.h>
#include <TFE_Jedi/Level/robject.h>
#include <TFE_Jedi/Level/rwall.h>
#include <TFE_Jedi/Memory/allocator.h>
#include <TFE_Jedi/Serialization/serialization.h>
#include <TFE_ExternalData/weaponExternal.h>
#include <TFE_ExternalData/customProjectile.h>

using namespace TFE_Jedi;

namespace TFE_DarkForces
{
	//////////////////////////////////////////////////////////////
	// Structures and Constants
	//////////////////////////////////////////////////////////////
	enum ProjectileConstants
	{
		PROJ_GRAVITY_ACCEL         = FIXED(120), // Gravity acceleration = 120.0 units / s^2
		PL_LANDMINE_TRIGGER_RADIUS = FIXED(20),	 // Pre-placed landmine trigger radius.
		WP_LANDMINE_TRIGGER_RADIUS = FIXED(15),	 // Player Weapon landmine trigger radius.
		CAMERA_SOUND_RANGE         = FIXED(15),  // At this distance, the projectile plays its "near camera" sound.
		HOMING_MISSILE_EXPLODE_RNG = FIXED(8),	 // The approximate distance when a homing missile might explode early.
		MAX_HOMING_ANGULAR_VEL     = 9102,       // Maximum angle change per second: ~200 degrees / second.
		HOMING_ANGULAR_ACCEL       = 1820,       // Rate at which homing speed increases in angular units / second: ~40 degrees / second.
		HOMING_PITCH_ZERO_MIN      = 2047,       // Range of pitch angles that are mapped to zero degrees.
		HOMING_PITCH_ZERO_MAX      = 14335,      // Range of pitch angles that are mapped to zero degrees.
		PROJ_PATH_MAX_SECTORS      = 16,		 // The maximum number of sectors in a projectile path (i.e. how many sectors can a projectile cross in a single frame).
	};

	//////////////////////////////////////////////////////////////
	// Internal State
	//////////////////////////////////////////////////////////////
	static Allocator* s_projectiles = nullptr;

	// Arrays to hold projectile assets
	static JediModel* s_projectileModels[PROJ_COUNT];
	static WaxFrame*  s_projectileFrames[PROJ_COUNT];
	static Wax*		  s_projectileWaxes[PROJ_COUNT];
	
	static SoundSourceId s_projectileCameraSnd[PROJ_COUNT];
	static SoundSourceId s_projectileReflectSnd[PROJ_COUNT];
	static SoundSourceId s_projectileFlightSnd[PROJ_COUNT];

	// Assets for custom projectiles
	static std::map<u32, JediModel*> s_customProjectileModels;
	static std::map<u32, WaxFrame*> s_customProjectileFrames;
	static std::map<u32, Wax*> s_customProjectileWaxes;

	static std::vector<SoundSourceId> s_customProjectileCameraSnd;
	static std::vector<SoundSourceId> s_customProjectileReflectSnd;
	static std::vector<SoundSourceId> s_customProjectileFlightSnd;

	static SoundSourceId s_landMineTriggerSnd     = NULL_SOUND;

	static RSector*   s_projPath[PROJ_PATH_MAX_SECTORS];
	static RSector*   s_projSector;
	static s32        s_projIter;
	static fixed16_16 s_projNextPosX;
	static fixed16_16 s_projNextPosY;
	static fixed16_16 s_projNextPosZ;
	static JBool      s_hitWater;
	static RWall*     s_hitWall;

	static s32        s_projGravityAccel = PROJ_GRAVITY_ACCEL;		// TFE
		
	// Projectile specific collisions
	static RSector*   s_colObjSector;
	static SecObject* s_colHitObj;
	static fixed16_16 s_colObjAdjX;
	static fixed16_16 s_colObjAdjY;
	static fixed16_16 s_colObjAdjZ;

	// Task
	static Task* s_projectileTask = nullptr;

	WallHitFlag s_hitWallFlag = WH_IGNORE;
	angle14_32 s_projReflectOverrideYaw = 0;

	//////////////////////////////////////////////////////////////
	// Forward Declarations
	//////////////////////////////////////////////////////////////
	void projectileLogicCleanupFunc(Logic* logic);
	void proj_setTransform(ProjectileLogic* logic, angle14_32 pitch, angle14_32 yaw);
	JBool proj_move(ProjectileLogic* logic);
	JBool proj_getHitObj(ProjectileLogic* logic);
		
	ProjectileHitType stdProjectileUpdateFunc(ProjectileLogic* logic);
	ProjectileHitType landMineUpdateFunc(ProjectileLogic* logic);
	ProjectileHitType arcingProjectileUpdateFunc(ProjectileLogic* logic);
	ProjectileHitType homingMissileProjectileUpdateFunc(ProjectileLogic* logic);

	ProjectileFunc getUpdateFunc(const char* type);
	   
	void projectileTaskFunc(MessageType msg);

	static ProjectileFunc c_projUpdateFunc[] =
	{
		stdProjectileUpdateFunc,
		landMineUpdateFunc,
		arcingProjectileUpdateFunc,
		homingMissileProjectileUpdateFunc
	};

	//////////////////////////////////////////////////////////////
	// API Implementation
	//////////////////////////////////////////////////////////////
	void projectile_startup()
	{
		// TFE: Assets are now listed externally.
		TFE_ExternalData::ExternalProjectile* externalProjectiles = TFE_ExternalData::getExternalProjectiles();
		for (u32 p = 0; p < PROJ_COUNT; p++)
		{
			// Frame
			if (strcasecmp(externalProjectiles[p].assetType, "frame") == 0)
			{
				s_projectileFrames[p] = TFE_Sprite_Jedi::getFrame(externalProjectiles[p].asset, POOL_GAME);
			}

			// Wax
			if (strcasecmp(externalProjectiles[p].assetType, "sprite") == 0)
			{
				s_projectileWaxes[p] = TFE_Sprite_Jedi::getWax(externalProjectiles[p].asset, POOL_GAME);
			}

			// 3D model
			if (strcasecmp(externalProjectiles[p].assetType, "3d") == 0)
			{
				s_projectileModels[p] = TFE_Model_Jedi::get(externalProjectiles[p].asset, POOL_GAME);
			}

			// Sounds
			s_projectileCameraSnd[p] = sound_load(externalProjectiles[p].cameraPassSound, SOUND_PRIORITY_LOW3);
			s_projectileReflectSnd[p] = sound_load(externalProjectiles[p].reflectSound, SOUND_PRIORITY_LOW1);
			s_projectileFlightSnd[p] = sound_load(externalProjectiles[p].flightSound, SOUND_PRIORITY_LOW3);
		}

		// This is the original list of hardcoded assets.
		// s_boltModel         = TFE_Model_Jedi::get("wrbolt.3do", POOL_GAME);
		// s_thermalDetProj    = TFE_Sprite_Jedi::getFrame("wdet.fme", POOL_GAME);
		// s_repeaterProj      = TFE_Sprite_Jedi::getFrame("bullet.fme", POOL_GAME);
		// s_plasmaProj        = TFE_Sprite_Jedi::getWax("wemiss.wax", POOL_GAME);
		// s_mortarProj        = TFE_Sprite_Jedi::getWax("wshell.wax", POOL_GAME);
		// s_landmineWpnFrame  = TFE_Sprite_Jedi::getFrame("wmine.fme", POOL_GAME);
		// s_cannonProj        = TFE_Sprite_Jedi::getWax("wplasma.wax", POOL_GAME);
		// s_missileProj       = TFE_Sprite_Jedi::getWax("wmsl.wax", POOL_GAME);
		// s_landmineFrame     = TFE_Sprite_Jedi::getFrame("wlmine.fme", POOL_GAME);
		// s_greenBoltModel    = TFE_Model_Jedi::get("wgbolt.3do", POOL_GAME);
		// s_probeProj         = TFE_Sprite_Jedi::getWax("widball.wax", POOL_GAME);
		// s_homingMissileProj = TFE_Sprite_Jedi::getWax("wdt3msl.wax", POOL_GAME);
		// s_bobafetBall       = TFE_Sprite_Jedi::getWax("bobaball.wax", POOL_GAME);
		// 
		// s_stdProjReflectSnd      = sound_load("boltref1.voc", SOUND_PRIORITY_LOW1);
		// s_thermalDetReflectSnd   = sound_load("thermal1.voc", SOUND_PRIORITY_LOW1);
		// s_plasmaReflectSnd       = sound_load("bigrefl1.voc", SOUND_PRIORITY_LOW1);
		// s_stdProjCameraSnd       = sound_load("lasrby.voc",   SOUND_PRIORITY_LOW3);
		// s_plasmaCameraSnd        = sound_load("emisby.voc",   SOUND_PRIORITY_LOW3);
		// s_missileLoopingSnd      = sound_load("rocket-1.voc", SOUND_PRIORITY_LOW3);
		// s_homingMissileFlightSnd = sound_load("tracker.voc",  SOUND_PRIORITY_LOW3);
		// s_bobaBallCameraSnd      = sound_load("fireball.voc", SOUND_PRIORITY_LOW3);
		s_landMineTriggerSnd     = sound_load("beep-10.voc",  SOUND_PRIORITY_HIGH3);		// still used!

		// Custom projectile assets
		s_customProjectileModels.clear();
		s_customProjectileFrames.clear();
		s_customProjectileWaxes.clear();
		s_customProjectileCameraSnd.clear();
		s_customProjectileReflectSnd.clear();
		s_customProjectileFlightSnd.clear();

		std::vector<TFE_ExternalData::ExternalProjectile>* customProjectiles = TFE_ExternalData::getCustomProjectiles();
		for (u32 p = 0; p < customProjectiles->size(); p++)
		{
			TFE_ExternalData::ExternalProjectile proj = customProjectiles->at(p);
			
			// Frame
			if (strcasecmp(proj.assetType, "frame") == 0)
			{
				WaxFrame* frame = TFE_Sprite_Jedi::getFrame(proj.asset, POOL_GAME);
				s_customProjectileFrames.insert({ p, frame });
			}

			// Wax
			if (strcasecmp(proj.assetType, "sprite") == 0)
			{
				Wax* wax = TFE_Sprite_Jedi::getWax(proj.asset, POOL_GAME);
				s_customProjectileWaxes.insert({p, wax});
			}

			// 3D model
			if (strcasecmp(proj.assetType, "3d") == 0)
			{
				JediModel* model = TFE_Model_Jedi::get(proj.asset, POOL_GAME);
				s_customProjectileModels.insert({ p, model });
			}

			s_customProjectileCameraSnd.push_back(sound_load(proj.cameraPassSound, SOUND_PRIORITY_LOW3));
			s_customProjectileReflectSnd.push_back(sound_load(proj.reflectSound, SOUND_PRIORITY_LOW1));
			s_customProjectileFlightSnd.push_back(sound_load(proj.flightSound, SOUND_PRIORITY_LOW3));
		}
	}

	void projectile_clearState()
	{
		s_projectiles = nullptr;
		s_projectileTask = nullptr;
		s_projReflectOverrideYaw = 0;
	}

	void projectile_createTask()
	{
		projectile_clearState();
		s_projectiles = allocator_create(sizeof(ProjectileLogic));
		s_projectileTask = createSubTask("projectiles", projectileTaskFunc);
	}

	// TFE: Set the Projectile object assets from external data. These were hardcoded in vanilla DF.
	void setProjectileObjAssets(SecObject*& projObj, ProjectileType type, TFE_ExternalData::ExternalProjectile* externalProjectile)
	{
		if (strcasecmp(externalProjectile->assetType, "spirit") == 0)
		{
			spirit_setData(projObj);
		}
		else if (strcasecmp(externalProjectile->assetType, "frame") == 0)
		{
			if (s_projectileFrames[type])
			{
				frame_setData(projObj, s_projectileFrames[type]);
			}
		}
		else if (strcasecmp(externalProjectile->assetType, "sprite") == 0)
		{
			if (s_projectileWaxes[type])
			{
				sprite_setData(projObj, s_projectileWaxes[type]);
				obj_setSpriteAnim(projObj);		// Setup the looping wax animation.
			}
		}
		else if (strcasecmp(externalProjectile->assetType, "3d") == 0)
		{
			if (s_projectileModels[type])
			{
				obj3d_setData(projObj, s_projectileModels[type]);
			}
		}
		else
		{
			spirit_setData(projObj);	// default to spirit
		}
	}
	
	// Set custom projectile object assets
	void setCustomProjectileObjAssets(SecObject*& projObj, u32 index, TFE_ExternalData::ExternalProjectile* customProjectile)
	{
		if (strcasecmp(customProjectile->assetType, "spirit") == 0)
		{
			spirit_setData(projObj);
		}
		else if (strcasecmp(customProjectile->assetType, "frame") == 0)
		{
			WaxFrame* frame = s_customProjectileFrames.at(index);
			if (frame)
			{
				frame_setData(projObj, frame);
			}
		}
		else if (strcasecmp(customProjectile->assetType, "sprite") == 0)
		{
			Wax* wax = s_customProjectileWaxes.at(index);
			if (wax)
			{
				sprite_setData(projObj, wax);
				obj_setSpriteAnim(projObj);		// Setup the looping wax animation.
			}
		}
		else if (strcasecmp(customProjectile->assetType, "3d") == 0)
		{
			JediModel* model = s_customProjectileModels.at(index);
			if (model)
			{
				obj3d_setData(projObj, model);
			}
		}
		else
		{
			spirit_setData(projObj);	// default to spirit
		}
	}

	// TFE: Set Projectile object data from external data
	void setProjectileObjData(SecObject*& projObj, TFE_ExternalData::ExternalProjectile* externalProjectile)
	{
		if (externalProjectile->fullBright)
		{
			projObj->flags |= OBJ_FLAG_FULLBRIGHT;
		}

		if (externalProjectile->autoAim)
		{
			projObj->flags |= OBJ_FLAG_AIM;		// this is only meaningful for homing missiles, which can be shot down
		}

		if (externalProjectile->movable)
		{
			projObj->flags |= OBJ_FLAG_MOVABLE;		// thermal detonators & mines will be moved by elevators
		}

		if (externalProjectile->zeroWidth)
		{
			projObj->worldWidth = 0;
		}
	}

	// TFE: Set the Projectile logic from external data. These were hardcoded in vanilla DF.
	void setProjectileLogic(ProjectileLogic*& projLogic, ProjectileType type, TFE_ExternalData::ExternalProjectile* externalProjectile)
	{
		projLogic->type = type;
		projLogic->updateFunc = getUpdateFunc(externalProjectile->updateFunc);
		projLogic->dmg = FIXED(externalProjectile->damage);
		projLogic->falloffAmt = externalProjectile->falloffAmount;
		projLogic->nextFalloffTick = s_curTick + externalProjectile->nextFalloffTick;
		projLogic->dmgFalloffDelta = externalProjectile->damageFalloffDelta;
		projLogic->minDmg = FIXED(externalProjectile->minDamage);

		projLogic->projForce = externalProjectile->force;
		projLogic->speed = FIXED(externalProjectile->speed);
		projLogic->horzBounciness = externalProjectile->horzBounciness;
		projLogic->vertBounciness = externalProjectile->vertBounciness;
		projLogic->bounceCnt = externalProjectile->bounceCount;
		projLogic->reflVariation = externalProjectile->reflectVariation;
		projLogic->reflectEffectId = (HitEffectID)externalProjectile->reflectEffectId;
		projLogic->hitEffectId = (HitEffectID)externalProjectile->hitEffectId;;
		projLogic->duration = s_curTick + externalProjectile->duration;
		projLogic->homingAngleSpd = externalProjectile->homingAngularSpeed;	// Starting homing rate

		if (externalProjectile->explodeOnTimeout)
		{
			projLogic->flags |= PROJFLAG_EXPLODE;
		}
	}

	// TFE: Set projectile sounds
	void setProjectileSounds(ProjectileLogic*& projLogic, ProjectileType type)
	{
		projLogic->cameraPassSnd = s_projectileCameraSnd[type];
		projLogic->reflectSnd = s_projectileReflectSnd[type];
		projLogic->flightSndSource = s_projectileFlightSnd[type];
	}

	// Set custom projectile sounds
	void setCustomProjectileSounds(ProjectileLogic*& projLogic, u32 index)
	{
		projLogic->cameraPassSnd = s_customProjectileCameraSnd.at(index);
		projLogic->reflectSnd = s_customProjectileReflectSnd.at(index);
		projLogic->flightSndSource = s_customProjectileFlightSnd.at(index);
	}

	// TFE
	void resetProjectileGravityAccel()
	{
		s_projGravityAccel = PROJ_GRAVITY_ACCEL;
	}
	
	// TFE
	void setProjectileGravityAccel(s32 accel)
	{
		s_projGravityAccel = accel;
	}

	// TFE
	s32 getProjectileGravityAccel()
	{
		return s_projGravityAccel;
	}

	Logic* createProjectile(ProjectileType type, RSector* sector, fixed16_16 x, fixed16_16 y, fixed16_16 z, SecObject* obj)
	{
		ProjectileLogic* projLogic = (ProjectileLogic*)allocator_newItem(s_projectiles);
		if (!projLogic)
			return nullptr;
		SecObject* projObj = allocateObject();
		if (!projObj)
		{
			allocator_deleteItem(s_projectiles, projLogic);
			return nullptr;
		}

		projObj->entityFlags |= ETFLAG_PROJECTILE;
		projObj->flags &= ~OBJ_FLAG_MOVABLE;
		projObj->projectileLogic = projLogic;

		projLogic->prevColObj = nullptr;
		projLogic->excludeObj = nullptr;
		projLogic->reflVariation   = 0;
		projLogic->cameraPassSnd   = NULL_SOUND;
		projLogic->flightSndSource = NULL_SOUND;
		projLogic->flightSndId     = NULL_SOUND;
		projLogic->reflectSnd      = NULL_SOUND;
		projLogic->flags = PROJFLAG_CAMERA_PASS_SOUND;
		projLogic->prevObj = obj;  // 'obj' gets deleted after this function is called so this is likely a bug.
		projLogic->vel.x   = 0;
		projLogic->vel.y   = 0;
		projLogic->vel.z   = 0;
		projLogic->minDmg  = 0;

		obj_addLogic(projObj, (Logic*)projLogic, LOGIC_PROJECTILE, s_projectileTask, projectileLogicCleanupFunc);
		
		TFE_ExternalData::ExternalProjectile* externalProjectiles = TFE_ExternalData::getExternalProjectiles();

		switch (type)
		{
			case PROJ_PUNCH:
			{
				setProjectileObjAssets(projObj, PROJ_PUNCH, &externalProjectiles[type]);
				setProjectileObjData(projObj, &externalProjectiles[type]);
				setProjectileLogic(projLogic, PROJ_PUNCH, &externalProjectiles[type]);
				setProjectileSounds(projLogic, PROJ_PUNCH);
			} break;
			case PROJ_PISTOL_BOLT:
			{
				setProjectileObjAssets(projObj, PROJ_PISTOL_BOLT, &externalProjectiles[type]);
				setProjectileObjData(projObj, &externalProjectiles[type]);
				setProjectileLogic(projLogic, PROJ_PISTOL_BOLT, &externalProjectiles[type]);
				setProjectileSounds(projLogic, PROJ_PISTOL_BOLT);
			} break;
			case PROJ_RIFLE_BOLT:
			{
				setProjectileObjAssets(projObj, PROJ_RIFLE_BOLT, &externalProjectiles[type]);
				setProjectileObjData(projObj, &externalProjectiles[type]);
				setProjectileLogic(projLogic, PROJ_RIFLE_BOLT, &externalProjectiles[type]);
				setProjectileSounds(projLogic, PROJ_RIFLE_BOLT);
			} break;
			case PROJ_THERMAL_DET:
			{
				setProjectileObjAssets(projObj, PROJ_THERMAL_DET, &externalProjectiles[type]);
				setProjectileObjData(projObj, &externalProjectiles[type]);
				setProjectileLogic(projLogic, PROJ_THERMAL_DET, &externalProjectiles[type]);
				setProjectileSounds(projLogic, PROJ_THERMAL_DET);
			} break;
			case PROJ_REPEATER:
			{
				setProjectileObjAssets(projObj, PROJ_REPEATER, &externalProjectiles[type]);
				setProjectileObjData(projObj, &externalProjectiles[type]);
				setProjectileLogic(projLogic, PROJ_REPEATER, &externalProjectiles[type]);
				setProjectileSounds(projLogic, PROJ_REPEATER);

				// dmgFalloffDelta was 19660 in the code; this probably should have been 0.3 seconds, or 43 ticks
			} break;
			case PROJ_PLASMA:
			{
				setProjectileObjAssets(projObj, PROJ_PLASMA, &externalProjectiles[type]);
				setProjectileObjData(projObj, &externalProjectiles[type]);
				setProjectileLogic(projLogic, PROJ_PLASMA, &externalProjectiles[type]);
				setProjectileSounds(projLogic, PROJ_PLASMA);
			} break;
			case PROJ_MORTAR:
			{
				setProjectileObjAssets(projObj, PROJ_MORTAR, &externalProjectiles[type]);
				setProjectileObjData(projObj, &externalProjectiles[type]);
				setProjectileLogic(projLogic, PROJ_MORTAR, &externalProjectiles[type]);
				setProjectileSounds(projLogic, PROJ_MORTAR);
			} break;
			case PROJ_LAND_MINE:
			{
				setProjectileObjAssets(projObj, PROJ_LAND_MINE, &externalProjectiles[type]);
				setProjectileObjData(projObj, &externalProjectiles[type]);
				projObj->entityFlags |= ETFLAG_LANDMINE_WPN;
				setProjectileLogic(projLogic, PROJ_LAND_MINE, &externalProjectiles[type]);
				setProjectileSounds(projLogic, PROJ_LAND_MINE);
			} break;
			case PROJ_LAND_MINE_PROX:
			{
				setProjectileObjAssets(projObj, PROJ_LAND_MINE_PROX, &externalProjectiles[type]);
				setProjectileObjData(projObj, &externalProjectiles[type]);
				projObj->entityFlags |= ETFLAG_LANDMINE_WPN;
				setProjectileLogic(projLogic, PROJ_LAND_MINE_PROX, &externalProjectiles[type]);
				setProjectileSounds(projLogic, PROJ_LAND_MINE_PROX);
			} break;
			case PROJ_LAND_MINE_PLACED:
			{
				setProjectileObjAssets(projObj, PROJ_LAND_MINE_PLACED, &externalProjectiles[type]);
				setProjectileObjData(projObj, &externalProjectiles[type]);
				setProjectileLogic(projLogic, PROJ_LAND_MINE_PLACED, &externalProjectiles[type]);
				setProjectileSounds(projLogic, PROJ_LAND_MINE_PLACED);
			} break;
			case PROJ_CONCUSSION:
			{
				setProjectileObjAssets(projObj, PROJ_CONCUSSION, &externalProjectiles[type]);
				setProjectileObjData(projObj, &externalProjectiles[type]);
				setProjectileLogic(projLogic, PROJ_CONCUSSION, &externalProjectiles[type]);
				setProjectileSounds(projLogic, PROJ_CONCUSSION);
			} break;
			case PROJ_CANNON:
			{
				setProjectileObjAssets(projObj, PROJ_CANNON, &externalProjectiles[type]);
				setProjectileObjData(projObj, &externalProjectiles[type]);
				setProjectileLogic(projLogic, PROJ_CANNON, &externalProjectiles[type]);
				setProjectileSounds(projLogic, PROJ_CANNON);
			} break;
			case PROJ_MISSILE:
			{
				setProjectileObjAssets(projObj, PROJ_MISSILE, &externalProjectiles[type]);
				setProjectileObjData(projObj, &externalProjectiles[type]);
				setProjectileLogic(projLogic, PROJ_MISSILE, &externalProjectiles[type]);
				setProjectileSounds(projLogic, PROJ_MISSILE);
			} break;
			case PROJ_TURRET_BOLT:
			{
				setProjectileObjAssets(projObj, PROJ_TURRET_BOLT, &externalProjectiles[type]);
				setProjectileObjData(projObj, &externalProjectiles[type]);
				setProjectileLogic(projLogic, PROJ_TURRET_BOLT, &externalProjectiles[type]);
				setProjectileSounds(projLogic, PROJ_TURRET_BOLT);
			} break;
			case PROJ_REMOTE_BOLT:
			{
				setProjectileObjAssets(projObj, PROJ_REMOTE_BOLT, &externalProjectiles[type]);
				setProjectileObjData(projObj, &externalProjectiles[type]);
				setProjectileLogic(projLogic, PROJ_REMOTE_BOLT, &externalProjectiles[type]);
				setProjectileSounds(projLogic, PROJ_REMOTE_BOLT);
			} break;
			case PROJ_EXP_BARREL:
			{
				setProjectileObjAssets(projObj, PROJ_EXP_BARREL, &externalProjectiles[type]);
				setProjectileObjData(projObj, &externalProjectiles[type]);
				setProjectileLogic(projLogic, PROJ_EXP_BARREL, &externalProjectiles[type]);
				setProjectileSounds(projLogic, PROJ_EXP_BARREL);
			} break;
			case PROJ_HOMING_MISSILE:
			{
				setProjectileObjAssets(projObj, PROJ_HOMING_MISSILE, &externalProjectiles[type]);
				setProjectileObjData(projObj, &externalProjectiles[type]);
				setProjectileLogic(projLogic, PROJ_HOMING_MISSILE, &externalProjectiles[type]);
				setProjectileSounds(projLogic, PROJ_HOMING_MISSILE);

				// projLogic->speed = FIXED(58) / 2;
				// speed will be set to 29 externally; the value is correct according to the code, but they are slower in DOS.

			} break;
			case PROJ_PROBE_PROJ:
			{
				setProjectileObjAssets(projObj, PROJ_PROBE_PROJ, &externalProjectiles[type]);
				setProjectileObjData(projObj, &externalProjectiles[type]);
				setProjectileLogic(projLogic, PROJ_PROBE_PROJ, &externalProjectiles[type]);
				setProjectileSounds(projLogic, PROJ_PROBE_PROJ);
			} break;
			case PROJ_BOBAFET_BALL:
			{
				setProjectileObjAssets(projObj, PROJ_BOBAFET_BALL, &externalProjectiles[type]);
				setProjectileObjData(projObj, &externalProjectiles[type]);
				setProjectileLogic(projLogic, PROJ_BOBAFET_BALL, &externalProjectiles[type]);
				setProjectileSounds(projLogic, PROJ_BOBAFET_BALL);
			} break;
		}

		// Custom projectiles - these are numbered starting at 100
		if (type >= TFE_ExternalData::CUSTOM_PROJ_STARTNUM)
		{
			u32 index = type - TFE_ExternalData::CUSTOM_PROJ_STARTNUM;
			std::vector<TFE_ExternalData::ExternalProjectile>* customProjs = TFE_ExternalData::getCustomProjectiles();

			if (index < customProjs->size())
			{
				TFE_ExternalData::ExternalProjectile* customProj = &customProjs->at(index);
				setCustomProjectileObjAssets(projObj, index, customProj);
				setProjectileObjData(projObj, customProj);
				setProjectileLogic(projLogic, type, customProj);
				setCustomProjectileSounds(projLogic, index);
			}
		}

		projLogic->col_speed = projLogic->speed;

		projObj->posWS.x = x;
		projObj->posWS.y = y;
		projObj->posWS.z = z;
		sector_addObject(sector, projObj);
		task_makeActive(s_projectileTask);

		obj_addToRefList(projObj, ObjRefType_Projectile);	// scripting
		return (Logic*)projLogic;
	}
		
	void projectileTaskFunc(MessageType msg)
	{
		struct LocalContext
		{
			ProjectileLogic* projLogic;
		};
		task_begin_ctx;

		while (msg != MSG_FREE_TASK)
		{
			taskCtx->projLogic = nullptr;
			task_yield(TASK_NO_DELAY);

			// Keep looping until a projectile becomes active.
			// Note: this task will need to be made active by something else to wake up.
			while (!taskCtx->projLogic)
			{
				taskCtx->projLogic = (ProjectileLogic*)allocator_getHead(s_projectiles);
				if (!taskCtx->projLogic)
				{
					task_yield(TASK_SLEEP);
				}
			}

			taskCtx->projLogic = (ProjectileLogic*)allocator_getHead(s_projectiles);
			while (taskCtx->projLogic)
			{
				ProjectileLogic* projLogic = taskCtx->projLogic;

				SecObject* obj = projLogic->logic.obj;
				ProjectileHitType projHitType = PHIT_NONE;
				Tick curTick = s_curTick;

				// The projectile is still active.
				if (curTick < projLogic->duration)
				{
					// Handle damage falloff.
					if (projLogic->falloffAmt && curTick > projLogic->nextFalloffTick)
					{
						projLogic->dmg -= projLogic->falloffAmt;
						if (projLogic->dmg < projLogic->minDmg)
						{
							projLogic->nextFalloffTick = 0xffffffff;	// This basically sets the falloff to "infinity" so it never enters this block again.
							projLogic->dmg = projLogic->minDmg;
						}
						else
						{
							projLogic->nextFalloffTick += projLogic->dmgFalloffDelta;
						}
					}
				}
				else  // The projectile has reached the end of its life.
				{
					const ProjectileType type = projLogic->type;
					// Note that proximity landmines that have reached end of life don't explode right away, but instead check for valid object proximity.
					if (type == PROJ_LAND_MINE_PLACED)	// Pre-placed landmines.
					{
						const fixed16_16 approxDist = distApprox(s_playerObject->posWS.x, s_playerObject->posWS.z, obj->posWS.x, obj->posWS.z);
						if (approxDist <= PL_LANDMINE_TRIGGER_RADIUS)
						{
							if (s_playerObject->posWS.y >= obj->posWS.y - PL_LANDMINE_TRIGGER_RADIUS && s_playerObject->posWS.y <= obj->posWS.y + PL_LANDMINE_TRIGGER_RADIUS &&
								collision_isAnyObjectInRange(obj->sector, PL_LANDMINE_TRIGGER_RADIUS, obj->posWS, obj, ETFLAG_PLAYER))
							{
								projLogic->type = PROJ_LAND_MINE;
								projLogic->duration = s_curTick + 87;  // The landmine will explode in 0.6 seconds.
								sound_playCued(s_landMineTriggerSnd, obj->posWS);
							}
						}
					}
					else if (type == PROJ_LAND_MINE_PROX)	// Player placed proximity landmine (secondary fire).
					{
						if (collision_isAnyObjectInRange(obj->sector, WP_LANDMINE_TRIGGER_RADIUS, obj->posWS, obj, ETFLAG_PLAYER | ETFLAG_AI_ACTOR))
						{
							projLogic->type = PROJ_LAND_MINE;
							projLogic->duration = s_curTick + TICKS_PER_SECOND;	// The landmine will explode in 1 second.
							sound_playCued(s_landMineTriggerSnd, obj->posWS);
						}
					}
					else
					{
						projHitType = PHIT_OUT_OF_RANGE;
					}
				}  // if (curTick < projLogic->duration)

				if (projHitType == PHIT_NONE)
				{
					RSector* sector = obj->sector;
					InfLink* link = (InfLink*)allocator_getHead(sector->infLink);
					if (link && link->type == LTYPE_SECTOR && inf_isOnMovingFloor(obj, link->elev, sector))
					{
						fixed16_16 res;
						vec3_fixed vel;
						inf_getMovingElevatorVelocity(link->elev, &vel, &res);
						projLogic->vel.y = vel.y;
						projLogic->vel.x += vel.x;
						projLogic->vel.z += vel.z;
					}
					if (projLogic->updateFunc)
					{
						projHitType = projLogic->updateFunc(projLogic);
					}
				}

				// Play a looping sound as the projectile travels, this updates its position if already playing.
				if (projLogic->flightSndSource)
				{
					projLogic->flightSndId = sound_maintain(projLogic->flightSndId, projLogic->flightSndSource, obj->posWS);
				}
				// Play a sound as the object passes near the camera.
				if (projLogic->cameraPassSnd && (projLogic->flags & PROJFLAG_CAMERA_PASS_SOUND))
				{
					fixed16_16 dy = TFE_Jedi::abs(obj->posWS.y - s_eyePos.y);
					fixed16_16 approxDist = dy + distApprox(s_eyePos.x, s_eyePos.z, obj->posWS.x, obj->posWS.z);
					if (approxDist < CAMERA_SOUND_RANGE)
					{
						projLogic->flightSndId = sound_playCued(projLogic->cameraPassSnd, obj->posWS);
						projLogic->flags &= ~PROJFLAG_CAMERA_PASS_SOUND;
					}
				}
				else if (projLogic->cameraPassSnd && projLogic->flightSndId)
				{
					sound_adjustCued(projLogic->flightSndId, obj->posWS);
					projLogic->flightSndId = NULL_SOUND;
				}
				// Finally handle the hit itself (this includes going out of range).
				if (projHitType != PHIT_NONE)
				{
					handleProjectileHit(projLogic, projHitType);
				}
				taskCtx->projLogic = (ProjectileLogic*)allocator_getNext(s_projectiles);
			}  // while (taskCtx->projLogic)
		}  // while (id != -1)

		task_end;
	}

	void triggerLandMine(ProjectileLogic* projLogic, Tick delay)
	{
		projLogic->type = PROJ_LAND_MINE;
		projLogic->duration = s_curTick + delay;

		SecObject* obj = projLogic->logic.obj;
		sound_playCued(s_landMineTriggerSnd, obj->posWS);
	}

	//////////////////////////////////////////////////////////////
	// Internal Implementation
	//////////////////////////////////////////////////////////////
	// This function never actually gets called because the Projectile system handles the cleanup.
	void projectileLogicCleanupFunc(Logic* logic)
	{
		allocator_addRef(s_projectiles);
		deleteLogicAndObject(logic);
		allocator_deleteItem(s_projectiles, logic);
		// Bug: allocator_release() is never called
	}

	ProjectileFunc getUpdateFunc(const char* type)
	{
		if (strcasecmp(type, "standard") == 0)
		{
			return stdProjectileUpdateFunc;
		}

		if (strcasecmp(type, "arcing") == 0)
		{
			return arcingProjectileUpdateFunc;
		}

		if (strcasecmp(type, "landmine") == 0)
		{
			return landMineUpdateFunc;
		}

		if (strcasecmp(type, "homing") == 0)
		{
			return homingMissileProjectileUpdateFunc;
		}

		return stdProjectileUpdateFunc;
	}

	// The "standard" update function - projectiles travel in a straight line with a fixed velocity.
	ProjectileHitType stdProjectileUpdateFunc(ProjectileLogic* projLogic)
	{
		// Calculate how much the projectile moves this timeslice.
		const fixed16_16 dt = s_deltaTime;
		projLogic->delta.x = mul16(projLogic->vel.x, dt);
		projLogic->delta.y = mul16(projLogic->vel.y, dt);
		projLogic->delta.z = mul16(projLogic->vel.z, dt);

		return proj_handleMovement(projLogic);
	}

	// Landmines do not move but can fall if off of the ground.
	ProjectileHitType landMineUpdateFunc(ProjectileLogic* projLogic)
	{
		SecObject* obj = projLogic->logic.obj;
		fixed16_16 ceilHeight, floorHeight;
		sector_getObjFloorAndCeilHeight(obj->sector, obj->posWS.y, &floorHeight, &ceilHeight);

		// Gravity - fall if the landmine is above the ground.
		if (obj->posWS.y < floorHeight)
		{
			const fixed16_16 dt = s_deltaTime;
			projLogic->vel.y  += mul16(s_projGravityAccel, dt);
			projLogic->delta.y = mul16(projLogic->vel.y, dt);
			return proj_handleMovement(projLogic);
		}
		return PHIT_NONE;
	}
		
	// Projectiles, such as Thermal Detonators and Mortars fly in an arc.
	// This is done by giving them an initial velocity and then modifying that velocity
	// over time based on gravity.
	ProjectileHitType arcingProjectileUpdateFunc(ProjectileLogic* projLogic)
	{
		const fixed16_16 dt = s_deltaTime;
		// The projectile arcs due to gravity, accel = 120.0 units / s^2
		projLogic->vel.y += mul16(s_projGravityAccel, dt);
		// Get the frame delta from the velocity and delta time.
		projLogic->delta.x = mul16(projLogic->vel.x, dt);
		projLogic->delta.y = mul16(projLogic->vel.y, dt);
		projLogic->delta.z = mul16(projLogic->vel.z, dt);

		// Arcing projectiles cannot move straight up or down.
		const fixed16_16 horzSpeed = vec2Length(projLogic->vel.x, projLogic->vel.z);
		projLogic->dir.y = (horzSpeed) ? div16(projLogic->vel.y, horzSpeed) : 0;

		return proj_handleMovement(projLogic);
	}

	ProjectileHitType homingMissileProjectileUpdateFunc(ProjectileLogic* projLogic)
	{
		SecObject* missileObj = projLogic->logic.obj;
		SecObject* targetObj  = s_playerObject;
		fixed16_16 dt = s_deltaTime;

		angle14_32 homingAngleSpd = projLogic->homingAngleSpd;
		// Target near the head/upper chest.
		fixed16_16 yTarget = targetObj->posWS.y - targetObj->worldHeight + ONE_16;
		angle14_32 homingAngleDelta = mul16(homingAngleSpd, dt);

		// Calculate the horizontal turn amount (change in yaw).
		fixed16_16 dx = targetObj->posWS.x - missileObj->posWS.x;
		fixed16_16 dz = targetObj->posWS.z - missileObj->posWS.z;
		angle14_32 hAngle = vec2ToAngle(dx, dz);
		angle14_32 hAngleDiff = getAngleDifference(missileObj->yaw, hAngle);
		if (hAngleDiff > homingAngleDelta)
		{
			hAngleDiff = homingAngleDelta;
		}
		else if (hAngleDiff < -homingAngleDelta)
		{
			hAngleDiff = -homingAngleDelta;
		}
		// Turn toward the target (yaw).
		missileObj->yaw += hAngleDiff;

		// Calculate the vertical turn amount (change in pitch).
		fixed16_16 cosYaw, sinYaw;
		sinCosFixed(missileObj->yaw, &sinYaw, &cosYaw);

		fixed16_16 hDir = mul16(sinYaw, dx) + mul16(cosYaw, dz);
		fixed16_16 dy = missileObj->posWS.y - yTarget;
		angle14_32 vAngle = vec2ToAngle(dy, hDir);
		if (vAngle > HOMING_PITCH_ZERO_MIN && vAngle < HOMING_PITCH_ZERO_MAX)
		{
			vAngle = 0;
		}
		angle14_32 vAngleDiff = getAngleDifference(missileObj->pitch, vAngle);
		if (vAngleDiff > homingAngleDelta)
		{
			vAngleDiff = homingAngleDelta;
		}
		else if (vAngleDiff < -homingAngleDelta)
		{
			vAngleDiff = -homingAngleDelta;
		}
		// Turn toward the target (pitch).
		missileObj->pitch += vAngleDiff;

		// The missile will continue homing at a faster rate, up until the maximum.
		if (projLogic->homingAngleSpd < MAX_HOMING_ANGULAR_VEL)
		{
			projLogic->homingAngleSpd += mul16(HOMING_ANGULAR_ACCEL, dt);
		}

		// Set the projectile transform based on the new yaw and pitch.
		proj_setTransform(projLogic, missileObj->pitch, missileObj->yaw);

		// Finally calculate the per-frame movement from the velocity computed in proj_setTransform().
		projLogic->delta.x = mul16(projLogic->vel.x, dt);
		projLogic->delta.y = mul16(projLogic->vel.y, dt);
		projLogic->delta.z = mul16(projLogic->vel.z, dt);

		// Then apply movement and handle the 'explode early' special case.
		s_collision_excludeEntityFlags = ETFLAG_PROJECTILE;
		ProjectileHitType hitType = proj_handleMovement(projLogic);
		s_collision_excludeEntityFlags = 0;
		if (hitType == PHIT_NONE)
		{
			// There is a chance that the missile will explode early, once it gets within a certain range.
			fixed16_16 approxDist = distApprox(missileObj->posWS.x, missileObj->posWS.z, targetObj->posWS.x, targetObj->posWS.z);
			if (approxDist < HOMING_MISSILE_EXPLODE_RNG && (s_curTick & 1))
			{
				hitType = PHIT_SOLID;
			}
		}
		return hitType;
	}


	void proj_computeTransform3D(SecObject* obj, fixed16_16 sinPitch, fixed16_16 cosPitch, fixed16_16 sinYaw, fixed16_16 cosYaw, fixed16_16 dt)
	{
		// Projectile size is scaled by the delta-time.
		// But this breaks down if the delta-time is too small.
		// Given the original game had an average frame time of 2 ticks, then the minimum
		// ~ 2 * ONE_SECOND / 145 = 2 * 65536 / 145 = 903. I use 900 because it is a nice round number.
		if (dt < 900) { dt = 900; }

		fixed16_16* transform = obj->transform;
		transform[0] = cosYaw;
		transform[1] = mul16(sinPitch, sinYaw);
		transform[2] = mul16(mul16(cosPitch, sinYaw), dt);

		transform[3] = 0;
		transform[4] = cosPitch;
		transform[5] = -mul16(sinPitch, dt);

		transform[6] = -sinYaw;
		transform[7] = mul16(sinPitch, cosYaw);
		transform[8] = mul16(mul16(cosPitch, cosYaw), dt);
	}

	void proj_setYawPitch(ProjectileLogic* projLogic, fixed16_16 sinPitch, fixed16_16 cosPitch, fixed16_16 sinYaw, fixed16_16 cosYaw)
	{
		projLogic->dir.x = sinYaw;
		projLogic->dir.z = cosYaw;
		projLogic->vel.x = mul16(mul16(sinYaw, projLogic->speed), cosPitch);
		projLogic->vel.z = mul16(mul16(cosYaw, projLogic->speed), cosPitch);
		projLogic->vel.y = -mul16(sinPitch, projLogic->speed);

		// Projectiles cannot point straight up or down.
		const fixed16_16 horzSpeed = vec2Length(projLogic->vel.x, projLogic->vel.z);
		projLogic->dir.y = (horzSpeed) ? div16(projLogic->vel.y, horzSpeed) : 0;

		SecObject* obj = projLogic->logic.obj;
		if (obj->type == OBJ_TYPE_3D)
		{
			proj_computeTransform3D(obj, sinPitch, cosPitch, sinYaw, cosYaw, s_deltaTime);
		}
	}

	void proj_setTransform(ProjectileLogic* projLogic, angle14_32 pitch, angle14_32 yaw)
	{
		SecObject* obj = projLogic->logic.obj;
		obj->roll  = 0;
		obj->pitch = pitch;
		obj->yaw   = yaw;

		fixed16_16 cosYaw, sinYaw;
		fixed16_16 cosPitch, sinPitch;
		sinCosFixed(yaw,   &sinYaw,   &cosYaw);
		sinCosFixed(pitch, &sinPitch, &cosPitch);

		proj_setYawPitch(projLogic, sinPitch, cosPitch, sinYaw, cosYaw);
	}

	// Originally this code was inlined in the final DOS code, but I am splitting it out to reduce redundant code.
	// This code adds some variation to the final yaw and pitch of a projectile after reflection if
	// projLogic->reflVariation is non-zero.
	void handleReflectVariation(ProjectileLogic* projLogic, SecObject* obj)
	{
		if (!projLogic->reflVariation) { return; }

		// Random variation between [-projLogic->reflVariation, projLogic->reflVariation]
		obj->yaw   += random(2 * projLogic->reflVariation) - projLogic->reflVariation;
		obj->pitch += random(2 * projLogic->reflVariation) - projLogic->reflVariation;
	}

	void proj_reflect(SecObject* obj, ProjectileLogic* projLogic)
	{
		angle14_32 angleDiff = getAngleDifference(obj->yaw, s_projReflectOverrideYaw);
		obj->yaw = (angleDiff + s_projReflectOverrideYaw) & ANGLE_MASK;

		if (projLogic->reflVariation)
		{
			obj->yaw += random(projLogic->reflVariation * 2) - projLogic->reflVariation;
			obj->pitch += random(projLogic->reflVariation * 2) - projLogic->reflVariation;
		}
		projLogic->prevColObj = nullptr;
		projLogic->excludeObj = nullptr;
		proj_setTransform(projLogic, obj->pitch, obj->yaw);
		sound_playCued(projLogic->reflectSnd, obj->posWS);

		projLogic->flags |= PROJFLAG_CAMERA_PASS_SOUND;
	}

	// Handle projectile movement.
	// Returns 0 if it moved without hitting anything.
	ProjectileHitType proj_handleMovement(ProjectileLogic* projLogic)
	{
		SecObject* obj = projLogic->logic.obj;
		JBool envHit = proj_move(projLogic);
		JBool objHit = proj_getHitObj(projLogic);
		if (objHit)
		{
			obj->posWS.y = s_colObjAdjY;
			fixed16_16 dz = s_colObjAdjZ - obj->posWS.z;
			fixed16_16 dx = s_colObjAdjX - obj->posWS.x;
			collision_moveObj(obj, dx, dz);

			if (projLogic->bounceCnt == -1)
			{
				projLogic->speed = mul16(projLogic->speed, projLogic->horzBounciness);
				angle14_32 offsetYaw = obj->yaw + 4096;
				// getAngleDifference() will always return 4096, then we add yaw + 4096 = yaw + 8192 = 180 degrees.
				// This could have been accomplished with: obj->yaw = (obj->yaw + 8192) & 16383
				obj->yaw = (getAngleDifference(obj->yaw, offsetYaw) + offsetYaw) & 16383;
				handleReflectVariation(projLogic, obj);

				projLogic->prevColObj = nullptr;
				projLogic->excludeObj = nullptr;
				proj_setTransform(projLogic, obj->pitch, obj->yaw);
				sound_playCued(projLogic->reflectSnd, obj->posWS);

				projLogic->flags |= PROJFLAG_CAMERA_PASS_SOUND;
				projLogic->delta.x = mul16(HALF_16, projLogic->dir.x);
				projLogic->delta.z = mul16(HALF_16, projLogic->dir.z);
				collision_moveObj(obj, projLogic->delta.x, projLogic->delta.z);

				projLogic->delta.x = 0;
				projLogic->delta.y = 0;
				projLogic->delta.z = 0;
			}
			else
			{
				if (projLogic->type == PROJ_PUNCH)
				{
					vec3_fixed effectPos = { s_colObjAdjX, s_colObjAdjY, s_colObjAdjZ };
					spawnHitEffect(projLogic->hitEffectId, obj->sector, effectPos, projLogic->excludeObj);
				}
				s_hitWallFlag = WH_STDEXP;
				if (s_colHitObj->entityFlags & ETFLAG_PROJECTILE)
				{
					s_hitWallFlag = WH_IGNORE;
					Logic** objLogicPtr = (Logic**)allocator_getHead((Allocator*)s_colHitObj->logic);
					if (objLogicPtr)
					{
						ProjectileLogic* objLogic = (ProjectileLogic*)*objLogicPtr;
						// PROJ_HOMING_MISSILE can be destroyed by shooting it with a different type of projectile.
						if (projLogic->type != objLogic->type && objLogic->type == PROJ_HOMING_MISSILE)
						{
							s_hitWallFlag = WH_STDEXP;
							objLogic->duration = 0;
						}
					}
				}
				else if (projLogic->dmg)
				{
					s_msgEntity = projLogic;
					message_sendToObj(s_colHitObj, MSG_DAMAGE, nullptr);
				}

				switch (s_hitWallFlag)
				{
					case WH_IGNORE:
					{
						// Do nothing, this will proceed to the next section.
					} break;
					case WH_NOEXP:
					{
						return PHIT_SKY;
					} break;
					case WH_STDEXP:
					case WH_CUSTEXP:	// CUSTEXP is not actually used but is included for completeness.
					{
						return PHIT_SOLID;
					} break;
					case WH_BOUNCE:
					{
						proj_reflect(obj, projLogic);
						return PHIT_NONE;
					} break;
					default:
					{
						return PHIT_SOLID;
					}
				}
			}
		}

		if (envHit)
		{
			obj->posWS.y = s_projNextPosY;
			fixed16_16 dz = s_projNextPosZ - obj->posWS.z;
			fixed16_16 dx = s_projNextPosX - obj->posWS.x;
			RSector* newSector = collision_moveObj(obj, dx, dz);

			if (s_hitWall)
			{
				s_hitWallFlag = (s_projSector->flags1 & SEC_FLAGS1_NOWALL_DRAW) ? WH_NOEXP : WH_STDEXP;
				
				vec2_fixed* w0 = s_hitWall->w0;
				fixed16_16 paramPos = vec2Length(s_projNextPosX - w0->x, s_projNextPosZ - w0->z);
				inf_wallAndMirrorSendMessageAtPos(s_hitWall, projLogic->logic.obj, INF_EVENT_SHOOT_LINE, paramPos, s_projNextPosY);

				switch (s_hitWallFlag)
				{
					case WH_IGNORE:
					case WH_NOEXP:
					{
						return PHIT_SKY;
					} break;
					case WH_STDEXP:
					{
						return PHIT_SOLID;
					} break;
					case WH_BOUNCE:
					{
						proj_reflect(obj, projLogic);
						return PHIT_NONE;
					} break;
				}
			}
			return s_hitWater ? PHIT_WATER : PHIT_SOLID;
		}

		// Hit nothing.
		obj->posWS.y += projLogic->delta.y;
		collision_moveObj(obj, projLogic->delta.x, projLogic->delta.z);
		return PHIT_NONE;
	}

	// Return JTRUE if something was hit during movement.
	JBool proj_move(ProjectileLogic* projLogic)
	{
		SecObject* obj = projLogic->logic.obj;
		RSector* sector = obj->sector;

		s_hitWall = nullptr;
		s_hitWater = JFALSE;
		s_projSector = sector;
		s_projPath[0] = sector;
		s_projIter = 1;
		s_projNextPosX = obj->posWS.x + projLogic->delta.x;
		s_projNextPosY = obj->posWS.y + projLogic->delta.y;
		s_projNextPosZ = obj->posWS.z + projLogic->delta.z;

		fixed16_16 y0CeilHeight;
		fixed16_16 y0FloorHeight;
		bool hitFloor = false, hitCeil = false;

		RWall* wall = collision_wallCollisionFromPath(s_projSector, obj->posWS.x, obj->posWS.z, s_projNextPosX, s_projNextPosZ);
		while (wall)
		{
			fixed16_16 bot, top;
			if (wall->nextSector && !(wall->flags3 & WF3_CANNOT_FIRE_THROUGH))
			{
				// Determine the opening in the wall.
				wall_getOpeningHeightRange(wall, &top, &bot);

				// The check if the projectile trivially passes through.
				fixed16_16 y0 = s_projNextPosY;
				fixed16_16 y1 = projLogic->type == PROJ_THERMAL_DET ? s_projNextPosY - obj->worldHeight : s_projNextPosY;
				if (obj->posWS.y <= bot && obj->posWS.y >= top && y0 <= bot && y1 >= top)
				{
					s_projSector = wall->nextSector;
					s_projPath[s_projIter] = s_projSector;
					s_projIter++;
					assert(s_projIter <= PROJ_PATH_MAX_SECTORS);
					wall = collision_pathWallCollision(s_projSector);
					continue;
				}
			}
			else
			{
				// There is no opening in the wall.
				bot = -SEC_SKY_HEIGHT;
				top =  SEC_SKY_HEIGHT;
			}

			collision_getHitPoint(&s_projNextPosX, &s_projNextPosZ);
			fixed16_16 len = vec2Length(s_projNextPosX - obj->posWS.x, s_projNextPosZ - obj->posWS.z);
			fixed16_16 yDelta = mul16(len, projLogic->dir.y);

			fixed16_16 y0 = obj->posWS.y;
			fixed16_16 y1 = obj->posWS.y + yDelta;
			sector_getObjFloorAndCeilHeight(s_projSector, y0, &y0FloorHeight, &y0CeilHeight);
			// In the case of water, the real floor height is the actual floor.
			if (s_projSector->secHeight > 0)
			{
				y0FloorHeight = s_projSector->floorHeight;
			}

			fixed16_16 nextY = y1;
			if (y1 > y0FloorHeight)
			{
				nextY = y0FloorHeight;
			}
			else if (y1 < y0CeilHeight)
			{
				nextY = y0CeilHeight;
			}

			fixed16_16 nY0 = nextY;
			fixed16_16 nY1 = projLogic->type == PROJ_THERMAL_DET ? nextY - obj->worldHeight : nextY;
			if (nY0 <= bot && nY1 >= top)
			{
				if (y1 >= y0FloorHeight)
				{
					hitFloor = true;
					break;
				}
				if (y1 <= y0CeilHeight)
				{
					hitCeil = true;
					break;
				}
			}
			else  // Wall Hit!
			{
				fixed16_16 offset = ONE_16 + HALF_16;
				fixed16_16 offsetX = mul16(offset, projLogic->dir.x);
				fixed16_16 offsetZ = mul16(offset, projLogic->dir.z);

				s_projNextPosY = nextY;
				s_projNextPosX -= offsetX;
				s_projNextPosZ -= offsetZ;

				projLogic->delta.x = s_projNextPosX - obj->posWS.x;
				projLogic->delta.y = s_projNextPosY - obj->posWS.y;
				projLogic->delta.z = s_projNextPosZ - obj->posWS.z;

				if (projLogic->bounceCnt == -1)
				{
					if (s_projNextPosY < s_projSector->ceilingHeight)
					{
						s_hitWall = wall;
						s_hitWallFlag = WH_NOEXP;
						return JTRUE;
					}
					projLogic->speed = mul16(projLogic->speed, projLogic->horzBounciness);
					obj->pitch = (getAngleDifference(obj->pitch, 0) & 16383);
					obj->yaw = (getAngleDifference(obj->yaw, wall->angle) + wall->angle) & 16383;
					handleReflectVariation(projLogic, obj);

					projLogic->prevColObj = nullptr;
					projLogic->excludeObj = nullptr;
					proj_setTransform(projLogic, obj->pitch, obj->yaw);
					sound_playCued(projLogic->reflectSnd, obj->posWS);

					projLogic->flags |= PROJFLAG_CAMERA_PASS_SOUND;
					obj->posWS.y = s_projNextPosY;
					collision_moveObj(obj, projLogic->delta.x, projLogic->delta.z);
					s_projSector = obj->sector;

					projLogic->delta.x = 0;
					projLogic->delta.y = 0;
					projLogic->delta.z = 0;

					return JFALSE;
				}
				else if (s_projSector->flags1 & SEC_FLAGS1_MAG_SEAL)
				{
					if (s_projNextPosY < s_projSector->ceilingHeight)
					{
						s_hitWall = wall;
						s_hitWallFlag = WH_NOEXP;
						return JTRUE;
					}
					s32 count = projLogic->bounceCnt;
					projLogic->bounceCnt--;
					if (count)
					{
						vec3_fixed effectPos = { s_projNextPosX, s_projNextPosY, s_projNextPosZ };
						spawnHitEffect(projLogic->reflectEffectId, s_projSector, effectPos, projLogic->excludeObj);

						SecObject* obj = projLogic->logic.obj;
						obj->yaw = (getAngleDifference(obj->yaw, wall->angle) + wall->angle) & 16383;
						handleReflectVariation(projLogic, obj);

						projLogic->prevColObj = nullptr;
						projLogic->excludeObj = nullptr;
						proj_setTransform(projLogic, obj->pitch, obj->yaw);
						sound_playCued(projLogic->reflectSnd, obj->posWS);

						projLogic->flags |= 1;
						obj->posWS.y = s_projNextPosY;
						collision_moveObj(obj, projLogic->delta.x, projLogic->delta.z);
						s_projSector = obj->sector;

						projLogic->delta.x = 0;
						projLogic->delta.y = 0;
						projLogic->delta.z = 0;
						return JFALSE;
					}
				}

				// Wall Hit!
				s_hitWall = wall;
				return JTRUE;
			}

			s_projSector = wall->nextSector;
			s_projPath[s_projIter] = s_projSector;
			s_projIter++;
			assert(s_projIter <= PROJ_PATH_MAX_SECTORS);
			wall = collision_pathWallCollision(s_projSector);
		}

		if (!hitFloor && !hitCeil)
		{
			sector_getObjFloorAndCeilHeight(s_projSector, obj->posWS.y, &y0FloorHeight, &y0CeilHeight);
			if (s_projSector->secHeight > 0)
			{
				y0FloorHeight = s_projSector->floorHeight;
			}

			if (s_projNextPosY > y0FloorHeight)
			{
				hitFloor = true;
			}
			else if (s_projNextPosY < y0CeilHeight)
			{
				hitCeil = true;
			}
			else  // Nothing Hit!
			{
				// Projectile doesn't hit anything.
				return JFALSE;
			}
		}

		if (hitFloor)
		{
			SecObject* obj = projLogic->logic.obj;
			// fraction of the path where the path hits the floor.
			assert(projLogic->delta.y != 0);
			fixed16_16 dy = projLogic->delta.y == 0 ? 1 : projLogic->delta.y;	// avoid possible divide-by-zero.
			fixed16_16 u = div16(y0FloorHeight - obj->posWS.y, dy);
			s_projNextPosX = obj->posWS.x + mul16(projLogic->delta.x, u);
			s_projNextPosY = y0FloorHeight;
			s_projNextPosZ = obj->posWS.z + mul16(projLogic->delta.z, u);

			if (s_projSector->secHeight > 0)  // Hit water.
			{
				s_hitWater = JTRUE;
				return JTRUE;
			}
		}
		else if (hitCeil)
		{
			SecObject* obj = projLogic->logic.obj;
			// fraction of the path where the path hits the floor.
			assert(projLogic->delta.y != 0);
			fixed16_16 dy = projLogic->delta.y == 0 ? 1 : projLogic->delta.y;	// avoid possible divide-by-zero.
			fixed16_16 u = div16(y0CeilHeight - obj->posWS.y, dy);
			s_projNextPosX = obj->posWS.x + mul16(projLogic->delta.x, u);
			s_projNextPosY = y0CeilHeight;
			s_projNextPosZ = obj->posWS.z + mul16(projLogic->delta.z, u);
		}

		// If the thermal detonator is hitting the ground at a fast enough speed, play the reflect sound.
		if (projLogic->type == PROJ_THERMAL_DET && TFE_Jedi::abs(projLogic->speed) > FIXED(7))
		{
			sound_playCued(projLogic->reflectSnd, obj->posWS);
		}

		if (projLogic->bounceCnt == -1)
		{
			obj->pitch = -obj->pitch;
			projLogic->prevColObj = nullptr;
			projLogic->prevObj = nullptr;

			// Some projectiles can bounce when hitting the floor or ceiling.
			projLogic->speed = mul16(projLogic->speed, projLogic->vertBounciness);
			proj_setTransform(projLogic, obj->pitch, obj->yaw);

			obj->posWS.y = s_projNextPosY;
			collision_moveObj(obj, projLogic->delta.x, projLogic->delta.z);
			s_projSector = obj->sector;	// eax

			projLogic->delta.x = 0;
			projLogic->delta.y = 0;
			projLogic->delta.z = 0;
			return JFALSE;
		}
		else if (s_projSector->flags1 & SEC_FLAGS1_MAG_SEAL)
		{
			s32 count = projLogic->bounceCnt;
			projLogic->bounceCnt--;
			if (count > 0)
			{
				vec3_fixed effectPos = { s_projNextPosX, s_projNextPosY, s_projNextPosZ };
				spawnHitEffect(projLogic->reflectEffectId, s_projSector, effectPos, projLogic->excludeObj);
				// Hit the floor or ceiling simply negates the pitch to reflect.
				obj->pitch = -obj->pitch;
				handleReflectVariation(projLogic, obj);

				projLogic->prevColObj = nullptr;
				projLogic->excludeObj = nullptr;
				proj_setTransform(projLogic, obj->pitch, obj->yaw);
				sound_playCued(projLogic->reflectSnd, obj->posWS);

				obj->posWS.y = s_projNextPosY;
				collision_moveObj(obj, projLogic->delta.x, projLogic->delta.z);
				s_projSector = obj->sector;

				projLogic->delta.x = 0;
				projLogic->delta.y = 0;
				projLogic->delta.z = 0;
				return JFALSE;
			}
		}

		// Hit floor or ceiling.
		return JTRUE;
	}
	
	// return JTRUE if an object is hit.
	JBool proj_getHitObj(ProjectileLogic* projLogic)
	{
		SecObject* obj = projLogic->logic.obj;
		if (!projLogic->speed)
		{
			return JFALSE;
		}

		const fixed16_16 move = mul16(projLogic->col_speed, s_deltaTime);
		CollisionInterval interval =
		{
			obj->posWS.x,   // x0
			s_projNextPosX,	// x1
			obj->posWS.y,	// y0
			s_projNextPosY,	// y1
			obj->posWS.z,	// z0
			s_projNextPosZ,	// z1
			move,           // move during timestep
			projLogic->dir.x,	// dirX
			projLogic->dir.z	// dirZ
		};

		SecObject* hitObj = nullptr;
		while (!hitObj && s_projIter > 0)
		{
			s_projIter--;
			hitObj = collision_getObjectCollision(s_projPath[s_projIter], &interval, projLogic->prevColObj);
		}
		if (hitObj)
		{
			s_colHitObj = hitObj;
			s_colObjAdjX = obj->posWS.x + mul16(projLogic->dir.x, s_colObjOverlap);
			s_colObjAdjY = obj->posWS.y + mul16(projLogic->dir.y, s_colObjOverlap);
			s_colObjAdjZ = obj->posWS.z + mul16(projLogic->dir.z, s_colObjOverlap);
			s_colObjSector = collision_tryMove(obj->sector, obj->posWS.x, obj->posWS.z, s_colObjAdjX, s_colObjAdjZ);

			if (s_colObjSector)
			{
				s_colObjAdjY = min(max(s_colObjAdjY, s_colObjSector->ceilingHeight), s_colObjSector->floorHeight);
				if (projLogic->type == PROJ_CONCUSSION)
				{
					projLogic->hitEffectId = HEFFECT_CONCUSSION2;
				}
				return JTRUE;
			}
		}
		return JFALSE;
	}

	void proj_aimAtTarget(ProjectileLogic* proj, vec3_fixed target)
	{
		SecObject* obj = proj->logic.obj;
		vec3_fixed delta = { target.x - obj->posWS.x, target.y - obj->posWS.y, target.z - obj->posWS.z };

		fixed16_16 dirX, dirZ;
		computeDirAndLength(delta.x, delta.z, &dirX, &dirZ);

		// This is a roundabout but mathematically correct way of deriving the length...
		fixed16_16 len = mul16(dirX, delta.x) + mul16(dirZ, delta.z);

		fixed16_16 dirY, dirH;
		computeDirAndLength(-delta.y, len, &dirY, &dirH);

		proj_setYawPitch(proj, dirY/*sinPitch*/, dirH/*cosPitch*/, dirX/*sinYaw*/, dirZ/*cosYaw*/);
	}

	void proj_aimArcing(ProjectileLogic* proj, vec3_fixed target, fixed16_16 speed)
	{
		assert(speed != 0);

		SecObject* obj = proj->logic.obj;
		fixed16_16 dist = distApprox(obj->posWS.x, obj->posWS.z, target.x, target.z);
		fixed16_16 distOverSpd = speed ? div16(dist, speed) : dist;
		fixed16_16 gravityHalfDistOverSpd = mul16(s_projGravityAccel, distOverSpd >> 1);

		fixed16_16 dy = obj->posWS.y - target.y;
		angle14_32 vertAngle = vec2ToAngle(dy, dist);

		fixed16_16 sinAngle = speed ? div16(gravityHalfDistOverSpd, speed) : gravityHalfDistOverSpd;
		angle14_32 pitch = vertAngle + arcCosFixed(sinAngle, vertAngle);
		proj->speed = distOverSpd ? div16(dist + dy, distOverSpd) : (dist + dy);
		proj_setTransform(proj, pitch, obj->yaw);
	}

	// TFE: Serialization functionality.
	s32 proj_getLogicIndex(ProjectileLogic* logic)
	{
		return allocator_getIndex(s_projectiles, logic);
	}

	ProjectileLogic* proj_getByLogicIndex(s32 index)
	{
		if (index < 0) { return nullptr; }
		return (ProjectileLogic*)allocator_getByIndex(s_projectiles, index);
	}

	// Serialization
	void projLogic_serialize(Logic*& logic, SecObject* obj, Stream* stream)
	{
		ProjectileLogic* proj;
		if (serialization_getMode() == SMODE_WRITE)
		{
			proj = (ProjectileLogic*)logic;
		}
		else
		{
			proj = (ProjectileLogic*)allocator_newItem(s_projectiles);
			if (!proj)
				return;
			logic = (Logic*)proj;

			proj->logic.task = s_projectileTask;
			proj->logic.cleanupFunc = projectileLogicCleanupFunc;
			task_makeActive(s_projectileTask);
		}
		SERIALIZE(ObjState_InitVersion, proj->type, PROJ_COUNT);
		SERIALIZE(ObjState_InitVersion, proj->dmg, 0);
		SERIALIZE(ObjState_InitVersion, proj->falloffAmt, 0);
		SERIALIZE(ObjState_InitVersion, proj->nextFalloffTick, 0);
		SERIALIZE(ObjState_InitVersion, proj->dmgFalloffDelta, 0);
		SERIALIZE(ObjState_InitVersion, proj->minDmg, 0);

		const vec3_fixed def = { 0 };
		SERIALIZE(ObjState_InitVersion, proj->projForce, 0);
		SERIALIZE(ObjState_InitVersion, proj->delta, def);
		SERIALIZE(ObjState_InitVersion, proj->vel, def);
		SERIALIZE(ObjState_InitVersion, proj->speed, 0);
		SERIALIZE(ObjState_InitVersion, proj->col_speed, 0);
		SERIALIZE(ObjState_InitVersion, proj->dir, def);
		SERIALIZE(ObjState_InitVersion, proj->horzBounciness, 0);
		SERIALIZE(ObjState_InitVersion, proj->vertBounciness, 0);

		s32 prevColObjId, prevObjId, excludeObjId;
		if (serialization_getMode() == SMODE_WRITE)
		{
			prevColObjId = proj->prevColObj ? proj->prevColObj->serializeIndex : -1;
			prevObjId    = proj->prevObj    ? proj->prevObj->serializeIndex    : -1;
			excludeObjId = proj->excludeObj ? proj->excludeObj->serializeIndex : -1;
		}
		SERIALIZE(ObjState_InitVersion, prevColObjId, -1);
		SERIALIZE(ObjState_InitVersion, prevObjId, -1);
		SERIALIZE(ObjState_InitVersion, excludeObjId, -1);
		if (serialization_getMode() == SMODE_READ)
		{
			proj->prevColObj = (prevColObjId >= 0) ? objData_getObjectBySerializationId(prevColObjId) : nullptr;
			proj->prevObj    = (prevObjId >= 0)    ? objData_getObjectBySerializationId(prevObjId)    : nullptr;
			proj->excludeObj = (excludeObjId >= 0) ? objData_getObjectBySerializationId(excludeObjId) : nullptr;
		}

		SERIALIZE(ObjState_InitVersion, proj->duration, 0);
		SERIALIZE(ObjState_InitVersion, proj->homingAngleSpd, 0);
		SERIALIZE(ObjState_InitVersion, proj->bounceCnt, 0);
		SERIALIZE(ObjState_InitVersion, proj->reflVariation, 0);

		if (serialization_getMode() == SMODE_READ)
		{
			proj->flightSndId = 0;
		}
		serialization_serializeDfSound(stream, ObjState_InitVersion, &proj->flightSndSource);
		serialization_serializeDfSound(stream, ObjState_InitVersion, &proj->cameraPassSnd);
		serialization_serializeDfSound(stream, ObjState_InitVersion, &proj->reflectSnd);

		s32 projUpdateFuncId = -1;
		if (serialization_getMode() == SMODE_WRITE && proj->updateFunc)
		{
			for (s32 i = 0; i < TFE_ARRAYSIZE(c_projUpdateFunc); i++)
			{
				if (proj->updateFunc == c_projUpdateFunc[i])
				{
					projUpdateFuncId = i;
					break;
				}
			}
		}
		SERIALIZE(ObjState_InitVersion, projUpdateFuncId, -1);
		if (serialization_getMode() == SMODE_READ)
		{
			proj->updateFunc = (projUpdateFuncId >= 0) ? c_projUpdateFunc[projUpdateFuncId] : nullptr;
			// There is at least one projectile, so make sure the task is running.
			task_makeActive(s_projectileTask);
		}

		SERIALIZE(ObjState_InitVersion, proj->reflectEffectId, HEFFECT_NONE);
		SERIALIZE(ObjState_InitVersion, proj->hitEffectId, HEFFECT_NONE);
		SERIALIZE(ObjState_InitVersion, proj->flags, 0);
	}
		
	// Returns JTRUE if the hit was properly handled, otherwise returns JFALSE.
	JBool handleProjectileHit(ProjectileLogic* projLogic, ProjectileHitType hitType)
	{
		SecObject* obj  = projLogic->logic.obj;
		RSector* sector = obj->sector;
		switch (hitType)
		{
			case PHIT_SKY:
			{
				sound_stop(projLogic->flightSndId);

				// Delete the projectile itself.
				allocator_addRef(s_projectiles);
					deleteLogicAndObject((Logic*)projLogic);
				allocator_release(s_projectiles);

				allocator_deleteItem(s_projectiles, projLogic);
				return JTRUE;
			} break;
			case PHIT_SOLID:
			{
				// Stop the projectiles in-flight sound.
				sound_stop(projLogic->flightSndId);

				// Spawn the projectile hit effect.
				if (obj->posWS.y <= sector->floorHeight && obj->posWS.y >= sector->ceilingHeight && (projLogic->type != PROJ_PUNCH || hitType != PHIT_OUT_OF_RANGE))
				{
					spawnHitEffect(projLogic->hitEffectId, obj->sector, obj->posWS, projLogic->excludeObj);
				}

				// Delete the projectile itself.
				allocator_addRef(s_projectiles);
					deleteLogicAndObject((Logic*)projLogic);
				allocator_release(s_projectiles);
				allocator_deleteItem(s_projectiles, projLogic);
				return JTRUE;
			} break;
			case PHIT_OUT_OF_RANGE:
			{
				sound_stop(projLogic->flightSndId);
				if (projLogic->flags & PROJFLAG_EXPLODE)
				{
					if (obj->posWS.y <= sector->floorHeight && obj->posWS.y >= sector->ceilingHeight && (projLogic->type != PROJ_PUNCH || hitType != PHIT_OUT_OF_RANGE))
					{
						spawnHitEffect(projLogic->hitEffectId, sector, obj->posWS, projLogic->excludeObj);
					}
				}

				// Delete the projectile itself.
				allocator_addRef(s_projectiles);
					deleteLogicAndObject((Logic*)projLogic);
				allocator_release(s_projectiles);
				allocator_deleteItem(s_projectiles, projLogic);
				return JTRUE;
			} break;
			case PHIT_WATER:
			{
				sound_stop(projLogic->flightSndId);
				spawnHitEffect(HEFFECT_SPLASH, obj->sector, obj->posWS, projLogic->excludeObj);

				// Delete the projectile itself.
				allocator_addRef(s_projectiles);
					deleteLogicAndObject((Logic*)projLogic);
				allocator_release(s_projectiles);
				allocator_deleteItem(s_projectiles, projLogic);
				return JTRUE;
			} break;
		}

		return JFALSE;
	}

	// TFE - Transform the spawn offset of a projectile based on orientation (yaw)
	void transformFireOffsets(angle14_16 yaw, vec3_fixed* sourceOffset, vec3_fixed* offset)
	{
		fixed16_16 sinYaw;
		fixed16_16 cosYaw;

		sinCosFixed(yaw, &sinYaw, &cosYaw);
		offset->x = mul16(sourceOffset->x,cosYaw) + mul16(sourceOffset->z, sinYaw);
		offset->z = -mul16(sourceOffset->x,sinYaw) + mul16(sourceOffset->z, cosYaw);
		offset->y = sourceOffset->y;
	}

}  // TFE_DarkForces