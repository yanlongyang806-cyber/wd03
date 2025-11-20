#pragma once
/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CurrencyExchangeCommon.h"

// Five minute timeout for creating containers.
#define CURRENCY_EXCHANGE_CONTAINER_CREATE_TIMEOUT 5*60

// The maximum time we wait for order removal to complete before timing out and allowing other processing.
#define CURRENCY_ORDER_REMOVAL_TIMEOUT 5*60

// Throttle the number of order removal transactions we will do per tick to avoid spamming the transaction server.
#define ORDER_REMOVALS_PER_TICK 10

// Throttle the number of queued order we will handle per tick to avoid spamming the transaction server.
#define QUEUED_ORDERS_PER_TICK 10

// How often (in seconds) to send global ui data to servers.
#define SEND_UIDATA_TO_SERVERS_INTERVAL 1

// How long (in seconds) to continue sending global ui data to a server without getting a request.
#define SEND_UIDATA_TO_SERVERS_EXPIRE_INTERVAL 10

AUTO_ENUM;
typedef enum CurrencyTransactionState
{
    CurrencyActionState_None = 0,
    CurrencyActionState_Pending,
    CurrencyActionState_Started,
    CurrencyActionState_Completed,
    CurrencyActionState_Failed,
} CurrencyTransactionState;

AUTO_STRUCT;
typedef struct CurrencyOrdersToRemove
{
    ContainerID accountID;
    U32 *orderIDs;
} CurrencyOrdersToRemove;

//
// This struct is used to hold the non-persisted copy of order info that is used to decide which orders to match.
//
AUTO_STRUCT;
typedef struct CurrencyOrderInfo
{
    // Unique key for this order.  It is derived from accountID and orderID like so:
    //  orderKey = ((S64)orderID << 32) | accountID
    // This is used to look up exact orders in the all orders object index.
    S64 orderKey;

    // Order details
    ContainerID accountID;
    CurrencyExchangeOrderType orderType;
    U32 orderID;
    U32 quantity;
    U32 price;

    // Used as a secondary sorting key along with price on sell orders, so that they will be ordered from oldest to newest.
    U32 listingTime;

    // This is set to U32_MAX - listingTime, so that when it is used for ordering, it will give a newest to oldest ordering.
    // Used as a secondary sorting key along with price on buy orders.  The buy order index is ordered from lowest to highest price, 
    //  and so we need to traverse it in reverse order since we want to pull orders from the high priced end of the list.  
    //  The use of reversed time as the secondary key maintains the oldest to newest ordering when traversing the index in reverse.
    U32 listingTimeReverse;

    // Number of transactions that are pending on this order.
    U32 pendingTransCount;

    // Will be true if this order is in the process of being withdrawn.
    bool withdrawalPending;
} CurrencyOrderInfo;

AUTO_ENUM;
typedef enum CurrencyExchangeCreatingContainerState {
    CreateContainer_CreateStarted,
    CreateContainer_InitStarted,
    CreateContainer_WaitingForContainer,
    CreateContainer_Failed,
    CreateContainer_InitFailed,
} CurrencyExchangeCreatingContainerState;

//
// This struct is used to maintain state during creation of CurrencyExchangeAccountData containers, and to ensure
//  that the container creation is done only once, even if multiple operations arrive before the creation is complete.
//
AUTO_STRUCT;
typedef struct CurrencyExchangeCreatingContainerCBData {
    // The current state of this container creation
    CurrencyExchangeCreatingContainerState state;

    // The account ID of the container being created
    ContainerID accountID;

    // The time that the creation started. Used to timeout creations that never complete.
    U32 createTime;

    // The list of queued operations that are waiting for this container
    EARRAY_OF(CurrencyExchangeOperationRequest) operations;
} CurrencyExchangeCreatingContainerCBData;

AUTO_STRUCT;
typedef struct CurrencyExchangeOverview {
    char *currentState;                         AST(ESTRING)
    bool accountServerAvailable;
    U32 numBuyOrders;
    U32 numSellOrders;
    U64 totalMTCToSell;
    U64 totalMTCToBuy;
    U64 totalTCToSell;
    U64 totalTCToBuy;
    U32 numOrders;
    U32 numQueuedOperations;
    U32 numPendingWithdrawals;
    U32 numPendingContainerCreates;

    U32 numMTCTransfersPending;
    U32 numMTCTransferLockedAccounts;

    CurrencyExchangeGlobalUIData *globalUiData;
    STRING_EARRAY recentLogLines;
} CurrencyExchangeOverview;

int CurrencyExchangeInit(void);
bool CurrencyExchangeIsEnabled(void);
void CurrencyExchangeOncePerFrame(F32 fElapsed);
void CurrencyExchangeReceiveOperation(CurrencyExchangeOperationRequest *operation, bool containerMustExist, bool processingQueue);
void CurrencyExchangeOperationNotifyEx(CurrencyExchangeOperationRequest *operation, CurrencyExchangeResultType errorType, const char *logMessage, bool doError, bool doLog, bool doNotify, const char *func, int line);
#define CurrencyExchangeOperationFail(op, err, msg) CurrencyExchangeOperationNotifyEx(op, err, msg, true, true, true, __FUNCTION__, __LINE__)
#define CurrencyExchangeOperationLog(op, err, msg) CurrencyExchangeOperationNotifyEx(op, err, msg, false, true, false, __FUNCTION__, __LINE__)
#define CurrencyExchangeOperationLogAndError(op, err, msg) CurrencyExchangeOperationNotifyEx(op, err, msg, true, true, false, __FUNCTION__, __LINE__)
#define CurrencyExchangeOperationNotify(op, err, msg, doError, doLog, doNotify) CurrencyExchangeOperationNotifyEx(op, err, msg, doError, doLog, doNotify, __FUNCTION__, __LINE__)
void CurrencyExchangeOrderInfoLogAndErrorEx(const char *tag, CurrencyOrderInfo *orderInfo, CurrencyExchangeResultType errorType, const char *logMessage, bool doError, bool doLog, const char *func, int line);
#define CurrencyExchangeOrderInfoLogAndError(tag, orderInfo, err, msg) CurrencyExchangeOrderInfoLogAndErrorEx(tag, orderInfo, err, msg, true, true, __FUNCTION__, __LINE__)
#define CurrencyExchangeOrderInfoLog(tag, orderInfo, err, msg) CurrencyExchangeOrderInfoLogAndErrorEx(tag, orderInfo, err, msg, false, true, __FUNCTION__, __LINE__)
#define CurrencyExchangeOrderInfoError(tag, orderInfo, err, msg) CurrencyExchangeOrderInfoLogAndErrorEx(tag, orderInfo, err, msg, true, false, __FUNCTION__, __LINE__)
void CurrencyExchange_DumpOrderInfos(int count, char **outStrHandle);
CurrencyExchangeGlobalUIData *CurrencyExchange_GetGlobalUIData(void);
CurrencyExchangeOverview *CurrencyExchange_GetServerInfoOverview(void);
void aslCurrencyExchange_Log(char* format, ...);
void CurrencyExchange_CheckAccountServerAvailable(void);

void aslCurrencyExchange_WithdrawOrder(ContainerID accountID, ContainerID characterID, const char *charName, U32 orderID, const char *debugName, const char *gameSpecificLogInfo);

// The name of the currency exchange state machine
#define CE_STATE_MACHINE "CurrencyExchangeStateMachine"

// The names of the states for the currency exchange
#define CE_STATE_LOAD_CONTAINERS "LoadingContainers"
#define CE_STATE_SCAN_FOR_ZERO_ORDERID "ScanForZeroOrderID"
#define CE_STATE_PAUSE_AFTER_FIXUP "PauseAfterFixup"
#define CE_STATE_SCAN_ALL_CONTAINERS "ScanAllContainers"
#define CE_STATE_WAIT_FOR_REMOVALS "WaitForRemovals"
#define CE_STATE_PROCESS_QUEUE "ProcessQueue"
#define CE_STATE_PROCESS_ORDERS "ProcessOrders"
#define CE_STATE_WAIT_PENDING_COMPLETE "WaitPendingComplete"

void aslCurrencyExchange_CancelAllOrdersShardOnly(void);