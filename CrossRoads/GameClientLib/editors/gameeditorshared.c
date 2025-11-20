/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "aiJobEditor.h"
#include "CharacterClass.h"
#include "contact_common.h"
#include "editlib.h"
#include "EditLibGizmos.h"
#include "encounter_common.h"
#include "EncounterEditor.h"
#include "entCritter.h"
#include "Expression.h"
#include "ExpressionEditor.h"
#include "gameaction_common.h"
#include "gameeditorshared.h"
#include "ItemAssignments.h"
#include "GfxPrimitive.h"
#include "mission_common.h"
#include "MultiEditField.h"
#include "NotifyCommon.h"
#include "ObjectLibrary.h"
#include "partition_enums.h"
#include "quat.h"
#include "ShardVariableCommon.h"
#include "StateMachine.h"
#include "stringcache.h"
#include "StringUtil.h"
#include "wlEncounter.h"
#include "wlGroupPropertyStructs.h"
#include "WorldColl.h"
#include "worldgrid.h"
#include "WorldLib.h"
#include "ChoiceTable_common.h"
#include "WorldEditorAttributesHelpers.h"
#include "ActivityLogCommon.h"
#include "Guild.h"
#include "dynFxInfo.h"
#include "bounds.h"

#include "AutoGen/AnimList_Common_h_ast.h"
#include "AutoGen/Cutscene_Common_h_ast.h"
#include "AutoGen/mission_common_h_ast.h"
#include "AutoGen/GameAction_common_h_ast.h"
#include "Autogen/entEnums_h_ast.h"
#include "AutoGen/StateMachine_h_ast.h"
#include "AutoGen/ActivityLogEnums_h_ast.h"
#include "autogen/NotifyEnum_h_ast.h"
#include "Autogen/Guild_h_ast.h"
#include "AutoGen/ItemAssignments_h_ast.h"
#include "AutoGen/wlGroupPropertyStructs_h_ast.h"

#include "soundLib.h"
#include "oldencounter_common.h"
#include "encounter_common.h"
#include "AutoGen/oldencounter_common_h_ast.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

// ----------------------------------------------------------------------------------
// Type definitions
// ----------------------------------------------------------------------------------

// used by GECreateExpressionEditButton
typedef struct GEExprEditCallback
{
	UIButton *button;
	ExprEdExprFunc func;
	UserData data;
} GEExprEditCallback;

typedef struct GEJobEditorData{
	AIJobDesc *job;
	EncounterPropUI *encPropUI;
} GEJobEditorData;

// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

const char** g_GEMapDispNames = NULL;
const char** g_GEPatrolRouteNames = NULL;
bool g_InClickableAttachMode = false;

static int s_ActorCritterIndex;
static int s_ActorRankIndex;
static int s_ActorSubRankIndex;
static int s_ActorFactionIndex;
static int s_ActorContactIndex;
static int s_ActorSpawnCondIndex;

static int s_EncounterSpawnCondIndex;
static int s_EncounterSuccessCondIndex;
static int s_EncounterFailCondIndex;
static int s_EncounterWaveCondIndex;
static int s_EncounterWaveIntervalIndex;
static int s_EncounterWaveMinDelayIndex;
static int s_EncounterWaveMaxDelayIndex;
static int s_EncounterSpawnChanceIndex;
static int s_EncounterSpawnRadiusIndex;
static int s_EncounterLockoutRadiusIndex;
static int s_EncounterRespawnTimerIndex;
static int s_EncounterGangIDIndex;

char** g_GEPowerStrengthDispNames = NULL;

#define ENC_DETAIL_DIST_SQ (250000*g_GEDrawScale) // 500 squared
#define ENC_AGGRO_DIST_SQ (1000000*g_GEDrawScale) // 1000 squared

#define CHECK_ACTORINFO_FIELD(boolField, compareField) if (boolField && TokenCompare(parse_OldActorInfo, compareField, firstActorInfo, currActorInfo, 0, 0)) boolField = false
#define CHECK_ENCOUNTER_FIELD(boolField, compareField) if (boolField && TokenCompare(parse_EncounterDef, compareField, firstEncDef, currEncDef, 0, 0)) boolField = false

#define GE_ACTLABEL_WIDTH 80
#define MAX_FSM_VARS 512
#define GE_MARQUEEDIST 1000000 // 1000 squared
#define GE_MINCOPYDIST 225 // 15 pixels squared

// Colors
static Vec4 gvFailedColor   = { 0.886275, 0, 0, 1.0 };
static Vec4 gvGainedColor   = { 0, 0.886275, 0, 1.0 };
static Vec4 gvProgressColor = { 0, 0, 0.886275, 1.0 };

#define X_OFFSET_BASE     15
#define X_OFFSET_INDENT   30
#define X_OFFSET_CONTROL  115
#define X_OFFSET_CONTROL2 135
#define X_OFFSET_SPACING  80
#define Y_OFFSET_ROW      26
#define Y_OFFSET_SPACING  80

#endif

// Since these are accessed by commands, they have to exist even if no editors
F32 g_GEDrawScale = 1.0f;
bool g_ShowAggroRadius = false;
int g_EncAggroRadiusOverride = 0;
static int s_GESphereVolumeDetail = 12;

AUTO_CMD_FLOAT(g_GEDrawScale, ELEDrawScale) ACMD_CMDLINE;
AUTO_CMD_INT(g_ShowAggroRadius, ShowEncounterAggro) ACMD_CMDLINE;
AUTO_CMD_INT(s_GESphereVolumeDetail, ELESphereVolumeDetail) ACMD_CMDLINE;
AUTO_CMD_INT(g_EncAggroRadiusOverride, ELEAggroRadius) ACMD_CMDLINE;

#ifndef NO_EDITORS

// ----------------------------------------------------------------------------------
// Stuff for Messages
// ----------------------------------------------------------------------------------

F32 GECreateMessageEntryEx(UIAnyWidget** msgEntry, UILabel** labelPtr, const char* labelName, 
						   const Message* message, F32 x, F32 y, F32 w, F32 labelWidth, 
						   UIActivationFunc callback, void* cbData, UIAnyWidget *parent, 
						   bool canEditKey, bool canEditScope)
{
	UIMessageEntry *entry = NULL;
	UILabel* label = labelName ? ui_LabelCreate(labelName, x, y) : NULL;

	entry = ui_MessageEntryCreate(message, x + labelWidth, y, w);
	ui_MessageEntrySetChangedCallback(entry, callback, cbData);
	ui_MessageEntrySetCanEditKey(entry, canEditKey);
	ui_MessageEntrySetCanEditScope(entry, canEditScope);

	if (parent)
	{
		if (label) ui_WidgetGroupAdd(&((UIWidget*)parent)->children, (UIWidget*)label);
		ui_WidgetGroupAdd(&((UIWidget*)parent)->children, (UIWidget*)entry);
	}

	if (msgEntry) *msgEntry = entry;
	if (labelPtr) *labelPtr = label;
	
	return y + entry->widget.height + GE_SPACE;
}

void GEGenericUpdateMessage(Message **messageToUpdate, const Message *newMessage, const Message *defaultMessage)
{
	if (messageToUpdate)
	{
		if (!(*messageToUpdate))
			*messageToUpdate = StructClone(parse_Message, defaultMessage);
		else // Update the Key and Scope, in case this was copied from an EncounterDef
		{
			(*messageToUpdate)->pcMessageKey = allocAddString(defaultMessage->pcMessageKey);
			(*messageToUpdate)->pcScope = allocAddString(defaultMessage->pcScope);
			if (!(*messageToUpdate)->pcDescription)
				(*messageToUpdate)->pcDescription = StructAllocString(defaultMessage->pcDescription);
		}

		if ((*messageToUpdate) && newMessage)
		{
			StructFreeString((*messageToUpdate)->pcDefaultString);
			(*messageToUpdate)->pcDefaultString = StructAllocString(newMessage->pcDefaultString);
			if (newMessage->pcDescription)
			{
				StructFreeString((*messageToUpdate)->pcDescription);
				(*messageToUpdate)->pcDescription = StructAllocString(newMessage->pcDescription);
			}
			(*messageToUpdate)->bDoNotTranslate = newMessage->bDoNotTranslate;
			(*messageToUpdate)->bFinal = newMessage->bFinal;
		}
	}
}

bool GECompareMessages(const DisplayMessage *displayMsgA, const DisplayMessage *displayMsgB)
{
	Message *a = displayMsgA->pEditorCopy?displayMsgA->pEditorCopy:GET_REF(displayMsgA->hMessage);
	Message *b = displayMsgB->pEditorCopy?displayMsgB->pEditorCopy:GET_REF(displayMsgB->hMessage);
	if (a == b)
		return true;
	if (a && b)
	{
		if (!a->pcDefaultString || !b->pcDefaultString || strcmp(a->pcDefaultString, b->pcDefaultString))
			return false;
		if (!a->pcDescription || !b->pcDescription ||strcmp(a->pcDescription, b->pcDescription))
			return false;
		return true;
	}
	return false;
}

Message *GECreateMessageCopy(const Message *originalMsg, const Message *defaultMsg)
{
	Message *copy = NULL;
	if (originalMsg)
		copy = StructClone(parse_Message, originalMsg);
	if (copy && defaultMsg)
	{
		copy->pcMessageKey = allocAddString(defaultMsg->pcMessageKey);
		copy->pcScope = allocAddString(defaultMsg->pcScope);
		if (!copy->pcDescription)
			copy->pcDescription = StructAllocString(defaultMsg->pcDescription);
	}
	return copy;
}


// ----------------------------------------------------------------------------------
// GEList
// ----------------------------------------------------------------------------------

static void GEListHeightChanged(GEList *list)
{
	F32 newHeight = list->addNewButton->widget.y + list->addNewButton->widget.height + GE_SPACE;
	list->heightChangedCB(list, newHeight, list->userData);
}

static void GEListMoveItemUI(GEListItem *listItem, F32 yChange)
{
	int i, n = eaSize(&listItem->widgets);
	for (i = 0; i < n; i++)
		listItem->widgets[i]->y += yChange;
	listItem->y += yChange;
}

static void GEListItemDestroy(GEListItem *listItem)
{
	int i = 0;
	// Remove all widgets from parent and destroy
	for (i = 0; i < eaSize(&listItem->widgets); i++)
		ui_WidgetRemoveChild((UIWidget*)listItem->parentList->widgetParent, listItem->widgets[i]);
	eaDestroyEx(&listItem->widgets, ui_WidgetQueueFree);

	// Free this MEListItem
	free(listItem);
}

static void GEListDeleteItem(UIButton* button, GEListItem *listItem)
{
	GEList *parentList = listItem->parentList;
	int i = 0;
	int itemPos = eaFindAndRemove(&parentList->listItems, listItem);
	void *item = eaRemove(parentList->items, itemPos);
	int yChange = 0;

	// Calculate how much the other widgets need to move
	if (itemPos < eaSize(&parentList->listItems))
		yChange = parentList->listItems[itemPos]->y - listItem->y;
	else
		yChange = parentList->addNewButton->widget.y - listItem->y;

	// Move all items below this one, and the Add New button
	for (i = itemPos; i < eaSize(&listItem->parentList->listItems); i++)
		GEListMoveItemUI(parentList->listItems[i], -yChange);
	parentList->addNewButton->widget.y -= yChange;

	// Free item
	GEListItemDestroy(listItem);

	// Free item
	parentList->destroyItemFunc(item, parentList->userData);
	
	GEListHeightChanged(parentList);
	if (parentList->setDocUnsavedOnChange)
		GESetCurrentDocUnsaved();
}

static void GEListMoveItemUp(UIButton* button, GEListItem *listItem)
{
	GEList *list = listItem->parentList;
	int i, size = eaSize(&list->listItems);
	F32 yChange;

	// Find index of the item
	i = eaFind(&listItem->parentList->listItems, listItem);
	if (0 < i && i < size)
	{
		// Move UI elements
		yChange = listItem->y - listItem->parentList->listItems[i-1]->y;
		GEListMoveItemUI(listItem, -yChange);
		GEListMoveItemUI(listItem->parentList->listItems[i-1], yChange);
		
		// Swap defs in the def list
		eaSwap(listItem->parentList->items, i, i-1);
		
		// Swap UI elements in UI element list
		eaSwap(&listItem->parentList->listItems, i, i-1);
		
		if (list->setDocUnsavedOnChange)
			GESetCurrentDocUnsaved();
	}
}

static void GEListMoveItemDown(UIButton* button, GEListItem *listItem)
{
	GEList *list = listItem->parentList;
	int i, size = eaSize(&list->listItems);
	F32 yChange;

	// Find index of the item
	i = eaFind(&listItem->parentList->listItems, listItem);
	if (0 <= i && i < size-1)
	{
		// Move UI elements
		yChange = listItem->y - listItem->parentList->listItems[i+1]->y;
		GEListMoveItemUI(listItem, -yChange);
		GEListMoveItemUI(listItem->parentList->listItems[i+1], yChange);
		
		// Swap defs in the def list
		eaSwap(listItem->parentList->items, i, i+1);
		
		// Swap UI elements in UI element list
		eaSwap(&listItem->parentList->listItems, i, i+1);

		if (list->setDocUnsavedOnChange)
			GESetCurrentDocUnsaved();
	}
}

static int GEListAddItem(GEList *list, void *item, F32 y)
{
	GEListItem *listItem = calloc(1, sizeof(GEListItem));
	UIButton *button = NULL;
	F32 currX = list->x;

	if (!item)
	{
		item = list->createItemFunc(list->userData);
		if (item)
			eaPush(list->items, item);
		if (list->setDocUnsavedOnChange)
			GESetCurrentDocUnsaved();
	}

	listItem->parentList = list;
	listItem->y = y;
	
	button = ui_ButtonCreate("X", currX, y, GEListDeleteItem, listItem);
	ui_WidgetAddChild(list->widgetParent, UI_WIDGET(button));
	eaPush(&listItem->widgets, (UIWidget*)button);
	currX += button->widget.width + GE_SPACE;

	if (list->orderable)
	{
		button = ui_ButtonCreate("/\\", currX, y, GEListMoveItemUp, listItem);
		ui_WidgetAddChild(list->widgetParent, UI_WIDGET(button));
		eaPush(&listItem->widgets, (UIWidget*)button);
		currX += button->widget.width + GE_SPACE;

		button = ui_ButtonCreate("\\/", currX, y, GEListMoveItemDown, listItem);
		ui_WidgetAddChild(list->widgetParent, UI_WIDGET(button));
		eaPush(&listItem->widgets, (UIWidget*)button);
		currX += button->widget.width + GE_SPACE;
	}

	y = list->createUIFunc(listItem, &listItem->widgets, item, currX, y, list->widgetParent, list->userData);

	eaPush(&list->listItems, listItem);
	return y;
}

static void GEListAddNewItem(UIButton* button, GEList* list)
{
	int x = list->x;
	F32 y = button->widget.y;
	y = GEListAddItem(list, NULL, y) + GE_SPACE;
	button->widget.y = y;
	GEListHeightChanged(list);
}

GEList* GEListCreate(void*** items, F32 x, F32 y, UIAnyWidget* widgetParent, void *userData, 
					 GEListItemCreateUIFunc createUIFunc, GEListItemCreateFunc createItemFunc, 
					 GEListItemDestroyFunc destroyItemFunc, GEListHeightChangedCB heightChangedCB, 
					 bool orderable, bool setDocUnsavedOnChange)
{
	GEList *list = calloc(1, sizeof(GEList));
	int i, n = eaSize(items);

	list->items = items;
	list->createUIFunc = createUIFunc;
	list->createItemFunc = createItemFunc;
	list->destroyItemFunc = destroyItemFunc;
	list->heightChangedCB = heightChangedCB;
	list->userData = userData;
	list->widgetParent = widgetParent;
	list->orderable = orderable;
	list->setDocUnsavedOnChange = setDocUnsavedOnChange;
	list->x = x;
	list->y = y;

	for (i = 0; i < n; i++)
		y = GEListAddItem(list, (*items)[i], y) + GE_SPACE;

	// Add the add new button
	list->addNewButton = ui_ButtonCreate("Add New", x, y, GEListAddNewItem, list);
	ui_WidgetAddChild(widgetParent, UI_WIDGET(list->addNewButton));
	
	GEListHeightChanged(list);
	return list;
}

void GEListRefresh(GEList *list)
{
	F32 y = list->y;
	int i, n = eaSize(&list->listItems);
	for (i = n-1; i >= 0; --i)
	{
		GEListItem *listItem = list->listItems[i];
		int j, m = eaSize(&listItem->widgets);
		for (j = 0; j < m; ++j)
		{
			ui_WidgetRemoveChild(list->widgetParent, listItem->widgets[j]);
			ui_WidgetQueueFree(listItem->widgets[j]);
		}
		eaDestroy(&listItem->widgets);
		eaRemove(&list->listItems, i);
		free(listItem);
	}
	ui_WidgetRemoveChild(list->widgetParent, UI_WIDGET(list->addNewButton));
	ui_WidgetQueueFree(UI_WIDGET(list->addNewButton));

	n = eaSize(list->items);
	for (i = 0; i < n; i++)
		y = GEListAddItem(list, (*list->items)[i], y) + GE_SPACE;

	// Add the add new button
	list->addNewButton = ui_ButtonCreate("Add New", list->x, y, GEListAddNewItem, list);
	ui_WidgetAddChild(list->widgetParent, UI_WIDGET(list->addNewButton));
	
	GEListHeightChanged(list);
}

void GEListItemSetHeight(GEListItem *listItem, F32 newHeight)
{
	GEList *list = listItem->parentList;
	int index = eaFind(&list->listItems, listItem);
	if (index >= 0)
	{
		F32 yChange = 0.0f;
		F32 oldHeight = 0.0f;
		int i, n = eaSize(&list->listItems);

		if (index < eaSize(&list->listItems)-1)
			oldHeight = list->listItems[index+1]->y - list->listItems[index]->y;
		else
			oldHeight = list->addNewButton->widget.y - list->listItems[index]->y;
		yChange = newHeight - oldHeight;
		
		for (i = index+1; i < n; i++)
		{
			GEListMoveItemUI(list->listItems[i], yChange);
		}

		list->addNewButton->widget.y += yChange;
		GEListHeightChanged(list);
	}
}

void GEListDestroy(GEList *list)
{
	if (list)
	{
		UIWidget *widgetParent = (UIWidget*)list->widgetParent;
		eaDestroyEx(&list->listItems, GEListItemDestroy);
		ui_WidgetRemoveChild(widgetParent, UI_WIDGET(list->addNewButton));
		ui_WidgetQueueFree(UI_WIDGET(list->addNewButton));
		list->addNewButton = NULL;
		free(list);
	}
}

// ----------------------------------------------------------------------------------
// Map Utilities
// ----------------------------------------------------------------------------------

void GERefreshMapNamesList(void)
{
	RefDictIterator iter;
	ZoneMapInfo *zminfo;
	eaClear(&g_GEMapDispNames);
	worldGetZoneMapIterator(&iter);
	while (zminfo = worldGetNextZoneMap(&iter))
	{
		eaPush(&g_GEMapDispNames, allocAddString(zmapInfoGetPublicName(zminfo)));
	}
}

// Note that var names are pooled strings and should not be freed
void GERefreshMapVarNamesList(ZoneMap *pZoneMap, char ***peaVarNames)
{
	if (pZoneMap && peaVarNames) {
		int i;
		ZoneMapInfo *zminfo = zmapGetInfo(pZoneMap);
		for(i=0; i<zmapInfoGetVariableCount(zminfo); ++i) {
			WorldVariableDef *pDef = zmapInfoGetVariableDef(zminfo, i);
			eaPush(peaVarNames, (char*)pDef->pcName);
		}
	}
}

void GERefreshPatrolRouteList(void)
{
	eaClear(&g_GEPatrolRouteNames);

	// Find all patrol routes on the world layer
	worldGetObjectNames(WL_ENC_PATROL_ROUTE, &g_GEPatrolRouteNames, NULL);

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
	// Find all patrol routes on map layers
	if (g_EncounterMasterLayer) {
		int i, j, n = eaSize(&g_EncounterMasterLayer->encLayers);
		for (i = 0; i < n; i++) {
			EncounterLayer *layer = g_EncounterMasterLayer->encLayers[i];
			if (layer) {
				for(j=eaSize(&layer->oldNamedRoutes)-1; j>=0; --j) {
					if (layer->oldNamedRoutes[j]->routeName) {
						eaPush(&g_GEPatrolRouteNames, allocAddString(layer->oldNamedRoutes[j]->routeName));
					}
				}
			}
		}
	}
#endif
}


// ----------------------------------------------------------------------------------
// Text Entry Validation Utilities
// ----------------------------------------------------------------------------------

// Validation functions for text entries
bool GEValidateNameFunc(UIAnyWidget *widget, unsigned char **oldString, unsigned char **newString, void *unused)
{
	if (newString && *newString && **newString)
	{
		// Replace any spaces with underscores
		char *tmp = *newString;
		while (tmp && *tmp != '\0')
		{
			if (*tmp == ' ')
				*tmp = '_';
			tmp++;
		}
		return resIsValidName(*newString);
	}
	return true;
}

bool GEValidateScopeFunc(UIAnyWidget *widget, unsigned char **oldString, unsigned char **newString, void *unused)
{
	if (newString && *newString)
	{
		// Replace any spaces with underscores
		bool result;
		char *tmp = *newString;
		char slash = 0;
		while (tmp && *tmp != '\0')
		{
			if (*tmp == ' ')
				*tmp = '_';
			tmp++;
		}

		// If the last character is a slash, remove it
		--tmp;
		if ((*tmp) == '\\' || (*tmp) == '/')
		{
			slash = (*tmp);
			*tmp = '\0';
		}
		
		// Validate the string
		result = resIsValidScope(*newString);
		
		// Replace the slash at the end
		if (slash)
			*tmp = slash;
		
		return result;
	}
	return true;
}

// ----------------------------------------------------------------------------------
// UI Control Utilities
// ----------------------------------------------------------------------------------

static bool GETextEntryValidateLevel(UITextEntry* textEntry, unsigned char** oldString, 
									 unsigned char** newString, UserData validateData)
{
	char* textStr = *newString;
	if (ui_TextEntryValidationIntegerOnly(textEntry, oldString, newString, validateData))
	{
		// Check to see if it is filled out, we want clearing the string to be valid input
		if (textStr[0])
		{
			int intVal = atoi(textStr);
			return ((intVal >= 1) && (intVal <= MAX_LEVELS));
		}
		return true;
	}
	return false;
}

int GETextEntryCreateEx(UIAnyWidget** widgetPtr, UILabel** labelPtr, const char* labelName, 
						const char* startString, const char ***optionsList, const void *dictHandleOrName, 
						EArrayHandle* optionsEArray, ParseTable *pTable, const unsigned char* pTableField, 
						int x, int y, int w, int textAreaHeight, int labelBuffer, int whichValidFunc, 
						UIActivationFunc callback, void* cbData, UIAnyWidget *parent)
{
	UIWidget* editableWidget;
	UILabel* label = labelName ? ui_LabelCreate(labelName, x, y) : NULL;
	UITextEntry* uiEntry;

	if (textAreaHeight)
	{
		uiEntry = ui_TextEntryCreateWithTextArea(startString ? startString : "", x+labelBuffer, y);
		//ui_TextEntrySetChangedCallback(uiEntry, callback, cbData);
	}
	else
	{
		uiEntry = NULL; 

		if (optionsList)
			uiEntry = ui_TextEntryCreateWithStringCombo(startString ? startString : "", x + labelBuffer, y, optionsList, true, true, false, true);
		else if (dictHandleOrName)
			uiEntry = ui_TextEntryCreateWithGlobalDictionaryCombo(startString ? startString : "", x + labelBuffer, y, RefSystem_GetDictionaryNameFromNameOrHandle(dictHandleOrName), "resourceName", true, true, false, true);
		else if (optionsEArray)
			uiEntry = ui_TextEntryCreateWithObjectCombo(startString ? startString : "", x + labelBuffer, y, optionsEArray, pTable, pTableField, true, true, false, true);
		else
			uiEntry = ui_TextEntryCreate(startString ? startString : "", x + labelBuffer, y);
	}

	if (whichValidFunc == GE_VALIDFUNC_NOSPACE)
		ui_TextEntrySetIgnoreSpace(uiEntry, true);
	else if (whichValidFunc == GE_VALIDFUNC_INTEGER)
		ui_TextEntrySetIntegerOnly(uiEntry);
	else if (whichValidFunc == GE_VALIDFUNC_FLOAT)
		ui_TextEntrySetFloatOnly(uiEntry);
	else if (whichValidFunc == GE_VALIDFUNC_LEVEL)
		ui_TextEntrySetValidateCallback(uiEntry, GETextEntryValidateLevel, NULL);
	else if (whichValidFunc == GE_VALIDFUNC_NAME)
		ui_TextEntrySetValidateCallback(uiEntry, GEValidateNameFunc, NULL);
	else if (whichValidFunc == GE_VALIDFUNC_SCOPE)
		ui_TextEntrySetValidateCallback(uiEntry, GEValidateScopeFunc, NULL);

	ui_WidgetSetWidth((UIWidget*)uiEntry, w);
	ui_EditableSetMaxLength(UI_EDITABLE(uiEntry), GE_NAMELENGTH_MAX - 10);	// So we can tack numbers on the end
	ui_TextEntrySetFinishedCallback(uiEntry, callback, cbData);
	editableWidget = (UIWidget*)uiEntry;

	assertmsg(parent || (widgetPtr && (!label || labelPtr)), "Must give the button a window or expander to add it to, or pass in a pointer to receive the new widget");
	if (parent)
	{
		if (label) ui_WidgetGroupAdd(&((UIWidget*)parent)->children, (UIWidget*)label);
		ui_WidgetGroupAdd(&((UIWidget*)parent)->children, (UIWidget*)editableWidget);
	}

	if (widgetPtr) *widgetPtr = editableWidget;
	if (labelPtr) *labelPtr = label;
	return y + ui_StyleFontLineHeight(GET_REF(g_ui_State.font), 1.f) + UI_STEP;
}

int GEComboBoxCreateEx(UIAnyWidget** widgetPtr, UILabel** labelPtr, const char* labelName, 
					   const char*** options, int initialVal, int x, int y, int w, int labelWidth, 
					   UIActivationFunc callback, void* cbData, UIAnyWidget* parent)
{
	UIWidget* editableWidget;
	UILabel* label = labelName ? ui_LabelCreate(labelName, x, y) : NULL;

	UIComboBox* combo = ui_ComboBoxCreate(x+labelWidth, y, w, NULL, options, NULL);
	ui_ComboBoxSetSelected(combo, initialVal);
	ui_ComboBoxSetSelectedCallback(combo, callback, cbData);
	editableWidget = (UIWidget*)combo;
	
	assertmsg(parent, "Must give the text entry a window or expander to add it to");
	if (parent)
	{
		if (label) ui_WidgetGroupAdd(&((UIWidget*)parent)->children, (UIWidget*)label);
		ui_WidgetGroupAdd(&((UIWidget*)parent)->children, (UIWidget*)editableWidget);
	}
	if (widgetPtr) *widgetPtr = editableWidget;
	if (labelPtr) *labelPtr = label;
	return y + ui_StyleFontLineHeight(GET_REF(g_ui_State.font), 1.f) + UI_STEP;
}

void GENestedExpanderReflowCB(UIExpanderGroup *childGroup, UIExpander *expander)
{
	expander->openedHeight = childGroup->totalHeight + GE_SPACE;
	ui_ExpanderReflow(expander);
}

void GETextEntryFreeDataCB(UIEditable *editable)
{
	if (editable)
		free(editable->changedData);
}

// In many cases, we want a user-entered "" to be stored as a NULL pointer in memory.  This function is like StructAllocString, but doesn't allocate the "" string
char* GEAllocStringIfNN(const char* string)
{
	if(string && string[0])
		return StructAllocString(string);
	else
		return NULL;
}

// In many cases, we want a user-entered "" to be stored as a NULL pointer in memory.  This function is like StructAllocString, but doesn't allocate the "" string
char* GEAllocPooledStringIfNN(const char* string)
{
	if(string && string[0])
		return (char*)allocAddString(string);
	else
		return NULL;
}

// ----------------------------------------------------------------------------------
// Expression Utilities
// ----------------------------------------------------------------------------------

void GEUpdateExpressionFromExpression(Expression** exprPtr, const Expression* exprToCopy)
{
	if (exprToCopy)
	{
		if (!(*exprPtr))
			(*exprPtr) = exprCreate();
		exprCopy(*exprPtr, exprToCopy);
	}
	else if (*exprPtr)
	{
		exprDestroy(*exprPtr);
		*exprPtr = NULL;
	}
}

static void GEExprButtonEditCloseCB(Expression *expression, GEExprEditCallback* callback)
{
	const char *exprText = exprGetCompleteString(expression);
	ui_ButtonSetText(callback->button, exprText);
	if(callback->func)	// Not sure why there wouldn't be a callback
		callback->func(expression, callback->data);
}

static void GEExprButtonCB(UIButton* button, GEExprEditCallback* callback)
{
	const char* buttonTxt = ui_WidgetGetText(UI_WIDGET(button));
	const char* exprText = buttonTxt ? buttonTxt : "";
	Expression* tempExpression = exprCreateFromString(exprText, NULL);
	exprEdOpen(GEExprButtonEditCloseCB, tempExpression, callback, NULL, 0);
	exprDestroy(tempExpression);
}

static void GEExprButtonFreeCB(UIButton* button)
{
	free(button->clickedData);
}

int GEExpressionEditButtonCreate(UIButton** buttonPtr, UILabel** labelPtr, const char* labelName, 
								 Expression* startExpr, int x, int y, int w, int labelBuffer, 
								 ExprEdExprFunc exprFunc, UserData data, UIAnyWidget* parent)
{
	char* exprString = exprGetCompleteString(startExpr);
	UILabel* label = labelName ? ui_LabelCreate(labelName, x, y) : NULL;
	GEExprEditCallback *callback = calloc(1, sizeof(GEExprEditCallback));
	UIButton* button;
	
	callback->func = exprFunc;
	callback->data = data;
	button = ui_ButtonCreate(exprString ? exprString : "", x + labelBuffer, y, GEExprButtonCB, callback);
	ui_WidgetSetFreeCallback(UI_WIDGET(button), GEExprButtonFreeCB);
	callback->button = button;
	
	ui_WidgetSetWidth((UIWidget*)button, w);
	assertmsg(parent, "Must give the text entry a window or expander to add it to");
	if (parent)
	{
		if (label) ui_WidgetGroupAdd(&((UIWidget*)parent)->children, (UIWidget*)label);
		ui_WidgetGroupAdd(&((UIWidget*)parent)->children, (UIWidget*)button);
	}
	if (buttonPtr) *buttonPtr = button;
	if (labelPtr) *labelPtr = label;
	return y + button->widget.height;
}

// ----------------------------------------------------------------------------------
// Encounter Utilities
// ----------------------------------------------------------------------------------

static void GEDestroyActorStructCB(OldActor* actor)
{
	StructDestroy(parse_OldActor, actor);
}

void GEInstanceStaticEncounter(OldStaticEncounter *staticEnc)
{
	EncounterDef* baseDef = GET_REF(staticEnc->baseDef); 
	if (!staticEnc->defOverride)
		staticEnc->defOverride = StructCreate(parse_EncounterDef);

	// If no name and a base def, we need to instance the encounter info
	// Save the actors and copy everything else from the base def, then restore actors
	if (!staticEnc->defOverride->name && baseDef)
	{
		OldActor** overrideActors = staticEnc->defOverride->actors;
		staticEnc->defOverride->actors = NULL;
		StructCopyAll(parse_EncounterDef, baseDef, staticEnc->defOverride);
		eaDestroyEx(&staticEnc->defOverride->actors, GEDestroyActorStructCB);
		staticEnc->defOverride->actors = overrideActors;
	}
}

bool GESaveEncounterInternal(EncounterDef* def, char* fileOverride)
{
	int saved = 0;

	saved = ParserWriteTextFileFromSingleDictionaryStruct(fileOverride ? fileOverride : def->filename, g_EncounterDictionary, def, 0, 0);

	if(!saved)
		Alertf("Save failed: unable to write file!");

	return (bool) saved;
}

// Save an existing encounter (from the ELE) as a new encounter
void GESaveEncAsDefHelper(UITextEntry* textEntry, EncounterDef* oldEnc)
{
	const char* newName = ui_TextEntryGetText(textEntry);
	EncounterDef* encounter = NULL;
	char outFile[MAX_PATH];
	bool saved = 0;

	if(NULL == RefSystem_ReferentFromString(g_EncounterDictionary, newName))
	{
		// Create the encounter
		encounter = StructAlloc(parse_EncounterDef);
		StructCopyAll(parse_EncounterDef, oldEnc, encounter);
		encounter->name = (char*)allocAddString(newName);

		sprintf(outFile, "defs/encounters/%s.encounter", encounter->name);
		encounter->filename = StructAllocString(outFile);

		// Fixup messages
		langMakeEditorCopy(parse_EncounterDef, encounter, false);
		EDEFixupEncounterMessages(encounter);

		// Add to the global encounter list
		RefSystem_AddReferent(g_EncounterDictionary, encounter->name, encounter);

		// Write it to disk
		// TODO - Does not work with server-saving.  This should use the Resource system
		langApplyEditorCopy(parse_EncounterDef, encounter, NULL, true, false);
		GESaveEncounterInternal(encounter, NULL);
	}
	else
		Errorf("An encounter with that name already exists.");
}


// ----------------------------------------------------------------------------------
// Actor Utilities
// ----------------------------------------------------------------------------------

static F32 GEGetActorValue(OldActor* a, U32 teamsize)
{
	OldActorInfo *info;
	
	if (!a)
		return -1.0;
	
	info = oldencounter_GetActorInfo(a);

	if (info && oldencounter_IsEnabledAtTeamSize(a, teamsize))
	{
		CritterDef* def = GET_REF(info->critterDef);
		const char *pcRank;

		// Critter is either a specific def or 
		if(def)
		{
			pcRank = def->pcRank;
		}
		else
		{
			pcRank = info->pcCritterRank;
		}
		return critterRankGetDifficultyValue(pcRank, info->pcCritterSubRank, 0);
	}
	return 0.0;  // Encounter doesn't spawn at this team size
}

static F32 GEGetEncounterValue(EncounterDef *def, U32 teamsize)
{
	if (def)
	{
		int i, size = eaSize(&def->actors);
		F32 result = 0.0f;

		for (i = 0; i < size; i++)
		{
			F32 value = GEGetActorValue(def->actors[i], teamsize);
			if (value < 0)
				return -1.0;
			result += value;
		}
		return result;
	}
	else
		return -1.0;
}

char* GECreateUniqueActorName(EncounterDef* encDef, const char* desiredName)
{
	static char nextName[GE_NAMELENGTH_MAX];
	int counter = 1;
	strcpy(nextName, desiredName);
	while (oldencounter_FindDefActorByName(encDef, nextName))
	{
		sprintf(nextName, "%s%i", desiredName, counter);
		counter++;
	}
	return nextName;
}

// This function returns the info struct that should be displayed without modifying anything
const OldActorInfo *GEGetActorInfoNoUpdate(OldActor* actor, OldActor* baseActor)
{
	OldActorInfo *info = actor->details.info;
	OldActorInfo *baseinfo = baseActor?(baseActor->details.info):NULL;
	if (info)
		return info;
	else if (baseinfo)
		return baseinfo;
	else
		return NULL;
}

// This function will return the info struct to update and will create it if it does not exist
OldActorInfo* GEGetActorInfoForUpdate(OldActor* actor, OldActor* baseActor)
{
	if (!actor->details.info)
	{
		const OldActorInfo* currActorInfo = GEGetActorInfoNoUpdate(actor, baseActor);
		actor->details.info = StructAlloc(parse_OldActorInfo);
		StructCopyAll(parse_OldActorInfo, currActorInfo, actor->details.info);
	}
	return actor->details.info;
}

// This function returns the info struct that should be displayed without modifying anything
const OldActorAIInfo *GEGetActorAIInfoNoUpdate(OldActor* actor, OldActor* baseActor)
{
	OldActorAIInfo *info = actor->details.aiInfo;
	OldActorAIInfo *baseinfo = baseActor->details.aiInfo;
	if (info)
		return info;
	else if (baseinfo)
		return baseinfo;
	else
		return NULL;
}

// This function will return the AI info struct to update and will create it if it does not exist
OldActorAIInfo* GEGetActorAIInfoForUpdate(OldActor* actor, OldActor* baseActor)
{
	if (!actor->details.aiInfo)
	{
		const OldActorAIInfo* currActorAIInfo = GEGetActorAIInfoNoUpdate(actor, baseActor);
		actor->details.aiInfo = StructAlloc(parse_OldActorAIInfo);
		StructCopyAll(parse_OldActorAIInfo, currActorAIInfo, actor->details.aiInfo);
		langMakeEditorCopy(parse_OldActorAIInfo, actor->details.aiInfo, false);
	}
	return actor->details.aiInfo;
}

void GEChangeActorFSM(OldStaticEncounter* staticEnc, OldActor* actor, const char* fsmName, U32 teamSize, OldActor* srcActor)
{
	OldActorAIInfo* actorAIInfo = GEGetActorAIInfoForUpdate(actor, srcActor);
	REMOVE_HANDLE(actorAIInfo->hFSM);
	SET_HANDLE_FROM_STRING(gFSMDict, fsmName, actorAIInfo->hFSM);
}

// Function that should be used when creating a new actor
// Makes sure to initialize all necessary fields
OldActor* GEActorCreate(U32 uniqueID, OldActor* copyThisActor)
{
	OldActor* newActor = StructCreate(parse_OldActor);
	if (copyThisActor)
	{
		StructCopyAll(parse_OldActor, copyThisActor, newActor);
		newActor->name = NULL;
	}
	else
	{
		OldActorPosition* newBasePos = newActor->details.position = StructCreate(parse_OldActorPosition);
		newActor->details.info = StructCreate(parse_OldActorInfo);
		newActor->details.aiInfo = StructCreate(parse_OldActorAIInfo);
	}
	newActor->uniqueID = uniqueID;
	return newActor;
}

// This function will return the position struct to update and will create it if it does not exist
static OldActorPosition* GEGetActorPosForUpdate(OldActor* actor, OldActor* baseActor)
{
	if (!actor->details.position)
	{
		actor->details.position = StructCreate(parse_OldActorPosition);
		copyQuat(unitquat, actor->details.position->rotQuat);
	}
	return actor->details.position;
}

// This will store the new pos/rot offset change based on what the new relative mat is
void GEApplyActorPositionChange(OldActor* actor, const Mat4 newRelMat, OldActor* baseActor)
{
	Quat currQuat, changeQuat, tmpQuat;
	Mat4 currMat, currMatInv, changeMat;
	Vec3 posChange, currPos;
	OldActorPosition* actorPos = GEGetActorPosForUpdate(actor, baseActor);

	// Find what the current mat for this actor is
	// Using base actor because if the actor is just an override, we wont get the actual position
	oldencounter_GetActorPositionOffset(baseActor, currQuat, currPos);
	quatToMat(currQuat, currMat);
	
	// Now figure out the actual change in position and rotation
	invertMat3Copy(currMat, currMatInv);
	mulMat4(currMatInv, newRelMat, changeMat);
	mat3ToQuat(changeMat, changeQuat);
	subVec3(newRelMat[3], currPos, posChange);

	// Apply the change in rotation and position to the current team size scaling position
	copyQuat(actorPos->rotQuat, tmpQuat);
	quatMultiply(tmpQuat, changeQuat, actorPos->rotQuat);
	addVec3(posChange, actorPos->posOffset, actorPos->posOffset);
}

void GEEncounterDefAddActor(EncounterDef* def, CritterDef* critterDef, Mat4 actorMat, U32 uniqueID)
{
	OldActor* newActor = GEActorCreate(uniqueID, NULL);
	eaPush(&def->actors, newActor);
	copyVec3(actorMat[3], newActor->details.position->posOffset);
	mat3ToQuat(actorMat, newActor->details.position->rotQuat);
	if (critterDef)
	{
		SET_HANDLE_FROM_STRING(g_hCritterDefDict, critterDef->pchName, newActor->details.info->critterDef);
	}
}

FSM* GEFindCommonActorFSM(OldActor*** pppActors)
{
	int i, selectedCount = eaSize(pppActors);

	if (selectedCount)
	{
		OldActor* pFirstActor = eaGet(pppActors,0);
		OldActorInfo* firstActorInfo = oldencounter_GetActorInfo(pFirstActor);
		OldActorAIInfo* firstActorAIInfo = oldencounter_GetActorAIInfo(pFirstActor);
		FSM* whichFSM = oldencounter_GetActorFSM(firstActorInfo, firstActorAIInfo);

		for (i = 0; i < selectedCount; i++)
		{
			OldActor* pActor = eaGet(pppActors,i);
			OldActorInfo* currActorInfo = oldencounter_GetActorInfo(pActor);
			OldActorAIInfo* currActorAIInfo = oldencounter_GetActorAIInfo(pActor);
			if (whichFSM != oldencounter_GetActorFSM(currActorInfo, currActorAIInfo))
				return NULL;
		}
		return whichFSM;
	}
	return NULL;
}

const char* GEFindCommonActorFSMName(OldActor*** pppActors)
{
	int i, selectedCount = eaSize(pppActors);

	if (selectedCount)
	{
		OldActor* pFirstActor = eaGet(pppActors,0);
		OldActorInfo* firstActorInfo = oldencounter_GetActorInfo(pFirstActor);
		OldActorAIInfo* firstActorAIInfo = oldencounter_GetActorAIInfo(pFirstActor);
		const char* whichFSM = oldencounter_GetFSMName(firstActorInfo, firstActorAIInfo);

		for (i = 0; i < selectedCount; i++)
		{
			OldActor* pActor = eaGet(pppActors,i);
			OldActorInfo* currActorInfo = oldencounter_GetActorInfo(pActor);
			OldActorAIInfo* currActorAIInfo = oldencounter_GetActorAIInfo(pActor);
			const char *thisFSM = oldencounter_GetFSMName(currActorInfo, currActorAIInfo);
			if (whichFSM && thisFSM && (stricmp(whichFSM,thisFSM) != 0))
				return NULL;
			else if (!whichFSM)
				whichFSM = thisFSM;
		}
		return whichFSM;
	}
	return NULL;
}

// ----------------------------------------------------------------------------------
// Selected Object Utilities
// ----------------------------------------------------------------------------------

static bool GESelectedObjectIsValid(GEObjectType selType, void* objData, int groupIndex, int objIndex)
{
	switch (selType)
	{
		xcase GEObjectType_Actor:
			return (objIndex >= 0);
		xcase GEObjectType_Encounter:
			return (groupIndex >= 0);
		xcase GEObjectType_Group:
			return (objData != NULL);
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
		xcase GEObjectType_PatrolPoint:
			return (objIndex >= 0);
		xcase GEObjectType_PatrolRoute:
			return (groupIndex >= 0);
#endif
		xcase GEObjectType_Point:
			return true;
		xdefault:
			assertmsg(NULL, "Programmer Error: Bad parameters or you forgot to add the isValid for a new type");
	}
	return false;
}

GESelectedObject* GESelectedObjectFind(GESelectedObject*** selObjListPtr, GEObjectType selType, void* objData, int groupIndex, int objIndex)
{
	int i, n = eaSize(selObjListPtr);
	for (i = 0; i < n; i++)
	{
		GESelectedObject* currObj = (*selObjListPtr)[i];
		if (currObj->selType == selType)
		{
			switch (selType)
			{
				xcase GEObjectType_Actor:
					if ((currObj->objIndex == objIndex) && (currObj->groupIndex == groupIndex))
						return currObj;
				xcase GEObjectType_Encounter:
					if (currObj->groupIndex == groupIndex)
						return currObj;
				xcase GEObjectType_Group:
					if (currObj->objData == objData)
						return currObj;
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
				xcase GEObjectType_PatrolPoint:
					if ((currObj->objIndex == objIndex) && (currObj->groupIndex == groupIndex))
						return currObj;
				xcase GEObjectType_PatrolRoute:
					if (currObj->groupIndex == groupIndex)
						return currObj;
#endif
				xcase GEObjectType_Point:
					if (currObj->groupIndex == groupIndex && currObj->objIndex == objIndex)
						return currObj;	// Point in encounter
				xdefault:
					assertmsg(NULL, "Programmer Error: Bad object in list or you forgot to add the find for a new type");
			}
		}
	}
	return NULL;
}

static GESelectedObject* GESelectedObjectCreate(GEObjectType type, void* objData, int groupIndex, int objIndex)
{
	GESelectedObject* newObject = calloc(1, sizeof(GESelectedObject));
	newObject->selType = type;
	newObject->objData = objData;
	newObject->groupIndex = groupIndex;
	newObject->objIndex = objIndex;
	return newObject;
}

void GESelectedObjectDestroyCB(GESelectedObject* object)
{
	free(object);
}

void GESelectObject(GESelectedObject*** selObjListPtr, GEObjectType selType, void* objData, int groupIndex, int objIndex, bool additive)
{
	GESelectedObject* selectedObj;

	g_InClickableAttachMode = false;

	// If not additive, clear the selected lists
	if (!additive)
		eaDestroyEx(selObjListPtr, GESelectedObjectDestroyCB);

	// Add the object if not selected, otherwise deselect the object if additive is enabled
	if (GESelectedObjectIsValid(selType, objData, groupIndex, objIndex))
	{
		selectedObj = GESelectedObjectFind(selObjListPtr, selType, objData, groupIndex, objIndex);
		if (!selectedObj)
			eaPush(selObjListPtr, GESelectedObjectCreate(selType, objData, groupIndex, objIndex));
		else if (additive)
		{
			eaFindAndRemove(selObjListPtr, selectedObj);
			GESelectedObjectDestroyCB(selectedObj);
		}
	}
}


// ----------------------------------------------------------------------------------
// Placement Tool Utilities
// ----------------------------------------------------------------------------------

bool GEPlacementToolIsInPlacementMode(GEPlacementTool* placementTool)
{
	return (placementTool->createNew || placementTool->moveSelected || placementTool->moveOrigin);
}

void GEPlacementToolApply(GEPlacementTool* placementTool, Mat4 initialMat)
{
	Vec3 cursor3d;
	Mat4 changeMat, tmpMat, invStartMat;

	// Calculate the changes from the initial mat to the current position
	if (!placementTool->useGizmos)
	{
		if (!GEFindMouseLocation(placementTool, cursor3d))
			copyVec3(placementTool->movementOrigin[3], cursor3d);
		subVec3(cursor3d, placementTool->movementOrigin[3], changeMat[3]);
		copyMat3(unitmat, changeMat);
		yawMat3(-placementTool->rotAngle, changeMat);
	}
	else
	{
		invertMat3Copy(placementTool->movementOrigin, invStartMat);
		mulMat3(invStartMat, placementTool->gizmoMat, changeMat);
		subVec3(placementTool->gizmoMat[3], placementTool->movementOrigin[3], changeMat[3]);
	}

	// Apply to a relative mat, then move back to nonrelative coordinates
	subVec3(initialMat[3], placementTool->movementOrigin[3], initialMat[3]);
	copyMat4(initialMat, tmpMat);
	mulMat4(changeMat, tmpMat, initialMat);
	addVec3(initialMat[3], placementTool->movementOrigin[3], initialMat[3]);
}

bool GEPlacementToolShouldCopy(GEPlacementTool* placementTool)
{
	if (placementTool->copySelected)
		return true;
	else if (placementTool->useCopySelect)
	{
		int mx, my;
		Vec2 currMouse, qpMouse = {placementTool->qpMx, placementTool->qpMy};
		mousePos(&mx, &my); currMouse[0] = mx; currMouse[1] = my;
		return (distance2Squared(currMouse, qpMouse) > GE_MINCOPYDIST);
	}
	return false;
}

bool GEPlacementToolCancelAction(GEPlacementTool* placementTool)
{
	if (placementTool->createNew)
		placementTool->createNew = 0;
	else if (placementTool->moveSelected)
		placementTool->moveSelected = placementTool->copySelected = 0;
	else if (placementTool->moveOrigin)
		placementTool->moveOrigin = 0;
	else
		return false;

	if (placementTool->isRotating)
		mouseLock(0);
	placementTool->isQuickPlacing = placementTool->isRotating = 0;
	return true;
}

void GEPlacementToolReset(GEPlacementTool* placementTool, const Vec3 gizmoPos, const Vec3 clickOffset)
{
	placementTool->rotAngle = 0;
	copyMat3(unitmat, placementTool->gizmoMat);
	copyVec3(gizmoPos, placementTool->gizmoMat[3]);
	copyMat4(placementTool->gizmoMat, placementTool->movementOrigin);
	copyVec3(clickOffset, placementTool->clickOffset);
	TranslateGizmoSetMatrix(placementTool->transGizmo, placementTool->gizmoMat);
	RotateGizmoSetMatrix(placementTool->rotGizmo, placementTool->gizmoMat);
}

void GEPlacementToolUpdate(GEPlacementTool* placementTool)
{
	// Update the current angle of the quickrotate tool
	if (GEPlacementToolIsInPlacementMode(placementTool) && placementTool->isRotating)
	{
		int tmpX, currMy = placementTool->qrMy;
		mousePos(&tmpX, &placementTool->qrMy);
		if (currMy != placementTool->qrMy)
			placementTool->rotAngle += (currMy - placementTool->qrMy) * RAD(1);
	}

	// Draw the marquee selection tool
	if (placementTool->inMarqueeSelect)
	{
		int mx, my;
		mousePos(&mx, &my);
		gfxDrawLine(placementTool->marqueeMx, placementTool->marqueeMy, 0, mx, placementTool->marqueeMy, ColorWhite);
		gfxDrawLine(placementTool->marqueeMx, placementTool->marqueeMy, 0, placementTool->marqueeMx, my, ColorWhite);
		gfxDrawLine(placementTool->marqueeMx, my, 0, mx, my, ColorWhite);
		gfxDrawLine(mx, placementTool->marqueeMy, 0, mx, my, ColorWhite);
	}

	// Draw and update the editlib gizmos
	if (GEPlacementToolIsInPlacementMode(placementTool) && placementTool->useGizmos)
	{
		if (placementTool->useRotateTool)
		{
			RotateGizmoUpdate(placementTool->rotGizmo);
			RotateGizmoDraw(placementTool->rotGizmo);
			RotateGizmoGetMatrix(placementTool->rotGizmo, placementTool->gizmoMat);
		}
		else
		{
			TranslateGizmoUpdate(placementTool->transGizmo);
			TranslateGizmoDraw(placementTool->transGizmo);
			TranslateGizmoGetMatrix(placementTool->transGizmo, placementTool->gizmoMat);
		}
	}
}


// ----------------------------------------------------------------------------------
// Selection/Mouse Utilities
// ----------------------------------------------------------------------------------

void GEFindActorMat(OldActor* actor, Mat4 encCenter, Mat4 actorMat)
{
	Mat4 tmpMat;
	Quat rotQuat;
	oldencounter_GetActorPositionOffset(actor, rotQuat, tmpMat[3]);
	quatToMat(rotQuat, tmpMat);
	mulMat4(encCenter, tmpMat, actorMat);
}

void GEFindEncPointMat(OldNamedPointInEncounter* point, Mat4 encCenter, Mat4 absMat)
{
	mulMat4(encCenter, point->relLocation, absMat);
}

void GEFindScreenCoordsEx(Vec3 minbounds, Vec3 maxbounds, Mat4 worldMat, Mat44 scrProjMat, Vec3 bottomLeft, Vec3 topRight)
{
	F32 tmpY;
	Vec3 boxMin, boxMax;
	int scrWd, scrHt;
	gfxGetActiveSurfaceSize(&scrWd, &scrHt);

	// Create the 2d screen box of the actor's bounding box	
	mulBoundsAA(minbounds, maxbounds, worldMat, boxMin, boxMax);
	mulBoundsAA44(boxMin, boxMax, scrProjMat, bottomLeft, topRight);

	// mulBounds returns a float from -1 to 1, convert to 0 to 1, then to screen coords; 0 the z value
	// We invert the y because mouse coordinates start at 0 from the top, swap the 2 y values
	bottomLeft[0] = 0.5 * scrWd * (bottomLeft[0] + 1.0f);
	bottomLeft[1] = scrHt * (1.0 - 0.5 * (bottomLeft[1] + 1.0f));
	topRight[0] = 0.5 * scrWd * (topRight[0] + 1.0f);
	topRight[1] = scrHt * (1.0 - 0.5 * (topRight[1] + 1.0f));
	tmpY = bottomLeft[1]; bottomLeft[1] = topRight[1]; topRight[1] = tmpY;
	bottomLeft[2] = topRight[2] = 0;
}

// Finds where the mouse location in the 3d world is
bool GEFindMouseLocation(GEPlacementTool* placementTool, Vec3 resultPos)
{
	if (placementTool->isRotating)
	{
		copyVec3(placementTool->rotMousePos, resultPos);
		return true;
	}
	else if (placementTool->collideWithWorld)
	{
		WorldCollCollideResults wcResults;
		bool collide;
		Vec3 start, end;
		editLibCursorRay(start, end);
		addVec3(start, placementTool->clickOffset, start);
		addVec3(end, placementTool->clickOffset, end);
		collide = worldCollideRay(PARTITION_CLIENT, start, end, WC_QUERY_BITS_WORLD_ALL, &wcResults);
		if (collide)
			copyVec3(wcResults.posWorldImpact, resultPos);
		return collide;
	}
	else
	{
		Vec4 plane;
		Vec3 start, end, normal = {0, 1, 0};
		makePlane2(zerovec3, normal, plane);
		editLibCursorRay(start, end);
		addVec3(start, placementTool->clickOffset, start);
		addVec3(end, placementTool->clickOffset, end);
		return intersectPlane(start, end, plane, resultPos);
	}
	return false;
}

bool GEIsLegalMargqueeSelection(Vec3 centerPos, Mat4 camMat)
{
	Vec3 targetPoint;
	F32 closestObjDistance = GE_MARQUEEDIST;
	WorldCollCollideResults wcResults;
	subVec3(centerPos, camMat[3], targetPoint);

	if (worldCollideRay(PARTITION_CLIENT, camMat[3], centerPos, WC_QUERY_BITS_EDITOR_ALL, &wcResults))
		closestObjDistance = MIN((wcResults.distance * wcResults.distance), GE_MARQUEEDIST);

	return ((distance3Squared(centerPos, camMat[3]) <= closestObjDistance+1 ) && (dotVec3(targetPoint, camMat[2]) <= 0));
}

int GEWhichActorUnderMouse(EncounterDef* def, Mat4 encCenter, Vec3 selActorPos, Vec3 clickOffset, F32 *distance)
{
	Mat4 actorMat;
	Vec3 result, start, end, boxMin, boxMax;
	int clickedActor = -1;
	int i, n = eaSize(&def->actors);
	F32 clickedActorDist = -1;

	if(distance)
		clickedActorDist = *distance;

	// Shoot a ray and find the closest actor whose bounding box is intersected
	editLibCursorRay(start, end);
	for (i = 0; i < n; i++)
	{
		OldActor* actor = def->actors[i];
		GEFindActorMat(actor, encCenter, actorMat);
		mulBoundsAA(g_GEDisplayDefs.actDispDef->bounds.min, g_GEDisplayDefs.actDispDef->bounds.max, actorMat, boxMin, boxMax);
		if (lineBoxCollision(start, end, boxMin, boxMax, result))
		{
			F32 dist = distance3Squared(start, result);
			if ((clickedActorDist < 0) || (dist < clickedActorDist))
			{
				clickedActorDist = dist;
				clickedActor = i;
				if (clickOffset)
					subVec3(actorMat[3], result, clickOffset);
				if (selActorPos)
					copyVec3(actorMat[3], selActorPos);
			}
		}
	}

	if(distance)
		*distance = clickedActorDist;
	return clickedActor;
}

int GEWhichEncounterPointUnderMouse(EncounterDef* def, Mat4 encCenter, Vec3 selPointPos, Vec3 clickOffset, F32 *distance)
{
	Vec3 result, start, end;
	int clickedPoint = -1;
	int i, n = eaSize(&def->namedPoints);
	F32 clickedPointDist = -1;

	if(distance)
		clickedPointDist = *distance;

	// Shoot a ray and find the closest point whose bounding box is intersected
	editLibCursorRay(start, end);
	for (i = 0; i < n; i++)
	{
		OldNamedPointInEncounter* point = def->namedPoints[i];
		if (!point->frozen && findSphereLineIntersection(start, end, point->relLocation[3], ME_NAMED_POINT_RADIUS, result))
		{
			F32 dist = distance3Squared(start, result);
			if ((clickedPointDist < 0) || (dist < clickedPointDist))
			{
				clickedPointDist = dist;
				clickedPoint = i;
				if (clickOffset)
					subVec3(point->relLocation[3], result, clickOffset);
				if (selPointPos)
					copyVec3(point->relLocation[3], selPointPos);
			}
		}
	}

	if(distance)
		*distance = clickedPointDist;
	return clickedPoint;
}

// ----------------------------------------------------------------------------------
// World Draw Utilities
// ----------------------------------------------------------------------------------

void GEDrawPatrolPoint(Mat4 mat, bool isSelected, Color selColor, bool bInWorld)
{
	TempGroupParams tgparams = {0};
	PERFINFO_AUTO_START("MEDrawPatrolPoint",1);
	tgparams.dont_cast_shadows = true;
	worldAddTempGroup(g_GEDisplayDefs.spawnLocDispDef, mat, &tgparams, bInWorld);
	if (isSelected)
		gfxDrawBox3D(g_GEDisplayDefs.spawnLocDispDef->bounds.min, g_GEDisplayDefs.spawnLocDispDef->bounds.max, mat, selColor, 5);
	PERFINFO_AUTO_STOP();
}

// We need to rotate the mat 180 degrees because critters spawn facing the opposite direction of the model
void GEDrawActor(OldActor* actor, Mat4 actorMat, bool isSelected, Color selColor, U32 teamSize, bool bInWorld, F32 distSquared)
{
	bool showAsDimmed = (actor && !oldencounter_IsEnabledAtTeamSize(actor, teamSize));
	TempGroupParams tgparams = {0};
	
	PERFINFO_AUTO_START("MEDrawActor",1);
	
	tgparams.alpha = showAsDimmed ? 0.25f : 1.f;
	tgparams.dont_cast_shadows = true;
	if (distSquared >= ENC_DETAIL_DIST_SQ)
		tgparams.unlit = true;
	if (distSquared <= ENC_MAX_DIST_SQ)
		worldAddTempGroup(g_GEDisplayDefs.actDispDef, actorMat, &tgparams, bInWorld);
	//else if (distSquared <= ENC_MAX_DIST_SQ)// draw fast version
	//	gfxDrawBox3DEx(g_GEDisplayDefs.actDispDef->bounds.min, g_GEDisplayDefs.actDispDef->bounds.max, actorMat, ColorRed, 0, VOLFACE_POSX|VOLFACE_POSY|VOLFACE_POSZ);
	
	if (isSelected)
		gfxDrawBox3D(g_GEDisplayDefs.actDispDef->bounds.min, g_GEDisplayDefs.actDispDef->bounds.max, actorMat, selColor, 5);
	
	PERFINFO_AUTO_STOP();
}

void GEDrawPoint(Mat4 pointLoc, bool isSelected, Color selColor)
{
	if(!isSelected)
		selColor = ColorYellow;
	PERFINFO_AUTO_START("MEDrawPoint",1);
	gfxDrawSphere3D(pointLoc[3], ME_NAMED_POINT_RADIUS, 8, selColor, 0);
	// Sanity check; don't draw an arrow if the point's orientation is invalid
	if(!(vec3IsZero(pointLoc[0]) || vec3IsZero(pointLoc[1]) || vec3IsZero(pointLoc[2])))
		GEDrawPatrolPoint(pointLoc, isSelected, selColor, true);
	PERFINFO_AUTO_STOP();
}

// Draw the currently loaded encounter actors and center point of the group
void GEDrawEncounter(EncounterDef* def, Mat4 encCenter, int* selectedActors, int* selectedPoints, bool groupSelected, bool inPlacement, bool isCopy, bool isMouseover, U32 teamSize, bool bInWorld, bool snapToGround)
{
	if (def){

		Color dispColor = inPlacement ? (isCopy ? GE_COPY_COLOR : GE_MOVE_COLOR) : GE_SEL_COLOR;
		Color aggroColor = GE_AGGRO_COLOR;
		int i, n = eaSize(&def->actors);
		int m = eaSize(&def->namedPoints);
		Mat4 actorMat;
		Mat4 camMat;
		F32 distToEncSquared = 0.0f;
		TempGroupParams tgparams = {0};
		tgparams.dont_cast_shadows = true;
		
		PERFINFO_AUTO_START("MEDrawEncounter",1);
		
		gfxGetActiveCameraMatrix(camMat);
		distToEncSquared = distance3Squared(camMat[3], encCenter[3]);
		
		if (distToEncSquared <= ENC_MAX_DIST_SQ)
		{
			if (distToEncSquared <= ENC_DETAIL_DIST_SQ)
				tgparams.unlit = true;

			if (isMouseover) dispColor = GE_INFO_COLOR;

			// Draw the actors and show as selected if the group is or if the actor is in the selected list
			for (i = 0; i < n; i++)
			{
				bool isSelected = (groupSelected || (selectedActors && eaiFind(&selectedActors, i) != -1));
				GEFindActorMat(def->actors[i], encCenter, actorMat);

				if(snapToGround)
					worldSnapPosToGround(PARTITION_CLIENT, actorMat[3], 10, -10, NULL);

				GEDrawActor(def->actors[i], actorMat, isSelected, dispColor, teamSize, bInWorld, distToEncSquared);
			}

			// Draw any named points in the encounter
			for (i = 0; i < m; i++)
			{
				bool isSelected = (groupSelected || (selectedPoints && eaiFind(&selectedPoints, i) != -1));
				GEFindEncPointMat(def->namedPoints[i], encCenter, actorMat);
				GEDrawPoint(actorMat, isSelected, dispColor);
			}

			// Show the center/origin point of the encounter
			//if (distToEncSquared <= ENC_MAX_DIST_SQ)
				worldAddTempGroup(g_GEDisplayDefs.encDispDef, encCenter, &tgparams, bInWorld);
			//else if (distToEncSquared <= ENC_MAX_DIST_SQ)
			//	gfxDrawBox3DEx(g_GEDisplayDefs.encDispDef->bounds.min, g_GEDisplayDefs.encDispDef->bounds.max, encCenter, ColorBlue, 0, VOLFACE_POSX|VOLFACE_POSY|VOLFACE_POSZ);

			if (groupSelected)
				gfxDrawBox3D(g_GEDisplayDefs.encDispDef->bounds.min, g_GEDisplayDefs.encDispDef->bounds.max, encCenter, dispColor, 5);
		}

		// Draw an aggro radius around the encounter
		// We don't draw the correct aggro radius because it's hard to know; it can be changed by critter classes,
		// by FSMs, etc.  Instead, we take a ballpark guess of what "most" critters "should" be.
		// In the future, it would be nice to get an accurate number for this
		if(g_ShowAggroRadius && distToEncSquared <= ENC_AGGRO_DIST_SQ)
		{
			U32 radiusToDraw = g_EncAggroRadiusOverride?g_EncAggroRadiusOverride:gConf.iDefaultEditorAggroRadius;
			aggroColor.a = 64;
			gfxDrawSphere3D(encCenter[3], radiusToDraw, 8, aggroColor, 0);
		}

		PERFINFO_AUTO_STOP();

	}
}

// ----------------------------------------------------------------------------------
// Job UI
// ----------------------------------------------------------------------------------

static void GEJobUIExpanderToggleCB(UIExpander *jobExpander, EncounterPropUI *encPropUI)
{
	ui_ExpanderSetHeight(encPropUI->encPropExpander, encPropUI->jobExpander->widget.y + encPropUI->jobExpander->widget.height);
}

static AIJobDesc* GEJobUIFindJobByName(EncounterDef *def, const char *name)
{
	int i, n = eaSize(&def->encJobs);
	for (i = 0; i < n; i++)
	{
		AIJobDesc *job = def->encJobs[i];
		if (job && job->jobName && name && !stricmp(job->jobName, name))
			return job;
	}
	return NULL;
}

static AIJobDesc* GEJobUIFindJobByNameFromMultiple(EncounterDef*** defList, const char *name)
{
	int i, n = eaSize(defList);
	for (i = 0; i < n; i++)
	{
		AIJobDesc *job = GEJobUIFindJobByName((*defList)[i], name);
		if (job)
			return job;
	}
	return NULL;
}

static char* GEJobUICreateUniqueJobName(EncounterDef*** encDefList, const char* desiredName)
{
	static char nextJobName[GE_NAMELENGTH_MAX];
	int counter = 1;
	strcpy(nextJobName, desiredName);
	while (GEJobUIFindJobByNameFromMultiple(encDefList, nextJobName))
	{
		sprintf(nextJobName, "%s%i", desiredName, counter);
		counter++;
	}
	return nextJobName;
}

static void GEGetEncounterDefsForEdit(EncounterDef*** defList, EncounterPropUI* encPropUI)
{
	int i, n = eaSize(&encPropUI->staticEncList);

	// Add all EncounterDefs to the list
	eaPushEArray(defList, &encPropUI->defList);

	// Add all Static Encounters to the list
	for (i = 0; i < n; i++)
	{
		OldStaticEncounter *staticEnc = encPropUI->staticEncList[i];
		GEInstanceStaticEncounter(staticEnc);
		eaFindAndRemove(defList, GET_REF(encPropUI->staticEncList[i]->baseDef));
		eaPushUnique(defList, encPropUI->staticEncList[i]->defOverride);
	}
}

static AIJobDesc* GEJobCreate(EncounterPropUI* encPropUI)
{
	AIJobDesc *jobDesc = StructCreate(parse_AIJobDesc);
	EncounterDef** defList = NULL;
	const char *name = NULL;
	int i;

	GEGetEncounterDefsForEdit(&defList, encPropUI);

	name = GEJobUICreateUniqueJobName(&defList, "NewJob");
	jobDesc->jobName = StructAllocString(name);

	//Add job to all the EncounterDefs and StaticEncounters linked to the propUI
	for (i = 0; i < eaSize(&defList); ++i)
	{
		eaPush(&defList[i]->encJobs, StructClone(parse_AIJobDesc, jobDesc));
	}
	eaDestroy(&defList);

	return jobDesc;
}

static bool GEJobsMatch(const AIJobDesc *jobA, const AIJobDesc *jobB)
{
	// For now just do a StructCompare
	return (StructCompare(parse_AIJobDesc, jobA, jobB, 0, 0, 0) == 0);
}

static void GEJobUIListHeightChangedCB(GEList *list, F32 newHeight, EncounterPropUI *encPropUI)
{
	ui_ExpanderSetHeight(encPropUI->jobExpander, newHeight);
	ui_ExpanderSetHeight(encPropUI->encPropExpander, encPropUI->jobExpander->widget.y + encPropUI->jobExpander->widget.height);
}

static void GEJobUIFreeClickedDataCB(UIButton *button)
{
	free(button->clickedData);
}

static void GEJobUISaveJobCB(JobEditor *jobEditor, AIJobDesc *origJobDesc, AIJobDesc *newJobDesc, EncounterPropUI *encPropUI)
{
	EncounterDef **defList = NULL;
	int i, n;
	
	GEGetEncounterDefsForEdit(&defList, encPropUI);

	if (stricmp(newJobDesc->jobName, origJobDesc->jobName) != 0)
	{
		char *name = StructAllocString(GEJobUICreateUniqueJobName(&defList, newJobDesc->jobName));
		StructFreeString(newJobDesc->jobName);
		newJobDesc->jobName = name;
	}

	n = eaSize(&defList);
	for (i = 0; i < n; i++)
	{
		EncounterDef *def = defList[i];
		int iJob, numJobs = eaSize(&def->encJobs);
		for (iJob = numJobs-1; iJob >= 0; --iJob)
		{
			if (GEJobsMatch(origJobDesc, def->encJobs[iJob]))
			{
				AIJobDesc *match = def->encJobs[iJob];
				StructCopyAll(parse_AIJobDesc, newJobDesc, match);
				break;  // only remove the first match?
			}
		}		
	}

	StructCopyAll(parse_AIJobDesc, newJobDesc, origJobDesc);
	eaDestroy(&defList);
	GEListRefresh(encPropUI->jobList);
	GESetCurrentDocUnsaved();
}

static void GEJobUIOpenJobEditorCB(UIButton *button, GEJobEditorData *jobEditorData)
{
	jobeditor_Create(jobEditorData->job, GEJobUISaveJobCB, jobEditorData->encPropUI);
}

static void GEJobUIDestroy(AIJobDesc* job, EncounterPropUI* encPropUI)
{
	// TODO - Remove job to all the EncounterDefs and StaticEncounters linked to the propUI
	EncounterDef** defList = NULL;
	int i, n;

	GEGetEncounterDefsForEdit(&defList, encPropUI);
	n = eaSize(&defList);
	for (i = 0; i < n; i++)
	{
		EncounterDef *def = defList[i];
		int iJob, numJobs = eaSize(&def->encJobs);
		for (iJob = numJobs-1; iJob >= 0; --iJob)
		{
			if (GEJobsMatch(job, def->encJobs[iJob]))
			{
				AIJobDesc *match = eaRemove(&def->encJobs, iJob);
				StructDestroy(parse_AIJobDesc, match);
				break;  // only remove the first match?
			}
		}
	}

	jobeditor_DestroyForJob(job);
	StructDestroy(parse_AIJobDesc, job);
	eaDestroy(&defList);
}

static F32 GEJobUICreate(GEListItem *listItem, UIWidget*** widgetList, AIJobDesc *job, F32 x, F32 y, UIAnyWidget* widgetParent, EncounterPropUI *encPropUI)
{
	GEJobEditorData *data = calloc(1, sizeof(GEJobEditorData));
	UIButton *button = NULL;
	char jobName[256];
	sprintf(jobName, "%s (%3.2f)", job->jobName?job->jobName:"<unnamed job>", job->priority);

	data->job = job;
	data->encPropUI = encPropUI;
	
	button = ui_ButtonCreate(jobName, x, y, GEJobUIOpenJobEditorCB, data);
	ui_WidgetSetFreeCallback(UI_WIDGET(button), GEJobUIFreeClickedDataCB);
	ui_WidgetSetWidth(UI_WIDGET(button), 200);
	y += button->widget.height;
	eaPush(widgetList, UI_WIDGET(button));
	ui_WidgetAddChild(widgetParent, UI_WIDGET(button));

	return y;
}


// ----------------------------------------------------------------------------
// Encounter properties editing callbacks
// ----------------------------------------------------------------------------

static void GEEncounterOwnerMapVarChanged(UIEditable *pWidget, EncounterPropUI *pUI)
{
	EncounterDef** eaDefs = NULL;
	const char *pchNewValue = allocAddString(ui_EditableGetText(pWidget));
	int i;
	
	GEGetEncounterDefsForEdit(&eaDefs, pUI);

	for (i = 0; i < eaSize(&eaDefs); ++i) {
		eaDefs[i]->pchOwnerMapVar = pchNewValue;
	}

	eaDestroy(&eaDefs);
	GESetCurrentDocUnsaved();
}

// ----------------------------------------------------------------------------------
// Encounter Prop UI
// ----------------------------------------------------------------------------------

static CritterGroup* GEGetCritterGroupForEncounterProp(OldStaticEncounter* staticEnc, EncounterDef* def)
{
	if (staticEnc && GET_REF(staticEnc->encCritterGroup) ) return GET_REF(staticEnc->encCritterGroup);
	return GET_REF(def->critterGroup);
}

static F32 GEGetEncounterValueForEncounterProp(OldStaticEncounter *staticEnc, EncounterDef *encDef, U32 teamsize)
{
	if (staticEnc && staticEnc->spawnRule)
		return GEGetEncounterValue(staticEnc->spawnRule, teamsize);
	else
		return GEGetEncounterValue(encDef, teamsize);
}

static CritterFaction* GEGetFactionForEncounterProp(OldStaticEncounter* staticEnc, EncounterDef* def)
{
	CritterFaction* faction;
	if (staticEnc && (faction = GET_REF(staticEnc->encFaction))) return faction;
	return GET_REF(def->faction);
}

static bool GEGetSpawnPerPlayerForEncounterProp(OldStaticEncounter* staticEnc, EncounterDef* def)
{
	if (staticEnc && staticEnc->defOverride)
		return staticEnc->defOverride->bCheckSpawnCondPerPlayer;
	return def->bCheckSpawnCondPerPlayer;
}
static bool GEGetUsePlayerLevelForEncounterProp(OldStaticEncounter* staticEnc, EncounterDef* def)
{
	if (staticEnc && staticEnc->defOverride)
		return staticEnc->defOverride->bUsePlayerLevel;
	return def->bUsePlayerLevel;
}
static bool GEGetIsAmbushForEncounterProp(OldStaticEncounter* staticEnc, EncounterDef* def)
{
	if (staticEnc && staticEnc->defOverride)
		return staticEnc->defOverride->bAmbushEncounter;
	return def->bAmbushEncounter;
}
static bool GEGetNoDespawnForEncounterProp(OldStaticEncounter* staticEnc, EncounterDef* def)
{
	if (staticEnc)
		return staticEnc->noDespawn;
	return false;
}

static WorldEncounterDynamicSpawnType GEGetDynamicSpawnForEncounterProp(OldStaticEncounter* staticEnc, EncounterDef* def)
{
	if (staticEnc && staticEnc->defOverride)
		return staticEnc->defOverride->eDynamicSpawnType;
	return def->eDynamicSpawnType;
}

static int GEGetMaxLevelForEncounterProp(OldStaticEncounter* staticEnc, EncounterDef* def)
{
	if (staticEnc && (staticEnc->minLevel || staticEnc->maxLevel))
		return staticEnc->maxLevel;
	return def->maxLevel;
}

static int GEGetMinLevelForEncounterProp(OldStaticEncounter* staticEnc, EncounterDef* def)
{
	if (staticEnc && (staticEnc->minLevel || staticEnc->maxLevel))
		return staticEnc->minLevel;
	return def->minLevel;
}

static char* GEGetPatrolRouteForEncounterProp(OldStaticEncounter* staticEnc, EncounterLayer* encLayer)
{
	return (staticEnc ? staticEnc->patrolRouteName : NULL);
}

static char* GEGetSpawnAnimForEncounterProp(OldStaticEncounter* staticEnc, EncounterDef* def)
{
	if( staticEnc && staticEnc->spawnAnim ) return staticEnc->spawnAnim;
	return def->spawnAnim;
}

void GEEncounterPropUIRefresh(EncounterPropUI* encounterUI, EncounterDef*** pppEncDefs, 
							  OldStaticEncounter*** pppStaticEncs, EncounterLayer* encLayer, bool fullRefresh)
{
	char tmpStr[1024];
	const char* whichName = NULL;
	char *whichSpawnAnim = NULL, *whichScope = NULL;
	const char *pchOwnerMapVar = NULL;
	int whichMinLevel = 0;
	int whichMaxLevel = 0;
	U32 whichSpawnChance = 0;
	U32 whichSpawnRadius = 0;
	U32 whichWaveInterval = 0;
	U32 whichWaveMinDelay = 0;
	U32 whichWaveMaxDelay = 0;
	U32 whichLockoutRadius = 0;
	U32 whichRespawnTimer = 0;
	U32 whichGangID = 0;
	F32 whichEncValue = 0;
	char* whichPatrol = NULL;
	Expression* whichSpawnCond = NULL;
	Expression* whichSuccessCond = NULL;
	Expression* whichFailCond = NULL;
	OldEncounterAction* whichSuccessAction = NULL;
	OldEncounterAction* whichFailAction = NULL;
	Expression* whichWaveCond = NULL;
	CritterGroup* whichGroup = NULL;
	CritterFaction* whichFaction = NULL;
	int i, j, encDefCount = eaSize(pppEncDefs);
	int staticEncCount = pppStaticEncs?eaSize(pppStaticEncs):0;
	bool isInstanced = false;
	bool sameEncValue = true;
	bool bSpawnPerPlayer = false;
	bool bSameSpawnPerPlayer = true;
	bool bNoDespawn = true;
	bool bSameNoDespawn = true;
	bool bUsePlayerLevel = false;
	bool bSameUsePlayerLevel = true;
	bool bIsAmbush = false;
	bool bSameIsAmbush = true;
	bool bSnapToGround = false;
	bool bSameSnapToGround = true;
	WorldEncounterDynamicSpawnType eDynamicSpawnType = 0;
	bool bSameDynamicSpawn = true;

	eaClear(&encounterUI->defList);
	eaClear(&encounterUI->staticEncList);
	eaPushEArray(&encounterUI->defList, pppEncDefs);
	if(pppStaticEncs)
		eaPushEArray(&encounterUI->staticEncList, pppStaticEncs);

	GERefreshPatrolRouteList();

	if (!encDefCount)
	{
		// Destroy all the job editors for the old list of Jobs
		for (i = 0; i < eaSize(&encounterUI->aiJobDescs); i++)
			jobeditor_DestroyForJob(encounterUI->aiJobDescs[i]);
		eaClearStruct(&encounterUI->aiJobDescs, parse_AIJobDesc);
	}

	// Do a diff on all the fields we care about
	if (encDefCount)
	{
		bool sameMaxLevel = true;
		bool sameMinLevel = true;
		bool sameSpawnCond = true;
		bool sameSuccessCond = true;
		bool sameFailCond = true;
		bool sameSuccessAction = true;
		bool sameFailAction = true;
		bool sameWaveCond = true;
		bool sameWaveInterval = true;
		bool sameWaveMinDelay = true;
		bool sameWaveMaxDelay = true;
		bool sameRespawnTimer = true;
		bool sameSpawnChance = true;
		bool sameFaction = true;
		bool sameGroup = true;
		bool sameSpawnRadius = true;
		bool sameLockoutRadius = true;
		bool sameGangID = true;
		bool samePatrol = true;
		bool sameSpawnAnim = true;
		EncounterDef* firstEncDef = eaGet(pppEncDefs,0);
		OldStaticEncounter* firstStaticEnc = staticEncCount ? eaGet(pppStaticEncs,0) : NULL;
		CritterFaction* firstFaction = GEGetFactionForEncounterProp(firstStaticEnc, firstEncDef);
		CritterGroup* firstGroup = GEGetCritterGroupForEncounterProp(firstStaticEnc, firstEncDef);
		char* firstPatrolRoute = GEGetPatrolRouteForEncounterProp(firstStaticEnc, encLayer);
		char * firstSpawnAnim = GEGetSpawnAnimForEncounterProp(firstStaticEnc,firstEncDef);
		int firstMinLevel = GEGetMinLevelForEncounterProp(firstStaticEnc, firstEncDef);
		int firstMaxLevel = GEGetMaxLevelForEncounterProp(firstStaticEnc, firstEncDef);
		F32 firstEncValue = GEGetEncounterValueForEncounterProp(firstStaticEnc, firstEncDef, ui_SliderGetValue(encounterUI->teamSizeSlider));
		isInstanced = (firstStaticEnc && firstStaticEnc->defOverride && firstStaticEnc->defOverride->name);
		bSpawnPerPlayer = GEGetSpawnPerPlayerForEncounterProp(firstStaticEnc, firstEncDef);
		bNoDespawn = GEGetNoDespawnForEncounterProp(firstStaticEnc, firstEncDef);
		bUsePlayerLevel = GEGetUsePlayerLevelForEncounterProp(firstStaticEnc, firstEncDef);
		bIsAmbush = GEGetIsAmbushForEncounterProp(firstStaticEnc, firstEncDef);
		bSnapToGround = firstStaticEnc ? !firstStaticEnc->bNoSnapToGround : true;
		eDynamicSpawnType = GEGetDynamicSpawnForEncounterProp(firstStaticEnc, firstEncDef);
		pchOwnerMapVar = firstEncDef?firstEncDef->pchOwnerMapVar:NULL;
		assertmsg(!staticEncCount || (staticEncCount == encDefCount), "You must pass in either no static encounters or the same number as there are encounter defs");

		// Add all jobs to the job list.  If they are invalid, they will be removed later on.
		for (j = 0; j < eaSize(&firstEncDef->encJobs); j++)
		{
			int iJob, numJobs = eaSize(&encounterUI->aiJobDescs);
			for (iJob = 0; iJob < numJobs; iJob++)
			{
				if (StructCompare(parse_AIJobDesc, encounterUI->aiJobDescs[iJob], firstEncDef->encJobs[j], 0, 0, 0) == 0)
					break;
			}
			if (iJob == numJobs)
				eaPush(&encounterUI->aiJobDescs, StructClone(parse_AIJobDesc, firstEncDef->encJobs[j]));
		}

		for (i = 0; i < encDefCount; i++)
		{
			EncounterDef* currEncDef = eaGet(pppEncDefs,i);

			OldStaticEncounter* currStaticEnc = pppStaticEncs ? eaGet(pppStaticEncs,i) : NULL;
			isInstanced |= (currStaticEnc && currStaticEnc->defOverride && currStaticEnc->defOverride->name);
			CHECK_ENCOUNTER_FIELD(sameSpawnCond, s_EncounterSpawnCondIndex);
			CHECK_ENCOUNTER_FIELD(sameSuccessCond, s_EncounterSuccessCondIndex);
			CHECK_ENCOUNTER_FIELD(sameFailCond, s_EncounterFailCondIndex);
			CHECK_ENCOUNTER_FIELD(sameWaveCond, s_EncounterWaveCondIndex);
			if (sameSuccessAction && StructCompare(parse_OldEncounterAction, oldencounter_GetDefFirstAction(firstEncDef, EncounterState_Success), oldencounter_GetDefFirstAction(currEncDef, EncounterState_Success), 0, 0, 0))
				sameSuccessAction = false;
			if (sameFailAction && StructCompare(parse_OldEncounterAction, oldencounter_GetDefFirstAction(firstEncDef, EncounterState_Failure), oldencounter_GetDefFirstAction(currEncDef, EncounterState_Failure), 0, 0, 0))
				sameFailAction = false;
			CHECK_ENCOUNTER_FIELD(sameWaveInterval, s_EncounterWaveIntervalIndex);
			CHECK_ENCOUNTER_FIELD(sameWaveMinDelay, s_EncounterWaveMinDelayIndex);
			CHECK_ENCOUNTER_FIELD(sameWaveMaxDelay, s_EncounterWaveMaxDelayIndex);
			CHECK_ENCOUNTER_FIELD(sameSpawnChance, s_EncounterSpawnChanceIndex);
			CHECK_ENCOUNTER_FIELD(sameSpawnRadius, s_EncounterSpawnRadiusIndex);
			CHECK_ENCOUNTER_FIELD(sameLockoutRadius, s_EncounterLockoutRadiusIndex);
			CHECK_ENCOUNTER_FIELD(sameRespawnTimer, s_EncounterRespawnTimerIndex);
			CHECK_ENCOUNTER_FIELD(sameGangID, s_EncounterGangIDIndex);
			if (sameFaction && (firstFaction != GEGetFactionForEncounterProp(currStaticEnc, currEncDef))) sameFaction = false;
			if (sameGroup && (firstGroup != GEGetCritterGroupForEncounterProp(currStaticEnc, currEncDef))) sameGroup = false;
			if (samePatrol && (firstPatrolRoute != GEGetPatrolRouteForEncounterProp(currStaticEnc, encLayer))) samePatrol = false;
			if (sameMinLevel && (firstMinLevel != GEGetMinLevelForEncounterProp(currStaticEnc, currEncDef))) sameMinLevel = false;
			if (sameMaxLevel && (firstMaxLevel != GEGetMaxLevelForEncounterProp(currStaticEnc, currEncDef))) sameMaxLevel = false;
			if (sameEncValue && (firstEncValue != GEGetEncounterValueForEncounterProp(currStaticEnc, currEncDef, ui_SliderGetValue(encounterUI->teamSizeSlider)))) sameEncValue = false;
			if (sameSpawnAnim && (firstSpawnAnim != GEGetSpawnAnimForEncounterProp(currStaticEnc, currEncDef))) sameSpawnAnim = false;
			if (bSameSpawnPerPlayer && (bSpawnPerPlayer != GEGetSpawnPerPlayerForEncounterProp(currStaticEnc, currEncDef))) bSameSpawnPerPlayer = false;
			if (bSameUsePlayerLevel && (bUsePlayerLevel != GEGetUsePlayerLevelForEncounterProp(currStaticEnc, currEncDef))) bSameUsePlayerLevel = false;
			if (bSameIsAmbush && (bIsAmbush != GEGetIsAmbushForEncounterProp(currStaticEnc, currEncDef))) bSameIsAmbush = false;
			if (bSameSnapToGround && currStaticEnc && (bSnapToGround != (!currStaticEnc->bNoSnapToGround))) bSameSnapToGround = false;
			if (bSameNoDespawn && (bNoDespawn != GEGetNoDespawnForEncounterProp(currStaticEnc, currEncDef))) bSameNoDespawn = false;
			if (bSameDynamicSpawn && (eDynamicSpawnType != GEGetDynamicSpawnForEncounterProp(currStaticEnc, currEncDef))) bSameDynamicSpawn = false;
			if (pchOwnerMapVar && currEncDef->pchOwnerMapVar != pchOwnerMapVar) pchOwnerMapVar = NULL;

			// Remove any jobs from the Job List that aren't in this Encounter
			for (j = eaSize(&encounterUI->aiJobDescs)-1; j >= 0; --j)
			{
				int iJob, numJobs = eaSize(&currEncDef->encJobs);
				for (iJob = 0; iJob < numJobs; iJob++)
				{
					if (StructCompare(parse_AIJobDesc, encounterUI->aiJobDescs[j], currEncDef->encJobs[iJob], 0, 0, 0) == 0)
						break;
				}
				if (iJob == numJobs) // no match was found
				{
					AIJobDesc *jobToRemove = eaRemove(&encounterUI->aiJobDescs, j);
					jobeditor_DestroyForJob(jobToRemove);
					StructDestroy(parse_AIJobDesc, jobToRemove);
				}
			}

		}
		if (sameSpawnCond) whichSpawnCond = firstEncDef->spawnCond;
		if (sameSuccessCond) whichSuccessCond = firstEncDef->successCond;
		if (sameFailCond) whichFailCond = firstEncDef->failCond;
		if (sameSuccessAction) whichSuccessAction = oldencounter_GetDefFirstAction(firstEncDef, EncounterState_Success);
		if (sameFailAction) whichFailAction = oldencounter_GetDefFirstAction(firstEncDef, EncounterState_Failure);
		if (sameWaveCond) whichWaveCond = firstEncDef->waveCond;
		if (sameWaveInterval) whichWaveInterval = firstEncDef->waveInterval;
		if (sameWaveMinDelay) whichWaveMinDelay = firstEncDef->waveDelayMin;
		if (sameWaveMaxDelay) whichWaveMaxDelay = firstEncDef->waveDelayMax;
		if (sameSpawnChance) whichSpawnChance = firstEncDef->spawnChance;
		if (sameSpawnRadius) whichSpawnRadius = firstEncDef->spawnRadius;
		if (sameLockoutRadius) whichLockoutRadius = firstEncDef->lockoutRadius;
		if (sameRespawnTimer) whichRespawnTimer = firstEncDef->respawnTimer;
		if (sameGangID) whichGangID = firstEncDef->gangID;
		if (sameFaction) whichFaction = firstFaction;
		if (sameGroup) whichGroup = firstGroup;
		if (samePatrol) whichPatrol = firstPatrolRoute;
		if (sameMinLevel) whichMinLevel = firstMinLevel;
		if (sameMaxLevel) whichMaxLevel = firstMaxLevel;
		if (sameEncValue) whichEncValue = firstEncValue;
		if (sameSpawnAnim) whichSpawnAnim = firstSpawnAnim;
		if (staticEncCount == 1) whichName = firstStaticEnc->name;
		else if (!staticEncCount && (encDefCount == 1)) 
		{
			whichScope = firstEncDef->scope;
			whichName = firstEncDef->name;
		}
	}

	// Create or remove "Instanced" label
	if(isInstanced && !encounterUI->instancedLabel)
	{
		encounterUI->instancedLabel = ui_LabelCreate("Instanced", encounterUI->nameEntry->widget.x + encounterUI->nameEntry->widget.width + GE_SPACE, encounterUI->nameEntry->widget.y);
		ui_ExpanderAddChild(encounterUI->encPropExpander, encounterUI->instancedLabel);
	}
	else if(!isInstanced && encounterUI->instancedLabel)
	{
		ui_ExpanderRemoveChild(encounterUI->encPropExpander, encounterUI->instancedLabel);
		ui_LabelFreeInternal(encounterUI->instancedLabel);
		encounterUI->instancedLabel = NULL;
	}

	// Update the value label
	if (sameEncValue)
	{
		char tmp[32];
		sprintf(tmp, "Strength: %.2f", whichEncValue);
		ui_LabelSetText(encounterUI->encounterValue, tmp);
	}
	else
	{
		ui_LabelSetText(encounterUI->encounterValue, "");
	}

	// Update the checkboxes
	ui_CheckButtonSetState(encounterUI->spawnForPlayerButton, bSameSpawnPerPlayer && bSpawnPerPlayer);
	ui_CheckButtonSetState(encounterUI->usePlayerLevelButton, bSameUsePlayerLevel && bUsePlayerLevel);
	ui_CheckButtonSetState(encounterUI->noDespawnButton, bSameNoDespawn && bNoDespawn);
	ui_CheckButtonSetState(encounterUI->ambushCheckBox, bSameIsAmbush && bIsAmbush);
	if(encounterUI->snapToGroundCheckBox)
		ui_CheckButtonSetState(encounterUI->snapToGroundCheckBox, bSameSnapToGround && bSnapToGround);

	if (bSameDynamicSpawn){
		ui_ComboBoxSetSelectedEnum(encounterUI->dynamicSpawnCombo, eDynamicSpawnType);
	} else {
		ui_ComboBoxSetSelectedEnum(encounterUI->dynamicSpawnCombo, -1);
	}

	// Update other fields if fullRefresh is true
	if(fullRefresh)
	{
		ResourceInfo* tmpResInfo;

		ui_EditableSetText(encounterUI->nameEntry, whichName ? whichName : "");
		if (encounterUI->scopeEntry)
			ui_EditableSetText(encounterUI->scopeEntry, whichScope ? whichScope : "");
		ui_ButtonSetText(encounterUI->spawnExpr, whichSpawnCond ? exprGetCompleteString(whichSpawnCond) : "");
		ui_ButtonSetText(encounterUI->successExpr, whichSuccessCond ? exprGetCompleteString(whichSuccessCond) : "");
		ui_ButtonSetText(encounterUI->failExpr, whichFailCond ? exprGetCompleteString(whichFailCond) : "");
		ui_ButtonSetText(encounterUI->successAction, whichSuccessAction ? exprGetCompleteString(whichSuccessAction->actionExpr) : "");
		ui_ButtonSetText(encounterUI->failAction, whichFailAction ? exprGetCompleteString(whichFailAction->actionExpr) : "");
		ui_ButtonSetText(encounterUI->waveCondExpr, whichWaveCond ? exprGetCompleteString(whichWaveCond) : "");
		sprintf(tmpStr, "%u", whichSpawnChance);
		ui_LabelSetText(encounterUI->chanceCount, tmpStr);
		ui_IntSliderSetValueAndCallbackEx(encounterUI->chanceSlider, 0, whichSpawnChance, 0);
		sprintf(tmpStr, "%u", whichWaveInterval);
		ui_EditableSetText(encounterUI->waveIntervalEntry, tmpStr);
		sprintf(tmpStr, "%u", whichWaveMinDelay);
		ui_EditableSetText(encounterUI->waveDelayMinEntry, !whichWaveMinDelay ? "" : tmpStr);
		sprintf(tmpStr, "%u", whichWaveMaxDelay);
		ui_EditableSetText(encounterUI->waveDelayMaxEntry, !whichWaveMaxDelay ? "" : tmpStr);
		sprintf(tmpStr, "%u", whichSpawnRadius);
		ui_EditableSetText(encounterUI->spawnRadiusEntry, tmpStr);
		sprintf(tmpStr, "%u", whichLockoutRadius);
		ui_EditableSetText(encounterUI->lockoutRadiusEntry, tmpStr);
		sprintf(tmpStr, "%u", whichRespawnTimer);
		ui_EditableSetText(encounterUI->respawnTimeEntry, tmpStr);
		sprintf(tmpStr, "%u", whichGangID);
		ui_EditableSetText(encounterUI->gangIDEntry, tmpStr);
		sprintf(tmpStr, "%i", whichMinLevel);
		ui_EditableSetText(encounterUI->minLevelEntry, !whichMinLevel ? "" : tmpStr);
		sprintf(tmpStr, "%i", whichMaxLevel);
		ui_EditableSetText(encounterUI->maxLevelEntry, !whichMaxLevel ? "" : tmpStr);
		tmpResInfo = whichFaction ? resGetInfo(g_hCritterFactionDict, whichFaction->pchName) : NULL;
		ui_ComboBoxSetSelectedObject(encounterUI->factionCombo, tmpResInfo);
		tmpResInfo = whichGroup ? resGetInfo(g_hCritterGroupDict, whichGroup->pchName) : NULL;
		ui_ComboBoxSetSelectedObject(encounterUI->groupCombo, tmpResInfo);
		if(encounterUI->spawnAnim)
			ui_EditableSetText(encounterUI->spawnAnim, whichSpawnAnim ? whichSpawnAnim : "");
		if (encounterUI->patrolCombo)
			ui_ComboBoxSetSelectedObject(encounterUI->patrolCombo, whichPatrol);
		ui_EditableSetText(encounterUI->ownerMapVarEntry, pchOwnerMapVar?pchOwnerMapVar:"");
	}
	GEListRefresh(encounterUI->jobList);
}

void GEEncounterPropUIRefreshSingle(EncounterPropUI* encounterUI, EncounterDef* pEncDef, OldStaticEncounter*** pppStaticEncs, EncounterLayer* encLayer, bool fullRefresh)
{
	EncounterDef** ppEncDef = NULL;
	eaPush(&ppEncDef, pEncDef);

	GEEncounterPropUIRefresh(encounterUI, &ppEncDef, pppStaticEncs, encLayer, fullRefresh);

	eaDestroy(&ppEncDef);
}


EncounterPropUI* GEEncounterPropUICreate(GEEditorDocPtr geDoc, int y, UIActivationFunc nameChangeFunc, UIActivationFunc ambushCheckBoxChangeFunc, UIActivationFunc spawnForPlayerChangeFunc, 
										 UIActivationFunc snapToGroundChangeFunc, UIActivationFunc noDespawnChangeFunc, UIComboBoxEnumFunc dynamicSpawnChangeFunc, ExprEdExprFunc spawnChangeFunc,
										 ExprEdExprFunc successChangeFunc, ExprEdExprFunc failChangeFunc,
										 ExprEdExprFunc successActionChangeFunc, ExprEdExprFunc failActionChangeFunc,
										 ExprEdExprFunc waveCondChangeFunc, UIActivationFunc waveIntervalChangeFunc, 
										 UIActivationFunc waveMinDelayChangeFunc, UIActivationFunc waveMaxDelayChangeFunc, 
										 UIActivationFunc spawnRadChangeFunc,
										 UIActivationFunc lockoutRadChangeFunc, UISliderChangeFunc teamSizeChangeFunc, U32 initTeamSize,  UIActivationFunc multiTeamSizeSelectFunc,
										 UIActivationFunc critterGroupChangeFunc, UIActivationFunc factionChangeFunc, UIActivationFunc gangIDChangeFunc, UISliderChangeFunc chanceChangeFunc,
										 UIActivationFunc patrolChangeFunc, const char*** patrolListPtr, UIActivationFunc minLevelChangeFunc, UIActivationFunc maxLevelChangeFunc,
										 UIActivationFunc usePlayerLevelChangeFunc, UIActivationFunc spawnAnimChangeFunc, UIActivationFunc respawnTimeChangeFunc, UIActivationFunc scopeChangeFunc)
{
	int currY = GE_SPACE;
	char countStr[64];
	UILabel* widgetLabel;
	EncounterPropUI* encounterUI = calloc(1, sizeof(EncounterPropUI));
	encounterUI->encPropExpander = ui_ExpanderCreate("Encounter Properties", 0);

	// EncounterName TextEntry
	currY = GETextEntryCreate(&encounterUI->nameEntry, "Name", "", 0, currY, 180, 0, GE_ENCLABEL_WIDTH, GE_VALIDFUNC_NOSPACE, nameChangeFunc, geDoc, encounterUI->encPropExpander) + GE_SPACE;

	// Encounter Scope TextEntry
	if (scopeChangeFunc)
	{
		currY = GETextEntryCreate(&encounterUI->scopeEntry, "Scope", "", 0, currY, 180, 0, GE_ENCLABEL_WIDTH, GE_VALIDFUNC_NOSPACE, scopeChangeFunc, geDoc, encounterUI->encPropExpander) + GE_SPACE;
	}

	// Level Override
	GETextEntryCreate(&encounterUI->minLevelEntry, "Level Range", "", 0, currY, 50, 0, GE_ENCLABEL_WIDTH, GE_VALIDFUNC_LEVEL, minLevelChangeFunc, geDoc, encounterUI->encPropExpander);
	GETextEntryCreate(&encounterUI->maxLevelEntry, "-", "", 50 + GE_ENCLABEL_WIDTH + GE_SPACE, currY, 50, 0, 10, GE_VALIDFUNC_LEVEL, maxLevelChangeFunc, geDoc, encounterUI->encPropExpander);

	// Use Player Level button
	encounterUI->usePlayerLevelButton = ui_CheckButtonCreate(50 + GE_ENCLABEL_WIDTH + GE_SPACE*3 + 50 + 10, currY, "Use Player Level", false);
	ui_CheckButtonSetToggledCallback(encounterUI->usePlayerLevelButton, usePlayerLevelChangeFunc, geDoc);
	ui_ExpanderAddChild(encounterUI->encPropExpander, encounterUI->usePlayerLevelButton);
	currY += encounterUI->usePlayerLevelButton->widget.height + GE_SPACE;

	// Encounter Value label
	encounterUI->encounterValue = ui_LabelCreate("", GE_ENCLABEL_WIDTH, currY);
	ui_ExpanderAddChild(encounterUI->encPropExpander, encounterUI->encounterValue);
	currY += encounterUI->encounterValue->widget.height + GE_SPACE;

	// SpawnCondition button and expression
	encounterUI->spawnForPlayerButton = ui_CheckButtonCreate(GE_ENCLABEL_WIDTH, currY, "Check Spawn When For Each Player", false);
	ui_CheckButtonSetToggledCallback(encounterUI->spawnForPlayerButton, spawnForPlayerChangeFunc, geDoc);
	ui_ExpanderAddChild(encounterUI->encPropExpander, encounterUI->spawnForPlayerButton);
	currY += encounterUI->spawnForPlayerButton->widget.height + GE_SPACE;
	currY = GEExpressionEditButtonCreate(&encounterUI->spawnExpr, NULL, "Spawn When", NULL, 0, currY, 300, GE_ENCLABEL_WIDTH, spawnChangeFunc, geDoc, encounterUI->encPropExpander) + GE_SPACE;

	// SuccessCondition
	currY = GEExpressionEditButtonCreate(&encounterUI->successExpr, NULL, "Succeed When", NULL, 0, currY, 300, GE_ENCLABEL_WIDTH, successChangeFunc, geDoc, encounterUI->encPropExpander) + GE_SPACE;

	// FailCondition
	currY = GEExpressionEditButtonCreate(&encounterUI->failExpr, NULL, "Fail When", NULL, 0, currY, 300, GE_ENCLABEL_WIDTH, failChangeFunc, geDoc, encounterUI->encPropExpander) + GE_SPACE;

	// SuccessAction
	currY = GEExpressionEditButtonCreate(&encounterUI->successAction, NULL, "Success Actions", NULL, 0, currY, 300, GE_ENCLABEL_WIDTH, successActionChangeFunc, geDoc, encounterUI->encPropExpander) + GE_SPACE;

	// FailAction
	currY = GEExpressionEditButtonCreate(&encounterUI->failAction, NULL, "Fail Actions", NULL, 0, currY, 300, GE_ENCLABEL_WIDTH, failActionChangeFunc, geDoc, encounterUI->encPropExpander) + GE_SPACE;

	// WaveCondition
	currY = GEExpressionEditButtonCreate(&encounterUI->waveCondExpr, NULL, "Reinforce While", NULL, 0, currY, 300, GE_ENCLABEL_WIDTH, waveCondChangeFunc, geDoc, encounterUI->encPropExpander) + GE_SPACE;

	// WaveInterval TextEntry
	GETextEntryCreate(&encounterUI->waveIntervalEntry, "Wave Interval", "", 0, currY, 50, 0, GE_ENCLABEL_WIDTH, GE_VALIDFUNC_INTEGER, waveIntervalChangeFunc, geDoc, encounterUI->encPropExpander);

	// WaveDelay range
	GETextEntryCreate(&encounterUI->waveDelayMinEntry, "Delay", "", 190 + GE_SPACE, currY, 55, 0, 50, GE_VALIDFUNC_LEVEL, waveMinDelayChangeFunc, geDoc, encounterUI->encPropExpander);
	currY = GETextEntryCreate(&encounterUI->waveDelayMaxEntry, "-", "", 200 + GE_ENCLABEL_WIDTH + 2*GE_SPACE, currY, 50, 0, 10, GE_VALIDFUNC_LEVEL, waveMaxDelayChangeFunc, geDoc, encounterUI->encPropExpander) + GE_SPACE;

	// SpawnRadius TextEntry
	currY = GETextEntryCreate(&encounterUI->spawnRadiusEntry, "Spawn Radius", "", 0, currY, 50, 0, GE_ENCLABEL_WIDTH, GE_VALIDFUNC_INTEGER, spawnRadChangeFunc, geDoc, encounterUI->encPropExpander) + GE_SPACE;

	// LockoutRadius TextEntry
	currY = GETextEntryCreate(&encounterUI->lockoutRadiusEntry, "Lockout Radius", "", 0, currY, 50, 0, GE_ENCLABEL_WIDTH, GE_VALIDFUNC_INTEGER, lockoutRadChangeFunc, geDoc, encounterUI->encPropExpander) + GE_SPACE;

	// Respawn Time text entry
	currY = GETextEntryCreate(&encounterUI->respawnTimeEntry, "Respawn Time", "", 0, currY, 50, 0, GE_ENCLABEL_WIDTH, GE_VALIDFUNC_INTEGER, respawnTimeChangeFunc, geDoc, encounterUI->encPropExpander) + GE_SPACE;

	// Dynamic Spawn Type
	widgetLabel = ui_LabelCreate("Dynamic Spawn", 0, currY); 
	encounterUI->dynamicSpawnCombo = ui_ComboBoxCreateWithEnum(GE_ENCLABEL_WIDTH, currY, 150, WorldEncounterDynamicSpawnTypeEnum, dynamicSpawnChangeFunc, geDoc);
	ui_ExpanderAddChild(encounterUI->encPropExpander, encounterUI->dynamicSpawnCombo);
	ui_ExpanderAddChild(encounterUI->encPropExpander, widgetLabel);
	currY += encounterUI->dynamicSpawnCombo->widget.height + GE_SPACE;

	// No despawn button
	encounterUI->noDespawnButton = ui_CheckButtonCreate(GE_ENCLABEL_WIDTH, currY, "Never Despawn (Warning: expensive!)", false);
	ui_CheckButtonSetToggledCallback(encounterUI->noDespawnButton, noDespawnChangeFunc, geDoc);
	ui_ExpanderAddChild(encounterUI->encPropExpander, encounterUI->noDespawnButton);
	currY += encounterUI->noDespawnButton->widget.height + GE_SPACE;

	// SpawnChance Slider
	widgetLabel = ui_LabelCreate("Chance", 0, currY);
	sprintf(countStr, "%i", 0);
	encounterUI->chanceSlider = ui_IntSliderCreate(GE_ENCLABEL_WIDTH, currY, 100, 0, 100, 0);
	encounterUI->chanceCount = ui_LabelCreate(countStr, GE_ENCLABEL_WIDTH + 100 + GE_SPACE, currY);
	ui_SliderSetPolicy(encounterUI->chanceSlider, UISliderContinuous);
	ui_SliderSetChangedCallback(encounterUI->chanceSlider, chanceChangeFunc, geDoc);
	ui_ExpanderAddChild(encounterUI->encPropExpander, encounterUI->chanceSlider);
	ui_ExpanderAddChild(encounterUI->encPropExpander, encounterUI->chanceCount);
	ui_ExpanderAddChild(encounterUI->encPropExpander, widgetLabel);
	currY += (encounterUI->chanceSlider->widget.height + GE_SPACE);

	// Ambush Encounter checkbox
	encounterUI->ambushCheckBox = ui_CheckButtonCreate(GE_ENCLABEL_WIDTH, currY, "Ambush Encounter", false);
	ui_CheckButtonSetToggledCallback(encounterUI->ambushCheckBox, ambushCheckBoxChangeFunc, geDoc);
	ui_ExpanderAddChild(encounterUI->encPropExpander, encounterUI->ambushCheckBox);
	currY += (encounterUI->ambushCheckBox->widget.height + GE_SPACE);

	// Don't snap to ground checkbox
	if (snapToGroundChangeFunc)
	{
		encounterUI->snapToGroundCheckBox = ui_CheckButtonCreate(GE_ENCLABEL_WIDTH, currY, "Snap To Ground", true);
		ui_CheckButtonSetToggledCallback(encounterUI->snapToGroundCheckBox, snapToGroundChangeFunc, geDoc);
		ui_ExpanderAddChild(encounterUI->encPropExpander, encounterUI->snapToGroundCheckBox);
		currY += (encounterUI->snapToGroundCheckBox->widget.height + GE_SPACE);
	}

	// Owner MapVar text entry
	currY = GETextEntryCreate(&encounterUI->ownerMapVarEntry, "Owner MapVar", "", 0, currY, 150, 0, GE_ENCLABEL_WIDTH, GE_VALIDFUNC_NOSPACE, GEEncounterOwnerMapVarChanged, encounterUI, encounterUI->encPropExpander) + GE_SPACE;

	// Group ComboBox
	widgetLabel = ui_LabelCreate("Group", 0, currY);
	encounterUI->groupCombo = ui_ComboBoxCreateWithGlobalDictionary(GE_ENCLABEL_WIDTH, currY, 150, g_hCritterGroupDict, "resourceName");
//	ui_ComboBoxSetDefaultDisplayString(encounterUI->groupCombo, "No Group", true);
	ui_ComboBoxSetDefaultDisplayString(encounterUI->groupCombo, "No Group");
	ui_ComboBoxSetSelectedCallback(encounterUI->groupCombo, critterGroupChangeFunc, geDoc);
	ui_ExpanderAddChild(encounterUI->encPropExpander, encounterUI->groupCombo);
	ui_ExpanderAddChild(encounterUI->encPropExpander, widgetLabel);
	currY += (encounterUI->groupCombo->widget.height + GE_SPACE);

	// Faction ComboBox
	widgetLabel = ui_LabelCreate("Faction", 0, currY);
	encounterUI->factionCombo = ui_ComboBoxCreateWithGlobalDictionary(GE_ENCLABEL_WIDTH, currY, 150, g_hCritterFactionDict, "resourceName");
//	ui_ComboBoxSetDefaultDisplayString(encounterUI->factionCombo, "No Faction", true);
	ui_ComboBoxSetDefaultDisplayString(encounterUI->factionCombo, "No Faction");
	ui_ComboBoxSetSelectedCallback(encounterUI->factionCombo, factionChangeFunc, geDoc);
	ui_ExpanderAddChild(encounterUI->encPropExpander, encounterUI->factionCombo);
	ui_ExpanderAddChild(encounterUI->encPropExpander, widgetLabel);
	
	// Gang ID
	currY = GETextEntryCreate(&encounterUI->gangIDEntry, "Gang", "", GE_ENCLABEL_WIDTH+175+GE_SPACE, currY, 50, 0, GE_ENCLABEL_WIDTH-50, GE_VALIDFUNC_INTEGER, gangIDChangeFunc, geDoc, encounterUI->encPropExpander);

	// Spawn Anim
	if(spawnAnimChangeFunc)
	{
		currY += (encounterUI->factionCombo->widget.height + GE_SPACE);
		currY = GETextEntryCreate(&encounterUI->spawnAnim, "Spawn Anim", "", 0, currY, 180, 0, GE_ENCLABEL_WIDTH, GE_VALIDFUNC_INTEGER, spawnAnimChangeFunc, geDoc, encounterUI->encPropExpander);
	}
	else
	{
		encounterUI->spawnAnim = NULL;
	}

	// Patrol ComboBox
	if (patrolListPtr && patrolChangeFunc)
	{
		widgetLabel = ui_LabelCreate("Patrol", 0, currY);
		encounterUI->patrolCombo = ui_ComboBoxCreate(GE_ENCLABEL_WIDTH, currY, 150, NULL, patrolListPtr, NULL);
//		ui_ComboBoxSetDefaultDisplayString(encounterUI->patrolCombo, "No Patrol Route", true);
		ui_ComboBoxSetDefaultDisplayString(encounterUI->patrolCombo, "No Patrol Route");
		ui_ComboBoxSetSelectedCallback(encounterUI->patrolCombo, patrolChangeFunc, geDoc);
		ui_ExpanderAddChild(encounterUI->encPropExpander, encounterUI->patrolCombo);
		ui_ExpanderAddChild(encounterUI->encPropExpander, widgetLabel);
		currY += (encounterUI->patrolCombo->widget.height + GE_SPACE);
	}
	else
	{
		encounterUI->patrolCombo = NULL;
	}

	// Slider should always be active
	widgetLabel = ui_LabelCreate("Team Size", 0, currY);
	sprintf(countStr, "%i", initTeamSize);
	encounterUI->teamSizeSlider = ui_IntSliderCreate(GE_ENCLABEL_WIDTH, currY, 100, 1, MAX_TEAM_SIZE, initTeamSize);
	encounterUI->teamSizeCount = ui_LabelCreate(countStr, GE_ENCLABEL_WIDTH + 100 + GE_SPACE, currY);
	ui_SliderSetPolicy(encounterUI->teamSizeSlider, UISliderContinuous);
	ui_SliderSetChangedCallback(encounterUI->teamSizeSlider, teamSizeChangeFunc, geDoc);
	ui_ExpanderAddChild(encounterUI->encPropExpander, encounterUI->teamSizeSlider);
	ui_ExpanderAddChild(encounterUI->encPropExpander, encounterUI->teamSizeCount);
	ui_ExpanderAddChild(encounterUI->encPropExpander, widgetLabel);

	currY += (encounterUI->teamSizeSlider->widget.height + GE_SPACE);

	// Expander for Jobs
	encounterUI->jobExpander = ui_ExpanderCreate("Jobs", 0);
	ui_WidgetSetPosition(UI_WIDGET(encounterUI->jobExpander), 0, currY);
	ui_WidgetSetWidthEx(UI_WIDGET(encounterUI->jobExpander), 1.0f, UIUnitPercentage);
	ui_ExpanderSetOpened(encounterUI->jobExpander, false);
	ui_ExpanderAddChild(encounterUI->encPropExpander, encounterUI->jobExpander);
	ui_ExpanderSetExpandCallback(encounterUI->jobExpander, GEJobUIExpanderToggleCB, encounterUI);
	currY += encounterUI->jobExpander->widget.height;
	encounterUI->jobList = GEListCreate(&encounterUI->aiJobDescs, 0, 0, encounterUI->jobExpander, encounterUI, GEJobUICreate, GEJobCreate, GEJobUIDestroy, GEJobUIListHeightChangedCB, false, true);

	// Set the window height
	encounterUI->encPropExpander->openedHeight = currY;
	ui_ExpanderSetOpened(encounterUI->encPropExpander, true);

	return encounterUI;
}


// ----------------------------------------------------------------------------------
// Actor Var UI
// ----------------------------------------------------------------------------------

static bool GEVarIsEqual(OldEncounterVariable *pVarA, OldEncounterVariable *pVarB, FSMExternVar* externVar)
{
	if(!pVarA && !pVarB)
		return true;
	else if(!pVarA || !pVarB)
		return false;

	// Messages are the special case
	if(externVar->scType && (stricmp(externVar->scType, "message") == 0))
	{
		const Message* msgA = pVarA->message.pEditorCopy?pVarA->message.pEditorCopy:GET_REF(pVarA->message.hMessage);
		const Message* msgB = pVarB->message.pEditorCopy?pVarB->message.pEditorCopy:GET_REF(pVarB->message.hMessage);

		if((!msgA || !msgA->pcDefaultString) && (!msgB || !msgB->pcDefaultString))
			return true;
		if(!msgA || !msgA->pcDefaultString || !msgB || !msgB->pcDefaultString)
			return false;

		return(0 == stricmp(msgA->pcDefaultString, msgB->pcDefaultString));
	}
	else
	{
		const char *pchStringA = MultiValGetString(&pVarA->varValue, NULL);
		const char *pchStringB = MultiValGetString(&pVarB->varValue, NULL);

		if (!pchStringA && !pchStringB){
			return true;
		} else if (!pchStringA || !pchStringB){
			return false;
		}

		// Do a strcmp
		return(0 == stricmp(pchStringA, pchStringB));
	}
}

// Add all the fsm vars from the actor passed in
static void GEActorVarUIPopulate(ActorPropUI* actorUI, 
								 FSMExternVar*** externVarList, OldEncounterVariable*** vars,
								 bool* allowVarEdit)
{
	UILabel* currLabel;
	UIEditable* currEditable;
	int retHeight = -GEAD_SPACE;
	int i, numVars = eaSize(externVarList);
	eaDestroyEx(&actorUI->varUIList, ui_WidgetQueueFree);
	for (i = 0; i < numVars; i++)
	{
		FSMExternVar* curVar = (*externVarList)[i];
		if (curVar->scType && (stricmp(curVar->scType, "message") == 0))
		{
			const Message *pMsg = NULL;
			if ((*vars)[i])
				pMsg = (*vars)[i]->message.pEditorCopy?(*vars)[i]->message.pEditorCopy:GET_REF((*vars)[i]->message.hMessage);
			retHeight = GECreateMessageEntryEx(&currEditable, &currLabel, curVar->name, pMsg, GEAD_XPOS, retHeight + GEAD_SPACE, GEAD_TEXT_WIDTH, 200, actorUI->varMessageChanged, strdup(curVar->name), actorUI->varExpander, false, false);
		}
		else
		{
			const char *pchStringVal = NULL;
			int validFunc;
			MultiValType curFlaglessType = MULTI_FLAGLESS_TYPE(curVar->type);

			if ((*vars)[i])
				pchStringVal = MultiValGetString(&(*vars)[i]->varValue, NULL);

			if(curFlaglessType == MULTI_INT)
				validFunc = GE_VALIDFUNC_INTEGER;
			else if(curFlaglessType == MULTI_FLOAT)
				validFunc = GE_VALIDFUNC_FLOAT;
			else
				validFunc = GE_VALIDFUNC_NONE;

			retHeight = GETextEntryCreateEx(&currEditable, &currLabel, curVar->name, pchStringVal, NULL, NULL, NULL, NULL, NULL, GEAD_XPOS, retHeight + GEAD_SPACE, GEAD_TEXT_WIDTH, GEAD_TEXT_WIDTH, 200, validFunc, actorUI->varChanged, strdup((*externVarList)[i]->name), actorUI->varExpander);
		}
		ui_WidgetSetFreeCallback(UI_WIDGET(currEditable), GETextEntryFreeDataCB);
		if(!allowVarEdit[i] && currEditable)
			ui_SetActive((UIWidget*) currEditable, false);

		actorUI->varExpander->openedHeight = retHeight;
		eaPush(&actorUI->varUIList, (UIWidget*)currEditable);
		eaPush(&actorUI->varUIList, (UIWidget*)currLabel);
	}
	actorUI->varExpander->openedHeight = MAX(0, retHeight);
	if(retHeight > 0)
		ui_ExpanderSetOpened(actorUI->varExpander, true);
	else
		ui_ExpanderSetOpened(actorUI->varExpander, false);
	ui_ExpanderReflow(actorUI->varExpander);
}


// ----------------------------------------------------------------------------------
// Actor Properties UI callbacks
// ----------------------------------------------------------------------------------

static bool GEActorChangeCritterType(OldActor *pActor, OldActor *pBaseActor, /*Actor1CritterType*/ const void *type)
{
	OldActorInfo *pInfo = GEGetActorInfoForUpdate(pActor, pBaseActor);
	Actor1CritterType eType = PTR_TO_S32(type);
	if (pInfo && pInfo->eCritterType != eType && StaticDefineIntRevLookup(Actor1CritterTypeEnum, eType)){
		pInfo->eCritterType = eType;
		
		// Clear unneeded data
		if (eType == Actor1CritterType_Nemesis){
			REMOVE_HANDLE(pInfo->critterGroup);
			pInfo->pcCritterRank = NULL;
		} else if (eType == Actor1CritterType_NemesisMinion){
			REMOVE_HANDLE(pInfo->critterDef);
		}

		return true;
	}
	return false;
}

static void GEActorCritterTypeComboChanged(UIComboBox *pWidget, /*Actor1CritterType*/ int eType, ActorPropUI* pUI)
{
	GEEditorDocPtr pDoc = pUI->pDoc;

	if (pUI->modifyActorsFunc)
		pUI->modifyActorsFunc(pDoc, GEActorChangeCritterType, S32_TO_PTR(eType));
}

static bool GEActorChangeCritterGroup(OldActor *pActor, OldActor *pBaseActor, const char *pchGroup)
{
	OldActorInfo *pInfo = GEGetActorInfoForUpdate(pActor, pBaseActor);
	if (pInfo && pchGroup){
		const char *pchCurrentGroup = REF_STRING_FROM_HANDLE(pInfo->critterGroup);
		if (!pchCurrentGroup || stricmp(pchCurrentGroup, pchGroup) != 0){
			SET_HANDLE_FROM_STRING(g_hCritterGroupDict, pchGroup, pInfo->critterGroup);
			return true;
		}
	}
	return false;
}

static void GEActorCritterGroupComboChanged(UITextEntry *pWidget, ActorPropUI* pUI)
{
	GEEditorDocPtr pDoc = pUI->pDoc;

	if (pUI->modifyActorsFunc)
		pUI->modifyActorsFunc(pDoc, GEActorChangeCritterGroup, ui_TextEntryGetText(pWidget));
}

// ----------------------------------------------------------------------------------
// Actor Properties UI
// ----------------------------------------------------------------------------------

static FSMState* GEActorGetFSMState(FSM* fsm, char* stateName)
{
	int i, n = eaSize(&fsm->states);
	for (i = 0; i < n; i++)
	{
		FSMState* fsmState = fsm->states[i];
		if (!stricmp(stateName, fsmState->name))
			return fsmState;
	}
	return NULL;
}

static void GEActorPropUIReflowGroup(UIExpanderGroup* expanderGroup, ActorPropUI* actorUI)
{
	actorUI->actorPropExpander->openedHeight = expanderGroup->widget.y + expanderGroup->totalHeight;
	ui_ExpanderReflow(actorUI->actorPropExpander);
}

ActorPropUI* GEActorPropUICreate(GEEditorDocPtr geDoc, 

								 // Please don't add any more callbacks like this...
								 UIActivationFunc nameChangeFunc, UIActivationFunc selCritterFunc, UIActivationFunc selFSMFunc, 
								 UIActivationFunc openFSMFunc, UIActivationFunc seChangedFunc, UIActivationFunc selRankFunc,
								 UIActivationFunc selSubRankFunc, UIActivationFunc selFactionFunc,
								 UIActivationFunc dialogStateChanged, UIActivationFunc dialogTextChanged, UIActivationFunc dialogAudioChanged, UIActivationFunc dialogWhenChanged,
								 UIActivationFunc selContactFunc, UIActivationFunc dispNameChangedFunc, ExprEdExprFunc spawnWhenChangedFunc, UIActivationFunc interactCondChangedFunc,
								 UIActivationFunc bossBarChangedFunc, UIActivationFunc varChangedFunc, UIActivationFunc varMessageChangedFunc,
								 UIActivationFunc spawnAnimChangedFunc,

								 // ...Just use this instead :)
								 GEModifySelectedActorsFunc modifyActorsFunc)
{
	DictionaryEArrayStruct *pArray = resDictGetEArrayStruct(g_hCritterDefDict);
	DictionaryEArrayStruct *pArrayFaction = resDictGetEArrayStruct(g_hCritterFactionDict);
	DictionaryEArrayStruct *pArrayGroup = resDictGetEArrayStruct(g_hCritterGroupDict);
	int i, currY = GE_SPACE;
	UILabel* widgetLabel;
	ActorPropUI* actorUI = calloc(1, sizeof(ActorPropUI));
	actorUI->pDoc = geDoc;
	actorUI->modifyActorsFunc = modifyActorsFunc;
	actorUI->actorPropExpander = ui_ExpanderCreate("Actor Properties", 0);

	// Actor name
	currY = GETextEntryCreate(&actorUI->nameEntry, "Name", "", 0, currY, 200, 0, GE_ACTLABEL_WIDTH, GE_VALIDFUNC_NOSPACE, nameChangeFunc, geDoc, actorUI->actorPropExpander) + GE_SPACE;

	// Spawn Type
	widgetLabel = ui_LabelCreate("Spawn Type", 0, currY);
	actorUI->critterTypeCombo = ui_ComboBoxCreateWithEnum(GE_ACTLABEL_WIDTH, currY, 200, Actor1CritterTypeEnum, GEActorCritterTypeComboChanged, actorUI);
	ui_ExpanderAddChild(actorUI->actorPropExpander, widgetLabel);
	ui_ExpanderAddChild(actorUI->actorPropExpander, actorUI->critterTypeCombo);
	currY += (actorUI->critterTypeCombo->widget.height + GE_SPACE);

	// TypeCritter Widgets - CritterName Combobox
	actorUI->critterLabel = ui_LabelCreate("Critter", 0, currY);
	actorUI->critterCombo = ui_TextEntryCreateWithGlobalDictionaryCombo("Random Critter", GE_ACTLABEL_WIDTH, currY, "CritterDef", "resourceName", true, true, false, true);
	actorUI->critterCombo->widget.width = 200;
//	ui_ComboBoxSetDefaultDisplayString(((UITextEntry*)actorUI->critterCombo)->cb, "Random Critter", true);
	ui_ComboBoxSetDefaultDisplayString(((UITextEntry*)actorUI->critterCombo)->cb, "Random Critter");
	ui_TextEntrySetFinishedCallback(actorUI->critterCombo, selCritterFunc, geDoc);
	ui_ExpanderAddChild(actorUI->actorPropExpander, actorUI->critterLabel);
	ui_ExpanderAddChild(actorUI->actorPropExpander, actorUI->critterCombo);
	currY += (actorUI->critterCombo->widget.height + GE_SPACE);

	// CritterGroup combo box
	currY = GETextEntryCreateEx(&actorUI->groupCombo, &actorUI->critterGroupLabel, "CritterGroup", NULL, NULL, g_hCritterGroupDict, NULL, NULL, NULL, 0, currY, 150, 0, GE_ACTLABEL_WIDTH, 0, GEActorCritterGroupComboChanged, actorUI, actorUI->actorPropExpander);
	currY += GE_SPACE;

	// TypeRandomCritter Widgets - Rank, SubRank, and Group Combobox
	widgetLabel = ui_LabelCreate("Type", 0, currY);
	actorUI->rankCombo = ui_ComboBoxCreate(GE_ACTLABEL_WIDTH, currY, 90, NULL, &g_eaCritterRankNames, NULL);
	actorUI->subRankCombo = ui_ComboBoxCreate(174, currY, 65, NULL, &g_eaCritterSubRankNames, NULL);
	ui_ComboBoxSetSelectedCallback(actorUI->rankCombo, selRankFunc, geDoc);
	ui_ComboBoxSetSelectedCallback(actorUI->subRankCombo, selSubRankFunc, geDoc);
	ui_ExpanderAddChild(actorUI->actorPropExpander, widgetLabel);
	ui_ExpanderAddChild(actorUI->actorPropExpander, actorUI->rankCombo);
	ui_ExpanderAddChild(actorUI->actorPropExpander, actorUI->subRankCombo);
	currY += (actorUI->rankCombo->widget.height + GE_SPACE);

	// Faction override for the actor
	widgetLabel = ui_LabelCreate("Faction", 0, currY);
	actorUI->factionCombo = ui_ComboBoxCreateWithGlobalDictionary(GE_ACTLABEL_WIDTH, currY, 150, g_hCritterFactionDict, "resourceName");
//	ui_ComboBoxSetDefaultDisplayString(actorUI->factionCombo, "No Faction", true);
	ui_ComboBoxSetDefaultDisplayString(actorUI->factionCombo, "No Faction");
	ui_ComboBoxSetSelectedCallback(actorUI->factionCombo, selFactionFunc, geDoc);
	ui_ExpanderAddChild(actorUI->actorPropExpander, widgetLabel);
	ui_ExpanderAddChild(actorUI->actorPropExpander, actorUI->factionCombo);
	currY += (actorUI->factionCombo->widget.height + GE_SPACE);

	// FSMOverride entry
	GETextEntryCreateEx(&actorUI->fsmEntry, NULL, "FSM", "", NULL, gFSMDict, NULL, parse_FSM, "Name:", 0, currY, 200, 0, GE_ACTLABEL_WIDTH, GE_VALIDFUNC_NONE, selFSMFunc, geDoc, actorUI->actorPropExpander);
	actorUI->fsmButton = ui_ButtonCreate("No FSM", actorUI->fsmEntry->widget.x + actorUI->fsmEntry->widget.width + GE_SPACE, currY, openFSMFunc, geDoc);
	ui_ExpanderAddChild(actorUI->actorPropExpander, actorUI->fsmButton);
	currY = actorUI->fsmButton->widget.y + actorUI->fsmButton->widget.height + GE_SPACE;

	// Spawn Anim
	if(spawnAnimChangedFunc)
		currY = GETextEntryCreate(&actorUI->spawnAnim, "Spawn Anim", "", 0, currY, 200, 0, GE_ACTLABEL_WIDTH, GE_VALIDFUNC_NONE, spawnAnimChangedFunc, geDoc, actorUI->actorPropExpander) + GE_SPACE;

	// Display Name entry
	currY = GECreateMessageEntry(&actorUI->dispNameEntry, "DisplayName", NULL, 0, currY, 200, GE_ACTLABEL_WIDTH, dispNameChangedFunc, geDoc, actorUI->actorPropExpander);

	// ContactDef that this critter can use when iteracted with
	widgetLabel = ui_LabelCreate("Contact", 0, currY);
	actorUI->contactEntry = (UIEditable*)ui_TextEntryCreateWithGlobalDictionaryCombo("", GE_ACTLABEL_WIDTH, currY, "Contact", "resourceName", true, true, false, true);
	ui_WidgetSetWidth(UI_WIDGET(actorUI->contactEntry), 150);
	ui_TextEntrySetFinishedCallback((UITextEntry*)actorUI->contactEntry, selContactFunc, geDoc);
	ui_ExpanderAddChild(actorUI->actorPropExpander, widgetLabel);
	ui_ExpanderAddChild(actorUI->actorPropExpander, actorUI->contactEntry);
	currY += (actorUI->contactEntry->widget.height + GE_SPACE);

	// Spawn When entry
	currY = GEExpressionEditButtonCreate(&actorUI->spawnWhenExpr, NULL, "Spawn When", NULL, 0, currY, 200, GE_ACTLABEL_WIDTH, spawnWhenChangedFunc, geDoc, actorUI->actorPropExpander) + GE_SPACE;

	// Interaction condition override
	currY = GEExpressionEditButtonCreate(&actorUI->interactCondExpr, NULL, "Interact Condition", NULL, 0, currY, 200, GE_ACTLABEL_WIDTH, interactCondChangedFunc, geDoc, actorUI->actorPropExpander) + GE_SPACE;

	// TeamSize Checkboxes
	widgetLabel = ui_LabelCreate("Spawn", 0, currY);
	for (i = 0; i < MAX_TEAM_SIZE; i++)
	{
		char countStr[64];
		int teamSize = i + 1;
		sprintf(countStr, "%i", teamSize);
		eaPush(&actorUI->spawnEnabledButtons, ui_CheckButtonCreate(GE_ACTLABEL_WIDTH + i * 35, currY, countStr, false));
		ui_CheckButtonSetToggledCallback(actorUI->spawnEnabledButtons[i], seChangedFunc, S32_TO_PTR(teamSize));
		ui_ExpanderAddChild(actorUI->actorPropExpander, actorUI->spawnEnabledButtons[i]);
	}
	ui_ExpanderAddChild(actorUI->actorPropExpander, widgetLabel);
	currY += (widgetLabel->widget.height + GE_SPACE);

	// Boss Bar Checkboxes
	widgetLabel = ui_LabelCreate("Boss Bar", 0, currY);
	for (i = 0; i < MAX_TEAM_SIZE; i++)
	{
		char countStr[64];
		int teamSize = i + 1;
		sprintf(countStr, "%i", teamSize);
		eaPush(&actorUI->bossBarButtons, ui_CheckButtonCreate(GE_ACTLABEL_WIDTH + i * 35, currY, countStr, false));
		ui_CheckButtonSetToggledCallback(actorUI->bossBarButtons[i], bossBarChangedFunc, S32_TO_PTR(teamSize));
		ui_ExpanderAddChild(actorUI->actorPropExpander, actorUI->bossBarButtons[i]);
	}
	ui_ExpanderAddChild(actorUI->actorPropExpander, widgetLabel);
	currY += (widgetLabel->widget.height + GE_SPACE);

	// Create the expander group for all expanders created below
	actorUI->expanderGroup = ui_ExpanderGroupCreate();
	ui_WidgetSetPosition((UIWidget*)actorUI->expanderGroup, 0, currY);
	ui_WidgetSetDimensionsEx((UIWidget*)actorUI->expanderGroup, 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_ExpanderAddChild(actorUI->actorPropExpander, actorUI->expanderGroup);

	// Actor FSM Vars
	actorUI->varExpander = ui_ExpanderCreate("FSM Vars", GE_SPACE);
	ui_ExpanderGroupAddExpander(actorUI->expanderGroup, actorUI->varExpander);
	actorUI->varChanged = varChangedFunc;
	actorUI->varMessageChanged = varMessageChangedFunc;
	currY += actorUI->varExpander->widget.height + GE_SPACE;

	// Set the reflow callback to reflow the bigger expander upon smaller reflow
	ui_ExpanderGroupSetReflowCallback(actorUI->expanderGroup, GEActorPropUIReflowGroup, actorUI);

	// Set the window height
	actorUI->actorPropExpander->openedHeight = currY;
	ui_ExpanderSetOpened(actorUI->actorPropExpander, true);

	return actorUI;
}

void GEActorPropUIRefresh(ActorPropUI* actorUI, OldActor*** pppActors, bool fullRefresh)
{
	static char* windowTitle = NULL;
	FSMExternVar** commonExternVars = NULL;
	OldEncounterVariable** commonEncounterVars = NULL;
	bool allowVarEdit[MAX_FSM_VARS];	// Flag for each var; if false, don't allow this var to be edited
	const char* whichName = NULL;
	const char* whichFSMOverride = NULL;
	Message* whichDisplayName = NULL;
	Expression* whichSpawnCond = NULL;
	Expression* whichInteractCond = NULL;
	const char* pchCritterGroup = NULL;
	const char* whichContact = NULL;
	const char* whichCritter = NULL;
	ActorScalingFlag whichSpawnFlags = 0;
	ActorScalingFlag whichBossFlags = 0;
	const char *pcWhichRank = NULL;
	const char *pcWhichSubRank = NULL;
	CritterFaction* whichFaction = NULL;
	const char * whichSpawnAnim = NULL;
	int i, j, selectedCount = eaSize(pppActors);
	bool isOverridden = false;	// There's information for this actor in the override def
	bool infoInstanced = false;	// The actor info for this particular scaling isn't inherited
	bool aiInfoInstanced = false; // The actor AI info for this particular scaling isn't inherited
	bool hasCritterDef = false; // At least one selected actor has a specific CritterDef or is a Nemesis (instead of random)
	bool hasNemesisMinion = false; // At least one selected actor is a NemesisMinion
	FSM* commonFSM = GEFindCommonActorFSM(pppActors);
	Actor1CritterType eCritterType = -1;

	// If all actors have the same FSM (through override, by default or by critter def),
	// We need to create a list of the variable names for use in the comparision section
	if (commonFSM)
	{
		fsmGetExternVarNamesRecursive(commonFSM, &commonExternVars, "Encounter");
		devassert(eaSize(&commonExternVars) < MAX_FSM_VARS);

		// Mark all vars as editable
		for(i=0; i<eaSize(&commonExternVars); i++)
			allowVarEdit[i] = true;
	}

	// Do a diff on all the fields we care about
	if (selectedCount)
	{
		bool sameCritter = true;
		bool sameFSMOverride = true;
		bool sameSpawnFlags = true;
		bool sameBossFlags = true;
		bool sameFaction = true;
		bool sameRank = true;
		bool sameSubRank = true;
		bool sameContact = true;
		bool sameDispName = true;
		bool sameSpawnCond = true;
		bool sameInteractCond = true;
		bool sameSpawnAnim = true;
		OldActor* firstActor = eaGet(pppActors,0);
		OldActorInfo* firstActorInfo = oldencounter_GetActorInfo(firstActor);
		OldActorAIInfo* firstActorAIInfo = oldencounter_GetActorAIInfo(firstActor);
		int varIdx, numVars = eaSize(&commonExternVars);
		
		if (firstActorInfo){
			eCritterType = firstActorInfo->eCritterType;
			pchCritterGroup = REF_STRING_FROM_HANDLE(firstActorInfo->critterGroup);
		}
		
		if (firstActor->details.info)
			infoInstanced = true;
		if (firstActor->details.aiInfo)
			aiInfoInstanced = true;
		isOverridden = firstActor->overridden;
		estrPrintf(&windowTitle, "Actor Properties (");

		for (varIdx = 0; varIdx < numVars; varIdx++)
		{
			OldEncounterVariable* actorVar = oldencounter_LookupActorVariable(firstActorAIInfo, commonExternVars[varIdx]->name);
			eaPush(&commonEncounterVars, actorVar);
		}

		for (i = 0; i < selectedCount; i++)
		{
			OldActor *currActor = eaGet(pppActors,i);
			OldActorInfo* currActorInfo = oldencounter_GetActorInfo(currActor);
			OldActorAIInfo* currActorAIInfo = oldencounter_GetActorAIInfo(currActor);
			isOverridden |= currActor->overridden;
			if(currActor->details.info)
				infoInstanced = true;
			if(currActor->details.aiInfo)
				aiInfoInstanced = true;
			if(!hasCritterDef && (IS_HANDLE_ACTIVE(currActorInfo->critterDef) || currActorInfo->eCritterType == Actor1CritterType_Nemesis))
				hasCritterDef = true;
			if (!hasNemesisMinion && currActorInfo->eCritterType == Actor1CritterType_NemesisMinion)
				hasNemesisMinion = true;
			CHECK_ACTORINFO_FIELD(sameCritter, s_ActorCritterIndex);
			CHECK_ACTORINFO_FIELD(sameFaction, s_ActorFactionIndex);
			CHECK_ACTORINFO_FIELD(sameRank, s_ActorRankIndex);
			CHECK_ACTORINFO_FIELD(sameSubRank, s_ActorSubRankIndex);
			CHECK_ACTORINFO_FIELD(sameContact, s_ActorContactIndex);
			CHECK_ACTORINFO_FIELD(sameSpawnCond, s_ActorSpawnCondIndex);
			if(sameFSMOverride && (!REF_STRING_FROM_HANDLE(firstActorAIInfo->hFSM) || !REF_STRING_FROM_HANDLE(currActorAIInfo->hFSM)
					|| 0 != stricmp(REF_STRING_FROM_HANDLE(firstActorAIInfo->hFSM), REF_STRING_FROM_HANDLE(currActorAIInfo->hFSM))))
				sameFSMOverride = false;
			if (pchCritterGroup && (!REF_STRING_FROM_HANDLE(currActorInfo->critterGroup) || stricmp(pchCritterGroup, REF_STRING_FROM_HANDLE(currActorInfo->critterGroup))!=0))
				pchCritterGroup = NULL;
			if (sameSpawnFlags && (firstActor->disableSpawn != currActor->disableSpawn))
				sameSpawnFlags = false;
			if (sameBossFlags && (firstActor->useBossBar != currActor->useBossBar))
				sameBossFlags = false;
			if (sameSpawnAnim && (!firstActorInfo->pchSpawnAnim || !currActorInfo->pchSpawnAnim
					|| stricmp(firstActorInfo->pchSpawnAnim, currActorInfo->pchSpawnAnim)))
					sameSpawnAnim = false;
			if (sameDispName && !GECompareMessages(&firstActor->displayNameMsg, &currActor->displayNameMsg))
				sameDispName = false;
			if (currActorInfo->eCritterType != eCritterType){
				eCritterType = -1;
			}
			if (commonFSM)
			{
				for (varIdx = 0; varIdx < numVars; varIdx++)
				{
					if (allowVarEdit[varIdx])
					{
						OldEncounterVariable* actorVar = oldencounter_LookupActorVariable(currActorAIInfo, commonExternVars[varIdx]->name);
						if (!GEVarIsEqual(actorVar, commonEncounterVars[varIdx], commonExternVars[varIdx]))
						{
							commonEncounterVars[varIdx] = NULL;
							allowVarEdit[varIdx] = false;
						}
					}
				}
			}
			estrConcatf(&windowTitle, "%i%s", currActor->uniqueID, (i != (selectedCount - 1)) ? ", " : ")");
		}
		if (sameCritter) whichCritter = REF_STRING_FROM_HANDLE(firstActorInfo->critterDef);
		if (sameFSMOverride) whichFSMOverride = REF_STRING_FROM_HANDLE(firstActorAIInfo->hFSM);
		if (sameSpawnFlags) whichSpawnFlags = firstActor->disableSpawn;
		if (sameBossFlags) whichBossFlags = firstActor->useBossBar;
		if (sameFaction) whichFaction = GET_REF(firstActorInfo->critterFaction);
		if (sameSpawnAnim) whichSpawnAnim = firstActorInfo->pchSpawnAnim;
		if (sameRank) pcWhichRank = firstActorInfo->pcCritterRank;
		if (sameSubRank) pcWhichSubRank = firstActorInfo->pcCritterSubRank;
		if (sameContact) whichContact = REF_STRING_FROM_HANDLE(firstActorInfo->contactScript);
		if (sameDispName) whichDisplayName = (firstActor->displayNameMsg.pEditorCopy?firstActor->displayNameMsg.pEditorCopy:GET_REF(firstActor->displayNameMsg.hMessage));
		if (sameSpawnCond) whichSpawnCond = firstActorInfo->spawnCond;
		if (sameInteractCond) whichInteractCond = firstActorInfo->oldActorInteractProps.interactCond;
		if (selectedCount == 1) whichName = firstActor->name;
	}

	// Show the IDs of the actor's selected in the window title
	ui_WidgetSetTextString((UIWidget*)actorUI->actorPropExpander, selectedCount ? windowTitle : "Actor Properties");

	// Set or clear the "Instanced" and "Overridden" labels
	if(isOverridden && actorUI->overriddenLabel == NULL)
	{
		actorUI->overriddenLabel = ui_LabelCreate("Overridden", actorUI->nameEntry->widget.x + actorUI->nameEntry->widget.width + GE_SPACE, actorUI->nameEntry->widget.y);
		ui_ExpanderAddChild(actorUI->actorPropExpander, actorUI->overriddenLabel);
	}
	else if(!isOverridden && actorUI->overriddenLabel)
	{
		ui_ExpanderRemoveChild(actorUI->actorPropExpander, actorUI->overriddenLabel);
		ui_LabelFreeInternal(actorUI->overriddenLabel);
		actorUI->overriddenLabel = NULL;
	}
	if(infoInstanced && actorUI->infoInstancedLabel == NULL)
	{
		actorUI->infoInstancedLabel = ui_LabelCreate("Instanced", actorUI->critterCombo->widget.x + actorUI->critterCombo->widget.width + GE_SPACE, actorUI->critterCombo->widget.y);
		ui_ExpanderAddChild(actorUI->actorPropExpander, actorUI->infoInstancedLabel);
	}
	else if(!infoInstanced && actorUI->infoInstancedLabel)
	{
		ui_ExpanderRemoveChild(actorUI->actorPropExpander, actorUI->infoInstancedLabel);
		ui_LabelFreeInternal(actorUI->infoInstancedLabel);
		actorUI->infoInstancedLabel = NULL;
	}
	if(aiInfoInstanced && actorUI->aiInstancedLabel == NULL)
	{
		actorUI->aiInstancedLabel= ui_LabelCreate("AI Instanced", actorUI->fsmButton->widget.x + actorUI->fsmButton->widget.width + GE_SPACE, actorUI->fsmEntry->widget.y);
		ui_ExpanderAddChild(actorUI->actorPropExpander, actorUI->aiInstancedLabel);
	}
	else if(!aiInfoInstanced && actorUI->aiInstancedLabel)
	{
		ui_ExpanderRemoveChild(actorUI->actorPropExpander, actorUI->aiInstancedLabel);
		ui_LabelFreeInternal(actorUI->aiInstancedLabel);
		actorUI->aiInstancedLabel = NULL;
	}
	ui_ButtonSetText(actorUI->fsmButton, whichFSMOverride ? whichFSMOverride : (commonFSM ? commonFSM->name : " "));
	if(whichFSMOverride || commonFSM)
		ui_SetActive(&actorUI->fsmButton->widget, true);
	else
		ui_SetActive(&actorUI->fsmButton->widget, false);

	ui_MessageEntrySetMessage(actorUI->dispNameEntry, whichDisplayName ? whichDisplayName : NULL);

	ui_ComboBoxSetSelectedEnum(actorUI->critterTypeCombo, eCritterType);

	if (eCritterType == Actor1CritterType_Nemesis){
		ui_LabelSetText(actorUI->critterLabel, "Default");
	} else if (eCritterType == Actor1CritterType_NemesisMinion){
		ui_LabelSetText(actorUI->critterLabel, "");
	} else {
		ui_LabelSetText(actorUI->critterLabel, "Critter");
	}

	if (eCritterType == Actor1CritterType_NemesisMinion){
		ui_LabelSetText(actorUI->critterGroupLabel, "Default");
	} else if (eCritterType == Actor1CritterType_Nemesis){
		ui_LabelSetText(actorUI->critterGroupLabel, "");
	} else {
		ui_LabelSetText(actorUI->critterGroupLabel, "CritterGroup");
	}

	// Only update the other fields if fullRefresh is specified (if the user isn't potentially editing one of the text fields)
	if(fullRefresh)
	{
		ResourceInfo* tmpResInfo;
		ui_EditableSetText(actorUI->nameEntry, whichName ? whichName : "");
		ui_TextEntrySetText(actorUI->critterCombo, (whichCritter?whichCritter:(hasCritterDef || hasNemesisMinion)?"":"Random Critter"));
		ui_EditableSetText(actorUI->fsmEntry, whichFSMOverride ? whichFSMOverride : "");
		tmpResInfo = whichFaction ? resGetInfo(g_hCritterFactionDict, whichFaction->pchName) : NULL;
		ui_ComboBoxSetSelectedObject(actorUI->factionCombo, tmpResInfo);
		if(actorUI->spawnAnim)
			ui_EditableSetText(actorUI->spawnAnim, whichSpawnAnim ? whichSpawnAnim : "");
		ui_ComboBoxSetSelectedsAsString(actorUI->rankCombo, pcWhichRank);
		ui_ComboBoxSetSelectedsAsString(actorUI->subRankCombo, pcWhichSubRank);
		ui_TextEntrySetText(actorUI->groupCombo, pchCritterGroup?pchCritterGroup:"");
		ui_EditableSetText(actorUI->contactEntry, whichContact?whichContact:"");
		ui_ButtonSetText(actorUI->spawnWhenExpr, whichSpawnCond ? exprGetCompleteString(whichSpawnCond): "");
		ui_ButtonSetText(actorUI->interactCondExpr, whichInteractCond ? exprGetCompleteString(whichInteractCond): "");

		if (hasCritterDef)
		{
			ui_SetActive((UIWidget*)actorUI->groupCombo, false);
			ui_SetActive((UIWidget*)actorUI->rankCombo, false);
		}
		else
		{
			ui_SetActive((UIWidget*)actorUI->groupCombo, true);
			ui_SetActive((UIWidget*)actorUI->rankCombo, true);
		}

		if (hasNemesisMinion){
			ui_SetActive((UIWidget*)actorUI->critterCombo, false);
		} else {
			ui_SetActive((UIWidget*)actorUI->critterCombo, true);
		}

		for (j = 0; j < MAX_TEAM_SIZE; j++)
			actorUI->spawnEnabledButtons[j]->state = !(whichSpawnFlags & (1 << (j + 1)));
		for (j = 0; j < MAX_TEAM_SIZE; j++)
			actorUI->bossBarButtons[j]->state = (whichBossFlags & (1 << (j + 1)));

		if (commonFSM)
		{
			// Set the expander active first so that individual widgets can set themselves inactive if they want
			ui_SetActive((UIWidget*)actorUI->varExpander, true);
			GEActorVarUIPopulate(actorUI, &commonExternVars, &commonEncounterVars, allowVarEdit);
		}
		else
		{
			if (ui_ExpanderIsOpened(actorUI->varExpander))
				ui_ExpanderToggle(actorUI->varExpander);
			ui_SetActive((UIWidget*)actorUI->varExpander, false);
		}

		ui_ExpanderGroupReflow(actorUI->expanderGroup);
	}
	eaDestroy(&commonExternVars);
	eaDestroy(&commonEncounterVars);
}


// ----------------------------------------------------------------------------------
// Display Def Setup
// ----------------------------------------------------------------------------------

GEDisplayDefData g_GEDisplayDefs = {0};

void GELoadDisplayDefs(void* unused, bool bUnused)
{
	g_GEDisplayDefs.actDispDef = objectLibraryGetGroupDefByName("core_icons_humanoid", true);
	g_GEDisplayDefs.encDispDef = objectLibraryGetGroupDefByName("core_icons_objective", true);
	g_GEDisplayDefs.spawnLocDispDef = objectLibraryGetGroupDefByName("core_icons_patrol", true);
	assertmsg(g_GEDisplayDefs.actDispDef && g_GEDisplayDefs.encDispDef && g_GEDisplayDefs.spawnLocDispDef, "If this failed, our actor/enc drawing groupdefs were changed or removed");
}


// ----------------------------------------------------------------------------------
// GameAction UI
// ----------------------------------------------------------------------------------

// Returns TRUE if the GEActionGroup should be considered editable
static bool GEActionGroupIsEditable(GEActionGroup *pGroup)
{
	if (pGroup && pGroup->isEditableFunc)
		return pGroup->isEditableFunc(pGroup->pUserData);
	else
		return true;
}

static UILabel *GERefreshLabel(UILabel *pLabel, const char *pcText, const char* pcTooltip, F32 x, F32 xPercent, F32 y, UIWidget *pWidgetParent)
{
	if (!pLabel) {
		pLabel = ui_LabelCreate(pcText, x, y);
		ui_WidgetAddChild(pWidgetParent, UI_WIDGET(pLabel));
	}
	ui_LabelSetText(pLabel, pcText);
	ui_WidgetSetPositionEx(UI_WIDGET(pLabel), x, y, xPercent, 0, UITopLeft);
	ui_WidgetSetTooltipString(UI_WIDGET(pLabel), pcTooltip);
	ui_LabelEnableTooltips(pLabel);
	return pLabel;
}

// This is called by MEField prior to allowing an edit
static bool GEGameActionFieldPreChangeCB(MEField *pField, bool bFinished, GEActionGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	return GEActionGroupIsEditable(pGroup);
}


// This is called when an MEField is changed
static void GEGameActionFieldChangedCB(MEField *pField, bool bFinished, GEActionGroup *pGroup)
{
	pGroup->dataChangedCB(pGroup->pUserData, bFinished);
}


static void GEGameActionAddFieldToParent(MEField *pField, UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, GEActionGroup *pGroup)
{
	MEFieldAddToParent(pField, pParent, x, y);
	ui_WidgetSetPositionEx(pField->pUIWidget, x, y, xPercent, 0, UITopLeft);
	ui_WidgetSetWidthEx(pField->pUIWidget, w, wUnit);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, padRight, 0, 0);
	MEFieldSetChangeCallback(pField, GEGameActionFieldChangedCB, pGroup);
	MEFieldSetPreChangeCallback(pField, GEGameActionFieldPreChangeCB, pGroup);
}

void GEAddFieldToParent(MEField *pField, UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, MEFieldChangeCallback changeFunc, MEFieldPreChangeCallback preChangeFunc, void *pData)
{
	MEFieldAddToParent(pField, pParent, x, y);
	ui_WidgetSetPositionEx(pField->pUIWidget, x, y, xPercent, 0, UITopLeft);
	ui_WidgetSetWidthEx(pField->pUIWidget, w, wUnit);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, padRight, 0, 0);
	MEFieldSetChangeCallback(pField, changeFunc, pData);
	MEFieldSetPreChangeCallback(pField, preChangeFunc, pData);
}

static void GERemoveActionCB(UIButton *pButton, GEActionGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (!GEActionGroupIsEditable(pGroup)) {
		return;
	}

	// Remove the action
	StructDestroy(parse_WorldGameActionProperties, (*pGroup->peaActions)[pGroup->index]);
	eaRemove(pGroup->peaActions, pGroup->index);

	// Update the UI
	pGroup->dataChangedCB(pGroup->pUserData, true);
}

static void GEColorComboCB(UIComboBox *pCombo, int eChoice, GEActionGroup *pGroup)
{
	WorldGameActionProperties *pAction;
	Vec4 vColor;

	// Make sure the resource is checked out of Gimme
	if (!GEActionGroupIsEditable(pGroup)) {
		// Need to refresh the combo
		pGroup->updateDisplayCB(pGroup->pUserData);
		return;
	}

	pAction = (*pGroup->peaActions)[pGroup->index];
	assert(pAction->pSendFloaterProperties);
	ui_ColorButtonGetColor(pGroup->pColorButton, vColor);
	switch(eChoice) {
		xcase FloaterActionColors_Custom:
			// No change
		xcase FloaterActionColors_Failed:
			if (!sameVec3(pAction->pSendFloaterProperties->vColor, gvFailedColor)) {
				copyVec3(gvFailedColor, pAction->pSendFloaterProperties->vColor);
				pGroup->dataChangedCB(pGroup->pUserData, true);
			}
		xcase FloaterActionColors_Gained:
			if (!sameVec3(pAction->pSendFloaterProperties->vColor, gvGainedColor)) {
				copyVec3(gvGainedColor, pAction->pSendFloaterProperties->vColor);
				pGroup->dataChangedCB(pGroup->pUserData, true);
			}
		xcase FloaterActionColors_Progress:
			if (!sameVec3(pAction->pSendFloaterProperties->vColor, gvProgressColor)) {
				copyVec3(gvProgressColor, pAction->pSendFloaterProperties->vColor);
				pGroup->dataChangedCB(pGroup->pUserData, true);
			}
	}
}


static void GEColorButtonCB(UIColorButton *pButton, bool bFinished, GEActionGroup *pGroup)
{
	WorldGameActionProperties *pAction;
	Vec4 vColor;

	// Make sure the resource is checked out of Gimme
	if (!GEActionGroupIsEditable(pGroup)) {
		// Need to refresh the color button
		pGroup->updateDisplayCB(pGroup->pUserData);
		return;
	}

	// Apply color to data
	pAction = (*pGroup->peaActions)[pGroup->index];
	assert(pAction->pSendFloaterProperties);
	ui_ColorButtonGetColor(pGroup->pColorButton, vColor);
	copyVec3(vColor, pAction->pSendFloaterProperties->vColor);

	pGroup->dataChangedCB(pGroup->pUserData, true);
}

static void GECleanUpActionProperties(WorldGameActionProperties *pAction)
{
	// Destroy unneccessary sets of properties
	if (pAction->eActionType != WorldGameActionType_GrantMission)
		StructDestroySafe(parse_WorldGrantMissionActionProperties, &pAction->pGrantMissionProperties);
	if (pAction->eActionType != WorldGameActionType_GrantSubMission)
		StructDestroySafe(parse_WorldGrantSubMissionActionProperties, &pAction->pGrantSubMissionProperties);
	if (pAction->eActionType != WorldGameActionType_MissionOffer)
		StructDestroySafe(parse_WorldMissionOfferActionProperties, &pAction->pMissionOfferProperties);
	if (pAction->eActionType != WorldGameActionType_DropMission)
		StructDestroySafe(parse_WorldDropMissionActionProperties, &pAction->pDropMissionProperties);
	if (pAction->eActionType != WorldGameActionType_GiveItem)
		StructDestroySafe(parse_WorldGiveItemActionProperties, &pAction->pGiveItemProperties);
	if (pAction->eActionType != WorldGameActionType_TakeItem)
		StructDestroySafe(parse_WorldTakeItemActionProperties, &pAction->pTakeItemProperties);
	if (pAction->eActionType != WorldGameActionType_SendFloaterMsg)
		StructDestroySafe(parse_WorldSendFloaterActionProperties, &pAction->pSendFloaterProperties);
	if (pAction->eActionType != WorldGameActionType_SendNotification)
		StructDestroySafe(parse_WorldSendNotificationActionProperties, &pAction->pSendNotificationProperties);
	if (pAction->eActionType != WorldGameActionType_Warp)
		StructDestroySafe(parse_WorldWarpActionProperties, &pAction->pWarpProperties);
	if (pAction->eActionType != WorldGameActionType_Contact)
		StructDestroySafe(parse_WorldContactActionProperties, &pAction->pContactProperties);
	if (pAction->eActionType != WorldGameActionType_Expression)
		StructDestroySafe(parse_WorldExpressionActionProperties, &pAction->pExpressionProperties);
	if (pAction->eActionType != WorldGameActionType_ChangeNemesisState)
		StructDestroySafe(parse_WorldChangeNemesisStateActionProperties, &pAction->pNemesisStateProperties);
	if (pAction->eActionType != WorldGameActionType_NPCSendMail)
		StructDestroySafe(parse_WorldNPCSendEmailActionProperties, &pAction->pNPCSendEmailProperties);
	if (pAction->eActionType != WorldGameActionType_GADAttribValue)
		StructDestroySafe(parse_WorldGADAttribValueActionProperties, &pAction->pGADAttribValueProperties);
	if (pAction->eActionType != WorldGameActionType_ShardVariable)
		StructDestroySafe(parse_WorldShardVariableActionProperties, &pAction->pShardVariableProperties);
	if (pAction->eActionType != WorldGameActionType_ActivityLog)
		StructDestroySafe(parse_WorldActivityLogActionProperties, &pAction->pActivityLogProperties);
	if (pAction->eActionType != WorldGameActionType_GiveDoorKeyItem)
		StructDestroySafe(parse_WorldGiveDoorKeyItemActionProperties, &pAction->pGiveDoorKeyItemProperties);
	if (pAction->eActionType != WorldGameActionType_GuildStatUpdate)
		StructDestroySafe(parse_WorldGuildStatUpdateActionProperties, &pAction->pGuildStatUpdateProperties);
	if (pAction->eActionType != WorldGameActionType_GuildThemeSet)
		StructDestroySafe(parse_WorldGuildThemeSetActionProperties, &pAction->pGuildThemeSetProperties);
	if (pAction->eActionType != WorldGameActionType_UpdateItemAssignment)
		StructDestroySafe(parse_WorldItemAssignmentActionProperties, &pAction->pItemAssignmentProperties);
}


void GEFreeVariableGroup(GEVariableGroup *pGroup)
{
	ui_WidgetQueueFreeAndNull(&pGroup->pTitleLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pNameLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pTypeLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pTypeValueLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pValueLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pRemoveButton);

	MEFieldSafeDestroy(&pGroup->pNameField);
	MEFieldSafeDestroy(&pGroup->pValueField);
	free(pGroup);
}

void GEFreeVariableDefGroup(GEVariableDefGroup *pGroup)
{
	if (!pGroup)
		return;
	
	ui_WidgetQueueFreeAndNull(&pGroup->pTitleLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pNameLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pInitFromLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pTypeLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pTypeValueLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pValueLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pValueLabel2);
	ui_WidgetQueueFreeAndNull(&pGroup->pChoiceTableLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pChoiceNameLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pMapVariableLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pMissionLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pMissionVariableLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pExpressionLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pActivityNameLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pActivityVariableNameLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pActivityDefaultValueLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pActivityDefaultValueLabel2);
	ui_WidgetQueueFreeAndNull(&pGroup->pRemoveButton);

	MEFieldSafeDestroy(&pGroup->pNameField);
	MEFieldSafeDestroy(&pGroup->pTypeField);
	MEFieldSafeDestroy(&pGroup->pInitFromField);
	MEFieldSafeDestroy(&pGroup->pValueField);
	MEFieldSafeDestroy(&pGroup->pValueField2);
	MEFieldSafeDestroy(&pGroup->pChoiceTableField);
	MEFieldSafeDestroy(&pGroup->pChoiceNameField);
	MEFieldSafeDestroy(&pGroup->pMapVariableField);
	MEFieldSafeDestroy(&pGroup->pMissionField);
	MEFieldSafeDestroy(&pGroup->pMissionVariableField);
	MEFieldSafeDestroy(&pGroup->pExpressionField);
	MEFieldSafeDestroy(&pGroup->pActivityNameField);
	MEFieldSafeDestroy(&pGroup->pActivityVariableNameField);
	MEFieldSafeDestroy(&pGroup->pActivityDefaultValueField);
	MEFieldSafeDestroy(&pGroup->pActivityDefaultValueField2);

	eaDestroyEx(&pGroup->eaChoiceTableNames, NULL);
	eaDestroyEx(&pGroup->eaSrcMapVariables, NULL);
	
	free(pGroup);
}

void GEFreeVariableDefGroupSafe(GEVariableDefGroup **pGroup)
{
	if (*pGroup)
		GEFreeVariableDefGroup(*pGroup);
	*pGroup = NULL;
}

F32 GEUpdateMissionLevelDefGroup(GEMissionLevelDefGroup *pGroup, UIExpander *pParent, MissionLevelDef *levelDef, MissionLevelDef *origLevelDef,
								 F32 xOffsetBase, F32 xOffsetControl, F32 y,
								 MEFieldChangeCallback changeFunc, MEFieldPreChangeCallback preChangeFunc, void *pData)
{
	// Update the level type
	pGroup->pLevelTypeLabel = GERefreshLabel(pGroup->pLevelTypeLabel, "Level From", "How the level of the mission should be determined.", xOffsetBase, 0, y, UI_WIDGET(pParent));
	MEExpanderRefreshEnumField( &pGroup->pLevelTypeField, origLevelDef, levelDef, parse_MissionLevelDef, "LevelType", MissionLevelTypeEnum,
								UI_WIDGET(pParent), xOffsetControl, y, 0, 200, UIUnitFixed, 5,
								changeFunc, preChangeFunc, pData );
	y += Y_OFFSET_ROW;

	if(levelDef->eLevelType == MissionLevelType_Specified) {
		pGroup->pLevelLabel = GERefreshLabel(pGroup->pLevelLabel, "Level", "The specified level of the mission.", xOffsetBase + (X_OFFSET_INDENT - X_OFFSET_BASE), 0, y, UI_WIDGET(pParent));
		MEExpanderRefreshSimpleField( &pGroup->pLevelField, origLevelDef, levelDef, parse_MissionLevelDef, "Level", kMEFieldType_TextEntry,
									  UI_WIDGET(pParent), xOffsetControl, y, 0, 60, UIUnitFixed, 5,
									  changeFunc, preChangeFunc, pData );
		y += Y_OFFSET_ROW;
	} else {
		ui_WidgetQueueFreeAndNull( &pGroup->pLevelLabel );
		MEFieldSafeDestroy( &pGroup->pLevelField );
	}

	// Player level
	if( levelDef->eLevelType == MissionLevelType_PlayerLevel ) {
		if( !levelDef->pLevelClamp ) {
			levelDef->pLevelClamp = StructCreate( parse_MissionLevelClamp );
		}

		pGroup->pLevelClampTypeLabel = GERefreshLabel(pGroup->pLevelClampTypeLabel, "Clamp From", "Specifies how the encounter level will be clamped.", xOffsetBase + (X_OFFSET_INDENT - X_OFFSET_BASE), 0, y, UI_WIDGET(pParent));
		MEExpanderRefreshEnumField( &pGroup->pLevelClampTypeField, SAFE_MEMBER(origLevelDef, pLevelClamp), levelDef->pLevelClamp, parse_MissionLevelClamp, "ClampType", MissionLevelClampTypeEnum,
									UI_WIDGET(pParent), xOffsetControl, y, 0, 200, UIUnitFixed, 5,
									changeFunc, preChangeFunc, pData );
		y += Y_OFFSET_ROW;

		// Specified clamp
		if( levelDef->pLevelClamp->eClampType == MissionLevelClampType_Specified ) {
			pGroup->pLevelClampSpecifiedMinLabel = GERefreshLabel(pGroup->pLevelClampSpecifiedMinLabel, "Clamp: Min", "Specified the min and max level.  A value of zero in either position means no min or max (respectively).", xOffsetBase + (X_OFFSET_INDENT - X_OFFSET_BASE), 0, y, UI_WIDGET(pParent));
			MEExpanderRefreshSimpleField( &pGroup->pLevelClampSpecifiedMinField, SAFE_MEMBER(origLevelDef, pLevelClamp), levelDef->pLevelClamp, parse_MissionLevelClamp, "MinLevel", kMEFieldType_TextEntry,
										  UI_WIDGET(pParent), xOffsetControl, y, 0, 60, UIUnitFixed, 5,
										  changeFunc, preChangeFunc, pData );

			pGroup->pLevelClampSpecifiedMaxLabel = GERefreshLabel( pGroup->pLevelClampSpecifiedMaxLabel, "Max", "Specified the min and max level.  A value of zero in either position means no min or max (respectively).", xOffsetControl + 60, 0, y, UI_WIDGET(pParent));
			MEExpanderRefreshSimpleField( &pGroup->pLevelClampSpecifiedMaxField, SAFE_MEMBER(origLevelDef, pLevelClamp), levelDef->pLevelClamp, parse_MissionLevelClamp, "MaxLevel", kMEFieldType_TextEntry,
										  UI_WIDGET(pParent), xOffsetControl + 90, y, 0, 60, UIUnitFixed, 5,
										  changeFunc, preChangeFunc, pData );
			y += Y_OFFSET_ROW;
		} else {
			ui_WidgetQueueFreeAndNull( &pGroup->pLevelClampSpecifiedMinLabel );
			MEFieldSafeDestroy( &pGroup->pLevelClampSpecifiedMinField );
			ui_WidgetQueueFreeAndNull( &pGroup->pLevelClampSpecifiedMaxLabel );
			MEFieldSafeDestroy( &pGroup->pLevelClampSpecifiedMaxField );
		}

		// Map variable clamp
		if( levelDef->pLevelClamp->eClampType == MissionLevelClampType_MapVariable ) {
			pGroup->pLevelClampMapVarLabel = GERefreshLabel( pGroup->pLevelClampMapVarLabel, "Clamp Variable", "The map variable to be used to determine the encounter level clamping.", xOffsetBase + (X_OFFSET_INDENT - X_OFFSET_BASE), 0, y, UI_WIDGET(pParent));
			MEExpanderRefreshSimpleField( &pGroup->pLevelClampMapVarField, SAFE_MEMBER(origLevelDef, pLevelClamp), levelDef->pLevelClamp, parse_MissionLevelClamp, "ClampMapVariable", kMEFieldType_TextEntry,
										  UI_WIDGET(pParent), xOffsetControl, y, 0, 1.0, UIUnitPercentage, 5,
										  changeFunc, preChangeFunc, pData );
			y += Y_OFFSET_ROW;
		} else {
			ui_WidgetQueueFreeAndNull( &pGroup->pLevelClampMapVarLabel );
			MEFieldSafeDestroy( &pGroup->pLevelClampMapVarField );
		}

		// Offsets
		if( levelDef->pLevelClamp->eClampType != MissionLevelClampType_Specified ) {
			pGroup->pLevelClampOffsetMinLabel = GERefreshLabel(pGroup->pLevelClampOffsetMinLabel, "Clamp Off: Min", "Specify the min and max level offset for clamping.", xOffsetBase + (X_OFFSET_INDENT - X_OFFSET_BASE), 0, y, UI_WIDGET(pParent));
			MEExpanderRefreshSimpleField( &pGroup->pLevelClampOffsetMinField, SAFE_MEMBER(origLevelDef, pLevelClamp), levelDef->pLevelClamp, parse_MissionLevelClamp, "ClampOffsetMin", kMEFieldType_TextEntry,
										  UI_WIDGET(pParent), xOffsetControl, y, 0, 60, UIUnitFixed, 5,
										  changeFunc, preChangeFunc, pData );

			pGroup->pLevelClampOffsetMaxLabel = GERefreshLabel( pGroup->pLevelClampOffsetMaxLabel, "Max", "Specify the min and max level offset for clamping.", xOffsetControl + 60, 0, y, UI_WIDGET(pParent));
			MEExpanderRefreshSimpleField( &pGroup->pLevelClampOffsetMaxField, SAFE_MEMBER(origLevelDef, pLevelClamp), levelDef->pLevelClamp, parse_MissionLevelClamp, "ClampOffsetMax", kMEFieldType_TextEntry,
										  UI_WIDGET(pParent), xOffsetControl + 90, y, 0, 60, UIUnitFixed, 5,
										  changeFunc, preChangeFunc, pData );
			y += Y_OFFSET_ROW;
		} else {
			ui_WidgetQueueFreeAndNull( &pGroup->pLevelClampOffsetMinLabel );
			MEFieldSafeDestroy( &pGroup->pLevelClampOffsetMinField );
			ui_WidgetQueueFreeAndNull( &pGroup->pLevelClampOffsetMaxLabel );
			MEFieldSafeDestroy( &pGroup->pLevelClampOffsetMaxField );
		}
	} else {
		ui_WidgetQueueFreeAndNull( &pGroup->pLevelClampTypeLabel);
		MEFieldSafeDestroy( &pGroup->pLevelClampTypeField );
		ui_WidgetQueueFreeAndNull( &pGroup->pLevelClampSpecifiedMinLabel );
		MEFieldSafeDestroy( &pGroup->pLevelClampSpecifiedMinField );
		ui_WidgetQueueFreeAndNull( &pGroup->pLevelClampSpecifiedMaxLabel );
		MEFieldSafeDestroy( &pGroup->pLevelClampSpecifiedMaxField );
		ui_WidgetQueueFreeAndNull( &pGroup->pLevelClampMapVarLabel );
		MEFieldSafeDestroy( &pGroup->pLevelClampMapVarField );
		ui_WidgetQueueFreeAndNull( &pGroup->pLevelClampOffsetMinLabel );
		MEFieldSafeDestroy( &pGroup->pLevelClampOffsetMinField );
		ui_WidgetQueueFreeAndNull( &pGroup->pLevelClampOffsetMaxLabel );
		MEFieldSafeDestroy( &pGroup->pLevelClampOffsetMaxField );
	}

	// Map variable
	if( levelDef->eLevelType == MissionLevelType_MapVariable ) {
		pGroup->pLevelMapVarLabel = GERefreshLabel( pGroup->pLevelMapVarLabel, "Map Variable", "The name of the map variable from which the level of the mission should be determined.", xOffsetBase + (X_OFFSET_INDENT - X_OFFSET_BASE), 0, y, UI_WIDGET(pParent));
		MEExpanderRefreshSimpleField( &pGroup->pLevelMapVarField, origLevelDef, levelDef, parse_MissionLevelDef, "MapVariableForLevel", kMEFieldType_TextEntry,
									  UI_WIDGET(pParent), xOffsetControl, y, 0, 1.0, UIUnitPercentage, 5,
									  changeFunc, preChangeFunc, pData );
		y += Y_OFFSET_ROW;
	} else {
		ui_WidgetQueueFreeAndNull( &pGroup->pLevelMapVarLabel );
		MEFieldSafeDestroy( &pGroup->pLevelMapVarField );
	}

	return y;
}

void GEFreeMissionLevelDefGroupSafe(GEMissionLevelDefGroup **ppGroup)
{
	ui_WidgetQueueFreeAndNull( &(*ppGroup)->pLevelTypeLabel );
	MEFieldSafeDestroy( &(*ppGroup)->pLevelTypeField );
	ui_WidgetQueueFreeAndNull( &(*ppGroup)->pLevelLabel );
	MEFieldSafeDestroy( &(*ppGroup)->pLevelField );
	ui_WidgetQueueFreeAndNull( &(*ppGroup)->pLevelMapVarLabel );
	MEFieldSafeDestroy( &(*ppGroup)->pLevelMapVarField );
	ui_WidgetQueueFreeAndNull( &(*ppGroup)->pLevelTypeLabel );
	MEFieldSafeDestroy( &(*ppGroup)->pLevelTypeField );
	ui_WidgetQueueFreeAndNull( &(*ppGroup)->pLevelClampTypeLabel );
	MEFieldSafeDestroy( &(*ppGroup)->pLevelClampTypeField );
	ui_WidgetQueueFreeAndNull( &(*ppGroup)->pLevelClampMapVarLabel );
	MEFieldSafeDestroy( &(*ppGroup)->pLevelClampMapVarField );
	ui_WidgetQueueFreeAndNull( &(*ppGroup)->pLevelClampSpecifiedMinLabel );
	MEFieldSafeDestroy( &(*ppGroup)->pLevelClampSpecifiedMinField );
	ui_WidgetQueueFreeAndNull( &(*ppGroup)->pLevelClampSpecifiedMaxLabel );
	MEFieldSafeDestroy( &(*ppGroup)->pLevelClampSpecifiedMaxField );
	ui_WidgetQueueFreeAndNull( &(*ppGroup)->pLevelClampMapVarLabel );
	MEFieldSafeDestroy( &(*ppGroup)->pLevelClampMapVarField );
	ui_WidgetQueueFreeAndNull( &(*ppGroup)->pLevelClampOffsetMinLabel );
	MEFieldSafeDestroy( &(*ppGroup)->pLevelClampOffsetMinField );
	ui_WidgetQueueFreeAndNull( &(*ppGroup)->pLevelClampOffsetMaxLabel );
	MEFieldSafeDestroy( &(*ppGroup)->pLevelClampOffsetMaxField );

	free( *ppGroup );
	*ppGroup = NULL;
}

void GEMissionLevelDefPreSaveFixup(MissionLevelDef *levelDef)
{
	if( levelDef->eLevelType == MissionLevelType_PlayerLevel ) {
		if( levelDef->pLevelClamp ) {
			if( levelDef->pLevelClamp->eClampType != MissionLevelClampType_Specified ) {
				levelDef->pLevelClamp->iClampSpecifiedMin = 0;
				levelDef->pLevelClamp->iClampSpecifiedMax = 0;
			} else {
				levelDef->pLevelClamp->iClampOffsetMin = 0;
				levelDef->pLevelClamp->iClampOffsetMax = 0;
			}
			
			if( levelDef->pLevelClamp->eClampType != MissionLevelClampType_MapVariable ) {
				StructFreeStringSafe( &levelDef->pLevelClamp->pcClampMapVariable );
			}
		}
	} else {
		StructDestroySafe( parse_MissionLevelClamp, &levelDef->pLevelClamp );
	}
	if( levelDef->eLevelType != MissionLevelType_MapVariable ) {
		StructFreeStringSafe( &levelDef->pchLevelMapVar );
	}
	if( levelDef->eLevelType != MissionLevelType_Specified ) {
		levelDef->missionLevel = 1;
	}
}

void GEFreeActionGroup(GEActionGroup *pGroup)
{
	int i;

	for(i=eaSize(&pGroup->eaVariableDefGroups)-1; i>=0; --i) {
		GEFreeVariableDefGroup(pGroup->eaVariableDefGroups[i]);
	}
	eaDestroy(&pGroup->eaVariableDefGroups);

	ui_WidgetQueueFreeAndNull(&pGroup->pMainLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pLabel1);
	ui_WidgetQueueFreeAndNull(&pGroup->pLabel2);
	ui_WidgetQueueFreeAndNull(&pGroup->pLabel3);
	ui_WidgetQueueFreeAndNull(&pGroup->pLabel4);	
	ui_WidgetQueueFreeAndNull(&pGroup->pLabel5);
	ui_WidgetQueueFreeAndNull(&pGroup->pLabel6);
	ui_WidgetQueueFreeAndNull(&pGroup->pLabel7);
	ui_WidgetQueueFreeAndNull(&pGroup->pLabel8);
	ui_WidgetQueueFreeAndNull(&pGroup->pLabel9);
	ui_WidgetQueueFreeAndNull(&pGroup->pOutBox);
	ui_WidgetQueueFreeAndNull(&pGroup->pRemoveButton);
	ui_WidgetQueueFreeAndNull(&pGroup->pSeparator);
	ui_WidgetQueueFreeAndNull(&pGroup->pColorCombo);
	ui_WidgetQueueFreeAndNull(&pGroup->pColorButton);
	ui_WidgetQueueFreeAndNull(&pGroup->pVarAddButton);
	ui_WidgetQueueFreeAndNull(&pGroup->pDepartSequenceOverride);
	ui_WidgetQueueFreeAndNull(&pGroup->pSpecifyMapOverride);
	ui_WidgetQueueFreeAndNull(&pGroup->pIncludeTeammates);
	ui_WidgetQueueFreeAndNull(&pGroup->pSourceMapOverride);
	ui_WidgetQueueFreeAndNull(&pGroup->pDestMapOverride);

	MEFieldSafeDestroy(&pGroup->pTypeField);
	MEFieldSafeDestroy(&pGroup->pMissionField);
	MEFieldSafeDestroy(&pGroup->pMissionFromField);
	MEFieldSafeDestroy(&pGroup->pMissionVarField);
	MEFieldSafeDestroy(&pGroup->pMissionMapVarField);
	MEFieldSafeDestroy(&pGroup->pSubMissionField);
	MEFieldSafeDestroy(&pGroup->pDropMissionField);
	MEFieldSafeDestroy(&pGroup->pItemField);
	MEFieldSafeDestroy(&pGroup->pCountField);
	MEFieldSafeDestroy(&pGroup->pMessageField);
	MEFieldSafeDestroy(&pGroup->pContactField);
	MEFieldSafeDestroy(&pGroup->pNameField);
	MEFieldSafeDestroy(&pGroup->pTransSequenceField);
	MEFieldSafeDestroy(&pGroup->pExpressionField);
	MEFieldSafeDestroy(&pGroup->pBoolField);
	MEFieldSafeDestroy(&pGroup->pNemesisStateField);
	MEFieldSafeDestroy(&pGroup->pSubjectField);
	MEFieldSafeDestroy(&pGroup->pBodyField);
	MEFieldSafeDestroy(&pGroup->pNotifyTypeField);
	MEFieldSafeDestroy(&pGroup->pNotifySoundField);
	MEFieldSafeDestroy(&pGroup->pNotifyLogicalStringField);
	MEFieldSafeDestroy(&pGroup->pNotifyHeadshotTypeField);
	MEFieldSafeDestroy(&pGroup->pNotifyHeadshotStyleField);
	MEFieldSafeDestroy(&pGroup->pNotifyHeadshotField);
	MEFieldSafeDestroy(&pGroup->pNotifyHeadshotPetContactField);
	MEFieldSafeDestroy(&pGroup->pNotifyHeadshotCritterGroupTypeField);
	MEFieldSafeDestroy(&pGroup->pNotifyHeadshotCritterGroupField);
	MEFieldSafeDestroy(&pGroup->pNotifyHeadshotCritterGroupMapVarField);
	MEFieldSafeDestroy(&pGroup->pNotifyHeadshotCritterGroupIdentifierField);
	MEFieldSafeDestroy(&pGroup->pOfferHeadshotDisplayName);
	MEFieldSafeDestroy(&pGroup->pOfferHeadshotTypeField);
	MEFieldSafeDestroy(&pGroup->pOfferHeadshotStyleField);
	MEFieldSafeDestroy(&pGroup->pOfferHeadshotField);
	MEFieldSafeDestroy(&pGroup->pOfferHeadshotPetContactField);
	MEFieldSafeDestroy(&pGroup->pOfferHeadshotCritterGroupTypeField);
	MEFieldSafeDestroy(&pGroup->pOfferHeadshotCritterGroupField);
	MEFieldSafeDestroy(&pGroup->pOfferHeadshotCritterGroupMapVarField);
	MEFieldSafeDestroy(&pGroup->pOfferHeadshotCritterGroupIdentifierField);
	MEFieldSafeDestroy(&pGroup->pNotifySplatFXField);
	MEFieldSafeDestroy(&pGroup->pAttribKeyField);
	MEFieldSafeDestroy(&pGroup->pAttribValueField);
	MEFieldSafeDestroy(&pGroup->pAttribModifyTypeField);
	MEFieldSafeDestroy(&pGroup->pVarNameField);
	MEFieldSafeDestroy(&pGroup->pModifyTypeField);
	MEFieldSafeDestroy(&pGroup->pIntIncrementField);
	MEFieldSafeDestroy(&pGroup->pFloatIncrementField);
	MEFieldSafeDestroy(&pGroup->pActivityLogTypeField);
	MEFieldSafeDestroy(&pGroup->pDoorKeyField);
	MEFieldSafeDestroy(&pGroup->pGuildStatNameField);
	MEFieldSafeDestroy(&pGroup->pGuildOperationTypeField);
	MEFieldSafeDestroy(&pGroup->pGuildOperationValueField);
	MEFieldSafeDestroy(&pGroup->pGuildThemeNameField);
	MEFieldSafeDestroy(&pGroup->pItemAssignmentNameField);
	MEFieldSafeDestroy(&pGroup->pItemAssignmentOperationField);

	GEFreeVariableDefGroupSafe( &pGroup->pWarpDestGroup );
	GEFreeVariableDefGroupSafe( &pGroup->pDoorKeyDestGroup );
	eaDestroyEx( &pGroup->eaVariableDefGroups, GEFreeVariableDefGroup );
	
	if (pGroup->pShardVarGroup) {
		GEFreeVariableGroup(pGroup->pShardVarGroup);
		pGroup->pShardVarGroup = NULL;
	}

	free(pGroup);
}

void GECleanupActionGroup(GEActionGroup *pGroup, WorldGameActionProperties *pAction)
{
	// Each field lists above it the action types that use the field
	// This isn't the easiest to look at, but it turns out to be relatively simple to maintain

	if ((pAction->eActionType != WorldGameActionType_GrantMission) &&
		(pAction->eActionType != WorldGameActionType_GrantSubMission) &&
		(pAction->eActionType != WorldGameActionType_MissionOffer) &&
		(pAction->eActionType != WorldGameActionType_DropMission) &&
		(pAction->eActionType != WorldGameActionType_TakeItem) &&
		(pAction->eActionType != WorldGameActionType_GiveItem) &&
		(pAction->eActionType != WorldGameActionType_SendFloaterMsg) &&
		(pAction->eActionType != WorldGameActionType_SendNotification) &&
		(pAction->eActionType != WorldGameActionType_NPCSendMail) &&
		(pAction->eActionType != WorldGameActionType_GADAttribValue) &&
		(pAction->eActionType != WorldGameActionType_Contact) &&
		(pAction->eActionType != WorldGameActionType_Expression) &&
		(pAction->eActionType != WorldGameActionType_ChangeNemesisState) &&
		(pAction->eActionType != WorldGameActionType_ShardVariable) &&
		(pAction->eActionType != WorldGameActionType_Warp) &&
		(pAction->eActionType != WorldGameActionType_ActivityLog) &&
		(pAction->eActionType != WorldGameActionType_GiveDoorKeyItem) &&
		(pAction->eActionType != WorldGameActionType_GuildStatUpdate) &&
		(pAction->eActionType != WorldGameActionType_GuildThemeSet) &&
		(pAction->eActionType != WorldGameActionType_UpdateItemAssignment)
		) 
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pLabel1);
	}
	if ((pAction->eActionType != WorldGameActionType_GrantMission) &&
		(pAction->eActionType != WorldGameActionType_MissionOffer) && 
		(pAction->eActionType != WorldGameActionType_TakeItem) &&
		(pAction->eActionType != WorldGameActionType_GiveItem) &&
		(pAction->eActionType != WorldGameActionType_SendFloaterMsg) &&
		(pAction->eActionType != WorldGameActionType_SendNotification) &&
		(pAction->eActionType != WorldGameActionType_NPCSendMail) &&
		(pAction->eActionType != WorldGameActionType_GADAttribValue) &&
		(pAction->eActionType != WorldGameActionType_Contact) &&
		(pAction->eActionType != WorldGameActionType_ShardVariable) &&
		(pAction->eActionType != WorldGameActionType_Warp) &&
		(pAction->eActionType != WorldGameActionType_ActivityLog) &&
		(pAction->eActionType != WorldGameActionType_GiveDoorKeyItem) &&
		(pAction->eActionType != WorldGameActionType_GuildStatUpdate) &&
		(pAction->eActionType != WorldGameActionType_UpdateItemAssignment)
		) 
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pLabel2);
	}
	if ((pAction->eActionType != WorldGameActionType_MissionOffer) &&
		(pAction->eActionType != WorldGameActionType_TakeItem) &&
		(pAction->eActionType != WorldGameActionType_SendNotification) &&
		(pAction->eActionType != WorldGameActionType_NPCSendMail) &&
		(pAction->eActionType != WorldGameActionType_Warp) &&
		(pAction->eActionType != WorldGameActionType_ShardVariable) &&
		(pAction->eActionType != WorldGameActionType_GuildStatUpdate)
		) 
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pLabel3);
	}
	if ((pAction->eActionType != WorldGameActionType_MissionOffer) &&
		(pAction->eActionType != WorldGameActionType_NPCSendMail) &&
		(pAction->eActionType != WorldGameActionType_SendNotification) &&
		(pAction->eActionType != WorldGameActionType_ShardVariable)
		) 
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pLabel4);
	}
	if ((pAction->eActionType != WorldGameActionType_MissionOffer) &&
		(pAction->eActionType != WorldGameActionType_NPCSendMail) &&
		(pAction->eActionType != WorldGameActionType_SendNotification) &&
		(pAction->eActionType != WorldGameActionType_ShardVariable)
		) 
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pLabel5);
	}
	if ((pAction->eActionType != WorldGameActionType_MissionOffer) &&
		(pAction->eActionType != WorldGameActionType_SendNotification))
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pLabel6);
		ui_WidgetQueueFreeAndNull(&pGroup->pLabel7);
		ui_WidgetQueueFreeAndNull(&pGroup->pLabel8);
		ui_WidgetQueueFreeAndNull(&pGroup->pLabel9);
	}
	
	if (pAction->eActionType == WorldGameActionType_GrantMission ||
		pAction->eActionType == WorldGameActionType_MissionOffer)
	{
		WorldMissionActionType eType =
			(pAction->eActionType == WorldGameActionType_GrantMission) ? pAction->pGrantMissionProperties->eType : pAction->pMissionOfferProperties->eType;

		if (eType == WorldMissionActionType_Named)
		{
			MEFieldSafeDestroy(&pGroup->pMissionVarField);
			MEFieldSafeDestroy(&pGroup->pMissionMapVarField);
		}
		else if (eType == WorldMissionActionType_MissionVariable)
		{
			MEFieldSafeDestroy(&pGroup->pMissionField);
			MEFieldSafeDestroy(&pGroup->pMissionMapVarField);
		}
		else if (eType == WorldMissionActionType_MapVariable)
		{
			MEFieldSafeDestroy(&pGroup->pMissionField);
			MEFieldSafeDestroy(&pGroup->pMissionVarField);
		}
	}
	else
	{
		MEFieldSafeDestroy(&pGroup->pMissionFromField);
		MEFieldSafeDestroy(&pGroup->pMissionField);
		MEFieldSafeDestroy(&pGroup->pMissionVarField);
		MEFieldSafeDestroy(&pGroup->pMissionMapVarField);
	}
	if (pAction->eActionType != WorldGameActionType_MissionOffer)
	{
		MEFieldSafeDestroy(&pGroup->pOfferHeadshotDisplayName);
		MEFieldSafeDestroy(&pGroup->pOfferHeadshotTypeField);
		MEFieldSafeDestroy(&pGroup->pOfferHeadshotStyleField);
		MEFieldSafeDestroy(&pGroup->pOfferHeadshotField);
		MEFieldSafeDestroy(&pGroup->pOfferHeadshotPetContactField);
		MEFieldSafeDestroy(&pGroup->pOfferHeadshotCritterGroupTypeField);
		MEFieldSafeDestroy(&pGroup->pOfferHeadshotCritterGroupField);
		MEFieldSafeDestroy(&pGroup->pOfferHeadshotCritterGroupMapVarField);
		MEFieldSafeDestroy(&pGroup->pOfferHeadshotCritterGroupIdentifierField);
	}

	if (pAction->eActionType != WorldGameActionType_GrantSubMission)
	{
		MEFieldSafeDestroy(&pGroup->pSubMissionField);
	}
	if (pAction->eActionType != WorldGameActionType_DropMission)
	{
		MEFieldSafeDestroy(&pGroup->pDropMissionField);
	}
	if ((pAction->eActionType != WorldGameActionType_TakeItem) &&
		(pAction->eActionType != WorldGameActionType_GiveItem) &&
		(pAction->eActionType != WorldGameActionType_NPCSendMail) &&
		(pAction->eActionType != WorldGameActionType_GiveDoorKeyItem)
		)
	{
		MEFieldSafeDestroy(&pGroup->pItemField);
	}
	if ((pAction->eActionType != WorldGameActionType_TakeItem) &&
		(pAction->eActionType != WorldGameActionType_GiveItem) &&
		(pAction->eActionType != WorldGameActionType_NPCSendMail)
		)
	{
		MEFieldSafeDestroy(&pGroup->pCountField);
	}
	if (pAction->eActionType != WorldGameActionType_TakeItem)
	{
		MEFieldSafeDestroy(&pGroup->pBoolField);
	}
	if (pAction->eActionType != WorldGameActionType_SendFloaterMsg)
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pColorCombo)
		ui_WidgetQueueFreeAndNull(&pGroup->pColorButton)
	}
	if ((pAction->eActionType != WorldGameActionType_SendFloaterMsg) &&
		(pAction->eActionType != WorldGameActionType_SendNotification) &&
		(pAction->eActionType != WorldGameActionType_NPCSendMail) &&
		(pAction->eActionType != WorldGameActionType_ActivityLog)
		)
	{
		MEFieldSafeDestroy(&pGroup->pMessageField);
	}
	if (pAction->eActionType != WorldGameActionType_SendNotification)
	{
		MEFieldSafeDestroy(&pGroup->pNotifyTypeField);
		MEFieldSafeDestroy(&pGroup->pNotifySoundField);
		MEFieldSafeDestroy(&pGroup->pNotifyLogicalStringField);
		MEFieldSafeDestroy(&pGroup->pNotifyHeadshotTypeField);
		MEFieldSafeDestroy(&pGroup->pNotifyHeadshotStyleField);
		MEFieldSafeDestroy(&pGroup->pNotifyHeadshotField);
		MEFieldSafeDestroy(&pGroup->pNotifyHeadshotPetContactField);
		MEFieldSafeDestroy(&pGroup->pNotifyHeadshotCritterGroupTypeField);
		MEFieldSafeDestroy(&pGroup->pNotifyHeadshotCritterGroupField);
		MEFieldSafeDestroy(&pGroup->pNotifyHeadshotCritterGroupMapVarField);
		MEFieldSafeDestroy(&pGroup->pNotifyHeadshotCritterGroupIdentifierField);
	}
	if (pAction->eActionType != WorldGameActionType_NPCSendMail)
	{
		MEFieldSafeDestroy(&pGroup->pSubjectField);
		MEFieldSafeDestroy(&pGroup->pBodyField);
	}
	if (pAction->eActionType != WorldGameActionType_GADAttribValue)
	{
		MEFieldSafeDestroy(&pGroup->pAttribKeyField);
		MEFieldSafeDestroy(&pGroup->pAttribValueField);
		MEFieldSafeDestroy(&pGroup->pAttribModifyTypeField);
	}
	if (pAction->eActionType != WorldGameActionType_Warp)
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pDepartSequenceOverride);
		ui_WidgetQueueFreeAndNull(&pGroup->pSpecifyMapOverride);
		ui_WidgetQueueFreeAndNull(&pGroup->pIncludeTeammates);
		ui_WidgetQueueFreeAndNull(&pGroup->pSourceMapOverride);
		ui_WidgetQueueFreeAndNull(&pGroup->pDestMapOverride);
		MEFieldSafeDestroy(&pGroup->pTransSequenceField);
		GEFreeVariableDefGroupSafe(&pGroup->pWarpDestGroup);
	}
	if ((pAction->eActionType != WorldGameActionType_Warp) &&
		(pAction->eActionType != WorldGameActionType_GiveDoorKeyItem))
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pVarAddButton);
		eaDestroyEx(&pGroup->eaVariableDefGroups, GEFreeVariableDefGroup);
	}
	if (pAction->eActionType != WorldGameActionType_Contact)
	{
		MEFieldSafeDestroy(&pGroup->pContactField);
		MEFieldSafeDestroy(&pGroup->pNameField);
	}
	if (pAction->eActionType != WorldGameActionType_Expression)
	{
		MEFieldSafeDestroy(&pGroup->pExpressionField);
	}
	if (pAction->eActionType != WorldGameActionType_ChangeNemesisState)
	{
		MEFieldSafeDestroy(&pGroup->pNemesisStateField);
	}
	if (pAction->eActionType != WorldGameActionType_ShardVariable)
	{
		MEFieldSafeDestroy(&pGroup->pVarNameField);
		MEFieldSafeDestroy(&pGroup->pModifyTypeField);
		MEFieldSafeDestroy(&pGroup->pIntIncrementField);
		MEFieldSafeDestroy(&pGroup->pFloatIncrementField);
		if (pGroup->pShardVarGroup) {
			GEFreeVariableGroup(pGroup->pShardVarGroup);
			pGroup->pShardVarGroup = NULL;
		}
	}
	if (pAction->eActionType != WorldGameActionType_ActivityLog)
	{
		MEFieldSafeDestroy(&pGroup->pActivityLogTypeField);
	}
	if((pAction->eActionType != WorldGameActionType_GiveDoorKeyItem))
	{
		GEFreeVariableDefGroupSafe(&pGroup->pDoorKeyDestGroup);
		MEFieldSafeDestroy(&pGroup->pDoorKeyField);
	}
	if (pAction->eActionType != WorldGameActionType_GuildStatUpdate)
	{
		MEFieldSafeDestroy(&pGroup->pGuildStatNameField);
		MEFieldSafeDestroy(&pGroup->pGuildOperationTypeField);
		MEFieldSafeDestroy(&pGroup->pGuildOperationValueField);
	}
	if (pAction->eActionType != WorldGameActionType_GuildThemeSet)
	{
		MEFieldSafeDestroy(&pGroup->pGuildThemeNameField);
	}
	if (pAction->eActionType != WorldGameActionType_UpdateItemAssignment)
	{
		MEFieldSafeDestroy(&pGroup->pItemAssignmentNameField);
		MEFieldSafeDestroy(&pGroup->pItemAssignmentOperationField);
	}
}

static F32 GEUpdateGrantMissionAction(GEActionGroup *pGroup, const char *pcSrcZoneMap, WorldGameActionProperties *pAction, WorldGameActionProperties *pOrigAction, F32 y)
{
	// Check data
	if (!pAction->pGrantMissionProperties) {
		pAction->pGrantMissionProperties = StructCreate(parse_WorldGrantMissionActionProperties);
	}

	GECleanUpActionProperties(pAction);

	pGroup->pLabel1 = GERefreshLabel(pGroup->pLabel1, "Grant Type", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	// Grant type chooser
	if (!pGroup->pMissionFromField)
	{
		pGroup->pMissionFromField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigAction ? pOrigAction->pGrantMissionProperties : NULL, pAction->pGrantMissionProperties, parse_WorldGrantMissionActionProperties, "MissionFrom", WorldMissionActionTypeEnum);
		GEGameActionAddFieldToParent(pGroup->pMissionFromField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 200, UIUnitFixed, 3, pGroup);
	}
	else
	{
		ui_WidgetSetPosition(pGroup->pMissionFromField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pMissionFromField, pOrigAction ? pOrigAction->pGrantMissionProperties : NULL, pAction->pGrantMissionProperties);
	}
	y += Y_OFFSET_ROW;

	// Mission chooser
	if (pAction->pGrantMissionProperties->eType == WorldMissionActionType_Named)
	{
		pGroup->pLabel2 = GERefreshLabel(pGroup->pLabel2, "Mission", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
		if (!pGroup->pMissionField)
		{
			pGroup->pMissionField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOrigAction ? pOrigAction->pGrantMissionProperties : NULL, pAction->pGrantMissionProperties, parse_WorldGrantMissionActionProperties, "MissionDef", "Mission", "ResourceName");
			GEGameActionAddFieldToParent(pGroup->pMissionField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
			ui_TextEntryComboValidate(pGroup->pMissionField->pUIText);
		}
		else
		{
			ui_WidgetSetPosition(pGroup->pMissionField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMissionField, pOrigAction ? pOrigAction->pGrantMissionProperties : NULL, pAction->pGrantMissionProperties);
		}
	}
	// Mission variable chooser
	else if (pAction->pGrantMissionProperties->eType == WorldMissionActionType_MissionVariable)
	{
		pGroup->pLabel2 = GERefreshLabel(pGroup->pLabel2, "Variable", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
		if (!pGroup->pMissionVarField)
		{
			pGroup->pMissionVarField = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pOrigAction ? pOrigAction->pGrantMissionProperties : NULL, pAction->pGrantMissionProperties, parse_WorldGrantMissionActionProperties, "VariableName", parse_WorldVariableDef, &pGroup->peaVarDefs, "Name");
			GEGameActionAddFieldToParent(pGroup->pMissionVarField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
			ui_TextEntryComboValidate(pGroup->pMissionVarField->pUIText);
		}
		else
		{
			ui_WidgetSetPosition(pGroup->pMissionVarField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMissionVarField, pOrigAction ? pOrigAction->pGrantMissionProperties : NULL, pAction->pGrantMissionProperties);
		}
	}
	// Map variable chooser
	else
	{
		ZoneMapInfo* pSrcZoneMap = zmapInfoGetByPublicName(pcSrcZoneMap);
		WorldVariableDef ***pppMapVariableDefs = pSrcZoneMap ? zmapInfoGetVariableDefs(pSrcZoneMap) : NULL;
		pGroup->pLabel2 = GERefreshLabel(pGroup->pLabel2, "Variable", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
		if (!pGroup->pMissionMapVarField)
		{
			pGroup->pMissionMapVarField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigAction ? pOrigAction->pGrantMissionProperties : NULL, pAction->pGrantMissionProperties, parse_WorldGrantMissionActionProperties, "VariableName", parse_WorldVariableDef, pppMapVariableDefs, "Name");
			GEGameActionAddFieldToParent(pGroup->pMissionMapVarField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
		}
		else
		{
			pGroup->pMissionMapVarField->peaComboModel = pppMapVariableDefs;
			ui_WidgetSetPosition(pGroup->pMissionMapVarField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMissionMapVarField, pOrigAction ? pOrigAction->pGrantMissionProperties : NULL, pAction->pGrantMissionProperties);
		}
	}
	y += Y_OFFSET_ROW;

	// Clean up others
	GECleanupActionGroup(pGroup, pAction);

	return y;
}

static F32 GEUpdateGrantSubMissionAction(GEActionGroup *pGroup, WorldGameActionProperties *pAction, WorldGameActionProperties *pOrigAction, F32 y)
{
	// Check data
	if (!pAction->pGrantSubMissionProperties) {
		pAction->pGrantSubMissionProperties = StructCreate(parse_WorldGrantSubMissionActionProperties);
	}
	
	GECleanUpActionProperties(pAction);

	if (pGroup->peaSubMissions)
	{
		pGroup->pLabel1 = GERefreshLabel(pGroup->pLabel1, "SubMission", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

		// If the sub-mission earray has changed, destroy the MEField to get a complete refresh
		if (pGroup->pSubMissionField && pGroup->pSubMissionField->peaComboModel != pGroup->peaSubMissions){
			MEFieldSafeDestroy(&pGroup->pSubMissionField);
		}

		// Sub-Mission chooser
		if (!pGroup->pSubMissionField) {
			pGroup->pSubMissionField = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pOrigAction ? pOrigAction->pGrantSubMissionProperties : NULL, pAction->pGrantSubMissionProperties, parse_WorldGrantSubMissionActionProperties, "SubMissionName", parse_MissionDef, pGroup->peaSubMissions, "Name");
			GEGameActionAddFieldToParent(pGroup->pSubMissionField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
			ui_TextEntryComboValidate(pGroup->pSubMissionField->pUIText);
		} else {
			ui_WidgetSetPosition(pGroup->pSubMissionField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pSubMissionField, pOrigAction ? pOrigAction->pGrantSubMissionProperties : NULL, pAction->pGrantSubMissionProperties);
		}

		y += Y_OFFSET_ROW;
	}
	else
	{
		pGroup->pLabel1 = GERefreshLabel(pGroup->pLabel1, "Not supported", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
		y += Y_OFFSET_ROW;		

		MEFieldSafeDestroy(&pGroup->pSubMissionField);
	}

	// Clean up others
	GECleanupActionGroup(pGroup, pAction);

	return y;
}

static F32 GEUpdateDropMissionAction(GEActionGroup *pGroup, WorldGameActionProperties *pAction, WorldGameActionProperties *pOrigAction, F32 y)
{
	// Check data
	if (!pAction->pDropMissionProperties) {
		pAction->pDropMissionProperties = StructCreate(parse_WorldDropMissionActionProperties);
	}
	
	GECleanUpActionProperties(pAction);

	pGroup->pLabel1 = GERefreshLabel(pGroup->pLabel1, "Mission", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	// Mission Name
	if (!pGroup->pDropMissionField) {
		pGroup->pDropMissionField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOrigAction ? pOrigAction->pDropMissionProperties : NULL, pAction->pDropMissionProperties, parse_WorldDropMissionActionProperties, "MissionName", "Mission", "ResourceName");
		GEGameActionAddFieldToParent(pGroup->pDropMissionField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
		ui_TextEntryComboValidate(pGroup->pDropMissionField->pUIText);
	} else {
		ui_WidgetSetPosition(pGroup->pDropMissionField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pDropMissionField, pOrigAction ? pOrigAction->pDropMissionProperties : NULL, pAction->pDropMissionProperties);
	}

	y += Y_OFFSET_ROW;

	// Clean up others
	GECleanupActionGroup(pGroup, pAction);

	return y;
}

static F32 GEUpdateMissionOfferAction(GEActionGroup *pGroup, const char *pcSrcZoneMap, WorldGameActionProperties *pAction, WorldGameActionProperties *pOrigAction, F32 y)
{
	// Check data
	if (!pAction->pMissionOfferProperties) {
		pAction->pMissionOfferProperties = StructCreate(parse_WorldMissionOfferActionProperties);
	}

	GECleanUpActionProperties(pAction);

	pGroup->pLabel1 = GERefreshLabel(pGroup->pLabel1, "Offer Type", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	// Offer type chooser
	if (!pGroup->pMissionFromField)
	{
		pGroup->pMissionFromField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigAction ? pOrigAction->pMissionOfferProperties : NULL, pAction->pMissionOfferProperties, parse_WorldMissionOfferActionProperties, "MissionFrom", WorldMissionActionTypeEnum);
		GEGameActionAddFieldToParent(pGroup->pMissionFromField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 150, UIUnitFixed, 3, pGroup);
	}
	else
	{
		ui_WidgetSetPosition(pGroup->pMissionFromField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pMissionFromField, pOrigAction ? pOrigAction->pMissionOfferProperties : NULL, pAction->pMissionOfferProperties);
	}
	y += Y_OFFSET_ROW;

	// Mission chooser
	if (pAction->pMissionOfferProperties && pAction->pMissionOfferProperties->eType == WorldMissionActionType_Named)
	{
		pGroup->pLabel2 = GERefreshLabel(pGroup->pLabel2, "Mission", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
		if (!pGroup->pMissionField)
		{
			pGroup->pMissionField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOrigAction ? pOrigAction->pMissionOfferProperties : NULL, pAction->pMissionOfferProperties, parse_WorldMissionOfferActionProperties, "MissionDef", "Mission", "ResourceName");
			GEGameActionAddFieldToParent(pGroup->pMissionField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
			ui_TextEntryComboValidate(pGroup->pMissionField->pUIText);
		}
		else
		{
			ui_WidgetSetPosition(pGroup->pMissionField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMissionField, pOrigAction ? pOrigAction->pMissionOfferProperties : NULL, pAction->pMissionOfferProperties);
		}
	}
	// Mission variable chooser
	else if (pAction->pMissionOfferProperties->eType == WorldMissionActionType_MissionVariable)
	{
		pGroup->pLabel2 = GERefreshLabel(pGroup->pLabel2, "Variable", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
		if (!pGroup->pMissionVarField)
		{
			pGroup->pMissionVarField = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pOrigAction ? pOrigAction->pMissionOfferProperties : NULL, pAction->pMissionOfferProperties, parse_WorldMissionOfferActionProperties, "VariableName", parse_WorldVariableDef, &pGroup->peaVarDefs, "Name");
			GEGameActionAddFieldToParent(pGroup->pMissionVarField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
			ui_TextEntryComboValidate(pGroup->pMissionVarField->pUIText);
		}
		else
		{
			ui_WidgetSetPosition(pGroup->pMissionVarField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMissionVarField, pOrigAction ? pOrigAction->pMissionOfferProperties : NULL, pAction->pMissionOfferProperties);
		}
	}
	// Map variable chooser
	else
	{
		ZoneMapInfo* pSrcZoneMap = zmapInfoGetByPublicName(pcSrcZoneMap);
		WorldVariableDef ***pppMapVariableDefs = pSrcZoneMap ? zmapInfoGetVariableDefs(pSrcZoneMap) : NULL;
		pGroup->pLabel2 = GERefreshLabel(pGroup->pLabel2, "Variable", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
		if (!pGroup->pMissionMapVarField)
		{
			pGroup->pMissionMapVarField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigAction ? pOrigAction->pMissionOfferProperties : NULL, pAction->pMissionOfferProperties, parse_WorldMissionOfferActionProperties, "VariableName", parse_WorldVariableDef, pppMapVariableDefs, "Name");
			GEGameActionAddFieldToParent(pGroup->pMissionMapVarField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
		}
		else
		{
			pGroup->pMissionMapVarField->peaComboModel = pppMapVariableDefs;
			ui_WidgetSetPosition(pGroup->pMissionMapVarField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMissionMapVarField, pOrigAction ? pOrigAction->pMissionOfferProperties : NULL, pAction->pMissionOfferProperties);
		}
	}
	y += Y_OFFSET_ROW;

	// Headshot fields
	//   First the display name field
		
	pGroup->pLabel3 = GERefreshLabel(pGroup->pLabel3, "Headshot Name", "The text to display as the name of the headshot", X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);	

	if (!pGroup->pOfferHeadshotDisplayName) {
		pGroup->pOfferHeadshotDisplayName = MEFieldCreateSimple(kMEFieldType_Message,
												((pOrigAction && pOrigAction->pMissionOfferProperties) ?
													&pOrigAction->pMissionOfferProperties->headshotNameMsg : NULL),
												((pAction->pMissionOfferProperties) ?
													&pAction->pMissionOfferProperties->headshotNameMsg : NULL),
												parse_DisplayMessage, "editorCopy");
		GEGameActionAddFieldToParent(pGroup->pOfferHeadshotDisplayName, pGroup->pWidgetParent, X_OFFSET_CONTROL + X_OFFSET_INDENT, y, 0, 1.0, UIUnitPercentage, 3, pGroup);		
	} else {
		ui_WidgetSetPosition(pGroup->pOfferHeadshotDisplayName->pUIWidget, X_OFFSET_CONTROL + X_OFFSET_INDENT, y);
		MEFieldSetAndRefreshFromData(pGroup->pOfferHeadshotDisplayName,
												((pOrigAction && pOrigAction->pMissionOfferProperties) ?
													&pOrigAction->pMissionOfferProperties->headshotNameMsg : NULL),
												((pAction->pMissionOfferProperties) ?
													 &pAction->pMissionOfferProperties->headshotNameMsg : NULL));
	}
	
	y += Y_OFFSET_ROW;
	
	// Headshot fields
	//   Actual headshot data stuff
	{
		WorldGameActionHeadshotProperties *pHeadshot = NULL;
		WorldGameActionHeadshotProperties *pOrigHeadshot = NULL;

		if(!pAction->pMissionOfferProperties->pHeadshotProps)
			pAction->pMissionOfferProperties->pHeadshotProps = StructCreate(parse_WorldGameActionHeadshotProperties);

		pHeadshot = pAction->pMissionOfferProperties->pHeadshotProps;
		pOrigHeadshot = pOrigAction && pOrigAction->pMissionOfferProperties ? pAction->pMissionOfferProperties->pHeadshotProps : NULL;

		// Headshot type
		pGroup->pLabel4 = GERefreshLabel(pGroup->pLabel4, "Headshot From", "Where the offers's costume should be pulled from", X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

		if(!pGroup->pOfferHeadshotTypeField)
		{
			pGroup->pOfferHeadshotTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigHeadshot, pHeadshot, parse_WorldGameActionHeadshotProperties, "Type", WorldGameActionHeadshotTypeEnum);
			GEGameActionAddFieldToParent(pGroup->pOfferHeadshotTypeField, pGroup->pWidgetParent, X_OFFSET_CONTROL + X_OFFSET_INDENT, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
		} else {
			ui_WidgetSetPosition(pGroup->pOfferHeadshotTypeField->pUIWidget, X_OFFSET_CONTROL + X_OFFSET_INDENT, y);
			MEFieldSetAndRefreshFromData(pGroup->pOfferHeadshotTypeField, pOrigHeadshot, pHeadshot);
		}

		y += Y_OFFSET_ROW;

		// Headshot Style
		pGroup->pLabel5 = GERefreshLabel(pGroup->pLabel5, "Headshot Style", "The headshot style to use for this offer.", X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

		if(!pGroup->pOfferHeadshotStyleField)
		{
			pGroup->pOfferHeadshotStyleField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pOrigHeadshot, pHeadshot, parse_WorldGameActionHeadshotProperties, "HeadshotStyleDef", "HeadshotStyleDef", parse_HeadshotStyleDef, "Name");
			GEGameActionAddFieldToParent(pGroup->pOfferHeadshotStyleField, pGroup->pWidgetParent, X_OFFSET_CONTROL + X_OFFSET_INDENT, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
		} else {
			ui_WidgetSetPosition(pGroup->pOfferHeadshotStyleField->pUIWidget, X_OFFSET_CONTROL + X_OFFSET_INDENT, y);
			MEFieldSetAndRefreshFromData(pGroup->pOfferHeadshotStyleField, pOrigHeadshot, pHeadshot);
		}

		y += Y_OFFSET_ROW;

		// Specified Headshot
		if(pHeadshot->eType == WorldGameActionHeadshotType_Specified)
		{
			pGroup->pLabel6 = GERefreshLabel(pGroup->pLabel6, "Costume", "The costume to use for the headshot.", X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
			if(!pGroup->pOfferHeadshotField)
			{
				pGroup->pOfferHeadshotField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOrigHeadshot, pHeadshot, parse_WorldGameActionHeadshotProperties, "Costume", "PlayerCostume", "ResourceName");
				GEGameActionAddFieldToParent(pGroup->pOfferHeadshotField, pGroup->pWidgetParent, X_OFFSET_CONTROL + X_OFFSET_INDENT, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
			} else {
				ui_WidgetSetPosition(pGroup->pOfferHeadshotField->pUIWidget, X_OFFSET_CONTROL + X_OFFSET_INDENT, y);
				MEFieldSetAndRefreshFromData(pGroup->pOfferHeadshotField, pOrigHeadshot, pHeadshot);
			}

			y += Y_OFFSET_ROW;
		} else {
			MEFieldSafeDestroy(&pGroup->pOfferHeadshotField);
			if(IS_HANDLE_ACTIVE(pHeadshot->hCostume))
				REMOVE_HANDLE(pHeadshot->hCostume);
		}

		// Pet Contact List
		if(pHeadshot->eType == WorldGameActionHeadshotType_PetContactList)
		{
			pGroup->pLabel6 = GERefreshLabel(pGroup->pLabel6, "PetContactList", "The pet contact to use for headshot.", X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
			if(!pGroup->pOfferHeadshotPetContactField)
			{
				pGroup->pOfferHeadshotPetContactField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOrigHeadshot, pHeadshot, parse_WorldGameActionHeadshotProperties, "PetContactList", "PetContactList", "ResourceName");
				GEGameActionAddFieldToParent(pGroup->pOfferHeadshotPetContactField, pGroup->pWidgetParent, X_OFFSET_CONTROL + X_OFFSET_INDENT, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
			} else {
				ui_WidgetSetPosition(pGroup->pOfferHeadshotPetContactField->pUIWidget, X_OFFSET_CONTROL + X_OFFSET_INDENT, y);
				MEFieldSetAndRefreshFromData(pGroup->pOfferHeadshotPetContactField, pOrigHeadshot, pHeadshot);
			}

			y += Y_OFFSET_ROW;
		} else {
			MEFieldSafeDestroy(&pGroup->pOfferHeadshotPetContactField);
			if(IS_HANDLE_ACTIVE(pHeadshot->hPetContactList))
				REMOVE_HANDLE(pHeadshot->hPetContactList);
		}
		
		if(pHeadshot->eType == WorldGameActionHeadshotType_CritterGroup)
		{
			// Critter Group type
			pGroup->pLabel6 = GERefreshLabel(pGroup->pLabel6, "CritterGroup From", "Where the critter group should be gathered from.", X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
			if(!pGroup->pOfferHeadshotCritterGroupTypeField)
			{
				pGroup->pOfferHeadshotCritterGroupTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigHeadshot, pHeadshot, parse_WorldGameActionHeadshotProperties, "CritterGroupType", WorldHeadshotMapVarOverrideTypeEnum);
				GEGameActionAddFieldToParent(pGroup->pOfferHeadshotCritterGroupTypeField, pGroup->pWidgetParent, X_OFFSET_CONTROL + X_OFFSET_INDENT, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
			} else {
				ui_WidgetSetPosition(pGroup->pOfferHeadshotCritterGroupTypeField->pUIWidget, X_OFFSET_CONTROL + X_OFFSET_INDENT, y);
				MEFieldSetAndRefreshFromData(pGroup->pOfferHeadshotCritterGroupTypeField, pOrigHeadshot, pHeadshot);
			}

			y += Y_OFFSET_ROW;

			// Specified Critter Group
			if(pHeadshot->eCritterGroupType == WorldHeadshotMapVarOverrideType_Specified) {
				pGroup->pLabel7 = GERefreshLabel(pGroup->pLabel7, "CritterGroup", "The specified Critter Group which the contact's costume will be generated from.", X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
				if(!pGroup->pOfferHeadshotCritterGroupField)
				{
					pGroup->pOfferHeadshotCritterGroupField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOrigHeadshot, pHeadshot, parse_WorldGameActionHeadshotProperties, "CostumeCritterGroup", "CritterGroup", "resourceName");
					GEGameActionAddFieldToParent(pGroup->pOfferHeadshotCritterGroupField, pGroup->pWidgetParent, X_OFFSET_CONTROL + X_OFFSET_INDENT, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
				} else {
					ui_WidgetSetPosition(pGroup->pOfferHeadshotCritterGroupField->pUIWidget, X_OFFSET_CONTROL + X_OFFSET_INDENT, y);
					MEFieldSetAndRefreshFromData(pGroup->pOfferHeadshotCritterGroupField, pOrigHeadshot, pHeadshot);
				}
			} else {
				MEFieldSafeDestroy(&pGroup->pOfferHeadshotCritterGroupField);
				if(IS_HANDLE_ACTIVE(pHeadshot->hCostumeCritterGroup))
					REMOVE_HANDLE(pHeadshot->hCostumeCritterGroup);
			}

			// Critter Group from Map Var
			if(pHeadshot->eCritterGroupType == WorldHeadshotMapVarOverrideType_MapVar) {
				WorldVariableDef ***pppMapVariableDefs = zmapInfoGetVariableDefs(NULL);

				pGroup->pLabel7 = GERefreshLabel(pGroup->pLabel7, "Map Var", "The map variable where the Critter Group should be pulled from.", X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
				if(!pGroup->pOfferHeadshotCritterGroupMapVarField)
				{
					pGroup->pOfferHeadshotCritterGroupMapVarField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigHeadshot, pHeadshot, parse_WorldGameActionHeadshotProperties, "CritterGroupMapVar", parse_WorldVariableDef, pppMapVariableDefs, "Name");
					GEGameActionAddFieldToParent(pGroup->pOfferHeadshotCritterGroupMapVarField, pGroup->pWidgetParent, X_OFFSET_CONTROL + X_OFFSET_INDENT, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
				} else {
					pGroup->pOfferHeadshotCritterGroupMapVarField->peaComboModel = pppMapVariableDefs;
					ui_WidgetSetPosition(pGroup->pOfferHeadshotCritterGroupMapVarField->pUIWidget, X_OFFSET_CONTROL + X_OFFSET_INDENT, y);
					MEFieldSetAndRefreshFromData(pGroup->pOfferHeadshotCritterGroupMapVarField, pOrigHeadshot, pHeadshot);
				}
			} else {
				MEFieldSafeDestroy(&pGroup->pOfferHeadshotCritterGroupMapVarField);
				if(pHeadshot->pchCritterGroupMapVar)
					pHeadshot->pchCritterGroupMapVar = NULL;
			}

			y += Y_OFFSET_ROW;

			// Critter Group Identifier
			pGroup->pLabel8 = GERefreshLabel(pGroup->pLabel8, "Name", "A name for this costume.  To use this same costume again, use the same critter group and same name.", X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
			if(!pGroup->pOfferHeadshotCritterGroupIdentifierField)
			{
				pGroup->pOfferHeadshotCritterGroupIdentifierField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigHeadshot, pHeadshot, parse_WorldGameActionHeadshotProperties, "CritterGroupIdentifier");
				GEGameActionAddFieldToParent(pGroup->pOfferHeadshotCritterGroupIdentifierField, pGroup->pWidgetParent, X_OFFSET_CONTROL + X_OFFSET_INDENT, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
			} else {
				ui_WidgetSetPosition(pGroup->pOfferHeadshotCritterGroupIdentifierField->pUIWidget, X_OFFSET_CONTROL + X_OFFSET_INDENT, y);
				MEFieldSetAndRefreshFromData(pGroup->pOfferHeadshotCritterGroupIdentifierField, pOrigHeadshot, pHeadshot);
			}

			y += Y_OFFSET_ROW;

		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pLabel7);
			ui_WidgetQueueFreeAndNull(&pGroup->pLabel8);
			MEFieldSafeDestroy(&pGroup->pOfferHeadshotCritterGroupTypeField);
			MEFieldSafeDestroy(&pGroup->pOfferHeadshotCritterGroupField);
			MEFieldSafeDestroy(&pGroup->pOfferHeadshotCritterGroupMapVarField);
			MEFieldSafeDestroy(&pGroup->pOfferHeadshotCritterGroupIdentifierField);

			// Reset the fields
			pHeadshot->eCritterGroupType = 0;
			if(IS_HANDLE_ACTIVE(pHeadshot->hCostumeCritterGroup))
				REMOVE_HANDLE(pHeadshot->hCostumeCritterGroup);
			if(pHeadshot->pchCritterGroupMapVar)
				pHeadshot->pchCritterGroupMapVar = NULL;
			if(pHeadshot->pchCritterGroupIdentifier)
				pHeadshot->pchCritterGroupIdentifier = NULL;
		}
	}

	// Clean up others
	GECleanupActionGroup(pGroup, pAction);

	return y;
}


static F32 GEUpdateTakeItemAction(GEActionGroup *pGroup, WorldGameActionProperties *pAction, WorldGameActionProperties *pOrigAction, F32 y)
{
	// Check data
	if (!pAction->pTakeItemProperties) {
		pAction->pTakeItemProperties = StructCreate(parse_WorldTakeItemActionProperties);
		pAction->pTakeItemProperties->iCount = 1;
	}
	GECleanUpActionProperties(pAction);

	// Item chooser
	pGroup->pLabel1 = GERefreshLabel(pGroup->pLabel1, "Item", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
	if (!pGroup->pItemField) {
		pGroup->pItemField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOrigAction ? pOrigAction->pTakeItemProperties : NULL, pAction->pTakeItemProperties, parse_WorldTakeItemActionProperties, "ItemDef", "ItemDef", "ResourceName");
		GEGameActionAddFieldToParent(pGroup->pItemField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
		ui_TextEntryComboValidate(pGroup->pItemField->pUIText);
	} else {
		ui_WidgetSetPosition(pGroup->pItemField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pItemField, pOrigAction ? pOrigAction->pTakeItemProperties : NULL, pAction->pTakeItemProperties);
	}

	y += Y_OFFSET_ROW;

	// Count choice
	pGroup->pLabel2 = GERefreshLabel(pGroup->pLabel2, "Min Count", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
	if (!pGroup->pCountField) {
		pGroup->pCountField = MEFieldCreate(kMEFieldType_TextEntry, pOrigAction ? pOrigAction->pTakeItemProperties : NULL, pAction->pTakeItemProperties, parse_WorldTakeItemActionProperties, "count",
			NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, false, NULL, NULL, NULL, NULL,
			1, 0, 0, 0, NULL);
		GEGameActionAddFieldToParent(pGroup->pCountField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 0, pGroup);
	} else {
		ui_WidgetSetPosition(pGroup->pCountField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pCountField, pOrigAction ? pOrigAction->pTakeItemProperties : NULL, pAction->pTakeItemProperties);
	}

	y += Y_OFFSET_ROW;

	// Take others boolean
	pGroup->pLabel3 = GERefreshLabel(pGroup->pLabel3, "Take All", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
	if (!pGroup->pBoolField) {
		pGroup->pBoolField = MEFieldCreate(kMEFieldType_BooleanCombo, pOrigAction ? pOrigAction->pTakeItemProperties : NULL, pAction->pTakeItemProperties, parse_WorldTakeItemActionProperties, "TakeAll",
			NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, false, NULL, NULL, NULL, NULL,
			1, 0, 0, 0, NULL);
		GEGameActionAddFieldToParent(pGroup->pBoolField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 0, pGroup);
	} else {
		ui_WidgetSetPosition(pGroup->pBoolField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pBoolField, pOrigAction ? pOrigAction->pTakeItemProperties : NULL, pAction->pTakeItemProperties);
	}

	y += Y_OFFSET_ROW;

	// Clean up others
	GECleanupActionGroup(pGroup, pAction);

	return y;
}

static F32 GEUpdateGiveItemAction(GEActionGroup *pGroup, WorldGameActionProperties *pAction, WorldGameActionProperties *pOrigAction, F32 y)
{
	// Check data
	if (!pAction->pGiveItemProperties) {
		pAction->pGiveItemProperties = StructCreate(parse_WorldGiveItemActionProperties);
		pAction->pGiveItemProperties->iCount = 1;
	}
	GECleanUpActionProperties(pAction);

	// Item chooser
	pGroup->pLabel1 = GERefreshLabel(pGroup->pLabel1, "Item", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
	if (!pGroup->pItemField) {
		pGroup->pItemField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOrigAction ? pOrigAction->pGiveItemProperties : NULL, pAction->pGiveItemProperties, parse_WorldGiveItemActionProperties, "ItemDef", "ItemDef", "ResourceName");
		GEGameActionAddFieldToParent(pGroup->pItemField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
		ui_TextEntryComboValidate(pGroup->pItemField->pUIText);
	} else {
		ui_WidgetSetPosition(pGroup->pItemField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pItemField, pOrigAction ? pOrigAction->pGiveItemProperties : NULL, pAction->pGiveItemProperties);
	}

	y += Y_OFFSET_ROW;

	// Count choice
	pGroup->pLabel2 = GERefreshLabel(pGroup->pLabel2, "Count", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
	if (!pGroup->pCountField) {
		pGroup->pCountField = MEFieldCreate(kMEFieldType_TextEntry, pOrigAction ? pOrigAction->pGiveItemProperties : NULL, pAction->pGiveItemProperties, parse_WorldGiveItemActionProperties, "count",
			NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, false, NULL, NULL, NULL, NULL,
			1, 0, 0, 0, NULL);
		GEGameActionAddFieldToParent(pGroup->pCountField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 0, pGroup);
	} else {
		ui_WidgetSetPosition(pGroup->pCountField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pCountField, pOrigAction ? pOrigAction->pGiveItemProperties : NULL, pAction->pGiveItemProperties);
	}

	y += Y_OFFSET_ROW;

	// Clean up others
	GECleanupActionGroup(pGroup, pAction);

	return y;
}


static F32 GEUpdateSendFloaterAction(GEActionGroup *pGroup, WorldGameActionProperties *pAction, WorldGameActionProperties *pOrigAction, int index, F32 y)
{
	DisplayMessage *pDispMessage;
	DisplayMessage *pOrigDispMessage = NULL;
	Vec3 *pvParamColor;
	Vec4 vButtonColor = { 0, 0, 0, 1.0 };

	// Check data
	if (!pAction->pSendFloaterProperties) {
		pAction->pSendFloaterProperties = StructCreate(parse_WorldSendFloaterActionProperties);
		copyVec3(gvGainedColor, pAction->pSendFloaterProperties->vColor);
	}
	GECleanUpActionProperties(pAction);

	// The label
	pGroup->pLabel1 = GERefreshLabel(pGroup->pLabel1, "Text", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	// Message
	pDispMessage = &pAction->pSendFloaterProperties->floaterMsg;
		
	if (pOrigAction && (pOrigAction->eActionType == pAction->eActionType)) {
		pOrigDispMessage = &pOrigAction->pSendFloaterProperties->floaterMsg;
	}
	if (pDispMessage) {
		if (!pGroup->pMessageField) {
			pGroup->pMessageField = MEFieldCreateSimple(kMEFieldType_Message, pOrigDispMessage, pDispMessage, parse_DisplayMessage, "EditorCopy");
			GEGameActionAddFieldToParent(pGroup->pMessageField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
		} else {
			ui_WidgetSetPosition(pGroup->pMessageField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMessageField, pOrigDispMessage, pDispMessage);
		}
	}

	y += Y_OFFSET_ROW;

	// The label
	pGroup->pLabel2 = GERefreshLabel(pGroup->pLabel2, "Color", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	pvParamColor = &pAction->pSendFloaterProperties->vColor;
	if (pvParamColor) {
		vButtonColor[0] = (*pvParamColor)[0];
		vButtonColor[1] = (*pvParamColor)[1];
		vButtonColor[2] = (*pvParamColor)[2];
	} else {
		copyVec4(gvGainedColor, vButtonColor);
	}

	// The color combo
	if (!pGroup->pColorCombo) {
		pGroup->pColorCombo = ui_ComboBoxCreateWithEnum(X_OFFSET_CONTROL, y, 1, FloaterActionColorsEnum, GEColorComboCB, pGroup);
		ui_WidgetSetPaddingEx(UI_WIDGET(pGroup->pColorCombo), 0, 95, 0, 0);
		ui_WidgetSetWidthEx(UI_WIDGET(pGroup->pColorCombo), 1.0, UIUnitPercentage);
		ui_WidgetAddChild(pGroup->pWidgetParent, UI_WIDGET(pGroup->pColorCombo));
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pColorCombo), X_OFFSET_CONTROL, y);
	}
	if (nearSameVec4(vButtonColor, gvFailedColor)) {
		ui_ComboBoxSetSelectedEnum(pGroup->pColorCombo, FloaterActionColors_Failed);
	} else if (nearSameVec4(vButtonColor, gvGainedColor)) {
		ui_ComboBoxSetSelectedEnum(pGroup->pColorCombo, FloaterActionColors_Gained);
	} else if (nearSameVec4(vButtonColor, gvProgressColor)) {
		ui_ComboBoxSetSelectedEnum(pGroup->pColorCombo, FloaterActionColors_Progress);
	} else {
		ui_ComboBoxSetSelectedEnum(pGroup->pColorCombo, FloaterActionColors_Custom);
	}

	// The color button
	if (!pGroup->pColorButton) {
		Vec4 vInitial = {0, 0, 0, 0};
		pGroup->pColorButton = ui_ColorButtonCreate(0, y, vInitial);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pColorButton), 80);
		ui_ColorButtonSetChangedCallback(pGroup->pColorButton, GEColorButtonCB, pGroup);
		ui_WidgetAddChild(pGroup->pWidgetParent, UI_WIDGET(pGroup->pColorButton));
	}
	ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pColorButton), 3, y, 0, 0, UITopRight);
	ui_ColorButtonSetColor(pGroup->pColorButton, vButtonColor);

	y += Y_OFFSET_ROW;

	// Clean up others
	GECleanupActionGroup(pGroup, pAction);

	return y;
}

static const char*** notifyTypeListStatic()
{
	static const char** eaNotifyTypeList = NULL;
	if (!eaNotifyTypeList)
	{
		int i = 1; // StaticDefineInts use special values for thier 0th place
		const char* pchName;
		while (pchName = NotifyTypeEnum[i++].key)
		{
			eaPush(&eaNotifyTypeList, pchName);
		}
	}
	return &eaNotifyTypeList;
}

static F32 GEUpdateSendNotificationAction(GEActionGroup *pGroup, WorldGameActionProperties *pAction, WorldGameActionProperties *pOrigAction, int index, F32 y)
{
	DisplayMessage *pDispMessage;
	DisplayMessage *pOrigDispMessage = NULL;
	NotifyType eType = kNotifyType_Default;

	// Check data
	if (!pAction->pSendNotificationProperties) {
		pAction->pSendNotificationProperties = StructCreate(parse_WorldSendNotificationActionProperties);
	}
	GECleanUpActionProperties(pAction);

	// The label
	pGroup->pLabel1 = GERefreshLabel(pGroup->pLabel1, "Text", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	// Message
	pDispMessage = &pAction->pSendNotificationProperties->notifyMsg;
		
	if (pOrigAction && (pOrigAction->eActionType == pAction->eActionType)) {
		pOrigDispMessage = &pOrigAction->pSendNotificationProperties->notifyMsg;
	}
	if (pDispMessage) {
		if (!pGroup->pMessageField) {
			pGroup->pMessageField = MEFieldCreateSimple(kMEFieldType_Message, pOrigDispMessage, pDispMessage, parse_DisplayMessage, "EditorCopy");
			GEGameActionAddFieldToParent(pGroup->pMessageField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
		} else {
			ui_WidgetSetPosition(pGroup->pMessageField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMessageField, pOrigDispMessage, pDispMessage);
		}
	}

	y += Y_OFFSET_ROW;

	// The label
	pGroup->pLabel2 = GERefreshLabel(pGroup->pLabel2, "Sound", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	if (!pGroup->pNotifySoundField) {
		pGroup->pNotifySoundField = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pOrigAction ? pOrigAction->pSendNotificationProperties : NULL, pAction->pSendNotificationProperties, parse_WorldSendNotificationActionProperties, "Sound", NULL, sndGetEventListStatic(), NULL);
		GEGameActionAddFieldToParent(pGroup->pNotifySoundField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
	} else {
		ui_WidgetSetPosition(pGroup->pNotifySoundField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pNotifySoundField, pOrigAction ? pOrigAction->pSendNotificationProperties : NULL, pAction->pSendNotificationProperties);
	}

	y += Y_OFFSET_ROW;

	// The label
	pGroup->pLabel3 = GERefreshLabel(pGroup->pLabel3, "Notify Type", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	if (!pGroup->pNotifyTypeField) {
		pGroup->pNotifyTypeField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigAction ? pOrigAction->pSendNotificationProperties : NULL, pAction->pSendNotificationProperties, parse_WorldSendNotificationActionProperties, "NotifyType", NULL, notifyTypeListStatic(), NULL);
		GEGameActionAddFieldToParent(pGroup->pNotifyTypeField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
	} else {
		ui_WidgetSetPosition(pGroup->pNotifyTypeField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pNotifyTypeField, pOrigAction ? pOrigAction->pSendNotificationProperties : NULL, pAction->pSendNotificationProperties);
	}

	y += Y_OFFSET_ROW;

	eType = StaticDefineIntGetInt(NotifyTypeEnum, pAction->pSendNotificationProperties->pchNotifyType);

	// Notification types for which splat FX's are allowed
	if(eType == kNotifyType_MissionSplatFX)
	{
		pGroup->pLabel4 = GERefreshLabel(pGroup->pLabel4, "Splat FX", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
		
		if(!pGroup->pNotifySplatFXField)
		{
			pGroup->pNotifySplatFXField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pOrigAction ? pOrigAction->pSendNotificationProperties : NULL, pAction->pSendNotificationProperties, parse_WorldSendNotificationActionProperties, "splatFX", "DynFXInfo", parse_DynFxInfo, "InternalName");
			GEGameActionAddFieldToParent(pGroup->pNotifySplatFXField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
		}
		else
		{
			ui_WidgetSetPosition(pGroup->pNotifySplatFXField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pNotifySplatFXField, pOrigAction ? pOrigAction->pSendNotificationProperties : NULL, pAction->pSendNotificationProperties);
		}

		y += Y_OFFSET_ROW;
	}
	else
	{
		MEFieldSafeDestroy(&pGroup->pNotifySplatFXField);

		ui_WidgetQueueFreeAndNull(&pGroup->pLabel4);

		if(pAction->pSendNotificationProperties->pchSplatFX)
			StructFreeStringSafe(&pAction->pSendNotificationProperties->pchSplatFX);
	}
	// Notification types for which headshots are allowed (add to this list if you wish to enable headshots for other types)
	if(eType == kNotifyType_MiniContact)
	{
		WorldGameActionHeadshotProperties *pHeadshot = NULL;
		WorldGameActionHeadshotProperties *pOrigHeadshot = NULL;

		if(!pAction->pSendNotificationProperties->pHeadshotProperties)
			pAction->pSendNotificationProperties->pHeadshotProperties = StructCreate(parse_WorldGameActionHeadshotProperties);

		pHeadshot = pAction->pSendNotificationProperties->pHeadshotProperties;
		pOrigHeadshot = pOrigAction && pOrigAction->pSendNotificationProperties ? pAction->pSendNotificationProperties->pHeadshotProperties : NULL;

		// Headshot type
		pGroup->pLabel4 = GERefreshLabel(pGroup->pLabel4, "Headshot From", "Where the notification's costume should be pulled from", X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

		if(!pGroup->pNotifyHeadshotTypeField)
		{
			pGroup->pNotifyHeadshotTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigHeadshot, pHeadshot, parse_WorldGameActionHeadshotProperties, "Type", WorldGameActionHeadshotTypeEnum);
			GEGameActionAddFieldToParent(pGroup->pNotifyHeadshotTypeField, pGroup->pWidgetParent, X_OFFSET_CONTROL + X_OFFSET_INDENT, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
		} else {
			ui_WidgetSetPosition(pGroup->pNotifyHeadshotTypeField->pUIWidget, X_OFFSET_CONTROL + X_OFFSET_INDENT, y);
			MEFieldSetAndRefreshFromData(pGroup->pNotifyHeadshotTypeField, pOrigHeadshot, pHeadshot);
		}

		y += Y_OFFSET_ROW;

		// Headshot Style
		pGroup->pLabel5 = GERefreshLabel(pGroup->pLabel5, "Headshot Style", "The headshot style to use for this notification.", X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

		if(!pGroup->pNotifyHeadshotStyleField)
		{
			pGroup->pNotifyHeadshotStyleField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pOrigHeadshot, pHeadshot, parse_WorldGameActionHeadshotProperties, "HeadshotStyleDef", "HeadshotStyleDef", parse_HeadshotStyleDef, "Name");
			GEGameActionAddFieldToParent(pGroup->pNotifyHeadshotStyleField, pGroup->pWidgetParent, X_OFFSET_CONTROL + X_OFFSET_INDENT, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
		} else {
			ui_WidgetSetPosition(pGroup->pNotifyHeadshotStyleField->pUIWidget, X_OFFSET_CONTROL + X_OFFSET_INDENT, y);
			MEFieldSetAndRefreshFromData(pGroup->pNotifyHeadshotStyleField, pOrigHeadshot, pHeadshot);
		}

		y += Y_OFFSET_ROW;

		// Specified Headshot
		if(pHeadshot->eType == WorldGameActionHeadshotType_Specified)
		{
			pGroup->pLabel6 = GERefreshLabel(pGroup->pLabel6, "Costume", "The costume to use for the headshot.", X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
			if(!pGroup->pNotifyHeadshotField)
			{
				pGroup->pNotifyHeadshotField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOrigHeadshot, pHeadshot, parse_WorldGameActionHeadshotProperties, "Costume", "PlayerCostume", "ResourceName");
				GEGameActionAddFieldToParent(pGroup->pNotifyHeadshotField, pGroup->pWidgetParent, X_OFFSET_CONTROL + X_OFFSET_INDENT, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
			} else {
				ui_WidgetSetPosition(pGroup->pNotifyHeadshotField->pUIWidget, X_OFFSET_CONTROL + X_OFFSET_INDENT, y);
				MEFieldSetAndRefreshFromData(pGroup->pNotifyHeadshotField, pOrigHeadshot, pHeadshot);
			}

			y += Y_OFFSET_ROW;
		} else {
			MEFieldSafeDestroy(&pGroup->pNotifyHeadshotField);
			if(IS_HANDLE_ACTIVE(pHeadshot->hCostume))
				REMOVE_HANDLE(pHeadshot->hCostume);
		}

		// Pet Contact List
		if(pHeadshot->eType == WorldGameActionHeadshotType_PetContactList)
		{
			pGroup->pLabel6 = GERefreshLabel(pGroup->pLabel6, "PetContactList", "The pet contact to use for headshot.", X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
			if(!pGroup->pNotifyHeadshotPetContactField)
			{
				pGroup->pNotifyHeadshotPetContactField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pOrigHeadshot, pHeadshot, parse_WorldGameActionHeadshotProperties, "PetContactList", "PetContactList", parse_PetContactList, "Name");
				GEGameActionAddFieldToParent(pGroup->pNotifyHeadshotPetContactField, pGroup->pWidgetParent, X_OFFSET_CONTROL + X_OFFSET_INDENT, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
			} else {
				ui_WidgetSetPosition(pGroup->pNotifyHeadshotPetContactField->pUIWidget, X_OFFSET_CONTROL + X_OFFSET_INDENT, y);
				MEFieldSetAndRefreshFromData(pGroup->pNotifyHeadshotPetContactField, pOrigHeadshot, pHeadshot);
			}

			y += Y_OFFSET_ROW;
		} else {
			MEFieldSafeDestroy(&pGroup->pNotifyHeadshotPetContactField);
			if(IS_HANDLE_ACTIVE(pHeadshot->hPetContactList))
				REMOVE_HANDLE(pHeadshot->hPetContactList);
		}
		
		if(pHeadshot->eType == WorldGameActionHeadshotType_CritterGroup)
		{
			// Critter Group type
			pGroup->pLabel6 = GERefreshLabel(pGroup->pLabel6, "CritterGroup From", "Where the critter group should be gathered from.", X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
			if(!pGroup->pNotifyHeadshotCritterGroupTypeField)
			{
				pGroup->pNotifyHeadshotCritterGroupTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigHeadshot, pHeadshot, parse_WorldGameActionHeadshotProperties, "CritterGroupType", WorldHeadshotMapVarOverrideTypeEnum);
				GEGameActionAddFieldToParent(pGroup->pNotifyHeadshotCritterGroupTypeField, pGroup->pWidgetParent, X_OFFSET_CONTROL + X_OFFSET_INDENT, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
			} else {
				ui_WidgetSetPosition(pGroup->pNotifyHeadshotCritterGroupTypeField->pUIWidget, X_OFFSET_CONTROL + X_OFFSET_INDENT, y);
				MEFieldSetAndRefreshFromData(pGroup->pNotifyHeadshotCritterGroupTypeField, pOrigHeadshot, pHeadshot);
			}

			y += Y_OFFSET_ROW;

			// Specified Critter Group
			if(pHeadshot->eCritterGroupType == WorldHeadshotMapVarOverrideType_Specified) {
				pGroup->pLabel7 = GERefreshLabel(pGroup->pLabel7, "CritterGroup", "The specified Critter Group which the contact's costume will be generated from.", X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
				if(!pGroup->pNotifyHeadshotCritterGroupField)
				{
					pGroup->pNotifyHeadshotCritterGroupField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOrigHeadshot, pHeadshot, parse_WorldGameActionHeadshotProperties, "CostumeCritterGroup", "CritterGroup", "resourceName");
					GEGameActionAddFieldToParent(pGroup->pNotifyHeadshotCritterGroupField, pGroup->pWidgetParent, X_OFFSET_CONTROL + X_OFFSET_INDENT, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
				} else {
					ui_WidgetSetPosition(pGroup->pNotifyHeadshotCritterGroupField->pUIWidget, X_OFFSET_CONTROL + X_OFFSET_INDENT, y);
					MEFieldSetAndRefreshFromData(pGroup->pNotifyHeadshotCritterGroupField, pOrigHeadshot, pHeadshot);
				}
			} else {
				MEFieldSafeDestroy(&pGroup->pNotifyHeadshotCritterGroupField);
				if(IS_HANDLE_ACTIVE(pHeadshot->hCostumeCritterGroup))
					REMOVE_HANDLE(pHeadshot->hCostumeCritterGroup);
			}

			// Critter Group from Map Var
			if(pHeadshot->eCritterGroupType == WorldHeadshotMapVarOverrideType_MapVar) {
				WorldVariableDef ***pppMapVariableDefs = zmapInfoGetVariableDefs(NULL);

				pGroup->pLabel7 = GERefreshLabel(pGroup->pLabel7, "Map Var", "The map variable where the Critter Group should be pulled from.", X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
				if(!pGroup->pNotifyHeadshotCritterGroupMapVarField)
				{
					pGroup->pNotifyHeadshotCritterGroupMapVarField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigHeadshot, pHeadshot, parse_WorldGameActionHeadshotProperties, "CritterGroupMapVar", parse_WorldVariableDef, pppMapVariableDefs, "Name");
					GEGameActionAddFieldToParent(pGroup->pNotifyHeadshotCritterGroupMapVarField, pGroup->pWidgetParent, X_OFFSET_CONTROL + X_OFFSET_INDENT, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
				} else {
					pGroup->pNotifyHeadshotCritterGroupMapVarField->peaComboModel = pppMapVariableDefs;
					ui_WidgetSetPosition(pGroup->pNotifyHeadshotCritterGroupMapVarField->pUIWidget, X_OFFSET_CONTROL + X_OFFSET_INDENT, y);
					MEFieldSetAndRefreshFromData(pGroup->pNotifyHeadshotCritterGroupMapVarField, pOrigHeadshot, pHeadshot);
				}
			} else {
				MEFieldSafeDestroy(&pGroup->pNotifyHeadshotCritterGroupMapVarField);
				if(pHeadshot->pchCritterGroupMapVar)
					pHeadshot->pchCritterGroupMapVar = NULL;
			}

			y += Y_OFFSET_ROW;

			// Critter Group Identifier
			pGroup->pLabel8 = GERefreshLabel(pGroup->pLabel8, "Name", "A name for this costume.  To use this same costume again, use the same critter group and same name.", X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
			if(!pGroup->pNotifyHeadshotCritterGroupIdentifierField)
			{
				pGroup->pNotifyHeadshotCritterGroupIdentifierField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigHeadshot, pHeadshot, parse_WorldGameActionHeadshotProperties, "CritterGroupIdentifier");
				GEGameActionAddFieldToParent(pGroup->pNotifyHeadshotCritterGroupIdentifierField, pGroup->pWidgetParent, X_OFFSET_CONTROL + X_OFFSET_INDENT, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
			} else {
				ui_WidgetSetPosition(pGroup->pNotifyHeadshotCritterGroupIdentifierField->pUIWidget, X_OFFSET_CONTROL + X_OFFSET_INDENT, y);
				MEFieldSetAndRefreshFromData(pGroup->pNotifyHeadshotCritterGroupIdentifierField, pOrigHeadshot, pHeadshot);
			}

			y += Y_OFFSET_ROW;

		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pLabel7);
			ui_WidgetQueueFreeAndNull(&pGroup->pLabel8);
			MEFieldSafeDestroy(&pGroup->pNotifyHeadshotCritterGroupTypeField);
			MEFieldSafeDestroy(&pGroup->pNotifyHeadshotCritterGroupField);
			MEFieldSafeDestroy(&pGroup->pNotifyHeadshotCritterGroupMapVarField);
			MEFieldSafeDestroy(&pGroup->pNotifyHeadshotCritterGroupIdentifierField);

			// Reset the fields
			pHeadshot->eCritterGroupType = 0;
			if(IS_HANDLE_ACTIVE(pHeadshot->hCostumeCritterGroup))
				REMOVE_HANDLE(pHeadshot->hCostumeCritterGroup);
			if(pHeadshot->pchCritterGroupMapVar)
				pHeadshot->pchCritterGroupMapVar = NULL;
			if(pHeadshot->pchCritterGroupIdentifier)
				pHeadshot->pchCritterGroupIdentifier = NULL;
		}

	} else {
		MEFieldSafeDestroy(&pGroup->pNotifyHeadshotTypeField);
		MEFieldSafeDestroy(&pGroup->pNotifyHeadshotStyleField);
		MEFieldSafeDestroy(&pGroup->pNotifyHeadshotField);
		MEFieldSafeDestroy(&pGroup->pNotifyHeadshotPetContactField);
		MEFieldSafeDestroy(&pGroup->pNotifyHeadshotCritterGroupTypeField);
		MEFieldSafeDestroy(&pGroup->pNotifyHeadshotCritterGroupField);
		MEFieldSafeDestroy(&pGroup->pNotifyHeadshotCritterGroupMapVarField);
		MEFieldSafeDestroy(&pGroup->pNotifyHeadshotCritterGroupIdentifierField);

		ui_WidgetQueueFreeAndNull(&pGroup->pLabel6);
		ui_WidgetQueueFreeAndNull(&pGroup->pLabel7);
		ui_WidgetQueueFreeAndNull(&pGroup->pLabel8);

		if(pAction->pSendNotificationProperties->pHeadshotProperties)
			StructDestroySafe(parse_WorldGameActionHeadshotProperties, &pAction->pSendNotificationProperties->pHeadshotProperties);
	}

	// The logical string (varies based on notify type)
	if (eType == kNotifyType_Tip_General)
	{
		pGroup->pLabel9 = GERefreshLabel(pGroup->pLabel9, "Screen Region", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

		if (!pGroup->pNotifyLogicalStringField) {
			pGroup->pNotifyLogicalStringField = MEFieldCreateSimpleEnum(kMEFieldType_ValidatedTextEntry, pOrigAction ? pOrigAction->pSendNotificationProperties : NULL, pAction->pSendNotificationProperties, parse_WorldSendNotificationActionProperties, "LogicalString", TutorialScreenRegionEnum);
			GEGameActionAddFieldToParent(pGroup->pNotifyLogicalStringField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
		} else {
			ui_WidgetSetPosition(pGroup->pNotifyLogicalStringField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pNotifyLogicalStringField, pOrigAction ? pOrigAction->pSendNotificationProperties : NULL, pAction->pSendNotificationProperties);
		}

		y += Y_OFFSET_ROW;
	} else {
		MEFieldSafeDestroy(&pGroup->pNotifyLogicalStringField);

		ui_WidgetQueueFreeAndNull(&pGroup->pLabel9);

		if(pAction->pSendNotificationProperties->pchLogicalString)
			StructFreeStringSafe(&pAction->pSendNotificationProperties->pchLogicalString);
	}

	// Clean up others
	GECleanupActionGroup(pGroup, pAction);

	return y;
}

static F32 GEUpdateNPCSendEmailAction(GEActionGroup *pGroup, WorldGameActionProperties *pAction, WorldGameActionProperties *pOrigAction, F32 y)
{
	DisplayMessage *pFrom;
	DisplayMessage *pOrigFrom = NULL;
	DisplayMessage *pSubject;
	DisplayMessage *pOrigSubject = NULL;
	DisplayMessage *pBody;
	DisplayMessage *pOrigBody = NULL;

	// Check data
	if (!pAction->pNPCSendEmailProperties)
	{
		pAction->pNPCSendEmailProperties = StructCreate(parse_WorldNPCSendEmailActionProperties);
	}
	
	GECleanUpActionProperties(pAction);

	// The label
	pGroup->pLabel1 = GERefreshLabel(pGroup->pLabel1, "FromName", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	// Message
	pFrom = &pAction->pNPCSendEmailProperties->dFromName;
		
	if (pOrigAction && (pOrigAction->eActionType == pAction->eActionType)) {
		pOrigFrom = &pOrigAction->pNPCSendEmailProperties->dFromName;
	}
	if (pFrom) {
		if (!pGroup->pMessageField) {
			pGroup->pMessageField = MEFieldCreateSimple(kMEFieldType_Message, pOrigFrom, pFrom, parse_DisplayMessage, "EditorCopy");
			GEGameActionAddFieldToParent(pGroup->pMessageField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
		} else {
			ui_WidgetSetPosition(pGroup->pMessageField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMessageField, pOrigFrom, pFrom);
		}
	}

	y += Y_OFFSET_ROW;

	// The label
	pGroup->pLabel2 = GERefreshLabel(pGroup->pLabel2, "Subject", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	// Message
	pSubject = &pAction->pNPCSendEmailProperties->dSubject;
		
	if (pOrigAction && (pOrigAction->eActionType == pAction->eActionType)) {
		pOrigSubject = &pOrigAction->pNPCSendEmailProperties->dSubject;
	}
	if (pSubject) {
		if (!pGroup->pSubjectField) {
			pGroup->pSubjectField = MEFieldCreateSimple(kMEFieldType_Message, pOrigSubject, pSubject, parse_DisplayMessage, "EditorCopy");
			GEGameActionAddFieldToParent(pGroup->pSubjectField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
		} else {
			ui_WidgetSetPosition(pGroup->pSubjectField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pSubjectField, pOrigSubject, pSubject);
		}
	}

	y += Y_OFFSET_ROW;

	// The label
	pGroup->pLabel3 = GERefreshLabel(pGroup->pLabel3, "Body", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	// Message
	pBody = &pAction->pNPCSendEmailProperties->dBody;
		
	if (pOrigAction && (pOrigAction->eActionType == pAction->eActionType)) {
		pOrigBody = &pOrigAction->pNPCSendEmailProperties->dBody;
	}
	if (pBody) {
		if (!pGroup->pBodyField) {
			pGroup->pBodyField = MEFieldCreateSimple(kMEFieldType_Message, pOrigBody, pBody, parse_DisplayMessage, "EditorCopy");
			GEGameActionAddFieldToParent(pGroup->pBodyField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
		} else {
			ui_WidgetSetPosition(pGroup->pBodyField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pBodyField, pOrigBody, pBody);
		}
	}

	y += Y_OFFSET_ROW;

	// Item chooser
	pGroup->pLabel4 = GERefreshLabel(pGroup->pLabel4, "Item", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
	if (!pGroup->pItemField) {
		pGroup->pItemField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOrigAction ? pOrigAction->pNPCSendEmailProperties : NULL, pAction->pNPCSendEmailProperties, parse_WorldNPCSendEmailActionProperties, "ItemDef", "ItemDef", "ResourceName");
		GEGameActionAddFieldToParent(pGroup->pItemField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
		ui_TextEntryComboValidate(pGroup->pItemField->pUIText);
	} else {
		ui_WidgetSetPosition(pGroup->pItemField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pItemField, pOrigAction ? pOrigAction->pNPCSendEmailProperties : NULL, pAction->pNPCSendEmailProperties);
	}

	y += Y_OFFSET_ROW;

	// Count choice
	pGroup->pLabel5 = GERefreshLabel(pGroup->pLabel5, "FutureSendSeconds", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
	if (!pGroup->pCountField) {
		pGroup->pCountField = MEFieldCreate(kMEFieldType_TextEntry, pOrigAction ? pOrigAction->pNPCSendEmailProperties : NULL, pAction->pNPCSendEmailProperties, parse_WorldNPCSendEmailActionProperties, "FutureSendTime",
			NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, false, NULL, NULL, NULL, NULL,
			1, 0, 0, 0, NULL);
		GEGameActionAddFieldToParent(pGroup->pCountField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 0, pGroup);
	} else {
		ui_WidgetSetPosition(pGroup->pCountField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pCountField, pOrigAction ? pOrigAction->pNPCSendEmailProperties : NULL, pAction->pNPCSendEmailProperties);
	}

	y += Y_OFFSET_ROW;

	// Clean up others
	GECleanupActionGroup(pGroup, pAction);

	return y;
}

static F32 GEUpdateGADAttribValueAction(GEActionGroup *pGroup, WorldGameActionProperties *pAction, WorldGameActionProperties *pOrigAction, F32 y)
{

	// Check data
	if (!pAction->pGADAttribValueProperties)
	{
		pAction->pGADAttribValueProperties = StructCreate(parse_WorldGADAttribValueActionProperties);
	}

	GECleanUpActionProperties(pAction);

	// The label
	pGroup->pLabel1 = GERefreshLabel(pGroup->pLabel1, "Key", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
	if (!pGroup->pAttribKeyField) {
		pGroup->pAttribKeyField = MEFieldCreate(kMEFieldType_TextEntry, pOrigAction ? pOrigAction->pGADAttribValueProperties : NULL, pAction->pGADAttribValueProperties, parse_WorldGADAttribValueActionProperties, "Attrib",
			NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, false, NULL, NULL, NULL, NULL,
			1, 0, 0, 0, NULL);
		GEGameActionAddFieldToParent(pGroup->pAttribKeyField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 0, pGroup);
	} else {
		ui_WidgetSetPosition(pGroup->pAttribKeyField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pAttribKeyField, pOrigAction ? pOrigAction->pGADAttribValueProperties : NULL, pAction->pGADAttribValueProperties);
	}

	y += Y_OFFSET_ROW;

	// The label
	pGroup->pLabel2 = GERefreshLabel(pGroup->pLabel2, "Value", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	if (!pGroup->pAttribValueField) {
		pGroup->pAttribValueField = MEFieldCreate(kMEFieldType_TextEntry, pOrigAction ? pOrigAction->pGADAttribValueProperties : NULL, pAction->pGADAttribValueProperties, parse_WorldGADAttribValueActionProperties, "Value",
			NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, false, NULL, NULL, NULL, NULL,
			1, 0, 0, 0, NULL);
		GEGameActionAddFieldToParent(pGroup->pAttribValueField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 0, pGroup);
	} else {
		ui_WidgetSetPosition(pGroup->pAttribValueField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pAttribValueField, pOrigAction ? pOrigAction->pGADAttribValueProperties : NULL, pAction->pGADAttribValueProperties);
	}

	y += Y_OFFSET_ROW;

	// The label
	pGroup->pLabel3 = GERefreshLabel(pGroup->pLabel3, "ModifyType", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	if (!pGroup->pAttribModifyTypeField) {
		pGroup->pAttribModifyTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigAction ? pOrigAction->pGADAttribValueProperties : NULL, pAction->pGADAttribValueProperties, parse_WorldGADAttribValueActionProperties, "ModifyType", WorldVariableActionTypeEnum);
		GEGameActionAddFieldToParent(pGroup->pAttribModifyTypeField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 0, pGroup);
	} else {
		ui_WidgetSetPosition(pGroup->pAttribModifyTypeField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pAttribModifyTypeField, pOrigAction ? pOrigAction->pGADAttribValueProperties : NULL, pAction->pGADAttribValueProperties);
	}

	y += Y_OFFSET_ROW;

	// Clean up others
	GECleanupActionGroup(pGroup, pAction);

	return y;
}

static void GEActionAddVariableDef(UIButton *pButton, GEActionGroup *pGroup)
{
	WorldVariableDef *pVarDef = StructCreate(parse_WorldVariableDef);
	eaPush(&(*pGroup->peaActions)[pGroup->index]->pWarpProperties->eaVariableDefs, pVarDef);

	// Notify of change
	GEGameActionFieldChangedCB(NULL, true, pGroup);
}

static void GEActionAddDoorKeyVariableDef(UIButton *pButton, GEActionGroup *pGroup)
{
	WorldVariableDef *pVarDef = StructCreate(parse_WorldVariableDef);
	eaPush(&(*pGroup->peaActions)[pGroup->index]->pGiveDoorKeyItemProperties->eaVariableDefs, pVarDef);

	// Notify of change
	GEGameActionFieldChangedCB(NULL, true, pGroup);
}

static void GERemoveVariable(UIButton *pButton, GEVariableGroup *pGroup)
{
	if (pGroup->preChangeFunc && !(*pGroup->preChangeFunc)(NULL, true, pGroup->pData))
	{
		// If pre-change fails, then don't to the removal
		return;
	}

	StructDestroy(parse_WorldVariable, (*pGroup->peaVariables)[pGroup->index]);
	eaRemove(pGroup->peaVariables, pGroup->index);

	// Notify of change
	if (pGroup->changeFunc)
	{
		(*pGroup->changeFunc)(NULL, true, pGroup->pData);
	}
}

static void GERemoveVariableDef(UIButton *pButton, GEVariableDefGroup *pGroup)
{
	bool handled = false;
	if (pGroup->preChangeFunc && !(*pGroup->preChangeFunc)(NULL, true, pGroup->pData))
	{
		// If pre-change fails, then don't to the removal
		return;
	}
	if (!pGroup->peaVariableDefs || !(*pGroup->peaVariableDefs))
	{
		return;
	}
	//If we have a name, match by that instead.
	if (pGroup->pNameField)
	{
		char* pcName = NULL;
		int i;

		estrStackCreate(&pcName);
		MEFieldGetString(pGroup->pNameField, &pcName);

		if (pcName && pcName[0])
		{
			for (i = 0; i < eaSize(pGroup->peaVariableDefs); i++)
			{
				if (stricmp(pcName, (*pGroup->peaVariableDefs)[i]->pcName) == 0)
				{
					StructDestroy(parse_WorldVariableDef, (*pGroup->peaVariableDefs)[i]);
					eaRemove(pGroup->peaVariableDefs, i);
					handled = true;
					break;
				}
			}
		}

		estrDestroy(&pcName);
	}
	if (!handled && pGroup->index < eaSize(pGroup->peaVariableDefs))
	{
		StructDestroy(parse_WorldVariableDef, (*pGroup->peaVariableDefs)[pGroup->index]);
		eaRemove(pGroup->peaVariableDefs, pGroup->index);
		handled = true;
	}

	assertmsg(handled, "Tried to remove a worldvariabledef that couldn't be found. This is probably cmiller's fault, go yell at him.");

	// Notify of change
	if (pGroup->changeFunc)
	{
		(*pGroup->changeFunc)(NULL, true, pGroup->pData);
	}
}


F32 GEUpdateVariableGroupValue(GEVariableGroup *pGroup, UIWidget *pParent, WorldVariable *pVar, WorldVariable *pOrigVar, WorldVariableType eType, F32 y, MEFieldChangeCallback changeFunc, MEFieldPreChangeCallback preChangeFunc, void *pData)
{
	// Value
	pGroup->pValueLabel = GERefreshLabel(pGroup->pValueLabel, "Value", NULL, X_OFFSET_INDENT+15, 0, y, pParent);
	if (eType != pVar->eType) {
		pVar->eType = eType;
	}
	if (pVar->eType != pGroup->ePrevType) {
		pGroup->ePrevType = pVar->eType;
		MEFieldSafeDestroy(&pGroup->pValueField);
	}
	if (!pGroup->pValueField) {
		ParseTable *pTable = parse_WorldVariable;
		const char *pcField;
		const char *pcDictName = NULL;
		MEFieldType eFieldType = kMEFieldType_TextEntry;
		void *pOld = pOrigVar, *pNew = pVar;

		// VARIABLE_TYPES: Add code below if add to the available variable types
		switch(pVar->eType) {
			xcase WVAR_INT:   
				pcField = "IntVal";
			xcase WVAR_FLOAT: 
				pcField = "FloatVal";
			xcase WVAR_STRING: 
			case WVAR_LOCATION_STRING:
				pcField = "StringVal";
			xcase WVAR_ANIMATION:
				pcField = "StringVal";
				pcDictName = "AIAnimList";
			xcase WVAR_CRITTER_DEF:
				pcField = "CritterDef";
				pcDictName = "CritterDef";
			xcase WVAR_CRITTER_GROUP:
				pcField = "CritterGroup";
				pcDictName = "CritterGroup";
			xcase WVAR_MESSAGE:
				pcField = NULL;
				eFieldType = kMEFieldType_Message;
				pTable = parse_DisplayMessage;
				pOld = pOrigVar ? pOrigVar->messageVal.pEditorCopy : NULL;
				pNew = pVar ? pVar->messageVal.pEditorCopy : NULL;
				if (!pNew) {
					pNew = pVar->messageVal.pEditorCopy = StructCreate(parse_Message);
				}
				pGroup->pValueField = MEFieldCreateSimple(kMEFieldType_Message, pOld, pNew, pTable, "EditorCopy");
				GEAddFieldToParent(pGroup->pValueField, pParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, changeFunc, preChangeFunc, pData);
			xcase WVAR_ITEM_DEF:
				pcField = "StringVal";
				pcDictName = "ItemDef";
			xcase WVAR_MISSION_DEF:
				pcField = "StringVal";
				pcDictName = "Mission";
			xcase WVAR_NONE:
			default:
				pcField = NULL; 
		}
		if (pcField) {
			pGroup->pValueField = MEFieldCreateSimpleGlobalDictionary(eFieldType, pOld, pNew, pTable, pcField, pcDictName, pcDictName ? "ResourceName" : NULL);
			GEAddFieldToParent(pGroup->pValueField, pParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, changeFunc, preChangeFunc, pData);
		}
	} else {
		void *pOld = pOrigVar, *pNew = pVar;
		if (pVar->eType == WVAR_MESSAGE) {
			pOld = pOrigVar ? pOrigVar->messageVal.pEditorCopy : NULL;
			pNew = pVar ? pVar->messageVal.pEditorCopy : NULL;
		}
		ui_WidgetSetPosition(pGroup->pValueField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pValueField, pOld, pNew);
	}

	y += Y_OFFSET_ROW;

	return y;

}


F32 GEUpdateVariableGroup(GEVariableGroup *pGroup, UIWidget *pParent, WorldVariable ***peaVariables, WorldVariable *pVar, WorldVariable *pOrigVar, WorldVariableDef ***peaVarDefs, const char ***peaVarNames, int index, F32 y, MEFieldChangeCallback changeFunc, MEFieldPreChangeCallback preChangeFunc, void *pData)
{
	WorldVariableDef *pVarDef = NULL;
	char buf[256];
	int i;

	pGroup->index = index;
	pGroup->peaVariables = peaVariables;
	pGroup->changeFunc = changeFunc;
	pGroup->preChangeFunc = preChangeFunc;
	pGroup->pData = pData;

	// Figure out which variable this is
	for(i=eaSize(peaVarDefs)-1; i>=0; --i) {
		if (pVar->pcName && stricmp((*peaVarDefs)[i]->pcName, pVar->pcName) == 0) {
			pVarDef = (*peaVarDefs)[i];
			break;
		}
	}

	// Title
	sprintf(buf, "Variable #%d", index+1);
	pGroup->pTitleLabel = GERefreshLabel(pGroup->pTitleLabel, buf, NULL, X_OFFSET_INDENT, 0, y, pParent);

	// Remove button
	if (!pGroup->pRemoveButton) {
		pGroup->pRemoveButton = ui_ButtonCreate("Delete Var", X_OFFSET_CONTROL, y, GERemoveVariable, pGroup);
		ui_WidgetAddChild(pParent, UI_WIDGET(pGroup->pRemoveButton));
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pRemoveButton), X_OFFSET_CONTROL, y);
	}

	y += Y_OFFSET_ROW;

	// Name
	pGroup->pNameLabel = GERefreshLabel(pGroup->pNameLabel, "Name", NULL, X_OFFSET_INDENT+15, 0, y, pParent);
	if (!pGroup->pNameField) {
		pGroup->pNameField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigVar, pVar, parse_WorldVariable, "Name", NULL, peaVarNames, NULL);
		GEAddFieldToParent(pGroup->pNameField, pParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, changeFunc, preChangeFunc, pData);
	} else {
		ui_WidgetSetPosition(pGroup->pNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pNameField, pOrigVar, pVar);
	}

	y += Y_OFFSET_ROW;

	// Type
	pGroup->pTypeLabel = GERefreshLabel(pGroup->pTypeLabel, "Type", NULL, X_OFFSET_INDENT+15, 0, y, pParent);
	sprintf(buf, "%s", StaticDefineIntRevLookup(WorldVariableTypeEnum, pVar->eType));
	pGroup->pTypeValueLabel = GERefreshLabel(pGroup->pTypeValueLabel, buf, NULL, X_OFFSET_CONTROL, 0, y, pParent);

	y += Y_OFFSET_ROW;

	// Value
	y = GEUpdateVariableGroupValue(pGroup, pParent, pVar, pOrigVar, pVarDef ? pVarDef->eType : WVAR_NONE, y, changeFunc, preChangeFunc, pData);

	return y;

}

void GECheckNameList( char*** peaCurrentList, char*** peaNewList )
{
	bool bChanged = false;
	int i;

	if (eaSize(peaCurrentList) == eaSize(peaNewList)) {
		for(i=eaSize(peaCurrentList)-1; i>=0; --i) {
			if (stricmp((*peaCurrentList)[i], (*peaNewList)[i]) != 0) {
				bChanged = true;
				break;
			}
		}
	} else {
		bChanged = true;
	}
	if (bChanged) {
		eaDestroyEx(peaCurrentList, NULL);
		*peaCurrentList = *peaNewList;
	} else {
		eaDestroyEx(peaNewList, NULL);
	}
}

F32 GEUpdateVariableDefGroupFromNames(GEVariableDefGroup *pGroup, UIWidget *pParent, const char*** peaDefNames, WorldVariableDef ***peaAllVariableDefs, WorldVariableDef ***peaModifiableVariableDefs, WorldVariableDef *pVarDef, WorldVariableDef *pOrigVarDef, const char* pchZoneMap, int index, F32 x, F32 xControl, F32 y, MEFieldChangeCallback changeFunc, MEFieldPreChangeCallback preChangeFunc, void *pData)
{
	WorldVariableDef *pVarDefDef = NULL;
	char buf[256];

	pGroup->index = index;
	pGroup->peaVariableDefs = peaModifiableVariableDefs;

	if( pGroup->eaDestVariableDefs ) {
		eaDestroy( &pGroup->eaDestVariableDefs );
	}

	// Title
	sprintf(buf, "Variable #%d", index+1);
	pGroup->pTitleLabel = GERefreshLabel(pGroup->pTitleLabel, buf, NULL, x, 0, y, pParent);

	// Remove button
	if (!pGroup->pRemoveButton) {
		pGroup->pRemoveButton = ui_ButtonCreate("Delete Var", xControl, y, GERemoveVariableDef, pGroup);
		ui_WidgetAddChild(pParent, UI_WIDGET(pGroup->pRemoveButton));
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pRemoveButton), xControl, y);
	}

	y += Y_OFFSET_ROW;

	// Name
	pGroup->pNameLabel = GERefreshLabel(pGroup->pNameLabel, "Name", NULL, x+15, 0, y, pParent);
	if (!pGroup->pNameField) {
		pGroup->pNameField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigVarDef, pVarDef, parse_WorldVariableDef, "Name", NULL, peaDefNames, NULL);
		GEAddFieldToParent(pGroup->pNameField, pParent, xControl, y, 0, 1.0, UIUnitPercentage, 3, changeFunc, preChangeFunc, pData);
	} else {
		ui_WidgetSetPosition(pGroup->pNameField->pUIWidget, xControl, y);
		MEFieldSetAndRefreshFromData(pGroup->pNameField, pOrigVarDef, pVarDef);
	}

	y += Y_OFFSET_ROW;

	// Type
	pGroup->pTypeLabel = GERefreshLabel(pGroup->pTypeLabel, "Type", NULL, x+15, 0, y, pParent);
	ui_WidgetQueueFreeAndNull(&pGroup->pTypeValueLabel);
	if(!pGroup->pTypeField) {
		pGroup->pTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigVarDef, pVarDef, parse_WorldVariableDef, "Type", WorldVariableTypeEnum);
		GEAddFieldToParent(pGroup->pTypeField, pParent, xControl, y, 0, 0.55, UIUnitPercentage, 3, changeFunc, preChangeFunc, pData);
	} else {
		ui_WidgetSetPosition(pGroup->pTypeField->pUIWidget, xControl, y);
		MEFieldSetAndRefreshFromData(pGroup->pTypeField, pOrigVarDef, pVarDef);
	}
	pVarDefDef = pVarDef;

	y += Y_OFFSET_ROW;

	return GEUpdateVariableDefGroupNoName(pGroup, pParent, pVarDef, pOrigVarDef,
		(pVarDefDef ? pVarDefDef->eType : WVAR_NONE), pchZoneMap, NULL,
		"Init From", "How this variable is specified.",
		x + 15, xControl, y, changeFunc, preChangeFunc, pData);
}


F32 GEUpdateVariableDefGroup(GEVariableDefGroup *pGroup, UIWidget *pParent, WorldVariableDef ***peaVariableDefs, WorldVariableDef *pVarDef, WorldVariableDef *pOrigVarDef, const char* pchSrcZoneMap, const char** eaSrcMapVariableNames, const char * pchDestZoneMap, int index, F32 x, F32 xControl, F32 y, MEFieldChangeCallback changeFunc, MEFieldPreChangeCallback preChangeFunc, void *pData)
{
	ZoneMapInfo* pSrcZoneMap = zmapInfoGetByPublicName(pchSrcZoneMap);
	ZoneMapInfo* pDestZoneMap = zmapInfoGetByPublicName(pchDestZoneMap);
			
	WorldVariableDef *pVarDefDef = NULL;
	char buf[256];
	
	pGroup->index = index;
	pGroup->peaVariableDefs = peaVariableDefs;

	// Update list of var defs
	if( pDestZoneMap ) {
		char** destVariableDefs = zmapInfoGetVariableNames( pDestZoneMap );
		GECheckNameList( &pGroup->eaDestVariableDefs, &destVariableDefs );
	} else {
		eaDestroy( &pGroup->eaDestVariableDefs );
	}

	// Figure out which variable this is
	pVarDefDef = zmapInfoGetVariableDefByName( pDestZoneMap, pVarDef->pcName );

	// Title
	sprintf(buf, "Variable #%d", index+1);
	pGroup->pTitleLabel = GERefreshLabel(pGroup->pTitleLabel, buf, NULL, x, 0, y, pParent);

	// Remove button
	if (!pGroup->pRemoveButton) {
		pGroup->pRemoveButton = ui_ButtonCreate("Delete Var", xControl, y, GERemoveVariableDef, pGroup);
		ui_WidgetAddChild(pParent, UI_WIDGET(pGroup->pRemoveButton));
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pRemoveButton), xControl, y);
	}

	y += Y_OFFSET_ROW;

	// Name
	pGroup->pNameLabel = GERefreshLabel(pGroup->pNameLabel, "Name", NULL, x+15, 0, y, pParent);
	if (!pGroup->pNameField) {
		pGroup->pNameField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigVarDef, pVarDef, parse_WorldVariableDef, "Name", NULL, &pGroup->eaDestVariableDefs, NULL);
		GEAddFieldToParent(pGroup->pNameField, pParent, xControl, y, 0, 1.0, UIUnitPercentage, 3, changeFunc, preChangeFunc, pData);
	} else {
		ui_WidgetSetPosition(pGroup->pNameField->pUIWidget, xControl, y);
		MEFieldSetAndRefreshFromData(pGroup->pNameField, pOrigVarDef, pVarDef);
	}

	y += Y_OFFSET_ROW;

	// Type
	pGroup->pTypeLabel = GERefreshLabel(pGroup->pTypeLabel, "Type", NULL, x+15, 0, y, pParent);
	if(pVarDefDef)
	{
		MEFieldSafeDestroy(&pGroup->pTypeField);
		sprintf(buf, "%s", StaticDefineIntRevLookup(WorldVariableTypeEnum, pVarDef->eType));
		pGroup->pTypeValueLabel = GERefreshLabel(pGroup->pTypeValueLabel, buf, NULL, xControl, 0, y, pParent);
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pTypeValueLabel);
		if(!pGroup->pTypeField) {
			pGroup->pTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigVarDef, pVarDef, parse_WorldVariableDef, "Type", WorldVariableTypeEnum);
			GEAddFieldToParent(pGroup->pTypeField, pParent, xControl, y, 0, 0.55, UIUnitPercentage, 3, changeFunc, preChangeFunc, pData);
		} else {
			ui_WidgetSetPosition(pGroup->pTypeField->pUIWidget, xControl, y);
			MEFieldSetAndRefreshFromData(pGroup->pTypeField, pOrigVarDef, pVarDef);
		}
		pVarDefDef = pVarDef;
	}

	y += Y_OFFSET_ROW;

	return GEUpdateVariableDefGroupNoName(pGroup, pParent, pVarDef, pOrigVarDef,
										  (pVarDefDef ? pVarDefDef->eType : WVAR_NONE), pchSrcZoneMap, eaSrcMapVariableNames,
										  "Init From", "How this variable is specified.",
										  x + 15, xControl, y, changeFunc, preChangeFunc, pData);
}

F32 GEUpdateVariableDefGroupNoName(GEVariableDefGroup *pGroup, UIWidget *pParent, WorldVariableDef *pVarDef, WorldVariableDef *pOrigVarDef, WorldVariableType varType, const char* pchSrcZoneMap, const char** eaSrcMapVariableNames, char* initFromText, char* initFromTooltip, F32 x, F32 xControl, F32 y, MEFieldChangeCallback changeFunc, MEFieldPreChangeCallback preChangeFunc, void *pData)
{
	ZoneMapInfo* pSrcZoneMap = zmapInfoGetByPublicName(pchSrcZoneMap);

	assert( !pchSrcZoneMap || !eaSrcMapVariableNames );

	// make sure choice table dictionary is editable
	if (!resIsDictionaryEditMode("ChoiceTable"))
		resSetDictionaryEditMode("ChoiceTable", true);
	
	// Update list of map variables
	if (eaSrcMapVariableNames) {
		char** srcMapVariables = NULL;
		eaCopyEx( &(char**)eaSrcMapVariableNames, &srcMapVariables, strdupFunc, strFreeFunc );
		GECheckNameList( &pGroup->eaSrcMapVariables, &srcMapVariables );
	} else if (pSrcZoneMap) {
		char** srcMapVariables = zmapInfoGetVariableNames(pSrcZoneMap);
		GECheckNameList( &pGroup->eaSrcMapVariables, &srcMapVariables );
	} else {
		eaDestroyEx(&pGroup->eaSrcMapVariables, NULL);
	}

	pGroup->changeFunc = changeFunc;
	pGroup->preChangeFunc = preChangeFunc;
	pGroup->pData = pData;

	// Init From
	pGroup->pInitFromLabel = GERefreshLabel(pGroup->pInitFromLabel, initFromText, initFromTooltip, x, 0, y, pParent);
	if (!pGroup->pInitFromField) {
		pGroup->pInitFromField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigVarDef, pVarDef, parse_WorldVariableDef, "DefaultType", WorldVariableDefaultValueTypeEnum);
		GEAddFieldToParent(pGroup->pInitFromField, pParent, xControl, y, 0, 140, UIUnitFixed, 0, changeFunc, preChangeFunc, pData);
	} else {
		ui_WidgetSetPosition(pGroup->pInitFromField->pUIWidget, xControl, y);
		MEFieldSetAndRefreshFromData(pGroup->pInitFromField, pOrigVarDef, pVarDef);
	}
	y += Y_OFFSET_ROW;

	if (pVarDef->eDefaultType == WVARDEF_SPECIFY_DEFAULT)
	{
		const char* pcLabel = NULL;
		const char* pcTooltip = NULL;
		const char* pcField = NULL;
		const char* pcDictName = NULL;
		const char* pcLabel2 = NULL;
		const char* pcTooltip2 = NULL;
		const char* pcField2 = NULL;
		const char* pcDictName2 = NULL;
		ParseTable *pTable = parse_WorldVariable;
		MEFieldType eFieldType = kMEFieldType_TextEntry;
		void *pOld = SAFE_MEMBER(pOrigVarDef, pSpecificValue), *pNew = pVarDef->pSpecificValue;
		
		if (varType != pVarDef->eType) {
			pVarDef->eType = varType;
		}
		if (pVarDef->eType != pGroup->ePrevType) {
			pGroup->ePrevType = pVarDef->eType;
			MEFieldSafeDestroy(&pGroup->pValueField);
		}
		if (!pNew) {
			pNew = pVarDef->pSpecificValue = StructCreate(parse_WorldVariable);
		}
		pVarDef->pSpecificValue->eType = pVarDef->eType;

		// VARIABLE_TYPES: Add code below if add to the available variable types
		switch (pVarDef->eType) {
			xcase WVAR_INT:
				pcLabel = "Int Value";
				pcField = "IntVal";
			xcase WVAR_FLOAT:
				pcLabel = "Float Value";
				pcField = "FloatVal";
			xcase WVAR_STRING:
				pcLabel = "String";
				pcField = "StringVal";
			xcase WVAR_LOCATION_STRING:
				pcLabel = "Loc. String";
				pcField = "StringVal";
			xcase WVAR_ANIMATION:
				pcLabel = "Animation";
				pcField = "StringVal";
				pcDictName = "AIAnimList";
			xcase WVAR_CRITTER_DEF:
				pcLabel = "Critter Def";
				pcField = "CritterDef";
				pcDictName = "CritterDef";
			xcase WVAR_CRITTER_GROUP:
				pcLabel = "Critter Group";
				pcField = "CritterGroup";
				pcDictName = "CritterGroup";
			xcase WVAR_MESSAGE:
				pcLabel = "Message";
				pcField = "EditorCopy";
				eFieldType = kMEFieldType_Message;
				pTable = parse_DisplayMessage;
				pOld = SAFE_MEMBER_ADDR2(pOrigVarDef, pSpecificValue, messageVal);
				if (!pVarDef->pSpecificValue->messageVal.pEditorCopy) {
					pVarDef->pSpecificValue->messageVal.pEditorCopy = StructCreate(parse_Message);
				}
				pNew = &pVarDef->pSpecificValue->messageVal;
			xcase WVAR_MAP_POINT:
				pcLabel = "Zone Map";
				pcTooltip = "The target zonemap.  If left empty, the current map.";
				pcField = "ZoneMap";
				pcDictName = "ZoneMap";
				pcLabel2 = "Spawn Point";
				pcTooltip2 = "The target spawn point on the zonemap.  Can also be MissionReturn to go back to how this map was entered.";
				pcField2 = "StringVal";
			xcase WVAR_ITEM_DEF:
				pcLabel = "ItemDef";
				pcField = "StringVal";
				pcDictName = "ItemDef";
			xcase WVAR_MISSION_DEF:
				pcLabel = "MissionDef";
				pcField = "StringVal";
				pcDictName = "Mission";
		}
		
		// Value
		if (pcField) {
			pGroup->pValueLabel = GERefreshLabel(pGroup->pValueLabel, pcLabel, pcTooltip, x, 0, y, pParent);
			
			if (!pGroup->pValueField && pcField) {
				pGroup->pValueField = MEFieldCreateSimpleGlobalDictionary(eFieldType, pOld, pNew, pTable, pcField, pcDictName, pcDictName ? "ResourceName" : NULL);
				GEAddFieldToParent(pGroup->pValueField, pParent, xControl, y, 0, 1.0, UIUnitPercentage, 3, changeFunc, preChangeFunc, pData);
			} else {
				ui_WidgetSetPosition(pGroup->pValueField->pUIWidget, xControl, y);
				MEFieldSetAndRefreshFromData(pGroup->pValueField, pOld, pNew);
			}
			y += Y_OFFSET_ROW;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pValueLabel);
			MEFieldSafeDestroy(&pGroup->pValueField);
		}

		if (pcField2) {
			pGroup->pValueLabel2 = GERefreshLabel(pGroup->pValueLabel2, pcLabel2, pcTooltip2, x, 0, y, pParent);
		
			if (!pGroup->pValueField2) {
				pGroup->pValueField2 = MEFieldCreateSimple(eFieldType, pOld, pNew, parse_WorldVariable, pcField2);
				GEAddFieldToParent(pGroup->pValueField2, pParent, xControl, y, 0, 1.0, UIUnitPercentage, 3, changeFunc, preChangeFunc, pData);
			} else {
				ui_WidgetSetPosition(pGroup->pValueField2->pUIWidget, xControl, y);
				MEFieldSetAndRefreshFromData(pGroup->pValueField2, pOld, pNew);
			}
			y += Y_OFFSET_ROW;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pValueLabel2);
			MEFieldSafeDestroy(&pGroup->pValueField2);
		}
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pValueLabel2);
		MEFieldSafeDestroy(&pGroup->pValueField);
		MEFieldSafeDestroy(&pGroup->pValueField2);
	}

	if (pVarDef->eDefaultType == WVARDEF_CHOICE_TABLE) {
		const char *pchChoiceTableName = REF_STRING_FROM_HANDLE(pVarDef->choice_table);
		ChoiceTable *pChoiceTable = GET_REF(pVarDef->choice_table);
		char** partNames = choice_ListNames(pchChoiceTableName);
		int iMax = pChoiceTable ? choice_TimedRandomValuesPerInterval(pChoiceTable) : 0;

		GECheckNameList( &pGroup->eaChoiceTableNames, &partNames );

		// Choice Table
		pGroup->pChoiceTableLabel = GERefreshLabel(pGroup->pChoiceTableLabel, "Choice Table", "Value comes from this choice table.", x, 0, y, pParent);
		if (!pGroup->pChoiceTableField) {
			pGroup->pChoiceTableField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, pOrigVarDef, pVarDef, parse_WorldVariableDef, "ChoiceTable", "ChoiceTable", "ResourceName");
			GEAddFieldToParent(pGroup->pChoiceTableField, pParent, xControl, y, 0, 1.0, UIUnitPercentage, 3, changeFunc, preChangeFunc, pData);
		} else {
			ui_WidgetSetPosition(pGroup->pChoiceTableField->pUIWidget, xControl, y);
			MEFieldSetAndRefreshFromData(pGroup->pChoiceTableField, pOrigVarDef, pVarDef);
		}
		y += Y_OFFSET_ROW;

		// Choice name
		pGroup->pChoiceNameLabel = GERefreshLabel(pGroup->pChoiceNameLabel, "Choice Value", "Value comes from this value in the choice table use.  If two places specify the same choice table, the values will come from the same row.", x, 0, y, pParent);
		if (!pGroup->pChoiceNameField) {
			pGroup->pChoiceNameField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigVarDef, pVarDef, parse_WorldVariableDef, "ChoiceName", NULL, &pGroup->eaChoiceTableNames, NULL);
			GEAddFieldToParent(pGroup->pChoiceNameField, pParent, xControl, y, 0, 1.0, UIUnitPercentage, 3, changeFunc, preChangeFunc, pData);
		} else {
			ui_WidgetSetPosition(pGroup->pChoiceNameField->pUIWidget, xControl, y);
			MEFieldSetAndRefreshFromData(pGroup->pChoiceNameField, pOrigVarDef, pVarDef);
		}
		y += Y_OFFSET_ROW;

		// Choice index
		if (pChoiceTable && pChoiceTable->eSelectType == CST_TimedRandom && iMax > 0)
		{
			pGroup->pChoiceIndexLabel = GERefreshLabel(pGroup->pChoiceIndexLabel, "Random Index", "Chooses which timed value will be used; different timed indexes are guaranteed to yield unique values.", x, 0, y, pParent);
			if (!pGroup->pChoiceIndexField) {
				pGroup->pChoiceIndexField = MEFieldCreateSimpleDataProvided(kMEFieldType_Spinner, pOrigVarDef, pVarDef, parse_WorldVariableDef, "ChoiceIndex", NULL, &pGroup->eaChoiceTableNames, NULL);
				GEAddFieldToParent(pGroup->pChoiceIndexField, pParent, xControl, y, 0, 1.0, UIUnitPercentage, 3, changeFunc, preChangeFunc, pData);
			} else {
				ui_WidgetSetPosition(pGroup->pChoiceIndexField->pUIWidget, xControl, y);
				MEFieldSetAndRefreshFromData(pGroup->pChoiceIndexField, pOrigVarDef, pVarDef);
			}
			ui_SpinnerEntrySetBounds((UISpinnerEntry*) pGroup->pChoiceIndexField->pUIWidget, 1, iMax, 1);
			y += Y_OFFSET_ROW;
		}
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pChoiceTableLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pChoiceNameLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pChoiceIndexLabel);
		MEFieldSafeDestroy(&pGroup->pChoiceTableField);
		MEFieldSafeDestroy(&pGroup->pChoiceNameField);
		MEFieldSafeDestroy(&pGroup->pChoiceIndexField);
	}

	if (pVarDef->eDefaultType == WVARDEF_MAP_VARIABLE) {
		// Map variable
		pGroup->pMapVariableLabel = GERefreshLabel(pGroup->pMapVariableLabel, "Map Variable", "Value comes from this map variable.", x, 0, y, pParent);
		if (!pGroup->pMapVariableField) {
			pGroup->pMapVariableField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigVarDef, pVarDef, parse_WorldVariableDef, "MapVariable", NULL, &pGroup->eaSrcMapVariables, NULL);
			GEAddFieldToParent(pGroup->pMapVariableField, pParent, xControl, y, 0, 1.0, UIUnitPercentage, 3, changeFunc, preChangeFunc, pData);
		} else {
			ui_WidgetSetPosition(pGroup->pMapVariableField->pUIWidget, xControl, y);
			MEFieldSetAndRefreshFromData(pGroup->pMapVariableField, pOrigVarDef, pVarDef);
		}
		y += Y_OFFSET_ROW;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pMapVariableLabel);
		MEFieldSafeDestroy(&pGroup->pMapVariableField);
	}

	if (pVarDef->eDefaultType == WVARDEF_MISSION_VARIABLE) {
		MissionDef *pMissionDef;

		// Mission
		pGroup->pMissionLabel = GERefreshLabel(pGroup->pMissionLabel, "Mission", "Value comes from this mission's variables.", x, 0, y, pParent);
		if (!pGroup->pMissionField) {
			pGroup->pMissionField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, pOrigVarDef, pVarDef, parse_WorldVariableDef, "MissionRefString", "Mission", "ResourceName");
			GEAddFieldToParent(pGroup->pMissionField, pParent, xControl, y, 0, 1.0, UIUnitPercentage, 3, changeFunc, preChangeFunc, pData);
		} else {
			ui_WidgetSetPosition(pGroup->pMissionField->pUIWidget, xControl, y);
			MEFieldSetAndRefreshFromData(pGroup->pMissionField, pOrigVarDef, pVarDef);
		}
		y += Y_OFFSET_ROW;

		// Mission variable
		pGroup->pMissionVariableLabel = GERefreshLabel(pGroup->pMissionVariableLabel, "Mission Var", "Value comes from this specific mission variable.", x, 0, y, pParent);

		pMissionDef = pVarDef->mission_refstring ? missiondef_DefFromRefString(pVarDef->mission_refstring) : NULL;
		eaDestroy(&pGroup->eaMissionVariableDefs);
		if (pMissionDef)
		{
			MissionDef *pRootMissionDef = GET_REF(pMissionDef->parentDef);
			eaIndexedEnable(&pGroup->eaMissionVariableDefs, parse_WorldVariableDef);
			eaPushEArray(&pGroup->eaMissionVariableDefs, &pMissionDef->eaVariableDefs);
			if (pRootMissionDef)
				eaPushEArray(&pGroup->eaMissionVariableDefs, &pRootMissionDef->eaVariableDefs);
		}

		if (!pGroup->pMissionVariableField) {
			pGroup->pMissionVariableField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigVarDef, pVarDef, parse_WorldVariableDef, "MissionVariable", parse_WorldVariableDef, &pGroup->eaMissionVariableDefs, "Name");
			GEAddFieldToParent(pGroup->pMissionVariableField, pParent, xControl, y, 0, 1.0, UIUnitPercentage, 3, changeFunc, preChangeFunc, pData);
		} else {
			ui_WidgetSetPosition(pGroup->pMissionVariableField->pUIWidget, xControl, y);
			MEFieldSetAndRefreshFromData(pGroup->pMissionVariableField, pOrigVarDef, pVarDef);
		}
		y += Y_OFFSET_ROW;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pMissionLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pMissionVariableLabel);
		MEFieldSafeDestroy(&pGroup->pMissionField);
		MEFieldSafeDestroy(&pGroup->pMissionVariableField);
	}

	if (pVarDef->eDefaultType == WVARDEF_EXPRESSION) {
		// Expression
		pGroup->pExpressionLabel = GERefreshLabel(pGroup->pExpressionLabel, "Expression", "Value comes from this expression.", x, 0, y, pParent);
		if (!pGroup->pExpressionField) {
			pGroup->pExpressionField = MEFieldCreateSimple(kMEFieldTypeEx_Expression, pOrigVarDef, pVarDef, parse_WorldVariableDef, "Expression");
			GEAddFieldToParent(pGroup->pExpressionField, pParent, xControl, y, 0, 1.0, UIUnitPercentage, 3, changeFunc, preChangeFunc, pData);
		} else {
			ui_WidgetSetPosition(pGroup->pExpressionField->pUIWidget, xControl, y);
			MEFieldSetAndRefreshFromData(pGroup->pExpressionField, pOrigVarDef, pVarDef);
		}
		y += Y_OFFSET_ROW;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pExpressionLabel);
		MEFieldSafeDestroy(&pGroup->pExpressionField);
	}

	if (pVarDef->eDefaultType == WVARDEF_ACTIVITY_VARIABLE) {
		const char* pcLabel = NULL;
		const char* pcTooltip = NULL;
		const char* pcField = NULL;
		const char* pcDictName = NULL;
		const char* pcLabel2 = NULL;
		const char* pcTooltip2 = NULL;
		const char* pcField2 = NULL;
		const char* pcDictName2 = NULL;
		ParseTable *pTable = parse_WorldVariable;
		MEFieldType eFieldType = kMEFieldType_TextEntry;
		void *pOld = SAFE_MEMBER(pOrigVarDef, pSpecificValue), *pNew = pVarDef->pSpecificValue;

		// Activity name
		pGroup->pActivityNameLabel = GERefreshLabel(pGroup->pActivityNameLabel, "Activity", "The activity to pull the variable from.", x, 0, y, pParent);
		if (!pGroup->pActivityNameField) {
			pGroup->pActivityNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigVarDef, pVarDef, parse_WorldVariableDef, "ActivityName");
			GEAddFieldToParent(pGroup->pActivityNameField, pParent, xControl, y, 0, 1.0, UIUnitPercentage, 3, changeFunc, preChangeFunc, pData);
		} else {
			ui_WidgetSetPosition(pGroup->pActivityNameField->pUIWidget, xControl, y);
			MEFieldSetAndRefreshFromData(pGroup->pActivityNameField, pOrigVarDef, pVarDef);
		}
		y += Y_OFFSET_ROW;

		// Activity variable name
		pGroup->pActivityVariableNameLabel = GERefreshLabel(pGroup->pActivityVariableNameLabel, "Activity Var", "Value comes from this variable on an activity.", x, 0, y, pParent);
		if (!pGroup->pActivityVariableNameField) {
			pGroup->pActivityVariableNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigVarDef, pVarDef, parse_WorldVariableDef, "ActivityVariable");
			GEAddFieldToParent(pGroup->pActivityVariableNameField, pParent, xControl, y, 0, 1.0, UIUnitPercentage, 3, changeFunc, preChangeFunc, pData);
		} else {
			ui_WidgetSetPosition(pGroup->pActivityVariableNameField->pUIWidget, xControl, y);
			MEFieldSetAndRefreshFromData(pGroup->pActivityVariableNameField, pOrigVarDef, pVarDef);
		}
		y += Y_OFFSET_ROW;

		if (varType != pVarDef->eType) {
			pVarDef->eType = varType;
		}
		if (pVarDef->eType != pGroup->ePrevType) {
			pGroup->ePrevType = pVarDef->eType;
			MEFieldSafeDestroy(&pGroup->pValueField);
		}
		if (!pNew) {
			pNew = pVarDef->pSpecificValue = StructCreate(parse_WorldVariable);
		}
		pVarDef->pSpecificValue->eType = pVarDef->eType;

		// VARIABLE_TYPES: Add code below if add to the available variable types
		switch (pVarDef->eType) {
			xcase WVAR_INT:
			pcLabel = "Int Value";
			pcField = "IntVal";
		xcase WVAR_FLOAT:
			pcLabel = "Float Value";
			pcField = "FloatVal";
		xcase WVAR_STRING:
			pcLabel = "String";
			pcField = "StringVal";
		xcase WVAR_LOCATION_STRING:
			pcLabel = "Loc. String";
			pcField = "StringVal";
		xcase WVAR_ANIMATION:
			pcLabel = "Animation";
			pcField = "StringVal";
			pcDictName = "AIAnimList";
		xcase WVAR_CRITTER_DEF:
		pcLabel = "Critter Def";
			pcField = "CritterDef";
			pcDictName = "CritterDef";
		xcase WVAR_CRITTER_GROUP:
			pcLabel = "Critter Group";
			pcField = "CritterGroup";
			pcDictName = "CritterGroup";
		xcase WVAR_MESSAGE:
			pcLabel = "Message";
			pcField = "EditorCopy";
			eFieldType = kMEFieldType_Message;
			pTable = parse_DisplayMessage;
			pOld = SAFE_MEMBER_ADDR2(pOrigVarDef, pSpecificValue, messageVal);
			if (!pVarDef->pSpecificValue->messageVal.pEditorCopy) {
				pVarDef->pSpecificValue->messageVal.pEditorCopy = StructCreate(parse_Message);
			}
			pNew = &pVarDef->pSpecificValue->messageVal;
		xcase WVAR_MAP_POINT:
			pcLabel = "Default Zone";
			pcTooltip = "The default target zonemap.  If left empty, the current map.  This is selected if the Activity is not active.";
			pcField = "ZoneMap";
			pcDictName = "ZoneMap";
			pcLabel2 = "Default Spawn";
			pcTooltip2 = "The default target spawn point on the zonemap.  Can also be MissionReturn to go back to how this map was entered.  This is selected if the Activity is not active.";
			pcField2 = "StringVal";
		xcase WVAR_ITEM_DEF:
			pcLabel = "ItemDef";
			pcField = "StringVal";
			pcDictName = "ItemDef";
		xcase WVAR_MISSION_DEF:
			pcLabel = "MissionDef";
			pcField = "StringVal";
			pcDictName = "Mission";
		}

		// Value
		if (pcField) {
			pGroup->pActivityDefaultValueLabel = GERefreshLabel(pGroup->pActivityDefaultValueLabel, pcLabel, pcTooltip, x, 0, y, pParent);

			if (!pGroup->pActivityDefaultValueField && pcField) {
				pGroup->pActivityDefaultValueField = MEFieldCreateSimpleGlobalDictionary(eFieldType, pOld, pNew, pTable, pcField, pcDictName, pcDictName ? "ResourceName" : NULL);
				GEAddFieldToParent(pGroup->pActivityDefaultValueField, pParent, xControl, y, 0, 1.0, UIUnitPercentage, 3, changeFunc, preChangeFunc, pData);
			} else {
				ui_WidgetSetPosition(pGroup->pActivityDefaultValueField->pUIWidget, xControl, y);
				MEFieldSetAndRefreshFromData(pGroup->pActivityDefaultValueField, pOld, pNew);
			}
			y += Y_OFFSET_ROW;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pActivityDefaultValueLabel);
			MEFieldSafeDestroy(&pGroup->pActivityDefaultValueField);
		}

		if (pcField2) {
			pGroup->pActivityDefaultValueLabel2 = GERefreshLabel(pGroup->pActivityDefaultValueLabel2, pcLabel2, pcTooltip2, x, 0, y, pParent);

			if (!pGroup->pActivityDefaultValueField2) {
				pGroup->pActivityDefaultValueField2 = MEFieldCreateSimple(eFieldType, pOld, pNew, parse_WorldVariable, pcField2);
				GEAddFieldToParent(pGroup->pActivityDefaultValueField2, pParent, xControl, y, 0, 1.0, UIUnitPercentage, 3, changeFunc, preChangeFunc, pData);
			} else {
				ui_WidgetSetPosition(pGroup->pActivityDefaultValueField2->pUIWidget, xControl, y);
				MEFieldSetAndRefreshFromData(pGroup->pActivityDefaultValueField2, pOld, pNew);
			}
			y += Y_OFFSET_ROW;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pActivityDefaultValueLabel2);
			MEFieldSafeDestroy(&pGroup->pActivityDefaultValueField2);
		}
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pActivityNameLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pActivityVariableNameLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pActivityDefaultValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pActivityDefaultValueLabel2);
		MEFieldSafeDestroy(&pGroup->pActivityNameField);
		MEFieldSafeDestroy(&pGroup->pActivityVariableNameField);
		MEFieldSafeDestroy(&pGroup->pActivityDefaultValueField);
		MEFieldSafeDestroy(&pGroup->pActivityDefaultValueField2);
	}


	return y;

}

static void GECheckToggle(UICheckButton *pButton, GEActionGroup *pGroup) 
{
	pGroup->dataChangedCB( pGroup->pUserData, true ); 
}

static void GEIncludeTeammatesCheckToggle(UICheckButton *pButton, GEActionGroup *pGroup) 
{
	WorldGameActionProperties* pProps = (pGroup->peaActions && *pGroup->peaActions) ? (*pGroup->peaActions)[pGroup->index] : NULL;
	if (pProps && pProps->pWarpProperties)
		pProps->pWarpProperties->bIncludeTeammates = ui_CheckButtonGetState(pButton);
	pGroup->dataChangedCB( pGroup->pUserData, true ); 
}

static void GETextEntryChanged(UITextEntry *pTextEntry, GEActionGroup *pGroup)
{
	pGroup->dataChangedCB( pGroup->pUserData, true );
}

static F32 GEUpdateWarpAction(GEActionGroup *pGroup, const char *pcSrcZoneMap, WorldGameActionProperties *pAction, WorldGameActionProperties *pOrigAction, int index, F32 y)
{
	int i;
	int iNumVarDefs = 0;
	const char* overrideDestMap = NULL;

	if( pGroup->pSourceMapOverride ) {
		const char* text = ui_TextEntryGetText( pGroup->pSourceMapOverride );
		if( text && text[0]) {
			pcSrcZoneMap = text;
		}
	}
	if( pGroup->pDestMapOverride ) {
		const char* text = ui_TextEntryGetText( pGroup->pDestMapOverride );
		if( text && text[0]) {
			overrideDestMap = text;
		}
	}

	// Check data
	if (!pAction->pWarpProperties) {
		pAction->pWarpProperties = StructCreate(parse_WorldWarpActionProperties);
	}
	GECleanUpActionProperties(pAction);
	GERefreshMapNamesList();

	// The Target
	if (!pGroup->pWarpDestGroup) {
		pGroup->pWarpDestGroup = calloc( 1, sizeof( *pGroup->pWarpDestGroup ));
		pGroup->pWarpDestGroup->pData = pGroup;
	}
	y = GEUpdateVariableDefGroupNoName( pGroup->pWarpDestGroup, pGroup->pWidgetParent, &pAction->pWarpProperties->warpDest, SAFE_MEMBER_ADDR2(pOrigAction, pWarpProperties, warpDest), WVAR_MAP_POINT, pcSrcZoneMap, NULL, "Target", "How the warp destination is specified.", X_OFFSET_INDENT, X_OFFSET_CONTROL, y, GEGameActionFieldChangedCB, GEGameActionFieldPreChangeCB, pGroup);

	if ( !pGroup->pDepartSequenceOverride )
	{
		pGroup->pDepartSequenceOverride = ui_CheckButtonCreate( X_OFFSET_CONTROL, y, "Override Depart Sequence", GET_REF(pAction->pWarpProperties->hTransSequence) != NULL );
		ui_WidgetAddChild(pGroup->pWidgetParent, UI_WIDGET(pGroup->pDepartSequenceOverride));
		ui_CheckButtonSetToggledCallback(pGroup->pDepartSequenceOverride, GECheckToggle, pGroup);
	}
	else
	{
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pDepartSequenceOverride), X_OFFSET_CONTROL, y);

		if ( !ui_CheckButtonGetState(pGroup->pDepartSequenceOverride) )
		{
			REMOVE_HANDLE(pAction->pWarpProperties->hTransSequence);
		}
	}
	
	y += Y_OFFSET_ROW;

	if ( !pGroup->pTransSequenceField )
	{
		if ( ui_CheckButtonGetState(pGroup->pDepartSequenceOverride) )
		{
			pGroup->pLabel3 = GERefreshLabel(pGroup->pLabel3, "Transition", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

			pGroup->pTransSequenceField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, pOrigAction ? pOrigAction->pWarpProperties : NULL, pAction->pWarpProperties, parse_WorldWarpActionProperties, "TransitionOverride", "DoorTransitionSequenceDef", "ResourceName");

			GEGameActionAddFieldToParent(pGroup->pTransSequenceField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
		
			y += Y_OFFSET_ROW;
		}
		else
		{
			ui_WidgetQueueFreeAndNull(&pGroup->pLabel3);
		}
	}
	else
	{
		if ( ui_CheckButtonGetState(pGroup->pDepartSequenceOverride) )
		{
			pGroup->pLabel3 = GERefreshLabel(pGroup->pLabel3, "Transition", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

			ui_WidgetSetPosition(pGroup->pTransSequenceField->pUIWidget, X_OFFSET_CONTROL, y);
			
			MEFieldSetAndRefreshFromData(pGroup->pTransSequenceField, pOrigAction ? pOrigAction->pWarpProperties : NULL, pAction->pWarpProperties );
		
			y += Y_OFFSET_ROW;
		}
		else
		{
			ui_WidgetQueueFreeAndNull(&pGroup->pLabel3);
			MEFieldSafeDestroy(&pGroup->pTransSequenceField);
		}
	}

	if( !pGroup->pSpecifyMapOverride ) {
		pGroup->pSpecifyMapOverride = ui_CheckButtonCreate( X_OFFSET_CONTROL, y, "Override Maps", false );
		ui_WidgetAddChild(pGroup->pWidgetParent, UI_WIDGET(pGroup->pSpecifyMapOverride));
		ui_CheckButtonSetToggledCallback(pGroup->pSpecifyMapOverride, GECheckToggle, pGroup);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pSpecifyMapOverride), X_OFFSET_CONTROL, y);
	}

	y += Y_OFFSET_ROW;

	if( !pGroup->pIncludeTeammates ) {
		pGroup->pIncludeTeammates = ui_CheckButtonCreate( X_OFFSET_CONTROL, y, "Include Teammates", pAction->pWarpProperties->bIncludeTeammates );
		ui_WidgetAddChild(pGroup->pWidgetParent, UI_WIDGET(pGroup->pIncludeTeammates));
		ui_CheckButtonSetToggledCallback(pGroup->pIncludeTeammates, GEIncludeTeammatesCheckToggle, pGroup);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pIncludeTeammates), X_OFFSET_CONTROL, y);
	}

	y += Y_OFFSET_ROW;
	
	if( !ui_CheckButtonGetState(pGroup->pSpecifyMapOverride)) {
		pGroup->pOverrideSrcMap = NULL;
		pGroup->pOverrideDestMap = NULL;
		ui_WidgetQueueFreeAndNull( &pGroup->pLabel1 );
		ui_WidgetQueueFreeAndNull( &pGroup->pLabel2 );
	} else {
		pGroup->pLabel1 = GERefreshLabel( pGroup->pLabel1, "Source Map", "An example map to start on.", X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
		if( !pGroup->pSourceMapOverride ) {
			pGroup->pSourceMapOverride = ui_TextEntryCreateWithGlobalDictionaryCombo( "", X_OFFSET_CONTROL, y, "ZoneMap", "ResourceName", true, true, false, true );
			ui_TextEntrySetChangedCallback( pGroup->pSourceMapOverride, GETextEntryChanged, pGroup );
			ui_WidgetAddChild(pGroup->pWidgetParent, UI_WIDGET(pGroup->pSourceMapOverride));
			ui_WidgetSetWidthEx(UI_WIDGET(pGroup->pSourceMapOverride), 1.0, UIUnitPercentage);
		} else {
			ui_WidgetSetPosition( UI_WIDGET(pGroup->pSourceMapOverride), X_OFFSET_CONTROL, y );
		} 
		y += Y_OFFSET_ROW;
		
		pGroup->pLabel2 = GERefreshLabel( pGroup->pLabel2, "Dest Map", "An example map to go to.", X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
		if( !pGroup->pDestMapOverride ) {
			pGroup->pDestMapOverride = ui_TextEntryCreateWithGlobalDictionaryCombo( "", X_OFFSET_CONTROL, y, "ZoneMap", "ResourceName", true, true, false, true );
			ui_TextEntrySetChangedCallback( pGroup->pDestMapOverride, GETextEntryChanged, pGroup );
			ui_WidgetAddChild(pGroup->pWidgetParent, UI_WIDGET(pGroup->pDestMapOverride));
			ui_WidgetSetWidthEx(UI_WIDGET(pGroup->pDestMapOverride), 1.0, UIUnitPercentage);
		} else {
			ui_WidgetSetPosition( UI_WIDGET(pGroup->pDestMapOverride), X_OFFSET_CONTROL, y );
		}
		y += Y_OFFSET_ROW;
	}

	{
		WorldVariable* pDest = wleAEWorldVariableCalcVariableNonRandom(SAFE_MEMBER_ADDR(pAction->pWarpProperties, warpDest));

		const char* pcDestZoneMap;

		if( overrideDestMap ) {
			pcDestZoneMap = overrideDestMap;
		} else if(pDest && pDest->eType == WVAR_MAP_POINT && pDest->pcZoneMap && pDest->pcZoneMap[0]) {
			pcDestZoneMap = pDest->pcZoneMap;
		} else {
			pcDestZoneMap = NULL;
		}

		iNumVarDefs = eaSize(&pAction->pWarpProperties->eaVariableDefs);

		// Refresh variable groups
		for(i=0; i<iNumVarDefs; ++i) {
			WorldVariableDef *pVarDef = pAction->pWarpProperties->eaVariableDefs[i];
			WorldVariableDef *pOrigVarDef = NULL;

			if (pOrigAction && pOrigAction->pWarpProperties && (i < eaSize(&pOrigAction->pWarpProperties->eaVariableDefs))) {
				pOrigVarDef = pOrigAction->pWarpProperties->eaVariableDefs[i];
			}
			if (i >= eaSize(&pGroup->eaVariableDefGroups)) {
				GEVariableDefGroup *pVarDefGroup = calloc(1, sizeof(GEVariableDefGroup));
				pVarDefGroup->pData = pGroup;
				eaPush(&pGroup->eaVariableDefGroups, pVarDefGroup);
			}

			y = GEUpdateVariableDefGroup(pGroup->eaVariableDefGroups[i], pGroup->pWidgetParent, &pAction->pWarpProperties->eaVariableDefs, pVarDef, pOrigVarDef, pcSrcZoneMap, NULL, pcDestZoneMap, i, X_OFFSET_INDENT, X_OFFSET_CONTROL, y, GEGameActionFieldChangedCB, GEGameActionFieldPreChangeCB, pGroup);
		}
		
		if (pcDestZoneMap) {
			// Add button
			if (!pGroup->pVarAddButton) {
				pGroup->pVarAddButton = ui_ButtonCreate("Set Variable", X_OFFSET_INDENT, y, GEActionAddVariableDef, pGroup);
				ui_WidgetAddChild(pGroup->pWidgetParent, UI_WIDGET(pGroup->pVarAddButton));
			} else {
				ui_WidgetSetPosition(UI_WIDGET(pGroup->pVarAddButton), X_OFFSET_INDENT, y);
			}

			y += Y_OFFSET_ROW;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pVarAddButton);
		}
	} 
	
	// Free unusued groups
	for(i=eaSize(&pGroup->eaVariableDefGroups)-1; i>=iNumVarDefs; --i) {
		GEFreeVariableDefGroup(pGroup->eaVariableDefGroups[i]);
		eaRemove(&pGroup->eaVariableDefGroups, i);
	}

	// Clean up others
	GECleanupActionGroup(pGroup, pAction);

	return y;
}

static F32 GEUpdateGiveDoorKeyItemAction(GEActionGroup *pGroup, const char *pcSrcZoneMap, WorldGameActionProperties *pAction, WorldGameActionProperties *pOrigAction, int index, F32 y)
{
	int i;
	int iNumVarDefs = 0;

	// Check data
	if (!pAction->pGiveDoorKeyItemProperties) {
		pAction->pGiveDoorKeyItemProperties = StructCreate(parse_WorldGiveDoorKeyItemActionProperties);
	}
	if (!pAction->pGiveDoorKeyItemProperties->pDestinationMap) {
		pAction->pGiveDoorKeyItemProperties->pDestinationMap = StructCreate(parse_WorldVariableDef);
	}

	GECleanUpActionProperties(pAction);
	GERefreshMapNamesList();
	
	// The item
	pGroup->pLabel1 = GERefreshLabel(pGroup->pLabel1, "Item", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
	if (!pGroup->pItemField) {
		pGroup->pItemField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOrigAction ? pOrigAction->pGiveDoorKeyItemProperties : NULL, pAction->pGiveDoorKeyItemProperties, parse_WorldGiveDoorKeyItemActionProperties, "ItemDef", "ItemDef", "ResourceName");
		GEGameActionAddFieldToParent(pGroup->pItemField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
		ui_TextEntryComboValidate(pGroup->pItemField->pUIText);
	} else {
		ui_WidgetSetPosition(pGroup->pItemField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pItemField, pOrigAction ? pOrigAction->pGiveDoorKeyItemProperties : NULL, pAction->pGiveDoorKeyItemProperties);
	}

	y += Y_OFFSET_ROW;

	// The door key
	pGroup->pLabel2 = GERefreshLabel(pGroup->pLabel2, "Door Key", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
	if (!pGroup->pDoorKeyField) {
		pGroup->pDoorKeyField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigAction ? pOrigAction->pGiveDoorKeyItemProperties : NULL, pAction->pGiveDoorKeyItemProperties, parse_WorldGiveDoorKeyItemActionProperties, "DoorKey");
		GEGameActionAddFieldToParent(pGroup->pDoorKeyField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
	} else {
		ui_WidgetSetPosition(pGroup->pDoorKeyField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pDoorKeyField, pOrigAction ? pOrigAction->pGiveDoorKeyItemProperties : NULL, pAction->pGiveDoorKeyItemProperties);
	}

	y += Y_OFFSET_ROW;

	// The Target Map
	if (!pGroup->pDoorKeyDestGroup) {
		pGroup->pDoorKeyDestGroup = calloc( 1, sizeof( *pGroup->pDoorKeyDestGroup ));
		pGroup->pDoorKeyDestGroup->pData = pGroup;
	}
	y = GEUpdateVariableDefGroupNoName( pGroup->pDoorKeyDestGroup, pGroup->pWidgetParent, pAction->pGiveDoorKeyItemProperties->pDestinationMap, SAFE_MEMBER2(pOrigAction, pGiveDoorKeyItemProperties, pDestinationMap), WVAR_MAP_POINT, pcSrcZoneMap, NULL, "Destination", "How the destination map is specified.", X_OFFSET_INDENT, X_OFFSET_CONTROL, y, GEGameActionFieldChangedCB, GEGameActionFieldPreChangeCB, pGroup);

	// The Map Variables
	{
		WorldVariable* pDest = wleAEWorldVariableCalcVariableNonRandom(SAFE_MEMBER(pAction->pGiveDoorKeyItemProperties, pDestinationMap));

		const char* pcDestZoneMap;

		if(pDest && pDest->eType == WVAR_MAP_POINT && pDest->pcZoneMap && pDest->pcZoneMap[0]) {
			pcDestZoneMap = pDest->pcZoneMap;
		} else {
			pcDestZoneMap = NULL;
		}

		iNumVarDefs = eaSize(&pAction->pGiveDoorKeyItemProperties->eaVariableDefs);

		// Refresh variable groups
		for(i=0; i<iNumVarDefs; ++i) {
			WorldVariableDef *pVarDef = pAction->pGiveDoorKeyItemProperties->eaVariableDefs[i];
			WorldVariableDef *pOrigVarDef = NULL;

			if (pOrigAction && pOrigAction->pGiveDoorKeyItemProperties && (i < eaSize(&pOrigAction->pGiveDoorKeyItemProperties->eaVariableDefs))) {
				pOrigVarDef = pOrigAction->pGiveDoorKeyItemProperties->eaVariableDefs[i];
			}
			if (i >= eaSize(&pGroup->eaVariableDefGroups)) {
				GEVariableDefGroup *pVarDefGroup = calloc(1, sizeof(GEVariableDefGroup));
				pVarDefGroup->pData = pGroup;
				eaPush(&pGroup->eaVariableDefGroups, pVarDefGroup);
			}

			y = GEUpdateVariableDefGroup(pGroup->eaVariableDefGroups[i], pGroup->pWidgetParent, &pAction->pGiveDoorKeyItemProperties->eaVariableDefs, pVarDef, pOrigVarDef, pcSrcZoneMap, NULL, pcDestZoneMap, i, X_OFFSET_INDENT, X_OFFSET_CONTROL, y, GEGameActionFieldChangedCB, GEGameActionFieldPreChangeCB, pGroup);
		}

		if (pcDestZoneMap) {
			// Add button
			if (!pGroup->pVarAddButton) {
				pGroup->pVarAddButton = ui_ButtonCreate("Add Variable", X_OFFSET_INDENT, y, GEActionAddDoorKeyVariableDef, pGroup);
				ui_WidgetAddChild(pGroup->pWidgetParent, UI_WIDGET(pGroup->pVarAddButton));
			} else {
				ui_WidgetSetPosition(UI_WIDGET(pGroup->pVarAddButton), X_OFFSET_INDENT, y);
			}

			y += Y_OFFSET_ROW;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pVarAddButton);
		}
	} 

	// Free unusued groups
	for(i=eaSize(&pGroup->eaVariableDefGroups)-1; i>=iNumVarDefs; --i) {
		GEFreeVariableDefGroup(pGroup->eaVariableDefGroups[i]);
		eaRemove(&pGroup->eaVariableDefGroups, i);
	}

	// Clean up others
	GECleanupActionGroup(pGroup, pAction);

	return y;
}


static F32 GEUpdateContactAction(GEActionGroup *pGroup, WorldGameActionProperties *pAction, WorldGameActionProperties *pOrigAction, int index, F32 y)
{
	// Check data
	if (!pAction->pContactProperties) {
		pAction->pContactProperties = StructCreate(parse_WorldContactActionProperties);
	}
	GECleanUpActionProperties(pAction);

	// The label
	pGroup->pLabel1 = GERefreshLabel(pGroup->pLabel1, "Contact", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	if (!pGroup->pContactField) {
		pGroup->pContactField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOrigAction ? pOrigAction->pContactProperties : NULL, pAction->pContactProperties, parse_WorldContactActionProperties, "ContactDef", "Contact", "ResourceName");
		GEGameActionAddFieldToParent(pGroup->pContactField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
	} else {
		ui_WidgetSetPosition(pGroup->pContactField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pContactField, pOrigAction ? pOrigAction->pContactProperties : NULL, pAction->pContactProperties);
	}

	y += Y_OFFSET_ROW;

	// The label
	pGroup->pLabel2 = GERefreshLabel(pGroup->pLabel2, "Dialog Name", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	if (!pGroup->pNameField) {
		pGroup->pNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigAction ? pOrigAction->pContactProperties : NULL, pAction->pContactProperties, parse_WorldContactActionProperties, "DialogName");
		GEGameActionAddFieldToParent(pGroup->pNameField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
	} else {
		ui_WidgetSetPosition(pGroup->pNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pNameField, pOrigAction ? pOrigAction->pContactProperties : NULL, pAction->pContactProperties);
	}

	y += Y_OFFSET_ROW;

	// Clean up others
	GECleanupActionGroup(pGroup, pAction);

	return y;
}

static F32 GEUpdateExpressionAction(GEActionGroup *pGroup, WorldGameActionProperties *pAction, WorldGameActionProperties *pOrigAction, int index, F32 y)
{
	// Check data
	if (!pAction->pExpressionProperties) {
		pAction->pExpressionProperties = StructCreate(parse_WorldExpressionActionProperties);
	}
	GECleanUpActionProperties(pAction);

	// The label
	pGroup->pLabel1 = GERefreshLabel(pGroup->pLabel1, "Expression", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	if (!pGroup->pExpressionField) {
		pGroup->pExpressionField = MEFieldCreateSimple(kMEFieldTypeEx_Expression, pOrigAction ? pOrigAction->pExpressionProperties : NULL, pAction->pExpressionProperties, parse_WorldExpressionActionProperties, "ExprBlock");
		GEGameActionAddFieldToParent(pGroup->pExpressionField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
	} else {
		ui_WidgetSetPosition(pGroup->pExpressionField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pExpressionField, pOrigAction ? pOrigAction->pExpressionProperties : NULL, pAction->pExpressionProperties);
	}

	y += Y_OFFSET_ROW;

	// Clean up others
	GECleanupActionGroup(pGroup, pAction);

	return y;
}

static F32 GEUpdateChangeNemesisStateAction(GEActionGroup *pGroup, WorldGameActionProperties *pAction, WorldGameActionProperties *pOrigAction, int index, F32 y)
{
	// Check data
	if (!pAction->pNemesisStateProperties) {
		pAction->pNemesisStateProperties = StructCreate(parse_WorldChangeNemesisStateActionProperties);
	}
	GECleanUpActionProperties(pAction);

	// The label
	pGroup->pLabel1 = GERefreshLabel(pGroup->pLabel1, "New State", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	// The nemesis state combo
	if (!pGroup->pNemesisStateField) {
		pGroup->pNemesisStateField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigAction ? pOrigAction->pNemesisStateProperties : NULL, pAction->pNemesisStateProperties, parse_WorldChangeNemesisStateActionProperties, "NewNemesisState", NemesisStateEnum);
		GEGameActionAddFieldToParent(pGroup->pNemesisStateField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
	} else {
		ui_WidgetSetPosition(pGroup->pNemesisStateField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pNemesisStateField, pOrigAction ? pOrigAction->pNemesisStateProperties : NULL, pAction->pNemesisStateProperties);
	}

	y += Y_OFFSET_ROW;

	// Clean up others
	GECleanupActionGroup(pGroup, pAction);

	return y;
}

static F32 GEUpdateGuildStatUpdateAction(GEActionGroup *pGroup, WorldGameActionProperties *pAction, WorldGameActionProperties *pOrigAction, int index, F32 y)
{
	// Check data
	if (!pAction->pGuildStatUpdateProperties) 
	{
		pAction->pGuildStatUpdateProperties = StructCreate(parse_WorldGuildStatUpdateActionProperties);
	}
	GECleanUpActionProperties(pAction);

	// The stat name
	pGroup->pLabel1 = GERefreshLabel(pGroup->pLabel1, "Stat Name", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	if (!pGroup->pGuildStatNameField) 
	{
		pGroup->pGuildStatNameField = MEFieldCreateSimpleDictionary(kMEFieldType_TextEntry, pOrigAction ? pOrigAction->pGuildStatUpdateProperties : NULL, pAction->pGuildStatUpdateProperties, parse_WorldGuildStatUpdateActionProperties, "StatName", "GuildStatDef", parse_GuildStatDef, "Name");
		GEGameActionAddFieldToParent(pGroup->pGuildStatNameField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
	} 
	else 
	{
		ui_WidgetSetPosition(pGroup->pGuildStatNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pGuildStatNameField, pOrigAction ? pOrigAction->pGuildStatUpdateProperties : NULL, pAction->pGuildStatUpdateProperties);
	}

	y += Y_OFFSET_ROW;

	// The type of operation to perform
	pGroup->pLabel2 = GERefreshLabel(pGroup->pLabel2, "Operation", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	if (!pGroup->pGuildOperationTypeField) 
	{
		pGroup->pGuildOperationTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigAction ? pOrigAction->pGuildStatUpdateProperties : NULL, pAction->pGuildStatUpdateProperties, parse_WorldGuildStatUpdateActionProperties, "Operation", GuildStatUpdateOperationEnum);
		GEGameActionAddFieldToParent(pGroup->pGuildOperationTypeField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
	} 
	else 
	{
		ui_WidgetSetPosition(pGroup->pGuildOperationTypeField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pGuildOperationTypeField, pOrigAction ? pOrigAction->pGuildStatUpdateProperties : NULL, pAction->pGuildStatUpdateProperties);
	}

	y += Y_OFFSET_ROW;

	// The value for the operation
	if (pAction->pGuildStatUpdateProperties->eOperation != GuildStatUpdateOperation_None)
	{
		const char *pchOperationValueLabel = NULL;
		switch (pAction->pGuildStatUpdateProperties->eOperation)
		{
		case GuildStatUpdateOperation_Add:
			pchOperationValueLabel = "Inc. Value";
			break;
		case GuildStatUpdateOperation_Subtract:
			pchOperationValueLabel = "Dec. Value";
			break;
		case GuildStatUpdateOperation_Max:
			pchOperationValueLabel = "Max Value";
			break;
		case GuildStatUpdateOperation_Min:
			pchOperationValueLabel = "Min Value";
			break;
		}
		if (pchOperationValueLabel)
		{
			pGroup->pLabel3 = GERefreshLabel(pGroup->pLabel3, pchOperationValueLabel, NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

			if (!pGroup->pGuildOperationValueField) 
			{
				pGroup->pGuildOperationValueField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigAction ? pOrigAction->pGuildStatUpdateProperties : NULL, pAction->pGuildStatUpdateProperties, parse_WorldGuildStatUpdateActionProperties, "Value");
				GEGameActionAddFieldToParent(pGroup->pGuildOperationValueField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 0, pGroup);
			} else {
				ui_WidgetSetPosition(pGroup->pGuildOperationValueField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pGuildOperationValueField, pOrigAction ? pOrigAction->pGuildStatUpdateProperties : NULL, pAction->pGuildStatUpdateProperties);
			}

			y += Y_OFFSET_ROW;
		}
		else
		{
			ui_WidgetQueueFreeAndNull(&pGroup->pLabel3);
			MEFieldSafeDestroy(&pGroup->pGuildOperationValueField);
		}
	}
	else
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pLabel3);
		MEFieldSafeDestroy(&pGroup->pGuildOperationValueField);
	}

	// Clean up others
	GECleanupActionGroup(pGroup, pAction);

	return y;
}

static F32 GEUpdateGuildThemeSetAction(GEActionGroup *pGroup, WorldGameActionProperties *pAction, WorldGameActionProperties *pOrigAction, int index, F32 y)
{
	// Check data
	if (!pAction->pGuildThemeSetProperties) 
	{
		pAction->pGuildThemeSetProperties = StructCreate(parse_WorldGuildThemeSetActionProperties);
	}
	GECleanUpActionProperties(pAction);

	// The stat name
	pGroup->pLabel1 = GERefreshLabel(pGroup->pLabel1, "Theme", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	if (!pGroup->pGuildThemeNameField) 
	{
		pGroup->pGuildThemeNameField = MEFieldCreateSimpleDictionary(kMEFieldType_TextEntry, pOrigAction ? pOrigAction->pGuildThemeSetProperties : NULL, pAction->pGuildThemeSetProperties, parse_WorldGuildThemeSetActionProperties, "ThemeName", "GuildThemeDef", parse_GuildThemeDef, "Name");
		GEGameActionAddFieldToParent(pGroup->pGuildThemeNameField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
	} 
	else 
	{
		ui_WidgetSetPosition(pGroup->pGuildThemeNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pGuildThemeNameField, pOrigAction ? pOrigAction->pGuildThemeSetProperties : NULL, pAction->pGuildThemeSetProperties);
	}

	y += Y_OFFSET_ROW;

	// Clean up others
	GECleanupActionGroup(pGroup, pAction);

	return y;
}

static F32 GEUpdateItemAssignmentAction(GEActionGroup *pGroup, WorldGameActionProperties *pAction, WorldGameActionProperties *pOrigAction, int index, F32 y)
{
	// Check data
	if (!pAction->pItemAssignmentProperties) 
	{
		pAction->pItemAssignmentProperties = StructCreate(parse_WorldItemAssignmentActionProperties);
	}
	GECleanUpActionProperties(pAction);

	// The stat name
	pGroup->pLabel1 = GERefreshLabel(pGroup->pLabel1, "Assignment Name", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	if (!pGroup->pItemAssignmentNameField) 
	{
		pGroup->pItemAssignmentNameField = MEFieldCreateSimpleDictionary(kMEFieldType_TextEntry, pOrigAction ? pOrigAction->pItemAssignmentProperties : NULL, pAction->pItemAssignmentProperties, parse_WorldItemAssignmentActionProperties, "AssignmentName", "ItemAssignmentDef", parse_ItemAssignmentDef, "Name");
		GEGameActionAddFieldToParent(pGroup->pItemAssignmentNameField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
	} 
	else 
	{
		ui_WidgetSetPosition(pGroup->pItemAssignmentNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pItemAssignmentNameField, pOrigAction ? pOrigAction->pItemAssignmentProperties : NULL, pAction->pItemAssignmentProperties);
	}

	y += Y_OFFSET_ROW;

	// The type of operation to perform
	pGroup->pLabel2 = GERefreshLabel(pGroup->pLabel2, "Operation", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	if (!pGroup->pItemAssignmentOperationField) 
	{
		pGroup->pItemAssignmentOperationField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigAction ? pOrigAction->pItemAssignmentProperties : NULL, pAction->pItemAssignmentProperties, parse_WorldItemAssignmentActionProperties, "Operation", ItemAssignmentOperationEnum);
		GEGameActionAddFieldToParent(pGroup->pItemAssignmentOperationField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
	} 
	else 
	{
		ui_WidgetSetPosition(pGroup->pItemAssignmentOperationField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pItemAssignmentOperationField, pOrigAction ? pOrigAction->pItemAssignmentProperties : NULL, pAction->pItemAssignmentProperties);
	}

	y += Y_OFFSET_ROW;

	// Clean up others
	GECleanupActionGroup(pGroup, pAction);

	return y;
}

static F32 GEUpdateShardVariableAction(GEActionGroup *pGroup, WorldGameActionProperties *pAction, WorldGameActionProperties *pOrigAction, int index, F32 y)
{
	const WorldVariable *pDefaultVar;

	// Check data
	if (!pAction->pShardVariableProperties) {
		pAction->pShardVariableProperties = StructCreate(parse_WorldShardVariableActionProperties);
	}
	GECleanUpActionProperties(pAction);

	// The variable name
	pGroup->pLabel1 = GERefreshLabel(pGroup->pLabel1, "Variable Name", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	if (!pGroup->pVarNameField) {
		pGroup->pVarNameField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigAction ? pOrigAction->pShardVariableProperties : NULL, pAction->pShardVariableProperties, parse_WorldShardVariableActionProperties, "VariableName", NULL, shardvariable_GetShardVariableNames(), NULL);
		GEGameActionAddFieldToParent(pGroup->pVarNameField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
	} else {
		ui_WidgetSetPosition(pGroup->pVarNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pVarNameField, pOrigAction ? pOrigAction->pShardVariableProperties : NULL, pAction->pShardVariableProperties);
	}

	y += Y_OFFSET_ROW;

	pGroup->pLabel2 = GERefreshLabel(pGroup->pLabel2, "Variable Type", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
	pDefaultVar = shardvariable_GetDefaultValue(pAction->pShardVariableProperties->pcVarName);
	pGroup->pLabel3 = GERefreshLabel(pGroup->pLabel3, pDefaultVar ? worldVariableTypeToString(pDefaultVar->eType) : "", NULL, X_OFFSET_CONTROL, 0, y, pGroup->pWidgetParent);

	y += Y_OFFSET_ROW;

	// The type of modification to perform
	pGroup->pLabel4 = GERefreshLabel(pGroup->pLabel4, "Modify Type", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	if (!pGroup->pModifyTypeField) {
		pGroup->pModifyTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigAction ? pOrigAction->pShardVariableProperties : NULL, pAction->pShardVariableProperties, parse_WorldShardVariableActionProperties, "ModifyType", WorldVariableActionTypeEnum);
		GEGameActionAddFieldToParent(pGroup->pModifyTypeField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
	} else {
		ui_WidgetSetPosition(pGroup->pModifyTypeField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pModifyTypeField, pOrigAction ? pOrigAction->pShardVariableProperties : NULL, pAction->pShardVariableProperties);
	}

	y += Y_OFFSET_ROW;

	if (pAction->pShardVariableProperties->eModifyType == WorldVariableActionType_Set) {
		if (!pAction->pShardVariableProperties->pVarValue) {
			pAction->pShardVariableProperties->pVarValue = StructCreate(parse_WorldVariable);
		}
		pAction->pShardVariableProperties->pVarValue->pcName = pAction->pShardVariableProperties->pcVarName;
		pAction->pShardVariableProperties->pVarValue->eType = pDefaultVar ? pDefaultVar->eType : WVAR_NONE;
		if (!pGroup->pShardVarGroup) {
			pGroup->pShardVarGroup = calloc(1,sizeof(GEVariableGroup));
			pGroup->pShardVarGroup->pData = pGroup;
			pGroup->pShardVarGroup->changeFunc = GEGameActionFieldChangedCB;
			pGroup->pShardVarGroup->preChangeFunc = GEGameActionFieldPreChangeCB;
		}
		y = GEUpdateVariableGroupValue(pGroup->pShardVarGroup, pGroup->pWidgetParent, pAction->pShardVariableProperties->pVarValue, pOrigAction && pOrigAction->pShardVariableProperties ? pOrigAction->pShardVariableProperties->pVarValue : NULL, pAction->pShardVariableProperties->pVarValue->eType, y, GEGameActionFieldChangedCB, GEGameActionFieldPreChangeCB, pGroup);

		ui_WidgetQueueFreeAndNull(&pGroup->pLabel5);
	} else if (pAction->pShardVariableProperties->eModifyType == WorldVariableActionType_IntIncrement) {
		// Value
		pGroup->pLabel5 = GERefreshLabel(pGroup->pLabel5, "Increment", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
		if (!pGroup->pIntIncrementField) {
			pGroup->pIntIncrementField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigAction ? pOrigAction->pShardVariableProperties : NULL, pAction->pShardVariableProperties, parse_WorldShardVariableActionProperties, "IntIncrement");
			GEGameActionAddFieldToParent(pGroup->pIntIncrementField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 0, pGroup);
		} else {
			ui_WidgetSetPosition(pGroup->pIntIncrementField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pIntIncrementField, pOrigAction ? pOrigAction->pShardVariableProperties : NULL, pAction->pShardVariableProperties);
		}

		y += Y_OFFSET_ROW;

		MEFieldSafeDestroy(&pGroup->pFloatIncrementField);
		if (pGroup->pShardVarGroup) {
			GEFreeVariableGroup(pGroup->pShardVarGroup);
			pGroup->pShardVarGroup = NULL;
		}
	} else if (pAction->pShardVariableProperties->eModifyType == WorldVariableActionType_FloatIncrement) {
		// Value
		pGroup->pLabel5 = GERefreshLabel(pGroup->pLabel5, "Increment", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);
		if (!pGroup->pFloatIncrementField) {
			pGroup->pFloatIncrementField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigAction ? pOrigAction->pShardVariableProperties : NULL, pAction->pShardVariableProperties, parse_WorldShardVariableActionProperties, "FloatIncrement");
			GEGameActionAddFieldToParent(pGroup->pFloatIncrementField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 0, pGroup);
		} else {
			ui_WidgetSetPosition(pGroup->pFloatIncrementField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pFloatIncrementField, pOrigAction ? pOrigAction->pShardVariableProperties : NULL, pAction->pShardVariableProperties);
		}

		y += Y_OFFSET_ROW;

		MEFieldSafeDestroy(&pGroup->pIntIncrementField);
		if (pGroup->pShardVarGroup) {
			GEFreeVariableGroup(pGroup->pShardVarGroup);
			pGroup->pShardVarGroup = NULL;
		}
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pLabel5);
		MEFieldSafeDestroy(&pGroup->pIntIncrementField);
		MEFieldSafeDestroy(&pGroup->pFloatIncrementField);
		if (pGroup->pShardVarGroup) {
			GEFreeVariableGroup(pGroup->pShardVarGroup);
			pGroup->pShardVarGroup = NULL;
		}
	}

	// Clean up others
	GECleanupActionGroup(pGroup, pAction);

	return y;
}

static F32 GEUpdateActivityLogAction(GEActionGroup *pGroup, WorldGameActionProperties *pAction, WorldGameActionProperties *pOrigAction, int index, F32 y)
{
	DisplayMessage *pArgMsg;
	DisplayMessage *pArgMsgOrig = NULL;

	// Check data
	if (!pAction->pActivityLogProperties) {
		pAction->pActivityLogProperties = StructCreate(parse_WorldActivityLogActionProperties);
	}
	GECleanUpActionProperties(pAction);

	// The variable name
	pGroup->pLabel1 = GERefreshLabel(pGroup->pLabel1, "Entry Type", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	if (!pGroup->pActivityLogTypeField) {		
		pGroup->pActivityLogTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigAction ? pOrigAction->pActivityLogProperties : NULL, pAction->pActivityLogProperties, parse_WorldActivityLogActionProperties, "EntryType", ActivityLogEntryTypeEnum);
		GEGameActionAddFieldToParent(pGroup->pActivityLogTypeField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
	} else {
		ui_WidgetSetPosition(pGroup->pActivityLogTypeField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pActivityLogTypeField, pOrigAction ? pOrigAction->pActivityLogProperties : NULL, pAction->pActivityLogProperties);
	}

	y += Y_OFFSET_ROW;

	// The label
	pGroup->pLabel2 = GERefreshLabel(pGroup->pLabel2, "ArgString", NULL, X_OFFSET_INDENT, 0, y, pGroup->pWidgetParent);

	// Message
	pArgMsg = &pAction->pActivityLogProperties->dArgString;

	if (pOrigAction && (pOrigAction->eActionType == pAction->eActionType)) {
		pArgMsgOrig = &pOrigAction->pActivityLogProperties->dArgString;
	}
	if (pArgMsg) {
		if (!pGroup->pMessageField) {
			pGroup->pMessageField = MEFieldCreateSimple(kMEFieldType_Message, pArgMsgOrig, pArgMsg, parse_DisplayMessage, "EditorCopy");
			GEGameActionAddFieldToParent(pGroup->pMessageField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup);
		} else {
			ui_WidgetSetPosition(pGroup->pMessageField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMessageField, pArgMsgOrig, pArgMsg);
		}
	}

	y += Y_OFFSET_ROW;

	// Clean up others
	GECleanupActionGroup(pGroup, pAction);

	return y;
}

F32 GEUpdateAction(GEActionGroup *pGroup, char *pcText, char* pcSrcZoneMap, WorldGameActionProperties ***peaActions, WorldGameActionProperties ***peaOrigActions, F32 y, int index)
{
	WorldGameActionProperties *pAction;
	WorldGameActionProperties *pOrigAction = NULL;

	pGroup->peaActions = peaActions;
	pGroup->peaOrigActions = peaOrigActions;
	pGroup->index = index;

	// Calculate pointers
	pAction = (*peaActions)[index];
	if (peaOrigActions && (index < eaSize(peaOrigActions))) {
		pOrigAction = (*peaOrigActions)[index];
	}

	if (!pGroup->pSeparator) {
		pGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetAddChild(pGroup->pWidgetParent, UI_WIDGET(pGroup->pSeparator));
	}
	pGroup->pSeparator->widget.y = y;

	y += 8;

	// Action type chooser
	pGroup->pMainLabel = GERefreshLabel(pGroup->pMainLabel, pcText, NULL, X_OFFSET_BASE, 0, y, pGroup->pWidgetParent);
	if (!pGroup->pTypeField) {
		pGroup->pTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigAction, pAction, parse_WorldGameActionProperties, "ActionType", WorldGameActionTypeEnum);
		GEGameActionAddFieldToParent(pGroup->pTypeField, pGroup->pWidgetParent, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 95, pGroup);
	} else {
		ui_WidgetSetPosition(pGroup->pTypeField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pTypeField, pOrigAction, pAction);
	}

	// Remove button
	if (!pGroup->pRemoveButton) {
		pGroup->pRemoveButton = ui_ButtonCreate("Remove", 0, y, GERemoveActionCB, pGroup);
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 5, y, 0, 0, UITopRight);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pRemoveButton), 80);
		ui_WidgetAddChild(pGroup->pWidgetParent, UI_WIDGET(pGroup->pRemoveButton));
	} else {
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 5, y, 0, 0, UITopRight);
	}

	y += Y_OFFSET_ROW;

	switch(pAction->eActionType) 
	{
		xcase WorldGameActionType_GrantMission:
			y = GEUpdateGrantMissionAction(pGroup, pcSrcZoneMap, pAction, pOrigAction, y);

		xcase WorldGameActionType_GrantSubMission:
			y = GEUpdateGrantSubMissionAction(pGroup, pAction, pOrigAction, y);

		xcase WorldGameActionType_MissionOffer:
			y = GEUpdateMissionOfferAction(pGroup, pcSrcZoneMap, pAction, pOrigAction, y);

		xcase WorldGameActionType_DropMission:
			y = GEUpdateDropMissionAction(pGroup, pAction, pOrigAction, y);

		xcase WorldGameActionType_TakeItem:
			y = GEUpdateTakeItemAction(pGroup, pAction, pOrigAction, y);

		xcase WorldGameActionType_GiveItem:
			y = GEUpdateGiveItemAction(pGroup, pAction, pOrigAction, y);

		xcase WorldGameActionType_SendFloaterMsg:
			y = GEUpdateSendFloaterAction(pGroup, pAction, pOrigAction, index, y);

		xcase WorldGameActionType_SendNotification:
			y = GEUpdateSendNotificationAction(pGroup, pAction, pOrigAction, index, y);

		xcase WorldGameActionType_Warp:
			y = GEUpdateWarpAction(pGroup, pcSrcZoneMap, pAction, pOrigAction, index, y);

		xcase WorldGameActionType_Contact:
			y = GEUpdateContactAction(pGroup, pAction, pOrigAction, index, y);

		xcase WorldGameActionType_Expression:
			y = GEUpdateExpressionAction(pGroup, pAction, pOrigAction, index, y);

		xcase WorldGameActionType_ChangeNemesisState:
			y = GEUpdateChangeNemesisStateAction(pGroup, pAction, pOrigAction, index, y);

		xcase WorldGameActionType_NPCSendMail:
			y = GEUpdateNPCSendEmailAction(pGroup, pAction, pOrigAction, y);
		
		xcase WorldGameActionType_GADAttribValue:
			y = GEUpdateGADAttribValueAction(pGroup, pAction, pOrigAction, y);

		xcase WorldGameActionType_ShardVariable:
			y = GEUpdateShardVariableAction(pGroup, pAction, pOrigAction, index, y);

		xcase WorldGameActionType_ActivityLog:
			y = GEUpdateActivityLogAction(pGroup, pAction, pOrigAction, index, y);

		xcase WorldGameActionType_GiveDoorKeyItem:
			y = GEUpdateGiveDoorKeyItemAction(pGroup, pcSrcZoneMap, pAction, pOrigAction, index, y);

		xcase WorldGameActionType_GuildStatUpdate:
			y = GEUpdateGuildStatUpdateAction(pGroup, pAction, pOrigAction, index, y);

		xcase WorldGameActionType_GuildThemeSet:
			y = GEUpdateGuildThemeSetAction(pGroup, pAction, pOrigAction, index, y);

		xcase WorldGameActionType_UpdateItemAssignment:
			y = GEUpdateItemAssignmentAction(pGroup, pAction, pOrigAction, index, y);

		xdefault:
			Alertf("Unsupported action type.  Have a programmer get this fixed.");
	}

	return y;
}



// ----------------------------------------------------------------------------------
// Lists of objects to populate combo boxes
// ----------------------------------------------------------------------------------

void GERefreshEncounterList(const char*** peaList)
{
	eaClear((char***)peaList);

	// Find all encounters
	worldGetObjectNames(WL_ENC_ENCOUNTER, peaList, NULL);
}

void GERefreshActorList(SA_PARAM_NN_VALID const char*** peaList)
{
	ZoneMap *zmap = worldGetPrimaryMap();
	WorldZoneMapScope *pScope = zmapGetScope(zmap);
	int i;

	// Iterate all the encounters that are loaded for editing
	// because we can only get properties if it's loaded for editing
	if (pScope) {
		StashTableIterator iter;
		StashElement elem;

		stashGetIterator(pScope->scope.name_to_obj, &iter);
		while (stashGetNextElement(&iter, &elem))
		{
			WorldEncounterObject *obj = stashElementGetPointer(elem);
			if (obj && obj->type == WL_ENC_ENCOUNTER && obj->tracker && obj->tracker->def) {
				WorldEncounterProperties *pProps = obj->tracker->def->property_structs.encounter_properties;
				if (pProps) {
					for(i=eaSize(&pProps->eaActors)-1; i>=0; --i) {
						eaPushUnique(peaList, pProps->eaActors[i]->pcName);
					}
				}
			}
		}
	}
}

void GERefreshUsedEncounterTemplateList(SA_PARAM_NN_VALID const char*** peaList)
{
	ZoneMap *zmap = worldGetPrimaryMap();
	WorldZoneMapScope *pScope = zmapGetScope(zmap);

	// Iterate all the encounters that are loaded for editing
	// because we can only get properties if it's loaded for editing
	if (pScope) {
		StashTableIterator iter;
		StashElement elem;

		stashGetIterator(pScope->scope.name_to_obj, &iter);
		while (stashGetNextElement(&iter, &elem))
		{
			WorldEncounterObject *obj = stashElementGetPointer(elem);
			if (obj && obj->type == WL_ENC_ENCOUNTER && obj->tracker && obj->tracker->def) {
				WorldEncounterProperties *pProps = obj->tracker->def->property_structs.encounter_properties;
				if (pProps) {
					EncounterTemplate *pTemplate = GET_REF(pProps->hTemplate);
					if (pTemplate) {
						eaPushUnique(peaList, pTemplate->pcName);
					}
				}
			}
		}
	}
}

void GERefreshClickableList(const char*** peaList)
{
	eaClear((char***)peaList);

	// Find all interactables
	worldGetObjectNames(WL_ENC_INTERACTABLE, peaList, NULL);
}

void GERefreshVolumeAndInteractableList(const char*** peaList)
{
	// Clear list and add interactables
	GERefreshClickableList(peaList);
	// Add volumes to the list and sort
	worldGetObjectNames(WL_ENC_NAMED_VOLUME, peaList, NULL);
}

void GERefreshEncounterLayerList(const char*** peaList, OldEncounterMasterLayer *pMasterLayer)
{
	eaClearEx((char***)peaList, NULL);
	if(pMasterLayer)
	{
		int i, n = eaSize(&pMasterLayer->encLayers);
		for (i = 0; i < n; i++)
		{
			EncounterLayer *layer = pMasterLayer->encLayers[i];

			if (layer)
			{
				int k;
				int found = 0;
				for(k=0; k<eaSize(peaList); k++)
				{
					if(!stricmp((*peaList)[k], layer->name))
					{
						found = 1;
						break;
					}
				}

				if(!found)
				{
					eaPush(peaList, strdup(layer->name));
				}
			}
		}
	}
}

// Gets a list of all Encounter Groups that have Static Encounters as immediate children
void GERefreshEncGroupList(const char*** peaList)
{
	eaClear(peaList);

	worldGetObjectNames(WL_ENC_LOGICAL_GROUP, peaList, NULL);

	if(gConf.bAllowOldEncounterData && g_EncounterMasterLayer)
	{
		int i, n = eaSize(&g_EncounterMasterLayer->encLayers);
		for (i = 0; i < n; i++)
		{
			EncounterLayer *layer = g_EncounterMasterLayer->encLayers[i];
			if (layer)
			{
				int j, m = eaSize(&layer->staticEncounters);
				for (j = 0; j < m; j++)
				{
					if (layer->staticEncounters[j]->groupOwner && layer->staticEncounters[j]->groupOwner->groupName)
					{
						eaPushUnique(peaList, layer->staticEncounters[j]->groupOwner->groupName);
					}
				}
			}
		}
	}
}

// ----------------------------------------------------------------------------------
// Misc Setup
// ----------------------------------------------------------------------------------

#endif

// This exists to pre-load parse table information into static values for performance at runtime
AUTO_RUN;
int GEGetColumnInfo(void)
{
#ifndef NO_EDITORS
	assertmsg(ParserFindColumn(parse_OldActorInfo, "CritterName", &s_ActorCritterIndex), "Failed to find ActorField: \"CritterName\", was it renamed?");
	assertmsg(ParserFindColumn(parse_OldActorInfo, "critterRank", &s_ActorRankIndex), "Failed to find ActorField: \"critterRank\", was it renamed?");
	assertmsg(ParserFindColumn(parse_OldActorInfo, "Strength", &s_ActorSubRankIndex), "Failed to find ActorField: \"Strength\", was it renamed?");
	assertmsg(ParserFindColumn(parse_OldActorInfo, "critterFaction", &s_ActorFactionIndex), "Failed to find ActorField: \"critterFaction\", was it renamed?");
	assertmsg(ParserFindColumn(parse_OldActorInfo, "contactScript", &s_ActorContactIndex), "Failed to find ActorField: \"contactScript\", was it renamed?");
	assertmsg(ParserFindColumn(parse_EncounterDef, "SpawnCondition", &s_EncounterSpawnCondIndex), "Failed to find EncounterField: \"SpawnCondition\", was it renamed?");
	assertmsg(ParserFindColumn(parse_EncounterDef, "SuccessCondition", &s_EncounterSuccessCondIndex), "Failed to find EncounterField: \"SuccessCondition\", was it renamed?");
	assertmsg(ParserFindColumn(parse_EncounterDef, "FailureCondition", &s_EncounterFailCondIndex), "Failed to find EncounterField: \"FailureCondition\", was it renamed?");
	assertmsg(ParserFindColumn(parse_EncounterDef, "WaveCondition", &s_EncounterWaveCondIndex), "Failed to find EncounterField: \"WaveCondition\", was it renamed?");
	assertmsg(ParserFindColumn(parse_EncounterDef, "WaveInterval", &s_EncounterWaveIntervalIndex),  "Failed to find EncounterField: \"WaveInterval\", was it renamed?");
	assertmsg(ParserFindColumn(parse_EncounterDef, "waveDelayMin", &s_EncounterWaveMinDelayIndex),  "Failed to find EncounterField: \"waveDelayMin\", was it renamed?");
	assertmsg(ParserFindColumn(parse_EncounterDef, "waveDelayMax", &s_EncounterWaveMaxDelayIndex),  "Failed to find EncounterField: \"waveDelayMax\", was it renamed?");
	assertmsg(ParserFindColumn(parse_EncounterDef, "spawnChance", &s_EncounterSpawnChanceIndex),  "Failed to find EncounterField: \"spawnChance\", was it renamed?");
	assertmsg(ParserFindColumn(parse_EncounterDef, "spawnRadius", &s_EncounterSpawnRadiusIndex),  "Failed to find EncounterField: \"spawnRadius\", was it renamed?");
	assertmsg(ParserFindColumn(parse_EncounterDef, "lockoutRadius", &s_EncounterLockoutRadiusIndex),  "Failed to find EncounterField: \"lockoutRadius\", was it renamed?");
	assertmsg(ParserFindColumn(parse_EncounterDef, "respawnTimer", &s_EncounterRespawnTimerIndex),  "Failed to find EncounterField: \"respawnTimer\", was it renamed?");
	assertmsg(ParserFindColumn(parse_EncounterDef, "gangID", &s_EncounterGangIDIndex),  "Failed to find EncounterField: \"gangID\", was it renamed?");
#endif
	return 1;
}

