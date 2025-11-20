#include "EString.h"
#include "StringUtil.h"
#include "sysutil.h"
#include "TextFilter.h"

#include "TextBuffer.h"
#include "TextBuffer_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

AUTO_STRUCT;
typedef struct TextBufferState
{
	char *pchText; AST(ESTRING DEFAULT(""))
	char *pchProfanityFilteredText;
	S32 iCursor;
	S32 iSelectionStart;
	S32 iSelectionEnd;
} TextBufferState;

AUTO_STRUCT;
typedef struct TextBuffer
{
	S32 iUndoStackSize; AST(DEFAULT(TEXT_BUFFER_UNDO_STACK_SIZE))
	TextBufferState *pState; AST(ALWAYS_ALLOC)
	TextBufferState **eaUndo;
	TextBufferState **eaRedo;
	char *pchClipboard; AST(ESTRING DEFAULT(""))
	S32 iMaxLength; AST(DEFAULT(-1))
	TextBufferStringValidateCB cbStringValidate; NO_AST
	TextBufferCodepointValidateCB cbCodepointValidate; NO_AST
	U32 cpReplacement; NO_AST
	bool bPrivateClipboard : 1;
	bool bLastOpWasSimpleInsert : 1;
} TextBuffer;

#define TEXT_BUFFER_FORCE_STRING(pState) { if (!(pState)->pchText) estrCopy2(&(pState)->pchText, ""); }

// Textparser does not actually understand empty strings vs. NULL strings, so instead we need to do this earlier.
AUTO_FIXUPFUNC;
TextParserResult TextBufferState_ParserFixup(TextBufferState *pState, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_CONSTRUCTOR:
		if (!pState->pchText)
			estrCopy2(&pState->pchText, "");
	}
	return PARSERESULT_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////
// Private helper functions

// Make sure the selection state and cursor are within bounds.
static void TextBufferState_BoundsCheck(TextBufferState *pState)
{
	TEXT_BUFFER_FORCE_STRING(pState)
	{
		S32 iLength = UTF8GetLength(pState->pchText);
		pState->iCursor = CLAMP(pState->iCursor, 0, iLength);
		pState->iSelectionStart = CLAMP(pState->iSelectionStart, 0, iLength);
		pState->iSelectionEnd = CLAMP(pState->iSelectionEnd, 0, iLength);
	}
}

static void TextBuffer_EnforceUndoStackSize(TextBuffer *pText)
{
	while (eaSize(&pText->eaRedo) > pText->iUndoStackSize)
		StructDestroy(parse_TextBufferState, eaRemove(&pText->eaRedo, 0));
	while (eaSize(&pText->eaUndo) > pText->iUndoStackSize)
		StructDestroy(parse_TextBufferState, eaRemove(&pText->eaUndo, 0));
}

static void TextBuffer_ClampLength(TextBuffer *pText)
{
	TEXT_BUFFER_FORCE_STRING(pText->pState)
	{
		if (pText->iMaxLength >= 0)
		{
			const char *pchEnd = UTF8GetCodepoint(pText->pState->pchText, pText->iMaxLength);
			if (pchEnd && *pchEnd)
			{
				estrSetSize(&pText->pState->pchText, pchEnd - pText->pState->pchText);
				TextBufferState_BoundsCheck(pText->pState);
				SAFE_FREE(pText->pState->pchProfanityFilteredText);
			}
		}
	}
}

static void TextBuffer_Normalize(TextBuffer *pText)
{
	char* str = NULL;
	TEXT_BUFFER_FORCE_STRING( pText->pState );
	StructCopyString( &str, pText->pState->pchText );
	UTF8NormalizeString( str, &pText->pState->pchText );
	StructFreeString( str );
}

static void TextBuffer_Validate(TextBuffer *pText)
{
	TEXT_BUFFER_FORCE_STRING(pText->pState)
	{
		if (pText->cbStringValidate) {
			pText->cbStringValidate(pText, &pText->pState->pchText);
			SAFE_FREE(pText->pState->pchProfanityFilteredText);
		}
		TextBuffer_Normalize(pText);
		TextBuffer_ClampLength(pText);
	}
}

//////////////////////////////////////////////////////////////////////////
// Public interface

TextBuffer *TextBuffer_CreateEx(const unsigned char *pchInitial, S32 iUndoStackSize)
{
	TextBuffer *pText = StructCreate(parse_TextBuffer);
	pText->iUndoStackSize = iUndoStackSize;
	TextBuffer_SetText(pText, pchInitial);
	return pText;
}

void TextBuffer_Destroy(TextBuffer *pText)
{
	StructDestroy(parse_TextBuffer, pText);
}

void TextBuffer_DestroySafe(TextBuffer **ppText)
{
	StructDestroySafe(parse_TextBuffer, ppText);
}

const unsigned char *TextBuffer_GetText(TextBuffer *pText)
{
	return pText->pState->pchText ? pText->pState->pchText : "";
}

const unsigned char *TextBuffer_GetProfanityFilteredText(TextBuffer *pText, bool bApplyFilter)
{
	if (!pText->pState->pchText) {
		return "";
	}

	if (!bApplyFilter) {
		return pText->pState->pchText;
	}

	if (!pText->pState->pchProfanityFilteredText) {
		pText->pState->pchProfanityFilteredText = strdup(pText->pState->pchText);
	}

	return pText->pState->pchProfanityFilteredText;
}

void TextBuffer_CopyText(TextBuffer *pText, unsigned char **ppchText)
{
	estrCopy(ppchText, &pText->pState->pchText);
}

bool TextBuffer_SetText(TextBuffer *pText, const unsigned char *pchText)
{
	if (!pchText)
		pchText = "";
	if (!UTF8StringIsValid(pchText, NULL))
		return false;
	if (pText->pState->pchText && strcmp(pText->pState->pchText, pchText) == 0)
		return true;
	estrCopy2(&pText->pState->pchText, pchText);
	SAFE_FREE(pText->pState->pchProfanityFilteredText);
	TextBuffer_Validate(pText);
	TextBufferState_BoundsCheck(pText->pState);
	return true;
}

S32 TextBuffer_RemoveText(TextBuffer *pText, S32 iStart, S32 iLength)
{
	TEXT_BUFFER_FORCE_STRING(pText->pState)
	{
		S32 iEnd = UTF8GetLength(pText->pState->pchText);
		S32 iToDelete = min(iLength, iEnd - iStart);
		const char *pchStart = UTF8GetCodepoint(pText->pState->pchText, iStart);
		const char *pchEnd = pchStart ? UTF8GetCodepoint(pText->pState->pchText, iStart + iToDelete) : NULL;
		pText->bLastOpWasSimpleInsert = false;
		if (pchStart && pchEnd && pchStart != pchEnd)
		{
			estrRemove(&pText->pState->pchText, pchStart - pText->pState->pchText, pchEnd - pchStart);
			SAFE_FREE(pText->pState->pchProfanityFilteredText);
			TextBuffer_Validate(pText);
			return iToDelete;
		}
		else
			return 0;
	}
}

S32 TextBuffer_InsertText(TextBuffer *pText, S32 iOffset, const unsigned char *pchText)
{
	TEXT_BUFFER_FORCE_STRING(pText->pState)
	{
		pText->bLastOpWasSimpleInsert = false;
		if (!pchText)
			pchText = "";
		if (UTF8StringIsValid(pchText, NULL))
		{
			char *pchOffset = UTF8GetCodepoint(pText->pState->pchText, iOffset);
			if (pchOffset)
			{
				S32 iToInsert = (S32)strlen(pchText);
				if (pText->iMaxLength >= 0)
				{
					S32 iRemaining = pText->iMaxLength - UTF8GetLength(pText->pState->pchText);
					const unsigned char *pchEnd = UTF8GetCodepoint(pchText, iRemaining);
					if (pchEnd)
						iToInsert = pchEnd - pchText;
				}
				if (iToInsert)
				{
					estrInsert(&pText->pState->pchText, pchOffset - pText->pState->pchText, pchText, iToInsert);
					SAFE_FREE(pText->pState->pchProfanityFilteredText);
					TextBuffer_Validate(pText);
				}
				return UTF8GetLength(pchText);
			}
			return 0;
		}
		return 0;
	}
}

void TextBuffer_TrimLeadingAndTrailingWhitespace(SA_PARAM_NN_VALID TextBuffer *pText)
{
	TEXT_BUFFER_FORCE_STRING(pText->pState);
	estrTrimLeadingAndTrailingWhitespace(&pText->pState->pchText);
	TextBuffer_Validate(pText);
	TextBufferState_BoundsCheck(pText->pState);
}

//////////////////////////////////////////////////////////////////////////
// Cursor management

S32 TextBuffer_GetCursor(TextBuffer *pText)
{
	return pText->pState->iCursor;
}

S32 TextBuffer_SetCursor(TextBuffer *pText, S32 iCursor)
{
	TEXT_BUFFER_FORCE_STRING(pText->pState)
	{
		S32 iLength = UTF8GetLength(pText->pState->pchText);
		pText->bLastOpWasSimpleInsert = false;
		if (iCursor < 0)
			iCursor = CLAMP(iLength - (iCursor + 1), 0, iLength);
		pText->pState->iCursor = iCursor;
		if (iCursor != pText->pState->iSelectionEnd)
			TextBuffer_SetSelectionBounds(pText, 0, 0);
		TextBufferState_BoundsCheck(pText->pState);
		return pText->pState->iCursor;
	}
}

S32 TextBuffer_MoveCursor(TextBuffer *pText, S32 iCursor)
{
	if (iCursor < 0 && pText->pState->iCursor < iCursor)
		pText->pState->iCursor = 0;
	else
		pText->pState->iCursor += iCursor;

	pText->bLastOpWasSimpleInsert = false;
	TextBuffer_SelectionClear(pText);
	TextBufferState_BoundsCheck(pText->pState);
	return pText->pState->iCursor;
}

S32 TextBuffer_CursorToEnd(TextBuffer *pText)
{
	TEXT_BUFFER_FORCE_STRING(pText->pState)
	{
		pText->bLastOpWasSimpleInsert = false;
		TextBuffer_SelectionClear(pText);
		pText->pState->iCursor = UTF8GetLength(pText->pState->pchText);
		return pText->pState->iCursor;
	}
}

S32 TextBuffer_CursorToNextWord(TextBuffer *pText)
{
	TEXT_BUFFER_FORCE_STRING(pText->pState)
	{
		S32 iNextWord = UTF8CodepointOfNextWord(pText->pState->pchText, pText->pState->iCursor);
		pText->bLastOpWasSimpleInsert = false;
		if (iNextWord > pText->pState->iCursor)
			TextBuffer_SetCursor(pText, iNextWord);
		TextBuffer_SelectionClear(pText);
		return pText->pState->iCursor;
	}
}

S32 TextBuffer_CursorToPreviousWord(TextBuffer *pText)
{
	TEXT_BUFFER_FORCE_STRING(pText->pState)
	{
		S32 iPreviousWord = UTF8CodepointOfPreviousWord(pText->pState->pchText, pText->pState->iCursor);
		pText->bLastOpWasSimpleInsert = false;
		if (iPreviousWord < pText->pState->iCursor)
			TextBuffer_SetCursor(pText, iPreviousWord);
		TextBuffer_SelectionClear(pText);
		return pText->pState->iCursor;
	}
}

//////////////////////////////////////////////////////////////////////////
// Selection management

bool TextBuffer_GetSelection(TextBuffer *pText, unsigned char **ppchSelection, S32 *piStart, S32 *piEnd)
{
	TEXT_BUFFER_FORCE_STRING(pText->pState)
	{
		S32 iStart = pText->pState->iSelectionStart;
		S32 iEnd = pText->pState->iSelectionEnd;
		if (piStart)
			*piStart = iStart;
		if (piEnd)
			*piEnd = iEnd;
		if (ppchSelection)
		{
			estrClear(ppchSelection);
			if (iStart != iEnd)
			{
				const char *pchStart = UTF8GetCodepoint(pText->pState->pchText, min(iStart, iEnd));
				const char *pchEnd = pchStart ? UTF8GetCodepoint(pText->pState->pchText, max(iStart, iEnd)) : NULL;
				if (devassertmsg(pchStart && pchEnd, "TextBuffer selection got out of bounds!"))
					estrConcat(ppchSelection, pchStart, pchEnd - pchStart);
			}
		}

		return (iStart != iEnd);
	}
}

S32 TextBuffer_GetSelectionStart(TextBuffer *pText)
{
	return pText->pState->iSelectionStart;
}

S32 TextBuffer_GetSelectionEnd(TextBuffer *pText)
{
	return pText->pState->iSelectionEnd;
}

void TextBuffer_SetSelectionBounds(TextBuffer *pText, S32 iStart, S32 iEnd)
{
	pText->bLastOpWasSimpleInsert = false;
	pText->pState->iSelectionStart = iStart;
	pText->pState->iSelectionEnd = iEnd;
	if (iEnd || iStart)
		pText->pState->iCursor = pText->pState->iSelectionEnd;
	TextBufferState_BoundsCheck(pText->pState);
}

void TextBuffer_SetSelection(TextBuffer *pText, S32 iStart, S32 iLength)
{
	pText->bLastOpWasSimpleInsert = false;
	TextBuffer_SetSelectionBounds(pText, iStart, iStart + iLength);
}

void TextBuffer_SelectToPos(TextBuffer *pText, S32 iPos)
{	
	int iSelectStart;
	pText->bLastOpWasSimpleInsert = false;

	if(pText->pState->iCursor==pText->pState->iSelectionEnd)
		iSelectStart = pText->pState->iSelectionStart;
	else
		iSelectStart = pText->pState->iCursor;

	TextBuffer_SetSelectionBounds(pText, iSelectStart, iPos);
}

void TextBuffer_SelectionPrevious(TextBuffer *pText)
{
	pText->bLastOpWasSimpleInsert = false;
	if (pText->pState->iSelectionStart == 0 && pText->pState->iSelectionEnd == 0)
	{
		pText->pState->iSelectionStart = pText->pState->iCursor;
		pText->pState->iSelectionEnd = pText->pState->iCursor;
	}
	if (pText->pState->iSelectionEnd > 0)
	{
		pText->pState->iSelectionEnd--;
		TextBufferState_BoundsCheck(pText->pState);
	}
	pText->pState->iCursor = pText->pState->iSelectionEnd;
}

void TextBuffer_SelectionNext(TextBuffer *pText)
{
	pText->bLastOpWasSimpleInsert = false;
	if (pText->pState->iSelectionStart == 0 && pText->pState->iSelectionEnd == 0)
	{
		pText->pState->iSelectionStart = pText->pState->iCursor;
		pText->pState->iSelectionEnd = pText->pState->iCursor;
	}
	pText->pState->iSelectionEnd++;
	TextBufferState_BoundsCheck(pText->pState);
	pText->pState->iCursor = pText->pState->iSelectionEnd;
}

void TextBuffer_SelectionPreviousWord(TextBuffer *pText)
{
	TEXT_BUFFER_FORCE_STRING(pText->pState)
	{
		pText->bLastOpWasSimpleInsert = false;
		if (pText->pState->iSelectionStart == 0 && pText->pState->iSelectionEnd == 0)
		{
			pText->pState->iSelectionStart = pText->pState->iCursor;
			pText->pState->iSelectionEnd = pText->pState->iCursor;
		}
		pText->pState->iSelectionEnd = UTF8CodepointOfPreviousWord(pText->pState->pchText, pText->pState->iSelectionEnd);
		TextBufferState_BoundsCheck(pText->pState);
		pText->pState->iCursor = pText->pState->iSelectionEnd;
	}
}

void TextBuffer_SelectionNextWord(TextBuffer *pText)
{
	TEXT_BUFFER_FORCE_STRING(pText->pState)
	{
		pText->bLastOpWasSimpleInsert = false;
		if (pText->pState->iSelectionStart == 0 && pText->pState->iSelectionEnd == 0)
		{
			pText->pState->iSelectionStart = pText->pState->iCursor;
			pText->pState->iSelectionEnd = pText->pState->iCursor;
		}
		pText->pState->iSelectionEnd = UTF8CodepointOfNextWord(pText->pState->pchText, pText->pState->iSelectionEnd);
		TextBufferState_BoundsCheck(pText->pState);
		pText->pState->iCursor = pText->pState->iSelectionEnd;
	}
}

void TextBuffer_SelectionToStart(TextBuffer *pText)
{
	pText->bLastOpWasSimpleInsert = false;
	if (!pText->pState->iSelectionStart && !pText->pState->iSelectionEnd)
		pText->pState->iSelectionStart = pText->pState->iCursor;
	pText->pState->iSelectionEnd = 0;
	pText->pState->iCursor = pText->pState->iSelectionEnd;
}

void TextBuffer_SelectionToEnd(TextBuffer *pText)
{
	TEXT_BUFFER_FORCE_STRING(pText->pState)
	{
		pText->bLastOpWasSimpleInsert = false;
		if (pText->pState->iSelectionEnd == 0 && pText->pState->iSelectionStart == 0)
			pText->pState->iSelectionStart = pText->pState->iCursor;
		pText->pState->iSelectionEnd = UTF8GetLength(pText->pState->pchText);
		pText->pState->iCursor = pText->pState->iSelectionEnd;
		if (pText->pState->iSelectionEnd == pText->pState->iSelectionStart)
			TextBuffer_SelectionClear(pText);
	}
}

void TextBuffer_SelectionClear(TextBuffer *pText)
{
	pText->bLastOpWasSimpleInsert = false;
	TextBuffer_SetSelectionBounds(pText, 0, 0);
}

void TextBuffer_SelectAll(TextBuffer *pText)
{
	TEXT_BUFFER_FORCE_STRING(pText->pState)
	{
		pText->bLastOpWasSimpleInsert = false;
		TextBuffer_SetSelectionBounds(pText, 0, UTF8GetLength(pText->pState->pchText));
	}
}

//////////////////////////////////////////////////////////////////////////
// History management

bool TextBuffer_Undo(TextBuffer *pText)
{
	TextBufferState *pState = eaPop(&pText->eaUndo);
	pText->bLastOpWasSimpleInsert = false;
	if (pState)
	{
		eaPush(&pText->eaRedo, pText->pState);
		pText->pState = pState;
	}
	return !!pState;
}

bool TextBuffer_Redo(TextBuffer *pText)
{
	TextBufferState *pState = eaPop(&pText->eaRedo);
	pText->bLastOpWasSimpleInsert = false;
	if (pState)
	{
		eaPush(&pText->eaUndo, pText->pState);
		pText->pState = pState;
	}
	return !!pState;
}

void TextBuffer_Checkpoint(TextBuffer *pText)
{
	TEXT_BUFFER_FORCE_STRING(pText->pState)
	{
		if (!pText->bLastOpWasSimpleInsert)
		{
			if (eaSize(&pText->eaUndo) < pText->iUndoStackSize)
			{
				TextBufferState *pOldState = StructClone(parse_TextBufferState, pText->pState);
				if (pOldState && !pOldState->pchText)
					estrCreate(&pOldState->pchText);
				eaPush(&pText->eaUndo, pOldState);
			}
			else
			{
				// Rather than make a new one and destroy the old one, shift the undo stack over
				// to avoid memory churn.
				TextBufferState *pState = eaRemove(&pText->eaUndo, 0);
				StructCopy(parse_TextBufferState, pText->pState, pState, 0, 0, 0);
				eaPush(&pText->eaUndo, pState);

			}
			TextBuffer_EnforceUndoStackSize(pText);
			eaDestroyStruct(&pText->eaRedo, parse_TextBufferState);
		}
	}
}

void TextBuffer_ClearHistory(TextBuffer *pText)
{
	eaDestroyStruct(&pText->eaUndo, parse_TextBufferState);
	eaDestroyStruct(&pText->eaRedo, parse_TextBufferState);
	pText->bLastOpWasSimpleInsert = false;
}

//////////////////////////////////////////////////////////////////////////
// Clipboard management

const unsigned char *TextBuffer_GetClipboard(TextBuffer *pText)
{
	if (!pText->bPrivateClipboard)
		estrCopy2(&pText->pchClipboard, winCopyUTF8FromClipboard());
	if (pText->cpReplacement != TEXTBUFFER_REPLACEMENT_NONE && pText->cbCodepointValidate && pText->pchClipboard && pText->pchClipboard[0])
	{
		U32 i = 0;
		while (pText->pchClipboard[i])
		{
			unsigned char *pchCodepoint = &pText->pchClipboard[i];
			U32 iCodepointLength = UTF8GetCodepointLength(pchCodepoint);
			U32 cp = UTF8ToWideCharConvert(pchCodepoint);
			if (!pText->cbCodepointValidate(pText, pText->pchClipboard, cp))
			{
				estrRemove(&pText->pchClipboard, i, iCodepointLength);
				if (pText->cpReplacement != TEXTBUFFER_REPLACEMENT_DELETE)
				{
					char achTemp[2] = {0};
					const char *pchBytes = achTemp;
					if (pText->cpReplacement <= 127)
					{
						achTemp[0] = pText->cpReplacement;
						iCodepointLength = 1;
					}
					else
					{
						pchBytes = WideToUTF8CodepointConvert(cp);
						iCodepointLength = (S32)strlen(pchBytes);
					}
					estrInsert(&pText->pchClipboard, i, pchBytes, iCodepointLength);
				}
				else
					iCodepointLength = 0;
			}
			i += iCodepointLength;
		}
	}
	return pText->pchClipboard ? pText->pchClipboard : "";
}

static const unsigned char *TextBuffer_SetClipboard(TextBuffer *pText, const unsigned char *pchText)
{
	estrCopy2(&pText->pchClipboard, pchText);
	if (!pText->bPrivateClipboard)
		winCopyUTF8ToClipboard(pText->pchClipboard);
	return pText->pchClipboard;
}

void TextBuffer_UseSystemClipboard(TextBuffer *pText, bool bUseSystemClipboard)
{
	pText->bPrivateClipboard = !bUseSystemClipboard;
}

const unsigned char *TextBuffer_Copy(TextBuffer *pText)
{
	unsigned char *pchSelection = NULL;
	pText->bLastOpWasSimpleInsert = false;
	estrStackCreate(&pchSelection);
	TextBuffer_GetSelection(pText, &pchSelection, NULL, NULL);
	TextBuffer_SetClipboard(pText, pchSelection);
	estrDestroy(&pchSelection);
	return pText->pchClipboard;
}

const unsigned char *TextBuffer_Cut(TextBuffer *pText)
{
	pText->bLastOpWasSimpleInsert = false;
	TextBuffer_Copy(pText);
	TextBuffer_SelectionDelete(pText);
	return pText->pchClipboard;
}

// Paste from the clipboard to the buffer.
const unsigned char *TextBuffer_Paste(TextBuffer *pText)
{
	const unsigned char *pchClipboard = TextBuffer_GetClipboard(pText);
	S32 iCursor;
	pText->bLastOpWasSimpleInsert = false;
	TextBuffer_SelectionDelete(pText);
	iCursor = TextBuffer_InsertText(pText, TextBuffer_GetCursor(pText), pchClipboard);
	TextBuffer_MoveCursor(pText, iCursor);
	return pText->pchClipboard;
}

// Paste from the clipboard to the buffer.
const unsigned char *TextBuffer_PasteLine(TextBuffer *pText)
{
	const unsigned char *pchClipboard = TextBuffer_GetClipboard(pText);
	unsigned char *pchFixup = pText->pchClipboard;
	S32 iCursor;
	
	if(!pchFixup)
	{
		return NULL;
	}
	
	for (; *pchFixup; pchFixup++)
	{
		if (*pchFixup == '\r' || *pchFixup == '\n')
		{
			*pchFixup = '\0';
			break;
		}
	}
	pText->bLastOpWasSimpleInsert = false;
	TextBuffer_SelectionDelete(pText);
	iCursor = TextBuffer_InsertText(pText, TextBuffer_GetCursor(pText), pchClipboard);
	TextBuffer_MoveCursor(pText, iCursor);
	return pText->pchClipboard;
}

//////////////////////////////////////////////////////////////////////////
// Text-editor like operations.

S32 TextBuffer_InsertTextAtCursor(TextBuffer *pText, const unsigned char *pchText)
{
	S32 iRet;
	TextBuffer_SelectionDelete(pText);
	iRet = TextBuffer_InsertText(pText, TextBuffer_GetCursor(pText), pchText);
	TextBuffer_MoveCursor(pText, iRet);
	pText->bLastOpWasSimpleInsert = true;
	return iRet;
}

bool TextBuffer_InsertChar(TextBuffer *pText, const unsigned char ch)
{
	TEXT_BUFFER_FORCE_STRING(pText->pState)
	{
		if (!pText->cbCodepointValidate || pText->cbCodepointValidate(pText, pText->pState->pchText, ch))
		{
			char ach[2] = {ch, '\0'};
			if (UTF8StringIsValid(ach, NULL))
				return TextBuffer_InsertTextAtCursor(pText, ach);
		}
		return false;
	}
}

static bool TextBuffer_CanInsert(TextBuffer *pText, wchar_t wch)
{
	TEXT_BUFFER_FORCE_STRING(pText->pState)
	{
		bool bValidated = !pText->cbCodepointValidate || pText->cbCodepointValidate(pText, pText->pState->pchText, wch);
		bool bShort = pText->iMaxLength < 0 || (S32)UTF8GetLength(pText->pState->pchText) < pText->iMaxLength;
		return bValidated && (bShort || pText->pState->iSelectionEnd != pText->pState->iSelectionStart);
	}
}

bool TextBuffer_InsertCodepoint(TextBuffer *pText, wchar_t wch)
{
	if (TextBuffer_CanInsert(pText, wch))
	{
		const char *pch = WideToUTF8CodepointConvert(wch);
		return TextBuffer_InsertTextAtCursor(pText, pch);
	}
	else
		return false;
}

bool TextBuffer_Backspace(TextBuffer *pText)
{
	if (TextBuffer_GetSelection(pText, NULL, NULL, NULL))
	{
		TextBuffer_SelectionDelete(pText);
		return true;
	}
	if (pText->pState->iCursor > 0)
	{
		TextBuffer_Checkpoint(pText);
		TextBuffer_MoveCursor(pText, -TextBuffer_RemoveText(pText, pText->pState->iCursor - 1, 1));
		TextBufferState_BoundsCheck(pText->pState);
		return true;
	}
	else
		return false;
}

bool TextBuffer_Delete(TextBuffer *pText)
{
	TEXT_BUFFER_FORCE_STRING(pText->pState)
	{
		if (TextBuffer_GetSelection(pText, NULL, NULL, NULL))
		{
			TextBuffer_SelectionDelete(pText);
			return true;
		}
		else if ((U32)pText->pState->iCursor < UTF8GetLength(pText->pState->pchText))
		{
			TextBuffer_Checkpoint(pText);
			TextBuffer_RemoveText(pText, pText->pState->iCursor, 1);
			return true;
		}
		else
			return false;
	}
}

bool TextBuffer_DeletePreviousWordDelim(TextBuffer *pText, SA_PARAM_OP_STR const char *pchDelimiter)
{
	TEXT_BUFFER_FORCE_STRING(pText->pState)
	{
		if (pText->pState->iCursor > 0)
		{
			S32 iPrevious = UTF8CodepointOfPreviousWordDelim(pText->pState->pchText, pText->pState->iCursor, pchDelimiter);
			TextBuffer_Checkpoint(pText);
			TextBuffer_RemoveText(pText, iPrevious, pText->pState->iCursor - iPrevious);
			TextBuffer_SetCursor(pText, iPrevious);
			return true;
		}
		else
			return false;
	}
}

bool TextBuffer_DeleteNextWordDelim(TextBuffer *pText, SA_PARAM_OP_STR const char *pchDelimiter)
{
	TEXT_BUFFER_FORCE_STRING(pText->pState)
	{
		if ((U32)pText->pState->iCursor < UTF8GetLength(pText->pState->pchText))
		{
			S32 iNext = UTF8CodepointOfNextWordDelim(pText->pState->pchText, pText->pState->iCursor, pchDelimiter);
			TextBuffer_Checkpoint(pText);
			TextBuffer_RemoveText(pText, pText->pState->iCursor, iNext - pText->pState->iCursor);
			TextBufferState_BoundsCheck(pText->pState);
			return true;
		}
		else
			return false;
	}
}

SA_RET_NN_STR const unsigned char *TextBuffer_GetPreviousWordDelim(SA_PARAM_NN_VALID TextBuffer *pText, SA_PARAM_OP_STR const char *pchDelimiters) {
	static char *pchTmp = NULL;
	estrClear(&pchTmp);

	TEXT_BUFFER_FORCE_STRING(pText->pState)
	{
		if (pText->pState->iCursor > 0)
		{
			U32 iPrevious = UTF8CodepointOfPreviousWordDelim(pText->pState->pchText, pText->pState->iCursor, pchDelimiters);
			U32 iPreviousEnd = UTF8CodepointOfWordEndDelim(pText->pState->pchText, iPrevious, pchDelimiters);
			const char *pchPrev = UTF8GetCodepoint(pText->pState->pchText, iPrevious);
			const char *pchPrevEnd = UTF8GetCodepoint(pText->pState->pchText, iPreviousEnd);
			S32 iPreviousEndLength = pchPrevEnd ? UTF8GetCodepointLength(pchPrevEnd) : 0;
			S32 iRawLength = pchPrevEnd - pchPrev + iPreviousEndLength;
			if (iRawLength > 0) {
				estrConcat(&pchTmp, pchPrev, iRawLength);
			}
		}
	}

	return pchTmp;
}

bool TextBuffer_ReplacePreviousWordDelim(SA_PARAM_NN_VALID TextBuffer *pText, SA_PARAM_NN_VALID const unsigned char *pchText, SA_PARAM_OP_STR const char *pchDelimiters) {	// Note: DeletePreviousWordDelim effectively does the checkpoint for this function
	TEXT_BUFFER_FORCE_STRING(pText->pState)
	{
		if (pText->pState->iCursor > 0)
		{
			S32 iPrevious = UTF8CodepointOfPreviousWordDelim(pText->pState->pchText, pText->pState->iCursor, pchDelimiters);
			S32 iPreviousEnd = UTF8CodepointOfWordEndDelim(pText->pState->pchText, iPrevious, pchDelimiters);
			S32 iInsertedLength;

			TextBuffer_Checkpoint(pText);
			TextBuffer_RemoveText(pText, iPrevious, iPreviousEnd - iPrevious + 1);
			iInsertedLength = TextBuffer_InsertText(pText, iPrevious, pchText);
			TextBuffer_SetCursor(pText, iPrevious + iInsertedLength);

			return true;
		}
	}
	return false;
}

bool TextBuffer_ReplaceRange(SA_PARAM_NN_VALID TextBuffer *pText, S32 iFrom, S32 iTo, SA_PARAM_NN_VALID const unsigned char *pchText) {
	TEXT_BUFFER_FORCE_STRING(pText->pState)
	{
		if (pText->pState->iCursor > 0)
		{
			S32 iInsertedLength;

			// if iTo < 0, then replace to the end of the string
			if (iTo < 0 || iTo >= (S32) UTF8GetLength(pText->pState->pchText)) {
				iTo = UTF8GetLength(pText->pState->pchText) - 1;
			}

			if (iFrom < 0) {
				// If iFrom < 0, then replace from the beginning of the string
				iFrom = 0;
			} else if (iFrom >= (S32) UTF8GetLength(pText->pState->pchText)) {
				// If iFrom > length of string, then append
				iFrom = UTF8GetLength(pText->pState->pchText);
				iTo = iFrom;
			}

			TextBuffer_Checkpoint(pText);
			TextBuffer_RemoveText(pText, iFrom, iTo - iFrom + 1);
			iInsertedLength = TextBuffer_InsertText(pText, iFrom, pchText);
			TextBuffer_SetCursor(pText, iFrom + iInsertedLength);

			return true;
		}
	}
	return false;
}

void TextBuffer_SelectionDelete(TextBuffer *pText)
{
	S32 iStart;
	S32 iEnd;
	TextBuffer_GetSelection(pText, NULL, &iStart, &iEnd);
	TextBuffer_Checkpoint(pText);
	if (iStart != iEnd)
	{
		S32 iRealStart = min(iStart, iEnd);
		S32 iRealEnd = max(iStart, iEnd);
		S32 iLength = iRealEnd - iRealStart;
		iLength = TextBuffer_RemoveText(pText, iRealStart, iLength);
		TextBuffer_SelectionClear(pText);
		if (pText->pState->iCursor > iRealEnd)
			TextBuffer_MoveCursor(pText, -iLength);
		else if (pText->pState->iCursor > iRealStart)
			TextBuffer_SetCursor(pText, iRealStart);
	}
}

//////////////////////////////////////////////////////////////////////////
// Validation

static struct
{
	TextBufferStringValidateCB cbStringValidate;
	TextBufferCodepointValidateCB cbCodepointValidate;
	U32 cpReplacement;
} s_TextBufferValidateCallbacks[] = {
	// kTextBufferValidate_None
	{ NULL, NULL, TEXTBUFFER_REPLACEMENT_NONE },

	// kTextBufferValidate_ASCII
	{ TextBuffer_ValidateString_Ascii, TextBuffer_ValidateCodepoint_Ascii, '?' },

	// kTextBufferValidate_PrintableASCII
	{ TextBuffer_ValidateString_AsciiPrint, TextBuffer_ValidateCodepoint_AsciiPrint, '?' },

	// kTextBufferValidate_Username
	{ TextBuffer_ValidateString_Username, TextBuffer_ValidateCodepoint_Username, TEXTBUFFER_REPLACEMENT_DELETE },

	// kTextBufferValidate_Letters
	{ TextBuffer_ValidateString_Letters, TextBuffer_ValidateCodepoint_Letters, TEXTBUFFER_REPLACEMENT_DELETE },

	// kTextBufferValidate_LettersSpaces
	{ TextBuffer_ValidateString_LettersSpaces, TextBuffer_ValidateCodepoint_LettersSpaces, TEXTBUFFER_REPLACEMENT_DELETE },

	// kTextBufferValidate_Float
	{ TextBuffer_ValidateString_Float, TextBuffer_ValidateCodepoint_Float, TEXTBUFFER_REPLACEMENT_DELETE },

	// kTextBufferValidate_Int
	{ TextBuffer_ValidateString_Int, TextBuffer_ValidateCodepoint_Int, TEXTBUFFER_REPLACEMENT_DELETE },

	// kTextBufferValidate_UnsignedInt
	{ TextBuffer_ValidateString_UnsignedInt, TextBuffer_ValidateCodepoint_UnsignedInt, TEXTBUFFER_REPLACEMENT_DELETE },
};

void TextBuffer_SetValidation(SA_PARAM_NN_VALID TextBuffer *pText, TextBufferValidation eValidate)
{
	if (!devassertmsgf(eValidate >= 0 && eValidate < ARRAY_SIZE(s_TextBufferValidateCallbacks), "Invalid validation type passed to %s, assuming no validation", __FUNCTION__))
	{
		eValidate = kTextBufferValidate_None;
	}
	TextBuffer_SetValidationCallbacksWithReplacement(pText, s_TextBufferValidateCallbacks[eValidate].cbStringValidate, s_TextBufferValidateCallbacks[eValidate].cbCodepointValidate, s_TextBufferValidateCallbacks[eValidate].cpReplacement);
}

void TextBuffer_SetValidationCallbacks(SA_PARAM_NN_VALID TextBuffer *pText, TextBufferStringValidateCB cbStringValidate, TextBufferCodepointValidateCB cbCodepointValidate)
{
	if (pText->cbStringValidate != cbStringValidate || pText->cbCodepointValidate != cbCodepointValidate)
	{
		pText->cbStringValidate = cbStringValidate;
		pText->cbCodepointValidate = cbCodepointValidate;
		pText->cpReplacement = TEXTBUFFER_REPLACEMENT_NONE;
		TextBuffer_Validate(pText);
	}
}

void TextBuffer_SetValidationCallbacksWithReplacement(SA_PARAM_NN_VALID TextBuffer *pText, TextBufferStringValidateCB cbStringValidate, TextBufferCodepointValidateCB cbCodepointValidate, U32 cpReplacement)
{
	if (pText->cbStringValidate != cbStringValidate || pText->cbCodepointValidate != cbCodepointValidate)
	{
		pText->cbStringValidate = cbStringValidate;
		pText->cbCodepointValidate = cbCodepointValidate;
		pText->cpReplacement = cpReplacement;
		TextBuffer_Validate(pText);
	}
}

void TextBuffer_SetMaxLength(SA_PARAM_NN_VALID TextBuffer *pText, S32 iMaxLength)
{
	if (pText->iMaxLength != iMaxLength)
	{
		pText->iMaxLength = iMaxLength;
		TextBuffer_Validate(pText);
	}
}

int TextBuffer_GetMaxLength(SA_PARAM_NN_VALID TextBuffer *pText)
{
	return pText->iMaxLength;
}

void TextBuffer_ValidateString_Ascii(TextBuffer *pBuffer, unsigned char **ppchValidate)
{
	unsigned char *pchValidate = *ppchValidate;
	while (pchValidate && *pchValidate)
	{
		if (*pchValidate > 127)
			*pchValidate = '?';
		pchValidate++;
	}
}

bool TextBuffer_ValidateCodepoint_Ascii(TextBuffer *pBuffer, const unsigned char *pchString, U32 uiCodepoint)
{
	return uiCodepoint < 128;
}

void TextBuffer_ValidateString_AsciiPrint(TextBuffer *pBuffer, unsigned char **ppchValidate)
{
	unsigned char *pchValidate = *ppchValidate;
	while (pchValidate && *pchValidate)
	{
		if (*pchValidate > 127 || !iswprint((wchar_t)*pchValidate))
			*pchValidate = '?';
		pchValidate++;
	}

}

static const char *s_pchUsername = ".-' _";

bool TextBuffer_ValidateCodepoint_AsciiPrint(TextBuffer *pBuffer, const unsigned char *pchString, U32 uiCodepoint)
{
	return uiCodepoint < 128 && iswprint(uiCodepoint);
}

// Only allow [A-Za-z0-9'-. ] and no consecutive spaces or punctuation, but a space and punc together allowed
void TextBuffer_ValidateString_Username(TextBuffer *pBuffer, unsigned char **ppchValidate)
{
	U32 i;
	const char *pchLastPunc = NULL;
	const char *pchLastLastPunc = NULL;
	bool bLastCharRemoved = false;
	U32 iSize = estrLength(ppchValidate);

	//trim leading whitespace
	if ( iSize > 0 )
	{
		U32 iIndex = 0;
		U32 iLeadCount = 0;

		while (iIndex < iSize && IS_WHITESPACE((*ppchValidate)[iIndex]))
		{
			iLeadCount++;
			iIndex++;
		}

		if (iLeadCount)
		{
			estrRemove(ppchValidate, 0, iLeadCount);
		}
	}

	for (	i = 0;
			i < estrLength(ppchValidate);
			bLastCharRemoved ? i : i++)
	{
		const char *pchPunc = strchr(s_pchUsername, (*ppchValidate)[i]);
		if ( !((*ppchValidate)[i] < 127 && (iswalnum((wchar_t)(*ppchValidate)[i]) || pchPunc)) ||
			(pchPunc && pchLastPunc && !iswalnum((wchar_t)*pchPunc) && !iswalnum((wchar_t)*pchLastPunc) && ((*pchPunc != ' ' && *pchLastPunc != ' ') || (*pchPunc == ' ' && *pchLastPunc == ' ') || (pchLastLastPunc && !isalnum(*pchLastLastPunc))) ) 
			|| (pchPunc && *pchPunc == ' ' && i == 0) )
		{
			estrRemove(ppchValidate, i, 1);
			bLastCharRemoved = true;
		}
		else
			bLastCharRemoved = false;
		
		pchLastLastPunc = pchLastPunc;
		pchLastPunc = pchPunc;
	}
}

bool TextBuffer_ValidateCodepoint_Username(TextBuffer *pBuffer, const unsigned char *pchString, U32 uiCodepoint)
{
	const char *pchPunc = strchr(s_pchUsername, uiCodepoint);
	return uiCodepoint < 127 && (iswalnum(uiCodepoint) || pchPunc) && (*pchString || !pchPunc);
}

// Only allow [A-Za-z]
void TextBuffer_ValidateString_Letters(TextBuffer *pBuffer, unsigned char **ppchValidate)
{
	U32 i;
	const char *pchLastPunc = NULL;
	bool bLastCharRemoved = false;
	bool bSpotFound = false;

	for (	i = 0;
		i < estrLength(ppchValidate);
		bLastCharRemoved ? i : i++)
	{
		bLastCharRemoved = false;
		if ((((*ppchValidate)[i] < 'A') || (*ppchValidate)[i] > 'Z') && (((*ppchValidate)[i] < 'a') || (*ppchValidate)[i] > 'z'))
		{
			estrRemove(ppchValidate, i, 1);
			bLastCharRemoved = true;
		}
	}
}

bool TextBuffer_ValidateCodepoint_Letters(TextBuffer *pBuffer, const unsigned char *pchString, U32 uiCodepoint)
{
	return (uiCodepoint >= 'A' && uiCodepoint <= 'Z') || (uiCodepoint >= 'a' && uiCodepoint <= 'z');
}

// Only allow [A-Za-z ] and no consecutive spaces
void TextBuffer_ValidateString_LettersSpaces(TextBuffer *pBuffer, unsigned char **ppchValidate)
{
	U32 i;
	const char *pchLastPunc = NULL;
	bool bLastCharRemoved = false;
	bool bSpotFound = false;

	for (	i = 0;
		i < estrLength(ppchValidate);
		bLastCharRemoved ? i : i++)
	{
		bLastCharRemoved = false;
		if ((((*ppchValidate)[i] < 'A') || (*ppchValidate)[i] > 'Z') && (((*ppchValidate)[i] < 'a') || (*ppchValidate)[i] > 'z') && ((*ppchValidate)[i] != ' '))
		{
			estrRemove(ppchValidate, i, 1);
			bLastCharRemoved = true;
		}
		if ((*ppchValidate)[i] == ' ' && i > 0)
		{
			if ((*ppchValidate)[(i-1)] == ' ')
			{
				estrRemove(ppchValidate, i, 1);
				bLastCharRemoved = true;
			}
		}
	}
}

bool TextBuffer_ValidateCodepoint_LettersSpaces(TextBuffer *pBuffer, const unsigned char *pchString, U32 uiCodepoint)
{
	size_t len = strlen(pchString);
	return (uiCodepoint >= 'A' && uiCodepoint <= 'Z') || (uiCodepoint >= 'a' && uiCodepoint <= 'z') || (uiCodepoint == ' ' && ((!len) || pchString[(len-1)] != ' '));
}

void TextBuffer_ValidateString_Float(TextBuffer *pBuffer, unsigned char **ppchValidate)
{
	TextBuffer_ValidateString_NumericEx(pBuffer, ppchValidate, true, true);
}
void TextBuffer_ValidateString_Int(TextBuffer *pBuffer, unsigned char **ppchValidate)
{
	TextBuffer_ValidateString_NumericEx(pBuffer, ppchValidate, true, false);
}
void TextBuffer_ValidateString_UnsignedInt(TextBuffer *pBuffer, unsigned char **ppchValidate)
{
	TextBuffer_ValidateString_NumericEx(pBuffer, ppchValidate, false, false);
}

static const char *s_pchNumeric = ".-";

void TextBuffer_ValidateString_NumericEx(TextBuffer *pBuffer, unsigned char **ppchValidate, bool bSigned, bool bFloatingPoint)
{
	U32 i;
	const char *pchLastPunc = NULL;
	bool bLastCharRemoved = false;
	bool bSpotFound = false;

	for (	i = 0;
			i < estrLength(ppchValidate);
			bLastCharRemoved ? i : i++)
	{
		const char *pchPunc = strchr(s_pchNumeric, (*ppchValidate)[i]);
		if(!((*ppchValidate)[i] < 127 && (iswdigit((wchar_t)(*ppchValidate)[i]) || pchPunc)) ||
			((*ppchValidate)[i] == '.' && (bSpotFound || !bFloatingPoint)) ||
			((*ppchValidate)[i] == '-' && (i != 0 || !bSigned)) )
		{
			estrRemove(ppchValidate, i, 1);
			bLastCharRemoved = true;
		}
		else
		{
			if((*ppchValidate)[i] == '.')
				bSpotFound = true;
			bLastCharRemoved = false;
		}
	}
}

bool TextBuffer_ValidateCodepoint_Float(TextBuffer *pBuffer, const unsigned char *pchString, U32 uiCodepoint)
{
	return TextBuffer_ValidateCodepoint_NumericEx(pBuffer, pchString, uiCodepoint, true, true);
}
bool TextBuffer_ValidateCodepoint_Int(TextBuffer *pBuffer, const unsigned char *pchString, U32 uiCodepoint)
{
	return TextBuffer_ValidateCodepoint_NumericEx(pBuffer, pchString, uiCodepoint, true, false);
}
bool TextBuffer_ValidateCodepoint_UnsignedInt(TextBuffer *pBuffer, const unsigned char *pchString, U32 uiCodepoint)
{
	return TextBuffer_ValidateCodepoint_NumericEx(pBuffer, pchString, uiCodepoint, false, false);
}

bool TextBuffer_ValidateCodepoint_NumericEx(TextBuffer *pBuffer, const unsigned char *pchString, U32 uiCodepoint, bool bSigned, bool bFloatingPoint)
{
	const char *pchSpot = bFloatingPoint ? strchr(pchString, '.') : NULL;
	return uiCodepoint < 127 && (iswdigit(uiCodepoint) || (bFloatingPoint && !pchSpot && uiCodepoint == '.') || (bSigned && !(*pchString) && uiCodepoint == '-'));
}


#include "TextBuffer_c_ast.c"
#include "TextBuffer_h_ast.c"
