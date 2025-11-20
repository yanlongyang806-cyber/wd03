/***************************************************************************



***************************************************************************/

#include "earray.h"

#include "inputMouse.h"
#include "inputText.h"

#include "GfxClipper.h"
#include "GfxPrimitive.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"
#include "GfxSprite.h"

#include "Message.h"
#include "UIScrollBar.h"
#include "UITabs.h"
#include "textparser.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static Color s_TabBorderColor = {0, 0, 0, 128};

static void ui_TabGroupActiveFillDrawingDescription(UITabGroup* pane, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( pane );
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName;
		if( pane->bVerticalTabs ) {
			descName = skin->astrTabGroupVerticalStyleSelected;
		} else {
			descName = skin->astrTabGroupStyleSelected;
		}
		
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->styleBorderNameUsingLegacyColor = "Default_Capsule_Filled";
	}
}

static void ui_TabGroupInactiveFillDrawingDescription(UITabGroup* pane, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( pane );
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName;
		if( pane->bVerticalTabs ) {
			descName = skin->astrTabGroupVerticalStyle;
		} else {
			descName = skin->astrTabGroupStyle;
		}
		
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->styleBorderNameUsingLegacyColor = "Default_Capsule_Filled";
	}
}

static UIStyleFont* ui_TabGroupGetFont(UITabGroup* pTabs, bool bActive)
{
	UISkin* skin = UI_GET_SKIN( pTabs );
	UIStyleFont* font = NULL;
	
	UIWidget *widget = UI_WIDGET(pTabs);
	if (bActive) {
		font = GET_REF( skin->hTabGroupFontSelected );
	} else {
		font = GET_REF( skin->hTabGroupFont );
	}

	if( !font ) {
		font = ui_WidgetGetFont( UI_WIDGET( pTabs ));
	}

	return font;
}

void ui_TabGroupHeaderSizes(UITabGroup *pTabs, F32 fScale, F32 fWidth, F32 *pfWidth, F32 *pfHeight)
{
	UIStyleFont *pInactiveFont = ui_TabGroupGetFont(pTabs, false);
	UIStyleFont *pActiveFont = ui_TabGroupGetFont(pTabs, true);
	F32 fTabWidth = 0.f;
	S32 i;
	UIDrawingDescription inactiveDesc = { 0 };
	UIDrawingDescription activeDesc = { 0 };
	F32 fMaxBorderWidth, fMaxBorderHeight, fHeight;
	ui_TabGroupInactiveFillDrawingDescription( pTabs, &inactiveDesc );
	ui_TabGroupActiveFillDrawingDescription( pTabs, &activeDesc );
	fMaxBorderWidth = max(ui_DrawingDescriptionWidth(&inactiveDesc), ui_DrawingDescriptionWidth(&activeDesc)) * fScale;
	fMaxBorderHeight = max(ui_DrawingDescriptionHeight(&inactiveDesc), ui_DrawingDescriptionHeight(&activeDesc)) * fScale;
	fHeight = MAX( ui_StyleFontLineHeight(pInactiveFont, fScale), ui_StyleFontLineHeight(pActiveFont, fScale) ) + fMaxBorderHeight;

	if (pTabs->bEqualWidths && pfWidth)
	{
		for (i = 0; i < eaSize(&pTabs->eaTabs); i++)
		{
			F32 fTextWidth;
			if( pTabs->eaTabs[i] == pTabs->pActive) {
				fTextWidth = ui_StyleFontWidth(pActiveFont, fScale, ui_TabGetTitle(pTabs->eaTabs[i]));
			} else {
				fTextWidth = ui_StyleFontWidth(pInactiveFont, fScale, ui_TabGetTitle(pTabs->eaTabs[i]));
			}
			MAX1(fTabWidth, fTextWidth + fMaxBorderWidth + pTabs->fTabXPad * fScale);
		}
		if( fWidth == 0 || fTabWidth == 0 || floorf(fWidth / fTabWidth) == 0 ) {
			fTabWidth = 0;
		} else {
			fTabWidth = (fWidth - 2) / floorf(fWidth / fTabWidth);
		}
		*pfWidth = fTabWidth;
	} else if (pTabs->bFitToSize && pfWidth) {
		F32 fTotalWidth = 0;
		for (i = 0; i < eaSize(&pTabs->eaTabs); i++)
		{
			F32 fTextWidth;
			if( pTabs->eaTabs[i] == pTabs->pActive) {
				fTextWidth = ui_StyleFontWidth(pActiveFont, fScale, ui_TabGetTitle(pTabs->eaTabs[i]));
			} else {
				fTextWidth = ui_StyleFontWidth(pInactiveFont, fScale, ui_TabGetTitle(pTabs->eaTabs[i]));
			}
			fTotalWidth += (fTextWidth + fMaxBorderWidth + pTabs->fTabXPad * fScale);
		}
		if(fTotalWidth > fWidth && eaSize(&pTabs->eaTabs)>0)
			fTabWidth = fWidth/eaSize(&pTabs->eaTabs);
		else
			fTabWidth = 0;
		*pfWidth = fTabWidth;
	}
	if (pfHeight)
		*pfHeight = fHeight + pTabs->fTabYPad * fScale;
}

F32 ui_TabGroupTickTabs(UITabGroup *pTabs, F32 fX, F32 fY, F32 fWidth, F32 fHeight, F32 fTabWidth, F32 fScale)
{
	UIStyleFont *pInactiveFont = ui_TabGroupGetFont(pTabs, false);
	UIStyleFont *pActiveFont = ui_TabGroupGetFont(pTabs, true);
	bool bFitToSize = (pTabs->bFitToSize && fTabWidth);
	F32 fDrawX = fX + 1;
	F32 fDrawY = fY;
	S32 i;
	UIDrawingDescription inactiveDesc = { 0 };
	UIDrawingDescription activeDesc = { 0 };
	F32 fMaxBorderWidth;
	ui_TabGroupInactiveFillDrawingDescription( pTabs, &inactiveDesc );
	ui_TabGroupActiveFillDrawingDescription( pTabs, &activeDesc );
	fMaxBorderWidth = max(ui_DrawingDescriptionWidth(&inactiveDesc), ui_DrawingDescriptionWidth(&activeDesc)) * fScale;
	for (i = 0; i < eaSize(&pTabs->eaTabs); i++)
	{
		F32 fIconScale = 1;
		UITab *pTab = pTabs->eaTabs[i];
		CBox box;
		if (!pTabs->bEqualWidths && !bFitToSize) {
			if( pTabs->eaTabs[i] == pTabs->pActive) {
				fTabWidth = ui_StyleFontWidth(pActiveFont, fScale, ui_TabGetTitle(pTabs->eaTabs[i]));
			} else {
				fTabWidth = ui_StyleFontWidth(pInactiveFont, fScale, ui_TabGetTitle(pTabs->eaTabs[i]));
			}
			fTabWidth += fMaxBorderWidth + pTabs->fTabXPad * fScale;
			if (pTab->pRIcon) {
				fIconScale = fHeight / pTab->pRIcon->height * fScale;
				fTabWidth += pTab->pRIcon->width * fIconScale;
			}
		}

		if (!pTabs->bVerticalTabs)
		{
			if ((fDrawX + fTabWidth >= fX + fWidth) && (pTabs->eStyle == UITabStyleButtons))
			{
				fDrawY += fHeight;
				fDrawX = fX + 1;
			}
		}

		BuildCBox(&box, fDrawX, fDrawY, fTabWidth, fHeight);

		if (mouseDownHit(MS_LEFT, &box))
		{
			ui_TabGroupSetActive(pTabs, pTab);
			if (pTabs->bFocusOnClick)
				ui_SetFocus(pTabs);
		}
		if (mouseDownHit(MS_RIGHT, &box) && pTab->cbContext)
		{
			if (pTabs->bFocusOnClick)
				ui_SetFocus(pTabs);
			pTab->cbContext(pTab,pTab->pContextData);
		}

		if (pTabs->bVerticalTabs)
			fDrawY += fHeight;
		else
			fDrawX += fTabWidth + pTabs->fTabSpacing * fScale;
	}
	if (pTabs->bVerticalTabs)
	{
		if (fDrawY != fY)
			fDrawX += fTabWidth;
		return fDrawX-fX;
	}
	if (fDrawX != fX)
		fDrawY += fHeight;
	return fDrawY-fY;
}

void ui_TabGroupTick(UITabGroup *pTabs, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pTabs);
	F32 fTabWidth;
	F32 fTabHeight;
	F32 fHeaderSize;

	UI_TICK_EARLY(pTabs, false, false);

	ui_TabGroupHeaderSizes(pTabs, scale, w, &fTabWidth, &fTabHeight);
	fHeaderSize = ui_TabGroupTickTabs(pTabs, x, y, w, fTabHeight, fTabWidth, scale);

	if (pTabs->eStyle == UITabStyleFoldersWithBorder)
	{
		x+=4; w-=8; h-=4;
	}

	if (pTabs->pActive)
	{
		if (pTabs->bVerticalTabs)
			ui_WidgetGroupTick(&pTabs->pActive->eaChildren, x + fHeaderSize, y, w - fHeaderSize, h, scale);
		else
			ui_WidgetGroupTick(&pTabs->pActive->eaChildren, x, y + fHeaderSize, w, h - fHeaderSize, scale);
	}

	UI_TICK_LATE(pTabs);
}

F32 ui_TabGroupDrawTabs(UITabGroup *pTabs, F32 fX, F32 fY, F32 fWidth, F32 fHeight, F32 fTabWidth, F32 fScale, F32 fZ)
{
	UIStyleFont *pInactiveFont = ui_TabGroupGetFont(pTabs, false);
	UIStyleFont *pActiveFont = ui_TabGroupGetFont(pTabs, true);
	Color inactive = UI_WIDGET(pTabs)->color[1];
	Color active = UI_WIDGET(pTabs)->color[0];
	bool bFitToSize = (pTabs->bFitToSize && fTabWidth);
	F32 fDrawX = fX + 1;
	F32 fDrawY = fY;
	S32 i;
	UIDrawingDescription inactiveDesc = { 0 };
	UIDrawingDescription activeDesc = { 0 };
	F32 fMaxBorderWidth;
	ui_TabGroupInactiveFillDrawingDescription( pTabs, &inactiveDesc );
	ui_TabGroupActiveFillDrawingDescription( pTabs, &activeDesc );
	fMaxBorderWidth = max(ui_DrawingDescriptionWidth(&inactiveDesc), ui_DrawingDescriptionWidth(&activeDesc)) * fScale;

	if (UI_GET_SKIN(pTabs))
	{
		active = UI_GET_SKIN(pTabs)->button[2];
		inactive = UI_GET_SKIN(pTabs)->button[0];
	}

	for (i = 0; i < eaSize(&pTabs->eaTabs); i++)
	{
		F32 fIconScale = 1;
		UITab *pTab = pTabs->eaTabs[i];
		Color bg = (pTab == pTabs->pActive) ? active : inactive;
		CBox box;

		if (!pTabs->bEqualWidths && !bFitToSize) {
			if( pTabs->eaTabs[i] == pTabs->pActive) {
				fTabWidth = ui_StyleFontWidth(pActiveFont, fScale, ui_TabGetTitle(pTabs->eaTabs[i]));
			} else {
				fTabWidth = ui_StyleFontWidth(pInactiveFont, fScale, ui_TabGetTitle(pTabs->eaTabs[i]));
			}
			fTabWidth += fMaxBorderWidth + pTabs->fTabXPad * fScale;
			if (pTab->pRIcon) {
				fIconScale = fHeight / pTab->pRIcon->height * fScale;
				fTabWidth += pTab->pRIcon->width * fIconScale;
			}
		}

		if (!pTabs->bVerticalTabs)
		{
			if ((fDrawX + fTabWidth >= fX + fWidth) && (pTabs->eStyle == UITabStyleButtons))
			{
				fDrawY += fHeight;
				fDrawX = fX + 1;
			}
		}

		if (pTabs->eStyle == UITabStyleButtons) {
			BuildCBox(&box, fDrawX, fDrawY, fTabWidth, fHeight);
			if (pTab == pTabs->pActive)
				ui_DrawingDescriptionDraw( &activeDesc, &box, fScale, fZ, 255, bg, ColorBlack );
			else
				ui_DrawingDescriptionDraw( &inactiveDesc, &box, fScale, fZ, 255, bg, ColorBlack );
		} else {
			gfxDrawLine(fDrawX + 4*fScale, fDrawY+1, fZ, fDrawX + fTabWidth-(4*fScale), fDrawY+1, s_TabBorderColor);
			gfxDrawLine(fDrawX + 4*fScale, fDrawY+1, fZ, fDrawX, fDrawY + fHeight, s_TabBorderColor);
			gfxDrawLine(fDrawX + fTabWidth-(4*fScale), fDrawY+1, fZ, fDrawX + fTabWidth, fDrawY + fHeight, s_TabBorderColor);
			if (pTab != pTabs->pActive) 
				gfxDrawLine(fDrawX, fDrawY+fHeight, fZ, fDrawX + fTabWidth, fDrawY + fHeight, s_TabBorderColor);
		}
		if( pTabs->pActive == pTab ) {
			ui_StyleFontUse(pActiveFont, false, UI_WIDGET(pTabs)->state);
		} else {
			ui_StyleFontUse(pInactiveFont, false, UI_WIDGET(pTabs)->state);
		}
		if (pTab->pRIcon) {
			gfxfont_Printf(fDrawX + fTabWidth/2 - pTab->pRIcon->width/2 * fIconScale, fDrawY + fHeight/2, fZ + 0.1, fScale, fScale, CENTER_XY, "%s", ui_TabGetTitle(pTab));
			display_sprite(pTab->pRIcon, fDrawX + fTabWidth - pTab->pRIcon->width * fIconScale, fDrawY, fZ + 0.1, fIconScale, fIconScale, 0xFFFFFFFF );
		} else if(bFitToSize) {
			gfxfont_PrintMaxWidth(fDrawX + fTabWidth/2, fDrawY + fHeight/2, fZ + 0.1, fTabWidth*0.8f, fScale, fScale, CENTER_XY, ui_TabGetTitle(pTab));
		} else {
			gfxfont_Printf(fDrawX + fTabWidth/2, fDrawY + fHeight/2, fZ + 0.1, fScale, fScale, CENTER_XY, "%s", ui_TabGetTitle(pTab));
		}

		if (pTabs->bVerticalTabs)
			fDrawY += fHeight;
		else
			fDrawX += fTabWidth + pTabs->fTabSpacing * fScale;
	}
	if ((pTabs->eStyle == UITabStyleFolders || pTabs->eStyle == UITabStyleFoldersWithBorder) && eaSize(&pTabs->eaTabs))
	{
		gfxDrawLine(fDrawX, fDrawY+fHeight, fZ, fX + fWidth - 1, fDrawY + fHeight, s_TabBorderColor);
	}
	if (pTabs->bVerticalTabs)
	{
		if (fDrawY != fY)
			fDrawX += fTabWidth;
		return fDrawX-fX;
	}
	if (fDrawX != fX) 
		fDrawY += fHeight;
	return fDrawY-fY;
}

void ui_TabGroupDraw(UITabGroup *pTabs, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pTabs);
	F32 fTabWidth;
	F32 fTabHeight;
	F32 fHeaderSize;

	UI_DRAW_EARLY(pTabs);

	ui_TabGroupHeaderSizes(pTabs, scale, w, &fTabWidth, &fTabHeight);
	fHeaderSize = ui_TabGroupDrawTabs(pTabs, x, y, w, fTabHeight, fTabWidth, scale, z);

	if (pTabs->eStyle == UITabStyleFoldersWithBorder && eaSize(&pTabs->eaTabs) && fHeaderSize)
	{
		gfxDrawLine(x + 1, y + fHeaderSize, z, x + 1, y + h - 1, s_TabBorderColor);
		gfxDrawLine(x + w - 1, y + fHeaderSize, z, x + w - 1, y + h - 1, s_TabBorderColor);
		gfxDrawLine(x + 1, y + h - 1, z, x + w - 1, y + h - 1, s_TabBorderColor);
		x+=4; w-= 8; h-= 4;
	}

	if (pTabs->pActive)
	{
		if (pTabs->bVerticalTabs)
			ui_WidgetGroupDraw(&pTabs->pActive->eaChildren, x + fHeaderSize, y, w - fHeaderSize, h, scale);
		else
			ui_WidgetGroupDraw(&pTabs->pActive->eaChildren, x, y + fHeaderSize, w, h - fHeaderSize, scale);
	}
	clipperPop();
}

bool ui_TabGroupInput(UITabGroup *pTabs, KeyInput *pInput)
{
	S32 iTab;
	if (pInput->type != KIT_EditKey)
		return false;
	switch (pInput->scancode)
	{
	case INP_LEFT:
		iTab = MAX(0, ui_TabGroupGetActiveIndex(pTabs) - 1);
		ui_TabGroupSetActiveIndex(pTabs, iTab);
		return true;
	case INP_RIGHT:
		iTab = MIN(eaSize(&pTabs->eaTabs) - 1, ui_TabGroupGetActiveIndex(pTabs) + 1);
		ui_TabGroupSetActiveIndex(pTabs, iTab);
		return true;
	default:
		return false;
	}
}

UITabGroup *ui_TabGroupCreate(F32 fX, F32 fY, F32 fW, F32 fH)
{
	UITabGroup *pTabs = (UITabGroup *)calloc(1, sizeof(UITabGroup));
	ui_TabGroupInitialize(pTabs, fX, fY, fW, fH);
	return pTabs;
}

void ui_TabGroupInitialize(UITabGroup *pTabs, F32 fX, F32 fY, F32 fW, F32 fH)
{
	ui_WidgetInitialize(UI_WIDGET(pTabs), ui_TabGroupTick, ui_TabGroupDraw, ui_TabGroupFreeInternal, ui_TabGroupInput, NULL);
	ui_WidgetSetPosition(UI_WIDGET(pTabs), fX, fY);
	ui_WidgetSetDimensions(UI_WIDGET(pTabs), fW, fH);
	pTabs->fTabXPad = UI_HSTEP;
	pTabs->fTabYPad = UI_HSTEP;
	pTabs->bFocusOnClick = true;
}

void ui_TabGroupFreeInternal(UITabGroup *pTabs)
{
	eaDestroyEx(&pTabs->eaTabs, ui_TabFree);
	ui_WidgetFreeInternal(UI_WIDGET(pTabs));
}

void ui_TabGroupSetActive(UITabGroup *pTabs, UITab *pTab)
{
	assert(!pTab || eaFind(&pTabs->eaTabs, pTab) >= 0);
	if (pTabs->pActive)
		ui_WidgetGroupUnfocus(&pTabs->pActive->eaChildren);
	pTabs->pActive = pTab;
	UI_WIDGET(pTabs)->children = SAFE_MEMBER(pTab, eaChildren);
	if (pTabs->cbChanged)
		pTabs->cbChanged(pTabs, pTabs->pChangedData);
}

void ui_TabGroupSetActiveIndex(UITabGroup *pTabs, S32 iTab)
{
	iTab = CLAMP(iTab, 0, eaSize(&pTabs->eaTabs) - 1);
	if (iTab >= 0)
	{
		ui_TabGroupSetActive(pTabs, pTabs->eaTabs[iTab]);
	}
}

UITab *ui_TabGroupGetActive(UITabGroup *pTabs)
{
	return pTabs->pActive;
}

S32 ui_TabGroupGetActiveIndex(UITabGroup *pTabs)
{
	return eaFind(&pTabs->eaTabs, pTabs->pActive);
}

void ui_TabGroupAddTab(UITabGroup *pTabs, UITab *pTab)
{
	if (pTab->pTabGroup)
		ui_TabGroupRemoveTab(pTab->pTabGroup, pTab);
	eaPush(&pTabs->eaTabs, pTab);
	pTab->pTabGroup = pTabs;
	if (pTabs->bNeverEmpty && pTabs->pActive == NULL)
	{
		ui_TabGroupSetActive(pTabs, pTab);
	}
}

void ui_TabGroupInsertTab(UITabGroup *pTabs, UITab *pTab, int idx)
{
	if (pTab->pTabGroup)
		ui_TabGroupRemoveTab(pTab->pTabGroup, pTab);
	eaInsert(&pTabs->eaTabs, pTab, idx);
	pTab->pTabGroup = pTabs;
}

void ui_TabGroupAddTabFirst(UITabGroup *pTabs, UITab *pTab)
{
	ui_TabGroupInsertTab(pTabs, pTab, 0);
}

void ui_TabGroupRemoveTab(UITabGroup *pTabs, UITab *pTab)
{
	eaFindAndRemove(&pTabs->eaTabs, pTab);
	if (pTabs->pActive == pTab)
	{
		if (pTabs->bNeverEmpty && eaSize(&pTabs->eaTabs) > 0)
		{
			ui_TabGroupSetActiveIndex(pTabs, 0);
		}
		else
		{
			pTabs->pActive = NULL;
			UI_WIDGET(pTabs)->children = NULL;
		}
	}
	pTab->pTabGroup = NULL;
}

static void ui_TabClearTabGroup(UITab *pTab)
{
	pTab->pTabGroup = NULL;
}

void ui_TabGroupRemoveAllTabs(UITabGroup *pTabs)
{
	eaClearEx(&pTabs->eaTabs, ui_TabClearTabGroup);
	pTabs->pActive = NULL;
	UI_WIDGET(pTabs)->children = NULL;
}

void ui_TabGroupSetChangedCallback(UITabGroup *pTabs, UIActivationFunc cbChanged, UserData pChangedData)
{
	pTabs->cbChanged = cbChanged;
	pTabs->pChangedData = pChangedData;
}

void ui_TabSetTitleString(UITab *pTab, const char *pchTitle)
{
	REMOVE_HANDLE(pTab->hTitleMessage_USEACCESSOR);
	if( !pchTitle ) {
		StructFreeStringSafe( &pTab->pchTitle_USEACCESSOR );
	} else {
		StructCopyString( &pTab->pchTitle_USEACCESSOR, pchTitle );
	}
}

void ui_TabSetTitleMessage(UITab *pTab, const char *pchTitle)
{
	StructFreeStringSafe( &pTab->pchTitle_USEACCESSOR );
	if( !pchTitle ) {
		REMOVE_HANDLE(pTab->hTitleMessage_USEACCESSOR);
	} else {
		SET_HANDLE_FROM_STRING( "Message", pchTitle, pTab->hTitleMessage_USEACCESSOR );
	}
}

const char* ui_TabGetTitle(UITab *pTab)
{
	if( IS_HANDLE_ACTIVE( pTab->hTitleMessage_USEACCESSOR )) {
		return TranslateMessageRef( pTab->hTitleMessage_USEACCESSOR );
	} else {
		return pTab->pchTitle_USEACCESSOR;
	}
}

void ui_TabSetRIcon(UITab *pTab, const char *pchRIcon)
{
	if( pchRIcon ) {
		pTab->pRIcon = atlasFindTexture( pchRIcon );
	} else {
		pTab->pRIcon = NULL;
	}
}

UITab *ui_TabCreate(const char *pchTitle)
{
	UITab *pTab = (UITab *)calloc(1, sizeof(UITab));
	ui_TabSetTitleString(pTab, pchTitle);
	return pTab;
}

UITab *ui_TabCreateWithScrollArea(const char *pchTitle)
{
	UITab *pTab = ui_TabCreate(pchTitle);
	pTab->pScrollArea = ui_ScrollAreaCreate(0, 0, 1.f, 1.f, 1, 1, true, true);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pTab->pScrollArea), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	pTab->pScrollArea->autosize = 1;
	ui_TabAddChild(pTab, UI_WIDGET(pTab->pScrollArea));
	return pTab;
}

void ui_TabFree(UITab *pTab)
{
	if (pTab->pTabGroup)
		ui_TabGroupRemoveTab(pTab->pTabGroup, pTab);
	ui_WidgetGroupFreeInternal(&pTab->eaChildren);
	StructFreeStringSafe( &pTab->pchTitle_USEACCESSOR );
	REMOVE_HANDLE( pTab->hTitleMessage_USEACCESSOR );
	free(pTab);
}

void ui_TabAddChild(UITab *pTab, UIAnyWidget *pChild)
{
	ui_WidgetGroupAdd(&pTab->eaChildren, pChild);
	if (pTab->pTabGroup && pTab->pTabGroup->pActive == pTab)
		UI_WIDGET(pTab->pTabGroup)->children = pTab->eaChildren;
}

void ui_TabRemoveChild(UITab *pTab, UIAnyWidget *pChild)
{
	ui_WidgetGroupRemove(&pTab->eaChildren, pChild);
	if (pTab->pTabGroup && pTab->pTabGroup->pActive == pTab)
		UI_WIDGET(pTab->pTabGroup)->children = pTab->eaChildren;
}

UIScrollArea *ui_TabGetScrollArea(UITab *pTab)
{
	return pTab->pScrollArea;
}

void ui_TabGroupSetSpacing(UITabGroup *pTabs, F32 fSpacing)
{
	pTabs->fTabSpacing = fSpacing;
}

void ui_TabGroupSetTabPadding(UITabGroup *pTabs, F32 fXPad, F32 fYPad)
{
	pTabs->fTabXPad = fXPad;
	pTabs->fTabYPad = fYPad;
}

void ui_TabGroupSetStyle(UITabGroup *pTabs, UITabStyle eStyle)
{
	pTabs->eStyle = eStyle;
}
