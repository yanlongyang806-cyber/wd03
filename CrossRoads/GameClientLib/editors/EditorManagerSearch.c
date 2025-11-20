#include "EditorManagerSearch.h"
#include "ResourceSearch.h"

#ifndef NO_EDITORS
#include "EditorManagerPrivate.h"
#include "EditorPrefs.h"
#include "EditLibUIUtil.h"


#include "EditorManagerSearch_c_ast.h"
#include "../AutoGen/ResourceSearch_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

/********************
* DEFINITIONS AND GLOBALS
********************/
#endif
AUTO_STRUCT;
typedef struct EMSearchResult
{
	char *tab_name;				// tab name
	char *panel_name;			// panel name
	char *desc;					// description

	ResourceSearchResultRow **rows;			// result rows
} EMSearchResult;
#ifndef NO_EDITORS

typedef struct EMSearchResultTab
{
	EMSearchResult *result;
	EMPanel *panel;
	UIList *list;
	EMSearchCloseFunc close_func;
	void *close_data;
} EMSearchResultTab;

typedef struct EMUsageSearchResult
{
	EMSearchResult *result;
	void **funcs;
	char *search_text;
	bool closed;
} EMUsageSearchResult;


/********************
* SEARCH RESULT MANAGEMENT
********************/
/******
* This function creates a search result object.
* PARAMS:
*   tab_name - string name to give to the search results; displayed on a search results tab
*   panel_name - string name for the search result panel; displayed on the expander
*   desc - string description for the search results; displayed at the top of the
*          results panel
* RETURNS:
*   EMSearchResult object
******/
EMSearchResult *emSearchResultCreate(const char *tab_name, const char *panel_name, const char *desc)
{
	EMSearchResult *result = StructCreate(parse_EMSearchResult);

	if (tab_name)
		result->tab_name = StructAllocString(tab_name);
	if (panel_name)
		result->panel_name = StructAllocString(panel_name);
	if (desc)
		result->desc = StructAllocString(desc);

	return result;
}

/******
* This function adds all rows from a resource search result to a search result.
* PARAMS:
*   result - EMSearchResult object to contain the result rows
*   res_result - ResourceSearchResult with rows to be added
******/
void emSearchResultAddResourceResult(EMSearchResult *result, ResourceSearchResult *res_result)
{
	int i;
	for(i = 0; i<eaSize(&res_result->eaRows); ++i)
		eaPush(&result->rows, StructClone(parse_ResourceSearchResultRow, res_result->eaRows[i]));
}

/******
* This function adds a search result row to a specified search result object.
* PARAMS:
*   result - EMSearchResult object to contain the result rows
*   name - string corresponding to the asset name; will also be used when invoking emOpenFileEx
*   type - string corresponding to the asset type; will also be used when invoking emOpenFileEx
*   display_type - string displayed to the user corresponding to the asset type
*   relation - string describing the relation of the object with regard to the search
******/
void emSearchResultAddRow(EMSearchResult *result, const char *name, const char *type, const char *relation)
{
	// create search row
	ResourceSearchResultRow *row = StructCreate(parse_ResourceSearchResultRow);
	char buf[128];

	row->pcName = StructAllocString(name);

	if (type && type[0])
		row->pcType = StructAllocString(type);
	else
	{
		sprintf(buf, "%s (not openable)", name);
		StructFreeString(row->pcName);
		row->pcName = StructAllocString(buf);
	}

	if (relation)
		row->pcExtraData = StructAllocString(relation);

	// add the row
	eaPush(&result->rows, row);
}

/******
* This function appends copies of the rows of one search results object to another.  This
* does NOT move rows and does NOT do freeing of any sort.  The source result object's rows can
* be freed without affecting the rows that were copied to the dest object.
* PARAMS:
*   dest - EMSearchResult where the all of the rows will end up
*   source - EMSearchResult whose rows will be appended to dest's rows
******/
void emSearchResultAppend(EMSearchResult *dest, const EMSearchResult *source)
{
	int i;

	for (i = 0; i < eaSize(&source->rows); i++)
	{
		ResourceSearchResultRow *row = StructCreate(parse_ResourceSearchResultRow);
		StructCopyAll(parse_ResourceSearchResultRow, source->rows[i], row);
		eaPush(&dest->rows, row);
	}
}

/******
* This function gets the number of rows in the search result.
* PARAMS:
*   result - EMSearchResult to get the size of
******/
int emSearchResultSize(EMSearchResult *result)
{
	return eaSize(&result->rows);
}

/******
* This function clears the rows from a search result object without destroying the search 
* result object itself.
* PARAMS:
*   result - EMSearchResult to clear of rows
******/
void emSearchResultClear(EMSearchResult *result) 
{
	int i;

	for(i = eaSize(&result->rows) - 1; i >= 0; i--)
		StructDestroy(parse_ResourceSearchResultRow, result->rows[i]);

	eaClear(&result->rows);
}

/******
* This function destroys a result object and all of its rows.
* PARAMS:
*   result - EMSearchResult to destroy
******/
void emSearchResultDestroy(EMSearchResult *result)
{
	StructDestroy(parse_EMSearchResult, result);
}

/********************
* SEARCH TABS
********************/
// UI callbacks
static void emSearchOpenDoc(UIWidget *unused, UIList *list)
{
	const int * const *selected_rows = ui_ListGetSelectedRows(list);
	ResourceSearchResultRow ***rows = (ResourceSearchResultRow***) ui_ListGetModel(list);
	int i;

	selected_rows = ui_ListGetSelectedRows(list);
	if (!selected_rows || eaiSize(selected_rows) == 0)
		return;

	for (i = 0; i < eaiSize(selected_rows); i++)
	{
		ResourceSearchResultRow *row = (*rows)[(*selected_rows)[i]];
		
		if (row)
			emOpenFileEx(row->pcName, row->pcType);
	}
}

static void emSearchCloseClicked(UIButton *button, EMSearchResultTab *search_ui)
{
	if (search_ui->close_func)
		search_ui->close_func(search_ui->result, search_ui->close_data);

	emSearchResultTabSaveColWidths(search_ui);
	eaFindAndRemove(&em_data.search_panels.result_tabs, search_ui);
	eaFindAndRemove(&em_data.sidebar.panels, search_ui->panel);
	emPanelFree(search_ui->panel);
	StructDestroy(parse_EMSearchResult, search_ui->result);
	free(search_ui);
	emQueueFunctionCall2(emPanelsShow, em_data.current_doc, NULL);
}

/******
* This function assigns a unique tab/panel name combination to the specified search result,
* keeping the tab name fixed and adding a numeric appendix to the panel name, incrementing
* the appended number continuously until a unique name is found.
* PARAMS:
*   result - EMSearchResult for which to create a unique tab/panel name combination
******/
static void emSearchCreateUniquePanelName(EMSearchResult *result)
{
	bool found;
	int i, num = 1;
	char buf[260];

	// give results default name values if not already specified
	if (!result->tab_name)
		result->tab_name = StructAllocString("Search Results");
	if (!result->panel_name)
		result->panel_name = StructAllocString("Results");

	// append number to result's panel name and continue incrementing number until
	// its tab/panel name combination is unique
	do 
	{
		found = false;
		for (i = 0; i < eaSize(&em_data.search_panels.result_tabs); i++)
		{
			if (strcmpi(em_data.search_panels.result_tabs[i]->result->panel_name, result->panel_name) == 0
				&& strcmpi(em_data.search_panels.result_tabs[i]->result->tab_name, result->tab_name) == 0)
			{
				found = true;
				break;
			}
		}
		if (found)
		{
			sprintf(buf, "%s #%i", result->panel_name, em_data.search_panels.curr_num++);
			StructFreeString(result->panel_name);
			result->panel_name = StructAllocString(buf);
		}
	} while (found);
}

/******
* This function saves the width values for each of the tab's list columns.
* PARAMS:
*   result_tab - EMSearchResultTab from which to get the column widths
******/
void emSearchResultTabSaveColWidths(const EMSearchResultTab *result_tab)
{
	EditorPrefStoreFloat("Search Panel", "Column Width", "Name", result_tab->list->eaColumns[0]->fWidth);
	EditorPrefStoreFloat("Search Panel", "Column Width", "Type", result_tab->list->eaColumns[1]->fWidth);
	EditorPrefStoreFloat("Search Panel", "Column Width", "Relation", result_tab->list->eaColumns[2]->fWidth);
}

/******
* This function takes a search result object and displays it in a panel in the Editor Manager.
* The panel is created using the tab and panel names specified in the EMSearchResult object.
* PARAMS:
*   result - EMSearchResult to display
******/
void emSearchResultShowEx(EMSearchResult *result, EMSearchCloseFunc close_func, void *close_data, bool no_copy)
{
	EMSearchResultTab *search_ui;
	EMPanel *panel;
	UIList *list;
	UIListColumn *column;
	UILabel *label;
	UIButton *button;
	F32 h = 0;

	if (!result)
		return;

	// allocate UI tracking structure
	search_ui = calloc(1, sizeof(EMSearchResultTab));
	search_ui->close_func = close_func;
	search_ui->close_data = close_data;
	if (no_copy)
		search_ui->result = result;
	else
	{
		search_ui->result = calloc(1, sizeof(EMSearchResult));
		StructCopyAll(parse_EMSearchResult, result, search_ui->result);
	}

	// validate uniqueness of tab/panel name
	emSearchCreateUniquePanelName(search_ui->result);
	
	// create panel
	panel = emPanelCreate(search_ui->result->tab_name ? search_ui->result->tab_name : SEARCH_TAB_NAME, search_ui->result->panel_name ? search_ui->result->panel_name : "Search Results", 500);
	search_ui->panel = panel;

	label = ui_LabelCreate(search_ui->result->desc ? search_ui->result->desc : SEARCH_TAB_NAME, 0, 0);
	emPanelAddChild(panel, label, false);

	list = ui_ListCreate(parse_ResourceSearchResultRow, &search_ui->result->rows, 20);
	ui_WidgetSetPosition(UI_WIDGET(list), 0, 0);
	ui_WidgetSetDimensionsEx(UI_WIDGET(list), 1, 1, UIUnitPercentage, UIUnitPercentage);
	ui_ListSetActivatedCallback(list, emSearchOpenDoc, list);
	ui_ListSetMultiselect(list, true);
	list->bDrawGrid = true;
	search_ui->list = list;
	emPanelAddChild(panel, list, false);

	column = ui_ListColumnCreateParseName("Object Name", "name", NULL);
	if (EditorPrefIsSet("Search Panel", "Column Width", "Name"))
		ui_ListColumnSetWidth(column, false, EditorPrefGetFloat("Search Panel", "Column Width", "Name", 120));
	else
		ui_ListColumnSetWidth(column, true, 0);
	ui_ListAppendColumn(list, column);

	column = ui_ListColumnCreateParseName("Type", "type", NULL);
	if (EditorPrefIsSet("Search Panel", "Column Width", "Type"))
		ui_ListColumnSetWidth(column, false, EditorPrefGetFloat("Search Panel", "Column Width", "Type", 120));
	else
		ui_ListColumnSetWidth(column, true, 0);
	ui_ListAppendColumn(list, column);

	column = ui_ListColumnCreateParseName("Relation", "relation", NULL);
	if (EditorPrefIsSet("Search Panel", "Column Width", "Relation"))
		ui_ListColumnSetWidth(column, false, EditorPrefGetFloat("Search Panel", "Column Width", "Relation", 200));
	else
		ui_ListColumnSetWidth(column, true, 0);
	ui_ListAppendColumn(list, column);

	// set up buttons
	button = ui_ButtonCreate("Open", 0, 0, emSearchOpenDoc, list);
	ui_WidgetSetWidthEx(UI_WIDGET(button), 0.5, UIUnitPercentage);
	ui_WidgetSetPositionEx(UI_WIDGET(button), 0, 0, 0, 0, UIBottomLeft);
	emPanelAddChild(panel, button, false);

	button = ui_ButtonCreate("Close Results", 0, 0, emSearchCloseClicked, search_ui);
	ui_WidgetSetWidthEx(UI_WIDGET(button), 0.5, UIUnitPercentage);
	ui_WidgetSetPositionEx(UI_WIDGET(button), 0, 0, 0, 0, UIBottomRight);
	emPanelAddChild(panel, button, false);

	ui_WidgetSetPaddingEx(UI_WIDGET(list), 0, 0, elUINextY(label) + 5, elUINextY(button) + 5);
	emPanelSetHeight(panel, MIN(450, 20*(eaSize(&search_ui->result->rows)+1) + 15 + label->widget.height + button->widget.height));

	eaPush(&em_data.sidebar.panels, panel);
	emSidebarShow(true);  // Open sidebar if not open right now
	emPanelsShow(em_data.current_doc, NULL);
	emQueueFunctionCall(emPanelFocus, panel);
	eaPush(&em_data.search_panels.result_tabs, search_ui);
}

/********************
* USAGE SEARCHES
********************/
/******
* This function is used as the close callback for the search result tab.  It destroys
* the usage search result object properly, or, if some editors are still waiting for
* data from the server, this function flags the emSearchUsagesRepeat function to
* free the search result object.
* PARAMS:
*   result - EMSearchResult used as the model for the usage search result tab
*   usage_result - EMUsageSearchResult object
******/
static void emSearchUsagesClose(EMSearchResult *result, EMUsageSearchResult *usage_result)
{
	if (eaSize(&usage_result->funcs) == 0)
	{
		eaDestroy(&usage_result->funcs);
		SAFE_FREE(usage_result->search_text);
		free(usage_result);
	}
	else
		usage_result->closed = true;
}

/******
* This function is called every frame while some search functions continue to fail
* to finish the search (intended to be used when the editor is waiting for data
* from the server).
* PARAMS:
*   usage_result - EMUsageSearchResult to repeat
******/
static void emSearchUsagesRepeat(EMUsageSearchResult *usage_result)
{
	if (!usage_result->closed)
	{
		int i;
		for (i = 0; i < eaSize(&usage_result->funcs); i++)
		{
			if (!usage_result->funcs[i] || ((EMSearchFunc) usage_result->funcs[i])(usage_result->result, usage_result->search_text))
				eaRemove(&usage_result->funcs, i--);
		}
		if (eaSize(&usage_result->funcs) != 0)
			emQueueFunctionCallEx(emSearchUsagesRepeat, usage_result, 1);
	}
	else
	{
		eaDestroy(&usage_result->funcs);
		SAFE_FREE(usage_result->search_text);
		free(usage_result);
	}
}

/******
* This function searches the usages of a particular string by invoking the usage
* search functions of registered editors.  If the usage search function returns
* false in any case, this is an indication that the search function isn't ready
* yet to process the search and is waiting to be ready.  In this case, the unready
* search function(s) will be called every frame until they return true with the
* correct search results.
* PARAMS:
*   search_text - string text to use for the usage search
******/
void emSearchUsages(const char *search_text)
{
	EMUsageSearchResult *usage_results;
	int i;
	char desc[256];

	if (!search_text || !search_text[0])
		return;

	sprintf(desc, "Found usages of \"%s\":", search_text);
	usage_results = calloc(1, sizeof(EMUsageSearchResult));
	usage_results->search_text = strdup(search_text);
	usage_results->result = emSearchResultCreate(SEARCH_TAB_NAME, "Results", desc);

	for (i = 0; i < eaSize(&em_data.editors); i++)
	{
		if (em_data.editors[i]->usage_search_func && !em_data.editors[i]->usage_search_func(usage_results->result, search_text))
			eaPush(&usage_results->funcs, em_data.editors[i]->usage_search_func);
	}

	emQueueFunctionCallEx(emSearchUsagesRepeat, usage_results, 200);
	emSearchResultShowEx(usage_results->result, emSearchUsagesClose, usage_results, true);
}

#endif

#include "EditorManagerSearch_c_ast.c"
