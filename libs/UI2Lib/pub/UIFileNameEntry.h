#ifndef UI_FILENAME_ENTRY_H
#define UI_FILENAME_ENTRY_H
GCC_SYSTEM

#include "UICore.h"
#include "UIFileBrowser.h"

typedef struct UITextEntry UITextEntry;
typedef struct UIButton UIButton;
typedef struct UIWindow UIWindow;

typedef struct UIFileNameEntry
{
	UIWidget widget;

	char *pchBrowseTitle;
	char *pchTopDir;
	char *pchStartDir;
	char *pchDefaultExt;
	UIBrowserMode eMode;

	UITextEntry *pEntry;
	UIButton *pButton;

	UIActivationFunc cbChanged;
	UserData pChangedData;
	UIWindow *pFileBrowser;

} UIFileNameEntry;

SA_RET_NN_VALID UIFileNameEntry *ui_FileNameEntryCreate(SA_PARAM_NN_STR const char *pchFileName, char *pchBrowseTitle, char *pchTopDir, char *pchStartDir, char *pchDefaultExt, UIBrowserMode eMode);
void ui_FileNameEntryFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIFileNameEntry *pFNEntry);
void ui_FileNameEntryTick(SA_PARAM_NN_VALID UIFileNameEntry *pFNEntry, UI_PARENT_ARGS);
void ui_FileNameEntryDraw(SA_PARAM_NN_VALID UIFileNameEntry *pFNEntry, UI_PARENT_ARGS);

void ui_FileNameEntrySetBrowseValues(SA_PARAM_NN_VALID UIFileNameEntry *pFNEntry, char *pchBrowseTitle, char *pchTopDir, char *pchStartDir, char *pchDefaultExt, UIBrowserMode eMode);

const char *ui_FileNameEntryGetFileName(SA_PARAM_NN_VALID UIFileNameEntry *pFNEntry);

void ui_FileNameEntrySetFileName(SA_PARAM_NN_VALID UIFileNameEntry *pFNEntry, SA_PARAM_NN_STR const char *pchFileName);
void ui_FileNameEntrySetFileNameAndCallback(SA_PARAM_NN_VALID UIFileNameEntry *pFNEntry, SA_PARAM_NN_STR const char *pchFileName);

void ui_FileNameEntrySetChangedCallback(SA_PARAM_NN_VALID UIFileNameEntry *pFNEntry, UIActivationFunc cbChanged, UserData pChangedData);

#endif