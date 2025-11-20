/***************************************************************************



***************************************************************************/

#ifndef UI_TABS_H
#define UI_TABS_H
GCC_SYSTEM

#include "UICore.h"

//////////////////////////////////////////////////////////////////////////
// A tabbed "notebook"-style widget.

typedef struct Message Message;
typedef struct UIScrollArea UIScrollArea;
typedef struct UITabGroup UITabGroup;

// An individual tab.
typedef struct UITab
{
	char *pchTitle_USEACCESSOR;
	REF_TO(Message) hTitleMessage_USEACCESSOR;
	AtlasTex* pRIcon;
	
	UIActivationFunc cbContext;
	UserData pContextData;
	UIWidgetGroup eaChildren;
	UITabGroup *pTabGroup;
	UIScrollArea *pScrollArea;
} UITab;

SA_RET_NN_VALID UITab *ui_TabCreate(SA_PARAM_NN_STR const char *pchTitle);
SA_RET_NN_VALID UITab *ui_TabCreateWithScrollArea(SA_PARAM_NN_STR const char *pchTitle);
void ui_TabFree(SA_PRE_NN_VALID SA_POST_P_FREE UITab *pTab);

void ui_TabSetTitleString(SA_PARAM_NN_VALID UITab *pTab, SA_PARAM_NN_STR const char *pchTitle);
void ui_TabSetTitleMessage(SA_PARAM_NN_VALID UITab *pTab, SA_PARAM_NN_STR const char *pchTitle);
const char* ui_TabGetTitle(SA_PARAM_NN_VALID UITab *pTab);

void ui_TabSetRIcon(SA_PARAM_NN_VALID UITab *pTab, SA_PARAM_OP_STR const char *pchRIcon);

void ui_TabAddChild(SA_PARAM_NN_VALID UITab *pTab, NN_UIAnyWidget *pChild);
void ui_TabRemoveChild(SA_PARAM_NN_VALID UITab *pTab, NN_UIAnyWidget *pChild);

UIScrollArea *ui_TabGetScrollArea(SA_PARAM_NN_VALID UITab *pTab);

typedef enum UITabStyle 
{
	UITabStyleButtons,
	UITabStyleFolders,
	UITabStyleFoldersWithBorder,
} UITabStyle;

// A list of tabs, with one actively selected and displaying.
typedef struct UITabGroup
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE);
	UITab **eaTabs;
	UITab *pActive;

	// Called when the user switches tabs.
	UIActivationFunc cbChanged;
	UserData pChangedData;

	// If true, the tabs are all the width of the widest tab. Otherwise,
	// the tabs are the width of their titles.
	bool bEqualWidths;

	bool bFitToSize;

	UITabStyle eStyle;

	F32 fTabSpacing;
	F32 fTabXPad;
	F32 fTabYPad;

	unsigned bVerticalTabs : 1;
	unsigned bNeverEmpty : 1;
	unsigned bFocusOnClick : 1;
} UITabGroup;

SA_RET_NN_VALID UITabGroup *ui_TabGroupCreate(F32 fX, F32 fY, F32 fW, F32 fH);
void ui_TabGroupInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UITabGroup *pTabs, F32 fX, F32 fY, F32 fW, F32 fH);
void ui_TabGroupFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UITabGroup *pTabs);

void ui_TabGroupHeaderSizes(SA_PARAM_NN_VALID UITabGroup *pTabs, F32 fScale, F32 fWidth, SA_PRE_OP_FREE SA_POST_OP_VALID F32 *pfWidth, SA_PRE_OP_FREE SA_POST_OP_VALID F32 *pfHeight);
F32 ui_TabGroupTickTabs(SA_PARAM_NN_VALID UITabGroup *pTabs, F32 fX, F32 fY, F32 fWidth, F32 fHeight, F32 fTabWidth, F32 fScale);
F32 ui_TabGroupDrawTabs(SA_PARAM_NN_VALID UITabGroup *pTabs, F32 fX, F32 fY, F32 fWidth, F32 fHeight, F32 fTabWidth, F32 fScale, F32 fZ);

void ui_TabGroupSetActive(SA_PARAM_NN_VALID UITabGroup *pTabs, SA_PARAM_NN_VALID UITab *pTab);
void ui_TabGroupSetActiveIndex(SA_PARAM_NN_VALID UITabGroup *pTabs, S32 iTab);
SA_RET_OP_VALID UITab *ui_TabGroupGetActive(SA_PARAM_NN_VALID UITabGroup *pTabs);
S32 ui_TabGroupGetActiveIndex(SA_PARAM_NN_VALID UITabGroup *pTabs);

void ui_TabGroupAddTab(SA_PARAM_NN_VALID UITabGroup *pTabs, SA_PARAM_NN_VALID UITab *pTab);
void ui_TabGroupInsertTab(SA_PARAM_NN_VALID UITabGroup *pTabs, SA_PARAM_NN_VALID UITab *pTab, int idx);
void ui_TabGroupAddTabFirst(SA_PARAM_NN_VALID UITabGroup *pTabs, SA_PARAM_NN_VALID UITab *pTab);
void ui_TabGroupRemoveTab(SA_PARAM_NN_VALID UITabGroup *pTabs, SA_PARAM_NN_VALID UITab *pTab);
void ui_TabGroupRemoveAllTabs(SA_PARAM_NN_VALID UITabGroup *pTabs);

void ui_TabGroupSetChangedCallback(SA_PARAM_NN_VALID UITabGroup *pTabs, UIActivationFunc cbChanged, UserData pChangedData);

void ui_TabGroupTick(SA_PARAM_NN_VALID UITabGroup *pTabs, UI_PARENT_ARGS);
void ui_TabGroupDraw(SA_PARAM_NN_VALID UITabGroup *pTabs, UI_PARENT_ARGS);
bool ui_TabGroupInput(SA_PARAM_NN_VALID UITabGroup *pTabs, SA_PARAM_NN_VALID KeyInput *pInput);

void ui_TabGroupSetSpacing(SA_PARAM_NN_VALID UITabGroup *pTabs, F32 fSpacing);
void ui_TabGroupSetTabPadding(SA_PARAM_NN_VALID UITabGroup *pTabs, F32 fXPad, F32 fYPad);
void ui_TabGroupSetStyle(SA_PARAM_NN_VALID UITabGroup *pTabs, UITabStyle eStyle);

#endif
