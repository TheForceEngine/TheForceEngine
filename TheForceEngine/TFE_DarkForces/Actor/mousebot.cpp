#include <cstring>

#include "mousebot.h"
#include "actorModule.h"
#include "actorSerialization.h"
#include "../logic.h"
#include <TFE_DarkForces/player.h>
#include <TFE_DarkForces/hitEffect.h>
#include <TFE_DarkForces/projectile.h>
#include <TFE_DarkForces/item.h>
#include <TFE_DarkForces/random.h>
#include <TFE_DarkForces/sound.h>
#include <TFE_Game/igame.h>
#include <TFE_Asset/modelAsset_jedi.h>
#include <TFE_System/system.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_Jedi/Memory/list.h>
#include <TFE_Jedi/Memory/allocator.h>
#include <TFE_Jedi/Serialization/serialization.h>

namespace TFE_DarkForces
{
	enum MouseBotState
	{
		MBSTATE_SLEEPING = 0,
		MBSTATE_ACTIVE,
		MBSTATE_DYING
	};

	struct MouseBotResources
	{
		WaxFrame* deadFrame  = nullptr;
		SoundSourceId sound0 = NULL_SOUND;
		SoundSourceId sound1 = NULL_SOUND;
		SoundSourceId sound2 = NULL_SOUND;
	};
	struct MouseBot
	{
		Logic logic;
		PhysicsActor actor;
	};

	static MouseBotResources s_mouseBotRes = { 0 };
	static MouseBot* s_curMouseBot;
	static s32 s_mouseNum = 0;

	MessageType mousebot_handleDamage(MessageType msg, MouseBot* mouseBot)
	{
		SecObject* obj = mouseBot->logic.obj;
		if (mouseBot->actor.alive)
		{
			ProjectileLogic* proj = (ProjectileLogic*)s_msgEntity;
			mouseBot->actor.hp -= proj->dmg;
			if (mouseBot->actor.hp <= 0)
			{
				mouseBot->actor.state = MBSTATE_DYING;
				msg = MSG_RUN_TASK;
			}
			else
			{
				sound_playCued(s_mouseBotRes.sound1, obj->posWS);
				msg = MSG_DAMAGE;
			}
		}
		return msg;
	}

	MessageType mousebot_handleExplosion(MessageType msg, MouseBot* mouseBot)
	{
		PhysicsActor* phyActor = &mouseBot->actor;
		SecObject* obj = mouseBot->logic.obj;
		if (mouseBot->actor.alive)
		{
			fixed16_16 dmg   = s_msgArg1;
			fixed16_16 force = s_msgArg2;

			mouseBot->actor.hp -= dmg;
			vec3_fixed pushDir;
			vec3_fixed pos = { obj->posWS.x, obj->posWS.y - obj->worldHeight, obj->posWS.z };

			computeExplosionPushDir(&pos, &pushDir);
			vec3_fixed vel = { mul16(force, pushDir.x), mul16(force, pushDir.y), mul16(force, pushDir.z) };

			msg = MSG_RUN_TASK;
			if (mouseBot->actor.hp <= 0)
			{
				mouseBot->actor.state = MBSTATE_DYING;
				phyActor->vel = vel;
			}
			else
			{
				phyActor->vel = { vel.x >> 1, vel.y >> 1, vel.z >> 1 };
				sound_playCued(s_mouseBotRes.sound1, obj->posWS);
			}
		}
		return msg;
	}
		
	void mousebot_handleActiveState(MessageType msg)
	{
		struct LocalContext
		{
			MouseBot* mouseBot;
			SecObject* obj;
			PhysicsActor* phyActor;
			MovementModule* moveMod;
			CollisionInfo* colInfo;
			Tick tick;

			JBool odd;
			JBool flip;
		};
		task_begin_ctx;

		local(mouseBot) = s_curMouseBot;
		local(obj)      = local(mouseBot)->logic.obj;
		local(phyActor) = &local(mouseBot)->actor;
		local(moveMod)  = &local(phyActor)->moveMod;
		local(colInfo)  = &local(moveMod)->physics;

		local(moveMod)->target.pos = local(obj)->posWS;
		local(moveMod)->target.yaw = local(obj)->yaw;

		local(tick) = s_curTick;
		local(odd)  = JFALSE;
		local(flip) = JFALSE;
		while (local(phyActor)->state == MBSTATE_ACTIVE)
		{
			do
			{
				entity_yield(TASK_NO_DELAY);
				if (msg == MSG_DAMAGE)
				{
					msg = mousebot_handleExplosion(msg, local(mouseBot));
				}
				else if (msg == MSG_EXPLOSION)
				{
					msg = mousebot_handleDamage(msg, local(mouseBot));
				}
			} while (msg != MSG_RUN_TASK);

			if (local(phyActor)->state != MBSTATE_ACTIVE) { break; }

			// Go to sleep if the player hasn't been spotted in about 5 seconds.
			if (actor_isObjectVisible(local(obj), s_playerObject, 0x4000/*360 degrees*/, FIXED(25)/*closeDist*/))
			{
				local(tick) = s_curTick;
			}
			else
			{			
				Tick dt = s_curTick - local(tick);
				// If enough time has past since the player was last spotted, go back to sleep.
				if (dt > 728) // ~5 seconds.
				{
					local(phyActor)->state = MBSTATE_SLEEPING;
					break;
				}
			}

			angle14_32 yaw = local(obj)->yaw & ANGLE_MASK;
			angle14_32 actorYaw = local(moveMod)->target.yaw & ANGLE_MASK;
			if (actorYaw == yaw)
			{
				local(odd) = (s_curTick & 1) ? JTRUE : JFALSE;
				angle14_32 deltaYaw = random(16338);
				local(moveMod)->target.yaw = local(odd) ? (local(obj)->yaw + deltaYaw) : (local(obj)->yaw - deltaYaw);				
				
				local(moveMod)->target.speedRotation = random(0x3555) + 0x555;
				local(moveMod)->target.flags |= TARGET_MOVE_ROT;
				local(flip) = JFALSE;
			}

			if (local(colInfo)->wall)
			{
				s32 rnd = random(100);
				if (rnd <= 10)	// ~10% chance of playing a sound effect when hitting a wall.
				{
					sound_playCued(s_mouseBotRes.sound0, local(obj)->posWS);
				}
				if (local(odd))
				{
					local(moveMod)->target.yaw += 4096;
				}
				else
				{
					local(moveMod)->target.yaw -= 4096;
				}
				local(moveMod)->target.speedRotation = random(0x3555);
				local(flip) = ~local(flip);
			}

			fixed16_16 speed = local(flip) ? -FIXED(22) : FIXED(22);
			fixed16_16 cosYaw, sinYaw;
			sinCosFixed(local(obj)->yaw, &sinYaw, &cosYaw);
			local(moveMod)->target.pos.x = local(obj)->posWS.x + mul16(sinYaw, speed);
			local(moveMod)->target.pos.z = local(obj)->posWS.z + mul16(cosYaw, speed);

			local(moveMod)->target.speed = FIXED(22);
			local(moveMod)->target.flags |= TARGET_MOVE_XZ;
		}
		task_end;
	}

	void mousebot_die(MessageType msg)
	{
		struct LocalContext
		{
			MouseBot* mouseBot;
			MovementModule* moveMod;
			SecObject* obj;
			RSector* sector;
		};
		task_begin_ctx;
		local(mouseBot) = s_curMouseBot;
		local(obj)      = local(mouseBot)->logic.obj;
		local(moveMod)  = &local(mouseBot)->actor.moveMod;
		local(sector)   = local(obj)->sector;

		local(moveMod)->target.flags |= TARGET_FREEZE;
		sound_playCued(s_mouseBotRes.sound2, local(obj)->posWS);

		while (1)
		{
			entity_yield(TASK_NO_DELAY);
			if (msg == MSG_RUN_TASK && actor_canDie(&local(mouseBot)->actor))
			{
				break;
			}
		}

		// Spawn a small explosion.
		spawnHitEffect(HEFFECT_SMALL_EXP, local(sector), local(obj)->posWS, local(obj));

		fixed16_16 secHeight = local(sector)->secHeight - 1;
		if (secHeight < 0)  // Not a water sector.
		{
			// Create the dead version of the mousebot.
			SecObject* newObj = allocateObject();
			if (newObj)
			{
				if (!s_mouseBotRes.deadFrame)
				{
					TFE_System::logWrite(LOG_ERROR, "MouseBot", "Missing asset - dead frame.");
					// Attempt to load it now.
					s_mouseBotRes.deadFrame = TFE_Sprite_Jedi::getFrame("dedmouse.fme");
				}

				frame_setData(newObj, s_mouseBotRes.deadFrame);
				newObj->frame = 0;
				newObj->posWS = local(obj)->posWS;
				newObj->entityFlags |= ETFLAG_CORPSE;
				newObj->worldWidth = 0;
				newObj->worldHeight = 0;
				sector_addObject(local(sector), newObj);
				
				obj_addToRefList(newObj, ObjRefType_Corpse);	// scripting
			}

			// Spawn a battery.
			SecObject* item = item_create(ITEM_BATTERY);
			item->posWS = local(obj)->posWS;
			sector_addObject(local(sector), item);
		}

		local(mouseBot)->actor.alive = JFALSE;
		task_end;
	}

	void mouseBotLocalMsgFunc(MessageType msg)
	{
		MouseBot* mouseBot = (MouseBot*)task_getUserData();
		if (msg == MSG_DAMAGE)
		{
			mousebot_handleDamage(msg, mouseBot);
		}
		else if (msg == MSG_EXPLOSION)
		{
			mousebot_handleExplosion(msg, mouseBot);
		}

		// Wake up the mousebot if it has been hit.
		if (mouseBot->actor.state == MBSTATE_SLEEPING && (msg == MSG_DAMAGE || msg == MSG_EXPLOSION))
		{
			mouseBot->actor.state = MBSTATE_ACTIVE;
			task_makeActive(mouseBot->actor.actorTask);
		}
	}

	void mouseBotTaskFunc(MessageType msg)
	{
		struct LocalContext
		{
			MouseBot* mouseBot;
			PhysicsActor* actor;
			SecObject* obj;
		};
		task_begin_ctx;
		local(mouseBot) = (MouseBot*)task_getUserData();
		local(actor) = &local(mouseBot)->actor;

		while (local(mouseBot)->actor.alive)
		{
			task_setMessage(MSG_RUN_TASK);
			if (local(actor)->state == MBSTATE_DYING)
			{
				s_curMouseBot = local(mouseBot);
				task_callTaskFunc(mousebot_die);
			}
			else if (local(actor)->state == MBSTATE_ACTIVE)
			{
				s_curMouseBot = local(mouseBot);
				task_callTaskFunc(mousebot_handleActiveState);
			}
			else
			{
				local(obj) = local(mouseBot)->logic.obj;
				while (local(actor)->state == MBSTATE_SLEEPING)
				{
					do
					{
						entity_yield(145);
						if (msg == MSG_DAMAGE)
						{
							msg = mousebot_handleExplosion(msg, local(mouseBot));
						}
						else if (msg == MSG_EXPLOSION)
						{
							msg = mousebot_handleDamage(msg, local(mouseBot));
						}

						if (msg == MSG_DAMAGE || msg == MSG_EXPLOSION)
						{
							local(actor)->state = MBSTATE_ACTIVE;
							task_makeActive(local(actor)->actorTask);
							task_yield(TASK_NO_DELAY);
						}
					} while (msg != MSG_RUN_TASK);

					// Wakeup if the player is visible.
					if (local(actor)->state == MBSTATE_SLEEPING && actor_isObjectVisible(local(obj), s_playerObject, 0x4000/*full 360 degree fov*/, FIXED(25)/*25 units "close distance"*/))
					{
						sound_playCued(s_mouseBotRes.sound0, local(obj)->posWS);
						local(mouseBot)->actor.state = MBSTATE_ACTIVE;
					}
				}
			}
		}

		// The Mousebot is dead.
		while (msg != MSG_RUN_TASK)
		{
			task_yield(TASK_NO_DELAY);
		}
		actor_removePhysicsActorFromWorld(&local(mouseBot)->actor);
		deleteLogicAndObject(&local(mouseBot)->logic);
		level_free(local(mouseBot));

		task_end;
	}

	void mouseBotLogicCleanupFunc(Logic* logic)
	{
		MouseBot* mouseBot = (MouseBot*)logic;

		Task* task = mouseBot->actor.actorTask;
		actor_removePhysicsActorFromWorld(&mouseBot->actor);
		deleteLogicAndObject(&mouseBot->logic);
		level_free(mouseBot);
		task_free(task);
	}

	void mousebot_clear()
	{
		s_mouseBotRes.deadFrame = nullptr;
	}

	void mousebot_exit()
	{
		s_mouseBotRes = {};
	}

	void mousebot_precache()
	{
		s_mouseBotRes.sound0 = sound_load("eeek-1.voc", SOUND_PRIORITY_MED5);
		s_mouseBotRes.sound1 = sound_load("eeek-2.voc", SOUND_PRIORITY_LOW0);
		s_mouseBotRes.sound2 = sound_load("eeek-3.voc", SOUND_PRIORITY_MED5);
	}

	void mousebot_serialize(Logic*& logic, SecObject* obj, Stream* stream)
	{
		MouseBot* mouseBot = nullptr;
		PhysicsActor* physActor = nullptr;
		Task* mouseBotTask = nullptr;
		bool write = serialization_getMode() == SMODE_WRITE;
		if (write)
		{
			mouseBot = (MouseBot*)logic;
			physActor = &mouseBot->actor;
		}
		else
		{
			mouseBot = (MouseBot*)level_alloc(sizeof(MouseBot));
			memset(mouseBot, 0, sizeof(MouseBot));
			logic = (Logic*)mouseBot;

			// Resources
			if (!s_mouseBotRes.deadFrame)
			{
				s_mouseBotRes.deadFrame = TFE_Sprite_Jedi::getFrame("dedmouse.fme");
			}

			// Task
			char name[32];
			sprintf(name, "mouseBot%d", s_mouseNum);
			s_mouseNum++;

			mouseBotTask = createSubTask(name, mouseBotTaskFunc, mouseBotLocalMsgFunc);
			task_setUserData(mouseBotTask, mouseBot);

			// Logic
			logic->task = mouseBotTask;
			logic->cleanupFunc = mouseBotLogicCleanupFunc;
			logic->type = LOGIC_MOUSEBOT;
			logic->obj = obj;
			physActor = &mouseBot->actor;
		}
		actor_serializeMovementModuleBase(stream, &physActor->moveMod);
		actor_serializeLogicAnim(stream, &physActor->anim);
		if (!write)
		{
			// Clear out functions, the mousebot handles all of this internally.
			physActor->moveMod.header.obj = obj;
			physActor->moveMod.header.func = nullptr;
			physActor->moveMod.header.freeFunc = nullptr;
			physActor->moveMod.header.attribFunc = nullptr;
			physActor->moveMod.header.msgFunc = nullptr;
			physActor->moveMod.header.type = ACTMOD_MOVE;
			physActor->moveMod.updateTargetFunc = nullptr;

			actor_addPhysicsActorToWorld(physActor);

			physActor->moveMod.header.obj = obj;
			physActor->moveMod.physics.obj = obj;
			physActor->actorTask = mouseBotTask;
		}
		SERIALIZE(SaveVersionInit, physActor->vel, { 0 });
		SERIALIZE(SaveVersionInit, physActor->lastPlayerPos, { 0 });
		SERIALIZE(SaveVersionInit, physActor->alive, JTRUE);
		SERIALIZE(SaveVersionInit, physActor->hp, FIXED(10));
		SERIALIZE(SaveVersionInit, physActor->state, MBSTATE_SLEEPING);
	}

	Logic* mousebot_setup(SecObject* obj, LogicSetupFunc* setupFunc)
	{
		if (!s_mouseBotRes.deadFrame)
		{
			s_mouseBotRes.deadFrame = TFE_Sprite_Jedi::getFrame("dedmouse.fme");
		}

		MouseBot* mouseBot = (MouseBot*)level_alloc(sizeof(MouseBot));
		memset(mouseBot, 0, sizeof(MouseBot));

		// Give the name of the task a number so I can tell them apart when debugging.
		char name[32];
		sprintf(name, "mouseBot%d", s_mouseNum);
		s_mouseNum++;
		Task* mouseBotTask = createSubTask(name, mouseBotTaskFunc, mouseBotLocalMsgFunc);
		task_setUserData(mouseBotTask, mouseBot);

		obj->flags &= ~OBJ_FLAG_AIM;
		obj->entityFlags = ETFLAG_AI_ACTOR;
		mouseBot->logic.obj = obj;

		PhysicsActor* physActor = &mouseBot->actor;
		physActor->alive = JTRUE;
		physActor->hp = FIXED(10);
		physActor->actorTask = mouseBotTask;
		physActor->state = MBSTATE_SLEEPING;
		physActor->moveMod.header.obj = obj;
		physActor->moveMod.physics.obj = obj;
		actor_addPhysicsActorToWorld(physActor);
		actor_setupSmartObj(&physActor->moveMod);

		JediModel* model = obj->model;
		fixed16_16 width = model->radius + ONE_16;
		obj->worldWidth  = width;
		obj->worldHeight = width >> 1;

		physActor->moveMod.physics.stepUpHeight = 0;
		physActor->moveMod.physics.stepDownHeight = 0;
		physActor->moveMod.collisionFlags = (physActor->moveMod.collisionFlags | (ACTORCOL_NO_Y_MOVE | ACTORCOL_GRAVITY)) & ~ACTORCOL_SLIDE_RESPONSE;
		physActor->moveMod.physics.height = obj->worldHeight + HALF_16;
		physActor->moveMod.target.speed = FIXED(22);
		physActor->moveMod.target.speedRotation = FIXED(3185);
		physActor->moveMod.target.flags &= ~TARGET_ALL;
		physActor->moveMod.target.pitch = obj->pitch;
		physActor->moveMod.target.yaw   = obj->yaw;
		physActor->moveMod.target.roll  = obj->roll;

		obj_addLogic(obj, (Logic*)mouseBot, LOGIC_MOUSEBOT, mouseBotTask, mouseBotLogicCleanupFunc);
		if (setupFunc)
		{
			*setupFunc = nullptr;
		}
		return (Logic*)mouseBot;
	}
}  // namespace TFE_DarkForces