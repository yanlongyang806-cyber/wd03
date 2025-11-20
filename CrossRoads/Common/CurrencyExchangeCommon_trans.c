/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "CurrencyExchangeCommon.h"
#include "AutoTransDefs.h"
#include "stdtypes.h"
#include "inventoryCommon.h"
#include "CurrencyExchangeCommon.h"
#include "StringCache.h"
#include "Entity.h"
#include "Player.h"
#include "AccountProxyCommon.h"
#include "MicroTransactions.h"
#include "logging.h"
#include "Alerts.h"

#include "AutoGen/CurrencyExchangeCommon_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/Player_h_ast.h"

void
CurrencyExchange_SetTransactionReturnOrderID(ATR_ARGS, U32 orderID)
{
    // save order ID to the result string, so that it can be passed back to the transaction callback
    estrInsertf(ATR_RESULT_SUCCESS, 0, "%u\n", orderID);

    return;
}

// Get the index in the openOrders array of an available order struct
AUTO_TRANS_HELPER;
int 
CurrencyExchange_trh_GetAvailableOrderIndex(ATR_ARGS, ATH_ARG NOCONST(CurrencyExchangeAccountData) *currencyAccountData)
{
    int i;

    // Return one of the existing order structs if there is one that is not in use
    for ( i = eaSize(&currencyAccountData->openOrders) - 1; i >= 0; i-- )
    {
        if ( currencyAccountData->openOrders[i]->orderType == OrderType_None )
        {
            return i;
        }
    }

    if ( eaSize(&currencyAccountData->openOrders) < gCurrencyExchangeConfig.maxPlayerOpenOrders )
    {
        // there is room to create a new order struct
        NOCONST(CurrencyExchangeOpenOrder) *newOrder = StructCreateNoConst(parse_CurrencyExchangeOpenOrder);
        eaPush(&currencyAccountData->openOrders, newOrder);

        // return the index of the new order struct
        return eaSize(&currencyAccountData->openOrders) - 1;
    }

    // there is not an available slot for a new order
    return -1;
}

AUTO_TRANS_HELPER;
bool 
CurrencyExchange_trh_WriteLogEntry(ATR_ARGS, ATH_ARG NOCONST(CurrencyExchangeAccountData) *currencyAccountData, CurrencyExchangeOperationType logType, 
    CurrencyExchangeOrderType orderType, U32 orderID, U32 quantityMTC, U32 quantityTC, U32 time, const char *characterName)
{
    int logSize = eaSize(&currencyAccountData->logEntries);

    devassert(logSize <= gCurrencyExchangeConfig.maxPlayerLogEntries);
    if ( logSize > gCurrencyExchangeConfig.maxPlayerLogEntries )
    {
        return false;
    }

    devassert(logSize == gCurrencyExchangeConfig.maxPlayerLogEntries || (U32)logSize == currencyAccountData->logNext);
    if ( logSize != gCurrencyExchangeConfig.maxPlayerLogEntries && (U32)logSize != currencyAccountData->logNext )
    {
        return false;
    }

    devassert(currencyAccountData->logNext < (U32)gCurrencyExchangeConfig.maxPlayerLogEntries);
    if ( currencyAccountData->logNext >= (U32)gCurrencyExchangeConfig.maxPlayerLogEntries )
    {
        return false;
    }

    if ( currencyAccountData->logNext == (U32)logSize )
    {
        NOCONST(CurrencyExchangeLogEntry) *newLogEntry = StructCreateNoConst(parse_CurrencyExchangeLogEntry);
        eaPush(&currencyAccountData->logEntries, newLogEntry);
        logSize++;
    }

    currencyAccountData->logEntries[currencyAccountData->logNext]->logType = logType;
    currencyAccountData->logEntries[currencyAccountData->logNext]->orderType = orderType;
    currencyAccountData->logEntries[currencyAccountData->logNext]->orderID = orderID;
    currencyAccountData->logEntries[currencyAccountData->logNext]->quantityMTC = quantityMTC;
    currencyAccountData->logEntries[currencyAccountData->logNext]->quantityTC = quantityTC;
    currencyAccountData->logEntries[currencyAccountData->logNext]->time = time;
    currencyAccountData->logEntries[currencyAccountData->logNext]->characterName = allocAddString(characterName);

    currencyAccountData->logNext++;
    if ( currencyAccountData->logNext >= (U32)gCurrencyExchangeConfig.maxPlayerLogEntries )
    {
        currencyAccountData->logNext = 0;
    }

    return true;
}

void
CurrencyExchange_SetTransactionReturnErrorEx(ATR_ARGS, CurrencyExchangeResultType resultType, const char *funcName, int lineNum, const char *logFormat, ...)
{
    static char * failStr = NULL;

    estrPrintf(&failStr, "%d:", resultType);
    VA_START(args, logFormat);
    estrConcatfv_dbg(&failStr, funcName, lineNum, logFormat, args);

    estrInsertf(ATR_RESULT_FAIL, 0, "%s\n", failStr);
    VA_END();

    return;
}

AUTO_TRANSACTION
ATR_LOCKS(currencyAccountData, ".Iaccountid, .Readytoclaimescrowtc, .Nextorderid, .Forsaleescrowtc, .Openorders, .Lognext, .Logentries")
ATR_LOCKS(playerEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Pplayer.Accountid, .Pplayer.Pugckillcreditlimit, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome CurrencyExchange_tr_CreateBuyOrder(ATR_ARGS, NOCONST(CurrencyExchangeAccountData) *currencyAccountData, NOCONST(Entity) *playerEnt, CurrencyExchangeOperationRequest *operation, const char *tcNumericName)
{
    U32 escrowTCAmount;
    U32 escrowTCFromNumeric;
    int orderIndex;
    U32 orderID;
    ItemChangeReason reason = {0};

    if ( ISNULL(playerEnt) || ISNULL(playerEnt->pPlayer) || ISNULL(currencyAccountData) || ISNULL(operation) )
    {
        CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "NULL entity, player, account data or operation");
        return TRANSACTION_OUTCOME_FAILURE;
    }

    devassert(operation->accountID == currencyAccountData->iAccountID && operation->accountID == playerEnt->pPlayer->accountID);
    if ( operation->accountID != currencyAccountData->iAccountID || operation->accountID != playerEnt->pPlayer->accountID )
    {
        CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "Account ID mismatch:operation=%u, account data=%u, player=%u", operation->accountID, currencyAccountData->iAccountID, playerEnt->pPlayer->accountID);
        return TRANSACTION_OUTCOME_FAILURE;
    }

    devassert(operation->operationType == CurrencyOpType_CreateOrder);
    if ( operation->operationType != CurrencyOpType_CreateOrder )
    {
        CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "Invalid operation type, expecting CreateOrder, got: %d", operation->operationType);
        return TRANSACTION_OUTCOME_FAILURE;
    }

    devassert(operation->orderType == OrderType_Buy);
    if ( operation->orderType != OrderType_Buy )
    {
        CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "Invalid order type, expecting Buy, got: %d", operation->orderType);
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // compute the full price of the order
    escrowTCAmount = operation->price * operation->quantity;
    escrowTCFromNumeric = 0;

    if ( currencyAccountData->readyToClaimEscrowTC >= escrowTCAmount )
    {
        // full escrow amount comes from ready to claim balance
        currencyAccountData->readyToClaimEscrowTC -= escrowTCAmount;
    }
    else if ( currencyAccountData->readyToClaimEscrowTC > 0 )
    {
        // part of escrow amount comes from ready to claim balance, and the rest comes from the numeric
        escrowTCFromNumeric = escrowTCAmount - currencyAccountData->readyToClaimEscrowTC;
        currencyAccountData->readyToClaimEscrowTC = 0;
    }
    else
    {
        // there is no ready to claim balance so full escrow amount comes from numeric
        escrowTCFromNumeric = escrowTCAmount;
    }

    orderID = currencyAccountData->nextOrderID;

    if ( escrowTCFromNumeric > 0 )
    {
        inv_FillItemChangeReason(&reason, NULL, "CurrencyExchange:EscrowTC", playerEnt->debugName);
        if ( !inv_ent_trh_AddNumeric(ATR_PASS_ARGS, playerEnt, false, tcNumericName, -((S32)escrowTCFromNumeric), &reason ) )
        {
            CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_NotEnoughTC, "Not enough TC to escrow for Buy order.  totalTC=%u, fromNumeric=%u", escrowTCAmount, escrowTCFromNumeric);
            return TRANSACTION_OUTCOME_FAILURE;
        }
        // log the escrowing of TC from the character numeric
        if ( !CurrencyExchange_trh_WriteLogEntry(ATR_PASS_ARGS, currencyAccountData, CurrencyOpType_EscrowTC, OrderType_None, orderID, 0, escrowTCFromNumeric, operation->arrivalTime, operation->characterName ) )
        {
            CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "Error writing buy order TC escrow log.");
            return TRANSACTION_OUTCOME_FAILURE;
        }
    }

    // add the full price of the order to the escrow bucket
    currencyAccountData->forSaleEscrowTC += escrowTCAmount;

    // get the index of an available order struct
    orderIndex = CurrencyExchange_trh_GetAvailableOrderIndex(ATR_PASS_ARGS, currencyAccountData);
    if ( orderIndex < 0 || orderIndex > ( gCurrencyExchangeConfig.maxPlayerOpenOrders - 1 ) )
    {
        CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_TooManyOrders, "Order index out of range.  Most likely due to too many orders. orderIndex=%d", orderIndex);
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // initialize the order
    currencyAccountData->openOrders[orderIndex]->orderType = OrderType_Buy;
    currencyAccountData->openOrders[orderIndex]->orderID = orderID;
    currencyAccountData->nextOrderID++;

    currencyAccountData->openOrders[orderIndex]->listingTime = operation->arrivalTime;
    currencyAccountData->openOrders[orderIndex]->price = operation->price;
    currencyAccountData->openOrders[orderIndex]->quantity = operation->quantity;

    currencyAccountData->openOrders[orderIndex]->consumedTC = 0;
    currencyAccountData->openOrders[orderIndex]->consumedMTC = 0;

    // Log the order
    if ( !CurrencyExchange_trh_WriteLogEntry(ATR_PASS_ARGS, currencyAccountData, CurrencyOpType_CreateOrder, OrderType_Buy, orderID, operation->quantity, operation->price, operation->arrivalTime, operation->characterName ) )
    {
        CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "Error writing buy order creation log.");
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // save order ID to the result string, so that it can be passed back to the transaction callback
    CurrencyExchange_SetTransactionReturnOrderID(ATR_PASS_ARGS, orderID);

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(currencyAccountData, ".Readytoclaimescrowtc, .Lognext, .Logentries")
ATR_LOCKS(playerEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Pplayer.Pugckillcreditlimit, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome 
CurrencyExchange_tr_ClaimTC(ATR_ARGS, NOCONST(CurrencyExchangeAccountData) *currencyAccountData, NOCONST(Entity) *playerEnt, U32 quantity, U32 operationTime, const char *characterName, const char *tcNumericName)
{
    ItemChangeReason reason = {0};

    if ( ISNULL(currencyAccountData) || ISNULL(playerEnt) )
    {
        CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "NULL account data or player entity");
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // make sure there is enough escrowed TC
    if ( quantity > currencyAccountData->readyToClaimEscrowTC )
    {
        CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_NotEnoughTC, "trying to claim too much TC");
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // subtract from escrow bucket
    currencyAccountData->readyToClaimEscrowTC -= quantity;

    // add to player numeric
    inv_FillItemChangeReason(&reason, NULL, "CurrencyExchange:ClaimTC", playerEnt->debugName);
    if ( !inv_ent_trh_AddNumeric(ATR_PASS_ARGS, playerEnt, false, tcNumericName, (S32)quantity, &reason ) )
    {
        CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_NotEnoughTC, "Add numeric failure when claiming TC.  quantity=%u", quantity);
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // log the transfer of TC from the escrow bucket to the character numeric
    if ( !CurrencyExchange_trh_WriteLogEntry(ATR_PASS_ARGS, currencyAccountData, CurrencyOpType_ClaimTC, OrderType_None, 0, 0, quantity, operationTime, characterName ) )
    {
        CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "Error writing claim TC log.");
        return TRANSACTION_OUTCOME_FAILURE;
    }

    return TRANSACTION_OUTCOME_SUCCESS;
}
