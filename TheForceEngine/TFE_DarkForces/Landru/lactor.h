#pragma once
//////////////////////////////////////////////////////////////////////
// Dark Forces
// Landru Actor
// Landru was the cutscene system developed for XWing and Tie Fighter
// and is also used in Dark Forces.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include "lview.h"
#include "ltimer.h"

///////////////////////////////////////////
// Constants
///////////////////////////////////////////
enum LActorConstants
{
	LA_SCALE_FACTOR = 256,
	// Flags
	LAFLAG_VISIBLE     = FLAG_BIT(0),
	LAFLAG_ACTIVE      = FLAG_BIT(1),
	LAFLAG_DISCARD	   = FLAG_BIT(2),
	LAFLAG_DIRTY	   = FLAG_BIT(3),
	LAFLAG_REFRESHABLE = FLAG_BIT(4),
	LAFLAG_REFRESH     = FLAG_BIT(5),
	LAFLAG_KEEPABLE	   = FLAG_BIT(6),
	LAFLAG_KEEP        = FLAG_BIT(7),
	LAFLAG_HFLIP       = FLAG_BIT(8),
	LAFLAG_VFLIP       = FLAG_BIT(9),
	LAFLAG_COLOR       = FLAG_BIT(10),
	LAFLAG_PACKET      = FLAG_BIT(11),
	LAFLAG_USER_FLAG1  = FLAG_BIT(14),
	LAFLAG_USER_FLAG2  = FLAG_BIT(15),
};

namespace TFE_DarkForces
{
	struct LActor;

	typedef JBool(*LActorDrawFunc)(LActor*, LRect*, LRect*, s16, s16, JBool);
	typedef void(*LActorUpdateFunc)(LActor*);
	typedef void(*LActorCallback)(LActor*, s32);
	typedef void(*LActorFrameFunc)(LActor*, LRect*);
	typedef void(*LActorStateFunc)(LActor*, s16, s16);

	struct LActor
	{
		u32 resType;
		char name[16];
		
		LActor* next;
		s32 start;
		s32 stop;

		LRect frame;
		LRect frameVelocity;
		LRect bounds;

		s16 x, y;
		s16 xFract, yFract;
		s16 xVel, yVel;
		s16 xVelFract, yVelFract;

		s16 w, h;
		s16 zplane;

		s16 flags;
		s16 id;

		s16 state;
		s16 stateFract;
		s16 stateVel;
		s16 stateVelFract;

		s16 fgColor;
		s16 bgColor;
		s16 xScale;
		s16 yScale;

		s16 var1;
		s16 var2;
		u8* varPtr;
		u8* varPtr2;

		u8* data;
		u8** array;
		u16 arraySize;

		LActorDrawFunc   drawFunc;
		LActorUpdateFunc updateFunc;
		LActorCallback   callbackFunc;
	};

	// TFE - support for DF Remaster hi res assets
	struct HighResActor
	{
		u32** array;
		u16 arraySize;
	};

	struct LActorType
	{
		u32 type;
		LActorType* next;
		LActorFrameFunc frameFunc;
		LActorStateFunc stateFunc;
	};

	void lactor_init();
	void lactor_destroy();

	JBool lactor_createType(u32 type, LActorFrameFunc frameFunc, LActorStateFunc stateFunc);
	void lactor_destroyType(u32 type);
	LActorType* lactor_findType(u32 type);

	void lactor_addActor(LActor* actor);
	void lactor_removeActor(LActor* actor);

	LActor* lactor_alloc(s16 extend);
	void lactor_free(LActor* actor);
	void lactor_freeList(LActor* actor);

	// High res assets
	HighResActor* actorHighRes_alloc();

	LActor* lactor_getList();
	LActor* lactor_find(u32 type, const char* name);

	void lactor_refresh();
	void lactor_draw(JBool refresh);
	void lactor_update(LTick time);
	void lactor_updateCallbacks(LTick time);
	void lactor_updateZPlanes();
	void lactor_sortZPlanes();

	void lactor_copyData(LActor* dst, const LActor* src);
	void lactor_clipToActor(LActor* dst, LActor* src);
	
	void lactor_setName(LActor* actor, u32 resType, const char* name);
	void lactor_getRelativePos(LActor* actor, LRect* rect, s16* x, s16* y);
	void lactor_getOffset(LActor* actor, s16* x, s16* y);
	void lactor_setPos(LActor* actor, s16 x, s16 y, s16 xFract, s16 yFract);
	void lactor_getPos(LActor* actor, s16* x, s16* y, s16* xFract, s16* yFract);
	void lactor_setSize(LActor* actor, s16 w, s16 h);
	void lactor_getSize(LActor* actor, s16* w, s16* h);
	void lactor_getCenter(LActor* actor, s16* x, s16* y);
	void lactor_setFrame(LActor* actor, LRect* rect);
	void lactor_getFrame(LActor* actor, LRect* rect);
	void lactor_setBounds(LActor* actor, LRect* rect);
	void lactor_getBounds(LActor* actor, LRect* rect);
	void lactor_getBaseRect(LActor* actor, LRect* rect);
	void lactor_getRect(LActor* actor, LRect* rect);
	void lactor_show(LActor* actor);
	void lactor_hide(LActor* actor);
	void lactor_setDirty(LActor* actor);
	void lactor_start(LActor* actor);
	void lactor_stop(LActor* actor);

	void lactor_enableRefresh(LActor* actor);
	void lactor_disableRefresh(LActor* actor);
	void lactor_enableRefreshable(LActor* actor);
	void lactor_disableRefreshable(LActor* actor);
	void lactor_discardData(LActor* actor);
	void lactor_keepData(LActor* actor);
	void lactor_setFlag1(LActor* actor);
	void lactor_clearFlag1(LActor* actor);
	void lactor_setFlag2(LActor* actor);
	void lactor_clearFlag2(LActor* actor);

	void lactor_move(LActor* actor);
	void lactor_moveFrame(LActor* actor);
	void lactor_moveState(LActor* actor);

	void lactor_setZPlane(LActor* actor, s16 zplane);
	void lactor_getZPlane(LActor* actor, s16* zplane);
	void lactor_setTime(LActor* actor, s32 start, s32 stop);
	void lactor_getTime(LActor* actor, s32* start, s32* stop);
	void lactor_setState(LActor* actor, s16 state, s16 stateFract);
	void lactor_getState(LActor* actor, s16* state, s16* stateFract);
	void lactor_setStateSpeed(LActor* actor, s16 stateVel, s16 stateVelFract);
	void lactor_getStateSpeed(LActor* actor, s16* stateVel, s16* stateVelFract);
	void lactor_setColor(LActor* actor, s16 fgColor, s16 bgColor);
	void lactor_getColor(LActor* actor, s16* fgColor, s16* bgColor);
	void lactor_setRemapColor(LActor* actor, s16 fgColor);
	s16  lactor_getRemapColor(LActor* actor);
	void lactor_clearRemapColor(LActor* actor);
	void lactor_setScale(LActor* actor, s16 xScale, s16 yScale);
	void lactor_getScale(LActor* actor, s16* xScale, s16* yScale);
	void lactor_setFlip(LActor* actor, JBool hFlip, JBool vFlip);
	void lactor_getFlip(LActor* actor, JBool* hFlip, JBool* vFlip);
	void lactor_setSpeed(LActor* actor, s16 xVel, s16 yVel, s16 xVelFract, s16 yVelFract);
	void lactor_getSpeed(LActor* actor, s16* xVel, s16* yVel, s16* xVelFract, s16* yVelFract);
	void lactor_setFrameSpeed(LActor* actor, s16 leftVel, s16 topVel, s16 rightVel, s16 bottomVel);
	void lactor_getFrameSpeed(LActor* actor, s16* leftVel, s16* topVel, s16* rightVel, s16* bottomVel);
	void lactor_setData(LActor* actor, u8* data);
	void lactor_getData(LActor* actor, u8** data);
	void lactor_setArray(LActor* actor, u8** array);
	void lactor_getArray(LActor* actor, u8*** array);
	u8*  lactor_getArrayData(LActor* actor, s16 index);

	void lactor_setDrawFunc(LActor* actor, LActorDrawFunc drawFunc);
	void lactor_getDrawFunc(LActor* actor, LActorDrawFunc* drawFunc);
	void lactor_setUpdateFunc(LActor* actor, LActorUpdateFunc updateFunc);
	void lactor_getUpdateFunc(LActor* actor, LActorUpdateFunc* updateFunc);
	void lactor_setCallback(LActor* actor, LActorCallback callback);
	void lactor_getUpdateFunc(LActor* actor, LActorCallback* callback);
			
	JBool lactor_isVisible(LActor* actor);
	JBool lactor_isDirty(LActor* actor);
	JBool lactor_isActive(LActor* actor);
	JBool lactor_isRefreshable(LActor* actor);
	JBool lactor_willDiscardData(LActor* actor);
	JBool lactor_isFlag1Set(LActor* actor);
	JBool lactor_isFlag2Set(LActor* actor);
}  // namespace TFE_DarkForces