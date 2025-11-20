/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslNumericConversion.h"
#include "NumericConversionCommon.h"
#include "Entity.h"
#include "Player.h"
#include "AutoTransDefs.h"
#include "objTransactions.h"
#include "itemCommon.h"
#include "inventoryCommon.h"
#include "EntityLib.h"
#include "StringUtil.h"
#include "NotifyEnum.h"
#include "StringFormat.h"
#include "GameStringFormat.h"
#include "LoggedTransactions.h"
#include "GamePermissionsCommon.h"
#include "GameAccountDataCommon.h"

#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/Player_h_ast.h"
#include "AutoGen/gslNumericConversion_c_ast.h"
#include "AutoGen/NumericConversionCommon_h_ast.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

static void
NotifyError(Entity *pEnt)
{
    char *notifyStr = NULL;
    langFormatGameMessageKey(entGetLanguage(pEnt), &notifyStr, "NumericConversion.InternalError", STRFMT_END);
    ClientCmd_NotifySend(pEnt, kNotifyType_NumericConversionFailure, notifyStr, NULL, NULL);
    estrDestroy(&notifyStr);
}

static void
NotifySuccess(Entity *pEnt, U32 quantity)
{
    char *notifyStr = NULL;
    langFormatGameMessageKey(entGetLanguage(pEnt), &notifyStr, "NumericConversion.Success", STRFMT_INT("Quantity", quantity), STRFMT_END);
    ClientCmd_NotifySend(pEnt, kNotifyType_NumericConversionSuccess, notifyStr, NULL, NULL);
    estrDestroy(&notifyStr);
}

AUTO_STRUCT;
typedef struct ConvertNumericCBData
{
    ContainerID entID;
    int iPartitionIdx;
} ConvertNumericCBData;

void
ConvertNumeric_CB(TransactionReturnVal *pReturn, ConvertNumericCBData *pData)
{
    Entity *pEnt = entFromContainerID(pData->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pData->entID);
    if ( pEnt )
    {
        if ( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
        {
            U32 quantityConverted = 0;

            if ( !StringToUint(objAutoTransactionGetResult(pReturn), &quantityConverted) )
            {
                quantityConverted = 0;
            }

            NotifySuccess(pEnt, quantityConverted);
        }
        else
        {
            NotifyError(pEnt);
        }
    }
}

static bool
ShouldDoAutoConvert(Entity *pEnt, NumericConversionDef *conversionDef)
{
	NumericConversionState *conversionState = eaIndexedGetUsingString(&pEnt->pPlayer->eaNumericConversionStates, conversionDef->name);
	U32 curTime = timeSecondsSince2000();
	ItemDef *sourceItemDef = GET_REF(conversionDef->hSourceNumeric);
	S32 sourceValue = inv_GetNumericItemValue(pEnt, sourceItemDef->pchName); 

	if ( conversionState != NULL )
	{
		U32 startOfCurrentInterval;
		// They do have a conversion state, so check it to see if they have done a conversion in the current interval.

		// compute the start time of the current time interval
		startOfCurrentInterval = ( curTime / conversionDef->timeIntervalSeconds ) * conversionDef->timeIntervalSeconds;

		// If the time of the last auto conversion was before the current interval
		if ( conversionState->lastAutoConversionTime < startOfCurrentInterval )
		{
			return true;
		}
	}
	else if ( sourceValue != 0 )
	{
		// They don't have a conversion state, but they do have the source numeric, so go ahead and do their first auto conversion.
		return true;
	}

	return false;
}

void
gslDoAutoNumericConversion(Entity *pEnt)
{
	RefDictIterator iter;
	S32 numIntervals;
	GameAccountData *pGameAccount;
	NumericConversionDef *conversionDef;

	if ( pEnt == NULL || pEnt->pPlayer == NULL || pEnt->pPlayer->pPlayerAccountData == NULL )
	{
		return;
	}

	pGameAccount = GET_REF(pEnt->pPlayer->pPlayerAccountData->hData);
	if ( pGameAccount == NULL )
	{
		return;
	}

	RefSystem_InitRefDictIterator("NumericConversion", &iter);

	while (conversionDef = RefSystem_GetNextReferentFromIterator(&iter))
	{
		if ( conversionDef->autoConversionMaxIntervalsToken != NULL )
		{
			if(GetGamePermissionValueUncached(pGameAccount, conversionDef->autoConversionMaxIntervalsToken, &numIntervals))
			{
				if ( numIntervals > 0 && ShouldDoAutoConvert(pEnt, conversionDef) )
				{
					ItemChangeReason reason = {0};
					ConvertNumericCBData *cbData = StructCreate(parse_ConvertNumericCBData);
					TransactionReturnVal *pReturn = LoggedTransactions_CreateManagedReturnValEnt("NumericConversionAuto", pEnt, ConvertNumeric_CB, cbData);

					cbData->entID = pEnt->myContainerID;
					cbData->iPartitionIdx = entGetPartitionIdx(pEnt);

					inv_FillItemChangeReason(&reason, pEnt, "NumericConversionAuto", conversionDef->name);
					AutoTrans_trAutoConvertNumeric(pReturn, GLOBALTYPE_GAMESERVER, pEnt->myEntityType, pEnt->myContainerID, conversionDef->name, numIntervals, &reason);
				}
			}
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_SERVERCMD;
void 
gslConvertNumeric(Entity *pEnt, const char *conversionName)
{
    NumericConversionDef *conversionDef = NumericConversion_DefFromName(conversionName);
    U32 lastConversionTime = 0;
    U32 lastConversionQuantity = 0;
    ItemChangeReason reason = {0};
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

    if ( pEnt == NULL || pEnt->pPlayer == NULL )
    {
        NotifyError(pEnt);
        return;
    }

    if ( conversionDef == NULL )
    {
        NotifyError(pEnt);
        return;
    }

    if ( NumericConversion_QuantityRemaining(pEnt, &pEnt->pPlayer->eaNumericConversionStates, conversionDef) > 0 )
    {
        ConvertNumericCBData *cbData = StructCreate(parse_ConvertNumericCBData);
        TransactionReturnVal *pReturn = LoggedTransactions_CreateManagedReturnValEnt("NumericConversion", pEnt, ConvertNumeric_CB, cbData);

        cbData->entID = pEnt->myContainerID;
        cbData->iPartitionIdx = entGetPartitionIdx(pEnt);

        inv_FillItemChangeReason(&reason, pEnt, "NumericConversion", conversionName);
        AutoTrans_trConvertNumeric(pReturn, GLOBALTYPE_GAMESERVER, pEnt->myEntityType, pEnt->myContainerID, conversionName, &reason, pExtract);
    }
    else
    {
        NotifyError(pEnt);
        return;
    }
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Pplayer.Eanumericconversionstates, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome
trConvertNumeric(ATR_ARGS, NOCONST(Entity) *pEnt, const char *conversionName, const ItemChangeReason *pReason, GameAccountDataExtract* pExtract)
{
    NumericConversionDef *conversionDef = NumericConversion_DefFromName(conversionName);
    S32 convertMax = 0;
    ItemDef *sourceItemDef;
    ItemDef *destItemDef;
    S32 sourceValue;
    S32 convertQuantity;
    NOCONST(NumericConversionState) *conversionState;
    U32 curTime = timeSecondsSince2000();
    U32 startOfCurrentInterval;
    bool newInterval = false;
	S32 quantityPerInterval = conversionDef->quantityPerInterval + NumericConversion_trh_GetBonusQuantity(pEnt,conversionDef, pExtract);

    if ( ISNULL(pEnt) || ISNULL(pEnt->pPlayer) || ISNULL(conversionDef) ) 
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    sourceItemDef = GET_REF(conversionDef->hSourceNumeric);
    destItemDef = GET_REF(conversionDef->hDestNumeric);

    if ( ISNULL(sourceItemDef) || ISNULL(destItemDef) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    conversionState = eaIndexedGetUsingString(&pEnt->pPlayer->eaNumericConversionStates, conversionDef->name);
    if ( conversionState == NULL )
    {
        // if the player doesn't have the needed conversion state then create it
        conversionState = StructCreateNoConst(parse_NumericConversionState);
        SET_HANDLE_FROM_STRING("NumericConversion", conversionDef->name, conversionState->hNumericConversionDef);
        conversionState->lastConversionTime = 0;
        conversionState->quantityConverted = 0;
        eaIndexedAdd(&pEnt->pPlayer->eaNumericConversionStates, conversionState);
    }

    // compute the start time of the current time interval
    startOfCurrentInterval = ( curTime / conversionDef->timeIntervalSeconds ) * conversionDef->timeIntervalSeconds;

    if ( conversionState->lastConversionTime < startOfCurrentInterval )
    {
        // last conversion was in a previous interval, so we can convert the the entire allowed amount
        convertMax = quantityPerInterval;
        newInterval = true;
    } 
    else if ( conversionState->quantityConverted <= quantityPerInterval )
    {
        // last conversion was in the current interval, so calculate the amount remaining that can be converted this interval
        convertMax = quantityPerInterval - conversionState->quantityConverted;
    }

    if ( convertMax == 0 )
    {
        // nothing to convert
        return TRANSACTION_OUTCOME_FAILURE;
    }

    sourceValue = inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt, sourceItemDef->pchName); 

    // calculate actual amount to convert
    if ( sourceValue < convertMax )
    {
        convertQuantity = sourceValue;
    }
    else
    {
        convertQuantity = convertMax;
    }

    // save quantity converted to the result string, so that it can be passed back to the transaction callback
    // NOTE - this must happen before adjusting the numerics, because that will append logging info to the success string
    estrPrintf(ATR_RESULT_SUCCESS, "%u\n", convertQuantity);

    // update the conversion state
    conversionState->lastConversionTime = curTime;
    if ( newInterval )
    {
        conversionState->quantityConverted = convertQuantity;
    }
    else
    {
        conversionState->quantityConverted = convertQuantity + conversionState->quantityConverted;
    }
    devassert(conversionState->quantityConverted <= quantityPerInterval);    

    // transfer the numeric
    if ( !inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, true, sourceItemDef->pchName, -convertQuantity, pReason) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    if ( !inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, true, destItemDef->pchName, convertQuantity, pReason) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Pplayer.Eanumericconversionstates, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[], .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp");
enumTransactionOutcome
trAutoConvertNumeric(ATR_ARGS, NOCONST(Entity) *pEnt, const char *conversionName, U32 maxIntervalsToConvert, const ItemChangeReason *pReason)
{
	NumericConversionDef *conversionDef = NumericConversion_DefFromName(conversionName);
	S32 convertMax = 0;
	ItemDef *sourceItemDef;
	ItemDef *destItemDef;
	S32 sourceValue;
	S32 convertQuantity;
	NOCONST(NumericConversionState) *conversionState;
	U32 curTime = timeSecondsSince2000();
	U32 startOfCurrentInterval;
	bool newInterval = false;
	U32 intervalsToConvert;
	U32 firstIntervalAmountAlreadyConverted;
	U32 curIntervalNum;
	U32 lastConversionIntervalNum;
	U32 lastAutoConversionIntervalNum;

	if ( ISNULL(pEnt) || ISNULL(pEnt->pPlayer) || ISNULL(conversionDef) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	sourceItemDef = GET_REF(conversionDef->hSourceNumeric);
	destItemDef = GET_REF(conversionDef->hDestNumeric);

	if ( ISNULL(sourceItemDef) || ISNULL(destItemDef) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	conversionState = eaIndexedGetUsingString(&pEnt->pPlayer->eaNumericConversionStates, conversionDef->name);
	if ( conversionState == NULL )
	{
		// If the player doesn't have the needed conversion state then create it.
		conversionState = StructCreateNoConst(parse_NumericConversionState);
		SET_HANDLE_FROM_STRING("NumericConversion", conversionDef->name, conversionState->hNumericConversionDef);
		conversionState->lastConversionTime = 0;
		conversionState->quantityConverted = 0;
		conversionState->lastAutoConversionTime = 0;
		eaIndexedAdd(&pEnt->pPlayer->eaNumericConversionStates, conversionState);
	}

	// Compute the start time of the current time interval.
	startOfCurrentInterval = ( curTime / conversionDef->timeIntervalSeconds ) * conversionDef->timeIntervalSeconds;

	// If the last conversion or auto conversion was in the current interval, then don't do anything.
	if ( ( conversionState->lastAutoConversionTime >= startOfCurrentInterval ) || ( conversionState->lastConversionTime >= startOfCurrentInterval ) )
	{
		return TRANSACTION_OUTCOME_SUCCESS;
	}

	// compute the interval numbers for the current interval and the last interval where a conversion happened
	curIntervalNum = curTime / conversionDef->timeIntervalSeconds;
	lastConversionIntervalNum = conversionState->lastConversionTime / conversionDef->timeIntervalSeconds;
	lastAutoConversionIntervalNum = conversionState->lastAutoConversionTime / conversionDef->timeIntervalSeconds;

	if ( lastConversionIntervalNum == 0 )
	{
		// Player has not refined dilithium before.  Only auto convert one interval.
		firstIntervalAmountAlreadyConverted = 0;
		intervalsToConvert = 1;
	}
	else
	{
		intervalsToConvert = curIntervalNum - MAX(lastConversionIntervalNum, lastAutoConversionIntervalNum);
		if ( intervalsToConvert > maxIntervalsToConvert )
		{
			// Limit the number of intervals that will be auto converted.
			intervalsToConvert = maxIntervalsToConvert;
			firstIntervalAmountAlreadyConverted = 0;
		}
		else if ( intervalsToConvert == ( curIntervalNum - lastConversionIntervalNum ) )
		{
			// If the auto conversion includes the last interval where a conversion happened, then account for the quantity converted in that interval.
			firstIntervalAmountAlreadyConverted = conversionState->quantityConverted;
		}
		else
		{
			firstIntervalAmountAlreadyConverted = 0;
		}
	}

	// Make sure we are not converting too many intervals.
	devassert(intervalsToConvert <= ( curIntervalNum - lastConversionIntervalNum ) );
	if ( intervalsToConvert > ( curIntervalNum - lastConversionIntervalNum ) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	devassert(intervalsToConvert <= ( curIntervalNum - lastAutoConversionIntervalNum ) );
	if ( intervalsToConvert > ( curIntervalNum - lastAutoConversionIntervalNum ) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	devassert(intervalsToConvert > 0);
	if ( intervalsToConvert == 0 )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	devassert(intervalsToConvert <= maxIntervalsToConvert);
	if ( intervalsToConvert > maxIntervalsToConvert )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if ( firstIntervalAmountAlreadyConverted > 0 )
	{
		convertMax = ( conversionDef->quantityPerInterval - firstIntervalAmountAlreadyConverted ) + ( ( intervalsToConvert - 1 ) * conversionDef->quantityPerInterval );
	}
	else
	{
		convertMax = intervalsToConvert * conversionDef->quantityPerInterval;
	}

	if ( convertMax == 0 )
	{
		// nothing to convert
		return TRANSACTION_OUTCOME_FAILURE;
	}

	sourceValue = inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt, sourceItemDef->pchName); 

	// calculate actual amount to convert
	if ( sourceValue < convertMax )
	{
		convertQuantity = sourceValue;
	}
	else
	{
		convertQuantity = convertMax;
	}

	// save quantity converted to the result string, so that it can be passed back to the transaction callback
	// NOTE - this must happen before adjusting the numerics, because that will append logging info to the success string
	estrPrintf(ATR_RESULT_SUCCESS, "%u\n", convertQuantity);

	// update the conversion state
	// Since the auto conversion represents conversion in previous intervals, set the last conversion time to the 
	//  start of the current interval and the quantity to zero.  This will allow full conversion in the current interval.
	conversionState->lastConversionTime = startOfCurrentInterval;
	conversionState->lastAutoConversionTime = curTime;
	conversionState->quantityConverted = 0;

	// transfer the numeric
	if ( !inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, true, sourceItemDef->pchName, -convertQuantity, pReason) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if ( !inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, true, destItemDef->pchName, convertQuantity, pReason) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

#include "gslNumericConversion_c_ast.c"
