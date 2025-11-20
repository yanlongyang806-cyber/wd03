/***************************************************************************



***************************************************************************/


#include "objTransactions.h"
#include "objTransactionCommands.h"
#include "objLocks.h"
#include "AutoTransDefs.h"
#include "strings_opt.h"
#include "AutoTransSupport.h"
#include "ResourceInfo.h"
#include "MemAlloc.h"
#include "ObjTransactionCommands_c_ast.h"
#include "ObjTransactionCommands_h_ast.h"
#include "alerts.h"
#include "StringCache.h"
#include "windefinclude.h"
#include "logging.h"

#define MAX_LOCAL_CONTAINER_INFOS_BEFORE_ALERT 300


// This file contains the code for dealing with object transactions commands, 
// and the command callbacks for per-object commands.

// objServerTransactions.c has the per-server commands

// Callbacks for generic locking functions

__forceinline static bool CommitShouldModify(TransactionCommand *cmd)
{
	// Do not modify from the CommitCB's on ObjectDB or Shard-External servers with Fully Local TransManagers
	// dbupdatestring will take care of container modificiations for those cases
	return (!IsThisObjectDB() && !IsLocalManagerFullyLocal(objLocalManager()) );
}

// Attempts to lock a whole container
int CheckFullContainerLockCB(TransactionCommand *cmd)
{
	int iRetVal = CanContainerBeLocked(cmd->transactionID,cmd->objectType,cmd->objectID,&cmd->blockingTransactionID);

	return iRetVal;
}


// Locks a whole container, and backs it up
int FullContainerLockCB(TransactionCommand *cmd)
{
	LockContainer(cmd->transactionID,cmd->objectType,cmd->objectID);
	AddContainerBackup(cmd->transactionID,cmd->objectType,cmd->objectID);
	return 1;
}

// Locks a whole container, and backs it up
int FullDeletedContainerLockCB(TransactionCommand *cmd)
{
	LockContainer(cmd->transactionID,cmd->objectType,cmd->objectID);
	AddDeletedContainerBackup(cmd->transactionID,cmd->objectType,cmd->objectID);
	return 1;
}


// Commits a whole container
int FullContainerCommitCB(TransactionCommand *cmd)
{
	ContainerLock *lock = FindContainerLock(cmd->objectType,cmd->objectID);
	ContainerBackup *backup = FindContainerBackup(cmd->transactionID,cmd->objectType,cmd->objectID);

	if (lock->containerRemoved)
	{
		if (CommitShouldModify(cmd))
		{
			// Don't modify it here on the ObjectDB, because the dbupdatestring will take care of it
			objRemoveContainerFromRepository(cmd->objectType,cmd->objectID);
		}
	}
	else
	{
		Container *object = objGetContainer(cmd->objectType,cmd->objectID);

		// Don't modify it here on the ObjectDB, because the dbupdatestring will take care of it
		if (CommitShouldModify(cmd))
		{
			if (object)
			{
				objModifyContainer(object,cmd->diffString);
			}
			else
			{
				if (cmd->bUseBackupContainerWhenCommitting)
				{
					object = objAddExistingContainerToRepository(cmd->objectType,cmd->objectID, backup->containerBackup->containerData);
					backup->containerBackup->containerData = NULL;
				}
				else
				{
					object = objAddToRepositoryFromString(cmd->objectType,cmd->objectID,cmd->diffString);
				}
			}
		}
		
		if (backup && object)
		{
			assert(backup->containerBackup);
			objChangeContainerState(object, backup->containerBackup->meta.containerState, 
				backup->containerBackup->meta.containerOwnerType, backup->containerBackup->meta.containerOwnerID);
		}
	}

	UnlockContainer(cmd->transactionID,cmd->objectType,cmd->objectID);
	RemoveContainerBackup(cmd->transactionID,cmd->objectType,cmd->objectID);

	return 1;
}

// Unlock a container being deleted or undeleted
int ContainerCacheCommitCB(TransactionCommand *cmd)
{
	ContainerLock *lock = FindContainerLock(cmd->objectType,cmd->objectID);
	ContainerBackup *backup = FindContainerBackup(cmd->transactionID,cmd->objectType,cmd->objectID);

	UnlockContainer(cmd->transactionID,cmd->objectType,cmd->objectID);
	RemoveContainerBackup(cmd->transactionID,cmd->objectType,cmd->objectID);

	return 1;
}

// Unlock a container being deleted or undeleted
int ContainerCacheCommitNoBackupCB(TransactionCommand *cmd)
{
	ContainerLock *lock = FindContainerLock(cmd->objectType,cmd->objectID);
	UnlockContainer(cmd->transactionID,cmd->objectType,cmd->objectID);

	return 1;
}

// Reverts a whole container
int FullContainerRevertCB(TransactionCommand *cmd)
{
	UnlockContainer(cmd->transactionID,cmd->objectType,cmd->objectID);
	RemoveContainerBackup(cmd->transactionID,cmd->objectType,cmd->objectID);
	return 1;
}


// Sees if we can lock a set of fields
int CheckFieldLockCB(TransactionCommand *cmd)
{
	int i;
	bool lockable = true;
	ContainerLock *conLock = FindContainerLock(cmd->objectType,cmd->objectID);

	if (!conLock)
	{
		// If no lock entry for this container, we know it's not locked
		return true;
	}
	if (conLock->lockOwnerId > 0 && conLock->lockOwnerId != cmd->transactionID)
	{
		cmd->blockingTransactionID = conLock->lockOwnerId;

		return false;
	}


	for (i = 0; i < eaSize(&cmd->fieldEntries); i++)
	{
		lockable &= CanFieldBeLocked(cmd->transactionID,cmd->objectType,cmd->objectID,
			cmd->fieldEntries[i]->pathEString,&cmd->blockingTransactionID);
	}
	

	return lockable;
}

// Lock a set of fields
int FieldLockCB(TransactionCommand *cmd)
{
	int i;
	bool lockable = true;

	AddSparseContainerBackup(cmd->transactionID,cmd->objectType,cmd->objectID);

	for (i = 0; i < eaSize(&cmd->fieldEntries); i++)
	{
		LockField(cmd->transactionID,cmd->objectType,
			cmd->objectID,
			cmd->fieldEntries[i]->pathEString);

		BackupField(cmd->transactionID,
			cmd->objectType, cmd->objectID,
			cmd->fieldEntries[i]->pathEString);
	}

	//TODO: This needs to be replaced with a per-field back up.
	//AddContainerBackup(cmd->transactionID,cmd->objectType,cmd->objectID);

	return 1;
}

// Commit changes to a set of fields
int FieldCommitCB(TransactionCommand *cmd)
{
	int i;

	if (CommitShouldModify(cmd))
	{
		// Don't modify it here on the ObjectDB, because the dbupdatestring will take care of it
		objModifyContainer(objGetContainer(cmd->objectType,cmd->objectID),cmd->diffString);
	}

	for (i = 0; i < eaSize(&cmd->fieldEntries); i++)
	{
		UnlockField(cmd->transactionID,cmd->objectType,
			cmd->objectID,
			cmd->fieldEntries[i]->pathEString);
	}

	RemoveContainerBackup(cmd->transactionID,cmd->objectType,cmd->objectID);

	return 1;
}

// Return changes to a set of fields
int FieldRevertCB(TransactionCommand *cmd)
{
	int i;
	bool lockable = true;

	for (i = 0; i < eaSize(&cmd->fieldEntries); i++)
	{
		UnlockField(cmd->transactionID,cmd->objectType,
			cmd->objectID,
			cmd->fieldEntries[i]->pathEString);
	}

	RemoveContainerBackup(cmd->transactionID,cmd->objectType,cmd->objectID);

	return 1;
}


// Lock a set of fields
int FieldLockNoBackupCB(TransactionCommand *cmd)
{
	int i;
	bool lockable = true;

	for (i = 0; i < eaSize(&cmd->fieldEntries); i++)
	{
		LockField(cmd->transactionID,cmd->objectType,
			cmd->objectID,
			cmd->fieldEntries[i]->pathEString);
	}

	return 1;
}

// Commit changes to a set of fields
int FieldCommitNoBackupCB(TransactionCommand *cmd)
{
	int i;

	if (CommitShouldModify(cmd))
	{
		// Don't modify it here on the ObjectDB, because the dbupdatestring will take care of it
		objModifyContainer(objGetContainer(cmd->objectType,cmd->objectID),cmd->diffString);
	}

	for (i = 0; i < eaSize(&cmd->fieldEntries); i++)
	{
		UnlockField(cmd->transactionID,cmd->objectType,
			cmd->objectID,
			cmd->fieldEntries[i]->pathEString);
	}

	return 1;
}

// Return changes to a set of fields
int FieldRevertNoBackupCB(TransactionCommand *cmd)
{
	int i;
	bool lockable = true;

	for (i = 0; i < eaSize(&cmd->fieldEntries); i++)
	{
		UnlockField(cmd->transactionID,cmd->objectType,
			cmd->objectID,
			cmd->fieldEntries[i]->pathEString);
	}

	return 1;
}


// Container locking without backups
int FullContainerLockNoBackupCB(TransactionCommand *cmd)
{
	LockContainer(cmd->transactionID,cmd->objectType,cmd->objectID);
	return 1;
}

int FullContainerCommitNoBackupCB(TransactionCommand *cmd)
{
	UnlockContainer(cmd->transactionID,cmd->objectType,cmd->objectID);
	return 1;
}


int FullContainerRevertNoBackupCB(TransactionCommand *cmd)
{
	UnlockContainer(cmd->transactionID,cmd->objectType,cmd->objectID);
	return 1;
}

int FullDeletedContainerLockNoBackupCB(TransactionCommand *cmd)
{
	LockContainer(cmd->transactionID,cmd->objectType,cmd->objectID);
	AddDeletedContainerBackup(cmd->transactionID,cmd->objectType,cmd->objectID);
	return 1;
}



// Parse different command argument types, doing TRVAR substitution

const char *TransactionParseStringArgument(TransactionCommand *trCommand, const char *strArg)
{
	if (strncmp(strArg, "TRVAR_", 6) == 0)
	{
		int iTempSize;
		return GetTransactionVariable(objLocalManager(), trCommand->transactionID, strArg + 6, &iTempSize);
	}
	else
	{
		return strArg;
	}
}

int TransactionParseIntArgument(TransactionCommand *trCommand, const char *intArg)
{

	return atoi(TransactionParseStringArgument(trCommand, intArg));
}


F32 TransactionParseFloatArgument(TransactionCommand *trCommand, const char *floatArg)
{
	return opt_atof(TransactionParseStringArgument(trCommand, floatArg));
}




// Utility functions that operate on the transaction command list

CmdList gServerTransactionCmdList = {1,0,0}; // Internal list

// Not recursive/thread safe
bool ParseServerTransactionCommand(const char *cmd, TransactionCommand *command, char **returnString)
{
	int result = 0;
	static CmdContext context = {0};
	context.output_msg = returnString;

	context.access_level = 9;
	context.data = command;
	context.multi_line = true;

	verbose_printf("Received Server Transaction %s\n",cmd);

	result = cmdParseAndExecute(&gServerTransactionCmdList,cmd,&context);

	if (result)
	{
		S64 val;
		bool valid = false;
		val = MultiValGetInt(&context.return_val,&valid);
		if (val && valid)
		{
			result = 1;
		}
		else
		{
			result = 0;
		}
	}

	if (!result)
	{
		command->commandState = TRANSTATE_INVALID;
		if (returnString && !(*returnString))
		{
			estrPrintf(returnString,"Invalid transaction string");
		}
		return 0;
	}

	command->commandState = TRANSTATE_PARSED;
	return 1;		
}

// This defines the cmdparse table for per-object transaction commands

CmdList gObjectTransactionCmdList = {1,0,0};

// Not Recursion/thread safe
bool ParseObjectTransactionCommand(const char *cmd, TransactionCommand *command, char **returnString)
{
	int result = 0;
	static CmdContext context = {0};
	context.output_msg = returnString;

	context.access_level = 9;
	context.data = command;
	context.multi_line = true;

	verbose_printf("Received Object Transaction %s\n",cmd);

	result = cmdParseAndExecute(&gObjectTransactionCmdList,cmd,&context);

	if (result)
	{
		S64 val;
		bool valid = false;
		val = MultiValGetInt(&context.return_val,&valid);
		if (val && valid)
		{
			result = 1;
		}
		else
		{
			result = 0;
		}
	}

	if (!result)
	{
		command->commandState = TRANSTATE_INVALID;
		if (returnString && !(*returnString))
		{
			estrPrintf(returnString,"Invalid transaction string");
		}
		return 0;
	}

	command->commandState = TRANSTATE_PARSED;
	return 1;		
}


// Functions that call the appropriate callbacks

bool CanTransactionCommandBeDone(TransactionCommand *command, char **returnString)
{
	bool result;

	command->bCheckingIfPossible = 1;
	command->returnString = returnString;
	command->transReturnString = NULL;

	if (command->executeCB)
	{
		result = command->executeCB(command);
	}
	else
	{
		Errorf("Transaction command %s needs an execute callback!",command->parseCommand->name);
		result = false;
	}

	if (result)
	{
		command->commandState = TRANSTATE_ALLOWED;
		return 1;
	}
	command->commandState = TRANSTATE_DISALLOWED;
	return 0;
}

const char *GetCurTransactionName(void);
void SetCurTransactionName(const char *transactionName);

bool DoTransactionCommand(TransactionCommand *command, char **returnString, TransDataBlock *dbReturnData, char **transReturnString)
{
	bool result;
	command->bCheckingIfPossible = 0;
	command->returnString = returnString;
	command->pDatabaseReturnData = dbReturnData;
	command->transReturnString = transReturnString;

	if (command->bLockRequired && !command->bIsLocked)
	{
		estrPrintf(returnString,"This command must be part of an atomic transaction!");
		result = false;
	}
	else if (command->executeCB)
	{
		const char* oldTransactionName;
		
		PERFINFO_AUTO_START_STATIC(command->parseCommand->name, &command->parseCommand->perfInfo, 1);
		coarseTimerAddInstanceWithTrivia(NULL, "DoTransactionCommand", allocAddString(command->parseCommand->name));
		
		oldTransactionName = GetCurTransactionName();
		SetCurTransactionName(command->pTransactionName);
		result = command->executeCB(command);
		assert(GetCurTransactionName() == command->pTransactionName);
		SetCurTransactionName(oldTransactionName);

		coarseTimerStopInstance(NULL, "DoTransactionCommand");
		PERFINFO_AUTO_STOP_CHECKED(command->parseCommand->name);
	}
	else
	{
		Errorf("Transaction command %s needs an execute callback!",command->parseCommand->name);
		result = false;
	}

	if (result)
	{
		if (command->commandState != TRANSTATE_WAITING)
		{
			command->commandState = TRANSTATE_EXECUTED;
		}
		return 1;
	}
	command->commandState = TRANSTATE_FAILED;
	return 0;
}

bool CanTransactionCommandBeLocked(TransactionCommand *command, U32 *blockingTransaction)
{
	if (command->checkLockCB)
	{
		if (!command->checkLockCB(command))
		{
			*blockingTransaction = command->blockingTransactionID;
			return false;
		}
		else
		{
			return true;
		}
	}
	else
	{
		return true; // no callback, so no locking needed
	}
}

void LockTransactionCommand(TransactionCommand *command)
{
	command->bIsLocked = true;	
	if (command->lockCB)
	{
		PERFINFO_AUTO_START_STATIC(command->parseCommand->name, &command->parseCommand->perfInfo, 1);

		command->lockCB(command);

		PERFINFO_AUTO_STOP_CHECKED(command->parseCommand->name);		
	}
	command->commandState = TRANSTATE_LOCKED;
}


void CommitTransactionCommand(TransactionCommand *command)
{
	if (command->commitCB)
	{
		PERFINFO_AUTO_START_STATIC(command->parseCommand->name, &command->parseCommand->perfInfo, 1);
		coarseTimerAddInstanceWithTrivia(NULL, "CommitTransactionCommand", allocAddString(command->parseCommand->name));

		command->commitCB(command);

		coarseTimerStopInstance(NULL, "CommitTransactionCommand");
		PERFINFO_AUTO_STOP_CHECKED(command->parseCommand->name);
	}
	command->commandState = TRANSTATE_COMMITTED;
}

void RevertTransactionCommand(TransactionCommand *command)
{
	if (command->revertCB)
	{
		PERFINFO_AUTO_START_STATIC(command->parseCommand->name, &command->parseCommand->perfInfo, 1);

		command->revertCB(command);

		PERFINFO_AUTO_STOP_CHECKED(command->parseCommand->name);		
	}
	command->commandState = TRANSTATE_REVERTED;
}

void TryWaitingTransactionCommand(TransactionCommand *command, char **returnString, TransDataBlock *pDatabaseUpdateData, char **transReturnString)
{
	if (command->slowCB)
	{
		PERFINFO_AUTO_START_STATIC(command->parseCommand->name, &command->parseCommand->perfInfo, 1);
		
		command->slowCB(command);

		PERFINFO_AUTO_STOP_CHECKED(command->parseCommand->name);		
	}
}

// Helper functions for path transactions

static bool DecodePathTransactLine(ObjectPathOpType op, char *path, TransactionCommand *pHolder)
{
	char *pathEstr = NULL,*valueEstr = NULL;
	char *tempPath = NULL;
	bool quotedValue;
	int ok = 1;

	if (!path)
	{
		return 0;
	}

	if (op == 0)
	{
		return 0;
	}
	
	estrStackCreate(&tempPath);

	PERFINFO_AUTO_START_FUNC();

	if (objPathParseSingleOperation(path,strlen(path),&op,&tempPath,&valueEstr,&quotedValue))
	{
		ContainerStore * store = objFindContainerStoreFromType(pHolder->objectType);
		PERFINFO_AUTO_START("addFieldOp",1);		
		if (objPathNormalize(tempPath,store->containerSchema->classParse,&pathEstr))	
		{
			AddFieldOperationToTransaction_EStrings(pHolder,op,pathEstr,valueEstr,quotedValue);
		}
		else
		{
			ok = 0;
		}
		PERFINFO_AUTO_STOP();
	}
	else
	{
		estrDestroy(&pathEstr);
		estrDestroy(&valueEstr);
		ok = 0;
	}
	PERFINFO_AUTO_STOP();
	estrDestroy(&tempPath);
	return ok;
}

#define isWhitespace(c) (((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r') ? 1 : 0)

static bool DecodeFieldLocksForAutoTransAcquireFields(const char *command, TransactionCommand *pHolder)
{
	const char *line = command;

	if (!command)
	{
		return false;
	}
	
	while (line && line[0])
	{
		int pathType;
		char *nextLine = strchr(line,'\n');
		size_t lineLength;
		int op;

		pathType = atoi(line);
		line = strchr(line, ' ');

		if (nextLine)
		{
			lineLength = nextLine - line;
		}
		else
		{
			lineLength = strlen(line);
		}
		while (nextLine && isWhitespace(*nextLine))
		{
			nextLine++;
		}
		while (lineLength - 1 && isWhitespace(line[lineLength - 1]))
		{
			lineLength--;
		}

		if (pathType == ATR_LOCK_ARRAY_OPS)
		{
			op = TRANSOP_GET_ARRAY_ONLY;
		}
		else if (pathType == ATR_LOCK_INDEXED_NULLISOK || pathType == ATR_LOCK_NORMAL)
		{
			op = TRANSOP_GET_NULL_OKAY;
		}		
		else
		{
			op = TRANSOP_GET;
		}
		AddFieldOperationToTransaction_NotEStrings(pHolder,op,line, (int)lineLength,NULL,0, false);

		line = nextLine;
	}

	return true;
}

static bool ExecuteFieldTransaction(TransactionCommand *pHolder, char **resultString, char **diffEstring)
{
	Container *baseContainer = GetBaseContainer(pHolder->objectType,pHolder->objectID,pHolder->transactionID);
	int i;
	for (i = 0; i < eaSize(&pHolder->fieldEntries); i++)
	{
		ObjectPathOperation *fieldEntry = pHolder->fieldEntries[i];
		ParseTable* table;
		int column;
		void *object;
		int index;

		int success = GetLockedField(baseContainer,fieldEntry->pathEString,&table,&column,&object,&index,resultString,NULL);

		if (!success)
		{
			return 0;
		}

		if (objPathApplySingleOperation(table,column,object,index,fieldEntry->op,fieldEntry->valueEString,fieldEntry->quotedValue,resultString))
		{
			char *opString = NULL;
			char *pTempString = NULL;
			
			switch (fieldEntry->op)
			{
				xcase TRANSOP_SET:
				case TRANSOP_ADD:
				case TRANSOP_SUB:
				case TRANSOP_MULT:
				case TRANSOP_DIV:
				case TRANSOP_OR:
				case TRANSOP_AND:
					estrStackCreate(&pTempString);
					estrConcatf(diffEstring,"set %s = \"",fieldEntry->pathEString);
					FieldWriteText(table,column,object,index,&pTempString,0);
					estrAppendEscaped(diffEstring, pTempString);
					estrDestroy(&pTempString);
					estrAppend2(diffEstring,"\"");
				xcase TRANSOP_CREATE:
					opString = "create";
				case TRANSOP_DESTROY:					
					if (!opString) opString = "destroy";
					estrConcatf(diffEstring,"%s %s",opString,fieldEntry->pathEString);
					if (!(table[column].type & TOK_EARRAY))
					{
						// nothing extra needed
					}
					else if (fieldEntry->valueEString[0] == '"')
					{
						estrConcatf(diffEstring,"[%s]",fieldEntry->valueEString);
					}
					else
					{
						estrConcatf(diffEstring,"[\"%s\"]",fieldEntry->valueEString);
					}
				xdefault:		
					break;
			}
		}

	}
	return 1;
}


// Execute a field (set/get/create) command
int FieldCommandExecuteCB(TransactionCommand *cmd)
{
	bool bResult;
	size_t transactLength = 0;

	Container *pObject = GetLockedContainerEx(cmd->objectType,cmd->objectID,cmd->transactionID, false, false, true);

	if (!pObject || !IsContainerOwnedByMe(pObject))
	{
		estrPrintf(cmd->returnString,"Can't call field commands on a server that doesn't own the container");
		return 0;
	}

	if (cmd->bCheckingIfPossible)
	{
		return true;
	}

	objContainerPrepareForModify(pObject);
	objContainerCommitNotify(pObject,cmd->fieldEntries,true);

	objUpdateIndexRemove(pObject, cmd->fieldEntries, NULL);

	bResult = ExecuteFieldTransaction(cmd,cmd->returnString,cmd->transactionID?&cmd->diffString:NULL); 

	objUpdateIndexInsert(pObject, cmd->fieldEntries, NULL);

	if (bResult)
	{
		if (estrLength(&cmd->diffString) > 0)
		{
			objContainerCommitNotify(pObject,cmd->fieldEntries,false);
			if (CommitShouldModify(cmd))
			{
				objContainerMarkModified(pObject);
			}
			estrPrintf(&cmd->pDatabaseReturnData->pString1,"dbUpdateContainer %u %u ",cmd->objectType,cmd->objectID);
			estrConcatf(&cmd->pDatabaseReturnData->pString1,"comment %s %u %u\n", cmd->pTransactionName, objServerType(), objServerID());
			estrAppend(&cmd->pDatabaseReturnData->pString1,&cmd->diffString);	
		}
		return 1;
	}
	else
	{
		return 0;
	}
}

// Gets the value of a field
AUTO_COMMAND ACMD_NAME(set) ACMD_LIST(gObjectTransactionCmdList);
int ParseSetField(TransactionCommand *trCommand, ACMD_SENTENCE command)
{
	if (!DecodePathTransactLine(TRANSOP_SET,command,trCommand))
	{
		return 0;
	}
	trCommand->executeCB = FieldCommandExecuteCB;
	trCommand->checkLockCB = CheckFieldLockCB;
	trCommand->lockCB = FieldLockCB;
	trCommand->commitCB = FieldCommitCB;
	trCommand->revertCB = FieldRevertCB;

	return 1;
}


// Creates a subfield
AUTO_COMMAND ACMD_NAME(create) ACMD_LIST(gObjectTransactionCmdList);
int ParseCreateField(TransactionCommand *trCommand, ACMD_SENTENCE command)
{
	if (!DecodePathTransactLine(TRANSOP_CREATE,command,trCommand))
	{
		return 0;
	}

	trCommand->executeCB = FieldCommandExecuteCB;
	trCommand->checkLockCB = CheckFieldLockCB;
	trCommand->lockCB = FieldLockCB;
	trCommand->commitCB = FieldCommitCB;
	trCommand->revertCB = FieldRevertCB;

	return 1;
}


// Destroys a subfield
AUTO_COMMAND ACMD_NAME(destroy) ACMD_LIST(gObjectTransactionCmdList);
int ParseDestroyField(TransactionCommand *trCommand, ACMD_SENTENCE command)
{
	if (!DecodePathTransactLine(TRANSOP_DESTROY,command,trCommand))
	{
		return 0;
	}

	trCommand->executeCB = FieldCommandExecuteCB;
	trCommand->checkLockCB = CheckFieldLockCB;
	trCommand->lockCB = FieldLockCB;
	trCommand->commitCB = FieldCommitCB;
	trCommand->revertCB = FieldRevertCB;

	return 1;
}


// Gets the value of a field
AUTO_COMMAND ACMD_NAME(get) ACMD_LIST(gObjectTransactionCmdList);
int ParseGetField(TransactionCommand *trCommand, ACMD_SENTENCE command)
{
	if (!DecodePathTransactLine(TRANSOP_GET,command,trCommand))
	{
		return 0;
	}

	trCommand->executeCB = FieldCommandExecuteCB;

	return 1;
}

// Auto Transaction support

#define FAST_LOCAL_COPY_DIFF_CUTOFF 5000

typedef struct AutoTransAcquireThreadData
{
	char *diffString;
	char *curDiffString;
} AutoTransAcquireThreadData;

// Get the field values requested 
int AutoTransAcquireFieldsCB(TransactionCommand *cmd)
{
	AutoTransAcquireThreadData *threadData;
	int i;
	void *pFastBackupCopy;
	void *pFastModifyCopy;
	char *tempResultString = NULL;
	char **resultString;
	Container *baseContainer;
	LocalContainerInfo* containerInfo;

	STATIC_THREAD_ALLOC(threadData);

	PERFINFO_AUTO_START_FUNC();

	baseContainer = GetBaseContainer(cmd->objectType,cmd->objectID,cmd->transactionID);
	pFastBackupCopy = GetFastLocalBackupCopy(cmd, cmd->objectType, cmd->objectID);
	pFastModifyCopy = GetFastLocalModifyCopy(cmd, cmd->objectType, cmd->objectID);

	devassert(pFastBackupCopy && pFastModifyCopy || !pFastBackupCopy && !pFastModifyCopy);

	if(pFastBackupCopy && !cmd->bCheckingIfPossible)
	{
		containerInfo = CreateLocalContainerInfo(cmd, cmd->pTransVarName1, cmd->objectType, cmd->objectID);
		eaSetCapacity(&containerInfo->lockedFields, eaSize(&cmd->fieldEntries));
	}

	if (cmd->returnString)
	{
		resultString = cmd->returnString;
	}
	else
	{
		resultString = &tempResultString;
	}

	estrClear(&threadData->diffString);

/*	if (eaSize(&cmd->fieldEntries) == 0)
	{
		if (cmd->returnString) 
		{
			devassertmsgf(0, "Transaction %d does not lock any fields! This is invalid", cmd->transactionID);
			estrPrintf(cmd->returnString, "Container is used without any locked fields! This is invalid");
		}
		PERFINFO_AUTO_STOP();
		return 0;
	}*/

	for (i = 0; i < eaSize(&cmd->fieldEntries); i++)
	{
		ObjectPathOperation *fieldEntry = cmd->fieldEntries[i];
		ParseTable* table;
		int column;
		void *object;
		int index;
		int flags = TOK_PERSIST;

		int success;

		estrClear(&threadData->curDiffString);

		success = GetLockedField(baseContainer,fieldEntry->pathEString,&table,&column,&object,&index,resultString,&threadData->curDiffString);

		if (!success)
		{
			if (fieldEntry->op == TRANSOP_GET_NULL_OKAY && 
				resultString && *resultString && 
				(strStartsWith(*resultString, PARSERRESOLVE_KEYED_INDEX_NOT_FOUND_SHORT) || strStartsWith(*resultString, PARSERRESOLVE_TRAVERSE_NULL_SHORT) || strStartsWith(*resultString, PARSERRESOLVE_EMPTY_KEY)))
			{
				// It's okay in this specific case for it to be null, because we tried to look up an indexed
				//thing in an earray and it wasn't there, but we're in the NULL_OK case, so that's fine. BUT, we
				//want to make sure to create any necessary parent structs there might be in the remote case
				if (estrLength(&threadData->curDiffString) && !cmd->bCheckingIfPossible && !pFastBackupCopy)
				{
					estrConcatf(&threadData->diffString, "%s\n", threadData->curDiffString);
				}

				//in the local case, we want to make sure we add this field to our locked fields so that
				//during diffing after the transaction runs, we'll know to check if this field was added
				if (!cmd->bCheckingIfPossible && pFastBackupCopy)
				{
					eaPush(&containerInfo->lockedFields, strdup(fieldEntry->pathEString));
				}
				continue;
			}
			Errorf("Transaction '%s' failed to lock fields.  This only happens if there is a schema problem or if a programmer sets up a transaction to lock a field that is inside an optional structure, and the optional structure is not present.  Detailed error is: %s", cmd->pTransactionName, *resultString);
			if (tempResultString) estrDestroy(&tempResultString);
			return 0;
		}
		if (!cmd->bCheckingIfPossible)
		{
			void *oldStruct = NULL;
			int oldStructIndex = index;

			if(pFastBackupCopy)
			{
				eaPush(&containerInfo->lockedFields, strdup(fieldEntry->pathEString));
				if(pFastModifyCopy)
				{
					ParseTable* dstTable;
					int dstColumn;
					void *dstObject;
					bool result;
					int pathFlags = OBJPATHFLAG_DONTLOOKUPROOTPATH;

					result = ParserResolvePathEx(fieldEntry->pathEString,baseContainer->containerSchema->classParse,
						pFastModifyCopy,&dstTable,&dstColumn,&dstObject,&oldStructIndex,NULL,NULL,NULL,NULL,pathFlags);

					if(!result)
					{
						// this failure means the local modify copy doesn't have the structs it needs, so create
						// the structs GetLockedField() says are along the path
						objPathParseAndApplyOperations(baseContainer->containerSchema->classParse, pFastModifyCopy, threadData->curDiffString);

						result = ParserResolvePathEx(fieldEntry->pathEString,baseContainer->containerSchema->classParse,
							pFastModifyCopy,&dstTable,&dstColumn,&dstObject,&oldStructIndex,NULL,NULL,NULL,NULL,pathFlags);

						devassert(result);
					}

					devassert(dstTable == table);
					devassert(dstColumn == column);
					oldStruct = dstObject;
				}
				else
				{
					// if you have a fast backup copy but no fast modify copy, it'll StructClone in RunAutoTransCB anyway
					continue;
				}
			}
			else
				estrConcat(&threadData->diffString, threadData->curDiffString, estrLength(&threadData->curDiffString));

			//verify that the new and old text diffing produce identical results
			if (oldStruct == NULL)
			{
				ParserTextDiffWithNull_Verify(&threadData->diffString,table,column,index,object,fieldEntry->pathEString, (int)strlen(fieldEntry->pathEString), TOK_PERSIST,TOK_NO_TRANSACT,0);
			}
			else
			{
				ParserWriteTextDiff(&threadData->diffString,table,column,oldStructIndex,index,oldStruct,object,fieldEntry->pathEString,TOK_PERSIST,TOK_NO_TRANSACT,0);
			}
		}
	}

	if (tempResultString) estrDestroy(&tempResultString);
	if (cmd->bCheckingIfPossible)
	{
		PERFINFO_AUTO_STOP();
		return 1;
	}

	//in the remote case, include all "always included" fields
	if (!pFastBackupCopy)
	{
		char ***pppAlwaysIncludeList = GetListOfFieldsThatAreAlwaysIncluded(baseContainer->containerSchema->classParse);
		for (i=0; i < eaSize(pppAlwaysIncludeList); i++)
		{
			ParseTable* table;
			int column;
			void *object;
			int index;
			int success;
	
			estrClear(&threadData->curDiffString);

			success = GetLockedField(baseContainer,(*pppAlwaysIncludeList)[i],&table,&column,&object,&index,resultString,&threadData->curDiffString);
	
			if (!success)
			{
				AssertOrAlert("ALWAYS_INCLUDE_FIELD_ABSENT", "Couldn't get always-include field %s during transaction %s. This is very bad",
					(*pppAlwaysIncludeList)[i], cmd->pTransactionName);
				PERFINFO_AUTO_STOP();
				return 0;
			}
			estrConcat(&threadData->diffString, threadData->curDiffString, estrLength(&threadData->curDiffString));

			ParserTextDiffWithNull_Verify(&threadData->diffString,table,column,index,object,(*pppAlwaysIncludeList)[i], (int)strlen((*pppAlwaysIncludeList)[i]), TOK_PERSIST,TOK_NO_TRANSACT,0);
		}
	}



	if (pFastBackupCopy)
	{
		containerInfo->localDiffString = strdup(threadData->diffString);
	}
	else
	{
		SetTransactionVariableEString(objLocalManager(),cmd->transactionID,cmd->pTransVarName1,&threadData->diffString);
	}

	PERFINFO_AUTO_STOP();
	return 1;
}

// Special commit
int FieldAutoTransAcquireCommitCB(TransactionCommand *cmd)
{
	int i;

	// Do a full container lock so it can be unlocked at the apply step when the change actually occurs
	LockContainer(cmd->transactionID,cmd->objectType,
		cmd->objectID);
	
	// Unlock individual fields now because we only have the data in this transaction
	for (i = 0; i < eaSize(&cmd->fieldEntries); i++)
	{
		UnlockField(cmd->transactionID,cmd->objectType,
			cmd->objectID,
			cmd->fieldEntries[i]->pathEString);
	}

	return 1;
}



// Acquires and locks fields for an auto transaction. DO NOT CALL MANUALLY
AUTO_COMMAND ACMD_NAME(AutoTransAcquireFields) ACMD_LIST(gObjectTransactionCmdList);
int ParseAutoTransAcquireFields(TransactionCommand *trCommand, char *trvar, char *pExecutionServer, ACMD_SENTENCE command)
{
	DecodeFieldLocksForAutoTransAcquireFields(command,trCommand);

	if(IsThisObjectDB())
	{
		unsigned int length = (unsigned int)strlen(trvar);
		estrHeapCreate(&trCommand->pTransVarName1, MAX(length, 64), CRYPTIC_CHURN_HEAP);
	}
	estrCopy2(&trCommand->pTransVarName1, trvar);
	trCommand->bLockRequired = true;

	trCommand->executeCB = AutoTransAcquireFieldsCB;
	trCommand->checkLockCB = CheckFieldLockCB;
	trCommand->lockCB = FieldLockNoBackupCB;
	trCommand->commitCB = FieldAutoTransAcquireCommitCB;
	trCommand->revertCB = FieldRevertNoBackupCB;

	ParseGlobalTypeAndID(pExecutionServer, &trCommand->autoTransExecutionServerType, &trCommand->autoTransExecutionServerID);

	return 1;
}


// Get an entire container
// Not thread/recursion safe
int AutoTransAcquireContainerCB(TransactionCommand *command)
{
	AutoTransAcquireThreadData *threadData;
	Container *pObject = GetLockedContainer(command->objectType,command->objectID,command->transactionID, false);
	STATIC_THREAD_ALLOC(threadData);

	if (!pObject)
	{
		return 0;
	}
	if (command->bCheckingIfPossible)
	{
		return 1;
	}

	estrClear(&threadData->diffString);

	if (GetFastLocalBackupCopy(command, command->objectType, command->objectID))
	{
		LocalContainerInfo *containerInfo;
		containerInfo = CreateLocalContainerInfo(command, command->pTransVarName1, command->objectType, command->objectID);
		containerInfo->fullContainer = true;
	}
	else
	{	
		// If we're using the fast backup copy, don't send it
		StructTextDiffWithNull_Verify(&threadData->diffString,pObject->containerSchema->classParse,pObject->containerData,NULL, 0, TOK_PERSIST,0,0);
		SetTransactionVariableEString(objLocalManager(),command->transactionID,command->pTransVarName1,&threadData->diffString);
	}

	return 1;
}

// Acquires and locks fields for an auto transaction. DO NOT CALL MANUALLY
AUTO_COMMAND ACMD_NAME(AutoTransAcquireContainer) ACMD_LIST(gObjectTransactionCmdList);
int ParseAutoTransAcquireContainer(TransactionCommand *trCommand, char *trvar, char *pExecutionServer)
{
	estrCopy2(&trCommand->pTransVarName1, trvar);
	trCommand->bLockRequired = true;

	trCommand->executeCB = AutoTransAcquireContainerCB;
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockNoBackupCB;
	//trCommand->commitCB = FullContainerCommitNoBackupCB; // AutoTransUpdate will unlock it
	trCommand->revertCB = FullContainerRevertNoBackupCB;

	ParseGlobalTypeAndID(pExecutionServer, &trCommand->autoTransExecutionServerType, &trCommand->autoTransExecutionServerID);

	return 1;
}

// Apply the changes of the auto transaction. Nothing actually happens until commit
int AutoTransUpdateCB(TransactionCommand *cmd)
{
	
	if (cmd->bCheckingIfPossible || estrLength(&cmd->diffString) == 0)
	{
		return 1;
	}
	if(IsThisObjectDB())
		estrHeapCreate(&cmd->pDatabaseReturnData->pString1, 2 * estrLength(&cmd->diffString), CRYPTIC_CHURN_HEAP);
	estrPrintf(&cmd->pDatabaseReturnData->pString1,"dbUpdateContainer %u %u ",cmd->objectType,cmd->objectID);
	estrConcatf(&cmd->pDatabaseReturnData->pString1,"comment %s %u %u\n", cmd->pTransactionName, cmd->autoTransExecutionServerType, cmd->autoTransExecutionServerID);
	estrAppend(&cmd->pDatabaseReturnData->pString1,&cmd->diffString);

	return 1;
}

// Special commit function for auto transactions
int AutoTransUpdateCommitCB(TransactionCommand *cmd)
{
	Container *object = objGetContainer(cmd->objectType,cmd->objectID);
	if (cmd->diffString && cmd->diffString[0] && object && CommitShouldModify(cmd))
	{
		// Don't modify it here on the ObjectDB, because the dbupdatestring will take care of it
		objModifyContainer(object,cmd->diffString);
	}

	// This lock is either the original lock for AcquireContainer, or the one added by FieldAutoTransAcquireCommit
	UnlockContainer(cmd->transactionID,cmd->objectType,cmd->objectID);

	return 1;
}


// Updates fields on an object that are assumed to be already locked. DO NOT CALL MANUALLY
AUTO_COMMAND ACMD_NAME(AutoTransUpdate) ACMD_LIST(gObjectTransactionCmdList);
int ParseAutoTransUpdate(TransactionCommand *trCommand, char *pExecutionServer, ACMD_SENTENCE command)
{
	const char *fullCommand = TransactionParseStringArgument(trCommand,command);

	if(IsThisObjectDB())
	{
		unsigned int length = (unsigned int)strlen(fullCommand);
		estrHeapCreate(&trCommand->diffString, MAX(length, 64), CRYPTIC_CHURN_HEAP);
	}
	estrCopy2(&trCommand->diffString, fullCommand);

	trCommand->bLockRequired = true;

	trCommand->executeCB = AutoTransUpdateCB;
	trCommand->commitCB = AutoTransUpdateCommitCB;

	ParseGlobalTypeAndID(pExecutionServer, &trCommand->autoTransExecutionServerType, &trCommand->autoTransExecutionServerID);


	return 1;
}

// Gets owner data and store it in a transaction variable
int ReturnOwnerCB(TransactionCommand *command)
{
	Container *pObject = objGetContainer(command->objectType,command->objectID);

	if (!pObject || !IsContainerOwnedByMe(pObject))
	{
		estrPrintf(command->returnString,"Unknown");
		return 0;
	}
	if (!command->bCheckingIfPossible)
	{
		estrPrintf(command->returnString,"%s %d",GlobalTypeToName(objServerType()),objServerID());
	}
	return 1;
}

// Returns owner information (type,id)
AUTO_COMMAND ACMD_NAME(ReturnOwner) ACMD_LIST(gObjectTransactionCmdList);
int ParseReturnOwner(TransactionCommand *trCommand)
{
	trCommand->executeCB = ReturnOwnerCB;

	return 1;
}



// Gets owner data and store it in a transaction variable
int GetOwnerCB(TransactionCommand *command)
{
	Container *pObject = objGetContainer(command->objectType,command->objectID);

	if (!pObject)
	{
		estrPrintf(command->returnString,"InvalidObject");
		return 0;
	}
	if (command->pTransVarName1 == NULL || command->pTransVarName2 == NULL)
	{
		estrPrintf(command->returnString,"getowner expects a valid TRVAR as an argument");
		return 0;
	}
	if (!command->bCheckingIfPossible)
	{
		char numString[100];

		estrPrintf(command->returnString,"%s %d",GlobalTypeToName(objServerType()),objServerID());

		sprintf(numString,"%d",objServerID());

		SetTransactionVariableString(objLocalManager(),command->transactionID,command->pTransVarName1,GlobalTypeToName(objServerType()));
		SetTransactionVariableString(objLocalManager(),command->transactionID,command->pTransVarName2,numString);
	}
	return 1;
}

// Get owner information and store to variables (typevar, idvar, type, id)
AUTO_COMMAND ACMD_NAME(GetOwner) ACMD_LIST(gObjectTransactionCmdList);
int ParseGetOwner(TransactionCommand *trCommand, char *trvar1, char *trvar2)
{
	estrCopy2(&trCommand->pTransVarName1, trvar1);
	estrCopy2(&trCommand->pTransVarName2, trvar2);

	trCommand->executeCB = GetOwnerCB;

	return 1;
}

// Fast local copies for auto transactions

int bDisableFastLocalCopies = 0;

AUTO_CMD_INT(bDisableFastLocalCopies, DisableFastLocalCopies) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE;


static bool sbAllowMixedLocalAndRemoteAutoTransactions = true;
AUTO_CMD_INT(sbAllowMixedLocalAndRemoteAutoTransactions, AllowMixedLocalAndRemoteAutoTransactions) ACMD_CMDLINE;

bool AreFastLocalCopiesAllowedForCommand(TransactionCommand *pCommand)
{
	if (bDisableFastLocalCopies)
	{
		return false;
	}

	if (IsLocalTransactionCurrentlyHappening(objLocalManager()))
	{
		return true;
	}

	if (pCommand->autoTransExecutionServerID == GetAppGlobalID() && pCommand->autoTransExecutionServerType == GetAppGlobalType() && sbAllowMixedLocalAndRemoteAutoTransactions)
	{
		return true;
	}

	return false;
}

#if 0
int bFastLocalCopiesEnabled = 0;

bool AreFastLocalCopiesActive(TransactionCommand *pCommand)
{
	return bFastLocalCopiesEnabled && IsLocalTransactionCurrentlyHappening(objLocalManager());
}
#endif

typedef struct FastLocalCopyCBStruct
{
	GlobalType registeredType;
	FastLocalCopyCB backupCB;
	FastLocalCopyCB modifyCB;
} FastLocalCopyCBStruct;

StashTable gFastLocalCopyCBTable;

// CRITICAL_SECTION for accessing gFastLocalCopyCBTable

static CRITICAL_SECTION sFastLocalCopyCBTableMutex = {0};

static void EnterFastLocalCopyCBTableCriticalSection(void)
{
	ATOMIC_INIT_BEGIN;

	InitializeCriticalSection(&sFastLocalCopyCBTableMutex); 

	ATOMIC_INIT_END;

	EnterCriticalSection(&sFastLocalCopyCBTableMutex);
}

static void LeaveFastLocalCopyCBTableCriticalSection(void)
{
	LeaveCriticalSection(&sFastLocalCopyCBTableMutex);
}


//have one cache of local container info per transaction ID, meaning that TransactionCommands have to share them
//note that we don't actually use textparser to manipulate this struct, but it
//can be servermonitored
AUTO_STRUCT;
typedef struct LocalContainerInfoCache
{
	U32 iTransactionID; AST(KEY)
	const char *pTransactionName; AST(POOL_STRING)
	int iRefCount;
	LocalContainerInfo **ppLocalInfos;
} LocalContainerInfoCache;
 
StashTable sLocalContainerInfoCachesByID = NULL;

// CRITICAL_SECTION for accessing sLocalContainerInfoCachesByID

static CRITICAL_SECTION sLocalContainerInfoCachesByIDMutex = {0};

static void EnterLocalContainerInfoCachesByIDCriticalSection(void)
{
	ATOMIC_INIT_BEGIN;

	InitializeCriticalSection(&sLocalContainerInfoCachesByIDMutex); 

	ATOMIC_INIT_END;

	EnterCriticalSection(&sLocalContainerInfoCachesByIDMutex);
}

static void LeaveLocalContainerInfoCachesByIDCriticalSection(void)
{
	LeaveCriticalSection(&sLocalContainerInfoCachesByIDMutex);
}


LocalContainerInfo* GetLocalContainerInfo(TransactionCommand *pCommand, const char *varName)
{
	int i;


	if (!pCommand->pLocalContainerInfoCache)
	{
		bool bFound;
		EnterLocalContainerInfoCachesByIDCriticalSection();
		bFound = stashIntFindPointer(sLocalContainerInfoCachesByID, pCommand->transactionID, &pCommand->pLocalContainerInfoCache);
		LeaveLocalContainerInfoCachesByIDCriticalSection();
		if (bFound)
		{
			pCommand->pLocalContainerInfoCache->iRefCount++;
		}
		else
		{
			return NULL;
		}
	}

	if (varName)
	{
		for (i = 0; i < eaSize(&pCommand->pLocalContainerInfoCache->ppLocalInfos); i++)
		{
			if (stricmp(varName, pCommand->pLocalContainerInfoCache->ppLocalInfos[i]->varName) == 0)
			{
				return pCommand->pLocalContainerInfoCache->ppLocalInfos[i];
			}
		}
	}
	return NULL;
}


LocalContainerInfo* CreateLocalContainerInfo(TransactionCommand *pCommand, const char *varName, GlobalType type, ContainerID id)
{
	LocalContainerInfoCache *pCache;
	LocalContainerInfo *pInfo;

	assert(pCommand->transactionID);

	PERFINFO_AUTO_START_FUNC();

	if (!pCommand->pLocalContainerInfoCache)
	{
		EnterLocalContainerInfoCachesByIDCriticalSection();
		if (!sLocalContainerInfoCachesByID)
		{
			sLocalContainerInfoCachesByID = stashTableCreateInt(16);
			resRegisterDictionaryForStashTable("LocalContainerInfoCaches", RESCATEGORY_SYSTEM, 0, sLocalContainerInfoCachesByID, parse_LocalContainerInfoCache);
		}

		if (!stashIntFindPointer(sLocalContainerInfoCachesByID, pCommand->transactionID, &pCache))
		{
			pCache = calloc(sizeof(LocalContainerInfoCache), 1);
			pCache->iTransactionID = pCommand->transactionID;
			pCache->pTransactionName = pCommand->pTransactionName;
			stashIntAddPointer(sLocalContainerInfoCachesByID, pCache->iTransactionID, pCache, false);
			if (stashGetMaxSize(sLocalContainerInfoCachesByID) > MAX_LOCAL_CONTAINER_INFOS_BEFORE_ALERT)
			{
				static bool sbOnce = false;

				if (!sbOnce)
				{
					PointerCounter *pCounter = PointerCounter_Create();
					PointerCounterResult **ppResults = NULL;
					char *pResultString = NULL;
					sbOnce = true;
					FOR_EACH_IN_STASHTABLE(sLocalContainerInfoCachesByID, LocalContainerInfoCache, pCountingCache)
					{
						PointerCounter_AddSome(pCounter, pCountingCache->pTransactionName, 1);
					}
					FOR_EACH_END;

					PointerCounter_GetMostCommon(pCounter, 3, &ppResults);
					PointerCounter_Destroy(&pCounter);

					estrPrintf(&pResultString, "Resized our LCI stash table > %d, leak suspect. Most common %d transactions:\n",
						MAX_LOCAL_CONTAINER_INFOS_BEFORE_ALERT, eaSize(&ppResults));

					FOR_EACH_IN_EARRAY(ppResults, PointerCounterResult, pSingleResult)
					{
						estrConcatf(&pResultString, "%d occurrences of %s. ", pSingleResult->iCount, (char*)(pSingleResult->pPtr));
					}
					FOR_EACH_END;

					CRITICAL_NETOPS_ALERT("LOCAL_CONT_INFO_LEAK", "%s", pResultString);

					estrDestroy(&pResultString);
					eaDestroyEx(&ppResults, NULL);
				}
			}
		}
		LeaveLocalContainerInfoCachesByIDCriticalSection();

		pCache->iRefCount++;
		pCommand->pLocalContainerInfoCache = pCache;
	}

	


//	assert(AreFastLocalCopiesActive());
	assert(!GetLocalContainerInfo(pCommand, varName));
	pInfo = calloc(sizeof(LocalContainerInfo),1);
	strcpy(pInfo->varName, varName);
	pInfo->conRef.containerType = type;
	pInfo->conRef.containerID = id;
	eaPush(&pCommand->pLocalContainerInfoCache->ppLocalInfos, pInfo);

	PERFINFO_AUTO_STOP();

	return pInfo;
}


void DestroyLocalContainerInfo(LocalContainerInfo *info)
{
	eaDestroyEx(&info->lockedFields, NULL);
	SAFE_FREE(info->localDiffString);
	free(info);
}

void LocalContainerInfoCache_DecreaseRefCount(LocalContainerInfoCache *pCache)
{
	if (!pCache->iRefCount)
	{
		AssertOrAlert("LCIC_REFCOUNT_CORRUPTION", "Cache for command %s trying to decrease refcount that is already 0",
			pCache->pTransactionName);
		return;
	}

	pCache->iRefCount--;

	if (pCache->iRefCount == 0)
	{
		eaDestroyEx(&pCache->ppLocalInfos, DestroyLocalContainerInfo);
		EnterLocalContainerInfoCachesByIDCriticalSection();
		stashIntRemovePointer(sLocalContainerInfoCachesByID, pCache->iTransactionID, NULL);
		LeaveLocalContainerInfoCachesByIDCriticalSection();
		free(pCache);
	}
}



void *GetFastLocalBackupCopy(TransactionCommand *pCommand, GlobalType type, ContainerID id)
{
	void *pObject = NULL;
	FastLocalCopyCBStruct *pFound;
	PERFINFO_AUTO_START_FUNC();
	EnterFastLocalCopyCBTableCriticalSection();
	if (gFastLocalCopyCBTable && AreFastLocalCopiesAllowedForCommand(pCommand))
	{
		if (stashIntFindPointer(gFastLocalCopyCBTable, type, &pFound))
		{
			if (pFound->backupCB)
			{				
				pObject = pFound->backupCB(type, id);
				if (pObject)
				{
					ParseTable *pTable = objFindContainerSchema(type)->classParse;
					int foundID = 0;

					assert(objGetKeyInt(pTable, pObject, &foundID) && foundID == id);
				}
			}
		}
	}
	LeaveFastLocalCopyCBTableCriticalSection();
	PERFINFO_AUTO_STOP();
	return pObject;
}

void *GetFastLocalModifyCopy(TransactionCommand *pCommand, GlobalType type, ContainerID id)
{
	void *pObject = NULL;
	FastLocalCopyCBStruct *pFound;
	PERFINFO_AUTO_START_FUNC();
	EnterFastLocalCopyCBTableCriticalSection();
	if (gFastLocalCopyCBTable && AreFastLocalCopiesAllowedForCommand(pCommand))
	{
		if (stashIntFindPointer(gFastLocalCopyCBTable, type, &pFound))
		{
			if (pFound->modifyCB)
			{
				pObject = pFound->modifyCB(type, id);
				if (pObject)
				{
					ParseTable *pTable = objFindContainerSchema(type)->classParse;
					int foundID = 0;

					assert(objGetKeyInt(pTable, pObject, &foundID) && foundID == id);
				}
			}
		}
	}
	LeaveFastLocalCopyCBTableCriticalSection();
	PERFINFO_AUTO_STOP();
	return pObject;
}

void RegisterFastLocalCopyCB(GlobalType type, FastLocalCopyCB backupCopy, FastLocalCopyCB modifyCopy)
{
	FastLocalCopyCBStruct *pFound;
	EnterFastLocalCopyCBTableCriticalSection();
	if (!gFastLocalCopyCBTable)
	{
		gFastLocalCopyCBTable = stashTableCreateInt(8);
	}
	if (stashIntFindPointer(gFastLocalCopyCBTable, type, &pFound))
	{
		assertmsgf(0,"Can't register two fast local copy callbacks for type %d", type);
	}
	pFound = calloc(sizeof(FastLocalCopyCBStruct), 1);
	pFound->registeredType = type;
	pFound->backupCB = backupCopy;
	pFound->modifyCB = modifyCopy;
	stashIntAddPointer(gFastLocalCopyCBTable, type, pFound, false);
	LeaveFastLocalCopyCBTableCriticalSection();
}

#include "ObjTransactionCommands_h_ast.c"
#include "ObjTransactionCommands_c_ast.c"
