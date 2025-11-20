/***************************************************************************
*     Copyright (c) 2005-2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct DefineContext DefineContext;
typedef struct PowerDef PowerDef;

void comboTracker_initPowerPurposes(DefineContext *pPurposeContext, int *iStartingNumber);
bool comboTracker_RequiresTracking(PowerDef *pDef);

bool comboTracker_isComboStart(U32 iPowerPurpose);
bool comboTracker_isComboContinue(U32 iPowerPurpose);
bool comboTracker_isComboEnd(U32 iPowerPurpose);