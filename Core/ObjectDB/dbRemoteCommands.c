/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "logging.h"
#include "GlobalComm.h"
#include "dbGenericDatabaseThreads.h"
#include "objContainerIO.h"
#include "objIndex.h"
#include "StringCache.h"
#include "TokenStore.h"
#include "dbReplication.h"
#include "dbSubscribe.h"
#include "file.h"
#include "strings_opt.h"
#include "ObjectDB.h"
#include "LoginCommon.h"
#include "stringUtil.h"
#include "accountnet.h"
#include "utilitiesLib.h"
#include "FloatAverager.h"
#include "NameValuePair.h"
#include "structInternals.h"
#include "GlobalStateMachine.h"
#include "GlobalTypes.h"
#include "WorldVariable.h"
#include "Alerts.h"
#include "WorkerThread.h"
#include "XboxThreads.h"
#include "structnet.h"
#include "Queue.h"
#include "wininclude.h"
#include "AccountStub.h"
#include "dbContainerRestore.h"
#include "dbOfflining.h"
#include "dbCharacterChoice.h"

#include "AutoGen/HeadshotServer_autogen_remoteFuncs.h"
#include "AutoGen/ServerLib_autogen_remoteFuncs.h"
#include "AutoGen/GameServerLib_autogen_remotefuncs.h"
#include "AutoGen/AppServerLib_autogen_remotefuncs.h"
#include "AutoGen/ChatRelay_autogen_remotefuncs.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"
#include "autogen/ObjectDB_autogen_SlowFuncs.h"

#include "textparser.h"
#include "AutoGen/GlobalTypes_h_ast.h"
#include "AutoGen/LoginCommon_h_ast.h"
#include "AutoGen/dbRemoteCommands_c_ast.h"
#include "AutoGen/ChatServer_autogen_RemoteFuncs.h"

#include "AutoGen/objContainerIO_h_ast.h"
#include "sysutil.h"

#ifdef CCGSUPPORT
#include "CCGCommon.h"
#include "AutoGen/CCGCommon_h_ast.h"
#endif /* CCGSUPPORT */

// DO NOT include structures or parse tables for any actual containers (like Entity, team, etc). 
// These will NOT work correctly on the DB, because things are packed differently

U32 dbAccountIDFromID(U32 eContainerType, U32 iContainerID);

extern ParseTable parse_NamedPathQueryAndResult[];
#define TYPE_parse_NamedPathQueryAndResult NamedPathQueryAndResult

extern bool gReportPathFailure;

CmdList gDBUpdateCmdList = {1,0,0};

static bool sbAllowMultipleCommandsPerLine = false;
AUTO_CMD_INT(sbAllowMultipleCommandsPerLine, AllowMultipleCommandsPerLine);

static bool sbCheckForMultipleCommands = false;
AUTO_CMD_INT(sbCheckForMultipleCommands, CheckForMultipleCommands);

// Commands

int dbLogContainerActivityForAllTypes = false;
AUTO_CMD_INT(dbLogContainerActivityForAllTypes, LogContainerActivityForAllTypes);

int dbContainerLog(enumLogCategory eCategory, GlobalType type, ContainerID id, bool getLock, const char *action, FORMAT_STR char const *fmt, ...)
{
	Container *pContainer;
	int result;
	va_list ap;
	char containerName[MAX_NAME_LEN] = {0};
	char accountName[MAX_NAME_LEN] = {0};
	ContainerID accountID = 0;

	if(!(dbLogContainerActivityForAllTypes || type == GLOBALTYPE_ENTITYPLAYER || type == GLOBALTYPE_ENTITYSAVEDPET))
		return 0;

	pContainer = objGetContainerEx(type, id, false, false, getLock);
	if (!pContainer)
	{
		return 0;
	}
	
	if(type == GLOBALTYPE_ENTITYPLAYER || type == GLOBALTYPE_ENTITYSAVEDPET)
	{
		if(pContainer->header)
		{
			strcpy(containerName, pContainer->header->savedName);
			strcpy(accountName, pContainer->header->pubAccountName);
			accountID = pContainer->header->accountId;
		}
		else if(pContainer->containerData)
		{
			objPathGetString(".debugName", pContainer->containerSchema->classParse, pContainer->containerData, SAFESTR(containerName));
			objPathGetString(".pPlayer.publicAccountName", pContainer->containerSchema->classParse, pContainer->containerData, SAFESTR(accountName));
		}
	}

	va_start(ap, fmt);
	result = objLog_vprintf(eCategory, type, id, accountID,
		containerName, NULL, accountName , action, NULL, fmt, ap);
	va_end(ap);

	if(getLock)
		objUnlockContainer(&pContainer);

	return result;
}

extern ObjectIndex *gAccountID_idx;
extern ObjectIndex *gAccountDisplayName_idx;
extern ObjectIndex *gAccountAccountName_idx;
extern ObjectIndex *gCreatedTime_idx;

extern ObjectIndex *gAccountIDDeleted_idx;
extern ObjectIndex *gAccountAccountNameDeleted_idx;

void dbBootContainerIDs(ContainerID *eaIDs)
{
	EARRAY_INT_CONST_FOREACH_BEGIN(eaIDs, i, n);
	{
		ContainerID id = eaIDs[i];
		Container *container = objGetContainerEx(GLOBALTYPE_ENTITYPLAYER, id, false, false, true);

		if(!container)
		{
			continue;
		}

		switch(container->meta.containerOwnerType)
		{
		case GLOBALTYPE_LOGINSERVER:
			RemoteCommand_AbortLoginByTypeAndID(GLOBALTYPE_LOGINSERVER, container->meta.containerOwnerID, container->containerType, container->containerID);
			break;
		case GLOBALTYPE_GAMESERVER:
			RemoteCommand_ForceEntityLogOut(NULL, container->meta.containerOwnerType, container->meta.containerOwnerID, container->containerID);
			break;
		}

		objUnlockContainer(&container);
	}
	EARRAY_FOREACH_END;
}

AUTO_COMMAND;
void dbBootPlayerByAccountID(ContainerID iAccountID)
{
	ContainerID *eaIDs = GetContainerIDsFromAccountID(iAccountID);
	dbBootContainerIDs(eaIDs);
	ea32Destroy(&eaIDs);
}

AUTO_COMMAND_REMOTE;
void dbBootPlayerByAccountID_Remote(ContainerID iAccountID)
{
	dbBootPlayerByAccountID(iAccountID);
}

AUTO_COMMAND;
void dbBootPlayerByAccountName(char *pAccountName)
{
	ContainerID *eaIDs = GetContainerIDsFromAccountName(pAccountName);
	dbBootContainerIDs(eaIDs);
	ea32Destroy(&eaIDs);
}

AUTO_COMMAND_REMOTE;
void dbBootPlayerByAccountName_Remote(char *pAccountName)
{
	dbBootPlayerByAccountName(pAccountName);
}

AUTO_COMMAND;
void dbBootPlayerByPublicAccountName(char *pPublicAccountName)
{
	ContainerID *eaIDs = GetContainerIDsFromDisplayName(pPublicAccountName);
	dbBootContainerIDs(eaIDs);
	ea32Destroy(&eaIDs);
}

AUTO_COMMAND_REMOTE;
void dbBootPlayerByPublicAccountName_Remote(char *pPublicAccountName)
{
	dbBootPlayerByPublicAccountName(pPublicAccountName);
}

static void dbUpdateDisplayNameRequestTransaction(GlobalType eType, ContainerID uID, const char *pDisplayName)
{
	objRequestTransactionSimplef(NULL, eType, uID, "UpdateDisplayName", "set .pPlayer.publicAccountName = \"%s\"", pDisplayName);
}

void dbUpdateDisplayName_RestoreCB(TransactionReturnVal *pReturnVal, ContainerRestoreRequest *pRequest)
{
	// We have to assume pReturnVal and pRequest are non-NULL here, but only because of the specific flow that calls this
	// Don't go making assumptions willy-nilly about transaction callbacks unless you really know what's going on
	Container *container = NULL;
	char *displayName = (char*) pRequest->callbackData.appData;

	if (pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		dbUpdateDisplayNameRequestTransaction(pRequest->pConRef->containerType, pRequest->pConRef->containerID, displayName);
	}

	CleanUpContainerRestoreRequest(&pRequest);
}

AUTO_COMMAND;
void dbUpdateDisplayName(ContainerID uAccountID, char *pNewDisplayName)
{
	ContainerID *eaIDs = GetContainerIDsFromAccountID(uAccountID);
	RestoreCallbackData callbackData = {0};

	// Start by changing display name on existing characters
	EARRAY_INT_CONST_FOREACH_BEGIN(eaIDs, i, n);
	{
		dbUpdateDisplayNameRequestTransaction(GLOBALTYPE_ENTITYPLAYER, eaIDs[i], pNewDisplayName);
	}
	EARRAY_FOREACH_END;

	ea32Destroy(&eaIDs);

	// Now request restores on offline characters and change those as well
	callbackData.cbFunc = dbUpdateDisplayName_RestoreCB;
	callbackData.appData = strdup(pNewDisplayName);
	FindOfflineCharactersAndAutoRestore(NULL, &callbackData, uAccountID);
}

AUTO_COMMAND_REMOTE;
void dbUpdateDisplayName_Remote(ContainerID uAccountID, char *pNewDisplayName)
{
	dbUpdateDisplayName(uAccountID, pNewDisplayName);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATRELAY);
void DBReturnGameAccount(GlobalType eServerType, U32 iServerId, U32 accountID)
{
    Container *con = objGetContainerEx(GLOBALTYPE_GAMEACCOUNTDATA, accountID, true, false, true);
    if(con && con->containerData)
    {
        char *estrGameAccountData = NULL;
        if(!ParserWriteText(&estrGameAccountData, con->containerSchema->classParse, con->containerData, 0, 0, 0))
			estrDestroy(&estrGameAccountData);
		ANALYSIS_ASSUME(con);
		objUnlockContainer(&con);
        RemoteCommand_crReceiveGameAccountData(eServerType, iServerId, estrGameAccountData);
        estrDestroy(&estrGameAccountData);
	}
	else if(con)
	{
		objUnlockContainer(&con);
	}
}

// Change the ownership of all containers of a specific type to another server.
// Called once by the requesting server at startup (e.g. Team/Guild/Auction server)
AUTO_COMMAND_REMOTE;
ContainerList *dbAcquireContainers(GlobalType eServerType, U32 iServerID, GlobalType containerType)
{
	ContainerIterator iter;
	Container *container;
	ContainerStore *store = objFindContainerStoreFromType(containerType);
	ContainerList *pContainers = StructCreate(parse_ContainerList);

	pContainers->type = containerType;
	if (!store) {
		return pContainers;
	}

	verbose_printf("Sending list of %s containers to server:%s[%d]\n", GlobalTypeToName(containerType), GlobalTypeToName(eServerType), iServerID);

	objInitContainerIteratorFromType(containerType, &iter);

	while (container = objGetNextContainerFromIterator(&iter)) {
		eaiPush(&pContainers->eaiContainers, container->containerID);
	}
	objClearContainerIterator(&iter);

	return pContainers;
}

//This command returns a list of all containers of a given type whose IDs modulo div are equal to mod.
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(TESTSERVER);
ContainerList *dbRaidContainers(int div, int mod, GlobalType containerType)
{
	ContainerIterator iter;
	Container *container;
	ContainerStore *store = objFindContainerStoreFromType(containerType);
	ContainerList *pContainers = StructCreate(parse_ContainerList);

	pContainers->type = containerType;
	if (!store) {
		return pContainers;
	}

	objInitContainerIteratorFromType(containerType, &iter);

	while (container = objGetNextContainerFromIterator(&iter)) {
		if (container->containerID%div == mod)
			eaiPush(&pContainers->eaiContainers, container->containerID);
	}
	objClearContainerIterator(&iter);

	return pContainers;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(TESTSERVER);
ContainerList *dbPercContainers(GlobalType containerType, U32 storeindex, int batchsize)
{
	ContainerIterator iter;
	Container *container;
	ContainerStore *store = objFindContainerStoreFromType(containerType);
	ContainerList *pContainers = StructCreate(parse_ContainerList);
	int i = 0;

	pContainers->type = containerType;
	if (!store) {
		return pContainers;
	}

	objInitContainerIteratorFromType(containerType, &iter);
	iter.storeIndex = storeindex;

	while (i < batchsize && (container = objGetNextContainerFromIterator(&iter))) {
		eaiPush(&pContainers->eaiContainers, container->containerID);
		i++;
	}
	pContainers->storeindex = iter.storeIndex;

	if (!objGetNextContainerFromIterator(&iter))
		pContainers->storeindex = -1;
	objClearContainerIterator(&iter);

	return pContainers;
}

// This is a last-ditch effort to log out a character
AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void DBForciblyLogOutCharacter(U32 containerType, U32 containerID)
{
	Container *pObject = objGetContainerEx(containerType,containerID, true, false, true);
	char *updateString = NULL;
	char commentString[1024];
	if (!pObject)
	{
		return;
	}
	estrStackCreate(&updateString);

	estrPrintf(&updateString,"offline %s %d %s %d",GlobalTypeToName(containerType),containerID,GlobalTypeToName(pObject->meta.containerOwnerType),pObject->meta.containerOwnerID);
	objUnlockContainer(&pObject);

	sprintf(commentString, "%s[%u] is forcibly logging out character %s[%u]",GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID(), GlobalTypeToName(containerType),containerID);
	SendTransactionServerCommand(objLocalManager(),updateString, commentString);
	estrPrintf(&updateString,"dbUpdateContainerOwner %d %d %d %d",containerType,containerID,objServerType(),objServerID());
	dbHandleDatabaseUpdateString(updateString,0,0);
	estrDestroy(&updateString);
}

// ObjectDB container updates

static void objDBAlertResolveFailure(const char *tableName, char *path, char **errorString)
{
	estrPrintf(errorString, "Failure to resolve path %s while applying operations to object type %s.", path, tableName);

	if (isDevelopmentMode() && gReportPathFailure)
	{
		ErrorDeferredf("Failure to resolve path %s. Your schema files need to be regenerated, your manual transaction has an error, or there is an error in the object system", path);
	}
}

static void objDBAlertApplyFailure(const char *tableName, ObjectPathOpType op, char *path, char *value, char *opResult, char **errorString)
{
	estrPrintf(errorString, "Failure to execute path operation: %d %s %s on object of type %s; %s", op, path, value, tableName, opResult);

	if (isDevelopmentMode())
	{
		ErrorDeferredf("Failure to execute path operation: %d %s %s. Your schema files need to be regenerated, your manual transaction has an error, or there is an error in the object system", op, path, value);
	}
}

typedef struct objDBUpdateApplyOperationsData
{
	char *tempResult;
	char *opResult;
	ObjectPath **paths;
} objDBUpdateApplyOperationsData;

int objDBUpdateApplyOperations(char **broadcastDiff, Container *object, ObjectPathOperation **pathOperations, char **errorString)
{
	objDBUpdateApplyOperationsData *threadData;
	int i, j, indices = 0, success = 1;
	int *indicesAffected = NULL;
	ParseTable *table_in = object->containerSchema->classParse;
	void *structptr_in = object->containerData;
	ContainerStore *store = objFindContainerStoreFromType(object->containerType);
	bool reparsedHeader = false;
	STATIC_THREAD_ALLOC(threadData);

	if (!store)
		return 0;

	PERFINFO_AUTO_START_FUNC();

	indices = eaSize(&store->indices);

	if (indices && !object->isTemporary)
	{
		eaiStackCreate(&indicesAffected, indices);
		for (j = 0; j < indices; ++j)
			eaiPush(&indicesAffected, 0);
	}

	if (eaSize(&pathOperations) > eaCapacity(&threadData->paths))
		eaSetCapacity(&threadData->paths, eaSize(&pathOperations));

	for (i = 0; i < eaSize(&pathOperations) && success; ++i)
	{
		ObjectPathOperation *op = pathOperations[i];
		ObjectPath *path = NULL;
		ParseTable *table = NULL;
		int column = 0;
		void *structptr = NULL;
		int index = 0;
		int pathFlags = OBJPATHFLAG_DONTLOOKUPROOTPATH | OBJPATHFLAG_WRITERESULTIFNOBROADCAST | OBJPATHFLAG_INCREASEREFCOUNT;

		estrClear(&threadData->tempResult);
		success = ParserResolvePathEx(op->pathEString, table_in, structptr_in, &table, &column, &structptr, &index, &path, &threadData->tempResult, NULL, NULL, pathFlags);
		eaPush(&threadData->paths, path);

		if (!success)
		{
			objDBAlertResolveFailure(ParserGetTableName(table_in), op->pathEString, errorString);
			continue;
		}

		if (!object->isTemporary)
		{
			for (j = 0; j < indices; ++j)
			{
				ObjectIndex *oi = store->indices[j];

				if (indicesAffected[j]) continue;
				if (!objIndexPathAffected(oi, path)) continue;

				// If this container type doesn't use headers, then the actual fields of the object are
				// indexed, which means the index's key fields may change while the object is in the index.
				// We can't allow this, so we /have/ to lock the indices and remove the object from them
				// right now.
				if (!store->requiresHeaders)
				{
					PERFINFO_AUTO_START("Remove from Index", 1);
					objIndexObtainWriteLock(oi);
					objIndexRemove(oi, structptr_in);
					PERFINFO_AUTO_STOP();
				}

				// Mark the index as being affected, headers or no. If we're using headers, we'll just
				// remove the object from the affected indices after the update instead of now.
				indicesAffected[j] = 1;
			}
		}

		estrClear(&threadData->opResult);
		success = objPathApplySingleOperation(table, column, structptr, index, op->op, op->valueEString, op->quotedValue, &threadData->opResult);

		if (!success)
		{
			objDBAlertApplyFailure(ParserGetTableName(table_in), op->op, op->pathEString, op->valueEString, threadData->opResult, errorString);
		}

		if (!estrLength(&threadData->tempResult))
		{
			objPathWriteSingleOperation(broadcastDiff, op);
			estrConcatChar(broadcastDiff, '\n');
		}
	}

	if (!object->isTemporary)
	{
		// If this container type does use headers, now is the time to lock the indices and
		// remove the old header from them.
		if (store->requiresHeaders)
		{
			for (j = 0; j < indices; ++j)
			{
				ObjectIndex *oi = store->indices[j];

				if (!indicesAffected[j]) continue;

				PERFINFO_AUTO_START("Remove from Index", 1);
				objIndexObtainWriteLock(oi);
				objIndexRemove(oi, object->header);
				PERFINFO_AUTO_STOP();
			}
		}

		for (j = 0; j < indices; ++j)
		{
			ObjectIndex *oi = store->indices[j];

			if (!indicesAffected[j]) continue;

			PERFINFO_AUTO_START("Insert into Index", 1);
			if (store->requiresHeaders)
			{
				if(!reparsedHeader)
				{
					objGenerateHeader(object, store->containerSchema);
					reparsedHeader = true;
				}
				objIndexInsert(oi, object->header);
			}
			else
				objIndexInsert(oi, object->containerData);
			objIndexReleaseWriteLock(oi);
			PERFINFO_AUTO_STOP();
		}
	}

	eaClearEx(&threadData->paths, ObjectPathDestroy);
	eaiDestroy(&indicesAffected);

	PERFINFO_AUTO_STOP();
	return success;
}

typedef struct UpdatesByContainerType
{
	U64 updateCount;
	U64 updateLineCount;
} UpdatesByContainerType;

UpdatesByContainerType gUpdatesByContainerType[GLOBALTYPE_MAX];

void dbRecordUpdateLines(GlobalType containerType, U32 operationCount)
{
#ifdef _M_X64 //When running a 32 bit objectdb, these numbers just don't get populated
	InterlockedIncrement64(&gUpdatesByContainerType[containerType].updateCount);
	InterlockedExchangeAdd64(&gUpdatesByContainerType[containerType].updateLineCount, operationCount);
#endif
}

void GetUpdatesByContainerType(GlobalType containerType, U64 *countOut, U64 *lineCountOut)
{
	assert(countOut && lineCountOut);
	*countOut = gUpdatesByContainerType[containerType].updateCount;
	*lineCountOut = gUpdatesByContainerType[containerType].updateLineCount;
}

typedef struct objDBModifyContainerData
{
	char *opResult;
	ObjectPathOperation **pathOperations;
	ObjectPath **cachedpaths;
} objDBModifyContainerData;

int objDBModifyContainer(char **broadcastDiff, Container *object, char *diffString)
{
	objDBModifyContainerData *threadData;
	int success = 1;
	STATIC_THREAD_ALLOC(threadData);

	if (!object || !diffString)
	{
		return 0;
	}
	PERFINFO_AUTO_START_FUNC();
	
	if (!objPathParseOperations(object->containerSchema->classParse, diffString, &threadData->pathOperations))
	{
		eaClearEx(&threadData->pathOperations, DestroyObjectPathOperation);
		PERFINFO_AUTO_STOP();
		return 0;
	}

	dbRecordUpdateLines(object->containerType, eaSize(&threadData->pathOperations));

	objContainerPrepareForModify(object);
	objContainerCommitNotify(object, threadData->pathOperations, true);

	if (!objDBUpdateApplyOperations(broadcastDiff, object, threadData->pathOperations, &threadData->opResult))
		success = 0;

	if (success)
	{
		objContainerCommitNotify(object, threadData->pathOperations, false);
		objContainerMarkModified(object);
	}
	else
	{
		log_printf(LOG_OBJTRANS,"Error Applying Diff to " CON_PRINTF_STR ". %s: FULL DIFF %s",
			CON_PRINTF_ARG(object->containerType, object->containerID), threadData->opResult, diffString);

		if (IsThisMasterObjectDB())
		{		
			TriggerAlertf("OBJECTDB.DATA_CORRUPTION", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, object->containerType, object->containerID, GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0,
				"ObjectDB Diff Apply Error on " CON_PRINTF_STR ". %s: FULL DIFF %s", CON_PRINTF_ARG(object->containerType, object->containerID), threadData->opResult, diffString);
		}
	}

	eaClearEx(&threadData->pathOperations, DestroyObjectPathOperation);

	estrClear(&threadData->opResult);
	PERFINFO_AUTO_STOP();
	return success;
}

// Write out the container to disk, used for db updates like headers to force a save correctly
int dbWriteContainer_CB(ContainerUpdateInfo *info)
{
	Container *pObject = objGetContainerEx(info->containerType, info->containerID, false, false, false);

	if (!pObject)
		return 1;

	verbose_printf("Writing container %s[%d]\n",GlobalTypeToName(info->containerType),info->containerID);

	objContainerMarkModified(pObject);

	return 1;
}

AUTO_COMMAND ACMD_LIST(gDBUpdateCmdList);
int dbWriteContainer(U32 containerType, U32 containerID)
{
	ContainerUpdateInfo info = {0};
	if(!containerType)
		return 0;
	info.containerID = containerID;
	info.containerType = containerType;
	if(GenericDatabaseThreadIsActive() && !OnBackgroundGenericDatabaseThread())
	{
		QueueWriteContainerOnGenericDatabaseThreads(&info);
		return 1;
	}

	return dbWriteContainer_CB(&info);
}

typedef struct dbUpdateContainer_CBThreadData
{
	char *broadcastDiff;
} dbUpdateContainer_CBThreadData;

int dbUpdateContainer_CB(GWTCmdPacket *packet, ContainerUpdateInfo *info, bool getLock)
{
	dbUpdateContainer_CBThreadData *threadData;
	GlobalType containerType = info->containerType;
	ContainerID containerID = info->containerID;
	char *diffString = info->diffString;

	Container *pObject;
	STATIC_THREAD_ALLOC(threadData);
	if (!containerType)
	{
		SAFE_FREE(diffString);
		return 0;
	}
	PERFINFO_AUTO_START("dbUpdateContainer",1);

	verbose_printf("Updating data of container %s[%d] using diff %s\n",GlobalTypeToName(containerType),containerID,diffString);

	pObject = objGetContainer(containerType,containerID);

	if (gDatabaseConfig.iDebugLagOnUpdate)
	{
		Sleep(gDatabaseConfig.iDebugLagOnUpdate);
	}

	estrClear(&threadData->broadcastDiff);
	if (pObject)
	{
		objDBModifyContainer(&threadData->broadcastDiff, pObject, diffString);
		FreeZippedContainerData(pObject);
		if (SAFE_DEREF(threadData->broadcastDiff))
		{
			dbContainerChangeNotify(packet, containerType, containerID, threadData->broadcastDiff);
		}
		//filelog_printf("subscription.log", "ORIGINAL: %s\n NEW: %s\n", diffString, broadcastDiff);
	}
	else
	{
		if (!objAddToRepositoryFromString(containerType, containerID, diffString))
		{
			PERFINFO_AUTO_STOP();
			return 0;
		}
		dbContainerCreateNotify(packet, containerType, containerID);
		dbContainerLog(LOG_LOGIN, containerType, containerID, getLock, "NewContainer", "Type %s, ID %u", GlobalTypeToName(containerType), containerID);
	}
	SAFE_FREE(diffString);
	PERFINFO_AUTO_STOP();
	return 1;
}

// Update the local copy of a container on this server, using a diff log
AUTO_COMMAND ACMD_LIST(gDBUpdateCmdList);
int dbUpdateContainer(U32 containerType, U32 containerID, ACMD_SENTENCE diffString)
{
	ContainerUpdateInfo info = {0};
	if(!containerType)
		return 0;
	info.containerID = containerID;
	info.containerType = containerType;
	info.diffString = strdup(diffString); 
	if(GenericDatabaseThreadIsActive() && !OnBackgroundGenericDatabaseThread())
	{
		QueueDBUpdateContainerOnGenericDatabaseThreads(&info);
		return 1;
	}

	return dbUpdateContainer_CB(NULL, &info, true);
}

AUTO_COMMAND_REMOTE;
int RemoteUpdateContainer(U32 type, U32 id, char *diff)
{
	return dbUpdateContainer(type, id, diff);
}

extern DatabaseState gDatabaseState;

// Permanently destroys a container
int dbDestroyContainer_CB(GWTCmdPacket *packet, ContainerUpdateInfo *info, bool getLock)
{
	Container *con;
	PERFINFO_AUTO_START("dbDestroyContainer",1);

	con = objGetContainer(info->containerType, info->containerID);

	if (!con || !con->containerData)
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	verbose_printf("Permanently destroying container %s[%d]\n",GlobalTypeToName(info->containerType),info->containerID);

	dbContainerLog(LOG_LOGIN, info->containerType, info->containerID, getLock, "DestroyContainer", "Type %s, ID %u", GlobalTypeToName(info->containerType), info->containerID);

	FreeZippedContainerData(con);
	dbContainerDeleteNotify(packet, info->containerType, info->containerID, true);

	objRemoveContainerFromRepository(info->containerType,info->containerID);

	PERFINFO_AUTO_STOP();
	return 1;
}

AUTO_COMMAND ACMD_LIST(gDBUpdateCmdList);
int dbDestroyContainer(char *containerTypeName, U32 containerID)
{
	ContainerUpdateInfo info = {0};
	GlobalType containerType = NameToGlobalType(containerTypeName);
	if (!containerType)
	{
		return 0;
	}
	info.containerID = containerID;
	info.containerType = containerType;
	if(GenericDatabaseThreadIsActive() && !OnBackgroundGenericDatabaseThread())
	{
		QueueDestroyContainerOnGenericDatabaseThreads(&info);
		return 1;
	}

	return dbDestroyContainer_CB(NULL, &info, true);
}

// Permanently destroys a deleted container
int dbDestroyDeletedContainer_CB(GWTCmdPacket *packet, ContainerUpdateInfo *info, bool getLock)
{
	Container *con;
	PERFINFO_AUTO_START("dbDestroyDeletedContainer",1);

	con = objGetDeletedContainer(info->containerType, info->containerID);

	if (!con || !con->containerData)
	{
		return 0;
	}

	verbose_printf("Permanently destroying container %s[%d]\n",GlobalTypeToName(info->containerType),info->containerID);

	dbContainerLog(LOG_LOGIN, info->containerType, info->containerID, getLock, "DestroyDeletedContainer", "Type %s, ID %u", GlobalTypeToName(info->containerType), info->containerID);

	FreeZippedContainerData(con);
	dbContainerDeleteNotify(packet, info->containerType, info->containerID, true);

	objRemoveDeletedContainerFromRepository(info->containerType,info->containerID);

	PERFINFO_AUTO_STOP();
	return 1;
}

AUTO_COMMAND ACMD_LIST(gDBUpdateCmdList);
int dbDestroyDeletedContainer(char *containerTypeName, U32 containerID)
{
	ContainerUpdateInfo info = {0};
	GlobalType containerType = NameToGlobalType(containerTypeName);
	if (!containerType)
	{
		return 0;
	}
	info.containerID = containerID;
	info.containerType = containerType;
	if(GenericDatabaseThreadIsActive() && !OnBackgroundGenericDatabaseThread())
	{
		QueueDestroyDeletedContainerOnGenericDatabaseThreads(&info);
		return 1;
	}

	return dbDestroyDeletedContainer_CB(NULL, &info, true);
}

// Deletes a container and caches it
int dbDeleteContainer_CB(GWTCmdPacket *packet, ContainerDeleteInfo *info, bool getLock)
{
	PERFINFO_AUTO_START("dbDeleteContainer",1);

	verbose_printf("Caching deleted container %s[%d]\n",GlobalTypeToName(info->containerType),info->containerID);

	dbContainerLog(LOG_LOGIN, info->containerType, info->containerID, getLock, "DeleteContainer", "Type %s, ID %u", GlobalTypeToName(info->containerType), info->containerID);

	dbContainerDeleteNotify(packet, info->containerType, info->containerID, info->cleanup);

	objDeleteContainer(info->containerType, info->containerID, objServerType(), objServerID(), info->destroyNow);

	PERFINFO_AUTO_STOP();
	return 1;
}

AUTO_COMMAND ACMD_LIST(gDBUpdateCmdList);
int dbDeleteContainer(char *containerTypeName, U32 containerID, bool destroyNow, bool cleanup)
{
	ContainerDeleteInfo info = {0};
	GlobalType containerType = NameToGlobalType(containerTypeName);
	if (!containerType)
	{
		return 0;
	}
	info.containerID = containerID;
	info.containerType = containerType;
	info.destroyNow = destroyNow;
	info.cleanup = cleanup;
	if(GenericDatabaseThreadIsActive() && !OnBackgroundGenericDatabaseThread())
	{
		QueueDeleteContainerOnGenericDatabaseThreads(&info);
		return 1;
	}

	return dbDeleteContainer_CB(NULL, &info, true);
}

// Removes a deleted container from the cache and makes it usable again.
int dbUndeleteContainer_CB(ContainerUndeleteInfo *info, bool getLock)
{
	Container *con;
	PERFINFO_AUTO_START("dbUndeleteContainer",1);

	con = objGetDeletedContainer(info->containerType, info->containerID);

	if (!con || !con->containerData)
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	verbose_printf("Restoring deleted container %s[%d]\n",GlobalTypeToName(info->containerType),info->containerID);

	dbContainerLog(LOG_LOGIN, info->containerType, info->containerID, getLock, "UndeleteContainer", "Type %s, ID %u", GlobalTypeToName(info->containerType), info->containerID);
		
	objUndeleteContainer(info->containerType, info->containerID, objServerType(), objServerID(), NULL);

	PERFINFO_AUTO_STOP();
	return 1;
}

AUTO_COMMAND ACMD_LIST(gDBUpdateCmdList);
int dbUndeleteContainer(char *containerTypeName, U32 containerID)
{
	ContainerUndeleteInfo info = {0};
	GlobalType containerType = NameToGlobalType(containerTypeName);
	if (!containerType)
	{
		return 0;
	}
	info.containerID = containerID;
	info.containerType = containerType;
	if(GenericDatabaseThreadIsActive() && !OnBackgroundGenericDatabaseThread())
	{
		QueueUndeleteContainerOnGenericDatabaseThreads(&info);
		return 1;
	}

	return dbUndeleteContainer_CB(&info, true);
}

// Removes a deleted container from the cache and makes it usable again.
int dbUndeleteContainerWithRename_CB(ContainerUndeleteInfo *info, bool getLock)
{
	Container *con;
	PERFINFO_AUTO_START("dbUndeleteContainerWithRename",1);

	con = objGetDeletedContainer(info->containerType, info->containerID);

	if (!con || !con->containerData)
	{
		SAFE_FREE(info->namestr);
		return 0;
	}

	verbose_printf("Restoring deleted container %s[%d]\n",GlobalTypeToName(info->containerType),info->containerID);

	dbContainerLog(LOG_LOGIN, info->containerType, info->containerID, getLock, "UndeleteContainer", "Type %s, ID %u%s%s%s", GlobalTypeToName(info->containerType), info->containerID, info->namestr && *info->namestr ? ", NewName \"" : "", info->namestr && *info->namestr ? info->namestr : "", info->namestr && *info->namestr ? "\"" : "");
		
	objUndeleteContainer(info->containerType, info->containerID, objServerType(), objServerID(), info->namestr);

	SAFE_FREE(info->namestr);
	PERFINFO_AUTO_STOP();
	return 1;
}

AUTO_COMMAND ACMD_LIST(gDBUpdateCmdList);
int dbUndeleteContainerWithRename(char *containerTypeName, U32 containerID, const char *namestr)
{
	ContainerUndeleteInfo info = {0};
	GlobalType containerType = NameToGlobalType(containerTypeName);
	if (!containerType)
	{
		return 0;
	}
	info.containerID = containerID;
	info.containerType = containerType;
	info.namestr = strdup(namestr);
	if(GenericDatabaseThreadIsActive() && !OnBackgroundGenericDatabaseThread())
	{
		QueueUndeleteContainerWithRenameOnGenericDatabaseThreads(&info);
		return 1;
	}

	return dbUndeleteContainerWithRename_CB(&info, true);
}

//ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(OBJECTDB)
AUTO_COMMAND_REMOTE;
void DBUpdateNonTransactedData(U32 containerType, U32 containerID, ACMD_SENTENCE diffString)
{
	char *updateString = NULL;
	estrStackCreateSize(&updateString,10000);

	estrPrintf(&updateString,"dbUpdateContainer %d %d ",containerType,containerID);
	estrConcatf(&updateString,"comment NonTransactedData %u %u\n", objServerType(), objServerID());
	estrAppend2(&updateString, diffString);

	dbHandleDatabaseUpdateString(updateString,0,0);

	estrDestroy(&updateString);
}


// Updates a server's copy of a container's ownership data
int dbUpdateContainerOwner_CB(GWTCmdPacket *packet, ContainerOwnerUpdateInfo *info, bool getLock)
{
	GlobalType containerType = info->containerType;
	ContainerID containerID = info->containerID;
	GlobalType ownerType = info->ownerType;
	ContainerID ownerID = info->ownerID;
	ContainerState state;
	Container *pObject;

	if (!containerType || !ownerType)
	{
		return 0;
	}

	PERFINFO_AUTO_START("dbUpdateContainerOwner",1);

	verbose_printf("Updating owner of container %s[%d] to %s[%d]\n",GlobalTypeToName(containerType),containerID,GlobalTypeToName(ownerType),ownerID);


	pObject = objGetContainerEx(containerType,containerID, false, false, false);

	if (!pObject)
	{
		dbContainerLog(LOG_LOGIN, containerType, containerID, getLock, "NewContainer", "Type %s, ID %u", GlobalTypeToName(containerType), containerID);
		pObject = objAddToRepositoryFromString(containerType,containerID,"");
	}

	if (!pObject)
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	if (ownerType == objServerType() && (ownerID == objServerID() || ownerID == 0))
	{	
		state = CONTAINERSTATE_OWNED;
	}
	else
	{
		state = CONTAINERSTATE_DB_COPY;
	}

	objChangeContainerState(pObject, state, ownerType, ownerID);
	dbContainerOwnerChangeNotify(packet, containerType, containerID, ownerType, ownerID, getLock);

	PERFINFO_AUTO_STOP();
	return 1;
}

AUTO_COMMAND ACMD_LIST(gDBUpdateCmdList);
int dbUpdateContainerOwner(U32 containerType, U32 containerID,U32 ownerType, U32 ownerID)
{
	ContainerOwnerUpdateInfo info = {0};
	if (!containerType || !ownerType)
	{
		return 0;
	}
	info.containerID = containerID;
	info.containerType = containerType;
	info.ownerType = ownerType;
	info.ownerID = ownerID;
	if(GenericDatabaseThreadIsActive() && !OnBackgroundGenericDatabaseThread())
	{
		QueueDBUpdateContainerOwnerOnGenericDatabaseThreads(&info);
		return 1;
	}

	return dbUpdateContainerOwner_CB(NULL, &info, true);
}

void dbUpdateCB(TransDataBlock ***pppBlocks, void *pUserData)
{
	dbHandleDatabaseUpdateDataBlocks(pppBlocks,0,0);
}

// This is NOT recursion safe, for efficiency
void dbHandleDatabaseUpdateString(const char *cmd_orig, U64 sequence, U32 timestamp)
{
	dbHandleDatabaseUpdateStringEx(cmd_orig, sequence, timestamp, false);
}

void dbHandleDatabaseReplayString(const char *cmd_orig, U64 sequence, U32 timestamp)
{
	dbHandleDatabaseUpdateStringEx(cmd_orig, sequence, timestamp, true);
	MonitorGenericDatabaseThreads();
}

AUTO_COMMAND_REMOTE;
void RemoteHandleDatabaseUpdateString(const char *cmd, U64 sequence, U32 timestamp)
{
	dbHandleDatabaseUpdateString(cmd, sequence, timestamp);
}

AUTO_COMMAND_REMOTE;
void RemoteHandleDatabaseReplayString(const char *cmd, U64 sequence, U32 timestamp)
{
	static U32 startTime = 0;
	static U32 startTimestamp = 0;
	static U32 lastTime = 0;

	static U32 startHourTime = 0;
	static U32 startHourTimestamp = 0;
	static U32 startMinuteTime = 0;
	static U32 startMinuteTimestamp = 0;

	U32 currentTime;
	U32 playbackDuration;
	U32 logDuration;

	currentTime = timerCpuSeconds();

	if(!startTime || !startTimestamp)
	{
		startTime = currentTime;
		startTimestamp = timestamp;
	}

	if(!startHourTimestamp || !startHourTime || timestamp >= startHourTimestamp + 60*60)
	{
		startHourTime = currentTime;
		startHourTimestamp = timestamp;
	}

	if(!startMinuteTimestamp || timestamp >= startMinuteTimestamp + 5*60)
	{
		startMinuteTime = currentTime;
		startMinuteTimestamp = timestamp;
	}

	if(!lastTime || currentTime > (lastTime + 3))
	{
		char timestampStr[64];
		timeMakeDateStringFromSecondsSince2000(timestampStr, timestamp);

		playbackDuration = currentTime - startTime;
		logDuration = timestamp - startTimestamp;

		if(playbackDuration <= logDuration)
			printf("[%s] total ahead: %u s", timestampStr, logDuration - playbackDuration);
		else
			printf("[%s] total BEHIND: %u s", timestampStr, playbackDuration - logDuration);

		playbackDuration = currentTime - startHourTime;
		logDuration = timestamp - startHourTimestamp;

		if(playbackDuration <= logDuration)
			printf(", hour ahead: %u s", logDuration - playbackDuration);
		else
			printf(", hour BEHIND: %u s", playbackDuration - logDuration);

		playbackDuration = currentTime - startMinuteTime;
		logDuration = timestamp - startMinuteTimestamp;

		if(playbackDuration <= logDuration)
			printf(", 5 minute ahead: %u s\n", logDuration - playbackDuration);
		else
			printf(", 5 minute BEHIND: %u s\n", playbackDuration - logDuration);

		lastTime = currentTime;
	}

	dbHandleDatabaseReplayString(cmd, sequence, gContainerSource.strictMerge ? 0 : timestamp);
}

static void dbRunUpdateCmds(const char* cmds)
{
	static CmdContext context = {0};
	static char* estrLine;
	static char* context_message;

	const char* cmd;
	S32			result;

	context.output_msg = &context_message;
	context.access_level = 9;
	context.multi_line = true;


	if (sbAllowMultipleCommandsPerLine)
	{
		int iCmdCount = 0;

		for(cmd = cmds; cmd;)
		{
			
			const char* cmdToRun;

			if(!cmdReadNextLineConst(&cmd, &estrLine, &cmdToRun))
			{
				break;
			}
		
			iCmdCount++;
			if (iCmdCount > 1 && sbCheckForMultipleCommands)
			{
				AssertOrAlert("MULTIPLE_COMMANDS_PER_LINE", "More than one command was in a single cmd in the object DB, this is not expected");
			}

			result = cmdParseAndExecute(&gDBUpdateCmdList,cmdToRun,&context);
	
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
				ErrorDetailsf("DBUpdateCommand: %s", cmdToRun);
				Errorf("Error \"%s\" while executing DBUpdateCommand",context_message);
			}
		}
	}
	else
	{
		result = cmdParseAndExecute(&gDBUpdateCmdList,cmds,&context);
	
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
			ErrorDetailsf("DBUpdateCommand: %s", cmds);
			Errorf("Error \"%s\" while executing DBUpdateCommand",context_message);
		}
	}
}

void dbHandleDatabaseUpdateStringEx(const char *cmd, U64 sequence, U32 timestamp, bool replay)
{
	PERFINFO_AUTO_START_FUNC();
	
	// This may cause a log rotation as well
	//objRotateIncrementalHog();
	// We no longer care that log files and hogg files 100% match in filename -BZ

	if (!timestamp)
	{
		//if (replay) assertmsg(timestamp, "Timestamp must be set for replay updates.\n");
		timestamp = timeSecondsSince2000();
	}

	if (!sequence)
	{
		if (replay) assertmsg(sequence, "Sequence number must be set for replay updates.\n");
		sequence = objContainerGetSequence() + 1;
	}
	objContainerSetSequence(sequence);
	objContainerSetTimestamp(timestamp);

	if(!replay) 
	{
		dbLogTransaction(cmd, sequence, timestamp);
	}

	dbRunUpdateCmds(cmd);

	//if (!replay) dbLogFlushTransactions();

	PERFINFO_AUTO_STOP();
}


// Parse and execute an earray of Database Update TransDataBlocks
void dbHandleDatabaseUpdateDataBlocks(TransDataBlock ***pppBlocks, U64 sequence, U32 timestamp)
{
	dbHandleDatabaseUpdateDataBlocksEx(pppBlocks, sequence, timestamp, false);
}

void dbHandleDatabaseReplayDataBlocks(TransDataBlock ***pppBlocks, U64 sequence, U32 timestamp)
{
	dbHandleDatabaseUpdateDataBlocksEx(pppBlocks, sequence, timestamp, true);
}

// Parse and execute a Database Update String; replay=true will disable logging and require seqence and timestamp to be valid.
void dbHandleDatabaseUpdateDataBlocksEx(TransDataBlock ***pppBlocks, U64 sequence, U32 timestamp, bool replay)
{
	int i;

	for (i=0; i < eaSize(pppBlocks); i++)
	{
		char *pString = (*pppBlocks)[i]->pString1;
		if (pString)
		{
			dbHandleDatabaseUpdateStringEx(pString, sequence, timestamp, replay);
		}
		pString = (*pppBlocks)[i]->pString2;
		if (pString)
		{
			dbHandleDatabaseUpdateStringEx(pString, sequence, timestamp, replay);
		}
	}
}


AUTO_COMMAND_REMOTE;
char *dbNameFromID(U32 containerType, U32 containerID)
{
	static char characterName[MAX_NAME_LEN];
	Container *container;

	container = objGetContainerEx(containerType,containerID, false, false, true);
	if (!container)
	{
		return NULL;
	}

	if (!container->header->savedName)
	{
		objUnlockContainer(&container);
		return NULL;
	}

	strcpy(characterName, container->header->savedName);
	objUnlockContainer(&container);
	return characterName;
}

AUTO_COMMAND_REMOTE;
char *dbNameAndPublicAccountFromID(U32 containerType, U32 containerID)
{
	static char *characterName;
	Container *container;

	container = objGetContainerEx(containerType,containerID, false, false, true);
	if (!container)
	{
		return NULL;
	}

	if (!container->header->savedName || !container->header->pubAccountName)
	{
		objUnlockContainer(&container);
		return NULL;
	}

	estrClear(&characterName);
	estrPrintf(&characterName, "%s@%s", container->header->savedName, container->header->pubAccountName);
	objUnlockContainer(&container);
	return characterName;
}

AUTO_COMMAND_REMOTE;
U32 dbAccountIDFromID(U32 eContainerType, U32 iContainerID)
{
	U32 iAccountID;
	Container *pContainer;
	
	pContainer = objGetContainerEx(eContainerType, iContainerID, false, false, true);
	if (!pContainer)
	{
		return 0;
	}
	
	iAccountID = pContainer->header->accountId;
	objUnlockContainer(&pContainer);
	return iAccountID;
}

void dbIDFromNameAndPublicAccountWithRestore_SlowReturn_CB(TransactionReturnVal *returnVal, ContainerRestoreRequest *request)
{
	if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		SlowRemoteCommandReturn_dbIDFromNameAndPublicAccountWithRestore(request->callbackData.slowCommandID, request->sourceID);
	}
	else
	{
		SlowRemoteCommandReturn_dbIDFromNameAndPublicAccountWithRestore(request->callbackData.slowCommandID, 0);
	}
	CleanUpContainerRestoreRequest(&request);
}

void dbIDFromNameAndPublicAccountWithRestore_CB(TransactionReturnVal *returnVal, AccountRestoreRequest *accountRestoreRequest)
{
	if (RemoteCommandCheck_aslAPCmdGetAccountIDFromDisplayName(returnVal, &accountRestoreRequest->accountID) == TRANSACTION_OUTCOME_SUCCESS)
	{
		// Check for offline characters
		if(!FindOfflineCharactersAndAutoRestoreEx(NULL, NULL, &accountRestoreRequest->callbackData, accountRestoreRequest->accountID, accountRestoreRequest->characterName))
			SlowRemoteCommandReturn_dbIDFromNameAndPublicAccountWithRestore(accountRestoreRequest->callbackData.slowCommandID, 0);
		accountRestoreRequest->eStatus = 1;
	}
	else
	{
		SlowRemoteCommandReturn_dbIDFromNameAndPublicAccountWithRestore(accountRestoreRequest->callbackData.slowCommandID, 0);
		accountRestoreRequest->eStatus = -1;
	}

	accountRestoreRequest->done = true;
	DestroyAccountRestoreRequest(accountRestoreRequest);
}

AUTO_COMMAND_REMOTE_SLOW(U32);
void dbIDFromNameAndPublicAccountWithRestore(SlowRemoteCommandID iCmdID, U32 containerType, const char *name, const char *accountName, ContainerID iVirtualShardID)
{
	U32 containerID = dbIDFromNameAndPublicAccount(containerType, name, accountName, iVirtualShardID);
	AccountRestoreRequest* accountRestoreRequest;
	if(containerID)
	{
		SlowRemoteCommandReturn_dbIDFromNameAndPublicAccountWithRestore(iCmdID, containerID);
		return;
	}

	accountRestoreRequest = CreateAccountRestoreRequest(accountName);
	accountRestoreRequest->callbackData.slowCommandID = iCmdID;
	accountRestoreRequest->callbackData.cbFunc = dbIDFromNameAndPublicAccountWithRestore_SlowReturn_CB;
	accountRestoreRequest->characterName = strdup(name);

	// Get the accountid from the AccountProxyServer
	RemoteCommand_aslAPCmdGetAccountIDFromDisplayName(
		objCreateManagedReturnVal(dbIDFromNameAndPublicAccountWithRestore_CB, accountRestoreRequest),
		GLOBALTYPE_ACCOUNTPROXYSERVER, 0, accountName);
}


AUTO_COMMAND_REMOTE;
U32 dbIDFromNameAndPublicAccount(U32 containerType, const char *name, const char *accountName, ContainerID iVirtualShardID)
{
	ContainerID resultID = 0;
	ObjectIndexKey key = {0};
	ObjectHeaderOrData *data;
	Container *con = NULL;

	//Create a template
	objIndexInitKey_Template(gAccountDisplayName_idx, &key, accountName, name); //".pPlayer.publicAccountName", ".pSaved.savedName"

	//look for an exact match
	objIndexObtainReadLock(gAccountDisplayName_idx);
	if (objIndexGet(gAccountDisplayName_idx, &key, 0, &data))
	{
		resultID = ((ObjectIndexHeader*)data)->containerId;
	}
	objIndexReleaseReadLock(gAccountDisplayName_idx);
	objIndexDeinitKey_Template(gAccountDisplayName_idx, &key);

	if (resultID)
		con = objGetContainerEx(GLOBALTYPE_ENTITYPLAYER, resultID, false, false, true);
	if (!con || con->header->virtualShardId != iVirtualShardID)
		resultID = 0;
	if (con)
		objUnlockContainer(&con);

	return resultID;
}

void OVERRIDE_LATELINK_dbOnlineCharacterIDFromAccountID(GlobalType eSourceType, ContainerID uSourceID, U32 requestID, U32 accountID)
{
	ContainerID *eaIDs = NULL;
	ContainerID resultID = 0;

	verbose_printf("Returning character list for accountID %d\n", accountID);

	eaIDs = GetContainerIDsFromAccountID(accountID);
	resultID = GetOnlineCharacterIDFromList(eaIDs, -1);
	ea32Destroy(&eaIDs);

	RemoteCommand_ReturnOnlineCharacterIDForAccountID(eSourceType, uSourceID, requestID, resultID);
}

void dbIDFromPlayerReferenceWithRestore_CB(TransactionReturnVal *returnVal, void *cbData)
{
	U32 containerID = 0;
	SlowRemoteCommandID iCmdID =  (SlowRemoteCommandID)(S64)cbData;
	if (RemoteCommandCheck_dbIDFromNameAndPublicAccountWithRestore(returnVal, &containerID) == TRANSACTION_OUTCOME_SUCCESS)
	{
 		SlowRemoteCommandReturn_dbIDFromPlayerReferenceWithRestore(iCmdID, containerID);
	}
	else
	{
 		SlowRemoteCommandReturn_dbIDFromPlayerReferenceWithRestore(iCmdID, 0);
	}
}

typedef struct DevModeFindEntityPlayerByNameInfo
{
	ParseTable *pti;
	ContainerID containerID;
	ContainerID iVirtualShardID;
	const char *targetName;
} DevModeFindEntityPlayerByNameInfo;

static bool DevModeHaveNotFoundEntityPlayerByName(DevModeFindEntityPlayerByNameInfo *info)
{
	return info->containerID == 0;
}

static void DevModeFindEntityPlayerByName(Container *con, DevModeFindEntityPlayerByNameInfo *info)
{
	char name[MAX_NAME_LEN];
	if(!con || con->header->virtualShardId != info->iVirtualShardID || !objPathGetString(".pSaved.savedName",info->pti,con->containerData,SAFESTR(name)))
        return;
    if(0 != stricmp(name,info->targetName))
        return;
	info->containerID = con->containerID;
}

static U32 dbIDFromPlayerReference_internal(SlowRemoteCommandID iCmdID, U32 containerType, const char *pcRef, ContainerID iVirtualShardID, bool *slowcall, char **ppcNameOut, char **ppcAccountOut)
{
	char *pcAccountStart = strchr(pcRef, '@');
	
	if (!pcAccountStart) {
		// We don't allow resolving a character name into an ID
		// because: 1) It's expensive, 2) It's very likely to 
		// be ambiguous, 3) If it is ambiguous, there's no good
		// choice that can be made at this point.
		//
		// It's better if the client resolves character names into 
		// full name & account and/or ent ID's because it has more
		// context about whom the player might be looking for.
		// (i.e. teammate, friend, guild member, chat correspondent,
		// local entity)
        if(isDevelopmentMode())
        {
			DevModeFindEntityPlayerByNameInfo info = {0};
            ContainerStore *store = objFindContainerStoreFromType(GLOBALTYPE_ENTITYPLAYER);
            
            info.pti = SAFE_MEMBER2(store,containerSchema,classParse);

			if(!info.pti)
			{
				return 0;
			}

			info.targetName = pcRef;
			info.iVirtualShardID = iVirtualShardID;
			info.containerID = 0;

			ForEachContainerOfTypeEx(GLOBALTYPE_ENTITYPLAYER, DevModeFindEntityPlayerByName, DevModeHaveNotFoundEntityPlayerByName, &info, true, false);

			return info.containerID;
        }
		return 0;
	} else if (pcAccountStart == pcRef) {
		// Just @account
		ContainerID *eaIDs = NULL;
		ContainerID resultID = 0;

		eaIDs = GetContainerIDsFromDisplayName(pcAccountStart + 1);
		resultID = GetOnlineCharacterIDFromList(eaIDs, iVirtualShardID);
		ea32Destroy(&eaIDs);

		return resultID;
	} else {
		// Full name@account syntax
		static char *pcName = NULL;
		static char *pcAccount = NULL;
		char **ppcName;
		char **ppcAccount;
		if(ppcNameOut && ppcAccountOut)
		{
			ppcName = ppcNameOut;
			ppcAccount = ppcAccountOut;
		}
		else
		{
			ppcName = &pcName;
			ppcAccount = &pcAccount;
		}

		estrClear(ppcName);
		estrInsert(ppcName, 0, pcRef, pcAccountStart-pcRef);
		estrCopy2(ppcAccount, pcAccountStart+1);
		if(slowcall)
			*slowcall = true;
		return dbIDFromNameAndPublicAccount(containerType, *ppcName, *ppcAccount, iVirtualShardID);
	}
}

AUTO_COMMAND_REMOTE_SLOW(U32);
void dbIDFromPlayerReferenceWithRestore(SlowRemoteCommandID iCmdID, U32 containerType, const char *pcRef, ContainerID iVirtualShardID)
{
	bool slowcall;
	static char *pcName = NULL;
	static char *pcAccount = NULL;
	U32 containerID = dbIDFromPlayerReference_internal(iCmdID, containerType, pcRef, iVirtualShardID, &slowcall, &pcName, &pcAccount);

	if(!containerID && slowcall)
	{
		RemoteCommand_dbIDFromNameAndPublicAccountWithRestore(
			objCreateManagedReturnVal(dbIDFromPlayerReferenceWithRestore_CB, (void*)(S64)iCmdID),
			GLOBALTYPE_OBJECTDB, 0, containerType, pcName, pcAccount, iVirtualShardID);
	}
	else
	{
 		SlowRemoteCommandReturn_dbIDFromPlayerReferenceWithRestore(iCmdID, containerID);
	}
}

AUTO_COMMAND_REMOTE;
U32 dbIDFromPlayerReference(U32 containerType, const char *pcRef, ContainerID iVirtualShardID)
{
	return dbIDFromPlayerReference_internal(0, containerType, pcRef, iVirtualShardID, NULL, NULL, NULL);
}

AUTO_COMMAND_REMOTE;
U32 dbIDFromPetReference(U32 containerType, const char *pcRef, ContainerID iVirtualShardID)
{
	char *pcPlayerRefStart = strchr(pcRef, ':');
	if (pcPlayerRefStart) {
		// Syntax: PetName:PlayerName@account
		ContainerID playerID = 0;
		ContainerID *eaPetIDs = NULL;
		static char *pcPetName = NULL;
		estrClear(&pcPetName);
		estrInsert(&pcPetName, 0, pcRef, pcPlayerRefStart-pcRef);

		playerID = dbIDFromPlayerReference(GLOBALTYPE_ENTITYPLAYER, pcPlayerRefStart+1, iVirtualShardID);
		eaPetIDs = GetPetIDsFromCharacterID(playerID);

		EARRAY_INT_CONST_FOREACH_BEGIN(eaPetIDs, i, n);
		{
			Container *con = objGetContainerEx(GLOBALTYPE_ENTITYSAVEDPET, eaPetIDs[i], true, false, true);

			if (con)
			{
				char petName[MAX_NAME_LEN] = {0};
				bool found = objPathGetString(".pSaved.savedName", con->containerSchema->classParse, con->containerData, SAFESTR(petName));

				objUnlockContainer(&con);

				if (found && !stricmp(pcPetName, petName))
				{
					ContainerID petID = eaPetIDs[i];
					ea32Destroy(&eaPetIDs);
					return petID;
				}
			}
		}
		EARRAY_FOREACH_END;

		ea32Destroy(&eaPetIDs);
	}

	return 0;
}

AUTO_COMMAND_REMOTE;
char* dbPetListFromPlayerReference(U32 containerType, const char *pcRef, ContainerID iVirtualShardID)
{
	static char *estrResult = NULL;
	estrClear(&estrResult);

	if (pcRef) {
		// Syntax: PlayerName@account
		ContainerID playerID = dbIDFromPlayerReference(GLOBALTYPE_ENTITYPLAYER, pcRef, iVirtualShardID);
		ContainerID *eaPetIDs = GetPetIDsFromCharacterID(playerID);

		EARRAY_INT_CONST_FOREACH_BEGIN(eaPetIDs, i, n);
		{
			Container *con = objGetContainerEx(GLOBALTYPE_ENTITYSAVEDPET, eaPetIDs[i], true, false, true);

			if (con)
			{
				char savedName[MAX_NAME_LEN] = {0};
				bool found = objPathGetString(".pSaved.savedName", con->containerSchema->classParse, con->containerData, SAFESTR(savedName));

				objUnlockContainer(&con);

				if (found)
				{
					estrConcatf(&estrResult, "%s - %u\n", savedName, eaPetIDs[i]);
				}
			}
		}
		EARRAY_FOREACH_END;
	}

	return estrResult;
}

//
// if you change the return type or arguments to this function, you will also
//  have to change HTTP_dbContainerLocationFromPlayerRef() below.
//
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
ContainerLocation* dbContainerLocationFromPlayerRef(U32 containerType, const char *pcRef, ContainerID iVirtualShardID)
{
	ContainerID id;
	ContainerLocation *loc = StructCreate(parse_ContainerLocation);
	loc->containerID = 0;
	loc->containerType = GLOBALTYPE_NONE;
	loc->ownerID = 0;
	loc->ownerType = GLOBALTYPE_NONE;

	if (strStartsWith(pcRef, "Entity") && strEndsWith(pcRef, "]") && !strchr(pcRef, '@'))
	{
		GlobalType type;
		if (DecodeContainerTypeAndIDFromString(pcRef, &type, &id))
		{
			if (type != containerType) id = 0;
		}
	}
	else
	{
		id = dbIDFromPlayerReference(containerType, pcRef, iVirtualShardID);
	}

	if (id)
	{
		Container *con;
		if (con = objGetContainerEx(containerType, id, false, false, true))
		{
			loc->containerID = id;
			loc->containerType = containerType;
			loc->ownerID = con->meta.containerOwnerID;
			loc->ownerType = con->meta.containerOwnerType;
			objUnlockContainer(&con);
		}
	}
	return loc;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(SERVERLIB);
ContainerLocation* HTTP_dbContainerLocationFromPlayerRef(U32 containerType, const char *pcRef, ContainerID iVirtualShardID)
{
	return dbContainerLocationFromPlayerRef(containerType, pcRef, iVirtualShardID);
}

AUTO_COMMAND_REMOTE;
char *dbContainerOwner(U32 containerType, U32 conid)
{
	static char buf[1024];
	Container *con = objGetContainerEx(containerType, conid, false, false, true);

	if (!con)
	{
		return NULL;
	}

	sprintf(buf, "%s[%u]", GlobalTypeToName(con->meta.containerOwnerType),(con->meta.containerOwnerID));
	objUnlockContainer(&con);

	return buf;
}

AUTO_COMMAND;
char *DBFindContainerOwner(const char *type, U32 id)
{
	GlobalType eType;

	if(!type || !type[0] || !id)
		return NULL;

	eType = NameToGlobalType(type);
	if (!eType)
	{
		Errorf("Could not find container type: %s", type);
		return NULL;
	}
	return dbContainerOwner(eType, id);
}

AUTO_COMMAND_REMOTE;
char *dbContainerOwnerFromPlayerRef(U32 containerType, const char *pcRef, ContainerID iVirtualShardID)
{
	static char buf[1024];
	ContainerID conid;
	Container *con;

	conid = dbIDFromPlayerReference(containerType, pcRef, iVirtualShardID);
	if (!conid)
	{
		return NULL;
	}

	con = objGetContainerEx(containerType, conid, false, false, true);

	if (!con)
	{
		return NULL;
	}

	sprintf(buf, "%s[%u]", GlobalTypeToName(con->meta.containerOwnerType),(con->meta.containerOwnerID));
	objUnlockContainer(&con);

	return buf;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(CSR);
char *dbFindPlayer(const char *pcRef)
{
	/*VSHARD_FIXME*/
	char *result = dbContainerOwnerFromPlayerRef(GLOBALTYPE_ENTITYPLAYER, pcRef, 0);
	static char buf[1024];
	if (result)
		return result;
	else
	{
		sprintf(buf,"Could not find player:\"%s\"", pcRef);
		return buf;
	}
}

AUTO_COMMAND_REMOTE;
U32 dbAccountIDFromPlayerReference(U32 containerType, const char *pcRef, ContainerID iVirtualShardID)
{
	U32 iContainerID = dbIDFromPlayerReference(containerType, pcRef, iVirtualShardID);
	if( iContainerID )
	{
		return dbAccountIDFromID(containerType, iContainerID );
	}
	return 0;
}

//Return the matching EntityPlayer containerID by character name and account ID; return 0 if not found.
AUTO_COMMAND_REMOTE;
U32 dbIDFromNameAndAccountID(U32 containerType, const char *name, int accountID, ContainerID iVirtualShardID)
{
	ContainerID resultID = 0;
	ContainerID *eaIDs = NULL;

	eaIDs = GetContainerIDsFromAccountID(accountID);
	verbose_printf("Returning character list for accountID %d\n", accountID);

	EARRAY_INT_CONST_FOREACH_BEGIN(eaIDs, i, n);
	{
		Container *con = objGetContainerEx(GLOBALTYPE_ENTITYPLAYER, eaIDs[i], false, false, true);

		if (!con) continue;

		if (con->header->virtualShardId != iVirtualShardID)
		{
			objUnlockContainer(&con);
			continue;
		}

		if (stricmp(con->header->savedName, name))
		{
			objUnlockContainer(&con);
			continue;
		}

		objUnlockContainer(&con);
		resultID = eaIDs[i];
		break;
	}
	EARRAY_FOREACH_END;

	ea32Destroy(&eaIDs);
	return resultID;
}


typedef struct RenameData
{
	char newName[MAX_NAME_LEN];
	GlobalType type;
	ContainerID id;	
} RenameData;

static void RenamePlayer_CB(TransactionReturnVal *returnVal, RenameData *cbData)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		dbContainerLog(LOG_LOGIN, cbData->type, cbData->id, true, "CharacterRename", "Type %s, ID %u, Result Success, NewName \"%s\"", GlobalTypeToName(cbData->type), cbData->id, cbData->newName ? cbData->newName : "");		
	}
	else
	{
		dbContainerLog(LOG_LOGIN, cbData->type, cbData->id, true, "CharacterRename", "Type %s, ID %u, Result Failure, NewName \"%s\"", GlobalTypeToName(cbData->type), cbData->id, cbData->newName ? cbData->newName : "");
	}
}

AUTO_COMMAND_REMOTE;
U32 dbRenamePlayer(ContainerID id, const char *newName)
{
	RenameData *cbData;
	Container *container = objGetContainerEx(GLOBALTYPE_ENTITYPLAYER, id, false, false, true);
	U32 accountID = 0;
	U32 virtualShardID = 0;

	if (!container)
	{
		return 0;
	}

	accountID = container->header->accountId;
	virtualShardID = container->header->virtualShardId;
	objUnlockContainer(&container);

	if (dbIDFromNameAndAccountID(GLOBALTYPE_ENTITYPLAYER, newName, accountID, virtualShardID))
	{
		return 0;
	}

	cbData = calloc(sizeof(RenameData),1);
	cbData->type = GLOBALTYPE_ENTITYPLAYER;
	cbData->id = id;
	strcpy(cbData->newName, newName);

	objRequestTransactionSimplef(objCreateManagedReturnVal(RenamePlayer_CB, cbData),
		GLOBALTYPE_ENTITYPLAYER, id, "dbRenamePlayer", "set pSaved.savedName = \"%s\"", newName);
	
	return 1;
}

typedef struct AccountInfoData
{
	char accountname[MAX_NAME_LEN];
	U32 accountid;
	GlobalType type;
	ContainerID id;	
} AccountInfoData;

static void SetAccountInfo_CB(TransactionReturnVal *returnVal, AccountInfoData *cbData)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		dbContainerLog(LOG_LOGIN, cbData->type, cbData->id, true, "SetAccountInfo", "Succeeded, account name = %s[%d]", cbData->accountname, cbData->accountid);		
	}
	else
	{
		dbContainerLog(LOG_LOGIN, cbData->type, cbData->id, true, "SetAccountInfo", "Failed, attempted name = %s[%d]", cbData->accountname, cbData->accountid);
	}
}

static ContainerID findNewIdFromOldID(ContainerRefMapping **mappings, GlobalType type, ContainerID oldID)
{
	EARRAY_FOREACH_BEGIN(mappings, i);
	{
		if (mappings[i]->type == type && mappings[i]->oldID == oldID) 
			return mappings[i]->newID;
	}
	EARRAY_FOREACH_END;
	return 0;
}

typedef struct FixupParseTableContainerIDData
{
	ContainerID oldPlayerID;
	ContainerID newPlayerID;
	GlobalType containerType;
	ContainerID oldContainerID;
	ContainerID newContainerID;

	ContainerRefMapping **mappings;
} FixupParseTableContainerIDData;

bool FixupParseTableContainerID(ParseTable pti[], void *pStruct, int column, int index, void *pCBData)
{
	char *ptypestring;
	bool found = false;
	FixupParseTableContainerIDData *fixupData = (FixupParseTableContainerIDData *)pCBData;
	ContainerID id = 0;
	GlobalType eGlobalType = GLOBALTYPE_NONE;
	char *idObjPath = NULL;
	estrStackCreate(&idObjPath);
	PERFINFO_AUTO_START_FUNC();
	if(GetStringFromTPIFormatString(pti + column, "DEPENDENT_CONTAINER_TYPE", &ptypestring) 
		|| GetStringFromTPIFormatString(pti + column, "EXPORT_CONTAINER_TYPE", &ptypestring)
		|| GetStringFromTPIFormatString(pti + column, "FIXUP_CONTAINER_TYPE", &ptypestring))	
	{
		found = GetContainerTypeAndIDFromTypeString(pti, pStruct, column, index, ptypestring, &idObjPath, &id, &eGlobalType);
	}
	else if(GetBoolFromTPIFormatString(pti + column, "DEPENDENT_CONTAINER_REF") 
		|| GetBoolFromTPIFormatString(pti + column, "EXPORT_CONTAINER_REF")
		|| GetBoolFromTPIFormatString(pti + column, "FIXUP_CONTAINER_REF"))
	{
		found = GetContainerTypeAndIDFromContainerRef(pti, pStruct, column, index, &idObjPath, &id, &eGlobalType);
	}

	if(found)
	{
		char *buf = NULL;
		ContainerID newID;
		estrStackCreate(&buf);
		if(!(newID = findNewIdFromOldID(fixupData->mappings, eGlobalType, id)))
		{
			ErrorOrAlert("CONTAINER_IMPORT_FIXUP_ERROR", 
				"%s[%u] was not included in import. EntityPlayer: oldID %u, newID %u; Reference found in %s: oldID %u, newID %u; Field %s.", 
				GlobalTypeToName(eGlobalType), id,
				fixupData->oldPlayerID,
				fixupData->newPlayerID,
				GlobalTypeToName(fixupData->containerType),
				fixupData->oldContainerID,
				fixupData->newContainerID,
				NULL_TO_EMPTY((pti + column)->name));
		}
		estrPrintf(&buf, "%u", newID);
		objPathSetString(idObjPath, pti, pStruct, buf);
		estrDestroy(&buf);
	}

	estrDestroy(&idObjPath);

	PERFINFO_AUTO_STOP();
	return false;
}

static void dbEntityFixUpDependentContainerRefs(ParseTable *enttpi, void *ent, FixupParseTableContainerIDData *fixupData)
{
	ParseTable *tpi = NULL;

	PERFINFO_AUTO_START_FUNC();
	ParserTraverseParseTable(enttpi, ent, TOK_PERSIST, TOK_UNOWNED | TOK_REDUNDANTNAME, FixupParseTableContainerID, NULL, NULL, fixupData);
	PERFINFO_AUTO_STOP();
}

extern ParseTable parse_SerializedContainers[];
#define TYPE_parse_SerializedContainers SerializedContainers
extern ParseTable parse_SerializedEntity[];
#define TYPE_parse_SerializedEntity SerializedEntity

//Return an EntityExport struct.
SerializedContainers *dbParseCharacterEntity(char *fileData)
{
	SerializedContainers *sc = StructCreate(parse_SerializedContainers);
	if (!ParserReadText(fileData, parse_SerializedContainers, sc, PARSER_IGNORE_ALL_UNKNOWN))
	{
		ErrorDeferredf("Could not read exported entity");
		StructDestroy(parse_SerializedContainers, sc);
		return NULL;
	}
	else
	{
		return sc;
	}
}

void dbDestroyEntityExport(SerializedContainers *ee)
{
	StructDestroy(parse_SerializedContainers, ee);
}

char *FindNonDuplicateName(char *name, U32 accountID)
{
	ContainerID *eaIDs = NULL;
	const char **names = NULL;
	bool foundExact = false;
	int maxValue = 0;
	char *returnString = NULL;
	AccountStub *stub = NULL;
	Container *accountStubContainer = NULL;

	// Grab the headers and get the names directly, instead of stupidly populating character choices just for this
	eaIDs = GetContainerIDsFromAccountID(accountID);

	EARRAY_INT_CONST_FOREACH_BEGIN(eaIDs, i, n);
	{
		Container *con = objGetContainerEx(GLOBALTYPE_ENTITYPLAYER, eaIDs[i], false, false, true);

		if (con)
		{
			eaPush(&names, strdup(con->header->savedName));
			objUnlockContainer(&con);
		}
	}
	EARRAY_FOREACH_END;

	ea32Destroy(&eaIDs);

	// Get the offline character names too!
	accountStubContainer = objGetContainerEx(GLOBALTYPE_ACCOUNTSTUB, accountID, true, false, true);

	if(accountStubContainer)
		stub = accountStubContainer->containerData;

	if(stub)
	{
		EARRAY_FOREACH_BEGIN(stub->eaOfflineCharacters, i);
		{
			if(stub->eaOfflineCharacters[i] && !stub->eaOfflineCharacters[i]->restored)
			{
				eaPush(&names, strdup(stub->eaOfflineCharacters[i]->savedName));
			}
		}
		EARRAY_FOREACH_END;
	}

	if(accountStubContainer)
		objUnlockContainer(&accountStubContainer);

	EARRAY_FOREACH_BEGIN(names, i);
	{
		if(strStartsWith(names[i], name))
		{
			size_t length = strlen(name);
			size_t old_length = strlen(names[i]);
			int value = 0;

			if(length == old_length)
			{
				foundExact = true;
			}
			else if(names[i][length] == ' ')
			{
				char tmpNameStr[MAX_NAME_LEN];
				tmpNameStr[0] = 0;
				value = atoi(names[i] + length + 1);
				sprintf(tmpNameStr, "%s %d", name, value);
				if(strcmp(tmpNameStr, names[i]) == 0)
				{
					maxValue = MAX(maxValue, value);
				}
			}
		}
	}
	EARRAY_FOREACH_END;

	if(foundExact)
	{
		estrStackCreate(&returnString);
		estrPrintf(&returnString, "%s %d", name, maxValue + 1);
	}

	return returnString;
}

bool EntityPlayerSpecificFixup(ContainerSchema *schema, void *playerEnt, U32 *accountid, U32 setAccountid, ContainerRefMapping ***refs)
{
	bool accountidwarning = false;
	ContainerRefMapping *mapping = NULL;
	U32 oldaccountid = 0;
	//preflight character collision check
	char *namestr = NULL;
	estrStackCreate(&namestr);
	objPathGetEString(".Psaved.Savedname", schema->classParse, playerEnt, &namestr);

	objPathSetInt(".Psaved.Timelastimported", schema->classParse, playerEnt, timeSecondsSince2000(), true);

	objPathGetInt(".Pplayer.Accountid", schema->classParse, playerEnt, &oldaccountid);

	if(setAccountid)
	{
		*accountid = setAccountid;
		objPathSetInt(".Pplayer.Accountid", schema->classParse, playerEnt, setAccountid, true);
		objPathSetInt(".Pplayer.Pplayeraccountdata.Iaccountid", schema->classParse, playerEnt, setAccountid, true);
	}
	else if (oldaccountid)
		*accountid = oldaccountid;

	if (*accountid)
	{
		char *newNameStr = NULL;

		if(newNameStr = FindNonDuplicateName(namestr, *accountid))
		{
			objPathSetString(".Psaved.Savedname", schema->classParse, playerEnt, newNameStr);
		}
		estrDestroy(&newNameStr);

		mapping = StructCreate(parse_ContainerRefMapping);
		mapping->type = GLOBALTYPE_GAMEACCOUNTDATA;
		mapping->oldID = oldaccountid;
		mapping->newID = *accountid;
		eaPush(refs, mapping);
	}
	else
	{
		accountidwarning = true;
	}
	estrDestroy(&namestr);
	return accountidwarning;
}

void GameAccountDataSpecificFixup(U32 accountid, ContainerSchema *accountSchema, Container **sourceCon, void *entData)
{
	Container *con = objGetContainerEx(GLOBALTYPE_GAMEACCOUNTDATA, accountid, true, false, true);
	
	if(con)
		*sourceCon = con;

	objPathSetInt(".Iaccountid", accountSchema->classParse, entData, accountid, true);
}

typedef struct ImportFixupStruct {
	GlobalType containerType; // Global type of container, here for cache speed
	Container *sourceCon; // If we're diffing between an old container and a new one, this contains the LOCKED source version
	ContainerID oldContainerID; // Keep this here for more informative alerts
	ContainerID newContainerID; // The cached container ID for this container
	void *containerData; // The actual data of this object
	ParseTable *classParse; // The parse table of the object
} ImportFixupStruct;

static void DestroyFixupStructInternalData(void *data)
{
	ImportFixupStruct *fixup = (ImportFixupStruct*) data;
	if(fixup->classParse && fixup->containerData)
	{
		StructDestroyVoid(fixup->classParse, fixup->containerData);
	}
}

static ContainerID PrepareEntityForAddTransaction(GlobalType eGlobalType, ContainerID forcedContainerID, ParseTable *pti, void *entity, U32 iKeyColumn)
{
	ContainerStore *store;
	ContainerID newContainerID = forcedContainerID;

	store = objFindContainerStoreFromType(eGlobalType);

	if(!store)
		return 0;

	if(!newContainerID)
		newContainerID = objReserveNewContainerID(store);

	if(!newContainerID)
		return 0;

	TokenStoreSetInt(pti, iKeyColumn, entity, 0, newContainerID, NULL, NULL);

	return newContainerID;
}

static void AddToFixup(GlobalType eGlobalType, Container *sourceCon, ContainerID oldContainerID, ContainerID newContainerID, ParseTable *pti, void *entity, ContainerRefMapping ***refs, ImportFixupStruct ***pppFixupList)
{
	ImportFixupStruct *con = calloc(sizeof(ImportFixupStruct), 1);
	ContainerRefMapping *mapping;

	if(entity)
	{
		con->classParse = pti;
		con->containerData = entity;
		con->sourceCon = sourceCon;
		con->oldContainerID = oldContainerID;
		con->newContainerID = newContainerID;
		con->containerType = eGlobalType;
		eaPush(pppFixupList, con);
	}

	mapping = StructCreate(parse_ContainerRefMapping);
	mapping->type = eGlobalType;
	mapping->oldID = oldContainerID;
	mapping->newID = newContainerID;
	eaPush(refs, mapping);
}

static bool PrepareImportContainer(ContainerID oldContainerID, ContainerID forcedContainerID, 
							Container *sourceCon, void *entity, U32 iKeyColumn, ContainerSchema *schema, GlobalType eGlobalType, ContainerID *oldPlayerID, ContainerID *newPlayerID, bool accountidwarning, ContainerRefMapping ***refs, ImportFixupStruct ***pppFixupList)
{
	ContainerID newContainerID;
	if(!(newContainerID = PrepareEntityForAddTransaction(eGlobalType, forcedContainerID, schema->classParse, entity, iKeyColumn)))
		return false;

	AddToFixup(eGlobalType, sourceCon, oldContainerID, newContainerID, schema->classParse, entity, refs, pppFixupList);

	if(eGlobalType == GLOBALTYPE_ENTITYPLAYER)
	{
		*newPlayerID = newContainerID;
		*oldPlayerID = oldContainerID;
		if (accountidwarning)
		{
			ErrorOrAlert("CONTAINER_IMPORT", "Someone imported a character without an accountID. Old id %u, new id %u.", *oldPlayerID, *newPlayerID);
		}
	}
	return true;
}

bool AddContainer(void *entity, ContainerSchema *schema, GlobalType eGlobalType, ContainerID *oldPlayerID, ContainerID *newPlayerID, ContainerID *accountid, ContainerID setAccountid, bool overwriteAccountData, ContainerRefMapping ***refs, ImportFixupStruct ***pppFixupList)
{
	bool accountidwarning = false;
	int iKeyColumn = ParserGetTableKeyColumn(schema->classParse);
	U32 id = (U32)TokenStoreGetInt(schema->classParse, iKeyColumn, entity, 0, NULL);
	ContainerID forcedContainerID = 0;
	Container *sourceCon = NULL;
	
	if(eGlobalType == GLOBALTYPE_ENTITYPLAYER)
	{
		accountidwarning = EntityPlayerSpecificFixup(schema, entity, accountid, setAccountid, refs);
	}
	else if(eGlobalType == GLOBALTYPE_GAMEACCOUNTDATA)
	{
		if(!overwriteAccountData)
			return true;

		GameAccountDataSpecificFixup(*accountid, schema, &sourceCon, entity);
		forcedContainerID = *accountid;
	}

	return PrepareImportContainer(id, forcedContainerID, sourceCon, entity, iKeyColumn, schema, eGlobalType, oldPlayerID, newPlayerID, accountidwarning, refs, pppFixupList);
}

// returns false iff it tries to add an EntityPlayer and fails
bool ParseAndAddContainer(SerializedEntity *entData, ContainerID *oldPlayerID, ContainerID *newPlayerID, U32 *accountid, U32 setAccountid, bool overwriteAccountData, ContainerRefMapping ***refs, ImportFixupStruct ***pppFixupList, GlobalType targettype)
{
	bool returnval = true;
	GlobalType eGlobalType;

	eGlobalType = NameToGlobalType(entData->entityType);
	if(eGlobalType && (!targettype || (targettype == eGlobalType)))
	{
		ContainerSchema *schema = objFindContainerSchema(eGlobalType);
		void *entity = StructCreateVoid(schema->classParse);
		if(ParserReadText(entData->entityData, schema->classParse, entity, 0))
		{
			returnval = AddContainer(entity, schema, eGlobalType, oldPlayerID, newPlayerID, accountid, setAccountid, overwriteAccountData, refs, pppFixupList);
		}
	}

	return returnval;
}

static void ImportEntitiesByType(void **entArray, GlobalType eGlobalType, ContainerID *oldPlayerID, ContainerID *newPlayerID, ContainerID *accountid, ContainerID setAccountid, bool overwriteAccountData, ContainerRefMapping ***refs, ImportFixupStruct ***pppFixupList)
{
	ContainerSchema *schema = objFindContainerSchema(eGlobalType);

	EARRAY_FOREACH_BEGIN(entArray,i);
	{
		void *entData = entArray[i];
		AddContainer(entData, schema, eGlobalType, oldPlayerID, newPlayerID, accountid, setAccountid, overwriteAccountData, refs, pppFixupList);
	}
	EARRAY_FOREACH_END;
}

U32 dbLoadEntity(U32 type, SerializedContainers *ee, TransactionRequest *request, U32 setAccountid, const char *setPrivateAccountName, const char *setPublicAccountName, bool overwriteAccountData)
{
	ContainerRefMapping **refs = NULL;
	ContainerID newPlayerID = 0;
	ContainerID oldPlayerID = 0;
	U32 accountid = 0;
	bool accountIDWarning = false;
	ImportFixupStruct **ppFixupList = NULL;
	FixupParseTableContainerIDData fixupData = {0};

	PERFINFO_AUTO_START_FUNC();

	if (!ee)
		return 0;

	// Parse container data and determine container mappings
	EARRAY_FOREACH_BEGIN(ee->containers, i);
	{
		SerializedEntity *entData = ee->containers[i];

		if(i == 0)
		{
			// the first entry must be a player
			if(!ParseAndAddContainer(entData, &oldPlayerID, &newPlayerID, &accountid, setAccountid, overwriteAccountData, &refs, &ppFixupList, GLOBALTYPE_ENTITYPLAYER))
				goto cleanup;
		}
		else
		{
			ParseAndAddContainer(entData, &oldPlayerID, &newPlayerID, &accountid, setAccountid, overwriteAccountData, &refs, &ppFixupList, 0);
		}
	}
	EARRAY_FOREACH_END;

	// Fixup account names if required
	if (setPrivateAccountName && *setPrivateAccountName)
	{
		objPathSetString(".Pplayer.Privateaccountname", ppFixupList[0]->classParse, ppFixupList[0]->containerData, setPrivateAccountName);
	}

	if (setPublicAccountName && *setPublicAccountName)
	{
		objPathSetString(".Pplayer.Publicaccountname", ppFixupList[0]->classParse, ppFixupList[0]->containerData, setPublicAccountName);
	}

	//Fix up Dependent Container data on imported containers
	fixupData.oldPlayerID = oldPlayerID;
	fixupData.newPlayerID = newPlayerID;
	fixupData.mappings = refs;

	EARRAY_FOREACH_BEGIN(ppFixupList,i);
	{
		fixupData.containerType = ppFixupList[i]->containerType;
		fixupData.oldContainerID = ppFixupList[i]->oldContainerID;
		fixupData.newContainerID = ppFixupList[i]->newContainerID;
		servLog(LOG_CONTAINER, "ImportingContainer", "ContainerType %s ContainerID %u OldContainerID %u OwnerID %u OldOwnerID %u", GlobalTypeToName(ppFixupList[i]->containerType), ppFixupList[i]->newContainerID, ppFixupList[i]->oldContainerID, newPlayerID, oldPlayerID);
		dbEntityFixUpDependentContainerRefs(ppFixupList[i]->classParse, ppFixupList[i]->containerData, &fixupData);
	}
	EARRAY_FOREACH_END;

	// All fixup is done, now append the create steps to the transaction
	EARRAY_FOREACH_BEGIN(ppFixupList, i);
	{
		char *diffString = NULL;
		estrStackCreateSize(&diffString, 4096);

		if (ppFixupList[i]->sourceCon)
		{
			StructWriteTextDiff(&diffString, ppFixupList[i]->classParse, ppFixupList[i]->sourceCon->containerData, ppFixupList[i]->containerData, NULL, TOK_PERSIST, 0, 0);
			objUnlockContainer(&ppFixupList[i]->sourceCon);
		}
		else
		{
			StructTextDiffWithNull_Verify(&diffString, ppFixupList[i]->classParse, ppFixupList[i]->containerData, NULL, 0, TOK_PERSIST, 0, 0);
		}

		objAddToTransactionRequestf(request, objServerType(), objServerID(), NULL,
			"CreateSpecificContainerUsingData containerIDVar %s %d \"%s\"",
			GlobalTypeToName(ppFixupList[i]->containerType), ppFixupList[i]->newContainerID, diffString);

		estrDestroy(&diffString);
	}
	EARRAY_FOREACH_END;

cleanup:
	eaDestroyEx(&ppFixupList, DestroyFixupStructInternalData);
	eaDestroyStruct(&refs, parse_ContainerRefMapping);
	PERFINFO_AUTO_STOP();
	return newPlayerID;
}

U32 dbLoadEntityFromFile(U32 type, const char *fileName, TransactionRequest *request, U32 setAccountid, const char *setPrivateAccountName, const char *setPublicAccountName, bool overwriteAccountData)
{
	char fname[MAX_PATH];
	char *fileData = NULL;
	SerializedContainers *ee = NULL;
	ContainerID playerID = 0;
	U32 len;
	
	bool success = true;
	static char result[1024];


	if ( request==NULL )
	{
		ErrorDeferredf( "Import character: Failed to create transaction request" );
		return 0;
	}
	PERFINFO_AUTO_START_FUNC();

	strcpy(fname, fileName);
	if(!fileExists(fname))
	{
		sprintf(result, "File does not exist: %s", fname);
		PERFINFO_AUTO_STOP();
		return 0;
	}

	fileData = fileAlloc(fname,&len);
	if (!fileData)
	{
		sprintf(result, "Error reading file: %s", fname);
		PERFINFO_AUTO_STOP();
		return 0;
	}
	ee = dbParseCharacterEntity(fileData);
	if (ee)
	{
		playerID = dbLoadEntity(type, ee, request, setAccountid, setPrivateAccountName, setPublicAccountName, overwriteAccountData);
		dbDestroyEntityExport(ee);
	}
	
	free(fileData);

	PERFINFO_AUTO_STOP();

	if (!playerID)
	{
		sprintf(result, "Could not add the entity to the database.");
	}

	return playerID;
}

static U32 ImportEntityFromFile_internal(const char *fileName, const char *setPrivateAccountname,const char *setPublicAccountname, U32 setAccountid, bool overwriteAccountData)
{
	TransactionRequest *request = objCreateTransactionRequest();
	ContainerID conid = dbLoadEntityFromFile(GLOBALTYPE_ENTITYPLAYER, fileName, request, setAccountid, setPrivateAccountname, setPublicAccountname, overwriteAccountData);

	if ( conid )
	{
		AccountInfoData *cbData = calloc(sizeof(AccountInfoData),1);

		cbData->type = GLOBALTYPE_ENTITYPLAYER;
		cbData->id = conid;
		cbData->accountid = setAccountid;
		strcpy(cbData->accountname, setPrivateAccountname);

		objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC, 
			objCreateManagedReturnVal(SetAccountInfo_CB, cbData), 
			"ImportEntity", request);

		servLog(LOG_CONTAINER, "ImportFromFile", "Filename \"%s\"", fileName);
	}

	objDestroyTransactionRequest(request);

	return conid;
}

//This command imports an entity from a file on the ObjectDB machine.
AUTO_COMMAND ACMD_CATEGORY(ObjectDB,CSR) ACMD_ACCESSLEVEL(9);
U32 ImportEntityFromFile(const char *fileName, const char *setPrivateAccountname,const char *setPublicAccountname, U32 setAccountid, bool overwriteAccountData)
{
	return ImportEntityFromFile_internal(fileName, setPrivateAccountname, setPublicAccountname, setAccountid, overwriteAccountData);
}


AUTO_COMMAND_REMOTE ACMD_CATEGORY(ObjectDB) ACMD_NAME(DBImportEntityFromFile) ACMD_ACCESSLEVEL(9);
U32 dbImportEntityFromFile(const char *fileName, const char *setPrivateAccountname,const char *setPublicAccountname, U32 setAccountid, bool overwriteAccountData)
{
	return ImportEntityFromFile_internal(fileName, setPrivateAccountname, setPublicAccountname, setAccountid, overwriteAccountData);
}


static bool dbSaveEntityToFile(GlobalType containerType, U32 containerID, const char *outputDir, char *outputName, char **resultString)
{
	Container *container;

	if ( GlobalTypeParent(containerType) != GLOBALTYPE_ENTITY )
	{
		if (resultString) estrPrintf(resultString, "Failed to write entity container %s[%d]: Not an entity type", GlobalTypeToName(containerType), containerID);	
			return false;
	}

	if((container = objGetContainerEx(containerType, containerID, true, false, true)) || (container = objGetDeletedContainerEx(containerType, containerID, true, false, true)))
	{
		if(container->containerData)
		{
			char* estr = NULL;
			char * filename = NULL;

			estrStackCreate(&filename);
			if (!outputDir || !outputDir[0])
			{
				U32 accountID = accountIDFromEntityPlayerContainer(container);
				dbAccountIDMakeExportPath(&filename, accountID);
			}
			else
			{
				estrAppend2(&filename, outputDir);
			}

			makeDirectories(filename);
		
			if (outputName)
				estrConcatf(&filename, "/%s.con",outputName);
			else
				estrConcatf(&filename,"/%s[%u].con",GlobalTypeToName(containerType),containerID);

			estrStackCreate(&estr);
		
			//at this point, estr has information about the owner entity as well as the pets - write it out to file
			//!!dbSerializeEntityForExport releases the lock on container!!
			if ( dbSerializeEntityForExport(&estr, &container) )
			{
				FILE *pOutFile = NULL;

				pOutFile = fopen(filename, "wt");

				if ( pOutFile )
				{
					fprintf(pOutFile, "%s", estr);
					fclose(pOutFile);
				}
				else
				{
					estrClear(&filename);
				}
			}

			estrDestroy(&estr);

			if (resultString) 
				estrPrintf(resultString, "%s", filename);

			if (filename[0])
			{
				estrDestroy(&filename);
				return true;
			}
			else
			{
				estrDestroy(&filename);
				return false;
			}
		}
		else
		{
			objUnlockContainer(&container);
			if (resultString) estrPrintf(resultString, "Failed to write entity container %s[%d]: Container has no data", GlobalTypeToName(containerType), containerID);	
				return false;
		}
	}
	else
	{
		if (resultString) estrPrintf(resultString, "Failed to write entity container %s[%d]: Container does not exist", GlobalTypeToName(containerType), containerID);	
			return false;
	}
}


AUTO_COMMAND_REMOTE ACMD_CATEGORY(ObjectDB) ACMD_NAME(DBExportEntityToFile) ACMD_ACCESSLEVEL(9);
char* dbExportEntityToFile(GlobalType type, U32 id, const char *fileName)
{
	static char result[1024];
	char *resultString = 0;
	estrStackCreate(&resultString);

	if (dbSaveEntityToFile(type, id, fileName, NULL, &resultString))
	{
		servLog(LOG_CONTAINER, "ContainerExportSucceeded", "ContainerType %s ContainerID %u Filename \"%s\"", GlobalTypeToName(type), id, resultString);
	}
	else
	{
		servLog(LOG_CONTAINER, "ContainerExportFailed", "ContainerType %s ContainerID %u Reason \"%s\"", GlobalTypeToName(type), id, resultString);
	}
	sprintf(result, "%s", resultString);
	estrDestroy(&resultString);
	return result;
}


AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_NAME(DBDumpEntity) ACMD_ACCESSLEVEL(4);
char* dbDumpEntity(GlobalType type, U32 id, bool transfer)
{
	static char result[1024];
	char *resultString = 0;
	estrStackCreate(&resultString);

	if (dbSaveEntityToFile(type, id, NULL, NULL, &resultString))
	{
		char *buf = NULL;
		estrStackCreate(&buf);
		estrPrintf(&buf, "%s", resultString);

		if (!transfer)
		{	//under normal circumstances, leave it as a txt file and return a servermon link
			dbChangeFilePathEstringToHttpPath(&buf);
			sprintf(result, "%s <br/>[<a href=\"%s\">Download</a>]", resultString, buf);
		}
		else
		{	//if we're transfering it, rename it so the transfer script catches it.
			char *dot = strrchr(buf, '.');
			*dot = '\0';
			fileMove(resultString, buf);
			sprintf(result, "%s", buf);
		}

		estrDestroy(&buf);
		
		servLog(LOG_CONTAINER, "ContainerExportSucceeded", "ContainerType %s ContainerID %u Filename \"%s\"", GlobalTypeToName(type), id, resultString);
	}
	else
	{
		servLog(LOG_CONTAINER, "ContainerExportFailed", "ContainerType %s ContainerID %u Reason \"%s\"", GlobalTypeToName(type), id, resultString);
		sprintf(result, "failure: %s", resultString);
	}
	estrDestroy(&resultString);
	return result;
}

AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_NAME(DBDumpEntity_XMLRPC) ACMD_ACCESSLEVEL(4);
char *dbDumpEntity_XMLRPC(char *pTypeName, ContainerID iID, CmdContext *pContext)
{
	static char result[1024];
	char *resultString = 0;
	GlobalType type;

	if(!pTypeName || !pTypeName[0])
	{
		sprintf(result, "failure: No type included");
		return result;
	}

	type = NameToGlobalType(pTypeName);

	if(!type)
	{
		sprintf(result, "failure: Invalid type specified");
		return result;
	}

	estrStackCreate(&resultString);
	if (dbSaveEntityToFile(type, iID, NULL, NULL, &resultString))
	{
		char *buf = NULL;
		estrStackCreate(&buf);
		estrPrintf(&buf, "%s", resultString);

		//under normal circumstances, leave it as a txt file and return a servermon link
		dbChangeFilePathEstringToHttpPath(&buf);
		sprintf(result, "%s", buf);
	
		estrDestroy(&buf);
		
		servLog(LOG_CONTAINER, "ContainerExportSucceeded", "ContainerType %s ContainerID %u Filename \"%s\"", GlobalTypeToName(type), iID, resultString);
	}
	else
	{
		servLog(LOG_CONTAINER, "ContainerExportFailed", "ContainerType %s ContainerID %u Reason \"%s\"", GlobalTypeToName(type), iID, resultString);
		sprintf(result, "failure: %s", resultString);
	}
	estrDestroy(&resultString);
	return result;
}

AUTO_COMMAND ACMD_CATEGORY(DEBUG);
char *ServerMonDumpEntity(char *pTypeName, ContainerID iID, CmdContext *pContext)
{
	GlobalType type;
	if(!pTypeName || !pTypeName[0])
		return NULL;

	type = NameToGlobalType(pTypeName);
	return dbDumpEntity(type, iID, false);
}



AUTO_COMMAND ACMD_CATEGORY(DEBUG);
char *ServerMonGetEntityRAMUsage(char *pTypeName, ContainerID iID, bool bAllocateABunchOfCopiesToTestReporting)
{
	GlobalType type;
	Container *pContainer;
	static char *spRetVal = NULL;
	S64 iRamUsage;

	if(!pTypeName || !pTypeName[0] || !(type = NameToGlobalType(pTypeName)))
	{
		return "Invalid type name";
	}

	pContainer = objGetContainerEx(type, iID, true, false, true);

	if (!pContainer)
	{
		return "Container does not exist";
	}
	
	if (!pContainer->containerData)
	{
		objUnlockContainer(&pContainer);
		return "Container has no data, somehow";
	}

/*	{
		int i;
		S64 iStartingTime; 
	

		iStartingTime = timeGetTime();
		for (i = 0; i < 100000; i++)
		{
			Packet *pPkt = pktCreateTemp(NULL);
			ParserSend(pContainer->containerSchema->classParse, pPkt, NULL,
				pContainer->containerData, SENDDIFF_FLAG_FORCEPACKALL, TOK_PERSIST | TOK_SUBSCRIBE, 0, NULL);
			pktFree(&pPkt);
		}
		printf("100000 ParserSends took %f seconds\n", ((float)(timeGetTime() - iStartingTime)) / 1000.0f);


		iStartingTime = timeGetTime();
		for (i = 0; i < 100000; i++)
		{
			void *pOtherStruct = StructCreateVoid(pContainer->containerSchema->classParse);
			StructCopyVoid(pContainer->containerSchema->classParse, pContainer->containerData, pOtherStruct, 0, TOK_PERSIST | TOK_SUBSCRIBE, 0);
			StructDestroyVoid(pContainer->containerSchema->classParse, pOtherStruct);
		}
		printf("100000 Structcopy/destroys took %f seconds\n", ((float)(timeGetTime() - iStartingTime)) / 1000.0f);

	}*/


	iRamUsage = StructGetMemoryUsage(pContainer->containerSchema->classParse, pContainer->containerData, true);
	
	estrMakePrettyBytesString(&spRetVal, iRamUsage);

	{
		void *pOtherStruct = StructCreateVoid(pContainer->containerSchema->classParse);
		char *pTempString = NULL;
		S64 iSubscribeCopyRAMUsage;
		StructCopyVoid(pContainer->containerSchema->classParse, pContainer->containerData, pOtherStruct, 0, TOK_PERSIST | TOK_SUBSCRIBE, 0);
		iSubscribeCopyRAMUsage = StructGetMemoryUsage(pContainer->containerSchema->classParse, pOtherStruct, true);
		estrMakePrettyBytesString(&pTempString, iSubscribeCopyRAMUsage);
		StructDestroyVoid(pContainer->containerSchema->classParse, pOtherStruct);
		estrConcatf(&spRetVal, ". Subscribe copy uses: %s\n", pTempString);
		estrDestroy(&pTempString);
	}

	if (bAllocateABunchOfCopiesToTestReporting)
	{
		void **ppOtherStructs = NULL;
		S64 iSizeBefore;
		S64 iSizeAfter;
		int i;
		char *pTempString1 = NULL;
		char *pTempString2 = NULL;
		char *pTempString3 = NULL;
		char *pTempString4 = NULL;

		eaSetCapacity(&ppOtherStructs, 4096);
				
		iSizeBefore = getProcessPageFileUsage();

		for (i = 0; i < 4096; i++)
		{
			eaPush(&ppOtherStructs, StructCloneVoid(pContainer->containerSchema->classParse, pContainer->containerData));
		}

		iSizeAfter = getProcessPageFileUsage();

		estrMakePrettyBytesString(&pTempString1, iSizeBefore);
		estrMakePrettyBytesString(&pTempString2, iSizeAfter);
		estrMakePrettyBytesString(&pTempString3, iSizeAfter - iSizeBefore);
		estrMakePrettyBytesString(&pTempString4, (iSizeAfter - iSizeBefore) / 4096);

		estrConcatf(&spRetVal, "Allocating 4096 extra of this container increased mem usage from %s to %s, a total of %s, or %s per object\n",
			pTempString1, pTempString2, pTempString3, pTempString4);

		estrDestroy(&pTempString1);
		estrDestroy(&pTempString2);
		estrDestroy(&pTempString3);
		estrDestroy(&pTempString4);

		eaDestroyStructVoid(&ppOtherStructs, pContainer->containerSchema->classParse);
	}

	objUnlockContainer(&pContainer);

	return spRetVal;
}

static void UndeleteCharacter_CB(TransactionReturnVal *returnVal, ContainerRef *cbData)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		dbContainerLog(LOG_CONTAINER, cbData->containerType, true, cbData->containerID, "UndeleteContainer", "Type %s, ID %u, Result Success", GlobalTypeToName(cbData->containerType), cbData->containerID);
	}
	else
	{
		dbContainerLog(LOG_CONTAINER, cbData->containerType, true, cbData->containerID, "UndeleteContainer", "Type %s, ID %u, Result Failure", GlobalTypeToName(cbData->containerType), cbData->containerID);
	}
	StructDestroy(parse_ContainerRef, cbData);
}

AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_NAME(UndeleteCharacter) ACMD_ACCESSLEVEL(9);
int UndeleteCharacter(CmdContext *context, U32 id)
{
	int result = 0;
	int queue_success = 0;
	Container *container = objGetDeletedContainerEx(GLOBALTYPE_ENTITYPLAYER, id, false, false, true);
	if(container)
	{
		ContainerRef *ref;
		char *newNameStr = NULL;
		U32 accountid = container->header->accountId;
		char *charname = strdup(container->header->savedName);
		objUnlockContainer(&container);

		newNameStr = FindNonDuplicateName(charname, accountid);
		free(charname);

		ref = StructCreate(parse_ContainerRef);
		ref->containerType = GLOBALTYPE_ENTITYPLAYER;
		ref->containerID = id;
		
		objRequestContainerUndelete(objCreateManagedReturnVal(UndeleteCharacter_CB, ref), GLOBALTYPE_ENTITYPLAYER, id, objServerType(), objServerID(), newNameStr);
		estrDestroy(&newNameStr);
		return CR_QUEUED;
	}
	
	return CR_CONTAINERNOTFOUND;
}

AUTO_COMMAND_REMOTE;
ContainerLocationList *GetMultipleContainerOwners(ContainerLocationList *pInList, bool bIgnoreThingsOwnedByObjectDB)
{
	ContainerLocationList *pOutList = StructCreate(parse_ContainerLocationList);
	int i;
	ContainerLocation *pLocation;

	for (i=0; i < eaSize(&pInList->ppList); i++)
	{
		Container *pObject = objGetContainerEx(pInList->ppList[i]->containerType, pInList->ppList[i]->containerID, false, false, true);

		if (pObject)
		{
			if (pObject->meta.containerOwnerType == GLOBALTYPE_OBJECTDB && bIgnoreThingsOwnedByObjectDB)
			{
				objUnlockContainer(&pObject);
				continue;
			}

			pLocation = StructClone(parse_ContainerLocation, pInList->ppList[i]);
			pLocation->ownerType = pObject->meta.containerOwnerType;
			pLocation->ownerID = pObject->meta.containerOwnerID;
			objUnlockContainer(&pObject);

			eaPush(&pOutList->ppList, pLocation);
		}
	}

	return pOutList;
}

AUTO_COMMAND_REMOTE;
U32 dbGetActivePlayerCount(void)
{
	return objCountUnownedContainersWithType(GLOBALTYPE_ENTITYPLAYER);
}

bool FindDependentContainer(ParseTable pti[], void *pStruct, int column, int index, void *pCBData)
{
	char *ptypestring;
	ContainerRef *conref = NULL;
	ContainerRef ***pppRefs = (ContainerRef ***)pCBData;
	ContainerID id = 0;
	GlobalType eGlobalType = GLOBALTYPE_NONE;
	bool found = false;

	PERFINFO_AUTO_START_FUNC();

	if(GetStringFromTPIFormatString(pti + column, "DEPENDENT_CONTAINER_TYPE", &ptypestring))
	{
		found = GetContainerTypeAndIDFromTypeString(pti, pStruct, column, index, ptypestring, NULL, &id, &eGlobalType);
	}
	else if(GetBoolFromTPIFormatString(pti + column, "DEPENDENT_CONTAINER_REF"))
	{
		found = GetContainerTypeAndIDFromContainerRef(pti, pStruct, column, index, NULL, &id, &eGlobalType);
	}

	if(found && !alreadyInRefs(*pppRefs, eGlobalType, id))
	{
		conref = StructCreate(parse_ContainerRef);
		conref->containerID = id;
		conref->containerType = eGlobalType;
		eaPush(pppRefs, conref);
	}

	PERFINFO_AUTO_STOP();
	return false;
}

void OVERRIDE_LATELINK_objGetDependentContainers(GlobalType containerType, Container **con, ContainerRef ***pppRefs, bool recursive)
{
	ContainerStore *store = objFindContainerStoreFromType(containerType);
	int prevRefCount = 0;
	void *conData = NULL;

	if (!con || !*con || !pppRefs)
		return;

	PERFINFO_AUTO_START_FUNC();
	prevRefCount = eaSize(pppRefs);
	ParserTraverseParseTable(store->containerSchema->classParse, (*con)->containerData, TOK_PERSIST, TOK_UNOWNED | TOK_REDUNDANTNAME, FindDependentContainer, NULL, NULL, pppRefs);

	if (recursive)
	{
		if (!(*con)->isTemporary)
			objUnlockContainer(con);

		for (; prevRefCount < eaSize(pppRefs); ++prevRefCount)
		{
			ContainerRef *pRef = (*pppRefs)[prevRefCount];
			Container *dependentCon = objGetContainerEx(pRef->containerType, pRef->containerID, true, false, true);

			if (dependentCon)
				objGetDependentContainers(pRef->containerType, &dependentCon, pppRefs, recursive);
		}
	}

	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND_REMOTE;
ContainerRefArray *DBReturnDependentContainers( GlobalType iType, ContainerID iID )
{
	ContainerRefArray *pRefArray = StructCreate(parse_ContainerRefArray);
	Container *pContainer = objGetContainerEx(iType, iID, true, false, true);

	if (!pContainer)
	{
		return NULL;
	}
	else if (!pContainer->containerData)
	{
		objUnlockContainer(&pContainer);
		return NULL;
	}
	else
	{
		objGetDependentContainers(iType, &pContainer, &pRefArray->containerRefs, true);
		return pRefArray;
	}
}

#define LOCATION_CHECKER_REMOTE_COMMAND_TIMEOUT 5


AUTO_STRUCT;
typedef struct ContainerCheckerReturn
{
	GlobalType eOwnerType_FromDB;
	ContainerID iOwnerID_FromDB;

	bool bTransServerReturned;
	GlobalType eOwnerType_FromTransServer;
	ContainerID iOwnerID_FromTransServer;
	bool bTransServerLocationIsDefault;

	bool bDBOwnerReturned;
	bool bDBOwnerOwned;

	bool bTransOwnerReturned;
	bool bTransOwnerOwned;
} ContainerCheckerReturn;

typedef void ContainerCheckerReturnCB(ContainerCheckerReturn *pReturn, void *pUserData);


AUTO_STRUCT;
typedef struct ContainerLocationChecker
{
	int iRequestID;

	GlobalType eType;
	ContainerID iID;

	GlobalType eOwnerType_FromDB;
	ContainerID iOwnerID_FromDB;

	bool bTransServerReturned;
	GlobalType eOwnerType_FromTransServer;
	ContainerID iOwnerID_FromTransServer;
	bool bTransServerLocationIsDefault;

	U32 iDBOwnerRequestIssuedTime;
	bool bDBOwnerReturned;
	bool bDBOwnerOwned;

	U32 iTransOwnerRequestIssuedTime;
	bool bTransOwnerReturned;
	bool bTransOwnerOwned;

	CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo; NO_AST
	ContainerCheckerReturnCB *pReturnCB; NO_AST
	void *pUserData; NO_AST
} ContainerLocationChecker;

ContainerLocationChecker **sppLocationCheckers = NULL;
int siNextContainerCheckerID = 1;


bool CheckCheckerForPossibleReturn(ContainerLocationChecker *pChecker);


ContainerLocationChecker *FindCheckerFromRequestID(int iRequestID)
{
	int i;

	for (i=0; i < eaSize(&sppLocationCheckers); i++)
	{
		if (sppLocationCheckers[i]->iRequestID == iRequestID)
		{
			return sppLocationCheckers[i];
		}
	}

	return NULL;
}

static void DoCheckerReturn(ContainerLocationChecker *pChecker, char *pRetString)
{
	if (pChecker->pSlowReturnInfo)
	{
		DoSlowCmdReturn(1, pRetString, pChecker->pSlowReturnInfo);
	}

	if (pChecker->pReturnCB)
	{
		ContainerCheckerReturn sCheckerReturn = {0};
		sCheckerReturn.eOwnerType_FromDB = pChecker->eOwnerType_FromDB;
		sCheckerReturn.iOwnerID_FromDB = pChecker->iOwnerID_FromDB;

		sCheckerReturn.bTransServerReturned = pChecker->bTransServerReturned;
		sCheckerReturn.eOwnerType_FromTransServer = pChecker->eOwnerType_FromTransServer;
		sCheckerReturn.iOwnerID_FromTransServer = pChecker->iOwnerID_FromTransServer;
		sCheckerReturn.bTransServerLocationIsDefault = pChecker->bTransServerLocationIsDefault;

		sCheckerReturn.bDBOwnerReturned = pChecker->bDBOwnerReturned;
		sCheckerReturn.bDBOwnerOwned = pChecker->bDBOwnerOwned;

		sCheckerReturn.bTransOwnerReturned = pChecker->bTransOwnerReturned;
		sCheckerReturn.bTransOwnerOwned = pChecker->bTransOwnerOwned;
		
		pChecker->pReturnCB(&sCheckerReturn, pChecker->pUserData);
	}
}



//returns true if returned
bool CheckCheckerForPossibleReturn(ContainerLocationChecker *pChecker)
{
	char *pRetString = NULL;
	int i;
	U32 iTimeCutoff = timeSecondsSince2000() - LOCATION_CHECKER_REMOTE_COMMAND_TIMEOUT;

	if (!pChecker->bTransServerReturned)
	{
		return false;
	}

	if (pChecker->iDBOwnerRequestIssuedTime && pChecker->iDBOwnerRequestIssuedTime > iTimeCutoff && !pChecker->bDBOwnerReturned)
	{
		return false;
	}

	if (pChecker->iTransOwnerRequestIssuedTime && pChecker->iTransOwnerRequestIssuedTime  > iTimeCutoff && !pChecker->bTransOwnerReturned)
	{
		return false;
	}

	estrPrintf(&pRetString, "CheckContainerLoc called for %s[%d]\n", GlobalTypeToName(pChecker->eType), pChecker->iID);
	estrConcatf(&pRetString, "ObjectDb thinks owner is: %s[%d]\n", GlobalTypeToName(pChecker->eOwnerType_FromDB), pChecker->iOwnerID_FromDB);
	estrConcatf(&pRetString, "Trans Server thinks owner is: %s[%d]%s\n", GlobalTypeToName(pChecker->eOwnerType_FromTransServer), pChecker->iOwnerID_FromTransServer, pChecker->bTransServerLocationIsDefault ? "(which is the default)" : "");
	
	if (pChecker->bDBOwnerReturned)
	{
		estrConcatf(&pRetString, "%s[%d] thinks it %s the owner\n", GlobalTypeToName(pChecker->eOwnerType_FromDB), pChecker->iOwnerID_FromDB, pChecker->bDBOwnerOwned ? "IS" : "IS NOT");
	}
	else if (pChecker->iDBOwnerRequestIssuedTime && pChecker->iDBOwnerRequestIssuedTime <= iTimeCutoff)
	{
		estrConcatf(&pRetString, "%s[%d] was queried, but did not respond in %d seconds\n", GlobalTypeToName(pChecker->eOwnerType_FromDB), pChecker->iOwnerID_FromDB, LOCATION_CHECKER_REMOTE_COMMAND_TIMEOUT);
	}
	
	if (pChecker->bTransOwnerReturned)
	{
		estrConcatf(&pRetString, "%s[%d] thinks it %s the owner\n", GlobalTypeToName(pChecker->eOwnerType_FromTransServer), pChecker->iOwnerID_FromTransServer, pChecker->bTransOwnerOwned ? "IS" : "IS NOT");
	}
	else if (pChecker->iTransOwnerRequestIssuedTime && pChecker->iTransOwnerRequestIssuedTime <= iTimeCutoff)
	{
		estrConcatf(&pRetString, "%s[%d] was queried, but did not respond in %d seconds\n", GlobalTypeToName(pChecker->eOwnerType_FromTransServer), pChecker->iOwnerID_FromTransServer, LOCATION_CHECKER_REMOTE_COMMAND_TIMEOUT);
	}

	DoCheckerReturn(pChecker, pRetString);
	estrDestroy(&pRetString);
	SAFE_FREE(pChecker->pSlowReturnInfo);

	for (i=0; i < eaSize(&sppLocationCheckers); i++)
	{
		if (sppLocationCheckers[i] == pChecker)
		{
			free(pChecker);
			eaRemoveFast(&sppLocationCheckers, i);
			return true;
		}
	}

	return true;
}

void ContainerLocationCheckerTransCB(GlobalType eContainerType, ContainerID iContainerID, 
	GlobalType eOwnerType, ContainerID iOwnerID, bool bIsDefaultLocation, void *pUserData)
{
	ContainerLocationChecker *pChecker = FindCheckerFromRequestID((int)((intptr_t)pUserData));

	if (!pChecker)
	{
		return;
	}

	pChecker->bTransServerReturned = true;
	pChecker->eOwnerType_FromTransServer = eOwnerType;
	pChecker->iOwnerID_FromTransServer = iOwnerID;
	pChecker->bTransServerLocationIsDefault = bIsDefaultLocation;

	if (eOwnerType != GLOBALTYPE_OBJECTDB && eOwnerType != GLOBALTYPE_NONE && !(eOwnerType == pChecker->eOwnerType_FromDB && iOwnerID == pChecker->iOwnerID_FromDB))
	{
		RemoteCommand_DebugCheckContainer_DoYouOwnContainer(pChecker->eOwnerType_FromTransServer, pChecker->iOwnerID_FromTransServer,
			GetAppGlobalType(), GetAppGlobalID(),
			pChecker->eType, pChecker->iID, pChecker->iRequestID, true);
		pChecker->iTransOwnerRequestIssuedTime = timeSecondsSince2000_ForceRecalc();
	}

	CheckCheckerForPossibleReturn(pChecker);		
}

void OVERRIDE_LATELINK_DoYouOwnContainerReturn_Internal(int iRequestID, bool bIsTrans, bool bIOwn)
{
	ContainerLocationChecker *pChecker = FindCheckerFromRequestID(iRequestID);

	if (!pChecker)
	{
		return;
	}

	if (bIsTrans)
	{
		pChecker->bTransOwnerReturned = true;
		pChecker->bTransOwnerOwned = bIOwn;
	}
	else
	{
		pChecker->bDBOwnerReturned = true;
		pChecker->bDBOwnerOwned = bIOwn;
	}

	CheckCheckerForPossibleReturn(pChecker);		
}

void CheckContainerTimedCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	ContainerLocationChecker *pChecker = FindCheckerFromRequestID((int)((intptr_t)userData));

	if (!pChecker)
	{
		return;
	}

	if (!CheckCheckerForPossibleReturn(pChecker))
	{
		TimedCallback_Run(CheckContainerTimedCB, userData, 2.0f);
	}

}

AUTO_COMMAND ACMD_CATEGORY(DEBUG);
char *DebugCheckContainerLoc(char *pTypeName, ContainerID iID, CmdContext *pContext)
{
	Container *con;
	static char *pRetString = NULL;
	ContainerLocationChecker *pChecker;
	GlobalType eType;

	if(!pTypeName || !pTypeName[0])
		return "No type name specified.";

	//allow both ints and strings
	eType = atoi(pTypeName);
	if (!eType)
	{
		eType = NameToGlobalType(pTypeName);
	}

	if (!objLocalManager())
	{
		return "No obj local manager";
	}

	if (!eType)
	{
		estrPrintf(&pRetString, "Invalid global type %s\n", pTypeName);
		return pRetString;
	}

	con = objGetContainerEx(eType, iID, false, false, true);

	if (!con)
	{
		estrPrintf(&pRetString, "ObjectDB does not know about %s\n", GlobalTypeAndIDToString(eType, iID));
		return pRetString;
	}

	pChecker = StructCreate(parse_ContainerLocationChecker);
	pChecker->iRequestID = siNextContainerCheckerID++;
	pChecker->eType = eType;
	pChecker->iID = iID;
	pChecker->eOwnerType_FromDB = con->meta.containerOwnerType;
	pChecker->iOwnerID_FromDB = con->meta.containerOwnerID;
	objUnlockContainer(&con);

	pContext->slowReturnInfo.bDoingSlowReturn = true;
	pChecker->pSlowReturnInfo = malloc(sizeof(CmdSlowReturnForServerMonitorInfo));
	memcpy(pChecker->pSlowReturnInfo, &pContext->slowReturnInfo, sizeof(CmdSlowReturnForServerMonitorInfo));

	eaPush(&sppLocationCheckers, pChecker);

	RequestTransactionServerContainerLocation(objLocalManager(), ContainerLocationCheckerTransCB, (void*)(intptr_t)(pChecker->iRequestID), 
		eType, iID);

	if (pChecker->eOwnerType_FromDB != GLOBALTYPE_OBJECTDB && pChecker->eOwnerType_FromDB != GLOBALTYPE_NONE)
	{
		RemoteCommand_DebugCheckContainer_DoYouOwnContainer(pChecker->eOwnerType_FromDB, pChecker->iOwnerID_FromDB,
			GetAppGlobalType(), GetAppGlobalID(),
			pChecker->eType, pChecker->iID, pChecker->iRequestID, false);
		pChecker->iDBOwnerRequestIssuedTime = timeSecondsSince2000_ForceRecalc();
	}

	TimedCallback_Run(CheckContainerTimedCB, (UserData)((intptr_t)pChecker->iRequestID), 2.0f);

	return NULL;
}

//if returns non-NULL, that must be an error message
char *CheckContainerLocation_WithCB(GlobalType eType, ContainerID iID, 
							   ContainerCheckerReturnCB *pCB, void *pUserData)
{
	Container *con;
	static char *pRetString = NULL;
	ContainerLocationChecker *pChecker;

	if (!objLocalManager())
	{
		return "No obj local manager";
	}

	if (!eType)
	{
		estrPrintf(&pRetString, "Invalid global type");
		return pRetString;
	}

	con = objGetContainerEx(eType, iID, false, false, true);
	if (!con)
	{
		estrPrintf(&pRetString, "ObjectDB does not know about %s\n", GlobalTypeAndIDToString(eType, iID));
		return pRetString;
	}

	pChecker = StructCreate(parse_ContainerLocationChecker);
	pChecker->iRequestID = siNextContainerCheckerID++;
	pChecker->eType = eType;
	pChecker->iID = iID;
	pChecker->eOwnerType_FromDB = con->meta.containerOwnerType;
	pChecker->iOwnerID_FromDB = con->meta.containerOwnerID;
	objUnlockContainer(&con);

	pChecker->pReturnCB = pCB;
	pChecker->pUserData = pUserData;

	eaPush(&sppLocationCheckers, pChecker);

	RequestTransactionServerContainerLocation(objLocalManager(), ContainerLocationCheckerTransCB, (void*)(intptr_t)(pChecker->iRequestID), 
		eType, iID);

	if (pChecker->eOwnerType_FromDB != GLOBALTYPE_OBJECTDB && pChecker->eOwnerType_FromDB != GLOBALTYPE_NONE)
	{
		RemoteCommand_DebugCheckContainer_DoYouOwnContainer(pChecker->eOwnerType_FromDB, pChecker->iOwnerID_FromDB,
			GetAppGlobalType(), GetAppGlobalID(),
			pChecker->eType, pChecker->iID, pChecker->iRequestID, false);
		pChecker->iDBOwnerRequestIssuedTime = timeSecondsSince2000_ForceRecalc();
	}

	TimedCallback_Run(CheckContainerTimedCB, (UserData)((intptr_t)pChecker->iRequestID), 2.0f);

	return NULL;
}

void GetDebugContainerLocStringCB(ContainerID iMCPID, int iRequestID, int iClientID, char *pMessageString, void *pUserData)
{
	SlowRemoteCommandReturn_GetDebugContainerLocString(iRequestID, pMessageString);
}

AUTO_COMMAND_REMOTE_SLOW(char*);
void GetDebugContainerLocString(GlobalType eType, ContainerID iContainerID, SlowRemoteCommandID iCmdID)
{
	CmdContext context = {0};
	char *pCmdString = NULL;
	char *pOutString = NULL;

	context.access_level = 9;
	context.slowReturnInfo.pSlowReturnCB = GetDebugContainerLocStringCB;
	context.slowReturnInfo.iCommandRequestID = iCmdID;
	context.output_msg = &pOutString;

	estrPrintf(&pCmdString, "DebugCheckContainerLoc %s %u", GlobalTypeToName(eType), iContainerID);

	cmdParseAndExecute(&gGlobalCmdList, pCmdString, &context);

	if (!context.slowReturnInfo.bDoingSlowReturn)
	{
		estrInsertf(&pOutString, 0, "DebugCheckContainerLoc failed: ");
		SlowRemoteCommandReturn_GetDebugContainerLocString(iCmdID, pOutString);
	}

	estrDestroy(&pOutString);
	estrDestroy(&pCmdString);
}

void PrintfSlowCmdReturnCallbackFunc(ContainerID iMCPID, int iRequestID, int iClientID, char *pMessageString, void *pUserData)
{
	printf("%s", pMessageString);
}


AUTO_COMMAND;
void TestGetDebugContainerLoc(char *pTypeName, ContainerID iContainerID, CmdContext *pCallingContext)
{
	
	CmdContext context = {0};
	char *pCmdString = NULL;
	char *pOutString = NULL;
	context.output_msg = &pOutString;
	
	context.slowReturnInfo.pSlowReturnCB = PrintfSlowCmdReturnCallbackFunc;
	context.slowReturnInfo.bDoingSlowReturn = true;

	context.access_level = pCallingContext->access_level;
	

	estrPrintf(&pCmdString, "DebugCheckContainerLoc %s %u", pTypeName, iContainerID);

	cmdParseAndExecute(&gGlobalCmdList, pCmdString, &context);

	printf("Output string: %s", pOutString);

	estrDestroy(&pCmdString);
	estrDestroy(&pOutString);

}

typedef void AttempToFixContainerReturnCB(char *pRetString, void *pUserData);

typedef struct ContainerFixAttempt
{
	GlobalType eContainerType;
	ContainerID iID;
	ContainerCheckerReturn *pCheckerReturn1;
	AttempToFixContainerReturnCB *pCB;
	void *pUserData;
	int iForce;
} ContainerFixAttempt;

void FixAttemptReturnAndCleanup(ContainerFixAttempt *pAttempt, char *pStr, ...)
{
	char *pFullString = NULL;
	estrGetVarArgs(&pFullString, pStr);

	if (pAttempt->pCB)
	{
		pAttempt->pCB(pFullString, pAttempt->pUserData);
	}

	estrDestroy(&pFullString);
	if (pAttempt->pCheckerReturn1)
	{
		StructDestroy(parse_ContainerCheckerReturn, pAttempt->pCheckerReturn1);
	}

	free(pAttempt);
}

void SecondFixAttempt(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData);

bool ContainerIsFixed(ContainerCheckerReturn *pReturn)
{
	if (!pReturn->bTransServerReturned)
	{
		return false;
	}

	if (pReturn->eOwnerType_FromDB != pReturn->eOwnerType_FromTransServer)
	{
		return false;
	}

	if (pReturn->iOwnerID_FromDB != pReturn->iOwnerID_FromTransServer)
	{
		return false;
	}

	if (pReturn->eOwnerType_FromDB == GLOBALTYPE_OBJECTDB)
	{
		return true;
	}

	if (pReturn->bDBOwnerOwned && pReturn->bDBOwnerReturned)
	{
		return true;
	}

	return false;
}


void FixContainerCBFromCheckContainer(ContainerCheckerReturn *pReturn, void *pUserData)
{
	ContainerFixAttempt *pFixAttempt = (ContainerFixAttempt *)pUserData;
	char *pFullErrorString = NULL;

	if (ContainerIsFixed(pReturn))
	{
		FixAttemptReturnAndCleanup(pFixAttempt, "After querying container location, container appears to be fixed");
		return;
	}

	if (!pFixAttempt->pCheckerReturn1)
	{
		pFixAttempt->pCheckerReturn1 = StructClone(parse_ContainerCheckerReturn, pReturn);
		TimedCallback_Run(SecondFixAttempt, pFixAttempt, 2.0f);
		return;
	}

	if (StructCompare(parse_ContainerCheckerReturn, pFixAttempt->pCheckerReturn1, pReturn, 0, 0, 0) != 0)
	{
		FixAttemptReturnAndCleanup(pFixAttempt, "Did two container location queries, got different results. Container is volatile.");
		return;
	}

	//now actually attempt to fix the container
	//
	//only case we're currently confident about is the case where the trans server thinks the object DB owns it, and
	//whatever the object DB thinks owns it disagrees, or doesn't exist
	if (pReturn->bTransServerReturned && pReturn->eOwnerType_FromTransServer == GLOBALTYPE_OBJECTDB
		&& (!pReturn->bDBOwnerOwned || !pReturn->bDBOwnerReturned))
	{
		char *updateString = NULL;
		estrPrintf(&updateString,"dbUpdateContainerOwner %d %d %d %d",pFixAttempt->eContainerType,pFixAttempt->iID,objServerType(),objServerID());
		dbHandleDatabaseUpdateString(updateString,0,0);
		FixAttemptReturnAndCleanup(pFixAttempt, "Found the stable broken case where the object DB thinks someone owns it but everyone else disagrees... ran dbUpdateContainerOwner");
		estrDestroy(&updateString);
		return;
	}

	if (pFixAttempt->iForce)
	{
		DBForciblyLogOutCharacter(pFixAttempt->eContainerType, pFixAttempt->iID);
		FixAttemptReturnAndCleanup(pFixAttempt, "Found a stable broken case. ForceFix was setup, so called DBForciblyLogOutCharacter");
		return;
	}


	ParserWriteText(&pFullErrorString, parse_ContainerCheckerReturn, pReturn, 0, 0, 0);

	FixAttemptReturnAndCleanup(pFixAttempt, "Found a stable broken case we didn't know how to deal with. Doing nothing: %s",
		pFullErrorString);

	estrDestroy(&pFullErrorString);
}

void SecondFixAttempt(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	ContainerFixAttempt *pFixAttempt = userData;
	char *pImmediateReturn = CheckContainerLocation_WithCB(pFixAttempt->eContainerType, pFixAttempt->iID, FixContainerCBFromCheckContainer, pFixAttempt);

	if (pImmediateReturn)
	{
		FixAttemptReturnAndCleanup(pFixAttempt, "%s", pImmediateReturn);
	}
}

void AttemptToFixContainerWithBrokenOwnership_Internal(GlobalType eContainerType, ContainerID iID, char *pReason, AttempToFixContainerReturnCB *pCB, void *pUserData, int iAttemptToForce)
{
	ContainerFixAttempt *pFixAttempt = calloc(sizeof(ContainerFixAttempt), 1);
	char *pImmediateReturn = NULL;

	pFixAttempt->pCB = pCB;
	pFixAttempt->pUserData = pUserData;
	pFixAttempt->eContainerType = eContainerType;
	pFixAttempt->iID = iID;
	pFixAttempt->iForce = iAttemptToForce;

	pImmediateReturn = CheckContainerLocation_WithCB(eContainerType, iID, FixContainerCBFromCheckContainer, pFixAttempt);

	if (pImmediateReturn)
	{
		FixAttemptReturnAndCleanup(pFixAttempt, "%s", pImmediateReturn);
		return;
	}
}

static int iRemoteFixAttemptID = 1;
void FixAttemptLoggingCB(char *pStr, void *pUserData)
{
	log_printf(LOG_ERRORS, "Result from ContainerFixAttempt %d: %s",
		(int)((intptr_t)pUserData), pStr);
}

AUTO_COMMAND_REMOTE;
void AttemptToFixContainerWithBrokenOwnership(GlobalType eContainerType, ContainerID iID, char *pReason)
{
	log_printf(LOG_ERRORS, "Received remote attempt %d to fix container %s because %s", 
		iRemoteFixAttemptID, GlobalTypeAndIDToString(eContainerType, iID), pReason);
	AttemptToFixContainerWithBrokenOwnership_Internal(eContainerType, iID, pReason, FixAttemptLoggingCB, (void*)((intptr_t)iRemoteFixAttemptID), 0);
	iRemoteFixAttemptID++;
}

void FixAttemptCmdCB(char *pStr, void *pUserData)
{
	DoSlowCmdReturn(1, pStr, pUserData);
	free(pUserData);
}

AUTO_COMMAND ACMD_CATEGORY(DEBUG);
char *AttemptToFixPlayerContainer(ContainerID iID, CmdContext *pContext, int iAttemptToForce)
{
	void *pUserData = malloc(sizeof(CmdSlowReturnForServerMonitorInfo));
	pContext->slowReturnInfo.bDoingSlowReturn = true;
	memcpy(pUserData, &pContext->slowReturnInfo, sizeof(CmdSlowReturnForServerMonitorInfo));

	AttemptToFixContainerWithBrokenOwnership_Internal(GLOBALTYPE_ENTITYPLAYER, iID, "AUTO_CMD called", FixAttemptCmdCB, pUserData, iAttemptToForce);
	return NULL;
}

// This is unsafe to run on a live shard, and is for local memory profiling
AUTO_COMMAND ACMD_CATEGORY(DEBUG);
void ContainerWriteMemoryReport(GlobalType type, ContainerID id)
{
	const char *report = NULL;
	Container *con = objGetContainerEx(type, id, true, false, true);
	if (con)
	{
		if (con->containerData)
		{	
			report = StructWriteMemoryReport(con->containerSchema->classParse, con->containerData);
			objUnlockContainer(&con);
			printf("Memory report for Container %s[%d]:\n%s", GlobalTypeToName(type), id, report);
			return;
		}

		objUnlockContainer(&con);
	}
	else
	{
		printf("Can't find Container %s[%d]\n", GlobalTypeToName(type), id);
	}
}

//
// Check if the list of containers exists.  Any that don't are in the list that is returned.
//
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
ContainerRefArray *DBCheckContainersExist(ContainerRefArray *containersIn)
{
	ContainerRefArray *containersOut = StructCreate(parse_ContainerRefArray);

	if ( containersIn != NULL )
	{
		int i;
		int n;

		// check all the passed in containers
		n = eaSize(&containersIn->containerRefs);
		for ( i = 0; i < n; i++ )
		{
			ContainerRef *containerRef = containersIn->containerRefs[i];
			if ( containerRef != NULL )
			{
				// if container doesn't exist, add it to the output list
				if (!objDoesContainerExist(containerRef->containerType, containerRef->containerID))
				{
					eaPush(&containersOut->containerRefs, StructClone(parse_ContainerRef, containerRef));
				}
			}
		}
	}

	return containersOut;
}

//very simple... returns 1 if the container exists, 0 otherwise
AUTO_COMMAND_REMOTE;
int DBCheckSingleContainerExists(GlobalType eType, ContainerID iID)
{
	if (objDoesContainerExist(eType, iID))
	{
		return 1;
	}

	return 0;
}

static void ConsoleDestroyContainer_CB(TransactionReturnVal *returnVal, ContainerRef *cbData)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		dbContainerLog(LOG_CONTAINER, cbData->containerType, true, cbData->containerID, "ConsoleDestroyContainer", "Type %s, ID %u, Result Success", GlobalTypeToName(cbData->containerType), cbData->containerID);
	}
	else
	{
		dbContainerLog(LOG_CONTAINER, cbData->containerType, true, cbData->containerID, "ConsoleDestroyContainer", "Type %s, ID %u, Result Failure", GlobalTypeToName(cbData->containerType), cbData->containerID);
	}
	StructDestroy(parse_ContainerRef, cbData);
}

AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_NAME(ConsoleDestroyContainer) ACMD_ACCESSLEVEL(9);
int ConsoleDestroyContainer(CmdContext *context, const char *typeName, U32 id)
{
	ContainerRef *ref;
	GlobalType type;
	
	if(!typeName || !typeName[0] || !id)
		return 0;

	type = NameToGlobalType(typeName);
	if(!type)
		return 0;

	ref = StructCreate(parse_ContainerRef);
	ref->containerType = type;
	ref->containerID = id;
	objRequestContainerDestroy(objCreateManagedReturnVal(ConsoleDestroyContainer_CB, ref), ref->containerType, id, objServerType(), objServerID());
	return 1;
}

AUTO_STRUCT;
typedef struct OwnerPetPair
{
	ContainerID ownerID;
	ContainerID petID;
	char result[20];
} OwnerPetPair;

AUTO_STRUCT;
typedef struct OwnerPetPairArray
{
	bool errors;
	OwnerPetPair **pairs;
} OwnerPetPairArray;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
OwnerPetPairArray *CleanupStalePets(CmdContext *context, OwnerPetPairArray *refs)
{
	OwnerPetPairArray *result = StructCreate(parse_OwnerPetPairArray);
	result->errors = false;
	FOR_EACH_IN_EARRAY(refs->pairs, OwnerPetPair, pair);
	{
		const char *returnStr = NULL;
		ContainerRef *ref;
		if(!pair)
		{
			returnStr = "Empty";
		}
		else if(!pair->ownerID || !pair->petID)
		{
			returnStr = "Bad Input";
		}
		else if(objDoesContainerExist(GLOBALTYPE_ENTITYPLAYER, pair->ownerID)
			|| objDoesDeletedContainerExist(GLOBALTYPE_ENTITYPLAYER, pair->ownerID))
		{
			returnStr = "Player exists";
		}
		else if(!objDoesContainerExist(GLOBALTYPE_ENTITYSAVEDPET, pair->petID))
		{
			returnStr = "Pet does not exist";
		}

		if(returnStr)
		{
			OwnerPetPair *resultPair = StructCreate(parse_OwnerPetPair);
			resultPair->ownerID = pair ? pair->ownerID : 0;
			resultPair->petID = pair ? pair->petID : 0;
			sprintf(resultPair->result, "%s", returnStr);
			result->errors = true;
			eaPush(&result->pairs, resultPair);
			continue;
		}

		ref = StructCreate(parse_ContainerRef);
		ref->containerType = GLOBALTYPE_ENTITYSAVEDPET;
		ref->containerID = pair->petID;
		objRequestContainerDestroy(objCreateManagedReturnVal(ConsoleDestroyContainer_CB, ref), ref->containerType, ref->containerID, objServerType(), objServerID());
	}
	FOR_EACH_END;

	return result;
}

static void ConsoleDestroyDeletedContainer_CB(TransactionReturnVal *returnVal, ContainerRef *cbData)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		dbContainerLog(LOG_CONTAINER, cbData->containerType, true, cbData->containerID, "ConsoleDestroyDeletedContainer", "Type %s, ID %u, Result Success", GlobalTypeToName(cbData->containerType), cbData->containerID);
	}
	else
	{
		dbContainerLog(LOG_CONTAINER, cbData->containerType, true, cbData->containerID, "ConsoleDestroyDeletedContainer", "Type %s, ID %u, Result Failure", GlobalTypeToName(cbData->containerType), cbData->containerID);
	}
	StructDestroy(parse_ContainerRef, cbData);
}

AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_NAME(ConsoleDestroyDeletedContainer) ACMD_ACCESSLEVEL(9);
int ConsoleDestroyDeletedContainer(CmdContext *context, const char *typeName, U32 id)
{
	ContainerRef *ref;
	GlobalType type;
	
	if(!typeName || !typeName[0] || !id)
		return 0;

	type = NameToGlobalType(typeName);
	if(!type)
		return 0;

	ref = StructCreate(parse_ContainerRef);
	ref->containerType = type;
	ref->containerID = id;
	objRequestDeletedContainerDestroy(objCreateManagedReturnVal(ConsoleDestroyDeletedContainer_CB, ref), ref->containerType, id, objServerType(), objServerID());
	return 1;
}

static void ConsoleDeleteCharacter_CB(TransactionReturnVal *returnVal, ContainerRef *cbData)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		dbContainerLog(LOG_CONTAINER, cbData->containerType, true, cbData->containerID, "ConsoleDeleteContainer", "Type %s, ID %u, Result Success", GlobalTypeToName(cbData->containerType), cbData->containerID);
	}
	else
	{
		dbContainerLog(LOG_CONTAINER, cbData->containerType, true, cbData->containerID, "ConsoleDeleteContainer", "Type %s, ID %u, Result Failure", GlobalTypeToName(cbData->containerType), cbData->containerID);
	}
	StructDestroy(parse_ContainerRef, cbData);
}

AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_NAME(ConsoleDeleteCharacter) ACMD_ACCESSLEVEL(9);
int ConsoleDeleteCharacter(CmdContext *context, U32 id)
{
	ContainerRef *ref;
	
	if(!id)
		return 0;

	ref = StructCreate(parse_ContainerRef);
	ref->containerType = GLOBALTYPE_ENTITYPLAYER;
	ref->containerID = id;
	objRequestContainerDelete(objCreateManagedReturnVal(ConsoleDeleteCharacter_CB, ref), GLOBALTYPE_ENTITYPLAYER, id, objServerType(), objServerID());
	return 1;
}

static void ConsoleDestroyCharacterAndDependents_CB(TransactionReturnVal *returnVal, ContainerRef *cbData)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		dbContainerLog(LOG_CONTAINER, cbData->containerType, true, cbData->containerID, "ConsoleDestroyCharacterAndDependents", "Type %s, ID %u, Result Success", GlobalTypeToName(cbData->containerType), cbData->containerID);
	}
	else
	{
		dbContainerLog(LOG_CONTAINER, cbData->containerType, true, cbData->containerID, "ConsoleDestroyCharacterAndDependents", "Type %s, ID %u, Result Failure", GlobalTypeToName(cbData->containerType), cbData->containerID);
	}
	StructDestroy(parse_ContainerRef, cbData);
}

AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_NAME(ConsoleDestroyCharacterAndDependents) ACMD_ACCESSLEVEL(9);
int ConsoleDestroyCharacterAndDependents(CmdContext *context, U32 id)
{
	ContainerRef *ref = StructCreate(parse_ContainerRef);
	ref->containerType = GLOBALTYPE_ENTITYPLAYER;
	ref->containerID = id;
	objRequestDestroyContainerAndDependents(objCreateManagedReturnVal(ConsoleDestroyCharacterAndDependents_CB, ref), GLOBALTYPE_ENTITYPLAYER, id, objServerType(), objServerID());
	return 1;
}

AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_NAME(ConsoleUndeleteCharacter) ACMD_ACCESSLEVEL(9);
int ConsoleUndeleteCharacter(CmdContext *context, U32 id)
{
	if(!id)
		return CR_CONTAINERNOTFOUND;
	return UndeleteCharacter(context, id);
}

// Permanently destroy a container and its dependents in the backing store
// Cleans up external dependencies like guilds
int DestroyContainerAndDependentsCB(TransactionCommand *command)
{
	Container *pObject = GetLockedContainerNoUnpack(command->objectType,command->objectID,command->transactionID, false);

	if (!pObject || !IsContainerOwnedByMe(pObject))
	{
		estrPrintf(command->returnString,"Can't call DeleteContainer on a server that doesn't own the container");
		return 0;
	}
	if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_PERSISTED)
	{
		if (!IsThisObjectDB() && !command->bShardExternalCommand)
		{
			estrPrintf(command->returnString,"You can only delete persisted containers on an ObjectDB");
			return 0;
		}
	}
	else if (GlobalTypeSchemaType(command->objectType) != SCHEMATYPE_TRANSACTED)
	{
		estrPrintf(command->returnString,"Only Transacted and Persisted schematypes can be deleted via transactions");
		return 0;
	}
	if (!command->bCheckingIfPossible)
	{
		if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_TRANSACTED)
		{
			estrPrintf(command->transReturnString,"offline %s %u %s %u",GlobalTypeToName(command->objectType),command->objectID,GlobalTypeToName(objServerType()),objServerID());
		}
		else
		{		
			estrPrintf(&command->pDatabaseReturnData->pString1,"dbDeleteContainer %s %u 1 1",GlobalTypeToName(command->objectType),command->objectID);
		}
	}
	return 1;
}

// Permanently destroy a container and its dependents in the backing store
// Cleans up external dependencies like guilds
AUTO_COMMAND ACMD_NAME(DestroyContainerAndDependents) ACMD_LIST(gServerTransactionCmdList);
int ParseDestroyContainerAndDependents(TransactionCommand *trCommand, char *containerTypeName,char *containerID)
{	
	trCommand->objectID = TransactionParseIntArgument(trCommand,containerID);
	trCommand->objectType = NameToGlobalType(TransactionParseStringArgument(trCommand,containerTypeName));

	trCommand->bLockRequired = true;
	trCommand->executeCB = DestroyContainerAndDependentsCB;
	
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockNoBackupCB;
	trCommand->commitCB = ContainerCacheCommitNoBackupCB;
	trCommand->revertCB = FullContainerRevertNoBackupCB;

	return 1;
}

// Permanently destroy a container and its dependents in the backing store
// Does not clean up external dependencies like guilds
int DestroyContainerAndDependentsWithoutCleanupCB(TransactionCommand *command)
{
	Container *pObject = GetLockedContainerNoUnpack(command->objectType,command->objectID,command->transactionID, false);

	if (!pObject || !IsContainerOwnedByMe(pObject))
	{
		estrPrintf(command->returnString,"Can't call DeleteContainer on a server that doesn't own the container");
		return 0;
	}
	if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_PERSISTED)
	{
		if (!IsThisObjectDB() && !command->bShardExternalCommand)
		{
			estrPrintf(command->returnString,"You can only delete persisted containers on an ObjectDB");
			return 0;
		}
	}
	else if (GlobalTypeSchemaType(command->objectType) != SCHEMATYPE_TRANSACTED)
	{
		estrPrintf(command->returnString,"Only Transacted and Persisted schematypes can be deleted via transactions");
		return 0;
	}
	if (!command->bCheckingIfPossible)
	{
		if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_TRANSACTED)
		{
			estrPrintf(command->transReturnString,"offline %s %u %s %u",GlobalTypeToName(command->objectType),command->objectID,GlobalTypeToName(objServerType()),objServerID());
		}
		else
		{		
			estrPrintf(&command->pDatabaseReturnData->pString1,"dbDeleteContainer %s %u 1 0",GlobalTypeToName(command->objectType),command->objectID);
		}
	}
	return 1;
}

// Permanently destroy a container and its dependents in the backing store
// Does not clean up external dependencies like guilds
AUTO_COMMAND ACMD_NAME(DestroyContainerAndDependentsWithoutCleanup) ACMD_LIST(gServerTransactionCmdList);
int ParseDestroyContainerAndDependentsForOffliningWithoutCleanup(TransactionCommand *trCommand, char *containerTypeName,char *containerID)
{	
	trCommand->objectID = TransactionParseIntArgument(trCommand,containerID);
	trCommand->objectType = NameToGlobalType(TransactionParseStringArgument(trCommand,containerTypeName));

	trCommand->bLockRequired = true;
	trCommand->executeCB = DestroyContainerAndDependentsWithoutCleanupCB;
	
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockNoBackupCB;
	trCommand->commitCB = ContainerCacheCommitNoBackupCB;
	trCommand->revertCB = FullContainerRevertNoBackupCB;

	return 1;
}

// Put a container in the deleted cache in the backing store
int DeleteContainerCB(TransactionCommand *command)
{
	Container *pObject = GetLockedContainerNoUnpack(command->objectType,command->objectID,command->transactionID, false);

	if (!pObject || !IsContainerOwnedByMe(pObject))
	{
		estrPrintf(command->returnString,"Can't call DeleteContainer on a server that doesn't own the container");
		return 0;
	}
	if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_PERSISTED)
	{
		if (!IsThisObjectDB() && !command->bShardExternalCommand)
		{
			estrPrintf(command->returnString,"You can only delete persisted containers on an ObjectDB");
			return 0;
		}
	}
	else if (GlobalTypeSchemaType(command->objectType) != SCHEMATYPE_TRANSACTED)
	{
		estrPrintf(command->returnString,"Only Transacted and Persisted schematypes can be deleted via transactions");
		return 0;
	}
	if (!command->bCheckingIfPossible)
	{
		if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_TRANSACTED)
		{
			estrPrintf(command->transReturnString,"offline %s %u %s %u",GlobalTypeToName(command->objectType),command->objectID,GlobalTypeToName(objServerType()),objServerID());
		}
		else
		{		
			estrPrintf(&command->pDatabaseReturnData->pString1,"dbDeleteContainer %s %u 0 1",GlobalTypeToName(command->objectType),command->objectID);
		}
	}
	return 1;
}

// Put a container in the deleted cache in the backing store
AUTO_COMMAND ACMD_NAME(DeleteContainer) ACMD_LIST(gServerTransactionCmdList);
int ParseDeleteContainer(TransactionCommand *trCommand, char *containerTypeName,char *containerID)
{	
	trCommand->objectID = TransactionParseIntArgument(trCommand,containerID);
	trCommand->objectType = NameToGlobalType(TransactionParseStringArgument(trCommand,containerTypeName));

	trCommand->bLockRequired = true;
	trCommand->executeCB = DeleteContainerCB;
	
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockNoBackupCB;
	trCommand->commitCB = ContainerCacheCommitNoBackupCB;
	trCommand->revertCB = FullContainerRevertNoBackupCB;

	return 1;
}

// Undelete a container in the backing store
int UndeleteContainerCB(TransactionCommand *command)
{
	if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_PERSISTED)
	{
		if (!IsThisObjectDB() && !command->bShardExternalCommand)
		{
			estrPrintf(command->returnString,"You can only undelete persisted containers on an ObjectDB");
			return 0;
		}
	}
	else if (GlobalTypeSchemaType(command->objectType) != SCHEMATYPE_TRANSACTED)
	{
		estrPrintf(command->returnString,"Only Transacted and Persisted schematypes can be undeleted via transactions");
		return 0;
	}
	if (!command->bCheckingIfPossible)
	{
		if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_TRANSACTED)
		{
			estrPrintf(command->transReturnString,"offline %s %u %s %u",GlobalTypeToName(command->objectType),command->objectID,GlobalTypeToName(objServerType()),objServerID());			
		}
		else
		{
			if(command->stringData && *command->stringData)
			{
				estrPrintf(&command->pDatabaseReturnData->pString1,"dbUndeleteContainerWithRename %s %u \"%s\"",GlobalTypeToName(command->objectType),command->objectID, command->stringData);
			}
			else
			{
				estrPrintf(&command->pDatabaseReturnData->pString1,"dbUndeleteContainer %s %u",GlobalTypeToName(command->objectType),command->objectID);
			}
		}
	}
	return 1;
}

// Undelete a container in the backing store
AUTO_COMMAND ACMD_NAME(UndeleteContainer) ACMD_LIST(gServerTransactionCmdList);
int ParseUndeleteContainer(TransactionCommand *trCommand, char *containerTypeName,char *containerID)
{	
	trCommand->objectID = TransactionParseIntArgument(trCommand,containerID);
	trCommand->objectType = NameToGlobalType(TransactionParseStringArgument(trCommand,containerTypeName));

	trCommand->bLockRequired = true;
	trCommand->executeCB = UndeleteContainerCB;
	
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockNoBackupCB;
	trCommand->commitCB = ContainerCacheCommitNoBackupCB;
	trCommand->revertCB = FullContainerRevertNoBackupCB;

	return 1;
}

// Undelete a container on this server
// Perform a rename to avoid name collisions
AUTO_COMMAND ACMD_NAME(UndeleteContainerWithRename) ACMD_LIST(gServerTransactionCmdList);
int ParseUndeleteContainerWithRename(TransactionCommand *trCommand, char *containerTypeName,char *containerID, char *namestr)
{	
	trCommand->objectID = TransactionParseIntArgument(trCommand,containerID);
	trCommand->objectType = NameToGlobalType(TransactionParseStringArgument(trCommand,containerTypeName));
	trCommand->stringData = strdup(namestr);

	trCommand->bLockRequired = true;
	trCommand->executeCB = UndeleteContainerCB;
	
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockNoBackupCB;
	trCommand->commitCB = ContainerCacheCommitNoBackupCB;
	trCommand->revertCB = FullContainerRevertNoBackupCB;

	return 1;
}

// Write a container to offline.hogg
int WriteContainerToOfflineHoggCB(TransactionCommand *command)
{
	if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_PERSISTED)
	{
		if (!IsThisObjectDB() && !command->bShardExternalCommand)
		{
			estrPrintf(command->returnString,"You can only offline persisted containers on an ObjectDB");
			return 0;
		}
	}
	else if (GlobalTypeSchemaType(command->objectType) != SCHEMATYPE_TRANSACTED)
	{
		estrPrintf(command->returnString,"Only Transacted and Persisted schematypes can be offlined via transactions");
		return 0;
	}
	if (!command->bCheckingIfPossible)
	{
		if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_TRANSACTED)
		{
			estrPrintf(command->transReturnString,"offline %s %u %s %u",GlobalTypeToName(command->objectType),command->objectID,GlobalTypeToName(objServerType()),objServerID());			
		}
		else
		{
			estrPrintf(&command->pDatabaseReturnData->pString1,"dbWriteContainerToOfflineHogg %s %u",GlobalTypeToName(command->objectType),command->objectID);
		}
	}
	return 1;
}

// Write a container to the offline.hogg
AUTO_COMMAND ACMD_NAME(WriteContainerToOfflineHogg) ACMD_LIST(gServerTransactionCmdList);
int ParseWriteContainerToOfflineHogg(TransactionCommand *trCommand, char *containerTypeName,char *containerID)
{	
	trCommand->objectID = TransactionParseIntArgument(trCommand,containerID);
	trCommand->objectType = NameToGlobalType(TransactionParseStringArgument(trCommand,containerTypeName));

	trCommand->bLockRequired = true;
	trCommand->executeCB = WriteContainerToOfflineHoggCB;
	
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockNoBackupCB;
	trCommand->commitCB = FullContainerCommitNoBackupCB;
	trCommand->revertCB = FullContainerRevertNoBackupCB;

	return 1;
}

// Write a container to the offline.hogg
int dbWriteContainerToOfflineHogg_CB(ContainerUpdateInfo *info, bool getLock)
{
	if (!info->containerType)
	{
		return 0;
	}

	if(!gDatabaseConfig.bEnableOfflining)
	{
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	if (!objDoesContainerExist(info->containerType, info->containerID))
	{
		return 0;
	}

	verbose_printf("Offlining container %s[%d]\n",GlobalTypeToName(info->containerType),info->containerID);

	dbContainerLog(LOG_LOGIN, info->containerType, info->containerID, getLock, "OffliningCharacter", "Type %s, ID %u, Result Success", GlobalTypeToName(info->containerType), info->containerID);
		
	objWriteContainerToOfflineHog(info->containerType, info->containerID);

	PERFINFO_AUTO_STOP();
	return 1;
}

AUTO_COMMAND ACMD_LIST(gDBUpdateCmdList);
int dbWriteContainerToOfflineHogg(char *containerTypeName, U32 containerID)
{
	GlobalType containerType = NameToGlobalType(containerTypeName);
	ContainerUpdateInfo info = {0};
	if(!containerType)
		return 0;
	info.containerID = containerID;
	info.containerType = containerType;
	if(GenericDatabaseThreadIsActive() && !OnBackgroundGenericDatabaseThread())
	{
		QueueWriteContainerToOfflineHoggOnGenericDatabaseThreads(&info);
		return 1;
	}

	DecrementContainersToOffline();
	return dbWriteContainerToOfflineHogg_CB(&info, true);
}

// Remove a restored container from the offline.hogg
int RemoveContainerFromOfflineHoggCB(TransactionCommand *command)
{
	if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_PERSISTED)
	{
		if (!IsThisObjectDB() && !command->bShardExternalCommand)
		{
			estrPrintf(command->returnString,"You can only offline persisted containers on an ObjectDB");
			return 0;
		}
	}
	else if (GlobalTypeSchemaType(command->objectType) != SCHEMATYPE_TRANSACTED)
	{
		estrPrintf(command->returnString,"Only Transacted and Persisted schematypes can be offlined via transactions");
		return 0;
	}
	if (!command->bCheckingIfPossible)
	{
		if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_TRANSACTED)
		{
			estrPrintf(command->transReturnString,"offline %s %u %s %u",GlobalTypeToName(command->objectType),command->objectID,GlobalTypeToName(objServerType()),objServerID());			
		}
		else
		{
			estrPrintf(&command->pDatabaseReturnData->pString1,"dbRemoveContainerFromOfflineHogg %s %u",GlobalTypeToName(command->objectType),command->objectID);
		}
	}
	return 1;
}

// Remove a restored container from the offline.hogg
AUTO_COMMAND ACMD_NAME(RemoveContainerFromOfflineHogg) ACMD_LIST(gServerTransactionCmdList);
int ParseRemoveContainerFromOfflineHogg(TransactionCommand *trCommand, char *containerTypeName,char *containerID)
{	
	trCommand->objectID = TransactionParseIntArgument(trCommand,containerID);
	trCommand->objectType = NameToGlobalType(TransactionParseStringArgument(trCommand,containerTypeName));

	trCommand->bLockRequired = true;
	trCommand->executeCB = RemoveContainerFromOfflineHoggCB;
	
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockNoBackupCB;
	trCommand->commitCB = FullContainerCommitNoBackupCB;
	trCommand->revertCB = FullContainerRevertNoBackupCB;

	return 1;
}

int dbRemoveContainerFromOfflineHogg_CB(ContainerUpdateInfo *info, bool getLock)
{
	Container *con = NULL;
	if (!info->containerType)
	{
		return 0;
	}
	PERFINFO_AUTO_START_FUNC();

	verbose_printf("Removing container %s[%d] from Offline hogg\n",GlobalTypeToName(info->containerType),info->containerID);

	dbContainerLog(LOG_LOGIN, info->containerType, info->containerID, getLock, "UnOffliningCharacter", "Type %s, ID %u, Result Success", GlobalTypeToName(info->containerType), info->containerID);
		
	objRemoveContainerFromOfflineHog(info->containerType, info->containerID);

	PERFINFO_AUTO_STOP();
	return 1;
}

AUTO_COMMAND ACMD_LIST(gDBUpdateCmdList);
int dbRemoveContainerFromOfflineHogg(char *containerTypeName, U32 containerID)
{
	GlobalType containerType = NameToGlobalType(containerTypeName);
	ContainerUpdateInfo info = {0};
	if(!containerType)
		return 0;
	info.containerID = containerID;
	info.containerType = containerType;
	if(GenericDatabaseThreadIsActive() && !OnBackgroundGenericDatabaseThread())
	{
		QueueDBRemoveContainerFromOfflineHoggOnGenericDatabaseThreads(&info);
		return 1;
	}

	DecrementOfflineContainersToCleanup();
	return dbRemoveContainerFromOfflineHogg_CB(&info, true);
}

// Record an offlined character so we can restore it later
int AddOfflineEntityPlayerToAccountStubCB(TransactionCommand *command)
{
	if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_PERSISTED)
	{
		if (!IsThisObjectDB() && !command->bShardExternalCommand)
		{
			estrPrintf(command->returnString,"You can only offline persisted containers on an ObjectDB");
			return 0;
		}
	}
	else if (GlobalTypeSchemaType(command->objectType) != SCHEMATYPE_TRANSACTED)
	{
		estrPrintf(command->returnString,"Only Transacted and Persisted schematypes can be offlined via transactions");
		return 0;
	}
	if (!command->bCheckingIfPossible)
	{
		if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_TRANSACTED)
		{
			estrPrintf(command->transReturnString,"offline %s %u %s %u",GlobalTypeToName(command->objectType),command->objectID,GlobalTypeToName(objServerType()),objServerID());			
		}
		else
		{
			if (!command->stringData)
				return 0;

			estrPrintf(&command->pDatabaseReturnData->pString1,"dbAddOfflineEntityPlayerToAccountStub %s %u \"%s\"",GlobalTypeToName(command->objectType),command->objectID, command->stringData);
		}
	}
	return 1;
}

// Record an offlined character so we can restore it later
AUTO_COMMAND ACMD_NAME(AddOfflineEntityPlayerToAccountStub) ACMD_LIST(gServerTransactionCmdList);
int ParseAddOfflineEntityPlayerToAccountStub(TransactionCommand *trCommand, char *accountID, char *headerData)
{	
	trCommand->objectID = TransactionParseIntArgument(trCommand,accountID);
	trCommand->objectType = GLOBALTYPE_ACCOUNTSTUB;
	trCommand->stringData = strdup(headerData);

	trCommand->bLockRequired = true;
	trCommand->executeCB = AddOfflineEntityPlayerToAccountStubCB;
	
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockCB;
	trCommand->commitCB = FullContainerCommitCB;
	trCommand->revertCB = FullContainerRevertCB;

	return 1;
}

// Record an offlined character so we can restore it later
int dbAddOfflineEntityPlayerToAccountStub_CB(ContainerAccountStubInfo *info)
{
	Container *con = NULL;

	if (!info->containerType)
	{
		SAFE_FREE(info->characterStubString);
		return 0;
	}
	PERFINFO_AUTO_START("dbAddOfflineEntityPlayerToAccountStub",1);

	con = objGetContainer(info->containerType, info->accountID);

	if (!con || !con->containerData)
	{
		SAFE_FREE(info->characterStubString);
		return 0;
	}
	
	if(PushOfflineEntityPlayerToAccountStub(CONTAINER_NOCONST(AccountStub, con->containerData), info->characterStubString))
	{
		IncrementTotalOfflineCharacters();
	}

	objContainerMarkModified(con);
	SAFE_FREE(info->characterStubString);

	PERFINFO_AUTO_STOP();
	return 1;
}

AUTO_COMMAND ACMD_LIST(gDBUpdateCmdList);
int dbAddOfflineEntityPlayerToAccountStub(char *containerTypeName, U32 accountID, char *characterStubString)
{
	GlobalType containerType = NameToGlobalType(containerTypeName);
	ContainerAccountStubInfo info = {0};
	if(!containerType)
		return 0;
	info.accountID = accountID;
	info.containerType = containerType;
	info.characterStubString = strdup(characterStubString);
	if(GenericDatabaseThreadIsActive() && !OnBackgroundGenericDatabaseThread())
	{
		QueueDBAddOfflineEntityPlayerToAccountStubOnGenericDatabaseThreads(&info);
		return 1;
	}

	DecrementOutstandingAccountStubOperations();
	return dbAddOfflineEntityPlayerToAccountStub_CB(&info);
}

// Mark a player restored so we don't restore twice
int MarkEntityPlayerRestoredInAccountStubCB(TransactionCommand *command)
{
	if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_PERSISTED)
	{
		if (!IsThisObjectDB() && !command->bShardExternalCommand)
		{
			estrPrintf(command->returnString,"You can only offline persisted containers on an ObjectDB");
			return 0;
		}
	}
	else if (GlobalTypeSchemaType(command->objectType) != SCHEMATYPE_TRANSACTED)
	{
		estrPrintf(command->returnString,"Only Transacted and Persisted schematypes can be offlined via transactions");
		return 0;
	}
	if (!command->bCheckingIfPossible)
	{
		if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_TRANSACTED)
		{
			estrPrintf(command->transReturnString,"offline %s %u %s %u",GlobalTypeToName(command->objectType),command->objectID,GlobalTypeToName(objServerType()),objServerID());			
		}
		else
		{
			if (!command->stringData)
				return 0;

			estrPrintf(&command->pDatabaseReturnData->pString1,"dbMarkEntityPlayerRestoredInAccountStub %s %u %s",GlobalTypeToName(command->objectType),command->objectID, command->stringData);
		}
	}
	return 1;
}

// Mark a player restored so we don't restore twice
AUTO_COMMAND ACMD_NAME(MarkEntityPlayerRestoredInAccountStub) ACMD_LIST(gServerTransactionCmdList);
int ParseMarkEntityPlayerRestoredInAccountStub(TransactionCommand *trCommand, char *accountID,char *containerID)
{	
	trCommand->objectID = TransactionParseIntArgument(trCommand,accountID);
	trCommand->objectType = GLOBALTYPE_ACCOUNTSTUB;
	trCommand->stringData = strdup(containerID);

	trCommand->bLockRequired = true;
	trCommand->executeCB = MarkEntityPlayerRestoredInAccountStubCB;
	
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockCB;
	trCommand->commitCB = FullContainerCommitCB;
	trCommand->revertCB = FullContainerRevertCB;

	return 1;
}

// Mark a player restored so we don't restore twice
int dbMarkEntityPlayerRestoredInAccountStub_CB(ContainerMiniAccountStubInfo *info)
{
	Container *con = NULL;
	if (!info->containerType)
	{
		return 0;
	}
	PERFINFO_AUTO_START("dbMarkEntityPlayerRestoredInAccountStub",1);
	
	con = objGetContainer(info->containerType, info->accountID);

	if (!con || !con->containerData)
	{
		return 0;
	}

	if(MarkEntityPlayerRestoredInAccountStub(CONTAINER_NOCONST(AccountStub, con->containerData), info->containerID))
		DecrementTotalOfflineCharacters();

	objContainerMarkModified(con);

	PERFINFO_AUTO_STOP();
	return 1;
}

AUTO_COMMAND ACMD_LIST(gDBUpdateCmdList);
int dbMarkEntityPlayerRestoredInAccountStub(char *containerTypeName, U32 accountID, U32 containerID)
{
	GlobalType containerType = NameToGlobalType(containerTypeName);
	ContainerMiniAccountStubInfo info = {0};
	if(!containerType)
		return 0;
	info.accountID = accountID;
	info.containerType = containerType;
	info.containerID = containerID;
	if(GenericDatabaseThreadIsActive() && !OnBackgroundGenericDatabaseThread())
	{
		QueueDBMarkEntityPlayerRestoredInAccountStubOnGenericDatabaseThreads(&info);
		return 1;
	}

	return dbMarkEntityPlayerRestoredInAccountStub_CB(&info);
}

// Clean up account stub of previously restore EntityPlayers
int RemoveOfflineEntityPlayerFromAccountStubCB(TransactionCommand *command)
{
	if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_PERSISTED)
	{
		if (!IsThisObjectDB() && !command->bShardExternalCommand)
		{
			estrPrintf(command->returnString,"You can only offline persisted containers on an ObjectDB");
			return 0;
		}
	}
	else if (GlobalTypeSchemaType(command->objectType) != SCHEMATYPE_TRANSACTED)
	{
		estrPrintf(command->returnString,"Only Transacted and Persisted schematypes can be offlined via transactions");
		return 0;
	}
	if (!command->bCheckingIfPossible)
	{
		if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_TRANSACTED)
		{
			estrPrintf(command->transReturnString,"offline %s %u %s %u",GlobalTypeToName(command->objectType),command->objectID,GlobalTypeToName(objServerType()),objServerID());			
		}
		else
		{
			if (!command->stringData)
				return 0;

			estrPrintf(&command->pDatabaseReturnData->pString1,"dbRemoveOfflineEntityPlayerFromAccountStub %s %u %s",GlobalTypeToName(command->objectType),command->objectID, command->stringData);
		}
	}
	return 1;
}

// Clean up account stub of previously restore EntityPlayers
AUTO_COMMAND ACMD_NAME(RemoveOfflineEntityPlayerFromAccountStub) ACMD_LIST(gServerTransactionCmdList);
int ParseRemoveOfflineEntityPlayerFromAccountStub(TransactionCommand *trCommand, char *accountID,char *containerID)
{	
	trCommand->objectID = TransactionParseIntArgument(trCommand,accountID);
	trCommand->objectType = GLOBALTYPE_ACCOUNTSTUB;
	trCommand->stringData = strdup(containerID);

	trCommand->bLockRequired = true;
	trCommand->executeCB = RemoveOfflineEntityPlayerFromAccountStubCB;
	
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockCB;
	trCommand->commitCB = FullContainerCommitCB;
	trCommand->revertCB = FullContainerRevertCB;

	return 1;
}

// Clean up account stub of previously restore EntityPlayers
int dbRemoveOfflineEntityPlayerFromAccountStub_CB(ContainerMiniAccountStubInfo *info)
{
	Container *con = NULL;
	if (!info->containerType)
	{
		return 0;
	}
	PERFINFO_AUTO_START("dbRemoveOfflineEntityPlayerFromAccountStub",1);

	con = objGetContainer(info->containerType, info->accountID);

	if (!con || !con->containerData)
	{
		return 0;
	}

	RemoveOfflineEntityPlayerFromAccountStub(CONTAINER_NOCONST(AccountStub, con->containerData), info->containerID);
	objContainerMarkModified(con);

	PERFINFO_AUTO_STOP();
	return 1;
}

AUTO_COMMAND ACMD_LIST(gDBUpdateCmdList);
int dbRemoveOfflineEntityPlayerFromAccountStub(char *containerTypeName, U32 accountID, U32 containerID)
{
	GlobalType containerType = NameToGlobalType(containerTypeName);
	ContainerMiniAccountStubInfo info = {0};
	if(!containerType)
		return 0;
	info.accountID = accountID;
	info.containerType = containerType;
	info.containerID = containerID;
	if(GenericDatabaseThreadIsActive() && !OnBackgroundGenericDatabaseThread())
	{
		QueueDBRemoveOfflineEntityPlayerFromAccountStubOnGenericDatabaseThreads(&info);
		return 1;
	}

	DecrementOutstandingAccountStubOperations();
	return dbRemoveOfflineEntityPlayerFromAccountStub_CB(&info);
}

// Record an offlined character so we can restore it later
int AddOfflineAccountWideContainerToAccountStubCB(TransactionCommand *command)
{
	if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_PERSISTED)
	{
		if (!IsThisObjectDB() && !command->bShardExternalCommand)
		{
			estrPrintf(command->returnString,"You can only offline persisted containers on an ObjectDB");
			return 0;
		}
	}
	else if (GlobalTypeSchemaType(command->objectType) != SCHEMATYPE_TRANSACTED)
	{
		estrPrintf(command->returnString,"Only Transacted and Persisted schematypes can be offlined via transactions");
		return 0;
	}
	if (!command->bCheckingIfPossible)
	{
		if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_TRANSACTED)
		{
			estrPrintf(command->transReturnString,"offline %s %u %s %u",GlobalTypeToName(command->objectType),command->objectID,GlobalTypeToName(objServerType()),objServerID());			
		}
		else
		{
			if (!command->stringData)
				return 0;

			estrPrintf(&command->pDatabaseReturnData->pString1,"dbAddOfflineAccountWideContainerToAccountStub %s %u %s",GlobalTypeToName(command->objectType),command->objectID, command->stringData);
		}
	}
	return 1;
}

// Record an offlined account wide container so we can restore it later
AUTO_COMMAND ACMD_NAME(AddOfflineAccountWideContainerToAccountStub) ACMD_LIST(gServerTransactionCmdList);
int ParseAddOfflineAccountWideContainerToAccountStub(TransactionCommand *trCommand, char *accountID, char *accountWideContainerType)
{	
	trCommand->objectID = TransactionParseIntArgument(trCommand,accountID);
	trCommand->objectType = GLOBALTYPE_ACCOUNTSTUB;
	trCommand->stringData = strdup(accountWideContainerType);

	trCommand->bLockRequired = true;
	trCommand->executeCB = AddOfflineAccountWideContainerToAccountStubCB;
	
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockCB;
	trCommand->commitCB = FullContainerCommitCB;
	trCommand->revertCB = FullContainerRevertCB;

	return 1;
}

// Record an offlined character so we can restore it later
int dbAddOfflineAccountWideContainerToAccountStub_CB(AccountWideContainerAccountStubInfo *info)
{
	Container *con = NULL;
	if (!info->containerType)
	{
		return 0;
	}
	PERFINFO_AUTO_START("dbAddOfflineAccountWideContainerToAccountStub",1);

	con = objGetContainer(info->containerType, info->accountID);

	if (!con || !con->containerData)
	{
		return 0;
	}
	
	PushOfflineAccountWideContainerToAccountStub(CONTAINER_NOCONST(AccountStub, con->containerData), info->accountWideContainerType);

	objContainerMarkModified(con);

	PERFINFO_AUTO_STOP();
	return 1;
}

AUTO_COMMAND ACMD_LIST(gDBUpdateCmdList);
int dbAddOfflineAccountWideContainerToAccountStub(char *containerTypeName, U32 accountID, U32 accountWideContainerType)
{
	GlobalType containerType = NameToGlobalType(containerTypeName);
	AccountWideContainerAccountStubInfo info = {0};
	if(!containerType)
		return 0;
	info.accountID = accountID;
	info.containerType = containerType;
	info.accountWideContainerType = accountWideContainerType;
	if(GenericDatabaseThreadIsActive() && !OnBackgroundGenericDatabaseThread())
	{
		QueueDBAddOfflineAccountWideContainerToAccountStubOnGenericDatabaseThreads(&info);
		return 1;
	}

	return dbAddOfflineAccountWideContainerToAccountStub_CB(&info);
}

// Mark a player restored so we don't restore twice
int MarkAccountWideContainerRestoredInAccountStubCB(TransactionCommand *command)
{
	if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_PERSISTED)
	{
		if (!IsThisObjectDB() && !command->bShardExternalCommand)
		{
			estrPrintf(command->returnString,"You can only offline persisted containers on an ObjectDB");
			return 0;
		}
	}
	else if (GlobalTypeSchemaType(command->objectType) != SCHEMATYPE_TRANSACTED)
	{
		estrPrintf(command->returnString,"Only Transacted and Persisted schematypes can be offlined via transactions");
		return 0;
	}
	if (!command->bCheckingIfPossible)
	{
		if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_TRANSACTED)
		{
			estrPrintf(command->transReturnString,"offline %s %u %s %u",GlobalTypeToName(command->objectType),command->objectID,GlobalTypeToName(objServerType()),objServerID());			
		}
		else
		{
			if (!command->stringData)
				return 0;

			estrPrintf(&command->pDatabaseReturnData->pString1,"dbMarkAccountWideContainerRestoredInAccountStub %s %u %s",GlobalTypeToName(command->objectType),command->objectID, command->stringData);
		}
	}
	return 1;
}

// Mark a player restored so we don't restore twice
AUTO_COMMAND ACMD_NAME(MarkAccountWideContainerRestoredInAccountStub) ACMD_LIST(gServerTransactionCmdList);
int ParseMarkAccountWideContainerRestoredInAccountStub(TransactionCommand *trCommand, char *accountID,char *accountWideContainerType)
{	
	trCommand->objectID = TransactionParseIntArgument(trCommand,accountID);
	trCommand->objectType = GLOBALTYPE_ACCOUNTSTUB;
	trCommand->stringData = strdup(accountWideContainerType);

	trCommand->bLockRequired = true;
	trCommand->executeCB = MarkAccountWideContainerRestoredInAccountStubCB;
	
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockCB;
	trCommand->commitCB = FullContainerCommitCB;
	trCommand->revertCB = FullContainerRevertCB;

	return 1;
}

// Mark a container restored so we don't restore twice
int dbMarkAccountWideContainerRestoredInAccountStub_CB(AccountWideContainerAccountStubInfo *info)
{
	Container *con = NULL;
	if (!info->containerType)
	{
		return 0;
	}
	PERFINFO_AUTO_START("dbMarkAccountWideContainerRestoredInAccountStub",1);
	
	con = objGetContainer(info->containerType, info->accountID);

	if (!con || !con->containerData)
	{
		return 0;
	}

	MarkAccountWideContainerRestoredInAccountStub(CONTAINER_NOCONST(AccountStub, con->containerData), info->accountWideContainerType);

	objContainerMarkModified(con);

	PERFINFO_AUTO_STOP();
	return 1;
}

AUTO_COMMAND ACMD_LIST(gDBUpdateCmdList);
int dbMarkAccountWideContainerRestoredInAccountStub(char *containerTypeName, U32 accountID, U32 accountWideContainerType)
{
	GlobalType containerType = NameToGlobalType(containerTypeName);
	AccountWideContainerAccountStubInfo info = {0};
	if(!containerType)
		return 0;
	info.accountID = accountID;
	info.containerType = containerType;
	info.accountWideContainerType = accountWideContainerType;
	if(GenericDatabaseThreadIsActive() && !OnBackgroundGenericDatabaseThread())
	{
		QueueDBMarkAccountWideContainerRestoredInAccountStubOnGenericDatabaseThreads(&info);
		return 1;
	}

	return dbMarkAccountWideContainerRestoredInAccountStub_CB(&info);
}

// Clean up account stub of previously restored AccountWideContainers
int RemoveOfflineAccountWideContainerFromAccountStubCB(TransactionCommand *command)
{
	if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_PERSISTED)
	{
		if (!IsThisObjectDB() && !command->bShardExternalCommand)
		{
			estrPrintf(command->returnString,"You can only offline persisted containers on an ObjectDB");
			return 0;
		}
	}
	else if (GlobalTypeSchemaType(command->objectType) != SCHEMATYPE_TRANSACTED)
	{
		estrPrintf(command->returnString,"Only Transacted and Persisted schematypes can be offlined via transactions");
		return 0;
	}
	if (!command->bCheckingIfPossible)
	{
		if (GlobalTypeSchemaType(command->objectType) == SCHEMATYPE_TRANSACTED)
		{
			estrPrintf(command->transReturnString,"offline %s %u %s %u",GlobalTypeToName(command->objectType),command->objectID,GlobalTypeToName(objServerType()),objServerID());			
		}
		else
		{
			if (!command->stringData)
				return 0;

			estrPrintf(&command->pDatabaseReturnData->pString1,"dbRemoveOfflineAccountWideContainerFromAccountStub %s %u %s",GlobalTypeToName(command->objectType),command->objectID, command->stringData);
		}
	}
	return 1;
}

// Clean up account stub of previously restore EntityPlayers
AUTO_COMMAND ACMD_NAME(RemoveOfflineAccountWideContainerFromAccountStub) ACMD_LIST(gServerTransactionCmdList);
int ParseRemoveOfflineAccountWideContainerFromAccountStub(TransactionCommand *trCommand, char *accountID,char *containerType)
{	
	trCommand->objectID = TransactionParseIntArgument(trCommand,accountID);
	trCommand->objectType = GLOBALTYPE_ACCOUNTSTUB;
	trCommand->stringData = strdup(containerType);

	trCommand->bLockRequired = true;
	trCommand->executeCB = RemoveOfflineAccountWideContainerFromAccountStubCB;
	
	trCommand->checkLockCB = CheckFullContainerLockCB;
	trCommand->lockCB = FullContainerLockCB;
	trCommand->commitCB = FullContainerCommitCB;
	trCommand->revertCB = FullContainerRevertCB;

	return 1;
}

// Clean up account stub of previously restored AccountWideContainers
int dbRemoveOfflineAccountWideContainerFromAccountStub_CB(AccountWideContainerAccountStubInfo *info)
{
	Container *con = NULL;
	if (!info->containerType)
	{
		return 0;
	}
	PERFINFO_AUTO_START("dbRemoveOfflineAccountWideContainerFromAccountStub",1);

	con = objGetContainer(info->containerType, info->accountID);

	if (!con || !con->containerData)
	{
		return 0;
	}

	RemoveOfflineAccountWideContainerFromAccountStub(CONTAINER_NOCONST(AccountStub, con->containerData), info->accountWideContainerType);
	objContainerMarkModified(con);

	PERFINFO_AUTO_STOP();
	return 1;
}

AUTO_COMMAND ACMD_LIST(gDBUpdateCmdList);
int dbRemoveOfflineAccountWideContainerFromAccountStub(char *containerTypeName, U32 accountID, U32 accountWideContainerType)
{
	GlobalType containerType = NameToGlobalType(containerTypeName);
	AccountWideContainerAccountStubInfo info = {0};
	if(!containerType)
		return 0;
	info.accountID = accountID;
	info.containerType = containerType;
	info.accountWideContainerType = accountWideContainerType;
	if(GenericDatabaseThreadIsActive() && !OnBackgroundGenericDatabaseThread())
	{
		QueueDBRemoveOfflineAccountWideContainerFromAccountStubOnGenericDatabaseThreads(&info);
		return 1;
	}

	return dbRemoveOfflineAccountWideContainerFromAccountStub_CB(&info);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CONTROLLER);
void InformObjectDBOfServerDeath (GlobalType eType, ContainerID iContainerID)
{
	dbUnsubscribeFromAllContainers(eType, iContainerID);
}

//for a given container type (ie, EntityPlayer) prints out a report of total container counts and RAM usage. VERY VERY SLOW DONT USE ON LIVE
AUTO_COMMAND ACMD_CATEGORY(ObjectDB, debug);
char *GetObjectCountAndRAMUsageReport_WARNING_VERY_SLOW_DONT_USE_ON_LIVE_DB(char *pContainerTypeName)
{
	GlobalType eType = NameToGlobalType(pContainerTypeName);
	
	ContainerIterator iter;
	Container *con;

	int iContainerCount = 0;
	int iCompressedContainerCount = 0;
	int iUncompressedContainerCount = 0;
	S64 iUncompressedRAMUse = 0;
	S64 iSubscribeRAMUse = 0;
	S64 iCompressedRAMUse = 0;
	char *pTempString1 = NULL;
	char *pTempString2 = NULL;
	static char *spRetVal = NULL;

	estrClear(&spRetVal);

	if (!eType)
	{
		return "Unknown container type";
	}

	FlushGenericDatabaseThreads(false);

	objInitContainerIteratorFromType(eType, &iter);
	while (con = objGetNextContainerFromIterator(&iter))
	{
		iContainerCount++;

		if (con->fileData)
		{
			iCompressedContainerCount++;
			iCompressedRAMUse += con->bytesCompressed;
		}
		else if (con->containerData)
		{
			iUncompressedContainerCount++;
			iUncompressedRAMUse += StructGetMemoryUsage(con->containerSchema->classParse, con->containerData, true);

			{
				void *pOtherStruct = StructCreateVoid(con->containerSchema->classParse);
				StructCopyVoid(con->containerSchema->classParse, con->containerData, pOtherStruct, 0, TOK_PERSIST | TOK_SUBSCRIBE, 0);
				iSubscribeRAMUse += StructGetMemoryUsage(con->containerSchema->classParse, pOtherStruct, true);
				StructDestroyVoid(con->containerSchema->classParse, pOtherStruct);
			}
		}
	}
	objClearContainerIterator(&iter);

	estrConcatf(&spRetVal, "%d total containers, %d compressed, %d uncompressed\n",
		iContainerCount, iCompressedContainerCount, iUncompressedContainerCount);

	if (iCompressedContainerCount)
	{
		estrMakePrettyBytesString(&pTempString1, iCompressedRAMUse);
		estrMakePrettyBytesString(&pTempString2, iCompressedRAMUse / iCompressedContainerCount);
		estrConcatf(&spRetVal, "Compressed containers use %s, average of %s per container\n", pTempString1, pTempString2);
	}

	if (iUncompressedContainerCount)
	{
		estrMakePrettyBytesString(&pTempString1, iUncompressedRAMUse);
		estrMakePrettyBytesString(&pTempString2, iUncompressedRAMUse / iUncompressedContainerCount);
		estrConcatf(&spRetVal, "Uncompressed containers use %s, average of %s per container\n", pTempString1, pTempString2);

		estrMakePrettyBytesString(&pTempString1, iSubscribeRAMUse);
		estrMakePrettyBytesString(&pTempString2, iSubscribeRAMUse / iUncompressedContainerCount);
		estrConcatf(&spRetVal, "Subscribe copies of uncompressed containers would use %s, average of %s per container\n", pTempString1, pTempString2);
	}

	estrDestroy(&pTempString1);
	estrDestroy(&pTempString2);

	return spRetVal;
}

//#include "AutoGen/LoginCommon_h_ast.c"
#include "AutoGen/dbRemoteCommands_c_ast.c"
