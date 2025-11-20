GCC_SYSTEM

#include "EditLibClipboard.h"
#include "Expression.h"
#include "StateMachine.h"

#ifndef NO_EDITORS
#include "EditorManager.h"
#include "file.h"
#include "EString.h"
#include "GfxTexAtlas.h"
#include "EditLibUIUtil.h"

#include "EditLibClipboard_h_ast.h"
#include "EditLibClipboard_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

typedef struct EcbClip EcbClip;

static void ecbCreate(void);
static void ecbRenameClipCreateUI(EcbClip *clip);

/******
* CLIP TYPE MANAGEMENT
******/
// note to developers:
// adding a "clippable" struct type (which must be AUTO_STRUCTED) involves the following steps:
// 1) adding EcbClipType enum corresponding to the struct
// 2) register EcbClipType
// TODO: 3 & 4 should go away after Ben/Alex implement serializing different types in AUTO_STRUCT
// 3) adding pointer to the struct type in EcbClip (and making sure the parse table is accessible by AUTO_STRUCT)
// 4) adding appropriate case to ecbGetClipPointer

typedef struct EcbClipType
{
	EcbClipTypeEnum type;
	EcbMakeTextCallback displayFunc;
	ParseTable *pti;
} EcbClipType;

static StashTable ecbAllClipTypes;

void ecbRegisterClipType(EcbClipTypeEnum type, EcbMakeTextCallback displayFunc, ParseTable *pti)
{
	EcbClipType *newType = calloc(1, sizeof(EcbClipType));
	if (!ecbAllClipTypes)
		ecbAllClipTypes = stashTableCreateInt(16);
	newType->type = type;
	newType->pti = pti;
	newType->displayFunc = displayFunc;
	stashIntAddPointer(ecbAllClipTypes, type, newType, false);
}

const char *ecbClipTypeToString(EcbClipTypeEnum type)
{
	return StaticDefineIntRevLookup(EcbClipTypeEnumEnum, type);
}

static EcbClipType *ecbGetClipType(EcbClipTypeEnum type)
{
	EcbClipType *retType;
	if (stashIntFindPointer(ecbAllClipTypes, type, &retType))
		return retType;
	return NULL;
}

/******
* CLIP AND CLIPBOARD MANAGEMENT
******/
#endif

typedef struct ExprLine ExprLine;

AUTO_STRUCT;
typedef struct EcbClip
{
	char *displayName;
	EcbClipTypeEnum type;

	// Specific struct pointers (TODO: wait for Ben and Alex to allow for serializing void*'s into a single file)
	// FSM and Expression Editors
	Expression *cbExpr;		AST(LATEBIND)
	ExprLine *cbExprLine;	AST(LATEBIND)
	FSMState *cbState;
	FSMStates *cbStates;
} EcbClip;

AUTO_STRUCT;
typedef struct EcbClipboard
{
	EcbClip **clips;
} EcbClipboard;
#ifndef NO_EDITORS

typedef struct EcbClipboardUI
{
	EMPanel *mainPanel;
	UITextEntry *textFilter;
	UIComboBox *comboFilter;
	UIList *clipList;
	EcbClipboard clipboard;
	EcbClip **visClips;
	struct
	{
		UIWindow *modalWin;
		UITextEntry *input;
	} modalData;
} EcbClipboardUI;

static EcbClipboardUI ecbGlobalUI;

EMPanel *ecbGetPanel(void)
{
	if (!ecbGlobalUI.mainPanel)
		return ecbLoad();
	else
		return ecbGlobalUI.mainPanel;
}

static void **ecbGetClipPointer(EcbClip *clip)
{
	switch (clip->type)
	{
		xcase ECB_EXPRESSION:
			return &clip->cbExpr;
		xcase ECB_EXPR_LINE:
			return &clip->cbExprLine;
		xcase ECB_STATE:
			return &clip->cbState;
		xcase ECB_STATES:
			return &clip->cbStates;
		xdefault:
			assertmsg(0, "Clipboard clip pointer not being handled!");
			return NULL;
	}
}

static void ecbReloadVisClips(void)
{
	int i;
	EcbClip *selected = ui_ListGetSelectedObject(ecbGlobalUI.clipList);

	eaClear(&ecbGlobalUI.visClips);

	for (i = 0; i < eaSize(&ecbGlobalUI.clipboard.clips); i++)
	{
		EcbClip *currClip = ecbGlobalUI.clipboard.clips[i];
		EcbClipTypeEnum cbSel = ui_ComboBoxGetSelected(ecbGlobalUI.comboFilter);
		const char *filterText = ui_TextEntryGetText(ecbGlobalUI.textFilter);

		// apply filters to clipboard items
		if (cbSel != 0 && currClip->type != cbSel)
			continue;
		if (strlen(filterText) > 0 && !strstri(currClip->displayName, filterText))
			continue;

		eaPush(&ecbGlobalUI.visClips, currClip);
	}

	ui_ListSetSelectedObjectAndCallback(ecbGlobalUI.clipList, selected);
}

/******
* This function loads the contents of the clipboard from file, creating the UI if it hasn't
* been done already.
* RETURNS:
*   EMPanel of the clipboard
******/
EMPanel *ecbLoad(void)
{
	char buf[260];

	if (!ecbGlobalUI.mainPanel)
		ecbCreate();

	// reload the contents of the clipboard from file
	eaDestroyStruct(&ecbGlobalUI.clipboard.clips, parse_EcbClip);
	sprintf(buf, "%s/editor/clipboard.txt", fileLocalDataDir());
	ParserLoadFiles(NULL, buf, NULL, 0, parse_EcbClipboard, &ecbGlobalUI.clipboard);
	ecbReloadVisClips();

	return ecbGlobalUI.mainPanel;
}

/******
* This function saves the clipboard file to disk.
******/
void ecbSave(void)
{
	char buf[260];
	sprintf(buf, "%s/editor/clipboard.txt", fileLocalDataDir());
	ParserWriteTextFile(buf, parse_EcbClipboard, &ecbGlobalUI.clipboard, 0, 0);
}

/******
* This function adds a new clip to the clipboard.
* PARAMS:
*   obj - void pointer to object being stored on the clipboard
*   type - EcbClipTypeEnum corresponding to obj's type
******/
int ecbAddClip(void *obj, EcbClipTypeEnum type)
{
	EcbClip *newClip = StructCreate(parse_EcbClip);
	EcbClipType *clipType = ecbGetClipType(type);
	void *copiedObj = StructCreateVoid(clipType->pti);
	char *estrDispName = NULL;
	estrStackCreate(&estrDispName);

	// copy the object as a clip
	StructCopyFieldsVoid(clipType->pti, obj, copiedObj, 0, 0);
	newClip->type = type;
	clipType->displayFunc(obj, &estrDispName);
	newClip->displayName = StructAllocString(estrDispName);
	estrDestroy(&estrDispName);

	// set the appropriate pointer
	*ecbGetClipPointer(newClip) = copiedObj;

	eaPush(&ecbGlobalUI.clipboard.clips, newClip);
	ecbReloadVisClips();
	return eaSize(&ecbGlobalUI.clipboard.clips) - 1;
}

EcbClipTypeEnum ecbGetClipByIndex(int i, void **obj)
{
	assert(i >= 0 && i < eaSize(&ecbGlobalUI.clipboard.clips));
	*obj = *ecbGetClipPointer(ecbGlobalUI.clipboard.clips[i]);
	return ecbGlobalUI.clipboard.clips[i]->type;
}

/******
* This function looks for an object in the clipboard and removes it if it's there.
* PARAMS:
*   obj - pointer to the object to remove
******/
void ecbRemoveClip(void *obj)
{
	int i;
	for (i = 0; i < eaSize(&ecbGlobalUI.clipboard.clips); i++)
	{
		if (obj == *ecbGetClipPointer(ecbGlobalUI.clipboard.clips[i]))
		{
			StructDestroyVoid(ecbGetClipType(ecbGlobalUI.clipboard.clips[i]->type)->pti, obj);
			eaRemove(&ecbGlobalUI.clipboard.clips, i);
			ecbReloadVisClips();
			break;
		}
	}
}

/******
* This function removes an object from the clipboard at a specified index.
* PARAMS:
*   i - the index of the clipboard object to remove
******/
void ecbRemoveClipByIndex(int i)
{
	if (i >= 0 && i < eaSize(&ecbGlobalUI.clipboard.clips))
	{
		StructDestroyVoid(ecbGetClipType(ecbGlobalUI.clipboard.clips[i]->type)->pti, *ecbGetClipPointer(ecbGlobalUI.clipboard.clips[i]));
		eaRemove(&ecbGlobalUI.clipboard.clips, i);
		ecbReloadVisClips();
	}
}

/******
* CALLBACKS
******/
static void ecbTextFilterChanged(UITextEntry *entry, UserData unused)
{
	ecbReloadVisClips();
}

static void ecbComboFilterChanged(UIComboBox *combo, UserData unused)
{
	ecbReloadVisClips();
}

static void ecbListDrop(UIWidget *source, UIList *dest, UIDnDPayload *payload, UserData unused)
{
	EcbClipTypeEnum typeEnum = StaticDefineIntGetInt(EcbClipTypeEnumEnum, payload->type);
	EcbClipType *type = ecbGetClipType(typeEnum);

	if (type && payload->payload && source != (UIWidget*) dest)
	{
		ecbAddClip(payload->payload, typeEnum);
	}
}

static void ecbListDrag(UIList *list, UserData unused)
{
	EcbClip *clip = ui_ListGetSelectedObject(list);

	if (clip)
		ui_DragStart((UIWidget*) list, ecbClipTypeToString(clip->type), *ecbGetClipPointer(clip), atlasLoadTexture("button_pinned.tga"));
}

static void ecbListActivated(UIList *list, UserData unused)
{
	EcbClip *clip = ui_ListGetSelectedObject(list);
	if (clip)
		ecbRenameClipCreateUI(clip);
}

static void ecbShuttleTopClicked(UIButton *button, UIList *list)
{
	EcbClip *clip = ui_ListGetSelectedObject(list);
	if (clip)
	{
		EcbClip *topClip = ecbGlobalUI.visClips[0];
		int destIdx = eaFind(&ecbGlobalUI.clipboard.clips, topClip);
		eaMove(&ecbGlobalUI.clipboard.clips, destIdx, eaFind(&ecbGlobalUI.clipboard.clips, clip));
		ecbReloadVisClips();
		ui_ListSetSelectedObjectAndCallback(list, clip);
	}
}

static void ecbShuttleUpClicked(UIButton *button, UIList *list)
{
	EcbClip *clip = ui_ListGetSelectedObject(list);
	if (clip)
	{
		EcbClip *aboveClip = ecbGlobalUI.visClips[eaFind(&ecbGlobalUI.visClips, clip) - 1];
		int destIdx = eaFind(&ecbGlobalUI.clipboard.clips, aboveClip);
		eaMove(&ecbGlobalUI.clipboard.clips, destIdx, eaFind(&ecbGlobalUI.clipboard.clips, clip));
		ecbReloadVisClips();
		ui_ListSetSelectedObjectAndCallback(list, clip);
	}
}

static void ecbShuttleDownClicked(UIButton *button, UIList *list)
{
	EcbClip *clip = ui_ListGetSelectedObject(list);
	if (clip)
	{
		EcbClip *belowClip = ecbGlobalUI.visClips[eaFind(&ecbGlobalUI.visClips, clip) + 1];
		int destIdx = eaFind(&ecbGlobalUI.clipboard.clips, belowClip);
		eaMove(&ecbGlobalUI.clipboard.clips, destIdx, eaFind(&ecbGlobalUI.clipboard.clips, clip));
		ecbReloadVisClips();
		ui_ListSetSelectedObjectAndCallback(list, clip);
	}
}

static void ecbShuttleBottomClicked(UIButton *button, UIList *list)
{
	EcbClip *clip = ui_ListGetSelectedObject(list);
	if (clip)
	{
		EcbClip *bottomClip = ecbGlobalUI.visClips[eaSize(&ecbGlobalUI.visClips) - 1];
		int destIdx = eaFind(&ecbGlobalUI.clipboard.clips, bottomClip);
		eaMove(&ecbGlobalUI.clipboard.clips, destIdx, eaFind(&ecbGlobalUI.clipboard.clips, clip));
		ecbReloadVisClips();
		ui_ListSetSelectedObjectAndCallback(list, clip);
	}
}

static void ecbDeleteButtonClicked(UIButton *button, UIList *list)
{
	EcbClip *clip = ui_ListGetSelectedObject(list);
	if (clip)
		ecbRemoveClip(*ecbGetClipPointer(clip));
}

static void ecbRenameButtonClicked(UIButton *button, UIList *list)
{
	ecbListActivated(list, NULL);
}

static void ecbSaveButtonClicked(UIButton *button, UserData unused)
{
	ecbSave();
}

static void ecbRenameClipCancel(UIButton *button, UIWindow *win)
{
	ui_WindowClose(win);
}

static void ecbRenameClipOK(UIWidget *widget, EcbClip *clip)
{
	if (clip && clip->displayName)
		StructFreeString(clip->displayName);

	if (clip)
		clip->displayName = StructAllocString(ui_TextEntryGetText(ecbGlobalUI.modalData.input));
	ui_WindowClose(ecbGlobalUI.modalData.modalWin);
}

/******
* UI
******/
/******
* This function initializes the main UI.
******/
static void ecbCreate(void)
{
	const int shuttleBtnSize = 15;

	EMPanel *panel;
	UIList *list;
	UIListColumn *col;
	UILabel *label;
	UIButton *button;
	UITextEntry *entry;
	UIComboBox *combo;
	int listBottomPad, listRightPad;
	const void ***clipTypes = calloc(1, sizeof(void**));
	U32 i;

	// create the combo box model
	eaPush(clipTypes, "--NO FILTER--");
	for (i = 0; i < stashGetOccupiedSlots(ecbAllClipTypes); i++)
		eaPush(clipTypes, StaticDefineIntRevLookup(EcbClipTypeEnumEnum, i + 1));

	panel = emPanelCreate("Utilities", "Clipboard", 600);
	ecbGlobalUI.mainPanel = panel;
	combo = ui_ComboBoxCreate(0, 0, 100, NULL, clipTypes, NULL);
	combo->widget.width = 1;
	combo->widget.widthUnit = UIUnitPercentage;
	ui_ComboBoxSetSelectedCallback(combo, ecbComboFilterChanged, NULL);
	ui_ComboBoxSetSelected(combo, 0);
	emPanelAddChild(panel, combo, false);
	ecbGlobalUI.comboFilter = combo;

	label = ui_LabelCreate("Search:", 0, elUINextY(combo) + 5);
	emPanelAddChild(panel, label, false);
	entry = ui_TextEntryCreate("", 0, label->widget.y);
	ui_TextEntrySetChangedCallback(entry, ecbTextFilterChanged, NULL);
	entry->widget.width = 1;
	entry->widget.widthUnit = UIUnitPercentage;
	entry->widget.leftPad = elUINextX(label) + 5;
	emPanelAddChild(panel, entry, false);
	ecbGlobalUI.textFilter = entry;

	list = ui_ListCreate(parse_EcbClip, &ecbGlobalUI.visClips, 16);
	col = ui_ListColumnCreate(UIListPTName, "Type", (intptr_t) "type", NULL);
	ui_ListColumnSetWidth(col, false, 70);
	ui_ListAppendColumn(list, col);
	ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Object", (intptr_t) "displayName", NULL));
	ui_ListSetActivatedCallback(list, ecbListActivated, NULL);
	ui_WidgetSetPosition(UI_WIDGET(list), 0, 0);
	ui_WidgetSetDimensionsEx(UI_WIDGET(list), 1, 1, UIUnitPercentage, UIUnitPercentage);

	button = ui_ButtonCreate("Delete", 0, 0, ecbDeleteButtonClicked, list);
	button->widget.width = 0.3333;
	button->widget.widthUnit = UIUnitPercentage;
	button->widget.offsetFrom = UIBottomLeft;
	emPanelAddChild(panel, button, false);
	button = ui_ButtonCreate("Rename", 0, 0, ecbRenameButtonClicked, list);
	button->widget.width = 0.3333;
	button->widget.widthUnit = UIUnitPercentage;
	button->widget.offsetFrom = UIBottomLeft;
	button->widget.xPOffset = 0.3333;
	emPanelAddChild(panel, button, false);
	button = ui_ButtonCreate("Save", 0, 0, ecbSaveButtonClicked, list);
	button->widget.width = 0.3333;
	button->widget.widthUnit = UIUnitPercentage;
	button->widget.offsetFrom = UIBottomLeft;
	button->widget.xPOffset = 0.6666;
	emPanelAddChild(panel, button, false);
	listBottomPad = elUINextY(button);

	button = ui_ButtonCreateImageOnly("eui_arrow_large_up", 0, 0, ecbShuttleTopClicked, list);
	ui_WidgetSetPositionEx((UIWidget*) button, 0, 0, 0, 0.2, UITopRight);
	ui_WidgetSetDimensions((UIWidget*) button, shuttleBtnSize, shuttleBtnSize + 10);
	ui_ButtonSetImageStretch(button, true);
	emPanelAddChild(panel, button, false);
	button = ui_ButtonCreateImageOnly("eui_arrow_large_up", 0, 0, ecbShuttleUpClicked, list);
	ui_WidgetSetPositionEx((UIWidget*) button, 0, 0, 0, 0.4, UITopRight);
	ui_WidgetSetDimensions((UIWidget*) button, shuttleBtnSize, shuttleBtnSize);
	ui_ButtonSetImageStretch(button, true);
	emPanelAddChild(panel, button, false);
	button = ui_ButtonCreateImageOnly("eui_arrow_large_down", 0, 0, ecbShuttleDownClicked, list);
	ui_WidgetSetPositionEx((UIWidget*) button, 0, 0, 0, 0.6, UITopRight);
	ui_WidgetSetDimensions((UIWidget*) button, shuttleBtnSize, shuttleBtnSize);
	ui_ButtonSetImageStretch(button, true);
	emPanelAddChild(panel, button, false);
	button = ui_ButtonCreateImageOnly("eui_arrow_large_down", 0, 0, ecbShuttleBottomClicked, list);
	ui_WidgetSetPositionEx((UIWidget*) button, 0, 0, 0, 0.8, UITopRight);
	ui_WidgetSetDimensions((UIWidget*) button, shuttleBtnSize, shuttleBtnSize + 10);
	ui_ButtonSetImageStretch(button, true);
	emPanelAddChild(panel, button, false);
	listRightPad = elUINextX(button) + 5;

	ui_WidgetSetDropCallback(UI_WIDGET(list), ecbListDrop, NULL);
	ui_WidgetSetDragCallback(UI_WIDGET(list), ecbListDrag, NULL);
	ui_WidgetSetPaddingEx(UI_WIDGET(list), 0, listRightPad, elUINextY(label) + 5, listBottomPad);
	emPanelAddChild(panel, list, false);
	ecbGlobalUI.clipList = list;
}

static void ecbRenameClipCreateUI(EcbClip *clip)
{
	UIWindow *win;
	UILabel *label;
	UITextEntry *entry;
	UIButton *button;

	win = ui_WindowCreate("Rename Clip", 0, 0, 300, 75);
	elUICenterWindow(win);
	label = ui_LabelCreate("Clip Name:", 5, 5);
	ui_WindowAddChild(win, label);
	entry = ui_TextEntryCreate(clip->displayName, 0, label->widget.y);
	ui_TextEntrySetEnterCallback(entry, ecbRenameClipOK, clip);
	ui_WidgetSetDimensionsEx((UIWidget*) entry, 1, entry->widget.height, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPaddingEx((UIWidget*) entry, elUINextX(label) + 5, 5, 0, 0);
	ui_WindowAddChild(win, entry);
	button = ui_ButtonCreate("OK", 5, 5, ecbRenameClipOK, clip);
	button->widget.offsetFrom = UIBottomRight;
	ui_WindowAddChild(win, button);
	button = ui_ButtonCreate("Cancel", elUINextX(button) + 5, button->widget.y, ecbRenameClipCancel, win);
	button->widget.offsetFrom = UIBottomRight;
	ui_WindowAddChild(win, button);
	ui_WindowSetModal(win, true);
	ecbGlobalUI.modalData.modalWin = win;
	ecbGlobalUI.modalData.input = entry;
	ui_WindowShow(win);
	ui_SetFocus(entry);
}

#endif

// include auto-generated files
#include "EditLibClipboard_h_ast.c"
#include "EditLibClipboard_c_ast.c"
