#include "EString.h"
#include "Expression.h"
#include "file.h"
#include "textparser.h"
#include "TokenStore.h"
#include "TextBuffer.h"
#include "objPath.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "ResourceInfo.h"
#include "utilitiesLib.h"
#include "Message.h"

#include "dynFxManager.h"
#include "GraphicsLib.h"

#include "ChatData.h"
#include "input.h"
#include "inputKeyBind.h"
#include "inputMouse.h"
#include "inputGamepad.h"
#include "inputText.h"
#include "UIGenLayoutBox.h"
#include "UIGen_h_ast.h"
#include "UIGenPrivate.h"
#include "UIGenList.h"
#include "UIGenTabGroup.h"
#include "UIGenTabGroup_h_ast.h"
#include "UIGenTextEntry.h"
#include "UIGenTextArea.h"
#include "UIGenSlider.h"
#include "UIGenButton.h"
#include "UIGenCheckButton.h"
#include "UIGenDnD.h"
#include "UIGenMovableBox.h"
#include "UIGenSprite.h"
#include "UIGenJail.h"
#include "UIGenTutorial.h"
#include "UIGenJail_h_ast.h"
#include "UIGenTutorial_h_ast.h"
#include "UIGenScrollView.h"
#include "rand.h"
#include "UIGenText.h"
#include "UIGenSMF.h"

#include "AutoGen/ChatData_h_ast.h"

extern ParseTable parse_UIGenSprite[];
#define TYPE_parse_UIGenSprite UIGenSprite

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

//////////////////////////////////////////////////////////////////////////
// Expression functions for Gens. These should only be called from
// expressions and never from other code.

// Get the name of the gen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetName);
const char *ui_GenExprGetName(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	return pGen->pchName;
}

// True if the gen is in the given state, false if the gen is not.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(InState);
bool ui_GenExprInState(ExprContext *pContext, U32 eState)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	if (pGen && (ui_GenInState(pGen, eState) || ui_GenInGlobalState(eState)))
		return true;
	else
		return false;
}

// True if the given global state is on.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenInGlobalState);
bool ui_GenExprInGlobalState(ExprContext *pContext, U32 eState)
{
	return ui_GenInGlobalState(eState);
}

// True if the given global state is on.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenInGlobalStateName);
bool ui_GenExprInGlobalStateName(ExprContext *pContext, const char* pchState)
{
	return ui_GenInGlobalStateName(pchState);
}

// True if the given gen is in the given state.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenInState);
bool ui_GenExprVarInState(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, U32 eState)
{
	if (pGen)
		return ui_GenInState(pGen, eState) || ui_GenInGlobalState(eState);
	else
		return false;
}

// Set a state on a gen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetState);
void ui_GenExprVarSetState(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, U32 eState, bool bEnabled)
{
	if (pGen && eState)
	{
		ui_GenState(pGen, eState, bEnabled);
	}
}

// Set a state on a gen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetGlobalState);
void ui_GenExprSetGlobalState(ExprContext *pContext, U32 eState, bool bEnabled)
{
	if (eState)
		ui_GenSetGlobalState(eState, bEnabled);
}

// Set a string property of a Gen to be applied after the base state but before any overrides.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetEarlyOverrideString);
void ui_GenExprSetEarlyOverrideString(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchField, const char *pchValue)
{
	MultiVal mv = {0};
	MultiValReferenceString(&mv, pchValue);
	if (!ui_GenSetEarlyOverrideField(pGen, pchField, &mv))
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSetEarlyOverrideString: Invalid structure field %s", pchField);
	}
}

// Set an integer property of a Gen to be applied after the base state but before any overrides.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetEarlyOverrideInt);
void ui_GenExprSetEarlyOverrideInt(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchField, S32 iValue)
{
	MultiVal mv = {0};
	MultiValSetInt(&mv, iValue);
	if (!ui_GenSetEarlyOverrideField(pGen, pchField, &mv))
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSetEarlyOverrideInt: Invalid structure field %s", pchField);
	}
}

// Set a numeric property of a Gen to be applied after the base state but before any overrides.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetEarlyOverrideFloat);
void ui_GenExprSetEarlyOverrideFloat(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchField, F32 fValue)
{
	MultiVal mv = {0};
	MultiValSetFloat(&mv, fValue);
	if (!ui_GenSetEarlyOverrideField(pGen, pchField, &mv))
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSetEarlyOverrideFloat: Invalid structure field %s", pchField);
	}
}

// Set a numeric property of a Gen to be applied after the base state but before any overrides.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetEarlyOverrideAngle);
void ui_GenExprSetEarlyOverrideAngle(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchField, F32 fValue, int eUnit)
{
	if (!ui_GenSetEarlyOverrideAngle(pGen, pchField, fValue, eUnit))
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSetEarlyOverrideAngle: Invalid structure field %s", pchField);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetPositionDeltaX);
S32 ui_GenGetScreenPositionDeltaX(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGenA, SA_PARAM_NN_VALID UIGen *pGenB)
{
	return pGenB->ScreenBox.left - pGenA->ScreenBox.left;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetPositionDeltaY);
S32 ui_GenGetScreenPositionDeltaY(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGenA, SA_PARAM_NN_VALID UIGen *pGenB)
{
	return pGenB->ScreenBox.top - pGenA->ScreenBox.top;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenClearEarlyOverride);
void ui_GenExprClearEarlyOverride(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchField)
{
	if (!ui_GenClearEarlyOverrideField(pGen, pchField))
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenClearEarlyOverride: Invalid structure field %s", pchField);
}

// Set a float on the gen's result. This should rarely be used. It needs to be done every frame,
// and usually in BeforeLayout.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetResultFloat);
void ui_GenExprSetResultFloat(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchField, F32 fValue)
{
	MultiVal mv = {0};
	MultiValSetFloat(&mv, fValue);
	if (!ui_GenSetResultField(pGen, pchField, &mv))
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSetResultFloat: Invalid structure field %s", pchField);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenIsFocused);
bool ui_GenExprIsFocused(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	return ui_GenGetFocus() == pGen;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenNothingFocused);
bool ui_GenExprNothingFocused(ExprContext *pContext)
{
	return ui_GenGetFocus() == NULL;
}

// FIXME: Add static checking to the variable name.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetData);
void ui_GenExprSetData(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchVarName)
{
	ParseTable *pTable;
	void *pStruct = exprContextGetVarPointerAndType(pContext, pchVarName, &pTable);
	if (!(pStruct || pTable))
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSetData: Invalid variable name %s", pchVarName);
		ui_GenSetPointer(pGen, NULL, pTable);
	}
	else if (!pGen)
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSetData: Invalid gen");
		ui_GenSetPointer(pGen, NULL, pTable);
	}
	else
		ui_GenSetPointer(pGen, pStruct, pTable);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetDataFromParent);
bool ui_GenExprSetDataFromParent(SA_PARAM_NN_VALID UIGen *pGen)
{
	UIGen *pParent = pGen->pParent;
	if (pParent)
	{
		ParseTable *pTable = NULL;
		void* pStruct = ui_GenGetPointer(pParent, NULL, &pTable);
		ui_GenSetPointer(pGen, pStruct, pTable);
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal ui_GenExprGetDataCheck(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchVar, const char *pchType, ACMD_EXPR_ERRSTRING errEstr)
{
	ParseTable *pTable = ParserGetTableFromStructName(pchType);
	if (!pTable)
	{
		estrPrintf(errEstr, "GenGetData: Invalid object type: %s.", pchType);
		return ExprFuncReturnError;
	}
	else if (!exprContextHasVar(pContext, pchVar))
	{
		estrPrintf(errEstr, "GenGetData: Invalid variable name: %s.", pchVar);
		return ExprFuncReturnError;
	}
	else
	{
		exprContextSetPointerVarPooled(pContext, pchVar, NULL, pTable, true, true);
		return ExprFuncReturnFinished;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetData) ACMD_EXPR_STATIC_CHECK(ui_GenExprGetDataCheck);
ExprFuncReturnVal ui_GenExprGetData(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchVar, const char *pchType, ACMD_EXPR_ERRSTRING errEstr)
{
	ParseTable *pExpected = ParserGetTableFromStructName(pchType);
	ParseTable *pTable = NULL;
	void *pStruct = exprContextGetVarPointerAndType(pContext, pchVar, &pTable);
	if (!(pStruct || pTable))
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenGetData: Invalid variable name %s", pchVar);
	else if (!pGen)
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenGetData: Invalid gen");
	else if (pTable != pExpected)
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenGetData: Non-matched types: %s != %s", ParserGetTableName(pTable), ParserGetTableName(pExpected));
	else
		exprContextSetPointerVarPooled(pContext, pchVar, pStruct, pTable, true, true);

	return ExprFuncReturnFinished;
}

// Return the texture for the controller button for the given command.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenButtonForCommand);
const char *ui_GenButtonForCommand(ExprContext *pContext, const char *pchCommand, bool bBig)
{
	KeyBind *pBind = keybind_BindForCommand(pchCommand, true, true);
	if (pBind)
	{
		if (bBig)
			return ui_ControllerButtonToBigTexture(pBind->iKey1);
		else
			return ui_ControllerButtonToTexture(pBind->iKey1);
	}
	else
		return "";
}

// Set focus if nothing is focused.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetDefaultFocus);
bool ui_GenExprSetDefaultFocus(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (!ui_GenGetFocus() && !g_ui_State.focused && pGen)
	{
		ui_GenSetFocus(pGen);
		return true;
	}
	return false;
}

// Set focus
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetFocus);
bool ui_GenExprSetFocus(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetFocus(pGen);
	return ui_GenGetFocus() == pGen;
}

// Set focus to NULL
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenUnsetFocus);
void ui_GenExprUnsetFocus(ExprContext *pContext)
{
	ui_GenSetFocus(NULL);
}

// Set the gen to take focus the next time it's created
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetFocusOnCreate);
void ui_GenExprSetFocusOnCreate(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	pGen->bNextFocusOnCreate = true;
}

// Returns the number of columns in the list
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenListColumns);
S32 ui_GenExprListGetColumns(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeList))
	{
		UIGenListState *pState = UI_GEN_STATE(pGen, List);
		return pState ? eaSize(&pState->eaCols) : 0;
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenListColumns: %s is not a list", pGen->pchName);
	}
	return 0;
}

// Returns the size of this gen's list.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenListSize);
S32 ui_GenExprListGetSize(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	ParseTable *pTable;
	void ***peaList = ui_GenGetList(pGen, NULL, &pTable);
	return peaList ? eaSize(peaList) : 0;
}

// Returns the size of displayed elements in the list (including template children and filtered model data).
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenListInstanceSize);
S32 ui_GenExprListInstanceSize(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	switch (pGen->eType)
	{
	case kUIGenTypeList:
		{
			UIGenListState *pListState = UI_GEN_STATE(pGen, List);
			return ui_GenListGetSize(pListState);
		}
	case kUIGenTypeListColumn:
		{
			UIGenListColumnState *pListColumnState = UI_GEN_STATE(pGen, ListColumn);
			return eaSize(&pListColumnState->eaRows);
		}
	case kUIGenTypeLayoutBox:
		{
			UIGenLayoutBoxState *pLayoutBoxState = UI_GEN_STATE(pGen, LayoutBox);
			return eaSize(&pLayoutBoxState->eaInstances);
		}
	case kUIGenTypeTabGroup:
		{
			UIGenTabGroupState *pTabGroupState = UI_GEN_STATE(pGen, TabGroup);
			return eaSize(&pTabGroupState->eaTabs);
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenAddEarlyOverrideChild);
bool ui_GenExprAddEarlyOverrideChild(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID UIGen *pChild)
{
	return pChild && ui_GenAddEarlyOverrideChild(pGen, pChild);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenAddEarlyOverrideChildName);
bool ui_GenExprAddEarlyOverrideChildName(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchChild)
{
	UIGen *pChild = RefSystem_ReferentFromString(UI_GEN_DICTIONARY, pchChild);
	if (!pChild)
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenAddEarlyOverrideChildName: Gen name %s was invalid", pchChild);
		return false;
	}
	else
		return ui_GenAddEarlyOverrideChild(pGen, pChild);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenClearEarlyOverrideChildren);
bool ui_GenExprClearEarlyOverrideChildren(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	return ui_GenClearEarlyOverrideChildren(pGen);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenRemoveEarlyOverrideChild);
bool ui_GenExprGenRemoveEarlyOverrideChild(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID UIGen *pChild)
{
	return pChild && ui_GenRemoveEarlyOverrideChild(pGen, pChild);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetEarlyOverrideFieldColor);
void ui_GenExprSetEarlyOverrideColor(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchField, F32 fRed, F32 fGreen, F32 fBlue, F32 fAlpha)
{
	Color c = {fRed, fGreen, fBlue, fAlpha};
	U32 uiColor = RGBAFromColor(c);
	ui_GenExprSetEarlyOverrideInt(pContext, pGen, pchField, uiColor);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetEarlyOverrideFieldColorRGBA);
void ui_GenExprSetEarlyOverrideColorRGBA(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchField, U32 uiColor)
{
	ui_GenExprSetEarlyOverrideInt(pContext, pGen, pchField, uiColor);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetValue);
bool ui_GenExprSetValue(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchVar, F64 dValue)
{
	UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar);
	if (pGlob)
	{
		pGlob->fFloat = dValue;
		pGlob->iInt = MINMAX(dValue, (F32)INT_MIN, (F32)INT_MAX);
	}
	else
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Variable %s not found in gen %s", pchVar, pGen->pchName);
	return !!pGlob;
}

// Deprecated - use FloatToString.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenFloatToString);
const char *ui_GenExprFloatToString(ExprContext *pContext, const char *pchFormat, F32 dValue)
{  
	char *result;
	char *tmpS = NULL;
	estrStackCreate(&tmpS);
	estrPrintf(&tmpS, FORMAT_OK(pchFormat), dValue); // Note: pchFormat Must contain '%f' to display the value
	result = exprContextAllocScratchMemory(pContext, strlen(tmpS)+1 );
	memcpy(result,tmpS,strlen(tmpS)+1);
	estrDestroy(&tmpS);
	return result;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetString);
bool ui_GenExprSetString(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchVar, const char *pchString)
{
	UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar);
	if (pGlob)
	{
		estrCopy2(&pGlob->pchString, pchString);
	}
	else
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Variable %s not found in gen %s", pchVar, pGen->pchName);
	return !!pGlob;
}

// Append String to the variable.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenAppendString);
bool ui_GenExprAppendString(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchVar, const char *pchString)
{
	UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar);
	if (pGlob)
	{
		estrAppend2(&pGlob->pchString, pchString);
	}
	else
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Variable %s not found in gen %s", pchVar, pGen->pchName);
	return !!pGlob;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenAddString);
bool ui_GenExprAddString(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchVar, const char *pchString)
{
	UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar);
	if(pGlob)
	{
		if(!pGlob->pchString || !strstr(pGlob->pchString,pchString))
		{
			estrAppend2(&pGlob->pchString, pchString);
			estrAppend2(&pGlob->pchString, "%");
		}
	}
	else
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Variable %s not found in gen %s", pchVar, pGen->pchName);

	return !!pGlob;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenFindString);
bool ui_GenExprFindString(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchVar, const char *pchString)
{
	char **ppchPossibleStrings = NULL;
	int i;
	UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar);

	if(!pGlob)
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Variable %s not found in gen %s", pchVar, pGen->pchName);
		return false;
	}

	DivideString(pGlob->pchString,"%",&ppchPossibleStrings, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

	if(!ppchPossibleStrings)
		return false;

	for(i=0;i<eaSize(&ppchPossibleStrings);i++)
	{
		if(strcmp(ppchPossibleStrings[i],pchString) == 0)
		{
			eaDestroyEx(&ppchPossibleStrings,NULL);
			return true;
		}
	}

	eaDestroyEx(&ppchPossibleStrings,NULL);
	return false;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenPickRandomString);
char * ui_GenExprPickRandomString(ExprContext *pContext, const char *pchChoiceString, const char *pchIgnoreVar)
{
	char **ppchPossibleStrings = NULL;
	int i,iIgnore;

	DivideString(pchChoiceString,"%",&ppchPossibleStrings, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

	if(!ppchPossibleStrings)
		return NULL;

	if(eaSize(&ppchPossibleStrings) == 1)
		return ppchPossibleStrings[0];

	iIgnore = eaSize(&ppchPossibleStrings);

	if(pchIgnoreVar)
	{
		for(i=0;i<eaSize(&ppchPossibleStrings);i++)
		{
			if(strcmp(pchIgnoreVar,ppchPossibleStrings[i]) == 0)
			{
				iIgnore = i;
				break;
			}
		}
	}

	i = randomIntRange(0,eaSize(&ppchPossibleStrings)-2);

	if(i >= iIgnore)
		i++;

	eaDestroyEx(&ppchPossibleStrings,NULL);
	return ppchPossibleStrings[i];
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenRemoveString);
bool ui_GenExprRemoveString(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchVar, const char *pchString)
{
	UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar);
	if(pGlob)
	{
		char *pchStart = pGlob->pchString ? strstr(pGlob->pchString,pchString) : NULL;
		if(pchStart)
		{
			char *pchEnd = pchStart + strlen(pchString) + 1; //use the +1 for the added "%" to every string
			estrRemove(&pGlob->pchString, pchStart - pGlob->pchString, pchEnd - pchStart);
		}
	}
	else
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Variable %s not found in gen %s", pchVar, pGen->pchName);

	return !!pGlob;
}

// If String is non-empty, append Join, then String.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenAppendStringWith);
bool ui_GenExprAppendStringWith(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchVar, const char *pchString, const char *pchJoin)
{
	UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar);
	if (pGlob)
	{
		if (*pchString)
		{
			estrAppend2(&pGlob->pchString, pchJoin);
			estrAppend2(&pGlob->pchString, pchString);
		}
	}
	else
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Variable %s not found in gen %s", pchVar, pGen->pchName);
	return !!pGlob;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetStringLength);
S32 ui_GenGetStringLength( ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_STR const char* pchVar )
{
	UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar);
	return pGlob && pGlob->pchString ? (S32)strlen( pGlob->pchString ) : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenStringStartsWith);
bool ui_GenExprStringStartsWith( ExprContext *pContext, SA_PARAM_NN_STR const char* pchStr, SA_PARAM_NN_STR const char* pchStart)
{
	return strStartsWith(pchStr, pchStart);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenAdjustScrollbarByPercentage);
void ui_GenExprAdjustScrollbarByPercentage(ExprContext *pContext, SA_PARAM_NN_VALID UIGenScrollbarState *pScrollState, F32 fPercentage)
{
	F32 fAdjust = pScrollState->fTotalHeight * fPercentage;
	pScrollState->fScrollPosition = CLAMP(pScrollState->fScrollPosition + fAdjust, 0, pScrollState->fTotalHeight);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenAdjustScrollbar);
void ui_GenExprAdjustScrollbar(ExprContext *pContext, SA_PARAM_NN_VALID UIGenScrollbarState *pScrollState, F32 fAdjust)
{
	pScrollState->fScrollPosition = CLAMP(pScrollState->fScrollPosition + fAdjust, 0, pScrollState->fTotalHeight);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenResetScrollbar);
void ui_GenExprResetScrollbar(ExprContext *pContext, SA_PARAM_NN_VALID UIGenScrollbarState *pScrollState)
{
	pScrollState->fScrollPosition = 0;
}

// Set a slider's notch, whether or not its interactive.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSliderSetNonInteractiveNotch);
bool ui_GenExprSliderSetNonInteractiveNotch(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, F32 fValue)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeSlider))
	{
		UIGenSliderState *pState = UI_GEN_STATE(pGen, Slider);
		pState->fNotch = CLAMP(fValue, 0, pState->fMax);
		if (UI_GEN_READY(pGen))
		{
			UIGenSlider *pSlider = UI_GEN_RESULT(pGen, Slider);
			ui_GenRunAction(pGen, pSlider->pOnChanged);
		}
		return true;
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSliderSetNonInteractiveNotch: %s is not a slider", pGen->pchName);
	}
	return false;
}

// Return true if a gen is "ready".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenReady);
bool ui_GenExprReady(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen)
{
	return UI_GEN_READY(pGen);
}

// Return the selected row gen of a list.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenListGetSelectedRow);
SA_RET_NN_VALID UIGen *ui_GenExprListGetSelectedRow(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeList))
	{
		if (UI_GEN_READY(pGen))
		{
			UIGenListState *pState = UI_GEN_STATE(pGen, List);
			UIGen *pSelectedRow = eaGet(&pState->eaRows, pState->iSelectedRow);
			return pSelectedRow && !pState->bUnselected ? pSelectedRow : pGen;
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenListGetSelectedRow: %s is not a list", pGen->pchName);
	}
	return pGen;
}

// Return the selected row gen of a list in a column.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenListColumnGetSelectedRow);
SA_RET_NN_VALID UIGen *ui_GenExprListColumnGetSelectedRow(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, U32 iCol)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeList))
	{
		if (UI_GEN_READY(pGen))
		{
			UIGenListState *pState = UI_GEN_STATE(pGen, List);
			UIGenListColumnState *pColState = UI_GEN_STATE(pState->eaCols[iCol], ListColumn);
			UIGen *pRow = pColState ? eaGet(&pColState->eaRows, pState->iSelectedRow) : NULL;
			return FIRST_IF_SET(pRow, pGen);
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenListColumnGetSelectedRow: %s is not a list", pGen->pchName);
	}
	return pGen;
}

// Return the Y value of the top of a gen
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetTop);
F32 ui_GenExprGetTop(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen)
{
	return pGen ? pGen->ScreenBox.ly : 0;
}

// Return the Y value of the bottom of a gen
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetBottom);
F32 ui_GenExprGetBottom(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen)
{
	return pGen ? pGen->ScreenBox.hy : 0.f;
}

// Return the Y value of the top of a gen
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetLeft);
F32 ui_GenExprGetLeft(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen)
{
	return pGen ? pGen->ScreenBox.lx : 0.f;
}

// Return the Y value of the bottom of a gen
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetRight);
F32 ui_GenExprGetRight(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen)
{
	return pGen ? pGen->ScreenBox.hx : 0.f;
}

// Get the text from a text entry or text area, specifying the FilterProfanity
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetTextAreaTextProfanityFiltered);
const char *ui_GenExprGetTextEntryTextProfanityFiltered(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, bool bFilterProfanity);

// Get the text from a text entry or text area, specifying the FilterProfanity
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetTextEntryTextProfanityFiltered);
const char *ui_GenExprGetTextEntryTextProfanityFiltered(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, bool bFilterProfanity)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry))
	{
		UIGenTextEntryState *pTextEntryState = UI_GEN_STATE(pGen, TextEntry);
		return TextBuffer_GetProfanityFilteredText(pTextEntryState->pBuffer, bFilterProfanity);
	}
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextArea))
	{
		UIGenTextAreaState *pTextAreaState = UI_GEN_STATE(pGen, TextArea);
		return TextBuffer_GetProfanityFilteredText(pTextAreaState->pBuffer, bFilterProfanity);
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenGetTextAreaTextProfanityFiltered/GenGetTextEntryTextProfanityFiltered: %s is not a text entry or text area", pGen->pchName);
		return "";
	}
}

// Get the text from a text entry or text area.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetTextAreaText);
const char *ui_GenExprGetTextEntryText(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen);

// Get the text from a text entry or text area.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetTextEntryText);
const char *ui_GenExprGetTextEntryText(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry))
	{
		UIGenTextEntry *pTextEntry = UI_GEN_RESULT(pGen, TextEntry);
		UIGenTextEntryState *pTextEntryState = UI_GEN_STATE(pGen, TextEntry);
		return TextBuffer_GetProfanityFilteredText(pTextEntryState->pBuffer, 
			pTextEntry && pTextEntry->TextBundle.bFilterProfanity);
	}
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextArea))
	{
		UIGenTextArea *pTextArea = UI_GEN_RESULT(pGen, TextArea);
		UIGenTextAreaState *pTextAreaState = UI_GEN_STATE(pGen, TextArea);
		return TextBuffer_GetProfanityFilteredText(pTextAreaState->pBuffer,
			pTextArea && pTextArea->TextBundle.bFilterProfanity);
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenGetTextAreaText/GenGetTextEntryText: %s is not a text entry or text area", pGen->pchName);
		return "";
	}
}

// Set the text in a text entry or text area.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetTextAreaText);
void ui_GenExprSetTextEntryText(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchText);

// Set the text in a text entry or text area.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetTextEntryText);
void ui_GenExprSetTextEntryText(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchText)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry))
	{
		UIGenTextEntry *pTextEntry = UI_GEN_RESULT(pGen, TextEntry);
		UIGenTextEntryState *pTextEntryState = UI_GEN_STATE(pGen, TextEntry);
		TextBuffer_SetText(pTextEntryState->pBuffer, pchText);
		TextBuffer_CursorToEnd(pTextEntryState->pBuffer);
		if (UI_GEN_READY(pGen))
			ui_GenRunAction(pGen, pTextEntry->pOnChanged);
		ui_GenSetState(pGen, kUIGenStateChanged);
	}
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextArea))
	{
		UIGenTextArea *pTextArea = UI_GEN_RESULT(pGen, TextArea);
		UIGenTextAreaState *pTextAreaState = UI_GEN_STATE(pGen, TextArea);
		TextBuffer_SetText(pTextAreaState->pBuffer, pchText);
		pTextAreaState->bDirty = true;
		TextBuffer_CursorToEnd(pTextAreaState->pBuffer);
		if (UI_GEN_READY(pGen))
			ui_GenRunAction(pGen, pTextArea->pOnChanged);
		ui_GenSetState(pGen, kUIGenStateChanged);
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSetTextAreaText/GenSetTextEntryText: %s is not a text entry or text area", pGen->pchName);
	}
}

// Get the text from a text entry or text area.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetCurrentTextAreaWord);
const char *ui_GenExprGetCurrentTextEntryWord(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen);

// Get the text from a text entry or text area.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetCurrentTextEntryWord);
const char *ui_GenExprGetCurrentTextEntryWord(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry))
	{
		UIGenTextEntry *pTextEntry = UI_GEN_RESULT(pGen, TextEntry);
		UIGenTextEntryState *pTextEntryState = UI_GEN_STATE(pGen, TextEntry);
		return TextBuffer_GetPreviousWordDelim(pTextEntryState->pBuffer, g_pchCommandWordDelimiters);
	}
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextArea))
	{
		UIGenTextArea *pTextArea = UI_GEN_RESULT(pGen, TextArea);
		UIGenTextAreaState *pTextAreaState = UI_GEN_STATE(pGen, TextArea);
		return TextBuffer_GetPreviousWordDelim(pTextAreaState->pBuffer, g_pchCommandWordDelimiters);
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenGetCurrentTextAreaWord/GenGetCurrentTextEntryWord: %s is not a text entry or text area", pGen->pchName);
		return "";
	}
}

// Set the text in a text entry or text area.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenReplaceCurrentTextAreaWord);
void ui_GenExprReplaceCurrentTextEntryWord(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchText);

// Set the text in a text entry or text area.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenReplaceCurrentTextEntryWord);
void ui_GenExprReplaceCurrentTextEntryWord(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchText)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry))
	{
		UIGenTextEntry *pTextEntry = UI_GEN_RESULT(pGen, TextEntry);
		UIGenTextEntryState *pTextEntryState = UI_GEN_STATE(pGen, TextEntry);
		TextBuffer_ReplacePreviousWordDelim(pTextEntryState->pBuffer, pchText, g_pchCommandWordDelimiters);
		if (UI_GEN_READY(pGen))
			ui_GenRunAction(pGen, pTextEntry->pOnChanged);
	}
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextArea))
	{
		UIGenTextArea *pTextArea = UI_GEN_RESULT(pGen, TextArea);
		UIGenTextAreaState *pTextAreaState = UI_GEN_STATE(pGen, TextArea);
		TextBuffer_ReplacePreviousWordDelim(pTextAreaState->pBuffer, pchText, g_pchCommandWordDelimiters);
		pTextAreaState->bDirty = true;
		if (UI_GEN_READY(pGen))
			ui_GenRunAction(pGen, pTextArea->pOnChanged);
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenReplaceCurrentTextAreaWord/GenReplaceCurrentTextEntryWord: %s is not a text entry or text area", pGen->pchName);
	}
}

// Set the text in a text entry or text area.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTextAreaReplaceRange);
void ui_GenExprTextEntryReplaceRange(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, S32 iFrom, S32 iTo, const char *pchReplaceText);

// Set the text in a text entry or text area.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTextEntryReplaceRange);
void ui_GenExprTextEntryReplaceRange(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, S32 iFrom, S32 iTo, const char *pchReplaceText)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry))
	{
		UIGenTextEntry *pTextEntry = UI_GEN_RESULT(pGen, TextEntry);
		UIGenTextEntryState *pTextEntryState = UI_GEN_STATE(pGen, TextEntry);
		TextBuffer_ReplaceRange(pTextEntryState->pBuffer, iFrom, iTo, pchReplaceText);
		if (UI_GEN_READY(pGen))
			ui_GenRunAction(pGen, pTextEntry->pOnChanged);
	}
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextArea))
	{
		UIGenTextArea *pTextArea = UI_GEN_RESULT(pGen, TextArea);
		UIGenTextAreaState *pTextAreaState = UI_GEN_STATE(pGen, TextArea);
		TextBuffer_ReplaceRange(pTextAreaState->pBuffer, iFrom, iTo, pchReplaceText);
		pTextAreaState->bDirty = true;
		if (UI_GEN_READY(pGen))
			ui_GenRunAction(pGen, pTextArea->pOnChanged);
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenTextAreaReplaceRange/GenTextEntryReplaceRange: %s is not a text entry or text area", pGen->pchName);
	}
}

// Get the text from a text entry or text area.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTextAreaAddLink);
const void ui_GenExprTextEntryAddLink(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID const char *pchEncodedLink);

// Get the current ChatData associated with a text entry or text area.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTextEntryAddLink);
const void ui_GenExprTextEntryAddLink(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID const char *pchEncodedLink)
{
	ChatLink *pLink = StructCreate(parse_ChatLink);
	ChatData *pData = NULL;
	ChatLinkInfo *pLinkInfo;
	TextBuffer *pBuffer;
	S32 icpStart, icpLength;
	const unsigned char *pch;

	if (!ChatData_DecodeLink(pchEncodedLink, pLink)) {
		StructDestroy(parse_ChatLink, pLink);
		return;
	}

	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry))
	{
		UIGenTextEntry *pTextEntry = UI_GEN_RESULT(pGen, TextEntry);
		UIGenTextEntryState *pTextEntryState = UI_GEN_STATE(pGen, TextEntry);
		if (!pTextEntryState->pChatData) {
			pTextEntryState->pChatData = StructCreate(parse_ChatData);
		}
		pData = pTextEntryState->pChatData;
		pBuffer = pTextEntryState->pBuffer;
	}
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextArea))
	{
		UIGenTextArea *pTextArea = UI_GEN_RESULT(pGen, TextArea);
		UIGenTextAreaState *pTextAreaState = UI_GEN_STATE(pGen, TextArea);
		if (!pTextAreaState->pChatData) {
			pTextAreaState->pChatData = StructCreate(parse_ChatData);
		}
		pData = pTextAreaState->pChatData;
		pBuffer = pTextAreaState->pBuffer;
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenTextAreaAddLink/GenTextEntryAddLink: %s is not a text entry or text area", pGen->pchName);
		StructDestroy(parse_ChatLink, pLink);
		return;
	}

	pLinkInfo = StructCreate(parse_ChatLinkInfo);
	pLinkInfo->pLink = pLink; // Note: This obviates the need to call StructDestroy(..., pLink)
	if (!pLinkInfo->pLink->pchText || !*pLinkInfo->pLink->pchText) {
		pLinkInfo->pLink->pchText = estrCreateFromStr("[Unknown]");
	}

	icpStart = TextBuffer_GetCursor(pBuffer);
	icpLength = TextBuffer_InsertTextAtCursor(pBuffer, pLinkInfo->pLink->pchText);
	TextBuffer_SetCursor(pBuffer, (S32)(icpStart + icpLength));

	pch = TextBuffer_GetText(pBuffer);
	while (icpStart-- > 0)
		pch += UTF8GetCodepointLength(pch);
	pLinkInfo->iStart = (S32)(pch - TextBuffer_GetText(pBuffer));
	pLinkInfo->iLength = (S32)strlen(pLinkInfo->pLink->pchText);

	eaPush(&pData->eaLinkInfos, pLinkInfo);	
}

// Get the text from a text entry or text area.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTextAreaSetLinkText);
const void ui_GenExprTextEntrySetLinkText(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID const char *pchEncodedLink);

// Get the current ChatData associated with a text entry or text area.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTextEntrySetLinkText);
const void ui_GenExprTextEntrySetLinkText(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID const char *pchEncodedLink)
{
	ChatLink *pLink = StructCreate(parse_ChatLink);
	TextBuffer *pBuffer;
	U32 iStart, iLength;
	char text[256];

	if (!ChatData_DecodeLink(pchEncodedLink, pLink)) {
		StructDestroy(parse_ChatLink, pLink);
		return;
	}

	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry))
	{
		UIGenTextEntry *pTextEntry = UI_GEN_RESULT(pGen, TextEntry);
		UIGenTextEntryState *pTextEntryState = UI_GEN_STATE(pGen, TextEntry);
		pBuffer = pTextEntryState->pBuffer;
	}
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextArea))
	{
		UIGenTextArea *pTextArea = UI_GEN_RESULT(pGen, TextArea);
		UIGenTextAreaState *pTextAreaState = UI_GEN_STATE(pGen, TextArea);
		pBuffer = pTextAreaState->pBuffer;
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenTextAreaSetLinkText/GenTextEntrySetLinkText: %s is not a text entry or text area", pGen->pchName);
		StructDestroy(parse_ChatLink, pLink);
		return;
	}

	//Remove begin and end brackets
	*text = '\0';
	if (*pLink->pchText == '[')
	{
		U32 len;
		strcat(text, pLink->pchText+1);
		text[255] = '\0';
		len = (U32)strlen(text)-1;
		if (text[len] == ']')
		{
			text[len] = '\0';
		}
	}
	else
	{
		strcat(text, pLink->pchText);
		text[255] = '\0';
	}

	TextBuffer_SelectAll(pBuffer);
	iStart = TextBuffer_GetCursor(pBuffer);
	iLength = TextBuffer_InsertTextAtCursor(pBuffer, text);
	TextBuffer_SetCursor(pBuffer, (S32)(iStart + iLength));

	StructDestroy(parse_ChatLink, pLink);
}

// Clear all chat data associated with the text area or text entry
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTextAreaClearChatData);
const void ui_GenExprTextEntryClearChatData(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen);

// Clear all chat data associated with the text area or text entry
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTextEntryClearChatData);
const void ui_GenExprTextEntryClearChatData(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry))
	{
		UIGenTextEntry *pTextEntry = UI_GEN_RESULT(pGen, TextEntry);
		UIGenTextEntryState *pTextEntryState = UI_GEN_STATE(pGen, TextEntry);
		if (pTextEntryState->pChatData) {
			StructDestroy(parse_ChatData, pTextEntryState->pChatData);
			pTextEntryState->pChatData = NULL;
		}
	}
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextArea))
	{
		UIGenTextArea *pTextArea = UI_GEN_RESULT(pGen, TextArea);
		UIGenTextAreaState *pTextAreaState = UI_GEN_STATE(pGen, TextArea);
		if (pTextAreaState->pChatData) {
			StructDestroy(parse_ChatData, pTextAreaState->pChatData);
			pTextAreaState->pChatData = NULL;
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenTextAreaClearChatData/GenTextEntryClearChatData: %s is not a text entry or text area", pGen->pchName);
		return;
	}
}

// Alias: GenTextAreaSetEncodedChatData
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTextAreaSetChatData);
void ui_GenExprTextEntrySetChatData(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_STR const char *pchEncodedChatData);

// Clear all chat data associated with the text area or text entry
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTextAreaSetEncodedChatData);
void ui_GenExprTextEntrySetChatData(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_STR const char *pchEncodedChatData);

// Clear all chat data associated with the text area or text entry
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTextEntrySetEncodedChatData);
void ui_GenExprTextEntrySetChatData(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_STR const char *pchEncodedChatData)
{
	ChatData *pData = StructCreate(parse_ChatData);
	if (!ChatData_Decode(pchEncodedChatData, pData)) {
		StructDestroy(parse_ChatData, pData);
		pData = NULL;
	}

	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry))
	{
		UIGenTextEntry *pTextEntry = UI_GEN_RESULT(pGen, TextEntry);
		UIGenTextEntryState *pTextEntryState = UI_GEN_STATE(pGen, TextEntry);
		if (pTextEntryState->pChatData) {
			StructDestroy(parse_ChatData, pTextEntryState->pChatData);
			pTextEntryState->pChatData = NULL;
		}

		pTextEntryState->pChatData = pData;
	}
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextArea))
	{
		UIGenTextArea *pTextArea = UI_GEN_RESULT(pGen, TextArea);
		UIGenTextAreaState *pTextAreaState = UI_GEN_STATE(pGen, TextArea);
		if (pTextAreaState->pChatData) {
			StructDestroy(parse_ChatData, pTextAreaState->pChatData);
			pTextAreaState->pChatData = NULL;
		}

		pTextAreaState->pChatData = pData;
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenTextAreaSetChatData/GenTextAreaSetEncodedChatData/GenTextEntrySetEncodedChatData: %s is not a text entry or text area", pGen->pchName);
		return;
	}
}

// Update the text entry/area cursor to match the mouse cursor location
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetTextAreaCursorPosition);
S32 ui_GenGetTextEntryCursorPosition(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen);

// Update the text entry/area cursor to match the mouse cursor location
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetTextEntryCursorPosition);
S32 ui_GenGetTextEntryCursorPosition(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	MultiVal *mouseX = exprContextGetSimpleVar(pContext, "MouseX");
	MultiVal *mouseY = exprContextGetSimpleVar(pContext, "MouseY");

	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry))
	{
		UIGenTextEntry *pTextEntry = UI_GEN_RESULT(pGen, TextEntry);
		UIGenTextEntryState *pTextEntryState = UI_GEN_STATE(pGen, TextEntry);

		return TextBuffer_GetCursor(pTextEntryState->pBuffer);
	}
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextArea))
	{
		UIGenTextArea *pTextArea = UI_GEN_RESULT(pGen, TextArea);
		UIGenTextAreaState *pTextAreaState = UI_GEN_STATE(pGen, TextArea);

		return TextBuffer_GetCursor(pTextAreaState->pBuffer);
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenGetTextAreaCursorPosition/GenGetTextEntryCursorPosition: %s is not a text entry or text area", pGen->pchName);
		return 0;
	}
}

// Update the text entry/area cursor to match the mouse cursor location
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenMoveCursorToMouseTextArea);
void ui_GenMoveCursorToMouseTextEntry(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen);

// Update the text entry/area cursor to match the mouse cursor location
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenMoveCursorToMouseTextEntry);
void ui_GenMoveCursorToMouseTextEntry(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	MultiVal *mouseX = exprContextGetSimpleVar(pContext, "MouseX");
	MultiVal *mouseY = exprContextGetSimpleVar(pContext, "MouseY");

	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry))
	{
		ui_GenMoveCursorToScreenLocationTextEntry(pGen, mouseX->int32);
	}
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextArea))
	{
		ui_GenMoveCursorToScreenLocationTextArea(pGen, mouseX->int32, mouseY->int32);
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenMoveCursorToMouseTextArea/GenMoveCursorToMouseTextEntry: %s is not a text entry or text area", pGen->pchName);
	}
}

// Update the text entry/area cursor to match the mouse cursor location
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTextAreaSelectAll);
void ui_GenTextEntrySelectAll(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen);

// Update the text entry/area cursor to match the mouse cursor location
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTextEntrySelectAll);
void ui_GenTextEntrySelectAll(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	TextBuffer *pBuffer = NULL;
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry))
	{
		UIGenTextEntryState *pTextEntryState = UI_GEN_STATE(pGen, TextEntry);
		pBuffer = pTextEntryState->pBuffer;
	}
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextArea))
	{
		UIGenTextAreaState *pTextAreaState = UI_GEN_STATE(pGen, TextArea);
		pBuffer = pTextAreaState->pBuffer;
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenTextAreaSelectAll/GenTextEntrySelectAll: %s is not a text entry or text area", pGen->pchName);
		return;
	}

	if (pBuffer)
		TextBuffer_SelectAll(pBuffer);
}

// Update the text entry/area cursor to match the mouse cursor location
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTextAreaSelectNone);
void ui_GenTextEntrySelectNone(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen);

// Update the text entry/area cursor to match the mouse cursor location
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTextEntrySelectNone);
void ui_GenTextEntrySelectNone(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	TextBuffer *pBuffer = NULL;
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry))
	{
		UIGenTextEntryState *pTextEntryState = UI_GEN_STATE(pGen, TextEntry);
		pBuffer = pTextEntryState->pBuffer;
	}
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextArea))
	{
		UIGenTextAreaState *pTextAreaState = UI_GEN_STATE(pGen, TextArea);
		pBuffer = pTextAreaState->pBuffer;
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenTextAreaSelectNone/GenTextEntrySelectNone: %s is not a text entry or text area", pGen->pchName);
		return;
	}

	if (pBuffer)
		TextBuffer_SelectionClear(pBuffer);
}

// Insert text in a text entry or text area using the cursor as the insertion point.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenInsertTextAreaText);
void ui_GenExprInsertTextEntryText(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchText);

// Insert text in a text entry or text area.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenInsertTextEntryText);
void ui_GenExprInsertTextEntryText(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchText)
{
	if (!pchText || !pchText[0])
	{
		return;
	}

	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry))
	{
		UIGenTextEntry *pTextEntry = UI_GEN_RESULT(pGen, TextEntry);
		UIGenTextEntryState *pTextEntryState = UI_GEN_STATE(pGen, TextEntry);

		TextBuffer_InsertTextAtCursor(pTextEntryState->pBuffer, pchText);
		if (UI_GEN_READY(pGen))
			ui_GenRunAction(pGen, pTextEntry->pOnChanged);
	}
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextArea))
	{
		UIGenTextArea *pTextArea = UI_GEN_RESULT(pGen, TextArea);
		UIGenTextAreaState *pTextAreaState = UI_GEN_STATE(pGen, TextArea);

		TextBuffer_InsertTextAtCursor(pTextAreaState->pBuffer, pchText);
		pTextAreaState->bDirty = true;
		if (UI_GEN_READY(pGen))
			ui_GenRunAction(pGen, pTextArea->pOnChanged);
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenInsertTextAreaText/GenInsertTextEntryText: %s is not a text entry or text area", pGen->pchName);
	}
}

// Simulate pressing Enter in a text entry; also gives it focus.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTextEntrySendEnter);
void ui_GenExprTextEntrySendEnter(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetFocus(pGen);
	inpKeyAddBuf(KIT_EditKey, INP_RETURN, 0, KIA_NONE, 0);
}

// If pChild is in the given LayoutBox, return the child before it. Otherwise, return pChild.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenPreviousInLayoutBox);
SA_RET_NN_VALID UIGen *ui_GenExprPreviousInLayoutBox(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pLayoutBox, SA_PARAM_NN_VALID UIGen *pChild)
{
	if (UI_GEN_IS_TYPE(pLayoutBox, kUIGenTypeLayoutBox))
	{
		UIGenLayoutBoxState *pState = UI_GEN_STATE(pLayoutBox, LayoutBox);
		S32 i;
		for (i = 1; i < eaSize(&pState->eaInstances); i++)
			if (pState->eaInstances[i]->pGen == pChild)
				return pState->eaInstances[i - 1]->pGen;
		return pChild;
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenPreviousInLayoutBox: %s is not a layout box", pLayoutBox ? pLayoutBox->pchName : "Impossible");
		return pChild;
	}
}

// If pChild is in the given LayoutBox, return the child after it. Otherwise, return pChild.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenNextInLayoutBox);
SA_RET_NN_VALID UIGen *ui_GenExprNextInLayoutBox(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pLayoutBox, SA_PARAM_NN_VALID UIGen *pChild)
{
	if (UI_GEN_IS_TYPE(pLayoutBox, kUIGenTypeLayoutBox))
	{
		UIGenLayoutBoxState *pState = UI_GEN_STATE(pLayoutBox, LayoutBox);
		S32 i;
		for (i = 0; i < eaSize(&pState->eaInstances) - 1; i++)
		{
			if (pState->eaInstances[i]->pGen == pChild)
				return pState->eaInstances[i + 1]->pGen;
		}
		return pChild;
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenNextInLayoutBox: %s is not a layout box", pLayoutBox ? pLayoutBox->pchName : "Impossible");
		return pChild;
	}
}

// Move the selected instance forward by one; returns true if it worked.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenLayoutBoxPrevious);
bool ui_GenExprLayoutBoxPrevious(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pLayoutBox)
{
	if (UI_GEN_IS_TYPE(pLayoutBox, kUIGenTypeLayoutBox))
	{
		UIGenLayoutBoxState *pState = UI_GEN_STATE(pLayoutBox, LayoutBox);
		if (pState->iSelected - 1 >= 0)
		{
			pState->iSelected--;
			ui_GenTutorialEvent(NULL, pLayoutBox, kUIGenTutorialEventTypeSelect);
			return true;
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenLayoutBoxPrevious: %s is not a layout box", pLayoutBox ? pLayoutBox->pchName : "Impossible");
	}
	return false;
}

// Move the selected instance back by one; returns true if it worked.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenLayoutBoxNext);
bool ui_GenExprLayoutBoxNext(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pLayoutBox)
{
	if (UI_GEN_IS_TYPE(pLayoutBox, kUIGenTypeLayoutBox))
	{
		UIGenLayoutBoxState *pState = UI_GEN_STATE(pLayoutBox, LayoutBox);
		if (pState->iSelected + 1 < eaSize(&pState->eaInstances))
		{
			pState->iSelected++;
			ui_GenTutorialEvent(NULL, pLayoutBox, kUIGenTutorialEventTypeSelect);
			return true;
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenLayoutBoxNext: %s is not a layout box", pLayoutBox ? pLayoutBox->pchName : "Impossible");
	}
	return false;
}

// Get the nth child of the layout box, or its first/last one if out of bounds, or the layout box itself if it has no children.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenLayoutBoxGetInstance);
SA_RET_NN_VALID UIGen *ui_GenExprLayoutBoxGetInstance(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, S32 iInstance)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeLayoutBox))
	{
		UIGenLayoutBoxState *pState = UI_GEN_STATE(pGen, LayoutBox);
		if (eaSize(&pState->eaInstances))
		{
			S32 i = CLAMP(iInstance, 0, eaSize(&pState->eaInstances) - 1);
			UIGenLayoutBoxInstance *pInstance = eaGet(&pState->eaInstances, i);
			return SAFE_MEMBER(pInstance, pGen);
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenLayoutBoxGetInstance: %s is not a layout box", pGen->pchName);
	}
	return pGen;
}

// Get the nth row of a list, or its first/last one if out of bounds, or the list itself if it has no children.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenListGetRow);
SA_RET_NN_VALID UIGen *ui_GenExprListGetRow(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, S32 iRow)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeList))
	{
		UIGenListState *pState = UI_GEN_STATE(pGen, List);
		if (eaSize(&pState->eaRows))
		{
			iRow = CLAMP(iRow, 0, eaSize(&pState->eaRows) - 1);
			return eaGet(&pState->eaRows, iRow);
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenListGetRow: %s is not a list", pGen->pchName);
	}
	return pGen;
}

// Act as if the button was clicked.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenButtonClick);
bool ui_GenExprButtonClick(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeButton))
	{
		ui_GenButtonClick(pGen);
		return true;
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenButtonClick: %s is not a button", pGen->pchName);
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSelectChildInLayoutBox);
SA_RET_NN_VALID UIGen *ui_GenExprSelectChildInLayoutBox(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pLayoutBox, int iChild)
{
	if(UI_GEN_IS_TYPE(pLayoutBox, kUIGenTypeLayoutBox))
	{
		UIGenLayoutBoxState *pState = UI_GEN_STATE(pLayoutBox, LayoutBox);
		
		if(iChild < eaSize(&pState->eaInstances) && iChild >= 0)
			return pState->eaInstances[iChild]->pGen;
		else
		{
			ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSelectChildInLayoutBox: Child number '%d' is invalid, and is not an instanced child of %s", iChild, pLayoutBox ? pLayoutBox->pchName : "Impossible");
			return pLayoutBox;
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSelectChildInLayoutBox: %s is not a layout box", pLayoutBox ? pLayoutBox->pchName : "Impossible");
		return pLayoutBox;
	}

}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetDataFromDictionary);
void ui_GenExprSetDataFromDictionary(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchDictionary, const char *pchKey)
{
	ParseTable *pTable = RefSystem_GetDictionaryParseTable(pchDictionary);
	void *pData = RefSystem_ReferentFromString(pchDictionary, pchKey);
	ui_GenSetPointer(pGen, pData, pTable);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenClearData);
void ui_GenExprClearData(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetPointer(pGen, NULL, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenLayoutBoxCountInstances);
S32 ui_GenExprGenLayoutBoxCountInstances(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	UIGenLayoutBoxState *pState = UI_GEN_STATE(pGen, LayoutBox);
	return pState ? (eaSize(&pState->eaInstances)) : 0;
}

// Get the number of columns in the the layout box.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenLayoutBoxGetColumnCount);
S32 ui_GenExprLayoutBoxGetColumnCount(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeLayoutBox))
	{
		UIGenLayoutBoxState *pState = UI_GEN_STATE(pGen, LayoutBox);
		return pState->iColumns;
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenLayoutBoxGetColumnCount: %s is not a layout box", pGen->pchName);
	}
	return 0;
}


static void ExprShowVirtualKeyboardCB(const char *pchResult, bool bAccepted, UIGen *pGen)
{
	if (bAccepted)
	{
		if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry))
		{
			UIGenTextEntry *pTextEntry = UI_GEN_RESULT(pGen, TextEntry);
			UIGenTextEntryState *pTextEntryState = UI_GEN_STATE(pGen, TextEntry);
			TextBuffer_SetText(pTextEntryState->pBuffer, pchResult);
			TextBuffer_CursorToEnd(pTextEntryState->pBuffer);
			if (UI_GEN_READY(pGen))
				ui_GenRunAction(pGen, pTextEntry->pOnChanged);
		}
		else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextArea))
		{
			UIGenTextArea *pTextArea = UI_GEN_RESULT(pGen, TextArea);
			UIGenTextAreaState *pTextAreaState = UI_GEN_STATE(pGen, TextArea);
			TextBuffer_SetText(pTextAreaState->pBuffer, pchResult);
			TextBuffer_CursorToEnd(pTextAreaState->pBuffer);
			if (UI_GEN_READY(pGen))
				ui_GenRunAction(pGen, pTextArea->pOnChanged);
		}
	}
}

// Pop up the Xbox virtual keyboard, and put the resulting string in the TextEntry/Area given as the first parameter.
// Valid types of keyboard are "Default", "Email", "Username", "Password", "Phone", and "Numeric".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenShowVirtualKeyboard);
void ui_GenExprShowVirtualKeyboard(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchType, const char *pchTitle, const char *pchPrompt, const char *pchDefault)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry) || UI_GEN_IS_TYPE(pGen, kUIGenTypeTextArea))
		inpShowVirtualKeyboard(pchType, pchTitle, pchPrompt, ExprShowVirtualKeyboardCB, pGen, pchDefault, 256);
	else
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenShowVirtualKeyboard: %s is not a text entry or text area", pGen ? pGen->pchName : "(null)");
}

// Turn a check button on or off.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenCheckButtonSetState);
void ui_GenExprCheckButtonSetState(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, bool bChecked)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeCheckButton) && pGen->pState)
	{
		UIGenCheckButtonState *pState = UI_GEN_STATE(pGen, CheckButton);
		pState->bChecked = bChecked;
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenCheckButtonSetState: %s is not a check button", pGen->pchName);
	}
}

// Get the state of the check button
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenCheckButtonGetState);
bool ui_GenExprCheckButtonGetState(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeCheckButton) && pGen->pState)
	{
		UIGenCheckButtonState *pState = UI_GEN_STATE(pGen, CheckButton);
		return pState->bChecked;
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenCheckButtonGetState: %s is not a check button", pGen->pchName);
	}
	return false;
}

// Mark whether a check button is inconsistent or not.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenCheckButtonSetInconsistent);
void ui_GenExprCheckButtonSetInconsistent(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, bool bInconsistent)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeCheckButton) && pGen->pState)
	{
		UIGenCheckButtonState *pState = UI_GEN_STATE(pGen, CheckButton);
		pState->bInconsistent = bInconsistent;
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenCheckButtonSetInconsistent: %s is not a check button", pGen->pchName);
	}
}

// Get the state of the check button is inconsistent or not.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenCheckButtonGetInconsistent);
bool ui_GenExprCheckButtonGetInconsistent(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeCheckButton) && pGen->pState)
	{
		UIGenCheckButtonState *pState = UI_GEN_STATE(pGen, CheckButton);
		return pState->bInconsistent;
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenCheckButtonGetInconsistent: %s is not a check button", pGen->pchName);
	}
	return false;
}

UIGenTimer* ui_GenGetTimer(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchTimerName)
{
	S32 i;
	for (i = 0; i < eaSize(&pGen->eaTimers); i++)
	{
		UIGenTimer *pTimer = pGen->eaTimers[i];
		if (pTimer->pchName && !stricmp(pchTimerName, pTimer->pchName))
		{
			return pTimer;
		}
	}
	ErrorFilenamef(exprContextGetBlameFile(pContext), "Invalid timer name %s for gen %s", pchTimerName, pGen->pchName);
	return NULL;
}

// sets the elapsed time e.g. if a timer times out in 10 seconds and
// 5.4 seconds have elapsed , this will reset the elapsed from 5.4 to
// e.g. 2 seconds : GenTimerSet(Self,"Foo",2)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTimerGetPercentage);
F32 ui_GenExprTimerGetPercent(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchTimerName);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTimerGetPercent);
F32 ui_GenExprTimerGetPercent(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchTimerName)
{
	UIGenTimer *pTimer = ui_GenGetTimer(pContext, pGen, pchTimerName);
	if (pTimer)
	{
		return pTimer->fCurrent / pTimer->fTime;
	}
	return -1;
}

// sets the elapsed time e.g. if a timer times out in 10 seconds and
// 5.4 seconds have elapsed , this will reset the elapsed from 5.4 to
// e.g. 2 seconds : GenTimerSet(Self,"Foo",2)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTimerSet);
void ui_GenExprTimerSet(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchTimerName, F32 val)
{
	UIGenTimer *pTimer = ui_GenGetTimer(pContext, pGen, pchTimerName);
	if (pTimer)
	{
		pTimer->fCurrent = val;
	}
}

// set the time at which a timer goes off. A UIGenTimer has the
// following fields:
// * fTime : the time the event expires (sets this field)
// * fCurrent : how much time has elapsed
// * bPaused : if the timer is running
//
// each frame more time is added to fCurrent until it passed fTime and
// an event is queued.
// @see ui_GenUpdateTimers
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTimeoutSet);
void ui_GenExprTimeoutSet(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchTimerName, F32 val)
{
	UIGenTimer *pTimer = ui_GenGetTimer(pContext, pGen, pchTimerName);
	if (pTimer)
	{
		pTimer->fTime = val;
		pTimer->fCurrent = 0.f;
	}
}

// Pause or unpause a timer.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTimerPause);
void ui_GenExprTimerPause(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchTimerName, bool bPause)
{
	UIGenTimer *pTimer = ui_GenGetTimer(pContext, pGen, pchTimerName);
	if (pTimer)
	{
		pTimer->bPaused = bPause;
	}
}

// Send a message to a gen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSendMessage);
bool ui_GenExprSendMessage(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchMessage)
{
	return ui_GenSendMessage(pGen, pchMessage);
}

// Send a message to a list row, layoutbox instance, or tab
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSendMessageToInstance);
bool ui_GenExprSendMessageToInstance(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchMessage, int iIndex);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSendMessageToListRow);
bool ui_GenExprSendMessageToInstance(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchMessage, int iIndex);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSendMessageToTab);
bool ui_GenExprSendMessageToInstance(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchMessage, int iIndex)
{
	UIGen *pTarget = NULL;
	if (UI_GEN_READY(pGen))
	{
		if (UI_GEN_IS_TYPE(pGen, kUIGenTypeList))
		{
			// TODO: This does not properly dispatch messages to list columns
			UIGenListState *pState = UI_GEN_STATE(pGen, List);
			pTarget = eaGet(&pState->eaRows, iIndex);
		}
		else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeLayoutBox))
		{
			UIGenLayoutBoxState *pState = UI_GEN_STATE(pGen, LayoutBox);
			UIGenLayoutBoxInstance *pInstance = eaGet(&pState->eaInstances, iIndex);
			if (pInstance)
				pTarget = pInstance->pGen;
			else
				ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSendMessageToXX: %s - no element %d", pGen->pchName, iIndex);
		}
		else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTabGroup))
		{
			UIGenTabGroupState *pState = UI_GEN_STATE(pGen, TabGroup);
			pTarget = eaGet(&pState->eaTabs, iIndex);
		}
		else
		{
			ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSendMessageToXX: %s is not a list, layout box, or tab group", pGen->pchName);
		}
	}
	if (pTarget)
		return ui_GenSendMessage(pTarget, pchMessage);
	else
		return false;
}

// Send a message to the selected list row, layoutbox instance, or tab
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSendMessageToSelectedInstance);
bool ui_GenExprSendMessageToSelectedInstance(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchMessage);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSendMessageToSelectedListRow);
bool ui_GenExprSendMessageToSelectedInstance(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchMessage);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSendMessageToSelectedTab);
bool ui_GenExprSendMessageToSelectedInstance(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchMessage)
{
	UIGen *pTarget = NULL;
	if (UI_GEN_READY(pGen))
	{
		if (UI_GEN_IS_TYPE(pGen, kUIGenTypeList))
		{
			// TODO: This does not properly dispatch messages to list columns
			UIGenListState *pState = UI_GEN_STATE(pGen, List);
			pTarget = eaGet(&pState->eaRows, pState->iSelectedRow);
		}
		else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeLayoutBox))
		{
			UIGenLayoutBoxState *pState = UI_GEN_STATE(pGen, LayoutBox);
			if (pState->iSelected >= 0)
			{
				UIGenLayoutBoxInstance *pInstance = eaGet(&pState->eaInstances, pState->iSelected);
				if (pInstance)
					pTarget = pInstance->pGen;
				else
					ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSendMessageToSelectedXX: %s - no element %d", pGen->pchName, pState->iSelected);
			}
		}
		else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTabGroup))
		{
			UIGenTabGroupState *pState = UI_GEN_STATE(pGen, TabGroup);
			pTarget = eaGet(&pState->eaTabs, pState->iSelectedTab);
		}
		else
		{
			ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSendMessageToSelectedXX: %s is not a list, layout box, or tab", pGen->pchName);
		}
	}
	if (pTarget)
		return ui_GenSendMessage(pTarget, pchMessage);
	else
		return false;
}

// Show a gen on the window layer.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenAddWindow);
bool ui_GenExprAddWindow(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pWindow)
{
	return ui_GenAddWindow(pWindow);
}

// Show a gen on the window layer at a specific location.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenAddWindowPos);
bool ui_GenExprAddWindowPos(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pWindow, F32 fPercentX, F32 fPercentY)
{
	return ui_GenAddWindowPos(pWindow, fPercentX, fPercentY);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenAddWindowId);
bool ui_GenExprAddWindowId(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pWindow, S32 iWindowId)
{
	return ui_GenAddWindowId(pWindow, iWindowId);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenAddWindowIdPos);
bool ui_GenExprAddWindowIdPos(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pWindow, S32 iWindowId, F32 fPercentX, F32 fPercentY)
{
	return ui_GenAddWindowIdPos(pWindow, iWindowId, fPercentX, fPercentY);
}

// Hide a gen on the window layer.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenRemoveWindow);
bool ui_GenExprRemoveWindow(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pWindow)
{
	return ui_GenRemoveWindow(pWindow, false);
}

// Hide a gen on the window layer, forcefully. This is only safe during
// the Layout phase.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenForceRemoveWindow);
bool ui_GenExprForceRemoveWindow(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pWindow)
{
	return ui_GenRemoveWindow(pWindow, true);
}

// Show a gen on the modal layer.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenAddModal);
bool ui_GenExprAddModal(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pWindow)
{
	return ui_GenAddModalPopup(pWindow);
}

// Hide a gen on the modal layer.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenRemoveModal);
bool ui_GenExprRemoveModal(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pWindow)
{
	return ui_GenRemoveWindow(pWindow, false);
}

// Set the cursor texture and overlay, and hot spot. This must be called every frame.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetCursor);
void ui_GenExprSetCursor(ExprContext *pContext, const char *pchTexture, const char *pchOverlay, S32 iHotX, S32 iHotY)
{
	ui_SetCursor(pchTexture, pchOverlay, iHotX, iHotY, 0xFFFFFFFF, 0xFFFFFFFF);
}

// Start a drag, specifying all possible payload parameters.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenDragStart);
void ui_GenExprDragStart(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pSource,
						 const char *pchType, const char *pchPayload, S32 iIntPayload, F32 fFloatPayload,
						 const char *pchVariable, const char *pchCursor, S32 eButton)
{
	ParseTable *pTable = NULL;
	void *pStruct = pchVariable ? exprContextGetVarPointerAndType(pContext, pchVariable, &pTable) : NULL;
	ui_GenDragDropStart(pSource, pchType, pchPayload, iIntPayload, fFloatPayload, pStruct, pTable, pchCursor, eButton, 0, 0);
}

// Start a drag with a string payload.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenDragStringStart);
void ui_GenExprDragStringStart(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pSource,
							   const char *pchType, const char *pchPayload, const char *pchCursor, S32 eButton)
{
	ui_GenDragDropStart(pSource, pchType, pchPayload, 0, 0.f, NULL, NULL, pchCursor, eButton, 0, 0);
}

// Start a drag with a string payload.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenDragIntStart);
void ui_GenExprDragIntStart(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pSource,
	const char *pchType, int iPayload, const char *pchCursor, S32 eButton)
{
	ui_GenDragDropStart(pSource, pchType, "", iPayload, 0.f, NULL, NULL, pchCursor, eButton, 0, 0);
}

// Start a drag with a string payload and a hotspot.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenDragStringStartHot);
void ui_GenExprDragStringStartHot(	ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pSource,
									const char *pchType, const char *pchPayload, const char *pchCursor, S32 eButton, int hotX, int hotY)
{
	ui_GenDragDropStart(pSource, pchType, pchPayload, 0, 0.f, NULL, NULL, pchCursor, eButton, hotX, hotY);
}

// Determine if a specific UIGen was the source of the active drag
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenIsDragSource);
bool ui_GenExprIsDragSource(SA_PARAM_NN_VALID UIGen *pGen)
{
	return ui_GenDragDropGetSource() == pGen;
}

// Return true if the given texture exists, false otherwise.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(TextureExists);
bool ui_TextureExists(ExprContext *pContext, const char *pchName)
{
	return texFindAndFlag(pchName, false, 0) != NULL;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal ui_GenExprSetEarlyOverrideDimensionCheck(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchField, F32 fMagnitude, U32 eUnit, ACMD_EXPR_ERRSTRING errEstr)
{
	S32 iColumn = -1;
	bool bFound = ParserFindColumn(parse_UIGenInternal, pchField, &iColumn);
	if (iColumn < 0 || !bFound)
	{
		estrPrintf(errEstr, "Invalid field name %s", pchField);
		return ExprFuncReturnError;
	}
	else if (parse_UIGenInternal[iColumn].subtable != parse_UISizeSpec)
	{
		estrPrintf(errEstr, "Field %s is not a dimension specification", pchField);
		return ExprFuncReturnError;
	}
	return ExprFuncReturnFinished;
}

// Set a dimension on a UIGen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetEarlyOverrideDimension) ACMD_EXPR_STATIC_CHECK(ui_GenExprSetEarlyOverrideDimensionCheck);
ExprFuncReturnVal ui_GenExprSetEarlyOverrideDimension(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchField, F32 fMagnitude, U32 eUnit, ACMD_EXPR_ERRSTRING errEstr)
{
	ui_GenSetEarlyOverrideDimension(pGen, pchField, fMagnitude, eUnit);
	return ExprFuncReturnFinished;
}

// Get the gen's width; from its screen box if available, otherwise from its position's width magnitude.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetWidth);
F32 ui_GenExprGetWidth(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	return pGen->pResult ? CBoxWidth(&pGen->ScreenBox) : ui_GenGetBase(pGen, false)->pos.Width.fMagnitude;
}

// Get the gen's height; from its screen box if available, otherwise from its position's height magnitude.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetHeight);
F32 ui_GenExprGetHeight(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	return pGen->pResult ? CBoxHeight(&pGen->ScreenBox) : ui_GenGetBase(pGen, false)->pos.Height.fMagnitude;
}

// Get the gen's width including any padding; from its unpadded screen box if available, otherwise from its position's width magnitude.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetTotalPadWidth);
F32 ui_GenExprGetTotalPadWidth(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	return pGen->pResult ? CBoxWidth(&pGen->UnpaddedScreenBox) - CBoxWidth(&pGen->ScreenBox): 0;
}

// Get the gen's height including any padding; from its unpadded screen box if available, otherwise from its position's height magnitude.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetTotalPadHeight);
F32 ui_GenExprGetTotalPadHeight(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	return pGen->pResult ? CBoxHeight(&pGen->UnpaddedScreenBox) - CBoxHeight(&pGen->ScreenBox) : 0;
}

// Set the sort field and order for this gen's list.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetListSort);
void ui_GenExprSetListSort(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchField, U32 eSort)
{
	ui_GenSetListSort(pGen, pchField, eSort);
}

// Set only the sort order of the list
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetListSortOrder);
void ui_GenExprSetListSortOrder(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, U32 eSort)
{
	ui_GenSetListSortOrder(pGen, eSort);
}

// DEPRECATED: Set the basic texture override of a GenSprite. This is rarely used, e.g. for headshots.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSpriteSetBasicTexture);
void ui_GenExprSpriteSetBasicTexture(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID BasicTexture *pTexture)
{
	UIGenSpriteState *pState = UI_GEN_STATE(pGen, Sprite);
	if (pState)
	{
		pState->TextureState.pBasicTexture = pTexture;
	}
}

// DEPRECATED: Set the basic texture override of a GenSprite. This is rarely used, e.g. for headshots.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSpriteSetBasicTextureByName);
void ui_GenExprSpriteSetBasicTextureByName(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pcTextureName)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeSprite))
	{
		UIGenSprite *pSprite = UI_GEN_RESULT(pGen, Sprite);
		UIGenSpriteState *pState = UI_GEN_STATE(pGen, Sprite);
		if (pSprite && pState)
		{
			pSprite->TextureBundle.pchTexture = allocAddString(pcTextureName);//StructAllocString(pcTextureName);
			pState->TextureState.pBasicTexture = NULL;
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSpriteSetBasicTextureByName: %s is not a sprite", pGen->pchName);
	}
}

// DEPRECATED: Remove the basic texture override of a GenSprite. This is rarely used, e.g. for headshots.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSpriteRemoveBasicTexture);
void ui_GenExprSpriteClearBasicTexture(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeSprite))
	{
		UIGenSpriteState *pState = UI_GEN_STATE(pGen, Sprite);
		if (pState)
		{
			pState->TextureState.pBasicTexture = NULL;
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSpriteRemoveBasicTexture: %s is not a sprite", pGen->pchName);
	}
}

// Set the mask override of a GenSprite.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSpriteSetMaskByName);
void ui_GenExprSpriteSetMaskByName(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char* pchMaskName)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeSprite))
	{
		UIGenSprite *pSprite = UI_GEN_RESULT(pGen, Sprite);
		UIGenSpriteState *pState = UI_GEN_STATE(pGen, Sprite);
		if (pSprite && pState)
		{
			pSprite->TextureBundle.pchMask = allocAddString(pchMaskName);
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSpriteSetMaskByName: %s is not a sprite", pGen->pchName);
	}
}

// Sets the bottom most layer on the sprite to the given texture and color
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSpriteSetBackgroundByInt);
void ui_GenExprSpriteSetBackgroundByInt(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char* pchTextureName, U32 uColor)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeSprite))
	{
		UIGenSprite *pSprite = UI_GEN_RESULT(pGen, Sprite);
		UIGenSpriteState *pState = UI_GEN_STATE(pGen, Sprite);
		if (pSprite && pState)
		{
			UIGenBundleTexture *pBundle = eaGetStruct(&pSprite->eaLowerLayer, parse_UIGenBundleTexture, 0);
			UIGenBundleTextureState *pTexState = eaGetStruct(&pState->eaLowerLayer, parse_UIGenBundleTextureState, 0);
			char ach[MAX_PATH];
			if (g_texture_name_fixup)
			{
				g_texture_name_fixup(pchTextureName, SAFESTR(ach));
				pchTextureName = allocFindString(ach);
			}
			if (pBundle->pchTexture != pchTextureName
				&& pBundle->uiTopLeftColor != uColor
				&& pBundle->eMode != UITextureModeStretched)
			{
				pBundle->pchTexture = allocFindString(pchTextureName);
				pBundle->uiTopLeftColor = uColor;
				pBundle->eMode = UITextureModeStretched;
				ui_GenBundleTextureUpdate(pGen, pBundle, pTexState);
			}
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSpriteSetBackgroundByInt: %s is not a sprite", pGen->pchName);
	}
}

// Add or set the sprite background layer code override to the given texture and color
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSpriteSetEarlyOverrideBackgroundByInt);
bool ui_GenExprSpriteSetEarlyOverrideBackgroundByInt(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchTextureName, U32 uiColor)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeSprite))
	{
		UIGenSprite *pSprite;
		UIGenSpriteState *pState = UI_GEN_STATE(pGen, Sprite);
		int column = -1;
		if (!devassertmsg(ParserFindColumn(parse_UIGenSprite, "LowerLayer", &column), "LowerLayer does not exist"))
			return false;
		if (pState)
		{
			char ach[MAX_PATH];
			UIGenBundleTexture *pBundle;
			if (!pGen->pCodeOverrideEarly)
				pGen->pCodeOverrideEarly = StructCreateVoid(parse_UIGenSprite);
			pSprite = (UIGenSprite *) pGen->pCodeOverrideEarly;
			if (g_texture_name_fixup)
			{
				g_texture_name_fixup(pchTextureName, SAFESTR(ach));
				pchTextureName = allocFindString(ach);
			}
			pBundle = eaGetStruct(&pSprite->eaLowerLayer, parse_UIGenBundleTexture, 0);
			if (pBundle->pchTexture != pchTextureName
				|| pBundle->uiTopLeftColor != uiColor
				|| pBundle->eMode != UITextureModeStretched)
			{
				pBundle->pchTexture = allocFindString(pchTextureName);
				pBundle->uiTopLeftColor = uiColor;
				pBundle->eMode = UITextureModeStretched;
				pBundle->bBackground = true;
				TokenSetSpecified(parse_UIGenSprite, column, pSprite, -1, true);
				ui_GenMarkDirty(pGen);
			}
			return true;
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSpriteSetEarlyOverrideBackgroundByInt: %s is not a sprite", pGen->pchName);
	}
	return false;
}

// Remove the sprite background layer override
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSpriteClearEarlyOverrideBackground);
bool ui_GenExprSpriteClearEarlyOverrideBackground(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeSprite))
	{
		UIGenSprite *pSprite = UI_GEN_RESULT(pGen, Sprite);
		UIGenSpriteState *pState = UI_GEN_STATE(pGen, Sprite);
		int column = -1;
		if (!devassertmsg(ParserFindColumn(parse_UIGenSprite, "LowerLayer", &column), "LowerLayer does not exist"))
			return false;
		if (pSprite && pState)
		{
			if (pGen->pCodeOverrideEarly)
			{
				pSprite = (UIGenSprite *) pGen->pCodeOverrideEarly;
				if (eaSize(&pSprite->eaLowerLayer))
				{
					StructDestroy(parse_UIGenBundleTexture, eaRemove(&pSprite->eaLowerLayer, 0));
					ui_GenMarkDirty(pGen);

					if (eaSize(&pSprite->eaLowerLayer) == 0)
					{
						TokenSetSpecified(parse_UIGenSprite, column, pGen->pCodeOverrideEarly, -1, false);
					}
					return true;
				}
			}
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSpriteClearEarlyOverrideBackground: %s is not a sprite", pGen->pchName);
	}
	return false;
}

// Sets the bottom most layer on the sprite to the given texture and color
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSpriteSetBackgroundByRGBA);
void ui_GenExprSpriteSetBackgroundByRGBA(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char* pchTextureName, F32 fRed, F32 fGreen, F32 fBlue, F32 fAlpha)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeSprite))
	{
		UIGenSprite *pSprite = UI_GEN_RESULT(pGen, Sprite);
		UIGenSpriteState *pState = UI_GEN_STATE(pGen, Sprite);
		if (pSprite && pState)
		{
			Color c = {fRed, fGreen, fBlue, fAlpha};
			U32 uiColor = RGBAFromColor(c);
			char ach[MAX_PATH];
			UIGenBundleTexture *pBundle = eaGetStruct(&pSprite->eaLowerLayer, parse_UIGenBundleTexture, 0);
			UIGenBundleTextureState *pTexState = eaGetStruct(&pState->eaLowerLayer, parse_UIGenBundleTextureState, 0);
			if (g_texture_name_fixup)
			{
				g_texture_name_fixup(pchTextureName, SAFESTR(ach));
				pchTextureName = allocFindString(ach);
			}
			if (pBundle->pchTexture != pchTextureName
				|| pBundle->uiTopLeftColor != uiColor
				|| pBundle->eMode != UITextureModeStretched)
			{
				pBundle->pchTexture = allocFindString(pchTextureName);
				pBundle->uiTopLeftColor = uiColor;
				pBundle->eMode = UITextureModeStretched;
				ui_GenBundleTextureUpdate(pGen, pBundle, pTexState);
			}
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSpriteSetBackgroundByRGBA: %s is not a sprite", pGen->pchName);
	}
}

// Add or set the sprite background layer code override to the given texture and color
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSpriteSetEarlyOverrideBackgroundByRGBA);
bool ui_GenExprSpriteSetEarlyOverrideBackgroundByRGBA(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchTextureName, F32 fRed, F32 fGreen, F32 fBlue, F32 fAlpha)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeSprite))
	{
		Color c = {fRed, fGreen, fBlue, fAlpha};
		U32 uiColor = RGBAFromColor(c);
		return ui_GenExprSpriteSetEarlyOverrideBackgroundByInt(pContext, pGen, pchTextureName, uiColor);
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSpriteSetEarlyOverrideBackgroundByRGBA: %s is not a sprite", pGen->pchName);
	}
	return false;
}

// Clear the mask override of a GenSprite.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSpriteClearMask);
void ui_GenExprSpriteClearMask(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeSprite))
	{
		UIGenSprite *pSprite = UI_GEN_RESULT(pGen, Sprite);
		UIGenSpriteState *pState = UI_GEN_STATE(pGen, Sprite);
		if (pSprite && pState)
		{
			pSprite->TextureBundle.pchMask = NULL;
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSpriteClearMask: %s is not a sprite", pGen->pchName);
	}
}

// Check to see if a gen has a particular variable.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenHasVar);
bool ui_GenHasVar(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchVar)
{
	return eaIndexedFindUsingString(&pGen->eaVars, pchVar) >= 0;
}

// Move the selected list row up by one; returns true if it worked.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenListUp);
bool ui_GenExprListUp(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeList))
	{
		if (UI_GEN_READY(pGen))
		{
			UIGenList *pList = UI_GEN_RESULT(pGen, List);
			UIGenListState *pListState = UI_GEN_STATE(pGen, List);
			S32 iRow = ui_GenListPreviousSelectableRow(pGen, pList, pListState, pListState->iSelectedRow);

			if (iRow >= 0)
				pListState->bSelectedRowNotSelected = false;
			if (iRow >= 0 && iRow != pListState->iSelectedRow)
			{
				ui_GenListSetSelectedRow(pGen, iRow);
				if ( pList->bShowSelectedRow )
					pListState->bForceShowSelectedRow = true;
			
				if(GET_REF(pList->hRowTemplate))
				{
					UIGen *pRowGen = eaGet(&pListState->eaRows, iRow);

					if (UI_GEN_READY(pRowGen))
					{
						UIGenListRow *pRow = UI_GEN_RESULT(pRowGen, ListRow);
						ui_GenRunAction(pRowGen, pRow->pOnSelected);
					}
				}
				else
				{
					S32 i;
					for(i = 0; i < eaSize(&pListState->eaCols); i++)
					{
						UIGenListColumnState *pColState = UI_GEN_STATE(pListState->eaCols[i], ListColumn);
						UIGen *pRowGen = eaGet(&pColState->eaRows, iRow);
						if (UI_GEN_READY(pRowGen))
						{
							UIGenListRow *pRow = UI_GEN_RESULT(pRowGen, ListRow);
							ui_GenRunAction(pRowGen, pRow->pOnSelected);
						}
					}
				}
				return true;
			}
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenListUp: %s is not a list", pGen->pchName);
	}
	return false;
}

// Move the selected list row down by one; returns true if it worked.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenListDown);
bool ui_GenExprListDown(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeList))
	{
		if (UI_GEN_READY(pGen))
		{
			UIGenList *pList = UI_GEN_RESULT(pGen, List);
			UIGenListState *pListState = UI_GEN_STATE(pGen, List);
			S32 iRow = ui_GenListNextSelectableRow(pGen, pList, pListState, pListState->iSelectedRow);
			if (iRow >= 0) {
				pListState->bSelectedRowNotSelected = false;
				pListState->bUnselected = false;
			}
			if (iRow >= 0 && iRow != pListState->iSelectedRow)
			{
				ui_GenListSetSelectedRow(pGen, iRow);
				if ( pList->bShowSelectedRow )
					pListState->bForceShowSelectedRow = true;

				if(GET_REF(pList->hRowTemplate))
				{
					UIGen *pRowGen = eaGet(&pListState->eaRows, iRow);

					if (UI_GEN_READY(pRowGen))
					{
						UIGenListRow *pRow = UI_GEN_RESULT(pRowGen, ListRow);
						ui_GenRunAction(pRowGen, pRow->pOnSelected);
					}
				}
				else
				{
					S32 i;
					for(i = 0; i < eaSize(&pListState->eaCols); i++)
					{
						UIGenListColumnState *pColState = UI_GEN_STATE(pListState->eaCols[i], ListColumn);
						UIGen *pRowGen = eaGet(&pColState->eaRows, iRow);
						if (UI_GEN_READY(pRowGen))
						{
							UIGenListRow *pRow = UI_GEN_RESULT(pRowGen, ListRow);
							ui_GenRunAction(pRowGen, pRow->pOnSelected);
						}
					}
				}
				return true;
			}
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenListDown: %s is not a list", pGen->pchName);
	}
	return false;
}

// Unselect the row in the gen list so that it graphically appears to be unselected.
// The row is still selected internally for keyboard navigation... etc.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenListUnselect);
bool ui_GenExprListUnselect(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeList))
	{
		if (UI_GEN_READY(pGen))
		{
			UIGenList *pList = UI_GEN_RESULT(pGen, List);
			UIGenListState *pListState = UI_GEN_STATE(pGen, List);
			if (pListState)
			{
				pListState->bSelectedRowNotSelected = true;
				return true;
			}
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenListUnselect: %s is not a list", pGen->pchName);
	}
	return false;
}

// Unselect the row in the gen list.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenListHardUnselect);
bool ui_GenExprListHardUnselect(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeList))
	{
		if (UI_GEN_READY(pGen))
		{
			UIGenList *pList = UI_GEN_RESULT(pGen, List);
			UIGenListState *pListState = UI_GEN_STATE(pGen, List);
			if (pListState)
			{
				pListState->bSelectedRowNotSelected = true;
				pListState->bUnselected = true;
				return true;
			}
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenListHardUnselect: %s is not a list", pGen->pchName);
	}
	return false;
}


// Get the index of the selected tab gen; returns -1 if the selected tab is not found.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenListGetSelectedRowIndex);
S32 ui_GenExprListGetSelectedRowIndex(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeList))
	{
		UIGenListState *pState = UI_GEN_STATE(pGen, List);
		return pState->iSelectedRow;
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenListGetSelectedRowIndex: %s is not a list", pGen->pchName);
	}

	return -1;
}

// Set the selected list row; returns true if it worked.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenListSetSelectedRow, GenListSetSelectedRowIndex);
bool ui_GenExprListSetSelectedRow(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, S32 iRow)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeList))
	{
		if (UI_GEN_READY(pGen))
		{
			ui_GenListSetSelectedRow(pGen, iRow);
			return true;
		}
		else if (pGen->pState)
		{
			UIGenListState *pListState = UI_GEN_STATE(pGen, List);
			pListState->iDesiredRowSelection = iRow;
			return true;
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenListSetSelectedRow/GenListSetSelectedRowIndex: %s is not a list", pGen->pchName);
	}
	return false;
}

// Set the selected list row by data; returns true if it worked.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenListSetSelectedRowFilter);
bool ui_GenExprListSetSelectedRowFilter(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, ACMD_EXPR_SUBEXPR_IN pSubExpr)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeList))
	{
		UIGenListState *pState = UI_GEN_STATE(pGen, List);
		if (pState)
		{
			UIGen **eaRows = NULL;
			S32 i;

			if (eaSize(&pState->eaRows) > 0)
			{
				eaRows = pState->eaRows;
			}
			else if (eaSize(&pState->eaCols) > 0)
			{
				UIGenListColumnState *pColState = UI_GEN_STATE(pState->eaCols[0], ListColumn);
				if (eaSize(&pColState->eaRows) > 0)
					eaRows = pColState->eaRows;
			}

			for (i = 0; i < eaSize(&eaRows); i++)
			{
				MultiVal mv = {0};
				// This is probably going to be really slow, since it has to build
				// the full context for the row gen.
				g_GenState.iCurrentContextDepth++;
				exprEvaluateSubExpr(pSubExpr, pContext, ui_GenGetContext(eaRows[i]), &mv, true);
				g_GenState.iCurrentContextDepth--;
				if (MultiValToBool(&mv))
				{
					ui_GenListSetSelectedRow(pGen, i);
					return true;
				}
			}
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenListSetSelectedRowFilter: %s is not a list", pGen->pchName);
	}
	return false;
}

// Activate the selected list row; returns true if it worked.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenListActivate);
bool ui_GenExprListActivate(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeList))
	{
		if (UI_GEN_READY(pGen))
		{
			ui_GenListActivateSelectedRow(pGen);
			return true;
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenListActivate: %s is not a list", pGen->pchName);
	}
	return false;
}

// DEPRECATED: Create a tab group model from a space-separated list of tab titles
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTabGroupFillTabTitles);
void ui_GenExprTabGroupFillTabTitles(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID const char *pchTabTitles)
{
	TabGroupTabTitle ***peaTabTitles = ui_GenGetManagedListSafe(pGen, TabGroupTabTitle);
	char *pchMutableTitleList;
	char *pchContext;
	char *pchTitle;
	S32 i;
	strdup_alloca(pchMutableTitleList, pchTabTitles);
	for (pchTitle = strtok_r(pchMutableTitleList, " ", &pchContext), i = 0; 
		 pchTitle; 
		 pchTitle = strtok_r(NULL, " ", &pchContext), i++)
	{
		TabGroupTabTitle *pchTabTitle = eaGetStruct(peaTabTitles, parse_TabGroupTabTitle, i);
		estrCopy2(&pchTabTitle->pchTitle, pchTitle);
	}

	eaSetSizeStruct(peaTabTitles, parse_TabGroupTabTitle, i);

	ui_GenSetManagedListSafe(pGen, peaTabTitles, TabGroupTabTitle, true);
}

// Return whether or not we can decrement the selected tab in a group.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTabGroupMayDecrementSelectedTab);
bool ui_GenExprTabGroupMayDecrementSelectedTab(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTabGroup))
	{
		UIGenTabGroupState *pState = UI_GEN_STATE(pGen, TabGroup);
		return ui_GenTabGroupMayDecrementSelectedTab(pGen, pState);
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenTabGroupMayDecrementSelectedTab: %s is not a tab group", pGen->pchName);
	}

	return false;
}

// Return whether or not we can increment the selected tab in a group.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTabGroupMayIncrementSelectedTab);
bool ui_GenExprTabGroupMayIncrementSelectedTab(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTabGroup))
	{
		UIGenTabGroupState *pState = UI_GEN_STATE(pGen, TabGroup);
		return ui_GenTabGroupMayIncrementSelectedTab(pGen, pState);
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenTabGroupMayIncrementSelectedTab: %s is not a tab group", pGen->pchName);
	}

	return false;
}

// Decrement the selected tab in a group.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTabGroupDecrementSelectedTab);
bool ui_GenExprTabGroupDecrementSelectedTab(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTabGroup))
	{
		UIGenTabGroupState *pState = UI_GEN_STATE(pGen, TabGroup);
		return ui_GenTabGroupDecrementSelectedTab(pGen, pState);
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenTabGroupDecrementSelectedTab: %s is not a tab group", pGen->pchName);
	}

	return false;
}

// Increment the selected tab in a group.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTabGroupIncrementSelectedTab);
bool ui_GenExprTabGroupIncrementSelectedTab(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTabGroup))
	{
		UIGenTabGroupState *pState = UI_GEN_STATE(pGen, TabGroup);
		return ui_GenTabGroupIncrementSelectedTab(pGen, pState);
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenTabGroupIncrementSelectedTab: %s is not a tab group", pGen->pchName);
	}

	return false;
}

// Get the index of the selected tab gen; returns -1 if the selected tab is not found.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTabGroupGetSelectedTabIndex);
S32 ui_GenExprTabGroupGetSelectedTabIndex(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTabGroup))
	{
		UIGenTabGroupState *pState = UI_GEN_STATE(pGen, TabGroup);
		return pState->iSelectedTab;
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenTabGroupGetSelectedTabIndex: %s is not a tab group", pGen->pchName);
	}

	return -1;
}

// Set the index of the selected tab gen; returns true on success.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTabGroupSetSelectedTabIndex);
bool ui_GenExprTabGroupSetSelectedTabIndex(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, S32 iTab)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTabGroup))
	{
		UIGenTabGroupState *pState = UI_GEN_STATE(pGen, TabGroup);
		return ui_GenTabGroupSetSelectedTabIndex(pGen, pState, iTab);
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenTabGroupSetSelectedTabIndex: %s is not a tab group", pGen->pchName);
	}

	return false;
}

// Set the selected tab; returns true if it worked.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTabGroupSetSelectedTab);
bool ui_GenExprTabGroupSetSelectedTab(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID UIGen *pSelectedTab)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTabGroup))
	{
		UIGenTabGroupState *pState = UI_GEN_STATE(pGen, TabGroup);
		S32 iTab = eaFind(&pState->eaTabs, pSelectedTab);
		//int x = eaSize(&pState->eaTabs);

		// Verify the tab group contains the tab to be selected
		if (iTab >= 0)
		{
			pState->iSelectedTab = iTab;
			ui_GenTutorialEvent(NULL, pGen, kUIGenTutorialEventTypeSelect);
			return true;
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenTabGroupSetSelectedTab: %s is not a tab group", pGen->pchName);
	}

	return false;
}

// Move a slider's notch, if interactive, by the given amount.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSliderAdjustNotch);
bool ui_GenExprSliderAdjustNotch(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, F32 fDiff)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeSlider))
	{
		if (UI_GEN_READY(pGen))
		{
			UIGenSlider *pSlider = UI_GEN_RESULT(pGen, Slider);
			if (pSlider->bNotchInteractive)
			{
				UIGenSliderState *pState = UI_GEN_STATE(pGen, Slider);
				pState->fNotch += fDiff;
				pState->fNotch = CLAMP(pState->fNotch, 0, pState->fMax);
				ui_GenRunAction(pGen, pSlider->pOnChanged);
			}
			return true;
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSliderAdjustNotch: %s is not a slider", pGen->pchName);
	}
	return false;
}

// Run a command in a gen context.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenRunCommand);
bool ui_GenExprRunCommand(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, const char *pchCommand)
{
	if (pGen)
	{
		ui_GenRunCommandInExprContext(pGen, pchCommand);
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetSibling);
SA_RET_NN_VALID UIGen *ui_GenExprGetSibling(SA_PARAM_NN_VALID UIGen *pGen, const char *pchName)
{
	UIGen *pSib = ui_GenGetSiblingEx(pGen, pchName, false, false);
	if(!pSib)
		pSib = pGen;

	return pSib;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetChild);
SA_RET_NN_VALID UIGen *ui_GenExprGetChild(SA_PARAM_NN_VALID UIGen *pGen, const char *pchName)
{
	const char *pchPooledName = allocFindString(pchName);
	UIGen *pChild = pchPooledName ? ui_GenGetChild(pGen, pchPooledName) : NULL;
	if(!pChild)
		pChild = pGen;

	return pChild;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetBaseChild);
SA_RET_NN_VALID UIGen *ui_GenExprGetBaseChild(SA_PARAM_NN_VALID UIGen *pGen, const char *pchName)
{
	const char *pchPooledName = allocFindString(pchName);
	UIGen *pChild = pchPooledName ? ui_GenGetBaseChild(pGen, pchPooledName) : NULL;
	if(!pChild)
		pChild = pGen;

	return pChild;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetEarlyOverrideAssembly);
void ui_GenExprSetEarlyOverrideAssembly(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID UITextureAssembly *pTexAs, U32 uiColor1, U32 uiColor2, U32 uiColor3, U32 uiColor4)
{
	Color4 Tint;
	if (!(uiColor1 || uiColor2 || uiColor3 || uiColor4))
		uiColor1 = 0xFFFFFFFF;
	if (!(uiColor2 || uiColor3 || uiColor4))
		uiColor2 = uiColor3 = uiColor4 = uiColor1;
	Tint.uiTopLeftColor = uiColor1;
	Tint.uiTopRightColor = uiColor2;
	Tint.uiBottomRightColor = uiColor3;
	Tint.uiBottomLeftColor = uiColor4;
	ui_GenSetEarlyOverrideAssembly(pGen, pTexAs, &Tint);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenClearEarlyOverrideAssembly);
void ui_GenExprClearEarlyOverrideAssembly(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetEarlyOverrideAssembly(pGen, NULL, NULL);
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal ui_GenExprGetListFromDictionaryCheck(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchDictionary, const char *pchPrefix, ACMD_EXPR_ERRSTRING errEstr)
{
	ParseTable *pTable = resDictGetParseTable(pchDictionary);
	DictionaryEArrayStruct *pArray = resDictGetEArrayStruct(pchDictionary);
	pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	if (!pTable)
	{
		estrPrintf(errEstr, "%s: Dictionary %s has no parse table", pGen->pchName, pchDictionary);
		return ExprFuncReturnError;
	}
	if (!pArray)
	{
		estrPrintf(errEstr, "%s: Dictionary %s has no array", pGen->pchName, pchDictionary);
		return ExprFuncReturnError;
	}
	return ExprFuncReturnFinished;
}

// Get a list of all elements of the dictionary starting with the given prefix.
// Note that this will not cause references to these objects to be created; it
// will only find already-present things. The list is a copy, so you can sort
// it or filter it afterwards.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetListFromDictionary) ACMD_EXPR_STATIC_CHECK(ui_GenExprGetListFromDictionaryCheck);
ExprFuncReturnVal ui_GenExprGetListFromDictionary(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchDictionary, const char *pchPrefix, ACMD_EXPR_ERRSTRING errEstr)
{
	ParseTable *pTable = resDictGetParseTable(pchDictionary);
	DictionaryEArrayStruct *pArray = resDictGetEArrayStruct(pchDictionary);
	if (!pTable || !pArray)
	{
		ui_GenSetManagedList(pGen, NULL, NULL, false);
	}
	else if (!(pchPrefix && *pchPrefix))
	{
		ui_GenSetManagedList(pGen, &pArray->ppReferents, pTable, false);
	}
	else
	{
		S32 i;
		S32 iColumn = ParserGetTableKeyColumn(pTable);
		void ***peaList = ui_GenGetManagedList(pGen, pTable);
		eaClear(peaList);
		for (i = 0; i < eaSize(&pArray->ppReferents); i++)
		{
			const char *pchKey = TokenStoreGetString(pTable, iColumn, pArray->ppReferents[i], 0, NULL);
			if (pchKey && strStartsWith(pchKey, pchPrefix))
			{
				eaPush(peaList, pArray->ppReferents[i]);
			}
		}
	}

	return ExprFuncReturnFinished;
}

ExprFuncReturnVal ui_GenExprGetListFromEnumCheckInternal(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchEnum, ACMD_EXPR_ERRSTRING errEstr)
{
	StaticDefineInt *pEnum = FindNamedStaticDefine(pchEnum);
	pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	if (!pEnum) {
		estrPrintf(errEstr, "%s: Enum %s does not exist", pGen->pchName, pchEnum);
		return ExprFuncReturnError;
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal ui_GenExprGetListFromEnumInRangeCheck(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchEnum, S32 iBeginIndex, S32 iEndIndex, ACMD_EXPR_ERRSTRING errEstr)
{
	return ui_GenExprGetListFromEnumCheckInternal(pContext, pGen, pchEnum, errEstr);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetListFromEnumInRange") ACMD_EXPR_STATIC_CHECK(ui_GenExprGetListFromEnumInRangeCheck);
ExprFuncReturnVal ui_GenExprGetListFromEnumInRange(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchEnum, S32 iBeginIndex, S32 iEndIndex, ACMD_EXPR_ERRSTRING errEstr)
{
	StaticDefineInt *pEnum = FindNamedStaticDefine(pchEnum);
	char** eaKeys = NULL;
	S32* eaValues = NULL;
	DefineFillAllKeysAndValues(pEnum, &eaKeys, &eaValues);
	if (!pEnum) 
	{
		ui_GenSetManagedList(pGen, NULL, NULL, false);
	} 
	else 
	{
		GenEnum ***peaList = ui_GenGetManagedListSafe(pGen, GenEnum);
		S32 i = 0;
		S32 j = 0;
		while (i < eaSize(&eaKeys)) 
		{
			if (eaValues[i] >= iBeginIndex && eaValues[i] <= iEndIndex)
			{
				if (j >= eaSize(peaList)) 
				{
					GenEnum *pNewEnum = StructCreate(parse_GenEnum);
					eaPush(peaList, pNewEnum);
				}
				(*peaList)[j]->key = eaKeys[i];
				(*peaList)[j]->value = eaValues[i];
				j++;
			}
			i++;
		}
		eaSetSizeStruct(peaList, parse_GenEnum, j);
		ui_GenSetManagedListSafe(pGen, peaList, GenEnum, true);
	}
	eaDestroy(&eaKeys);
	eaiDestroy(&eaValues);
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal ui_GenExprGetListFromEnumCheck(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchEnum, ACMD_EXPR_ERRSTRING errEstr)
{
	return ui_GenExprGetListFromEnumCheckInternal(pContext, pGen, pchEnum, errEstr);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetListFromEnum") ACMD_EXPR_STATIC_CHECK(ui_GenExprGetListFromEnumCheck);
ExprFuncReturnVal ui_GenExprGetListFromEnum(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchEnum, ACMD_EXPR_ERRSTRING errEstr)
{
	return ui_GenExprGetListFromEnumInRange(pContext, pGen, pchEnum, INT_MIN, INT_MAX, errEstr);
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal ui_GenExprGetListFromEnumFromIndexCheck(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchEnum, S32 iBeginIndex, ACMD_EXPR_ERRSTRING errEstr)
{
	return ui_GenExprGetListFromEnumCheckInternal(pContext, pGen, pchEnum, errEstr);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetListFromEnumFromIndex") ACMD_EXPR_STATIC_CHECK(ui_GenExprGetListFromEnumFromIndexCheck);
ExprFuncReturnVal ui_GenExprGetListFromEnumFromIndex(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchEnum, S32 iBeginIndex, ACMD_EXPR_ERRSTRING errEstr)
{
	return ui_GenExprGetListFromEnumInRange(pContext, pGen, pchEnum, iBeginIndex, INT_MAX, errEstr);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetListFromEnumToIndex") ACMD_EXPR_STATIC_CHECK(ui_GenExprGetListFromEnumFromIndexCheck);
ExprFuncReturnVal ui_GenExprGetListFromEnumToIndex(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchEnum, S32 iEndIndex, ACMD_EXPR_ERRSTRING errEstr)
{
	return ui_GenExprGetListFromEnumInRange(pContext, pGen, pchEnum, INT_MIN, iEndIndex, errEstr);
}

// Append the item from the dictionary to the end of this gen's list, if found.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenAppendToListFromDictionary) ACMD_EXPR_STATIC_CHECK(ui_GenExprGetListFromDictionaryCheck);
ExprFuncReturnVal ui_GenExprAppendToListFromDictionary(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchDictionary, const char *pchName, ACMD_EXPR_ERRSTRING errEstr)
{
	ParseTable *pTable = resDictGetParseTable(pchDictionary);
	if (pTable)
	{
		void ***peaList = ui_GenGetManagedList(pGen, pTable);
		void *pStruct = RefSystem_ReferentFromString(pchDictionary, pchName);
		if (pStruct)
		{
			eaPush(peaList, pStruct);
		}
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal ui_GenExprSetListFromObjectPathCheck(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchPath, ACMD_EXPR_ERRSTRING errEstr)
{
	ParseTable *pTable;
	void *pStruct;
	S32 iColumn;
	S32 iIndex;
	if (!objPathResolveFieldWithResult(pchPath, parse_UIGen, NULL, &pTable, &iColumn, &pStruct, &iIndex, OBJPATHFLAG_TRAVERSEUNOWNED | OBJPATHFLAG_SEARCHNONINDEXED, NULL))
	{
		estrPrintf(errEstr, "Object path %s failed to statically resolve", pchPath);
		return ExprFuncReturnError;
	}
	return ExprFuncReturnFinished;
}

// Set this gen's list from the given object path
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetListFromObjectPath) ACMD_EXPR_STATIC_CHECK(ui_GenExprSetListFromObjectPathCheck);
ExprFuncReturnVal ui_GenExprSetListFromObjectPath(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchPath, ACMD_EXPR_ERRSTRING errEstr)
{
	ParseTable *pTable;
	void *pStruct;
	S32 iColumn;
	S32 iIndex;
	if (objPathResolveFieldWithResult(pchPath, parse_UIGen, pGen, &pTable, &iColumn, &pStruct, &iIndex, OBJPATHFLAG_TRAVERSEUNOWNED | OBJPATHFLAG_SEARCHNONINDEXED, NULL))
	{
		if ((pTable[iColumn].type & TOK_EARRAY) && TOK_GET_TYPE(pTable[iColumn].type) & TOK_STRUCT_X)
		{
			void ***peaArray = TokenStoreGetEArray(pTable, iColumn, pStruct, NULL);
			ui_GenSetList(pGen, peaArray, pTable[iColumn].subtable);
		}
		else
		{
			ui_GenSetList(pGen, NULL, NULL);
		}
	}
	else
	{
		ui_GenSetList(pGen, NULL, NULL);
	}

	return ExprFuncReturnFinished;
}

// Cycle focus between the given gens, or if called with just one gen name, set focus to that gen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenCycleFocus);
bool ui_GenExprCycleFocus(SA_PARAM_NN_VALID UIGen *pGenParent, const char *pchGensOrig)
{
	char *pchGens;
	char *pchContext;
	char *pchStart;
	UIGen *pFocused = ui_GenGetFocus();
	bool bFocusNext;
	UIGen *pSetFocusTo = NULL, *pFirstValid = NULL;


	if (pFocused == pGenParent)
		pFocused = NULL;

	bFocusNext = !pFocused;
	strdup_alloca(pchGens, pchGensOrig);

	pchStart = strtok_r(pchGens, " ", &pchContext);

	// First look in children
	do 
	{
		UIGen *pGen = ui_GenFindChild(pGenParent, ui_GenMatchesByNameCB, pchStart);
		if (!pGen)
			pGen = RefSystem_ReferentFromString(UI_GEN_DICTIONARY, pchStart);

		if (UI_GEN_READY(pGen) && !ui_GenInState(pGen, kUIGenStateDisabled))
		{
			if (! pFirstValid)
				pFirstValid = pGen;

			if (bFocusNext)
			{
				pSetFocusTo = pGen;
				break;
			}
			if (pFocused == pGen)
			{
				pSetFocusTo = NULL;
				bFocusNext = true;
				continue;
			}
			pSetFocusTo = pGen;

		}
	} while (pchStart = strtok_r(NULL, " ", &pchContext));

	// The last one in the list was focused, so back to the first.
	if (!pSetFocusTo && bFocusNext)
	{
		pSetFocusTo = pFirstValid;
	}

	if (UI_GEN_READY(pSetFocusTo))
	{
		ui_GenSetFocus(pSetFocusTo);
		return true;
	}

	return false;
}

// Cycle focus between the given gens in reverse, or if called with just one gen name, set focus to that gen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenCycleFocusReverse);
bool ui_GenExprCycleFocusReverse(SA_PARAM_NN_VALID UIGen *pGenParent, const char *pchGensOrig)
{
	char *pchGens;
	char *pchContext;
	char *pchStart;
	UIGen *pFocused = ui_GenGetFocus();
	UIGen *pSetFocusTo = NULL;
	UIGen *pLast = NULL;
	bool bFocusLast = false;
	strdup_alloca(pchGens, pchGensOrig);

	pchStart = strtok_r(pchGens, " ", &pchContext);

	// First look in children
	do 
	{
		UIGen *pGen = ui_GenFindChild(pGenParent, ui_GenMatchesByNameCB, pchStart);
		if (!pGen)
			pGen = RefSystem_ReferentFromString(UI_GEN_DICTIONARY, pchStart);
		if (pGen)
		{
			if (!UI_GEN_READY(pGen) || ui_GenInState(pGen, kUIGenStateDisabled))
			{
				continue;
			}

			if (pFocused == pGen)
			{
				if (pSetFocusTo == NULL)
				{
					bFocusLast = true;
				}
				else
				{
					break;
				}
			}
			pSetFocusTo = pGen;
		}
	} while (pchStart = strtok_r(NULL, " ", &pchContext));

	if (UI_GEN_READY(pSetFocusTo))
	{
		ui_GenSetFocus(pSetFocusTo);
		return true;
	}

	return false;
}

// Get a gen by name. Note due to the optimization this has, using a Gen var twice in a row
// with the only difference being the contents it may return the wrong gen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Gen);
SA_RET_NN_VALID UIGen *ui_GenExprFind(ExprContext *pContext, ACMD_EXPR_DICT("UIGen") const char *pchName)
{
#define UI_GEN_CACHE_SIZE 16
	UIGen *pGen = NULL;
	static struct 
	{
		const char* pchName;
		UIGen *pGen;
		U32 uiFrame;
	} s_apRecent[UI_GEN_CACHE_SIZE];
	
	// Stupid hash that's on par with actual hash functions we use
	U32 uiIndex = 'GEN~';
	uiIndex = (unsigned int)pchName >> 4;
	uiIndex ^= pchName[0] | (pchName[1] << 8);
	uiIndex %= UI_GEN_CACHE_SIZE;
	
	if (pchName == s_apRecent[uiIndex].pchName &&  s_apRecent[uiIndex].uiFrame == g_ui_State.uiFrameCount)
		return s_apRecent[uiIndex].pGen;
	if (!(pGen = RefSystem_ReferentFromString(g_GenState.hGenDict, pchName)))
	{
		ErrorFilenamef(pContext ? exprContextGetBlameFile(pContext) : "NoContext", "%s: Not found in UIGen reference dictionary. This should not have passed static check. Defaulting to root, this will go poorly!", pchName);
		pGen = RefSystem_ReferentFromString(g_GenState.hGenDict, "Root");
		assert(pGen);
	}
	
	s_apRecent[uiIndex].uiFrame = g_ui_State.uiFrameCount;
	s_apRecent[uiIndex].pchName = pchName;
	s_apRecent[uiIndex].pGen = pGen;
	return pGen;

#undef UI_GEN_CACHE_SIZE
}

// Get a gen by name. This is like Gen(), except it is slower and can return NULL if the provided gen does not exist.
// It also does not suffer from the same problem as Gen().
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenLookup);
SA_RET_OP_VALID UIGen *ui_GenExprFindSlow(ExprContext *pContext, ACMD_EXPR_DICT("UIGen") const char *pchName)
{
	UIGen *pGen = NULL;
	static const char *s_pchLastName;
	static UIGen *s_pLastGen;
	static U32 s_uiFrame;

	if (pchName == s_pchLastName && s_uiFrame == g_ui_State.uiFrameCount)
	{
		// Validate that the existing gen name is equal to the cached gen.
		const char *pchLastGenName = s_pLastGen ? s_pLastGen->pchName : NULL;
		if (s_pchLastName == pchLastGenName || (s_pchLastName && pchLastGenName && !stricmp(s_pchLastName, pchLastGenName)))
			return s_pLastGen;
	}

	pGen = RefSystem_ReferentFromString(g_GenState.hGenDict, pchName);
	s_uiFrame = g_ui_State.uiFrameCount;
	s_pchLastName = pchName;
	s_pLastGen = pGen;
	return pGen;
}

// Get the integer value of a slider notch.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSliderGetNotchInt);
S32 ui_GenExprSliderGetNotchInt(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeSlider))
	{
		UIGenSliderState *pState = UI_GEN_STATE(pGen, Slider);
		return pState ? round(pState->fNotch) : 0;
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSliderGetNotchInt: %s is not a slider", pGen->pchName);
	}
	return 0;
}

// Get the float value of a slider notch.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSliderGetNotchFloat);
F32 ui_GenExprSliderGetNotchFloat(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeSlider))
	{
		UIGenSliderState *pState = UI_GEN_STATE(pGen, Slider);
		return pState ? pState->fNotch : 0;
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenSliderGetNotchFloat: %s is not a slider", pGen->pchName);
	}
	return 0;
}

// Returns true if the given bar exists
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenStyleBarExists);
bool ui_GenExprStyleBarExists(ExprContext *pContext, const char* pchBarName)
{
	return !!RefSystem_ReferentFromString("UIStyleBar", pchBarName);
}

// Raise a window (specifically a movable box) to the top of the priority list.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenWindowRaise);
bool ui_GenExprWindowRaise(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen)
{
	if (pGen && UI_GEN_IS_TYPE(pGen, kUIGenTypeMovableBox))
	{
		if (UI_GEN_READY(pGen->pParent))
		{
			ui_GenMovableBoxRaise(pGen);
			return true;
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenWindowRaise: %s is not a movable box", pGen->pchName);
	}
	return false;
}

// Return the type of the active drag or "" if no drag is happening.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenDragType);
const char *ui_GenExprTypeActive(ExprContext *pContext)
{
	return ui_GenDragDropGetType();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenDragStringPayload);
const char *ui_GenExprDragStringPayload(ExprContext *pContext)
{
	return ui_GenDragDropGetStringPayload();
}

// Delete the selected area in a TextEntry or TextArea.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTextAreaSelectionDelete);
void ui_GenTextEntryTextSelectionDelete(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen);

// Delete the selected area in a TextEntry or TextArea.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTextEntrySelectionDelete);
void ui_GenTextEntryTextSelectionDelete(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	TextBuffer *pBuffer = NULL;
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextArea))
		pBuffer = UI_GEN_STATE(pGen, TextArea)->pBuffer;
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry))
		pBuffer = UI_GEN_STATE(pGen, TextEntry)->pBuffer;
	else
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenTextAreaSelectionDelete/GenTextEntrySelectionDelete: %s is not a text entry or text area", pGen->pchName);
	if (pBuffer)
		TextBuffer_SelectionDelete(pBuffer);
}

// Empty a gen's list.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenListClear);
void ui_GenListClear(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetList(pGen, NULL, NULL);
}

// Removes rows before iStart, and only displays iCount rows
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenListTrimToRange);
void ui_GenListRange(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, int iStart, int iCount)
{
	ParseTable *pTable = NULL; 
	void*** pea = ui_GenGetList(pGen, NULL, &pTable);
	int iSize = eaSize(pea);
	eaRemoveRange(pea, iStart+iCount, iSize-iStart+iCount);
	eaRemoveRange(pea, 0, iStart);
}

// Get a jail by name.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenJail);
SA_RET_OP_VALID UIGenJail *ui_GenExprJail(ExprContext *pContext, ACMD_EXPR_DICT("UIGenJailDef") const char *pchJail)
{
	UIGenJailDef *pDef = RefSystem_ReferentFromString("UIGenJailDef", pchJail);
	UIGenJail *pJail = NULL;

	if (pDef && g_ui_pDefaultKeeper) 
	{
		S32 i;
		for (i=0; i < eaSize(&g_ui_pDefaultKeeper->eaJail); i++) 
		{
			UIGenJail *pKeeperJail = g_ui_pDefaultKeeper->eaJail[i];
			UIGenJailDef *pKeeperJailDef = GET_REF(pKeeperJail->hJail);
			if (pKeeperJailDef == pDef) 
			{
				pJail = pKeeperJail;
				break;
			}
		}
	} 
	else if (!pDef) 
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "%s: Not found in UIGenJailDef reference dictionary. This should not have passed static check. Defaulting to NULL, this will go poorly!", pchJail);
	}

	return pJail;
}

// Add a new jail to the screen.
AUTO_EXPR_FUNC(UIGen)  ACMD_NAME(GenJailShow);
bool ui_GenExprJailShow(ACMD_EXPR_DICT(UIGenJailDef) const char *pchJail)
{
	UIGenJailDef *pDef = RefSystem_ReferentFromString("UIGenJailDef", pchJail);
	if (pDef)
	{
		return !!ui_GenJailShow(NULL, pDef);
	}
	else
		return false;
}

// Add a new jail to the screen.
AUTO_EXPR_FUNC(UIGen)  ACMD_NAME(GenJailHide);
bool ui_GenExprJailHide(ACMD_EXPR_DICT(UIGenJailDef) const char *pchJail)
{
	UIGenJailDef *pDef = RefSystem_ReferentFromString("UIGenJailDef", pchJail);
	if (pDef)
	{
		UIGenJail *pJail = ui_GenJailHide(NULL, pDef);
		bool bFound = !!pJail;
		ui_GenJailDestroy(pJail);
		return bFound;
	}
	else
		return false;
}

// Add a new gen to the appropriate jail
AUTO_EXPR_FUNC(UIGen)  ACMD_NAME(GenJailAdd);
bool ui_GenExprJailAdd(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen)
{
	if (pGen)
		return !!ui_GenJailKeeperAdd(NULL, pGen);
	else
		return false;
}

// Remove a gen from jail.
AUTO_EXPR_FUNC(UIGen)  ACMD_NAME(GenJailRemove);
bool ui_GenExprJailRemove(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen)
{
	if (pGen)
		return !!ui_GenJailKeeperRemove(NULL, pGen);
	else
		return false;
}

// Gets a jail's scale name.  If the Jail is not found an error is generated an 1.0 is returned.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenJailGetScale);
F32 ui_GenExprJailGetScale(ExprContext *pContext, ACMD_EXPR_DICT("UIGenJailDef") const char *pchJail)
{
	UIGenJail *pJail = ui_GenExprJail(pContext, pchJail);
	if (pJail)
	{
		return pJail->fScale;
	}

	return 1.0f;
}

// Returns true if jail cells are able to be rearranged
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(JailCanRearrangeCells);
bool ui_GenExprJailCanRearrangeCells(void)
{
	return g_GenState.bJailUnlocked && 
		   g_GenState.bJailFrames &&
		   g_GenState.bJailNoGens;
}

// This is for a very specific case in the HUD,
// and it's pretty terrible that we have to use it at all. 
// If you absolutely must use this expression ask Alex A. first.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetTween);
void ui_GenExprSetTween(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, U32 eType, F32 fTotalTime)
{
	if (SAFE_MEMBER(pGen, pBase))
	{
		if (!pGen->pBase->pTween)
		{
			pGen->pBase->pTween = StructCreate(parse_UIGenTweenInfo);
		}
		pGen->pBase->pTween->eType = eType;
		pGen->pBase->pTween->fTotalTime = fTotalTime;
	}
}

// Get the current gen scale.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenScaleGet);
F32 ui_GenExprScaleGet(ExprContext *pContext)
{
	return g_GenState.fScale;
}

// Set and return the current gen scale.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenScaleSet);
F32 ui_GenExprScaleSet(ExprContext *pContext, F32 fScale)
{
	fScale = CLAMP(fScale, 0.25, 4.0);
	g_GenState.fScale = fScale;
	return fScale;
}

// Check if a gen is stuck to the left edge of the screen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsOnScreenEdgeLeft);
bool ui_GenExprIsOnScreenEdgeLeft(SA_PARAM_NN_VALID UIGen *pGen)
{
	return (pGen->ScreenBox.lx <= 0);
}

// Check if a gen is stuck to the right edge of the screen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsOnScreenEdgeRight);
bool ui_GenExprIsOnScreenEdgeRight(SA_PARAM_NN_VALID UIGen *pGen)
{
	return (pGen->ScreenBox.hx >= g_ui_State.screenWidth - 1);
}

// Check if a gen is stuck to the top edge of the screen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsOnScreenEdgeTop);
bool ui_GenExprIsOnScreenEdgeTop(SA_PARAM_NN_VALID UIGen *pGen)
{
	return (pGen->ScreenBox.ly <= 0);
}

// Check if a gen is stuck to the bottom edge of the screen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsOnScreenEdgeBottom);
bool ui_GenExprIsOnScreenEdgeBottom(SA_PARAM_NN_VALID UIGen *pGen)
{
	return (pGen->ScreenBox.hy >= g_ui_State.screenHeight - 1);
}

// Check if a gen is stuck to the bottom edge of the screen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsOffscreenGenOnBottom);
bool ui_GenExprIsOffscreenGenOnBottom(SA_PARAM_NN_VALID UIGen *pGen)
{
	int iBottomInset = StaticDefineIntGetInt(UISizeEnum, "OffscreenEntityGenBottomInset");

	return (pGen->ScreenBox.hy >= g_ui_State.screenHeight - iBottomInset - 1);
}

// Set tooltip focus
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetTooltipFocus);
bool ui_GenExprSetTooltipFocus(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetTooltipFocus(pGen);
	return g_GenState.pTooltipFocused == pGen;
}

// Set tooltip focus to NULL
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenUnsetTooltipFocus);
void ui_GenExprUnsetTooltipFocus(ExprContext *pContext)
{
	ui_GenSetTooltipFocus(NULL);
}

// Return the distance that the mouse is from the gen's unpadded screen box.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenDistanceFromMouse);
F32 ui_GenDistanceFromMouse(SA_PARAM_OP_VALID UIGen *pGen)
{
	F32 fDist = 1e9;
	if (pGen)
	{
		S32 iMouseX = g_ui_State.mouseX;
		S32 iMouseY = g_ui_State.mouseY;
		F32 fDistX;
		F32 fDistY;
		if (iMouseX < pGen->UnpaddedScreenBox.lx)
			fDistX = pGen->UnpaddedScreenBox.lx - iMouseX;
		else if (iMouseX > pGen->UnpaddedScreenBox.hx)
			fDistX = iMouseX - pGen->UnpaddedScreenBox.hx;
		else
			fDistX = 0;
		if (iMouseY < pGen->UnpaddedScreenBox.ly)
			fDistY = pGen->UnpaddedScreenBox.ly - iMouseY;
		else if (iMouseY > pGen->UnpaddedScreenBox.hy)
			fDistY = iMouseY - pGen->UnpaddedScreenBox.hy;
		else
			fDistY = 0;
		fDist = sqrtf(fDistX * fDistX + fDistY * fDistY);
	}
	return fDist;
}

// Get a persisted UI string variable
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetPersistedString);
const char *ui_GenExprGetPersistedString(const char *pchKey, const char *pchDefault)
{
	return ui_GenGetPersistedString(pchKey, pchDefault);
}

// Get a persisted UI int variable
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetPersistedInt);
S32 ui_GenExprGetPersistedInt(const char *pchKey, S32 iDefault)
{
	return ui_GenGetPersistedInt(pchKey, iDefault);
}

// Get a persisted UI float variable
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetPersistedFloat);
F32 ui_GenExprGetPersistedFloat(const char *pchKey, F32 fDefault)
{
	return ui_GenGetPersistedFloat(pchKey, fDefault);
}

// Set a persisted UI string variable
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetPersistedString);
bool ui_GenExprSetPersistedString(const char *pchKey, const char *pchValue)
{
	return ui_GenSetPersistedString(pchKey, pchValue);
}

// Set a persisted UI int variable
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetPersistedInt);
bool ui_GenExprSetPersistedInt(const char *pchKey, S32 iValue)
{
	return ui_GenSetPersistedInt(pchKey, iValue);
}

// Set a persisted UI float variable
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetPersistedFloat);
bool ui_GenExprSetPersistedFloat(const char *pchKey, F32 fValue)
{
	return ui_GenSetPersistedFloat(pchKey, fValue);
}

const char *pLoremIpsum = "Lorem ipsum dolor sit amet, consectetur adipisicing elit, sed " \
"do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, " \
"quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis " \
"aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla " \
"pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt " \
"mollit anim id est laborum. ";
// Return the specified number of characters of lorem ipsum text
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenLoremIpsum);
char *ui_GenExprLoremIpsum(U32 uNumCharacters)
{
	// !!!: This is not threadsafe at all. <NPK 2010-04-28>
	static char *pBuf = NULL;
	static U32 uLastNum = 0;
	U32 lorenIpsumNumChars = UTF8GetLength(pLoremIpsum);

	// Shortcut
	if(uNumCharacters == uLastNum)
		return pBuf;

	if(uNumCharacters == 0)
		return "";

	estrClear(&pBuf);
	uLastNum = uNumCharacters;
	for(; uNumCharacters >= lorenIpsumNumChars; uNumCharacters -= (U32)lorenIpsumNumChars)
	{
		estrAppend2(&pBuf, pLoremIpsum);
	}
	if(uNumCharacters)
	{
		estrConcat(&pBuf, pLoremIpsum, UTF8GetCodepointOffset(pLoremIpsum, uNumCharacters));
	}
	estrAppend2( &pBuf, "." ); // Always end with a .
	return pBuf;
}

// Creates a list with nothing interesting in it, mostly used for cases where 
// you only care about instance number (e.g. Self.State.Row or GenInstanceNumber)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenNullList);
void ui_GenExprGetListWithEmptyRows(SA_PARAM_NN_VALID UIGen* pGen, S32 iSize);

// Alias for GenNullList
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetListWithEmptyRows);
void ui_GenExprGetListWithEmptyRows(SA_PARAM_NN_VALID UIGen* pGen, S32 iSize)
{
	static UIGen** s_eaData = NULL;
	eaSetSize(&s_eaData, iSize);
	ui_GenSetManagedListSafe(pGen, &s_eaData, UIGen, false);
	eaDestroy(&s_eaData);
}

// Alias for GenNullList
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetChildren);
void ui_GenGetChildren(SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_NN_VALID UIGen* pParent)
{
	static UIGen **eaChildren = NULL;
	int i, iSize = eaSize(SAFE_MEMBER_ADDR2(pParent, pResult, eaChildren));
	eaClearFast(&eaChildren);
	for (i = 0; i < iSize; ++i)
		eaPush(&eaChildren, GET_REF(pParent->pResult->eaChildren[i]->hChild));

	ui_GenSetManagedListSafe(pGen, &eaChildren, UIGen, false);
}

// Alias for GenNullList
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetJailedGens);
void ui_GenGetJailedGens(SA_PARAM_NN_VALID UIGen* pGen)
{
	ui_GenSetManagedListSafe(pGen, &g_ui_pDefaultKeeper->eaGens, UIGen, false);
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprGenTypedDummyListCheck(SA_PARAM_NN_VALID UIGen* pGen, const char* pchParseTable, S32 iSize, ACMD_EXPR_ERRSTRING errEstr)
{
	ParseTable *pTable = ParserGetTableFromStructName(pchParseTable);
	if (!pTable)
	{
		estrPrintf(errEstr, "Invalid object type: %s.", pchParseTable);
		return ExprFuncReturnError;
	}
	return ExprFuncReturnFinished;
}

// Populates a list with dummy structs. Works better with lists that use DeclareType on their templates.
// This function is extremely dangerous if misused.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTypedDummyList) ACMD_EXPR_STATIC_CHECK(exprGenTypedDummyListCheck);
ExprFuncReturnVal ui_GenExprTypedDummyList(SA_PARAM_NN_VALID UIGen* pGen, const char* pchParseTable, S32 iSize, ACMD_EXPR_ERRSTRING errEstr)
{
	ParseTable *pTable = ParserGetTableFromStructName(pchParseTable);
	void*** peaData = ui_GenGetManagedList(pGen, pTable);
	eaSetSizeStructVoid(peaData, pTable, iSize);
	ui_GenSetManagedList(pGen, peaData, pTable, true);
	return ExprFuncReturnFinished;
}

// Returns the window id for the given gen, or -1 if invalid.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetWindowId);
S32 ui_GenExprGetWindowId(SA_PARAM_OP_VALID UIGen* pGen)
{
	return pGen ? pGen->chClone : -1;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(GenCleanupDestinationData);
void ui_GenExprClearPointer(SA_PARAM_NN_VALID UIGen *pGen);

// YOU DO NOT NEED TO USE THIS FUNCTION!
// Clears the GenData. Only exists for certain backward compatibility.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenClearPointer);
void ui_GenExprClearPointer(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenClearPointer(pGen);
}

AUTO_COMMAND ACMD_NAME(GenTestExpr);
void ui_GenExprTest(const char* pchGen, ACMD_SENTENCE pchExpr)
{
	UIGen *pGen = ui_GenFind(pchGen, kUIGenTypeNone);
	Expression *pExpr = exprCreate();

	if(!pGen)
		return;
	
	if(exprGenerateFromString(pExpr, ui_GenGetContext(pGen), pchExpr, NULL))
	{
		MultiVal val;
		ui_GenEvaluate(pGen, pExpr, &val);
	}
}

// DEPRECATED: Do not use this function, use the per-gen type filter expression.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenListFilter);
ExprFuncReturnVal ui_GenExprListFilter(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, ACMD_EXPR_SUBEXPR_IN pSubExpression)
{
	static const char *s_pchFilterVar;
	static int s_hFilterVar;
	static ExprContext *s_pFilterContext;
	MultiVal answer = {0};
	ParseTable *pTable;
	void ***peaList = ui_GenGetList(pGen, NULL, &pTable);

	if (!s_pFilterContext)
	{
		s_pchFilterVar = allocAddString("FilterData");

		s_pFilterContext = exprContextCreate();
		exprContextSetFuncTable(s_pFilterContext, g_GenState.stGenFuncTable);
		exprContextSetParent(s_pFilterContext, g_GenState.pContext);
		exprContextSetStaticCheck(s_pFilterContext, ExprStaticCheck_AllowTypeChanges);

		exprContextSetPointerVarPooledCached(s_pFilterContext, s_pchFilterVar, NULL, parse_UIGen, true, true, &s_hFilterVar);
	}

	if (pTable && peaList && eaSize(peaList) > 0)
	{
		S32 i, n = eaSize(peaList);
		for (i = 0; i < n; )
		{
			bool bKeep;

			// Set the value
			exprContextSetPointerVarPooledCached(s_pFilterContext, s_pchFilterVar, (*peaList)[i], pTable, true, true, &s_hFilterVar);

			// Sample the value
			exprEvaluateSubExpr(pSubExpression, pContext, s_pFilterContext, &answer, false);
			bKeep = MultiValToBool(&answer);

			if (!bKeep)
			{
				eaRemove(peaList, i);
				n--;
			}
			else
			{
				i++;
			}
		}
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenScrollViewScrollTo, GenListScrollTo, GenTabGroupScrollTo);
ExprFuncReturnVal ui_GenExprScrollviewScrollTo(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pScrollable, SA_PARAM_NN_VALID UIGen *pChild, ACMD_EXPR_ERRSTRING pestrError)
{
	UIGen *pGen;

	for (pGen = pChild; pGen && pGen != pScrollable; )
	{
		pGen = pGen->pParent;
	}

	estrClear(pestrError);

	if (UI_GEN_IS_TYPE(pScrollable, kUIGenTypeScrollView))
	{
		if (pGen)
		{
			UIGenScrollViewState *pScrollViewState = UI_GEN_STATE(pScrollable, ScrollView);
			if (!CBoxContains(&pScrollable->ScreenBox, &pChild->UnpaddedScreenBox) && !nearf(pScrollable->ScreenBox.ly, pChild->UnpaddedScreenBox.ly))
			{
				F32 fDifference = CBoxHeight(&pChild->UnpaddedScreenBox) > CBoxHeight(&pScrollable->ScreenBox)
						|| pChild->UnpaddedScreenBox.ly <= pScrollable->UnpaddedScreenBox.ly
					? pChild->UnpaddedScreenBox.ly - pScrollable->ScreenBox.ly
					: pChild->UnpaddedScreenBox.hy - pScrollable->ScreenBox.hy;
				pScrollViewState->Scrollbar.fScrollPosition += fDifference * pScrollable->fScale;
				CLAMP(pScrollViewState->Scrollbar.fScrollPosition, 0, pScrollViewState->Scrollbar.fTotalHeight);
			}
			return ExprFuncReturnFinished;
		}
	}
	else if (UI_GEN_IS_TYPE(pScrollable, kUIGenTypeList))
	{
		estrConcatf(pestrError, "GenListScrollTo() is not implemented.");
	}
	else if (UI_GEN_IS_TYPE(pScrollable, kUIGenTypeTabGroup))
	{
		estrConcatf(pestrError, "GenTabGroupScrollTo() is not implemented.");
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenScrollViewScrollTo/GenListScrollTo/GenTabGroupScrollTo: %s is not a scrollview, list or tab group", pGen->pchName);
	}

	if (!pGen)
	{
		if (estrLength(pestrError))
			estrConcatChar(pestrError, ' ');
		estrConcatf(pestrError, "Gen %s is not a child of %s.", pChild->pchName, pScrollable->pchName);
	}

	return ExprFuncReturnError;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenScrollViewScrollToTargetTop);
ExprFuncReturnVal ui_GenExprScrollviewScrollToTop(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pScrollable, SA_PARAM_NN_VALID UIGen *pChild, ACMD_EXPR_ERRSTRING pestrError)
{
	UIGen *pGen;
	for (pGen = pChild; pGen && pGen != pScrollable; )
	{
		pGen = pGen->pParent;
	}
	estrClear(pestrError);
	if (UI_GEN_IS_TYPE(pScrollable, kUIGenTypeScrollView))
	{
		if (pGen)
		{
			UIGenScrollViewState *pScrollViewState = UI_GEN_STATE(pScrollable, ScrollView);
			F32 fHeight = CBoxHeight(&pGen->ScreenBox);
			F32 fOffSpace = max(pScrollViewState->Scrollbar.fTotalHeight - fHeight, 0);
			F32 fDifference = pChild->UnpaddedScreenBox.ly - pScrollable->ScreenBox.ly;
			pScrollViewState->Scrollbar.fScrollPosition += fDifference * pScrollable->fScale;
			pScrollViewState->Scrollbar.fScrollPosition = floorf(CLAMP(pScrollViewState->Scrollbar.fScrollPosition, 0, fOffSpace));
			return ExprFuncReturnFinished;
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenScrollViewScrollToTargetTop: %s is not a scrollview", pGen->pchName);
	}

	if (!pGen)
	{
		if (estrLength(pestrError))
			estrConcatChar(pestrError, ' ');
		estrConcatf(pestrError, "Gen %s is not a child of %s.", pChild->pchName, pScrollable->pchName);
	}

	return ExprFuncReturnError;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenScrollViewScrollToTargetBottom);
ExprFuncReturnVal ui_GenExprScrollviewScrollToBottom(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pScrollable, SA_PARAM_NN_VALID UIGen *pChild, ACMD_EXPR_ERRSTRING pestrError)
{
	UIGen *pGen;
	for (pGen = pChild; pGen && pGen != pScrollable; )
	{
		pGen = pGen->pParent;
	}
	estrClear(pestrError);
	if (UI_GEN_IS_TYPE(pScrollable, kUIGenTypeScrollView))
	{
		if (pGen)
		{
			UIGenScrollViewState *pScrollViewState = UI_GEN_STATE(pScrollable, ScrollView);
			F32 fHeight = CBoxHeight(&pGen->ScreenBox);
			F32 fOffSpace = max(pScrollViewState->Scrollbar.fTotalHeight - fHeight, 0);
			F32 fDifference = pChild->UnpaddedScreenBox.hy - pScrollable->ScreenBox.hy;
			pScrollViewState->Scrollbar.fScrollPosition += fDifference * pScrollable->fScale;
			pScrollViewState->Scrollbar.fScrollPosition = floorf(CLAMP(pScrollViewState->Scrollbar.fScrollPosition, 0, fOffSpace));
			return ExprFuncReturnFinished;
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenScrollViewScrollToTargetTop: %s is not a scrollview", pGen->pchName);
	}

	if (!pGen)
	{
		if (estrLength(pestrError))
			estrConcatChar(pestrError, ' ');
		estrConcatf(pestrError, "Gen %s is not a child of %s.", pChild->pchName, pScrollable->pchName);
	}

	return ExprFuncReturnError;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("UITextureAssemblyExists");
bool ui_GenTextureAssemblyExists(const char *pchName)
{
	return !!RefSystem_ReferentFromString("UITextureAssembly", pchName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenStyleFontExists");
bool ui_GenStyleFontExists(const char *pchName)
{
	return !!RefSystem_ReferentFromString("UIStyleFont", pchName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenScrollbarExists");
bool ui_GenScrollbarExists(const char *pchName)
{
	return !!RefSystem_ReferentFromString("UIGenScrollbarDef", pchName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenStringVar");
const char *ui_GenExprStringVar(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchVar)
{
	UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar);
	if (!pGlob)
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Gen %s does not define Var %s", pGen->pchName, pchVar);
		return NULL;
	}
	return pGlob->pchString;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenFloatVar");
F32 ui_GenExprFloatVar(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchVar)
{
	UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar);
	if (!pGlob)
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Gen %s does not define Var %s", pGen->pchName, pchVar);
		return 0.0f;
	}
	return pGlob->fFloat;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenIntVar");
S32 ui_GenExprIntVar(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchVar)
{
	UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar);
	if (!pGlob)
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Gen %s does not define Var %s", pGen->pchName, pchVar);
		return 0;
	}
	return pGlob->iInt;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenNameStringVar");
const char *ui_GenExprFindStringVar(ExprContext *pContext, ACMD_EXPR_DICT("UIGen") const char *pchGen, const char *pchVar)
{
	UIGen *pGen = ui_GenExprFind(pContext, pchGen);
	UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar);
	if (!pGlob)
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Gen %s does not define Var %s", pGen->pchName, pchVar);
		return NULL;
	}
	return pGlob->pchString;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenNameFloatVar");
F32 ui_GenExprFindFloatVar(ExprContext *pContext, ACMD_EXPR_DICT("UIGen") const char *pchGen, const char *pchVar)
{
	UIGen *pGen = ui_GenExprFind(pContext, pchGen);
	UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar);
	if (!pGlob)
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Gen %s does not define Var %s", pGen->pchName, pchVar);
		return 0.0f;
	}
	return pGlob->fFloat;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenNameIntVar");
S32 ui_GenExprFindIntVar(ExprContext *pContext, ACMD_EXPR_DICT("UIGen") const char *pchGen, const char *pchVar)
{
	UIGen *pGen = ui_GenExprFind(pContext, pchGen);
	UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar);
	if (!pGlob)
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Gen %s does not define Var %s", pGen->pchName, pchVar);
		return 0;
	}
	return pGlob->iInt;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenParentStringVar");
const char *ui_GenExprParentStringVar(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchVar)
{
	return !pGen->pParent ? NULL : ui_GenExprStringVar(pContext, pGen->pParent, pchVar);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenParentFloatVar");
F32 ui_GenExprParentFloatVar(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchVar)
{
	return !pGen->pParent ? 0.0f : ui_GenExprFloatVar(pContext, pGen->pParent, pchVar);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenParentIntVar");
S32 ui_GenExprParentIntVar(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchVar)
{
	return !pGen->pParent ? 0 : ui_GenExprIntVar(pContext, pGen->pParent, pchVar);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetParent");
SA_RET_NN_VALID UIGen *ui_GenExprGetParent(SA_PARAM_NN_VALID UIGen *pGen)
{
	if (pGen->pParent)
		return pGen->pParent;
	return pGen;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetAncestor");
SA_RET_NN_VALID UIGen *ui_GenExprGetAncestor(SA_PARAM_NN_VALID UIGen *pGen, int iGeneration)
{
	UIGen *pTarget = pGen;
	while (pTarget && iGeneration--)
		pTarget = pTarget->pParent;
	return pTarget ? pTarget : pGen;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSetStaticString");
bool ui_GenExprSetStaticString(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchStaticString)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeText))
	{
		UIGenTextState *pText = UI_GEN_STATE(pGen, Text);
		if (!pText->pchStaticString || stricmp(pText->pchStaticString, pchStaticString))
		{
			estrCopy2(&pText->pchStaticString, pchStaticString);
		}
		return true;
	}
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeSMF))
	{
		UIGenSMFState *pSMF = UI_GEN_STATE(pGen, SMF);
		if (!pSMF->pchStaticString || stricmp(pSMF->pchStaticString, pchStaticString))
		{
			estrCopy2(&pSMF->pchStaticString, pchStaticString);
		}
		return true;
	}

	ErrorFilenamef(exprContextGetBlameFile(pContext), "%s: Expected UIGenText or UIGenSMF in GenSetStaticString", pGen->pchName);
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenListColumnSortable");
bool ui_GenExprListColumnSortable(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeListColumn))
	{
		UIGenListColumn *pColumn = UI_GEN_RESULT(pGen, ListColumn);
		return SAFE_MEMBER(pColumn, bSortable);
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenListColumns: %s is not a list", pGen->pchName);
		return false;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenIsChild");
bool ui_GenExprIsChild(SA_PARAM_NN_VALID UIGen *pGen, SA_PRE_NN_VALID SA_POST_OP_VALID UIGen *pChild)
{
	while (pChild && pChild != pGen)
		pChild = pChild->pParent;
	return !!pChild;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenTextIsTruncated");
bool ui_GenExprIsTruncated(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenIsTruncated");
bool ui_GenExprIsTruncated(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	UIGenBundleTruncateState *pTruncate = NULL;
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeListRow))
	{
		UIGenListRowState *pState = UI_GEN_STATE(pGen, ListRow);
		pTruncate = pState? &pState->Truncate : NULL;
	}
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeListColumn))
	{
		UIGenListColumnState *pState = UI_GEN_STATE(pGen, ListColumn);
		pTruncate = pState? &pState->Truncate : NULL;
	}
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeText))
	{
		UIGenTextState *pState = UI_GEN_STATE(pGen, Text);
		pTruncate = pState? &pState->Truncate : NULL;
	}
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextArea))
	{
		UIGenTextAreaState *pState = UI_GEN_STATE(pGen, TextArea);
		pTruncate = pState? &pState->Truncate : NULL;
	}
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry))
	{
		UIGenTextEntryState *pState = UI_GEN_STATE(pGen, TextEntry);
		pTruncate = pState? &pState->Truncate : NULL;
	}
	return !!SAFE_MEMBER(pTruncate, pPreviousTruncateMessage);
}

//This is a temporary expression and it may not be robust enough for real use
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenAppendTextToFile");
void ui_GenExprAppendTextToFile(const char *pchFileName, const char *pchTextToDump)
{
	FILE *pFile = NULL;
	char *pchPath = estrCreateFromStr(fileLocalDataDir());

	estrAppend2(&pchPath, "\\");
	estrAppend2(&pchPath, pchFileName);

	pFile = fopen(pchPath, "a");

	if (pFile)
	{
		fprintf(pFile, "%s\n", pchTextToDump);
		fclose(pFile);
	}

	estrDestroy(&pchPath);
}

// NOTE: There's a 1 frame delay on the position returned by this. If you can fix it, great! Nobody is relying on that behavior.
// So, it gives the wrong value while scrolling.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetBottomInScrollView"); 
S32 ui_GenExprGetBottomInScrollView(SA_PARAM_NN_VALID UIGen *pGenA, SA_PARAM_NN_VALID UIGen *pScrollView)
{
	UIGenScrollView *pView = UI_GEN_RESULT(pScrollView, ScrollView);
	UIGenScrollViewState *pState = UI_GEN_STATE(pScrollView, ScrollView);
	return pGenA->ScreenBox.hy - pScrollView->ScreenBox.ly + (pState->Scrollbar.fScrollPosition - pView->iVirtualTopPadding * pScrollView->fScale);
}

// NOTE: There's a 1 frame delay on the position returned by this. If you can fix it, great! Nobody is relying on that behavior.
// So, it gives the wrong value while scrolling.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetTopInScrollView"); 
S32 ui_GenExprGetTopInScrollView(SA_PARAM_NN_VALID UIGen *pGenA, SA_PARAM_NN_VALID UIGen *pScrollView)
{
	UIGenScrollView *pView = UI_GEN_RESULT(pScrollView, ScrollView);
	UIGenScrollViewState *pState = UI_GEN_STATE(pScrollView, ScrollView);
	return pGenA->ScreenBox.ly - pScrollView->ScreenBox.ly + (pState->Scrollbar.fScrollPosition - pView->iVirtualTopPadding * pScrollView->fScale);
}

// Start a UI tutorial
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenTutorialStart");
bool ui_GenExprTutorialStart(const char *pchTutorial)
{
	UIGenTutorial *pTutorial = RefSystem_ReferentFromString("UIGenTutorial", pchTutorial);
	if (pTutorial)
	{
		ui_GenTutorialStart(NULL, pTutorial);
		return true;
	}
	return false;
}

// Reset the current UI tutorial
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenTutorialReset");
bool ui_GenExprTutorialReset(void)
{
	return ui_GenTutorialReset(NULL);
}

// Stop the current UI tutorial
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenTutorialStop");
bool ui_GenExprTutorialStop(void)
{
	return ui_GenTutorialStop(NULL);
}

// Advance to the next step in the current UI tutorial
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenTutorialPrevious");
bool ui_GenExprTutorialPrevious(void)
{
	return ui_GenTutorialPrevious(NULL);
}

// Advance to the next step in the current UI tutorial
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenTutorialNext");
bool ui_GenExprTutorialNext(void)
{
	return ui_GenTutorialNext(NULL);
}

// Advance to the next step in the current UI tutorial using the step event
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenTutorialStep");
bool ui_GenExprTutorialStep(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenTutorialEvent(NULL, pGen, kUIGenTutorialEventTypeManualNext);
	return true;
}

// Get the current UI tutorial
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenTutorialGetCurrentName");
const char *ui_GenExprTutorialCurrentName(void)
{
	UIGenTutorial *pTutorial = SAFE_GET_REF(g_pUIGenActiveTutorial, hTutorial);
	return SAFE_MEMBER(pTutorial, pchName);
}

// Get the current step in the UI tutorial
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenTutorialGetCurrentStep");
S32 ui_GenExprTutorialGetCurrentStep(void)
{
	return SAFE_MEMBER(g_pUIGenActiveTutorial, iStep);
}

// Get the maximum number of steps in the current UI tutorial
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenTutorialGetStepCount");
S32 ui_GenExprTutorialGetSteps(void)
{
	UIGenTutorial *pTutorial = SAFE_GET_REF(g_pUIGenActiveTutorial, hTutorial);
	return pTutorial ? eaSize(&pTutorial->eaSteps) : 0;
}

// Set the current step in the UI tutorial
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenTutorialSetCurrentStep");
bool ui_GenExprTutorialSetCurrentStep(S32 iStep)
{
	return ui_GenTutorialSetStep(NULL, iStep);
}

// Get the current UI tutorial step display name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenTutorialGetCurrentStepDisplayName");
const char *ui_GenExprTutorialGetCurrentStepDisplayName(void)
{
	UIGenTutorial *pTutorial = SAFE_GET_REF(g_pUIGenActiveTutorial, hTutorial);
	UIGenTutorialStep *pStep = pTutorial ? eaGet(&pTutorial->eaSteps, g_pUIGenActiveTutorial->iStep) : NULL;
	return pStep ? TranslateMessageRef(pStep->hDisplayName) : NULL;
}

// Get the current UI tutorial step description
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenTutorialGetCurrentStepDescription");
const char *ui_GenExprTutorialGetCurrentStepDescription(void)
{
	UIGenTutorial *pTutorial = SAFE_GET_REF(g_pUIGenActiveTutorial, hTutorial);
	UIGenTutorialStep *pStep = pTutorial ? eaGet(&pTutorial->eaSteps, g_pUIGenActiveTutorial->iStep) : NULL;
	return pStep ? TranslateMessageRef(pStep->hDescription) : NULL;
}

// Get the current UI tutorial step description
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenTutorialGetGenTutorialInfo");
const char *ui_GenExprTutorialGetGenTutorialInfo(SA_PARAM_NN_VALID UIGen *pGen)
{
	return pGen->pTutorialInfo ? TranslateMessageRef(pGen->pTutorialInfo->hInfo) : NULL;
}

// Get the steps of the current UI tutorial as a list model
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenTutorialGenGetSteps");
void ui_GenExprTutorialGenGetSteps(SA_PARAM_NN_VALID UIGen *pGen)
{
	UIGenTutorial *pTutorial = SAFE_GET_REF(g_pUIGenActiveTutorial, hTutorial);
	if (pTutorial)
		ui_GenSetListSafe(pGen, &pTutorial->eaSteps, UIGenTutorialStep);
	else
		ui_GenSetListSafe(pGen, NULL, UIGenTutorialStep);
}

// Add a texture override
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenTextureSkinningOverrideAdd");
bool ui_GenExprTextureSkinningOverrideAdd(const char *pchTextureSuffix)
{
	return ui_GenAddTextureSkinOverride(pchTextureSuffix);
}

// Remove a texture override
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenTextureSkinningOverrideRemove");
bool ui_GenExprTextureSkinningOverrideRemove(const char *pchTextureSuffix)
{
	return ui_GenRemoveTextureSkinOverride(pchTextureSuffix);
}
