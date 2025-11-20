#ifndef TEXT_BUFFER_H
#define TEXT_BUFFER_H
#pragma once
GCC_SYSTEM

#define TEXT_BUFFER_UNDO_STACK_SIZE 20

// A TextBuffer is a suitable backend for any situation where interactive
// text entry is required. It is used by many UI widgets.
//
// To avoid a seemingly endless parade of Unicode-related errors, the TextBuffer
// structure is not public, and is mediated entirely by accessor functions which
// (hopefully) ensure proper UTF-8 support. As a result the API is large.
//
// All strings are UTF-8 and all offsets are in codepoints.
//
// Features include:
//  * Selection and cursor position
//  * Per-TextBuffer clipboards and Win32 clipboard support
//  * Undo/redo support
//  * textparsered.
//  * Validation functions and length clamping

//////////////////////////////////////////////////////////////////////////
// TODO:
// * Byte size limits as well as codepoint limits

typedef struct TextBuffer TextBuffer;

extern ParseTable parse_TextBuffer[];
#define TYPE_parse_TextBuffer TextBuffer

// Called whenever the text in the buffer changes. ppchValidate is an EString that should
// be changed to whatever is valid for this particular entry.
typedef void (*TextBufferStringValidateCB)(TextBuffer *pBuffer, unsigned char **ppchValidate);

// Called preemptively when a single codepoint is to be inserted, if it returns false
// the codepoint will not be inserted. This lets you preemptively reject codepoints that
// would fail the StringValidate step.
typedef bool (*TextBufferCodepointValidateCB)(TextBuffer *pBuffer, const unsigned char *ppchString, U32 uiCodepoint);

//////////////////////////////////////////////////////////////////////////
// Initialization/destruction functions.

// Create a new TextBuffer object with the given UndoStackSize. Note that the
// undo stack memory usage grows linearly with the size of strings being handled,
// so expected short strings can have a larger undo stack, and vice versa.
// It is also safe to use StructCreate.
SA_RET_NN_VALID TextBuffer *TextBuffer_CreateEx(SA_PARAM_OP_STR const unsigned char *pchInitial, S32 iUndoStackSize);
#define TextBuffer_Create(pchInitial) TextBuffer_CreateEx(pchInitial, TEXT_BUFFER_UNDO_STACK_SIZE)

// Destroy a TextBuffer object. It is also safe to call StructDestroy.
void TextBuffer_Destroy(SA_PRE_NN_VALID SA_POST_P_FREE TextBuffer *pText);
void TextBuffer_DestroySafe(SA_PRE_NN_VALID SA_POST_FREE TextBuffer **ppText);

// Return the text within the buffer. This pointer is only valid until the next edit operation.
SA_RET_NN_STR const unsigned char *TextBuffer_GetText(SA_PARAM_NN_VALID TextBuffer *pText);

// Return the text within the buffer, filtered for profanity. This pointer is only valid until the next edit operation.
SA_RET_NN_STR const unsigned char *TextBuffer_GetProfanityFilteredText(SA_PARAM_NN_VALID TextBuffer *pText, bool bApplyFilter);

// Copy the text within the buffer to another EString.
void TextBuffer_CopyText(SA_PARAM_NN_VALID TextBuffer *pText, SA_PRE_NN_NN_STR unsigned char **ppchText);

// Set the text within the buffer. If the given string is not valid UTF-8, this function
// returns false and the string remains unchanged.
bool TextBuffer_SetText(SA_PARAM_NN_VALID TextBuffer *pText, SA_PARAM_OP_STR const unsigned char *pchText);

// Delete text from the buffer. Returns the number of characters actually deleted.
S32 TextBuffer_RemoveText(SA_PARAM_NN_VALID TextBuffer *pText, S32 iStart, S32 iLength);

// Insert text into the buffer. Returns the number of characters actually inserted.
S32 TextBuffer_InsertText(SA_PARAM_NN_VALID TextBuffer *pText, S32 iOffset, SA_PARAM_OP_STR const unsigned char *pchText);

// Trim the leading and trailing whitespace off of this buffer
void TextBuffer_TrimLeadingAndTrailingWhitespace(SA_PARAM_NN_VALID TextBuffer *pText);

//////////////////////////////////////////////////////////////////////////
// Cursor management

// Get the current cursor position.
S32 TextBuffer_GetCursor(SA_PARAM_NN_VALID TextBuffer *pText);

// Set the current cursor position. Return the actually-set position.
// If iCursor is negative, it is relative to the end of the string.
S32 TextBuffer_SetCursor(SA_PARAM_NN_VALID TextBuffer *pText, S32 iCursor);

// Move the cursor Previous or Next by iCursor characters. Return the new position.
S32 TextBuffer_MoveCursor(SA_PARAM_NN_VALID TextBuffer *pText, S32 iCursor);

// Move the cursor to the end of the buffer.
S32 TextBuffer_CursorToEnd(SA_PARAM_NN_VALID TextBuffer *pText);

S32 TextBuffer_CursorToNextWord(SA_PARAM_NN_VALID TextBuffer *pText);
S32 TextBuffer_CursorToPreviousWord(SA_PARAM_NN_VALID TextBuffer *pText);

//////////////////////////////////////////////////////////////////////////
// Selection management
//
// Selection is managed as a pair of integers, a start and end position.
// This does not necessarily correspond to the min/max of the selection,
// as things can be selected backwards.
//
// An unselected state is represented by Start == End == 0.
//
// Selection depends on and may change cursor position.

bool TextBuffer_GetSelection(SA_PARAM_NN_VALID TextBuffer *pText, unsigned char **ppchSelection, S32 *piStart, S32 *piEnd);
void TextBuffer_SetSelection(SA_PARAM_NN_VALID TextBuffer *pText, S32 iStart, S32 iLength);
void TextBuffer_SetSelectionBounds(SA_PARAM_NN_VALID TextBuffer *pText, S32 iStart, S32 iEnd);
void TextBuffer_SelectToPos(SA_PARAM_NN_VALID TextBuffer *pText, S32 iPos);

// Move the selection endpoint to the next or previous character.
void TextBuffer_SelectionPrevious(SA_PARAM_NN_VALID TextBuffer *pText);
void TextBuffer_SelectionNext(SA_PARAM_NN_VALID TextBuffer *pText);

// Move the selection endpoint a word at a time.
void TextBuffer_SelectionPreviousWord(SA_PARAM_NN_VALID TextBuffer *pText);
void TextBuffer_SelectionNextWord(SA_PARAM_NN_VALID TextBuffer *pText);

// Move the selection endpoint to the start or end of the text.
void TextBuffer_SelectionToStart(SA_PARAM_NN_VALID TextBuffer *pText);
void TextBuffer_SelectionToEnd(SA_PARAM_NN_VALID TextBuffer *pText);

// Unselect everything
void TextBuffer_SelectionClear(SA_PARAM_NN_VALID TextBuffer *pText);

// Select everything.
void TextBuffer_SelectAll(SA_PARAM_NN_VALID TextBuffer *pText);

// Delete the current selection.
void TextBuffer_SelectionDelete(SA_PARAM_NN_VALID TextBuffer *pText);

S32 TextBuffer_GetSelectionStart(SA_PARAM_NN_VALID TextBuffer *pText);
S32 TextBuffer_GetSelectionEnd(SA_PARAM_NN_VALID TextBuffer *pText);

//////////////////////////////////////////////////////////////////////////
// History management
//
// History depends on and may change selection and cursor position.

// Undo the last action. Returns true if anything actually changed.
bool TextBuffer_Undo(SA_PARAM_NN_VALID TextBuffer *pText);

// Redo the last undo. Returns true if anything actually changed.
bool TextBuffer_Redo(SA_PARAM_NN_VALID TextBuffer *pText);

// Push the current state onto the undo stack.
void TextBuffer_Checkpoint(SA_PARAM_NN_VALID TextBuffer *pText);

// Clear all history.
void TextBuffer_ClearHistory(SA_PARAM_NN_VALID TextBuffer *pText);

//////////////////////////////////////////////////////////////////////////
// Clipboard management
//
// The clipboard depends on and may change selection, cursor position, and history.

// If UseSystemClipboard is false, the TextBuffer maintains its own clipboard,
// which other clipboard functions will use. Otherwise, it uses the system
// clipboard. The default is to use the system clipboard.
void TextBuffer_UseSystemClipboard(SA_PARAM_NN_VALID TextBuffer *pText, bool bUseSystemClipboard);

// Cut the current selection to the clipboard. Return the text cut, the pointer
// is good until the next clipboard operation on this buffer.
const unsigned char *TextBuffer_Cut(SA_PARAM_NN_VALID TextBuffer *pText);

// Copy the current selection to the clipboard. Return the text copied, the pointer
// is good until the next clipboard operation on this buffer.
const unsigned char *TextBuffer_Copy(SA_PARAM_NN_VALID TextBuffer *pText);

// Paste from the clipboard to the buffer. Return the text pasted, the pointer
// is good until the next clipboard operation on this buffer.
const unsigned char *TextBuffer_Paste(SA_PARAM_NN_VALID TextBuffer *pText);

// Paste from the clipboard to the buffer. Return the text pasted, the pointer
// is good until the next clipboard operation on this buffer. This will only paste
// the first line in the clipboard.
const unsigned char *TextBuffer_PasteLine(TextBuffer *pText);

// Get a pointer to the clipboard text. This is what would get pasted.
const unsigned char *TextBuffer_GetClipboard(TextBuffer *pText);

//////////////////////////////////////////////////////////////////////////
// Text-editor like operations. These all manipulate the cursor, history,
// and selection in "smart" ways.
//
// These depend on and may change selection, cursor position, and history.

S32 TextBuffer_InsertTextAtCursor(SA_PARAM_NN_VALID TextBuffer *pText, SA_PARAM_NN_VALID const unsigned char *pchText);
bool TextBuffer_InsertChar(SA_PARAM_NN_VALID TextBuffer *pText, SA_PARAM_OP_VALID const unsigned char ch);
bool TextBuffer_InsertCodepoint(SA_PARAM_NN_VALID TextBuffer *pText, SA_PARAM_OP_VALID wchar_t wch);
bool TextBuffer_Backspace(SA_PARAM_NN_VALID TextBuffer *pText);
bool TextBuffer_Delete(SA_PARAM_NN_VALID TextBuffer *pText);

bool TextBuffer_DeletePreviousWordDelim(SA_PARAM_NN_VALID TextBuffer *pText, SA_PARAM_OP_STR const char *pchDelimiters);
#define TextBuffer_DeletePreviousWord(pTextBuffer) TextBuffer_DeletePreviousWordDelim((pTextBuffer), NULL)

bool TextBuffer_DeleteNextWordDelim(SA_PARAM_NN_VALID TextBuffer *pText, SA_PARAM_OP_STR const char *pchDelimiters);
#define TextBuffer_DeleteNextWord(pTextBuffer) TextBuffer_DeleteNextWordDelim((pTextBuffer), NULL)

SA_RET_NN_STR const unsigned char *TextBuffer_GetPreviousWordDelim(SA_PARAM_NN_VALID TextBuffer *pText, SA_PARAM_OP_STR const char *pchDelimiters);
bool TextBuffer_ReplacePreviousWordDelim(SA_PARAM_NN_VALID TextBuffer *pText, SA_PARAM_NN_VALID const unsigned char *pchText, SA_PARAM_OP_STR const char *pchDelimiters);
bool TextBuffer_ReplaceRange(SA_PARAM_NN_VALID TextBuffer *pText, S32 iFrom, S32 iTo, SA_PARAM_NN_VALID const unsigned char *pchText);

//////////////////////////////////////////////////////////////////////////
// Validation functions
//
// You can set a validation function, and a maximum length; if the maximum length is < 0, it is
// considered unbounded. The maximum length is in Unicode codepoints, not chars.
void TextBuffer_SetValidationCallbacks(SA_PARAM_NN_VALID TextBuffer *pText, TextBufferStringValidateCB cbStringValidate, TextBufferCodepointValidateCB cbCodepointValidate);
void TextBuffer_SetValidationCallbacksWithReplacement(SA_PARAM_NN_VALID TextBuffer *pText, TextBufferStringValidateCB cbStringValidate, TextBufferCodepointValidateCB cbCodepointValidate, U32 cpReplacement);
void TextBuffer_SetMaxLength(SA_PARAM_NN_VALID TextBuffer *pText, S32 iMaxLength);
int TextBuffer_GetMaxLength(SA_PARAM_NN_VALID TextBuffer *pText);

// Some pre-made validation types that you can pass around by name.
AUTO_ENUM;
typedef enum TextBufferValidation
{
	kTextBufferValidate_None = 0,
	kTextBufferValidate_ASCII,
	kTextBufferValidate_PrintableASCII,
	kTextBufferValidate_Username,
	kTextBufferValidate_Letters,
	kTextBufferValidate_LettersSpaces,
	kTextBufferValidate_Float,
	kTextBufferValidate_Int,
	kTextBufferValidate_UnsignedInt,
	kTextBufferValidate_MAX, EIGNORE
} TextBufferValidation;

#define TEXTBUFFER_REPLACEMENT_NONE 0
#define TEXTBUFFER_REPLACEMENT_DELETE 0xFFFFFFFF

extern StaticDefineInt TextBufferValidationEnum[];

void TextBuffer_SetValidation(SA_PARAM_NN_VALID TextBuffer *pText, TextBufferValidation eValidate);

// Only allow ASCII characters, i.e. codepoints < 127.
void TextBuffer_ValidateString_Ascii(TextBuffer *pBuffer, unsigned char **ppchValidate);
bool TextBuffer_ValidateCodepoint_Ascii(TextBuffer *pBuffer, const unsigned char *pchString, U32 uiCodepoint);

// Only allow ASCII printables, i.e. codepoints < 127 and passing isprint.
void TextBuffer_ValidateString_AsciiPrint(TextBuffer *pBuffer, unsigned char **ppchValidate);
bool TextBuffer_ValidateCodepoint_AsciiPrint(TextBuffer *pBuffer, const unsigned char *pchString, U32 uiCodepoint);

// Only allow [A-Za-z0-9'-. ] and no consecutive spaces or punctuation.
void TextBuffer_ValidateString_Username(TextBuffer *pBuffer, unsigned char **ppchValidate);
bool TextBuffer_ValidateCodepoint_Username(TextBuffer *pBuffer, const unsigned char *pchString, U32 uiCodepoint);

// Only allow [A-Za-z]
void TextBuffer_ValidateString_Letters(TextBuffer *pBuffer, unsigned char **ppchValidate);
bool TextBuffer_ValidateCodepoint_Letters(TextBuffer *pBuffer, const unsigned char *pchString, U32 uiCodepoint);

// Only allow [A-Za-z ] and no consecutive spaces
void TextBuffer_ValidateString_LettersSpaces(TextBuffer *pBuffer, unsigned char **ppchValidate);
bool TextBuffer_ValidateCodepoint_LettersSpaces(TextBuffer *pBuffer, const unsigned char *pchString, U32 uiCodepoint);

// Only allow [0-9-.]
void TextBuffer_ValidateString_NumericEx(TextBuffer *pBuffer, unsigned char **ppchValidate, bool bSigned, bool bFloatingPoint);
void TextBuffer_ValidateString_Float(TextBuffer *pBuffer, unsigned char **ppchValidate);
void TextBuffer_ValidateString_Int(TextBuffer *pBuffer, unsigned char **ppchValidate);
void TextBuffer_ValidateString_UnsignedInt(TextBuffer *pBuffer, unsigned char **ppchValidate);
bool TextBuffer_ValidateCodepoint_Float(TextBuffer *pBuffer, const unsigned char *pchString, U32 uiCodepoint);
bool TextBuffer_ValidateCodepoint_UnsignedInt(TextBuffer *pBuffer, const unsigned char *pchString, U32 uiCodepoint);
bool TextBuffer_ValidateCodepoint_Int(TextBuffer *pBuffer, const unsigned char *pchString, U32 uiCodepoint);

bool TextBuffer_ValidateCodepoint_NumericEx(TextBuffer *pBuffer,
											const unsigned char *pchString,
											U32 uiCodepoint,
											bool bSigned,
											bool bFloatingPoint);

#endif
