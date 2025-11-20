

#pragma once

#include "stdtypes.h"

typedef struct SUIWindow SUIWindow;

enum {
	SUI_FRAME_MSG_DESTROYED,
};

typedef struct SUIFrameMsgDestroyed {
	void*		userPointer;
} SUIFrameMsgDestroyed;

typedef struct SUIFrameCreateParams {
	const char*	name;
	void*		userPointer;
	SUIWindow*	wReader;
} SUIFrameCreateParams;

// Frame.

S32 suiFrameWindowCreate(	SUIWindow** wOut,
							SUIWindow* wParent,
							const SUIFrameCreateParams* cp);

S32 suiFrameWindowSetClientWindow(	SUIWindow* w,
									SUIWindow* wClient);

