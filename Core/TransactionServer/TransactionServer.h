#ifndef _TRANSACTIONSERVER_H_
#define _TRANSACTIONSERVER_H_


#include "net/net.h"
#include "stashtable.h"
#include "GlobalTypeEnum.h"
#include "TransactionSystem.h"
#include "GlobalTypeEnum.h"
#include "nametable.h"
#include "TimedCallback.h"
#include "FloatAverager.h"


//if any single transaction is blocked and unblocked this many times, we assume we're in gridlock and start
//only unblocking one thing at a time
#define MAX_UNBLOCKS_BEFORE_ASSUMED_GRIDLOCK 32








AUTO_ENUM;
typedef enum
{
	BASETRANS_STATE_STARTUP,
	BASETRANS_STATE_INITQUERYSENT,
	BASETRANS_STATE_FAILED,
	BASETRANS_STATE_BLOCKED,
	BASETRANS_STATE_SUCCEEDED,
	BASETRANS_STATE_POSSIBLE_WAITFORCONFIRM,
	BASETRANS_STATE_CONFIRM_SENT,
	BASETRANS_STATE_CONFIRMED,
	BASETRANS_STATE_CANCEL_SENT,
	BASETRANS_STATE_CANCELLED,

	BASETRANS_STATE_COUNT
} enumBaseTransactionState;
#define BITS_FOR_BASE_TRANS_STATE 4



typedef struct
{
	int iID; // 0 if none
	int iIndex;
} LogicalConnectionHandle;


typedef struct
{
	//if 0, no return val requested
	int iReturnValID;
	LogicalConnectionHandle connectionHandle;

} TransactionResponseHandle;


typedef struct
{
	BaseTransaction transaction;
	enumBaseTransactionState eState;

	//what logical connection this base transaction is currently "talking" over, if any. (id == 0 for none) Only set if
	//this base transaction is currently in the middle of some handshake of some sort.
	LogicalConnectionHandle transConnectionHandle;

	//when a "this transaction is possible" message is sent to the trans server, it includes a reference ID which needs
	//to be passed back with the cancel or confirm message
	int iLocalTransactionManagerCallbackID;

	char * returnString;
	TransDataBlock databaseUpdateData;
	char * transServerUpdateString;

	//it seems redundant to store both the state and the outcome, but the outcome can not always be determined
	//from the state. For instance, something in an atomic that was cancelled may have succeeded or failed
	enumTransactionOutcome eOutcome; 
} BaseTransactionInfo;

AUTO_ENUM;
typedef enum
{
	IS_NOT_BLOCKED,
	BLOCKED_BY_SOMETHING,
	BLOCKED_BY_NOTHING,
} enumBlockedStatus;



AUTO_ENUM;
typedef enum TransVerboseLogEntryType
{
	TVL_NONE,
	TVL_CREATED, //container type and ID set
	TVL_CLEANUP, //nothing set. This triggers log_printfing of the entire verbose log
	TVL_NOWBLOCKEDBYNOTHING, //nothing set
	TVL_BLOCKEDBYSOMEONE, //iOtherID = trans ID of blocker, pStr = trans name of blocker
	TVL_BASETRANS_BEGAN, //container type and ID set, ID = step #
	TVL_FAILED, //nothing set
	TVL_SUCCEEDED, //nothing set
	TVL_BEGIN_CANCELLING, //nothing set
	TVL_FAIL_FROM_INIT_QUERY_STATE, //ID = step #
	TVL_STEP_FAILED, //ID = step #
	TVL_STEP_SUCCEEDED, //ID = step #
	TVL_STEP_CANCEL_CONFIRMED, //ID = step #
	TVL_STEP_POSSIBLE, //ID = step #
	TVL_STEP_POSSIBLE_AND_CONFIRMED, //ID = step #
	TVL_STEP_ABORTED, //ID = step, str = reason
	TVL_UNBLOCKED, //nothing set
} TransVerboseLogEntryType;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct TransVerboseLogEntry
{
	TransVerboseLogEntryType eType;
	const char *pStr; AST(POOL_STRING)
	GlobalType eContainerType;
	ContainerID iContainerID;
	U32 iOtherID;
	U64 iTime; //milliseconds since the transaction was created
} TransVerboseLogEntry;


typedef struct TransactionTracker TransactionTracker;

typedef struct Transaction
{
	const char *pTransactionName; //for debugging/tracking only

	TransactionID iID;

	enumTransactionType eType;

	int iUnblockCount;

	int iNumBaseTransactions;

	int iCurSequentialIndex;

	BaseTransactionInfo *pBaseTransactions;

	//keep running totals of how many base transactions are in each state to make various checks
	//easier
	U16 iBaseTransactionsPerState[BASETRANS_STATE_COUNT];

	TransactionResponseHandle responseHandle;

	NameTable transVariableTable;

	enumBlockedStatus eBlockedStatus;

	struct Transaction *pNextBlocked; //this means one of four things:
	//for an active transaction, this is the first transaction blocked by me
	//for a transaction blocked by another transaction, this is the next transaction blocked by the same transaction
	//for a transaction blocked by nothing in particular, this is the next transaction blocked by nothing
	//for inactive transactions, pointer to next inactive transaction in the fifo

	TransactionID iWhoBlockedMe; //in rare cases, more than one transaction can block a transaction simultaneously. If so,
		//we just remember who blocked us first

	U64 iTimeBegan; //timeMsecsSince2000

	TransVerboseLogEntry **ppVerboseLogs; //set to non-NULL at transaction creation time if the transaction should
		//be verbose logged

	TransactionTracker *pTracker;
} Transaction;




//WARNING WARNING WARNING when adding fields to this, make sure to initialize them in
//GetAndActivateNewLogicalConnection
typedef struct LogicalConnection
{
	//internally, the server refers to all its connection by both index (for quick access) and ID (for verification)
	int iConnectionIndex;
	int iConnectionID;

	GlobalType eServerType; //note that this is the type of the server, not the type of the object the server handles
						//GLOBAL
	
	//TODO: make all server IDs ContainerIDs, not U32s
	U32 iServerID;
	
	//if -1, this is not a multiplex connection
	//
	//note that iMultiplexConnectionIndex is the index WITHIN the multiplex connection, not the index OF the multiplex
	//connection, which you can get from the TransactionServerClientLink which is the userdata hanging off the
	//pNetLink
	int iMultiplexConnectionIndex;
	NetLink *pNetLink;

	struct LogicalConnection *pNext;
	struct LogicalConnection *pPrev;

	//these two variables are used when the special "find the best server to run this transaction on" code is
	//searching through all the connections to find the one that fits it best
	int iCurCount;
	int iCurCountID;

	//prevents us from repeating effort of aborting all transactions on a connection
	bool bAlreadyAbortedAll;

	//stash table of ints (keys are ints, vals are NULL) of transaction IDs that might (but might not, so double check before going nuts) 
	//currently be active on this connection. If the conneciton dies, these transactions are the ones that need to be killed.
	StashTable transTable;

} LogicalConnection;
//WARNING WARNING WARNING when adding fields to this, make sure to initialize them in
//GetAndActivateNewLogicalConnection

//note that no effort is being made to handle Multiplex connections in a fashion that makes it efficient to add/remove them, because the
//general assumption is that they start up once and remain on forever
typedef struct MultiplexConnection
{
	NetLink *pNetLink; //if null, this connection is inactive
	
	int iMaxConnectionIndex;
	
	//array which converts multiplex indexes into logical connection indexes. Each field is -1 if unused.
	int *piLogicalIndexesOfMultiplexConnections;
} MultiplexConnection;




//tracks where to find specified objects of a particular type. 
typedef struct
{
	int iDefaultConnectionIndex;
	StashTable directory;
} ObjectTypeDirectory;
		
typedef struct
{
	Transaction *pTransactions; //single allocated array of size gMaxTransactions
	TransactionID iNextTransactionID;

	//transactions "blocked by nothing" were blocked by a transaction that no longer exists, or is now blocked itself
	//stored this as a FIFO because that seems fair
	Transaction *pFirstTransactionBlockedByNothing;
	Transaction *pLastTransactionBlockedByNothing;

	Transaction *pNextEmptyTransaction;
	Transaction *pLastEmptyTransaction;

	int iNumAllocations;


	int iNextConnectionID;
	LogicalConnection *pConnections; //single allocated array of size gMaxLogicalConnections
	LogicalConnection *pFirstActive;
	LogicalConnection *pFirstFree;

	
	MultiplexConnection *pMultiplexConnections; //single allocated array of size gMaxMultiplexConnections

	//index of the only connection to a server of type GLOBALTYPE_OBJECTDB, or -1. More than one such
	//connection is not allowed
	int iDatabaseConnectionIndex;

	int iNumActiveConnections;
	int iNumActiveTransactions;
	U64 iNumCompletedNonSucceededTransactions;
	U64 iNumSucceededTransactions;


	ObjectTypeDirectory objectDirectories[GLOBALTYPE_MAXTYPES];

	IntAverager *pCompletionTimeAverager;
	IntAverager *pBytesPerTransactionAverager;

	CountAverager *pAllTransactionCounter;
	CountAverager *pBlockedTransactionCounter;

//u32 earray of transactions to verbose log
	TransactionID *pTransactionsToVerboseLog;

	StashTable sTrackersByTransName;

} TransactionServer;




typedef struct
{
	NetLink		*link;
	
	//-1 if none
	int iIndexOfLogicalConnection;
	int iIndexOfMultiplexConnection;

	U32 iTimeOfNextDeadServerAlert;
} TransactionServerClientLink;



void InitTransactionServer(TransactionServer *pServer);
void UpdateTransactionServer(TransactionServer *pServer);

void HandleNewTransactionRequest(TransactionServer *pServer, Packet *pPacket, int eServerType, U32 iServerID, int iConnectionIndex);
void HandleTransactionFailed(TransactionServer *pTransactionServer, Packet *pPacket);
void HandleTransactionSucceeded(TransactionServer *pTransactionServer, Packet *pPacket);
void HandleTransactionBlocked(TransactionServer *pTransactionServer, Packet *pPacket);
void HandleTransactionCancelConfirmed(TransactionServer *pTransactionServer, Packet *pPacket);
void HandleTransactionPossible(TransactionServer *pTransactionServer, Packet *pPacket);
void HandleTransactionPossibleAndConfirmed(TransactionServer *pTransactionServer, Packet *pPacket);
void HandleNormalConnectionRegisterClientInfo(TransactionServer *pServer, Packet *pPacket, NetLink *pLink, TransactionServerClientLink *client);
void HandleTransactionSetVariable(TransactionServer *pTransactionServer, Packet *pPacket);

void HandleMultiplexConnectionRegisterClientInfo(TransactionServer *pServer, Packet *pPacket, NetLink *pLink, int iSenderMultiplexIndex,TransactionServerClientLink *client);

void LogicalConnectionDied(TransactionServer *pServer, int iLogicalConnectionIndex, bool bNeedToKillConnection, const char *pDisconnectReason);
void MultiplexConnectionDied(TransactionServer *pServer, int iMultiplexConnectionIndex, const char *pReason);
void ConnectionFromMultiplexConnectionDied(TransactionServer *pServer, int iMultiplexConnectionIndex, int iIndexOfServer, const char *pReason);

void HandleTransactionServerCommand(TransactionServer *pServer, Packet *pPacket);
void HandleLocalTransactionUpdates(TransactionServer *pServer, Packet *pPacket);
void DumpTransactionServerStatus(TransactionServer *pServer);

extern TransactionServer gTransactionServer;

void transactionServerSendGlobalInfo(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData);
void TransServerAuxControllerMessageHandler(Packet *pkt,int cmd,NetLink* link,void *user_data);

void HandleSendPacketSimple(TransactionServer *pServer, Packet *pPak, int iConnectionIndex);
void HandleSendPacketSimpleOtherShard(TransactionServer *pServer, Packet *pPak, int iConnectionIndex);
void HandleRequestContainerOwner(TransactionServer *pServer, Packet *pPak, int iConnectionIndex);

void HandleReportLaggedTransactions(TransactionServer *pServer, Packet *pPak);

void HandleRegisterSlowTransCallbackWithTracker(TransactionServer *pServer, Packet *pPak);

void AbortBaseTransaction(TransactionServer *pServer, Transaction *pTransaction, int iIndex, const char *pReason);


extern int gMaxTransactions;


void DoVerboseTransLogging(Transaction *pTrans, TransVerboseLogEntryType eType, const char *pStr, GlobalType eCtrType, ContainerID iCtrID, U32 iOtherID);

void DumpLaggedTransactionSummary(void);
extern int giLaggedTransSummaryDumpInterval;
extern bool gbNoVerboseLogging;

// Call Transaction Server commMonitor()
void TransactionServerMonitorTick(F32 elapsed);

// Disable incoming message processing, for debugging.
void TransactionServerIgnoreIncomingMessages(bool bIgnore);

// Request that TransServerMain begin playing a capture replay on a link.
void TransactionServerSetCaptureReplayLink(NetLink *pLink);

// Replay a transaction capture to a link.
void TransactionServerDebugCaptureReplay(NetLink *pLink);

void TransServer_HandleRemoteCommandFromOtherShard(NetLink *pLink, Packet *pPack);

void HandleGotFailureFromOtherShard(int iReturnConnectionIndex, int iReturnConnectionID, 
	S64 iErrorCB, S64 iErrorUserData1, S64 iErrorUserData2);

#endif
