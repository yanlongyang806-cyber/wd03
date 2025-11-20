/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
GCC_SYSTEM

#include "StateMachine.h"
#include "FSMEditorMain.h"

#ifndef NO_EDITORS 

#include "cmdparse.h"
#include "Color.h"
#include "earray.h"
#include "EditLibUIUtil.h"
#include "EditLibClipboard.h"
#include "EditorManager.h"
#include "ExpressionEditor.h"
#include "ExpressionPrivate.h" // include this in other files and I will kill you
#include "file.h"
#include "GfxTexAtlas.h"
#include "Prefs.h"
#include "StringCache.h"
#include "StateMachine.h"
#include "inputText.h"
#include "UIGraphNode.h"

#include "inputLib.h"

#include "FSMEditorMain_c_ast.h"
//#include "../Common/AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define FSME_BUTTON_SIZE 20
#define FSME_EXPR_WIDTH 140
#define FSME_STATE_NAME_MAX_LEN 1024
#define FSME_START_STRING "[START] "
#define FSME_START_STRING_LEN 8
#define FSME_STATE_EXP_HEIGHT 25
#define FSME_TRANS_EXP_HEIGHT 50
#define FSME_TRANS_EXP_WIDTH 165
#define FSME_STATE_WIDTH 200
#define FSME_STATE_PAYLOAD "FSME_STATECONNECT"
#define FSME_TRANSBTN_PAYLOAD "FSME_TRANSCONNECT"
#define FSME_TRANS_PAYLOAD "FSME_TRANSMOVE"

/********************************************************************
* EXTERNS AND FORWARD DECLARATIONS
********************************************************************/
extern ParseTable parse_FSMGroup[];
#define TYPE_parse_FSMGroup FSMGroup
extern ParseTable parse_ExprContext[];
#define TYPE_parse_ExprContext ExprContext
extern ParseTable parse_ExprLine[];
#define TYPE_parse_ExprLine ExprLine
extern ParseTable parse_FSMEditorSaveMachine[];
#define TYPE_parse_FSMEditorSaveMachine FSMEditorSaveMachine
extern ParseTable parse_FSMEditorSaveState[];
#define TYPE_parse_FSMEditorSaveState FSMEditorSaveState
extern ParseTable parse_FSMEditorSaveTrans[];
#define TYPE_parse_FSMEditorSaveTrans FSMEditorSaveTrans

extern StaticDefineInt EcbClipTypeEnumEnum[];

typedef struct FSMEditorDoc FSMEditorDoc;
typedef struct FSMEditorMachine FSMEditorMachine;
typedef struct FSMEditorExpr FSMEditorExpr;
typedef struct FSMEditorState FSMEditorState;
typedef struct FSMEditorTransButton FSMEditorTransButton;
typedef struct FSMEditorExprLine FSMEditorExprLine;

static void fsmEditorErrorWinCreate(char **errors);
static void fsmEditorStateRenameWinCreate(FSMEditorState *state);
static void fsmEditorEditExpr(UIButton *button, FSMEditorExpr *expr);
static void fsmEditorEditLine(UIButton *button, FSMEditorExprLine *line);
static void fsmEditorTransExpandCallback(UIExpander *exp, FSMEditorState *state);
static void fsmEditorInTransDelete(UIWidget *unused, FSMEditorTransButton *inButton);
static void fsmEditorOutTransDelete(UIButton *unused, FSMEditorTransButton *outButton);
static void fsmEditorConnectDrag(UIButton *button, FSMEditorState *state);
static void fsmEditorConnectDrop(UIButton *source, UIButton *dest, UIDnDPayload *payload, FSMEditorState *destState);
static void fsmEditorTransDrag(UIExpander *expander, FSMEditorTransButton *trans);
static void fsmEditorTransDrop(UIWidget *source, UIExpander *dest, UIDnDPayload *payload, FSMEditorTransButton *trans);
static void fsmEditorTransButtonDrag(UIButton *button, FSMEditorTransButton *trans);
static void fsmEditorTransButtonDrop(UIButton *source, UIButton *dest, UIDnDPayload *payload, FSMEditorTransButton *trans);
static void fsmEditorExprDrag(UIButton *button, Expression *expr);
static void fsmEditorExprDrop(UIWidget *source, UIButton *dest, UIDnDPayload *payload, FSMEditorExpr *expr);
static void fsmEditorExprLineDrag(UIButton *button, ExprLine *exprLine);
static void fsmEditorExprLineDrop(UIWidget *source, UIButton *dest, UIDnDPayload *payload, FSMEditorExprLine *exprLine);
static bool fsmEditorDeleteContinue(EMEditor *editor, const char *name, void *data, EMResourceState eState, void *data2, bool success);
static bool fsmEditorSaveDocContinue(EMEditor *editor, const char *name, void *data, EMResourceState eState, FSMEditorDoc *activeDoc, bool success);
static void fsmEditorStateWinTick(UIWindow *window, UI_PARENT_ARGS);
static void fsmEditorStateExpandCallback(UIExpander *exp, FSMEditorState *state);
static void fsmEditorStateRename(UIMenuItem *item, FSMEditorState *state);
static void fsmEditorStateSetStart(UIMenuItem *item, FSMEditorState *state);
static void fsmEditorStateDelete(UIMenuItem *item, FSMEditorState *state);
static EMTaskStatus fsmEditorSaveDoc(EMEditorDoc *doc);
static EMTaskStatus fsmEditorSaveDocAs(EMEditorDoc *doc);
static void fsmEditorSelectSubFSM(UIWidget *widget, FSMEditorState *state);
static void fsmEditorSelectSubFSMCreate(FSMEditorState *state);
static void fsmEditorStateSetSubFSM(FSMEditorState *state, const char *subFSMName);
static void fsmEditorSubFSMClear(UIButton *button, FSMEditorState *state);
static void fsmEditorSubFSMOpen(UIButton *button, FSMEditorState *state);
static void fsmEditorStateCommentGotFocus(UITextArea *textArea, FSMEditorState *state);
static void fsmEditorStateCommentLostFocus(UITextArea *textArea, FSMEditorState *state);
static void fsmEditorStateCommentChanged(UITextArea *textArea, FSMEditorState *state);
static void fsmEditorStateExpandState(UIMenuItem *item, FSMEditorState *state);
static void fsmEditorStateExpandTrans(UIMenuItem *item, FSMEditorState *state);
static void fsmEditorStateExpandAll(UIMenuItem *item, FSMEditorState *state);
static void fsmEditorStateCollapseState(UIMenuItem *item, FSMEditorState *state);
static void fsmEditorStateCollapseTrans(UIMenuItem *item, FSMEditorState *state);
static void fsmEditorStateCollapseAll(UIMenuItem *item, FSMEditorState *state);
static void fsmEditorStateCopyToClipboard(UIMenuItem *item, FSMEditorState *state);
static void fsmEditorPopulateFSMState(FSMEditorState *state, FSMState *newState, bool saveLayout);
static void fsmEditorLoadTrans(FSMEditorState *startState, FSMEditorState *endState, FSMTransition *trans);
static bool fsmEditorLoadState(FSMEditorState *edState, FSMState *state);

static const char *fsmEditorGetStateName(FSMEditorState *state);

static void fsmEditorUpdateCableColors(FSMEditorDoc *doc);
static void fsmEditorUpdateCableColorsForState(FSMEditorState *pState, Color inColor, Color outColor, F32 lineScale);
static void fsmEditorCopySelectionToClipboard(UIMenuItem *item, FSMEditorDoc *doc);
static void fsmEditorDeleteSelection(UIMenuItem *item, FSMEditorDoc *doc);
static void fsmEditorSetCommentVisible(FSMEditorState *edState, bool bVisible);
static void fsmEditorStateAddComment(UIMenuItem *item, FSMEditorState *state);

bool fsmEditorOnScrollAreaInput(UIScrollArea *pScrollArea, KeyInput *key);
static void fsmEditorOnGraphNodeLostFocus(UIAnyWidget *graphNode, UserData data);

static void fsmEditorFindTextDialog(FSMEditorDoc *doc);
static void fsmEditorClearSearch(FSMEditorState *pState);

/********************************************************************
* PRIVATE STRUCTS
********************************************************************/
typedef struct FSMEditorNewDocParams
{
	UIWindow *window;
	UITextEntry *nameEntry;
	UITextEntry *scopeEntry;
	UILabel *fileNameLabel;
	UIButton *okButton;
	FSMEditorMachine *workingFSM;
	bool bCombatFSMRequested;
} FSMEditorNewDocParams;

#endif

// Local FSM layout structs
AUTO_STRUCT;
typedef struct FSMEditorSaveTrans
{
	char *outStateName;
	bool expOpen;
} FSMEditorSaveTrans;

AUTO_STRUCT;
typedef struct FSMEditorSaveState
{
	char *name;
	int x;
	int y;
	bool onEntryFExpOpen;
	bool onEntryFFlavExpOpen;
	bool onEntryExpOpen;
	bool onEntryFlavExpOpen;
	bool actionExpOpen;
	bool actionFlavExpOpen;
	bool subFSMExpOpen;
	FSMEditorSaveTrans **trans;
} FSMEditorSaveState;

// FSN autosave struct
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct FSMSaveHolder
{
	FSM *fsm;    AST(NAME("FSM"))
} FSMSaveHolder;

#ifndef NO_EDITORS

// Internal FSM representations
typedef struct FSMEditorExprLine
{
	ExprLine *line;
	FSMEditorExpr *expr;
	UIButton *button;
} FSMEditorExprLine;

typedef struct FSMEditorExpr
{
	Expression *expr;
	UIButton *editAll;
	FSMEditorExprLine **lines;
	UIExpander *expander;
	FSMEditorExpr *belowExpr;
	FSMEditorState *state;
	const char *name;
} FSMEditorExpr;

typedef struct FSMEditorTransButton
{
	FSMEditorState *state;
	FSMEditorTransButton *pair;

	FSMTransitionLayout *layout;		// layout loaded from the file

	// below are specifically used for outward transitions
	UIExpander *expander;
	FSMEditorExpr *condExpr;
	FSMEditorExpr *actionExpr;
	UIPairedBox *pBox;
} FSMEditorTransButton;

typedef struct FSMEditorState
{
	UIGraphNode *stateWin;
	UITextArea *comment;
	FSMEditorTransButton **connsIn, **connsOut;
	UIExpanderGroup *stateExpGrp;
	FSMEditorExpr *onEntryFExpr;
	FSMEditorExpr *onEntryExpr;
	FSMEditorExpr *actionExpr;
	REF_TO(FSM) subFSM;
	UIButton *in, *out;
	UIExpanderGroup *connsExpGrp;
	UIExpander *subFSMExp;
	UIButton *subFSMButton;
	const char *oldName;	// used for copying/pasting (maintaining connections)
	FSMEditorMachine *machine;
	FSMStateLayout *layout;				// layout loaded from the file

	void *userData;

	U8 bIsCommentVisible : 1;

} FSMEditorState;

typedef struct FSMEditorMachine
{
	FSMEditorState **states;
	FSMEditorState *startState;
	char name[80];
	char scope[260];
	FSMEditorDoc *doc;

	// subFSM Override
	REF_TO(FSMState) overrideStatePath;
	REF_TO(FSM) overrideFSM;
} FSMEditorMachine;

typedef struct FSMEditorUI
{
	struct
	{
		UIWindow *window;
		UIScrollArea *area;
	} stateMachine;

	struct
	{
		UIWindow *window;
		UITextEntry *entry;
		UIList *list;
		UIComboBox *cb1, *cb2;
		void *object;
		REF_TO(FSM) fsmHandle;
	} modalData;

	UITextArea *fsmCommentArea;

	UISkin *redSkin;
	UISkin *blueSkin;
	UISkin *greenSkin;
	UISkin *unselectedNodeSkin;
	UISkin *selectedNodeSkin;

} FSMEditorUI;

#define SAVE_AS_NEW    0x01
#define SAVE_OVERWRITE 0x02
#define SAVE_LAYOUT    0x04

typedef struct FSMEditorDoc
{
	EMEditorDoc doc;
	FSMEditorUI fsmEditorUI;
	FSMEditorMachine workingFSM;
	FSMGroup *refGroup;
	REF_TO(FSM) refFSM;
	char startPath[MAX_PATH];
	const char **eaFSMNames;

	UIGraphNode **ppSelectedGraphNodes;

	char *origName;
	char *origFile;
	bool layoutChanged;
	int saveFlags;

	UIMenu *pRightClickMenu;
	UIMenu *pScrollAreaRightClickMenu;
	Vec2 lastMousePoint;
	Vec2 mouseDownPoint;
} FSMEditorDoc;

FSMEditorGroupRoot fsmEditorGroups;
static ResourceGroup fsmPickerTree;
StashTable fsmEditorGroupContexts;
StashTable fsmEditorGroupCallbacks;
static bool bPickerRefreshRequested = false;

static EMEditor fsmEditor;
static EMPicker fsmPicker;
static int fsmFinishedLoading = 0;
static char **geaScopes = NULL;
static bool s_FSMDictChanged = false;
static bool s_FSMIndexChanged = false;
static char **s_FSMsThatChanged = NULL;

static bool fsmEditorCanEdit(FSMEditorDoc *doc)
{
	ResourceInfo *info;
	
	// Can always edit if this is a new doc
	if (!doc->origName)
	{
		return true;
	}

	// Cannot edit if not writable
	info = resGetInfo(gFSMDict, doc->origName);
	if (info && !resIsWritable(info->resourceDict, info->resourceName))
	{
		emQueuePrompt(EMPROMPT_CHECKOUT, &doc->doc, NULL, gFSMDict, doc->origName);
		return false;
	}

	return true;
}

static FSMEditorExprLine *fsmEditorExprLineCreate(UIButton *button, ExprLine *line, FSMEditorExpr *expr)
{
	FSMEditorExprLine *newLine = calloc(1, sizeof(FSMEditorExprLine));
	newLine->expr = expr;
	newLine->button = button;
	newLine->line = line;
	return newLine;
}

static void fsmEditorExprLineFree(FSMEditorExprLine *line)
{
	free(line);
}

static FSMEditorExpr *fsmEditorExprCreate(FSMEditorState *state, FSMEditorExprLine **lines)
{
	FSMEditorExpr *newExpr = calloc(1, sizeof(FSMEditorExpr));
	newExpr->state = state;
	newExpr->lines = lines;
	newExpr->expr = exprCreate();
	newExpr->belowExpr = NULL;
	return newExpr;
}

static void fsmEditorExprFree(FSMEditorExpr *edExpr)
{
	eaDestroyEx(&edExpr->lines, fsmEditorExprLineFree);
	free(edExpr);
}

#define fsmEditorTransButtonCreate(pBox) fsmEditorTransButtonCreateEx(pBox, NULL, NULL, NULL);
static FSMEditorTransButton *fsmEditorTransButtonCreateEx(UIPairedBox *pBox, FSMEditorExpr *condExpr, FSMEditorExpr *actionExpr, UIExpander *expander)
{
	FSMEditorTransButton *tButton = calloc(1, sizeof(FSMEditorTransButton));
	tButton->pBox = pBox;
	tButton->condExpr = condExpr;
	tButton->actionExpr = actionExpr;
	tButton->expander = expander;
	return tButton;
}

static void fsmEditorTransButtonFree(FSMEditorTransButton *button)
{
	if (button->expander)
	{
		fsmEditorExprFree(button->condExpr);
		fsmEditorExprFree(button->actionExpr);
		ui_WidgetQueueFree((UIWidget*) button->expander);
	}
	else
	{
		ui_WidgetQueueFree((UIWidget*) button->pBox);		
	}
	free(button);
}

static FSMEditorState *fsmEditorStateCreate(UIGraphNode *stateWin, UIButton *in, UIButton *out, UIExpanderGroup *conns)
{
	FSMEditorState *state = calloc(1, sizeof(FSMEditorState));
	state->stateWin = stateWin;
	state->connsIn = NULL;
	state->connsOut = NULL;
	state->in = in;
	state->out = out;
	state->connsExpGrp = conns;
	state->onEntryFExpr = fsmEditorExprCreate(state, NULL);
	state->onEntryExpr = fsmEditorExprCreate(state, NULL);
	state->actionExpr = fsmEditorExprCreate(state, NULL);	
	state->subFSMExp = NULL;
	state->subFSMButton = NULL;
	return state;
}

static void fsmEditorStateFree(FSMEditorState *state)
{
	eaDestroyEx(&state->connsIn, fsmEditorTransButtonFree);
	eaDestroyEx(&state->connsOut, fsmEditorTransButtonFree);
	eaClear(&state->connsExpGrp->childrenInOrder);
	ui_WidgetQueueFree((UIWidget*) state->stateWin);
	fsmEditorExprFree(state->onEntryFExpr);
	fsmEditorExprFree(state->onEntryExpr);
	fsmEditorExprFree(state->actionExpr);
	REMOVE_HANDLE(state->subFSM);
	free(state);
}

/********************************************************************
* PREF FUNCTIONS
********************************************************************/
static int iFSMPrefSet = -1;

static void FSMPrefFormatName(char buf[], const char *doc_name, const char *state_name)
{
	quick_sprintf(buf, 260, "FSM Editor.%s.%s", doc_name, state_name);
}

static int FSMPrefGetPrefSet(void)
{
	if (iFSMPrefSet < 0) {
		// Initialize the pref set
		char buf[260];
		sprintf(buf, "%s/editor/fsmprefs.pref", fileLocalDataDir());
		iFSMPrefSet = PrefSetGet(buf);
	}
	return iFSMPrefSet;
}

static void FSMPrefGetStruct(const char *doc_name, const char *state_name, ParseTable *pParseTable, void *pStruct)
{
	char buf[260];
	FSMPrefFormatName(buf, doc_name, state_name);
	PrefGetStruct(FSMPrefGetPrefSet(), buf, pParseTable, pStruct);
}

static void FSMPrefStoreStruct(const char *doc_name, const char *state_name, ParseTable *pParseTable, void *pStruct)
{
	char buf[260];
	FSMPrefFormatName(buf, doc_name, state_name);
	PrefStoreStruct(FSMPrefGetPrefSet(), buf, pParseTable, pStruct);
}

static void fsmEditorDocPrefsSave(FSMEditorDoc *doc)
{
	FSMEditorMachine *fsm;
	FSMEditorSaveState *stateUI;
	FSMEditorSaveTrans *transUI;
	int i, j;

	fsm = &doc->workingFSM;
	if (!fsm)
		return;

	for (i = 0; i < eaSize(&fsm->states); i++)
	{
		FSMEditorState *state = fsm->states[i];

		// populate the UI save state
		stateUI = StructCreate(parse_FSMEditorSaveState);
		stateUI->x = state->stateWin->window.widget.x;
		stateUI->y = state->stateWin->window.widget.y;
		stateUI->name = StructAllocString(fsmEditorGetStateName(state));
		stateUI->onEntryExpOpen = ui_ExpanderIsOpened(state->onEntryExpr->expander);
		stateUI->onEntryFExpOpen = ui_ExpanderIsOpened(state->onEntryFExpr->expander);
		stateUI->subFSMExpOpen = ui_ExpanderIsOpened(state->subFSMExp);
		stateUI->actionExpOpen = ui_ExpanderIsOpened(state->actionExpr->expander);

		for (j = 0; j < eaSize(&state->connsOut); j++)
		{
			FSMEditorTransButton *button = state->connsOut[j];

			transUI = StructCreate(parse_FSMEditorSaveTrans);
			transUI->outStateName = StructAllocString(ui_WidgetGetText(UI_WIDGET(button->expander)));
			transUI->expOpen = ui_ExpanderIsOpened(button->expander);
			eaPush(&stateUI->trans, transUI);
		}

		FSMPrefStoreStruct(doc->doc.doc_name, stateUI->name, parse_FSMEditorSaveState, stateUI);
	}
}

static void fsmEditorDocPrefsApply(FSMEditorDoc *doc, bool fromFSM)
{
	FSMEditorMachine *fsm = &doc->workingFSM;
	int i, j, k;

	for (i = 0; i < eaSize(&fsm->states); i++)
	{
		FSMEditorState *state = fsm->states[i];

		if (fromFSM)
		{
			if (state->layout)
			{
				ui_WidgetSetPosition((UIWidget*) &state->stateWin->window, state->layout->x, state->layout->y);
				ui_ExpanderSetOpened(state->onEntryExpr->expander, state->layout->onEntryOpen);
				ui_ExpanderSetOpened(state->onEntryFExpr->expander, state->layout->onFirstEntryOpen);
				ui_ExpanderSetOpened(state->actionExpr->expander, state->layout->actionOpen);
				ui_ExpanderSetOpened(state->subFSMExp, state->layout->subFSMOpen);			
			}
			for (j = 0; j < eaSize(&state->connsOut); j++)
			{
				FSMEditorTransButton *trans = state->connsOut[j];
				if (trans->layout)
					ui_ExpanderSetOpened(trans->expander, trans->layout->open);
			}
		}
		else
		{
			FSMEditorSaveState *stateUI = StructCreate(parse_FSMEditorSaveState);
			FSMPrefGetStruct(doc->doc.doc_name, fsmEditorGetStateName(state),parse_FSMEditorSaveState, stateUI); 

			// Skip if the loaded state wasn't there
			if (!stateUI->name)
			{
				StructDestroy(parse_FSMEditorSaveState, stateUI);
				continue;
			}

			ui_WidgetSetPosition((UIWidget*) &state->stateWin->window, stateUI->x, stateUI->y);
			ui_ExpanderSetOpened(state->onEntryExpr->expander, stateUI->onEntryExpOpen);
			ui_ExpanderSetOpened(state->onEntryFExpr->expander, stateUI->onEntryFExpOpen);
			ui_ExpanderSetOpened(state->actionExpr->expander, stateUI->actionExpOpen);
			ui_ExpanderSetOpened(state->subFSMExp, stateUI->subFSMExpOpen);

			// deal with the transitions
			for (j = 0; j < eaSize(&state->connsOut); j++)
			{
				FSMEditorSaveTrans *transUI = NULL;
				for (k = 0; k < eaSize(&stateUI->trans); k++)
				{
					if (strcmpi(fsmEditorGetStateName(state->connsOut[j]->pair->state), stateUI->trans[k]->outStateName) == 0)
					{
						transUI = stateUI->trans[k];
						break;
					}
				}
				if (!transUI)
					continue;

				ui_ExpanderSetOpened(state->connsOut[j]->expander, transUI->expOpen);

				// remove the transition from the UI struct to process multiple transitions between the same two states
				StructDestroy(parse_FSMEditorSaveTrans, transUI);
				eaRemove(&stateUI->trans, k);
			}

			// Clean up
			StructDestroy(parse_FSMEditorSaveState, stateUI);
		}
	}
}

/********************************************************************
* UTIL FUNCTIONS
********************************************************************/
/******
* This function returns the name of a state.
* PARAMS:
*   state - FSMEditorState
* RETURNS:
*   state's name.
******/
static const char *fsmEditorGetStateName(FSMEditorState *state)
{
	// get state name from window text, minus the start state prefix if applicable
	const char *stateWinText = ui_WidgetGetText(UI_WIDGET(state->stateWin));
	if (strstri(stateWinText, FSME_START_STRING) == stateWinText)
		return stateWinText + strlen(FSME_START_STRING);
	else
		return stateWinText;
}

/******
* This function takes a name and returns the state that has that name.
* PARAMS:
*   activeDoc - FSMEditorDoc where the state belongs
*   name - name of the state to find
* RETURNS:
*   FSMEditorState in activeDoc with the passed-in name; NULL if not found
******/
static FSMEditorState *fsmEditorGetStateByName(FSMEditorDoc *activeDoc, const char *name)
{
	int i;
	const char *colon;

	if (!name)
		return NULL;

	// deal with the colon-style reference to states (created during fsmLoad)
	colon = strchr(name, ':');
	if (!colon)
		colon = name;
	else
		colon += 1;

	// search for the state with the same name
	for (i = 0; i < eaSize(&activeDoc->workingFSM.states); i++)
		if (strcmpi(fsmEditorGetStateName(activeDoc->workingFSM.states[i]), colon) == 0)
			return activeDoc->workingFSM.states[i];

	// return NULL if not found
	return NULL;
}

/******
* This function indicates whether a particular expression is a "state" expression (action, onEntry, etc).
* PARAMS:
*   state - FSMEditorState whose expressions will be compared to expr
*   expr - FSMEditorExpr expression to check
* RETURNS:
*   bool indicating whether expr is a "state" expression of state
******/
static bool fsmEditorIsStateExpr(FSMEditorState *state, FSMEditorExpr *expr)
{
	return (state->onEntryExpr == expr || state->onEntryFExpr == expr || state->actionExpr == expr);
}

/******
* This function sets the text on a button, clipping text off as necessary when it would not
* fit on the button.
* PARAMS:
*   button - UIBUtton where the text will be set
*   text - the text to set onto the button
******/
static void fsmEditorSetButtonText(UIButton *button, char *text)
{
	char *bText = NULL;
	char temp = 0;
	int i = -1;
	estrCreate(&bText);
	estrCopy2(&bText, text);
	ui_StyleFontUse(NULL, false, UI_WIDGET(button)->state);

	// loop to find the first substring that would fit entirely on the button
	do
	{
		if (i > -1)
			bText[i] = temp;
		i++;
		temp = bText[i];
		bText[i] = '\0';
	} while ((ui_StyleFontWidth(NULL, 1, bText) < (button->widget.width - 10)) && (bText[i + 1] != '\0'));

	if (bText[i + 1] == '\0')
		bText[i] = temp;
	if ((i < EXPR_ED_MAX_EXPR_LEN - 1) && (bText[i + 1] != '\0') && (i > 0))
		bText[i - 1] = '\0';

	// set the partial string
	ui_ButtonSetText(button, bText);
	estrDestroy(&bText);
}

/******
* This function moves the in/out buttons up or down since they are not part of expanders and
* are not dealt with automatically when other connection expanders are opened or closed.  The
* buttons are using the bottom of the state window as the reference.
* PARAMS:
*   state - FSMEditorState on which the buttons reside
*   yDiff - int amount to move the buttons down (negative value moves them up)
******/
static void fsmEditorMoveButtonsY(FSMEditorState *state, int yDiff)
{
	int i;

	// all in connection paired boxes will move
	for (i = 0; i < eaSize(&state->connsIn); i++)
	{
		FSMEditorTransButton *transButton = state->connsIn[i];
		transButton->pBox->widget.y += yDiff;
	}

	// fix the "IN" and "OUT" draggable buttons
	state->in->widget.y += yDiff;
	state->out->widget.y += yDiff;
}

/******
* This function refreshes the UI for a particular expression.
* PARAMS:
*   expr - the FSMEditorExpr to refresh
******/
static void fsmEditorRefreshExprUI(FSMEditorExpr *expr)
{
	int i;
	UIButton *button;
	FSMEditorExprLine *newLine;
	int startx = expr->editAll->widget.x + expr->editAll->widget.width;
	int y = expr->editAll->widget.y;
	int oldShift, shiftAmt;
	char *editAllTooltipString = NULL;
	int numLines;

	// free all of the line buttons in the expression
	for (i = 0; i < eaSize(&expr->lines); i++)
	{
		ui_WidgetQueueFree((UIWidget*) expr->lines[i]->button);
		ui_ExpanderRemoveChild(expr->expander, (UIWidget*) expr->lines[i]->button);
	}
	eaDestroyEx(&expr->lines, fsmEditorExprLineFree);

	// shrink the expander (and window, if expander open) to fit one line
	oldShift = MAX(i - 1, 0) * FSME_BUTTON_SIZE;

	numLines = eaSize(&expr->expr->lines);
	if(expr->name && numLines > 0)
	{
		char *str = NULL;
		
		estrPrintf(&str, "%s (%d)", expr->name, numLines);
		ui_WidgetSetTextString(UI_WIDGET(expr->expander), str);
	
		estrDestroy(&str);
	}
	else if(expr->name)
	{
		ui_WidgetSetTextString(UI_WIDGET(expr->expander), expr->name);
	}

	// recreate the line buttons
	for (i = 0; i < numLines; i++)
	{
		button = ui_ButtonCreate(NULL, expr->editAll->widget.x + expr->editAll->widget.width, y, NULL, NULL);
		ui_WidgetSetDimensionsEx((UIWidget*) button, FSME_EXPR_WIDTH, FSME_BUTTON_SIZE, UIUnitFixed, UIUnitFixed);
		if (expr->expr->lines[i]->descStr && strlen(expr->expr->lines[i]->descStr) > 0)
		{
			char bStr[EXPR_ED_MAX_DESC_LEN + 2];
			sprintf(bStr, "[%s]", expr->expr->lines[i]->descStr);
			fsmEditorSetButtonText(button, bStr);

			ui_WidgetSetTooltipString((UIWidget*)button, expr->expr->lines[i]->descStr);

			estrAppend2(&editAllTooltipString, expr->expr->lines[i]->descStr);
			estrAppend2(&editAllTooltipString, "<br>");
		}
		else if (expr->expr->lines[i]->origStr)
		{
			fsmEditorSetButtonText(button, expr->expr->lines[i]->origStr);
			ui_WidgetSetTooltipString((UIWidget*)button, expr->expr->lines[i]->origStr);

			estrAppend2(&editAllTooltipString, expr->expr->lines[i]->origStr);
			estrAppend2(&editAllTooltipString, "<br>");
		}
		else
		{
			fsmEditorSetButtonText(button, "");
		}
		

		newLine = fsmEditorExprLineCreate(button, expr->expr->lines[i], expr);
		ui_ButtonSetCallback(button, fsmEditorEditLine, newLine);
		ui_WidgetSetDragCallback((UIWidget*) button, fsmEditorExprLineDrag, newLine->line);
		ui_WidgetSetDropCallback((UIWidget*) button, fsmEditorExprLineDrop, newLine);
		eaPush(&expr->lines, newLine);
		ui_ExpanderAddChild(expr->expander, (UIWidget*) button);
		y += FSME_BUTTON_SIZE;
	}

	// reset the expression button drag callback data
	ui_WidgetSetDragCallback((UIWidget*) expr->editAll, fsmEditorExprDrag, expr->expr);

	// resize the edit all button and set its tooltip
	ui_WidgetSetDimensionsEx((UIWidget*) expr->editAll, expr->editAll->widget.width, MAX(i, 1) * FSME_BUTTON_SIZE, UIUnitFixed, UIUnitFixed);
	
	ui_WidgetSetTooltipString((UIWidget*) expr->editAll, editAllTooltipString);
	ui_WidgetSetTooltipString((UIWidget*) expr->expander, editAllTooltipString);

	// resize expander (and window, if expander open)
	shiftAmt = MAX(i - 1, 0) * FSME_BUTTON_SIZE - oldShift;
	
	ui_ExpanderSetHeight(expr->expander, expr->expander->openedHeight + shiftAmt);

	if (!expr->expander->widget.childrenInactive)
	{
		expr->state->stateWin->window.widget.height += shiftAmt;

		// shift appropriate connection buttons
		if (!fsmEditorIsStateExpr(expr->state, expr))
			fsmEditorMoveButtonsY(expr->state, shiftAmt);
	}

	// shift any expressions below
	if (expr->belowExpr)
	{
		expr->belowExpr->editAll->widget.y += shiftAmt;
		for (i = 0; i < eaSize(&expr->belowExpr->lines); i++)
			expr->belowExpr->lines[i]->button->widget.y += shiftAmt;
	}

	estrDestroy(&editAllTooltipString);
}

/*****
* This function refreshes a transition's expander name to match its state and destination.
* PARAMS:
*   trans - FSMEditorTransButton for the transition to refresh
******/
static void fsmEditorRefreshTransUI(FSMEditorTransButton *trans)
{
	if (trans->expander)
	{
		ui_WidgetSetTextString((UIWidget*) trans->expander, trans->pair ? fsmEditorGetStateName(trans->pair->state) : "[DISCONNECTED]");
	}
}

/******
* This function creates a UI for an expression.
* PARAMS:
*   x - int x coord of where to put the UI within an expander
*   y - int y coord of where to put the UI within an expander
*   expr - FSMEditorExpr that will hold the UI widgets created and contains the expression
*          used to generate the UI
*   exp - the expander where the UI will reside
* RETURNS:
*   the newly created "Edit All" (">") UIButton
******/
static UIButton *fsmEditorCreateExprUI(int x, int y, FSMEditorExpr *expr, UIExpander *exp, const char *name)
{
	UIButton *editAll = ui_ButtonCreate(">", x, y, fsmEditorEditExpr, expr);
	ui_WidgetSetDragCallback((UIWidget*) editAll, fsmEditorExprDrag, expr->expr);
	ui_WidgetSetDropCallback((UIWidget*) editAll, fsmEditorExprDrop, expr);
	expr->editAll = editAll;
	expr->expander = exp;
	expr->name = strlen(name) > 0 ? strdup(name) : NULL;
	ui_ExpanderAddChild(exp, (UIWidget*) editAll);
	fsmEditorRefreshExprUI(expr);
	return editAll;
}

/******
* This function creates a unique state name.
* PARAMS:
*   activeDoc - FSMEditorDoc to use when determining name uniqueness
*   newName - where the new name will be stored
*   newNameLen - size of newName buffer
******/
static void fsmEditorUniqueStateName(FSMEditorDoc *activeDoc, char *newName, int newName_size)
{
	int i = 0;

	// start with "NewState0" and keep incrementing the number suffix until it is unique
	strcpy_s(SAFESTR2(newName), "NewState0");
	while (fsmEditorGetStateByName(activeDoc, newName))
	{
		i++;
		sprintf_s(SAFESTR2(newName), "NewState%i", i);
	}
}

/******
* This function refreshes the top-level UI for a state, such as the state's window title and 
* its transition expander names (if, for instance, a connected state's name was changed).
* PARAMS:
*   state - FSMEditorState to refresh
******/
static void fsmEditorRefreshState(FSMEditorState *state)
{
	FSMEditorDoc *activeDoc = state->machine->doc;
	int i;
	char stateText[FSME_STATE_NAME_MAX_LEN + FSME_START_STRING_LEN];

	// rename the window
	if (state == activeDoc->workingFSM.startState)
	{
		sprintf(stateText, "%s%s", FSME_START_STRING, fsmEditorGetStateName(state));
		ui_WindowSetTitle(&state->stateWin->window, stateText);
	}
	else
	{
		strcpy(stateText, fsmEditorGetStateName(state));
		ui_WindowSetTitle(&state->stateWin->window, stateText);
	}

	// rename transition expanders
	for (i = 0; i < eaSize(&state->connsOut); i++)
		fsmEditorRefreshTransUI(state->connsOut[i]);
	for (i = 0; i < eaSize(&state->connsIn); i++)
		fsmEditorRefreshTransUI(state->connsIn[i]->pair);
}

/******
* This function takes a state machine and rearranges the state windows into an adjacent flow pattern.
* This function does not (yet) determine optimal flow layout; it only does a simple flow.
* PARAMS:
*   edFSM - FSMEditorMachine to reflow
******/
static void fsmEditorReflow(FSMEditorMachine *edFSM)
{
	int xBounds, yBounds, i, j;
	int *colMaxY = NULL;
	int minYDiff, minYDiffIdx;
	const int areaOffset = 40;
	const int barHeight = 30;
	const int spacer = 30;

	// calculate the window's bounds to determine what should get priority - adding a new col
	// or adding to an existing col and making it larger
	xBounds = edFSM->doc->fsmEditorUI.stateMachine.window->widget.width;
	yBounds = edFSM->doc->fsmEditorUI.stateMachine.window->widget.height;
	if (xBounds < 0)
		xBounds = 0;
	if (yBounds < 0)
		yBounds = 0;

	// create an EArray of Y bounds to store column heights
	for (i = 0; i < xBounds / (FSME_STATE_WIDTH + spacer); i++)
		eaiPush(&colMaxY, 0);
	if (eaiSize(&colMaxY) == 0)
		eaiPush(&colMaxY, 0);
	xBounds = (FSME_STATE_WIDTH + spacer) * eaiSize(&colMaxY);

	// move each window
	for (i = 0; i < eaSize(&edFSM->states); i++)
	{
		UIWindow *window = &edFSM->states[i]->stateWin->window;

	//FOR_EACH_IN_EARRAY(edFSM->doc->ppSelectedGraphNodes, UIGraphNode, pNode)
	//{
	//	FSMEditorState *pState = (FSMEditorState*)pNode->userData;
	//	if(pState)
	//	{
	//		UIWindow *window = &pState->stateWin->window;

		bool placed = false;
		int height = window->widget.height + barHeight;
		minYDiff = -1;
		minYDiffIdx = -1;

		// look for a nice-fitting place to put the state that won't extend the boundaries
		for (j = 0; j < eaiSize(&colMaxY); j++)
		{
			if (yBounds - colMaxY[j] >= height + areaOffset)
			{
				ui_WidgetSetPosition((UIWidget*) window, areaOffset + j * (FSME_STATE_WIDTH + spacer), colMaxY[j] + areaOffset);
				colMaxY[j] += (height + spacer);
				placed = true;
				break;
			}
			else if (colMaxY[j] + height + spacer - yBounds < minYDiff || minYDiff == -1)
			{
				minYDiff = colMaxY[j] + height + spacer - yBounds;
				minYDiffIdx = j;
			}
		}

		// if such a nice place does not exist determine how to extend the boundaries and do it
		if (!placed)
		{
			// check whether it's more economical to extend in y direction or x direction
			if ((yBounds < xBounds) && (minYDiff < FSME_STATE_WIDTH + spacer || minYDiff == (height + spacer)))
			{
				// extend Y and place where min diff was found
				yBounds += minYDiff;
				ui_WidgetSetPosition((UIWidget*) window, areaOffset + minYDiffIdx * (FSME_STATE_WIDTH + spacer), colMaxY[minYDiffIdx] + areaOffset);
				colMaxY[minYDiffIdx] = yBounds;
			}
			else
			{
				// extend X and place at end
				ui_WidgetSetPosition((UIWidget*) window, xBounds + areaOffset, areaOffset);
				eaiPush(&colMaxY, height + spacer);
				xBounds = (FSME_STATE_WIDTH + spacer) * eaiSize(&colMaxY);
				yBounds = MAX(yBounds, height + spacer);
			}
		}
	}
	//FOR_EACH_END
}

static void fsmEditorAutoArrange(UIAnyWidget *pWidget, UserData userData)
{
	FSMEditorDoc *doc = (FSMEditorDoc*)userData;
	if(doc)
	{
		fsmEditorReflow(&doc->workingFSM);
	}
}

static void fsmEditorSelectAll(UIAnyWidget *pWidget, UserData userData)
{
	FSMEditorDoc *doc = (FSMEditorDoc*)userData;
	if(doc)
	{
		FSMEditorMachine *pMachine = &doc->workingFSM;

		eaClear(&doc->ppSelectedGraphNodes);
		
		FOR_EACH_IN_EARRAY(pMachine->states, FSMEditorState, pState)
		{
			ui_GraphNodeSetSelected(pState->stateWin, true);

			eaPush(&doc->ppSelectedGraphNodes, pState->stateWin);
		}
		FOR_EACH_END

		fsmEditorUpdateCableColors(doc);
	}
}

static void fsmEditorClearSelectionEx(UIAnyWidget *pWidget, UserData userData, bool bClearSearch)
{
	FSMEditorDoc *doc = (FSMEditorDoc*)userData;
	if(doc)
	{
		FSMEditorMachine *pMachine = &doc->workingFSM;

		FOR_EACH_IN_EARRAY(pMachine->states, FSMEditorState, pState)
		{
			if(bClearSearch)
				fsmEditorClearSearch(pState);

			ui_GraphNodeSetSelected(pState->stateWin, false);
		}
		FOR_EACH_END

		eaClear(&doc->ppSelectedGraphNodes);

		fsmEditorUpdateCableColors(doc);
	}
}

static void fsmEditorClearSelection(UIAnyWidget *pWidget, UserData userData)
{
	fsmEditorClearSelectionEx(pWidget, userData, true);
}

static void fsmEditorCopy(UIAnyWidget *pWidget, UserData userData)
{
	//FSMEditorDoc *doc = (FSMEditorDoc*)userData;
	FSMEditorDoc *doc = (FSMEditorDoc*)userData;
	if(doc)
	{
		//FSMEditorMachine *pMachine = &doc->workingFSM;
		//FOR_EACH_IN_EARRAY(pMachine->states, FSMEditorState, pState)
		//{
		//	fsmEditorStateExpandAll(NULL, pState->stateWin);
		//}
		//FOR_EACH_END
	}
}

static bool fsmEditorFindString(const char *pchHaystack, const char *pchNeedle)
{
	return strstri(pchHaystack, pchNeedle) != NULL;
}

static void fsmEditorClearSearch(FSMEditorState *pState)
{
	UISkin *pDefaultSkin = NULL;
	
	FOR_EACH_IN_EARRAY(pState->onEntryFExpr->lines, FSMEditorExprLine, pEditorExprLine)
	{
		ui_WidgetSkin((UIWidget*)pEditorExprLine->button, pDefaultSkin);
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pState->onEntryExpr->lines, FSMEditorExprLine, pEditorExprLine)
	{
		ui_WidgetSkin((UIWidget*)pEditorExprLine->button, pDefaultSkin);
	}
	FOR_EACH_END;
	
	// actions (or subFSM)
	if(GET_REF(pState->subFSM))
	{
		
	}
	else
	{
		FOR_EACH_IN_EARRAY(pState->actionExpr->lines, FSMEditorExprLine, pEditorExprLine)
		{
			ui_WidgetSkin((UIWidget*)pEditorExprLine->button, pDefaultSkin);
		}
		FOR_EACH_END;
	}

	// transitions
	FOR_EACH_IN_EARRAY(pState->connsOut, FSMEditorTransButton, pEditorTransButton)
	{
		FOR_EACH_IN_EARRAY(pEditorTransButton->condExpr->lines, FSMEditorExprLine, pEditorExprLine)
		{
			ui_WidgetSkin((UIWidget*)pEditorExprLine->button, pDefaultSkin);
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY(pEditorTransButton->actionExpr->lines, FSMEditorExprLine, pEditorExprLine)
		{
			ui_WidgetSkin((UIWidget*)pEditorExprLine->button, pDefaultSkin);
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;
}

static void fsmEditorFindStringInState(FSMEditorState *pState, const char *pchSearch)
{
	UISkin *pFoundSkin = pState->machine->doc->fsmEditorUI.greenSkin;

	FOR_EACH_IN_EARRAY(pState->onEntryFExpr->lines, FSMEditorExprLine, pEditorExprLine)
	{
		if(fsmEditorFindString(pEditorExprLine->line->origStr, pchSearch))
		{
			// mark and expand field
			ui_ExpanderSetOpened(pState->onEntryFExpr->expander, true);
			ui_WidgetSkin((UIWidget*)pEditorExprLine->button, pFoundSkin);
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pState->onEntryExpr->lines, FSMEditorExprLine, pEditorExprLine)
	{
		if(fsmEditorFindString(pEditorExprLine->line->origStr, pchSearch))
		{
			// mark and expand field
			ui_ExpanderSetOpened(pState->onEntryExpr->expander, true);
			ui_WidgetSkin((UIWidget*)pEditorExprLine->button, pFoundSkin);
		}
	}
	FOR_EACH_END;

	if(GET_REF(pState->subFSM))
	{

	}
	else
	{
		FOR_EACH_IN_EARRAY(pState->actionExpr->lines, FSMEditorExprLine, pEditorExprLine)
		{
			if(fsmEditorFindString(pEditorExprLine->line->origStr, pchSearch))
			{
				// mark and expand field
				ui_ExpanderSetOpened(pState->actionExpr->expander, true);
				ui_WidgetSkin((UIWidget*)pEditorExprLine->button, pFoundSkin);
			}
		}
		FOR_EACH_END;
	}


	FOR_EACH_IN_EARRAY(pState->connsOut, FSMEditorTransButton, pEditorTransButton)
	{
		FOR_EACH_IN_EARRAY(pEditorTransButton->condExpr->lines, FSMEditorExprLine, pEditorExprLine)
		{
			if(fsmEditorFindString(pEditorExprLine->line->origStr, pchSearch))
			{
				// mark and expand field
				ui_ExpanderSetOpened(pEditorTransButton->expander, true);
				ui_WidgetSkin((UIWidget*)pEditorExprLine->button, pFoundSkin);
			}
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY(pEditorTransButton->actionExpr->lines, FSMEditorExprLine, pEditorExprLine)
		{
			if(fsmEditorFindString(pEditorExprLine->line->origStr, pchSearch))
			{
				// mark and expand field
				ui_ExpanderSetOpened(pEditorTransButton->expander, true);
				ui_WidgetSkin((UIWidget*)pEditorExprLine->button, pFoundSkin);
			}
		}
		FOR_EACH_END;

	}
	FOR_EACH_END;
}

static void fsmEditorFindStringInDoc(FSMEditorDoc *doc, const char *pchSearch)
{
	FSMEditorMachine *pMachine;

	if(!doc)
		return;

	pMachine = &doc->workingFSM;
	
	FOR_EACH_IN_EARRAY(pMachine->states, FSMEditorState, pState)
	{
		fsmEditorClearSearch(pState);
		fsmEditorFindStringInState(pState, pchSearch);
	}
	FOR_EACH_END;
}

static void fsmEditorCollapseSelected(UIAnyWidget *pWidget, UserData userData)
{
	FSMEditorDoc *doc = (FSMEditorDoc*)userData;
	if(doc)
	{
		FOR_EACH_IN_EARRAY(doc->ppSelectedGraphNodes, UIGraphNode, pGraphNode)
		{
			fsmEditorStateCollapseAll(NULL, pGraphNode->userData);
		}
		FOR_EACH_END
	}
}

static void fsmEditorExpandSelected(UIAnyWidget *pWidget, UserData userData)
{
	FSMEditorDoc *doc = (FSMEditorDoc*)userData;
	if(doc)
	{
		FOR_EACH_IN_EARRAY(doc->ppSelectedGraphNodes, UIGraphNode, pGraphNode)
		{
			fsmEditorStateExpandAll(NULL, pGraphNode->userData);
		}
		FOR_EACH_END
	}
}

static void fsmEditorBuildContextMenu(FSMEditorDoc *doc)
{
	int iNumSelected;

	iNumSelected = eaSize(&doc->ppSelectedGraphNodes);

	if (!doc->pRightClickMenu)
		doc->pRightClickMenu = ui_MenuCreate("");

	eaDestroyEx(&doc->pRightClickMenu->items, ui_MenuItemFree);


	ui_MenuAppendItems(doc->pRightClickMenu, NULL);

	ui_MenuAppendItems(doc->pRightClickMenu,
		ui_MenuItemCreate("Add State", UIMenuCommand, NULL, "FSMEditor.AddState", NULL),
		ui_MenuItemCreate("Find", UIMenuCommand, NULL, "FSMEditor.Find", NULL),
		NULL);

	if(iNumSelected == 1)
	{
		UIGraphNode *pGraphNode = doc->ppSelectedGraphNodes[0];
		FSMEditorState *pEditorState = (FSMEditorState*)pGraphNode->userData;

		ui_MenuAppendItems(doc->pRightClickMenu,
			ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL),
			ui_MenuItemCreate("Rename...", UIMenuCallback, fsmEditorStateRename, pEditorState, NULL),
			ui_MenuItemCreate("Set to start", UIMenuCallback, fsmEditorStateSetStart, pEditorState, NULL),
			ui_MenuItemCreate("Set Sub-FSM...", UIMenuCallback, fsmEditorSelectSubFSM, pEditorState, NULL),
			ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL),
			ui_MenuItemCreate("Copy State", UIMenuCallback, fsmEditorStateCopyToClipboard, pEditorState, NULL),
			ui_MenuItemCreate("Delete State", UIMenuCallback, fsmEditorStateDelete, pEditorState, NULL),
			NULL);

	
		if(pEditorState && !pEditorState->bIsCommentVisible)
		{
			ui_MenuAppendItems(doc->pRightClickMenu,
				ui_MenuItemCreate("Add Comment", UIMenuCallback, fsmEditorStateAddComment, pEditorState, NULL), 
					NULL);
		}
	}
	else if(iNumSelected > 1)
	{
		ui_MenuAppendItems(doc->pRightClickMenu,
			ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL),
			ui_MenuItemCreate("Copy States", UIMenuCallback, fsmEditorCopySelectionToClipboard, doc, NULL),
			ui_MenuItemCreate("Delete States", UIMenuCallback, fsmEditorDeleteSelection, doc, NULL),
			NULL);
	}

	if(iNumSelected > 0)
	{
		ui_MenuAppendItems(doc->pRightClickMenu,
			ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL),
			ui_MenuItemCreate("Expand (X)", UIMenuCallback, fsmEditorExpandSelected, doc, NULL),
			ui_MenuItemCreate("Collapse (C)", UIMenuCallback, fsmEditorCollapseSelected, doc, NULL),
			ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL),
			NULL);
	}
	else
	{
		ui_MenuAppendItems(doc->pRightClickMenu,
			ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL),
			NULL);
	}

	ui_MenuAppendItems(doc->pRightClickMenu,

		ui_MenuItemCreate("Auto-Arrange", UIMenuCallback, fsmEditorAutoArrange, doc, NULL),
		ui_MenuItemCreate("Select All", UIMenuCommand, NULL, "FSMEditor.SelectAll", NULL),	
		ui_MenuItemCreate("Clear Selection", UIMenuCallback, fsmEditorClearSelection, doc, NULL),	
		NULL);

	ui_MenuPopupAtCursor(doc->pRightClickMenu);	
}

static void fsmEditorOnContextOnNode(UIAnyWidget *pWidget, void *userData)
{
	UIGraphNode *pGraphNode = (UIGraphNode*)userData;

	if(!pGraphNode)
		return;

	{
		FSMEditorState *pEditorState = (FSMEditorState*)pGraphNode->userData;
		FSMEditorDoc *pDoc = pEditorState->machine->doc;

		// if the graph node is not in our selection, replace the selection (before building the menu)
		if(eaFind(&pDoc->ppSelectedGraphNodes, pGraphNode) < 0)
		{
			fsmEditorClearSelectionEx(NULL, pDoc, false);

			ui_GraphNodeSetSelected(pGraphNode, true);
			eaPush(&pDoc->ppSelectedGraphNodes, pGraphNode);
		}
		fsmEditorBuildContextMenu(pDoc);
	}
}

static void fsmEditorOnContext(UIAnyWidget *pWidget, void *userData)
{
	FSMEditorDoc *doc = (FSMEditorDoc*)userData;
	
	if(!doc)
		return;

	fsmEditorBuildContextMenu(doc);
}

static void fsmEditorOnMouseUp(UIGraphNode *pGraphNode, Vec2 clickPoint, void *userData)
{
	FSMEditorState *state;
	FSMEditorDoc *doc;

	assert(pGraphNode);

	state = (FSMEditorState*)userData;
	doc = SAFE_MEMBER2(state, machine, doc);

	if(doc)
	{
		if(	doc->mouseDownPoint[0] == clickPoint[0] && 
			doc->mouseDownPoint[1] == clickPoint[1])
		{
			bool bIsControlKeyDown = inpLevelPeek(INP_CONTROL);

			if(!bIsControlKeyDown)
			{
				FOR_EACH_IN_EARRAY(doc->ppSelectedGraphNodes, UIGraphNode, pNode)
				{
					ui_GraphNodeSetSelected(pNode, false);
				}
				FOR_EACH_END

				eaClear(&doc->ppSelectedGraphNodes);

				eaPush(&doc->ppSelectedGraphNodes, pGraphNode);
				ui_GraphNodeSetSelected(pGraphNode, true);

				fsmEditorUpdateCableColors(doc);
			}
		}
	}
}

static void fsmEditorOnMouseDrag(UIGraphNode *pGraphNode, Vec2 clickPoint, void *userData)
{
	FSMEditorState *state;
	FSMEditorDoc *doc;

	assert(pGraphNode);

	state = (FSMEditorState*)userData;
	doc = SAFE_MEMBER2(state, machine, doc);

	if(doc)
	{
		Vec2 delta;

		subVec2(clickPoint, doc->lastMousePoint, delta);

		// scale
		delta[0] /= pGraphNode->pScale;
		delta[1] /= pGraphNode->pScale;

		//printf("drag <%.2f, %.2f>   delta <%.2f, %.f>\n", clickPoint[0], clickPoint[1], delta[0], delta[1]);

		if(pGraphNode->bIsMiddleMouseDown)
		{
			UIWindow *pStateMachineWindow = doc->fsmEditorUI.stateMachine.window;
			UIScrollArea *area = doc->fsmEditorUI.stateMachine.area;
			UIScrollbar *sb = area->widget.sb;
			
			sb->xpos += delta[0];
			sb->ypos += delta[1];
		}
		else
		{
			bool bMoveX = delta[0] != 0.0;
			bool bMoveY = delta[1] != 0.0;

			if(bMoveX || bMoveY)
			{
				// test all first
				FOR_EACH_IN_EARRAY(doc->ppSelectedGraphNodes, UIGraphNode, pNode)
				{
					if(pNode->window.widget.x + delta[0] < 0.)
					{
						bMoveX = false;
					}
					
					if(pNode->window.widget.y + delta[1] < pNode->topPad)
					{
						bMoveY = false;
					}
				}
				FOR_EACH_END

				// apply movement
				FOR_EACH_IN_EARRAY(doc->ppSelectedGraphNodes, UIGraphNode, pNode)
				{
					if(bMoveX)
					{
						pNode->window.widget.x += delta[0];
					}

					if(bMoveY)
					{
						pNode->window.widget.y += delta[1];
					}
				}
				FOR_EACH_END
			}
		}
		
		// remember to save last point
		copyVec2(clickPoint, doc->lastMousePoint);
	}
}

static void fsmEditorUpdateCableColorsForState(FSMEditorState *pState, Color inColor, Color outColor, F32 lineScale)
{
	// inputs
	FOR_EACH_IN_EARRAY(pState->connsIn, FSMEditorTransButton, pButton)
	{
		if(pButton->pair && pButton->pair->pBox)
		{
			pButton->pair->pBox->color = inColor;
			if(pButton->pair->pBox->line)
			{
				pButton->pair->pBox->line->lineScale = lineScale;
			}
		}
	}
	FOR_EACH_END

	// outputs
	FOR_EACH_IN_EARRAY(pState->connsOut, FSMEditorTransButton, pButton)
	{
		if(pButton->pBox)
		{
			pButton->pBox->color = outColor;
		}

		if(pButton->pair && pButton->pair->pBox && pButton->pair->pBox->line)
		{
			pButton->pair->pBox->line->lineScale = lineScale;
		}
	}
	FOR_EACH_END
}

static void fsmEditorUpdateCableColors(FSMEditorDoc *doc)
{
	static Color blueColor = { 0x00, 0x66, 0xcc, 0xFF };
	static Color orangeColor = { 0xcc, 0x66, 0x00, 0xFF };
	static Color grayColor = { 0x80, 0x80, 0x80, 0xAA };

	// update colors
	FSMEditorMachine *pMachine = &doc->workingFSM;

	// reset colors
	FOR_EACH_IN_EARRAY(pMachine->states, FSMEditorState, pState)
	{
		fsmEditorUpdateCableColorsForState(pState, grayColor, grayColor, 2.0);
	}
	FOR_EACH_END

	// set selected colors
	FOR_EACH_IN_EARRAY(doc->ppSelectedGraphNodes, UIGraphNode, pNode)
	{
		FSMEditorState *pState = (FSMEditorState*)pNode->userData;
		if(pState)
		{
			fsmEditorUpdateCableColorsForState(pState, orangeColor, blueColor, 4.0);
		}
	}
	FOR_EACH_END
}

static void fsmEditorOnMouseDown(UIGraphNode *pGraphNode, Vec2 clickPoint, void *userData)
{
	FSMEditorState *state;
	FSMEditorDoc *doc;

	assert(pGraphNode);

	state = (FSMEditorState*)userData;
	doc = SAFE_MEMBER2(state, machine, doc);

	if(doc)
	{
		if(pGraphNode->bLeftMouseDown) // only left-click
		{
			bool bIsInSelection = eaFind(&doc->ppSelectedGraphNodes, pGraphNode) >= 0;
			bool bIsControlKeyDown = inpLevelPeek(INP_CONTROL);

			if(!bIsControlKeyDown && !bIsInSelection)
			{
				// de-select old
				FOR_EACH_IN_EARRAY(doc->ppSelectedGraphNodes, UIGraphNode, pNode)
				{
					ui_GraphNodeSetSelected(pNode, false);
				}
				FOR_EACH_END

				eaClear(&doc->ppSelectedGraphNodes);
			} 
			
			if(bIsControlKeyDown && bIsInSelection)
			{
				// de-select individual
				ui_GraphNodeSetSelected(pGraphNode, false);
				eaFindAndRemove(&doc->ppSelectedGraphNodes, pGraphNode);
			}
			else
			{
				eaPushUnique(&doc->ppSelectedGraphNodes, pGraphNode);
				ui_GraphNodeSetSelected(pGraphNode, true);
			}

			fsmEditorUpdateCableColors(doc);
		}

		copyVec2(clickPoint, doc->lastMousePoint);
		copyVec2(clickPoint, doc->mouseDownPoint);
	}
}

bool fsmEditorOnScrollAreaInput(UIScrollArea *pScrollArea, KeyInput *key)
{
	FSMEditorDoc *pDoc = (FSMEditorDoc*)pScrollArea->widget.userinfo;

	if(key->scancode == INP_X)
	{
		fsmEditorExpandSelected(NULL, pDoc);
		return true;
	}
	else if(key->scancode == INP_C)
	{
		fsmEditorCollapseSelected(NULL, pDoc);
		return true;
	}

	return false;
}

bool fsmEditorOnGraphNodeInput(UIGraphNode *pGraphNode, KeyInput *key)
{
	FSMEditorState *pState = (FSMEditorState*)pGraphNode->onInputUserData;
	FSMEditorDoc *pDoc = pState->machine->doc;

	if(key->scancode == INP_X)
	{
		fsmEditorExpandSelected(NULL, pDoc);
		return true;
	}
	else if(key->scancode == INP_C)
	{
		fsmEditorCollapseSelected(NULL, pDoc);
		return true;
	}

	return false;
}

/******
* This function adds a new FSMEditorState to an FSM.
* PARAMS:
*   activeDoc - the FSMEditorDoc where the new state will reside
* RETURNS:
*   FSMEditorState - the FSMEditorState just created
******/
FSMEditorState *fsmEditorAddState(FSMEditorDoc *activeDoc)
{
	UIGraphNode *stateWin;
	UIButton *in, *out, *button;
	//UIMenuBar *menuBar;
	//UIMenu *menu;
	UIExpander *exp;
	UIExpanderGroup *expGrp;
	UIScrollArea *area;
	UITextArea *textArea;
	FSMEditorState *state = NULL;
	int stateX, stateY;
	int w, h;
	char stateName[FSME_STATE_NAME_MAX_LEN];
	F32 y_offset = 0;

	// create the state's starting UI components
	fsmEditorUniqueStateName(activeDoc, SAFESTR(stateName));
	stateWin = ui_GraphNodeCreate(stateName, 0, 0, 0, 0, activeDoc->fsmEditorUI.selectedNodeSkin, activeDoc->fsmEditorUI.unselectedNodeSkin);
	

	ui_WidgetSetDimensions((UIWidget*) &stateWin->window, FSME_STATE_WIDTH, 105);
	ui_WindowSetClosable(&stateWin->window, false);
	ui_WindowSetResizable(&stateWin->window, false);

	textArea = ui_TextAreaCreate("");
	ui_TextAreaSetCollapse(textArea, true);
	textArea->widget.y = -25;
	ui_WidgetSetWidthEx(UI_WIDGET(textArea), 1, UIUnitPercentage);
	ui_WidgetSetHeight(UI_WIDGET(textArea), 100);
	ui_WindowAddChild(&stateWin->window, textArea);

	
	//y_offset += 25;

	in = ui_ButtonCreate("->In", 0, 0, NULL, NULL);
	out = ui_ButtonCreate("Out", 0, 0, NULL, NULL);
	expGrp = ui_ExpanderGroupCreate();

	state = fsmEditorStateCreate(stateWin, in, out, expGrp);
	//stateWin->window.widget.tickF = fsmEditorStateWinTick;
	stateWin->window.widget.dragData = state;

	ui_WidgetSetUnfocusCallback(&stateWin->window.widget, fsmEditorOnGraphNodeLostFocus, state); 
	ui_GraphNodeSetOnInputCallback(stateWin, fsmEditorOnGraphNodeInput, state);
	ui_GraphNodeSetOnMouseDownCallback(stateWin, fsmEditorOnMouseDown, state);
	ui_GraphNodeSetOnMouseDragCallback(stateWin, fsmEditorOnMouseDrag, state);
	ui_GraphNodeSetOnMouseUpCallback(stateWin, fsmEditorOnMouseUp, state);
	ui_WidgetSetContextCallback(&stateWin->window.widget, fsmEditorOnContextOnNode, stateWin);
	stateWin->userData = state;
	ui_WidgetSkin((UIWidget*)&stateWin->window.widget, activeDoc->fsmEditorUI.unselectedNodeSkin);

	// background window
	ui_WidgetSetContextCallback(&activeDoc->fsmEditorUI.stateMachine.window->widget, fsmEditorOnContext, activeDoc);
	//	fsmEditorOnScrollAreaContext, activeDoc);


	state->comment = textArea;
	ui_WidgetSetFocusCallback(UI_WIDGET(textArea), fsmEditorStateCommentGotFocus, state); 
	ui_WidgetSetUnfocusCallback(UI_WIDGET(textArea), fsmEditorStateCommentLostFocus, state); 
	ui_TextAreaSetChangedCallback(textArea, fsmEditorStateCommentChanged, state);

	ui_WidgetSetDimensions((UIWidget*) in, in->widget.width, FSME_BUTTON_SIZE);
	ui_WidgetSetPositionEx((UIWidget*) in, 5, 5, 0, 0, UIBottomLeft);
	ui_WidgetSetDragCallback((UIWidget*) in, fsmEditorConnectDrag, state);
	ui_WidgetSetDropCallback((UIWidget*) in, fsmEditorConnectDrop, state);
	ui_WindowAddChild(&stateWin->window, in);
	
	ui_WidgetSetDimensions((UIWidget*) out, out->widget.width, FSME_BUTTON_SIZE);
	ui_WidgetSetPositionEx((UIWidget*) out, 5, 5, 0, 0, UIBottomRight);
	ui_WidgetSetDragCallback((UIWidget*) out, fsmEditorConnectDrag, state);
	ui_WidgetSetDropCallback((UIWidget*) out, fsmEditorConnectDrop, state);
	ui_WindowAddChild(&stateWin->window, out);
	
	ui_WidgetSetPositionEx((UIWidget*) expGrp, 10 + FSME_BUTTON_SIZE / 2, 5, 0, 0, UIBottomRight);
	ui_WidgetSetDimensionsEx((UIWidget*) expGrp, FSME_TRANS_EXP_WIDTH, expGrp->widget.height, UIUnitFixed, UIUnitFixed);
	ui_ExpanderGroupSetGrow(expGrp, true);
	ui_WindowAddChild(&stateWin->window, expGrp);
	
	ui_ScrollAreaAddChild(activeDoc->fsmEditorUI.stateMachine.area, (UIWidget*) &stateWin->window);

	// create initial state expander group
	expGrp = ui_ExpanderGroupCreate();
	ui_WidgetSetDimensionsEx((UIWidget*) expGrp, 1, expGrp->widget.height, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPosition((UIWidget*) expGrp, 0, y_offset);
	ui_ExpanderGroupSetGrow(expGrp, true);
	ui_WindowAddChild(&stateWin->window, expGrp);
	state->stateExpGrp = expGrp;

	// OnEntryFirst expanders
	exp = ui_ExpanderCreate("OnEntryFirst", FSME_STATE_EXP_HEIGHT);
	ui_ExpanderGroupAddExpander(expGrp, exp);
	ui_ExpanderSetExpandCallback(exp, fsmEditorStateExpandCallback, state);
	fsmEditorCreateExprUI(5, 5, state->onEntryFExpr, exp, "OnEntryFirst");

	// OnEntry expanders
	exp = ui_ExpanderCreate("OnEntry", FSME_STATE_EXP_HEIGHT);
	ui_ExpanderGroupAddExpander(expGrp, exp);
	ui_ExpanderSetExpandCallback(exp, fsmEditorStateExpandCallback, state);
	fsmEditorCreateExprUI(5, 5, state->onEntryExpr, exp, "OnEntry");

	// Sub-StateMachine expander
	exp = ui_ExpanderCreate("Sub-StateMachine", 25);
	ui_ExpanderSetExpandCallback(exp, fsmEditorStateExpandCallback, state);
	button = ui_ButtonCreateImageOnly("button_close", 5, 5, fsmEditorSubFSMClear, state);
	ui_WidgetSetDimensions((UIWidget*) button, FSME_BUTTON_SIZE, FSME_BUTTON_SIZE);
	ui_ButtonSetImageStretch( button, true );
	ui_ExpanderAddChild(exp, button);
	button = ui_ButtonCreate("...", elUINextX(button), button->widget.y, fsmEditorSelectSubFSM, state);
	button->widget.height = FSME_BUTTON_SIZE;
	ui_ExpanderAddChild(exp, button);
	button = ui_ButtonCreate("", elUINextX(button), button->widget.y, fsmEditorSubFSMOpen, state);
	button->widget.height = FSME_BUTTON_SIZE;
	ui_ExpanderAddChild(exp, button);
	state->subFSMButton = button;
	state->subFSMExp = exp;

	// Action expanders
	exp = ui_ExpanderCreate("Action", FSME_STATE_EXP_HEIGHT);
	ui_ExpanderGroupAddExpander(expGrp, exp);
	ui_ExpanderSetExpandCallback(exp, fsmEditorStateExpandCallback, state);
	fsmEditorCreateExprUI(5, 5, state->actionExpr, exp, "Action");

	// menus
	//menuBar = ui_MenuBarCreate(NULL);
	//menu = ui_MenuCreateWithItems("State",
	//	ui_MenuItemCreate("Rename...", UIMenuCallback, fsmEditorStateRename, state, NULL),
	//	ui_MenuItemCreate("Set to start", UIMenuCallback, fsmEditorStateSetStart, state, NULL),
	//	ui_MenuItemCreate("Set Sub-FSM...", UIMenuCallback, fsmEditorSelectSubFSM, state, NULL),
	//	ui_MenuItemCreate("Copy to clipboard", UIMenuCallback, fsmEditorStateCopyToClipboard, state, NULL),
	//	ui_MenuItemCreate("Delete", UIMenuCallback, fsmEditorStateDelete, state, NULL),
	//	NULL);
	//ui_MenuBarAppendMenu(menuBar, menu);
	//menu = ui_MenuCreateWithItems("FSM",
	//	ui_MenuItemCreate("Add State", UIMenuCommand, NULL, "FSMEditor.AddState", NULL),
	//	NULL);
	//ui_MenuBarAppendMenu(menuBar, menu);
	//menu = ui_MenuCreateWithItems("View",
	//	ui_MenuItemCreate("Expand State", UIMenuCallback, fsmEditorStateExpandState, state, NULL),
	//	ui_MenuItemCreate("Expand Transitions", UIMenuCallback, fsmEditorStateExpandTrans, state, NULL),
	//	ui_MenuItemCreate("Expand All", UIMenuCallback, fsmEditorStateExpandAll, state, NULL),
	//	ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL),
	//	ui_MenuItemCreate("Collapse State", UIMenuCallback, fsmEditorStateCollapseState, state, NULL),
	//	ui_MenuItemCreate("Collapse Transitions", UIMenuCallback, fsmEditorStateCollapseTrans, state, NULL),
	//	ui_MenuItemCreate("Collapse All", UIMenuCallback, fsmEditorStateCollapseAll, state, NULL),
	//	NULL);
	//ui_MenuBarAppendMenu(menuBar, menu);
	//ui_WindowAddChild(&stateWin->window, menuBar);

	// add state to the machine
	if (eaSize(&activeDoc->workingFSM.states) == 0)
		activeDoc->workingFSM.startState = state;
	eaPush(&activeDoc->workingFSM.states, state);
	state->machine = &activeDoc->workingFSM;
	fsmEditorRefreshState(state);

	area = activeDoc->fsmEditorUI.stateMachine.area;
	w = ui_WidgetWidth(UI_WIDGET(area), activeDoc->fsmEditorUI.stateMachine.window->widget.width, 1) - ui_ScrollbarWidth(area->widget.sb);
	h = ui_WidgetHeight(UI_WIDGET(area), activeDoc->fsmEditorUI.stateMachine.window->widget.height, 1) - ui_ScrollbarHeight(area->widget.sb);
	stateX = area->widget.sb->xpos + (w / 2)
		- (stateWin->window.widget.width / 2);
	stateY = area->widget.sb->ypos + (h / 2)
		- (stateWin->window.widget.height / 2);
	ui_WidgetSetPosition((UIWidget*) &stateWin->window, stateX / area->childScale, stateY / area->childScale);
	return state;
}

/******
* This function connects an existing transition to a destination state.  This function does NOT handle
* disconnecting an existing connection.
* PARAMS:
*   outButton - FSMEditorTransButton for the transition to connect
*   newDest - FSMEditorState destination to which outButton will connect
******/
static void fsmEditorReconnectStates(FSMEditorTransButton *outButton, FSMEditorState *newDest)
{
	// create connection for destination state
	FSMEditorTransButton *destTransButton;
	UIPairedBox *endBox = ui_PairedBoxCreate(ColorGray);
	ui_PairedBoxConnect(outButton->pBox, endBox, newDest->machine->doc->fsmEditorUI.stateMachine.area);
	ui_WindowAddChild(&newDest->stateWin->window, endBox);
	destTransButton = fsmEditorTransButtonCreate(endBox);
	destTransButton->state = newDest;
	ui_WidgetSetPositionEx((UIWidget*) endBox, newDest->in->widget.x, newDest->in->widget.y + newDest->in->widget.height / 2, 0, 0, UIBottomLeft);
	ui_WidgetSetDimensions((UIWidget*) endBox, 0, 0);
	eaPush(&newDest->connsIn, destTransButton);

	destTransButton->pair = outButton;
	outButton->pair = destTransButton;
	fsmEditorRefreshTransUI(outButton);
}

/******
* This function connects two states and deals with the UI appropriately.
* PARAMS:
*   startState - the transition's beginning FSMEditorState
*   endState - the transition's end FSMEditorState
* RETURNS:
*   FSMEditorTransButton corresponding to the new outgoing connection from startState
******/
static FSMEditorTransButton *fsmEditorConnectStates(FSMEditorState *startState, FSMEditorState *endState)
{
	FSMEditorDoc *activeDoc = startState->machine->doc;
	FSMEditorTransButton *startTransButton;
	FSMEditorExpr *condExpr, *actionExpr;
	UIWindow *win;
	UIExpander *exp;
	UIPairedBox *startBox;
	UIButton *closeButton, *dragButton;
	UIExpanderGroup *expGrp;

	// add UI for new start state expander
	condExpr = fsmEditorExprCreate(startState, NULL);
	actionExpr = fsmEditorExprCreate(startState, NULL);
	win = &startState->stateWin->window;
	expGrp = startState->connsExpGrp;
	exp = ui_ExpanderCreate("", FSME_TRANS_EXP_HEIGHT);
	fsmEditorMoveButtonsY(startState, exp->widget.height);
	ui_ExpanderSetExpandCallback(exp, fsmEditorTransExpandCallback, startState);
	ui_ExpanderGroupAddExpander(expGrp, exp);
	condExpr->belowExpr = actionExpr;
	fsmEditorCreateExprUI(5, 30, actionExpr, exp, "");
	fsmEditorCreateExprUI(5, 5, condExpr, exp, "");
	ui_WidgetSkin((UIWidget*) condExpr->editAll, activeDoc->fsmEditorUI.redSkin);
	ui_ButtonSetText(condExpr->editAll, "C:");
	ui_WidgetSkin((UIWidget*) actionExpr->editAll, activeDoc->fsmEditorUI.blueSkin);
	ui_ButtonSetText(actionExpr->editAll, "A:");

	// add UI for stuff outside of the expander (like the close button and paired box)
	startBox = ui_PairedBoxCreate(ColorGray);
	ui_WidgetSetDimensions((UIWidget*) startBox, 0, 0);
	ui_WidgetSetPositionEx((UIWidget*) startBox, 5, 11, 0, 0, UITopRight);
	ui_ExpanderAddLabel(exp, (UIWidget*) startBox);
	closeButton = ui_ButtonCreateImageOnly("button_close", 0, 0, NULL, NULL);
	ui_WidgetSetDimensions((UIWidget*) closeButton, 11, 11);
	ui_ButtonSetImageStretch( closeButton, true );
	ui_WidgetSetPositionEx((UIWidget*) closeButton, 25, 5, 0, 0, UITopRight);
	ui_ExpanderAddLabel(exp, (UIWidget*) closeButton);
	dragButton = ui_ButtonCreate(NULL, 0, 0, NULL, NULL);
	ui_WidgetSetDimensions((UIWidget*) dragButton, 11, 11);
	ui_WidgetSetPositionEx((UIWidget*) dragButton, 5, 5, 0, 0, UITopRight);
	ui_ExpanderAddLabel(exp, (UIWidget*) dragButton);
	startTransButton = fsmEditorTransButtonCreateEx(startBox, condExpr, actionExpr, exp);
	startTransButton->state = startState;
	startTransButton->pair = NULL;
	ui_ButtonSetCallback(closeButton, fsmEditorOutTransDelete, startTransButton);
	ui_WidgetSetDragCallback((UIWidget*) dragButton, fsmEditorTransButtonDrag, startTransButton);
	ui_WidgetSetDropCallback((UIWidget*) dragButton, fsmEditorTransButtonDrop, startTransButton);
	ui_WidgetSetDragCallback((UIWidget*) exp, fsmEditorTransDrag, startTransButton);
	ui_WidgetSetDropCallback((UIWidget*) exp, fsmEditorTransDrop, startTransButton);
	eaPush(&startState->connsOut, startTransButton);
	ui_WidgetSetDimensions((UIWidget*) win, win->widget.width, win->widget.height + exp->widget.height);

	// pair the states
	if (endState)
		fsmEditorReconnectStates(startTransButton, endState);
	fsmEditorRefreshTransUI(startTransButton);

	return startTransButton;
}

/******
* This function sets the name of a state.
* PARAMS:
*   state - FSMEditorState to rename
*   name - the new name to give
******/
static void fsmEditorStateSetName(FSMEditorState *state, const char *name)
{
	if (fsmEditorGetStateByName(state->machine->doc, name))
	{
		emStatusPrintf("A state named \"%s\" already exists!", name);
		return;
	}
	ui_WindowSetTitle(&state->stateWin->window, name);
	fsmEditorRefreshState(state);
}

/******
* This function renames the entire FSM.
* PARAMS:
*   activeDoc - FSMEditorDoc containing the FSM to rename
*   name - the new name to give
******/
static void fsmEditorFSMSetName(FSMEditorDoc *activeDoc, const char *name)
{
	strcpy(activeDoc->workingFSM.name, name);
	strcpy(activeDoc->doc.doc_display_name, name);
	strcpy(activeDoc->doc.doc_name, name);
	activeDoc->doc.name_changed = 1;
	ui_WindowSetTitle(activeDoc->fsmEditorUI.stateMachine.window, name);
}

static void fsmEditorFSMSetScope(FSMEditorDoc *activeDoc, const char *scope)
{
	activeDoc->workingFSM.scope[0] = '\0';

	if (scope) 
	{
		strcat(activeDoc->workingFSM.scope, scope);
	}
}

/******
* This function populates a stash table of all states that are connected to a specified state.
* PARAMS:
*   state - the starting FSMEditorState from which to check connections
*   connedStates - the StashTable where the connected states will be stored for lookup
******/
static void fsmEditorGetConnectedStates(FSMEditorState *state, StashTable connedStates)
{
	int i;
	const char *name = fsmEditorGetStateName(state);
	FSMEditorState *statePtr;

	// make sure state is not already in the stash
	if (stashFindPointer(connedStates, name, &statePtr))
		return;
	else
		stashAddPointer(connedStates, name, state, false);

	// recurse on connected states
	for (i = 0; i < eaSize(&state->connsOut); i++)
	{
		if (state->connsOut[i]->pair)
			fsmEditorGetConnectedStates(state->connsOut[i]->pair->state, connedStates);
	}
}

/******
* This function returns the FSMGroup to which a specified FSM belongs.
* PARAMS:
*   fsm - the FSM whose group is to be found
* RETURNS:
*   FSMGroup to which fsm belongs
******/
static FSMGroup *fsmEditorGetGroupForFSM(FSM *fsm)
{
	int i;
	for (i = 0; i < eaSize(&fsmEditorGroups.fsmGroups); i++)
	{
		if ((fsm->group && stricmp(fsm->group, fsmEditorGroups.fsmGroups[i]->name) == 0) ||
			(!fsm->group && strnicmp(fsmEditorGroups.fsmGroups[i]->dir, fsm->fileName, strlen(fsmEditorGroups.fsmGroups[i]->dir)) == 0))
		{
			return fsmEditorGroups.fsmGroups[i];
		}
	}
	return NULL;
}

/******
* This function validates the following and prints out messages when they fail:
* 1) All states are connected
* 2) Each transition has a conditional expression and a target
* 3) The name of the machine is valid and unique
* PARAMS:
*   fsm - FSMEditorMachine to validate
*   isNew - bool indicating whether the validation is for a new machine (i.e.
*           on save as or saving a new document)
******/
static bool fsmEditorFSMValidate(FSMEditorMachine *fsm)
{
	StashTable connedStates = stashTableCreateWithStringKeys(20, StashDefault);
	int i, j;
	void *temp;
	char **errors = NULL;

	// all states must be connected
	fsmEditorGetConnectedStates(fsm->startState, connedStates);
	for (i = 0; i < eaSize(&fsm->states); i++)
	{
		FSMEditorState *state = fsm->states[i];
		const char *name = fsmEditorGetStateName(state);
		if (!stashFindPointer(connedStates, name, &temp))
		{
			char *estr = NULL;
			estrPrintf(&estr, "%s should be connected.", name);
			eaPush(&errors, estr);
		}

		// transitions must have targets and conditions
		for (j = 0; j < eaSize(&state->connsOut); j++)
		{
			FSMEditorTransButton *trans = state->connsOut[j];
			if (!trans->pair)
			{
				char *estr = NULL;
				estrPrintf(&estr, "%s requires all transitions to be connected.", name);
				eaPush(&errors, estr);
			}
			else if (!trans->condExpr || eaSize(&trans->condExpr->lines) == 0)
			{
				char *estr = NULL;
				estrPrintf(&estr, "Transition from %s to %s requires a conditional expression.", name, fsmEditorGetStateName(trans->pair->state));
				eaPush(&errors, estr);
			}
		}
	}

	// machine name is valid and unique
	if (!resIsValidName(fsm->name))
	{
		char *estr = NULL;
		estrPrintf(&estr, "State machine name is not legal.");
		eaPush(&errors, estr);
	}

	if (!resIsValidScope(fsm->scope))
	{
		char *estr = NULL;
		estrPrintf(&estr, "State machine scope is not legal.");
		eaPush(&errors, estr);
	}

	// create the window of errors
	stashTableDestroy(connedStates);
	if (eaSize(&errors) > 0)
	{
		fsmEditorErrorWinCreate(errors);
		for (i = 0; i < eaSize(&errors); i++)
			estrDestroy(&errors[i]);
		eaDestroy(&errors);
		return false;
	}
	return true;
}

static void fsmEditorRefreshSubFsmUI(FSMEditorState *state)
{
	const char *fsmName = REF_STRING_FROM_HANDLE(state->subFSM);
	if (fsmName)
	{
		int height = state->stateExpGrp->totalHeight;

		// remove expanders
		ui_ExpanderGroupRemoveExpander(state->stateExpGrp, state->subFSMExp);
		ui_ExpanderGroupRemoveExpander(state->stateExpGrp, state->actionExpr->expander);

		// refresh the UI to show the sub FSM expander and hide the action expanders
		ui_ExpanderGroupAddExpander(state->stateExpGrp, state->subFSMExp);
		height -= state->stateExpGrp->totalHeight;
		ui_ExpanderSetOpened(state->subFSMExp, true);

		// shift UI elements as necessary
		ui_WidgetSetDimensions((UIWidget*) &state->stateWin->window, state->stateWin->window.widget.width, state->stateWin->window.widget.height - height);
		
		// set the expander button's text
		ui_ButtonSetTextAndResize(state->subFSMButton, fsmName);
		ui_WidgetSetDimensions((UIWidget*) state->subFSMButton, state->subFSMButton->widget.width, FSME_BUTTON_SIZE);
	}
}

static void fsmEditorRefreshSubFsms(FSMEditorDoc *activeDoc)
{
	int i, j;
	for(i=eaSize(&activeDoc->workingFSM.states)-1; i>=0; --i)
	{
		FSMEditorState *state = activeDoc->workingFSM.states[i];

		// Clear sub-FSM if it has been deleted or renamed
		if (eaFind(&state->stateExpGrp->childrenInOrder, (UIWidget*)state->subFSMExp) != -1)
		{
			const char *fsmName = REF_STRING_FROM_HANDLE(state->subFSM);
			bool found = false;
			if (fsmName)
			{
				ResourceDictionaryInfo *pDictInfo = resDictGetInfo("FSM");
				for(j=eaSize(&pDictInfo->ppInfos)-1; j>=0; --j) {
					if (stricmp(pDictInfo->ppInfos[j]->resourceName, fsmName) == 0) {
						found = true;
						break;
					}
				}
			}
			if (!found)
			{
				activeDoc->doc.saved = false;
				fsmEditorStateSetSubFSM(state, NULL);
			}
		}
	}
}

static void fsmEditorStateSetSubFSM(FSMEditorState *state, const char *subFSMName)
{
	REMOVE_HANDLE(state->subFSM);

	// if setting a sub FSM...
	if (subFSMName)
	{
		SET_HANDLE_FROM_REFDATA(gFSMDict, subFSMName, state->subFSM);
		fsmEditorRefreshSubFsmUI(state);
	}
	// if clearing a sub FSM...
	else
	{
		int height = state->stateExpGrp->totalHeight;

		// remove expanders
		ui_ExpanderGroupRemoveExpander(state->stateExpGrp, state->subFSMExp);
		ui_ExpanderGroupRemoveExpander(state->stateExpGrp, state->actionExpr->expander);

		// hide the sub FSM expander and show the action expanders
		ui_ExpanderGroupAddExpander(state->stateExpGrp, state->actionExpr->expander);
		height -= state->stateExpGrp->totalHeight;

		// shift UI elements as necessary
		ui_WidgetSetDimensions((UIWidget*) &state->stateWin->window, state->stateWin->window.widget.width, state->stateWin->window.widget.height - height);
	}
}

static FSMGroup *fsmEditorGetGroupFromScope(const char *scope) 
{
	char groupBuf[1024];
	int i;
	char *ptr;
	
	if (!scope)
		return NULL;

	groupBuf[0] = '\0';
	ptr = strchr(scope, '/');
	if (ptr)
	{
		strncpy(groupBuf, scope, ptr-scope);
		groupBuf[ptr-scope] = '\0';
	}
	else
	{
		strcpy(groupBuf, scope);
	}

	for(i=eaSize(&fsmEditorGroups.fsmGroups)-1; i>=0; --i) 
	{
		if (stricmp(groupBuf, fsmEditorGroups.fsmGroups[i]->name) == 0)
			return fsmEditorGroups.fsmGroups[i];
	}
	return NULL;
}

static bool fsmEditorMakeFileName(const char *name, const char *scope, char **estr)
{
	const char *parsedScope = NULL;
	FSMGroup *group;

	if (!name || !strlen(name)) 
	{
		estrPrintf(estr, "Name is not currently legal");
		return false;
	}

	// Determine if scope is legal
	group = fsmEditorGetGroupFromScope(scope);
	if (!group)
	{
		estrPrintf(estr, "Scope is not currently legal");
		return false;
	}

	// Remove group name from the parsed version of the scope
	parsedScope = scope + strlen(group->name);
	if (*parsedScope == '/')
		++parsedScope;

	estrPrintf(estr, "%s", group->dir);
	estrConcatChar(estr, '/');
	if (parsedScope)
	{
		estrConcat(estr, parsedScope, (int)strlen(parsedScope));
		estrConcatChar(estr, '/');
	}
	estrConcat(estr, name, (int)strlen(name));
	estrConcat(estr, ".fsm", 4);

	return true;
}

/********************************************************************
* PANELS
********************************************************************/
static void fsmEditorCommentChanged(UITextArea *commentArea, FSMEditorDoc *doc)
{
	if (!fsmEditorCanEdit(doc))
		return;

	emSetDocUnsaved(&doc->doc, true);
}

static EMPanel *fsmEditorCreateCommentPanel(FSMEditorDoc *doc)
{
	EMPanel *panel;
	UITextArea *textArea;

	panel = emPanelCreate("FSM", "Comment", 0);

	textArea = ui_TextAreaCreate("");
	ui_TextAreaSetChangedCallback(textArea, fsmEditorCommentChanged, doc);
	ui_WidgetSetWidthEx(UI_WIDGET(textArea), 1, UIUnitPercentage);
	ui_WidgetSetHeightEx(UI_WIDGET(textArea), 1, UIUnitPercentage);
	emPanelAddChild(panel, textArea, false);
	doc->fsmEditorUI.fsmCommentArea = textArea;
	emPanelSetHeight(panel, 150);

	return panel;
}

static void fsmEditorLayoutLoadLocal(UIButton *button, FSMEditorDoc *doc)
{
	fsmEditorDocPrefsApply(doc, false);
}

static void fsmEditorLayoutLoadFile(UIButton *button, FSMEditorDoc *doc)
{
	fsmEditorDocPrefsApply(doc, true);
}

static EMPanel *fsmEditorCreateLayoutPanel(FSMEditorDoc *doc)
{
	EMPanel *panel;
	UIButton *button;

	panel = emPanelCreate("Utilities", "Layout", 0);

	button = ui_ButtonCreate("Apply last saved local layout", 0, 0, fsmEditorLayoutLoadLocal, doc);
	ui_WidgetSetWidthEx(UI_WIDGET(button), 1, UIUnitPercentage);
	emPanelAddChild(panel, button, false);
	button = ui_ButtonCreate("Apply FSM layout", 0, elUINextY(button), fsmEditorLayoutLoadFile, doc);
	ui_WidgetSetWidthEx(UI_WIDGET(button), 1, UIUnitPercentage);
	emPanelAddChild(panel, button, true);

	return panel;
}

bool fsmEditorUsageSearch(EMSearchResult *result, const char *search_text)
{
	// TODO: Perform search on the server of the client
	return false;
}


static void fsmUsageSearchButtonClicked(UIWidget *unused, UITextEntry *entry)
{
	emSearchUsages(ui_TextEntryGetText(entry));
}

static EMPanel *fsmEditorCreateSearchPanel(void)
{
	EMPanel *panel;
	UILabel *label;
	UITextEntry *entry;
	UIButton *button;

	panel = emPanelCreate("Utilities", "Usage Search", 0);

	label = ui_LabelCreate("Search for usages of this text:", 0, 0);
	emPanelAddChild(panel, label, false);

	entry = ui_TextEntryCreate("", 0, elUINextY(label) + 5);
	ui_WidgetSetWidthEx(UI_WIDGET(entry), 1, UIUnitPercentage);
	ui_TextEntrySetEnterCallback(entry, fsmUsageSearchButtonClicked, entry);
	emPanelAddChild(panel, entry, false);

	button = ui_ButtonCreate("Search", 0, entry->widget.y, fsmUsageSearchButtonClicked, entry);
	emPanelAddChild(panel, button, true);
	button->widget.offsetFrom = UITopRight;
	entry->widget.rightPad = elUINextX(button) + 5;

	return panel;
}

/********************************************************************
* CALLBACKS
********************************************************************/
// callback from dragging a state window
static void fsmEditorStateWinTick(UIWindow *window, UI_PARENT_ARGS)
{
	FSMEditorState *state = window->widget.dragData;
	int x, y;

	x = window->widget.x;
	y = window->widget.y;
	ui_WindowTick(window, UI_PARENT_VALUES);


	// flag change on document to save layout
	if (window->dragging && state->machine->doc->doc.saved && (x != window->widget.x || y != window->widget.y))
		emSetDocUnsaved(&state->machine->doc->doc, true);
}

// got focus callback from comment text area
static void fsmEditorStateCommentGotFocus(UITextArea *textArea, FSMEditorState *state)
{
	ui_WidgetSetHeight(&state->stateWin->window.widget, state->stateWin->window.widget.height + textArea->widget.height - 20);
	state->stateExpGrp->widget.y = elUINextY(textArea);
	textArea->widget.y = 0;
}

// lost focus callback from comment text area
static void fsmEditorStateCommentLostFocus(UITextArea *textArea, FSMEditorState *state)
{
	ui_WidgetSetHeight(&state->stateWin->window.widget, state->stateWin->window.widget.height - textArea->widget.height + 20);
	state->stateExpGrp->widget.y = 0;
	textArea->widget.y = -25;
}

static void fsmEditorOnGraphNodeLostFocus(UIAnyWidget *widget, UserData data)
{
	// The Esc key appears to be unavailable to the onInput callbacks
	// since the LostFocus function captures it
	// this is a special hack to detect when the user presses the escape key (to clear selection)
	// maybe this could be moved to a keybind?

	UIGraphNode *pGraphNode = (UIGraphNode*)widget;
	FSMEditorState *pEditorState = (FSMEditorState*)pGraphNode->userData;
	KeyInput *pKey = inpGetKeyBuf();

	if(pKey && pKey->scancode == INP_ESCAPE)
	{
		fsmEditorClearSelection(NULL, pEditorState->machine->doc);
	}
}

// callback from changing state's comment
static void fsmEditorStateCommentChanged(UITextArea *textArea, FSMEditorState *state)
{
	if (!fsmEditorCanEdit(state->machine->doc))
		return;

	emSetDocUnsaved(&state->machine->doc->doc, true);
}

// callback from the expression editor's OK button
static void fsmEditorExprCB(Expression *expr, FSMEditorExpr *edExpr)
{
	// Check if editable and don't apply change if it isn't
	if (!fsmEditorCanEdit(edExpr->state->machine->doc))
		return;

	exprDestroy(edExpr->expr);
	edExpr->expr = exprCreate();
	if (expr)
		exprCopy(edExpr->expr, expr);
	fsmEditorRefreshExprUI(edExpr);
	emSetDocUnsaved(&edExpr->state->machine->doc->doc, true);
}

// callback for non-transition-related expanders when they expand
static void fsmEditorStateExpandCallback(UIExpander *exp, FSMEditorState *state)
{
	UIWindow *win = &state->stateWin->window;

	// flag change on document to save layout
	emSetDocUnsaved(&state->machine->doc->doc, true);

	// only move widgets around if the expander is actually in the state's expander group
	if (eaFind(&state->stateExpGrp->childrenInOrder, (UIWidget*)exp) == -1)
		return;

	if (!exp->widget.childrenInactive)
		ui_WidgetSetDimensions((UIWidget*) win, win->widget.width, win->widget.height + exp->openedHeight);
	else
		ui_WidgetSetDimensions((UIWidget*) win, win->widget.width, win->widget.height - exp->openedHeight);
}

// callback for transition-related expanders when they expand
static void fsmEditorTransExpandCallback(UIExpander *exp, FSMEditorState *state)
{
	UIWindow *win = &state->stateWin->window;

	// flag change on document to save layout
	emSetDocUnsaved(&state->machine->doc->doc, true);

	if (!exp->widget.childrenInactive)
	{
		fsmEditorMoveButtonsY(state, exp->openedHeight);
		ui_WidgetSetDimensions((UIWidget*) win, win->widget.width, win->widget.height + exp->openedHeight);
	}
	else
	{
		fsmEditorMoveButtonsY(state, -exp->openedHeight);
		ui_WidgetSetDimensions((UIWidget*) win, win->widget.width, win->widget.height - exp->openedHeight);
	}
}

// callback for an expression's individual line buttons
static void fsmEditorEditLine(UIButton *button, FSMEditorExprLine *line)
{
	FSMGroup *group = line->expr->state->machine->doc->refGroup;
	ExprContext *context = NULL;

	stashFindPointer(fsmEditorGroupContexts, group->name, &context);
	exprEdOpen(fsmEditorExprCB, line->expr->expr, line->expr, context, eaFind(&line->expr->lines, line));
}

// callback for an expression's edit all button
static void fsmEditorEditExpr(UIButton *button, FSMEditorExpr *expr)
{
	FSMGroup *group = expr->state->machine->doc->refGroup;
	ExprContext *context = NULL;

	stashFindPointer(fsmEditorGroupContexts, group->name, &context);
	exprEdOpen(fsmEditorExprCB, expr->expr, expr, context, -1);
}

// callback for dragging expressions from edit all button
static void fsmEditorExprDrag(UIButton *button, Expression *expr)
{
	if (expr && eaSize(&expr->lines) > 0)
		ui_DragStart((UIWidget*) button, ecbClipTypeToString(ECB_EXPRESSION), expr, atlasLoadTexture("button_pinned.tga"));
}

// callback for dropping expressions onto edit all button
static void fsmEditorExprDrop(UIWidget *source, UIButton *dest, UIDnDPayload *payload, FSMEditorExpr *expr)
{
	// verify payload
	if (strcmpi(payload->type, ecbClipTypeToString(ECB_EXPRESSION)) != 0)
	{
		ui_DragCancel();
		return;
	}

	// ensure payload is not the same as the destination
	if (payload->payload == expr->expr)
	{
		ui_DragCancel();
		return;
	}

	fsmEditorExprCB(payload->payload, expr);
}

// callback for dragging expression lines from a line button
static void fsmEditorExprLineDrag(UIButton *button, ExprLine *exprLine)
{
	if (exprLine && exprLine->origStr && strlen(exprLine->origStr) > 0)
		ui_DragStart((UIWidget*) button, ecbClipTypeToString(ECB_EXPR_LINE), exprLine, atlasLoadTexture("button_pinned.tga"));
}

// callback for dropping expression lines onto a line button
static void fsmEditorExprLineDrop(UIWidget *source, UIButton *dest, UIDnDPayload *payload, FSMEditorExprLine *exprLine)
{
	ExprLine *payloadLine;
	int i;

	// Check if editing is enabled
	if (!fsmEditorCanEdit(exprLine->expr->state->machine->doc))
	{
		ui_DragCancel();
		return;
	}

	if (strcmpi(payload->type, ecbClipTypeToString(ECB_EXPR_LINE)) != 0)
	{
		ui_DragCancel();
		return;
	}

	// ensure user is not dropping onto the source
	if (payload->payload == exprLine->line)
	{
		ui_DragCancel();
		return;
	}

	payloadLine = (ExprLine*) payload->payload;
	i = eaFind(&exprLine->expr->expr->lines, exprLine->line);
	if (payloadLine->descStr && strlen(payloadLine->descStr) > 0)
		exprLineSetDescStr(exprLine->expr->expr->lines[i], payloadLine->descStr);
	exprLineSetOrigStr(exprLine->expr->expr->lines[i], payloadLine->origStr);
	emSetDocUnsaved(&exprLine->expr->state->machine->doc->doc, true);
	fsmEditorRefreshExprUI(exprLine->expr);
}

static FSMEditorState* fsmEditorFindOldStateByName(FSMEditorState **ppNewEdStates, const char *pchTargetName)
{
	FSMEditorState *pNewState = NULL;
	
	// look through states and attempt to match oldName
	FOR_EACH_IN_EARRAY(ppNewEdStates, FSMEditorState, pNewEditorState)
	{
		if(!strcmp(pchTargetName, pNewEditorState->oldName))
		{
			pNewState = pNewEditorState;
			break;
		}	
	}
	FOR_EACH_END

	return pNewState;
}

// callback for dropping states from clipboard onto state machine window
static void fsmEditorStateDrop(UIWidget *source, UIWindow *dest, UIDnDPayload *payload, FSMEditorDoc *doc)
{
	FSMEditorState *edState;
	int i;
	char newName[FSME_STATE_NAME_MAX_LEN];

	// Check if editable
	if (!fsmEditorCanEdit(doc))
	{
		ui_DragCancel();
		return;
	}

	if(strcmpi(payload->type, ecbClipTypeToString(ECB_STATE)) == 0)
	{
		FSMState *state;

		state = (FSMState*) payload->payload;
		edState = fsmEditorAddState(doc);
		fsmEditorUniqueStateName(doc, SAFESTR(newName));
		fsmEditorStateSetName(edState, newName);
		fsmEditorLoadState(edState, state);
		for (i = 0; i < eaSize(&state->transitions); i++)
			fsmEditorLoadTrans(edState, NULL, state->transitions[i]);
		emSetDocUnsaved(&doc->doc, true);
	}
	else if(strcmpi(payload->type, ecbClipTypeToString(ECB_STATES)) == 0)
	{
		FSMStates *states;
		FSMEditorState **ppNewEdStates = NULL;

		states = (FSMStates*) payload->payload;

		FOR_EACH_IN_EARRAY(states->ppStates, FSMState, pState)
		{
			edState = fsmEditorAddState(doc);
			edState->oldName = strdup(pState->name); // tmp hold onto old name
			edState->userData = pState; 

			fsmEditorUniqueStateName(doc, SAFESTR(newName));
			
			fsmEditorStateSetName(edState, newName);
			fsmEditorLoadState(edState, pState);

			eaPush(&ppNewEdStates, edState);
		}
		FOR_EACH_END

		// make the connections
		FOR_EACH_IN_EARRAY(ppNewEdStates, FSMEditorState, pEditorState)
		{
			FSMState *pState = pEditorState->userData;

			FOR_EACH_IN_EARRAY(pState->transitions, FSMTransition, pTransition)
			{
				FSMEditorState *pNewState = fsmEditorFindOldStateByName(ppNewEdStates, pTransition->targetName);

				fsmEditorLoadTrans(pEditorState, pNewState, pTransition);
			}
			FOR_EACH_END
		}
		FOR_EACH_END

		// clean up
		FOR_EACH_IN_EARRAY(ppNewEdStates, FSMEditorState, pEditorState)
		{
			SAFE_FREE(pEditorState->oldName);
		}
		FOR_EACH_END

		eaDestroy(&ppNewEdStates);

		emSetDocUnsaved(&doc->doc, true);
	}
	else
	{
		ui_DragCancel();
		return;
	}
}

// drag callback for connecting states
static void fsmEditorConnectDrag(UIButton *button, FSMEditorState *state)
{
	ui_DragStart((UIWidget*) button, FSME_STATE_PAYLOAD, state, atlasLoadTexture("button_pinned.tga"));
}

// drop callback for connecting states
static void fsmEditorConnectDrop(UIButton *source, UIButton *dest, UIDnDPayload *payload, FSMEditorState *destState)
{
	FSMEditorState *startState = NULL;
	FSMEditorTransButton *startTrans = NULL;

	// verify the correct payload type
	if (strcmpi(payload->type, FSME_STATE_PAYLOAD) == 0)
		startState = (FSMEditorState*) payload->payload;	
	else if (strcmpi(payload->type, FSME_TRANSBTN_PAYLOAD) == 0)
		startTrans = (FSMEditorTransButton*) payload->payload;
	else
	{
		ui_DragCancel();
		return;
	}

	// do not let connections go in-to-in or out-to-out
	if ((startState &&
		((source == startState->in && dest == destState->in)
		|| (source == startState->out && dest == destState->out))) ||
		(startTrans && dest == destState->out))
	{
		emStatusPrintf("Cannot make input-to-input or output-to-output connections!");
		ui_DragCancel();
		return;
	}


	// Check if editable
	if (!fsmEditorCanEdit(destState->machine->doc))
	{
		ui_DragCancel();
		return;
	}

	// find the correct connecting order
	if (startTrans)
	{
		if (startTrans->pair)
			fsmEditorInTransDelete(NULL, startTrans->pair);
		fsmEditorReconnectStates(startTrans, destState);
	}
	else if (startState && source == startState->out)
		fsmEditorConnectStates(startState, destState);
	else if (startState)
		fsmEditorConnectStates(destState, startState);
	emSetDocUnsaved(&destState->machine->doc->doc, true);
}

static void fsmEditorTransDrag(UIExpander *expander, FSMEditorTransButton *trans)
{
	ui_DragStart((UIWidget*) expander, FSME_TRANS_PAYLOAD, trans, atlasLoadTexture("button_pinned.tga"));
}

static void fsmEditorTransDrop(UIWidget *source, UIExpander *dest, UIDnDPayload *payload, FSMEditorTransButton *trans)
{
	FSMEditorTransButton *srcTrans;
	int idx;

	if (strcmpi(payload->type, FSME_TRANS_PAYLOAD) != 0)
	{
		ui_DragCancel();
		return;
	}

	// Check if editable
	if (!fsmEditorCanEdit(trans->state->machine->doc))
	{
		return;
	}

	srcTrans = (FSMEditorTransButton *) payload->payload;

	// verify that the transition is only dragged within its own state and that it is not dragged to itself
	if (srcTrans == trans || srcTrans->state != trans->state)
	{
		ui_DragCancel();
		return;
	}

	// place payload transition ABOVE destination transition expander
	eaFindAndRemove(&srcTrans->state->connsOut, srcTrans);
	ui_ExpanderGroupRemoveExpander(srcTrans->state->connsExpGrp, srcTrans->expander);
	idx = eaFind(&srcTrans->state->connsOut, trans);
	eaInsert(&srcTrans->state->connsOut, srcTrans, idx);
	ui_ExpanderGroupInsertExpander(srcTrans->state->connsExpGrp, srcTrans->expander, idx);
	emSetDocUnsaved(&trans->state->machine->doc->doc, true);
}

// drag callback for connecting existing transitions
static void fsmEditorTransButtonDrag(UIButton *button, FSMEditorTransButton *trans)
{
	ui_DragStart((UIWidget*) button, FSME_TRANSBTN_PAYLOAD, trans, atlasLoadTexture("button_pinned.tga"));
}

// drop callback for connecting existing transitions
static void fsmEditorTransButtonDrop(UIButton *source, UIButton *dest, UIDnDPayload *payload, FSMEditorTransButton *trans)
{
	FSMEditorState *destState;

	// verify the correct payload type
	if (strcmpi(payload->type, FSME_STATE_PAYLOAD) != 0)
	{
		ui_DragCancel();
		return;
	}

	// Check if editable
	if (!fsmEditorCanEdit(trans->state->machine->doc))
	{
		return;
	}

	destState = (FSMEditorState*) (payload->payload);

	// do not let connections go from one state to itself
	if (trans->state == destState)
	{
		emStatusPrintf("Cannot connect a state to itself!");
		ui_DragCancel();
		return;
	}

	// cleanup any existing connection to the current transition
	if (trans->pair)
		fsmEditorInTransDelete(NULL, trans->pair);

	// move the connection
	fsmEditorReconnectStates(trans, destState);
	emSetDocUnsaved(&trans->state->machine->doc->doc, true);
}

// menu callback for state renaming
static void fsmEditorStateRename(UIMenuItem *item, FSMEditorState *state)
{
	// Check if editable
	if (!fsmEditorCanEdit(state->machine->doc))
	{
		return;
	}

	fsmEditorStateRenameWinCreate(state);
}

// rename window cancel callback
static void fsmEditorStateRenameCancel(UIWidget *widget, FSMEditorState *state)
{
	ui_WidgetQueueFree((UIWidget*) state->machine->doc->fsmEditorUI.modalData.window);
}

// rename window OK callback
static void fsmEditorStateRenameOk(UIWidget *widget, FSMEditorState *state)
{
	fsmEditorStateSetName(state, ui_TextEntryGetText(state->machine->doc->fsmEditorUI.modalData.entry));
	fsmEditorStateRenameCancel(NULL, state);
	emSetDocUnsaved(&state->machine->doc->doc, true);
}

// FSM rename window cancel callback
static void fsmEditorFSMSetNameCancel(UIWidget *widget, FSMEditorMachine *fsm)
{
	ui_WidgetQueueFree((UIWidget*) fsm->doc->fsmEditorUI.modalData.window);
}

// FSM rename window OK callback
static void fsmEditorFSMSetNameOk(UIWidget *widget, FSMEditorMachine *fsm)
{
	fsmEditorFSMSetName(fsm->doc, ui_TextEntryGetText(fsm->doc->fsmEditorUI.modalData.entry));
	fsmEditorFSMSetNameCancel(NULL, fsm);
	emSetDocUnsaved(&fsm->doc->doc, true);
}

// FSM rescope window cancel callback
static void fsmEditorFSMSetScopeCancel(UIWidget *widget, FSMEditorMachine *fsm)
{
	ui_WidgetQueueFree((UIWidget*) fsm->doc->fsmEditorUI.modalData.window);
}

// FSM rescope window OK callback
static void fsmEditorFSMSetScopeOk(UIWidget *widget, FSMEditorMachine *fsm)
{
	fsmEditorFSMSetScope(fsm->doc, ui_TextEntryGetText(fsm->doc->fsmEditorUI.modalData.entry));
	fsmEditorFSMSetScopeCancel(NULL, fsm);
	emSetDocUnsaved(&fsm->doc->doc, true);
}

// menu callback for setting start state
static void fsmEditorStateSetStart(UIMenuItem *item, FSMEditorState *state)
{
	FSMEditorDoc *activeDoc = state->machine->doc;

	// Check if editable
	if (!fsmEditorCanEdit(state->machine->doc))
	{
		return;
	}

	if (state != activeDoc->workingFSM.startState)
	{
		FSMEditorState *oldStart = activeDoc->workingFSM.startState;
		activeDoc->workingFSM.startState = state;
		eaMove(&state->machine->states, 0, eaFind(&state->machine->states, state));
		fsmEditorRefreshState(oldStart);
		fsmEditorRefreshState(state);
		emSetDocUnsaved(&activeDoc->doc, true);
	}
}

// callback for in transition deletion
static void fsmEditorInTransDelete(UIWidget *unused, FSMEditorTransButton *inButton)
{
	ui_PairedBoxDisconnect(inButton->pair->pBox, inButton->pBox);
	ui_WindowRemoveChild(&inButton->state->stateWin->window, inButton->pBox);
	eaFindAndRemove(&inButton->state->connsIn, inButton);
	emSetDocUnsaved(&inButton->state->machine->doc->doc, true);
	fsmEditorTransButtonFree(inButton);
}

// out transition delete button callback
static void fsmEditorOutTransDelete(UIButton *unused, FSMEditorTransButton *outButton)
{
	UIWindow *stateWin = &outButton->state->stateWin->window;

	// this should only be called on outbound transition buttons
	assert (outButton->expander);

	// Check if editable
	if (!fsmEditorCanEdit(outButton->state->machine->doc))
	{
		return;
	}

	// handle effect on outButton's destination state
	if (outButton->pair)
		fsmEditorInTransDelete(NULL, outButton->pair);

	// handle effect on outButton's state
	fsmEditorMoveButtonsY(outButton->state, -outButton->expander->widget.height);
	ui_WidgetSetDimensions((UIWidget*) stateWin, stateWin->widget.width, stateWin->widget.height - outButton->expander->widget.height);
	ui_ExpanderGroupRemoveExpander(outButton->state->connsExpGrp, outButton->expander);
	eaFindAndRemove(&outButton->state->connsOut, outButton);
	emSetDocUnsaved(&outButton->state->machine->doc->doc, true);
	fsmEditorTransButtonFree(outButton);
}

static void fsmEditorDeleteSelection(UIMenuItem *item, FSMEditorDoc *doc)
{
	if(!doc)
		return;

	// stepping through in reverse order should prevent array from getting foo-bar'd
	FOR_EACH_IN_EARRAY(doc->ppSelectedGraphNodes, UIGraphNode, pNode)
	{
		fsmEditorStateDelete(item, pNode->userData);
	}
	FOR_EACH_END
}

static void fsmEditorStateAddComment(UIMenuItem *item, FSMEditorState *state)
{
	fsmEditorSetCommentVisible(state, true);
}

// delete state menu callback
static void fsmEditorStateDelete(UIMenuItem *item, FSMEditorState *state)
{
	FSMEditorDoc *activeDoc = state->machine->doc;
	int i;

	// Check if editable
	if (!fsmEditorCanEdit(state->machine->doc))
	{
		return;
	}

	// deal with deletion of start state
	if (state == activeDoc->workingFSM.startState)
	{
		if (eaSize(&activeDoc->workingFSM.states) == 1)
			return;
	}

	eaFindAndRemove(&activeDoc->workingFSM.states, state);
	eaFindAndRemove(&activeDoc->ppSelectedGraphNodes, state->stateWin); // remove from selected graph nodes

	if(state == activeDoc->workingFSM.startState)
	{
		activeDoc->workingFSM.startState = activeDoc->workingFSM.states[0];
		fsmEditorRefreshState(activeDoc->workingFSM.startState);
	}

	// deal with connections
	for (i = 0; i < eaSize(&state->connsIn); i++)
	{
		FSMEditorTransButton *button = state->connsIn[i];
		if (button->pair->state != state)
		{
			UIWindow *stateWin = &button->pair->state->stateWin->window;
			ui_PairedBoxDisconnect(button->pair->pBox, button->pBox);
			fsmEditorMoveButtonsY(button->pair->state, -button->pair->expander->widget.height);
			ui_WidgetSetDimensions((UIWidget*) stateWin, stateWin->widget.width, stateWin->widget.height - button->pair->expander->widget.height);
			ui_ExpanderGroupRemoveExpander(button->pair->state->connsExpGrp, button->pair->expander);
			eaFindAndRemove(&button->pair->state->connsOut, button->pair);
			fsmEditorTransButtonFree(button->pair);
		}
	}
	for (i = 0; i < eaSize(&state->connsOut); i++)
	{
		FSMEditorTransButton *button = state->connsOut[i];
		if (button->pair && button->pair->state != state)
		{
			ui_PairedBoxDisconnect(button->pBox, button->pair->pBox);
			ui_WindowRemoveChild(&button->pair->state->stateWin->window, button->pair->pBox);
			eaFindAndRemove(&button->pair->state->connsIn, button->pair);
			fsmEditorTransButtonFree(button->pair);
		}
	}
	

	ui_GraphNodeClose(state->stateWin);
	
	fsmEditorStateFree(state);
	emSetDocUnsaved(&activeDoc->doc, true);
}

// select sub FSM list name text callback
static void fsmEditorSelectSubFSMNameMakeText(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	estrPrintf(output, "%s", ((char**) *list->peaModel)[row]);
}

// select sub FSM callback
static void fsmEditorSelectSubFSM(UIWidget *widget, FSMEditorState *state)
{
	// Check if editable
	if (!fsmEditorCanEdit(state->machine->doc))
	{
		return;
	}

	fsmEditorSelectSubFSMCreate(state);
}

// select sub FSM cancel callback
bool fsmEditorSelectSubFSMCancel(UIWidget *widget, FSMEditorDoc *doc)
{
	eaDestroy(doc->fsmEditorUI.modalData.list->peaModel);
	ui_WidgetQueueFree((UIWidget*) doc->fsmEditorUI.modalData.window);
	return true;
}

// select sub FSM list activated callback
static void fsmEditorSelectSubFSMListActivated(UIList *list, FSMEditorState *state)
{
	char *sel = (char*) ui_ListGetSelectedObject(list);
	if (sel)
	{
		fsmEditorStateSetSubFSM(state, sel);
		fsmEditorSelectSubFSMCancel(NULL, state->machine->doc);
		emSetDocUnsaved(&state->machine->doc->doc, true);
	}
}

// select sub FSM OK callback
static void fsmEditorSelectSubFSMOk(UIWidget *widget, UIList *fsmList)
{
	FSMEditorState *currState = (FSMEditorState*) fsmList->pActivatedData;
	fsmEditorSelectSubFSMListActivated(fsmList, currState);
}

// select sub FSM search changed callback
static void fsmEditorSelectSubFSMSearchChanged(UITextEntry *entry, FSMEditorDoc *doc)
{
	const char *text = ui_TextEntryGetText(entry);
	ResourceDictionaryInfo *pDictInfo;
	int i, len;

	// clear the list
	eaDestroyEx(doc->fsmEditorUI.modalData.list->peaModel, NULL);

	// recreate the filtered list
	pDictInfo = resDictGetInfo("FSM");
	len = (int)strlen(doc->refGroup->name);

	for( i = 0; i<eaSize(&pDictInfo->ppInfos); i++)
	{
		const char *name = pDictInfo->ppInfos[i]->resourceName;
		if (strstri(name, text) && pDictInfo->ppInfos[i]->resourceScope &&
			(strnicmp(pDictInfo->ppInfos[i]->resourceScope, doc->refGroup->name, len) == 0))
		{
			eaPush(doc->fsmEditorUI.modalData.list->peaModel, strdup(name));
		}
	}
	ui_ListSetSelectedRow(doc->fsmEditorUI.modalData.list, -1);
}

// select sub FSM search finished callback
static void fsmEditorSelectSubFSMSearchFinished(UITextEntry *entry, FSMEditorState *state)
{
	UIList *list = state->machine->doc->fsmEditorUI.modalData.list;
	if (eaSize(list->peaModel) == 1)
	{
		ui_ListSetSelectedRow(list, 0);
		fsmEditorSelectSubFSMListActivated(list, state);
	}
}

// sub FSM clear callback
static void fsmEditorSubFSMClear(UIButton *button, FSMEditorState *state)
{
	// Check if editable
	if (!fsmEditorCanEdit(state->machine->doc))
	{
		return;
	}

	fsmEditorStateSetSubFSM(state, NULL);
}

// sub FSM open callback
static void fsmEditorSubFSMOpen(UIButton *button, FSMEditorState *state)
{
	const char *fsmName = REF_STRING_FROM_HANDLE(state->subFSM);
	if (fsmName)
		emOpenFileEx(fsmName, "fsm");
}

// expand/collapse menu callbacks
static void fsmEditorStateExpandState(UIMenuItem *item, FSMEditorState *state)
{
	if(eaSize(&state->onEntryFExpr->expr->lines) > 0)
	{
		ui_ExpanderSetOpened(state->onEntryFExpr->expander, true);
	}
	if(eaSize(&state->onEntryExpr->expr->lines) > 0)
	{
		ui_ExpanderSetOpened(state->onEntryExpr->expander, true);
	}

	if (GET_REF(state->subFSM))
	{
		ui_ExpanderSetOpened(state->subFSMExp, true);
	}
	else
	{
		if(eaSize(&state->actionExpr->expr->lines) > 0)
		{
			ui_ExpanderSetOpened(state->actionExpr->expander, true);
		}
	}
}

static void fsmEditorStateExpandTrans(UIMenuItem *item, FSMEditorState *state)
{
	int i;
	for (i = 0; i < eaSize(&state->connsOut); i++)
		ui_ExpanderSetOpened(state->connsOut[i]->expander, true);
}

static void fsmEditorStateExpandAll(UIMenuItem *item, FSMEditorState *state)
{
	fsmEditorStateExpandState(item, state);
	fsmEditorStateExpandTrans(item, state);
}

static void fsmEditorStateCollapseState(UIMenuItem *item, FSMEditorState *state)
{
	ui_ExpanderSetOpened(state->onEntryFExpr->expander, false);
	ui_ExpanderSetOpened(state->onEntryExpr->expander, false);
	if (GET_REF(state->subFSM))
		ui_ExpanderSetOpened(state->subFSMExp, false);
	else
		ui_ExpanderSetOpened(state->actionExpr->expander, false);
}

static void fsmEditorStateCollapseTrans(UIMenuItem *item, FSMEditorState *state)
{
	int i;
	for (i = 0; i < eaSize(&state->connsOut); i++)
		ui_ExpanderSetOpened(state->connsOut[i]->expander, false);
}

static void fsmEditorStateCollapseAll(UIMenuItem *item, FSMEditorState *state)
{
	fsmEditorStateCollapseState(item, state);
	fsmEditorStateCollapseTrans(item, state);
}

static void fsmEditorCopySelectionToClipboard(UIMenuItem *item, FSMEditorDoc *doc)
{
	if(doc)
	{
		FSMStates *dstStates = StructCreate(parse_FSMStates);
		
		FOR_EACH_IN_EARRAY(doc->ppSelectedGraphNodes, UIGraphNode, pNode)
		{
			FSMState *dstState = StructCreate(parse_FSMState);
			FSMEditorState *srcState = pNode->userData;

			fsmEditorPopulateFSMState(srcState, dstState, true);

			eaPush(&dstStates->ppStates, dstState);
		}
		FOR_EACH_END

		ecbAddClip(dstStates, ECB_STATES);

		StructDestroy(parse_FSMStates, dstStates);
	}
}

// copy state to clipboard
static void fsmEditorStateCopyToClipboard(UIMenuItem *item, FSMEditorState *state)
{
	FSMState *convState = StructCreate(parse_FSMState);
	int i;

	fsmEditorPopulateFSMState(state, convState, false);
	for (i = 0; i < eaSize(&convState->transitions); i++)
	{
		convState->transitions[i]->targetName = NULL;
	}
	ecbAddClip(convState, ECB_STATE);
	StructDestroy(parse_FSMState, convState);
}

static void fsmEditorRefreshOverride(FSMEditorDoc *doc)
{
	UIComboBox *fsmCombo = doc->fsmEditorUI.modalData.cb1;
	UIComboBox *stateCombo = doc->fsmEditorUI.modalData.cb2;
	const char *fsmName = REF_STRING_FROM_HANDLE(doc->workingFSM.overrideFSM);
	const char *stateName = REF_STRING_FROM_HANDLE(doc->workingFSM.overrideStatePath);

	if (fsmName)
	{
		// Make sure FSM exists, else clear handles and names
		ResourceDictionaryInfo *pDictInfo = resDictGetInfo("FSM");
		int i;

		for( i = eaSize(&pDictInfo->ppInfos)-1; i>=0; --i)
		{
			if (stricmp(pDictInfo->ppInfos[i]->resourceName, fsmName) == 0)
				break;
		}
		if (i < 0)
		{
			// FSM was deleted
			REMOVE_HANDLE(doc->workingFSM.overrideFSM);
			REMOVE_HANDLE(doc->workingFSM.overrideStatePath);
			fsmName = NULL;
			stateName = NULL;
			emSetDocUnsaved(&doc->doc, true);
		}
	}

	// Update modal dialog combo for states based on FSM choice
	// Does not apply changes to the working FSM since this is just a chooser window
	if (fsmCombo && stateCombo) 
	{
		const char *fsmChoice = ui_ComboBoxGetSelectedObject(fsmCombo);
		const char *stateChoice = ui_ComboBoxGetSelectedObject(stateCombo);

		if (fsmChoice)
		{
			const char *currChoice = REF_STRING_FROM_HANDLE(doc->fsmEditorUI.modalData.fsmHandle);
			if (!currChoice || stricmp(currChoice, fsmChoice) != 0)
			{
				REMOVE_HANDLE(doc->fsmEditorUI.modalData.fsmHandle);
				SET_HANDLE_FROM_STRING(gFSMDict, fsmChoice, doc->fsmEditorUI.modalData.fsmHandle);
			}
		}
		if (fsmChoice) 
		{
			FSM *fsm = RefSystem_ReferentFromString(gFSMDict, fsmChoice);
			if (fsm)
			{
				int i;

				// Deal with the state when the chooser is first opened and the initial FSM is loaded
				if (!stateChoice)
				{
					const char *overrideString = REF_STRING_FROM_HANDLE(doc->workingFSM.overrideStatePath);
					if (overrideString)
					{
						stateChoice = strchr(overrideString, ':');
						if (stateChoice)
							stateChoice++;
					}
				}

				ui_ComboBoxSetModel(stateCombo, parse_FSMState, &fsm->states);
				for(i=eaSize(&fsm->states)-1; i>=0; --i)
				{
					if (stateChoice && stricmp(stateChoice, fsm->states[i]->name) == 0)
					{
						ui_ComboBoxSetSelected(stateCombo, i);
						break;
					}
				}
				if (i < 0)
				{
					// State not found so reset it
					if (eaSize(&fsm->states))
						ui_ComboBoxSetSelected(stateCombo, 0);
					else
						ui_ComboBoxSetSelected(stateCombo, -1);
				}
			}
		} 
		else 
		{
			ui_ComboBoxSetSelected(stateCombo, -1);
			stateCombo->model = NULL;
		}
	}
}

// subFSM override clear
static void fsmEditorSubFSMOverrideClear(UIButton *button, UIComboBox *fsmCB)
{
	ui_ComboBoxSetSelectedAndCallback(fsmCB, -1);
}

// subFSM override FSM select callback
static void fsmEditorSubFSMOverrideFSMSelected(UIComboBox *cb, FSMEditorDoc *doc)
{
	fsmEditorRefreshOverride(doc);
}

static void fsmEditorSubFSMOverrideCancel(UIButton *button, FSMEditorDoc *doc)
{
	doc->fsmEditorUI.modalData.cb1 = NULL;
	doc->fsmEditorUI.modalData.cb2 = NULL;
	elUIWindowClose(NULL, doc->fsmEditorUI.modalData.window);
}

// subFSM override OK callback
static void fsmEditorSubFSMOverrideOk(UIButton *button, FSMEditorDoc *doc)
{
	UIComboBox *fsmCB = doc->fsmEditorUI.modalData.cb1;
	UIComboBox *stateCB = doc->fsmEditorUI.modalData.cb2;
	const char *selectedFSMName;
	FSMState *selectedFSMState;

	assert(stateCB);
	REMOVE_HANDLE(doc->workingFSM.overrideFSM);
	REMOVE_HANDLE(doc->workingFSM.overrideStatePath);
	if ((selectedFSMName = ui_ComboBoxGetSelectedObject(fsmCB)) && (selectedFSMState = ui_ComboBoxGetSelectedObject(stateCB)))
	{
		char refData[MAX_PATH];
		sprintf(refData, "%s:%s", selectedFSMName, selectedFSMState->name);
		SET_HANDLE_FROM_REFDATA("FSMStateDict", refData, doc->workingFSM.overrideStatePath);
	}
	emSetDocUnsaved(&doc->doc, true);

	doc->fsmEditorUI.modalData.cb1 = NULL;
	doc->fsmEditorUI.modalData.cb2 = NULL;
	elUIWindowClose(NULL, doc->fsmEditorUI.modalData.window);
}

static void fsmEditorSetNameAndScopeCancel(UIButton *button, FSMEditorNewDocParams *params)
{
	elUIWindowClose(NULL, params->window);
	free(params);
}

static void fsmEditorSetNameOk(UIButton *button, FSMEditorNewDocParams *params)
{
	char *fileName = NULL;

	// Clear sub-FSM overrides if scope changes
	if (strcmpi(params->workingFSM->scope, ui_TextEntryGetText(params->scopeEntry)) != 0)
	{
		REMOVE_HANDLE(params->workingFSM->overrideFSM);
		REMOVE_HANDLE(params->workingFSM->overrideStatePath);
	}

	// Set name, scope, and group
	fsmEditorFSMSetName(params->workingFSM->doc, ui_TextEntryGetText(params->nameEntry));
	fsmEditorFSMSetScope(params->workingFSM->doc, ui_TextEntryGetText(params->scopeEntry));
	params->workingFSM->doc->refGroup = fsmEditorGetGroupFromScope(params->workingFSM->scope);

	// Set file name
	fsmEditorMakeFileName(params->workingFSM->name, params->workingFSM->scope, &fileName);
	emDocRemoveAllFiles(&params->workingFSM->doc->doc, false);
	emDocAssocFile(&params->workingFSM->doc->doc, fileName);
	estrDestroy(&fileName);

	// Mark as dirty
	emSetDocUnsaved(&params->workingFSM->doc->doc, true);

	fsmEditorSetNameAndScopeCancel(button, params);
}

static void fsmEditorCustomNewOk(UIButton *button, FSMEditorNewDocParams *params)
{
	emNewDoc("fsm", params);
	fsmEditorSetNameAndScopeCancel(button, params);
}

/********************************************************************
* UI
********************************************************************/
static void fsmEditorUIInit(FSMEditorDoc *activeDoc)
{
	UIWindow *window;
	UIScrollArea *scrollArea;

	// initialize the state machine window
	window = ui_WindowCreate("State Machine", 0, 0, 700, 700);
	elUICenterWindow(window);
	ui_WidgetSetDropCallback((UIWidget*) window, fsmEditorStateDrop, activeDoc);
	scrollArea = ui_ScrollAreaCreate(0, 0, 1, 1, 0, 0, true, true);
	scrollArea->autosize = true;
	scrollArea->scrollPadding = 50;
	scrollArea->widget.inputF = fsmEditorOnScrollAreaInput;
	scrollArea->widget.userinfo = activeDoc;

	ui_WidgetSetDimensionsEx((UIWidget*) scrollArea, 1, 1, UIUnitPercentage, UIUnitPercentage);
	ui_WindowAddChild(window, scrollArea);
	activeDoc->fsmEditorUI.stateMachine.area = scrollArea;
	eaPush(&activeDoc->doc.ui_windows, window);
	activeDoc->fsmEditorUI.stateMachine.window = window;

	activeDoc->fsmEditorUI.redSkin = ui_SkinCreate(NULL);
	ui_SkinSetButton(activeDoc->fsmEditorUI.redSkin, colorFromRGBA(0xFF7777FF));
	activeDoc->fsmEditorUI.blueSkin = ui_SkinCreate(NULL);
	ui_SkinSetButton(activeDoc->fsmEditorUI.blueSkin, colorFromRGBA(0x7777FFFF));
	activeDoc->fsmEditorUI.greenSkin = ui_SkinCreate(NULL);
	ui_SkinSetButton(activeDoc->fsmEditorUI.greenSkin, colorFromRGBA(0x77FF77FF));

	// TODO: this is confusing -- it would be nice to remove the data dependency to the ui/bars/FSM_WindowTitle 
	// OR make both the skin and style bar data driven
	activeDoc->fsmEditorUI.selectedNodeSkin = ui_SkinCreate(NULL);
	ui_SkinSetBorderEx(activeDoc->fsmEditorUI.selectedNodeSkin, colorFromRGBA(0x0099ffFF), colorFromRGBA(0x0099ffFF));
	//ui_SkinSetTitleBarEx(activeDoc->fsmEditorUI.selectedNodeSkin, colorFromRGBA(0x0099ffFF), colorFromRGBA(0x0099ffFF));
	UI_SET_STYLE_BAR_NAME(activeDoc->fsmEditorUI.selectedNodeSkin->hTitlebarBar, "FSM_WindowTitle");

	activeDoc->fsmEditorUI.unselectedNodeSkin = ui_SkinCreate(NULL);
	ui_SkinSetBorderEx(activeDoc->fsmEditorUI.unselectedNodeSkin, colorFromRGBA(0x999999FF), colorFromRGBA(0x999999FF));
	//ui_SkinSetTitleBarEx(activeDoc->fsmEditorUI.unselectedNodeSkin, colorFromRGBA(0x999999FF), colorFromRGBA(0x999999FF));
	UI_SET_STYLE_BAR_NAME(activeDoc->fsmEditorUI.unselectedNodeSkin->hTitlebarBar, "FSM_WindowTitleNoSel");
 
	activeDoc->doc.primary_ui_window = window;

	// create panels
	eaPush(&activeDoc->doc.em_panels, fsmEditorCreateCommentPanel(activeDoc));
	eaPush(&activeDoc->doc.em_panels, fsmEditorCreateLayoutPanel(activeDoc));
	eaPush(&activeDoc->doc.em_panels, fsmEditorCreateSearchPanel());
}

static void fsmEditorToolbarZoomInClicked(UIButton *button, UserData unused)
{
	globCmdParse("FSMEditor.ZoomIn");
}

static void fsmEditorToolbarZoomOutClicked(UIButton *button, UserData unused)
{
	globCmdParse("FSMEditor.ZoomOut");
}

static EMMenuItemDef fsmEditorMenuItems[] = 
{
	{"savefsmlayout", "Save FSM with layout", NULL, NULL, "FSMEditor.SaveFSMLayout"},
	{"setfsmname", "Set FSM name and scope...", NULL, NULL, "FSMEditor.SetFSMName"},
	{"setsubfsmoverride", "Set sub-FSM override...", NULL, NULL, "FSMEditor.SetSubFSMOverride"},
	{"addstate", "Add state", NULL, NULL, "FSMEditor.AddState"},
	{"reflow", "Reflow", NULL, NULL, "FSMEditor.Reflow"},
	{"zoomin", "Zoom in", NULL, NULL, "FSMEditor.ZoomIn"},
	{"zoomout", "Zoom out", NULL, NULL, "FSMEditor.ZoomOut"},
	{"find", "Find", NULL, NULL, "FSMEditor.Find"},
	{"selectall", "Select All", NULL, NULL, "FSMEditor.SelectAll"},
};

static void fsmEditorMenuInit(void)
{
	emMenuItemCreateFromTable(&fsmEditor, fsmEditorMenuItems, ARRAY_SIZE_CHECKED(fsmEditorMenuItems));
	emMenuRegister(&fsmEditor, emMenuCreate(&fsmEditor, "File", "savefsmlayout", "setfsmname", "setsubfsmoverride", NULL));
	emMenuRegister(&fsmEditor, emMenuCreate(&fsmEditor, "Edit", "addstate", "reflow", "find", "selectall", NULL));
	emMenuRegister(&fsmEditor, emMenuCreate(&fsmEditor, "View", "zoomout", "zoomin", NULL));
}

static void fsmEditorToolbarInit(void)
{
	EMToolbar *toolbar;
	UIButton *button;

	toolbar = emToolbarCreateDefaultFileToolbar();
	eaPush(&fsmEditor.toolbars, toolbar);
	eaPush(&fsmEditor.toolbars, emToolbarCreateWindowToolbar());

	toolbar = emToolbarCreate(0);

	button = ui_ButtonCreateImageOnly("32px_Zoom_Out", 0, 0, fsmEditorToolbarZoomOutClicked, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(button), emToolbarGetHeight(toolbar), emToolbarGetHeight(toolbar));
	ui_ButtonSetImageStretch( button, true );
	emToolbarAddChild(toolbar, button, false);
	button = ui_ButtonCreateImageOnly("32px_Zoom_In", elUINextX(button), 0, fsmEditorToolbarZoomInClicked, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(button), emToolbarGetHeight(toolbar), emToolbarGetHeight(toolbar));
	ui_ButtonSetImageStretch( button, true );
	emToolbarAddChild(toolbar, button, true);

	eaPush(&fsmEditor.toolbars, toolbar);
}

static void fsmEditorStateRenameWinCreate(FSMEditorState *state)
{
	FSMEditorDoc *activeDoc = state->machine->doc;
	UIWindow *renameWin;
	UILabel *label;
	UITextEntry *entry;

	renameWin = ui_WindowCreate("Rename State", 0, 0, 175, 60);
	elUICenterWindow(renameWin);
	label = ui_LabelCreate("New Name:", 5, 5);
	ui_WindowAddChild(renameWin, label);
	entry = ui_TextEntryCreate(fsmEditorGetStateName(state), 5, 5);
	ui_TextEntrySetEnterCallback(entry, fsmEditorStateRenameOk, state);
	ui_WidgetSetDimensionsEx((UIWidget*) entry, 1, 20, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPaddingEx((UIWidget*) entry, 10 + label->widget.width, 5, 0, 0);
	ui_WindowAddChild(renameWin, entry);
	activeDoc->fsmEditorUI.modalData.entry = entry;
	elUIAddCancelOkButtons(renameWin, fsmEditorStateRenameCancel, state, fsmEditorStateRenameOk, state);
	ui_WindowSetResizable(renameWin, false);
	ui_WindowSetModal(renameWin, true);
	ui_WindowShow(renameWin);
	ui_SetFocus(entry);
	activeDoc->fsmEditorUI.modalData.window = renameWin;
}

static void fsmEditorFSMSetNameWinCreate(FSMEditorMachine *fsm)
{
	FSMEditorDoc *activeDoc = fsm->doc;
	UIWindow *renameWin;
	UILabel *label;
	UITextEntry *entry;

	renameWin = ui_WindowCreate("Rename FSM", 0, 0, 300, 60);
	elUICenterWindow(renameWin);
	label = ui_LabelCreate("New Name:", 5, 5);
	ui_WindowAddChild(renameWin, label);
	entry = ui_TextEntryCreate(fsm->name, 5, 5);
	ui_TextEntrySetEnterCallback(entry, fsmEditorFSMSetNameOk, fsm);
	ui_WidgetSetDimensionsEx((UIWidget*) entry, 1, 20, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPaddingEx((UIWidget*) entry, 10 + label->widget.width, 5, 0, 0);
	ui_WindowAddChild(renameWin, entry);
	activeDoc->fsmEditorUI.modalData.entry = entry;
	elUIAddCancelOkButtons(renameWin, fsmEditorFSMSetNameCancel, fsm, fsmEditorFSMSetNameOk, fsm);
	ui_WindowSetResizable(renameWin, false);
	ui_WindowSetModal(renameWin, true);
	ui_WindowShow(renameWin);
	ui_SetFocus(entry);
	activeDoc->fsmEditorUI.modalData.window = renameWin;
}

static void fsmEditorErrorWinClose(UIButton *button, UIWindow *win)
{
	ui_WindowClose(win);
}

static void fsmEditorErrorWinCreate(char **errors)
{
	UIWindow *win;
	UILabel *label;
	UIScrollArea *area;
	UIButton *button;
	int i;

	win = ui_WindowCreate("Errors", 0, 0, 500, 200);
	elUICenterWindow(win);
	label = ui_LabelCreate("Errors were found:", 5, 5);
	ui_WindowAddChild(win, label);
	area = ui_ScrollAreaCreate(0, 0, 1, 1, 0, 0, false, true);
	ui_WidgetSetDimensionsEx((UIWidget*) area, 1, 1, UIUnitPercentage, UIUnitPercentage);
	ui_WindowAddChild(win, area);

	// add error labels
	for (i = 0; i < eaSize(&errors); i++)
		ui_ScrollAreaAddChild(area, (UIWidget*) ui_LabelCreate(errors[i], 0, i * 15));
	ui_ScrollAreaSetSize(area, 0, i * 15);

	button = ui_ButtonCreate("Close", 5, 5, fsmEditorErrorWinClose, win);
	ui_WindowAddChild(win, button);
	button->widget.offsetFrom = UIBottomRight;
	ui_WidgetSetPaddingEx((UIWidget*) area, 5, 5, 30, elUINextY(button) + 5);

	ui_WindowSetModal(win, true);
	ui_WindowShow(win);
}

bool fsmEditorCancelFind(UIWidget *widget, UserData userData)
{
	FSMEditorDoc *doc = (FSMEditorDoc*)userData;

	ui_WidgetQueueFree((UIWidget*) doc->fsmEditorUI.modalData.window);

	return true;
}

static void fsmEditorFind(UIWidget *widget, UserData userData)
{
	FSMEditorDoc *doc = (FSMEditorDoc*)userData;
	UITextEntry *entry = doc->fsmEditorUI.modalData.entry;

	const unsigned char *pchFindText = ui_TextEntryGetText(entry);

	fsmEditorFindStringInDoc(doc, pchFindText);

	fsmEditorCancelFind(NULL, userData);
}

// select sub FSM search finished callback
static void fsmEditorFindEntered(UITextEntry *entry, UserData userData)
{
	FSMEditorDoc *doc = (FSMEditorDoc*)userData;
	const unsigned char *pchFindText = ui_TextEntryGetText(entry);

	fsmEditorFindStringInDoc(doc, pchFindText);

	fsmEditorCancelFind(NULL, userData);
}

static void fsmEditorFindTextDialog(FSMEditorDoc *doc)
{
	UIWindow *win;
	UILabel *label;
	UITextEntry *entry;

	// search window
	win = ui_WindowCreate("Find", 0, 0, 300, 100);
	elUICenterWindow(win);

	// find label
	label = ui_LabelCreate("Find What:", 5, 5);
	ui_WindowAddChild(win, label);

	// search text entry
	entry = ui_TextEntryCreate("", 5, 5);
	ui_WidgetSetDimensionsEx((UIWidget*) entry, 1, entry->widget.height, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPaddingEx((UIWidget*) entry, elUINextX(label) + 5, 5, 0, 0);
	//ui_TextEntrySetChangedCallback(entry, fsmEditorSelectSubFSMSearchChanged, doc);
	ui_TextEntrySetEnterCallback(entry, fsmEditorFindEntered, doc);
	ui_WindowAddChild(win, entry);

	// ok / cancel buttons
	elUIAddCancelOkButtons(win, fsmEditorCancelFind, doc, fsmEditorFind, doc);

	// show window
	ui_WindowSetCloseCallback(win, fsmEditorCancelFind, doc);
	ui_WindowSetModal(win, true);
	ui_WindowShow(win);
	
	ui_SetFocus(entry);

	// store pointers
	doc->fsmEditorUI.modalData.entry = entry;
	doc->fsmEditorUI.modalData.window = win;
}

static void fsmEditorSelectSubFSMCreate(FSMEditorState *state)
{
	UIWindow *win;
	UILabel *label;
	UITextEntry *entry;
	UIList *list;
	UIListColumn *col;
	FSMGroup *currGrp;
	FSM ***listModel = calloc(1, sizeof(FSM**));

	// initialize FSM list contents
	assert(currGrp = state->machine->doc->refGroup);

	*listModel = NULL;

	win = ui_WindowCreate("Select FSM", 0, 0, 600, 300);
	elUICenterWindow(win);
	label = ui_LabelCreate("FSM Name:", 5, 5);
	ui_WindowAddChild(win, label);
	entry = ui_TextEntryCreate("", 5, 5);
	ui_WidgetSetDimensionsEx((UIWidget*) entry, 1, entry->widget.height, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPaddingEx((UIWidget*) entry, elUINextX(label) + 5, 5, 0, 0);
	ui_TextEntrySetChangedCallback(entry, fsmEditorSelectSubFSMSearchChanged, state->machine->doc);
	ui_TextEntrySetEnterCallback(entry, fsmEditorSelectSubFSMSearchFinished, state);
	ui_WindowAddChild(win, entry);
	list = ui_ListCreate(NULL, listModel, 15);
	col = ui_ListColumnCreate(UIListTextCallback, "Name", (intptr_t) fsmEditorSelectSubFSMNameMakeText, NULL);
	col->fWidth = 125;
	ui_ListAppendColumn(list, col);
	ui_ListSetActivatedCallback(list, fsmEditorSelectSubFSMListActivated, state);
	ui_WidgetSetDimensionsEx((UIWidget*) list, 1, 1, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx((UIWidget*) list, 5, 5, elUINextY(entry) + 5, elUINextY(entry) + 5);
	ui_WindowAddChild(win, list);
	state->machine->doc->fsmEditorUI.modalData.list = list;
	fsmEditorSelectSubFSMSearchChanged(entry, state->machine->doc); // Force load of list now
	elUIAddCancelOkButtons(win, fsmEditorSelectSubFSMCancel, state->machine->doc, fsmEditorSelectSubFSMOk, list);
	ui_WindowSetCloseCallback(win, fsmEditorSelectSubFSMCancel, state->machine->doc);
	ui_WindowSetModal(win, true);
	ui_WindowShow(win);
	state->machine->doc->fsmEditorUI.modalData.window = win;
}

static void fsmEditorLoadFSMNames(FSMEditorDoc *doc)
{
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo("FSM");
	int i;

	eaClear(&doc->eaFSMNames);
	for(i=0; i < eaSize(&pDictInfo->ppInfos)-1; ++i)
	{
		const char *name = pDictInfo->ppInfos[i]->resourceName;
		if (pDictInfo->ppInfos[i]->resourceScope && strnicmp(doc->refGroup->name, pDictInfo->ppInfos[i]->resourceScope, strlen(doc->refGroup->name)) == 0)
		{
			eaPush(&doc->eaFSMNames, name);
		}
	}
}

static void fsmEditorSetSubFSMOverrideCreate(FSMEditorDoc *doc)
{
	UIWindow *window;
	UIButton *button;
	UILabel *fsmLabel, *stateLabel;
	UIComboBox *cb1, *cb2;
	int offset;
	const char *overrideString;

	// Check if editable
	if (!fsmEditorCanEdit(doc)) 
	{
		return;
	}

	overrideString = REF_STRING_FROM_HANDLE(doc->workingFSM.overrideStatePath);
	window = ui_WindowCreate("SubFSM Override", 0, 0, 300, 0);
	doc->fsmEditorUI.modalData.window = window;
	elUIAddCancelOkButtons(window, fsmEditorSubFSMOverrideCancel, doc, fsmEditorSubFSMOverrideOk, doc);
	fsmLabel = ui_LabelCreate("FSM", 5, 5);
	ui_WindowAddChild(window, fsmLabel);
	offset = elUINextX(fsmLabel);
	stateLabel = ui_LabelCreate("State", fsmLabel->widget.x, elUINextY(fsmLabel));
	ui_WindowAddChild(window, stateLabel);
	offset = MAX(elUINextX(stateLabel), offset);
	cb1 = ui_ComboBoxCreate(0, fsmLabel->widget.y, 1, NULL, &doc->eaFSMNames, NULL);
	ui_WidgetSetPaddingEx((UIWidget*) cb1, offset + 5, 5, 0, 0);
	cb1->widget.widthUnit = UIUnitPercentage;
	ui_WindowAddChild(window, cb1);
	doc->fsmEditorUI.modalData.cb1 = cb1;
	cb2 = ui_ComboBoxCreate(0, stateLabel->widget.y, 1, NULL, NULL, "name");
	ui_WidgetSetPaddingEx((UIWidget*) cb2, offset + 5, 5, 0, 0);
	cb2->widget.widthUnit = UIUnitPercentage;
	ui_WindowAddChild(window, cb2);
	doc->fsmEditorUI.modalData.cb2 = cb2;
	ui_ComboBoxSetSelectedCallback(cb1, fsmEditorSubFSMOverrideFSMSelected, doc);
	button = ui_ButtonCreate("Clear overrides", stateLabel->widget.x, elUINextY(stateLabel) + 5, fsmEditorSubFSMOverrideClear, cb1);
	ui_WindowAddChild(window, button);
	window->widget.height = elUINextY(button) + button->widget.height + 15;

	fsmEditorLoadFSMNames(doc);

	// load the machine's current settings
	if (overrideString && overrideString[0])
	{
		char buf[260];
		char *ptr;
		int i;

		strcpy(buf, overrideString);
		ptr = strchr(buf, ':');
		if (ptr) {
			*ptr = '\0';
			for (i=eaSize(&doc->eaFSMNames)-1; i>=0; --i)
			{
				if (stricmp(buf, doc->eaFSMNames[i]) == 0)
				{
					ui_ComboBoxSetSelectedAndCallback(cb1, i);
					break;
				}
			}
			if (i < 0)
			{
				ui_ComboBoxSetSelectedAndCallback(cb1, -1);
				ui_ComboBoxSetSelectedAndCallback(cb2, -1);
			}
		}
	}
	else
	{
		ui_ComboBoxSetSelectedAndCallback(cb1, -1);
		ui_ComboBoxSetSelectedAndCallback(cb2, -1);
	}

	elUICenterWindow(window);
	ui_WindowSetModal(window, true);
	ui_WindowShow(window);
}

static void fsmEditorNameChanged(UITextEntry *entry, FSMEditorNewDocParams *params)
{
	char *newName = NULL;
	if (fsmEditorMakeFileName(ui_TextEntryGetText(params->nameEntry), ui_TextEntryGetText(params->scopeEntry), &newName))
		ui_SetActive(UI_WIDGET(params->okButton), true);
	else
		ui_SetActive(UI_WIDGET(params->okButton), false);
	ui_LabelSetText(params->fileNameLabel, newName);
	estrDestroy(&newName);
}

static void fsmEditorCreateNameScopeWindow(char *name, char *scope, FSMEditorMachine *workingFSM, char *okText, UIActivationFunc okFunc, UIActivationFunc cancelFunc)
{
	UIWindow *window = ui_WindowCreate("Set Name and Scope", 0, 0, 0, 0);
	UILabel *label = ui_LabelCreate("FSM Name", 5, 5);
	UITextEntry *entry = ui_TextEntryCreate(name, 0, label->widget.y);
	UILabel *scopeLabel = ui_LabelCreate("FSM Scope", 5, elUINextY(label)+5);
	UITextEntry *scopeEntry = ui_TextEntryCreateWithStringCombo(scope, 0, scopeLabel->widget.y, &geaScopes, true, true, false, true);
	UILabel *fileLabel = ui_LabelCreate("FSM File", 5, elUINextY(scopeLabel)+5);
	UILabel *fileNameLabel = ui_LabelCreate("", 5, fileLabel->widget.y);
	UITextArea *instructArea = ui_TextAreaCreate("");
	FSMEditorNewDocParams *params = calloc(1, sizeof(FSMEditorNewDocParams));
	UIButton *okButton = elUIAddCancelOkButtons(window, cancelFunc, params, okFunc, params);

	UICheckButton* combatFSMCheck = ui_CheckButtonCreate(5,5,"Combat FSM",false); // allow user to make this a combat FSM

	char buf[1024];
	char *newFile = NULL;
	int i;

	// Check if editable
	if (workingFSM && !fsmEditorCanEdit(workingFSM->doc)) 
	{
		return;
	}

	params->window = window;
	params->okButton = okButton;
	params->workingFSM = workingFSM;
	if (okText)
	{
		ui_ButtonSetText(okButton, okText);
	}

	ui_WindowAddChild(window, label);
	entry->widget.width = 1;
	entry->widget.widthUnit = UIUnitPercentage;
	entry->widget.leftPad = elUINextX(label) + 5;
	entry->widget.rightPad = 5;
	ui_TextEntrySetChangedCallback(entry, fsmEditorNameChanged, params);
	ui_WindowAddChild(window, entry);
	params->nameEntry = entry;

	ui_WindowAddChild(window, scopeLabel);
	scopeEntry->widget.width = 1;
	scopeEntry->widget.widthUnit = UIUnitPercentage;
	scopeEntry->widget.leftPad = elUINextX(label) + 5;
	scopeEntry->widget.rightPad = 5;
	ui_TextEntrySetChangedCallback(scopeEntry, fsmEditorNameChanged, params);
	ui_WindowAddChild(window, scopeEntry);
	params->scopeEntry = scopeEntry;

	ui_WindowAddChild(window, fileLabel);
	fileNameLabel->widget.width = 1;
	fileNameLabel->widget.widthUnit = UIUnitPercentage;
	fileNameLabel->widget.leftPad = elUINextX(label) + 5;
	fileNameLabel->widget.rightPad = 5;
	ui_WindowAddChild(window, fileNameLabel);
	params->fileNameLabel = fileNameLabel;

	fsmEditorMakeFileName(name,scope,&newFile);
	ui_LabelSetText(fileNameLabel, newFile);
	estrDestroy(&newFile);

	ui_WidgetSetDimensionsEx(UI_WIDGET(instructArea), 1, 1, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(instructArea), 5, 5, elUINextY(fileLabel) + 5, elUINextY(okButton) + 5);
	ui_SetActive(UI_WIDGET(instructArea), false);
	ui_WindowAddChild(window, instructArea);

	sprintf(buf,"The scope must begin with the name one of these groups:\n");
	for(i=eaSize(&fsmEditorGroups.fsmGroups)-1; i>=0; --i) {
		strcat(buf, " * ");
		strcat(buf, allocAddString(fsmEditorGroups.fsmGroups[i]->name));
		strcat(buf, "\n");
	}
	ui_TextAreaSetText(instructArea, buf);

	combatFSMCheck->widget.offsetFrom = UIBottomLeft;
	combatFSMCheck->statePtr = &params->bCombatFSMRequested;
	ui_WindowAddChild(window,combatFSMCheck);

	ui_WidgetSetDimensions(UI_WIDGET(window), 400, 200);
	elUICenterWindow(window);
	ui_WindowSetModal(window, true);
	ui_WidgetSetFamily(UI_WIDGET(window), UI_FAMILY_EDITOR);
	ui_WindowShow(window);
}

static void fsmEditorSetZoomScale(FSMEditorUI *fsmUI, F32 scale)
{
	UIScrollArea *area = fsmUI->stateMachine.area;
	UIScrollbar *sb = area->widget.sb;
	F32 scrollWidth = ui_WidgetWidth(UI_WIDGET(area), fsmUI->stateMachine.window->widget.width, 1) - ui_ScrollbarWidth(sb);
	F32 scrollHeight = ui_WidgetHeight(UI_WIDGET(area), fsmUI->stateMachine.window->widget.height, 1) - ui_ScrollbarHeight(sb);
	F32 centerX, centerY;

	// we make adjustments to the position of the scrollbar to ensure that the center point
	// of the scroll area remains in the center after the zoom
	centerX = (sb->xpos + scrollWidth / 2) / area->xSize;
	centerY = (sb->ypos + scrollHeight / 2) / area->ySize;
	area->childScale = scale;
	sb->xpos = area->xSize * centerX - scrollWidth / 2;
	sb->ypos = area->ySize * centerY - scrollHeight / 2;
}

/********************************************************************
* COMMANDS
********************************************************************/
#endif

// Find
AUTO_COMMAND ACMD_CATEGORY(Interface) ACMD_NAME("FSMEditor.Find");
void fsmEditorCmdFind(void)
{
#ifndef NO_EDITORS
	FSMEditorDoc *activeDoc = (FSMEditorDoc*) emGetActiveEditorDoc();
	if (activeDoc)
	{
		fsmEditorFindTextDialog(activeDoc);
	}
#endif
}

// Select All
AUTO_COMMAND ACMD_CATEGORY(Interface) ACMD_NAME("FSMEditor.SelectAll");
void fsmEditorCmdSelectAll(void)
{
#ifndef NO_EDITORS
	FSMEditorDoc *activeDoc = (FSMEditorDoc*) emGetActiveEditorDoc();
	if (activeDoc)
	{
		fsmEditorSelectAll(NULL, activeDoc);
	}
#endif
}

// Add new state
AUTO_COMMAND ACMD_CATEGORY(Interface) ACMD_NAME("FSMEditor.AddState");
void fsmEditorCmdAddState(void)
{
#ifndef NO_EDITORS
	FSMEditorDoc *activeDoc = (FSMEditorDoc*) emGetActiveEditorDoc();
	if (activeDoc)
	{
		// Check if editable
		if (!fsmEditorCanEdit(activeDoc))
		{
			return;
		}

		fsmEditorAddState(activeDoc);
		emSetDocUnsaved(&activeDoc->doc, true);
	}
#endif
}

// Set FSM name
AUTO_COMMAND ACMD_CATEGORY(Interface) ACMD_NAME("FSMEditor.SetFSMName");
void fsmEditorCmdFSMSetName(void)
{
#ifndef NO_EDITORS
	FSMEditorDoc *activeDoc = (FSMEditorDoc*) emGetActiveEditorDoc();
	if (activeDoc)
	{
		FSMEditorMachine *workingFSM = &activeDoc->workingFSM;
		fsmEditorCreateNameScopeWindow(workingFSM->name, workingFSM->scope, workingFSM, NULL, fsmEditorSetNameOk, fsmEditorSetNameAndScopeCancel);
	}
#endif
}

// Save FSM Layout
AUTO_COMMAND ACMD_CATEGORY(Interface) ACMD_NAME("FSMEditor.SaveFSMLayout");
void fsmEditorCmdSaveLayout(void)
{
#ifndef NO_EDITORS
	FSMEditorDoc *activeDoc = (FSMEditorDoc*) emGetActiveEditorDoc();
	if (activeDoc)
	{
		activeDoc->saveFlags = SAVE_OVERWRITE | SAVE_LAYOUT;
		emQueueFunctionCallStatus(NULL, NULL, emSaveDoc, activeDoc, -1);
	}
#endif
}

// Set SubFSM override
AUTO_COMMAND ACMD_CATEGORY(Interface) ACMD_NAME("FSMEditor.SetSubFSMOverride");
void fsmEditorCmdSetSubFSMOverride(void)
{
#ifndef NO_EDITORS
	FSMEditorDoc *activeDoc = (FSMEditorDoc*) emGetActiveEditorDoc();
	if (activeDoc)
		fsmEditorSetSubFSMOverrideCreate(activeDoc);
#endif
}

// Reflow FSM states
AUTO_COMMAND ACMD_CATEGORY(Interface) ACMD_NAME("FSMEditor.Reflow");
void fsmEditorCmdReflow(void)
{
#ifndef NO_EDITORS
	FSMEditorDoc *activeDoc = (FSMEditorDoc*) emGetActiveEditorDoc();
	if (activeDoc)
	{
		fsmEditorReflow(&activeDoc->workingFSM);

		// flag change on document to save layout
		emSetDocUnsaved(&activeDoc->doc, true);
	}
#endif
}

AUTO_COMMAND ACMD_CATEGORY(Interface) ACMD_NAME("FSMEditor.ZoomIn");
void fsmEditorCmdZoomIn(void)
{
#ifndef NO_EDITORS
	FSMEditorDoc *activeDoc = (FSMEditorDoc*) emGetActiveEditorDoc();
	if (activeDoc)
	{
		F32 scale = activeDoc->fsmEditorUI.stateMachine.area->childScale;
		if (scale < 2)
			fsmEditorSetZoomScale(&activeDoc->fsmEditorUI, scale * 1.1);
	}
#endif
}

AUTO_COMMAND ACMD_CATEGORY(Interface) ACMD_NAME("FSMEditor.ZoomOut");
void fsmEditorCmdZoomOut(void)
{
#ifndef NO_EDITORS
	FSMEditorDoc *activeDoc = (FSMEditorDoc*) emGetActiveEditorDoc();
	if (activeDoc)
	{
		F32 scale = activeDoc->fsmEditorUI.stateMachine.area->childScale;
		if (scale > 0.5)
			fsmEditorSetZoomScale(&activeDoc->fsmEditorUI, scale / 1.1);
	}
#endif
}

#ifndef NO_EDITORS

/********************************************************************
* FSM GROUP INITIALIZATION AND REGISTRATION
********************************************************************/

static void fsmEditorInitPostServerInfo(UserData *unused)
{
	int i;

	if (!fsmFinishedLoading)
	{
		for (i = 0; i < eaSize(&fsmEditorGroups.fsmGroups); i++)
		{
			char *reloadPath = NULL;
			ExprContext *context;
			FSMEditorGroupLoadCallback callback;

			if (!stashFindPointer(fsmEditorGroupContexts, fsmEditorGroups.fsmGroups[i]->name, &context))
			{
				context = exprContextCreate();
				exprContextSetPointerVar(context, "Context", context, parse_ExprContext, false, true);
				stashAddPointer(fsmEditorGroupContexts, fsmEditorGroups.fsmGroups[i]->name, context, false);
			}

			// call any specified group on-load callback
#pragma warning(suppress:4054) // 'type cast' : from function pointer 'FSMEditorGroupLoadCallback' to data pointer 'void *'
			if (stashFindPointer(fsmEditorGroupCallbacks, fsmEditorGroups.fsmGroups[i]->name, &((void*) callback)))
				callback(fsmEditorGroups.fsmGroups[i]->dir, fsmEditorGroups.fsmGroups[i]->name, context);
		}
		fsmFinishedLoading = 1;

		// clean up callback stash
		stashTableDestroy(fsmEditorGroupCallbacks);
	}
}

static bool fsmEditorWaitForExprEdInit(UserData *unused)
{
	return exprEdIsInitialized();
}

/******
* This function registers a group of FSM's to be modifiable with the FSM Editor.
* PARAMS:
*   path - relative path to the folder where the group resides
*   uniqueName - name to give to the group
******/
void fsmEditorRegisterGroupEx(const char *path, const char *uniqueName, ExprContext *clientContext, FSMEditorGroupLoadCallback onLoadF)
{
	FSMGroup *newGroup;
	int i;

	// return if uniqueName is already being used
	for (i = 0; i < eaSize(&fsmEditorGroups.fsmGroups); i++)
		if (strcmpi(fsmEditorGroups.fsmGroups[i]->name, uniqueName) == 0)
			return;

	// TODO: get contexts from the server somehow and associate with the group
	newGroup = StructCreate(parse_FSMGroup);
	newGroup->dir = StructAllocString(path);
	newGroup->name = StructAllocString(uniqueName);
	eaPush(&fsmEditorGroups.fsmGroups, newGroup);

	if (clientContext)
	{
		if (!fsmEditorGroupContexts)
			fsmEditorGroupContexts = stashTableCreateWithStringKeys(16, StashDefault);
		stashAddPointer(fsmEditorGroupContexts, newGroup->name, clientContext, false);
	}

	if (onLoadF)
	{
		if (!fsmEditorGroupCallbacks)
			fsmEditorGroupCallbacks = stashTableCreateWithStringKeys(16, StashDefault);
		stashAddPointer(fsmEditorGroupCallbacks, newGroup->name, onLoadF, false);
	}
}

/******
* returns a registered context given the name it was registered with
******/
ExprContext* fsmEditorFindContextByName(SA_PARAM_NN_STR const char *name)
{
	if (fsmEditorGroupContexts)
	{
		ExprContext* context = NULL;
		stashFindPointer(fsmEditorGroupContexts, name, &context);
		return context;
	}

	return NULL;
}


/******
* This function registers the default groups to be used with the FSM Editor.
******/
#endif
AUTO_RUN_EARLY;
void fsmEditorRegisterKnownGroups(void)
{
}
#ifndef NO_EDITORS


/*********************
* CLIPBOARD REGISTRATION
*********************/
static void fsmEditorGetStateDesc(FSMState *state, char **output)
{
	estrPrintf(output, "%s", state->name);
}

#define MAX_STATES_TO_LIST (5)

static void fsmEditorGetStatesDesc(FSMStates *states, char **output)
{
	int i = 0;
	estrPrintf(output, "(");

	FOR_EACH_IN_EARRAY(states->ppStates, FSMState, pState)
	{
		estrConcatf(output, " %s", pState->name);
		i++;
		if(i > MAX_STATES_TO_LIST)
		{
			break;
		}
	}
	FOR_EACH_END

	if(eaSize(&states->ppStates) > MAX_STATES_TO_LIST)
	{
		estrConcatf(output, " ... )");
	}
	else
	{
		estrConcatf(output, " )");
	}
}

#endif
AUTO_RUN;
void fsmEditorClipboardInit(void)
{
#ifndef NO_EDITORS
	ecbRegisterClipType(ECB_STATE, fsmEditorGetStateDesc, parse_FSMState);
	ecbRegisterClipType(ECB_STATES, fsmEditorGetStatesDesc, parse_FSMStates);
#endif
}
#ifndef NO_EDITORS
/*********************
* EDITOR MANAGER INTEGRATION
*********************/
static float fsmEditorPreInit(void *unused)
{
	static bool setup_called = false;
	static float num = 0;

	if (!setup_called)
	{
		if (!exprEdIsInitialized())
			exprEdInit();
		setup_called = true;
	}

	if (exprEdIsInitialized())
	{
		fsmEditorInitPostServerInfo(NULL);
		return 1;
	}
	else
		return 0.5;
}

static void fsmEditorUpdateAfterDictChange(void *unused)
{
	int i, j;

	s_FSMDictChanged = false;

	for( i = 0; i < eaSize(&fsmEditor.open_docs); ++i) 
	{
		FSMEditorDoc *doc = (FSMEditorDoc*)fsmEditor.open_docs[i];

		// Update name list used in UI so it always has proper FSMs
		fsmEditorLoadFSMNames(doc);
		
		// Update sub-FSM entries on each state
		fsmEditorRefreshSubFsms(doc);

		// Update override
		fsmEditorRefreshOverride(doc);

		// Check if the FSM changed on disk
		for(j=eaSize(&s_FSMsThatChanged)-1; j>=0; --j)
		{
			if (doc->origName && (stricmp(s_FSMsThatChanged[j], doc->origName) == 0))
			{
				if (!doc->doc.saved)
				{
					// Prompt for revert or continue
					emQueuePrompt(EMPROMPT_REVERT_CONTINUE, &doc->doc, NULL, gFSMDict, doc->origName);
				}
				else
				{
					// Queue up a reload so we get the data from disk
					emQueueFunctionCall(emReloadDoc,&doc->doc);
				}
			}
		}
	}

	// Clean up
	eaDestroy(&s_FSMsThatChanged);
}

static void fsmEditorUpdateAfterIndexChange(void *unused)
{
	ResourceInfo *info;
	FSMEditorDoc *doc;
	int i;

	s_FSMIndexChanged = false;
	resGetUniqueScopes(gFSMDict, &geaScopes);

	for( i = 0; i < eaSize(&fsmEditor.open_docs); ++i) 
	{
		doc = (FSMEditorDoc*)fsmEditor.open_docs[i];
		if (!doc->doc.saved && doc->origName)
		{
			info = resGetInfo(gFSMDict, doc->origName);
			if (info && !resIsWritable(info->resourceDict, info->resourceName))
			{
				emQueuePrompt(EMPROMPT_CHECKOUT_REVERT, &doc->doc, NULL, gFSMDict, doc->origName);
			}
		}
	}
}

static void fsmDictChanged(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData)
{
	const char *name = (const char *)pRefData;

	if (!name) {
		return;
	}

	if ((eType == RESEVENT_RESOURCE_MODIFIED) ||
		(eType == RESEVENT_RESOURCE_REMOVED) ||
		(eType == RESEVENT_RESOURCE_ADDED)) 
	{
		if (!s_FSMDictChanged)
		{
			s_FSMDictChanged = true;
			emQueueFunctionCall(fsmEditorUpdateAfterDictChange, NULL);
		}
		eaPushUnique(&s_FSMsThatChanged, (char*)allocAddString(name));
	}
	else if (eType == RESEVENT_INDEX_MODIFIED)
	{
		if (!s_FSMIndexChanged)
		{
			s_FSMIndexChanged = true;
			emQueueFunctionCall(fsmEditorUpdateAfterIndexChange, NULL);
		}
	}	
}


static void fsmEditorInit(EMEditor *editor)
{
	fsmEditorMenuInit();
	fsmEditorToolbarInit();

	resGetUniqueScopes(gFSMDict, &geaScopes);

	emAddDictionaryStateChangeHandler(&fsmEditor, "fsm", NULL, NULL, fsmEditorSaveDocContinue, fsmEditorDeleteContinue, NULL);

	resDictRegisterEventCallback(gFSMDict, fsmDictChanged, NULL);
}

static void fsmEditorEnter(EMEditor *editor)
{
	resSetDictionaryEditMode(gFSMDict, true);
}

static void fsmEditorExit(EMEditor *editor)
{

}

static void fsmEditorLoadExpression(FSMEditorExpr *edExpr, Expression *expr)
{
	if (!edExpr || !expr)
		return;

	exprDestroy(edExpr->expr);
	edExpr->expr = exprCreate();
	exprCopy(edExpr->expr, expr);
	fsmEditorRefreshExprUI(edExpr);
}

static void fsmEditorLoadTrans(FSMEditorState *startState, FSMEditorState *endState, FSMTransition *trans)
{
	FSMEditorTransButton *startButton = fsmEditorConnectStates(startState, endState);

	// update condition, action expressions
	fsmEditorLoadExpression(startButton->condExpr, trans->expr);
	fsmEditorLoadExpression(startButton->actionExpr, trans->action);

	if (trans->layout)
	{
		ui_ExpanderSetOpened(startButton->expander, trans->layout->open);

		startButton->layout = StructClone(parse_FSMTransitionLayout, trans->layout);
	}
}

static void fsmEditorSetCommentVisible(FSMEditorState *edState, bool bVisible)
{
	edState->bIsCommentVisible = bVisible;

	if(bVisible)
	{
		// reveal
		edState->comment->widget.y = 0;
		edState->stateExpGrp->widget.y = 25;

		ui_WidgetSetDimensions((UIWidget*) &edState->stateWin->window, FSME_STATE_WIDTH, edState->stateWin->window.widget.height + 20); // resize window
	}
	else
	{
		// hide comment textarea (by moving it off screen - hack)
		edState->comment->widget.y = -25;
		edState->stateExpGrp->widget.y = 0;

		//ui_WidgetSetDimensions((UIWidget*) &edState->stateWin->window, FSME_STATE_WIDTH, edState->stateWin->window.widget.height - 20); // resize window
	}
}

static bool fsmEditorLoadState(FSMEditorState *edState, FSMState *state)
{
	const char *fsmName;

	// set state name
	fsmEditorStateSetName(edState, state->name);

	// set state comment
	if (state->comment)
	{
		ui_TextAreaSetText(edState->comment, state->comment);

		fsmEditorSetCommentVisible(edState, true);
	}
	else
	{
		fsmEditorSetCommentVisible(edState, false);
	}

	// set state expressions
	fsmEditorLoadExpression(edState->onEntryExpr, state->onEntry);
	fsmEditorLoadExpression(edState->onEntryFExpr, state->onFirstEntry);
	fsmEditorLoadExpression(edState->actionExpr, state->action);

	// set sub FSM
	fsmName = REF_STRING_FROM_HANDLE(state->subFSM);
	if (fsmName)
		fsmEditorStateSetSubFSM(edState, fsmName);
	else
		fsmEditorStateSetSubFSM(edState, NULL);

	if (state->layout)
	{
		edState->stateWin->window.widget.x = state->layout->x;
		edState->stateWin->window.widget.y = state->layout->y;
		ui_ExpanderSetOpened(edState->onEntryFExpr->expander, state->layout->onFirstEntryOpen);
		ui_ExpanderSetOpened(edState->onEntryExpr->expander, state->layout->onEntryOpen);
		ui_ExpanderSetOpened(edState->actionExpr->expander, state->layout->actionOpen);
		ui_ExpanderSetOpened(edState->subFSMExp, state->layout->subFSMOpen);

		// save layout to the state so it can be saved out or reloaded as necessary
		edState->layout = StructClone(parse_FSMStateLayout, state->layout);

		return true;
	}
	else
		return false;
}

static void fsmEditorLoad(FSMEditorDoc *activeDoc, FSM *fsm)
{
	bool foundPref = false;
	int i, j;

	// set name
	fsmEditorFSMSetName(activeDoc, fsm->name);
	fsmEditorFSMSetScope(activeDoc, fsm->scope);

	// set comment
	if (fsm->comment)
		ui_TextAreaSetText(activeDoc->fsmEditorUI.fsmCommentArea, fsm->comment);

	// get override data
	if (eaSize(&fsm->overrides) > 0)
	{
		REMOVE_HANDLE(activeDoc->workingFSM.overrideFSM);
		SET_HANDLE_FROM_REFDATA(gFSMDict, fsm->overrides[0]->subFSMOverride, activeDoc->workingFSM.overrideFSM);
		REMOVE_HANDLE(activeDoc->workingFSM.overrideStatePath);
		SET_HANDLE_FROM_REFDATA("FSMStateDict", fsm->overrides[0]->statePath, activeDoc->workingFSM.overrideStatePath);
	}

	// add states
	for (i = 0; i < eaSize(&fsm->states); i++)
	{
		FSMEditorState *edState = fsmEditorAddState(activeDoc);
		FSMState *state = fsm->states[i];

		foundPref = (fsmEditorLoadState(edState, state) || foundPref);
	}

	// create transitions
	for (i = 0; i < eaSize(&fsm->states); i++)
	{
		FSMState *state = fsm->states[i];
		for (j = 0; j < eaSize(&state->transitions); j++)
		{
			FSMTransition *trans = state->transitions[j];
			FSMEditorState *startState = fsmEditorGetStateByName(activeDoc, state->name);
			FSMEditorState *endState = fsmEditorGetStateByName(activeDoc, trans->targetName);

			fsmEditorLoadTrans(startState, endState, trans);
		}
	}

	if (!foundPref)
		fsmEditorReflow(&activeDoc->workingFSM);
}

/******
* This function cleans up all of the UI and data associated with a document when it is closed.
* PARAMS:
*   doc - EMEditorDoc to close
******/
static void fsmEditorCloseDoc(EMEditorDoc *doc)
{
	FSMEditorDoc *activeDoc = (FSMEditorDoc*) doc;

	if (!activeDoc->startPath[0])
		doc->saved = true;
	fsmEditorDocPrefsSave(activeDoc);
	eaDestroyEx(&activeDoc->workingFSM.states, fsmEditorStateFree);
	ui_WidgetQueueFree((UIWidget*) activeDoc->fsmEditorUI.stateMachine.window);
	eaDestroyEx(&activeDoc->doc.em_panels, emPanelFree);
	ecbSave();
	REMOVE_HANDLE(activeDoc->refFSM);
	REMOVE_HANDLE(activeDoc->workingFSM.overrideFSM);
	REMOVE_HANDLE(activeDoc->workingFSM.overrideStatePath);	
	REMOVE_HANDLE(activeDoc->fsmEditorUI.modalData.fsmHandle);
	SAFE_FREE(activeDoc);
}

static void fsmEditorCustomNewDoc(void)
{
	fsmEditorCreateNameScopeWindow("", "AI", NULL, NULL, fsmEditorCustomNewOk, fsmEditorSetNameAndScopeCancel);
}

/******
* This function is called to create new FSM's via the emNewDoc API.  Parameters must be passed
* in to create the new FSM in a specific group.
* PARAMS:
*   type - EditorManager type of the doc being created
*   params - FSMEditorNewDocParams indicating where the new FSM will be created
* RETURNS:
*   EMEditorDoc corresponding to the new FSM
******/
static EMEditorDoc *fsmEditorNewDoc(const char *type, FSMEditorNewDocParams *params)
{
	FSMEditorDoc *newDoc = calloc(1, sizeof(FSMEditorDoc));

	assert(strcmpi(type, "fsm") == 0 && params);

	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	// initialize the working state machine
	newDoc->workingFSM.startState = NULL;
	newDoc->workingFSM.states = NULL;
	newDoc->workingFSM.doc = newDoc;
	fsmEditorUIInit(newDoc);

	// load new FSM
	strcpy(newDoc->doc.doc_display_name, "New State Machine");
	fsmEditorFSMSetName(newDoc, ui_TextEntryGetText(params->nameEntry));
	fsmEditorFSMSetScope(newDoc, ui_TextEntryGetText(params->scopeEntry));
	newDoc->refGroup = fsmEditorGetGroupFromScope(newDoc->workingFSM.scope);

	fileRelativePath(newDoc->refGroup->dir, newDoc->startPath);
	if (!strEndsWith(newDoc->startPath, "/"))
		strcat(newDoc->startPath, "/");	
	fsmEditorAddState(newDoc);

	// NOTE: the following is a group-type-specific default
	assert(newDoc->refGroup);
	if (strcmpi(newDoc->refGroup->name, "AI") == 0)
	{
		if (!params->bCombatFSMRequested) {
			REMOVE_HANDLE(newDoc->workingFSM.overrideStatePath);
			SET_HANDLE_FROM_REFDATA("FSMStateDict", "Combat:Ambient", newDoc->workingFSM.overrideStatePath);
		}
	}

	return &newDoc->doc;
}

static void fsmEditorLoadInternal(FSMEditorDoc *newDoc, const char *name)
{
}

static EMEditorDoc *fsmEditorLoadDoc(const char *name, const char *type)
{
	FSMEditorDoc *newDoc = calloc(1, sizeof(FSMEditorDoc));
	FSM *refFSM;

	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	if (strcmpi(type, "fsm") != 0)
	{
		Errorf("Invalid type \"%s\" cannot be opened in State Machine Editor.", type);
		return NULL;
	}

	// initialize the working state machine
	newDoc->workingFSM.startState = NULL;
	newDoc->workingFSM.states = NULL;
	newDoc->workingFSM.doc = newDoc;
	fsmEditorUIInit(newDoc);

	// load existing FSM
	strcpy(newDoc->doc.doc_display_name, name);
	REMOVE_HANDLE(newDoc->refFSM);
	SET_HANDLE_FROM_REFDATA(gFSMDict, name, newDoc->refFSM);
	refFSM = GET_REF(newDoc->refFSM);
	if (!refFSM || !resIsEditingVersionAvailable(gFSMDict, name))
	{
		fsmEditorCloseDoc((EMEditorDoc*) newDoc);
		// Wait for object to show up so we can open it
		emSetResourceState(&fsmEditor, name, EMRES_STATE_OPENING);
		resRequestOpenResource(gFSMDict, name);
		return NULL;
	}
	emDocAssocFile((EMEditorDoc*) newDoc, refFSM->fileName);
	fsmEditorLoad(newDoc, refFSM);
	newDoc->refGroup = fsmEditorGetGroupForFSM(refFSM);
	newDoc->origName = StructAllocString(refFSM->name);
	newDoc->origFile = StructAllocString(refFSM->fileName);

	fsmEditorDocPrefsApply(newDoc, false);

	fsmEditorUpdateCableColors(newDoc);

	return &newDoc->doc;
}

static void fsmEditorReloadDoc(EMEditorDoc *doc)
{
	FSMEditorDoc *activeDoc = (FSMEditorDoc*)doc;
	char *name;

	if (!activeDoc->origName)
	{
		Alertf("Cannot re-open a document that was never saved.");
		return;
	}

	// Close then re-open
	name = strdup(activeDoc->origName);
	emForceCloseDoc(doc);
	emOpenFileEx(name, "fsm");
	free(name);
}

static void fsmEditorPopulateFSMState(FSMEditorState *state, FSMState *newState, bool saveLayout)
{
	FSMTransition *trans;
	const char *comment;
	int i;

	// state name
	newState->name = (char*)allocAddString(fsmEditorGetStateName(state));

	// comment
	comment = ui_TextAreaGetText(state->comment);
	if (comment && comment[0])
		newState->comment = StructAllocString(comment);

	// populate state expressions
	if (state->onEntryExpr->expr && eaSize(&state->onEntryExpr->expr->lines) > 0)
	{
		newState->onEntry = exprCreate();
		exprCopy(newState->onEntry, state->onEntryExpr->expr);
	}
	if (state->onEntryFExpr->expr && eaSize(&state->onEntryFExpr->expr->lines) > 0)
	{
		newState->onFirstEntry = exprCreate();
		exprCopy(newState->onFirstEntry, state->onEntryFExpr->expr);
	}

	// if a sub-FSM is set, do not populate the action expression
	if (REF_STRING_FROM_HANDLE(state->subFSM))
		COPY_HANDLE(newState->subFSM, state->subFSM)
	else
	{
		if (state->actionExpr->expr && eaSize(&state->actionExpr->expr->lines) > 0)
		{
			newState->action = exprCreate();
			exprCopy(newState->action, state->actionExpr->expr);
		}
	}

	if (saveLayout)
	{
		newState->layout = StructCreate(parse_FSMStateLayout);
		newState->layout->x = state->stateWin->window.widget.x;
		newState->layout->y = state->stateWin->window.widget.y;
		newState->layout->onEntryOpen = ui_ExpanderIsOpened(state->onEntryExpr->expander);
		newState->layout->onFirstEntryOpen = ui_ExpanderIsOpened(state->onEntryFExpr->expander);
		newState->layout->subFSMOpen = ui_ExpanderIsOpened(state->subFSMExp);
		newState->layout->actionOpen = ui_ExpanderIsOpened(state->actionExpr->expander);

		// destroy old layout on editor state and overwrite it with the new one
		StructDestroy(parse_FSMStateLayout, state->layout);
		state->layout = StructClone(parse_FSMStateLayout, newState->layout);
	}
	// if not overwriting layout, use layout that was loaded from the file
	else
		newState->layout = StructClone(parse_FSMStateLayout, state->layout);

	// populate state transitions
	for (i = 0; i < eaSize(&state->connsOut); i++)
	{
		FSMEditorTransButton *button = state->connsOut[i];
		trans = StructCreate(parse_FSMTransition);

		// expressions
		trans->expr = exprCreate();
		exprCopy(trans->expr, button->condExpr->expr);
		if (button->actionExpr->expr && eaSize(&button->actionExpr->expr->lines) > 0)
		{
			trans->action = exprCreate();
			exprCopy(trans->action, button->actionExpr->expr);
		}
		if (button->pair)
			trans->targetName = (char*)allocAddString(fsmEditorGetStateName(button->pair->state));

		if (saveLayout)
		{
			trans->layout = StructCreate(parse_FSMTransitionLayout);
			trans->layout->open = ui_ExpanderIsOpened(button->expander);

			// destroy old layout on editor state and overwrite with the new one
			StructDestroy(parse_FSMTransitionLayout, button->layout);
			button->layout = StructClone(parse_FSMTransitionLayout, trans->layout);
		}
		else
			trans->layout = StructClone(parse_FSMTransitionLayout, button->layout);

		eaPush(&newState->transitions, trans);
	}
}

static void fsmEditorPopulateFSM(FSMEditorMachine *edFSM, FSM *fsm, bool saveLayout)
{
	const char *overrideFsmName;
	const char *overrideStateName;
	const char *comment;
	FSMState *newState;
	FSM *refFSM;
	int i;

	if (!edFSM || !fsm)
		return;

	// populate FSM-specific data
	overrideFsmName = REF_STRING_FROM_HANDLE(edFSM->overrideFSM);
	overrideStateName = REF_STRING_FROM_HANDLE(edFSM->overrideStatePath);
	if (overrideFsmName)
	{
		fsm->onLoadStartState = (char*)allocAddString(overrideStateName);
		eaPush(&fsm->overrides, StructCreate(parse_FSMOverrideMapping));
		fsm->overrides[0]->statePath = (char*)allocAddString(overrideStateName);
		if (overrideFsmName)
			fsm->overrides[0]->subFSMOverride = (char*)allocAddString(overrideFsmName);
		else
			fsm->overrides[0]->subFSMOverride = (char*)allocAddString(fsm->name);
	}

	// populate any additional overrides from the reference FSM
	refFSM = GET_REF(edFSM->doc->refFSM);
	if (refFSM && eaSize(&fsm->overrides) == 1)
	{
		for (i = 1; i < eaSize(&refFSM->overrides); i++)
		{
			eaPush(&fsm->overrides, StructCreate(parse_FSMOverrideMapping));
			StructCopyFields(parse_FSMOverrideMapping, refFSM->overrides[i], fsm->overrides[i], 0, 0);
		}
	}

	// populate comment field
	comment = ui_TextAreaGetText(edFSM->doc->fsmEditorUI.fsmCommentArea);
	if (comment && comment[0])
		fsm->comment = StructAllocString(comment);

	for (i = 0; i < eaSize(&edFSM->states); i++)
	{
		FSMEditorState *state = edFSM->states[i];
		newState = StructCreate(parse_FSMState);

		// populate the new state
		fsmEditorPopulateFSMState(state, newState, saveLayout);
		eaPush(&fsm->states, newState);
	}
}

static void fsmEditorSaveDocAsOk(UIButton *button, FSMEditorNewDocParams *params)
{
	FSMEditorDoc *activeDoc = (FSMEditorDoc*) emGetActiveEditorDoc();

	assert(activeDoc);

	// Set name and scope and close window
	fsmEditorSetNameOk(button, params);

	activeDoc->startPath[0] = '\0';
	activeDoc->saveFlags = SAVE_AS_NEW;
	fsmEditorDocPrefsSave(activeDoc);
	emQueueFunctionCallStatus(NULL, NULL, emSaveDoc, activeDoc, -1);	
}

static EMTaskStatus fsmEditorSaveDocAs(EMEditorDoc *doc)
{
	FSMEditorDoc *activeDoc = (FSMEditorDoc*) doc;

	fsmEditorCreateNameScopeWindow(activeDoc->workingFSM.name, activeDoc->workingFSM.scope, &activeDoc->workingFSM, "Save", fsmEditorSaveDocAsOk, fsmEditorSetNameAndScopeCancel);

	return EM_TASK_FAILED;
}

static EMTaskStatus fsmEditorAutosaveDoc(EMEditorDoc *doc)
{
	FSMEditorDoc *activeDoc = (FSMEditorDoc*) doc;
	FSM *outFsm = StructCreate(parse_FSM);
	char backupName[MAX_PATH];
	FSMSaveHolder *saveHolder;

	// create override state path reference if it doesn't exist
	if (!activeDoc->startPath[0] && !REF_STRING_FROM_HANDLE(activeDoc->workingFSM.overrideFSM) && REF_STRING_FROM_HANDLE(activeDoc->workingFSM.overrideStatePath))
	{
		REMOVE_HANDLE(activeDoc->workingFSM.overrideFSM);
		SET_HANDLE_FROM_REFDATA(gFSMDict, activeDoc->workingFSM.name, activeDoc->workingFSM.overrideFSM);
	}

	// populate an FSM
	if (activeDoc->startPath[0])
		outFsm->name = (char*)allocAddString("NewFSM");
	else
		outFsm->name = (char*)allocAddString(activeDoc->workingFSM.name);
	outFsm->states = NULL;
	if (activeDoc->workingFSM.startState)
		outFsm->myStartState = (char*)allocAddString(fsmEditorGetStateName(activeDoc->workingFSM.startState));
	fsmEditorPopulateFSM(&activeDoc->workingFSM, outFsm, false);

	// populate a state machine struct
	if (activeDoc->startPath[0])
		sprintf(backupName, "%s/editor/FSMEditor.autosave", fileLocalDataDir());
	else
	{
		EMFile **files = NULL;
		emDocGetFiles(&activeDoc->doc, &files, false);
		if (eaSize(&files) > 0)
			sprintf(backupName, "%s.autosave", files[0]->filename);
		eaDestroy(&files);
	}

	// write file out with text parser
	saveHolder = StructCreate(parse_FSMSaveHolder);
	saveHolder->fsm = outFsm;
	ParserWriteTextFile(backupName, parse_FSMSaveHolder, saveHolder, 0, 0);
	saveHolder->fsm = NULL;
	StructDestroy(parse_FSMSaveHolder, saveHolder);

	return EM_TASK_SUCCEEDED;
}

static void fsmEditorUIDismissWindow(FSMEditorDoc *activeDoc)
{
	// Free the window
	ui_WindowHide(activeDoc->fsmEditorUI.modalData.window);
	ui_WidgetQueueFree(UI_WIDGET(activeDoc->fsmEditorUI.modalData.window));
	activeDoc->fsmEditorUI.modalData.window = NULL;
}

static void fsmEditorUICancelSave(UIButton *pButton, FSMEditorDoc *activeDoc)
{
	fsmEditorUIDismissWindow(activeDoc);

	// Clear flags
	activeDoc->saveFlags = 0;
}

static void fsmEditorUISaveAsNew(UIButton *pButton, FSMEditorDoc *activeDoc)
{
	fsmEditorUIDismissWindow(activeDoc);

	activeDoc->saveFlags = SAVE_AS_NEW | SAVE_OVERWRITE;
	emSaveDoc(&activeDoc->doc);
}


static void fsmEditorUISaveRename(UIButton *pButton, FSMEditorDoc *activeDoc)
{
	fsmEditorUIDismissWindow(activeDoc);

	activeDoc->saveFlags = SAVE_OVERWRITE;
	emSaveDoc(&activeDoc->doc);
}


static void fsmEditorPromptForSave(FSMEditorDoc *activeDoc, bool bNameCollision, bool bNameChanged)
{
	UIWindow *pWindow;
	UILabel *pLabel;
	UIButton *pButton;
	char buf[1024];
	int y = 0;
	int width = 0;
	int x = 0;

	pWindow = ui_WindowCreate("Confirm Save?", 200, 200, 300, 60);

	if (bNameChanged) 
	{
		sprintf(buf, "The FSM name was changed to a new name.  Did you want to rename or save as new?");
		pLabel = ui_LabelCreate(buf, 0, y);
		ui_WindowAddChild(pWindow, pLabel);
		width = MAX(width, pLabel->widget.width + 20);
		y += 28;
	}

	if (bNameCollision) 
	{
		sprintf(buf, "The FSM name '%s' is already in use.  Did you want to overwrite it?", activeDoc->workingFSM.name);
		pLabel = ui_LabelCreate(buf, 0, y);
		ui_WindowAddChild(pWindow, pLabel);
		width = MAX(width, pLabel->widget.width + 20);
		y += 28;
	}

	if (bNameChanged) 
	{
		if (bNameCollision) 
		{
			pButton = ui_ButtonCreate("Save As New AND Overwrite", 0, 28, fsmEditorUISaveAsNew, activeDoc);
			ui_WidgetSetWidth(UI_WIDGET(pButton), 200);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), -260, y, 0.5, 0, UITopLeft);
			ui_WindowAddChild(pWindow, pButton);

			pButton = ui_ButtonCreate("Rename AND Overwrite", 0, 28, fsmEditorUISaveRename, activeDoc);
			ui_WidgetSetWidth(UI_WIDGET(pButton), 200);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), -50, y, 0.5, 0, UITopLeft);
			ui_WindowAddChild(pWindow, pButton);

			x = 160;
			width = MAX(width, 540);
		} 
		else 
		{
			pButton = ui_ButtonCreate("Save As New", 0, 0, fsmEditorUISaveAsNew, activeDoc);
			ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), -160, y, 0.5, 0, UITopLeft);
			ui_WindowAddChild(pWindow, pButton);

			pButton = ui_ButtonCreate("Rename", 0, 0, fsmEditorUISaveRename, activeDoc);
			ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), -50, y, 0.5, 0, UITopLeft);
			ui_WindowAddChild(pWindow, pButton);

			x = 60;
			width = MAX(width, 340);
		}
	} 
	else 
	{
		pButton = ui_ButtonCreate("Overwrite", 0, 0, fsmEditorUISaveAsNew, activeDoc);
		ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), -105, y, 0.5, 0, UITopLeft);
		ui_WindowAddChild(pWindow, pButton);

		x = 5;
		width = MAX(width, 230);
	}

	pButton = ui_ButtonCreate("Cancel", 0, 0, fsmEditorUICancelSave, activeDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.5, 0, UITopLeft);
	ui_WindowAddChild(pWindow, pButton);

	y += 28;

	pWindow->widget.width = width;
	pWindow->widget.height = y;

	activeDoc->fsmEditorUI.modalData.window = pWindow;

	ui_WindowSetClosable(pWindow, false);
	ui_WindowSetModal(pWindow, true);
	ui_WindowPresent(pWindow);
}


static bool fsmEditorDeleteContinue(EMEditor *editor, const char *name, void *data, EMResourceState eState, void *data2, bool success)
{
	if (success && (eState == EMRES_STATE_LOCK_SUCCEEDED)) {
		// Since we got the lock, continue by doing the delete save
		emSetResourceStateWithData(&fsmEditor, name, EMRES_STATE_DELETING, NULL);
		resRequestSaveResource(gFSMDict, name, NULL);
	}

	return true;
}

static bool fsmEditorSaveDocContinue(EMEditor *editor, const char *name, void *data, EMResourceState eState, FSMEditorDoc *activeDoc, bool success)
{
	if (success && (eState == EMRES_STATE_SAVE_SUCCEEDED))
	{
		int i;
		for(i = eaSize(&editor->open_docs)-1; i>=0; --i)
		{
			if (stricmp(editor->open_docs[i]->doc_name, name) == 0)
			{
				editor->open_docs[i]->saved = true;
				break;
			}
		}
	}
	return true;
}

static EMTaskStatus fsmEditorSaveDoc(EMEditorDoc *doc)
{
	ResourceInfo *info;
	FSMEditorDoc *activeDoc = (FSMEditorDoc*) doc;
	FSM *outFsm;
	EMFile **files = NULL;
	EMTaskStatus status;
	char *name;
	char fileName[260];

	// skip straight to saving preferences if the FSM data is not editable (i.e. only layout was changed)
	// and not writing layout to file
	if (!(activeDoc->saveFlags & SAVE_LAYOUT))
	{
		info = resGetInfo(gFSMDict, activeDoc->origName);
		if (info && !resIsWritable(info->resourceDict, info->resourceName))
		{
			fsmEditorDocPrefsSave(activeDoc);
			return EM_TASK_SUCCEEDED;
		}
	}

	// Deal with state changes
	name = activeDoc->workingFSM.name;
	if (emHandleSaveResourceState(activeDoc->doc.editor, name, &status)) 
	{
		if (status != EM_TASK_INPROGRESS)
		{
			activeDoc->saveFlags = 0;
		}
		return status;
	}

	// validate the FSM
	if (!fsmEditorFSMValidate(&activeDoc->workingFSM))
	{
		activeDoc->saveFlags = 0;
		return EM_TASK_FAILED;
	}

	// force user to specify a filename for new files
	if (!activeDoc->saveFlags && (activeDoc->startPath[0] || !eaSize(&activeDoc->doc.files) || !activeDoc->doc.files[0]->file))
	{
		activeDoc->saveFlags = 0;
		fsmEditorSaveDocAs(doc);
		return EM_TASK_FAILED;
	}

	// Check for confirmation on collision or rename
	if (((activeDoc->saveFlags & SAVE_AS_NEW) != 0) && resGetInfo("FSM", activeDoc->workingFSM.name) && 
		(!activeDoc->origName || (stricmp(activeDoc->origName, activeDoc->workingFSM.name) != 0))) 
	{
		// Saving as new to a name that is already existing
		fsmEditorPromptForSave(activeDoc, true, false);
		activeDoc->saveFlags = 0;
		return EM_TASK_FAILED;
	} 
	else if (!activeDoc->saveFlags && activeDoc->origName && (stricmp(activeDoc->origName, activeDoc->workingFSM.name) != 0)) 
	{
		// Name changed and not doing a save-as, so prompt for rename confirm
		fsmEditorPromptForSave(activeDoc, (resGetInfo("FSM", activeDoc->workingFSM.name) != NULL), true);
		activeDoc->saveFlags = 0;
		return EM_TASK_FAILED;
	}

	fileRelativePath(activeDoc->doc.files[0]->file->filename, fileName);

	// create override state path reference to current FSM
	REMOVE_HANDLE(activeDoc->workingFSM.overrideFSM);
	if (REF_STRING_FROM_HANDLE(activeDoc->workingFSM.overrideStatePath))
	{
		SET_HANDLE_FROM_REFDATA(gFSMDict, activeDoc->workingFSM.name, activeDoc->workingFSM.overrideFSM);
	}

	// populate a state machine struct
	outFsm = StructCreate(parse_FSM);
	outFsm->name = (char*)allocAddString(activeDoc->workingFSM.name);
	outFsm->states = NULL;
	outFsm->myStartState = (char*)allocAddString(fsmEditorGetStateName(activeDoc->workingFSM.startState));
	outFsm->fileName = allocAddFilename(fileName);
	outFsm->group = (char*)allocAddString(activeDoc->refGroup->name);
	outFsm->scope = (char*)allocAddString(activeDoc->workingFSM.scope);
	fsmEditorPopulateFSM(&activeDoc->workingFSM, outFsm, (activeDoc->saveFlags & SAVE_LAYOUT));

	resSetDictionaryEditMode(gFSMDict, true);

	// checkout the file
	if (!resGetLockOwner(gFSMDict, name)) 
	{
		// Don't have lock, so ask server to lock and go into locking state
		emSetResourceState(activeDoc->doc.editor, name, EMRES_STATE_LOCKING_FOR_SAVE);
		resRequestLockResource(gFSMDict, name, outFsm);
		StructDestroy(parse_FSM, outFsm);
		return EM_TASK_INPROGRESS;
	}
	// Get here if have the lock
	if (((activeDoc->saveFlags & SAVE_AS_NEW) == 0) && activeDoc->origName && (stricmp(activeDoc->origName, name) != 0))
	{
		// Name changed, so need to delete the old name
		FSM *origFSM = RefSystem_ReferentFromString(gFSMDict, activeDoc->origName);
		if (origFSM)
		{
			if (!resGetLockOwner(gFSMDict, activeDoc->origName)) {
				emSetResourceStateWithData(&fsmEditor, origFSM->name, EMRES_STATE_LOCKING_FOR_DELETE, NULL);
				resRequestLockResource(gFSMDict, origFSM->name, origFSM);
			}
			else
			{
				// Otherwise continue the delete
				fsmEditorDeleteContinue(&fsmEditor, origFSM->name, origFSM, EMRES_STATE_LOCK_SUCCEEDED, NULL, true);
			}
		}
	}

	// reset the FSM reference (in case it hasn't been set here yet)
	REMOVE_HANDLE(activeDoc->refFSM);
	SET_HANDLE_FROM_REFDATA(gFSMDict, activeDoc->workingFSM.name, activeDoc->refFSM);

	fsmEditorDocPrefsSave(activeDoc);

	// Correct the orig values so future saves are based on the new values
	if (!activeDoc->origName || (stricmp(activeDoc->origName, outFsm->name) != 0))
	{
		StructFreeString(activeDoc->origName);
		activeDoc->origName = StructAllocString(outFsm->name);
	}
	if (!activeDoc->origFile || (stricmp(activeDoc->origFile, outFsm->fileName) != 0))
	{
		StructFreeString(activeDoc->origFile);
		activeDoc->origFile = StructAllocString(outFsm->fileName);
	}

	// Send save to server
	emSetResourceStateWithData(activeDoc->doc.editor, name, EMRES_STATE_SAVING, outFsm);
	resRequestSaveResource(gFSMDict, name, outFsm);
	return EM_TASK_INPROGRESS;
}

/********************************************************************
* Editor Registration
********************************************************************/

#endif

AUTO_RUN_LATE;
int fsmEditorRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// register editor
	strcpy(fsmEditor.editor_name, "State Machine Editor");
	fsmEditor.type = EM_TYPE_SINGLEDOC;
	fsmEditor.allow_multiple_docs = 1;
	fsmEditor.allow_save = 1;
	fsmEditor.hide_world = 1;
	fsmEditor.disable_auto_checkout = 1;
	fsmEditor.default_type = "FSM";

	fsmEditor.keybinds_name = "FSMEditor";

	fsmEditor.can_init_func = fsmEditorPreInit;
	fsmEditor.init_func = fsmEditorInit;
	fsmEditor.enter_editor_func = fsmEditorEnter;
	fsmEditor.exit_func = fsmEditorExit;
	fsmEditor.custom_new_func = fsmEditorCustomNewDoc;
	fsmEditor.new_func = fsmEditorNewDoc;
	fsmEditor.load_func = fsmEditorLoadDoc;
	fsmEditor.reload_func = fsmEditorReloadDoc;
	fsmEditor.close_func = fsmEditorCloseDoc;
	fsmEditor.save_func = fsmEditorSaveDoc;
	fsmEditor.save_as_func = fsmEditorSaveDocAs;
	fsmEditor.autosave_func = fsmEditorAutosaveDoc;
	fsmEditor.usage_search_func = fsmEditorUsageSearch;
	fsmEditor.autosave_interval = 1;
	fsmEditor.enable_clipboard = 1;
	fsmEditor.do_not_reload = 1; // Change detection is handled separately

	emRegisterEditor(&fsmEditor);
	emRegisterFileType(fsmEditor.default_type, "State Machine", fsmEditor.editor_name);

	// register picker
	strcpy(fsmPicker.picker_name, "State Machine Library");
	strcpy(fsmPicker.default_type, fsmEditor.default_type);
	emPickerManage(&fsmPicker);
	eaPush(&fsmEditor.pickers, &fsmPicker);
#endif

	return 1;
}

#include "FSMEditorMain_h_ast.c"
#include "FSMEditorMain_c_ast.c"
