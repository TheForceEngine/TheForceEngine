#include "lactor.h"
#include "lsystem.h"
#include "lcanvas.h"
#include "lview.h"
#include "ltimer.h"
#include <TFE_Game/igame.h>
#include <cassert>
#include <cstring>

namespace TFE_DarkForces
{
	static JBool s_lactorInit = JFALSE;
	static JBool s_zPlaneBuild = JFALSE;
	static JBool s_refreshActors;
	static LActorType* s_actorType = nullptr;
	static LActor* s_actorList = nullptr;

	void lactor_freeData(LActor* actor);

	void lactor_init()
	{
		s_lactorInit = JTRUE;
		s_actorType = nullptr;
		s_actorList = nullptr;
	}

	void lactor_destroy()
	{
		s_lactorInit = JFALSE;
		s_actorType = nullptr;
		s_actorList = nullptr;
	}

	JBool lactor_createType(u32 type, LActorFrameFunc frameFunc, LActorStateFunc stateFunc)
	{
		assert(s_lactorInit);
		LActorType* newType = (LActorType*)landru_alloc(sizeof(LActorType));
		if (newType)
		{
			newType->type = type;
			newType->frameFunc = frameFunc;
			newType->stateFunc = stateFunc;

			newType->next = s_actorType;
			s_actorType = newType;
		}
		return (newType) ? JTRUE : JFALSE;
	}

	void lactor_destroyType(u32 type)
	{
		LActorType* newType = s_actorType;
		LActorType* lastType = nullptr;
		while (newType && newType->type != type)
		{
			lastType = newType;
			newType = newType->next;
		}

		if (newType)
		{
			if (lastType) { lastType->next = newType->next; }
			else { s_actorType = s_actorType->next; }

			landru_free(newType);
		}
	}

	LActorType* lactor_findType(u32 type)
	{
		LActorType* curType = s_actorType;
		while (curType && curType->type != type)
		{
			curType = curType->next;
		}
		return curType;
	}

	LActor* lactor_getList()
	{
		return s_actorList;
	}

	void lactor_addActor(LActor* actor)
	{
		LActor* curActor = s_actorList;
		LActor* lastActor = nullptr;

		while (curActor && curActor->zplane > actor->zplane)
		{
			lastActor = curActor;
			curActor = curActor->next;
		}

		if (!lastActor)
		{
			actor->next = curActor;
			s_actorList = actor;
		}
		else
		{
			actor->next = curActor;
			lastActor->next = actor;
		}

		actor->id = 0;
	}

	void lactor_removeActor(LActor* actor)
	{
		if (!actor) { return; }

		LActor* curActor = s_actorList;
		LActor* lastActor = nullptr;

		while (curActor && curActor != actor)
		{
			lastActor = curActor;
			curActor = curActor->next;
		}

		if (curActor && curActor == actor)
		{
			if (!lastActor)
			{
				s_actorList = curActor->next;
			}
			else
			{
				lastActor->next = curActor->next;
			}
			actor->next = nullptr;
		}
	}

	LActor* lactor_alloc(s16 extend)
	{
		LActor* actor = (LActor*)landru_alloc(sizeof(LActor) + extend);
		memset(actor, 0, sizeof(LActor) + extend);

		if (actor)
		{
			actor->next = nullptr;
			actor->start = 0;
			actor->stop = -1;

			actor->flags = LAFLAG_REFRESHABLE | LAFLAG_REFRESH | LAFLAG_DIRTY;

			actor->fgColor = 0x0f;
			actor->bgColor = 0x00;
			actor->xScale = LA_SCALE_FACTOR;
			actor->yScale = LA_SCALE_FACTOR;
		}
		return actor;
	}

	void lactor_free(LActor* actor)
	{
		if (!actor) { return; }
		lactor_freeData(actor);

		if (actor->varPtr)
		{
			landru_free(actor->varPtr);
		}
		if (actor->varPtr2)
		{
			landru_free(actor->varPtr2);
		}
		landru_free(actor);
	}

	// TFE
	HighResActor* actorHighRes_alloc()
	{
		HighResActor* actor = (HighResActor*)landru_alloc(sizeof(HighResActor));
		memset(actor, 0, sizeof(HighResActor));
		return actor;
	}

	void lactor_freeList(LActor* actor)
	{
		while (actor)
		{
			LActor* next = actor->next;
			lactor_removeActor(actor);
			lactor_free(actor);

			actor = next;
		}
	}

	void lactor_refresh()
	{
		s_refreshActors = JTRUE;
	}

	void lactor_draw(JBool refresh)
	{
		refresh |= s_refreshActors;
		s_refreshActors = JFALSE;

		LActor* actorList = lactor_getList();
		for (s32 i = 0; i < LVIEW_COUNT; i++)
		{
			LRect rect, clipRect;
			lview_getFrame(i, &rect);

			LActor* curActor = actorList;
			if (!lrect_isEmpty(&rect))
			{
				while (curActor)
				{
					// This might not be needed in TFE.
					ltime_checkTimeElapsed();

					if (curActor->drawFunc && lactor_isVisible(curActor))
					{
						if (lview_clipObjToView(i, curActor->zplane, &curActor->frame, &rect, &clipRect))
						{
							s32 curRefresh = (curActor->flags&LAFLAG_REFRESH) | (curActor->flags&LAFLAG_REFRESHABLE) | refresh;
							lcanvas_setClip(&clipRect);

							s16 x, y;
							lactor_getRelativePos(curActor, &rect, &x, &y);
							curActor->drawFunc(curActor, &rect, &clipRect, x, y, curRefresh ? JTRUE : JFALSE);
						}
					}

					curActor = curActor->next;
				}  // while (curActor)
			}  // if (!empty)
		}  // view loop

		// Clear actor refresh
		for (LActor* curActor = actorList; curActor; curActor = curActor->next)
		{
			curActor->flags &= ~LAFLAG_REFRESH;
		}
	}

	void lactor_update(LTick time)
	{
		LActor* actor = lactor_getList();
		while (actor)
		{
			if (actor->start == time)
			{
				lactor_start(actor);
				if (actor->stop == time)
				{
					lactor_stop(actor);
				}
			}
			else
			{
				if (actor->stop == time)
				{
					lactor_stop(actor);
				}
				else if (actor->updateFunc && (actor->flags & LAFLAG_ACTIVE))
				{
					actor->updateFunc(actor);
				}
			}

			actor = actor->next;
		}
	}

	void lactor_updateCallbacks(LTick time)
	{
		LActor* actor = lactor_getList();
		while (actor)
		{
			if (actor->callbackFunc)
			{
				actor->callbackFunc(actor, time);
			}
			actor = actor->next;
		}
	}

	LActor* lactor_find(u32 type, const char* name)
	{
		LActor* actor = lactor_getList();
		while (actor)
		{
			if (actor->resType == type && strcasecmp(actor->name, name) == 0)
			{
				return actor;
			}
			actor = actor->next;
		}
		return nullptr;
	}

	void lactor_show(LActor* actor)
	{
		actor->flags |= LAFLAG_VISIBLE;
	}

	void lactor_hide(LActor* actor)
	{
		actor->flags &= ~LAFLAG_VISIBLE;
	}

	JBool lactor_isVisible(LActor* actor)
	{
		return (actor->flags&LAFLAG_VISIBLE) ? JTRUE : JFALSE;
	}

	void lactor_start(LActor* actor)
	{
		actor->flags |= (LAFLAG_VISIBLE | LAFLAG_ACTIVE);
	}

	void lactor_stop(LActor* actor)
	{
		actor->flags &= ~(LAFLAG_VISIBLE | LAFLAG_ACTIVE);
	}

	void lactor_setDirty(LActor* actor)
	{
		actor->flags |= LAFLAG_DIRTY;
	}

	JBool lactor_isDirty(LActor* actor)
	{
		return (actor->flags&LAFLAG_DIRTY) ? JTRUE : JFALSE;
	}

	void lactor_enableRefresh(LActor* actor)
	{
		actor->flags |= LAFLAG_REFRESH;
	}

	void lactor_disableRefresh(LActor* actor)
	{
		actor->flags &= ~LAFLAG_REFRESH;
	}

	JBool lactor_isActive(LActor* actor)
	{
		return (actor->flags & LAFLAG_REFRESH) ? JTRUE : JFALSE;
	}

	void lactor_enableRefreshable(LActor* actor)
	{
		actor->flags |= LAFLAG_REFRESHABLE;
	}

	void lactor_disableRefreshable(LActor* actor)
	{
		actor->flags &= ~LAFLAG_REFRESHABLE;
	}

	JBool lactor_isRefreshable(LActor* actor)
	{
		return (actor->flags & LAFLAG_REFRESHABLE) ? JTRUE : JFALSE;
	}

	void lactor_discardData(LActor* actor)
	{
		actor->flags |= LAFLAG_DISCARD;
	}

	void lactor_keepData(LActor* actor)
	{
		actor->flags &= ~LAFLAG_DISCARD;
	}

	JBool lactor_willDiscardData(LActor* actor)
	{
		return (actor->flags & LAFLAG_DISCARD) ? JTRUE : JFALSE;
	}

	void lactor_setFlag1(LActor* actor)
	{
		actor->flags |= LAFLAG_USER_FLAG1;
	}

	void lactor_clearFlag1(LActor* actor)
	{
		actor->flags &= ~LAFLAG_USER_FLAG1;
	}

	JBool lactor_isFlag1Set(LActor* actor)
	{
		return (actor->flags & LAFLAG_USER_FLAG1) ? JTRUE : JFALSE;
	}

	void lactor_setFlag2(LActor* actor)
	{
		actor->flags |= LAFLAG_USER_FLAG2;
	}

	void lactor_clearFlag2(LActor* actor)
	{
		actor->flags &= ~LAFLAG_USER_FLAG2;
	}

	JBool lactor_isFlag2Set(LActor* actor)
	{
		return (actor->flags & LAFLAG_USER_FLAG2) ? JTRUE : JFALSE;
	}

	void lactor_getRelativePos(LActor* actor, LRect* rect, s16* x, s16* y)
	{
		*x = (actor->x - actor->frame.left) + rect->left;
		*y = (actor->y - actor->frame.top) + rect->top;
	}

	void lactor_setPos(LActor* actor, s16 x, s16 y, s16 xFract, s16 yFract)
	{
		actor->x = x;
		actor->y = y;
		actor->xFract = xFract;
		actor->yFract = yFract;
	}

	void lactor_getOffset(LActor* actor, s16* x, s16* y)
	{
		LActorType* type = lactor_findType(actor->resType);
		if (type && type->frameFunc)
		{
			LRect rect;
			type->frameFunc(actor, &rect);
			*x = rect.left;
			*y = rect.top;
		}
		else
		{
			*x = 0;
			*y = 0;
		}
	}

	void lactor_getPos(LActor* actor, s16* x, s16* y, s16* xFract, s16* yFract)
	{
		*x = actor->x;
		*y = actor->y;
		*xFract = actor->xFract;
		*yFract = actor->yFract;
	}

	void lactor_setSize(LActor* actor, s16 w, s16 h)
	{
		actor->w = w;
		actor->h = h;
	}

	void lactor_getSize(LActor* actor, s16* w, s16* h)
	{
		*w = actor->w;
		*h = actor->h;
	}

	void lactor_getBaseRect(LActor* actor, LRect* rect)
	{
		LActorType* type = lactor_findType(actor->resType);
		if (type && type->frameFunc)
		{
			type->frameFunc(actor, rect);
		}
		else
		{
			lrect_set(rect, 0, 0, actor->w, actor->h);
		}
	}

	void lactor_getRect(LActor* actor, LRect* rect)
	{
		lactor_getBaseRect(actor, rect);
		lrect_offset(rect, actor->x, actor->y);
	}

	void lactor_getCenter(LActor* actor, s16* x, s16* y)
	{
		*x = actor->x + (actor->w >> 1);
		*y = actor->y + (actor->h >> 1);
	}

	void lactor_setName(LActor* actor, u32 resType, const char* name)
	{
		actor->resType = resType;
		strcpy(actor->name, name);
	}

	void lactor_setFrame(LActor* actor, LRect* rect)
	{
		actor->frame = *rect;
	}

	void lactor_getFrame(LActor* actor, LRect* rect)
	{
		*rect = actor->frame;
	}

	void lactor_setBounds(LActor* actor, LRect* rect)
	{
		actor->bounds = *rect;
	}

	void lactor_getBounds(LActor* actor, LRect* rect)
	{
		*rect = actor->bounds;
	}

	void lactor_setZPlane(LActor* actor, s16 zplane)
	{
		actor->zplane = zplane;
		s_zPlaneBuild = JTRUE;
	}

	void lactor_getZPlane(LActor* actor, s16* zplane)
	{
		*zplane = actor->zplane;
	}

	void lactor_setTime(LActor* actor, s32 start, s32 stop)
	{
		if (!actor) { return; }
		actor->start = start;
		actor->stop = stop;
	}

	void lactor_getTime(LActor* actor, s32* start, s32* stop)
	{
		*start = actor->start;
		*stop = actor->stop;
	}

	void lactor_setState(LActor* actor, s16 state, s16 stateFract)
	{
		if (!actor) { return; }

		LActorType* type = lactor_findType(actor->resType);
		if (type && type->stateFunc)
		{
			type->stateFunc(actor, state, stateFract);
		}
		else
		{
			actor->state = state;
			actor->stateFract = stateFract;
		}
	}

	void lactor_getState(LActor* actor, s16* state, s16* stateFract)
	{
		*state = actor->state;
		*stateFract = actor->stateFract;
	}

	void lactor_setStateSpeed(LActor* actor, s16 stateVel, s16 stateVelFract)
	{
		actor->stateVel = stateVel;
		actor->stateVelFract = stateVelFract;
	}

	void lactor_getStateSpeed(LActor* actor, s16* stateVel, s16* stateVelFract)
	{
		*stateVel = actor->stateVel;
		*stateVelFract = actor->stateVelFract;
	}

	void lactor_setColor(LActor* actor, s16 fgColor, s16 bgColor)
	{
		actor->fgColor = fgColor;
		actor->bgColor = bgColor;
	}

	void lactor_getColor(LActor* actor, s16* fgColor, s16* bgColor)
	{
		*fgColor = actor->fgColor;
		*bgColor = actor->bgColor;
	}

	void lactor_setRemapColor(LActor* actor, s16 fgColor)
	{
		actor->fgColor = fgColor;
		actor->flags |= LAFLAG_COLOR;
	}

	s16 lactor_getRemapColor(LActor* actor)
	{
		return actor->fgColor;
	}

	void lactor_clearRemapColor(LActor* actor)
	{
		actor->flags &= ~LAFLAG_COLOR;
	}

	void lactor_setScale(LActor* actor, s16 xScale, s16 yScale)
	{
		actor->xScale = xScale;
		actor->yScale = yScale;
	}

	void lactor_getScale(LActor* actor, s16* xScale, s16* yScale)
	{
		*xScale = actor->xScale;
		*yScale = actor->yScale;
	}

	void lactor_setFlip(LActor* actor, JBool hFlip, JBool vFlip)
	{
		if (hFlip) { actor->flags |= LAFLAG_HFLIP; }
		else { actor->flags &= ~LAFLAG_HFLIP; }

		if (vFlip) { actor->flags |= LAFLAG_VFLIP; }
		else { actor->flags &= ~LAFLAG_VFLIP; }
	}

	void lactor_getFlip(LActor* actor, JBool* hFlip, JBool* vFlip)
	{
		*hFlip = (actor->flags & LAFLAG_HFLIP) ? JTRUE : JFALSE;
		*vFlip = (actor->flags & LAFLAG_VFLIP) ? JTRUE : JFALSE;
	}
		
	void lactor_setSpeed(LActor* actor, s16 xVel, s16 yVel, s16 xVelFract, s16 yVelFract)
	{
		actor->xVel = xVel;
		actor->yVel = yVel;
		actor->xVelFract = xVelFract;
		actor->yVelFract = yVelFract;
	}

	void lactor_getSpeed(LActor* actor, s16* xVel, s16* yVel, s16* xVelFract, s16* yVelFract)
	{
		*xVel = actor->xVel;
		*yVel = actor->yVel;
		*xVelFract = actor->xVelFract;
		*yVelFract = actor->yVelFract;
	}

	void lactor_setFrameSpeed(LActor* actor, s16 leftVel, s16 topVel, s16 rightVel, s16 bottomVel)
	{
		lrect_set(&actor->frameVelocity, leftVel, topVel, rightVel, bottomVel);
	}

	void lactor_getFrameSpeed(LActor* actor, s16* leftVel, s16* topVel, s16* rightVel, s16* bottomVel)
	{
		*leftVel   = actor->frameVelocity.left;
		*topVel    = actor->frameVelocity.top;
		*rightVel  = actor->frameVelocity.right;
		*bottomVel = actor->frameVelocity.bottom;
	}
		
	void lactor_setData(LActor* actor, u8* data)
	{
		actor->data = data;
	}

	void lactor_getData(LActor* actor, u8** data)
	{
		*data = actor->data;
	}

	void lactor_setArray(LActor* actor, u8** array)
	{
		actor->array = array;
	}

	void lactor_getArray(LActor* actor, u8*** array)
	{
		*array = actor->array;
	}

	u8* lactor_getArrayData(LActor* actor, s16 index)
	{
		if (!actor->array) { return nullptr; }
		return actor->array[index];
	}

	void lactor_setDrawFunc(LActor* actor, LActorDrawFunc drawFunc)
	{
		actor->drawFunc = drawFunc;
	}

	void lactor_getDrawFunc(LActor* actor, LActorDrawFunc* drawFunc)
	{
		*drawFunc = actor->drawFunc;
	}

	void lactor_setUpdateFunc(LActor* actor, LActorUpdateFunc updateFunc)
	{
		actor->updateFunc = updateFunc;
	}

	void lactor_getUpdateFunc(LActor* actor, LActorUpdateFunc* updateFunc)
	{
		*updateFunc = actor->updateFunc;
	}

	void lactor_setCallback(LActor* actor, LActorCallback callback)
	{
		actor->callbackFunc = callback;
	}

	void lactor_getUpdateFunc(LActor* actor, LActorCallback* callback)
	{
		*callback = actor->callbackFunc;
	}

	void lactor_move(LActor* actor)
	{
		actor->xFract += actor->xVelFract;
		actor->yFract += actor->yVelFract;
		actor->x += actor->xVel + (actor->xFract / 256);
		actor->y += actor->yVel + (actor->yFract / 256);

		while (actor->xFract >=  256) { actor->xFract -= 256; }
		while (actor->yFract >=  256) { actor->yFract -= 256; }
		while (actor->xFract <= -256) { actor->xFract += 256; }
		while (actor->yFract <= -256) { actor->yFract += 256; }
	}

	void lactor_moveFrame(LActor* actor)
	{
		actor->frame.left   += actor->frameVelocity.left;
		actor->frame.top    += actor->frameVelocity.top;
		actor->frame.right  += actor->frameVelocity.right;
		actor->frame.bottom += actor->frameVelocity.bottom;
	}

	void lactor_moveState(LActor* actor)
	{
		actor->stateFract += actor->stateVelFract;
		actor->state += actor->stateVel + (actor->stateVelFract / 256);

		while (actor->stateFract >=  256) { actor->stateFract -= 256; }
		while (actor->stateFract <= -256) { actor->stateFract += 256; }
	}

	void lactor_copyData(LActor* dst, const LActor* src)
	{
		lactor_freeData(dst);
		lactor_keepData(dst);
		strcpy(dst->name, src->name);

		dst->data  = src->data;
		dst->array = src->array;
		dst->arraySize = src->arraySize;
		dst->bounds = src->bounds;
		dst->w = src->w;
		dst->h = src->h;
	}

	void lactor_clipToActor(LActor* dst, LActor* src)
	{
		lrect_clip(&dst->frame, &src->frame);
	}

	void lactor_updateZPlanes()
	{
		if (s_zPlaneBuild)
		{
			s_zPlaneBuild = JFALSE;
			lactor_sortZPlanes();
		}
	}

	void lactor_sortZPlanes()
	{
		LActor* actorSort = nullptr;
		LActor* actorBase = lactor_getList();
		while (actorBase)
		{
			LActor* curActorSort = actorSort;
			LActor* curActorBase = actorBase;
			actorBase = actorBase->next;

			LActor* lastActorSort = nullptr;
			curActorBase->next = nullptr;

			while (curActorSort && curActorSort->zplane >= curActorBase->zplane)
			{
				lastActorSort = curActorSort;
				curActorSort  = curActorSort->next;
			}

			if (!curActorSort)
			{
				if (!lastActorSort)
				{
					actorSort = curActorBase;
				}
				else
				{
					lastActorSort->next = curActorBase;
				}
			}
			else if (!lastActorSort)
			{
				actorSort = curActorBase;
				curActorBase->next = curActorSort;
			}
			else
			{
				lastActorSort->next = curActorBase;
				curActorBase->next  = curActorSort;
			}
		}

		s_actorList = actorSort;
	}

	void lactor_freeData(LActor* actor)
	{
		if (!actor) { return; }

		if (actor->flags & LAFLAG_DISCARD)
		{
			if (actor->data)
			{
				landru_free(actor->data);
			}
			if (actor->array)
			{
				for (s32 i = 0; i < actor->arraySize; i++)
				{
					if (actor->array[i])
					{
						landru_free(actor->array[i]);
					}
				}
				landru_free(actor->array);
			}
		}

		actor->data  = nullptr;
		actor->array = nullptr;
		actor->arraySize = 0;
	}
}  // namespace TFE_DarkForces