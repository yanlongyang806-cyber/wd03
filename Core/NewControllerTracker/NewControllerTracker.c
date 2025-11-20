#if _MSC_VER < 1600
#if _WIN64
#pragma comment(lib,"../../libs/AttachToDebuggerLib/x64/debug/AttachToDebuggerLibX64.lib")
#else
#pragma comment(lib,"../../libs/AttachToDebuggerLib/debug/AttachToDebuggerLib.lib")
#endif
#else
#if _WIN64
#pragma comment(lib,"../../libs/AttachToDebuggerLib/lib/AttachToDebuggerLibX64_vs10.lib")
#else
#pragma comment(lib,"../../libs/AttachToDebuggerLib/lib/AttachToDebuggerLib_vs10.lib")
#endif
#endif

#include "sysutil.h"
#include "file.h"
#include "memorymonitor.h"
#include "foldercache.h"
#include "winutil.h"
#include "gimmeDLLWrapper.h"
#include "serverlib.h"
#include "UtilitiesLib.h"
#include "ResourceInfo.h"

#include "GenericHttpServing.h"

#include "NewControllerTracker.h"
#include "TimedCallback.h"

#include "NewControllerTracker_CriticalSystems.h"
#include "cmdparse.h"
#include "NewControllerTracker.h"
#include "NewControllerTracker_h_ast.h"
#include "NewControllerTracker_pub_h_ast.h"
#include "StructNet.h"
#include "NewControllerTracker_MailingLists.h"
#include "sock.h"
#include "controllerLink.h"
#include "logging.h"
#include "NewControllerTracker_AlertTrackers.h"
#include "netsmtp.h"


ShardCategory **gppShardCategories = NULL;
StashTable gShardsByID = 0;
StashTable gShardsByName = 0;

bool gbYouAreMasterCT = false;
AUTO_CMD_INT(gbYouAreMasterCT, YouAreMasterCT) ACMD_CMDLINE;

char gMasterCT[256] = "";
AUTO_CMD_STRING(gMasterCT, MasterCT);

CommConnectFSM *pMasterCTFSM = NULL;
NetLink *pLinkToMasterCT = NULL;

NetListen *gpMasterCTListen = NULL;

int giNumChildCTsToExpect = 0;
AUTO_CMD_INT(giNumChildCTsToExpect, NumChildCTsToExpect);

ControllerTrackerStaticData gStaticData = {0};
void LoadControllerTrackerStaticData(void)
{
	char fileName[CRYPTIC_MAX_PATH];
	StructReset(parse_ControllerTrackerStaticData, &gStaticData);

	sprintf(fileName, "%s/ControllerTrackerStaticData.txt", fileLocalDataDir());

	ParserReadTextFile(fileName, parse_ControllerTrackerStaticData, &gStaticData, 0);

	resRegisterDictionaryForEArray("PermShards", RESCATEGORY_OTHER, 0, &gStaticData.ppPermanentShards, parse_ShardInfo_Perm);
}

int giChildCTCount;
char *pChildCTString = NULL;

static void MirrorStaticData(NetLink* link, ShardConnectionUserData *pUserData)
{
	Packet *pPak = pktCreate(link, TO_NEWCONTROLLERTRACKER_FROM_MASTER_HERE_IS_STATIC_DATA);
	printf("Sending static data to %s\n", makeIpStr(linkGetIp(link)));
	ParserSendStructSafe(parse_ControllerTrackerStaticData, pPak, &gStaticData);
	pktSend(&pPak);
	giChildCTCount++;
	estrConcatf(&pChildCTString, "%s ", makeIpStr(linkGetIp(link)));
}



void SaveControllerTrackerStaticData(void)
{	char fileName[CRYPTIC_MAX_PATH];
	sprintf(fileName, "%s/ControllerTrackerStaticData.txt", fileLocalDataDir());
	ParserWriteTextFile(fileName, parse_ControllerTrackerStaticData, &gStaticData, 0, 0);

	if (gbYouAreMasterCT)
	{
		estrClear(&pChildCTString);
		giChildCTCount = 0;
		printf("About to send static data update to all child CTs\n");
		linkIterate(gpMasterCTListen, MirrorStaticData);

		if (giNumChildCTsToExpect && giNumChildCTsToExpect != giChildCTCount)
		{
			TriggerAlertf("MISSING_CHILD_CTS", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0, "Master CT was told to expect %d child CTs, only sent data to %d: %s",
				giNumChildCTsToExpect, giChildCTCount, pChildCTString);
		}
	}
}


void controllerTrackerPeriodicUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	ShardComPeriodicUpdate();
}

void controllerTrackerOneSecondUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	CriticalSystems_PeriodicUpdate();
}

#define SECS_BEFORE_MCP_COM 5


int MasterCTConnect(NetLink* link, void *pUserData)
{
	Packet *pPak = pktCreate(link, TO_NEWCONTROLLERTRACKER_FROM_MASTER_HERE_IS_STATIC_DATA);
	printf("Connected as master CT to %s\n", makeIpStr(linkGetIp(link)));
	ParserSendStructSafe(parse_ControllerTrackerStaticData, pPak, &gStaticData);
	pktSend(&pPak);

	linkSetDebugName(link, STACK_SPRINTF("Master CT link to %s", makeIpStr(linkGetIp(link))));
	linkSetTimeout(link, 60);
	linkSetKeepAliveSeconds(link, 15);

	return 1;
}

int MasterCTDisconnect(NetLink* link, void *pUserData)
{
	static char *pDisconnectString = NULL;
	linkGetDisconnectReason(link, &pDisconnectString);

	printf("Disconnected as master CT from %s: %s\n", makeIpStr(linkGetIp(link)), pDisconnectString);
	log_printf(LOG_MISC, "Disconnected as master CT from %s: %s\n", makeIpStr(linkGetIp(link)), pDisconnectString);
	return 1;
}

void FromMasterCTPacketCB(Packet *pak,int cmd, NetLink *link,void *pUserData)
{
	switch (cmd)
	{
	case TO_NEWCONTROLLERTRACKER_FROM_MASTER_HERE_IS_STATIC_DATA:
		StructReset(parse_ControllerTrackerStaticData, &gStaticData);
		ParserRecvStructSafe(parse_ControllerTrackerStaticData, pak, &gStaticData);

		UpdateAllShardPrepatchCommandLines();

		SaveControllerTrackerStaticData();
		break;
	}
}


int FromMasterCTConnect(NetLink* link, void *pUserData)
{
	printf("Connected to master CT\n");
	linkSetTimeout(link, 60);
	linkSetKeepAliveSeconds(link, 15);
	return 1;
}
int FromMasterCTDisconnect(NetLink* link, void *pUserData)
{
	static char *pDisconnectReason = NULL;
	linkGetDisconnectReason(link, &pDisconnectReason);

	printf("Disconnected from master CT: %s\n", pDisconnectReason);
	log_printf(LOG_MISC, "Disconnected from master CT: %s\n", pDisconnectReason);
	linkRemove(&pLinkToMasterCT);
	return 1;
}

void MasterCTConnectionErrorCB(void *pUserData, char *pErrorString)
{
	printf("Error connecting to master CT: %s\n", pErrorString);
	log_printf(LOG_MISC, "Error connecting to master CT: %s\n", pErrorString);
}

int main(int argc,char **argv)
{

bool bStartedMCPCom = false;
U32 iStartTime;
int		i;
int frameTimer;

	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER
	SetAppGlobalType(GLOBALTYPE_CONTROLLERTRACKER);
	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");


	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'T', 0x8080ff);

	//FolderCacheChooseModeNoPigsInDevelopment();
	FolderCacheChooseMode();
	FolderCacheSetManualCallbackMode(1);

	preloadDLLs(0);

	if (fileIsUsingDevData()) {
	} else {
		gimmeDLLDisable(1);
	}

	srand((unsigned int)time(NULL));
	consoleUpSize(110,128);

	ControllerLink_SetNeverConnect(true);

	InitShardCom();


	frameTimer = timerAlloc();

	AlertTrackers_InitSystem();

	printf("Ready.\n");

	NewControllerTracker_InitShardDictionary();

	GenericHttpServing_Begin(CONTROLLERTRACKER_HTTP_PORT, "ControllerTracker", DEFAULT_HTTP_CATEGORY_FILTER, 0);


	TimedCallback_Startup();
	TimedCallback_Add(controllerTrackerPeriodicUpdate, NULL, 5.0f);
	TimedCallback_Add(controllerTrackerOneSecondUpdate, NULL, 1.0f);


	//make sure we don't use a log server unless it's specified on the command line
	sprintf(gServerLibState.logServerHost, "NONE");
	serverLibStartup(argc, argv);
	gServerLibState.bAllowErrorDialog = false;

	CriticalSystems_InitSystem();
	FolderCacheEnableCallbacks(1);

	iStartTime = timeSecondsSince2000();

	LoadControllerTrackerStaticData();

	if (gbYouAreMasterCT)
	{
		loadstart_printf("MasterCT trying to start listen for other CTs...");

		while (!(gpMasterCTListen = commListen(commDefault(),LINKTYPE_SHARD_NONCRITICAL_500K, LINK_FORCE_FLUSH,CONTROLLERTRACKER_SLAVED_CONTROLLERTRACKER_PORT,
				NULL,MasterCTConnect,MasterCTDisconnect,0)))
		{
			Sleep(1);
		}

		loadend_printf("done");
	}

	{
		char *pErrorString = NULL;
		if (!LoadMailingLists("c:\\controllertracker", &pErrorString))
		{
			assertmsgf(0, "Error while doing initial mailing list load: %s", pErrorString);
		}
	}


	for(;;)
	{	
		F32 frametime = timerElapsedAndStart(frameTimer);

		if (gMasterCT[0])
		{
			if (pMasterCTFSM)
			{
				CommConnectFSMStatus eStatus = commConnectFSMUpdate(pMasterCTFSM, &pLinkToMasterCT);
				printf("Master CT connect status: %s\n", StaticDefineIntRevLookup(CommConnectFSMStatusEnum, eStatus));
				if (eStatus == COMMFSMSTATUS_SUCCEEDED)
				{
					commConnectFSMDestroy(&pMasterCTFSM);
					linkSetDebugName(pLinkToMasterCT, "link to master CT");
				}
			}
			else
			{
				if (!pLinkToMasterCT || linkDisconnected(pLinkToMasterCT))
				{
					printf("Master CT disconnected...\n");
					linkRemove(&pLinkToMasterCT);

					if (!pMasterCTFSM)
					{	
						printf("Restarting Master CT FSM\n");

						pMasterCTFSM = commConnectFSM(COMMFSMTYPE_RETRY_FOREVER, 5.0f, commDefault(), LINKTYPE_SHARD_NONCRITICAL_500K, LINK_FORCE_FLUSH,
							gMasterCT, CONTROLLERTRACKER_SLAVED_CONTROLLERTRACKER_PORT, FromMasterCTPacketCB, FromMasterCTConnect, FromMasterCTDisconnect, 0, MasterCTConnectionErrorCB, NULL);
					}
				}
			}
		}


		Sleep(1);
		serverLibOncePerFrame();
		smtpBgThreadingOncePerFrame();

		utilitiesLibOncePerFrame(frametime, 1);
		FolderCacheDoCallbacks();

		commMonitor(commDefault());
		commMonitor(gpShardComm);

		GenericHttpServing_Tick();

//don't start MCP COM until we've been running long enough that any shards which want to register have
		//had time
		if (!bStartedMCPCom && (timeSecondsSince2000() - iStartTime > SECS_BEFORE_MCP_COM))
		{
			bStartedMCPCom = true;
			InitMCPCom();
		}
		TicketValidationOncePerFrame();
		MailingLists_Tick();
	}


	EXCEPTION_HANDLER_END



}



#include "NewControllerTracker_pub_h_ast.c"
#include "NewControllerTracker_h_ast.c"
