#include "UtilitiesLib.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include <stdio.h>
#include <conio.h>
#include "MemoryMonitor.h"

#include "file.h"
#include "FolderCache.h"

#include "sysutil.h"
#include <math.h>
#include "gimmeDLLWrapper.h"

#include "winutil.h"

#include "transactionserver.h"
#include "LocalTransactionManager.h"
#include "ServerLib.h"
#include "SharedMemory.h"

#include "MultiplexedNetLinkList.h"
#include "logcomm.h"
#include "MemoryPool.h"
#include "ControllerLink.h"
#include "cpu_count.h"
#include "TransactionServerUtilities.h"
#include "sock.h"
#include "ConsoleDebug.h"
#include "../multiplexer/Multiplexer.h"
#include "TransactionServer_ShardCluster.h"

#define DEV_MODE_TRANS_COMM_TIMEOUT 1 // 100 // TODO: REDUCE_DEV_MODE_CPU
#define MAX_CLIENTS 50
NetListen *net_links;

TransactionServer gTransactionServer;
int sTransactionsPerSecond = 0;

// If true, drop all incoming packets.
static bool sbIgnoreIncomingMessages = false;

bool gbMultipleSendThreads = false;
AUTO_CMD_INT(gbMultipleSendThreads, MultipleSendThreads) ACMD_COMMANDLINE;

//how often to dump out info about all recently completed transactions
int giTransCountDumpInterval = 10;
AUTO_CMD_INT(giTransCountDumpInterval, TransCountDumpInterval);

//how often to dump/alert a summary of recent lagged transactions
int giLaggedTransSummaryDumpInterval = 3600;
AUTO_CMD_INT(giLaggedTransSummaryDumpInterval, LaggedTransSummaryDumpInterval);

// If set, immediately play the transaction log on this link.
static NetLink *spCaptureReplayLink = NULL;

void UpdateTransServerTitle(void)
{
	char buf[200];
	PERFINFO_AUTO_START_FUNC();
	sprintf_s(SAFESTR(buf), "TransServer - %d connections, %d transactions processed, %d active transactions, %d per second",
		gTransactionServer.iNumActiveConnections, gTransactionServer.iNextTransactionID, gTransactionServer.iNumActiveTransactions,
		sTransactionsPerSecond);
	setConsoleTitle(buf);
	PERFINFO_AUTO_STOP();
}

int TransServerClientConnect(NetLink* link,TransactionServerClientLink* client)
{
	char ipBuf[100];
	client->link = link;
	client->iIndexOfLogicalConnection = -1;
	client->iIndexOfMultiplexConnection = -1;
	//link->cookieEcho = 0;


	printf("Someone connected (%s)!\n", linkGetIpPortStr(link, SAFESTR(ipBuf)));
	return 1;
}


void TransServerMultiplexClientDisconnect(NetLink *pNetLink, int iIndexOfServer, TransactionServerClientLink *tsc_link)
{
	char *pDisconnectReason = NULL;
	estrStackCreate(&pDisconnectReason);
	linkGetDisconnectReason(pNetLink, &pDisconnectReason);
	ConnectionFromMultiplexConnectionDied(&gTransactionServer, tsc_link->iIndexOfMultiplexConnection, iIndexOfServer, pDisconnectReason);
	estrDestroy(&pDisconnectReason);
}

int TransServerClientDisconnect(NetLink* link,TransactionServerClientLink *pTscLink)
{
	char ipBuf[100];
	char *pDisconnectReason = NULL;
	estrStackCreate(&pDisconnectReason);
	linkGetDisconnectReason(link, &pDisconnectReason);
	printf("Someone disconnected (%s)!\n", linkGetIpPortStr(link, SAFESTR(ipBuf)));
	LogicalConnectionDied(&gTransactionServer, pTscLink->iIndexOfLogicalConnection, false, pDisconnectReason);
	MultiplexConnectionDied(&gTransactionServer, pTscLink->iIndexOfMultiplexConnection, pDisconnectReason);

	estrDestroy(&pDisconnectReason);

	return 1;
}



static void TransServerHandleMsg(Packet *pak,int cmd, NetLink *link, TransactionServerClientLink *client)
{
	int iConnectionNum;

	if (sbIgnoreIncomingMessages)
	{
		ADD_MISC_COUNT(1, "TransServerIgnore");
		return;
	}

	switch(cmd)
	{
		#define PERF_CASE(foo) xcase foo: START_BIT_COUNT(pak, "Msg:"#foo);PERFINFO_AUTO_START("Msg:"#foo, 1);
		
		PERF_CASE(TRANSCLIENT_REQUEST_NEW_TRANSACTION)
			iConnectionNum = client->iIndexOfLogicalConnection;
			assert(iConnectionNum != -1);

			HandleNewTransactionRequest(&gTransactionServer, pak, gTransactionServer.pConnections[iConnectionNum].eServerType,
				gTransactionServer.pConnections[iConnectionNum].iServerID, iConnectionNum);

		PERF_CASE(TRANSCLIENT_TRANSACTIONFAILED)
			HandleTransactionFailed(&gTransactionServer, pak);
		PERF_CASE(TRANSCLIENT_TRANSACTIONSUCCEEDED)
			HandleTransactionSucceeded(&gTransactionServer, pak);
		PERF_CASE(TRANSCLIENT_TRANSACTIONBLOCKED)
			HandleTransactionBlocked(&gTransactionServer, pak);
		PERF_CASE(TRANSCLIENT_TRANSACTIONCANCELCONFIRMED)
			HandleTransactionCancelConfirmed(&gTransactionServer, pak);
		PERF_CASE(TRANSCLIENT_TRANSACTIONPOSSIBLE)
			HandleTransactionPossible(&gTransactionServer, pak);
		PERF_CASE(TRANSCLIENT_REGISTERCLIENTINFO)
			HandleNormalConnectionRegisterClientInfo(&gTransactionServer, pak, link, client);
		PERF_CASE(TRANSCLIENT_SENDTRANSSERVERCOMMANDANDCOMMENT)
			HandleTransactionServerCommand(&gTransactionServer, pak);
		PERF_CASE(TRANSCLIENT_UPDATESFROMLOCALTRANS)
			HandleLocalTransactionUpdates(&gTransactionServer, pak);
		PERF_CASE(TRANSCLIENT_SETTRANSVARIABLE)
			HandleTransactionSetVariable(&gTransactionServer, pak);
		PERF_CASE(TRANSCLIENT_TRANSACTIONPOSSIBLEANDCONFIRMED)
			HandleTransactionPossibleAndConfirmed(&gTransactionServer, pak);
		PERF_CASE(TRANSCLIENT_SEND_PACKET_SIMPLE)
			iConnectionNum = client->iIndexOfLogicalConnection;
			assert(iConnectionNum != -1);
			HandleSendPacketSimple(&gTransactionServer, pak, iConnectionNum);
		PERF_CASE(TRANSCLIENT_SEND_PACKET_SIMPLE_OTHER_SHARD)
			iConnectionNum = client->iIndexOfLogicalConnection;
			assert(iConnectionNum != -1);
			HandleSendPacketSimpleOtherShard(&gTransactionServer, pak, iConnectionNum);



		PERF_CASE(TRANSCLIENT_DBG_REQUESTCONTAINEROWNER)
			iConnectionNum = client->iIndexOfLogicalConnection;
			assert(iConnectionNum != -1);
			HandleRequestContainerOwner(&gTransactionServer, pak, iConnectionNum);
		PERF_CASE(TRANSCLIENT_DBG_LAGGEDTRANSACTION)
			HandleReportLaggedTransactions(&gTransactionServer, pak);
		PERF_CASE(TRANSCLIENT_REGISTER_SLOW_TRANSCALLBACK_WITH_TRACKER)
			HandleRegisterSlowTransCallbackWithTracker(&gTransactionServer, pak);

		xdefault:
			START_BIT_COUNT(pak, "unknown cmd");
			PERFINFO_AUTO_START("unknown cmd", 1);
			printf("Unknown command %d\n",cmd);

		#undef PERF_CASE
	}
	
	PERFINFO_AUTO_STOP();
	STOP_BIT_COUNT(pak);
}

void AlertRequestFromDeadServer(TransactionServerClientLink *client, NetLink *pLink)
{
	//this seems to happen a few times a day with no negative impact... turning off alert for now
	/*
	if (client->iTimeOfNextDeadServerAlert > timeSecondsSince2000())
	{
		return;
	}

	client->iTimeOfNextDeadServerAlert = timeSecondsSince2000() + 600;
	
	ErrorOrAlert("DEAD_CLIENT_REQUEST", "Trans server got a request on a link from multiplexer on machine %s for a client which no longer exists. Possible dying-server corner case?",
		makeIpStr(linkGetIp(pLink)));*/
}






void TransServerHandleMultiplexMsg(Packet *pak, int cmd, int iIndexOfSenderFull, NetLink *pNetLink, TransactionServerClientLink *client)
{
	int iInternalIndex = MULTIPLEXER_GET_REAL_CONNECTION_INDEX(iIndexOfSenderFull);

	switch(cmd)
	{
		#define PERF_CASE(foo) xcase foo: START_BIT_COUNT(pak, "MultiplexMsg:"#foo);PERFINFO_AUTO_START("MultiplexMsg:"#foo, 1);

		PERF_CASE(TRANSCLIENT_REQUEST_NEW_TRANSACTION)
			{
				int iMultiplexConnectionNum = client->iIndexOfMultiplexConnection;
				int iConnectionNum;
				
				MultiplexConnection *pMultiplexConnection;

				assert(iMultiplexConnectionNum != -1);

				pMultiplexConnection = &gTransactionServer.pMultiplexConnections[iMultiplexConnectionNum];

				assert(iInternalIndex >= 0 && iInternalIndex <= pMultiplexConnection->iMaxConnectionIndex);

				iConnectionNum = pMultiplexConnection->piLogicalIndexesOfMultiplexConnections[iInternalIndex];

				if (iConnectionNum == -1)
				{
					AlertRequestFromDeadServer(client, pNetLink);
					break;
				}

				HandleNewTransactionRequest(&gTransactionServer, pak, gTransactionServer.pConnections[iConnectionNum].eServerType,
					gTransactionServer.pConnections[iConnectionNum].iServerID, iConnectionNum);
			}

		PERF_CASE(TRANSCLIENT_TRANSACTIONFAILED)
			HandleTransactionFailed(&gTransactionServer, pak);
		PERF_CASE(TRANSCLIENT_TRANSACTIONSUCCEEDED)
			HandleTransactionSucceeded(&gTransactionServer, pak);
		PERF_CASE(TRANSCLIENT_TRANSACTIONBLOCKED)
			HandleTransactionBlocked(&gTransactionServer, pak);
		PERF_CASE(TRANSCLIENT_TRANSACTIONCANCELCONFIRMED)
			HandleTransactionCancelConfirmed(&gTransactionServer, pak);
		PERF_CASE(TRANSCLIENT_TRANSACTIONPOSSIBLE)
			HandleTransactionPossible(&gTransactionServer, pak);
		PERF_CASE(TRANSCLIENT_REGISTERCLIENTINFO)
			HandleMultiplexConnectionRegisterClientInfo(&gTransactionServer, pak, pNetLink, iIndexOfSenderFull, client);
		PERF_CASE(TRANSCLIENT_SENDTRANSSERVERCOMMANDANDCOMMENT)
			HandleTransactionServerCommand(&gTransactionServer, pak);
		PERF_CASE(TRANSCLIENT_UPDATESFROMLOCALTRANS)
			HandleLocalTransactionUpdates(&gTransactionServer, pak);
		PERF_CASE(TRANSCLIENT_SETTRANSVARIABLE)
			HandleTransactionSetVariable(&gTransactionServer, pak);
		PERF_CASE(TRANSCLIENT_TRANSACTIONPOSSIBLEANDCONFIRMED)
			HandleTransactionPossibleAndConfirmed(&gTransactionServer, pak);
		PERF_CASE(TRANSCLIENT_SEND_PACKET_SIMPLE)
			{
				int iMultiplexConnectionNum = client->iIndexOfMultiplexConnection;
				int iConnectionNum;
				
				MultiplexConnection *pMultiplexConnection;
				assert(iMultiplexConnectionNum != -1);
				pMultiplexConnection = &gTransactionServer.pMultiplexConnections[iMultiplexConnectionNum];
				assert(iInternalIndex >= 0 && iInternalIndex <= pMultiplexConnection->iMaxConnectionIndex);
				iConnectionNum = pMultiplexConnection->piLogicalIndexesOfMultiplexConnections[iInternalIndex];
				if (iConnectionNum == -1)
				{
					AlertRequestFromDeadServer(client, pNetLink);
					break;
				}


				HandleSendPacketSimple(&gTransactionServer, pak, iConnectionNum);
			}
		PERF_CASE(TRANSCLIENT_SEND_PACKET_SIMPLE_OTHER_SHARD)
			{
				int iMultiplexConnectionNum = client->iIndexOfMultiplexConnection;
				int iConnectionNum;
				
				MultiplexConnection *pMultiplexConnection;
				assert(iMultiplexConnectionNum != -1);
				pMultiplexConnection = &gTransactionServer.pMultiplexConnections[iMultiplexConnectionNum];
				assert(iInternalIndex >= 0 && iInternalIndex <= pMultiplexConnection->iMaxConnectionIndex);
				iConnectionNum = pMultiplexConnection->piLogicalIndexesOfMultiplexConnections[iInternalIndex];
				if (iConnectionNum == -1)
				{
					AlertRequestFromDeadServer(client, pNetLink);
					break;
				}


				HandleSendPacketSimpleOtherShard(&gTransactionServer, pak, iConnectionNum);
			}
		PERF_CASE(TRANSCLIENT_DBG_REQUESTCONTAINEROWNER)
			{
				int iMultiplexConnectionNum = client->iIndexOfMultiplexConnection;
				int iConnectionNum;
				
				MultiplexConnection *pMultiplexConnection;
				assert(iMultiplexConnectionNum != -1);
				pMultiplexConnection = &gTransactionServer.pMultiplexConnections[iMultiplexConnectionNum];
				assert(iInternalIndex >= 0 && iInternalIndex <= pMultiplexConnection->iMaxConnectionIndex);
				iConnectionNum = pMultiplexConnection->piLogicalIndexesOfMultiplexConnections[iInternalIndex];
				if (iConnectionNum == -1)
				{
					AlertRequestFromDeadServer(client, pNetLink);
					break;
				}
				HandleRequestContainerOwner(&gTransactionServer, pak, iConnectionNum);
			}
		PERF_CASE(TRANSCLIENT_DBG_LAGGEDTRANSACTION)
			HandleReportLaggedTransactions(&gTransactionServer, pak);

		PERF_CASE(TRANSCLIENT_REGISTER_SLOW_TRANSCALLBACK_WITH_TRACKER)
			HandleRegisterSlowTransCallbackWithTracker(&gTransactionServer, pak);


		xdefault:
			START_BIT_COUNT(pak, "unknown cmd");
			PERFINFO_AUTO_START("unknown cmd", 1);
			printf("Unknown command %d\n",cmd);
			
		#undef PERF_CASE
	}
	
	PERFINFO_AUTO_STOP();
	STOP_BIT_COUNT(pak);
}

static void DumpTransactionServerStatusConsoleCallback(void){
	DumpTransactionServerStatus(&gTransactionServer);
}

static NetComm *spTransComm;
static U32 msTransCommTimeout;
AUTO_CMD_INT(msTransCommTimeout, transCommTimeout) ACMD_CALLBACK(transCommTimeoutChanged);
void transCommTimeoutChanged(void){
	commSetMinReceiveTimeoutMS(spTransComm, msTransCommTimeout);
}

// Call Transaction Server commMonitor()
void TransactionServerMonitorTick(F32 elapsed)
{
	PERFINFO_AUTO_START_FUNC();

	commMonitor(spTransComm);
	commMonitor(commDefault());
	utilitiesLibOncePerFrame(elapsed, 1);
	serverLibOncePerFrame();

	PERFINFO_AUTO_STOP_FUNC();
}

// Disable incoming message processing, for debugging.
void TransactionServerIgnoreIncomingMessages(bool bIgnore)
{
	sbIgnoreIncomingMessages = bIgnore;
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void TransactionServerFlushCaptureReplayLink(void)
{
	if (spCaptureReplayLink)
		linkSendKeepAlive(spCaptureReplayLink);
}

// Request that TransServerMain begin playing a capture replay on a link.
void TransactionServerSetCaptureReplayLink(NetLink *pLink)
{
	spCaptureReplayLink = pLink;
}

int main(int argc,char **argv)
{
	int		i,frameTimer;
	U32 iLastSeconds = 0, iCountLastSecond = 0;
	U32 iNextTransCountDump = 0;
	U32 iNextLaggedTransSummaryDump = 0;

	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER
	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

	commSetMinReceiveTimeoutMS(commDefault(), 0);

	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'T', 0x8080ff);

	FolderCacheChooseModeNoPigsInDevelopment();

	preloadDLLs(0);

	if (fileIsUsingDevData()) 
	{
	} else 
	{
		gimmeDLLDisable(1);
	}

	srand((unsigned int)time(NULL));
	consoleUpSize(110,128);

	sharedMemorySetMode(SMM_DISABLED);
	mpEnablePoolCompaction(false);
	
	//packetStartup(0,0);
	

	logSetDir(GlobalTypeToName(GetAppGlobalType()));
	serverLibStartup(argc, argv);

	InitTransactionServer(&gTransactionServer);

	UpdateTransServerTitle();
		
	
	PrepareForMultiplexedNetLinkListMode(TransServerHandleMultiplexMsg, TransServerMultiplexClientDisconnect, TransServerHandleMsg, TransServerClientDisconnect);

	loadstart_printf("Opening client port..");


	{
		U32 msTimeout = FIRST_IF_SET(	msTransCommTimeout,
										isProductionMode() ? 1 : DEV_MODE_TRANS_COMM_TIMEOUT);
		U32 threadCount = gbMultipleSendThreads ? getNumRealCpus() - 1 : 1;
		
		spTransComm = commCreate(msTimeout, threadCount);
	}

	//we generally want the trans server to process all of its incoming packets each frame, but only up to a point. That point
	//is 2 seconds
	commSetPacketReceiveMsecs(spTransComm, 2000);


	for(;;)
	{
		LinkFlags extraFlags = 0;

		// Don't require compression, unless the other side wants it, or it's enabled by auto command.
		if (giCompressTransactionLink != 1)
			extraFlags |= LINK_NO_COMPRESS;

		net_links = commListen(spTransComm,
			LINKTYPE_SHARD_NONCRITICAL_20MEG,
			LINK_FORCE_FLUSH | extraFlags,
			gServerLibState.transactionServerPort,
			MultiplexedNetLinkList_Wrapper_HandleMsg,
			TransServerClientConnect,
			MultiplexedNetLinkList_Wrapper_ClientDisconnect,
			sizeof(TransactionServerClientLink));

		if (net_links)
			break;
		Sleep(DEFAULT_SERVER_SLEEP_TIME);
	}

	loadend_printf("");

	AttemptToConnectToController(true, TransServerAuxControllerMessageHandler, false);

	printf("Ready.\n");

	frameTimer = timerAlloc();

	DirectlyInformControllerOfState("ready");

	log_printf(LOG_TRANSSERVER, "Transaction Server Ready");

	TimedCallback_Add(transactionServerSendGlobalInfo, &gTransactionServer, 5.0f);

	// Register console keypresses.
	{
		static ConsoleDebugMenu menu[] = {
			{'t', "Dump TransactionServer status", DumpTransactionServerStatusConsoleCallback, NULL},
			{0},
		};
		
		ConsoleDebugAddToDefault(menu);
	}

	for(;;)
	{	
		F32 frametime;

		autoTimerThreadFrameBegin("main");
		
		frametime = timerElapsedAndStart(frameTimer);
		
		commMonitor(spTransComm);
		commMonitor(commDefault());
		if (spCaptureReplayLink)
			TransactionServerDebugCaptureReplay(spCaptureReplayLink);
		utilitiesLibOncePerFrame(frametime, 1);

		UpdateTransactionServer(&gTransactionServer);
		UpdateTransServerTitle();
		serverLibOncePerFrame();
		TransactionServer_ShardCluster_Tick();
		
		PERFINFO_AUTO_START("bottom", 1);
		{
			U32 iCurTime = timeSecondsSince2000();
			if (iCurTime != iLastSeconds)
			{
				sTransactionsPerSecond = gTransactionServer.iNumSucceededTransactions - iCountLastSecond;
				iCountLastSecond = gTransactionServer.iNumSucceededTransactions;
				iLastSeconds = iCurTime;
			}

			if (iCurTime >= iNextTransCountDump)
			{
				iNextTransCountDump = iCurTime + giTransCountDumpInterval;
				DumpTransCounts(&gTransactionServer);
			}

			if (iCurTime >= iNextLaggedTransSummaryDump)
			{
				iNextLaggedTransSummaryDump = iCurTime + giLaggedTransSummaryDumpInterval;
				DumpLaggedTransactionSummary();
			}
		}
		PERFINFO_AUTO_STOP();

		autoTimerThreadFrameEnd();
	}
	EXCEPTION_HANDLER_END
}
