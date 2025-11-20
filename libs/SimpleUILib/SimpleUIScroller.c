
#include "SimpleUI.h"
#include "SimpleUIScroller.h"
#include "stdtypes.h"
#include "wininclude.h"
#include "mathutil.h"
#include "assert.h"
#include "timing.h"

SUI_MSG_GROUP_FUNCTION_DEFINE(suiScroller, "SUIScroller");

typedef struct ScrollerWD {
	SUIWindowPipe*				wp;
	
	SUIWindowProcessingHandle*	phNotOver;

	S32							upOverAtTime;
	S32							downOverAtTime;
	
	S32							isOver;
	
	S32							upSize;
	S32							downSize;
	S32							lastTime;
	
	S32							size;
	S32							pos;
} ScrollerWD;

typedef struct SUIScrollerCreateParams {
	SUIWindow*					wReader;
} SUIScrollerCreateParams;

static S32 suiScrollerMsgHandler(	SUIWindow* w,
									ScrollerWD* wd,
									const SUIWindowMsg* msg);

static S32 suiScrollerHandleCreate(	SUIWindow* w,
									ScrollerWD* wd,
									const SUIWindowMsg* msg)
{
	const SUIScrollerCreateParams* cp = msg->msgData;
	assert(!wd);
	
	wd = callocStruct(ScrollerWD);
	
	suiWindowSetUserPointer(w, suiScrollerMsgHandler, wd);

	suiWindowPipeCreate(&wd->wp,
						cp->wReader,
						w);
	
	return 1;
}

static S32 suiScrollerHandleDestroy(SUIWindow* w,
									ScrollerWD* wd,
									const SUIWindowMsg* msg)
{
	SAFE_FREE(wd);
	return 1;
}

#define SCROLLER_TIME			(300)
#define SCROLLER_SIZE			(30)
#define SCROLLER_LINGER_TIME	(200)

static S32 suiScrollerHandleDraw(	SUIWindow* w,
									ScrollerWD* wd,
									const SUIWindowMsg* msg)
{
	PERFINFO_AUTO_START_FUNC();
	{
		const SUIDrawContext*	dc = msg->msgData;
		
		S32 sx = suiWindowGetSizeX(w);
		S32 sy = suiWindowGetSizeY(w);
		S32 upY = wd->upSize * SCROLLER_SIZE / SCROLLER_TIME;
		S32 downY = wd->downSize * SCROLLER_SIZE / SCROLLER_TIME;
		S32 upSize;
		S32 downSize;
		S32 underMouse;
		S32 mousePos[2];
		S32 upOffTime = wd->lastTime - wd->upOverAtTime;
		S32 downOffTime = wd->lastTime - wd->downOverAtTime;
		
		upY = upY ? sinf(PI * upY / (F32)SCROLLER_SIZE - PI * 0.5f) * (SCROLLER_SIZE / 2) + (SCROLLER_SIZE / 2) : 0;
		downY = downY ? sinf(PI * downY / (F32)SCROLLER_SIZE - PI * 0.5f) * (SCROLLER_SIZE / 2) + (SCROLLER_SIZE / 2) : 0;
		
		upSize = upY * (SCROLLER_SIZE * 0.6) / SCROLLER_SIZE;
		downSize = downY * (SCROLLER_SIZE * 0.6) / SCROLLER_SIZE;
		
		MINMAX1(upOffTime, 0, 255);
		MINMAX1(downOffTime, 0, 255);
		
		underMouse = suiWindowGetMousePos(w, mousePos);

		#define OVER_COLOR		0xff111133
		#define NOT_OVER_COLOR	0xff000000
		
		// Draw the top arrow.
		
		suiDrawRect(dc, 0, 0, sx, upY, 2, 0xff000000);
		suiDrawFilledRect(	dc, 3, 3, sx - 6, upY - 6,
							suiColorInterpAllRGB(0xff, OVER_COLOR, NOT_OVER_COLOR, upOffTime));
		suiDrawRect(dc, 2, 2, sx - 4, upY - 4, 1, 0xff222822);
		
		suiSetClipRect(dc, 3, 3, sx - 6, upY - 6);
		suiDrawFilledTriangle(	dc,
								sx / 2, upY - (SCROLLER_SIZE / 5) - upSize,
								sx / 2 - upSize, upY - (SCROLLER_SIZE / 5),
								sx / 2 + upSize, upY - (SCROLLER_SIZE / 5),
								0xff557755);
		
		// Draw the bottom arrow.
		
		suiClearClipRect(dc);
		suiDrawRect(dc, 0, sy - downY, sx, downY, 2, 0xff000000);
		suiDrawFilledRect(	dc, 3, sy - downY + 3, sx - 6, downY - 6,
							suiColorInterpAllRGB(0xff, OVER_COLOR, NOT_OVER_COLOR, downOffTime));
		suiDrawRect(dc, 2, sy - downY + 2, sx - 4, downY - 4, 1, 0xff222822);
		
		suiSetClipRect(dc, 3, sy - downY + 3, sx - 6, downY - 6);
		suiDrawFilledTriangle(	dc,
								sx / 2, sy - downY + (SCROLLER_SIZE / 5) + downSize,
								sx / 2 - downSize, sy - downY + (SCROLLER_SIZE / 5),
								sx / 2 + downSize, sy - downY + (SCROLLER_SIZE / 5),
								0xff557755);
	}
	PERFINFO_AUTO_STOP();
	
	return 1;
}

static void suiScrollerSendPositionChanged(	SUIWindow* w,
											ScrollerWD* wd)
{
	SUIScrollerMsgPositionChanged md = {0};
	
	md.pos = wd->pos;
	
	suiWindowPipeMsgSend(	wd->wp,
							w,
							NULL,
							SUI_MSG_GROUP(suiScroller),
							SUI_SCROLLER_MSG_POSITION_CHANGED,
							&md);
}

static S32 suiScrollerHandleMouseDown(	SUIWindow* w,
										ScrollerWD* wd,
										const SUIWindowMsg* msg)
{
	const SUIWindowMsgMouseButton*	mm = msg->msgData;
	S32								posChanged = 0;
	S32								delta;

	if(mm->button & SUI_MBUTTON_LEFT){
		delta = 100;
	}
	else if(mm->button & SUI_MBUTTON_RIGHT){
		delta = suiWindowGetSizeY(w);
	}else{
		delta = 0;
	}

	if(	mm->y < SCROLLER_SIZE &&
		wd->pos > 0)
	{
		if(delta){
			wd->pos -= delta;
			MAX1(wd->pos, 0);
		}else{
			wd->pos = 0;
		}
		posChanged = 1;
	}
	else if(wd->pos < wd->size &&
			mm->y >= suiWindowGetSizeY(w) - SCROLLER_SIZE)
	{
		if(delta){
			wd->pos += delta;
			MIN1(wd->pos, wd->size);
		}else{
			wd->pos = wd->size;
		}
		posChanged = 1;
	}
	
	if(posChanged){
		suiScrollerSendPositionChanged(w, wd);

		return 1;
	}
	
	return 0;
}

static S32 suiScrollerHandleMouseLeave(	SUIWindow* w,
										ScrollerWD* wd,
										const SUIWindowMsg* msg)
{
	const SUIWindowMsgMouseLeave* mm = msg->msgData;
	
	if(wd->isOver){
		wd->isOver = 0;
		
		suiWindowInvalidate(w);

		if(!wd->phNotOver){
			wd->lastTime = 0;
			suiWindowProcessingHandleCreate(w, &wd->phNotOver);
		}
	}
	
	return 0;
}

static S32 suiScrollerHandleMouseUp(SUIWindow* w,
									ScrollerWD* wd,
									const SUIWindowMsg* msg)
{
	const SUIWindowMsgMouseButton* mm = msg->msgData;
	
	return 1;
}

static S32 suiScrollerHandleMouseMove(	SUIWindow* w,
										ScrollerWD* wd,
										const SUIWindowMsg* msg)
{
	const SUIWindowMsgMouseMove*	mm = msg->msgData;
	S32								isOver = 0;
	
	if(mm->y < SCROLLER_SIZE){
		wd->upOverAtTime = timeGetTime();

		if(wd->pos > 0){
			isOver = 1;
		}
	}
	else if(mm->y >= suiWindowGetSizeY(w) - SCROLLER_SIZE){
		wd->downOverAtTime = timeGetTime();

		if(wd->pos < wd->size){
			isOver = 2;
		}
	}
	
	if(isOver != wd->isOver){
		wd->isOver = isOver;
		
		suiWindowInvalidate(w);

		if(!wd->phNotOver){
			wd->lastTime = 0;
			suiWindowProcessingHandleCreate(w, &wd->phNotOver);
		}
	}
	
	return !!isOver;
}

static S32 suiScrollerHandleMouseWheel(	SUIWindow* w,
										ScrollerWD* wd,
										const SUIWindowMsg* msg)
{
	const SUIWindowMsgMouseWheel* md = msg->msgData;
	
	if(md->clickThousandths < 0){
		wd->pos += 100;
		MIN1(wd->pos, wd->size);
	}
	else if(md->clickThousandths > 0){
		wd->pos -= 100;
		MAX1(wd->pos, 0);
	}

	suiScrollerSendPositionChanged(w, wd);
	
	return 1;
}

static S32 suiScrollerHandleProcess(SUIWindow* w,
									ScrollerWD* wd,
									const SUIWindowMsg* msg)
{
	const SUIWindowMsgMouseMove* mm = msg->msgData;
	
	S32 curTime = timeGetTime();
	S32 deltaTime;
	S32 underMouse;
	S32 mousePos[2];
	S32 keepProcessing = 0;
	
	if(!wd->lastTime){
		wd->lastTime = curTime;
	}
	
	if(curTime - wd->lastTime < 25){
		return 1;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	{
		S32 upOffTime = wd->lastTime - wd->upOverAtTime;
		S32 downOffTime = wd->lastTime - wd->downOverAtTime;
		
		if(	upOffTime > 0 &&
			upOffTime < 255
			||
			downOffTime > 0 &&
			downOffTime < 255)
		{
			suiWindowInvalidate(w);

			keepProcessing = 1;
		}
	}

	deltaTime = curTime - wd->lastTime;
	
	underMouse = suiWindowGetMousePos(w, mousePos);
	
	if(	underMouse &&
		mousePos[1] < SCROLLER_SIZE &&
		wd->pos > 0)
	{
		wd->upOverAtTime = curTime;
	}
		
	if(	wd->pos > 0 &&
		(U32)(curTime - wd->upOverAtTime) < SCROLLER_TIME + SCROLLER_LINGER_TIME)
	{
		if(wd->upSize < SCROLLER_TIME){
			wd->upSize += deltaTime;

			suiWindowInvalidate(w);

			keepProcessing = 1;
		}
		else if(curTime != wd->upOverAtTime){
			keepProcessing = 1;
		}
	}
	else if(wd->upSize > 0){
		wd->upSize -= deltaTime;
		
		suiWindowInvalidate(w);

		keepProcessing = 1;
	}
	
	MINMAX1(wd->upSize, 0, SCROLLER_TIME);
	
	if(	underMouse &&
		mousePos[1] > suiWindowGetSizeY(w) - SCROLLER_SIZE &&
		wd->pos < wd->size)
	{
		wd->downOverAtTime = curTime;
	}
	
	if(	wd->pos < wd->size &&
		(U32)(curTime - wd->downOverAtTime) < SCROLLER_TIME + SCROLLER_LINGER_TIME)
	{
		if(wd->downSize < SCROLLER_TIME){
			wd->downSize += deltaTime;

			suiWindowInvalidate(w);
			
			keepProcessing = 1;
		}
		else if(curTime != wd->downOverAtTime){
			keepProcessing = 1;
		}
	}
	else if(wd->downSize > 0){
		wd->downSize -= deltaTime;
		
		suiWindowInvalidate(w);

		keepProcessing = 1;
	}

	MINMAX1(wd->downSize, 0, SCROLLER_TIME);
	
	wd->lastTime = curTime;
	
	if(!keepProcessing){
		suiWindowProcessingHandleDestroy(w, &wd->phNotOver);
	}

	PERFINFO_AUTO_STOP();
	
	return 1;
}

static S32 suiScrollerHandleParentSizeChanged(	SUIWindow* w,
												ScrollerWD* wd,
												const SUIWindowMsg* msg)
{
	S32 psx;
	S32 psy;
	
	if(suiWindowParentGetSize(w, &psx, &psy)){
		suiWindowSetSize(w, psx, psy);
	}
	
	return 1;
}

static S32 suiScrollerMsgHandler(	SUIWindow* w,
									ScrollerWD* wd,
									const SUIWindowMsg* msg)
{
	SUI_WM_DEFAULT_HANDLERS_BEGIN(w, msg, ScrollerWD, wd);
		SUI_WM_HANDLER(SUI_WM_CREATE,				suiScrollerHandleCreate);
		SUI_WM_HANDLER(SUI_WM_DESTROY,				suiScrollerHandleDestroy);
		SUI_WM_HANDLER(SUI_WM_DRAW,					suiScrollerHandleDraw);
		SUI_WM_HANDLER(SUI_WM_MOUSE_MOVE,			suiScrollerHandleMouseMove);
		SUI_WM_HANDLER(SUI_WM_MOUSE_WHEEL,			suiScrollerHandleMouseWheel);
		SUI_WM_HANDLER(SUI_WM_MOUSE_LEAVE,			suiScrollerHandleMouseLeave);
		SUI_WM_HANDLER(SUI_WM_MOUSE_DOWN,			suiScrollerHandleMouseDown);
		SUI_WM_HANDLER(SUI_WM_MOUSE_DOUBLECLICK,	suiScrollerHandleMouseDown);
		SUI_WM_HANDLER(SUI_WM_MOUSE_UP,				suiScrollerHandleMouseUp);
		SUI_WM_HANDLER(SUI_WM_PROCESS,				suiScrollerHandleProcess);
		SUI_WM_HANDLER(SUI_WM_PARENT_SIZE_CHANGED,	suiScrollerHandleParentSizeChanged);
	SUI_WM_HANDLERS_END;
	
	return 0;
}

S32 suiScrollerCreate(	SUIWindow** wOut,
						SUIWindow* wParent,
						SUIWindow* wReader)
{
	SUIScrollerCreateParams cp = {0};
	
	cp.wReader = wReader;
	
	return suiWindowCreate(	wOut,
							wParent,
							suiScrollerMsgHandler,
							&cp);
}

S32 suiScrollerSetSize(	SUIWindow* w,
						S32 size)
{
	ScrollerWD* wd;
	
	if(!suiWindowGetUserPointer(w, suiScrollerMsgHandler, &wd)){
		return 0;	
	}
	
	wd->size = MAX(size, 0);
	
	if(wd->pos > wd->size){
		wd->pos = wd->size;

		suiScrollerSendPositionChanged(w, wd);
	}
	
	return 1;
}
						
S32 suiScrollerSetPos(	SUIWindow* w,
						S32 pos)
{
	ScrollerWD* wd;
	
	if(!suiWindowGetUserPointer(w, suiScrollerMsgHandler, &wd)){
		return 0;
	}
	
	wd->pos = MAX(pos, 0);
	
	suiScrollerSendPositionChanged(w, wd);

	return 1;
}

S32 suiScrollerGetPos(	SUIWindow* w,
						S32* posOut,
						S32* sizeOut)
{
	ScrollerWD* wd;
	
	if(!suiWindowGetUserPointer(w, suiScrollerMsgHandler, &wd)){
		return 0;
	}
	
	if(posOut){
		*posOut = wd->pos;
	}
	
	if(sizeOut){
		*sizeOut = wd->size;
	}
	
	return 1;
}