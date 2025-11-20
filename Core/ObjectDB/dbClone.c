/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "UtilitiesLib.h"
#include "logging.h"
#include "sock.h"
#include "file.h"
#include "folderCache.h"

#include "objContainerIO.h"
#include "objTransactions.h"

#include "dbGenericDatabaseThreads.h"
#include "dbContainerRestore.h"
#include "dbCharacterChoice.h"

#include "ObjectDB.h"
#include "dbClone.h"
#include "dbReplication.h"
#include "GenericFileServing.h"

#include "GlobalStateMachine.h"
#include "ServerLib.h"

#include "Alerts.h"
#include "dbOfflining.h"

extern int gShortenStalenessIncrementInterval;

void dbClone_Enter(void)
{
	TimedCallback_Add(dbSendGlobalInfo, NULL, 5.0f);
	GSM_AddChildState(DBSTATE_CLONE_LOAD,false);
}


void dbClone_BeginFrame(void)
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

void dbCloneLoad_RunOnce(void)
{
	U32 lastSnapshot = 0;
	char filebuf[MAX_PATH];

	loadstart_printf("loading database");
	
	dbConfigureDatabase();

	strcpy(filebuf, objGetContainerSourcePath());
	getDirectoryName(filebuf);
	GenericFileServing_Begin(0, 0);
	GenericFileServing_ExposeDirectory(strdup(filebuf));

	dbLoadEntireDatabase();

	if ((lastSnapshot = objGetLastSnapshot()) != 0)
		gDatabaseState.lastSnapshotTime = lastSnapshot;

	if(gDatabaseConfig.bEnableOfflining)
	{
		InitializeTotalOfflineCharacters();
	}

	loadend_printf("done");

	if(gDatabaseConfig.bCloneConnectToMaster)
		GSM_SwitchToSibling(DBSTATE_CLONE_WAITING_FOR_MASTER,false);
	else
		GSM_SwitchToSibling(DBSTATE_CLONE_HANDLE_LOG,false);
}

NetLink *sMasterLink;
NetListen *sMasterLinkList;


int MasterObjectDBConnect(NetLink* link, void *user_data)
{
	//link->userData = NULL;
	sMasterLink = link;
	return 1;
}

int MasterObjectDBDisconnect(NetLink* link, void *user_data)
{
	sMasterLink = NULL;
	gDatabaseState.bConnectedToMaster = false;

//	assertmsg(0,"MasterDB Disconnected. Code needs to go here to gracefully shutdown clone");
	return 1;
}

static void MasterObjectDBHandleMsg(Packet *pak,int cmd, NetLink *link,void *user_data)
{
	switch (cmd)
	{
		xcase DBTOCLONE_HANDSHAKE:
		{
			Packet *newPak;
			U32 type;
			if (gDatabaseState.bConnectedToMaster)
			{
				assertmsg(0,"Received multiple Master DB Handshakes! Something is wrong.");
			}
			gDatabaseState.bConnectedToMaster = true;
			
			while (type = pktGetBits(pak, 32))
			{
				bool lazyload = pktGetBool(pak);
				bool requiresHeaders = pktGetBool(pak);
				U64 count = pktGetBits64(pak, 64);
				U64 lcount;
				ContainerStore *store;

				store = objFindContainerStoreFromType(type);
				if(store)
					objLockContainerStore_ReadOnly(store);
				lcount = objCountTotalContainersWithType(type);

				if (store && (store->lazyLoad != lazyload))
				{
					char *kind = GlobalTypeToName(type);
					AssertOrAlert("OBJECTDB.CLONESYNC", 
						"For %s containers, ObjectDB Clone has lazyload %s, while the Master has lazyload %s.",
						kind, store->lazyLoad ? "on" : "off", lazyload ? "on" : "off");
				}

				if (store && (store->requiresHeaders != requiresHeaders))
				{
					char *kind = GlobalTypeToName(type);
					AssertOrAlert("OBJECTDB.CLONESYNC", 
						"For %s containers, ObjectDB Clone has requiresHeaders %s, while the Master has requiresHeaders %s.",
						kind, store->requiresHeaders ? "on" : "off", requiresHeaders ? "on" : "off");
				}

				if (lcount != count && (!store || (store->requiresHeaders || !store->lazyLoad)))
				{
					char *kind = GlobalTypeToName(type);
					AssertOrAlert("OBJECTDB.CLONESYNC", 
						"ObjectDB Clone has a different number of %s containers (%"FORM_LL"u) than the Master (%"FORM_LL"u)",
						kind, lcount, count);
				}
				if(store)
					objUnlockContainerStore_ReadOnly(store);
			}

			if (gDatabaseState.cloneHostName[0])
			{
				// got passed it on command line
				gDatabaseState.cloneServerIP = ipFromString(gDatabaseState.cloneHostName);
			}
			if (gDatabaseState.cloneServerIP && !dbConnectToClone())
			{
				AssertOrAlert("OBJECTDB.CLONECLONECONNECT","Failed to connect to clone ObjectDB!");
			}

			printf("Connecting to master objectdb.\n\n");

			newPak = pktCreate(link,DBTOMASTER_ACK_HANDSHAKE);
			pktSend(&newPak);
		}
		xcase DBTOCLONE_LOGTRANSACTION:
		{
			U64 trSeq = pktGetBits64(pak,64);
			U32 trTimeStamp = pktGetBits(pak,32);
			const char *trMessage = (const char*)pktGetStringTemp(pak);
			dbHandleDatabaseUpdateString(trMessage,trSeq,trTimeStamp);
			UpdateObjectDBStatusReplicationData(trTimeStamp, trSeq);
		}
	}
}


AUTO_COMMAND_REMOTE;
int dbCloneFlush(void)
{
	printf("Flushing Clone...");
	PERFINFO_AUTO_START("dbCloneFlush",1);	
	logWaitForQueueToEmpty();
	objFlushContainers();
	PERFINFO_AUTO_STOP();
	printf("Done!\n");
	return 1;
}

static U32 giCloneMasterWaitTimeoutMinutes = 30;
AUTO_CMD_INT(giCloneMasterWaitTimeoutMinutes, CloneMasterWaitTimeoutMinutes) ACMD_COMMANDLINE;

static U32 giCloneMasterWaitStartTime = 0;

void dbCloneWaitingForMaster_Enter(void)
{
	LinkFlags extraFlags = 0;
	giCloneMasterWaitStartTime = timeSecondsSince2000();
	// sMasterLinkList is never cleared, so don't try to set it twice.
	if(sMasterLinkList)
		return;

	// Don't require compression, unless the other side wants it, or it's enabled by auto command.
	if (giCompressReplicationLink != 1)
		extraFlags |= LINK_NO_COMPRESS;

	for(;;)
	{
		U32 uPort = DEFAULT_OBJECTDB_REPLICATE_PORT;
		if (gDatabaseState.cloneListenPort) uPort = gDatabaseState.cloneListenPort;
		sMasterLinkList = commListen(commDefault(),LINKTYPE_UNSPEC, LINK_FORCE_FLUSH | extraFlags,uPort,MasterObjectDBHandleMsg,MasterObjectDBConnect,MasterObjectDBDisconnect,0);
		if (sMasterLinkList)
			break;
		Sleep(1);
	}
}

void dbCloneWaitingForMaster_BeginFrame(void)
{
	if (gDatabaseState.bConnectedToMaster)
	{
		printf("Connected to main ObjectDB\n\n");
		GSM_SwitchToSibling(DBSTATE_CLONE_HANDLE_LOG,false);
	}

	if(timeSecondsSince2000() > (giCloneMasterWaitStartTime + giCloneMasterWaitTimeoutMinutes * SECONDS_PER_MINUTE))
	{
		if(gRetryWaitTimeoutMinutes)
		{
			AssertOrAlert("CLONEOBJECTDB.CLONECONNECT","Failed to connect to main ObjectDB! Run RetryCloneConnection on the CloneObjectDB console within %d minute%s to retry.", gRetryWaitTimeoutMinutes, gRetryWaitTimeoutMinutes == 1 ? "" : "s");
			GSM_SwitchToSibling(DBSTATE_CLONE_WAIT_FOR_RETRY,false);
		}
		else
		{
			AssertOrAlert("CLONEOBJECTDB.CLONECONNECT","Failed to connect to main ObjectDB!");
			GSM_SwitchToSibling(DBSTATE_CLONE_HANDLE_LOG,false);
		}
	}
}

void dbCloneWaitForRetryCloneConnect_Enter(void)
{
	gTimeOfLastCloneConnectAttempt = timeSecondsSince2000();
	gRetryCloneConnection = false;
}

void dbCloneWaitForRetryCloneConnect_BeginFrame(void)
{
	if(gRetryCloneConnection)
	{
		GSM_SwitchToSibling(DBSTATE_CLONE_WAITING_FOR_MASTER,false);
	}

	if(timeSecondsSince2000() - gTimeOfLastCloneConnectAttempt > gRetryWaitTimeoutMinutes * SECONDS_PER_MINUTE)
	{
		AssertOrAlert("CLONEOBJECTDB.CLONECONNECT.GIVEUP", "Giving up on connection from master.");
		GSM_SwitchToSibling(DBSTATE_CLONE_HANDLE_LOG, false);
	}
}

void dbCloneHandleLog_Enter(void)
{	
	printf("Waiting for log data.\n");
	initOutgoingIntershardTransfer();

	if(gContainerSource.enableStaleContainerCleanup && gDatabaseConfig.bLazyLoad)
	{
		activateStaleContainerCleanup();
	}

	initContainerRestoreStashTable();
}

void dbCloneHandleLog_Leave(void)
{

}


void dbCloneHandleLog_BeginFrame(void)
{
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

	if(linkConnected(sMasterLink))
		SendCloneStatusPacket(sMasterLink);
}


AUTO_RUN;
void dbCloneInitStates(void)
{
	GSM_AddGlobalState(DBSTATE_CLONE);
	GSM_AddGlobalStateCallbacks(DBSTATE_CLONE,dbClone_Enter,dbClone_BeginFrame,NULL,NULL);

	GSM_AddGlobalState(DBSTATE_CLONE_LOAD);
	GSM_AddGlobalStateCallbacks(DBSTATE_CLONE_LOAD,NULL,dbCloneLoad_RunOnce,NULL,dbLogTimeInState);

	GSM_AddGlobalState(DBSTATE_CLONE_WAITING_FOR_MASTER);
	GSM_AddGlobalStateCallbacks(DBSTATE_CLONE_WAITING_FOR_MASTER,dbCloneWaitingForMaster_Enter,
		dbCloneWaitingForMaster_BeginFrame,NULL,NULL);

	GSM_AddGlobalState(DBSTATE_CLONE_WAIT_FOR_RETRY);
	GSM_AddGlobalStateCallbacks(DBSTATE_CLONE_WAIT_FOR_RETRY,dbCloneWaitForRetryCloneConnect_Enter,
		dbCloneWaitForRetryCloneConnect_BeginFrame,NULL,NULL);

	GSM_AddGlobalState(DBSTATE_CLONE_HANDLE_LOG);
	GSM_AddGlobalStateCallbacks(DBSTATE_CLONE_HANDLE_LOG,dbCloneHandleLog_Enter,dbCloneHandleLog_BeginFrame,NULL,dbCloneHandleLog_Leave);
}

