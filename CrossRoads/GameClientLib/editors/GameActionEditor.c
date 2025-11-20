/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS
GCC_SYSTEM

#include "GameActionEditor.h"
#include "gameeditorshared.h"
#include "worldgrid.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

typedef struct GameActionEditor {
	WorldGameActionBlock actionBlock;
	WorldGameActionBlock origBlock;

	UIWindow *pWindow;
	UIScrollArea *pScrollArea;
	GEActionGroup **eaActionGroups;
	UIButton *pNewActionButton;
	UIButton *pSaveButton;
	UIButton *pCancelButton;

	GameActionEditorChangeFunc onChangeFunc;
	GameActionEditorChangeFixupFunc onChangeFixupFunc;
	void *onChangeData;

	GameActionEditorChangeFunc onCloseFunc;
	void *onCloseData;

	bool bIsRefreshing;
	bool bIsBeingDestroyed;

	char* pcSrcZoneMap;

} GameActionEditor;

//-----------------------
// Forward declarations
//-----------------------

static void gaeditor_Update(GameActionEditor *pEditor);

//-----------------------
// GameActionEditor
//-----------------------

static bool gaeditor_IsEditable(GameActionEditor *pEditor)
{
	// always allow edits for now
	return true;
}

static void gaeditor_DataChangedCB(GameActionEditor *pEditor, bool bUndoable)
{
	gaeditor_Update(pEditor);
}

static GEActionGroup *GAECreateActionGroup(GameActionEditor *pEditor)
{
	GEActionGroup *pGroup = calloc(1, sizeof(GEActionGroup));
	pGroup->pWidgetParent = UI_WIDGET(pEditor->pScrollArea);
	pGroup->dataChangedCB = gaeditor_DataChangedCB;
	pGroup->updateDisplayCB = gaeditor_Update;
	pGroup->isEditableFunc = gaeditor_IsEditable;
	pGroup->pUserData = pEditor;
	return pGroup;
}

static void gaeditor_NewAction(UIButton *pButton, GameActionEditor *pEditor)
{
	WorldGameActionProperties *pNewAction = StructCreate(parse_WorldGameActionProperties);
	pNewAction->eActionType = WorldGameActionType_GiveItem;
	eaPush(&pEditor->actionBlock.eaActions, pNewAction);
	gaeditor_Update(pEditor);
}

static void gaeditor_Update(GameActionEditor *pEditor)
{
	// Create necessary action groups
	int total = eaSize(&pEditor->actionBlock.eaActions);
	F32 y = 0;
	int i;

	if (pEditor->bIsRefreshing)
		return;

	pEditor->bIsRefreshing = true;
	
	while(total > eaSize(&pEditor->eaActionGroups)) {
		eaPush(&pEditor->eaActionGroups, GAECreateActionGroup(pEditor));
	}

	// Update the action groups
	for(i=0; i<eaSize(&pEditor->actionBlock.eaActions); ++i) {	
		assert(pEditor->eaActionGroups);
		y = GEUpdateAction(pEditor->eaActionGroups[i], "Action", pEditor->pcSrcZoneMap, &pEditor->actionBlock.eaActions, &pEditor->origBlock.eaActions, y, i);
	}
	if (pEditor->onChangeFixupFunc)
	{
		pEditor->onChangeFixupFunc(&pEditor->actionBlock, pEditor->onChangeData);

		// Do the update loop again -- the fixup function may have
		// changed the data's values
		y = 0;
		for(i=0; i<eaSize(&pEditor->actionBlock.eaActions); ++i) {	
			assert(pEditor->eaActionGroups);
			y = GEUpdateAction(pEditor->eaActionGroups[i], "Action", pEditor->pcSrcZoneMap, &pEditor->actionBlock.eaActions, &pEditor->origBlock.eaActions, y, i);
		}
	}

	while (i < eaSize(&pEditor->eaActionGroups)) {
		GEFreeActionGroup(pEditor->eaActionGroups[i]);
		eaRemove(&pEditor->eaActionGroups, i);
	}

	if (!pEditor->pNewActionButton){
		pEditor->pNewActionButton = ui_ButtonCreate("New Action", 0, 0, gaeditor_NewAction, pEditor);
		ui_ScrollAreaAddChild(pEditor->pScrollArea, UI_WIDGET(pEditor->pNewActionButton));
	}
	ui_WidgetSetPosition(UI_WIDGET(pEditor->pNewActionButton), 10, y);

	ui_ScrollAreaSetSize(pEditor->pScrollArea, 0, y + pEditor->pNewActionButton->widget.height + 12);

	pEditor->bIsRefreshing = false;

}

static void gaeditor_SaveButtonCB(UIButton *pSaveButton, GameActionEditor *pEditor)
{
	pEditor->bIsBeingDestroyed = true;
	if (pEditor->onChangeFunc)
		pEditor->onChangeFunc(pEditor, pEditor->onChangeData);
	pEditor->bIsBeingDestroyed = false;
	gaeditor_Destroy(pEditor);
}

static void gaeditor_CancelButtonCB(UIButton *pSaveButton, GameActionEditor *pEditor)
{
	gaeditor_Destroy(pEditor);
}

static bool gaeditor_WindowClosed(UIWindow *window, GameActionEditor *pEditor)
{
	gaeditor_Destroy(pEditor);
	return true;
}

// Creates a GameActionEditor for the specified WorldGameActionBlock.
GameActionEditor* gaeditor_Create(char* pcSrcZoneMap, const WorldGameActionBlock *pActionBlock, const WorldGameActionBlock *pOrigActionBlock, GameActionEditorChangeFunc onChangeFunc, GameActionEditorChangeFixupFunc onChangeFixupFunc, void *onChangeData)
{
	GameActionEditor *pEditor = calloc(1, sizeof(GameActionEditor));
	pEditor->pWindow = ui_WindowCreate("GameAction Editor", 100, 100, 500, 600);
	ui_WindowShow(pEditor->pWindow);
	ui_WindowSetCloseCallback(pEditor->pWindow, gaeditor_WindowClosed, pEditor);
	pEditor->pScrollArea = ui_ScrollAreaCreate(0, 0, 0, 0, 0, 0, false, true);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pEditor->pScrollArea), 1.0, 0.8, UIUnitPercentage, UIUnitPercentage);
	ui_WindowAddChild(pEditor->pWindow, pEditor->pScrollArea);

	// Save button
	pEditor->pSaveButton = ui_ButtonCreate("Save", 0, 0, gaeditor_SaveButtonCB, pEditor);
	ui_WindowAddChild(pEditor->pWindow, pEditor->pSaveButton);
	ui_WidgetSetPositionEx(UI_WIDGET(pEditor->pSaveButton), 30, 10, 0, 0, UIBottomRight);

	// Cancel button
	pEditor->pCancelButton = ui_ButtonCreate("Cancel", 0, 0, gaeditor_CancelButtonCB, pEditor);
	ui_WindowAddChild(pEditor->pWindow, pEditor->pCancelButton);
	ui_WidgetSetPositionEx(UI_WIDGET(pEditor->pCancelButton), 100, 10, 0, 0, UIBottomRight);
	
	if (pOrigActionBlock)
		StructCopyAll(parse_WorldGameActionBlock, pOrigActionBlock, &pEditor->origBlock);
	if (pActionBlock)
		StructCopyAll(parse_WorldGameActionBlock, pActionBlock, &pEditor->actionBlock);

	pEditor->onChangeFunc = onChangeFunc;
	pEditor->onChangeFixupFunc = onChangeFixupFunc;
	pEditor->onChangeData = onChangeData;
	pEditor->pcSrcZoneMap = strdup( pcSrcZoneMap );

	gaeditor_Update(pEditor);

	return pEditor;
}

// Set function to be called on window close
void gaeditor_SetCloseFunc(GameActionEditor *editor, GameActionEditorChangeFunc func, void *data)
{
	editor->onCloseFunc = func;
	editor->onCloseData = data;
}

// Destroys the editor
void gaeditor_Destroy(GameActionEditor *editor)
{
	if (editor && !editor->bIsBeingDestroyed){
		editor->bIsBeingDestroyed = true;
		if (editor->onCloseFunc)
			editor->onCloseFunc(editor, editor->onCloseData);
		eaDestroyEx(&editor->eaActionGroups, GEFreeActionGroup); // Do this before freeing the window
		if (editor->pWindow){
			ui_WidgetQueueFree(UI_WIDGET(editor->pWindow));
			editor->pWindow = NULL;
		}
		free(editor->pcSrcZoneMap);
		free(editor);
	}
}

// Returns the main window for the editor
UIWindow* gaeditor_GetWindow(GameActionEditor *editor)
{
	return editor->pWindow;
}

// --------------------------------------------------------------------
// Functions for UIGameActionEditButton
// --------------------------------------------------------------------

static void ui_GameActionEditButtonUpdateText(UIGameActionEditButton *pButton)
{
	char buffer[100];
	int numActions = 0;

	if (pButton && pButton->pActionBlock)
		numActions = eaSize(&pButton->pActionBlock->eaActions);
	if (numActions == 0)
		sprintf(buffer, "No Actions");
	else if (numActions == 1)
		sprintf(buffer, "1 Action");
	else
		sprintf(buffer, "%d Actions", numActions);

	if (pButton)
		ui_ButtonSetText((UIButton*)pButton, buffer);
}

static void ui_GameActionEditButtonEditorCloseCB(GameActionEditor *pEditor, UIGameActionEditButton *pActionButton)
{
	pActionButton->pEditor = NULL;
}

static void ui_GameActionEditButtonEditorChangeCB(GameActionEditor *pEditor, UIGameActionEditButton *pActionButton)
{
	if (pActionButton->pActionBlock)
		StructCopyAll(parse_WorldGameActionBlock, &pEditor->actionBlock, pActionButton->pActionBlock);
	else
		pActionButton->pActionBlock = StructClone(parse_WorldGameActionBlock, &pEditor->actionBlock);
	
	ui_GameActionEditButtonUpdateText(pActionButton);

	if (pActionButton->onChangeFunc)
		pActionButton->onChangeFunc(pActionButton, pActionButton->onChangeData);
}

static void ui_GameActionEditButtonEditorChangeFixupCB(WorldGameActionBlock *pActionBlock, UIGameActionEditButton *pActionButton)
{
	if (pActionButton->onChangeFixupFunc)
		pActionButton->onChangeFixupFunc(pActionBlock, pActionButton->onChangeData);
}

static void ui_GameActionEditButtonClick(UIButton *pButton, void *unused)
{
	UIGameActionEditButton *pActionButton = (UIGameActionEditButton*)pButton;
	if (!pActionButton->pEditor){
		pActionButton->pEditor = gaeditor_Create(pActionButton->pcSrcZoneMap, pActionButton->pActionBlock, pActionButton->pOrigBlock, ui_GameActionEditButtonEditorChangeCB, ui_GameActionEditButtonEditorChangeFixupCB, pActionButton);
	}
	if (pActionButton->pEditor){
		gaeditor_SetCloseFunc(pActionButton->pEditor, ui_GameActionEditButtonEditorCloseCB, pActionButton);
	}
}

void ui_GameActionEditButtonSetData(UIGameActionEditButton *pButton, const WorldGameActionBlock *pActionBlock, const WorldGameActionBlock *pOrigBlock)
{
	// For now just close the editor if it's open
	// TODO - Refresh editor
	if (pButton->pEditor){
		gaeditor_Destroy(pButton->pEditor);
	}

	if (pActionBlock){
		if (pButton->pActionBlock){
			StructCopyAll(parse_WorldGameActionBlock, pActionBlock, pButton->pActionBlock);
		}else{
			pButton->pActionBlock = StructClone(parse_WorldGameActionBlock, pActionBlock);
		}
	} else if (pButton->pActionBlock){
		StructDestroy(parse_WorldGameActionBlock, pButton->pActionBlock);
		pButton->pActionBlock = NULL;
	}

	if (pOrigBlock){
		if (pButton->pOrigBlock){
			StructCopyAll(parse_WorldGameActionBlock, pOrigBlock, pButton->pOrigBlock);
		}else{
			pButton->pOrigBlock = StructClone(parse_WorldGameActionBlock, pOrigBlock);
		}
	} else if (pButton->pOrigBlock){
		StructDestroy(parse_WorldGameActionBlock, pButton->pOrigBlock);
		pButton->pOrigBlock = NULL;
	}
	
	ui_GameActionEditButtonUpdateText(pButton);
}

UIGameActionEditButton *ui_GameActionEditButtonCreate(char* pcSrcZoneMap, const WorldGameActionBlock *pActionBlock, const WorldGameActionBlock *pOrigBlock, UIGameActionEditButtonChangeFunc onChangeFunc, UIGameActionEditButtonChangeFixupFunc onChangeFixupFunc, void *onChangeData)
{
	UIGameActionEditButton *pButton = calloc(1, sizeof(UIGameActionEditButton));
	F32 height;
	UIStyleFont *font = NULL;

	pButton->onChangeFunc = onChangeFunc;
	pButton->onChangeFixupFunc = onChangeFixupFunc;
	pButton->onChangeData = onChangeData;
	pButton->pActionBlock = StructClone(parse_WorldGameActionBlock, pActionBlock);
	pButton->pOrigBlock = StructClone(parse_WorldGameActionBlock, pOrigBlock);

	ui_WidgetInitialize(UI_WIDGET(pButton), ui_ButtonTick, ui_ButtonDraw, ui_GameActionEditButtonFreeInternal, ui_ButtonInput, ui_WidgetDummyFocusFunc);
	ui_ButtonSetDownCallback((UIButton*)pButton, NULL, NULL);
	ui_ButtonSetUpCallback((UIButton*)pButton, NULL, NULL);
	ui_ButtonSetCallback((UIButton*)pButton, ui_GameActionEditButtonClick, NULL);

	ui_GameActionEditButtonUpdateText(pButton);

	if (UI_GET_SKIN(pButton))
		font = GET_REF(UI_GET_SKIN(pButton)->hNormal);
	height = ui_StyleFontLineHeight(font, 1.f) + UI_STEP;
	ui_WidgetSetDimensions(UI_WIDGET(pButton), 200, height);

	pButton->pcSrcZoneMap = strdup(pcSrcZoneMap);

	return pButton;
}

void ui_GameActionEditButtonFreeInternal(UIGameActionEditButton *pButton)
{
	gaeditor_Destroy(pButton->pEditor);
	StructDestroy(parse_WorldGameActionBlock, pButton->pActionBlock);
	StructDestroy(parse_WorldGameActionBlock, pButton->pOrigBlock);
	free(pButton->pcSrcZoneMap);
	ui_ButtonFreeInternal((UIButton*)pButton);
}


#endif
