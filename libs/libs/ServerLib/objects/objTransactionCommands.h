/***************************************************************************



***************************************************************************/

#ifndef OBJTRANSACTIONCOMMANDS_H_
#define OBJTRANSACTIONCOMMANDS_H_

#include "objPath.h"
#include "GlobalTypes.h"

typedef struct TransactionCommand TransactionCommand;
typedef struct TransDataBlock TransDataBlock;

AUTO_STRUCT; //note that we don't actually use textparser to manipulate this struct, but it
//can be servermonitored
typedef struct LocalContainerInfo
{
	char varName[128];
	ContainerRef conRef;
	char **lockedFields;
	char *localDiffString;

	U32 fullContainer : 1;
} LocalContainerInfo;

// Creates a TransactionCommand, which is initially empty
TransactionCommand* CreateTransactionCommand(void);

// Destroys a TransactionCommand, calling the destroy callback if it exists
void DestroyTransactionCommand(TransactionCommand *pHolder);

// Initially parses a server-directed transaction command from a string
bool ParseServerTransactionCommand(const char *cmd, TransactionCommand *parsed, char **returnVal);

// Parse a transaction command that is specific to an object
bool ParseObjectTransactionCommand(const char *cmd, TransactionCommand *command, char **returnString);

// The following functions are called in rough chronological order, and correspond to the different stages of a transaction
bool CanTransactionCommandBeLocked(TransactionCommand *cmd, U32 *blockingTransaction);
void LockTransactionCommand(TransactionCommand *cmd);
bool CanTransactionCommandBeDone(TransactionCommand *cmd, char **returnVal);
bool DoTransactionCommand(TransactionCommand *cmd, char **returnVal, TransDataBlock *dbReturnData, char **transReturnString);
void CommitTransactionCommand(TransactionCommand *cmd);
void RevertTransactionCommand(TransactionCommand *cmd);

// Called every tick to see if any new progress has been made on slow transaction commands
void TryWaitingTransactionCommand(TransactionCommand *cmd, char **returnVal, TransDataBlock *dbReturnData, char **transReturnString);

// Add a new field entry to a transaction command
void AddFieldOperationToTransaction_EStrings(TransactionCommand *holder, ObjectPathOpType op, char*pathEString,char *valEString, bool quotedValue);

//similar to the above if you have a path and val strings temporary strings rather than an estring, can then copy them
//into the ObjectPathOperations internal buffers and avoid mallocs
void AddFieldOperationToTransaction_NotEStrings(TransactionCommand *holder, 
	ObjectPathOpType op, 
	const char*path, int iPathLen, 
	const char *val, int iValLen, bool quotedValue);


// Code for dealing with auto transaction optimizations

// Returns true if we are allowed to use fast local copies
bool AreFastLocalCopiesAllowedForCommand(TransactionCommand *pCommand);

#if 0
// Returns true if fast local copies are actually enabled for this transaction
bool AreFastLocalCopiesActive(TransactionCommand *pCommand);

// Enable/disable fast local copies for this transaction
void EnableFastLocalCopies(bool enable);
#endif

// Get fast local copy for modify or backup
void *GetFastLocalBackupCopy(TransactionCommand *pCommand, GlobalType type, ContainerID id);
void *GetFastLocalModifyCopy(TransactionCommand *pCommand, GlobalType type, ContainerID id);

typedef void *(*FastLocalCopyCB)(GlobalType type, ContainerID id); 

void RegisterFastLocalCopyCB(GlobalType type, FastLocalCopyCB backupCopy, FastLocalCopyCB modifyCopy);

// Store local transaction information
typedef struct LocalContainerInfoCache LocalContainerInfoCache;
LocalContainerInfo* GetLocalContainerInfo(TransactionCommand *cmd, const char *varName);
LocalContainerInfo* CreateLocalContainerInfo(TransactionCommand *cmd, const char *varName, GlobalType type, ContainerID id);
void LocalContainerInfoCache_DecreaseRefCount(LocalContainerInfoCache *pCache);

//there is no objServerTransactions.h, so putting this here. This is called whenever
//a new container is locally "created", ie, transfered in
LATELINK;
void NewContainerLocallyCreated(GlobalType eContainerType);

#endif