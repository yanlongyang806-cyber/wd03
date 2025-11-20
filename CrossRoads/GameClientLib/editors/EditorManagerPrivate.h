/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef __EDITORMANAGERPRIVATE_H__
#define __EDITORMANAGERPRIVATE_H__
GCC_SYSTEM

#include "EditorManager.h"

#ifndef NO_EDITORS
#include "StashTable.h"
#include "MemLog.h"
#include "EditorManagerUI.h"
#include "inputKeybind.h"

#define EM_STATUS_NUMLINES 10
#define EM_STATUS_FLASHFRAMES 50
#define EM_CUSTOMGROUPS_MAX 4

#define FORALL_DOCWINDOWS(doc, i) for (i = 0; i < eaSize(&doc->ui_windows) + eaSize(&doc->ui_windows_private); ++i)
#define DOCWINDOW(doc, i) ((i < eaSize(&doc->ui_windows))?doc->ui_windows[i]:doc->ui_windows_private[i - eaSize(&doc->ui_windows)])
#define DOCWINDOW_IS_PRIVATE(doc, i) (i >= eaSize(&doc->ui_windows))

typedef struct FolderCache FolderCache;
typedef struct UIMenuBar UIMenuBar;
typedef struct MemLog MemLog;
typedef struct MRUList MRUList;
typedef struct EMSearchResultTab EMSearchResultTab;
typedef struct GfxCameraController GfxCameraController;
typedef struct EMMessageOfTheDay EMMessageOfTheDay;

typedef enum EMCloseMode
{
	EMClose_None,
	EMClose_AllDocs,     // All documents
	EMClose_AllSubDocs,  // All sub-documents, but not the document itself
	EMClose_Doc,         // All sub-documents AND the document
	EMClose_SubDoc       // Just one sub-document
} EMCloseMode;

typedef enum EMSaveMode
{
	EMSave_None,
	EMSave_Doc,         // All sub-documents AND the document
	EMSave_SubDoc       // Just one sub-document
} EMSaveMode;

AUTO_ENUM;
typedef enum EMPreviewTexSize
{
	EMPreviewTex_Small,
	EMPreviewTex_Medium,
	EMPreviewTex_Large       
} EMPreviewTexSize;
extern StaticDefineInt EMPreviewTexSizeEnum[];

typedef struct EMSaveEntry
{
	EMEditorDoc *doc;
	EMEditorSubDoc *sub_doc;
	EMSaveStatusFunc status_func;
	bool save_as;
} EMSaveEntry;

typedef struct EMPanel
{
	char *tab_name;
	bool active;
	UIExpander *ui_expander;

	bool windowed;
	UIWindow *ui_window;
	UIScrollArea *ui_scroll;
	UIPane *ui_pane;

	UIExpanderGroup *ui_expander_group;
	int y_scroll;
} EMPanel;

typedef struct EMPickerListNode
{
	char *name;
	char *notes;
	ParseTable *table;
	void *contents;
} EMPickerListNode;

#endif
AUTO_STRUCT;
typedef struct EMMapLayerType
{
	EMFile *file;     NO_AST

	char *layer_name;
	char *layer_type;

	bool visible;
	char **menu_item_names;

	AST_STOP
#ifndef NO_EDITORS
	void *user_layer_ptr;
	EMMapLayerVisFunc visible_toggled_callback;
	EMMapLayerFunc draw_callback;
	void **menu_funcs;
#endif
} EMMapLayerType;

AUTO_STRUCT;
typedef struct EMRegisteredType
{
	char *type_name;
	char *display_name;
	char *editor_name;

#ifndef NO_EDITORS
	EMPreviewFunc preview_func;		NO_AST
#endif
} EMRegisteredType;
#ifndef NO_EDITORS

typedef struct EMResourceStatus {
	char *name;
	EMResourceState state;
	void *data;
} EMResourceStatus;

typedef struct EMPromptEntry {
	EMPromptType prompt_type;
	EMEditorDoc *doc;
	EMEditorSubDoc *sub_doc;
	DictionaryHandle dict;
	const char *res_name;
	char *custom_message;
	const char *filename;
	int line_number;
} EMPromptEntry;

typedef struct EMEditorHistoryEntry {
	char *pcName;
	char *pcType;
} EMEditorHistoryEntry;

typedef struct EMInternalData
{
	U32 initialized : 1;
	U32 editor_mode : 1;
	U32 files_changed : 1;
	U32 outsource_mode : 1;
	U32 ui_hidden : 1;

	U32 version;
	int freecam_keybind_version;
	int standardcam_keybind_version;

	char *user_name;

	EMEditorDoc *current_doc, *last_doc, *prev_open_doc;
	EMEditorDoc *doc_before_em_exit;
	EMEditor *current_editor, *last_editor;

	EMEditorHistoryEntry **history;
	int history_idx;

	EMEditor **editors;
	EMPicker **pickers;
	EMEditorDoc **open_docs;

	__time32_t timestamp;

	// camera
	GfxCameraController *worldcam;
	KeyBindProfile *standardcam_profile;
	KeyBindProfile *freecam_profile;

	EMEditor *primary_editor;

	StashTable registered_editors;
	StashTable registered_pickers;
	StashTable registered_file_types;
	StashTable menu_items;

	StashTable files;

	MRUList *open_recent;
	FolderCache *local_settings_cache;

	U32 last_crc;

	UISkin *transparent_skin;

	// remembers the last doc focused in each mode; becomes default doc when mode is selected
	EMEditorDoc *last_global_doc;

	EMWorkspace **workspaces;
	EMWorkspace *active_workspace;

	F32 last_game_ui_scale;

	struct 
	{
		UIPane *pane;
		UIPane *toolbar;

		// menus
		UIMenuBar *menu_bar;
		UIMenu *file_menu;
		UIMenu *edit_menu;
		UIMenu *help_menu;
		UIMenu *view_menu;
		UIMenu *tools_menu;
		UIMenu *doc_menu;
		UIMenu *window_menu;
		UIMenu *open_recent_menu;
		UIMenu *toolbars_menu;

		// toolbars
		EMToolbar *global_toolbar;
		EMToolbar *camera_toolbar;
		EMToolbar *window_toolbar;
		UIButton *freecam_button;

		// doc tabs
		char **mode_model;
		UIComboBox *mode;
		UIMenu *tabs_menu;
		UIButton *close_button;
		UIButton *prev_button;
		UIButton *next_button;
	} title;

	struct
	{
		EMPanel *panel;
		UIList *list;
		UITextEntry *search;
		UIMenu *context_menu;
		UIComboBox *filter;

		EMMapLayerType **layers;
		EMMapLayerType **filtered_layers;
		char **type_list;

		U32 sort_column;
		bool sort_reverse;
	} map_layers;

	struct
	{
		ParseTable *parse_table;
		EMStructFunc free_func;
		char custom_type[256];
		void *data;
	} clipboard;

	struct  
	{
		EMPanel *panel;
		UIList *file_list;
		UIButton *checkout;
		UIButton *undo_checkout;
		UIButton *update;
		UIButton *open_folder;
		UIButton *stat;
		UIButton *revert;
		EMFile **empty_list;
	} file_manager;

	struct 
	{
		EMPanel *panel;
		UIScrollArea *scroll_area;
		UILabel *label[EM_STATUS_NUMLINES];
		MemLog log;
		int idx;
		EMMsgLogFlash **msgs;
	} status;

	struct 
	{
		EMMessageOfTheDay **registered_messages; // all registered messages
		EMMessageOfTheDay *current_message; // currently displayed message
		StashTable registered_messages_by_keyname;
	} message_of_the_day;

	struct
	{
		bool show;
		bool disable;
		bool resize_mode;
		int resize_start_x, resize_start_width;
		F32 scale;
		UIPane *pane, *hidden_pane;
		UITabGroup *tab_group;
		UIExpanderGroup **expander_groups;
		UIWindow **windows;
		EMPanel **panels;
		UISkin *expander_group_skin;
		UIButton *show_button, *hide_button;
	} sidebar;

	struct
	{
		UIWindow *win;
		EMOptionsTab **tabs;
		EMEditor *active_editor;
	} options;

	struct 
	{
		UIWindow *window;
		UILabel *label;
		UIProgressBar *progress;
	} progress;

	struct 
	{
		UIWindow *window;
		UITree *current_tree;
		UIList *current_list;
		UIList *current_no_preview_list;
		UILabel *selected_cnt_lable;
		UILabel *filter_label;
		UITextEntry *search_entry;
		UIComboBox *filter_combo;
		UILabel *tex_size_lable;
		UIComboBox *tex_size_combo;
		UIButton *action_button;
		UIButton *cancel_button;
		UITabGroup *tab_group;
		UITab *tree_tab;
		UITab *list_tab;
		UITab *no_preview_tab;
		EMPickerCallback callback;
		EMPickerClosedWindowCallback closedCallback;
		void *callback_data;
		EMPickerFilter *all_filter;
	} asset_picker;

	struct
	{
		UIWindow *window;
		UIList *list;
		EMRegisteredType **new_types;
		EMPicker **pickers;
	} toolbar_chooser;

	struct
	{
		bool visible;
		UIPane *active_min_pane;
	} minimized_windows;

	struct
	{
		EMSearchResultTab **result_tabs;
		unsigned int curr_num;
	} search_panels;

	struct  
	{
		EMEditorDoc *doc;
		bool active;
	} modal_data;

	struct
	{
		UIWindow *confirm_window;
		EMEditorDoc *doc;
		EMEditorSubDoc *sub_doc;
		EMCloseMode close_mode;
		bool should_quit;
		bool force_close;
		bool force_save;
		bool quitting;
	} close_state;

	struct
	{
		EMSaveEntry **entries;
		UIWindow *progress_window;
	} save_state;

	struct
	{
		EMPromptEntry *current_prompt;
		EMPromptEntry **prompts;
	} prompt_state;

} EMInternalData;

extern EMInternalData em_data;

bool emIsEditorOk(SA_PARAM_NN_VALID EMEditor *editor);
void emCameraInit(GfxCameraController **camera);
EMFile *emGetFile(const char *filename, bool create);
void emSetActiveEditorDoc(SA_PARAM_OP_VALID EMEditorDoc *doc, SA_PARAM_OP_VALID EMEditor *editor);

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#endif // NO_EDITORS

#endif // __EDITORMANAGERPRIVATE_H__
