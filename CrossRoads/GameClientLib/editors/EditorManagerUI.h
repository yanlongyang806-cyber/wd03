/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef __EDITORMANAGERUI_H__
#define __EDITORMANAGERUI_H__
GCC_SYSTEM

#ifndef NO_EDITORS

#include "EditorManagerUI.h"
#include "EditorManagerUIMenus.h"
#include "EditorManagerUIPanels.h"
#include "EditorManagerUIPickers.h"
#include "EditorManagerUIToolbars.h"

typedef struct BasicTexture BasicTexture;
typedef struct EMPicker EMPicker;
typedef struct EMEditor EMEditor;
typedef struct EMEditorDoc EMEditorDoc;
typedef struct EMToolbar EMToolbar;
typedef struct EMPanel EMPanel;
typedef struct GroupTrackerChildSelect GroupTrackerChildSelect;
typedef struct ResourceInfo ResourceInfo;
typedef struct UIWidget UIWidget;
typedef struct UIExpander UIExpander;
typedef struct UIMenu UIMenu;
typedef struct UIMenuItem UIMenuItem;
typedef struct UITabGroup UITabGroup;
typedef struct UIWindow UIWindow;
typedef struct UIPane UIPane;

typedef enum GfxHeadshotObjectCamType GfxHeadshotObjectCamType;

/******
* The Editor Manager UI consists of several different components, all of which can be optionally utilized
* on a per-editor or per-document basis.  The Editor Manager currently manages menus and toolbars on a per-
* editor basis, and it manages windows and side panels on a per-document basis.  The associated
* header files should be consulted for the list of available API's.  What resides here are the API's
* related to popping up particular file or document related modal dialogs, API's for managing document
* and editor UI's (namely, displaying/hiding them according to whether the doc has focus), and the main
* functions that initialize the entire editor manager UI and update appropriate documents/editors once
* every frame.
******/

#define age_color_old 0xa67d00ff
#define age_color_new 0x69f821ff
#define age_delta_to_old 14

#define EM_UI_SIDEBAR_MARGIN_WIDTH 10.0f

#define TOOLBAR_VIS_PREF "ToolbarVisibleState"

/********************
* DIALOGS
********************/
void emDialogReloadPrompt(SA_PARAM_NN_VALID EMEditorDoc *doc);
void emDialogProgressRefresh(SA_PARAM_NN_VALID EMEditorDoc *doc);


/********************
* SIDEBAR
********************/
void emSidebarShow(bool show);

/********************
* WINDOW MANAGEMENT
********************/
void emWinMinimize(SA_PARAM_NN_VALID UIWindow *win);
void emWinHide(SA_PARAM_NN_VALID UIWindow *window);
void emWinShow(SA_PARAM_NN_VALID UIWindow *window);
void emWinHideAll(SA_PARAM_OP_VALID EMEditorDoc *doc);
void emWinShowAll(SA_PARAM_OP_VALID EMEditorDoc *doc);

void emDocApplyWindowScale(EMEditorDoc *doc, F32 scale);
void emDocApplyWindowPrefs(EMEditorDoc *doc, bool force_position);
void emDocSaveWindowPrefs(EMEditorDoc *doc);


/********************
* DOCUMENT MANAGEMENT
********************/
UITabGroup *emDocTabGroupCreate(void);
void emDocTabCreate(SA_PARAM_OP_VALID EMEditorDoc *doc);
UIPane *emMinPaneCreate(void);
void emShowDocumentUI(SA_PARAM_OP_VALID EMEditorDoc *doc, SA_PARAM_OP_VALID EMEditor *editor, int show, int give_focus);
void emRemoveDocumentUI(SA_PARAM_OP_VALID EMEditorDoc *doc);


/********************
* WORKSPACES
********************/
#endif
AUTO_STRUCT;
typedef struct EMWorkspace
{
	char name[1024];

#ifndef NO_EDITORS
	AST_STOP
	EMEditor **editors;
	EMEditorDoc **open_docs;
	UITabGroup *tab_group;
	UIPane *min_pane;
#endif
} EMWorkspace;
#ifndef NO_EDITORS

SA_RET_OP_VALID EMWorkspace *emWorkspaceGet(SA_PARAM_NN_STR const char *name, bool create);
SA_RET_OP_VALID EMWorkspace *emWorkspaceFindDoc(SA_PARAM_OP_VALID EMEditorDoc *doc);
void emWorkspaceSetActive(SA_PARAM_OP_VALID EMWorkspace *workspace, SA_PARAM_OP_VALID EMEditorDoc *doc);


/********************
* CAMERA
********************/
void emFreecamApplyPrefs(void);


/********************
* MAIN
********************/
void emUIInit(void);
void emDocUIOncePerFrame(EMEditorDoc *doc);
void emUIOncePerFrame(void);

void emObjectPickerRefresh(void);

/*************************
* OBJECT LIBRARY PREVIEW
*************************/

AUTO_STRUCT;
typedef struct ObjectLibraryPreviewParams
{
	bool useAlpha;
	int camType;								// GfxHeadshotObjectCamType
	bool useNearPlane;							// Enables the following field
	F32 nearPlane; 								AST(NAME(NearPlaneOverride)) // For top-down view
	const char *skyName;						AST(NAME(SkyOverride) POOL_STRING)
	bool enableShadows;
} ObjectLibraryPreviewParams;
extern ParseTable parse_ObjectLibraryPreviewParams[];
#define TYPE_parse_ObjectLibraryPreviewParams ObjectLibraryPreviewParams

void objectLibraryGetPreview(const ResourceInfo *res_info, const void* pData, const ObjectLibraryPreviewParams *params, F32 size, BasicTexture** out_tex, Color* out_mod_color);

void emClearPreviews(void);

#endif // NO_EDITORS

#endif // __EDITORMANAGERUI_H__
