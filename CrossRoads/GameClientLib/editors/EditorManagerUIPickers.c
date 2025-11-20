/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
GCC_SYSTEM
#ifndef NO_EDITORS

#include "Allegiance.h"
#include "Color.h"
#include "EditLibUIUtil.h"
#include "EditorManagerPrivate.h"
#include "EditorManagerUIPickers_c_ast.h"
#include "EditorPrefs.h"
#include "GfxConsole.h"
#include "GfxHeadshot.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GfxTexturesPublic.h"
#include "GraphicsLib.h"
#include "HashFunctions.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "UIList.h"
#include "UIMinimap.h"
#include "WorldGrid.h"
#include "crypt.h"
#include "fileutil.h"
#include "inputMouse.h"
#include "strings_opt.h"
#include "tokenstore.h"
#include "utils.h"
#include "UGCCommon.h"
#include "UGCProjectUtils.h"

#define STANDARD_ROW_HEIGHT 26

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););
/********************
* FORWARD DECLARATIONS
********************/
#endif
typedef struct EMEasyPicker EMEasyPicker;
typedef struct EMEasyPickerEntry EMEasyPickerEntry;
typedef struct EMEasyPickerFolder EMEasyPickerFolder;
typedef struct EMEasyPickerScanner EMEasyPickerScanner;
typedef struct ZoneMapInfo ZoneMapInfo;
#ifndef NO_EDITORS

static void emEasyPickerInit( EMEasyPicker* picker );
static void emEasyPickerEnter( SA_PARAM_NN_VALID EMEasyPicker* picker );
static bool emEasyPickerSelected( EMPicker* picker, EMPickerSelection* selection );
static void emEasyPickerTexFunc1( SA_PARAM_NN_VALID EMPicker* picker, SA_PARAM_NN_VALID void* entry, ParseTable pti[], SA_PRE_NN_FREE SA_POST_NN_VALID BasicTexture** out_tex, SA_PRE_NN_FREE SA_POST_NN_VALID Color* out_mod_color );
static Color emEasyPickerColorFunc1( SA_PARAM_NN_VALID EMPicker* picker, SA_PARAM_NN_VALID void* entry, ParseTable pti[], bool isSelected );
static EMEasyPickerFolder* emEasyPickerDataRoot( EMEasyPicker* picker );
static FileScanAction emEasyPickerScan( char* dir, struct _finddata32_t* data, EMEasyPickerScanner* scanner );
static EMEasyPickerEntry* emEasyPickerInternEntry(
		SA_PARAM_NN_VALID EMEasyPicker* picker, EMEasyPickerFolder* folder, char* path );
static EMEasyPickerFolder* emEasyPickerInternFolder(
		SA_PARAM_NN_VALID EMEasyPicker* picker, EMEasyPickerFolder* folder, char* subfolder );

static void emPickerTreeSelect(SA_PARAM_NN_VALID UITree *tree, SA_PARAM_NN_VALID EMPicker *picker);
static void emPickerTreeActivate(UITree *tree, EMPicker *picker);
static void emPickerTreeRClickNode(UITree *tree, UserData unused);
static void emPickerListSelect(UIList *list, void *unused);
static void emPickerListActivate(UIList *list, void *unused);
static const char *emPickerTreeNodeGetName(UITreeNode *node);
static void emPickerListNodeDraw(UIList *list, UIListColumn *column, UI_MY_ARGS, F32 z, CBox *logical_box, S32 row, void *unused);
static void emPickerListNodeDrawNoPreviews(UIList *list, UIListColumn *column, UI_MY_ARGS, F32 z, CBox *logical_box, S32 row, void *unused);

typedef struct EMEasyPickerScanner {
	EMEasyPicker* picker;
	EMEasyPickerFolder* root;
} EMEasyPickerScanner;

typedef struct EMEasyPicker {
	EMPicker picker;

	char** exts;
	char** dirRoots;

	EMEasyPickerNameFilter nameFilter;
	EMEasyPickerTexFunc texFunc;
	EMEasyPickerColorFunc colorFunc;
} EMEasyPicker;

#endif
AUTO_STRUCT;
typedef struct EMEasyPickerEntry {
	char* name;
	char* path;
	BasicTexture * texture;
} EMEasyPickerEntry;

AUTO_STRUCT;
typedef struct EMEasyPickerFolder {
	char* name;
	EMEasyPickerFolder** folders;
	EMEasyPickerEntry** entries;
} EMEasyPickerFolder;
#ifndef NO_EDITORS

typedef struct EMZoneMapEncounterObjectPickerWindow {
	UIWindow* window;
	UITabGroup* tabGroup;
	
	UIWidget* rootWidget;
	UITab* mapTab;

	UIWidget* projRootWidget;
	UITab* projMapTab;

	UIWidget* overworldMapWidget;
	UITab* overworldMapTab;
	
	EMPickerZoneMapEncounterObjectCallback cb;
	UserData userData;

	// Window level filtering
	const char* defaultZmap;
	EMPickerZoneMapEncounterObjectFilterFn filterFn;
	UserData filterData;
} EMZoneMapEncounterObjectPickerWindow;

typedef struct EMOverworldMapPickerWindow {
	UIWindow* window;
	UIWidget* overworldMapWidget;
	UIButton* okButton;

	EMPickerZoneMapEncounterObjectCallback cb;
	UserData userData;
} EMOverworldMapPickerWindow;

typedef struct EMZeniObjectPicker {
	UIPane* rootPane;
	UITree* pickerTree;
	UIScrollArea* minimapArea;
	UIMinimap* minimap;
	UIWidget* minimapCustomWidget;
	
	UIPane* selectedDetails;

	ZoneMapEncounterInfo **ugcInfoOverrides;
	S32* filterValidTypes;
	const char* filterZmap;
	EMPickerZoneMapEncounterObjectFilterFn filterFn;
	UserData filterData;

	ZoneMapEncounterInfo** zenis;
} EMZeniObjectPicker;

typedef struct EMOverworldMapIconPicker {
	UIPane* rootPane;
	UIList* iconList;
	UISprite* icon;

	const char** eaIconNames;

	// Widget info for converting between screen space coords and
	// percentage coords
	Vec2 mapLastTopLeft;
	float mapLastWidth;
	float mapLastHeight;
	
	Vec2 iconPosition;
	const char* iconName;

	// Drag and Drop info
	bool isDragging;

	// Changed callbacks
	EMPickerChangedFn changedFn;
	UserData changedData;
} EMOverworldMapIconPicker;

bool emZMShowDebugName;
AUTO_CMD_INT( emZMShowDebugName, emZMShowDebugName );

static const char** emTextureDirs = NULL;
static const char** emTextureExts = NULL;

static bool emPickerShowPreviews = true;
AUTO_CMD_INT(emPickerShowPreviews, emPickerShowPreviews) ACMD_HIDE;
														 
static int emPickerPreviewSize = 96;

//static EMZeniObjectPicker emZMEncObjPicker;

AUTO_COMMAND ACMD_NAME(emPickerPreviewSize);
void emPickerPreviewSizeSet(int newSize)
{
	emPickerPreviewSize = newSize;

	if (em_data.asset_picker.current_list)
		em_data.asset_picker.current_list->fRowHeight = newSize + 2;
}

AUTO_COMMAND ACMD_HIDE ACMD_I_AM_THE_ERROR_FUNCTION_FOR(emPickerPreviewSize);
void emPickerPreviewSizeError(void)
{
	conPrintf("emPickerPreviewSize %i", emPickerPreviewSize);
}


/********************
* UTIL
********************/
/******
* This function compares pickers for the purpose of ordering them alphabetically.
* PARAMS:
*   picker1 - EMPicker address for the first picker to compare
*   picker2 - EMPicker address for the second picker to compare
* RETURNS:
*   int comparison of the two pickers' names
******/
static int emPickerCompare(const EMPicker **picker1, const EMPicker **picker2)
{
	return stricmp((*picker1)->picker_name, (*picker2)->picker_name);
}

/******
* This function validates a picker, saving the validation results on the picker itself to quicken
* future validation checks.  The picker's valid flag can be interpreted as follows:
*   0 = validation has not yet been performed
*   1 = validation was performed and failed
*   2 = validation was performed and successful
* PARAMS:
*   picker - EMPicker picker to validate
* RETURNS:
*   bool indicating whether specified picker is valid
******/
bool emPickerValidate(EMPicker *picker)
{
	// if validation was done previously, returned stored result
	if (picker->valid)
		return picker->valid == 2;

	// make sure server data is available if necessary for the picker
	if (picker->requires_server_data && !hasServerDir_NotASecurityCheck())
	{
		picker->valid = 1;
		return false;
	}

	// only show outsourced pickers in outsource mode
	if (em_data.outsource_mode)
	{
		if (picker->allow_outsource)
		{
			picker->valid = 2;
			return true;
		}

		picker->valid = 1;
		return false;
	}

	// Picker is usable
	picker->valid = 2;
	return true;
}

/******
* This function creates the tree that can be used as any asset picker tree; also used to create 
* the filtered search tree.
* PARAMS:
*   picker - EMPicker to set onto the tree's various callback data
* RETURNS:
*   UITree tailored to fit into the asset picker window layout with the appropriate callbacks set
******/
static UITree *emPickerTreeCreate(EMPicker *picker)
{
	UITree *tree = ui_TreeCreate(0, 0, 1, 1);

	tree->widget.offsetFrom = UITopLeft;
	tree->widget.widthUnit = tree->widget.heightUnit = UIUnitPercentage;
	tree->widget.topPad = 5;
	tree->selectedData = picker;
	tree->activatedData = picker;
	tree->selectedF = emPickerTreeSelect;
	tree->activatedF = emPickerTreeActivate;
	ui_TreeSetContextCallback(tree, emPickerTreeRClickNode, NULL);

	return tree;
}

/******
* This function creates a list out of the contents of the specified picker.
* PARAMS:
*   picker - EMPicker from which to create the list
* RETURNS:
*   UIList created with the picker's contents
******/
static UIList *emPickerListCreate(EMPicker *picker, bool allow_previews)
{
	UIList *list;
	UIListColumn *column;
	int i;
	bool has_texs = false;

	if (allow_previews)
	{
		for (i = 0; i < eaSize(&picker->display_types); ++i)
		{
			if (picker->display_types[i]->tex_func)
			{
				has_texs = true;
				break;
			}
		}
	}

	list = ui_ListCreate(NULL, &picker->list_model, has_texs ? emPickerPreviewSize + 2 : 14);
	ui_WidgetSetPosition(UI_WIDGET(list), 0, 5);
	ui_WidgetSetDimensionsEx(UI_WIDGET(list), 1, 1, UIUnitPercentage, UIUnitPercentage);
	ui_ListSetSelectedCallback(list, emPickerListSelect, NULL);
	ui_ListSetActivatedCallback(list, emPickerListActivate, NULL);
	list->fHeaderHeight = 0;

	column = ui_ListColumnCreateCallback("Names",
										 allow_previews ? emPickerListNodeDraw : emPickerListNodeDrawNoPreviews,
										 NULL);
	ui_ListAppendColumn(list, column);

	return list;
}

/******
* This function returns the EMPickerDisplayType associated with the specified ParseTable.
* PARAMS:
*   picker - EMPicker to search for the display type
*   table - ParseTable whose associated display type will be returned
* RETURNS:
*   EMPickerDisplayType belonging to the specified picker associated to the specified ParseTable
******/
static EMPickerDisplayType *emPickerGetDisplayType(EMPicker *picker, ParseTable *table)
{
	int i;
	for (i = 0; i < eaSize(&picker->display_types); ++i)
	{
		if (picker->display_types[i]->parse_info == table)
			return picker->display_types[i];
	}

	return NULL;
}

/******
* This function is used by the tree text drawing code to display picker tree nodes.
* PARAMS:
*   text - string text to display at the node
*   clr - Color to draw the text
*   x - F32 x-coordinate of where to start drawing text
*   y - F32 y-coordinate of where to start drawing text
*   scale - F32 scale to apply to the drawn text
*   z - F32 z-depth of the drawn text
*   tex - BasicTexture to draw immediately preceding the text; can be NULL
*   mod_color - mod color for tex
******/
static void emPickerTreeDisplayText(const char *text, const char *notes, Color clr, F32 x, F32 y, F32 scale, F32 z, BasicTexture *tex, Color mod_color)
{
	// ensure colors are fully opaque
	clr.a = 255;

	// display any display-type-specific textures
	if (tex)
	{
		int maxDim = MAX(texHeight(tex), texWidth(tex));
		int dispHeight = MIN(emPickerPreviewSize, maxDim) * scale;
		float dispScale = (float)dispHeight / maxDim;
		int dispWidth = texWidth(tex) * dispScale;
		
		display_sprite_tex(tex, x, y - (texHeight(tex)*dispScale * 0.5f), z, dispScale, dispScale, RGBAFromColor(mod_color));
		x += (dispWidth + 2);
	}

	// set font and print text
	ui_StyleFontUse(NULL, false, kWidgetModifier_None);
	gfxfont_SetColor(clr, clr);
	gfxfont_Printf(x, y, z, scale, scale, CENTER_Y, "%s", text);
	
	// Print notes if available
	if (notes)
	{
		ui_StyleFontUse(NULL, false, kWidgetModifier_None);
		gfxfont_SetColor(ColorBlack, ColorBlack);
		gfxfont_Printf(x+350, y, z, scale, scale, CENTER_Y, ":: %s", notes);
	}
}

/******
* This function takes picker content entry and returns the display name.
* PARAMS:
*   table - ParseTable of an entry
*   data - The data for that entry
* RETURNS:
*   string name of the entry
******/
static const char *emPickerDataGetName(EMPicker *picker, ParseTable *table, void *contents)
{
	static char message[1024];
	EMPickerDisplayType *type = emPickerGetDisplayType(picker, table);

	assert(type);

	if (table)
	{
		int i;

		// find the string for the specified field
		FORALL_PARSETABLE(table, i)
		{
			if (table[i].name && stricmp(table[i].name, type->display_name_parse_field) == 0)
			{
				char * token = NULL;
				bool bValidToken;
				estrStackCreateSize(&token,256);
				bValidToken = TokenWriteText(table, i, contents, &token, 0);

				if (estrLength(&token) >= 256)
				{
					Errorf("Token is too long: %s \n",token);
					bValidToken = false;
				}

				if (bValidToken)
					sprintf(message, "%s", token);
				else
					sprintf(message, "<%s>", type->display_name_parse_field);

				estrDestroy(&token);

				return message;
			}
		}
	}

	// by default, if a parse table is not used, return the parse field
	sprintf(message, "<%s>", type->display_name_parse_field);
	return message;
}

/******
* This function takes picker content entry and returns the display notes.
* PARAMS:
*   table - ParseTable of an entry
*   data - The data for that entry
* RETURNS:
*   string notes of the entry
******/
static const char *emPickerDataGetNotes(EMPicker *picker, ParseTable *table, void *contents)
{
	static char message[1024];
	EMPickerDisplayType *type = emPickerGetDisplayType(picker, table);

	assert(type);

	// NULL if no notes
	if (!type->display_notes_parse_field)
		return NULL;

	if (table)
	{
		int i;

		// find the string for the specified field
		FORALL_PARSETABLE(table, i)
		{
			if (table[i].name && stricmp(table[i].name, type->display_notes_parse_field) == 0)
			{
				char * token = NULL;
				bool bValidToken;
				estrStackCreateSize(&token,1024);
				bValidToken = TokenWriteText(table, i, contents, &token, 0);
				if (estrLength(&token) >= 1024)
				{
					Errorf("Token is too long: %s \n",token);
					bValidToken = false;
				}

				if (bValidToken)
					sprintf(message, "%s", token);
				else
					sprintf(message, "<%s>", type->display_notes_parse_field);

				estrDestroy(&token);

				// Return null instead of empty message
				if (message && message[0]) 
					return message;
				else
					return NULL;
			}
		}
	}

	// by default, if a parse table is not used, return the parse field
	sprintf(message, "<%s>", type->display_notes_parse_field);
	return message;
}

/******
* This function takes a picker tree node and returns the display name.
* PARAMS:
*   node - UITreeNode whose name is to be retrieved
* RETURNS:
*   string name to display for the node
******/
static const char *emPickerTreeNodeGetName(UITreeNode *node)
{
	return emPickerDataGetName(node->tree->selectedData, node->table, node->contents);
}


/******
* This function takes a picker tree node and returns the display notes.
* PARAMS:
*   node - UITreeNode whose notes are to be retrieved
* RETURNS:
*   string notes to display for the node
******/
static const char *emPickerTreeNodeGetNotes(UITreeNode *node)
{
	return emPickerDataGetNotes(node->tree->selectedData, node->table, node->contents);
}


static bool nameMatchesSearchText(const unsigned char *name, const unsigned char *search_text)
{
	// VisualAssist style - perhaps too loose to be useful?
#if 0
	while (*name)
	{
		if (tolower(*name) == tolower(*search_text))
		{
			search_text++;
			if (!*search_text)
				return true;
		}
		name++;
	}
	return false;
#else
	// Just ignore underscores and handle wildcards
	char namebuf[MAX_PATH];
	char searchbuf[MAX_PATH];
	strcpy(namebuf, name);
	strcpy(searchbuf, "*");
	strcat(searchbuf, search_text);
	strcat(searchbuf, "*");
	if (!strchr(search_text, '_'))
	{
		stripUnderscoresInPlace(namebuf);
		stripUnderscoresInPlace(searchbuf);
	}
	return match(searchbuf, namebuf);
#endif
}

static void emPickerRefreshListModelHelper(EMPicker *picker, ParseTable *parent_table, void *parent_data, const char *search_text)
{
	int i, j;

	// create a list model from the data, its ParseTable, and the registered EMPickerDisplayTypes
	FORALL_PARSETABLE(parent_table, i)
	{
		if (TOK_GET_TYPE(parent_table[i].type) == TOK_STRUCT_X && parent_table[i].type & TOK_EARRAY)
		{
			void **children = *TokenStoreGetEArray(parent_table, i, parent_data, NULL);
			ParseTable *child_table = parent_table[i].subtable;
			EMPickerDisplayType *type = emPickerGetDisplayType(picker, child_table);

			// populate child nodes that correspond to display types
			if (type)
			{
				for (j = 0; j < eaSize(&children); ++j)
				{
					void *child_data = children[j];
					EMPickerFilter *filter;
					const char *child_name = emPickerDataGetName(picker, child_table, child_data);
					const char *child_notes = emPickerDataGetNotes(picker, child_table, child_data);

					if (type->selected_func &&
						(!picker->filter_func || picker->filter_func(child_data, child_table)) &&
						(!(filter = ui_ComboBoxGetSelectedObject(em_data.asset_picker.filter_combo)) || !filter->checkF || filter->checkF(child_data, child_table)) &&
						(!search_text || nameMatchesSearchText(child_name, search_text)))
					{
						// Add node to list
						EMPickerListNode *newNode = calloc(1, sizeof(EMPickerListNode));
						newNode->name = strdup(child_name);
						newNode->notes = strdup(child_notes);
						newNode->table = child_table;
						newNode->contents = child_data;
						eaPush(&picker->list_model, newNode);
					}
					
					if (!type->is_leaf)
					{
						// Recurse on non-leaves
						emPickerRefreshListModelHelper(picker, child_table, child_data, search_text);
					}
				}
			}
		}
	}
}

static int emPickerCompareListNodes(const EMPickerListNode** left, const EMPickerListNode** right)
{
	return stricmp((*left)->name,(*right)->name);
}

/******
* This function refreshes the model for the list view in the picker.
* PARAMS:
*   picker - The picker to be refreshed
******/
static void emPickerRefreshListModel(EMPicker *picker)
{
	int i;
	const char *search_text = ui_TextEntryGetText(em_data.asset_picker.search_entry);

	if (search_text && !search_text[0])
		search_text = NULL;

	// Free old list model
	for(i = eaSize(&picker->list_model)-1; i >= 0; i--)
	{
		EMPickerListNode *node = picker->list_model[i];
		free(node->name);
		SAFE_FREE(node->notes);
		free(node);
	}
	eaDestroy(&picker->list_model);

	// Build new list model
	emPickerRefreshListModelHelper(picker, picker->display_parse_info_root, picker->display_data_root, search_text);

	// Sort it
	eaQSort(picker->list_model, emPickerCompareListNodes);
}


static void emPickerClearSelections(void)
{
	EMPicker *picker = em_data.asset_picker.current_tree->selectedData;
	int i;

	// Clear selections on tree and list
	ui_TreeUnselectAll(em_data.asset_picker.current_tree);
	ui_ListClearEverySelection(em_data.asset_picker.current_list);

	// Free memory from selections
	for(i = eaSize(&picker->selections)-1; i >= 0; i--)
		free(picker->selections[i]);
	eaDestroy(&picker->selections);

	// Disable action button
	ui_SetActive(UI_WIDGET(em_data.asset_picker.action_button), false);
}

/********************
* UI CALLBACKS
********************/
static void emPickerPerformAction(EMPicker *picker)
{
	int i;

	if (em_data.asset_picker.callback && em_data.asset_picker.callback(picker, picker->selections, em_data.asset_picker.callback_data))
	{
		ui_WindowClose(em_data.asset_picker.window);
		eaDestroyEx(&picker->selections, NULL);
	}
	else if (!em_data.asset_picker.callback)
	{
		ui_WindowClose(em_data.asset_picker.window);
		for (i = 0; i < eaSize(&picker->selections); i++)
			emOpenFileEx(picker->selections[i]->doc_name, picker->selections[i]->doc_type[0] ? picker->selections[i]->doc_type : picker->default_type);
	}
}

static void emPickerTreeSelect(UITree *tree, EMPicker *picker)
{
	const UITreeNode * const * const *selected_nodes = ui_TreeGetSelectedNodes(tree);
	int i, j;
	bool found;

	assert(selected_nodes);
		
	// remove selections that are no longer present
	for (i = eaSize(&picker->selections) - 1; i >= 0; i--)
	{
		found = false;
		for (j = eaSize(selected_nodes) - 1; j >= 0; j--)
		{
			if (picker->selections[i]->private_ref == (*selected_nodes)[j])
			{
				found = true;
				break;
			}
		}
		if (!found)
		{
			free(picker->selections[i]);
			eaRemove(&picker->selections, i);
		}
	}

	// add selections that are new
	for (i = eaSize(selected_nodes) - 1; i >= 0; i--)
	{
		const UITreeNode *node = (*selected_nodes)[i];
		found = false;
		for (j = eaSize(&picker->selections) - 1; j >= 0; j--)
		{
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '**picker[432][0]'"
			if (picker->selections[j]->private_ref == node)
			{
				found = true;
				break;
			}
		}
		if (!found)
		{
			EMPickerSelection *selection = calloc(1, sizeof(EMPickerSelection));
			EMPickerDisplayType *type;

			selection->private_ref = (void*) node;
			selection->table = node->table;
			selection->data = node->contents;
			type = emPickerGetDisplayType(picker, selection->table);
			assert(type);

			// call selected callback; discard if doc_name and doc_type are not provided in file opening mode
			if (type->selected_func && type->selected_func(picker, selection))
			{
				if (em_data.asset_picker.callback || (selection->doc_name[0] && selection->doc_type[0]))
					eaPush(&picker->selections, selection);
			}
			else
				free(selection);
		}
	}

	ui_WindowRemoveChild(em_data.asset_picker.window, em_data.asset_picker.selected_cnt_lable);
	if(eaSize(&picker->selections) > 1) {
		char text_buf[256];
		sprintf(text_buf, "Selected Count: %d", eaSize(&picker->selections));
		ui_LabelSetText(em_data.asset_picker.selected_cnt_lable, text_buf);
		ui_WindowAddChild(em_data.asset_picker.window, em_data.asset_picker.selected_cnt_lable);
	}

	if (eaSize(&picker->selections))
		ui_SetActive(UI_WIDGET(em_data.asset_picker.action_button), true);
	else
		ui_SetActive(UI_WIDGET(em_data.asset_picker.action_button), false);
}

static void emPickerTreeActivate(UITree *tree, EMPicker *picker)
{
	// this is called for double-click action
	if (eaSize(&picker->selections))
		emPickerPerformAction(picker);
	else if (tree->selected)
	{	
		// if we have no valid selections, then "tree->selected" holds last node clicked
		if (tree->selected->open)
			ui_TreeNodeCollapse(tree->selected);
		else
			ui_TreeNodeExpand(tree->selected);
	}
}

static void emPickerTreeRClickSelectChildren(UIMenu *pMenu, UITree *tree)
{
	UITreeNode *selected = ui_TreeGetSelected(tree);

	if (!selected)
		return;

	ui_TreeNodeExpand(selected);
	if (!selected->children)
		return;

	ui_TreeSelectLeaves(tree, selected, false);
}

static void emPickerTreeRClickSelectChildrenAndSub(UIMenu *pMenu, UITree *tree)
{
	UITreeNode *selected = ui_TreeGetSelected(tree);

	if (!selected)
		return;

	elUITreeExpandAll(selected);
	if (!selected->children)
		return;

	ui_TreeSelectLeaves(tree, selected, true);
}

static void emPickerTreeRClickNode(UITree *tree, UserData unused)
{
	int i;
	static UIMenu *pPickerTreeRightClickMenu = NULL;
	UITreeNode *selected = ui_TreeGetSelected(tree);

	if (!tree->multiselect || !selected)
		return;

	if(!pPickerTreeRightClickMenu)
	{
		pPickerTreeRightClickMenu = ui_MenuCreate(NULL);
		ui_MenuAppendItems(pPickerTreeRightClickMenu,
			ui_MenuItemCreate("Select Children",UIMenuCallback, emPickerTreeRClickSelectChildren, NULL, NULL),
			ui_MenuItemCreate("Select Children and Sub Folder Children",UIMenuCallback, emPickerTreeRClickSelectChildrenAndSub, NULL, NULL),
			NULL);	
	}
	for ( i=0; i < eaSize(&pPickerTreeRightClickMenu->items); i++ )
		pPickerTreeRightClickMenu->items[i]->clickedData = tree;
	ui_MenuPopupAtCursor(pPickerTreeRightClickMenu);
}

static void emPickerListSelect(UIList *list, void *unused)
{
	EMPicker *picker = em_data.asset_picker.current_tree->selectedData;
	const S32 * const *selected_rows = ui_ListGetSelectedRows(list);
	int i, j;
	bool found;

	assert(selected_rows);
		
	// remove selections that are no longer present
	for (i = eaSize(&picker->selections) - 1; i >= 0; i--)
	{
		found = false;
		for (j = eaiSize(selected_rows) - 1; j >= 0; j--)
		{
			if (picker->selections[i]->private_ref == picker->list_model[(*selected_rows)[j]])
			{
				found = true;
				break;
			}
		}
		if (!found)
		{
			free(picker->selections[i]);
			eaRemove(&picker->selections, i);
		}
	}

	// add selections that are new
	for (i = eaiSize(selected_rows) - 1; i >= 0; i--)
	{
		EMPickerListNode *node = picker->list_model[(*selected_rows)[i]];
		found = false;
		for (j = eaSize(&picker->selections) - 1; j >= 0; j--)
		{
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '**picker[432][0]'"
			if (picker->selections[j]->private_ref == node)
			{
				found = true;
				break;
			}
		}
		if (!found)
		{
			EMPickerSelection *selection = calloc(1, sizeof(*selection));
			EMPickerDisplayType *type;

			selection->private_ref = (void*) node;
			selection->table = node->table;
			selection->data = node->contents;
			type = emPickerGetDisplayType(picker, selection->table);
			assert(type);

			// call selected callback; discard if doc_name and doc_type are not provided in file opening mode
			if (type->selected_func && type->selected_func(picker, selection))
			{
				if (em_data.asset_picker.callback || (selection->doc_name[0] && selection->doc_type[0]))
					eaPush(&picker->selections, selection);
			}
			else
				free(selection);
		}
	}

	if (eaSize(&picker->selections))
		ui_SetActive(UI_WIDGET(em_data.asset_picker.action_button), true);
	else
		ui_SetActive(UI_WIDGET(em_data.asset_picker.action_button), false);
}

static void emPickerListActivate(UIList *list, void *unused)
{
	EMPicker *picker = em_data.asset_picker.current_tree->selectedData;

	// This is called for double-click action
	if (eaSize(&picker->selections))
	{
		emPickerPerformAction(picker);
	}
}

static bool emPickerClosed(UIWindow *window, EMPicker *picker)
{
	// Call leave function when picker is closed
	if (picker && picker->leave_func)
		picker->leave_func(picker);

	if (picker) {
		int i;
		for ( i=0; i < eaSize(&picker->display_types); i++ ) {
			if(picker->display_types[i]->tex_free_func)
				picker->display_types[i]->tex_free_func(picker);
		}
	}

	if (picker && em_data.asset_picker.closedCallback)
		em_data.asset_picker.closedCallback(picker, em_data.asset_picker.callback_data);

	// Save preferences
	if (picker)
	{
		EditorPrefStoreWindowPosition(picker->picker_name, "Picker", "Window Position", em_data.asset_picker.window);
		EditorPrefStoreInt(picker->picker_name, "Picker", "Active Tab", ui_TabGroupGetActiveIndex(em_data.asset_picker.tab_group));
		EditorPrefStoreString(picker->picker_name, "Picker", "Filter Text", ui_TextEntryGetText(em_data.asset_picker.search_entry));
	}

	em_data.asset_picker.callback = NULL;
	em_data.asset_picker.closedCallback = NULL;
	em_data.asset_picker.callback_data = NULL;

	return true;
}

static void emPickerCancel(UIWidget *widget, UserData unused)
{
	ui_WindowClose(em_data.asset_picker.window);
}

static void emPickerFilterSelected(UIComboBox *filter, EMPicker *picker)
{
	EMPickerFilter *selected = ui_ComboBoxGetSelectedObject(filter);

	// Refresh the tree and list
	ui_TreeRefresh(em_data.asset_picker.current_tree);
	emPickerRefreshListModel(em_data.asset_picker.current_tree->selectedData);

	if (selected)
		EditorPrefStoreString(picker->picker_name, "Picker", "Filter Selection", selected->display_text);
}

static void emPickerTextSearch(UIWidget *widget, UITextEntry *entry)
{
	EMPicker *picker = em_data.asset_picker.current_tree->selectedData;

	// Refresh the tree and list
	ui_TreeRefresh(em_data.asset_picker.current_tree);
	emPickerRefreshListModel(picker);

	// Clear all selections
	emPickerClearSelections();
}

static void emPickerAction(UIButton *button, UserData unused)
{
	emPickerPerformAction(em_data.asset_picker.current_tree->selectedData);
}

static void emPickerTreeNodeDisplay(UITreeNode *node, void *unused, UI_MY_ARGS, F32 z)
{
	EMPickerDisplayType *type = emPickerGetDisplayType(node->tree->selectedData, node->table);
	BasicTexture* tex = NULL;
	Color mod_color = ColorWhite;
	bool is_selected;
	assert(type);

	if (type->tex_func && emPickerShowPreviews)
		type->tex_func(node->tree->selectedData, node->contents, node->table, &tex, &mod_color);
	if (tex)
		node->height = emPickerPreviewSize * scale + 2;

	is_selected = ui_TreeIsNodeSelected(node->tree, node);
	
	emPickerTreeDisplayText(emPickerTreeNodeGetName(node), emPickerTreeNodeGetNotes(node), type->color_func ? type->color_func(node->tree->selectedData, node->contents, node->table, is_selected) : (is_selected ? type->selected_color : type->color), x, y + h / 2, scale, z, tex, mod_color);
}

static void emPickerTexSizeChange(UIComboBox *combo, int idx, EMPicker *picker)
{
	EditorPrefStoreInt(picker->picker_name, "Picker", "TexSize", idx);
	switch(idx)
	{
	case EMPreviewTex_Small:
		emPickerPreviewSizeSet(96);
		break;
	case EMPreviewTex_Medium:
		emPickerPreviewSizeSet(192);
		break;
	case EMPreviewTex_Large:
		emPickerPreviewSizeSet(384);
		break;
	}
}

static void emPickerTabChange(UITabGroup *tab_group, void *unused)
{
	emPickerClearSelections();
}

static void emPickerListNodeDraw(UIList *list, UIListColumn *column, UI_MY_ARGS, F32 z, CBox *logical_box, S32 row, void *unused)
{
	EMPicker *picker = em_data.asset_picker.current_tree->selectedData;
	EMPickerListNode *node = picker->list_model[row];
	EMPickerDisplayType *type = emPickerGetDisplayType(picker, node->table);
	bool is_selected;
	BasicTexture* tex = NULL;
	Color mod_color = ColorWhite;
	
	assert(type);

	if (type->tex_func && emPickerShowPreviews)
		type->tex_func(picker, node->contents, node->table, &tex, &mod_color);

	is_selected = ui_ListIsSelected(list, column, row);
	emPickerTreeDisplayText(node->name, node->notes, type->color_func ? type->color_func(picker, node->contents, node->table, is_selected) : (is_selected ? type->selected_color : type->color), x+3, y + h / 2, scale, z, tex, mod_color);
}

static void emPickerListNodeDrawNoPreviews(UIList *list, UIListColumn *column, UI_MY_ARGS, F32 z, CBox *logical_box, S32 row, void *unused)
{
	EMPicker *picker = em_data.asset_picker.current_tree->selectedData;
	EMPickerListNode *node = picker->list_model[row];
	EMPickerDisplayType *type = emPickerGetDisplayType(picker, node->table);
	bool is_selected;
	assert(type);

	is_selected = ui_ListIsSelected(list, column, row);
	emPickerTreeDisplayText(node->name, node->notes, type->color_func ? type->color_func(picker, node->contents, node->table, is_selected) : (is_selected ? type->selected_color : type->color), x+3, y + h / 2, scale, z, NULL, ColorWhite);
}

static void emPickerTreeNodeFill(UITreeNode *parent_node, UITextEntry *search_entry)
{
	int i, j;
	EMPicker *picker = parent_node->tree->selectedData;
	const char *search_text = search_entry ? ui_TextEntryGetText(search_entry) : NULL;

	if (search_text && !search_text[0])
		search_text = NULL;

	// create a UITree from the data, its ParseTable, and the registered EMPickerDisplayTypes
	FORALL_PARSETABLE(parent_node->table, i)
	{
		if (TOK_GET_TYPE(parent_node->table[i].type) == TOK_STRUCT_X && parent_node->table[i].type & TOK_EARRAY)
		{
			void **children = *TokenStoreGetEArray(parent_node->table, i, parent_node->contents, NULL);
			ParseTable *child_table = parent_node->table[i].subtable;
			EMPickerDisplayType *type = emPickerGetDisplayType(picker, child_table);

			// populate child nodes that correspond to display types
			if (type)
			{
				for (j = 0; j < eaSize(&children); ++j)
				{
					void *child_data = children[j];
					UITreeNode *newNode = NULL;
					EMPickerFilter *filter;

					if ((!picker->filter_func || picker->filter_func(child_data, child_table)) && 
						(!(filter = ui_ComboBoxGetSelectedObject(em_data.asset_picker.filter_combo)) || !filter->checkF || filter->checkF(child_data, child_table)) &&
						(!search_text || nameMatchesSearchText(emPickerDataGetName(picker, child_table, child_data), search_text)))
					{
						// Add all children of a node with matching name
						newNode = ui_TreeNodeCreate(
							parent_node->tree, 0, child_table, child_data,
							type->is_leaf? NULL : emPickerTreeNodeFill, NULL,
							type->draw_func ? type->draw_func : emPickerTreeNodeDisplay, NULL, 14);

						newNode->crc = cryptAdler32String(emPickerTreeNodeGetName(newNode));
						ui_TreeNodeAddChild(parent_node, newNode);
					}
					else if (!type->is_leaf)
					{
						// Add folder if it has a child that matches
						bool match = false;

						// Temporarily add the node
						newNode = ui_TreeNodeCreate(
							parent_node->tree, 0, child_table, child_data,
							type->is_leaf? NULL : emPickerTreeNodeFill, search_entry,
							type->draw_func ? type->draw_func : emPickerTreeNodeDisplay, NULL, 14);

						newNode->crc = cryptAdler32String(emPickerTreeNodeGetName(newNode));
						ui_TreeNodeAddChild(parent_node, newNode);

						// Expand it to force its children to be figured out
						ui_TreeNodeExpand(newNode);

						// Remove it if it has no children
						if (!eaSize(&newNode->children))
						{
							if (ui_TreeNodeRemoveChild(parent_node, newNode))
								newNode = NULL;
						}

						// Collapse it when done
						if (newNode)
							ui_TreeNodeCollapse(newNode);
					}
				}
			}
		}
	}
}


/********************
* PICKER MANAGEMENT
********************/
/******
* This function returns a picker registered with the Editor Manager, taking its name as the search index.
* PARAMS:
*   name - string name of the picker to return
* RETURNS:
*   EMPicker whose name matches the specified name
******/
EMPicker *emPickerGetByName(const char *name)
{
	EMPicker *picker;
	if (stashFindPointer(em_data.registered_pickers, name, &picker))
		return picker;
	else
		return NULL;
}

static void emTexturePickerTex( const char* path, BasicTexture** outTex, Color* outColor )
{
	char texName[ MAX_PATH ];
	BasicTexture * source_tex;
	getFileNameNoExt( texName, path );

	source_tex = texFindAndFlag( texName, false, WL_FOR_PREVIEW_INTERNAL );
// TODO DJR put this on a switch for testing
#if 0
	if (source_tex && (texIsCubemap(source_tex) || texIsVolume(source_tex)))
	{
		if (!*outTex)
		{
			BasicTexture ** eaTextureSwaps = NULL;
			eaPush(&eaTextureSwaps, texFindAndFlag("test_cube_cube", false, WL_FOR_PREVIEW_INTERNAL));
			eaPush(&eaTextureSwaps, source_tex);
			// TODO DJR - make a standardized texture preview wrapper, use a smaller headshot
			source_tex = gfxHeadshotCaptureModelEx("TexturePick", 1024, 1024, modelFind("OutwardAndInwardSpheres", true, WL_FOR_PREVIEW_INTERNAL), NULL, ColorTransparent, eaTextureSwaps);
		}
		else
		{
			source_tex = *outTex;
			gfxHeadshotRaisePriority(source_tex);
		}
	}
#endif
	*outColor = ColorWhite;
	*outTex = source_tex;
}

/******
* This function returns a texture picker that'll open with the specified type, creating it if necesarry.
* PARAMS:
*   type - string type for the picker to return.
* RETURNS:
*   EMPicker for textures whose type matches the specified name
******/
EMPicker *emTexturePickerCreateForType(const char *type)
{
	EMPicker* accum;

	if (!emTextureExts)
	{
		eaPush( &emTextureExts, ".wtex" );
		eaPush( &emTextureExts, ".TexWord" );
	}
	if (!emTextureDirs)
	{
		eaPush( &emTextureDirs, "texture_library/" );
		eaPush( &emTextureDirs, "texts/English/texture_library/" );
	}

	accum = emEasyPickerCreateEx( "Texture Picker", emTextureExts, emTextureDirs, NULL, emTexturePickerTex );
	strcpy( accum->default_type, type );

	return accum;
}



/******
* This function is used by managed pickers to handle selections
******/
static bool emPickerManagedSelectedFunc(EMPicker *picker, EMPickerSelection *selection)
{
	sprintf(selection->doc_name, "%s", ((ResourceInfo*)selection->data)->resourceName);
	strcpy(selection->doc_type, picker->default_type);

	return true;
}


/******
* This function is used by managed pickers to detect dictionary changes
******/
static void emPickerManagedDictChangeCallback(enumResourceEventType eType, const char *dict_name, const void *data2, void *data1, EMPicker *picker)
{
	emPickerRefreshSafe(picker);
}


/******
* This function is used by managed pickers to start subscribing to changes
******/
static void emPickerManagedEnterFunc(EMPicker *picker)
{
	// Subscribe to changes
	resDictRegisterEventCallback(picker->dict_name,emPickerManagedDictChangeCallback,picker);

	// Build the tree
	resBuildGroupTree(picker->dict_name, picker->res_group);
	emPickerRefreshSafe(picker);
}


/******
* This function is used by managed pickers to stop subscribing to changes
******/
static void emPickerManagedLeaveFunc(EMPicker *picker)
{
	// Stop subscribing to changes
	resDictRemoveEventCallback(picker->dict_name,emPickerManagedDictChangeCallback);
}


/******
* This function is used by managed pickers to set up the standard display type information
******/
static void emPickerManagedInitFunc(EMPicker *picker)
{
	EMPickerDisplayType *pDispType;	

	pDispType = calloc(1, sizeof(EMPickerDisplayType));
	pDispType->parse_info = parse_ResourceGroup;
	pDispType->display_name_parse_field = "Name";
	pDispType->color = CreateColorRGB(0, 0, 0);
	pDispType->selected_color = CreateColorRGB(255, 255, 255);
	pDispType->is_leaf = 0;
	eaPush(&picker->display_types, pDispType);

	pDispType = calloc(1, sizeof(EMPickerDisplayType));
	pDispType->parse_info = parse_ResourceInfo;
	pDispType->display_name_parse_field = "resourceName";	
	pDispType->display_notes_parse_field = "resourceNotes";
	pDispType->selected_func = emPickerManagedSelectedFunc;
	pDispType->color = CreateColorRGB(0,0,80);
	pDispType->selected_color = CreateColorRGB(255, 255, 255);
	pDispType->is_leaf = 1;
	eaPush(&picker->display_types, pDispType);
}

/******
* This function registers a picker with the Editor Manager.  This should be called in an AUTO_RUN, so
* that the picker will be registered when Editor Manager initialization occurs, which will call the initialization
* function on the picker.  This does not need to be done for pickers that are inside of the EMEditor's pickers EArray.
* PARAMS:
*   picker - EMPicker to register
******/
void emPickerRegister(EMPicker *picker)
{
	// ensure picker has a name
	if (!picker->picker_name[0])
	{
		Errorf("Attempted to register an unnamed picker.");
		return;
	}

	if (!em_data.registered_pickers)
		em_data.registered_pickers = stashTableCreateWithStringKeys(16, StashDefault);

	// insert into global picker array and stash table
	eaPushUnique(&em_data.pickers, picker);
	stashAddPointer(em_data.registered_pickers, picker->picker_name, picker, false);
}

/******
* This function sets the picker to be managed.
* PARAMS:
*    picker - EMPicker picker to refresh
******/
void emPickerManage(EMPicker *picker)
{
	assertmsg(resDictGetInfo(picker->default_type), "Managed pickers must have a dictionary");
	

	picker->managed = true;
	picker->dict_name = picker->default_type;
	picker->res_group = calloc(1, sizeof(ResourceGroup));
	picker->display_data_root = picker->res_group;
	picker->display_parse_info_root = parse_ResourceGroup;
	picker->init_func = emPickerManagedInitFunc;
	picker->enter_func = emPickerManagedEnterFunc;
	picker->leave_func = emPickerManagedLeaveFunc;
}

/******
* This function refreshes the picker's associated tree and list.
* PARAMS:
*    picker - EMPicker picker to refresh
******/
void emPickerRefresh(EMPicker *picker)
{
	if (picker && emPickerValidate(picker) && picker->initialized)
	{
		if (picker->managed)
			resBuildGroupTree(picker->dict_name, picker->res_group);

		if (picker->tree_ui_control)
			ui_TreeRefresh(picker->tree_ui_control);

		emPickerRefreshListModel(picker);
	}
	picker->refresh_queued = false; // Clear queued refresh (if any)
}


/******
* This function refreshes the picker's associated tree and list at the end of the tick.
* PARAMS:
*    picker - EMPicker picker to refresh
******/
void emPickerRefreshSafe(EMPicker *picker)
{
	if (picker && emPickerValidate(picker) && picker->initialized)
	{
		if (!picker->refresh_queued)
		{
			picker->refresh_queued = true;
			emQueueFunctionCall(emPickerRefresh, picker);
		}
	}
}


/********************
* MAIN
********************/
/******
* This function initializes the specified picker.
* PARAMS:
*   picker - EMPicker to initialize
******/
void emPickerInit(EMPicker *picker)
{
	bool has_texs = false;
	int i;
	
	// just need to reset rowheights
	if (picker->initialized)
	{
		// figure out if it has previews
		for (i = 0; i < eaSize(&picker->display_types); ++i)
		{
			if (picker->display_types[i]->tex_func)
			{
				has_texs = true;
				break;
			}
		}

		if( has_texs ) {
			picker->list_ui_control->fRowHeight = emPickerPreviewSize + 2;
		} else {
			picker->list_ui_control->fRowHeight = 14;
		}
		return;
	}

	// call initialization callback
	if (picker->init_func)
		picker->init_func(picker);
	picker->initialized = 1;

	// ensure picker is valid
	if (!emPickerValidate(picker))
		return;

	// figure out if it has previews
	for (i = 0; i < eaSize(&picker->display_types); ++i)
	{
		if (picker->display_types[i]->tex_func)
		{
			has_texs = true;
			break;
		}
	}

	// create tree
	picker->tree_ui_control = emPickerTreeCreate(picker);
	ZeroStruct(&picker->tree_ui_control->root);
	picker->tree_ui_control->root.contents = picker->display_data_root;
	picker->tree_ui_control->root.fillF = emPickerTreeNodeFill;
	picker->tree_ui_control->root.fillData = em_data.asset_picker.search_entry;
	picker->tree_ui_control->root.table = picker->display_parse_info_root;
	picker->tree_ui_control->root.tree = picker->tree_ui_control;
	ui_TreeNodeExpand(&picker->tree_ui_control->root);

	// create list
	picker->list_ui_control = emPickerListCreate(picker, true);
	ui_ListSetModel(picker->list_ui_control, NULL, &picker->list_model);
	if (has_texs)
	{
		picker->list_no_preview_ui_control = emPickerListCreate(picker, false);
		ui_ListSetModel(picker->list_no_preview_ui_control, NULL, &picker->list_model);
	}
}

/******
* This function displays an asset picker window for a given asset picker, allowing the user to
* select an existing asset
* PARAMS:
*   picker - EMPicker to show
*   action_name - string name of the action being performed; displayed on the confirm button; if NULL, then "Open"
*   allow_multi_select - bool indicating whether or not multiple selections can be made
*   callback - callback to invoke when data is chosen from the picker; if NULL, Editor Manager will
*              attempt to open data using selected_data_name and selected_data_type set on picker
*   data - data passed to the callback
******/
void emPickerShowEx(EMPicker *picker, char *action_name, bool allow_multi_select, EMPickerCallback callback, EMPickerClosedWindowCallback onClosedCallback, void *data)
{
	// initialize picker window
	if (!em_data.asset_picker.window)
	{
		UILabel *label;
		UIButton *button;
		UIComboBox *combo;
		char ***cb_model = calloc(1, sizeof(char**));

		em_data.asset_picker.window = ui_WindowCreate(picker->picker_name, 0, 0, 400, 600);
		elUICenterWindow(em_data.asset_picker.window);

		//Number of items selected
		label = ui_LabelCreate("", 5, 5);
		ui_WidgetSetPositionEx(UI_WIDGET(label), 5, 5, 0, 0, UITopRight);
		em_data.asset_picker.selected_cnt_lable = label;

		// tab group
		em_data.asset_picker.tab_group = ui_TabGroupCreate(0, 0, 0, 0);
		ui_WidgetSetDimensionsEx(UI_WIDGET(em_data.asset_picker.tab_group), 1, 1, UIUnitPercentage, UIUnitPercentage);
		em_data.asset_picker.tab_group->eStyle = UITabStyleFolders;
		em_data.asset_picker.tab_group->widget.topPad = em_data.asset_picker.tab_group->widget.leftPad = em_data.asset_picker.tab_group->widget.rightPad = 5;
		ui_WindowAddChild(em_data.asset_picker.window, em_data.asset_picker.tab_group);

		em_data.asset_picker.tree_tab = ui_TabCreate("Tree");
		ui_TabGroupAddTab(em_data.asset_picker.tab_group, em_data.asset_picker.tree_tab);
		em_data.asset_picker.list_tab = ui_TabCreate("List");
		ui_TabGroupAddTab(em_data.asset_picker.tab_group, em_data.asset_picker.list_tab);
		em_data.asset_picker.no_preview_tab = ui_TabCreate("No Preview");
		// no_preview_tab is added and removed dynamically 

		ui_TabGroupSetActive(em_data.asset_picker.tab_group, em_data.asset_picker.tree_tab);
		ui_TabGroupSetChangedCallback(em_data.asset_picker.tab_group, emPickerTabChange, NULL); // Set after choosing active tab

		label = ui_LabelCreate("Preview Size:", 5, 5);
		ui_WidgetSetPositionEx(UI_WIDGET(label), 5, 5, 0, 0, UIBottomLeft);
		em_data.asset_picker.tex_size_lable = label;
		combo = ui_ComboBoxCreateWithEnum(0, 0, 100, EMPreviewTexSizeEnum, NULL, NULL);
		ui_WidgetSetPositionEx(UI_WIDGET(combo), 90, 5, 0, 0, UIBottomLeft);
		em_data.asset_picker.tex_size_combo = combo;

		button = ui_ButtonCreate("Open", 5, 5, emPickerAction, em_data.asset_picker.search_entry);
		button->widget.offsetFrom = UIBottomRight;
		button->widget.width = 80;
		ui_WindowAddChild(em_data.asset_picker.window, button);
		em_data.asset_picker.action_button = button;

		button = ui_ButtonCreate("Cancel", 0, button->widget.y, emPickerCancel, em_data.asset_picker.search_entry);
		button->widget.offsetFrom = UIBottomRight;
		button->widget.width = 80;
		ui_WindowAddChild(em_data.asset_picker.window, button);
		em_data.asset_picker.cancel_button = button;

		label = ui_LabelCreate("Filter", 5, elUINextY(button) + 5);
		label->widget.offsetFrom = UIBottomLeft;
		ui_WindowAddChild(em_data.asset_picker.window, label);
		em_data.asset_picker.filter_label = label;

		em_data.asset_picker.filter_combo = ui_ComboBoxCreate(elUINextX(label) + 5, label->widget.y, 100, parse_EMPickerFilter, NULL, "display_text");
		em_data.asset_picker.filter_combo->widget.offsetFrom = UIBottomLeft;
		ui_ComboBoxSetSelectedCallback(em_data.asset_picker.filter_combo, emPickerFilterSelected, NULL);

		em_data.asset_picker.search_entry = ui_TextEntryCreate("", 0, label->widget.y);
		em_data.asset_picker.search_entry->widget.offsetFrom = UIBottomLeft;
		ui_WidgetSetWidthEx(UI_WIDGET(em_data.asset_picker.search_entry), 1.0, UIUnitPercentage);
		em_data.asset_picker.search_entry->widget.rightPad = 5;
		ui_TextEntrySetChangedCallback(em_data.asset_picker.search_entry, emPickerTextSearch, em_data.asset_picker.search_entry);
		ui_WindowAddChild(em_data.asset_picker.window, em_data.asset_picker.search_entry);

		em_data.asset_picker.tab_group->widget.bottomPad = elUINextY(label) + 5;
	}

	// ensure picker is valid and initialized
	emPickerInit(picker);
	if (!emPickerValidate(picker))
		return;

	// remove the old tree and list
	ui_TabRemoveChild(em_data.asset_picker.tree_tab, em_data.asset_picker.current_tree);
	ui_TabRemoveChild(em_data.asset_picker.list_tab, em_data.asset_picker.current_list);
	ui_TabRemoveChild(em_data.asset_picker.no_preview_tab, em_data.asset_picker.current_no_preview_list);
	
	// call the enter function if available so picker can load the tree data
	if (picker->enter_func)
		picker->enter_func(picker);

	// add the new tree and list
	em_data.asset_picker.current_tree = picker->tree_ui_control;
	ui_TabAddChild(em_data.asset_picker.tree_tab, picker->tree_ui_control);
	em_data.asset_picker.current_list = picker->list_ui_control;
	ui_TabAddChild(em_data.asset_picker.list_tab, picker->list_ui_control);
	if (picker->list_no_preview_ui_control)
	{
		em_data.asset_picker.current_no_preview_list = picker->list_no_preview_ui_control;
		ui_TabAddChild(em_data.asset_picker.no_preview_tab, picker->list_no_preview_ui_control);
		ui_TabGroupAddTab(em_data.asset_picker.tab_group, em_data.asset_picker.no_preview_tab);
	}
	else
	{
		em_data.asset_picker.current_no_preview_list = NULL;
		ui_TabGroupRemoveTab(em_data.asset_picker.tab_group, em_data.asset_picker.no_preview_tab);
	}

	ui_WindowRemoveChild(em_data.asset_picker.window, em_data.asset_picker.selected_cnt_lable);

	// show or hide texture size selector
	{
		int i;
		bool uses_textures = false;
		ui_WindowRemoveChild(em_data.asset_picker.window, em_data.asset_picker.tex_size_lable);
		ui_WindowRemoveChild(em_data.asset_picker.window, em_data.asset_picker.tex_size_combo);
		for ( i=0; i < eaSize(&picker->display_types); i++ ) {
			EMPickerDisplayType *display_type = picker->display_types[i];
			if(display_type->tex_func) {
				uses_textures = true;
				break;
			}
		}
		if(uses_textures) {
			ui_WindowAddChild(em_data.asset_picker.window, em_data.asset_picker.tex_size_lable);
			ui_WindowAddChild(em_data.asset_picker.window, em_data.asset_picker.tex_size_combo);
			ui_ComboBoxSetSelectedEnumCallback(em_data.asset_picker.tex_size_combo, emPickerTexSizeChange, picker);
			ui_ComboBoxSetSelectedEnumAndCallback(em_data.asset_picker.tex_size_combo, EditorPrefGetInt(picker->picker_name, "Picker", "TexSize", EMPreviewTex_Small));
		}
	}

	// set filters on combo box
	ui_WindowRemoveChild(em_data.asset_picker.window, em_data.asset_picker.filter_combo);
	em_data.asset_picker.filter_combo->selectedData = picker;
	if (eaSize(&picker->filters) > 0)
	{
		char *default_filter;
		int i;

		// create "All" filter if necessary
		if (!em_data.asset_picker.all_filter)
		{
			em_data.asset_picker.all_filter = StructCreate(parse_EMPickerFilter);
			em_data.asset_picker.all_filter->display_text = StructAllocString("All");
			em_data.asset_picker.all_filter->checkF = NULL;
		}

		// add "All" filter, if not already there
		if (eaFind(&picker->filters, em_data.asset_picker.all_filter) == -1)
			eaPush(&picker->filters, em_data.asset_picker.all_filter);

		// add the combo box to the window
		ui_ComboBoxSetModel(em_data.asset_picker.filter_combo, parse_EMPickerFilter, &picker->filters);
		ui_WindowAddChild(em_data.asset_picker.window, em_data.asset_picker.filter_combo);
		em_data.asset_picker.search_entry->widget.leftPad = elUINextX(em_data.asset_picker.filter_combo) + 5;

		// set combo box selection from preference
		strdup_alloca(default_filter, EditorPrefGetString(picker->picker_name, "Picker", "Filter Selection", "All"));
		ui_ComboBoxSetSelectedObjectAndCallback(em_data.asset_picker.filter_combo, em_data.asset_picker.all_filter);
		for (i = 0; i < eaSize(&picker->filters); i++)
		{
			if (strcmpi(picker->filters[i]->display_text, default_filter) == 0)
				ui_ComboBoxSetSelectedAndCallback(em_data.asset_picker.filter_combo, i);
		}
	}
	else
	{
		em_data.asset_picker.search_entry->widget.leftPad = elUINextX(em_data.asset_picker.filter_label) + 5;
		ui_ComboBoxSetSelectedAndCallback(em_data.asset_picker.filter_combo, -1);
	}
	assert( !em_data.asset_picker.callback
			&& !em_data.asset_picker.callback_data );
	em_data.asset_picker.callback = callback;
	em_data.asset_picker.closedCallback = onClosedCallback;
	em_data.asset_picker.callback_data = data;

	// set the action text
	em_data.asset_picker.cancel_button->widget.x = 0;
	ui_ButtonSetTextAndResize(em_data.asset_picker.action_button, action_name ? action_name : "Open");
	if (em_data.asset_picker.action_button->widget.width < 80) 
		em_data.asset_picker.action_button->widget.width = 80;
	em_data.asset_picker.action_button->widget.x = elUINextX(em_data.asset_picker.cancel_button) + 5;

	// Set up multi-select
	ui_TreeSetMultiselect(picker->tree_ui_control, allow_multi_select);
	ui_ListSetMultiselect(em_data.asset_picker.current_list, allow_multi_select);

	// Set window title
	ui_WindowSetTitle(em_data.asset_picker.window, picker->picker_name);

	// Apply pref on which tab to show
	ui_TabGroupSetActiveIndex(em_data.asset_picker.tab_group, EditorPrefGetInt(picker->picker_name, "Picker", "Active Tab", 0));

	// Apply pref on filter(search) string
	ui_TextEntrySetTextAndCallback(em_data.asset_picker.search_entry, EditorPrefGetString(picker->picker_name, "Picker", "Filter Text", ""));

	// TODO: Apply pref on combo choice

	// Get window position from preferences
	EditorPrefGetWindowPosition(picker->picker_name, "Picker", "Window Position", em_data.asset_picker.window);

	// Clear selections
	emPickerClearSelections();

	// Refresh data in the picker
	emPickerRefresh(picker);

	ui_WindowSetModal(em_data.asset_picker.window, true);
	ui_WindowSetCloseCallback(em_data.asset_picker.window, emPickerClosed, picker);
	ui_WidgetSetFamily(UI_WIDGET(em_data.asset_picker.window), UI_FAMILY_EDITOR);
	ui_WindowShow(em_data.asset_picker.window);
	ui_SetFocus(em_data.asset_picker.search_entry);
}

static bool emPickerObjectAcceptsType( ZoneMapEncounterObjectInfo* object, EMEncounterObjectFilterType type )
{
	if (type == EncObj_Any)
		return true;

	if( object->interactType == WL_ENC_CONTACT ) {
		return (type == EncObj_Contact);
	}
	
	switch( object->type ) {
		case WL_ENC_INTERACTABLE:
			switch( object->interactType ) {
				case WL_ENC_DOOR: return (type == EncObj_Door || type == EncObj_Usable_As_Warp);
				case WL_ENC_UGC_OPEN_DOOR: return (type == EncObj_Door);
				case WL_ENC_CLICKIE: return (type == EncObj_Clickie || type == EncObj_Usable_As_Warp);
				case WL_ENC_DESTRUCTIBLE: return (type == EncObj_Destructible);
				case WL_ENC_REWARD_BOX: return (type == EncObj_Reward_Box);
			}

		xcase WL_ENC_ENCOUNTER:
			return (type == EncObj_Encounter);
			
		xcase WL_ENC_NAMED_VOLUME:
			return (type == EncObj_Volume || type == EncObj_Usable_As_Warp);

		xcase WL_ENC_SPAWN_POINT:
			return (type == EncObj_Spawn);
	}

	return false;
}

static bool emPickerObjectMatchesActiveFilter( EMZeniObjectPicker* picker, const char* zmName, ZoneMapEncounterObjectInfo* object )
{
	bool isUsingWholeMap = eaiSize( &picker->filterValidTypes ) == 1 && picker->filterValidTypes[ 0 ] == EncObj_WholeMap;
	
	if( picker->filterFn && !picker->filterFn( zmName, object, picker->filterData )) {
		return false;
	}

	if( ea32Size( &picker->filterValidTypes ) == 0 || isUsingWholeMap ) {
		return true;
	} else {
		int i;
		for (i = ea32Size( &picker->filterValidTypes )-1; i >= 0; --i)
		{
			if (emPickerObjectAcceptsType( object, picker->filterValidTypes[ i ]))
			{
				return true;
			}
		}

		return false;
	}
}

static const char* emPickerZoneMapEncounterObjectIcon( ZoneMapEncounterObjectInfo* object )
{
	switch( object->type ) {
		xcase WL_ENC_CONTACT:
			return "ugc_icon_contact";
			
		xcase WL_ENC_INTERACTABLE:
			switch( object->interactType ) {
				xcase WL_ENC_DOOR: case WL_ENC_UGC_OPEN_DOOR:
					return "ugc_icon_door";
				xcase WL_ENC_REWARD_BOX:
					return "MapIcon_Treasurechest_01";
				xcase WL_ENC_CLICKIE:
					return "ugc_icon_clickie";
			}
		xcase WL_ENC_ENCOUNTER:
			return "ugc_icon_encounter";
		xcase WL_ENC_NAMED_VOLUME:
			return "ugc_icon_marker";
		xcase WL_ENC_SPAWN_POINT:
			return "ugc_icon_spawn";
	}

	return "ugc_icon_object";
}

static int emPickerZoneMapEncounterObjectCompareObjects( const ZoneMapEncounterObjectInfo** obj1, const ZoneMapEncounterObjectInfo** obj2 )
{
	const char* name1 = NULL;
	const char* name2 = NULL;

	if( g_ui_State.bInUGCEditor ) {
		name1 = TranslateMessageRef( (*obj1)->displayName );
		name1 = TranslateMessageRef( (*obj2)->displayName );
	}

	if( !name1 ) {
		name1 = (*obj1)->logicalName;
	}
	if( !name2 ) {
		name2 = (*obj2)->logicalName;
	}

	return stricmp_safe( name1, name2 );
}

static int emPickerZoneMapEncounterObjectCompareZenis( const ZoneMapEncounterInfo** zeni1, const ZoneMapEncounterInfo** zeni2 )
{
	ZoneMapInfo* zminfo1 = zmapInfoGetByPublicName( (*zeni1)->map_name );
	ZoneMapInfo* zminfo2 = zmapInfoGetByPublicName( (*zeni2)->map_name );
	const char* name1 = NULL;
	const char* name2 = NULL;

	if( g_ui_State.bInUGCEditor ) {
		name1 = TranslateMessagePtr( zmapInfoGetDisplayNameMessagePtr( zminfo1 ));
		name2 = TranslateMessagePtr( zmapInfoGetDisplayNameMessagePtr( zminfo2 ));
	}

	if( !name1 ) {
		name1 = (*zeni1)->map_name;
	}
	if( !name2 ) {
		name2 = (*zeni2)->map_name;
	}

	return stricmp_safe( name1, name2 );
}

static void emPickerZoneMapEncounterObjectMaybeAddZeni( EMZeniObjectPicker* picker, ZoneMapEncounterInfo* zeni )
{
	ZoneMapEncounterInfo* zeniClone = StructClone( parse_ZoneMapEncounterInfo, zeni );
	int it;
	bool isUsingWholeMap = eaiSize( &picker->filterValidTypes ) == 1 && picker->filterValidTypes[ 0 ] == EncObj_WholeMap;

	assert( zeniClone );
	for( it = 0; it != eaSize( &zeniClone->objects ); ++it ) {
		ZoneMapEncounterObjectInfo* zeniObj = zeniClone->objects[ it ];
			
		if( !emPickerObjectMatchesActiveFilter( picker, zeni->map_name, zeniObj )) {
			StructDestroy( parse_ZoneMapEncounterObjectInfo, zeniObj );
			eaRemove( &zeniClone->objects, it );
			--it;
		}
	}
		
	if( eaSize( &zeniClone->objects ) == 0 ) {
		StructDestroySafe( parse_ZoneMapEncounterInfo, &zeniClone );
	} else if( isUsingWholeMap ) {
		eaDestroyStruct( &zeniClone->objects, parse_ZoneMapEncounterObjectInfo );
	}


	if( zeniClone ) {
		eaQSort( zeniClone->objects, emPickerZoneMapEncounterObjectCompareObjects );
		eaPush( &picker->zenis, zeniClone );
	}
}

static void emPickerZoneMapEncounterObjectRefreshUI( EMZeniObjectPicker* picker, bool selectionMakeVisible, const char* forceSetZmap, const char* forceSetObject, bool* out_setFound )
{
	ZoneMapEncounterInfo* selectedZeni = NULL;
	ZoneMapEncounterObjectInfo* selectedObject = NULL;

	if( out_setFound ) {
		*out_setFound = false;
	}

	// Refresh the tree
	eaDestroyStruct( &picker->zenis, parse_ZoneMapEncounterInfo );

	if( eaSize( &picker->ugcInfoOverrides )) {
		int it;
		for( it = 0; it != eaSize( &picker->ugcInfoOverrides ); ++it ) {
			emPickerZoneMapEncounterObjectMaybeAddZeni( picker, picker->ugcInfoOverrides[ it ]);
		}
	} else {
		FOR_EACH_IN_REFDICT( "ZoneMapEncounterInfo", ZoneMapEncounterInfo, zeni ) {
			emPickerZoneMapEncounterObjectMaybeAddZeni( picker, zeni );
		} FOR_EACH_END;
	}
	
	eaQSort( picker->zenis, emPickerZoneMapEncounterObjectCompareZenis );
	ui_TreeRefresh( picker->pickerTree );

	if( forceSetZmap ) {
		int zeniIt;
		int objIt;
		for( zeniIt = 0; zeniIt != eaSize( &picker->zenis ); ++zeniIt ) {
			ZoneMapEncounterInfo* zeni = picker->zenis[ zeniIt ];

			if (resNamespaceBaseNameEq( forceSetZmap, zeni->map_name)) {
				if( forceSetObject ) {
					for( objIt = 0; objIt != eaSize( &zeni->objects ); ++objIt ) {
						ZoneMapEncounterObjectInfo* object = zeni->objects[ objIt ];

						if( stricmp( forceSetObject, object->logicalName ) == 0 ) {
							selectedZeni = zeni;
							selectedObject = object;
							break;
						}
					}
				} else {
					selectedZeni = zeni;
					selectedObject = NULL;
				}
				break;
			}
		}
		
		if( selectedZeni ) {
			void* selectPath[ 3 ] = { 0 };
			selectPath[ 0 ] = selectedZeni;
			selectPath[ 1 ] = selectedObject;
			ui_TreeExpandAndSelect( picker->pickerTree, selectPath, false );

			if( out_setFound ) {
				*out_setFound = true;
			}
		}
	} else {
		UITreeNode* selectedNode = ui_TreeGetSelected( picker->pickerTree );
		
		if( selectedNode && selectedNode->table == parse_ZoneMapEncounterObjectInfo ) {
			selectedObject = selectedNode->contents;
		}
		while( selectedNode && selectedNode->table != parse_ZoneMapEncounterInfo ) {
			selectedNode = ui_TreeNodeFindParent( picker->pickerTree, selectedNode );
		}
		if( selectedNode ) {
			selectedZeni = selectedNode->contents;
		}
	}

	if( !selectedZeni && eaSize( &picker->zenis )) {
		void* selectPath[ 2 ] = { 0 };
		
		selectedZeni = picker->zenis[ 0 ];
		selectPath[ 0 ] = selectedZeni;
		ui_TreeExpandAndSelect( picker->pickerTree, selectPath, false );
	}

	// Refresh the minimap
	{
		bool mapChanged = false;
		ui_WidgetRemoveFromGroup( picker->minimapCustomWidget );
		picker->minimapCustomWidget = NULL;
		if( !selectedZeni ) {
			mapChanged = ui_MinimapSetMap( picker->minimap, NULL );
		} else if( picker->ugcInfoOverrides ) {
			ui_MinimapSetMapInfo( picker->minimap, selectedZeni );
			ui_MinimapSetScale( picker->minimap, selectedZeni->ugc_map_scale );
			if( selectedZeni->ugc_picker_widget ) {
				ui_ScrollAreaAddChild( picker->minimapArea, selectedZeni->ugc_picker_widget );
				picker->minimapCustomWidget = selectedZeni->ugc_picker_widget;
			}
		} else {
			mapChanged = ui_MinimapSetMap( picker->minimap, selectedZeni->map_name );
		}

		ui_MinimapClearObjects( picker->minimap );
		if( selectedZeni ) {
			int it;
			for( it = 0; it != eaSize( &selectedZeni->objects ); ++it ) {
				ZoneMapEncounterObjectInfo* object = selectedZeni->objects[ it ];
				const char* displayName = TranslateMessageRef( object->displayName );
				if( !displayName ) {
					displayName = object->ugcDisplayName;
				}
				if( !displayName ) {
					displayName = object->logicalName;
				}
			
				ui_MinimapAddObject( picker->minimap, object->pos,
									 displayName, emPickerZoneMapEncounterObjectIcon( object ), object );
				if( selectedObject == object ) {
					ui_MinimapSetSelectedObject( picker->minimap, object, false );
				}
			}
		}

		if( selectionMakeVisible ) {
			bool sane_default_scroll_pos = false;
			if( mapChanged ) {
				ui_ScrollAreaSetChildScale( picker->minimapArea, 0.0001 );
			}
			
			if( selectedObject ) {
				FOR_EACH_IN_EARRAY(picker->minimap->objects, UIMinimapObject, minimapObject) {
					if (minimapObject->data == selectedObject) {
						Vec2 objectPos;
						ui_MinimapGetObjectPos( picker->minimap, minimapObject, objectPos );
						ui_ScrollAreaScrollToPosition( picker->minimapArea, objectPos[0], objectPos[1] );
						picker->minimapArea->autoScrollCenter = true;
						sane_default_scroll_pos = true;
						break;
					}
				} FOR_EACH_END;
			}

			if(!sane_default_scroll_pos)
			{
				ui_ScrollAreaScrollToPosition( picker->minimapArea, picker->minimap->layout_size[0] / 2, picker->minimap->layout_size[1] / 2 );
				picker->minimapArea->autoScrollCenter = true;
			}
		}
	}

	// Refresh the details
	{
		ui_WidgetGroupQueueFreeAndRemove( &picker->selectedDetails->widget.children );
		
		if( selectedObject ) {
			UIScrollArea* detailsScroll = NULL;
			const char* selectedDisplayName = NULL;
			UITextEntry* headerLabel = NULL;
			UILabel* restrictionsLabel = NULL;
			UILabel* detailsLabel = NULL;
			const char *details_str;
			UILabel* debugLabel = NULL;
			int yIt = 0;

			selectedDisplayName = selectedObject->ugcDisplayName;
			if( !selectedDisplayName ) {
				selectedDisplayName = TranslateMessageRef( selectedObject->displayName );
			}

			detailsScroll = ui_ScrollAreaCreate( 0, yIt, 0, 0, 0, 0, false, true );
			ui_WidgetSetDimensionsEx( UI_WIDGET( detailsScroll ), 1, 1, UIUnitPercentage, UIUnitPercentage );
			detailsScroll->autosize = true;
			yIt = 0;

			{
				char* estrRestrictions = NULL;

				if( selectedObject->restrictions.iMinLevel || selectedObject->restrictions.iMaxLevel ) {
					if( selectedObject->restrictions.iMinLevel && selectedObject->restrictions.iMaxLevel ) {
						estrConcatf( &estrRestrictions, "%s Level %d - %d",
									 (!estrRestrictions ? "Requires:" : ","),
									 selectedObject->restrictions.iMinLevel, selectedObject->restrictions.iMaxLevel );
					} else if( selectedObject->restrictions.iMinLevel ) {
						estrConcatf( &estrRestrictions, "%s Level %d+",
									 (!estrRestrictions ? "Requires:" : ","),
									 selectedObject->restrictions.iMinLevel );
					} else if( selectedObject->restrictions.iMaxLevel ) {
						estrConcatf( &estrRestrictions, "%s Level %d-",
									 (!estrRestrictions ? "Requires:" : ","),
									 selectedObject->restrictions.iMaxLevel );
					}
				}

				if( eaSize( &selectedObject->restrictions.eaFactions )) {
					int it;
					estrConcatf( &estrRestrictions, "%s ",
								 (!estrRestrictions ? "Requires:" : ","));
					for( it = 0; it != eaSize( &selectedObject->restrictions.eaFactions ); ++it ) {
						const char* allegianceName = selectedObject->restrictions.eaFactions[ it ]->pcFaction;
						AllegianceDef* allegiance = allegiance_FindByName( allegianceName );
						const char* translated = (allegiance ? TranslateDisplayMessage( allegiance->displayNameMsg ) : NULL);

						if( translated ) {
							estrConcatf( &estrRestrictions, "%s%s",
										 (it == 0 ? "" : ", "), translated );
						} else {
							estrConcatf( &estrRestrictions, "%s%s [UNTRANSLATED!]",
										 (it == 0 ? "" : ", "), allegianceName );
						}
					}
					estrConcatf( &estrRestrictions, " only" );
				}

				if( estrRestrictions ) {
					restrictionsLabel = ui_LabelCreate( estrRestrictions, 0, yIt );
					yIt += 28;
				}
				estrDestroy( &estrRestrictions );
			}

			details_str = TranslateMessageRef( selectedObject->displayDetails );
			if (!details_str)
				details_str = selectedObject->ugcDisplayDetails;
			
			detailsLabel = ui_LabelCreate( details_str, 0, yIt );
			ui_LabelSetWordWrap( detailsLabel, true );
			ui_WidgetSetWidthEx( UI_WIDGET( detailsLabel ), 1.0, UIUnitPercentage );

			if( emZMShowDebugName ) {
				char debugBuffer[ 1024 ];
				sprintf( debugBuffer, "(%.2f, %.2f, %.2f)  %s",
						 selectedObject->pos[ 0 ], selectedObject->pos[ 1 ], selectedObject->pos[ 2 ], selectedObject->logicalName );
				debugLabel = ui_LabelCreate( debugBuffer, 0, 0 );
				ui_WidgetSetPositionEx( UI_WIDGET( debugLabel ), 0, 0, 0, 0, UITopRight );
			}

			ui_PaneAddChild( picker->selectedDetails, headerLabel );
			ui_PaneAddChild( picker->selectedDetails, detailsScroll );
			if( restrictionsLabel ) {
				ui_ScrollAreaAddChild( detailsScroll, restrictionsLabel );
			}
			ui_ScrollAreaAddChild( detailsScroll, detailsLabel );
			if( debugLabel ) {
				ui_PaneAddChild( picker->selectedDetails, debugLabel );
			}
		}
	}
}

static void emPickerZoneMapEncounterObjectClickFunc( UIMinimap* minimap, UserData rawPicker, UserData rawObject )
{
	EMZeniObjectPicker* picker = rawPicker;
	const ZoneMapEncounterObjectInfo* selectedObject = (ZoneMapEncounterObjectInfo*)rawObject;
	const char* selectedMapName = ui_MinimapGetMap( minimap );
	char* selectedObjectName = NULL;
	strdup_alloca( selectedObjectName, selectedObject->logicalName );

	emPickerZoneMapEncounterObjectRefreshUI( picker, false, selectedMapName, selectedObjectName, NULL );
}

static void emPickerZoneMapEncounterObjectSelect( UIWidget* ignored, UserData rawData )
{
	EMZoneMapEncounterObjectPickerWindow* data = rawData;

	if( data->cb ) {
		UITab* selectedTab = data->tabGroup ? ui_TabGroupGetActive( data->tabGroup ) : NULL;

		if( selectedTab == data->mapTab ) {
			const char* mapName = NULL;
			const char* logicalName = NULL;
			emZeniObjectWidgetGetSelection( data->rootWidget, &mapName, &logicalName );
			data->cb( mapName, logicalName, NULL, NULL, data->userData );
		} else if( selectedTab == data->projMapTab ) {
			const char* mapName = NULL;
			const char* logicalName = NULL;
			emZeniObjectWidgetGetSelection( data->projRootWidget, &mapName, &logicalName );
			data->cb( mapName, logicalName, NULL, NULL, data->userData );
		} else if( selectedTab == data->overworldMapTab ) {
			Vec2 vec = { 0, 0 };
			const char* icon = NULL;
			emOverworldMapWidgetGetSelection( data->overworldMapWidget, vec, &icon );
			data->cb( NULL, NULL, vec, icon, data->userData );
		}
	}

	ui_WidgetQueueFreeAndNull( &data->window );
	free( data );
}

static void emPickerZoneMapEncounterObjectClear( UIWidget* ignored, UserData rawData )
{
	EMZoneMapEncounterObjectPickerWindow* data = rawData;
	
	if( data->cb ) {
		data->cb( NULL, NULL, NULL, NULL, data->userData );
	}

	ui_WidgetForceQueueFree( UI_WIDGET( data->window ));
	free( data );
}

static bool emPickerZoneMapEncounterObjectClose( UIWidget* ignored, UserData rawData )
{
	emPickerZoneMapEncounterObjectClear( NULL, rawData );
	return true;
}

static bool emZeniObjectPickerFilter( const char* zmName, ZoneMapEncounterObjectInfo* object, EMZoneMapEncounterObjectPickerWindow* window )
{
	if( window->filterFn ) {
		return window->filterFn( zmName, object, window->filterData );
	} else {
		return true;
	}
}

bool emShowZeniObjectPicker(ZoneMapEncounterInfo **ugcInfoOverrides,
							EMEncounterObjectFilterType forceFilterType, const char** eaOverworldIconNames,
							const char* defaultZmap, const char* defaultObj,
							float* defaultOverworldPos, const char* defaultOverworldIcon,
							EMPickerZoneMapEncounterObjectFilterFn filterFn, UserData filterData,
							EMPickerZoneMapEncounterObjectCallback cb, UserData userData )
{
	EMZoneMapEncounterObjectPickerWindow* data = calloc( 1, sizeof( *data ));
	UIButton* okButton;
	UIButton* cancelButton;
	bool defaultOnCrypticMap = false;
	
	data->defaultZmap = allocAddString( defaultZmap );
	data->filterFn = filterFn;
	data->filterData = filterData;

	if( forceFilterType == EncObj_WholeMap ) {
		data->window = ui_WindowCreate( "Select Map", 0, 0, 900, 700 );
	} else {
		data->window = ui_WindowCreate( "Select Object", 0, 0, 900, 700 );
	}
	ui_WindowSetCloseCallback( data->window, emPickerZoneMapEncounterObjectClose, data );

	data->tabGroup = ui_TabGroupCreate( 0, 0, 1, 1 );
	ui_WidgetSetDimensionsEx(UI_WIDGET(data->tabGroup), 1, 1, UIUnitPercentage, UIUnitPercentage);
	ui_WindowAddChild(data->window, data->tabGroup);


	if( ugcInfoOverrides ) {
		data->projRootWidget = emZeniObjectWidgetCreate( NULL, ugcInfoOverrides, forceFilterType, defaultZmap, defaultObj, emZeniObjectPickerFilter, data );
	}
	data->rootWidget = emZeniObjectWidgetCreate( &defaultOnCrypticMap, NULL, forceFilterType, defaultZmap, defaultObj, emZeniObjectPickerFilter, data );
	if( eaOverworldIconNames ) {
		data->overworldMapWidget = emOverworldMapWidgetCreate( eaOverworldIconNames, defaultOverworldPos, defaultOverworldIcon, NULL, NULL );
	}

	if (!data->rootWidget && !data->projRootWidget)
	{
		return false;
	}

	
	if (data->projRootWidget) {
		data->projMapTab = ui_TabCreate( "Project Maps" );
		ui_TabGroupAddTab( data->tabGroup, data->projMapTab );
		ui_TabAddChild(data->projMapTab, data->projRootWidget);
	}
	if (data->rootWidget) {
		data->mapTab = ui_TabCreate( "Cryptic Maps" );
		ui_TabGroupAddTab( data->tabGroup, data->mapTab );
		ui_TabAddChild(data->mapTab, data->rootWidget);
	}
	if( data->overworldMapWidget ) {
		data->overworldMapTab = ui_TabCreate( "Overworld Map" );
		ui_TabGroupAddTab( data->tabGroup, data->overworldMapTab );
		ui_TabAddChild( data->overworldMapTab, data->overworldMapWidget );
	}

	if( defaultOverworldPos || defaultOverworldIcon ) {
		ui_TabGroupSetActive( data->tabGroup, data->overworldMapTab );
	} else if( defaultOnCrypticMap || !data->projRootWidget ) {
		ui_TabGroupSetActive( data->tabGroup, data->mapTab );
	} else {
		ui_TabGroupSetActive( data->tabGroup, data->projMapTab );
	}
	
	okButton = ui_ButtonCreate( "OK", 0, 0, emPickerZoneMapEncounterObjectSelect, data );
	ui_WidgetSetPositionEx( UI_WIDGET( okButton ), 90, 0, 0, 0, UIBottomRight );
	ui_WidgetSetWidth( UI_WIDGET( okButton ), 80 );

	cancelButton = ui_ButtonCreate( "Cancel", 0, 0, emPickerZoneMapEncounterObjectClear, data );
	ui_WidgetSetPositionEx( UI_WIDGET( cancelButton ), 0, 0, 0, 0, UIBottomRight );
	ui_WidgetSetWidth( UI_WIDGET( cancelButton ), 80 );
	data->tabGroup->widget.bottomPad = UI_WIDGET(cancelButton)->height+5;
	
	ui_WindowSetDimensions( data->window, 900, 700, 450, 350 );
	elUICenterWindow( data->window );
	if( g_ui_State.bInUGCEditor ) {
		ui_WindowSetModal( data->window, true );
	}

	ui_WindowAddChild( data->window, okButton );
	ui_WindowAddChild( data->window, cancelButton );
	ui_WindowShow( data->window );

	data->cb = cb;
	data->userData = userData;

	return true;
}

static void emPickerOverworldMapSelect( UIWidget* ignored, UserData rawData )
{
	EMOverworldMapPickerWindow* data = rawData;
	Vec2 vec = { 0, 0 };
	const char* icon = NULL;
	emOverworldMapWidgetGetSelection( data->overworldMapWidget, vec, &icon );
	data->cb( NULL, NULL, vec, icon, data->userData );
	ui_WidgetQueueFreeAndNull( &data->window );
	free( data );
}

static void emPickerOverworldMapClear( UIWidget* ignored, UserData rawData )
{
	EMOverworldMapPickerWindow* data = rawData;
	
	if( data->cb ) {
		data->cb( NULL, NULL, NULL, NULL, data->userData );
	}

	ui_WidgetForceQueueFree( UI_WIDGET( data->window ));
	free( data );
}

static bool emPickerOverworldMapClose( UIWidget* ignored, UserData rawData )
{
	emPickerOverworldMapClear( NULL, rawData );
	return true;
}

static void emPickerOverworldMapChanged( EMOverworldMapPickerWindow* data )
{
	Vec2 vec = { 0, 0 };
	const char* icon = NULL;
	emOverworldMapWidgetGetSelection( data->overworldMapWidget, vec, &icon );

	if( vec[ 0 ] >= 0 && vec[ 1 ] >= 0 && icon ) {
		ui_SetActive( UI_WIDGET( data->okButton ), true );
	} else {
		ui_SetActive( UI_WIDGET( data->okButton ), false );
	}
}

void emShowOverworldMapPicker(const char** eaIconNames, float* defaultPos, const char* defaultIcon,
							  EMPickerZoneMapEncounterObjectCallback cb, UserData userData )
{
	EMOverworldMapPickerWindow* data = calloc( 1, sizeof( *data ));
	UIButton* cancelButton;

	data->window = ui_WindowCreate( "Select Location", 0, 0, 900, 700 );
	ui_WindowSetCloseCallback( data->window, emPickerOverworldMapClose, data );
	data->overworldMapWidget = emOverworldMapWidgetCreate( eaIconNames, defaultPos, defaultIcon, emPickerOverworldMapChanged, data );
	ui_WindowAddChild( data->window, data->overworldMapWidget );
	assert( data->overworldMapWidget );
	
	data->okButton = ui_ButtonCreate( "OK", 0, 0, emPickerOverworldMapSelect, data );
	ui_WidgetSetPositionEx( UI_WIDGET( data->okButton ), 90, 0, 0, 0, UIBottomRight );
	ui_WidgetSetWidth( UI_WIDGET( data->okButton ), 80 );

	cancelButton = ui_ButtonCreate( "Cancel", 0, 0, emPickerOverworldMapClear, data );
	ui_WidgetSetPositionEx( UI_WIDGET( cancelButton ), 0, 0, 0, 0, UIBottomRight );
	ui_WidgetSetWidth( UI_WIDGET( cancelButton ), 80 );
	data->overworldMapWidget->bottomPad = UI_WIDGET(cancelButton)->height+5;
	
	ui_WindowSetDimensions( data->window, 900, 700, 450, 350 );
	elUICenterWindow( data->window );
	ui_WindowSetModal( data->window, true );

	ui_WindowAddChild( data->window, data->okButton );
	ui_WindowAddChild( data->window, cancelButton );
	ui_WindowShow( data->window );

	data->cb = cb;
	data->userData = userData;

	emPickerOverworldMapChanged( data );
}


static void emPickerZoneMapEncounterToggleFilter( UICheckButton* checkButton, UserData rawPicker )
{
	EMZeniObjectPicker* picker = rawPicker;
	EMEncounterObjectFilterType value = checkButton->widget.u64;

	if( ui_CheckButtonGetState( checkButton )) {
		ea32PushUnique( &picker->filterValidTypes, value );
	} else {
		while( ea32FindAndRemove( &picker->filterValidTypes, value ) >= 0 ) {}
	}

	emPickerZoneMapEncounterObjectRefreshUI( picker, false, NULL, NULL, NULL );
}

static void emPickerZoneMapEncounterObjectPickerTreeSelected( UITreeNode* node, UserData rawPicker )
{
	EMZeniObjectPicker* picker = rawPicker;
	emPickerZoneMapEncounterObjectRefreshUI( picker, true, NULL, NULL, NULL );
}

static void emPickerZoneMapEncounterObjectDisplayObject( UITreeNode* node, UserData ignored, UI_MY_ARGS, F32 z )
{
	UITree* tree = node->tree;
	ZoneMapEncounterObjectInfo* zeniInfo = node->contents;
	CBox box = { x, y, x + w, y + h };
	UIStyleFont* font = GET_REF( UI_GET_SKIN(tree)->hNormal );

	clipperPushRestrict( &box );
	ui_StyleFontUse( font, false, UI_WIDGET( tree )->state );
	{
		const char* name;

		if( g_ui_State.bInUGCEditor ) {
			name = TranslateMessageRef( zeniInfo->displayName );
		} else {
			name = zeniInfo->logicalName;
		}
		if( !name ) {
			name = zeniInfo->ugcDisplayName;
		}
		
		if( name ) {
			gfxfont_Printf( x, y + h / 2, z, scale, scale, CENTER_Y, "%s", name );
		} else {
			gfxfont_Printf( x, y + h / 2, z, scale, scale, CENTER_Y, "%s [UNTRANSLATED]", zeniInfo->logicalName );
		}
	}
	clipperPop();
}

static void emPickerZoneMapEncounterObjectFillMap( UITreeNode* mapNode, UserData ignored )
{
	ZoneMapEncounterInfo* zmEncInfo = mapNode->contents;
	int it;

	for( it = 0; it != eaSize( &zmEncInfo->objects ); ++it ) {
		ZoneMapEncounterObjectInfo* zeniInfo = zmEncInfo->objects[ it ];
		
		UITreeNode* node = ui_TreeNodeCreate( mapNode->tree, hashString( zeniInfo->logicalName, false ), parse_ZoneMapEncounterObjectInfo, zeniInfo,
											  NULL, NULL, emPickerZoneMapEncounterObjectDisplayObject, NULL,
											  20 );
		ui_TreeNodeAddChild( mapNode, node );
	}
}

static void emPickerZoneMapEncounterObjectDisplayMap( UITreeNode* node, UserData ignored, UI_MY_ARGS, F32 z )
{
	UITree* tree = node->tree;
	ZoneMapEncounterInfo* zmEncInfo = node->contents;
	ZoneMapInfo* zminfo = zmapInfoGetByPublicName( zmEncInfo->map_name );
	CBox box = { x, y, x + w, y + h };
	UIStyleFont* font = GET_REF( UI_GET_SKIN(tree)->hNormal );

	clipperPushRestrict( &box );
	ui_StyleFontUse( font, false, UI_WIDGET( tree )->state );
	{
		const char* name;

		if( g_ui_State.bInUGCEditor ) {
			name = TranslateMessagePtr( zmapInfoGetDisplayNameMessagePtr( zminfo ));
		} else {
			name = zmEncInfo->map_name;
		}
		
		if( !name ) {
			name = zmEncInfo->ugc_display_name;
		}
		
		if( name ) {
			gfxfont_Printf( x, y + h / 2, z, scale, scale, CENTER_Y, "%s", name );
		} else {
			gfxfont_Printf( x, y + h / 2, z, scale, scale, CENTER_Y, "%s [UNTRANSLATED]", zmEncInfo->map_name );
		}
	}
	clipperPop();
}

static void emPickerZoneMapEncounterObjectFillRoot( UITreeNode* rootNode, UserData rawPicker )
{
	EMZeniObjectPicker* picker = rawPicker;
	int it;
	for( it = 0; it != eaSize( &picker->zenis ); ++it ) {
		ZoneMapEncounterInfo* zeni = picker->zenis[ it ];
		UITreeNode* node = ui_TreeNodeCreate( rootNode->tree, hashString( zeni->map_name, false ), parse_ZoneMapEncounterInfo, zeni,
											  (eaSize( &zeni->objects ) ? emPickerZoneMapEncounterObjectFillMap : NULL),
											  NULL, emPickerZoneMapEncounterObjectDisplayMap, NULL,
											  20 );
		ui_TreeNodeAddChild( rootNode, node );
	}
}

static void emZeniObjectWidgetFree( UIWidget* widget )
{
	EMZeniObjectPicker* picker = (EMZeniObjectPicker*)(widget->u64);
	
	ui_PaneFreeInternal( (UIPane*)widget );
	eaDestroyStruct( &picker->ugcInfoOverrides, parse_ZoneMapEncounterInfo );
	ea32Destroy( &picker->filterValidTypes );
	eaDestroyStruct( &picker->zenis, parse_ZoneMapEncounterInfo );
	free( picker );
}

UIWidget* emZeniObjectWidgetCreate(bool* out_selectedDefault, ZoneMapEncounterInfo **ugcInfoOverrides,
								   EMEncounterObjectFilterType forceFilterType,
								   const char* defaultZmap, const char* defaultObj,
								   EMPickerZoneMapEncounterObjectFilterFn filterFn, UserData filterData)
{
	EMZeniObjectPicker* pickerAccum = calloc( 1, sizeof( *pickerAccum ));
	
	{
		UIPane* rootPane;
		UITree* pickerTree;
				
		UIScrollArea* minimapArea;
		UIMinimap* minimap;
		UIPane* selectedDetails;

		rootPane = ui_PaneCreate( 0, 0, 1, 1, UIUnitPercentage, UIUnitPercentage, 0 );
		rootPane->invisible = true;

		if( forceFilterType ) {
			ea32Push( &pickerAccum->filterValidTypes, forceFilterType );
		}

		// Initialize the tree to check if we have any matching maps
		pickerTree = ui_TreeCreate( 0, (!forceFilterType ? STANDARD_ROW_HEIGHT : 0), 300, 1 );
		ui_WidgetSetHeightEx( UI_WIDGET( pickerTree ), 1, UIUnitPercentage );
		ui_TreeNodeSetFillCallback( &pickerTree->root, emPickerZoneMapEncounterObjectFillRoot, pickerAccum );
		ui_TreeSetSelectedCallback( pickerTree, emPickerZoneMapEncounterObjectPickerTreeSelected, pickerAccum );
		ui_TreeNodeExpand( &pickerTree->root );
		ui_PaneAddChild( rootPane, pickerTree );

		minimapArea = ui_ScrollAreaCreate( 0, 0, 0, 0, 0, 0, true, true );
		UI_WIDGET(minimapArea)->sb->scrollBoundsX = UIScrollBounds_KeepContentsAtViewCenter;
		UI_WIDGET(minimapArea)->sb->scrollBoundsY = UIScrollBounds_KeepContentsAtViewCenter;
		ui_ScrollAreaSetNoCtrlDraggable( minimapArea, true );
		ui_ScrollAreaSetZoomSlider( minimapArea, true );
		ui_WidgetSetDimensionsEx( UI_WIDGET( minimapArea ), 1, 1, UIUnitPercentage, UIUnitPercentage );
		ui_WidgetSetPaddingEx( UI_WIDGET( minimapArea ), 304, 0, (!forceFilterType ? STANDARD_ROW_HEIGHT : 0), 164 );
		minimapArea->autosize = true;
		ui_PaneAddChild( rootPane, minimapArea );

		minimap = ui_MinimapCreate();
		ui_MinimapSetObjectClickCallback( minimap, emPickerZoneMapEncounterObjectClickFunc, pickerAccum );
		ui_WidgetSetPositionEx( UI_WIDGET( minimap ), 0, 0, 0, 0, UITopLeft );
		minimap->autosize = true;
		minimap->widget.priority = 5;
		ui_ScrollAreaAddChild( minimapArea, minimap );

		selectedDetails = ui_PaneCreate( 0, 0, 1, 160, UIUnitPercentage, UIUnitFixed, 0 );
		ui_WidgetSetPositionEx( UI_WIDGET( selectedDetails ), 304, 0, 0, 0, UIBottomLeft );
		ui_PaneAddChild( rootPane, selectedDetails );

		pickerAccum->rootPane = rootPane;
		pickerAccum->pickerTree = pickerTree;
		pickerAccum->minimapArea = minimapArea;
		pickerAccum->minimap = minimap;
		pickerAccum->selectedDetails = selectedDetails;
	}

	pickerAccum->filterFn = filterFn;
	pickerAccum->filterData = filterData;
	eaCopyStructs( &ugcInfoOverrides, &pickerAccum->ugcInfoOverrides, parse_ZoneMapEncounterInfo );
	
	emPickerZoneMapEncounterObjectRefreshUI( pickerAccum, true, defaultZmap, defaultObj, out_selectedDefault );

	pickerAccum->rootPane->widget.u64 = (U64)pickerAccum;
	pickerAccum->rootPane->widget.freeF = emZeniObjectWidgetFree;

	if (eaSize(&pickerAccum->pickerTree->root.children) == 0)
	{
		// We have no maps. Bail and return NULL
		emZeniObjectWidgetFree(&pickerAccum->rootPane->widget);
		return NULL;
	}
	else if (eaSize(&pickerAccum->pickerTree->root.children) == 1)
	{
		// If there is only one map, expand it.
		// But if we have a defaultZmap and defaultObj we should already have set up our selected object and expanded the map
		//   it is within. Do not call ui_TreeNodeExpand again as it will undo that work and leave us with nothing selected.
		if (!(defaultZmap && defaultObj))
		{
			ui_TreeNodeExpand(pickerAccum->pickerTree->root.children[0]);
		}
	}

	return UI_WIDGET( pickerAccum->rootPane );
}

void emZeniObjectWidgetGetSelection( UIWidget* widget, const char** out_mapName, const char** out_logicalName )
{
	EMZeniObjectPicker* picker = (EMZeniObjectPicker*)widget->u64;
	ZoneMapEncounterInfo* selectedZeni = NULL;
	ZoneMapEncounterObjectInfo* selectedObject = NULL;
	bool isUsingWholeMap = eaiSize( &picker->filterValidTypes ) == 1 && picker->filterValidTypes[ 0 ] == EncObj_WholeMap;

	UITreeNode* selectedNode = ui_TreeGetSelected( picker->pickerTree );
	if( selectedNode && selectedNode->table == parse_ZoneMapEncounterObjectInfo ) {
		selectedObject = selectedNode->contents;
	}
	while( selectedNode && selectedNode->table != parse_ZoneMapEncounterInfo ) {
		selectedNode = ui_TreeNodeFindParent( picker->pickerTree, selectedNode );
	}
	if( selectedNode ) {
		selectedZeni = selectedNode->contents;
	}

	if( selectedZeni && selectedObject ) {
		*out_mapName = selectedZeni->map_name;
		*out_logicalName = selectedObject->logicalName;
	} else if( selectedZeni && isUsingWholeMap ) {
		*out_mapName = selectedZeni->map_name;
	} else {
		*out_mapName = NULL;
		*out_logicalName = NULL;
	}
}

void emOverworldMapApplyData( EMOverworldMapIconPicker* picker )
{
	Vec2 iconPos;
	const char* iconName;

	if( picker->iconName ) {
		iconName = picker->iconName;
	} else {
		iconName = "MapIcon_QuestionMark_01";
	}

	if( picker->iconPosition[0] >= 0 && picker->iconPosition[1] >= 0 ) {
		iconPos[0] = picker->iconPosition[0];
		iconPos[1] = picker->iconPosition[1];
	} else {
		setVec2( iconPos, 0.5, 0.5 );
	}

	ui_SpriteSetTexture( picker->icon, iconName );
	ui_ListSetSelectedObject( picker->iconList, allocAddString( iconName ));
	ui_WidgetSetPositionEx( UI_WIDGET( picker->icon ), -16, -16, iconPos[0], iconPos[1], UITopLeft );

	if( picker->changedFn ) {
		picker->changedFn( picker->changedData );
	}
}

static void emOverworldMapWidgetFree( UIWidget* widget )
{
	EMOverworldMapIconPicker* picker = (EMOverworldMapIconPicker*)(widget->u64);

	ui_PaneFreeInternal( picker->rootPane );
	free( picker );
}

static void emOverworldMapIconPreDrag( UIWidget* widget, EMOverworldMapIconPicker* picker )
{
	ui_SetCursorByName( "CF_Cursor_Hand" );
	ui_CursorLock();
}

static void emOverworldMapIconPositionToMouseCoords( EMOverworldMapIconPicker* picker, const Vec2 iconPos, int* out_mouseCoords )
{
	out_mouseCoords[0] = picker->mapLastTopLeft[0] + iconPos[0] * picker->mapLastWidth;
	out_mouseCoords[1] = picker->mapLastTopLeft[1] + iconPos[1] * picker->mapLastHeight;
}

static void emOverworldMapIconMouseCoordsToPosition( EMOverworldMapIconPicker* picker, const int* mouseCoords, Vec2 out_iconPos )
{
	setVec2( out_iconPos,
			 CLAMP( (mouseCoords[0] - picker->mapLastTopLeft[0]) / picker->mapLastWidth, 0, 1 ),
			 CLAMP( (mouseCoords[1] - picker->mapLastTopLeft[1]) / picker->mapLastHeight, 0, 1 ));
}

static void emOverworldMapIconDrag( UIWidget* widget, EMOverworldMapIconPicker* picker )
{
	int mouse[2];
	if( picker->iconPosition[0] >= 0 && picker->iconPosition[1] >= 0 ) {
		emOverworldMapIconPositionToMouseCoords( picker, picker->iconPosition, mouse );
	} else {
		Vec2 center = { 0.5, 0.5 };
		emOverworldMapIconPositionToMouseCoords( picker, center, mouse );
	}
	mouseSetScreen( mouse[0], mouse[1] );
	{
		const char* iconName;
		if( picker->iconName ) {
			iconName = picker->iconName;
		} else {
			iconName = "MapIcon_QuestionMark_01";
		}
		
		ui_DragStartEx( widget, "overworld_map_icon_drag", NULL, atlasFindTexture( iconName ), 0xFFFFFFFF, true, "CF_Cursor_Hand_Closed" );
	}

	picker->isDragging = true;
}

static void emOverworldMapIconDragEnd( UIWidget* dragWidget, UIWidget* ignoredDest, UIDnDPayload* ignored, EMOverworldMapIconPicker* picker )
{
	int mouse[2];
	mousePos( &mouse[0], &mouse[1] );
	emOverworldMapIconMouseCoordsToPosition( picker, mouse, picker->iconPosition );
	picker->isDragging = false;

	emOverworldMapApplyData( picker );
}

static void emOverworldMapIconPickerMapTick( UISprite* sprite, UI_PARENT_ARGS )
{
	EMOverworldMapIconPicker* picker = (EMOverworldMapIconPicker*)sprite->widget.u64;
	UI_GET_COORDINATES( sprite );
	AtlasTex* texture = atlasLoadTexture( ui_WidgetGetText( UI_WIDGET( sprite )));
	assert( texture );
	
	if( picker->isDragging ) {
		picker->icon->tint = colorFromRGBA( 0 );
	} else {
		picker->icon->tint = colorFromRGBA( -1 );
	}

	if( sprite->bPreserveAspectRatio ) {
		float aspectRatio = CBoxWidth( &box ) / CBoxHeight( &box );
		float boxCenterX;
		float boxCenterY;
		float spriteWidth = 1;
		float spriteHeight = 1;

		CBoxGetCenter( &box, &boxCenterX, &boxCenterY );
		spriteWidth = texture->width;
		spriteHeight = texture->height;

		if( aspectRatio > spriteWidth / spriteHeight ) {
			float height = CBoxHeight( &box );
			BuildCBoxFromCenter( &box, boxCenterX, boxCenterY, height / spriteHeight * spriteWidth, height );
		} else {
			float width = CBoxWidth( &box );
			BuildCBoxFromCenter( &box, boxCenterX, boxCenterY, width, width / spriteWidth * spriteHeight );
		}
	}
	picker->mapLastTopLeft[0] = box.lx;
	picker->mapLastTopLeft[1] = box.ly;
	picker->mapLastWidth = box.hx - box.lx;
	picker->mapLastHeight = box.hy - box.ly;
	
	ui_SpriteTick( sprite, UI_PARENT_VALUES );
}

static void emOverworldMapListDrawIcon(UIList *pList, UIListColumn *pColumn, UI_MY_ARGS, F32 z, CBox *pLogicalBox, S32 iRow, UserData pDrawData)
{
	const char* iconName = eaGet( pList->peaModel, iRow );
	if( iconName ) {
		AtlasTex* icon = atlasLoadTexture( iconName );
		CBox iconBox;
		const char* iconDisplayName = NULL;

		iconBox.lx = x + 2;
		iconBox.ly = y + 2;
		iconBox.hy = y + h - 4;
		iconBox.hx = iconBox.lx + iconBox.hy - iconBox.ly;
		display_sprite_box( icon, &iconBox, z, -1 );

		iconDisplayName = iconName;
		gfxfont_PrintMaxWidth( iconBox.hx + 2, (iconBox.ly + iconBox.hy) / 2, z, x + w - iconBox.hx - 4, scale, scale, CENTER_Y,
							   iconDisplayName );
	}
}

static void emOverworldMapWidgetIconChanged( UIList* pList, EMOverworldMapIconPicker* picker )
{
	const char* iconName = ui_ListGetSelectedObject( pList );
	if( iconName ) {
		picker->iconName = allocAddString( iconName );
	}
	
	emOverworldMapApplyData( picker );
}

UIWidget* emOverworldMapWidgetCreate( const char** eaIconNames, const float* defaultPos, const char* defaultIcon, EMPickerChangedFn changedFn, UserData changedData )
{
	EMOverworldMapIconPicker* pickerAccum = calloc( 1, sizeof( *pickerAccum ));
	
	UIPane* rootPane;
		UIList* iconList;
		UIScrollArea* mapArea;
			UISprite* map;
				UISprite* icon;

	{
		int it;
		for( it = 0; it != eaSize( &eaIconNames ); ++it ) {
			eaPush( &pickerAccum->eaIconNames, allocAddString( eaIconNames[ it ]));
		}
	}

	rootPane = ui_PaneCreate( 0, 0, 1, 1, UIUnitPercentage, UIUnitPercentage, 0 );
	rootPane->invisible = true;

	iconList = ui_ListCreate( NULL, (char***)&pickerAccum->eaIconNames, 32 );
	iconList->fHeaderHeight = 0;	
	ui_ListAppendColumn( iconList, ui_ListColumnCreateCallback( "Icon", emOverworldMapListDrawIcon, NULL ));
	ui_ListSetSelectedCallback( iconList, emOverworldMapWidgetIconChanged, pickerAccum );
	ui_WidgetSetDimensionsEx( UI_WIDGET( iconList ), 300, 1, UIUnitFixed, UIUnitPercentage );
	ui_PaneAddChild( rootPane, iconList );

	mapArea = ui_ScrollAreaCreate( 0, 0, 0, 0, 0, 0, true, true );
	ui_WidgetSetDimensionsEx( UI_WIDGET( mapArea ), 1, 1, UIUnitPercentage, UIUnitPercentage );
	ui_WidgetSetPaddingEx( UI_WIDGET( mapArea ), 304, 0, 0, 0 );
	mapArea->autosize = true;
	ui_PaneAddChild( rootPane, mapArea );

	map = ui_SpriteCreate( 0, 0, 1, 1, "World_Map" );
	map->widget.tickF = emOverworldMapIconPickerMapTick;
	map->widget.u64 = (U64)pickerAccum;
	ui_WidgetSetDimensionsEx( UI_WIDGET( map ), 1, 1, UIUnitPercentage, UIUnitPercentage );
	map->bPreserveAspectRatio = true;
	ui_ScrollAreaAddChild( mapArea, map );

	icon = ui_SpriteCreate( 0, 0, 32, 32, "MapIcon_QuestionMark_01" );
	ui_WidgetAddChild( UI_WIDGET( map ), UI_WIDGET( icon ));
	assert( !icon->widget.preDragF && !icon->widget.dragF && !icon->widget.dropF );
	ui_WidgetSetPreDragCallback( UI_WIDGET( icon ), emOverworldMapIconPreDrag, pickerAccum );
	ui_WidgetSetDragCallback( UI_WIDGET( icon ), emOverworldMapIconDrag, pickerAccum );
	ui_WidgetSetAcceptCallback( UI_WIDGET( icon ), emOverworldMapIconDragEnd, pickerAccum );

	rootPane->widget.u64 = (U64)pickerAccum;
	rootPane->widget.freeF = emOverworldMapWidgetFree;
	pickerAccum->rootPane = rootPane;
	pickerAccum->iconList = iconList;
	pickerAccum->icon = icon;

	if( defaultPos ) {
		copyVec2( defaultPos, pickerAccum->iconPosition );
	} else {
		setVec2( pickerAccum->iconPosition, 0.5, 0.5 );
	}
	pickerAccum->iconName = allocAddString( defaultIcon );
	emOverworldMapApplyData( pickerAccum );

	// Make sure this is after the call to ApplyData, so changedFn
	// doesn't get called during startup.
	pickerAccum->changedFn = changedFn;
	pickerAccum->changedData = changedData;
	
	return UI_WIDGET( rootPane );
}

void emOverworldMapWidgetGetSelection( UIWidget* widget, float* out_mapPos, const char** out_icon )
{
	EMOverworldMapIconPicker* picker = (EMOverworldMapIconPicker*)widget->u64;

	if( picker->iconPosition[ 0 ] >= 0 && picker->iconPosition[ 1 ] >= 0 ) {
		copyVec2( picker->iconPosition, out_mapPos );
	} else {
		setVec2( out_mapPos, 0.5, 0.5 );
	}
	*out_icon = picker->iconName;
}

AUTO_COMMAND;
void emShowZeniPicker( void )
{
	static const char** eaIconNames = NULL;

	if( !eaIconNames ) {
		eaPush( &eaIconNames, allocAddString( "Icon_Quest_Quest" ));
		eaPush( &eaIconNames, allocAddString( "Icon_Quest_Module" ));
		eaPush( &eaIconNames, allocAddString( "Icon_Quest_Campaign" ));
	}
	
	if( !emShowZeniObjectPicker( NULL, EncObj_Any, eaIconNames, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL )) {
		ui_DialogPopup( "No maps available", "There are no maps available that contain any named objects." );
	}
}


/// Create a generic picker that lets you pick from all files with EXT extension
/// in DIR-ROOT.
EMPicker* emEasyPickerCreate( const char* name, const char* ext, const char* dirRoot,
							  EMEasyPickerNameFilter nameFilter )
{
	EMEasyPicker* easyPicker = calloc( 1, sizeof( *easyPicker ));

	assert( ext[ 0 ] == '.' );
	assert( dirRoot[ strlen( dirRoot ) - 1 ] == '/' );

	eaPush( &easyPicker->exts, strdup( ext ));
	eaPush( &easyPicker->dirRoots, strdup( dirRoot ));

	easyPicker->picker.init_func = (EMPickerFunc)emEasyPickerInit;
	easyPicker->picker.enter_func = (EMPickerFunc)emEasyPickerEnter;
	strcpy( easyPicker->picker.picker_name, name );
	strcpy( easyPicker->picker.default_type, ext + 1 );

	easyPicker->nameFilter = nameFilter;

	return (EMPicker*)easyPicker;
}

/// Create a generic picker that lets you pick from all files with any
/// of EXTS extensions in any of DIR-ROOTS.
EMPicker* emEasyPickerCreateEx( const char* name, const char** exts, const char** dirRoots,
								EMEasyPickerNameFilter nameFilter, EMEasyPickerTexFunc texFunc )
{
	EMEasyPicker* easyPicker = calloc( 1, sizeof( *easyPicker ));
	assert( exts && dirRoots );

	{
		int it;
		for( it = 0; it != eaSize( &exts ); ++it ) {
			assert( exts[ it ][ 0 ] == '.' );
		}
		for( it = 0; it != eaSize( &dirRoots ); ++it ) {
			assert( dirRoots[ it ][ strlen( dirRoots[ it ]) - 1 ] == '/' );
		}
	}

	{
		int it;
		for( it = 0; it != eaSize( &exts ); ++it ) {
			eaPush( &easyPicker->exts, strdup( exts[ it ]));
		}
		for( it = 0; it != eaSize( &dirRoots ); ++it ) {
			eaPush( &easyPicker->dirRoots, strdup( dirRoots[ it ]));
		}
	}

	easyPicker->picker.init_func = (EMPickerFunc)emEasyPickerInit;
	easyPicker->picker.enter_func = (EMPickerFunc)emEasyPickerEnter;
	strcpy( easyPicker->picker.picker_name, name );
	strcpy( easyPicker->picker.default_type, exts[ 0 ] + 1 );

	easyPicker->nameFilter = nameFilter;
	easyPicker->texFunc = texFunc;
	
	return (EMPicker*)easyPicker;
}


void emEasyPickerDestroy(EMPicker* picker)
{
	EMEasyPicker* easyPicker = (EMEasyPicker*)picker;

	eaDestroyEx( &easyPicker->exts, NULL );
	eaDestroyEx( &easyPicker->dirRoots, NULL );
	SAFE_FREE( easyPicker );
}

void emEasyPickerSetTexFunc( EMPicker* picker, EMEasyPickerTexFunc texFunc )
{
	EMEasyPicker* easyPicker = (EMEasyPicker*)picker;

	easyPicker->texFunc = texFunc;
}

void emEasyPickerSetColorFunc( EMPicker* picker, EMEasyPickerColorFunc colorFunc )
{
	EMEasyPicker* easyPicker = (EMEasyPicker*)picker;

	easyPicker->colorFunc = colorFunc;
}

/// Initialize the general picker
static void emEasyPickerInit( EMEasyPicker* picker )
{
	{
		EMPickerDisplayType* accum;

		accum = calloc( 1, sizeof( *accum ));
		accum->parse_info = parse_EMEasyPickerFolder;
		accum->display_name_parse_field = "name";
		accum->color = CreateColorRGB( 0, 0, 0 );
		accum->selected_color = CreateColorRGB( 255, 255, 255 );
		accum->is_leaf = false;
		eaPush( &picker->picker.display_types, accum );
	}
	{
		EMPickerDisplayType* accum;

		accum = calloc( 1, sizeof( *accum ));
		accum->parse_info = parse_EMEasyPickerEntry;
		accum->display_name_parse_field = "name";
		accum->color = CreateColorRGB( 0, 0, 0 );
		accum->selected_color = CreateColorRGB( 255, 255, 255 );
		accum->is_leaf = true;
		accum->selected_func = emEasyPickerSelected;
		if( picker->texFunc ) {
			accum->tex_func = emEasyPickerTexFunc1;
		}
		if( picker->colorFunc ) {
			accum->color_func = emEasyPickerColorFunc1;
		}
		eaPush( &picker->picker.display_types, accum );
	}

	picker->picker.display_parse_info_root = parse_EMEasyPickerFolder;
	picker->picker.display_data_root = emEasyPickerDataRoot( picker );
	picker->picker.allow_outsource = 1;
}

/// Refresh the data root
static void emEasyPickerEnter( SA_PARAM_NN_VALID EMEasyPicker* picker )
{
	EMEasyPickerScanner scanner;
	scanner.picker = picker;
	scanner.root = picker->picker.display_data_root;

	{
		int it;
		for( it = 0; it != eaSize( &picker->dirRoots ); ++it ) {
			fileScanAllDataDirs( picker->dirRoots[ it ], emEasyPickerScan, &scanner );
		}
	}
}

/// Callback when the picker is selected
static bool emEasyPickerSelected( EMPicker* picker, EMPickerSelection* selection )
{
	EMEasyPicker* easyPicker = (EMEasyPicker*)picker;
	EMEasyPickerEntry* entry = (EMEasyPickerEntry*)selection->data;
	
	if( selection->table == parse_EMEasyPickerEntry ) {
		strcpy( selection->doc_type, easyPicker->picker.default_type );
		strcpy( selection->doc_name, entry->path );
		return true;
	} else {
		return false;
	}
}

/// Callback to generate a basic texture from a picker entry.
static void emEasyPickerTexFunc1( EMPicker* rawPicker, void* rawEntry, ParseTable pti[], BasicTexture** out_tex, Color* out_mod_color )
{
	EMEasyPicker* picker = (EMEasyPicker*)rawPicker;
	EMEasyPickerEntry* entry = (EMEasyPickerEntry*)rawEntry;
	assert( pti == parse_EMEasyPickerEntry );

	if( picker->texFunc ) {
		picker->texFunc( entry->path, &entry->texture, out_mod_color );
		*out_tex = entry->texture;
	} else {
		*out_tex = NULL;
		*out_mod_color = ColorWhite;
	}
}

/// Callback to generate a color from a picker entry.
static Color emEasyPickerColorFunc1( EMPicker* rawPicker, void* rawEntry, ParseTable pti[], bool isSelected )
{
	EMEasyPicker* picker = (EMEasyPicker*)rawPicker;
	EMEasyPickerEntry* entry = (EMEasyPickerEntry*)rawEntry;
	assert( pti == parse_EMEasyPickerEntry );
		
	if( picker->colorFunc ) {
		return picker->colorFunc( entry->path, isSelected );
	} else {
		if( isSelected ) {
			return CreateColorRGB( 255, 255, 255 );
		} else {
			return CreateColorRGB( 0, 0, 0 );
		}
	}
}

static EMEasyPickerFolder* emEasyPickerDataRoot( EMEasyPicker* picker )
{
	EMEasyPickerScanner scanner;
	scanner.picker = picker;
	scanner.root = StructCreate( parse_EMEasyPickerFolder );
	
	scanner.root->name = StructAllocString( picker->dirRoots[ 0 ]);

	{
		int it;
		for( it = 0; it != eaSize( &picker->dirRoots ); ++it ) {
			fileScanAllDataDirs( picker->dirRoots[ it ], emEasyPickerScan, &scanner );
		}
	}
	
	return scanner.root;
}

static FileScanAction emEasyPickerScan( char* dir, struct _finddata32_t* data, EMEasyPickerScanner* scanner )
{
	char fullPath[ MAX_PATH ];
	char dirPath[ MAX_PATH ];
	const char* dirRoot = NULL;

	sprintf( fullPath, "%s/%s", dir, data->name );
	sprintf( dirPath, "%s/", dir );

	{
		int it;
		for( it = 0; it != eaSize( &scanner->picker->dirRoots ); ++it ) {
			if( strStartsWith( dirPath, scanner->picker->dirRoots[ it ])) {
				dirRoot = scanner->picker->dirRoots[ it ];
				break;
			}
		}
		assert( dirRoot != NULL );
	}
	
	{
		EMEasyPickerFolder* nodeIt = scanner->root;
		char* contextIt = NULL;
		char* dirPathIt = strtok_s( dirPath + strlen( dirRoot ), "/", &contextIt );
		
		while( dirPathIt != NULL ) {
			nodeIt = emEasyPickerInternFolder( scanner->picker, nodeIt, dirPathIt );
			dirPathIt = strtok_s( NULL, "/", &contextIt );
		}

		if( !(data->attrib & _A_SUBDIR) && strEndsWithAny( data->name, scanner->picker->exts )) {
			emEasyPickerInternEntry( scanner->picker, nodeIt, fullPath );
		}
	}
	
	return FSA_EXPLORE_DIRECTORY;
}

/// Think Java's String.Intern.
///
/// Find the entry with name ENTRY in FOLDER and return it.  If no
/// such entry exists, create it and then return it.
static EMEasyPickerEntry* emEasyPickerInternEntry(
		EMEasyPicker* picker, EMEasyPickerFolder* folder, char* path )
{
	{
		int it;
		for( it = 0; it != eaSize( &folder->entries ); ++it ) {
			if( strcmp( folder->entries[ it ]->path, path ) == 0 ) {
				return folder->entries[ it ];
			}
		}
	}

	{
		char* name;
		if( picker->nameFilter ) {
			name = picker->nameFilter( path );
		} else {
			name = StructAllocString( getFileName( path ));
		}

		if( name ) {
			EMEasyPickerEntry* accum = StructCreate( parse_EMEasyPickerEntry );
			accum->name = name;
			accum->path = StructAllocString( path );
			eaPush( &folder->entries, accum );
			return accum;
		} else {
			return NULL;
		}
	}
}

/// Think Java's String.Intern.
///
/// Find the subfolder with name SUBFOLDER in FOLDER and return it.
/// If no such subfolder exists, create it and then return it.
static EMEasyPickerFolder* emEasyPickerInternFolder(
		EMEasyPicker* picker, EMEasyPickerFolder* folder, char* subfolder )
{
	{
		int it;
		for( it = 0; it != eaSize( &folder->folders ); ++it ) {
			if( strcmp( folder->folders[ it ]->name, subfolder ) == 0 ) {
				return folder->folders[ it ];
			}
		}
	}

	{
		EMEasyPickerFolder* accum = StructCreate( parse_EMEasyPickerFolder );
		accum->name = StructAllocString( subfolder );
		eaPush( &folder->folders, accum );
		return accum;
	}
}

/******
* This function gets the current selections from the picker.
* This should only be called and the returned value is only valid during a callback as
* generated by calling "emPickerShow".
* PARAMS:
*   picker - The picker
******/
EMPickerSelection ***emPickerGetSelections(EMPicker *picker)
{
	return &picker->selections;
}

#endif

#include "EditorManagerUIPickers_c_ast.c"
#include "EditorManagerUIPickers_h_ast.c"
