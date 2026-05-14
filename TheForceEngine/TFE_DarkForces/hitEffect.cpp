#include "hitEffect.h"
#include "animLogic.h"
#include "projectile.h"
#include "sound.h"
#include <TFE_DarkForces/Actor/actor.h>
#include <TFE_Jedi/Collision/collision.h>
#include <TFE_Jedi/InfSystem/message.h>
#include <TFE_Jedi/Level/level.h>
#include <TFE_Jedi/Level/robject.h>
#include <TFE_Jedi/Serialization/serialization.h>
#include <TFE_Jedi/Memory/allocator.h>
#include <TFE_Jedi/Task/task.h>
#include <TFE_ExternalData/weaponExternal.h>
#include <TFE_ExternalData/customEffect.h>

using namespace TFE_Jedi;

namespace TFE_DarkForces
{
	struct HitEffect
	{
		HitEffectID type;
		RSector* sector;
		fixed16_16 x;
		fixed16_16 y;
		fixed16_16 z;
		SecObject* excludeObj;
	};
		
	enum HitEffectConstants
	{
		MAX_SPEC_EFFECT_COUNT = 5,
		EXPLOSION_TRIGGER_DIST = FIXED(10),
	};

	static Wax* s_genExplosion;
	static SoundSourceId s_concussionExplodeSnd;
	static EffectData s_effectData[HEFFECT_COUNT];
	static Allocator* s_hitEffects;

	std::vector<EffectData> s_customEffectData;	// TFE - custom effects

	Task* s_hitEffectTask = nullptr;
	vec3_fixed s_explodePos;
	EffectData* s_curEffectData = nullptr;

	EffectData setEffectData(HitEffectID type, TFE_ExternalData::ExternalEffect* extEffects);
	void hitEffectExplodeFunc(SecObject* obj);
	void hitEffectTaskFunc(MessageType msg);

	void hitEffect_clearState()
	{
		s_hitEffects = nullptr;
		s_hitEffectTask = nullptr;
		s_curEffectData = nullptr;
		s_genExplosion = nullptr;
	}

	void hitEffect_startup()
	{
		// TFE: Effect data is now defined externally. These were hardcoded in vanilla DF.
		TFE_ExternalData::ExternalEffect* externalEffects = TFE_ExternalData::getExternalEffects();

		s_effectData[HEFFECT_SMALL_EXP] = setEffectData(HEFFECT_SMALL_EXP, &externalEffects[HEFFECT_SMALL_EXP]);
		s_effectData[HEFFECT_THERMDET_EXP] = setEffectData(HEFFECT_THERMDET_EXP, &externalEffects[HEFFECT_THERMDET_EXP]);
		s_effectData[HEFFECT_PLASMA_EXP] = setEffectData(HEFFECT_PLASMA_EXP, &externalEffects[HEFFECT_PLASMA_EXP]);
		s_effectData[HEFFECT_MORTAR_EXP] = setEffectData(HEFFECT_MORTAR_EXP, &externalEffects[HEFFECT_MORTAR_EXP]);
		s_effectData[HEFFECT_CONCUSSION] = setEffectData(HEFFECT_CONCUSSION, &externalEffects[HEFFECT_CONCUSSION]);
		s_effectData[HEFFECT_CONCUSSION2] = setEffectData(HEFFECT_CONCUSSION2, &externalEffects[HEFFECT_CONCUSSION2]);
		s_effectData[HEFFECT_MISSILE_EXP] = setEffectData(HEFFECT_MISSILE_EXP, &externalEffects[HEFFECT_MISSILE_EXP]);
		s_effectData[HEFFECT_MISSILE_WEAK] = setEffectData(HEFFECT_MISSILE_WEAK, &externalEffects[HEFFECT_MISSILE_WEAK]);
		s_effectData[HEFFECT_PUNCH] = setEffectData(HEFFECT_PUNCH, &externalEffects[HEFFECT_PUNCH]);
		s_effectData[HEFFECT_CANNON_EXP] = setEffectData(HEFFECT_CANNON_EXP, &externalEffects[HEFFECT_CANNON_EXP]);
		s_effectData[HEFFECT_REPEATER_EXP] = setEffectData(HEFFECT_REPEATER_EXP, &externalEffects[HEFFECT_REPEATER_EXP]);
		s_effectData[HEFFECT_LARGE_EXP] = setEffectData(HEFFECT_LARGE_EXP, &externalEffects[HEFFECT_LARGE_EXP]);
		s_effectData[HEFFECT_EXP_BARREL] = setEffectData(HEFFECT_EXP_BARREL, &externalEffects[HEFFECT_EXP_BARREL]);
		s_effectData[HEFFECT_EXP_INVIS] = setEffectData(HEFFECT_EXP_INVIS, &externalEffects[HEFFECT_EXP_INVIS]);
		s_effectData[HEFFECT_SPLASH] = setEffectData(HEFFECT_SPLASH, &externalEffects[HEFFECT_SPLASH]);
		s_effectData[HEFFECT_EXP_35] = setEffectData(HEFFECT_EXP_35, &externalEffects[HEFFECT_EXP_35]);
		s_effectData[HEFFECT_EXP_NO_DMG] = setEffectData(HEFFECT_EXP_NO_DMG, &externalEffects[HEFFECT_EXP_NO_DMG]);
		s_effectData[HEFFECT_EXP_25] = setEffectData(HEFFECT_EXP_25, &externalEffects[HEFFECT_EXP_25]);

		// Custom effects
		s_customEffectData.clear();
		std::vector<TFE_ExternalData::ExternalEffect>* customEffects = TFE_ExternalData::getCustomEffects();
		for (u32 e = 0; e < customEffects->size(); e++)
		{
			TFE_ExternalData::ExternalEffect* custEffect = &customEffects->at(e);
			EffectData data = setEffectData((HitEffectID)(e + TFE_ExternalData::CUSTOM_EFFECT_STARTNUM), custEffect);
			s_customEffectData.push_back(data);
		}
	}
	
	// TFE: Set up effect from external data.
	EffectData setEffectData(HitEffectID type, TFE_ExternalData::ExternalEffect* extEffect)
	{
		return
		{
			type,														// type
			TFE_Sprite_Jedi::getWax(extEffect->wax, POOL_GAME),			// spriteData
			FIXED(extEffect->force),									// force
			FIXED(extEffect->damage),									// damage
			FIXED(extEffect->explosiveRange),							// explosiveRange
			FIXED(extEffect->wakeupRange),							// wakeupRange
			sound_load(extEffect->soundEffect, SoundPriority(extEffect->soundPriority)),	// soundEffect & priority
		};
	}

	void spawnHitEffect(HitEffectID hitEffectId, RSector* sector, vec3_fixed pos, SecObject* excludeObj)
	{
		if (hitEffectId != HEFFECT_NONE)
		{
			HitEffect* effect = (HitEffect*)allocator_newItem(s_hitEffects);
			if (!effect)
				return;
			effect->type = hitEffectId;
			effect->sector = sector;
			effect->x = pos.x;
			effect->y = pos.y;
			effect->z = pos.z;
			effect->excludeObj = excludeObj;

			// Make the hit effect task active since there is at least one effect to process.
			task_makeActive(s_hitEffectTask);
		}
	}

	void hitEffect_createTask()
	{
		hitEffect_clearState();
		s_hitEffects = allocator_create(sizeof(HitEffect));
		s_hitEffectTask = createSubTask("hitEffects", hitEffectTaskFunc);
	}

	void hitEffect_serializeTasks(Stream* stream)
	{
		u8 hasTask = 0;
		if (serialization_getMode() == SMODE_WRITE)
		{
			hasTask = s_hitEffectTask ? 1 : 0;
		}
		SERIALIZE(SaveVersionInit, hasTask, 0);
		if (hasTask && s_hitEffectTask)
		{
			const bool resetIP = s_sVersion < SaveVersionHitEffectTaskUpdate;
			task_serializeState(stream, s_hitEffectTask, nullptr, nullptr, resetIP);
		}
	}
		
	////////////////////////////////////////////////////////////////////////
	// Hit effect logic function.
	// This handles all effects currently queued up.
	////////////////////////////////////////////////////////////////////////
	void hitEffectTaskFunc(MessageType msg)
	{
		struct LocalContext
		{
			s32 count;
		};
		task_begin_ctx;
		taskCtx->count = 0;
		while (1)
		{
			// The task sleeps until there are effects to process.
			task_yield(TASK_SLEEP);

			// When it is woken up, either it will be directly called with id = 20 after an animation is complete or
			// is called during the update because there are effects to process.
			if (msg == MSG_SEQ_COMPLETE)
			{
				taskCtx->count--;
			}
			else if (msg == MSG_RUN_TASK)
			{
				// There are no yields in this block, so using purely local variables is safe.
				HitEffect* effect = (HitEffect*)allocator_getHead(s_hitEffects);
				while (effect)
				{
					RSector* sector = effect->sector;
					fixed16_16 x = effect->x;
					fixed16_16 y = effect->y;
					fixed16_16 z = effect->z;

					// TFE - custom effects are numbered starting from 100
					EffectData* data = nullptr;
					if (effect->type < TFE_ExternalData::CUSTOM_EFFECT_STARTNUM)
					{
						data = &s_effectData[effect->type];
					}
					else
					{
						u32 index = effect->type - TFE_ExternalData::CUSTOM_EFFECT_STARTNUM;
						if (index < s_customEffectData.size())
						{
							data = &s_customEffectData.at(index);
						}
					}
					
					s_curEffectData = data;

					if (data && data->spriteData)
					{
						JBool createEffectObj = JTRUE;
						if (effect->type == HEFFECT_PLASMA_EXP || effect->type == HEFFECT_CANNON_EXP)
						{
							if (taskCtx->count >= MAX_SPEC_EFFECT_COUNT)
							{
								createEffectObj = false;
							}
							else
							{
								taskCtx->count++;
							}
						}

						// Avoid creating a new effect object if there are too many.
						if (createEffectObj)
						{
							SecObject* obj = allocateObject();
							// If effect is Concussion, adjust y so it sits on the floor.
							if (effect->type == HEFFECT_CONCUSSION || effect->type == HEFFECT_CONCUSSION2)
							{
								fixed16_16 dummy;
								sector_getObjFloorAndCeilHeight(sector, y, &y, &dummy);
							}

							setObjPos_AddToSector(obj, x, y, z, sector);
							if (effect->type != HEFFECT_SPLASH)
							{
								obj->flags |= OBJ_FLAG_FULLBRIGHT;
							}
							sprite_setData(obj, s_curEffectData->spriteData);
							SpriteAnimLogic* animLogic = (SpriteAnimLogic*)obj_setSpriteAnim(obj);

							// Setup to call this task when animation is finished.
							if (effect->type == HEFFECT_PLASMA_EXP || effect->type == HEFFECT_CANNON_EXP)
							{
								setAnimCompleteTask(animLogic, s_hitEffectTask);
							}

							setupAnimationFromLogic(animLogic, 0/*animIndex*/, 0/*firstFrame*/, 0xffffffff/*lastFrame*/, 1/*loopCount*/);
							obj->worldWidth = 0;
							
							// Add this if we want scripting access to effects
							// obj_addToRefList(obj, ObjRefType_Effect);	// scripting
						}
					}
					if (s_curEffectData && s_curEffectData->soundEffect)
					{
						vec3_fixed soundPos = { x,y,z };
						sound_playCued(s_curEffectData->soundEffect, soundPos);
					}

					if (s_curEffectData && s_curEffectData->explosiveRange)
					{
						s_explodePos.x = x;
						s_explodePos.y = y;
						s_explodePos.z = z;
						collision_effectObjectsInRange3D(sector, s_curEffectData->explosiveRange, s_explodePos, hitEffectExplodeFunc, effect->excludeObj,
							ETFLAG_AI_ACTOR | ETFLAG_SCENERY | ETFLAG_LANDMINE | ETFLAG_LANDMINE_WPN | ETFLAG_PLAYER);

						if (s_curEffectData->type != HEFFECT_PUNCH)
						{
							fixed16_16 range = s_curEffectData->explosiveRange;
							inf_handleExplosion(sector, x, z, range >> 3);
						}
					}
					if (s_curEffectData && s_curEffectData->wakeupRange)
					{
						// Wakes up all objects with a valid collision path to (x,y,z) that are within s_curEffectData->wakeupRange units.
						vec3_fixed hitPos = { x, y, z };
						collision_effectObjectsInRangeXZ(sector, s_curEffectData->wakeupRange, hitPos, actor_sendWakeupMsg, effect->excludeObj, ETFLAG_AI_ACTOR);
					}
					allocator_deleteItem(s_hitEffects, effect);
					// Since the current item is deleted, "head" is the next item in the list.
					effect = (HitEffect*)allocator_getHead(s_hitEffects);
				}  // while (effect)
			}  // if (id == 0)
		}  // while (1)
		task_end;
	}
		
	///////////////////////////////////////
	// Internal
	///////////////////////////////////////
	void hitEffectExplodeFunc(SecObject* obj)
	{
		const u32 flags = ETFLAG_AI_ACTOR | ETFLAG_SCENERY | ETFLAG_LANDMINE | ETFLAG_LANDMINE_WPN | ETFLAG_PLAYER;
		if (!(obj->entityFlags & flags))
		{
			return;
		}

		fixed16_16 dy = TFE_Jedi::abs(s_explodePos.y - obj->posWS.y);
		fixed16_16 approxDist = dy + distApprox(obj->posWS.x, obj->posWS.z, s_explodePos.x, s_explodePos.z);
		// This triggers when an explosive is hit with an explosion.
		if ((obj->entityFlags & ETFLAG_LANDMINE) || (obj->entityFlags & ETFLAG_LANDMINE_WPN))
		{
			if (approxDist < EXPLOSION_TRIGGER_DIST)
			{
				triggerLandMine((ProjectileLogic*)obj->projectileLogic, (obj->entityFlags & ETFLAG_LANDMINE) ? 87 : 145);
			}
		}
		else
		{
			fixed16_16 radius = s_curEffectData->explosiveRange;
			fixed16_16 leftOverRadius = max(radius - approxDist, 0);
			HitEffectID type = s_curEffectData->type;
			if (type == HEFFECT_CONCUSSION || type == HEFFECT_CONCUSSION2)
			{
				fixed16_16 floor, dummy;
				RSector* sector = obj->sector;
				SecObject* newObj = allocateObject();

				sprite_setData(newObj, s_effectData[HEFFECT_CONCUSSION].spriteData);
				sector_getObjFloorAndCeilHeight(sector, s_explodePos.y, &floor, &dummy);
				setObjPos_AddToSector(newObj, obj->posWS.x, floor, obj->posWS.z, sector);

				newObj->flags |= OBJ_FLAG_FULLBRIGHT;
				newObj->worldWidth = 0;

				SpriteAnimLogic* logic = (SpriteAnimLogic*)obj_setSpriteAnim(newObj);
				setupAnimationFromLogic(logic, 0/*animIndex*/, 0/*firstFrame*/, 0xffffffff/*lastFrame*/, 1/*loopCount*/);
				sound_playCued(s_concussionExplodeSnd, newObj->posWS);

				// Add this if we want scripting access to effects
				// obj_addToRefList(obj, ObjRefType_Effect);	// scripting

				s_msgArg1 = s_curEffectData->damage;
			}
			else
			{
				fixed16_16 damage = s_curEffectData->damage;
				fixed16_16 attenDmg = mul16(div16(leftOverRadius, radius), damage);
				s_msgArg1 = attenDmg;
			}

			fixed16_16 force = s_curEffectData->force;
			fixed16_16 attenForce = mul16(div16(leftOverRadius, radius), force);
			s_msgArg2 = attenForce;
			message_sendToObj(obj, MSG_EXPLOSION, actor_messageFunc);
		}
	}
}  // TFE_DarkForces