/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "entCritter.h"
#include "oldencounter_common.h"
#include "gameeditorshared.h"
#include "mission_common.h"
#include "contact_common.h"
#include "MissionVarTableEditor.h"
#include "StringCache.h"
#include "wlEncounter.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"

#include "AutoGen/entCritter_h_ast.h"
#include "AutoGen/mission_common_h_ast.h"
#include "AutoGen/mission_enums_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define SE_SPACE 4

static void MVERefreshUI(MissionVarTableEditDoc *editDoc);
static void MVECreateListUI(MissionVarTableEditDoc *editDoc);

static bool MVEChangeStateEM(void* unused, MissionVarTable* newState)
{
	MissionVarTableEditDoc* editDoc = (MissionVarTableEditDoc*) GEGetActiveEditorDocEM(NULL);

	// Destroy the old UI
	GEDestroyUIGenericEM(&editDoc->emDoc);

	// Copy over the current def
	StructCopyAll(parse_MissionVarTable, newState, editDoc->varTable);

	// Recreate the UI
	MVESetupUIEM(editDoc);
}
static MissionVarTable *MVESaveStateEM(MissionVarTableEditDoc* mveDoc)
{
	MissionVarTable* varTable = StructCreate(parse_MissionVarTable);

	StructCopyAll(parse_MissionVarTable, mveDoc->varTable, varTable);

	return varTable;
}
static bool MVEFreeState(void* unused, MissionVarTable* varTable)
{
	StructDestroy(parse_MissionVarTable, varTable);
}
EditDocDefinition MVEDocDefinition = 
{
	NULL,						// Paste
	NULL,						// Cut/Copy
	NULL,						// Cancel
	NULL,						// Delete
	NULL,						// Move Origin
	NULL,						// Move SpawnLoc
	NULL,						// Freeze Selected Objects
	NULL,						// Unfreeze all frozen objects
	NULL,						// LeftClick
	NULL,						// RightClick
	NULL,						// Group
	NULL,						// Ungroup
	NULL,						// Attach
	NULL,						// Detach
	NULL,						// Center Camera
	NULL,						// PlacementTool from Doc
	NULL,						// Begin Quickplace
};

typedef struct TemplateVarCBData{
	TemplateVariable* var;
	MVETemplateVarUpdateFunc callback;
	void* userData;
} TemplateVarCBData;

static char* MVECreateUniqueVarTableName(const char* desiredName)
{
	static char nextName[GE_NAMELENGTH_MAX];
	int counter = 1;
	strcpy(nextName, desiredName);

	while (resGetInfo(RefSystem_GetDictionaryNameFromNameOrHandle(g_MissionVarTableDict), nextName))
	{
		sprintf(nextName, "%s%i", desiredName, counter);
		counter++;
	}

	return nextName;
}

static Message* missionvartable_CreateChildVarMessage(const MissionVarTable *varTable, const char *varName, U32 id)
{
	Message *newMessage = StructCreate(parse_Message);
	char *key = NULL;
	char *description = NULL;
	char *scope = NULL;

	estrStackCreate(&key);
	estrStackCreate(&description);
	estrStackCreate(&scope);
	
	estrPrintf(&key, "MissionVarTable.%s.childvar.id%d", varTable->pchName, id);
	estrPrintf(&description, "This is a possible value for a text field in a randomly-generated Mission.");
	estrPrintf(&scope, "MissionVarTables/%s", varTable->pchName);

	newMessage->pcMessageKey = StructAllocString(key);
	newMessage->pcDescription = StructAllocString(description);
	newMessage->pcScope = allocAddString(scope);

	estrDestroy(&key);
	estrDestroy(&description);
	estrDestroy(&scope);

	return newMessage;
}

static void MVEUpdateChildVarValue(UIWidget *widget, TemplateVariable *var, MissionVarTable *varTable)
{
	int intVal;
	if (var)
	{
		switch(var->varType)
		{
		case TemplateVariableType_Int:
			{
				const char *string = ui_TextEntryGetText((UITextEntry*)widget);
				if (!string || !string[0])
					MultiValClear(&var->varValue);
				else
				{
					intVal = atoi(string);
					MultiValSetInt(&var->varValue, intVal);
				}
			}
		xcase TemplateVariableType_Message:
			{
				const Message *newMessage = ui_MessageEntryGetMessage((UIMessageEntry*)widget);
				Message *defaultMessage = missionvartable_CreateChildVarMessage(varTable, var->varName, var->id);
				Message *updateMessage = langGetOrCreateDisplayMessageFromList(&varTable->varMessageList, defaultMessage->pcMessageKey, defaultMessage->pcDescription, defaultMessage->pcScope)->pEditorCopy;
				GEGenericUpdateMessage(&updateMessage, newMessage, defaultMessage);
				MultiValSetString(&var->varValue, updateMessage->pcMessageKey);
				StructDestroy(parse_Message, defaultMessage);
			}
		xcase TemplateVariableType_String:
		case TemplateVariableType_LongString:
		case TemplateVariableType_CritterDef:
		case TemplateVariableType_CritterGroup:
		case TemplateVariableType_StaticEncounter:
		case TemplateVariableType_Mission:
		case TemplateVariableType_Item:
		case TemplateVariableType_Volume:
		case TemplateVariableType_Map:
		case TemplateVariableType_Neighborhood:
			{
				const char *string = ui_EditableGetText((UIEditable*)widget);
				if (!string || !string[0])
					MultiValClear(&var->varValue);
				else
					MultiValSetString(&var->varValue, string);
			}

		xdefault:
			Errorf("Unknown variable type in mission template UI.  Talk to a mission programmer.");
		}
	}
	GESetCurrentDocUnsaved();
}

static void MVEUpdateDependencyVarValue(UIWidget *widget, TemplateVariable *var, MissionVarTable *varTable)
{
	int intVal;
	if (var)
	{
		switch(var->varType)
		{
		case TemplateVariableType_Int:
			{
				const char *string = ui_TextEntryGetText((UITextEntry*)widget);
				if (!string || !string[0])
					MultiValClear(&var->varValue);
				else
				{
					intVal = atoi(string);
					MultiValSetInt(&var->varValue, intVal);
				}
			}
		xcase TemplateVariableType_Message:
			{
/*  Can't have a Message dependency - BF
				const Message *newMessage = ui_MessageEntryGetMessage((UIMessageEntry*)widget);
				Message *defaultMessage = missionvartable_CreateDependencyVarMessage(varTable, var->varName, var->id);
				Message *updateMessage = langGetOrCreateDisplayMessageFromList(&varTable->varMessageList, defaultMessage->pcMessageKey, defaultMessage->pcDescription, defaultMessage->pcScope)->pEditorCopy;
				GEGenericUpdateMessage(&updateMessage, newMessage, defaultMessage);
				MultiValSetString(&var->varValue, updateMessage->pcMessageKey);
				StructDestroy(parse_Message, defaultMessage);
*/
				Errorf("Error: Can't have a Message dependency! Talk to a Mission programmer.");
			}
		xcase TemplateVariableType_String:
		case TemplateVariableType_LongString:
		case TemplateVariableType_CritterDef:
		case TemplateVariableType_CritterGroup:
		case TemplateVariableType_StaticEncounter:
		case TemplateVariableType_Mission:
		case TemplateVariableType_Item:
		case TemplateVariableType_Volume:
		case TemplateVariableType_Map:
		case TemplateVariableType_Neighborhood:
			{
				const char *string = ui_EditableGetText((UIEditable*)widget);
				if (!string || !string[0])
					MultiValClear(&var->varValue);
				else
					MultiValSetString(&var->varValue, string);
			}

		xdefault:
			Errorf("Unknown variable type in mission template UI.  Talk to a mission programmer.");
		}
	}
	GESetCurrentDocUnsaved();
}


static void MVEGenericTemplateVarChangeFunc(UIAnyWidget *widget, void *userdata)
{
	TemplateVarCBData *data = (TemplateVarCBData *)userdata;
	if (data->callback)
		data->callback(widget, data->var, data->userData);
}


static void MVEFreeTextEntryUserDataCB(UIEditable *editable)
{
	if (editable->changedData)
		free(editable->changedData);
}

static void MVEFreeMessageEntryUserDataCB(UIMessageEntry *entry)
{
	if (entry->pChangedData)
		free(entry->pChangedData);
}

F32 MVECreateTemplateVariableUI(UIAnyWidget** ppWidget, TemplateVariable *var, TemplateVariableType varType, F32 x, F32 y, F32 w, MVETemplateVarUpdateFunc callback, void* userData, DisplayMessageList *displayMessageList, MVETemplateVarCreateMessageFunc createMessageFunc, void *messageParent, UIWidget *widgetParent)
{
	char tmpStr[1024];
	const char *varString = NULL;
	static const char** staticEncList = NULL;
	static const char** volumeNameList = NULL;
	UIEditable* textEntry = NULL;
	UIMessageEntry *messageEntry = NULL;
	TemplateVarCBData *cbData = calloc(1, sizeof(TemplateVarCBData));

	// Create a list of all static encounters on the current map for the templates
	// TODO: this doesn't always need to be done, if there's already a list for the current map or if
	// the template doesn't use encounters
	eaClear(&staticEncList);
	eaClear(&volumeNameList);
	oldencounter_MakeMasterStaticEncounterNameList(g_EncounterMasterLayer, &staticEncList, NULL);
	worldGetObjectNames(WL_ENC_NAMED_VOLUME, &volumeNameList, NULL);
	
	if (!var)
		return y;

	if (var && (var->varValue.type == MULTI_STRING || var->varValue.type == MULTI_STRING_F ))
		varString = MultiValGetString(&var->varValue, NULL);

	cbData->var = var;
	cbData->userData = userData;
	cbData->callback = callback;

	switch(varType)
	{
		case TemplateVariableType_CritterDef:
			y = GETextEntryCreateEx(&textEntry, NULL, NULL, varString, NULL, "CritterDef", NULL, parse_CritterDef, "Name", x, y, w, 0, 0, GE_VALIDFUNC_NONE, MVEGenericTemplateVarChangeFunc, cbData, widgetParent);
		xcase TemplateVariableType_CritterGroup:
			y = GETextEntryCreateEx(&textEntry, NULL, NULL, varString, NULL, "CritterGroup", NULL, parse_CritterGroup, "Name", x, y, w, 0, 0, GE_VALIDFUNC_NONE, MVEGenericTemplateVarChangeFunc, cbData, widgetParent);
		xcase TemplateVariableType_StaticEncounter:
			y = GETextEntryCreateEx(&textEntry, NULL, NULL, varString, &staticEncList, NULL, NULL, NULL, NULL, x, y, w, 0, 0, GE_VALIDFUNC_NONE, MVEGenericTemplateVarChangeFunc, cbData, widgetParent);
		xcase TemplateVariableType_Volume:
			y = GETextEntryCreateEx(&textEntry, NULL, NULL, varString, &volumeNameList, NULL, NULL, NULL, NULL, x, y, w, 0, 0, GE_VALIDFUNC_NONE, MVEGenericTemplateVarChangeFunc, cbData, widgetParent);
		xcase TemplateVariableType_Mission:
			y = GETextEntryCreateEx(&textEntry, NULL, NULL, varString, NULL, g_MissionDictionary, NULL, parse_MissionDef, "Name", x, y, w, 0, 0, GE_VALIDFUNC_NONE, MVEGenericTemplateVarChangeFunc, cbData, widgetParent);
		xcase TemplateVariableType_Map:
			GERefreshMapNamesList();
			y = GETextEntryCreateEx(&textEntry, NULL, NULL, varString, &g_GEMapDispNames, NULL, NULL, NULL, NULL, x, y, w, 0, 0, GE_VALIDFUNC_NONE, MVEGenericTemplateVarChangeFunc, cbData, widgetParent);
		xcase TemplateVariableType_Neighborhood:
			y = GETextEntryCreateEx(&textEntry, NULL, NULL, varString, NULL, NULL, NULL, NULL, NULL, x, y, w, 0, 0, GE_VALIDFUNC_NONE, MVEGenericTemplateVarChangeFunc, cbData, widgetParent);
		xcase TemplateVariableType_Item:
			y = GETextEntryCreateEx(&textEntry, NULL, NULL, varString, NULL, "ItemDef", NULL, parse_ItemDef, "Name", x, y, w, 0, 0, GE_VALIDFUNC_NONE, MVEGenericTemplateVarChangeFunc, cbData, widgetParent);
		xcase TemplateVariableType_String:
			y = GETextEntryCreateEx(&textEntry, NULL, NULL, varString, NULL, NULL, NULL, NULL, NULL, x, y, w, 0, 0, GE_VALIDFUNC_NONE, MVEGenericTemplateVarChangeFunc, cbData, widgetParent);
		xcase TemplateVariableType_LongString:
			y = GETextEntryCreateEx(&textEntry, NULL, NULL, varString, NULL, NULL, NULL, NULL, NULL, x, y, w, 150, 0, GE_VALIDFUNC_NONE, MVEGenericTemplateVarChangeFunc, cbData, widgetParent);
		xcase TemplateVariableType_Message:
			{
				if (createMessageFunc)
				{
					const char *messageKey = varString;
					Message *temp = createMessageFunc(messageParent, var->varName, var->id);
					Message *message = langGetOrCreateDisplayMessageFromList(displayMessageList, (messageKey?messageKey:temp->pcMessageKey), temp->pcDescription, temp->pcScope)->pEditorCopy;
					y = GECreateMessageEntryEx(&messageEntry, NULL, NULL, message, x, y, w, 0, MVEGenericTemplateVarChangeFunc, cbData, widgetParent, false, false);
					StructDestroy(parse_Message, temp);
				}
			}
		xcase TemplateVariableType_Int:
			sprintf(tmpStr, "%"FORM_LL"d", MultiValGetInt(&var->varValue, NULL));
			y = GETextEntryCreateEx(&textEntry, NULL, NULL, tmpStr, NULL, NULL, NULL, NULL, NULL, x, y, w, 0, 0, GE_VALIDFUNC_INTEGER, MVEGenericTemplateVarChangeFunc, cbData, widgetParent);
		xdefault:
			Errorf("Unknown template variable type %d", var->varType);
	}

	// The spacing of the expander is fairly fragile; it relies on having labels and text entries
	if(textEntry)
	{
		ui_WidgetSetFreeCallback(UI_WIDGET(textEntry), MVEFreeTextEntryUserDataCB);
		if (ppWidget)
			*ppWidget = textEntry;
	}
	else if (messageEntry)
	{
		ui_WidgetSetFreeCallback(UI_WIDGET(messageEntry), MVEFreeMessageEntryUserDataCB);
		if (ppWidget)
			*ppWidget = messageEntry;
	}
	
	y+= GE_SPACE;
	return y;
}

//
static F32 MVECreateVarOptionUI(GEListItem *listItem, UIWidget*** widgetList, TemplateVariableOption* varOption, F32 x, F32 y, UIAnyWidget* widgetParent, MissionVarTableEditDoc* editDoc)
{
	MissionVarTable *varTable = editDoc->varTable;
	UIWidget *widget = NULL;
	UILabel *label = NULL;

	label = ui_LabelCreate("Child Value", x, y);
	eaPush(widgetList, UI_WIDGET(label));
	ui_WidgetAddChild(widgetParent, UI_WIDGET(label));

	y = MVECreateTemplateVariableUI(&widget, &varOption->value, varOption->value.varType, x+80, y, 200, MVEUpdateChildVarValue, varTable, &varTable->varMessageList, missionvartable_CreateChildVarMessage, varTable, widgetParent);
	if (widget)
		eaPush(widgetList, widget);

	return y;
}

static TemplateVariableOption *MVECreateVarOption(MissionVarTableEditDoc *editDoc)
{
	MissionVarTable *varTable = editDoc->varTable;
	TemplateVariableOption *option = StructCreate(parse_TemplateVariableOption);
	option->value.id = varTable->nextVarId++;
	option->value.varName = allocAddString("VarTableValue");
	option->value.varType = varTable->varType;

	return option;
}

static void MVEDestroyVarOption(TemplateVariableOption *varOption, void* unused)
{
	StructDestroy(parse_TemplateVariableOption, varOption);
}

static void MVESubListExpanderCB(UIExpander *expander, GEListItem *listItem)
{
	F32 totalHeight = 0.0f;

	// Fix the height of the parent list
	if (listItem)
	{
		totalHeight = expander->widget.y + expander->widget.height - listItem->y;
		GEListItemSetHeight(listItem, totalHeight);
	}
}

static GEListItem* MVEFindListItemForWidget(UIWidget *widget, GEList *list)
{
	if (list)
	{
		int i, n = eaSize(&list->listItems);
		for (i = 0; i < n; i++)
		{
			if (eaFind(&list->listItems[i]->widgets, widget) != -1)
				return list->listItems[i];
		}
	}
	return NULL;
}

static void MVESubListHeightChangedCB(GEList *list, F32 newHeight, MissionVarTableEditDoc *editDoc)
{
	GEListItem *listItem = NULL;
	UIExpander *expander = (UIExpander*)list->widgetParent;
	F32 totalHeight = 0.0f;

	ui_ExpanderSetHeight(expander, newHeight + GE_SPACE);

	// Fix the height of the parent list
	if (expander && editDoc->mainList)
		listItem = MVEFindListItemForWidget(UI_WIDGET(expander), editDoc->mainList);
	if (listItem)
	{
		totalHeight = expander->widget.y + expander->widget.height - listItem->y;
		GEListItemSetHeight(listItem, totalHeight);
	}
}


static F32 MVECreateSublistUI(GEListItem *listItem, UIWidget*** widgetList, TemplateVariableSubList* subList, F32 x, F32 y, UIAnyWidget* widgetParent, MissionVarTableEditDoc* editDoc)
{
	MissionVarTable *varTable = editDoc->varTable;
	F32 currY = y;
	F32 currX = x;
	UIWidget *widget = NULL;
	UILabel *label = NULL;
	UIExpander *expander = NULL;
	GEList *list = NULL;

	// Parent value
	if (varTable->dependencyType != TemplateVariableType_None && varTable->dependencyType != TemplateVariableType_Message && varTable->dependencyType != TemplateVariableType_Int)
	{
		if (subList->parentValue)
		{
			label = ui_LabelCreate("Parent Value", x, y);
			eaPush(widgetList, UI_WIDGET(label));
			ui_WidgetAddChild(widgetParent, UI_WIDGET(label));

			currY = MVECreateTemplateVariableUI(&widget, subList->parentValue, varTable->dependencyType, x+80, y, 200, MVEUpdateDependencyVarValue, varTable, &varTable->varMessageList, NULL, varTable, widgetParent);
			if (widget)
				eaPush(widgetList, widget);
		}
	}
	else if (varTable->dependencyType == TemplateVariableType_Int)
	{
		label = ui_LabelCreate("Parent Value", x, y);
		eaPush(widgetList, UI_WIDGET(label));
		ui_WidgetAddChild(widgetParent, UI_WIDGET(label));

		// TODO - Int range
	}

	expander = ui_ExpanderCreate("Values", 100.0f);
	ui_ExpanderSetExpandCallback(expander, MVESubListExpanderCB, listItem);
	eaPush(widgetList, UI_WIDGET(expander));
	ui_WidgetAddChild(widgetParent, UI_WIDGET(expander));
	ui_WidgetSetPosition(UI_WIDGET(expander), x, currY);
	ui_WidgetSetWidthEx(UI_WIDGET(expander), 1.0, UIUnitPercentage);
	list = GEListCreate(&subList->childValues, 0, 0, expander, editDoc, MVECreateVarOptionUI, MVECreateVarOption, MVEDestroyVarOption, MVESubListHeightChangedCB, false, true);
	eaPush(&editDoc->subGroupLists, list);
	currY += expander->widget.height + GE_SPACE;

	return currY;
}

static TemplateVariableSubList *MVECreateSublist(MissionVarTableEditDoc *editDoc)
{
	MissionVarTable *varTable = editDoc->varTable;
	TemplateVariableSubList *subList = StructCreate(parse_TemplateVariableSubList);
	if (varTable->dependencyType != TemplateVariableType_None && varTable->dependencyType != TemplateVariableType_Message && varTable->dependencyType != TemplateVariableType_Int)
	{
		subList->parentValue = StructCreate(parse_TemplateVariable);
		subList->parentValue->varType = editDoc->varTable->dependencyType;
		subList->parentValue->id = varTable->nextVarId++;
		subList->parentValue->varName = allocAddString("VarTableDependency");
	}
	return subList;
}

static void MVEDestroySublist(TemplateVariableSubList *subList, MissionVarTableEditDoc *editDoc)
{
	int i, n = eaSize(&editDoc->subGroupLists);
	for (i = 0; i < n; i++)
	{
		if (editDoc->subGroupLists[i]->items == &subList->childValues)
		{
			GEList *list = eaRemove(&editDoc->subGroupLists, i);
			free(list);
			break;
		}
	}
	StructDestroy(parse_TemplateVariableSubList, subList);
}


static void MVEMainListHeightChangedCB(GEList *list, F32 newHeight, MissionVarTableEditDoc *editDoc)
{
	ui_ScrollAreaSetSize(editDoc->mainScrollArea, 1000, list->y + newHeight + GE_SPACE);
}


static void MVEMissionVarTableNameChanged(UIEditable *editable, MissionVarTableEditDoc *editDoc)
{
	const char *newName = ui_EditableGetText(editable);
	if (editDoc && editDoc->varTable)
	{
		editDoc->varTable->pchName = (char*)allocAddString(newName);
	}
	MVENameChangedEM(newName, editDoc);
}

static void MVEDependencyTypeChanged(UIComboBox *combo, int value, MissionVarTableEditDoc *editDoc)
{
	if (editDoc && editDoc->varTable)
	{
		editDoc->varTable->dependencyType = value;
		MVERefreshUI(editDoc);
		GESetDocUnsaved(editDoc);
	}
}

static void MVEVariableTypeChanged(UIComboBox *combo, int value, MissionVarTableEditDoc *editDoc)
{
	if (editDoc && editDoc->varTable)
	{
		editDoc->varTable->varType = value;
		MVERefreshUI(editDoc);
		GESetDocUnsaved(editDoc);
	}
}

void MVESetupUI(MissionVarTableEditDoc *editDoc)
{
	F32 currY = 0.0f;
	int dialogHeight = 0;
	int inventoryHeight = 0;
	MissionVarTable* varTable = editDoc->varTable;
	UIComboBox *combo = NULL;
	UILabel *label = NULL;
	editDoc->window = ui_WindowCreate(varTable->pchName, 0, 0, 600, 600);

	// Create a scroll area
	editDoc->mainScrollArea = ui_ScrollAreaCreate(0, 0, 0, 0, 4000, 4000, false, true);
	ui_WidgetSetDimensionsEx(UI_WIDGET(editDoc->mainScrollArea), 1.0, 1.0f, UIUnitPercentage, UIUnitPercentage);
	ui_WindowAddChild(editDoc->window, editDoc->mainScrollArea);

	// Add all the editing widgets to the pane
	currY = GETextEntryCreate(NULL, "Name", varTable->pchName, 0, 0, 200, 0, 100, GE_VALIDFUNC_NOSPACE, MVEMissionVarTableNameChanged, editDoc, editDoc->mainScrollArea) + GE_SPACE;

	label = ui_LabelCreate("Dependency", 0, currY);
	ui_WidgetAddChild(UI_WIDGET(editDoc->mainScrollArea), UI_WIDGET(label));
	combo = ui_ComboBoxCreateWithEnum(100, currY, 200, TemplateVariableTypeEnum, MVEDependencyTypeChanged, editDoc);
	ui_ComboBoxSetSelected(combo, varTable->dependencyType);
	ui_WidgetAddChild(UI_WIDGET(editDoc->mainScrollArea), UI_WIDGET(combo));
	currY += combo->widget.height + GE_SPACE;
	editDoc->dependencyTypeCombo = combo;

	label = ui_LabelCreate("Variable Type", 0, currY);
	ui_WidgetAddChild(UI_WIDGET(editDoc->mainScrollArea), UI_WIDGET(label));
	combo = ui_ComboBoxCreateWithEnum(100, currY, 200, TemplateVariableTypeEnum, MVEVariableTypeChanged, editDoc);
	ui_ComboBoxSetSelected(combo, varTable->varType);
	ui_WidgetAddChild(UI_WIDGET(editDoc->mainScrollArea), UI_WIDGET(combo));
	currY += combo->widget.height + GE_SPACE;
	editDoc->variableTypeCombo = combo;

	// Add the list of sub-lists
	MVECreateListUI(editDoc);
};

bool MVESaveMissionVarTableFile(MissionVarTable* varTable, char* fileOverride)
{
	return (bool) ParserWriteTextFileFromSingleDictionaryStruct(fileOverride?fileOverride:varTable->filename, g_MissionVarTableDict, varTable, 0, 0);
}


MissionVarTable* MVENewMissionVarTable(const char* name, MissionVarTableEditDoc* editDoc)
{
	char varTableName[512];
	char outFile[MAX_PATH];
	MissionVarTable* varTable = NULL;
	editDoc->docDefinition = &MVEDocDefinition;

	if(name)
		strcpy(varTableName, MVECreateUniqueVarTableName(name));
	else
		strcpy(varTableName, MVECreateUniqueVarTableName("MissionVarTable"));

	// Now create the varTable
	varTable = StructCreate(parse_MissionVarTable);
	varTable->pchName = (char*)allocAddString(varTableName);
	sprintf(outFile, "defs/MissionVars/%s.missionvars", varTableName);
	varTable->filename = allocAddString(outFile);

	editDoc->newAndNeverSaved = 1;
	editDoc->varTable = varTable;
	editDoc->varTableOrig = StructClone(parse_MissionVarTable, varTable);

	// Create the UI from the store def
	MVESetupUI(editDoc);

	return varTable;
}

MissionVarTable* MVEOpenMissionVarTable(const char* name, MissionVarTableEditDoc* editDoc)
{
	MissionVarTable* varTable = NULL;
	editDoc->docDefinition = &MVEDocDefinition;

	if ((varTable = RefSystem_ReferentFromString(g_MissionVarTableDict, name))
		&& resIsEditingVersionAvailable(g_MissionVarTableDict, name))
	{
		editDoc->varTable = StructClone(parse_MissionVarTable, varTable);
		editDoc->varTableOrig = StructClone(parse_MissionVarTable, varTable);
		langMakeEditorCopy(parse_MissionVarTable, editDoc->varTable, false);
		langMakeEditorCopy(parse_MissionVarTable, editDoc->varTableOrig, false);

		// Create the UI from the store def
		MVESetupUI(editDoc);
		return varTable;
	}
	else
	{
		// Wait for object to show up so we can open it
		resSetDictionaryEditMode(g_MissionVarTableDict, true);
		resSetDictionaryEditMode(gMessageDict, true);
		emSetResourceState(editDoc->emDoc.editor, name, EMRES_STATE_OPENING);
		resRequestOpenResource(g_MissionVarTableDict, name);
		return NULL;
	}
}

void MVECloseMissionVarTable(MissionVarTableEditDoc* editDoc)
{
	StructDestroy(parse_MissionVarTable, editDoc->varTable);
	StructDestroy(parse_MissionVarTable, editDoc->varTableOrig);
	
	// Free everything else allocated by the doc
	eaDestroyEx(&editDoc->subGroupLists, NULL);
	if (editDoc->mainList)
		GEListDestroy(editDoc->mainList);
}

EMTaskStatus MVESaveMissionVarTable(MissionVarTableEditDoc* editDoc, bool saveAsCopy)
{
	MissionVarTable* varTable = editDoc->varTable;
	EMTaskStatus status;
	if (emHandleSaveResourceState(editDoc->emDoc.editor, editDoc->varTable->pchName, &status)) {
		return status;
	}

	// --- Pre-save validation goes here ---

	resSetDictionaryEditMode(g_MissionVarTableDict, true);
	resSetDictionaryEditMode(gMessageDict, true);

	// Aquire the lock
	if (!resGetLockOwner(g_MissionVarTableDict, editDoc->varTable->pchName)) {
		// Don't have lock, so ask server to lock and go into locking state
		emSetResourceState(editDoc->emDoc.editor, editDoc->varTable->pchName, EMRES_STATE_LOCKING_FOR_SAVE);
		resRequestLockResource(g_MissionVarTableDict, editDoc->varTable->pchName, editDoc->varTable);
		return EM_TASK_INPROGRESS;
	}
	// Get here if have the lock

	// Send save to server
	emSetResourceStateWithData(editDoc->emDoc.editor, editDoc->varTable->pchName, EMRES_STATE_SAVING, editDoc);
	resRequestSaveResource(g_MissionVarTableDict, editDoc->varTable->pchName, editDoc->varTable);
	return EM_TASK_INPROGRESS;
}

/*
static void MVESaveMissionVarTableAsConfirm(const char *dir, const char *file, MissionVarTableEditDoc* editDoc)
{
	MissionVarTable* varTable = editDoc->varTable;
	char fileName[MAX_PATH];

	sprintf(fileName, "%s/%s", dir, file);
	StructFreeString(varTable->filename);
	varTable->filename = StructAllocString(fileName);
	GESetDocFileEM(&editDoc->emDoc, fileName);

	MVESaveMissionVarTable(editDoc);
	GECheckoutDocEM(&editDoc->emDoc);
}
*/

/*  SaveAs is just going to do a Save, but not delete the old file.
    Since the filename is the same as the MissionVarTable name,
	selecting a file is superfluous.
	This may need to be revisited at some point.
	- BF 3/28/08

void MVESaveMissionVarTableAs(MissionVarTableEditDoc* editDoc)
{
	MissionVarTable* varTable = editDoc->varTable;
	UIWindow *browser;
	char startDir[CRYPTIC_MAX_PATH];
	char topDir[CRYPTIC_MAX_PATH];

	fileLocateWrite(varTable->filename, startDir);
	fileLocateWrite("defs/MissionVars", topDir);
	backSlashes(startDir);
	backSlashes(topDir);
	getDirectoryName(startDir);
	browser = ui_FileBrowserCreate("Save MissionVarTable as", "Save", UIBrowseNew, UIBrowseFiles,
		topDir, startDir, ".missionvars", NULL, NULL, MVESaveMissionVarTableAsConfirm, editDoc);
	if (browser)
	{
		elUICenterWindow(browser);
		ui_WindowShow(browser);
	}
}
*/

static void MVECreateListUI(MissionVarTableEditDoc *editDoc)
{
	MissionVarTable *varTable = editDoc->varTable;
	F32 y = editDoc->variableTypeCombo->widget.y + editDoc->variableTypeCombo->widget.height + GE_SPACE;
	if (varTable->varType != TemplateVariableType_None)
		editDoc->mainList = GEListCreate(&varTable->subLists, 0, y, (UIAnyWidget*)editDoc->mainScrollArea, (void*)editDoc, MVECreateSublistUI, MVECreateSublist, MVEDestroySublist, MVEMainListHeightChangedCB, false, true);
}

static void MVERefreshUI(MissionVarTableEditDoc *editDoc)
{
	eaClearEx(&editDoc->subGroupLists, NULL);
	if (editDoc->mainList)
		GEListDestroy(editDoc->mainList);
	editDoc->mainList = NULL;

	MVECreateListUI(editDoc);
}

#endif // NO_EDITORS