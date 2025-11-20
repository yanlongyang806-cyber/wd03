/***************************************************************************



***************************************************************************/

#include "EString.h"
#include "Message.h"
#include "StringUtil.h"

#include "inputMouse.h"
#include "inputText.h"

#include "GfxClipper.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"
#include "GfxPrimitive.h"

#include "UIComboBox.h"
#include "UIFilteredList.h"
#include "UIScrollbar.h"
#include "UITextEntry.h"
#include "UISkin.h"
#include "ResourceInfo.h"
#include "UIList.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static void ui_ComboBoxSetPopupCallbacks(SA_PARAM_NN_VALID UIComboBox *cb, UIComboBoxPopupShowFunc cbPopupShow, UIComboBoxPopupUpdateFunc cbPopupUpdate, UIComboBoxPopupTickFunc cbPopupTick, UIComboBoxPopupFreeFunc cbPopupFree);

bool ui_ComboBoxInput(UIComboBox *cb, KeyInput *input)
{
	if (!ui_IsActive(UI_WIDGET(cb)))
		return false;
	if (input->type != KIT_EditKey)
		return false;
	switch (input->scancode)
	{
	case INP_RETURN:
	case INP_NUMPADENTER:
		ui_ComboBoxSetPopupOpen(cb, !cb->opened);
		break;
	case INP_UP:
		if (!cb->allowOutOfBoundsSelected)
		{
			// Ignore up unless we're not on top entry
			if (cb->iSelected > 0)
			{
				ui_ComboBoxSetSelectedAndCallback(cb, cb->iSelected - 1);
				cb->scrollToSelectedOnNextTick = true;
			}
		}
		break;
	case INP_DOWN:
		if (!cb->allowOutOfBoundsSelected)
		{
			// Ignore down unless not on last entry
			if (cb->model && (cb->iSelected < eaSize(cb->model)-1))
			{
				ui_ComboBoxSetSelectedAndCallback(cb, cb->iSelected + 1);
				cb->scrollToSelectedOnNextTick = true;
			}
		}
		break;
	case INP_LSTICK_UP:
	case INP_JOYPAD_UP:
		if (cb->opened)
		{
			// Ignore up unless we're not on top entry
			if (cb->iSelected > 0)
			{
				ui_ComboBoxSetSelectedAndCallback(cb, cb->iSelected - 1);
				cb->scrollToSelectedOnNextTick = true;
			}
		}
		else
			return false;
		break;
	case INP_LSTICK_DOWN:
	case INP_JOYPAD_DOWN:
		if (cb->opened)
		{
			// Ignore down unless not on last entry
			if (cb->model && (cb->iSelected < eaSize(cb->model)-1))
			{
				ui_ComboBoxSetSelectedAndCallback(cb, cb->iSelected + 1);
				cb->scrollToSelectedOnNextTick = true;
			}
		}
		else
			return false;
		break;
	case INP_AB:
		ui_ComboBoxSetPopupOpen(cb, !cb->opened);
		break;
	default:
		return false;
	}
	return true;
}

void ui_ComboBoxTick(UIComboBox *cb, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(cb);
	UISkin* skin = UI_GET_SKIN( cb );
	F32 popupHeight;
	UIDrawingDescription listDesc = { 0 };
	
	if (cb->model == NULL)
		return;

//	popupHeight = floorf((eaSize(cb->model) + (cb->showDefault ? 1 : 0)) * cb->rowHeight * scale);
	ui_ListOutsideFillDrawingDescriptionFromSkin( skin, &listDesc, true );
	popupHeight = floorf((eaSize(cb->model)) * cb->rowHeight * scale) + ui_DrawingDescriptionHeight( &listDesc ) * scale;

	if (cb->opened && !ui_IsActive(UI_WIDGET(cb)))
		ui_ComboBoxSetPopupOpen(cb, false);

	UI_TICK_EARLY(cb, false, true);

	if (mouseDownHit(MS_LEFT, &box))
	{
		ui_SetFocus(cb);
		ui_ComboBoxSetPopupOpen(cb, !cb->opened);
		inpHandled();
	}
	if (cb->closeOnNextTick
		|| (!ui_IsFocusedOrChildren(cb)
			&& (cb->pPopupFilteredList ? !ui_IsFocusedOrChildren(cb->pPopupFilteredList) : true)
			&& (cb->pPopupList ? !ui_IsFocusedOrChildren(cb->pPopupList) : true)))
	{
		cb->closeOnNextTick = false;
		ui_ComboBoxSetPopupOpen(cb, false);
	}

	if (cb->opened)
	{
		F32 popupX = x;
		F32 popupY = y + h + skin->iComboBoxPopupBottomOffsetY;
		if (popupY + popupHeight > g_ui_State.screenHeight)
		{
			popupHeight = g_ui_State.screenHeight - (y + h);
			if (popupHeight < cb->rowHeight * 3) 
			{
				popupHeight = floorf((eaSize(cb->model)) * cb->rowHeight * scale);
				if (y - popupHeight <= 0)
				{
					popupHeight = y - 1;
				}
				popupY = y - popupHeight + skin->iComboBoxPopupTopOffsetY;
			}
		}
		if (x + w > g_ui_State.screenWidth)
			popupX = g_ui_State.screenWidth - w;
		if ((popupX < 0) && (popupX + w >= 0)) 
			popupX = 0;
		if (cb->cbPopupTick)
			cb->cbPopupTick(cb, popupX, popupY, w, popupHeight, scale);
	}

	UI_TICK_LATE(cb);
}

void ui_ComboBoxFillDrawingDescription( UIComboBox* cb, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( cb );
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName;
		if(!ui_IsActive(UI_WIDGET(cb))) {
			descName = skin->astrComboBoxStyleDisabled;
		} else if (cb->opened) {
			descName = skin->astrComboBoxStyleOpened;		
		} else if (ui_IsHovering(UI_WIDGET(cb))) {
			descName = skin->astrComboBoxStyleHighlight;
		} else if( ui_IsFocused(UI_WIDGET(cb)) ) {
			descName = skin->astrComboBoxStyleFocused;
		} else {
			descName = skin->astrComboBoxStyle;
		}

		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->styleBorderNameUsingLegacyColor = "Default_Capsule_Filled";
	}
}

UIStyleFont* ui_ComboBoxGetFont( UIComboBox* cb )
{
	UISkin* skin = UI_GET_SKIN( cb );
	UIStyleFont* font = NULL;
	
	if (!ui_IsActive(UI_WIDGET( cb ))) {
		font = GET_REF( skin->hComboBoxFontDisabled );
	} else if(cb->opened) {
		font = GET_REF( skin->hComboBoxFontOpened );
	} else if (ui_IsHovering(UI_WIDGET( cb ))) {
		font = GET_REF( skin->hComboBoxFontHighlight );
	} else if( ui_IsFocused(UI_WIDGET( cb ))) {
		font = GET_REF( skin->hComboBoxFontFocused );
	} else {
		font = GET_REF( skin->hComboBoxFont );
	}

	if( !font ) {
		font = ui_WidgetGetFont( UI_WIDGET( cb ));
	}

	return font;
}

void ui_ComboBoxDraw(UIComboBox *cb, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(cb);
	AtlasTex *down = (g_ui_Tex.arrowDropDown);
	UIDrawingDescription desc = { 0 };
	Color c;
	ui_ComboBoxFillDrawingDescription( cb, &desc );

	UI_DRAW_EARLY(cb);

	if (!UI_GET_SKIN(cb))
		c = cb->widget.color[0];
	else if (!ui_IsActive(UI_WIDGET(cb)))
		c = UI_GET_SKIN(cb)->button[3];
	else if (cb->opened)
		c = UI_GET_SKIN(cb)->button[2];
	else if (ui_IsHovering(UI_WIDGET(cb)) || ui_IsFocused(cb))
	{
		if (ui_IsChanged(UI_WIDGET(cb)))
			c = ColorLerp(UI_GET_SKIN(cb)->button[1], UI_GET_SKIN(cb)->button[4], 0.5);
		else if (ui_IsInherited(UI_WIDGET(cb)))
			c = ColorLerp(UI_GET_SKIN(cb)->button[1], UI_GET_SKIN(cb)->button[5], 0.5);
		else
			c = UI_GET_SKIN(cb)->button[1];
	}
	else if (ui_IsChanged(UI_WIDGET(cb)))
		c = UI_GET_SKIN(cb)->button[4];
	else if (ui_IsInherited(UI_WIDGET(cb)))
		c = UI_GET_SKIN(cb)->button[5];
	else
		c = UI_GET_SKIN(cb)->button[0];

	ui_DrawingDescriptionDraw( &desc, &box, scale, z, 255, c, ColorBlack );
	display_sprite(down, floorf(x + w - (down->width + UI_HSTEP) * scale), floorf(y + h/2 - down->height * scale / 2), z + 0.1, scale, scale, RGBAFromColor(c));
	
	cb->drawF(cb, cb->drawData, cb->iSelected, x, y, z + 0.01, floorf(w - (down->width + UI_HSTEP) * scale), h, scale, true);

	UI_DRAW_LATE(cb);
}

UIComboBox *ui_ComboBoxCreate(F32 x, F32 y, F32 w, ParseTable *table, cUIModel model, const char *field)
{
	UIComboBox *cb = (UIComboBox *)calloc(1, sizeof(UIComboBox));
	ui_ComboBoxInitialize(cb, x, y, w, table, model, field);
	return cb;
}

UIComboBox *ui_ComboBoxCreateWithDictionary(F32 x, F32 y, F32 w, const void *dictHandleOrName, ParseTable *table, const char *field)
{
	UIComboBox *cb = (UIComboBox *)calloc(1, sizeof(UIComboBox));
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(dictHandleOrName);
	assertmsg(pStruct, "Cannot create combo box with a nonexistent dictionary");
	ui_ComboBoxInitialize(cb, x, y, w, table, &pStruct->ppReferents, field);
	return cb;
}

UIComboBox *ui_ComboBoxCreateWithGlobalDictionary(F32 x, F32 y, F32 w, const char *globalDictName, const char *field)
{
	UIComboBox *cb = (UIComboBox *)calloc(1, sizeof(UIComboBox));
	ResourceDictionaryInfo *pStruct = resDictGetInfo(globalDictName);
	assertmsg(pStruct, "Cannot create combo box with a nonexistent global object dictionary");
	ui_ComboBoxInitialize(cb, x, y, w, parse_ResourceInfo, &pStruct->ppInfos, field);
	return cb;
}

UIComboBox *ui_ComboBoxCreateWithEnum(F32 x, F32 y, F32 w, StaticDefineInt *enumTable, UIComboBoxEnumFunc selectedF, UserData selectedData)
{
	UIComboBox *cb = ui_ComboBoxCreate(x, y, w, NULL, NULL, NULL);
	ui_ComboBoxSetEnum(cb, enumTable, selectedF, selectedData);
	ui_ComboBoxCalculateOpenedWidth(cb);
	return cb;
}

void ui_ComboBoxInitialize(UIComboBox *cb, F32 x, F32 y, F32 w, ParseTable *table, cUIModel model, const char *field) 
{
	UIStyleFont *font = ui_ComboBoxGetFont( cb );
	F32 fontHeight;
	ui_WidgetInitialize(UI_WIDGET(cb), ui_ComboBoxTick, ui_ComboBoxDraw, ui_ComboBoxFreeInternal, ui_ComboBoxInput, ui_WidgetDummyFocusFunc);
	fontHeight = ui_StyleFontLineHeight(font, 1.f);
	ui_WidgetSetDimensions(UI_WIDGET(cb), w, fontHeight + UI_STEP);
	ui_WidgetSetPosition(UI_WIDGET(cb), x, y);
	ui_ComboBoxSetDrawFunc(cb, ui_ComboBoxDefaultDraw, (void *)field);
	ui_ComboBoxSetTextCallback(cb, ui_ComboBoxDefaultText, (void *)field);
	ui_ComboBoxSetPopupCallbacks(cb, ui_ComboBoxDefaultPopupShow, ui_ComboBoxDefaultPopupUpdate, ui_ComboBoxDefaultPopupTick, ui_ComboBoxDefaultPopupFree);
	ui_ComboBoxSetModel(cb, table, model);
	cb->defaultDisplay = strdup("");
	cb->widget.sb = ui_ScrollbarCreate(false, true);
	cb->openedWidth = -1;
	cb->rowHeight = fontHeight + UI_HSTEP;
	cb->iSelected = -1;
	cb->cbItemActivated = ui_ComboBoxDefaultRowCallback;
}

void ui_ComboBoxFreeInternal(UIComboBox *cb)
{
	SAFE_FREE(cb->defaultDisplay);
	if (cb->cbPopupFree)
		cb->cbPopupFree(cb);
	ui_ComboBoxSetSelectedCallback(cb, NULL, NULL);	// Frees the memory allocated for StaticDefineInt tables, if any.
	eaiDestroy(&cb->eaiSelected);
	ui_WidgetFreeInternal(UI_WIDGET(cb));
}

void ui_ComboBoxSetDrawFunc(UIComboBox *cb, UIComboBoxDrawFunc func, UserData drawData)
{
	cb->drawF = func;
	cb->drawData = drawData;
}

void ui_ComboBoxSetModel(UIComboBox *cb, ParseTable *table, cUIModel cModel)
{
	// ugly local variable is explicity cast to get around GCC compiler type checking
	// MS compiler was doing conversion from CUIModel to UIModel in function call
	UIModel model = (UIModel)cModel;

	const void *obj = NULL;
	cb->table = table;
	cb->model = model;
	if (cb->pPopupFilteredList)
	{
		ui_FilteredListSetModel(cb->pPopupFilteredList, table, model);
	}
	else if (cb->pPopupList)
	{
		ui_ListSetModel(cb->pPopupList, table, model);
	}
	ui_ComboBoxSetSelectedAndCallback(cb, -1);
}

void ui_ComboBoxSetModelNoCallback(UIComboBox *cb, ParseTable *table, cUIModel cModel)
{
	// ugly local variable is explicity cast to get around GCC compiler type checking
	// MS compiler was doing conversion from CUIModel to UIModel in function call
	UIModel model = (UIModel)cModel;

	const void *obj = NULL;
	cb->table = table;
	cb->model = model;
	if (cb->pPopupFilteredList)
	{
		ui_FilteredListSetModel(cb->pPopupFilteredList, table, model);
	}
	if (cb->pPopupList)
	{
		ui_ListSetModel(cb->pPopupList, table, model);
	}
	ui_ComboBoxSetSelected(cb, -1);
}

S32 ui_ComboBoxGetSelected(UIComboBox *cb)
{
	return cb->iSelected;
}

void *ui_ComboBoxGetSelectedObject(UIComboBox *cb)
{
	// Object brush UI needs this to never assert. --TomY
	if (cb->iSelected >= 0 && cb->iSelected < eaSize(cb->model))
	{
		return cb->model ? eaGet(cb->model, cb->iSelected) : NULL;
	}
	return NULL;
}

const S32 * ui_ComboBoxGetSelecteds(UIComboBox *cb)
{
	return cb->eaiSelected;
}

void ui_ComboBoxSetSelected(UIComboBox *cb, S32 i)
{
	cb->iSelected = i;

	if (!cb->allowOutOfBoundsSelected)
		cb->iSelected = CLAMP(cb->iSelected, -1, cb->model ? eaSize(cb->model) : -1);

	if (i < 0)
		eaiClear(&cb->eaiSelected);
	else
	{
		eaiSetSize(&cb->eaiSelected, 1);
		cb->eaiSelected[0] = cb->iSelected;
	}

	if (cb->cbPopupUpdate)
		cb->cbPopupUpdate(cb);
}

void ui_ComboBoxSetSelecteds(UIComboBox *cb, const S32 * const *i)
{
	devassertmsg(cb->bMultiSelect || eaiSize(i) <= 1, "Trying to select multiple item on a single-item combo box.");
	eaiCopy(&cb->eaiSelected, i);
	cb->iSelected = eaiSize(&cb->eaiSelected) ? cb->eaiSelected[0] : -1;
	if (cb->cbPopupUpdate)
		cb->cbPopupUpdate(cb);
}

void ui_ComboBoxSetSelectedsAndCallback(UIComboBox *cb, const S32 * const *i)
{
	ui_ComboBoxSetSelecteds(cb, i);
	if (cb->selectedF)
		cb->selectedF(cb, cb->selectedData);
}

static S32 CompareInts(const S32 *i, const S32 *j) { return *i - *j; }

void ui_ComboBoxToggleSelection(UIComboBox *cb, S32 iRow)
{
	if (!cb->allowOutOfBoundsSelected)
		iRow = CLAMP(iRow, -1, eaSize(cb->model));

	if (cb->iSelected == iRow)
		cb->iSelected = -1;

	if (eaiFindAndRemove(&cb->eaiSelected, iRow) < 0)
		eaiPush(&cb->eaiSelected, iRow);

	ea32QSort(cb->eaiSelected, CompareInts);

	if (cb->cbPopupUpdate)
		cb->cbPopupUpdate(cb);
}

void ui_ComboBoxToggleSelectionAndCallback(UIComboBox *cb, S32 iRow)
{
	ui_ComboBoxToggleSelection(cb, iRow);
	if (cb->selectedF)
		cb->selectedF(cb, cb->selectedData);
}

void ui_ComboBoxRowToString(UIComboBox *pCombo, S32 iRow, char **ppchOut)
{
	if (pCombo->cbText)
		pCombo->cbText(pCombo, iRow, false, pCombo->pTextData, ppchOut);
	else
		estrCopy2(ppchOut, pCombo->defaultDisplay ? pCombo->defaultDisplay : "(null)");
}

S32 ui_ComboBoxFindRowForString(UIComboBox *pCombo, const char *pSearchString)
{
	char *estrTemp = NULL;
	int i;

	for (i=0; i<eaSize(pCombo->model); i++)
	{
		ui_ComboBoxRowToString(pCombo, i, &estrTemp);

		if (stricmp(estrTemp, pSearchString) == 0)
			break;
	}

	estrDestroy(&estrTemp);

	if (i == eaSize(pCombo->model))
		return -1;
	return i;
}

void ui_ComboBoxGetSelectedAsString(UIComboBox *cb, char **ppchOut)
{
	ui_ComboBoxRowToString(cb, ui_ComboBoxGetSelected(cb), ppchOut);
}

void ui_ComboBoxGetSelectedsAsString(UIComboBox *cb, char **ppchOut)
{
	ui_ComboBoxGetSelectedsAsStringEx(cb, ppchOut, NULL);
}



void ui_ComboBoxGetSelectedsAsStringEx(UIComboBox *cb, char **ppchOut, const char* pchIndexSeparator)
{
	char *estrTemp = NULL;
	int i;

	if( !pchIndexSeparator) {
		pchIndexSeparator = " ";
	}
	
	estrStackCreate(&estrTemp);
	for(i=0; i<eaiSize(&cb->eaiSelected); ++i)
	{
		ui_ComboBoxRowToString(cb, cb->eaiSelected[i], &estrTemp);
		if (i != 0)
			estrAppend2(ppchOut, pchIndexSeparator);
		estrAppend2(ppchOut, estrTemp);
	}
	estrDestroy(&estrTemp);
}

void ui_ComboBoxSetSelectedsAsString(UIComboBox *cb, const char *pchValue)
{
	if (!cb->bMultiSelect)
	{
		S32 index = ui_ComboBoxFindRowForString(cb, pchValue);
		ui_ComboBoxSetSelected(cb, index);
	}
	else
	{
		char tempString[1024], *strings[64];
		S32 *iSelected = NULL;
		int count;
		int i;
		S32 index;

		if (!pchValue || !pchValue[0])
		{
			ui_ComboBoxSetSelected(cb, -1);
			return;
		}

		strcpy(tempString, pchValue);
		count = tokenize_line(tempString,strings, NULL);

		for (i = 0; i < count; i++)
		{
			index = ui_ComboBoxFindRowForString(cb, strings[i]);
			if (index != -1)
			{
				eaiPush(&iSelected, index);
			}
		}
		
		ui_ComboBoxSetSelecteds(cb, &iSelected);

		eaiDestroy(&iSelected);
	}

}

void ui_ComboBoxSetSelectedsAsStringAndCallback(UIComboBox *cb, const char *pchValue)
{
	ui_ComboBoxSetSelectedsAsString(cb, pchValue);
	if (cb->selectedF)
		cb->selectedF(cb, cb->selectedData);
}

void ui_ComboBoxSetSelectedAndCallback(UIComboBox *cb, int i)
{
	ui_ComboBoxSetSelected(cb, i);
	if (cb->selectedF)
		cb->selectedF(cb, cb->selectedData);
}

void ui_ComboBoxSetSelectedObject(UIComboBox *cb, const void *obj)
{
	if (cb->model)
		ui_ComboBoxSetSelected(cb, eaFind(cb->model, (void*)obj));
}

void ui_ComboBoxSetSelectedObjectAndCallback(UIComboBox *cb, const void *obj)
{
	if (cb->model)
		ui_ComboBoxSetSelectedAndCallback(cb, eaFind(cb->model, (void*)obj));
}

static void ComboBoxEnumInternal(UIComboBox *cb, ComboBoxEnumProxy *proxy)
{
	if (proxy->cbEnumSelected)
		proxy->cbEnumSelected(cb, eaiGet(&proxy->eaiValues, cb->iSelected), proxy->pEnumSelectedData);
}

int ui_ComboBoxEnumRowToVal(UIComboBox *cb, int row)
{
	ComboBoxEnumProxy *proxy = (ComboBoxEnumProxy *)cb->selectedData;
	devassertmsg(cb->selectedF == ComboBoxEnumInternal, "This is not an enumerated combo box.");
	if (row < 0 || row >= eaiSize(&proxy->eaiValues))
		return -1;
	else if (proxy && proxy->eaiValues)
		return proxy->eaiValues[row];
	else
		return -1;
}

int ui_ComboBoxGetSelectedEnum(UIComboBox *cb)
{
	ComboBoxEnumProxy *proxy = (ComboBoxEnumProxy *)cb->selectedData;
	devassertmsg(cb->selectedF == ComboBoxEnumInternal, "This is not an enumerated combo box.");
	if (cb->iSelected < 0 || cb->iSelected >= eaiSize(&proxy->eaiValues))
		return -1;
	else if (proxy && proxy->eaiValues)
		return proxy->eaiValues[cb->iSelected];
	else
		return -1;
}

UserData ui_ComboBoxGetSelectedData(UIComboBox *cb)
{
	if (cb->selectedF == ComboBoxEnumInternal && cb->selectedData)
		return ((ComboBoxEnumProxy*)(cb->selectedData))->pEnumSelectedData;
	else
		return cb->selectedData;
}

// Enums may have aliased keys in them which show up in sequence with the same value
// This function removes the duplicates from the proxy list
void ui_ComboBoxRemoveEnumDuplicates(UIComboBox *cb)
{
	ComboBoxEnumProxy *proxy = (ComboBoxEnumProxy *)cb->selectedData;
	int i;

	for(i=eaiSize(&proxy->eaiValues)-1; i>=1; --i) {
		if (proxy->eaiValues[i] == proxy->eaiValues[i-1]) {
			eaiRemove(&proxy->eaiValues, i);
			eaRemove(&proxy->eachNames, i);
		}
	}
}

void ui_ComboBoxSortEnum(UIComboBox *cb)
{
	ComboBoxEnumProxy *proxy = (ComboBoxEnumProxy *)cb->selectedData;
	int i,j;

	// Simple sort
	for(i=eaSize(&proxy->eachNames)-1; i>=1; --i) {
		for(j=i-1; j>=0; --j) {
			if (strcmp(proxy->eachNames[i],proxy->eachNames[j]) < 0) {
				eaSwap(&proxy->eachNames, i, j);
				eaiSwap(&proxy->eaiValues, i, j);
			}
		}
	}
}

static void UICBSetSelectedEnum(UIComboBox *cb, S32 **peaiValues, bool bAndCallback)
{
	ComboBoxEnumProxy *proxy = (ComboBoxEnumProxy *)cb->selectedData;
	static S32 *eaiRows = NULL;
	S32 p;
	devassertmsg(cb->selectedF == ComboBoxEnumInternal, "This is not an enumerated combo box.");

	eaiClear(&eaiRows);
	for (p = 0; p < eaiSize(&proxy->eaiValues); p++)
	{
		if (eaiFind(peaiValues, proxy->eaiValues[p]) >= 0)
		{
			eaiPushUnique(&eaiRows, p);
			if (!cb->bMultiSelect)
				break;
		}
	}

	if (bAndCallback)
		ui_ComboBoxSetSelectedsAndCallback(cb, &eaiRows);
	else
		ui_ComboBoxSetSelecteds(cb, &eaiRows);
}

void ui_ComboBoxSetSelectedEnum(UIComboBox *cb, S32 iValue)
{
	static S32 *eaiRows = NULL;
	eaiSetSize(&eaiRows, 1);
	eaiRows[0] = iValue;
	UICBSetSelectedEnum(cb, &eaiRows, false);	
}

void ui_ComboBoxSetSelectedEnumAndCallback(UIComboBox *cb, S32 iValue)
{
	static S32 *eaiRows = NULL;
	eaiSetSize(&eaiRows, 1);
	eaiRows[0] = iValue;
	UICBSetSelectedEnum(cb, &eaiRows, true);
}

void ui_ComboBoxSetSelectedEnums(UIComboBox *cb, S32 **peaiValues)
{
	UICBSetSelectedEnum(cb, peaiValues, false);
}

void ui_ComboBoxSetSelectedEnumsAndCallback(UIComboBox *cb, S32 **peaiValues)
{
	UICBSetSelectedEnum(cb, peaiValues, true);
}

void ui_ComboBoxSetEnum(UIComboBox *cb, StaticDefineInt *enumTable, UIComboBoxEnumFunc selectedF, UserData selectedData)
{
	ComboBoxEnumProxy *proxy = calloc(1, sizeof(ComboBoxEnumProxy));
	proxy->cbEnumSelected = selectedF;
	proxy->pEnumSelectedData = selectedData;

	DefineFillAllKeysAndValues(enumTable, &proxy->eachNames, &proxy->eaiValues);

	// Set the model before the selected callback, so that we don't call
	// the callback when the model is set.
	ui_ComboBoxSetModel(cb, NULL, &proxy->eachNames);
	ui_ComboBoxSetSelectedCallback(cb, ComboBoxEnumInternal, proxy);
	ui_ComboBoxSetDrawFunc(cb, ui_ComboBoxDefaultDraw, NULL);
}

void ui_ComboBoxEnumInsertValue(SA_PARAM_NN_VALID UIComboBox *cb, char *pchLabel, S32 iValue)
{
	if (cb->selectedF == ComboBoxEnumInternal)
	{
		ComboBoxEnumProxy *proxy = (ComboBoxEnumProxy*)cb->selectedData;
		int i, n = eaiSize(&proxy->eaiValues);
		for (i = 0; i < n; i++)
		{
			if (proxy->eaiValues[i] >= iValue) 
				break;
		}
		eaInsert(&proxy->eachNames, pchLabel, i);
		eaiInsert(&proxy->eaiValues, iValue, i);
	}
}

void ui_ComboBoxEnumAddValue(SA_PARAM_NN_VALID UIComboBox *cb, const char *pchLabel, S32 iValue)
{
	if (cb->selectedF == ComboBoxEnumInternal)
	{
		ComboBoxEnumProxy *proxy = (ComboBoxEnumProxy*)cb->selectedData;
		eaPush(&proxy->eachNames, pchLabel);
		eaiPush(&proxy->eaiValues, iValue);
	}
}

void ui_ComboBoxEnumRemoveValueInt(SA_PARAM_NN_VALID UIComboBox *cb, S32 iEnumValue)
{
	if (cb->selectedF == ComboBoxEnumInternal)
	{
		ComboBoxEnumProxy *proxy = (ComboBoxEnumProxy*)cb->selectedData;
		int i, n = eaiSize(&proxy->eaiValues);
		for (i = n-1; i >= 0; i--)
		{
			if (proxy->eaiValues[i] == iEnumValue)
			{
				eaRemove(&proxy->eachNames, i);
				eaiRemove(&proxy->eaiValues, i);
			}
		}
	}
}

void ui_ComboBoxEnumRemoveValueString(SA_PARAM_NN_VALID UIComboBox *cb, const char *pchValue)
{
	if (cb->selectedF == ComboBoxEnumInternal)
	{
		ComboBoxEnumProxy *proxy = (ComboBoxEnumProxy*)cb->selectedData;
		int i, n = eaiSize(&proxy->eaiValues);
		for (i = n-1; i >= 0; i--)
		{
			if (proxy->eachNames[i] && !stricmp(proxy->eachNames[i],pchValue))
			{
				eaRemove(&proxy->eachNames, i);
				eaiRemove(&proxy->eaiValues, i);
			}
		}
	}
}

void ui_ComboBoxEnumRemoveAllValues(SA_PARAM_NN_VALID UIComboBox *cb)
{
	if (cb->selectedF == ComboBoxEnumInternal)
	{
		ComboBoxEnumProxy *proxy = (ComboBoxEnumProxy*)cb->selectedData;
		eaClear(&proxy->eachNames);
		eaiClear(&proxy->eaiValues);
	}
}

void ui_ComboBoxGetSelectedEnums(UIComboBox *cb, S32 **peaiValues)
{
	S32 i;
	ComboBoxEnumProxy *proxy = (ComboBoxEnumProxy *)cb->selectedData;
	for (i = 0; i < eaiSize(&cb->eaiSelected); i++)
		eaiPushUnique(peaiValues, eaiGet(&proxy->eaiValues, cb->eaiSelected[i]));
}

void ui_ComboBoxSetSelectedEnumCallback(UIComboBox *cb, UIComboBoxEnumFunc selectedF, UserData selectedData)
{
	ComboBoxEnumProxy *proxy = (ComboBoxEnumProxy *)cb->selectedData;
	proxy->cbEnumSelected = selectedF;
	proxy->pEnumSelectedData = selectedData;
}

void ui_ComboBoxSetSelectedCallback(UIComboBox *cb, UIActivationFunc selectedF, UserData selectedData)
{
	if (cb->selectedF == ComboBoxEnumInternal)
	{
		ComboBoxEnumProxy *proxy = cb->selectedData;
		eaDestroy(&proxy->eachNames);
		eaiDestroy(&proxy->eaiValues);
		SAFE_FREE(cb->selectedData);
	}
	cb->selectedF = selectedF;
	cb->selectedData = selectedData;
}

void ui_ComboBoxSetDefaultDisplayString(UIComboBox *cb, const char* dispStr)
{
	SAFE_FREE(cb->defaultDisplay);
	cb->defaultDisplay = strdup(dispStr ? dispStr : "");
//	cb->showDefault = display;
}

void ui_ComboBoxDefaultRowCallback(UIComboBox *cb, S32 selected, UserData dummy)
{
	if (cb->bMultiSelect)
		ui_ComboBoxToggleSelectionAndCallback(cb, selected);
	else
		ui_ComboBoxSetSelectedAndCallback(cb, selected);
}

void ui_ComboBoxSetHoverCallback(UIComboBox *cb, UIComboBoxHoverFunc cbRowHover, UserData pHoverData)
{
	cb->cbHover = cbRowHover;
	cb->pHoverData = pHoverData;
}

void ui_ComboBoxSetPopupOpenEx(SA_PARAM_NN_VALID UIComboBox *cb, bool opened, bool focus)
{
	if (opened == cb->opened)
		return;
	cb->opened = opened;
	cb->widget.bConsumesEscape = opened;
	cb->cbPopupShow(cb, opened, focus);
}

void ui_ComboBoxSetPopupCallbacks(UIComboBox *cb, UIComboBoxPopupShowFunc cbPopupShow, UIComboBoxPopupUpdateFunc cbPopupUpdate, UIComboBoxPopupTickFunc cbPopupTick, UIComboBoxPopupFreeFunc cbPopupFree)
{
	// Free previous popup
	if (cb->cbPopupFree)
		cb->cbPopupFree(cb);

	cb->cbPopupShow = cbPopupShow;
	cb->cbPopupUpdate = cbPopupUpdate;
	cb->cbPopupTick = cbPopupTick;
	cb->cbPopupFree = cbPopupFree;
}

void ui_ComboBoxHoverProxy(UIList *pList, UIListColumn *pColumn, S32 iRow, UIComboBox *cb)
{
	if (cb->cbHover)
	{
		cb->cbHover(cb, iRow, cb->pHoverData);
	}
}

// Callback given to default list for popup to get the text for a row
static void ui_ComboBoxGetListText(UIList *pList, UIListColumn *pColumn, S32 iRow, UIComboBox *cb, char **estrOutput)
{
	if (cb->model && (*cb->model) && (*cb->model)[iRow])
	{
		if (cb->cbText)
		{
			cb->cbText(cb, iRow, false, cb->pTextData, estrOutput);
		}
		else
			estrConcatf(estrOutput,"%s",(char*)((*cb->model)[iRow]));
	}
}

// Callback given to default list for popup to be notified of selection changes
static void ui_ComboBoxListSelectCallback(UIList *pList, UIComboBox *cb)
{
	if( !mouseDown( MS_RIGHT ))
	{
		if(!cb->bMultiSelect)
			ui_ComboBoxSetPopupOpen(cb, false);
		ui_SetFocus(cb);
	}
	inpResetMouseTime(); // Make sure click doesn't happen after this
	if (cb->bMultiSelect)
		ui_ComboBoxSetSelectedsAndCallback(cb, ui_ListGetSelectedRows(pList));
	else if (!cb->bIsFlagCombo)
		ui_ComboBoxSetSelectedAndCallback(cb, ui_ListGetSelectedRow(pList));
	else
	{
		ComboBoxEnumProxy *proxy = cb->pItemActivatedData;
 		const int * const *peaiSelected = ui_ListGetSelectedRows(pList);
		int i, iSelected = 0;
		for(i=eaiSize(peaiSelected)-1; i>=0; --i)
			iSelected |= proxy->eaiValues[(*peaiSelected)[i]];
		ui_ComboBoxSetSelectedAndCallback(cb, iSelected);
	}
}

// Callback on popup's tick so we can dismiss the window on unhandled mouse input
static void ui_ComboBoxListTick(UIList *pList, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pList);

	// First pass to list
	ui_ListTick(pList, pX, pY, pW, pH, pScale);

	// If unhandled mouse action outside the combo parent, then hide
	if (  (UI_WIDGET(pList)->group && eaGet(UI_WIDGET(pList)->group, 0) != UI_WIDGET(pList))
		  || (mouseDown(MS_LEFT) && !mouseDownHit(MS_LEFT, &box)))
	{
		((UIComboBox*)pList->pSelectedData)->closeOnNextTick = true;
		ui_ComboBoxSetPopupOpen((UIComboBox*)pList->pSelectedData, false);
	}
}

void ui_ComboBoxDefaultPopupShow(UIComboBox *cb, bool bShow, bool bFocus)
{
	UIList *pList = cb->pPopupList;

	if (bShow) 
	{
		if (!pList) 
		{
			pList = ui_ListCreate(cb->table, cb->model, cb->rowHeight);
			ui_ListSetCellContextCallback(pList, ui_ListCellClickedDefault, NULL);
			cb->pPopupList = pList;
			pList->widget.sb->scrollX = false;
			pList->widget.sb->alwaysScrollY = false;
			pList->fHeaderHeight = 0;  // Hide the headers
			pList->bMultiSelect = cb->bMultiSelect || cb->bIsFlagCombo;
			pList->bDrawSelection = cb->drawSelected;
			pList->bDrawHover = true;
			pList->bIsComboBoxDropdown = true;
			if (cb->table && cb->drawData)
				ui_ListAppendColumn(pList, ui_ListColumnCreate(cb->bUseMessage ? UIListPTMessage : UIListPTName, "DontShowMe", (intptr_t)cb->drawData, NULL));
			else
				ui_ListAppendColumn(pList, ui_ListColumnCreate(UIListTextCallback, "DontShowMe", (intptr_t)ui_ComboBoxGetListText, cb));
			pList->eaColumns[0]->fWidth = 1; // Auto-fill of column will make it the right width
			{
				UISkin* skin = UI_GET_SKIN(cb);
				if (skin && !skin->bUseStyleBorders && !skin->bUseTextureAssemblies)
				{
					pList->backgroundColor = (UI_GET_SKIN(cb)->background[0]);
					pList->bUseBackgroundColor = true;
				}
			}
			if (cb->bMultiSelect || cb->bIsFlagCombo || cb->bShowCheckboxes)
			{
				pList->bToggleSelect = true;
				pList->eaColumns[0]->bShowCheckBox = true;
			}
			pList->widget.tickF = ui_ComboBoxListTick;
			ui_ListSetSelectedCallback(pList, ui_ComboBoxListSelectCallback, cb);
			if (cb->cbHover)
				ui_ListSetHoverCallback(pList, ui_ComboBoxHoverProxy, cb);
		}

		if (UI_GET_SKIN(cb))
			ui_WidgetSkin(UI_WIDGET(pList), UI_GET_SKIN(cb));

		// Make sure we know the right width each time it opens
		ui_ComboBoxCalculateOpenedWidth(cb);

		// Add the widget to the device
		UI_WIDGET(pList)->priority = UI_HIGHEST_PRIORITY;
		ui_TopWidgetAddToDevice(UI_WIDGET(pList), NULL);

		// Scroll to selected row on next tick
		cb->scrollToSelectedOnNextTick = true;
		ui_ComboBoxDefaultPopupUpdate(cb);
	} 
	else 
	{
		// unattach from top to hide it
		ui_WidgetRemoveFromGroup(UI_WIDGET(cb->pPopupList));
	}
}

void ui_ComboBoxDefaultPopupUpdate(UIComboBox *cb)
{
	// Update the selection to track the combo box change
	UIList *pList = cb->pPopupList;
	if (pList)
	{
		if (cb->bMultiSelect)
			ui_ListSetSelectedRows(pList, &cb->eaiSelected);
		else if (!cb->bIsFlagCombo)
			ui_ListSetSelectedRow(pList, cb->iSelected);
		else
		{
			ComboBoxEnumProxy *proxy = cb->pItemActivatedData;
			int *eaiSelected = NULL;
			int i;
			for(i=eaiSize(&proxy->eaiValues)-1; i>=0; --i)
			{
				if (proxy->eaiValues[i] & cb->iSelected) {
					eaiPush(&eaiSelected, i);
				}
			}
			ui_ListSetSelectedRows(pList, &eaiSelected);
			eaiDestroy(&eaiSelected);
		}
	}
}

void ui_ComboBoxDefaultPopupTick(UIComboBox *cb, UI_PARENT_ARGS)
{
	UIList *pList = cb->pPopupList;
	if (pList)
	{
		F32 popupHeight = pH;
		F32 popupWidth = MAX(cb->openedWidth, pW/pScale);
		F32 popupX = pX + (pW/pScale)/2.0 - popupWidth/2.0;
		if (pY + popupHeight > g_ui_State.screenHeight)
			popupHeight = g_ui_State.screenHeight - pY;
		if (popupX + popupWidth*pScale > g_ui_State.screenWidth)
			popupX = g_ui_State.screenWidth - popupWidth*pScale;
		if ((popupX < 0) && (popupX + popupWidth >= 0))
			popupX = 0;
		ui_WidgetSetPosition(UI_WIDGET(pList), popupX/g_ui_State.scale, pY/g_ui_State.scale);
		ui_WidgetSetWidth(UI_WIDGET(pList), popupWidth);
		ui_WidgetSetHeight(UI_WIDGET(pList), popupHeight/pScale);
		UI_WIDGET(pList)->scale = pScale / g_ui_State.scale;
		if (cb->scrollToSelectedOnNextTick)
		{
			cb->scrollToSelectedOnNextTick = false;
			if (!cb->bIsFlagCombo)
			{
				if (cb->bMultiSelect && eaiSize(&cb->eaiSelected))
					ui_ListScrollToRow(pList, cb->eaiSelected[0]);
				else if (!cb->bMultiSelect)
					ui_ListScrollToRow(pList, cb->iSelected);
				pList->bScrollToCenter = true;
				ui_ListScrollToPath(pList, pList->widget.x, pList->widget.y, pList->widget.width, pList->widget.height, pScale);
			}
		}
	}
}

void ui_ComboBoxDefaultPopupFree(UIComboBox *cb)
{
	if (cb->pPopupList)
	{
		ui_ComboBoxDefaultPopupShow(cb, false, false);
		ui_WidgetQueueFree(UI_WIDGET(cb->pPopupList));
		cb->pPopupList = NULL;
	}
}

UIComboBox *ui_FlagComboBoxCreate(StaticDefineInt *pDefines)
{
	int i;
	ComboBoxEnumProxy *proxy = calloc(1, sizeof(ComboBoxEnumProxy));
	UIComboBox *cb = ui_ComboBoxCreate(0, 0, 0, NULL, &proxy->eachNames, NULL);
	DefineFillAllKeysAndValues(pDefines, &proxy->eachNames, &proxy->eaiValues);

	for (i = eaSize(&proxy->eachNames) - 1; i >= 0; i--)
	{
		// Remove non-flag flag values, so we can deprecate them by setting them to 0
		if (proxy->eaiValues[i] <= 0)
		{
			eaiRemove(&proxy->eaiValues, i);
			eaRemove(&proxy->eachNames, i);
		}
	}

	ui_ComboBoxSetDrawFunc(cb, ui_FlagComboBoxDraw, proxy);
	cb->cbItemActivated = ui_FlagComboBoxRowCallback;
	cb->pItemActivatedData = proxy;
	cb->allowOutOfBoundsSelected = true;
	cb->iSelected = 0;
	cb->bIsFlagCombo = true;
	cb->widget.freeF = ui_FlagComboBoxFree;
	ui_ComboBoxCalculateOpenedWidth(cb);
	devassertmsg(ea32Size(&proxy->eaiValues) <= 32, "You've got too many flags, this won't work");
	return cb;
}

void ui_FlagComboBoxFree(UIComboBox *cb)
{
	ComboBoxEnumProxy *proxy = cb->drawData;
	if (proxy)
	{
		eaDestroy(&proxy->eachNames);
		ea32Destroy(&proxy->eaiValues);
	}
	SAFE_FREE(cb->drawData);
	ui_ComboBoxFreeInternal(cb);
}

void ui_FlagComboBoxRowCallback(UIFlagComboBox *cb, S32 selected, ComboBoxEnumProxy *proxy)
{
	ui_ComboBoxSetSelectedAndCallback(cb, cb->iSelected ^ proxy->eaiValues[selected]);
}

void ui_FlagComboBoxDraw(UIComboBox *cb, ComboBoxEnumProxy *proxy, S32 row, F32 x, F32 y, F32 z, F32 w, F32 h, F32 scale, bool inBox)
{
	CBox box = {x, y, x + w, y + h};
	UIStyleFont *pFont = ui_ComboBoxGetFont( cb );

	if (!inBox)
		return;

	clipperPushRestrict(&box);

	ui_StyleFontUse(pFont, false, UI_WIDGET(cb)->state);

	if (!cb->iSelected)
		gfxfont_Printf(x + UI_HSTEP_SC, y + h / 2, z + 0.1, scale, scale, CENTER_Y, "%s", cb->defaultDisplay);
	else
	{
		char *pchDisplay = NULL;
		S32 i;
		estrStackCreate(&pchDisplay);
		for (i = 0; i < ea32Size(&proxy->eaiValues); i++)
		{
			S32 iValue = proxy->eaiValues[i];
			if (cb->iSelected & iValue)
			{
				if (pchDisplay[0])
					estrConcatf(&pchDisplay, ", ");
				estrAppend2(&pchDisplay, proxy->eachNames[i]);
			}
		}
		gfxfont_Printf(x + UI_HSTEP_SC, y + h / 2, z + 0.1, scale, scale, CENTER_Y, "%s", pchDisplay);
		estrDestroy(&pchDisplay);
	}
	clipperPop();
}

// Enums may have aliased keys in them which show up in sequence with the same value
// This function removes the duplicates from the proxy list
void ui_FlagComboBoxRemoveDuplicates(UIComboBox *cb)
{
	ComboBoxEnumProxy *proxy = (ComboBoxEnumProxy *)cb->drawData;
	int i;

	for(i=eaiSize(&proxy->eaiValues)-1; i>=1; --i) {
		if (proxy->eaiValues[i] == proxy->eaiValues[i-1]) {
			eaiRemove(&proxy->eaiValues, i);
			eaRemove(&proxy->eachNames, i);
		}
	}
}

void ui_FlagComboBoxSort(UIComboBox *cb)
{
	ComboBoxEnumProxy *proxy = (ComboBoxEnumProxy *)cb->drawData;
	int i,j;

	// Simple sort
	for(i=eaSize(&proxy->eachNames)-1; i>=1; --i) {
		for(j=i-1; j>=0; --j) {
			if (strcmp(proxy->eachNames[i],proxy->eachNames[j]) < 0) {
				eaSwap(&proxy->eachNames, i, j);
				eaiSwap(&proxy->eaiValues, i, j);
			}
		}
	}
}

void ui_ComboBoxCalculateOpenedWidth(SA_PARAM_NN_VALID UIComboBox *cb)
{
	UIStyleFont *pFont = ui_ListItemGetFontFromSkinAndWidget( UI_GET_SKIN( cb ), UI_WIDGET( cb ), false, false );
	char *pchToken = NULL;
	S32 i;
	F32 fCheckBoxWidth = 0.0;

	cb->openedWidth = -1;
	if (!cb->model)
		return;

	if (cb->bMultiSelect || cb->bIsFlagCombo)
	{
		fCheckBoxWidth = atlasLoadTexture( "eui_tickybox_checked_8x8" )->width + UI_HSTEP;
	}

	estrStackCreate(&pchToken);
	for (i = 0; i < eaSize(cb->model); i++)
	{
		F32 fTokenWidth = 0.f;
		ui_ComboBoxRowToString(cb, i, &pchToken);
		fTokenWidth = ui_StyleFontWidth(pFont, 1.f, pchToken) + UI_HSTEP + ui_ScrollbarWidth(UI_WIDGET(cb)->sb) + fCheckBoxWidth;
		MAX1(cb->openedWidth, fTokenWidth);
	}
	estrDestroy(&pchToken);
}

void ui_ComboBoxSetMultiSelect(UIComboBox *cb, bool bMultiSelect)
{
	cb->bMultiSelect = bMultiSelect;
}

void ui_ComboBoxDefaultDraw(UIComboBox *pCombo, UserData dummy, S32 iRow, F32 fX, F32 fY, F32 fZ, F32 fW, F32 fH, F32 fScale, bool bInBox)
{
	AtlasTex *pWhite = (g_ui_Tex.white);
	CBox box = {fX, fY, fX + fW, fY + fH};
	char *pchText = NULL;

	estrStackCreate(&pchText);

	if (pCombo->cbText)
		pCombo->cbText(pCombo, iRow, bInBox, pCombo->pTextData, &pchText);
	else
		estrCopy2(&pchText, pCombo->defaultDisplay ? pCombo->defaultDisplay : "(null)");

	clipperPushRestrict(&box);

	{
		UIStyleFont *pFont = ui_ComboBoxGetFont( pCombo );
		F32 fCenterY = fY + fH / 2.f;
		fZ += 0.2f;
		ui_StyleFontUse(pFont, false, UI_WIDGET(pCombo)->state);
		if( !nullStr( pchText )) {
			gfxfont_Printf(fX + UI_HSTEP * fScale, fCenterY, fZ, fScale, fScale, CENTER_Y, "%s", pchText);
		} else {
			const char* defaultText = ui_WidgetGetText(UI_WIDGET(pCombo));
			if( defaultText ) {
				gfxfont_Printf(fX + UI_HSTEP * fScale, fCenterY, fZ, fScale, fScale, CENTER_Y, "%s", defaultText );
			}
		}
	}
	clipperPop();

	estrDestroy(&pchText);
}

void ui_ComboBoxSetTextCallback(UIComboBox *pCombo, UIComboBoxTextFunc cbText, UserData pTextData)
{
	pCombo->cbText = cbText;
	pCombo->pTextData = pTextData;
}

void ui_ComboBoxDefaultText(UIComboBox *pCombo, S32 iRow, bool bInBox, const char *pchField, char **ppchOutput)
{
	S32 i;

	estrClear(ppchOutput);

	if (bInBox && pCombo->bMultiSelect && eaiSize(&pCombo->eaiSelected) > 0)
	{
		char *pchValue = NULL;
		estrStackCreate(&pchValue);
		ui_ComboBoxDefaultText(pCombo, pCombo->eaiSelected[0], false, pchField, ppchOutput);
		for (i = 1; i < eaiSize(&pCombo->eaiSelected); i++)
		{
			ui_ComboBoxDefaultText(pCombo, pCombo->eaiSelected[i], false, pchField, &pchValue);
			estrConcatf(ppchOutput, ", %s", pchValue);
		}
		estrDestroy(&pchValue);
	}
	else if (pCombo->model && iRow >= 0 && iRow < eaSize(pCombo->model))
	{
		if (pCombo->table && pchField)
		{
			FORALL_PARSETABLE(pCombo->table, i)
			{
				if (!stricmp(pCombo->table[i].name, pchField))
				{
					void *pStruct = eaGet(pCombo->model, iRow);
					if (pStruct)
					{
						if (!pCombo->bUseMessage)
						{
							if (TokenWriteText(pCombo->table, i, pStruct, ppchOutput, true))
							{
								if(pCombo->bStringAsMessageKey)
								{
									const char* str = TranslateMessageKey(*ppchOutput);

									if(!str)
									{
										estrConcatf( ppchOutput, " [UNTRANSLATED!]" );
									}
									else
									{
										estrCopy2(ppchOutput, str);
									}
								}
								
								break;
							}
						}
						else
						{
							ReferenceHandle *pHandle = (ReferenceHandle*)(((char*)pStruct) + pCombo->table[i].storeoffset);
							Message *pMsg = RefSystem_IsHandleActive(pHandle) ? RefSystem_ReferentFromHandle(pHandle) : NULL;
							const char *pchText = pMsg ? TranslateMessagePtr(pMsg) : NULL;
							if (pchText)
							{
								estrPrintf(ppchOutput, "%s", pchText);
								break;
							}
						}
					}
				}
			}
		}
		else
		{
			estrCopy2(ppchOutput, eaGet(pCombo->model, iRow));
			
			if(pCombo->bStringAsMessageKey)
			{
				const char* str = TranslateMessageKey(*ppchOutput);

				if(!str)
				{
					estrConcatf( ppchOutput, " [UNTRANSLATED!]" );
				}
				else
				{
					estrCopy2(ppchOutput, str);
				}
			}
		}
	}

	if (nullStr(*ppchOutput))
		estrCopy2(ppchOutput, pCombo->defaultDisplay ? pCombo->defaultDisplay : "(null)");
}

UIFilteredComboBox *ui_FilteredComboBoxCreate(F32 x, F32 y, F32 w, ParseTable *table, cUIModel model, const char *field)
{
	UIComboBox *cb = (UIComboBox *)calloc(1, sizeof(UIComboBox));
	ui_ComboBoxInitialize(cb, x, y, w, table, model, field);
	ui_ComboBoxSetPopupCallbacks(cb, ui_FilteredComboBoxPopupShow, ui_FilteredComboBoxPopupUpdate, ui_FilteredComboBoxPopupTick, ui_FilteredComboBoxPopupFree);
	return cb;
}

UIFilteredComboBox *ui_FilteredComboBoxCreateWithDictionary(F32 x, F32 y, F32 w, const void *dictHandleOrName, ParseTable *table, const char *field)
{
	UIComboBox *cb = (UIComboBox *)calloc(1, sizeof(UIComboBox));
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(dictHandleOrName);
	assertmsg(pStruct, "Cannot create combo box with a nonexistent dictionary");
	ui_ComboBoxInitialize(cb, x, y, w, table, &pStruct->ppReferents, field);
	ui_ComboBoxSetPopupCallbacks(cb, ui_FilteredComboBoxPopupShow, ui_FilteredComboBoxPopupUpdate, ui_FilteredComboBoxPopupTick, ui_FilteredComboBoxPopupFree);
	return cb;
}

UIFilteredComboBox *ui_FilteredComboBoxCreateWithGlobalDictionary(F32 x, F32 y, F32 w, const char *globalDictName, const char *field)
{
	UIComboBox *cb = (UIComboBox *)calloc(1, sizeof(UIComboBox));
	ResourceDictionaryInfo *pStruct = resDictGetInfo(globalDictName);
	assertmsg(pStruct, "Cannot create combo box with a nonexistent global object dictionary");
	ui_ComboBoxInitialize(cb, x, y, w, parse_ResourceInfo, &pStruct->ppInfos, field);
	ui_ComboBoxSetPopupCallbacks(cb, ui_FilteredComboBoxPopupShow, ui_FilteredComboBoxPopupUpdate, ui_FilteredComboBoxPopupTick, ui_FilteredComboBoxPopupFree);
	return cb;
}

UIFilteredComboBox *ui_FilteredComboBoxCreateWithEnum(F32 x, F32 y, F32 w, StaticDefineInt *enumTable, UIComboBoxEnumFunc selectedF, UserData selectedData)
{
	UIComboBox *cb = ui_ComboBoxCreate(x, y, w, NULL, NULL, NULL);
	ui_ComboBoxSetPopupCallbacks(cb, ui_FilteredComboBoxPopupShow, ui_FilteredComboBoxPopupUpdate, ui_FilteredComboBoxPopupTick, ui_FilteredComboBoxPopupFree);
	ui_ComboBoxSetEnum(cb, enumTable, selectedF, selectedData);
	ui_ComboBoxCalculateOpenedWidth(cb);
	return cb;
}

void ui_ComboBoxFilteredHoverProxy(UIFilteredList *pFList, UIListColumn *pColumn, S32 iRow, UIComboBox *cb)
{
	if (cb->cbHover)
	{
		cb->cbHover(cb, iRow, cb->pHoverData);
	}
}

// Callback given to filtered list for popup to be notified of selection changes
static void ui_FilteredComboBoxListSelectCallback(UIFilteredList *pFList, UIComboBox *cb)
{
	ui_ComboBoxSetPopupOpen(cb, false);
	ui_SetFocus(cb);
	inpResetMouseTime(); // Make sure click doesn't happen after this
	if (cb->bMultiSelect)
		ui_ComboBoxSetSelectedsAndCallback(cb, ui_FilteredListGetSelectedRows(pFList));
	else if (!cb->bIsFlagCombo)
		ui_ComboBoxSetSelectedAndCallback(cb, ui_FilteredListGetSelectedRow(pFList));
}

// Callback on popup's tick so we can dismiss the window on unhandled mouse input
static void ui_FilteredComboBoxListTick(UIFilteredList *pFList, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pFList);

	// First pass to list
	ui_FilteredListTick(pFList, pX, pY, pW, pH, pScale);

	// If unhandled mouse action outside the combo parent, then hide
	if (mouseDown(MS_LEFT) && !mouseDownHit(MS_LEFT, &box)) 
	{
		((UIComboBox*)pFList->pSelectedData)->closeOnNextTick = true;
		ui_ComboBoxSetPopupOpen((UIComboBox*)pFList->pSelectedData, false);
	}
}

void ui_FilteredComboBoxPopupShow(UIComboBox *cb, bool bShow, bool bFocus)
{
	UIFilteredList *pFList = cb->pPopupFilteredList;

	if (bShow) 
	{
		if (!pFList) 
		{
			pFList = ui_FilteredListCreate(NULL, cb->table ? (cb->bUseMessage ? UIListPTMessage : UIListPTName) : UIListTextCallback, cb->table, cb->model, (intptr_t)cb->drawData, cb->rowHeight);
			ui_FilteredListSetMultiselect(pFList, cb->bMultiSelect || cb->bIsFlagCombo);
			cb->pPopupFilteredList = pFList;
			pFList->pList->widget.sb->scrollX = false;
			pFList->pList->bDrawSelection = true;
			pFList->pList->bDrawHover = true;
			pFList->pList->eaColumns[0]->fWidth = 1; // Auto-fill of column will make it the right width
			pFList->bUseBackgroundColor = true;
			pFList->bDrawBorder = true;
			pFList->bDontAutoSort = cb->bDontSortList;
			assertmsg(!cb->bIsFlagCombo, "Filtered combo does not support flags mode");
			pFList->widget.tickF = ui_FilteredComboBoxListTick;
			ui_FilteredListSetSelectedCallback(pFList, ui_FilteredComboBoxListSelectCallback, cb);
			ui_FilteredListSetFilterCallback(pFList,cb->cbFilterFunc,cb->pFilterData);
			if (cb->cbHover)
				ui_FilteredListSetHoverCallback(pFList, ui_ComboBoxFilteredHoverProxy, cb);
		}

		// Make sure we know the right width each time it opens
		ui_FilteredListRefresh(pFList);
		ui_ComboBoxCalculateOpenedWidth(cb);

		// Add the widget to the device
		UI_WIDGET(pFList)->priority = UI_HIGHEST_PRIORITY;
		ui_TopWidgetAddToDevice(UI_WIDGET(pFList), NULL);

		// Scroll to selected row on next tick
		cb->scrollToSelectedOnNextTick = true;
		ui_FilteredComboBoxPopupUpdate(cb);

		// Put the cursor inside the entry for typing
		if(bFocus)
			ui_SetFocus(pFList->pEntry);
	} 
	else 
	{
		// unattach from top to hide it
		ui_WidgetRemoveFromGroup(UI_WIDGET(cb->pPopupFilteredList));
	}
}

void ui_FilteredComboBoxPopupUpdate(UIComboBox *cb)
{
	// Update the selection to track the combo box change
	UIFilteredList *pFList = cb->pPopupFilteredList;
	if (pFList)
	{
		if (cb->bMultiSelect)
			ui_FilteredListSetSelectedRows(pFList, &cb->eaiSelected);
		else if (!cb->bIsFlagCombo)
			ui_FilteredListSetSelectedRow(pFList, cb->iSelected);
	}
}

void ui_FilteredComboBoxPopupTick(UIComboBox *cb, UI_PARENT_ARGS)
{
	UIFilteredList *pFList = cb->pPopupFilteredList;
	if (pFList)
	{
		F32 popupHeight = pH + (UI_WIDGET(pFList->pEntry)->height+1)*pScale;
		F32 popupWidth = MAX(cb->openedWidth, pW/pScale);
		F32 popupX = pX + (pW/pScale)/2.0 - popupWidth/2.0;
		if (pY + popupHeight > g_ui_State.screenHeight)
			popupHeight = g_ui_State.screenHeight - pY;
		if (popupX + popupWidth*pScale > g_ui_State.screenWidth)
			popupX = g_ui_State.screenWidth - popupWidth*pScale;
		if ((popupX < 0) && (popupX + popupWidth >= 0))
			popupX = 0;
		ui_WidgetSetPosition(UI_WIDGET(pFList), popupX/g_ui_State.scale, pY/g_ui_State.scale);
		ui_WidgetSetWidth(UI_WIDGET(pFList), popupWidth);
		ui_WidgetSetHeight(UI_WIDGET(pFList), popupHeight/pScale);
		UI_WIDGET(pFList)->scale = pScale / g_ui_State.scale;
		if (cb->scrollToSelectedOnNextTick)
		{
			cb->scrollToSelectedOnNextTick = false;
			if (!cb->bIsFlagCombo)
			{
				if (cb->bMultiSelect && eaiSize(&cb->eaiSelected))
					ui_FilteredListScrollToRow(pFList, cb->eaiSelected[0]);
				else if (!cb->bMultiSelect)
					ui_FilteredListScrollToRow(pFList, cb->iSelected);
				pFList->pList->bScrollToCenter = true;
				ui_FilteredListScrollToPath(pFList, pFList->widget.x, pFList->widget.y, pFList->widget.width, pFList->widget.height, pScale);
			}
		}
	}
}

void ui_FilteredComboBoxPopupFree(UIComboBox *cb)
{
	if (cb->pPopupFilteredList)
	{
		ui_ComboBoxDefaultPopupShow(cb, false, false);
		ui_WidgetQueueFree(UI_WIDGET(cb->pPopupFilteredList));
		cb->pPopupFilteredList = NULL;
	}
}

void ui_FilteredComboBoxSetFilterCallback(SA_PARAM_NN_VALID UIComboBox *cb, UIFilterListFunc cbFilterFunc, UserData pUserData)
{
	UIFilteredList *pFList = cb->pPopupFilteredList;

	if(pFList)
	{
		ui_FilteredListSetFilterCallback(pFList,cbFilterFunc,pUserData);
	}

	cb->cbFilterFunc = cbFilterFunc;
	cb->pFilterData = pUserData;
}

