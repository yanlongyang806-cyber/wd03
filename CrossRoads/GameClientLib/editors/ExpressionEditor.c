/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
GCC_SYSTEM

#include "ExpressionEditor.h"
#include "ExpressionFunc.h"
#include "ExpressionDebug.h"
#include "sysutil.h"

#include "inputLib.h"

AUTO_ENUM;
typedef enum ExprEdCompleteEntryType
{
	ExprEdComplete_Automatic, // Automatically pick right type
	ExprEdComplete_Function, // Expression functions
	ExprEdComplete_Value, // Literal values
	ExprEdComplete_Variable, // Static variables
} ExprEdCompleteEntryType;

AUTO_STRUCT;
typedef struct ExprEdCompleteEntry
{
	ExprEdCompleteEntryType entryType;	
	char *typeString;
	char *valueString;
	ExprFuncDesc *entryFunc; NO_AST
} ExprEdCompleteEntry;

// Up here to get around xbox compile errors

#ifndef NO_EDITORS
#include "EditLibUIUtil.h"
#include "EditorPrefs.h"
#include "cmdparse.h"
#include "ExpressionTokenize.h"
#include "ExpressionPrivate.h" // include this in other files and I will kill you
#include "EditLibClipboard.h"
#include "GfxSpriteText.h"
#include "Color.h"
#include "EditorManager.h"
#include "inputText.h"
#include "inputData.h"
#include "StringCache.h"
#include "AutoGen/ExpressionEditor_c_ast.h"

#include "../../CrossRoads/Common/AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define EXPR_ED_TEXT_ENTRY_HEIGHT 20
#define EXPR_ED_MINI_ENTRY_WIDTH 50
#define EXPR_ED_LIST_ROW_HEIGHT 14

#define getEntryUI(widget) ((ExprEdEntryUI*) widget->onFocusData)
#define debugToks(toks, locs) {int debugCount;\
	printf("**START\n");\
	for(debugCount = 0; debugCount < eaSize(&toks); debugCount++)\
	{\
		char *str = NULL;\
		MultiValPrint(toks[debugCount], &str);\
		printf("--%s-- [%s]\n", str, locs[debugCount]);\
	}\
	printf("**END\n");\
}\
0 + 0

/********************************************************************
* FORWARD DECLARATIONS
********************************************************************/
typedef struct ExprEdLineUI ExprEdLineUI;
typedef struct ExprEdEntryUI ExprEdEntryUI;

static void exprEdEntryAreaUnfocus(UITextEntry *entry, UserData unused);
static bool exprEdEntryAreaInput(UITextArea *area, KeyInput *key);
static void exprEdEntryButtonClicked(UIWidget *button, ExprEdEntryUI *entry);
static void exprEdEntryChanged(UITextEntry *entry, ExprEdEntryUI *entryUI);
static void exprEdEntryEnter(UITextEntry *textEntry, ExprEdEntryUI *entry);
static void exprEdEntryFocus(UITextEntry *textEntry, ExprEdEntryUI *entryUI);
static void exprEdEntryFinished(UITextEntry *textEntry, UserData unused);
static void exprEdEntryTick(UITextEntry *textEntry, UI_PARENT_ARGS);
static void exprEdEntryDraw(UITextEntry *textEntry, UI_PARENT_ARGS);
static void exprEdEntryFree(UIWidget *widget);
static void exprEdRefreshFuncLabels(ExprEdEntryUI *activeParam);
static bool exprEdEntryInput(UITextEntry *entry, KeyInput *key);
static void exprEdLineUIMenuCallback(UIExpander *widget_UNUSED, ExprEdLineUI *lineUI);
static void exprEdExpandAll(ExprEdLineUI *workingLine);

static void exprEdOk(UIButton *button, UserData unused);

extern ParseTable parse_Expression[];
#define TYPE_parse_Expression Expression

/********************************************************************
* STRUCTS AND THEIR FUNCTIONS
********************************************************************/
typedef enum ExprEdValidationType
{
	EXPR_ED_VALID_LIST,
	EXPR_ED_VALID_DICT,
	EXPR_ED_VALID_FUNC,
} ExprEdValidationType;


typedef struct ExprEdEntryUI
{
	UITextEntry *textEntry;
	UIButton *setButton;
	int lastCurPos;
	MultiVal **tokens;
	char **locs;
	int tokenPos;

	MultiValType type;
	ExprEdLineUI *line;
	ExprEdEntryUI *openParen, *closeParen;
	ExprEdEntryUI *parentOpen, *parentClose;
	ExprFuncDesc *func;
	int funcParam;
	int hashIndex;
	bool bIsFirstParam;
} ExprEdEntryUI;

typedef struct ExprEdLineUI
{
	UIExpander *expander;
	ExprEdEntryUI **entries;
} ExprEdLineUI;

typedef struct ExprEdUI
{
	bool active;
	bool advMode;	
	ExprEdExprFunc exprFunc;
	ExprContext *context;
	ExprFuncDesc **contextFuncs;
	ExprVarEntry **contextVars;
	Expression *expr;
	UserData data;
	int singleLineIdx;
	ExprEdCompleteEntryType completeType;
	UIWindow *mainWin;	
	bool switching;
	bool justSelected;
	UIMenu *lineContextMenu;

	StashTable skins;
	UISkin *otherSkin;

	// basic mode
	UIExpanderGroup *lineGroup;
	ExprEdLineUI **lines;
	UIComboBox *completeTypeCombo;
	UIList *autocomplete;
	ExprEdCompleteEntry **completeEntries;

	ExprEdEntryUI *workingEntry;
	UISMFView *funcLabel;
	UICheckButton *substr;

	// advanced mode data
	UITextArea *textArea;
} ExprEdUI;

typedef struct ExprEdValidation
{
	union
	{
		struct 
		{
			ExprEdValidationClicked clickFunc;	// function to call with a button press
			UserData clickData;
		};
	};
	ExprEdValidationType type;
} ExprEdValidation;

static ExprFuncDescContainer *exprEdAllExprs;
static bool exprEdIsInit = false;
static ExprEdUI exprEdUI;
static StashTable exprEdTypeValidations;
static StashTable exprEdTypeButtonMappings;

static ExprEdEntryUI *exprEdEntryUICreateText(MultiValType type, ExprEdLineUI *line, bool bMakeButton)
{
	ExprEdEntryUI *entryUI = calloc(1, sizeof(ExprEdEntryUI));

	// create and initialize text entry widget
	entryUI->textEntry = ui_TextEntryCreate("", 0, 0);
	ui_TextEntrySetValidateCallback(entryUI->textEntry, ui_EditableForceAscii, NULL);
	entryUI->textEntry->widget.inputF = exprEdEntryInput;
	entryUI->textEntry->widget.tickF = exprEdEntryTick;
	entryUI->textEntry->widget.drawF = exprEdEntryDraw;
	ui_WidgetSetFreeCallback(UI_WIDGET(entryUI->textEntry), exprEdEntryFree);
	ui_TextEntrySetChangedCallback(entryUI->textEntry, exprEdEntryChanged, entryUI);
	ui_TextEntrySetFinishedCallback(entryUI->textEntry, exprEdEntryFinished, NULL);
	ui_WidgetSetFocusCallback(UI_WIDGET(entryUI->textEntry), exprEdEntryFocus, entryUI);
	ui_TextEntrySetEnterCallback(entryUI->textEntry, exprEdEntryEnter, entryUI);
	ui_WidgetSetDimensions(UI_WIDGET(entryUI->textEntry), 10, EXPR_ED_TEXT_ENTRY_HEIGHT);
	ui_ExpanderAddChild(line->expander, UI_WIDGET(entryUI->textEntry));

	if (bMakeButton)
	{
		static U32 index = 1;

		entryUI->setButton = ui_ButtonCreate("Set", 0, 0, exprEdEntryButtonClicked, entryUI);
		ui_WidgetSetFocusCallback(UI_WIDGET(entryUI->setButton), exprEdEntryFocus, entryUI);
		ui_WidgetSetDimensionsEx((UIWidget*)entryUI->setButton, 30, EXPR_ED_TEXT_ENTRY_HEIGHT, UIUnitFixed, UIUnitFixed);
		ui_ExpanderAddChild(line->expander, UI_WIDGET(entryUI->setButton));
		if (!exprEdTypeButtonMappings)
			exprEdTypeButtonMappings = stashTableCreateInt(16);
		entryUI->hashIndex = index;
		stashIntAddPointer(exprEdTypeButtonMappings, index++, entryUI, false);
	}

	entryUI->lastCurPos = ui_TextEntryGetCursorPosition(entryUI->textEntry);
	entryUI->tokenPos = 0;
	entryUI->tokens = NULL;
	entryUI->locs = NULL;
	entryUI->type = type;
	entryUI->line = line;
	return entryUI;
}

static void exprEdEntryUIFree(ExprEdEntryUI *entryUI)
{
	if (entryUI->setButton)
	{	
		void *temp;

		// remove pointer from reference stash
		stashIntRemovePointer(exprEdTypeButtonMappings, entryUI->hashIndex, &temp);
	}

	eaDestroyEx(&entryUI->tokens, MultiValDestroy);
	eaDestroy(&entryUI->locs);
	
	free(entryUI);
}

static ExprEdLineUI *exprEdLineUICreate(void)
{	
	ExprEdLineUI *lineUI = calloc(1, sizeof(ExprEdLineUI));
	UIExpander *expander = ui_ExpanderCreate("Expression", EXPR_ED_TEXT_ENTRY_HEIGHT);
	
	ui_ExpanderSetHeaderContextCallback(expander, exprEdLineUIMenuCallback, lineUI);
	expander->openedWidth = 1000;	
	
	lineUI->expander = expander;
	eaPush(&lineUI->entries, exprEdEntryUICreateText(MULTI_NONE, lineUI, false));
	
	return lineUI;
}

static void exprEdLineUIFree(ExprEdLineUI *lineUI)
{
	eaDestroy(&lineUI->entries);
	free(lineUI);
}

static UITextEntry* exprEdGetNextEntry(void)
{
	ExprEdEntryUI *entry = exprEdUI.workingEntry;

	if (entry)
	{
		int i = eaFind(&entry->line->entries, entry);
		if (i + 1 < eaSize(&entry->line->entries))
		{
			return entry->line->entries[i+1]->textEntry;
		}
		else
		{
			i = eaFind(&exprEdUI.lines, entry->line);
			if (i + 1 < eaSize(&exprEdUI.lines))
			{
				return exprEdUI.lines[i+1]->entries[0]->textEntry;
			}
		}
	}
	else if (eaSize(&exprEdUI.lines) && eaSize(&exprEdUI.lines[0]->entries))
	{
		return exprEdUI.lines[0]->entries[0]->textEntry;
	}

	return NULL;
}

static void exprEdFocusNextField(void)
{
	UITextEntry *textEntry = exprEdGetNextEntry();

	if(textEntry)
	{
		ui_SetFocus(UI_WIDGET(textEntry));
	}
}

/********************
* STATIC CHECK TYPE VALIDATION
********************/

/******
* This function registers a static check type to a particular function.  When an expression
* uses a parameter that has the registered static check type, a button will appear in the place
* of the parameter that, when clicked, will call the registered function.
* PARAMS:
*   type - string static check type to register
*   clickFunc - function to invoke when button is clicked; is passed a reference value that should
*               be used in conjunction with ExprEdSetValidationValue in order to set the actual
*               value of the associated parameter
*   clickData - pointer to data that is passed into clickFunc when it is invoked
******/
void exprEdRegisterValidationFunc(const char *type, ExprEdValidationClicked clickFunc, void *clickData)
{
	ExprEdValidation *validation = calloc(1, sizeof(*validation));

	if (!exprEdTypeValidations)
		exprEdTypeValidations = stashTableCreateWithStringKeys(16, StashDeepCopyKeys);

	validation->type = EXPR_ED_VALID_FUNC;
	validation->clickFunc = clickFunc;
	validation->clickData = clickData;

	stashAddPointer(exprEdTypeValidations, type, validation, false);
}

/******
* This function is to be called from outside the expression editor (primarily through the
* validation function call).  It is used to set the value of a button parameter.
*   valRef - int index corresponding to the parameter being set; this is initially passed
*            in the validation function call
*   text - string value to which the referenced parameter should be set
******/
void exprEdSetValidationValue(int valRef, const char *text)
{
	ExprEdEntryUI *edEntry;
	char *estr = NULL;

	if (stashIntFindPointer(exprEdTypeButtonMappings, valRef, &edEntry))
	{
		ui_TextEntrySetTextAndCallback(edEntry->textEntry, text);
		exprEdFocusNextField();
	}
}


/******
* This function returns the validation object for a particular registered static check type.
* PARAMS:
*   type - string registered static check type
* RETURNS:
*   ExprEdValidation registered for the type; NULL if nothing is registered
******/
static ExprEdValidation *exprEdGetTypeValidation(const char *type)
{
	ExprEdValidation *validation = NULL;
	if (type && stashFindPointer(exprEdTypeValidations, type, &validation))
		return validation;
	return NULL;
}

/******
* This is invoked when a button belonging to a parameter with a function-type validation
* is clicked.  It is set as a callback for the button.
* PARAMS:
*   button - UIWidget button that was clicked   
******/
static void exprEdEntryButtonClicked(UIWidget *button, ExprEdEntryUI *entry)
{
	ExprEdValidation *validation = exprEdGetTypeValidation(entry->func->args[entry->funcParam].staticCheckType);

	if (validation)
	{
		char *tempString = NULL;
		estrStackCreate(&tempString);
		estrCopy2(&tempString, ui_TextEntryGetText(entry->textEntry));
		if (estrLength(&tempString) > 2 && tempString[0] == '"' && tempString[estrLength(&tempString) - 1] == '"')
		{
			// Remove quotes
			tempString[estrLength(&tempString) - 1] = '\0';
			validation->clickFunc(entry->hashIndex, tempString + 1, validation->clickData);
		}
		else
		{
			validation->clickFunc(entry->hashIndex, tempString, validation->clickData);
		}
		estrDestroy(&tempString);
	}
}

/********************************************************************
* UTIL FUNCTIONS
********************************************************************/
static void exprEdGetExprDisplayName(SA_PARAM_OP_VALID Expression *expr, SA_PARAM_NN_VALID char **output)
{
	if (expr)
	{
		if (eaSize(&expr->lines) > 0)
			estrPrintf(output, "%s%s", expr->lines[0]->origStr, eaSize(&expr->lines) > 1 ? "..." : "");
	}
}

static void exprEdGetExprLineDisplayName(SA_PARAM_OP_VALID ExprLine *line, SA_PARAM_NN_VALID char **output)
{
	if (line)
	{
		estrPrintf(output, "%s", line->origStr);
	}
}

int exprFuncDescFindComp(const ExprFuncDesc **a, const ExprFuncDesc **b)
{
	return strnicmp((*a)->funcName, (*b)->funcName, strlen((*a)->funcName));
}

int exprFuncDescComp(const ExprFuncDesc **a, const ExprFuncDesc **b)
{
	return strcmpi((*a)->funcName, (*b)->funcName);
}

static void exprEdSkinEntry(ExprEdEntryUI *entry, int indent)
{
	UISkin *skin;
	MultiValType fieldType = MULTI_NONE;

	if (entry->func)
	{
		fieldType = entry->func->args[entry->funcParam].type;
	}
	else if (!entry->parentOpen && !entry->parentClose)
	{
		fieldType = MULTI_NP_POINTER; // TODO: Replace this with actual type when we know about return value
	}

	if ((!entry->closeParen && !entry->openParen) || fieldType == MULTI_NONE)
	{
		ui_WidgetSkin(UI_WIDGET(entry->textEntry), NULL);
	}
	else if (stashIntFindPointer(exprEdUI.skins, fieldType, &skin))
	{
		ui_WidgetSkin(UI_WIDGET(entry->textEntry), skin);
	}
	else
	{
		ui_WidgetSkin(UI_WIDGET(entry->textEntry), exprEdUI.otherSkin);
	}
}

/******
* This function appends the appropriate text to the specified EString for the given entry.
* PARAMS:
*   entryUI - ExprEdEntryUI being enclosed
*   output - EString where the enclosure will be appended
******/
static void exprEdEntryGetCollapsedText(ExprEdEntryUI *currEntry, char **output)
{
	MultiValType type;
	MultiVal **toks = currEntry->tokens;
	char **locs = currEntry->locs;;

	if (eaSize(&toks) > 0)
	{			
		// process the types appropriately
		// -put quotes around strings
		// -put braces around expressions
		if (!currEntry->openParen && !currEntry->closeParen && 
			(((currEntry->type == MULTI_STRING || currEntry->type == MULTIOP_LOC_MAT4 || currEntry->type == MULTIOP_LOC_STRING) && (eaSize(&toks) > 1 || (toks[0]->type != MULTI_STRING && toks[0]->type != MULTIOP_LOC_MAT4 && toks[0]->type != MULTIOP_LOC_STRING)))
			|| (currEntry->type == MULTIOP_NP_STACKPTR && toks[0]->type != MULTIOP_BRACE_OPEN && toks[eaSize(&toks) - 1]->type != MULTIOP_BRACE_CLOSE)))
		{
			type = currEntry->type;
		}		
		else
		{
			type = MULTI_NONE;			
		}

		switch (type)
		{
		xcase MULTI_STRING:
		case MULTIOP_LOC_MAT4:
		case MULTIOP_LOC_STRING:
			estrConcatf(output, "\"");
		xcase MULTIOP_NP_STACKPTR:
			estrConcatf(output, "{");
		}

		estrConcatf(output, "%s", ui_TextEntryGetText(currEntry->textEntry));

		switch (type)
		{
		xcase MULTI_STRING:
		case MULTIOP_LOC_MAT4:
		case MULTIOP_LOC_STRING:
			estrConcatf(output, "\"");
		xcase MULTIOP_NP_STACKPTR:
			estrConcatf(output, "}");
		}
	}
}

static void exprEdLineGetCollapsedText(ExprEdLineUI *lineUI, char **output)
{
	int i;
	for (i = 0; i < eaSize(&lineUI->entries); i++)
	{
		if (!lineUI->entries[i]->bIsFirstParam && lineUI->entries[i]->funcParam > 0 && (lineUI->entries[i]->closeParen || !lineUI->entries[i]->openParen))
		{
			estrConcatf(output, ",");
		}

		exprEdEntryGetCollapsedText(lineUI->entries[i], output);
	}
}


static int exprEdTokenizeAtCursor(UITextEntry *entry, MultiVal ***mvArray, const char ***mvLocs)
{
	int i;
	size_t pos = ui_TextEntryGetCursorPosition(entry);
	int cursorIdx;
	char *beforePos = NULL;
	char *afterPos = NULL;
	size_t beforeLen, afterLen;
	MultiVal **tempArray = NULL;
	const char *pcEntryText = ui_TextEntryGetText(entry);

	assert(mvArray && !eaSize(mvArray)); // should start with empty array

	// split text before and after cursor
	beforePos = strdup(pcEntryText);
	if (pos > strlen(beforePos))
		pos = strlen(beforePos);
	afterPos = strdup(&beforePos[pos]);
	beforePos[pos] = '\0';
	beforeLen = strlen(beforePos);
	afterLen = strlen(afterPos);

	if(exprTokenizeEx(pcEntryText, mvArray, mvLocs, false, NULL) && eaSize(mvArray) == 0) 
	{
		//if nothing was tokenized
		MultiVal *curVal = MultiValCreate();
		curVal->type = MULTIOP_IDENTIFIER;
		curVal->str = allocAddString(pcEntryText);
		eaPush(mvArray, curVal);
		eaPush(mvLocs, pcEntryText);
	}

	// tokenize before and after cursor and see if the cursor splits or changes a token
	exprTokenizeEx(beforePos, &tempArray, NULL, false, NULL);
	cursorIdx = eaSize(&tempArray);
	exprTokenizeEx(afterPos, &tempArray, NULL, false, NULL);
	if (cursorIdx > 0)
	{	
		if (eaSize(&tempArray) != eaSize(mvArray))
		{
			cursorIdx--;
		}
		else 
		{	
			for (i = 0; i < eaSize(&tempArray); i++)
			{
				// If we stop on a tailing quote, it can change the type but not number of following arguments.
				if (tempArray[i]->type != (*mvArray)[i]->type)
				{
					cursorIdx--;
					break;
				}
			}
		}
	}

	eaDestroyEx(&tempArray, MultiValDestroy);
	free(beforePos);
	free(afterPos);
	return cursorIdx;
}

static void exprEdLineRefreshUI(ExprEdLineUI *line)
{
	int i, nextX = 0, nextY = 0, maxX = 0, maxY = 0;
	int lineIndex;
	int indentLevel = 0;
	int lastDeepest = -1;
	ExprEdEntryUI *pLastParent = NULL;
	static char *labelString;
	static int *indentLevels;
	const char* lineText = ui_WidgetGetText( UI_WIDGET( line->expander ));
	F32 labelWidth = ui_StyleFontWidthNoCache(NULL, 1, lineText) + 40;
	lineIndex = eaFind(&exprEdUI.lines,line);

	eaiClear(&indentLevels);

	for (i = 0; i < eaSize(&line->entries); i++)
	{
		bool bPushClose =false;
		ExprEdEntryUI *entryUI = line->entries[i];

		if (entryUI->parentOpen != pLastParent)
		{
		
			if (entryUI->bIsFirstParam && entryUI->func && (!pLastParent || entryUI->parentOpen != pLastParent->parentOpen))
			{
				indentLevel++;
				lastDeepest = i;
			}
			else
			{
				int j;
				if (lastDeepest != -1 && indentLevels[lastDeepest] == indentLevel)
				{
					// If we're at the local max in depth, set indent levels of things at this level to -1
					for (j = i - 1; j >= lastDeepest; j--)
					{
						indentLevels[j] = -1;
					}
					bPushClose = true;
				}
				indentLevel--;
				lastDeepest = -1;
			}			
			pLastParent = entryUI->parentOpen;
		}
		if (bPushClose)
			eaiPush(&indentLevels, -1);
		else
			eaiPush(&indentLevels, indentLevel);
	}	

	for (i = 0; i < eaSize(&line->entries); i++)
	{
		ExprEdEntryUI *entryUI = line->entries[i];
		UIWidget *entry = NULL;
		if (entryUI->textEntry)
		{
			UITextEntry *textEntry = entryUI->textEntry;
			F32 width = ui_StyleFontWidthNoCache(ui_WidgetGetFont(UI_WIDGET(textEntry)), 1, ui_TextEntryGetText(line->entries[i]->textEntry)) + 20;
			F32 maxWidth = 1000;
			entry = UI_WIDGET(entryUI->textEntry);
			if (!textEntry->area || !textEntry->areaOpened)
				entry->width = MIN(width, maxWidth);
			if (width >= maxWidth)
			{
				if (!textEntry->area)
				{
					UITextArea *area = ui_TextAreaCreate("");
					ui_WidgetSetUnfocusCallback(UI_WIDGET(area), exprEdEntryAreaUnfocus, NULL);
					ui_TextEntrySetTextArea(textEntry, area);
				}
				assert(textEntry->area);
				textEntry->area->widget.inputF = exprEdEntryAreaInput;
			}
			else if (textEntry->area && !textEntry->areaOpened)
			{
				ui_WidgetQueueFree(UI_WIDGET(textEntry->area));
				textEntry->area = NULL;
			}
		}
		
		assert(entry);
				
		if (indentLevels[i] != -1)
		{		
			entry->x = 25 + indentLevels[i] * 20;
			entry->y = nextY + 2;
			indentLevel = indentLevels[i];
		}
		else
		{
			// Indent of -1 means use previous line
			entry->x = nextX + 5;
			entry->y = nextY - entry->height;
		}

		if (entryUI->setButton)
		{
			UI_WIDGET(entryUI->setButton)->x = entry->x;
			UI_WIDGET(entryUI->setButton)->y = entry->y;
			entry->x += UI_WIDGET(entryUI->setButton)->width;
		}

		nextX = entry->x + entry->width;
		nextY = entry->y + entry->height;
		
		maxX = MAX(maxX, nextX + 5);
		maxY = MAX(maxY, nextY + 2);
		exprEdSkinEntry(line->entries[i], indentLevel);	
	}

	estrPrintf(&labelString, "");
	exprEdLineGetCollapsedText(line, &labelString);

	ui_WidgetSetTextString(UI_WIDGET(line->expander), labelString);

	line->expander->openedWidth = MAX(maxX, labelWidth);
	line->expander->openedHeight = MAX(maxY, EXPR_ED_TEXT_ENTRY_HEIGHT);
	ui_ExpanderReflow(line->expander);
}

static void exprEdRefreshUI(void)
{
	int i;
	for (i = 0; i < eaSize(&exprEdUI.lines); i++)
		exprEdLineRefreshUI(exprEdUI.lines[i]);
}

#define exprEdAddLine(text, create) exprEdAddLineAt(text, eaSize(&exprEdUI.lines), create)
static S32 exprEdAddLineAt(const char *text, int i, bool create)
{
	S32 created = false;
	ExprEdLineUI *lineUI = exprEdLineUICreate();

	eaInsert(&exprEdUI.lines, lineUI, i);

	ui_ExpanderGroupInsertExpander(exprEdUI.lineGroup, lineUI->expander, i);

	if (create)
	{
		eaInsert(&exprEdUI.expr->lines, exprLineCreate("", ""), i);
		created = true;
	}
	ui_TextEntrySetTextAndCallback(lineUI->entries[0]->textEntry, text);
	ui_SetFocus(lineUI->entries[0]->textEntry);
	if (i == eaSize(&exprEdUI.expr->lines) - 1)
		exprEdLineRefreshUI(lineUI);
	else
		exprEdRefreshUI();

	return created;
}

void exprEdDeleteLine(ExprEdLineUI *lineUI)
{
	int i = eaFind(&exprEdUI.lines, lineUI);
	int j;

	ui_ExpanderGroupRemoveExpander(exprEdUI.lineGroup, lineUI->expander);
	ui_WidgetQueueFree((UIWidget*) lineUI->expander);
	for (j = 0; j < eaSize(&lineUI->entries); j++)
	{
		ui_ExpanderRemoveChild(lineUI->expander, (UIWidget*) lineUI->entries[j]->textEntry);
		ui_WidgetQueueFree((UIWidget*) lineUI->entries[j]->textEntry);
	}
	eaRemove(&exprEdUI.lines, i);

	exprLineDestroy(exprEdUI.expr->lines[i]);
	eaRemove(&exprEdUI.expr->lines, i);

	exprEdLineUIFree(lineUI);
}


static char *exprEdMultiValTypeToString(MultiValType type)
{
	switch(type)
	{
	xcase MULTI_NONE:
		return "VOID";
	xcase MULTI_INT:
		return "INT";
	xcase MULTI_FLOAT:
		return "FLOAT";
	xcase MULTI_INTARRAY:
		return "INTARRAY";
	xcase MULTI_FLOATARRAY:
		return "FLOATARRAY";
	xcase MULTI_STRING:
		return "STRING";
	xcase MULTI_NP_ENTITYARRAY:
		return "ENTITYARRAY";
	xcase MULTIOP_NP_STACKPTR:
		return "EXPRESSION";
	xcase MULTI_NP_POINTER:
		return "STRUCT";
	xcase MULTIOP_LOC_MAT4:
	case MULTIOP_LOC_STRING:
		return "LOCATION";
	xdefault:
		return "OTHER";
	}
}

static void exprEdWriteFuncArgDesc(ExprFuncArg* pArg, char **estrOut)
{
	if (pArg->staticCheckType)
	{
		estrConcatf(estrOut, "%s(%s)", exprEdMultiValTypeToString(pArg->type), pArg->staticCheckType);
	}
	else if (pArg->ptrTypeName)
	{
		estrConcatf(estrOut, "%s(%s)", exprEdMultiValTypeToString(pArg->type), pArg->ptrTypeName);
	}
	else
	{
		estrConcatf(estrOut, "%s", exprEdMultiValTypeToString(pArg->type));
	}
	if (pArg->name)
	{
		estrConcatf(estrOut, " %s", pArg->name);
	}
}

static void exprEdListAddFunction(UIList *list, ExprFuncDesc *currDesc)
{
	int i;
	static char *estrTemp;
	ExprEdCompleteEntry *pEntry = StructCreate(parse_ExprEdCompleteEntry);
	pEntry->entryType = ExprEdComplete_Function;
	pEntry->entryFunc = currDesc;

	estrPrintf(&estrTemp, "");
	exprEdWriteFuncArgDesc(&currDesc->returnType,&estrTemp);
	pEntry->typeString = strdup(estrTemp);

	estrPrintf(&estrTemp, "%s (", currDesc->funcName);
	for (i = 0; i < currDesc->argc; i++)
	{		
		if (i)
			estrConcatf(&estrTemp, ", ");

		exprEdWriteFuncArgDesc(&currDesc->args[i], &estrTemp);

		// sub expressions actually take up two parameters; we'll only list one
		if (currDesc->args[i].type == MULTIOP_NP_STACKPTR)
			i++;
	}
	estrConcatf(&estrTemp, ")");
	pEntry->valueString = strdup(estrTemp);

	eaPush(list->peaModel, pEntry);
}

static void exprEdListAddValue(UIList *list, const char *typeString, const char *valueString)
{
	ExprEdCompleteEntry *pEntry = StructCreate(parse_ExprEdCompleteEntry);
	pEntry->entryType = ExprEdComplete_Value;
	pEntry->typeString = strdup(typeString);
	pEntry->valueString = strdup(valueString);

	eaPush(list->peaModel, pEntry);
}

static void exprEdListAddVariable(UIList *list, const char *typeString, const char *valueString)
{
	static char *estrTemp;
	ExprEdCompleteEntry *pEntry = StructCreate(parse_ExprEdCompleteEntry);
	pEntry->entryType = ExprEdComplete_Variable;

	estrPrintf(&estrTemp, "STRUCT(%s)", typeString);
	pEntry->typeString = strdup(estrTemp);

	pEntry->valueString = strdup(valueString);

	eaPush(list->peaModel, pEntry);
}

static void exprEdEntryHandleChange(ExprEdEntryUI *entry)
{
	const char *text = ui_TextEntryGetText(entry->textEntry);
	bool idTokenFound = false;
	const char **locs = NULL;
	ExprFuncDesc ***descs;
	ExprFuncArg *pArg = NULL;
	bool bCheckSubstr = false;

	entry->lastCurPos = ui_TextEntryGetCursorPosition(entry->textEntry);

	// find a potential function token around the cursor
	if (entry->tokens)
		eaDestroyEx(&entry->tokens, MultiValDestroy);
	if (entry->locs)
		eaClear(&entry->locs);
	entry->tokenPos = exprEdTokenizeAtCursor(entry->textEntry, &entry->tokens, &entry->locs);
	
	if (eaSize(&entry->tokens))
	{
		if (entry->tokenPos < eaSize(&entry->tokens) && 
			(MULTI_GET_OPER(entry->tokens[entry->tokenPos]->type) == MMO_IDENTIFIER ||
			(MULTI_GET_OPER(entry->tokens[entry->tokenPos]->type) == MMO_NONE && MULTI_GET_TYPE(entry->tokens[entry->tokenPos]->type) == MMT_STRING)))
		{
			// On identifer or string token
			idTokenFound = true;
		}
		else if (entry->tokenPos > 0 &&
			(MULTI_GET_OPER(entry->tokens[entry->tokenPos - 1]->type) == MMO_IDENTIFIER ||
			(MULTI_GET_OPER(entry->tokens[entry->tokenPos - 1]->type) == MMO_NONE && MULTI_GET_TYPE(entry->tokens[entry->tokenPos - 1]->type) == MMT_STRING))
			&& (entry->tokenPos == eaSize(&entry->tokens) || text + ui_TextEntryGetCursorPosition(entry->textEntry) <= entry->locs[entry->tokenPos]))
		{
			// Right after identifier or string token
			idTokenFound = true;
			entry->tokenPos--;
		}
	}

	if(exprEdUI.substr && ui_CheckButtonGetState(exprEdUI.substr))
		bCheckSubstr = true;

	if (entry->func)
	{
		pArg = &entry->func->args[entry->funcParam];
	}

	// check for function name matches
	eaClearStructVoid(exprEdUI.autocomplete->peaModel, parse_ExprEdCompleteEntry);
	
	{	
		int bestIdx = -1;
		const char *bestName = NULL;
		bool bFound = false;

		// If it starts with a quote, don't show functions in auto mode
		if ((exprEdUI.completeType == ExprEdComplete_Automatic && (!idTokenFound || entry->locs[entry->tokenPos][0] != '"'))
			|| exprEdUI.completeType == ExprEdComplete_Function)
		{
			const char *keyName = NULL;
			int idx;
			
			// determine whether to use context functions or all functions
			if (eaSize(&exprEdUI.contextFuncs) > 0)
				descs = &exprEdUI.contextFuncs;
			else
				descs = &exprEdAllExprs->funcs;

			if (idTokenFound)
			{			
				if (entry->tokens[entry->tokenPos]->str[0] == '.')
					keyName = entry->tokens[entry->tokenPos]->str + 1;
				else
					keyName = entry->tokens[entry->tokenPos]->str;
			}

			for(idx=0; idx<eaSize(descs); idx++)
			{
				ExprFuncDesc *desc = (*descs)[idx];

				if (pArg && exprEdUI.completeType == ExprEdComplete_Automatic && !exprFuncCanArgBeConverted(&desc->returnType, pArg))
				{
					// invalid return type for function, but only in automatic mode
					continue;
				}

				if (!idTokenFound)
				{
					exprEdListAddFunction(exprEdUI.autocomplete, desc);					
				}	
				else if (strStartsWith(desc->funcName, keyName))
				{
					exprEdListAddFunction(exprEdUI.autocomplete, desc);
					if(bestIdx == -1 || stricmp(desc->funcName, bestName) < 0)
					{
						bestName = desc->funcName;
						bestIdx = eaSize(exprEdUI.autocomplete->peaModel) - 1;
					}
					bFound = true;
				}			
				else if(bCheckSubstr && strstri(desc->funcName, keyName))
				{
					exprEdListAddFunction(exprEdUI.autocomplete, desc);
					bFound = true;
				}
			}			
		}

		if (exprEdUI.completeType == ExprEdComplete_Value || exprEdUI.completeType == ExprEdComplete_Automatic)
		{
			if (entry->func)
			{
				const char *keyName = NULL;

				if (idTokenFound)
				{
					if (entry->tokens[entry->tokenPos]->str[0] == '"')
						keyName = entry->tokens[entry->tokenPos]->str + 1;
					else
						keyName = entry->tokens[entry->tokenPos]->str;
				}

				if (pArg->scTypeCategory == ExprStaticCheckCat_Resource || pArg->scTypeCategory == ExprStaticCheckCat_Reference)
				{
					ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pArg->staticCheckType);
					if (pDictInfo)
					{
						int i;
						for (i = 0; i < eaSize(&pDictInfo->ppInfos); i++)
						{
							bool bMatches = false;
							ResourceInfo *pInfo = pDictInfo->ppInfos[i];

							if (!idTokenFound)
							{
								exprEdListAddValue(exprEdUI.autocomplete, pArg->staticCheckType, pInfo->resourceName);
							}
							else if (strStartsWith(pInfo->resourceName,keyName))
							{
								exprEdListAddValue(exprEdUI.autocomplete, pArg->staticCheckType, pInfo->resourceName);
								if(bestIdx == -1 || stricmp(pInfo->resourceName, bestName) < 0)
								{
									bestName = pInfo->resourceName;
									bestIdx = eaSize(exprEdUI.autocomplete->peaModel) - 1;
								}
								bFound = true;
							}
							else if(bCheckSubstr && strstri(pInfo->resourceName,keyName))
							{
								exprEdListAddValue(exprEdUI.autocomplete, pArg->staticCheckType, pInfo->resourceName);
								bFound = true;
							}
						}						
					}
				}				
				else if (pArg->scTypeCategory == ExprStaticCheckCat_Enum)
				{
					StaticDefineInt *pEnum = FindNamedStaticDefine(pArg->staticCheckType);
					if (pEnum)
					{
						char **ppKeys = NULL;
						int i;
						DefineFillAllKeysAndValues(pEnum, &ppKeys, NULL);					
						for (i = 0; i < eaSize(&ppKeys); i++)
						{
							bool bMatches = false;

							if (!idTokenFound)
							{
								exprEdListAddValue(exprEdUI.autocomplete, pArg->staticCheckType, ppKeys[i]);
							}
							else if (strStartsWith(ppKeys[i],keyName))
							{
								exprEdListAddValue(exprEdUI.autocomplete, pArg->staticCheckType, ppKeys[i]);
								if(bestIdx == -1 || stricmp(ppKeys[i], bestName) < 0)
								{
									bestName = ppKeys[i];
									bestIdx = eaSize(exprEdUI.autocomplete->peaModel) - 1;
								}
								bFound = true;
							}
							else if(bCheckSubstr && strstri(ppKeys[i],keyName))
							{
								exprEdListAddValue(exprEdUI.autocomplete, pArg->staticCheckType, ppKeys[i]);
								bFound = true;
							}
						}
						eaDestroy(&ppKeys);
					}
				}
			}
		}

		if (exprEdUI.completeType == ExprEdComplete_Variable || exprEdUI.completeType == ExprEdComplete_Automatic)
		{
			int i;
			const char *keyName = NULL;

			if (idTokenFound)
			{
				if (entry->tokens[entry->tokenPos]->str[0] == '"')
					keyName = entry->tokens[entry->tokenPos]->str + 1;
				else
					keyName = entry->tokens[entry->tokenPos]->str;
			}

			for(i = eaSize(&exprEdUI.contextVars)-1; i >= 0; i--)
			{
				ExprVarEntry *varEntry = exprEdUI.contextVars[i];

				// TODO: Must we rely on having a ParseTable? Is there anything else in a Context Vars?
				if(!varEntry->table)
					continue;

				// TODO: Should we use a variant of exprFuncCanArgBeConverted instead of just checking parse table?
				if (pArg && exprEdUI.completeType == ExprEdComplete_Automatic && varEntry->table != pArg->ptrType) // !exprFuncCanArgBeConverted(&desc->returnType, pArg))
				{
					// invalid type for variable, but only in automatic mode
					continue;
				}

				if (!idTokenFound)
				{
					exprEdListAddVariable(exprEdUI.autocomplete, varEntry->table->name, varEntry->name);
				}
				else if (strStartsWith(varEntry->name,keyName))
				{
					exprEdListAddVariable(exprEdUI.autocomplete, varEntry->table->name, varEntry->name);
					if(bestIdx == -1 || stricmp(varEntry->name, bestName) < 0)
					{
						bestName = varEntry->name;
						bestIdx = eaSize(exprEdUI.autocomplete->peaModel) - 1;
					}
					bFound = true;
				}
				else if(bCheckSubstr && strstri(varEntry->name,keyName))
				{
					exprEdListAddVariable(exprEdUI.autocomplete, varEntry->table->name, varEntry->name);
					bFound = true;
				}
			}
		}

		if(bestIdx > -1)
		{
			int iRow = (bFound && !exprEdUI.justSelected) ? bestIdx : -1;
			ui_ListSetSelectedRow(exprEdUI.autocomplete, iRow);
		}
		else
		{
			int iRow = (bFound && !exprEdUI.justSelected) ? 0 : -1;
			ui_ListSetSelectedRow(exprEdUI.autocomplete, iRow);
		}
		ui_ListSort(exprEdUI.autocomplete);
		ui_ListScrollToSelection(exprEdUI.autocomplete);
	}

	exprEdRefreshFuncLabels(entry);
}

static int collapsing = 0;

static void exprEdCollapseEntries(ExprEdEntryUI *start)
{
	int s, e;
	ExprEdLineUI *line = start->line;
	ExprEdEntryUI *closeParen = NULL;
	char *newStartText = NULL;

	estrStackCreate(&newStartText);

	assert(start->closeParen);
	s = eaFind(&line->entries, start) + 1;
	e = eaFind(&line->entries, start->closeParen);
	estrPrintf(&newStartText, "%s", ui_TextEntryGetText(start->textEntry));
	while (s < eaSize(&line->entries) && s < e + 1)
	{
		ExprEdEntryUI *currEntry = line->entries[s];
		size_t oldLen = strlen(newStartText);

		while (currEntry->closeParen && currEntry->openParen != start)
		{
			exprEdCollapseEntries(currEntry);
			e = eaFind(&line->entries, start->closeParen);
		}
		
		exprEdEntryGetCollapsedText(currEntry, &newStartText);

		if (s < e - 1)
			estrConcatf(&newStartText, ",");

		ui_ExpanderRemoveChild(line->expander, UI_WIDGET(currEntry->textEntry));
		ui_WidgetForceQueueFree(UI_WIDGET(currEntry->textEntry));		

		eaRemove(&line->entries, s);

		e--;
		closeParen = currEntry->closeParen;
	}

	start->closeParen = closeParen;
	if (closeParen)
		closeParen->openParen = start;
	collapsing++;
	ui_TextEntrySetTextAndCallback(start->textEntry, newStartText);
	collapsing--;
	ui_SetFocus((UIWidget*) start->textEntry);
	exprEdLineRefreshUI(line);

	estrDestroy(&newStartText);
}

static void exprEdRefreshFuncLabels(ExprEdEntryUI *activeParam)
{
	static char *labelText = NULL;
	int i;
	// will only populate labels when a parameter has focus
	if (!activeParam->parentOpen && !activeParam->parentClose)
	{
		ui_SMFViewSetText(exprEdUI.funcLabel, "", NULL);
		return;
	}

	// set the pre-active-param label
	estrPrintf(&labelText, "%s(", activeParam->func->funcName);
	for (i = 0; i < activeParam->func->argc; i++)
	{
		if (i)
			estrConcatf(&labelText, ", ");

		if (i == activeParam->funcParam)
		{
			estrConcatf(&labelText, "<b>");
			exprEdWriteFuncArgDesc(&activeParam->func->args[i], &labelText);
			estrConcatf(&labelText, "</b>");
		}
		else
		{
			exprEdWriteFuncArgDesc(&activeParam->func->args[i], &labelText);
		}
		
		if (activeParam->func->args[i].type == MULTIOP_NP_STACKPTR)
			i++;
	}		
	estrConcatf(&labelText, ")");

	ui_SMFViewSetText(exprEdUI.funcLabel, labelText, NULL);
}

/******
* This function creates text entries for function parameters.
* PARAMS:
*   desc - ExprFuncDesc whose parameters will be made into text entries
*   offset - int number of parameters from the beginning of desc's parameter list to skip
*   sourcePos - int index in the line where the parameter entries will be inserted
*   openParen - ExprEdEntryUI of the parent open parenthesis entry enclosing the parameters to be created
******/
static int exprEdParamEntriesCreate(ExprFuncDesc *desc, int offset, int sourcePos, ExprEdEntryUI *openParen)
{
	ExprEdLineUI *workingLine = openParen->line;
	int i, count = 0;

	for (i = offset; i < desc->argc; i++)
	{
		ExprEdEntryUI *paramEntry = NULL;
		bool bShowButton = false;

		assert(i < EXPR_MAX_ALLOWED_ARGS);

		if (desc->args[i].staticCheckType)
		{
			ExprEdValidation *validation = exprEdGetTypeValidation(desc->args[i].staticCheckType);
			if (validation && validation->type == EXPR_ED_VALID_FUNC)
			{
				bShowButton = true;
			}
		}

		paramEntry = exprEdEntryUICreateText(desc->args[i].type, workingLine, bShowButton);

		eaInsert(&workingLine->entries, paramEntry, sourcePos);
		sourcePos++;
		count++;

		// set navigational and function parameter info on the new entries
		paramEntry->parentOpen = openParen;
		paramEntry->parentClose = openParen->closeParen;
		paramEntry->func = desc;
		paramEntry->funcParam = i;

		// only create one text entry for subexpression parameters
		if (desc->args[i].type == MULTIOP_NP_STACKPTR)
			i++;
	}

	return count;
}

/******
* This function populates parameter entries with strings, attempting to separate the string tokens
* at commas.
* PARAMS:
*   startEntry - ExprEdEntryUI where parameters values will start being populated; will continue until tokens
*                run out or parentClose entry is hit
*   tokens - MultiVal EArray representing the parsed string values that should be filled into the parameter
*            entries
*   locs - char pointer EArray pointing to the location in the original parsed string where each corresponding
*          token at the same index can be found
*   tokenPos - int index into tokens and locs where values should begin to be copied into the entries
******/
static int exprEdParamEntriesPopulate(ExprEdEntryUI *startEntry, const MultiVal **tokens, const char **locs, int tokenPos)
{
	ExprEdLineUI *lineUI = startEntry->line;
	int s, e;
	int parenCount = 1;
	bool tokensLeft = tokens && !(tokenPos == eaSize(&tokens) || tokens[tokenPos]->type == MULTIOP_PAREN_CLOSE);

	assert(lineUI && startEntry->parentClose);
	s = eaFind(&lineUI->entries, startEntry);
	e = eaFind(&lineUI->entries, startEntry->parentClose);
	assert(s > 0 && e > 0 && s < e);

	while (s < e && tokensLeft)
	{
		// populate entry with any already-entered args
		char *paramText;
		size_t paramLen;
		int startPos;

		startPos = tokenPos;
		// get string between this next token and the next comma/delimiter
		while (tokenPos < eaSize(&tokens) && ((parenCount > 1) || (tokens[tokenPos]->type != MULTIOP_COMMA && tokens[tokenPos]->type != MULTIOP_PAREN_CLOSE)))
		{
			if (tokens[tokenPos]->type == MULTIOP_PAREN_OPEN)
				parenCount++;
			else if (tokens[tokenPos]->type == MULTIOP_PAREN_CLOSE)
				parenCount--;

			if (parenCount > 0)
				tokenPos++;
		}

		paramLen = strlen(locs[startPos]) - (tokenPos == eaSize(&tokens) ? 0 : strlen(locs[tokenPos]));
		strdup_alloca(paramText, locs[startPos]);
		paramText[paramLen] = '\0';

		// populate the entry
		ui_TextEntrySetTextAndCallback(lineUI->entries[s]->textEntry, paramText);
		s++;

		// should we continue getting existing params?
		if (tokenPos >= eaSize(&tokens) - 1 || tokens[tokenPos]->type == MULTIOP_PAREN_CLOSE)
		{
			tokensLeft = false;
			if (tokenPos == eaSize(&tokens) - 1)
				tokenPos++;
		}
		else
			tokenPos++;
	}
	return tokenPos;
}

/********************
* SETTINGS
********************/
static void exprEdSettingsUpdate(bool save)
{
	EditorPrefStoreWindowPosition("Expression Editor", "Window Position", "Main", exprEdUI.mainWin);
	if (exprEdUI.substr)
		EditorPrefStoreInt("Expression Editor", "Settings", "SubstringSearch", ui_CheckButtonGetState(exprEdUI.substr));
	if (exprEdUI.autocomplete)
	{
		int i;
		for(i = eaSize(&exprEdUI.autocomplete->eaColumns) - 1; i >= 0; i--)
		{
			static char *str;
			estrPrintf(&str, "AutoComplete_Col_%d", i);
			EditorPrefStoreInt("Expression Editor", "Settings", str, exprEdUI.autocomplete->eaColumns[i]->fWidth);
		}
	}
}

static void exprEdSettingsApply(void)
{
	EditorPrefGetWindowPosition("Expression Editor", "Window Position", "Main", exprEdUI.mainWin);
	if (exprEdUI.substr)
		ui_CheckButtonSetState(exprEdUI.substr,EditorPrefGetInt("Expression Editor", "Settings", "SubstringSearch", false));
	if (exprEdUI.autocomplete)
	{
		int i;
		for(i = eaSize(&exprEdUI.autocomplete->eaColumns) - 1; i >= 0; i--)
		{
			static char *str;
			int width;
			estrPrintf(&str, "AutoComplete_Col_%d", i);
			width = EditorPrefGetInt("Expression Editor", "Settings", str, -1);
			if(width > -1)
				exprEdUI.autocomplete->eaColumns[i]->fWidth = width;
		}
	}
}

/********************************************************************
* EXPRESSION INTERPRETATION
********************************************************************/
/******
* This function uses the data filled into the basic mode UI to populate the
* working expression's data.
******/
static void exprEdGetExprBasic(void)
{
	int i, j;
	assert(!exprEdUI.advMode);

	// first collapse all lines
	for (i = 0; i < eaSize(&exprEdUI.lines); i++)
	{
		for (j = 0; j < eaSize(&exprEdUI.lines[i]->entries); j++)
		{
			ExprEdEntryUI *currEntry = exprEdUI.lines[i]->entries[j];
			if (currEntry->closeParen)
			{
				exprEdCollapseEntries(currEntry);
				j--;
			}
		}
	}

	for (i = 0; i < eaSize(&exprEdUI.lines); i++)
	{			
		if (eaSize(&exprEdUI.lines[i]->entries[0]->tokens) > 0)
		{
			exprLineSetOrigStr(exprEdUI.expr->lines[i], ui_TextEntryGetText(exprEdUI.lines[i]->entries[0]->textEntry));
		}
		else
		{
			exprEdDeleteLine(exprEdUI.lines[i]);
			i--;
		}
	}
}

/******
* This function uses the data filled into the advanced mode UI to populate the
* working expression's data.
******/
static void exprEdGetExprAdvanced(void)
{
	static char *expr;
	char *nextLine, *last;

	assert(exprEdUI.advMode);

	estrCopy2(&expr, ui_TextAreaGetText(exprEdUI.textArea));
	exprDestroy(exprEdUI.expr);
	exprEdUI.expr = exprCreate();
	nextLine = strtok_r(expr, "\n", &last);
	while(nextLine)
	{
		if (strlen(nextLine) > 0)
			eaPush(&exprEdUI.expr->lines, exprLineCreate("", nextLine));
		nextLine = strtok_r(NULL, "\n", &last);
	}
}

/********************************************************************
* CALLBACKS
********************************************************************/
static bool exprEdEntryInput(UITextEntry *entry, KeyInput *key)
{
	if (key->type == KIT_EditKey)
	{
		if (key->scancode == INP_UP || key->scancode == INP_DOWN || key->scancode == INP_PGUP || key->scancode == INP_PGDN)
		{
			int s = ui_ListGetSelectedRow(exprEdUI.autocomplete);
			F32 listHeight = ui_WidgetHeight(UI_WIDGET(exprEdUI.autocomplete), exprEdUI.mainWin->widget.height, exprEdUI.mainWin->widget.scale);
			
			if (key->scancode == INP_UP)
				s--;
			else if (key->scancode == INP_DOWN)
				s++;
			else if (key->scancode == INP_PGUP)
				s += 2 - listHeight / EXPR_ED_LIST_ROW_HEIGHT;
			else if (key->scancode == INP_PGDN)
				s += listHeight / EXPR_ED_LIST_ROW_HEIGHT - 2;
			s = CLAMP(s, 0, eaSize(exprEdUI.autocomplete->peaModel) - 1);

			ui_ListSetSelectedRow(exprEdUI.autocomplete, s);

			// adjust scrollbar if new selection is out of view
			if (EXPR_ED_LIST_ROW_HEIGHT * (s + 2) > exprEdUI.autocomplete->widget.sb->ypos + listHeight)
				exprEdUI.autocomplete->widget.sb->ypos = EXPR_ED_LIST_ROW_HEIGHT * (s + 2) - listHeight;
			else if (EXPR_ED_LIST_ROW_HEIGHT * s < exprEdUI.autocomplete->widget.sb->ypos)
				exprEdUI.autocomplete->widget.sb->ypos = EXPR_ED_LIST_ROW_HEIGHT * s;
			return true;
		}
	}

	return ui_TextEntryInput(entry, key);
}

static void exprEdEntryAreaUnfocus(UITextEntry *unused1, UserData unused2)
{
	exprEdRefreshUI();
}

static bool exprEdBlockModeEntryAreaInput(UITextArea *area, KeyInput *key)
{
	if (key->type == KIT_EditKey)
	{
		int bIsControlDown = inpLevelPeek(INP_CONTROL);
		if (key->scancode == INP_RETURN && bIsControlDown)
		{
			exprEdOk(NULL, 0);
			return true;
		}
	}

	return ui_TextAreaInput(area, key);
}

static bool exprEdEntryAreaInput(UITextArea *area, KeyInput *key)
{
	if (key->type == KIT_EditKey)
	{
		if (key->scancode == INP_RETURN)
		{
			exprEdEntryEnter(NULL, getEntryUI(UI_WIDGET((UITextEntry*) area->editable.changedData)));
			return true;
		}
	}

	return ui_TextAreaInput(area, key);
}

static bool exprEdCancel(UIWidget *widget, UserData unused)
{
	exprEdSettingsUpdate(true);	
	
	ui_WidgetQueueFree((UIWidget*) exprEdUI.mainWin);
	
	exprDestroy(exprEdUI.expr);
	eaDestroyEx(&exprEdUI.lines, exprEdLineUIFree);
	if (exprEdUI.autocomplete)
		eaClearStructVoid(exprEdUI.autocomplete->peaModel, parse_ExprEdCompleteEntry);
	exprEdUI.active = false;
	return true;
}

static void exprEdOk(UIButton *button, UserData unused)
{
	if (exprEdUI.advMode)
		exprEdGetExprAdvanced();
	else
		exprEdGetExprBasic();
	if (eaSize(&exprEdUI.expr->lines))
		exprEdUI.exprFunc(exprEdUI.expr, exprEdUI.data);
	else
		exprEdUI.exprFunc(NULL, exprEdUI.data);
	exprEdCancel(NULL, NULL);
}

static void exprEdCopyLineMenu(UIMenuItem *pItem, ExprEdLineUI *lineUI)
{
	char *labelString = NULL;
	estrStackCreate(&labelString);
	estrPrintf(&labelString, "");
	exprEdLineGetCollapsedText(lineUI, &labelString);
	winCopyToClipboard(labelString);
	estrDestroy(&labelString);

}

static void exprEdPasteLineMenu(UIMenuItem *pItem, ExprEdLineUI *lineUI)
{
	int index = eaFind(&exprEdUI.lines, lineUI);
	exprEdDeleteLine(lineUI);
	exprEdAddLineAt(winCopyFromClipboard(), index, true);
	if (eaGet(&exprEdUI.lines,index))
	{
		exprEdExpandAll(exprEdUI.lines[index]);
		ui_ExpanderSetOpened(exprEdUI.lines[index]->expander, true);
		ui_SetFocus(UI_WIDGET(exprEdUI.lines[index]->entries[0]->textEntry));
	}
}


static void exprEdDeleteLineMenu(UIMenuItem *pItem, ExprEdLineUI *lineUI)
{
	bool bWasWorking = false;
	if (exprEdUI.workingEntry && exprEdUI.workingEntry->line == lineUI)
	{
		exprEdUI.workingEntry = NULL;
		bWasWorking = true;
	}

	exprEdDeleteLine(lineUI);
	if (exprEdUI.singleLineIdx >= 0)
	{
		exprEdOk(NULL, NULL);
	}
	else if (bWasWorking)
	{
		exprEdFocusNextField();
	}

}

static void exprEdInsertLineMenu(UIMenuItem *pItem,ExprEdLineUI *lineUI)
{
	int i = eaFind(&exprEdUI.lines, lineUI);
	exprEdAddLineAt("", i, true);
	if (eaGet(&exprEdUI.lines,i))
	{
		ui_ExpanderSetOpened(exprEdUI.lines[i]->expander, true);
	}
}

static void exprEdLineUpMenu(UIMenuItem *pItem, ExprEdLineUI *lineUI)
{
	int i = eaFind(&exprEdUI.lines, lineUI);
	if (i > 0)
	{
		ui_ExpanderGroupRemoveExpander(exprEdUI.lineGroup, lineUI->expander);
		eaMove(&exprEdUI.lines, i - 1, i);
		ui_ExpanderGroupInsertExpander(exprEdUI.lineGroup, lineUI->expander, i - 1);

		exprEdLineRefreshUI(exprEdUI.lines[i]);
		exprEdLineRefreshUI(exprEdUI.lines[i - 1]);
	}
}

static void exprEdLineDownMenu(UIMenuItem *pItem, ExprEdLineUI *lineUI)
{
	int i = eaFind(&exprEdUI.lines, lineUI);
	if (i < eaSize(&exprEdUI.lines) - 1)
	{
		ui_ExpanderGroupRemoveExpander(exprEdUI.lineGroup, lineUI->expander);
		eaMove(&exprEdUI.lines, i + 1, i);
		ui_ExpanderGroupInsertExpander(exprEdUI.lineGroup, lineUI->expander, i + 1);

		exprEdLineRefreshUI(exprEdUI.lines[i]);
		exprEdLineRefreshUI(exprEdUI.lines[i + 1]);
	}
}

static void exprEdLineUIMenuCallback(UIExpander *widget_UNUSED, ExprEdLineUI *lineUI)
{
	if (!exprEdUI.lineContextMenu) {
		exprEdUI.lineContextMenu = ui_MenuCreate(NULL);
	}
	else
	{
		ui_MenuClearAndFreeItems(exprEdUI.lineContextMenu);
	}

	if (exprEdUI.singleLineIdx == -1)
	{	
		ui_MenuAppendItems(exprEdUI.lineContextMenu,
			ui_MenuItemCreate("Copy",UIMenuCallback, exprEdCopyLineMenu, lineUI, NULL),
			ui_MenuItemCreate("Paste",UIMenuCallback, exprEdPasteLineMenu, lineUI, NULL),
			ui_MenuItemCreate("Move Up",UIMenuCallback, exprEdLineUpMenu, lineUI, NULL),
			ui_MenuItemCreate("Move Down",UIMenuCallback, exprEdLineDownMenu, lineUI, NULL),
			ui_MenuItemCreate("Delete Statement",UIMenuCallback, exprEdDeleteLineMenu, lineUI, NULL),
			ui_MenuItemCreate("Insert Statement",UIMenuCallback, exprEdInsertLineMenu, lineUI, NULL),
			NULL);
	}
	else
	{
		ui_MenuAppendItems(exprEdUI.lineContextMenu,
			ui_MenuItemCreate("Copy",UIMenuCallback, exprEdCopyLineMenu, lineUI, NULL),
			ui_MenuItemCreate("Paste",UIMenuCallback, exprEdPasteLineMenu, lineUI, NULL),
			ui_MenuItemCreate("Delete",UIMenuCallback, exprEdDeleteLineMenu, lineUI, NULL),
			NULL);
	}

	ui_MenuPopupAtCursor(exprEdUI.lineContextMenu);
}



/******
* This function has pseudo context-sensitivity, depending on where the cursor is; so if the
* user hits "ENTER" in various places, different things will occur.
******/
static void exprEdEntryEnter(UITextEntry *textEntry, ExprEdEntryUI *entry)
{
	int selected = ui_ListGetSelectedRow(exprEdUI.autocomplete);
	int bIsControlDown = inpLevelPeek(INP_CONTROL);

	if(bIsControlDown)
	{
		exprEdOk(NULL, 0);
	}
	else if (selected >= 0)
	{
		ui_ListSetSelectedRowAndCallback(exprEdUI.autocomplete, selected);
	}
	else if (entry->setButton)
	{
		ui_ButtonClick(entry->setButton);
	}	
	else if(exprEdGetNextEntry())
	{
		exprEdFocusNextField();
	}
	else
	{
		exprEdOk(NULL, 0);
	}
}

static void exprEdEntryDraw(UITextEntry *textEntry, UI_PARENT_ARGS)
{
	ExprEdEntryUI *entryUI = getEntryUI(UI_WIDGET(textEntry));
	UI_GET_COORDINATES( textEntry );

	ui_TextEntryDraw(textEntry, pX, pY, pW, pH, pScale);

	if (entryUI)
	{
		UIStyleFont *pFont = ui_StyleFontGet("Default_Bold");
		if (pFont)
		{
			ui_StyleFontUse(pFont, false, 0);
		}
		if (entryUI->func && entryUI->funcParam != entryUI->func->argc - 1 && (entryUI->openParen || !entryUI->closeParen))
			gfxfont_Printf(x + w, y + h, z + 0.01, scale, scale, 0, ",");		
	}
}

static void exprEdEntryTick(UITextEntry *textEntry, UI_PARENT_ARGS)
{
	if (eaFind(&g_ui_State.freeQueue, (UIWidget*)exprEdUI.mainWin) == -1 &&
		exprEdUI.active && !exprEdUI.advMode)
	{
		ExprEdEntryUI *entryUI = getEntryUI(UI_WIDGET(textEntry));
		int cursorPos = ui_TextEntryGetCursorPosition(textEntry);

		if (entryUI && cursorPos != entryUI->lastCurPos)
			exprEdEntryHandleChange(entryUI);
	}

	ui_TextEntryTick(textEntry, pX, pY, pW, pH, pScale);
}

static void exprEdEntryFree(UIWidget *widget)
{
	exprEdEntryUIFree(getEntryUI(widget));
}

static void exprEdEntryChanged(UITextEntry *entry, ExprEdEntryUI *entryUI)
{
	const char *text = ui_TextEntryGetText(entry);
	ExprEdLineUI *line = entryUI->line;

	// ensure tokens are up to date
	exprEdEntryHandleChange(entryUI);

	// TODO: check for a change in the preceding function name too

	// if changed entry is an open parenthesis, make sure that last token is an open paren or collapse
	if (!collapsing && entryUI->closeParen)
	{
		// check if they've removed the open parenthesis
		if (eaSize(&entryUI->tokens) == 0 || entryUI->tokens[eaSize(&entryUI->tokens) - 1]->type != MULTIOP_PAREN_OPEN)
		{
			int cursorPos = ui_TextEntryGetCursorPosition(entryUI->textEntry);
			exprEdCollapseEntries(entryUI);
			ui_TextEntrySetCursorPosition(entryUI->textEntry, cursorPos);
		}
	}
	// if changed entry is a close parenthesis, make sure that first token is a close paren or collapse
	if (!collapsing && entryUI->openParen)
	{
		// check if they've removed the close parenthesis
		if (eaSize(&entryUI->tokens) == 0 || entryUI->tokens[0]->type != MULTIOP_PAREN_CLOSE)
			exprEdCollapseEntries(entryUI->openParen);
	}
	exprEdLineRefreshUI(line);
}

static void exprEdEntryFocus(UITextEntry *textEntry, ExprEdEntryUI *entryUI)
{
	exprEdEntryHandleChange(entryUI);

	// set working entry for list to know what to update
	exprEdUI.workingEntry = entryUI;	
}

static void exprEdEntryFinished(UITextEntry *textEntry, UserData unused)
{

}

static void exprEdInsertFunc(ExprEdEntryUI *workingEntry, ExprFuncDesc *func, int tokenPos)
{
	ExprEdEntryUI *closeEdEntry;
	char *newStartEntry = NULL, *newEndEntry = NULL;
	size_t newStartEntryLen;
	const char *text = ui_TextEntryGetText(workingEntry->textEntry);
	bool populateParams = false;
	int parenCount;
	int numArgs = func->argc;
	int i, sourcePos;

	MultiVal **tokens = workingEntry->tokens;
	char **locs = workingEntry->locs;
	int paramOffset = 0;
	bool printDot = false;
	ExprEdLineUI *workingLine = workingEntry->line;

	assert(tokenPos >= 0);

	sourcePos = eaFind(&workingLine->entries, workingEntry) + 1;

	// check for "." notation, which would reduce parameter count by 1 (the first parameter disappears)
	if (tokenPos > 0 && func->args[0].type != MULTIOP_NP_STACKPTR)
	{
		// this is necessary to deal with the user clicking a function when the last character in
		// the entry is a '.' (i.e. they're selecting from the full list of functions); in this case,
		// the tokenizer doesn't create a token for the '.'
		if (tokenPos < eaSize(&locs) && locs[tokenPos][0] == '.')
		{
			paramOffset = 1;
			printDot = true;
		}
		else if (tokenPos >= eaSize(&locs) && text[strlen(text) - 1] == '.')
			paramOffset = 1;
	}

	// ensure we're not recreating parameter entries that already exist;
	// if the parameters are already there, we reset types that may have been removed
	if (workingEntry->closeParen && tokenPos >= eaSize(&tokens) - 2)
	{
		int s = eaFind(&workingLine->entries, workingEntry) + 1;
		int e = eaFind(&workingLine->entries, workingEntry->closeParen);
		int paramIdx = paramOffset;

		while (s < e && paramIdx < func->argc)
		{
			workingLine->entries[s]->func = func;
			workingLine->entries[s]->funcParam = paramIdx;

			if (!workingLine->entries[s]->closeParen)
				workingLine->entries[s]->type = func->args[paramIdx].type;
			else
			{
				s = eaFind(&workingLine->entries, workingLine->entries[s]->closeParen);
				workingLine->entries[s]->func = func;
				workingLine->entries[s]->funcParam = paramIdx;
			}
			s++;
			paramIdx++;
		}

		// if we have more parameter entries than function arguments, remove extra ones
		if (paramIdx >= func->argc)
		{
			for (i = e - 1; i >= s; i--)
			{
				ui_ExpanderRemoveChild(workingLine->expander, (UIWidget*) workingLine->entries[i]->textEntry);
				ui_WidgetQueueFree(UI_WIDGET(workingLine->entries[i]->textEntry));
				eaRemove(&workingLine->entries, i);
			}
			if (func->argc - paramOffset == 0)
				exprEdCollapseEntries(workingLine->entries[s - 1]);
		}
		// if we have less entries than function arguments, add some more
		else if (s >= e && paramIdx < func->argc)
			exprEdParamEntriesCreate(func, paramIdx, s, workingEntry);

		// apply the actual change to the function name
		estrCopy2(&newStartEntry, text);
		newStartEntryLen = strlen(text) - (eaSize(&locs) >= 2 ? strlen(locs[eaSize(&locs) - 2]) : 0);
		estrSetSize(&newStartEntry, (int) newStartEntryLen);
		estrConcatf(&newStartEntry, "%s%s(", printDot ? "." : "", func->funcName);
		ui_TextEntrySetTextAndCallback(workingEntry->textEntry, newStartEntry);
		if (func->argc - paramOffset > 0)
			ui_SetFocus((UIWidget*) workingLine->entries[sourcePos]->textEntry);

		exprEdLineRefreshUI(workingLine);
		estrDestroy(&newStartEntry);
		return;
	}

	// split the working entry's text into pre-open-paren and post-close-paren
	newStartEntryLen = strlen(text) - (eaSize(&locs) == tokenPos ? 0 : strlen(locs[tokenPos]));
	estrCopy2(&newStartEntry, text);
	estrSetSize(&newStartEntry, (int) newStartEntryLen);
	estrConcatf(&newStartEntry, "%s%s(", printDot ? "." : "", func->funcName);

	// clear the entry's type if necessary to prevent enclosing a function in special characters
	if (workingEntry->type == MULTI_STRING || workingEntry->type == MULTIOP_LOC_MAT4 || workingEntry->type == MULTIOP_LOC_STRING)
		workingEntry->type = MULTI_NONE;

	// if no args are needed, then just finish off the start entry with a close paren
	tokenPos++;
	if (numArgs <= paramOffset)
	{
		int newCursPos = 1;
		if (tokenPos < eaSize(&tokens) && tokens[tokenPos]->type == MULTIOP_PAREN_OPEN)
			tokenPos++;
		if (tokenPos >= eaSize(&tokens) || tokens[tokenPos]->type != MULTIOP_PAREN_CLOSE)
		{
			estrConcatf(&newStartEntry, ")");
			newCursPos--;
		}
		newCursPos += (int) strlen(newStartEntry);
		if (tokenPos < eaSize(&locs))
			estrConcatf(&newStartEntry, "%s", locs[tokenPos]);

		ui_TextEntrySetTextAndCallback(workingEntry->textEntry, newStartEntry);
		ui_TextEntrySetCursorPosition(workingEntry->textEntry, newCursPos);
		exprEdLineRefreshUI(workingLine);
		estrDestroy(&newStartEntry);

		//exprEdFocusNextField();
		return;
	}

	// create the closing entry
	closeEdEntry = exprEdEntryUICreateText(workingEntry->type, workingLine, false);

	// connect the start and end entries and shuffle pointers around
	if (workingEntry->closeParen)
	{
		int s, e;
		s = eaFind(&workingLine->entries, workingEntry) + 1;
		e = eaFind(&workingLine->entries, workingEntry->closeParen);
		workingEntry->closeParen->openParen = closeEdEntry;
		closeEdEntry->closeParen = workingEntry->closeParen;
		for (; s < e; s++)
		{
			workingLine->entries[s]->parentOpen = closeEdEntry;
			workingLine->entries[s]->parentClose = closeEdEntry->closeParen;
		}
	}
	closeEdEntry->parentOpen = workingEntry->parentOpen;
	closeEdEntry->parentClose = workingEntry->parentClose;
	closeEdEntry->func = workingEntry->func;
	closeEdEntry->funcParam = workingEntry->funcParam;
	workingEntry->closeParen = closeEdEntry;
	closeEdEntry->openParen = workingEntry;

	// determine whether we need to parse existing parameters
	if (tokenPos < eaSize(&tokens) && tokens[tokenPos]->type == MULTIOP_PAREN_OPEN)
	{
		populateParams = true;
		parenCount = 1;
		tokenPos++;
	}

	i = exprEdParamEntriesCreate(func, paramOffset, sourcePos, workingEntry);
	if (i >= 1)
	{
		workingLine->entries[sourcePos]->bIsFirstParam = true;
	}
	eaInsert(&workingLine->entries, closeEdEntry, sourcePos + i);
	ui_ExpanderAddChild(workingLine->expander, (UIWidget*) closeEdEntry->textEntry);
	if (populateParams && i > 0)
		tokenPos = exprEdParamEntriesPopulate(workingLine->entries[sourcePos], tokens, locs, tokenPos);

	// set closing paren entry to post-function text
	estrPrintf(&newEndEntry, "");
	if (tokenPos >= eaSize(&tokens) || tokens[tokenPos]->type != MULTIOP_PAREN_CLOSE)
		estrConcatf(&newEndEntry, ")");
	if (tokenPos < eaSize(&locs))
		estrConcatf(&newEndEntry, "%s", locs[tokenPos]);

	// set text at end
	ui_TextEntrySetTextAndCallback(closeEdEntry->textEntry, newEndEntry);
	ui_TextEntrySetTextAndCallback(workingEntry->textEntry, newStartEntry);

	// set widget focus to first parameter
	if (i > 0)
		ui_SetFocus((UIWidget*) workingLine->entries[sourcePos]->textEntry);
	exprEdLineRefreshUI(workingLine);

	// cleanup
	estrDestroy(&newStartEntry);
	estrDestroy(&newEndEntry);

}


static void exprEdFuncListClicked(UIList *list, UserData unused)
{
	int i;
	ExprEdCompleteEntry *pEntry = ui_ListGetSelectedObject(list);	
	bool found = false;

	ui_ListClearSelected(list);
	
	// validate to make sure that the list is pointing to an EXISTING active entry (i.e. it wasn't deleted)
	if (!exprEdUI.workingEntry)
		return;

	for (i = 0; i < eaSize(&exprEdUI.lines); i++)
	{
		int j;
		for (j = 0; j < eaSize(&exprEdUI.lines[i]->expander->widget.children); j++)
		{
			if (exprEdUI.lines[i]->expander->widget.children[j] == (UIWidget*) exprEdUI.workingEntry->textEntry)
				found = true;
		}
	}
	if (!found || !pEntry)
		return;

	exprEdUI.justSelected = true;

	if (pEntry->entryType == ExprEdComplete_Function)
	{
		ExprFuncDesc *func = pEntry->entryFunc;
		if (!func || exprEdUI.workingEntry->type == MULTIOP_PAREN_CLOSE || exprEdUI.workingEntry->type == MULTIOP_PAREN_OPEN)
		{
			exprEdUI.justSelected = false;
			return;
		}

		// determine where to place the open and close parentheses
		if (func->argc >= 0)
		{
			exprEdInsertFunc(exprEdUI.workingEntry, func, exprEdUI.workingEntry->tokenPos);
		}
	}
	else if (pEntry->entryType == ExprEdComplete_Value)
	{
		ui_TextEntrySetTextAndCallback(exprEdUI.workingEntry->textEntry, pEntry->valueString);
		exprEdFocusNextField();
	}
	else if (pEntry->entryType == ExprEdComplete_Variable)
	{
		ui_TextEntrySetTextAndCallback(exprEdUI.workingEntry->textEntry, pEntry->valueString);
	}

	exprEdUI.justSelected = false;
}

static void exprEdExpandAll(ExprEdLineUI *workingLine)
{
	ExprFuncDesc ***descs;
	int i, j;

	if (eaSize(&exprEdUI.contextFuncs) > 0)
		descs = &exprEdUI.contextFuncs;
	else
		descs = &exprEdAllExprs->funcs;

	for (i = 0; i < eaSize(&workingLine->entries); i++)
	{
		ExprEdEntryUI *workingEntry = workingLine->entries[i];
		for (j = 0; j < eaSize(&workingEntry->tokens) - 2; j++)
		{
			if (MULTI_GET_OPER(workingEntry->tokens[j]->type) == MMO_IDENTIFIER)
			{
				int k;
				for (k = 0; k < eaSize(descs); k++)
				{
					if (stricmp(workingEntry->tokens[j]->str, (*descs)[k]->funcName) == 0)
					{
						exprEdInsertFunc(workingEntry, (*descs)[k], j);
						break;
					}
				}
			}
		}
	}
}

static void exprEdAddLineAndExpand(void)
{
	if(exprEdAddLine("", true))
	{
		ExprEdLineUI *line = eaTail(&exprEdUI.lines);

		if(line)
			ui_ExpanderSetOpened(line->expander, true);
	}
}

static void exprEdAddLineButtonClicked(UIButton *button, UserData unused)
{
	exprEdAddLineAndExpand();
}

AUTO_COMMAND ACMD_NAME("ExprAddLine");
void cmd_ExprEdAddLine(void)
{
	if(!exprEdUI.active)
		return;

	if(exprEdUI.advMode)
		return;

	exprEdAddLineAndExpand();
}

static void exprEdSwitchMode(UIButton *unused1, UserData unused2)
{
	Expression *pOldExpression;
	int x, y, w, h;

	// buffer window settings
	x = exprEdUI.mainWin->widget.x;
	y = exprEdUI.mainWin->widget.y;
	w = exprEdUI.mainWin->widget.width;
	h = exprEdUI.mainWin->widget.height;

	if (!exprEdUI.advMode)
		exprEdGetExprBasic();
	else
		exprEdGetExprAdvanced();
	eaDestroyEx(&exprEdUI.lines, exprEdLineUIFree);
	if (exprEdUI.autocomplete)
		eaClearStructVoid(exprEdUI.autocomplete->peaModel, parse_ExprEdCompleteEntry);
	exprEdUI.advMode = !exprEdUI.advMode;
	exprEdUI.active = false;
	pOldExpression = exprEdUI.expr;

	ui_WidgetQueueFree((UIWidget*) exprEdUI.mainWin);

	exprEdOpen(exprEdUI.exprFunc, exprEdUI.expr, exprEdUI.data, exprEdUI.context, exprEdUI.singleLineIdx);
	exprDestroy(pOldExpression);

	// reset window position/dimensions
	ui_WidgetSetPosition((UIWidget*) exprEdUI.mainWin, x, y);
	ui_WidgetSetDimensions((UIWidget*) exprEdUI.mainWin, w, h);
}

AUTO_COMMAND ACMD_NAME("ExprSwitchMode");
void cmd_ExprEdSwitchMode(void)
{
	if(!exprEdUI.active)
		return;

	exprEdEntryAreaUnfocus(NULL, NULL);
	exprEdSwitchMode(NULL, NULL);
}

void exprEdInitTimeout(UserData unused)
{
	ui_DialogPopup("Timeout", "The expression editor failed to retrieve server data. Grab a programmer or try again.");
}


/********************************************************************
* MAIN FUNCTIONS
********************************************************************/
/******
* This function sends a packet to the server instructing it to send the expressions to the client.
******/
void exprEdInit(void)
{
	ServerCmd_exprEdInitSendFunctions();
}

/******
* This function sets up the expression editor's various data.  This is only invoked after
* the expression descriptions are retrieved from the server.
******/
static void exprEdSetup(void)
{
	UISkin *newSkin;

	if (exprEdAllExprs)
		free(exprEdAllExprs);
	exprEdAllExprs = exprGetAllFuncs();

	// sort them for binary searches
	eaQSort(exprEdAllExprs->funcs, exprFuncDescComp);

	// initialize the skin hash table
	if (!exprEdUI.skins)
	{
		exprEdUI.skins = stashTableCreateInt(20);
		newSkin = ui_SkinCreate(NULL);
		ui_SkinSetEntry(newSkin, colorFromRGBA(0x9999FFFF));
		stashIntAddPointer(exprEdUI.skins, MULTI_STRING, newSkin, false);
		newSkin = ui_SkinCreate(NULL);
		ui_SkinSetEntry(newSkin, colorFromRGBA(0x99FF99FF));
		stashIntAddPointer(exprEdUI.skins, MULTI_INT, newSkin, false);
		newSkin = ui_SkinCreate(NULL);
		ui_SkinSetEntry(newSkin, colorFromRGBA(0x99FF99FF));
		stashIntAddPointer(exprEdUI.skins, MULTI_FLOAT, newSkin, false);
		exprEdUI.otherSkin = ui_SkinCreate(NULL);
		ui_SkinSetEntry(exprEdUI.otherSkin, colorFromRGBA(0xFFFF99FF));
	}
}

static bool exprEdIsInitializedCheckFunc(UserData unused)
{
	return exprEdIsInit;
}

/******
* This function indicates where the initialization steps for the expression editor have
* completed (which includes retrieved the data from the server successfully).
* RETURNS:
*   bool indicating whether expression editor setup has been completed
******/
bool exprEdIsInitialized(void)
{
	return exprEdIsInit;
}

typedef struct ExprEdOpenParams
{
	ExprEdExprFunc exprFunc;
	Expression *expr;
	ExprContext *context;
	UserData data;
	int singleLineIdx;
} ExprEdOpenParams;

static void exprEdOpenExecFunc(ExprEdOpenParams *params)
{
	exprEdOpen(params->exprFunc, params->expr, params->data, params->context, params->singleLineIdx);
	exprDestroy(params->expr);
	free(params);
}

static void exprEdSubstrToggle(UIAnyWidget *widget, UserData data)
{
	if(exprEdUI.workingEntry)
	{
		exprEdEntryHandleChange(exprEdUI.workingEntry);
	}
}

static void exprEdCompleteTypeComboSelect(UIComboBox *comboBox, int selected, void * userData)
{
	exprEdUI.completeType = selected;
	if(exprEdUI.workingEntry)
	{
		exprEdEntryHandleChange(exprEdUI.workingEntry);
	}
}

#endif
/******
* This function opens and returns the expression editor window for use wherever it is needed.
* PARAMS:
*   exprFunc - ExprEdExprFunc callback which is set to the editor's OK button
*   expr - the editor will initialize itself to a copy of this Expression
*   data - any desired UserData that will be passed to exprFunc
*   singleLinIdx - if this int is >= 0, then the editor will be in single-line mode, only
*                  allowing modifications to the line in expr indexed by this value
******/
UIWindow *exprEdOpen(ExprEdExprFunc exprFunc, Expression *expr, UserData data, ExprContext *context, int singleLineIdx)
{
#ifndef NO_EDITORS
	UIWindow *window;
	UILabel *label;
	UIButton *button;
	UITextArea *textArea;
	UIList *list;
	UIListColumn *col;
	UIWidget *focus = NULL;
	int p, i;	

	const int selectedOpWidth = 15;

	assert(expr);	

	// initialize data if necessary
	if (!exprEdIsInit)
	{
		ExprEdOpenParams *params = calloc(1, sizeof(*params));

		// grab expression data from the server and wait until that data is retrieved and
		// processed before continuing to open the expression editor window
		params->exprFunc = exprFunc;
		params->expr = exprCreate();
		exprCopy(params->expr, expr);
		params->data = data;
		params->singleLineIdx = singleLineIdx;
		params->context = context;
		ServerCmd_exprEdInitSendFunctions();
		elUIWaitDialog("Initializing...", "Please wait while the expression editor initializes...", 3600,
			exprEdIsInitializedCheckFunc, NULL, exprEdOpenExecFunc, params, exprEdInitTimeout, NULL);
		return NULL;
	}

	// ensure only one editing window is open
	if (exprEdUI.active)
		return NULL;
	exprEdUI.active = true;

	// set static data
	exprEdUI.exprFunc = exprFunc;
	exprEdUI.expr = exprCreate();
	exprCopy(exprEdUI.expr, expr);
	exprEdUI.data = data;
	exprEdUI.singleLineIdx = singleLineIdx;

	exprEdUI.context = context;
	if (exprEdUI.contextFuncs)
		eaClear(&exprEdUI.contextFuncs);
	if (exprEdUI.contextVars)
		eaClear(&exprEdUI.contextVars);

	// get functions from context
	if (context && context->funcTable)
	{
		StashTableIterator iter;
		StashElement el;

		stashGetIterator(globalFuncTable, &iter);
		while(stashGetNextElement(&iter, &el))
		{
			ExprFuncDesc *desc = stashElementGetPointer(el);
			if(exprAllowedToUseFunc(context->funcTable, desc))
				eaPush(&exprEdUI.contextFuncs, desc);
		}
		eaQSort(exprEdUI.contextFuncs, exprFuncDescComp);
	}

	// get vars from context
	if(context)
		exprContextGetVarsAsEArray(context, &exprEdUI.contextVars);

	// create common elements
	window = ui_WindowCreate("Expression Editor", 0, 0, 400, 500);
	ui_WidgetSetFamily(UI_WIDGET(window), UI_FAMILY_EDITOR);
	ui_WindowSetDimensions(window, 400, 500, 300, 300);
	elUICenterWindow(window);
	ui_WindowSetCloseCallback(window, exprEdCancel, NULL);
	exprEdUI.mainWin = window;
	
	/*label = ui_LabelCreate("Description:", 5, 30);
	ui_WindowAddChild(window, label);

	textEntry = ui_TextEntryCreate("", 0, 30);
	ui_TextEntrySetValidateCallback(textEntry, ui_EditableForceAscii, NULL);
	entryHeight = textEntry->widget.height;
	ui_WidgetSetDimensionsEx((UIWidget*) textEntry, 1, entryHeight, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPaddingEx((UIWidget*) textEntry, 10 + label->widget.width, 5, 0, 0);
	ui_WindowAddChild(window, textEntry);
	exprEdUI.desc = textEntry;*/

	// determine whether to render basic or advanced mode
	if (exprEdUI.advMode)
	{
		char *estr = NULL;
		estrStackCreate(&estr);
		for (i = 0; i < eaSize(&expr->lines); i++)
		{
			estrAppend2(&estr, expr->lines[i]->origStr);
			if (i < eaSize(&expr->lines) - 1)
				estrAppend2(&estr, "\n");
		}
		label = ui_LabelCreate("Block Mode", 5, 5);
		ui_WindowAddChild(window, label);
		button = ui_ButtonCreate("Switch to Line Mode", 5, 0, exprEdSwitchMode, NULL);
		ui_WidgetSetPositionEx((UIWidget*) button, 5, 5, 0, 0, UITopRight);
		ui_WindowAddChild(window, button);
		textArea = ui_TextAreaCreate(estr ? estr : "");
		ui_WidgetSetDimensionsEx((UIWidget*) textArea, 1, 1, UIUnitPercentage, UIUnitPercentage);
		ui_WidgetSetPaddingEx((UIWidget*) textArea, 5, 5, 55, 40);

		textArea->widget.inputF = exprEdBlockModeEntryAreaInput;

		ui_WindowAddChild(window, textArea);
		exprEdUI.textArea = textArea;
		estrDestroy(&estr);
		exprEdUI.autocomplete = NULL;
		exprEdUI.substr = NULL;
		exprEdUI.funcLabel = NULL;
		exprEdUI.lineGroup = NULL;
	}
	else
	{
		label = ui_LabelCreate("Line Mode", 5, 5);
		ui_WindowAddChild(window, label);

		exprEdUI.funcLabel = ui_SMFViewCreate(5,55,500,20);
		ui_WidgetSetDimensionsEx((UIWidget*) exprEdUI.funcLabel, 1, 20 , UIUnitPercentage, UIUnitFixed);
		ui_WindowAddChild(window, exprEdUI.funcLabel );

		exprEdUI.lineGroup = ui_ExpanderGroupCreate();
		ui_WidgetSetDimensionsEx((UIWidget*) exprEdUI.lineGroup, 1, 0.5, UIUnitPercentage, UIUnitPercentage);
		ui_WidgetSetPaddingEx((UIWidget*) exprEdUI.lineGroup, 5, 5, 75, 5);
		ui_WindowAddChild(window, exprEdUI.lineGroup);

		if (exprEdUI.singleLineIdx == -1)
		{		
			button = ui_ButtonCreate("Add New Statement", 5, 0, exprEdAddLineButtonClicked, NULL);
			ui_ExpanderGroupAddWidget(exprEdUI.lineGroup, UI_WIDGET(button));
		}


		// Combo box that selects auto complete mode
		exprEdUI.completeTypeCombo = ui_ComboBoxCreateWithEnum(0,0, 150, ExprEdCompleteEntryTypeEnum, exprEdCompleteTypeComboSelect, NULL);
		ui_WidgetSetPositionEx((UIWidget*) exprEdUI.completeTypeCombo, 0, 0, 0, 0.5, UITopLeft);
		ui_WidgetSetPaddingEx((UIWidget*) exprEdUI.completeTypeCombo, 5, 5, 0, 0);
		ui_WindowAddChild(window, exprEdUI.completeTypeCombo);
		ui_ComboBoxSetSelected(exprEdUI.completeTypeCombo, 0);		
		
		// autocomplete command list
		list = ui_ListCreate(parse_ExprEdCompleteEntry, &exprEdUI.completeEntries, EXPR_ED_LIST_ROW_HEIGHT);
		exprEdUI.autocomplete = list;
		ui_ListSetSelectedCallback(list, exprEdFuncListClicked, NULL);
		col = ui_ListColumnCreate(UIListPTName, "", (intptr_t)"entryType", NULL);		
		col->fWidth = 80;
		ui_ListAppendColumn(list, col);
		col = ui_ListColumnCreate(UIListPTName, "Return Type", (intptr_t)"typeString", NULL);	
		col->fWidth = 100;
		ui_ListAppendColumn(list, col);
		col = ui_ListColumnCreate(UIListPTName, "Name", (intptr_t)"valueString", NULL);
		ui_ListAppendColumn(list, col);
		ui_ListSetSortedColumn(list, 2);
		ui_WidgetSetPositionEx((UIWidget*) list, 0, 0, 0, 0.5, UITopLeft);
		ui_WidgetSetDimensionsEx((UIWidget*) list, 1, 0.5, UIUnitPercentage, UIUnitPercentage);
		ui_WidgetSetPaddingEx((UIWidget*) list, 5, 5, 25, 40);
		ui_WindowAddChild(window, list);

		exprEdUI.substr = ui_CheckButtonCreate(0, 0, "Substring Search", 0);
		ui_CheckButtonSetToggledCallback(exprEdUI.substr, exprEdSubstrToggle, &exprEdUI);
		ui_WidgetSetPositionEx(UI_WIDGET(exprEdUI.substr), 90, 5, 0, 0, UIBottomRight);
		ui_WindowAddChild(window, exprEdUI.substr);

		button = ui_ButtonCreate("Switch to Block Mode", 5, 0, exprEdSwitchMode, NULL);
		ui_WidgetSetPositionEx((UIWidget*) button, 5, 5, 0, 0, UITopRight);
		ui_WindowAddChild(window, button);
		
		// add the lines in the expression
		for (i = 0; i < eaSize(&expr->lines); i++)
		{
			exprEdAddLine(expr->lines[i]->origStr, false);				
		}

		// add a line for an empty expression
		if (i == 0)
		{
			exprEdAddLine("", true);
		}

		while (exprEdUI.singleLineIdx >= eaSize(&exprEdUI.lines))
		{
			exprEdAddLine("", true);
		}

		for (i = 0; i < eaSize(&exprEdUI.lines); i++)
		{
			exprEdExpandAll(exprEdUI.lines[i]);
		}

		if (eaSize(&exprEdUI.lines))
		{
			if (exprEdUI.singleLineIdx > -1)
			{
				ui_ExpanderSetOpened(exprEdUI.lines[exprEdUI.singleLineIdx]->expander, true);
				focus = UI_WIDGET(exprEdUI.lines[exprEdUI.singleLineIdx]->entries[0]->textEntry);
			}
			else
			{
				ui_ExpanderSetOpened(exprEdUI.lines[0]->expander, true);
				focus = UI_WIDGET(exprEdUI.lines[0]->entries[0]->textEntry);
			}
		}
	}

	button = ui_ButtonCreate("Ok", 0, 0, exprEdOk, NULL);
	ui_WidgetSetTooltipString(UI_WIDGET(button), "Press Ctrl+Enter");
	ui_WidgetSetPositionEx((UIWidget*) button, 5, 5, 0, 0, UIBottomRight);
	ui_WindowAddChild(window, button);
	p = button->widget.width;
	button = ui_ButtonCreate("Cancel", 0, 0, exprEdCancel, NULL);
	ui_WidgetSetPositionEx((UIWidget*) button, 10 + p, 5, 0, 0, UIBottomRight);
	ui_WindowAddChild(window, button);
	ui_WindowSetCloseCallback(window, exprEdCancel, NULL);

	exprEdSettingsApply();

	ui_WindowSetModal(window, true);
	ui_WindowShow(window);
	if(focus)
		ui_SetFocus(focus);

	return window;
#else
	return NULL;
#endif
}
#ifndef NO_EDITORS

extern ParseTable parse_ExprLine[];
#define TYPE_parse_ExprLine ExprLine
extern ParseTable parse_ExprFuncDesc[];
#define TYPE_parse_ExprFuncDesc ExprFuncDesc

#endif
AUTO_RUN;
void exprEdAutorun(void)
{
#ifndef NO_EDITORS
	exprEdUI.advMode = false;

	// register with clipboard
	ecbRegisterClipType(ECB_EXPRESSION, exprEdGetExprDisplayName, parse_Expression);
	ecbRegisterClipType(ECB_EXPR_LINE, exprEdGetExprLineDisplayName, parse_ExprLine);

#endif
}
#ifndef NO_EDITORS

/******
* This function registers the various expression functions passed in from the server.
* PARAMS:
*   container - ExprFuncDescContainer holding all of the expression function descriptions to
*               register with the client.
******/
AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD;
void exprEdInitAddFunctions(ExprFuncDescContainer *container)
{
	int i;

	if (!exprEdIsInit)
	{
		for (i = 0; i < eaSize(&container->funcs); i++)
		{
			// copy the function description and register it
			/*
			ExprFuncDesc *tempDesc = calloc(1, sizeof(ExprFuncDesc));
			memcpy(tempDesc, container->funcs[i], sizeof(ExprFuncDesc));
			tempDesc->funcName = strdup(container->funcs[i]->funcName);
			*/
			ExprFuncDesc *tempDesc = StructAlloc(parse_ExprFuncDesc);
			StructCopyAll(parse_ExprFuncDesc, container->funcs[i], tempDesc);
			exprRegisterFunction(tempDesc, false, true);
		}

		exprEdSetup();
		exprEdIsInit = true;
	}
}

// This is just here to allow loading on the client
int encounter_ExprLocationResolveDummy(ExprContext* context, const char* name, Mat4 matOut, const char* blamefile)
{
	devassertmsg(0, "Can't resolve encounter named points or encounters on client");
	return false;
}

#endif
AUTO_RUN;
void registerLocationCallbackEditor(void)
{
#ifndef NO_EDITORS
	exprRegisterLocationPrefix("namedpoint", encounter_ExprLocationResolveDummy, false);
	exprRegisterLocationPrefix("encounter", encounter_ExprLocationResolveDummy, false);
	exprRegisterLocationPrefix("spawnpoint", encounter_ExprLocationResolveDummy, false);
#endif
}

AUTO_COMMAND ACMD_NAME("ExprAddText");
void cmd_ExprEdAddText(const char* text)
{
	char *estr = NULL;

	if(!exprEdUI.active)
		return;

	if(!exprEdUI.workingEntry && !exprEdUI.textArea)
		return;

	estrAllocaCreate(&estr, 2048);

	if(!exprEdUI.advMode && exprEdUI.workingEntry)
	{
		estrPrintf(&estr, "%s%s", ui_TextEntryGetText(exprEdUI.workingEntry->textEntry), text);
		ui_TextEntrySetTextAndCallback(exprEdUI.workingEntry->textEntry, estr);
	}
	else if(exprEdUI.advMode && exprEdUI.textArea)
	{
		estrPrintf(&estr, "%s%s", ui_TextAreaGetText(exprEdUI.textArea), text);
		ui_TextAreaSetTextAndCallback(exprEdUI.textArea, estr);
	}
}

#include "AutoGen/ExpressionEditor_c_ast.c"
