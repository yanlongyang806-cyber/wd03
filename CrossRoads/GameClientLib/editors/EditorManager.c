/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "EditorManager.h"
#include "EditorManagerPrivate.h"
#include "UtilitiesLib.h"

#ifndef NO_EDITORS
#include "EditorManagerUtils.h"
#include "EditorManagerOptions.h"
#include "EditorManagerUIPrompts.h"
#include "EditorManagerUIMotD.h"
#include "EditorPrefs.h"
#include "EditorShared.h"
#include "EditLibUIUtil.h"
#include "FolderCache.h"
#include "gclBaseStates.h"
#include "gclKeyBind.h"
#include "GlobalStateMachine.h"
#include "MRUList.h"
#include "StringCache.h"
#include "inputlib.h"
#include "inputMouse.h"
#include "GraphicsLib.h"
#include "GfxCommandParse.h"
#include "WorldGrid.h"
#include "StringUtil.h"
#include "sharedmemory.h"
#include "gclCommandParse.h"
#include "EditorManagerPrivate_h_ast.h"
#include "wininclude.h"
#include "EditorShared.h"
#include "UGCEditorMain.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

/******
* FORWARD DECLARATIONS
******/
static void emInit(void);
static void emEditorInit(EMEditor *editor);
static EMEditorDoc *emOpenFileInternal(EMEditor *editor, const char *name, const char *type);
static EMTaskStatus emCloseDocInternal(EMEditorDoc *doc, EMEditorSubDoc *sub_doc, bool force);

/******
* LOCAL STRUCTS
******/
typedef struct EMQueuedFunc
{
	void (*func1)(void *data);
	void (*func2)(void *data1, void *data2);
	bool (*check_func)(void *data);

	void (*status_func)(EMTaskStatus, void *data);
	EMTaskStatus (*statuscheck_func)(void *data);

	void (*status_func2)(EMTaskStatus, void *data, void *data2);
	EMTaskStatus (*statuscheck_func2)(void *data, void *data2);

	void *data1, *data2;
	void *checkdata1, *checkdata2;
	U32 frame_delay;
} EMQueuedFunc;

typedef struct EMQueuedMapChangeFunc
{
	void (*func)(void *data, bool is_reset);
	void *data;
} EMQueuedMapChangeFunc;


/********************
* GLOBALS
********************/
// this is the global asset manager state struct
EMInternalData em_data = {0};

// queued function callbacks
static EMQueuedFunc **enter_editor_callbacks;
static EMQueuedFunc **queued_functions;
static EMQueuedMapChangeFunc **mapchange_callbacks;

// map-tracking data
static char current_map_filename[MAX_PATH];
static int current_reset_count = -1;
static int current_reload_count = -1;


/********************
* UTIL
********************/
/******
* This function is a wrapper to the progress dialog function in EditLibUIUtil, setting
* the dialog texts to appropriate values.
* PARAMS:
*   editor - EMEditor that is loading
*   func - function to call when loading is complete
*   params - parameter passed to func
******/
static void emInitProgressDialogCreate(EMEditor *editor, void (*func)(void*), void *params)
{
	char title_text[1024];
	char msg_text[1024];

	sprintf(title_text, "Initializing %s...", editor->editor_name);
	sprintf(msg_text, "Please wait while the %s initializes...", editor->editor_name);
	elUIProgressDialog(title_text, msg_text, editor->can_init_func, editor->can_init_data, func, params, 1000);
}

/******
* This function queues a function call, which is invoked at the end of a frame.
* PARAMS:
*   func - function to invoke; takes one parameter
*   data - parameter to pass to the function
*	frame_delay - U32 how many frames to wait before calling function; 0 means
*                 the function will be called at the end of the current frame
******/
void emQueueFunctionCallEx(emFunc func, void *data, U32 frame_delay)
{
	EMQueuedFunc *qf = calloc(1, sizeof(*qf));
	qf->func1 = func;
	qf->func2 = NULL;
	qf->data1 = data;
	qf->frame_delay = frame_delay;
	eaPush(&queued_functions, qf);
}

/******
* This function queues a function call, which is invoked at the end of a frame.
* PARAMS:
*   func - function to invoke; takes two parameters
*   data1 - first parameter to pass to the function
*   data2 - second parameter to pass to the function
*	frame_delay - U32 how many frames to wait before calling function; 0 means
*                 the function will be called at the end of the current frame
******/
void emQueueFunctionCall2Ex(emFunc2 func, void *data1, void *data2, U32 frame_delay)
{
	EMQueuedFunc *qf = calloc(1, sizeof(*qf));
	qf->func2 = func;
	qf->data1 = data1;
	qf->data2 = data2;
	qf->frame_delay = frame_delay;
	eaPush(&queued_functions, qf);
}

/******
* This function queues a function call that is not invoked until the specified conditional
* function returns true.  The condition is checked every frame until the true condition is found.
* PARAMS:
*   func - function to invoke; takes one parameter
*   data - parameter which will be passed to func
*   check_func - function used to check whether to invoke func
*   check_data - parameter passed to check_func
*   timeout - int maximum number of frames to wait for true condition; -1 will never timeout
******/
void emQueueFunctionCallCond(emFunc func, void *data, bool (*check_func)(void *data), void *check_data, int timeout)
{
	EMQueuedFunc *qf = calloc(1, sizeof(*qf));
	qf->func1 = func;
	qf->check_func = check_func;
	qf->data1 = data;
	qf->checkdata1 = check_data;
	qf->frame_delay = timeout;
	eaPush(&queued_functions, qf);
}

/******
* This function queues a function call that is not invoked until the specified status function
* function returns success or fail.  The condition is checked every frame until it succeeds or fails
* PARAMS:
*   func - function to invoke; takes status code and one parameter
*   data - parameter which will be passed to func
*   check_func - function used to check whether to invoke func
*   check_data - parameter passed to check_func
*   timeout - int maximum number of frames to wait for true condition; -1 will never timeout
******/
void emQueueFunctionCallStatus(void (*func)(EMTaskStatus status, void *data), void *data, EMTaskStatus (*check_func)(void *data), void *check_data, int timeout)
{
	EMQueuedFunc *qf = calloc(1, sizeof(*qf));
	qf->status_func = func;
	qf->statuscheck_func = check_func;
	qf->data1 = data;
	qf->checkdata1 = check_data;
	qf->frame_delay = timeout;
	eaPush(&queued_functions, qf);
}

/******
* This function queues a function call that is not invoked until the specified status function
* function returns success or fail.  The condition is checked every frame until it succeeds or fails
* PARAMS:
*   func - function to invoke; takes status code and one parameter
*   data/data2 - parameters which will be passed to func
*   check_func - function used to check whether to invoke func
*   check_data/check_data2 - parameters passed to check_func
*   timeout - int maximum number of frames to wait for true condition; -1 will never timeout
******/
void emQueueFunctionCallStatus2(void (*func)(EMTaskStatus status, void *data, void *data2), void *data, void *data2, EMTaskStatus (*check_func)(void *data, void *data2), void *check_data, void *check_data2, int timeout)
{
	EMQueuedFunc *qf = calloc(1, sizeof(*qf));
	qf->status_func2 = func;
	qf->statuscheck_func2 = check_func;
	qf->data1 = data;
	qf->data2 = data2;
	qf->checkdata1 = check_data;
	qf->checkdata2 = check_data2;
	qf->frame_delay = timeout;
	eaPush(&queued_functions, qf);
}


/******
* This function adds a callback to the list of callbacks that are invoked every time
* the map changes.
* PARAMS:
*   func - callback function
*   data - data passed to the callback when it is invoked
******/
void emAddMapChangeCallback(void (*func)(void *data, bool is_reset), void *data)
{
	EMQueuedMapChangeFunc *qf = calloc(1, sizeof(*qf));
	qf->func= func;
	qf->data = data;
	eaPush(&mapchange_callbacks, qf);
}

/******
* This function adds a callback that is invoked whenever the user enters editor mode from
* game mode.
* PARAMS:
*	func - function to invoke when user enters editor mode; takes one parameter
*   data - parameter passed to function
******/
void emAddEditorEntryCallback(void (*func)(void *data), void *data)
{
	EMQueuedFunc *qf = calloc(1, sizeof(*qf));
	qf->func1 = func;
	qf->func2 = NULL;
	qf->data1 = data;
	eaPush(&enter_editor_callbacks, qf);
}

/******
* This function is used for the CHECK_EM_FUNC macro to support edit and continue for functions.
* PARAMS:
*   oldfunc - AnyFunc old function pointer
*   newfunc - AnyFunc new function pointer
******/
void emReplaceCodeFuncs(AnyFunc oldfunc, AnyFunc newfunc)
{
	int i, j;
	for (i = eaSize(&em_data.editors)-1; i >= 0; i--)
	{
		EMEditor *editor = em_data.editors[i];
		for (j = 0; j < ARRAY_SIZE(editor->functions_array); j++)
		{
			if (editor->functions_array[j] == oldfunc)
				editor->functions_array[j] = newfunc;
		}
	}
	for (i = eaSize(&em_data.pickers)-1; i >= 0; i--)
	{
		EMPicker *picker = em_data.pickers[i];
		for (j = 0; j < ARRAY_SIZE(picker->functions_array); j++)
		{
			if (picker->functions_array[j] == oldfunc)
				picker->functions_array[j] = newfunc;
		}
	}
}

/******
* This function determines the position and size of the space that is not consumed by panes.
* PARAMS:
*   x - F32 pointer where x position is returned
*   y - F32 pointer where y position is returned
*   w - F32 pointer where width is returned
*   h - F32 pointer where height is returned
******/
void emGetCanvasSize(F32 *x, F32 *y, F32 *w, F32 *h)
{
	int width, height;
	F32 scale = 1.f; // global scale from somewhere?
	gfxGetActiveDeviceSize(&width, &height);
	*x = g_ui_State.viewportMin[0];
	*y = g_ui_State.viewportMin[1];
	*w = g_ui_State.viewportMax[0] - g_ui_State.viewportMin[0];
	*h = g_ui_State.viewportMax[1] - g_ui_State.viewportMin[1];
}

AUTO_COMMAND ACMD_NAME("EM.Documentation");
void emEditorDocumentation(void)
{
	if (em_data.current_editor && em_data.current_editor->editor_name[0]) {
		int str_len, i;
		char wiki_path[256];

		sprintf(wiki_path, "Core/%s", em_data.current_editor->editor_name);

		str_len = (int)strlen(wiki_path);
		for ( i=5; i < str_len; i++ ) {
			if(wiki_path[i] == ' ')
				wiki_path[i] = '+';
		}

		openCrypticWikiPage(wiki_path);
	}
}

AUTO_COMMAND ACMD_NAME("EM.Screenshot");
void emAssetScreenshot(int show_ui)
{
	EMEditorDoc *doc = emGetActiveEditorDoc();
	if (doc)
	{
		char ss_name[MAX_PATH];
		char *c = ss_name;

		// take out slashes from display name
		strcpy(ss_name, doc->doc_display_name);
		while (*c)
		{
			if (*c == '/' || *c == '\\')
				*c = '-';
			c++;
		}
		if (show_ui)
			gfxSaveJPGScreenshotWithUI(ss_name);
		else
			gfxSaveJPGScreenshot3dOnly(ss_name);
	}
}

/******
* This function returns the editor window scale value.
* PARAMS:
*   editor - EMEditor
* RETURNS:
*   F32 editor scale value; defaults to editor manager scale value
******/
F32 emGetEditorScale(EMEditor *editor)
{
	return EditorPrefGetFloat(editor->editor_name, "Option", "Scale",
			EditorPrefGetFloat("Editor Manager", "Option", "Scale", 1.0f));
}

/******
* This function returns the sidebar scale value.
* RETURNS:
*   F32 sidebar scale value; defaults to 0.75
******/
F32 emGetSidebarScale(void)
{
	return EditorPrefGetFloat("Sidebar", "Option", "Scale", 0.75f);
}

/********************
* INITIALIZATION
********************/
/******
* This function initializes a particular camera for use in editing.  If the camera
* is already initialized, this function does nothing.
* PARAMS:
*   camera - GfxCameraController address to initialize with the camera.
******/
void emCameraInit(GfxCameraController **camera)
{
	if (!(*camera))
	{
		*camera = calloc(1, sizeof(**camera));
		gfxInitCameraController(*camera, gfxDefaultEditorCamFunc, NULL);
	}
}

/******
* This function returns the world camera.
* Useful if you are hiding the world but need to do a sky override or
* other stuff..
******/
GfxCameraController *emGetWorldCamera(void)
{
	return em_data.worldcam;
}

/******
* This function initializes an editor.
* PARAMS:
*   editor - EMEditor to initialize
******/
static void emEditorInit(EMEditor *editor)
{
	EMRegisteredType *type = NULL;
	KeyBindProfile *profile = NULL;
	char kb_path[MAX_PATH];
	int i;
	bool add_file_toolbar;

	// if editor is already initialized, do nothing
	if (editor->inited)
		return;

	// ensure default type is a registered file type and registered with the editor; assert otherwise
	if (editor->default_type && (!stashFindPointer(em_data.registered_file_types, editor->default_type, &type) || strcmpi(type->editor_name, editor->editor_name) != 0))
		assert(0);

	// enable editor mode on shared memory if editor requires it
	if (editor->edit_shared_memory)
		sharedMemoryEnableEditorMode();

	// get keybind profile for the specified name
	editor->keybinds = StructCreate(parse_KeyBindProfile);
	if (editor->keybinds_name)
		profile = keybind_FindProfile(editor->keybinds_name);
	if (profile)
		StructCopyAll(parse_KeyBindProfile, profile, editor->keybinds);
	else
	{
		editor->keybinds->ePriority = InputBindPriorityDevelopment;
		editor->keybinds->pchName = StructAllocString(editor->editor_name);
	}

	// initialize camera
	if (editor->hide_world || editor->force_editor_cam)
	{
		emCameraInit(&editor->camera);
		if (editor->camera_func)
			editor->camera->camera_func = editor->camera_func;
		else if (!editor->use_em_cam_keybinds)
			editor->camera->camera_func = gfxEditorCamFunc;
		gfxCameraControllerSetSkyOverride(editor->camera, "default_sky", NULL);
	}

	// call initialization function
	if (editor->init_func)
		editor->init_func(editor);

	// load custom keybinds
	if (editor->keybinds)
	{
		sprintf(kb_path, "%s/editor/%s.userbinds", fileLocalDataDir(), editor->editor_name);
		if (fileExists(kb_path) && EditorPrefGetInt(editor->editor_name, "Keybinds", "Version", 0) >= editor->keybind_version)
		{
			eaDestroyStruct(&editor->keybinds->eaBinds, parse_KeyBind);
			keybind_LoadProfile(editor->keybinds, kb_path);
		}
		else
		{
			keybind_SaveProfile(editor->keybinds, kb_path);
			EditorPrefStoreInt(editor->editor_name, "Keybinds", "Version", editor->keybind_version);
		}
	}

	// refresh keybinds on menu items
	emMenuItemsRefreshBinds(editor);

	// If editor provided no toolbars, give it the same default we have if there is no editor open
	add_file_toolbar = !eaSize(&editor->toolbars);
	if ((!editor->hide_world && !editor->force_editor_cam) || editor->use_em_cam_keybinds)
		eaInsert(&editor->toolbars, em_data.title.camera_toolbar, 0);
	if (add_file_toolbar && !editor->hide_file_toolbar)
		eaInsert(&editor->toolbars, em_data.title.global_toolbar, 0);

	// create toolbar order
	for (i = 0; i < eaSize(&editor->toolbars); i++)
		eaiPush(&editor->toolbar_order, i);

	// set initialization flag
	editor->inited = 1;
}

static void emMenuGlobalScaleChangedCallback(UISliderTextEntry *pSliderTextEntry, bool bFinished, UIWindow *pWindow)
{
	F32 scale = ui_SliderGetValue(pSliderTextEntry->pSlider);
	EMEditorDoc *doc = emGetActiveEditorDoc();
	g_ui_State.scale = scale*0.01f;
	UI_WIDGET(pWindow)->scale = 1/g_ui_State.scale;
	ui_WidgetSetPosition(UI_WIDGET(pWindow), 100 * UI_WIDGET(pWindow)->scale,
		100 * UI_WIDGET(pWindow)->scale);
	if (doc)
		emDocApplyWindowScale(doc, EditorPrefGetFloat(doc->editor->editor_name, "Option", "Scale", 
				EditorPrefGetFloat("Editor Manager", "Option", "Scale", 1.0f)));
	emSidebarApplyCurrentScale();
}

static void emMenuCurrentEditorScaleChangedCallback(UISliderTextEntry *pSliderTextEntry, bool bFinished, UIWindow *pWindow)
{
	F32 scale = ui_SliderGetValue(pSliderTextEntry->pSlider) * 0.01f;
	EMEditorDoc *doc = emGetActiveEditorDoc();
	if (doc)
		emDocApplyWindowScale(doc, scale);
}

static void emMenuSidebarScaleChangedCallback(UISliderTextEntry *pSliderTextEntry, bool bFinished, UIWindow *pWindow)
{
	F32 scale = ui_SliderGetValue(pSliderTextEntry->pSlider) * 0.01f;
	emSidebarSetScale(scale);
}

static void emSetGlobalScaleOkClicked(UIButton *pButton, UIWindow *pWindow)
{
	EditorPrefStoreFloat("Editor Manager", "Option", "Scale", g_ui_State.scale);
	ui_WindowClose(pWindow);
}

static void emSetGlobalScaleCancelClicked(UIButton *pButton, UIWindow *pWindow)
{
	EMEditorDoc *doc = emGetActiveEditorDoc();
	g_ui_State.scale = EditorPrefGetFloat("Editor Manager", "Option", "Scale", 1.0f);
	if (doc)
		emDocApplyWindowScale(doc, EditorPrefGetFloat(doc->editor->editor_name, "Option", "Scale", 
				EditorPrefGetFloat("Editor Manager", "Option", "Scale", 1.0f)));
	emSidebarApplyCurrentScale();
	ui_WindowClose(pWindow);
}

static void emSetCurrentEditorScaleOkClicked(UIButton *pButton, UIWindow *pWindow)
{
	EMEditorDoc *doc = emGetActiveEditorDoc();
	if (doc)
	{
		EditorPrefStoreFloat(doc->editor->editor_name, "Option", "Scale", doc->editor->scale);
		emDocApplyWindowScale(doc, doc->editor->scale);
	}
	ui_WindowClose(pWindow);
}

static void emSetCurrentEditorScaleCancelClicked(UIButton *pButton, UIWindow *pWindow)
{
	EMEditorDoc *doc = emGetActiveEditorDoc();
	if (doc)
		emDocApplyWindowScale(doc, EditorPrefGetFloat(doc->editor->editor_name, "Option", "Scale", 
				EditorPrefGetFloat("Editor Manager", "Option", "Scale", 1.0f)));
	ui_WindowClose(pWindow);
}

static void emSetSidebarScaleOkClicked(UIButton *pButton, UIWindow *pWindow)
{
	EditorPrefStoreFloat("Sidebar", "Option", "Scale", em_data.sidebar.scale);
	ui_WindowClose(pWindow);
}

static void emSetSidebarScaleCancelClicked(UIButton *pButton, UIWindow *pWindow)
{
	emSidebarApplyCurrentScale();
	ui_WindowClose(pWindow);
}

AUTO_COMMAND ACMD_NAME("EM.SetAutosaveInterval");
void emSetAutosaveInterval(U32 autosave_internal)
{
	EMEditorDoc *doc = emGetActiveEditorDoc();
	if(SAFE_MEMBER(doc, editor))
		doc->editor->autosave_interval = autosave_internal;
}

AUTO_COMMAND ACMD_NAME("EM.GetAutosaveInterval");
U32 emGetAutosaveInterval()
{
	EMEditorDoc *doc = emGetActiveEditorDoc();
	if(SAFE_MEMBER(doc, editor))
		return doc->editor->autosave_interval;
	return 0;
}

AUTO_COMMAND ACMD_NAME("EM.SetScale");
void emMenuSetScale(int scaleMenu)
{
	EMEditorDoc *doc = emGetActiveEditorDoc();
	UISliderTextEntry *pSliderTextEntry = NULL;
	UIButton *pButton = NULL;
	UIWindow *pWindow = NULL;
	UISliderChangeFunc changedCallback = NULL;
	UIActivationFunc okClickedCallback = NULL;
	UIActivationFunc cancelClickedCallback = NULL;
	F32 curScale = 1.0f;
	char buf[32];

	switch (scaleMenu)
	{
	case 0:
		changedCallback = emMenuGlobalScaleChangedCallback;
		okClickedCallback = emSetGlobalScaleOkClicked;
		cancelClickedCallback = emSetGlobalScaleCancelClicked;
		curScale = g_ui_State.scale * 100;
		break;
	case 1:
		assert(doc);
		changedCallback = emMenuCurrentEditorScaleChangedCallback;
		okClickedCallback = emSetCurrentEditorScaleOkClicked;
		cancelClickedCallback = emSetCurrentEditorScaleCancelClicked;
		curScale = EditorPrefGetFloat(doc->editor->editor_name, "Option", "Scale", 
						EditorPrefGetFloat("Editor Manager", "Option", "Scale", 1.0f)) * 100.0f;
		break;
	case 2:
		changedCallback = emMenuSidebarScaleChangedCallback;
		okClickedCallback = emSetSidebarScaleOkClicked;
		cancelClickedCallback = emSetSidebarScaleCancelClicked;
		curScale = EditorPrefGetFloat("Sidebar", "Option", "Scale", 1.0f) * 100.0f;
		break;
	}

	safe_ftoa(curScale, buf);

	pWindow = ui_WindowCreate("Set Scale", 100, 100, 160, 55);
	pSliderTextEntry = ui_SliderTextEntryCreate(buf, 65.0f, 135.0f, 5, 5, 135);
	ui_SliderTextEntrySetChangedCallback(pSliderTextEntry, changedCallback, pWindow);
	ui_SliderSetPolicy(pSliderTextEntry->pSlider, UISliderContinuous);
	pSliderTextEntry->pSlider->step = 5.0f;
	ui_FloatSliderSetValueAndCallback(pSliderTextEntry->pSlider, curScale);
	ui_WindowAddChild(pWindow, pSliderTextEntry);

	pButton = ui_ButtonCreate("OK", 0, 0, okClickedCallback, pWindow);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, 0, 0, 0, UIBottomRight);
	ui_WindowAddChild(pWindow, pButton);

	pButton = ui_ButtonCreate("Cancel", 0, 0, cancelClickedCallback, pWindow);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, 0, 0, 0, UIBottomLeft);
	ui_WindowAddChild(pWindow, pButton);

	UI_WIDGET(pWindow)->scale = 1/g_ui_State.scale;

	ui_WindowShow(pWindow);
}


/********************
* EDITOR/PICKER REGISTRATION
********************/
/******
* This function validates an editor and sets its valid flag accordingly.
* PARAMS:
*   editor - EMEditor to validate
* RETURNS:
*   bool indicating whether editor passed validation
******/
bool emIsEditorOk(EMEditor *editor)
{
	// if editor was already validated, return its previous validation results
	if (editor->valid)
		return editor->valid - 1;

	// validate that the editor is allowed in outsource mode
	if (em_data.outsource_mode)
	{
		if (editor->allow_outsource)
		{
			editor->valid = 2;
			return true;
		}

		editor->valid = 1;
		return false;
	}
	if (isProductionEditMode())
	{
		editor->valid = 1;
		return false;
	}

	// all editors are valid in development mode except when in outsource mode
	if (isDevelopmentMode())
	{
		editor->valid = 2;
		return true;
	}

	// validate presence of server data
	if (editor->requires_server_data && !hasServerDir_NotASecurityCheck())
	{
		editor->valid = 1;
		return false;
	}

	if (!editor->close_func)
	{
		Errorf("Editor %s does not have a close function", editor->editor_name);
		editor->valid = 1;
		return false;
	}

	editor->valid = 2;
	return true;
}

/******
* This function validates a picker and sets its valid flag appropriately.
* PARAMS:
*   picker - EMPicker to validate
* RETURNS:
*   bool indicating whether picker passed validation
******/
static int emIsPickerOk(EMPicker *picker)
{
	if (picker->valid)
		return picker->valid - 1;

	if (picker->requires_server_data && !hasServerDir_NotASecurityCheck())
	{
		picker->valid = 1;
		return 0;
	}

	if (em_data.outsource_mode)
	{
		if (picker->allow_outsource)
		{
			picker->valid = 2;
			return 1;
		}

		picker->valid = 1;
		return 0;
	}

	picker->valid = 2;
	return 1;
}

/******
* This function is used to register an editor with the Asset Manager, which will make it
* available for use.  This must be called after initializing an EMEditor struct and
* populating the appropriate values.  This should generally be called in an AUTO_RUN'ed
* function.
* PARAMS:
*   editor - EMEditor to register with the Asset Manager
******/
void emRegisterEditor(EMEditor *editor)
{
	// initialize the registered editor stash
	if (!em_data.registered_editors)
		em_data.registered_editors = stashTableCreateWithStringKeys(64, StashDefault);

	// validate/adjust the editor
	if (editor->type == EM_TYPE_MULTIDOC)
	{
		// doesn't make sense to allow multi-doc editors without, well, having multiple docs open
		editor->allow_multiple_docs = true;
		// multi-doc editors should not show the world
		editor->hide_world = true;
	}

	if (!editor->editor_name[0])
	{
		// attempted to register unnamed editor
		assert(0);
		return;
	}

	// register editor with asset manager
	if (!stashAddPointer(em_data.registered_editors, editor->editor_name, editor, false))
	{
		// attempted to register editor twice (under same name)
		assert(0);
		return;
	}

	// set primary editor to be loaded on startup
	if (editor->primary_editor)
	{
		// attempted to register a second primary editor
		if (em_data.primary_editor)
			assert(0);
		else
			em_data.primary_editor = editor;
	}

	eaPush(&em_data.editors, editor);
}

/******
* This function registers a file type with an editor for the purpose of being able to determine
* which editor should be used to open particular files. (By default, the filename extension is
* assumed to be the registered file type.)
* PARAMS:
*   type - string type to register
*   display_name - string display name corresponding to the type
*   editor_name - string name of the editor to register with the type; if NULL, type will be
*				  deregistered
*   preview_func - EMPreviewFunc invoked by the asset viewer to display an asset of the registered
*                  type; must be specified in order to include the associated asset type in
*                  an asset list
******/
void emRegisterFileTypeEx(const char *type, const char *display_name, const char *editor_name, EMPreviewFunc preview_func)
{
	EMRegisteredType *registered_type = NULL;
	EMEditor *editor;
	const char *lastp;

	// initialize file type registration stash table
	if (!em_data.registered_file_types)
		em_data.registered_file_types = stashTableCreateWithStringKeys(64, StashDeepCopyKeys);

	lastp = strrchr(type, '.');
	if (lastp)
		type = lastp+1;

	// if editor name is not specified, deregister the type
	if (!editor_name)
	{
		if (stashRemovePointer(em_data.registered_file_types, type, &registered_type))
		{
			editor = emGetEditorByName(registered_type->editor_name);
			if (editor)
				eaFindAndRemove(&editor->registered_types, registered_type);
			StructDestroy(parse_EMRegisteredType, registered_type);
		}
		return;
	}

	// if type is already registered, overwrite the associated editor
	if (stashFindPointer(em_data.registered_file_types, type, &registered_type))
	{
		editor = emGetEditorByName(registered_type->editor_name);
		if (editor)
			eaFindAndRemove(&editor->registered_types, registered_type);
		StructFreeString(registered_type->type_name);
		StructFreeString(registered_type->display_name);
		StructFreeString(registered_type->editor_name);
	}
	else
	{
		registered_type = calloc(1, sizeof(*registered_type));
		stashAddPointer(em_data.registered_file_types, type, registered_type, false);
	}

	editor = emGetEditorByName(editor_name);

	// register the specified editor and preview function
	registered_type->type_name = StructAllocString(type);
	registered_type->editor_name = StructAllocString(editor_name);
	registered_type->display_name = display_name ? StructAllocString(display_name) : NULL;
	registered_type->preview_func = preview_func;
	if (editor)
		eaPush(&editor->registered_types, registered_type);
}


static int emFindResourceStatus(EMEditor *editor, const char *name)
{
	int i;
	for(i=eaSize(&editor->resource_status)-1; i>=0; --i) 
	{
		if (stricmp(name, editor->resource_status[i]->name) == 0)
		{
			return i;
		}
	}
	return -1;
}

/******
* This function returns the specified state.
* PARAMS:
*   editor - The editor
*   name - An object name
* RETURNS:
*   EMResourceState set for the name, or EMRES_STATE_NONE if no entry is for the name.
******/
EMResourceState emGetResourceState(EMEditor *editor, const char *name)
{
	int index = emFindResourceStatus(editor, name);
	if (index >= 0)
	{
		return editor->resource_status[index]->state;
	}
	return EMRES_STATE_NONE;
}

/******
* This function returns the specified state's data pointer.
* PARAMS:
*   editor - The editor.
*   name - An object name.
* RETURNS:
*   The data set for the name, or NULL if no entry is for the name.
******/
void *emGetResourceStateData(EMEditor *editor, const char *name)
{
	int index = emFindResourceStatus(editor, name);
	if (index >= 0)
	{
		return editor->resource_status[index]->data;
	}
	return NULL;
}

/******
* This function sets the state for an object.  If set to EM_OBJ_NONE, it clears the entry.
* Otherwise, it does not modify the data pointer.
* PARAMS:
*   editor - The editor.
*   name - An object name.
*   state - The state to set.
******/
void emSetResourceState(EMEditor *editor, const char *name, EMResourceState state)
{
	int index = emFindResourceStatus(editor, name);
	if (index >= 0)
	{
		if (state == EMRES_STATE_NONE)
		{
			free(editor->resource_status[index]->name);
			free(editor->resource_status[index]);
			eaRemove(&editor->resource_status, index);
		}
		else
		{
			editor->resource_status[index]->state = state;
		}		
	} 
	else if (state != EMRES_STATE_NONE)
	{
		EMResourceStatus *newStatus = calloc(sizeof(EMResourceStatus), 1);
		newStatus->state = state;
		newStatus->name = strdup(name);
		eaPush(&editor->resource_status, newStatus);
	}
}

/******
* This function sets the state and data for an object.  If set to EM_OBJ_NONE, it clears the entry.
* PARAMS:
*   editor - The editor.
*   name - An object name.
*   state - The state to set.
*   data - The data to set.
******/
void emSetResourceStateWithData(EMEditor *editor, const char *name, EMResourceState state, void *data)
{
	int index = emFindResourceStatus(editor, name);
	if (index >= 0)
	{
		if (state == EMRES_STATE_NONE)
		{
			free(editor->resource_status[index]->name);
			free(editor->resource_status[index]);
			eaRemove(&editor->resource_status, index);
		}
		else
		{
			editor->resource_status[index]->state = state;
			editor->resource_status[index]->data = data;
		}		
	} 
	else if (state != EMRES_STATE_NONE)
	{
		EMResourceStatus *newStatus = calloc(sizeof(EMResourceStatus), 1);
		newStatus->state = state;
		newStatus->name = strdup(name);
		newStatus->data = data;
		eaPush(&editor->resource_status, newStatus);
	}
}

/******
* Processes the standard behaviors for state changes during save.
* PARAMS:
*   editor - The editor
*   name - The document/object name (called pRefData from the dictionary change callback)
*   status - The address of where to put status
* RETURNS:
*   True if the status was updated and false if it was not.
******/
bool emHandleSaveResourceState(SA_PARAM_NN_VALID EMEditor *editor, SA_PARAM_NN_STR const char *name, EMTaskStatus *status)
{
	EMResourceState eState = emGetResourceState(editor, name);
	assertmsg(status, "Must pass in status pointer");
	switch (eState)
	{
		xcase EMRES_STATE_SAVE_SUCCEEDED:
			emSetResourceState(editor, name, EMRES_STATE_NONE);
			*status = EM_TASK_SUCCEEDED;
			return true;
		xcase EMRES_STATE_SAVE_FAILED:
			emSetResourceState(editor, name, EMRES_STATE_NONE);
			*status = EM_TASK_FAILED;
			return true;
		xcase EMRES_STATE_LOCK_FAILED:
			emSetResourceState(editor, name, EMRES_STATE_NONE);
			*status = EM_TASK_FAILED;
			return true;
		xcase EMRES_STATE_LOCKING_FOR_SAVE:
			*status = EM_TASK_INPROGRESS;
			return true;
		xcase EMRES_STATE_SAVING:
			*status = EM_TASK_INPROGRESS;
			return true;
		default:
			// EMRES_DELETE_SUCCEEDED
			// EMRES_STATE_LOCK_SUCCEEDED
			// EMRES_STATE_LOCKING_FOR_DELETE
			// EMRES_STATE_NONE
			return false;
	}
}


/******
* This function is queued when a dictionary changes after registering with
* the "emAutoHandleDictionaryStateChange" function.  It is queued so it only runs
* once per frame regardless of the number of dictionary changes.
* It checks all open docs to see if they are edited on disk while they have changes.
*/
static void emUpdateAfterDictChange(EMEditor *editor)
{
	int i, j;

	editor->dict_changed = false;

	for( i = 0; i < eaSize(&editor->open_docs); ++i) {
		EMEditorDoc *doc = editor->open_docs[i];

		for(j=eaSize(&editor->changed_resources)-1; j>=0; --j) {
			if (stricmp(editor->changed_resources[j], doc->orig_doc_name) == 0) {
				if (!doc->saved) {
					bool bShouldRevert = editor->should_revert_func == NULL || editor->should_revert_func(doc);
					if (bShouldRevert) {
						// Prompt for revert or continue
						emQueuePrompt(EMPROMPT_REVERT_CONTINUE, doc, NULL, editor->dict_name, doc->orig_doc_name);
					}

					// Since the should_revert_func is defined we don't reload the document.
					// The expectation is that should_revert_func does whatever necessary.
					continue;
				}

				// Queue up a reload so we get the data from disk
				emQueueFunctionCall(emReloadDoc,doc);
			}
		}
	}

	// Clean up
	eaDestroyEx(&editor->changed_resources, NULL);
}


/******
* This function is queued when a message dictionary changes after registering with
* the "emAutoHandleDictionaryStateChange" function.  It is queued so it only runs
* once per frame regardless of the number of message changes.
* It checks all open docs to see if they are edited on disk while they have changes.
*/
static void emUpdateAfterMessageDictChange(EMEditor *editor)
{
	int i;

	editor->message_dict_changed = false;

	for( i = 0; i < eaSize(&editor->open_docs); ++i) {
		EMEditorDoc *doc = editor->open_docs[i];
		if (doc->saved) {
			// Queue up a reload so we get the data from disk
			emQueueFunctionCall(emReloadDoc,doc);
		}
	}
}


/******
* This function is queued when a dictionary index changes after registering with
* the "emAutoHandleDictionaryStateChange" function.  It is queued so it only runs
* once per frame regardless of the number of index changes.
* It checks all open docs to see if they lose editability.
*/
static void emUpdateAfterIndexChanged(EMEditor *editor)
{
	int i;

	editor->index_changed = false;

	// Check if lose editability on an open document
	for( i = 0; i < eaSize(&editor->open_docs); ++i) 
	{
		EMEditorDoc *doc = editor->open_docs[i];
		if (!doc->saved && doc->orig_doc_name[0])
		{
			ResourceInfo *info = resGetInfo(editor->dict_name, doc->orig_doc_name);
			if (info && !resIsWritable(info->resourceDict, info->resourceName))
			{
				emQueuePrompt(EMPROMPT_CHECKOUT_REVERT, doc, NULL, editor->dict_name, doc->orig_doc_name);
			}
		}
	}
}


/******
* This function is called when a dictionary changes after registering with
* the "emAutoHandleDictionaryStateChange" function.
*/
static void emHandleAutoDictChanged(enumResourceEventType eType, const char *pDictName, const char *pcName, Referent pReferent, EMEditor *editor)
{
	if (!pcName) {
		return;
	}

	if ((eType == RESEVENT_RESOURCE_MODIFIED) ||
		(eType == RESEVENT_RESOURCE_REMOVED) ||
		(eType == RESEVENT_RESOURCE_ADDED)) {
		if (!editor->dict_changed) {
			editor->dict_changed = true;
			emQueueFunctionCall(emUpdateAfterDictChange, editor);
		}
		eaPush(&editor->changed_resources, strdup(pcName));

	} else if (eType == RESEVENT_INDEX_MODIFIED) {
		if (!editor->index_changed) {
			editor->index_changed = true;
			emQueueFunctionCall(emUpdateAfterIndexChanged, editor);
		}
	}
}


/******
* This function is called when the message dictionary changes after registering with
* the "emAutoHandleDictionaryStateChange" function.
*/
static void emHandleAutoMessageDictChanged(enumResourceEventType eType, const char *pDictName, const char *pcName, Referent pReferent, EMEditor *editor)
{
	if ((eType == RESEVENT_RESOURCE_MODIFIED) ||
		(eType == RESEVENT_RESOURCE_REMOVED) ||
		(eType == RESEVENT_RESOURCE_ADDED)) {
		if (!editor->message_dict_changed) {
			editor->message_dict_changed = true;
			emQueueFunctionCall(emUpdateAfterMessageDictChange, editor);
		}
	}
}

/******
* This function sets the editor to additionally handle state changes to a specified dictionary
* PARAMS:
*   editor - The editor
*   dict_name - The name of the dictionary to watch   
*   open_callback - The function to call when a resource needs to open.  If not set, emOpenFileEx is called.
*   lock_callback - The function to call when a resource needs to lock
*   save_callback - The function to call when a resource needs to save
*   delete_callback - The function to call when a resource needs to delete
******/
void emAddDictionaryStateChangeHandler(SA_PARAM_NN_VALID EMEditor *editor, const char *dict_name, EMResourceStateFunc open_callback, EMResourceStateFunc lock_callback, EMResourceStateFunc save_callback, EMResourceStateFunc delete_callback, void *callback_userdata)
{
	EMEditorDictionaryHandler *pNewHandler = calloc(sizeof(EMEditorDictionaryHandler),1);
	pNewHandler->dict_name = allocAddString(dict_name);	
	pNewHandler->auto_open_callback = open_callback;
	pNewHandler->auto_lock_callback = lock_callback;
	pNewHandler->auto_save_callback = save_callback;
	pNewHandler->auto_delete_callback = delete_callback;
	pNewHandler->auto_callback_userdata = callback_userdata;

	eaPush(&editor->ppExtraDictHandlers, pNewHandler);
}


/******
* This function sets the editor to automatically have dictionary state changes processed.
* PARAMS:
*   editor - The editor
*   dict_name - The name of the dictionary to watch
*   handle_refresh - True if it should handle refresh, as well as state management
*   has_messages - True if the resource has messages to manage
*   open_callback - The function to call when a resource needs to open.  If not set, emOpenFileEx is called.
*   lock_callback - The function to call when a resource needs to lock
*   save_callback - The function to call when a resource needs to save
*   delete_callback - The function to call when a resource needs to delete
******/
void emAutoHandleDictionaryStateChange(EMEditor *editor, const char *dict_name, bool has_messages,
								   EMResourceStateFunc open_callback, EMResourceStateFunc lock_callback, EMResourceStateFunc save_callback, EMResourceStateFunc delete_callback, void *callback_userdata)
{
	editor->dict_name = allocAddString(dict_name);
	assertmsgf(stricmp(editor->dict_name, editor->default_type) == 0, "Editor type %s and dictionary type %s must match!", editor->default_type, dict_name);
	editor->auto_open_callback = open_callback;
	editor->auto_lock_callback = lock_callback;
	editor->auto_save_callback = save_callback;
	editor->auto_delete_callback = delete_callback;
	editor->auto_callback_userdata = callback_userdata;

	resDictRegisterEventCallback(dict_name, emHandleAutoDictChanged, editor);
	if (has_messages)
	{
		resDictRegisterEventCallback("Message", emHandleAutoMessageDictChanged, editor);
	}
}


/******
* This function returns the editor whose name matches the specified string.
* PARAMS:
*   editor - The editor
*   name - The document/object name (called pRefData from the dictionary change callback)
*   type - The document/object type used for opening the document
*   error_type - The type name to use in error messages
*   eType - The eType value from the dictionary change callback
******/
void emHandleDictionaryStateChange(EMEditor *editor, ResourceAction *pAction,								   
								   void *callback_data, EMResourceStateFunc open_callback, EMResourceStateFunc lock_callback, EMResourceStateFunc save_callback, EMResourceStateFunc delete_callback)
{
	const char *name = pAction->pResourceName;
	const char *type = pAction->pDictName;
	const char *error_type = resDictGetItemDisplayName(type);

	void *data;

	if (!name || !editor)
		return;

	// If something is modified, removed, or added, need to scan for updates to the UI
	if (pAction->eActionType == kResAction_Open && pAction->eResult == kResResult_Success) 
	{
		// See if something needs to be opened
		if (emGetResourceState(editor, name) == EMRES_STATE_OPENING) 
		{
			// Clear state and attempt open again
			data = emGetResourceStateData(editor, name);
			emSetResourceState(editor, name, EMRES_STATE_NONE);
			if (!open_callback || (*open_callback)(editor, name, data, EMRES_STATE_NONE, callback_data, true))
			{
				// Run open on next tick so dictionary finishes loading before the open is called
				emQueueFunctionCall2((emFunc2)emOpenFileEx, (void*)allocAddString(name), (void*)allocAddString(type));
			}
		}
	}
	
	if (pAction->eActionType == kResAction_Check_Out && pAction->eResult == kResResult_Failure) 
	{
		if (emGetResourceState(editor, name) == EMRES_STATE_LOCKING) 
		{
			data = emGetResourceStateData(editor, name);
			emSetResourceState(editor, name, EMRES_STATE_LOCK_FAILED);
			if (!lock_callback || (*lock_callback)(editor, name, data, EMRES_STATE_LOCK_FAILED, callback_data, false))
			{
				Errorf("Failed to lock %s %s! Lock failed with: %s", error_type, name, pAction->estrResultString);
			}
		} 
		else if (emGetResourceState(editor, name) == EMRES_STATE_LOCKING_FOR_SAVE) 
		{
			data = emGetResourceStateData(editor, name);
			emSetResourceState(editor, name, EMRES_STATE_LOCK_FAILED);
			if (!save_callback || (*save_callback)(editor, name, data, EMRES_STATE_LOCK_FAILED, callback_data, false))
			{
				Errorf("Failed to lock %s %s! Save failed with: %s", error_type, name, pAction->estrResultString);
			}
		}
		else if (emGetResourceState(editor, name) == EMRES_STATE_LOCKING_FOR_DELETE) 
		{
			data = emGetResourceStateData(editor, name);
			emSetResourceState(editor, name, EMRES_STATE_LOCK_FAILED);
			if (!delete_callback || (*delete_callback)(editor, name, data, EMRES_STATE_LOCK_FAILED, callback_data, false))
			{
				Errorf("Failed to lock %s %s! Delete failed with: %s", error_type, name, pAction->estrResultString);
			}
		}
		else
		{
			// no state change, but the user should still know
			Errorf("Failed to lock %s %s! Lock failed with: %s", error_type, name, pAction->estrResultString);
		}
	}
	if (pAction->eActionType == kResAction_Check_Out && pAction->eResult == kResResult_Success) 
	{
		if (emGetResourceState(editor, name) == EMRES_STATE_LOCKING) 
		{
			data = emGetResourceStateData(editor, name);
			emSetResourceState(editor, name, EMRES_STATE_LOCK_SUCCEEDED);
			if (lock_callback)
				(*lock_callback)(editor, name, data, EMRES_STATE_LOCK_SUCCEEDED, callback_data, true);
		} 
		else if (emGetResourceState(editor, name) == EMRES_STATE_LOCKING_FOR_SAVE) 
		{
			data = emGetResourceStateData(editor, name);
			emSetResourceState(editor, name, EMRES_STATE_LOCK_SUCCEEDED);
			if (save_callback)
				(*save_callback)(editor, name, data, EMRES_STATE_LOCK_SUCCEEDED, callback_data, true);
		}
		else if (emGetResourceState(editor, name) == EMRES_STATE_LOCKING_FOR_DELETE) 
		{
			data = emGetResourceStateData(editor, name);
			emSetResourceState(editor, name, EMRES_STATE_LOCK_SUCCEEDED);
			if (delete_callback)
				(*delete_callback)(editor, name, data, EMRES_STATE_LOCK_SUCCEEDED, callback_data, true);
		}
	}
	if (pAction->eActionType == kResAction_Modify && pAction->eResult == kResResult_Failure) 
	{
		if (emGetResourceState(editor, name) == EMRES_STATE_SAVING) 
		{
			data = emGetResourceStateData(editor, name);
			emSetResourceState(editor, name, EMRES_STATE_SAVE_FAILED);
			if (!save_callback || (*save_callback)(editor, name, data, EMRES_STATE_SAVE_FAILED, callback_data, false))
			{
				Errorf("Failed to save %s %s with: %s!", error_type, name, pAction->estrResultString);
			}
		} 
		else if (emGetResourceState(editor, name) == EMRES_STATE_DELETING) 
		{
			data = emGetResourceStateData(editor, name);
			emSetResourceState(editor, name, EMRES_STATE_DELETE_FAILED);
			if (!delete_callback || (*delete_callback)(editor, name, data, EMRES_STATE_DELETE_FAILED, callback_data, false))
			{
				Errorf("Failed to delete %s %s with: %s!", error_type, name, pAction->estrResultString);
			}
		}
	}
	if (pAction->eActionType == kResAction_Modify && pAction->eResult == kResResult_Success) 
	{
		if (emGetResourceState(editor, name) == EMRES_STATE_SAVING) 
		{
			data = emGetResourceStateData(editor, name);
			emSetResourceState(editor, name, EMRES_STATE_SAVE_SUCCEEDED);
			if (save_callback)
				(*save_callback)(editor, name, data, EMRES_STATE_SAVE_SUCCEEDED, callback_data, true);
			else if (editor->dict_name)
			{
				// If no custom save callback and this is a auto-managed editor, update save flag
				EMEditorDoc *doc = emGetEditorDoc(name, type);
				if (doc)
					doc->saved = true;
			}
		} 
		else if (emGetResourceState(editor, name) == EMRES_STATE_DELETING) 
		{
			data = emGetResourceStateData(editor, name);
			emSetResourceState(editor, name, EMRES_STATE_DELETE_SUCCEEDED);
			if (delete_callback)
				(*delete_callback)(editor, name, data, EMRES_STATE_DELETE_SUCCEEDED, callback_data, true);
		}
	}
}

void emHandleResourceActions(ResourceActionList *pActions)
{
	int i;
	for (i = 0; i < eaSize(&pActions->ppActions); i++)
	{
		ResourceAction *pAction = pActions->ppActions[i];
		EMEditor *pEditor = emGetEditorForType(pAction->pDictName);
		if (pEditor && pEditor->dict_name == pAction->pDictName)
		{
			// First try auto dictionary
			emHandleDictionaryStateChange(pEditor, pAction,
				pEditor->auto_callback_userdata,pEditor->auto_open_callback, pEditor->auto_lock_callback, pEditor->auto_save_callback, pEditor->auto_delete_callback);
		}
		else
		{
			StashTableIterator iter;
			StashElement el;

			// See if it's an additional handler somewhere

			stashGetIterator(em_data.registered_editors, &iter);
			while (stashGetNextElement(&iter, &el))
			{	
				int j;
				pEditor = stashElementGetPointer(el);
				for (j = 0; j < eaSize(&pEditor->ppExtraDictHandlers); j++)
				{
					if (pEditor->ppExtraDictHandlers[j]->dict_name == pAction->pDictName)
					{
						emHandleDictionaryStateChange(pEditor, pAction, pEditor->ppExtraDictHandlers[j]->auto_callback_userdata,
							pEditor->ppExtraDictHandlers[j]->auto_open_callback, pEditor->ppExtraDictHandlers[j]->auto_lock_callback, pEditor->ppExtraDictHandlers[j]->auto_save_callback, pEditor->ppExtraDictHandlers[j]->auto_delete_callback);
						return;
					}
				}
			}			
		}
	}
}

AUTO_RUN;
void registerEMHandleActions(void)
{
	resRegisterHandleActionListCB(emHandleResourceActions);
}

static bool emSmartDeleteContinue(EMEditor *editor, const char *name, void *resource, EMResourceState state, void *data, bool success)
{
	if (success && (state == EMRES_STATE_LOCK_SUCCEEDED)) {
		// Since we got the lock, continue by doing the delete save
		emSetResourceStateWithData(editor, name, EMRES_STATE_DELETING, resource);
		resRequestSaveResource(editor->dict_name, name, NULL);
	}
	return true;
}


EMTaskStatus emSmartSaveDoc(EMEditorDoc *doc, void *resource, void *orig_resource, bool save_as_new)
{
	// Prompt if name already in use (new or save as new)
	if (!doc->smart_save_overwrite && (!doc->orig_doc_name[0] || save_as_new) && resGetInfo(doc->editor->dict_name, doc->doc_name)) {
		emQueuePrompt(EMPROMPT_SAVE_NEW_RENAME_CANCEL, doc, NULL, doc->editor->dict_name, doc->doc_name);

		StructDestroyVoid(RefSystem_GetDictionaryParseTable(doc->editor->dict_name), resource);
		return EM_TASK_FAILED;
	} else if (!doc->smart_save_rename && !doc->smart_save_overwrite && 
				doc->orig_doc_name[0] && (stricmp(doc->orig_doc_name,doc->doc_name) != 0)) {
		// Name changed and may have collision
		if (resGetInfo(doc->editor->dict_name, doc->doc_name)) {
			emQueuePrompt(EMPROMPT_SAVE_NEW_RENAME_OVERWRITE_CANCEL, doc, NULL, doc->editor->dict_name, doc->doc_name);
		} else {
			emQueuePrompt(EMPROMPT_SAVE_NEW_RENAME_CANCEL, doc, NULL, doc->editor->dict_name, doc->doc_name);
		}

		StructDestroyVoid(RefSystem_GetDictionaryParseTable(doc->editor->dict_name), resource);
		return EM_TASK_FAILED;
	}

	// Make sure dictionary is in edit mode
	resSetDictionaryEditMode(doc->editor->dict_name, true);
	resSetDictionaryEditMode(gMessageDict, true);

	// Check the lock
	if (!resGetLockOwner(doc->editor->dict_name, doc->doc_name)) {
		// Don't have lock, so ask server to lock and go into locking state
		emSetResourceState(doc->editor, doc->doc_name, EMRES_STATE_LOCKING_FOR_SAVE);
		resRequestLockResource(doc->editor->dict_name, doc->doc_name, resource);

		StructDestroyVoid(RefSystem_GetDictionaryParseTable(doc->editor->dict_name), resource);
		return EM_TASK_INPROGRESS;
	}

	// Get here if have the main lock... now check for rename lock
	if (!save_as_new && doc->orig_doc_name[0] && orig_resource &&
		(strcmp(doc->orig_doc_name, doc->doc_name) != 0)) {
		void *resource_copy = StructCloneVoid(RefSystem_GetDictionaryParseTable(doc->editor->dict_name), resource);
		resRequestLockResource(doc->editor->dict_name, doc->orig_doc_name, resource_copy);

		// Go into lock state if we don't already have the lock
		if (!resGetLockOwner(doc->editor->dict_name, doc->orig_doc_name)) {
			resSetDictionaryEditMode(doc->editor->dict_name, true);
			resSetDictionaryEditMode(gMessageDict, true);
			emSetResourceStateWithData(doc->editor, doc->orig_doc_name, EMRES_STATE_LOCKING_FOR_DELETE, resource_copy);
		} else {
			// Otherwise continue the delete
			emSmartDeleteContinue(doc->editor, doc->orig_doc_name, resource_copy, EMRES_STATE_LOCK_SUCCEEDED, NULL, true);
		}
	}

	if (doc->smart_save_rename || save_as_new || !doc->orig_doc_name[0]) {
		// When rename or save as new, set original doc name so old original is forgotten
		strcpy(doc->orig_doc_name, doc->doc_name);
	}

	// Send save to server
	emSetResourceStateWithData(doc->editor, doc->doc_name, EMRES_STATE_SAVING, doc);
	resRequestSaveResource(doc->editor->dict_name, doc->doc_name, resource);
	return EM_TASK_INPROGRESS;
}


/******
* This function works only if the dict_name is set on the editor using 
* "emAutoHandleDictionaryStateChange()".  It tells if the resource is currently
* checked out and editable.  If it is not, it can prompt to check out.
* PARAMS:
*   doc - The document to check
*   prompt - Whether or not to automatically prompt for editing
* RETURNS:
*   true if the document is checked out and editable
******/
bool emDocIsEditable(EMEditorDoc *doc, bool prompt)
{
	ResourceInfo *info;
	
	// Can always edit if this is a new doc or if not configured with a dictionary
	if (!doc->orig_doc_name[0] || !doc->editor->dict_name)
	{
		return true;
	}

	// Cannot edit if not writable
	info = resGetInfo(doc->editor->dict_name, doc->orig_doc_name);
	if (info && !resIsWritable(info->resourceDict, info->resourceName))
	{
		if (prompt)
		{
			emQueuePrompt(EMPROMPT_CHECKOUT, doc, NULL, doc->editor->dict_name, doc->orig_doc_name);
		}
		return false;
	}

	return true;
}


/********************
* EDITOR/DOCUMENT MANAGEMENT
********************/
/******
* This function returns the editor whose name matches the specified string.
* PARAMS:
*   name - string name of the editor to find
* RETURNS:
*   EMEditor whose name matches the specified name
******/
EMEditor *emGetEditorByName(const char *name)
{
	int i;

	if (!name)
		return NULL;

	for (i = 0; i < eaSize(&em_data.editors); i++)
	{
		EMEditor *editor = em_data.editors[i];
		if (strcmpi(editor->editor_name, name) == 0)
			return editor;
	}
	return NULL;
}

/******
* This function returns the editor associated with a particular type.
* PARAMS:
*   type - string type
* RETURNS:
*   EMEditor associated with the type
******/
EMEditor *emGetEditorForType(const char *type)
{
	EMRegisteredType *registered_type;
	EMEditor *editor = NULL;

	// ensure type exists and is registered
	if (!type || !em_data.registered_file_types || !stashFindPointer(em_data.registered_file_types, type, &registered_type))
		return NULL;

	// ensure editor exists
	if (stashFindPointer(em_data.registered_editors, registered_type->editor_name, &editor))
		return editor;

	return NULL;
}

typedef struct EMOpenFileParams
{
	EMEditor *editor;
	char name[256];
	char type[16];
} EMOpenFileParams;

static void emOpenFileQueued(EMOpenFileParams *params)
{
	emOpenFileInternal(params->editor, params->name, params->type);
	free(params);
}

/******
* This function is the main internal function used to load a document based on an existing
* file or existing data.
* PARAMS:
*   editor - EMEditor editor being used to edit the file
*   name - string name of the file to edit
*   type - string type/extension of the file
*   focus - bool indicating whether to set focus on the document when loaded
* RETURNS:
*   EMEditorDoc created to edit the specified file; NULL if the load failed or if the editor
*   needs time to initialize
******/
static EMEditorDoc *emOpenFileInternal(EMEditor *editor, const char *name, const char *type)
{
	EMWorkspace *workspace;
	EMEditorDoc *doc = NULL;
	int i;
	bool bFound = false;

	// validate the editor
	if (!emIsEditorOk(editor))
		return NULL;
	workspace = emWorkspaceGet(editor->default_workspace, true);
	assert(workspace);

	// do not continue with editing the file if another doc is already
	// open for editors that disallow multiple documents
	if (!editor->allow_multiple_docs && eaSize(&editor->open_docs) > 0)
	{
		emSetActiveEditorDoc(editor->open_docs[0], NULL);
		return NULL;
	}

	if (!editor->primary_editor)
		sharedMemoryEnableEditorMode();

	// ensure editor has been initialized
	if (editor->can_init_func && editor->can_init_func(editor->can_init_data) != 1.0f)
	{
		EMOpenFileParams *params = calloc(1, sizeof(*params));
		params->editor = editor;
		strcpy(params->name, name);
		strcpy(params->type, type);
		emInitProgressDialogCreate(editor, emOpenFileQueued, params);
		return NULL;
	}
	emEditorInit(editor);

	// If none are open yet, run enter call back
	if (editor->enter_editor_func && eaSize(&editor->open_docs) == 0)
		editor->enter_editor_func(editor);

	// create the new document by calling editor's load function
	if (editor->load_func)
	{
		doc = editor->load_func(name, type);
	}
	if (!doc)
	{
		if (editor->exit_func && eaSize(&editor->open_docs) == 0)
			editor->exit_func(editor);
		return NULL;
	}

	// set the document's name, type, and editor
	if (!doc->doc_name[0] && name)
		strcpy(doc->doc_name, name);
	if (!doc->doc_type[0] && type)
		strcpy(doc->doc_type, type);
	doc->editor = editor;

	// add the new document to the tracked open docs if it is not already there
	// Some editors may return an existing document
	for(i = eaSize(&editor->open_docs)-1; i>=0; --i)
	{
		if (editor->open_docs[i] == doc)
		{
			bFound = true;
			break;
		}
	}
	if (!bFound)
	{
		doc->saved = true;
		eaPush(&editor->open_docs, doc);
		eaPush(&em_data.open_docs, doc);
		eaPush(&workspace->open_docs, doc);
	}

	// set the document to be active
	emSetActiveEditorDoc(doc, NULL);

	// When first open a document, apply window prefs to it
	emDocApplyWindowPrefs(doc, true);

	return doc;
}

/******
* This function loads a document from an existing file.
* PARAMS:
*   name - string name/path of data/file to load; if NULL, opens editor associated to type
*   type - string type/extension of the data to load
* RETURNS:
*   EMEditorDoc* for the opened doc, NULL if the file type was unrecognized
******/
EMEditorDoc* emOpenFileEx(const char *name, const char *type)
{
	EMRegisteredType *registered_type;
	EMEditor *editor;
	int i;

	if(!em_data.initialized)
		emInit();

	// add the name to the recently opened list
	// Skip documents with no type or with a type starting with an underbar
	if (name && (type[0] != '_'))
	{
		char mru_name[1024];
		sprintf(mru_name, "%s:%s", type, name);
		mruAddToList(em_data.open_recent, mru_name);
		emDocumentMenuRebuild();
	}

	// first check if doc is already open in an internal editor
	if (name)
	{
		for (i = 0; i < eaSize(&em_data.open_docs); ++i)
		{
			if (stricmp(em_data.open_docs[i]->doc_name, name) == 0
				&& stricmp(em_data.open_docs[i]->doc_type, type) == 0)
			{
				emSetActiveEditorDoc(em_data.open_docs[i], NULL);
				return em_data.open_docs[i];
			}
		}
	}

	// correct type format
	if (type[0] == '.')
		++type;

	// if file type is not registered, do nothing
	if (!em_data.registered_file_types || !stashFindPointer(em_data.registered_file_types, type, &registered_type))
		return NULL;

	// it has an associated internal editor, run it through that
	if (stashFindPointer(em_data.registered_editors, registered_type->editor_name, &editor))
	{
		return emOpenFileInternal(editor, name, type);
	}

	return NULL;
}

/******
* This function loads a document from an existing file.  If the file's extension is not
* registered with an editor, this function will attempt to open it in an external reader
* or editor.
* PARAMS:
*   name - string name/path of data/file to load; function will attempt to determine type
*          from extension appended to name
* RETURNS:
*   EMEditorDoc* for the opened file, NULL if the file type was not registered with the Editor Manager
******/
EMEditorDoc* emOpenFile(const char *filename)
{
	EMEditorDoc* doc;
	char *extension;
	char *filename_dup = strdup(filename);

	// no filename, we can't open this
	if (!filename_dup)
	{
		return NULL;
	}

	// if filename has no extension, try opening it in external reader/editor
	extension = strrchr(filename_dup, '.');
	if (!extension)
	{
		SAFE_FREE(filename_dup);
		emuOpenFile(filename);
		return NULL;
	}

	// split main file name and extension into two strings
	*extension = 0;
	++extension;

	if (!(doc = emOpenFileEx(filename_dup, extension)))
	{
		SAFE_FREE(filename_dup);
		emuOpenFile(filename);
		return NULL;
	}

	SAFE_FREE(filename_dup);
	return doc;
}

/******
* This function sets a workspace active, as if you had just clicked on it from the combo box.
* PARAMS:
*   workspace_name - string name of the workspace to open; will do nothing if a matching workspace is not found
******/
void emWorkspaceOpen(const char *workspace_name)
{
	EMWorkspace *workspace = emWorkspaceGet(workspace_name, false);

	if (workspace)
	{
		emQueueFunctionCall2(emWorkspaceSetActive, workspace, NULL);
		emQueueFunctionCall2(emSetActiveEditorDoc, eaSize(&workspace->open_docs) > 0 ? workspace->open_docs[0] : NULL, NULL);
	}
}

/******
* Command to switch workspaces as if you'd just clicked the given one.
******/
AUTO_COMMAND ACMD_NAME("EM.WorkspaceOpen");
void emCmdWorkspaceOpen(const char* workspace_name)
{
	emWorkspaceOpen(workspace_name);
}

typedef struct EMNewDocParams
{
	char type[16];
	void *data;
} EMNewDocParams;

static void emNewDocQueued(EMNewDocParams *params)
{
	emNewDoc(params->type, params->data);
	free(params);
}

/******
* This function is the main function used to create new documents of a particular type.  If
* the data pointer is not specified and the editor associated with the specified type has
* a custom_new_func specified, then the custom function will be invoked.  The custom_new_func
* *should*, when actually creating the new document, reinvoke emNewDoc with data populated with
* whatever the custom_new_func determined.
* PARAMS:
*   type - string type/extension for which a new document should be created; should be
*          registered to an editor
*   data - pointer to any custom struct that may be necessary for creating a new document
*          (eg. category/group, dependent data, parent data, etc.)
******/
void emNewDoc(const char *type, void *data)
{
	EMEditorDoc *doc = NULL;
	EMEditor *editor = emGetEditorForType(type);
	EMWorkspace *workspace;
	int i;
	bool bFound = false;

	// validate the editor
	if (!editor || !emIsEditorOk(editor))
		return;
	workspace = emWorkspaceGet(editor->default_workspace, true);
	assert(workspace);

	// do not continue with editing the file if another doc is already
	// open for editors that disallow multiple documents
	if (!editor->allow_multiple_docs && eaSize(&editor->open_docs) > 0)
	{
		emSetActiveEditorDoc(editor->open_docs[0], NULL);
		return;
	}

	// ensure editor has been initialized
	if (editor->can_init_func && editor->can_init_func(editor->can_init_data) != 1.0f)
	{
		EMNewDocParams *params = calloc(1, sizeof(*params));
		strcpy(params->type, type);
		params->data = data;
		emInitProgressDialogCreate(editor, emNewDocQueued, params);
		return;
	}
	emEditorInit(editor);

	// if editor has a custom new function, then invoke it
	if (!data && editor->custom_new_func)
	{
		editor->custom_new_func();
		return;
	}

	// If none are open yet, run enter call back
	if (editor->enter_editor_func && eaSize(&editor->open_docs) == 0)
		editor->enter_editor_func(editor);

	// call editor's new_func function to create the document
	if (editor->new_func)
	{
		doc = editor->new_func(type, data);
	}
	if (!doc)
	{
		if (editor->exit_func && eaSize(&editor->open_docs) == 0)
			editor->exit_func(editor);
		return;
	}

	// set the document's type and editor
	if (!doc->doc_type[0] && type)
		strcpy(doc->doc_type, type);
	doc->editor = editor;

	// add the new document to the tracked open docs if it is not already there
	// Some editors may return an existing document
	for(i=eaSize(&editor->open_docs)-1; i>=0; --i)
	{
		if (editor->open_docs[i] == doc)
		{
			bFound = true;
			break;
		}
	}
	if (!bFound)
	{
		eaPush(&editor->open_docs, doc);
		eaPush(&em_data.open_docs, doc);
		eaPush(&workspace->open_docs, doc);
	}

	// create its UI
	emDocTabCreate(doc);

	// set new document to be active
	emSetActiveEditorDoc(doc, NULL);

	// When first open a doc, apply window prefs to it
	emDocApplyWindowPrefs(doc, true);
}

/******
* This function is used solely for the purpose of default document reloading behavior.  We could not use
* the usual means of closing a doc and reopening it, as special care is required to maintain the UI when
* simply refreshing a document.
* PARAMS:
*   doc - EMEditorDoc to reopen
******/
void emReopenDoc(EMEditorDoc *doc)
{
	EMEditor *editor = doc->editor;
	EMEditorDoc *current_doc = em_data.current_doc;
	EMEditorDoc *reopened_doc;
	UITab *doc_tab;
	EMWorkspace *workspace;
	char doc_name[MAX_PATH];
	char doc_type[MAX_PATH];
	int idx = eaFind(&em_data.open_docs, doc);
	int i;

	strcpy(doc_name, doc->doc_name);
	strcpy(doc_type, doc->doc_type);
	doc_tab = doc->ui_tab;

	// validation steps
	if (idx < 0 || !em_data.registered_file_types)
		return;

	// close and destroy doc
	workspace = emWorkspaceFindDoc(doc);
	assert(workspace);

	i = eaFind(&workspace->tab_group->eaTabs, doc->ui_tab);
	assert(i >= 0);
	emCloseDocInternal(doc, NULL, true);

	reopened_doc = emOpenFileInternal(editor, doc_name, doc_type);
	if (!reopened_doc)
	{
		emStatusPrintf("Failed to reopen document \"%s\".", doc_name);
		emSetActiveEditorDoc(eaSize(&em_data.active_workspace->open_docs) > 0 ? em_data.active_workspace->open_docs[0] : NULL, NULL);
		return;
	}
	emMoveDocToWorkspace(reopened_doc, workspace->name);
	eaMove(&workspace->tab_group->eaTabs, i, eaFind(&workspace->tab_group->eaTabs, reopened_doc->ui_tab));

	// if reopened doc was not the current doc, reset focus to current doc
	if (current_doc != em_data.current_doc)
		emSetActiveEditorDoc(current_doc, NULL);
	else
		ui_TabGroupSetActive(workspace->tab_group, reopened_doc->ui_tab);
}

/******
* This function reloads the specified document using the function given in the
* editor during registration.
* PARAMS:
*   doc - EMEditorDoc to reload
******/
void emReloadDoc(EMEditorDoc *doc)
{
	EMFile **changed_files = NULL;
	int i;

	// Don't do anything if called on an invalid doc
	if (!emIsDocOpen(doc))
		return;

	assert(doc->editor);

	// update timestamps
	emDocGetChangedFiles(doc, &changed_files, true);
	for (i = 0; i < eaSize(&changed_files); i++)
		emDocUpdateFile(doc, changed_files[i]);
	if (doc->editor->reload_func)
		doc->editor->reload_func(doc);
	eaDestroy(&changed_files);
}

/******
* This function reloads the specified sub-document using the function given in the
* editor during registration.
* PARAMS:
*   doc - EMEditorDoc to reload
*   subdoc - EMEditorSubDoc to reload
******/
void emReloadSubDoc(EMEditorDoc *doc, EMEditorSubDoc *subdoc)
{
	EMFile **changed_files = NULL;
	int i;

	assert(doc->editor);

	// update timestamps
	emDocGetChangedFiles(doc, &changed_files, true);
	for (i = 0; i < eaSize(&changed_files); i++)
		emDocUpdateFile(doc, changed_files[i]);
	if (doc->editor->sub_reload_func)
		doc->editor->sub_reload_func(doc,subdoc);
	eaDestroy(&changed_files);
}

/******
* This function determines whether a document is saved (including the unsaved bits of its subdocs).
* PARAMS:
*   doc - EMEditorDoc to check for save state
* RETURNS:
*   bool indicating whether doc and all of its subdocs are saved
******/
bool emGetDocSaved(EMEditorDoc *doc)
{
	int i;
	for (i = 0; i < eaSize(&doc->sub_docs); i++)
		if (!doc->sub_docs[i]->saved)
			return false;
	return doc->saved;
}

/******
* This function should be used to "dirty" a subdoc's saved state.  It will ensure that the saved bit is
* set properly and will do automatic checkout of associated files.
* PARAMS:
*   doc - EMEditorDoc to which the subdoc belongs
*   subdoc - EMEditorSubDoc to dirty
******/
void emSetSubDocUnsaved(EMEditorDoc *doc, EMEditorSubDoc *subdoc)
{
	assert(eaFind(&doc->sub_docs, subdoc) >= 0);
	subdoc->saved = 0;
	if (doc->editor && !doc->editor->disable_auto_checkout)
	{
		EMFile **files = NULL;
		int i;

		emSubDocGetFiles(subdoc, &files);
		for (i = 0; i < eaSize(&files); i++)
		{
			if (emuCheckoutFile(files[i]))
				files[i]->checked_out = 1;
			else if (!files[i]->failed_checkout)
				emStatusPrintf("File \"%s\" could not be checked out, changes will not be saved!", files[i]->filename);
			files[i]->failed_checkout = !files[i]->checked_out;
		}
		eaDestroy(&files);
	}
}

/******
* This function should be used to "dirty" your doc's saved state.  It will ensure that the saved bit is
* set properly and will do automatic checkout of associated files.
* PARAMS:
*   doc - EMEditorDoc to dirty
*   include_subdocs - determines whether to dirty all subdocs as well
******/
void emSetDocUnsaved(EMEditorDoc *doc, bool include_subdocs)
{
	doc->saved = 0;
	if (doc->editor && !doc->editor->disable_auto_checkout)
	{
		EMFile **files = NULL;
		int i;

		if (include_subdocs)
		{
			for (i = 0; i < eaSize(&doc->sub_docs); i++)
				emSetSubDocUnsaved(doc, doc->sub_docs[i]);
		}
		emDocGetFiles(doc, &files, false);
		for (i = 0; i < eaSize(&files); i++)
		{
			if (emuCheckoutFile(files[i]))
				files[i]->checked_out = 1;
			else if (!files[i]->failed_checkout)
				emStatusPrintf("File \"%s\" could not be checked out, changes will not be saved!", files[i]->filename);
			files[i]->failed_checkout = !files[i]->checked_out;
		}
		eaDestroy(&files);
	}
}

/******
* This is an internal function to clear all save states.
******/
static void emAddSaveState(EMEditorDoc *doc, EMEditorSubDoc *sub_doc, bool save_as, EMSaveStatusFunc func)
{
	EMSaveEntry *entry;
	int i;

	// Check if already in the list
	for(i=eaSize(&em_data.save_state.entries)-1; i>=0; --i)
	{
		entry = em_data.save_state.entries[i];
		if ((entry->doc == doc) &&
			((entry->sub_doc == NULL) || (entry->sub_doc == sub_doc)) &&
			(entry->save_as == save_as))
			return; // Already in the list
	}

	// Add to the list
	entry = calloc(1, sizeof(EMSaveEntry));
	entry->doc = doc;
	entry->sub_doc = sub_doc;
	entry->status_func = func;
	entry->save_as = save_as;
	eaPush(&em_data.save_state.entries, entry);

	// Create a progress window if one doesn't exist
	if (!em_data.save_state.progress_window) 
	{
		UIWindow *window;
		UILabel *label;

		// Create the window
		window = ui_WindowCreate("Save in Progress", 0, 0, 300, 50);
		ui_WindowSetModal(window, true);
		ui_WindowSetClosable(window, false);

		// Lay out the message
		label = ui_LabelCreate("Saving...",50,0);
		ui_WidgetSetPositionEx(UI_WIDGET(label),-label->widget.width/2, 0, 0.5, 0, UITopLeft);
		ui_WindowAddChild(window, label);

		// Show the window
		elUICenterWindow(window);
		ui_WindowPresent(window);

		em_data.save_state.progress_window = window;
	}
}


/******
* This is an internal function to clear all save states.
******/
static void emRemoveSaveState(EMEditorDoc *doc, EMEditorSubDoc *sub_doc, bool doc_is_open)
{
	int i;

	// Do the removal
	for(i=eaSize(&em_data.save_state.entries)-1; i>=0; --i)
	{
		if ((em_data.save_state.entries[i]->doc == doc) && (em_data.save_state.entries[i]->sub_doc == sub_doc))
		{
			free(em_data.save_state.entries[i]);
			eaRemove(&em_data.save_state.entries, i);
		}
	}

	// Clear the progress window if the save list empties
	if ((eaSize(&em_data.save_state.entries) == 0) && em_data.save_state.progress_window)
	{
		ui_WindowHide(em_data.save_state.progress_window);
		ui_WidgetQueueFree(UI_WIDGET(em_data.save_state.progress_window));
		em_data.save_state.progress_window = NULL;
	}

	// Clear flags
	if (doc_is_open) {
		doc->smart_save_rename = false;
		doc->smart_save_overwrite = false;
	}
}

static EMSaveEntry *emGetSaveState(EMEditorDoc *doc, EMEditorSubDoc *sub_doc)
{
	EMSaveEntry *entry = NULL;
	int i;

	for(i=eaSize(&em_data.save_state.entries)-1; i>=0; --i)
	{
		entry = em_data.save_state.entries[i];
		if ((entry->doc == doc) &&
			((entry->sub_doc == NULL) || (entry->sub_doc == sub_doc)))
			break;
	}

	return entry;
}


/******
* This is an internal function to continue saving
******/
static void emContinueSaving(void)
{
	int i;

	for(i=eaSize(&em_data.save_state.entries)-1; i>=0; --i)
	{
		EMSaveEntry *entry = em_data.save_state.entries[i];
		if (entry->save_as)
			emSaveDocAsEx(entry->doc, false, entry->status_func);
		else if (entry->sub_doc)
			emSaveSubDocEx(entry->doc, entry->sub_doc, false, entry->status_func);
		else 
			emSaveDocEx(entry->doc, false, entry->status_func);
	}
}


/******
* This function saves a specified document.
* PARAMS:
*   doc - EMEditorDoc to save
*   doAsyncSave - Queues up the save to try on future ticks if it is INPROGRESS
* RETURNS:
*   EMTaskStatus indicating whether save was successful, or is still ongoing
******/
EMTaskStatus emSaveDocEx(EMEditorDoc *doc, bool doAsyncSave, EMSaveStatusFunc saveFunc)
{
	EMTaskStatus status = EM_TASK_SUCCEEDED;
	int i;

	// Don't do anything if called on an invalid doc
	if (!emIsDocOpen(doc))
		return EM_TASK_FAILED;

	// if editor has custom save function, call that instead
	if (doc->editor->custom_save_func)
	{
		status = doc->editor->custom_save_func(doc);
		if (status == EM_TASK_INPROGRESS)
		{
			if (doAsyncSave)
				emAddSaveState(doc, NULL, false, saveFunc);
			return EM_TASK_INPROGRESS;
		}
		else
		{
			emRemoveSaveState(doc, NULL, true);
			if (saveFunc)
				(*saveFunc)(status, doc, NULL);
			return status;
		}
	}

	// if it has sub-docs, save them first
	for(i = eaSize(&doc->sub_docs)-1; i >= 0; --i)
	{
		status = emSaveSubDocEx(doc, doc->sub_docs[i], false, NULL);
		if (status == EM_TASK_FAILED)
		{
			emRemoveSaveState(doc, NULL, true);
			if (saveFunc)
				(*saveFunc)(EM_TASK_FAILED, doc, NULL);
			return EM_TASK_FAILED;
		}
		if (status == EM_TASK_INPROGRESS)
		{
			if (doAsyncSave)
				emAddSaveState(doc, NULL, false, saveFunc);
			return EM_TASK_INPROGRESS;
		}
	}

	// Simply finish if doc is already saved and does not have an active save state
	if (doc->saved && !emGetSaveState(doc, NULL))
	{
		emRemoveSaveState(doc, NULL, true);
		if (saveFunc)
			(*saveFunc)(EM_TASK_SUCCEEDED, doc, NULL);
		return EM_TASK_SUCCEEDED;
	}

	// ensure all files are checked out
	if (!doc->editor->disable_auto_checkout)
	{
		EMFile **files = NULL;

		emDocGetFiles(doc, &files, false);
		for (i = 0; i < eaSize(&files); i++)
			if (!files[i]->checked_out)
				emStatusPrintf("Failed to save. File \"%s\" is not checked out!", files[i]->filename);
		eaDestroy(&files);
	}

	// call editor's save function
	if (doc->editor->save_func)
	{
		// if save function fails, do not continue
		status = doc->editor->save_func(doc);
		if (status == EM_TASK_FAILED)
		{
			emRemoveSaveState(doc, NULL, true);
			if (saveFunc)
				(*saveFunc)(EM_TASK_FAILED, doc, NULL);
			return EM_TASK_FAILED;
		}
		if (status == EM_TASK_INPROGRESS)
		{
			if (doAsyncSave)
				emAddSaveState(doc, NULL, false, saveFunc);
			return EM_TASK_INPROGRESS;
		}
	}

	// update file timestamp to ensure that this doc does not attempt to reload
	for (i = 0; i < eaSize(&doc->files); i++)
	{
		char relpath[MAX_PATH];

		// ensure last timestamp is up-to-date
		fileRelativePath(doc->files[i]->file->filename, relpath);
		if (relpath[1] != ':')
			FolderCacheForceUpdate(folder_cache, relpath);
		doc->files[i]->last_timestamp = fileLastChanged(doc->files[i]->file->filename);
	}

	// save window positions
	emDocSaveWindowPrefs(doc);

	doc->saved = true;

	emRemoveSaveState(doc, NULL, true);
	if (saveFunc)
		(*saveFunc)(EM_TASK_SUCCEEDED, doc, NULL);
	return EM_TASK_SUCCEEDED;
}

EMTaskStatus emSaveDoc(EMEditorDoc *doc)
{
	return emSaveDocEx(doc, true, NULL);
}

/******
* This function saves a specified document as a different name.
* PARAMS:
*   doc - EMEditorDoc to save
*   doAsyncSave - Queues up the save to try on future ticks if it is INPROGRESS
* RETURNS:
*   bool indicating whether save as was successful
******/
EMTaskStatus emSaveDocAsEx(EMEditorDoc *doc, bool doAsyncSave, EMSaveStatusFunc saveFunc)
{
	EMTaskStatus status;

	if (doc->editor->save_as_func)
	{
		status = doc->editor->save_as_func(doc);
		if (status == EM_TASK_INPROGRESS)
		{
			if (doAsyncSave)
				emAddSaveState(doc, NULL, true, saveFunc);
			return EM_TASK_INPROGRESS;
		}
		else
		{
			emRemoveSaveState(doc, NULL, true);
			if (saveFunc)
				(*saveFunc)(status, doc, NULL);
			return status;
		}
	}

	if (doc->saved)
		status = EM_TASK_SUCCEEDED;
	else
		status = EM_TASK_FAILED;

	emRemoveSaveState(doc, NULL, true);
	if (saveFunc)
		(*saveFunc)(status, doc, NULL);
	return status;
}

EMTaskStatus emSaveDocAs(EMEditorDoc *doc)
{
	return emSaveDocAsEx(doc, true, NULL);
}


/******
* This function saves a specified sub-document.
* PARAMS:
*   doc - EMEditorDoc that as a sub-doc
*   sub_doc - EMEditorSubDoc to save
*   doAsyncSave - Queues up the save to try on future ticks if it is INPROGRESS
* RETURNS:
*   bool indicating whether save was successful
******/
EMTaskStatus emSaveSubDocEx(EMEditorDoc *doc, EMEditorSubDoc *sub_doc, bool doAsyncSave, EMSaveStatusFunc saveFunc)
{
	EMTaskStatus status = EM_TASK_SUCCEEDED;	
	int i;

	// Do nothing if sub-doc has already closed
	if (eaFind(&doc->sub_docs, sub_doc) == -1)
	{
		emRemoveSaveState(doc, sub_doc, true);
		return EM_TASK_SUCCEEDED;
	}

	// do nothing if doc is already saved and does not have a save state
	if (sub_doc->saved && !emGetSaveState(doc, sub_doc))
	{
		emRemoveSaveState(doc, sub_doc, true);
		if (saveFunc)
			(*saveFunc)(EM_TASK_SUCCEEDED, doc, sub_doc);
		return EM_TASK_SUCCEEDED;
	}

	if (!doc->editor->disable_auto_checkout)
	{
		for (i = 0; i < eaSize(&sub_doc->files); i++)
		{
			if (!sub_doc->files[i]->file->checked_out)
				emStatusPrintf("Failed to save. File \"%s\" is not checked out!", sub_doc->files[i]->file->filename);
		}
	}

	// call editor's save function
	if (doc->editor->sub_save_func)
	{
		// if save function fails, do not continue
		status = doc->editor->sub_save_func(doc, sub_doc);
		if (status == EM_TASK_FAILED)
		{
			emRemoveSaveState(doc, sub_doc, true);
			if (saveFunc)
				(*saveFunc)(status, doc, sub_doc);
			return status;
		}
		if (status == EM_TASK_INPROGRESS)
		{
			if (doAsyncSave)
				emAddSaveState(doc, sub_doc, false, saveFunc);
			return status;
		}
	}

	// update file timestamp to ensure that this doc does not attempt to reload
	for (i = 0; i < eaSize(&sub_doc->files); i++)
	{
		char relpath[MAX_PATH];

		// ensure last timestamp is up-to-date
		fileRelativePath(sub_doc->files[i]->file->filename, relpath);
		if (relpath[1] != ':')
			FolderCacheForceUpdate(folder_cache, relpath);
		sub_doc->files[i]->last_timestamp = fileLastChanged(sub_doc->files[i]->file->filename);
	}

	// save window positions
	emDocSaveWindowPrefs(doc);

	sub_doc->saved = true;

	emRemoveSaveState(doc, sub_doc, true);
	if (saveFunc)
		(*saveFunc)(EM_TASK_SUCCEEDED, doc, sub_doc);
	return EM_TASK_SUCCEEDED;
}

EMTaskStatus emSaveSubDoc(EMEditorDoc *doc, EMEditorSubDoc *sub_doc)
{
	return emSaveSubDocEx(doc, sub_doc, true, NULL);
}


static void emSetShouldQuit(void *unused)
{
	utilitiesLibSetShouldQuit(true);
}


/******
* This is an internal function to hide and free the confirmation window.
******/
static void emDismissPromptWindow(void)
{
	if (em_data.close_state.confirm_window) {
		ui_WindowHide(em_data.close_state.confirm_window);
		ui_WidgetQueueFree((UIWidget*)em_data.close_state.confirm_window);
		em_data.close_state.confirm_window = NULL;
	}
}


/******
* This is an internal function to clear all close states.
******/
static void emClearCloseState(void)
{
	EMEditorDoc *active_doc = em_data.current_doc;
	int i;

	em_data.close_state.close_mode = EMClose_None;
	em_data.close_state.should_quit = false;
	em_data.close_state.force_close = false;
	em_data.close_state.force_save = false;

	// reopen docs for editors that should always be open
	if (!em_data.close_state.quitting)
	{
		for (i = 0; i < eaSize(&em_data.editors); i++)
		{
			EMEditor *editor = em_data.editors[i];
			if (editor->always_open && eaSize(&editor->open_docs) == 0)
				emNewDoc(editor->default_type, NULL);
		}
	}
	if (active_doc)
		emSetActiveEditorDoc(active_doc, NULL);
}


/******
* This is an internal function to continue closing after a prompt.
******/
static void emContinueClosing(void)
{
	if (em_data.close_state.close_mode == EMClose_AllDocs)
		emCloseAllDocs();
	else if (em_data.close_state.close_mode == EMClose_AllSubDocs)
		emCloseAllSubDocs(em_data.close_state.doc);
	else if (em_data.close_state.close_mode == EMClose_SubDoc)
		emCloseSubDoc(em_data.close_state.doc, em_data.close_state.sub_doc);
	else if (em_data.close_state.close_mode == EMClose_Doc)
		emCloseDoc(em_data.close_state.doc);
}


/******
* This is an internal function to deal with result of save from close dialog
******/
static void emSaveFromPromptResult(EMTaskStatus status, EMEditorDoc *doc, EMEditorSubDoc *subdoc)
{
	if (status == EM_TASK_SUCCEEDED)
	{
		if (emCloseDocInternal(em_data.close_state.doc, em_data.close_state.sub_doc, true) == EM_TASK_SUCCEEDED)
		{
			// For single doc or subdoc close, we wrap things up here and the continue will do nothing
			// For multi-doc modes, there will be more to do
			if (((em_data.close_state.close_mode == EMClose_Doc) && (em_data.close_state.sub_doc == NULL)) || 
				(em_data.close_state.close_mode == EMClose_SubDoc))
				emClearCloseState();
		}
	}
	else
	{
		emClearCloseState();		
	}

	emContinueClosing();
}



/******
* This is an internal function to perform a save-then-close from the confirmation window.
******/
static void emSaveFromPrompt(UIButton *button, void *data)
{
	emDismissPromptWindow();

	// Save the document as requested
	em_data.close_state.force_save = true; // Otherwise we get the prompt again next frame
	if (em_data.close_state.sub_doc)
	{
		emSaveSubDocEx(em_data.close_state.doc, em_data.close_state.sub_doc, true, emSaveFromPromptResult);
	}
	else
	{
		emSaveDocEx(em_data.close_state.doc, true, emSaveFromPromptResult);
	}
}


/******
* This is an internal function to perform a forced close from the confirmation window.
******/
static void emDiscardFromPrompt(UIButton *button, void *data)
{
	emDismissPromptWindow();

	// Force close the document discarding changes
	if (emCloseDocInternal(em_data.close_state.doc, em_data.close_state.sub_doc, true) == EM_TASK_SUCCEEDED)
	{
		// For single doc or subdoc close, we wrap things up here and the continue will do nothing
		// For multi-doc modes, there will be more to do
		if (((em_data.close_state.close_mode == EMClose_Doc) && (em_data.close_state.sub_doc == NULL)) || 
			(em_data.close_state.close_mode == EMClose_SubDoc))
		emClearCloseState();
	}

	emContinueClosing();
}


/******
* This is an internal function to perform a forced save and close of all from the confirmation window.
******/
static void emSaveAllFromPrompt(UIButton *button, void *data)
{
	emDismissPromptWindow();

	// Cause close all to run in forced mode
	em_data.close_state.force_save = true;

	emContinueClosing();
}


/******
* This is an internal function to perform a forced close of all from the confirmation window.
******/
static void emDiscardAllFromPrompt(UIButton *button, void *data)
{
	emDismissPromptWindow();

	// Cause close all to run in forced mode
	em_data.close_state.force_close = true;

	emContinueClosing();
}


/******
* This is an internal function to cancel closing from the confirmation window.
******/
static void emCancelFromPrompt(UIButton *button, void *data)
{
	emDismissPromptWindow();

	// Cancel close behavior
	emClearCloseState();
}


/******
* This is an internal function to display a close prompt dialog.
* PARAMS:
*   doc - EMEditorDoc to prompt about
******/
static void emPromptForClose(EMEditorDoc *doc, EMEditorSubDoc *sub_doc)
{
	EMRegisteredType *type = NULL;
	UIWindow *window;
	UILabel *label;
	UIButton *button;
	char buf[1024];
	F32 y = 0;
	char *name;

	// Create the prompt window
	window = ui_WindowCreate("Save Before Close?", 0, 0, 400, 50);
	ui_WindowSetModal(window, true);
	ui_WindowSetClosable(window, false);

	// Lay out the message
	if (sub_doc)
		name = sub_doc->doc_name;
	else 
		name = doc->doc_name;

	if (doc->doc_type && stashFindPointer(em_data.registered_file_types, doc->doc_type, &type) && type->display_name)
		sprintf(buf,"The %s \"%s\" is not saved.  Do you want to save it?", type->display_name, name);
	else
		sprintf(buf,"\"%s\" is not saved.  Do you want to save it?", name);
	label = ui_LabelCreate(buf,50,0);
	ui_WidgetSetPositionEx(UI_WIDGET(label),-label->widget.width/2, 0, 0.5, 0, UITopLeft);
	ui_WindowAddChild(window, label);

	// Ensure the window is big enough
	window->widget.width = MIN(MAX(100 + label->widget.width,530),1000);

	if (((em_data.close_state.close_mode == EMClose_AllDocs) && (eaSize(&em_data.open_docs) > 1)) ||
		((em_data.close_state.close_mode != EMClose_SubDoc) && (eaSize(&doc->sub_docs) > 1)))
		y = 28;

	// Create the buttons
	button = ui_ButtonCreate("Save",0,0,emSaveFromPrompt,NULL);
	button->widget.width = 120;
	ui_WidgetSetPositionEx(UI_WIDGET(button),-190, y, 0.5, 0, UIBottomLeft);
	ui_WindowAddChild(window, button);

	button = ui_ButtonCreate("Discard Changes",0,0,emDiscardFromPrompt,NULL);
	button->widget.width = 120;
	ui_WidgetSetPositionEx(UI_WIDGET(button),-60, y, 0.5, 0, UIBottomLeft);
	ui_WindowAddChild(window, button);

	button = ui_ButtonCreate("Cancel Close",0,0,emCancelFromPrompt,NULL);
	button->widget.width = 120;
	ui_WidgetSetPositionEx(UI_WIDGET(button),70, y, 0.5, 0, UIBottomLeft);
	ui_WindowAddChild(window, button);

	if (((em_data.close_state.close_mode == EMClose_AllDocs) && (eaSize(&em_data.open_docs) > 1)) ||
		((em_data.close_state.close_mode != EMClose_SubDoc) && (eaSize(&doc->sub_docs) > 1)))
	{
		// Create the buttons
		button = ui_ButtonCreate("Save All",0,0,emSaveAllFromPrompt,NULL);
		button->widget.width = 120;
		ui_WidgetSetPositionEx(UI_WIDGET(button),-190, 0, 0.5, 0, UIBottomLeft);
		ui_WindowAddChild(window, button);

		button = ui_ButtonCreate("Discard All",0,0,emDiscardAllFromPrompt,NULL);
		button->widget.width = 120;
		ui_WidgetSetPositionEx(UI_WIDGET(button),-60, 0, 0.5, 0, UIBottomLeft);
		ui_WindowAddChild(window, button);

		window->widget.height += 28;
	}

	// Store the prompt state
	em_data.close_state.confirm_window = window;
	em_data.close_state.doc = doc;
	em_data.close_state.sub_doc = sub_doc;

	// Show the window
	elUICenterWindow(window);
	ui_WindowPresent(window);
}

/******
* This is the internal function that deals with closing a doc.
* PARAMS:
*   doc - EMEditorDoc to close
*   force - bool indicating whether to force the document to close or to first
*           prompt of unsaved changes
* RETURNS:
*   EMTaskStatus indicating whether document closed successfully
******/
static EMTaskStatus emCloseDocInternal(EMEditorDoc *doc, EMEditorSubDoc *sub_doc, bool force)
{
	int idx = eaFind(&em_data.open_docs, doc);
	EMTaskStatus status;

	// if doc is not open, do nothing
	if (idx < 0)
		return EM_TASK_SUCCEEDED;

	if (!sub_doc)
	{
		EMWorkspace *workspace = emWorkspaceFindDoc(doc);
		EMEditor *editor = doc->editor;
		int i;

		// if closing entire doc and it has sub-docs, recurse on sub-docs
		for (i = eaSize(&doc->sub_docs)-1; i >= 0; --i)
		{
			status = emCloseDocInternal(doc, doc->sub_docs[i], force);
			if (status != EM_TASK_SUCCEEDED)
			{
				// Nested call will either clear close state or put up a prompt, so simply return
				return status;
			}
		}

		// save the document if forced save mode
		if (em_data.close_state.force_save && !doc->saved)
		{
			status = emSaveDocEx(doc, false, NULL);
			if (status == EM_TASK_FAILED)
			{
				// Stop closing if fail to save
				emClearCloseState();
				return status;
			}
			if (status == EM_TASK_INPROGRESS)
			{
				// Try again on next tick
				return status;
			}
		}
			
		// prompt for save if the document has unsaved changes
		if (!force && !em_data.close_state.force_close && !doc->saved)
		{
			emPromptForClose(doc, sub_doc);
			return EM_TASK_INPROGRESS;
		}

		// use editor's close check function if specified to validate closure
		if (doc->editor->close_check_func && doc->editor->close_check_func(doc, em_data.close_state.should_quit) == 0)
		{
			// Stop closing if fail the check function
			emClearCloseState();
			return EM_TASK_FAILED;
		}

		// Save window positions before cleaning up the UI
		emDocSaveWindowPrefs(doc);

		// cleanup UI and remove doc from Asset Manager's open docs
		emRemoveDocumentUI(doc);
		eaRemove(&em_data.open_docs, idx);
		eaFindAndRemove(&doc->editor->open_docs, doc);

		// if closed doc is active, switch focus to another doc in the workspace, or
		// just set focus to NULL doc for the editor
		if (em_data.current_doc == doc)
		{
			EMEditorDoc *switch_to_doc = NULL;
			if (workspace)
			{
				for (i = 0; i < eaSize(&workspace->open_docs); i++)
				{
					if (workspace->open_docs[i] != doc)
					{
						switch_to_doc = workspace->open_docs[i];
						break;
					}
				}
			}

			emSetActiveEditorDoc(switch_to_doc, NULL);
		}

		// dissociate all files
		emDocRemoveAllFiles(doc, true);

		// invoke close function, which should deal with freeing document memory
		editor->close_func(doc);

		// Call exit function after closing doc
		if (editor->exit_func && eaSize(&editor->open_docs) == 0)
			editor->exit_func(editor);
	}
	else // sub_doc
	{
		// Do nothing if sub_doc isn't open
		idx = eaFind(&doc->sub_docs, sub_doc);
		if (idx < 0)
			return EM_TASK_SUCCEEDED;

		// save the document if forced save mode
		if (em_data.close_state.force_save && !sub_doc->saved)
		{
			status = emSaveSubDocEx(doc, sub_doc, false, NULL);
			if (status == EM_TASK_FAILED)
			{
				// Stop closing if fail to save
				emClearCloseState();
				return status;
			}
			if (status == EM_TASK_INPROGRESS)
			{
				// Try again on next tick
				return status;
			}
		}

		// prompt for save if the document has unsaved changes
		if (!force && !em_data.close_state.force_close && !sub_doc->saved)
		{
			emPromptForClose(doc, sub_doc);
			return EM_TASK_INPROGRESS;
		}

		// TODO: Should there be a close check functions for sub-docs ?

		// dissociate all files
		emSubDocRemoveAllFiles(doc, sub_doc);

		// invoke close function, which should deal with freeing sub-document memory
		if (doc->editor->sub_close_func)
			doc->editor->sub_close_func(doc, sub_doc);

		// remove sub document from document
		eaFindAndRemove(&doc->sub_docs, sub_doc);
	}

	emRemoveSaveState(doc, sub_doc, false); // Don't try to save a closed doc
	emRemovePrompts(doc, sub_doc); // Don't try to prompt for a closed doc
	return EM_TASK_SUCCEEDED;
}


/******
* This function forces a doc to close.
* PARAMS:
*   doc - EMEditorDoc to close
* RETURNS:
*   bool indicating whether doc successfully closed
******/
EMTaskStatus emForceCloseDoc(EMEditorDoc *doc)
{
	EMTaskStatus status;

	em_data.close_state.close_mode = EMClose_Doc;

	status = emCloseDocInternal(doc, NULL, true);
	if (status != EM_TASK_INPROGRESS)
		emClearCloseState(); // Success or failure

	return status;
}


/******
* This function closes a doc.
* PARAMS:
*   doc - EMEditorDoc to close
* RETURNS:
*   EMTaskStatus indicating whether doc successfully closed
******/
EMTaskStatus emCloseDoc(EMEditorDoc *doc)
{
	EMTaskStatus status;

	em_data.close_state.close_mode = EMClose_Doc;

	status = emCloseDocInternal(doc, NULL, false);
	if (status != EM_TASK_INPROGRESS)
		emClearCloseState();

	return status;
}


/******
* This function closes all open documents.
******/
EMTaskStatus emCloseAllDocs(void)
{
	int i;
	EMTaskStatus status;

	// Start of close all
	em_data.close_state.close_mode = EMClose_AllDocs;

	for (i = eaSize(&em_data.open_docs)-1; i >= 0; --i)
	{
		EMEditorDoc *doc = em_data.open_docs[i];
		status = emCloseDocInternal(doc, NULL, em_data.close_state.force_close);
		if (status != EM_TASK_SUCCEEDED)
			return status; // Stop closing once one doesn't succeed
	}

	// Only get here if all documents were successfully closed
	if (em_data.close_state.should_quit)
	{
		// this is a hack to ensure save commands sent to server by a closing document are, in
		// fact, sent; this can probably only be properly remedied by having EM document state
		// management on the server
		em_data.close_state.quitting = true;
		emQueueFunctionCallEx(emSetShouldQuit, NULL, 60);
	}

	emClearCloseState();  // Need to do this after the should_quit test for correct behavior

	return EM_TASK_SUCCEEDED;
}


/******
* This function forces a sub-doc to close.
* PARAMS:
*   doc - EMEditorDoc that has a sub-doc
*   sub_doc - EMEditorSubDoc to close
* RETURNS:
*   EMTaskStatus indicating whether doc successfully closed
******/
EMTaskStatus emForceCloseSubDoc(EMEditorDoc *doc, EMEditorSubDoc *sub_doc)
{
	EMTaskStatus status;

	em_data.close_state.close_mode = EMClose_SubDoc;

	status = emCloseDocInternal(doc, sub_doc, true);
	if (status != EM_TASK_INPROGRESS)
		emClearCloseState();

	return status;
}


/******
* This function closes a sub-doc.
* PARAMS:
*   doc - EMEditorDoc that has a sub-doc
*   sub_doc - EMEditorSubDoc to close
* RETURNS:
*   EMTaskStatus indicating whether doc successfully closed
******/
EMTaskStatus emCloseSubDoc(EMEditorDoc *doc, EMEditorSubDoc *sub_doc)
{
	EMTaskStatus status;

	em_data.close_state.close_mode = EMClose_SubDoc;

	status = emCloseDocInternal(doc, sub_doc, false);
	if (status != EM_TASK_INPROGRESS)
		emClearCloseState();

	return status;
}


/******
* This function closes several sub-docs.
* PARAMS:
*   doc - EMEditorDoc that has a sub-doc
*   sub_docs - EMEditorSubDoc list to close
* RETURNS:
*   EMTaskStatus indicating whether docs successfully closed
******/
EMTaskStatus emCloseSubDocs(EMEditorDoc *doc, EMEditorSubDoc ***sub_docs)
{
	int i;
	EMTaskStatus status = EM_TASK_FAILED;
	em_data.close_state.close_mode = EMClose_SubDoc;

	for (i = eaSize(sub_docs) - 1; i >= 0; --i)
	{
		status = emCloseDocInternal(doc, (*sub_docs)[i], false);
		if (status != EM_TASK_SUCCEEDED)
		{
			return status; // Stop closing once one doesn't succeed
		}
	}

	if (status != EM_TASK_INPROGRESS)
	{
		emClearCloseState();
	}
	return status;
}


/******
* This function closes all open sub-documents.
* PARAMS:
*   doc - EMEditorDoc that has sub-docs
******/
EMTaskStatus emCloseAllSubDocs(EMEditorDoc *doc)
{
	int i;
	EMTaskStatus status;

	// Start of close all sub-docs
	em_data.close_state.close_mode = EMClose_AllSubDocs;

	for (i = eaSize(&doc->sub_docs)-1; i >= 0; --i)
	{
		EMEditorSubDoc *sub_doc = doc->sub_docs[i];
		status = emCloseDocInternal(doc, sub_doc, em_data.close_state.force_close);
		if (status != EM_TASK_SUCCEEDED)
			return status; // Stop closing once one doesn't succeed
	}

	// Only get here if all sub-documents were successfully closed
	if (em_data.close_state.close_mode == EMClose_AllDocs)
		emCloseAllDocs(); // Get here if someone hits app close during this close
	else
		emClearCloseState();

	return EM_TASK_SUCCEEDED;
}


/******
* This function moves a doc to the specified workspace.
* PARAMS:
*   doc - EMEditorDoc to move
*   workspace_name - string name of the workspace where the document will be moved
******/
void emMoveDocToWorkspace(EMEditorDoc *doc, const char *workspace_name)
{
	EMWorkspace *workspace = emWorkspaceGet(workspace_name, false);

	if (!workspace || workspace == emWorkspaceFindDoc(doc))
		return;

	assert(doc->ui_tab);

	// remove doc from old workspace
	emRemoveDocumentUI(doc);

	// add document to new workspace
	eaPush(&workspace->open_docs, doc);

	emSetActiveEditorDoc(doc, NULL);
}

static EMEditorHistoryEntry* emHistoryItemCreate(const char *pcName, const char *pcType)
{
	EMEditorHistoryEntry *pItem = calloc(1, sizeof(EMEditorHistoryEntry));
	pItem->pcName = StructAllocString(pcName);
	pItem->pcType = StructAllocString(pcType);
	return pItem;
}

static void emHistoryItemFree(EMEditorHistoryEntry *pItem)
{
	StructFreeString(pItem->pcName);
	StructFreeString(pItem->pcType);
	free(pItem);
}

static bool emHistorySameDoc(EMEditorHistoryEntry *pItem, const char *pcName, const char *pcType)
{
	if(stricmp_safe(pItem->pcName, pcName) != 0)
		return false;
	if(stricmp_safe(pItem->pcType, pcType) != 0)
		return false;
	return true;
}

static EMEditorHistoryEntry* emHistoryGetTopDoc()
{
	int iHistorySize = eaSize(&em_data.history);
	if(iHistorySize == 0)
		return NULL;
	assert(em_data.history_idx >= 0 && em_data.history_idx < iHistorySize);
	return em_data.history[em_data.history_idx];
}

static void emHistoryPushItem(const char *pcName, const char *pcType)
{
	int i;
	EMEditorHistoryEntry *pTopItem = emHistoryGetTopDoc();
	if(stricmp_safe(pcType, "zone") == 0)
		pcName = "worldmap";

	//If already on the top, don't do anything
	if(pTopItem && emHistorySameDoc(pTopItem, pcName, pcType))
		return;

	//If we had undone a few times, clear what was passed over
	while(em_data.history_idx > 0) {
		EMEditorHistoryEntry *pItem = eaRemove(&em_data.history, 0);
		emHistoryItemFree(pItem);
		em_data.history_idx--;
	}

	//If already in the history, remove from the history and bring to the top
	for ( i=0; i < eaSize(&em_data.history); i++ ) {
		EMEditorHistoryEntry *pItem = em_data.history[i];
		if(emHistorySameDoc(pItem, pcName, pcType)) {
			eaRemove(&em_data.history, i);
			eaInsert(&em_data.history, pItem, 0);
			return;
		}
	}

	//Otherwise add a new entry
	eaInsert(&em_data.history, emHistoryItemCreate(pcName, pcType), 0);
}

AUTO_COMMAND ACMD_NAME("EM.HistoryPrevDoc");
void emHistorySelectPreviousItem()
{
	EMEditorHistoryEntry *pTopItem;
	if(em_data.history_idx >= eaSize(&em_data.history)-1)
		return;
	if(em_data.current_doc)
		em_data.history_idx++;
	pTopItem = emHistoryGetTopDoc();
	if(pTopItem)
		emOpenFileEx(pTopItem->pcName, pTopItem->pcType);
}

AUTO_COMMAND ACMD_NAME("EM.HistoryNextDoc");
void emHistorySelectNextItem()
{
	EMEditorHistoryEntry *pTopItem;
	if(em_data.history_idx <= 0)
		return;
	if(em_data.current_doc)
		em_data.history_idx--;
	pTopItem = emHistoryGetTopDoc();
	if(pTopItem)
		emOpenFileEx(pTopItem->pcName, pTopItem->pcType);
}


/******
* This function sets the specified editor doc to be active.  The doc MUST BE OPEN before it can be active.
* PARAMS:
*   doc - EMEditorDoc (must be open) to set active
*******/
void emSetActiveEditorDoc(EMEditorDoc *doc, EMEditor *editor)
{
	EMWorkspace *workspace;
	static U32 g_DocSortOrderTop = 0;

	assert(!doc || !editor || doc->editor == editor);
	if (doc && !editor)
		editor = doc->editor;

	// don't do anything if the doc is already the active one
	if ((doc || editor) && (doc == em_data.current_doc && editor == em_data.current_editor))
		return;

	// if this occurs outside of the editor, queue it up so it happens the next time someone enters the editor
	if (!em_data.editor_mode)
	{
		em_data.current_doc = doc;
		return;
	}

	// ensure doc is open; otherwise, switch to previously open doc
	if (doc && eaFind(&em_data.open_docs, doc) < 0)
		return;

	// cache previous open doc for tab switching between docs
	if(doc != em_data.current_doc && em_data.prev_open_doc != em_data.current_doc && em_data.current_doc != NULL)
		em_data.prev_open_doc = em_data.current_doc;

	if(doc)
		emHistoryPushItem(doc->doc_name, doc->doc_type);

	// Save prefs on switching docs before modifying windows
	emDocSaveWindowPrefs(em_data.current_doc); 

	// take focus from current document
	emShowDocumentUI(em_data.current_doc, NULL, false, false);
	if (em_data.current_doc && em_data.current_doc->editor->keybinds)
		keybind_PopProfile(em_data.current_doc->editor->keybinds);

	if (em_data.current_doc && em_data.current_doc->editor->lost_focus_func)
		em_data.current_doc->editor->lost_focus_func(em_data.current_doc);

	// set focus on specified document
	inpClear();
	mouseClear();
	em_data.current_doc = doc;
	em_data.current_editor = editor;
	emMotDCycle();
	if(editor)
		editor->sort_order = (++g_DocSortOrderTop);
	emDocApplyWindowPrefs(em_data.current_doc, false);
	emShowDocumentUI(doc, editor, true, true);
	if (editor && editor->keybinds)
		keybind_PushProfile(editor->keybinds);

	// remove camera keybinds
	keybind_PopProfileEx(em_data.freecam_profile, InputBindPriorityDevelopment);
	keybind_PopProfileEx(em_data.standardcam_profile, InputBindPriorityDevelopment);

	editor_RefreshAllShared();

	if ( editor ) 
	{
		//if we're going into the editor, disable the "EntityBinds" profile
		gclKeyBindDisable();
	}
	else if ( !gclKeyBindIsEnabled() && GSM_IsStateActive(GCL_GAMEPLAY) )
	{
		//when leaving the editor, re-enable "EntityBinds"
		gclKeyBindEnable();
	}

	// if showing the world, ensure camera keybinds take precedence over editor keybinds
	if (editor && ((!editor->hide_world && !editor->force_editor_cam) || editor->use_em_cam_keybinds))
	{
		// sync the editor camera's mode
		if (editor->camera)
		{
			gfxCameraSwitchMode(editor->camera, em_data.worldcam->mode_switch);
			emFreecamApplyPrefs();
		}
		keybind_PushProfileEx(em_data.worldcam->mode_switch ? em_data.freecam_profile : em_data.standardcam_profile, InputBindPriorityDevelopment);
	}

	if (editor && editor->hide_world)
	{
		WorldRegion *region = worldGetEditorWorldRegion();
		zmapRegionSetType(NULL, region, editor->region_type);
	}

	if (editor && editor->got_focus_func)
		editor->got_focus_func(doc);

	// handle last document tracking
	workspace = emWorkspaceFindDoc(doc);
	if (workspace)
	{
		int idx = eaFind(&workspace->open_docs, doc);
		assert(idx > -1);
		eaMove(&workspace->open_docs, 0, idx);
	}

	// check for any changed files and prompt for reload as required
	if (doc && editor && editor->reload_prompt)
	{
		EMFile **changed_files = NULL;

		emDocGetChangedFiles(doc, &changed_files, true);
		if (eaSize(&changed_files) > 0)
		{
			emDialogReloadPrompt(doc);
			return;
		}
	}
}

/******
* This returns the currently active (focused) editor doc.  If none is set to active, it sets the first
* open doc to active before returning it.
* RETURNS:
*   EMEditorDoc currently active; NULL if no docs are open
******/
EMEditorDoc *emGetActiveEditorDoc(void)
{
	return em_data.current_doc;
}

/******
* This returns the currently active (focused) editor.
* RETURNS:
*   EMEditor currently active; NULL if no editors are open
******/
EMEditor *emGetActiveEditor(void)
{
	if (!emIsEditorActive())
	{
		return NULL;
	}
	return em_data.current_editor;
}

/******
* This function returns the editor doc whose name and type matches the specified values.
* PARAMS:
*   name - string name given to the doc
*   type - string type of the doc
* RETURNS:
*   EMEditorDoc that is open whose name and type match the parameters
******/
EMEditorDoc *emGetEditorDoc(const char *name, const char *type)
{
	int i;

	// return a doc if it is found to be open
	for (i = 0; i < eaSize(&em_data.open_docs); ++i)
	{
		if (stricmp(em_data.open_docs[i]->doc_name, name) == 0
			&& stricmp(em_data.open_docs[i]->doc_type, type) == 0)
			return em_data.open_docs[i];
	}
	return NULL;
}

/******
* This function returns whether or not a given doc pointer is valid.
* PARAMS:
*   doc - The editor doc
* RETURNS:
*   True if the doc pointer is valid.
******/
bool emIsDocOpen(EMEditorDoc *doc)
{
	int i;

	for (i = 0; i < eaSize(&em_data.open_docs); ++i)
	{
		if (em_data.open_docs[i] == doc)
			return true;
	}

	return false;
}


/******
* Fills in the document name with a unique name based on the provided base_name, and
* checking currently open docs and the dictionary (if one is provided) to avoid duplication.
******/
void emMakeUniqueDocName(EMEditorDoc *doc, const char *base_name, const char *doc_type, const char *dict_name)
{
	char result_buf[260];
	int count = 0;

	// Strip off trailing digits and underbar
	strcpy(doc->doc_name, base_name);
	while(doc->doc_name[0] && (doc->doc_name[strlen(doc->doc_name)-1] >= '0') && (doc->doc_name[strlen(doc->doc_name)-1] <= '9')) {
		doc->doc_name[strlen(doc->doc_name)-1] = '\0';
	}
	if (doc->doc_name[0] && doc->doc_name[strlen(doc->doc_name)-1] == '_') {
		doc->doc_name[strlen(doc->doc_name)-1] = '\0';
	}

	// Generate new name
	do {
		++count;
		sprintf(result_buf,"%s_%d",doc->doc_name,count);
	} while (emGetEditorDoc(result_buf,doc_type) || (dict_name && resGetInfo(dict_name,result_buf)));

	strcpy(doc->doc_name, result_buf);
}


/******
* Command to create a new document of the type corresponding to the currently open
* editor's default type.
******/
AUTO_COMMAND ACMD_NAME("EM.NewDoc");
void emNewDocFromActiveDoc(void)
{
	EMEditor *editor = NULL;
	EMEditorDoc *doc = emGetActiveEditorDoc();

	if (doc)
		editor = doc->editor;
	if (!editor)
	{
		// Check if only one editor in workspace, ignoring global editors
		int count = eaSize(&em_data.active_workspace->editors);
		int i;
		for (i = eaSize(&em_data.active_workspace->editors) - 1; i >= 0; --i)
		{
			if (em_data.active_workspace->editors[i]->type == EM_TYPE_GLOBAL)
				--count;
			else
				editor = em_data.active_workspace->editors[i];
		}
		if (count != 1)
			editor = NULL;
	}

	if (editor)
		emNewDoc(editor->default_type, NULL);
}

/******
* Command to open the picker for the currently open editor if the editor only has one
* associated picker.
******/
AUTO_COMMAND ACMD_NAME("EM.OpenDoc");
void emOpenDocFromActiveDoc(void)
{
	EMEditor *editor = NULL;
	EMEditorDoc *doc = emGetActiveEditorDoc();

	if (doc)
		editor = doc->editor;
	if (!editor)
	{
		// Check if only one editor in workspace, ignoring global editors
		int count = eaSize(&em_data.active_workspace->editors);
		int i;
		for (i = eaSize(&em_data.active_workspace->editors) - 1; i >= 0; --i)
		{
			if (em_data.active_workspace->editors[i]->type == EM_TYPE_GLOBAL)
				--count;
			else
				editor = em_data.active_workspace->editors[i];
		}
		if (count != 1)
			editor = NULL;
	}

	if (editor && (eaSize(&editor->pickers) == 1))
		emPickerShow(editor->pickers[0], NULL, true, NULL, NULL);
}

/******
* Command to save the active document.
******/
AUTO_COMMAND ACMD_NAME("EM.Save") ACMD_ACCESSLEVEL(0);
void emSaveActiveDoc(void)
{
	EMEditorDoc *doc = emGetActiveEditorDoc();
	if (doc && !emGetDocSaved(doc))
		emQueueFunctionCallStatus(NULL, NULL, emSaveDoc, doc, -1);
}

/******
* Command to save the active document to a new location.
******/
AUTO_COMMAND ACMD_NAME("EM.SaveAs");
void emSaveActiveDocAs(void)
{
	EMEditorDoc *doc = emGetActiveEditorDoc();
	if (doc)
		emQueueFunctionCallStatus(NULL, NULL, emSaveDocAs, doc, -1);
}

/******
* Command to close the active document.
******/
AUTO_COMMAND ACMD_NAME("EM.Close");
void emCloseActiveDoc(void)
{
	EMEditorDoc *doc = emGetActiveEditorDoc();
	if (doc)
		emCloseDoc(doc);
}

/******
* Command to select previous tab in the workspace
******/
AUTO_COMMAND ACMD_NAME("EM.PrevDoc");
void emSelectPrevDoc(void)
{
	EMEditorDoc *doc = emGetActiveEditorDoc();
	EMWorkspace *workspace = em_data.active_workspace;

	if (workspace && doc && eaSize(&workspace->open_docs) > 1)
	{
		int i = eaFind(&workspace->tab_group->eaTabs, doc->ui_tab);

		if (i == -1)
			return;
		i--;
		if (i < 0)
			i += eaSize(&workspace->tab_group->eaTabs);
		ui_TabGroupSetActive(workspace->tab_group, workspace->tab_group->eaTabs[i]);
	}
}

/******
* Command to select next tab in the workspace
******/
AUTO_COMMAND ACMD_NAME("EM.NextDoc");
void emSelectNextDoc(void)
{
	EMEditorDoc *doc = emGetActiveEditorDoc();
	EMWorkspace *workspace = em_data.active_workspace;

	if (workspace && doc && eaSize(&workspace->open_docs) > 1)
	{
		int i = eaFind(&workspace->tab_group->eaTabs, doc->ui_tab);

		if (i == -1)
			return;
		i = (i + 1) % eaSize(&workspace->tab_group->eaTabs);
		ui_TabGroupSetActive(workspace->tab_group, workspace->tab_group->eaTabs[i]);
	}
}


/********************
* FILE MANAGEMENT
********************/
/******
* This function is called by the folder cache to ensure that EMFile data is kept
* up-to-date and that documents are reloaded as necessary when files change on
* disk.
* PARAMS:
*   relpath - string filename of the file that has changed
******/
static void emFileChanged(const char *relpath, int when)
{
	EMFile *file = NULL;
	char fullname[MAX_PATH];

	if (!relpath || !relpath[0] || !em_data.files)
		return;

	// get the properly formatted name of the file and search for it among
	// the registered EMFile's
	fileLocateWrite(relpath, fullname);
	stashFindPointer(em_data.files, fullname, &file);

	if (file)
	{
		int i;

		// update file statuses
		file->read_only = !!fileIsReadOnly(fullname);
		file->file_timestamp = fileLastChanged(fullname);

		for (i = 0; i < eaSize(&file->docs); i++)
		{
			EMEditorDoc *doc = file->docs[i]->doc;
			if (doc->editor && !doc->editor->do_not_reload)
			{
				// if the file has changed and the document is not aware of the change
				if (file->docs[i]->last_timestamp != file->file_timestamp  || doc->editor->force_reload)
				{
					if (doc->editor->reload_prompt)
						emQueueFunctionCall(emDialogReloadPrompt, doc);
					else
						emQueueFunctionCall(emReloadDoc, doc);
				}
			}
		}

		// TODO: is it possible to get a more accurate determination of the checkout state without doing a
		// slow query?
		file->checked_out = !file->read_only;
	}

	// signal the EM to do any necessary syncing for the UI
	em_data.files_changed = 1;
}

/******
* This function finds an EMFile registered with the Asset Manager.  If no file of
* the specified filename exists, a new one is created if desired.
* PARAMS:
*   filename - string filename to look for; use relative path
*   create - bool instructing function to create a new EMFile and return it if one
*            matching filename is not found
* RETURNS:
*   EMFile corresponding to the specified filename
******/
EMFile *emGetFile(const char *filename, bool create)
{
	EMFile *file = NULL;
	char fullname[MAX_PATH];

	if (!filename || !filename[0])
		return NULL;

	// create file stash table if not yet created
	if (!em_data.files)
		em_data.files = stashTableCreateWithStringKeys(512, StashDeepCopyKeys);

	// get full path of file and look for whether an EMFile for it exists
	fileLocateWrite(filename, fullname);
	stashFindPointer(em_data.files, fullname, &file);
	if (!file && create)
	{
		// if not, create an EMFile
		file = calloc(sizeof(*file), 1);
		strcpy(file->filename, fullname);
		file->read_only = !!fileIsReadOnly(fullname);
		file->checked_out = !file->read_only;
		file->file_timestamp = fileLastChanged(fullname);
		stashAddPointer(em_data.files, fullname, file, true);
	}

	return file;
}

static void emRebuildFileList(EMEditorDoc *doc)
{
	int i, j;

	// Clear previous data
	eaClear(&doc->all_files);

	for (i = 0; i < eaSize(&doc->files); i++)
		eaPush(&doc->all_files, doc->files[i]);

	for (i = 0; i < eaSize(&doc->sub_docs); i++)
	{
		for (j = 0; j < eaSize(&doc->sub_docs[i]->files); j++)
			eaPushUnique(&doc->all_files, doc->sub_docs[i]->files[j]);
	}
}

/******
* This function associates a file with a doc.  This function should always be used when making
* a document dependent on a file, as it stores the association in several places.
* PARAMS:
*   doc - EMEditorDoc to which the file should be associated
*   filename - string relative path of file to associate
******/
void emDocAssocFile(EMEditorDoc *doc, const char *filename)
{
	EMFile *file = emGetFile(filename, true);
	EMFileAssoc *assoc = calloc(1, sizeof(EMFileAssoc));
	int i;

	assert(file);

	// ensure file association is unique
	for (i = 0; i < eaSize(&doc->files); i++)
		if (doc->files[i]->file == file)
			return;

	// create association
	assoc->file = file;
	assoc->doc = doc;
	assoc->last_timestamp = file->file_timestamp;

	// store association
	eaPush(&file->docs, assoc);
	eaPush(&doc->files, assoc);

	emRebuildFileList(doc);
}

/******
* This function dissociates a file from a doc.  This function should always be used
* to remove a document from a file, as the association is stored in several places.
* PARAMS:
*   doc - EMEditorDoc from which the file should be dissociated
*   filename - string relative path of file to dissociate
******/
void emDocRemoveFile(EMEditorDoc *doc, const char *filename)
{
	EMFile *file = emGetFile(filename, false);
	int i;

	// if file doesn't exist, then it cannot possibly be associated with doc
	if (!file)
		return;

	for (i = 0; i < eaSize(&doc->files); i++)
	{
		// find the file among the doc's associations
		if (doc->files[i]->file == file)
		{
			// remove the doc association from file
			eaFindAndRemove(&file->docs, doc->files[i]);

			// if the file has no more docs referencing it, destroy it
			/* TODO: deal with map layers referencing the file as well
			if (eaSize(&file->docs) == 0)
			{
				EMFile *temp;
				stashRemovePointer(em_data.files, file->filename, &temp);
				SAFE_FREE(temp);
			}
			*/

			// remove the association from the doc and free it
			free(eaRemove(&doc->files, i));
			return;
		}
	}

	emRebuildFileList(doc);
}

/******
* This function does the same thing as emDocRemoveFile, except that
* it does it for all files on the doc.
* PARAMS:
*   doc - EMEditorDoc from which all files should be dissociated
*   include_sub_docs - Also removes all on sub-docs
******/
void emDocRemoveAllFiles(EMEditorDoc *doc, bool include_sub_docs)
{
	int i;

	for (i = eaSize(&doc->files) - 1; i >= 0; i--)
	{
		EMFile *file = doc->files[i]->file;

		// remove the doc association from file
		eaFindAndRemove(&file->docs, doc->files[i]);

		// if the file has no more docs referencing it, destroy it
		if (eaSize(&file->docs) == 0)
		{
			EMFile *temp;
			stashRemovePointer(em_data.files, file->filename, &temp);
			SAFE_FREE(temp);
		}

		// remove the association from the doc and free it
		free(eaRemove(&doc->files, i));
	}

	if (include_sub_docs)
	{
		for (i = eaSize(&doc->sub_docs) - 1; i >= 0; i--)
		{
			emSubDocRemoveAllFiles(doc, doc->sub_docs[i]);
		}
	}

	emRebuildFileList(doc);
}

/******
* This function associates a file with a sub-doc.  This function should always be used when making
* a sub-document dependent on a file, as it stores the association in several places.
* PARAMS:
*   doc - EMEditorDoc which contains the sub-doc
*   sub_doc - EMEditorSubDoc to which the file should be associated
*   filename - string relative path of file to associate
******/
void emSubDocAssocFile(EMEditorDoc *doc, EMEditorSubDoc *sub_doc, const char *filename)
{
	EMFile *file = emGetFile(filename, true);
	EMFileAssoc *assoc = calloc(1, sizeof(EMFileAssoc));
	int i;

	assert(file);

	// ensure file association is unique
	for (i = 0; i < eaSize(&sub_doc->files); i++)
		if (sub_doc->files[i]->file == file)
			return;

	// create association
	assoc->file = file;
	assoc->doc = doc;
	assoc->sub_doc = sub_doc;
	assoc->last_timestamp = file->file_timestamp;

	// store association
	eaPush(&file->docs, assoc);
	eaPush(&sub_doc->files, assoc);

	emRebuildFileList(doc);
}

/******
* This function dissociates a file from a sub-doc.  This function should always be used
* to remove a sub-document from a file, as the association is stored in several places.
* PARAMS:
*   doc - EMEditorDoc which contains the sub-doc
*   sub_doc - EMEditorSubDoc from which the file should be dissociated
*   filename - string relative path of file to dissociate
******/
void emSubDocRemoveFile(EMEditorDoc *doc, EMEditorSubDoc *sub_doc, const char *filename)
{
	EMFile *file = emGetFile(filename, false);
	int i;

	// if file doesn't exist, then it cannot possibly be associated with doc
	if (!file)
		return;

	for (i = 0; i < eaSize(&sub_doc->files); i++)
	{
		// find the file among the doc's associations
		if (sub_doc->files[i]->file == file)
		{
			// remove the doc association from file
			eaFindAndRemove(&file->docs, sub_doc->files[i]);

			// if the file has no more docs referencing it, destroy it
			if (eaSize(&file->docs) == 0)
			{
				EMFile *temp;
				stashRemovePointer(em_data.files, file->filename, &temp);
				SAFE_FREE(temp);
			}

			// remove the association from the doc and free it
			free(eaRemove(&sub_doc->files, i));
			return;
		}
	}

	emRebuildFileList(doc);
}

/******
* This function does the same thing as emSubDocRemoveFile, except that
* it does it for all files on the sub-doc.
* PARAMS:
*   doc - EMEditorDoc that contains the sub-doc
*   sub_doc - EMEditorSubDoc from which all files should be dissociated
******/
void emSubDocRemoveAllFiles(EMEditorDoc *doc, EMEditorSubDoc *sub_doc)
{
	int i;

	for (i = eaSize(&sub_doc->files) - 1; i >= 0; i--)
	{
		EMFile *file = sub_doc->files[i]->file;

		// remove the doc association from file
		eaFindAndRemove(&file->docs, sub_doc->files[i]);

		// if the file has no more docs referencing it, destroy it
		if (eaSize(&file->docs) == 0)
		{
			EMFile *temp;
			stashRemovePointer(em_data.files, file->filename, &temp);
			SAFE_FREE(temp);
		}

		// remove the association from the doc and free it
		free(eaRemove(&sub_doc->files, i));
	}

	emRebuildFileList(doc);
}

/******
* This function populates an array with the files associated to a specified doc.
* PARAMS:
*   doc - EMEditorDoc whose files will be retrieved
*   files - EMFile EArray to populate
*   include_sub_docs - If true, files from sub-documents are included
******/
void emDocGetFiles(EMEditorDoc *doc, EMFile ***files, bool include_sub_docs)
{
	int i, j;

	for (i = 0; i < eaSize(&doc->files); i++)
		eaPush(files, doc->files[i]->file);

	if (include_sub_docs)
	{
		for (i = 0; i < eaSize(&doc->sub_docs); i++)
		{
			for (j = 0; j < eaSize(&doc->sub_docs[i]->files); j++)
				eaPushUnique(files, doc->sub_docs[i]->files[j]->file);
		}
	}
}

/******
* This function populates an array with the files associated to a specified subdoc.
* PARAMS:
*   subdoc - EMEditorSubDoc whose files will be retrieved
*   files - EMFile EArray to populate
******/
void emSubDocGetFiles(EMEditorSubDoc *subdoc, EMFile ***files)
{
	int i;

	for (i = 0; i < eaSize(&subdoc->files); i++)
		eaPush(files, subdoc->files[i]->file);
}

/******
* This function populates an array with the files associated to a specified doc
* that have changed since the file was last read in the doc's session.  This function
* is especially useful for reload functions to determine which files need to be
* reloaded.
* PARAMS:
*   doc - EMEditorDoc whose files will be retrieved
*   changed_files - EMFile EArray to be populated
*   include_sub_docs - If true, files from sub-docs are included
******/
void emDocGetChangedFiles(EMEditorDoc *doc, EMFile ***changed_files, bool include_sub_docs)
{
	int i, j;

	for (i = 0; i < eaSize(&doc->files); i++)
		if (doc->files[i]->last_timestamp != doc->files[i]->file->file_timestamp)
			eaPush(changed_files, doc->files[i]->file);

	if (include_sub_docs)
	{
		for (i = 0; i < eaSize(&doc->sub_docs); i++)
		{
			for (j = 0; j < eaSize(&doc->sub_docs[i]->files); j++)
				if (doc->sub_docs[i]->files[j]->last_timestamp != doc->sub_docs[i]->files[j]->file->file_timestamp)
					eaPushUnique(changed_files, doc->sub_docs[i]->files[j]->file);
		}
	}
}

/******
* This function signals a doc to flag one of its files as "updated", meaning that
* it has changed and that the document is aware of the change.  This flagging 
* happens at the association level, so if two docs reference the same file, calling
* this update function for the first doc will cause the second doc to be reloaded.
* This should be called on a doc's file every time an editor writes something to the
* file.
* PARAMS:
*   doc - EMEditorDoc to notify about the file change
*   file - EMFile that has been updated
******/
void emDocUpdateFile(EMEditorDoc *doc, EMFile *file)
{
	int i, j;

	for (i = 0; i < eaSize(&doc->files); i++)
		if (doc->files[i]->file == file)
			doc->files[i]->last_timestamp = file->file_timestamp;

	for (i = 0; i < eaSize(&doc->sub_docs); i++)
	{
		for (j = 0; j < eaSize(&doc->sub_docs[i]->files); j++)
			if (doc->sub_docs[i]->files[j]->file == file)
				doc->sub_docs[i]->files[j]->last_timestamp = file->file_timestamp;
	}
}


/********************
* MAP LAYERS
********************/
/******
* This function creates a new map layer that can be displayed in the Editor Manager's Map Layers
* panel.
* PARAMS:
*   filename - string filename for this file; used for gimme operations and tracking changes
*              to the file on disk
*   layer_name - string layer name to be displayed on the panel
*   layer_type - string type to be displayed on the panel
*   user_layer_ptr - data to be associated with this layer; is passed to all layer callbacks
*   toggle_vis_func - EMMapLayerVisFunc called when the user toggles the layer's visibility
*   draw_func - EMMapLayerFunc called every frame while the layer is visible
* RETURNS:
*   EMMapLayerType created
******/
EMMapLayerType *emMapLayerCreate(const char *filename, const char *layer_name, const char *layer_type, void *user_layer_ptr, EMMapLayerVisFunc toggle_vis_func, EMMapLayerFunc draw_func)
{
	EMMapLayerType* map_layer = StructCreate(parse_EMMapLayerType);
	map_layer->file = emGetFile(filename, true);
	map_layer->layer_name = StructAllocString(layer_name);
	map_layer->layer_type = StructAllocString(layer_type);
	map_layer->visible = EditorPrefGetInt("Editor Manager", "Map Layers", map_layer->layer_name, 1);
	map_layer->visible_toggled_callback = toggle_vis_func;
	map_layer->draw_callback = draw_func;
	map_layer->user_layer_ptr = user_layer_ptr;
	map_layer->menu_funcs = NULL;
	map_layer->menu_item_names = NULL;
	return map_layer;
}

/******
* This function frees up the memory associated with a specified map layer
* PARAMS:
*   map_layer - EMMapLayerType to destroy
******/
void emMapLayerDestroy(EMMapLayerType *map_layer)
{
	eaDestroy(&map_layer->menu_funcs);
	StructDestroy(parse_EMMapLayerType, map_layer);
}

/******
* This function registers a layer with the Asset Manager's Map Layers panel.
* PARAMS:
*   map_layer - EMMapLayerType to register
*****/
void emMapLayerAdd(EMMapLayerType *map_layer)
{
	int i;

	// add to map layer list
	eaPush(&em_data.map_layers.layers, map_layer);
	map_layer->visible_toggled_callback(map_layer, map_layer->user_layer_ptr, map_layer->visible);

	// update type list
	for (i = 1; i <= eaSize(&em_data.map_layers.type_list); i++)
	{
		if (i < eaSize(&em_data.map_layers.type_list) && strcmpi(em_data.map_layers.type_list[i], map_layer->layer_type) == 0)
			break;
		if (i == eaSize(&em_data.map_layers.type_list) || strcmpi(map_layer->layer_type, em_data.map_layers.type_list[i]) < 0)
		{
			eaInsert(&em_data.map_layers.type_list, strdup(map_layer->layer_type), i);
			break;
		}
	}

	emMapLayerListRefresh();
}

/******
* This function unregisters a layer from the Asset Manager's Map Layers panel.
* PARAMS:
*   map_layer - EMMapLayerType to unregister
*****/
void emMapLayerRemove(EMMapLayerType *map_layer)
{
	int i;

	eaFindAndRemove(&em_data.map_layers.layers, map_layer);

	// remove type from type list if necessary
	for (i = 0; i < eaSize(&em_data.map_layers.layers); i++)
	{
		if (strcmpi(map_layer->layer_type, em_data.map_layers.layers[i]->layer_type) == 0)
			return;
	}
	for (i = 0; i < eaSize(&em_data.map_layers.type_list); i++)
	{
		if (strcmpi(map_layer->layer_type, em_data.map_layers.type_list[i]) == 0)
		{
			char *type_str = eaRemove(&em_data.map_layers.type_list, i);
			SAFE_FREE(type_str);
			break;
		}
	}

	emMapLayerListRefresh();
}

/******
* This function adds a menu item to the map layer right-click menu.
* PARAMS:
*   map_layer - EMMapLayerType for which to add the right-click menu item
*   item_name - string text to display on the menu item
*   item_callback - EMMapLayerFunc to call when the menu item is clicked
******/
void emMapLayerMenuItemAdd(EMMapLayerType *map_layer, const char *item_name, EMMapLayerFunc item_callback)
{
	eaPush(&map_layer->menu_item_names, StructAllocString(item_name));
#pragma warning(suppress:4054) // 'type cast' : from function pointer 'EMMapLayerFunc' to data pointer 'void *'
	eaPush(&map_layer->menu_funcs, (void*) item_callback);

	assert(eaSize(&map_layer->menu_item_names) == eaSize(&map_layer->menu_funcs));
}

/******
* This function returns whether the specified map layer is visible
* PARAMS:
*   map_layer - EMMapLayerType to check
* RETURNS:
*   bool whether the specified layer is visible
******/
bool emMapLayerGetVisible(EMMapLayerType *map_layer)
{
	return map_layer->visible;
}

/******
* This function sets the specified map layer to be visible
* PARAMS:
*   map_layer - EMMapLayerType to modify
*   visible - bool indicating whether to make the layer visible or not
******/
void emMapLayerSetVisible(EMMapLayerType *map_layer, bool visible)
{
	map_layer->visible = visible;

	// store preference
	EditorPrefStoreInt("Editor Manager", "Map Layers", map_layer->layer_name, map_layer->visible);

	if (map_layer->visible_toggled_callback)
		map_layer->visible_toggled_callback(map_layer, map_layer->user_layer_ptr, visible);
}

/******
* This function gets the specified map layer's associated filename
* PARAMS:
*   map_layer - EMMapLayerType whose filename is to be returned
* RETURNS:
*   string filename (absolute path)
******/
const char *emMapLayerGetFilename(EMMapLayerType *map_layer)
{
	return map_layer->file->filename;
}

/******
* This function gets the specified map layer's custom data pointer.
* PARAMS:
*   map_layer - EMMapLayerType whose data is to be returned
* RETURNS:
*   data associated with the map layer
******/
void *emMapLayerGetUserPtr(EMMapLayerType *map_layer)
{
	return map_layer->user_layer_ptr;
}


/********************
* UNDO/REDO
********************/
/******
* This function is a wrapper to EditUndoLast.
* PARAMS:
*   doc - EMEditorDoc whose last operation is to be undone
******/
void emUndo(EMEditorDoc* doc)
{
	if (doc->edit_undo_stack)
		EditUndoLast(doc->edit_undo_stack);
}

/******
* This function is a wrapper to EditRedoLast.
* PARAMS:
*   doc - EMEditorDoc whose last undone operation is to be redone
******/
void emRedo(EMEditorDoc *doc)
{
	if (doc->edit_undo_stack)
		EditRedoLast(doc->edit_undo_stack);
}

/******
* This command undoes the last operation for the currently active document.
******/
AUTO_COMMAND ACMD_NAME("EM.Undo");
void emCmdUndo()
{
	EMEditorDoc *current_doc = emGetActiveEditorDoc();
	if (current_doc)
		emUndo(current_doc);
}

/******
* This command redoes the last undone operation for the currently active document.
******/
AUTO_COMMAND ACMD_NAME("EM.Redo");
void emCmdRedo()
{
	EMEditorDoc *current_doc = emGetActiveEditorDoc();
	if (current_doc)
		emRedo(current_doc);
}


/********************
* CUT/COPY/PASTE
********************/
/******
* This function destroys the old contents of the clipboard.
******/
static void emClipboardFree(void)
{
	if (em_data.clipboard.parse_table)
		StructDestroyVoid(em_data.clipboard.parse_table, em_data.clipboard.data);
	else if (em_data.clipboard.free_func)
		em_data.clipboard.free_func(em_data.clipboard.data);
	em_data.clipboard.custom_type[0] = '\0';
	em_data.clipboard.parse_table = NULL;
	em_data.clipboard.free_func = NULL;
	em_data.clipboard.data = NULL;
}

/******
* This function copies a parsed struct to the clipboard.
* PARAMS:
*   pti - ParseTable of the struct
*   structptr - data to copy to the clipboard
******/
void emAddToClipboardParsed(ParseTable *pti, void *structptr)
{
	emClipboardFree();

	if (!structptr)
		return;
	em_data.clipboard.parse_table = pti;
	em_data.clipboard.data = StructCreateVoid(pti);
	StructCopyAllVoid(pti, structptr, em_data.clipboard.data);
}

/******
* This function copies a custom struct to the clipboard.
* PARAMS:
*   free_func - EMStructFunc that will be called to free the copied data the next time something
*	            writes to the clipboard
*   data - data to put into the clipboard
******/
void emAddToClipboardCustom(const char *type, EMStructFunc free_func, void *data)
{
	emClipboardFree();

	if (!data)
		return;
	strcpy(em_data.clipboard.custom_type, type);
	em_data.clipboard.data = data;
	em_data.clipboard.free_func = free_func;
}

/******
* This command invokes the editor's cut callback.
******/
AUTO_COMMAND ACMD_NAME("EM.Cut");
void emCmdCut(void)
{
	EMEditorDoc *current_doc = emGetActiveEditorDoc();
	if (current_doc && current_doc->editor->copy_func)
		current_doc->editor->copy_func(current_doc, true);
}

/******
* This command invokes the editor's copy callback.
******/
AUTO_COMMAND ACMD_NAME("EM.Copy");
void emCmdCopy(void)
{
	EMEditorDoc *current_doc = emGetActiveEditorDoc();
	if (current_doc && current_doc->editor->copy_func)
		current_doc->editor->copy_func(current_doc, false);
}

/******
* This command invokes the editor's paste callback.
******/
AUTO_COMMAND ACMD_NAME("EM.Paste");
void emCmdPaste(void)
{
	EMEditorDoc *current_doc = emGetActiveEditorDoc();
	if (current_doc && current_doc->editor->paste_func)
		current_doc->editor->paste_func(current_doc, em_data.clipboard.parse_table, em_data.clipboard.custom_type, em_data.clipboard.data);
}


/********************
* ASSET VIEWER INTEGRATION
********************/
/******
* This function is used by the asset viewer to preview an asset.
* PARAMS:
*   name - string name of the asset/file
*   type - string type of the asset/file
* RETURNS:
*   bool indicating whether preview function was found for the specified type and
*   executed successfully
******/
bool emPreviewFileEx(const char *name, const char *type)
{
	EMRegisteredType *registered_type;

	// fix up type string
	if (type[0] == '.')
		++type;

	// if file type is not registered, do nothing
	if (!em_data.registered_file_types || !stashFindPointer(em_data.registered_file_types, type, &registered_type))
		return false;

	// if preview function is not associated with file type
	if (!registered_type->preview_func)
		return false;

	// invoke preview function
	return registered_type->preview_func(name, type);
}

/******
* This function is a wrapper to emPreviewFileEx.
* PARAMS:
*   filename - string filename of the asset; type will be derived from extension
* RETURNS:
*   bool indicating whether preview function was found for the specified type and
*   executed successfully
******/
bool emPreviewFile(const char *filename)
{
	bool ret = false;
	char *filename_dup = strdup(filename);
	char *extension;

	if (extension = strrchr(filename_dup, '.'))
	{
		// if extension is found, split the filename into name and type and
		// call emPreviewFileEx
		*extension = 0;
		++extension;
		ret = emPreviewFileEx(filename_dup, extension);
	}

	SAFE_FREE(filename_dup);
	return ret;
}


/********************
* MISC
********************/
/******
* This function sets the specified editor to hide or display the world.  Use of this
* function is not necessary during editor initialization (the flag can be populated directly),
* but this function must be used in order to switch modes during runtime.
* PARAMS:
*   editor - EMEditor to change
*   hide_world - bool whether the editor should hide the world
******/
void emEditorHideWorld(SA_PARAM_OP_VALID EMEditor *editor, bool hide_world)
{
	if (!editor)
		editor = em_data.current_editor;

	// do nothing if the value is not changing
	if (!editor || editor->hide_world == (U32)hide_world)
		return;

	// set hide world flag
	editor->hide_world = hide_world;

	// remove camera-related stuff
	eaFindAndRemove(&editor->toolbars, em_data.title.camera_toolbar);
	keybind_PopProfileEx(em_data.freecam_profile, InputBindPriorityDevelopment);
	keybind_PopProfileEx(em_data.standardcam_profile, InputBindPriorityDevelopment);

	// deal with editor hiding world
	if (hide_world)
	{
		// initialize camera
		emCameraInit(&editor->camera);
		if (editor->camera_func)
			editor->camera->camera_func = editor->camera_func;
		else if (!editor->use_em_cam_keybinds)
			editor->camera->camera_func = gfxEditorCamFunc;
		gfxCameraControllerSetSkyOverride(editor->camera, "default_sky", NULL);
		gfxCameraSwitchMode(editor->camera, em_data.worldcam->mode_switch);
		emFreecamApplyPrefs();

		// remove camera toolbar
		eaFindAndRemove(&editor->toolbars, em_data.title.camera_toolbar);
	}
	// deal with editor unhiding world
	if (!hide_world || editor->use_em_cam_keybinds)
	{
		// insert camera toolbar
		eaInsert(&editor->toolbars, em_data.title.camera_toolbar, 0);

		// if showing the world, ensure camera keybinds take precedence over editor keybinds
		keybind_PushProfileEx(em_data.worldcam->mode_switch ? em_data.freecam_profile : em_data.standardcam_profile, InputBindPriorityDevelopment);
	}
}


/********************
* MAIN
********************/
/******
* This is where the various asset manager data is setup.
******/
static void emInit(void)
{
	if (!em_data.initialized)
	{
		EMEditorDoc *first_doc = NULL;
		char *pref_workspace = strdup(EditorPrefGetString("Editor Manager", "Option", "Active Workspace", NULL));
		int i;

		em_data.version = 1;

		em_data.standardcam_keybind_version = 1;
		em_data.freecam_keybind_version = 1;
		em_data.open_recent = createMRUList("EditorManager_OpenRecent", 16, 1024);
		emOptionsInit();
		emUIInit();

		em_data.search_panels.curr_num = 1;
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_ATTRIB_CHANGE|FOLDER_CACHE_CALLBACK_UPDATE, "*", emFileChanged);
		em_data.initialized = 1;

		// open documents on startup
		// primary editor
		if (em_data.primary_editor && emIsEditorOk(em_data.primary_editor))
		{
			emNewDoc(em_data.primary_editor->default_type, NULL);
			first_doc = em_data.current_doc;
		}

		// always open editors
		for (i = 0; i < eaSize(&em_data.editors); i++)
		{
			if (em_data.editors[i]->always_open)
				emNewDoc(em_data.editors[i]->default_type, NULL);
		}
		if (first_doc && em_data.current_doc != first_doc)
			emSetActiveEditorDoc(first_doc, NULL);

		// switch to preference workspace on first entry to editor manager
		if (pref_workspace)
		{
			EMWorkspace *workspace = emWorkspaceGet(pref_workspace, false);
			if (workspace)
			{
				emWorkspaceSetActive(workspace, NULL);
				emSetActiveEditorDoc(eaSize(&workspace->open_docs) > 0 ? workspace->open_docs[0] : NULL, NULL);
			}
			free(pref_workspace);
		}

		em_data.doc_before_em_exit = em_data.current_doc;
	}
}

void ugcEditMode(int enabled);


void emToggleEditorUI()
{
	if(!em_data.initialized)
		return;

	if(em_data.ui_hidden) {
		ui_PaneWidgetAddToDevice(UI_WIDGET(em_data.title.pane), NULL);
		ui_WidgetAddToDevice(UI_WIDGET(em_data.title.mode), NULL);
		if (em_data.active_workspace)
			ui_WidgetAddToDevice(UI_WIDGET(em_data.active_workspace->tab_group), NULL);
		ui_WidgetAddToDevice(UI_WIDGET(em_data.title.close_button), NULL);
		emSidebarShow(em_data.sidebar.show);
		em_data.ui_hidden = false;
	} else {
		ui_WidgetRemoveFromGroup(UI_WIDGET(em_data.title.pane));
		ui_WidgetRemoveFromGroup(UI_WIDGET(em_data.sidebar.pane));
		ui_WidgetRemoveFromGroup(UI_WIDGET(em_data.sidebar.hidden_pane));
		ui_WidgetRemoveFromGroup(UI_WIDGET(em_data.title.mode));
		if (em_data.active_workspace)
			ui_WidgetRemoveFromGroup(UI_WIDGET(em_data.active_workspace->tab_group));
		ui_WidgetRemoveFromGroup(UI_WIDGET(em_data.title.close_button));
		em_data.ui_hidden = true;
	}

	emPanelsShow(em_data.current_doc, NULL);
}

/******
* This function enables or disables editor mode for the game.
* PARAMS:
*   editor_mode - bool indicating whether the mode should be enabled or disabled
******/
void emSetEditorMode(bool editor_mode)
{
	int i, n;

#ifdef NO_EDITORS
	return;
#endif

	g_ui_State.bInEditor = !!editor_mode;

	if (em_data.editor_mode == (U32)!!editor_mode)
		return;

	inpClear();

	if (editor_mode)
	{
		if (g_ui_State.bInUGCEditor)
			ugcEditMode(0);

		ClipCursor(NULL);

		// halt existing camera movement
		globCmdParse("Camera.halt");

		// enable edit mode
		em_data.editor_mode = 1;

		// Set scale before setting active doc
		em_data.last_game_ui_scale = g_ui_State.scale;
		g_ui_State.scale = EditorPrefGetFloat("Editor Manager", "Option", "Scale", 1.0f);

		// initialize asset manager
		ui_SetActiveFamilies(UI_FAMILY_EDITOR);
		keybind_PushProfileName("EditorManager");
		emInit();
		emSetActiveEditorDoc(em_data.doc_before_em_exit, NULL);

		// display panes and global widgets
		// TODO: these can be removed after AM 1.0 is gone
		ui_PaneWidgetAddToDevice(UI_WIDGET(em_data.title.pane), NULL);
		ui_WidgetAddToDevice(UI_WIDGET(em_data.title.mode), NULL);
		if (em_data.active_workspace)
			ui_WidgetAddToDevice(UI_WIDGET(em_data.active_workspace->tab_group), NULL);
		ui_WidgetAddToDevice(UI_WIDGET(em_data.title.close_button), NULL);
		emSidebarShow(em_data.sidebar.show);
		em_data.ui_hidden = false;

		// disable position displays
		globCmdParse("showcampos 0");

		stringCacheDisableWarnings(); // Don't care about string cache resizing in edit mode

		// run enter-edit-mode callbacks
		n = eaSize(&enter_editor_callbacks);
		for (i = 0; i < n; ++i)
		{
			enter_editor_callbacks[i]->func1(enter_editor_callbacks[i]->data1);
			free(enter_editor_callbacks[i]);
		}
		eaSetSize(&enter_editor_callbacks, 0);

		emSidebarApplyCurrentScale();

		// Load the metadata
		resRequestAllResourcesInDictionary( "ZoneMapEncounterInfo" );
		resRequestAllResourcesInDictionary( "ZoneMapExternalMapSnap" );
	}
	else
	{
		// halt camera movement
		globCmdParse("Camera.halt");

		// disable edit mode
		// clear the current document's UI
		em_data.doc_before_em_exit = em_data.current_doc;
		emSetActiveEditorDoc(NULL, NULL);
		em_data.editor_mode = 0;
		ui_SetActiveFamilies(UI_FAMILY_GAME);
		keybind_PopProfileName("EditorManager");

		// hide panes
		// TODO: these can be removed after AM 1.0 is gone
		ui_WidgetRemoveFromGroup(UI_WIDGET(em_data.title.pane));
		ui_WidgetRemoveFromGroup(UI_WIDGET(em_data.sidebar.pane));
		ui_WidgetRemoveFromGroup(UI_WIDGET(em_data.sidebar.hidden_pane));
		ui_WidgetRemoveFromGroup(UI_WIDGET(em_data.title.mode));
		if (em_data.active_workspace)
			ui_WidgetRemoveFromGroup(UI_WIDGET(em_data.active_workspace->tab_group));
		ui_WidgetRemoveFromGroup(UI_WIDGET(em_data.title.close_button));

		// reshow position displays
		if (!em_data.outsource_mode)
		{
			globCmdParse("showcampos 1");
		}
		ui_SetFocus(NULL);
		em_data.last_crc = 0;
		g_ui_State.scale = em_data.last_game_ui_scale;
	}
}

/******
* This function switches the client back to game mode from Editor Manager mode.  It also spawns
* the player at the current camera's position.
******/
AUTO_COMMAND ACMD_NAME("EM.ExitAndSpawn");
void emExitAndSpawn(void)
{
	globCmdParse("Camera.halt");
	globCmdParse("FreeCamera.spawn_player");
	CommandEditMode(0);
}

/******
* This function can be used to tell whether the game's edit mode is active.
* RETURNS:
*   bool indicating whether editor is active
******/
#endif
bool emIsEditorActive(void)
{
#ifndef NO_EDITORS
	return !!em_data.editor_mode;
#else
	return false;
#endif
}
#ifndef NO_EDITORS

/******
* This function sets whether the editor is in outsource mode or not.  Outsource mode limits
* particular features that should not be accessible to outsourced artists.
* PARAMS:
*   outsource_mode - bool whether to put Asset Manager into outsource mode
******/
void emSetOutsourceMode(bool outsource_mode)
{
	em_data.outsource_mode = !!outsource_mode;
}

/******
* This function is called by the GameClient to set the appropriate camera
* depending on whether the Asset Manager is active and which document is
* currently open.  If the Asset Manager is not active, the normal game camera
* is used.  If an editor is open that is not using the world camera, the
* editor's camera is used.  Otherwise, the global Asset Manager camera is used,
* which holds its own position/rotation data apart from the game camera.
* PARAMS:
*   gamecamera - GfxCameraController corresponding to the main game camera
******/
#endif
void emSetActiveCamera(GfxCameraController *gameCamera, GfxCameraView *gameCameraView)
{
	// UGC editor -- should completely control editor
	if (g_ui_State.bInUGCEditor)
	{
		// Clear any previous cameras
		emCameraInit(&em_data.worldcam);
		gfxSetActiveCameraController(em_data.worldcam, true);
		gfxSetActiveCameraView(NULL, true);

		// Let UGC optionally override it
		ugcEditorSetCamera();
	}
	// normal game operation
	else if (!emIsEditorActive())
	{
		gfxSetActiveCameraController(gameCamera, true);
		gfxSetActiveCameraView(gameCameraView, true);
	}
#ifndef NO_EDITORS
	// editor is open that hides world
	else if (em_data.current_doc && (em_data.current_doc->editor->hide_world || em_data.current_doc->editor->force_editor_cam))
	{
		gfxSetActiveCameraController(em_data.current_doc->editor->camera, true);
		gfxSetActiveCameraView(NULL, true);
	}
	// editor is open that shows world
	else
	{
		emCameraInit(&em_data.worldcam);
		gfxSetActiveCameraController(em_data.worldcam, true);
		gfxSetActiveCameraView(NULL, true);
	}
#endif
}
#ifndef NO_EDITORS

/******
* This function indicates whether the editor is in freecam mode or not.
* RETURNS:
*   bool indicating whether the editor is in freecam mode
******/
bool emIsFreecamActive(void)
{
	return em_data.worldcam ? em_data.worldcam->mode_switch : false;
}

/******
* This function sets the editor manager camera to the specified camera's position/rotation.
* PARAMS:
*   gamecamera - GfxCameraController to which the asset manager camera will be moved
******/
#endif
void emSetDefaultCameraPosPyr(GfxCameraController *gamecamera)
{
#ifndef NO_EDITORS
	emCameraInit(&em_data.worldcam);
	gfxCameraControllerCopyPosPyr(gamecamera, em_data.worldcam);
#endif
}

/******
* This function is invoked by GameClient at the end of a frame to execute all queued functions.
******/
void emRunQueuedFunctions(void)
{
#ifndef NO_EDITORS
	const char *filename;
	int i, reset_count, reload_count;

	// run queued functions
	for (i = 0; i < eaSize(&queued_functions); ++i)
	{
		if (queued_functions[i]->frame_delay > 0 || queued_functions[i]->frame_delay == -1)
		{
			EMTaskStatus status;
			// deal with conditional queued functions
			if (queued_functions[i]->check_func && queued_functions[i]->check_func(queued_functions[i]->checkdata1))
			{
				queued_functions[i]->func1(queued_functions[i]->data1);
				free(queued_functions[i]);
				eaRemove(&queued_functions, i--);
			}			
			else if (queued_functions[i]->statuscheck_func)
			{
				status = queued_functions[i]->statuscheck_func(queued_functions[i]->checkdata1);
				if (status != EM_TASK_INPROGRESS)
				{				
					if (queued_functions[i]->status_func) 
						queued_functions[i]->status_func(status, queued_functions[i]->data1);
					free(queued_functions[i]);
					eaRemove(&queued_functions, i--);
				}
				else if (queued_functions[i]->frame_delay > 0)
					queued_functions[i]->frame_delay--;
			}
			else if (queued_functions[i]->statuscheck_func2)
			{
				status = queued_functions[i]->statuscheck_func2(queued_functions[i]->checkdata1, queued_functions[i]->checkdata2);
				if (status != EM_TASK_INPROGRESS)
				{
					if (queued_functions[i]->status_func2)
						queued_functions[i]->status_func2(status, queued_functions[i]->data1, queued_functions[i]->data2);
					free(queued_functions[i]);
					eaRemove(&queued_functions, i--);
				}
				else if (queued_functions[i]->frame_delay > 0)
					queued_functions[i]->frame_delay--;
			}
			else if (queued_functions[i]->frame_delay > 0)
				queued_functions[i]->frame_delay--;
		}
		else
		{
			if (!queued_functions[i]->check_func)
			{
				if (queued_functions[i]->func2)
					queued_functions[i]->func2(queued_functions[i]->data1, queued_functions[i]->data2);
				else
					queued_functions[i]->func1(queued_functions[i]->data1);
			}
			if (queued_functions[i]->status_func)
			{
				queued_functions[i]->status_func(EM_TASK_FAILED, queued_functions[i]->data1);
			}
			if (queued_functions[i]->status_func2)
			{
				queued_functions[i]->status_func2(EM_TASK_FAILED, queued_functions[i]->data1, queued_functions[i]->data2);
			}
			free(queued_functions[i]);
			eaRemove(&queued_functions, i--);
		}
	}

	// check for map change
	filename = zmapGetFilename(NULL);
	reset_count = worldGetResetCount(false);
	reload_count = worldGetResetCount(true);
	if (reload_count != current_reload_count || (filename && stricmp(current_map_filename, filename) != 0))
	{
		// if map has changed, invoke mapchange callbacks
		bool reset_changed = reset_count != current_reset_count || (filename && stricmp(current_map_filename, filename)!=0);
		for (i = 0; i < eaSize(&mapchange_callbacks); ++i)
			mapchange_callbacks[i]->func(mapchange_callbacks[i]->data, reset_changed);
		strcpy(current_map_filename, filename?filename:"");
		current_reload_count = reload_count;
	}
	current_reset_count = reset_count;
#endif
}

/******
* This function is called by GameClient to render editor-required objects.
* PARAMS:
******/
void emEditorDrawGhosts(void)
{
#ifndef NO_EDITORS
	int i;

	// do nothing if not in editor mode
	if (!em_data.editor_mode)
		return;

	// don't call ghost draw function if doc loading is still in progress
	if (em_data.current_doc && em_data.current_doc->process && em_data.current_doc->process->active)
		return;

	PERFINFO_AUTO_START_FUNC();

	if (em_data.current_doc && em_data.current_doc->editor->ghost_draw_func)
		em_data.current_doc->editor->ghost_draw_func(em_data.current_doc);

	if (em_data.current_doc && !em_data.current_doc->editor->hide_world)
	{
		for (i = 0; i < eaSize(&em_data.map_layers.layers); ++i)
		{
			if (em_data.map_layers.layers[i]->visible && em_data.map_layers.layers[i]->draw_callback)
				em_data.map_layers.layers[i]->draw_callback(em_data.map_layers.layers[i], em_data.map_layers.layers[i]->user_layer_ptr);
		}
	}

	PERFINFO_AUTO_STOP();
#endif
}

/******
* This is the main editor manager once per frame function.
******/
bool emEditorMain(void)
{
#ifndef NO_EDITORS
	int i;
	bool in_process = false;

	PERFINFO_AUTO_START_FUNC();

	em_data.timestamp = _time32(NULL);

	// close all docs when user quits
	if (em_data.initialized && utilitiesLibShouldQuit() && !em_data.close_state.quitting)
	{
		// Stop quit temporarily.  The close all will restart it if allowed.
		utilitiesLibSetShouldQuit(false);

		// If not already in close process, start it
		// This prevents starting a second one if currently prompting user
		if (!em_data.close_state.should_quit)
		{
			em_data.close_state.should_quit = true;
			em_data.close_state.close_mode = EMClose_AllDocs;
			if (em_data.close_state.confirm_window || ui_IsModalShowing())
			{
				em_data.close_state.should_quit = false;
				emStatusPrintf("Cannot quit with a modal window open.");
			}
		}
	}

	// Each tick where a close is in progress and there is no confirm window
	// should continue the close
	if ((em_data.close_state.close_mode != EMClose_None) && !em_data.close_state.confirm_window)
		emContinueClosing();

	// Each tick where a save is in progress should continue the save
	if (eaSize(&em_data.save_state.entries) > 0)
		emContinueSaving();

	if (!eaSize(&em_data.open_docs))
		emDocUIOncePerFrame(NULL);
	for (i = 0; i < eaSize(&em_data.open_docs); ++i)
		emDocUIOncePerFrame(em_data.open_docs[i]);

	// do not continue if not in editor mode
	if (!em_data.editor_mode)
	{
		PERFINFO_AUTO_STOP();
		return utilitiesLibShouldQuit();
	}

	// show doc loading progress bar if current document is still processing
	if (em_data.current_doc && em_data.current_doc->process && em_data.current_doc->process->active)
	{
		if (em_data.current_doc->process->progress == 1)
			em_data.current_doc->process->active = false;
		else
			in_process = true;

		emDialogProgressRefresh(em_data.current_doc);
	}

	em_data.last_doc = em_data.current_doc;

	if (em_data.current_doc && em_data.current_doc->editor->draw_func)
		em_data.current_doc->editor->draw_func(em_data.current_doc);

	/* Now checked by a called to emEditorHidingWorld()
	if (!em_data.current_doc || (em_data.current_doc && em_data.current_doc->editor->hide_world))
		*draw_world = 0;*/

	// TODO: possibly uncomment this for picker refreshing
	/*
	if (em_data.files_changed)
	{
		for (i = 0; i < eaSize(&em_data.browsers); ++i)
		{
			if (emIsBrowserOk(em_data.browsers[i]))
				emRefreshBrowserFileStatus(em_data.browsers[i]);
		}
		em_data.files_changed = 0;
	}*/

	emUIOncePerFrame();

	PERFINFO_AUTO_STOP();
#endif
	return utilitiesLibShouldQuit();
}

/******
* This returns if the editor wants to hide the world.  Used to be checked in emEditorMain but it now
*  needs to be determined before we run the UI.
******/
bool emEditorHidingWorld(void)
{
	return em_data.editor_mode && (!em_data.current_doc || (em_data.current_doc && em_data.current_doc->editor->hide_world));
}


#include "EditorManager_h_ast.c"
#include "EditorManagerPrivate_h_ast.c"
