#pragma once
GCC_SYSTEM

#include "UIGen.h"

typedef struct ChatData ChatData;
typedef struct TextBuffer TextBuffer;

extern StaticDefineInt TextBufferValidationEnum[];

AUTO_STRUCT AST_FOR_ALL(WIKI(AUTO));
typedef struct UIGenTextEntry
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeTextEntry))

	// The Text/TextExpr in a text entry what is displayed when the text box is empty.
	// TODO: Implement alignment in text entries.
	UIGenBundleText TextBundle; AST(EMBEDDED_FLAT)
	REF_TO(Message) hTruncate; AST(NAME(Truncate) NON_NULL_REF)

	UIGenAction *pOnChanged;

	// Set this to 0 to prevent cursor blink.
	F32 fCursorBlinkInterval; AST(DEFAULT(0.5))
	U32 uiSelectionColor; AST(NAME(SelectionColor) DEFAULT(0x669999CC))

	UIGenBundleTexture CursorBundle; AST(EMBEDDED_FLAT(Cursor))

	// Replace characters with the given one.
	const char *pchPassword;

	TextBufferValidation eValidate; AST(NAME(Validate) SUBTABLE(TextBufferValidationEnum))
	S32 iMaxLength; AST(DEFAULT(-1))

	bool bShowCursorWhenUnfocused : 1;
	bool bTintCursorFromFont : 1;

	bool bEditable: 1; AST(DEFAULT(1))

} UIGenTextEntry;

AUTO_STRUCT;
typedef struct UIGenTextEntryState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeTextEntry))
	TextBuffer *pBuffer; AST(ALWAYS_ALLOC LATEBIND)
	ChatData *pChatData; // Text meta data
	UIGenBundleTextureState CursorBundle;

	// The X offset of the start of the text from the start of the box.
	// This is adjusted each frame to help keep the cursor on-screen.
	F32 fTextOffset;
	F32 fTimer;

	bool bSelecting;

	// Text to be displayed this frame, after expression/password processing.
	char *pchString; AST(ESTRING)
	S32 iSelectionStart;
	S32 iSelectionEnd;
	S32 iCursor;
	bool bBufferFilled;
	UIGenBundleTruncateState Truncate;
	Vec2 v2TextSize;
	UIStyleFont *pTextFont; AST(UNOWNED)
} UIGenTextEntryState;

void ui_GenMoveCursorToScreenLocationTextEntry(UIGen *pGen, S32 iScreenX);
