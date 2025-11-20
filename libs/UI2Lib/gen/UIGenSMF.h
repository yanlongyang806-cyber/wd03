#ifndef UI_GEN_SMF_H
#define UI_GEN_SMF_H
GCC_SYSTEM

#include "UIGenScrollbar.h"

typedef struct TextAttribs TextAttribs;

AUTO_STRUCT;
typedef struct UIGenSMFDefaults
{
	REF_TO(UIStyleFont) hFont; AST(NAME(Font) NON_NULL_REF)
	U32 uiColor; AST(NAME(Color) SUBTABLE(ColorEnum))
	U32 uiShadow; AST(NAME(Shadow))
} UIGenSMFDefaults;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenSMF
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeSMF))
	REF_TO(Message) hText; AST(NAME(Text) NON_NULL_REF)

	UIGenScrollbar scrollbar; AST(EMBEDDED_FLAT)
	Expression *pTextExpr; AST(NAME(TextBlock) REDUNDANT_STRUCT(TextExpr, parse_Expression_StructParam) LATEBIND WIKI(AUTO))
	UIGenSMFDefaults Defaults;
	REF_TO(UIStyleFont) hFont; AST(NAME(Font) NON_NULL_REF)

	// Cannot be a UIGenBundleText because we need a default alignment value.

	U8 eAlignment; AST(DEFAULT(UITopLeft) SUBTABLE(UIDirectionEnum))

	bool bFilterProfanity : 1;
	bool bShrinkToFit : 1; // Shrinks down to fit, won't scale up.
	bool bScaleToFit : 1;  // Shrinks down or scales up to fit.
	bool bNoWrap : 1;      // Don't do any word wrapping
	bool bSafeMode : 1;    // Only allow "safe" SMF tags
	bool bAllowInteract : 1; // Allow clicking href links
} UIGenSMF;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenSMFState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeSMF))
	UIGenScrollbarState scrollbar;
	char *pchStaticString; AST(ESTRING)
	char *pchText; AST(ESTRING)
	SMFBlock *pBlock; NO_AST
	TextAttribs *pAttribs; NO_AST
} UIGenSMFState;

#endif