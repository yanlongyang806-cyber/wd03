/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef __EDITORMANAGER_H__
#define __EDITORMANAGER_H__
GCC_SYSTEM

#include "textparser.h"
#include "GfxCamera.h"
#include "inputKeyBind.h"
#include "UILib.h"
#include "EditLibUndo.h"
#include "WorldLibEnums.h"
#include "resourceManager.h"

/*******************
* MACROS
*******************/
// for easy EnC support, call this macro once per frame on your favorite function
typedef void (*AnyFunc)(void);
#ifdef NO_EDITORS
#define CHECK_EM_FUNC(unused)
#else
#define CHECK_EM_FUNC(funcname)                 \
{                             \
	static AnyFunc old_##funcname = NULL;         \
	if (old_##funcname != (AnyFunc)funcname) {        \
	if (old_##funcname) {               \
	emReplaceCodeFuncs((AnyFunc)old_##funcname, (AnyFunc)funcname); \
	}                         \
	old_##funcname = (AnyFunc)funcname;         \
	}                           \
}
#endif


/*******************
* DEFINITIONS
*******************/
// forward declarations
typedef struct EMPicker EMPicker;
typedef struct EMEditor EMEditor;
typedef struct EMEditorDoc EMEditorDoc;
typedef struct EMEditorSubDoc EMEditorSubDoc;
typedef struct EMFile EMFile;
typedef struct EMFileAssoc EMFileAssoc;
typedef struct EMMapLayerType EMMapLayerType;
typedef struct EMPanel EMPanel;
typedef struct EMToolbar EMToolbar;
typedef struct EMMenu EMMenu;
typedef struct RdrSurface RdrSurface;
typedef struct GfxCameraController GfxCameraController;
typedef void EMDocState;  // state of the document's def (callbacks will return a void *)
typedef struct WorkerThread WorkerThread;
typedef struct EMSearchResult EMSearchResult;
typedef struct EMPanel EMPanel;
typedef struct EMPickerSelection EMPickerSelection;
typedef struct EMOptionsTab EMOptionsTab;
typedef struct EMInfoWinText EMInfoWinText;
typedef struct EMResourceStatus EMResourceStatus;
typedef struct ResourceSearchResult ResourceSearchResult;
typedef struct EMRegisteredType EMRegisteredType;
typedef struct ResourceGroup ResourceGroup;
#ifndef NO_EDITORS

typedef enum EMTaskStatus
{
	EM_TASK_FAILED,
	EM_TASK_SUCCEEDED,	
	EM_TASK_INPROGRESS,
} EMTaskStatus;

typedef enum EMResourceState
{
	EMRES_STATE_NONE,
	EMRES_STATE_DELETING,
	EMRES_STATE_DELETE_FAILED,
	EMRES_STATE_DELETE_SUCCEEDED,
	EMRES_STATE_LOCKING,
	EMRES_STATE_LOCKING_FOR_DELETE,
	EMRES_STATE_LOCKING_FOR_SAVE,
	EMRES_STATE_LOCK_FAILED,
	EMRES_STATE_LOCK_SUCCEEDED,
	EMRES_STATE_OPENING,
	EMRES_STATE_SAVING,
	EMRES_STATE_SAVE_FAILED,
	EMRES_STATE_SAVE_SUCCEEDED,
	EMRES_STATE_PARENT_CHANGE,
} EMResourceState;

typedef enum EMPromptType
{
	EMPROMPT_CHECKOUT,
	EMPROMPT_REVERT_CONTINUE,
	EMPROMPT_CHECKOUT_REVERT,
	EMPROMPT_SAVE_OVERWRITE_CANCEL,
	EMPROMPT_SAVE_NEW_RENAME_CANCEL,
	EMPROMPT_SAVE_NEW_RENAME_OVERWRITE_CANCEL,
} EMPromptType;

// picker callback definitions
typedef void (*EMPickerFunc)(EMPicker *picker);
typedef bool (*EMPickerTreeNodeSelectedFunc)(EMPicker *picker, EMPickerSelection *selection);
typedef Color (*EMPickerTreeNodeColorFunc)(EMPicker *picker, void *selected_data, ParseTable *parse_info, bool is_selected);
typedef void (*EMPickerTreeNodeTexFunc)(EMPicker *picker, void *selected_data, ParseTable *parse_info, BasicTexture** out_texture, Color* out_mod_color);
typedef bool (*EMPickerFilterCheck)(void *node_data, ParseTable *parse_table);

// editor/doc callback definitions
typedef void (*EMEditorFunc)(EMEditor *editor);
typedef void (*EMEditorDocCustomNewFunc)(void);
typedef EMEditorDoc *(*EMEditorDocNewFunc)(const char *type, void *);
typedef EMEditorDoc *(*EMEditorDocLoadFunc)(const char *name, const char *type);
typedef void (*EMEditorDocFunc)(EMEditorDoc *doc);
typedef void (*EMEditorSubDocFunc)(EMEditorDoc *doc, EMEditorSubDoc *sub_doc);
typedef bool (*EMEditorDocCheckFunc)(EMEditorDoc *doc);
typedef bool (*EMEditorCloseDocCheckFunc)(EMEditorDoc *doc, bool quitting);
typedef bool (*EMEditorSubDocCheckFunc)(EMEditorDoc *doc, EMEditorSubDoc *sub_doc);
typedef EMTaskStatus (*EMEditorDocStatusFunc)(EMEditorDoc *doc);
typedef EMTaskStatus (*EMEditorSubDocStatusFunc)(EMEditorDoc *doc, EMEditorSubDoc *sub_doc);
typedef void (*EMEditorDocGhostDrawFunc)(EMEditorDoc *doc);
typedef void (*EMEditorDocObjectDroppedFunc)(EMEditorDoc *doc, const char *name, const char *type);
typedef EMDocState* (*EMEditorDocSaveStateFunc)(EMEditorDoc *doc);
typedef void (*EMEditorDocRestoreStateFunc)(EMEditorDoc *doc, EMDocState *state);
typedef void (*EMEditorDocFreeStateFunc)(EMDocState *state);
typedef void (*EMEditorCopyFunc)(EMEditorDoc *doc, bool cut);
typedef void (*EMEditorPasteFunc)(EMEditorDoc *doc, ParseTable *pti, const char *custom_type, void *structptr);
typedef void (*EMStructFunc)(void *data);

// misc callback definitions
typedef bool (*EMPreviewFunc)(const char *name, const char *type);
typedef bool (*EMSearchFunc)(EMSearchResult *result, const char *search_string);
typedef void (*EMSearchCloseFunc)(EMSearchResult *result, void *data);
typedef float (*EMProgressFunc)(void *data);
typedef void (*EMMapLayerFunc)(EMMapLayerType *layer_type, void *user_layer_ptr);
typedef void (*EMMapLayerVisFunc)(EMMapLayerType *layer_type, void *user_layer_ptr, bool visible);
typedef void (*EMSaveStatusFunc)(EMTaskStatus status, EMEditorDoc *doc, EMEditorSubDoc *subdoc);
typedef bool (*EMResourceStateFunc)(EMEditor *editor, const char *name, void *state_data, EMResourceState state, void *callback_data, bool success);
typedef bool (*EMShouldRevertFunc)(EMEditorDoc *pDoc);

// enums
/******
* EDITOR TYPES:
*   EM_TYPE_SINGLEDOC:
*		-only uses windows set on document, both public and private
*	EM_TYPE_MULTIDOC:
*		-only uses primary window on document and editor's shared windows
*	EM_TYPE_GLOBAL:
*		-special-case editors (namely, environment editors) that only have one document
*		 at a time
******/
typedef enum EMEditorType
{
	EM_TYPE_SINGLEDOC,		// editors which have one doc open at a time, but can have multiple docs loaded
	EM_TYPE_MULTIDOC,		// editors which can have multiple docs open at once that share a set of UI elements
	EM_TYPE_GLOBAL,			// editors which should have only one doc open, ever
} EMEditorType;

// struct definitions
typedef struct EMPickerDisplayType
{
	ParseTable *parse_info;
	char *display_name_parse_field;
	char *display_notes_parse_field;

	// this callback should fill in doc_name and doc_type for file opening operations; otherwise, this can
	// just return true when used in conjunction with a picker callback; NULL signifies that type cannot
	// be opened
	EMPickerTreeNodeSelectedFunc selected_func;

	Color color; // Only used if color_func is NULL
	Color selected_color; // Only used if color_func is NULL
	EMPickerTreeNodeColorFunc color_func;
	EMPickerTreeNodeTexFunc tex_func;
	EMPickerFunc tex_free_func;
	UITreeDisplayFunc draw_func; // if non-NULL use this instead of the default tree drawing function
	int is_leaf;
} EMPickerDisplayType;

#endif
AUTO_STRUCT;
typedef struct EMPickerFilter
{
	char *display_text;

#ifndef NO_EDITORS
	EMPickerFilterCheck checkF;	NO_AST
#endif
} EMPickerFilter;
#ifndef NO_EDITORS

typedef struct EMPickerSelection
{
	char doc_name[256];
	char doc_type[128];
	ParseTable *table;
	void *data;							// data taken directly from the picker's tree model
	void *private_ref;					// INTERNAL USE ONLY
} EMPickerSelection;

#define EMPickerFunctions \
	EMPickerFunc init_func;		/* // initialization; set parameters indicated below*/\
	EMPickerFunc enter_func;	/* // called when opened; should load (or refresh) tree data*/\
	EMPickerFunc leave_func;	/* // called when closed*/\

struct EMPickerFunctionsArray { EMPickerFunctions };

typedef struct EMPicker
{
	// need to be filled upon registration
	char picker_name[256];				// picker display name
	char default_type[128];				// the type created when the new button is pressed
	union {
		struct {
            EMPickerFunctions
		};
		AnyFunc functions_array[sizeof(struct EMPickerFunctionsArray)/sizeof(AnyFunc)];
	};

	// flags
	U32 allow_outsource : 1;			// set this bit to allow this editor in the standalone outsource Editor Manager
	U32 requires_server_data : 1;		// this picker requires server data
	U32 managed : 1;					// Set to true if this is a managed picker
	U32 refresh_queued : 1;				// INTERNAL DATA
	U32 valid : 2;						// INTERNAL DATA
	U32 initialized : 1;				// INTERNAL DATA

	// Information set for managed pickers
	char *dict_name;					// Provided as a name to avoid order of startup issues
	ResourceGroup *res_group;

	// need to be filled upon initialization
	EMPickerDisplayType **display_types;
	ParseTable *display_parse_info_root;
	void *display_data_root;
	EMPickerFilter **filters;			// filters, one of which can be set in a dropdown when searching for data
	EMPickerFilterCheck filter_func;	// if set, this filtering function will always be called to filter out nodes
	void* userData;                     // Optional data.

	// need to be filled upon node selection
	EMPickerSelection **selections;

	// internal data
	UITree *tree_ui_control;
	UIList *list_ui_control;
	UIList *list_no_preview_ui_control;
	void **list_model;
} EMPicker;

typedef struct EMEditorDictionaryHandler
{
	const char *dict_name; // Pooled
	EMResourceStateFunc auto_open_callback;
	EMResourceStateFunc auto_lock_callback;
	EMResourceStateFunc auto_save_callback;
	EMResourceStateFunc auto_delete_callback;
	void *auto_callback_userdata;
} EMEditorDictionaryHandler;

#define EMEditorFunctions \
\
			/* initialization*/\
			EMProgressFunc can_init_func;\
			EMEditorFunc init_func;\
\
			/* custom new function can be used if extra information is needed from user before*/\
			/* continuing with new doc workflow*/\
			EMEditorDocCustomNewFunc custom_new_func;\
			EMEditorDocNewFunc new_func;\
\
			/* called on open document flows*/\
			EMEditorDocLoadFunc load_func;\
\
			/* enter/exit doc callbacks*/\
			EMEditorFunc enter_editor_func;\
			EMEditorFunc exit_func;\
\
			/* place/drop callback*/\
			EMEditorDocObjectDroppedFunc object_dropped_func;\
\
			/* called every frame when this editor has a doc visible*/\
			EMEditorDocFunc draw_func;\
\
			/* focus functions*/\
			EMEditorDocFunc got_focus_func;\
			EMEditorDocFunc lost_focus_func;\
\
			/* called when a doc has changed, if NULL, will attempt to close and reopen the doc*/\
			EMEditorDocFunc reload_func;\
			EMEditorSubDocFunc sub_reload_func;\
\
			/* used to add drawable items to the draw list*/\
			EMEditorDocGhostDrawFunc ghost_draw_func;\
\
			/* cut/copy/paste functions*/\
			EMEditorCopyFunc copy_func;\
			EMEditorPasteFunc paste_func;\
\
			/* various save functions*/\
			EMEditorDocStatusFunc autosave_func;\
			EMEditorDocStatusFunc custom_save_func;			/* this function will be called on save instead of going through normal validations*/\
			EMEditorDocStatusFunc save_func;\
			EMEditorDocStatusFunc save_as_func;				/* function is expected to ask for new name*/\
			EMEditorSubDocStatusFunc sub_save_func;		/* Saves a sub-document*/\
\
			/* editor manager handles asking if the user wants to save.*/\
			EMEditorCloseDocCheckFunc close_check_func;	/* return false from this function if you want to cancel a close.*/\
			EMEditorDocFunc close_func;					/* this function is just for cleanup*/\
			EMEditorSubDocFunc sub_close_func;			/* Closes a sub-document*/\
\
			/* usage search functions*/\
			EMSearchFunc usage_search_func;\
\
			/* determines if the document should be reverted after a resource change is received from the server */\
			EMShouldRevertFunc should_revert_func;\

struct EMEditorFunctionsArray { EMEditorFunctions };

typedef struct EMEditor
{
	// INTERNAL DATA - DO NOT TOUCH THESE UNLESS YOU ABSOLUTELY KNOW WHAT YOU'RE DOING
	EMRegisteredType **registered_types;// all registered types; used to generate "new" submenu
	GfxCameraController *camera;		// camera controller
	EMEditorDoc *last_doc;				// used for single-doc editors to remember the last focused doc
	StashTable menu_items;				// registered menu items are stored here
	UIMenu **ui_menus;					// menus to display when the editor is open
	StashTable menus;					// registered menus are stored here
	KeyBindProfile *keybinds;			// keybinds that are in use when editor is open
	EMEditorDoc **open_docs;			// all documents being edited by the editor
	StashTable avail_entries;			// hashed table of info window entries
	int *toolbar_order;					// each element must be in range of [-1, highest idx of toolbars]; -1 corresponds to global toolbar
	F32 scale;							// the scale for this editor's UI elements
	U32 inited : 1;						// stores whether the editor's initialization has been called or not
	U32 valid : 2;						// stores whether the editor has been validated by the EM

	// POPULATED DATA - SET ANY OR ALL OF THE FOLLOWING FIELDS/DATA BEFORE REGISTERING YOUR EDITOR
	// flags
	U32 primary_editor : 1;				// flag indicating this should be the first editor opened; the world editor is currently set to be the primary editor
	U32 always_open : 1;				// flag indicating whether this editor should always have an open doc
	U32 edit_shared_memory : 1;			// flag indicating whether to call sharedMemoryEnableEditorMode when a document is loaded
	U32 allow_multiple_docs : 1;		// indicates whether to restrict an editor to have only one document open
	U32 allow_external_docs : 1;		// indicates whether docs from other editors will be shown or hidden (automatically true if the editor type is EM_TYPE_MULTIDOC)
	U32 show_without_focus : 1;			// indicates whether it is legal to show this doc when the editor doesn't have focus (automatically true the editor type is EM_TYPE_MULTIDOC)
	U32 allow_save : 1;					// for editors that do not automatically save after every operation
	U32 disable_single_doc_menus : 1;	// will not render save, save as, and close menu items
	U32 hide_file_toolbar : 1;			// can be used to hide the file toolbar if it is showing up
	U32 hide_world : 1;					// setting this bit disables the rendering of the world in the viewport
	U32 hide_info_window : 1;			// setting this bit hides the info window from this editor
	U32 force_editor_cam : 1;			// setting this ensures that the editor's camera is initialized and used instead of the world camera; this is really only necessary when hide_world is false
	U32 use_em_cam_keybinds : 1;		// this bit is ignored if hide_world is false; otherwise, setting this bit causes world camera keybinds to operate on the editor's camera
	U32 allow_outsource : 1;			// set this bit to allow this editor in the standalone outsource Editor Manager
	U32 requires_server_data : 1;		// this editor requires server data
	U32 disable_auto_checkout : 1;		// this editor will not attempt to checkout the file on first change
	U32 do_not_reload : 1;				// editor wont attempt a refresh on file change (only set this if you do it manually)
	U32 force_reload : 1;				// call reload functions even if the timestamps have not changed.
	U32 reload_prompt : 1;				// prompt a reload if a base file changes underneath an open document; only applies if do_not_refresh is 0
	U32 enable_clipboard : 1;			// display clipboard panel for this editor
	WorldRegionType region_type;		// sets the region type used if hide_world is set

	// UI elements
	UIWindow **shared_windows;			// all windows common to the editor and NOT doc-specific
	EMToolbar **toolbars;				// toolbars
	EMOptionsTab **options_tabs;		// option tabs for the preferences window
	int sort_order;						// sort order of the windows of editors in multi editor workspaces

	// misc
	char editor_name[128];				// unique name for the editor; also used as a display name
	EMEditorType type;					// document management type
	const char *default_type;			// type that is created by default when new button is clicked
	EMPicker **pickers;					// all pickers associated with the editor
	GfxCameraControllerFunc camera_func;// use this to override the default camera function used for editors that don't show the world
	char *keybinds_name;				// ref name of keybinds to push onto the stack when this editor is open
	char default_workspace[1024];		// name of the workspace where the editor should belong; defaults to the editor name
	int keybind_version;				// increment this in code whenever you need user keybinds to be deleted and updated with new defaults
	U32 autosave_interval;				// minutes between autosaves, set to 0 to disable autosave
	EMResourceStatus **resource_status;	// resource status storage

	// callback data
	void *can_init_data;				// passed to can_init_func when determining whether to begin editor initiation

	// Auto dictionary functions and data
	bool dict_changed;
	bool message_dict_changed;
	bool index_changed;	
	char **changed_resources;
	const char *dict_name; // Pooled
	EMResourceStateFunc auto_open_callback;
	EMResourceStateFunc auto_lock_callback;
	EMResourceStateFunc auto_save_callback;
	EMResourceStateFunc auto_delete_callback;
	void *auto_callback_userdata;
	EMEditorDictionaryHandler **ppExtraDictHandlers;

	// callbacks
	union {
        struct {
            EMEditorFunctions
        };
		AnyFunc functions_array[sizeof(struct EMEditorFunctionsArray)/sizeof(AnyFunc)];
	};

} EMEditor;

typedef struct EMWorkerProcess
{
	bool active;
	F32 progress;
	WorkerThread *worker_thread;
} EMWorkerProcess;

typedef struct EMEditorDoc
{
	// POPULATED DATA - FILL THESE FIELDS IN WHEN CREATING YOUR DOC
	char doc_display_name[MAX_PATH];
	char doc_name[MAX_PATH];		// file name without extension for normal files, otherwise custom definition
	char orig_doc_name[MAX_PATH];	// the doc name at the time the document was opened.  Empty if doc is new.
	char doc_type[MAX_PATH];		// file extension for normal files, otherwise custom definition

	EMEditorSubDoc **sub_docs;      // optional separately managed sub-documents within this document
	EditUndoStack *edit_undo_stack;	// undo stack; use EditLibUndo API's to create this on doc creation functions
	bool saved;						// set this bit to indicate the document has no unsaved changes
	U32 name_changed : 1;           // set this bit if you change the display name to refresh the tab

	// UI
	EMPanel **em_panels;			// panels to display when the document is active
	UIWindow **ui_windows;          // Named windows to show up in the Window menu
	UIWindow **ui_windows_private;  // Windows which should not show up in the Windows menu (and can have their own close callbacks)
	UIWindow *primary_ui_window;    // if this window is closed the doc gets closed

	// Smart save logic
	bool smart_save_rename;
	bool smart_save_overwrite;

	// INTERNAL DATA - DO NOT TOUCH THESE UNLESS YOU ABSOLUTELY KNOW WHAT YOU'RE DOING
	bool prev_saved : 1;			// determines whether to render the '*' by the document tab or not
	__time32_t last_autosave_time;  // used to track how much time has elapsed since the last autosave
	UITab *ui_tab;					// document tab created automatically by Editor Manager
	EMWorkerProcess *process;		// if non-null and active, show only the progress window
	EMEditor *editor;				// the editor used to modify this document
	EMFileAssoc **files;			// files on which the document is dependent; use emDocAssocFile to populate this
	EMFileAssoc **all_files;		// constructed from "files" and from sub-document "files" for use by the file list
} EMEditorDoc;

typedef struct EMEditorSubDoc
{
	char doc_display_name[MAX_PATH];
	char doc_name[MAX_PATH];		// file name without extension for normal files, otherwise custom definition
	char doc_type[MAX_PATH];		// file extension for normal files, otherwise custom definition

	// UI
	bool saved;
	__time32_t last_autosave_time;	// for internal purposes only

	// internal data
	EMFileAssoc **files;			// files on which the document is dependent; use emDocAssocFile to populate this
} EMEditorSubDoc;

typedef struct EMFileAssoc
{
	EMFile *file;
	EMEditorDoc *doc;
	EMEditorSubDoc *sub_doc;
	__time32_t last_timestamp;
} EMFileAssoc;

typedef struct EMFile
{
	char filename[MAX_PATH];

	EMFileAssoc **docs;

	// internal use only
	__time32_t file_timestamp;
	U32 read_only : 1;
	U32 checked_out : 1;
	U32 failed_checkout : 1;
} EMFile;

/********************
* UTIL
********************/
typedef void (*emFunc)(void *data);
typedef void (*emFunc2)(void *data1, void *data2);

#define emQueueFunctionCall(func, data) emQueueFunctionCallEx(func, data, 0)
void emQueueFunctionCallEx(emFunc func, void *data, U32 frame_delay);
#define emQueueFunctionCall2(func, data1, data2) emQueueFunctionCall2Ex(func, data1, data2, 0)
void emQueueFunctionCall2Ex(emFunc2 func, void *data1, void *data2, U32 frame_delay);
void emQueueFunctionCallCond(emFunc func, void *data, bool (*check_func)(void *data), void *check_data, int timeout);
void emQueueFunctionCallStatus(void (*func)(EMTaskStatus status, void *data), void *data, EMTaskStatus (*check_func)(void *data), void *check_data, int timeout);
void emQueueFunctionCallStatus2(void (*func)(EMTaskStatus status, void *data, void *data2), void *data, void *data2, EMTaskStatus (*check_func)(void *data, void *data2), void *check_data, void *check_data2, int timeout);
void emAddMapChangeCallback(void (*func)(void *data, bool is_reset), void *data);
void emAddEditorEntryCallback(void (*func)(void *data), void *data);
void emReplaceCodeFuncs(AnyFunc oldfunc, AnyFunc newfunc);
void emGetCanvasSize(SA_PRE_NN_FREE SA_POST_NN_VALID F32 *x, SA_PRE_NN_FREE SA_POST_NN_VALID F32 *y, SA_PRE_NN_FREE SA_POST_NN_VALID F32 *w, SA_PRE_NN_FREE SA_POST_NN_VALID F32 *h);
void emEditorDocumentation(void);
F32 emGetEditorScale(SA_PARAM_NN_VALID EMEditor *editor);
F32 emGetSidebarScale(void);
void emSetFreecam(bool freecam);
void emQueuePrompt(EMPromptType prompt_type, EMEditorDoc *doc, EMEditorSubDoc *sub_doc, DictionaryHandle dict, const char *res_name);
GfxCameraController *emGetWorldCamera(void);

/********************
* EDITOR REGISTRATION
********************/
void emRegisterEditor(SA_PARAM_NN_VALID EMEditor *editor);
void emRegisterFileTypeEx(SA_PARAM_NN_STR const char *type, SA_PARAM_OP_STR const char *display_name, SA_PARAM_OP_STR const char *editor_name, SA_PARAM_OP_VALID EMPreviewFunc preview_func);
#define emRegisterFileType(type, display_name, editor_name) emRegisterFileTypeEx(type, display_name, editor_name, NULL)


/********************
* DOCUMENT MANAGEMENT
********************/
SA_RET_OP_VALID EMEditor *emGetEditorByName(SA_PARAM_OP_STR const char *name);
SA_RET_OP_VALID EMEditor *emGetEditorForType(SA_PARAM_OP_STR const char *type);
EMEditorDoc* emOpenFile(SA_PARAM_NN_STR  const char *filename);
EMEditorDoc* emOpenFileEx(SA_PARAM_OP_STR const char *name, SA_PARAM_NN_STR const char *type);
void emWorkspaceOpen(SA_PARAM_NN_STR const char *workspace_name);
void emNewDoc(SA_PARAM_NN_STR const char *type, void *data);
void emReopenDoc(EMEditorDoc *doc);
void emReloadDoc(EMEditorDoc *doc);
void emReloadSubDoc(EMEditorDoc *doc, EMEditorSubDoc *subdoc);
void emRefreshDocumentUI(void);
bool emGetDocSaved(SA_PARAM_NN_VALID EMEditorDoc *doc);
void emSetSubDocUnsaved(SA_PARAM_NN_VALID EMEditorDoc *doc, SA_PARAM_NN_VALID EMEditorSubDoc *subdoc);
void emSetDocUnsaved(SA_PARAM_NN_VALID EMEditorDoc *doc, bool include_subdocs);
EMTaskStatus emSaveDoc(EMEditorDoc *doc);
EMTaskStatus emSaveDocEx(EMEditorDoc *doc, bool doAsyncSave, EMSaveStatusFunc saveFunc);
EMTaskStatus emSaveDocAs(EMEditorDoc *doc);
EMTaskStatus emSaveDocAsEx(EMEditorDoc *doc, bool doAsyncSave, EMSaveStatusFunc saveFunc);
EMTaskStatus emSaveSubDoc(EMEditorDoc *doc, EMEditorSubDoc *sub_doc);
EMTaskStatus emSaveSubDocEx(EMEditorDoc *doc, EMEditorSubDoc *sub_doc, bool doAsyncSave, EMSaveStatusFunc saveFunc);
EMTaskStatus emCloseDoc(EMEditorDoc *doc);
EMTaskStatus emForceCloseDoc(EMEditorDoc *doc);
EMTaskStatus emCloseAllDocs(void);
EMTaskStatus emCloseSubDocs(EMEditorDoc *doc, EMEditorSubDoc ***sub_docs);
EMTaskStatus emCloseSubDoc(EMEditorDoc *doc, EMEditorSubDoc *sub_doc);
EMTaskStatus emForceCloseSubDoc(EMEditorDoc *doc, EMEditorSubDoc *sub_doc);
EMTaskStatus emCloseAllSubDocs(EMEditorDoc *doc);
void emMoveDocToWorkspace(SA_PARAM_NN_VALID EMEditorDoc *doc, SA_PARAM_NN_STR const char *workspace_name);
SA_RET_OP_VALID EMEditorDoc *emGetActiveEditorDoc(void);
SA_RET_OP_VALID EMEditor *emGetActiveEditor(void);
EMEditorDoc *emGetEditorDoc(const char *name, const char *type);
bool emIsDocOpen(EMEditorDoc *doc);
void emMakeUniqueDocName(EMEditorDoc *doc, const char *base_name, const char *doc_type, const char *dict_name);
void emHistorySelectNextItem();
void emHistorySelectPreviousItem();


/********************
* FILE MANAGEMENT
*
* Each EMEditorDoc has an EArray of associations with EMFiles.  When a new doc is created from the
* editor, there should be nothing in this EArray.  When the new doc is saved, a file association
* should be created with the saved document.
********************/
SA_RET_OP_VALID EMFile *emGetFile(SA_PARAM_NN_VALID const char *filename, bool create);

void emDocAssocFile(SA_PARAM_NN_VALID EMEditorDoc *doc, SA_PARAM_NN_STR const char *filename);
void emDocRemoveFile(SA_PARAM_NN_VALID EMEditorDoc *doc, SA_PARAM_NN_STR const char *filename);
void emDocRemoveAllFiles(SA_PARAM_NN_VALID EMEditorDoc *doc, bool include_sub_docs);

void emSubDocAssocFile(SA_PARAM_NN_VALID EMEditorDoc *doc, SA_PARAM_NN_VALID EMEditorSubDoc *sub_doc, SA_PARAM_NN_STR const char *filename);
void emSubDocRemoveFile(SA_PARAM_NN_VALID EMEditorDoc *doc, SA_PARAM_NN_VALID EMEditorSubDoc *sub_doc, SA_PARAM_NN_STR const char *filename);
void emSubDocRemoveAllFiles(SA_PARAM_NN_VALID EMEditorDoc *doc, SA_PARAM_NN_VALID EMEditorSubDoc *sub_doc);

void emDocGetFiles(SA_PARAM_NN_VALID EMEditorDoc *doc, SA_PARAM_NN_VALID EMFile ***files, bool include_sub_docs);
void emSubDocGetFiles(SA_PARAM_NN_VALID EMEditorSubDoc *subdoc, SA_PARAM_NN_VALID EMFile ***files);
void emDocGetChangedFiles(SA_PARAM_NN_VALID EMEditorDoc *doc, SA_PARAM_NN_VALID EMFile ***changed_files, bool include_sub_docs);
void emDocUpdateFile(SA_PARAM_NN_VALID EMEditorDoc *doc, SA_PARAM_NN_VALID EMFile *file);


/********************
* MAP LAYERS
*
* Map layers are layers of content that are associated with a particular map.  They appear in
* the generic map layer panel and a variety of operations can be performed on them.  Map
* layers generally consist of the world library layers and encounter layers.
* TODO: tidy up this API and functionality; determine overlap in functionality with other
* editors and see if it is redundant
********************/
SA_RET_NN_VALID EMMapLayerType *emMapLayerCreate(SA_PARAM_NN_STR const char *filename, SA_PARAM_NN_STR const char *layer_name, SA_PARAM_NN_STR const char *layer_type, void *user_layer_ptr, SA_PARAM_OP_VALID EMMapLayerVisFunc toggle_vis_func, SA_PARAM_OP_VALID EMMapLayerFunc draw_func);
void emMapLayerDestroy(SA_PARAM_NN_VALID EMMapLayerType *map_layer);
void emMapLayerAdd(SA_PARAM_NN_VALID EMMapLayerType *map_layer);
void emMapLayerRemove(SA_PARAM_NN_VALID EMMapLayerType *map_layer);
void emMapLayerMenuItemAdd(SA_PARAM_NN_VALID EMMapLayerType *map_layer, SA_PARAM_NN_STR const char *item_name, SA_PARAM_NN_VALID EMMapLayerFunc item_callback);
bool emMapLayerGetVisible(SA_PARAM_NN_VALID EMMapLayerType *map_layer);
void emMapLayerSetVisible(SA_PARAM_NN_VALID EMMapLayerType *map_layer, bool visible);
SA_RET_NN_STR const char *emMapLayerGetFilename(SA_PARAM_NN_VALID EMMapLayerType *map_layer);
void *emMapLayerGetUserPtr(SA_PARAM_NN_VALID EMMapLayerType *map_layer);

/********************
* UNDO/REDO
********************/
void emUndo(SA_PARAM_NN_VALID EMEditorDoc *doc);
void emRedo(SA_PARAM_NN_VALID EMEditorDoc *doc);


/********************
* CUT/COPY/PASTE
********************/
void emAddToClipboardParsed(SA_PARAM_NN_VALID ParseTable *pti, void *structptr);
void emAddToClipboardCustom(SA_PARAM_NN_STR const char *type, SA_PARAM_OP_VALID EMStructFunc freeFunc, void *data);


/********************
* MENU/MENU ITEMS
********************/
// definitions
typedef bool (*EMMenuItemCheckFunc)(UserData data);
typedef struct EMMenuItemDef
{
	char *indexed_text;
	char *item_name;
	EMMenuItemCheckFunc check_func;
	UserData check_func_data;
	char *command_str;
} EMMenuItemDef;

// menu item management
void emMenuItemCreate(SA_PARAM_OP_VALID EMEditor *editor, SA_PARAM_NN_STR const char *indexed_text, SA_PARAM_OP_STR const char *item_name,
					  SA_PARAM_OP_VALID EMMenuItemCheckFunc check_func, SA_PARAM_OP_VALID UserData check_func_data, SA_PARAM_OP_STR const char *command_str);
void emMenuItemCreateFromTable(SA_PARAM_OP_VALID EMEditor *editor, const EMMenuItemDef *menu_item_table, size_t num_entries);
void emMenuItemDestroy(SA_PARAM_OP_VALID EMEditor *editor, SA_PARAM_NN_STR const char *indexed_text);
SA_RET_OP_VALID UIMenuItem *emMenuItemGet(SA_PARAM_OP_VALID EMEditor *editor, SA_PARAM_NN_STR const char *indexed_text);
void emMenuItemSet(SA_PARAM_OP_VALID EMEditor *editor, SA_PARAM_NN_STR const char *indexed_text, SA_PARAM_NN_VALID UIMenuItem *override_item);

// menu management
SA_RET_NN_VALID UIMenu *emMenuCreate(SA_PARAM_OP_VALID EMEditor *editor, SA_PARAM_NN_STR const char *menu_name, ...);
void emMenuAppendItems(SA_PARAM_OP_VALID EMEditor *editor, SA_PARAM_NN_VALID UIMenu *menu, ...);
void emMenuRegister(SA_PARAM_NN_VALID EMEditor *editor, SA_PARAM_NN_VALID UIMenu *menu);


/********************
* PANELS
********************/
// global panel API's
void emStatusPrintf(FORMAT_STR const char *fmt, ...);
#define emStatusPrintf(fmt, ...) emStatusPrintf(FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

// panel management
SA_RET_NN_VALID EMPanel *emPanelCreate(SA_PARAM_NN_STR const char *tab_name, SA_PARAM_NN_STR const char *panel_name, int expander_height);
void emPanelFree(SA_PARAM_OP_VALID EMPanel *panel);
SA_RET_NN_STR const char *emPanelGetName(SA_PARAM_NN_VALID EMPanel *panel);
void emPanelAddChild(SA_PARAM_NN_VALID EMPanel *panel, SA_PARAM_NN_VALID UIAnyWidget *widget, bool update_height);
void emPanelRemoveChild(SA_PARAM_NN_VALID EMPanel *panel, SA_PARAM_NN_VALID UIAnyWidget *widget, bool update_height);
void emPanelUpdateHeight(SA_PARAM_NN_VALID EMPanel *panel);
void emPanelSetHeight(SA_PARAM_NN_VALID EMPanel *panel, int height);
int emPanelGetHeight(SA_PARAM_NN_VALID EMPanel *panel);
void emPanelSetActive(SA_PARAM_NN_VALID EMPanel *panel, bool active);
void emPanelSetOpened(SA_PARAM_NN_VALID EMPanel *panel, bool opened);
void emPanelFocus(SA_PARAM_NN_VALID EMPanel *panel);
void emPanelSetName(EMPanel *panel, const char* expander_name);
UIWidget *emPanelGetUIContainer(SA_PARAM_NN_VALID EMPanel *panel);
UIExpander *emPanelGetExpander(SA_PARAM_NN_VALID EMPanel *panel);
UIExpanderGroup *emPanelGetExpanderGroup(SA_PARAM_NN_VALID EMPanel *panel);
UIPane *emPanelGetPane(SA_PARAM_NN_VALID EMPanel *panel);
void emPanelSetExpanderGroup(SA_PARAM_NN_VALID EMPanel *panel, SA_PARAM_NN_VALID UIExpanderGroup *expander_group);


/********************
* PICKERS
********************/
// definitions
typedef bool (*EMPickerCallback)(EMPicker *picker, EMPickerSelection **selections, void *data);
typedef void (*EMPickerClosedWindowCallback)(EMPicker *picker, void *data);

// picker management
SA_RET_OP_VALID EMPicker *emPickerGetByName(const char *name);
SA_RET_NN_VALID EMPicker *emTexturePickerCreateForType(SA_PARAM_NN_STR const char *type);
void emPickerRegister(SA_PARAM_NN_VALID EMPicker *picker);
void emPickerManage(SA_PARAM_NN_VALID EMPicker *picker);
void emPickerRefresh(SA_PARAM_NN_VALID EMPicker *picker);
void emPickerRefreshSafe(SA_PARAM_NN_VALID EMPicker *picker);

void emPickerShowEx(SA_PARAM_NN_VALID EMPicker *picker, char *action_name, bool allow_multi_select, EMPickerCallback callback, EMPickerClosedWindowCallback onClosedCallback, void *data);
#define emPickerShow(picker, action_name, allow_multi_select, callback, data) emPickerShowEx(picker, action_name, allow_multi_select, callback, NULL, data)


typedef char* (*EMEasyPickerNameFilter)( const char* path );
typedef void (*EMEasyPickerTexFunc)( SA_PARAM_NN_STR const char* path, SA_PRE_NN_FREE SA_POST_NN_VALID BasicTexture** outTex, SA_PRE_NN_FREE SA_POST_NN_VALID Color* outColor );
typedef Color (*EMEasyPickerColorFunc)( const char* path, bool isSelected );
extern ParseTable parse_EMEasyPickerEntry[];
#define TYPE_parse_EMEasyPickerEntry EMEasyPickerEntry

SA_RET_NN_VALID EMPicker* emEasyPickerCreate( SA_PARAM_NN_STR const char* name, SA_PARAM_NN_STR const char* ext, SA_PARAM_NN_STR const char* dirRoot,
										  SA_PARAM_OP_VALID EMEasyPickerNameFilter nameFilter );
EMPicker* emEasyPickerCreateEx( const char* name, const char** exts, const char** dirRoots,
								SA_PARAM_OP_VALID EMEasyPickerNameFilter nameFilter,
								SA_PARAM_OP_VALID EMEasyPickerTexFunc texFunc );
void emEasyPickerDestroy(SA_PARAM_NN_VALID EMPicker* easyPicker);
void emEasyPickerSetTexFunc( SA_PARAM_NN_VALID EMPicker* picker, EMEasyPickerTexFunc texFunc );
void emEasyPickerSetColorFunc( SA_PARAM_NN_VALID EMPicker* picker, EMEasyPickerColorFunc colorFunc );

/********************
* TOOLBARS
********************/
SA_RET_NN_VALID EMToolbar *emToolbarCreateEx(const char *name_id, int width);
#define emToolbarCreate(width) emToolbarCreateEx(NULL, width)
void emToolbarDestroy(SA_PARAM_NN_VALID EMToolbar *toolbar);
void emToolbarAddChild(SA_PARAM_NN_VALID EMToolbar *toolbar, UIAnyWidget *widget, bool update_width);
void emToolbarRemoveChild(SA_PARAM_NN_VALID EMToolbar *toolbar, UIAnyWidget *widget, bool update_width);
int emToolbarGetHeight(SA_PARAM_NN_VALID EMToolbar *toolbar);
const char* emToolbarGetNameId(SA_PARAM_NN_VALID EMToolbar *toolbar);
void emToolbarSetWidth(SA_PARAM_NN_VALID EMToolbar *toolbar, int width);
void emToolbarSetActive(SA_PARAM_NN_VALID EMToolbar *toolbar, bool active);

typedef enum EMEditorFileToolbarFlags
{
	EM_FILE_TOOLBAR_NEW      = (1 << 0),
	EM_FILE_TOOLBAR_OPEN     = (1 << 1),
	EM_FILE_TOOLBAR_SAVE     = (1 << 2),
	EM_FILE_TOOLBAR_SAVE_ALL = (1 << 3),
} EMEditorFileToolbarFlags;

SA_RET_NN_VALID EMToolbar *emToolbarCreateFileToolbar(U32 flags);
#define emToolbarCreateDefaultFileToolbar()  emToolbarCreateFileToolbar(EM_FILE_TOOLBAR_NEW | EM_FILE_TOOLBAR_OPEN | EM_FILE_TOOLBAR_SAVE)
SA_RET_NN_VALID EMToolbar *emToolbarCreateWindowToolbar(void);


/********************
* OPTIONS
********************/
typedef bool (*EMOptionsLoadFunc)(EMOptionsTab *options_tab, UITab *tab);
typedef void (*EMOptionsTabFunc)(EMOptionsTab *options_tab, void *data);

SA_RET_NN_VALID EMOptionsTab *emOptionsTabCreate(SA_PARAM_NN_STR const char *tab_name, SA_PARAM_OP_VALID EMOptionsLoadFunc load_func, SA_PARAM_OP_VALID EMOptionsTabFunc ok_func, SA_PARAM_OP_VALID EMOptionsTabFunc cancel_func);
void emOptionsTabSetData(SA_PARAM_NN_VALID EMOptionsTab *tab, void *data);

void emOptionsShowEx(const char *tab_name);
void emOptionsShow(void);

/********************
* INFO WINDOW
********************/
typedef void (*EMInfoWinTextFunc)(const char *indexed_name, EMInfoWinText ***text_lines);

void emInfoWinEntryRegister(SA_PARAM_NN_VALID EMEditor *editor, SA_PARAM_NN_STR const char *indexed_name, SA_PARAM_NN_STR const char *display_name, SA_PARAM_NN_VALID EMInfoWinTextFunc text_func);
EMInfoWinText *emInfoWinCreateTextLineWithColor(SA_PARAM_NN_STR const char *text, U32 rgba);
#define emInfoWinCreateTextLine(text) emInfoWinCreateTextLineWithColor(text, 0)

/********************
* ASSET VIEWER INTEGRATION
********************/
bool emPreviewFileEx(const char *name, const char *type);
bool emPreviewFile(const char *filename);


/********************
* SEARCHES
********************/
#define SEARCH_TAB_NAME          "Search Results"

SA_RET_NN_VALID EMSearchResult *emSearchResultCreate(SA_PARAM_OP_STR const char *tab_name, SA_PARAM_OP_STR const char *panel_name, SA_PARAM_OP_STR const char *desc);
void emSearchResultAddResourceResult(SA_PARAM_NN_VALID EMSearchResult *result, ResourceSearchResult *res_result);
void emSearchResultAddRow(SA_PARAM_NN_VALID EMSearchResult *result, SA_PARAM_NN_STR const char *name, SA_PARAM_OP_STR const char *type, SA_PARAM_OP_STR const char *relation);
void emSearchResultAppend(SA_PARAM_NN_VALID EMSearchResult *dest, SA_PARAM_NN_VALID const EMSearchResult *source);
int emSearchResultSize(SA_PARAM_NN_VALID EMSearchResult *result);
void emSearchResultClear(EMSearchResult *result);
void emSearchResultDestroy(EMSearchResult *result);

#define emSearchResultShow(result) emSearchResultShowEx(result, NULL, NULL, false)
void emSearchResultShowEx(SA_PARAM_OP_VALID EMSearchResult *result, SA_PARAM_OP_VALID EMSearchCloseFunc close_func, void *close_data, bool no_copy);
void emSearchUsages(SA_PARAM_OP_STR const char *search_text);


/********************
* STATES
********************/
// These are used by editors to store state on documents
EMResourceState emGetResourceState(SA_PARAM_NN_VALID EMEditor *editor, SA_PARAM_NN_STR const char *name);
void *emGetResourceStateData(SA_PARAM_NN_VALID EMEditor *editor, SA_PARAM_NN_STR const char *name);
void emSetResourceState(SA_PARAM_NN_VALID EMEditor *editor, SA_PARAM_NN_STR const char *name, EMResourceState state);
void emSetResourceStateWithData(SA_PARAM_NN_VALID EMEditor *editor, SA_PARAM_NN_STR const char *name, EMResourceState state, void *data);
bool emHandleSaveResourceState(SA_PARAM_NN_VALID EMEditor *editor, SA_PARAM_NN_STR const char *name, EMTaskStatus *status);

/********************
* Smart Saving and Auto-Handling
********************/

void emAddDictionaryStateChangeHandler(SA_PARAM_NN_VALID EMEditor *editor, const char *dict_name, EMResourceStateFunc open_callback, EMResourceStateFunc lock_callback, EMResourceStateFunc save_callback, EMResourceStateFunc delete_callback, void *callback_userdata);
void emAutoHandleDictionaryStateChange(SA_PARAM_NN_VALID EMEditor *editor, const char *dict_name, bool has_messages,
									   EMResourceStateFunc open_callback, EMResourceStateFunc lock_callback, EMResourceStateFunc save_callback, EMResourceStateFunc delete_callback, void *callback_userdata);
EMTaskStatus emSmartSaveDoc(EMEditorDoc *doc, void *resource, void *orig_resource, bool save_as_new);
bool emDocIsEditable(EMEditorDoc *doc, bool prompt);


/********************
* MISC
********************/
void emEditorHideWorld(SA_PARAM_OP_VALID EMEditor *editor, bool hide_world);


/********************
* MAIN
********************/
// these are generally only called by the GameClient to interface the EditorManager with the game
#endif
bool emIsEditorActive(void);
void emSetActiveCamera(SA_PARAM_NN_VALID GfxCameraController *gameCamera, SA_PARAM_OP_VALID GfxCameraView *gameCameraView);
void emRunQueuedFunctions(void);
void emEditorDrawGhosts(void);
bool emEditorMain(void);
bool emEditorHidingWorld(void);
void emSetDefaultCameraPosPyr(SA_PARAM_NN_VALID GfxCameraController *gamecamera);
#ifndef NO_EDITORS
void emSetEditorMode(bool editor_mode);
void emToggleEditorUI();
void emSetOutsourceMode(bool outsource_mode);
bool emIsFreecamActive(void);


/********************
* EXTERNS
********************/
extern ParseTable parse_EMPickerFilter[];
#define TYPE_parse_EMPickerFilter EMPickerFilter

#endif // NO_EDITORS

#endif //__EDITORMANAGER_H__
