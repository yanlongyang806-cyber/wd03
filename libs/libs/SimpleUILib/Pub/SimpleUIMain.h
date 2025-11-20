
#pragma once

#include "stdtypes.h"

typedef struct SUIMainWindow	SUIMainWindow;
typedef struct SUIWindow		SUIWindow;

// --- SUIMainWindow Functions --------------------------------------------------------------------

typedef enum SUIMainWindowStyle {
	SUI_MWSTYLE_BORDER				= 1 << 0,
	SUI_MWSTYLE_TASKBAR_BUTTON		= 1 << 1,
} SUIMainWindowStyle;

typedef struct SUIMainWindowCreateParams {
	const char*		name;
	U32				style;
} SUIMainWindowCreateParams;

S32		suiMainWindowCreate(SUIMainWindow** mwOut,
							const SUIMainWindowCreateParams* params);

S32		suiMainWindowCreateBasic(	SUIMainWindow** mwOut,
									const char* name,
									U32 style);

void	suiMainWindowDestroy(SUIMainWindow** mwInOut);

S32		suiMainWindowProcess(SUIMainWindow* mw);
void	suiMainWindowProcessUntilDone(SUIMainWindow* mw);

S32		suiMainWindowAddChild(SUIMainWindow* mw, SUIWindow* wChild);

void	suiMainWindowNotifyIconSet(	SUIMainWindow* mw,
									char letter,
									U32 rgb,
									const char* toolTip);
								
typedef void (*SUIMainWindowEventCallback)(	SUIMainWindow* mw,
											void* userPointer,
											void* hEvent);

S32		suiMainWindowAddEvent(	SUIMainWindow* mw,
								void* hEvent,
								void* userPointer,
								SUIMainWindowEventCallback callback);

S32		suiMainWindowMinimize(SUIMainWindow* mw);

S32		suiMainWindowSetName(	SUIMainWindow* mw,
								const char* name);
