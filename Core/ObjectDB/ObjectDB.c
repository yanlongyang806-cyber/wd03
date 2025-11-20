/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GlobalComm.h"
#include "objContainerIO.h"
#include "Expression.h"
#include "structinternals.h"
#include "genericlist.h"
#include "AccountStub.h"
#include "dbOfflining.h"
#include "objDCC.h"
#include "dbCharacterChoice.h"
#include "dbGenericDatabaseThreads.h"
#include "UtilitiesLib.h"
#include "mathutil.h"
#include "LoginCommon.h"
#include "dbLogin2.h"

#include "objIndex.h"

#include "ObjectDB.h"
#include "dbReplication.h"
#include "dbSubscribe.h"
#include "dbContainerRestore.h"
#include "dbQuery.h"
#include "dbLocalTransactionManager.h"
#include "GlobalStateMachine.h"
#include "ServerLib.h"
#include "file.h"
#include "AutoGen/controller_autogen_remotefuncs.h"
#include "Autogen/ObjectDB_autogen_remotefuncs.h"
#include "Autogen/ServerLib_autogen_slowfuncs.h"
#include "sysutil.h"
#include "winutil.h"
#include "SharedMemory.h"
#include "FolderCache.h"
#include "gimmeDLLWrapper.h"
#include "MemoryPool.h"
#include "MemReport.h"
#include "hoglib.h"
#include "AutoGen/ObjectDB_h_ast.h"
#include "StringCache.h"
#include "svrGlobalInfo.h"
#include "FloatAverager.h"
#include "DatabaseTest/DatabaseTest.h"
#include "ControllerLink.h"
#include "ScratchStack.h"
#include "logging.h"
#include "netipfilter.h"
#include "GenericFileServing.h"
#include "ContinuousBuilderSupport.h"
#include "playerBooter.h"
#include "PlayerBooter_h_ast.h"
#include "Tokenstore.h"
#include "tokenstore_inline.h"
#include "Alerts.h"
#include "crypt.h"
#include "logcomm.h"
#include "Queue.h"
#include "hogutil.h"
#include "textparserUtils.h"

#include "AutoGen/objContainerIO_h_ast.h"
#include "GlobalTypes_h_ast.h"

//For intershard transfer
#include "cmdparse.h"
#include "WorkerThread.h"
#include "XboxThreads.h"

// This flag should be used in case of emergency downtime. The idea is to skip unnecessary, time-intensive tasks
// when performing emergency maintenance. 
// Tasks being skipped:
//		Offlining
//		Offline Cleanup
static U32 gEmergencyRestart = false;
AUTO_CMD_INT(gEmergencyRestart, EmergencyRestart) ACMD_CMDLINE;

bool ObjectDBDoingEmergencyRestart(void)
{
	return gEmergencyRestart;
}

void GenerateEmergencyRestartAlert(void)
{
	if(ObjectDBDoingEmergencyRestart())
	{
		ErrorOrCriticalAlert("OBJECTDB.EMERGENCYRESTART", "The ObjectDB has been started using the EmergencyRestart flag. This flag should be removed before the next normal maintenance.");
	}
}

static U32 gMergerLaunchFailuresBeforeAlert_Normal = 2;
AUTO_CMD_INT(gMergerLaunchFailuresBeforeAlert_Normal, MergerLaunchFailuresBeforeAlert_Normal) ACMD_CMDLINE;

static U32 gMergerLaunchFailuresBeforeAlert_Defrag = 10;
AUTO_CMD_INT(gMergerLaunchFailuresBeforeAlert_Defrag, MergerLaunchFailuresBeforeAlert_Defrag) ACMD_CMDLINE;

U32 gCurrentDatabaseVersion = 0;
DatabaseState gDatabaseState;
DatabaseConfig gDatabaseConfig = {0};

ObjectIndex *gAccountID_idx;
ObjectIndex *gAccountDisplayName_idx;
ObjectIndex *gAccountAccountName_idx;
ObjectIndex *gCreatedTime_idx;
ObjectIndex *gLevel_idx;
ObjectIndex *gLastPlayedTime_idx;
ObjectIndex *gFixupVersion_idx;
ObjectIndex *gVirtualShardId_idx;
ObjectIndex *gAccountIDDeleted_idx;
ObjectIndex *gAccountAccountNameDeleted_idx;

//__CATEGORY ObjectDB Settings
//The maximum size of the EntityPlayer string pool
U64 gConupEstrPoolCap = (U64)10 * (U64)(1024 * 1024 * 1024); //Bigger than a U32
AUTO_CMD_INT(gConupEstrPoolCap, ConupEstrPoolCap) ACMD_AUTO_SETTING(ObjectDB, OBJECTDB, CLONEOBJECTDB, CLONEOFCLONE);

//The minimum amount of memory to free from ConupEstrPool each tick if it is over the cap
U64 gConupEstrPoolRemoveSize = 100 * 1024 * 1024;
AUTO_CMD_INT(gConupEstrPoolRemoveSize, ConupEstrPoolRemoveSize) ACMD_AUTO_SETTING(ObjectDB, OBJECTDB, CLONEOBJECTDB, CLONEOFCLONE);

//Guild indices
ObjectIndex *gGuildCreatedTime_idx;

static WorkerThread *wtIntershardTransfer;
static NetComm *intershardTransferComm;

extern int diskLoadingMode;

// Sets the timeout for Intershard Transfer NetLinks
U32 gIntershardTransferNetLinkTimeout = 10;
AUTO_CMD_INT(gIntershardTransferNetLinkTimeout, IntershardTransferNetLinkTimeout) ACMD_CMDLINE;

// Sets the timeout for Intershard Transfer NetLinks
U32 gMaximumSentCharacters = 10;
AUTO_CMD_INT(gMaximumSentCharacters, MaximumSentCharacters) ACMD_CMDLINE;

// Choose whether to overwrite the local GameAccountData with an incoming transfer
U32 gOverwriteAccountDataOnReceive = 0;
AUTO_CMD_INT(gOverwriteAccountDataOnReceive, OverwriteAccountDataOnReceive) ACMD_CMDLINE;

// Default scratch stack sizes for 64 and 32 bit ObjectDB processes.
#if defined(_M_X64)
#define OBJECTDB_MAIN_THREAD_SCRATCH_STACK_SIZE 64*1024*1024
#define OBJECTDB_DEFAULT_PER_THREAD_SCRATCH_STACK_SIZE 2*1024*1024
#else
#define OBJECTDB_MAIN_THREAD_SCRATCH_STACK_SIZE 2*1024*1024
#define OBJECTDB_DEFAULT_PER_THREAD_SCRATCH_STACK_SIZE 256*1024
#endif

static enum
{
	IST_QUEUED = 1,
	IST_CONNECTING = 2,
	IST_SENT = 3,
	IST_SUCCESS = 4,
	//Destination Failures
	IST_CONNECTTIMEOUT = -1,
	IST_PRODUCTMISMATCH = -2,
	IST_UNTRUSTEDSOURCE = -3,
	IST_PARSERFAILURE = -4,
	IST_LOADFAILURE = -5,
	IST_TRANSACTIONFAILURE = -6,

	//Source Failures
	IST_UNOWNEDCHARACTER = -9,
	IST_UNKNOWNCHARACTER = -10,
	IST_INVALIDCHARACTER = -11,
	IST_QUEUEFULL = -12,
	IST_ALREADYAREQUEST = -13,
	IST_UNKNOWNTRANSFER = -14,
};

typedef struct OutgoingIntershardTransferNetLink
{
	NetLink *link;
	U32 iCurrentSentCharacters;
	U32 iLastUsed;
	U32 iLastIncomingMessageTimeStamp;
} OutgoingIntershardTransferNetLink;

typedef struct OutgoingIntershardTransferRequest
{
	ContainerID sourceID;
	ContainerID destinationID;
	const char *pDestinationName;

	U32 iRequestTime;

	U32 eTransferStatus;

	bool bDone;
} OutgoingIntershardTransferRequest;

typedef struct IncomingIntershardTransferRequest
{
	bool doReturn;
	CmdSlowReturnForServerMonitorInfo sri;
	U32 linkID;

	//parameters
	char *server;
	char *characterData;
	void *ee;
	
	//return val; values less than 1 are errors.
	ContainerID destinationID;
	ContainerID sourceID;

	U32 requestTime;
} IncomingIntershardTransferRequest;

Queue gOutgoingIntershardTransferQueue;

StashTable gAllOutgoingIntershardTransferRequests;

StashTable gOutgoingIntershardTransferNetLinks;

static void IntershardTransferResponse_CB(Packet *pkt, int cmd, NetLink* link, void *user_data)
{
	OutgoingIntershardTransferRequest *pRequest;
	ContainerID sourceID = pktGetU32(pkt);
	U32 requestTime = pktGetU32(pkt);
	int returnCode = (int)pktGetU32(pkt);

	PERFINFO_AUTO_START_FUNC();

	if(stashIntFindPointer(gAllOutgoingIntershardTransferRequests, sourceID, &pRequest))
	{
		OutgoingIntershardTransferNetLink *pLinkStruct = NULL;
		if(stashFindPointer(gOutgoingIntershardTransferNetLinks, pRequest->pDestinationName, &pLinkStruct))
		{
			pLinkStruct->iLastIncomingMessageTimeStamp = requestTime;
		}

		if(pRequest->iRequestTime == requestTime)
		{
			if(pRequest->eTransferStatus == IST_SENT && pLinkStruct)
			{
				--pLinkStruct->iCurrentSentCharacters;
			}

			pRequest->eTransferStatus = returnCode;
			pRequest->bDone = true;

			if(pRequest->eTransferStatus == IST_SUCCESS)
			{
				pRequest->destinationID = pktGetU32(pkt);
				servLog(LOG_CONTAINER, "TransferSucceeded", "CharacterID %u Destination \"%s\" DestinationID %u", pRequest->sourceID, pRequest->pDestinationName, pRequest->destinationID);
			}
			else
			{
				servLog(LOG_CONTAINER, "TransferFailed", "CharacterID %u Destination \"%s\" Reason \"Failure on destination\" Code %d", pRequest->sourceID, pRequest->pDestinationName, pRequest->eTransferStatus);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

void UpdateOutgoingIntershardTransfers(TimedCallback *callback, F32 timeSinceLastCallback, UserData userdata)
{
	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("Queue check", 1);
	if(!qIsEmpty(gOutgoingIntershardTransferQueue))
	{
		// Check for NetLink
		OutgoingIntershardTransferNetLink *pLinkStruct = NULL;
		OutgoingIntershardTransferRequest *pRequest = (OutgoingIntershardTransferRequest*)qPeek(gOutgoingIntershardTransferQueue);

		const char *pDestinationName = pRequest->pDestinationName;

		if(stashFindPointer(gOutgoingIntershardTransferNetLinks, pDestinationName, &pLinkStruct))
		{
			PERFINFO_AUTO_START("Link found", 1);
			if(linkConnected(pLinkStruct->link) && !linkDisconnected(pLinkStruct->link) && pLinkStruct->iCurrentSentCharacters < gMaximumSentCharacters)
			{
				// Serialize and send entity
				Container *character = objGetContainerEx(GLOBALTYPE_ENTITYPLAYER, pRequest->sourceID, true, false, true);
				qDequeue(gOutgoingIntershardTransferQueue);
				log_printf(LOG_CONTAINER, "Attempting to send EntityPlayer[%u] to ObjectDB at %s", pRequest->sourceID, pDestinationName);
				if (!character)
				{
					pRequest->eTransferStatus = IST_UNKNOWNCHARACTER;
					servLog(LOG_CONTAINER, "TransferFailed", "CharacterID %u Destination \"%s\" Reason \"Non-existent character\"", pRequest->sourceID, pDestinationName);
					pRequest->bDone = true;
				}

				if (character)
				{
					int queue_success = 0;
					char *pEntityData = NULL;

					//This has to be done on the main thread because we need to block transactions while we serialize all the dependent containers.
					// If character->containerData is populated, we call dbSerializeEntityForExport which will always unlock character
					// This is why we only need to have an unlock in the else.
					PERFINFO_AUTO_START("Serialize", 1);
					if (character->containerData && dbSerializeEntityForExport(&pEntityData, &character))
					{
						// Send
						Packet *pak;

						PERFINFO_AUTO_STOP();
						pak = pktCreate(pLinkStruct->link, TO_OBJECTDB_TRANSFER_CHARACTER);
						pktSendU32(pak, pRequest->sourceID);
						pktSendU32(pak, pRequest->iRequestTime);
						pktSendString(pak, GetShortProductName());
						pktSendString(pak, pEntityData);
						pktSendString(pak, GetShardNameFromShardInfoString());
						pktSendString(pak, GetShardCategoryFromShardInfoString());
						pktSend(&pak);
						pLinkStruct->iLastUsed = timeSecondsSince2000();
						pRequest->eTransferStatus = IST_SENT;
						++pLinkStruct->iCurrentSentCharacters;
					}
					else
					{
						if (character)
						{
							objUnlockContainer(&character);
						}

						PERFINFO_AUTO_STOP();
						ErrorOrAlert("OBJECTDB_TRANSFER_FAILED", "Could not serialize character %u", pRequest->sourceID);
						servLog(LOG_CONTAINER, "TransferFailed", "CharacterID %u Destination \"%s\" Reason \"Could not serialize character\"", pRequest->sourceID, pDestinationName);
						pRequest->eTransferStatus = IST_INVALIDCHARACTER;
						pRequest->bDone = true;
					}

					estrDestroy(&pEntityData);
				}
			}
			else if(timeSecondsSince2000() > pLinkStruct->iLastUsed + gIntershardTransferNetLinkTimeout)
			{
				PERFINFO_AUTO_START("Attempt timeout", 1);
				qDequeue(gOutgoingIntershardTransferQueue);
				pRequest->eTransferStatus = IST_CONNECTTIMEOUT;
				pRequest->bDone = true;
				servLog(LOG_CONTAINER, "TransferFailed", "CharacterID %u Destination \"%s\" Reason \"Connection to destination timed out\"", pRequest->sourceID, pDestinationName);
				PERFINFO_AUTO_STOP();
			}
			PERFINFO_AUTO_STOP();
		}
		else
		{
			PERFINFO_AUTO_START("Link not found", 1);
			pLinkStruct = (OutgoingIntershardTransferNetLink*)calloc(sizeof(OutgoingIntershardTransferNetLink), 1);
			pLinkStruct->iLastUsed = timeSecondsSince2000();

			// initialize NetLink
			pLinkStruct->link = commConnect(commDefault(), LINKTYPE_SHARD_NONCRITICAL_10MEG, LINK_FORCE_FLUSH, pRequest->pDestinationName, DEFAULT_OBJECTDB_TRANSFER_PORT, IntershardTransferResponse_CB, NULL, NULL, 0);
			stashAddPointer(gOutgoingIntershardTransferNetLinks, pDestinationName, pLinkStruct, false);
			pRequest->eTransferStatus = IST_CONNECTING;
			PERFINFO_AUTO_STOP();
		}
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("NetLink check", 1);
	if(gIntershardTransferNetLinkTimeout)
	{
		// Manage NetLinks
		U32 iTime = timeSecondsSince2000();
		StashTableIterator iter;
		StashElement ppElem;

		stashGetIterator(gOutgoingIntershardTransferNetLinks, &iter);
		while(stashGetNextElement(&iter, &ppElem))
		{
			OutgoingIntershardTransferNetLink *pLinkStruct = (OutgoingIntershardTransferNetLink*)stashElementGetPointer(ppElem);
			if(iTime > pLinkStruct->iLastUsed + gIntershardTransferNetLinkTimeout)
			{
				PERFINFO_AUTO_START("Link timeout", 1);
				if(pLinkStruct->link)
					linkFlushAndClose(&pLinkStruct->link, "Link was idle");

				free(pLinkStruct);
				stashRemovePointer(gOutgoingIntershardTransferNetLinks, stashElementGetStringKey(ppElem), &pLinkStruct);
				PERFINFO_AUTO_STOP();
			}
		}
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP();
}

void *entityPlayerGetDestroyTimeUserData(Container *con)
{
	if(con && con->header)
		return (void*)(intptr_t)con->header->level;
	else
		return NULL;
}

U32 entityPlayerGetDestroyTime(const DeletedContainerQueueEntry *entry)
{
	if(entry->userData)
	{
		U32 level = (int)(intptr_t)entry->userData;
		if(level <= gDatabaseConfig.iLowLevelThreshold)
			return entry->iDeletedTime + gDatabaseConfig.iLowLevelDCCThreshold;
	}

	return entry->iDeletedTime + GetCachedDeleteTimeout();
}

U32 gNamedFetchMaxTimeMS = 20;	//default to 20ms.
int gStringCacheSize = 0;
int gDebugStringCacheSize = 0;

XMLRPCNamedFetchWrapper **namedfetches;

char gObjectDBConfigFileName[CRYPTIC_MAX_PATH] = "server/ObjectDBConfig.txt";

extern ParseTable parse_NamedPathQueriesAndResults[];
#define TYPE_parse_NamedPathQueriesAndResults NamedPathQueriesAndResults

//if true, then only minimal information about characters is returned hwen character choices are requested
bool sbMinimalCharacterChoices = false;
AUTO_CMD_INT(sbMinimalCharacterChoices, MinimalCharacterChoices) ACMD_COMMANDLINE;

bool dbMinimalCharacterChoices(void)
{
	return sbMinimalCharacterChoices && gDatabaseConfig.bUseHeaders;
}

AUTO_CMD_INT(gStringCacheSize, FastStringCache) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDebugStringCacheSize, DebugFastStringCache) ACMD_COMMANDLINE;

AUTO_RUN_FIRST;
void DBInitialize(void)
{
	char	buf[100] = {0};
	int		default_size = 1024*1024;

	ParseCommandOutOfCommandLine("DebugFastStringCache",buf);
	gDebugStringCacheSize = atoi(buf);
	if (gDebugStringCacheSize)
	{
		stringCacheSetInitialSize(gDebugStringCacheSize);
		stringCacheEnableLocklessRead();
	}
	else
	{
		ParseCommandOutOfCommandLine("FastStringCache",buf);
		gStringCacheSize = atoi(buf);
		if (gStringCacheSize)
		{
			stringCacheSetInitialSize(10 * default_size * gStringCacheSize);
			stringCacheEnableLocklessRead();
		}
		else
		{
			stringCacheSetInitialSize(default_size);
		}
	}

    // Set default per-thread scratch stack size for other threads.
    ScratchStackSetDefaultPerThreadSize(OBJECTDB_DEFAULT_PER_THREAD_SCRATCH_STACK_SIZE);

    // Set per-thread scratch stack size for the main thread.
	ScratchStackSetThreadSize(OBJECTDB_MAIN_THREAD_SCRATCH_STACK_SIZE);

	SetSchemaVersion(DATABASE_VERSION);	
	SetLateCreateIndexedEArrays(true);
	if(isProductionMode())
		logSetMsgQueueSize(256*1024*1024);
}

AUTO_RUN;
void DBInitializeSourceOptions(void)
{
	objSetSkipPeriodicContainerSaves(true);
}

AUTO_RUN;
void DBTypeInitialize(void)
{
	if (IsThisMasterObjectDB())
	{
		if (gDatabaseState.databaseType == DBTYPE_INVALID)
		{
			gDatabaseState.databaseType = DBTYPE_STANDALONE;
		}
	}
	else if (IsThisCloneObjectDB())
	{
		if (gDatabaseState.databaseType == DBTYPE_INVALID)
		{
			gDatabaseState.databaseType = DBTYPE_CLONE;
		}
	}
	else if (GetAppGlobalType() == GLOBALTYPE_TESTGAMESERVER)
	{
		//nothing
	}
	else
	{
		assertmsg(0,"ObjectDB was passed an invalid container type on the command line!");
	}

	StructInit(parse_DatabaseConfig, &gDatabaseConfig);
}

// We want to launch this as a master DB
AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void DBMaster(int i)
{
	if (i)
	{
		assertmsg(IsThisMasterObjectDB(),"You can't run a clone server in master mode!");
		gDatabaseState.databaseType = DBTYPE_MASTER;
	}
	else
	{
		if (IsThisCloneObjectDB())
		{
			gDatabaseState.databaseType = DBTYPE_CLONE;
		}
		else
		{		
			gDatabaseState.databaseType = DBTYPE_STANDALONE;
		}
	}
}

AUTO_CMD_STRING(gDatabaseState.corruptionScan, CorruptionScan) ACMD_COMMANDLINE;
AUTO_CMD_STRING(gDatabaseState.corruptionExpr, Expr) ACMD_COMMANDLINE;


AUTO_CMD_STRING(gDatabaseState.dumpSQLData, DumpSQLData) ACMD_COMMANDLINE;
AUTO_CMD_STRING(gDatabaseState.dumpType, DumpType) ACMD_COMMANDLINE;
AUTO_CMD_STRING(gDatabaseState.dumpOutPath, DumpOut) ACMD_COMMANDLINE;

AUTO_CMD_STRING(gDatabaseState.cloneHostName,CloneHostName) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseState.cloneListenPort,CloneListenPort) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseState.cloneConnectPort,CloneConnectPort) ACMD_COMMANDLINE;
AUTO_CMD_STRING(gDatabaseConfig.CLDatabaseDir,DatabaseDir) ACMD_COMMANDLINE;
AUTO_CMD_STRING(gDatabaseConfig.CLCloneDir,CloneDir) ACMD_COMMANDLINE;
AUTO_CMD_STRING(gDatabaseConfig.CLExportDir,ExportDir) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.iIncrementalInterval,IncrementalInterval) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.iIncrementalHoursToKeep, IncrementalHoursToKeep) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.iSnapshotInterval, SnapshotInterval) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.iSnapshotBackupInterval, SnapshotBackupInterval) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseState.bNoMerger, NoMerger) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.bShowSnapshots, ShowSnapshots) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.bNoHogs,NoHogs) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseState.bNoSaving,NoSaving) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseState.bCreateSnapshot,CreateSnapshot) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseState.bDefragAfterMerger,DefragAfterMerger) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.bAppendSnapshot,AppendSnapshot) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.iTargetDefragWindowStart,TargetDefragWindowStart) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.iTargetDefragWindowDuration,TargetDefragWindowDuration) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.iDaysBetweenDefrags,DaysBetweenDefrags) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.bFastSnapshot,FastSnapshot) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.bBackupSnapshot,BackupSnapshot) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseState.bCloneCreateSnapshot,CloneSnapshot) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.bCloneConnectToMaster,CloneConnectToMaster) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.bEnableOfflining,EnableOfflining) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.bDisableNonCriticalIndexing,DisableNonCriticalIndexing) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.bDisablePlaytimeReportingBuckets,DisablePlaytimeReportingBuckets) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.iOfflineThreshold,OfflineThreshold) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.iLowLevelOfflineThreshold,LowLevelOfflineThreshold) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.iLowLevelThreshold,LowLevelThreshold) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.iOfflineThrottle,OfflineThrottle) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.iOfflineFrameThrottle,OfflineFrameThrottle) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.iLowLevelDCCThreshold,LowLevelDCCThreshold) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.iLowLevelStaleThresholdHours,LowLevelStaleThresholdHours) ACMD_COMMANDLINE;
AUTO_CMD_STRING(gDatabaseState.dumpWebData, DumpWebData) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseState.bIsolatedMode,IsolatedMode) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseState.bReplayLogsToClone,ReplayMode) ACMD_COMMANDLINE;

AUTO_CMD_INT(gDatabaseState.mergerThreads,MergerThreads) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseState.loadThreads,LoadThreads) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.bLazyLoad,LazyLoad) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.bUseHeaders,UseHeaders) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.bSaveAllHeaders,SaveAllHeaders) ACMD_COMMANDLINE;
// command name can't have "waitfordebugger" in it because of cmd line parsing
AUTO_CMD_INT(gDatabaseConfig.bMergerWaitForDebugger,MergerDebug) ACMD_COMMANDLINE;

AUTO_CMD_INT(gDatabaseState.iStartupWait, StartupWait) ACMD_COMMANDLINE;
AUTO_CMD_INT(gDatabaseConfig.iDebugLagOnUpdate, LagOnDBUpdate) ACMD_COMMANDLINE;

AUTO_CMD_INT(gNamedFetchMaxTimeMS, NamedFetchMaxTimeMS) ACMD_COMMANDLINE;

AUTO_COMMAND ACMD_COMMANDLINE;
void RunAfterStartup(char *pchCommand)
{
	eaPush(&gDatabaseState.ppCommandsToRun, strdup(pchCommand));
}

void dbUpdateTitle(void)
{
	static char *tempTitleString;
	char fullString[1024];


	GSM_PutFullStateStackIntoEString(&tempTitleString);

	sprintf(fullString, "%s %s", GlobalTypeToName(GetAppGlobalType()),tempTitleString);

	setConsoleTitle(fullString);

}


void ServerMonFindEntity_Return(TransactionReturnVal *pReturnVal, CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo)
{
	char *pFullRetString = NULL;
	char *owner = NULL;
	estrStackCreate(&owner);
	estrStackCreate(&pFullRetString);
	
	if (pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
	{
		RemoteCommandCheck_dbContainerOwner(pReturnVal, &owner);
		estrPrintf(&pFullRetString, "<a href=\"/viewxpath?xpath=%s.custom\">%s</a>",owner, owner);
	}
	else
	{
		estrPrintf(&pFullRetString, "Could not find entity.");
	}

	DoSlowCmdReturn(pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS, pFullRetString, pSlowReturnInfo);

	free(pSlowReturnInfo);
	estrDestroy(&pFullRetString);
	estrDestroy(&owner);
}


AUTO_COMMAND;
void ServerMonFindEntity(CmdContext *pContext, char *pEntityTypeName, ContainerID conid)
{
	GlobalType eType = NameToGlobalType(pEntityTypeName);
	CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo;

	if (eType == GLOBALTYPE_NONE)
	{
		return;
	}

	if (!objLocalManager())
	{
		return;
	}

	pContext->slowReturnInfo.bDoingSlowReturn = true;
	pSlowReturnInfo = malloc(sizeof(CmdSlowReturnForServerMonitorInfo));
	memcpy(pSlowReturnInfo, &pContext->slowReturnInfo, sizeof(CmdSlowReturnForServerMonitorInfo));

	RemoteCommand_dbContainerOwner(objCreateManagedReturnVal(ServerMonFindEntity_Return, pSlowReturnInfo),
		GLOBALTYPE_OBJECTDB, 0, eType, conid);
}

AUTO_COMMAND;
void dbForceSnapshot()
{
	dbCreateSnapshotLocal(0, false, "", false, false);
}

//This will detach a snapshot merging process.
AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_ACCESSLEVEL(9);
char* dbCreateSnapshotLocal(S32 delaySeconds, bool verbose, char *dumpwebdata, bool hideMerger, bool defrag)
{
	static char string[200];
	static U32 poke_count = 0;
	char *estr = NULL;

	if (gDatabaseState.bNoMerger || gDBTestState.testingEnabled)
	{
		return "ObjectDB merger has been disabled by the NoMerger commandline flag.\n";
	}

	if (g_isContinuousBuilder)
	{
		return "ObjectDB merger should not run on continuous builders.\n";
	}

	if (gDatabaseConfig.bNoHogs)
	{
		return "ObjectDB can only make snapshots in hog mode.\n";
	}

	gDatabaseState.lastSnapshotTime = timeSecondsSince2000();

	if (gDatabaseState.lastMergerPID)
	{
		char exename[MAX_PATH];
		getFileNameNoExtNoDirs_s(exename, MAX_PATH, getExecutableName());
		if (system_pokename(gDatabaseState.lastMergerPID, exename))
		{
			poke_count++;
			sprintf(string, "ObjectDB merger [pid:%d] is still running! Please wait a few minutes before snapshotting again. If this message repeats, escalate.\n", gDatabaseState.lastMergerPID);
			if (poke_count > gMergerLaunchFailuresBeforeAlert_Normal && !gDatabaseState.lastMergerWasDefrag)
				AssertOrAlert("OBJECTDB.MERGER_STILL_RUNNING", "%s", string);
			else if (poke_count > gMergerLaunchFailuresBeforeAlert_Defrag && gDatabaseState.lastMergerWasDefrag)
				AssertOrAlert("OBJECTDB.DEFRAG_MERGER_STILL_RUNNING", "%s", string);
			else
				printf("%s", string);
			return string;
		}
	}
	poke_count = 0;
	
	estrStackCreate(&estr);
	estrPrintf(&estr, "%s", getExecutableName());
	if (gDatabaseState.databaseType == DBTYPE_CLONE) estrConcatf(&estr, " -CloneSnapshot");
	else estrConcatf(&estr, " -CreateSnapshot");

	if (defrag)
	{
		U32 iTime = timeSecondsSince2000();
		gDatabaseState.lastDefragDay = GetDefragDay();
		gDatabaseState.lastMergerWasDefrag = true;
		estrAppend2(&estr, " -DefragAfterMerger");
		servLog(LOG_MISC, "DefragMergerLaunch", "Time %d", iTime);
	}
	else
	{
		gDatabaseState.lastMergerWasDefrag = false;
	}

	if (gDatabaseConfig.iDaysBetweenDefrags)
	{
		estrAppend2(&estr, " -AppendSnapshot");
	}

	if (delaySeconds > 0)
		estrConcatf(&estr, " -StartupWait %d", delaySeconds*1000);

	if (delaySeconds < 0 || gDatabaseConfig.bMergerWaitForDebugger) 
		estrConcatf(&estr, " -WaitForDebugger 1");

	if (verbose) estrConcatf(&estr, " -Verbose");

	if (gDatabaseState.mergerThreads)
		estrConcatf(&estr, " -LoadThreads %d", gDatabaseState.mergerThreads);

	if (diskLoadingMode)
		estrConcatf(&estr, " -diskLoadingMode");
	
	estrConcatf(&estr, " -LogServer %s", gServerLibState.logServerHost);
	estrConcatf(&estr, " -ControllerHost %s", gServerLibState.controllerHost);
	estrConcatf(&estr, " -SetProductName %s %s", GetProductName(), GetShortProductName());
	estrConcatf(&estr, " -SetErrorTracker %s", getErrorTracker());
	if (dumpwebdata && dumpwebdata[0] && strlen(dumpwebdata) > 1) estrConcatf(&estr, " -DumpWebData %s", dumpwebdata);

	if (gDatabaseConfig.bFastSnapshot) 
	{
		estrConcatf(&estr, " -FastSnapshot 1");
	}
	else
	{
		estrConcatf(&estr, " -FastSnapshot 0");
	}
	if (gDatabaseConfig.iSnapshotBackupInterval)
	{
		U32 time = timeSecondsSince2000();
		if (time > gDatabaseState.lastSnapshotBackupTime + gDatabaseConfig.iSnapshotBackupInterval * 60)
		{
			estrConcatf(&estr, " -BackupSnapshot 1");
			gDatabaseState.lastSnapshotBackupTime = time;
		}
		else
		{
			estrConcatf(&estr, " -BackupSnapshot 0");
		}	
	}
	else if (gDatabaseConfig.bBackupSnapshot) 
	{
		estrConcatf(&estr, " -BackupSnapshot 1");
	}
	else
	{
		estrConcatf(&estr, " -BackupSnapshot 0");
	}
	gDatabaseState.lastMergerPID = system_detach(estr,0,hideMerger);
	if(isDevelopmentMode())
	{
		HANDLE h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, gDatabaseState.lastMergerPID);
		if(h)
		{
			SetPriorityClass(h, IDLE_PRIORITY_CLASS);
			CloseHandle(h);
		}
	}

	estrDestroy(&estr);

	sprintf(string, "ObjectDB Snapshot Merger detached. [pid:%d]\n", gDatabaseState.lastMergerPID);

	printf("%s", string);
	return string;
}

AUTO_COMMAND_REMOTE;
void dbCreateSnasphot(U32 delaySeconds, bool verbose, char *dumpwebdata)
{
	dbCreateSnapshotLocal(delaySeconds, verbose, dumpwebdata, false, false);
}

void InitObjectDBStatus(void);
void InitObjectDBDebugInfo(void);

//extern void ParseOperationsTest(void);

void ObjectDBInit(int argc, char **argv)
{
	int i;
	setDefaultAssertMode();
	//setAssertMode(ASSERTMODE_FULLDUMP | ASSERTMODE_DATEDMINIDUMPS | ASSERTMODE_ZIPPED | ASSERTMODE_USECRYPTICERROR);

	memCheckInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'O', 0x8080ff);

	FolderCacheChooseModeNoPigsInDevelopment();

	preloadDLLs(0);

	if (!fileIsUsingDevData())
	{
		gimmeDLLDisable(1);
	}

	// no shared memory for this, different layout than others
	sharedMemorySetMode(SMM_DISABLED);
	mpEnablePoolCompaction(false);

	srand((unsigned int)time(NULL));
	consoleUpSize(110,128);

	logSetDir(GlobalTypeToName(GetAppGlobalType()));

	serverLibStartup(argc, argv);

	enableContainerStoreLocking(GenericDatabaseThreadIsEnabled());
	enableObjectLocks(GenericDatabaseThreadIsEnabled());
	EnableTLSCompiledObjectPaths(GenericDatabaseThreadIsEnabled()); // If EnableTLSCompiledObjectPaths is passed on the command-line it overrides this.

	InitObjectDBStatus();
	InitObjectDBDebugInfo();
	GenerateEmergencyRestartAlert();

//	ParseOperationsTest();

}

int OVERRIDE_LATELINK_comm_commandqueue_size(void)
{
	return (1<<20);
}

static int dbHandleControllerMsg(Packet *pak,int cmd, NetLink *link,void *userdata)
{
	switch(cmd)
	{
		case FROM_CONTROLLER_IAMDYING:
			printf("Controller died... Cleaning up\n");
			GSM_SwitchToState_Complex("/" DBSTATE_CLEANUP);
			break;
	}
	return 1;
}

void dbGracefulShutdownCB(void)
{
	printf("trans server died... Cleaning up\n");
	GSM_SwitchToState_Complex("/" DBSTATE_CLEANUP);
}

AUTO_FIXUPFUNC;
TextParserResult fixupDatabaseConfig(DatabaseConfig* dbconf, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		{
			if (dbconf->IOConfig) dbconf->IOConfig = NULL;
		}
	}
	return true;
}

int totalFields;

void dbAddHeaderEntryToTableEx(StashTable table, const char *path, const char *crcpath, int val)
{
	totalFields++;

	if(path)
		stashAddInt(table, path, val, false);
	cryptAdler32Update_IgnoreCase(crcpath, (int)strlen(crcpath));
}

#define dbAddHeaderEntryToTable(table, path, val) dbAddHeaderEntryToTableEx(table, path, path, val)

void dbAddExtraHeaderToCRC(ObjectIndexHeaderField field)
{
	const char *tempString;
	assert(OBJ_HEADER_EXTRA_DATA_1 <= field && field <= OBJ_HEADER_EXTRA_DATA_5);
	tempString = objIndexGetExtraDataPath(field);
	if(!tempString)
		tempString = "(NULL)";
	cryptAdler32Update_IgnoreCase(tempString, (int)strlen(tempString));
}

#define HEADER_VERSION 3 
StashTable dbCreateEntityHeaderTable()
{
	StashTable headerTable = stashTableCreateWithStringKeys(8, StashDeepCopyKeys);
	U32 headerVersion = HEADER_VERSION;

	cryptAdler32Init();

	cryptAdler32Update_AutoEndian((U8*)&headerVersion, sizeof(headerVersion));

	dbAddHeaderEntryToTable(headerTable, ".myContainerId", OBJ_HEADER_CONTAINERID);
	dbAddHeaderEntryToTable(headerTable, ".pPlayer.accountId", OBJ_HEADER_ACCOUNTID);
	dbAddHeaderEntryToTable(headerTable, ".pPlayer.iCreatedTime", OBJ_HEADER_CREATEDTIME);
	dbAddHeaderEntryToTable(headerTable, ".pInventoryV2.ppLiteBags[Numeric].ppIndexedLiteSlots[Level].count", OBJ_HEADER_LEVEL);
	dbAddHeaderEntryToTable(headerTable, ".pSaved.fixupVersion", OBJ_HEADER_FIXUP_VERSION);
	dbAddHeaderEntryToTable(headerTable, ".pPlayer.iLastPlayedTime", OBJ_HEADER_LAST_PLAYED_TIME);
	dbAddHeaderEntryToTable(headerTable, ".pPlayer.publicAccountName", OBJ_HEADER_PUB_ACCOUNTNAME);
	dbAddHeaderEntryToTable(headerTable, ".pPlayer.privateAccountName", OBJ_HEADER_PRIV_ACCOUNTNAME);
	dbAddHeaderEntryToTable(headerTable, ".pSaved.savedName", OBJ_HEADER_SAVEDNAME);
	dbAddHeaderEntryToTable(headerTable, ".pPlayer.iVirtualShardID", OBJ_HEADER_VIRTUAL_SHARD_ID);
	// The headerCRC is based on the existence of the 5 extra data fields. There is a separate crc for the paths in the configuration file.
	dbAddHeaderEntryToTableEx(headerTable, objIndexGetExtraDataPath(OBJ_HEADER_EXTRA_DATA_1), "ExtraData1", OBJ_HEADER_EXTRA_DATA_1);
	dbAddHeaderEntryToTableEx(headerTable, objIndexGetExtraDataPath(OBJ_HEADER_EXTRA_DATA_2), "ExtraData2", OBJ_HEADER_EXTRA_DATA_2);
	dbAddHeaderEntryToTableEx(headerTable, objIndexGetExtraDataPath(OBJ_HEADER_EXTRA_DATA_3), "ExtraData3", OBJ_HEADER_EXTRA_DATA_3);
	dbAddHeaderEntryToTableEx(headerTable, objIndexGetExtraDataPath(OBJ_HEADER_EXTRA_DATA_4), "ExtraData4", OBJ_HEADER_EXTRA_DATA_4);
	dbAddHeaderEntryToTableEx(headerTable, objIndexGetExtraDataPath(OBJ_HEADER_EXTRA_DATA_5), "ExtraData5", OBJ_HEADER_EXTRA_DATA_5);

	devassertmsg(totalFields == OBJ_HEADER_COUNT, "All header fields must be in the header table");

	headerCRC = cryptAdler32Final();

	cryptAdler32Init();
	dbAddExtraHeaderToCRC(OBJ_HEADER_EXTRA_DATA_1);
	dbAddExtraHeaderToCRC(OBJ_HEADER_EXTRA_DATA_2);
	dbAddExtraHeaderToCRC(OBJ_HEADER_EXTRA_DATA_3);
	dbAddExtraHeaderToCRC(OBJ_HEADER_EXTRA_DATA_4);
	dbAddExtraHeaderToCRC(OBJ_HEADER_EXTRA_DATA_5);
	extraDataCRC = cryptAdler32Final();

	return headerTable;
}

void objFixupContainerConfig()
{
	if(gDatabaseConfig.CloneConfig && (IsThisCloneObjectDB() || gDatabaseState.bCloneCreateSnapshot))
	{
		DatabaseConfig *pCloneConfig = gDatabaseConfig.CloneConfig;
		gDatabaseConfig.CloneConfig = NULL;
		
		StructCopyAll(parse_DatabaseConfig, pCloneConfig, &gDatabaseConfig);
		StructDestroy(parse_DatabaseConfig, pCloneConfig);
	}

	if(gDatabaseConfig.MasterConfig && (GetAppGlobalType() == GLOBALTYPE_OBJECTDB || gDatabaseState.bCreateSnapshot))
	{
		DatabaseConfig *pMasterConfig = gDatabaseConfig.MasterConfig;
		gDatabaseConfig.MasterConfig = NULL;
		
		StructCopyAll(parse_DatabaseConfig, pMasterConfig, &gDatabaseConfig);
		StructDestroy(parse_DatabaseConfig, pMasterConfig);
	}
}

// every index that entityPlayer_GetStaleCutoffIndex returns must be populated by entityPlayer_GenerateExtraStaleCutoffs
void entityPlayer_GenerateExtraStaleCutoffs(U32 now, U32 multiplier, U32 cutoff, U32* extracutoffs)
{
	extracutoffs[0] = now - gDatabaseConfig.iLowLevelStaleThresholdHours * multiplier;
}

int entityPlayer_GetStaleCutoffIndex(Container *con)
{
	if(!con->header)
		return -1;

	if(con->header->level <= gDatabaseConfig.iLowLevelThreshold)
	{
		return 0;
	}

	return -1;
}

bool entityPlayer_NotStale(Container *con, U32 cutoff)
{
	if(con->header)
	{
		if(con->header->lastPlayedTime > cutoff || con->header->createdTime > cutoff)
			return true;
	}

	return false;
}

void UpdateDeletedPlayerStaleness(Container *con, void *containerData);
void UpdateUndeletedPlayerStaleness(Container *con, void *containerData);

static int sLogPacketTrackersSeconds = HOURS(1);
AUTO_CMD_INT(sLogPacketTrackersSeconds, LogPacketTrackersSeconds) ACMD_COMMANDLINE;

void dbLogPacketTrackers(TimedCallback *callback, F32 timeSinceLastCallback, void *userdata)
{
	PERFINFO_AUTO_START_FUNC();
	LogPacketTrackers();
	TimedCallback_Run(dbLogPacketTrackers, userdata, sLogPacketTrackersSeconds);
	PERFINFO_AUTO_STOP();
}

static void dbPreSaveCallback(void);

void dbInit_RunOnce(void)
{
	bool configLoaded = false;
	char *estr = 0;

	InitGenericDatabaseThreads();

	if (gDatabaseState.bCloneCreateSnapshot) 
	{
		gDatabaseState.bCreateSnapshot = true;
	}
	if (gDatabaseState.bCreateSnapshot)
	{
		// These are here to avoid duplicate id issues when reporting alerts
		if (gDatabaseState.bCloneCreateSnapshot)
		{
			SetAppGlobalType(GLOBALTYPE_OBJECTDB_MERGER);
			gServerLibState.containerID = 1001;
		}
		else
		{
			SetAppGlobalType(GLOBALTYPE_OBJECTDB_MERGER);
			gServerLibState.containerID = 1000;
		}
		gDatabaseState.databaseType = DBTYPE_MERGER;
	}
	if (gDatabaseState.dumpSQLData[0])
	{
		gDatabaseState.databaseType = DBTYPE_DUMPER;
	}
	if (gDatabaseState.corruptionScan[0])
	{
		gDatabaseState.databaseType = DBTYPE_SCAN;
	}
	if (gDatabaseState.bReplayLogsToClone)
	{
		gDatabaseState.databaseType = DBTYPE_REPLAY;
	}

	//load the config
	do {
		if (!fileExists(gObjectDBConfigFileName)) break;
		if (!ParserReadTextFile(gObjectDBConfigFileName, parse_DatabaseConfig, &gDatabaseConfig, 0)) break;
		objFixupContainerConfig();
		gDatabaseConfig.IOConfig = objGetContainerIOConfig(gDatabaseConfig.IOConfig);
		configLoaded = true;
	} while (false);

	//clean up config defaults
	if (!gDatabaseConfig.iSnapshotInterval) gDatabaseConfig.iSnapshotInterval = 60; //Minutes
	if (!gDatabaseConfig.iIncrementalInterval && !gDatabaseConfig.bNoHogs) gDatabaseConfig.iIncrementalInterval = 20; //Minutes
	if (!gDatabaseConfig.iIncrementalHoursToKeep && !gDatabaseConfig.bNoHogs) gDatabaseConfig.iIncrementalHoursToKeep = 4;
	if (!gDatabaseConfig.CLExportDir[0]) 
	{
		sprintf(gDatabaseConfig.CLExportDir, "%s/userExportedCharacters", dbDataDir());
		makeDirectories(gDatabaseConfig.CLExportDir);
	}

	if (configLoaded)
	{
		estrStackCreate(&estr);
		printf("Database configs loaded:");
		ParserWriteText(&estr, parse_DatabaseConfig, &gDatabaseConfig, WRITETEXTFLAG_PRETTYPRINT,0,0);
		printf("%s\n\n",estr);
		estrDestroy(&estr);
	}
	else
	{
		printf("No Database config file found.\n\n");
	}

	//Install "ObjectDB[0].query" support for httpXpaths.
	dbRegisterQueryXpathDomain();

	
	logEnableHighPerformance();
	gDatabaseState.DBUpdateCountAverager = CountAverager_Create(AVERAGE_MINUTE);
	gDatabaseState.DBUpdateSizeAverager = IntAverager_Create(AVERAGE_MINUTE);

	gDatabaseState.DBUpdateTransferAverager = CountAverager_Create(AVERAGE_MINUTE);


	if (gDatabaseState.databaseType == DBTYPE_DUMPER)
	{
		printf("Starting in DBDump mode.\n");
	}
	else if (gDatabaseState.databaseType == DBTYPE_SCAN)
	{
		printf("Starting in DBScan mode.\n");
	}
	else if (gDatabaseState.databaseType == DBTYPE_REPLAY)
	{
		printf("Starting in DBReplay mode.\n");
	}
	else
	{
		ContainerStore *playerStore;
		ContainerStore *auctionLotStore;
		ContainerStore *guildStore;
		StashTable entityHeaderTable = NULL;

		loadstart_printf("Loading schemas...");
		objLoadAllGenericSchemas();

		TimedCallback_Run(dbLogPacketTrackers, NULL, 1.0);

		assertmsg(objFindContainerSchema(GLOBALTYPE_ENTITYPLAYER),"Database was unable to find a schema for EntityPlayer! This schema file is required.");
		loadend_printf(" done.");

		LoadAndCheckExtraHeaderDataConfig();

		if(gDatabaseConfig.bUseHeaders)
			entityHeaderTable = dbCreateEntityHeaderTable();

		objCreateContainerStoreLazyLoad(objFindContainerSchema(GLOBALTYPE_ENTITYSAVEDPET), gDatabaseConfig.bLazyLoad);
		objCreateContainerStoreLazyLoad(objFindContainerSchema(GLOBALTYPE_GAMEACCOUNTDATA), gDatabaseConfig.bLazyLoad);
		objCreateContainerStoreLazyLoad(objFindContainerSchema(GLOBALTYPE_PLAYERDIARY), gDatabaseConfig.bLazyLoad);
		objCreateContainerStoreLazyLoad(objFindContainerSchema(GLOBALTYPE_DIARYENTRYBUCKET), gDatabaseConfig.bLazyLoad);

		auctionLotStore = objCreateContainerStore(objFindContainerSchema(GLOBALTYPE_AUCTIONLOT));

		playerStore = objCreateContainerStoreEx(objFindContainerSchema(GLOBALTYPE_ENTITYPLAYER), entityHeaderTable, 1024, entityHeaderTable && gDatabaseConfig.bLazyLoad, OBJECT_INDEX_HEADER_TYPE_ENTITYPLAYER, false);
		gAccountID_idx = objAddContainerStoreIndexWithPaths(playerStore,".pPlayer.AccountId", ".myContainerId", 0);
		gAccountDisplayName_idx = objAddContainerStoreIndexWithPaths(playerStore,".pPlayer.publicAccountName", ".pSaved.savedName", 0);
		gAccountAccountName_idx = objAddContainerStoreIndexWithPaths(playerStore,".pPlayer.privateAccountName", ".pSaved.savedName", 0);
		gCreatedTime_idx = objAddContainerStoreIndexWithPaths(playerStore, ".pPlayer.iCreatedTime", ".myContainerId", 0);
		gAccountIDDeleted_idx = objAddContainerStoreDeletedIndexWithPaths(playerStore,".pPlayer.AccountId", ".myContainerId", 0);
		gAccountAccountNameDeleted_idx = objAddContainerStoreDeletedIndexWithPaths(playerStore,".pPlayer.privateAccountName", ".myContainerId", 0);

		// These container types ignore base container id limits because they will be cleared in the event of a shard merge
		objCreateContainerStoreEx(objFindContainerSchema(GLOBALTYPE_TEAM), NULL, 1024, false, OBJECT_INDEX_HEADER_TYPE_NONE, true);
		objCreateContainerStoreEx(objFindContainerSchema(GLOBALTYPE_QUEUEINFO), NULL, 1024, false, OBJECT_INDEX_HEADER_TYPE_NONE, true);

//		This one is commented out because it can't distinguish between inventory V1 and V2 characters.
//		Also it was never used.
//		gLevel_idx = objAddContainerStoreIndexWithPaths(playerStore,".pChar.iLevelExp", ".myContainerId", 0);

		gLastPlayedTime_idx = objAddContainerStoreIndexWithPaths(playerStore,".pPlayer.iLastPlayedTime", ".myContainerId", 0);
		gFixupVersion_idx = objAddContainerStoreIndexWithPaths(playerStore,".pSaved.fixupVersion", ".myContainerId", 0);
		gVirtualShardId_idx = objAddContainerStoreIndexWithPaths(playerStore,".pPlayer.iVirtualShardId", ".myContainerId", 0);

		objContainerStoreSetDestroyTimeFunc(playerStore, entityPlayerGetDestroyTime, entityPlayerGetDestroyTimeUserData);
		
		objSetPreSaveCallback(dbPreSaveCallback);

		if(gDatabaseConfig.bLazyLoad)
		{
			objStaleContainerTypeAdd(GLOBALTYPE_ENTITYPLAYER, entityPlayer_GetStaleCutoffIndex, entityPlayer_GenerateExtraStaleCutoffs, entityPlayer_NotStale);
			objStaleContainerTypeAdd(GLOBALTYPE_ENTITYSAVEDPET, NULL, NULL, NULL);
			objStaleContainerTypeAdd(GLOBALTYPE_GAMEACCOUNTDATA, NULL, NULL, NULL);
			objStaleContainerTypeAdd(GLOBALTYPE_PLAYERDIARY, NULL, NULL, NULL);
			objStaleContainerTypeAdd(GLOBALTYPE_DIARYENTRYBUCKET, NULL, NULL, NULL);
		}

		if(gDatabaseConfig.bSaveAllHeaders)
			objContainerStoreSetSaveAllHeaders(playerStore, true);

		guildStore = objCreateContainerStore(objFindContainerSchema(GLOBALTYPE_GUILD));
		gGuildCreatedTime_idx = objAddContainerStoreIndexWithPaths(guildStore, ".iCreatedOn", ".iContainerID", 0);

		if(gDatabaseConfig.bDisableNonCriticalIndexing)
		{
			objContainerStoreDisableIndexing(auctionLotStore);
			objContainerStoreDisableIndexing(guildStore);
		}

		if (!gDatabaseState.bIsolatedMode && !gDatabaseState.bCreateSnapshot)
		{
			loadstart_printf("Connecting to transaction server ...");

			while (!InitObjectTransactionManagerEx(GetAppGlobalType(),gServerLibState.containerID,
				gServerLibState.transactionServerHost,
				gServerLibState.transactionServerPort,
				gServerLibState.bUseMultiplexerForTransactions, NULL, 
				GenericDatabaseThreadIsEnabled() ? ObjectDBThreadedLocalTransactionManagerLinkCallback : NULL)) // NULL means to use the default LinkCallback
			{
				Sleep(1000);
			}

			SetLocalTransactionManagerShutdownCB(objLocalManager(), dbGracefulShutdownCB);

			AttemptToConnectToController(false, dbHandleControllerMsg, true);

			loadend_printf("done");
		}
	}

	dbUpdateTitle();
	
	if (gDatabaseState.iStartupWait)
	{
		int wait = gDatabaseState.iStartupWait;
		printf("Startup Wait %dms.\n", gDatabaseState.iStartupWait);
		while (wait > 0)
		{
			Sleep(10);
			wait -= 10;
			printf("[%dms]            \r", wait);
		}
		printf("                                    \r");
	}

	if (gDatabaseState.databaseType == DBTYPE_DUMPER)
	{
		GSM_SwitchToSibling(DBSTATE_DUMPER, false);
	}
	else if (gDatabaseState.databaseType == DBTYPE_SCAN)
	{
		GSM_SwitchToSibling(DBSTATE_SCANNER, false);
	}
	else if (gDatabaseState.databaseType == DBTYPE_REPLAY)
	{
		//Start the clone.
		// It will then go to the replay state
		GSM_SwitchToSibling(DBSTATE_MASTER_LAUNCH_CLONE,false);
	}
	else if (gDatabaseState.databaseType == DBTYPE_CLONE)
	{
		GSM_SwitchToSibling(DBSTATE_CLONE, false);
	}
	else
	{
		GSM_SwitchToSibling(DBSTATE_MASTER, false);
	}
}

void dbMaster_Enter(void)
{
	if (gDatabaseState.bCreateSnapshot)
	{
		GSM_AddChildState(DBSTATE_HOG_SNAPSHOT_MERGER, false);
	}
	else
	{
		if (!gDatabaseState.bIsolatedMode)
			TimedCallback_Add(dbSendGlobalInfo, NULL, 5.0f);

		GSM_AddChildState(DBSTATE_MASTER_LOAD,false);
	}
}

ObjectDBStatus gStatus;

void InitObjectDBStatus(void)
{
	eaIndexedEnable(&gStatus.ppContainerInfo, parse_ObjectDBStatus_ContainerInfo);
	estrPrintf(&gStatus.pGenericInfo, "<input type=\"hidden\" id=\"defaultautorefresh\" value=\"1\"><a href=\"/viewxpath?xpath=%s[%u].generic\">Generic ServerLib info for the %s</a>",
		GlobalTypeToName(GetAppGlobalType()), gServerLibState.containerID,GlobalTypeToName(GetAppGlobalType()));

	estrPrintf(&gStatus.pCommandLine, "%s", GetCommandLine());

	estrPrintf(&gStatus.ObjectDBCommands, "<a href=\"%s%sobjectdb\">Command List</a>",
		LinkToThisServer(), COMMANDCATEGORY_DOMAIN_NAME);
}

void InitObjectDBDebugInfo(void)
{
	eaIndexedEnable(&gStatus.ppContainerStorageInfo, parse_ObjectDBStatus_StorageInfo);
}

void OVERRIDE_LATELINK_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct)
{
	gStatus.iOutstandingDatabaseRequests = GetOutstandingGenericDatabaseRequestCount();

	*ppTPI = parse_ObjectDBStatus;
	*ppStruct = &gStatus;
}

void UpdateObjectDBStatusReplicationData(U32 timestamp, U64 sequence)
{
	gStatus.iLastReplicatedSequenceNumber = sequence;
	gStatus.iLastReplicatedTimeStamp = timestamp;
}

void SendCloneStatusPacket(NetLink *link)
{
	static U32 timeLastSent = 0;
	U32 now = timeSecondsSince2000();
	Packet *pkt;
	if(now == timeLastSent)
		return;

	pkt = pktCreate(link,DBTOMASTER_STATUS_UPDATE);
	pktSendU64(pkt, gStatus.iLastReplicatedSequenceNumber);
	pktSendU32(pkt, gStatus.iLastReplicatedTimeStamp);
	pktSend(&pkt);
	timeLastSent = now;
}

void ApplyCloneStatusPacket(Packet *pkt)
{
	gStatus.iLastSequenceNumberProcessedOnClone = pktGetU64(pkt);
	gStatus.iLastTimeStampProcessedOnClone = pktGetU32(pkt);
	gStatus.iLastCloneUpdateTimeStamp = timeSecondsSince2000();
}

U32 gDisableNonResponsiveCloneAlert = 0;
AUTO_CMD_INT(gDisableNonResponsiveCloneAlert, DisableNonResponsiveCloneAlert);

U32 gNonResponsiveCloneAlertThresholdMinutes = 2;
AUTO_CMD_INT(gNonResponsiveCloneAlertThresholdMinutes, NonResponsiveCloneAlertThresholdMinutes);

U32 gNonResponsiveCloneAlertOkayThreshold = 10;
AUTO_CMD_INT(gNonResponsiveCloneAlertOkayThreshold, NonResponsiveCloneAlertOkayThreshold);

U32 gNonResponsiveAlertThrottleMinutes = 60;
AUTO_CMD_INT(gNonResponsiveAlertThrottleMinutes, NonResponsiveAlertThrottleMinutes);

U32 gNonResponsiveAlertResetThrottleOnOkay = true;
AUTO_CMD_INT(gNonResponsiveAlertResetThrottleOnOkay, NonResponsiveAlertResetThrottleOnOkay);

U32 gDisableCloneReplicationLagAlert = 0;
AUTO_CMD_INT(gDisableCloneReplicationLagAlert, DisableCloneReplicationLagAlert);

U32 gCloneReplicationLagThresholdMinutes = 20;
AUTO_CMD_INT(gCloneReplicationLagThresholdMinutes, CloneReplicationLagThresholdMinutes);

U32 gCloneReplicationLagOkayThresholdMinutes = 1;
AUTO_CMD_INT(gCloneReplicationLagOkayThresholdMinutes, CloneReplicationLagOkayThresholdMinutes);

U32 gCloneReplicationLagAlertThrottleMinutes = 60;
AUTO_CMD_INT(gCloneReplicationLagAlertThrottleMinutes, CloneReplicationLagAlertThrottleMinutes);

U32 gCloneReplicationAlertDelay = 10;
AUTO_CMD_INT(gCloneReplicationAlertDelay, CloneReplicationAlertDelay);

U32 gCloneReplicationLagAlertResetThrottleOnOkay = true;
AUTO_CMD_INT(gCloneReplicationLagAlertResetThrottleOnOkay, CloneReplicationLagAlertResetThrottleOnOkay);

void AnalyzeCloneStatus(void)
{
	U32 now;
	static U32 timeLastNonResponsiveAlertSent = 0;
	static U32 timeLastCloneReplicationLagAlertSent = 0;
	static bool nonResponsiveAlertSent = false;
	static bool cloneReplicationLagAlertSent = false;

	if(!dbConnectedToClone())
		return;

	now = timeSecondsSince2000();
	if(!gDisableNonResponsiveCloneAlert)
	{
		if( (!timeLastNonResponsiveAlertSent || now > timeLastNonResponsiveAlertSent + (gNonResponsiveAlertThrottleMinutes * SECONDS_PER_MINUTE)) &&
			gStatus.iLastCloneUpdateTimeStamp && 
			now > gStatus.iLastCloneUpdateTimeStamp + (gNonResponsiveCloneAlertThresholdMinutes * SECONDS_PER_MINUTE))
		{
			ErrorOrCriticalAlert("OBJECTDB.CLONE.NONRESPONSIVE", "The CloneObjectDB has not sent a status update in %u seconds.", now-gStatus.iLastCloneUpdateTimeStamp);
			timeLastNonResponsiveAlertSent = now;
			nonResponsiveAlertSent = true;
		}
		else if(nonResponsiveAlertSent &&
				gStatus.iLastCloneUpdateTimeStamp &&
				now < gStatus.iLastCloneUpdateTimeStamp + gNonResponsiveCloneAlertOkayThreshold)
		{
			ErrorOrCriticalAlert("OBJECTDB.CLONE.RESPONSIVE", "The CloneObjectDB has begun sending status updates again.");
			nonResponsiveAlertSent = false;
			if(gNonResponsiveAlertResetThrottleOnOkay)
				timeLastNonResponsiveAlertSent = 0;
		}
	}

	if(!gDisableCloneReplicationLagAlert)
	{
		static U32 timeOfFirstTrigger = 0;
		if((!timeLastCloneReplicationLagAlertSent || now > timeLastCloneReplicationLagAlertSent + (gCloneReplicationLagAlertThrottleMinutes * SECONDS_PER_MINUTE)) &&
			gStatus.iLastTimeStampProcessedOnClone && 
			gStatus.iLastReplicatedTimeStamp > gStatus.iLastTimeStampProcessedOnClone + (gCloneReplicationLagThresholdMinutes * SECONDS_PER_MINUTE))
		{
			if(!timeOfFirstTrigger)
				timeOfFirstTrigger = now;

			if(now > timeOfFirstTrigger + gCloneReplicationAlertDelay)
			{
				ErrorOrCriticalAlert("OBJECTDB.CLONE.SLOWPROCESSING", "The CloneObjectDB is currently %u seconds behind processing updates.", gStatus.iLastReplicatedTimeStamp - gStatus.iLastTimeStampProcessedOnClone);
				timeLastCloneReplicationLagAlertSent = now;
				cloneReplicationLagAlertSent = true;
				timeOfFirstTrigger = 0;
			}
		}
		else if(cloneReplicationLagAlertSent &&
				gStatus.iLastTimeStampProcessedOnClone &&
				gStatus.iLastReplicatedTimeStamp < gStatus.iLastTimeStampProcessedOnClone + (gCloneReplicationLagOkayThresholdMinutes * SECONDS_PER_MINUTE))
		{
			ErrorOrCriticalAlert("OBJECTDB.CLONE.PROCESSING.CAUGHTUP", "The CloneObjectDB has recovered and is now %u seconds behind processing updates.", gStatus.iLastReplicatedTimeStamp - gStatus.iLastTimeStampProcessedOnClone);
			cloneReplicationLagAlertSent = false;
			timeOfFirstTrigger = 0;
			if(gCloneReplicationLagAlertResetThrottleOnOkay)
				timeLastCloneReplicationLagAlertSent = 0;
		}
		else
		{
			timeOfFirstTrigger = 0;
		}
	}
}

static U32 iNextStatsTime = 0;
static U32 iNextLogTime = 0;

static U32 gDumpStatsFrequency = 10;
AUTO_CMD_INT(gDumpStatsFrequency, DumpStatsFrequency) ACMD_CMDLINE;

static U32 gLogStatsFrequency = 3600;
AUTO_CMD_INT(gLogStatsFrequency, LogStatsFrequency) ACMD_CMDLINE;

typedef struct StalenessTimeBucket StalenessTimeBucket;
typedef struct StalenessTimeBucket
{
	U32 uCount;
	StalePlayerID **ppIDs;
	StalenessTimeBucket* pNext;
	StalenessTimeBucket* pPrev;
} StalenessTimeBucket;

static StalenessTimeBucket *sSmallStalenessBucketHead = NULL;
static StalenessTimeBucket *sSmallStalenessBucketTail = NULL;
static StalenessTimeBucket *sLargeStalenessBucketHead = NULL;
static StalenessTimeBucket *sLargeStalenessBucketTail = NULL;
static StashTable stPlayerIDToStalenessBucket = NULL;

void ObjectDBDumpStats(void)
{
	U32 iCurTime = timeSecondsSince2000();
	if (iCurTime > iNextStatsTime)
	{
		int i;
		U32 iActivePlayers;
		PERFINFO_AUTO_START_FUNC();

		iActivePlayers = objCountActiveContainersWithType(GLOBALTYPE_ENTITYPLAYER);

		sprintf(gStatus.serverType, "%s (version: %s)", GlobalTypeToName(GetAppGlobalType()), GetUsefulVersionString());

		gStatus.iID = GetAppGlobalID();

		gStatus.PlayerStorageInfo.PreloadedPlayers = objCountPreloadedContainersOfType(GLOBALTYPE_ENTITYPLAYER);
		gStatus.PlayerStorageInfo.OfflinePlayerCount = GetTotalOfflineCharacters();
		gStatus.PlayerStorageInfo.OnlinedPlayers = GetTotalRestoredCharacters();
		gStatus.PlayerStorageInfo.LastRestoreTime = GetLastRestoreTime();
		gStatus.PlayerStorageInfo.UnpackedDeletedPlayers = objCountDeletedUnpackedContainersOfType(GLOBALTYPE_ENTITYPLAYER);

		gStatus.iInUseHandleCaches = GetNumberOfInUseHandleCaches();
		
		// This will be accumulated as we walk the ContainerStores
		gStatus.iTotalSubscriptions = 0;

		gStatus.iCurrentlyLockedContainerStores = objGetCurrentlyLockedContainerStoreCount();

		if(gContainerSource.sourceInfo)
		{
			gStatus.iBaseContainerID = gContainerSource.sourceInfo->iBaseContainerID;
			gStatus.iMaxContainerID = gContainerSource.sourceInfo->iMaxContainerID;
		}

		for(i = 0; i < GLOBALTYPE_MAXTYPES; ++i)
		{
			ContainerStore *store = objFindContainerStoreFromType(i);
			if(store)
			{
				int index;
				ObjectDBStatus_ContainerInfo *conInfo = NULL;
				ObjectDBStatus_StorageInfo *stInfo = NULL;
				PERFINFO_AUTO_START("PopulateContainerInfo", 1);
				// setup container info
				index = eaIndexedFindUsingInt(&gStatus.ppContainerInfo, i);
				objLockContainerStore_ReadOnly(store);
				if(i == GLOBALTYPE_ENTITYPLAYER)
				{
					gStatus.PlayerStorageInfo.DeletedPlayerCount = store->deletedContainers;
					gStatus.PlayerStorageInfo.NextDelete = GetNextCachedDeleteExpireInterval(i);
				}

				if(index >= 0)
				{
					conInfo = gStatus.ppContainerInfo[index];
				}
				else
				{
					conInfo = StructCreate(parse_ObjectDBStatus_ContainerInfo);
					conInfo->eType = i;
					eaIndexedAdd(&gStatus.ppContainerInfo, conInfo);
					estrPrintf(&conInfo->pName, "<a href=\"%s%s%s\">%s</a>",
						LinkToThisServer(), GLOBALOBJECTS_DOMAIN_NAME, GlobalTypeToName(i), GlobalTypeToName(i));
				}
				GetUpdatesByContainerType(i, &conInfo->UpdateCount, &conInfo->UpdateLineCount);
				conInfo->AverageLinesPerUpdate = conInfo->UpdateCount ? (F32)conInfo->UpdateLineCount / conInfo->UpdateCount : 0;
				conInfo->ContainerCount = store->totalContainers;
				conInfo->ActiveCount = store->totalContainers - store->ownedContainers;
				conInfo->MaxContainerID = store->maxID;
				conInfo->Subscriptions = GetSubscriptionRefCountByType(i);
				conInfo->OnlineSubscriptions = GetOnlineSubscriptionRefCountByType(i);
				conInfo->DestroyedThisRun = objCountDestroyedContainersOfType(i);
				conInfo->SubscriptionsPerPlayer = iActivePlayers ? (F32)conInfo->Subscriptions / iActivePlayers : 0;
				conInfo->CurrentLockedContainers = store->lockedContainerCount;
				gStatus.iTotalSubscriptions += conInfo->Subscriptions;

				PERFINFO_AUTO_STOP();

				if (!store->lazyLoad)
				{
					objUnlockContainerStore_ReadOnly(store);
					continue;
				}

				PERFINFO_AUTO_START("PopulateContainerStorageInfo", 1);
				index = eaIndexedFindUsingInt(&gStatus.ppContainerStorageInfo, i);
				if(index >= 0)
				{
					stInfo = gStatus.ppContainerStorageInfo[index];
				}
				else
				{
					stInfo = StructCreate(parse_ObjectDBStatus_StorageInfo);
					stInfo->eType = i;
					eaIndexedAdd(&gStatus.ppContainerStorageInfo, stInfo);
					estrPrintf(&stInfo->pName, "<a href=\"%s%s%s\">%s</a>",
						LinkToThisServer(), GLOBALOBJECTS_DOMAIN_NAME, GlobalTypeToName(i), GlobalTypeToName(i));
				}

				stInfo->ContainerCount = store->totalContainers;
				stInfo->UnpackedThisRun = objCountUnpackedContainersOfType(i);
				stInfo->RepackedThisRun = objCountUnloadedContainersOfType(i);
				stInfo->FreeRepacksThisRun = objCountFreeRepacksOfType(i);
				stInfo->UnpackedCount = 0;
				stInfo->TotalUnpacks = objCountTotalUnpacksOfType(i);
				stInfo->UnpackAvgTicks = objGetAverageUnpackTicksOfType(i);

				PERFINFO_AUTO_START("CountUnpackedContainers", 1);
				if (store->containerSchema)
				{
					int numContainers;
					int j;

					numContainers = eaSize(&store->containers);
					for (j = 0; j < numContainers; j++)
					{
						Container* con = store->containers[j];
						if (con->containerData)
						{
							++stInfo->UnpackedCount;
						}
					}
				}
				PERFINFO_AUTO_STOP();

				objUnlockContainerStore_ReadOnly(store);
				PERFINFO_AUTO_STOP();
			}
		}

		gStatus.fSubscriptionsPerPlayer = iActivePlayers ? (F32) gStatus.iTotalSubscriptions / iActivePlayers : 0;

		iNextStatsTime = iCurTime + gDumpStatsFrequency;

		if(iCurTime > iNextLogTime)
		{
			// Log the contents of gStatus.

			SERVLOG_PAIRS(LOG_OBJECTDB_PERF, "PlayerStorageInfo", 
				("DeletedPlayerCount", "%u", gStatus.PlayerStorageInfo.DeletedPlayerCount)
				("LastRestoreTime", "%u", gStatus.PlayerStorageInfo.LastRestoreTime)
				("NextDelete", "%u", gStatus.PlayerStorageInfo.NextDelete)
				("OfflinePlayerCount", "%u", gStatus.PlayerStorageInfo.OfflinePlayerCount)
				("OnlinedPlayers", "%u", gStatus.PlayerStorageInfo.OnlinedPlayers)
				("PreloadedPlayers", "%u", gStatus.PlayerStorageInfo.PreloadedPlayers)
				("UnpackedDeletedPlayers", "%u", gStatus.PlayerStorageInfo.UnpackedDeletedPlayers));

			for(i = 0; i < eaSize(&gStatus.ppContainerInfo); ++i)
			{
				ObjectDBStatus_ContainerInfo *info = gStatus.ppContainerInfo[i];
				SERVLOG_PAIRS(LOG_OBJECTDB_PERF, "ContainerInfo", 
					("ContainerType", "%u", info->eType)
					("ContainerTypeName", "%s", GlobalTypeToName(info->eType))
					("ContainerCount", "%u", info->ContainerCount)
					("ActiveCount", "%u", info->ActiveCount)
					("Subscriptions", "%u", info->Subscriptions)
					("OnlineSubscriptions", "%u", info->OnlineSubscriptions)
					("MaxContainerID", "%u", info->MaxContainerID)
					("DestroyedThisRun", "%u", info->DestroyedThisRun)
					("SubscriptionsPerPlayer", "%g", info->SubscriptionsPerPlayer));
			}

			for(i = 0; i < eaSize(&gStatus.ppContainerStorageInfo); ++i)
			{
				ObjectDBStatus_StorageInfo *info = gStatus.ppContainerStorageInfo[i];
				SERVLOG_PAIRS(LOG_OBJECTDB_PERF, "ContainerStorageInfo", 
					("ContainerType", "%u", info->eType)
					("ContainerTypeName", "%s", GlobalTypeToName(info->eType))
					("ContainerCount", "%u", info->ContainerCount)
					("UnpackedCount", "%u", info->UnpackedCount)
					("UnpackedThisRun", "%u", info->UnpackedThisRun)
					("RepackedThisRun", "%u", info->RepackedThisRun)
					("FreeRepacksThisRun", "%u", info->FreeRepacksThisRun)
					("TotalUnpacks", "%u", info->TotalUnpacks)
					("UnpackAvgTicks", "%"FORM_LL"u", info->UnpackAvgTicks));
			}

			LogSubscriptionSendTrackers();

			iNextLogTime = iCurTime + gLogStatsFrequency;
		}
		PERFINFO_AUTO_STOP();
	}
}

static StalenessTimeBucket *CreateStalenessBucket(void)
{
	StalenessTimeBucket *pBucket = calloc(1, sizeof(StalenessTimeBucket));
	eaIndexedEnable(&pBucket->ppIDs, parse_StalePlayerID);
	return pBucket;
}

static void DestroyStalenessBucket(StalenessTimeBucket *pBucket)
{
	eaDestroy(&pBucket->ppIDs);
	free(pBucket);
}

static StalePlayerID *CreateStalePlayerID(U32 uID)
{
	StalePlayerID *pID = StructCreate(parse_StalePlayerID);
	pID->uID = uID;
	return pID;
}

static void DestroyStalePlayerID(StalePlayerID *pID)
{
	StructDestroy(parse_StalePlayerID, pID);
}

static bool AddPlayerToStalenessBucket(U32 uID, StalenessTimeBucket *pBucket, StalePlayerID *pID)
{
	if (!pBucket || !uID) return false;
	if (eaIndexedFindUsingInt(&pBucket->ppIDs, uID) != -1) return false;
	if (!stashIntAddPointer(stPlayerIDToStalenessBucket, uID, pBucket, true)) return false;

	if (!pID) pID = CreateStalePlayerID(uID);
	eaIndexedAdd(&pBucket->ppIDs, pID);
	++pBucket->uCount;
	return true;
}

static bool RemovePlayerFromStalenessBucket(U32 uID, bool bRemoveStash, StalePlayerID **ppID)
{
	StalenessTimeBucket *pBucket = NULL;
	StalePlayerID *pID = NULL;
	int index;

	if (!uID) return false;
	if (!stashIntFindPointer(stPlayerIDToStalenessBucket, uID, &pBucket)) return false;
	if (!pBucket) return false;

	index = eaIndexedFindUsingInt(&pBucket->ppIDs, uID);

	if (index < 0) return false;
	if (bRemoveStash && !stashIntRemovePointer(stPlayerIDToStalenessBucket, uID, NULL)) return false;

	--pBucket->uCount;
	pID = eaRemove(&pBucket->ppIDs, index);
	if (ppID)
		*ppID = pID;
	else
		DestroyStalePlayerID(pID);
	return true;
}

void dbMaster_BeginFrame(void)
{
	static int frameTimer;
	F32 frametime;
	if (!frameTimer)
		frameTimer = timerAlloc();
	frametime = timerElapsedAndStart(frameTimer);
	utilitiesLibOncePerFrame(frametime, 1);
	serverLibOncePerFrame();
	FolderCacheDoCallbacks();
	commMonitor(commDefault());
	dbUpdateTitle();

	ObjectDBDumpStats();
	LoginCharacterRestoreTick();
	AnalyzeCloneStatus();
	AlertIfStringCacheIsNearlyFull();
	MonitorGenericDatabaseThreads();

	/*if (timerElapsed(frameTimer) < 0.0005)
	{
		Sleep(DEFAULT_SERVER_SLEEP_TIME);
	}
	else
	{
		Sleep(0);
	}*/
}

int gShortenStalenessIncrementInterval = 0;
AUTO_CMD_INT(gShortenStalenessIncrementInterval, DebugShortenStalenessIncrementInterval) ACMD_COMMANDLINE;

void ForEachContainer_TakeOwnership(Container *con, void *unused)
{
	objChangeContainerState(con, CONTAINERSTATE_OWNED, GetAppGlobalType(), gServerLibState.containerID);
}

void dbMasterLoad_RunOnce(void)
{
	int i;
	U32 lastSnapshot = 0;
	char filebuf[MAX_PATH];

	dbConfigureDatabase();

	strcpy(filebuf, objGetContainerSourcePath());
	getDirectoryName(filebuf);
	GenericFileServing_Begin(0, 0);
	GenericFileServing_ExposeDirectory(strdup(filebuf));

	loadstart_printf("loading database...\n");

	serverLibLogMemReport("PreLoad");

	dbLoadEntireDatabase();

	serverLibLogMemReport("PostLoad");

	//if we're just creating a snapshot, then we're done.
	if (gDatabaseState.bCreateSnapshot)
	{
		loadend_printf("done.");
		printf("Finished creating snapshot. Exiting.\n");
		GSM_SwitchToState_Complex("/" DBSTATE_CLEANUP);
		return;
	}

	//For normal load, if we generated a snapshot, keep track of the time so we can report it in ServerMon
	if ((lastSnapshot = objGetLastSnapshot()) != 0)
		gDatabaseState.lastSnapshotTime = lastSnapshot;
	

	if (objLocalManager())
	{
		for (i = 1; i < GLOBALTYPE_MAXTYPES; i++)
		{
			if (GlobalTypeSchemaType(i) == SCHEMATYPE_PERSISTED)
			{
				RegisterAsDefaultOwnerOfObjectTypeWithTransactionServer(objLocalManager(),i);
			}

		}		

		RegisterDBUpdateDataCallback(objLocalManager(),&dbUpdateCB);
	}

	// Needed to make sure there is no race condition with owner changes
	FlushGenericDatabaseThreads(true);

	for (i = 0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		char* classname = 0;
		ContainerStore *store = &gContainerRepository.containerStores[i];
		GlobalType type;
		if (!store->containerSchema)
		{
			continue;
		}
		type = store->containerSchema->containerType;

		ForEachContainerOfTypeEx(i, ForEachContainer_TakeOwnership, NULL, NULL, false, true);
		ForEachContainerOfTypeEx(i, ForEachContainer_TakeOwnership, NULL, NULL, false, false);
		objLockContainerStore_Write(store);
		store->ownedContainers = eaSize(&store->containers);
		objUnlockContainerStore_Write(store);

	}

	if(gDatabaseConfig.bEnableOfflining)
	{
		InitializeTotalOfflineCharacters();
	}

	CreateAndStartDBSubscriptionThread();

	//Load queries for Controller Scripts
	if (isDevelopmentMode() && gDBTestState.testingEnabled)
	{
		objLoadNamedQueries("server/queries/databaseTest/", ".query", "DatabaseTestQueries.bin");
	}

	loadend_printf("done.");

	loadstart_printf("Loading login character info queries...");
	gDatabaseState.pLoginCharacterQueries = StructCreate(parse_NamedPathQueriesAndResults);
	ParserLoadFiles(NULL, "server/LoginCharacterQueries.txt", "LoginCharacterQueries.bin", 0, parse_NamedPathQueriesAndResults, gDatabaseState.pLoginCharacterQueries);
	if (eaSize(&gDatabaseState.pLoginCharacterQueries->eaQueries))
	{
		ContainerSchema *pSchema = objFindContainerSchema(GLOBALTYPE_ENTITYPLAYER);
		ParseTable *pTable = pSchema->classParse;
		for (i = 0; i < eaSize(&gDatabaseState.pLoginCharacterQueries->eaQueries); i++)
		{
			NamedPathQueryAndResult *pQuery = gDatabaseState.pLoginCharacterQueries->eaQueries[i];
			ParseTable *pOutTable;
			S32 iColOut, iIndOut;
			void *pOut;
			if (!ParserResolvePathEx(pQuery->pchObjPath, pTable, NULL, &pOutTable, &iColOut, &pOut, &iIndOut, NULL, NULL, NULL, NULL, OBJPATHFLAG_TRAVERSEUNOWNED))
			{
				ErrorFilenamef(gDatabaseState.pLoginCharacterQueries->pchFilename, "Unable to resolve object path %s (for variable %s) during login", pQuery->pchObjPath, pQuery->pchKey);
			}
		}
	}

	if(gContainerSource.enableStaleContainerCleanup && gDatabaseConfig.bLazyLoad)
	{
		activateStaleContainerCleanup();
	}

	stringCacheDisableLocklessRead();
	loadend_printf(" done. (%d loaded)", eaSize(&gDatabaseState.pLoginCharacterQueries->eaQueries));
	if (gDatabaseState.databaseType == DBTYPE_MASTER)
	{
		//Start the clone.
		GSM_SwitchToSibling(DBSTATE_MASTER_LAUNCH_CLONE,false);
	}
	else //clone or standalone
	{
		GSM_SwitchToSibling(DBSTATE_MASTER_MOVE_TO_OFFLINE_HOGG,false);
	}
}

void dbCleanup(void);

void dbScanner_ScanFile(void)
{
	GlobalType type = GLOBALTYPE_ENTITYPLAYER;
	const char *kind = GlobalTypeToName(type);
	//char *result = NULL;
	//char cwd[MAX_PATH];
	//char *outpath = cwd;

	ExprContext *pContext;
	Expression *pExpr = NULL;

	//(void)getcwd(cwd, MAX_PATH);

	if (!fileExists(gDatabaseState.corruptionScan))
	{
		printf("Could not open file: %s\n", gDatabaseState.corruptionScan);
		dbCleanup();
		return;
	}

	objSetDumpMode(4);

	objSetContainerSourceToHogFile(gDatabaseState.corruptionScan, INT_MAX, NULL, NULL);
	if (gDatabaseState.loadThreads)
		objSetMultiThreadedLoadThreads(gDatabaseState.loadThreads);
	else
		objSetMultiThreadedLoadThreads(2);

	printf("Loading containers from file: %s\n", gDatabaseState.corruptionScan);
	objLoadContainersFromHoggForDump(gDatabaseState.corruptionScan, type);


	pContext = exprContextCreate();
	exprContextSetSilentErrors(pContext, true);
	//Make the context use default objectpath root object.
	exprContextSetUserPtrIsDefault(pContext, true);
	exprContextAddStaticDefineIntAsVars(pContext, GlobalTypeEnum, "");

	printf("Scanning...\n\n>>>\n");
	
	do
	{
		ContainerIterator iter;
		Container *con;
		ContainerStore *store = objFindContainerStoreFromType(type);
		int count = 0;
		char *estr = NULL;
		char *title = NULL;
		if (!store) break;

		estrStackCreate(&estr);
		estrStackCreate(&title);

		estrPrintf(&estr, "%s", gDatabaseState.corruptionExpr);
		estrReplaceOccurrences(&estr, "\\q", "\"");

		objInitContainerIteratorFromType(type, &iter);
		while (con = objGetNextContainerFromIterator(&iter))
		{
			MultiVal answer = {0};
			int iAnswer = 0;

			exprContextSetUserPtr(pContext, con->containerData, store->containerSchema->classParse);
			if (!pExpr)
			{
				pExpr = exprCreate();
				if (!exprGenerateFromString(pExpr, pContext, estr, NULL)) 
				{
					printf("<<<\n\nThere was an error generating the expression from string:\n%s\n\n", gDatabaseState.corruptionExpr);
					break;
				}
			}
			exprEvaluate(pExpr, pContext, &answer);

			if (exprContextCheckStaticError(pContext))
			{
				printf("<<<\n\nThe expression \"%s\" had a static error.\n\n", gDatabaseState.corruptionExpr);
				break;
			}

			if (answer.type == MULTI_INT) iAnswer = QuickGetInt(&answer);
			else iAnswer = 0;
			
			if (iAnswer)
			{
				printf("%s[%u] = %d.\n", kind, con->containerID, iAnswer);
			}

			estrPrintf(&title, "ObjectDB Corruption Scan: %u containers scanned.", ++count);
			if (count % 100 == 0)
				setConsoleTitle(title);
			
		}
		objClearContainerIterator(&iter);
		setConsoleTitle(title);

		estrDestroy(&title);
		estrDestroy(&estr);
	}
	while (false);


	exprContextDestroy(pContext);
	if (pExpr) exprDestroy(pExpr);

	printf("\n<<<\ndone.\n");

	dbCleanup();
}

void dbReplayer_ResendLogs(const char *cmd_orig, U64 sequence, U32 timestamp)
{
	dbLogTransaction(cmd_orig, sequence, timestamp);
}

void DoNothing(ErrorMessage *errMsg, void *userdata)
{
	PERFINFO_AUTO_START_FUNC();
	PERFINFO_AUTO_STOP();
}

void dbReplayer_ReplayLogsEnter(void)
{
	dbConfigureDatabase();
	objSetCommandReplayCallback(dbReplayer_ResendLogs);
	objFindLogsToReplay();
	ErrorfSetCallback(DoNothing, NULL);
}

void dbReplayer_ReplayLogs(void)
{
	static int frameTimer = 0;
	F32 frametime;
	if (!frameTimer)
		frameTimer = timerAlloc();
	frametime = timerElapsedAndStart(frameTimer);
	objReplayLogs();
	utilitiesLibOncePerFrame(frametime, 1);
	serverLibOncePerFrame();
	commMonitor(commDefault());	
}

void dbDumper_DumpFile(void)
{
	GlobalType type = NameToGlobalType(gDatabaseState.dumpType);
	char *result = NULL;
	char cwd[MAX_PATH];
	char *outpath = cwd;
	(void)getcwd(cwd, MAX_PATH);

	if (!type)
	{
		printf("Could not find GlobalType: %s\n", gDatabaseState.dumpType);
		dbCleanup();
		return;
	}
	if (!fileExists(gDatabaseState.dumpSQLData))
	{
		printf("Could not open file: %s\n", gDatabaseState.dumpSQLData);
		dbCleanup();
		return;
	}

	objSetDumpMode(1);

	objSetContainerSourceToHogFile(gDatabaseState.dumpSQLData, INT_MAX, NULL, NULL);

	printf("Loading containers from file: %s\n", gDatabaseState.dumpSQLData);
	objLoadContainersFromHoggForDump(gDatabaseState.dumpSQLData, type);

	if (gDatabaseState.dumpOutPath && gDatabaseState.dumpOutPath[0])
	{
		if (dirExists(gDatabaseState.dumpOutPath))
		{
			outpath = gDatabaseState.dumpOutPath;
		}
		else
		{
			printf("Could not find output directory:%s\n", outpath);
			dbCleanup();
			return;
		}
	}
	
	dbDumpSchemaToSQL(gDatabaseState.dumpType, outpath);

	printf("Dumping %ss to %s...\n\n", gDatabaseState.dumpType, outpath);
	result = objDumpDatabaseToSQL(gDatabaseState.dumpType, outpath);

	if (result)
		printf("%s\n", result);

	printf ("done.\n");
	dbCleanup();
}


static int extraslowness = 0;
AUTO_CMD_INT(extraslowness, ExtraSlowness) ACMD_COMMANDLINE;

extern WorkerThread *wtContainerRestore;

static void dbPreSaveCallback(void)
{
	if (GenericDatabaseThreadIsActive())
	{
		FlushGenericDatabaseThreads(false);
	}
}

void dbMasterHandleRequests_BeginFrame(void)
{
	if (eaSize(&gDatabaseState.ppCommandsToRun))
	{
		int i;
		for (i = 0; i < eaSize(&gDatabaseState.ppCommandsToRun); i++)
		{
			globCmdParse(gDatabaseState.ppCommandsToRun[i]);
		}
		eaClearEx(&gDatabaseState.ppCommandsToRun, NULL);
	}
	if (!gDatabaseState.bNoSaving)
	{
		U32 currentTime = timeSecondsSince2000();
	
		objContainerSaveTick();

		if (currentTime > gDatabaseState.lastSnapshotTime + gDatabaseConfig.iSnapshotInterval*60)
		{
			if(isDevelopmentMode() && gDatabaseConfig.bShowSnapshots == 2)
				gDatabaseConfig.bShowSnapshots = 0;

			dbCreateSnapshotLocal(0, 0, NULL,!gDatabaseConfig.bShowSnapshots, TimeToDefrag(gDatabaseConfig.iDaysBetweenDefrags, gDatabaseState.lastDefragDay, gDatabaseConfig.iTargetDefragWindowStart, gDatabaseConfig.iTargetDefragWindowDuration));
		}
	}
	dbUpdateCloneConnection();

	//check pending intershardtransfer requests
	wtMonitor(wtIntershardTransfer);
	wtMonitor(wtContainerRestore);

	if (extraslowness) Sleep(extraslowness);
}

// Entity Export

//a helper function that prefixes the entity "block" with the entity type for convenience
static void dbSaveEntityText(char** pestr, Container* container)
{
	char* estrEnt = NULL;
	estrStackCreate(&estrEnt);
	estrPrintf(&estrEnt, "\ncontainers\n{\n\tentityType %s\n\tentityData ",GlobalTypeToName(container->containerType)); 
	estrAppend(pestr,&estrEnt);
	estrClear(&estrEnt);
	ParserWriteTextEscaped(&estrEnt,container->containerSchema->classParse,container->containerData,0,TOK_PERSIST,0);
	estrConcatf(&estrEnt, "\n}\n");
	estrAppend(pestr,&estrEnt);
	estrDestroy(&estrEnt);
}

typedef struct ExportedContainerData
{
	char **estr;
	ContainerRef **refs;
} ExportedContainerData;

bool alreadyInRefs(ContainerRef **refs, GlobalType type, ContainerID ID)
{
	EARRAY_FOREACH_BEGIN(refs, i);
	{
		if (refs[i]->containerType == type && refs[i]->containerID == ID) 
			return true;
	}
	EARRAY_FOREACH_END;
	return false;
}

bool GetContainerTypeAndIDFromTypeString(ParseTable pti[], void *pStruct, int column, int index, const char *ptypestring, char **idObjPath, ContainerID *id, GlobalType *eGlobalType)
{
	char *buf = NULL;
	char *objpath = NULL;
	
	*eGlobalType = GLOBALTYPE_NONE;
	*id = 0;

	if(ptypestring[0] == '.')
	{
		int type;
		if(objPathGetInt(ptypestring, pti, pStruct,&type))
		{
			*eGlobalType = (GlobalType)type;
		}
	}
	else
	{
		*eGlobalType = NameToGlobalType(ptypestring);
	}

	if(idObjPath)
	{
		if(pti[column].type & TOK_EARRAY)
		{
			estrConcatf(idObjPath, ".%s[%d]", pti[column].name, index);
		}
		else
		{
			estrConcatf(idObjPath, ".%s", pti[column].name);
		}
	}

	if(eGlobalType)
	{
		if(TOK_GET_TYPE(pti[column].type) == TOK_STRING_X)
		{
			const char* pchRefString = TokenStoreGetString_inline(pti, &pti[column], column, pStruct, index, NULL);
			*id = StringToContainerID( pchRefString );
		}
		else
		{
			*id = TokenStoreGetInt_inline(pti, &pti[column], column, pStruct, index, NULL);
		}
	}

	return *eGlobalType && *id;
}

bool GetContainerTypeAndIDFromContainerRef(ParseTable pti[], void *pStruct, int column, int index, char **idObjPath, ContainerID *id, GlobalType *eGlobalType)
{
	char *buf = NULL;
	char *objpath = NULL;
	void *ref;
	ParseTable *pSubTable;

	estrStackCreate(&objpath);

	if(pti[column].type & TOK_EARRAY)
	{
		estrConcatf(&objpath, ".%s[%d]", pti[column].name, index);
	}
	else
	{
		estrConcatf(&objpath, ".%s", pti[column].name);
	}

	if(idObjPath)
		estrCopy(idObjPath, &objpath);

	objPathGetStruct(objpath, pti, pStruct, &pSubTable, &ref);
	
	if(ref)
	{
		int type;
		estrCopy2(&objpath, ".containerID");

		if(idObjPath)
			estrConcatf(idObjPath, "%s", objpath);

		objPathGetInt(objpath, pSubTable, ref, id);
		estrCopy2(&objpath, ".containerType");
		if(objPathGetInt(objpath, pSubTable, ref,&type))
		{
			*eGlobalType = (GlobalType)type;
		}
	}
	estrDestroy(&objpath);
	return (*eGlobalType && *id);
}

bool ExportDependentContainer(ParseTable pti[], void *pStruct, int column, int index, void *pCBData)
{
	char *ptypestring;
	bool found = false;
	ContainerRef *conref = NULL;
	ContainerID id = 0;
	GlobalType eGlobalType = GLOBALTYPE_NONE;
	ExportedContainerData *containerData = (ExportedContainerData *)pCBData;
	PERFINFO_AUTO_START_FUNC();
	if(GetStringFromTPIFormatString(pti + column, "DEPENDENT_CONTAINER_TYPE", &ptypestring) 
		|| GetStringFromTPIFormatString(pti + column, "EXPORT_CONTAINER_TYPE", &ptypestring))
	{
		found = GetContainerTypeAndIDFromTypeString(pti, pStruct, column, index, ptypestring, NULL, &id, &eGlobalType);
	}
	else if(GetBoolFromTPIFormatString(pti + column, "DEPENDENT_CONTAINER_REF") 
		|| GetBoolFromTPIFormatString(pti + column, "EXPORT_CONTAINER_REF"))
	{
		found = GetContainerTypeAndIDFromContainerRef(pti, pStruct, column, index, NULL, &id, &eGlobalType);
	}

	if(found && !alreadyInRefs(containerData->refs, eGlobalType, id))
	{
		conref = StructCreate(parse_ContainerRef);
		conref->containerType = eGlobalType;
		conref->containerID = id;
		eaPush(&containerData->refs, conref);
	}

	PERFINFO_AUTO_STOP();
	return false;
}

// THIS WILL RELEASE THE LOCK ON CONTAINER
bool dbSerializeEntityForExport(char **estr, Container **container)
{
	ParseTable* pti = (*container)->containerSchema->classParse;
	void* substruct = NULL;
	ExportedContainerData containerData = {0};

	eaCreate(&containerData.refs);
	containerData.estr = estr;

	dbSaveEntityText(estr, *container);

	ParserTraverseParseTable(pti, (*container)->containerData, TOK_PERSIST, TOK_UNOWNED | TOK_REDUNDANTNAME, ExportDependentContainer, NULL, NULL, &containerData);
	objUnlockContainer(container);

	EARRAY_FOREACH_BEGIN(containerData.refs, i);
	{
		ContainerRef *ref = containerData.refs[i];
		Container *refCon = objGetContainerEx(ref->containerType, ref->containerID, true, false, true);

		if (refCon)
			dbSerializeEntityForExport(estr, &refCon);
	}
	EARRAY_FOREACH_END;

	eaDestroyStruct(&containerData.refs, parse_ContainerRef);

	if ( estrLength(estr) > 0 )
		return true;
	else
		return false;
}

static enum
{
	ISTTHREAD_CMDSTARTCOMM = WT_CMD_USER_START,
	ISTTHREAD_CMDCHARACTERSEND,
	ISTTHREAD_MSGSENDCOMPLETE,
	ISTTHREAD_CMDCHARACTERRECV,
	ISTTHREAD_MSGRECVCOMMIT,
};

static void wt_dbIntershardTransferCreateComm(void *user_data, void *data, WTCmdPacket *packet)
{
	if (!intershardTransferComm)
		intershardTransferComm = commCreate(0,1);
}

void initOutgoingIntershardTransfer()
{
	gOutgoingIntershardTransferQueue = createQueue();
	initQueue(gOutgoingIntershardTransferQueue, 1000);

	gAllOutgoingIntershardTransferRequests = stashTableCreateInt(1000);
	gOutgoingIntershardTransferNetLinks = stashTableCreate(10, StashDeepCopyKeys, StashKeyTypeStrings, 0);

	TimedCallback_Add(UpdateOutgoingIntershardTransfers, NULL, 0.1f);
}

AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_ACCESSLEVEL(9);
int dbIntershardTransferStatus(ContainerID id)
{
	OutgoingIntershardTransferRequest *pRequest;
	if(stashIntFindPointer(gAllOutgoingIntershardTransferRequests, id, &pRequest))
	{
		return pRequest->eTransferStatus;
	}

	return IST_UNKNOWNTRANSFER;
}

//This command initiates the intershard transfer on the source shard's objectdb.
//Returns one of the following values:
//  1 - Successfully queued the transfer
//  -9  - Character not owned by source ObjectDB
//	-10 - Character not found on source ObjectDB
//  -13 - There is already an outstanding request for that Character
//  -14 - Null destobjectdb

AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_ACCESSLEVEL(9);
const char *dbIntershardTransferCharacter(CmdContext *context, ContainerID id, const char *destobjectdb)
{
	OutgoingIntershardTransferRequest *pRequest;
	Container *character;

	// Check for existence before going any further

	servLog(LOG_CONTAINER, "InitiateTransfer", "CharacterID %u Destination \"%s\"", id, NULL_TO_EMPTY(destobjectdb));

	PERFINFO_AUTO_START("Get container", 1);
	character = objGetContainer(GLOBALTYPE_ENTITYPLAYER, id);
	PERFINFO_AUTO_STOP();

	if (!character)
	{
		servLog(LOG_CONTAINER, "TransferFailed", "CharacterID %u Destination \"%s\" Reason \"Non-existent character\"", id, destobjectdb);
		return "-10";
	}

	PERFINFO_AUTO_START("Enqueue", 1);
	if(stashIntFindPointer(gAllOutgoingIntershardTransferRequests, id, &pRequest))
	{
		OutgoingIntershardTransferNetLink *pLinkStruct = NULL;
		stashFindPointer(gOutgoingIntershardTransferNetLinks, pRequest->pDestinationName, &pLinkStruct);
		// Check whether to ignore or overwrite
		if(pRequest->bDone || (pLinkStruct && pLinkStruct->iLastIncomingMessageTimeStamp > pRequest->iRequestTime))
		{
			if(pRequest->eTransferStatus == IST_SENT && pLinkStruct)
			{
				--pLinkStruct->iCurrentSentCharacters;
			}

			pRequest->sourceID = id;
			pRequest->eTransferStatus = IST_QUEUED;
			pRequest->pDestinationName = allocAddString(destobjectdb);
			pRequest->iRequestTime = timeSecondsSince2000();
			pRequest->bDone = false;
			qEnqueue(gOutgoingIntershardTransferQueue, pRequest);
		}
		else
		{
			//return IST_ALREADYAREQUEST;
			servLog(LOG_CONTAINER, "TransferFailed", "CharacterID %u Destination \"%s\" Reason \"Transfer already pending\"", id, destobjectdb);
			PERFINFO_AUTO_STOP();
			return "-13";
		}
	}
	else
	{
		pRequest = (OutgoingIntershardTransferRequest*)calloc(sizeof(OutgoingIntershardTransferRequest), 1);
		pRequest->sourceID = id;
		pRequest->eTransferStatus = IST_QUEUED;
		pRequest->pDestinationName = allocAddString(destobjectdb);
		pRequest->iRequestTime = timeSecondsSince2000();

		stashIntAddPointer(gAllOutgoingIntershardTransferRequests, pRequest->sourceID, pRequest, true);
		qEnqueue(gOutgoingIntershardTransferQueue, pRequest);
	}
	PERFINFO_AUTO_STOP();

	//return IST_QUEUED;
	return "1";
}

typedef struct ISTComplete
{	//I should probably get the char@accountname and stick it in here.
	ContainerID sourceID;
	U32 requestTime;
	ContainerID destinationID;
	U32 linkID;
}ISTComplete;

static void ReceiveIntershardTransfer_CB(TransactionReturnVal *returnVal, ISTComplete *completion)
{
	NetLink *link = linkFindByID(completion->linkID);
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		if (linkConnected(link) && !linkDisconnected(link))
		{
			Packet *pak = pktCreate(link, SHAREDCMD_MAX);	//the command doesn't matter here since we're overriding the handler
			pktSendU32(pak, completion->sourceID);
			pktSendU32(pak, completion->requestTime);
			pktSendU32(pak, IST_SUCCESS);
			pktSendU32(pak, completion->destinationID);
			pktSend(&pak);
		}
		log_printf(LOG_CONTAINER, "Received intershard character transfer EntityPlayer[%u]", completion->destinationID);
	}
	else
	{
		if (linkConnected(link) && !linkDisconnected(link))
		{
			Packet *pak = pktCreate(link, SHAREDCMD_MAX);	//the command doesn't matter here since we're overriding the handler
			pktSendU32(pak, completion->sourceID);
			pktSendU32(pak, completion->requestTime);
			pktSendU32(pak, IST_TRANSACTIONFAILURE);
			pktSend(&pak);
		}
		ErrorOrAlert("OBJTRANS.CHARACTERTRANSFERFAIL", "Failed intershard character transfer transaction for EntityPlayer[%u]", completion->destinationID);
		log_printf(LOG_OBJTRANS, "Failed intershard character transfer transaction for EntityPlayer[%u]", completion->destinationID);
		
		//TODO:This should really get a container list from dbLoadEntity and remove all the added containers!
	}

	free(completion);
}

//add and transact the parsed characterdata on the destination server
static void mt_dbIntershardTransferCharacterCommit(void *user_data, IncomingIntershardTransferRequest **itr, WTCmdPacket *packet)
{
	if (itr)
	{
		if (itr[0]->ee)
		{
			TransactionRequest *request = objCreateTransactionRequest();
			ContainerID conid = 0;
			if (conid = dbLoadEntity(GLOBALTYPE_ENTITYPLAYER, itr[0]->ee, request, 0, NULL, NULL, gOverwriteAccountDataOnReceive))
			{
				ISTComplete *completion = (ISTComplete*)calloc(1,sizeof(ISTComplete));
				completion->linkID = itr[0]->linkID;
				completion->destinationID = conid;
				completion->sourceID = itr[0]->sourceID;
				completion->requestTime = itr[0]->requestTime;

				objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC, 
					objCreateManagedReturnVal(ReceiveIntershardTransfer_CB, completion), 
					"ForeignShardEntityReceived", request);
			}
			else
			{
				NetLink *link = linkFindByID(itr[0]->linkID);
				if (linkConnected(link) && !linkDisconnected(link))
				{
					Packet *pak = pktCreate(link, SHAREDCMD_MAX);	//the command doesn't matter here since we're overriding the handler
					pktSendU32(pak, itr[0]->sourceID);
					pktSendU32(pak, itr[0]->requestTime);
					pktSendU32(pak, IST_LOADFAILURE);
					pktSend(&pak);
				}
				ErrorOrAlert("OBJECTDB_TRANSFER_FAILED", "Could not load imported character %u", itr[0]->sourceID);
			}
			objDestroyTransactionRequest(request);
			dbDestroyEntityExport(itr[0]->ee);
			itr[0]->ee = NULL;
		}
		else
		{
			NetLink *link = linkFindByID(itr[0]->linkID);
			if (linkConnected(link) && !linkDisconnected(link))
			{
				Packet *pak = pktCreate(link, SHAREDCMD_MAX);	//the command doesn't matter here since we're overriding the handler
				pktSendU32(pak, itr[0]->sourceID);
				pktSendU32(pak, itr[0]->requestTime);
				pktSendU32(pak, IST_PARSERFAILURE);
				pktSend(&pak);
			}
			ErrorOrAlert("OBJECTDB_TRANSFER_FAILED", "Could not parse imported character %u", itr[0]->sourceID);
		}
		estrDestroy(&itr[0]->characterData);
		free(itr[0]);
	}
}
//parse the character data on the destination shard and submit it back to the main thread for processing
static void wt_dbIntershardTransferCharacterRecv(void *user_data, IncomingIntershardTransferRequest **request, WTCmdPacket *packet)
{
	request[0]->ee = dbParseCharacterEntity(request[0]->characterData);
	wtQueueMsg(wtIntershardTransfer, ISTTHREAD_MSGRECVCOMMIT, request, sizeof(IncomingIntershardTransferRequest));
}

//Unpack the sent character on the destination shard
static void ReceiveIntershardTransfer(Packet *pak, NetLink *link)
{
	U32 sourceID;
	U32 requestTime;
	char prodname[32];
	char *chardata;
	char *sourceShardName = NULL;
	char *sourceShardCategory = NULL;
	
	IncomingIntershardTransferRequest *request;
	
	sourceID = pktGetU32(pak);
	requestTime = pktGetU32(pak);
	pktGetString(pak,prodname,32);
	chardata = pktGetStringTemp(pak);
	if(!pktEnd(pak))
	{
		sourceShardName = pktGetStringTemp(pak);
		sourceShardCategory = pktGetStringTemp(pak);
	}

	if (stricmp(prodname, GetShortProductName()))
	{
		if (linkConnected(link))
		{
			Packet *pkt = pktCreate(link, SHAREDCMD_MAX);	//the command doesn't matter here since we're overriding the handler
			pktSendU32(pkt, sourceID);
			pktSendU32(pkt, requestTime);
			pktSendU32(pkt, IST_PRODUCTMISMATCH);
			pktSend(&pkt);
		}
		if(sourceShardName)
			ErrorOrAlert("OBJECTDB_TRANSFER_FAILED", "%s shard received character from %s(%s) a %s shard", GetShortProductName(), sourceShardName, sourceShardCategory, prodname);
		else
			ErrorOrAlert("OBJECTDB_TRANSFER_FAILED", "%s shard received character from a %s shard", GetShortProductName(), prodname);
		return;
	}

	if (!ipfIsIpInGroup("TrustedObjectDB", linkGetIp(link)))
	{	//compare link IP to list of allowed source shards.
		if (linkConnected(link))
		{
			Packet *pkt = pktCreate(link, SHAREDCMD_MAX);	//the command doesn't matter here since we're overriding the handler
			pktSendU32(pkt, sourceID);
			pktSendU32(pkt, requestTime);
			pktSendU32(pkt, IST_UNTRUSTEDSOURCE);
			pktSend(&pkt);
		}
		if(sourceShardName)
			ErrorOrAlert("OBJECTDB_TRANSFER_FAILED", "Received character transfer from untrusted source %s(%s)", sourceShardName, sourceShardCategory);
		else
			ErrorOrAlert("OBJECTDB_TRANSFER_FAILED", "Received character transfer from untrusted source");
		return;
	}

	request = (IncomingIntershardTransferRequest*)calloc(1,sizeof(IncomingIntershardTransferRequest));
	estrPrintf(&request->characterData,"%s", chardata);
	request->linkID = linkID(link);
	request->sourceID = sourceID;
	request->requestTime = requestTime;

	wtQueueCmd(wtIntershardTransfer, ISTTHREAD_CMDCHARACTERRECV, &request, sizeof(IncomingIntershardTransferRequest*));
}

static void IntershardHandleMessage(Packet* pak, int cmd, NetLink* link, void *userData)
{
	PERFINFO_AUTO_START_FUNC();
	switch (cmd)
	{
	case TO_OBJECTDB_TRANSFER_CHARACTER: ReceiveIntershardTransfer(pak, link); break;
	default: break;
	}
	PERFINFO_AUTO_STOP();
}

extern ContainerSource gContainerSource;

void dbThrottledSaveCallback(TimedCallback* cb, F32 timeSinceLastCallback, UserData userData)
{
	int i;
	int count = 0;
	char updateStr[256];

	PERFINFO_AUTO_START_FUNC();

	verbose_printf("objThrottledSaveCallback: %d containers left\n", eaSize(&gContainerSource.throttledSaveQueue));

	for (i = eaSize(&gContainerSource.throttledSaveQueue)-1; i >= 0 && count < 1000; count++,i--)
	{
		ContainerRef *ref = gContainerSource.throttledSaveQueue[i];
		sprintf(updateStr, "dbWriteContainer %d %d", ref->containerType, ref->containerID);
		dbHandleDatabaseUpdateString(updateStr, 0, 0);
		eaRemoveFast(&gContainerSource.throttledSaveQueue, i);
		StructDestroy(parse_ContainerRef, ref);
	}

	if (!eaSize(&gContainerSource.throttledSaveQueue))
	{
		eaDestroy(&gContainerSource.throttledSaveQueue);
		TimedCallback_Remove(cb);
	}

	PERFINFO_AUTO_STOP();
}

void dbMasterMoveToOffline_Enter(void)
{
	if(gDatabaseConfig.bEnableOfflining)
	{
		loadstart_printf("Offlining characters...\n");
		log_printf(LOG_CONTAINER,"Starting offlining");
		CreateOffliningQueue(ObjectDBDoingEmergencyRestart());
		log_printf(LOG_CONTAINER,"Queued %u containers for offlining", TotalEntityPlayersToOffline());
	}
}

void dbMasterMoveToOffline_BeginFrame(void)
{
	if(IsMovingToOfflineHoggDone())
	{
		if(gDatabaseConfig.bEnableOfflining)
		{
			fprintf(fileGetStderr(),"\r%u/%u containers offlined. %u failures.", EntityPlayersOfflined(), TotalEntityPlayersToOffline(), OfflineFailures());
			loadend_printf("done");
			log_printf(LOG_CONTAINER,"Done offlining containers");
		}
		GSM_SwitchToSibling(DBSTATE_MASTER_CLEAN_UP_OFFLINE_HOGG,false);
	}
	else
	{
		ProcessOffliningQueue();
		fprintf(fileGetStderr(),"\r%u/%u containers offlined. %u failures.", EntityPlayersOfflined(), TotalEntityPlayersToOffline(), OfflineFailures());
	}
}

void dbMasterCleanUpOffline_Enter(void)
{
	if(gDatabaseConfig.bEnableOfflining)
	{
		loadstart_printf("Cleaning up offline.hogg...\n");
		log_printf(LOG_CONTAINER,"Starting cleaning up offline.hogg");
		QueueOfflineHoggCleanUp(ObjectDBDoingEmergencyRestart());
		log_printf(LOG_CONTAINER,"Queued %u characters for cleanup from offline.hogg", TotalOfflineContainersToCleanup());
	}
}

void dbMasterCleanUpOffline_BeginFrame(void)
{
	if(IsCleaningUpOfflineHoggDone())
	{
		if (gDatabaseConfig.bEnableOfflining)
		{
			ReOpenOfflineHoggForRestores();

			LaunchOfflineBackupScript();
			fprintf(fileGetStderr(),"\r%u/%u containers cleaned up.", ContainersCleanedUp(), TotalOfflineContainersToCleanup());
			loadend_printf("done");
			log_printf(LOG_CONTAINER,"Done processing cleanup of offline.hogg");
		}

		GSM_SwitchToSibling(DBSTATE_MASTER_HANDLE_REQUESTS,false);
	}
	else
	{
		ProcessCleanUpQueue();
		fprintf(fileGetStderr(),"\r%u/%u containers cleaned up.", ContainersCleanedUp(), TotalOfflineContainersToCleanup());
	}
}

void dbMasterHandleRequests_Enter(void)
{
	//Start listening for intershard communication
	if (!commListen(commDefault(), LINKTYPE_SHARD_NONCRITICAL_10MEG, 0, DEFAULT_OBJECTDB_TRANSFER_PORT, 
		IntershardHandleMessage,
		NULL,//IntershardConnectCallback,
		NULL,//IntershardDisconnectCallback,
		0))
	{
		ErrorOrAlert("OBJECTDB_INTERSHARD_LINK_FAILED", "Could not commListen on default objectdb transfer port.");
	}

	//Setup Intershard transfer worker thread. This thread connects to a foreignshard and sends the data.
	wtIntershardTransfer = wtCreateEx(1024, 32, NULL, "IntershardTransferThread", true);
	wtRegisterCmdDispatch(wtIntershardTransfer, ISTTHREAD_CMDSTARTCOMM, wt_dbIntershardTransferCreateComm);
	//Receiving shard
	wtRegisterCmdDispatch(wtIntershardTransfer, ISTTHREAD_CMDCHARACTERRECV, wt_dbIntershardTransferCharacterRecv);
	wtRegisterMsgDispatch(wtIntershardTransfer, ISTTHREAD_MSGRECVCOMMIT, mt_dbIntershardTransferCharacterCommit);

	wtSetProcessor(wtIntershardTransfer, THREADINDEX_NETSEND);
	wtSetThreaded(wtIntershardTransfer, true, 0, false);
	wtSetSkipIfFull(wtIntershardTransfer, true);
	wtStart(wtIntershardTransfer);
	wtQueueCmd(wtIntershardTransfer, ISTTHREAD_CMDSTARTCOMM, NULL, 0);

	initOutgoingIntershardTransfer();

	initContainerRestoreThread();

	if ((gDatabaseState.databaseType == DBTYPE_MASTER || gDatabaseState.databaseType == DBTYPE_STANDALONE)
		&& eaSize(&gContainerSource.throttledSaveQueue))
	{
		TimedCallback_Add(dbThrottledSaveCallback, NULL, 60.f);
	}

	TimedCallback_Add(ExpireCachedDeletes, NULL, 1.0);
}

#define OBJECTDB_DIR "objectdb"
#define CLONEDB_DIR "cloneobjectdb"

char *dbDataDir(void)
{
	static char offline_dir[MAX_PATH];
	char *custom_dir = NULL;
	char *last_dir = NULL;
	bool isClone = false;

	if (IsThisCloneObjectDB() || gDatabaseState.bCloneCreateSnapshot)
		isClone = true;

	if (!gDatabaseConfig.pDatabaseDir && gDatabaseConfig.CLDatabaseDir[0])
		gDatabaseConfig.pDatabaseDir = strdup(gDatabaseConfig.CLDatabaseDir);

	if (!gDatabaseConfig.pCloneDir && gDatabaseConfig.CLCloneDir[0])
		gDatabaseConfig.pCloneDir = strdup(gDatabaseConfig.CLCloneDir);


	if (!offline_dir[0])
	{
		if (isClone)
		{
			custom_dir = gDatabaseConfig.pCloneDir;
			if (!custom_dir && gDatabaseConfig.pDatabaseDir) custom_dir = gDatabaseConfig.pDatabaseDir;
			last_dir = CLONEDB_DIR;
		}
		else
		{
			custom_dir = gDatabaseConfig.pDatabaseDir;
			last_dir = OBJECTDB_DIR;
		}

		if (custom_dir)
		{
			concatpath(custom_dir, OBJECTDB_DIR, offline_dir);
			forwardSlashes(offline_dir);
			
			if (isClone)
			{
				char pathbuf[MAX_PATH];
				concatpath(custom_dir, last_dir, pathbuf);
				forwardSlashes(pathbuf);

				//if clone path exists
				if (dirExists(pathbuf))
				{
					sprintf(offline_dir, "%s", pathbuf);
				}
				//or default path also does not exists
				else if (!dirExists(offline_dir))
				{
					sprintf(offline_dir, "%s", pathbuf);
				}
				//otherwise use the default path if it exists.
			}
			mkdirtree(offline_dir);
		}
		else
		{
			if (isClone)
			{
				if (!fileMakeSpecialDir(true,last_dir, SAFESTR(offline_dir)))
				{
					if (!fileMakeSpecialDir(false, OBJECTDB_DIR, SAFESTR(offline_dir)))
					{
						fileSpecialDir(last_dir, SAFESTR(offline_dir));
					}
				}
			}
			else
			{
				fileSpecialDir(last_dir, SAFESTR(offline_dir));
			}
		}
	}
	return offline_dir;
}

char *dbDataHogFile(void)
{
	static char offline_dir[MAX_PATH];
	if (!offline_dir[0]) {
		sprintf(offline_dir, "%s", dbDataDir());
		strcat(offline_dir, "/db.hogg");
	}
	return offline_dir;
}

void dbCleanup(void)
{
	FlushGenericDatabaseThreads(true);
	objCloseContainerSource();

	svrLogSetSystemIsShuttingDown();

	GSM_Quit("dbCleanup");
}

void dbLogTimeInState(void)
{
	float time = GSM_TimeInStateIncludeCurrentFrame(NULL);
	SERVLOG_PAIRS(LOG_OBJECTDB_PERF, "TimeInState", 
		("state", "\"%s\"", GSM_GetCurActiveStateName())
		("duration", "%f", time)
		("dbtype", "%u", GetAppGlobalType())
		("dbid", "%u", GetAppGlobalID()));
}

AUTO_RUN;
void dbInitStates(void)
{
	GSM_AddGlobalState(DBSTATE_INIT);
	GSM_AddGlobalStateCallbacks(DBSTATE_INIT,NULL,dbInit_RunOnce,NULL,NULL);

	GSM_AddGlobalState(DBSTATE_MASTER);
	GSM_AddGlobalStateCallbacks(DBSTATE_MASTER,dbMaster_Enter,dbMaster_BeginFrame,NULL,NULL);

	GSM_AddGlobalState(DBSTATE_MASTER_LOAD);
	GSM_AddGlobalStateCallbacks(DBSTATE_MASTER_LOAD,NULL,dbMasterLoad_RunOnce,NULL,dbLogTimeInState);

	GSM_AddGlobalState(DBSTATE_MASTER_MOVE_TO_OFFLINE_HOGG);
	GSM_AddGlobalStateCallbacks(DBSTATE_MASTER_MOVE_TO_OFFLINE_HOGG,dbMasterMoveToOffline_Enter,dbMasterMoveToOffline_BeginFrame,NULL,dbLogTimeInState);

	GSM_AddGlobalState(DBSTATE_MASTER_CLEAN_UP_OFFLINE_HOGG);
	GSM_AddGlobalStateCallbacks(DBSTATE_MASTER_CLEAN_UP_OFFLINE_HOGG,dbMasterCleanUpOffline_Enter,dbMasterCleanUpOffline_BeginFrame,NULL,dbLogTimeInState);

	GSM_AddGlobalState(DBSTATE_HOG_SNAPSHOT_MERGER);
	GSM_AddGlobalStateCallbacks(DBSTATE_HOG_SNAPSHOT_MERGER, NULL, dbMasterLoad_RunOnce, NULL, dbLogTimeInState);

	GSM_AddGlobalState(DBSTATE_DUMPER);
	GSM_AddGlobalStateCallbacks(DBSTATE_DUMPER,NULL, dbDumper_DumpFile, NULL,NULL);

	GSM_AddGlobalState(DBSTATE_SCANNER);
	GSM_AddGlobalStateCallbacks(DBSTATE_SCANNER, NULL, dbScanner_ScanFile, NULL, NULL);

	GSM_AddGlobalState(DBSTATE_REPLAYER);
	GSM_AddGlobalStateCallbacks(DBSTATE_REPLAYER, dbReplayer_ReplayLogsEnter, dbReplayer_ReplayLogs, NULL, NULL);

	GSM_AddGlobalState(DBSTATE_MASTER_HANDLE_REQUESTS);
	GSM_AddGlobalStateCallbacks(DBSTATE_MASTER_HANDLE_REQUESTS,dbMasterHandleRequests_Enter,dbMasterHandleRequests_BeginFrame,NULL,NULL);

	GSM_AddGlobalState(DBSTATE_MASTER_RUNNING_TEST);
	GSM_AddGlobalStateCallbacks(DBSTATE_MASTER_RUNNING_TEST,NULL,NULL,NULL,NULL);

	GSM_AddGlobalState(DBSTATE_CLEANUP);
	GSM_AddGlobalStateCallbacks(DBSTATE_CLEANUP,dbCleanup,NULL,NULL,NULL);

	slSetGSMReportsStateToController();
}


typedef struct DatabaseVersionTransition
{
	U32 resultVersion;
	VersionTransitionFunction *loadedTransitionFunction;
	VersionTransitionFunction *invalidTransitionFunction;
} DatabaseVersionTransition;

DatabaseVersionTransition **gVersionTransitions;

void dbSetDatabaseCodeVersion(U32 version)
{
	assertmsg(!gCurrentDatabaseVersion,"dbSetDatabaseCodeVersion can only be called once!");
	gCurrentDatabaseVersion = version;
}

DatabaseVersionTransition *getVersionTransition(U32 version)
{
	int i;
	for (i = eaSize(&gVersionTransitions) - 1; i >= 0; i--)
	{
		if (gVersionTransitions[i]->resultVersion == version)
		{
			return gVersionTransitions[i];
		}
	}
	return NULL;
}

void dbRegisterVersionTransition(U32 version, VersionTransitionFunction loadedFunction, VersionTransitionFunction invalidFunction)
{
	DatabaseVersionTransition *transition;
	assertmsg(!getVersionTransition(version),"Only one version transition function can be registered per version!");
	assertmsg(loadedFunction || invalidFunction, "You must register at least one transition function for a valid transition");

	if (!version)
	{
		return;
	}

	transition = calloc(sizeof(DatabaseVersionTransition),1);
	transition->resultVersion = version;
	transition->loadedTransitionFunction = loadedFunction;
	transition->invalidTransitionFunction = invalidFunction;
	eaPush(&gVersionTransitions,transition);
}

void dbTransitionDeleteLoadedContainer(GlobalType type, ContainerID id)
{
	objRemoveContainerFromRepository(type,id);
}

void dbTransitionDeleteInvalidContainer(GlobalType type, ContainerID id)
{
	objDeleteInvalidContainer(type, id);
}

ErrorMessage **ppLoadingErrors;

static CRITICAL_SECTION loadErrorfCriticalSection;

void dbLoadErrorfCallback(ErrorMessage* errMsg, void *userdata)
{
	ErrorMessage *msgClone;
	char *errString;

	/*if (errMsg->bForceShow && !errMsg->bReport)
	{
		ErrorfPopCallback();
		ErrorfCallCallback(errMsg);
		ErrorfPushCallback(dbLoadErrorfCallback, userdata);
		return;
	}*/

	msgClone = StructClone(parse_ErrorMessage, errMsg);
	EnterCriticalSection(&loadErrorfCriticalSection);
	eaPush(&ppLoadingErrors, msgClone);
	LeaveCriticalSection(&loadErrorfCriticalSection);

	errMsg->bRelevant = false;
	errMsg->bReport = false;

	errString = errorFormatErrorMessage(errMsg);

	log_printf(LOG_CONTAINER, "%s", errString);
}

int dbConfigureDatabase(void)
{
	if (gDatabaseState.bCreateSnapshot)
		gDatabaseConfig.iIncrementalInterval = INT_MAX;
	if (gDatabaseConfig.bNoHogs)
	{
		objSetContainerSourceToDirectory(dbDataDir());
	} else {
		hogSetGlobalOpenMode(HogSafeAgainstAppCrash);
		hogSetMaxBufferSize(DBLIB_HOG_BUFFER_SIZE);
		objSetContainerSourceToHogFile(dbDataHogFile(), gDatabaseConfig.iIncrementalInterval, dbLogRotateCB, dbLogCloseCB);
		objSetSnapshotMode(gDatabaseState.bCreateSnapshot);
		objSetMultiThreadedLoadThreads(gDatabaseState.loadThreads);
		if (gDatabaseState.dumpWebData[0])
			objSetDumpWebDataMode(gDatabaseState.dumpWebData);
	}

	objSetIncrementalHoursToKeep(gDatabaseConfig.iIncrementalHoursToKeep);
	objSetCommandReplayCallback(dbHandleDatabaseReplayString);

	//setup named fetches. There might be a better place to do this, but here is fine for now.
	eaCreate(&namedfetches);
	eaIndexedEnable(&namedfetches, parse_XMLRPCNamedFetchWrapper);

	return 1;
}

void ForEachContainer_AddToTransitionArray(Container *con, ContainerRefArray *refArray)
{
	AddToContainerRefArray(refArray,con->containerType,con->containerID);
}

int dbLoadEntireDatabase(void)
{
	DatabaseInfo *dbInfo = StructCreate(parse_DatabaseInfo);
	char filename[MAX_PATH];
	sprintf(filename,"%s/DB.info",dbDataDir());

	ParserLoadFiles(NULL,filename,NULL,PARSER_OPTIONALFLAG,parse_DatabaseInfo,dbInfo);

	if (dbInfo->ProductName && dbInfo->ProductName[0])
	{
		assertmsg(stricmp(dbInfo->ProductName,GetProductName()) == 0,"Data ProductName and code ProductName don't match!");
	}

	if (gDatabaseState.bCreateSnapshot && gDatabaseConfig.bFastSnapshot)
	{
		objMergeIncrementalHogs(gDatabaseConfig.bBackupSnapshot, gDatabaseConfig.bAppendSnapshot, true);
		if(gDatabaseState.bDefragAfterMerger)
		{
			objDefragLatestSnapshot();
		}
		svrLogFlush(true);
		return 1;
	}

	if(gDatabaseConfig.bEnableOfflining)
	{
		bool bCreated = false;
		HogFile *hogfile = NULL;
		hogfile = OpenOfflineHogg(true, &bCreated);

		if(!hogfile)
		{
			AssertOrAlert("OPENING_OFFLINE_HOG", "Unable to open offline.hogg");
			gDatabaseConfig.bEnableOfflining = false;
		}

		if(bCreated && isProductionMode())
		{
			ErrorOrCriticalAlert("OPENING_OFFLINE_HOG", "No offline.hog found. Creating a new one.");
		}
	}

	InitializeCriticalSection(&loadErrorfCriticalSection);

	ErrorfPushCallback(dbLoadErrorfCallback, NULL);

	objLoadAllContainers();

	ErrorfPopCallback();

	AlertIfStringCacheIsNearlyFull();

	if (dbInfo->DatabaseVersion != gCurrentDatabaseVersion && dbInfo->DatabaseVersion)
	{
		U32 curVersion;
		ContainerRefArray *refArray;
		assertmsg(gDatabaseState.databaseType == DBTYPE_STANDALONE, "Data version doesn't match code version, and only a STANDALONE type can migrate data!");

		refArray = CreateContainerRefArray();

		for (curVersion = dbInfo->DatabaseVersion + 1; curVersion <= gCurrentDatabaseVersion; curVersion++)
		{
			ContainerIterator iter = {0};
			DatabaseVersionTransition *transition = getVersionTransition(curVersion);
			int i;
			eaClear(&refArray->containerRefs);

			if (!transition)
			{
				continue;
			}
			if (transition->loadedTransitionFunction)
			{
				ForEachContainerInRepository(ForEachContainer_AddToTransitionArray, refArray, false);

				if (eaSize(&refArray->containerRefs))
				{	
					Alertf("Transitioning Database from version %d to version %d. This will result in changes to characters.",curVersion -1,curVersion);
				}

				for (i = 0; i < eaSize(&refArray->containerRefs); i++)
				{
					transition->loadedTransitionFunction(refArray->containerRefs[i]->containerType,refArray->containerRefs[i]->containerID);
				}
			}
			if (transition->invalidTransitionFunction)
			{
				objLockGlobalContainerRepository();
				if (eaSize(&gContainerRepository.invalidContainers))
				{
					Alertf("Transitioning Database from version %d to version %d. This WILL delete characters that are invalid.",curVersion -1,curVersion);
				}

				for (i = 0; i < eaSize(&gContainerRepository.invalidContainers); i++)
				{
					transition->invalidTransitionFunction(gContainerRepository.invalidContainers[i]->containerType,gContainerRepository.invalidContainers[i]->containerID);
					objDestroyContainer(gContainerRepository.invalidContainers[i]);
				}

				eaDestroy(&gContainerRepository.invalidContainers);
				objUnlockGlobalContainerRepository();
			}

		}

		DestroyContainerRefArray(refArray);

		objContainerSaveTick();
	}
	else
	{
		int i;
		EnterCriticalSection(&loadErrorfCriticalSection);
		if (eaSize(&ppLoadingErrors))
		{		
			Alertf("Errors found during load:\n");	
			for (i = 0; i < eaSize(&ppLoadingErrors); i++)
			{
				if(ppLoadingErrors[i])
				{
					ppLoadingErrors[i]->bRelevant = 1;
					ppLoadingErrors[i]->bReport = 1;
					ErrorfCallCallback(ppLoadingErrors[i]);
				}
			}
			eaDestroyStruct(&ppLoadingErrors, parse_ErrorMessage);
		}
		LeaveCriticalSection(&loadErrorfCriticalSection);

		objLockGlobalContainerRepository();
		if (eaSize(&gContainerRepository.invalidContainers))
		{
			TriggerAlertf("OBJECTDB.DATA_CORRUPTION",ALERTLEVEL_CRITICAL,ALERTCATEGORY_NETOPS,0, 0, 0, GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0,
				"ObjectDB failed to load containers. Individual alerts will follow.");
			for (i = 0; i < eaSize(&gContainerRepository.invalidContainers); i++)
			{
				log_printf(LOG_CONTAINER,"Invalid container id:%u type:%d\n", gContainerRepository.invalidContainers[i]->containerID, gContainerRepository.invalidContainers[i]->containerType);
				if (i < 500)
				{
					TriggerAlertf("OBJECTDB.DATA_CORRUPTION",ALERTLEVEL_WARNING,ALERTCATEGORY_NETOPS,0, gContainerRepository.invalidContainers[i]->containerType, gContainerRepository.invalidContainers[i]->containerID, GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0,
						"Invalid container id:%u type:%d\n", gContainerRepository.invalidContainers[i]->containerID, gContainerRepository.invalidContainers[i]->containerType);
				}
				objDestroyContainer(gContainerRepository.invalidContainers[i]);
			}

			eaDestroy(&gContainerRepository.invalidContainers);
		}
		objUnlockGlobalContainerRepository();
	}

	dbSaveDatabaseInfo();
	objContainerLoadingFinished();

	return 1;
}

void dbSaveDatabaseInfo(void)
{
	char filename[MAX_PATH];
	DatabaseInfo dbInfo = {0};
	strcpy(dbInfo.ProductName,GetProductName());
	dbInfo.DatabaseVersion = gCurrentDatabaseVersion;

	sprintf(filename,"%s/DB.info",dbDataDir());

	ParserWriteTextFile(filename,parse_DatabaseInfo,&dbInfo,0,0);
}

void dbSendGlobalInfo(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	DatabaseGlobalInfo info = {0};

	info.iDBThreadQueue = GetOutstandingGenericDatabaseRequestCount();

	info.iUpdateBytesMax = IntAverager_Query(gDatabaseState.DBUpdateSizeAverager, AVERAGE_MAX);
	info.iBytesPerUpdate = IntAverager_Query(gDatabaseState.DBUpdateSizeAverager, AVERAGE_MINUTE);
	info.iUpdatesPerSec = CountAverager_Query(gDatabaseState.DBUpdateCountAverager, AVERAGE_MINUTE);
	info.iTransfersPerSec = CountAverager_Query(gDatabaseState.DBUpdateTransferAverager, AVERAGE_MINUTE);

	//info.fMeanServiceTime = MeanServiceTime();
	//info.iPendingWrites = PendingWrites();

	info.bHogsEnabled = !gDatabaseConfig.bNoHogs;
	info.iLastRotateTime = objGetLastIncrementalRotation();
	info.iSnapshotTime = gDatabaseState.lastSnapshotTime;

	info.iTotalPlayers = objCountTotalContainersWithType(GLOBALTYPE_ENTITYPLAYER);
	// Active is all players the db DOESN'T own, but does know about
	info.iActivePlayers = info.iTotalPlayers - objCountOwnedContainersWithType(GLOBALTYPE_ENTITYPLAYER);
	// Players in the deleted list
	info.iDeletedPlayers = objCountDeletedContainersWithType(GLOBALTYPE_ENTITYPLAYER);

	info.iOfflinePlayers = GetTotalOfflineCharacters();

	info.iLastReplicatedSequenceNumber = gStatus.iLastReplicatedSequenceNumber;
	info.iLastReplicatedTimeStamp = gStatus.iLastReplicatedTimeStamp;

	info.iTotalSubscriptions = gStatus.iTotalSubscriptions;
	info.fSubscriptionsPerPlayer = gStatus.fSubscriptionsPerPlayer;

	if(gStatus.ppContainerStorageInfo)
	{
		int index;
		ObjectDBStatus_StorageInfo *stInfo;
		index = eaIndexedFindUsingInt(&gStatus.ppContainerStorageInfo, GLOBALTYPE_ENTITYPLAYER);
		if(index >= 0)
		{
			stInfo = gStatus.ppContainerStorageInfo[index];
			info.iUnpackedPlayers = stInfo->UnpackedCount;
		}
	}
	info.iOnlinedPlayers = gStatus.PlayerStorageInfo.OnlinedPlayers;
	
	RemoteCommand_HereIsDBGlobalInfo(GLOBALTYPE_CONTROLLER, 0, objServerType(), objServerID(), &info);
}

//when a playerBooter request gets to the object DB, it means the booting has succeeded
void OVERRIDE_LATELINK_AttemptToBootPlayerWithBooter(ContainerID iPlayerToBootID, U32 iHandle, char *pReason)
{
	PlayerBooterAttemptReturn(iHandle, true, "Player %u now on the object db", iPlayerToBootID);
}

//non command innards
XMLRPCNamedFetch *dbGetNamedFetchInternal(char *name, GlobalType type)
{
	static XMLRPCNamedFetchKey key = {0};
	int index;

	if (!type) type = GLOBALTYPE_ENTITYPLAYER;
	key.name = name;
	key.type = type;

	index = eaIndexedFind(&namedfetches, &key);
	if (index >= 0)
	{
		XMLRPCNamedFetchWrapper *w = eaGet(&namedfetches, index);
		return w->fetch;
	}
	else
	{
		return NULL;
	}
}


AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool DbSetNamedFetch(CmdContext *context, XMLRPCNamedFetch *namedFetch)
{
	XMLRPCNamedFetchWrapper *w;
	static XMLRPCNamedFetchKey key = {0};
	int existing = -1;
	if (context->eHowCalled != CMD_CONTEXT_HOWCALLED_XMLRPC)
	{
		Errorf("DbSetNamedFetch may only be called via XMLRPC. howCalled = %d", context->eHowCalled);
		return false;
	}
	if (!namedFetch->name || !namedFetch->name[0])
	{
		Errorf("Cannot set a named fetch without a name.");
		return false;
	}

	if (!namedFetch->ppFetchColumns || eaSize(&namedFetch->ppFetchColumns) == 0)
	{
		Errorf("Cannot set a named fetch without fetch columns.");
		return false;
	}

	if (!namedFetch->type)
	{	//default to entity player
		namedFetch->type = GLOBALTYPE_ENTITYPLAYER;
	}

	key.name = namedFetch->name;
	key.type = namedFetch->type;
	if ((existing = eaIndexedFind(&namedfetches, &key)) >= 0)
	{
		w = eaRemove(&namedfetches, existing);
		StructDestroy(parse_XMLRPCNamedFetchWrapper, w);
		w = NULL;
	}

	w = StructCreate(parse_XMLRPCNamedFetchWrapper);
	w->fetch = StructClone(parse_XMLRPCNamedFetch, namedFetch);
	w->key.name = w->fetch->name;
	w->key.type = w->fetch->type;
	eaIndexedAdd(&namedfetches, w);
	return true;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
ACMD_STATIC_RETURN XMLRPCNamedFetch *DbGetNamedFetch2(CmdContext *context, char *name, GlobalType type)
{
	XMLRPCNamedFetch *existing = NULL;

	if (context->eHowCalled != CMD_CONTEXT_HOWCALLED_XMLRPC)
	{
		Errorf("DbGetNamedFetch2 may only be called via XMLRPC. howCalled = %d", context->eHowCalled);
		return NULL;
	}

	if (existing = dbGetNamedFetchInternal(name, type))
	{
		return existing;
	}
	else
	{	
		Errorf("DbGetNamedFetch2 could not find named fetch:%s for type:%s", name, GlobalTypeToName(type));
		return NULL;
	}
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
ACMD_STATIC_RETURN XMLRPCNamedFetch *DbGetNamedFetch(CmdContext *context, char *name)
{
	return DbGetNamedFetch2(context, name, GLOBALTYPE_ENTITYPLAYER);
}


void OVERRIDE_LATELINK_GetCommandsForGenericServerMonitoringPage(GenericServerInfoForHttpXpath *pInfo)
{
	estrPrintf(&pInfo->pCommand1, "DebugCheckContainerLoc $SELECT(Find location of container type|ENUM_GlobalType) $INT(ID) $COMMANDNAME(Check Container Location)");
	estrPrintf(&pInfo->pCommand2, "ServerMonDumpEntity $SELECT(Container type|ENUM_GlobalType) $INT(ID) $COMMANDNAME(Dump Container)");
}

U32 accountIDFromEntityPlayerContainer(Container *con)
{
	ParseTable *subtable;
	void *ptr;
	int col;
	int ind;

	if (!con)
		return 0;

	if (con->containerType != GLOBALTYPE_ENTITYPLAYER)
		return 0;

	if (!con->containerData || !con->containerSchema)
		return 0;

	if (ParserResolvePath( ".pplayer.accountid", con->containerSchema->classParse, con->containerData,
		&subtable, &col, &ptr, &ind, NULL, NULL, 0))
		return TokenStoreGetInt(subtable, col, ptr, ind, NULL);
	else
		return 0;
}

void dbAccountIDMakeExportPath(char **estr, U32 accountID)
{
	estrPrintf(estr, "%s/%010u-%010u/%010u-%010u/%010u-%010u/%010u",
		gDatabaseConfig.CLExportDir,
		(accountID & 0xFF000000), ((accountID & 0xFF000000) + 0x01000000),
		(accountID & 0xFFFF0000), ((accountID & 0xFFFF0000) + 0x00010000),
		(accountID & 0xFFFFFF00), ((accountID & 0xFFFFFF00) + 0x00000100),
		accountID);
}

void dbChangeFilePathEstringToHttpPath(char **estr)
{
	char *http = NULL;
	char *file = NULL;
	estrStackCreate(&http);
	estrStackCreate(&file);
	estrPrintf(&http, "/file/%s/%u/filesystem", GlobalTypeToName(objServerType()), objServerID());
	estrPrintf(&file, "%s", objGetContainerSourcePath());
	estrReplaceOccurrences_CaseInsensitive(estr, getDirectoryName(file), http);
	estrDestroy(&http);
	estrDestroy(&file);
}

/*
Performance test for Alex's new objPathParseOperations_Fast... checked in commented out in a checkin related to
IntAveragers just to avoid massive checkin confusion


#include "objPath.h"
#include "StringUtil.h"

extern int objPathParseOperations_Fast(ParseTable *table, const char *query, ObjectPathOperation ***pathOperations);
extern int objPathParseOperations(ParseTable *table, const char *query, ObjectPathOperation ***pathOperations);

bool objPathOperationCompare(ObjectPathOperation *pOperation1, ObjectPathOperation *pOperation2)
{
	if (!!pOperation1 != !!pOperation2)
	{
		return false;
	}

	if (!pOperation1)
	{
		return true;
	}

	if (pOperation1->op != pOperation2->op)
	{
		return false;
	}

	if (stricmp_safe(pOperation1->pathEString, pOperation2->pathEString) != 0)
	{
		return false;
	}

	if (stricmp_safe(pOperation1->valueEString, pOperation2->valueEString) != 0)
	{
		return false;
	}

	return true;


}

void ParseOperationsTest(void)
{
	ObjectPathOperation **ppOperations1 = NULL;
	ObjectPathOperation **ppOperations2 = NULL;
	char *pStr_raw = fileAlloc("c:\\temp\\path_ops.txt", NULL);
	
	char **ppStringsToTest = NULL;
	char *pCurReadHead = pStr_raw;
	char *pCurStringBeingBuilt = NULL;

	S64 siStartTime;
	S64 iTime1, iTime2;
	int i;

	while (1)
	{
		char *pNextEOL = strchr(pCurReadHead, '\n');
		char *pTempString = NULL;
		assert(pNextEOL);
		*pNextEOL = 0;
		
		estrCopy2(&pTempString, pCurReadHead);
		estrTrimLeadingAndTrailingWhitespace(&pTempString);

		if (estrLength(&pTempString) == 0)
		{
			eaPush(&ppStringsToTest, pCurStringBeingBuilt);
			pCurStringBeingBuilt = NULL;
			if (eaSize(&ppStringsToTest) == 1024)
			{
				break;
			}
		}

		estrConcatf(&pCurStringBeingBuilt, "%s%s", estrLength(&pCurStringBeingBuilt) == 0 ? "" : "\n", pTempString);
		pCurReadHead = pNextEOL + 1;
	}

	free(pStr_raw);

	while (1)
	{
		siStartTime = timeGetTime();
		for (i = 0; i < eaSize(&ppStringsToTest); i++)
		{
			objPathParseOperations_Fast(NULL, ppStringsToTest[i], &ppOperations1);

			PERFINFO_AUTO_START("eadestroy", 1);
			eaDestroyEx(&ppOperations1, DestroyObjectPathOperation);
			PERFINFO_AUTO_STOP();
		}
		iTime1 = timeGetTime() - siStartTime;
	}
	
	siStartTime = timeGetTime();
	for (i = 0; i < eaSize(&ppStringsToTest); i++)
	{
		objPathParseOperations(NULL, ppStringsToTest[i], &ppOperations1);
		eaDestroyEx(&ppOperations1, DestroyObjectPathOperation);
	}	
	
	iTime2 = timeGetTime() - siStartTime;


	printf("Time1: %d msecs. Time2: %d msecs\n", (int)iTime1, (int)iTime2);

	siStartTime = timeGetTime();
	for (i = 0; i < eaSize(&ppStringsToTest); i++)
	{
		objPathParseOperations_Fast(NULL, ppStringsToTest[i], &ppOperations1);
		eaDestroyEx(&ppOperations1, DestroyObjectPathOperation);
	}
	iTime1 = timeGetTime() - siStartTime;
	
	siStartTime = timeGetTime();
	for (i = 0; i < eaSize(&ppStringsToTest); i++)
	{
		objPathParseOperations(NULL, ppStringsToTest[i], &ppOperations1);
		eaDestroyEx(&ppOperations1, DestroyObjectPathOperation);
	}	
	
	iTime2 = timeGetTime() - siStartTime;


	printf("Time1: %d msecs. Time2: %d msecs\n", (int)iTime1, (int)iTime2);

}
*/

void AlertIfStringCacheIsNearlyFull(void)
{
	static alreadyAlerted = false;
	if(alreadyAlerted)
		return;

	if(stringCacheIsLocklessReadActive() && stringCacheIsNearlyFull())
	{
		ErrorOrCriticalAlert("OBJECTDB_STRING_CACHE_NEARFULL", "The ObjectDB string cache is 90%% full. If using faststringcache, increase the value.");
		alreadyAlerted = true;
	}
}

ContainerID *GetContainerIDsFromIndexForKey(ObjectIndex *oi, ObjectIndexKey *key)
{
	ContainerID *eaIDs = NULL;
	ObjectIndexHeader **headers = NULL;

	assertmsg(oi->useHeaders, "This only works for a container type with headers, i.e. EntityPlayer");

	objIndexObtainReadLock(oi);
	objIndexCopyEArrayOfKey(oi, &headers, key, false);

	EARRAY_FOREACH_BEGIN(headers, i);
	{
		ea32Push(&eaIDs, headers[i]->containerId);
	}
	EARRAY_FOREACH_END;

	eaDestroy(&headers); // You may NEVER hold on to this array beyond releasing the objIndex lock
	objIndexReleaseReadLock(oi);

	return eaIDs;
}

ContainerID *GetContainerIDsFromAccountID(U32 accountID)
{
	ContainerID *eaIDs = NULL;
	ObjectIndexKey key = {0};

	objIndexInitKey_Int(gAccountID_idx, &key, accountID);
	eaIDs = GetContainerIDsFromIndexForKey(gAccountID_idx, &key);
	objIndexDeinitKey_Int(gAccountID_idx, &key);

	return eaIDs;
}

ContainerID *GetContainerIDsFromAccountName(const char *accountName)
{
	ContainerID *eaIDs = NULL;
	ObjectIndexKey key = {0};

	objIndexInitKey_String(gAccountAccountName_idx, &key, accountName);
	eaIDs = GetContainerIDsFromIndexForKey(gAccountAccountName_idx, &key);
	objIndexDeinitKey_String(gAccountAccountName_idx, &key);

	return eaIDs;
}

ContainerID *GetContainerIDsFromDisplayName(const char *displayName)
{
	ContainerID *eaIDs = NULL;
	ObjectIndexKey key = {0};

	objIndexInitKey_String(gAccountDisplayName_idx, &key, displayName);
	eaIDs = GetContainerIDsFromIndexForKey(gAccountDisplayName_idx, &key);
	objIndexDeinitKey_String(gAccountDisplayName_idx, &key);

	return eaIDs;
}

ContainerID *GetDeletedContainerIDsFromAccountID(U32 accountID)
{
	ContainerID *eaIDs = NULL;
	ObjectIndexKey key = {0};

	objIndexInitKey_Int(gAccountIDDeleted_idx, &key, accountID);
	eaIDs = GetContainerIDsFromIndexForKey(gAccountIDDeleted_idx, &key);
	objIndexDeinitKey_Int(gAccountIDDeleted_idx, &key);

	return eaIDs;
}

ContainerID *GetDeletedContainerIDsFromAccountName(const char *accountName)
{
	ContainerID *eaIDs = NULL;
	ObjectIndexKey key = {0};

	objIndexInitKey_String(gAccountAccountNameDeleted_idx, &key, accountName);
	eaIDs = GetContainerIDsFromIndexForKey(gAccountAccountNameDeleted_idx, &key);
	objIndexDeinitKey_String(gAccountAccountNameDeleted_idx, &key);

	return eaIDs;
}

Container *GetOnlineCharacterFromList(ContainerID *eaIDs, int virtualShard)
{
	EARRAY_INT_CONST_FOREACH_BEGIN(eaIDs, i, n);
	{
		Container *con = objGetContainerEx(GLOBALTYPE_ENTITYPLAYER, eaIDs[i], false, false, true);

		if (!con) continue;

		if (virtualShard > -1 && con->header->virtualShardId != virtualShard)
		{
			objUnlockContainer(&con);
			continue;
		}

		if (con->meta.containerOwnerType == GLOBALTYPE_OBJECTDB)
		{
			objUnlockContainer(&con);
			continue;
		}

		return con;
	}
	EARRAY_FOREACH_END;

	return NULL;
}

ContainerID GetOnlineCharacterIDFromList(ContainerID *eaIDs, int virtualShard)
{
	Container *con = GetOnlineCharacterFromList(eaIDs, virtualShard);
	ContainerID conID = 0;

	if (con)
	{
		conID = con->containerID;
		objUnlockContainer(&con);
	}

	return conID;
}

ContainerID *GetPetIDsFromCharacterID(ContainerID id)
{
	Container *con = objGetContainerEx(GLOBALTYPE_ENTITYPLAYER, id, true, false, true);
	ContainerID *eaIDs = NULL;
	int i;

	if (!con)
		return NULL;

	// Get each active pet (assume max of 100)
	for (i = 0; i < 100; ++i)
	{
		char statePath[128];
		char petIDStr[128];
		ContainerID petID = 0;

		// Find the ID of this pet
		sprintf(statePath, ".pSaved.ppOwnedContainers[%d].hPet", i);
		if (!objPathGetString(statePath, con->containerSchema->classParse, con->containerData, SAFESTR(petIDStr)))
		{
			// If path is not present, then we're done looking through pets
			break;
		}

		ea32Push(&eaIDs, atoi(petIDStr));
	}
	objUnlockContainer(&con);

	return eaIDs;
}

#include "AutoGen/ObjectDB_h_ast.c"
