#ifndef NO_EDITORS

#include "AudioEventEditor.h"
#include "contact_common.h"
#include "EditorManagerUtils.h"
#include "EString.h"
#include "eventeditor.h"
#include "GameEvent.h"
#include "GraphicsLib.h"
#include "oldencounter_common.h"
#include "Sound_common.h"
#include "soundLib.h"
#include "StringCache.h"
#include "wlEncounter.h"
#include "WorldGrid.h"

#include "GfxClipper.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"
#include "GfxPrimitive.h"


#include "AutoGen/Sound_common_h_ast.h"
#include "AutoGen/GameEvent_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

EventType unsupportedEvents[] = {
	//EventType_CutsceneEnd,
	//EventType_CutsceneStart,
	EventType_Damage,
	EventType_Emote,
	EventType_EncounterState,
	EventType_FSMState,
	EventType_Healing,
	//EventType_InteractBegin,		
	EventType_ItemGained,
	EventType_ItemLost,
	EventType_LevelUp,
	EventType_MissionLockoutState,
	EventType_NemesisState,
	EventType_Poke,
	EventType_PowerAttrModApplied,
	EventType_PickedUpObject,
	EventType_ZoneEventRunning,
	EventType_ZoneEventState,
};

void audioEventEdit_GameEventSave(EventEditor *editor, void *data);
void audioEventEdit_GameEventClose(EventEditor *editor, void *data);

void audioEventEdit_UpdateSelection(AudioEventEditDoc *doc);
AudioEventEditRow *audioEventEdit_AddRowEx(AudioEventEditDoc *doc,  GameAudioEventPair *pair, const char *pcName, EventType eventType);
UIComboBox* audioEventEdit_CreateAdditionalTypesList(F32 x, F32 y, F32 width, UIActivationFunc callbackFunc, UserData userData);

bool audioEventEdit_eventTypeFromString(char *pchValue, EventType *eventType);

// This is called once to initialize global data
void audioEventEdit_InitData(EMEditor *pEditor)
{
	static int inited = 0;

	if(inited)
	{
		return;
	}
	
	sndCommonCreateGAEDict();

	inited = 1;
}

void audioEventEdit_OpenGameEventEditorForRow(AudioEventEditRow *row)
{
	if(!row->pair->game_event)
	{
		row->pair->game_event = StructCreate(parse_GameEvent);
	}
	row->editor = eventeditor_Create(row->pair->game_event, audioEventEdit_GameEventSave, row, false);
	
	if(row->editor)
	{
		int i;

		for(i=0; i<ARRAY_SIZE(unsupportedEvents); i++)
			eventeditor_RemoveEventType(row->editor, unsupportedEvents[i]);

		eventeditor_SetNoMapMatch(row->editor, 1);
		eventeditor_SetCloseFunc(row->editor, audioEventEdit_GameEventClose, row);
		eventeditor_Open(row->editor);
	}
}

void audioEventEdit_SoundEventSelect(UIAnyWidget *widget, UserData data)
{
	UIComboBox *cb = (UIComboBox*)widget;
	AudioEventEditDoc *doc = (AudioEventEditDoc*)data;
	char *tempstr = NULL;

	if(doc->selectedRow)
	{
		S32 selectedItem;

		selectedItem = ui_ComboBoxGetSelected(doc->audioListCombo);
		if(selectedItem >= 0) 
		{
			ui_ComboBoxGetSelectedAsString(cb, &tempstr);
			audioEventEdit_SetSoundEventForRow(doc->selectedRow, tempstr);
		}

		estrDestroy(&tempstr);

		doc->emDoc.saved = 0; // not saved flag
	}
	else
	{

		// Multi-selection
		if(doc->numSelected > 1)
		{
			AudioEventEditRow **rows = (AudioEventEditRow**)(*doc->eventMappingList->peaModel);

			const S32* const* peaiMultiSelected = ui_ListGetSelectedRows(doc->eventMappingList);
			if (peaiMultiSelected)
			{
				U32 uiIndex;
				const U32 uiNum = eaiSize(peaiMultiSelected);
				AudioEventEditRow *row;

				ui_ComboBoxGetSelectedAsString(cb, &tempstr);

				for (uiIndex = 0; uiIndex < uiNum; ++uiIndex)
				{
					const S32 iRow = (*peaiMultiSelected)[uiIndex]; // grab row
					row = rows[iRow];
					audioEventEdit_SetSoundEventForRow(row, tempstr);

					doc->emDoc.saved = 0; // not saved flag
				}
			}
		}
	}
}

void audioEventEdit_SetSoundEventForRow(AudioEventEditRow *row, const char *audioEventName)
{
	row->pair->audio_event = strdup(audioEventName);
	row->pair->one_shot = sndEventIsOneShot(row->pair->audio_event);

	row->doc->emDoc.saved = 0;
}

//
//void audioEventEdit_ClearSoundOnRow(AudioEventEditRow *row)
//{
//	UIComboBox *comboBox = row->aeCombo;
//	GameAudioEventPair *gameAudioEventPair = row->pair;
//
//	ui_ComboBoxSetSelected(comboBox, -1); // < 0 should clear selection
//
//	gameAudioEventPair->one_shot = 0; // clear this
//	gameAudioEventPair->audio_event = NULL; // empty the field
//
//	row->doc->emDoc.saved = 0; // not saved flag
//}
//
//void audioEventEdit_RemoveChildrenFromRow(AudioEventEditRow *row) 
//{
//	ui_ScrollAreaRemoveChild(row->doc->scrollarea, UI_WIDGET(row->aeCombo));
//	if(row->delButton) 
//	{
//		ui_ScrollAreaRemoveChild(row->doc->scrollarea, UI_WIDGET(row->delButton));
//	}
//	
//	ui_ScrollAreaRemoveChild(row->doc->scrollarea, UI_WIDGET(row->clearSoundButton));
//	ui_ScrollAreaRemoveChild(row->doc->scrollarea, UI_WIDGET(row->geButton));
//}
//
//void audioEventEdit_DeleteRow(UIAnyWidget *widget, AudioEventEditRow *row)
//{
//	audioEventEdit_RemoveChildrenFromRow(row);
//
//	row->doc->emDoc.saved = 0;
//	
//	eaFindAndRemove(&row->doc->filteredRows, row); // remove from filter list
//	eaFindAndRemove(&row->doc->rows, row); // remove from main list
//	eaFindAndRemove(&row->doc->mapBeingEdited->pairs, row->pair); // should this be moved to DestroyRow()?
//
//	audioEventEdit_UpdateRows(row->doc);
//	
//	audioEventEdit_DestroyRow(row);
//}
//
void audioEventEdit_DestroyRow(AudioEventEditRow *row)
{
	StructDestroy(parse_GameAudioEventPair, row->pair);

	estrDestroy(&row->name);
	
	// note: filtered rows are just references to the row (so only need to free the row)
	free(row);
}

void audioEventEdit_UpdateEventNames(AudioEventEditDoc *doc)
{
	int numRows;
	int i;
	numRows = eaSize(&doc->audioEventRows);
	for(i = 0; i < numRows; i++)
	{
		AudioEventEditRow *row = doc->audioEventRows[i];
		if(row->name)
		{
			estrDestroy(&row->name);
			row->name = NULL;
		}
		gameevent_PrettyPrint(row->pair->game_event, &row->name);
	}
}

AudioEventEditRow *audioEventEdit_AddRow(AudioEventEditDoc *doc,  GameAudioEventPair *pair, const char *pcName)
{
	return audioEventEdit_AddRowEx(doc, pair, pcName, EventType_InteractSuccess);
}

AudioEventEditRow *audioEventEdit_AddRowEx(AudioEventEditDoc *doc,  GameAudioEventPair *pair, const char *pcName, EventType eventType)
{
	AudioEventEditRow *row = NULL;

	row = callocStruct(AudioEventEditRow);
	row->doc = doc;
	eaPush(&doc->audioEventRows, row);

	if(pair)
	{
		row->pair = pair;		
	}
	else
	{
		row->pair = StructCreate(parse_GameAudioEventPair);
		row->pair->map = doc->mapBeingEdited;
		eaPush(&row->doc->mapBeingEdited->pairs, row->pair);

		row->pair->game_event = StructCreate(parse_GameEvent);
		row->pair->game_event->type = eventType;
		row->pair->game_event->pchClickableName = strdup(pcName);
	}

	// get the name of the clickable
	gameevent_PrettyPrint(row->pair->game_event, &row->name);

	return row;
}



//typedef void (*EventEditorChangeFunc)(EventEditor *, void *);
void audioEventEdit_GameEventSave(EventEditor *editor, void *data)
{
	//F32 row_y;
	AudioEventEditRow *row = (AudioEventEditRow*)data;

	if(row && row->pair && row->pair->game_event)
	{
		if(row->pair->game_event->type == EventType_ClickableActive)
		{
			Alertf("Unsupported event type: ClickableActive. Use InteractEndActive instead.\n");
			row->pair->game_event->type = EventType_InteractEndActive;
		}

		audioEventEdit_UpdateEventNames(row->doc);
	}

	// add a delete button to the row (if it needs one)
	//if(!row->delButton) 
	//{
	//	row_y = ui_WidgetGetY(UI_WIDGET(row->clearSoundButton));
	//	row->delButton = ui_ButtonCreate("Delete", 0, row_y, audioEventEdit_DeleteRow, row);
	//}

	//if(row==eaTail(&row->doc->rows))
	//{
	//	// Add a new row
	//	assert(row->pair==row->doc->pairBeingEdited);
	//	audioEventEdit_AddRow(row->doc, NULL);

	//	audioEventEdit_UpdateRows(row->doc);
	//}

	//audioEventEdit_UpdateGameEventButtonText(row);

	if(row && row->doc)
	{
		row->doc->emDoc.saved = 0;
	}
	
}

void audioEventEdit_GameEventClose(EventEditor *editor, void *data)
{
	AudioEventEditRow *row = (AudioEventEditRow*)data;

	audioEventEdit_GameEventSave(editor, row);

	row->editor = NULL;
}

U32 audioEventEdit_RefreshAudio(AudioEventEditDoc *doc)
{
	int i;
	int changed = 0;

	for(i=0; i<eaSize(&doc->mapBeingEdited->pairs); i++)
	{
		GameAudioEventPair *pair = doc->mapBeingEdited->pairs[i];

		if(pair->audio_event && pair->audio_event[0])
		{
			if(!sndEventExists(pair->audio_event))
			{
				pair->audio_event = NULL;
				changed = 1;
			}
			else
			{
				U32 one_shot;
				one_shot = sndEventIsOneShot(pair->audio_event);

				if(one_shot!=pair->one_shot)
				{
					changed = 1;
				}
				pair->one_shot = one_shot;
			}
		}
	}

	if(changed)
	{
		doc->emDoc.saved = 0;
	}

	return changed;
}

void audioEventEdit_RefreshAudioButton(UIAnyWidget *widget, AudioEventEditDoc *doc)
{
	audioEventEdit_RefreshAudio(doc);
}

void audioEventEdit_AddClickable(AudioEventEditDoc *doc, StashTable lookupStash, const char *pcName)
{
	stashAddInt(lookupStash, pcName, 1, 1);

	audioEventEdit_AddRow(doc, NULL, pcName);
}

void audioEventEdit_MakeClickableEvents(UIAnyWidget *widget, AudioEventEditDoc *doc)
{
	// For each possible clicky on the map, make a row!
	int i;
	int added = 0;
	char layerDir[MAX_PATH], gaeDir[260];
	static char **peaList;

	if(!doc->lookupStash)
	{
		doc->lookupStash = stashTableCreateWithStringKeys(20, StashDefault);
	}

	stashTableClear(doc->lookupStash);

	strcpy(layerDir, zmapGetFilename(NULL));
	getDirectoryName(layerDir);

	if(doc->mapBeingEdited->filename) // Open'd file
		strcpy(gaeDir, doc->mapBeingEdited->filename);
	else if(doc->mapBeingEdited->zone_dir && doc->mapBeingEdited->zone_dir[0]) // New file
		strcpy(gaeDir, doc->mapBeingEdited->zone_dir);
	else // Just... not supposed to happen
		return;
	getDirectoryName(gaeDir);

	if(stricmp(layerDir, gaeDir))
	{
		Alertf("Only able to use Clicky button when editing a gae layer belonging to the loaded map.");
		return;
	}

	for(i=0; i<eaSize(&doc->audioEventRows); i++)
	{
		AudioEventEditRow *row = doc->audioEventRows[i];
		GameEvent *ge = row->pair->game_event;

		if(	ge && ge->type==EventType_InteractSuccess && ge->pchClickableName)
		{
			stashAddInt(doc->lookupStash, ge->pchClickableName, 1, 1);
		}
	}

	eaClear((char***)&peaList);

	// Find all interactables
	worldGetObjectNames(WL_ENC_INTERACTABLE, &peaList, NULL);
	
	for(i = eaSize(&peaList)-1; i >= 0; --i)
	{
		//WorldEncounterObject *obj;
		const char *pcName = peaList[i];
		
		//obj = worldScopeGetObject(NULL, pcName);

		if(!stashFindInt(doc->lookupStash, pcName, NULL))
		{
			audioEventEdit_AddClickable(doc, doc->lookupStash, pcName);
			added = 1;
		}

	}

	if(added)
	{
		doc->emDoc.saved = 0;
		//audioEventEdit_AddRow(doc, NULL);
		
		audioEventEdit_UpdateRows(doc);
//		audioEventEdit_DisplayAllEventsForDoc(doc);
	}

	
}

void audioEventEdit_UpdateNameFilter(UIAnyWidget *widget, UserData userData)
{
	audioEventEdit_UpdateRows((AudioEventEditDoc*)userData);
}

void audioEventEdit_UpdateExcludeLogFilter(UIAnyWidget *widget, UserData userData)
{
	audioEventEdit_UpdateRows((AudioEventEditDoc*)userData);
}

void audioEventEdit_ClearAudioEvent(UIAnyWidget *widget, UserData userData)
{
	AudioEventEditDoc *doc = (AudioEventEditDoc*)userData;
	AudioEventEditRow *row;
	UIComboBox *comboBox = doc->audioListCombo;

	//if(row = doc->selectedRow) 
	//{
	//	GameAudioEventPair *gameAudioEventPair = row->pair;
	//
	//	ui_ComboBoxSetSelected(comboBox, -1); // < 0 should clear selection
	//
	//	gameAudioEventPair->one_shot = 0; // clear this
	//	gameAudioEventPair->audio_event = NULL; // empty the field
	//
	//	row->doc->emDoc.saved = 0; // not saved flag
	//}
	if(doc->numSelected > 0)
	{
		const S32* const* peaiMultiSelected;
		AudioEventEditRow **rows = (AudioEventEditRow**)(*doc->eventMappingList->peaModel);

		peaiMultiSelected = ui_ListGetSelectedRows(doc->eventMappingList);
		if (peaiMultiSelected)
		{
			U32 uiIndex;
			const U32 uiNum = eaiSize(peaiMultiSelected);

			for (uiIndex = 0; uiIndex < uiNum; ++uiIndex)
			{
				GameAudioEventPair *gameAudioEventPair;
				const S32 iRow = (*peaiMultiSelected)[uiIndex]; // grab row
				row = rows[iRow];
				
				gameAudioEventPair = row->pair;

				gameAudioEventPair->one_shot = 0; // clear this
				gameAudioEventPair->audio_event = NULL; // empty the field

				row->doc->emDoc.saved = 0; // not saved flag
			}
		}
	}
}

void audioEventEdit_DuplicateSelection(UIAnyWidget *widget, UserData userData)
{
	AudioEventEditDoc *doc = (AudioEventEditDoc*)userData;
	char *tempstr = NULL;
	char *pchValue = NULL;
	EventType eventType = EventType_InteractSuccess;
	

	ui_ComboBoxGetSelectedAsString(doc->eventTypeList, &pchValue);
	audioEventEdit_eventTypeFromString(pchValue, &eventType);

	if(doc->numSelected > 0)
	{
		AudioEventEditRow **rows = (AudioEventEditRow**)(*doc->eventMappingList->peaModel);

		const S32* const* peaiMultiSelected = ui_ListGetSelectedRows(doc->eventMappingList);
		if (peaiMultiSelected)
		{
			U32 uiIndex;
			const U32 uiNum = eaiSize(peaiMultiSelected);
			AudioEventEditRow *row;
			AudioEventEditRow *newRow;

			//ui_ComboBoxGetSelectedAsString(cb, &tempstr);

			for (uiIndex = 0; uiIndex < uiNum; ++uiIndex)
			{
				const S32 iRow = (*peaiMultiSelected)[uiIndex]; // grab row
				row = rows[iRow];

				newRow = audioEventEdit_AddRowEx(doc, NULL, row->pair->game_event->pchClickableName, eventType);
				if(newRow)
				{
					audioEventEdit_SetSoundEventForRow(newRow, row->pair->audio_event);
				}
				
				doc->emDoc.saved = 0; // not saved flag
			}
		}
	}

	ui_ListClearEverySelection(doc->eventMappingList);
	audioEventEdit_UpdateRows(doc);
}

bool audioEventEdit_eventTypeFromString(char *pchValue, EventType *eventType)
{
	bool found = false;

	if(pchValue)
	{
		if(!strcmp(pchValue, "InteractBegin"))
		{
			*eventType = EventType_InteractBegin;			
			found = true;
		}
		else if(!strcmp(pchValue, "InteractFailure"))
		{
			*eventType = EventType_InteractFailure;
			found = true;
		}
		else if(!strcmp(pchValue, "InteractInterrupted"))
		{
			*eventType = EventType_InteractInterrupted;
			found = true;
		}
		else if(!strcmp(pchValue, "InteractSuccess"))
		{
			*eventType = EventType_InteractSuccess;
			found = true;
		}
	}

	return found;
}

void audioEventEdit_CreateNewEvent(UIAnyWidget *widget, UserData userData)
{
	AudioEventEditDoc* doc = (AudioEventEditDoc*)userData;
	char *pchValue = NULL;
	EventType eventType = EventType_InteractSuccess;

	ui_ComboBoxGetSelectedAsString(doc->eventTypeList, &pchValue);
	audioEventEdit_eventTypeFromString(pchValue, &eventType);

	audioEventEdit_AddRowEx(doc, NULL, NULL, eventType);
	audioEventEdit_UpdateRows(doc);
}

void audioEventEdit_RemoveRow(AudioEventEditRow *row) {
	row->doc->emDoc.saved = 0;

	eaFindAndRemove(&row->doc->filteredAudioEventRows, row); // remove from filter list
	eaFindAndRemove(&row->doc->audioEventRows, row); // remove from main list
	eaFindAndRemove(&row->doc->mapBeingEdited->pairs, row->pair); // should this be moved to DestroyRow()?

	audioEventEdit_DestroyRow(row);
}

void audioEventEdit_DeleteEvents(UIAnyWidget *widget, UserData userData)
{
	AudioEventEditDoc *doc = (AudioEventEditDoc*)userData;
	AudioEventEditRow *row = NULL;

	//if(row = doc->selectedRow) 
	//{
	//	audioEventEdit_RemoveRow(row);

	//	doc->selectedRow = NULL;
	//	audioEventEdit_UpdateSelection(row->doc);
	//	audioEventEdit_UpdateRows(row->doc);
	//}
	//else 
	if(doc->numSelected > 0) // Multi-selection
	{
		const S32* const* peaiMultiSelected;
		AudioEventEditRow **rows = (AudioEventEditRow**)(*doc->eventMappingList->peaModel);

		peaiMultiSelected = ui_ListGetSelectedRows(doc->eventMappingList);
		if (peaiMultiSelected)
		{
			U32 uiIndex;
			const U32 uiNum = eaiSize(peaiMultiSelected);

			for (uiIndex = 0; uiIndex < uiNum; ++uiIndex)
			{
				const S32 iRow = (*peaiMultiSelected)[uiIndex]; // grab row
				row = rows[iRow];
				audioEventEdit_RemoveRow(row);
			}

			doc->selectedRow = NULL;
			audioEventEdit_UpdateSelection(doc);
			audioEventEdit_UpdateRows(doc);
		}
	}
}

void audioEventEdit_Exists(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	AudioEventEditRow **rows = (AudioEventEditRow**)(*pList->peaModel);
	AudioEventEditDoc* doc = (AudioEventEditDoc*)pDrawData;
	AudioEventEditRow *row = rows[iRow];
	bool exists = false;

	if(doc->lookupStash && row && row->pair && row->pair->game_event)
	{
		if(stashFindInt(doc->lookupStash, row->pair->game_event->pchClickableName, NULL))
		{
			exists = true; 
		}
	}

	estrPrintf(estrOutput, "%s", exists ? "Yes" : "No");
}

void audioEventEdit_ClickableEventName(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	AudioEventEditRow **rows = (AudioEventEditRow**)(*pList->peaModel);
	AudioEventEditRow *row = rows[iRow];

	estrPrintf(estrOutput, "%s", row->name);
}

void audioEventEdit_AudioEventName(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	AudioEventEditRow **rows = (AudioEventEditRow**)(*pList->peaModel);
	AudioEventEditRow *row = rows[iRow];

	if(row->pair->audio_event)
	{
		estrPrintf(estrOutput, "%s", row->pair->audio_event);
	}
}

void audioEventEdit_UpdateSelection(AudioEventEditDoc *doc)
{
	if(doc->numSelected == 1 && doc->selectedRow)
	{
		ui_LabelSetText(doc->selectedClickableLabel, doc->selectedRow->name);
		ui_ComboBoxSetSelectedsAsString(doc->audioListCombo, doc->selectedRow->pair->audio_event);
		ui_SetActive(UI_WIDGET(doc->audioListCombo), true);
	}
	else if(doc->numSelected > 1)
	{
		static char *estrBuffer = NULL;
		if(!estrBuffer)
		{
			estrCreate(&estrBuffer);
		}

		estrPrintf(&estrBuffer, "%d events selected", doc->numSelected);

		ui_LabelSetText(doc->selectedClickableLabel, estrBuffer);
		ui_ComboBoxSetSelected(doc->audioListCombo, -1);

		estrDestroy(&estrBuffer);
		//ui_SetActive(UI_WIDGET(doc->audioListCombo), false);
	}
	else
	{
		ui_LabelSetText(doc->selectedClickableLabel, "");
		ui_SetActive(UI_WIDGET(doc->audioListCombo), false);
	}
}

void audioEventEdit_EventSelected(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData)
{
	AudioEventEditRow **rows = (AudioEventEditRow**)(*pList->peaModel);
	AudioEventEditDoc *doc = (AudioEventEditDoc*)pCellData;
	if(!rows) return;

	// call the default handler
	ui_ListCellClickedDefault(pList, iColumn, iRow, fMouseX, fMouseY, pBox, pCellData);

	if(doc)
	{
		const S32* const* peaiMultiSelected = ui_ListGetSelectedRows(pList);
		if (peaiMultiSelected)
		{
			const U32 uiNum = eaiSize(peaiMultiSelected);
			doc->numSelected = uiNum;

			if(uiNum == 1)
			{
				if(iRow < eaSize(&rows))
				{
					AudioEventEditRow *row = rows[iRow];
					// pList->pCellClickedData

					doc->selectedRow = row;
				}
				else
				{
					doc->selectedRow = NULL;
				}
			}
			else
			{
				doc->selectedRow = NULL;
			}
			audioEventEdit_UpdateSelection(doc);
		}
	}
}

void audioEventEdit_EventActivated(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData)
{
	AudioEventEditRow **rows = (AudioEventEditRow**)(*pList->peaModel);
	if(!rows) return;

	if(iRow < eaSize(&rows))
	{
		AudioEventEditRow *row = rows[iRow];

		audioEventEdit_OpenGameEventEditorForRow(row);
	}
}

void audioEventEdit_BuildWindow(AudioEventEditDoc *doc)
{
	F32 x, y;
	UIListColumn *col;
	F32 filter_label_width = 60.0;
	F32 padding = 5.0;
	F32 rowHeight = gfxfont_FontHeight(&g_font_Sans, 1.f) + UI_HSTEP;
	F32 name_entry_width = 200.0;
	int i;
	UISeparator *separator;
	//UILabel *label;

	// Name, Refresh Audio & Build Clickables
	x = 5;

	ui_WindowAddChild(doc->emDoc.primary_ui_window, ui_LabelCreate("Filename", 5, 0));

	doc->filename = ui_TextEntryCreate(doc->mapBeingEdited->name ? doc->mapBeingEdited->name : "Enter Name", x + filter_label_width, 0);
	ui_WidgetSetWidthEx(UI_WIDGET(doc->filename), 200, UIUnitFixed);
	ui_WindowAddChild(doc->emDoc.primary_ui_window, doc->filename);

	doc->refresh_loops = ui_ButtonCreate("Refresh Audio", ui_WidgetGetNextX(UI_WIDGET(doc->filename))+5, 0, audioEventEdit_RefreshAudioButton, doc);
	ui_WindowAddChild(doc->emDoc.primary_ui_window, doc->refresh_loops);

	doc->build_clickies = ui_ButtonCreate("Build Clickables", ui_WidgetGetNextX(UI_WIDGET(doc->refresh_loops))+5, 0, audioEventEdit_MakeClickableEvents, doc);
	ui_WindowAddChild(doc->emDoc.primary_ui_window, doc->build_clickies);

	y = 30;

	// Search (label)
	ui_WindowAddChild(doc->emDoc.primary_ui_window, ui_LabelCreate("Search", x, y));

	// Search Entry
	doc->nameFilter = ui_TextEntryCreate("", x + filter_label_width, y);
	ui_TextEntrySetChangedCallback(doc->nameFilter, audioEventEdit_UpdateNameFilter, doc);
	ui_WidgetSetWidthEx(UI_WIDGET(doc->nameFilter), name_entry_width, UIUnitFixed);
	ui_WindowAddChild(doc->emDoc.primary_ui_window, doc->nameFilter);

	x += filter_label_width + name_entry_width + 20;

	// Exclude (label)
	ui_WindowAddChild(doc->emDoc.primary_ui_window, ui_LabelCreate("Exclude", x, y));

	// Exclude Entry
	doc->excludeFilter = ui_TextEntryCreate("", x + filter_label_width, y);
	ui_TextEntrySetChangedCallback(doc->excludeFilter, audioEventEdit_UpdateExcludeLogFilter, doc);
	ui_WidgetSetWidthEx(UI_WIDGET(doc->excludeFilter), name_entry_width, UIUnitFixed);
	ui_WindowAddChild(doc->emDoc.primary_ui_window, doc->excludeFilter);

	y += 30;
	x = 5;

	// Note
	ui_WindowAddChild(doc->emDoc.primary_ui_window, ui_LabelCreate("Note: you may use commas to search and exclude multiple strings", x, y));

	y += 30;

	// separator
	separator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(separator), 0, y);
	ui_WindowAddChild(doc->emDoc.primary_ui_window, separator);

	y += 5;

	//label = ui_LabelCreate("Selected Event", x, y);
	//ui_WindowAddChild(doc->emDoc.primary_ui_window, label);

	//y += 30;

	doc->selectedClickableLabel = ui_LabelCreate("Selected Clickable Event", x, y);
	ui_WindowAddChild(doc->emDoc.primary_ui_window, doc->selectedClickableLabel);

	y += 25;

	doc->audioListCombo = audioEventEdit_CreateAudioList(x, y, 300, audioEventEdit_SoundEventSelect, doc);
	ui_WindowAddChild(doc->emDoc.primary_ui_window, doc->audioListCombo);

	doc->clearAudioEvent = ui_ButtonCreate("Clear", x + 305, y, audioEventEdit_ClearAudioEvent, doc);
	ui_WindowAddChild(doc->emDoc.primary_ui_window, UI_WIDGET(doc->clearAudioEvent));

	y += 30;

	separator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(separator), 0, y);
	ui_WindowAddChild(doc->emDoc.primary_ui_window, separator);

	y += 8;

	doc->countLabel = ui_LabelCreate("Displaying 0 of 0 events", x, y);
	ui_WindowAddChild(doc->emDoc.primary_ui_window, doc->countLabel);

	y += 25;

	// Click-able Event to Audio Event Mappings
	doc->eventMappingList = ui_ListCreate(NULL, &doc->filteredAudioEventRows, rowHeight);
	ui_ListSetMultiselect(doc->eventMappingList, true);
	ui_WidgetSetClickThrough(UI_WIDGET(doc->eventMappingList), 1);
	ui_ListSetCellClickedCallback(doc->eventMappingList, audioEventEdit_EventSelected, doc);
	ui_ListSetCellActivatedCallback(doc->eventMappingList, audioEventEdit_EventActivated, doc);

	col = ui_ListColumnCreate(UIListTextCallback, "Exists", (intptr_t)audioEventEdit_Exists, doc);
	ui_ListColumnSetWidth(col, false, 50);
	ui_ListAppendColumn(doc->eventMappingList, col);

	col = ui_ListColumnCreate(UIListTextCallback, "Clickable Event", (intptr_t)audioEventEdit_ClickableEventName, doc);
	//ui_ListColumnSetWidth(col, false, 75);
	ui_ListAppendColumn(doc->eventMappingList, col);

	col = ui_ListColumnCreate(UIListTextCallback, "Audio Event", (intptr_t)audioEventEdit_AudioEventName, doc);
	//ui_ListColumnSetWidth(col, false, 75);
	ui_ListAppendColumn(doc->eventMappingList, col);

	ui_WidgetSetDimensionsEx(UI_WIDGET(doc->eventMappingList), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(doc->eventMappingList), 0, 0, y, 30);
	ui_WindowAddChild(doc->emDoc.primary_ui_window, doc->eventMappingList);

	// Selected Row Info -------------

	x = 5;
	y = 2;

	doc->eventTypeList = audioEventEdit_CreateAdditionalTypesList(x, y, 150, NULL, doc);
	ui_WidgetSetPositionEx(UI_WIDGET(doc->eventTypeList), x, y, 0, 0, UIBottomLeft);
	ui_WindowAddChild(doc->emDoc.primary_ui_window, UI_WIDGET(doc->eventTypeList));

	x += 155;

	doc->createNewEvent = ui_ButtonCreate("Create New Event", 0, 0, audioEventEdit_CreateNewEvent, doc);
	ui_WidgetSetPositionEx(UI_WIDGET(doc->createNewEvent), x, y, 0, 0, UIBottomLeft);
	ui_WindowAddChild(doc->emDoc.primary_ui_window, UI_WIDGET(doc->createNewEvent));

	x += 125;

	doc->duplicateSelection = ui_ButtonCreate("Duplicate Selection", 0, 0, audioEventEdit_DuplicateSelection, doc);
	ui_WidgetSetPositionEx(UI_WIDGET(doc->duplicateSelection), x, y, 0, 0, UIBottomLeft);
	ui_WindowAddChild(doc->emDoc.primary_ui_window, UI_WIDGET(doc->duplicateSelection));

	x += 125;

	doc->deleteSelectedEvents = ui_ButtonCreate("Delete Selected Events", 0, 0, audioEventEdit_DeleteEvents, doc);
	ui_WidgetSetPositionEx(UI_WIDGET(doc->deleteSelectedEvents), x, y, 0, 0, UIBottomLeft);
	ui_WindowAddChild(doc->emDoc.primary_ui_window, UI_WIDGET(doc->deleteSelectedEvents));




	// Add the rows
	for(i = 0; i < eaSize(&doc->mapBeingEdited->pairs); i++)
	{
		GameAudioEventPair *pair = doc->mapBeingEdited->pairs[i];

		audioEventEdit_AddRow(doc, pair, NULL);
	}

	audioEventEdit_UpdateRows(doc);
}

UIComboBox* audioEventEdit_CreateAdditionalTypesList(F32 x, F32 y, F32 width, UIActivationFunc callbackFunc, UserData userData)
{
	UIComboBox *comboBox;

	static char **eventTypes = NULL;

	if(!eventTypes)
	{
		eaPush(&eventTypes, strdup("InteractBegin"));
		eaPush(&eventTypes, strdup("InteractFailure"));
		eaPush(&eventTypes, strdup("InteractInterrupted"));
		eaPush(&eventTypes, strdup("InteractSuccess"));
	}

	comboBox = (UIComboBox*)ui_FilteredComboBoxCreate(x, y, width, NULL, &eventTypes, NULL);

	ui_WidgetSetWidthEx(UI_WIDGET(comboBox), width, UIUnitFixed); // appears to require width setting here.
	ui_ComboBoxSetSelectedCallback(comboBox, callbackFunc, userData);

	ui_ComboBoxSetSelectedsAsString(comboBox, "InteractSuccess");
	
	return comboBox;
}

UIComboBox* audioEventEdit_CreateAudioList(F32 x, F32 y, F32 width, UIActivationFunc callbackFunc, UserData userData)
{
	UIComboBox *comboBox;

	comboBox = (UIComboBox*)ui_FilteredComboBoxCreate(x, y, width, NULL, sndGetEventListStatic(), NULL);

	ui_WidgetSetWidthEx(UI_WIDGET(comboBox), width, UIUnitFixed); // appears to require width setting here.
	ui_ComboBoxSetSelectedCallback(comboBox, callbackFunc, userData);

	return comboBox;
}

// Filter all of the game events based on the filter entry textfield (case insensitive comparison)
void audioEventEdit_FilterEvents(UIAnyWidget *widget, UserData userData)
{
	AudioEventEditDoc *doc = (AudioEventEditDoc*)userData;

	audioEventEdit_UpdateRows(doc);
}

void audioEventEdit_UpdateSearchLabelInfo(AudioEventEditDoc *doc) 
{
	char infoText[128];
	const unsigned char *searchString;
	int numRows = eaSize(&doc->audioEventRows);
	int numFilteredRows = eaSize(&doc->filteredAudioEventRows);

	searchString = ui_TextEntryGetText(doc->nameFilter);
	
	sprintf(infoText, "Display results (%d of %d)", numFilteredRows, numRows);

	ui_LabelSetText(doc->countLabel, infoText);
}

void splitStringWithDelim(const char *str, const char *delim, char ***eaOutput)
{
	char *token;
	char *strCopy = strdup(str);
	char *ptr = strCopy;

	do {
		token = strsep2(&ptr, delim, NULL);
		if(token)
		{
			eaPush(eaOutput, token);
		}
	} while(token);

	free(strCopy);
}
 
void audioEventEdit_FilterRows(AudioEventEditDoc *doc, const char *searchStrings, const char *excludeStrings)
{
	int i, numRows;
	char **eaSearchStrings = NULL;
	char **eaExcludeStrings = NULL;

	eaClear(&doc->filteredAudioEventRows);

	splitStringWithDelim(searchStrings, ",", &eaSearchStrings); 
	splitStringWithDelim(excludeStrings, ",", &eaExcludeStrings);

	numRows = eaSize(&doc->audioEventRows);
	for(i = 0; i < numRows; i++)
	{
		bool addRow = true;
		AudioEventEditRow *row = doc->audioEventRows[i];

		if(searchStrings && searchStrings[0])
		{
			if(row->name == NULL)
			{
				addRow = false;
			}
			else
			{
				bool found = false;
				int j;
				for(j = eaSize(&eaSearchStrings)-1; j >= 0; j--)
				{
					char *str = eaSearchStrings[j];
					if(strstri(row->name, str))
					{
						found = true;
						break;
					}
				}
				if(!found) addRow = false;
			}
		}

		if(excludeStrings && excludeStrings[0] && row->name)
		{
			bool found = false;
			int j;
			for(j = eaSize(&eaExcludeStrings)-1; j >= 0; j--)
			{
				char *str = eaExcludeStrings[j];
				if(strstri(row->name, str))
				{
					found = true;
					break;
				}
			}
			if(found) addRow = false;
		}
		
		if(addRow)
		{
			eaPush(&doc->filteredAudioEventRows, row);
		}
	}

	eaDestroy(&eaSearchStrings);
	eaDestroy(&eaExcludeStrings);
}

int cmpAudioEventEditRow(const AudioEventEditRow **row1, const AudioEventEditRow **row2)
{
	if(*row1 && *row2)
	{
		return stricmp((*row1)->name, (*row2)->name);
	} 
	else if(*row1 == NULL)
	{
		return 1;
	}
	else
	{
		return -1;
	}
}

void audioEventEdit_UpdateRows(AudioEventEditDoc *doc) 
{
	audioEventEdit_FilterRows(doc, ui_TextEntryGetText(doc->nameFilter), ui_TextEntryGetText(doc->excludeFilter));

	eaQSort(doc->filteredAudioEventRows, cmpAudioEventEditRow);

	audioEventEdit_UpdateSearchLabelInfo(doc);
}

// This is called to open a audioEvent edit document
// If the name does not exist, then a new audioEvent is created
AudioEventEditDoc *audioEventEdit_OpenAudioEvent(EMEditor *pEditor, GameAudioEventMap *map)
{
	static int load_request = 0;
	AudioEventEditDoc *doc = NULL;

	doc = callocStruct(AudioEventEditDoc);

	doc->emDoc.primary_ui_window = ui_WindowCreate(map->name ? map->name : "New GameEvent/AudioEvent", 0, 0, 500, 20);
	eaPush(&doc->emDoc.ui_windows, doc->emDoc.primary_ui_window);

	ui_WindowShow(doc->emDoc.primary_ui_window);

	devassert(map);
	doc->mapBeingEdited = StructClone(parse_GameAudioEventMap, map);
	audioEventEdit_BuildWindow(doc);

	if (gConf.bAllowOldEncounterData)
		oldencounter_AllowLayerLoading();

	if(map->zone_dir && map->zone_dir[0])
	{			// New file
	}
	else		// Opening file
	{
		EMFile **files = NULL;
		assert(map->filename && map->filename[0]);
		emDocAssocFile((EMEditorDoc*)doc, map->filename);
		strcpy(doc->emDoc.doc_display_name, doc->mapBeingEdited->name);

		// TRY to check it out
		emDocGetFiles((EMEditorDoc*)doc, &files, 1);
		doc->checked_out = emuCheckoutFile(files[0]);

		eaDestroy(&files);
	}

	contact_LoadDefs();

	return doc;
}

// This is called to save the audioEvent being edited
bool audioEventEdit_SaveAudioEvent(AudioEventEditDoc *pDoc, bool bSaveAsNew)
{
	const char *filename;

	pDoc->mapBeingEdited->name = ui_TextEntryGetText(pDoc->filename);
	strcpy(pDoc->emDoc.doc_display_name, pDoc->mapBeingEdited->name);

	sndGAEMapValidate(pDoc->mapBeingEdited, 0);
	if(!pDoc->mapBeingEdited->invalid && pDoc->mapBeingEdited->name && pDoc->mapBeingEdited->name[0])
	{
		if(!stricmp(pDoc->mapBeingEdited->name, "Enter Name"))
		{
			Errorf("Please enter a name");
		}
		else
		{
			if(!pDoc->mapBeingEdited->filename)
			{
				EMFile **files = NULL;
				char path[MAX_PATH];
				sprintf(path, "%s/%s.gaelayer", getDirectoryName(pDoc->mapBeingEdited->zone_dir), pDoc->mapBeingEdited->name);
				pDoc->mapBeingEdited->filename = allocAddString(path);
				emDocAssocFile((EMEditorDoc*)pDoc, pDoc->mapBeingEdited->filename);
				// No check out, since it's new and thus not in database
				pDoc->checked_out = 1;
			}
			filename = pDoc->mapBeingEdited->filename;

			if(!RefSystem_ReferentFromString(g_GAEMapDict, filename))
			{
				RefSystem_AddReferent(g_GAEMapDict, filename, StructClone(parse_GameAudioEventMap, pDoc->mapBeingEdited));
			}
			else
			{
				GameAudioEventMap *map = RefSystem_ReferentFromString(g_GAEMapDict, filename);
				RefSystem_RemoveReferent(map, 1);
				RefSystem_AddReferent(g_GAEMapDict, filename, StructClone(parse_GameAudioEventMap, pDoc->mapBeingEdited));
			}

			if(pDoc->checked_out)
			{
				ParserWriteTextFileFromSingleDictionaryStruct(filename, g_GAEMapDict, pDoc->mapBeingEdited, 0, 0);
				pDoc->emDoc.saved = 1;
			}
			else
			{
				gfxStatusPrintf("Failed to save %s: not checked out", pDoc->mapBeingEdited->filename);
			}
		}
	}
	else
		gfxStatusPrintf("Failed to save %s: failed validation", pDoc->mapBeingEdited->filename);

	return pDoc->emDoc.saved;
}

// This is called to close the audioEvent being edited
void audioEventEdit_CloseAudioEvent(AudioEventEditDoc *pDoc)
{
	int i;
	
	for(i=eaSize(&pDoc->audioEventRows)-1; i>=0; i--)
	{
		AudioEventEditRow *row = pDoc->audioEventRows[i];

		if(row->editor)
		{
			eventeditor_Destroy(row->editor);
			row->editor = NULL;
		}

		audioEventEdit_DestroyRow(row);
	}
	
	eaDestroy(&pDoc->audioEventRows);
	eaDestroy(&pDoc->filteredAudioEventRows);

	ui_WindowHide(pDoc->emDoc.primary_ui_window);
	ui_WindowClose(pDoc->emDoc.primary_ui_window);
}

// This is called prior to close to see if the audioEvent can be closed
bool audioEventEdit_CloseCheck(AudioEventEditDoc *pDoc)
{
	int i;

	for(i=0; i<eaSize(&pDoc->audioEventRows); i++)
	{
		AudioEventEditRow *row = pDoc->audioEventRows[i];

		if(row->editor)
		{
			gfxStatusPrintf("Unable to close with event editors open!");
			return 0;
		}
	}

	if(pDoc->lookupStash)
	{
		stashTableDestroy(pDoc->lookupStash);
	}
	
	return 1;
}

#endif