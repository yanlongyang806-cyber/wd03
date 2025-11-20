#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

#include "EditorManager.h"

#define AUDIO_EVENT "Audio Event"
#define AUDIO_EVENT_EDITOR "Audio Event Editor"

typedef struct AudioEventEditDoc AudioEventEditDoc;
typedef struct EMEditor EMEditor;
typedef struct MEWindow MEWindow;
typedef struct GameAudioEventPair GameAudioEventPair;
typedef struct GameAudioEventMap GameAudioEventMap;
typedef struct UIButton UIButton;
typedef struct UIComboBox UIComboBox;
typedef struct UILabel UILabel;
typedef const void* DictionaryHandle;
typedef struct EventEditor EventEditor;
typedef struct UIScrollArea UIScrollArea;

typedef struct AudioEventEditRow {
	char *name;

	AudioEventEditDoc *doc;
	GameAudioEventPair *pair;

	EventEditor *editor;
	//UIButton *geButton;
	//UIComboBox *aeCombo;
	//UIButton *delButton;
	//UIButton *clearSoundButton;

	//char *temp_audio_event;

	//U32 added_del : 1;
} AudioEventEditRow;

typedef struct AudioEventEditDoc {
	// NOTE: This must be first for EDITOR MANAGER
	EMEditorDoc emDoc;

	UIScrollArea *scrollarea;
	UILabel *loading;
	UITextEntry *filename;
	UIButton *refresh_loops;
	UIButton *build_clickies;

	StashTable lookupStash;

	AudioEventEditRow **audioEventRows;
	AudioEventEditRow **filteredAudioEventRows;

	AudioEventEditRow *selectedRow;
	int numSelected;

	UITextEntry *nameFilter;
	UITextEntry *excludeFilter;
	UIList *eventMappingList;
	UILabel *countLabel;
	UILabel *selectedClickableLabel;

	UIComboBox *eventTypeList;
	UIButton *createNewEvent;
	UIButton *deleteSelectedEvents;
	UIButton *duplicateSelection;

	//UITextEntry *searchFilterEntry;
	//UIButton *filterEventsButton;
	//UIButton *displayAllButton;
	UIComboBox *audioListCombo;
	UIButton *clearAudioEvent;
	//UIButton *setAllButton;
	//UIButton *clearAllButton;

	//UILabel *infoLabel;

	GameAudioEventPair *pairBeingEdited;
	//AudioEventEditRow **rows;
	//AudioEventEditRow **filteredRows;

	GameAudioEventMap *mapFromServer;
	GameAudioEventMap *mapBeingEdited;

	
	U32 is_loading;
	U32 is_saving;

	F32 max_event_length;
	//AudioEventEditRow *max_event_row;

	char **event_list;

	U32		checked_out : 1;
} AudioEventEditDoc;

extern DictionaryHandle *g_GAEMapDict;

// This is called to open a audioEvent edit document
AudioEventEditDoc *audioEventEdit_OpenAudioEvent(EMEditor *pEditor, GameAudioEventMap *map);

// This is called to save the audioEvent being edited
bool audioEventEdit_SaveAudioEvent(AudioEventEditDoc *pDoc, bool bSaveAsNew);

// This is called to close the audioEvent being edited
void audioEventEdit_CloseAudioEvent(AudioEventEditDoc *pDoc);

// This is called prior to close to see if the audioEvent can be closed
bool audioEventEdit_CloseCheck(AudioEventEditDoc *pDoc);

// This is called once to initialize global data
void audioEventEdit_InitData(EMEditor *pEditor);



// callback for clicking 'clear' button
//void audioEventEdit_ClearSoundOnRowCB(UIAnyWidget *widget, AudioEventEditRow *row);
// clear the sound setting for the row
//void audioEventEdit_ClearSoundOnRow(AudioEventEditRow *row);

//void audioEventEdit_FilterEvents(UIAnyWidget *widget, UserData userData);
// Clear the search field and display all game events
//void audioEventEdit_DisplayAllEvents(UIAnyWidget *widget, UserData userData);
// Set all currently visible rows to the selected item
//void audioEventEdit_SetAllEvents(UIAnyWidget *widget, UserData userData);
// Clear all currently visible rows' selection
//void audioEventEdit_ClearAllEvents(UIAnyWidget *widget, UserData userData);
// A selection is made 
//void audioEventEdit_FilteredSoundEventSelect(UIAnyWidget *widget, UserData data);

// A selection is made (on a row)
void audioEventEdit_SoundEventSelect(UIAnyWidget *widget, UserData data);
// Set the sound for a row
void audioEventEdit_SetSoundEventForRow(AudioEventEditRow *row, const char *audioEventName);

// update the rows based on current filter (if any)
void audioEventEdit_UpdateRows(AudioEventEditDoc *doc);
// perform the row filter operation to construct the array of filtered rows
//void audioEventEdit_FilterRowsByEventName(AudioEventEditDoc *doc, const char *eventName);
// update display for filtered rows
//void audioEventEdit_UpdateDisplayableRows(AudioEventEditDoc *doc);
// add the controls from the row to the window (standard row with delete)
//void audioEventEdit_DisplayRow(AudioEventEditDoc *doc, AudioEventEditRow *row);
// add the controls from the row to the window (with no delete)
//void audioEventEdit_DisplayNewRow(AudioEventEditDoc *doc, AudioEventEditRow *row);
// add the controls from the row to the window (with delete option)
//void audioEventEdit_DisplayRowWithDelete(AudioEventEditDoc *doc, AudioEventEditRow *row, bool withDelete);

// update the label's info
void audioEventEdit_UpdateSearchLabelInfo(AudioEventEditDoc *doc);

// remove controls that belong to the row
//void audioEventEdit_RemoveChildrenFromRow(AudioEventEditRow *row);
// visually remove visible rows
//void audioEventEdit_VisuallyRemoveVisibleRows(AudioEventEditDoc *doc);

// destroy the row data (release the mem)
void audioEventEdit_DestroyRow(AudioEventEditRow *row);

// Helper function to create a combo box with the audio events listed
UIComboBox* audioEventEdit_CreateAudioList(F32 x, F32 y, F32 width, UIActivationFunc callbackFunc, UserData userData);

#endif