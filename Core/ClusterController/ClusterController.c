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
#include "timing.h"

#include "GenericHttpServing.h"
#include "StringCache.h"

#include "controllerpub_h_ast.h"
#include "structNet.h"
#include "ClusterController_c_ast.h"
#include "ResourceInfo.h"
#include "Alerts.h"
#include "TimedCallback.h"
#include "StringUtil.h"
#include "structDefines.h"
#include "cmdparse.h"
#include "logging.h"
#include "ClusterController.h"
#include "ClusterController_h_ast.h"
#include "ClusterController_Commands.h"
#include "..\..\libs\serverlib\pub\shardCluster.h"
#include "SentryServerComm.h"
#include "ClusterController_AutoSettings.h"
#include "..\..\CrossRoads\Common\ShardVariableCommon.h"
#include "ClusterController_ShardVariables.h"
#include "ClusterController_PCLStatusMonitoring.h"
#include "ShardCommon.h"
#include "SimpleStatusMonitoring.h"
#include "sock.h"






static NetListen *spClusterControllerListen = NULL;

StashTable gShardsByName = NULL;

static int siInactivityTimeout = 20;
AUTO_CMD_INT(siInactivityTimeout, InactivityTimeout);



AUTO_COMMAND ACMD_COMMANDLINE;
void AddShard(char *pShardName, char *pMachineName, char *pRestartBatchFile)
{
	const char *pPooled = allocAddString(pShardName);
	Shard *pShard;
	
	if (!gShardsByName)
	{
		gShardsByName = stashTableCreateAddress(16);
		resRegisterDictionaryForStashTable("Shards", RESCATEGORY_SYSTEM, 0, gShardsByName, parse_Shard);

	}

	if (stashFindPointer(gShardsByName, pPooled, NULL))
	{
		ErrorOrAlert("DUP_SHARD_NAME", "Shard name %s was passed into ClusterController twice... this is non-fatal but worrisome",
			pPooled);
		return;
	}

	pShard = StructCreate(parse_Shard);
	pShard->pShardName = pPooled;
	pShard->pRestartBatchFile = strdup(pRestartBatchFile);
	pShard->pMachineName = strdup(pMachineName);
	estrPrintf(&pShard->pInternal, "<a href=\"%s.globObj.Shards[%s]\">Link</a>",
			LinkToThisServer(), pShard->pShardName);

	stashAddPointer(gShardsByName, pPooled, pShard, true);
}




static void UpdateShardSummary(Shard *pShard, ControllerSummaryForClusterController *pSummary)
{
	if (!pShard->pMonitor)
	{
		estrPrintf(&pShard->pMonitor, "<a href=\"http://%s\">Link</a>",
			pShard->pMachineName);
	   estrPrintf(&pShard->pVNC, "<a href=\"cryptic://vnc/%s\">VNC</a>", pShard->pMachineName);
	}


	if (pShard->eState != SHARD_CONNECTED)
	{
		ClusterControllerCommands_ShardNewlyConnected(pShard);
	}
	pShard->eState = SHARD_CONNECTED;
	pShard->iLastConnectionTime = timeSecondsSince2000();

	StructCopy(parse_ControllerSummaryForClusterController, pSummary, &pShard->summary, 0, 0, 0);

	StructDestroy(parse_ControllerSummaryForClusterController, pSummary);
}

static void DisconnectShard(Shard *pShard)
{
	pShard->eState = SHARD_DISCONNECTED;
	pShard->pLink = NULL;

	ClusterControllerCommands_ShardDisconnected(pShard);
}



	


static void ClusterControllerMessageCB(Packet *pak,int cmd, NetLink *link, Shard *pShard)
{
	ControllerSummaryForClusterController *pSummary;

	if (linkFileSendingMode_ReceiveHelper(link, cmd, pak)) return;

	switch (cmd)
	{
	xcase CONTROLLER_TO_CLUSTERCONTROLLER__STATUS:
		pSummary = ParserRecvStructSafe_Create(parse_ControllerSummaryForClusterController, pak);

		if (pShard == UNKNOWN_SHARD)
		{
			return;
		}

		if (!pShard)
		{
			if (!stashFindPointer(gShardsByName, pSummary->pShardNameForSummary, &pShard))
			{
				CRITICAL_NETOPS_ALERT("UNKONWN_SHARD", "Got a status update from shard %s, which we've never heard of",
					pSummary->pShardNameForSummary);
				linkSetUserData(link, UNKNOWN_SHARD);
				return;
			}

			if (stricmp_safe(pSummary->pClusterName, ShardCommon_GetClusterName()) != 0)
			{
				CRITICAL_NETOPS_ALERT("CLUSTER_NAME_MISMATCH", "Got a status update from shard %s, think's its part of cluster %s, should be %s",
					pSummary->pShardNameForSummary, pSummary->pClusterName, ShardCommon_GetClusterName());
				linkSetUserData(link, UNKNOWN_SHARD);
				return;
			}
		
			if (stricmp_safe(pSummary->pProductName, GetProductName()) != 0)
			{
				CRITICAL_NETOPS_ALERT("CLUSTER_PRODUCT_MISMATCH", "Got a status update from shard %s, think's its product is %s, should be %s",
					pSummary->pShardNameForSummary, pSummary->pProductName, GetProductName());
				linkSetUserData(link, UNKNOWN_SHARD);
				return;
			}


			linkSetUserData(link, pShard);
			linkSetDebugName(link, STACK_SPRINTF("Link to shard %s", pShard->pShardName));
		}

		pShard->pLink = link;
		UpdateShardSummary(pShard, pSummary);

		break;

	xcase CONTROLLER_TO_CLUSTERCONTROLLER__COMMAND_RETURN:
		HandleCommandReturn(pak, pShard);

	xcase CONTROLLER_TO_CLUSTERCONTROLLER__HERE_ARE_AUTO_SETTINGS:
		HandleHereAreAutoSettings(pak, pShard);

	xcase CONTROLLER_TO_CLUSTERCONTROLLER__HERE_ARE_SHARD_VARIABLES:
		ClusterController_HandleHereAreShardVariables(pak, pShard);
	}

}

void ClusterControllerConnectCB(NetLink *link, Shard *pShard)
{
	linkFileSendingMode_InitSending(link);
}

void ClusterControllerDisconnectCB(NetLink *link, Shard *pShard)
{
	if (pShard && pShard != UNKNOWN_SHARD)
	{
		DisconnectShard(pShard);
	}
}

void ClusterController_OncePerSecond(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	FOR_EACH_IN_STASHTABLE(gShardsByName, Shard, pShard)
	{
		if (pShard->eState == SHARD_CONNECTED && pShard->iLastConnectionTime < timeSecondsSince2000() - siInactivityTimeout)
		{
			CRITICAL_NETOPS_ALERT("INACTIVE_SHARD", "We still have a net connection to shard %s, but we haven't heard from it in %d seconds... setting it to disconnected",
				pShard->pShardName, siInactivityTimeout);
			DisconnectShard(pShard);
		}
	}
	FOR_EACH_END;

	ClusterControllerCommands_OncePerSecond(timeSinceLastCallback);

	
}

void ClusterController_TickPerLink(NetLink *pLink, Shard *pShard)
{
	linkFileSendingMode_Tick(pLink);
}

void ClusterController_Tick(void)
{
	linkIterate(spClusterControllerListen, ClusterController_TickPerLink);
}

void FileSendTest_Success(char *pFileName, char *pErrorString, void *pUserData)
{
	printf("SUCCESS! Sent %s\n", pFileName);
}

void FileSendTest_Failure(char *pFileName, char *pErrorString, void *pUserData)
{
	printf("FAILURE! While sending %s, %s\n", pFileName, pErrorString);
}

AUTO_STRUCT;
typedef struct ClusterController_SimpleServerStatusWrapper
{
	char *pName;
	char *pVNC; AST(ESTRING, FORMATSTRING(HTML=1))
	char *pMonitor; AST(ESTRING, FORMATSTRING(HTML=1))

	int	iConnected; AST(FORMATSTRING(HTML_CLASS_IFEXPR = "$ = 0 ; divRed" ))
	float fps;



	AST_COMMAND("Restart", "SimpleStatusMonitoring_RestartSystem $FIELD(Name)")
	AST_COMMAND("Shutdown", "SimpleStatusMonitoring_TellSystemToShutDown $FIELD(Name)")
} ClusterController_SimpleServerStatusWrapper;


AUTO_STRUCT;
typedef struct ClusterControllerOverview
{
	char *pFileSending; AST(FORMATSTRING(HTML_PREFORMATTED=1))
	Shard **ppShards;

	ClusterController_SimpleServerStatusWrapper **ppOtherServers;

	ClusterControllerPCLState *pPatching;

	char *pGenericInfo; AST(NO_LOG, ESTRING, FORMATSTRING(HTML=1, HTML_NO_HEADER=1))


	AST_COMMAND("Prime all shards", "SendCommandToShard ALL DoPrePatching $STRING(Patch version) $INT(How many machine groups to prime) $STRING(Are you really sure? Type yes here if you are. Double check your patch version!) $STRING(Extra patching command line)")
	AST_COMMAND("Reload cluster-local mapmanager config.txt", "UpdateMapManagerConfigClusterLocal $CONFIRM(This will load the local-HD copy of MapManagerConfig_ClusterLocal.txt and send it to all MapManagers, then reload)")
	AST_COMMAND("Request AUTO_SETTINGS", "RequestAutoSettings $CONFIRM(This will ask all shards for AUTO_SETTINGS, and populate the AUTO_SETTING objects locally)")
	AST_COMMAND("Request ShardVariables", "RequestAllShardVariables $CONFIRM(This will ask all shards for ShardVariables, and populate the ShardVariables objects locally)")
	AST_COMMAND("Apply Frankenbuild", "SendCommandToShardWithTimeout ALL 240 BeginFrankenBuildHotPush $INT(ID number) $STRING(Comment)")
	AST_COMMAND("Request Frankenbuild", "SendFrankenBuildToShard All $STRING(Base filename)")
	AST_COMMAND("Set MT KillSwitch", "SendCommandToShard ALL MTKillSwitch $INT(Set)")
	AST_COMMAND("Set Billing KillSwitch", "SendCommandToShard ALL BillingKillSwitch $INT(Set)")
	AST_COMMAND("Gateway lock all shards", "SendCommandToShard All GatewayLockTheShard $INT(Set)")
	AST_COMMAND("Lock all shards", "SendCommandToShard ALL LockTheShard $INT(Lock)")
	AST_COMMAND("Kill all shards", "KillAllShards $STRING(Type cluster name to confirm)")
	AST_COMMAND("Boot all players", "SendCommandToShard ALL BootEveryone $STRING(Type yes to boot everyone) 1")
	AST_COMMAND("Broadcast a message", "SendCommandToShard ALL BroadcastMessage $STRING(Message to Broadcast)")
} ClusterControllerOverview;



void OVERRIDE_LATELINK_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct)
{
	static ClusterControllerOverview *pOverview = NULL;
	
	if (!pOverview)
	{
		pOverview = StructCreate(parse_ClusterControllerOverview);
	}

	eaClear(&pOverview->ppShards);
	eaDestroyStruct(&pOverview->ppOtherServers, parse_ClusterController_SimpleServerStatusWrapper);

	pOverview->pFileSending = netGetFileSendingSummaryString();
	FOR_EACH_IN_STASHTABLE(gShardsByName, Shard, pShard)
	{
		eaPush(&pOverview->ppShards, pShard);
	}
	FOR_EACH_END;

	FOR_EACH_IN_STASHTABLE(gSimpleMonitoringStatusByName, SimpleMonitoringStatus, pSimpleStatus)
	{
		ClusterController_SimpleServerStatusWrapper *pStatus = StructCreate(parse_ClusterController_SimpleServerStatusWrapper);
		U32 iMonitoringPort = 0;
		const char *pAddressString = NULL;

		pStatus->pName = strdup(pSimpleStatus->pName);
		pStatus->iConnected = pSimpleStatus->bConnected;
		pStatus->fps = pSimpleStatus->status.status.fFPS;

		if (pSimpleStatus->status.iMyMainMonitoringPort)
		{
			iMonitoringPort = pSimpleStatus->status.iMyMainMonitoringPort;
		}
		else
		{
			iMonitoringPort = pSimpleStatus->status.iMyGenericMonitoringPort;
		}
		

		if (pSimpleStatus->status.pMyMachineName)
		{
			pAddressString = pSimpleStatus->status.pMyMachineName;
		}
		else if (pSimpleStatus->iIP)
		{
			pAddressString = makeIpStr(pSimpleStatus->iIP);
		}
		else if (pSimpleStatus->pMachineName)
		{
			pAddressString = pSimpleStatus->pMachineName;
		}

		if (pAddressString)
		{
			estrPrintf(&pStatus->pVNC, "<a href=\"cryptic://vnc/%s\">VNC</a>", pAddressString);

			if (iMonitoringPort)
			{
				estrPrintf(&pStatus->pMonitor, "<a href=\"http://%s:%d\">Monitor</a>", pAddressString, 
					iMonitoringPort);
			}
		}

		eaPush(&pOverview->ppOtherServers, pStatus);
	}
	FOR_EACH_END;


	estrPrintf(&pOverview->pGenericInfo, "<input type=\"hidden\" id=\"defaultautorefresh\" value=\"1\"><a href=\"/viewxpath?xpath=%s[%u].generic\">Generic ServerLib info for the %s</a>",
		GlobalTypeToName(GetAppGlobalType()), gServerLibState.containerID,GlobalTypeToName(GetAppGlobalType()));

	pOverview->pPatching = ClusterController_GetPCLStatus();

	*ppTPI = parse_ClusterControllerOverview;
	*ppStruct = pOverview;

}



void OVERRIDE_LATELINK_AddCustomHTMLFooter(char **ppOutString)
{
	estrConcatf(ppOutString,"<h1 style=\"position:fixed; left:4px; top:-10px; opacity:.7; z-index:1; color:#500\">Cluster:%s(%s)(%s)</h1>",
		ShardCommon_GetClusterName(), GetProductName(), GetUsefulVersionString_Short());


}

int main(int argc,char **argv)
{

	int		i;


	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER
	SetAppGlobalType(GLOBALTYPE_CLUSTERCONTROLLER);
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




	//make sure we don't use a log server unless it's specified on the command line
	sprintf(gServerLibState.logServerHost, "NONE");
	serverLibStartup(argc, argv);
	gServerLibState.bAllowErrorDialog = false;

	FolderCacheEnableCallbacks(1);


	spClusterControllerListen = commListen(commDefault(),LINKTYPE_SHARD_NONCRITICAL_20MEG, 
		LINK_FORCE_FLUSH,CLUSTERCONTROLLER_PORT,
		ClusterControllerMessageCB,ClusterControllerConnectCB, ClusterControllerDisconnectCB, 0);

	assertmsgf(spClusterControllerListen, "Unable to listen on pord %d... is another ClusterController running?",
		CLUSTERCONTROLLER_PORT);

	GenericHttpServing_Begin(CLUSTERCONTROLLER_HTTP_PORT, GetProductName(), DEFAULT_HTTP_CATEGORY_FILTER, 0);

	TimedCallback_Add(ClusterController_OncePerSecond, NULL, 1.0f);


	assertmsgf(ShardCommon_GetClusterName(), "Cluster controller must have cluster name set (-ShardClusterName)");
	assertmsgf(GetProductName_IfSet(), "Cluster controller must have product set (-SetProductName)");

	ClusterController_PCLStatusMonitoring_Begin();

	{
		char consoleTitle[1024];
		sprintf(consoleTitle, "ClusterController - %s(%s)(%s)",
			ShardCommon_GetClusterName(), GetProductName(), GetUsefulVersionString_Short());
		setConsoleTitle(consoleTitle);
	}

	SimpleStatusMonitoring_Begin();

	for(;;)
	{	


		Sleep(1);
		serverLibOncePerFrame();
		

		utilitiesLibOncePerFrame(REAL_TIME);
		FolderCacheDoCallbacks();

		commMonitor(commDefault());
	

		GenericHttpServing_Tick();

		ClusterController_Tick();
		SentryServerComm_Tick();
		ClusterController_PCLStatusMonitoring_Tick();

	}


	EXCEPTION_HANDLER_END



}


#include "ClusterController_c_ast.c"
#include "ClusterController_h_ast.c"
