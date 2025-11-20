/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#include "UICore.h"

typedef struct UIMenu UIMenu;
typedef struct UISprite UISprite;
typedef struct EMFile EMFile;
typedef enum GimmeErrorValue GimmeErrorValue;

typedef struct UIGimmeButton
{
	UIWidget widget;

	UIMenu *pMenu;
	UISprite *pSprite;

	char *pcDictName;
	char *pcName;
	void *pReferent;

	bool pressed : 1;

	REF_TO(UIStyleBorder) hBorder;
	REF_TO(UIStyleBorder) hFocusedBorder;
} UIGimmeButton;

SA_RET_NN_VALID UIGimmeButton *ui_GimmeButtonCreate(F32 x, F32 y, const char *pcDictName, const char *pcName, const void *pReferent);
void ui_GimmeButtonInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UIGimmeButton *pGimmeButton, F32 x, F32 y, const char *pcDictName, const char *pcName, const void *pReferent);
void ui_GimmeButtonFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIGimmeButton *pGimmeButton);

void ui_GimmeButtonDraw(SA_PARAM_NN_VALID UIGimmeButton *pGimmeButton, UI_PARENT_ARGS);
void ui_GimmeButtonTick(SA_PARAM_NN_VALID UIGimmeButton *pGimmeButton, UI_PARENT_ARGS);

void ui_GimmeButtonSetName(SA_PARAM_NN_VALID UIGimmeButton *pGimmeButton, const char *pcName);
void ui_GimmeButtonSetReferent(SA_PARAM_NN_VALID UIGimmeButton *pGimmeButton, const void *pReferent);


