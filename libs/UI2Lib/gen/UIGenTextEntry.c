#include "ChatData.h"
#include "StringUtil.h"
#include "StringCache.h"
#include "TextBuffer.h"
#include "cmdparse.h"
#include "Expression.h"

#include "inputMouse.h"
#include "inputText.h"
#include "inputKeyBind.h"

#include "GraphicsLib.h"
#include "GfxClipper.h"
#include "GfxTexAtlas.h"
#include "GfxSprite.h"
#include "GfxPrimitive.h"
#include "GfxSpriteText.h"

#include "UICore_h_ast.h"
#include "UIGen_h_ast.h"
#include "UIGenTextEntry.h"
#include "UIGenPrivate.h"

#include "UIGenTextEntry_h_ast.h"

#include "AutoGen/ChatData_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static const char *ChatDataString;

static F32 GenCursorCenterX(UIGen *pGen, UIGenTextEntry *pEntry, UIGenTextEntryState *pState)
{
	UIStyleFont *pFont = ui_GenBundleTextGetFont(&pEntry->TextBundle);
	const unsigned char *pchText = pState->pchString;
	const unsigned char *pchOffset = UTF8GetCodepoint(pchText, pState->iCursor);
	const unsigned char *pchBefore;
	F32 fScale = pGen->fScale;
	F32 fBeforeCursor = 0;
	ui_StyleFontUse(pFont, false, kWidgetModifier_None);
	for (pchBefore = pchText, fBeforeCursor = 0.f; pchBefore && *pchBefore && pchBefore < pchOffset; pchBefore = UTF8GetNextCodepoint(pchBefore))
	{
		U16 wch = UTF8ToWideCharConvert(pchBefore);
		fBeforeCursor += ttGetGlyphWidth(g_font_Active, wch, fScale, fScale);
	}
	return fBeforeCursor;
}

static S32 GenPixelToCursor(UIGen *pGen, UIGenTextEntry *pEntry, UIGenTextEntryState *pState, F32 fPixel)
{
	UIStyleFont *pFont = ui_GenBundleTextGetFont(&pEntry->TextBundle);
	const unsigned char *pchText = pState->pchString;
	const unsigned char *pchFront;
	F32 fScale = pGen->fScale;
	F32 fWidth;
	F32 fLastHalf = 0;
	F32 fGlyphWidth = 0;
	S32 iCodepoint;

	ui_StyleFontUse(pFont, false, kWidgetModifier_None);
	for (pchFront = pchText, fWidth = 0.f, iCodepoint = 0; pchFront && *pchFront && fWidth < fPixel; pchFront = UTF8GetNextCodepoint(pchFront), iCodepoint++, fWidth += fGlyphWidth)
	{
		U16 wch = UTF8ToWideCharConvert(pchFront);
		fGlyphWidth = ttGetGlyphWidth(g_font_Active, wch, fScale, fScale);
	}

	return iCodepoint;
}

void ui_GenMoveCursorToScreenLocationTextEntry(UIGen *pGen, S32 iScreenX) 
{
	UIGenTextEntry *pEntry = UI_GEN_RESULT(pGen, TextEntry);
	UIGenTextEntryState *pState = UI_GEN_STATE(pGen, TextEntry);
	CBox textBox = pGen->ScreenBox;
	S32 iCursor;

	iCursor = TextBuffer_SetCursor(pState->pBuffer, GenPixelToCursor(pGen, pEntry, pState, iScreenX - textBox.lx));
	TextBuffer_SelectionClear(pState->pBuffer);
	TextBuffer_SetSelectionBounds(pState->pBuffer, iCursor, iCursor);
}

void ui_GenTickEarlyTextEntry(UIGen *pGen)
{
	UIGenTextEntry *pEntry = UI_GEN_RESULT(pGen, TextEntry);
	UIGenTextEntryState *pState = UI_GEN_STATE(pGen, TextEntry);
	const unsigned char *pchText = pState->pchString;
	UIStyleFont *pFont = ui_GenBundleTextGetFont(&pEntry->TextBundle);
	CBox textBox = pGen->ScreenBox;
	F32 fCursorX;
	F32 fScale = pGen->fScale;
	F32 fTextWidth = ui_StyleFontWidthNoCache(pFont, fScale, pchText);

	CBoxMoveX(&textBox, textBox.lx - pState->fTextOffset);

	if (mouseDownHit(MS_LEFT, &pGen->ScreenBox) && (pEntry->polyp.bFocusOnClick || ui_GenGetFocus() == pGen))
	{
		S32 iDownX;
		S32 iCursor;
		mouseDownPos(MS_LEFT, &iDownX, NULL);
		iCursor = TextBuffer_SetCursor(pState->pBuffer, GenPixelToCursor(pGen, pEntry, pState, iDownX - textBox.lx));
		TextBuffer_SelectionClear(pState->pBuffer);
		TextBuffer_SetSelectionBounds(pState->pBuffer, iCursor, iCursor);
		pState->bSelecting = true;
		ui_GenSetFocus(pGen);
	}

	if (!mouseIsDown(MS_LEFT))
		pState->bSelecting = false;
	else if (pState->bSelecting)
	{
		S32 iMouseX = g_ui_State.mouseX - textBox.lx;
		S32 iSelectionStart;
		S32 iSelectionEnd;
		TextBuffer_GetSelection(pState->pBuffer, NULL, &iSelectionStart, &iSelectionEnd);
		iSelectionEnd = GenPixelToCursor(pGen, pEntry, pState, iMouseX);
		TextBuffer_SetSelectionBounds(pState->pBuffer, iSelectionStart, iSelectionEnd);
		TextBuffer_SetCursor(pState->pBuffer, iSelectionEnd);
	}

	fCursorX = GenCursorCenterX(pGen, pEntry, pState);
	if (fCursorX < pState->fTextOffset)
		pState->fTextOffset = fCursorX;
	else if (fCursorX > pState->fTextOffset + CBoxWidth(&textBox))
		pState->fTextOffset = fCursorX - CBoxWidth(&textBox);
	if (fTextWidth > CBoxWidth(&textBox))
		pState->fTextOffset = CLAMP(pState->fTextOffset, 0, fTextWidth - CBoxWidth(&textBox));
	else
		pState->fTextOffset = 0;
}

static void GenTextEntryDrawCursor(UIGen *pGen, UIGenTextEntry *pEntry, UIGenTextEntryState *pState, CBox *pTextBox)
{
	if (pEntry->fCursorBlinkInterval == 0 || (U32)(pState->fTimer / pEntry->fCursorBlinkInterval) & 1)
	{
		UIStyleFont *pFont = ui_GenBundleTextGetFont(&pEntry->TextBundle);
		F32 fScale = pGen->fScale;
		F32 fCenterY = (pTextBox->ly + pTextBox->hy) / 2;
		F32 fCursorCenterX = pTextBox->lx + GenCursorCenterX(pGen, pEntry, pState);

		if (pEntry->bTintCursorFromFont && pFont)
		{
			pEntry->CursorBundle.uiTopLeftColor = pFont->uiTopLeftColor;
			pEntry->CursorBundle.uiTopRightColor = pFont->uiTopRightColor;
			pEntry->CursorBundle.uiBottomRightColor = pFont->uiBottomRightColor;
			pEntry->CursorBundle.uiBottomLeftColor = pFont->uiBottomLeftColor;
		}

		if (pEntry->CursorBundle.pchTexture)
		{
			ui_GenBundleTextureDraw(pGen, pGen->pResult, &pEntry->CursorBundle, NULL, fCursorCenterX, fCenterY, true, true, &pState->CursorBundle, NULL);
		}
		else
		{
			U32 iColorTop = ColorRGBAMultiplyAlpha(ui_StyleColorPaletteIndex(pEntry->CursorBundle.uiTopLeftColor), pGen->chAlpha);
			U32 iColorBottom = ColorRGBAMultiplyAlpha(ui_StyleColorPaletteIndex((pEntry->CursorBundle.uiBottomLeftColor == 0xFFFFFFFF) ? pEntry->CursorBundle.uiTopLeftColor : pEntry->CursorBundle.uiBottomLeftColor), pGen->chAlpha);
			gfxDrawLine2(fCursorCenterX, pTextBox->ly, UI_GET_Z(), fCursorCenterX, pTextBox->hy, colorFromRGBA(iColorTop), colorFromRGBA(iColorBottom));
		}
	}
}

static void GenTextEntryDrawSelection(UIGen *pGen, UIGenTextEntry *pEntry, UIGenTextEntryState *pState, CBox *pTextBox)
{
	UIGenTextEntry *pTextArea = UI_GEN_RESULT(pGen, TextEntry);
	UIStyleFont *pFont = ui_GenBundleTextGetFont(&pEntry->TextBundle);
	const unsigned char *pchText = pState->pchString;
	const unsigned char *pchStart = UTF8GetCodepoint(pchText, pState->iSelectionStart);
	const unsigned char *pchEnd = UTF8GetCodepoint(pchText, pState->iSelectionEnd);
	const unsigned char *pchFirst = min(pchStart, pchEnd);
	const unsigned char *pchLast = max(pchStart, pchEnd);
	F32 fScale = pGen->fScale;

	if (pchFirst == pchLast)
		return;
	else
	{
		static unsigned char *s_pchBefore;
		F32 fSelectionFirstX;
		F32 fSelectionLastX;
		CBox box;
		estrClear(&s_pchBefore);
		estrConcat(&s_pchBefore, pchText, pchFirst - pchText);
		fSelectionFirstX = s_pchBefore ? ui_StyleFontWidthNoCache(pFont, fScale, s_pchBefore) + pTextBox->lx : 0;
		estrClear(&s_pchBefore);
		estrConcat(&s_pchBefore, pchText, pchLast - pchText);
		fSelectionLastX = s_pchBefore ? ui_StyleFontWidthNoCache(pFont, fScale, s_pchBefore) + pTextBox->lx : 0;

		BuildCBox(&box, fSelectionFirstX, pTextBox->ly, fSelectionLastX - fSelectionFirstX, CBoxHeight(pTextBox));

		display_sprite_box(white_tex_atlas, &box, UI_GET_Z(), pTextArea->uiSelectionColor);
	}
}

void ui_GenDrawEarlyTextEntry(UIGen *pGen)
{
	static unsigned char *s_estrText;
	UIGenTextEntry *pEntry = UI_GEN_RESULT(pGen, TextEntry);
	UIGenTextEntryState *pState = UI_GEN_STATE(pGen, TextEntry);
	UIStyleFont *pFont = ui_GenBundleTextGetFont(&pEntry->TextBundle);
	const unsigned char *pchText = pState->pchString;
	F32 fCenterY = (pGen->ScreenBox.ly + pGen->ScreenBox.hy) / 2;
	F32 fScale = pGen->fScale;
	CBox textBox = pGen->ScreenBox;
	CBox clipBox = pGen->ScreenBox;

	CBoxMoveX(&textBox, textBox.lx - pState->fTextOffset);

	if (!pState->bBufferFilled && ui_GenBundleTruncate(&pState->Truncate, pFont, GET_REF(pEntry->hTruncate), CBoxWidth(&pGen->ScreenBox), pGen->fScale, pchText, &s_estrText))
	{
		pchText = s_estrText;
	}

	// Needs to be done before drawing the cursor, since the cursor may use the current font color.
	ui_StyleFontUse(pFont, false, kWidgetModifier_None);
	gfxfont_MultiplyAlpha(pGen->chAlpha);

	clipperPushRestrict(&clipBox);
	// Draw the actual text 
	GenTextEntryDrawSelection(pGen, pEntry, pState, &textBox);
	gfxfont_Print(textBox.lx, fCenterY, UI_GET_Z(), fScale, fScale, CENTER_Y, pchText);
	clipperPop(); 

	if (pEntry->bShowCursorWhenUnfocused || (ui_GenGetFocus() == pGen && !gfxIsInactiveApp()))
		GenTextEntryDrawCursor(pGen, pEntry, pState, &textBox);
}

void ui_GenFitContentsSizeTextEntry(UIGen *pGen, UIGenTextEntry *pEntry, CBox *pOut)
{
	UIGenTextEntryState *pState = UI_GEN_STATE(pGen, TextEntry);
	UIStyleFont *pFont = ui_GenBundleTextGetFont(&pEntry->TextBundle);
	const char *pchString = pState->pchString;
	F32 fTextHeight, fTextWidth = 0;
	if (pchString && *pchString)
	{
		if (pState->v2TextSize[0] < 0 || pState->pTextFont != pFont)
		{
			fTextWidth = ui_StyleFontWidth(pFont, 1.f, pchString);
			fTextHeight = ui_StyleFontLineHeight(pFont, 1.f);
			pState->v2TextSize[0] = fTextWidth;
			pState->v2TextSize[1] = fTextHeight;
			pState->pTextFont = pFont;
		}
		else
		{
			fTextWidth = pState->v2TextSize[0];
			fTextHeight = pState->v2TextSize[1];
		}
	}
	else
	{
		fTextHeight = ui_StyleFontLineHeight(pFont, 1.f);
	}
	BuildCBox(pOut, 0, 0, fTextWidth, fTextHeight);
}

void ui_GenUpdateTextEntry(UIGen *pGen)
{
	UIGenTextEntry *pEntry = UI_GEN_RESULT(pGen, TextEntry);
	UIGenTextEntryState *pState = UI_GEN_STATE(pGen, TextEntry);
	bool bText;
	const char *pchString;

	TextBuffer_SetMaxLength(pState->pBuffer, pEntry->iMaxLength);
	TextBuffer_SetValidation(pState->pBuffer, pEntry->eValidate);

	pchString = TextBuffer_GetProfanityFilteredText(pState->pBuffer, pEntry->TextBundle.bFilterProfanity);
	if (pchString && !pState->pchString || !pchString && pState->pchString || pchString && strcmp(pchString, pState->pchString))
	{
		pState->v2TextSize[0] = pState->v2TextSize[1] = -1;
	}

	bText = pchString && *pchString;
	if (bText)
	{
		estrCopy2(&pState->pchString, pchString);
		if (pEntry->pchPassword && *pEntry->pchPassword)
		{
			S32 iChars = UTF8GetLength(pState->pchString);
			S32 iLen = (S32) strlen(pEntry->pchPassword);
			estrClear(&pState->pchString);
			if (iLen == 1)
				estrConcatCharCount(&pState->pchString, pEntry->pchPassword[0], iChars);
			else
			{
				while (iChars-- > 0)
					estrAppend2(&pState->pchString, pEntry->pchPassword);
			}
		}
	}
	else if (ui_GenBundleTextGetText(pGen, &pEntry->TextBundle, NULL, &pState->pchString))
	{
		pState->v2TextSize[0] = pState->v2TextSize[1] = -1;
	}

	pState->iSelectionStart = TextBuffer_GetSelectionStart(pState->pBuffer);
	pState->iSelectionEnd = TextBuffer_GetSelectionEnd(pState->pBuffer);
	pState->iCursor = TextBuffer_GetCursor(pState->pBuffer);

	ui_GenStates(pGen,
		kUIGenStateEmpty, !bText,
		kUIGenStateFilled, bText,
		kUIGenStateChanged, false,
		0);

	ui_GenBundleTextureUpdate(pGen, &pEntry->CursorBundle, &pState->CursorBundle);

	pState->fTimer += pGen->fTimeDelta;
}

bool ui_GenInputTextEntry(UIGen *pGen, KeyInput *pKey)
{
	UIGenTextEntry *pEntry = UI_GEN_RESULT(pGen, TextEntry);
	UIGenTextEntryState *pState = UI_GEN_STATE(pGen, TextEntry);
	bool bChanged = false;
	if (pKey->type == KIT_Text && iswprint(pKey->character) && pEntry->bEditable)
	{
		TextBuffer_InsertCodepoint(pState->pBuffer, pKey->character);
		bChanged = true;
	}
	else if (pKey->type == KIT_EditKey)
	{
		switch (pKey->scancode)
		{
		case INP_BACKSPACE:
			if (pEntry->bEditable)
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
			if (pEntry->bEditable)
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
			pState->fTimer = pEntry->fCursorBlinkInterval;
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
			pState->fTimer = pEntry->fCursorBlinkInterval;
			break;
		case INP_NUMPAD7:
			// Ignore NUMPAD if NUMLOCK is on.
			if (pKey->attrib & KIA_NUMLOCK)
				break;
		case INP_HOME:
			if (pKey->attrib & KIA_SHIFT)
				TextBuffer_SelectionToStart(pState->pBuffer);
			else
				TextBuffer_SetCursor(pState->pBuffer, 0);
			pState->fTimer = pEntry->fCursorBlinkInterval;
			break;
		case INP_NUMPAD1:
			// Ignore NUMPAD if NUMLOCK is on.
			if (pKey->attrib & KIA_NUMLOCK)
				break;
		case INP_END:
			if (pKey->attrib & KIA_SHIFT)
				TextBuffer_SelectionToEnd(pState->pBuffer);
			else
				TextBuffer_CursorToEnd(pState->pBuffer);
			pState->fTimer = pEntry->fCursorBlinkInterval;
			break;
		case INP_V:
			if (pEntry->bEditable)
			{
				if (pKey->attrib & KIA_CONTROL)
					TextBuffer_PasteLine(pState->pBuffer);
				bChanged = true;
			}
			break;
		case INP_C:
			if (pEntry->bEditable)
			{
				if (pKey->attrib & KIA_CONTROL && !(pEntry->pchPassword && *pEntry->pchPassword))
					TextBuffer_Copy(pState->pBuffer);
				bChanged = true;
			}
			break;
		case INP_X:
			if (pEntry->bEditable)
			{
				if (pKey->attrib & KIA_CONTROL && !(pEntry->pchPassword && *pEntry->pchPassword))
					TextBuffer_Cut(pState->pBuffer);
				bChanged = true;
			}
			break;
		case INP_Z:
			if (pEntry->bEditable)
			{
				if (pKey->attrib & KIA_CONTROL)
					TextBuffer_Undo(pState->pBuffer);
				bChanged = true;
			}
			break;
		case INP_Y:
			if (pEntry->bEditable)
			{
				if (pKey->attrib & KIA_CONTROL)
					TextBuffer_Redo(pState->pBuffer);
				bChanged = true;
			}
			break;
		case INP_A:
			if (pKey->attrib & KIA_CONTROL)
				TextBuffer_SelectAll(pState->pBuffer);
			pState->fTimer = pEntry->fCursorBlinkInterval;
			break;
		case INP_TAB:
			if (TextBuffer_GetSelectionEnd(pState->pBuffer) && TextBuffer_GetSelectionEnd(pState->pBuffer) != TextBuffer_GetSelectionStart(pState->pBuffer))
			{
				TextBuffer_SetCursor(pState->pBuffer, max(TextBuffer_GetSelectionEnd(pState->pBuffer), TextBuffer_GetSelectionStart(pState->pBuffer)));
				TextBuffer_SetSelection(pState->pBuffer, 0, 0);
				bChanged = true;
				break;
			}
			else
				return false;
		default:
			// any non-chord is something the user probably doesn't want passed through.
			// any unrecognized chord is probably something they do.
			if (pKey->attrib & (KIA_CONTROL | KIA_ALT) || pKey->scancode == INP_ESCAPE)
				return false;
			else if (key_IsJoystick(pKey->scancode))
				return false;
			else
				break;
		}
	}

	if (bChanged)
	{
		ui_GenRunAction(pGen, pEntry->pOnChanged);
		pState->fTimer = pEntry->fCursorBlinkInterval;
		ui_GenState(pGen, kUIGenStateChanged, true);
	}

	return true;
}

void ui_GenUpdateContextTextEntry(UIGen *pGen, ExprContext *pContext, UIGen *pFor) {
	static int s_hChatData = 0;
	UIGenTextEntryState *pState = UI_GEN_STATE(pGen, TextEntry);

	if (pState && pFor == pGen) {
		exprContextSetPointerVarPooledCached(pContext, ChatDataString, pState->pChatData, parse_ChatData, true, true, &s_hChatData);
	}
}

AUTO_RUN;
void ui_GenRegisterTextEntry(void)
{
	ChatDataString = allocAddStaticString("ChatData");
	ui_GenInitPointerVar(ChatDataString, parse_ChatData);

	ui_GenRegisterType(kUIGenTypeTextEntry, 
		UI_GEN_NO_VALIDATE, 
		UI_GEN_NO_POINTERUPDATE,
		ui_GenUpdateTextEntry, 
		UI_GEN_NO_LAYOUTEARLY, 
		UI_GEN_NO_LAYOUTLATE, 
		ui_GenTickEarlyTextEntry, 
		UI_GEN_NO_TICKLATE, 
		ui_GenDrawEarlyTextEntry,
		ui_GenFitContentsSizeTextEntry, 
		UI_GEN_NO_FITPARENTSIZE, 
		UI_GEN_NO_HIDE, 
		ui_GenInputTextEntry, 
		ui_GenUpdateContextTextEntry, 
		UI_GEN_NO_QUEUERESET);
}

//////////////////////////////////////////////////////////////////////////
// Helpers

const char *ui_GenTextEntryGetText(UIGen *pEntry, bool bProfanityFiltered)
{
	if (UI_GEN_IS_TYPE(pEntry, kUIGenTypeTextEntry))
	{
		UIGenTextEntryState *pState = UI_GEN_STATE(pEntry, TextEntry);
		if (pState && pState->pBuffer)
			return TextBuffer_GetProfanityFilteredText(pState->pBuffer, bProfanityFiltered);
	}
	return NULL;
}

bool ui_GenTextEntrySetText(UIGen *pEntry, const char *pchText)
{
	if (UI_GEN_IS_TYPE(pEntry, kUIGenTypeTextEntry))
	{
		UIGenTextEntry *pTextEntry = UI_GEN_RESULT(pEntry, TextEntry);
		UIGenTextEntryState *pState = UI_GEN_STATE(pEntry, TextEntry);
		if (pState && pState->pBuffer && TextBuffer_SetText(pState->pBuffer, pchText))
		{
			TextBuffer_CursorToEnd(pState->pBuffer);
			if (UI_GEN_READY(pEntry))
				ui_GenRunAction(pEntry, pTextEntry->pOnChanged);
			ui_GenSetState(pEntry, kUIGenStateChanged);
			return true;
		}
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////
// Testing

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTextEntryTestCompletion);
S32 ui_GenTextEntryTestCompletion(SA_PARAM_NN_VALID UIGen *pList, const char *pchMatch, S32 iCount)
{
	UIGenTextEntryCompletion ***peaComps = ui_GenGetManagedListSafe(pList, UIGenTextEntryCompletion);
	static UIGenTextEntryCompletion **s_eaComps;
	static const char *s_apchLoremIpsum[] = {
		"Lorem ipsum dolor sit amet",
		"consectetur adipisicing elit",
		"sed do eiusmod tempor incididunt ut labore",
		"et dolore magna aliqua.",
		"Ut enim ad minim veniam",
		"quis nostrud exercitation ullamco laboris nisi",
		"ut aliquip ex ea commodo consequat.",
		"Duis aute irure dolor in reprehenderit in voluptate velit",
		"esse cillum dolore eu fugiat nulla pariatur.",
		"Excepteur sint occaecat cupidatat non proident,",
		"sunt in culpa qui officia deserunt mollit anim id est laborum.",
		"This is a test",
		"This is another testing",
		"This ensures multiple strings will complete right",
	};
	S32 i;

	while (eaSize(&s_eaComps) < ARRAY_SIZE_CHECKED(s_apchLoremIpsum))
	{
		UIGenTextEntryCompletion *pComp = StructCreate(parse_UIGenTextEntryCompletion);
		pComp->pchSuggestion = (char *)s_apchLoremIpsum[eaSize(&s_eaComps) % ARRAY_SIZE_CHECKED(s_apchLoremIpsum)];
		pComp->pchDisplay = (char *)pComp->pchSuggestion;
		eaPush(&s_eaComps, pComp);
	}
	eaClear(peaComps);
	if (*pchMatch)
	{
		for (i = 0; i < eaSize(&s_eaComps); i++)
		{
			const char *pchSuggestion = s_eaComps[i]->pchSuggestion;
			if (strStartsWith(pchSuggestion, pchMatch) && stricmp(pchSuggestion, pchMatch))
				eaPush(peaComps, s_eaComps[i]);
		}
	}
	ui_GenSetManagedListSafe(pList, peaComps, UIGenTextEntryCompletion, false);
	return eaSize(peaComps);
}

#include "UIGenTextEntry_h_ast.c"