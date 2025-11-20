/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef __EDITORMANAGERUIPANELS_H__
#define __EDITORMANAGERUIPANELS_H__
GCC_SYSTEM

#ifndef NO_EDITORS

typedef struct EMEditor EMEditor;
typedef struct EMEditorDoc EMEditorDoc;
typedef struct EMPanel EMPanel;
typedef struct UITabGroup UITabGroup;
typedef void UIAnyWidget;

/******
* Panels are the various expanders (organized by tabs) that appear on the sidebar.  Panels are
* document-specific, as various actions on a document can radically change which panels are shown.
* Panels are to replace any and all non-modal-dialog windows as much as possible to save screen
* real estate for users.
*
* To create a panel, use emPanelCreate, specifying the tab where it will go and the name shown on the
* expander.  Widgets can be added or removed with emPanelAddChild and emPanelRemoveChild, respectively.
* The added widgets behave much like they would when added as a child of any other, typical UI widget,
* so widget positions and dimensions can be modified via the usual means.  The update_height parameter,
* if set, attempts to update the height of the entire panel according to the positions and heights of
* the panel's children.  Otherwise, you can set the height manually upon panel creation or with
* emPanelSetHeight.  Once you've created the panel, you can add it to a document by pushing it onto
* the document's em_panels EArray.  The panels appear in the EArray order.  Tabs are created as
* necessary, appearing from left to right as they are created.  Panels themselves appear in top-down
* order.
*
* Panels can be dynamically added, removed, or rearranged among the editor document's EArray, and the
* UI will automatically update itself accordingly.  Panel contents can also be disabled (if, instead,
* you do not wish to remove it) with emPanelSetActive.  Specific manipulation of panel expanders and
* the expander groups should NOT be performed, as EMPanels should have a uniform behavior at that
* level.
******/

typedef struct EMMsgLogFlash
{
	char *msg;
	int frames_left;
} EMMsgLogFlash;

// global panels
void emMsgLogRefresh(void);
void emMapLayerListRefresh(void);

void emSidebarSetScale(F32 scale);
void emSidebarApplyCurrentScale(void);

// main
void emPanelsInit(void);
void emPanelsShow(SA_PARAM_OP_VALID EMEditorDoc *doc, SA_PARAM_OP_VALID EMEditor *editor);
void emTabChanged(UITabGroup *tab_group, void *unused);

// commands
void emTabFocus(int tab_idx);

#endif // NO_EDITORS

#endif // __EDITORMANAGERUIPANELS_H__