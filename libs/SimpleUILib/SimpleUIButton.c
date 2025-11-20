#include "SimpleUI.h"
#include "SimpleUIButton.h"
#include "stdtypes.h"
#include "wininclude.h"
#include "earray.h"
#include "timing.h"
#include "StashTable.h"
#include "MemoryPool.h"
#include "strings_opt.h"
#include "utils.h"

SUI_MSG_GROUP_FUNCTION_DEFINE(suiButton, "SUIButton");

#define BUTTON_TIME			100
#define BUTTON_DOWN_SCALE	1

typedef struct SUIButtonCreateParams {
	const char*					text;
	SUIWindow*					wReader;
} SUIButtonCreateParams;

typedef struct ButtonWD {
	SUIWindow*					w;

	char*						text;
	U32							textHeight;
	U32							rgbText;
	
	SUIWindowPipe*				wp;
	SUIWindowProcessingHandle*	ph;
	
	S32							downTime;
	S32							lastTime;
	
	struct {
		S32*					growSizes;
	} debug;

	struct {
		U32						isDown		: 1;
		U32						mouseIsOver : 1;
		
		U32						test		: 1;
		
		U32						sendDrawMsg	: 1;
	} flags;
} ButtonWD;

static S32 buttonMsgHandler(SUIWindow* w,
							ButtonWD* wd,
							const SUIWindowMsg* msg);

static S32 suiButtonHandleCreate(	SUIWindow* w,
									ButtonWD* wd,
									const SUIWindowMsg* msg)
{
	const SUIButtonCreateParams* cp = msg->msgData;
	
	wd = callocStruct(ButtonWD);
	suiWindowSetUserPointer(w, buttonMsgHandler, wd);
	
	wd->w = w;
	
	wd->text = strdup(NULL_TO_EMPTY(cp->text));
	
	wd->textHeight = 18;
	
	wd->rgbText = 0x000000;
	
	suiWindowPipeCreate(&wd->wp,
						cp->wReader,
						w);
	
	suiWindowSetSize(w, 100, 50);
	
	return 1;
}

static S32 suiButtonHandleDestroy(	SUIWindow* w,
									ButtonWD* wd,
									const SUIWindowMsg* msg)
{
	SAFE_FREE(wd->text);
	SAFE_FREE(wd);
	
	return 1;
}

static S32 suiButtonHandleMouseMove(SUIWindow* w,
									ButtonWD* wd,
									const SUIWindowMsg* msg)
{
	const SUIWindowMsgMouseMove* md = msg->msgData;
	
	if(FALSE_THEN_SET(wd->flags.mouseIsOver)){
		suiWindowInvalidate(w);
	}
	
	return 1;
}

static S32 suiButtonHandleMouseLeave(	SUIWindow* w,
										ButtonWD* wd,
										const SUIWindowMsg* msg)
{
	const SUIWindowMsgMouseMove* md = msg->msgData;
	
	if(TRUE_THEN_RESET(wd->flags.mouseIsOver)){
		suiWindowInvalidate(w);
	}

	return 1;
}

static S32 suiButtonHandleMouseDown(SUIWindow* w,
									ButtonWD* wd,
									const SUIWindowMsg* msg)
{
	const SUIWindowMsgMouseButton* md = msg->msgData;
	
	if(	!wd->flags.isDown &&
		md->button & SUI_MBUTTON_LEFT)
	{
		wd->flags.isDown = 1;
		
		if(!wd->ph){
			suiWindowProcessingHandleCreate(w, &wd->ph);
		}

		suiWindowInvalidate(w);
	
		eaiSetSize(&wd->debug.growSizes, 0);
		
		suiWindowPipeMsgSend(	wd->wp,
								w,
								NULL,
								SUI_MSG_GROUP(suiButton),
								SUI_BUTTON_MSG_BUTTON_PRESSED,
								NULL);
	}
	else if(md->button & SUI_MBUTTON_RIGHT){
		suiWindowPipeMsgSend(	wd->wp,
								w,
								NULL,
								SUI_MSG_GROUP(suiButton),
								SUI_BUTTON_MSG_BUTTON_PRESSED_RIGHT,
								NULL);
	}
	else if(md->button & SUI_MBUTTON_MIDDLE){
		suiWindowPipeMsgSend(	wd->wp,
								w,
								NULL,
								SUI_MSG_GROUP(suiButton),
								SUI_BUTTON_MSG_BUTTON_PRESSED_MIDDLE,
								NULL);
	}
	
	return 1;
}

static S32 suiButtonHandleMouseUp(	SUIWindow* w,
									ButtonWD* wd,
									const SUIWindowMsg* msg)
{
	if(	wd->flags.isDown &&
		!(suiWindowGetMouseButtonsHeld(w) & SUI_MBUTTON_LEFT))
	{
		wd->flags.isDown = 0;
		
		if(!wd->ph){
			suiWindowProcessingHandleCreate(w, &wd->ph);
		}

		suiWindowInvalidate(w);

		eaiSetSize(&wd->debug.growSizes, 0);

		suiWindowPipeMsgSend(	wd->wp,
								w,
								NULL,
								SUI_MSG_GROUP(suiButton),
								SUI_BUTTON_MSG_BUTTON_RELEASED,
								NULL);
	}
	
	return 1;
}

typedef struct ClippedData {
	ButtonWD*	wd;
	S32			sx;
	S32			sy;
	S32			isUnderMouse;
} ClippedData;

static void suiButtonClipDrawContextCallback(	const SUIDrawContext* dc,
												ClippedData* data)
{
	SUIButtonMsgDraw msgOut = {0};
	
	msgOut.dc = dc;
	msgOut.sx = data->sx;
	msgOut.sy = data->sy;
	msgOut.flags.isUnderMouse = data->isUnderMouse;
	
	suiWindowPipeMsgSend(	data->wd->wp,
							data->wd->w,
							NULL,
							SUI_MSG_GROUP(suiButton),
							SUI_BUTTON_MSG_DRAW,
							&msgOut);
}

static S32 suiButtonHandleDraw(	SUIWindow* w,
								ButtonWD* wd,
								const SUIWindowMsg* msg)
{
	PERFINFO_AUTO_START_FUNC();
	{
		const SUIDrawContext*	dc = msg->msgData;
		const S32				wsx = suiWindowGetSizeX(w);
		const S32				wsy = suiWindowGetSizeY(w);
		//const S32				held = suiWindowGetMouseHeldOnSelf(w);
		const S32				scale = wd->downTime * 1000 / BUTTON_TIME;
		S32 					minSize = MIN(wsx, wsy);
		S32 					widthDiff = MAX(40, minSize) / 20;
		S32 					width = 4 + (widthDiff * scale / 1000);
		S32 					heightDiff = MAX(40, minSize) / 20;
		S32 					cx;
		S32 					cy;
		S32 					cdx;
		S32 					cdy;
		U32						textHeight = wd->textHeight;
		
		suiDrawFilledRect(dc, 0, 0, wsx, wsy, wd->flags.test ? 0xffff0000 : 0xff000000);
		
		cx = width;
		cy = width + (heightDiff * scale / 1000);
		cdx = wsx - width * 2;
		cdy = wsy - width * 2 - (heightDiff * scale / 1000) + (widthDiff * scale / 1000);
		
		// Draw the button box.
		
		suiDrawRect(dc, cx - 2, cy - 2, cdx + 4, cdy + 4, 1, 0xff333333);
		suiDrawRect(dc, cx - 1, cy - 1, cdx + 2, cdy + 2, 1, 0xff444444);
		suiDrawFilledRect(dc, cx - 2, cy - 2, 1, 1, 0xff111111);
		suiDrawFilledRect(dc, cx - 2 + cdx + 3, cy - 2, 1, 1, 0xff111111);
		suiDrawFilledRect(dc, cx - 2, cy - 2 + cdy + 3, 1, 1, 0xff111111);
		suiDrawFilledRect(dc, cx - 2 + cdx + 3, cy - 2 + cdy + 3, 1, 1, 0xff111111);
		suiDrawFilledRect(dc, cx, cy, cdx, cdy, 0xff666655 + 0x11 * (1000 - scale) / 1000);
		
		while(1){
			// Draw the text.
			
			S32 tx;
			S32 ty;
			S32 tsx;
			S32 tsy;

			suiWindowGetTextSize(w, wd->text, textHeight, &tsx, &tsy);
			
			if(	tsx > wsx - 6 &&
				textHeight > 6)
			{
				textHeight--;
				continue;
			}

			tx = (wsx - tsx) / 2;
			ty = (wsy - tsy) / 2 + (heightDiff * scale / 1000);

			//suiDrawRect(dc, x, y, tsx, tsy, 1, 0xffff0000);
			
			if(suiWindowIsMouseOverSelf(w)){
				suiPrintText(	dc,
								tx + 1,
								ty + 1,
								wd->text,
								-1,
								textHeight,
								0);
			}

			suiPrintText(	dc,
							tx,
							ty,
							wd->text,
							-1,
							textHeight,
							suiWindowIsMouseOverSelf(w) ?
								0xff99aa99 :
								suiColorInterpAllRGB(	0xff,
														wd->rgbText,
														0x333333,
														255 * scale / 1000));
			
			break;
		}
		
		if(wd->flags.sendDrawMsg){
			ClippedData data;
			
			data.wd = wd;
			data.sx = cdx;
			data.sy = cdy;
			data.isUnderMouse = suiWindowIsMouseOverSelf(w);
			
			suiClipDrawContext(dc, cx, cy, cdx, cdy, suiButtonClipDrawContextCallback, &data);
		}

		if(0){
			S32 i;
			S32 barWidth = wsx - 4;
			S32 x = 0;
			S32 y = 2;
			
			if(	!eaiSize(&wd->debug.growSizes) ||
				wd->debug.growSizes[eaiSize(&wd->debug.growSizes) - 1] != wd->downTime)
			{
				eaiPush(&wd->debug.growSizes, suiWindowGetMouseHeldOnSelf(w) ? wd->downTime : BUTTON_TIME - wd->downTime);
			}

			suiDrawFilledRect(dc, 2, 2, 200, 2, 0xff555555);

			for(i = 0; i < eaiSize(&wd->debug.growSizes); i++){
				S32 remaining = wd->debug.growSizes[i] - (i ? wd->debug.growSizes[i - 1] : 0);
				
				while(remaining > 0){
					S32 xRight = MIN(x + remaining, barWidth);
					S32 x2 = xRight - x;
					
					suiDrawFilledRect(dc, x + 2, y, x2, 2, i & 1 ? 0xffff0000 : 0xff800000);
					
					x = x2;
					
					if(x == barWidth){
						x = 0;
						y += 2;
					}
					
					remaining -= width;
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();

	return 1;
}

static S32 suiButtonHandleProcess(	SUIWindow* w,
									ButtonWD* wd,
									const SUIWindowMsg* msg)
{
	S32 curTime = timeGetTime();
	S32 diff;
	S32 target;
	
	if(suiWindowGetMouseButtonsHeld(w) & SUI_MBUTTON_LEFT){
		target = BUTTON_TIME;
	}else{
		target = 0;
	}
	
	if(!wd->lastTime){
		wd->lastTime = curTime;
	}
	
	diff = curTime - wd->lastTime;
	wd->lastTime = curTime;
	
	if(target > wd->downTime){
		diff *= BUTTON_DOWN_SCALE;
		diff = MIN(diff, target - wd->downTime);
	}
	else if(target < wd->downTime){
		diff = -MIN(diff, wd->downTime - target);
	}else{
		wd->lastTime = 0;
		suiWindowProcessingHandleDestroy(w, &wd->ph);
		diff = 0;
	}

	if(diff){
		wd->downTime += diff;
		
		suiWindowInvalidate(w);
	}
	
	return 1;
}

static S32 buttonMsgHandler(SUIWindow* w,
							ButtonWD* wd,
							const SUIWindowMsg* msg)
{
	SUI_WM_DEFAULT_HANDLERS_BEGIN(w, msg, ButtonWD, wd);
		SUI_WM_HANDLER(SUI_WM_CREATE,				suiButtonHandleCreate);
		SUI_WM_HANDLER(SUI_WM_DESTROY,				suiButtonHandleDestroy);
		SUI_WM_HANDLER(SUI_WM_PROCESS,				suiButtonHandleProcess);
		SUI_WM_HANDLER(SUI_WM_DRAW,					suiButtonHandleDraw);
		SUI_WM_HANDLER(SUI_WM_MOUSE_MOVE,			suiButtonHandleMouseMove);
		SUI_WM_HANDLER(SUI_WM_MOUSE_LEAVE,			suiButtonHandleMouseLeave);
		SUI_WM_HANDLER(SUI_WM_MOUSE_DOWN,			suiButtonHandleMouseDown);
		SUI_WM_HANDLER(SUI_WM_MOUSE_DOUBLECLICK,	suiButtonHandleMouseDown);
		SUI_WM_HANDLER(SUI_WM_MOUSE_UP,				suiButtonHandleMouseUp);
		
		SUI_WM_CASE(SUI_WM_KEY_DOWN){
			//wd->flags.test = !wd->flags.test;
			//suiWindowInvalidate(w);
		}
	SUI_WM_HANDLERS_END;
	
	return 0;
}

S32 suiButtonCreate(SUIWindow** wOut,
					SUIWindow* wParent,
					const char* text,
					SUIWindow* wReader)
{
	SUIButtonCreateParams cp = {0};
	
	cp.wReader = wReader;
	cp.text = text;

	return suiWindowCreate(	wOut,
							wParent,
							buttonMsgHandler,
							&cp);	
}

S32 suiButtonSetText(	SUIWindow* w,
						const char* text)
{
	ButtonWD* wd;
	
	if(!text){
		return 0;
	}
	
	if(suiWindowGetUserPointer(w, buttonMsgHandler, &wd)){
		SAFE_FREE(wd->text);
		wd->text = strdup(text);
		suiWindowInvalidate(w);
		
		return 1;
	}
	
	return 0;
}

S32	suiButtonFitToText(	SUIWindow* w,
						S32 xMinSize,
						S32 ySize)
{
	ButtonWD* wd;
	
	if(suiWindowGetUserPointer(w, buttonMsgHandler, &wd)){
		S32 sx;
		S32 sy;
		
		suiWindowGetTextSize(w, wd->text, wd->textHeight, &sx, &sy);
		suiWindowSetSize(w, MAX(sx + 20, xMinSize), MAX(sy + 10, ySize));
		
		return 1;
	}
	
	return 0;
}

S32 suiButtonSetSendDrawMsg(SUIWindow* w,
							S32 enabled)
{
	ButtonWD* wd;
	
	if(suiWindowGetUserPointer(w, buttonMsgHandler, &wd)){
		wd->flags.sendDrawMsg = !!enabled;
		return 1;
	}
	
	return 0;
}

S32 suiButtonSetTextColor(	SUIWindow* w,
							U32 rgb)
{
	ButtonWD* wd;
	
	if(suiWindowGetUserPointer(w, buttonMsgHandler, &wd)){
		wd->rgbText = rgb & 0xffffff;
		suiWindowInvalidate(w);
		return 1;
	}

	return 0;
}
