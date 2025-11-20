
#include "SimpleUI.h"
#include "SimpleUIFrame.h"
#include "SimpleUIButton.h"
#include "stdtypes.h"
#include "wininclude.h"
#include "earray.h"
#include "timing.h"
#include "StashTable.h"
#include "MemoryPool.h"
#include "strings_opt.h"
#include "utils.h"
#include "mathutil.h"

// FrameWD -------------------------------------------------------------------------------------

typedef struct FrameWD {
	char*						name;
	
	void*						userPointer;
	SUIWindowPipe*				wp;
	
	SUIWindow*					wClient;
	SUIWindow*					wCloseButton;
	SUIWindow*					wMaximizeButton;
	
	S32							startTime;
	S32							destroyTime;
	
	S32							dragAnchor[2];
	U32							colorARGB;
	
	U32							sizing;
	
	SUIWindowProcessingHandle*	phAppearing;
	SUIWindowProcessingHandle*	phDestroying;
	
	U32							destroying	: 1;
	U32							dragging	: 1;
} FrameWD;

#define SUI_FRAME_EDGE			6
#define SUI_FRAME_CLIENT_BORDER 2

SUI_MSG_GROUP_FUNCTION_DEFINE(suiFrame, "SUIFrame");

static S32 suiFrameMsgHandler(	SUIWindow* w,
								FrameWD* wd,
								const SUIWindowMsg* msg);

static S32 suiFrameHandleCreate(SUIWindow* w,
								FrameWD* wd,
								const SUIWindowMsg* msg)
{
	const SUIFrameCreateParams* cp = msg->msgData;
	
	assert(!wd);
	
	wd = callocStruct(FrameWD);
	
	wd->userPointer = cp->userPointer;
	
	if(cp->name){
		wd->name = strdup(cp->name);
	}
	
	suiWindowPipeCreate(&wd->wp,
						cp->wReader,
						w);
	
	suiWindowSetUserPointer(w, suiFrameMsgHandler, wd);
	
	suiWindowSetPosAndSize(w, 200, 200, 500, 0);
	
	wd->colorARGB = rand();
	
	wd->startTime = timeGetTime();
	
	suiWindowProcessingHandleCreate(w, &wd->phAppearing);
	
	suiButtonCreate(&wd->wCloseButton, w, "X", w);
	suiWindowSetPosAndSize(	wd->wCloseButton,
							SUI_FRAME_EDGE,
							SUI_FRAME_EDGE,
							30,
							30);
	
	suiButtonCreate(&wd->wMaximizeButton, w, "Y", w);
	suiWindowSetPosAndSize(	wd->wMaximizeButton,
							SUI_FRAME_EDGE + 30 + 5,
							SUI_FRAME_EDGE,
							30,
							30);

	return 1;
}

static S32 suiFrameHandleDestroy(	SUIWindow* w,
									FrameWD* wd,
									const SUIWindowMsg* msg)
{
	SUIFrameMsgDestroyed md = {0};

	md.userPointer = wd->userPointer;
	
	printf(	"Sending frame destroyed: 0x%8.8p:0x%8.8p\n",
			w,
			wd->userPointer);
	
	suiWindowPipeMsgSend(	wd->wp,
							w,
							NULL,
							SUI_MSG_GROUP(suiFrame),
							SUI_FRAME_MSG_DESTROYED,
							&md);

	return 1;
}
	
static S32 suiFrameHandleDraw(	SUIWindow* w,
								FrameWD* wd,
								const SUIWindowMsg* msg)
{
	const SUIDrawContext*	dc = msg->msgData;
	U32						bgColor = wd->colorARGB;
	U32						sx;
	U32						sy;
	
	PERFINFO_AUTO_START_FUNC();
	
	suiWindowGetSize(w, &sx, &sy);
	
	if(wd->destroying){
		S32 delta = timeGetTime() - wd->destroyTime;
		S32 red = (delta / 4);
		
		MIN1(red, 255);
		
		bgColor =	0xff000000 |
					(red << 16);
	}
	if(wd->dragging){
		bgColor =	0xff000000 |
					((bgColor & 0xff0000) * 50 / 100) & 0xff0000 |
					((bgColor & 0xff00) * 50 / 100) & 0xff00 |
					((bgColor & 0xff) * 50 / 100) & 0xff;
	}
	else if(wd->sizing){
		bgColor =	0xff000000 |
					min(0xff0000, (bgColor & 0xff0000) * 150 / 100) & 0xff0000 |
					min(0xff00, (bgColor & 0xff00) * 150 / 100) & 0xff00 |
					min(0xff, (bgColor & 0xff) * 150 / 100) & 0xff;
	}
	
	suiDrawFilledRect(dc, 0, 0, sx, sy, bgColor);
	suiDrawRect(dc, 0, 0, sx, sy, 3, 0xff000000);
	
	if(wd->name){
		suiPrintText(dc, 80, 8, wd->name, -1, 25, 0xffffffff);
	}
	
	if(wd->wClient){
		U32 cx;
		U32 cy;
		U32 csx;
		U32 csy;
		
		suiWindowGetPos(wd->wClient, &cx, &cy);
		suiWindowGetSize(wd->wClient, &csx, &csy);

		suiDrawRect(dc,
					cx - SUI_FRAME_CLIENT_BORDER,
					cy - SUI_FRAME_CLIENT_BORDER,
					csx + SUI_FRAME_CLIENT_BORDER * 2,
					csy + SUI_FRAME_CLIENT_BORDER * 2,
					1,
					0xff000000 |
					(((bgColor & 0xff0000) / 4) & 0xff0000) |
					(((bgColor & 0xff00) / 4) & 0xff00) |
					(((bgColor & 0xff) / 4) & 0xff));

		suiDrawRect(dc,
					cx - SUI_FRAME_CLIENT_BORDER + 1,
					cy - SUI_FRAME_CLIENT_BORDER + 1,
					csx + SUI_FRAME_CLIENT_BORDER * 2 - 2,
					csy + SUI_FRAME_CLIENT_BORDER * 2 - 2,
					1,
					0xff000000);
	}
	
	//suiPrintText(dc, 20, 50, "This is some text.", -1, 0xffffffff);
	//suiPrintText(dc, 20, 70, "This is some text.", -1, 0xffffffff);
	
	PERFINFO_AUTO_STOP();
	
	return 1;
}

static S32 suiFrameHandleMouseDown(	SUIWindow* w,
									FrameWD* wd,
									const SUIWindowMsg* msg)
{
	const SUIWindowMsgMouseButton* mm = msg->msgData;
	
	if(	!wd->dragging &&
		!wd->sizing)
	{
		if(	mm->button & SUI_MBUTTON_LEFT){
			wd->dragAnchor[0] = mm->x;
			wd->dragAnchor[1] = mm->y;
			wd->dragging = 1;
			suiWindowInvalidate(w);
		}
		else if(mm->button & SUI_MBUTTON_RIGHT){
			wd->dragAnchor[0] = mm->x;
			wd->dragAnchor[1] = mm->y;
			wd->sizing = 1;
			suiWindowInvalidate(w);
		}
		else if(mm->button & SUI_MBUTTON_MIDDLE){
			wd->dragAnchor[0] = mm->x;
			wd->dragAnchor[1] = mm->y;
			wd->sizing = 2;
			suiWindowInvalidate(w);
		}
	}
		
	return 1;
}

static S32 suiFrameHandleMouseUp(	SUIWindow* w,
									FrameWD* wd,
									const SUIWindowMsg* msg)
{
	const SUIWindowMsgMouseButton* mm = msg->msgData;
	
	if(mm->button & SUI_MBUTTON_LEFT){
		wd->dragging = 0;
		suiWindowInvalidate(w);
	}
	else if(wd->sizing == 1 &&
			mm->button & SUI_MBUTTON_RIGHT)
	{
		wd->sizing = 0;
		suiWindowInvalidate(w);
	}
	else if(wd->sizing == 2 &&
			mm->button & SUI_MBUTTON_MIDDLE)
	{
		wd->sizing = 0;
		suiWindowInvalidate(w);
	}
	
	return 1;
}

static void suiFrameSetChildSize(	SUIWindow* w,
									FrameWD* wd)
{
	S32 d = SUI_FRAME_EDGE;
	S32 x = d + SUI_FRAME_CLIENT_BORDER;
	S32 y = d + 30 + d - 2 + SUI_FRAME_CLIENT_BORDER;
	
	suiWindowSetPosAndSize(	wd->wClient,
							x,
							y,
							suiWindowGetSizeX(w) - d - SUI_FRAME_CLIENT_BORDER - x,
							suiWindowGetSizeY(w) - d - SUI_FRAME_CLIENT_BORDER - y);
}

static S32 suiFrameHandleMouseMove(	SUIWindow* w,
									FrameWD* wd,
									const SUIWindowMsg* msg)
{
	const SUIWindowMsgMouseMove* mm = msg->msgData;
	
	if(wd->dragging){
		suiWindowSetPos(w,
						suiWindowGetPosX(w) + mm->x - wd->dragAnchor[0],
						suiWindowGetPosY(w) + mm->y - wd->dragAnchor[1]);
	}
	else if(wd->sizing == 1){
		S32 dx = suiWindowGetSizeX(w) - wd->dragAnchor[0];
		S32 dy = suiWindowGetSizeY(w) - wd->dragAnchor[1];
		S32 sx = mm->x + dx;
		S32 sy = mm->y + dy;
		
		MAX1(sx, 200);
		MAX1(sy, 100);
		
		suiWindowSetSize(	w,
							sx,
							sy);
							
		wd->dragAnchor[0] = sx - dx;
		wd->dragAnchor[1] = sy - dy;

		suiFrameSetChildSize(w, wd);
	}
	else if(wd->sizing == 2){
		S32 dx = mm->x - wd->dragAnchor[0];
		S32 dy = mm->y - wd->dragAnchor[1];
		S32 sx = suiWindowGetSizeX(w);
		S32 sy = suiWindowGetSizeY(w);
		S32 x = suiWindowGetPosX(w) + sx;
		S32 y = suiWindowGetPosY(w) + sy;
		
		sx -= dx;
		sy -= dy;
		
		MAX1(sx, 200);
		MAX1(sy, 100);
		
		suiWindowSetPosAndSize(	w,
								x - sx,
								y - sy,
								sx,
								sy);
							
		suiFrameSetChildSize(w, wd);
	}
	
	return 1;
}

static S32 suiFrameHandleProcess(	SUIWindow* w,
									FrameWD* wd,
									const SUIWindowMsg* msg)
{
	if(wd->phAppearing){
		S32 		delta = timeGetTime() - wd->startTime;
		F32 		deltaRatio;
		F32 		distance;
		F32 		angle;
		const S32	timeToEnter = 750;
		
		if(delta > timeToEnter){
			delta = timeToEnter;
			
			suiWindowProcessingHandleDestroy(w, &wd->phAppearing);
		}
		
		deltaRatio = (F32)delta / (F32)timeToEnter;
		deltaRatio = 1.f - deltaRatio;
		deltaRatio = 1.f - SQR(deltaRatio);
		
		angle = deltaRatio * 4.f * PI;
		
		distance = (1.f - SQR(deltaRatio)) * 500.f;
		
		suiWindowSetSize(	w,
							100 + deltaRatio * 1200,
							100 + deltaRatio * 600);
		
		suiWindowSetPos(w,
						100 + sin(angle) * distance,
						100 + -cos(angle) * distance);
		
		suiFrameSetChildSize(w, wd);
	}
	
	if(wd->phDestroying){
		S32 delta = timeGetTime() - wd->destroyTime;
		S32 range = ((delta + 50) / 50);
		S32 x;
		S32 y;
		S32 sx;
		S32 sy;
		S32 dx;
		S32 dy;
		
		if(delta >= 1000){
			suiWindowProcessingHandleDestroy(w, &wd->phDestroying);
			suiWindowDestroy(&w);
			return 1;
		}
		
		suiWindowGetPos(w, &x, &y);
		suiWindowGetSize(w, &sx, &sy);
		
		if(0){
			dx = rand() % (range * 2 + 1) - range;
			dy = rand() % (range * 2 + 1) - range;
			
			x -= dx;
			y -= dy;
			sx += dx * 2;
			sy += dy * 2;
		}else{
			dx = sx * (delta / 10) / 1000;
			dy = sy * (delta / 10) / 1000;
			
			x += dx;
			y += dy;
			sx -= dx * 2;
			sy -= dy * 2;
		}
				
		suiWindowSetPosAndSize(w, x, y, sx, sy);

		suiFrameSetChildSize(w, wd);
	}
	
	return 1;
}

static S32 suiFrameHandleChildRemoved(	SUIWindow* w,
										FrameWD* wd,
										const SUIWindowMsg* msg)
{
	const SUIWindow* wChild = msg->msgData;
	
	if(wChild == wd->wClient){
		wd->wClient = NULL;
	}
	
	return 1;
}

static S32 suiFrameMsgHandler(	SUIWindow* w,
								FrameWD* wd,
								const SUIWindowMsg* msg)
{
	SUI_WM_DEFAULT_HANDLERS_BEGIN(w, msg, FrameWD, wd);
		SUI_WM_HANDLER(SUI_WM_CREATE,				suiFrameHandleCreate);
		SUI_WM_HANDLER(SUI_WM_DESTROY,				suiFrameHandleDestroy);
		SUI_WM_HANDLER(SUI_WM_DRAW,					suiFrameHandleDraw);
		SUI_WM_HANDLER(SUI_WM_MOUSE_DOWN,			suiFrameHandleMouseDown);
		SUI_WM_HANDLER(SUI_WM_MOUSE_DOUBLECLICK,	suiFrameHandleMouseDown);
		SUI_WM_HANDLER(SUI_WM_MOUSE_UP,				suiFrameHandleMouseUp);
		SUI_WM_HANDLER(SUI_WM_MOUSE_MOVE,			suiFrameHandleMouseMove);
		SUI_WM_HANDLER(SUI_WM_PROCESS,				suiFrameHandleProcess);
		SUI_WM_HANDLER(SUI_WM_CHILD_REMOVED,		suiFrameHandleChildRemoved);
	SUI_WM_HANDLERS_END;

	SUI_WM_HANDLERS_BEGIN(w, msg, suiButton, FrameWD, wd);
		SUI_WM_CASE(SUI_BUTTON_MSG_BUTTON_PRESSED){
			if(msg->pipe.wWriter == wd->wCloseButton){
				if(FALSE_THEN_SET(wd->destroying)){
					wd->destroyTime = timeGetTime();

					suiWindowDestroy(&wd->wCloseButton);
					suiWindowDestroy(&wd->wMaximizeButton);

					assert(!wd->phDestroying);
					suiWindowProcessingHandleCreate(w, &wd->phDestroying);
				}
			}
		}
	SUI_WM_HANDLERS_END;

	return 0;
}

S32 suiFrameWindowCreate(	SUIWindow** wOut,
							SUIWindow* wParent,
							const SUIFrameCreateParams* cp)
{
	if(!suiWindowCreate(wOut,
						wParent,
						suiFrameMsgHandler,
						cp))
	{
		return 0;
	}
	
	return 1;	
}

S32 suiFrameWindowSetClientWindow(	SUIWindow* w,
									SUIWindow* wClient)
{
	FrameWD* wd;
	
	if(	!suiWindowGetUserPointer(w, suiFrameMsgHandler, &wd) ||
		wd->wClient ||
		!suiWindowAddChild(w, wClient))
	{
		return 0;
	}

	wd->wClient = wClient;
	
	suiFrameSetChildSize(w, wd);
	
	suiWindowInvalidate(w);
	
	return 1;
}

