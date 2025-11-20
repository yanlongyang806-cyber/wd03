/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

#include "UICore.h"
#include "UIButton.h"

typedef struct EventEditor EventEditor;
typedef struct GameEvent GameEvent;
typedef struct UIWindow UIWindow;
typedef struct UIGameEventEditButton UIGameEventEditButton;

typedef void (*UIGameEventEditButtonChangeFunc)(UIGameEventEditButton *, void *);
typedef void (*UIGameEventEditButtonChangeFixupFunc)(GameEvent *, void *);
typedef void (*EventEditorChangeFunc)(EventEditor *, void *);

typedef struct UIGameEventEditButton {

	UI_INHERIT_FROM(UI_WIDGET_TYPE UI_BUTTON_TYPE);

	// GameActionEditor that this button opens
	EventEditor *pEditor;

	// New and Orig copies of the WorldGameActionBlock
	GameEvent *pEvent;
	GameEvent *pOrigEvent;

	// Callback to occur when the GameActions change
	UIGameEventEditButtonChangeFunc onChangeFunc;
	UIGameEventEditButtonChangeFixupFunc onChangeFixupFunc;
	void *onChangeData;

	// I'm using a label to display the text since I don't want the text centered
	UILabel *pEventTextLabel;

} UIGameEventEditButton;



// Creates an Event Editor for the specified Event.
EventEditor* eventeditor_Create(GameEvent *editableEvent, // Event to edit.  Optional
								EventEditorChangeFunc onChangeFunc, // Callback when event is saved.  Optional.
								void *onChangeData,  // User data
								bool bShowNameField);

// Creates an Event Editor for the specified non-editable Event
EventEditor* eventeditor_CreateConst(const GameEvent *pEvent, EventEditorChangeFunc onChangeFunc, void *onChangeData, bool bShowNameField);

// Set function to be called on window close
void eventeditor_SetCloseFunc(EventEditor *editor, EventEditorChangeFunc func, void *data);

// Creates an Event Editor using the specified string
EventEditor* eventeditor_CreateFromString(const char *eventString, EventEditorChangeFunc onChangeFunc, void *onChangeData);

// Destroys the editor
void eventeditor_Destroy(EventEditor *editor);

// Opens the window and brings it on top
void eventeditor_Open(EventEditor *editor);

// Returns the main window for the editor
UIWindow* eventeditor_GetWindow(EventEditor *editor);

// Gets the event as a string
void eventeditor_GetEventStringEscaped(EventEditor *editor, char** estrBuffer);

// Gets the event currently being edited by this editor
GameEvent *eventeditor_GetBoundEvent(EventEditor *editor);

// Removes the specified option from the type combo - type is EventType... don't care to forward declare it
void eventeditor_RemoveEventType(EventEditor *editor, U32 type);

// Allows encounter objects (clickies etc.) to be loaded from the without matching the public name
void eventeditor_SetNoMapMatch(EventEditor *editor, int on);

// --------------------------------------------------------------------
// Functions for UIGameEventEditButton
// --------------------------------------------------------------------

SA_RET_NN_VALID UIGameEventEditButton *ui_GameEventEditButtonCreate(SA_PARAM_OP_VALID const GameEvent *pActionBlock, SA_PARAM_OP_VALID const GameEvent *pOrigBlock, UIGameEventEditButtonChangeFunc onChangeFunc, UIGameEventEditButtonChangeFixupFunc onChangeFixupFunc, void *onChangeData);
void ui_GameEventEditButtonFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIGameEventEditButton *pButton);

// Sets the GameActionBlock data for this button
void ui_GameEventEditButtonSetData(UIGameEventEditButton *pButton, const GameEvent *pActionBlock, const GameEvent *pOrigBlock);

#endif