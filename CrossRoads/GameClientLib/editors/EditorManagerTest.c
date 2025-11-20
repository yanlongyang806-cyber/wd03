GCC_SYSTEM

#ifndef NO_EDITORS

#include "EditorManager.h"
#include "EditLibUIUtil.h"
#include "file.h"
#include "EString.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

const bool emTestEnabled = false;	// enable this to see the test editors

// This will both test and demonstrate how to use the various features of the Editor Manager.

static const char *testData[] = // used random word generator
{"object", "lightning", "sensitivity", "union", "finding", "attention", "named", "spill", "bent", "ready",
"laziness", "exempt", "gesture", "supporting", "gateway", "velocity", "arithmetic", "closet", "clipping",
"smiling", "adaptation", "ceremony", "guy", "cabinet", "posing", "embarrassment", "concerto", "faithful",
"slave", "fighter", "downhill", "status", "ill", "fix", "litter", "subway", "contradiction", "ethic",
"taxpayer", "buyer", "sum", "converting", "typing", "person", "receiving", "misuse", "trap", "drug", "sale",
"ecology", "screaming", "hill", "cover", "addict", "leading", "rave", "density", "defeat", "handle"};

static char *testData2[] = // file list
{"maps/_joseph/testdata/test1.txt","maps/_joseph/testdata/test2.txt","maps/_joseph/testdata/test3.txt"};

extern ParseTable parse_EMDocContainer[];
#define TYPE_parse_EMDocContainer EMDocContainer
extern ParseTable parse_EMDocData[];
#define TYPE_parse_EMDocData EMDocData

#endif

typedef struct EMDocContainer EMDocContainer;

AUTO_STRUCT;
typedef struct EMDocData
{
	char *data;
	char *filename;
} EMDocData;

AUTO_STRUCT;
typedef struct EMDocContainer
{
	char *name;
	EMDocContainer **children;
	EMDocData **data;
} EMDocContainer;
#ifndef NO_EDITORS

static void emTestDocCreateData(EMDocContainer *root)
{
	int i;
	EMDocContainer **stack = NULL;

	eaPush(&stack, root);
	for (i = 0; i < ARRAY_SIZE(testData); i++)
	{
		EMDocContainer *newContainer = calloc(1, sizeof(EMDocContainer));
		int type = ((float)rand() / (float)RAND_MAX * 3.0f);
		int maxI = ((float)rand() / (float)RAND_MAX * 4.0f) + i;

		assert(eaSize(&stack) > 0);
		newContainer->name = strdup(testData[i]);

		// randomly decide to make new container: 1) a child of last one, 2) a sibling of last one,
		// or 3) an uncle of the last one
		if (type == 0 && eaSize(&stack) > 2)
		{
			// uncle
			eaPop(&stack);
			eaPop(&stack);
			assert(eaSize(&stack) >= 1);
			eaPush(&stack[eaSize(&stack) - 1]->children, newContainer);
			eaPush(&stack, newContainer);
		}
		else if (type == 1 && eaSize(&stack) > 1)
		{
			// sibling
			eaPop(&stack);
			eaPush(&stack[eaSize(&stack) - 1]->children, newContainer);
			eaPush(&stack, newContainer);
		}
		else
		{
			// child
			eaPush(&stack[eaSize(&stack) - 1]->children, newContainer);
			eaPush(&stack, newContainer);
		}

		// randomly add child data
		for (i++; i < maxI && i < ARRAY_SIZE(testData); i++)
		{
			EMDocData *newData = calloc(1, sizeof(EMDocData));
			newData->data = strdup(testData[i]);
			eaPush(&newContainer->data, newData);
		}
		i--;
	}
	eaDestroy(&stack);
}

static void emTestDocCreateData2(EMDocContainer *root)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(testData2); i++)
	{
		EMDocData *newData = calloc(1, sizeof(EMDocData));
		newData->data = strdup(getFileName(testData2[i]));
		newData->filename = strdup(testData2[i]);
		eaPush(&root->data, newData);
	}
}

/********************
* SINGLE DOC
********************/
EMEditor *sdEditor;
EMPicker *sdGlobalPicker;
EMToolbar *toolbar1, *toolbar2;

AUTO_COMMAND ACMD_NAME("EMTest.SingleDocNew");
void emTestSingleDocCmd(void)
{
	printf("test single doc command\n");
}

AUTO_COMMAND ACMD_NAME("EMTest.SingleDocNewPanel");
void emTestSingleDocNewPanelCmd(void)
{
	EMEditorDoc *doc = emGetActiveEditorDoc();
	//	if (doc)
	//		eaPush(&doc->em_panels, emPanelCreate("SD panel", "SD expander", 50));
	if (eaFindAndRemove(&sdEditor->toolbars, toolbar1) >= 0)
		eaPush(&sdEditor->toolbars, toolbar2);
	else if (eaFindAndRemove(&sdEditor->toolbars, toolbar2) >= 0)
		eaPush(&sdEditor->toolbars, toolbar1);
}

static bool emTestSingleDocPickerCallback(EMPicker *picker, EMPickerSelection **selections, void *unused)
{
	assert(selections[0]->table == parse_EMDocData);
	printf("selected %s from picker.\n", ((EMDocData*) selections[0]->data)->data);
	return true;
}

AUTO_COMMAND ACMD_NAME("EMTest.SingleDocMenuFunc");
void emTestSingleDocMenuFunc(void)
{
	if (sdGlobalPicker)
		emPickerShow(sdGlobalPicker, "Test", false, emTestSingleDocPickerCallback, NULL);
}

EMMenuItemDef emTestSingleDocMenuItems[] =
{
	{"newpanel", "New panel", NULL, NULL, "EMTest.SingleDocNewPanel"},
	{"testfile1", "File menu item 1", NULL, NULL, "EMTest.SingleDocNew"},
	{"testfile2", "File menu item 2", NULL, NULL, "EMTest.SingleDocMenuFunc"},
	{"testfile3", "File menu item 3", NULL, NULL, "Editor.spawnplayer"},
};

static void emTestSingleDocEditorInit(EMEditor *editor)
{
	UIButton *button;

	// toolbar
	toolbar1 = emToolbarCreate(0);
	eaPush(&editor->toolbars, toolbar1);
	button = ui_ButtonCreate("singledoc button 1", 0, 0, NULL, NULL);
	emToolbarAddChild(toolbar1, button, false);
	button = ui_ButtonCreate("singledoc btn 2", elUINextX(button) + 5, 0, NULL, NULL);
	emToolbarAddChild(toolbar1, button, true);
	toolbar2 = emToolbarCreate(0);
	button = ui_ButtonCreateImageOnly("eui_button_close", 0, 0, NULL, NULL);
	emToolbarAddChild(toolbar2, button, false);
	button = ui_ButtonCreate("button 4", elUINextX(button) + 5, 0, NULL, NULL);
	emToolbarAddChild(toolbar2, button, true);

	// menus
	emMenuItemCreateFromTable(editor, emTestSingleDocMenuItems, ARRAY_SIZE_CHECKED(emTestSingleDocMenuItems));
	emMenuRegister(editor, emMenuCreate(editor, "File", "testfile1", "testfile2", "testfile3", NULL));
	emMenuRegister(editor, emMenuCreate(editor, "SD Test Menu", "new", "newpanel", "open", "save", "testfile1", NULL));
}

static EMEditorDoc *emTestSingleDocNew(const char *type, void *unused)
{
	EMEditorDoc *newDoc = calloc(1, sizeof(EMEditorDoc));
	UIWindow *mainWin = ui_WindowCreate("SD New Doc Window", 100, 100, 500, 500);
	UIWindow *otherWin = ui_WindowCreate("SD Other Window", 200, 200, 100, 100);
	UIWindow *privateWin = ui_WindowCreate("SD Private Window", 300, 300, 150, 300);

	strcpy(newDoc->doc_display_name, "SD New Doc");
	eaPush(&newDoc->ui_windows, mainWin);
	eaPush(&newDoc->ui_windows, otherWin);
	eaPush(&newDoc->ui_windows_private, privateWin);
	newDoc->primary_ui_window = mainWin;
	return newDoc;
}

static EMEditorDoc *emTestSingleDocLoad(const char *name, const char *type)
{
	EMEditorDoc *newDoc = calloc(1, sizeof(EMEditorDoc));
	UIWindow *mainWin = ui_WindowCreate(name, 100, 100, 500, 500);

	strcpy(newDoc->doc_display_name, name);
	eaPush(&newDoc->ui_windows, mainWin);
	newDoc->primary_ui_window = mainWin;
	return newDoc;
}

static bool emTestSingleDocDataClicked(EMPicker *browser, EMPickerSelection *selection)
{
	EMDocData *data = (EMDocData*) selection->data;

	assert(selection->table == parse_EMDocData);
	strcpy(selection->doc_name, data->data);
	strcpy(selection->doc_type, "emtestsd");

	return true;
}

static bool emTestSingleDocFilter1(void *nodeData, ParseTable *parseTable)
{
	char name[80];
	if (parseTable == parse_EMDocData)
		strcpy(name, ((EMDocData*) nodeData)->data);
	else if (parseTable == parse_EMDocContainer)
		strcpy(name, ((EMDocContainer*) nodeData)->name);
	else
		assert(1);

	return !!(strstri(name, "ch"));
}

static bool emTestSingleDocFilter2(void *nodeData, ParseTable *parseTable)
{
	char name[80];
	if (parseTable == parse_EMDocData)
		strcpy(name, ((EMDocData*) nodeData)->data);
	else if (parseTable == parse_EMDocContainer)
		strcpy(name, ((EMDocContainer*) nodeData)->name);
	else
		assert(1);

	return !!(strstri(name, "th"));
}

static void emTestSingleDocPickerInit(EMPicker *sdPicker)
{
	EMPickerDisplayType *type;
	EMPickerFilter *filter;
	EMDocContainer *sdData = calloc(1, sizeof(*sdData));

	emTestDocCreateData(sdData);

	sdPicker->display_data_root = sdData;
	sdPicker->display_parse_info_root = parse_EMDocContainer;

	type = calloc(1, sizeof(*type));
	type->parse_info = parse_EMDocContainer;
	type->display_name_parse_field = "name";
	type->is_leaf = 0;
	eaPush(&sdPicker->display_types, type);

	type = calloc(1, sizeof(*type));
	type->selected_func = emTestSingleDocDataClicked;
	type->parse_info = parse_EMDocData;
	type->display_name_parse_field = "data";
	type->is_leaf = 1;
	eaPush(&sdPicker->display_types, type);

	filter = calloc(1, sizeof(*filter));
	filter->display_text = StructAllocString("Filter1");
	filter->checkF = emTestSingleDocFilter1;
	eaPush(&sdPicker->filters, filter);

	filter = calloc(1, sizeof(*filter));
	filter->display_text = StructAllocString("Filter2");
	filter->checkF = emTestSingleDocFilter2;
	eaPush(&sdPicker->filters, filter);
}

#endif
AUTO_RUN;
void emTestRegisterSingleDocEditor(void)
{
#ifndef NO_EDITORS
	if (emTestEnabled)
	{
		EMPicker *sdPicker = calloc(1, sizeof(EMPicker));
		sdEditor = calloc(1, sizeof(EMEditor));

		// editor params
		sdEditor->type = EM_TYPE_SINGLEDOC;
		strcpy(sdEditor->editor_name, "EMTEST-SINGLEDOC");
		sdEditor->default_type = "emtestsd";
		sdEditor->new_func = emTestSingleDocNew;
		sdEditor->load_func = emTestSingleDocLoad;
		sdEditor->allow_multiple_docs = true;
		strcpy(sdEditor->default_workspace, "EMTEST WS");

		// callbacks
		sdEditor->init_func = emTestSingleDocEditorInit;

		// browser
		sdPicker->init_func = emTestSingleDocPickerInit;
		strcpy(sdPicker->picker_name, "EMTEST-SINGLEDOC-BROWSER1");
		eaPush(&sdEditor->pickers, sdPicker);
		sdGlobalPicker = sdPicker;
		sdPicker = calloc(1, sizeof(EMPicker));
		sdPicker->init_func = emTestSingleDocPickerInit;
		strcpy(sdPicker->picker_name, "EMTEST-SINGLEDOC-BROWSER2");
		eaPush(&sdEditor->pickers, sdPicker);

		emRegisterEditor(sdEditor);
		emRegisterFileType("emtestsd", "test doc", sdEditor->editor_name);
	}
#endif
}
#ifndef NO_EDITORS

/********************
* MULTI-DOC EDITOR EXAMPLE
********************/
typedef struct EMTestMultiDocEditorState
{
	bool state_switch;
} EMTestMultiDocEditorState;
static EMTestMultiDocEditorState mdEditorState = {0};

static void emTestMultiDocTextChanged(UITextArea *area, EMEditorDoc *doc)
{
	doc->saved = false;
}

static bool emTestMultiDocCommandCheck(UserData unused)
{
	return mdEditorState.state_switch;
}

static bool emTestMultiDocCustomCheck(UserData unused)
{
	return !mdEditorState.state_switch;
}

AUTO_COMMAND ACMD_NAME("EMTest.MultiDocCommand");
void emTestMultiDocCommand(void)
{
	EMSearchResult *result = emSearchResultCreate(NULL, "test results", "this is a test search");
	mdEditorState.state_switch = !mdEditorState.state_switch;
	emSearchResultAddRow(result, "Combat", NULL, "random");
	emSearchResultAddRow(result, "Idle", "fsm", "stuff");
	emSearchResultShow(result);
}

/******
* This static table defines all of the menu items that will exist in the Editor Manager's
* menu item framework, which allows for easy access to menu items by name.  The framework
* also allows you to define activation conditions.  The framework also does a reverse lookup
* of command strings on the keybind profile stack to attempt to match a keybind to each menu
* item.  If one is found, the bind is displayed by the menu item.
******/
static EMMenuItemDef emTestMultiDocMenuItems[] =
{
	// "saveall" is an item name belonging to one of the Asset Manager items; specifying this
	// for the editor will override the Asset Manager's default item in the "File" menu
	{"saveall", "MD Custom Save All", NULL, NULL, "EMTest.MultiDocSaveAll"},

	// typical command-string based menu item
	{"command", "MD Command", emTestMultiDocCommandCheck, NULL, "EMTest.MultiDocCommand"},

	// non-command-string menu item; must be overridden in code with a manually-created menu item
	{"custom", NULL, emTestMultiDocCustomCheck},
};

/******
* This function is the initialization function for the editor, which is called once when the editor
* opened for a new or loaded document.  This is where all UI setup should be performed (and NOT at
* the registration step).
* PARAMS:
*   editor - EMEditor being initialized
******/
static void emTestMultiDocEditorInit(EMEditor *editor)
{
	EMToolbar *toolbar;
	UIButton *button;

	// MENU EXAMPLES
	// standard menu
	emMenuRegister(editor, ui_MenuCreateWithItems("MD Manual Menu", ui_MenuItemCreate("MD Menu Item", UIMenuCallback, NULL, NULL, NULL), NULL));

	// automated menu 
	emMenuItemCreateFromTable(editor, emTestMultiDocMenuItems, ARRAY_SIZE_CHECKED(emTestMultiDocMenuItems));
	emMenuItemSet(editor, "custom", ui_MenuItemCreate("MD Custom Item", UIMenuCheckRefButton, NULL, NULL, &mdEditorState.state_switch));
	emMenuRegister(editor, emMenuCreate(editor, "MD Auto Menu", "saveall", "command", "custom", NULL));

	// custom items in existing Editor Manager menus
	emMenuRegister(editor, emMenuCreate(editor, "File", "command", "custom", NULL));

	// TOOLBAR EXAMPLES
	toolbar = emToolbarCreate(0);
	button = ui_ButtonCreate("MD Button 1", 0, 0, NULL, NULL);
	emToolbarAddChild(toolbar, button, false);
	emToolbarAddChild(toolbar, ui_ButtonCreate("MD Button 2", elUINextX(button) + 5, 0, NULL, NULL), true);
	eaPush(&editor->toolbars, toolbar);
}

static void emTestMultiDocUIInit(EMEditorDoc *doc)
{
	UIWindow *mainWin;
	UITextArea *textArea;
	UILabel *label;
	UIButton *button;
	UITextEntry *entry;
	EMPanel *panel;

	// WINDOW EXAMPLES
	mainWin = ui_WindowCreate("MD Main Window", 300, 300, 500, 500);
	textArea = ui_TextAreaCreate("");
	ui_WidgetSetPadding(UI_WIDGET(textArea), 5, 5);
	ui_WidgetSetDimensionsEx(UI_WIDGET(textArea), 1, 1, UIUnitPercentage, UIUnitPercentage);
	ui_WindowAddChild(mainWin, textArea);
	eaPush(&doc->ui_windows, mainWin);
	doc->primary_ui_window = mainWin;
	eaPush(&doc->ui_windows_private, ui_WindowCreate("MD Private Window", 100, 100, 150, 300));

	// PANEL EXAMPLES
	panel = emPanelCreate("MD Tab", "MD Panel 1", 0);
	label = ui_LabelCreate("Input:", 0, 0);
	emPanelAddChild(panel, label, false);
	entry = ui_TextEntryCreate("[value]", elUINextX(label) + 5, 0);
	emPanelAddChild(panel, entry, false);
	button = ui_ButtonCreate("OK", elUINextX(entry) + 5, 0, NULL, NULL);
	emPanelAddChild(panel, button, true);
	eaPush(&doc->em_panels, panel);
	panel = emPanelCreate("MD Tab", "MD Panel 2", 0);
	label = ui_LabelCreate(doc->doc_display_name, 0, 0);
	emPanelAddChild(panel, label, true);
	eaPush(&doc->em_panels, panel);
}

/******
* This function handles new document calls from the menu or the toolbar.  This callback is
* responsible for allocating memory for the new document and populating it with the necessary
* UI elements.
* PARAMS:
*   type - string type of document to create; it is possible to create a single editor that
*          operates on multiple types of data, so this parameter is used to differentiate
*          between the available types
* RETURNS:
*   EMEditorDoc created; if this is NULL, then the Editor Manager will cancel the new document
******/
static EMEditorDoc *emTestMultiDocNew(const char *type, void *unused)
{
	EMEditorDoc *newDoc = calloc(1, sizeof(EMEditorDoc));

	strcpy(newDoc->doc_display_name, "MD New Doc");
	emTestMultiDocUIInit(newDoc);
	return newDoc;
}

/******
* This function handles events that close a document.  This callback is responsible for freeing
* memory allocated for the document and freeing all UI elements used for document.
* PARAMS:
*   doc - EMEditorDoc being closed
******/
static void emTestMultiDocClose(EMEditorDoc *doc)
{
	int i;

	for (i = 0; i < eaSize(&doc->ui_windows); i++)
		ui_WidgetQueueFree(UI_WIDGET(doc->ui_windows[i]));
	for (i = 0; i < eaSize(&doc->ui_windows_private); i++)
		ui_WidgetQueueFree(UI_WIDGET(doc->ui_windows_private[i]));

	free(doc);
}

/******
* This function handles the creation of new docs via the opening/loading of existing data.  This
* generally occurs through the EMPicker UI.  The function is responsible for the same tasks
* as the new document callback, except that the function takes a name so that
* the document can populate the UI elements with the data being loaded that corresponds to that
* name.
* PARAMS:
*   name - string name (can be a reference string or a filename or any identifying string)
*          corresponding to the data being loaded
*   type - string type of data being loaded (in case the editor can handle multiple file types)
* RETURNS:
*   EMEditorDoc for the loaded data
******/
static EMEditorDoc *emTestMultiDocLoad(const char *name, const char *type)
{
	EMEditorDoc *newDoc = calloc(1, sizeof(EMEditorDoc));
	UIWindow *mainWin = ui_WindowCreate(name, 100, 100, 500, 500);
	UITextArea *text;
	char *str = NULL;
	char temp[1024];
	char filename[MAX_PATH];
	FILE *file;

	fileLocateWrite(name, filename);
	file = fopen(filename, "r");
	if (!file)
		return NULL;

	estrStackCreate(&str);

	while (fgets(temp, ARRAY_SIZE_CHECKED(temp), file))
		estrAppend2(&str, temp);
	fclose(file);
	strcpy(newDoc->doc_display_name, name);
	eaPush(&newDoc->ui_windows, mainWin);
	if (!str)
		estrAppend2(&str, "[NULL]");
	text = ui_TextAreaCreate(str);
	ui_TextAreaSetChangedCallback(text, emTestMultiDocTextChanged, newDoc);
	ui_WidgetSetDimensionsEx(UI_WIDGET(text), 1, 1, UIUnitPercentage, UIUnitPercentage);
	ui_WindowAddChild(mainWin, text);
	newDoc->primary_ui_window = mainWin;
	emDocAssocFile(newDoc, name);
	emDocAssocFile(newDoc, "maps/_joseph/testdata/test1.txt");
	estrDestroy(&str);
	return newDoc;
}

/******
* This function is responsible for saving the editor's document contents to disk.
* PARAMS:
*   doc - EMEditorDoc to save
******/
static EMTaskStatus emTestMultiDocSave(EMEditorDoc *doc)
{
	UITextArea *text = (UITextArea*) doc->primary_ui_window->widget.children[0];
	EMFile **files = NULL;
	const char *write_text;
	FILE *file = NULL;

	if (!file || !text)
		return EM_TASK_FAILED;

	emDocGetFiles(doc, &files, false);
	if (eaSize(&files) > 0)
		file = fopen(files[0]->filename, "w");

	write_text = ui_TextAreaGetText(text);
	fwrite(write_text, 1, strlen(write_text), file);
	fclose(file);
	return EM_TASK_SUCCEEDED;
}

static void emTestMultiDocCopy(EMEditorDoc *doc, bool cut)
{
	EMDocData *copyData = StructCreate(parse_EMDocData);

	copyData->data = StructAllocString("testingdata123");
	copyData->filename = StructAllocString("testingfilename123");

	emAddToClipboardParsed(parse_EMDocData, copyData);
	StructDestroy(parse_EMDocData, copyData);
}

static void emTestMultiDocPaste(EMEditorDoc *doc, ParseTable *pti, const char *custom_type, void *data)
{
	EMDocData *docData = (EMDocData*) data;
	if (!pti || pti != parse_EMDocData)
		return;


	emStatusPrintf("pasting data: %s:%s", docData->data, docData->filename);
}

/******
* This function handles clicks on the picker to set the name and type that will subsequently
* be passed to the open document function when the user confirms the selection.
* PARAMS:
*   picker - EMPicker picker being navigated
*   selection - struct data that is currently highlighted in the auto-created tree
******/
static bool emTestMultiDocDataClicked(EMPicker *picker, EMPickerSelection *selection)
{
	EMDocData *data = (EMDocData*) selection->data;

	// ensure correct data is being passed to this callback
	assert(selection->table == parse_EMDocData);

	// set the name/type of the data to be opened in the appropriate editor
	strcpy(selection->doc_name, data->filename);
	strcpy(selection->doc_type, "emtestmd");

	return true;
}

/******
* This function initializes the picker by specifying the various types of tree nodes and their
* behavior.
* PARAMS:
*   mdPicker - EMPicker to initialize
******/
static void emTestMultiDocBrowserInit(EMPicker *mdPicker)
{
	EMPickerDisplayType *type;
	EMDocContainer *mdData = calloc(1, sizeof(*mdData));

	emTestDocCreateData2(mdData);

	mdPicker->display_data_root = mdData;
	mdPicker->display_parse_info_root = parse_EMDocContainer;

	type = calloc(1, sizeof(*type));
	type->parse_info = parse_EMDocContainer;
	type->display_name_parse_field = "name";
	type->is_leaf = 0;
	eaPush(&mdPicker->display_types, type);

	type = calloc(1, sizeof(*type));
	type->selected_func = emTestMultiDocDataClicked;
	type->parse_info = parse_EMDocData;
	type->display_name_parse_field = "data";
	type->is_leaf = 1;
	eaPush(&mdPicker->display_types, type);
}

/******
* Editors and pickers should be registered in an AUTO_RUN.  All UI setup, however, should go into the 
* initialization functions.
******/
#endif
AUTO_RUN;
void emTestRegisterMultiDocEditor(void)
{
#ifndef NO_EDITORS
	if (emTestEnabled && !isProductionMode())
	{
		EMEditor *mdEditor = calloc(1, sizeof(EMEditor));
		EMPicker *mdBrowser = calloc(1, sizeof(EMPicker));

		// editor params
		mdEditor->type = EM_TYPE_MULTIDOC;
		strcpy(mdEditor->editor_name, "EMTEST-MULTIDOC");
		mdEditor->default_type = "emtestmd";

		// reload behaviors
		mdEditor->reload_prompt = 1;
		mdEditor->allow_multiple_docs = true;

		// workspace
		strcpy(mdEditor->default_workspace, "EMTEST WS");

		// callbacks
		mdEditor->init_func = emTestMultiDocEditorInit;
		mdEditor->new_func = emTestMultiDocNew;
		mdEditor->load_func = emTestMultiDocLoad;
		mdEditor->reload_func = emReopenDoc;
		mdEditor->close_func = emTestMultiDocClose;
		mdEditor->save_func = emTestMultiDocSave;
		mdEditor->copy_func = emTestMultiDocCopy;
		mdEditor->paste_func = emTestMultiDocPaste;

		// pickers
		mdBrowser->init_func = emTestMultiDocBrowserInit;

		strcpy(mdBrowser->picker_name, "EMTEST-MULTIDOC-BROWSER");
		eaPush(&mdEditor->pickers, mdBrowser);

		emRegisterEditor(mdEditor);
		emRegisterFileType("emtestmd", "test doc", mdEditor->editor_name);
	}
#endif
}
#ifndef NO_EDITORS

static EMEditorDoc *simpleEditorNew(const char *type, void *data)
{
	EMEditorDoc *newDoc = calloc(1, sizeof(*newDoc));

	strcpy(newDoc->doc_name, "ND");
	sprintf(newDoc->doc_display_name, "%s [%s]", newDoc->doc_name, type);
	strcpy(newDoc->doc_type, type);
	newDoc->primary_ui_window = ui_WindowCreate("New document", 100, 100, 200, 100);

	return newDoc;
}

static void simpleEditorClose(EMEditorDoc *doc)
{
	ui_WidgetQueueFree(UI_WIDGET(doc->primary_ui_window));
	free(doc);
}

/*AUTO_COMMAND;
void simpleEditorShowWorld(int show_world)
{
	emEditorHideWorld(NULL, !show_world);
}*/

#endif
AUTO_RUN;
void simpleEditorRegister(void)
{
#ifndef NO_EDITORS
	if (emTestEnabled && !isProductionMode())
	{
		EMEditor *simpleEditor = calloc(1, sizeof(EMEditor));

		// editor params
		simpleEditor->type = EM_TYPE_SINGLEDOC;
		simpleEditor->allow_multiple_docs = 1;
//		simpleEditor->hide_world = 1;
		simpleEditor->use_em_cam_keybinds = 1;
		strcpy(simpleEditor->editor_name, "SimpleEditor");
		strcpy(simpleEditor->default_workspace, "Environment Editors");
		simpleEditor->default_type = "simpletype";
		simpleEditor->always_open = 1;

		// callbacks
		simpleEditor->new_func = simpleEditorNew;
		simpleEditor->close_func = simpleEditorClose;

		emRegisterEditor(simpleEditor);
		emRegisterFileType("simpletype", "Simple Type doc", simpleEditor->editor_name);
		emRegisterFileType("simpletype2", "Simple Type 2 doc", simpleEditor->editor_name);
	}
#endif
}

#include "EditorManagerTest_c_ast.c"