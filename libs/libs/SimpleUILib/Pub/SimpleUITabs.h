
#pragma once

#include "stdtypes.h"

typedef struct SUIWindow SUIWindow;

enum {
	SUI_TABS_MSG_TAB_SELECTED,
};

S32 suiTabsCreate(	SUIWindow** wOut,
					SUIWindow* wParent,
					const char* text,
					SUIWindow* wReader);
