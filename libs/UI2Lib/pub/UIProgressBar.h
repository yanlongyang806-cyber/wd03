#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef UI_PROGRESSBAR_H
#define UI_PROGRESSBAR_H

#include "UICore.h"

//////////////////////////////////////////////////////////////////////////
// A horizontal progress bar.

typedef struct UIProgressBar
{
	UIWidget widget;

	// Current progress, [0..1].
	F32 progress;
} UIProgressBar;

SA_RET_NN_VALID UIProgressBar *ui_ProgressBarCreate(F32 x, F32 y, F32 w);
void ui_ProgressBarFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIProgressBar *pbar);

void ui_ProgressBarSet(SA_PARAM_NN_VALID UIProgressBar *pbar, F32 progress);

void ui_ProgressBarDraw(SA_PARAM_NN_VALID UIProgressBar *pbar, UI_PARENT_ARGS);

#endif