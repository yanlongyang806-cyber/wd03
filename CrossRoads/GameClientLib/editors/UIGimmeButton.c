/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Color.h"
#include "GfxClipper.h"
#include "inputMouse.h"
#include "inputText.h"
#include "ResourceManager.h"
#include "UIGimmeButton.h"
#include "UISprite.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static Color gGimmeIconColor = 	{ 0x69, 0xf8, 0x21, 0x9f };

void ui_GimmeButtonCheckout(UIMenu *pMenu, UIGimmeButton *pGimmeButton)
{
	ResourceInfo *pInfo = resGetInfo(pGimmeButton->pcDictName, pGimmeButton->pcName);
	if (pInfo) 
	{
		resCallback_HandleSimpleEdit *pCB;
		if (pGimmeButton->pReferent)
		{		
			resRequestLockResource(pGimmeButton->pcDictName, pGimmeButton->pcName, pGimmeButton->pReferent);
		}
		else if (pCB = resGetSimpleEditCB(pGimmeButton->pcDictName, kResEditType_CheckOut))
		{
			pCB(kResEditType_CheckOut, pInfo, NULL);
		}
		else
		{
			resRequestLockResource(pGimmeButton->pcDictName, pGimmeButton->pcName, pGimmeButton->pReferent);
		}
	}
}

void ui_GimmeButtonUndoCheckout(UIMenu *pMenu, UIGimmeButton *pGimmeButton)
{
	ResourceInfo *pInfo = resGetInfo(pGimmeButton->pcDictName, pGimmeButton->pcName);
	if (pInfo) 
	{
		resCallback_HandleSimpleEdit *pCB;
		if (pGimmeButton->pReferent)
		{		
			resRequestUnlockResource(pGimmeButton->pcDictName, pGimmeButton->pcName, pGimmeButton->pReferent);
		}
		else if (pCB = resGetSimpleEditCB(pGimmeButton->pcDictName, kResEditType_Revert))
		{
			pCB(kResEditType_Revert, pInfo, NULL);
		}
		else
		{
			resRequestUnlockResource(pGimmeButton->pcDictName, pGimmeButton->pcName, pGimmeButton->pReferent);
		}
	} 
	else 
	{
		char buf[1024];
		sprintf(buf, "Resource '%s' does not exist.  Perhaps it has not been saved yet.", pGimmeButton->pcName);
		ui_DialogPopup("Error", buf);
	}
}

void ui_GimmeButtonPreopenCallback(UIMenu *pMenu, UIGimmeButton *pGimmeButton)
{
	ResourceInfo *pInfo = resGetInfo(pGimmeButton->pcDictName, pGimmeButton->pcName);
	if (pInfo && !resIsWritable(pInfo->resourceDict, pInfo->resourceName))
	{
		pMenu->items[0]->active = true;
		pMenu->items[1]->active = false;
	}
	else
	{
		pMenu->items[0]->active = false;
		pMenu->items[1]->active = true;
	}
}

void ui_GimmeButtonTick(UIGimmeButton *pGimmeButton, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pGimmeButton);

	UI_TICK_EARLY(pGimmeButton, true, true);

	if (mouseClickHit(MS_LEFT, &box) || mouseClickHit(MS_RIGHT, &box) ||
		(mouseUpHit(MS_LEFT, &box) && pGimmeButton->pressed) ||
		(mouseUpHit(MS_RIGHT, &box) && pGimmeButton->pressed))
	{
		pGimmeButton->pressed = false;
		ui_SetFocus(pGimmeButton);
		inpHandled();
		pGimmeButton->pMenu->widget.scale = pScale/g_ui_State.scale; // Scale menu same as the button
		ui_MenuPopupAtCursorOrWidgetBox(pGimmeButton->pMenu);
	}
	else if (mouseDownHit(MS_LEFT, &box) || mouseDownHit(MS_RIGHT, &box))
	{
		pGimmeButton->pressed = true;
		ui_SetFocus(pGimmeButton);
		inpHandled();
	}
	else if (!mouseIsDown(MS_LEFT) && !mouseIsDown(MS_RIGHT))
		pGimmeButton->pressed = false;

	UI_TICK_LATE(pGimmeButton);
}

void ui_GimmeButtonDraw(UIGimmeButton *pGimmeButton, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pGimmeButton);
	UIStyleBorder *pBorder = GET_REF(pGimmeButton->hBorder);
	UIStyleBorder *pFocusedBorder = GET_REF(pGimmeButton->hFocusedBorder);
	ResourceInfo *pInfo = resGetInfo(pGimmeButton->pcDictName, pGimmeButton->pcName);
	Color c;

	UI_DRAW_EARLY(pGimmeButton);
	c = ui_WidgetButtonColor(UI_WIDGET(pGimmeButton), false, pGimmeButton->pressed);

	ui_StyleBorderDraw(pBorder, &box, RGBAFromColor(c), RGBAFromColor(c), z, scale, 255);

	// Update the sprite on each draw based on gimme status
	ui_SpriteSetTexture(pGimmeButton->pSprite,(pInfo && !resIsWritable(pInfo->resourceDict, pInfo->resourceName)) ? "eui_gimme_readonly":"eui_gimme_ok");

	UI_DRAW_LATE(pGimmeButton);

	if (ui_IsFocused(pGimmeButton) && pFocusedBorder)
		ui_StyleBorderDrawOutside(pFocusedBorder, &box, RGBAFromColor(c), RGBAFromColor(c), z, scale, 255);
}

bool ui_GimmeButtonInput(UIGimmeButton *pGimmeButton, KeyInput *input)
{
	if (input->type != KIT_EditKey)
		return false;
	if ((input->scancode == INP_SPACE || input->scancode == INP_RETURN) && ui_IsActive(UI_WIDGET(pGimmeButton)))
	{
		ui_MenuPopupAtCursorOrWidgetBox(pGimmeButton->pMenu);
		return true;
	}
	else
		return false;
}

UIGimmeButton *ui_GimmeButtonCreate(F32 x, F32 y, const char *pcDictName, const char *pcName, const void *pReferent)
{
	UIGimmeButton *pGimmeButton = (UIGimmeButton *)calloc(1, sizeof(UIGimmeButton));
	ui_GimmeButtonInitialize(pGimmeButton, x, y, pcDictName, pcName, pReferent);
	return pGimmeButton;
}

void ui_GimmeButtonInitialize(UIGimmeButton *pGimmeButton, F32 x, F32 y, const char *pcDictName, const char *pcName, const void *pReferent)
{
	UIStyleFont *font = GET_REF(g_ui_State.font);
	ui_WidgetInitialize(UI_WIDGET(pGimmeButton), ui_GimmeButtonTick, ui_GimmeButtonDraw, ui_GimmeButtonFreeInternal, ui_GimmeButtonInput, NULL);
	ui_WidgetSetPosition(UI_WIDGET(pGimmeButton), x, y);
	ui_WidgetSetDimensions(UI_WIDGET(pGimmeButton), 16, 16);
	SET_HANDLE_FROM_STRING(g_ui_BorderDict, "Default_Capsule_Filled", pGimmeButton->hBorder);
	if (UI_GET_SKIN(pGimmeButton))
		font = GET_REF(UI_GET_SKIN(pGimmeButton)->hNormal);
	pGimmeButton->pMenu = ui_MenuCreate(NULL);
	ui_MenuAppendItems(pGimmeButton->pMenu,
		ui_MenuItemCreate("Check Out",UIMenuCallback, ui_GimmeButtonCheckout, pGimmeButton, NULL),
		ui_MenuItemCreate("Undo Check Out",UIMenuCallback, ui_GimmeButtonUndoCheckout, pGimmeButton, NULL),
		NULL);
	ui_MenuSetPreopenCallback(pGimmeButton->pMenu, ui_GimmeButtonPreopenCallback, pGimmeButton);
	pGimmeButton->pSprite = ui_SpriteCreate(3,3,10,10,"eui_gimme_readonly");
	pGimmeButton->pSprite->widget.uClickThrough = true;
	pGimmeButton->pSprite->tint = gGimmeIconColor;
	if (pcName)
		pGimmeButton->pcName = strdup(pcName);
	if (pcDictName)
		pGimmeButton->pcDictName = strdup(pcDictName);
	pGimmeButton->pReferent = (void*)pReferent;
	ui_WidgetAddChild(UI_WIDGET(pGimmeButton), UI_WIDGET(pGimmeButton->pSprite));
}

void ui_GimmeButtonFreeInternal(UIGimmeButton *pGimmeButton)
{
	REMOVE_HANDLE(pGimmeButton->hBorder);
	REMOVE_HANDLE(pGimmeButton->hFocusedBorder);
	SAFE_FREE(pGimmeButton->pcDictName);
	SAFE_FREE(pGimmeButton->pcName);
	ui_WidgetQueueFree(UI_WIDGET(pGimmeButton->pMenu));
	ui_WidgetFreeInternal(UI_WIDGET(pGimmeButton));
}

void ui_GimmeButtonSetName(SA_PARAM_NN_VALID UIGimmeButton *pGimmeButton, const char *pcName)
{
	SAFE_FREE(pGimmeButton->pcName);
	if (pcName)
		pGimmeButton->pcName = strdup(pcName);
}

void ui_GimmeButtonSetReferent(SA_PARAM_NN_VALID UIGimmeButton *pGimmeButton, const void *pReferent)
{
	pGimmeButton->pReferent = (void*)pReferent;
}

