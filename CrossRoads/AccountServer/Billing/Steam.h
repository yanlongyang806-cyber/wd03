#pragma once

#define STEAM_GETUSERINFO "GetUserInfo"
#define STEAM_INITTXN "InitTxn"
#define STEAM_QUERYTXN "QueryTxn"
#define STEAM_FINALIZETXN "FinalizeTxn"
#define STEAM_REFUNDTXN "RefundTxn"

typedef enum PurchaseResult PurchaseResult;
typedef struct TransactionItem TransactionItem;

typedef void (*SteamGetUserInfoCB)(bool bSuccess,
	SA_PARAM_OP_STR const char *pCountry,
	SA_PARAM_OP_STR const char *pState,
	SA_PARAM_OP_STR const char *pCurrency,
	SA_PARAM_OP_STR const char *pStatus,
	SA_PARAM_OP_VALID void *pUserData);

typedef void (*SteamInitTransactionCB)(PurchaseResult eResult,
	SA_PARAM_OP_VALID void *pUserData,
	SA_PARAM_OP_STR const char *pRedirectURL,
	U64 uOrderID,
	U64 uTransactionID);

typedef bool (*SteamTransactionCB)(PurchaseResult eResult,
	SA_PARAM_OP_VALID void *pUserData,
	U64 uOrderID,
	U64 uTransactionID);

typedef void (*SteamRefundTransactionCB)(bool bSuccess,
	SA_PARAM_OP_VALID void *pUserData,
	U64 uOrderID,
	U64 uTransactionID);

// Calls the Steam GetUserInfo API
// calls pCallback with contents upon success
// Otherwise returns false with no contents
void GetSteamUserInfo(U32 uAccountID,
	U64 uSteamID,
	SA_PARAM_NN_STR const char *pProxy,
	SA_PARAM_OP_STR const char *pIP,
	SA_PARAM_NN_VALID SteamGetUserInfoCB pCallback,
	SA_PARAM_OP_VALID void *pUserData);

// Calls the Steam InitTxn API
// calls pCallback with PURCHASE_RESULT_PENDING upon success
// Otherwise returns an appropriate error
PurchaseResult InitSteamTransaction(U32 uAccountID,
	U64 uSteamID,
	SA_PARAM_NN_STR const char *pProxy,
	SA_PARAM_OP_STR const char *pCategory,
	SA_PARAM_NN_STR const char *pCurrency,
	SA_PARAM_NN_STR const char *pLocCode,
	SA_PARAM_NN_VALID EARRAY_OF(const TransactionItem) eaItems,
	SA_PARAM_NN_STR const char *pIP,
	bool bWebPurchase,
	SA_PARAM_NN_VALID SteamInitTransactionCB pCallback,
	SA_PARAM_OP_VALID void *pUserData);

// Calls the Steam RefundTxn API
// calls pCallback with true/false depending on result
void RefundSteamTransaction(U32 uAccountID,
	U64 uOrderID,
	SA_PARAM_NN_STR const char *pProxy,
	SA_PARAM_NN_VALID SteamRefundTransactionCB pCallback,
	SA_PARAM_OP_VALID void *pUserData);

// Queues a transaction for QueryTxn/FinalizeTxn
// calls pCallback once before QueryTxn/FinalizeTxn request with PURCHASE_RESULT_PENDING
// calls pCallback upon a success/failure with an appropriate result
// Otherwise continues re-queueing until success
void AddOrderToSteamQueue(U32 uAccountID,
	U64 uOrderID,
	SA_PARAM_NN_STR const char *pProxy,
	SA_PARAM_NN_VALID SteamTransactionCB pCallback,
	SA_PARAM_OP_VALID void *pUserData);

// Processes the queue of transactions
void ProcessSteamQueue(void);