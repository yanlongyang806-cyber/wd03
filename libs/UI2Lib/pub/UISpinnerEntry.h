#ifndef UI_SPINNER_ENTRY_H
#define UI_SPINNER_ENTRY_H
GCC_SYSTEM

#include "UICore.h"

typedef struct UITextEntry UITextEntry;
typedef struct UISpinner UISpinner;
typedef struct UIPane UIPane;

typedef struct UISpinnerEntry
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE);
	UITextEntry *pEntry;
	UISpinner *pSpinner;

	UIActivationFunc cbChanged;
	UserData pChangedData;
} UISpinnerEntry;

SA_RET_NN_VALID UISpinnerEntry *ui_SpinnerEntryCreate(F32 fMin, F32 fMax, F32 fStep, F32 fCurrent, bool bIsFloat);
void ui_SpinnerEntryInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UISpinnerEntry *pSpinEntry, F32 fMin, F32 fMax, F32 fStep, F32 fCurrent, bool bIsFloat);
void ui_SpinnerEntryTick(SA_PARAM_NN_VALID UISpinnerEntry *pSpinEntry, UI_PARENT_ARGS);
void ui_SpinnerEntryDraw(SA_PARAM_NN_VALID UISpinnerEntry *pSpinEntry, UI_PARENT_ARGS);
void ui_SpinnerEntryFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UISpinnerEntry *pSpinEntry);

F32 ui_SpinnerEntryGetValue(SA_PARAM_NN_VALID UISpinnerEntry *pSpinEntry);
F32 ui_SpinnerEntrySetValue(SA_PARAM_NN_VALID UISpinnerEntry *pSpinEntry, F32 fValue);
F32 ui_SpinnerEntrySetValueAndCallback(SA_PARAM_NN_VALID UISpinnerEntry *pSpinEntry, F32 fValue);

void ui_SpinnerEntrySetCallback(SA_PARAM_NN_VALID UISpinnerEntry *pSpinEntry, UIActivationFunc cbChanged, UserData pChangedData);
void ui_SpinnerEntrySetBounds(SA_PARAM_NN_VALID UISpinnerEntry *pSpinEntry, F32 fMin, F32 fMax, F32 fStep);


typedef struct UIMultiSpinnerEntry
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE);
	
	UIPane *pPane;
	UISpinnerEntry **ppEntries;

	UIActivationFunc cbChanged;
	UserData pChangedData;
} UIMultiSpinnerEntry;


SA_RET_NN_VALID UIMultiSpinnerEntry *ui_MultiSpinnerEntryCreate(F32 fMin, F32 fMax, F32 fStep, F32 fCurrent, int iCount, bool bIsFloat);

F32 ui_MultiSpinnerEntryGetIdxValue(SA_PARAM_NN_VALID UIMultiSpinnerEntry *pSpinEntry, int iIdx);
F32 ui_MultiSpinnerEntrySetIdxValue(SA_PARAM_NN_VALID UIMultiSpinnerEntry *pSpinEntry, F32 fValue, int iIdx);

void ui_MultiSpinnerEntryGetValue(SA_PARAM_NN_VALID UIMultiSpinnerEntry *pSpinEntry, F32 *fValues, int iCount);
void ui_MultiSpinnerEntrySetValue(SA_PARAM_NN_VALID UIMultiSpinnerEntry *pSpinEntry, F32 *fValues, int iCount);
void ui_MultiSpinnerEntrySetValueAndCallback(SA_PARAM_NN_VALID UIMultiSpinnerEntry *pSpinEntry, F32 *fValues, int iCount);

void ui_MultiSpinnerEntrySetCallback(SA_PARAM_NN_VALID UIMultiSpinnerEntry *pSpinEntry, UIActivationFunc cbChanged, UserData pChangedData);
void ui_MultiSpinnerEntrySetBounds(SA_PARAM_NN_VALID UIMultiSpinnerEntry *pSpinEntry, F32 fMin, F32 fMax, F32 fStep);
void ui_MultiSpinnerEntrySetVecBounds(UIMultiSpinnerEntry *pSpinEntry, F32 *fMin, F32 *fMax, F32 *fStep);

#endif
