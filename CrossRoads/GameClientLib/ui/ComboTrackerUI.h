/***************************************************************************
*     Copyright (c) 2005-2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "referencesystem.h"

typedef struct PowerActivation PowerActivation;
typedef struct PowerDef PowerDef;

AUTO_STRUCT;
typedef struct ComboTrackerEntry
{
	REF_TO(PowerDef) hDef;
	char *pchIconName;
	U8 uchPowerActID;
	bool bQueued;
	bool bCanActivate;
	int iTraySlot;
	U32 uiPowerID;
}ComboTrackerEntry;

AUTO_STRUCT;
typedef struct ComboTrackerUI
{
	ComboTrackerEntry *pQueuedEntry;
	ComboTrackerEntry **ppEntries;
}ComboTrackerUI;

void comboTracker_PowerActivate(PowerActivation *pAct);