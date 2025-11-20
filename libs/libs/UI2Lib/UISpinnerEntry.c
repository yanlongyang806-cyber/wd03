#include "inputMouse.h"
#include "GfxClipper.h"
#include "UISpinnerEntry.h"
#include "UITextEntry.h"
#include "UISpinner.h"
#include "UIPane.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

UISpinnerEntry *ui_SpinnerEntryCreate(F32 fMin, F32 fMax, F32 fStep, F32 fCurrent, bool bIsFloat)
{
	UISpinnerEntry *pSpinEntry = calloc(1, sizeof(UISpinnerEntry));
	ui_SpinnerEntryInitialize(pSpinEntry, fMin, fMax, fStep, fCurrent, bIsFloat);
	return pSpinEntry;
}

// The spinner has the canonical copy of the value data.
static void ui_SpinnerEntrySpinnerCallback(UISpinner *pSpinner, UISpinnerEntry *pSpinEntry)
{
	ui_SpinnerEntrySetValueAndCallback(pSpinEntry, ui_SpinnerGetValue(pSpinEntry->pSpinner));
}

static void ui_SpinnerEntryEntryCallback(UITextEntry *pEntry, UISpinnerEntry *pSpinEntry)
{
	F32 fValue = atof(ui_TextEntryGetText(pEntry));
	ui_SpinnerEntrySetValueAndCallback(pSpinEntry, fValue);
}

void ui_SpinnerEntryInitialize(UISpinnerEntry *pSpinEntry, F32 fMin, F32 fMax, F32 fStep, F32 fCurrent, bool bIsFloat)
{
	F32 fHeight = 0.f;
	ui_WidgetInitialize(UI_WIDGET(pSpinEntry), ui_SpinnerEntryTick, ui_SpinnerEntryDraw, ui_SpinnerEntryFreeInternal, NULL, NULL);
	pSpinEntry->pEntry = ui_TextEntryCreate("", 0, 0);
	pSpinEntry->pSpinner = ui_SpinnerCreate(0, 0, fMin, fMax, fStep, fCurrent, ui_SpinnerEntrySpinnerCallback, pSpinEntry);
	ui_TextEntrySetFinishedCallback(pSpinEntry->pEntry, ui_SpinnerEntryEntryCallback, pSpinEntry);
	MAX1(fHeight, UI_WIDGET(pSpinEntry->pEntry)->height);
	MAX1(fHeight, UI_WIDGET(pSpinEntry->pSpinner)->height);
	ui_WidgetAddChild(UI_WIDGET(pSpinEntry), UI_WIDGET(pSpinEntry->pSpinner));
	ui_WidgetAddChild(UI_WIDGET(pSpinEntry), UI_WIDGET(pSpinEntry->pEntry));
	ui_WidgetSetPositionEx(UI_WIDGET(pSpinEntry->pSpinner), 0, 0, 0, 0, UITopRight);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSpinEntry), 1.f, fHeight, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSpinEntry->pSpinner), 16, 1.f, UIUnitFixed, UIUnitPercentage);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSpinEntry->pEntry), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pSpinEntry->pEntry), 0, 16.f, 0, 0);

	if (bIsFloat)
	{
		ui_TextEntrySetValidateCallback(pSpinEntry->pEntry, ui_TextEntryValidationFloatOnly, NULL);
	}
	else
	{
		ui_TextEntrySetValidateCallback(pSpinEntry->pEntry, ui_TextEntryValidationIntegerOnly, NULL);
	}
	ui_SpinnerEntrySetValue(pSpinEntry, fCurrent);
}

void ui_SpinnerEntryTick(UISpinnerEntry *pSpinEntry, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pSpinEntry);
	UI_TICK_EARLY(pSpinEntry, true, true);
	UI_TICK_LATE(pSpinEntry);
}

void ui_SpinnerEntryDraw(UISpinnerEntry *pSpinEntry, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pSpinEntry);
	UI_DRAW_EARLY(pSpinEntry);
	UI_DRAW_LATE(pSpinEntry);
}

void ui_SpinnerEntryFreeInternal(UISpinnerEntry *pSpinEntry)
{
	ui_WidgetFreeInternal(UI_WIDGET(pSpinEntry));
}

F32 ui_SpinnerEntryGetValue(UISpinnerEntry *pSpinEntry)
{
	return ui_SpinnerGetValue(pSpinEntry->pSpinner);
}

F32 ui_SpinnerEntrySetValue(UISpinnerEntry *pSpinEntry, F32 fValue)
{
	char achValue[100];
	ui_SpinnerSetValue(pSpinEntry->pSpinner, fValue);
	fValue = ui_SpinnerEntryGetValue(pSpinEntry);
	sprintf(achValue, "%g", fValue);
	ui_TextEntrySetText(pSpinEntry->pEntry, achValue);
	return fValue;
}

F32 ui_SpinnerEntrySetValueAndCallback(UISpinnerEntry *pSpinEntry, F32 fValue)
{
	ui_SpinnerEntrySetValue(pSpinEntry, fValue);
	if (pSpinEntry->cbChanged)
		pSpinEntry->cbChanged(pSpinEntry, pSpinEntry->pChangedData);
	return ui_SpinnerGetValue(pSpinEntry->pSpinner);
}

void ui_SpinnerEntrySetCallback(UISpinnerEntry *pSpinEntry, UIActivationFunc cbChanged, UserData pChangedData)
{
	pSpinEntry->cbChanged = cbChanged;
	pSpinEntry->pChangedData = pChangedData;
}

void ui_SpinnerEntrySetBounds(UISpinnerEntry *pSpinEntry, F32 fMin, F32 fMax, F32 fStep)
{
	F32 fCurrent = pSpinEntry->pSpinner->currentVal;

	ui_SpinnerSetBounds(pSpinEntry->pSpinner, fMin, fMax, fStep);

	// Make sure to set whether we are integer or float validation now that bounds have changed
	if (fCurrent == (S32)fCurrent && fStep == (U32)fStep)
		ui_TextEntrySetValidateCallback(pSpinEntry->pEntry, ui_TextEntryValidationIntegerOnly, NULL);
	else
		ui_TextEntrySetValidateCallback(pSpinEntry->pEntry, ui_TextEntryValidationFloatOnly, NULL);

}


//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////


static void ui_MultiSpinnerEntryTick(UIMultiSpinnerEntry *pSpinEntry, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pSpinEntry);
	UI_TICK_EARLY(pSpinEntry, true, true);
	UI_TICK_LATE(pSpinEntry);
}

static void ui_MultiSpinnerEntryDraw(UIMultiSpinnerEntry *pSpinEntry, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pSpinEntry);
	UI_DRAW_EARLY(pSpinEntry);
	UI_DRAW_LATE(pSpinEntry);
}

static void ui_MultiSpinnerEntryFreeInternal(UIMultiSpinnerEntry *pSpinEntry)
{
	eaDestroy(&pSpinEntry->ppEntries);
	ui_WidgetFreeInternal(UI_WIDGET(pSpinEntry));
}

static void ui_MultiSpinnerInernalSpinnerChanged(UISpinnerEntry *pEntry, UIMultiSpinnerEntry *pSpinEntry)
{
	if(pSpinEntry->cbChanged)
		pSpinEntry->cbChanged(pSpinEntry, pSpinEntry->pChangedData);
}

static void ui_MultiSpinnerEntryInitialize(UIMultiSpinnerEntry *pSpinEntry, F32 fMin, F32 fMax, F32 fStep, F32 fCurrent, int iCount, bool bIsFloat)
{
	int i;
	F32 fHeight = 0.f;
	F32 fWidth = 0.f;
	ui_WidgetInitialize(UI_WIDGET(pSpinEntry), ui_MultiSpinnerEntryTick, ui_MultiSpinnerEntryDraw, ui_MultiSpinnerEntryFreeInternal, NULL, NULL);
	
	pSpinEntry->pPane = ui_PaneCreate(0, 0, 1, 1, UIUnitPercentage, UIUnitPercentage, 0);
	pSpinEntry->pPane->invisible = true;
	ui_WidgetAddChild(UI_WIDGET(pSpinEntry), UI_WIDGET(pSpinEntry->pPane));

	assert(iCount > 1);
	for ( i=0; i < iCount; i++ )
	{
		UISpinnerEntry *pEntry = ui_SpinnerEntryCreate(fMin, fMax, fStep, fCurrent, bIsFloat);

		MAX1(fHeight, UI_WIDGET(pEntry)->height);

		ui_WidgetSetPositionEx(UI_WIDGET(pEntry), 0, 0, i * 1.0f/iCount, 0, UITopLeft);
		ui_WidgetSetDimensionsEx(UI_WIDGET(pEntry), 1.0f/iCount, 1, UIUnitPercentage, UIUnitPercentage);
		UI_WIDGET(pEntry)->rightPad = ((i < iCount-1) ? 2 : 1);
		ui_SpinnerEntrySetCallback(pEntry, ui_MultiSpinnerInernalSpinnerChanged, pSpinEntry);

		ui_PaneAddChild(pSpinEntry->pPane, pEntry);
		eaPush(&pSpinEntry->ppEntries, pEntry);
	}
	fWidth = iCount*50.0f;

	ui_WidgetSetDimensionsEx(UI_WIDGET(pSpinEntry), fWidth, fHeight, UIUnitFixed, UIUnitFixed);
}

UIMultiSpinnerEntry *ui_MultiSpinnerEntryCreate(F32 fMin, F32 fMax, F32 fStep, F32 fCurrent, int iCount, bool bIsFloat)
{
	UIMultiSpinnerEntry *pSpinEntry = calloc(1, sizeof(UIMultiSpinnerEntry));
	ui_MultiSpinnerEntryInitialize(pSpinEntry, fMin, fMax, fStep, fCurrent, iCount, bIsFloat);
	return pSpinEntry;
}

F32 ui_MultiSpinnerEntryGetIdxValue(SA_PARAM_NN_VALID UIMultiSpinnerEntry *pSpinEntry, int iIdx)
{
	assert(iIdx >= 0 && iIdx < eaSize(&pSpinEntry->ppEntries));
	return ui_SpinnerEntryGetValue(pSpinEntry->ppEntries[iIdx]);
}

F32 ui_MultiSpinnerEntrySetIdxValue(SA_PARAM_NN_VALID UIMultiSpinnerEntry *pSpinEntry, F32 fValue, int iIdx)
{
	assert(iIdx >= 0 && iIdx < eaSize(&pSpinEntry->ppEntries));
	return ui_SpinnerEntrySetValue(pSpinEntry->ppEntries[iIdx], fValue);
}


void ui_MultiSpinnerEntryGetValue(SA_PARAM_NN_VALID UIMultiSpinnerEntry *pSpinEntry, F32 *fValues, int iCount)
{
	int i;
	assert(iCount == eaSize(&pSpinEntry->ppEntries));
	for ( i=0; i < iCount; i++ ) {
		fValues[i] = ui_SpinnerEntryGetValue(pSpinEntry->ppEntries[i]);
	}
}

void ui_MultiSpinnerEntrySetValue(SA_PARAM_NN_VALID UIMultiSpinnerEntry *pSpinEntry, F32 *fValues, int iCount)
{
	int i;
	assert(iCount == eaSize(&pSpinEntry->ppEntries));
	for ( i=0; i < iCount; i++ ) {
		ui_SpinnerEntrySetValue(pSpinEntry->ppEntries[i], fValues[i]);
	}
}

void ui_MultiSpinnerEntrySetValueAndCallback(SA_PARAM_NN_VALID UIMultiSpinnerEntry *pSpinEntry, F32 *fValues, int iCount)
{
	ui_MultiSpinnerEntrySetValue(pSpinEntry, fValues, iCount);
	if(pSpinEntry->cbChanged)
		pSpinEntry->cbChanged(pSpinEntry, pSpinEntry->pChangedData);
}

void ui_MultiSpinnerEntrySetCallback(SA_PARAM_NN_VALID UIMultiSpinnerEntry *pSpinEntry, UIActivationFunc cbChanged, UserData pChangedData)
{
	pSpinEntry->cbChanged = cbChanged;
	pSpinEntry->pChangedData = pChangedData;
}


void ui_MultiSpinnerEntrySetBounds(UIMultiSpinnerEntry *pSpinEntry, F32 fMin, F32 fMax, F32 fStep)
{
	int i;
	for ( i=0; i < eaSize(&pSpinEntry->ppEntries); i++ ) {
		ui_SpinnerEntrySetBounds(pSpinEntry->ppEntries[i], fMin, fMax, fStep);
	}
}

void ui_MultiSpinnerEntrySetVecBounds(UIMultiSpinnerEntry *pSpinEntry, F32 *fMin, F32 *fMax, F32 *fStep)
{
	int i;
	for ( i=0; i < eaSize(&pSpinEntry->ppEntries); i++ ) {
		ui_SpinnerEntrySetBounds(pSpinEntry->ppEntries[i], fMin[i], fMax[i], fStep[i]);
	}
}
