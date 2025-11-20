#pragma once
GCC_SYSTEM
//
// MultiEditPanel.h
//


#ifndef NO_EDITORS

#include "UIFileBrowser.h"

#define ME_STATE_HIDDEN              0x0001
#define ME_STATE_NOT_EDITABLE        0x0002
#define ME_STATE_NOT_PARENTABLE      0x0004
#define ME_STATE_NOT_REVERTABLE      0x0008
#define ME_STATE_NOT_GROUP_EDITABLE  0x0010
#define ME_STATE_LABEL               0x001E

typedef struct MEFileBrowseData {
	char *pcBrowseTitle;
	char *pcTopDir;
	char *pcStartDir;
	char *pcExtension;
	UIBrowserMode eMode;
} MEFileBrowseData;

#endif