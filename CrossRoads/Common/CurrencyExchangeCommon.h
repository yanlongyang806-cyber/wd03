#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GlobalTypeEnum.h"

typedef U32 ContainerID;
typedef struct CurrencyOrderInfo CurrencyOrderInfo;

AUTO_ENUM;
typedef enum CurrencyExchangeOrderType {
    OrderType_None = 0,
    OrderType_Buy,
    OrderType_Sell,
} CurrencyExchangeOrderType;

AUTO_ENUM;
typedef enum CurrencyExchangeOperationType {
    CurrencyOpType_None = 0,
    CurrencyOpType_CreateOrder,
    CurrencyOpType_FulfillOrder,
    CurrencyOpType_WithdrawOrder,
    CurrencyOpType_ExpireOrder,
    CurrencyOpType_ClaimMTC,
    CurrencyOpType_ClaimTC,
    CurrencyOpType_EscrowMTC,
    CurrencyOpType_EscrowTC,
	CurrencyOpType_ExcessTCReturn,
} CurrencyExchangeOperationType;

AUTO_ENUM;
typedef enum CurrencyExchangeResultType {
    CurrencyResultType_None = 0,
    CurrencyResultType_Success,
    CurrencyResultType_InternalError,
    CurrencyResultType_ExchangeDisabled,
    CurrencyResultType_TooManyOrders,
    CurrencyResultType_Info,
    CurrencyResultType_NotEnoughTC,
    CurrencyResultType_NotEnoughMTC,
    CurrencyResultType_DuplicateWithdrawal,
    CurrencyResultType_QuantityOutOfRange,
    CurrencyResultType_PriceOutOfRange,
    // InProgress is used for logging operations that are in progress.  It should not be returned as a result of any operation.
    CurrencyResultType_InProgress,
} CurrencyExchangeResultType;

AUTO_STRUCT AST_CONTAINER;
typedef struct CurrencyExchangeLogEntry {
    // What type of operation is being logged. Used by all log types.
    const CurrencyExchangeOperationType logType;      AST(PERSIST SUBSCRIBE)

    // The type of order. Used by CreateOrder, FulfillOrder, WithdrawOrder and ExpireOrder.
    const CurrencyExchangeOrderType orderType;  AST(PERSIST SUBSCRIBE)

    // The ID of the order. Used by CreateOrder, FulfillOrder, WithdrawOrder and ExpireOrder.
    const U32 orderID;                          AST(PERSIST SUBSCRIBE)

    // The quantity of MTC involved in the logged operation. Used by CreateOrder, FulfillOrder, WithdrawOrder, ExpireOrder and ClaimMTC.
    const U32 quantityMTC;                      AST(PERSIST SUBSCRIBE)

    // The quantity of TC involved in the logged operation. Used by CreateOrder, FulfillOrder, WithdrawOrder, ExpireOrder and ClaimTC.
    const U32 quantityTC;                       AST(PERSIST SUBSCRIBE)

    // The time that the logged operation occurred. Used by all log types.
    const U32 time;                             AST(PERSIST SUBSCRIBE)

    // The name of the character that performed the operation, or "website"
    CONST_STRING_POOLED characterName;          AST(PERSIST SUBSCRIBE POOL_STRING)
} CurrencyExchangeLogEntry;

// A persisted open order
AUTO_STRUCT AST_CONTAINER;
typedef struct CurrencyExchangeOpenOrder {
    // The ID of this order.  It is unique per account.  To fully identify an order you need both account ID and order ID.
    const U32 orderID;                          AST(PERSIST SUBSCRIBE)

    // The order type (buy or sell)
    const CurrencyExchangeOrderType orderType;  AST(PERSIST SUBSCRIBE)

    // The amount of MTC to buy or sell.  Note that this is the original amount, and is not decremented as the order if partially fulfilled.
	// For sell orders, this is the quantity of MTC that is put into escrow.
    const U32 quantity;                         AST(PERSIST SUBSCRIBE)

    // The price (in TC) for one MTC.
	// For buy orders, the quantity of TC that is put into escrow is (quantity * price).
    const U32 price;                            AST(PERSIST SUBSCRIBE)

    // The time this order was created.  This is used to expire old orders.
    const U32 listingTime;                      AST(PERSIST SUBSCRIBE)

	// The amount of TC that has been used to fulfill this order so far.
	// For buy orders, this indicates how much of the escrowed TC has been spent.  If the order is withdrawn, the
	//  amount returned to the player is ((quantity * price) - consumedTC)
    const U32 consumedTC;						AST(PERSIST SUBSCRIBE)

	// The amount of MTC that has been used to fulfill this order so far.
	// For sell orders, this indicates how much of the escrowed TC has been sold.  If the order is withdrawn, the
	//  amount returned to the player is (quantity - consumedMTC)
	const U32 consumedMTC;						AST(PERSIST SUBSCRIBE)
} CurrencyExchangeOpenOrder;

AUTO_STRUCT AST_CONTAINER;
typedef struct CurrencyExchangeAccountData {
    const ContainerID iAccountID;               AST(PERSIST SUBSCRIBE KEY)

    // Time Currency that is in escrow for currently open orders.  When an order is created, the TC is moved from the player character
    //  numeric to this variable.
    const U32 forSaleEscrowTC;                  AST(PERSIST SUBSCRIBE)

    // Time Currency that is in escrow waiting to be claimed by the player.  TC ends up here as the result of fulfilling an order
    //  or an order being withdrawn.
    const U32 readyToClaimEscrowTC;             AST(PERSIST SUBSCRIBE)

    // The order ID to use for the next order.  Used to ensure order IDs are unique per player account.
    const U32 nextOrderID;                      AST(PERSIST SUBSCRIBE)

    // The current set of open orders created by this player.  The array may be sparse(have some NULL entries) as entries are deleted 
    //  to avoid excessive database writes due to compaction.
    CONST_EARRAY_OF(CurrencyExchangeOpenOrder) openOrders;  AST(PERSIST SUBSCRIBE)

    // The offset in the logEntries array where the next log entry will be written
    const U32 logNext;                          AST(PERSIST SUBSCRIBE)

    // A circular log of currency exchange operations
    CONST_EARRAY_OF(CurrencyExchangeLogEntry) logEntries;   AST(PERSIST SUBSCRIBE)
} CurrencyExchangeAccountData;

AUTO_STRUCT;
typedef struct CurrencyExchangeConfig
{
    // Set this to true to turn on the Currency Exchange system
    bool enabled;

    // The maximum number of open orders a player can have at one time
    int maxPlayerOpenOrders;                AST(DEFAULT(5))

    // The number of buy and sell prices for open orders to display to the user
    int maxDisplayPrices;                   AST(DEFAULT(10))

    // How many days an order can be open before it is automatically withdrawn
    U32 orderExpireDays;

    // The number of entries in the player log array
    int maxPlayerLogEntries;

    // The maximum prices (in TC) for one MTC
    U32 maxMTCPrice;

    // The minimum price (in TC) for one MTC
    U32 minMTCPrice;

    // The maximum quantity on an order (in MTC).
    U32 maxQuantityPerOrder;

    // The minimum quantity on an order (in MTC).
    U32 minQuantityPerOrder;

    // The maximum number of times to retry 
    U32 maxMTCLockRetryCount;

    // The numeric that is the source and sink for TC on a player character
    char *tcNumeric;
} CurrencyExchangeConfig;

AUTO_STRUCT;
typedef struct CurrencyExchangePriceQuantity
{
	U32 price;
	U32 quantity;
} CurrencyExchangePriceQuantity;

AUTO_STRUCT;
typedef struct CurrencyExchangeGlobalUIData
{
	// Is the exchange enabled
	bool enabled;

	// The maximum prices (in TC) for one MTC
	U32 maxMTCPrice;

	// The minimum price (in TC) for one MTC
	U32 minMTCPrice;

	// The maximum number of open orders a player can have at one time
	int maxPlayerOpenOrders;

    // The maximum quantity on an order (in MTC).
    U32 maxQuantityPerOrder;

    // The minimum quantity on an order (in MTC).
    U32 minQuantityPerOrder;

	EARRAY_OF(CurrencyExchangePriceQuantity) sellPrices;
	EARRAY_OF(CurrencyExchangePriceQuantity) buyPrices;
} CurrencyExchangeGlobalUIData;

//
// This structure is used to keep track of subscriptions to global UI data updates.
// It is used by the auction server to keep track of gameserver subscriptions and by the
//  gameserver to keep track of client subscriptions.
AUTO_STRUCT;
typedef struct CurrencyExchangeSubscriptionState
{
	// use subscriberType << 32 | subscriberID as key
	S64 key;							AST(KEY)
	ContainerID subscriberID;
	GlobalType subscriberType;
	U32 lastUIDataRequestTime;
	U32 lastUIDataUpdateTime;
} CurrencyExchangeSubscriptionState;

//
// This struct is used to queue up requested operations from players.  Requests might be queued while waiting for
//  the CurrencyExchangeAccountData container to be created, or when requests are otherwise temporarily prevented
//  from being processed.
//
AUTO_STRUCT;
typedef struct CurrencyExchangeOperationRequest {
    CurrencyExchangeOperationType operationType;
    ContainerID accountID;
    ContainerID characterID;
    char *characterName;
    U32 orderID;
    CurrencyExchangeOrderType orderType;
    U32 quantity;
    U32 price;
    U32 arrivalTime;
    U32 mtcTransferID;

    const char *debugName;
    const char *gameSpecificLogInfo;

    // Used for some operations to temporarily point to the order
    CurrencyOrderInfo *orderInfo;       NO_AST
} CurrencyExchangeOperationRequest;

typedef void (*CurrencyExchangeSubscriptionUpdateFunc)(GlobalType subscriberType, ContainerID subscriberID);

extern CurrencyExchangeConfig gCurrencyExchangeConfig;

typedef struct NOCONST(CurrencyExchangeAccountData) NOCONST(CurrencyExchangeAccountData);

void CurrencyExchange_SetTransactionReturnOrderID(ATR_ARGS, U32 orderID);
void CurrencyExchange_SetTransactionReturnErrorEx(ATR_ARGS, CurrencyExchangeResultType resultType, const char *funcName, int lineNum, const char *logFormat, ...);
#define CurrencyExchange_SetTransactionReturnError(successStr, failStr, resultType, format, ...) CurrencyExchange_SetTransactionReturnErrorEx(successStr, failStr, resultType, __FUNCTION__, __LINE__, FORMAT_STRING_CHECKED(format), ##__VA_ARGS__)
bool CurrencyExchange_trh_WriteLogEntry(ATR_ARGS, ATH_ARG NOCONST(CurrencyExchangeAccountData) *currencyAccountData, CurrencyExchangeOperationType logType, 
    CurrencyExchangeOrderType orderType, U32 orderID, U32 quantityMTC, U32 quantityTC, U32 time, const char *characterName);
int CurrencyExchange_trh_GetAvailableOrderIndex(ATR_ARGS, ATH_ARG NOCONST(CurrencyExchangeAccountData) *currencyAccountData);

void CurrencyExchange_UpdateSubscribers(CurrencyExchangeSubscriptionUpdateFunc sendUpdateFunc, U32 sendInterval, U32 expireInterval);
void CurrencyExchange_AddSubscriber(GlobalType subscriberType, ContainerID subscriberID);
int	CurrencyExchange_NumSubscribers(void);

const char *CurrencyExchangeConfig_GetMtcReadyToClaimEscrowAccountKey(void);

void CurrencyExchange_SetTransactionReturnOrderID(ATR_ARGS, U32 orderID);
bool CurrencyExchange_trh_WriteLogEntry(ATR_ARGS, ATH_ARG NOCONST(CurrencyExchangeAccountData) *currencyAccountData, CurrencyExchangeOperationType logType, 
    CurrencyExchangeOrderType orderType, U32 orderID, U32 quantityMTC, U32 quantityTC, U32 time, const char *characterName);
void CurrencyExchange_SetTransactionReturnErrorEx(ATR_ARGS, CurrencyExchangeResultType resultType, const char *funcName, int lineNum, const char *logFormat, ...);
