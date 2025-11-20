
#pragma once

#include "stdtypes.h"

typedef struct SUIWindow SUIWindow;

typedef struct SUIButtonMsgDraw {
	const SUIDrawContext*	dc;
	S32						sx;
	S32						sy;
	
	struct {
		U32					isUnderMouse : 1;
	} flags;
} SUIButtonMsgDraw;

enum {
	SUI_BUTTON_MSG_BUTTON_PRESSED,
	SUI_BUTTON_MSG_BUTTON_RELEASED,
	SUI_BUTTON_MSG_DRAW,

	SUI_BUTTON_MSG_BUTTON_PRESSED_RIGHT,
	SUI_BUTTON_MSG_BUTTON_PRESSED_MIDDLE,
};

S32	suiButtonCreate(SUIWindow** wOut,
					SUIWindow* wParent,
					const char* text,
					SUIWindow* wReader);

S32 suiButtonSetText(	SUIWindow* w,
						const char* text);

S32	suiButtonFitToText(	SUIWindow* w,
						S32 xMinSize,
						S32 ySize);

S32 suiButtonSetSendDrawMsg(SUIWindow* w,
							S32 enabled);
							
S32 suiButtonSetTextColor(	SUIWindow* w,
							U32 rgb);