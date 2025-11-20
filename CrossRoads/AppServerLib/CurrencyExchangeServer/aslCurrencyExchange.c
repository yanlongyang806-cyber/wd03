/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslCurrencyExchange.h"
#include "AppServerLib.h"
#include "ServerLib.h"
#include "UtilitiesLib.h"
#include "objContainer.h"
#include "objIndex.h"
#include "objTransactions.h"
#include "AutoStartupSupport.h"
#include "logging.h"
#include "earray.h"
#include "Queue.h"
#include "StringUtil.h"
#include "AccountProxyCommon.h"
#include "accountnet.h"
#include "InstancedStateMachine.h"
#include "Alerts.h"
#include "aslMTCTransfer.h"
#include "MicroTransactions.h"
#include "LoggedTransactions.h"
#include "TimedCallback.h"
#include "aslCurrencyAutoTrading.h"

#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/appserverlib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
//#include "AutoGen/aslCurrencyExchange_c_ast.h"
#include "AutoGen/aslCurrencyExchange_h_ast.h"
#include "AutoGen/aslCurrencyExchange_c_ast.h"
#include "AutoGen/CurrencyExchangeCommon_h_ast.h"
#include "AutoGen/Controller_autogen_RemoteFuncs.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"

//__CATEGORY Currency Exchange related settings
// Allow currency exchange to be disabled on a running shard.  Note that the currency exchange may also be disabled
//  in the build by a .def file, and also may disable itself if it encounters inconsistent data.
bool gCurrencyExchangeDisabled = false;
AUTO_CMD_INT(gCurrencyExchangeDisabled, DisableCurrencyExchange) ACMD_AUTO_SETTING(CurrencyExchange, CURRENCYEXCHANGESERVER);

// If true, use the new "move" command on the Account Server to facilitate transfers, rather than two separate changes
bool gUseKeyValueMove = true;
AUTO_CMD_INT(gUseKeyValueMove, UseKeyValueMove);

// 
int gPauseAfterFixup = 0;
AUTO_CMD_INT(gPauseAfterFixup, PauseAfterFixup);

// This flag is used by the code to disable the currency exchange if some data inconsistency is found that might cause corruption.
bool gCurrencyExchangeDisabledInternal = false;
AUTO_CMD_INT(gCurrencyExchangeDisabledInternal, DisableCurrencyExchangeInternal);

// Debugging flag that causes logging to be sent to the console.
bool gLogToConsole = false;
AUTO_CMD_INT(gLogToConsole, LogToConsole);

// This variable will get set to false if the account server is unavailable.
static bool sAccountServerAvailable = true;

// The last time the account server was available.
static U32 sLastTimeAccountServerAvailable = 0;

static bool sContainerLoadingDone = false;

static EARRAY_OF(CurrencyOrdersToRemove) sOrderRemovalList = NULL;
static U32 sLastContainerScanTime = 0;

// This index contains all open buy orders.
static ObjectIndex *gBuyOrderIndex = NULL;

// This index contains all open sell orders.
static ObjectIndex *gSellOrderIndex = NULL;

// This index contains all open orders, including orders that are pending withdrawal.
static ObjectIndex *gAllOrderIndex = NULL;

// This earray contains any withdrawal operations that are waiting for transactions to complete.
static EARRAY_OF(CurrencyExchangeOperationRequest) gPendingWithdrawals = NULL;

// New orders go here if order processing is suspended
static Queue sNewOrderQueue = NULL;

// This array holds all of data for CurrencyExchangeAccountData containers that are being created and initialized.
//  Using an earray for this on the assumption that there will never be many pending creates.
static EARRAY_OF(CurrencyExchangeCreatingContainerCBData) sPendingContainerCreates = NULL;

#define STATE_MACHINE_USER_OBJ &gCurrencyExchangeConfig

static CurrencyExchangeGlobalUIData sGlobalUIData = {0};

#define NUM_RECENT_LOG_LINES_TO_SAVE 50
static int sNextLogIndex = 0;
static STRING_EARRAY sRecentLogLines = NULL;

static U64 sTotalMTCToSell = 0;
static U64 sTotalMTCToBuy = 0;
static U64 sTotalTCToSell = 0;
static U64 sTotalTCToBuy = 0;

static void
IncrementTotalCurrencyAvailable(ContainerID accountID, CurrencyExchangeOrderType orderType, U32 quantity, U32 price)
{
    // Ignore any currency orders belonging to the auto trading account.
    if ( accountID != gAutoTradingAccountID )
    {
        if ( orderType == OrderType_Buy )
        {
            sTotalMTCToBuy += quantity;
            sTotalTCToSell += ( quantity * price );
        }
        else if ( orderType == OrderType_Sell )
        {
            sTotalMTCToSell += quantity;
            sTotalTCToBuy += ( quantity * price );
        }
    }
}

static void
DecrementTotalCurrencyAvailable(ContainerID accountID, CurrencyExchangeOrderType orderType, U32 quantity, U32 price)
{
    // Ignore any currency orders belonging to the auto trading account.
    if ( accountID != gAutoTradingAccountID )
    {
        if ( orderType == OrderType_Buy )
        {
            sTotalMTCToBuy -= quantity;
            sTotalTCToSell -= ( quantity * price );
        }
        else if ( orderType == OrderType_Sell )
        {
            sTotalMTCToSell -= quantity;
            sTotalTCToBuy -= ( quantity * price );
        }
    }
}

void
aslCurrencyExchange_Log(char* format, ...)
{
    char* logStr = NULL;
    int recentLogSize = eaSize(&sRecentLogLines);

    VA_START(ap, format);

    estrConcatfv(&logStr, format, ap);

    log_printf(LOG_CURRENCYEXCHANGE, "%s", logStr);

    if ( gLogToConsole )
    {
        printf("%s", logStr);
    }

    if ( recentLogSize < NUM_RECENT_LOG_LINES_TO_SAVE )
    {
        // If the saved log lines array is not at max size yet, then just append to it.
        eaPush(&sRecentLogLines, logStr);    
    }
    else
    {
        devassert(sNextLogIndex < recentLogSize);
        if ( sNextLogIndex < recentLogSize )
        {
            // Free the previous string in that slot.
            estrDestroy( &sRecentLogLines[sNextLogIndex] );

            sRecentLogLines[sNextLogIndex] = logStr;
        }
    }

    // Increment the index for the next log line, wrapping if necessary.
    sNextLogIndex++;
    if ( sNextLogIndex >= NUM_RECENT_LOG_LINES_TO_SAVE )
    {
        sNextLogIndex = 0;
    }

    VA_END();
}

static void 
UpdateGlobalUIData(void)
{
	ObjectIndexIterator iter;
	CurrencyOrderInfo *orderInfo;
	CurrencyExchangePriceQuantity *priceQuantity = NULL;

	sGlobalUIData.enabled = CurrencyExchangeIsEnabled();
	sGlobalUIData.maxMTCPrice = gCurrencyExchangeConfig.maxMTCPrice;
	sGlobalUIData.minMTCPrice = gCurrencyExchangeConfig.minMTCPrice;

    sGlobalUIData.maxQuantityPerOrder = gCurrencyExchangeConfig.maxQuantityPerOrder;
    sGlobalUIData.minQuantityPerOrder = gCurrencyExchangeConfig.minQuantityPerOrder;

	sGlobalUIData.maxPlayerOpenOrders = gCurrencyExchangeConfig.maxPlayerOpenOrders;

	eaClearStruct(&sGlobalUIData.buyPrices, parse_CurrencyExchangePriceQuantity);
	eaClearStruct(&sGlobalUIData.sellPrices, parse_CurrencyExchangePriceQuantity);

	if ( sGlobalUIData.enabled )
	{
		// Don't bother sending price data if the exchange is disabled.
		objIndexObtainReadLock(gSellOrderIndex);
		if ( objIndexGetIterator(gSellOrderIndex, &iter, ITERATE_FORWARD) )
		{
			while ( orderInfo = (CurrencyOrderInfo *)objIndexGetNext(&iter) )
			{
				if ( ( priceQuantity == NULL ) || ( priceQuantity->price != orderInfo->price ) )
				{
					// do we already have enough prices?
					if ( eaSize(&sGlobalUIData.sellPrices) >= gCurrencyExchangeConfig.maxDisplayPrices )
					{
						break;
					}

					// create new price/quantity struct
					priceQuantity = StructCreate(parse_CurrencyExchangePriceQuantity);
					priceQuantity->price = orderInfo->price;
					priceQuantity->quantity = orderInfo->quantity;

					// save price/quantity in the global ui struct
					eaPush(&sGlobalUIData.sellPrices, priceQuantity);
				}
				else
				{
					// more quantity available at this price
					priceQuantity->quantity += orderInfo->quantity;
				}
			}
		}
		objIndexReleaseReadLock(gSellOrderIndex);

		priceQuantity = NULL;

		objIndexObtainReadLock(gBuyOrderIndex);
		if ( objIndexGetIterator(gBuyOrderIndex, &iter, ITERATE_REVERSE) )
		{
			while ( orderInfo = (CurrencyOrderInfo *)objIndexGetNext(&iter) )
			{
				if ( ( priceQuantity == NULL ) || ( priceQuantity->price != orderInfo->price ) )
				{
					// do we already have enough prices?
					if ( eaSize(&sGlobalUIData.buyPrices) >= gCurrencyExchangeConfig.maxDisplayPrices )
					{
						break;
					}

					// create new price/quantity struct
					priceQuantity = StructCreate(parse_CurrencyExchangePriceQuantity);
					priceQuantity->price = orderInfo->price;
					priceQuantity->quantity = orderInfo->quantity;

					// save price/quantity in the global ui struct
					eaPush(&sGlobalUIData.buyPrices, priceQuantity);
				}
				else
				{
					// more quantity available at this price
					priceQuantity->quantity += orderInfo->quantity;
				}
			}
		}
		objIndexReleaseReadLock(gBuyOrderIndex);
	}
}

CurrencyExchangeGlobalUIData *
CurrencyExchange_GetGlobalUIData(void)
{
	return &sGlobalUIData;
}

static bool 
EnqueueNewOperations(void)
{
    // enqueue all operations that come in if we are not in the process orders state
    return !ISM_IsStateActive(CE_STATE_MACHINE, STATE_MACHINE_USER_OBJ, CE_STATE_PROCESS_ORDERS);
}

static void
CurrencyOperationToErrorDetails(CurrencyExchangeOperationRequest *operation)
{
    ErrorDetailsf("operationType=%d, accountID=%d, characterID=%d, characterName=%s, orderID=%d, orderType=%d, quantity=%d, price=%d", 
        operation->operationType, operation->accountID, operation->characterID, operation->characterName, operation->orderID, 
        operation->orderType, operation->quantity, operation->price );
}

static void
CheckAccountServerAvailable_CB(TransactionReturnVal *returnVal, void *userData)
{
    bool enabled;

    if ( RemoteCommandCheck_aslAPCmdMicrotransEnabled(returnVal, &enabled) == TRANSACTION_OUTCOME_SUCCESS )
    {
        if ( enabled )
        {
            sLastTimeAccountServerAvailable = timeSecondsSince2000();
        }
        sAccountServerAvailable = enabled;
    }
    else
    {
        sAccountServerAvailable = false;
    }
}

void
CurrencyExchange_CheckAccountServerAvailable(void)
{
    TransactionReturnVal *returnVal = objCreateManagedReturnVal(CheckAccountServerAvailable_CB, NULL);
    RemoteCommand_aslAPCmdMicrotransEnabled(returnVal,GLOBALTYPE_ACCOUNTPROXYSERVER, 0);
}

bool
CurrencyExchangeIsEnabled(void)
{
    return (sAccountServerAvailable && (!gCurrencyExchangeDisabled) && (!gCurrencyExchangeDisabledInternal) && gCurrencyExchangeConfig.enabled);
}

static void
DisableCurrencyExchangeInternal(void)
{
	gCurrencyExchangeDisabledInternal = true;
}

bool
isCurrencyOrderComplete(CurrencyExchangeOpenOrder *order)
{
    // The order is complete if all MTC has been consumed
    return(order->quantity == order->consumedMTC);
}

//
// Find if there is a currently pending container create for the accountID.
//
CurrencyExchangeCreatingContainerCBData *
GetPendingCurrencyCreateData(ContainerID accountID)
{
    int i;

    // the array should be short, so just do a linear scan for now
    for( i = eaSize(&sPendingContainerCreates) - 1; i >= 0; i-- )
    {
        if ( sPendingContainerCreates[i]->accountID == accountID )
        {
            return sPendingContainerCreates[i];
        }
    }

    return NULL;
}

bool
CurrencyExchangeAccountDataExists(ContainerID accountID, bool checkPending)
{
    if ( objGetContainer(GLOBALTYPE_CURRENCYEXCHANGE, accountID) != NULL )
    {
        if ( !checkPending || ( GetPendingCurrencyCreateData(accountID) == NULL ) )
        {
            return true;
        }
    }

    return false;
}

// Successful transactions that create orders will return the orderID in the success result string of the transaction.
// This function parses that string and extracts the orderID.
static U32
OrderIDFromTransactionReturn(TransactionReturnVal *returnVal)
{
    U32 orderID = 0;

    if ( !StringToUint(objAutoTransactionGetResult(returnVal), &orderID) )
    {
        orderID = 0;
    }

    return orderID;
}

static CurrencyExchangeResultType
ErrorResultFromTransactionReturn(TransactionReturnVal *returnVal, char **logStrHandle)
{
    char *transStr = objAutoTransactionGetResult(returnVal);
    char *transLogStr;

    CurrencyExchangeResultType resultType;

    if ( transStr == NULL || transStr[0] == '\0' )
    {
        return CurrencyResultType_InternalError;
    }

    if (! StringToInt(transStr, &(int)resultType) )
    {
        return CurrencyResultType_InternalError;
    }

    transLogStr = strchr(transStr, ':');
    if ( transLogStr != NULL )
    {
        estrCopy2(logStrHandle, transLogStr);
    }

    return resultType;
}

void
CurrencyExchangeNotifyUserOperationResult(CurrencyExchangeResultType result, CurrencyExchangeOperationRequest *operation)
{
	if ( operation->characterID != 0 )
	{
		RemoteCommand_gslCurrencyExchange_NotifyPlayer(GLOBALTYPE_ENTITYPLAYER, operation->characterID, operation->characterID, result, operation->operationType);
	}
}

void
CurrencyExchangeOrderInfoLogAndErrorEx(const char *tag, CurrencyOrderInfo *orderInfo, CurrencyExchangeResultType errorType, const char *logMessage, bool doError, bool doLog, const char *func, int line)
{
    static char *tmpOpString = NULL;

    estrPrintf(&tmpOpString, "resultType=%s, accountID=%d, orderID=%d, orderType=%s, quantity=%d, price=%d, listingTime=%d", 
        StaticDefineIntRevLookupNonNull(CurrencyExchangeResultTypeEnum, errorType), orderInfo->accountID, orderInfo->orderID, 
		StaticDefineIntRevLookupNonNull(CurrencyExchangeOrderTypeEnum, orderInfo->orderType), orderInfo->quantity, orderInfo->price, orderInfo->listingTime );

    if ( doError )
    {
        // generate an errortracker record
        ErrorDetailsf("%s, %s, %d", tmpOpString, func, line);
        Errorf("CurrencyExchange(orderInfo): %s: %s", tag, logMessage);
    }

    if ( doLog )
    {
        // log the error
        aslCurrencyExchange_Log("%s:orderInfo:%s:%s:%s:%d", tag, logMessage, tmpOpString, func, line);
    }
}

void
CurrencyExchangeOperationNotifyEx(CurrencyExchangeOperationRequest *operation, CurrencyExchangeResultType errorType, const char *logMessage, bool doError, bool doLog, bool doNotify, const char *func, int line)
{
    static char *tmpOpString = NULL;

    estrPrintf(&tmpOpString, "resultType=%s, operationType=%s, accountID=%d, characterID=%d, characterName=%s, orderID=%d, orderType=%s, quantity=%d, price=%d", 
        StaticDefineIntRevLookupNonNull(CurrencyExchangeResultTypeEnum, errorType), StaticDefineIntRevLookupNonNull(CurrencyExchangeOperationTypeEnum, operation->operationType), 
		operation->accountID, operation->characterID, operation->characterName, operation->orderID, 
        StaticDefineIntRevLookupNonNull(CurrencyExchangeOrderTypeEnum, operation->orderType), operation->quantity, operation->price );

    if ( doError )
    {
        // generate an errortracker record
        ErrorDetailsf("%s, %s, %d", tmpOpString, func, line);
        Errorf("CurrencyExchange(operation): %s", logMessage);
    }

    if ( doLog )
    {
        // log the error
        aslCurrencyExchange_Log("operation:%s:%s:%s:%d", logMessage, tmpOpString, func, line);
    }

    if ( doNotify )
    {
        // notify the user
        CurrencyExchangeNotifyUserOperationResult(errorType, operation);
    }
}

//
// BadRuntimeData is called if any of the runtime data structures are in an invalid state.
// It will generate an alert and disable the exchange.
//
#define BadRuntimeData(msg, details) BadRuntimeDataEx(msg, details, __FUNCTION__, __LINE__)
static void
BadRuntimeDataEx(const char *errorStr, const char *detailsStr, const char *func, int line)
{
	aslCurrencyExchange_Log("BadRuntimeData: function=%s, line=%d, message=%s, details=%s", func, line, errorStr, NULL_TO_EMPTY(detailsStr));
	TriggerAlert("CURRENCYEXCHANGE_BADRUNTIMEDATA", 
		STACK_SPRINTF("The Currency Exchange has encountered bad runtime data and has disabled itself.  function=%s, line=%d, message=%s, details=%s", func, line, errorStr, NULL_TO_EMPTY(detailsStr)), 
		ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0);
	DisableCurrencyExchangeInternal();
}

static CurrencyOrderInfo *
FindOrderInfo(ContainerID accountID, U32 orderID)
{
    S64 orderKey = ((S64)orderID << 32) | accountID;
	ObjectIndexKey indexKey = {0};
    CurrencyOrderInfo *orderInfo;

	objIndexInitKey_Int64(gAllOrderIndex, &indexKey, orderKey);

    if(!objIndexGet(gAllOrderIndex, &indexKey, 0, (ObjectHeaderOrData **)&orderInfo))
    {
        orderInfo = NULL;
    }

    objIndexDeinitKey_Int64(gAllOrderIndex, &indexKey);

    return orderInfo;
}

void
PrintOrderInfo(char **outStrHandle, CurrencyOrderInfo *orderInfo, char *indent)
{
    char *orderTypeStr;

    switch(orderInfo->orderType)
    {
    case OrderType_Buy:
        orderTypeStr = "Buy";
        break;
    case OrderType_Sell:
        orderTypeStr = "Sell";
        break;
    default:
        orderTypeStr = "None";
        break;
    }
    estrConcatf(outStrHandle, "%s %d:%d %s %d MTC for %d TC\n", indent, orderInfo->accountID, orderInfo->orderID, orderTypeStr, orderInfo->quantity, orderInfo->price);
}

void
CurrencyExchange_DumpOrderInfos(int count, char **outStrHandle)
{
    int i;
    ObjectIndexIterator iter;
    CurrencyOrderInfo *orderInfo;

	objIndexObtainReadLock(gSellOrderIndex);
	if ( objIndexGetIterator(gSellOrderIndex, &iter, ITERATE_FORWARD) )
	{
		estrAppend2(outStrHandle, "Top open sell orders:\n");

		i = 0;
		while ( ( i < count ) && ( orderInfo = (CurrencyOrderInfo *)objIndexGetNext(&iter) ) )
		{
			PrintOrderInfo(outStrHandle, orderInfo, "  ");

			i++;
		}
	}
	else
	{
		estrAppend2(outStrHandle, "No sell orders.\n");
	}
	objIndexReleaseReadLock(gSellOrderIndex);

    objIndexObtainReadLock(gBuyOrderIndex);
    if ( objIndexGetIterator(gBuyOrderIndex, &iter, ITERATE_REVERSE) )
	{
		estrAppend2(outStrHandle, "Top open buy orders:\n");
		i = 0;
		while ( ( i < count ) && ( orderInfo = (CurrencyOrderInfo *)objIndexGetNext(&iter) ) )
		{
			PrintOrderInfo(outStrHandle, orderInfo, "  ");

			i++;
		}
	}
	else
	{
		estrAppend2(outStrHandle, "No buy orders.\n");
	}
    objIndexReleaseReadLock(gBuyOrderIndex);
}

//
// Create the CurrencyOrderInfo struct for an order and add it to the indices
//
static void
CreateOrderInfo(U32 accountID, U32 orderID, CurrencyExchangeOrderType orderType, U32 quantity, U32 price, U32 listingTime)
{

    // create an order info struct for the order and add it to the appropriate index
    CurrencyOrderInfo *orderInfo = StructCreate(parse_CurrencyOrderInfo);
    orderInfo->orderKey = ((S64)orderID << 32) | accountID;
    orderInfo->accountID = accountID;
    orderInfo->orderID = orderID;
    orderInfo->orderType = orderType;
    orderInfo->quantity = quantity;
    orderInfo->price = price;
    orderInfo->listingTime = listingTime;
    // reverse time is used for sorting buy orders, since we want to sort price highest to lowest, but listing time lowest to highest
    orderInfo->listingTimeReverse = U32_MAX - listingTime;

    if ( orderInfo->orderType == OrderType_Buy )
    {
        if (!objIndexInsert(gBuyOrderIndex, orderInfo))
        {
            CurrencyExchangeOrderInfoLogAndError("CreateOrderInfo", orderInfo, CurrencyResultType_InternalError, "Adding orderInfo to buy order index failed");
            BadRuntimeData("Adding orderInfo to buy order index failed", NULL);
        }

        if (!objIndexInsert(gAllOrderIndex, orderInfo))
        {
            CurrencyExchangeOrderInfoLogAndError("CreateOrderInfo", orderInfo, CurrencyResultType_InternalError, "Adding orderInfo to all order index failed");
            BadRuntimeData("Adding orderInfo to all order index failed", NULL);
        }
        IncrementTotalCurrencyAvailable(accountID, OrderType_Buy, quantity, price);
    }
    else if ( orderInfo->orderType == OrderType_Sell )
    {
        if (!objIndexInsert(gSellOrderIndex, orderInfo))
        {
            CurrencyExchangeOrderInfoLogAndError("CreateOrderInfo", orderInfo, CurrencyResultType_InternalError, "Adding orderInfo to sell order index failed");
            BadRuntimeData("Adding orderInfo to sell order index failed", NULL);
        }

        if (!objIndexInsert(gAllOrderIndex, orderInfo))
        {
            CurrencyExchangeOrderInfoLogAndError("CreateOrderInfo", orderInfo, CurrencyResultType_InternalError, "Adding orderInfo to all order index failed");
            BadRuntimeData("Adding orderInfo to all order index failed", NULL);
        }
        IncrementTotalCurrencyAvailable(accountID, OrderType_Sell, quantity, price);
    }
    else if ( orderInfo->orderType != OrderType_None )
    {
        ErrorDetailsf("accountID=%d, orderID=%d, orderType=%d, quantity=%d, price=%d, listingTime=%d", 
            orderInfo->accountID, orderInfo->orderID, orderInfo->orderType, orderInfo->quantity, orderInfo->price, orderInfo->listingTime);
        Errorf("%s: invalid order type found when building indices", __FUNCTION__);
    }

}

static void
ProcessPendingContainers(void)
{
    int i, j;
    U32 curTime = timeSecondsSince2000();

    for( i = eaSize(&sPendingContainerCreates)-1; i >= 0; i-- )
    {
        CurrencyExchangeCreatingContainerCBData *pendingCreateData = sPendingContainerCreates[i];

        // check if the container is now present
        if ( pendingCreateData->state == CreateContainer_WaitingForContainer && CurrencyExchangeAccountDataExists(pendingCreateData->accountID, false) )
        {
            // remove from the pending list
            eaRemove(&sPendingContainerCreates, i);

            // restart the operations with the containerMustExist flag set to true
            for(j = 0; j < eaSize(&pendingCreateData->operations); j++)
            {
                CurrencyExchangeReceiveOperation(pendingCreateData->operations[j], true, false);
            }

            // clear the operations array so that the operations don't get freed here
            eaClearFast(&pendingCreateData->operations);

            StructDestroy(parse_CurrencyExchangeCreatingContainerCBData, pendingCreateData);
        }
        else if ( pendingCreateData->state == CreateContainer_Failed || pendingCreateData->state == CreateContainer_InitFailed ||
             ( pendingCreateData->createTime + CURRENCY_EXCHANGE_CONTAINER_CREATE_TIMEOUT ) < curTime )
        {
            // remove from the pending list
            eaRemove(&sPendingContainerCreates, i);

            // generate errors for the operations
            for(j = 0; j < eaSize(&pendingCreateData->operations); j++)
            {
                const char *logString = NULL;

                CurrencyExchangeOperationRequest *operation = pendingCreateData->operations[j];
                if ( pendingCreateData->state == CreateContainer_Failed )
                {
                    logString = "Container Create Failed";
                }
                else if ( pendingCreateData->state == CreateContainer_InitFailed )
                {
                    logString = "Container Init Failed";
                }
                else
                {
                    logString = "Container Create Timeout";
                }

                CurrencyExchangeOperationFail(operation, CurrencyResultType_InternalError, logString);
                StructDestroy(parse_CurrencyExchangeOperationRequest, operation);

                pendingCreateData->operations[j] = NULL;
            }

            // clear the operations array so that the operations don't get freed here
            eaClearFast(&pendingCreateData->operations);

            StructDestroy(parse_CurrencyExchangeCreatingContainerCBData, pendingCreateData);
        }
    }
}

static void
InitAccountData_CB(TransactionReturnVal *pReturn, CurrencyExchangeCreatingContainerCBData *pendingCreateData)
{
    if ( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
    {
        devassert(pendingCreateData->state == CreateContainer_InitStarted);

        pendingCreateData->state = CreateContainer_WaitingForContainer;
    }
    else
    {
        pendingCreateData->state = CreateContainer_InitFailed;
    }
}

static void
CreateAccountData_CB(TransactionReturnVal *pReturn, CurrencyExchangeCreatingContainerCBData *pendingCreateData)
{
    if ( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
    {
        TransactionReturnVal *pNewReturn;

        devassert(pendingCreateData->state == CreateContainer_CreateStarted);

        pendingCreateData->state = CreateContainer_InitStarted;

        pNewReturn = objCreateManagedReturnVal(InitAccountData_CB, pendingCreateData);
        AutoTrans_CurrencyExchange_tr_InitAccountData(pNewReturn, GLOBALTYPE_CURRENCYEXCHANGESERVER, GLOBALTYPE_CURRENCYEXCHANGE, pendingCreateData->accountID, pendingCreateData->accountID);
    }
    else
    {
        pendingCreateData->state = CreateContainer_Failed;
    }
}

//
// Create a new CurrencyExchangeAccountData container for the account that initiated the operation.
// This should only be called if the container doesn't already exist
//
static void
CreateAccountData(CurrencyExchangeOperationRequest *operation)
{

    if ( operation != NULL )
    {
        CurrencyExchangeCreatingContainerCBData *pendingCreateData;

        devassert( !CurrencyExchangeAccountDataExists(operation->accountID, false) );
        
        pendingCreateData = GetPendingCurrencyCreateData(operation->accountID);

        if ( pendingCreateData != NULL )
        {
            // There is already a pending create for this account, so just add the current operation to it.
            eaPush(&pendingCreateData->operations, operation);
        }
        else
        {
            TransactionRequest *request = objCreateTransactionRequest();

            // create the struct that tracks pending container creates
            pendingCreateData = StructCreate(parse_CurrencyExchangeCreatingContainerCBData);

            pendingCreateData->accountID = operation->accountID;
            pendingCreateData->createTime = timeSecondsSince2000();
            pendingCreateData->state = CreateContainer_CreateStarted;
            eaPush(&pendingCreateData->operations, operation);

            // add it to the global list of pending creates
            eaPush(&sPendingContainerCreates, pendingCreateData);

            // This manually built transaction is required to force the containerID to be the accountID
            objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
                "VerifyContainer containerIDVar %s %d",
                GlobalTypeToName(GLOBALTYPE_CURRENCYEXCHANGE),
                operation->accountID);

            // Move the container to the currency exchange server
            objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, "containerIDVar", 
                "MoveContainerTo containerVar %s TRVAR_containerIDVar %s %d",
                GlobalTypeToName(GLOBALTYPE_CURRENCYEXCHANGE), GlobalTypeToName(GLOBALTYPE_CURRENCYEXCHANGESERVER), 0);

            objAddToTransactionRequestf(request, GLOBALTYPE_CURRENCYEXCHANGESERVER, 0, "containerVar containerIDVar ContainerVarBinary", 
                "ReceiveContainerFrom %s TRVAR_containerIDVar ObjectDB 0 TRVAR_containerVar",
                GlobalTypeToName(GLOBALTYPE_CURRENCYEXCHANGE));

            objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,
                objCreateManagedReturnVal(CreateAccountData_CB, pendingCreateData), "CreateCurrencyExchangeAccountData", request);
            objDestroyTransactionRequest(request);
        }
    }
}

static void
HandleCreateOrder_CB(TransactionReturnVal *returnVal, CurrencyExchangeOperationRequest *operation)
{
    if ( returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
    {
        U32 orderID;

        if ( operation->orderType == OrderType_Sell )
        {
            // Only sell orders use MTC transfer.
            MTCTransferCommitSuccessful(operation->mtcTransferID);
        }

        orderID = OrderIDFromTransactionReturn(returnVal);

        // Put orderID into operation so that it shows up in logs.
        operation->orderID = orderID;

        if ( orderID > 0 )
        {
            // Create an OrderInfo struct for this order and add it to the buy or sell index
            CreateOrderInfo(operation->accountID, orderID, operation->orderType, operation->quantity, operation->price, operation->arrivalTime);
            CurrencyExchangeOperationNotify(operation, CurrencyResultType_Success, "Create order succeeded", false, true, true);
        }
        else
        {
            CurrencyExchangeOperationNotify(operation, CurrencyResultType_Info, "Create order succeeded, but orderID not found", true, true, false);
        }
    }
    else
    {
        char *logStr = NULL;

        CurrencyExchangeResultType resultType = ErrorResultFromTransactionReturn(returnVal, &logStr);

        if ( operation->orderType == OrderType_Sell )
        {
            // Only sell orders use MTC transfer.
            MTCTransferCommitFailed(operation->mtcTransferID);
        }

        CurrencyExchangeOperationFail(operation, resultType, logStr);

        estrDestroy(&logStr);
    }

    StructDestroy(parse_CurrencyExchangeOperationRequest, operation);
}

static void
MTCEscrowCB(CurrencyExchangeResultType result, ContainerID srcLockID, ContainerID destLockID, char *failureDetailString, CurrencyExchangeOperationRequest *operation)
{
    if ( result == CurrencyResultType_Success )
    {
        TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValEntInfo("CurrencyExchange", GLOBALTYPE_ENTITYPLAYER, operation->characterID, 
            operation->debugName, operation->gameSpecificLogInfo, HandleCreateOrder_CB, operation);
        AutoTrans_CurrencyExchange_tr_CreateSellOrder(returnVal, GLOBALTYPE_CURRENCYEXCHANGESERVER, GLOBALTYPE_CURRENCYEXCHANGE, operation->accountID, 
            GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, srcLockID, GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, destLockID, operation);
    }
    else
    {
        CurrencyExchangeOperationFail(operation, result, failureDetailString);
        StructDestroy(parse_CurrencyExchangeOperationRequest, operation);
    }
}

static void
HandleCreateOrder(CurrencyExchangeOperationRequest *operation)
{
    TransactionReturnVal *returnVal;

    if ( operation->orderType == OrderType_Buy )
    {
        returnVal = LoggedTransactions_CreateManagedReturnValEntInfo("CurrencyExchange", GLOBALTYPE_ENTITYPLAYER, operation->characterID, 
            operation->debugName, operation->gameSpecificLogInfo, HandleCreateOrder_CB, operation);
        AutoTrans_CurrencyExchange_tr_CreateBuyOrder(returnVal, GLOBALTYPE_GAMESERVER, GLOBALTYPE_CURRENCYEXCHANGE, operation->accountID, GLOBALTYPE_ENTITYPLAYER, operation->characterID, operation, gCurrencyExchangeConfig.tcNumeric);
    } 
    else if ( operation->orderType == OrderType_Sell )
    {
        // create account server locks for the various MTC buckets before starting the microtransaction to create the sell order
		operation->mtcTransferID = MTCTransferRequest(operation->accountID, operation->accountID, microtrans_GetShardCurrency(), microtrans_GetShardForSaleBucketKey(), operation->quantity, operation, MTCEscrowCB);
    }
    else
    {
        CurrencyExchangeOperationFail(operation, CurrencyResultType_InternalError, "Attempted to create an order that was neither buy nor sell");
        StructDestroy(parse_CurrencyExchangeOperationRequest, operation);
    }

}

static void
WithdrawOrder_CB(TransactionReturnVal *returnVal, CurrencyExchangeOperationRequest *operation)
{
	if ( returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
	{
        if ( operation->orderInfo->orderType == OrderType_Sell )
        {
            MTCTransferCommitSuccessful(operation->mtcTransferID);
        }
		CurrencyExchangeOperationNotify(operation, CurrencyResultType_Success, "Withdraw order succeeded", false, true, true);
	}
	else
	{
		char *logStr = NULL;
		CurrencyExchangeResultType resultType = ErrorResultFromTransactionReturn(returnVal, &logStr);

        if ( operation->orderInfo->orderType == OrderType_Sell )
        {
            MTCTransferCommitFailed(operation->mtcTransferID);
        }
		CurrencyExchangeOperationFail(operation, resultType, logStr);

		estrDestroy(&logStr);
	}

	StructDestroy(parse_CurrencyOrderInfo, operation->orderInfo);
	StructDestroy(parse_CurrencyExchangeOperationRequest, operation);
}

static void
WithdrawSellOrderLocks_CB(CurrencyExchangeResultType result, ContainerID srcLockID, ContainerID destLockID, char *failureDetailString, CurrencyExchangeOperationRequest *operation)
{
	if ( result == CurrencyResultType_Success )
	{
		TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValEntInfo("CurrencyExchange", GLOBALTYPE_ENTITYPLAYER, operation->characterID, 
            operation->debugName, operation->gameSpecificLogInfo, WithdrawOrder_CB, operation);
		AutoTrans_CurrencyExchange_tr_WithdrawSellOrder(returnVal, GLOBALTYPE_CURRENCYEXCHANGESERVER, GLOBALTYPE_CURRENCYEXCHANGE, operation->accountID, 
			GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, srcLockID, GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, destLockID, operation->orderID, operation->orderInfo->quantity, 
			operation->arrivalTime, operation->characterName);
	}
	else
	{
		CurrencyExchangeOperationFail(operation, result, failureDetailString);
		StructDestroy(parse_CurrencyOrderInfo, operation->orderInfo);
		StructDestroy(parse_CurrencyExchangeOperationRequest, operation);
	}
}

static void
DoWithdrawal(CurrencyExchangeOperationRequest *operation)
{
    CurrencyOrderInfo *orderInfo = operation->orderInfo;

    devassert(orderInfo != NULL);
    if ( orderInfo == NULL )
    {
        CurrencyExchangeOperationLogAndError(operation, CurrencyResultType_InternalError, "Missing orderInfo in DoWithdrawal");
        BadRuntimeData("missing orderInfo", NULL);
        return;
    }

    devassert(orderInfo->pendingTransCount == 0);
    if ( orderInfo->pendingTransCount != 0 )
    {
        CurrencyExchangeOperationLogAndError(operation, CurrencyResultType_InternalError, "withdrawal while transactions pending");
        CurrencyExchangeOrderInfoLogAndError("WithdrawOrder", orderInfo, CurrencyResultType_InternalError, "withdrawal while transactions pending");
        BadRuntimeData("withdrawal while transactions pending", NULL);
        return;
    }

	if ( orderInfo->orderType == OrderType_Buy )
	{
		TransactionReturnVal *pReturn = LoggedTransactions_CreateManagedReturnValEntInfo("CurrencyExchange", GLOBALTYPE_ENTITYPLAYER, operation->characterID,
            operation->debugName, operation->gameSpecificLogInfo, WithdrawOrder_CB, operation);
		AutoTrans_CurrencyExchange_tr_WithdrawBuyOrder(pReturn, GLOBALTYPE_CURRENCYEXCHANGESERVER, GLOBALTYPE_CURRENCYEXCHANGE, operation->accountID, operation->orderID, operation->arrivalTime, operation->characterName);
	}
	else if ( orderInfo->orderType == OrderType_Sell )
	{
		operation->mtcTransferID = MTCTransferRequest(operation->accountID, operation->accountID, microtrans_GetShardForSaleBucketKey(), microtrans_GetShardReadyToClaimBucketKey(), orderInfo->quantity, operation, WithdrawSellOrderLocks_CB);
	}
	else
	{
        CurrencyExchangeOperationLogAndError(operation, CurrencyResultType_InternalError, "bad order type for withdrawal");
        CurrencyExchangeOrderInfoLogAndError("WithdrawOrder", orderInfo, CurrencyResultType_InternalError, "bad order type for withdrawal");
		BadRuntimeData("bad order type for withdrawal", NULL);
		return;
	}
}

static void
HandleWithdrawOrder(CurrencyExchangeOperationRequest *operation)
{
    CurrencyOrderInfo *orderInfo;

    orderInfo = FindOrderInfo(operation->accountID, operation->orderID);
    if ( orderInfo == NULL )
    {
        CurrencyExchangeOperationFail(operation, CurrencyResultType_InternalError, "Failed to find orderInfo for withdraw operation");
        StructDestroy(parse_CurrencyExchangeOperationRequest, operation);
        return;
    }

    if ( orderInfo->withdrawalPending )
    {
        CurrencyExchangeOperationFail(operation, CurrencyResultType_DuplicateWithdrawal, "Duplicate request to withdraw an order");
        StructDestroy(parse_CurrencyExchangeOperationRequest, operation);
        return;
    }

	if ( orderInfo->quantity == 0 )
	{
		CurrencyExchangeOperationFail(operation, CurrencyResultType_InternalError, "Attempt to withdraw an order that has already been fulfilled");
		StructDestroy(parse_CurrencyExchangeOperationRequest, operation);
		return;
	}

    // Remove orderInfo from buy or sell index so that it will not be used to fulfill any more orders.
    if ( orderInfo->orderType == OrderType_Buy )
    {
        if (!objIndexRemove(gBuyOrderIndex, orderInfo))
        {
			CurrencyExchangeOperationFail(operation, CurrencyResultType_InternalError, "Removing orderInfo from buy order index failed");
			CurrencyExchangeOrderInfoLogAndError("WithdrawOrder", orderInfo, CurrencyResultType_InternalError, "Removing orderInfo from buy order index failed");
            BadRuntimeData("Removing orderInfo from buy order index failed", NULL);
            StructDestroy(parse_CurrencyExchangeOperationRequest, operation);
            return;
        }

        DecrementTotalCurrencyAvailable(orderInfo->accountID, OrderType_Buy, orderInfo->quantity, orderInfo->price);
    }
    else if ( orderInfo->orderType == OrderType_Sell )
    {
        if (!objIndexRemove(gSellOrderIndex, orderInfo))
        {
			CurrencyExchangeOperationFail(operation, CurrencyResultType_InternalError, "Removing orderInfo from sell order index failed");
			CurrencyExchangeOrderInfoLogAndError("WithdrawOrder", orderInfo, CurrencyResultType_InternalError, "Removing orderInfo from sell order index failed");
            BadRuntimeData("Removing orderInfo from sell order index failed", NULL);
            StructDestroy(parse_CurrencyExchangeOperationRequest, operation);
            return;
        }
        DecrementTotalCurrencyAvailable(orderInfo->accountID, OrderType_Sell, orderInfo->quantity, orderInfo->price);
    }
    else
    {
		CurrencyExchangeOperationFail(operation, CurrencyResultType_InternalError, "orderInfo had invalid type");
		CurrencyExchangeOrderInfoLogAndError("WithdrawOrder", orderInfo, CurrencyResultType_InternalError, "orderInfo had invalid type");
        BadRuntimeData("orderInfo had invalid type", NULL);
        StructDestroy(parse_CurrencyExchangeOperationRequest, operation);
        return;
    }

    // Remove order from the All Orders index.
    if (!objIndexRemove(gAllOrderIndex, orderInfo))
    {
		CurrencyExchangeOperationFail(operation, CurrencyResultType_InternalError, "Removing orderInfo from all order index failed");
		CurrencyExchangeOrderInfoLogAndError("WithdrawOrder", orderInfo, CurrencyResultType_InternalError, "Removing orderInfo from all order index failed");
        BadRuntimeData("Removing orderInfo from all order index failed", NULL);
        StructDestroy(parse_CurrencyExchangeOperationRequest, operation);
        return;
    }

    operation->orderInfo = orderInfo;
	orderInfo->withdrawalPending = true;

    if ( orderInfo->pendingTransCount )
    {
        // There are still pending transactions on this order, so save it on the pending withdrawal list
        eaPush(&gPendingWithdrawals, operation);
    }
    else
    {
        DoWithdrawal(operation);
    }
}

static void
HandleClaimMTC_CB(TransactionReturnVal *returnVal, CurrencyExchangeOperationRequest *operation)
{
	if ( returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
	{
        MTCTransferCommitSuccessful(operation->mtcTransferID);

		CurrencyExchangeOperationNotify(operation, CurrencyResultType_Success, "Claim MTC succeeded", false, true, true);
	}
	else
	{
		char *logStr = NULL;
		CurrencyExchangeResultType resultType = ErrorResultFromTransactionReturn(returnVal, &logStr);

        MTCTransferCommitFailed(operation->mtcTransferID);

		CurrencyExchangeOperationFail(operation, resultType, logStr);

		estrDestroy(&logStr);
	}

	StructDestroy(parse_CurrencyExchangeOperationRequest, operation);
}

static void
HandleClaimMTCLocks_CB(CurrencyExchangeResultType result, ContainerID srcLockID, ContainerID destLockID, char *failureDetailString, CurrencyExchangeOperationRequest *operation)
{
	if ( result == CurrencyResultType_Success )
	{
		TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValEntInfo("CurrencyExchange", GLOBALTYPE_ENTITYPLAYER, operation->characterID, 
            operation->debugName, operation->gameSpecificLogInfo, HandleClaimMTC_CB, operation);
		AutoTrans_CurrencyExchange_tr_ClaimMTC(returnVal, GLOBALTYPE_CURRENCYEXCHANGESERVER, GLOBALTYPE_CURRENCYEXCHANGE, operation->accountID, 
			GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, srcLockID, GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, destLockID, operation->quantity, 
			operation->arrivalTime, operation->characterName);
	}
	else
	{
		CurrencyExchangeOperationFail(operation, result, failureDetailString);
		StructDestroy(parse_CurrencyExchangeOperationRequest, operation);
	}
}

static void
HandleClaimMTC(CurrencyExchangeOperationRequest *operation)
{
	const char *pTargetCurrency = gUseKeyValueMove ? microtrans_GetShardCurrency() : microtrans_GetExchangeWithdrawCurrency();
	operation->mtcTransferID = MTCTransferRequest(operation->accountID, operation->accountID, microtrans_GetShardReadyToClaimBucketKey(), pTargetCurrency, 
		operation->quantity, operation, HandleClaimMTCLocks_CB);
}

static void
HandleClaimTC_CB(TransactionReturnVal *returnVal, CurrencyExchangeOperationRequest *operation)
{
	if ( returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
	{
		CurrencyExchangeOperationNotify(operation, CurrencyResultType_Success, "Claim TC succeeded", false, true, true);
	}
	else
	{
		char *logStr = NULL;
		CurrencyExchangeResultType resultType = ErrorResultFromTransactionReturn(returnVal, &logStr);

		CurrencyExchangeOperationFail(operation, resultType, logStr);

		estrDestroy(&logStr);
	}

	StructDestroy(parse_CurrencyExchangeOperationRequest, operation);
}

static void
HandleClaimTC(CurrencyExchangeOperationRequest *operation)
{
	TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValEntInfo("CurrencyExchange", GLOBALTYPE_ENTITYPLAYER, operation->characterID,
        operation->debugName, operation->gameSpecificLogInfo, HandleClaimTC_CB, operation);
	AutoTrans_CurrencyExchange_tr_ClaimTC(returnVal, GLOBALTYPE_GAMESERVER, GLOBALTYPE_CURRENCYEXCHANGE, operation->accountID, GLOBALTYPE_ENTITYPLAYER, 
		operation->characterID, operation->quantity, operation->arrivalTime, operation->characterName, gCurrencyExchangeConfig.tcNumeric);
}

static void
ProcessPendingWithdrawals(void)
{
	int i;

	for ( i = eaSize(&gPendingWithdrawals) - 1; i >= 0; i-- )
	{
		CurrencyExchangeOperationRequest *operation = gPendingWithdrawals[i];

		// If a pending withdrawal has no more pending transactions, then actually withdraw it.
		if ( operation->orderInfo->pendingTransCount == 0 )
		{
			eaRemove(&gPendingWithdrawals, i);
			DoWithdrawal(operation);
		}
	}
}

void
CurrencyExchangeExecuteOperation(CurrencyExchangeOperationRequest *operation)
{
	CurrencyExchangeOperationLog(operation, CurrencyResultType_InProgress, "Executing operation");

    switch ( operation->operationType )
    {
    case CurrencyOpType_CreateOrder:
        HandleCreateOrder(operation);
        break;
    case CurrencyOpType_WithdrawOrder:
        HandleWithdrawOrder(operation);
        break;
    case CurrencyOpType_ClaimMTC:
        HandleClaimMTC(operation);
        break;
    case CurrencyOpType_ClaimTC:
        HandleClaimTC(operation);
        break;
    default:
        break;
    }
}

//
// Handle an incoming operation.  Operations may come from remote commands, the processing of the queue, or the retry of an operation
//  that was waiting for the CurrencyExchangeAccountData container to be created.
//
void
CurrencyExchangeReceiveOperation(CurrencyExchangeOperationRequest *operation, bool containerMustExist, bool processingQueue)
{
    //
    // Fail if the exchange is disabled
    //
    if ( !CurrencyExchangeIsEnabled() )
    {
        CurrencyExchangeNotifyUserOperationResult(CurrencyResultType_ExchangeDisabled, operation);

        StructDestroy(parse_CurrencyExchangeOperationRequest, operation);
        return;
    }

    //
    // Queue the operation if processing is temporarily suspended
    //
    if ( EnqueueNewOperations() && !processingQueue )
    {
        qEnqueue(sNewOrderQueue, operation);
        return;
    }

    //
    // If there is no account data container for this account, then create one before processing the operation
    // Passing "true" here also checks that there is not a create pending.  This is to avoid a race condition
    //   where the container has been created but the init transaction hasn't happened yet. 
    //
    if (!CurrencyExchangeAccountDataExists(operation->accountID, true))
    {
        if ( containerMustExist )
        {
            // This case is to avoid re-trying to create the container in case something unexpected goes wrong
            //  and it is not present when we think it should be.

            CurrencyExchangeOperationFail(operation, CurrencyResultType_InternalError, "currency exchange container not found when it must exist");

            StructDestroy(parse_CurrencyExchangeOperationRequest, operation);
            return;
        }
        else
        {
            CreateAccountData(operation);
        }
        return;
    }

    //
    // Process the operation now
    //
    CurrencyExchangeExecuteOperation(operation);
}

AUTO_STRUCT;
typedef struct MatchOrdersCBData
{
	CurrencyOrderInfo *sellOrder;	AST(UNOWNED)
	CurrencyOrderInfo *buyOrder;	AST(UNOWNED)
	U32 tradePrice;
	U32 tradeQuantity;
    U32 mtcTransferID;
} MatchOrdersCBData;

static void
HandleMatchOrder_CB(TransactionReturnVal *returnVal, MatchOrdersCBData *cbData)
{
	if ( returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
	{
        MTCTransferCommitSuccessful(cbData->mtcTransferID);

		// Log for success.
        aslCurrencyExchange_Log("MatchComplete: sellerAccountID=%u, sellerOrderID=%u, buyerAccountID=%u, buyerOrderID=%u, price=%u, quantity=%u", 
            cbData->sellOrder->accountID, cbData->sellOrder->orderID, cbData->buyOrder->accountID, cbData->buyOrder->orderID, cbData->tradePrice, cbData->tradeQuantity);
	}
	else
	{
		char *logStr = NULL;
		CurrencyExchangeResultType resultType = ErrorResultFromTransactionReturn(returnVal, &logStr);

        MTCTransferCommitFailed(cbData->mtcTransferID);

		// Log and error on failure.
		CurrencyExchangeOrderInfoLogAndError("MatchFailureSell", cbData->sellOrder, resultType, logStr);
		CurrencyExchangeOrderInfoLogAndError("MatchFailureBuy", cbData->buyOrder, resultType, logStr);

		// Alert and shutdown the exchange.
		BadRuntimeData("MatchOrder transaction failed", logStr);

		estrDestroy(&logStr);
	}

	// Free the orders if they have been fully consumed and have no more pending transactions.
    cbData->sellOrder->pendingTransCount--;
	if ( ( cbData->sellOrder->pendingTransCount == 0 ) && ( cbData->sellOrder->quantity == 0 ) )
	{
		StructDestroy(parse_CurrencyOrderInfo, cbData->sellOrder);
    }

    cbData->buyOrder->pendingTransCount--;
	if ( ( cbData->buyOrder->pendingTransCount == 0 ) && ( cbData->buyOrder->quantity == 0 ) )
	{
		StructDestroy(parse_CurrencyOrderInfo, cbData->buyOrder);
	}
	
	StructDestroy(parse_MatchOrdersCBData, cbData);
}

static void
MatchOrdersLock_CB(CurrencyExchangeResultType result, ContainerID srcLockID, ContainerID destLockID, char *failureDetailString, MatchOrdersCBData *cbData)
{
	if ( result == CurrencyResultType_Success )
	{
		TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnVal("CurrencyExchange", HandleMatchOrder_CB, cbData);
		bool sameAccount = (cbData->sellOrder->accountID == cbData->buyOrder->accountID);
		AutoTrans_CurrencyExchange_tr_FulfillOrder(returnVal, GLOBALTYPE_CURRENCYEXCHANGESERVER, GLOBALTYPE_CURRENCYEXCHANGE, cbData->sellOrder->accountID, 
			GLOBALTYPE_CURRENCYEXCHANGE, sameAccount ? 0 : cbData->buyOrder->accountID,GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, srcLockID, GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, destLockID, 
			cbData->sellOrder->orderID, cbData->buyOrder->orderID, cbData->tradePrice, cbData->tradeQuantity, sameAccount, timeSecondsSince2000());
	}
	else
	{
        CurrencyExchangeOrderInfoLogAndError("MatchOrdersLockSell", cbData->sellOrder, result, failureDetailString);
        CurrencyExchangeOrderInfoLogAndError("MatchOrderLockBuy", cbData->buyOrder, result, failureDetailString);

		// Free the orders if they have been fully consumed and have no more pending transactions.
        cbData->sellOrder->pendingTransCount--;
        if ( ( cbData->sellOrder->pendingTransCount == 0 ) && ( cbData->sellOrder->quantity == 0 ) )
        {
            StructDestroy(parse_CurrencyOrderInfo, cbData->sellOrder);
        }

        cbData->buyOrder->pendingTransCount--;
        if ( ( cbData->buyOrder->pendingTransCount == 0 ) && ( cbData->buyOrder->quantity == 0 ) )
        {
            StructDestroy(parse_CurrencyOrderInfo, cbData->buyOrder);
        }

		StructDestroy(parse_MatchOrdersCBData, cbData);

        // TODO(jweinstein) - figure out which side of the transaction did not fail to lock and restore the order.

        // Send a warning alert when a lock fails.
        TriggerAlert("CURRENCYEXCHANGE_LOCKFAIL", 
            STACK_SPRINTF("The Currency Exchange failed to acquire an account server key/value lock.  %s", NULL_TO_EMPTY(failureDetailString)), 
            ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0);
	}
}

static void
MatchOrders(void)
{
	CurrencyOrderInfo *firstSell = (CurrencyOrderInfo *)objIndexGetFirst(gSellOrderIndex);
	CurrencyOrderInfo *firstBuy = (CurrencyOrderInfo *)objIndexGetLast(gBuyOrderIndex);

	// If there is a sell price that is lower than a buy price, then we have orders to match
	// Don't continue matching if the exchange is disabled.
    // TODO(jweinstein) - limit the number of matches in one frame.
	while ( CurrencyExchangeIsEnabled() && ( firstSell != NULL ) && ( firstBuy != NULL ) && ( firstSell->price <= firstBuy->price ) )
	{
		U32 tradePrice;
		U32 tradeQuantity;
		CurrencyOrderInfo *sellOrder = firstSell;
		CurrencyOrderInfo *buyOrder = firstBuy;
		MatchOrdersCBData *cbData = StructCreate(parse_MatchOrdersCBData);

		//TODO(jweinstein) - add some order info validation here

		// Use the price from whichever order was entered first.
		if ( sellOrder->listingTime < buyOrder->listingTime )
		{
			tradePrice = sellOrder->price;
		}
		else
		{
			tradePrice = buyOrder->price;
		}

		// Use the smallest of the two quantities.
		tradeQuantity = MIN(sellOrder->quantity, buyOrder->quantity);

		// Save the sell and buy orders so they can be used by the completion callback.
		cbData->sellOrder = sellOrder;
		cbData->buyOrder = buyOrder;
		cbData->tradePrice = tradePrice;
		cbData->tradeQuantity = tradeQuantity;

		if ( sellOrder->quantity == tradeQuantity )
		{
			// This will consume the entire sell order, so remove it from the index and fetch the next order for the next loop iteration.
			if (!objIndexRemove(gSellOrderIndex, sellOrder))
			{
                CurrencyExchangeOrderInfoLogAndError("MatchOrder", sellOrder, CurrencyResultType_InternalError, "Removing orderInfo from sell order index failed");
				BadRuntimeData("Removing from sell order index while matching orders.", NULL);
			}
            if (!objIndexRemove(gAllOrderIndex, sellOrder))
            {
                CurrencyExchangeOrderInfoLogAndError("MatchOrder", sellOrder, CurrencyResultType_InternalError, "Removing sell orderInfo from all order index failed");
                BadRuntimeData("Removing sell orderInfo from all order index failed", NULL);
            }
			firstSell = (CurrencyOrderInfo *)objIndexGetFirst(gSellOrderIndex);
		}

		if ( buyOrder->quantity == tradeQuantity )
		{
			// This will consume the entire buy order, so remove it from the index and fetch the next order for the next loop iteration.
			if (!objIndexRemove(gBuyOrderIndex, buyOrder))
			{
                CurrencyExchangeOrderInfoLogAndError("MatchOrder", buyOrder, CurrencyResultType_InternalError, "Removing orderInfo from buy order index failed");
				BadRuntimeData("Removing from buy order index while matching orders.", NULL);
			}
            if (!objIndexRemove(gAllOrderIndex, buyOrder))
            {
                CurrencyExchangeOrderInfoLogAndError("MatchOrder", buyOrder, CurrencyResultType_InternalError, "Removing buy orderInfo from all order index failed");
                BadRuntimeData("Removing buy orderInfo from all order index failed", NULL);
            }
			firstBuy = (CurrencyOrderInfo *)objIndexGetLast(gBuyOrderIndex);
		}

        CurrencyExchangeOrderInfoLog("MatchOrdersSell", sellOrder, CurrencyResultType_InProgress, "Beginning match of sell order");
        CurrencyExchangeOrderInfoLog("MatchOrdersBuy", buyOrder, CurrencyResultType_InProgress, "Beginning match of buy order");

		// Mark the orders as having a pending transaction.
		sellOrder->pendingTransCount++;
		buyOrder->pendingTransCount++;

		// Update the order quantities.
		sellOrder->quantity -= tradeQuantity;
		buyOrder->quantity -= tradeQuantity;

        DecrementTotalCurrencyAvailable(buyOrder->accountID, OrderType_Buy, tradeQuantity, buyOrder->price);
        DecrementTotalCurrencyAvailable(sellOrder->accountID, OrderType_Sell, tradeQuantity, sellOrder->price);

		cbData->mtcTransferID = MTCTransferRequest(sellOrder->accountID, buyOrder->accountID, microtrans_GetShardForSaleBucketKey(), microtrans_GetShardReadyToClaimBucketKey(), tradeQuantity, cbData, MatchOrdersLock_CB);
	}
}

// Clear out index and conditionally free all the order info structs it contains
// If the CurrencyExchange ever needs to be multithreaded, we need to change how the locking works here.
static void
ClearIndex(ObjectIndex *index, bool freeStruct)
{
    static CurrencyOrderInfo **orderInfos = NULL;
    S64 count;

    while ( (count = objIndexCount(index) ) > 0 )
    {
        int i;
        objIndexCopyEArrayRange(index, &orderInfos, 0, 1000);

        for ( i = eaSize(&orderInfos) - 1; i >= 0; i-- )
        {
            // Remove the order from the index and then free it
            objIndexRemove(index, orderInfos[i]);
            if ( freeStruct )
            {
                StructDestroy(parse_CurrencyOrderInfo, orderInfos[i]);
            }
        }
        // Make sure that the count of objects in the index is going down, so we don't get stuck in this loop
        devassert(count == ( objIndexCount(index) + eaSize(&orderInfos)));
        eaClearFast(&orderInfos);
    }
}

static bool
OrderRemovalsPending(void)
{
    return eaSize(&sOrderRemovalList) > 0;
}

static void
HandleOrderRemovalTimeout(void)
{
    // If we ever get here it is likely that the shard is very sick because transactions are so backed up.
    AssertOrAlert("ORDER_REMOVAL_TIMEOUT", "Order removal timed out after %d seconds with %d removals remaining.", CURRENCY_ORDER_REMOVAL_TIMEOUT, eaSize(&sOrderRemovalList));

    // Clear the list of pending removals.
    eaClearFast(&sOrderRemovalList);
}

static void
RemoveOrdersTrans_CB(TransactionReturnVal *pReturn, CurrencyOrdersToRemove *remove)
{
    static char *sOrdersStr = NULL;
    int i;

    // Remove the order from the list of pending removals
    // This should be fast because the transactions are executed starting at the end of the list, so the amount
    //  of list compaction due to removal should be small.
    // Note that the removal struct might not be in the list because it timed out.  Be careful not to use the results
    //  of the lookup in sOrderRemovalList.
    eaFindAndRemove(&sOrderRemovalList, remove);
    estrClear(&sOrdersStr);

    for(i = 0; i < ea32Size(&remove->orderIDs); i++)
    {
        estrConcatf(&sOrdersStr, "%u,", remove->orderIDs[i]);
    }
    
    if ( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
    {
        aslCurrencyExchange_Log("%s: Order removal succeeded: accountID=%u:orderIDs=%s", __FUNCTION__, remove->accountID, sOrdersStr);
    }
    else
    {
        ErrorDetailsf("accountID=%u:orderIDs=%s", remove->accountID, sOrdersStr);
        Errorf("%s: Order removal failed", __FUNCTION__);
        aslCurrencyExchange_Log("%s: Order removal failed: accountID=%u:orderIDs=%s", __FUNCTION__, remove->accountID, sOrdersStr);
    }

    StructDestroy(parse_CurrencyOrdersToRemove, remove);
}

static void
InitAccountDataOnScan_CB(TransactionReturnVal *pReturn, void *userData)
{
    // TODO(jweinstein)
    if ( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
    {
    }
    else
    {
    }
}

//
// Scan all order containers to (re)build indices of buy and sell orders and generate a list of containers that should be deleted
//
static void
ScanAllOrders(void)
{
    int i;
    ContainerIterator iter;
    CurrencyExchangeAccountData *currencyAccountData;
    Container *container;

    if ( eaSize(&sOrderRemovalList) > 0 )
    {
        AssertOrAlert("CURRENCY_REMOVE_ORDER_QUEUE_NOT_EMPTY", "The currency exchange order removal list is not empty during a full order scan, which should never happen.  Disabling the Currency Exchange.");
        DisableCurrencyExchangeInternal();
        return;
    }

    // save the time, so that we can time out while waiting for removals to complete
    sLastContainerScanTime = timeSecondsSince2000();

    // Reset total buy/sell counts.
    sTotalMTCToBuy = 0;
    sTotalMTCToSell = 0;
    sTotalTCToBuy = 0;
    sTotalTCToSell = 0;

    // Create or clear order info indices
    if ( gBuyOrderIndex == NULL )
    {
        gBuyOrderIndex = objIndexCreateWithStringPaths(4, 0, parse_CurrencyOrderInfo, ".price", ".listingTimeReverse", NULL);
    }
    else
    {
        ClearIndex(gBuyOrderIndex, false);
    }
    if ( gSellOrderIndex == NULL )
    {
        gSellOrderIndex = objIndexCreateWithStringPaths(4, 0, parse_CurrencyOrderInfo, ".price", ".listingTime", NULL);
    } 
    else
    {
        ClearIndex(gSellOrderIndex, false);
    }

    if ( gAllOrderIndex == NULL )
    {
        gAllOrderIndex = objIndexCreateWithStringPath(4, 0, parse_CurrencyOrderInfo, ".orderKey");
    } 
    else
    {
        // all orders will be in gAllOrderIndex, and one of the others, so only actually free them when clearing this index.
        ClearIndex(gAllOrderIndex, true);
    }

    objInitContainerIteratorFromType(GLOBALTYPE_CURRENCYEXCHANGE, &iter);
    while (container = objGetNextContainerFromIterator(&iter))
    {
        currencyAccountData = (CurrencyExchangeAccountData *)container->containerData;

        if ( currencyAccountData->iAccountID != container->containerID )
        {
            if ( currencyAccountData->iAccountID == 0 )
            {
                TransactionReturnVal *returnVal;

                ErrorDetailsf("containerID=%d", container->containerID);
                Errorf("Currency Exchange: %s: Found container with account ID 0.  Initializing it.", __FUNCTION__)   ;

                // container has not been initialized yet, so do it now
                returnVal = objCreateManagedReturnVal(InitAccountDataOnScan_CB, NULL);
                AutoTrans_CurrencyExchange_tr_InitAccountData(returnVal, GLOBALTYPE_CURRENCYEXCHANGESERVER, GLOBALTYPE_CURRENCYEXCHANGE, container->containerID, container->containerID);
            }
            else
            {
                // if the containerID and accountID don't match, and the accountID is not zero, then something bad happened.  Don't try to fix it automatically.
                // XXX - should we alert here?
                ErrorDetailsf("containerID=%d, accountID=%d", container->containerID, currencyAccountData->iAccountID);
                Errorf("Currency Exchange: %s: Found container with incorrect account ID", __FUNCTION__);         
            }
        }
        else
        {
            // scan for completed or withdrawn orders, and add them to the order removal list
            CurrencyOrdersToRemove *remove = NULL;

            for ( i = eaSize(&currencyAccountData->openOrders) - 1; i >= 0; i-- )
            {
                CurrencyExchangeOpenOrder *order = currencyAccountData->openOrders[i];
                if ( order->orderType != OrderType_None )
                {
                    // if the order has been completed then add it to the order removal queue
                    if ( isCurrencyOrderComplete(order) )
                    {
                        if ( remove == NULL )
                        {
                            remove = StructCreate(parse_CurrencyOrdersToRemove);
                            remove->accountID = currencyAccountData->iAccountID;
                        }
                        ea32Push(&remove->orderIDs, order->orderID);
                    }
                    else // the order should still be open, so add it to the indices
                    {
                        CreateOrderInfo(currencyAccountData->iAccountID, order->orderID, order->orderType, order->quantity - order->consumedMTC, order->price, order->listingTime);
                    }
                }
            }
            if ( remove != NULL )
            {
                eaPush(&sOrderRemovalList, remove);
            }
        }
    }
	objClearContainerIterator(&iter);

    for ( i = eaSize(&sOrderRemovalList) - 1; i >= 0; i-- )
    {
        TransactionReturnVal *returnVal = objCreateManagedReturnVal(RemoveOrdersTrans_CB, sOrderRemovalList[i]);
        AutoTrans_CurrencyExchange_tr_RemoveOrders(returnVal, GLOBALTYPE_CURRENCYEXCHANGESERVER, GLOBALTYPE_CURRENCYEXCHANGE, sOrderRemovalList[i]->accountID, sOrderRemovalList[i]);
    }
}

// Allow two minutes for zero order ID fixup transactions to complete before alerting and moving on.
#define FIXUP_TIMEOUT (2 * 60)
static INT_EARRAY sFixupPendingContainerIDs = NULL;
static U32 sFixupStartTime;

static void
FixupZeroOrderID_CB(TransactionReturnVal *returnVal, CurrencyExchangeAccountData *currencyAccountData)
{
    // Log whether the fixup was successful or not.
    if ( returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
    {
        aslCurrencyExchange_Log("FixupZeroOrderID:Success:accountID=%u", currencyAccountData->iAccountID);
    }
    else
    {
        aslCurrencyExchange_Log("FixupZeroOrderID:Failed:accountID=%u", currencyAccountData->iAccountID);
    }

    // Remove from the pending fixup array.
    ea32FindAndRemove(&sFixupPendingContainerIDs, currencyAccountData->iAccountID);
}

static void
ScanForZeroOrderID(void)
{
    int i;
    ContainerIterator iter;
    CurrencyExchangeAccountData *currencyAccountData;
    Container *container;

    objInitContainerIteratorFromType(GLOBALTYPE_CURRENCYEXCHANGE, &iter);
    while (container = objGetNextContainerFromIterator(&iter))
    {
        currencyAccountData = (CurrencyExchangeAccountData *)container->containerData;

        for ( i = eaSize(&currencyAccountData->openOrders) - 1; i >= 0; i-- )
        {
            CurrencyExchangeOpenOrder *order = currencyAccountData->openOrders[i];
            if ( ( order->orderType != OrderType_None ) && ( order->orderID == 0) )
            {
                TransactionReturnVal *returnVal;
                ea32Push(&sFixupPendingContainerIDs, currencyAccountData->iAccountID);

                returnVal = objCreateManagedReturnVal(FixupZeroOrderID_CB, currencyAccountData);
                AutoTrans_CurrencyExchange_tr_FixupBadOrderID(returnVal, GLOBALTYPE_CURRENCYEXCHANGESERVER, GLOBALTYPE_CURRENCYEXCHANGE, currencyAccountData->iAccountID);
                break;
            }
        }
    }
	objClearContainerIterator(&iter);
}

static void
ProcessQueue(void)
{
    int requestsProcessed = 0;
    CurrencyExchangeOperationRequest *operation;

    while ( !qIsEmpty(sNewOrderQueue) && ( requestsProcessed < QUEUED_ORDERS_PER_TICK ) )
    {
        operation = qDequeue(sNewOrderQueue);

        CurrencyExchangeReceiveOperation(operation, false, true);

        requestsProcessed++;
    }

}

static void
CurrencyExchangeContainerLoadingDone(void)
{
    printf("Currency Exchange container transfer complete\n");
    ISM_SwitchToSibling(CE_STATE_MACHINE, STATE_MACHINE_USER_OBJ, CE_STATE_SCAN_FOR_ZERO_ORDERID);
}

static void 
CEScanZeroOrderID_Enter(InstancedMachineHandleOrName pStateMachineHandle, void *userData, F32 fElapsed)
{
    sFixupStartTime = timeSecondsSince2000();
    ScanForZeroOrderID();
}

static void 
CEScanZeroOrderID_BeginFrame(InstancedMachineHandleOrName pStateMachineHandle, void *userData, F32 fElapsed)
{
    if ( ea32Size(&sFixupPendingContainerIDs) == 0 )
    {
        if ( gPauseAfterFixup )
        {
            ISM_SwitchToSibling(CE_STATE_MACHINE, STATE_MACHINE_USER_OBJ, CE_STATE_PAUSE_AFTER_FIXUP);
        }
        else
        {
            ISM_SwitchToSibling(CE_STATE_MACHINE, STATE_MACHINE_USER_OBJ, CE_STATE_SCAN_ALL_CONTAINERS);
        }
    }
    else if ( ( sFixupStartTime + FIXUP_TIMEOUT ) < timeSecondsSince2000() )
    {
        char *uncompletedAccountIDs = NULL;
        int i;

        // Collect the accountIDs that didn't finish so that they can be listed in the alert.
        for( i = 0; i < ea32Size(&sFixupPendingContainerIDs); i++ )
        {
            estrConcatf(&uncompletedAccountIDs, "%u, ", sFixupPendingContainerIDs[i]);
        }

        // Alert that the fixup transactions didn't all finish.
        TriggerAlert("CURRENCY_EXCHANGE_FIXUP_TIMEOUT", STACK_SPRINTF("The currency exchange performed Zero OrderID fixup, which timed out.  Uncompleted transaction account IDs: %s.", NULL_TO_EMPTY(uncompletedAccountIDs)), ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0);

        estrDestroy(&uncompletedAccountIDs);

        // Continue with normal processing.
        ISM_SwitchToSibling(CE_STATE_MACHINE, STATE_MACHINE_USER_OBJ, CE_STATE_SCAN_ALL_CONTAINERS);
    }

}

static void 
CEPauseAfterFixup_Enter(InstancedMachineHandleOrName pStateMachineHandle, void *userData, F32 fElapsed)
{
    TriggerAlert("CURRENCY_EXCHANGE_PAUSED", "The currency exchange has paused during startup because you used the -PauseAfterFixup command line flag.", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0);
}

static void 
CEPauseAfterFixup_BeginFrame(InstancedMachineHandleOrName pStateMachineHandle, void *userData, F32 fElapsed)
{
    // Nothing here for now.
}

static void 
CEScanAllContainers_Enter(InstancedMachineHandleOrName pStateMachineHandle, void *userData, F32 fElapsed)
{
    ScanAllOrders();
}

static void 
CEScanAllContainers_BeginFrame(InstancedMachineHandleOrName pStateMachineHandle, void *userData, F32 fElapsed)
{
    if ( OrderRemovalsPending() )
    {
        if ( ( sLastContainerScanTime + CURRENCY_ORDER_REMOVAL_TIMEOUT ) > timeSecondsSince2000() )
        {
            HandleOrderRemovalTimeout();
            ISM_SwitchToSibling(CE_STATE_MACHINE, STATE_MACHINE_USER_OBJ, CE_STATE_PROCESS_QUEUE);
        }
    }
    else
    {
        // order removals are all done
        ISM_SwitchToSibling(CE_STATE_MACHINE, STATE_MACHINE_USER_OBJ, CE_STATE_PROCESS_QUEUE);
    }
}

static void 
CELoadingContainers_Enter(InstancedMachineHandleOrName pStateMachineHandle, void *userData, F32 fElapsed)
{
    RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "LoadingContainers");
    aslAcquireContainerOwnership(GLOBALTYPE_CURRENCYEXCHANGE, CurrencyExchangeContainerLoadingDone);
}

static void 
CEProcessQueue_BeginFrame(InstancedMachineHandleOrName pStateMachineHandle, void *userData, F32 fElapsed)
{
    // process pending container creations
    ProcessPendingContainers();

    MTCTransfer_BeginFrame();

    ProcessQueue();

    MatchOrders();

    if ( qIsEmpty(sNewOrderQueue) )
    {
        ISM_SwitchToSibling(CE_STATE_MACHINE, STATE_MACHINE_USER_OBJ, CE_STATE_PROCESS_ORDERS);
    }
}

static void 
CEProcessOrders_Enter(InstancedMachineHandleOrName pStateMachineHandle, void *userData, F32 fElapsed)
{
    RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "ready");
}

static void 
CEProcessOrders_BeginFrame(InstancedMachineHandleOrName pStateMachineHandle, void *userData, F32 fElapsed)
{
	// Process any withdrawals that have been waiting for transactions to complete.
	ProcessPendingWithdrawals();

    // Process pending container creations.
    ProcessPendingContainers();

    MTCTransfer_BeginFrame();

    MatchOrders();
}

int 
CurrencyExchangeInit(void)
{
    const char *currencyKeyName = microtrans_GetShardCurrency();
    const char *exchangeWithdrawCurrencyKeyName = microtrans_GetExchangeWithdrawCurrency();
    const char *readyToClaimKeyName = microtrans_GetShardReadyToClaimBucketKey();
    const char *forSaleKeyName = microtrans_GetShardForSaleBucketKey();

    // If the currency exchange is enabled but the required key names are not configured, then disable the exchange and alert.
    if ( CurrencyExchangeIsEnabled() && ( ( currencyKeyName == NULL || currencyKeyName[0] == 0 ) || 
        ( exchangeWithdrawCurrencyKeyName == NULL || exchangeWithdrawCurrencyKeyName[0] == 0 ) ||
        ( readyToClaimKeyName == NULL || readyToClaimKeyName[0] == 0 ) || 
        ( forSaleKeyName == NULL || forSaleKeyName[0] == 0 ) ) )
    {
        DisableCurrencyExchangeInternal();
        TriggerAlert("CURRENCY_EXCHANGE_MISSING_MICROTRANS_KEYS", STACK_SPRINTF("The Currency Exchange is enabled, but is the microtrans key names required for c-point trading are not configured.  Check that the proper microtrans category is set for the shard.  currencyKey=%s, readyToClaimKey=%s, forSaleKey=%s", currencyKeyName, readyToClaimKeyName, forSaleKeyName), ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0);
    }

    sNewOrderQueue = createQueue();
    initQueue(sNewOrderQueue, 100);

	MTCTransferInit();

    ISM_AddInstancedState(CE_STATE_MACHINE, CE_STATE_LOAD_CONTAINERS, CELoadingContainers_Enter, NULL, NULL, NULL);
    ISM_AddInstancedState(CE_STATE_MACHINE, CE_STATE_SCAN_FOR_ZERO_ORDERID, CEScanZeroOrderID_Enter, CEScanZeroOrderID_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(CE_STATE_MACHINE, CE_STATE_PAUSE_AFTER_FIXUP, CEPauseAfterFixup_Enter, CEPauseAfterFixup_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(CE_STATE_MACHINE, CE_STATE_SCAN_ALL_CONTAINERS, CEScanAllContainers_Enter, CEScanAllContainers_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(CE_STATE_MACHINE, CE_STATE_PROCESS_QUEUE, NULL, CEProcessQueue_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(CE_STATE_MACHINE, CE_STATE_PROCESS_ORDERS, CEProcessOrders_Enter, CEProcessOrders_BeginFrame, NULL, NULL);
    ISM_AddInstancedState(CE_STATE_MACHINE, CE_STATE_WAIT_PENDING_COMPLETE, NULL, NULL, NULL, NULL);

    ISM_CreateMachine(CE_STATE_MACHINE, STATE_MACHINE_USER_OBJ, CE_STATE_LOAD_CONTAINERS);
	return 1;
}

static void
SendGlobalUIDataToSubscriber(GlobalType serverType, ContainerID serverID)
{
	RemoteCommand_gslCurrencyExchange_ReturnGlobalUIData(serverType, serverID, &sGlobalUIData);
}

void
CurrencyExchangeOncePerFrame(F32 fElapsed)
{
	static U32 sLastFrameTimeSeconds = 0;
	U32 curFrameTimeSeconds = timeSecondsSince2000();

	// Only tick the currency exchange if it is enabled.
	if ( CurrencyExchangeIsEnabled() )
	{
		ISM_Tick(CE_STATE_MACHINE, STATE_MACHINE_USER_OBJ, fElapsed);
	}
    else
    {
        // If the account server is not available, check once per minute to see if it has come back.
        if ( sAccountServerAvailable == false )
        {
            if ( ( curFrameTimeSeconds % 60 ) == 0 )
            {
                CurrencyExchange_CheckAccountServerAvailable();
            }
        }
    }

	// Only update UI data once per second.
	if ( curFrameTimeSeconds != sLastFrameTimeSeconds )
	{
		sLastFrameTimeSeconds = curFrameTimeSeconds;

		// Only update UI data when we are in a state that has valid orderInfo indices.
		if ( ISM_IsStateActive(CE_STATE_MACHINE, STATE_MACHINE_USER_OBJ, CE_STATE_PROCESS_QUEUE) || 
			ISM_IsStateActive(CE_STATE_MACHINE, STATE_MACHINE_USER_OBJ, CE_STATE_PROCESS_ORDERS) || 
			ISM_IsStateActive(CE_STATE_MACHINE, STATE_MACHINE_USER_OBJ, CE_STATE_WAIT_PENDING_COMPLETE) )
		{
			// Update the global UI data.
			UpdateGlobalUIData();

			// Send updates to servers that have requested global UI data.
			CurrencyExchange_UpdateSubscribers(SendGlobalUIDataToSubscriber, SEND_UIDATA_TO_SERVERS_INTERVAL, SEND_UIDATA_TO_SERVERS_EXPIRE_INTERVAL);
		}
	}

	return;
}

CurrencyExchangeOverview *
CurrencyExchange_GetServerInfoOverview(void)
{
    static CurrencyExchangeOverview overview = {0};

    int i;
    int logSize;
    int srcIndex;

    ISM_PutFullStateStackIntoEString(CE_STATE_MACHINE, STATE_MACHINE_USER_OBJ, &overview.currentState);

    overview.numBuyOrders = objIndexCount(gBuyOrderIndex);
    overview.numSellOrders = objIndexCount(gSellOrderIndex);
    overview.numOrders = objIndexCount(gAllOrderIndex);
    overview.numPendingContainerCreates = eaSize(&sPendingContainerCreates);
    overview.numPendingWithdrawals = eaSize(&gPendingWithdrawals);
    overview.numQueuedOperations = qGetSize(sNewOrderQueue);
    overview.accountServerAvailable = sAccountServerAvailable;
    overview.totalMTCToBuy = sTotalMTCToBuy;
    overview.totalMTCToSell = sTotalMTCToSell;
    overview.totalTCToBuy = sTotalTCToBuy;
    overview.totalTCToSell = sTotalTCToSell;

    overview.globalUiData = &sGlobalUIData;

    // Free the previous log contents if any.
    logSize = eaSize(&overview.recentLogLines);
    for ( i = 0; i < logSize; i++ )
    {
        free(overview.recentLogLines[i]);
    }

    // Set the size of the recent logs array.
    logSize = eaSize(&sRecentLogLines);
    eaSetSize(&overview.recentLogLines, logSize);

    // Copy log lines into overview struct.
    srcIndex = sNextLogIndex;
    if ( srcIndex >= logSize )
    {
        srcIndex = 0;
    }
    for ( i = 0; i < logSize; i++ )
    {
        overview.recentLogLines[i] = strdup(sRecentLogLines[srcIndex]);
        srcIndex++;
        if ( srcIndex >= logSize )
        {
            srcIndex = 0;
        }
    }

    MTCTransfer_GetOverviewInfo(&overview);

    return &overview;
}

#define MAX_PENDING_CANCEL_TRANSACTIONS 500

INT_EARRAY s_PendingCancelContainerIDs = NULL;
int s_NumCancelsComplete = 0;
int s_NumCancelsFailed = 0;
U32 s_CancelStartTime = 0;
int s_CancelTransactionsPending = 0;

static void
CancelOrdersShardOnly_CB(TransactionReturnVal *returnVal, void *cbData)
{
    if ( returnVal->eOutcome != TRANSACTION_OUTCOME_SUCCESS )
    {
        s_NumCancelsFailed++;
    }
    s_CancelTransactionsPending--;
    s_NumCancelsComplete++;

    if ( ( s_NumCancelsComplete % 100 ) == 0 ) 
    {
        printf("Completed cancel of orders for %d accounts.  %d remaining.\n", 
            s_NumCancelsComplete, ea32Size(&s_PendingCancelContainerIDs) + s_CancelTransactionsPending);
    }
    if ( ( s_CancelTransactionsPending == 0 ) && ( ea32Size(&s_PendingCancelContainerIDs) == 0 ) )
    {
        printf("Completed cancel of orders for all(%d) accounts.\n", s_NumCancelsComplete);
        TriggerAlert("CURRENCY_EXCHANGE_CANCEL_ALL_ORDERS_COMPLETE", "Cancel all orders command complete.", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0);

    }
}

static void
StartCancelTransactions(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
    while ( ( s_CancelTransactionsPending < MAX_PENDING_CANCEL_TRANSACTIONS ) && ( ea32Size(&s_PendingCancelContainerIDs) > 0 ) )
    {
        ContainerID containerID = ea32Pop(&s_PendingCancelContainerIDs);
        TransactionReturnVal *returnVal = objCreateManagedReturnVal(CancelOrdersShardOnly_CB, NULL);

        s_CancelTransactionsPending++;
        AutoTrans_CurrencyExchange_tr_CancelAllOrdersShardOnly(returnVal, GLOBALTYPE_CURRENCYEXCHANGESERVER, GLOBALTYPE_CURRENCYEXCHANGE, containerID);
    }

    if ( ea32Size(&s_PendingCancelContainerIDs) > 0 )
    {
        // If there are more transactions to run, then run again in 1 second.
        TimedCallback_Run(StartCancelTransactions, NULL, 1);
    }
}

void
aslCurrencyExchange_CancelAllOrdersShardOnly(void)
{
    ContainerIterator iter = {0};
    Container *container;

    if ( s_NumCancelsComplete || ( ea32Size(&s_PendingCancelContainerIDs) > 0 ) )
    {
        // Alert if someone tries to do the cancel all orders twice.
        TriggerAlert("CURRENCY_EXCHANGE_CANCEL_ALL_ORDERS_REENTRY", "Don't run the cancel all orders command twice.", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0);
        return;
    }

    if ( !ISM_IsStateActive(CE_STATE_MACHINE, STATE_MACHINE_USER_OBJ, CE_STATE_PAUSE_AFTER_FIXUP) )
    {
        // Alert if the currency exchange is not paused.
        TriggerAlert("CURRENCY_EXCHANGE_CANCEL_ALL_ORDERS_NOT_PAUSED", "Don't run the cancel all orders command if the currency exchange is not paused.", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0);
        return;
    }

    s_CancelStartTime = timeSecondsSince2000();

    // First iterate through all existing 
    objInitContainerIteratorFromType(GLOBALTYPE_CURRENCYEXCHANGE, &iter);
    while (container = objGetNextContainerFromIterator(&iter))
    {
        CurrencyExchangeAccountData *currencyAccountData;
        bool pendingOrders = false;
        int i;

        currencyAccountData = (CurrencyExchangeAccountData *)container->containerData;

        // See if the container has any pending orders.
        for ( i = eaSize(&currencyAccountData->openOrders) - 1; i >= 0; i-- )
        {
            if ( currencyAccountData->openOrders[i]->orderType != OrderType_None )
            {
                pendingOrders = true;
                break;
            }
        }

        // If the container pending orders or any for sale escrow balance, then save the ID so we can run the transaction on it.
        if ( pendingOrders || ( currencyAccountData->forSaleEscrowTC != 0 ) )
        {
            ea32Push(&s_PendingCancelContainerIDs, currencyAccountData->iAccountID);
        }
    }
	objClearContainerIterator(&iter);

    StartCancelTransactions(NULL, 0, NULL);
}

#include "autogen/aslCurrencyExchange_c_ast.c"
#include "autogen/aslCurrencyExchange_h_ast.c"