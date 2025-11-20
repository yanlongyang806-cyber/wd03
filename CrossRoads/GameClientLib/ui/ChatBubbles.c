#include "CBox.h"
#include "Color.h"
#include "ResourceManager.h"
#include "GlobalStateMachine.h"

#include "GfxClipper.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"

#include "UIList.h"
#include "UIWindow.h"

#include "GameClientLib.h"
#include "gclBaseStates.h"

#include "UITextureAssembly.h"
#include "ChatBubbles.h"
#include "AutoGen/ChatBubbles_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static DictionaryHandle s_ChatBubbleDict;

static bool s_abBubblesTackedToLeft[CHAT_BUBBLES_PER_SIDE];
static bool s_abBubblesTackedToRight[CHAT_BUBBLES_PER_SIDE];
static S32 s_iScreenLeft, s_iScreenRight, s_iScreenTop, s_iScreenBottom;

extern bool gbNoGraphics;

void gclChatBubbleResetTacks(void)
{
	memset(s_abBubblesTackedToLeft, 0, sizeof(s_abBubblesTackedToLeft));
	memset(s_abBubblesTackedToRight, 0, sizeof(s_abBubblesTackedToLeft));
	ui_ScreenGetBounds(&s_iScreenLeft, &s_iScreenRight, &s_iScreenTop, &s_iScreenBottom);
}

static F32 ChatBubbleDrawSingle(ChatBubbleDef *pBubbleDef,
								UITextureAssembly *pAssembly, UIStyleFont *pFont,
								const char *pchText,
								F32 fX, F32 fY, F32 fZ, F32 fWidth, F32 fScale,
								F32 fDistance, F32 fAlpha)
{
	F32 fTextWidth;
	F32 fTextHeight;
	F32 fLineHeight;
	F32 fHeightDiff = 0;
	unsigned char chAlpha = MIN(255, fAlpha * 255);

	CBox Box;

	if (!pAssembly)
		return fY;

	fLineHeight = ui_StyleFontLineHeight(pFont, fScale);
	fTextWidth = ui_StyleFontWidth(pFont, fScale, pchText);
	MIN1(fWidth, floorf(fTextWidth * 1.1f));
	fWidth = floorf(CLAMP(fWidth, pBubbleDef->fMinWidth, pBubbleDef->fMaxWidth));
	fTextHeight = ui_StyleFontHeightWrapped(pFont, fWidth, fScale, pchText);
	if (fTextHeight < pBubbleDef->fMinHeight)
	{
		fHeightDiff = pBubbleDef->fMinHeight - fTextHeight;
		fTextHeight = pBubbleDef->fMinHeight;
	}
	fTextWidth = fWidth;
	fWidth = fWidth + (pAssembly->iPaddingLeft + pAssembly->iPaddingRight) * fScale;

	if (fX - fWidth / 2 < s_iScreenLeft || fDistance < 0 && fX <= (s_iScreenLeft + s_iScreenRight) / 2)
	{
		S32 iSize = ARRAY_SIZE(s_abBubblesTackedToLeft);
		S32 i;
		fX = fWidth / 2;
		for (i = 0; i < iSize; i++)
		{
			if (!s_abBubblesTackedToLeft[i])
			{
				s_abBubblesTackedToLeft[i] = true;
				fY = s_iScreenTop + (s_iScreenBottom - s_iScreenTop) * (iSize - i) * (1.f / (iSize + 1)) - fTextHeight / 2;
				break;
			}
		}
		if (i == ARRAY_SIZE(s_abBubblesTackedToLeft) && GSM_IsStateActive(GCL_GAMEPLAY))
			return fY;
	}
	else if (fX + fWidth / 2 > s_iScreenRight || fDistance < 0 && fX >= (s_iScreenLeft + s_iScreenRight) / 2)
	{
		S32 iSize = ARRAY_SIZE(s_abBubblesTackedToRight);
		S32 i;
		fX = s_iScreenRight - fWidth / 2;
		for (i = 0; i < iSize; i++)
		{
			if (!s_abBubblesTackedToRight[i])
			{
				s_abBubblesTackedToRight[i] = true;
				fY = s_iScreenTop + (s_iScreenBottom - s_iScreenTop) * (iSize - i) * (1.f / (iSize + 1)) - fTextHeight / 2;
				break;
			}
		}
		if (i == ARRAY_SIZE(s_abBubblesTackedToRight) && GSM_IsStateActive(GCL_GAMEPLAY))
			return fY;
	}

	if (fY > s_iScreenBottom)
	{
		fY = s_iScreenBottom;
	}

	ui_StyleFontUse(pFont, false, kWidgetModifier_None);
	gfxfont_SetAlpha(chAlpha);
	BuildIntCBox(&Box,
		fX - fWidth / 2,
		fY - (fTextHeight + (pAssembly->iPaddingTop + pAssembly->iPaddingBottom) * fScale),
		fWidth,
		fTextHeight + (pAssembly->iPaddingTop + pAssembly->iPaddingBottom) * fScale);
	ui_TextureAssemblyDraw(pAssembly, &Box, NULL, fScale, fZ, fZ + 0.1, chAlpha, &pBubbleDef->Colors);
	gfxfont_PrintWrapped((Box.lx + Box.hx) / 2 + (pAssembly->iPaddingLeft - pAssembly->iPaddingRight), Box.ly + fLineHeight + fHeightDiff / 2 + pAssembly->iPaddingTop * fScale, fZ + 0.2, fTextWidth, fScale, fScale, CENTER_X, true, pchText);
	return Box.ly;
}

F32 gclChatBubbleDefDraw(ChatBubbleDef *pDef, const char *pchText, F32 fWidth, F32 fX, F32 fY, F32 fZ, F32 fScale, F32 fCreatedAgo, F32 fDestroyIn, bool bVisible, bool bBottom, F32 fDistance)
{
	UITextureAssembly *pAssembly;
	F32 fNextY;
	F32 fAlpha;

	if (!pchText || fDestroyIn <= 0 || fCreatedAgo < 0)
		return fY;

	fAlpha = MIN(fDestroyIn / pDef->fFadeOutTime, fCreatedAgo / pDef->fFadeInTime);

	if (bBottom)
	{
		if (bVisible)
			pAssembly = GET_REF(pDef->hBottom);
		else
			pAssembly = GET_REF(pDef->hBottomWithoutTail);
	}
	else
		pAssembly = GET_REF(pDef->hUpper);

	if (fWidth < 0)
		fWidth = pDef->fMaxWidth;
	fNextY = ChatBubbleDrawSingle(pDef, pAssembly, GET_REF(pDef->hFont), pchText, fX, fY, fZ, fWidth, fScale, fDistance, fAlpha);
	return (fY + (fNextY - fY) * MIN(1.f, fCreatedAgo / pDef->fMoveUpTime));
}

int gclChatBubbleDefResourceValidate(enumResourceEventType eType, const char *pDictName, const char *pBubbleName, ChatBubbleDef *pBubble, U32 iUserID)
{
	return VALIDATE_NOT_HANDLED;
}

AUTO_STARTUP(GameUI) ASTRT_DEPS(UILib);
void gclChatBubbleDefLoad(void)
{
	s_ChatBubbleDict = RefSystem_RegisterSelfDefiningDictionary("ChatBubbleDef", false, parse_ChatBubbleDef, true, true, NULL);

	if(!gbNoGraphics)
	{
		resDictManageValidation(s_ChatBubbleDict, gclChatBubbleDefResourceValidate);
		resLoadResourcesFromDisk(s_ChatBubbleDict, "ui/chatbubbles", ".chatbubble", "ChatBubbles.bin", PARSER_CLIENTSIDE);
	}
}

//////////////////////////////////////////////////////////////////////////
// Debug tool, browse available bubbles.

typedef struct ChatBubblePane
{
	UIWidget widget;
	REF_TO(ChatBubbleDef) hDef;
} ChatBubblePane;

static void ChatBubblePaneFree(ChatBubblePane *pPane)
{
	REMOVE_HANDLE(pPane->hDef);
	ui_WidgetFreeInternal(UI_WIDGET(pPane));
}

static void ChatBubblePaneDraw(ChatBubblePane *pPane, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pPane);
	const char *apchMessage[] = {"sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.", "Lorem ipsum dolor sit amet, consectetur adipisicing elit,"};
	ChatBubbleDef *pDef = GET_REF(pPane->hDef);
	F32 fTime = (gGCLState.totalElapsedTimeMs - (U32)(gGCLState.totalElapsedTimeMs / 15000) * 15000) / 1000.0;
	F32 afCreatedAgo[] = {5, 5};
	F32 afDestroyIn[] = {5, 5};
	UI_DRAW_EARLY(pPane);

	if (fTime < 5)
	{
		afCreatedAgo[0] = fTime;
		afDestroyIn[0] = 10 - fTime;
		apchMessage[0] = apchMessage[1];
		apchMessage[1] = NULL;
	}
	else
	{
		afCreatedAgo[0] = fTime - 5;
		afCreatedAgo[1] = fTime;
		afDestroyIn[0] = 15 - fTime;
		afDestroyIn[1] = 10 - fTime;
	}

	gclChatBubbleResetTacks();
	if (pDef)
	{
		F32 fBubbleWidth = CLAMP(0.8 * w, pDef->fMinWidth, pDef->fMaxWidth);
		F32 fZ = UI_GET_Z();
		F32 fY = gclChatBubbleDefDraw(pDef, apchMessage[0], fBubbleWidth, x + w / 2, y + h - UI_STEP_SC, fZ, scale, afCreatedAgo[0], afDestroyIn[0], true, true, 1);
		if (apchMessage[1])
		{
			gclChatBubbleDefDraw(pDef, apchMessage[1], fBubbleWidth, x + w / 2, fY, fZ, scale, afCreatedAgo[1], afDestroyIn[1], true, false, 1);
		}
	}
	UI_DRAW_LATE(pPane);
}

static ChatBubblePane *ChatBubblePaneCreate(void)
{
	ChatBubblePane *pPane = calloc(1, sizeof(ChatBubblePane));
	ui_WidgetInitialize(UI_WIDGET(pPane), NULL, ChatBubblePaneDraw, ChatBubblePaneFree, NULL, NULL);
	return pPane;
}

static void ChatBubblePaneSetRef(UIList *pList, ChatBubblePane *pPane)
{
	ChatBubbleDef *pDef = ui_ListGetSelectedObject(pList);
	if (pDef)
		SET_HANDLE_FROM_STRING(s_ChatBubbleDict, pDef->pchName, pPane->hDef);
}

// View all currently loaded chat bubble definitions
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);
void ChatBubbleBrowser(void)
{
	UIWindow *pWindow = ui_WindowCreate("Chat Bubbles", 0, 0, 300, 200);
	DictionaryEArrayStruct *pBubbleArray = resDictGetEArrayStruct(s_ChatBubbleDict);
	UIList *pList = ui_ListCreate(parse_ChatBubbleDef, &pBubbleArray->ppReferents, 20);
	ChatBubblePane *pPane = ChatBubblePaneCreate();
	ui_ListAppendColumn(pList, ui_ListColumnCreate(UIListPTName, "Name", (intptr_t)"Name", NULL));
	ui_WidgetSetDimensionsEx(UI_WIDGET(pList), 0.33f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pPane), 0.67f, 1.f, UIUnitPercentage, UIUnitPercentage);
	UI_WIDGET(pPane)->xPOffset = 0.33f;

	ui_WindowAddChild(pWindow, pList);
	ui_WindowAddChild(pWindow, pPane);
	ui_ListSetSelectedCallback(pList, ChatBubblePaneSetRef, pPane);
	ui_WindowSetCloseCallback(pWindow, ui_WindowFreeOnClose, NULL);
	ui_WindowShow(pWindow);
}

#include "ChatBubbles_h_ast.c"
