#include "ChatData.h"
#include "estring.h"
#include "Expression.h"
#include "TextBuffer.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "Message.h"

#include "inputMouse.h"
#include "inputText.h"
#include "inputKeyBind.h"

#include "GraphicsLib.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"
#include "GfxPrimitive.h"
#include "UICore_h_ast.h"
#include "UIGen.h"
#include "UIGen_h_ast.h"

#include "UIGenPrivate.h"
#include "UIGenTextArea.h"

#include "AutoGen/ChatData_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static const char *ChatDataString;

static S32 GenGetCursorLine(UIGenTextArea *pArea, UIGenTextAreaState *pState)
{
	unsigned char *pchCursor = UTF8GetCodepoint(
		TextBuffer_GetProfanityFilteredText(pState->pBuffer, pArea && pArea->TextBundle.bFilterProfanity), 
		TextBuffer_GetCursor(pState->pBuffer));
	S32 i;
	for (i = eaSize(&pState->eaLineBreaks) - 1; i > 0; i--)
		if (pState->eaLineBreaks[i] <= pchCursor)
			return i;
	return 0;
}

static S32 GenCursorFromX(UIGen *pGen, UIGenTextArea *pArea, UIGenTextAreaState *pState, F32 fX, S32 iLine)
{
	const char *pchLine = eaGet(&pState->eaLineBreaks, iLine);
	const char *pchNextLine = eaGet(&pState->eaLineBreaks, iLine + 1);

	if (iLine >= eaSize(&pState->eaLineBreaks))
		return UTF8GetLength(TextBuffer_GetProfanityFilteredText(pState->pBuffer, pArea && pArea->TextBundle.bFilterProfanity)); 
	else if (iLine < 0)
		return 0;

	if (pchLine)
	{
		const char *pchLast = pchLine;
		F32 fWidth = 0;
		if (!pchNextLine)
			pchNextLine = strchr(pchLine, '\0');
		while (pchLine < pchNextLine && *pchLine)
		{
			F32 fGlyphWidth = ttGetGlyphWidth(g_font_Active, UTF8ToWideCharConvert(pchLine), pGen->fScale, pGen->fScale);
			pchLast = pchLine;
			pchLine = UTF8GetNextCodepoint(pchLine);
			if (fWidth + fGlyphWidth > fX)
			{
				if (fWidth + fGlyphWidth / 2 > fX && pchLast)
					return UTF8PointerToCodepointIndex(pState->eaLineBreaks[0], pchLast);
				else
					return UTF8PointerToCodepointIndex(pState->eaLineBreaks[0], pchLine);
			}
			fWidth += fGlyphWidth;
		}
		return UTF8PointerToCodepointIndex(pState->eaLineBreaks[0], pchNextLine) - (*pchNextLine ? 1 : 0);
	}
	return 0;
}

static F32 GenXFromCursor(UIGen *pGen, UIGenTextArea *pArea, UIGenTextAreaState *pState, S32 iCursor, S32 *piLine)
{
	F32 fCursor = 0.f;
	S32 iLine = GenGetCursorLine(pArea, pState);
	const char *pchStart = eaGet(&pState->eaLineBreaks, iLine);
	if (pchStart)
	{
		const char *pchCursor = UTF8GetCodepoint(
			TextBuffer_GetProfanityFilteredText(pState->pBuffer, pArea && pArea->TextBundle.bFilterProfanity),
			iCursor);
		UIStyleFont *pFont = ui_GenBundleTextGetFont(&pArea->TextBundle);
		ui_StyleFontUse(pFont, false, kWidgetModifier_None);
		while (pchStart && *pchStart && pchStart < pchCursor)
		{
			fCursor += ttGetGlyphWidth(g_font_Active, UTF8ToWideCharConvert(pchStart), pGen->fScale, pGen->fScale);
			pchStart = UTF8GetNextCodepoint(pchStart);
		}
	}
	if (piLine)
		*piLine = iLine;
	return fCursor;
}

static const unsigned char *GenTextAreaGetText(UIGenTextArea *pArea, UIGenTextAreaState *pState)
{
	bool bFilterProfanity = pArea && pArea->TextBundle.bFilterProfanity && g_bUIGenFilterProfanityThisFrame;
	const unsigned char *pchBuffer = TextBuffer_GetProfanityFilteredText(pState->pBuffer, bFilterProfanity);
	if (!(pchBuffer && *pchBuffer))
	{
		return pState->pchDefault ? pState->pchDefault : "";
	}
	else
		return pchBuffer;
}

static bool GenTextAreaCheckDirty(UIGen *pGen, UIGenTextArea *pArea, UIGenTextAreaState *pState)
{
	if (!nearf(round(CBoxWidth(&pState->TextBox)) /*- UI_STEP*/, pState->fLastWidth))
		return true;
	else if (!nearf(pGen->fScale, pState->fLastScale))
		return true;
	else if (ui_GenBundleTextGetFont(&pArea->TextBundle) != pState->pLastFont)
		return true;
	else if (GenTextAreaGetText(pArea, pState) != eaGet(&pState->eaLineBreaks, 0))
		return true;
	else
		return false;
}

static void GenTextAreaReflow(UIGen *pGen, UIGenTextArea *pArea, UIGenTextAreaState *pState)
{
	UIStyleFont *pFont = ui_GenBundleTextGetFont(&pArea->TextBundle);
	const char *pchStart = GenTextAreaGetText(pArea, pState);
	const unsigned char *pchBestEnd = NULL;
	const unsigned char *pchSecondBestEnd = NULL;
	const unsigned char *pchCheckEnd = NULL;
	// I removed this UI_STEP, since it was wasting 8 pixels on the right side of every text area, between the TextBox and the Scrollbar.
	// I think that buffer should be build into the scrollbar, but that's just my opinion.
	F32 fTotalWidth = round(CBoxWidth(&pState->TextBox)); //- UI_STEP;
	F32 fWidth = 0.f;

	eaClear(&pState->eaLineBreaks);
	ui_StyleFontUse(pFont, false, kWidgetModifier_None);
	for (pchCheckEnd = pchStart; *pchCheckEnd; pchCheckEnd = UTF8GetNextCodepoint(pchCheckEnd))
	{
		switch (*pchCheckEnd)
		{
		case '\n':
			eaPush(&pState->eaLineBreaks, pchStart);
			pchStart = UTF8GetNextCodepoint(pchCheckEnd);
			pchBestEnd = NULL;
			pchSecondBestEnd = NULL;
			fWidth = 0;
			break;
		case ' ':
			pchBestEnd = pchCheckEnd;
		default:
			// Optimization to avoid UTF8GetLength calls.
			fWidth += ttGetGlyphWidth(g_font_Active, UTF8ToWideCharConvert(pchCheckEnd), pGen->fScale, pGen->fScale);
			if (fWidth > fTotalWidth)
			{
				if (!pchBestEnd)
					pchBestEnd = pchSecondBestEnd;
				if (!pchBestEnd)
					break;
				eaPush(&pState->eaLineBreaks, pchStart);
				pchStart = UTF8GetNextCodepoint(pchBestEnd);
				pchBestEnd = NULL;
				pchSecondBestEnd = NULL;
				pchCheckEnd = pchStart;
				fWidth -= fTotalWidth;
			}
			pchSecondBestEnd = pchCheckEnd;
			break;
		}
	}

	eaPush(&pState->eaLineBreaks, pchStart);
	pState->fLastWidth = fTotalWidth;
	pState->fLastScale = pGen->fScale;
	pState->pLastFont = pFont;
	pState->bDirty = false;
}

static F32 GenCursorCenterX(UIGen *pGen, UIGenTextArea *pArea, UIGenTextAreaState *pState, const char *pchStart, const char *pchCursor)
{
	UIStyleFont *pFont = ui_GenBundleTextGetFont(&pArea->TextBundle);
	F32 fScale = pGen->fScale;
	F32 fBeforeCursor = 0.f;
	ui_StyleFontUse(pFont, false, kWidgetModifier_None);
	for (fBeforeCursor = 0.f; pchStart && *pchStart && pchStart < pchCursor; pchStart = UTF8GetNextCodepoint(pchStart))
	{
		U16 wch = UTF8ToWideCharConvert(pchStart);
		fBeforeCursor += ttGetGlyphWidth(g_font_Active, wch, fScale, fScale);
	}
	return fBeforeCursor;
}

static void GenTextAreaDrawSelection(UIGen *pGen, UIGenTextArea *pArea, UIGenTextAreaState *pState)
{
	UIGenTextArea *pTextArea = UI_GEN_RESULT(pGen, TextArea);
	UIStyleFont *pFont = ui_GenBundleTextGetFont(&pArea->TextBundle);
	F32 fScale = pGen->fScale;
	F32 fLineHeight = ui_StyleFontLineHeight(pFont, fScale);
	const char *pchSelectionStart;
	const char *pchSelectionEnd;
	S32 iSelectionStart;
	S32 iSelectionEnd;
	S32 i;
	F32 fZ = UI_GET_Z();
	const char *pchText = 
		TextBuffer_GetProfanityFilteredText(pState->pBuffer, pArea && pArea->TextBundle.bFilterProfanity);
	TextBuffer_GetSelection(pState->pBuffer, NULL, &iSelectionStart, &iSelectionEnd);
	if (pchText != eaGet(&pState->eaLineBreaks, 0) || iSelectionStart == iSelectionEnd)
		return;
	pchSelectionStart = UTF8GetCodepoint(pchText, min(iSelectionStart, iSelectionEnd));
	pchSelectionEnd = UTF8GetCodepoint(pchText, max(iSelectionStart, iSelectionEnd));
	ui_StyleFontUse(pFont, false, kWidgetModifier_None);
	for (i = 0; i < eaSize(&pState->eaLineBreaks) && pState->eaLineBreaks[i] < pchSelectionEnd; i++)
	{
		CBox box;
		bool bLast = (eaSize(&pState->eaLineBreaks) - 1) == i;
		const char *pchLine = pState->eaLineBreaks[i];
		const char *pchNextLine = !bLast ? pState->eaLineBreaks[i + 1] : strchr(pchLine, '\0');

		// We are not yet near the selected area.
		if (pchNextLine <= pchSelectionStart)
			continue;
		// This line is entirely within the selected area.
		else if (pchLine >= pchSelectionStart && pchNextLine <= pchSelectionEnd)
		{
			BuildCBox(&box, pState->TextBox.lx, (pState->TextBox.ly - pState->scrollbar.fScrollPosition) + i * fLineHeight,
				CBoxWidth(&pState->TextBox), fLineHeight);
		}
		else
		{
			F32 fStartX = pState->TextBox.lx;
			F32 fEndX;
			// Selection starts on this line, figure out where.
			if (pchLine <= pchSelectionStart)
			{
				const char *pchCounting = pchLine;
				while (pchCounting < pchNextLine && pchCounting < pchSelectionStart)
				{
					fStartX += ttGetGlyphWidth(g_font_Active, UTF8ToWideCharConvert(pchCounting), fScale, fScale);
					pchCounting = UTF8GetNextCodepoint(pchCounting);
				}
			}
			// Selection ends on this line, figure out where.
			if (pchNextLine >= pchSelectionEnd)
			{
				const char *pchCounting = pchLine;
				fEndX = pState->TextBox.lx;
				while (pchCounting < pchNextLine && pchCounting < pchSelectionEnd)
				{
					fEndX += ttGetGlyphWidth(g_font_Active, UTF8ToWideCharConvert(pchCounting), fScale, fScale);
					pchCounting = UTF8GetNextCodepoint(pchCounting);
				}
			}
			else
				fEndX = pState->TextBox.hx;
			BuildCBox(&box, fStartX, (pState->TextBox.ly - pState->scrollbar.fScrollPosition) + i * fLineHeight,
				fEndX - fStartX, fLineHeight);
		}


		display_sprite_box(atlasLoadTexture("white"), &box, fZ, pTextArea->uiSelectionColor);
	}
}

static void GenTextAreaDrawCursor(UIGen *pGen, UIGenTextArea *pArea, UIGenTextAreaState *pState, F32 fTopY, F32 fBottomY, F32 fX, const char *pchStart, const char *pchCursor)
{
	if (pArea->fCursorBlinkInterval == 0 || (U32)(pState->fTimer / pArea->fCursorBlinkInterval) & 1)
	{
		UIStyleFont *pFont = ui_GenBundleTextGetFont(&pArea->TextBundle);
		F32 fScale = pGen->fScale;
		F32 fCenterY = (fTopY + fBottomY) / 2;
		F32 fCursorCenterX = fX + GenCursorCenterX(pGen, pArea, pState, pchStart, pchCursor);

		if (pArea->bTintCursorFromFont && pFont)
		{
			pArea->CursorBundle.uiTopLeftColor = pFont->uiTopLeftColor;
			pArea->CursorBundle.uiTopRightColor = pFont->uiTopRightColor;
			pArea->CursorBundle.uiBottomRightColor = pFont->uiBottomRightColor;
			pArea->CursorBundle.uiBottomLeftColor = pFont->uiBottomLeftColor;
		}

		if (pArea->CursorBundle.pchTexture)
		{
			ui_GenBundleTextureDraw(pGen, pGen->pResult, &pArea->CursorBundle, NULL, fCursorCenterX, fCenterY, true, true, &pState->CursorBundle, NULL);
		}
		else
		{
			U32 iColorTop = ColorRGBAMultiplyAlpha(ui_StyleColorPaletteIndex(pArea->CursorBundle.uiTopLeftColor), pGen->chAlpha);
			U32 iColorBottom = ColorRGBAMultiplyAlpha(ui_StyleColorPaletteIndex((pArea->CursorBundle.uiBottomLeftColor == 0xFFFFFFFF) ? pArea->CursorBundle.uiTopLeftColor : pArea->CursorBundle.uiBottomLeftColor), pGen->chAlpha);
			gfxDrawLine2(fCursorCenterX, fTopY, UI_GET_Z(), fCursorCenterX, fBottomY, colorFromRGBA(iColorTop), colorFromRGBA(iColorBottom));
		}
	}
}

void ui_GenMoveCursorToScreenLocationTextArea(UIGen *pGen, S32 iScreenX, S32 iScreenY) 
{
	UIGenTextArea *pArea = UI_GEN_RESULT(pGen, TextArea);
	UIGenTextAreaState *pState = UI_GEN_STATE(pGen, TextArea);
	UIStyleFont *pFont = ui_GenBundleTextGetFont(&pArea->TextBundle);
	F32 fLineHeight = ui_StyleFontLineHeight(pFont, pGen->fScale);
	S32 iLine;
	S32 iCodepoint;

	iLine = (iScreenY - (pState->TextBox.ly - pState->scrollbar.fScrollPosition)) / fLineHeight;
	iCodepoint = GenCursorFromX(pGen, pArea, pState, iScreenX - pState->TextBox.lx, iLine);
	iCodepoint = TextBuffer_SetCursor(pState->pBuffer, iCodepoint);
	TextBuffer_SetSelectionBounds(pState->pBuffer, iCodepoint, iCodepoint);
	pState->bScrollToCursor = true;
}

void ui_GenTickEarlyTextArea(UIGen *pGen)
{
	UIGenTextArea *pArea = UI_GEN_RESULT(pGen, TextArea);
	UIGenTextAreaState *pState = UI_GEN_STATE(pGen, TextArea);
	UIStyleFont *pFont = ui_GenBundleTextGetFont(&pArea->TextBundle);
	F32 fLineHeight = ui_StyleFontLineHeight(pFont, pGen->fScale);

	if (pState->bScrollToCursor)
	{
		S32 iCursorLine = GenGetCursorLine(pArea, pState);
		F32 fCursorTop = fLineHeight * iCursorLine;
		F32 fCursorBottom = fCursorTop + fLineHeight;
		if (fCursorTop < pState->scrollbar.fScrollPosition)
			pState->scrollbar.fScrollPosition = fCursorTop;
		else if (fCursorBottom > pState->scrollbar.fScrollPosition + CBoxHeight(&pState->TextBox))
			pState->scrollbar.fScrollPosition = fCursorBottom - fLineHeight;
		pState->bScrollToCursor = false;
	}

	if (mouseDownHit(MS_LEFT, &pState->TextBox))
	{
		S32 iMouseX;
		S32 iMouseY;
		S32 iLine;
		S32 iCodepoint;
		mouseDownPos(MS_LEFT, &iMouseX, &iMouseY);
		iLine = (iMouseY - (pState->TextBox.ly - pState->scrollbar.fScrollPosition)) / fLineHeight;
		iCodepoint = GenCursorFromX(pGen, pArea, pState, iMouseX - pState->TextBox.lx, iLine);
		iCodepoint = TextBuffer_SetCursor(pState->pBuffer, iCodepoint);
		TextBuffer_SetSelectionBounds(pState->pBuffer, iCodepoint, iCodepoint);
		pState->bSelecting = true;
		pState->bScrollToCursor = true;
	}

	if (!mouseIsDown(MS_LEFT) && pState->bSelecting)
		pState->bSelecting = false;
	else if (pState->bSelecting)
	{
		S32 iMouseX = g_ui_State.mouseX - pState->TextBox.lx;
		S32 iMouseY = g_ui_State.mouseY - (pState->TextBox.ly - pState->scrollbar.fScrollPosition);
		S32 iSelectionStart;
		S32 iSelectionEnd;
		S32 iLine = iMouseY / fLineHeight;
		TextBuffer_GetSelection(pState->pBuffer, NULL, &iSelectionStart, &iSelectionEnd);
		iSelectionEnd = GenCursorFromX(pGen, pArea, pState, iMouseX, iLine);
		TextBuffer_SetSelectionBounds(pState->pBuffer, iSelectionStart, iSelectionEnd);
		TextBuffer_SetCursor(pState->pBuffer, iSelectionEnd);
		pState->bScrollToCursor = true;
	}

	ui_GenTickScrollbar(pGen, &pArea->scrollbar, &pState->scrollbar);
}

void ui_GenDrawEarlyTextArea(UIGen *pGen)
{
	UIGenTextArea *pArea = UI_GEN_RESULT(pGen, TextArea);
	UIGenTextAreaState *pState = UI_GEN_STATE(pGen, TextArea);
	UIStyleFont *pFont = ui_GenBundleTextGetFont(&pArea->TextBundle);
	F32 fLineHeight = round(ui_StyleFontLineHeight(pFont, 1.f) * pGen->fScale);
	F32 fStartY = (pState->TextBox.ly - pState->scrollbar.fScrollPosition);
	S32 iCursor = TextBuffer_GetCursor(pState->pBuffer);
	static unsigned char *s_pchDisplay = NULL;
	unsigned char *pchCursor = UTF8GetCodepoint(
		TextBuffer_GetProfanityFilteredText(pState->pBuffer, pArea && pArea->TextBundle.bFilterProfanity), 
		iCursor);
	F32 fTextZ = UI_GET_Z();
	bool bCursorDrawn = false;
	S32 i, iStart, iEnd;

	if (eaGet(&pState->eaLineBreaks, 0) == pState->pchDefault)
	{
		if (pArea->bShowCursorWhenUnfocused || (ui_GenGetFocus() == pGen && !gfxIsInactiveApp()))
			GenTextAreaDrawCursor(pGen, pArea, pState, pState->TextBox.ly, pState->TextBox.ly + fLineHeight, pState->TextBox.lx, pState->pchDefault, pState->pchDefault);
		bCursorDrawn = true;
	}
	GenTextAreaDrawSelection(pGen, pArea, pState);
	ui_StyleFontUse(pFont, false, kWidgetModifier_None);
	gfxfont_MultiplyAlpha(pGen->chAlpha);

	if (pState->bTruncating)
	{
		iStart = 0;
		iEnd = CBoxHeight(&pGen->ScreenBox) / fLineHeight;
	}
	else
	{
		iStart = pState->scrollbar.fScrollPosition / fLineHeight;
		iEnd = ceilf((pState->scrollbar.fScrollPosition + CBoxHeight(&pGen->ScreenBox)) / fLineHeight);
	}

	MIN1(iEnd, eaSize(&pState->eaLineBreaks));

	for (i = iStart; i < iEnd; i++)
	{
		F32 fDrawY = (pState->TextBox.ly - pState->scrollbar.fScrollPosition) + (i + 1) * fLineHeight;
		S32 iLength;

		if (i == iEnd - 1 && i == eaSize(&pState->eaLineBreaks) - 1)
		{
			iLength = strlen(pState->eaLineBreaks[i]);
		}
		else
		{
			iLength = pState->eaLineBreaks[i + 1] - pState->eaLineBreaks[i];
			if (*(pState->eaLineBreaks[i] + iLength) == '\n')
				--iLength;
		}

		estrClear(&s_pchDisplay);
		estrConcat(&s_pchDisplay, pState->eaLineBreaks[i], iLength);

		if (pState->bTruncating && i == iEnd - 1 && i < eaSize(&pState->eaLineBreaks) - 1)
		{
			// Ensure that the truncate is always appended.
			estrAppend2(&s_pchDisplay, TranslateMessageRef(pArea->hTruncate));
			ui_GenBundleTruncate(&pState->Truncate, pFont, GET_REF(pArea->hTruncate), CBoxWidth(&pGen->ScreenBox), pGen->fScale, s_pchDisplay, &s_pchDisplay);
		}

		if (s_pchDisplay && *s_pchDisplay)
			gfxfont_Print(pState->TextBox.lx, fDrawY, fTextZ, pGen->fScale, pGen->fScale, 0, s_pchDisplay);

		if (!bCursorDrawn && (i == eaSize(&pState->eaLineBreaks) - 1 || pchCursor < pState->eaLineBreaks[i + 1]))
		{
			if (pArea->bShowCursorWhenUnfocused || ui_GenGetFocus() == pGen)
				GenTextAreaDrawCursor(pGen, pArea, pState, fDrawY - fLineHeight, fDrawY, pState->TextBox.lx, pState->eaLineBreaks[i], pchCursor);
			bCursorDrawn = true;
		}
	}

	ui_GenDrawScrollbar(pGen, &pArea->scrollbar, &pState->scrollbar);
}

void ui_GenUpdateTextArea(UIGen *pGen)
{
	UIGenTextArea *pArea = UI_GEN_RESULT(pGen, TextArea);
	UIGenTextAreaState *pState = UI_GEN_STATE(pGen, TextArea);
	bool bText = pState->pBuffer ? *TextBuffer_GetText(pState->pBuffer) : false;
	TextBuffer_SetMaxLength(pState->pBuffer, pArea->iMaxLength);
	TextBuffer_SetValidation(pState->pBuffer, pArea->eValidate);

	if (ui_GenBundleTextGetText(pGen, &pArea->TextBundle, NULL, &pState->pchDefault))
		pState->bDirty = true;
	ui_GenBundleTextureUpdate(pGen, &pArea->CursorBundle, &pState->CursorBundle);
	ui_GenStates(pGen,
		kUIGenStateEmpty, !bText,
		kUIGenStateFilled, bText,
		kUIGenStateChanged, false,
		0);

	pState->bTruncating = GET_REF(pArea->hTruncate) && !bText;
	pState->fTimer += pGen->fTimeDelta;
}

void ui_GenLayoutLateTextArea(UIGen *pGen)
{
	UIGenTextArea *pArea = UI_GEN_RESULT(pGen, TextArea);
	UIGenTextAreaState *pState = UI_GEN_STATE(pGen, TextArea);
	F32 fTotalHeight;
	//pState->TextBox = pGen->ScreenBox;
	//pState->TextBox.hx -= (ui_GenScrollbarWidth(&pArea->scrollbar, &pState->scrollbar)) * pGen->fScale;
	ui_GenScrollbarBox(&pArea->scrollbar, &pState->scrollbar, &pGen->ScreenBox, &pState->TextBox, pGen->fScale);
	pState->bDirty |= GenTextAreaCheckDirty(pGen, pArea, pState);
	if (pState->bDirty)
		GenTextAreaReflow(pGen, pArea, pState);
	fTotalHeight = eaSize(&pState->eaLineBreaks) * ui_StyleFontLineHeight(pState->pLastFont, pGen->fScale);
	ui_GenLayoutScrollbar(pGen, &pArea->scrollbar, &pState->scrollbar, pState->bTruncating ? 0 : fTotalHeight);
}

void ui_GenHideTextArea(UIGen *pGen)
{
	UIGenTextAreaState *pState = UI_GEN_STATE(pGen, TextArea);
	pState->fLastScale = -1;
	pState->fLastWidth = -1;
	pState->pLastFont = NULL;
	pState->bSelecting = false;
	eaClear(&pState->eaLineBreaks);
}

bool ui_GenInputTextArea(UIGen *pGen, KeyInput *pKey)
{
	UIGenTextArea *pTextArea = UI_GEN_RESULT(pGen, TextArea);
	UIGenTextAreaState *pState = UI_GEN_STATE(pGen, TextArea);
	bool bChanged = false;
	if (pKey->type == KIT_Text && iswprint(pKey->character) && pTextArea->bEditable)
	{
		TextBuffer_InsertCodepoint(pState->pBuffer, pKey->character);
		bChanged = true;
	}
	else if (pKey->type == KIT_EditKey)
	{
		switch (pKey->scancode)
		{
		case INP_NUMPADENTER:
		case INP_RETURN:
			if (pTextArea->bEditable)
			{
				TextBuffer_InsertCodepoint(pState->pBuffer, '\n');
				bChanged = true;
			}
			break;
		case INP_BACKSPACE:
			if (pTextArea->bEditable)
			{
				if (pKey->attrib & KIA_CONTROL && !TextBuffer_GetSelection(pState->pBuffer, NULL, NULL, NULL))
					TextBuffer_DeletePreviousWord(pState->pBuffer);
				else
					TextBuffer_Backspace(pState->pBuffer);
				bChanged = true;
			}
			break;
		case INP_DECIMAL:
			// Ignore NUMPAD if NUMLOCK is on.
			if (pKey->attrib & KIA_NUMLOCK)
				break;
		case INP_DELETE:
			if (pTextArea->bEditable)
			{
				if (pKey->attrib & KIA_CONTROL && !TextBuffer_GetSelection(pState->pBuffer, NULL, NULL, NULL))
					TextBuffer_DeleteNextWord(pState->pBuffer);
				else
					TextBuffer_Delete(pState->pBuffer);
				bChanged = true;
			}
			break;
		case INP_NUMPAD4:
			// Ignore NUMPAD if NUMLOCK is on.
			if (pKey->attrib & KIA_NUMLOCK)
				break;
		case INP_LEFTARROW:
			if (pKey->attrib & KIA_CONTROL && pKey->attrib & KIA_SHIFT)
				TextBuffer_SelectionPreviousWord(pState->pBuffer);
			else if (pKey->attrib & KIA_CONTROL)
				TextBuffer_CursorToPreviousWord(pState->pBuffer);
			else if (pKey->attrib & KIA_SHIFT)
				TextBuffer_SelectionPrevious(pState->pBuffer);
			else
				TextBuffer_MoveCursor(pState->pBuffer, -1);
			pState->bScrollToCursor = true;
			break;
		case INP_NUMPAD6:
			// Ignore NUMPAD if NUMLOCK is on.
			if (pKey->attrib & KIA_NUMLOCK)
				break;
		case INP_RIGHTARROW:
			if (pKey->attrib & KIA_CONTROL && pKey->attrib & KIA_SHIFT)
				TextBuffer_SelectionNextWord(pState->pBuffer);
			else if (pKey->attrib & KIA_CONTROL)
				TextBuffer_CursorToNextWord(pState->pBuffer);
			else if (pKey->attrib & KIA_SHIFT)
				TextBuffer_SelectionNext(pState->pBuffer);
			else
				TextBuffer_MoveCursor(pState->pBuffer, +1);
			pState->bScrollToCursor = true;
			break;
		case INP_NUMPAD7:
			// Ignore NUMPAD if NUMLOCK is on.
			if (pKey->attrib & KIA_NUMLOCK)
				break;
		case INP_HOME:
			{
				S32 iLine = GenGetCursorLine(pTextArea, pState);
				const char *pchStart = eaGet(&pState->eaLineBreaks, iLine);
				if (pchStart)
				{
					S32 iSelectionStart = TextBuffer_GetSelectionStart(pState->pBuffer);
					S32 iOldCursor = TextBuffer_GetCursor(pState->pBuffer);
					S32 iCursor = UTF8PointerToCodepointIndex(pState->eaLineBreaks[0], pchStart);
					TextBuffer_SetCursor(pState->pBuffer, iCursor);
					if (pKey->attrib & KIA_SHIFT)
						TextBuffer_SetSelectionBounds(pState->pBuffer, iSelectionStart ? iSelectionStart : iOldCursor, iCursor);
					pState->bScrollToCursor = true;
				}
				break;
			}
		case INP_NUMPAD1:
			// Ignore NUMPAD if NUMLOCK is on.
			if (pKey->attrib & KIA_NUMLOCK)
				break;
		case INP_END:
			{
				S32 iLine = GenGetCursorLine(pTextArea, pState);
				const char *pchStart = eaGet(&pState->eaLineBreaks, iLine);
				if (pchStart)
				{
					S32 iSelectionStart = TextBuffer_GetSelectionStart(pState->pBuffer);
					S32 iOldCursor = TextBuffer_GetCursor(pState->pBuffer);
					S32 iCursor;
					const char *pchNextLine = eaGet(&pState->eaLineBreaks, iLine + 1);
					if (pchNextLine)
						iCursor = UTF8PointerToCodepointIndex(pState->eaLineBreaks[0], pchNextLine) - (*pchNextLine ? 1 : 0);
					else
						iCursor = UTF8PointerToCodepointIndex(pState->eaLineBreaks[0], strchr(pchStart, '\0'));
					TextBuffer_SetCursor(pState->pBuffer, iCursor);
					if (pKey->attrib & KIA_SHIFT)
						TextBuffer_SetSelectionBounds(pState->pBuffer, iSelectionStart ? iSelectionStart : iOldCursor, iCursor);
					pState->bScrollToCursor = true;
				}
				break;
			}
		case INP_V:
			if (pTextArea->bEditable)
			{
				if (pKey->attrib & KIA_CONTROL)
					TextBuffer_Paste(pState->pBuffer);
				bChanged = true;
				pState->bScrollToCursor = true;
			}
			break;
		case INP_C:
			if (pKey->attrib & KIA_CONTROL)
				TextBuffer_Copy(pState->pBuffer);
			bChanged = true;
			pState->bScrollToCursor = true;
			break;
		case INP_X:
			if (pTextArea->bEditable)
			{
				if (pKey->attrib & KIA_CONTROL)
					TextBuffer_Cut(pState->pBuffer);
				bChanged = true;
				pState->bScrollToCursor = true;
			}
			break;
		case INP_Z:
			if (pTextArea->bEditable)
			{
				if (pKey->attrib & KIA_CONTROL)
					TextBuffer_Undo(pState->pBuffer);
				bChanged = true;
				pState->bScrollToCursor = true;
			}
			break;
		case INP_Y:
			if (pTextArea->bEditable)
			{
				if (pKey->attrib & KIA_CONTROL)
					TextBuffer_Redo(pState->pBuffer);
				bChanged = true;
				pState->bScrollToCursor = true;
			}
			break;
		case INP_A:
			if (pKey->attrib & KIA_CONTROL)
				TextBuffer_SelectAll(pState->pBuffer);
			pState->fTimer = pTextArea->fCursorBlinkInterval;
			break;
		case INP_NUMPAD8:
			// Ignore NUMPAD if NUMLOCK is on.
			if (pKey->attrib & KIA_NUMLOCK)
				break;
		case INP_UPARROW:
			{
				S32 iLine;
				F32 fX = GenXFromCursor(pGen, pTextArea, pState, TextBuffer_GetCursor(pState->pBuffer), &iLine);
				S32 iCursor = GenCursorFromX(pGen, pTextArea, pState, fX, iLine - 1);
				S32 iSelectionStart = TextBuffer_GetSelectionStart(pState->pBuffer);
				S32 iOldCursor = TextBuffer_GetCursor(pState->pBuffer);
				TextBuffer_SetCursor(pState->pBuffer, iCursor);
				if (pKey->attrib & KIA_SHIFT)
					TextBuffer_SetSelectionBounds(pState->pBuffer, iSelectionStart ? iSelectionStart : iOldCursor, iCursor);
				pState->bScrollToCursor = true;
				break;
			}
		case INP_NUMPAD2:
			// Ignore NUMPAD if NUMLOCK is on.
			if (pKey->attrib & KIA_NUMLOCK)
				break;
		case INP_DOWNARROW:
			{
				S32 iLine;
				F32 fX = GenXFromCursor(pGen, pTextArea, pState, TextBuffer_GetCursor(pState->pBuffer), &iLine);
				S32 iCursor = GenCursorFromX(pGen, pTextArea, pState, fX, iLine + 1);
				S32 iSelectionStart = TextBuffer_GetSelectionStart(pState->pBuffer);
				S32 iOldCursor = TextBuffer_GetCursor(pState->pBuffer);
				TextBuffer_SetCursor(pState->pBuffer, iCursor);
				if (pKey->attrib & KIA_SHIFT)
					TextBuffer_SetSelectionBounds(pState->pBuffer, iSelectionStart ? iSelectionStart : iOldCursor, iCursor);
				pState->bScrollToCursor = true;
				break;
			}
		case INP_NUMPAD9:
			// Ignore NUMPAD if NUMLOCK is on.
			if (pKey->attrib & KIA_NUMLOCK)
				break;
		case INP_PGUP:
			{
				S32 iLine;
				UIStyleFont *pFont = ui_GenBundleTextGetFont(&pTextArea->TextBundle);
				S32 iLineCount = CBoxHeight(&pState->TextBox) / ui_StyleFontLineHeight(pFont, pGen->fScale) * 0.8;
				F32 fX = GenXFromCursor(pGen, pTextArea, pState, TextBuffer_GetCursor(pState->pBuffer), &iLine);
				S32 iCursor = GenCursorFromX(pGen, pTextArea, pState, fX, max(0, iLine - iLineCount));
				S32 iSelectionStart = TextBuffer_GetSelectionStart(pState->pBuffer);
				S32 iOldCursor = TextBuffer_GetCursor(pState->pBuffer);
				TextBuffer_SetCursor(pState->pBuffer, iCursor);
				if (pKey->attrib & KIA_SHIFT)
					TextBuffer_SetSelectionBounds(pState->pBuffer, iSelectionStart ? iSelectionStart : iOldCursor, iCursor);
				pState->bScrollToCursor = true;
				break;
			}
		case INP_NUMPAD3:
			// Ignore NUMPAD if NUMLOCK is on.
			if (pKey->attrib & KIA_NUMLOCK)
				break;
		case INP_PGDN:
			{
				S32 iLine;
				UIStyleFont *pFont = ui_GenBundleTextGetFont(&pTextArea->TextBundle);
				S32 iLineCount = CBoxHeight(&pState->TextBox) / ui_StyleFontLineHeight(pFont, pGen->fScale) * 0.8;
				F32 fX = GenXFromCursor(pGen, pTextArea, pState, TextBuffer_GetCursor(pState->pBuffer), &iLine);
				S32 iCursor = GenCursorFromX(pGen, pTextArea, pState, fX, min(eaSize(&pState->eaLineBreaks) - 1, iLine + iLineCount));
				S32 iSelectionStart = TextBuffer_GetSelectionStart(pState->pBuffer);
				S32 iOldCursor = TextBuffer_GetCursor(pState->pBuffer);
				TextBuffer_SetCursor(pState->pBuffer, iCursor);
				if (pKey->attrib & KIA_SHIFT)
					TextBuffer_SetSelectionBounds(pState->pBuffer, iSelectionStart ? iSelectionStart : iOldCursor, iCursor);
				pState->bScrollToCursor = true;
				break;
			}
		default:
			// any non-chord is something the user probably doesn't want passed through.
			// any unrecognized chord is probably something they do.
			if (pKey->attrib & (KIA_CONTROL | KIA_ALT) || pKey->scancode == INP_TAB || pKey->scancode == INP_ESCAPE)
				return false;
			else if (key_IsJoystick(pKey->scancode))
				return false;
			else
				break;
		}
	}

	if (bChanged)
	{
		ui_GenRunAction(pGen, pTextArea->pOnChanged);
		pState->bDirty = true;
		pState->bScrollToCursor = true;
		// Maybe remove this?
		ui_GenState(pGen, kUIGenStateChanged, true);
	}

	if (pState->bScrollToCursor)
		pState->fTimer = pTextArea->fCursorBlinkInterval;


	return true;
}

void ui_GenFitContentsSizeTextArea(UIGen *pGen, UIGenTextArea *pArea, CBox *pOut)
{
	UIGenTextAreaState *pState = UI_GEN_STATE(pGen, TextArea);
	UIStyleFont *pFont = ui_GenBundleTextGetFont(&pArea->TextBundle);
	F32 fTextHeight = ui_StyleFontLineHeight(pFont, 1.f);
	BuildCBox(pOut, 0, 0, 0, fTextHeight * MAX(1, eaSize(&pState->eaLineBreaks)));
}

void ui_GenUpdateContextTextArea(UIGen *pGen, ExprContext *pContext, UIGen *pFor)
{
	static int s_hChatData = 0;
	UIGenTextAreaState *pState = UI_GEN_STATE(pGen, TextArea);

	if (pState && pFor == pGen)
	{
		exprContextSetPointerVarPooledCached(pContext, ChatDataString, pState->pChatData, parse_ChatData, true, true, &s_hChatData);
	}
}

AUTO_RUN;
void ui_GenRegisterTextArea(void)
{
	ChatDataString = allocAddStaticString("ChatData");
	ui_GenInitPointerVar(ChatDataString, parse_ChatData);

	ui_GenRegisterType(kUIGenTypeTextArea, 
		UI_GEN_NO_VALIDATE, 
		UI_GEN_NO_POINTERUPDATE,
		ui_GenUpdateTextArea, 
		UI_GEN_NO_LAYOUTEARLY, 
		ui_GenLayoutLateTextArea, 
		ui_GenTickEarlyTextArea,
		UI_GEN_NO_TICKLATE,
		ui_GenDrawEarlyTextArea,
		ui_GenFitContentsSizeTextArea,
		UI_GEN_NO_FITPARENTSIZE,
		ui_GenHideTextArea,
		ui_GenInputTextArea,
		ui_GenUpdateContextTextArea,
		UI_GEN_NO_QUEUERESET);
}

AUTO_FIXUPFUNC;
TextParserResult ui_GenTextAreaStateParserFixup(UIGenTextAreaState *pState, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		eaDestroy(&pState->eaLineBreaks);
	}
	return PARSERESULT_SUCCESS;
}

#include "UIGenTextArea_h_ast.c"
