

#pragma once

#include "stdtypes.h"

typedef struct SUIWindow SUIWindow;

enum {
	SUI_SCROLLER_MSG_POSITION_CHANGED,
};

typedef struct SUIScrollerMsgPositionChanged {
	S32					pos;
} SUIScrollerMsgPositionChanged;

S32 suiScrollerCreate(	SUIWindow** wOut,
						SUIWindow* wParent,
						SUIWindow* wReader);

S32 suiScrollerSetSize(	SUIWindow* w,
						S32 size);
						
S32 suiScrollerSetPos(	SUIWindow* w,
						S32 pos);

S32 suiScrollerGetPos(	SUIWindow* w,
						S32* posOut,
						S32* sizeOut);
						
