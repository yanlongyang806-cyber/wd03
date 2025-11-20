
#include "SimpleUI.h"
#include "SimpleUIList.h"
#include "SimpleUIScroller.h"
#include "stdtypes.h"
#include "mathutil.h"
#include "assert.h"
#include "timing.h"
#include "earray.h"
#include "wininclude.h"

#define DRAW_BOX_WHILE_SCROLLING	0
#define DRAW_BOX_UNDER_LIST			0

SUI_MSG_GROUP_FUNCTION_DEFINE(suiList, "SUIList");

enum {
	X_INDENT_PER_DEPTH_DEFAULT = 20,
};

typedef struct ListWD		ListWD;
typedef struct SUIListEntry SUIListEntry;

typedef struct SUIListEntryClass {
	ListWD*						wd;
	SUIWindowPipe*				wp;
	U32							count;
} SUIListEntryClass;

typedef struct SUIListEntry {
	SUIListEntryClass*			lec;
	SUIListEntry*				parent;
	SUIListEntry**				children;

	void*						userPointer;
	
	struct {
		U32						self;
		U32						children;
	} height;
	
	struct {
		U32						iterators;
	} count;
	
	struct {
		U32						destroyed			: 1;
		
		U32						open				: 1;
		U32						hidden				: 1;
		
		U32						childNeedsDestroy	: 1;
	} flags;
} SUIListEntry;

typedef struct ListWD {
	void*						userPointer;
	
	SUIWindow*					w;
	SUIWindowPipe*				wp;

	SUIListEntryClass**			lecs;

	SUIListEntryClass			lecRoot;
	SUIListEntry				leRoot;
	SUIListEntry*				leUnderMouse;
	
	SUIWindowProcessingHandle*	phScrolling;
	
	SUIWindow*					wScroller;
	S32							yTop;
	
	S32							yDragAnchor;
	
	struct {
		S32						yTop;
		S32						yStart;
		U32						timeStart;
		U32						timeLast;
		U32						timeToFinish;
	} yTarget;

	S32							isOver;
	
	S32							upSize;
	S32							downSize;
	S32							lastTime;
	
	S32							size;
	S32							pos;
	
	S32							xIndentPerDepth;
	
	struct {
		U32						isDraggingY : 1;
	} flags;
} ListWD;

static struct {
	U32 suiList;
	U32 suiListEntry;
	U32 suiListEntryClass;
} count;

static void suiListEntryIteratorsInc(SUIListEntry* le);
static void suiListEntryIteratorsDec(SUIListEntry* le);

static S32	suiListEntryDestroyInternal(SUIListEntry* le,
										S32 destroySelf,
										S32 onlyDestroyedChildren);

static S32	suiListMsgHandler(	SUIWindow* w,
								ListWD* wd,
								const SUIWindowMsg* msg);

static S32 suiListHandleCreate(	SUIWindow* w,
								ListWD* wd,
								const SUIWindowMsg* msg)
{
	const SUIListCreateParams* cp = msg->msgData;

	assert(!wd);
	wd = callocStruct(ListWD);
	
	suiCountInc(count.suiList);
	
	wd->w = w;
	wd->userPointer = cp->userPointer;
	
	wd->lecRoot.wd = wd;
	wd->lecRoot.count = 1;
	
	wd->leRoot.lec = &wd->lecRoot;
	wd->leRoot.flags.open = 1;
	
	wd->xIndentPerDepth = X_INDENT_PER_DEPTH_DEFAULT;
	
	suiWindowSetUserPointer(w, suiListMsgHandler, wd);

	suiWindowPipeCreate(&wd->wp,
						cp->wReader,
						w);

	suiScrollerCreate(&wd->wScroller, w, w);

	return 1;
}

static S32 suiListHandleDestroy(SUIWindow* w,
								ListWD* wd,
								const SUIWindowMsg* msg)
{
	suiListEntryDestroyInternal(&wd->leRoot, 0, 0);

	while(eaSize(&wd->lecs)){
		suiCountDec(count.suiListEntryClass);
		SAFE_FREE(wd->lecs[0]);
		eaRemove(&wd->lecs, 0);
	}
	eaDestroy(&wd->lecs);

	suiCountDec(count.suiList);
	
	SAFE_FREE(wd);

	return 1;
}

typedef struct SUIListEntryDrawClippedData {
	SUIListEntry*	le;
	U32				xIndent;
	U32				sx;
	U32				argbDefault;
} SUIListEntryDrawClippedData;

static void suiListEntryDrawClippedCallback(const SUIDrawContext* dc,
											const SUIListEntryDrawClippedData* dcd)
{
	SUIListEntry*		le = dcd->le;
	SUIListMsgEntryDraw md = {0};
	
	md.le.le = le;
	md.le.userPointer = le->userPointer;

	md.dc = dc;

	md.sx = dcd->sx;
	md.sy = le->height.self;
	
	md.xIndent = dcd->xIndent;
	
	md.argbDefault = dcd->argbDefault;
	
	md.flags.isUnderMouse = le == le->lec->wd->leUnderMouse;
	
	PERFINFO_AUTO_START("drawEntryCallback", 1);
		suiWindowPipeMsgSend(	le->lec->wp,
								le->lec->wd->w,
								le->lec->wd->userPointer,
								SUI_MSG_GROUP(suiList),
								SUI_LIST_MSG_ENTRY_DRAW,
								&md);
	PERFINFO_AUTO_STOP();
	
	suiDrawFilledRect(dc, 0, 0, 1, 1, 0xff000000);
	suiDrawFilledRect(dc, 0, le->height.self - 1, 1, 1, 0xff000000);
	
	if(	le->flags.destroyed ||
		le->flags.childNeedsDestroy)
	{
		char buffer[100];
		
		sprintf(buffer,
				"BAD(%s, %s, %d)!!!!!",
				le->flags.destroyed ? "self" : "-",
				le->flags.childNeedsDestroy ? "child" : "-",
				le->count.iterators);
				
		suiPrintText(dc, 121, 6, buffer, -1, 0, 0xff000000);
		suiPrintText(dc, 120, 5, buffer, -1, 0, 0xff00ff00);
	}
}

static S32 indentForDepth(	S32 depth,
							S32 xIndentPerDepth)
{
	switch(depth){
		xcase 0:{
			return 0;
		}
		xcase 1:{
			return X_INDENT_PER_DEPTH_DEFAULT;
		}
		xdefault:{
			return X_INDENT_PER_DEPTH_DEFAULT + (depth - 1) * xIndentPerDepth;
		}
	}
}

static void suiListEntryDraw(	ListWD* wd,
								SUIListEntry* le,
								const SUIDrawContext* dc,
								S32 depth,
								S32 topY,
								S32 sx,
								S32 sy)
{
	const S32 xIndent = indentForDepth(depth, wd->xIndentPerDepth);
	
	if(le->height.self){
		U32 argbDefault;
		
		PERFINFO_AUTO_START("drawEntryBG", 1);
		{
			PERFINFO_AUTO_START("main", 1);
			{
				U32 c = 50 - (MIN(depth, 9) * 5);
				
				if(le == le->lec->wd->leUnderMouse){
					argbDefault = ((c + 0x11) << 16) | ((c + 0x11) << 8) | (c << 0);
				}else{
					argbDefault = (c << 16) | (c << 8) | ((c + 0x11) << 0);
				}
				
				argbDefault |= 0xff000000;
				
				suiDrawFilledRect(	dc,
									xIndent,
									topY,
									sx - xIndent,
									le->height.self,
									argbDefault);
			}
			PERFINFO_AUTO_STOP_START("outline", 1);
			{
				suiDrawFilledRect(	dc,
									xIndent - 1,
									topY,
									1,
									le->height.self,
									0xff000000);

				suiDrawFilledRect(	dc,
									xIndent,
									topY - 1,
									sx - 1,
									1,
									0xff000000);

				suiDrawFilledRect(	dc,
									xIndent,
									topY + le->height.self,
									sx - 1,
									1,
									0xff000000);
			}
			PERFINFO_AUTO_STOP();
		}
		PERFINFO_AUTO_STOP();
		
		if(le->children){
			const U32 colorClosed = 0xff30a030;
			const U32 colorOpen = 0xffa03030;
			const U32 crossSize = 6;
			const U32 rx = xIndent - 10 - crossSize;
			const U32 ry = topY + le->height.self / 2 - crossSize;
			
			if(le->flags.open){
				PERFINFO_AUTO_START("drawOpenIndicator", 1);
			}else{
				PERFINFO_AUTO_START("drawClosedIndicator", 1);
			}
			{
				suiDrawFilledRect(	dc,
									rx,
									ry + crossSize / 2,
									crossSize * 2,
									crossSize,
									0xff000000);
									
				if(!le->flags.open){
					suiDrawFilledRect(	dc,
										rx + crossSize / 2,
										ry,
										crossSize,
										crossSize * 2,
										0xff000000);
				}
				
				suiDrawFilledRect(	dc,
									rx + 1,
									ry + crossSize / 2 + 1,
									crossSize * 2 - 2,
									crossSize - 2,
									le->flags.open ? colorOpen : colorClosed);
			
				if(!le->flags.open){
					suiDrawFilledRect(	dc,
										rx + crossSize / 2 + 1,
										ry + 1,
										crossSize - 2,
										crossSize * 2 - 2,
										le->flags.open ? colorOpen : colorClosed);
				}
			}
			PERFINFO_AUTO_STOP();
		}

		if(!le->flags.destroyed){
			SUIListEntryDrawClippedData dcd = {0};
			
			dcd.le = le;
			dcd.sx = sx - xIndent;
			dcd.xIndent = xIndent;
			dcd.argbDefault = argbDefault;
			
			suiClipDrawContext(	dc,
								xIndent,
								topY,
								sx - xIndent,
								le->height.self,
								suiListEntryDrawClippedCallback,
								&dcd);
		}
									
		topY += le->height.self + 1;
	}

	if(le->flags.open){
		suiListEntryIteratorsInc(le);
		
		EARRAY_CONST_FOREACH_BEGIN(le->children, i, isize);
			SUIListEntry* leChild = le->children[i];

			if(!leChild->flags.hidden){
				suiListEntryDraw(	wd,
									leChild,
									dc,
									depth + 1,
									topY,
									sx,
									sy);

				topY += leChild->height.self +
						1 +
						(leChild->flags.open ? leChild->height.children : 0);
			}
		EARRAY_FOREACH_END;

		suiListEntryIteratorsDec(le);
	}
}

static S32 suiListHandleDraw(	SUIWindow* w,
								ListWD* wd,
								const SUIWindowMsg* msg)
{
	const SUIDrawContext*	dc = msg->msgData;
	S32 					sx = suiWindowGetSizeX(w);
	S32 					sy = suiWindowGetSizeY(w);
	
	PERFINFO_AUTO_START_FUNC();
	
	suiDrawFilledRect(dc, 0, 0, sx, sy, 0xff101010);
	
	PERFINFO_AUTO_START("drawListEntries", 1);
	{
		suiListEntryDraw(	wd,
							&wd->leRoot,
							dc,
							0,
							-wd->yTop,
							sx,
							sy);
	}
	PERFINFO_AUTO_STOP();
	
	#if DRAW_BOX_UNDER_LIST
	{
		// Draw green bar below list.
		
		suiDrawFilledRect(	dc,
							0,
							wd->leRoot.height.children - wd->yTop,
							sx,
							5,
							0xff80ff00);
	}
	#endif
	
	#if DRAW_BOX_WHILE_SCROLLING
	{
		if(wd->phScrolling){
			suiDrawFilledRect(	dc,
								10,
								10,
								100,
								100,
								0xff80ff00);
		}
	}
	#endif
	
	if(wd->phScrolling){
		S32 yDiff = wd->yTarget.yTop - wd->yTop;
		S32 scale = ABS(yDiff);
		S32 yTarget;
		S32 ySource;
		U32 argb;
		
		if(yDiff < 0){
			ySource = wd->yTop + sy - 1;
			yTarget = wd->yTarget.yTop + sy;
		}else{
			ySource = wd->yTop;
			yTarget = wd->yTarget.yTop - 1;
		}
		
		MIN1(scale, 255);

		argb = suiColorInterpAllRGB(0xff, 0x000000, 0xff0000, scale);
 
 		suiDrawRect(dc,
					0,
					wd->yTarget.yTop - wd->yTop,
					sx,
					sy,
					2,
					argb);
		
		FOR_BEGIN(i, 11);
			S32 yOffset = yDiff * i / 10;
			
			suiDrawFilledRect(	dc,
								0,
								ySource + yOffset - wd->yTop,
								sx,
								1,
								suiColorInterpAllRGB(0xff, 0x000000, argb, 0xff * i / 10));
		FOR_END;
	}
							
	PERFINFO_AUTO_STOP();

	return 1;
}

typedef struct SUIListFindUnderMouseResults {
	SUIListEntry*		le;
	S32					relativeY;
	S32					depth;
} SUIListFindUnderMouseResults;

static S32 suiListFindEntryUnderMouse(	SUIListFindUnderMouseResults* results,
										SUIListEntry* le,
										S32 topY,
										S32 mouseY,
										S32 depth)
{
	if(le->height.self){
		if(	mouseY >= topY &&
			(U32)(mouseY - topY) <= le->height.self)
		{
			results->le = le;
			results->relativeY = mouseY - topY;
			results->depth = depth;
			
			return 1;
		}
		
		topY += le->height.self + 1;
	}
	
	if(le->flags.open){
		suiListEntryIteratorsInc(le);
		
		EARRAY_CONST_FOREACH_BEGIN(le->children, i, isize);
			SUIListEntry* leChild = le->children[i];
			
			if(!leChild->flags.hidden){
				if(suiListFindEntryUnderMouse(	results,
												leChild,
												topY,
												mouseY,
												depth + 1))
				{
					suiListEntryIteratorsDec(le);
					return 1;
				}
				
				topY += leChild->height.self + 
						1 +
						(leChild->flags.open ? leChild->height.children : 0);
			}
		EARRAY_FOREACH_END;

		suiListEntryIteratorsDec(le);
	}
	
	return 0;
}

static S32 suiListHandleMouseButton(SUIWindow* w,
									ListWD* wd,
									const SUIWindowMsg* msg,
									U32 msgType)
{
	const SUIWindowMsgMouseButton*	md = msg->msgData;
	SUIListEntry*					le;
	
	if(0 && md->button & SUI_MBUTTON_MIDDLE){
		if(msg->msgType == SUI_WM_MOUSE_UP){
			wd->flags.isDraggingY = 0;
		}
		else if(FALSE_THEN_SET(wd->flags.isDraggingY)){
			wd->yDragAnchor = md->y + wd->yTop;

			suiWindowProcessingHandleDestroy(w, &wd->phScrolling);
		}
	}
	else if(!wd->flags.isDraggingY){
		SUIListFindUnderMouseResults	results = {0};
		SUIListMsgEntryMouse			mdSend = {0};
		
		suiListFindEntryUnderMouse(	&results,
									&wd->leRoot,
									-wd->yTop,
									md->y,
									0);

		le = results.le;
		
		if(le){
			mdSend.le.le = le;
			mdSend.le.userPointer = le->userPointer;

			mdSend.y = results.relativeY;
			mdSend.sy = le->height.self;
		}

		mdSend.button = md->button;
		mdSend.xIndent = indentForDepth(results.depth, wd->xIndentPerDepth);
		mdSend.x = md->x - mdSend.xIndent;
		mdSend.sx = suiWindowGetSizeX(w) - mdSend.xIndent;
		
		suiWindowPipeMsgSend(	le ? le->lec->wp : wd->wp,
								w,
								wd->userPointer,
								SUI_MSG_GROUP(suiList),
								msgType,
								&mdSend);
	}
	
	return 1;
}

static S32 suiListHandleMouseDown(	SUIWindow* w,
									ListWD* wd,
									const SUIWindowMsg* msg)
{
	return suiListHandleMouseButton(w, wd, msg, SUI_LIST_MSG_ENTRY_MOUSE_DOWN);
}

static S32 suiListHandleMouseUp(SUIWindow* w,
								ListWD* wd,
								const SUIWindowMsg* msg)
{
	return suiListHandleMouseButton(w, wd, msg, SUI_LIST_MSG_ENTRY_MOUSE_UP);
}

static void suiListSetEntryUnderMouse(	ListWD* wd,
										SUIListEntry* le)
{
	if(le != wd->leUnderMouse){
		if(wd->leUnderMouse){
			SUIListMsgEntryMouseLeave md = {0};
			
			md.le.le = wd->leUnderMouse;
			md.le.userPointer = md.le.le->userPointer;
			
			md.flags.enteredEmptySpace = !le;

			suiWindowPipeMsgSend(	md.le.le->lec->wp,
									wd->w,
									wd->userPointer,
									SUI_MSG_GROUP(suiList),
									SUI_LIST_MSG_ENTRY_MOUSE_LEAVE,
									&md);
		}
		
		wd->leUnderMouse = le;

		if(wd->leUnderMouse){
			SUIListMsgEntryMouseEnter md = {0};
			
			md.le.le = wd->leUnderMouse;
			md.le.userPointer = md.le.le->userPointer;
			
			suiWindowPipeMsgSend(	md.le.le->lec->wp,
									wd->w,
									wd->userPointer,
									SUI_MSG_GROUP(suiList),
									SUI_LIST_MSG_ENTRY_MOUSE_ENTER,
									&md);
		}
		
		suiWindowInvalidate(wd->w);
	}
}

static S32 suiListHandleMouseLeave(	SUIWindow* w,
									ListWD* wd,
									const SUIWindowMsg* msg)
{
	const SUIWindowMsgMouseLeave* mm = msg->msgData;
	
	suiListSetEntryUnderMouse(wd, NULL);

	return 1;
}

static S32 suiListHandleMouseMove(	SUIWindow* w,
									ListWD* wd,
									const SUIWindowMsg* msg)
{
	const SUIWindowMsgMouseMove*	md = msg->msgData;
	
	if(wd->flags.isDraggingY){
		if(1){
			suiScrollerSetPos(wd->wScroller, wd->yDragAnchor - md->y);
		}else{
			wd->yTop = wd->yDragAnchor - md->y;
			suiScrollerSetPos(wd->wScroller, wd->yTop);
		}
		
		MINMAX1(wd->yTop, 0, 999999999);
		
		suiWindowInvalidate(w);
	}else{
		SUIListFindUnderMouseResults	results = {0};
		SUIListMsgEntryMouse			mdSend = {0};

		suiListFindEntryUnderMouse(	&results,
									&wd->leRoot,
									-wd->yTop,
									md->y,
									0);

		suiListSetEntryUnderMouse(wd, results.le);
		
		if(results.le){
			mdSend.le.le = results.le;
			mdSend.le.userPointer = mdSend.le.le->userPointer;
			mdSend.y = results.relativeY;
			mdSend.sy = results.le->height.self;
		}

		mdSend.xIndent = indentForDepth(results.depth, wd->xIndentPerDepth);
		mdSend.x = md->x - mdSend.xIndent;
		mdSend.sx = suiWindowGetSizeX(w) - mdSend.xIndent;

		suiWindowPipeMsgSend(	mdSend.le.le ? mdSend.le.le->lec->wp : wd->wp,
								w,
								wd->userPointer,
								SUI_MSG_GROUP(suiList),
								SUI_LIST_MSG_ENTRY_MOUSE_MOVE,
								&mdSend);
	}
	
	return 1;
}

static S32 suiListHandleProcess(SUIWindow* w,
								ListWD* wd,
								const SUIWindowMsg* msg)
{
	const SUIWindowMsgMouseMove*	mm = msg->msgData;
	
	if(wd->phScrolling){
		S32 diff = wd->yTarget.yStart - wd->yTarget.yTop;
		U32	curTime = timeGetTime();
		U32 msDiff = MIN(curTime - wd->yTarget.timeStart, wd->yTarget.timeToFinish);
		F32 ratio = (F32)(wd->yTarget.timeToFinish - msDiff) /
					(F32)wd->yTarget.timeToFinish;
		
		wd->yTarget.timeLast = curTime;
		
		ratio = SQR(ratio);
		
		diff *= ratio;
		
		wd->yTop = wd->yTarget.yTop + diff;
		
		if(!diff){
			suiWindowProcessingHandleDestroy(w, &wd->phScrolling);
		}

		suiWindowInvalidate(w);
	}

	return 1;
}

static void suiListUpdateHeight(ListWD* wd){
	U32 sy = suiWindowGetSizeY(wd->w);
	
	if(wd->leRoot.height.children <= sy / 4){
		suiScrollerSetSize(wd->wScroller, 0);
	}else{
		suiScrollerSetSize(wd->wScroller, wd->leRoot.height.children - sy / 4);
	}
}

static S32 suiListHandleSizeChanged(SUIWindow* w,
									ListWD* wd,
									const SUIWindowMsg* msg)
{
	suiListUpdateHeight(wd);
	return 1;
}

static S32 suiListMsgHandler(	SUIWindow* w,
								ListWD* wd,
								const SUIWindowMsg* msg)
{
	SUI_WM_DEFAULT_HANDLERS_BEGIN(w, msg, ListWD, wd);
		SUI_WM_HANDLER(SUI_WM_CREATE,				suiListHandleCreate);
		SUI_WM_HANDLER(SUI_WM_DESTROY,				suiListHandleDestroy);
		SUI_WM_HANDLER(SUI_WM_DRAW,					suiListHandleDraw);
		SUI_WM_HANDLER(SUI_WM_MOUSE_DOWN,			suiListHandleMouseDown);
		SUI_WM_HANDLER(SUI_WM_MOUSE_LEAVE,			suiListHandleMouseLeave);
		SUI_WM_HANDLER(SUI_WM_MOUSE_DOUBLECLICK,	suiListHandleMouseDown);
		SUI_WM_HANDLER(SUI_WM_MOUSE_UP,				suiListHandleMouseUp);
		SUI_WM_HANDLER(SUI_WM_MOUSE_MOVE,			suiListHandleMouseMove);
		SUI_WM_HANDLER(SUI_WM_PROCESS,				suiListHandleProcess);
		SUI_WM_HANDLER(SUI_WM_SIZE_CHANGED,			suiListHandleSizeChanged);
	SUI_WM_HANDLERS_END;
	
	SUI_WM_HANDLERS_BEGIN(w, msg, suiScroller, ListWD, wd);
		SUI_WM_CASE(SUI_SCROLLER_MSG_POSITION_CHANGED){
			const SUIScrollerMsgPositionChanged* md = msg->msgData;
			
			if(md->pos != wd->yTop){
				wd->yTarget.yTop = md->pos;
				wd->yTarget.yStart = wd->yTop;
				
				wd->yTarget.timeToFinish = wd->flags.isDraggingY ? 100 : 300;

				if(wd->phScrolling){
					wd->yTarget.timeStart = wd->yTarget.timeLast;
				}else{
					wd->yTarget.timeStart = wd->yTarget.timeLast = timeGetTime();
					suiWindowProcessingHandleCreate(w, &wd->phScrolling);
				}
			}
		}
	SUI_WM_HANDLERS_END;
	
	return 0;
}

S32 suiListCreate(	SUIWindow** wOut,
					SUIWindow* wParent,
					const SUIListCreateParams* cp)
{
	return suiWindowCreate(	wOut,
							wParent,
							suiListMsgHandler,
							cp);
}

S32 suiListCreateBasic(	SUIWindow** wOut,
						SUIWindow* wParent,
						void* userPointer,
						SUIWindow* wReader)
{
	SUIListCreateParams cp = {0};
	
	cp.userPointer = userPointer;
	cp.wReader = wReader;
	
	return suiListCreate(	wOut,
							wParent,
							&cp);
}

S32 suiListSetPosY(	SUIWindow* w,
					S32 y)
{
	ListWD* wd;
	
	if(!suiWindowGetUserPointer(w, suiListMsgHandler, &wd)){
		return 0;
	}
	
	suiScrollerSetPos(wd->wScroller, y);
	
	return 1;
}

S32 suiListSetXIndentPerDepth(	SUIWindow* w,
								S32 xIndent)
{
	ListWD* wd;
	
	if(!suiWindowGetUserPointer(w, suiListMsgHandler, &wd)){
		return 0;
	}
	
	MAX1(xIndent, 0);
	
	if(xIndent != wd->xIndentPerDepth){
		wd->xIndentPerDepth = xIndent;
		suiWindowInvalidate(w);
	}
	
	return 1;
}

S32 suiListGetXIndentPerDepth(	SUIWindow* w,
								S32* xIndentOut)
{
	ListWD* wd;
	
	if(	!xIndentOut ||
		!suiWindowGetUserPointer(w, suiListMsgHandler, &wd))
	{
		return 0;
	}
	
	*xIndentOut = wd->xIndentPerDepth;
	
	return 1;
}

S32 suiListEntryClassCreate(SUIListEntryClass** lecOut,
							SUIWindow* w,
							SUIWindow* wReader)
{
	ListWD*				wd;
	SUIListEntryClass*	lec;
	SUIWindowPipe*		wp;
	
	if(	!wReader ||
		!lecOut ||
		!suiWindowGetUserPointer(w, suiListMsgHandler, &wd))
	{
		return 0;
	}
	
	if(!suiWindowPipeCreate(&wp, wReader, w)){
		return 0;
	}

	lec = callocStruct(SUIListEntryClass);
	
	suiCountInc(count.suiListEntryClass);

	eaPush(	&wd->lecs,
			lec);
			
	lec->wd = wd;
	lec->wp = wp;
			
	*lecOut = lec;
	
	return 1;
}

static void suiListEntryChildHeightInc(	SUIListEntry* le,
										U32 heightToAdd)
{
	for(; le; le = le->parent){
		le->height.children += heightToAdd;
		
		if(	!le->flags.open ||
			le->flags.hidden)
		{
			break;
		}
		
		if(!le->parent){
			suiListUpdateHeight(le->lec->wd);
			break;
		}
	}
}

static void suiListEntryChildHeightDec(	SUIListEntry* le,
										U32 heightToRemove)
{
	for(; le; le = le->parent){
		assert(le->height.children >= heightToRemove);
		
		le->height.children -= heightToRemove;
		
		if(	!le->flags.open ||
			le->flags.hidden)
		{
			break;
		}

		if(!le->parent){
			suiListUpdateHeight(le->lec->wd);
			break;
		}
	}
}

static S32 suiListEntryIsVisible(SUIListEntry* le){
	if(	!le ||
		le->flags.hidden)
	{
		return 0;
	}

	for(le = le->parent; le; le = le->parent){
		if(	le->flags.hidden ||
			!le->flags.open)
		{
			return 0;
		}
	}
	
	return 1;
}

static void suiListUpdatePosForEntryUnderMouse(	ListWD* wd,
												S32 prevSelectedEntryY)
{
	if(	prevSelectedEntryY >= 0 &&
		wd->leUnderMouse)
	{
		S32 newSelectedEntryY;
		
		if(suiListEntryGetPosY(wd->leUnderMouse, &newSelectedEntryY)){
			if(wd->phScrolling){
				wd->yTarget.yTop += newSelectedEntryY - prevSelectedEntryY;
				suiScrollerSetPos(wd->wScroller, wd->yTarget.yTop);
			}else{
				wd->yTop += newSelectedEntryY - prevSelectedEntryY;
				suiScrollerSetPos(wd->wScroller, wd->yTop);
			}
		}
	}
}

S32 suiListEntryCreate(	SUIListEntry** leOut,
						SUIListEntry* leParent,
						SUIListEntryClass* lec,
						void* leUserPointer)
{
	ListWD*			wd = SAFE_MEMBER(lec, wd);
	SUIListEntry*	le;
	S32				selectedEntryY = -1;

	if(	!wd
		||
		!leOut
		||
		leParent &&
		(	wd != SAFE_MEMBER2(leParent, lec, wd)
			||
			leParent->flags.destroyed))
	{
		return 0;
	}
	
	if(	leParent &&
		suiListEntryIsVisible(leParent) &&
		leParent->flags.open)
	{
		if(wd->leUnderMouse){
			suiListEntryGetPosY(wd->leUnderMouse, &selectedEntryY);
		}

		suiWindowInvalidate(wd->w);
	}
	
	le = callocStruct(SUIListEntry);
	
	suiCountInc(count.suiListEntry);
	
	le->lec = lec;
	lec->count++;
	le->userPointer = leUserPointer;
	
	if(leParent){
		le->parent = leParent;
		
		eaPush(	&leParent->children,
				le);
	}else{
		le->parent = &wd->leRoot;
		
		eaPush(	&wd->leRoot.children,
				le);
	}
	
	le->flags.open = 0;
	le->height.self = 20;
	
	suiListEntryChildHeightInc(	le->parent,
								le->height.self + 1);

	suiListUpdatePosForEntryUnderMouse(wd, selectedEntryY);
	
	*leOut = le;

	return 1;
}
						
static void suiListEntryFree(	SUIListEntry** leInOut,
								S32 line)
{
	SUIListEntry*	le = SAFE_DEREF(leInOut);
	ListWD*			wd = SAFE_MEMBER(le, lec->wd);
		
	if(!le){
		return;
	}
	
	#if 0
	{
		static S32 freeCount;
		
		printfColor(COLOR_BRIGHT|COLOR_RED,
					"Freeing 0x%8.8p, line %d, count %d\n",
					le,
					line,
					++freeCount);
					
		if(freeCount == 31){
			printf("");
		}
	}
	#endif
	
	if(wd->leUnderMouse == le){
		suiListSetEntryUnderMouse(wd, NULL);
	}

	// Remove from the lec.

	assert(SAFE_MEMBER(le->lec, count));
	le->lec->count--;
	le->lec = NULL;
	
	if(!le->flags.hidden){
		U32 height =	le->height.self +
						1 +
						(le->flags.open ? le->height.children : 0);
		
		suiListEntryChildHeightDec(	le->parent,
									height);
	}
	
	suiCountDec(count.suiListEntry);

	assert(!le->children);
	assert(!le->count.iterators);
	SAFE_FREE(*leInOut);
}
						
static void suiListEntrySendMsgDestroyed(SUIListEntry* le){
	SUIListMsgEntryDestroyed md = {0};

	md.le.le = le;
	md.le.userPointer = le->userPointer;
		
	le->flags.destroyed = 1;
	le->userPointer = NULL;
	
	suiWindowPipeMsgSend(	le->lec->wp,
							le->lec->wd->w,
							le->lec->wd->userPointer,
							SUI_MSG_GROUP(suiList),
							SUI_LIST_MSG_ENTRY_DESTROYED,
							&md);
}
						
static S32 suiListEntryDestroyInternal(	SUIListEntry* le,
										S32 destroySelf,
										S32 onlyDestroyedChildren)
{
	ListWD* wd = le->lec->wd;
	S32		childNeedsDestroy = 0;
	
	if(	destroySelf &&
		!le->flags.destroyed)
	{
		suiListEntrySendMsgDestroyed(le);
	}
	
	le->flags.childNeedsDestroy = 0;
	
	suiListEntryIteratorsInc(le);
	
	if(le->children){
		S32 selectedEntryY = -1;
		
		if(	le->flags.open &&
			suiListEntryIsVisible(le))
		{
			if(wd->leUnderMouse){
				suiListEntryGetPosY(wd->leUnderMouse, &selectedEntryY);
			}
		}

		EARRAY_CONST_FOREACH_BEGIN(le->children, i, isize);
			SUIListEntry* leChild = le->children[i];
			
			assert(leChild != le);
			assert(leChild->parent == le);
			
			if(	!onlyDestroyedChildren ||
				leChild->flags.destroyed)
			{
				if(suiListEntryDestroyInternal(leChild, 1, onlyDestroyedChildren)){
					eaRemove(&le->children, i);
					i--;
					if(!--isize){
						eaDestroy(&le->children);
					}
				}else{
					childNeedsDestroy = 1;
				}
			}
		EARRAY_FOREACH_END;

		suiListUpdatePosForEntryUnderMouse(wd, selectedEntryY);
	}
		
	assert(le->count.iterators);
	le->count.iterators--;

	le->flags.childNeedsDestroy = childNeedsDestroy;
	
	if(	!le->flags.childNeedsDestroy &&
		!le->count.iterators &&
		destroySelf)
	{
		suiListEntryFree(&le, __LINE__);
		return 1;
	}
	
	return 0;
}
						
static void suiListEntryIteratorsInc(SUIListEntry* le){
	le->count.iterators++;
}

static void suiListEntryIteratorsDec(SUIListEntry* le){
	assert(le->count.iterators);
	
	if(	!--le->count.iterators &&
		le->flags.childNeedsDestroy)
	{
		suiListEntryDestroyInternal(le, 0, 1);
	}
}
						
S32 suiListEntryDestroy(SUIListEntry** leInOut){
	SUIListEntry*	le = SAFE_DEREF(leInOut);
	ListWD*			wd = SAFE_MEMBER2(le, lec, wd);
	S32				selectedEntryY = -1;

	if(	!wd ||
		le->flags.destroyed)
	{
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	suiWindowInvalidate(wd->w);
	
	le->flags.destroyed = 1;
	le->userPointer = NULL;

	assert(le->parent);
	
	suiListEntryDestroyInternal(le, 0, 0);

	if(le->parent->count.iterators){
		// Can't destroy now.
		
		le->parent->flags.childNeedsDestroy = 1;
		*leInOut = NULL;
		
		PERFINFO_AUTO_STOP();
		
		return 1;
	}
	
	if(suiListEntryIsVisible(le)){
		if(	wd->leUnderMouse &&
			wd->leUnderMouse != le)
		{
			suiListEntryGetPosY(wd->leUnderMouse, &selectedEntryY);
		}
	}

	if(eaFindAndRemove(&le->parent->children, le) < 0){
		assert(0);
	}
	
	if(!le->flags.hidden){
		U32 height =	le->height.self +
						1 +
						(le->flags.open ? le->height.children : 0);
		
		suiListEntryChildHeightDec(	le->parent,
									height);

		suiWindowInvalidate(wd->w);
	}
	
	if(!eaSize(&le->parent->children)){
		assert(!le->parent->height.children);
		eaDestroy(&le->parent->children);
	}

	suiListUpdatePosForEntryUnderMouse(wd, selectedEntryY);

	le->parent = NULL;

	// Free.
	
	suiListEntryFree(leInOut, __LINE__);
	
	PERFINFO_AUTO_STOP();

	return 1;
}

S32 suiListEntrySetUserPointer(	SUIListEntry* le,
								void* userPointer)
{
	if(!le){
		return 0;
	}

	le->userPointer = userPointer;
	
	return 1;
}

S32 suiListEntryGetPosY(SUIListEntry* le,
						S32* yOut)
{
	SUIListEntry*	leCur = le;
	SUIListEntry*	leParent = SAFE_MEMBER(le, parent);
	S32				y = 0;
	
	if(	!le ||
		le->flags.hidden ||
		!yOut)
	{
		return 0;
	}
	
	while(leParent){
		if(	!leParent->flags.open ||
			leParent->flags.hidden)
		{
			return 0;
		}

		y += leParent->height.self + 1;

		EARRAY_CONST_FOREACH_BEGIN(leParent->children, i, isize);
			SUIListEntry* leChild = leParent->children[i];
			
			if(leChild == leCur){
				break;
			}

			if(leChild->flags.hidden){
				continue;
			}
			
			y +=	leChild->height.self +
					1 +
					(leChild->flags.open ? leChild->height.children : 0);
		EARRAY_FOREACH_END;
		
		leCur = leParent;
		leParent = leParent->parent;
	}
	
	*yOut = y;
	
	return 1;
}

S32 suiListEntryGetOpenState(	SUIListEntry* le,
								S32* openOut)
{
	if(	!openOut ||
		!SAFE_MEMBER2(le, lec, wd))
	{
		return 0;
	}
	
	*openOut = le->flags.open;
	
	return 1;
}

S32 suiListEntrySetOpenState(	SUIListEntry* le,
								S32 open)
{
	ListWD* wd = SAFE_MEMBER2(le, lec, wd);
	
	if(!wd){
		return 0;
	}
	
	open = !!open;
	
	if(le->flags.open != open){
		S32 selectedEntryY = -1;

		if(suiListEntryIsVisible(le)){
			suiWindowInvalidate(wd->w);
			
			if(wd->leUnderMouse){
				suiListEntryGetPosY(wd->leUnderMouse, &selectedEntryY);
			}
		}

		le->flags.open = open;
		
		if(!le->flags.hidden){
			if(open){
				suiListEntryChildHeightInc(	le->parent,
											le->height.children);
			}else{
				suiListEntryChildHeightDec(	le->parent,
											le->height.children);
			}
			
			suiWindowInvalidate(le->lec->wd->w);
		}

		suiListUpdatePosForEntryUnderMouse(wd, selectedEntryY);

		suiListUpdateHeight(wd);
	}
	
	return 1;
}

S32 suiListEntryGetHiddenState(	SUIListEntry* le,
								S32* hiddenOut)
{
	if(	!hiddenOut ||
		!SAFE_MEMBER2(le, lec, wd))
	{
		return 0;
	}
	
	*hiddenOut = le->flags.hidden;
	
	return 1;
}

S32 suiListEntrySetHiddenState(	SUIListEntry* le,
								S32 hidden)
{
	ListWD* wd = SAFE_MEMBER2(le, lec, wd);

	if(!wd){
		return 0;
	}
	
	hidden = !!hidden;
	
	if(le->flags.hidden != hidden){
		U32 height =	le->height.self +
						1 +
						(le->flags.open ? le->height.children : 0);
		S32 selectedEntryY = -1;
		
		if(	hidden &&
			wd->leUnderMouse)
		{
			SUIListEntry* leCheck;
			
			for(leCheck = wd->leUnderMouse; leCheck; leCheck = leCheck->parent){
				if(leCheck == le){
					suiListSetEntryUnderMouse(wd, NULL);
					break;
				}
			}
		}
		
		if(suiListEntryIsVisible(le)){
			suiWindowInvalidate(wd->w);
			
			if(wd->leUnderMouse){
				suiListEntryGetPosY(wd->leUnderMouse, &selectedEntryY);
			}
		}

		le->flags.hidden = hidden;
		
		if(!le->flags.hidden){
		    suiListEntryChildHeightInc(	le->parent,
										height);
		}else{
			suiListEntryChildHeightDec(	le->parent,
										height);
		}
			
		suiListUpdatePosForEntryUnderMouse(wd, selectedEntryY);

		suiListUpdateHeight(wd);
	}
	
	return 1;
}

S32 suiListEntryGetHeight(	SUIListEntry* le,
							U32* heightOut)
{
	if(	!heightOut ||
		!SAFE_MEMBER2(le, lec, wd))
	{
		return 0;
	}
	
	*heightOut = le->height.self;
	
	return 1;
}

S32 suiListEntrySetHeight(	SUIListEntry* le,
							U32 height)
{
	ListWD* wd = SAFE_MEMBER2(le, lec, wd);

	if(!wd){
		return 0;
	}
	
	if(height != le->height.self){
		S32 selectedEntryY = -1;
		
		if(suiListEntryIsVisible(le)){
			suiWindowInvalidate(wd->w);
			
			if(wd->leUnderMouse){
				suiListEntryGetPosY(wd->leUnderMouse, &selectedEntryY);
			}
		}

		if(!le->flags.hidden){
			if(height > le->height.self){
				suiListEntryChildHeightInc(	le->parent,
											height - le->height.self);
			}else{
				suiListEntryChildHeightDec(	le->parent,
											le->height.self - height);
			}
		}

		le->height.self = height;

		suiListUpdatePosForEntryUnderMouse(wd, selectedEntryY);

		suiListUpdateHeight(wd);
	}
	
	return 1;	
}