/***************************************************************************
*     Copyright (c) 2005-2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "ComboTracker.h"

#include "structDefines.h"
#include "GlobalTypes.h"
#include "Powers.h"

#define MAXCOMBOENUM 3

int iComboEnums[MAXCOMBOENUM];

void comboTracker_initPowerPurposes(DefineContext *pPurposeContext, int *iStartingNumber)
{
	if(gConf.bEnableClientComboTracker)
	{
		iComboEnums[0] = (*iStartingNumber)++;
		DefineAddInt(pPurposeContext, "ComboStart", iComboEnums[0]);
		iComboEnums[1] = (*iStartingNumber)++;
		DefineAddInt(pPurposeContext, "ComboContinue", iComboEnums[1]);
		iComboEnums[2] = (*iStartingNumber)++;
		DefineAddInt(pPurposeContext, "ComboEnd", iComboEnums[2]);
		
	}
}

bool comboTracker_RequiresTracking(PowerDef *pDef)
{
	if(pDef && gConf.bEnableClientComboTracker)
	{
		if(pDef->ePurpose >= iComboEnums[0] && pDef->ePurpose <= iComboEnums[MAXCOMBOENUM - 1])
		{
			return true;
		}
	}

	return false;
}

bool comboTracker_isComboStart(U32 iPowerPurpose)
{
	return iPowerPurpose == (U32)iComboEnums[0];
}

bool comboTracker_isComboContinue(U32 iPowerPurpose)
{
	return iPowerPurpose == (U32)iComboEnums[1];
}

bool comboTracker_isComboEnd(U32 iPowerPurpose)
{
	return iPowerPurpose == (U32)iComboEnums[2];
}
