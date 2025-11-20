/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "aslCurrencyExchange.h"
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

AUTO_TRANSACTION
ATR_LOCKS(currencyAccountData, ".Iaccountid, .Nextorderid");
enumTransactionOutcome CurrencyExchange_tr_InitAccountData(ATR_ARGS, NOCONST(CurrencyExchangeAccountData) *currencyAccountData, ContainerID accountID)
{
    currencyAccountData->iAccountID = accountID;
    currencyAccountData->nextOrderID = 1;

    return TRANSACTION_OUTCOME_SUCCESS;
}

//
// This transaction is used to fix up an order with an ID of zero.  Note that it will not fix existing log entries.
//
AUTO_TRANSACTION
ATR_LOCKS(currencyAccountData, ".Openorders, .Nextorderid");
enumTransactionOutcome CurrencyExchange_tr_FixupBadOrderID(ATR_ARGS, NOCONST(CurrencyExchangeAccountData) *currencyAccountData)
{
    int i;

    for ( i = 0; i < eaSize(&currencyAccountData->openOrders); i++ )
    {
        if ( ( currencyAccountData->openOrders[i]->orderID == 0 ) && ( currencyAccountData->openOrders[i]->orderType != OrderType_None ) )
        {
            currencyAccountData->openOrders[i]->orderID = currencyAccountData->nextOrderID;
            currencyAccountData->nextOrderID++;
        }
    }

    return TRANSACTION_OUTCOME_SUCCESS;
}

//
// This transaction is used to remove any orders that were listed as withdrawn or had been completely fulfilled when scanning all order containers.
//
AUTO_TRANSACTION
ATR_LOCKS(currencyAccountData, ".Iaccountid, .Openorders");
enumTransactionOutcome CurrencyExchange_tr_RemoveOrders(ATR_ARGS, NOCONST(CurrencyExchangeAccountData) *currencyAccountData, CurrencyOrdersToRemove *removeOrders)
{
    int i, j;

    devassert(removeOrders->accountID == currencyAccountData->iAccountID);
    if ( removeOrders->accountID != currencyAccountData->iAccountID )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // remove any order that has an orderID matching one of the orderIDs in the removal list
    for ( i = 0; i < ea32Size(&removeOrders->orderIDs); i++ )
    {
        U32 orderID = removeOrders->orderIDs[i];

        for ( j = 0; j < eaSize(&currencyAccountData->openOrders); j++ )
        {
            if ( currencyAccountData->openOrders[j]->orderID == orderID )
            {
                currencyAccountData->openOrders[j]->orderID = 0;
                currencyAccountData->openOrders[j]->orderType = OrderType_None;

                break;
            }
        }
    }

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(currencyAccountData, ".Iaccountid, .Nextorderid, .Openorders, .Lognext, .Logentries")
ATR_LOCKS(srcMTCLock, ".Plock.Uaccountid, .Plock.Result, .Plock.Fdestroytime, .Plock.Pkey, .Plock.Etransactiontype")
ATR_LOCKS(destMTCLock, ".Plock.Uaccountid, .Plock.Result, .Plock.Fdestroytime, .Plock.Pkey, .Plock.Etransactiontype");
enumTransactionOutcome CurrencyExchange_tr_CreateSellOrder(ATR_ARGS, NOCONST(CurrencyExchangeAccountData) *currencyAccountData, 
	NOCONST(AccountProxyLockContainer) *srcMTCLock, NOCONST(AccountProxyLockContainer) *destMTCLock, CurrencyExchangeOperationRequest *operation)
{
    U32 orderID;
    int orderIndex;

    if ( ISNULL(currencyAccountData) || ISNULL(srcMTCLock) || ISNULL(operation) )
    {
        CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "NULL account data, MTC Lock or operation");
        return TRANSACTION_OUTCOME_FAILURE;
    }

    devassert(operation->accountID == currencyAccountData->iAccountID);
    if ( operation->accountID != currencyAccountData->iAccountID )
    {
        CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "Account ID mismatch:operation=%u, account data=%u", operation->accountID, currencyAccountData->iAccountID);
        return TRANSACTION_OUTCOME_FAILURE;
    }

    devassert(operation->operationType == CurrencyOpType_CreateOrder);
    if ( operation->operationType != CurrencyOpType_CreateOrder )
    {
        CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "Invalid operation type, expecting CreateOrder, got: %d", operation->operationType);
        return TRANSACTION_OUTCOME_FAILURE;
    }

    devassert(operation->orderType == OrderType_Sell);
    if ( operation->orderType != OrderType_Sell )
    {
        CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "Invalid order type, expecting Sell, got: %d", operation->orderType);
        return TRANSACTION_OUTCOME_FAILURE;
    }

    orderID = currencyAccountData->nextOrderID;

    // commit source MTC chain account proxy lock
    if ( !APFinalizeKeyValue(srcMTCLock, operation->accountID, microtrans_GetShardCurrency(), APRESULT_COMMIT, TransLogType_Exchange) )
    {
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "Failed to commit source lock");
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // commit for sale bucket account proxy lock
    if ( NONNULL(destMTCLock) && !APFinalizeKeyValue(destMTCLock, operation->accountID, microtrans_GetShardForSaleBucketKey(), APRESULT_COMMIT, TransLogType_Exchange) )
    {
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "Failed to commit destination lock");
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // log the escrowing of MTC
    if ( !CurrencyExchange_trh_WriteLogEntry(ATR_PASS_ARGS, currencyAccountData, CurrencyOpType_EscrowMTC, OrderType_None, orderID, operation->quantity, 0, operation->arrivalTime, operation->characterName ) )
    {
        CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "Error writing sell order MTC escrow log.");
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // get the index of an available order struct
    orderIndex = CurrencyExchange_trh_GetAvailableOrderIndex(ATR_PASS_ARGS, currencyAccountData);
    if ( orderIndex < 0 || orderIndex > ( gCurrencyExchangeConfig.maxPlayerOpenOrders - 1 ) )
    {
        CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_TooManyOrders, "Order index out of range.  Most likely due to too many orders. orderIndex=%d", orderIndex);
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // initialize the order
    currencyAccountData->openOrders[orderIndex]->orderType = OrderType_Sell;
    currencyAccountData->openOrders[orderIndex]->orderID = orderID;
    currencyAccountData->nextOrderID++;

    currencyAccountData->openOrders[orderIndex]->listingTime = operation->arrivalTime;
    currencyAccountData->openOrders[orderIndex]->price = operation->price;
    currencyAccountData->openOrders[orderIndex]->quantity = operation->quantity;

	currencyAccountData->openOrders[orderIndex]->consumedTC = 0;
	currencyAccountData->openOrders[orderIndex]->consumedMTC = 0;

    // Log the order
    if ( !CurrencyExchange_trh_WriteLogEntry(ATR_PASS_ARGS, currencyAccountData, CurrencyOpType_CreateOrder, OrderType_Sell, orderID, operation->quantity, operation->price, operation->arrivalTime, operation->characterName ) )
    {
        CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "Error writing sell order creation log.");
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // save order ID to the result string, so that it can be passed back to the transaction callback
    CurrencyExchange_SetTransactionReturnOrderID(ATR_PASS_ARGS, orderID);

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER;
static int
CurrencyExchange_trh_FindOrderIndex(ATH_ARG NOCONST(CurrencyExchangeAccountData) *currencyAccountData, U32 orderID)
{
    int i;

    if ( orderID != 0 )
    {
        for ( i = 0; i < eaSize(&currencyAccountData->openOrders); i++ )
        {
            if ( currencyAccountData->openOrders[i]->orderID == orderID )
            {
                return i;
            }
        }
    }

    return -1;
}

//
// Withdraw a buy order.  Move any remaining escrowed TC from the "for sale" escrow bucket to the "ready to claim" escrow bucket.
//
AUTO_TRANSACTION
ATR_LOCKS(currencyAccountData, ".Openorders, .Forsaleescrowtc, .Readytoclaimescrowtc, .Lognext, .Logentries");
enumTransactionOutcome
CurrencyExchange_tr_WithdrawBuyOrder(ATR_ARGS, NOCONST(CurrencyExchangeAccountData) *currencyAccountData, U32 orderID, U32 timeWithdrawn, const char *characterName)
{
    U32 fullEscrowAmount;
    U32 consumed = 0;
    int orderIndex;
	U32 returnedTC;

    if ( ISNULL(currencyAccountData) )
    {
        CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "NULL account data");
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // find the order
    orderIndex = CurrencyExchange_trh_FindOrderIndex(currencyAccountData, orderID);
    if ( orderIndex < 0 )
    {
        CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "order not found");
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // compute the amount that was originally escrowed for this order
    fullEscrowAmount = currencyAccountData->openOrders[orderIndex]->quantity * currencyAccountData->openOrders[orderIndex]->price;

	// it is an error if the order has consumed more than was originally escrowed
    if ( currencyAccountData->openOrders[orderIndex]->consumedTC > fullEscrowAmount )
    {
        CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "consumed greater than original escrowed amount");
        return TRANSACTION_OUTCOME_FAILURE;
    }

	// compute amount to return from "for sale" escrow bucket to "ready to claim" bucket
	returnedTC = fullEscrowAmount - currencyAccountData->openOrders[orderIndex]->consumedTC;

	// check that there is enough TC in "for sale" escrow bucket
	if ( returnedTC > currencyAccountData->forSaleEscrowTC )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "returning greater than current escrowed balance");
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// move amount to return from "for sale" escrow bucket to "ready to claim" escrow bucket
	currencyAccountData->forSaleEscrowTC -= returnedTC;
	currencyAccountData->readyToClaimEscrowTC += returnedTC;

	// clear out the order
	currencyAccountData->openOrders[orderIndex]->orderType = OrderType_None;
	currencyAccountData->openOrders[orderIndex]->orderID = 0;

	currencyAccountData->openOrders[orderIndex]->listingTime = 0;
	currencyAccountData->openOrders[orderIndex]->price = 0;
	currencyAccountData->openOrders[orderIndex]->quantity = 0;

	currencyAccountData->openOrders[orderIndex]->consumedTC = 0;
	currencyAccountData->openOrders[orderIndex]->consumedMTC = 0;

	// do logging
	if ( !CurrencyExchange_trh_WriteLogEntry(ATR_PASS_ARGS, currencyAccountData, CurrencyOpType_WithdrawOrder, OrderType_Buy, orderID, 0, returnedTC, timeWithdrawn, characterName ) )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "Error writing withdraw buy order log.");
		return TRANSACTION_OUTCOME_FAILURE;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

//
// Withdraw a sell order.  Move any remaining escrowed MTC from the "for sale" escrow bucket to the "ready to claim" escrow bucket.
//
AUTO_TRANSACTION
ATR_LOCKS(currencyAccountData, ".Openorders, .Iaccountid, .Lognext, .Logentries")
ATR_LOCKS(srcMTCLock, ".Plock.Uaccountid, .Plock.Result, .Plock.Fdestroytime, .Plock.Pkey, .Plock.Etransactiontype")
ATR_LOCKS(destMTCLock, ".Plock.Uaccountid, .Plock.Result, .Plock.Fdestroytime, .Plock.Pkey, .Plock.Etransactiontype");
enumTransactionOutcome
CurrencyExchange_tr_WithdrawSellOrder(ATR_ARGS, NOCONST(CurrencyExchangeAccountData) *currencyAccountData, NOCONST(AccountProxyLockContainer) *srcMTCLock, 
	NOCONST(AccountProxyLockContainer) *destMTCLock, U32 orderID, U32 withdrawalQuantity, U32 timeWithdrawn, const char *characterName)
{
	int orderIndex;

	if ( ISNULL(currencyAccountData) || ISNULL(srcMTCLock) )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "NULL account data, source lock or dest lock");
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Find the order.
	orderIndex = CurrencyExchange_trh_FindOrderIndex(currencyAccountData, orderID);
	if ( orderIndex < 0 )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "order not found");
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// It is an error if the order has consumed more than was originally escrowed.
	if ( currencyAccountData->openOrders[orderIndex]->consumedMTC > currencyAccountData->openOrders[orderIndex]->quantity )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "consumed greater than original escrowed amount");
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Make sure the withdrawn quantity is exactly the amount of MTC that has not been sold yet.
	if ( withdrawalQuantity != ( currencyAccountData->openOrders[orderIndex]->quantity - currencyAccountData->openOrders[orderIndex]->consumedMTC) )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "returning greater than current escrowed balance");
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// commit "for sale" escrow bucket account proxy lock
	if ( !APFinalizeKeyValue(srcMTCLock, currencyAccountData->iAccountID, microtrans_GetShardForSaleBucketKey(), APRESULT_COMMIT, TransLogType_Exchange) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// commit "ready to claim" escrow bucket account proxy lock
	if ( NONNULL(destMTCLock) && !APFinalizeKeyValue(destMTCLock, currencyAccountData->iAccountID, microtrans_GetShardReadyToClaimBucketKey(), APRESULT_COMMIT, TransLogType_Exchange) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// clear out the order
	currencyAccountData->openOrders[orderIndex]->orderType = OrderType_None;
	currencyAccountData->openOrders[orderIndex]->orderID = 0;

	currencyAccountData->openOrders[orderIndex]->listingTime = 0;
	currencyAccountData->openOrders[orderIndex]->price = 0;
	currencyAccountData->openOrders[orderIndex]->quantity = 0;

	currencyAccountData->openOrders[orderIndex]->consumedTC = 0;
	currencyAccountData->openOrders[orderIndex]->consumedMTC = 0;

	// do logging
	if ( !CurrencyExchange_trh_WriteLogEntry(ATR_PASS_ARGS, currencyAccountData, CurrencyOpType_WithdrawOrder, OrderType_Sell, orderID, withdrawalQuantity, 0, timeWithdrawn, characterName ) )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "Error writing withdraw sell order log.");
		return TRANSACTION_OUTCOME_FAILURE;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(currencyAccountData, ".Iaccountid, .Lognext, .Logentries")
ATR_LOCKS(srcMTCLock, ".Plock.Uaccountid, .Plock.Result, .Plock.Fdestroytime, .Plock.Pkey, .Plock.Etransactiontype")
ATR_LOCKS(destMTCLock, ".Plock.Uaccountid, .Plock.Result, .Plock.Fdestroytime, .Plock.Pkey, .Plock.Etransactiontype");
enumTransactionOutcome 
CurrencyExchange_tr_ClaimMTC(ATR_ARGS, NOCONST(CurrencyExchangeAccountData) *currencyAccountData, NOCONST(AccountProxyLockContainer) *srcMTCLock, 
	NOCONST(AccountProxyLockContainer) *destMTCLock, U32 quantity, U32 operationTime, const char *characterName)
{
	ItemChangeReason reason = {0};

	if ( ISNULL(currencyAccountData) || ISNULL(srcMTCLock) )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "NULL account data, source lock or destination lock");
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// commit "ready to claim" escrow bucket account proxy lock
	if ( !APFinalizeKeyValue(srcMTCLock, currencyAccountData->iAccountID, microtrans_GetShardReadyToClaimBucketKey(), APRESULT_COMMIT, TransLogType_Exchange) )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "failed to finalize source lock");
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// commit claim destination account proxy lock
	if ( NONNULL(destMTCLock) && !APFinalizeKeyValue(destMTCLock, currencyAccountData->iAccountID, microtrans_GetExchangeWithdrawCurrency(), APRESULT_COMMIT, TransLogType_Exchange) )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "failed to finalize destination lock");
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// log the transfer of MTC from the escrow bucket to the MTC chain
	if ( !CurrencyExchange_trh_WriteLogEntry(ATR_PASS_ARGS, currencyAccountData, CurrencyOpType_ClaimMTC, OrderType_None, 0, quantity, 0, operationTime, characterName ) )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "Error writing claim MTC log.");
		return TRANSACTION_OUTCOME_FAILURE;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER;
enumTransactionOutcome
CurrencyExchange_trh_FulfillOrder(ATR_ARGS, ATH_ARG NOCONST(CurrencyExchangeAccountData) *sellAccountData, ATH_ARG NOCONST(CurrencyExchangeAccountData) *buyAccountData, 
	ATH_ARG NOCONST(AccountProxyLockContainer) *srcMTCLock, ATH_ARG NOCONST(AccountProxyLockContainer) *destMTCLock, U32 sellOrderID, U32 buyOrderID, U32 price, U32 quantity, 
	int sellerBuyerSame, U32 operationTime)
{
	int sellOrderIndex;
	int buyOrderIndex;
	U32 quantityTC = quantity * price;
	NOCONST(CurrencyExchangeOpenOrder) *sellOrder;
	NOCONST(CurrencyExchangeOpenOrder) *buyOrder;
    U32 returnTC = 0;

	if ( ISNULL(sellAccountData) || ISNULL(buyAccountData) || ISNULL(srcMTCLock) )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "NULL account data, source lock or destination lock");
		return TRANSACTION_OUTCOME_FAILURE;
	}

    if ( ( sellAccountData->iAccountID == buyAccountData->iAccountID ) && !sellerBuyerSame )
    {
        CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "Seller and buyer accounts are the same and shouldn't be");
        return TRANSACTION_OUTCOME_FAILURE;
    }

	// Find the sell order.
	sellOrderIndex = CurrencyExchange_trh_FindOrderIndex(sellAccountData, sellOrderID);
	if ( sellOrderIndex < 0 )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "sell order not found");
		return TRANSACTION_OUTCOME_FAILURE;
	}
	sellOrder = sellAccountData->openOrders[sellOrderIndex];

	// Find the buy order.
	buyOrderIndex = CurrencyExchange_trh_FindOrderIndex(buyAccountData, buyOrderID);
	if ( buyOrderIndex < 0 )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "buy order not found");
		return TRANSACTION_OUTCOME_FAILURE;
	}
	buyOrder = buyAccountData->openOrders[buyOrderIndex];

	// Check order types.
	if  ( sellOrder->orderType != OrderType_Sell )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "sell order doesn't have sell type");
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if  ( buyOrder->orderType != OrderType_Buy )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "buy order doesn't have buy type");
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Ensure that the unconsumed parts of the orders is large enough
	if ( ( sellOrder->consumedMTC + quantity ) > sellOrder->quantity )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "not enough MTC in sell order");
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if ( ( buyOrder->consumedTC + quantityTC ) > ( buyOrder->quantity * buyOrder->price ) )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "not enough TC in buy order");
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if ( ( buyOrder->consumedMTC + quantity ) > buyOrder->quantity )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "not enough MTC in buy order");
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Ensure that the buyer has enough escrowed TC
	if ( buyAccountData->forSaleEscrowTC < quantityTC )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "not enough TC in buyer's for sale escrow bucket");
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Adjust the consumed amounts in the orders
	sellOrder->consumedMTC += quantity;
	sellOrder->consumedTC += quantityTC;

	buyOrder->consumedMTC += quantity;
	buyOrder->consumedTC += quantityTC;

	// Move TC from buyer's "for sale" escrow bucket to seller's "ready to claim" escrow bucket.
	buyAccountData->forSaleEscrowTC -= quantityTC;
	sellAccountData->readyToClaimEscrowTC += quantityTC;

	// Commit seller's "for sale" escrow bucket account proxy lock.
	if ( !APFinalizeKeyValue(srcMTCLock, sellAccountData->iAccountID, microtrans_GetShardForSaleBucketKey(), APRESULT_COMMIT, TransLogType_Exchange) )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "failed to finalize source(seller for sale) lock");
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Commit buyer's "ready to claim" escrow bucket account proxy lock.
	if ( NONNULL(destMTCLock) && !APFinalizeKeyValue(destMTCLock, buyAccountData->iAccountID, microtrans_GetShardReadyToClaimBucketKey(), APRESULT_COMMIT, TransLogType_Exchange) )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "failed to finalize destination(buyer ready to claim) lock");
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// log the order fulfillment to the seller
	if ( !CurrencyExchange_trh_WriteLogEntry(ATR_PASS_ARGS, sellAccountData, CurrencyOpType_FulfillOrder, OrderType_Sell, sellOrderID, quantity, quantityTC, operationTime, NULL ) )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "Error writing seller fulfill order log.");
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// log the order fulfillment to the buyer
	if ( !CurrencyExchange_trh_WriteLogEntry(ATR_PASS_ARGS, buyAccountData, CurrencyOpType_FulfillOrder, OrderType_Buy, buyOrderID, quantity, quantityTC, operationTime, NULL ) )
	{
		CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "Error writing seller fulfill order log.");
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Clean up the sell order if it is fully consumed.
	if ( sellOrder->consumedMTC == sellOrder->quantity )
	{
		sellOrder->orderType = OrderType_None;
		sellOrder->orderID = 0;

		sellOrder->listingTime = 0;
		sellOrder->price = 0;
		sellOrder->quantity = 0;

		sellOrder->consumedTC = 0;
		sellOrder->consumedMTC = 0;
	}

	// Clean up the buy order if it is fully consumed.
	if ( buyOrder->consumedMTC == buyOrder->quantity )
	{
		U32 escrowedTC = buyOrder->quantity * buyOrder->price;
		returnTC = escrowedTC - buyOrder->consumedTC;

		if ( returnTC > 0 )
		{
			// Ensure there is enough TC in the "for sale" escrow bucket to return.
			if ( buyAccountData->forSaleEscrowTC < returnTC )
			{
				CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "not enough TC in buyer's for sale escrow bucket to return excess when buy order completed");
				return TRANSACTION_OUTCOME_FAILURE;
			}

			// Move any unused TC from the "for sale" bucket to the "ready to claim" bucket
			buyAccountData->forSaleEscrowTC -= returnTC;
			buyAccountData->readyToClaimEscrowTC += returnTC;

			// log the return of excess TC to the buyer
			if ( !CurrencyExchange_trh_WriteLogEntry(ATR_PASS_ARGS, buyAccountData, CurrencyOpType_ExcessTCReturn, OrderType_Buy, buyOrderID, 0, returnTC, operationTime, NULL ) )
			{
				CurrencyExchange_SetTransactionReturnError(pestrATRSuccess, pestrATRFail, CurrencyResultType_InternalError, "Error writing buyer excess TC return log.");
				return TRANSACTION_OUTCOME_FAILURE;
			}
		}

		// Clear out order
		buyOrder->orderType = OrderType_None;
		buyOrder->orderID = 0;

		buyOrder->listingTime = 0;
		buyOrder->price = 0;
		buyOrder->quantity = 0;

		buyOrder->consumedTC = 0;
		buyOrder->consumedMTC = 0;
	}

    // Do economy logging.
    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_CURRENCYEX_ECONOMY, "CurrencyExchangeFulfillOrder", "SellerAccount %u BuyerAccount %u Price %u Quantity %u TCReturned %u", 
        sellAccountData->iAccountID, buyAccountData->iAccountID, price, quantity, returnTC);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(sellAccountData, ".Iaccountid, .Openorders, .Readytoclaimescrowtc, .Lognext, .Logentries, .Forsaleescrowtc")
ATR_LOCKS(buyAccountDataIn, ".Iaccountid, .Openorders, .Forsaleescrowtc, .Readytoclaimescrowtc, .Lognext, .Logentries")
ATR_LOCKS(srcMTCLock, ".Plock.Uaccountid, .Plock.Result, .Plock.Fdestroytime, .Plock.Pkey, .Plock.Etransactiontype")
ATR_LOCKS(destMTCLock, ".Plock.Uaccountid, .Plock.Result, .Plock.Fdestroytime, .Plock.Pkey, .Plock.Etransactiontype");
enumTransactionOutcome
CurrencyExchange_tr_FulfillOrder(ATR_ARGS, NOCONST(CurrencyExchangeAccountData) *sellAccountData, NOCONST(CurrencyExchangeAccountData) *buyAccountDataIn, 
    NOCONST(AccountProxyLockContainer) *srcMTCLock, NOCONST(AccountProxyLockContainer) *destMTCLock, U32 sellOrderID, U32 buyOrderID, U32 price, U32 quantity, 
    int sellerBuyerSame, U32 operationTime)
{
    if ( ISNULL(buyAccountDataIn) )
    {
        return CurrencyExchange_trh_FulfillOrder(ATR_PASS_ARGS, sellAccountData, sellAccountData, srcMTCLock, destMTCLock, sellOrderID, buyOrderID, price, quantity, sellerBuyerSame, operationTime);
    }
    else
    {
        return CurrencyExchange_trh_FulfillOrder(ATR_PASS_ARGS, sellAccountData, buyAccountDataIn, srcMTCLock, destMTCLock, sellOrderID, buyOrderID, price, quantity, sellerBuyerSame, operationTime);
    }
}

AUTO_TRANSACTION
ATR_LOCKS(accountData, ".Openorders, .Readytoclaimescrowtc, .Forsaleescrowtc, .Iaccountid, .Lognext, .Logentries");
enumTransactionOutcome
CurrencyExchange_tr_CancelAllOrdersShardOnly(ATR_ARGS, NOCONST(CurrencyExchangeAccountData) *accountData)
{
    int i;
    U32 curTime = timeSecondsSince2000();

    for ( i = eaSize(&accountData->openOrders) - 1; i >= 0; i-- )
    {
        NOCONST(CurrencyExchangeOpenOrder) *openOrder = accountData->openOrders[i];

        if ( openOrder->orderType == OrderType_Buy )
        {
            U32 fullEscrowAmount = openOrder->quantity * openOrder->price;
            U32 returnedTC = fullEscrowAmount - openOrder->consumedTC;

            // Log the withdrawn buy order.
            CurrencyExchange_trh_WriteLogEntry(ATR_PASS_ARGS, accountData, CurrencyOpType_WithdrawOrder, OrderType_Buy, openOrder->orderID, 0, returnedTC, curTime, "" );
        }
        else if ( openOrder->orderType == OrderType_Sell )
        {
            U32 withdrawalQuantity = openOrder->quantity - openOrder->consumedMTC;

            // Log the withdrawn sell order.
            CurrencyExchange_trh_WriteLogEntry(ATR_PASS_ARGS, accountData, CurrencyOpType_WithdrawOrder, OrderType_Sell, openOrder->orderID, withdrawalQuantity, 0, curTime, "" );
        }
        else if ( openOrder->orderType == OrderType_None )
        {
            // Do nothing.
        }
        else
        {
            ErrorDetailsf("accountID=%d, orderID=%d", accountData->iAccountID, openOrder->orderID);
            Errorf("CurrencyExchange_tr_CancelAllOrdersShardOnly: bad order type");
            TriggerAlert("CURRENCYEXCHANGE_CANCELORDERSFAILURE", 
                STACK_SPRINTF("CurrencyExchange_tr_CancelAllOrdersShardOnly: bad order type.  accountID=%d, orderID=%d", accountData->iAccountID, openOrder->orderID), 
                ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0);
        }

        // Clear out order
        openOrder->orderType = OrderType_None;
        openOrder->orderID = 0;

        openOrder->listingTime = 0;
        openOrder->price = 0;
        openOrder->quantity = 0;

        openOrder->consumedTC = 0;
        openOrder->consumedMTC = 0;
    }

    // Move all TC in the for sale escrow bucket into the ready to claim escrow bucket.
    accountData->readyToClaimEscrowTC += accountData->forSaleEscrowTC;
    accountData->forSaleEscrowTC = 0;

    return TRANSACTION_OUTCOME_SUCCESS;
}