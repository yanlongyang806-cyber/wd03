/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

#include "UICore.h"
#include "UIButton.h"

typedef struct GameActionEditor GameActionEditor;
typedef struct UIButton UIButton;
typedef struct UIWindow UIWindow;
typedef struct UIGameActionEditButton UIGameActionEditButton;
typedef struct WorldGameActionBlock WorldGameActionBlock;

typedef void (*GameActionEditorChangeFunc)(GameActionEditor *, void *);
typedef void (*GameActionEditorChangeFixupFunc)(WorldGameActionBlock *, void *);
typedef void (*UIGameActionEditButtonChangeFunc)(UIGameActionEditButton *, void *);
typedef void (*UIGameActionEditButtonChangeFixupFunc)(WorldGameActionBlock *, void *);

typedef struct UIGameActionEditButton {

	UI_INHERIT_FROM(UI_WIDGET_TYPE UI_BUTTON_TYPE);

	// GameActionEditor that this button opens
	GameActionEditor *pEditor;

	// New and Orig copies of the WorldGameActionBlock
	WorldGameActionBlock *pActionBlock;
	WorldGameActionBlock *pOrigBlock;

	// The zone map that this action will be executed on (NULL if there is no known map)
	char *pcSrcZoneMap;

	// Callback to occur when the GameActions change
	UIGameActionEditButtonChangeFunc onChangeFunc;
	UIGameActionEditButtonChangeFixupFunc onChangeFixupFunc;
	void *onChangeData;

} UIGameActionEditButton;

// --------------------------------------------------------------------
// Callbacks and functions for the GameAction editor
// --------------------------------------------------------------------

// Creates a GameActionEditor for the specified WorldGameActionBlock.
GameActionEditor* gaeditor_Create(char* pcSrcZoneMap, const WorldGameActionBlock *pActionBlock, const WorldGameActionBlock *pOrigActionBlock, GameActionEditorChangeFunc onChangeFunc, GameActionEditorChangeFixupFunc onChangeFixupFunc, void *onChangeData);

// Set function to be called on window close
void gaeditor_SetCloseFunc(GameActionEditor *editor, GameActionEditorChangeFunc func, void *data);

// Destroys the editor
void gaeditor_Destroy(GameActionEditor *editor);

// Returns the main window for the editor
UIWindow* gaeditor_GetWindow(GameActionEditor *editor);

// --------------------------------------------------------------------
// Functions for UIGameActionEditButton
// --------------------------------------------------------------------

SA_RET_NN_VALID UIGameActionEditButton *ui_GameActionEditButtonCreate(SA_PARAM_OP_STR char* pcSrcZoneMap, SA_PARAM_OP_VALID const WorldGameActionBlock *pActionBlock, SA_PARAM_OP_VALID const WorldGameActionBlock *pOrigBlock, UIGameActionEditButtonChangeFunc onChangeFunc, UIGameActionEditButtonChangeFixupFunc onChangeFixupFunc, void *onChangeData);
void ui_GameActionEditButtonFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIGameActionEditButton *pButton);

// Sets the GameActionBlock data for this button
void ui_GameActionEditButtonSetData(UIGameActionEditButton *pButton, const WorldGameActionBlock *pActionBlock, const WorldGameActionBlock *pOrigBlock);

#endif
