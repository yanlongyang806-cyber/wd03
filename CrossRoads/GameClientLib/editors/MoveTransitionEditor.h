#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#ifndef NO_EDITORS

#include "EditorManager.h"
#include "MultiEditField.h"

typedef struct MEField MEField;
typedef struct UITextEntry UITextEntry;
typedef struct UIExpander UIExpander;
typedef struct UIExpanderGroup UIExpanderGroup;
typedef struct UIGimmeButton UIGimmeButton;
typedef struct UILabel UILabel;
typedef struct UIWindow UIWindow;
typedef struct MoveTransitionDoc MoveTransitionDoc;


typedef struct DynMoveTransition DynMoveTransition;


#define MOVE_TRANSITION_EDITOR "Move Transition Editor"
#define DEFAULT_MOVE_TRANSITION_NAME "New_MoveTransition"


// ---- Animation Chart Editor ----

typedef struct MTEUndoData 
{
	DynMoveTransition* pPreObject;
	DynMoveTransition* pPostObject;
} MTEUndoData;

typedef struct MoveTransitionDoc
{
	EMEditorDoc emDoc;

	DynMoveTransition* pOrigObject;
	DynMoveTransition* pObject;
	DynMoveTransition* pNextUndoObject;

	EMPanel *pMTPanel;
	EMPanel *pSearchPanel;

} MoveTransitionDoc;

MoveTransitionDoc* MTEOpenMoveTransition(EMEditor *pEditor, char *pcName, DynMoveTransition *pMoveTransitionIn);
void MTERevertMoveTransition(MoveTransitionDoc *pDoc);
void MTECloseMoveTransition(MoveTransitionDoc *pDoc);
EMTaskStatus MTESaveMoveTransition(MoveTransitionDoc *pDoc, bool bSaveAsNew);
void MTEInitData(EMEditor *pEditor);
void MTEOncePerFrame(MoveTransitionDoc *pDoc);
void MTELostFocus(MoveTransitionDoc *pDoc);
void MTEGotFocus(MoveTransitionDoc *pDoc);

#endif
