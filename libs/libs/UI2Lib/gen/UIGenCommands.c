#include "file.h"
#include "FolderCache.h"
#include "earray.h"
#include "error.h"
#include "cmdparse.h"
#include "TextBuffer.h"
#include "StringUtil.h"
#include "UIGen_h_ast.h"
#include "UIGenWidget.h"
#include "UIGenPrivate.h"
#include "UIGenTextEntry.h"
#include "UIGenList.h"
#include "UIGenButton.h"
#include "UIGenSlider.h"
#include "UIGenJail.h"
#include "UIGenTutorial.h"
#include "ExpressionFunc.h"
#include "StringCache.h"
#include "BlockEarray.h"
#include "ExpressionPrivate.h"
#include "GlobalTypes.h"

#include "UIGenJail_h_ast.h"
#include "UIGenCommands_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define GenErrorf(pchFormat, ...) do { if (isDevelopmentMode()) { Errorf(pchFormat, __VA_ARGS__); } } while (false)

//////////////////////////////////////////////////////////////////////////
// Commands that affect the state of Gens. Since these usually refer to
// widgets and states by name, they're not meant to be called from the
// client client, and so are private. If you need to do the things they
// do on the client, just call the actual functions with a real Gen and state.

// Display a highlight around gens which the mouse is hovering over.
AUTO_CMD_INT(g_GenState.bHighlight, GenHighlight) ACMD_CATEGORY(Debug);

// Display a highlight around the focused gen which the mouse is hovering over.
AUTO_CMD_INT(g_GenState.bFocusHighlight, GenFocusHighlight) ACMD_CATEGORY(Debug);

// Run the UIGen system in debug mode.
AUTO_CMD_INT(g_GenState.iDebugLevel, GenDebug) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// Enable per-gen timing information.
AUTO_CMD_INT(g_GenState.bTimingNames, GenTimingNames) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// Enable gen per-function timing information.
AUTO_CMD_INT(g_GenState.bTimingFunctions, GenTimingFunctions) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// Enable per-gen action timing information.
AUTO_CMD_INT(g_GenState.bTimingActions, GenTimingActions) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// Enable per-gen misc expression timing information.
AUTO_CMD_INT(g_GenState.bTimingExpressions, GenTimingExpressions) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// Enable detailed timing information.
AUTO_CMD_INT(g_GenState.iTimingLevel, GenTimingLevel) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// Change the base UI scale.
AUTO_CMD_FLOAT(g_GenState.fScale, GenScale) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;

// Turn a UI state on or off for a particular widget.
AUTO_COMMAND ACMD_CLIENTCMD ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("GenSetState");
void ui_GenCmdSetState(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen, UIGenState eState, bool bEnable)
{
	UIGen *pGen = ui_GenFind(pchGen, kUIGenTypeNone);
	if (pGen)
	{
		ui_GenState(pGen, eState, bEnable);
	}
}

// Turn a UI state on or off by name, for a particular widget by name.
AUTO_COMMAND ACMD_CLIENTCMD ACMD_HIDE ACMD_ACCESSLEVEL(0)  ACMD_NAME("GenSetStateName");
void ui_GenCmdSetStateName(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen, const char *pchState, bool bEnable)
{
	UIGen *pGen = ui_GenFind(pchGen, kUIGenTypeNone);
	UIGenState eState = StaticDefineIntGetInt(UIGenStateEnum, pchState);
	if (pGen && eState != kUIGenStateNone)
	{
		ui_GenState(pGen, eState, bEnable);
	}
}

// Turn a global UI state on or off.
AUTO_COMMAND ACMD_CLIENTCMD ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("GenSetGlobalState");
void ui_GenCmdSetGlobalState(UIGenState eState, bool bEnable)
{
	ui_GenSetGlobalState(eState, bEnable);
}

// Turn a global UI state on or off via name.
AUTO_COMMAND ACMD_CLIENTCMD ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("GenSetGlobalStateName");
void ui_GenCmdSetGlobalStateName(const char *pchState, bool bEnable)
{
	UIGenState eState = StaticDefineIntGetInt(UIGenStateEnum, pchState);
	if (eState != kUIGenStateNone)
		ui_GenSetGlobalState(eState, bEnable);
}

// If the given gen is a list, move the selected row up by one.
AUTO_COMMAND ACMD_NAME("GenListUp") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool ui_GenCmdListUp(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen)
{
	UIGen *pGen = ui_GenFind(pchGen, kUIGenTypeList);
	if (UI_GEN_READY(pGen))
	{
		UIGenList *pList = UI_GEN_RESULT(pGen, List);
		UIGenListState *pListState = UI_GEN_STATE(pGen, List);
		S32 iRow = ui_GenListPreviousSelectableRow(pGen, pList, pListState, pListState->iSelectedRow);
		UIGen *pRowGen = NULL;
		if (iRow >= 0)
			pListState->bSelectedRowNotSelected = false;
		if (iRow >= 0 && iRow != pListState->iSelectedRow)
		{
			pListState->iSelectedRow = iRow;
			if ( pList->bShowSelectedRow )
				pListState->bForceShowSelectedRow = true;
			pRowGen = eaGet(&pListState->eaRows, pListState->iSelectedRow);
			if (UI_GEN_READY(pRowGen))
			{
				UIGenListRow *pRow = UI_GEN_RESULT(pRowGen, ListRow);
				ui_GenRunAction(pRowGen, pRow->pOnSelected);
			}
		}
		return true;
	}
	return false;
}

// If the given gen is a list, move the selected row down by one.
AUTO_COMMAND ACMD_NAME("GenListDown") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool ui_GenCmdListDown(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen)
{
	UIGen *pGen = ui_GenFind(pchGen, kUIGenTypeList);
	if (UI_GEN_READY(pGen))
	{
		UIGenList *pList = UI_GEN_RESULT(pGen, List);
		UIGenListState *pListState = UI_GEN_STATE(pGen, List);
		S32 iRow = ui_GenListNextSelectableRow(pGen, pList, pListState, pListState->iSelectedRow);
		UIGen *pRowGen = NULL;
		if (iRow >= 0)
			pListState->bSelectedRowNotSelected = false;
		if (iRow >= 0 && ( pListState->bUnselected || iRow != pListState->iSelectedRow ) )
		{
			pListState->iSelectedRow = iRow;
			if ( pList->bShowSelectedRow )
				pListState->bForceShowSelectedRow = true;
			pListState->bUnselected = false;

			pRowGen = eaGet(&pListState->eaRows, pListState->iSelectedRow);
			if (UI_GEN_READY(pRowGen))
			{
				UIGenListRow *pRow = UI_GEN_RESULT(pRowGen, ListRow);
				ui_GenRunAction(pRowGen, pRow->pOnSelected);
			}
		}
		return true;
	}
	return false;
}

// If the given gen is a list, run the selected callback.
AUTO_COMMAND ACMD_NAME("GenListDoSelectedCallback") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool ui_GenCmdListDoSelectedCallback(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen)
{
	UIGen *pGen = ui_GenFind(pchGen, kUIGenTypeList);
	if (UI_GEN_READY(pGen))
	{
		UIGenListState *pListState = (UIGenListState *)pGen->pState;
		UIGen *pRowGen = NULL;
		pRowGen = eaGet(&pListState->eaRows, pListState->iSelectedRow);
		if (UI_GEN_READY(pRowGen) && !pListState->bSelectedRowNotSelected)
		{
			UIGenListRow *pRow = UI_GEN_RESULT(pRowGen, ListRow);
			ui_GenRunAction(pRowGen, pRow->pOnSelected);
		}
		return true;
	}
	return false;
}

// If the given gen is a list, activate the selected row.
AUTO_COMMAND ACMD_NAME("GenListActivate") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool ui_GenCmdListActivate(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen)
{
	UIGen *pGen = ui_GenFind(pchGen, kUIGenTypeList);
	if (UI_GEN_READY(pGen))
	{
		ui_GenListActivateSelectedRow(pGen);
		return true;
	}
	return false;
}

// Cycle focus between the given gens, or if called with just one gen name, set focus to that gen.
AUTO_COMMAND ACMD_NAME("GenCycleFocus") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool ui_GenCmdCycleFocus(ACMD_SENTENCE pchGens)
{
	char *pchContext;
	char *pchStart = strtok_r(pchGens, " ", &pchContext);
	UIGen *pFocused = ui_GenGetFocus();
	bool bFocusNext = !pFocused;
	do 
	{
		UIGen *pGen = RefSystem_ReferentFromString(UI_GEN_DICTIONARY, pchStart);
		if (pGen)
		{
			if (!UI_GEN_READY(pGen) || ui_GenInState(pGen, kUIGenStateDisabled))
				continue;
			else if (bFocusNext)
			{
				ui_GenSetFocus(pGen);
				return true;
			}
			if (pFocused == pGen)
				bFocusNext = true;
		}
		else
			GenErrorf("Gen %s not found in focus cycling", pchStart);
	} while (pchStart = strtok_r(NULL, " ", &pchContext));

	// The last one in the list was focused, so back to the first.
	if (bFocusNext)
	{
		UIGen *pFirst = RefSystem_ReferentFromString(UI_GEN_DICTIONARY, pchGens);
		if (UI_GEN_READY(pFirst) && !ui_GenInState(pFirst, kUIGenStateDisabled))
		{
			ui_GenSetFocus(pFirst);
			return true;
		}
	}

	return false;
}

// Cycle focus between the given gens in reverse, or if called with just one gen name, set focus to that gen.
AUTO_COMMAND ACMD_NAME("GenCycleFocusReverse") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool ui_GenCmdCycleFocusReverse(ACMD_SENTENCE pchGens)
{
	char *pchContext;
	char *pchStart = strtok_r(pchGens, " ,\t\r\n", &pchContext);
	UIGen *pFocused = ui_GenGetFocus();
	UIGen *pLast = NULL;
	bool bFocusLast = false;
	do 
	{
		UIGen *pGen = RefSystem_ReferentFromString(UI_GEN_DICTIONARY, pchStart);
		if (pGen)
		{
			if (!UI_GEN_READY(pGen) || ui_GenInState(pGen, kUIGenStateDisabled))
				continue;
			if (pFocused == pGen)
			{
				if (pLast == NULL)
					bFocusLast = true;
				else
				{
					ui_GenSetFocus(pLast);
					break;
				}
			}
			pLast = pGen;
		}
		else
			GenErrorf("Gen %s not found in focus cycling", pchStart);
	} while (pchStart = strtok_r(NULL, " ", &pchContext));

	// The first one in the list was focused, so focus the last one.
	if (bFocusLast && UI_GEN_READY(pLast) && !ui_GenInState(pLast, kUIGenStateDisabled))
	{
		ui_GenSetFocus(pLast);
		return true;
	}

	return false;
}

// Set focus to the given gen.
AUTO_COMMAND ACMD_NAME("GenSetFocus") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool ui_GenCmdSetFocus(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen)
{
	UIGen *pGen = RefSystem_ReferentFromString(UI_GEN_DICTIONARY, pchGen);
	if (pGen)
	{
		if (!UI_GEN_READY(pGen))
			return false;
		else
		{
			ui_GenSetFocus(pGen);
			return true;
		}
	}
	else
	{
		GenErrorf("Gen %s not found when setting focus", pchGen);
		return false;
	}
}

// Set focus to the given gen as soon as it becomes ready.
AUTO_COMMAND ACMD_NAME("GenSetFocusOnCreate") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(interface);
bool ui_GenCmdSetFocusOnCreate(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen)
{
	UIGen *pGen = RefSystem_ReferentFromString(UI_GEN_DICTIONARY, pchGen);
	if (pGen)
	{
		pGen->bNextFocusOnCreate = true;
		return true;
	}
	else
	{
		GenErrorf("Gen %s not found when setting focus on create", pchGen);
		return false;
	}
}

// Set tooltip focus to the given gen.
AUTO_COMMAND ACMD_NAME("GenSetTooltipFocus") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool ui_GenCmdSetTooltipFocus(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen)
{
	UIGen *pGen = RefSystem_ReferentFromString(UI_GEN_DICTIONARY, pchGen);
	if (pGen)
	{
		if (!UI_GEN_READY(pGen))
			return false;
		else
		{
			ui_GenSetTooltipFocus(pGen);
			return true;
		}
	}
	else
	{
		GenErrorf("Gen %s not found when setting tooltip focus", pchGen);
		return false;
	}
}

// If the given gen is a button, click it.
AUTO_COMMAND ACMD_NAME("GenButtonClick") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool ui_GenCmdButtonClickCmd(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen)
{
	UIGen *pGen = ui_GenFind(pchGen, kUIGenTypeButton);
	if (UI_GEN_READY(pGen))
	{
		ui_GenButtonClick(pGen);
		return true;
	}
	return false;
}

// Set the text of a gen text entry.
AUTO_COMMAND ACMD_NAME("GenSetText") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool ui_GenCmdSetText(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen, ACMD_SENTENCE pchText)
{
	UIGen *pGen = ui_GenFind(pchGen, kUIGenTypeTextEntry);
	if (pGen)
	{
		UIGenTextEntryState *pTextEntryState = UI_GEN_STATE(pGen, TextEntry);
		bool bRet = TextBuffer_SetText(pTextEntryState->pBuffer, pchText);
		TextBuffer_CursorToEnd(pTextEntryState->pBuffer);
		if (UI_GEN_READY(pGen))
		{
			UIGenTextEntry *pTextEntry = UI_GEN_RESULT(pGen, TextEntry);
			ui_GenRunAction(pGen, pTextEntry->pOnChanged);
		}
		return bRet;
	}
	return false;
}

// Set the text of a gen text entry.
AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR("GenSetText");
bool ui_GenCmdSetTextEmpty(CmdContext *pContext)
{
	return globCmdParsef("%s \"\"", pContext->commandString);
}

// Move a slider's value, if interactive, by the given amount.
AUTO_COMMAND ACMD_NAME("GenSliderAdjustValue") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool ui_GenCmdSliderAdjustValue(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen, F32 fDiff)
{
	UIGen *pGen = ui_GenFind(pchGen, kUIGenTypeSlider);
	if (UI_GEN_READY(pGen))
	{
		UIGenSlider *pSlider = UI_GEN_RESULT(pGen, Slider);
		if (pSlider->bValueInteractive)
		{
			UIGenSliderState *pState = UI_GEN_STATE(pGen, Slider);
			pState->fValue += fDiff;
			pState->fValue = CLAMP(pState->fValue, 0, pState->fMax);
			ui_GenRunAction(pGen, pSlider->pOnChanged);
		}
		return true;
	}
	return false;
}

// Move a slider's notch, if interactive, by the given amount.
AUTO_COMMAND ACMD_NAME("GenSliderAdjustNotch") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool ui_GenCmdSliderAdjustNotch(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen, F32 fDiff)
{
	UIGen *pGen = ui_GenFind(pchGen, kUIGenTypeSlider);
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
	return false;
}

// Set a slider's notch, if interactive.
AUTO_COMMAND ACMD_NAME("GenSliderSetNotch") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool ui_GenCmdSliderSetNotch(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen, F32 fValue)
{
	UIGen *pGen = ui_GenFind(pchGen, kUIGenTypeSlider);
	if (UI_GEN_READY(pGen))
	{
		UIGenSlider *pSlider = UI_GEN_RESULT(pGen, Slider);
		if (pSlider->bNotchInteractive)
		{
			UIGenSliderState *pState = UI_GEN_STATE(pGen, Slider);
			pState->fNotch = CLAMP(fValue, 0, pState->fMax);
			ui_GenRunAction(pGen, pSlider->pOnChanged);
		}
		return true;
	}
	return false;
}

// Set a slider's notch, if interactive.
AUTO_COMMAND ACMD_NAME("GenSliderSetValue") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool ui_GenCmdSliderSetValue(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen, F32 fValue)
{
	UIGen *pGen = ui_GenFind(pchGen, kUIGenTypeSlider);
	if (UI_GEN_READY(pGen))
	{
		UIGenSlider *pSlider = UI_GEN_RESULT(pGen, Slider);
		if (pSlider->bValueInteractive)
		{
			UIGenSliderState *pState = UI_GEN_STATE(pGen, Slider);
			pState->fValue = CLAMP(fValue, 0, pState->fMax);
			ui_GenRunAction(pGen, pSlider->pOnChanged);
		}
		return true;
	}
	return false;
}

// Show a gen on the window layer.
AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME("GenAddWindow") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool ui_GenCmdAddWindow(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen)
{
	UIGen *pWindow = ui_GenFind(pchGen, kUIGenTypeNone);
	bool bResult = ui_GenAddWindow(pWindow);
	if (!pWindow)
		GenErrorf("%s: Invalid Gen name %s", __FUNCTION__, pchGen);
	return bResult;
}

// Show a gen on the window layer that is specific to PC or Xbox.
AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME("GenAddWindowPCXbox") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool ui_GenCmdAddWindowPCXbox(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGenPC, ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGenXbox)
{
#if _XBOX
	UIGen *pWindow = ui_GenFind(pchGenXbox, kUIGenTypeNone);
#else
	UIGen *pWindow = ui_GenFind(pchGenPC, kUIGenTypeNone);
#endif
	bool bResult = ui_GenAddWindow(pWindow);
	if (!pWindow)
	{
#if _XBOX
		GenErrorf("%s: Invalid Gen name %s", __FUNCTION__, pchGenXbox);
#else
		GenErrorf("%s: Invalid Gen name %s", __FUNCTION__, pchGenPC);
#endif
	}
	return bResult;
}


// Hide a gen on the window layer.
AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME("GenRemoveWindow") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool ui_GenCmdRemoveWindow(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen)
{
	UIGen *pWindow = ui_GenFind(pchGen, kUIGenTypeNone);
	bool bResult = ui_GenRemoveWindow(pWindow, false);
	if (!pWindow)
		GenErrorf("%s: Invalid Gen name %s", __FUNCTION__, pchGen);
	return bResult;
}

// Hide a gen on the window layer that is specific to PC or Xbox.
AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME("GenRemoveWindowPCXbox") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool ui_GenCmdRemoveWindowPCXbox(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGenPC, ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGenXbox)
{
#if _XBOX
	UIGen *pWindow = ui_GenFind(pchGenXbox, kUIGenTypeNone);
#else
	UIGen *pWindow = ui_GenFind(pchGenPC, kUIGenTypeNone);
#endif
	bool bResult = ui_GenRemoveWindow(pWindow, false);
	if (!pWindow)
	{
#if _XBOX
		GenErrorf("%s: Invalid Gen name %s", __FUNCTION__, pchGenXbox);
#else
		GenErrorf("%s: Invalid Gen name %s", __FUNCTION__, pchGenPC);
#endif
	}
	return bResult;
}

// Show a gen on the modal layer.
AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME("GenAddModal") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool ui_GenCmdAddModal(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen)
{
	UIGen *pWindow = ui_GenFind(pchGen, kUIGenTypeNone);
	bool bResult = ui_GenAddModalPopup(pWindow);
	if (!pWindow)
		GenErrorf("%s: Invalid Gen name %s", __FUNCTION__, pchGen);
	return bResult;
}

// Hide a gen on the modal layer.
AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME("GenRemoveModal") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool ui_GenCmdRemoveModal(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen)
{
	UIGen *pWindow = ui_GenFind(pchGen, kUIGenTypeNone);
	bool bResult = ui_GenRemoveWindow(pWindow, false);
	if (!pWindow)
		GenErrorf("%s: Invalid Gen name %s", __FUNCTION__, pchGen);
	return bResult;
}

// Send a message to a gen.
AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME("GenSendMessage") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool ui_GenCmdSendMessage(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen, const char *pchMessage)
{
	UIGen *pGen = ui_GenFind(pchGen, kUIGenTypeNone);
	bool bResult = pGen ? ui_GenSendMessage(pGen, pchMessage) : false;
	if (!pGen)
		GenErrorf("%s: Invalid Gen name %s", __FUNCTION__, pchGen);
//	else if (!bResult)
//		GenErrorf("%s: %s: Invalid message name %s", __FUNCTION__, pGen->pchName, pchMessage);
	return bResult;
}

// Set a value on a gen
AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME("GenSetValue") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool ui_GenCmdSetValue(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen, const char *pchVar, F32 fValue, const char *pchString)
{
	UIGen *pGen = ui_GenFind(pchGen, kUIGenTypeNone);	
	if (!pGen)
	{
		GenErrorf("%s: Invalid Gen name %s", __FUNCTION__, pchGen);
		return false;
	}
	else
	{
		UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar);
		if (pGlob)
		{
			pGlob->fFloat = fValue;
			pGlob->iInt = fValue;
			estrCopy2(&pGlob->pchString, pchString);
		}
		else
		{
			GenErrorf("%s: Variable %s not found in gen %s", __FUNCTION__, pchVar, pGen->pchName);
			return false;			
		}
		return true;
	}
}

// Reload a gen from file by name, useful on the Xbox 360.
AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME("GenReload") ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Interface);
void ui_GenCmdReload(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen)
{
	UIGen *pGen = ui_GenFind(pchGen, kUIGenTypeNone);
	if (pGen)
	{
		S32 i;
#if _XBOX
		extern bool g_xbox_local_hoggs_hack;
		if (fileIsUsingDevData() && !g_xbox_local_hoggs_hack)
			FolderCacheSetMode(FOLDER_CACHE_MODE_FILESYSTEM_ONLY);
#endif
		for (i = 0; i < eaSize(&pGen->eaBorrowed); i++)
			ui_GenCmdReload(REF_STRING_FROM_HANDLE(pGen->eaBorrowed[i]->hGen));
		ParserReloadFileToDictionary(pGen->pchFilename, UI_GEN_DICTIONARY);
	}
}

// Clone and destroy a UIGen, useful for memory leak testing.
AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME("GenCloneAndDestroy") ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Interface);
void ui_GenCmdCloneAndDestroy(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen)
{
	UIGen *pGen = RefSystem_ReferentFromString("UIGen", pchGen);
	if (pGen)
	{
		S32 i;
		for (i = 0; i < 100; i++)
		{
			UIGen *pNew = ui_GenClone(pGen);
			UIGen *pRoot = ui_GenFind("Root", kUIGenTypeNone);
			ui_GenUpdateCB(pNew, pRoot);
			ui_GenLayoutCB(pNew, pRoot);
			StructDestroy(parse_UIGen, pNew);
		}
	}
}

// Set a float override on a gen.
AUTO_COMMAND ACMD_NAME("GenSetEarlyOverrideFloat") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool ui_GenCmdSetEarlyOverrideFloat(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen, const char *pchField, F32 fValue)
{
	UIGen *pGen = RefSystem_ReferentFromString(UI_GEN_DICTIONARY, pchGen);
	if (pGen)
	{
		MultiVal val = {0};
		MultiValSetFloat(&val, fValue);
		return ui_GenSetEarlyOverrideField(pGen, pchField, &val);
	}
	else
	{
		GenErrorf("Gen %s not found when setting override", pchGen);
		return false;
	}
}

// Add a new jail to the screen.
AUTO_COMMAND ACMD_NAME("GenJailShow") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool ui_GenCmdJailShow(ACMD_NAMELIST("UIGenJailDef", REFDICTIONARY) const char *pchJail)
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
AUTO_COMMAND ACMD_NAME("GenJailHide") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool ui_GenCmdJailHide(ACMD_NAMELIST("UIGenJailDef", REFDICTIONARY) const char *pchJail)
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
AUTO_COMMAND ACMD_NAME("GenJailAdd") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool ui_GenCmdJailAddGen(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen)
{
	UIGen *pGen = RefSystem_ReferentFromString(UI_GEN_DICTIONARY, pchGen);
	if (pGen)
	{
		return !!ui_GenJailKeeperAdd(NULL, pGen);
	}
	else
	{
		GenErrorf("Gen %s not found when setting override", pchGen);
		return false;
	}
}

// Remove a gen from jail.
AUTO_COMMAND ACMD_NAME("GenJailRemove") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool ui_GenCmdJailRemoveGen(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen)
{
	UIGen *pGen = RefSystem_ReferentFromString(UI_GEN_DICTIONARY, pchGen);
	if (pGen)
	{
		return ui_GenJailKeeperRemove(NULL, pGen);
	}
	else
	{
		GenErrorf("Gen %s not found when setting override", pchGen);
		return false;
	}
}

// Remove all gens in a given jail cell block.
AUTO_COMMAND ACMD_NAME("GenJailRemoveAll") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool ui_GenCmdJailRemoveAll(const char *pchCellBlock)
{
	return ui_GenJailKeeperRemoveAll(NULL, pchCellBlock);
}

// Like GenSendMessage, but takes Message, Gen as arguments instead of Gen, Message.
AUTO_COMMAND ACMD_NAME("GenMessageSend") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool ui_GenCmdMessageSend(const char *pchMessage, ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen)
{
	UIGen *pGen = ui_GenFind(pchGen, kUIGenTypeNone);
	bool bResult = pGen ? ui_GenSendMessage(pGen, pchMessage) : false;
	if (!pGen)
		GenErrorf("%s: Invalid Gen name %s", __FUNCTION__, pchGen);
//	else if (!bResult)
//		GenErrorf("%s: %s: Invalid message name %s", __FUNCTION__, pGen->pchName, pchMessage);
	return bResult;
}

// Start a UI tutorial
AUTO_COMMAND ACMD_NAME("GenTutorialStart") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool ui_GenCmdTutorialStart(ACMD_NAMELIST("UIGenTutorial", REFDICTIONARY) const char *pchTutorial)
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
AUTO_COMMAND ACMD_NAME("GenTutorialReset") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool ui_GenCmdTutorialReset(void)
{
	return ui_GenTutorialReset(NULL);
}

// Stop the current UI tutorial
AUTO_COMMAND ACMD_NAME("GenTutorialStop") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool ui_GenCmdTutorialStop(void)
{
	return ui_GenTutorialStop(NULL);
}

// Advance to the next step in the current UI tutorial
AUTO_COMMAND ACMD_NAME("GenTutorialPrevious") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool ui_GenCmdTutorialPrevious(void)
{
	return ui_GenTutorialPrevious(NULL);
}

// Advance to the next step in the current UI tutorial
AUTO_COMMAND ACMD_NAME("GenTutorialNext") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool ui_GenCmdTutorialNext(void)
{
	return ui_GenTutorialNext(NULL);
}

// Print the current UI screen resolution.
AUTO_COMMAND ACMD_NAME(ui_resolution) ACMD_ACCESSLEVEL(0);
const char *ui_CmdResolution(CmdContext *pContext)
{
	static char ach[32];
	sprintf(ach, "%dx%d", g_ui_State.screenWidth, g_ui_State.screenHeight);
	return ach;
}

AUTO_STRUCT;
typedef struct GenDumpExprRef
{
	const char *pchName; AST(NAME(Func) POOL_STRING STRUCTPARAM)
	const char **ppchProjects; AST(NAME(Project) POOL_STRING)
} GenDumpExprRef;

AUTO_STRUCT;
typedef struct GenDumpExprState
{
	ExprFuncDesc **eaUnusedFuncDescs; NO_AST
	ExprFuncDesc **eaUsedFuncDescs; NO_AST
	GenDumpExprRef **eaUsedFuncs; AST(NAME(UsedFunc) POOL_STRING)
	GenDumpExprRef **eaUnusedFuncs; AST(NAME(UnusedFunc) POOL_STRING)
} GenDumpExprState;

static int eaFindExprRef(GenDumpExprRef ***peaRefs, const char *pchFuncName)
{
	int i;
	pchFuncName = allocFindString(pchFuncName);
	if (!pchFuncName)
		return -1;
	for (i = eaSize(peaRefs) - 1; i >= 0; --i)
	{
		if ((*peaRefs)[i]->pchName == pchFuncName)
			break;
	}
	return i;
}

static bool GenDumpExprFunctions(Expression *pExpr, UIGen *pGen, const char *pchPathString, GenDumpExprState *pContext)
{
	S32 i, s = beaSize(&pExpr->postfixEArray);
	for (i = 0; i < s; i++)
	{
		MultiVal *pVal = &pExpr->postfixEArray[i];
		if (pVal->type == MULTIOP_FUNCTIONCALL)
		{
			ExprFuncDesc *pFuncDesc = NULL;
			stashAddressFindPointer(globalFuncTable, allocFindString(pVal->str), &pFuncDesc);

			if (!pFuncDesc)
			{
				printf("DEBUG: '%s' (or an InlineChild) uses undefined expression function '%s'?\n", pGen->pchName, pVal->str);
				continue;
			}

			if (eaFindAndRemove(&pContext->eaUnusedFuncDescs, pFuncDesc) >= 0)
			{
				eaPush(&pContext->eaUsedFuncDescs, pFuncDesc);
			}
			else if (eaFind(&pContext->eaUsedFuncDescs, pFuncDesc) < 0)
			{
				printf("DEBUG: '%s' (or an InlineChild) refers to invalid expression '%s'?\n", pGen->pchName, pVal->str);
			}
		}
	}
	return true;
}

bool g_ui_GenDumpExprFunctions = false;

// Dump the expression functions used by all the UIGens.
AUTO_COMMAND ACMD_NAME("GenDumpExprFunctions") ACMD_CMDLINE ACMD_ACCESSLEVEL(9);
bool ui_GenCmdDumpExprFunctions(CmdContext *pContext, bool bClear)
{
	if (pContext && pContext->eHowCalled == CMD_CONTEXT_HOWCALLED_COMMANDLINE)
	{
		// Since it's called on the command line, odds are the person is just going through
		// the projects dumping the used expression functions. Wait for the UIGen system to load
		// then quit afterwards.
		g_ui_GenDumpExprFunctions = true;
	}
	else
	{
		static char *s_apchAllowedTags[] = { "UIGen", "gameutil", "util", "entityutil" };
		GenDumpExprState State = {0};
		GenDumpExprRef *pRef;
		S32 i, pos;
		const char *pchProject = allocAddString(GetProjectName());

		if (!bClear)
		{
			// Read in the state file
			ParserReadTextFile("C:\\UIGenExpressionFunctions.txt", parse_GenDumpExprState, &State, 0);
		}

		FOR_EACH_IN_STASHTABLE(globalFuncTable, ExprFuncDesc, pFuncDesc);
		{
			eaPush(&State.eaUnusedFuncDescs, pFuncDesc);
		}
		FOR_EACH_END;

		FOR_EACH_IN_REFDICT("UIGen", UIGen, pGen);
		{
			ParserScanForSubstruct(parse_UIGen, pGen, parse_Expression, 0, 0, GenDumpExprFunctions, &State);
		}
		FOR_EACH_END;

		for (i = eaSize(&State.eaUsedFuncDescs) - 1; i >= 0; --i)
		{
			ExprFuncDesc *pFuncDesc = State.eaUsedFuncDescs[i];
			const char *pchName = allocFindString(pFuncDesc->funcName);
			if ((pos = eaFindExprRef(&State.eaUnusedFuncs, pchName)) >= 0)
			{
				pRef = eaRemove(&State.eaUnusedFuncs, pos);
				eaPush(&State.eaUsedFuncs, pRef);
				eaPushUnique(&pRef->ppchProjects, pchProject);
			}
			else if (eaFindExprRef(&State.eaUsedFuncs, pchName) < 0)
			{
				pRef = StructCreate(parse_GenDumpExprRef);
				pRef->pchName = pchName;
				eaPushUnique(&pRef->ppchProjects, pchProject);
				eaPush(&State.eaUsedFuncs, pRef);
			}
		}

		for (i = eaSize(&State.eaUnusedFuncDescs) - 1; i >= 0; --i)
		{
			ExprFuncDesc *pFuncDesc = State.eaUnusedFuncDescs[i];
			const char *pchName = allocFindString(pFuncDesc->funcName);
			if ((pos = eaFindExprRef(&State.eaUsedFuncs, pchName)) < 0 && eaFindExprRef(&State.eaUnusedFuncs, pchName) < 0)
			{
				pRef = StructCreate(parse_GenDumpExprRef);
				pRef->pchName = pchName;
				eaPush(&State.eaUnusedFuncs, pRef);
			}
			else if (pos >= 0)
			{
				eaPushUnique(&State.eaUsedFuncs[pos]->ppchProjects, pchProject);
			}
		}

		// Write out the functions
		ParserWriteTextFile("C:\\UIGenExpressionFunctions.txt", parse_GenDumpExprState, &State, 0, 0);

		eaDestroy(&State.eaUnusedFuncDescs);
		eaDestroy(&State.eaUsedFuncDescs);
		eaDestroyStruct(&State.eaUnusedFuncs, parse_GenDumpExprRef);
		eaDestroyStruct(&State.eaUsedFuncs, parse_GenDumpExprRef);
	}
	return true;
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR("GenDumpExprFunctions");
bool ui_GenCmdDumpExprFunctionsEmpty(CmdContext *pContext)
{
	return globCmdParsef("%s 0", pContext->commandString);
}

#include "UIGenCommands_c_ast.c"
