#ifndef UI_GEN_TEXT_AREA_H
#define UI_GEN_TEXT_AREA_H
#pragma once
GCC_SYSTEM

#include "UIGen.h"
#include "UIGenScrollbar.h"

typedef struct ChatData ChatData;
typedef struct TextBuffer TextBuffer;

AUTO_STRUCT AST_FOR_ALL(WIKI(AUTO));
typedef struct UIGenTextArea
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeTextArea))
	UIGenScrollbar scrollbar; AST(EMBEDDED_FLAT)

	// The Text/TextExpr in a text Area what is displayed when the text box is empty.
	// TODO: Implement alignment in text entries.
	UIGenBundleText TextBundle; AST(EMBEDDED_FLAT)
	REF_TO(Message) hTruncate; AST(NAME(Truncate) NON_NULL_REF)

	UIGenAction *pOnChanged;

	// Set this to 0 to prevent cursor blink.
	F32 fCursorBlinkInterval; AST(DEFAULT(0.5))
	U32 uiSelectionColor; AST(NAME(SelectionColor) DEFAULT(0x669999CC))

	UIGenBundleTexture CursorBundle; AST(EMBEDDED_FLAT(Cursor))

	TextBufferValidation eValidate; AST(NAME(Validate) SUBTABLE(TextBufferValidationEnum))
	S32 iMaxLength; AST(DEFAULT(-1))

	bool bShowCursorWhenUnfocused : 1;
	bool bTintCursorFromFont : 1;

	bool bEditable: 1; AST(DEFAULT(1))

} UIGenTextArea;

AUTO_STRUCT;
typedef struct UIGenTextAreaState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeTextArea))
	UIGenScrollbarState scrollbar;
	TextBuffer *pBuffer; AST(ALWAYS_ALLOC LATEBIND)
	ChatData *pChatData;  // Text meta data
	UIGenBundleTextureState CursorBundle;

	char *pchDefault; AST(ESTRING)
	const char **eaLineBreaks; AST(UNOWNED)

	UIStyleFont *pLastFont; AST(UNOWNED)
	F32 fLastWidth;
	F32 fLastScale;

	F32 fTimer;
	bool bScrollToCursor : 1;
	bool bDirty : 1;
	bool bSelecting : 1;

	bool bTruncating;
	UIGenBundleTruncateState Truncate;

	CBox TextBox; NO_AST
} UIGenTextAreaState;

extern void ui_GenMoveCursorToScreenLocationTextArea(UIGen *pGen, S32 iScreenX, S32 iScreenY);

#endif