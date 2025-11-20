/***************************************************************************



***************************************************************************/

#include "estring.h"
#include "Message.h"
#include "StringUtil.h"
#include "TextBuffer.h"

#include "ScratchStack.h"

#include "input.h"
#include "inputMouse.h"
#include "inputText.h"
#include "inputKeyBind.h"

#include "GfxClipper.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"
#include "GfxPrimitive.h"
#include "GfxSprite.h"

#include "UIButton.h"
#include "UIComboBox.h"
#include "UIFilteredList.h"
#include "UIInternal.h"
#include "UIList.h"
#include "UIMenu.h"
#include "UIPane.h"
#include "UISMFView.h"
#include "UIScrollbar.h"
#include "UISkin.h"
#include "UITextArea.h"
#include "UITextEntry.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

typedef void (*UITextEntryMenuFunc)(UITextEntry* entry);

static UIStyleFont* ui_TextEntryGetFont(UITextEntry* entry);
static void ui_TextEntrySMFUpdate(UITextEntry* entry, UserData ignored);
static void ui_TextEntryCallFunc( void* ignored, UITextEntryMenuFunc fn );

static UIMenu* s_textEntryMenu;
static UIMenu* s_textEntryReadOnlyMenu;
static UIMenuItem* s_textEntryItemCut;
static UIMenuItem* s_textEntryItemCopy;
static UIMenuItem* s_textEntryItemPaste;
static UIMenuItem* s_textEntryItemSelectAll;
static UITextEntry* s_textEntryMenuEntry;

int ui_TextEntryFindComboSelectedRow(UITextEntry *entry, const char *text)
{
	int i = -1;

	if (entry->cb->table)
	{
		U32 col = 0;
		unsigned char *scratchText = ScratchAlloc(TOKEN_BUFFER_LENGTH);
		FORALL_PARSETABLE(entry->cb->table, col)
		{
			if (entry->cb->table[col].name && !stricmp(entry->cb->table[col].name, (const unsigned char *)entry->cb->drawData))
			{
				for (i = eaSize(entry->cb->model)-1; i >=0 ; --i)
				{
					if (!entry->cb->bUseMessage)
					{
						if (TokenToSimpleString(entry->cb->table, col, (*entry->cb->model)[i], scratchText, (S32)ScratchSize(scratchText), WRITETEXTFLAG_PRETTYPRINT))
							if (!stricmp(scratchText, text))
								break;
					}
					else
					{
						ReferenceHandle *pHandle = (ReferenceHandle*)(((char*)(*entry->cb->model)[i]) + entry->cb->table[col].storeoffset);
						Message *pMsg = RefSystem_IsHandleActive(pHandle) ? RefSystem_ReferentFromHandle(pHandle) : NULL;
						const char *transText = pMsg ? TranslateMessagePtr(pMsg) : NULL;
						if (transText && !stricmp(transText, text))
							break;
					}
				}
				break;
			}
		}
		ScratchFree(scratchText);
	}
	else if (entry->cb->cbText)
	{
		char* estrScratch = NULL;
		for (i = eaSize(entry->cb->model) - 1; i >= 0; --i) {
			estrClear( &estrScratch );
			entry->cb->cbText(entry->cb, i, false, entry->cb->pTextData, &estrScratch);

			if (stricmp(estrScratch, text) == 0) {
				break;
			}
		}
	}
	else
		// MJF: And the JoeW said: This is not necesarily safe, just
		// because there is no table doesn't mean that the model is an
		// array of strings.
		i = StringArrayFind((const unsigned char **)(*entry->cb->model), text);
	return i;
}


void ui_TextEntryUpdateComboSelectedRows(UITextEntry *entry)
{
	if (!entry->cb->bMultiSelect) 
	{
		int row = ui_TextEntryFindComboSelectedRow(entry, ui_EditableGetText(UI_EDITABLE(entry)));
		ui_ComboBoxSetSelected(entry->cb, row);
	}
	else
	{
		// For multi-select combos do space delimited values
		char *estrTemp = NULL;
		const char *pStart = ui_EditableGetText(UI_EDITABLE(entry));
		const char *pEnd = pStart;
		const char *pchIndexSeparator = (entry->pchIndexSeparator ? entry->pchIndexSeparator : " ");
		size_t indexSeparatorLength = strlen(pchIndexSeparator);
		int *eaiRows = NULL;
		int row;
		while (*pEnd != '\0')
		{
			if (strStartsWith(pEnd, pchIndexSeparator))
			{
				if (pEnd != pStart)
				{
					estrClear(&estrTemp);
					estrConcat(&estrTemp, pStart, pEnd-pStart);
					row = ui_TextEntryFindComboSelectedRow(entry, estrTemp);
					if (row >= 0) 
						eaiPush(&eaiRows, row);
				}
				pEnd+=indexSeparatorLength;
				pStart = pEnd;
			}
			else
				pEnd++;

		}
		if (pEnd != pStart)
		{
			estrClear(&estrTemp);
			estrConcat(&estrTemp, pStart, pEnd-pStart);
			row = ui_TextEntryFindComboSelectedRow(entry, estrTemp);
			if (row >= 0) 
				eaiPush(&eaiRows, row);
		}
		ui_ComboBoxSetSelecteds(entry->cb, &eaiRows);
		eaiDestroy(&eaiRows);
	}
}


void ui_TextEntryComboValidate(UITextEntry *entry)
{
	if (!entry->cb) 
		return;
	// Use text to select proper row.  If there is no proper row, then change to empty string.
	ui_TextEntryUpdateComboSelectedRows(entry);
	if (ui_ComboBoxGetSelected(entry->cb) < 0)
		ui_TextEntrySetTextAndCallback(entry, "");
}

bool ui_TextEntrySetText(UITextEntry *entry, const unsigned char *text)
{
	return ui_EditableSetText(UI_EDITABLE(entry), text);
}

bool ui_TextEntrySetTextAndCallback(UITextEntry *entry, const unsigned char *text)
{
	return ui_EditableSetTextAndCallback(UI_EDITABLE(entry), text);
}

bool ui_TextEntrySetTextIfChanged(UITextEntry *entry, const unsigned char *text)
{
	bool result = false;

	if(strcmp(ui_TextEntryGetText(entry), text) != 0)
		result =  ui_TextEntrySetText(entry, text);

	return result;
}


const unsigned char *ui_TextEntryGetText(UITextEntry *entry)
{
	return ui_EditableGetText(UI_EDITABLE(entry));
}

void ui_TextEntrySetChangedCallback(UITextEntry *entry, UIActivationFunc changedF, UserData changedData)
{
	ui_EditableSetChangedCallback(UI_EDITABLE(entry), changedF, changedData);
}

void ui_TextEntrySMFUpdate(UITextEntry* entry, UserData ignored)
{
	if (entry->smf && entry->smfOpened) {
		ErrorfPushCallback( NULL, NULL );
		ui_SMFViewSetText(entry->smf, ui_EditableGetText(UI_EDITABLE(entry)), NULL);
		ErrorfPopCallback();
	}
}

void ui_TextEntrySetFinishedCallback(UITextEntry *entry, UIActivationFunc finishedF, UserData finishedData)
{
	entry->finishedF = finishedF;
	entry->finishedData = finishedData;
}

void ui_TextEntrySetValidateCallback(UITextEntry *entry, UITextValidateFunc validateF, UserData validateData)
{
	ui_EditableSetValidationCallback(UI_EDITABLE(entry), validateF, validateData);
}

void ui_TextEntrySetCursorPosition(UITextEntry *entry, U32 cursorPos)
{
	if (entry->areaOpened)
		ui_TextAreaSetCursorPosition(entry->area, cursorPos);
	else
		ui_EditableSetCursorPosition(UI_EDITABLE(entry), cursorPos);
}

U32 ui_TextEntryGetCursorPosition(UITextEntry *entry)
{
	if (entry->areaOpened)
		return ui_TextAreaGetCursorPosition(entry->area);
	else
		return ui_EditableGetCursorPosition(UI_EDITABLE(entry));
}

void ui_TextEntrySetSelectOnFocus(UITextEntry *entry, bool select)
{
	entry->selectOnFocus = select;
}

U32 ui_TextEntryInsertTextAt(UITextEntry *entry, U32 offset, const unsigned char *text)
{
	return ui_EditableInsertText(UI_EDITABLE(entry), offset, text);
}

void ui_TextEntryDoAutocomplete(UITextEntry *entry)
{
	S32 i=0;
	unsigned char *text = NULL;

	if (!entry->cb || !entry->cb->model)
		devassertmsg(false, "Autocomplete is on but there's no combo box attached.");

	if ((entry->cb->drawData && !entry->cb->table) || (entry->cb->table && !entry->cb->drawData))
		devassertmsg(false, "Combo box field/table user data mismatch.");

	if (entry->cb->table)
	{
		U32 col = 0;
		text = ScratchAlloc(TOKEN_BUFFER_LENGTH);
		FORALL_PARSETABLE(entry->cb->table, col)
		{
			if (entry->cb->table[col].name && !stricmp(entry->cb->table[col].name, (const unsigned char *)entry->cb->drawData))
			{
				for (i = 0; i < eaSize(entry->cb->model); i++)
				{
					if (TokenToSimpleString(entry->cb->table, col, (*entry->cb->model)[i], text, (S32)ScratchSize(text), WRITETEXTFLAG_PRETTYPRINT))
						if (strStartsWith(text, ui_EditableGetText(UI_EDITABLE(entry))))
							break;
				}
				break;
			}
		}
	}
	else
		i = StringArrayNFind((const unsigned char **)(*entry->cb->model), ui_EditableGetText(UI_EDITABLE(entry)));

	if (i < eaSize(entry->cb->model) && i != -1)
	{
		U32 oldLength = UTF8GetLength(ui_EditableGetText(UI_EDITABLE(entry)));
		if (entry->cb->table)
			ui_TextEntrySetTextAndCallback(entry, text);
		else
			ui_TextEntrySetTextAndCallback(entry, (*entry->cb->model)[i]);
		ui_TextEntryUpdateComboSelectedRows(entry);
		ui_ComboBoxSetPopupOpenEx(entry->cb, true, false);
		entry->cb->scrollToSelectedOnNextTick = true;
		ui_EditableSetSelection(UI_EDITABLE(entry), oldLength, UTF8GetLength(ui_EditableGetText(UI_EDITABLE(entry))));
		ui_EditableSetCursorPosition(UI_EDITABLE(entry), UTF8GetLength(ui_EditableGetText(UI_EDITABLE(entry))));
		entry->selectionIsAutoComplete = true;
	}
	if (text)
		ScratchFree(text);
}

bool ui_TextEntryDeleteTextAt(UITextEntry *entry, U32 offset, U32 length)
{
	return ui_EditableDeleteText(UI_EDITABLE(entry), offset, length);
}

bool ui_TextEntryDeleteSelection(UITextEntry *entry)
{
	return ui_EditableDeleteSelection(UI_EDITABLE(entry));
}

void ui_TextEntryPaste(UITextEntry *entry)
{
	ui_EditablePaste(UI_EDITABLE(entry));
}

void ui_TextEntryCopy(UITextEntry *entry)
{
	ui_EditableCopy(UI_EDITABLE(entry));
}

void ui_TextEntryCut(UITextEntry *entry)
{
	ui_EditableCopy(UI_EDITABLE(entry));
	ui_EditableDeleteSelection(UI_EDITABLE(entry));
}

void ui_TextEntryUndo(UITextEntry *entry)
{
	ui_EditableUndo(UI_EDITABLE(entry));
}

void ui_TextEntryRedo(UITextEntry *entry)
{
	ui_EditableRedo(UI_EDITABLE(entry));
}

void ui_TextEntrySelectAll(UITextEntry *entry)
{
	ui_EditableSelectAll(UI_EDITABLE(entry));
}

bool ui_TextEntryInput(UITextEntry *entry, KeyInput *input)
{
	bool changed = false;

	PERFINFO_AUTO_START_FUNC();

	if (KIT_EditKey == input->type)
	{
		UI_EDITABLE(entry)->time = 0;
		switch (input->scancode)
		{
		case INP_BACKSPACE:
			if (ui_IsActive(UI_WIDGET(entry)))
			{
				if (TextBuffer_GetSelection(UI_EDITABLE(entry)->pBuffer, NULL, NULL, NULL))
					ui_EditableDeleteSelection(UI_EDITABLE(entry));
				else if (input->attrib & KIA_CONTROL)
					ui_EditableDeletePreviousWord(UI_EDITABLE(entry));
				else
					ui_EditableBackspace(UI_EDITABLE(entry));
				changed = true;
			}
		xcase INP_DECIMAL:
			// Ignore NUMPAD if NUMLOCK is on
			if (input->attrib & KIA_NUMLOCK)
				break;
		case INP_DELETE:
			if (ui_IsActive(UI_WIDGET(entry)))
			{
				if (TextBuffer_GetSelection(UI_EDITABLE(entry)->pBuffer, NULL, NULL, NULL))
					ui_EditableDeleteSelection(UI_EDITABLE(entry));
				else if (input->attrib & KIA_CONTROL)
					ui_EditableDeleteNextWord(UI_EDITABLE(entry));
				else
					ui_EditableDelete(UI_EDITABLE(entry));
				changed = true;
			}
		xcase INP_NUMPAD4:
			// Ignore NUMPAD if NUMLOCK is on
			if (input->attrib & KIA_NUMLOCK)
				break;
		case INP_LEFT:
			if (input->attrib & KIA_SHIFT)
			{
				if (input->attrib & KIA_CONTROL)
					TextBuffer_SelectionPreviousWord(UI_EDITABLE(entry)->pBuffer);
				else
					TextBuffer_SelectionPrevious(UI_EDITABLE(entry)->pBuffer);
				entry->selectionIsAutoComplete = true;
			}
			else
			{
				if (input->attrib & KIA_CONTROL)
					TextBuffer_CursorToPreviousWord(UI_EDITABLE(entry)->pBuffer);
				else
					TextBuffer_MoveCursor(UI_EDITABLE(entry)->pBuffer, -1);
			}
		xcase INP_NUMPAD6:
			// Ignore NUMPAD if NUMLOCK is on
			if (input->attrib & KIA_NUMLOCK)
				break;
		case INP_RIGHT:
			if (input->attrib & KIA_SHIFT)
			{
				if (input->attrib & KIA_CONTROL)
					TextBuffer_SelectionNextWord(UI_EDITABLE(entry)->pBuffer);
				else
					TextBuffer_SelectionNext(UI_EDITABLE(entry)->pBuffer);
				entry->selectionIsAutoComplete = true;
			}
			else
			{
				if (input->attrib & KIA_CONTROL)
					TextBuffer_CursorToNextWord(UI_EDITABLE(entry)->pBuffer);
				else
					TextBuffer_MoveCursor(UI_EDITABLE(entry)->pBuffer, +1);
			}
		xcase INP_NUMPAD7:
			// Ignore NUMPAD if NUMLOCK is on
			if (input->attrib & KIA_NUMLOCK)
				break;
		case INP_HOME:
			{
				if(input->attrib & KIA_SHIFT)
				{
					TextBuffer_SelectionToStart(UI_EDITABLE(entry)->pBuffer);
					entry->selectionIsAutoComplete = true;
				}
				else
					TextBuffer_SetCursor(UI_EDITABLE(entry)->pBuffer, 0);
			}
		xcase INP_NUMPAD1:
			// Ignore NUMPAD if NUMLOCK is on
			if (input->attrib & KIA_NUMLOCK)
				break;
		case INP_END:
			{
				if(input->attrib & KIA_SHIFT)
				{
					TextBuffer_SelectionToEnd(UI_EDITABLE(entry)->pBuffer);
					entry->selectionIsAutoComplete = true;
				}
				else
					TextBuffer_CursorToEnd(UI_EDITABLE(entry)->pBuffer);
			}
		xcase INP_NUMPAD8:
			// Ignore NUMPAD if NUMLOCK is on
			if (input->attrib & KIA_NUMLOCK)
				break;
		case INP_UP:
			{
				if (entry->upKeyF)
					entry->upKeyF(entry, entry->upKeyData);
			}
		xcase INP_NUMPAD2:
			// Ignore NUMPAD if NUMLOCK is on
			if (input->attrib & KIA_NUMLOCK)
				break;
		case INP_DOWN:
			{
				if (entry->downKeyF)
					entry->downKeyF(entry, entry->downKeyData);
			}
		xcase INP_TAB:
			{
				if (entry->cb) 
					entry->cb->closeOnNextTick = true;
				PERFINFO_AUTO_STOP();
				return false; // Act like didn't deal with tab.  Intercept was merely to close popup
			}
		xcase INP_RETURN:
		case INP_NUMPADENTER:
			if (ui_IsActive(UI_WIDGET(entry)))
			{
				ui_EditableSetSelection(UI_EDITABLE(entry), 0, 0);
				if (entry->enterF)
					entry->enterF(entry, entry->enterData);
				if (entry->selectionIsAutoComplete && entry->cb)
					ui_ComboBoxSetPopupOpen(entry->cb, false);
			}
		xcase INP_V:
			if (ui_IsActive(UI_WIDGET(entry)) && input->attrib & KIA_CONTROL)
				ui_TextEntryPaste(entry);
		xcase INP_C:
			if (input->attrib & KIA_CONTROL)
				ui_TextEntryCopy(entry);
		xcase INP_X:
			if (ui_IsActive(UI_WIDGET(entry)) && input->attrib & KIA_CONTROL)
				ui_TextEntryCut(entry);
		xcase INP_Z:
			if (ui_IsActive(UI_WIDGET(entry)) && input->attrib & KIA_CONTROL)
				ui_TextEntryUndo(entry);
		xcase INP_Y:
			if (ui_IsActive(UI_WIDGET(entry)) && input->attrib & KIA_CONTROL)
				ui_TextEntryRedo(entry);
		xcase INP_A:
			if (input->attrib & KIA_CONTROL)
				ui_TextEntrySelectAll(entry);
		xdefault:
			// [COR-15337] Typing 'E' or 'R' in a text field in an editor occasionally causing the editor to close.
			if(!ui_EditableInputDefault(UI_EDITABLE(entry), input))
			{
				PERFINFO_AUTO_STOP();
				return false;
			}
		}
	}
	else if (ui_IsActive(UI_WIDGET(entry)) && ui_IsActive(UI_WIDGET(entry)) && KIT_Text == input->type && iswprint(input->character))
	{
		ui_EditableInsertCodepoint(UI_EDITABLE(entry), input->character);
		if (entry->autoComplete && TextBuffer_GetCursor(UI_EDITABLE(entry)->pBuffer) == UTF8GetLength(TextBuffer_GetText(UI_EDITABLE(entry)->pBuffer)))
			ui_TextEntryDoAutocomplete(entry);
		changed = true;
	}
	else
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	if (!ui_IsActive(UI_WIDGET(entry)))
		entry->selectionIsAutoComplete = false;
	if (!entry->selectionIsAutoComplete && entry->cb)
		ui_ComboBoxSetPopupOpen(entry->cb, false);
	if (!entry->selectionIsAutoComplete && entry->area)
		ui_TextEntrySetPopupAreaOpen(entry, false);

	if (changed)
	{
		if (!entry->selectionIsAutoComplete && entry->cb)
			ui_EditableSetSelection(UI_EDITABLE(entry), 0, 0);
		entry->selectionIsAutoComplete = false;
	}

	PERFINFO_AUTO_STOP();

	return true;
}

U32 ui_TextEntryPixelToCursor(UITextEntry *entry, F32 x, F32 scale)
{
	UIStyleFont *font = ui_TextEntryGetFont(entry);
	const char *text = ui_TextEntryGetText(entry);
	U32 i;
	U32 length = (U32)strlen(text); // yes, strlen. we count bytes.
	F32 textWidth;

	ui_StyleFontUse(font, false, UI_WIDGET(entry)->state);

	textWidth = ui_StyleFontWidth(font, scale, text);

	for (i = 0; i < length; i += UTF8GetCodepointLength(text + i))
	{
		F32 preCursorWidth, postCursorWidth;
		int drawCursorX;
		postCursorWidth = ui_StyleFontWidth(font, scale, text + i);
		preCursorWidth = textWidth - postCursorWidth;
		drawCursorX = round(preCursorWidth - entry->lastDrawnOffset);

		if (drawCursorX >= x)
			break;
	}
	return UTF8PointerToCodepointIndex(text, text + i);
}

void ui_TextEntryCursorToMouseWordSelect(UITextEntry *entry, F32 x, F32 scale)
{
	const char *text = ui_TextEntryGetText(entry);
	U32 pos = ui_TextEntryPixelToCursor(entry, x, scale);
	U32 previous = UTF8CodepointOfPreviousWord(text, pos);
	U32 next = UTF8CodepointOfNextWord(text, pos);
	ui_EditableSetSelection(UI_EDITABLE(entry), previous, next - previous);
}

void ui_TextEntryCursorToMouse(UITextEntry *entry, F32 x, F32 scale)
{
	U32 pos = ui_TextEntryPixelToCursor(entry, x, scale);

	if(inpLevelPeek(INP_SHIFT))
		ui_EditableSelectToPos(UI_EDITABLE(entry), pos);
	else
		ui_EditableSetSelection(UI_EDITABLE(entry), 0, 0);
	ui_EditableSetCursorPosition(UI_EDITABLE(entry), pos);
}

static bool ui_TextEntryReadOnlyValidate( UIWidget* ignored1, unsigned char** ignored2, unsigned char** ignored3, UserData ignored4 )
{
	return false;
}

static void ui_TextEntryFillDrawingDescription( UITextEntry* entry, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( entry );
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName;
		if (ui_IsFocused(UI_WIDGET(entry))) {
			if (!ui_IsActive(UI_WIDGET(entry))) {
				descName = skin->astrTextEntryStyle;
			} else {
				descName = skin->astrTextEntryStyleHighlight;
			}
		} else if (!ui_IsActive(UI_WIDGET(entry))) {
			descName = skin->astrTextEntryStyleDisabled;
		} else {
			descName = skin->astrTextEntryStyle;
		}

		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->textureNameUsingLegacyColor = "white";
		desc->overlayOutlineUsingLegacyColor2 = true;
	}
}

void ui_TextEntryTick(UITextEntry *entry, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(entry);
	float realW = w;
	AtlasTex *arrow = (g_ui_Tex.arrowDropDown);
	AtlasTex *plus = (g_ui_Tex.plus);
	UIDrawingDescription drawDesc = { 0 };
	bool mouseClickHitRight;
	
	ui_TextEntryFillDrawingDescription( entry, &drawDesc );

	if( entry->readOnly ) {
		ui_EditableSetValidationCallback(UI_EDITABLE(entry), ui_TextEntryReadOnlyValidate, NULL);
	}

	UI_TICK_EARLY(entry, false, true);

	UI_EDITABLE(entry)->time += g_ui_State.timestep;
	if (UI_EDITABLE(entry)->time > 1)
		UI_EDITABLE(entry)->time = 0;

	if (entry->cb)
	{
		w -= scale * (arrow->width + UI_HSTEP);
		BuildCBox(&box, x, y, w, h);
		CBoxClipTo(&pBox, &box);
	}
	if (entry->area)
	{
		w -= scale * (plus->width + UI_HSTEP);
		BuildCBox(&box, x, y, w, h);
		CBoxClipTo(&pBox, &box);
	}
	if (entry->smf)
	{
		w -= scale * (plus->width + UI_HSTEP);
		BuildCBox(&box, x, y, w, h);
		CBoxClipTo(&pBox, &box);
	}

	mouseClickHitRight = mouseClickHit(MS_RIGHT, &box);
	if (mouseDoubleClickHit(MS_LEFT,&box))
	{
		ui_SetFocus(entry);
		ui_TextEntryCursorToMouseWordSelect(entry, g_ui_State.mouseX - (x + ui_DrawingDescriptionLeftSize( &drawDesc )), scale);
		inpHandled();
		ui_SetCursorByName(ui_GetTextCursor(UI_WIDGET(entry)));
	}
	else if (mouseClickHit(MS_LEFT, &box) || mouseClickHitRight)
	{
		bool hasSelection = TextBuffer_GetSelection( UI_EDITABLE(entry)->pBuffer, NULL, NULL, NULL );
		
		if(!mouseClickHitRight || !hasSelection) {
			ui_TextEntryCursorToMouse(entry, g_ui_State.mouseX - (x + ui_DrawingDescriptionLeftSize( &drawDesc )), scale);
		}
		ui_SetFocus(entry);
		ui_SetCursorByName(ui_GetTextCursor(UI_WIDGET(entry)));

		if (mouseClickHit(MS_RIGHT, &box) && !UI_WIDGET(entry)->contextF)
		{
			const char* clipboard = TextBuffer_GetClipboard( UI_EDITABLE(entry)->pBuffer );
			s_textEntryMenuEntry = entry;
			if (entry->readOnly) {
				s_textEntryItemCopy->active = hasSelection;
				ui_MenuPopupAtCursor(s_textEntryReadOnlyMenu);
			} else {
				s_textEntryItemCut->active = hasSelection;
				s_textEntryItemCopy->active = hasSelection;
				s_textEntryItemPaste->active = !nullStr( clipboard );
				ui_MenuPopupAtCursor(s_textEntryMenu);
			}
		}
		
		inpHandled();
	}
	else if (mouseCollision(&box))
	{
		UIDeviceState *device = ui_StateForDevice(g_ui_State.device);
		ui_SetCursorByName( ui_GetTextCursor( UI_WIDGET( entry )));
		ui_CursorLock();
	}

	if (mouseDragHit(MS_LEFT, &box))
	{
		S32 iStart;
		S32 iDownX;
		S32 iDownY;
		mouseDownPos(MS_LEFT, &iDownX, &iDownY);
		iStart =  ui_TextEntryPixelToCursor(entry, iDownX - (x + ui_DrawingDescriptionLeftSize( &drawDesc )), scale);
		ui_SetFocus(entry);
		UI_EDITABLE(entry)->selecting = true;

		if(inpLevelPeek(INP_SHIFT))
			ui_EditableSelectToPos(UI_EDITABLE(entry), iStart);
		else
			ui_EditableSetSelection(UI_EDITABLE(entry), iStart, 0);
		inpHandled();
	}
	else if (UI_EDITABLE(entry)->selecting)
	{
		S32 iStart;
		S32 iEnd = ui_TextEntryPixelToCursor(entry, g_ui_State.mouseX - (x + ui_DrawingDescriptionLeftSize( &drawDesc )), scale);
		UI_EDITABLE(entry)->time = 0;
		TextBuffer_GetSelection(UI_EDITABLE(entry)->pBuffer, NULL, &iStart, NULL);
		ui_EditableSetSelection(UI_EDITABLE(entry), iStart, iEnd - iStart);
		if (!mouseIsDown(MS_LEFT))
			UI_EDITABLE(entry)->selecting = false;
		inpHandled();
	}

	if (entry->cb)
	{
		CBox tempBox;
		BuildCBox(&tempBox, x + w, y, (arrow->width + UI_HSTEP) * scale, h);
		CBoxClipTo(&pBox, &tempBox);
		if (mouseDownHit(MS_LEFT, &tempBox))
		{
			ui_SetFocus(entry);
			ui_TextEntryUpdateComboSelectedRows(entry);
			ui_ComboBoxSetPopupOpen(entry->cb, !entry->cb->opened);
			inpHandled();
		}
		if (entry->cb->closeOnNextTick)
		{
			entry->cb->closeOnNextTick = false;
			ui_ComboBoxSetPopupOpen(entry->cb, false);
		}
		if (entry->cb->opened)
		{
			F32 popupX = x;
			F32 popupY = y + h;
			F32 popupHeight = (eaSize(entry->cb->model) + 1) * entry->cb->rowHeight * scale;
			if (y + h + popupHeight > g_ui_State.screenHeight)
			{
				popupHeight = g_ui_State.screenHeight - (y + h);
				if (popupHeight < entry->cb->rowHeight * 4)
				{
					popupHeight = floorf((eaSize(entry->cb->model) + 1) * entry->cb->rowHeight * scale);
					if (y - popupHeight <= 0)
					{
						popupHeight = y - 1 - h;
					}
					popupY = y - popupHeight - h;
				}
			}

			if (entry->cb->cbPopupTick)
				entry->cb->cbPopupTick(entry->cb, popupX, popupY, w + (scale * (arrow->width + UI_HSTEP)), popupHeight, scale);
		}
	}
	if (entry->area)
	{
		CBox tempBox;
		if (entry->cb)
			BuildCBox(&tempBox, x + w + (arrow->width + UI_HSTEP) * scale, y, (plus->width + UI_HSTEP) * scale, h);
		else
			BuildCBox(&tempBox, x + w, y, (plus->width + UI_HSTEP) * scale, h);
		CBoxClipTo(&pBox, &tempBox);
		if (mouseDownHit(MS_LEFT, &tempBox))
		{
			ui_SetFocus(entry);
			ui_TextEntrySetPopupAreaOpen(entry, !entry->areaOpened);
			inpHandled();
		}
		
		if (entry->areaCloseOnNextTick)
		{
			entry->areaCloseOnNextTick = false;
			ui_TextEntrySetPopupAreaOpen(entry, false);
		}
		if (entry->areaOpened)
		{
			F32 popupHeight = 105 * scale;
			F32 popupWidth = (w-3)/scale;
			if (y + popupHeight > g_ui_State.screenHeight)
				popupHeight = g_ui_State.screenHeight - y;
			ui_WidgetSetPosition(UI_WIDGET(entry->area), (x+1)/g_ui_State.scale, (y+1)/g_ui_State.scale);
			ui_WidgetSetWidth(UI_WIDGET(entry->area), popupWidth);
			ui_WidgetSetHeight(UI_WIDGET(entry->area), popupHeight/scale);
			UI_WIDGET(entry->area)->scale = scale / g_ui_State.scale;

			// Make sure changed/inherited flags move into text area
			ui_SetChanged(UI_WIDGET(entry->area), ui_IsChanged(UI_WIDGET(entry)));
			ui_SetInherited(UI_WIDGET(entry->area), ui_IsInherited(UI_WIDGET(entry)));
		}
	}
	if (entry->smf)
	{
		CBox tempBox;
		BuildCBox(&tempBox, x + w, y, (plus->width + UI_HSTEP) * scale, h);
		CBoxClipTo(&pBox, &tempBox);
		if (mouseDownHit(MS_LEFT, &tempBox))
		{
			ui_SetFocus(entry);
			ui_TextEntrySetPopupSMFOpen(entry, !entry->smfOpened);
			inpHandled();
		}
		if (entry->smfOpened)
		{
			F32 popupWidth = realW;
			ui_WidgetSetPosition(UI_WIDGET(entry->smf), (x+1)/g_ui_State.scale, (y+h)/g_ui_State.scale);
			ui_WidgetSetWidth(UI_WIDGET(entry->smf), popupWidth/scale);
			if( ui_WidgetGetHeight( UI_WIDGET( entry->smf )) < 15 ) {
				ui_WidgetSetHeight( UI_WIDGET( entry->smf ), 15 );
			}
			UI_WIDGET(entry->smf)->scale = scale / g_ui_State.scale;

			// Make sure changed/inherited flags move into text area
			ui_SetChanged(UI_WIDGET(entry->smf), ui_IsChanged(UI_WIDGET(entry)));
			ui_SetInherited(UI_WIDGET(entry->smf), ui_IsInherited(UI_WIDGET(entry)));
		}
	}
	
	UI_TICK_LATE(entry);

	entry->editable.dirty = false;
	entry->editable.cursorDirty = false;
}

static void ui_TextEntrySelectionFillDrawingDescription( UITextEntry* entry, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( entry );
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = skin->astrTextEntryStyleSelectedText;
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->textureNameUsingLegacyColor = "white";
	}
}

static UIStyleFont* ui_TextEntryGetFont(UITextEntry* entry)
{
	UIStyleFont* font = NULL;
	UISkin* skin = UI_GET_SKIN(entry);
	
	if (skin)
	{
		if( nullStr( ui_TextEntryGetText( entry ))) {
			font = GET_REF(skin->hTextEntryDefaultTextFont);
		} else if( ui_IsActive( UI_WIDGET( entry ))) {
			font = GET_REF(skin->hTextEntryTextFont);
		} else {
			font = GET_REF(skin->hTextEntryDisabledFont);
		}
	}
	else
	{
		font = GET_REF(g_ui_State.font);
	}

	if (!font) {
		font = ui_WidgetGetFont( UI_WIDGET( entry ));
	}

	if (GET_REF(entry->font))
		font = GET_REF(entry->font);

	return font;
}

static UIStyleFont* ui_TextEntryGetHighlightFont(UITextEntry* entry)
{
	UIStyleFont* font = NULL;
	UISkin* skin = UI_GET_SKIN(entry);
	
	if (skin)
	{
		font = GET_REF( skin->hTextEntryHighlightTextFont );
	}
	else
	{
		font = GET_REF(g_ui_State.font);
	}

	if (!font) {
		font = ui_WidgetGetFont( UI_WIDGET( entry ));
	}

	if (GET_REF(entry->font))
		font = GET_REF(entry->font);

	return font;
}

static void ui_TextEntryDrawHelper(UITextEntry *entry, const char * text, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(entry);
	AtlasTex *arrow = (g_ui_Tex.arrowDropDown);
	AtlasTex *plus = (g_ui_Tex.plus);
	CBox clipped;
	F32 textWidth;
	F32 preCursorWidth, postCursorWidth;
	F32 drawOffset;
	int drawCursorX;
	S32 selectionStart;
	S32 selectionEnd;
	UIStyleFont *font = ui_TextEntryGetFont(entry);
	UIStyleFont *highlightFont = ui_TextEntryGetHighlightFont(entry);
	Color c, border;
	UIDrawingDescription desc = { 0 };
	int cursorPos = ui_EditableGetCursorPosition(UI_EDITABLE(entry));

	ui_TextEntryFillDrawingDescription( entry, &desc );

	if (UI_GET_SKIN(entry))
	{
		if (ui_IsFocused(UI_WIDGET(entry)))
		{
			if (!ui_IsActive(UI_WIDGET(entry)))			
				c = UI_GET_SKIN(entry)->entry[0];
			else if (ui_IsChanged(UI_WIDGET(entry)))
				c = ColorLerp(UI_GET_SKIN(entry)->entry[1], UI_GET_SKIN(entry)->entry[3], 0.5);
			else if (ui_IsInherited(UI_WIDGET(entry)))
				c = ColorLerp(UI_GET_SKIN(entry)->entry[1], UI_GET_SKIN(entry)->entry[4], 0.5);
			else
				c = UI_GET_SKIN(entry)->entry[1];
		}
		else if (!ui_IsActive(UI_WIDGET(entry)))
			c = UI_GET_SKIN(entry)->entry[2];
		else if (ui_IsChanged(UI_WIDGET(entry)))
			c = UI_GET_SKIN(entry)->entry[3];
		else if (ui_IsInherited(UI_WIDGET(entry)))
			c = UI_GET_SKIN(entry)->entry[4];
		else
			c = UI_GET_SKIN(entry)->entry[0];
		border = UI_GET_SKIN(entry)->thinBorder[0];
	}
	else
	{
		c = entry->widget.color[0];
		border = ColorBlack;
	}

	textWidth = ui_StyleFontWidth(font, scale, text);
	postCursorWidth = ui_StyleFontWidth(font, scale, UTF8GetCodepoint(text, cursorPos));
	preCursorWidth = textWidth - postCursorWidth;

	if(!entry->readOnly) {
		ui_DrawingDescriptionDraw( &desc, &box, scale, z, 255, c, border );
	}
	ui_DrawingDescriptionInnerBoxCoords( &desc, &x, &y, &w, &h, scale );

	if (TextBuffer_GetSelection(UI_EDITABLE(entry)->pBuffer, NULL, &selectionStart, &selectionEnd))
	{
		Color selection = {0, 0, 0, 0x22};
		CBox selectionBox;
		F32 offset = x - entry->lastDrawnOffset;
		F32 start = offset + (textWidth - ui_StyleFontWidth(font, scale, UTF8GetCodepoint(text, selectionStart)));
		F32 end = offset + (textWidth - ui_StyleFontWidth(font, scale, UTF8GetCodepoint(text, selectionEnd)));
		UIDrawingDescription selectionDesc = { 0 };
		ui_TextEntrySelectionFillDrawingDescription( entry, &selectionDesc );

		if (start > end)
			swap(&start, &end, sizeof(F32));

		clipperPushRestrict(&box);
		BuildCBox(&selectionBox, start, y, end - start, h);
		ui_DrawingDescriptionDraw( &selectionDesc, &selectionBox, scale, z, 255, selection, ColorBlack );
		clipperPop();
	}

	if (entry->cb && ui_IsActive(UI_WIDGET(entry)))
		w -= scale * (arrow->width + UI_HSTEP);
	if ((entry->area || entry->smf) && ui_IsActive(UI_WIDGET(entry)))
		w -= scale * (plus->width + UI_HSTEP);

	// If the cursor's moved out of bounds, we need to recalculate the draw offset.
	if(entry->forceLeftAligned && g_ui_State.focused != UI_WIDGET(entry))
		drawOffset = 0;
	else if (preCursorWidth < entry->lastDrawnOffset)
		drawOffset = preCursorWidth;
	else if (entry->lastDrawnOffset + w < preCursorWidth)
		drawOffset = preCursorWidth - w;
	else
		drawOffset = entry->lastDrawnOffset;

	drawOffset = MIN(drawOffset, textWidth - w);
	drawOffset = MAX(0, drawOffset);

	// Clip to the left and right padding for the text, but not for the cursor.
	BuildCBox(&clipped, x, y, w, h);
	clipperPushRestrict(&clipped);
	drawCursorX = round(x + preCursorWidth - drawOffset);

	{
		// Make sure text is only drawn at exact pixels
		float xPos = floorf( x - drawOffset );
		float yPos = floorf( y + (h + ui_StyleFontLineHeight( font, scale ))/2 );

		if( selectionEnd != selectionStart ) {
			int selectionFirst = MIN( selectionStart, selectionEnd );
			int selectionLast = MAX( selectionStart, selectionEnd );
			const char* textSelectionFirst = UTF8GetCodepoint( text, selectionFirst );
			const char* textSelectionLast = UTF8GetCodepoint( text, selectionLast );

			ui_StyleFontUse(font, false, UI_WIDGET(entry)->state);
			gfxfont_PrintEx(g_font_Active, xPos, yPos, z + 0.2f, scale, scale, 0, text, selectionFirst, 0, NULL);
			gfxfont_Dimensions(g_font_Active, scale, scale, text, selectionFirst, NULL, NULL, &xPos, false);

			ui_StyleFontUse(highlightFont, false, UI_WIDGET(entry)->state);
			gfxfont_PrintEx(g_font_Active, xPos, yPos, z + 0.2f, scale, scale, 0, textSelectionFirst, selectionLast - selectionFirst, 0, NULL);
			gfxfont_Dimensions(g_font_Active, scale, scale, textSelectionFirst, selectionLast - selectionFirst, NULL, NULL, &xPos, false);

			ui_StyleFontUse(font, false, UI_WIDGET(entry)->state);
			gfxfont_PrintEx(g_font_Active, xPos, yPos, z + 0.2f, scale, scale, 0, textSelectionLast, UTF8GetLength(text) - selectionLast, 0, NULL);
		} else {
			ui_StyleFontUse(font, false, UI_WIDGET(entry)->state);
			gfxfont_Print(xPos, yPos, z + 0.2f, scale, scale, 0, text );
		}
		if (!SAFE_DEREF(text) && ui_EditableGetDefault(UI_EDITABLE(entry)) && !ui_IsFocused(UI_WIDGET(entry)))
		{
			ui_StyleFontUse(font, false, UI_WIDGET(entry)->state);
			gfxfont_Print(xPos, yPos, z + 0.2f, scale, scale, 0, ui_EditableGetDefault(UI_EDITABLE(entry)));
		}
	}
	clipperPop();
	if (ui_IsFocused(UI_WIDGET(entry)) && UI_EDITABLE(entry)->time < 0.5)
	{
		Color cursorColor = ui_StyleFontGetColor(font);
		BuildCBox(&clipped, x, y, w, h);
		clipperPushRestrict(&clipped);
		gfxDrawLine(drawCursorX + 1, y, z + 0.2f, drawCursorX + 1, y + h, cursorColor);
		clipperPop();
		
	}
	if (ui_IsFocused(UI_WIDGET(entry)) && entry->lastCursorPosition != cursorPos)
	{
		ui_ScrollbarParentScrollTo(drawCursorX, y);
		ui_ScrollbarParentScrollTo(drawCursorX, y + h);
		entry->lastCursorPosition = cursorPos;
	}
	entry->lastDrawnOffset = drawOffset;

	if (entry->cb && ui_IsActive(UI_WIDGET(entry)))
	{
		UIComboBox *cb = entry->cb;
		w += scale * (arrow->width + UI_HSTEP);
		display_sprite(arrow, x + w - scale * (arrow->width + UI_HSTEP), y + h/2 - scale * arrow->height/2,
			z + 0.2f, scale, scale, RGBAFromColor(c));
	}
	if ((entry->area || entry->smf) && ui_IsActive(UI_WIDGET(entry)))
	{
		w += scale * (plus->width + UI_HSTEP);
		display_sprite(plus, x + w - scale * (plus->width + UI_HSTEP), y + h/2 - scale * plus->height/2,
			z + 0.2f, scale, scale, RGBAFromColor(c));
	}	
}

void ui_TextEntryDrawPassword(UITextEntry *entry, UI_PARENT_ARGS)
{
	size_t len = UTF8GetLength(ui_TextEntryGetText(entry));
	char *pAsterisks = malloc (len + 1);
	memset(pAsterisks, '*', len);
	pAsterisks[len] = '\0';
	ui_TextEntryDrawHelper(entry, pAsterisks, pX, pY, pW, pH, pScale);
}

void ui_TextEntryDraw(UITextEntry *entry, UI_PARENT_ARGS)
{
	ui_TextEntryDrawHelper(entry, ui_TextEntryGetText(entry), pX, pY, pW, pH, pScale);
}

UITextEntry *ui_TextEntryCreateEx(const unsigned char *text, int x, int y MEM_DBG_PARMS)
{
	UITextEntry *entry = (UITextEntry *)calloc(1, sizeof(UITextEntry));
	ui_TextEntryInitializeEx(entry, text, x, y MEM_DBG_PARMS_CALL);
	return entry;
}

void ui_TextEntryInitializeEx(UITextEntry *entry, const unsigned char *text, int x, int y MEM_DBG_PARMS) 
{
	UIStyleFont *font = NULL;
	F32 height;
	ui_WidgetInitializeEx(UI_WIDGET(entry), ui_TextEntryTick, ui_TextEntryDraw, ui_TextEntryFreeInternal, ui_TextEntryInput, ui_TextEntryFocus MEM_DBG_PARMS_CALL);
	ui_EditableInitialize(UI_EDITABLE(entry), text);
	ui_EditableSetSMFUpdateCallback(UI_EDITABLE(entry), ui_TextEntrySMFUpdate, NULL);
	ui_WidgetSetPosition(UI_WIDGET(entry), x, y);
	if (UI_GET_SKIN(entry))
		font = GET_REF(UI_GET_SKIN(entry)->hNormal);
	height = ui_StyleFontLineHeight(font, 1.f) + UI_STEP;
	ui_WidgetSetDimensions(UI_WIDGET(entry), 100, height);
	entry->widget.unfocusF = ui_TextEntryUnfocus;
	entry->widget.uFocusWhenDisabled = 1;
	entry->lastCursorPosition = -1;
	ui_TextEntrySetEnterCallback(entry, ui_TextEntryDefaultEnterCallback, NULL);

	if( !s_textEntryMenu ) {
		s_textEntryItemCut = ui_MenuItemCreate2( "Cut", "Ctrl-X", UIMenuCallback, ui_TextEntryCallFunc, ui_TextEntryCut, NULL );
		s_textEntryItemCopy = ui_MenuItemCreate2( "Copy", "Ctrl-C", UIMenuCallback, ui_TextEntryCallFunc, ui_TextEntryCopy, NULL );
		s_textEntryItemPaste = ui_MenuItemCreate2( "Paste", "Ctrl-V", UIMenuCallback, ui_TextEntryCallFunc, ui_TextEntryPaste, NULL );
		s_textEntryItemSelectAll = ui_MenuItemCreate2( "Select All", "Ctrl-A", UIMenuCallback, ui_TextEntryCallFunc, ui_TextEntrySelectAll, NULL );
		
		s_textEntryMenu = ui_MenuCreateWithItems( "Right Click",
												  s_textEntryItemCut, s_textEntryItemCopy, s_textEntryItemPaste,
												  ui_MenuItemCreate( "---", UIMenuSeparator, NULL, NULL, NULL ),
												  s_textEntryItemSelectAll,
												  NULL );
		s_textEntryReadOnlyMenu = ui_MenuCreateWithItems( "Right Click (RO)", s_textEntryItemCopy, s_textEntryItemSelectAll, NULL );
	}
}

void ui_TextEntryFreeInternal(UITextEntry *entry)
{
	// Freeing should not call a callback. It only leads to pain.
	entry->finishedF = NULL;
	entry->comboValidate = false;
	REMOVE_HANDLE(entry->font);
	if (entry->cb)
		ui_ComboBoxFreeInternal(entry->cb);
	if (entry->area)
		ui_TextAreaFreeInternal(entry->area);
	if (entry->smf)
		ui_SMFViewFreeInternal(entry->smf);
	ui_EditableFreeInternal(UI_EDITABLE(entry));
}

void ui_TextEntryComboBoxCallback(UIComboBox *cb, UITextEntry *entry)
{
	unsigned char *pchTemp = NULL;
	estrStackCreate(&pchTemp);
	ui_ComboBoxGetSelectedsAsStringEx(cb, &pchTemp, entry->pchIndexSeparator);
	if(!cb->bMultiSelect)
		ui_SetFocus(entry); // Need to claim focus from combo box, unless multiselect. This is what closes the dropdown
	ui_TextEntrySetTextAndCallback(entry, pchTemp);
	estrDestroy(&pchTemp);

	if (entry->comboFinishOnSelect && entry->enterF)
		entry->enterF(entry, entry->enterData);
}

void ui_TextEntryEnumComboBoxCallback(UIComboBox *cb, int i, UITextEntry *entry)
{
	unsigned char *pchTemp = NULL;
	estrStackCreate(&pchTemp);
	ui_ComboBoxGetSelectedsAsStringEx(cb, &pchTemp, entry->pchIndexSeparator);
	ui_SetFocus(entry); // Need to claim focus from combo box
	ui_TextEntrySetTextAndCallback(entry, pchTemp);
	estrDestroy(&pchTemp);

	if (entry->comboFinishOnSelect && entry->enterF)
		entry->enterF(entry, entry->enterData);
}

void ui_TextEntrySetComboBox(UITextEntry *entry, struct UIComboBox *cb)
{
	if (entry->cb)
		ui_WidgetGroupRemove(&entry->widget.children, UI_WIDGET(entry->cb));
	entry->cb = cb;
	if (!cb)
		return;
	entry->widget.childrenInactive = true;
	ui_WidgetGroupAdd(&entry->widget.children, UI_WIDGET(cb));
	ui_ComboBoxSetSelectedCallback(cb, ui_TextEntryComboBoxCallback, entry);
}

void ui_TextEntrySetTextArea(UITextEntry *entry, struct UITextArea *area)
{
	if (entry->area && entry->areaOpened)
		ui_TextEntrySetPopupAreaOpen(entry, false);
	entry->area = area;
	if (area)
	{
 		entry->areaClosedF = ((UIWidget*) area)->onUnfocusF;
		entry->areaClosedData = ((UIWidget*) area)->onUnfocusData;
	}
}

void ui_TextEntrySetSMFView(UITextEntry *entry, UISMFView *smf)
{
	if (entry->smf && entry->smfOpened)
		ui_TextEntrySetPopupSMFOpen(entry, false);
	entry->smf = smf;
}

static void ui_TextEntryPopupAreaChangeCallback(UITextArea *area, UITextEntry *entry)
{
	// Forward changes to the main text entry
	ui_TextEntrySetTextAndCallback(entry, ui_TextAreaGetText(area));
}

static void ui_TextEntryPopupAreaUnfocusCallback(UITextArea *area, UITextEntry *entry)
{
	// Forward changes to the main text entry
	ui_TextEntrySetPopupAreaOpen(entry, false);
	if (entry->areaClosedF)
		entry->areaClosedF(entry, entry->areaClosedData);
}

// Callback on popup area's tick so we can dismiss the window on unhandled mouse input
static void ui_TextEntryPopupAreaTick(UITextArea *area, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(area);

	// First pass to list
	ui_TextAreaTick(area, pX, pY, pW, pH, pScale);

	// If unhandled mouse action outside the combo parent, then hide
	if (mouseDown(MS_LEFT) && !mouseDownHit(MS_LEFT, &box))
		((UITextEntry*)UI_EDITABLE(area)->changedData)->areaCloseOnNextTick = true;
}

void ui_TextEntrySetPopupAreaOpen(UITextEntry *entry, bool opened)
{
	if (opened == entry->areaOpened)
		return;

	entry->areaOpened = opened;

	if (opened) 
	{
		// Copy in the text when opening
		ui_TextAreaSetText(entry->area, ui_TextEntryGetText(entry));

		// Copy cursor position
		ui_TextAreaSetCursorPosition(entry->area, ui_EditableGetCursorPosition(UI_EDITABLE(entry)));

		// Intercede on the tick
		entry->area->widget.tickF = ui_TextEntryPopupAreaTick;

		// Register for changes
		ui_TextAreaSetChangedCallback(entry->area, ui_TextEntryPopupAreaChangeCallback, entry);
		ui_WidgetSetUnfocusCallback(UI_WIDGET(entry->area), ui_TextEntryPopupAreaUnfocusCallback, entry);

		// Make sure the skin applies
		if (UI_GET_SKIN(entry))
			ui_WidgetSkin(UI_WIDGET(entry->area), UI_GET_SKIN(entry));

		// Add the widget to the device
		UI_WIDGET(entry->area)->priority = UI_HIGHEST_PRIORITY;
		ui_TopWidgetAddToDevice(UI_WIDGET(entry->area), NULL);

		ui_SetFocus(UI_WIDGET(entry->area));
	} 
	else 
	{
		// unattach from top to hide it
		ui_WidgetRemoveFromGroup(UI_WIDGET(entry->area));

		// Act like the main text entry lost focus for callbacks and such
		ui_TextEntryUnfocus(entry,entry->area);
	}
}

void ui_TextEntrySetPopupSMFOpen(UITextEntry *entry, bool opened)
{
	if (opened == entry->smfOpened)
		return;

	entry->smfOpened = opened;

	if (opened) 
	{
		// Copy in the text when opening
		ErrorfPushCallback( NULL, NULL );
		ui_SMFViewSetText(entry->smf, ui_TextEntryGetText(entry), NULL);
		ErrorfPopCallback();

		// Make sure the skin applies
		ui_SMFViewSetDrawBackground(entry->smf, true);
		if (UI_GET_SKIN(entry))
			ui_WidgetSkin(UI_WIDGET(entry->smf), UI_GET_SKIN(entry));

		// Add the widget to the device
		UI_WIDGET(entry->smf)->priority = UI_HIGHEST_PRIORITY;
		ui_TopWidgetAddToDevice(UI_WIDGET(entry->smf), NULL);
	} 
	else 
	{
		// unattach from top to hide it
		ui_WidgetRemoveFromGroup(UI_WIDGET(entry->smf));

		// Act like the main text entry lost focus for callbacks and such
		ui_TextEntryUnfocus(entry,entry->smf);
	}
}

void ui_TextEntrySetUpKeyCallback(SA_PARAM_NN_VALID UITextEntry *entry, UIActivationFunc upKeyF, UserData upKeyData)
{
	entry->upKeyF = upKeyF;
	entry->upKeyData = upKeyData;
}

void ui_TextEntrySetDownKeyCallback(SA_PARAM_NN_VALID UITextEntry *entry, UIActivationFunc downKeyF, UserData downKeyData)
{
	entry->downKeyF = downKeyF;
	entry->downKeyData = downKeyData;
}

void ui_TextEntrySetEnterCallback(UITextEntry *entry, UIActivationFunc enterF, UserData enterData)
{
	entry->enterF = enterF;
	entry->enterData = enterData;
}

void ui_TextEntryDefaultEnterCallback(UITextEntry *entry, UserData ignored)
{
	if (g_ui_State.focused == UI_WIDGET(entry))
		ui_SetFocus(NULL);
}

bool ui_TextEntryValidationNoSpaces(UITextEntry *entry, unsigned char **oldString, unsigned char **newString, UserData dummy)
{
	unsigned char *str = *newString;
	while( *str )
	{
		if (isspace( *str ))
			*str = '_';
	}
	return true;
}

bool ui_TextEntryValidationIntegerOnly(UITextEntry *entry, unsigned char **oldString, unsigned char **newString, UserData dummy)
{
	unsigned char *str = *newString;
	if (*str && *str == '-')str++;
	while( *str )
	{
		if (*str < '0' || *str > '9')
			return 0;
		str++;
	}
	return 1;
}

bool ui_TextEntryValidationFloatOnly(UITextEntry *entry, unsigned char **oldString, unsigned char **newString, UserData dummy)
{
	int decimal_point=0;
	unsigned char *str = *newString;
	if (*str && *str == '-') str++;
	while( *str )
	{
		if( *str == '.' )
		{
			decimal_point++;
			if(decimal_point>1)
				return 0;
		}
		else if( (*str < '0' || *str > '9') )
			return 0;
		str++;
	}
	return 1;
}

// Tests for a string with simple values, such as alphanumeric, space, 
// underscore/dash, apostrophe, and parens.  This rules out formatting characters
// and hidden characters.
bool ui_TextEntryValidationSimpleOnly(UITextEntry *entry, unsigned char **oldString, unsigned char **newString, UserData dummy)
{
	char *str = *newString;
	while( *str )
	{
		if ( !isalnum(*str) &&
			 *str != ' ' &&
			 *str != '\'' &&
			 *str != ',' &&
			 *str != '.' &&
			 *str != '.' &&
			 *str != '?' &&
			 *str != '_' &&
			 *str != '(' &&
			 *str != ')' &&
			 *str != '[' &&
			 *str != ']'
			 ) 
		{
			return false;
		}
		str++;
	}
	return true;
}

void ui_TextEntryFocus(UITextEntry *entry, UIAnyWidget *focusitem)
{
	if (!entry->bHasInputFocus)
	{
		ui_StartDeviceKeyboardTextInput();
		entry->bHasInputFocus = true;
	}
	if (entry->magnify == false)
	{
		if (entry->magnifyScale > 0)
		{
			F32 temp = entry->widget.scale;
			entry->widget.scale = entry->magnifyScale;
			entry->magnifyScale = temp;
		}
		if (entry->magnifyWidth > 0)
		{
			U32 temp = entry->widget.width;
			entry->widget.width = entry->magnifyWidth;
			entry->magnifyWidth= temp;
		}
		entry->magnify = true;
	}

	if (entry->selectOnFocus)
	{
		ui_EditableSelectAll(UI_EDITABLE(entry));
	}
	entry->lastCursorPosition = -1;
}

void ui_TextEntryUnfocus(UITextEntry *entry, UIAnyWidget *focusitem)
{
	if (entry->bHasInputFocus)
	{
		ui_StopDeviceKeyboardTextInput();
		entry->bHasInputFocus = false;
	}
	if (entry->magnify)
	{
		if (entry->magnifyScale > 0)
		{
			F32 temp;
			temp = entry->widget.scale;
			entry->widget.scale = entry->magnifyScale;
			entry->magnifyScale = temp;
		}
		if (entry->magnifyWidth > 0)
		{
			U32 temp;
			temp = entry->widget.width;
			entry->widget.width = entry->magnifyWidth;
			entry->magnifyWidth = temp;
		}
		entry->magnify = false;
	}
	if (entry->comboValidate)
		ui_TextEntryComboValidate(entry);
	if (entry->trimWhitespace)
		TextBuffer_TrimLeadingAndTrailingWhitespace(entry->editable.pBuffer);
	if (entry->finishedF && (!entry->cb
							 || (!ui_WidgetSearchTree(UI_WIDGET(entry->cb->pPopupList), focusitem) && !ui_WidgetSearchTree(UI_WIDGET(entry->cb->pPopupFilteredList), focusitem))
							 || (!focusitem && !entry->cb->pPopupList && !entry->cb->pPopupFilteredList)))
		entry->finishedF(entry, entry->finishedData);
	ui_EditableSetSelection(UI_EDITABLE(entry), 0, 0);
}

void ui_TextEntrySetWidthInCharacters(UITextEntry *entry, S32 iChars)
{
	UIStyleFont *pFont = ui_TextEntryGetFont(entry);
	unsigned char *pchM = calloc(1, iChars + 1);
	memset(pchM, 'W', iChars);
	pchM[iChars] = '\0';
	ui_WidgetSetWidth(UI_WIDGET(entry), ui_StyleFontWidth(pFont, 1.f, pchM) + UI_DSTEP);
	free(pchM);
}

UITextEntry *ui_TextEntryCreateWithStringCombo(const unsigned char *text, int x, int y, 
											   cUIModel model, 
											   bool bAutoComplete, bool bDrawSelected, bool bValidate, bool bFiltered)
{
	UITextEntry *pEntry;
	UIComboBox *pCombo;

	pEntry = ui_TextEntryCreate(text, x, y);
	pEntry->autoComplete = bAutoComplete;
	pEntry->comboFinishOnSelect = true;
	pEntry->comboValidate = bValidate;
	if (bFiltered)
		pCombo = ui_FilteredComboBoxCreate(x, y, 0, NULL, model, NULL);
	else
		pCombo = ui_ComboBoxCreate(x, y, 0, NULL, model, NULL);
	pCombo->drawSelected = bDrawSelected;
	ui_TextEntrySetComboBox(pEntry, pCombo);
	return pEntry;
}

UITextEntry *ui_TextEntryCreateWithStringMultiCombo(const unsigned char *text, int x, int y, 
											   cUIModel model, 
											   bool bAutoComplete, bool bDrawSelected, bool bValidate, bool bFiltered)
{
	UITextEntry *pEntry;
	UIComboBox *pCombo;

	pEntry = ui_TextEntryCreate(text, x, y);
	pEntry->autoComplete = bAutoComplete;
	pEntry->comboFinishOnSelect = true;
	pEntry->comboValidate = bValidate;
	if (bFiltered)
		pCombo = ui_FilteredComboBoxCreate(x, y, 0, NULL, model, NULL);
	else
		pCombo = ui_ComboBoxCreate(x, y, 0, NULL, model, NULL);
	pCombo->drawSelected = bDrawSelected;
	ui_ComboBoxSetMultiSelect(pCombo, true);
	ui_TextEntrySetComboBox(pEntry, pCombo);
	ui_ComboBoxSetSelectedsAsString(pCombo, text);

	return pEntry;
}

UITextEntry *ui_TextEntryCreateWithObjectCombo(const unsigned char *text, int x, int y, 
											   cUIModel model,
											   ParseTable *pTable, const unsigned char *field,
											   bool bAutoComplete, bool bDrawSelected, bool bValidate, bool bFiltered)
{
	UITextEntry *pEntry;
	UIComboBox *pCombo;

	pEntry = ui_TextEntryCreate(text, x, y);
	pEntry->autoComplete = bAutoComplete;
	pEntry->comboFinishOnSelect = true;
	pEntry->comboValidate = bValidate;
	if (bFiltered)
		pCombo = ui_FilteredComboBoxCreate(x, y, 0, pTable, model, field);
	else
		pCombo = ui_ComboBoxCreate(x, y, 0, pTable, model, field);
	pCombo->drawSelected = bDrawSelected;
	ui_TextEntrySetComboBox(pEntry, pCombo);
	return pEntry;
}

UITextEntry *ui_TextEntryCreateWithDictionaryCombo(const unsigned char *text, int x, int y, 
												   const void *dictHandleOrName,
												   ParseTable *pTable, const unsigned char *field,
												   bool bAutoComplete, bool bDrawSelected, bool bValidate, bool bFiltered)
{
	UITextEntry *pEntry;
	UIComboBox *pCombo;

	pEntry = ui_TextEntryCreate(text, x, y);
	pEntry->autoComplete = bAutoComplete;
	pEntry->comboFinishOnSelect = true;
	pEntry->comboValidate = bValidate;
	if (bFiltered)
		pCombo = ui_FilteredComboBoxCreateWithDictionary(x, y, 0, dictHandleOrName, pTable, field);
	else
		pCombo = ui_ComboBoxCreateWithDictionary(x, y, 0, dictHandleOrName, pTable, field);
	pCombo->drawSelected = bDrawSelected;
	ui_TextEntrySetComboBox(pEntry, pCombo);
	return pEntry;
}

UITextEntry *ui_TextEntryCreateWithGlobalDictionaryCombo(const unsigned char *text, int x, int y, 
												   const char *dictHandleOrName, const unsigned char *field,
												   bool bAutoComplete, bool bDrawSelected, bool bValidate, bool bFiltered)
{
	UITextEntry *pEntry;
	UIComboBox *pCombo;

	pEntry = ui_TextEntryCreate(text, x, y);
	pEntry->autoComplete = bAutoComplete;
	pEntry->comboFinishOnSelect = true;
	pEntry->comboValidate = bValidate;
	if (bFiltered)
		pCombo = ui_FilteredComboBoxCreateWithGlobalDictionary(x, y, 0, dictHandleOrName, field);
	else
		pCombo = ui_ComboBoxCreateWithGlobalDictionary(x, y, 0, dictHandleOrName, field);
	pCombo->drawSelected = bDrawSelected;
	ui_TextEntrySetComboBox(pEntry, pCombo);
	return pEntry;
}

UITextEntry *ui_TextEntryCreateWithEnumCombo(const unsigned char *text, int x, int y, 
											 struct StaticDefineInt *enumTable,
											 bool bAutoComplete, bool bDrawSelected, bool bValidate, bool bFiltered)
{
	UITextEntry *pEntry;
	UIComboBox *pCombo;

	pEntry = ui_TextEntryCreate(text, x, y);
	pEntry->autoComplete = bAutoComplete;
	pEntry->comboFinishOnSelect = true;
	pEntry->comboValidate = bValidate;
	if (bFiltered)
		pCombo = ui_FilteredComboBoxCreateWithEnum(x, y, 0, enumTable, ui_TextEntryEnumComboBoxCallback, pEntry);
	else
		pCombo = ui_ComboBoxCreateWithEnum(x, y, 0, enumTable, ui_TextEntryEnumComboBoxCallback, pEntry);
	pCombo->drawSelected = bDrawSelected;

	// Don't call ui_TextEntrySetComboBox(), since it screws up the enum combo boxes with a
	//  call to ui_ComboBoxSetSelectedCallback(), instead do most of the same stuff manually.
	{
		if (pEntry->cb)
			ui_WidgetGroupRemove(&pEntry->widget.children, UI_WIDGET(pEntry->cb));
		pEntry->cb = pCombo;
		pEntry->widget.childrenInactive = true;
		ui_WidgetGroupAdd(&pEntry->widget.children, UI_WIDGET(pCombo));
	}

	return pEntry;
}


UITextEntry *ui_TextEntryCreateWithTextArea(const unsigned char *text, int x, int y)
{
	UITextEntry *pEntry;
	UITextArea *pArea;

	pEntry = ui_TextEntryCreate(text, x, y);
	pArea = ui_TextAreaCreate("");
	ui_TextEntrySetTextArea(pEntry, pArea);
	return pEntry;
}

UITextEntry *ui_TextEntryCreateWithSMFView(const unsigned char *text, int x, int y)
{
	UITextEntry *pEntry;
	UISMFView *pSMF;
	
	pEntry = ui_TextEntryCreate(text, x, y);
	pSMF = ui_SMFViewCreate(0, 0, 0, 0);
	ui_TextEntrySetSMFView(pEntry, pSMF);
	return pEntry;
}

void ui_TextEntrySetAsPasswordEntry(UITextEntry *entry, bool isPassword)
{
	UI_WIDGET(entry)->drawF = isPassword ? ui_TextEntryDrawPassword : ui_TextEntryDraw;
}

void ui_TextEntryCallFunc( void* ignored, UITextEntryMenuFunc fn )
{
	fn( s_textEntryMenuEntry );
}
