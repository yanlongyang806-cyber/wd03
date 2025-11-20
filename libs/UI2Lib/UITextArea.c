/***************************************************************************



***************************************************************************/

#include "EString.h"
#include "earray.h"
#include "StringUtil.h"
#include "TextBuffer.h"

#include "inputLib.h"
#include "inputMouse.h"
#include "inputText.h"
#include "inputKeyBind.h"

#include "GfxClipper.h"
#include "GfxSpriteText.h"
#include "GfxPrimitive.h"
#include "GfxSprite.h"

#include "UITextArea.h"
#include "UIScrollbar.h"
#include "UIInternal.h"
#include "UISMFView.h"
#include "UISkin.h"
#include "UIButton.h"
#include "UITabs.h"
#include "UIPane.h"
#include "UIMenu.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

typedef void (*UITextAreaMenuFunc)(UITextArea* area);

static void ui_TextAreaSetPopupSMFOpen(UITextArea *area, bool opened);
static void ui_TextAreaSetSMFEditActive(UITextArea *textarea, bool active);
static void ui_TextAreaFocus(SA_PARAM_NN_VALID UITextArea *textarea, UIAnyWidget *focusitem);
static void ui_TextAreaCallFunc( void* ignored, UITextAreaMenuFunc fn );
static UIStyleFont* ui_TextAreaGetFont( UITextArea* textarea );

static UIMenu* s_textAreaMenu;
static UIMenuItem* s_textAreaItemCut;
static UIMenuItem* s_textAreaItemCopy;
static UIMenuItem* s_textAreaItemPaste;
static UIMenuItem* s_textAreaItemSelectAll;
static UITextArea* s_textAreaMenuArea;


static void ui_TextAreaSMFUpdate(UITextArea* area, UserData ignored)
{
	if (area->smf && area->smfOpened) {
		ErrorfPushCallback( NULL, NULL );
		ui_SMFViewSetText(area->smf, ui_EditableGetText(UI_EDITABLE(area)), NULL);
		ErrorfPopCallback();
	}
}

UITextArea *ui_TextAreaCreate(const unsigned char *text)
{
	UITextArea *textarea = (UITextArea *)calloc(1, sizeof(UITextArea));
	ui_TextAreaInitialize(textarea, text);
	return textarea;
}

UITextArea *ui_TextAreaCreateWithSMFView(const unsigned char *text)
{
	UITextArea *textarea = ui_TextAreaCreate(text);
	UISMFView *smf = ui_SMFViewCreate(0,0,0,0);
	ui_TextAreaSetSMFView(textarea, smf);

	return textarea;
}

void ui_TextAreaSetSMFView(UITextArea *textarea, struct UISMFView *smf)
{
	if (textarea->smf && textarea->smfOpened)
		ui_TextAreaSetPopupSMFOpen(textarea, false);
	textarea->smf = smf;
}

void ui_TextAreaSetPopupSMFOpen(UITextArea *area, bool opened)
{
	if (opened == area->smfOpened)
		return;

	area->smfOpened = opened;

	if (opened) 
	{
		// Copy in the text when opening
		ErrorfPushCallback( NULL, NULL );
		ui_SMFViewSetText(area->smf, ui_TextAreaGetText(area), NULL);
		ErrorfPopCallback();

		// Make sure the skin applies
		ui_SMFViewSetDrawBackground(area->smf, true);
		if (UI_GET_SKIN(area))
			ui_WidgetSkin(UI_WIDGET(area->smf), UI_GET_SKIN(area));

		// Add the widget to the device
		UI_WIDGET(area->smf)->priority = UI_HIGHEST_PRIORITY;
		ui_TopWidgetAddToDevice(UI_WIDGET(area->smf), NULL);
	} 
	else 
	{
		// unattach from top to hide it
		ui_WidgetRemoveFromGroup(UI_WIDGET(area->smf));
	}
}

void ui_TextAreaInitialize(UITextArea *textarea, const unsigned char *text) 
{
	ui_WidgetInitialize(UI_WIDGET(textarea), ui_TextAreaTick, ui_TextAreaDraw,
		ui_TextAreaFreeInternal, ui_TextAreaInput, ui_TextAreaFocus);
	ui_EditableInitialize(UI_EDITABLE(textarea), text);
	ui_EditableSetSMFUpdateCallback(UI_EDITABLE(textarea), ui_TextAreaSMFUpdate, NULL);
	textarea->widget.unfocusF = ui_TextAreaUnfocus;
	textarea->widget.sb = ui_ScrollbarCreate(false, true);
	textarea->wordWrap = 1;

	if( !s_textAreaMenu ) {
		s_textAreaItemCut = ui_MenuItemCreate2( "Cut", "Ctrl-X", UIMenuCallback, ui_TextAreaCallFunc, ui_TextAreaCut, NULL );
		s_textAreaItemCopy = ui_MenuItemCreate2( "Copy", "Ctrl-C", UIMenuCallback, ui_TextAreaCallFunc, ui_TextAreaCopy, NULL );
		s_textAreaItemPaste = ui_MenuItemCreate2( "Paste", "Ctrl-V", UIMenuCallback, ui_TextAreaCallFunc, ui_TextAreaPaste, NULL );
		s_textAreaItemSelectAll = ui_MenuItemCreate2( "Select All", "Ctrl-A", UIMenuCallback, ui_TextAreaCallFunc, ui_TextAreaSelectAll, NULL );
		s_textAreaMenu = ui_MenuCreateWithItems( "Right Click",
												 s_textAreaItemCut, s_textAreaItemCopy, s_textAreaItemPaste,
												 ui_MenuItemCreate( "---", UIMenuSeparator, NULL, NULL, NULL ),
												 s_textAreaItemSelectAll,
												 NULL );
	}
}

void ui_TextAreaFreeInternal(UITextArea *textarea)
{
	ui_TextAreaSetSMFEditActive(textarea, false);
	
	eaDestroy(&textarea->lines);
	if (textarea->smf)
		ui_SMFViewFreeInternal(textarea->smf);
	ui_EditableFreeInternal(UI_EDITABLE(textarea));
}

bool ui_TextAreaSetText(UITextArea *textarea, const unsigned char *text)
{
	return ui_EditableSetText(UI_EDITABLE(textarea), text);
}

void ui_TextAreaSetTextAndCallback(UITextArea *textarea, const unsigned char *text)
{
	ui_EditableSetTextAndCallback(UI_EDITABLE(textarea), text);
}

const unsigned char *ui_TextAreaGetText(UITextArea *textarea)
{
	return ui_EditableGetText(UI_EDITABLE(textarea));
}

void ui_TextAreaSetChangedCallback(UITextArea *textarea, UIActivationFunc changedF, UserData changedData)
{
	ui_EditableSetChangedCallback(UI_EDITABLE(textarea), changedF, changedData);
}

void ui_TextAreaSetFinishedCallback(UITextArea *textarea, UIActivationFunc finishedF, UserData finishedData)
{
	textarea->finishedF = finishedF;
	textarea->finishedData = finishedData;
}

void ui_TextAreaSetCursorPosition(UITextArea *textarea, U32 cursorPos)
{
	ui_EditableSetCursorPosition(UI_EDITABLE(textarea), cursorPos);
}

U32 ui_TextAreaGetCursorPosition(UITextArea *textarea)
{
	return ui_EditableGetCursorPosition(UI_EDITABLE(textarea));
}

U32 ui_TextAreaInsertTextAt(UITextArea *textarea, U32 offset, const unsigned char *text)
{
	return ui_EditableInsertText(UI_EDITABLE(textarea), offset, text);
}

void ui_TextAreaDeleteTextAt(UITextArea *textarea, U32 offset, U32 length)
{
	ui_EditableDeleteText(UI_EDITABLE(textarea), offset, length);
}

void ui_TextAreaDeleteSelection(UITextArea *textarea)
{
	ui_EditableDeleteSelection(UI_EDITABLE(textarea));
}

void ui_TextAreaPaste(UITextArea *textarea)
{
	ui_EditablePaste(UI_EDITABLE(textarea));
}

void ui_TextAreaCopy(UITextArea *textarea)
{
	ui_EditableCopy(UI_EDITABLE(textarea));
}

void ui_TextAreaCut(UITextArea *textarea)
{
	ui_EditableCut(UI_EDITABLE(textarea));
}

void ui_TextAreaUndo(UITextArea *textarea)
{
	ui_EditableUndo(UI_EDITABLE(textarea));
}

void ui_TextAreaRedo(UITextArea *textarea)
{
	ui_EditableRedo(UI_EDITABLE(textarea));
}

void ui_TextAreaSelectAll(UITextArea *textarea)
{
	ui_EditableSelectAll(UI_EDITABLE(textarea));
}

bool ui_TextAreaInput(UITextArea *textarea, KeyInput *input)
{
	bool changed = false;
	if (KIT_EditKey == input->type && ui_IsActive(UI_WIDGET(textarea)))
	{
		UI_EDITABLE(textarea)->time = 0;

		if (input->scancode == INP_BACKSPACE)
		{
			if (TextBuffer_GetSelection(UI_EDITABLE(textarea)->pBuffer, NULL, NULL, NULL))
				ui_EditableDeleteSelection(UI_EDITABLE(textarea));
			else if (input->attrib & KIA_CONTROL)
			{
				TextBuffer_DeletePreviousWord(UI_EDITABLE(textarea)->pBuffer);
				ui_EditableChanged(UI_EDITABLE(textarea));
			}
			else
			{
				TextBuffer_Backspace(UI_EDITABLE(textarea)->pBuffer);
				ui_EditableChanged(UI_EDITABLE(textarea));
			}
			UI_EDITABLE(textarea)->dirty = true;
			changed = true;
		}
		else if (input->scancode == INP_DELETE || (input->scancode == INP_DECIMAL && (input->attrib & KIA_NUMLOCK) == 0))
		{
			if (TextBuffer_GetSelection(UI_EDITABLE(textarea)->pBuffer, NULL, NULL, NULL))
				ui_EditableDeleteSelection(UI_EDITABLE(textarea));
			else if (input->attrib & KIA_CONTROL)
			{
				TextBuffer_DeleteNextWord(UI_EDITABLE(textarea)->pBuffer);
				ui_EditableChanged(UI_EDITABLE(textarea));
			}
			else
			{
				TextBuffer_Delete(UI_EDITABLE(textarea)->pBuffer);
				ui_EditableChanged(UI_EDITABLE(textarea));
			}
			UI_EDITABLE(textarea)->dirty = true;
			changed = true;
		}
		else if (input->scancode == INP_LEFT || (input->scancode == INP_NUMPAD4 && (input->attrib & KIA_NUMLOCK) == 0))
		{
			if (input->attrib & KIA_SHIFT)
			{
				if (input->attrib & KIA_CONTROL)
					TextBuffer_SelectionPreviousWord(UI_EDITABLE(textarea)->pBuffer);
				else
					TextBuffer_SelectionPrevious(UI_EDITABLE(textarea)->pBuffer);
			}
			else
			{
				if (input->attrib & KIA_CONTROL)
					TextBuffer_CursorToPreviousWord(UI_EDITABLE(textarea)->pBuffer);
				else
					TextBuffer_MoveCursor(UI_EDITABLE(textarea)->pBuffer, -1);
			}
			UI_EDITABLE(textarea)->cursorDirty = true;
		}
		else if (input->scancode == INP_RIGHT || (input->scancode == INP_NUMPAD6 && (input->attrib & KIA_NUMLOCK) == 0))
		{
			if (input->attrib & KIA_SHIFT)
			{
				if (input->attrib & KIA_CONTROL)
					TextBuffer_SelectionNextWord(UI_EDITABLE(textarea)->pBuffer);
				else
					TextBuffer_SelectionNext(UI_EDITABLE(textarea)->pBuffer);
			}
			else
			{
				if (input->attrib & KIA_CONTROL)
					TextBuffer_CursorToNextWord(UI_EDITABLE(textarea)->pBuffer);
				else
					TextBuffer_MoveCursor(UI_EDITABLE(textarea)->pBuffer, +1);
			}
			UI_EDITABLE(textarea)->cursorDirty = true;
		}
		else if (input->scancode == INP_UP || (input->scancode == INP_NUMPAD8 && (input->attrib & KIA_NUMLOCK) == 0))
		{
			S32 iCursor = ui_EditableGetCursorPosition(UI_EDITABLE(textarea));
			S32 iStart;
			S32 iEnd;
			TextBuffer_GetSelection(UI_EDITABLE(textarea)->pBuffer, NULL, &iStart, &iEnd);
			if (iStart == iEnd)
				iStart = iCursor;
			ui_TextAreaCursorUpLine(textarea);
			if (input->attrib & KIA_SHIFT)
				TextBuffer_SetSelectionBounds(UI_EDITABLE(textarea)->pBuffer, iStart, ui_EditableGetCursorPosition(UI_EDITABLE(textarea)));
			else
				TextBuffer_SetSelectionBounds(UI_EDITABLE(textarea)->pBuffer, 0, 0);
			UI_EDITABLE(textarea)->cursorDirty = true;
		}
		else if (input->scancode == INP_DOWN || (input->scancode == INP_NUMPAD2 && (input->attrib & KIA_NUMLOCK) == 0))
		{
			S32 iCursor = ui_EditableGetCursorPosition(UI_EDITABLE(textarea));
			S32 iStart;
			S32 iEnd;
			TextBuffer_GetSelection(UI_EDITABLE(textarea)->pBuffer, NULL, &iStart, &iEnd);
			if (iStart == iEnd)
				iStart = iCursor;
			ui_TextAreaCursorDownLine(textarea);
			if (input->attrib & KIA_SHIFT)
				TextBuffer_SetSelectionBounds(UI_EDITABLE(textarea)->pBuffer, iStart, ui_EditableGetCursorPosition(UI_EDITABLE(textarea)));
			else
				TextBuffer_SetSelectionBounds(UI_EDITABLE(textarea)->pBuffer, 0, 0);
			UI_EDITABLE(textarea)->cursorDirty = true;
		}
		else if (input->scancode == INP_HOME || (input->scancode == INP_NUMPAD7 && (input->attrib & KIA_NUMLOCK) == 0))
		{
			S32 iCursor = ui_EditableGetCursorPosition(UI_EDITABLE(textarea));
			S32 iStart;
			S32 iEnd;
			TextBuffer_GetSelection(UI_EDITABLE(textarea)->pBuffer, NULL, &iStart, &iEnd);
			if (iStart == iEnd)
				iStart = iCursor;
			ui_TextAreaCursorStartLine(textarea);
			if (input->attrib & KIA_SHIFT)
				TextBuffer_SetSelectionBounds(UI_EDITABLE(textarea)->pBuffer, iStart, ui_EditableGetCursorPosition(UI_EDITABLE(textarea)));
			else
				TextBuffer_SetSelectionBounds(UI_EDITABLE(textarea)->pBuffer, 0, 0);
			UI_EDITABLE(textarea)->cursorDirty = true;
		}
		else if (input->scancode == INP_END || (input->scancode == INP_NUMPAD1 && (input->attrib & KIA_NUMLOCK) == 0))
		{
			S32 iCursor = ui_EditableGetCursorPosition(UI_EDITABLE(textarea));
			S32 iStart;
			S32 iEnd;
			TextBuffer_GetSelection(UI_EDITABLE(textarea)->pBuffer, NULL, &iStart, &iEnd);
			if (iStart == iEnd)
				iStart = iCursor;
			ui_TextAreaCursorEndLine(textarea);
			if (input->attrib & KIA_SHIFT)
				TextBuffer_SetSelectionBounds(UI_EDITABLE(textarea)->pBuffer, iStart, ui_EditableGetCursorPosition(UI_EDITABLE(textarea)));
			else
				TextBuffer_SetSelectionBounds(UI_EDITABLE(textarea)->pBuffer, 0, 0);
			UI_EDITABLE(textarea)->cursorDirty = true;
		}
		else if (INP_V == input->scancode && input->attrib & KIA_CONTROL)
			ui_TextAreaPaste(textarea);
		else if (INP_C == input->scancode && input->attrib & KIA_CONTROL)
			ui_TextAreaCopy(textarea);
		else if (INP_X == input->scancode && input->attrib & KIA_CONTROL)
			ui_TextAreaCut(textarea);
		else if (INP_Z == input->scancode && input->attrib & KIA_CONTROL)
			ui_TextAreaUndo(textarea);
		else if (INP_Y == input->scancode && input->attrib & KIA_CONTROL)
			ui_TextAreaRedo(textarea);
		else if (INP_A == input->scancode && input->attrib & KIA_CONTROL)
			ui_TextAreaSelectAll(textarea);
		else if (input->scancode == INP_RETURN || input->scancode == INP_NUMPADENTER)
		{
			TextBuffer_InsertChar(UI_EDITABLE(textarea)->pBuffer, '\n');
			ui_EditableChanged(UI_EDITABLE(textarea));
			changed = true;
		}
		else
		{
			// [COR-15337] Typing 'E' or 'R' in a text field in an editor occasionally causing the editor to close.
			if(!ui_EditableInputDefault(UI_EDITABLE(textarea), input))
				return false;
		}
	}
	else if (ui_IsActive(UI_WIDGET(textarea)) && KIT_Text == input->type && !(input->attrib & KIA_CONTROL))
	{
		ui_EditableInsertCodepoint(UI_EDITABLE(textarea), input->character);
		changed = true;
	}

	if (changed)
	{
		TextBuffer_SelectionClear(UI_EDITABLE(textarea)->pBuffer);
		UI_EDITABLE(textarea)->selecting = false;
		UI_EDITABLE(textarea)->cursorDirty = true;
		UI_EDITABLE(textarea)->dirty = true;
	}

	return true;
}


void ui_TextAreaFocus(UITextArea *textarea, UIAnyWidget *focusitem)
{
	// Since there may be default text (now disappearing), we should make this work.
	UI_EDITABLE(textarea)->cursorDirty = true;
	UI_EDITABLE(textarea)->dirty = true;
	ui_StartDeviceKeyboardTextInput();
}

static F32 ui_TextAreaCounterScale( F32 scale )
{
	return 0.7 * scale;
}

static void ui_TextAreaFillCounterBox( UITextArea* textarea, UI_MY_ARGS, CBox* out_counterBox )
{
	const F32 counterScale = ui_TextAreaCounterScale( scale );
	const F32 counterHeight = gfxfont_FontHeight(g_font_Active, counterScale);
	const int maxTextLength = TextBuffer_GetMaxLength( textarea->editable.pBuffer );

	if( h > counterHeight * 4 && maxTextLength > 0 ) {
		BuildCBox( out_counterBox, x, y, w - ui_ScrollbarWidth(UI_WIDGET(textarea)->sb), counterHeight );
	}
}

U32 ui_TextAreaMouseToCursor(UITextArea *textarea, F32 x, F32 y, F32 w, F32 h, F32 scale, F32 lineHeight, bool bDownPos)
{
	UIStyleFont *font = ui_TextAreaGetFont(textarea);
	S32 xOff = g_ui_State.mouseX - x;
	S32 yOff;
	F32 textWidth;
	S32 row;
	S32 length, i, newPos = 0;
	unsigned char *tmp = NULL;
	const unsigned char *start;
	
	yOff = g_ui_State.mouseY - y;;
	row = MAX(0, MIN(eaSize(&textarea->lines) - 1, yOff / lineHeight));

	if (bDownPos)
	{
		mouseDownPos(MS_LEFT, &xOff, &yOff);
		xOff -= x;
		yOff -= y;
	}

	if (eaSize(&textarea->lines) <= 0 || textarea->usingDefault)
		return 0;
	start = textarea->lines[row];

	if (row == eaSize(&textarea->lines) - 1)
		length = (S32)strlen(start);
	else
		length = textarea->lines[row + 1] - start;

	estrStackCreate(&tmp);
	estrConcat(&tmp, start, length);
	textWidth = ui_StyleFontWidth(font, scale, tmp);

	for (i = 0; i < length; i += UTF8GetCodepointLength(tmp + i))
	{
		F32 preCursorWidth, postCursorWidth;
		int drawCursorX;
		postCursorWidth = ui_StyleFontWidth(font, scale, tmp + i);
		preCursorWidth = textWidth - postCursorWidth;
		drawCursorX = round(preCursorWidth);

		if (drawCursorX >= xOff)
			break;
	}

	estrDestroy(&tmp);

	return UTF8PointerToCodepointIndex(ui_TextAreaGetText(textarea), start + i);
}
void ui_TextAreaCursorToMouseWordSelect(UITextArea *textarea, F32 x, F32 y, F32 w, F32 h, F32 scale, F32 lineHeight)
{
	U32 pos = ui_TextAreaMouseToCursor(textarea, x, y, w, h, scale, lineHeight, true);
	const char *pchText = ui_TextAreaGetText(textarea);
	U32 previous = UTF8CodepointOfPreviousWord(pchText, pos);
	U32 next = UTF8CodepointOfNextWord(pchText, pos);
	ui_EditableSetSelection(UI_EDITABLE(textarea), previous, next - previous);
}

static void ui_TextAreaSelectionFillDrawingDescription( UITextArea* textarea, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( textarea );
		
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

static void ui_TextAreaFillDrawingDescription( UITextArea* textarea, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( textarea );
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName;
		if (ui_IsFocused(UI_WIDGET(textarea))) {
			if (!ui_IsActive(UI_WIDGET(textarea))) {
				descName = skin->astrTextEntryStyle;
			} else {
				descName = skin->astrTextEntryStyleHighlight;
			}
		} else if (!ui_IsActive(UI_WIDGET(textarea))) {
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

static UIStyleFont* ui_TextAreaGetFont( UITextArea* textarea )
{
	UISkin* skin = UI_GET_SKIN( textarea );
	UIStyleFont* font = NULL;
	
	if( textarea->usingDefault ) {
		font = GET_REF( skin->hTextEntryDefaultTextFont );
	} else if( ui_IsActive( UI_WIDGET( textarea ))){
		font = GET_REF( skin->hTextEntryTextFont );
	} else {
		font = GET_REF( skin->hTextEntryDisabledFont );
	}

	if (!font) {
		font = ui_WidgetGetFont( UI_WIDGET( textarea ));
	}

	return font;
}

static UIStyleFont* ui_TextAreaGetHighlightFont( UITextArea* textarea )
{
	UISkin* skin = UI_GET_SKIN( textarea );
	UIStyleFont* font = NULL;
	
	font = GET_REF( skin->hTextEntryHighlightTextFont );

	if (!font) {
		font = ui_WidgetGetFont( UI_WIDGET( textarea ));
	}

	return font;
}

static UIStyleFont* ui_TextAreaGetCharCountFont(UITextArea* textarea)
{
	UISkin* skin = UI_GET_SKIN( textarea );
	UIStyleFont* font = NULL;
	
	if( ui_IsActive( UI_WIDGET( textarea ))){
		font = GET_REF( skin->hTextEntryTextFont );
	} else {
		font = GET_REF( skin->hTextEntryDisabledFont );
	}

	if (!font) {
		font = ui_WidgetGetFont( UI_WIDGET( textarea ));
	}

	return font;
}



void ui_TextAreaTick(UITextArea *textarea, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(textarea);
	UIStyleFont *font = ui_TextAreaGetFont(textarea);
	F32 lineHeight = ui_StyleFontLineHeight(font, scale);
	F32 totalHeight = eaSize(&textarea->lines) * lineHeight + lineHeight/2;
	AtlasTex *plus = (g_ui_Tex.plus);
	CBox counterBox = { 0 };
	F32 wIncludingPlus;
	F32 realW;
	
	PERFINFO_AUTO_START_FUNC();

	UI_EDITABLE(textarea)->time += g_ui_State.timestep;
	if (UI_EDITABLE(textarea)->time > 1)
		UI_EDITABLE(textarea)->time = 0;
	
	if (textarea->smfEditTabs)
	{
		int xIt = x + w;
		int it;
		bool isActive = ui_IsFocused(textarea) || ui_IsFocusedWidgetGroup(&textarea->smfEditWidgets);
		
		ui_TextAreaSetSMFEditActive(textarea, isActive);
		for( it = 0; it != eaSize( &textarea->smfEditWidgets ); ++it ) {
			UIWidget* widget = textarea->smfEditWidgets[ it ];
			ui_WidgetSetPosition( widget, xIt, y );
			ui_WidgetSetHeight( widget, h );
			xIt += widget->width + UI_STEP_SC;
		}
	}

	realW = w;
	if (UI_WIDGET(textarea)->sb->scrollY)
		w -= ui_ScrollbarWidth(UI_WIDGET(textarea)->sb) * scale;

	if (textarea->collapse && !(ui_IsFocused(textarea) || ui_IsFocusedWidgetGroup(&textarea->smfEditWidgets)))
	{
		F32 visibleW = w;
		
		if( textarea->collapseHeight ) {
			h = textarea->collapseHeight * scale;
		} else {
			h = (ui_StyleFontLineHeight(font, scale) + UI_STEP) * scale;
		}
		w = realW;
		BuildCBox(&box, x, y, w, h);
		UI_TICK_EARLY(textarea, false, false);
		if(ui_IsActive(UI_WIDGET(textarea)))
		{
			if (mouseDownHit(MS_LEFT, &box) || mouseDownHit(MS_RIGHT, &box))
				ui_SetFocus(textarea);
			if (mouseCollision(&box))
			{
				ui_SetCursorByName(ui_GetTextCursor(UI_WIDGET(textarea)));
				ui_CursorLock();
			}
		}

		if (UI_EDITABLE(textarea)->dirty)
			ui_TextAreaReflow(textarea, textarea->wordWrap ? visibleW - UI_DSTEP_SC : FLT_MAX, scale);
		
		PERFINFO_AUTO_STOP();
		return;
	}
	ui_TextAreaFillCounterBox( textarea, UI_MY_VALUES, &counterBox );
	if( CBoxHeight( &counterBox ) > 0 ) {
		y += CBoxHeight( &counterBox );
		h -= CBoxHeight( &counterBox );
	}

	BuildCBox(&box, x, y, w, h);

	ui_ScrollbarTick(textarea->widget.sb, x, y, w, h, z, scale, w, totalHeight);

	UI_TICK_EARLY(textarea, false, false);

	wIncludingPlus = w;
	if (textarea->smf)
	{
		w -= scale * (plus->width + UI_HSTEP);
		BuildCBox(&box, x, y, w, h);
		CBoxClipTo(&pBox, &box);
	}
	
	// If we're uncollapsed but collapsable, ignore clipping.
	if (textarea->collapse)
	{
		mouseClipPushRestrict(NULL);
		BuildCBox(&box, x, y, w, h);
	}

	if (UI_EDITABLE(textarea)->cursorDirty)
	{
		S32 visibleTop = (textarea->widget.sb->ypos + lineHeight - 1) / lineHeight;
		S32 visibleBottom = (textarea->widget.sb->ypos + h - lineHeight) / lineHeight;
		S32 cursorCurrent = ui_TextAreaCursorGetLine(textarea);

		if (visibleTop > cursorCurrent)
			textarea->widget.sb->ypos = cursorCurrent * lineHeight;
		else if (visibleBottom < cursorCurrent)
			textarea->widget.sb->ypos = (cursorCurrent + 1) * lineHeight - (h - UI_STEP_SC);

		UI_EDITABLE(textarea)->cursorDirty = false;
	}

	if (ui_IsActive(UI_WIDGET(textarea)))
	{
		bool mouseDownHitRight = mouseDownHit(MS_RIGHT, &box);
		
		if (mouseCollision(&box))
		{
			ui_SetCursorByName(ui_GetTextCursor(UI_WIDGET(textarea)));
			ui_CursorLock();
		}

		if (mouseDragHit(MS_LEFT, &box))
		{
			S32 iStart = ui_TextAreaMouseToCursor(textarea, x + UI_HSTEP_SC, y - textarea->widget.sb->ypos, w, h, scale, lineHeight, true);
			ui_SetFocus(textarea);
			UI_EDITABLE(textarea)->selecting = true;
			UI_EDITABLE(textarea)->time = 0;

			if(inpLevelPeek(INP_SHIFT))
				ui_EditableSelectToPos(UI_EDITABLE(textarea), iStart);
			else
				ui_EditableSetSelection(UI_EDITABLE(textarea), iStart, 0);
			UI_EDITABLE(textarea)->cursorDirty = true;
			inpHandled();
		}
		else if (UI_EDITABLE(textarea)->selecting)
		{
			S32 iEnd = ui_TextAreaMouseToCursor(textarea, x + UI_HSTEP_SC, y - textarea->widget.sb->ypos, w, h, scale, lineHeight, false);
			S32 iStart;
			UI_EDITABLE(textarea)->time = 0;
			TextBuffer_GetSelection(UI_EDITABLE(textarea)->pBuffer, NULL, &iStart, NULL);
			ui_EditableSetSelection(UI_EDITABLE(textarea), iStart, iEnd - iStart);
			if (!mouseIsDown(MS_LEFT))
				UI_EDITABLE(textarea)->selecting = false;
			UI_EDITABLE(textarea)->cursorDirty = true;
			inpHandled();
		}
		else if (mouseDoubleClickHit(MS_LEFT,&box))
		{
			UI_EDITABLE(textarea)->time = 0;
			ui_SetFocus(textarea);
			ui_TextAreaCursorToMouseWordSelect(textarea, x + UI_HSTEP_SC, y - textarea->widget.sb->ypos, w, h, scale, lineHeight);
			inpHandled();
		}
		else if (mouseDownHit(MS_LEFT, &box) || mouseDownHitRight)
		{
			bool hasSelection = TextBuffer_GetSelection( UI_EDITABLE(textarea)->pBuffer, NULL, NULL, NULL );
				
			UI_EDITABLE(textarea)->time = 0;
			ui_SetFocus(textarea);

			if (!mouseDownHitRight || !hasSelection)
			{
				if (inpLevelPeek(INP_SHIFT))
				{
					S32 iCursor = ui_EditableGetCursorPosition(UI_EDITABLE(textarea));
					S32 iStart;
					S32 iEnd;
					TextBuffer_GetSelection(UI_EDITABLE(textarea)->pBuffer, NULL, &iStart, &iEnd);
					if (!(iStart || iEnd))
						iStart = iCursor;
					iEnd = ui_TextAreaMouseToCursor(textarea, x + UI_HSTEP_SC, y - textarea->widget.sb->ypos, w, h, scale, lineHeight, true);
					ui_EditableSetSelection(UI_EDITABLE(textarea), iStart, iEnd - iStart);
				}
				else
					ui_EditableSetSelection(UI_EDITABLE(textarea), 0, 0);
				
				ui_TextAreaSetCursorPosition(textarea,
											 ui_TextAreaMouseToCursor(textarea, x + UI_HSTEP_SC, y - textarea->widget.sb->ypos, w, h, scale, lineHeight, false));
			}

			UI_EDITABLE(textarea)->cursorDirty = true;

			if (mouseDownHit(MS_RIGHT, &box) && !UI_WIDGET(textarea)->contextF)
			{
				const char* clipboard = TextBuffer_GetClipboard( UI_EDITABLE(textarea)->pBuffer );
				s_textAreaMenuArea = textarea;
				s_textAreaItemCut->active = hasSelection;
				s_textAreaItemCopy->active = hasSelection;
				s_textAreaItemPaste->active = !nullStr( clipboard );
				ui_MenuPopupAtCursor(s_textAreaMenu);
			}
			
			inpHandled();
		}

		if (mouseCollision(&box))
			inpHandled();
	}

	if (textarea->collapse)
		mouseClipPop();

	if (textarea->smf && ui_IsActive(UI_WIDGET(textarea)))
	{
		CBox tempBox;
		BuildCBox(&tempBox, x + w, y, (plus->width + UI_HSTEP) * scale, h);
		CBoxClipTo(&pBox, &tempBox);
		if (mouseDownHit(MS_LEFT, &tempBox))
		{
			ui_SetFocus(textarea);
			ui_TextAreaSetPopupSMFOpen(textarea, !textarea->smfOpened);
			inpHandled();
		}
		if (textarea->smfOpened)
		{
			F32 popupWidth = wIncludingPlus;
			ui_WidgetSetPosition(UI_WIDGET(textarea->smf), x/scale, (y+h)/scale - 1);
			ui_WidgetSetWidth(UI_WIDGET(textarea->smf), popupWidth/scale);
			if( ui_WidgetGetHeight( UI_WIDGET( textarea->smf )) < 15 ) {
				ui_WidgetSetHeight( UI_WIDGET( textarea->smf ), 15 );
			}
			UI_WIDGET(textarea->smf)->scale = scale / g_ui_State.scale;

			// Make sure changed/inherited flags move into text area
			ui_SetChanged(UI_WIDGET(textarea->smf), ui_IsChanged(UI_WIDGET(textarea)));
			ui_SetInherited(UI_WIDGET(textarea->smf), ui_IsInherited(UI_WIDGET(textarea)));
		}
	}
	
	if(	(textarea->wordWrap && w - UI_DSTEP_SC != textarea->lastWidth)
		|| textarea->wordWrap != textarea->lastWordWrap || UI_EDITABLE(textarea)->dirty)
	{
		ui_TextAreaReflow(textarea, textarea->wordWrap ? w - UI_DSTEP_SC : FLT_MAX, scale);
		textarea->lastWordWrap = textarea->wordWrap;
	}
	
	PERFINFO_AUTO_STOP();
}

static void DrawCursor(UITextArea *textarea, F32 x, F32 y, F32 z, F32 h, F32 scale, const unsigned char *text, const unsigned char *cursor)
{
	if (ui_IsActive(UI_WIDGET(textarea)) && ui_IsFocused(textarea) && UI_EDITABLE(textarea)->time < 0.5)
	{
		UIStyleFont *font = ui_TextAreaGetFont(textarea);
		F32 drawX;
		unsigned char *pre = NULL;
		estrStackCreate(&pre);
		estrConcat(&pre, text, cursor - text);
		drawX = x + ui_StyleFontWidth(font, scale, pre);
		gfxDrawLine(drawX, y, z + 0.02f, drawX, y + h, ui_StyleFontGetColor(font));
		estrDestroy(&pre);
	}
}

static void ui_TextAreaDrawSelection(UITextArea *textarea, UI_MY_ARGS, F32 z, F32 lineHeight)
{
	UIDrawingDescription desc = { 0 };
	UIStyleFont *font = ui_TextAreaGetFont(textarea);
	Color selection = {0, 0, 0, 0x22};
	unsigned char *start;
	unsigned char *end;
	unsigned char *tmp = NULL;
	CBox box;
	int i;
	U32 iStart, iEnd;

	ui_TextAreaSelectionFillDrawingDescription( textarea, &desc );

	TextBuffer_GetSelection(UI_EDITABLE(textarea)->pBuffer, NULL, &iStart, &iEnd);
	start = UTF8GetCodepoint(ui_TextAreaGetText(textarea), iStart);
	end = UTF8GetCodepoint(ui_TextAreaGetText(textarea), iEnd);

	if (start == end)
		return;
	else if (start > end)
	{
		tmp = start;
		start = end;
		end = tmp;
	}

	y -= textarea->widget.sb->ypos;

	estrStackCreate(&tmp);
	for (i = 0; i < eaSize(&textarea->lines); i++)
	{
		bool atEnd = (i == eaSize(&textarea->lines) - 1);
		if (textarea->lines[i] >= start && (!atEnd && textarea->lines[i + 1] <= end))
		{
			// Line is entirely within the selection.
			BuildCBox(&box, x + UI_HSTEP * scale, y + lineHeight * i, w - (UI_STEP * scale), lineHeight);
			ui_DrawingDescriptionDraw( &desc, &box, scale, z + 0.1, 255, selection, ColorBlack );
		}
		else if (start >= textarea->lines[i] && (atEnd || end < textarea->lines[i + 1]))
		{
			// Selection is entirely within the line.
			F32 width, leftPad;

			estrConcat(&tmp, start, end - start);
			width = ui_StyleFontWidth(font, scale, tmp);
			estrClear(&tmp);
			estrConcat(&tmp, textarea->lines[i], start - textarea->lines[i]);
			leftPad = x + UI_HSTEP * scale + ui_StyleFontWidth(font, scale, tmp);

			BuildCBox(&box, leftPad, y + lineHeight * i, width, lineHeight);
			ui_DrawingDescriptionDraw( &desc, &box, scale, z + 0.1, 255, selection, ColorBlack );
		}
		else if (start > textarea->lines[i] && (atEnd || start < textarea->lines[i + 1]))
		{
			// Selection starts on this line.
			F32 leftPad;
			estrConcat(&tmp, textarea->lines[i], start - textarea->lines[i]);
			leftPad = x + UI_HSTEP * scale + ui_StyleFontWidth(font, scale, tmp);
			BuildCBox(&box, leftPad, y + lineHeight * i, x + w - (leftPad + UI_HSTEP), lineHeight);
			ui_DrawingDescriptionDraw( &desc, &box, scale, z + 0.1, 255, selection, ColorBlack );
		}
		else if (end > textarea->lines[i] && start < textarea->lines[i])
		{
			// Selection ends on this line.
			F32 width;
			estrConcat(&tmp, textarea->lines[i], end - textarea->lines[i]);
			width = ui_StyleFontWidth(font, scale, tmp);
			BuildCBox(&box, x + UI_HSTEP * scale, y + lineHeight * i, width, lineHeight);
			ui_DrawingDescriptionDraw( &desc, &box, scale, z + 0.1, 255, selection, ColorBlack );
		}
		estrClear(&tmp);
	}
	estrDestroy(&tmp);
}

static void ui_TextAreaDrawLines( UITextArea* textarea, int numLines, int yOff, UI_MY_ARGS, float z )
{
	UIStyleFont *font = ui_TextAreaGetFont(textarea);
	UIStyleFont *highlightFont = ui_TextAreaGetHighlightFont(textarea);
	UIStyleFont *charCountFont = ui_TextAreaGetCharCountFont(textarea);
	F32 lineHeight = ui_StyleFontLineHeight(font, scale);
	char* cursor = UTF8GetCodepoint(ui_TextAreaGetText(textarea), ui_EditableGetCursorPosition(UI_EDITABLE(textarea)));
	
	int selectionStart;
	int selectionEnd;
	int selectionFirst;
	int selectionLast;
	const char* textSelectionFirst = NULL;
	const char* textSelectionLast = NULL;
	int i;
		
	TextBuffer_GetSelection(UI_EDITABLE(textarea)->pBuffer, NULL, &selectionStart, &selectionEnd);
	if( selectionStart != selectionEnd ) {
		const char* text = ui_TextAreaGetText( textarea );
		selectionFirst = MIN( selectionStart, selectionEnd );
		selectionLast = MAX( selectionStart, selectionEnd );
		textSelectionFirst = UTF8GetCodepoint( text, selectionFirst );
		textSelectionLast = UTF8GetCodepoint( text, selectionLast );
	}

	for (i = 0; i < eaSize(&textarea->lines); i++)
	{
		float xPos = x + UI_HSTEP * scale;
		float yPos = yOff + i * lineHeight;
		devassert(textarea->lines);
		// FIXME: If this is still too slow we can cull the drawing even
		// earlier by starting i at a larger value.
		if (yPos > y - lineHeight && yPos < y + h + lineHeight)
		{
			const char* lineStart = textarea->lines[i];
			const char* lineEnd;
			const char* lineEndForCursor;
			int lineLength;

			if(i + 1 < eaSize(&textarea->lines))
			{
				int offset = 0;
				if (*(textarea->lines[i + 1] - 1) == '\n')
					offset = 1;
				lineEnd = textarea->lines[i + 1] - offset;
				lineEndForCursor = textarea->lines[i + 1];
			}
			else
			{
				lineEnd = lineStart + strlen(lineStart);
				lineEndForCursor = lineEnd + 1;
			}

			lineLength = UTF8PointerToCodepointIndex(lineStart, lineEnd);
			
			if (!textarea->usingDefault && cursor >= lineStart && cursor < lineEndForCursor)
				DrawCursor(textarea, xPos, yPos - lineHeight, z, lineHeight, scale, lineStart, cursor);
			if( selectionFirst != selectionLast ) {
				const char* textSelectionFirstThisLine = CLAMP( textSelectionFirst, lineStart, lineEnd );
				const char* textSelectionLastThisLine = CLAMP( textSelectionLast, lineStart, lineEnd );
				int selectionFirstThisLine = UTF8PointerToCodepointIndex( lineStart, textSelectionFirstThisLine );
				int selectionLastThisLine = UTF8PointerToCodepointIndex( lineStart, textSelectionLastThisLine );

				ui_StyleFontUse(font, false, UI_WIDGET(textarea)->state);
				gfxfont_PrintEx(g_font_Active, xPos, yPos, z + 0.2f, scale, scale, 0, lineStart, selectionFirstThisLine, 0, NULL);
				gfxfont_Dimensions(g_font_Active, scale, scale, lineStart, selectionFirstThisLine, NULL, NULL, &xPos, false);

				ui_StyleFontUse(highlightFont, false, UI_WIDGET(textarea)->state);
				gfxfont_PrintEx(g_font_Active, xPos, yPos, z + 0.2f, scale, scale, 0, textSelectionFirstThisLine, selectionLastThisLine - selectionFirstThisLine, 0, NULL);
				gfxfont_Dimensions(g_font_Active, scale, scale, textSelectionFirstThisLine, selectionLastThisLine - selectionFirstThisLine, NULL, NULL, &xPos, false);

				ui_StyleFontUse(font, false, UI_WIDGET(textarea)->state);
				gfxfont_PrintEx(g_font_Active, xPos, yPos, z + 0.2f, scale, scale, 0, textSelectionLastThisLine, lineLength - selectionLastThisLine, 0, NULL);
			} else {
				ui_StyleFontUse(font, false, UI_WIDGET(textarea)->state);
				gfxfont_PrintEx(g_font_Active, xPos, yPos, z + 0.2, scale, scale, 0, lineStart, lineLength, 0, NULL);
			}
		}
	}
}

void ui_TextAreaDraw(UITextArea *textarea, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(textarea);
	UIDrawingDescription desc = { 0 };
	AtlasTex *plus = (g_ui_Tex.plus);
	UIStyleFont *font = ui_TextAreaGetFont(textarea);
	UIStyleFont *charCountFont = ui_TextAreaGetCharCountFont(textarea);
	F32 lineHeight = ui_StyleFontLineHeight(font, scale);
	F32 xIndent;
	F32 yOff;
	const int curTextLength = UTF8GetLength( ui_TextAreaGetText( textarea ));
	const int maxTextLength = TextBuffer_GetMaxLength( textarea->editable.pBuffer );
	CBox counterBox = { 0 };
	
	Color c, border;

	ui_TextAreaFillDrawingDescription( textarea, &desc );

	if (UI_GET_SKIN(textarea))
	{
		if (ui_IsFocused(UI_WIDGET(textarea)))
		{
			if (ui_IsChanged(UI_WIDGET(textarea)))
				c = ColorLerp(UI_GET_SKIN(textarea)->entry[1], UI_GET_SKIN(textarea)->entry[3], 0.5);
			else if (ui_IsInherited(UI_WIDGET(textarea)))
				c = ColorLerp(UI_GET_SKIN(textarea)->entry[1], UI_GET_SKIN(textarea)->entry[4], 0.5);
			else
				c = UI_GET_SKIN(textarea)->entry[1];
		}
		else if (!ui_IsActive(UI_WIDGET(textarea)))
			c = UI_GET_SKIN(textarea)->entry[2];
		else if (ui_IsChanged(UI_WIDGET(textarea)))
			c = UI_GET_SKIN(textarea)->entry[3];
		else if (ui_IsInherited(UI_WIDGET(textarea)))
			c = UI_GET_SKIN(textarea)->entry[4];
		else
			c = UI_GET_SKIN(textarea)->entry[0];
		border = UI_GET_SKIN(textarea)->thinBorder[0];
	}
	else
	{
		c = textarea->widget.color[0];
		border = ColorBlack;
	}

	if (textarea->collapse && !(ui_IsFocused(textarea) || ui_IsFocusedWidgetGroup(&textarea->smfEditWidgets)))
	{
		if( textarea->collapseHeight ) {
			h = textarea->collapseHeight * scale;
		} else {
			h = lineHeight + UI_STEP * scale;
		}
		BuildCBox(&box, x, y, w, h);
		clipperPushRestrict(&box);
		ui_DrawingDescriptionDraw( &desc, &box, scale, z, 255, c, border );

		ui_TextAreaDrawLines(textarea, MIN( eaSize(&textarea->lines),
											textarea->collapseHeight / (ui_StyleFontLineHeight(font, scale) + UI_STEP) + 1),
							 y + lineHeight, UI_MY_VALUES, z );
		
		clipperPop();
		return;
	}
	ui_TextAreaFillCounterBox( textarea, UI_MY_VALUES, &counterBox );

	BuildCBox(&box, x, y, w, h);
	if (UI_WIDGET(textarea)->sb->scrollY) {
		w -= ui_ScrollbarWidth(UI_WIDGET(textarea)->sb) * scale;
	}

	if (textarea->collapse)
	{
		clipperPush(&box);
		z = UI_TOP_Z;
	}
	else
	{
		CBoxClipTo(&pBox, &box);
		clipperPushRestrict(&box);
	}

	ui_DrawingDescriptionDraw( &desc, &box, scale, z, 255, c, border );
	if (textarea->smf && ui_IsActive(UI_WIDGET(textarea)))
	{
		display_sprite(plus, x + w - scale * (plus->width + UI_HSTEP), y + scale * 5,
			z + 0.2f, scale, scale, RGBAFromColor(c));
	}
	if( CBoxHeight( &counterBox ) > 0 ) {
		y += CBoxHeight( &counterBox );
		h -= CBoxHeight( &counterBox );
	}
	
	// If there's nothing to draw or the text has not reflowed this frame
	// (meaning the ->lines array may contain invalid pointers), don't draw.
	if (eaSize(&textarea->lines) <= 0 || UI_EDITABLE(textarea)->dirty)
	{
		clipperPop();
		return;
	}

	ui_StyleFontUse(font, false, UI_WIDGET(textarea)->state);

	xIndent = w*.1;
	yOff = y + lineHeight - textarea->widget.sb->ypos;

	ui_TextAreaDrawLines( textarea, -1, yOff, UI_MY_VALUES, z );

	ui_TextAreaDrawSelection(textarea, UI_MY_VALUES, z, lineHeight);
	
	clipperPop();

	if (textarea->collapse)
		clipperPush(NULL);
	ui_ScrollbarDraw(textarea->widget.sb, x, y, w, h, z + 0.5, scale, w, eaSize(&textarea->lines) * lineHeight + lineHeight/2);
	if( CBoxHeight( &counterBox ) > 0 && maxTextLength > 0 ) {
		F32 centerX;
		F32 centerY;
		F32 textWidth;
		char buffer[ 256 ];
		float counterScale = ui_TextAreaCounterScale( scale );
		CBoxGetCenter( &counterBox, &centerX, &centerY );
		sprintf( buffer, "%d / %d", curTextLength, maxTextLength );
		ui_StyleFontUse(charCountFont, false, UI_WIDGET(textarea)->state);
		gfxfont_Dimensions(g_font_Active, counterScale, counterScale, buffer, UTF8GetLength( buffer ), &textWidth, NULL, NULL, true);
		gfxfont_Print( counterBox.hx - textWidth, centerY, z + 0.5, counterScale, counterScale, CENTER_Y, buffer );
	}
	
	if (textarea->collapse)
		clipperPop();
}

void ui_TextAreaUnfocus(UITextArea *textarea, UIAnyWidget *focusitem)
{
	ui_StopDeviceKeyboardTextInput();
	if( textarea->trimWhitespace )
		TextBuffer_TrimLeadingAndTrailingWhitespace(textarea->editable.pBuffer);
	if (textarea->finishedF)
		textarea->finishedF(textarea, textarea->finishedData);

	// If default text is now reappearing, we need to make this work
	if( nullStr(ui_TextAreaGetText( textarea ))) {
		UI_EDITABLE(textarea)->cursorDirty = true;
		UI_EDITABLE(textarea)->dirty = true;
	}
}

void ui_TextAreaReflow(UITextArea *textarea, F32 width, F32 scale)
{
	UIStyleFont *font;
	const unsigned char *lineStart = ui_TextAreaGetText(textarea);
	const unsigned char *bestEnd = NULL;
	const unsigned char *secondBestEnd = NULL;
	const unsigned char *checkEnd = NULL;
	S32 iLength = 0;
	F32 fWidth = 0.f;
	
	PERFINFO_AUTO_START_FUNC();

	eaSetSize(&textarea->lines, 0);

	checkEnd = ui_TextAreaGetText(textarea);
	textarea->usingDefault = false;
	if (nullStr(checkEnd) && !ui_IsFocusedOrChildren(textarea) && !ui_IsFocusedWidgetGroup(&textarea->smfEditWidgets)) {
		checkEnd = ui_EditableGetDefault(UI_EDITABLE(textarea));
		textarea->usingDefault = true;
	}
	if (nullStr(checkEnd) && !ui_IsFocusedOrChildren(textarea) && !ui_IsFocusedWidgetGroup(&textarea->smfEditWidgets)) {
		checkEnd = "";
		textarea->usingDefault = true;
	}

	// Have to use the font *after* the text area knows if it is using default text or not
	font = ui_TextAreaGetFont(textarea);
	ui_StyleFontUse(font, false, UI_WIDGET(textarea)->state);
	
	lineStart = checkEnd;

	if(textarea->usingDefault) {
		width *= 0.8;
	}
	
	for (; *checkEnd; checkEnd = UTF8GetNextCodepoint(checkEnd))
	{
		switch (*checkEnd)
		{
		case '\n':
			eaPush(&textarea->lines, lineStart);
			lineStart = UTF8GetNextCodepoint(checkEnd);
			bestEnd = NULL;
			secondBestEnd = NULL;
			iLength = 0;
			fWidth = 0;
			break;
		case ' ':
			bestEnd = checkEnd;
		default:
			iLength++;
			// Optimization to avoid UTF8GetLength calls.
			fWidth += ttGetGlyphWidth(g_font_Active, UTF8ToWideCharConvert(checkEnd), scale, scale);
			if (fWidth >= width)
			{
				if (!bestEnd)
					bestEnd = secondBestEnd;
				if (!bestEnd)
					break;
				eaPush(&textarea->lines, lineStart);
				lineStart = UTF8GetNextCodepoint(bestEnd);
				bestEnd = NULL;
				secondBestEnd = NULL;
				checkEnd = lineStart;
				iLength = 0;
				fWidth = 0;
			}
			secondBestEnd = checkEnd;
			break;
		}
	}

	eaPush(&textarea->lines, lineStart);
	textarea->lastWidth = width;
	UI_EDITABLE(textarea)->dirty = false;
	
	PERFINFO_AUTO_STOP();
}

S32 ui_TextAreaCursorGetLine(UITextArea *textarea)
{
	int i = 0;
	unsigned char *cursor = UTF8GetCodepoint(ui_TextAreaGetText(textarea), ui_EditableGetCursorPosition(UI_EDITABLE(textarea)));
	for (i = 0; i < eaSize(&textarea->lines) - 1; i++)
		if (cursor < textarea->lines[i + 1])
			return i;
	return i;
}

S32 ui_TextAreaGetLines(UITextArea *textarea)
{
	return eaSize(&textarea->lines);
}

void ui_TextAreaCursorUpLine(UITextArea *textarea)
{
	S32 line = ui_TextAreaCursorGetLine(textarea);
	S32 newLine = CLAMP(line - 1, 0, ui_TextAreaGetLines(textarea) - 1);
	S32 newPos = MAX(0, UTF8PointerToCodepointIndex(ui_TextAreaGetText(textarea), textarea->lines[newLine]));
	ui_TextAreaSetCursorPosition(textarea, newPos);
}

void ui_TextAreaCursorDownLine(UITextArea *textarea)
{
	S32 line = ui_TextAreaCursorGetLine(textarea);
	S32 newLine = CLAMP(line + 1, 0, ui_TextAreaGetLines(textarea) - 1);
	S32 newPos = MAX(0, UTF8PointerToCodepointIndex(ui_TextAreaGetText(textarea), textarea->lines[newLine]));
	ui_TextAreaSetCursorPosition(textarea, newPos);
}

void ui_TextAreaCursorStartLine(UITextArea *textarea)
{
	S32 line = ui_TextAreaCursorGetLine(textarea);
	ui_TextAreaSetCursorPosition(textarea, UTF8PointerToCodepointIndex(ui_TextAreaGetText(textarea), textarea->lines[line]));
}

void ui_TextAreaCursorEndLine(UITextArea *textarea)
{
	S32 line = ui_TextAreaCursorGetLine(textarea);
	S32 newLine = line + 1;
	if (newLine >= ui_TextAreaGetLines(textarea))
		ui_TextAreaSetCursorPosition(textarea, UTF8GetLength(ui_TextAreaGetText(textarea)));
	else
	{
		S32 newPos = MAX(0, UTF8PointerToCodepointIndex(ui_TextAreaGetText(textarea), textarea->lines[newLine]) - 1);
		ui_TextAreaSetCursorPosition(textarea, newPos);
	}
}

void ui_TextAreaSetCollapse(UITextArea *textarea, bool collapse)
{
	textarea->collapse = collapse;
	UI_EDITABLE(textarea)->dirty = true;
}

void ui_TextAreaSetCollapseHeight(SA_PARAM_NN_VALID UITextArea *textarea, int collapseHeight)
{
	textarea->collapseHeight = collapseHeight;
	UI_EDITABLE(textarea)->dirty = true;
}

void ui_TextAreaSetWordWrap(SA_PARAM_NN_VALID UITextArea *textarea, bool enabled)
{
	textarea->wordWrap = enabled;
}

static void ui_TextEntryInsertTextSurroundingSelection( UITextArea* area, const char* beginStr, const char* endStr )
{
	TextBuffer* buffer = UI_EDITABLE( area )->pBuffer;
	int selectionStart;
	int selectionEnd;

	if( !beginStr ) {
		beginStr = "";
	}
	if( !endStr ) {
		endStr = "";
	}

	if( TextBuffer_GetSelection( buffer, NULL, &selectionStart, &selectionEnd )) {
		int begin = MIN(selectionStart, selectionEnd);
		int end = MAX(selectionStart, selectionEnd);

		TextBuffer_Checkpoint( buffer );
		ui_TextAreaInsertTextAt( area, end, endStr );
		ui_TextAreaInsertTextAt( area, begin, beginStr );
		ui_EditableSetSelection( UI_EDITABLE( area ), begin + UTF8GetLength( beginStr ), end - begin );
	} else {
		int cursor = TextBuffer_GetCursor( buffer );
		TextBuffer_Checkpoint( buffer );
		ui_TextAreaInsertTextAt( area, cursor, endStr );
		ui_TextAreaInsertTextAt( area, cursor, beginStr );
		ui_TextAreaSetCursorPosition( area, cursor + UTF8GetLength( beginStr ));
	}
}

static void ui_TextAreaSMFShortuct( UIButton* button, UserData rawShortcut )
{
	UITextShortcut* shortcut = rawShortcut;
	UITextArea* textarea = (UITextArea*)(uintptr_t)button->widget.u64;

	ui_TextEntryInsertTextSurroundingSelection( textarea, shortcut->beforeRegionText, shortcut->afterRegionText );
}

static void ui_TextEntrySMFAreaClose( UIButton* ignored, UserData ignored2 )
{
	ui_SetFocus( NULL );
}

void ui_TextAreaSetSMFEdit(UITextArea *textarea, UITextShortcutTab** smfEditTabs)
{
	if (!smfEditTabs)
		ui_TextAreaSetSMFEditActive(textarea, false);

	textarea->smfEditTabs = smfEditTabs;
}

static void ui_TextAreaSMFEditPaneTick(UIPane* pane, UI_PARENT_ARGS)
{
	UITextArea* textarea = (UITextArea*)(uintptr_t)pane->widget.u64;
	bool isActive = ui_IsFocused(textarea) || ui_IsFocusedWidgetGroup(&textarea->smfEditWidgets);

	ui_PaneTick(pane, UI_PARENT_VALUES);
	
	ui_TextAreaSetSMFEditActive(textarea, isActive);
}

void ui_TextAreaSetSMFEditActive(UITextArea *textarea, bool active)
{
	if (textarea->smfEditIsActive == active) {
		return;
	}

	textarea->smfEditIsActive = active;

	if( active ) {
		UIPane* pane;
		UITabGroup* tabGroup;

		pane = ui_PaneCreate( 0, 0, 200, 150, UIUnitFixed, UIUnitFixed, 0 );
		pane->widget.priority = UI_HIGHEST_PRIORITY;
		ui_TopWidgetAddToDevice( UI_WIDGET( pane ), NULL );
		eaPush( &textarea->smfEditWidgets, UI_WIDGET( pane ));
		pane->widget.tickF = ui_TextAreaSMFEditPaneTick;
		pane->widget.u64 = (uintptr_t)textarea;

		tabGroup = ui_TabGroupCreate( 0, 0, 1, 1 );
		tabGroup->bFocusOnClick = false;
		ui_WidgetSetDimensionsEx( UI_WIDGET( tabGroup ), 1, 1, UIUnitPercentage, UIUnitPercentage );
		ui_PaneAddChild( pane, tabGroup );

		{
			int tabIt;
			int shortcutIt;
			for( tabIt = 0; tabIt != eaSize( &textarea->smfEditTabs ); ++tabIt ) {
				UITextShortcutTab* tab = textarea->smfEditTabs[ tabIt ];
				UITab* uiTab = ui_TabCreate( tab->label );
				F32 y = 2;

				ui_TabGroupAddTab( tabGroup, uiTab );				
				for( shortcutIt = 0; shortcutIt != eaSize( &tab->shortcuts ); ++shortcutIt ) {
					UITextShortcut* shortcut = tab->shortcuts[ shortcutIt ];
					UIButton* button = ui_ButtonCreate( shortcut->label, 0, y, ui_TextAreaSMFShortuct, shortcut );
					button->widget.u64 = (U64)(uintptr_t)textarea;
					button->bFocusOnClick = false;
					ui_WidgetSetWidthEx( UI_WIDGET( button ), 1, UIUnitPercentage );
					ui_TabAddChild( uiTab, button );
					y = ui_WidgetGetNextY( UI_WIDGET( button )) + 2;
				}
			}
		}
		ui_TabGroupSetActiveIndex( tabGroup, 0 );

		{
			UISkin *skin = UI_GET_SKIN(textarea);
			UIButton* button = ui_ButtonCreateImageOnly( skin->astrWindowCloseButton ? skin->astrWindowCloseButton : "white",
														 0, 0, ui_TextEntrySMFAreaClose, textarea );
			button->bFocusOnClick = false;
			button->widget.priority = 1;
			ui_WidgetSetPositionEx( UI_WIDGET( button ), 0, 0, 0, 0, UITopRight );
			ui_PaneAddChild( pane, button );
		}
	} else {
		ui_WidgetGroupQueueFreeAndRemove( &textarea->smfEditWidgets );
	}
}

void ui_TextAreaCallFunc( void* ignored, UITextAreaMenuFunc fn )
{
	fn( s_textAreaMenuArea );
}

#include"AutoGen/UITextArea_h_ast.c"
