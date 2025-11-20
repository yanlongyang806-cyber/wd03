/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef __EDITORMANAGERUITOOLBARS_H__
#define __EDITORMANAGERUITOOLBARS_H__
GCC_SYSTEM

#ifndef NO_EDITORS

typedef struct EMToolbar EMToolbar;
typedef struct UIPane UIPane;
typedef void UIAnyWidget;
typedef struct UIWidget UIWidget;

/******
* Toolbars are editor-specific panes that reside in a central toolbar location.  Toolbars generally
* contain useful buttons tied to important or frequently used editor functionality.  Also residing
* in the central toolbar area is an asset-manager-specific toolbar that is tied to core Editor Manager
* flows, such as creating new docs, saving docs, and opening docs.
*
* To add a toolbar to an editor, use emToolbarCreate to create a toolbar.  Various widgets can be
* added to the toolbar with emToolbarAddChild (and removed with emToolbarRemoveChild).  Added widgets
* behave much in the same way as if they were added as a child to any other UI widget, so the positions
* and dimensions of the added widgets can be altered via the usual means.  When adding or removing
* children, the update_width flag can be set to true to automatically attempt to determine and set the
* width of the toolbar according to the widths of the widgets in the toolbar.  If you wish to set this
* manually, use emToolbarSetWidth.  To associate the toolbar with the editor, add the created toolbar
* to the editor's toolbars EArray.
*
* We are imposing the standard that toolbars do not change across documents belonging to the same
* editor to avoid user confusion.  There may, however, be a desire to disable (i.e. gray out)
* particular toolbars for certain documents.  emToolbarSetActive can be used in the got_focus_func
* callback to selectively enable/disable toolbars.
******/

/********************
* SPECIFIC TOOLBARS
********************/
void emUICameraButtonRefresh(void);
SA_RET_NN_VALID EMToolbar *emToolbarCreateCameraToolbar(void);

/********************
* MAIN
********************/
SA_RET_NN_VALID UIPane *emToolbarPaneCreate(void);
void emEditorToolbarStack(SA_PARAM_OP_VALID EMEditor *editor);
void emEditorToolbarDisplay(SA_PARAM_OP_VALID EMEditor *editor);

UIWidget* emToolbarGetPaneWidget(EMToolbar* toolbar);

#endif // NO_EDITORS

#endif // __EDITORMANAGERUITOOLBARS_H__