/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GlobalComm.h"
#include "objContainerIO.h"

#include "ObjectDB.h"
#include "dbReplication.h"
#include "GlobalStateMachine.h"
#include "ServerLib.h"
#include "dbClone.h"
#include "AutoGen/controller_autogen_remotefuncs.h"
#include "sysutil.h"
#include "../../core/controller/pub/controllerpub.h"
#include "sock.h"
#include "FloatAverager.h"
#include "logging.h"
#include "file.h"
#include "WorkerThread.h"

NetLink *sLinkToClone;


static void LaunchClone_CB(TransactionReturnVal *returnVal, void *userData)
{
	Controller_SingleServerInfo *pSingleServerInfo;
	enumTransactionOutcome eOutcome;

	if (!GSM_IsStateActive(DBSTATE_MASTER_LAUNCH_CLONE))
	{
		return;
	}

	eOutcome = RemoteCommandCheck_StartServer(returnVal, &pSingleServerInfo);

	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS && pSingleServerInfo->iIP != 0)
	{
		gDatabaseState.cloneServerIP = pSingleServerInfo->iIP;
		StructDestroy(parse_Controller_SingleServerInfo, pSingleServerInfo);
		// Connect to clone

		GSM_SwitchToState_Complex(DBSTATE_MASTER "/" DBSTATE_MASTER_CONNECT_TO_CLONE);
	}
	else
	{
		AssertOrAlert("OBJECTDB.CLONECONNECT","Failed to launch clone ObjectDB!");
		GSM_SwitchToSibling(DBSTATE_MASTER_MOVE_TO_OFFLINE_HOGG,false);
	}
}


void dbMasterLaunchClone_Enter(void)
{
	char commandLine[1000];
	GlobalType cloneType;
	cloneType = GLOBALTYPE_CLONEOBJECTDB;

	if (gDatabaseState.cloneHostName[0])
	{
		// got passed it on command line
		gDatabaseState.cloneServerIP = ipFromString(gDatabaseState.cloneHostName);
	}
	if (gDatabaseState.cloneServerIP)
	{
		GSM_SwitchToSibling(DBSTATE_MASTER_CONNECT_TO_CLONE,false);
	}
	else
	{
		char minutesString[100] = {0};
		char keepString[100] = {0};
		
		sprintf(minutesString, "-IncrementalInterval %d", gDatabaseConfig.iIncrementalInterval);
		sprintf(keepString, "- IncrementalHoursToKeep %d", gDatabaseConfig.iIncrementalHoursToKeep);

		sprintf(commandLine,"%s %s %s %s %s %s %s",
			errorGetVerboseLevel()?"-verbose":"",
			gDatabaseConfig.bNoHogs?"-NoHogs":"",
			gDatabaseState.bNoSaving?"-NoSaving":"",
			gServerLibState.bProfile?"-profile":"",
			gWaitedForDebugger?"-WaitForDebugger":"",
			minutesString,
			keepString);

		RemoteCommand_StartServer( objCreateManagedReturnVal(LaunchClone_CB, NULL),
			GLOBALTYPE_CONTROLLER, 0, cloneType, 0,NULL, commandLine, "Launched by dbMasterLaunchClone_Enter()", 
			NULL, NULL, NULL, NULL); // try to launch on other server if possible
		
	}
}

static void HandleClonePackets(Packet *pak,int cmd, NetLink *link, void *user_data)
{
	switch (cmd)
	{
		xcase DBTOMASTER_ACK_HANDSHAKE:
		{
			assertmsg(!gDatabaseState.bConnectedToClone,"Clone acknowledged handshake twice, something is wrong!");
			gDatabaseState.bConnectedToClone = true;
			printf("Connected to clone objectdb.\n\n");
		}
		xcase DBTOMASTER_STATUS_UPDATE:
		{
			ApplyCloneStatusPacket(pak);
		}
	}
}

static void HandleCloneDisconnect(NetLink *link, void *user_data)
{
	sLinkToClone = NULL;
}

NetComm *clone_comm;
U32 gRetryWaitTimeoutMinutes = 5;
AUTO_CMD_INT(gRetryWaitTimeoutMinutes, RetryWaitTimeoutMinutes) ACMD_CMDLINE;
U32 gTimeOfLastCloneConnectAttempt = 0;
bool gRetryCloneConnection = true;
static U32 gCloneConnectWaitTimeoutMinutes = 20;
AUTO_CMD_INT(gCloneConnectWaitTimeoutMinutes, CloneConnectWaitTimeoutMinutes) ACMD_CMDLINE;
static U32 gCloneHandshakeWaitTimeoutMinutes = 21;
AUTO_CMD_INT(gCloneHandshakeWaitTimeoutMinutes, CloneHandshakeWaitTimeoutMinutes) ACMD_CMDLINE;

int giCompressReplicationLink = 1;
AUTO_CMD_INT(giCompressReplicationLink, CompressReplicationLink);

AUTO_COMMAND;
void RetryCloneConnection()
{
	gRetryCloneConnection = true;
}

bool dbConnectToClone(void)
{
	Packet *pak;
	NetLink* link = NULL;
	int i;
	U32 uPort = DEFAULT_OBJECTDB_REPLICATE_PORT;
	LinkFlags extraFlags = 0;

	if (gDatabaseState.cloneConnectPort) uPort = gDatabaseState.cloneConnectPort;

	// Don't require compression, unless the other side wants it, or it's enabled by auto command.
	if (giCompressReplicationLink != 1)
		extraFlags |= LINK_NO_COMPRESS;

	if (!clone_comm)
		clone_comm = commCreate(0,1);
	
	printf("Initiating clone connection to %s:%d.\n\n", makeIpStr(gDatabaseState.cloneServerIP), uPort);

	link = commConnect(clone_comm,LINKTYPE_SHARD_NONCRITICAL_100MEG,
		LINK_FORCE_FLUSH | extraFlags, makeIpStr(gDatabaseState.cloneServerIP),uPort,HandleClonePackets,0,HandleCloneDisconnect,0);
	
	if (!linkConnectWait(&link,(float)(gCloneConnectWaitTimeoutMinutes * SECONDS_PER_MINUTE)))
	{
		return false;
	}
	else
	{
		pak = pktCreate(link,DBTOCLONE_HANDSHAKE);

		for (i = 1; i < GLOBALTYPE_MAXTYPES; i++)
		{
			ContainerStore *store = objFindContainerStoreFromType(i);
			if (!store) continue;

			objLockContainerStore_ReadOnly(store);
			pktSendBits(pak, 32, i);
			pktSendBool(pak, store->lazyLoad);
			pktSendBool(pak, store->requiresHeaders);
			pktSendBits64(pak, 64, objCountTotalContainersWithType((GlobalType)i));
			objUnlockContainerStore_ReadOnly(store);
		}
		pktSendBits(pak, 32, GLOBALTYPE_NONE);

		pktSend(&pak);

		if (!linkWaitForPacket(link,0,(float)(gCloneHandshakeWaitTimeoutMinutes * SECONDS_PER_MINUTE)) ||
			!gDatabaseState.bConnectedToClone)
		{
			linkRemove(&link);
			AssertOrAlert("OBJECTDB.CLONECONNECT.FAILURE","Failed to complete clone ObjectDB handshake! Clone Not Connected.");
		}
	}

	// Don't set this until it's ready to be used by the dblog thread.
	sLinkToClone = link;
	return true;
}

void dbMasterConnectToClone_BeginFrame(void)
{
	if (dbConnectToClone())
	{
		if(gDatabaseState.bReplayLogsToClone)
		{
			GSM_SwitchToSibling(DBSTATE_REPLAYER, false);
		}
		else
		{
			GSM_SwitchToSibling(DBSTATE_MASTER_MOVE_TO_OFFLINE_HOGG, false);
		}		
	}
	else if (gRetryWaitTimeoutMinutes)
	{
		AssertOrAlert("OBJECTDB.CLONECONNECT",
			"Failed to connect to clone ObjectDB! Run RetryCloneConnection on the ObjectDB console within %d minute%s to retry.",
			gRetryWaitTimeoutMinutes, gRetryWaitTimeoutMinutes == 1 ? "" : "s");
		GSM_SwitchToSibling(DBSTATE_MASTER_WAIT_FOR_RETRY, false);
	}
	else
	{
		AssertOrAlert("OBJECTDB.CLONECONNECT","Failed to connect to clone ObjectDB!");
		GSM_SwitchToSibling(DBSTATE_MASTER_MOVE_TO_OFFLINE_HOGG, false);
	}
}

void dbMasterWaitForRetryCloneConnect_Enter(void)
{
	// timeSecondsSince2000() hasn't updated since we started the linkConnectWait for the Clone,
	// so set the start time to 0 and set it correctly in BeginFrame
	gTimeOfLastCloneConnectAttempt = 0;
	gRetryCloneConnection = false;
}

void dbMasterWaitForRetryCloneConnect_BeginFrame(void)
{
	if(!gTimeOfLastCloneConnectAttempt)
		gTimeOfLastCloneConnectAttempt = timeSecondsSince2000();

	if(gRetryCloneConnection)
	{
		GSM_SwitchToSibling(DBSTATE_MASTER_CONNECT_TO_CLONE,false);
	}

	if(timeSecondsSince2000() - gTimeOfLastCloneConnectAttempt > gRetryWaitTimeoutMinutes * SECONDS_PER_MINUTE)
	{
		sLinkToClone = NULL;
		AssertOrAlert("OBJECTDB.CLONECONNECT.GIVEUP", "Giving up on clone connection");
		GSM_SwitchToSibling(DBSTATE_MASTER_MOVE_TO_OFFLINE_HOGG, false);
	}
}

AUTO_RUN;
void dbReplicationInitStates(void)
{
	GSM_AddGlobalState(DBSTATE_MASTER_LAUNCH_CLONE);
	GSM_AddGlobalStateCallbacks(DBSTATE_MASTER_LAUNCH_CLONE,dbMasterLaunchClone_Enter,
		NULL,NULL,NULL);

	GSM_AddGlobalState(DBSTATE_MASTER_CONNECT_TO_CLONE);
	GSM_AddGlobalStateCallbacks(DBSTATE_MASTER_CONNECT_TO_CLONE,
		NULL,dbMasterConnectToClone_BeginFrame,NULL,NULL);

	GSM_AddGlobalState(DBSTATE_MASTER_WAIT_FOR_RETRY);
	GSM_AddGlobalStateCallbacks(DBSTATE_MASTER_WAIT_FOR_RETRY,
		dbMasterWaitForRetryCloneConnect_Enter,dbMasterWaitForRetryCloneConnect_BeginFrame,NULL,NULL);

}

void dbUpdateCloneConnection(void)
{	
	if (!sLinkToClone)
	{
		return;
	}
	PERFINFO_AUTO_START_FUNC();
	commMonitor(clone_comm);
	PERFINFO_AUTO_STOP();
}

WorkerThread* wtDBLog;
char gDBLogFilename[MAX_PATH];

enum {
	DBLOG_CMD_LOG = WT_CMD_USER_START,
};

static U32 outstandingBGLogCount;

void dbLogRotateCB(char *baseHog, char *oldIncrementalHog, char *newIncrementalHog)
{
	char *fname_orig = newIncrementalHog;
	char fname_final[MAX_PATH];
	char *dotStart = FindExtensionFromFilename(fname_orig);

	// Wait for the BG thread to finish writing whatever it's writing.

	while(outstandingBGLogCount)
	{
		PERFINFO_AUTO_START("wait for BG logs to finish", 1);
		wtFlush(wtDBLog);
		Sleep(1);
		PERFINFO_AUTO_STOP();
	}

	if (gDBLogFilename[0])
	{
		char buf[MAX_PATH];
		char *ext;

		//rename the file to .log
		strcpy(buf, gDBLogFilename);
		ext = FindExtensionFromFilename(buf);
		ext[0] = '\0';
		strcat(buf, ".log");
		
		logFlushAndRenameFile(gDBLogFilename, buf);		
	}

	strncpy(fname_final,fname_orig,dotStart - fname_orig);
	strcat(fname_final,".lcg");

	strcpy(gDBLogFilename, fname_final);
	logSetFileOptions_Filename(gDBLogFilename,true,0,0,1);
}

void dbLogCloseCB(char *baseHog, char *oldIncrementalHog, char *newIncrementalHog)
{
	logCloseAllLogs();
}

typedef struct DBLogSharedEntry {
	U64					trSeq;
	U32					timeStamp;
	U32					lockCount;
	char*				estr;
} DBLogSharedEntry;

static void dbLogSharedEntryReleaseBG(DBLogSharedEntry** entryInOut)
{
	DBLogSharedEntry* entry = SAFE_DEREF(entryInOut);
	
	if(!entry)
	{
		return;
	}
	
	*entryInOut = NULL;
	
	if(InterlockedDecrement(&entry->lockCount) == 0){
		// FG released it first, so we get to free it.

		PERFINFO_AUTO_START("free DBLogSharedEntry", 1);
		
		estrDestroy(&entry->estr);
		SAFE_FREE(entry);
		
		PERFINFO_AUTO_STOP();
	}
}

static void dbLogInBGThread(void *user_data, DBLogSharedEntry** entryPtr, WTCmdPacket *packet)
{
	static char *logString;
	static char timeString[128];
	
	DBLogSharedEntry* entry;

	PERFINFO_AUTO_START_FUNC();
	
	entry = *entryPtr;

	estrClear(&logString);

	timeMakeDateStringFromSecondsSince2000(timeString, entry->timeStamp);
	estrPrintf(&logString,"%"FORM_LL"u %s: %s\n",entry->trSeq, timeString, entry->estr);
	logDirectWrite(gDBLogFilename,logString);


	if (gDatabaseState.databaseType == DBTYPE_MASTER ||
		gDatabaseState.databaseType == DBTYPE_REPLAY ||
		gDatabaseState.cloneHostName[0])
	{
		if (!sLinkToClone)
		{
			if (gDatabaseState.databaseType == DBTYPE_CLONE)
			{
				AssertOrAlert("OBJECTDB.CLONECONNECT", "Clone is trying to send log and has no clone!");
			}
			else
			{
				AssertOrAlert("OBJECTDB.CLONECONNECT", "Master is trying to send log and has no clone! Switching to Standalone mode");
				gDatabaseState.databaseType = DBTYPE_STANDALONE;
			}
			gDatabaseState.cloneHostName[0] = 0;
		}
		else
		{
			Packet *pak = pktCreate(sLinkToClone,DBTOCLONE_LOGTRANSACTION);
			pktSendBits64(pak,64,entry->trSeq);
			pktSendBits(pak,32,entry->timeStamp);
			pktSendString(pak,entry->estr);
			pktSend(&pak);	
			UpdateObjectDBStatusReplicationData(entry->timeStamp, entry->trSeq);
		}
	}
	
	dbLogSharedEntryReleaseBG(&entry);
	
	InterlockedDecrement(&outstandingBGLogCount);
	
	PERFINFO_AUTO_STOP();
}

static DBLogSharedEntry* dbLogGetNewSharedEntry(void)
{
	static DBLogSharedEntry* entryArray[1000];
	static U32 index;
	DBLogSharedEntry* entry;

	// Find the DBLogSharedEntry to use.
	
	if(++index == ARRAY_SIZE(entryArray))
	{
		index = 0;
	}
	
	assert(index >= 0 && index < ARRAY_SIZE(entryArray));
	
	entry = entryArray[index];
	
	if(!entry)
	{
		entryArray[index] = entry = callocStruct(DBLogSharedEntry);
	}
	
	if(entry->lockCount){
		// Was sent to log previously.
		
		if(InterlockedDecrement(&entry->lockCount)){
			// BG didn't finish with it, so just let it go and make a new one.
			
			entryArray[index] = entry = callocStruct(DBLogSharedEntry);
		}else{
			// BG finished with it, so clear the lock and reuse it.
			
			entry->lockCount = 0;
		}
	}
	
	return entry;
}

void dbLogTransaction(const char* cmd, U64 trSeq, U32 timeStamp)
{
	DBLogSharedEntry*	entry;
	U32					cmdLength;
	
	if (gDatabaseState.bNoSaving)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();
	
	entry = dbLogGetNewSharedEntry();
	
	estrCopy2(&entry->estr, cmd);
	entry->lockCount = 2;
	entry->trSeq = trSeq;
	entry->timeStamp = timeStamp;
	
	cmdLength = estrLength(&entry->estr);
	
	ADD_MISC_COUNT(cmdLength, "cmd bytes");

	PERFINFO_AUTO_START("averagers", 1);
	{
		IntAverager_AddDatapoint(gDatabaseState.DBUpdateSizeAverager, cmdLength);
		CountAverager_ItHappened(gDatabaseState.DBUpdateCountAverager);
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("bottom", 1);
	{
		if (!gDBLogFilename[0])
		{
			sprintf(gDBLogFilename,"%s/DB.log",dbDataDir());			
			logSetFileOptions_Filename(gDBLogFilename,true,0,0,1);
		}
		
		if(!wtDBLog)
		{
			int iWorkerThreadCmdQueueSize;

#ifdef _M_X64
			iWorkerThreadCmdQueueSize = 1024 * 1024;
#else
			iWorkerThreadCmdQueueSize = 1024;
#endif

			wtDBLog = wtCreate(iWorkerThreadCmdQueueSize, 1024, NULL, "DBLogThread");
			wtSetThreaded(wtDBLog, 1, 0, 0);
			wtStart(wtDBLog);
			wtRegisterCmdDispatch(wtDBLog, DBLOG_CMD_LOG, dbLogInBGThread);
		}

		*(DBLogSharedEntry**)wtAllocCmd(wtDBLog, DBLOG_CMD_LOG, sizeof(entry)) = entry;
		wtSendCmd(wtDBLog);
		
		InterlockedIncrement(&outstandingBGLogCount);
	}
	PERFINFO_AUTO_STOP();
	
	PERFINFO_AUTO_STOP();// FUNC.
}

void dbLogFlushTransactions(void)
{
	PERFINFO_AUTO_START("dbLogFlushTransactions",1);
	//logWaitForQueueToEmpty();
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND_REMOTE;
int dbMasterFlush(void)
{
	printf("Flushing Master...");
	PERFINFO_AUTO_START("dbCloneFlush",1);
	if (clone_comm)
	{	
		commFlushAllLinks(clone_comm);
	}
	logWaitForQueueToEmpty();
	objFlushContainers();
	PERFINFO_AUTO_STOP();
	printf("Done!\n");
	return 1;
}

bool dbConnectedToClone(void)
{
	return linkConnected(sLinkToClone);
}
