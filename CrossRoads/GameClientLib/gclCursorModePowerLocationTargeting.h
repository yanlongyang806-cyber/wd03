/***************************************************************************
*     Copyright (c) 2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

// Initiates location based targeting for the given power
void gclCursorPowerLocationTargeting_Begin(U32 uiPowerId);

void gclCursorPowerLocationTargeting_PowerExec();

U32 gclCursorPowerLocationTargeting_GetCurrentPowID();

Power* gclCursorPowerLocationTargeting_GetCurrentPower();

bool gclCursorPowerLocationTargeting_PowerValid(PowerDef* pDef);

