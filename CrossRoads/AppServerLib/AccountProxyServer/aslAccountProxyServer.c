/***************************************************************************
*     Copyright (c) 2009-2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslAccountProxyServer.h"

#include "aslAPAccountLocation.h"

#include "AccountDataCache.h"
#include "accountnet.h"
#include "Alerts.h"
#include "AppServerLib.h"
#include "AutoTransDefs.h"
#include "chatCommon.h"
#include "logging.h"
#include "MicroTransactions.h"
#include "Money.h"
#include "objContainer.h"
#include "objIndex.h"
#include "ShardCommon.h"
#include "StringUtil.h"
#include "StructNet.h"
#include "TimedCallback.h"
#include "utilitiesLib.h"
#include "wininclude.h"

#include "AutoGen/AccountDataCache_h_ast.h"
#include "accountnet_h_ast.h"
#include "aslAccountProxyServer_c_ast.h"
#include "GameAccountData_h_ast.h"

#include "autogen/AppServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"
#include "autogen/GameServerLib_autogen_RemoteFuncs.h"
#include "autogen/AppServerLib_autogen_RemoteFuncs.h"
#include "autogen/controller_autogen_remotefuncs.h"
#include "autogen/AppServerLib_autogen_SlowFuncs.h"

#define PACKET_PARAMS_PROTOTYPE SA_PARAM_NN_VALID Packet *pkt, int cmd, SA_PARAM_NN_VALID NetLink *link, SA_PARAM_OP_VALID void *userData
#define PACKET_PARAMS pkt, cmd, link, userData

static bool gbNoAccountServer = false;
AUTO_CMD_INT(gbNoAccountServer, NoAccountServer) ACMD_CMDLINE;

static int giKeyValueLockMax = 5000;
AUTO_CMD_INT(giKeyValueLockMax, KeyValueLockMax) ACMD_CMDLINE;

static int giKeyValueLockCreateRate = 250;
AUTO_CMD_INT(giKeyValueLockCreateRate, KeyValueLockCreateRate) ACMD_CMDLINE;

static bool gbRecruitSystemEnabled = true;
AUTO_CMD_INT(gbRecruitSystemEnabled, RecruitSystemEnabled) ACMD_CMDLINE;

static int giAccountServerAuthTimeout = MINUTES(5);
AUTO_CMD_INT(giAccountServerAuthTimeout, AccountServerAuthTimeout) ACMD_CMDLINE;

static int giLockCompletionTimeout = MINUTES(5);
AUTO_CMD_INT(giLockCompletionTimeout, LockCompletionTimeout) ACMD_CMDLINE;

static bool gbVerifyMTPrices = true;
AUTO_CMD_INT(gbVerifyMTPrices, VerifyMTPrices) ACMD_CMDLINE;

static bool gbAccountProxyLogging = true;
AUTO_CMD_INT(gbAccountProxyLogging, AccountProxyLogging) ACMD_CMDLINE;

static bool gbVerboseAccountProxyLogging = false;
AUTO_CMD_INT(gbVerboseAccountProxyLogging, VerboseAccountProxyLogging) ACMD_CMDLINE;

static bool gbMTKillSwitch = false;
static bool gbBillingKillSwitch = false;

static int giContainerCount = 0;
ObjectIndex *gidx_AccountProxyLocksByOrderID;

AUTO_STRUCT;
typedef struct SentSetRequest
{
	SlowRemoteCommandID iCmdID;
	U32 accountID;
	char *key; AST(ESTRING)
	ProxyRequestID requestID; AST(INT)
} SentSetRequest;

AUTO_STRUCT;
typedef struct SentMoveRequest
{
	SlowRemoteCommandID iCmdID;
	U32 uSrcAccountID;
	char *pSrcKey; AST(ESTRING)
	U32 uDestAccountID;
	char *pDestKey; AST(ESTRING)
	ProxyRequestID uRequestID; AST(INT)
} SentMoveRequest;

AUTO_STRUCT;
typedef struct SentDiscountRequest
{
	ProxyRequestID requestID; AST(INT KEY)
	char * pCategory;
	ContainerID containerID;
	GlobalType eType;		//Could be player or login server
	int requestType;
} SentDiscountRequest;

AUTO_STRUCT;
typedef struct TransactionSetHolder
{
	SlowRemoteCommandID iCmdID;
	AccountProxySetRequest *pSetRequest;
} TransactionSetHolder;

AUTO_STRUCT;
typedef struct TransactionMoveHolder
{
	SlowRemoteCommandID iCmdID;
	U32 uDestAccountID;
	AccountProxyMoveRequest *pMoveRequest;
} TransactionMoveHolder;

AUTO_STRUCT;
typedef struct SimpleSetHolder
{
	SlowRemoteCommandID iCmdID;
	AccountProxySimpleSetRequest *pSetRequest;
} SimpleSetHolder;

AUTO_STRUCT;
typedef struct AccountEntityMapping
{
	EntityRef entityRef;
	ContainerID server;
} AccountEntityMapping;

AUTO_STRUCT;
typedef struct KeyValueRequestHolder
{
	SlowRemoteCommandID iCmdID;
	U32 uAccountID;
} KeyValueRequestHolder;

AUTO_STRUCT;
typedef struct SubbedTimeRequestHolder
{
	SlowRemoteCommandID iCmdID;
	AccountProxySubbedTimeRequest *pRequest;
} SubbedTimeRequestHolder;

AUTO_STRUCT;
typedef struct LinkingStatusRequestHolder
{
	SlowRemoteCommandID iCmdID;
	U32 uAccountID;
} LinkingStatusRequestHolder;

AUTO_STRUCT;
typedef struct PlayedTimeRequestHolder
{
	SlowRemoteCommandID iCmdID;
	AccountProxyPlayedTimeRequest *pRequest;
} PlayedTimeRequestHolder;

AUTO_STRUCT;
typedef struct AccountDataRequestHolder
{
	SlowRemoteCommandID iCmdID;
	AccountProxyAccountDataRequest *pRequest;
} AccountDataRequestHolder;

AUTO_STRUCT;
typedef struct AccountPacket
{
	int iType;
	void *pData;	NO_AST
	ParseTable *pt; NO_AST
} AccountPacket;

AUTO_STRUCT;
typedef struct ConvertHolder
{
	SlowRemoteCommandID iCmdID;
	char *pDisplayName; AST(ESTRING)
} ConvertHolder;

AUTO_ENUM;
typedef enum enumAPRemoteCommand
{
	APCMD_SET_ALL_KEY_VALUES,
	APCMD_SET_KEY_VALUE,
	APCMD_REMOVE_KEY_VALUE,
	APCMD_STEAM_CAPTURE,
} enumAPRemoteCommand;

AUTO_STRUCT;
typedef struct PendingAPRemoteCommand
{
	U32 uAccountID;
	enumAPRemoteCommand eCmd;
	char *pKey;
	AccountProxyProductList *pProductList;
	AccountProxyKeyValueInfoList *pList;
	AccountProxyKeyValueInfo *pInfo;
	PurchaseResult eResult;
} PendingAPRemoteCommand;

// Global list of sent set requests (so that the response can be sent back to the game server)
static EARRAY_OF(SentSetRequest) gSentRequests = NULL;
static EARRAY_OF(SentMoveRequest) gSentMoveRequests = NULL;
static EARRAY_OF(SimpleSetHolder) gSentSimpleSetRequests = NULL;
static EARRAY_OF(KeyValueRequestHolder) gSentKeyValueRequests = NULL;
static EARRAY_OF(ConvertHolder) gConvertRequests = NULL;
static EARRAY_OF(SubbedTimeRequestHolder) gSubbedTimeRequests = NULL;
static EARRAY_OF(PlayedTimeRequestHolder) gPlayedTimeRequests = NULL; // I hate this crap
static EARRAY_OF(LinkingStatusRequestHolder) gLinkingStatusRequests = NULL;
static EARRAY_OF(AccountDataRequestHolder) gAccountDataRequests = NULL; // TODO VAS 110712 - Yeah, I'm removing this ASAP
static EARRAY_OF(SentDiscountRequest) gSentDiscountRequests = NULL;

// Frequency to try and clear the cache
static const U32 CacheClearFrequency = 50;
static const U32 CacheClearTime = 1800;
static const F32 ReconnectTimeout = 5;
static const U32 ProductCacheClearTime = 0;

// How long to wait for the protocol version before deciding it is an old account server (in seconds)
static const F32 ProtocolWaitTime = 30;

static NetLink *gAccountServerLink = NULL;

static U32 gProtocolVersion = 0;

static void EnsureAPContainersExist(void);
static void EnsureConnection(F32 fElapsed);
static void WalkLocks(bool bForce);
static bool ShouldSend(SA_PARAM_NN_VALID const AccountProxyLockContainer *apContainer, bool forceSend);
static void DestroyLock(ContainerID containerID);
static void SendResponseToGameServer(SA_PARAM_NN_VALID AccountProxySetResponse *response);
static void SendMoveResponseToGameServer(SA_PARAM_NN_VALID AccountProxyMoveResponse *response);
static void SendPackets(void);
static void AddAccountPacket(int iType, SA_PARAM_NN_VALID ParseTable * pt, SA_PRE_NN_NN_VALID SA_POST_NN_OP_VALID void **pData);

static void LogMessage(FORMAT_STR const char *pFormat, ...);
static void LogError(SA_PARAM_NN_STR const char *pKey, FORMAT_STR const char *pFormat, ...);

ContainerID GetUnusedLockContainerID(void);
static bool AccountServerLinkActive(void);

static void AddSetRequest(SA_PARAM_NN_VALID SentSetRequest *pRequest);

static void getProducts(SA_PARAM_NN_STR const char *pCategory, SA_PARAM_NN_VALID CONTAINERID_EARRAY *ppProductIDs);

static bool gContainerExists = false;

static EARRAY_OF(AccountPacket) gPktQueue = NULL;

static CONTAINERID_EARRAY geaLockIDsToWalk = NULL;
static CONTAINERID_EARRAY geaPendingRemovalIDs = NULL;
static CONTAINERID_EARRAY geaSentIDs = NULL;

static bool MicrotransactionsEnabled(void)
{
	if (!AccountServerLinkActive()) return false;
	return !(gbMTKillSwitch || g_eMicroTrans_ShardCategory == kMTShardCategory_Off);
}

/************************************************************************/
/* Transactions                                                         */
/************************************************************************/

AUTO_TRANSACTION
ATR_LOCKS(pData, ".Earecruits, .Earecruiters");
enumTransactionOutcome trUpdateRecruitInfo(ATR_ARGS, NOCONST(GameAccountData) *pData, RecruitInfo *pRecruitInfo)
{
	if (NONNULL(pData) && pRecruitInfo)
	{
		eaDestroyStructNoConst(&pData->eaRecruits, parse_Recruit);
		eaDestroyStructNoConst(&pData->eaRecruiters, parse_Recruiter);
		eaCopyStructsNoConst(&pRecruitInfo->eaRecruits, &pData->eaRecruits, parse_Recruit);
		eaCopyStructsNoConst(&pRecruitInfo->eaRecruiters, &pData->eaRecruiters, parse_Recruiter);
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pContainer, ".Plock");
enumTransactionOutcome AccountProxy_tr_RemoveLock(ATR_ARGS, NOCONST(AccountProxyLockContainer) *pContainer)
{
	if (NONNULL(pContainer))
	{
		if (pContainer->pLock)
		{
			StructDestroyNoConst(parse_AccountProxyLock, pContainer->pLock);
			pContainer->pLock = NULL;
		}
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
ATR_LOCKS(pContainer, ".Plock.Result, .Plock.Fdestroytime, .Plock.Plock, .Plock.Upurchaseid, .Plock.Porderid, .Plock.Requestid");
enumTransactionOutcome AccountProxy_tr_UpdateLock(ATR_ARGS, NOCONST(AccountProxyLockContainer) *pContainer,
												  const char *pLockPassword,
												  U32 uPurchaseID,
												  char *pOrderID,
												  U32 uRequestID)
{
	if (ISNULL(pContainer) || ISNULL(pContainer->pLock))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (pContainer->pLock->result == APRESULT_TIMED_OUT)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (pContainer->pLock->requestID != uRequestID)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	pContainer->pLock->uPurchaseID = uPurchaseID;
	pContainer->pLock->pOrderID = StructAllocString(pOrderID);

	if (pLockPassword)
	{
		estrCopy2(&pContainer->pLock->pLock, pLockPassword);
	}

	if (devassert(pContainer->pLock->result == APRESULT_PENDING_ACCOUNT_SERVER_AUTHORIZE))
	{
		pContainer->pLock->result = APRESULT_WAITING_FOR_COMPLETION;
		pContainer->pLock->fDestroyTime = timeSecondsSince2000() + giLockCompletionTimeout;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

static void AP_tr_UpdateLock_CB(SA_PARAM_NN_VALID TransactionReturnVal *pReturn, SA_PARAM_NN_VALID AccountProxySetResponse *pResponse)
{
	PERFINFO_AUTO_START_FUNC();

	if (pReturn->eOutcome == TRANSACTION_OUTCOME_FAILURE)
	{
		pResponse->result = AKV_FAILURE;
	}

	SendResponseToGameServer(pResponse);
	StructDestroy(parse_AccountProxySetResponse, pResponse);
	PERFINFO_AUTO_STOP_FUNC();
}

static void AP_tr_UpdateMoveLock_CB(SA_PARAM_NN_VALID TransactionReturnVal *pReturn, SA_PARAM_NN_VALID AccountProxyMoveResponse *pResponse)
{
	PERFINFO_AUTO_START_FUNC();

	if (pReturn->eOutcome == TRANSACTION_OUTCOME_FAILURE)
	{
		pResponse->eResult = AKV_FAILURE;
	}

	SendMoveResponseToGameServer(pResponse);
	StructDestroy(parse_AccountProxyMoveResponse, pResponse);
	PERFINFO_AUTO_STOP();
}

AUTO_TRANSACTION
ATR_LOCKS(pContainer, ".Plock");
enumTransactionOutcome AccountProxy_tr_CreateLock(ATR_ARGS, NOCONST(AccountProxyLockContainer) *pContainer, U32 accountID, const char *key, int eType, int requestID, int iCmdID)
{
	if (!accountID || ISNULL(pContainer))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Can't reuse a lock that's already being used!
	if (NONNULL(pContainer->pLock))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	pContainer->pLock = StructCreateNoConst(parse_AccountProxyLock);

	pContainer->pLock->uAccountID = accountID;
	pContainer->pLock->fDestroyTime = timeSecondsSince2000() + giAccountServerAuthTimeout;
	pContainer->pLock->result = APRESULT_PENDING_ACCOUNT_SERVER_AUTHORIZE;
	pContainer->pLock->activityType = eType;
	pContainer->pLock->requestID = requestID;
	pContainer->pLock->iCmdID = iCmdID;
	estrCopy2(&pContainer->pLock->pKey, key);

	return TRANSACTION_OUTCOME_SUCCESS;
}

void aslAPSendLockRequestFailure(SlowRemoteCommandID iCmdID, U32 uAccountID, SA_PARAM_NN_STR const char *pKey)
{
	AccountProxySetResponse retVal = {0};

	PERFINFO_AUTO_START_FUNC();

	retVal.uAccountID = uAccountID;
	estrCopy2(&retVal.pKey, pKey);
	estrCopy2(&retVal.pLock, "");
	retVal.result = AKV_FAILURE;

	SlowRemoteCommandReturn_aslAPCmdSendLockRequest(iCmdID, &retVal);

	StructDeInit(parse_AccountProxySetResponse, &retVal);

	PERFINFO_AUTO_STOP_FUNC();
}

static void AP_tr_CreateLock_CB(SA_PARAM_NN_VALID TransactionReturnVal *pReturn, SA_PARAM_NN_VALID TransactionSetHolder *holder)
{
	PERFINFO_AUTO_START_FUNC();

	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		SentSetRequest *sent = StructCreate(parse_SentSetRequest);

		sent->accountID = holder->pSetRequest->uAccountID;
		sent->iCmdID = holder->iCmdID;
		sent->requestID = holder->pSetRequest->requestID;
		estrCopy2(&sent->key, holder->pSetRequest->pKey);
		AddSetRequest(sent);
		eaiPush(&geaLockIDsToWalk, holder->pSetRequest->containerID);

		if (gbVerboseAccountProxyLogging)
		{
			SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "KeyLockCreated",
				("requestid", "%u", holder->pSetRequest->requestID)
				("cmdid", "%u", holder->iCmdID)
				("accountid", "%u", holder->pSetRequest->uAccountID)
				("key", "%s", holder->pSetRequest->pKey)
				("value", "%"FORM_LL"d", holder->pSetRequest->iValue)
				("change", "%d", holder->pSetRequest->operation == AKV_OP_INCREMENT ? 1 : 0)
				("lockid", "%u", holder->pSetRequest->containerID));
		}

		// Send packet.
		AddAccountPacket(TO_ACCOUNTSERVER_PROXY_SET, parse_AccountProxySetRequest, &holder->pSetRequest);
	}
	else
	{
		aslAPSendLockRequestFailure(holder->iCmdID, holder->pSetRequest->uAccountID, holder->pSetRequest->pKey);
	}

	StructDestroy(parse_TransactionSetHolder, holder);

	PERFINFO_AUTO_STOP_FUNC();
}

AUTO_TRANSACTION
ATR_LOCKS(pContainer, ".Plock");
enumTransactionOutcome AccountProxy_tr_CreateMoveLock(ATR_ARGS, NOCONST(AccountProxyLockContainer) *pContainer, U32 uSrcAccountID, const char *pSrcKey, U32 uDestAccountID, const char *pDestKey, int requestID, int iCmdID)
{
	if (!uSrcAccountID || !uDestAccountID || ISNULL(pContainer))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Can't reuse a lock that's already being used!
	if (NONNULL(pContainer->pLock))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	pContainer->pLock = StructCreateNoConst(parse_AccountProxyLock);

	pContainer->pLock->uAccountID = uSrcAccountID;
	estrCopy2(&pContainer->pLock->pKey, pSrcKey);

	pContainer->pLock->uDestAccountID = uDestAccountID;
	estrCopy2(&pContainer->pLock->pDestKey, pDestKey);

	pContainer->pLock->fDestroyTime = timeSecondsSince2000() + giAccountServerAuthTimeout;
	pContainer->pLock->result = APRESULT_PENDING_ACCOUNT_SERVER_AUTHORIZE;
	pContainer->pLock->activityType = APACTIVITY_MOVE;
	pContainer->pLock->requestID = requestID;
	pContainer->pLock->iCmdID = iCmdID;

	return TRANSACTION_OUTCOME_SUCCESS;
}

void aslAPSendMoveRequestFailure(SlowRemoteCommandID iCmdID)
{
	AccountProxyMoveResponse retVal = {0};

	PERFINFO_AUTO_START_FUNC();

	estrCopy2(&retVal.pLock, "");
	retVal.eResult = AKV_FAILURE;

	SlowRemoteCommandReturn_aslAPCmdMoveKeyValue(iCmdID, &retVal);
	StructDeInit(parse_AccountProxyMoveResponse, &retVal);

	PERFINFO_AUTO_STOP();
}

static void AP_tr_CreateMoveLock_CB(SA_PARAM_NN_VALID TransactionReturnVal *pReturn, SA_PARAM_NN_VALID TransactionMoveHolder *pHolder)
{
	PERFINFO_AUTO_START_FUNC();

	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		SentMoveRequest *pSentRequest = StructCreate(parse_SentMoveRequest);

		pSentRequest->uSrcAccountID = pHolder->pMoveRequest->uSrcAccountID;
		pSentRequest->uDestAccountID = pHolder->pMoveRequest->uDestAccountID;
		estrCopy(&pSentRequest->pSrcKey, &pHolder->pMoveRequest->pSrcKey);
		estrCopy(&pSentRequest->pDestKey, &pHolder->pMoveRequest->pDestKey);
		pSentRequest->iCmdID = pHolder->iCmdID;
		pSentRequest->uRequestID = pHolder->pMoveRequest->uRequestID;
		eaPush(&gSentMoveRequests, pSentRequest);
		eaiPush(&geaLockIDsToWalk, pHolder->pMoveRequest->uLockContainerID);

		if (gbVerboseAccountProxyLogging)
		{
			SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "KeyMoveCreated",
				("requestid", "%u", pHolder->pMoveRequest->uRequestID)
				("cmdid", "%u", pHolder->iCmdID)
				("srcaccountid", "%u", pHolder->pMoveRequest->uSrcAccountID)
				("srckey", "%s", pHolder->pMoveRequest->pSrcKey)
				("destaccountid", "%u", pHolder->pMoveRequest->uDestAccountID)
				("destkey", "%s", pHolder->pMoveRequest->pDestKey)
				("amount", "%"FORM_LL"d", pHolder->pMoveRequest->iValue)
				("lockid", "%u", pHolder->pMoveRequest->uLockContainerID));
		}

		// Send packet.
		AddAccountPacket(TO_ACCOUNTSERVER_PROXY_MOVE, parse_AccountProxyMoveRequest, &pHolder->pMoveRequest);
	}
	else
	{
		aslAPSendMoveRequestFailure(pHolder->iCmdID);
	}

	StructDestroy(parse_TransactionMoveHolder, pHolder);

	PERFINFO_AUTO_STOP_FUNC();
}

AUTO_TRANSACTION
ATR_LOCKS(pContainer, ".Plock.Fdestroytime, .Plock.Result");
enumTransactionOutcome AccountProxy_tr_TimeoutLock(ATR_ARGS, NOCONST(AccountProxyLockContainer) *pContainer)
{
	U32 now = timeSecondsSince2000();

	if (ISNULL(pContainer) || ISNULL(pContainer->pLock))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Don't time out if the lock was finished before now!
	if (pContainer->pLock->result == APRESULT_COMMIT || pContainer->pLock->result == APRESULT_ROLLBACK)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (pContainer->pLock->fDestroyTime && now > pContainer->pLock->fDestroyTime)
	{
		pContainer->pLock->result = APRESULT_TIMED_OUT;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

/************************************************************************/
/* Public functions                                                     */
/************************************************************************/

AUTO_STARTUP(AccountProxyServer) ASTRT_DEPS(AS_Messages);
void aslAccountProxyServerStartup(void)
{
	MicroTrans_ConfigLoad();
}

void aslAPSetMTKillSwitch(int iActive)
{
	gbMTKillSwitch = iActive;
}

void aslAPSetBillingKillSwitch(int iActive)
{
	gbBillingKillSwitch = iActive;
}

AUTO_COMMAND_REMOTE;
bool aslAPCmdMicrotransEnabled(void)
{
    return ((!gbNoAccountServer) && AccountServerLinkActive() && MicrotransactionsEnabled());
}

// Initiates a request to create an authentication ticket for the given account and 
// returns the random number identifying the ticket as the slow remote command return value.
void aslAPCreateTicketForOnlineAccount(U32 iAccountID, U32 iIp, SlowRemoteCommandID iCmdID)
{
	AccountProxyRequestAuthTicketForOnlinePlayer *pRequest;

	PERFINFO_AUTO_START_FUNC();	

	// Make sure we have a valid account ID and an IP address. Also make sure there is an account server.
	if (iAccountID == 0 || iIp == 0 || gbNoAccountServer)
	{
		SlowRemoteCommandReturn_aslAPCmdCreateTicketForOnlineAccount(iCmdID, 0);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Create the request data
	pRequest = StructCreate(parse_AccountProxyRequestAuthTicketForOnlinePlayer);
	pRequest->uAccountID = iAccountID;
	pRequest->uIp = iIp;
	pRequest->iCmdID = iCmdID;

	// Send the packet
	AddAccountPacket(TO_ACCOUNTSERVER_PROXY_CREATE_TICKET_FOR_ONLINE_ACCOUNT, parse_AccountProxyRequestAuthTicketForOnlinePlayer, &pRequest);

	PERFINFO_AUTO_STOP_FUNC();
}

const AccountProxyProductList *aslAPRequestProductList(U32 uTimestamp, U32 uVersion)
{
	static AccountProxyProductList list = {0};
	const AccountProxyProductList *pList = NULL;
	bool bNeededUpdate = false;

	if (!ADCGetProductCacheTime() || ADCProductsAreUpToDate(uTimestamp, uVersion))
	{
		list.uTimestamp = uTimestamp;
		list.uVersion = uVersion;
		pList = &list;
	}
	else
	{
		pList = ADCGetProducts();
		bNeededUpdate = true;
	}

	if (gbAccountProxyLogging)
	{
		SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "RequestProductList", ("neededupdate", "%d", bNeededUpdate));
	}

	return pList;
}

const AccountProxyDiscountList *aslAPRequestDiscountList(U32 uTimestamp, U32 uVersion)
{
	static AccountProxyDiscountList list = {0};
	const AccountProxyDiscountList *pList = NULL;
	bool bNeededUpdate = false;

	if (!ADCGetDiscountCacheTime() || ADCDiscountsAreUpToDate(uTimestamp, uVersion))
	{
		list.uTimestamp = uTimestamp;
		list.uVersion = uVersion;
		pList = &list;
	}
	else
	{
		pList = ADCGetDiscounts();
		bNeededUpdate = true;
	}

	if (gbAccountProxyLogging)
	{
		SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "RequestDiscountList", ("neededupdate", "%d", bNeededUpdate));
	}

	return pList;
}

void aslAPSendLockRequest(SlowRemoteCommandID iCmdID, U32 uAccountID, SA_PARAM_NN_STR const char *key, S64 iValue, AccountProxySetOperation operation)
{
	TransactionSetHolder *holder;

	PERFINFO_AUTO_START_FUNC();

	if (gbNoAccountServer || !AccountServerLinkActive() || !uAccountID)
    {
        aslAPSendLockRequestFailure(iCmdID, uAccountID, key);
        PERFINFO_AUTO_STOP();
        return;
    }

	holder = StructCreate(parse_TransactionSetHolder);
	holder->pSetRequest = StructCreate(parse_AccountProxySetRequest);

	holder->iCmdID = iCmdID;
	holder->pSetRequest->uAccountID = uAccountID;
	estrCopy2(&holder->pSetRequest->pKey, AccountProxySubstituteKeyTokens(key, AccountGetShardProxyName(), ShardCommon_GetClusterName(), microtrans_GetShardEnvironmentName()));
	estrPrintf(&holder->pSetRequest->pValue, "%"FORM_LL"d", iValue); // TODO VAS 111212 - Remove this once AS and all shards have the new code
	holder->pSetRequest->iValue = iValue;
	holder->pSetRequest->operation = operation;
	holder->pSetRequest->requestID = getNewProxyRequestID();
	holder->pSetRequest->pProductInternalName = strdup(GetProductName());
	estrCopy2(&holder->pSetRequest->pProxy, AccountGetShardProxyName());

	holder->pSetRequest->containerID = GetUnusedLockContainerID();

	if (gbAccountProxyLogging)
	{
		SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "KeyLock",
			("requestid", "%u", holder->pSetRequest->requestID)
			("cmdid", "%u", iCmdID)
			("accountid", "%u", uAccountID)
			("key", "%s", holder->pSetRequest->pKey)
			("value", "%"FORM_LL"d", iValue)
			("change", "%d", operation == AKV_OP_INCREMENT ? 1 : 0)
			("lockid", "%u", holder->pSetRequest->containerID));
	}

	if (!holder->pSetRequest->containerID)
	{
		aslAPSendLockRequestFailure(holder->iCmdID, uAccountID, holder->pSetRequest->pKey);
		StructDestroy(parse_TransactionSetHolder, holder);
		PERFINFO_AUTO_STOP();
		return;
	}

	AutoTrans_AccountProxy_tr_CreateLock(objCreateManagedReturnVal(AP_tr_CreateLock_CB, holder),
		GLOBALTYPE_ACCOUNTPROXYSERVER, GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, holder->pSetRequest->containerID,
		uAccountID, holder->pSetRequest->pKey,
		APACTIVITY_KEYVALUE, holder->pSetRequest->requestID, holder->iCmdID);
	PERFINFO_AUTO_STOP();
}

void aslAPMoveKeyValue(SlowRemoteCommandID iCmdID, U32 uSrcAccountID, const char *pSrcKey, U32 uDestAccountID, const char *pDestKey, S64 iValue)
{
	TransactionMoveHolder *pHolder = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (gbNoAccountServer || !AccountServerLinkActive() || !MicrotransactionsEnabled() || !uSrcAccountID || !uDestAccountID)
	{
		aslAPSendMoveRequestFailure(iCmdID);
		PERFINFO_AUTO_STOP();
		return;
	}

	pHolder = StructCreate(parse_TransactionMoveHolder);
	pHolder->pMoveRequest = StructCreate(parse_AccountProxyMoveRequest);

	pHolder->iCmdID = iCmdID;
	pHolder->pMoveRequest->uSrcAccountID = uSrcAccountID;
	pHolder->pMoveRequest->uDestAccountID = uDestAccountID;
	estrCopy2(&pHolder->pMoveRequest->pSrcKey, pSrcKey);
	estrCopy2(&pHolder->pMoveRequest->pDestKey, pDestKey);
	pHolder->pMoveRequest->iValue = iValue;
	estrCopy2(&pHolder->pMoveRequest->pProxy, AccountGetShardProxyName());
	pHolder->pMoveRequest->uRequestID = getNewProxyRequestID();
	
	pHolder->pMoveRequest->uLockContainerID = GetUnusedLockContainerID();

	if (gbAccountProxyLogging)
	{
		SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "KeyMove",
			("requestid", "%u", pHolder->pMoveRequest->uRequestID)
			("cmdid", "%u", iCmdID)
			("srcaccountid", "%u", uSrcAccountID)
			("srckey", "%s", pSrcKey)
			("destaccountid", "%u", uDestAccountID)
			("destkey", "%s", pDestKey)
			("amount", "%"FORM_LL"d", iValue)
			("lockid", "%u", pHolder->pMoveRequest->uLockContainerID));
	}

	if (!pHolder->pMoveRequest->uLockContainerID)
	{
		aslAPSendMoveRequestFailure(iCmdID);
		StructDestroy(parse_TransactionMoveHolder, pHolder);
		PERFINFO_AUTO_STOP();
		return;
	}

	AutoTrans_AccountProxy_tr_CreateMoveLock(objCreateManagedReturnVal(AP_tr_CreateMoveLock_CB, pHolder),
		GLOBALTYPE_ACCOUNTPROXYSERVER, GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, pHolder->pMoveRequest->uLockContainerID,
		pHolder->pMoveRequest->uSrcAccountID, pHolder->pMoveRequest->pSrcKey, pHolder->pMoveRequest->uDestAccountID,
		pHolder->pMoveRequest->pDestKey, pHolder->pMoveRequest->uRequestID, pHolder->iCmdID);
	PERFINFO_AUTO_STOP();
}

void aslAPSetKeyValue(SlowRemoteCommandID iCmdID, U32 uAccountID, SA_PARAM_NN_STR const char *key, S64 iValue, AccountProxySetOperation operation)
{
	SimpleSetHolder *holder = NULL;
	AccountProxySimpleSetRequest *pRequest = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (gbNoAccountServer || !uAccountID)
	{
		SlowRemoteCommandReturn_aslAPCmdSetKeyValue(iCmdID, AKV_FAILURE);
		PERFINFO_AUTO_STOP();
		return;
	}

	holder = StructCreate(parse_SimpleSetHolder);
	holder->pSetRequest = StructCreate(parse_AccountProxySimpleSetRequest);

	holder->iCmdID = iCmdID;
	holder->pSetRequest->uAccountID = uAccountID;
	estrCopy2(&holder->pSetRequest->pKey, AccountProxySubstituteKeyTokens(key, AccountGetShardProxyName(), ShardCommon_GetClusterName(), microtrans_GetShardEnvironmentName()));
	estrPrintf(&holder->pSetRequest->pValue, "%"FORM_LL"d", iValue);
	holder->pSetRequest->iValue = iValue;
	holder->pSetRequest->operation = operation;
	holder->pSetRequest->requestID = getNewProxyRequestID();
	estrCopy2(&holder->pSetRequest->pProxy, AccountGetShardProxyName());

	if (gbAccountProxyLogging)
	{
		SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "KeySet",
			("requestid", "%u", holder->pSetRequest->requestID)
			("cmdid", "%u", iCmdID)
			("accountid", "%u", uAccountID)
			("key", "%s", holder->pSetRequest->pKey)
			("value", "%"FORM_LL"d", iValue)
			("change", "%d", operation == AKV_OP_INCREMENT ? 1 : 0));
	}

	eaPush(&gSentSimpleSetRequests, holder);
	pRequest = StructClone(parse_AccountProxySimpleSetRequest, holder->pSetRequest);
	AddAccountPacket(TO_ACCOUNTSERVER_PROXY_SIMPLE_SET, parse_AccountProxySimpleSetRequest, &pRequest);

	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND_REMOTE_SLOW(S64);
void aslAPCmdRequestKeyValue(SlowRemoteCommandID iCmdID, U32 accountID, const char *key)
{
	AccountProxyGetRequest *request;

	if (gbNoAccountServer) return;

	if (!accountID)
	{
		AssertOrAlert("ACCOUNTPROXY_INVALID_ACCOUNT", "Invalid account ID (0) passed to aslAPRequestKeyValue.");
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	request = StructCreate(parse_AccountProxyGetRequest);
	request->uAccountID = accountID;
	request->pKey = estrDup(AccountProxySubstituteKeyTokens(key, AccountGetShardProxyName(), ShardCommon_GetClusterName(), microtrans_GetShardEnvironmentName()));
	request->iCmdID = iCmdID;
	request->pProxy = StructAllocString(AccountGetShardProxyName());

	if (gbVerboseAccountProxyLogging)
	{
		SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "KeyGet",
			("accountid", "%u", accountID)
			("key", "%s", request->pKey));
	}

	AddAccountPacket(TO_ACCOUNTSERVER_PROXY_GET, parse_AccountProxyGetRequest, &request);

	PERFINFO_AUTO_STOP();
}

void aslAPRequestAllKeyValues(U32 accountID, U32 playerID)
{
	AccountProxyRequestAllKeyValues *request;
	
	if (gbNoAccountServer) return;

	if (!accountID)
	{
		AssertOrAlert("ACCOUNTPROXY_INVALID_ACCOUNT", "Invalid account ID (0) passed to aslAPRequestAllKeyValues.");
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	request = StructCreate(parse_AccountProxyRequestAllKeyValues);
	request->uAccountID = accountID;
	request->uPlayerID = playerID;

	if (gbVerboseAccountProxyLogging)
	{
		SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "KeyRequestAll",
			("accountid", "%u", accountID)
			("playerid", "%u", playerID));
	}

	AddAccountPacket(TO_ACCOUNTSERVER_PROXY_REQUEST_ALL_KEY_VALUES, parse_AccountProxyRequestAllKeyValues, &request);

	PERFINFO_AUTO_STOP_FUNC();
}

SA_RET_OP_VALID AccountProxyProduct *aslAPGetProductByID(SA_PARAM_NN_STR const char *pCategory, U32 uProductID)
{
	const AccountProxyProductList *list = NULL;

	PERFINFO_AUTO_START_FUNC();
	
	if (list = ADCGetProductsByCategory(pCategory))
	{
		EARRAY_CONST_FOREACH_BEGIN(list->ppList, i, s);
		{
			if (list->ppList[i]->uID == uProductID)
			{
				AccountProxyProduct *pRet = StructClone(parse_AccountProxyProduct, list->ppList[i]);

				PERFINFO_AUTO_STOP_FUNC();

				return pRet;
			}
		}
		EARRAY_FOREACH_END;
	}

	PERFINFO_AUTO_STOP_FUNC();
	return NULL;
}

//Returns an account proxy product by name.  The product is cloned!
AccountProxyProduct *aslAPGetProductByName(const char *pCategory, const char *pProductName)
{
	const AccountProxyProductList *list = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (list = ADCGetProductsByCategory(pCategory))
	{
		EARRAY_CONST_FOREACH_BEGIN(list->ppList, i, s);
		{
			if (stricmp(list->ppList[i]->pName, pProductName) == 0)
			{
				PERFINFO_AUTO_STOP_FUNC();

				return list->ppList[i];
			}
		}
		EARRAY_FOREACH_END;
	}

	PERFINFO_AUTO_STOP_FUNC();
	return NULL;
}

void aslAPGetAllKeyValues(SlowRemoteCommandID iCmdID, U32 accountID, U32 playerID)
{
	AccountProxyRequestAllKeyValues *request;
	KeyValueRequestHolder *holder;

	PERFINFO_AUTO_START_FUNC();

	if (gbNoAccountServer)
	{
		AccountProxyKeyValueInfoList retVal = {0};
		StructInit(parse_AccountProxyKeyValueInfoList, &retVal);
		SlowRemoteCommandReturn_aslAPCmdGetAllKeyValues(iCmdID, &retVal);
		StructDeInit(parse_AccountProxyKeyValueInfoList, &retVal);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	if (!accountID)
	{
		SlowRemoteCommandReturn_aslAPCmdGetAllKeyValues(iCmdID, NULL);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	request = StructCreate(parse_AccountProxyRequestAllKeyValues);

	holder = StructCreate(parse_KeyValueRequestHolder);
	holder->iCmdID = iCmdID;
	holder->uAccountID = accountID;
	eaPush(&gSentKeyValueRequests, holder);

	request->uAccountID = accountID;
	request->uPlayerID = playerID;

	if (gbVerboseAccountProxyLogging)
	{
		SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "KeyGetAll",
			("accountid", "%u", accountID)
			("playerid", "%u", playerID));
	}

	AddAccountPacket(TO_ACCOUNTSERVER_PROXY_REQUEST_ALL_KEY_VALUES, parse_AccountProxyRequestAllKeyValues, &request);
	PERFINFO_AUTO_STOP_FUNC();
}

void aslAPGetSubbedTime(SlowRemoteCommandID iCmdID, U32 accountID, SA_PARAM_OP_STR const char *pProductInternalName)
{
	AccountProxySubbedTimeRequest *pRequest = NULL;
	SubbedTimeRequestHolder *pHolder = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Make sure we have an account server and account ID
	if (gbNoAccountServer || !accountID)
	{
		SlowRemoteCommandReturn_aslAPCmdGetSubbedTime(iCmdID, 0);
		PERFINFO_AUTO_STOP();
	}

	// Make sure the product internal name is populated
	if (!pProductInternalName || !*pProductInternalName)
		pProductInternalName = GetProductName();

	pRequest = StructCreate(parse_AccountProxySubbedTimeRequest);
	pRequest->pProductInternalName = strdup(pProductInternalName);
	pRequest->uAccountID = accountID;

	pHolder = StructCreate(parse_SubbedTimeRequestHolder);
	pHolder->iCmdID = iCmdID;
	pHolder->pRequest = StructClone(parse_AccountProxySubbedTimeRequest, pRequest);

	eaPush(&gSubbedTimeRequests, pHolder);
	AddAccountPacket(TO_ACCOUNTSERVER_PROXY_GET_SUBBED_TIME, parse_AccountProxySubbedTimeRequest, &pRequest);
	PERFINFO_AUTO_STOP();
}

void aslAPGetAccountPlayedTime(SlowRemoteCommandID iCmdID, U32 uAccountID, const char *pProduct, const char *pCategory)
{
	AccountProxyPlayedTimeRequest *pRequest = NULL;
	PlayedTimeRequestHolder *pHolder = NULL;

	if (gbNoAccountServer || !uAccountID)
	{
		SlowRemoteCommandReturn_aslAPCmdGetAccountPlayedTime(iCmdID, NULL);
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	pRequest = StructCreate(parse_AccountProxyPlayedTimeRequest);
	pRequest->uAccountID = uAccountID;
	pRequest->pProduct = strdup(pProduct);
	pRequest->pCategory = strdup(pCategory);

	pHolder = StructCreate(parse_PlayedTimeRequestHolder);
	pHolder->iCmdID	= iCmdID;
	pHolder->pRequest = StructClone(parse_AccountProxyPlayedTimeRequest, pRequest);
	eaPush(&gPlayedTimeRequests, pHolder);

	AddAccountPacket(TO_ACCOUNTSERVER_PROXY_PLAYED_TIME, parse_AccountProxyPlayedTimeRequest, &pRequest);
	PERFINFO_AUTO_STOP();
}

void aslAPGetAccountLinkingStatus(SlowRemoteCommandID iCmdID, U32 accountID)
{
	AccountProxyLinkingStatusRequest *pRequest = NULL;
	LinkingStatusRequestHolder *pHolder = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (gbNoAccountServer || !accountID)
	{
		SlowRemoteCommandReturn_aslAPCmdGetAccountLinkingStatus(iCmdID, NULL);
		PERFINFO_AUTO_STOP();
		return;
	}

	pRequest = StructCreate(parse_AccountProxyLinkingStatusRequest);
	pRequest->uAccountID = accountID;

	pHolder = StructCreate(parse_LinkingStatusRequestHolder);
	pHolder->iCmdID = iCmdID;
	pHolder->uAccountID = accountID;

	eaPush(&gLinkingStatusRequests, pHolder);
	AddAccountPacket(TO_ACCOUNTSERVER_PROXY_GET_LINKING_STATUS, parse_AccountProxyLinkingStatusRequest, &pRequest);
	PERFINFO_AUTO_STOP();
}

void aslAPGetAccountData(SlowRemoteCommandID iCmdID, U32 uAccountID, const char *pProduct, const char *pCategory, const char *pShard, const char *pCluster)
{
	AccountProxyAccountDataRequest *pRequest = NULL;
	AccountDataRequestHolder *pHolder = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (gbNoAccountServer || !uAccountID)
	{
		SlowRemoteCommandReturn_aslAPCmdGetAccountData(iCmdID, NULL);
		PERFINFO_AUTO_STOP();
		return;
	}

	pRequest = StructCreate(parse_AccountProxyAccountDataRequest);
	pRequest->uAccountID = uAccountID;
	pRequest->pProduct = StructAllocString(pProduct);
	pRequest->pCategory = StructAllocString(pCategory);
	pRequest->pShard = StructAllocString(pShard);
	pRequest->pCluster = StructAllocString(pCluster);

	pHolder = StructCreate(parse_AccountDataRequestHolder);
	pHolder->iCmdID = iCmdID;
	pHolder->pRequest = StructClone(parse_AccountProxyAccountDataRequest, pRequest);

	if (gbVerboseAccountProxyLogging)
	{
		SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "AccountDataGet", ("accountid", "%u", uAccountID));
	}

	eaPush(&gAccountDataRequests, pHolder);
	AddAccountPacket(TO_ACCOUNTSERVER_PROXY_ACCOUNT_DATA, parse_AccountProxyAccountDataRequest, &pRequest);
	PERFINFO_AUTO_STOP();
}

void aslAPRequestRecruitInfo(U32 uAccountID)
{
	RequestRecruitInfo *request;

	PERFINFO_AUTO_START_FUNC();
	request = StructCreate(parse_RequestRecruitInfo);
	request->uAccountID = uAccountID;
	AddAccountPacket(TO_ACCOUNTSERVER_PROXY_REQUEST_RECRUIT_INFO, parse_RequestRecruitInfo, &request);
	PERFINFO_AUTO_STOP_FUNC();
}

void aslAPGetAccountIDFromDisplayName(SlowRemoteCommandID iCmdID, SA_PARAM_NN_STR const char *pDisplayName)
{
	ConvertHolder *holder = NULL;
	AccountProxyAccountIDFromDisplayNameRequest *pRequest = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (!pDisplayName || !pDisplayName[0] || gbNoAccountServer)
	{
		SlowRemoteCommandReturn_aslAPCmdGetAccountIDFromDisplayName(iCmdID, 0);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	holder = StructCreate(parse_ConvertHolder);
	holder->iCmdID = iCmdID;
	estrCopy2(&holder->pDisplayName, pDisplayName);
	eaPush(&gConvertRequests, holder);

	pRequest = StructCreate(parse_AccountProxyAccountIDFromDisplayNameRequest);
	estrCopy2(&pRequest->pDisplayName, pDisplayName);

	if (gbVerboseAccountProxyLogging)
	{
		SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "AccountIDFromDisplayName", ("display", "%s", pDisplayName));
	}

	AddAccountPacket(TO_ACCOUNTSERVER_PROXY_REQUEST_ACCOUNT_ID_BY_DISPLAY_NAME, parse_AccountProxyAccountIDFromDisplayNameRequest, &pRequest);
	PERFINFO_AUTO_STOP();
}

void aslAPSetNumCharacters(U32 uAccountID, U32 uNumCharacters, bool bChange)
{
	AccountProxyNumCharactersRequest *pRequest = NULL;

	if (gbNoAccountServer) return;

	PERFINFO_AUTO_START_FUNC();
	pRequest = StructCreate(parse_AccountProxyNumCharactersRequest);
	pRequest->uAccountID = uAccountID;
	pRequest->uNumCharacters = uNumCharacters;
	pRequest->bChange = bChange;
	pRequest->pProduct = StructAllocString(GetProductName());
	pRequest->pCategory = StructAllocString(GetShardCategoryFromShardInfoString());

	if (gbVerboseAccountProxyLogging)
	{
		SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "CharactersSet",
			("accountid", "%u", uAccountID)
			("characters", "%d", uNumCharacters)
			("change", "%d", bChange));
	}

	AddAccountPacket(TO_ACCOUNTSERVER_PROXY_NUM_CHARACTERS, parse_AccountProxyNumCharactersRequest, &pRequest);
	PERFINFO_AUTO_STOP();
}

void aslAPLogoutNotification(SA_PARAM_NN_VALID const AccountLogoutNotification *pLogout)
{
	AccountLogoutNotification *pLogoutClone;
	
	if (gbNoAccountServer) return;

	PERFINFO_AUTO_START_FUNC();
	pLogoutClone = StructClone(parse_AccountLogoutNotification, pLogout);
	AddAccountPacket(TO_ACCOUNTSERVER_PROXY_LOGOUT_NOTIFICATION, parse_AccountLogoutNotification, &pLogoutClone);
	PERFINFO_AUTO_STOP_FUNC();
}

AUTO_COMMAND_REMOTE_SLOW(PaymentMethodsResponse *);
void aslAPCmdRequestPaymentMethods(SlowRemoteCommandID iCmdID, U32 uAccountID, U64 uSteamID)
{
	PaymentMethodsRequest *pRequest = NULL;

	if (gbNoAccountServer || !MicrotransactionsEnabled() || gbBillingKillSwitch)
	{
		SlowRemoteCommandReturn_aslAPCmdRequestPaymentMethods(iCmdID, NULL);
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	pRequest = StructCreate(parse_PaymentMethodsRequest);
	pRequest->iCmdID = iCmdID;
	pRequest->uAccountID = uAccountID;
	pRequest->uSteamID = uSteamID;
	pRequest->pProxy = StructAllocString(AccountGetShardProxyName());
	AddAccountPacket(TO_ACCOUNTSERVER_PROXY_PAYMENT_METHODS, parse_PaymentMethodsRequest, &pRequest);

	PERFINFO_AUTO_STOP_FUNC();
}

static void aslAPCmdCaptureOnly_LockFinished(SA_PARAM_NN_VALID TransactionReturnVal *pReturn, SA_PARAM_NN_VALID void *pUserData)
{
	SlowRemoteCommandID iCmdID = (SlowRemoteCommandID)((intptr_t)pUserData);
	SlowRemoteCommandReturn_aslAPCmdCaptureOnly(iCmdID, pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS);
}

AUTO_COMMAND_REMOTE_SLOW(bool);
void aslAPCmdCaptureOnly(SlowRemoteCommandID iCmdID,
						 U32 uAccountID,
						 U32 uLockID,
						 const char *pOrderID,
						 bool bCommit)
{
	PERFINFO_AUTO_START_FUNC();
	
	if(pOrderID)
	{
		ObjectIndexKey key = {0};
		AccountProxyLockContainer *pLock = NULL;
		ContainerStore *pStore = objFindContainerStoreFromType(GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS);
		
		objIndexObtainReadLock(gidx_AccountProxyLocksByOrderID);
		objIndexInitKey_String(gidx_AccountProxyLocksByOrderID, &key, pOrderID);
		objIndexGetContainerData(gidx_AccountProxyLocksByOrderID, &key, 0, pStore, &pLock);
		objIndexReleaseReadLock(gidx_AccountProxyLocksByOrderID);
		objIndexDeinitKey_String(gidx_AccountProxyLocksByOrderID, &key);
		
		if(pLock)
		{
			if (uLockID && uLockID != pLock->id)
			{
				WARNING_NETOPS_ALERT("PROXY_CAPTURE_INVALID_LOCKID", "The proxy was asked to capture lock ID %d for order ID %s, "
					"but the lock and order ID don't match! Capturing the specified order ID anyway.", uLockID, pOrderID);
			}

			uLockID = pLock->id;
		}
		else
		{
			// This happens if the lock has expired before the user completed it.
			SlowRemoteCommandReturn_aslAPCmdCaptureOnly(iCmdID, false);
			PERFINFO_AUTO_STOP();
			return;
		}
	}

	if (bCommit)
	{
		AutoTrans_AccountProxy_tr_FinishLock(objCreateManagedReturnVal(aslAPCmdCaptureOnly_LockFinished, (void*)((intptr_t)iCmdID)), GetAppGlobalType(), GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, uLockID, uAccountID, NULL, TransLogType_Other);
	}
	else
	{
		AutoTrans_AccountProxy_tr_RollbackLock(objCreateManagedReturnVal(aslAPCmdCaptureOnly_LockFinished, (void*)((intptr_t)iCmdID)), GetAppGlobalType(), GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, uLockID, uAccountID, NULL);
	}

	PERFINFO_AUTO_STOP();
}

static void aslAPCmdAuthCapture_LockCreated(SA_PARAM_NN_VALID TransactionReturnVal *pReturn, SA_PARAM_NN_VALID AuthCaptureRequest *pRequest)
{
	PERFINFO_AUTO_START_FUNC();

	if (devassert(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS))
	{
		eaiPush(&geaLockIDsToWalk, pRequest->uLockContainerID);

		if (gbVerboseAccountProxyLogging)
		{
			SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "PurchaseAuthCreated",
				("requestid", "%u", pRequest->requestID)
				("cmdid", "%u", pRequest->iCmdID)
				("accountid", "%u", pRequest->uAccountID)
				("currency", "%s", pRequest->pCurrency)
				("lockid", "%u", pRequest->uLockContainerID));
		}

		// Send packet.
		AddAccountPacket(TO_ACCOUNTSERVER_PROXY_AUTHCAPTURE, parse_AuthCaptureRequest, &pRequest);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

AUTO_COMMAND_REMOTE_SLOW(AuthCaptureResultInfo *);
void aslAPCmdAuthCapture(SlowRemoteCommandID iCmdID,
						 AuthCaptureRequest * pRequestOriginal)
{
	AuthCaptureRequest *pRequest = NULL;
	U32 uContainerID = 0;
	AuthCaptureResultInfo response = {0};

	PERFINFO_AUTO_START_FUNC();

	if (gbNoAccountServer || !MicrotransactionsEnabled())
	{
		response.eResult = PURCHASE_RESULT_AUTH_FAIL;
		SlowRemoteCommandReturn_aslAPCmdAuthCapture(iCmdID, &response);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (pRequestOriginal->bAuthOnly)
	{
		uContainerID = GetUnusedLockContainerID();

		if (!uContainerID)
		{
			response.eResult = PURCHASE_RESULT_AUTH_FAIL;
			SlowRemoteCommandReturn_aslAPCmdAuthCapture(iCmdID, &response);
			PERFINFO_AUTO_STOP();
			return;
		}
	}

	pRequest = StructClone(parse_AuthCaptureRequest, pRequestOriginal);
	pRequest->iCmdID = iCmdID;
	pRequest->pProxy = StructAllocString(AccountGetShardProxyName());
	pRequest->requestID = getNewProxyRequestID();
	pRequest->bVerifyPrice = gbVerifyMTPrices;
	pRequest->uLockContainerID = uContainerID;

	if (pRequest->bAuthOnly)
	{
		if (gbAccountProxyLogging)
		{
			EARRAY_FOREACH_BEGIN(pRequest->eaItems, i);
			{
				TransactionItem *pItem = pRequest->eaItems[i];

				SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "PurchaseAuth",
					("requestid", "%u", pRequest->requestID)
					("cmdid", "%u", iCmdID)
					("accountid", "%u", pRequest->uAccountID)
					("productid", "%u", pItem->uProductID)
					("currency", "%s", pRequest->pCurrency)
					("price", "%"FORM_LL"d", moneyCountPoints(pItem->pPrice))
					("lockid", "%u", pRequest->uLockContainerID));
			}
			EARRAY_FOREACH_END;
		}

		AutoTrans_AccountProxy_tr_CreateLock(objCreateManagedReturnVal(aslAPCmdAuthCapture_LockCreated, pRequest),
			GLOBALTYPE_ACCOUNTPROXYSERVER, GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, uContainerID,
			pRequest->uAccountID, pRequest->pCurrency, APACTIVITY_AUTHCAPTURE, pRequest->requestID, iCmdID);
	}
	else
	{
		if (gbAccountProxyLogging)
		{
			EARRAY_FOREACH_BEGIN(pRequest->eaItems, i);
			{
				TransactionItem *pItem = pRequest->eaItems[i];

				SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "PurchaseAuthCapture",
					("requestid", "%u", pRequest->requestID)
					("cmdid", "%u", iCmdID)
					("accountid", "%u", pRequest->uAccountID)
					("productid", "%u", pItem->uProductID)
					("currency", "%s", pRequest->pCurrency)
					("price", "%"FORM_LL"d", moneyCountPoints(pItem->pPrice)));
			}
			EARRAY_FOREACH_END;
		}

		AddAccountPacket(TO_ACCOUNTSERVER_PROXY_AUTHCAPTURE, parse_AuthCaptureRequest, &pRequest);
	}
	PERFINFO_AUTO_STOP_FUNC();
}

void AccountProxyLockAddCB(Container *con, AccountProxyLockContainer *pLock)
{
	if (pLock->pLock && ShouldSend(pLock, true))
		eaiPush(&geaLockIDsToWalk, pLock->id);
	if (pLock->pLock && pLock->pLock->pOrderID)
		objIndexInsert(gidx_AccountProxyLocksByOrderID, pLock);
}

void AccountProxyLockRemoveCB(Container *con, AccountProxyLockContainer *pLock)
{
	devassertmsg(0, "Account Proxy Server removing a lock container - this shouldn't be happening! (but it is non-damaging)");
	eaiFindAndRemoveFast(&geaLockIDsToWalk, pLock->id);
	eaiFindAndRemoveFast(&geaSentIDs, pLock->id);
	if (pLock->pLock && pLock->pLock->pOrderID)
		objIndexRemove(gidx_AccountProxyLocksByOrderID, pLock);
}

void AccountProxyLockPreCommitCB(Container *con, ObjectPathOperation **ops)
{
	AccountProxyLockContainer *pLock = (AccountProxyLockContainer *)con->containerData;
	ObjectPathOperation *op = ops[0];

	if ((op->op != TRANSOP_DESTROY || stricmp_safe(op->pathEString, ".Plock")) && stricmp_safe(op->pathEString, ".Plock.Porderid"))
		return;
	if (pLock->pLock && pLock->pLock->pOrderID)
		objIndexRemove(gidx_AccountProxyLocksByOrderID, pLock);
}

void AccountProxyLockPostCommitCB(Container *con, ObjectPathOperation **ops)
{
	AccountProxyLockContainer *pLock = (AccountProxyLockContainer *)con->containerData;

	if (pLock->pLock && pLock->pLock->pOrderID)
		objIndexInsert(gidx_AccountProxyLocksByOrderID, pLock);
}

int AccountProxyServerLibOncePerFrame(F32 fElapsed)
{
	static bool bRunOnce = false;
	static F32 fWaitingForProtocolTime = 0;
	static bool warnedOnce = false;

	PERFINFO_AUTO_START_FUNC();

	if (!bRunOnce)
	{
		// Create an index for lock containers by order ID
		objRegisterContainerTypeAddCallback(GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, AccountProxyLockAddCB);
		objRegisterContainerTypeRemoveCallback(GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, AccountProxyLockRemoveCB);
		objRegisterContainerTypeCommitCallback(GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, AccountProxyLockPreCommitCB, ".Plock*", false, false, true, NULL);
		objRegisterContainerTypeCommitCallback(GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, AccountProxyLockPostCommitCB, ".Plock.Porderid", false, false, false, NULL);
		gidx_AccountProxyLocksByOrderID = objIndexCreateWithStringPath(4, 0, parse_AccountProxyLockContainer, ".Plock.Porderid");

		EnsureAPContainersExist();
		bRunOnce = true;
	}

	if (gContainerExists && !gbNoAccountServer)
	{
		EnsureConnection(fElapsed);
		SendPackets();
		WalkLocks(false);

		if (!gProtocolVersion && linkConnected(gAccountServerLink))
		{
			fWaitingForProtocolTime += fElapsed;
			
			if (fWaitingForProtocolTime > ProtocolWaitTime && !warnedOnce)
			{
				LogError("ACCOUNTPROXY_PROTOCOL_TIMEOUT", "It has been more than %d seconds since connection to the account server, and the proxy has still not gotten a protocol information packet.  This probably means the account server needs to be updated to a newer version.\n", (int)fWaitingForProtocolTime);
				fWaitingForProtocolTime = 0;
				warnedOnce = true;
			}
		}
	}

	PERFINFO_AUTO_STOP_FUNC();

	return 1;
}


/************************************************************************/
/* Tracking of sent key-value set requests                              */
/************************************************************************/

#define SET_REQUEST_INVALID_INDEX -1

// Find the index of a set request
static int FindSetRequestIndex(U32 uAccountID,
							   SA_PARAM_NN_VALID const char *pKey,
							   ProxyRequestID requestID)
{
	int index = SET_REQUEST_INVALID_INDEX;

	PERFINFO_AUTO_START_FUNC();

	EARRAY_FOREACH_BEGIN(gSentRequests, iCurReq);
	{
		SentSetRequest *pRequest = gSentRequests[iCurReq];

		if (pRequest->accountID != uAccountID) continue;
		if (pRequest->requestID != requestID) continue;
		if (stricmp_safe(pRequest->key, pKey) != 0) continue;

		index = iCurReq;
		break;
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_FUNC();

	return index;
}

#define ValidSetRequestIndex(index) ((index) >= 0 && (index) < eaSize(&gSentRequests))

// Get a set request given an index
SA_RET_OP_VALID static SentSetRequest * GetSetRequest(int index)
{
	if (!verify(ValidSetRequestIndex(index))) return NULL;
	return gSentRequests[index];
}

// Destroy a set request
static void DestroySetRequest(int index)
{
	if (!verify(ValidSetRequestIndex(index))) return;
	StructDestroy(parse_SentSetRequest, eaRemove(&gSentRequests, index));
}

// Add a set request to track
static void AddSetRequest(SA_PARAM_NN_VALID SentSetRequest *pRequest)
{
	if (!verify(pRequest)) return;

	eaPush(&gSentRequests, pRequest);
}


/************************************************************************/
/* Local functions                                                      */
/************************************************************************/

// Find an account proxy lock that the proxy knows about
SA_RET_OP_VALID static AccountProxyLock *GetProxyLock(ContainerID containerID)
{
	Container *container;
	AccountProxyLockContainer *apContainer;

	if (!containerID) return NULL;

	PERFINFO_AUTO_START_FUNC();

	container = objGetContainer(GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, containerID);
	apContainer = container ? container->containerData : NULL;

	if (apContainer && gAccountServerLink)
	{
		PERFINFO_AUTO_STOP_FUNC();

		return apContainer->pLock;
	}

	PERFINFO_AUTO_STOP_FUNC();

	return NULL;
}

static void LogMessage(FORMAT_STR const char *pFormat, ...)
{
	char *str = NULL;

	PERFINFO_AUTO_START_FUNC();

	VA_START(args, pFormat);
		estrConcatfv(&str, pFormat, args);
	VA_END();
	printf("%s", str);
	log_printf(LOG_ACCOUNT_PROXY, "%s", str);
	estrDestroy(&str);

	PERFINFO_AUTO_STOP_FUNC();
}

static void LogError(SA_PARAM_NN_STR const char *pKey, FORMAT_STR const char *pFormat, ...)
{
	char *str = NULL;

	PERFINFO_AUTO_START_FUNC();

	consoleSetFGColor(COLOR_RED|COLOR_BRIGHT);
	VA_START(args, pFormat);
		estrConcatfv(&str, pFormat, args);
	VA_END();
	log_printf(LOG_ACCOUNT_PROXY, "%s", str);
	ErrorOrAlert(pKey, "%s", str);
	estrDestroy(&str);
	consoleSetFGColor(COLOR_RED|COLOR_BLUE|COLOR_GREEN);

	PERFINFO_AUTO_STOP_FUNC();
}

static void getProducts(SA_PARAM_NN_STR const char *pCategory, SA_PARAM_NN_VALID CONTAINERID_EARRAY *ppProductIDs)
{
	const AccountProxyProductList *pList = NULL;

	PERFINFO_AUTO_START_FUNC();
	if (pList = ADCGetProductsByCategory(pCategory))
	{
		EARRAY_CONST_FOREACH_BEGIN(pList->ppList, iCurProduct, iNumProducts);
		{
			const AccountProxyProduct *pProduct = pList->ppList[iCurProduct];

			if (devassert(pProduct))
			{
				ea32Push(ppProductIDs, pProduct->uID);
			}
		}
		EARRAY_FOREACH_END;
	}
	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Send packets                                                         */
/************************************************************************/

static void AddAccountPacket(int iType, ParseTable * pt, void **pData)
{
	AccountPacket *pkt;

	PERFINFO_AUTO_START_FUNC();
	pkt = StructCreate(parse_AccountPacket);

	pkt->iType = iType;
	pkt->pData = *pData;
	pkt->pt = pt;
	eaPush(&gPktQueue, pkt);

	*pData = NULL;
	PERFINFO_AUTO_STOP();
}

static void SendPackets(void)
{
	AccountPacket *packet;

	PERFINFO_AUTO_START_FUNC();

	while (AccountServerLinkActive() && (packet = eaRemove(&gPktQueue, 0)))
	{
		Packet *pkt = pktCreate(gAccountServerLink, packet->iType);

		ParserSendStructSafe(packet->pt, pkt, packet->pData);
		pktSend(&pkt);
		StructDestroySafeVoid(packet->pt, &packet->pData);
		StructDestroy(parse_AccountPacket, packet);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void SendResponseToGameServer(SA_PARAM_NN_VALID AccountProxySetResponse *response)
{
	int requestIndex = SET_REQUEST_INVALID_INDEX;

	PERFINFO_AUTO_START_FUNC();

	requestIndex = FindSetRequestIndex(response->uAccountID, response->pKey, response->requestID);

	if (ValidSetRequestIndex(requestIndex))
	{
		SentSetRequest *sent = GetSetRequest(requestIndex);

		if (devassert(sent))
		{
			if (gbVerboseAccountProxyLogging)
			{
				SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "KeyLockResult",
					("requestid", "%u", sent->requestID)
					("cmdid", "%u", sent->iCmdID)
					("accountid", "%u", sent->accountID)
					("key", "%s", sent->key)
					("result", "%s", StaticDefineInt_FastIntToString(AccountKeyValueResultEnum, response->result)));
			}

			SlowRemoteCommandReturn_aslAPCmdSendLockRequest(sent->iCmdID, response);
		}
		
		DestroySetRequest(requestIndex);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void SendMoveResponseToGameServer(SA_PARAM_NN_VALID AccountProxyMoveResponse *pResponse)
{
	PERFINFO_AUTO_START_FUNC();

	EARRAY_FOREACH_REVERSE_BEGIN(gSentMoveRequests, iMoveRequest);
	{
		SentMoveRequest *pRequest = gSentMoveRequests[iMoveRequest];

		if (!devassert(pRequest)) continue;

		if (pRequest->uRequestID == pResponse->uRequestID)
		{
			if (gbVerboseAccountProxyLogging)
			{
				SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "KeyMoveResult",
					("requestid", "%u", pRequest->uRequestID)
					("cmdid", "%u", pRequest->iCmdID)
					("srcaccountid", "%u", pRequest->uSrcAccountID)
					("srckey", "%s", pRequest->pSrcKey)
					("destaccountid", "%u", pRequest->uDestAccountID)
					("destkey", "%s", pRequest->pDestKey)
					("result", "%s", StaticDefineInt_FastIntToString(AccountKeyValueResultEnum, pResponse->eResult)));
			}

			SlowRemoteCommandReturn_aslAPCmdMoveKeyValue(pRequest->iCmdID, pResponse);
			eaRemoveFast(&gSentMoveRequests, iMoveRequest);
			StructDestroy(parse_SentMoveRequest, pRequest);
			break;
		}
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();
}

static void SendStart(void)
{
	// This packet was originally sent repeatedly but has been repurposed to be part
	// of the initial handshake done between the proxy and Account Server and is only
	// sent once.
	AccountProxyBeginEndWalk walk = {0};
	Packet *pkt = NULL;

	PERFINFO_AUTO_START_FUNC();

	StructInit(parse_AccountProxyBeginEndWalk, &walk);
	estrCopy2(&walk.pProxy, AccountGetShardProxyName());
	estrCopy2(&walk.pCluster, ShardCommon_GetClusterName());
	estrCopy2(&walk.pEnvironment, microtrans_GetShardEnvironmentName());

	// Send the packet immediately; do not send as part of the queue
	pkt = pktCreate(gAccountServerLink, TO_ACCOUNTSERVER_PROXY_BEGIN);
	ParserSendStructSafe(parse_AccountProxyBeginEndWalk, pkt, &walk);
	pktSend(&pkt);

	StructDeInit(parse_AccountProxyBeginEndWalk, &walk);

	PERFINFO_AUTO_STOP_FUNC();
}

static void SendCommitRollback(SA_PARAM_NN_VALID const AccountProxyLockContainer *apContainer, bool bCommit)
{
	PERFINFO_AUTO_START_FUNC();

	if (apContainer->pLock->activityType == APACTIVITY_AUTHCAPTURE)
	{
		CaptureRequest *pRequest = StructCreate(parse_CaptureRequest);

		pRequest->bCapture = bCommit;
		pRequest->requestID = 0; // Not really needed
		pRequest->uAccountID = apContainer->pLock->uAccountID;
		pRequest->uLockContainerID = apContainer->id;
		pRequest->uPurchaseID = apContainer->pLock->uPurchaseID;
		pRequest->pProxy = StructAllocString(AccountGetShardProxyName());
		pRequest->pOrderID = StructAllocString(apContainer->pLock->pOrderID);

		if (gbAccountProxyLogging)
		{
			SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "PurchaseFinish",
				("lockid", "%u", apContainer->id)
				("accountid", "%u", apContainer->pLock->uAccountID)
				("purchaseid", "%u", apContainer->pLock->uPurchaseID)
				("capture", "%d", bCommit));
		}

		AddAccountPacket(TO_ACCOUNTSERVER_PROXY_CAPTURE, parse_CaptureRequest, &pRequest);
	}
	else if (apContainer->pLock->activityType == APACTIVITY_MOVE)
	{
		AccountProxyCommitRollbackMoveRequest *pRequest = StructCreate(parse_AccountProxyCommitRollbackMoveRequest);

		pRequest->uSrcAccountID = apContainer->pLock->uAccountID;
		estrCopy2(&pRequest->pSrcKey, apContainer->pLock->pKey);

		pRequest->uDestAccountID = apContainer->pLock->uDestAccountID;
		estrCopy2(&pRequest->pDestKey, apContainer->pLock->pDestKey);

		estrCopy2(&pRequest->pLock, apContainer->pLock->pLock);
		pRequest->eTransactionType = apContainer->pLock->eTransactionType;
		pRequest->uLockContainerID = apContainer->id;

		if (gbAccountProxyLogging)
		{
			SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "KeyMoveFinish",
				("lockid", "%u", apContainer->id)
				("srcaccountid", "%u", apContainer->pLock->uAccountID)
				("srckey", "%s", apContainer->pLock->pKey)
				("destaccountid", "%u", apContainer->pLock->uDestAccountID)
				("destkey", "%s", apContainer->pLock->pDestKey)
				("commit", "%d", bCommit));
		}

		AddAccountPacket(bCommit ? TO_ACCOUNTSERVER_PROXY_COMMIT_MOVE : TO_ACCOUNTSERVER_PROXY_ROLLBACK_MOVE, parse_AccountProxyCommitRollbackMoveRequest, &pRequest);
	}
	else
	{
		AccountProxyCommitRollbackRequest *pRequest = StructCreate(parse_AccountProxyCommitRollbackRequest);

		pRequest->uAccountID = apContainer->pLock->uAccountID;
		estrCopy2(&pRequest->pKey, apContainer->pLock->pKey);
		estrCopy2(&pRequest->pLock, apContainer->pLock->pLock);

		pRequest->eTransactionType = apContainer->pLock->eTransactionType;
		pRequest->containerID = apContainer->id;

		if (gbAccountProxyLogging)
		{
			SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "KeyFinish",
				("lockid", "%u", apContainer->id)
				("accountid", "%u", apContainer->pLock->uAccountID)
				("key", "%s", apContainer->pLock->pKey)
				("commit", "%d", bCommit));
		}

		AddAccountPacket(bCommit ? TO_ACCOUNTSERVER_PROXY_COMMIT : TO_ACCOUNTSERVER_PROXY_ROLLBACK, parse_AccountProxyCommitRollbackRequest, &pRequest);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void SendProtocolVersion(SA_PARAM_NN_VALID NetLink *link)
{
	Packet *packet;

	PERFINFO_AUTO_START_FUNC();

	packet = pktCreate(link, TO_ACCOUNTSERVER_PROXY_PROTOCOL_VERSION);

	pktSendU32(packet, ACCOUNT_PROXY_PROTOCOL_VERSION);
	pktSend(&packet);

	PERFINFO_AUTO_STOP_FUNC();
}

static void SendCreateCurrency(void)
{
	AccountProxyCreateCurrencyRequest *pRequest = StructCreate(parse_AccountProxyCreateCurrencyRequest);

	pRequest->pName = StructAllocString(microtrans_GetShardCurrency());
	pRequest->pGame = StructAllocString(GetProductName());
	pRequest->pEnvironment = StructAllocString(microtrans_GetShardMTCategory());

	AddAccountPacket(TO_ACCOUNTSERVER_PROXY_CREATE_CURRENCY, parse_AccountProxyCreateCurrencyRequest, &pRequest);
}


/************************************************************************/
/* Packet handlers                                                      */
/************************************************************************/

static void aslAPUpdateLock(U32 uLockContainerID, const char *pLockPassword, U32 uPurchaseID, const char *pOrderID, U32 uRequestID, TransactionReturnCallback pCallback, void *pUserdata)
{
	const AccountProxyLock *pLock = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	pLock = GetProxyLock(uLockContainerID);

	// There are two timeout cases - the lock has timed out, and it either has been reused, or has not been reused yet
	// In either case, the timeout code will have issued a remote command response and a rollback request to the AS, so just die
	if (!devassert(pLock))
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	if (uRequestID && pLock->requestID != uRequestID)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	// If the lock exists and the request IDs match, then we should be okay to update - try it
	AutoTrans_AccountProxy_tr_UpdateLock(objCreateManagedReturnVal(pCallback, pUserdata),
		GLOBALTYPE_ACCOUNTPROXYSERVER, GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, uLockContainerID,
		pLockPassword, uPurchaseID, pOrderID, uRequestID);
}

static void HandleProxySetResponse(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxySetResponse *response;

	PERFINFO_AUTO_START_FUNC();

	response = StructCreate(parse_AccountProxySetResponse);
	ParserRecvStructSafe(parse_AccountProxySetResponse, pkt, response);

	if (gbAccountProxyLogging)
	{
		SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "KeyLockASResponse",
			("requestid", "%u", response->requestID)
			("accountid", "%u", response->uAccountID)
			("key", "%s", response->pKey)
			("value", "%"FORM_LL"d", response->iValue)
			("lockid", "%u", response->containerID)
			("result", "%s", StaticDefineInt_FastIntToString(AccountKeyValueResultEnum, response->result)));
	}

	if (response->result == AKV_SUCCESS)
	{
		aslAPUpdateLock(response->containerID, response->pLock, 0, NULL, response->requestID, AP_tr_UpdateLock_CB, response);
	}
	else
	{
		SendResponseToGameServer(response);
		DestroyLock(response->containerID);
		StructDestroy(parse_AccountProxySetResponse, response);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void HandleMoveResponse(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyMoveResponse *pResponse = NULL;

	PERFINFO_AUTO_START_FUNC();

	pResponse = StructCreate(parse_AccountProxyMoveResponse);
	ParserRecvStructSafe(parse_AccountProxyMoveResponse, pkt, pResponse);

	if (gbAccountProxyLogging)
	{
		SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "KeyMoveASResponse",
			("requestid", "%u", pResponse->uRequestID)
			("lockid", "%u", pResponse->uLockContainerID)
			("result", "%s", StaticDefineInt_FastIntToString(AccountKeyValueResultEnum, pResponse->eResult)));
	}

	if (pResponse->eResult == AKV_SUCCESS)
	{
		aslAPUpdateLock(pResponse->uLockContainerID, pResponse->pLock, 0, NULL, pResponse->uRequestID, AP_tr_UpdateMoveLock_CB, pResponse);
	}
	else
	{
		SendMoveResponseToGameServer(pResponse);
		DestroyLock(pResponse->uLockContainerID);
		StructDestroy(parse_AccountProxyMoveResponse, pResponse);
	}

	PERFINFO_AUTO_STOP();
}

static void HandleProxyAck(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyAcknowledge ack = {0};
	const AccountProxyLock *pLock;

	PERFINFO_AUTO_START_FUNC();

	ParserRecvStructSafe(parse_AccountProxyAcknowledge, pkt, &ack);

	pLock = GetProxyLock(ack.containerID);

	if (pLock && !(ack.pLock && *ack.pLock && stricmp_safe(ack.pLock, pLock->pLock)))
	{
		DestroyLock(ack.containerID);
	}

	StructDeInit(parse_AccountProxyAcknowledge, &ack);

	PERFINFO_AUTO_STOP_FUNC();
}

static void PurgeUninterestingData(SA_PARAM_NN_VALID AccountProxyProduct *pProduct)
{
	const char *pShardCurrency = microtrans_GetShardCurrencyExactName();
	static char *pShardPrefix = NULL;
	static char *pPointBuyCategory = NULL;

	char **ppOldCategories = NULL;
	Money **ppOldFullMoneyPrices = NULL;
	Money **ppOldMoneyPrices = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (!pShardPrefix || !pPointBuyCategory)
	{
		estrPrintf(&pShardPrefix, "%s.", microtrans_GetShardCategoryPrefix());
		estrCopy2(&pPointBuyCategory, microtrans_GetGlobalPointBuyCategory());
	}
	
	ppOldFullMoneyPrices = pProduct->ppFullMoneyPrices;
	ppOldMoneyPrices = pProduct->ppMoneyPrices;
	ppOldCategories = pProduct->ppCategories;

	pProduct->ppFullMoneyPrices = NULL;
	pProduct->ppMoneyPrices = NULL;
	pProduct->ppCategories = NULL;

	PERFINFO_AUTO_START("ppFullMoneyPrices", 1);
	EARRAY_CONST_FOREACH_BEGIN(ppOldFullMoneyPrices, i, s);
	{
		const char *pCurrency = moneyCurrency(ppOldFullMoneyPrices[i]);
		if (isRealCurrency(pCurrency) || !stricmp_safe(pCurrency, pShardCurrency))
		{
			eaPush(&pProduct->ppFullMoneyPrices, ppOldFullMoneyPrices[i]);
			ppOldFullMoneyPrices[i] = NULL;
		}
	}
	EARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP_START("ppMoneyPrices", 1);

	EARRAY_CONST_FOREACH_BEGIN(ppOldMoneyPrices, i, s);
	{
		const char *pCurrency = moneyCurrency(ppOldMoneyPrices[i]);
		if (isRealCurrency(pCurrency) || !stricmp_safe(pCurrency, pShardCurrency))
		{
			eaPush(&pProduct->ppMoneyPrices, ppOldMoneyPrices[i]);
			ppOldMoneyPrices[i] = NULL;
		}
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_START("ppCategories", 1);

	EARRAY_CONST_FOREACH_BEGIN(ppOldCategories, i, s);
	{
		if ((SAFE_DEREF(pShardPrefix) && strStartsWith(ppOldCategories[i], pShardPrefix)) ||
			(SAFE_DEREF(pPointBuyCategory) && !stricmp_safe(ppOldCategories[i], pPointBuyCategory)))
		{
			eaPush(&pProduct->ppCategories, ppOldCategories[i]);
			ppOldCategories[i] = NULL;
		}
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_START("Purge extras", 1);

	eaDestroyStruct(&ppOldFullMoneyPrices, parse_Money);
	eaDestroyStruct(&ppOldMoneyPrices, parse_Money);
	eaDestroyEString(&ppOldCategories);
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP();
}

static U32 uLastDiscountVersion = 0;

static void HandleDiscounts(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyDiscountList discounts = {0};

	PERFINFO_AUTO_START_FUNC();

	ParserRecvStructSafe(parse_AccountProxyDiscountList, pkt, &discounts);
	discounts.uTimestamp = timeSecondsSince2000();
	discounts.uVersion = ++uLastDiscountVersion;

	FOR_EACH_IN_EARRAY(discounts.ppList, AccountProxyDiscount, pDiscount);
	{
		if (SAFE_DEREF(pDiscount->pInternalName) && stricmp_safe(pDiscount->pInternalName, GetProductName())) eaRemoveFast(&discounts.ppList, ipDiscountIndex);
		else if (stricmp_safe(pDiscount->pCurrency, microtrans_GetShardCurrencyExactName())) eaRemoveFast(&discounts.ppList, ipDiscountIndex);
		else continue;

		StructDestroy(parse_AccountProxyDiscount, pDiscount);
	}
	EARRAY_FOREACH_END;

	ADCReplaceDiscountCache(&discounts);
	StructDeInit(parse_AccountProxyDiscountList, &discounts);

	PERFINFO_AUTO_STOP();
}

static U32 uLastProductVersion = 0;

static void HandleProducts(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyProductList products = {0};

	PERFINFO_AUTO_START_FUNC();

	ParserRecvStructSafe(parse_AccountProxyProductList, pkt, &products);
	products.uTimestamp = timeSecondsSince2000();
	products.uVersion = ++uLastProductVersion;

	EARRAY_CONST_FOREACH_BEGIN(products.ppList, i, s1);
	{
		PurgeUninterestingData(products.ppList[i]);
	}
	EARRAY_FOREACH_END;

	ADCReplaceProductCache(&products);

	StructDeInit(parse_AccountProxyProductList, &products);

	RemoteCommand_gslAPProductCatalogChanged(GLOBALTYPE_WEBREQUESTSERVER, 0);

	PERFINFO_AUTO_STOP();
}



static void FindLocation_CB(ContainerRef *pRef, PendingAPRemoteCommand *pPending)
{
	PERFINFO_AUTO_START_FUNC();
	if(pRef && pPending)
	{
		if(pRef->containerType == GLOBALTYPE_LOGINSERVER)
		{
			switch(pPending->eCmd)
			{
			case APCMD_SET_ALL_KEY_VALUES:
				RemoteCommand_aslAPCmdClientCacheSetAllKeyValues(pRef->containerType, pRef->containerID, pRef->containerType, pRef->containerID, pPending->uAccountID, pPending->pList);
				break;
			case APCMD_SET_KEY_VALUE:
				RemoteCommand_aslAPCmdClientCacheSetKeyValue(pRef->containerType, pRef->containerID, pRef->containerType, pRef->containerID, pPending->uAccountID, pPending->pInfo);
				break;
			case APCMD_REMOVE_KEY_VALUE:
				RemoteCommand_aslAPCmdClientCacheRemoveKeyValue(pRef->containerType, pRef->containerID, pRef->containerType, pRef->containerID, pPending->uAccountID, pPending->pKey);
				break;
			case APCMD_STEAM_CAPTURE:
				RemoteCommand_aslAPCmdSteamCaptureComplete(pRef->containerType, pRef->containerID, pPending->uAccountID, pPending->eResult);
				break;
			}
		}
		else
		{
			switch(pPending->eCmd)
			{
			case APCMD_SET_ALL_KEY_VALUES:
				RemoteCommand_gslAPCmdClientCacheSetAllKeyValues(pRef->containerType, pRef->containerID, pRef->containerType, pRef->containerID, pPending->uAccountID, pPending->pList);
				break;
			case APCMD_SET_KEY_VALUE:
				RemoteCommand_gslAPCmdClientCacheSetKeyValue(pRef->containerType, pRef->containerID, pRef->containerType, pRef->containerID, pPending->uAccountID, pPending->pInfo);
				break;
			case APCMD_REMOVE_KEY_VALUE:
				RemoteCommand_gslAPCmdClientCacheRemoveKeyValue(pRef->containerType, pRef->containerID, pRef->containerType, pRef->containerID, pPending->uAccountID, pPending->pKey);
				break;
			case APCMD_STEAM_CAPTURE:
				RemoteCommand_gslAPCmdSteamCaptureComplete(pRef->containerType, pRef->containerID, pRef->containerType, pRef->containerID, pPending->uAccountID, pPending->eResult);
				break;
			}
		}
	}

	StructDestroy(parse_PendingAPRemoteCommand,pPending);

	PERFINFO_AUTO_STOP_FUNC();
}


static void HandleAllKeyValues(PACKET_PARAMS_PROTOTYPE)
{
	U32 uAccountID;
	AccountProxyKeyValueInfoList list = {0};
	PendingAPRemoteCommand *pPending;

	PERFINFO_AUTO_START_FUNC();

	uAccountID = pktGetU32(pkt);

	ParserRecvStructSafe(parse_AccountProxyKeyValueInfoList, pkt, &list);

	pPending = StructCreate(parse_PendingAPRemoteCommand);
	pPending->uAccountID = uAccountID;
	pPending->eCmd = APCMD_SET_ALL_KEY_VALUES;
	pPending->pList = StructClone(parse_AccountProxyKeyValueInfoList, &list);

	aslAPFindAccountLocation(uAccountID, FindLocation_CB, pPending);

	EARRAY_FOREACH_REVERSE_BEGIN(gSentKeyValueRequests, i);
	{
		if (gSentKeyValueRequests[i]->uAccountID == uAccountID)
		{
			SlowRemoteCommandReturn_aslAPCmdGetAllKeyValues(gSentKeyValueRequests[i]->iCmdID, &list);
			StructDestroy(parse_KeyValueRequestHolder, eaRemove(&gSentKeyValueRequests, i));
		}
	}
	EARRAY_FOREACH_END;

	StructDeInit(parse_AccountProxyKeyValueInfoList, &list);

	PERFINFO_AUTO_STOP_FUNC();
}

static void HandleAllKeyValuesWithPlayerID(PACKET_PARAMS_PROTOTYPE)
{
	U32 uAccountID, uPlayerID;
	AccountProxyKeyValueData data = {0};

	PERFINFO_AUTO_START_FUNC();

	ParserRecvStructSafe(parse_AccountProxyKeyValueData, pkt, &data);
	devassert(data.pList);
	uAccountID = data.uAccountID;
	uPlayerID = data.uPlayerID;
	devassertmsg(uPlayerID, "HandleAllKeyValuesWithPlayerID received an invalid player ID");
	RemoteCommand_gslAPCmdClientCacheSetAllKeyValues(GLOBALTYPE_ENTITYPLAYER, uPlayerID, GLOBALTYPE_ENTITYPLAYER, uPlayerID, uAccountID, data.pList);

	EARRAY_FOREACH_REVERSE_BEGIN(gSentKeyValueRequests, i);
	{
		if (gSentKeyValueRequests[i]->uAccountID == uAccountID)
		{
			SlowRemoteCommandReturn_aslAPCmdGetAllKeyValues(gSentKeyValueRequests[i]->iCmdID, data.pList);
			StructDestroy(parse_KeyValueRequestHolder, eaRemove(&gSentKeyValueRequests, i));
		}
	}
	EARRAY_FOREACH_END;

	StructDeInit(parse_AccountProxyKeyValueData, &data);

	PERFINFO_AUTO_STOP_FUNC();
}

static void HandleGetKeyValue(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyGetResponse response = {0};

	PERFINFO_AUTO_START_FUNC();

	ParserRecvStructSafe(parse_AccountProxyGetResponse, pkt, &response);
	SlowRemoteCommandReturn_aslAPCmdRequestKeyValue(response.iCmdID, response.iValue);
	StructDeInit(parse_AccountProxyGetResponse, &response);

	PERFINFO_AUTO_STOP();
}

static void HandleKeyValue(PACKET_PARAMS_PROTOTYPE)
{
	U32 uAccountID;
	AccountProxyKeyValueInfo info = {0};
	PendingAPRemoteCommand *pPending;

	PERFINFO_AUTO_START_FUNC();

	uAccountID = pktGetU32(pkt);

	ParserRecvStructSafe(parse_AccountProxyKeyValueInfo, pkt, &info);

	pPending = StructCreate(parse_PendingAPRemoteCommand);
	pPending->uAccountID = uAccountID;
	pPending->eCmd = APCMD_SET_KEY_VALUE;
	pPending->pInfo = StructClone(parse_AccountProxyKeyValueInfo, &info);
	
	aslAPFindAccountLocation(uAccountID, FindLocation_CB, pPending);

	StructDeInit(parse_AccountProxyKeyValueInfo, &info);

	PERFINFO_AUTO_STOP_FUNC();
}


static void HandleKeyRemove(PACKET_PARAMS_PROTOTYPE)
{
	U32 uAccountID;
	char *pKey;
	PendingAPRemoteCommand *pPending;

	PERFINFO_AUTO_START_FUNC();

	uAccountID = pktGetU32(pkt);
	pKey = pktGetStringTemp(pkt);

	pPending = StructCreate(parse_PendingAPRemoteCommand);
	pPending->uAccountID = uAccountID;
	pPending->eCmd = APCMD_REMOVE_KEY_VALUE;
	pPending->pKey = strdup(pKey);

	aslAPFindAccountLocation(uAccountID, FindLocation_CB, pPending);

	PERFINFO_AUTO_STOP_FUNC();
}

static void HandleAccountIDFromDisplayName(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyAccountIDFromDisplayNameResponse response = {0};

	PERFINFO_AUTO_START_FUNC();

	ParserRecvStructSafe(parse_AccountProxyAccountIDFromDisplayNameResponse, pkt, &response);

	if (!response.pDisplayName || !*response.pDisplayName)
	{
		LogMessage("Ignoring invalid ID from display name packet sent from the account server.\n");
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Handle all the explicit requests
	EARRAY_FOREACH_REVERSE_BEGIN(gConvertRequests, i);
	{
		ConvertHolder *pHolder = gConvertRequests[i];

		if (!stricmp(pHolder->pDisplayName, response.pDisplayName))
		{
			SlowRemoteCommandReturn_aslAPCmdGetAccountIDFromDisplayName(pHolder->iCmdID, response.uAccountID);
			StructDestroy(parse_ConvertHolder, eaRemove(&gConvertRequests, i));
		}
	}
	EARRAY_FOREACH_END;

	// TODO: cache response

	StructDeInit(parse_AccountProxyAccountIDFromDisplayNameResponse, &response);
	PERFINFO_AUTO_STOP();
}

static void HandleProtocolVersion(PACKET_PARAMS_PROTOTYPE)
{
	PERFINFO_AUTO_START_FUNC();

	gProtocolVersion = pktGetU32(pkt);

	if (gProtocolVersion && gProtocolVersion != ACCOUNT_PROXY_PROTOCOL_VERSION)
	{
		LogError("ACCOUNTPROXY_PROTOCOL_MISMATCH", "Account proxy protocol mismatch.  Account proxy: %d, Account server: %d.  Upgrade the lower of the two or downgrade the higher of the two.\n", ACCOUNT_PROXY_PROTOCOL_VERSION, gProtocolVersion);
	}
	else
	{
		LogMessage("Using protocol version %d.\n", gProtocolVersion);
		SendStart();
		SendCreateCurrency();
		WalkLocks(true);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void HandleSimpleSetResponse(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxySimpleSetResponse response = {0};

	PERFINFO_AUTO_START_FUNC();

	ParserRecvStructSafe(parse_AccountProxySimpleSetResponse, pkt, &response);

	if (gbAccountProxyLogging)
	{
		SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "KeySetASResponse",
			("requestid", "%u", response.requestID)
			("result", "%s", StaticDefineInt_FastIntToString(AccountKeyValueResultEnum, response.result)));
	}

	EARRAY_FOREACH_REVERSE_BEGIN(gSentSimpleSetRequests, i);
	{
		SimpleSetHolder *pSentRequest = gSentSimpleSetRequests[i];

		if (pSentRequest->pSetRequest->requestID == response.requestID)
		{
			if (gbVerboseAccountProxyLogging)
			{
				SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "KeySetResult",
					("requestid", "%u", response.requestID)
					("cmdid", "%u", pSentRequest->iCmdID)
					("accountid", "%u", pSentRequest->pSetRequest->uAccountID)
					("key", "%s", pSentRequest->pSetRequest->pKey)
					("result", "%s", StaticDefineInt_FastIntToString(AccountKeyValueResultEnum, response.result)));
			}

			SlowRemoteCommandReturn_aslAPCmdSetKeyValue(pSentRequest->iCmdID, response.result);
			StructDestroy(parse_SimpleSetHolder, eaRemove(&gSentSimpleSetRequests, i));
		}
	}
	EARRAY_FOREACH_END;

	StructDeInit(parse_AccountProxySimpleSetResponse, &response);

	PERFINFO_AUTO_STOP_FUNC();
}

static void HandleSubbedTime(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxySubbedTimeResponse response = {0};

	PERFINFO_AUTO_START_FUNC();
	
	ParserRecvStructSafe(parse_AccountProxySubbedTimeResponse, pkt, &response);

	EARRAY_FOREACH_REVERSE_BEGIN(gSubbedTimeRequests, iCurSubbedTimeRequest);
	{
		SubbedTimeRequestHolder *pHolder = gSubbedTimeRequests[iCurSubbedTimeRequest];

		if (!devassert(pHolder)) continue;

		if (!devassert(pHolder->pRequest)) continue;

		if (pHolder->pRequest->uAccountID == response.uAccountID &&
			!stricmp_safe(pHolder->pRequest->pProductInternalName, response.pProductInternalName))
		{
			SlowRemoteCommandReturn_aslAPCmdGetSubbedTime(pHolder->iCmdID, response.uTotalSecondEstimate);
			StructDestroy(parse_SubbedTimeRequestHolder, eaRemove(&gSubbedTimeRequests, iCurSubbedTimeRequest));
		}
	}
	EARRAY_FOREACH_END;

	StructDeInit(parse_AccountProxySubbedTimeResponse, &response);

	PERFINFO_AUTO_STOP_FUNC();
}

static void HandleLinkingStatus(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyLinkingStatusResponse response = {0};

	PERFINFO_AUTO_START_FUNC();
	ParserRecvStructSafe(parse_AccountProxyLinkingStatusResponse, pkt, &response);

	EARRAY_FOREACH_REVERSE_BEGIN(gLinkingStatusRequests, iCurRequest);
	{
		LinkingStatusRequestHolder *pHolder = gLinkingStatusRequests[iCurRequest];

		if (!devassert(pHolder)) continue;
		if (!devassert(pHolder->uAccountID)) continue;

		if (pHolder->uAccountID == response.uAccountID)
		{
			SlowRemoteCommandReturn_aslAPCmdGetAccountLinkingStatus(pHolder->iCmdID, &response);
			StructDestroy(parse_LinkingStatusRequestHolder, eaRemove(&gLinkingStatusRequests, iCurRequest));
		}
	}
	EARRAY_FOREACH_END;

	StructDeInit(parse_AccountProxyLinkingStatusResponse, &response);
	PERFINFO_AUTO_STOP();
}

static void HandlePlayedTime(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyPlayedTimeResponse response = {0};

	PERFINFO_AUTO_START_FUNC();
	ParserRecvStructSafe(parse_AccountProxyPlayedTimeResponse, pkt, &response);

	EARRAY_FOREACH_REVERSE_BEGIN(gPlayedTimeRequests, iCurRequest);
	{
		PlayedTimeRequestHolder *pHolder = gPlayedTimeRequests[iCurRequest];

		if (!devassert(pHolder) || !devassert(pHolder->pRequest)) continue;

		if (pHolder->pRequest->uAccountID == response.uAccountID &&
			!stricmp(pHolder->pRequest->pProduct, response.pProduct) &&
			!stricmp(pHolder->pRequest->pCategory, response.pCategory))
		{
			SlowRemoteCommandReturn_aslAPCmdGetAccountPlayedTime(pHolder->iCmdID, &response);
			StructDestroy(parse_PlayedTimeRequestHolder, eaRemove(&gPlayedTimeRequests, iCurRequest));
		}
	}
	EARRAY_FOREACH_END;

	StructDeInit(parse_AccountProxyPlayedTimeResponse, &response);
	PERFINFO_AUTO_STOP();
}

static void HandleAccountData(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyAccountDataResponse response = {0};

	PERFINFO_AUTO_START_FUNC();
	ParserRecvStructSafe(parse_AccountProxyAccountDataResponse, pkt, &response);

	EARRAY_FOREACH_REVERSE_BEGIN(gAccountDataRequests, iCurRequest);
	{
		AccountDataRequestHolder *pHolder = gAccountDataRequests[iCurRequest];

		if (!devassert(pHolder) || !devassert(pHolder->pRequest)) continue;

		if (pHolder->pRequest->uAccountID == response.uAccountID)
		{
			SlowRemoteCommandReturn_aslAPCmdGetAccountData(pHolder->iCmdID, &response);
			StructDestroy(parse_AccountDataRequestHolder, eaRemove(&gAccountDataRequests, iCurRequest));
		}
	}
	EARRAY_FOREACH_END;

	StructDeInit(parse_AccountProxyAccountDataResponse, &response);
	PERFINFO_AUTO_STOP();
}

static void HandleRecruitInfo(PACKET_PARAMS_PROTOTYPE)
{
	RecruitInfo recruitInfo = {0};

	if (!gbRecruitSystemEnabled || gConf.bDisableRecruitUpdates)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	ParserRecvStructSafe(parse_RecruitInfo, pkt, &recruitInfo);

	AutoTrans_trUpdateRecruitInfo(NULL, GLOBALTYPE_ACCOUNTPROXYSERVER, GLOBALTYPE_GAMEACCOUNTDATA, recruitInfo.uAccountID, &recruitInfo);

	StructDeInit(parse_RecruitInfo, &recruitInfo);

	PERFINFO_AUTO_STOP_FUNC();
}

static void HandlePaymentMethods(PACKET_PARAMS_PROTOTYPE)
{
	PaymentMethodsResponse response = {0};

	PERFINFO_AUTO_START_FUNC();

	ParserRecvStructSafe(parse_PaymentMethodsResponse, pkt, &response);

	SlowRemoteCommandReturn_aslAPCmdRequestPaymentMethods(response.iCmdID, &response);

	StructDeInit(parse_PaymentMethodsResponse, &response);

	PERFINFO_AUTO_STOP_FUNC();
}

static void HandleAuthCapture_LockUpdated(SA_PARAM_NN_VALID TransactionReturnVal *pReturn, SA_PARAM_NN_VALID AuthCaptureResponse *pResponse)
{
	AuthCaptureResultInfo response = {0};

	PERFINFO_AUTO_START_FUNC();

	response.uLockContainerID = pResponse->uLockContainerID;

	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		response.eResult = pResponse->eResult;
	}
	else
	{
		response.eResult = PURCHASE_RESULT_AUTH_FAIL;
	}

	if (gbVerboseAccountProxyLogging)
	{
		SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "PurchaseAuthResult",
			("requestid", "%u", pResponse->requestID)
			("cmdid", "%u", pResponse->iCmdID)
			("accountid", "%u", pResponse->uAccountID)
			("purchaseid", "%u", pResponse->uPurchaseID)
			("currency", "%s", pResponse->pCurrency)
			("lockid", "%u", pResponse->uLockContainerID)
			("result", "%s", StaticDefineInt_FastIntToString(PurchaseResultEnum, response.eResult)));
	}

	SlowRemoteCommandReturn_aslAPCmdAuthCapture(pResponse->iCmdID, &response);

	StructDestroy(parse_AuthCaptureResponse, pResponse);

	PERFINFO_AUTO_STOP_FUNC();
}

static void HandleAuthCapture(PACKET_PARAMS_PROTOTYPE)
{
	AuthCaptureResponse *pResponse = NULL;
	AuthCaptureResultInfo response = {0};

	PERFINFO_AUTO_START_FUNC();

	pResponse = StructCreate(parse_AuthCaptureResponse);
	ParserRecvStructSafe(parse_AuthCaptureResponse, pkt, pResponse);
	
	if (gbAccountProxyLogging)
	{
		if (pResponse->uLockContainerID)
		{
			SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "PurchaseAuthASResponse",
				("requestid", "%u", pResponse->requestID)
				("cmdid", "%u", pResponse->iCmdID)
				("accountid", "%u", pResponse->uAccountID)
				("purchaseid", "%u", pResponse->uPurchaseID)
				("currency", "%s", pResponse->pCurrency)
				("lockid", "%u", pResponse->uLockContainerID)
				("result", "%s", StaticDefineInt_FastIntToString(PurchaseResultEnum, pResponse->eResult)));
		}
		else
		{
			SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "PurchaseAuthCaptureASResponse",
				("requestid", "%u", pResponse->requestID)
				("cmdid", "%u", pResponse->iCmdID)
				("accountid", "%u", pResponse->uAccountID)
				("currency", "%s", pResponse->pCurrency)
				("result", "%s", StaticDefineInt_FastIntToString(PurchaseResultEnum, pResponse->eResult)));
		}
	}

	if (pResponse->eResult == PURCHASE_RESULT_PENDING && devassert(pResponse->uLockContainerID))
	{
		// The purchase is now authorized but not completed
		aslAPUpdateLock(pResponse->uLockContainerID, NULL, pResponse->uPurchaseID, pResponse->pOrderID, pResponse->requestID, HandleAuthCapture_LockUpdated, pResponse);
	}
	else
	{
		// The transaction either failed or was captured as a part of the auth

		// Remove the lock (this should only happen if it is a failure, as it wouldn't be created by an auth+capture request)
		if (pResponse->uLockContainerID)
		{
			DestroyLock(pResponse->uLockContainerID);
			devassert(pResponse->eResult != PURCHASE_RESULT_COMMIT);
		}

		response.eResult = pResponse->eResult;
		SlowRemoteCommandReturn_aslAPCmdAuthCapture(pResponse->iCmdID, &response);

		StructDestroy(parse_AuthCaptureResponse, pResponse);
	}
	
	PERFINFO_AUTO_STOP_FUNC();
}

static void HandleCapture(PACKET_PARAMS_PROTOTYPE)
{
	AuthCaptureResponse response = {0};

	PERFINFO_AUTO_START_FUNC();

	ParserRecvStructSafe(parse_AuthCaptureResponse, pkt, &response);

	if (devassert(response.uLockContainerID))
	{
		DestroyLock(response.uLockContainerID);
	}

	if (response.pOrderID)
	{
		PendingAPRemoteCommand *pPending = NULL;

		pPending = StructCreate(parse_PendingAPRemoteCommand);
		pPending->uAccountID = response.uAccountID;
		pPending->eCmd = APCMD_STEAM_CAPTURE;
		pPending->eResult = response.eResult;

		aslAPFindAccountLocation(response.uAccountID, FindLocation_CB, pPending);
	}

	StructDeInit(parse_AuthCaptureResponse, &response);

	PERFINFO_AUTO_STOP_FUNC();
}

static void HandleTicketForOnlineAccount(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyRequestAuthTicketForOnlinePlayer response = { 0 };

	PERFINFO_AUTO_START_FUNC();

	ParserRecvStructSafe(parse_AccountProxyRequestAuthTicketForOnlinePlayer, pkt, &response);

	// Return the result
	SlowRemoteCommandReturn_aslAPCmdCreateTicketForOnlineAccount(response.iCmdID, response.uTicketID);

	StructDeInit(parse_AccountProxyRequestAuthTicketForOnlinePlayer, &response);

	PERFINFO_AUTO_STOP_FUNC();
}

static void HandleDisplayName(PACKET_PARAMS_PROTOTYPE)
{
	U32 uAccountID = 0;
	char *pNewDisplayName = NULL;

	PERFINFO_AUTO_START_FUNC();
	uAccountID = pktGetU32(pkt);
	pNewDisplayName = pktGetStringTemp(pkt);

	RemoteCommand_dbUpdateDisplayName_Remote(GLOBALTYPE_OBJECTDB, 0, uAccountID, pNewDisplayName);
	PERFINFO_AUTO_STOP();
}

static void AccountProxyMsgHandler(PACKET_PARAMS_PROTOTYPE)
{
	PERFINFO_AUTO_START_FUNC();

	switch (cmd)
	{
	xcase FROM_ACCOUNTSERVER_PROXY_SET_RESULT:
		HandleProxySetResponse(PACKET_PARAMS);
	xcase FROM_ACCOUNTSERVER_PROXY_ACK:
		HandleProxyAck(PACKET_PARAMS);
	xcase FROM_ACCOUNTSERVER_PROXY_PRODUCTS:
		HandleProducts(PACKET_PARAMS);
	xcase FROM_ACCOUNTSERVER_PROXY_DISCOUNTS:
		HandleDiscounts(PACKET_PARAMS);
	xcase FROM_ACCOUNTSERVER_PROXY_ALL_KEY_VALUES:
		HandleAllKeyValues(PACKET_PARAMS);
	xcase FROM_ACCOUNTSERVER_PROXY_ALL_KEY_VALUES_PLAYERID:
		HandleAllKeyValuesWithPlayerID(PACKET_PARAMS);
	xcase FROM_ACCOUNTSERVER_PROXY_KEY_VALUE:
		HandleKeyValue(PACKET_PARAMS);
	xcase FROM_ACCOUNTSERVER_PROXY_REMOVE_KEY:
		HandleKeyRemove(PACKET_PARAMS);
	xcase FROM_ACCOUNTSERVER_PROXY_REQUEST_ACCOUNT_ID_BY_DISPLAY_NAME:
		HandleAccountIDFromDisplayName(PACKET_PARAMS);
	xcase FROM_ACCOUNTSERVER_PROXY_PROTOCOL_VERSION:
		HandleProtocolVersion(PACKET_PARAMS);
	xcase FROM_ACCOUNTSERVER_PROXY_SIMPLE_SET:
		HandleSimpleSetResponse(PACKET_PARAMS);
	xcase FROM_ACCOUNTSERVER_PROXY_SUBBED_TIME:
		HandleSubbedTime(PACKET_PARAMS);
	xcase FROM_ACCOUNTSERVER_PROXY_RECRUIT_INFO:
		HandleRecruitInfo(PACKET_PARAMS);
	xcase FROM_ACCOUNTSERVER_PROXY_PAYMENT_METHODS:
		HandlePaymentMethods(PACKET_PARAMS);
	xcase FROM_ACCOUNTSERVER_PROXY_AUTHCAPTURE:
		HandleAuthCapture(PACKET_PARAMS);
	xcase FROM_ACCOUNTSERVER_PROXY_CAPTURE:
		HandleCapture(PACKET_PARAMS);
	xcase FROM_ACCOUNTSERVER_PROXY_TICKET_FOR_ONLINE_ACCOUNT:
		HandleTicketForOnlineAccount(PACKET_PARAMS);
	xcase FROM_ACCOUNTSERVER_PROXY_GET_RESULT:
		HandleGetKeyValue(PACKET_PARAMS);
	xcase FROM_ACCOUNTSERVER_PROXY_LINKING_STATUS:
		HandleLinkingStatus(PACKET_PARAMS);
	xcase FROM_ACCOUNTSERVER_PROXY_MOVE_RESULT:
		HandleMoveResponse(PACKET_PARAMS);
	xcase FROM_ACCOUNTSERVER_PROXY_PLAYED_TIME:
		HandlePlayedTime(PACKET_PARAMS);
	xcase FROM_ACCOUNTSERVER_PROXY_DISPLAY_NAME:
		HandleDisplayName(PACKET_PARAMS);
	xcase FROM_ACCOUNTSERVER_PROXY_ACCOUNT_DATA:
		HandleAccountData(PACKET_PARAMS);
	}

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Container management                                                 */
/************************************************************************/

static void DestroyLockCB(SA_PARAM_NN_VALID TransactionReturnVal *pReturn, SA_PARAM_NN_VALID void *pData)
{
	ContainerID containerID = PtrToInt(pData);

	PERFINFO_AUTO_START_FUNC();

	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		devassertmsg(eaiFindAndRemove(&geaPendingRemovalIDs, containerID) > -1, "Destroyed lock not in array of removal IDs.");
		eaiFindAndRemove(&geaSentIDs, containerID); // This can fail
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void DestroyLock(ContainerID containerID)
{
	PERFINFO_AUTO_START_FUNC();

	if (eaiFind(&geaPendingRemovalIDs, containerID) == -1)
	{
		eaiPush(&geaPendingRemovalIDs, containerID);

		AutoTrans_AccountProxy_tr_RemoveLock(objCreateManagedReturnVal(DestroyLockCB, IntToPtr(containerID)),
			GLOBALTYPE_ACCOUNTPROXYSERVER, GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, containerID);
	}

	eaiFindAndRemove(&geaLockIDsToWalk, containerID);

	PERFINFO_AUTO_STOP_FUNC();
}

static void MarkLockSent(ContainerID containerID)
{
	PERFINFO_AUTO_START_FUNC();
	eaiPushUnique(&geaSentIDs, containerID);
	PERFINFO_AUTO_STOP_FUNC();
}

static void MarkLockTimeoutCB(TransactionReturnVal *returnVal, void *userData)
{
	ContainerID containerID = PtrToInt(userData);
	eaiPush(&geaLockIDsToWalk, containerID);
}

static void MarkLockTimeout(ContainerID containerID)
{
	AutoTrans_AccountProxy_tr_TimeoutLock(objCreateManagedReturnVal(MarkLockTimeoutCB, IntToPtr(containerID)),
		GLOBALTYPE_ACCOUNTPROXYSERVER, GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, containerID);
}

static bool ShouldSend(SA_PARAM_NN_VALID const AccountProxyLockContainer *apContainer, bool forceSend)
{
	if (!apContainer) return false;
	if (!apContainer->pLock) return false;
	if (!forceSend && eaiFind(&geaSentIDs, apContainer->id) > -1) return false;
	if (apContainer->pLock->result != APRESULT_COMMIT && apContainer->pLock->result != APRESULT_ROLLBACK) return false;

	return true;
}

static void HandleTimeout(SA_PARAM_NN_VALID const AccountProxyLockContainer *apContainer)
{
	if (!verify(apContainer)) return;
	if (!apContainer->pLock) return;

	PERFINFO_AUTO_START_FUNC();

	if (apContainer->pLock->activityType == APACTIVITY_AUTHCAPTURE)
	{
		if (apContainer->pLock->iCmdID) // May be missing (set to 0) if the proxy crashed recently.
		{
			AuthCaptureResultInfo response = {0};
			response.eResult = PURCHASE_RESULT_AUTH_FAIL;
			SlowRemoteCommandReturn_aslAPCmdAuthCapture(apContainer->pLock->iCmdID, &response);
		}
	}
	else if (apContainer->pLock->activityType == APACTIVITY_MOVE)
	{
		EARRAY_CONST_FOREACH_REVERSE_BEGIN(gSentMoveRequests, iMoveRequest, iNumMoveRequests);
		{
			SentMoveRequest *pMoveRequest = gSentMoveRequests[iMoveRequest];

			if (apContainer->pLock->requestID == pMoveRequest->uRequestID)
			{
				aslAPSendMoveRequestFailure(pMoveRequest->iCmdID);
				eaRemoveFast(&gSentMoveRequests, iMoveRequest);
				StructDestroy(parse_SentMoveRequest, pMoveRequest);
				break;
			}
		}
		EARRAY_FOREACH_END;
	}
	else
	{
		int requestIndex = FindSetRequestIndex(apContainer->pLock->uAccountID, apContainer->pLock->pKey, apContainer->pLock->requestID);

		if (ValidSetRequestIndex(requestIndex))
		{
			SentSetRequest *pRequest = GetSetRequest(requestIndex);
			aslAPSendLockRequestFailure(pRequest->iCmdID, pRequest->accountID, pRequest->key);
			DestroySetRequest(requestIndex);
		}
	}

	SendCommitRollback(apContainer, false);

	// Don't destroy the lock here! It'll get destroyed by the callback after the AS responds

	PERFINFO_AUTO_STOP_FUNC();
}

static void WalkLocks(bool bForce)
{
	int containerIDIndex = 0;
	F32 now = timeSecondsSince2000();

	PERFINFO_AUTO_START_FUNC();

	for (containerIDIndex = eaiSize(&geaLockIDsToWalk) - 1; containerIDIndex >= 0; containerIDIndex--)
	{
		ContainerID containerID = geaLockIDsToWalk[containerIDIndex];
		Container *container = objGetContainer(GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, containerID);
		AccountProxyLockContainer *apContainer = container ? container->containerData : NULL;

		if (!container)
		{
			LogError("ACCOUNTPROXY_CONTAINER_ERROR", "Could not get container.\n");
			continue;
		}

		if (!apContainer)
		{
			LogError("ACCOUNTPROXY_CONTAINER_DATA_ERROR", "Could not get container data.\n");
			continue;
		}

		if (AccountServerLinkActive() && ShouldSend(apContainer, bForce))
		{
			switch (apContainer->pLock->result)
			{
				xcase APRESULT_COMMIT:
					SendCommitRollback(apContainer, true);
				xcase APRESULT_ROLLBACK:
					SendCommitRollback(apContainer, false);
			}
			MarkLockSent(containerID);
		}
		else if (apContainer->pLock && apContainer->pLock->result == APRESULT_TIMED_OUT)
		{
			eaiRemove(&geaLockIDsToWalk, containerIDIndex);
			HandleTimeout(apContainer);
		}
		else if (apContainer->pLock && apContainer->pLock->fDestroyTime && now > apContainer->pLock->fDestroyTime)
		{
			eaiRemove(&geaLockIDsToWalk, containerIDIndex);
			MarkLockTimeout(containerID);
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void HandleTransactionError(SA_PARAM_NN_VALID const TransactionReturnVal *pReturn, SA_PARAM_NN_VALID const char *pMessage)
{
	int i;
	char *msg = NULL;

	PERFINFO_AUTO_START_FUNC();

	estrPrintf(&msg, "%s\n", pMessage);
	for (i = 0; i < pReturn->iNumBaseTransactions; i++)
	{
		enumTransactionOutcome outcome = pReturn->pBaseReturnVals[i].eOutcome;
		estrConcatf(&msg, "\tOutcome: %s (", StaticDefineInt_FastIntToString(enumTransactionOutcomeEnum, outcome));
		estrConcatf(&msg, "result string: %s)\n", pReturn->pBaseReturnVals[i].returnString && pReturn->pBaseReturnVals[i].returnString[0] ?
			pReturn->pBaseReturnVals[i].returnString : "(none)");
	}
	LogError("ACCOUNTPROXY_TRANSACTION_ERROR", "%s", msg);
	estrDestroy(&msg);

	PERFINFO_AUTO_STOP_FUNC();
}

static void EnsureAPContainerExists_CB(SA_PARAM_NN_VALID TransactionReturnVal *pReturn, SA_PARAM_OP_VALID void *pData)
{
	PERFINFO_AUTO_START_FUNC();

	if (pReturn->eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		HandleTransactionError(pReturn, "Failed to get singleton account proxy container.");
	}
	else if (giContainerCount >= giKeyValueLockMax)
	{
		LogMessage("Now own %d containers (max is %d), done creating.\n", giContainerCount, giKeyValueLockMax);
		RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "ready");
		gContainerExists = true;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void CreateProxyContainerCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	int count = 0;

	PERFINFO_AUTO_START_FUNC();

	while(++count < giKeyValueLockCreateRate && giContainerCount < giKeyValueLockMax) 
	{
		AccountProxyLockContainer container = {0};

		++giContainerCount;
		objRequestContainerCreate(objCreateManagedReturnVal(EnsureAPContainerExists_CB, NULL), GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS,
			&container, objServerType(), objServerID());
	}

	if(giContainerCount < giKeyValueLockMax)
	{
		LogMessage("Now own %d containers (max is %d), creating more.\n", giContainerCount, giKeyValueLockMax);
		TimedCallback_Run(CreateProxyContainerCB, NULL, 0.1f);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void AcquireContainersCallback(void)
{
	PERFINFO_AUTO_START_FUNC();

	giContainerCount = objCountOwnedContainersWithType(GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS);
	LogMessage("Got %d containers (max is %d).\n", giContainerCount, giKeyValueLockMax);

	if (giContainerCount < giKeyValueLockMax)
	{
		LogMessage("Creating new containers...\n");
		TimedCallback_Run(CreateProxyContainerCB, NULL, 0.1f);
	}
	else
	{
		RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "ready");
		gContainerExists = true;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

// This creates the single container if it doesn't already exist.
static void EnsureAPContainersExist(void)
{
	PERFINFO_AUTO_START_FUNC();

	LogMessage("Ensuring Object DB containers exist...\n");

	LogMessage("Acquiring existing containers...\n");
	aslAcquireContainerOwnership(GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, AcquireContainersCallback);

	PERFINFO_AUTO_STOP_FUNC();
}

ContainerID GetUnusedLockContainerID(void)
{
	ContainerID containerID = 0;

	PERFINFO_AUTO_START_FUNC();

	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, container);
	{
		AccountProxyLockContainer *apContainer = container ? container->containerData : NULL;

		if (!container)
		{
			LogError("ACCOUNTPROXY_CONTAINER_ERROR", "Could not get container.\n");
			break;
		}

		if (!apContainer)
		{
			LogError("ACCOUNTPROXY_CONTAINER_DATA_ERROR", "Could not get container data.\n");
			break;
		}

		if (!apContainer->pLock)
		{
			containerID = apContainer->id;
			break;
		}
	}
	CONTAINER_FOREACH_END;

	PERFINFO_AUTO_STOP_FUNC();

	return containerID;
}


/************************************************************************/
/* Connection management                                                */
/************************************************************************/

static bool AccountServerLinkActive(void)
{
	if (gAccountServerLink && linkConnected(gAccountServerLink) && gProtocolVersion == ACCOUNT_PROXY_PROTOCOL_VERSION) return true;
	return false;
}

static void AccountProxyServerConnected(SA_PARAM_NN_VALID NetLink *link, SA_PARAM_OP_VALID void *userData)
{
	PERFINFO_AUTO_START_FUNC();

	LogMessage("Connected to Account Server.\n");
	linkSetKeepAliveSeconds(link, 10);
	SendProtocolVersion(link);
	LogMessage("Waiting for protocol version...\n");

	PERFINFO_AUTO_STOP_FUNC();
}

static void AccountProxyServerDisconnected(SA_PARAM_OP_VALID NetLink *link, SA_PARAM_OP_VALID void *userData)
{
	PERFINFO_AUTO_START_FUNC();

	LogError("ACCOUNTPROXY_LOST_CONNECTION", "Disconnected from Account Server.  Will attempt to reconnect every %4.2f seconds.\n", ReconnectTimeout);
	gAccountServerLink = NULL;
	gProtocolVersion = 0;

	PERFINFO_AUTO_STOP_FUNC();
}

static void EnsureConnection(F32 fElapsed)
{
	static F32 timeBeforeReconnect = 0;
	char error[1024];

	PERFINFO_AUTO_START_FUNC();

	if (!gAccountServerLink || !linkConnected(gAccountServerLink))
	{
		if (timeBeforeReconnect < 1)
		{
			if (gAccountServerLink)
			{
				linkRemove(&gAccountServerLink);
				gAccountServerLink = NULL;
			}

			LogMessage("Attempting to connect to the Account Server...\n");
			gAccountServerLink = commConnectEx(commDefault(), LINKTYPE_SHARD_CRITICAL_1MEG, LINK_FORCE_FLUSH, getAccountServer(),
				DEFAULT_ACCOUNTPROXYSERVER_PORT, AccountProxyMsgHandler, AccountProxyServerConnected, AccountProxyServerDisconnected, 0,
				SAFESTR(error), __FILE__, __LINE__);
			if (gAccountServerLink)
			{
				linkSetKeepAlive(gAccountServerLink);
			}
			else
			{
				LogError("ACCOUNTPROXY_CONNECTION_FAILED", "Account Proxy could not connect to Account Server: %s\n", error);
			}

			timeBeforeReconnect = ReconnectTimeout;
		}
	}
	timeBeforeReconnect -= fElapsed;

	PERFINFO_AUTO_STOP_FUNC();
}

// Initiates a WebSrv game event request for the given account
void aslAPWebSrvGameEventRequest(AccountProxyWebSrvGameEvent *pGameEvent)
{
	AccountProxyWebSrvGameEvent *pRequest;
	PERFINFO_AUTO_START_FUNC();
	pRequest = StructClone(parse_AccountProxyWebSrvGameEvent, pGameEvent);	
	AddAccountPacket(TO_ACCOUNTSERVER_PROXY_EVENT_REQUEST, parse_AccountProxyWebSrvGameEvent, &pRequest);
	PERFINFO_AUTO_STOP_FUNC();
}

#include "aslAccountProxyServer_c_ast.c"