#ifndef FCUI_CHAT_BUBBLE_H
#define FCUI_CHAT_BUBBLE_H
GCC_SYSTEM

#include "ReferenceSystem.h"
#include "Color.h"

typedef struct AtlasTex AtlasTex;
typedef struct UIStyleFont UIStyleFont;
typedef struct UITextureAssembly UITextureAssembly;

#define CHAT_BUBBLE_MAX_WIDTH 200
#define CHAT_BUBBLE_MAX_PER_ENT 2

// Bubbles greater than this many feet in the Y axis from the camera, and not
// visible, are not shown. The intent is to not show bubbles for people
// talking on the floor above or below you.
#define CHAT_BUBBLE_FLOOR_DISTANCE 8.f
#define CHAT_BUBBLES_PER_SIDE 1

AUTO_STRUCT AST_FOR_ALL(WIKI(AUTO));
typedef struct ChatBubbleDef
{
	const char *pchName; AST(KEY, STRUCTPARAM)

	REF_TO(UITextureAssembly) hUpper; AST(NAME(Upper) NON_NULL_REF)
	REF_TO(UITextureAssembly) hBottom; AST(NAME(Bottom) NON_NULL_REF)
	REF_TO(UITextureAssembly) hBottomWithoutTail; AST(NAME(BottomWithoutTail) NON_NULL_REF)

	// The font to use for text within this chat bubble.
	REF_TO(UIStyleFont) hFont; AST(NAME(Font) NON_NULL_REF)

	F32 fFadeOutTime; AST(DEFAULT(0.5))
	F32 fMoveUpTime; AST(DEFAULT(0.3))
	F32 fFadeInTime; AST(DEFAULT(0.3))

	F32 fMinWidth;
	F32 fMaxWidth; AST(DEFAULT(200))
	F32 fMinHeight;

	Color4 Colors; AST(STRUCT(parse_Color4))

	const char *pchFilename; AST(CURRENTFILE)
} ChatBubbleDef;

F32 gclChatBubbleDefDraw(ChatBubbleDef *pDef, const char *pchText, F32 fWidth, F32 fX, F32 fY, F32 fZ, F32 fScale, F32 fCreatedAgo, F32 fDestroyIn, bool bVisible, bool bBottom, F32 fDistance);

void gclChatBubbleResetTacks(void);

#endif
