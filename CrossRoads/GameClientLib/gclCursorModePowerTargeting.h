/***************************************************************************
*     Copyright (c) 2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

// Indicates if the given power def is valid for this targeting mode
bool gclCursorPowerTargeting_PowerValid(Character *pChar, PowerDef *pDef);

// Initiates mouse targeting for the given power
void gclCursorPowerTargeting_Begin(U32 uiPowerId, EffectArea eEffectArea);

U32 gclCursorPowerTargeting_GetCurrentPowID();

Power* gclCursorPowerTargeting_GetCurrentPower();

void gclCursorPowerTargeting_PowerExec();
