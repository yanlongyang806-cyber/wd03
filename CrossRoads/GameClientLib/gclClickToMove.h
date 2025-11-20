#ifndef GCL_CLICK_TO_MOVE_H
#define GCL_CLICK_TO_MOVE_H

#include "powers.h"

void clickToMoveTick(void);

void enableClickToMove(bool enable);
void enableClickToMoveTwoButton(bool enable);

bool ctm_PowerValidForNextClick(PowerDef* pDef);
#endif