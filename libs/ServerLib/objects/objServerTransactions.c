/***************************************************************************



***************************************************************************/


#include "objTransactions.h"
#include "objTransactionCommands.h"
#include "objLocks.h"

#include "AutoTransDefs.h"
#include "Expression.h"
#include "file.h"
#include "net.h"
#include "LocalTransactionManager_Internal.h"
#include "logging.h"
#include "MemAlloc.h"
#include "strings_opt.h"
#include "stringutil.h"
#include "structnet.h"
#include "timing.h"
#include "alerts.h"
#include "timing.h"
#include "winInclude.h"
#include "serverLib.h"
#include "timedCallback.h"
#include "AutoTransSupport.h"
#include "tokenstore_inline.h"
#include "utilitiesLib.h"

// This is for handling the global transaction system commands that are addressed
// to the server, and not to objects it owns

// Create/destroy

typedef struct SpecialTransactionCallbackStruct
{
	SpecialTransactionCallbackType transactionType;
	GlobalType containerType;
	SpecialTransactionCallback cbFunc;
} SpecialTransactionCallbackStruct;


static SpecialTransactionCallbackStruct **sServerTransactionStructs;

bool DEFAULT_LATELINK_objTransactions_MaybeLocallyCopyBackupDataDuringReceive(GlobalType eType, void *pMainObject, void *pBackupObject)
{
	return false;
}

void objRegisterSpecialTransactionCallback(GlobalType containerType, SpecialTransactionCallbackType transactionType, SpecialTransactionCallback cbFunc)
{
	SpecialTransactionCallbackStruct *pStruct;
	int i;

	assert(containerType && cbFunc);

	if (IsThisObjectDB() &&
		(transactionType == TRANSACTION_MOVE_CONTAINER_TO || transactionType == TRANSACTION_RECOVER_CONTAINER_FROM) )
	{
		assertmsg(false, "Cannot register special transaction callbacks on container move from ObjectDB. No backup on lock.");
		return;
	}

	for (i = 0; i < eaSize(&sServerTransactionStructs); i++)
	{
		if (sServerTransactionStructs[i]->cbFunc == cbFunc && 
			sServerTransactionStructs[i]->containerType == containerType &&
			sServerTransactionStructs[i]->transactionType == transactionType)
		{
			return;
		}
	}

	pStruct = calloc(sizeof(SpecialTransactionCallbackStruct),1);
	pStruct->containerType = containerType;
	pStruct->transactionType = transactionType;
	pStruct->cbFunc = cbFunc;
	
	eaPush(&sServerTransactionStructs, pStruct);
}

static int RunSpecialTransactionCallbacks(GlobalType containerType, SpecialTransactionCallbackType transactionType, void *object, void *backupObject, GlobalType locationType, ContainerID locationID, char **resultEString)
{
	int i;
	int result;
	static char *estrFail = NULL;

	for (i = 0; i < eaSize(&sServerTransactionStructs); i++)
	{
		if (sServerTransactionStructs[i]->containerType == containerType &&
			sServerTransactionStructs[i]->transactionType == transactionType)
		{
			estrClear(&estrFail);
			result = sServerTransactionStructs[i]->cbFunc(resultEString, &estrFail, object, backupObject, locationType, locationID);
			if (result != TRANSACTION_OUTCOME_SUCCESS)
			{
				estrCopy(resultEString, &estrFail);
				return 0;
			}
		}
	}
	return 1;
}

// Ensure a container exists or Create it on the local server
// This function MUST be called with a specified container ID, and the lock must occur outside
// ***This should remain virtually identical to Create***
int CreateOrVerifyContainerCBEx(TransactionCommand *command, bool onlyApplyDiffOnCreate)
{
	if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_PERSISTED)
	{
		if (!IsThisObjectDB() && !command->bShardExternalCommand)
		{
			estrPrintf(command->returnString,"You can only create PERSISTED schemas on the objectDB");
			return 0;
		}
	}
	else if (GlobalTypeSchemaType(command->objectType) != SCHEMATYPE_TRANSACTED)
	{
		estrPrintf(command->returnString,"Only Transacted and Persisted schematypes can be created via transactions");
		return 0;
	}

	if (!command->objectID)
	{		
		estrPrintf(command->returnString,"Verify requires ID!");
		return 0;		
	}

	if (!command->bCheckingIfPossible)
	{
		ContainerStore *store = objFindContainerStoreFromType(command->objectType);
		Container *pObject;
		void *pObjectBackup;
		bool bCreatedCon = false;
		ContainerSchema *schema = objFindContainerSchema(command->objectType);
		char numString[128];

		if (!schema)
		{
			estrPrintf(command->returnString,"Invalid Container Type");
			return 0;
		}

		if (!store)
		{
			store = objCreateContainerStore(schema);
		}
		
		// We CANNOT lock here because we have to check the lock earlier
		bCreatedCon = IsContainerBeingCreated(command->objectType,command->objectID,command->transactionID);

		pObject = GetLockedContainer(command->objectType,command->objectID,command->transactionID, false);		

		if (!pObject)
		{
			estrPrintf(command->returnString,"Can't create temporary container!");
			return 0;
		}

		TransDataBlockClear(command->pDatabaseReturnData);

		pObjectBackup = StructCloneWithCommentVoid(schema->classParse, pObject->containerData, "ObjectBackup created during CreateOrVerifyContainerCB");

		if (command->diffString && command->diffString[0] && (!onlyApplyDiffOnCreate || bCreatedCon))
		{
			if (!objPathParseAndApplyOperations(schema->classParse, pObject->containerData, command->diffString))
			{
				estrPrintf(command->returnString,"objPathParseAndApply failed... are your schemas out of date?");
				return 0;				
			}
		}

		if (!RunSpecialTransactionCallbacks(command->objectType, TRANSACTION_VERIFY_CONTAINER, 
			pObject->containerData, pObjectBackup, 0, 0, command->returnString))
		{
			TransDataBlockClear(command->pDatabaseReturnData);
			estrClear(command->transReturnString);
			StructDestroyVoid(schema->classParse, pObjectBackup);
			return 0;
		}

		if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_TRANSACTED)
		{
			estrPrintf(command->transReturnString,"online %s %u %s %u",GlobalTypeToName(command->objectType),command->objectID,GlobalTypeToName(objServerType()),objServerID());
		}

		if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_PERSISTED)
		{
			int oldLength;
			if(IsThisObjectDB())
				estrHeapCreate(&command->pDatabaseReturnData->pString1, 2*estrLength(&command->diffString), CRYPTIC_CHURN_HEAP);
			estrPrintf(&command->pDatabaseReturnData->pString1,"dbUpdateContainer %u %u ",command->objectType,command->objectID);
			estrConcatf(&command->pDatabaseReturnData->pString1,"comment %s %u %u\n", command->pTransactionName, objServerType(), objServerID());
			oldLength = estrLength(&command->pDatabaseReturnData->pString1);

			if (bCreatedCon)
			{
				StructTextDiffWithNull_Verify(&command->pDatabaseReturnData->pString1, schema->classParse,
					pObject->containerData, NULL, 0, TOK_PERSIST, 0, 0);

			}
			else
			{
				StructWriteTextDiff(&command->pDatabaseReturnData->pString1, schema->classParse, pObjectBackup,
					pObject->containerData, NULL, TOK_PERSIST, 0, 0);
			}

			if (oldLength == estrLength(&command->pDatabaseReturnData->pString1))
			{
				estrClear(&command->pDatabaseReturnData->pString1);
			}
			else
			{
				estrClear(&command->diffString);
				if (bCreatedCon)
				{
					StructTextDiffWithNull_Verify(&command->diffString, schema->classParse,
						pObject->containerData, NULL, 0, TOK_PERSIST, 0, false);
				}
				else
				{
					StructWriteTextDiff(&command->diffString, schema->classParse, pObjectBackup,
						pObject->containerData, NULL, TOK_PERSIST, 0, false);
				}
			}
		}

		sprintf(numString,"%d",command->objectID);
		SetTransactionVariableString(objLocalManager(),command->transactionID,command->pTransVarName1,numString);
		estrCopy2(command->returnString,numString);

		objChangeContainerState(pObject, CONTAINERSTATE_OWNED, objServerType(), objServerID());

		StructDestroyVoid(schema->classParse, pObjectBackup);
	}
	return 1;
}

// Ensure a container exists or Create it on the local server
// This function MUST be called with a specified container ID, and the lock must occur outside
// ***This should remain virtually identical to Create***
// This variant always applies the command diff string to the container
int CreateOrVerifyContainerCB(TransactionCommand *command)
{
    return CreateOrVerifyContainerCBEx(command, false);
}

// Ensure a container exists or Create it on the local server
// This function MUST be called with a specified container ID, and the lock must occur outside
// ***This should remain virtually identical to Create***
// This variant only applies the command diff string to the container if it is being created
int CreateAndInitOrVerifyContainerCB(TransactionCommand *command)
{
    return CreateOrVerifyContainerCBEx(command, true);
}

static ContainerID ReserveContainerIDForCreate(TransactionCommand *command)
{
	ContainerStore *store = objFindContainerStoreFromType(command->objectType);
	ContainerSchema *schema = objFindContainerSchema(command->objectType);
	ContainerID id = 0;

	if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_PERSISTED)
	{
		if (!IsThisObjectDB() && !command->bShardExternalCommand)
		{
			estrPrintf(command->returnString,"You can only create PERSISTED schemas on the objectDB");
			return 0;
		}
	}
	else if (GlobalTypeSchemaType(command->objectType) != SCHEMATYPE_TRANSACTED)
	{
		estrPrintf(command->returnString,"Only Transacted and Persisted schematypes can be created via transactions");
		return 0;
	}

	if (!schema)
	{
		estrPrintf(command->returnString,"Invalid Container Type");
		return 0;
	}

	if (!store)
	{
		store = objCreateContainerStore(schema);
	}

	if (command->objectID)
	{
		estrPrintf(command->returnString,"Create cannot specify ID!");
		return 0;		
	}

	return objReserveNewContainerID(store);
}

// Create a new container on the local server
// This function CANNOT be called with a specified container ID, and locks inside it
// ***If you update this, update Verify!***
int CreateContainerCB(TransactionCommand *command)
{
	if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_PERSISTED)
	{
		if (!IsThisObjectDB() && !command->bShardExternalCommand)
		{
			estrPrintf(command->returnString,"You can only create PERSISTED schemas on the objectDB");
			return 0;
		}
	}
	else if (GlobalTypeSchemaType(command->objectType) != SCHEMATYPE_TRANSACTED)
	{
		estrPrintf(command->returnString,"Only Transacted and Persisted schematypes can be created via transactions");
		return 0;
	}
	if (!command->bCheckingIfPossible)
	{
		Container *pObject;
		void *pObjectBackup;
		ContainerSchema *schema = objFindContainerSchema(command->objectType);
		char numString[128];

		if (!schema)
		{
			estrPrintf(command->returnString,"Invalid Container Type");
			return 0;
		}

		// This is guaranteed to succeed ONLY because we just reserved a unique ID
		FullContainerLockCB(command);

		pObject = GetLockedContainer(command->objectType,command->objectID,command->transactionID, false);		

		if (!pObject)
		{
			estrPrintf(command->returnString,"Can't create temporary container!");
			return 0;
		}

		TransDataBlockClear(command->pDatabaseReturnData);

		pObjectBackup = StructCloneWithCommentVoid(schema->classParse, pObject->containerData, "ObjectBackup created during CreateContainerCB");

		if (command->diffString && command->diffString[0])
		{
			if (!objPathParseAndApplyOperations(schema->classParse, pObject->containerData, command->diffString))
			{
				estrPrintf(command->returnString,"objPathParseAndApply failed... are your schemas out of date?");
				return 0;				
			}
		}
		
		if (!RunSpecialTransactionCallbacks(command->objectType, TRANSACTION_CREATE_CONTAINER, 
			pObject->containerData, pObjectBackup, 0, 0, command->returnString))
		{
			TransDataBlockClear(command->pDatabaseReturnData);
			estrClear(command->transReturnString);
			StructDestroyVoid(schema->classParse, pObjectBackup);
			return 0;
		}
		
		if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_TRANSACTED)
		{
			estrPrintf(command->transReturnString,"online %s %u %s %u",GlobalTypeToName(command->objectType),command->objectID,GlobalTypeToName(objServerType()),objServerID());
		}

		if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_PERSISTED)
		{
			int oldLength;
			if(IsThisObjectDB())
				estrHeapCreate(&command->pDatabaseReturnData->pString1, 2*estrLength(&command->diffString), CRYPTIC_CHURN_HEAP);
			estrPrintf(&command->pDatabaseReturnData->pString1,"dbUpdateContainer %u %u ",command->objectType,command->objectID);
			estrConcatf(&command->pDatabaseReturnData->pString1,"comment %s %u %u\n", command->pTransactionName, objServerType(), objServerID());
			oldLength = estrLength(&command->pDatabaseReturnData->pString1);

			StructTextDiffWithNull_Verify(&command->pDatabaseReturnData->pString1, schema->classParse, 
				pObject->containerData, NULL, 0, TOK_PERSIST, 0, 0);

			if (oldLength == estrLength(&command->pDatabaseReturnData->pString1))
			{
				estrClear(&command->pDatabaseReturnData->pString1);
			}
			else
			{
				estrClear(&command->diffString);
				StructTextDiffWithNull_Verify(&command->diffString, schema->classParse,
					pObject->containerData, NULL, 0, TOK_PERSIST, 0, 0);
			}
		}

		sprintf(numString,"%d",command->objectID);
		SetTransactionVariableString(objLocalManager(),command->transactionID,command->pTransVarName1,numString);
		estrCopy2(command->returnString,numString);

		objChangeContainerState(pObject, CONTAINERSTATE_OWNED, objServerType(), objServerID());

		StructDestroyVoid(schema->classParse, pObjectBackup);
	}
	return 1;
}

// Create a container on this server
AUTO_COMMAND ACMD_NAME(CreateContainer) ACMD_LIST(gServerTransactionCmdList);
int ParseCreateContainer(TransactionCommand *trCommand, char *trVar, char *containerTypeName)
{	
	trCommand->objectType = NameToGlobalType(TransactionParseStringArgument(trCommand,containerTypeName));

	if (!trVar || !trVar[0])
	{
		return 0;
	}

	trCommand->objectID = ReserveContainerIDForCreate(trCommand);

	if (!trCommand->objectID)
	{
		return 0;
	}

	trCommand->diffString = NULL;
	estrCopy2(&trCommand->pTransVarName1, trVar);


	trCommand->bLockRequired = true;
	trCommand->executeCB = CreateContainerCB;
	// Commit but no lock, because create will lock it for us
	trCommand->commitCB = FullContainerCommitCB;
	trCommand->revertCB = FullContainerRevertCB;

	return 1;
}

// Verify that a container exists on this server, create it if it doesn't.
AUTO_COMMAND ACMD_NAME(VerifyContainer) ACMD_LIST(gServerTransactionCmdList);
int ParseVerifyContainer(TransactionCommand *trCommand, char *trVar, char *containerTypeName, char *containerID)
{	
	trCommand->objectID = TransactionParseIntArgument(trCommand,containerID);
	trCommand->objectType = NameToGlobalType(TransactionParseStringArgument(trCommand,containerTypeName));

	if (!trVar || !trVar[0])
	{
		return 0;
	}

	trCommand->diffString = NULL;
	estrCopy2(&trCommand->pTransVarName1, trVar);

	trCommand->bLockRequired = true;
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockCB; // We lock outside the verify because we already know the ID
	trCommand->executeCB = CreateOrVerifyContainerCB;
	trCommand->commitCB = FullContainerCommitCB;
	trCommand->revertCB = FullContainerRevertCB;

	return 1;
}




// Create a container on this server, using initial data
AUTO_COMMAND ACMD_NAME(CreateContainerUsingData) ACMD_LIST(gServerTransactionCmdList);
int ParseCreateContainerUsingData(TransactionCommand *trCommand, char *trVar, char *containerTypeName, ACMD_SENTENCE containerData)
{
	trCommand->objectType = NameToGlobalType(TransactionParseStringArgument(trCommand,containerTypeName));

	if (!trVar || !trVar[0])
	{
		return 0;
	}

	trCommand->objectID = ReserveContainerIDForCreate(trCommand);

	if (!trCommand->objectID)
	{
		return 0;
	}

	estrCopy2(&trCommand->pTransVarName1, trVar);

	estrAppend2(&trCommand->diffString, TransactionParseStringArgument(trCommand, containerData));

	trCommand->bLockRequired = true;
	trCommand->executeCB = CreateContainerCB;
	// Commit but no lock, because create will lock it for us
	trCommand->commitCB = FullContainerCommitCB;
	trCommand->revertCB = FullContainerRevertCB;

	return 1;
}

// Create a container on this server, using initial data and a specified container ID
AUTO_COMMAND ACMD_NAME(CreateSpecificContainerUsingData) ACMD_LIST(gServerTransactionCmdList);
int ParseCreateSpecificContainerUsingData(TransactionCommand *trCommand, char *trVar, char *containerTypeName, ContainerID objectID, ACMD_SENTENCE containerData)
{	
	trCommand->objectID = objectID;
	trCommand->objectType = NameToGlobalType(TransactionParseStringArgument(trCommand,containerTypeName));

	if (!trVar || !trVar[0])
	{
		return 0;
	}

	estrCopy2(&trCommand->pTransVarName1, trVar);

	estrAppend2(&trCommand->diffString, TransactionParseStringArgument(trCommand, containerData));

	trCommand->bLockRequired = true;
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockCB; // We lock outside the verify because we already know the ID
	trCommand->executeCB = CreateOrVerifyContainerCB;
	trCommand->commitCB = FullContainerCommitCB;
	trCommand->revertCB = FullContainerRevertCB;

	return 1;
}

// Verify that a container exists on this server, create it if it doesn't.
AUTO_COMMAND ACMD_NAME(VerifyAndSetContainer) ACMD_LIST(gServerTransactionCmdList);
int ParseVerifyAndSetContainer(TransactionCommand *trCommand, char *trVar, char *containerTypeName, char *containerID,  ACMD_SENTENCE containerData)
{	
	trCommand->objectID = TransactionParseIntArgument(trCommand,containerID);
	trCommand->objectType = NameToGlobalType(TransactionParseStringArgument(trCommand,containerTypeName));

	if (!trVar || !trVar[0])
	{
		return 0;
	}

	estrAppend2(&trCommand->diffString, TransactionParseStringArgument(trCommand, containerData));
	estrCopy2(&trCommand->pTransVarName1, trVar);

	trCommand->bLockRequired = true;
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockCB; // We lock outside the verify because we already know the ID
	trCommand->executeCB = CreateOrVerifyContainerCB;
	trCommand->commitCB = FullContainerCommitCB;
	trCommand->revertCB = FullContainerRevertCB;

	return 1;
}

// Verify that a container exists on this server, create it if it doesn't.
AUTO_COMMAND ACMD_NAME(VerifyOrCreateAndInitContainer) ACMD_LIST(gServerTransactionCmdList);
int ParseVerifyOrCreateAndInitContainer(TransactionCommand *trCommand, char *trVar, char *containerTypeName, char *containerID,  ACMD_SENTENCE containerData)
{	
    trCommand->objectID = TransactionParseIntArgument(trCommand,containerID);
    trCommand->objectType = NameToGlobalType(TransactionParseStringArgument(trCommand,containerTypeName));

    if (!trVar || !trVar[0])
    {
        return 0;
    }

    estrAppend2(&trCommand->diffString, TransactionParseStringArgument(trCommand, containerData));
    estrCopy2(&trCommand->pTransVarName1, trVar);

    trCommand->bLockRequired = true;
    trCommand->checkLockCB = CheckFullContainerLockCB;
    trCommand->lockCB = FullContainerLockCB; // We lock outside the verify because we already know the ID
    trCommand->executeCB = CreateAndInitOrVerifyContainerCB;
    trCommand->commitCB = FullContainerCommitCB;
    trCommand->revertCB = FullContainerRevertCB;

    return 1;
}

// Permanently destroy a container in the backing store
int DestroyContainerCB(TransactionCommand *command)
{
	Container *pObject = GetLockedContainer(command->objectType,command->objectID,command->transactionID, false);

	if (!pObject)
	{
		estrPrintf(command->returnString,"DestroyContainer: Container " CON_PRINTF_STR " not found", CON_PRINTF_ARG(command->objectType, command->objectID));
		return 0;
	}
	if (!IsContainerOwnedByMe(pObject))
	{
		estrPrintf(command->returnString,"Can't call DestroyContainer on a server that doesn't own the container");
		return 0;
	}
	if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_PERSISTED)
	{
		if (!IsThisObjectDB() && !command->bShardExternalCommand)
		{
			estrPrintf(command->returnString,"You can only destroy persisted containers on an ObjectDB");
			return 0;
		}
	}
	else if (GlobalTypeSchemaType(command->objectType) != SCHEMATYPE_TRANSACTED)
	{
		estrPrintf(command->returnString,"Only Transacted and Persisted schematypes can be destroyed via transactions");
		return 0;
	}
	if (!command->bCheckingIfPossible)
	{
		objChangeContainerState(pObject, CONTAINERSTATE_UNKNOWN, 0, 0);

		if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_TRANSACTED)
		{
			estrPrintf(command->transReturnString,"offline %s %u %s %u",GlobalTypeToName(command->objectType),command->objectID,GlobalTypeToName(objServerType()),objServerID());			
		}
		else
		{		
			estrPrintf(&command->pDatabaseReturnData->pString1,"dbDestroyContainer %s %u",GlobalTypeToName(command->objectType),command->objectID);
		}

		DeleteLockedContainer(command->objectType,command->objectID);
		// Delete the container, even if we're an objectDB
	}
	return 1;
}

// Permanently destroy a container in the backing store
int DestroyDeletedContainerCB(TransactionCommand *command)
{
	Container *pObject = GetLockedContainer(command->objectType,command->objectID,command->transactionID, true);

	if (!pObject || !IsContainerOwnedByMe(pObject))
	{
		estrPrintf(command->returnString,"Can't call DestroyDeletedContainer on a server that doesn't own the container");
		return 0;
	}
	if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_PERSISTED)
	{
		if (!IsThisObjectDB() && !command->bShardExternalCommand)
		{
			estrPrintf(command->returnString,"You can only destroy persisted containers on an ObjectDB");
			return 0;
		}
	}
	else if (GlobalTypeSchemaType(command->objectType) != SCHEMATYPE_TRANSACTED)
	{
		estrPrintf(command->returnString,"Only Transacted and Persisted schematypes can be destroyed via transactions");
		return 0;
	}
	if (!command->bCheckingIfPossible)
	{
		objChangeContainerState(pObject, CONTAINERSTATE_UNKNOWN, 0, 0);

		if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_TRANSACTED)
		{
			estrPrintf(command->transReturnString,"offline %s %u %s %u",GlobalTypeToName(command->objectType),command->objectID,GlobalTypeToName(objServerType()),objServerID());			
		}
		else
		{		
			estrPrintf(&command->pDatabaseReturnData->pString1,"dbDestroyDeletedContainer %s %u",GlobalTypeToName(command->objectType),command->objectID);
		}

		DeleteLockedContainer(command->objectType,command->objectID);
		// Delete the container, even if we're an objectDB
	}
	return 1;
}

// Destroy a container on this server
AUTO_COMMAND ACMD_NAME(DestroyContainer) ACMD_LIST(gServerTransactionCmdList);
int ParseDestroyContainer(TransactionCommand *trCommand, char *containerTypeName,char *containerID)
{	
	trCommand->objectID = TransactionParseIntArgument(trCommand,containerID);
	trCommand->objectType = NameToGlobalType(TransactionParseStringArgument(trCommand,containerTypeName));

	trCommand->bLockRequired = true;
	trCommand->executeCB = DestroyContainerCB;
	
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockCB;
	trCommand->commitCB = FullContainerCommitCB;
	trCommand->revertCB = FullContainerRevertCB;

	return 1;
}

// Destroy a container on this server
AUTO_COMMAND ACMD_NAME(DestroyDeletedContainer) ACMD_LIST(gServerTransactionCmdList);
int ParseDestroyDeletedContainer(TransactionCommand *trCommand, char *containerTypeName,char *containerID)
{	
	trCommand->objectID = TransactionParseIntArgument(trCommand,containerID);
	trCommand->objectType = NameToGlobalType(TransactionParseStringArgument(trCommand,containerTypeName));

	trCommand->bLockRequired = true;
	trCommand->executeCB = DestroyDeletedContainerCB;
	
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullDeletedContainerLockCB;
	trCommand->commitCB = FullContainerCommitCB;
	trCommand->revertCB = FullContainerRevertCB;

	return 1;
}

// Process a batch update
int BatchUpdateCB(TransactionCommand *command)
{
	Container *pObject = GetLockedContainer(command->objectType,command->objectID,command->transactionID, false);

	if (!pObject || !IsContainerOwnedByMe(pObject))
	{
		estrPrintf(command->returnString,"Can't call BatchUpdate on a server that doesn't own the container");
		return 0;
	}
	if (!command->bCheckingIfPossible)
	{
		estrPrintf(&command->pDatabaseReturnData->pString1,"dbUpdateContainer %u %u ",command->objectType,command->objectID);
		estrConcatf(&command->pDatabaseReturnData->pString1,"comment %s %u %u\n", command->pTransactionName, objServerType(), objServerID());
		estrAppend(&command->pDatabaseReturnData->pString1,&command->diffString);

		objModifyContainer(pObject,command->diffString);
	}
	return 1;
}

typedef struct DecodeBatchUpdatePathsThreadData
{
	ObjectPathOperation **pathOperations;
} DecodeBatchUpdatePathsThreadData;

bool DecodeBatchUpdatePaths(const char *diffString, TransactionCommand *pHolder)
{
	ContainerStore *store = objFindContainerStoreFromType(pHolder->objectType);
	DecodeBatchUpdatePathsThreadData *threadData = NULL;
	char *tempPath = NULL;
	int ok = 1;

	STATIC_THREAD_ALLOC(threadData);

	if (!diffString || !diffString[0])
		return 0;
	if (!store)
		return 0;

	if (!objPathParseOperations(store->containerSchema->classParse, diffString, &threadData->pathOperations))
	{
		eaClearEx(&threadData->pathOperations, DestroyObjectPathOperation);
		return 0;
	}

	estrStackCreate(&tempPath);
	EARRAY_FOREACH_BEGIN(threadData->pathOperations, i);
	{
		estrPrintf(&tempPath, "%s", threadData->pathOperations[i]->pathEString);
		if (objPathNormalize(threadData->pathOperations[i]->pathEString,store->containerSchema->classParse,&tempPath))	
		{
/*			char *pathEString = NULL;
			char *valueEString = NULL;
			estrPrintf(&pathEString, "%s", tempPath);
			estrPrintf(&valueEString, "%s", threadData->pathOperations[i]->valueEString);*/
			
			AddFieldOperationToTransaction_NotEStrings(pHolder,threadData->pathOperations[i]->op, tempPath, estrLength(&tempPath),
				threadData->pathOperations[i]->valueEString, estrLength(&threadData->pathOperations[i]->valueEString), threadData->pathOperations[i]->quotedValue);
		}
		else
		{
			ok = 0;
			break;
		}
	}
	EARRAY_FOREACH_END;
	estrDestroy(&tempPath);
	eaClearEx(&threadData->pathOperations, DestroyObjectPathOperation);

	return ok;
}

// Performs a batch series of updates to an owned container
AUTO_COMMAND ACMD_NAME(BatchUpdate) ACMD_LIST(gServerTransactionCmdList);
int ParseBatchUpdate(TransactionCommand *trCommand, char *containerTypeName,char *containerID, ACMD_SENTENCE containerData)
{	
	trCommand->objectID = TransactionParseIntArgument(trCommand,containerID);
	trCommand->objectType = NameToGlobalType(TransactionParseStringArgument(trCommand,containerTypeName));

	estrAppend2(&trCommand->diffString, TransactionParseStringArgument(trCommand, containerData));
	
	if (!DecodeBatchUpdatePaths(trCommand->diffString, trCommand))
	{
		return 0;
	}

	trCommand->executeCB = BatchUpdateCB;

	trCommand->checkLockCB = CheckFieldLockCB;
	trCommand->lockCB = FieldLockCB;
	trCommand->commitCB = FieldCommitCB;
	trCommand->revertCB = FieldRevertCB;

	return 1;
}

void DEFAULT_LATELINK_NewContainerLocallyCreated(GlobalType eContainerType)
{
}

// Movement commands

// Receive a container move from another server
int ReceiveContainerFromCB(TransactionCommand *command)
{
	Container *pObject = GetLockedContainer(command->objectType,command->objectID,command->transactionID, false);

	if (IsThisObjectDB())
	{
		estrPrintf(command->returnString,"Can't call ReceiveContainerFrom on an ObjectDB. Call ReturnContainerFrom");
		return 0;
	}
	if (GlobalTypeSchemaType(command->objectType) != SCHEMATYPE_PERSISTED)
	{
		estrPrintf(command->returnString,"Can't move containers that are not of schema type persisted");
		return 0;
	}
	if (!command->bCheckingIfPossible)
	{
		void *pObjectBackup;
		int oldLength;

		void *pBinData;
		int iBinDataSize;

		if (!pObject)
		{
			estrPrintf(command->returnString,"Creation of backup object failed");
			return 0;
		}
		if (IsContainerOwnedByMe(pObject))
		{
			estrPrintf(command->returnString,"Can't call ReceiveContainerFrom on a server that already owns the container");
			return 0;
		}		

		objChangeContainerState(pObject, CONTAINERSTATE_OWNED, objServerType(), objServerID());
		
		if (!IsTypeObjectDB(command->sourceType) && command->sourceType != GLOBALTYPE_NONE)
		{
			estrPrintf(command->transReturnString,"move %s %u %s %u %s %u",GlobalTypeToName(command->objectType),command->objectID,GlobalTypeToName(command->sourceType),command->sourceID,GlobalTypeToName(objServerType()),objServerID());				
		}
		else
		{
			estrPrintf(command->transReturnString,"online %s %u %s %u",GlobalTypeToName(command->objectType),command->objectID,GlobalTypeToName(objServerType()),objServerID());
		}

		

		if ((pBinData = GetTransactionVariable(objLocalManager(), command->transactionID, "containerVarBinary", &iBinDataSize)))
		{
			int iResult;

			Packet *pPacket = pktCreateTempWithSetPayload(pBinData, iBinDataSize);


			if (command->sourceType == GLOBALTYPE_OBJECTDB)
			{
				if (!pObject->containerSchema->loadedParseTable)
				{
					objForceLoadSchemaFilesFromDisk(pObject->containerSchema);
				}
				assertmsgf(pObject->containerSchema->loadedParseTable, "Couldn't load schema from disk for %s", GlobalTypeToName(pObject->containerType));
                assertmsgf(pObject->containerSchema->bIsNativeSchema, "Trying to use ParserRecv2tpis with a non-native schema. This won't work. You must call objRegisterNativeSchema");
				iResult = ParserRecv2tpis(pPacket, pObject->containerSchema->loadedParseTable, pObject->containerSchema->classParse, pObject->containerData);
			}
			else
			{
				iResult = ParserRecv(pObject->containerSchema->classParse, pPacket, pObject->containerData, 0);
			}

			pktFree(&pPacket);

			if (!iResult)
			{
				TransDataBlockClear(command->pDatabaseReturnData);
				estrClear(command->transReturnString);
				estrPrintf(command->returnString,"Applying binary data failed");

				return 0;
			}

		}
		else
		{
			if (!objPathParseAndApplyOperations(pObject->containerSchema->classParse, pObject->containerData, command->diffString))
			{
				TransDataBlockClear(command->pDatabaseReturnData);
				estrClear(command->transReturnString);
				estrPrintf(command->returnString,"Applying diff failed");

				return 0;
			}
		}


		PERFINFO_AUTO_START("MakeBackupOnReceive", 1);
		pObjectBackup = StructCloneWithCommentVoid(pObject->containerSchema->classParse, pObject->containerData, "ObjectBackup created during ReceiveContainerFromDB");
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("PostReceiveTransactionRun", 1);

		if (!RunSpecialTransactionCallbacks(command->objectType, TRANSACTION_RECEIVE_CONTAINER_FROM, 
			pObject->containerData, pObjectBackup, command->sourceType, command->sourceID, command->returnString))
		{
			TransDataBlockClear(command->pDatabaseReturnData);
			estrClear(command->transReturnString);
			PERFINFO_AUTO_STOP(); // "PostReceiveTransactionRun"

			PERFINFO_AUTO_START("DestroyBackupOnReceive-Fail", 1);
			StructDestroyVoid(pObject->containerSchema->classParse, pObjectBackup);
			PERFINFO_AUTO_STOP(); // "DestroyBackupOnReceive-Fail"

			return 0;
		}

		estrPrintf(&command->pDatabaseReturnData->pString1,"dbUpdateContainer %u %u ",command->objectType,command->objectID);
		estrConcatf(&command->pDatabaseReturnData->pString1,"comment %s %u %u\n", command->pTransactionName, objServerType(), objServerID());
		oldLength = estrLength(&command->pDatabaseReturnData->pString1);

		PERFINFO_AUTO_STOP(); // "PostReceiveTransactionRun"

		PERFINFO_AUTO_START("PostReceiveTransactionDiff", 1);

		StructWriteTextDiff(&command->pDatabaseReturnData->pString1, pObject->containerSchema->classParse, pObjectBackup,
			pObject->containerData, NULL, TOK_PERSIST, 0, 0);

		if (oldLength == estrLength(&command->pDatabaseReturnData->pString1))
		{
			estrPrintf(&command->pDatabaseReturnData->pString1,"dbUpdateContainerOwner %u %u %u %u",command->objectType,command->objectID,objServerType(),objServerID());		
		}
		else
		{
			estrClear(&command->diffString);
			StructTextDiffWithNull_Verify(&command->diffString, pObject->containerSchema->classParse,
				pObject->containerData, NULL, 0, TOK_PERSIST, 0, false);
			estrPrintf(&command->pDatabaseReturnData->pString2,"dbUpdateContainerOwner %u %u %u %u",command->objectType,command->objectID,objServerType(),objServerID());		
		}

		
		PERFINFO_AUTO_STOP(); // "PostReceiveTransactionDiff"



		if (!objTransactions_MaybeLocallyCopyBackupDataDuringReceive(pObject->containerType, pObject->containerData, pObjectBackup))
		{
			PERFINFO_AUTO_START("DestroyBackupOnReceive", 1);
			StructDestroyVoid(pObject->containerSchema->classParse, pObjectBackup);
			PERFINFO_AUTO_STOP();
		}

		//call the latelinked function so that server- and container- specific stuff can happen
		NewContainerLocallyCreated(pObject->containerType);

		command->bUseBackupContainerWhenCommitting = true;
	}
	return 1;
}

// Move ownership to this server (type, id, source server type, source server id, containerData)
AUTO_COMMAND ACMD_NAME(ReceiveContainerFrom) ACMD_LIST(gServerTransactionCmdList);
int ParseReceiveContainerFrom(TransactionCommand *trCommand, char *containerTypeName,char *containerID, char *sourceTypeName, char *sourceID, ACMD_SENTENCE containerData)
{	
	trCommand->objectID = TransactionParseIntArgument(trCommand,containerID);
	trCommand->objectType = NameToGlobalType(TransactionParseStringArgument(trCommand,containerTypeName));
	trCommand->sourceID = TransactionParseIntArgument(trCommand,sourceID);
	trCommand->sourceType = NameToGlobalType(TransactionParseStringArgument(trCommand,sourceTypeName));

	estrAppend2(&trCommand->diffString, TransactionParseStringArgument(trCommand, containerData));

	trCommand->bLockRequired = true;
	trCommand->executeCB = ReceiveContainerFromCB;
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockCB;
	trCommand->commitCB = FullContainerCommitCB;
	trCommand->revertCB = FullContainerRevertCB;



	return 1;
}


// Move a container to another server
int MoveContainerToCB(TransactionCommand *command)
{
	Container *pObject = GetLockedContainer(command->objectType,command->objectID,command->transactionID, false);

	if (!pObject || !IsContainerOwnedByMe(pObject))
	{
		estrPrintf(command->returnString,"Can't call MoveContainerTo on a server that doesn't own the container");
		return 0;
	}
	if (GlobalTypeSchemaType(command->objectType) != SCHEMATYPE_PERSISTED)
	{
		estrPrintf(command->returnString,"Can't move containers that are not of schema type persisted");
		return 0;
	}
	if (!command->bCheckingIfPossible)
	{
		if (IsTypeObjectDB(objServerType()))
		{
			//The ObjectDB does not do Special Transaction Callbacks on SendTo.			
			estrHeapCreate(&command->pDatabaseReturnData->pString1, 64, CRYPTIC_CHURN_HEAP);
			estrPrintf(&command->pDatabaseReturnData->pString1,"dbUpdateContainer %u %u ",command->objectType,command->objectID);
			TransDataBlockClear(command->pDatabaseReturnData);
		}
		else
		{
			void *pObjectBackup;
			int oldLength;

			objChangeContainerState(pObject, CONTAINERSTATE_UNKNOWN, command->destType, command->destID);
			DeleteLockedContainer(command->objectType,command->objectID);
	
			pObjectBackup = StructCloneWithCommentVoid(pObject->containerSchema->classParse, pObject->containerData, "ObjectBackup created during MoveContainerToDB");

			if (!RunSpecialTransactionCallbacks(command->objectType, TRANSACTION_MOVE_CONTAINER_TO, 
				pObject->containerData, pObjectBackup, command->destType, command->destID, command->returnString))
			{
				TransDataBlockClear(command->pDatabaseReturnData);
				estrClear(command->transReturnString);
				StructDestroyVoid(pObject->containerSchema->classParse, pObjectBackup);
				return 0;
			}
			estrPrintf(&command->pDatabaseReturnData->pString1,"dbUpdateContainer %u %u ",command->objectType,command->objectID);
			estrConcatf(&command->pDatabaseReturnData->pString1,"comment %s %u %u\n", command->pTransactionName, objServerType(), objServerID());
			oldLength = estrLength(&command->pDatabaseReturnData->pString1);

			StructWriteTextDiff(&command->pDatabaseReturnData->pString1, pObject->containerSchema->classParse, pObjectBackup,
				pObject->containerData, NULL, TOK_PERSIST, 0, 0);

			if (oldLength == estrLength(&command->pDatabaseReturnData->pString1))
			{
				TransDataBlockClear(command->pDatabaseReturnData);
			}

			StructDestroyVoid(pObject->containerSchema->classParse, pObjectBackup);
		}

		if (1/*GetAppGlobalType() != GLOBALTYPE_OBJECTDB*/)
		{
			Packet *pPak = pktCreateTemp(NULL);
			char binaryTransVarName[256];
			sprintf(binaryTransVarName, "%sBinary", command->pTransVarName1);
	
			ParserSend(pObject->containerSchema->classParse, pPak, NULL, pObject->containerData, SENDDIFF_FLAG_FORCEPACKALL, TOK_PERSIST, 0, NULL);

			SetTransactionVariablePacket(objLocalManager(), command->transactionID, binaryTransVarName, pPak);
			pktFree(&pPak);
		}
		//else
		//{
		//  char *updateString = NULL;
		//  estrStackCreate(&updateString);
		//	StructWriteTextDiff(&updateString,pObject->containerSchema->classParse,NULL,pObject->containerData,NULL,TOK_PERSIST,0,false);
		//	SetTransactionVariableEString(objLocalManager(),command->transactionID,command->pTransVarName1,&updateString);
		//  estrDestroy(&updateString);
		//}
	
	}
	return 1;
}

// Move ownership to this server (type, id, source server type, source server id, containerData)
AUTO_COMMAND ACMD_NAME(MoveContainerTo) ACMD_LIST(gServerTransactionCmdList);
int ParseMoveContainerTo(TransactionCommand *trCommand, char *trvar, char *containerTypeName,char *containerID, char *sourceTypeName, char *sourceID)
{	
	if (!trvar || !(trvar[0]))
	{
		return 0;
	}

	estrCopy2(&trCommand->pTransVarName1, trvar);
	
	trCommand->objectID = TransactionParseIntArgument(trCommand,containerID);
	trCommand->objectType = NameToGlobalType(TransactionParseStringArgument(trCommand,containerTypeName));
	trCommand->destID = TransactionParseIntArgument(trCommand,sourceID);
	trCommand->destType = NameToGlobalType(TransactionParseStringArgument(trCommand,sourceTypeName));

	trCommand->bLockRequired = true;
	trCommand->executeCB = MoveContainerToCB;
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockCB;
	trCommand->commitCB = FullContainerCommitCB;
	trCommand->revertCB = FullContainerRevertCB;

	return 1;
}

// Move ownership to this server from the ObjectDB (type, id, source server type, source server id, containerData)
AUTO_COMMAND ACMD_NAME(SendContainerTo) ACMD_LIST(gServerTransactionCmdList);
int ParseSendContainerTo(TransactionCommand *trCommand, char *trvar, char *containerTypeName,char *containerID, char *sourceTypeName, char *sourceID)
{	
	if (!trvar || !(trvar[0]))
	{
		return 0;
	}

	estrCopy2(&trCommand->pTransVarName1, trvar);

	trCommand->objectID = TransactionParseIntArgument(trCommand,containerID);
	trCommand->objectType = NameToGlobalType(TransactionParseStringArgument(trCommand,containerTypeName));
	trCommand->destID = TransactionParseIntArgument(trCommand,sourceID);
	trCommand->destType = NameToGlobalType(TransactionParseStringArgument(trCommand,sourceTypeName));

	trCommand->bLockRequired = true;
	trCommand->executeCB = MoveContainerToCB;
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockNoBackupCB;
	trCommand->commitCB = FullContainerCommitNoBackupCB;
	trCommand->revertCB = FullContainerRevertNoBackupCB;

	return 1;
}

// Return container to ObjectDB from another server
int RecoverContainerFromCB(TransactionCommand *command)
{
	Container *pObject = GetLockedContainer(command->objectType,command->objectID,command->transactionID, false);

	if (!pObject)
	{
		estrPrintf(command->returnString,"Invalid Container");
		return 0;
	}
	if (IsContainerOwnedByMe(pObject))
	{
		estrPrintf(command->returnString,"Can't call RecoverContainerFrom on a server that already owns the container");
		return 0;
	}
	if (!IsThisObjectDB())
	{
		estrPrintf(command->returnString,"Can't call RecoverContainerFrom on a non-ObjectDB. Call MoveContainerFrom");
		return 0;
	}
	if (GlobalTypeSchemaType(command->objectType) != SCHEMATYPE_PERSISTED)
	{
		estrPrintf(command->returnString,"Can't move containers that are not of schema type persisted");
		return 0;
	}
	if (GetAppGlobalType() != GLOBALTYPE_OBJECTDB)
	{
		estrPrintf(command->returnString,"Can't recover containers to anything other than the ObjectDB");
		return 0;
	}

	if (!command->bCheckingIfPossible)
	{
		estrHeapCreate(command->transReturnString, 64, CRYPTIC_CHURN_HEAP);
		estrPrintf(command->transReturnString,"offline %s %u %s %u",GlobalTypeToName(command->objectType),command->objectID,GlobalTypeToName(command->sourceType),command->sourceID);
		TransDataBlockClear(command->pDatabaseReturnData);
		estrHeapCreate(&command->pDatabaseReturnData->pString1, 64, CRYPTIC_CHURN_HEAP);
		estrPrintf(&command->pDatabaseReturnData->pString1,"dbUpdateContainerOwner %u %u %u %u",command->objectType,command->objectID,objServerType(),objServerID());
	}
	return 1;
}

// Move ownership to this server (type, id, source server type, source server id, containerData)
AUTO_COMMAND ACMD_NAME(RecoverContainerFrom) ACMD_LIST(gServerTransactionCmdList);
int ParseRecoverContainerFrom(TransactionCommand *trCommand, char *containerTypeName,char *containerID, char *sourceTypeName, char *sourceID)
{	
	trCommand->objectID = TransactionParseIntArgument(trCommand,containerID);
	trCommand->objectType = NameToGlobalType(TransactionParseStringArgument(trCommand,containerTypeName));
	trCommand->sourceID = TransactionParseIntArgument(trCommand,sourceID);
	trCommand->sourceType = NameToGlobalType(TransactionParseStringArgument(trCommand,sourceTypeName));

	trCommand->bLockRequired = true;
	trCommand->executeCB = RecoverContainerFromCB;
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockNoBackupCB;
	trCommand->commitCB = FullContainerCommitNoBackupCB;
	trCommand->revertCB = FullContainerRevertNoBackupCB;

	return 1;
}

// Return container to ObjectDB from another server, WITH a full lock, which is slower
int RecoverContainerFromWithBackupCB(TransactionCommand *command)
{
	Container *pObject = GetLockedContainer(command->objectType,command->objectID,command->transactionID, false);

	if (!pObject)
	{
		estrPrintf(command->returnString,"Invalid Container");
		return 0;
	}
	if (IsContainerOwnedByMe(pObject))
	{
		estrPrintf(command->returnString,"Can't call RecoverContainerFrom on a server that already owns the container");
		return 0;
	}
	if (!IsThisObjectDB())
	{
		estrPrintf(command->returnString,"Can't call RecoverContainerFrom on a non-ObjectDB. Call MoveContainerFrom");
		return 0;
	}
	if (GlobalTypeSchemaType(command->objectType) != SCHEMATYPE_PERSISTED)
	{
		estrPrintf(command->returnString,"Can't move containers that are not of schema type persisted");
		return 0;
	}
	if (!command->bCheckingIfPossible)
	{
		objChangeContainerState(pObject, CONTAINERSTATE_OWNED, objServerType(), objServerID());

		estrPrintf(command->transReturnString,"offline %s %u %s %u",GlobalTypeToName(command->objectType),command->objectID,GlobalTypeToName(command->sourceType),command->sourceID);
		TransDataBlockClear(command->pDatabaseReturnData);
		estrPrintf(&command->pDatabaseReturnData->pString1,"dbUpdateContainerOwner %u %u %u %u",command->objectType,command->objectID,objServerType(),objServerID());		
	}
	return 1;
}

// Move ownership to this server (type, id, source server type, source server id, containerData)
// This version saves a backup, so it can be chained into other transactions that need backups
AUTO_COMMAND ACMD_NAME(RecoverContainerFromWithBackup) ACMD_LIST(gServerTransactionCmdList);
int ParseRecoverContainerFromWithBackup(TransactionCommand *trCommand, char *containerTypeName,char *containerID, char *sourceTypeName, char *sourceID)
{	
	trCommand->objectID = TransactionParseIntArgument(trCommand,containerID);
	trCommand->objectType = NameToGlobalType(TransactionParseStringArgument(trCommand,containerTypeName));
	trCommand->sourceID = TransactionParseIntArgument(trCommand,sourceID);
	trCommand->sourceType = NameToGlobalType(TransactionParseStringArgument(trCommand,sourceTypeName));

	trCommand->bLockRequired = true;
	trCommand->executeCB = RecoverContainerFromWithBackupCB;
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockCB;
	trCommand->commitCB = FullContainerCommitCB;
	trCommand->revertCB = FullContainerRevertCB;

	return 1;
}



// Return a container to the objectDB
// Entity has a special version of this, for including a diff
int ReturnContainerToCB(TransactionCommand *command)
{
	Container *pObject = GetLockedContainer(command->objectType,command->objectID,command->transactionID, false);

	if (!pObject || !IsContainerOwnedByMe(pObject))
	{
		estrPrintf(command->returnString,"Can't call ReturnContainerTo on a server that doesn't own the container");
		return 0;
	}
	if (IsTypeObjectDB(objServerType()))
	{
		estrPrintf(command->returnString,"You can't call ReturnContainerTo on an objectDB");
		return 0;
	}
	if (GlobalTypeSchemaType(command->objectType) != SCHEMATYPE_PERSISTED)
	{
		estrPrintf(command->returnString,"Can't move containers that are not of schema type persisted");
		return 0;
	}
	if (!command->bCheckingIfPossible)
	{
		void *pObjectBackup;
		int oldLength;

		objChangeContainerState(pObject, CONTAINERSTATE_UNKNOWN, command->destType, command->destID);

		DeleteLockedContainer(command->objectType,command->objectID);		

		pObjectBackup = StructCloneWithCommentVoid(pObject->containerSchema->classParse, pObject->containerData, "ObjectBackup created ruing ReturnContainerToCB");

		if (!RunSpecialTransactionCallbacks(command->objectType, TRANSACTION_RETURN_CONTAINER_TO, 
			pObject->containerData, pObjectBackup, command->destType, command->destID, command->returnString))
		{
			TransDataBlockClear(command->pDatabaseReturnData);
			estrClear(command->transReturnString);
			StructDestroyVoid(pObject->containerSchema->classParse, pObjectBackup);
			return 0;
		}

		estrPrintf(&command->pDatabaseReturnData->pString1,"dbUpdateContainer %u %u ",command->objectType,command->objectID);
		estrConcatf(&command->pDatabaseReturnData->pString1,"comment %s %u %u\n", command->pTransactionName, objServerType(), objServerID());
		oldLength = estrLength(&command->pDatabaseReturnData->pString1);

		StructWriteTextDiff(&command->pDatabaseReturnData->pString1, pObject->containerSchema->classParse, pObjectBackup,
			pObject->containerData, NULL, TOK_PERSIST, 0, 0);

		if (oldLength == estrLength(&command->pDatabaseReturnData->pString1))
		{
			TransDataBlockClear(command->pDatabaseReturnData);
		}

		StructDestroyVoid(pObject->containerSchema->classParse, pObjectBackup);
	}
	return 1;
}

// Move ownership to this server (type, id, source server type, source server id, containerData)
AUTO_COMMAND ACMD_NAME(ReturnContainerTo) ACMD_LIST(gServerTransactionCmdList);
int ParseReturnContainerTo(TransactionCommand *trCommand, char *containerTypeName,char *containerID, char *sourceTypeName, char *sourceID)
{	
	trCommand->objectID = TransactionParseIntArgument(trCommand,containerID);
	trCommand->objectType = NameToGlobalType(TransactionParseStringArgument(trCommand,containerTypeName));
	trCommand->destID = TransactionParseIntArgument(trCommand,sourceID);
	trCommand->destType = NameToGlobalType(TransactionParseStringArgument(trCommand,sourceTypeName));

	trCommand->bLockRequired = true;
	trCommand->executeCB = ReturnContainerToCB;
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockCB;
	trCommand->commitCB = FullContainerCommitCB;
	trCommand->revertCB = FullContainerRevertCB;

	return 1;
}

// Auto transaction support

typedef struct ObjectsForAutoTransStructArg
{
	void *backupObject;
	void *newObject;
	bool bBackupIsLocalCopy;
	bool bNewIsLocalCopy;
	char *pTransVarName;
} ObjectsForAutoTransStructArg;

typedef struct AutoTransStructArg {
	ParseTable *parseTable;

	ObjectsForAutoTransStructArg nonArrayObjects;

	bool bIsArray;
	ObjectsForAutoTransStructArg **ppArrayObjects;

	//copy of the ppArrayObjects->newObject, needed in a separate earray so we can do SetATRArg_ContainerEArray
	void **ppNewObjects_ShallowCopy;

} AutoTransStructArg;

void FreeObjectsForAutoTransStructArg(ParseTable *pTPI, ObjectsForAutoTransStructArg *pObjects)
{
	if (pObjects->backupObject && !pObjects->bBackupIsLocalCopy)
	{
		StructDestroyVoid(pTPI, pObjects->backupObject);
	}

	if (pObjects->newObject && !pObjects->bNewIsLocalCopy)
	{
		StructDestroyVoid(pTPI, pObjects->newObject);
	}

	estrDestroy(&pObjects->pTransVarName);
}

void DestroyAutoTransStructArg(AutoTransStructArg *pArg)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	eaDestroy(&pArg->ppNewObjects_ShallowCopy);

	FreeObjectsForAutoTransStructArg(pArg->parseTable, &pArg->nonArrayObjects);

	for (i=0; i < eaSize(&pArg->ppArrayObjects); i++)
	{
		FreeObjectsForAutoTransStructArg(pArg->parseTable, pArg->ppArrayObjects[i]);
	}
	eaDestroyEx(&pArg->ppArrayObjects, NULL);

	free(pArg);

	PERFINFO_AUTO_STOP();
}

void TokenizeTRVarArrayNames(char *pInputString, AutoTransStructArg *pArg)
{
	char workString[1024];
	int iWorkStringLength = 0;
	bool bFoundAChar = false;

	assert(strncmp(pInputString, "TRVAR_ARRAY[", 12) == 0);
	pInputString += 12;

	while (1)
	{
		assert(*pInputString);
		if (*pInputString == ']' || *pInputString == ',')
		{
			if (bFoundAChar)
			{
				ObjectsForAutoTransStructArg *pNewObjects;
				pNewObjects = callocStruct(ObjectsForAutoTransStructArg);
				workString[iWorkStringLength] = 0;
				pNewObjects->pTransVarName = estrDup(workString);
				eaPush(&pArg->ppArrayObjects, pNewObjects);
			}

			if (*pInputString == ']')
			{
				return;
			}
			else
			{
				pInputString++;
				iWorkStringLength = 0;
				bFoundAChar = false;
			}
		}
		else
		{
			bFoundAChar = true;
			workString[iWorkStringLength++] = *(pInputString++);
		}
	}
}

void DiffLocalArgument(TransactionCommand *pCommand, char **diffString, U32 transactionID, const char *transVarName,
					   ParseTable *parseTable, void *backupObject, void *newObject,
					   int *iBytesOut)
{
	int i;
	LocalContainerInfo *containerInfo = GetLocalContainerInfo(pCommand, transVarName);

	estrClear(diffString);

	if(!containerInfo || containerInfo->fullContainer)
	{
		StructWriteTextDiff(diffString,parseTable,backupObject,newObject,
			"",TOK_PERSIST,TOK_NO_TRANSACT,0);
	}
	else
	{
		for (i = 0; i < eaSize(&containerInfo->lockedFields); i++)
		{
			char *field = containerInfo->lockedFields[i];
			void *srcObject;
			void *dstObject;
			int srcIndex;
			int dstIndex;
			ParseTable *table;
			int column;
			int flags = TOK_PERSIST;
			int parseResult;

			//note that ParserResolvePathEx into an optional substruct that doesn't exist
			//returns success (ie, into foo.bar.wakka, foo->bar exists, but foo->bar->wakka is NULL),
			//while ParserResolvePathEx into a field in an indexed array that is not there returns
			//failure
			parseResult = ParserResolvePathEx(field,parseTable,
				newObject, &table, &column, &srcObject, &srcIndex, NULL, NULL, NULL,
				NULL, OBJPATHFLAG_DONTLOOKUPROOTPATH);
			if(!parseResult)
			{
				//the specific object doesn't exist... check if it existed in the original object
				parseResult = ParserResolvePathEx(field,parseTable,
					backupObject, &table, &column, &dstObject, &dstIndex, NULL, NULL, NULL,
					NULL, OBJPATHFLAG_DONTLOOKUPROOTPATH);

				//if it existed in the original object, and doesn't exist any more, and this is an indexed
				//earray, then we need to destroy it specifically
				if (parseResult && ParserColumnIsIndexedEArray(table, column, NULL))
				{
					estrConcatf(diffString, "destroy %s\n", field);
				}

				continue; 
			}


			parseResult = ParserResolvePathEx(field,parseTable,
				backupObject, &table, &column, &dstObject, &dstIndex, NULL, NULL, NULL,
				NULL, OBJPATHFLAG_DONTLOOKUPROOTPATH);
			if(!parseResult)
			{
				//if this is a struct in an indexed earray, then we need to specially create the
				//struct in the diff, in case this is the new case with eaIndexedPushUsingStringIfPossible
				//in which case the parent is not locked. If the parent is locked, then double diffing
				//is harmless, if somewhat inefficient.
				if (ParserColumnIsIndexedEArray(table, column, NULL))
				{
					void *pNewStruct = TokenStoreGetPointer_inline(table, &table[column], column, srcObject, srcIndex, NULL);
					ParseTable *subtable = table[column].subtable;

					StructTextDiffWithNull_Verify(diffString,subtable,pNewStruct,field,(int)strlen(field),TOK_PERSIST,TOK_NO_TRANSACT,0);
					continue;
				}
				else
				{
					// This means the transaction locked the array this is in and
					// created a new item, so it's not on the backup copy currently
					// The array will also be locked, so the diff is already there
					continue;

				}
			}

			ParserWriteTextDiff(diffString,table,column,dstIndex,srcIndex,dstObject,srcObject,field,TOK_PERSIST,TOK_NO_TRANSACT,0);
		}
	}

	*iBytesOut += (int)estrLength(diffString);

	SetTransactionVariableEString(objLocalManager(),transactionID,transVarName,diffString);
}
			
typedef struct TransTimer {
	PERFINFO_TYPE*	pi;
	char*			name;
} TransTimer;

static StashTable stTransTimers;

static void startTransTimer(const char* name){
	TransTimer* t = NULL;
	
	if(!stTransTimers){
		stTransTimers = stashTableCreateWithStringKeys(100, StashDefault);
	}else{
		stashFindPointer(stTransTimers, name, &t);
	}
	
	if(!t){
		PERFINFO_AUTO_START_FUNC();

		t = callocStruct(TransTimer);
		t->name = strdup(name);
		
		stashAddPointer(stTransTimers, t->name, t, false);

		PERFINFO_AUTO_STOP();
	}
	
	PERFINFO_AUTO_START_STATIC(t->name, &t->pi, 1);
}

// Actually run an auto transaction
// Not thread safelog


U32  mSecsForSlowTrans = 250;
AUTO_CMD_INT(mSecsForSlowTrans, SecsForSowTrans);

#define CHECK_FOR_TIME_ALERT(caseString) \
{	coarseTimerStopInstance(NULL, "AutoTrans"); \
    if ((iEndTime = timeGetTime()) > iStartTime + mSecsForSlowTrans) {\
	ReportTransactionWithSlowCallback(objLocalManager(), trans->pFuncName);\
	if (gServerLibState.iSlowTransationCount++ < 1024) {\
	char *pErrorStr = NULL; char *pContainerStr = NULL; \
	estrConcatf(&pErrorStr, "Trans %s was slow (result:  %s).", trans->pFuncName, caseString); \
	ErrorDetailsf("%s", spAllContainersString); Errorf("%s", pErrorStr); estrDestroy(&pErrorStr); estrDestroy(&pContainerStr); }} \
}

static bool sbDupContainersNonFatal = false;  // Disable by default again when ET#21475654 is fixed.
AUTO_CMD_INT(sbDupContainersNonFatal, DupContainersNonFatal);

//this is mainly for debugging purpose, but it also does the checking that there are no duplicate containers
void AddToAllContainersString(char **ppString, const char *pTransName, const char *pTypeName, const char *pIndividualName)
{
	char *pLocalString = NULL;
	estrStackCreate(&pLocalString);
	estrPrintf(&pLocalString, " %s[%s] ", pTypeName, pIndividualName);

	if (*ppString && strstri(*ppString, pLocalString))
	{
		if (sbDupContainersNonFatal)
		{
			ErrorOrAlert("DUP_CONTAINER_IN_TRANS", "AutoTrans %s has container%spassed in as more than one argument simultaneously. This is definitely alarming, talk to Alex or Jeff",
				pTransName, pLocalString);
		}
		else
		{
			AssertOrAlert("DUP_CONTAINER_IN_TRANS", "AutoTrans %s has container%spassed in as more than one argument simultaneously. This is definitely alarming, talk to Alex or Jeff. Add –DupContainersNonFatal to your global command like to temporarily make this non-fatal.",
				pTransName, pLocalString);
		}
	}

	estrConcatf(ppString, "%s", pLocalString);
	estrDestroy(&pLocalString);
}


int RunAutoTransCB(TransactionCommand *command)
{
	static char *estrFail = NULL;
	static char *diffString = NULL;
	enumTransactionOutcome result;
	int i;	
	ATR_FuncDef *trans = (ATR_FuncDef *)command->userData;
	AutoTransStructArg **containerArgs = NULL;
	U32 iStartTime = timeGetTime();
	static int iTimeAlertCount = 0;
	U32 iEndTime;
	bool bFailedDuringContainerArgs = false;


	//for each container arg or container in an earray, as we decode it, stick its name in here. Used for debug reporting
	static char *spAllContainersString = NULL;

	if (command->bCheckingIfPossible)
	{
		return 1;
	}

	//because we're executing right now, we're obviously executing on ourself
	command->autoTransExecutionServerID = GetAppGlobalID();
	command->autoTransExecutionServerType = GetAppGlobalType();

	// can't use default "pass in the GSL transaction CoarseTimerManager" logic
	// so just adding a global default one (for now?)
	// DO NOT REMOVE THIS LINE WITHOUT ALSO MODIFYING THE CHECK_FOR_TIME_ALERT MACRO
	coarseTimerAddInstanceWithTrivia(NULL, "AutoTrans", allocAddString(trans->pFuncName));

	estrClear(&spAllContainersString);
	

	PERFINFO_RUN(
		PERFINFO_AUTO_START_FUNC();
		startTransTimer(trans->pFuncName);
	);

	devassertmsg(!exprCurAutoTrans, "exprCurAutoTrans is set, is RunAutoTransCB getting called recursively?");
	exprCurAutoTrans = trans->pFuncName;

	PERFINFO_AUTO_START("Unpack args", 1);
	for (i = 0; trans->pArgs[i].eArgType; i++)
	{
		ATRArgDef *argDef = &trans->pArgs[i];

		switch (argDef->eArgType)
		{
		xcase ATR_ARG_INT:
			SetATRArg_Int(i,atoi(command->fieldEntries[i]->pathEString));
		xcase ATR_ARG_INT64:
			SetATRArg_Int64(i,atoi64(command->fieldEntries[i]->pathEString));
		xcase ATR_ARG_FLOAT:
			SetATRArg_Float(i,opt_atof(command->fieldEntries[i]->pathEString));
		xcase ATR_ARG_STRING:
			SetATRArg_String(i,command->fieldEntries[i]->pathEString);
		xcase ATR_ARG_STRUCT:
		case ATR_ARG_CONST_STRUCT:
		{
			AutoTransStructArg *newArg = calloc(sizeof(AutoTransStructArg),1);

			command->iBytesIn += estrLength(&command->fieldEntries[i]->pathEString);

			newArg->parseTable = argDef->pParseTable;

			if (strcmp(command->fieldEntries[i]->pathEString, "NULL") == 0)
			{
				newArg->nonArrayObjects.newObject = NULL;
			}
			else
			{
				if (argDef->eArgType == ATR_ARG_CONST_STRUCT)
				{
					newArg->nonArrayObjects.newObject = AutoTrans_ParserReadTextEscapedOrMaybeFromLocalStructString(argDef->pParseTable, command->fieldEntries[i]->pathEString, true, &newArg->nonArrayObjects.bNewIsLocalCopy);
				}
				else
				{
					newArg->nonArrayObjects.newObject = AutoTrans_ParserReadTextEscapedOrMaybeFromLocalStructString(argDef->pParseTable, command->fieldEntries[i]->pathEString, false, NULL);
				}
			}
			SetATRArg_Struct(i,newArg->nonArrayObjects.newObject);
			eaPush(&containerArgs, newArg);
		}
		xcase ATR_ARG_CONTAINER:
		{
			PERFINFO_AUTO_START("ATR_ARG_CONTAINER", 1);
			if (strcmp(command->fieldEntries[i]->pathEString, "NULL") == 0)
			{
				SetATRArg_Container(i, NULL);
			}
			else
			{
				char *containedDiffString;
				int iDifStrSize;
				LocalContainerInfo* containerInfo;
				AutoTransStructArg *newArg;
				
				PERFINFO_AUTO_START("create arg", 1);
				{
					newArg = callocStruct(AutoTransStructArg);

					estrCopy(&newArg->nonArrayObjects.pTransVarName, &command->fieldEntries[i]->pathEString);				

					newArg->parseTable = argDef->pParseTable;
				}
				PERFINFO_AUTO_STOP_START("GetLocalContainerInfo", 1);
				{
					containerInfo = GetLocalContainerInfo(command, newArg->nonArrayObjects.pTransVarName);
				}
				PERFINFO_AUTO_STOP();
				
				if (containerInfo)					
				{
					PERFINFO_AUTO_START("fast local", 1);
					
					newArg->nonArrayObjects.backupObject = GetFastLocalBackupCopy(command, containerInfo->conRef.containerType, containerInfo->conRef.containerID);
					if (newArg->nonArrayObjects.backupObject)
						newArg->nonArrayObjects.bBackupIsLocalCopy = true;
						
					PERFINFO_AUTO_STOP();
				}
				if (!newArg->nonArrayObjects.backupObject)
				{
					PERFINFO_AUTO_START("create backup", 1);

					newArg->nonArrayObjects.backupObject = StructCreateWithComment(newArg->parseTable, "AutoTrans internal backup copy");
					containedDiffString = GetTransactionVariable(objLocalManager(),command->transactionID,newArg->nonArrayObjects.pTransVarName, &iDifStrSize);
					
					if (!objPathParseAndApplyOperations(newArg->parseTable,newArg->nonArrayObjects.backupObject,containedDiffString))
					{
						AssertOrAlert("OBJPATH_FAILURE_IN_AUTO_TRANS", "While executing AutoTrans %s, failed in objPathParseAndApplyOperations. This is extremely bad and fatal. There should be an errorf giving the precise path failure. The most likely cause of this is dynamic enums that exist on one server but not a different server",
							trans->pFuncName);
						bFailedDuringContainerArgs = true;
					}

					command->iBytesIn += (int)strlen(containedDiffString);
					
					PERFINFO_AUTO_STOP();
				}

				if (newArg->nonArrayObjects.bBackupIsLocalCopy)
				{
					PERFINFO_AUTO_START("bBackupIsLocalCopy", 1);
					
					newArg->nonArrayObjects.newObject = GetFastLocalModifyCopy(command, containerInfo->conRef.containerType, containerInfo->conRef.containerID);
					newArg->nonArrayObjects.bNewIsLocalCopy = !!newArg->nonArrayObjects.newObject;
					if (containerInfo->fullContainer)
					{
						StructCopyVoid(newArg->parseTable, newArg->nonArrayObjects.backupObject, newArg->nonArrayObjects.newObject, 0, TOK_PERSIST, 0);
					}
					else
					{
						objPathParseAndApplyOperations(newArg->parseTable,newArg->nonArrayObjects.newObject,containerInfo->localDiffString);
					}
					
					PERFINFO_AUTO_STOP();
				}
				else if (!newArg->nonArrayObjects.newObject)
				{
					PERFINFO_AUTO_START("!newObject", 1);
					newArg->nonArrayObjects.newObject = StructCloneWithCommentVoid(newArg->parseTable, newArg->nonArrayObjects.backupObject, "NewArg created for AutoTrans during RunAutoTransCB");
					PERFINFO_AUTO_STOP();
				}

				PERFINFO_AUTO_START("bottom", 1);
				{
					SetATRArg_Container(i,newArg->nonArrayObjects.newObject);

					eaPush(&containerArgs,newArg);
				}
				PERFINFO_AUTO_STOP();

				if (newArg->nonArrayObjects.newObject)
				{
					AddToAllContainersString(&spAllContainersString, trans->pFuncName, ParserGetTableName_WithEntityFixup(newArg->parseTable, newArg->nonArrayObjects.newObject), ParserGetStructName(newArg->parseTable, newArg->nonArrayObjects.newObject));
				}
			}
			PERFINFO_AUTO_STOP();
		}
		xcase ATR_ARG_CONTAINER_EARRAY:
		{
			int j;
			AutoTransStructArg *newArg;
			
			PERFINFO_AUTO_START("ATR_ARG_CONTAINER_EARRAY", 1);

			newArg = callocStruct(AutoTransStructArg);
			newArg->bIsArray = true;
			newArg->parseTable = argDef->pParseTable;

			//populates newArg->ppArrayObjects
			TokenizeTRVarArrayNames(command->fieldEntries[i]->pathEString, newArg);
			
			newArg->parseTable = argDef->pParseTable;

			for (j=0; j < eaSize(&newArg->ppArrayObjects); j++)
			{
				char *containedDiffString = NULL;
				int iTempStrSize;
				LocalContainerInfo* containerInfo;
				ObjectsForAutoTransStructArg *pObjects = newArg->ppArrayObjects[j];

				containerInfo = GetLocalContainerInfo(command, pObjects->pTransVarName);

				if (containerInfo)					
				{
					pObjects->backupObject = GetFastLocalBackupCopy(command, containerInfo->conRef.containerType, containerInfo->conRef.containerID);
					if (pObjects->backupObject)
						pObjects->bBackupIsLocalCopy = true;
				}
				if (!pObjects->backupObject)
				{
					pObjects->backupObject = StructCreateWithComment(newArg->parseTable, "AutoTrans internal backup copy (Earray)");
					containedDiffString = GetTransactionVariable(objLocalManager(), command->transactionID, pObjects->pTransVarName, &iTempStrSize);
					objPathParseAndApplyOperations(newArg->parseTable,pObjects->backupObject,containedDiffString);					

					command->iBytesIn += (int)strlen(containedDiffString);
				}
				if (pObjects->bBackupIsLocalCopy)
				{
					pObjects->newObject = GetFastLocalModifyCopy(command, containerInfo->conRef.containerType, containerInfo->conRef.containerID);
					pObjects->bNewIsLocalCopy = !!pObjects->newObject;
					if (containerInfo->fullContainer)
					{
						StructCopyVoid(newArg->parseTable, pObjects->backupObject, pObjects->newObject, 0, TOK_PERSIST, 0);
					}
					else
					{
						objPathParseAndApplyOperations(newArg->parseTable,pObjects->newObject,containerInfo->localDiffString);
					}
				}
				if (!pObjects->newObject)
				{
					pObjects->newObject = StructCloneWithCommentVoid(newArg->parseTable, pObjects->backupObject, "NewArg created for AutoTrans (Earray) during RunAutoTransCB");
				}

				if (pObjects->newObject)
				{
					AddToAllContainersString(&spAllContainersString, trans->pFuncName, ParserGetTableName_WithEntityFixup(newArg->parseTable, pObjects->newObject), ParserGetStructName(newArg->parseTable, pObjects->newObject));
				}

				eaPush(&newArg->ppNewObjects_ShallowCopy, pObjects->newObject);
			}

			SetATRArg_ContainerEArray(i, newArg->ppNewObjects_ShallowCopy);
			

			eaPush(&containerArgs, newArg);
			PERFINFO_AUTO_STOP();
		}			

		xdefault:
			PERFINFO_AUTO_START("default", 1);
			for (i = 0; i < eaSize(&containerArgs); i++)
			{
				AutoTransStructArg *containerArg = containerArgs[i];
				if (containerArg->nonArrayObjects.backupObject)
				{				
					StructDestroyVoid(containerArg->parseTable, containerArg->nonArrayObjects.backupObject);
				}
				if (containerArg->nonArrayObjects.newObject)
				{				
					StructDestroyVoid(containerArg->parseTable, containerArg->nonArrayObjects.newObject);
				}
				free(containerArg);
			}
			eaDestroy(&containerArgs);
			CHECK_FOR_TIME_ALERT("Unknown Arg");
			PERFINFO_AUTO_STOP();
			PERFINFO_AUTO_STOP();// Unpack args.
			PERFINFO_AUTO_STOP();// startTransTimer.
			PERFINFO_AUTO_STOP();// FUNC.
			// clear as this is an early out
			exprCurAutoTrans = NULL;
			return 0;
		}
	}

	PERFINFO_AUTO_STOP();// Unpack args.

	estrClear(command->returnString);
	estrClear(&estrFail);

	if (bFailedDuringContainerArgs)
	{
		eaDestroy(&containerArgs);
		estrPrintf(command->returnString,"Something went fatally wrong while trying to unpack container args");
		CHECK_FOR_TIME_ALERT("failed during containers");

		PERFINFO_AUTO_STOP();// startTransTimer.
		PERFINFO_AUTO_STOP();// FUNC.
		// clear as this is an early out
		exprCurAutoTrans = NULL;
		return 0;
	}

	if (eaSize(&containerArgs) == 0)
	{
		eaDestroy(&containerArgs);
		estrPrintf(command->returnString,"No containers were locked, transaction can have no possible effect");
		Errorf("Transaction %s was performed without locking any containers. This transaction can have no possible effect and should be fixed by a programmer.", command->pTransactionName);
		CHECK_FOR_TIME_ALERT("No locks");

		PERFINFO_AUTO_STOP();// startTransTimer.
		PERFINFO_AUTO_STOP();// FUNC.
		// clear as this is an early out
		exprCurAutoTrans = NULL;
		return 0;
	}
	result = trans->pWrapperFunc(command->returnString, &estrFail);

	PERFINFO_AUTO_START("Create Diffstring", 1);
	estrClear(&diffString);

	for (i = 0; i < eaSize(&containerArgs); i++)
	{
		AutoTransStructArg *containerArg = containerArgs[i];

		//the second half of this check ensures that it only applies to "actual" container args, not struct args which
		//also end up in this earray
		if (result == TRANSACTION_OUTCOME_SUCCESS && (containerArg->nonArrayObjects.pTransVarName || containerArg->bIsArray))
		{	
			if (containerArg->bIsArray)
			{
				int j;

				for (j = 0; j < eaSize(&containerArg->ppArrayObjects); j++)
				{
					ObjectsForAutoTransStructArg *pObjects = containerArg->ppArrayObjects[j];
					
					assertmsgf(pObjects->bBackupIsLocalCopy == pObjects->bNewIsLocalCopy, "Ended up with a container arg where one but not both copies are local");
					
					if (pObjects->bBackupIsLocalCopy)
					{
						DiffLocalArgument(command, &diffString, command->transactionID, 
							pObjects->pTransVarName, containerArg->parseTable,
							pObjects->backupObject, pObjects->newObject,
							&command->iBytesOut);
					}
					else
					{
						estrClear(&diffString);

						StructWriteTextDiff(&diffString, containerArg->parseTable, pObjects->backupObject, 
							pObjects->newObject, "", TOK_PERSIST, TOK_NO_TRANSACT, 0);

						command->iBytesOut += (int)estrLength(&diffString);

						SetTransactionVariableEString(objLocalManager(), command->transactionID, pObjects->pTransVarName, &diffString);		
					}
				}
			}
			else
			{
				assertmsgf(containerArg->nonArrayObjects.bBackupIsLocalCopy == containerArg->nonArrayObjects.bNewIsLocalCopy, "Ended up with a container arg where one but not both copies are local");

				if (containerArg->nonArrayObjects.bBackupIsLocalCopy)
				{
					DiffLocalArgument(command, &diffString, command->transactionID, 
						containerArg->nonArrayObjects.pTransVarName, containerArg->parseTable,
						containerArg->nonArrayObjects.backupObject, containerArg->nonArrayObjects.newObject, &command->iBytesOut);
				}
				else
				{
					estrClear(&diffString);

					StructWriteTextDiff(&diffString,containerArg->parseTable,
						containerArg->nonArrayObjects.backupObject,containerArg->nonArrayObjects.newObject,"",TOK_PERSIST,TOK_NO_TRANSACT,0);

					command->iBytesOut += estrLength(&diffString);

					SetTransactionVariableEString(objLocalManager(),command->transactionID,containerArg->nonArrayObjects.pTransVarName, &diffString);
				}
			}
		}

		DestroyAutoTransStructArg(containerArg);
	}
	eaDestroy(&containerArgs);
	PERFINFO_AUTO_STOP();// Create Diffstring.

	exprCurAutoTrans = NULL;

	if (result != TRANSACTION_OUTCOME_SUCCESS)
	{
		estrCopy(command->returnString, &estrFail);
		CHECK_FOR_TIME_ALERT("trans failed");
		PERFINFO_AUTO_STOP();// startTransTimer.
		PERFINFO_AUTO_STOP();// FUNC.
		return 0;
	}
	else
	{
		// returnString is used for success
		CHECK_FOR_TIME_ALERT("trans succeeded");
		PERFINFO_AUTO_STOP();// startTransTimer.
		PERFINFO_AUTO_STOP();// FUNC.
		return 1;
	}
}


// Do parsing for auto transactions
static bool DecodeAutoTransArgs(char *command, TransactionCommand *pHolder)
{
	char *line = command;
	char *args[50];

	int argCount = TokenizeLineRespectingStrings(args,command) - 1;
//	int argCount = tokenize_line(command,args,NULL) - 1;
	ATR_FuncDef *trans = FindATRFuncDef(args[0]);
	int i;

	if (!command || !trans)
	{
		return false;
	}


	pHolder->userData = trans;

	for (i = 0; i < argCount; i++)
	{
		char *arg = args[i+1];
		char *pathEstr = NULL;
		ATRArgDef *argDef = &trans->pArgs[i];
		if (!argDef || !argDef->eArgType)
		{
			return false;
		}


		if (argDef->eArgType == ATR_ARG_STRING)
		{
			int iLen = (int)strlen(arg);
			estrStackCreate(&pathEstr);
			assert(iLen >= 2);
			assert(arg[0] == '\"' && arg[iLen - 1] == '\"');
			arg[iLen - 1] = 0;
			arg++;
			estrAppendUnescaped(&pathEstr, arg);
			AddFieldOperationToTransaction_NotEStrings(pHolder,TRANSOP_GET,pathEstr, estrLength(&pathEstr),NULL, 0, false);
			estrDestroy(&pathEstr);
	
		}
		else
		{
			AddFieldOperationToTransaction_NotEStrings(pHolder,TRANSOP_GET,arg, (int)strlen(arg),NULL, 0, false);

		}
	}
	return true;
}


// Runs an auto transaction, using the specified arguments. DO NOT CALL MANUALLY
AUTO_COMMAND ACMD_NAME(runautotrans) ACMD_LIST(gServerTransactionCmdList);
int ParseRunAutoTrans(TransactionCommand *trCommand, ACMD_SENTENCE command)
{
	if (IsThisObjectDB())
	{
		Errorf("Someone requested an auto transaction to run on the ObjectDB! This is not allowed!");
		devassertmsg(0, "Someone requested an auto transaction to run on the ObjectDB! This is not allowed!");
		return 0;
	}
	if (!DecodeAutoTransArgs(command,trCommand))
	{	
		return 0;
	}
	trCommand->executeCB = RunAutoTransCB;
	return 1;
}


// Remote command support


// Internal lists, which reduces the string operations
CmdList gRemoteCmdList = {1,0,0};
CmdList gSlowRemoteCmdList = {1,0,0};


// Actually execute a remote command
int RemoteCommandCB(TransactionCommand *command)
{
	CmdContext context = {0};

	if (command->bCheckingIfPossible)
	{
		return 1;
	}

	objLocalManager()->bDoingRemoteTransaction = false; // Allowed to call transactions from remote commands
	objLocalManager()->bDoingLocalTransaction = false; // Allowed to call transactions from remote commands

	context.output_msg = command->returnString;
	context.access_level = 9;
	context.multi_line = true;
	context.clientType = command->objectType;
	context.clientID = command->objectID;
	context.eHowCalled = CMD_CONTEXT_HOWCALLED_TRANSACTION;

	if (!cmdParseAndExecute(&gRemoteCmdList,command->stringData,&context))
	{
		log_printf(LOG_REMOTECOMMANDS, "Remote command \"%s\" failed with output message \"%s\"", 
			command->stringData, (context.output_msg && *context.output_msg) ? *context.output_msg : "(no message)");
		return 0;
	}	
	return 1;
}

// Actually execute a slow remote command
int SlowRemoteCommandCB(TransactionCommand *command)
{
	int result = 0;
	CmdContext context = {0};

	if (command->bCheckingIfPossible)
	{
		return 1;
	}

	objLocalManager()->bDoingRemoteTransaction = false; // Allowed to call transactions from remote commands
	objLocalManager()->bDoingLocalTransaction = false; // Allowed to call transactions from remote commands

	context.output_msg = command->returnString;
	context.access_level = 9;
	context.iSlowCommandID = command->slowTransactionID;
	context.multi_line = true;
	context.clientType = command->objectType;
	context.clientID = command->objectID;

	command->commandState = TRANSTATE_WAITING;


	if (!cmdParseAndExecute(&gSlowRemoteCmdList,command->stringData,&context))
	{
		return 0;
	}
	return 1;
}

static TransactionCommandCB remoteCommandCB = RemoteCommandCB;
static TransactionCommandCB slowRemoteCommandCB = SlowRemoteCommandCB;

void SetRemoteCommandCB(TransactionCommandCB cb)
{
	remoteCommandCB = cb;
}
void SetSlowRemoteCommandCB(TransactionCommandCB cb)
{
	slowRemoteCommandCB = cb;
}

// Runs a remote command
AUTO_COMMAND ACMD_NAME(RemoteCommand) ACMD_LIST(gServerTransactionCmdList);
int ParseRemoteCommand(TransactionCommand *trCommand, ACMD_SENTENCE command)
{
	int heap = 0;
	if(IsThisObjectDB())
		heap = CRYPTIC_CHURN_HEAP;
	trCommand->objectType = objServerType();
	trCommand->objectID = objServerID();
	trCommand->stringData = strdup_special(command, heap);
	trCommand->bRemoteCommand = true;

	trCommand->executeCB = remoteCommandCB;

	return 1;
}


// Runs a slow remote command
AUTO_COMMAND ACMD_NAME(SlowRemoteCommand) ACMD_LIST(gServerTransactionCmdList);
int ParseSlowRemoteCommand(TransactionCommand *trCommand, ACMD_SENTENCE command)
{	
	int heap = 0;
	if(IsThisObjectDB())
		heap = CRYPTIC_CHURN_HEAP;
	trCommand->objectType = objServerType();
	trCommand->objectID = objServerID();
	trCommand->stringData = strdup_special(command, heap);
	trCommand->bRemoteCommand = true;
	trCommand->bSlowTransaction = true;

	trCommand->executeCB = slowRemoteCommandCB;

	return 1;
}

// Runs a remote command
AUTO_COMMAND ACMD_NAME(RemoteCommand) ACMD_LIST(gObjectTransactionCmdList);
int ParseRemoteCommandOnObject(TransactionCommand *trCommand, ACMD_SENTENCE command)
{
	int heap = 0;
	if(IsThisObjectDB())
		heap = CRYPTIC_CHURN_HEAP;
	trCommand->stringData = strdup_special(command, heap);
	trCommand->bRemoteCommand = true;

	trCommand->executeCB = remoteCommandCB;

	return 1;
}


// Runs a slow remote command
AUTO_COMMAND ACMD_NAME(SlowRemoteCommand) ACMD_LIST(gObjectTransactionCmdList);
int ParseSlowRemoteCommandOnObject(TransactionCommand *trCommand, ACMD_SENTENCE command)
{	
	int heap = 0;
	if(IsThisObjectDB())
		heap = CRYPTIC_CHURN_HEAP;
	trCommand->stringData = strdup_special(command, heap);
	trCommand->bRemoteCommand = true;
	trCommand->bSlowTransaction = true;

	trCommand->executeCB = slowRemoteCommandCB;

	return 1;
}
