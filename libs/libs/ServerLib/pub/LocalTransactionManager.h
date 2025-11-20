/***************************************************************************



***************************************************************************/

#ifndef LOCALTRANSACTIONMANAGER_H_
#define LOCALTRANSACTIONMANAGER_H_

#include "GlobalTypeEnum.h"
#include "TransactionSystem.h"
#include "EString.h"
#include "timing.h"
#include "TransactionOutcomes.h"

//this header file defines stuff relating to LocalTransactionManagers. A LocalTransactionManager
//(sometimes abbreviated LTM) lives on a server and does two things: (a) it allows code running on
//that server to request transactions and get results back, and (b) it allows objects living on that
//server to be transactable.
//
//Making generic objects transacatable, when the LTM code doesn't know anything about their structure
//or organization or anything, is somewhat tricky, and requires a complicated system of callbacks and
//handles


//-----HANDLE TYPES
//this is the handle to an object returned by local code to the LTM. Basically, the LTM starts
//with an object type (from globaltypes.h) and an ID number, and queries the local code
//as to whether that object exists locally. If it does, the local code passes back a handle
//which "points to" that object. If the local code just refers to objects by ID numbers, it
//can pass the ID number right back out as the handle. However, if searching for objects is
//expensive, it might wish to pass back a pointer, or something else of that sort
typedef void* LTMObjectHandle;

//Transactions are strings, which means it might take some amount of time and effort to turn
//them into useful data. However, there's a multi-step handshaking process during a transaction
//in which the same transaction has to be referred to multiple times (for instance, once during
//a "can this transaction be executed" function and once during a "execute this transaction now"
//function). Therefore, it can be useful for the local code to do all that work (converting a
//string into appliable data) only once, put the parsed data into a container of some sort, and
//return a handle to that container back out to the LTM. There are two such handles:
//
//This handle points to a presumably-small container containing information about what
//fields this transaction affects. This hangs around for a while and is used for locking/unlocking
typedef void* LTMObjectFieldsHandle;

//This handle points to a potentially-large container containing information about what the
//transaction actually does.
typedef void* LTMProcessedTransactionHandle;
//
//Note that these two handles are completely optional. If a particular bit of local code deals only with
//super-simple strings, or is super-non-performance-critical, or has its own internal caching scheme, it's
//free to just ignore these handles entirely and use the transaction string itself.


//---CALLBACK FUNCTIONS
//many of these functions take a char** which is a pointer to a pointer which points to the return string. This will start
//out as a NULL string, and each function is responsible for deallocating any previous strings it points to, if any, or for
//appending multiple return strings.
//
//Note that if ppReturnvalString == NULL (as opposed to *ppReturnValString being NULL), that means that no return value string
//is requested.
//
//In general, in non-debugging mode, only one of these functions will return a return value for any given transaction. For
//instance, if a transaction fails during one particular step, that step will return a failure return value, and no other step
//will return anything.

//These callbacks are listed in approximately the order in which they are called. If you want to figure out exactly which
//callbacks are called when, your best bet is to look at the code in HandleNewTransaction() in LocalTransactionManager.c


//checks whether the specified object of the specified type exists. If it does, set a handle to it (likely either a
//pointer or an ID) in *pFoundObjectHandle
typedef bool LTMCallBack_DoesObjectExistLocally(GlobalType eObjType, U32 iObjID, LTMObjectHandle *pFoundObjectHandle,
	char **ppReturnValString, void *pUserData);


//return values for LTMCallBack_PreProcessTransactionString
typedef enum
{
	TRANSACTION_INVALID, //the requested transaction is poorly formatted or disallowed or something
	TRANSACTION_VALID_NORMAL, //the requested transaction is valid, and will complete instantaneously
	TRANSACTION_VALID_SLOW, //the requested transaction is valid, but will not complete instantaneously
} enumTransactionValidity;

//This callback is OPTIONAL (although very very useful for any but the most simple applications)
//
//Does three things to the specified transaction string: (1) checks whether it's a legal and correct transaction string,
//and whether it will complete instantaneously or not. Returns enumTransactionValidity
//(2) Optionally preprocsses the transaction string into some local transaction format, and puts
//a handle to it in *pProcessedTransactionHandle, and (3) optionally extracts information about what fields this
//transaction might affect, and puts that information into *pObjectFieldsHandle
typedef enumTransactionValidity LTMCallBack_PreProcessTransactionString(GlobalType eObjType, const char *pTransactionString,
	LTMProcessedTransactionHandle *pProcessedTransactionHandle, LTMObjectFieldsHandle *pObjectFieldsHandle, char **ppReturnValString,
	TransactionID iTransactionID, const char *pTransactionName, void *pUserData);


//checks whether the specified object can be locked so that a new transaction can be performed on it. If this object type
//supports field-level locking, then it uses the objFieldsHandle to figure out whether the specified fields can be
//locked. Note that this function doesn't actually do any locking, just checks for its availability.
//
//Note that if the object or fields are already locked, they should have the transaction ID of the transaction which
//locked them, and if it's the same as the current transaction ID, then this function should return true.
//
//this function also sets the transaction ID of the transaction which is causing the blockage
typedef bool LTMCallBack_AreFieldsOKToBeLocked(GlobalType eObjType, LTMObjectHandle objHandle, const char *pTransactionString,
	LTMObjectFieldsHandle objFieldsHandle, TransactionID iTransactionID, const char *pTransactionName, void *pUserData, TransactionID *pTransIDCausingBlock);

//checks whether the specified object can currently legally perform the specified transaction. For instance,
//if the transaction is "add a guy to a team" and the team is full, this function should return FALSe, and
//put a string like "ERROR: Team Full" in the return val.
//
//CHANGE: it is now acceptable for this function give back a false positive. That is, if this function
//says that the transaction can be done, and then ApplyTransaction fails, that is just peachy fine
typedef bool LTMCallBack_CanTransactionBeDone(GlobalType eObjType, LTMObjectHandle objHandle,
	const char *pTransactionString, LTMProcessedTransactionHandle processedTransactionHandle,
	LTMObjectFieldsHandle objFieldsHandle,
	char **ppReturnValString, TransactionID iTransactionID, const char *pTransactionName, void *pUserData);

//begins a lock on the specified fields. Note that locks can be cancelled, and if they are cancelled, the object MUST be
//restored to the state it was in before the lock began. This is typically done by backing up the state of the relevant fields
//when the lock begins.
typedef void LTMCallBack_BeginLock(GlobalType eObjType, LTMObjectHandle objHandle, LTMObjectFieldsHandle objFieldsHandle,
	TransactionID iTransactionID, const char *pTransactionName, void *pUserData);

//applies the specified transaction to the specified object. This should work both on objects that have been locked and
//objects that have not been locked. For an object that is not locked, the transaction completes and is finalized. For
//an object which is locked, the transaction completes, but is not yet "committed".
//
//CHANGE: It is now possible for this function to fail. If so, the transaction step will fail just as if CanTransactionBeDone
//had returned false.
//
//Note that this function returns four strings:
//(1) true or false for success/failure
//(2) The return val string
//(3) A database update string, which is a string that the transaction server can pass off to the object DB when
//this transaction is finalized, which will update the DB representation of whatever objects this transaction affected
//(4) A transaction server update string, which is a string of commands being sent directly to the transaction server
//itself, which will update its state to reflect this transaction. (This is useful for, for instance, transactions
//which involve moving objects from one server to another.)
typedef bool LTMCallBack_ApplyTransaction(GlobalType eObjType, LTMObjectHandle objHandle, const char *pTransactionString,
	LTMProcessedTransactionHandle processedTransactionHandle, LTMObjectFieldsHandle objFieldsHandle,
	char **ppReturnValString, TransDataBlock *pDBUpdateData, char **ppTransServerUpdateString, TransactionID iTransactionID, const char *pTransactionName, void *pUserData);

//This callback is OPTIONAL
//
//attempts to do the transction, and fails if it's not possible. This basically combines the funcationality of
//CanTransactionBeDone and ApplyTransaction, but might be useful if it requires a ton of processing to calculate
//whether the transaction can be done, much of which is duplicated by processing required to actually do the transaction.
//This will only be called if it's not an "atomic" transaction
//
//This callback is optional. If it isn't provided, CanTransactionBeDone and ApplyTransaction will be used
typedef bool LTMCallBack_ApplyTransactionIfPossible(GlobalType eObjType, LTMObjectHandle objHandle, const char *pTransactionString,
	LTMProcessedTransactionHandle processedTransactionHandle, LTMObjectFieldsHandle objFieldsHandle,
	char **ppReturnValString, TransDataBlock *pDBUpdateData, char **ppTransServerUpdateString, TransactionID iTransactionID, const char *pTransactionName, void *pUserData);

//undoes the lock on the specified fields. If that's the only lock on that field, also restores the original data
typedef void LTMCallBack_UndoLock(GlobalType eObjType, LTMObjectHandle objHandle, LTMObjectFieldsHandle objFieldsHandle, TransactionID iTransactionID, 
	void *pUserData);

//commits the lock on the specified fields. If there is backup data floating around, it is now no longer needed
typedef void LTMCallBack_CommitAndReleaseLock(GlobalType eObjType, LTMObjectHandle objHandle, LTMObjectFieldsHandle objFieldsHandle,
	TransactionID iTransactionID, void *pUserData);

//This callback is OPTIONAL
//
//releases an objects field handle
typedef void LTMCallBack_ReleaseObjectFieldsHandle(GlobalType eObjType, LTMObjectFieldsHandle objFieldsHandle,
	void *pUserData);

//This callback is OPTIONAL
//
//releases a processed transaction handle
typedef void LTMCallBack_ReleaseProcessedTransactionHandle(GlobalType eObjType, LTMProcessedTransactionHandle transactionHandle,
	void *pUserData);

//This callback is OPTIONAL
//
//releases a string which was provided as a return val string, db update string, or trans server update string
typedef void LTMCallBack_ReleaseString(GlobalType eObjType, char *pString, void *pUserData);

//This callback is OPTIONAL
//
//releases the contents of a TransDataBlock. Does not attempt to free the TransDataBlock itself
typedef void LTMCallBack_ReleaseDataBlock(GlobalType eObjType, TransDataBlock *pBlock, void *pUserData);


///---SLOW TRANSACTION STUFF
//
//A "slow transaction" is a transaction which the local code does not finish instantly. Most transactions
//have a fair amount of time spent with little messages whizzing around between servers and so forth, but
//the actual execution of the transaction is instantaneous. So when the LTM calls a callback function
//saying "do this transction now", when the callback function returns, the transaction is alread done.
//
//In a "slow transaction", on the other hand, the LTM tells the local code to start doing the transaction,
//and gives it an ID number for reference. When the local code is finished doing the transaction, it
//calls SlowTransactionCompleted. Note that for a slow transaction, the local code is responsible
//for doing the is-this-locked checks as well as the can-I-do-this checks all as one black-box step.
typedef int LTMSlowTransactionID; // 0 == none

//possible outcomes of a "slow transaction"
typedef enum
{
	SLOWTRANSACTION_FAILED,
	SLOWTRANSACTION_BLOCKED,
	SLOWTRANSACTION_SUCCEEDED,
} enumSlowTransactionOutcome;

//local code calls this when the slow transaction is finished
void SlowTransactionCompleted(struct LocalTransactionManager *pManager, LTMSlowTransactionID slowTransactionID, enumSlowTransactionOutcome eOutcome,
  char *pReturnValString, TransDataBlock *pDBUpdateData, char *pTransServerUpdateString);

//This callback actually begins the slow transaction. The local code is responsible for ensuring that for every call
//the LTM makes to BeginSlowTransaction, it will eventually get one and only one call to SlowTransactionCompleted.
//
//This callback is optional, in the sense that it will never be called if this local code doesn't use slow transactions.
//
//If bTransactionRequiresLockAndConfirm is true, then an outcome of success means that the transaction is possible,
//all ready to go, and locked. The only further calls to happen will be UndoLock or Commit and ReleaseLock
//BeginLock, CanTransactionBeDone, and ApplyTransaction are wrapped up in this call back
//
//If bTransactionRequiresLockAndConfirm is false, then an outcome of success means
//that the transaction has been totally completed.
typedef void LTMCallBack_BeginSlowTransaction(GlobalType eObjType, LTMObjectHandle objHandle,
	bool bTransactionRequiresLockAndConfirm, char *pTransactionString,
	LTMProcessedTransactionHandle processedTransactionHandle, LTMObjectFieldsHandle objFieldsHandle,
	TransactionID iTransactionID, const char *pTransactionName, LTMSlowTransactionID slowTransactionID, void *pUserData);




//this is the function that a normal transaction calls when it wants to read a transaction variable
void *GetTransactionVariable(struct LocalTransactionManager *pManager, TransactionID iTransactionID, const char *pVariableName, int *piSize);

//normal transaction sets a transaction variable
void SetTransactionVariableBytes(struct LocalTransactionManager *pManager, TransactionID iTransactionID, const char *pVariableName, void *pData, int iSize);
static __forceinline void SetTransactionVariableString(struct LocalTransactionManager *pManager, TransactionID iTransactionID, char *pVariableName, char *pString) { 
	if (!pString) pString = "";
	SetTransactionVariableBytes(pManager, iTransactionID, pVariableName, pString, (int)strlen(pString) + 1); 
}

static __forceinline void SetTransactionVariableEString(struct LocalTransactionManager *pManager, TransactionID iTransactionID, const char *pVariableName, char **eString) { 
	PERFINFO_AUTO_START_FUNC();
	if (!eString || !*eString) 
		SetTransactionVariableBytes(pManager, iTransactionID, pVariableName, "", 1); 
	else
		SetTransactionVariableBytes(pManager, iTransactionID, pVariableName, *eString, estrLength(eString) + 1); 
	PERFINFO_AUTO_STOP();
}

void SetTransactionVariablePacket(struct LocalTransactionManager *pManager, TransactionID iTransactionID, const char *pVariableName, Packet *pPak);

//this is the function that a slow transaction calls when it wants to read a transaction variable.
void *SlowTransaction_GetTransactionVariable(struct LocalTransactionManager *pManager, LTMSlowTransactionID slowTransactionID,
   char *pVariableName, int *piSize);

//slow transaction sets a transaction variable
void SlowTransaction_SetTransactionVariableBytes(struct LocalTransactionManager *pManager, LTMSlowTransactionID slowTransactionID,
	char *pVariableName, void *pData, int iSize);
static __forceinline void SlowTransaction_SetTransactionVariableString(struct LocalTransactionManager *pManager, LTMSlowTransactionID slowTransactionID,
	char *pVariableName, char *pString) { SlowTransaction_SetTransactionVariableBytes(pManager, slowTransactionID, pVariableName, pString, (int)strlen(pString) + 1); }







//This callback is different from all the rest, as only one type of server uses it. When the LTM is running on the object DB, the LTM calls
//this function to pass database update blocks to the object DB
typedef void LTMCallBack_ProcessDBUpdateData(TransDataBlock ***pppData, void *pUserData);

//LTMs are privately declared for purposes of modularity and secrecy
typedef struct LocalTransactionManager LocalTransactionManager;

//this function (a) mallocs an LTM, (b) sets a bunch of initial variables, and (c) attempts to set up and activate a link
//to the transaction server at the specified hostname and port number. Returns NULL on failure.
struct LocalTransactionManager *CreateAndRegisterLocalTransactionManager(void *pCBUserData,
	LTMCallBack_DoesObjectExistLocally *pDoesObjectExistCB,
	LTMCallBack_PreProcessTransactionString *pPreProcessTransactionStringCB,
	LTMCallBack_AreFieldsOKToBeLocked *pAreFieldsOKToBeLockedCB,
	LTMCallBack_CanTransactionBeDone *pCanTransactionBeDoneCB,
	LTMCallBack_BeginLock *pBeginLockCB,
	LTMCallBack_ApplyTransaction *pApplyTransactionCB,
	LTMCallBack_ApplyTransactionIfPossible *pApplyTransactionIfPossibleCB,
	LTMCallBack_UndoLock *pUndoLockCB,
	LTMCallBack_CommitAndReleaseLock *pCommitAndReleaseLockCB,
	LTMCallBack_ReleaseObjectFieldsHandle *pReleaseObjectFieldsHandleCB,
	LTMCallBack_ReleaseProcessedTransactionHandle *pReleaseProcessedTransactionHandleCB,
	LTMCallBack_ReleaseString *pReleaseStringCB,
	LTMCallBack_BeginSlowTransaction *pBeginSlowTransactionCB,
	LTMCallBack_ReleaseDataBlock *pReleaseDataBlockCB,
	PacketCallback *pPacketCB,

	GlobalType eServerType, //what type of global object the current entire server is
	U32 iServerID, //its ID


	char *pServerHostName,
	int iServerPort,
	bool bServerIsAMultiplex,

	//true, then this LTM will run in fully local mode, meaning it will not attempt to contact a transaction server,
	//will run all transactions locally, and will "commit changes" via the LTMCallBack_ProcessDBUpdateData
	bool bIsFullyLocal,

	char **ppErrorString
	);

// This function mallocs an LTM and initializes with data from pMainManager. This is for database threading.
LocalTransactionManager *CreateBackgroundThreadLocalTransactionManager(LocalTransactionManager *pMainManager);

//sets the delay while attempting to connect to Trans Server. Most servers have a high delay (ie 30 seconds). But the controller needs to use a short one
//so that it can continue to talk to the MCP while waiting for the trans server to start up. Usually, this shouldn't be called at all.
void SetLocalTransactionManagerConnectionDelayTime(float fTime);

//this function cleans up and destroys an LTM
void DestroyLocalTransactionManager(struct LocalTransactionManager *pManager);

//this function updates an LTM, and should be called once a tick
void UpdateLocalTransactionManager(struct LocalTransactionManager *pManager);

//this function tells the LTM to inform the transaction server that this server owns a specific object
void RegisterObjectWithTransactionServer(struct LocalTransactionManager *pManager, GlobalType eObjectType, int iObjectID);

//this is like RegisterObjectWithTransactionServer, but you can use it for multiple objects of the same type at once
void RegisterMultipleObjectsWithTransactionServer(struct LocalTransactionManager *pManager, GlobalType eObjectType, int iNumObjects, int *piObjectIDs);

//this function tells the LTM to inform the transaction server that this server owns all unclaimed objects of a given type. Generally, the
//object DB will call this for all object types. So if a mapserver has claimed to own a specific entity, then the transaction server
//knows to find that entity on that mapserver. If no mapserver has claimed that entity, then the entity is assumed to be owned by the object DB.
void RegisterAsDefaultOwnerOfObjectTypeWithTransactionServer(struct LocalTransactionManager *pManager, GlobalType eObjectType);

//The Object DB uses this to register its LTMCallBack_ProcessDBUpdateString
void RegisterDBUpdateDataCallback(struct LocalTransactionManager *pManager, LTMCallBack_ProcessDBUpdateData *pProcessDBUpdateData);

// Send a hardcoded string to the transaction server, useful for debugging or error fixing
void SendTransactionServerCommand(struct LocalTransactionManager *pManager, char *commandString, char *pCommentString);

//---STUFF RELATING TO REQUESTING NEW TRANSACTIONS


//checks if transactions can be requested (will generally return false in background threads)
bool CanTransactionsBeRequested(struct LocalTransactionManager *pManager);

typedef enum TransactionRequestFlags
{
	TRANSREQUESTFLAG_DO_LOCAL_STRUCT_FIXUP_FOR_AUTO_TRANS = 1 << 1, 
		//if set, then this is an auto transaction, and if the transaction is not being executed locally, 
		//then the middle base transaction needs to have 
		//AutoTrans_FixupLocalStructStringIntoParserWriteText called on it for optimization purposes

	TRANSREQUESTFLAG_DO_LOCAL_STRUCT_FIXUP_FOR_AUTO_TRANS_WITH_APPENDING = 1 << 2,
		//like the above, but the transaction was built via appending, so you have to look
		//at each base transaction and check its type rather than just going to the middle one
		//as you would with a normal AUTO_TRANS

} TransactionRequestFlags;

//Requests a new transaction
void RequestNewTransaction(struct LocalTransactionManager *pManager, const char *pTransactionName /*debugging only... single token, must be static or cached*/, BaseTransaction **ppBaseTransactions, //earray
	enumTransactionType eTransType, TransactionReturnVal *pWhereToPutReturnVal, TransactionRequestFlags eFlags);

//old version that uses an array, not an EArray.
void RequestNewTransaction_Deprecated(struct LocalTransactionManager *pManager, char *pTransactionName, int iNumBaseTransactions, BaseTransaction *pBaseTransactions, //earray
	enumTransactionType eTransType, TransactionReturnVal *pWhereToPutReturnVal);


//Simple wrapper function which requests a transaction with only one base transaction. (Note that transactions with only one base transaction
//do not have transaction types, because the behaviors of all the different transaction types are identical when there's only one base transaction
void RequestSimpleTransaction(struct LocalTransactionManager *pManager, U32 iRecipientType, U32 iRecipientID, const char *pTransactionName, const char *pData,
							  enumTransactionType eTransType, TransactionReturnVal *pWhereToPutReturnVal);

//This function MUST be called after a transaction has requested a transaction and gotten back a return value.
void ReleaseReturnValData(struct LocalTransactionManager *pManager, TransactionReturnVal *pReturnVal);

//call this if you called a transaction and are waiting for it to finish, but have now decided you don't care 
//after all
void CancelAndRelaseReturnValData(struct LocalTransactionManager *pTransManager, TransactionReturnVal *pReturnVal);


//By default local transactions are enabled, but this lets us turn that off
void EnableLocalTransactions(bool enabled);

bool AreLocalTransactionsEnabled(void);

// Gets some metrics on transactions. 
// For received base transactions, returns total and average size. This is only for a segment of a transaction
// For sent transactions, returns total and average size. This is for entire sent transactions
// For DB updates, total and average size of the update packet. This only works on things registered as databases
bool GetTransactionMetrics(struct LocalTransactionManager *pManager,
						   U32 *processedBaseTransactions, F32 *averageBaseTransactionSize,
						   U32 *sentTransactions, F32 *averageSentTransactionSize);




//-------In addition to all the complicated things it can do, the transaction server can simply send a packet to somewhere
//else, using the same rules it uses to determine where to send transactions
typedef enum
{
	//NOTE NOTE NOTE if you ever add anything to this, make the packet tracking stuff in
	//GetPacketToSendThroughTransactionServer more sophisticated
	TRANSPACKETCMD_REMOTECOMMAND,
} enumTransPacketCommand;

typedef void TransServerPacketFailureCB(void *pUserData1, void *pUserData2);


Packet *GetPacketToSendThroughTransactionServer(LocalTransactionManager *pManager, PacketTracker *pTracker,  GlobalType eDestType, ContainerID iDestID, enumTransPacketCommand eCmd, char *pName, TransServerPacketFailureCB *pFailureCB, void *pFailureUserData1, void *pFailureUserData2);
Packet *GetPacketToSendThroughTransactionServer_MultipleRecipients(LocalTransactionManager *pManager, PacketTracker *pTracker, ContainerRef ***pppRecipients, enumTransPacketCommand eCmd, char *pName);

Packet *GetPacketToSendThroughTransactionServerToOtherShard(LocalTransactionManager *pManager, PacketTracker *pTracker,
	const char *pDestShardName, GlobalType eDestType, ContainerID iDestID, enumTransPacketCommand eCmd, char *pName, TransServerPacketFailureCB *pFailureCB, void *pFailureUserData1, void *pFailureUserData2);


#define GetPacketToSendThroughTransactionServer_CachedTracker(pak, pManager, eDestType, iDestID, eCmd, pName, pFailureCB, pFailureUserData1, pFailureUserData2) \
{ static PacketTracker *pTracker; ONCE(pTracker = PacketTrackerFind("RemoteCommandCached", 0, pName)); pak = GetPacketToSendThroughTransactionServer(pManager, pTracker, eDestType, iDestID, eCmd, pName, pFailureCB, pFailureUserData1, pFailureUserData2); }

#define GetPacketToSendThroughTransactionServer_MultipleRecipients_CachedTracker(pak, pManager, pppRecipients, eCmd, pName) \
{ static PacketTracker *pTracker; ONCE(pTracker = PacketTrackerFind("RemoteCommandCachedMulti", 0, pName)); pak = GetPacketToSendThroughTransactionServer_MultipleRecipients(pManager, pTracker, pppRecipients, eCmd, pName); }


void SendPacketThroughTransactionServer(LocalTransactionManager *pManager, Packet **ppPak);



typedef void SimplePacketRemoteCommandFunc(Packet *pPack);

void RegisterSimplePacketRemoteCommandFunc(char *pCommandName, SimplePacketRemoteCommandFunc *pFunc);

bool IsLocalTransactionCurrentlyHappening(LocalTransactionManager *pManager);
const char *GetTransactionCurrentlyHappening(LocalTransactionManager *pManager);


bool IsLocalManagerFullyLocal(LocalTransactionManager *pManager);

//gets the actual failure string from a fail return value
char *GetTransactionFailureString(TransactionReturnVal *pRetVal);



//for debugging purposes only... ask the transaction server where it currently things a given container is
typedef void TransServerContainerLocRequestCB(GlobalType eContainerType, ContainerID iContainerID, 
	GlobalType eOwnerType, ContainerID iOwnerID, bool bIsDefaultLocation, void *pUserData);
void RequestTransactionServerContainerLocation(LocalTransactionManager *pManager, TransServerContainerLocRequestCB *pCB, void *pUserData, 
	GlobalType eContainerType, ContainerID iContainerID);





//if you want your LTM to call a graceful shutdown callback instead of svrExit() when losing connection to trans server
typedef void LTMCallback_GracefulShutdown(void);
void SetLocalTransactionManagerShutdownCB(LocalTransactionManager *pLTM, LTMCallback_GracefulShutdown *pCB);


//for debugging purposes... report to the transaction server about a transaction that is going slowly. It will know what to do
void ReportLaggedTransactions(LocalTransactionManager *pLTM, const char *pTransName, int iRecentCount, int iRecentTime);

//report that a transaction had a slow callback function, send up to transaction tracker on trans server
void ReportTransactionWithSlowCallback(LocalTransactionManager *pLTM, const char *pTransName);

// If true, force the transaction link to be compressed.
// If false, request transaction link be uncompressed.
// Otherwise, if it's -1, do some default server-dependent behavior.
extern int giCompressTransactionLink;

bool OnLocalTransactionManagerThread(LocalTransactionManager *pManager);
LocalTransactionManager *CreateBackgroundThreadLocalTransactionManager(LocalTransactionManager *pMainManager);
LocalTransactionManager **objGetThreadLocalTransactionManager(void);
void InitializeHandleCache(void);
int GetNumberOfInUseHandleCaches(void);

// Name of debugging event object when replaying transaction link
#define LTM_DEBUG_REPLAY_SIGNAL_NAME L"LTMSignalReplay"

void InitializeHandleCache(void);


//this is what the transaction server uses to hijack the SimpleRemoteCmds global type, for stat-tracking of simple
//remote commands
void DirectlyUpdateSimpleRemoteCommandStats(char *pName, int iBytes, PacketTracker **ppOutTracker);
#endif
