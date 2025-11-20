/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "UGCTipsCommon.h"
#include "ResourceManager.h"
#include "GameBranch.h"
#include "file.h"
#include "itemCommon.h"
#include "timing.h"
#include "GameAccountData/GameAccountData.h"

#include "AutoGen/UGCTipsCommon_h_ast.h"

#include "error.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

UGCTipsConfig gUGCTipsConfig;


////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Load and Validate

void UGCTips_ConfigValidate(const char* filename, UGCTipsConfig* pConfig)
{
	if (pConfig==NULL || pConfig->uTippingEnabled==0)
	{
		return;
	}
	
    if (!GET_REF(pConfig->hTipsNumeric) && REF_STRING_FROM_HANDLE(pConfig->hTipsNumeric)) {
        ErrorFilenamef(filename, "UGCTips references non-existent tip numeric item '%s'", REF_STRING_FROM_HANDLE(pConfig->hTipsNumeric));
    }
    else
    {
        ItemDef *tipsItemDef = GET_REF(pConfig->hTipsNumeric);
        if ( tipsItemDef->eType != kItemType_Numeric )
        {
            ErrorFilenamef(filename, "UGCTips references tip item that is not a numeric '%s'", REF_STRING_FROM_HANDLE(pConfig->hTipsNumeric));
        }
    }

    if ( pConfig->timeIntervalSeconds == 0 )
    {
        ErrorFilenamef(filename, "UGCTips time interval must be non-zero");
    }

    if ( pConfig->allowedTipsPerTimeInterval <= 0 )
    {
        ErrorFilenamef(filename, "UGCTips tips per interval must be greater than zero");
    }
}


AUTO_STARTUP(UGCTips) ASTRT_DEPS(Items);
void UGCTips_Load(void)
{
	char *estrSharedMemory = NULL;
	char *pcBinFile = NULL;
	char *pcBuffer = NULL;
	char *estrFile = NULL;

    loadstart_printf("Loading UGCTips config...");

    // initialize with default values in case the config file doesn't exist
    StructReset(parse_UGCTipsConfig, &gUGCTipsConfig);

	MakeSharedMemoryName(GameBranch_GetFilename(&pcBinFile, "FoundryTips.bin"),&estrSharedMemory);
	estrPrintf(&estrFile, "defs/config/%s", GameBranch_GetFilename(&pcBuffer, "FoundryTips.def"));
	ParserLoadFilesShared(estrSharedMemory, NULL, estrFile, pcBinFile, PARSER_OPTIONALFLAG, parse_UGCTipsConfig, &gUGCTipsConfig);

	// Not entirely sure about this, but NumericConversion effectively does the same thing and only validates on a real server
    if (IsGameServerSpecificallly_NotRelatedTypes())
	{
		UGCTips_ConfigValidate(estrFile, &gUGCTipsConfig);
	}

	estrDestroy(&estrFile);

	loadend_printf("Done.");

	estrDestroy(&estrFile);
	estrDestroy(&pcBuffer);
	estrDestroy(&pcBinFile);
	estrDestroy(&estrSharedMemory);
}




////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Utility functions

bool UGCTipsAlreadyTippedAuthor(GameAccountData *pTipperAccountData, U32 uAuthorAccountID)
{
	U32 curTime = timeSecondsSince2000();
	U32 startOfCurrentInterval;
	int i;

	if (gUGCTipsConfig.uTippingEnabled==0)
	{
		return(false);
	}

    // compute the start time of the current time interval
    startOfCurrentInterval = ( curTime / gUGCTipsConfig.timeIntervalSeconds ) * gUGCTipsConfig.timeIntervalSeconds;

	for(i = eaSize(&(pTipperAccountData->eaTipRecords))-1; i>=0; i--)
	{
		// If the recorded tip is in the current interval
		if (pTipperAccountData->eaTipRecords[i]->uTimeOfTip >= startOfCurrentInterval)
		{
			// And is for the author specified
			if (pTipperAccountData->eaTipRecords[i]->uTipAuthorAccountID == uAuthorAccountID)
			{
				return(true);
			}
		}
	}
	return(false);
}

bool UGCTipsAlreadyTippedMaxTimes(GameAccountData *pTipperAccountData)
{
	U32 curTime = timeSecondsSince2000();
	U32 startOfCurrentInterval;
	int iMaxTipsPerPeriod=gUGCTipsConfig.allowedTipsPerTimeInterval;
	int iTipCount=0;
	int i;

	if (gUGCTipsConfig.uTippingEnabled==0)
	{
		return(false);
	}

    // compute the start time of the current time interval
    startOfCurrentInterval = ( curTime / gUGCTipsConfig.timeIntervalSeconds ) * gUGCTipsConfig.timeIntervalSeconds;

	for(i = eaSize(&(pTipperAccountData->eaTipRecords))-1; i>=0; i--)
	{
		if (pTipperAccountData->eaTipRecords[i]->uTimeOfTip >= startOfCurrentInterval)
		{
			iTipCount++;
		}
	}

	if (iTipCount>=iMaxTipsPerPeriod)
	{
		return(true);
	}
	return(false);
}

int UGCTipsAllowedTipsRemaining(GameAccountData *pTipperAccountData)
{
	U32 curTime = timeSecondsSince2000();
	U32 startOfCurrentInterval;
	int iMaxTipsPerPeriod=gUGCTipsConfig.allowedTipsPerTimeInterval;
	int iTipCount=0;
	int i;

	if (gUGCTipsConfig.uTippingEnabled==0)
	{
		return(0);
	}

    // compute the start time of the current time interval
    startOfCurrentInterval = ( curTime / gUGCTipsConfig.timeIntervalSeconds ) * gUGCTipsConfig.timeIntervalSeconds;

	for(i = eaSize(&(pTipperAccountData->eaTipRecords))-1; i>=0; i--)
	{
		if (pTipperAccountData->eaTipRecords[i]->uTimeOfTip >= startOfCurrentInterval)
		{
			iTipCount++;
		}
	}

	return(iMaxTipsPerPeriod-iTipCount);
}

bool UGCTipsEnabled()
{
	return(gUGCTipsConfig.uTippingEnabled!=0);
}



#include "UGCTipsCommon_h_ast.c"
