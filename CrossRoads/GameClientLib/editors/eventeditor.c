/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "CharacterAttribs.h"
#include "eventeditor.h"
#include "gameeditorshared.h"
#include "GameEvent.h"
#include "mission_common.h"

#include "encounter_common.h"
#include "itemCommon.h"
#include "StateMachine.h"

#include "UICheckButton.h"

#include "sysutil.h"
#include "entCritter.h"
#include "ExpressionEditor.h"
#include "StringCache.h"
#include "wlEncounter.h"
#include "WorldGrid.h"

#include "oldencounter_common.h"
#include "PowerModes.h"
#include "contact_common.h"
#include "nemesis_common.h"

#include "GameEvent_h_ast.h"
#include "entEnums_h_ast.h"
#include "MinigameCommon_h_ast.h"
#include "mission_enums_h_ast.h"
#include "encounter_enums_h_ast.h"

#include "EditorPrefs.h"

#define EV_FIELD_W 150
#define EV_LABEL_W 100
#define EV_MARGIN_W 20
#define EV_CENTER_SPACE 60
#define EV_SPACE 5

#define EV_RIGHT_COLUMN_OFFSET (EV_MARGIN_W + EV_LABEL_W + EV_SPACE + EV_FIELD_W + EV_SPACE + EV_CENTER_SPACE)
#define EV_WINDOW_DEFAULT_W (2*EV_FIELD_W + 2*EV_LABEL_W + 2*EV_MARGIN_W + EV_CENTER_SPACE + 4*EV_SPACE)

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

typedef struct EventEditor{
	UIScrollArea *scrollArea;
	UIWindow *window;
	GameEvent ev;  // This event is used internally to store the value of each field 
				   //and convert between the different string formats

	GameEvent *boundEvent; // This is the event that is actually being edited.  Optional.
	bool isCleared;
	bool bFreeBoundEvent;  // TRUE if boundEvent must be freed by the EventEditor

	EventEditor *chainedEventEditor;

	// Callbacks
	EventEditorChangeFunc changeFunc;
	void *changeData;

	EventEditorChangeFunc closeFunc;
	void *closeData;

	UIComboBox *typeCombo;
	UITextArea *descriptionArea;
	UITextArea *inputArea;
	UILabel *inputLabel;
	UITextArea *outputArea;
	UILabel *outputLabel;
	UIButton *outputCopyButton;
	UIButton *saveButton;
	UIButton *clearButton;

	// These are all the widgets for GameEvent fields.
	UIEditable** textEntries;
	UICheckButton** checkButtons;
	UIComboBox** comboBoxes;
	UILabel** labels;
	UISeparator** separators;
	UIButton* swapButton;
	UITextEntry *nameEntry;

	UIButton *pChainedEventButton;
	UIButton *pChainedEventRemoveBtn;
	UILabel *pChainEventBtnLabel;
	UILabel *pChainEventLabel;

	// These are lists used to populate combo boxes
	char** sourceActorList;
	char** targetActorList;
	const char** encounterList;
	const char** staticEncList;
	char** dialogList;
	const char** volumeList;
	const char** clickableList;
	const char** clickableGroupList;
	char** fsmStateList;
	const char** eaEncGroupList;

	U32 noMapMatch : 1;
} EventEditor;

// This is passed to all the widgets so that they can have a pointer to the editor as well as a pointer
// to a specific field in the GameEvent struct that the widget is bound to.
typedef struct EventFieldCBData{
	EventEditor *editor;
	void *data;			// some field in the GameEvent that this widget edits
	bool bPooledString;
	bool bIsArray;
	bool bIsFloat;
	bool bIsInt;
	StaticDefineInt *pEnumTable;
} EventFieldCBData;

// declarations
static void eventeditor_Refresh(EventEditor *editor);  // completely remakes UI from scratch
static void eventeditor_RefreshFields(EventEditor *editor);  // refreshes all the field widgets and output
static void eventeditor_RefreshOutput(EventEditor* editor);  // refreshes the output box
static void eventeditor_RefreshLists(EventEditor* editor);
static void eventeditor_UpdateEventFromFields(EventEditor *editor);

static void eventeditor_RefreshActorLists(EventEditor *editor)
{
	EncounterDef *def = NULL;

	eaClearEx(&editor->sourceActorList, NULL);
	eaClearEx(&editor->targetActorList, NULL);

	// Source
	// SDANGELO TODO: Populate "editor->targetActorList" from encounters on this map
	if (gConf.bAllowOldEncounterData)
	{
		if (editor->ev.pchSourceStaticEncName)
		{
			def = SAFE_MEMBER(oldencounter_StaticEncounterFromName(editor->ev.pchSourceStaticEncName), spawnRule);
		}
		else if (editor->ev.pchSourceEncounterName)
		{
			def = oldencounter_DefFromName(editor->ev.pchSourceEncounterName);
		}
		
		if (def)
		{
			char *tmpEstr = NULL;
			int i, n = eaSize(&def->actors);
			estrStackCreate(&tmpEstr);
			for (i = 0; i < n; i++)
			{
				oldencounter_GetActorName(def->actors[i], &tmpEstr);
				eaPush(&editor->sourceActorList, strdup(tmpEstr));
			}
			estrDestroy(&tmpEstr);
		}
	}

	// Target
	// SDANGELO TODO: Populate "editor->targetActorList" from encounters on this map
	if (gConf.bAllowOldEncounterData)
	{
		def = NULL;
		if (editor->ev.pchTargetStaticEncName)
		{
			def = SAFE_MEMBER(oldencounter_StaticEncounterFromName(editor->ev.pchTargetStaticEncName), spawnRule);
		}
		else if (editor->ev.pchTargetEncounterName)
		{
			def = oldencounter_DefFromName(editor->ev.pchTargetEncounterName);
		}
		
		if (def)
		{
			char *tmpEstr = NULL;
			int i, n = eaSize(&def->actors);
			estrStackCreate(&tmpEstr);
			for (i = 0; i < n; i++)
			{
				oldencounter_GetActorName(def->actors[i], &tmpEstr);
				eaPush(&editor->targetActorList, strdup(tmpEstr));
			}
			estrDestroy(&tmpEstr);
		}
	}
}

static void eventeditor_RefreshClickableGroupList(EventEditor *editor)
{
	const char *pcMapName = zmapInfoGetPublicName(NULL);

	eaClear((char***)&editor->clickableGroupList);
	if (editor->noMapMatch || (pcMapName && editor->ev.pchMapName && (stricmp(pcMapName, editor->ev.pchMapName) == 0)))
	{
		worldGetObjectNames(WL_ENC_LOGICAL_GROUP, &editor->clickableGroupList, NULL);
	}
}

static void eventeditor_RefreshVolumeList(EventEditor *editor)
{
	const char *pcMapName = zmapInfoGetPublicName(NULL);

	eaClear((char***)&editor->volumeList);
	if (editor->noMapMatch || (pcMapName && editor->ev.pchMapName && (stricmp(pcMapName, editor->ev.pchMapName) == 0)))
	{
		worldGetObjectNames(WL_ENC_NAMED_VOLUME, &editor->volumeList, NULL);
	}
}

static void eventeditor_RefreshFsmStateList(EventEditor *editor)
{
	const char *fsmName = editor->ev.pchFSMName;
	FSM *fsm = RefSystem_ReferentFromString(gFSMDict, fsmName);
	eaClearEx((char***)&editor->fsmStateList, NULL);

	if (fsm)
	{
		int i, n = eaSize(&fsm->states);
		for (i = 0; i < n; i++)
			eaPush(&editor->fsmStateList, strdup(fsm->states[i]->name));
	}	
}

static void eventeditor_RefreshEncounterLists(EventEditor *editor)
{
	const char *pcMapName = zmapInfoGetPublicName(NULL);

	eaClear(&editor->encounterList);
	eaClear(&editor->staticEncList);

	if (editor->noMapMatch || (pcMapName && editor->ev.pchMapName && (stricmp(pcMapName, editor->ev.pchMapName) == 0)))
	{
		// Populate "editor->staticEncList" with encounter names on this map
		GERefreshEncounterList(&editor->staticEncList);
		//  Populate "editor->encounterList" with encounter template names on this map
		GERefreshUsedEncounterTemplateList(&editor->encounterList);

		if (gConf.bAllowOldEncounterData)
		{
			// Add information from encounter layers
			oldencounter_MakeMasterStaticEncounterNameList(g_EncounterMasterLayer, &editor->staticEncList, &editor->encounterList);
		}
	}
	else
	{
		DictionaryEArrayStruct *pArrayStruct = resDictGetEArrayStruct("EncounterTemplate");
		if (pArrayStruct)
		{
			int i, n = eaSize(&pArrayStruct->ppReferents);
			for (i = 0; i < n; i++)
			{
				EncounterTemplate *pTemplate = (EncounterTemplate*)pArrayStruct->ppReferents[i];
				if (pTemplate->pcName)
					eaPush(&editor->encounterList, pTemplate->pcName);
			}
		}
		if (gConf.bAllowOldEncounterData)
		{
			DictionaryEArrayStruct *pDefArrayStruct = resDictGetEArrayStruct("EncounterDef");
			if (pDefArrayStruct)
			{
				int i, n = eaSize(&pDefArrayStruct->ppReferents);
				for (i = 0; i < n; i++)
				{
					EncounterDef *encounter = (EncounterDef*)pDefArrayStruct->ppReferents[i];
					if (encounter->name)
						eaPush(&editor->encounterList, encounter->name);
				}
			}
		}
	}


}

static void contactTableDictChanged(enumResourceEventType eType, const char *pDictName, const char *pResourceName, void *pResource, void *pUserData);

static void eventeditor_RefreshDialogList(EventEditor *editor)
{
	eaClear(&editor->dialogList);
	if (editor->ev.pchContactName)
	{
		ContactDef * pDef = NULL;
		
		if (resIsEditingVersionAvailable("Contact", editor->ev.pchContactName)) {
			// Simply open the object since it is in the dictionary
			pDef = RefSystem_ReferentFromString("Contact", editor->ev.pchContactName);
		}
		else
		{
			resDictRegisterEventCallback("Contact", contactTableDictChanged, editor);
			resRequestOpenResource("Contact",editor->ev.pchContactName);
		}

		pDef = contact_DefFromName(editor->ev.pchContactName);
		if (pDef)
		{
			SpecialDialogBlock** eaSpecialDialogs = NULL;
			contact_GetSpecialDialogs(pDef, &eaSpecialDialogs);
			
			if (eaSpecialDialogs)
			{
				int i, n = eaSize(&eaSpecialDialogs);
				for (i = 0; i < n; i++)
				{
					if (eaSpecialDialogs[i]->name)
						eaPush(&editor->dialogList, strdup(eaSpecialDialogs[i]->name));
				}

				eaDestroy(&eaSpecialDialogs);
			}
		}
	}
}

static void contactTableDictChanged(enumResourceEventType eType, const char *pDictName, const char *pResourceName, void *pResource, void *pUserData)
{
	resDictRemoveEventCallback(pDictName,contactTableDictChanged);

	eventeditor_RefreshDialogList((EventEditor *)pUserData);
}

static void eventeditor_RefreshLists(EventEditor* editor)
{
	eventeditor_RefreshEncounterLists(editor);
	eventeditor_RefreshActorLists(editor);
	eventeditor_RefreshDialogList(editor);

	// Eventually, I'd like to have all of the code for these lists in the central editor, rather then
	// just the event editor
	GERefreshClickableList(&editor->clickableList);
	GERefreshEncGroupList(&editor->eaEncGroupList);

	eventeditor_RefreshClickableGroupList(editor);
	eventeditor_RefreshVolumeList(editor);
	eventeditor_RefreshFsmStateList(editor);
}

static EventFieldCBData* eventeditor_CreateCBData(EventEditor *editor, void *data)
{
	EventFieldCBData *cbData = calloc(1, sizeof(EventFieldCBData));
	cbData->editor = editor;
	cbData->data = data;
	return cbData;
}

static void eventeditor_EnumComboChangedCB(UIComboBox *combo, int newvalue, EventFieldCBData* cbData)
{
	if (cbData)
	{	
		if (cbData->pEnumTable)
		{
			StructFreeStringSafe(((char**)cbData->data));
			*((char**)cbData->data) = StructAllocString(StaticDefineIntRevLookup(cbData->pEnumTable, newvalue));
		}
		else if (cbData->bIsArray)
			ea32Set(cbData->data, newvalue, 0);
		else if (cbData->data)
			*((int*)cbData->data) = newvalue;
		if (cbData->editor)
			cbData->editor->isCleared = false;
		eventeditor_RefreshOutput(cbData->editor);
	}
}

static void eventeditor_EventTypeComboChangedCB(UIComboBox *combo, int newvalue, EventFieldCBData* cbData)
{
	eventeditor_EnumComboChangedCB(combo, newvalue, cbData);
	if (cbData && cbData->editor)
	{
		cbData->editor->isCleared = false;
		eventeditor_Refresh(cbData->editor);
	}
}

static void eventeditor_ComboRefresh(UIComboBox *combo)
{
	if (combo)
	{
		EventFieldCBData *cbData = (EventFieldCBData*)ui_ComboBoxGetSelectedData(combo);
		if (cbData && cbData->data)
		{
			if (cbData->pEnumTable)
			{
				ui_ComboBoxSetSelectedEnum(combo, StaticDefineIntGetInt(cbData->pEnumTable, *((char**)cbData->data)));
			}
			else if (cbData->bIsArray)
			{
				ui_ComboBoxSetSelectedEnum(combo, ea32Get(cbData->data, 0));
			}
			else
			{
				ui_ComboBoxSetSelectedEnum(combo, *((int*)cbData->data));
			}
		}
	}
}

static void eventeditor_ComboFreeCB(UIComboBox *combo)
{
	free(combo->selectedData);
}

static int eventeditor_CreateComboForField(EventEditor *editor, int x, int y, char *labelText, StaticDefineInt* enumTable, int *eventField, int **eventArray, char** eventString, bool bAddNoneField)
{
	UILabel *label = NULL;
	UIWidget *widget = NULL;
	EventFieldCBData *cbData;

	if (eventString) {
		cbData = eventeditor_CreateCBData(editor, eventString);
		cbData->pEnumTable = enumTable;
	} else if (eventArray) {
		cbData = eventeditor_CreateCBData(editor, eventArray);
		cbData->bIsArray = true;
	} else {
		cbData = eventeditor_CreateCBData(editor, eventField);
	}

	label = ui_LabelCreate(labelText, x, y);
	ui_ScrollAreaAddChild(editor->scrollArea, label);
	eaPush(&editor->labels, label);
	
	widget = (UIWidget*)ui_ComboBoxCreateWithEnum(x + EV_LABEL_W + EV_SPACE, y, EV_FIELD_W, enumTable, eventeditor_EnumComboChangedCB, cbData);
	if (bAddNoneField)
		ui_ComboBoxEnumInsertValue((UIComboBox*)widget, "Any", -1);

	ui_WidgetSetFreeCallback(widget, eventeditor_ComboFreeCB);
	ui_ScrollAreaAddChild(editor->scrollArea, widget);
	eaPush(&editor->comboBoxes, (UIComboBox*)widget);
	
	eventeditor_ComboRefresh((UIComboBox*)widget);
	return 	y + widget->height + EV_SPACE;
}

static int eventeditor_CreateComboForTriState(EventEditor *editor, int x, int y, char *labelText, TriState *eventField)
{
	UILabel *label = NULL;
	UIWidget *widget = NULL;
	EventFieldCBData *cbData = eventeditor_CreateCBData(editor, eventField);

	label = ui_LabelCreate(labelText, x, y);
	ui_ScrollAreaAddChild(editor->scrollArea, label);
	eaPush(&editor->labels, label);

	widget = (UIWidget*)ui_ComboBoxCreateWithEnum(x + EV_LABEL_W + EV_SPACE + (EV_FIELD_W/2), y, (EV_FIELD_W/2), TriStateEnum, eventeditor_EnumComboChangedCB, cbData);
	ui_WidgetSetFreeCallback(widget, eventeditor_ComboFreeCB);
	ui_ScrollAreaAddChild(editor->scrollArea, widget);
	eaPush(&editor->comboBoxes, (UIComboBox*)widget);

	eventeditor_ComboRefresh((UIComboBox*)widget);
	return 	y + widget->height + EV_SPACE;
}

void eventeditor_RemoveEventType(EventEditor *editor, EventType type)
{
	if(!editor || !editor->typeCombo)
		return;

	ui_ComboBoxEnumRemoveValueInt(editor->typeCombo, type);
}

static int eventeditor_CreateEventTypeCombo(EventEditor *editor, int x, int y)
{
	UIComboBox *combo = NULL;
	EventFieldCBData *cbData = eventeditor_CreateCBData(editor, &editor->ev.type);
	int i;

	ui_ScrollAreaAddChild(editor->scrollArea, ui_LabelCreate("Event Type: ", x, y));
	combo = ui_ComboBoxCreateWithEnum(x + EV_LABEL_W + EV_SPACE, y, EV_FIELD_W, EventTypeEnum, eventeditor_EventTypeComboChangedCB, cbData);

	//Remove deprecated event types from the combo
	for (i = 0; i < EventType_Count; i++){
		const GameEventTypeInfo *pTypeInfo = gameevent_GetTypeInfo(i);
		if (pTypeInfo && pTypeInfo->bDeprecated){
			ui_ComboBoxEnumRemoveValueInt(combo, i);
		}
	}

	// Remove redundant names
	ui_ComboBoxEnumRemoveValueString(combo, "ClickableBeginInteract");
	ui_ComboBoxEnumRemoveValueString(combo, "ClickableBeginInteract");
	ui_ComboBoxEnumRemoveValueString(combo, "ClickableFailure");
	ui_ComboBoxEnumRemoveValueString(combo, "ClickableInterrupted");
	ui_ComboBoxEnumRemoveValueString(combo, "ClickableInteract");
	ui_ComboBoxEnumRemoveValueString(combo, "CritterInteract");
	ui_ComboBoxEnumRemoveValueString(combo, "ClickableComplete");
	ui_ComboBoxEnumRemoveValueString(combo, "FSMPoke");

	ui_WidgetSetFreeCallback(UI_WIDGET(combo), eventeditor_ComboFreeCB);
	ui_ScrollAreaAddChild(editor->scrollArea, combo);
	if (editor->typeCombo)
		ui_WidgetQueueFree(UI_WIDGET(editor->typeCombo));
	editor->typeCombo = combo;
	eventeditor_ComboRefresh(combo);
	return 	y + UI_WIDGET(combo)->height + EV_SPACE;
}

static void eventeditor_CheckButtonToggleCB(UICheckButton *checkbutton, EventFieldCBData *cbData)
{
	if (cbData)
	{
		if (cbData->data)
			*((bool*)cbData->data) = checkbutton->state;
		if (cbData->editor)
			cbData->editor->isCleared = false;
		eventeditor_RefreshOutput(cbData->editor);
	}
}

static void eventeditor_CheckButtonRefresh(UICheckButton *checkbutton)
{
	EventFieldCBData* cbData = (EventFieldCBData*)checkbutton->toggledData;
	if (cbData && cbData->data)
		ui_CheckButtonSetState(checkbutton, *((bool*)cbData->data));
}

static void eventeditor_CheckButtonFreeCB(UICheckButton *checkbutton)
{
	free(checkbutton->toggledData);
}

static int eventeditor_CreateCheckButtonForField(EventEditor *editor, int x, int y, char *labelText, bool* eventField)
{
	UILabel *label = NULL;
	UIWidget *widget = NULL;
	EventFieldCBData *cbData = eventeditor_CreateCBData(editor, eventField);

	label = ui_LabelCreate(labelText, x, y);
	ui_ScrollAreaAddChild(editor->scrollArea, label);
	eaPush(&editor->labels, label);
	widget = (UIWidget*)ui_CheckButtonCreate(x + EV_LABEL_W + EV_SPACE + EV_FIELD_W/2, y, "", false);
	ui_CheckButtonSetToggledCallback((UICheckButton *)widget, eventeditor_CheckButtonToggleCB, cbData);
	ui_WidgetSetFreeCallback(widget, eventeditor_CheckButtonFreeCB);
	ui_ScrollAreaAddChild(editor->scrollArea, widget);
	eaPush(&editor->checkButtons, (UICheckButton*)widget);
	eventeditor_CheckButtonRefresh((UICheckButton*)widget);
	return 	y + UI_WIDGET(label)->height + EV_SPACE;
}

static void eventeditor_TextEntryChangedCB(UITextEntry *entry, EventFieldCBData *cbData)
{
	if (cbData)
	{
		if (cbData->data)
		{
			if (cbData->bPooledString){
				const char *pchText = ui_TextEntryGetText(entry);
				*((char**)cbData->data) = NULL;
				if (pchText && pchText[0]){
					*((const char**)cbData->data) = allocAddString(pchText);
				}
			} else if (cbData->bIsFloat){
				const char *pchText = ui_TextEntryGetText(entry);
				*((F32*)cbData->data) = 0.f;
				if (pchText && pchText[0]){
					*((F32*)cbData->data) = (F32)atof(pchText);
				}
			} else if (cbData->bIsInt){
				const char *pchText = ui_TextEntryGetText(entry);
				*((S32*)cbData->data) = 0;
				if (pchText && pchText[0]){
					*((S32*)cbData->data) = (S32)atoi(pchText);
				}
			} else {
				const char *pchText = ui_TextEntryGetText(entry);
				StructFreeString(*((char**)cbData->data));
				*((char**)cbData->data) = NULL;
				if (pchText && pchText[0]){
					*((char**)cbData->data) = StructAllocString(ui_TextEntryGetText(entry));
				}
			}
		}
		if (cbData->editor)
			cbData->editor->isCleared = false;
		eventeditor_RefreshOutput(cbData->editor);
		eventeditor_RefreshLists(cbData->editor);
	}
}

static void eventeditor_EditableFreeCB(UIEditable *entry)
{
	free(entry->changedData);
}

static void eventeditor_TextEntryRefresh(UIEditable *entry)
{
	EventFieldCBData* cbData = (EventFieldCBData*)entry->changedData;
	if (cbData && cbData->data)
	{
		if (!cbData->bIsFloat && !cbData->bIsInt)
		{
			ui_EditableSetText(entry, *((char**)cbData->data));
		}
		else if (cbData->bIsInt)
		{
			char intString[128] = {0};
			S32 i = *((S32*)cbData->data);
			snprintf(intString, 128, "%d", i);
			ui_EditableSetText(entry, intString );
		}
		else
		{
			char floatString[128] = {0};
			F32 flt = *((F32*)cbData->data);
			snprintf(floatString, 128, "%.3f", flt);
			ui_EditableSetText(entry, floatString );
		}

	}
	
}

static void eventeditor_SwapSourceAndTarget(void *unused, EventEditor *editor)
{
	GameEvent *ev = &editor->ev;
	const char *tmpString;
	TriState tmpBool;
	S32 *pTempArray;

	tmpBool = ev->tMatchSource;
	ev->tMatchSource = ev->tMatchTarget;
	ev->tMatchTarget = tmpBool;

	tmpBool = ev->tMatchSourceTeam;
	ev->tMatchSourceTeam = ev->tMatchTargetTeam;
	ev->tMatchTargetTeam = tmpBool;

	tmpBool = ev->tSourceIsPlayer;
	ev->tSourceIsPlayer = ev->tTargetIsPlayer;
	ev->tTargetIsPlayer = tmpBool;

	tmpString = ev->pchSourceActorName;
	ev->pchSourceActorName = ev->pchTargetActorName;
	ev->pchTargetActorName = (char*)tmpString;

	tmpString = ev->pchSourceCritterName;
	ev->pchSourceCritterName = ev->pchTargetCritterName;
	ev->pchTargetCritterName = tmpString;

	tmpString = ev->pchSourceEncounterName;
	ev->pchSourceEncounterName = ev->pchTargetEncounterName;
	ev->pchTargetEncounterName = tmpString;

	tmpString = ev->pchSourceStaticEncName;
	ev->pchSourceStaticEncName = ev->pchTargetStaticEncName;
	ev->pchTargetStaticEncName = (char*)tmpString;

	tmpString = ev->pchSourceObjectName;
	ev->pchSourceObjectName = ev->pchTargetObjectName;
	ev->pchTargetObjectName = tmpString;

	pTempArray = ev->piSourceCritterTags;
	ev->piSourceCritterTags = ev->piTargetCritterTags;
	ev->piTargetCritterTags = pTempArray;

	// Populate the fields from the Event, clear the Event, and then copy the fields back to the Event.
	// This effectively only copies over fields that are still relevant according to the new EventType.
	eventeditor_RefreshFields(editor);
	eventeditor_UpdateEventFromFields(editor);
}

static int _createTextEntryForField(EventEditor *editor, UIWidget *widget, EventFieldCBData *cbData, 
									int x, int y, char *labelText, void* eventField)
{
	UILabel *label = NULL;
	devassert(widget && cbData);
	label = ui_LabelCreate(labelText, x, y);
	ui_ScrollAreaAddChild(editor->scrollArea, label);
	eaPush(&editor->labels, label);
	
	ui_TextEntrySetChangedCallback((UITextEntry*)widget, eventeditor_TextEntryChangedCB, cbData);
	ui_WidgetSetFreeCallback(widget, eventeditor_EditableFreeCB);
	ui_WidgetSetWidth(widget, EV_FIELD_W);
	ui_ScrollAreaAddChild(editor->scrollArea, widget);
	eaPush(&editor->textEntries, (UIEditable*)widget);
	eventeditor_TextEntryRefresh((UIEditable*)widget);

	return y + widget->height + EV_SPACE;
}

static int eventeditor_CreateFloatEntryForField(EventEditor *editor, int x, int y, char *labelText, F32* eventField)
{
	UIWidget *widget = NULL;
	EventFieldCBData *cbData = eventeditor_CreateCBData(editor, eventField);

	cbData->bIsFloat = true;
	widget = (UIWidget*)ui_TextEntryCreate("", x + EV_LABEL_W + EV_SPACE, y);
		
	return _createTextEntryForField(editor, widget, cbData, x, y, labelText, eventField);
}

static int eventeditor_CreateIntEntryForField(EventEditor *editor, int x, int y, char *labelText, S32* eventField)
{
	UIWidget *widget = NULL;
	EventFieldCBData *cbData = eventeditor_CreateCBData(editor, eventField);

	cbData->bIsInt = true;
	widget = (UIWidget*)ui_TextEntryCreate("", x + EV_LABEL_W + EV_SPACE, y);

	return _createTextEntryForField(editor, widget, cbData, x, y, labelText, eventField);
}

static int eventeditor_CreateTextEntryForField(EventEditor *editor, int x, int y, char *labelText, 
											   const char ***optionsList,
											   const void *dictHandleOrName, char** eventField, bool bPooledString)
{
	UIWidget *widget = NULL;

	EventFieldCBData *cbData = eventeditor_CreateCBData(editor, eventField);
	cbData->bPooledString = bPooledString;

	if (optionsList)
		widget = (UIWidget*)ui_TextEntryCreateWithStringCombo("", x + EV_LABEL_W + EV_SPACE, y, optionsList, true, true, false, true);
	else if (dictHandleOrName)
		widget = (UIWidget*)ui_TextEntryCreateWithGlobalDictionaryCombo("", x + EV_LABEL_W + EV_SPACE, y, dictHandleOrName, "resourceName", true, true, false, true);
	else
		widget = (UIWidget*)ui_TextEntryCreate("", x + EV_LABEL_W + EV_SPACE, y);
	
	return _createTextEntryForField(editor, widget, cbData, x, y, labelText, eventField);
}

static int eventeditor_CreateEventNameEntry(EventEditor *editor, int x, int y)
{
	UIWidget *widget = NULL;
	EventFieldCBData *cbData = eventeditor_CreateCBData(editor, (char**)&editor->ev.pchEventName);
	cbData->bPooledString = true;

	ui_ScrollAreaAddChild(editor->scrollArea, ui_LabelCreate("Name:", x, y));
	widget = (UIWidget*)ui_TextEntryCreate("", x + EV_LABEL_W + EV_SPACE, y);

	ui_TextEntrySetChangedCallback((UITextEntry*)widget, eventeditor_TextEntryChangedCB, cbData);
	ui_TextEntrySetValidateCallback((UITextEntry*)widget, GEValidateNameFunc, NULL);
	ui_WidgetSetFreeCallback(widget, eventeditor_EditableFreeCB);
	ui_WidgetSetWidth(widget, EV_FIELD_W);
	ui_ScrollAreaAddChild(editor->scrollArea, widget);
	eventeditor_TextEntryRefresh((UIEditable*)widget);
	editor->nameEntry = (UITextEntry*)widget;
	return y + widget->height + EV_SPACE;
}

static void eventeditor_Clear(void *unused, EventEditor *editor)
{
	const char *pcMapName = zmapInfoGetPublicName(NULL);
	EventType type = editor->ev.type;
	StructDeInit(parse_GameEvent, &editor->ev);
	StructInit(parse_GameEvent, &editor->ev);
	editor->ev.type = type;
	editor->ev.pchMapName = allocAddString(pcMapName);
	editor->isCleared = true;
	eventeditor_Refresh(editor);
}

static void eventeditor_InputChangedCB(UITextArea *textArea, EventEditor *editor)
{
	const char *pchEventString = ui_TextAreaGetText(textArea);
	if (pchEventString)
	{
		GameEvent *ev = gameevent_EventFromString(pchEventString);
		if (ev)
		{
			// We don't particularly want the Event Name in our struct
			ev->pchEventName = NULL;

			StructCopyAll(parse_GameEvent, ev, &editor->ev);
			StructDestroy(parse_GameEvent, ev);
			editor->isCleared = false;
			eventeditor_Refresh(editor);
		}
		else if (!pchEventString[0])
		{
			eventeditor_Clear(NULL, editor);
		}
	}
	else
	{
		eventeditor_Clear(NULL, editor);
	}
}

static void eventeditor_CreateTextInput(EventEditor *editor)
{
	UILabel *label = NULL;
	UITextArea *textArea = NULL;
	label = ui_LabelCreate("Input:", EV_MARGIN_W, 0);
	ui_ScrollAreaAddChild(editor->scrollArea, label);
	editor->inputLabel = label;
	textArea = ui_TextAreaCreate("");
	ui_TextAreaSetChangedCallback(textArea, eventeditor_InputChangedCB, editor);
	ui_WidgetSetWidth(UI_WIDGET(textArea), EV_FIELD_W + EV_CENTER_SPACE + EV_LABEL_W+EV_SPACE+EV_FIELD_W);
	ui_WidgetSetHeight(UI_WIDGET(textArea), EV_FIELD_W/2);
	UI_WIDGET(textArea)->x = EV_MARGIN_W + EV_LABEL_W + EV_SPACE;
	editor->inputArea = textArea;
	ui_ScrollAreaAddChild(editor->scrollArea, textArea);
}

static void eventeditor_CopyButtonCB(UIButton *button, EventEditor *editor)
{
	const char *pchEventName = editor->ev.pchEventName;
	char *temp = NULL;
	estrStackCreate(&temp);
	editor->ev.pchEventName = NULL;
	gameevent_WriteEventEscaped(&editor->ev, &temp);
	editor->ev.pchEventName = pchEventName;
	winCopyToClipboard(temp);
	estrDestroy(&temp);
}

static void eventeditor_SaveButtonCB(UIButton *button, EventEditor *editor)
{
	if (editor->chainedEventEditor)
	{
		eventeditor_SaveButtonCB(NULL, editor->chainedEventEditor);
	}

	if (editor->boundEvent)
		StructCopyAll(parse_GameEvent, &editor->ev, editor->boundEvent);
	if (editor->changeFunc)
		editor->changeFunc(editor, editor->changeData);
}

static void eventeditor_CreateTextOutput(EventEditor *editor)
{
	UILabel *label = NULL;
	UITextArea *textArea = NULL;
	UIButton *button = NULL;

	label = ui_LabelCreate("Output:", EV_MARGIN_W, 0);
	ui_ScrollAreaAddChild(editor->scrollArea, label);
	editor->outputLabel = label;
	textArea = ui_TextAreaCreate("");
	ui_WidgetSetWidth(UI_WIDGET(textArea), EV_FIELD_W + EV_CENTER_SPACE + EV_LABEL_W+EV_SPACE+EV_FIELD_W);
	ui_WidgetSetHeight(UI_WIDGET(textArea), EV_FIELD_W);
	ui_SetActive(UI_WIDGET(textArea), false); //--doesn't let you select, either
	UI_WIDGET(textArea)->x = EV_MARGIN_W + EV_LABEL_W + EV_SPACE;
	editor->outputArea = textArea;
	ui_ScrollAreaAddChild(editor->scrollArea, textArea);

	button = ui_ButtonCreate("Save", textArea->widget.x + textArea->widget.width - 80, 0, eventeditor_SaveButtonCB, editor);
	ui_ScrollAreaAddChild(editor->scrollArea, button);
	button->widget.width = 80;
	editor->saveButton = button;
	
	button = ui_ButtonCreate("Copy", textArea->widget.x + textArea->widget.width - 80 - EV_SPACE - 80, 0, eventeditor_CopyButtonCB, editor);
	ui_ScrollAreaAddChild(editor->scrollArea, button);
	button->widget.width = 80;
	editor->outputCopyButton = button;
}

static void eventeditor_RefreshOutput(EventEditor* editor)
{
	char *estrBuffer = NULL;
	if (editor->outputArea)
	{
		estrStackCreate(&estrBuffer);
		if (!editor->isCleared){
			const char *pchEventName = editor->ev.pchEventName;
			editor->ev.pchEventName = NULL;
			gameevent_WriteEvent(&editor->ev, &estrBuffer);
			editor->ev.pchEventName = pchEventName;
		}else{
			estrClear(&estrBuffer);
		}
		ui_TextAreaSetText(editor->outputArea, estrBuffer);
		estrDestroy(&estrBuffer);
	}
}

static void eventeditor_RefreshFields(EventEditor* editor)
{
	int i, n;

	// Refresh lists that populate combos
	eventeditor_RefreshLists(editor);
		
	n = eaSize(&editor->checkButtons);
	for (i = 0; i < n; i++)
		eventeditor_CheckButtonRefresh(editor->checkButtons[i]);

	n = eaSize(&editor->comboBoxes);
	for (i = 0; i < n; i++)
		eventeditor_ComboRefresh(editor->comboBoxes[i]);
	
	n = eaSize(&editor->textEntries);
	for (i = 0; i < n; i++)
		eventeditor_TextEntryRefresh(editor->textEntries[i]);

	// Refresh output area text
	eventeditor_RefreshOutput(editor);
}

static void eventeditor_DestroyFields(EventEditor *editor)
{
	int i, n = eaSize(&editor->checkButtons);
	for (i = 0; i < n; i++)
		ui_ScrollAreaRemoveChild(editor->scrollArea, editor->checkButtons[i]);
	eaDestroyEx(&editor->checkButtons, ui_WidgetQueueFree);

	n = eaSize(&editor->comboBoxes);
	for (i = 0; i < n; i++)
		ui_ScrollAreaRemoveChild(editor->scrollArea, editor->comboBoxes[i]);
	eaDestroyEx(&editor->comboBoxes, ui_WidgetQueueFree);
	
	n = eaSize(&editor->textEntries);
	for (i = 0; i < n; i++)
		ui_ScrollAreaRemoveChild(editor->scrollArea, editor->textEntries[i]);
	eaDestroyEx(&editor->textEntries, ui_WidgetQueueFree);

	n = eaSize(&editor->labels);
	for (i = 0; i < n; i++)
		ui_ScrollAreaRemoveChild(editor->scrollArea, editor->labels[i]);
	eaDestroyEx(&editor->labels, ui_WidgetQueueFree);

	n = eaSize(&editor->separators);
	for (i = 0; i < n; i++)
		ui_ScrollAreaRemoveChild(editor->scrollArea, editor->separators[i]);
	eaDestroyEx(&editor->separators, ui_WidgetQueueFree);

	if (editor->swapButton)
	{
		ui_ScrollAreaRemoveChild(editor->scrollArea, editor->swapButton);
		ui_WidgetQueueFree(UI_WIDGET(editor->swapButton));
		editor->swapButton = NULL;
	}

	if (editor->descriptionArea)
	{
		ui_ScrollAreaRemoveChild(editor->scrollArea, editor->descriptionArea);
		ui_WidgetQueueFree(UI_WIDGET(editor->descriptionArea));
		editor->descriptionArea = NULL;
	}
}

static void eventeditor_EventChangedCB(EventEditor *pEventEditor, EventEditor *editor)
{
	if (editor->chainedEventEditor && editor->ev.pChainEventDef)
	{
		GameEvent *pEvent = eventeditor_GetBoundEvent(editor->chainedEventEditor);
		
		if (StructCompare(parse_GameEvent, editor->ev.pChainEventDef, pEvent, 0, 0, 0) != 0)
		{
			StructCopy(parse_GameEvent, pEvent, editor->ev.pChainEventDef, 0, 0, 0);
			eventeditor_Refresh(editor);
		}

		eventeditor_Destroy(editor->chainedEventEditor);
		editor->chainedEventEditor = NULL;
	}
}

static void eventeditor_EventEditorClosedCB(EventEditor *pEventEditor, EventEditor *editor)
{
	if (editor)
	{
		eventeditor_Destroy(editor->chainedEventEditor);
		editor->chainedEventEditor = NULL;
	}
}


static void eventeditor_OpenChainedEventEditor(UIButton *pButton, EventEditor *editor)
{
	if (!editor)
		return;

	if (!editor->ev.pChainEventDef)
	{
		editor->ev.pChainEventDef = StructAlloc(parse_GameEvent);
		if (!editor->ev.pChainEventDef)
			return;
	}

	if (!editor->chainedEventEditor)
	{
		editor->chainedEventEditor = eventeditor_CreateConst(editor->ev.pChainEventDef, eventeditor_EventChangedCB, editor, false);
		
		if (!editor->chainedEventEditor)
			return;

		eventeditor_Open(editor->chainedEventEditor);
		eventeditor_SetCloseFunc(editor->chainedEventEditor, eventeditor_EventEditorClosedCB, editor);

		if (editor->window && editor->chainedEventEditor->window)
		{
			Vec2 vWindowPos;
			vWindowPos[0] = ui_WidgetGetX(UI_WIDGET(editor->window)) + 50.f;;
			vWindowPos[1] = ui_WidgetGetY(UI_WIDGET(editor->window)) + 50.f;;
			ui_WidgetSetPosition(UI_WIDGET(editor->chainedEventEditor->window), vWindowPos[0], vWindowPos[1]);
		}
		
	}
}

static void eventeditor_RemoveChainedEvent(UIButton *pButton, EventEditor *editor)
{
	if (editor->chainedEventEditor)
	{
		// 
		eventeditor_Destroy(editor->chainedEventEditor);
		editor->chainedEventEditor = NULL;
	}

	if (editor->ev.pChainEventDef)
	{
		StructDestroy(parse_GameEvent, editor->ev.pChainEventDef);
		editor->ev.pChainEventDef = NULL;

		eventeditor_Refresh(editor);
	}
	
}

static F32 eventeditor_CreateSeperator(EventEditor *editor, F32 currY)
{
	UISeparator *pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, currY);
	
	ui_ScrollAreaAddChild(editor->scrollArea, pSeparator);
	eaPush(&editor->separators, pSeparator);
	return currY + EV_SPACE*2;
}

// returns the next currY
static int eventeditor_CreateChainedEventButton(EventEditor *editor, int currY)
{
	// Update Chained Event button
	if (!editor->pChainedEventButton)
	{
		currY = eventeditor_CreateSeperator(editor, currY);

		editor->pChainedEventButton = ui_ButtonCreate("", EV_MARGIN_W+EV_LABEL_W+EV_SPACE, currY, eventeditor_OpenChainedEventEditor, editor);
		ui_ScrollAreaAddChild(editor->scrollArea, editor->pChainedEventButton);
		ui_WidgetSetWidthEx(UI_WIDGET(editor->pChainedEventButton), 1.0, UIUnitPercentage);
		ui_WidgetSetPaddingEx(UI_WIDGET(editor->pChainedEventButton), 0, 86, 0, 0);

		// I'm using a label to display the text since I don't want the text centered
		editor->pChainEventBtnLabel = ui_LabelCreate("", 0, 0);
		ui_WidgetSetDimensionsEx(UI_WIDGET(editor->pChainEventBtnLabel), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
		ui_WidgetAddChild(UI_WIDGET(editor->pChainedEventButton), UI_WIDGET(editor->pChainEventBtnLabel));

		editor->pChainEventLabel = ui_LabelCreate("Chained Event", EV_MARGIN_W, currY);
		ui_ScrollAreaAddChild(editor->scrollArea, editor->pChainEventLabel);
		currY += editor->pChainedEventButton->widget.height + EV_SPACE;

		if (editor->ev.pChainEventDef)
			currY = eventeditor_CreateFloatEntryForField(editor, EV_MARGIN_W, currY, "Chain Time", &editor->ev.fChainTime);

		editor->pChainedEventRemoveBtn = ui_ButtonCreate("Remove Chained Event", EV_MARGIN_W+EV_LABEL_W+EV_SPACE, currY, eventeditor_RemoveChainedEvent, editor);
		ui_ScrollAreaAddChild(editor->scrollArea, editor->pChainedEventRemoveBtn);
		currY += editor->pChainedEventRemoveBtn->widget.height + EV_SPACE;


		currY = eventeditor_CreateSeperator(editor, currY);

	} 
	else if (editor->pChainedEventButton)
	{
		currY = eventeditor_CreateSeperator(editor, currY);

		ui_WidgetSetPosition(UI_WIDGET(editor->pChainedEventButton), EV_MARGIN_W+EV_LABEL_W+EV_SPACE, currY);
		currY += editor->pChainedEventButton->widget.height + EV_SPACE;

		ui_WidgetSetPosition(UI_WIDGET(editor->pChainedEventRemoveBtn), EV_MARGIN_W+EV_LABEL_W+EV_SPACE, currY);
		currY += editor->pChainedEventRemoveBtn->widget.height + EV_SPACE;

		if (editor->ev.pChainEventDef)
			currY = eventeditor_CreateFloatEntryForField(editor, EV_MARGIN_W, currY, "Chain Time", &editor->ev.fChainTime);

		currY = eventeditor_CreateSeperator(editor, currY);

		//ui_WidgetSetPosition(UI_WIDGET(editor->pChainedEventButton), EV_MARGIN_W+EV_LABEL_W+EV_SPACE, currY);
		//currY += editor->pChainedEventButton->widget.height + EV_SPACE;

		//ui_WidgetSetPosition(UI_WIDGET(editor->pChainedEventRemoveBtn), EV_MARGIN_W+EV_LABEL_W+EV_SPACE, currY);
		//currY += editor->pChainedEventRemoveBtn->widget.height + EV_SPACE;
	}

	

	// Update text on Chained Event button
	if (editor->ev.pChainEventDef)
	{
		if (editor->pChainEventBtnLabel)
		{
			GameEvent *pChainedEvent = editor->ev.pChainEventDef;
			char *estr = NULL;
			estrStackCreate(&estr);
			gameevent_WriteEventSingleLine(pChainedEvent, &estr);
			ui_LabelSetText(editor->pChainEventBtnLabel, estr);
			ui_WidgetSetTooltipString(UI_WIDGET(editor->pChainedEventButton), estr);
			estrDestroy(&estr);
		}
		
		if (editor->pChainedEventRemoveBtn)
			ui_SetActive(UI_WIDGET(editor->pChainedEventRemoveBtn), true);
	} 
	else 
	{
		if (editor->pChainEventBtnLabel)
			ui_LabelSetText(editor->pChainEventBtnLabel, "Create Event");
		if (editor->pChainedEventRemoveBtn)
			ui_SetActive(UI_WIDGET(editor->pChainedEventRemoveBtn), false);
	}
	
	
	return currY;
}


// returns the next currY
static void eventeditor_UpdateChainedEventButton(EventEditor *editor)
{
	// Update text on Chained Event button
	if (editor->ev.pChainEventDef)
	{
		if (editor->pChainEventBtnLabel)
		{
			GameEvent *pChainedEvent = editor->ev.pChainEventDef;
			char *estr = NULL;
			estrStackCreate(&estr);
			gameevent_WriteEventSingleLine(pChainedEvent, &estr);
			ui_LabelSetText(editor->pChainEventBtnLabel, estr);
			ui_WidgetSetTooltipString(UI_WIDGET(editor->pChainedEventButton), estr);
			estrDestroy(&estr);
		}

		if (editor->pChainedEventRemoveBtn)
			ui_SetActive(UI_WIDGET(editor->pChainedEventRemoveBtn), true);
	} 
	else 
	{
		if (editor->pChainEventBtnLabel)
			ui_LabelSetText(editor->pChainEventBtnLabel, "Create Event");
		if (editor->pChainedEventRemoveBtn)
			ui_SetActive(UI_WIDGET(editor->pChainedEventRemoveBtn), false);
	}

	 
}

static int eventeditor_CreateFields(EventEditor *editor)
{
	int currY = editor->typeCombo->widget.y + editor->typeCombo->widget.height + EV_SPACE;
	int nextY = currY;
	int sourceTargetTopY = 0;
	UIWidget *widget = NULL;
	UILabel *label = NULL;
	UISeparator *sourceTargetSeparator = NULL;
	const GameEventTypeInfo* typeInfo = gameevent_GetTypeInfo(editor->ev.type);
	bool hasSource = typeInfo->hasSourceCritter || typeInfo->hasSourceEncounter || typeInfo->hasSourcePlayer;
	bool hasTarget = typeInfo->hasTargetCritter || typeInfo->hasTargetEncounter || typeInfo->hasTargetPlayer;


	// Description
	if (typeInfo->pchDescription)
	{
		F32 descriptionHeight = 0.0f;
		currY += EV_SPACE;
		editor->descriptionArea = ui_TextAreaCreate(typeInfo->pchDescription);
		ui_ScrollAreaAddChild(editor->scrollArea, editor->descriptionArea);
		ui_WidgetSetPosition(UI_WIDGET(editor->descriptionArea), EV_MARGIN_W+EV_LABEL_W+EV_SPACE, currY);
		ui_WidgetSetWidth(UI_WIDGET(editor->descriptionArea), EV_WINDOW_DEFAULT_W-2*EV_MARGIN_W-EV_LABEL_W-2*EV_SPACE-editor->clearButton->widget.width);
		ui_TextAreaReflow(editor->descriptionArea, EV_WINDOW_DEFAULT_W-2*EV_MARGIN_W-EV_LABEL_W-2*EV_SPACE-editor->clearButton->widget.width, 1.f);
		ui_SetActive(UI_WIDGET(editor->descriptionArea), false);

		descriptionHeight = ui_TextAreaGetLines(editor->descriptionArea)*ui_StyleFontLineHeight(NULL, 1.f) + 10;
		ui_WidgetSetHeight(UI_WIDGET(editor->descriptionArea), descriptionHeight);

		currY += editor->descriptionArea->widget.height;
	}

	currY += EV_SPACE*3;

	//
	currY = eventeditor_CreateChainedEventButton(editor, currY);
	
	// Source and Target labels
	if (hasSource)
	{
		label = ui_LabelCreate("Source:", EV_MARGIN_W, currY);
		ui_ScrollAreaAddChild(editor->scrollArea, label);
		eaPush(&editor->labels, label);
		hasSource = true;
	}
	if (hasTarget)
	{
		label = ui_LabelCreate("Target:", EV_RIGHT_COLUMN_OFFSET, currY);
		ui_ScrollAreaAddChild(editor->scrollArea, label);
		eaPush(&editor->labels, label);
		hasTarget = true;
	}
	if (hasSource || hasTarget)
		currY += label->widget.height + EV_SPACE;
	nextY = currY;
	sourceTargetTopY = currY;

	// MatchSource/Target field
	// TODO - Figure out how to disable these based on which editor the user is in
	if (typeInfo->hasSourcePlayer && !typeInfo->hasSourceEncounter)
		nextY = eventeditor_CreateComboForTriState(editor, EV_MARGIN_W, currY, "Current Player:", &editor->ev.tMatchSource);
	else if (!typeInfo->hasSourcePlayer && typeInfo->hasSourceEncounter)
		nextY = eventeditor_CreateComboForTriState(editor, EV_MARGIN_W, currY, "Current Encounter:", &editor->ev.tMatchSource);
	else if (typeInfo->hasSourcePlayer && typeInfo->hasSourceEncounter)
		nextY = eventeditor_CreateComboForTriState(editor, EV_MARGIN_W, currY, "Current Player/Encounter:", &editor->ev.tMatchSource);

	if (typeInfo->hasTargetPlayer && !typeInfo->hasTargetEncounter)
		nextY = eventeditor_CreateComboForTriState(editor, EV_RIGHT_COLUMN_OFFSET, currY, "Current Player:", &editor->ev.tMatchTarget);
	else if (!typeInfo->hasTargetPlayer && typeInfo->hasTargetEncounter)
		nextY = eventeditor_CreateComboForTriState(editor, EV_RIGHT_COLUMN_OFFSET, currY, "Current Encounter:", &editor->ev.tMatchTarget);
	else if (typeInfo->hasTargetPlayer && typeInfo->hasTargetEncounter)
		nextY = eventeditor_CreateComboForTriState(editor, EV_RIGHT_COLUMN_OFFSET, currY, "Current Player/Encounter:", &editor->ev.tMatchTarget);
	currY = nextY;

	// MatchSourceTeam/TargetTeam field
	// TODO - Figure out how to disable these based on which editor the user is in
	if (typeInfo->hasSourcePlayerTeam)
		nextY = eventeditor_CreateComboForTriState(editor, EV_MARGIN_W, currY, "Current Player's Team:", &editor->ev.tMatchSourceTeam);
	if (typeInfo->hasTargetPlayerTeam)
		nextY = eventeditor_CreateComboForTriState(editor, EV_RIGHT_COLUMN_OFFSET, currY, "Current Player's Team:", &editor->ev.tMatchTargetTeam);
	currY = nextY;

	// bSourceIsPlayer field
	if (typeInfo->hasSourcePlayer && typeInfo->hasSourceCritter)
		nextY = eventeditor_CreateComboForTriState(editor, EV_MARGIN_W, currY, "Is a Player:", &editor->ev.tSourceIsPlayer);
	if (typeInfo->hasTargetPlayer && typeInfo->hasTargetCritter)
		nextY = eventeditor_CreateComboForTriState(editor, EV_RIGHT_COLUMN_OFFSET, currY, "Is a Player:", &editor->ev.tTargetIsPlayer);
	currY = nextY;

	// 
	if (typeInfo->hasSourcePlayer)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Class Name:", NULL, "CharacterClass", (char**)&editor->ev.pchSourceClassName, true);
	if (typeInfo->hasTargetPlayer)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_RIGHT_COLUMN_OFFSET, currY, "Class Name:", NULL, "CharacterClass", (char**)&editor->ev.pchTargetClassName, true);
	currY = nextY;

	// Actor name
	if (typeInfo->hasSourceCritter)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Actor:", &editor->sourceActorList, NULL, (char**)&editor->ev.pchSourceActorName, true);
	if (typeInfo->hasTargetCritter)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_RIGHT_COLUMN_OFFSET, currY, "Actor:", &editor->targetActorList, NULL, (char**)&editor->ev.pchTargetActorName, true);
	currY = nextY;

	// Static Encounter name
	if (typeInfo->hasSourceCritter || typeInfo->hasSourceEncounter)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Static Encounter:", &editor->staticEncList, NULL, (char**)&editor->ev.pchSourceStaticEncName, true);
	if (typeInfo->hasTargetCritter || typeInfo->hasTargetEncounter)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_RIGHT_COLUMN_OFFSET, currY, "Static Encounter:", &editor->staticEncList, NULL, (char**)&editor->ev.pchTargetStaticEncName, true);
	currY = nextY;

	// Encounter Group name
	if (typeInfo->hasSourceCritter || typeInfo->hasSourceEncounter)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Encounter Group:", &editor->eaEncGroupList, NULL, (char**)&editor->ev.pchSourceEncGroupName, true);
	if (typeInfo->hasTargetCritter || typeInfo->hasTargetEncounter)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_RIGHT_COLUMN_OFFSET, currY, "Encounter Group:", &editor->eaEncGroupList, NULL, (char**)&editor->ev.pchTargetEncGroupName, true);
	currY = nextY;

	// Critter name
	if (typeInfo->hasSourceCritter)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "CritterDef:", NULL, "CritterDef", (char**)&editor->ev.pchSourceCritterName, true);
	if (typeInfo->hasTargetCritter)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_RIGHT_COLUMN_OFFSET, currY, "CritterDef:", NULL, "CritterDef", (char**)&editor->ev.pchTargetCritterName, true);
	currY = nextY;

	// Critter Group name
	if (typeInfo->hasSourceCritter)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Critter Group:", NULL, "CritterGroup", (char**)&editor->ev.pchSourceCritterGroupName, true);
	if (typeInfo->hasTargetCritter)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_RIGHT_COLUMN_OFFSET, currY, "Critter Group:", NULL, "CritterGroup", (char**)&editor->ev.pchTargetCritterGroupName, true);
	currY = nextY;

	// critter tags
	if (typeInfo->hasSourceCritter)
		nextY = eventeditor_CreateComboForField(editor, EV_MARGIN_W, currY, "Critter Tags:", CritterTagsEnum, NULL, (int**)&editor->ev.piSourceCritterTags, NULL, false);
	if (typeInfo->hasTargetCritter)
		nextY = eventeditor_CreateComboForField(editor, EV_RIGHT_COLUMN_OFFSET, currY, "Critter Tags:", CritterTagsEnum, NULL, (int**)&editor->ev.piTargetCritterTags, NULL, false);
	currY = nextY;


	// EncounterDef name
	if (typeInfo->hasSourceCritter || typeInfo->hasSourceEncounter)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "EncounterDef:", &editor->encounterList, "EncounterDef", (char**)&editor->ev.pchSourceEncounterName, true);
	if (typeInfo->hasTargetCritter || typeInfo->hasTargetEncounter)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_RIGHT_COLUMN_OFFSET, currY, "EncounterDef:", &editor->encounterList, "EncounterDef", (char**)&editor->ev.pchTargetEncounterName, true);
	currY = nextY;

	// Object name
	if (typeInfo->hasSourceCritter)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Object Name:", NULL, NULL, (char**)&editor->ev.pchSourceObjectName, true);
	if (typeInfo->hasTargetCritter)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_RIGHT_COLUMN_OFFSET, currY, "Object Name:", NULL, NULL, (char**)&editor->ev.pchTargetObjectName, true);
	currY = nextY;

	// Faction name
	if (typeInfo->hasSourceCritter || typeInfo->hasSourcePlayer)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Faction Name:", NULL, "CritterFaction", (char**)&editor->ev.pchSourceFactionName, true);
	if (typeInfo->hasTargetCritter || typeInfo->hasTargetPlayer)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_RIGHT_COLUMN_OFFSET, currY, "Faction Name:", NULL, "CritterFaction", (char**)&editor->ev.pchTargetFactionName, true);
	currY = nextY;

	// Allegiance name
	if (typeInfo->hasSourceCritter || typeInfo->hasSourcePlayer)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Allegiance Name:", NULL, "Allegiance", (char**)&editor->ev.pchSourceAllegianceName, true);
	if (typeInfo->hasTargetCritter || typeInfo->hasTargetPlayer)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_RIGHT_COLUMN_OFFSET, currY, "Allegiance Name:", NULL, "Allegiance", (char**)&editor->ev.pchTargetAllegianceName, true);
	currY = nextY;

	// Critter Rank
	if (typeInfo->hasSourceCritter)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Critter Rank:", &g_eaCritterRankNames, NULL, (char**)&editor->ev.pchSourceRank, true);
	if (typeInfo->hasTargetCritter)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_RIGHT_COLUMN_OFFSET, currY, "Critter Rank:", &g_eaCritterRankNames, NULL, (char**)&editor->ev.pcTargetRank, true);
	currY = nextY;

	// Region type
	if (typeInfo->hasSourceCritter || typeInfo->hasSourcePlayer)
		nextY = eventeditor_CreateComboForField(editor, EV_MARGIN_W, currY, "Region Type:", WorldRegionTypeEnum, (int*)&editor->ev.eSourceRegionType, NULL, NULL, true);
	if (typeInfo->hasTargetCritter || typeInfo->hasTargetPlayer)
		nextY = eventeditor_CreateComboForField(editor, EV_RIGHT_COLUMN_OFFSET, currY, "Region Type:", WorldRegionTypeEnum, (int*)&editor->ev.eSourceRegionType, NULL, NULL, true);
	currY = nextY;

	// power mode
	if (typeInfo->hasSourceCritter || typeInfo->hasSourcePlayer)
		nextY = eventeditor_CreateComboForField(editor, EV_MARGIN_W, currY, "Power Mode:", PowerModeEnum, NULL, NULL, (char**)&editor->ev.pchSourcePowerMode, true);
	if (typeInfo->hasTargetCritter || typeInfo->hasTargetPlayer)
		nextY = eventeditor_CreateComboForField(editor, EV_RIGHT_COLUMN_OFFSET, currY, "Power Mode:", PowerModeEnum, NULL, NULL, (char**)&editor->ev.pchTargetPowerMode, true);
	currY = nextY;
	
	// Nemesis minion type name (target only)
	if (typeInfo->hasTargetCritter)
		nextY = eventeditor_CreateTextEntryForField(editor, EV_RIGHT_COLUMN_OFFSET, currY, "Nemesis Minion:", &g_eaNemesisCostumeNames, NULL, (char**)&editor->ev.pchNemesisType, true);
	currY = nextY;

	// Add the Swap Source/Target button if needed
	if (hasSource && hasTarget)
	{
		UIButton *swapButton = ui_ButtonCreate("<--->", EV_RIGHT_COLUMN_OFFSET - EV_CENTER_SPACE, sourceTargetTopY + (currY - sourceTargetTopY)/2 - EV_CENTER_SPACE/2, eventeditor_SwapSourceAndTarget, editor);
		ui_WidgetSetWidth(UI_WIDGET(swapButton), EV_CENTER_SPACE-EV_SPACE*2);
		ui_WidgetSetHeight(UI_WIDGET(swapButton), EV_CENTER_SPACE-EV_SPACE*2);
		ui_ScrollAreaAddChild(editor->scrollArea, swapButton);
		editor->swapButton = swapButton;
	}

	// End Source and Target fields
	currY += EV_SPACE;
	if (hasSource || hasTarget)
	{
		sourceTargetSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(sourceTargetSeparator), 0, currY);
		currY += EV_SPACE*2;
	}
	nextY = currY;

	// Mission RefString
	if (typeInfo->hasMissionRefString){
		eventeditor_CreateCheckButtonForField(editor, EV_RIGHT_COLUMN_OFFSET, currY, "Root Missions Only", &editor->ev.bIsRootMission);
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Mission:", NULL, ALL_MISSIONS_INDEX, (char**)&editor->ev.pchMissionRefString, true);
	}

	if (typeInfo->hasMissionRefString || typeInfo->hasPvPQueueMatchResult) 
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Mission Category:", NULL, "MissionCategory", (char**)&editor->ev.pchMissionCategoryName, true);

	if (typeInfo->hasMissionRefString)
		eventeditor_CreateCheckButtonForField(editor, EV_RIGHT_COLUMN_OFFSET, currY, "Use Tracked Mission", &editor->ev.bIsTrackedMission);

	// Contact Name
	if (typeInfo->hasContactName)
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Contact:", NULL, "Contact", (char**)&editor->ev.pchContactName, false);

	// Store Name
	if (typeInfo->hasStoreName)
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Store:", NULL, "Store", (char**)&editor->ev.pchStoreName, false);

	// Item name
	if (typeInfo->hasItemName)
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Item:", NULL, "ItemDef", (char**)&editor->ev.pchItemName, true);

	// Gem name
	if (typeInfo->hasGemName)
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Gem:", NULL, "ItemDef", (char**)&editor->ev.pchGemName, true);

	// Clickable name
	if (typeInfo->hasClickableName)
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Clickable:", &editor->clickableList, NULL, (char**)&editor->ev.pchClickableName, false);

	// Clickable Group name
	if (typeInfo->hasClickableName)
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "ClickableGroup:", &editor->clickableGroupList, NULL, (char**)&editor->ev.pchClickableGroupName, false);

	// Volume name
	if (typeInfo->hasVolumeName)
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Volume:", &editor->volumeList, NULL, (char**)&editor->ev.pchVolumeName, false);

	// Cutscene name
	if (typeInfo->hasCutsceneName)
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Cutscene Name:", NULL, NULL, (char**)&editor->ev.pchCutsceneName, false);

	// Video name
	if (typeInfo->hasVideoName)
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Video Name:", NULL, NULL, (char**)&editor->ev.pchVideoName, false);

	// FSM name
	if (typeInfo->hasFSMName)
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Root FSM Name:", NULL, gFSMDict, (char**)&editor->ev.pchFSMName, false);

	// FSM state
	if (typeInfo->hasFsmStateName)
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "FSM State:", &editor->fsmStateList, NULL, (char**)&editor->ev.pchFsmStateName, false);

	// PowerDef
	if (typeInfo->hasPower)
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "PowerDef:", NULL, "PowerDef", (char**)&editor->ev.pchPowerName, false);

	if (typeInfo->hasPower)
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "PowerEventName:", NULL, NULL, (char**)&editor->ev.pchPowerEventName, false);

	// Damage Type
	if (typeInfo->hasDamageType)
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Damage Type:", &g_DamageTypeNames.ppchNames, NULL, (char**)&editor->ev.pchDamageType, false);

	// Attrib Type (Healing)
	if (typeInfo->hasAttribType)
		currY = eventeditor_CreateComboForField(editor, EV_MARGIN_W, currY, "Attrib Type:", AttribTypeEnum, NULL, NULL, (char**)&editor->ev.pchDamageType, true);

	// Dialog Name
	if (typeInfo->hasDialogName)
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Dialog Name:", &editor->dialogList, NULL, (char**)&editor->ev.pchDialogName, false);

	// Emote Name
	if (typeInfo->hasEmoteName)
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Emote Name:", NULL, NULL, (char**)&editor->ev.pchEmoteName, false);

	// ItemAssignment Name
	if (typeInfo->hasItemAssignmentName)
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Assignment Name:", NULL, "ItemAssignmentDef", (char**)&editor->ev.pchItemAssignmentName, true);
	
	// ItemAssignment Outcome
	if (typeInfo->hasItemAssignmentOutcome)
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Assign. Outcome:", NULL, NULL, (char**)&editor->ev.pchItemAssignmentOutcome, true);

	// ItemAssignment Outcome
	if (typeInfo->hasItemAssignmentSpeedBonus)
		currY = eventeditor_CreateFloatEntryForField(editor, EV_MARGIN_W, currY, "Assign. SpeedBonus:", &editor->ev.fItemAssignmentSpeedBonus);

	// Message
	if (typeInfo->hasMessage)
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Message:", NULL, NULL, (char**)&editor->ev.pchMessage, false);

	// State combos (MissionState, EncounterState, etc)
	if (typeInfo->hasMissionState)
		currY = eventeditor_CreateComboForField(editor, EV_MARGIN_W, currY, "Mission State:", MissionStateEnum, (int*)&editor->ev.missionState, NULL, NULL, true);
	if (typeInfo->hasMissionRefString)
		currY = eventeditor_CreateComboForField(editor, EV_MARGIN_W, currY, "Mission Type:", MissionTypeEnum, (int*)&editor->ev.missionType, NULL, NULL, true);
	if (typeInfo->hasMissionLockoutState)
		currY = eventeditor_CreateComboForField(editor, EV_MARGIN_W, currY, "Lockout State:", MissionLockoutStateEnum, (int*)&editor->ev.missionLockoutState, NULL, NULL, true);
	if (typeInfo->hasEncState)
		currY = eventeditor_CreateComboForField(editor, EV_MARGIN_W, currY, "Encounter State:", EncounterStateEnum, (int*)&editor->ev.encState, NULL, NULL, true);
	if (typeInfo->hasHealthState)
		currY = eventeditor_CreateComboForField(editor, EV_MARGIN_W, currY, "Health Percent:", HealthStateEnum, (int*)&editor->ev.healthState, NULL, NULL, true);
	if (typeInfo->hasNemesisState)
		currY = eventeditor_CreateComboForField(editor, EV_MARGIN_W, currY, "Nemesis State:", NemesisStateEnum, (int*)&editor->ev.nemesisState, NULL, NULL, true);
	if (typeInfo->hasVictoryType)
		currY = eventeditor_CreateComboForField(editor, EV_MARGIN_W, currY, "Victory Type:", PVPDuelVictoryTypeEnum, (int*)&editor->ev.victoryType, NULL, NULL, true);

	// PvP Queue Match Result
	if (typeInfo->hasPvPQueueMatchResult)
		currY = eventeditor_CreateComboForField(editor, EV_MARGIN_W, currY, "Match Result:", PvPQueueMatchResultEnum, (int*)&editor->ev.ePvPQueueMatchResult, NULL, NULL, true);

	// PvP Event type
	if (typeInfo->hasPvPEventType)
		currY = eventeditor_CreateComboForField(editor, EV_MARGIN_W, currY, "PVP Event Type:",PvPEventEnum, (int*)&editor->ev.ePvPEvent, NULL, NULL, false);

	// Part of UGC project flag
	if (typeInfo->hasUGCProject)
		currY = eventeditor_CreateComboForTriState(editor, EV_MARGIN_W, currY, "Part of UGC Project:", &editor->ev.tPartOfUGCProject);

	if (typeInfo->hasUGCProjectData) {
		currY = eventeditor_CreateComboForTriState(editor, EV_MARGIN_W, currY, "UGC Featured Currently:", &editor->ev.tUGCFeaturedCurrently);
		currY = eventeditor_CreateComboForTriState(editor, EV_MARGIN_W, currY, "UGC Featured Previously:", &editor->ev.tUGCFeaturedPreviously);
		currY = eventeditor_CreateComboForTriState(editor, EV_MARGIN_W, currY, "UGC Qualifies for Reward:", &editor->ev.tUGCProjectQualifiesForReward);
	}
	
	// Minigame Type
	if (typeInfo->hasMinigameType)
		currY = eventeditor_CreateComboForField(editor, EV_MARGIN_W, currY, "Minigame Type:", MinigameTypeEnum, (int*)&editor->ev.eMinigameType, NULL, NULL, false);

	// Item categories: allow the user to specify up to one category to match
	if (typeInfo->hasItemCategories)
		currY = eventeditor_CreateComboForField(editor, EV_MARGIN_W, currY, "Item Category:", ItemCategoryEnum, NULL, (int**)&editor->ev.eaItemCategories, NULL, false);

	// Map selector
	if (typeInfo->hasMap)
	{
		GERefreshMapNamesList();
		eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Map Name:", &g_GEMapDispNames, NULL, (char**)&editor->ev.pchMapName, true);

		// bSourceIsPlayer checkbox
		currY = eventeditor_CreateComboForTriState(editor, EV_RIGHT_COLUMN_OFFSET, currY, "Owned by current player:", &editor->ev.tMatchMapOwner);

		// Door Key name
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Door Key:", NULL, NULL, (char**)&editor->ev.pchDoorKey, false);
	}
//Disabling this for now since 0 is the top reward tier, so I am not sure how we want to handle that
	/*if (typeInfo->hasContestRewardTier)
	{
		currY = eventeditor_CreateIntEntryForField(editor, EV_MARGIN_W, currY, "Reward Tier:", (S32 *)&editor->ev.iRewardTier);
	}*/

	if (typeInfo->hasScoreboardMetricName)
	{
		currY = eventeditor_CreateTextEntryForField(editor, EV_MARGIN_W, currY, "Metric Name:", NULL, NULL, (char**)&editor->ev.pchScoreboardMetricName, true);
	}

	
	if (typeInfo->hasScoreboardRank)
	{
		currY = eventeditor_CreateIntEntryForField(editor, EV_MARGIN_W, currY, "Scoreboard Rank:", (S32 *)&editor->ev.iScoreboardRank);
	}

	if (sourceTargetSeparator && currY == nextY) // hack - if this is true, no fields have been added since the separator after Source/Target
	{
		ui_WidgetQueueFree(UI_WIDGET(sourceTargetSeparator));
	}
	else if (sourceTargetSeparator)
	{
		ui_ScrollAreaAddChild(editor->scrollArea, sourceTargetSeparator);
		eaPush(&editor->separators, sourceTargetSeparator);
	}

	currY += EV_SPACE*3;

	return currY;
}

static void eventeditor_Zoom(EventEditor *editor, int step)
{
	F32 fScale = CLAMP(editor->scrollArea->childScale + (0.1 * step), 0.20, 2.0);
	ui_ScrollAreaSetChildScale(editor->scrollArea, fScale);
}

static void eventeditor_ZoomOutCB(UIButton *button_unused, EventEditor *editor)
{
	eventeditor_Zoom(editor, -1);
}

static void eventeditor_ZoomInCB(UIButton *button_unused, EventEditor *editor)
{
	eventeditor_Zoom(editor, 1);
}

static void eventeditor_CreateScaleButtons(EventEditor *editor)
{
	UIButton *pButton = ui_ButtonCreateImageOnly("32px_Zoom_Out", EV_WINDOW_DEFAULT_W / 2 + EV_MARGIN_W, EV_SPACE, eventeditor_ZoomOutCB, editor);
	ui_WidgetSetDimensions(UI_WIDGET(pButton), 24, 24);
	ui_ButtonSetImageStretch(pButton, true);
	ui_ScrollAreaAddChild(editor->scrollArea, pButton);

	pButton = ui_ButtonCreateImageOnly("32px_Zoom_In", EV_WINDOW_DEFAULT_W / 2 + EV_MARGIN_W + 25, EV_SPACE, eventeditor_ZoomInCB, editor);
	ui_WidgetSetDimensions(UI_WIDGET(pButton), 24, 24);
	ui_ButtonSetImageStretch(pButton, true);
	ui_ScrollAreaAddChild(editor->scrollArea, pButton);
}

static void eventeditor_CreateClearButton(EventEditor *editor)
{
	UIButton *button = ui_ButtonCreate("Clear", 0, 0, eventeditor_Clear, editor);
	ui_ScrollAreaAddChild(editor->scrollArea, button);
	ui_WidgetSetPosition(UI_WIDGET(button), EV_WINDOW_DEFAULT_W - button->widget.width - EV_MARGIN_W, EV_SPACE);
	editor->clearButton = button;
}

bool eventeditor_Close(UIAnyWidget *widget, UserData data)
{
	EventEditor *editor = (EventEditor*)data;

	F32 xPos = editor->window->widget.x;
	F32 yPos = editor->window->widget.y;
	F32 width = editor->window->widget.width;
	F32 height = editor->window->widget.height;
	F32 scale = editor->scrollArea->childScale;

	EditorPrefStoreFloat("Event Editor", "Window", "X", xPos);
	EditorPrefStoreFloat("Event Editor", "Window", "Y", yPos);
	EditorPrefStoreFloat("Event Editor", "Window", "Width", width);
	EditorPrefStoreFloat("Event Editor", "Window", "Height", height);
	EditorPrefStoreFloat("Event Editor", "Window", "Scale", scale);

	if(editor->closeFunc)
	{
		editor->closeFunc(editor, editor->closeData);
	}

	return 1;
}

// Create the Event window for the first time
EventEditor* eventeditor_Create(GameEvent *editableEvent, EventEditorChangeFunc onChangeFunc, void *onChangeData, bool bShowEventName)
{
	EventEditor *editor = calloc(1, sizeof(EventEditor));
	UIWidget *widget = NULL;
	UILabel *label = NULL;
	F32 y = EV_SPACE;
	
	F32 xPos = EditorPrefGetFloat("Event Editor", "Window", "X", 50);
	F32 yPos = EditorPrefGetFloat("Event Editor", "Window", "Y", 50);
	F32 width = EditorPrefGetFloat("Event Editor", "Window", "Width", EV_WINDOW_DEFAULT_W);
	F32 height = EditorPrefGetFloat("Event Editor", "Window", "Height", 600);
	F32 scale = EditorPrefGetFloat("Event Editor", "Window", "Scale", 1);

	editor->window = ui_WindowCreate("Event Editor", xPos, yPos, width, height);
	ui_WidgetSetFamily(UI_WIDGET(editor->window), UI_FAMILY_EDITOR);
	editor->boundEvent = editableEvent;
	ui_WindowSetCloseCallback(editor->window, eventeditor_Close, editor);
	if (editableEvent){
		StructCopyAll(parse_GameEvent, editableEvent, &editor->ev);
	}else{
		editor->boundEvent = StructCreate(parse_GameEvent);
		editor->bFreeBoundEvent = true;
		StructInit(parse_GameEvent, &editor->ev);
		editor->isCleared = true;
		editor->ev.pchMapName = allocAddString(zmapInfoGetPublicName(NULL));
	}
	editor->changeFunc = onChangeFunc;
	editor->changeData = onChangeData;

	editor->scrollArea = ui_ScrollAreaCreate(0, 0, 0, 0, 4000, 4000, true, true);
	ui_ScrollAreaSetDraggable(editor->scrollArea, true);
	ui_WidgetSetDimensionsEx(UI_WIDGET(editor->scrollArea), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	editor->scrollArea->autosize = true;
	ui_ScrollAreaSetChildScale(editor->scrollArea, scale);
	ui_WindowAddChild(editor->window, editor->scrollArea);

	// Event Type combo
	if (bShowEventName){
		y = eventeditor_CreateEventNameEntry(editor, EV_MARGIN_W, y);
	}
	eventeditor_CreateEventTypeCombo(editor, EV_MARGIN_W, y);
	eventeditor_CreateScaleButtons(editor);
	eventeditor_CreateClearButton(editor);
	eventeditor_CreateTextInput(editor);
	eventeditor_CreateTextOutput(editor);

	eventeditor_Refresh(editor);

	return editor;
}


// Create the Event window for the first time
EventEditor* eventeditor_CreateConst(const GameEvent *pEvent, EventEditorChangeFunc onChangeFunc, void *onChangeData, bool bShowEventName)
{
	EventEditor *editor = eventeditor_Create(NULL, onChangeFunc, onChangeData, bShowEventName);
	if (pEvent)
	{
		StructCopyAll(parse_GameEvent, pEvent, &editor->ev);
		editor->isCleared = false;
	}
	else{
		editor->isCleared = true;
	}
	eventeditor_Refresh(editor);
	return editor;
}

void eventeditor_SetCloseFunc(EventEditor *editor, EventEditorChangeFunc func, void *data)
{
	editor->closeFunc = func;
	editor->closeData = data;
}

EventEditor* eventeditor_CreateFromString(const char *eventString, EventEditorChangeFunc onChangeFunc, void *onChangeData)
{
	EventEditor *editor = eventeditor_Create(NULL, onChangeFunc, onChangeData, false);
	GameEvent *ev = gameevent_EventFromString(eventString);
	if (ev)
	{
		StructCopyAll(parse_GameEvent, ev, &editor->ev);
		editor->isCleared = false;
	}
	else
		editor->isCleared = true;
	eventeditor_Refresh(editor);
	return editor;
}

void eventeditor_Destroy(EventEditor *editor)
{
	resDictRemoveEventCallback("Contact",contactTableDictChanged);

	if (editor->chainedEventEditor)
	{
		eventeditor_Destroy(editor->chainedEventEditor);
		editor->chainedEventEditor = NULL;
	}
	
	ui_WidgetQueueFree(UI_WIDGET(editor->window)); // this frees all the widgets too
	eaDestroyEx(&editor->sourceActorList, NULL);
	eaDestroyEx(&editor->targetActorList, NULL);
	eaDestroy(&editor->encounterList);
	eaDestroy(&editor->staticEncList);
	eaDestroyEx(&editor->dialogList, NULL);
	if (editor->bFreeBoundEvent){
		StructDestroy(parse_GameEvent, editor->boundEvent);
	}
	free(editor);
}

static void eventeditor_UpdateEventFromFields(EventEditor *editor)
{
	int i, n = 0;
	EventType type = editor->ev.type;
	const char *pchEventName = editor->ev.pchEventName;
	GameEvent *pChainEvent = editor->ev.pChainEventDef;

	// Reset the Event, keeping only the type
	editor->ev.pchEventName = NULL;
	editor->ev.pChainEventDef = NULL;
	StructDeInit(parse_GameEvent, &editor->ev);
	StructInit(parse_GameEvent, &editor->ev);
	editor->ev.type = type;
	editor->ev.pchEventName = pchEventName;
	editor->ev.pChainEventDef = pChainEvent;

	n = eaSize(&editor->checkButtons);
	for (i = 0; i < n; i++)
	{
		UICheckButton *checkbutton = editor->checkButtons[i];
		if (checkbutton->toggledF)
			checkbutton->toggledF(checkbutton, checkbutton->toggledData);
	}

	n = eaSize(&editor->comboBoxes);
	for (i = 0; i < n; i++)
	{
		UIComboBox *cb = editor->comboBoxes[i];
		if (cb->selectedF)
			cb->selectedF(cb, cb->selectedData);
	}
	
	n = eaSize(&editor->textEntries);
	for (i = 0; i < n; i++)
	{
		UIEditable *edit = editor->textEntries[i];
		ui_EditableChanged(edit);
	}

	// Refresh lists that populate combos
	eventeditor_RefreshLists(editor);
}

// Refresh all the combos and text fields from the given Event
static void eventeditor_Refresh(EventEditor *editor)
{
	int currY = 0;
	char *estrBuffer = NULL;

	ui_ComboBoxSetSelectedEnum(editor->typeCombo, editor->ev.type);
	eventeditor_DestroyFields(editor);
	currY = eventeditor_CreateFields(editor);

	if (editor->nameEntry)
		ui_TextEntrySetText(editor->nameEntry, editor->ev.pchEventName);

	// Reposition input/output areas
	if (editor->inputArea)
		UI_WIDGET(editor->inputArea)->y = currY;
	if (editor->inputLabel)
		UI_WIDGET(editor->inputLabel)->y = currY;
	if (editor->inputArea)
		currY = editor->inputArea->widget.y + editor->inputArea->widget.height;
	
	if (editor->outputArea)
		UI_WIDGET(editor->outputArea)->y = currY;
	if (editor->outputLabel)
		UI_WIDGET(editor->outputLabel)->y = currY;

	if (editor->outputArea)
		currY += editor->outputArea->widget.height;
	if (editor->outputCopyButton)
		UI_WIDGET(editor->outputCopyButton)->y  = currY + EV_SPACE;
	if (editor->saveButton)
		UI_WIDGET(editor->saveButton)->y  = currY + EV_SPACE;
	if (editor->saveButton)
		currY += editor->saveButton->widget.height + EV_SPACE*2;
	
	// Populate the fields from the Event, clear the Event, and then copy the fields back to the Event.
	// This effectively only copies over fields that are still relevant according to the new EventType.
	if (!editor->isCleared)
	{
		eventeditor_RefreshFields(editor);
		eventeditor_UpdateEventFromFields(editor);
	}
	eventeditor_RefreshOutput(editor);
	eventeditor_RefreshLists(editor);

	eventeditor_UpdateChainedEventButton(editor);
}

// Opens an existing EventEditor
void eventeditor_Open(EventEditor *editor)
{
	eventeditor_Refresh(editor);
	ui_WindowShow(editor->window);
	ui_WidgetGroupSteal(UI_WIDGET(editor->window)->group, UI_WIDGET(editor->window));
}

UIWindow* eventeditor_GetWindow(EventEditor *editor)
{
	return SAFE_MEMBER(editor, window);
}

void eventeditor_GetEventStringEscaped(EventEditor *editor, char** estrBuffer)
{
	if (!editor->isCleared)
		gameevent_WriteEventEscaped(&editor->ev, estrBuffer);
	else
		estrClear(estrBuffer);
}

GameEvent *eventeditor_GetBoundEvent(EventEditor *editor)
{
	if (editor->isCleared)
		return NULL;
	return editor->boundEvent;
}

// The following is used to register the "Event" static check type with the expression editor
static bool eventeditor_ExprEdSetValueClosed(UIWindow *window, EventEditor *editor)
{
	eventeditor_Destroy(editor);	
	return true;
}

static void eventeditor_ExprEdSetValueSaved(EventEditor *editor, void *ref)
{
	int refVal = (int) (intptr_t) ref;
	char *estr = NULL;

	estrStackCreate(&estr);
	eventeditor_GetEventStringEscaped(editor, &estr);
	exprEdSetValidationValue(refVal, estr);
	estrDestroy(&estr);
	ui_WindowClose(eventeditor_GetWindow(editor));
}

void eventeditor_ExprEdSetValueClicked(int ref, const char *origText, void *unused)
{
	EventEditor *editor = eventeditor_CreateFromString(origText, eventeditor_ExprEdSetValueSaved, (void*) (intptr_t) ref);
	UIWindow *window = eventeditor_GetWindow(editor);

	ui_WindowSetCloseCallback(window, eventeditor_ExprEdSetValueClosed, editor);
	ui_WindowSetModal(window, true);
	eventeditor_Open(editor);
}

void eventeditor_SetNoMapMatch(EventEditor *editor, int on)
{
	editor->noMapMatch = !!on;

	GERefreshClickableList(&editor->clickableList);
	eventeditor_RefreshClickableGroupList(editor);
}



// --------------------------------------------------------------------
// Functions for UIGameEventEditButton
// --------------------------------------------------------------------

static void ui_GameEventEditButtonUpdateText(UIGameEventEditButton *pButton)
{
	char *estrBuffer = NULL;
	int numActions = 0;
	estrStackCreate(&estrBuffer);

	if (pButton && pButton->pEvent){
		gameevent_WriteEventSingleLine(pButton->pEvent, &estrBuffer);
	}

	if (pButton && pButton->pEventTextLabel)
		ui_LabelSetText(pButton->pEventTextLabel, estrBuffer);

	estrDestroy(&estrBuffer);
}

static void ui_GameEventEditButtonEditorCloseCB(EventEditor *pEditor, UIGameEventEditButton *pButton)
{
	eventeditor_Destroy(pEditor);
	pButton->pEditor = NULL;
}

static void ui_GameEventEditButtonEditorChangeCB(EventEditor *pEditor, UIGameEventEditButton *pButton)
{
	if (pButton->pEditor && pButton->pEditor == pEditor){
		if (pButton->pEvent)
			StructCopyAll(parse_GameEvent, &pEditor->ev, pButton->pEvent);
		else
			pButton->pEvent = StructClone(parse_GameEvent, &pEditor->ev);

		ui_GameEventEditButtonUpdateText(pButton);

		if (pButton->onChangeFunc)
			pButton->onChangeFunc(pButton, pButton->onChangeData);

		eventeditor_Destroy(pEditor);
		pButton->pEditor = NULL;
	}
}

// This doesn't get called... not sure if it's important?
static void ui_GameEventEditButtonEditorChangeFixupCB(GameEvent *pEvent, UIGameEventEditButton *pButton)
{
	if (pButton->onChangeFixupFunc)
		pButton->onChangeFixupFunc(pEvent, pButton->onChangeData);
}

static void ui_GameEventEditButtonClick(UIButton *pEventButton, void *unused)
{
	UIGameEventEditButton *pButton = (UIGameEventEditButton*)pEventButton;
	if (!pButton->pEditor){
		pButton->pEditor = eventeditor_CreateConst(pButton->pEvent, ui_GameEventEditButtonEditorChangeCB, pButton, false);
		eventeditor_Open(pButton->pEditor);
	}
	if (pButton->pEditor){
		eventeditor_SetCloseFunc(pButton->pEditor, ui_GameEventEditButtonEditorCloseCB, pButton);
	}
}

void ui_GameEventEditButtonSetData(UIGameEventEditButton *pButton, const GameEvent *pEvent, const GameEvent *pOrigEvent)
{
	// For now just close the editor if it's open
	// TODO - Refresh editor
	if (pButton->pEditor){
		eventeditor_Destroy(pButton->pEditor);
	}

	if (pEvent){
		if (pButton->pEvent){
			StructCopyAll(parse_GameEvent, pEvent, pButton->pEvent);
		}else{
			pButton->pEvent = StructClone(parse_GameEvent, pEvent);
		}
	} else if (pButton->pEvent){
		StructDestroy(parse_GameEvent, pButton->pEvent);
		pButton->pEvent = NULL;
	}

	if (pOrigEvent){
		if (pButton->pOrigEvent){
			StructCopyAll(parse_GameEvent, pOrigEvent, pButton->pOrigEvent);
		}else{
			pButton->pOrigEvent = StructClone(parse_GameEvent, pOrigEvent);
		}
	} else if (pButton->pOrigEvent){
		StructDestroy(parse_GameEvent, pButton->pOrigEvent);
		pButton->pOrigEvent = NULL;
	}

	ui_GameEventEditButtonUpdateText(pButton);
}

SA_RET_NN_VALID UIGameEventEditButton *ui_GameEventEditButtonCreate(SA_PARAM_OP_VALID const GameEvent *pEvent, SA_PARAM_OP_VALID const GameEvent *pOrigEvent, UIGameEventEditButtonChangeFunc onChangeFunc, UIGameEventEditButtonChangeFixupFunc onChangeFixupFunc, void *onChangeData)
{
	UIGameEventEditButton *pButton = calloc(1, sizeof(UIGameEventEditButton));
	F32 height;
	UIStyleFont *font = NULL;

	pButton->onChangeFunc = onChangeFunc;
	pButton->onChangeFixupFunc = onChangeFixupFunc;
	pButton->onChangeData = onChangeData;
	pButton->pEvent = StructClone(parse_GameEvent, pEvent);
	pButton->pOrigEvent = StructClone(parse_GameEvent, pOrigEvent);

	ui_WidgetInitialize(UI_WIDGET(pButton), ui_ButtonTick, ui_ButtonDraw, ui_GameEventEditButtonFreeInternal, ui_ButtonInput, ui_WidgetDummyFocusFunc);
	ui_ButtonSetDownCallback((UIButton*)pButton, NULL, NULL);
	ui_ButtonSetUpCallback((UIButton*)pButton, NULL, NULL);
	ui_ButtonSetCallback((UIButton*)pButton, ui_GameEventEditButtonClick, NULL);

	// I'm using a label to display the text since I don't want the text centered
	pButton->pEventTextLabel = ui_LabelCreate("", 0, 0);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pButton->pEventTextLabel), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetAddChild(UI_WIDGET(pButton), UI_WIDGET(pButton->pEventTextLabel));

	ui_GameEventEditButtonUpdateText(pButton);

	if (UI_GET_SKIN(pButton))
		font = GET_REF(UI_GET_SKIN(pButton)->hNormal);
	height = ui_StyleFontLineHeight(font, 1.f) + UI_STEP;
	ui_WidgetSetDimensions(UI_WIDGET(pButton), 200, height);

	return pButton;
}

void ui_GameEventEditButtonFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIGameEventEditButton *pButton)
{
	if (pButton->pEditor){
		eventeditor_Destroy(pButton->pEditor);
	}
	StructDestroy(parse_GameEvent, pButton->pEvent);
	StructDestroy(parse_GameEvent, pButton->pOrigEvent);
	ui_ButtonFreeInternal((UIButton*)pButton);
}

#endif

AUTO_RUN;
void eventeditor_ExprEdRegisterValidationFunc(void)
{
#ifndef NO_EDITORS
	exprEdRegisterValidationFunc("Event", eventeditor_ExprEdSetValueClicked, NULL);
	exprEdRegisterValidationFunc("PlayerScopedEvent", eventeditor_ExprEdSetValueClicked, NULL);
#endif
}
