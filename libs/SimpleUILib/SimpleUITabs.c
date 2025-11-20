#include "SimpleUI.h"
#include "SimpleUITabs.h"
#include "stdtypes.h"

SUI_MSG_GROUP_FUNCTION_DEFINE(suiTabs, "SUITabs");

typedef struct SUITabsCreateParams {
	const char*					text;
	SUIWindow*					wReader;
} SUITabsCreateParams;

typedef struct TabsWD {
	char*						text;
	
	SUIWindowPipe*				wp;
	SUIWindowProcessingHandle*	ph;
} TabsWD;

static S32 tabsMsgHandler(	SUIWindow* w,
							TabsWD* wd,
							const SUIWindowMsg* msg);

static S32 suiTabsHandleCreate(	SUIWindow* w,
									TabsWD* wd,
									const SUIWindowMsg* msg)
{
	const SUITabsCreateParams* cp = msg->msgData;
	
	callocStruct(wd, TabsWD);
	suiWindowSetUserPointer(w, tabsMsgHandler, wd);
	
	return 1;
}

static S32 suiTabsHandleMouseDown(SUIWindow* w,
									TabsWD* wd,
									const SUIWindowMsg* msg)
{
	const SUIWindowMsgMouseButton* md = msg->msgData;
	
	return 1;
}

static S32 suiTabsHandleMouseUp(	SUIWindow* w,
									TabsWD* wd,
									const SUIWindowMsg* msg)
{
	return 1;
}

static S32 suiTabsHandleDraw(	SUIWindow* w,
								TabsWD* wd,
								const SUIWindowMsg* msg)
{
	return 1;
}

static S32 suiTabsHandleProcess(	SUIWindow* w,
									TabsWD* wd,
									const SUIWindowMsg* msg)
{
	return 1;
}

static S32 tabsMsgHandler(	SUIWindow* w,
							TabsWD* wd,
							const SUIWindowMsg* msg)
{
	SUI_WM_DEFAULT_HANDLERS_BEGIN(w, msg, TabsWD, wd);
		SUI_WM_HANDLER(SUI_WM_CREATE,				suiTabsHandleCreate);
		SUI_WM_HANDLER(SUI_WM_PROCESS,				suiTabsHandleProcess);
		SUI_WM_HANDLER(SUI_WM_DRAW,					suiTabsHandleDraw);
		SUI_WM_HANDLER(SUI_WM_MOUSE_DOWN,			suiTabsHandleMouseDown);
		SUI_WM_HANDLER(SUI_WM_MOUSE_DOUBLECLICK,	suiTabsHandleMouseDown);
		SUI_WM_HANDLER(SUI_WM_MOUSE_UP,				suiTabsHandleMouseUp);
	SUI_WM_HANDLERS_END;
	
	return 0;
}

S32 suiTabsCreate(	SUIWindow** wOut,
					SUIWindow* wParent,
					const char* text,
					SUIWindow* wReader)
{
	SUITabsCreateParams cp = {0};
	
	cp.wReader = wReader;
	cp.text = text;

	return suiWindowCreate(	wOut,
							wParent,
							tabsMsgHandler,
							&cp);	
}

