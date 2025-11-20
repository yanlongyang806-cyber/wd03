/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef UI_FXBUTTON_H
#define UI_FXBUTTON_H
GCC_SYSTEM

#include "UIButton.h"
#include "UIWindow.h"
#include "UIDictionaryEntry.h"
#include "dynFxInfo.h"
#include "MultiEditField.h"
#include "UICheckButton.h"
#include "UIBoxSizer.h"

typedef struct UIFXButton UIFXButton;
typedef void (*UIFXStopFunc)(UserData);
typedef void (*UIFXChangeFunc)(UIAnyWidget *, DynParamBlock *, UserData);

typedef struct FxParamGroup
{
	const char *pcName;
	eDynParamType type;
	const char *pcDictName;

	UICheckButton *pParamCheckButton;
	UIWidget *pParamWidget;
} FxParamGroup;

AUTO_STRUCT;
typedef struct FxData
{
	F32 FXDataHue;
	DynParamBlock *FXDataBlock;
} FxData;

typedef struct UIFXWindow
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE UI_WINDOW_TYPE);
	UIFXButton *pButton;	//Back-pointer
	UIBoxSizer *pBoxSizer;
	UIButton *pCancelButton;
	UIButton *pSelectButton;
	MEField *pHueField;
	UICheckButton *pHueCheckButton;
	UITextEntry *pHueEntry;
	bool bSelectPressed;
	FxParamGroup **eaParamGroups;
	FxData *oldData, *newData;
} UIFXWindow;

typedef struct UIFXButton
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE UI_BUTTON_TYPE);
	UIFXWindow *activeWindow;
	UIFXChangeFunc changedFunc;
	UIFXStopFunc stopFunc, selectFunc1, selectFunc2, changedFuncShort;
	UserData changedData, changedDataShort, stopData, selectData1, selectData2;
	F32 *pHue, *pSetHue;
	char **pcParams;
	DynFxInfo *pFX;
	DynParamBlock *pSetBlock;
} UIFXButton;

SA_RET_NN_VALID UIFXWindow *ui_FXWindowCreate(SA_PARAM_OP_VALID UIFXButton *fxbutton, F32 x, F32 y);
SA_RET_NN_VALID UIFXButton *ui_FXButtonCreate(F32 x, F32 y, DynFxInfo *pFX, F32 *pHue, char **pcParams);
void ui_FXButtonDestroy(SA_PARAM_NN_VALID UIFXButton *fxbutton);
void ui_FXButtonUpdate(SA_PARAM_NN_VALID UIFXButton *fxbutton, DynFxInfo *pFX, F32 *pHue, char **pcParams);
void ui_FXButtonSetChangedCallback(SA_PARAM_NN_VALID UIFXButton *fxbutton, UIFXChangeFunc changedFunc, UserData changedData);
void ui_FXButtonSetChangedCallbackShort(SA_PARAM_NN_VALID UIFXButton *fxbutton, UIFXStopFunc changedFunc, UserData changedData);
void ui_FXButtonSetStopCallback(SA_PARAM_NN_VALID UIFXButton *fxbutton, UIFXStopFunc stopFunc, UserData stopData);
void ui_FXButtonSetSelectVars(SA_PARAM_NN_VALID UIFXButton *fxbutton, F32 *pSetHue, DynParamBlock *pSetBlock);
void ui_FXButtonSetSelectCallback1(SA_PARAM_NN_VALID UIFXButton *fxbutton, UIFXStopFunc selectFunc, UserData selectData);
void ui_FXButtonSetSelectCallback2(SA_PARAM_NN_VALID UIFXButton *fxbutton, UIFXStopFunc selectFunc, UserData selectData);	//Reason for two: Could potentially want separate functions called for Hue & Params

#endif
