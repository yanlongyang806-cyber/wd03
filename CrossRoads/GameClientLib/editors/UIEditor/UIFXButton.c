/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "UIFXButton.h"
#include "UIInternal.h"
#include "GfxSpriteText.h"
#include "EditorManager.h"
#include "rgb_hsv.h"
#include "StringCache.h"

#include "AutoGen/dynFxInfo_h_ast.h"
#include "AutoGen/UIFXButton_h_ast.h"

#define BUTTON_BORDER 2
#define HUE_SLIDER_WIDTH 160
#define DEFAULT_WINDOW_X 50
#define DEFAULT_WINDOW_Y 50
#define MAX_HUE_VALUE_RED 360

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static void edit_FXChanged(UIFXWindow *pWindow, bool bRevert);

void edit_UpdateFXDrawFromWidgetEx(UIFXWindow *pWindow, bool bRevertUnchecked)
{
	int i, j;
	DynParamBlock *block = StructCreate(parse_DynParamBlock);

	if (pWindow->pButton->stopFunc)
	{
		pWindow->pButton->stopFunc(pWindow->pButton->stopData);
	}

	for (i = 0; i < eaSize(&pWindow->eaParamGroups); i++)
	{
		MultiVal *paramValue = NULL;
		if ((!(ui_CheckButtonGetState(pWindow->eaParamGroups[i]->pParamCheckButton))) && (bRevertUnchecked))
		{
			for (j = 0; j < eaSize(&pWindow->pButton->pFX->paramBlock.eaDefineParams); j++)
			{
				if (strcmp(pWindow->eaParamGroups[i]->pcName, pWindow->pButton->pFX->paramBlock.eaDefineParams[j]->pcParamName) == 0)
				{
					paramValue = MultiValCreate();
					MultiValCopy(paramValue, &(pWindow->pButton->pFX->paramBlock.eaDefineParams[j]->mvVal));
					break;
				}
			}
		}
		else if (ui_CheckButtonGetState(pWindow->eaParamGroups[i]->pParamCheckButton))
		{
			switch(pWindow->eaParamGroups[i]->type)
			{
				case edptString:
				{
					const char *str = NULL;
					if (pWindow->eaParamGroups[i]->pcDictName)
					{
						UIDictionaryEntry* pEntry = (UIDictionaryEntry*)pWindow->eaParamGroups[i]->pParamWidget;
						str = ui_DictionaryEntryGetText(pEntry);
					}
					else
					{
						UITextEntry* pEntry = (UITextEntry*)pWindow->eaParamGroups[i]->pParamWidget;
						str = ui_TextEntryGetText(pEntry);
					}

					paramValue = MultiValCreate();
					MultiValSetString(paramValue, str);
				}
				xcase edptNumber:
				{
					UISpinnerEntry* pEntry = (UISpinnerEntry*)pWindow->eaParamGroups[i]->pParamWidget;
					F64 val = ui_SpinnerEntryGetValue(pEntry);

					paramValue = MultiValCreate();
					MultiValSetFloat(paramValue, val);
				}
				xcase edptVector:
				{
					UIMultiSpinnerEntry* pEntry = (UIMultiSpinnerEntry*)pWindow->eaParamGroups[i]->pParamWidget;
					Vec3 vec;
					ui_MultiSpinnerEntryGetValue(pEntry, vec, 3);

					paramValue = MultiValCreate();
					MultiValSetVec3(paramValue, &vec);
				}
				xcase edptVector4:
				{
					UIMultiSpinnerEntry* pEntry = (UIMultiSpinnerEntry*)pWindow->eaParamGroups[i]->pParamWidget;
					Vec4 vec4;
					ui_MultiSpinnerEntryGetValue(pEntry, vec4, 4);

					paramValue = MultiValCreate();
					MultiValSetVec4(paramValue, &vec4);
				}
			}
		}

		if (paramValue)
		{
			DynDefineParam *param = StructCreate(parse_DynDefineParam);
			param->pcParamName = allocAddString(pWindow->eaParamGroups[i]->pcName);
			MultiValCopy(&param->mvVal, paramValue);
			eaPush(&block->eaDefineParams, param);

			MultiValDestroy(paramValue);
		}
	}

	if (eaSize(&block->eaDefineParams))
	{
		char *parserStr = NULL;
		ParserWriteText(&parserStr, parse_DynParamBlock, block, 0, 0, 0);
		StructCopyString(pWindow->pButton->pcParams, parserStr);
		estrDestroy(&parserStr);
	}
	else
	{
		// No properties in the block? Just remove it entirely.
		StructFreeStringSafe(pWindow->pButton->pcParams);
	}

	edit_FXChanged(pWindow, false);
	StructDestroy(parse_DynParamBlock, block);
}

static void FXWindowCancelButtonCallback(UIButton *button, UIFXWindow *pWindow)
{
	ui_WindowClose(UI_WINDOW(pWindow));
}

static void FXWindowSetParamBlock(DynParamBlock *dst, DynParamBlock *src)
{
	if (src && dst)
	{
		int i, j;
		for (i = 0; i < eaSize(&dst->eaDefineParams); i++)
		{
			for (j = 0; j < eaSize(&src->eaDefineParams); j++)
			{
				if (strcmp(dst->eaDefineParams[i]->pcParamName, src->eaDefineParams[j]->pcParamName) == 0)
				{
					MultiValCopy(&(dst->eaDefineParams[i]->mvVal), &(src->eaDefineParams[j]->mvVal));
					break;
				}
			}
		}
	}
}

static void FXWindowSelectButtonCallback(UIButton *button, UIFXWindow *pWindow)
{
	pWindow->bSelectPressed = true;

	if ((!pWindow->newData->FXDataHue) && (pWindow->pButton->pFX->fDefaultHue))
	{
		(*pWindow->pButton->pHue) = pWindow->newData->FXDataHue = MAX_HUE_VALUE_RED;
	}

	if (pWindow->pButton->pSetHue != NULL)
	{
		(*pWindow->pButton->pSetHue) = pWindow->newData->FXDataHue;
	}

	FXWindowSetParamBlock(&(pWindow->pButton->pFX->paramBlock), pWindow->newData->FXDataBlock);
	FXWindowSetParamBlock(pWindow->pButton->pSetBlock, pWindow->newData->FXDataBlock);

	if (pWindow->pButton->selectFunc1)
	{
		pWindow->pButton->selectFunc1(pWindow->pButton->selectData1);
	}

	if (pWindow->pButton->selectFunc2)
	{
		pWindow->pButton->selectFunc2(pWindow->pButton->selectData2);
	}

	edit_UpdateFXDrawFromWidgetEx(pWindow, true);
	ui_WindowClose(UI_WINDOW(pWindow));
}

static void OpenMaterialInEditor(UIDictionaryEntry *pEntry, UserData unused)
{
	emOpenFileEx(ui_DictionaryEntryGetText(pEntry), MATERIAL_DICT);
}

//A param changed
void edit_UpdateFXDrawFromWidget(UIAnyWidget *changedWidget, UIFXWindow *pWindow)
{
	edit_UpdateFXDrawFromWidgetEx(pWindow, false);
}

//Hue Entry Changed
void edit_UpdateFXDrawFromField(UITextEntry *pEntry, UIFXWindow *pWindow)
{
	F32 value;
	if ((sscanf(ui_TextEntryGetText(pEntry), "%f", &value) == 1) && (value >= 0) && (value <= 360))
	{
		if (pWindow->pButton->stopFunc)
		{
			pWindow->pButton->stopFunc(pWindow->pButton->stopData);
		}

		pWindow->newData->FXDataHue = value;
		MEFieldSetAndRefreshFromData(pWindow->pHueField, pWindow->oldData, pWindow->newData);
		(*pWindow->pButton->pHue) = pWindow->newData->FXDataHue;
		edit_FXChanged(pWindow, false);
	}
	else
	{
		char string[10];
		F32 hueValue = pWindow->newData->FXDataHue;
		if ((!hueValue) && (pWindow->pButton->pFX->fDefaultHue))
		{
			hueValue = MAX_HUE_VALUE_RED;
		}
		sprintf(string, "%0.2f", hueValue);
		ui_TextEntrySetText(pWindow->pHueEntry, string);
	}
}

//Hue Field Changed
void edit_UpdateFXDraw(MEField *pField, bool bFinished, UIFXWindow *pWindow)
{
	char string[10];
	F32 hueValue;
	if (pWindow->pButton->stopFunc)
	{
		pWindow->pButton->stopFunc(pWindow->pButton->stopData);
	}

	(*pWindow->pButton->pHue) = pWindow->newData->FXDataHue;
	hueValue = pWindow->newData->FXDataHue;
	if ((!hueValue) && (pWindow->pButton->pFX->fDefaultHue))
	{
		hueValue = MAX_HUE_VALUE_RED;
	}
	sprintf(string, "%0.2f", hueValue);
	ui_TextEntrySetText(pWindow->pHueEntry, string);
	edit_FXChanged(pWindow, false);
}

// This is called whenever any FX data changes to do cleanup
static void edit_FXChanged(UIFXWindow *pWindow, bool bRevert)
{
	int i, j;
	DynParamBlock *block;
	UIBoxSizer *hueBox = ui_BoxSizerCreate(UIHorizontal);
	UIBoxSizer *endBox = ui_BoxSizerCreate(UIHorizontal);
	char string[10];

	if (pWindow->pBoxSizer)
	{
		ui_BoxSizerFree(pWindow->pBoxSizer);
	}
	pWindow->pBoxSizer = ui_BoxSizerCreate(UIVertical);

	if (!pWindow->pHueField)
	{
		F32 hueValue;
		pWindow->pHueCheckButton = ui_CheckButtonCreate(0, 0, "Hue Shift", ((*pWindow->pButton->pHue) == pWindow->pButton->pFX->fDefaultHue));
		ui_CheckButtonSetToggledCallback(pWindow->pHueCheckButton, edit_UpdateFXDrawFromWidget, pWindow);

		pWindow->pHueField = MEFieldCreateSimple(kMEFieldType_Hue, pWindow->oldData, pWindow->newData, parse_FxData, "FXDataHue");
		MEFieldCreateThisWidget(pWindow->pHueField);
		MEFieldSetChangeCallback(pWindow->pHueField, edit_UpdateFXDraw, pWindow);
		ui_WidgetSetWidth(pWindow->pHueField->pUIWidget, HUE_SLIDER_WIDTH);
		ui_WidgetSetHeight(pWindow->pHueField->pUIWidget, ui_WidgetGetHeight(UI_WIDGET(pWindow->pHueCheckButton)));
		MEFieldRefreshFromData(pWindow->pHueField); // Need to refresh after setting range
		hueValue = pWindow->newData->FXDataHue;
		if ((!hueValue) && (pWindow->pButton->pFX->fDefaultHue))
		{
			hueValue = MAX_HUE_VALUE_RED;
		}
		sprintf(string, "%0.2f", hueValue);
		pWindow->pHueEntry = ui_TextEntryCreate(string, 0, 0);
		ui_TextEntrySetFinishedCallback(pWindow->pHueEntry, edit_UpdateFXDrawFromField, pWindow);
	}
	else if (!pWindow->pHueCheckButton->state)
	{
		(*pWindow->pButton->pHue) = pWindow->newData->FXDataHue = pWindow->pButton->pFX->fDefaultHue;
		sprintf(string, "%0.2f", pWindow->pButton->pFX->fDefaultHue);
		ui_TextEntrySetText(pWindow->pHueEntry, string);
		MEFieldRefreshFromData(pWindow->pHueField);
	}

	ui_SetActive(pWindow->pHueField->pUIWidget, pWindow->pHueCheckButton->state);
	ui_SetActive((UIWidget*)pWindow->pHueEntry, pWindow->pHueCheckButton->state);

	ui_BoxSizerAddWidget(hueBox, &(pWindow->pHueCheckButton->widget), 0, UIWidth, BUTTON_BORDER);
	ui_BoxSizerAddWidget(hueBox, pWindow->pHueField->pUIWidget, 25, UIWidth, BUTTON_BORDER);
	ui_BoxSizerAddWidget(hueBox, &pWindow->pHueEntry->widget, 10, UIWidth, BUTTON_BORDER);
	ui_BoxSizerAddSizer(pWindow->pBoxSizer, UI_SIZER(hueBox), 1, UIWidth, BUTTON_BORDER);

	for (i = 0; i < eaSize(&pWindow->eaParamGroups); i++)
	{
		FxParamGroup *paramGroup = pWindow->eaParamGroups[i];
		ui_WidgetQueueFree(UI_WIDGET(paramGroup->pParamCheckButton));
		ui_WidgetQueueFree(paramGroup->pParamWidget);
		free(paramGroup);
	}
	eaClear(&pWindow->eaParamGroups);

	if ((bRevert) && (pWindow->oldData->FXDataBlock))
	{
		block = pWindow->oldData->FXDataBlock;
	}
	else
	{
		if (pWindow->newData->FXDataBlock)
		{
			StructDestroy(parse_DynParamBlock, pWindow->newData->FXDataBlock);
		}

		block = StructCreateFromString(parse_DynParamBlock, *pWindow->pButton->pcParams);

		if (!block)
		{
			block = StructCreate(parse_DynParamBlock);
		}

		for (i = 0; i < eaSize(&pWindow->pButton->pFX->paramBlock.eaDefineParams); i++)
		{
			bool foundParam = false;
			eDynFxDictType eDictType = eDynFxDictType_None;

			// Check each parameter to make sure it's in the list of parameters exposed to the editor.
			for (j = 0; j < eaSize(&pWindow->pButton->pFX->eaEditorParams); j++)
			{
				if (pWindow->pButton->pFX->paramBlock.eaDefineParams[i]->pcParamName == pWindow->pButton->pFX->eaEditorParams[j]->pcParamName)
				{
					eDictType = pWindow->pButton->pFX->eaEditorParams[j]->eParamType;
					foundParam = true;
					break;
				}
			}

			if (foundParam)
			{
				FxParamGroup *paramGroup = calloc(1, sizeof(FxParamGroup));
				DynDefineParam *pExistingParam = NULL;
				UIBoxSizer *paramBox = ui_BoxSizerCreate(UIHorizontal);

				paramGroup->pcName = allocAddString(pWindow->pButton->pFX->paramBlock.eaDefineParams[i]->pcParamName);

				for (j = 0; j < eaSize(&block->eaDefineParams); j++)
				{
					if (block->eaDefineParams[j]->pcParamName == paramGroup->pcName)
					{
						pExistingParam = block->eaDefineParams[j];
						break;
					}
				}

				eaPush(&pWindow->eaParamGroups, paramGroup);

				// FIXME: None of the dictionaries except for
				//   materials exist on the client by default.
				if (eDictType != eDynFxDictType_Material)
				{
					eDictType = eDynFxDictType_None;
				}

				paramGroup->pParamCheckButton = ui_CheckButtonCreate(0, 0, paramGroup->pcName, !!pExistingParam);
				ui_CheckButtonSetToggledCallback(paramGroup->pParamCheckButton, edit_UpdateFXDrawFromWidget, pWindow);
				ui_BoxSizerAddWidget(paramBox, &(paramGroup->pParamCheckButton->widget), 0, UIWidth, BUTTON_BORDER);

				if (!pWindow->oldData->FXDataBlock)
				{
					//Set box's default checked or unchecked state based on whether or not it holds the default value
					ui_CheckButtonSetState(paramGroup->pParamCheckButton, pExistingParam != NULL);
				}

				switch(pWindow->pButton->pFX->paramBlock.eaDefineParams[i]->mvVal.type)
				{
					case MULTI_STRING:
					{
						if (eDictType == eDynFxDictType_None)
						{
							UITextEntry *pEntry = ui_TextEntryCreate("", 0, 0);
							ui_TextEntrySetFinishedCallback(pEntry, edit_UpdateFXDrawFromWidget, pWindow);

							paramGroup->pParamWidget = UI_WIDGET(pEntry);
							paramGroup->type = edptString;
						}
						else
						{
							// Type was specified in FX file. Limit to a given dictionary.
							switch(eDictType)
							{
								case eDynFxDictType_Material:
								{
									UIDictionaryEntry *pEntry = ui_DictionaryEntryCreate("", MATERIAL_DICT, true, true);
									ui_DictionaryEntrySetFinishedCallback(pEntry, edit_UpdateFXDrawFromWidget, pWindow);
									ui_DictionaryEntrySetOpenCallback(pEntry, OpenMaterialInEditor, NULL);

									paramGroup->pParamWidget = UI_WIDGET(pEntry);
									paramGroup->type = edptString;
									paramGroup->pcDictName = MATERIAL_DICT;
								}

								xcase eDynFxDictType_Geometry:
								case eDynFxDictType_ClothCollisionInfo:
								case eDynFxDictType_ClothInfo:
								case eDynFxDictType_Texture:
								{
									// FIXME: All of these don't work because the dictionaries do not
									//   exist on the client by default.
									UITextEntry *pEntry = ui_TextEntryCreate("", 0, 0);
									ui_TextEntrySetFinishedCallback(pEntry, edit_UpdateFXDrawFromWidget, pWindow);

									paramGroup->pParamWidget = UI_WIDGET(pEntry);
									paramGroup->type = edptString;
								}
							}
						}

						if (pExistingParam)
						{
							// If parameter exists in param block from data make sure current value reflects that
							const char *str = MultiValGetString(&pExistingParam->mvVal, NULL);
							if (paramGroup->pcDictName)
							{
								ui_DictionaryEntrySetText((UIDictionaryEntry*)paramGroup->pParamWidget, str);
							}
							else
							{
								ui_TextEntrySetText((UITextEntry*)paramGroup->pParamWidget, str);
							}
						}
						else
						{
							// Parameter does not exist, make sure defaults are reflected in widget
							MultiVal *paramValue = dynFxInfoGetParamValue(pWindow->pButton->pFX->pcDynName, paramGroup->pcName);
							if (paramValue)
							{
								const char *str = MultiValGetString(paramValue, NULL);
								if (paramGroup->pcDictName)
								{
									ui_DictionaryEntrySetText((UIDictionaryEntry*)paramGroup->pParamWidget, str);
								}
								else
								{
									ui_TextEntrySetText((UITextEntry*)paramGroup->pParamWidget, str);
								}
								MultiValDestroy(paramValue);
							}
						}
					}
					xcase MULTI_FLOAT:
					{
						UISpinnerEntry *pEntry = ui_SpinnerEntryCreate(-999999, 999999, 0.1, 0, true);
						ui_SpinnerEntrySetCallback(pEntry, edit_UpdateFXDrawFromWidget, pWindow);

						paramGroup->pParamWidget = UI_WIDGET(pEntry);
						paramGroup->type = edptNumber;

						if (pExistingParam)
						{
							// If parameter exists in param block from data make sure current value reflects that
							F64 val = MultiValGetFloat(&pExistingParam->mvVal, NULL);
							ui_SpinnerEntrySetValue(pEntry, val);
						}
						else
						{
							// Parameter does not exist, make sure defaults are reflected in widget
							MultiVal *paramValue = dynFxInfoGetParamValue(pWindow->pButton->pFX->pcDynName, paramGroup->pcName);
							if (paramValue)
							{
								F64 val = MultiValGetFloat(paramValue, NULL);
								ui_SpinnerEntrySetValue(pEntry, val);
								MultiValDestroy(paramValue);
							}
						}
					}
					xcase MULTI_VEC3:
					{
						UIMultiSpinnerEntry *pEntry = ui_MultiSpinnerEntryCreate(-999999, 999999, 0.1, 0, 3, true);
						ui_MultiSpinnerEntrySetCallback(pEntry, edit_UpdateFXDrawFromWidget, pWindow);

						paramGroup->pParamWidget = UI_WIDGET(pEntry);
						paramGroup->type = edptVector;

						if (pExistingParam)
						{
							// If parameter exists in param block from data make sure current value reflects that
							Vec3 *pVec3 = MultiValGetVec3(&pExistingParam->mvVal, NULL);
							if (pVec3)
							{
								ui_MultiSpinnerEntrySetValue(pEntry, *pVec3, 3);
							}
						}
						else
						{
							// Parameter does not exist, make sure defaults are reflected in widget
							MultiVal *paramValue = dynFxInfoGetParamValue(pWindow->pButton->pFX->pcDynName, paramGroup->pcName);
							if (paramValue)
							{
								Vec3 *pVec3 = MultiValGetVec3(paramValue, NULL);
								if (pVec3)
								{
									ui_MultiSpinnerEntrySetValue(pEntry, *pVec3, 3);
								}
								MultiValDestroy(paramValue);
							}
						}
					}
					xcase MULTI_VEC4:
					{
						UIMultiSpinnerEntry *pEntry = ui_MultiSpinnerEntryCreate(-999999, 999999, 0.1, 0, 4, true);
						ui_MultiSpinnerEntrySetCallback(pEntry, edit_UpdateFXDrawFromWidget, pWindow);

						paramGroup->pParamWidget = UI_WIDGET(pEntry);
						paramGroup->type = edptVector4;

						if (pExistingParam)
						{
							// If parameter exists in param block from data make sure current value reflects that
							Vec4 *pVec4 = MultiValGetVec4(&pExistingParam->mvVal, NULL);
							if (pVec4)
							{
								ui_MultiSpinnerEntrySetValue(pEntry, *pVec4, 4);
							}
						}
						else
						{
							// Parameter does not exist, make sure defaults are reflected in widget
							MultiVal *paramValue = dynFxInfoGetParamValue(pWindow->pButton->pFX->pcDynName, paramGroup->pcName);
							if (paramValue)
							{
								Vec4 *pVec4 = MultiValGetVec4(paramValue, NULL);
								if (pVec4)
								{
									ui_MultiSpinnerEntrySetValue(pEntry, *pVec4, 4);
								}
								MultiValDestroy(paramValue);
							}
						}
					}
				}

				ui_BoxSizerAddWidget(paramBox, paramGroup->pParamWidget, 10, UIWidth, BUTTON_BORDER);
				ui_BoxSizerAddSizer(pWindow->pBoxSizer, UI_SIZER(paramBox), 1, UIWidth, BUTTON_BORDER);

				if (!pExistingParam)
				{
					ui_SetActive(paramGroup->pParamWidget, false);
				}
			}

			if (!pWindow->oldData->FXDataBlock)
			{
				pWindow->oldData->FXDataBlock = block;
			}
			else
			{
				pWindow->newData->FXDataBlock = block;
			}
		}
	}

	if (!pWindow->pSelectButton)
	{
		pWindow->pSelectButton = ui_ButtonCreate("Select", 0, 0, NULL, NULL);
		ui_ButtonSetCallback(pWindow->pSelectButton, FXWindowSelectButtonCallback, pWindow);
	}
	ui_BoxSizerAddWidget(endBox, &(pWindow->pSelectButton->widget), 0, UIWidth, BUTTON_BORDER);

	if (!pWindow->pCancelButton)
	{
		pWindow->pCancelButton = ui_ButtonCreate("Cancel", 0, 0, NULL, NULL);
		ui_ButtonSetCallback(pWindow->pCancelButton, FXWindowCancelButtonCallback, pWindow);
	}
	ui_BoxSizerAddWidget(endBox, &(pWindow->pCancelButton->widget), 0, UIWidth, BUTTON_BORDER);

	ui_BoxSizerAddSizer(pWindow->pBoxSizer, UI_SIZER(endBox), 1, UIRight, BUTTON_BORDER);
	ui_WidgetSetSizer(UI_WIDGET(pWindow), UI_SIZER(pWindow->pBoxSizer));

	if (pWindow->pButton->changedFunc)
	{
		pWindow->pButton->changedFunc(&(pWindow->widget), block, pWindow->pButton->changedData);
	}

	if (pWindow->pButton->changedFuncShort)
	{
		pWindow->pButton->changedFuncShort(pWindow->pButton->changedDataShort);
	}
}

static bool FXWindowCloseCallback(UIFXWindow *pWindow, UIFXButton *fxbutton)
{
	ui_WidgetQueueFree(UI_WIDGET(pWindow));
	return false;
}

void ui_FXWindowFreeInternal(UIFXWindow *pWindow)
{
	if (!pWindow->bSelectPressed)
	{
		if (pWindow->pButton->stopFunc)
		{
			pWindow->pButton->stopFunc(pWindow->pButton->stopData);
		}

		(*pWindow->pButton->pHue) = pWindow->oldData->FXDataHue;
		edit_FXChanged(pWindow, true);
	}

	if (pWindow->newData->FXDataBlock)
	{
		StructDestroy(parse_DynParamBlock, pWindow->newData->FXDataBlock);
	}

	if (pWindow->pBoxSizer)
	{
		ui_BoxSizerFree(pWindow->pBoxSizer);
	}

	if ((pWindow->oldData->FXDataBlock) && (pWindow->oldData->FXDataBlock != pWindow->newData->FXDataBlock))
	{
		StructDestroy(parse_DynParamBlock, pWindow->oldData->FXDataBlock);
	}

	if (pWindow->pHueField)
	{
		MEFieldSafeDestroy(&(pWindow->pHueField));
	}

	pWindow->pButton->activeWindow = NULL;
	pWindow->pButton = NULL;

	ui_WindowFreeInternal(UI_WINDOW(pWindow));
}

UIFXWindow *ui_FXWindowCreate(UIFXButton *fxbutton, F32 x, F32 y)
{
	UIFXWindow *pWindow = calloc(1, sizeof(UIFXWindow));

	if (!fxbutton)
	{
		//Throw an assert
	}
	pWindow->newData = StructCreate(parse_FxData);
	pWindow->oldData = StructCreate(parse_FxData);

	if (strlen(fxbutton->pFX->pcDynName) > 0)
	{
		ui_WindowInitializeEx(UI_WINDOW(pWindow), fxbutton->pFX->pcDynName, x, y, 400, 500 MEM_DBG_PARMS_INIT);
	}
	else
	{
		ui_WindowInitializeEx(UI_WINDOW(pWindow), "Select FX Settings", x, y, 400, 500 MEM_DBG_PARMS_INIT);
	}

	pWindow->pButton = fxbutton;
	pWindow->oldData->FXDataHue = pWindow->newData->FXDataHue = (*pWindow->pButton->pHue);
	pWindow->oldData->FXDataBlock = pWindow->newData->FXDataBlock = NULL;
	pWindow->bSelectPressed = false;

	//Instead of repeating code, refresh & creation are the same.
	edit_FXChanged(pWindow, false);

	fxbutton->activeWindow = pWindow;

	pWindow->widget.freeF = ui_FXWindowFreeInternal;
	ui_WindowSetCloseCallback(UI_WINDOW(pWindow), FXWindowCloseCallback, fxbutton);
	return pWindow;
}

void ui_FXButtonFreeInternal(UIFXButton *fxbutton)
{
	if (fxbutton->activeWindow)
	{
		ui_WindowClose(UI_WINDOW(fxbutton->activeWindow));
		fxbutton->activeWindow = NULL;
	}
	ui_ButtonFreeInternal(UI_BUTTON(fxbutton));
}

void ui_FXButtonDestroy(UIFXButton *fxbutton)
{
	ui_FXButtonFreeInternal(fxbutton);
}

void ui_FXButtonDraw(UIFXButton *fxbutton, UI_PARENT_ARGS)
{
	Color col;
	Vec3 hsv, rgb;
	UIStyleFont *pFont = ui_WidgetGetFont(UI_WIDGET(fxbutton));
	UI_GET_COORDINATES(fxbutton);

	hsv[0] = (*fxbutton->pHue);
	hsv[1] = 1;
	hsv[2] = 1;
	hsvToRgb(hsv, rgb);
	vec3ToColor(&col, rgb);

	CBoxClipTo(&pBox, &box);

	ui_DrawCapsule(&box, z, col, scale);
	ui_WidgetGroupDraw(&fxbutton->widget.children, UI_MY_VALUES);

	ui_StyleFontUse(pFont, false, UI_WIDGET(fxbutton)->state);
	gfxfont_PrintMaxWidth(x + w/2, y + h/2, z + 0.05, w, scale, scale, CENTER_XY, ui_WidgetGetText(UI_WIDGET(fxbutton)));
}

void ui_FXButtonClick(UIFXButton *fxbutton, UserData dummy)
{
	if (!fxbutton->activeWindow)
	{
		UIWidgetGroup *group = ui_WidgetGroupForDevice(NULL);
		UIFXWindow *pWindow = ui_FXWindowCreate(fxbutton, DEFAULT_WINDOW_X, DEFAULT_WINDOW_Y);
		fxbutton->activeWindow = pWindow;
		ui_WindowSetCloseCallback(UI_WINDOW(pWindow), FXWindowCloseCallback, fxbutton);
		ui_WindowShow(UI_WINDOW(pWindow));
	}
	else
	{
		ui_WindowShow(UI_WINDOW(fxbutton->activeWindow));
		ui_WidgetGroupSteal(fxbutton->activeWindow->widget.group, UI_WIDGET(fxbutton->activeWindow));
	}
}

void ui_FXButtonUpdate(UIFXButton *fxbutton, DynFxInfo *pFX, F32 *pHue, char **pcParams)
{
	if (fxbutton->pHue != pHue)
	{
		fxbutton->pHue = pHue;
	}
	if (fxbutton->pFX != pFX)
	{
		fxbutton->pFX = pFX;
		if (!(*fxbutton->pHue))
		{
			(*fxbutton->pHue) = pFX->fDefaultHue;
		}
	}
	if (fxbutton->pcParams != pcParams)
	{
		fxbutton->pcParams = pcParams;
	}
}

UIFXButton *ui_FXButtonCreate(F32 x, F32 y, DynFxInfo *pFX, F32 *pHue, char **pcParams)
{
	UIFXButton *fxbutton = (UIFXButton *)calloc(1, sizeof(UIFXButton));
	F32 fontHeight;
	ui_FXButtonUpdate(fxbutton, pFX, pHue, pcParams);
	ui_ButtonInitialize(&fxbutton->button, "Edit FX", x, y, ui_FXButtonClick, NULL MEM_DBG_PARMS_INIT);
	fontHeight = ui_StyleFontLineHeight(GET_REF(UI_GET_SKIN(fxbutton)->hNormal), 1.f);
	ui_WidgetSetDimensions(UI_WIDGET(fxbutton), 64, fontHeight + UI_STEP);
	ui_WidgetSetPosition(UI_WIDGET(fxbutton), x, y);
	fxbutton->widget.drawF = ui_FXButtonDraw;
	fxbutton->widget.freeF = ui_FXButtonFreeInternal;
	return fxbutton;
}

void ui_FXButtonSetChangedCallback(UIFXButton *fxbutton, UIFXChangeFunc changedFunc, UserData changedData)
{
	fxbutton->changedFunc = changedFunc;
	fxbutton->changedData = changedData;
}

void ui_FXButtonSetChangedCallbackShort(UIFXButton *fxbutton, UIFXStopFunc changedFunc, UserData changedData)
{
	fxbutton->changedFuncShort = changedFunc;
	fxbutton->changedDataShort = changedData;
}

void ui_FXButtonSetStopCallback(UIFXButton *fxbutton, UIFXStopFunc stopFunc, UserData stopData)
{
	fxbutton->stopFunc = stopFunc;
	fxbutton->stopData = stopData;
}

void ui_FXButtonSetSelectVars(UIFXButton *fxbutton, F32 *pSetHue, DynParamBlock *pSetBlock)
{
	fxbutton->pSetHue = pSetHue;
	fxbutton->pSetBlock = pSetBlock;
}

void ui_FXButtonSetSelectCallback1(UIFXButton *fxbutton, UIFXStopFunc selectFunc, UserData selectData)
{
	fxbutton->selectFunc1 = selectFunc;
	fxbutton->selectData1 = selectData;
}

void ui_FXButtonSetSelectCallback2(UIFXButton *fxbutton, UIFXStopFunc selectFunc, UserData selectData)
{
	fxbutton->selectFunc2 = selectFunc;
	fxbutton->selectData2 = selectData;
}

#include "AutoGen/UIFXButton_h_ast.c"
