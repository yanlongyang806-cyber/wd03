/***************************************************************************



***************************************************************************/

#include "ServerLib.h"
#include "svrError.h"
#include "logcomm.h"
#include "sock.h"
#include "file.h"
#include "fileutil.h"
#include "fileutil2.h"
#include "FolderCache.h"
#include "crypt.h"
#include "structNet.h"
#include "Autogen/controller_autogen_remotefuncs.h"
#include "../../core/controller/pub/controllerpub.h"
#include "GlobalStateMachine.h"
#include "utilitiesLib.h"
#include "Autogen/ServerLib_h_ast.h"
#include "winutil.h"
#include "HttpXpathSupport.h"
#include "LocalTransactionManager_internal.h"
#include "ControllerLink.h"
#include "alerts.h"
#include "GlobalEnums_h_ast.h"
#include "MapDescription.h"
#include "MapDescription_h_ast.h"
#include "StringCache.h"
#include "ResourceSystem_Internal.h"
#include "GenericFileServing.h"
#include "timedCallback.h"
#include "netipfilter.h"
#include "Autogen/ServerLib_autogen_remotefuncs.h"
#include "accountNet.h"
#include "StatusReporting.h"
#include "GenericHttpServing.h"
#include "timedCallback.h"
#include "UGCProjectCommon.h"
#include "StringUtil.h"
#include "osdependent.h"
#include "AutoSettings.h"
#include "MemReport.h"
#include "MemoryBudget.h"
#include "AutoTransSupport.h"
#include "XMLRPC.h"
#include "HttpLib.h"
#include "../../Crossroads/AppServerLib/Pub/aslJobManagerPub.h"
#include "AslJobManagerPub_h_ast.h"
#include "remoteCommandGroup.h"
#include "../../Crossroads/AppServerLib/Pub/aslResourceDBPub.h"
#include "serverlib_c_ast.h"

#define SECS_BETWEEN_FPS_SENDS 5

extern ParseTable parse_ContainerRef[];
#define TYPE_parse_ContainerRef ContainerRef

static bool sbNoServerLogPrintf = false;
AUTO_CMD_INT(sbNoServerLogPrintf, NoServerLogPrintf);

ServerLibLoadedConfig gServerLibLoadedConfig = { "localhost", "localhost", "localhost" };

ServerLibState gServerLibState = { 0 };

static U32 sServerLibMainThread = 0;

NetLink *gpLinkToLauncher = NULL;

static S32 printContainerDiffStrings;
AUTO_CMD_INT(printContainerDiffStrings, printContainerDiffStrings) ACMD_CMDLINE;
																   
//if set, then generate the VO files and then exit immediately
bool gbMakeVOTXTFilesAndExit = false;
AUTO_CMD_INT(gbMakeVOTXTFilesAndExit, MakeVOTXTFilesAndExit);

AUTO_RUN;
void InitServerLibState(void)
{
	sprintf(gServerLibState.controllerHost,"localhost");
}

// Startup commands for serverlib

// Set Server's container ID
AUTO_CMD_INT(gServerLibState.containerID,ContainerID) ACMD_CMDLINE;

// Set Server's anti-zombification cookie
AUTO_CMD_INT(gServerLibState.antiZombificationCookie,Cookie) ACMD_CMDLINE;

// Set address for where to find controller
AUTO_CMD_STRING(gServerLibState.controllerHost,ControllerHost) ACMD_CMDLINE;

// Set address for where to find LogServer
AUTO_CMD_STRING(gServerLibState.logServerHost,LogServer) ACMD_CMDLINE;

// Sets whether to connect to log server via Multiplexer
AUTO_CMD_INT(gServerLibState.bUseMultiplexerForLogging,LogServerUsesMultiPlex) ACMD_CMDLINE;

// Set address for where to find TransactionServer
AUTO_CMD_STRING(gServerLibState.transactionServerHost,TransactionServer) ACMD_CMDLINE;

// Sets whether to connect to transaction server via Multiplexer
AUTO_CMD_INT(gServerLibState.bUseMultiplexerForTransactions,TransactionServerUsesMultiPlex) ACMD_CMDLINE;

AUTO_CMD_INT(gServerLibState.removeBins, RemoveBins) ACMD_CMDLINE;

//if true, then gamserver, mapmanager and loginserver work with account proxy server to restrict map access
AUTO_CMD_INT(gServerLibState.bUseAccountPermissionsForMapTransfer, UseAccountPermissionsForMapTransfer) ACMD_CMDLINE;

// allow developers to load keys for account
AUTO_CMD_INT(gServerLibState.bAllowDeveloperKeyAccess, KeyAccess) ACMD_CMDLINE;

// Write schemas and exit
AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_NAME(WriteSchemasAndExit);
void setWriteSchemasAndExit(bool bSet)
{
	gServerLibState.writeSchemasAndExit = bSet;
}

// Load all public maps and fixup layers with broken references.
AUTO_COMMAND ACMD_CMDLINE;
void fixupAllMaps(bool bSet)
{
	gServerLibState.fixupAllMapsAndExit = bSet;
}

// Load all public maps and fixup layers with broken references.
AUTO_COMMAND ACMD_CMDLINE;
void fixupAllMapsDryRun(bool bSet)
{
	gServerLibState.fixupAllMapsAndExit = bSet;
	gServerLibState.fixupAllMapsDryRun = bSet;
}

// Load all public maps and fixup layers with broken references.
AUTO_COMMAND ACMD_CMDLINE;
void calcSizesAllMaps(bool bSet)
{
	gServerLibState.calcSizesAllMapsAndExit = bSet;
}

AUTO_CMD_STRING(gServerLibState.validateUGCProjects, ValidateUGCProjects) ACMD_CMDLINE;

// Sets rather we should not load any data
AUTO_CMD_INT(gServerLibState.dontLoadData,DontLoadData)  ACMD_CMDLINE;

//NEEDS A COMMENT
AUTO_CMD_INT(gServerLibState.dontLoadExternData, DontLoadExtern)  ACMD_CMDLINE;

// Sets rather we should run a profiler
AUTO_CMD_INT(gServerLibState.bProfile,Profile)  ACMD_CMDLINE;


//tells a server to directly do generic http serving, so it can be servermonitored directly, rather than 
//routing through controller/etc
AUTO_CMD_INT(gServerLibState.iGenericHttpServingPort, DoGenericHttpServing) ACMD_CMDLINE;

void slAuxAssertFunc_SendAssertsToController(const char* expr, const char* errormsg, const char* filename, unsigned lineno)
{
	char assertString[4096];
	sprintf(assertString, "ASSERTION FAILED: %s -- %s -- %s(%d) (reported by %s)",
		expr, errormsg, filename, lineno, GlobalTypeToName(GetAppGlobalType()));


	if (!GetControllerLink() && objLocalManager() && objLocalManager()->iThreadID == GetCurrentThreadId())
	{
		RemoteCommand_SendErrorDialogToMCPThroughController(GLOBALTYPE_CONTROLLER, 0, assertString, 
			 "",  "", 0);
		return;
	}

	if (!GetControllerLink())
	{
		AttemptToConnectToController(true, NULL, false);
	}

	if (GetControllerLink())
	{
		Packet *pPak = pktCreate(GetControllerLink(), TO_CONTROLLER_ERROR_DIALOG_FOR_MCP);

		pktSendString(pPak, assertString);
		pktSendString(pPak, NULL);
		pktSendString(pPak, NULL);
		pktSendBitsPack(pPak, 32, 0);

		pktSend(&pPak);
	}



}


//sets whether all errors should be sent to the MCP
AUTO_COMMAND ACMD_NOTESTCLIENT  ACMD_CMDLINE; 
void SendAllErrorsToController(int bSend)
{
	gServerLibState.bSendAllErrorsToController = bSend;
	gbDontDoAlerts = true;
	if (bSend)
	{
		SetAuxAssertCB(slAuxAssertFunc_SendAssertsToController);
	}
}


//extern volatile int startProfile;
//extern volatile int stopProfile;
static CRITICAL_SECTION serverLibErrorDialogCS;

typedef struct DeferredServerLibErrorDialog
{
	HWND hwnd;
	char *str;
	char *title;
	char *fault;
	int highlight;
} DeferredServerLibErrorDialog;


static DeferredServerLibErrorDialog **sppDeferredServerLibErrorDialogs = NULL;
AUTO_RUN_EARLY;
void ServerlibAutoInitErrorCriticalSection(void)
{
	InitializeCriticalSection(&serverLibErrorDialogCS);

	//for all in-shard things, we don't want to be retrying failed IP lookups frequently, as that causes
	//huge stalls
	giDelayBeforeRetryingFailedIPLookup_Default = 240;
}


static void serverLibErrorDialog_internal(HWND hwnd, char *str, char* title, char* fault, int highlight, void *userdata)
{
	PERFINFO_AUTO_START_FUNC();
	if (!GetControllerLink() && objLocalManager() && CanTransactionsBeRequested(objLocalManager()) && !IsLocalManagerFullyLocal(objLocalManager()))
	{
		RemoteCommand_SendErrorDialogToMCPThroughController(GLOBALTYPE_CONTROLLER, 0, STACK_SPRINTF("%s (reported by %s)", str ? str : "", GlobalTypeToName(GetAppGlobalType())), 
			title ? title : "", fault ? fault : "", highlight);
		PERFINFO_AUTO_STOP();
		return;
	}
	
	if (!GetControllerLink() && (stricmp(gServerLibState.controllerHost, "NONE") != 0))
	{
		AttemptToConnectToController(true, NULL, false);
	}

	if (GetControllerLink())
	{
		Packet *pPak;
		
		pktCreateWithCachedTracker(pPak, GetControllerLink(), TO_CONTROLLER_ERROR_DIALOG_FOR_MCP);

		pktSendString(pPak, STACK_SPRINTF("%s (reported by %s)", str, GlobalTypeToName(GetAppGlobalType())));
		pktSendString(pPak, title);
		pktSendString(pPak, fault);
		pktSendBitsPack(pPak, 32, highlight);

		pktSend(&pPak);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (!gServerLibState.bSendAllErrorsToController)
	{
		errorDialogInternal(hwnd, str, title, fault, highlight);
	}
	PERFINFO_AUTO_STOP();
}


void serverLibErrorDialog(HWND hwnd, char *str, char* title, char* fault, int highlight, void *userdata)
{
	PERFINFO_AUTO_START_FUNC();
	if (GetCurrentThreadId() == sServerLibMainThread )
	{
		serverLibErrorDialog_internal(hwnd, str, title, fault, highlight, userdata);
	}
	else
	{
		PERFINFO_AUTO_START("deferred", 1);
		{
			DeferredServerLibErrorDialog *pDeferredDialog = calloc(sizeof(DeferredServerLibErrorDialog), 1);
			pDeferredDialog->hwnd = hwnd;
			pDeferredDialog->str = str ? strdup(str) : NULL;
			pDeferredDialog->title = title? strdup(title) : NULL;
			pDeferredDialog->fault = fault ? strdup(fault) : NULL;
			pDeferredDialog->highlight = highlight;

			EnterCriticalSection(&serverLibErrorDialogCS);
			eaPush(&sppDeferredServerLibErrorDialogs, pDeferredDialog);
			LeaveCriticalSection(&serverLibErrorDialogCS);
		}
		PERFINFO_AUTO_STOP();
	}
	PERFINFO_AUTO_STOP();
}

void serverLibEnableErrorDialog(bool bEnable)
{
	gServerLibState.bAllowErrorDialog = bEnable;
}

void UpdateDeferredServerLibDialogs(void)
{
	DeferredServerLibErrorDialog **ppDialogs;
	int i;

	EnterCriticalSection(&serverLibErrorDialogCS);
	ppDialogs = sppDeferredServerLibErrorDialogs;
	sppDeferredServerLibErrorDialogs = NULL;
	LeaveCriticalSection(&serverLibErrorDialogCS);

	for (i=0; i < eaSize(&ppDialogs); i++)
	{
		serverLibErrorDialog_internal(ppDialogs[i]->hwnd, ppDialogs[i]->str, ppDialogs[i]->title, ppDialogs[i]->fault, ppDialogs[i]->highlight, NULL);
		SAFE_FREE(ppDialogs[i]->str);
		SAFE_FREE(ppDialogs[i]->title);
		SAFE_FREE(ppDialogs[i]->fault);
		free(ppDialogs[i]);
	}

	eaDestroy(&ppDialogs);
}

bool DEFAULT_LATELINK_SpecialMapTypeCorruptionAlertHandling(const char *pKey, const char *pString)
{
	return false;
}

bool CheckForSpecialIntraShardAlertHandling(const char *pKey, const char *pString)
{
	static const char *spMAPTYPECORRUPTION = NULL;
	if (!spMAPTYPECORRUPTION)
	{
		spMAPTYPECORRUPTION = allocAddString(MAPTYPECORRUPTION);
	}

	if (pKey == spMAPTYPECORRUPTION)
	{
		if (SpecialMapTypeCorruptionAlertHandling(pKey, pString))
		{
			return true;
		}
	}

	return false;
}
//all servers in a shard send all alerts to the controller
bool SendAlertToController(const char *pKey, const char *pString, enumAlertLevel eLevel, enumAlertCategory eCategory, int iLifespan,
	  GlobalType eContainerTypeOfObject, ContainerID iIDOfObject, GlobalType eContainerTypeOfServer, ContainerID iIDOfServer,
	  const char *pMachineName, int iErrorID )
{
	if (CheckForSpecialIntraShardAlertHandling(pKey, pString))
	{
		return true;
	}

	if (GetControllerLink())
	{
		Packet *pPak = pktCreate(GetControllerLink(), TO_CONTROLLER_HERE_IS_ALERT);

		pktSendString(pPak, pKey);
		pktSendString(pPak, pString);
		pktSendBits(pPak, 32, eLevel);
		pktSendBits(pPak, 32, eCategory);
		pktSendBits(pPak, 32, iLifespan);
		PutContainerTypeIntoPacket(pPak, eContainerTypeOfObject);
		PutContainerIDIntoPacket(pPak, iIDOfObject);
		PutContainerTypeIntoPacket(pPak, eContainerTypeOfServer);
		PutContainerIDIntoPacket(pPak, iIDOfServer);
		pktSendString(pPak, pMachineName);
		pktSendBits(pPak, 32, iErrorID);

		pktSend(&pPak);

		return true;
	}
	else if (objLocalManager() && !IsLocalManagerFullyLocal(objLocalManager()) && objLocalManager()->iThreadID == GetCurrentThreadId())
	{
		RemoteCommand_ControllerAlertNotify(GLOBALTYPE_CONTROLLER, 0, pKey, pString, eLevel, eCategory, iLifespan, eContainerTypeOfObject, iIDOfObject, eContainerTypeOfServer, iIDOfServer, pMachineName, iErrorID);
		return true;
	}

	return false;
}

void SendAllLocallySavedAlertsToController(void)
{
	static bool sbSent = false;

	if (sbSent)
	{
		return;
	}

	if (resDictGetNumberOfObjects("Alerts"))
	{
		ResourceIterator iter = {0};
		Alert *pAlert = NULL;
		char *pAlertName = NULL;

		resInitIterator("Alerts", &iter);

		while (resIteratorGetNext(&iter, &pAlertName, &pAlert))
		{
			char *pTempString = NULL;
			estrPrintf(&pTempString, "(Happened during server startup %d seconds ago) %s",
				timeSecondsSince2000() - pAlert->iMostRecentHappenedTime, pAlert->pString);

			SendAlertToController(pAlert->pKey, pTempString, pAlert->eLevel, pAlert->eCategory,
				pAlert->iLifespan, pAlert->eContainerTypeOfObject, pAlert->iIDOfObject,
				pAlert->eContainerTypeOfServer, pAlert->iIDOfServer, pAlert->pMachineName,
				pAlert->iErrorID);

			estrDestroy(&pTempString);
		}

		resFreeIterator(&iter);
		sbSent = true;
	}


}

void OVERRIDE_LATELINK_ConnectedToController_ServerSpecificStuff(void)
{
	SendAllLocallySavedAlertsToController();
}

void ServerLib_ConnectedToTransServer(void)
{
	SendAllLocallySavedAlertsToController();
}


static void ReceiveContainerCopy(Packet *pak);

static int sLogMemReportSeconds = HOURS(1);
AUTO_CMD_INT(sLogMemReportSeconds, LogMemReportSeconds) ACMD_COMMANDLINE;

static int sLogMemShortReportSeconds = MINUTES(10);
AUTO_CMD_INT(sLogMemShortReportSeconds, LogMemShortReportSeconds) ACMD_COMMANDLINE;

// name should have no spaces in it
void serverLibLogMemReport(const char *name)
{
	char *pReport = NULL;
	assert(name && name[0]);

	PERFINFO_AUTO_START_FUNC();
	estrStackCreateSize(&pReport, 1024 * 1024);
	memMonitorPerLineStatsInternal(estrConcatHandler, &pReport, NULL, 0, 0);
	SERVLOG_PAIRS(LOG_MEMREPORT, "MemReport", ("Name", "%s", name) ("Report", "%s", pReport));
	estrClear(&pReport);
	memMonitorPerLineStatsInternal(estrConcatHandler, &pReport, NULL, 0, 1);
	SERVLOG_PAIRS(LOG_MEMREPORT, "MemReportNoUser", ("Name", "%s", name) ("Report", "%s", pReport));
	estrDestroy(&pReport);
	PERFINFO_AUTO_STOP();
}

void serverLibLogMemReportCB(TimedCallback *callback, F32 timeSinceLastCallback, void *userdata)
{
	PERFINFO_AUTO_START_FUNC();
	serverLibLogMemReport("Periodic");
	TimedCallback_Run(serverLibLogMemReportCB, userdata, sLogMemReportSeconds);
	PERFINFO_AUTO_STOP();
}

// name should have no spaces in it
void serverLibLogMemShortReport(const char *name)
{
	char *pReport = NULL;
	assert(name && name[0]);

	PERFINFO_AUTO_START_FUNC();
	estrStackCreateSize(&pReport, 1024 * 1024);
	memMonitorPerLineStatsInternal(estrConcatHandler, &pReport, NULL, 50, 0);
	SERVLOG_PAIRS(LOG_MEMREPORT, "MemShortReport", ("Name", "%s", name) ("Report", "%s", pReport));
	estrClear(&pReport);
	memMonitorPerLineStatsInternal(estrConcatHandler, &pReport, NULL, 50, 1);
	SERVLOG_PAIRS(LOG_MEMREPORT, "MemShortReportNoUser", ("Name", "%s", name) ("Report", "%s", pReport));
	estrDestroy(&pReport);
	PERFINFO_AUTO_STOP();
}

void serverLibLogMemShortReportCB(TimedCallback *callback, F32 timeSinceLastCallback, void *userdata)
{
	PERFINFO_AUTO_START_FUNC();
	serverLibLogMemShortReport("Periodic");
	TimedCallback_Run(serverLibLogMemShortReportCB, userdata, sLogMemShortReportSeconds);
	PERFINFO_AUTO_STOP();
}

void serverLibStartup(int argc, char **argv)
{
	sServerLibMainThread = GetCurrentThreadId();

	loadstart_printf("Starting ServerLib...");

	utilitiesLibStartup();

	fileLoadGameDataDirAndPiggs();

	ParserReadTextFile("server/ServerLibCfg.txt", parse_ServerLibLoadedConfig, &gServerLibLoadedConfig, 0);

	sockStart();

	gServerLibState.transactionServerPort = DEFAULT_TRANSACTIONSERVER_PORT;

	assert(GetAppGlobalType() != GLOBALTYPE_NONE);

	cmdParseCommandLine(argc, argv);

	ipfLoadDefaultFilters();

	if (gServerLibState.containerID == 0)
	{
		gServerLibState.containerID = 1;
	}

	// By default, all servers should do FRAMEPERF logging.
	utilitiesLibEnableFramePerfLogging(true);

	timeBeginPeriod(1);
	cryptMD5Init();

	if (!gServerLibState.transactionServerHost[0])
	{
		sprintf(gServerLibState.transactionServerHost, "localhost");
	}
	if (!gServerLibState.logServerHost[0])
	{
		sprintf(gServerLibState.logServerHost, "localhost");
	}

	if (GetAppGlobalType() != GLOBALTYPE_LOGSERVER && GetAppGlobalType() != GLOBALTYPE_LAUNCHER)
	{
		if (stricmp(gServerLibState.logServerHost, "NONE") == 0 || gServerLibState.dontLoadData)
		{
			printf("Logserver NONE requested... logging locally\n");
		}
		else if (gServerLibState.bUseMultiplexerForLogging)
		{
			printf("Using multiplexer for logging, connecting\n");
			svrLogInit();
		}
		else if (gServerLibState.logServerHost[0])
		{
			printf("Log server found, connecting\n");
			svrLogInit();
		}
		else
		{
			printf("No log server found, printing locally\n");
		}
	}

	// set up transaction manager here, once we add the db one


	gServerLibState.bAllowErrorDialog = 1;
	

	if (gServerLibState.writeSchemasAndExit || gbMakeBinsAndExit
		|| gServerLibState.fixupAllMapsAndExit || gbMakeVOTXTFilesAndExit)
	{
		gServerLibState.bAllowErrorDialog = 0;
	}

	if (isProductionMode())
	{
		gServerLibState.bAllowErrorDialog = 0;
	}

	ErrorfSetCallback(serverErrorfCallback, NULL);	
	ErrorSetAlwaysReportDespiteDuplication(true);

	RefSystem_Init();

	if (gServerLibState.bProfile)
	{
		timerRecordStart(GlobalTypeToName(GetAppGlobalType()));
		//startProfile = 1;
	}

	setErrorDialogCallback(serverLibErrorDialog, NULL);

	if (GetAppGlobalType() != GLOBALTYPE_CONTROLLER && StatusReporting_GetState() == STATUSREPORTING_OFF)
	{
		AddAlertRedirectionCB(SendAlertToController);
	}

	if (GetAppGlobalType() != GLOBALTYPE_OBJECTDB)
	{
		RegisterSimplePacketRemoteCommandFunc("ReceiveContainerCopy", ReceiveContainerCopy);
	}

	if (gConf.bUserContent)
	{
		ResourceNameSpaceIterator iter;
		ResourceNameSpace *pSpace;
		resNameSpaceInitIterator(&iter);

		while (pSpace = resNameSpaceIteratorGetNext(&iter))
		{
			ResourceNameSpace tempSpace = {0};
			char filename[MAX_PATH];
			int i;
			sprintf(filename, "%s:/%s.namespace", pSpace->pName, pSpace->pName);
			verbose_printf("Reading namespace from %s\n", filename);
			StructInit(parse_ResourceNameSpace, &tempSpace);
			if(!ParserReadTextFile(filename, parse_ResourceNameSpace, &tempSpace, 0))
			{
				// No file, so just setup some sane defaults
				tempSpace.pName = StructAllocString(pSpace->pName);
				eaPush(&tempSpace.ppDependencies, StructAllocString(pSpace->pName));
			}
			assert(pSpace->pName);
			assertmsgf(stricmp(tempSpace.pName, pSpace->pName) == 0, "Namespace in definition file %s does not match filename", filename);
			for (i = 0; i < eaSize(&tempSpace.ppDependencies); i++)
			{
				assertmsgf(resNameSpaceGetByName(tempSpace.ppDependencies[i]), "Namespace %s depended on by namespace %s does not exist", tempSpace.ppDependencies[i], pSpace->pName);
			}
			StructCopy(parse_ResourceNameSpace, &tempSpace, pSpace, 0, 0, 0);
			StructDeInit(parse_ResourceNameSpace, &tempSpace);
		}		
	}


	if (gServerLibState.iGenericHttpServingPort)
	{
		loadstart_printf("Beginning generic http server...\n");
		GenericHttpServing_Begin(gServerLibState.iGenericHttpServingPort, GetProductName(), DEFAULT_HTTP_CATEGORY_FILTER, 0);
		loadend_printf("...done.");
	}

	// Periodic logging of mem reports. We really don't want game servers to do this.
	if (!IsGameServerBasedType())
	{
		TimedCallback_Run(serverLibLogMemReportCB, NULL, 1.0f);
		TimedCallback_Run(serverLibLogMemShortReportCB, NULL, 1.0f);
	}

	loadend_printf("ServerLib startup done.");
}

void LauncherFpsLinkDisconnect(NetLink* link,void *user_data)
{
	if (GetAppGlobalType() != GLOBALTYPE_GAMESERVER)
	{
		char *pDisconnectReason = NULL;
		estrStackCreate(&pDisconnectReason);

		linkGetDisconnectReason(link, &pDisconnectReason);
		TriggerAlertf("DISCONNECT_FROM_LAUNCHER", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0,  
			"%s lost link to launcher for fps reporting due to %s... this may result in erroneous controller alerts",
			GlobalTypeAndIDToString(GetAppGlobalType(), GetAppGlobalID()), pDisconnectReason);
		estrDestroy(&pDisconnectReason);
	}

	linkRemove_wReason(&gpLinkToLauncher, "LauncherFpsLinkDisconnect");


}

AUTO_STRUCT;
typedef struct ChildProcToReport
{
	U32 iPid;
	char *pProcName; AST(ESTRING)
} ChildProcToReport;

static ChildProcToReport **sppChildProcsToReport = NULL;

void ReportOwnedChildProcess(char *pChildProcName, U32 iChildProcPid)
{
	ChildProcToReport *pProc = StructCreate(parse_ChildProcToReport);
	pProc->iPid = iChildProcPid;

	estrGetDirAndFileNameAndExtension(pChildProcName, NULL, &pProc->pProcName, NULL);

	eaPush(&sppChildProcsToReport, pProc);
}


//everything with serverLib talks to the local launcher and reports its FPS
//every 10 seconds
void UpdateLinkToLauncher(void)
{
	static U32 iLastConnectTime;
	static U32 iNextSendFPSTime;
	static S64 gUtilitiesLibTicksLastTime;

	U32 iCurTime = timeSecondsSince2000();

	if (!ShardInfoStringWasSet())
	{
		return;
	}

	if (!gpLinkToLauncher)
	{
		gpLinkToLauncher = commConnect(commDefault(), LINKTYPE_SHARD_NONCRITICAL_500K, LINK_FORCE_FLUSH, "localhost", GetLauncherListenPort(), NULL, NULL, LauncherFpsLinkDisconnect, 0);
		iLastConnectTime = iCurTime;
		iNextSendFPSTime = iCurTime + SECS_BETWEEN_FPS_SENDS;
		gUtilitiesLibTicksLastTime = gUtilitiesLibTicks;

		linkSetDebugName(gpLinkToLauncher, "Link to launcher for FPS reporting");

		return;
	}



	//after 5 seconds, if we tried to connect but have not succeeded, start over
	if (!linkConnected(gpLinkToLauncher) || linkDisconnected(gpLinkToLauncher))
	{
		if (iCurTime > iLastConnectTime + 5)
		{
			log_printf(LOG_BUG, "Couldn't connect to launcher after 5 seconds... restarting attempt");
			linkRemove_wReason(&gpLinkToLauncher, "Timed out or disconnected in updateLinkToLauncher");
		}

		return;
	}


	while (eaSize(&sppChildProcsToReport))
	{
		ChildProcToReport *pProc = eaPop(&sppChildProcsToReport);
		Packet *pPack;

		pktCreateWithCachedTracker(pPack, gpLinkToLauncher, TO_LAUNCHER_PROCESS_REPORTING_CHILD_PROCESS);
		pktSendBits(pPack, 32, getpid());
		PutContainerTypeIntoPacket(pPack, GetAppGlobalType());
		PutContainerIDIntoPacket(pPack, GetAppGlobalID());
		pktSendBits(pPack, 32, gServerLibState.antiZombificationCookie);

		pktSendString(pPack, pProc->pProcName);
		pktSendBits(pPack, 32, pProc->iPid);

		pktSend(&pPack);

		StructDestroy(parse_ChildProcToReport, pProc);
	}

	if (iCurTime >= iNextSendFPSTime)
	{
		Packet *pPack;
		float fFPS;
		int iTimePassed;
		U32 iPid = getpid();

		PERFINFO_AUTO_START_FUNC();

		pktCreateWithCachedTracker(pPack, gpLinkToLauncher, TO_LAUNCHER_PROCESS_REPORTING_FPS);
		pktSendBits(pPack, 32, iPid);
		PutContainerTypeIntoPacket(pPack, GetAppGlobalType());
		PutContainerIDIntoPacket(pPack, GetAppGlobalID());
		pktSendBits(pPack, 32, gServerLibState.antiZombificationCookie);
		

		iTimePassed = iCurTime - iNextSendFPSTime + SECS_BETWEEN_FPS_SENDS;
		fFPS = ((float)(gUtilitiesLibTicks - gUtilitiesLibTicksLastTime)) /iTimePassed;

		pktSendF32(pPack, fFPS);
		pktSendBits(pPack, 32, giLongestFrameMsecs);
		pktSend(&pPack);

		iNextSendFPSTime = iCurTime + SECS_BETWEEN_FPS_SENDS;
		gUtilitiesLibTicksLastTime = gUtilitiesLibTicks;
		PERFINFO_AUTO_STOP();
	}
}

// Minimum interval of Out_of_segment_memory alerts, in seconds.
static int sCor15550AlertInterval = 60*10;
AUTO_CMD_INT(sCor15550AlertInterval, Cor15550AlertInterval) ACMD_CMDLINE;

// Check if MemTrack's workaround for COR-15550 has ever been applied.
void serverLibCor15550WorkaroundCheck()
{
#ifdef _M_X64
	if (!IsUsingVista())
	{
		extern volatile U64 memtrackCor15550WorkaroundCount;
		extern volatile size_t memtrackCor15550WorkaroundLast;
		static U64 last_memtrackCor15550WorkaroundCount = 0;
		static U64 lastlog = 0;
		static U64 lastwarn = 0;
		U64 count = memtrackCor15550WorkaroundCount;
		if (count != last_memtrackCor15550WorkaroundCount)
		{
			U32 now;
			U64 last_size = memtrackCor15550WorkaroundLast;
			last_memtrackCor15550WorkaroundCount = count;
			now = timeSecondsSince2000();
			if (now > lastlog)
			{
				lastlog = now;
				servLog(LOG_BUG, "Cor15550", "count %"FORM_LL"u last_size %"FORM_LL"u", count, last_size);
				if (now > lastwarn + sCor15550AlertInterval)
				{
					lastwarn = now;
					CRITICAL_NETOPS_ALERT("Out_of_segment_memory", "The operating system heap has refused our request for more memory %"FORM_LL"u time%s, most recently %"FORM_LL"u bytes."
						"  The server is attempting to apply the COR-15550 workaround, but it may crash soon.  (This alert is auto-suppressed for the next %d seconds.)",
						count, count > 1 ? "s" : "", last_size, sCor15550AlertInterval);
				}
			}
		}
	}
#endif // _M_X64
}

#define LOG_SERVERDATA_PRINT_DELAY 600

void serverLibOncePerFrame(void)
{
	static U32 siLastPrinted = 0;
	U32 iCurrentTime = timeSecondsSince2000();
	PERFINFO_AUTO_START_FUNC();

	coarseTimerAddInstance(NULL, "svrLogFlush");
	PERFINFO_AUTO_START("svrLogFlush", 1);
	svrLogFlush(0);
	PERFINFO_AUTO_STOP();
	coarseTimerStopInstance(NULL, "svrLogFlush");

	coarseTimerAddInstance(NULL, "UpdateControllerConnection");
	UpdateControllerConnection();
	coarseTimerStopInstance(NULL, "UpdateControllerConnection");

	coarseTimerAddInstance(NULL, "UpdateObjectTransactionManager");
	UpdateObjectTransactionManager();
	coarseTimerStopInstance(NULL, "UpdateObjectTransactionManager");

	coarseTimerAddInstance(NULL, "Misc Updates");
	UpdateDeferredServerLibDialogs();

	//this function is in utilitieslib, but few if any non-serverlib apps are going to do much fileserving
	GenericFileServing_Tick();
	coarseTimerStopInstance(NULL, "Misc Updates");

	if(iCurrentTime >= siLastPrinted + LOG_SERVERDATA_PRINT_DELAY && !sbNoServerLogPrintf)
	{
		static	char ipStr[64] = {0}; 
		static	int pid=0;
		const char *pExtraString;

		coarseTimerAddInstance(NULL, "LogServerPrint");
		PERFINFO_AUTO_START("LogServerPrint", 1);
		if(!ipStr[0] || ipStr[0] == '0')
			sprintf(ipStr, "%s", makeIpStr(getHostLocalIp()));

#if !PLATFORM_CONSOLE
		if (!pid) {
			pid = _getpid();
		}
#endif

		PERFINFO_AUTO_START("GetExtraInfo", 1);
		pExtraString = GetExtraInfoForLogPrintf();
		PERFINFO_AUTO_STOP();

		if(pExtraString)
		{
			servLog(LOG_SERVERDATA, "ServerData", "Ip %s P %d M %s", ipStr, pid, pExtraString);
		}
		else
		{
			servLog(LOG_SERVERDATA, "ServerData", "Ip %s P %d", ipStr, pid);
		}

		siLastPrinted = iCurrentTime;
		PERFINFO_AUTO_STOP();
		coarseTimerStopInstance(NULL, "LogServerPrint");
	}

	coarseTimerAddInstance(NULL, "UpdateLinkToMultiplexer");
	UpdateMultiplexerLinksForLogging();
	coarseTimerStopInstance(NULL, "UpdateLinkToMultiplexer");

	coarseTimerAddInstance(NULL, "UpdateLinkToLauncher");
	UpdateLinkToLauncher();
	coarseTimerStopInstance(NULL, "UpdateLinkToLauncher");

	if (gServerLibState.iGenericHttpServingPort)
	{
		coarseTimerAddInstance(NULL, "GenericHttpServing_Tick");
		GenericHttpServing_Tick();
		coarseTimerStopInstance(NULL, "GenericHttpServing_Tick");
	}

	serverLibCor15550WorkaroundCheck();

	PERFINFO_AUTO_STOP();

	// update the transaction manager here, once we add the db one
}

void svrExitEx(int returnVal, char *pFile, int iLine)
{
	if (loggingActive())
	{
		log_printf(LOG_CRASH, "SvrExit: %s(%d)", pFile, iLine);
		filelog_printf("Shutdown.log", "SvrExit: %s(%d)", pFile, iLine);

		svrLogSetSystemIsShuttingDown();

		logFlush();
		logWaitForQueueToEmpty();
	}

	commFlushAndCloseAllComms(10.0f);

	timeEndPeriod(1);
	if (gServerLibState.bProfile)
	{	
		timerRecordEnd();
		//stopProfile = 1;
		//Sleep(10000);
	}
	exit(returnVal);
}



static void ReportStateToController(char *pStateString)
{
	if (objLocalManager())
	{
		RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, pStateString);
	}
}

void slSetGSMReportsStateToController(void)
{
	GSM_SetStatesChangedCB(ReportStateToController);
}

//This peculiar-seeming function allows you to call non-remote commands remotely. It is used by the magical
//buttons that appear on monitoring windows in the MCP
AUTO_COMMAND_REMOTE;
void CallLocalCommandRemotely(char *pLocalCommandString)
{
	globCmdParse(pLocalCommandString);
}


void DoSlowReturn_RemoteCommand(ContainerID iMCPID, int iRequestID, int iClientID, CommandServingFlags eFlags, char *pMessageString, void *pUserData)
{
	RemoteCommand_ReturnHtmlStringFromRemoteCommand(GLOBALTYPE_CONTROLLER, 0, iClientID, iRequestID, 
		iMCPID, eFlags, pMessageString);
}


AUTO_COMMAND_REMOTE;
void CallLocalCommandRemotelyAndReturnVerboseHtmlString(int iClientID, int iCommandRequestID, U32 iMCPID, CommandServingFlags eFlags, char *pCommand, int iAccessLevel, char *pAuthNameAndIP)
{
	if (eFlags & CMDSRV_NORETURN)
	{
		if (!cmdParseForServerMonitor(eFlags, pCommand, iAccessLevel, NULL, 0, 0, 0, 0, 0, pAuthNameAndIP, NULL))
		{
			RemoteCommand_ReturnHtmlStringFromRemoteCommand(GLOBALTYPE_CONTROLLER, 0, iClientID, iCommandRequestID, iMCPID, eFlags, "Execution failed... talk to a programmer. Most likely the AST_COMMAND was set up wrong");
		}
		else
		{
			RemoteCommand_ReturnHtmlStringFromRemoteCommand(GLOBALTYPE_CONTROLLER, 0, iClientID, iCommandRequestID, iMCPID, eFlags, SERVERMON_CMD_RESULT_HIDDEN);
		}
	}
	else
	{
		char *pRetString = NULL;
		bool bSlowReturn = false;


#ifndef _XBOX
		if (strStartsWith(pCommand, "<?xml"))
		{
			//This code is duplicated in Controller_net.c and RemoteXMLRPC.c
			//any updates must update all 3 places.

			XMLParseInfo *info = XMLRPC_Parse(pCommand, "RemoteClient");
			XMLMethodResponse *response = NULL;
			CmdSlowReturnForServerMonitorInfo slowReturnInfo = {0};
			estrCreate(&pRetString);
			if (info)
			{
				XMLMethodCall *method = NULL;
				if (method = XMLRPC_GetMethodCall(info))
				{
					slowReturnInfo.iClientID = iClientID;
					slowReturnInfo.iCommandRequestID = iCommandRequestID;
					slowReturnInfo.iMCPID = iMCPID;
					slowReturnInfo.pSlowReturnCB = DoSlowReturn_RemoteCommand;
					response = XMLRPC_ConvertAndExecuteCommand(method, iAccessLevel,
						//Everything since we don't know the actual origin
						NULL,
						pAuthNameAndIP,
						&slowReturnInfo);

/* ABW removing this 4/19/2013, pretty sure that it's just leaking memory at this point, not sure why it was
ever there.*/
					if (slowReturnInfo.bDontDestroyXMLMethodCall)
					{	//We're going to pass the method call to the destination server so don't destroy it.
						info->state->methodCall = NULL;
					}
				}
				else
				{
					estrPrintf(&pRetString, "XMLRPC Request contained an error: %s", info->error);
				}
				StructDestroy(parse_XMLParseInfo, info);
			}
			else
			{
				estrPrintf(&pRetString, "Error generating XMLRPC request.");
			}
			if (!response)
			{
				response = XMLRPC_BuildMethodResponse(NULL, XMLRPC_FAULT_BADPARAMS, pRetString);
				estrClear(&pRetString);
			}
			if (!(bSlowReturn = slowReturnInfo.bDoingSlowReturn))
			{
				XMLRPC_WriteOutMethodResponse(response, &pRetString);
			}
			StructDestroy(parse_XMLMethodResponse, response);
		}
		else
#endif
		{
			estrStackCreate(&pRetString);
			cmdParseForServerMonitor(eFlags, pCommand, iAccessLevel, &pRetString, iClientID, iCommandRequestID, iMCPID, DoSlowReturn_RemoteCommand, NULL, pAuthNameAndIP, &bSlowReturn);
		}

		if (!bSlowReturn)
		{
			RemoteCommand_ReturnHtmlStringFromRemoteCommand(GLOBALTYPE_CONTROLLER, 0, iClientID, iCommandRequestID, iMCPID, eFlags, pRetString);
		}

		estrDestroy(&pRetString);
	}
}


//controller scripting support
void OVERRIDE_LATELINK_ControllerScript_Succeeded(void)
{
	if (objLocalManager())
	{
		RemoteCommand_SetControllerScriptingOutcome(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, 1, "");
	}
	else
	{
		Errorf("someone wants to call RemoteCommand_SetControllerScriptingOutcome with no objLocalManager()");
	}
}
void OVERRIDE_LATELINK_ControllerScript_Failed(char *pFailureString)
{
	if (objLocalManager())
	{
		RemoteCommand_SetControllerScriptingOutcome(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, -1, pFailureString);
	}
	else
	{
		Errorf("someone wants to call RemoteCommand_SetControllerScriptingOutcome with no objLocalManager()");
	}
}


//returns "" if the command fails
AUTO_COMMAND_REMOTE;
void RunLocalCommandAndReturnStringToControllerScripting(char *pLocalCommand)
{
	char *pRetString = NULL;
	estrStackCreate(&pRetString);

	if (cmdParseAndReturn(pLocalCommand, &pRetString, CMD_CONTEXT_HOWCALLED_CONTROLLER_SCRIPTING))
	{
		RemoteCommand_SetControllerScriptingOutcome(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, 1, pRetString);
	}
	else
	{
		RemoteCommand_SetControllerScriptingOutcome(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, -1, "");
	}
	
	estrDestroy(&pRetString);
}


void ServerLib_GetXpathForHttp_CB(U32 iReqID1, U32 iReqID2, StructInfoForHttpXpath *pStructInfo)
{
	RemoteCommand_ReturnXPathForHttp(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), iReqID1, iReqID2, 
		pStructInfo);
}

AUTO_COMMAND_REMOTE;
void ServerLib_GetXpathForHttp(U32 iReqID1, U32 iReqID2, UrlArgumentList *pURL, int iAccessLevel, GetHttpFlags eFlags)
{

	GetStructForHttpXpath(pURL, iAccessLevel, iReqID1, iReqID2, &ServerLib_GetXpathForHttp_CB, eFlags);
}

AUTO_COMMAND_REMOTE;
void RemoteObjPrint(const char *pString, CmdContext *context)
{
	if (context && pString)
	{
		objPrintf(context->clientType, context->clientID, "%s", pString);
	}
}

AUTO_COMMAND_REMOTE;
void RemoteObjBroadcastMessage(const char *pTitle, const char *pString, CmdContext *context)
{
	if (context && pString)
	{
		objBroadcastMessage(context->clientType, context->clientID, pTitle, pString);
	}
}

AUTO_COMMAND_REMOTE;
void RemoteObjBroadcastMessageEx(const char *pTitle, MessageStruct *pFmt, CmdContext *context)
{
	if (context && pFmt)
	{
		objBroadcastMessageEx(context->clientType, context->clientID, pTitle, pFmt);
	}
}

AUTO_COMMAND_REMOTE;
ContainerRef *ContainerGetOwner(CmdContext *context)
{	
	ContainerRef *newRef = StructCreate(parse_ContainerRef);
	if (context)
	{		
		Container *pObject = objGetContainerEx(context->clientType, context->clientID, false, false, true);

		if(pObject)
		{
			if(IsContainerOwnedByMe(pObject))
			{
				newRef->containerType = objServerType();
				newRef->containerID = objServerID();
			}
			objUnlockContainer(&pObject);
		}
	}
	return newRef;
}

U32 OVERRIDE_LATELINK_GetAppGlobalID(void)
{
	return gServerLibState.containerID;
}

void* OVERRIDE_LATELINK_GetAppIDStr(void)
{
	static ContainerID containerID = 0;
	static char buf[50] = {0};
	if(!buf[0] || containerID != gServerLibState.containerID)
	{
		sprintf(buf, "%d", gServerLibState.containerID);
		containerID = gServerLibState.containerID;
	}

	return buf;
}


char *OVERRIDE_LATELINK_GetControllerHost(void)
{
	return gServerLibState.controllerHost;
}


U32 OVERRIDE_LATELINK_GetAntiZombificationCookie(void)
{
	return gServerLibState.antiZombificationCookie;
}


void OVERRIDE_LATELINK_IncAntiZombificationCoookie(void)
{
	gServerLibState.antiZombificationCookie++;
}



void ServerLibSetControllerHost (const char *host)
{
	strcpy(gServerLibState.controllerHost, host);
}

// Container subscription

static PERFINFO_TYPE *sAddNewSubscribedContainerCopyPerfInfos[GLOBALTYPE_MAXTYPES] = {0};


static void ReceiveContainerCopy(Packet *pak)
{
	GlobalType conType = pktGetBits64(pak, 64);
	ContainerID conID = pktGetBits64(pak, 64); 
	GlobalType ownerType = pktGetBits64(pak, 64);
	ContainerID ownerID = pktGetBits64(pak, 64);
	DictionaryHandle hDict;

	PERFINFO_AUTO_START_STATIC(GlobalTypeToName(conType), &sAddNewSubscribedContainerCopyPerfInfos[conType], 1);

	hDict = RefSystem_GetDictionaryHandleFromNameOrHandle(GlobalTypeToCopyDictionaryName(conType));
	if (hDict)
	{
		char locString[512];
		void *pObject;
		ParseTable *pTable = RefSystem_GetDictionaryParseTable(hDict);
		ResourceDictionaryInfo *pDictInfo = resDictGetInfo(hDict);
		ResourceInfo *pInfo;
		ResourceDictionary *pResDict = NULL;
		ResourceStatus *pResStatus = NULL;
		bool bAdded = true;
		ContainerSchema *schema = objFindContainerSchema(conType);
		char idBuf[128];

		VERBOSE_LOG_DICT(hDict, "Receiving container %u", conID);

		if (!schema || !objForceLoadSchemaFilesFromDisk(schema))
		{
			PERFINFO_AUTO_STOP();
			return;
		}

		ContainerIDToString(conID, idBuf);

		if (pObject = RefSystem_ReferentFromString(hDict, idBuf))
		{
			VERBOSE_LOG_DICT_WITH_STRUCT(hDict, pTable, pObject, "It previously existed");
			bAdded = false;

			resNotifyObjectPreModified(hDict, idBuf);
			StructResetVoid(pTable, pObject);
		}
		else
		{
			VERBOSE_LOG_DICT(hDict, "It's brand new");
			bAdded = true;
			pObject = StructCreateWithComment(pTable, "Created in ReceiveContainerCopy");
		}

		if (!pObject)
		{
			VERBOSE_LOG_DICT(hDict, "Failed, can't create");
			objLog(LOG_CONTAINER, conType, conID, 0, NULL, NULL, NULL, "AddSubscribed", NULL, "Failed, can't create");
			PERFINFO_AUTO_STOP();
			return;
		}

		assertmsgf(schema->bIsNativeSchema, "Trying to use ParserRecv2tpis with a non-native schema. This won't work. You must call objRegisterNativeSchema");

		if (!ParserRecv2tpis(pak, schema->loadedParseTable, schema->classParse, pObject)) 
		{
			VERBOSE_LOG_DICT(hDict, "Recv2tpis failed");
			objLog(LOG_CONTAINER, conType, conID, 0, NULL, NULL, NULL, "AddSubscribed", NULL, "Failed, recv2tpis failed");
			if (bAdded)
				StructDestroyVoid(pTable, pObject);
			PERFINFO_AUTO_STOP();
			return;
		}

		VERBOSE_LOG_DICT_WITH_STRUCT(hDict, pTable, pObject, "Received it successfully");


		pResDict = resGetDictionary(hDict);
		pResStatus = resGetStatus(pResDict, idBuf);
		if (pResDict->bShouldRequestMissingData && (!pResStatus || !pResStatus->bResourceRequested))
		{
			VERBOSE_LOG_DICT(hDict, "We did not want this update");
			// Ignore update because we didn't ask for it
			if (bAdded)
				StructDestroyVoid(pTable, pObject);
			PERFINFO_AUTO_STOP();
			return;
		}

		if (bAdded)
		{
			RefSystem_AddReferent(	GlobalTypeToCopyDictionaryName(conType), 
									idBuf,
									pObject);
		}
		else
		{
			resNotifyObjectModified(hDict, idBuf);
		}

		pInfo = resGetOrCreateInfo(pDictInfo, idBuf);
		pInfo->resourceID = conID;
		sprintf(locString, "%s[%d]", GlobalTypeToName(ownerType), ownerID);
		pInfo->resourceLocation = allocAddString(locString);	

		if(printContainerDiffStrings){
			static char* s;
			
			estrClear(&s);
			ParserWriteText(&s,
							schema->classParse,
							pObject,
							0,
							0,
							0);
							
			printfColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN,
						"vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv\n"
						"Container received: %s[%d] %s\n"
						"%s"
						"^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n"
						"\n",
						GlobalTypeToName(conType),
						conID,
						locString,
						s);
		}

		if (!pResStatus)
		{
			pResStatus = resGetOrCreateStatus(pResDict, idBuf);
		}
		pResStatus->bResourceManaged = true;
	}
	else
	{		
		AssertOrAlert("NO_DICT_FOR_SUBSCRIPTION", "A container of type %s was received for subscription, but there's no dictionary for it. Did you forget to register the dictionary?",
			GlobalTypeToName(conType));
		objLog(LOG_CONTAINER, conType, conID, 0, NULL, NULL, NULL, "AddSubscribed", NULL, "Failed, bad dict");
	}

	PERFINFO_AUTO_STOP();
}

static PERFINFO_TYPE *sHandleSubscribedContainerCopyChangePerfInfos[GLOBALTYPE_MAXTYPES] = {0};


AUTO_COMMAND_REMOTE ACMD_MULTIPLE_RECIPIENTS;
void HandleSubscribedContainerCopyDestroy(GlobalType conType, ContainerID conID)
{
	DictionaryHandle hDict;
	PERFINFO_AUTO_START_STATIC(GlobalTypeToName(conType), &sHandleSubscribedContainerCopyChangePerfInfos[conType], 1);

	hDict = RefSystem_GetDictionaryHandleFromNameOrHandle(GlobalTypeToCopyDictionaryName(conType));
	if (hDict)
	{
		char idBuf[128];
		void *pObject;
		ParseTable *pTable = RefSystem_GetDictionaryParseTable(hDict);

		ResourceDictionary *pResDict = NULL;
		ResourceStatus *pResStatus = NULL;

		ContainerIDToString(conID, idBuf);

		pResDict = resGetDictionary(hDict);
		pResStatus = resGetStatus(pResDict, idBuf);

		if (pResDict->bShouldRequestMissingData && (!pResStatus || !pResStatus->bResourceRequested))
		{
			// Ignore update because we didn't ask for it
			PERFINFO_AUTO_STOP();
			return;
		}

		if (pObject = RefSystem_ReferentFromString(hDict, idBuf))
		{
			if(!RefSystem_RemoveReferentWithReason(pObject, false, "Container destroyed on ObjectDB"))
			{
				objLog(LOG_CONTAINER, conType, conID, 0, NULL, NULL, NULL, "DestroySubscribed", NULL, "Failed, doesn't exist");
			}
		}
		else
		{
			objLog(LOG_CONTAINER, conType, conID, 0, NULL, NULL, NULL, "DestroySubscribed", NULL, "Failed, can't find");
		}
	}
	else
	{
		objLog(LOG_CONTAINER, conType, conID, 0, NULL, NULL, NULL, "DestroySubscribed", NULL, "Failed, bad dict");
	}

	PERFINFO_AUTO_STOP();
}


AUTO_COMMAND_REMOTE ACMD_MULTIPLE_RECIPIENTS;
void HandleSubscribedContainerCopyChange(GlobalType conType, ContainerID conID, char *diffString)
{
	DictionaryHandle hDict;
	const char *pcConType = GlobalTypeToName(conType);

	PERFINFO_AUTO_START_STATIC(pcConType, &sHandleSubscribedContainerCopyChangePerfInfos[conType], 1);

	coarseTimerAddInstanceWithTrivia(NULL, "HandleSubscribedContainerCopyChange", pcConType);

	if(printContainerDiffStrings){
		printfColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN,
					"vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv\n"
					"Container changed: %s[%d]\n"
					"%s%s"
					"^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n"
					"\n",
					GlobalTypeToName(conType),
					conID,
					diffString,
					strEndsWith(diffString, "\n") ? "" : "\n");
	}

	hDict = RefSystem_GetDictionaryHandleFromNameOrHandle(GlobalTypeToCopyDictionaryName(conType));
	if (hDict)
	{
		char idBuf[128];
		void *pObject;
		ParseTable *pTable = RefSystem_GetDictionaryParseTable(hDict);

		ResourceDictionary *pResDict = NULL;
		ResourceStatus *pResStatus = NULL;

		VERBOSE_LOG_DICT(hDict, "For container ID %u, got diff string %s", conID, diffString);

		ContainerIDToString(conID, idBuf);

		pResDict = resGetDictionary(hDict);
		pResStatus = resGetStatus(pResDict, idBuf);

		if (pResDict->bShouldRequestMissingData && (!pResStatus || !pResStatus->bResourceRequested))
		{
			VERBOSE_LOG_DICT(hDict, "Which we didn't ask for");

			// Ignore update because we didn't ask for it
			PERFINFO_AUTO_STOP();
			coarseTimerStopInstance(NULL, "HandleSubscribedContainerCopyChange");
			return;
		}

		if (pObject = RefSystem_ReferentFromString(hDict, idBuf))
		{
			VERBOSE_LOG_DICT(hDict, "The object exists");

			resNotifyObjectPreModified(hDict, idBuf);
			if (!objPathParseAndApplyOperations(pTable, pObject, diffString))
			{
				VERBOSE_LOG_DICT(hDict, "ParseAndApply failed");

				objLog(LOG_CONTAINER, conType, conID, 0, NULL, NULL, NULL, "ModifySubscribed", NULL, "Failed, bad diff");
			}
			else
			{
				VERBOSE_LOG_DICT_WITH_STRUCT(hDict, pTable, pObject, "ParseAndApply succeeded");

				resNotifyObjectModified(hDict, idBuf);
			}
		}
		else
		{		
			VERBOSE_LOG_DICT(hDict, "Object didn't exist");

			objLog(LOG_CONTAINER, conType, conID, 0, NULL, NULL, NULL, "ModifySubscribed", NULL, "Failed, can't find");
		}
	}
	else
	{	
		objLog(LOG_CONTAINER, conType, conID, 0, NULL, NULL, NULL, "ModifySubscribed", NULL, "Failed, bad dict");
	}
			


	coarseTimerStopInstance(NULL, "HandleSubscribedContainerCopyChange");
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND_REMOTE ACMD_MULTIPLE_RECIPIENTS;
void HandleSubscribedContainerCopyOwnerChange(GlobalType conType, ContainerID conID, GlobalType ownerType, ContainerID ownerID)
{
	DictionaryHandle hDict = RefSystem_GetDictionaryHandleFromNameOrHandle(GlobalTypeToCopyDictionaryName(conType));
	if (hDict)
	{
		ResourceDictionaryInfo *pDictInfo = resDictGetInfo(hDict);
		ResourceInfo *pInfo;
		char idBuf[128];

		pInfo = resGetInfo(pDictInfo, ContainerIDToString(conID, idBuf));
		if (pInfo)
		{		
			char locString[512];
			sprintf(locString, "%s[%d]", GlobalTypeToName(ownerType), ownerID);
			pInfo->resourceLocation = allocAddString(locString);	
		}

		if(printContainerDiffStrings){
			printfColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN,
						"vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv\n"
						"Container owner change: %s[%d]: %s[%d]\n"
						"^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n"
						"\n",
						GlobalTypeToName(conType),
						conID,
						GlobalTypeToName(ownerType),
						ownerID);
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_MULTIPLE_RECIPIENTS;
void HandleRemoveSubscribedContainer(GlobalType conType, ContainerID conID)
{
	DictionaryHandle hDict = RefSystem_GetDictionaryHandleFromNameOrHandle(GlobalTypeToCopyDictionaryName(conType));
	if (hDict)
	{
		ResourceDictionaryInfo *pDictInfo = resDictGetInfo(hDict);
		ParseTable *pTable = RefSystem_GetDictionaryParseTable(hDict);
		char idBuf[128];
		void *pObject;
		
		ContainerIDToString(conID, idBuf);
		pObject = RefSystem_ReferentFromString(hDict, idBuf);

		if(printContainerDiffStrings){
			printfColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN,
						"vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv\n"
						"Container removed: %s[%d]\n"
						"^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n"
						"\n",
						GlobalTypeToName(conType),
						conID);
		}

		if (pObject)
		{
			RefSystem_RemoveReferent(pObject, false);
			resRemoveInfo(pDictInfo, idBuf);
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_MULTIPLE_RECIPIENTS;
void HandleNonExistentContainerNotification(GlobalType conType, ContainerID conID)
{
	DictionaryHandle hDict;
	PERFINFO_AUTO_START_STATIC(GlobalTypeToName(conType), &sHandleSubscribedContainerCopyChangePerfInfos[conType], 1);

	hDict = RefSystem_GetDictionaryHandleFromNameOrHandle(GlobalTypeToCopyDictionaryName(conType));
	if (hDict)
	{
		char idBuf[128];
		void *pObject;
		ParseTable *pTable = RefSystem_GetDictionaryParseTable(hDict);

		ResourceDictionary *pResDict = NULL;
		ResourceStatus *pResStatus = NULL;

		ContainerIDToString(conID, idBuf);

		pResDict = resGetDictionary(hDict);
		pResStatus = resGetStatus(pResDict, idBuf);

		if (pResDict->bShouldRequestMissingData && (!pResStatus || !pResStatus->bResourceRequested))
		{
			// Ignore update because we didn't ask for it
			PERFINFO_AUTO_STOP();
			return;
		}

		if (pObject = RefSystem_ReferentFromString(hDict, idBuf))
		{
			if(!RefSystem_RemoveReferentWithReason(pObject, false, "Container destroyed on ObjectDB"))
			{
				objLog(LOG_CONTAINER, conType, conID, 0, NULL, NULL, NULL, "DestroySubscribed", NULL, "Failed, doesn't exist");
			}
		}
		else
		{
			RefSystem_MarkSetBySource(hDict, idBuf);
		}
	}
	else
	{
		objLog(LOG_CONTAINER, conType, conID, 0, NULL, NULL, NULL, "DestroySubscribed", NULL, "Failed, bad dict");
	}

	PERFINFO_AUTO_STOP();
}

static void objSubscribeToContainer(GlobalType conType, ContainerID conID, const char* reason)
{
	RemoteCommand_RemoteSubscribeToContainer(GLOBALTYPE_OBJECTDB, 0, objServerType(), objServerID(), conType, conID, reason);
}

static void objUnsubscribeFromContainer(GlobalType conType, ContainerID conID, const char* reason)
{
	RemoteCommand_RemoteUnsubscribeFromContainer(GLOBALTYPE_OBJECTDB, 0, objServerType(), objServerID(), conType, conID, reason);
}

void objSubscribeToOnlineContainers(GlobalType conType)
{
	RemoteCommand_RemoteSubscribeToOnlineContainers(GLOBALTYPE_OBJECTDB, 0, objServerType(), objServerID(), conType);
}

void objUnsubscribeFromOnlineContainers(GlobalType conType)
{
	RemoteCommand_RemoteUnsubscribeFromOnlineContainers(GLOBALTYPE_OBJECTDB, 0, objServerType(), objServerID(), conType);	
}

ContainerRef objGetSubscribedContainerLocation(GlobalType conType, ContainerID conID)
{
	ContainerRef returnRef = {0};
	char idBuf[128];
	ResourceInfo *pInfo = resGetInfo(GlobalTypeToCopyDictionaryName(conType), ContainerIDToString(conID, idBuf));
	if (pInfo)
	{
		return ParseGlobalTypeAndIDIntoContainerRef(pInfo->resourceLocation);
	}
	return returnRef;
}

static int sLogSubscriptionRequests = 0;
AUTO_CMD_INT(sLogSubscriptionRequests, LogSubscriptionRequests) ACMD_CMDLINE;

void objCopyDictHandleRequest(DictionaryHandleOrName dictHandle, int command, const char *pResourceName, void * pResource, const char* reason)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);

	if (!pDictionary || !pDictionary->bShouldRequestMissingData)
	{
		return;
	}

	if (command == RESREQUEST_GET_RESOURCE || command == RESREQUEST_OPEN_RESOURCE)
	{	
		GlobalType type = CopyDictionaryNameToGlobalType(pDictionary->pDictName);
		ContainerID id = StringToContainerID(pResourceName);

		if (sLogSubscriptionRequests)
			objLog(LOG_CONTAINER, type, id, 0, NULL, NULL, NULL, "RequestSubscription", NULL, "Requested");

		objSubscribeToContainer(type, id, reason);
	}
	if (command == RESREQUEST_CANCEL_REQUEST)
	{
		GlobalType type = CopyDictionaryNameToGlobalType(pDictionary->pDictName);
		ContainerID id = StringToContainerID(pResourceName);

		if (sLogSubscriptionRequests)
			objLog(LOG_CONTAINER, type, id, 0, NULL, NULL, NULL, "CancelSubscription", NULL, "Cancelled");

		objUnsubscribeFromContainer(type, id, reason);
	}
}

// Commands are here because of linking issues


void DEFAULT_LATELINK_dbSubscribeToContainer(GlobalType ownerType, ContainerID ownerID, GlobalType conType, ContainerID conID, const char* reason)
{
}

void DEFAULT_LATELINK_dbUnsubscribeFromContainer(GlobalType ownerType, ContainerID ownerID, GlobalType conType, ContainerID conID, const char* reason)
{
}

void DEFAULT_LATELINK_dbSubscribeToOnlineContainers(GlobalType ownerType, ContainerID ownerID, GlobalType conType)
{
}

void DEFAULT_LATELINK_dbUnsubscribeFromOnlineContainers(GlobalType ownerType, ContainerID ownerID, GlobalType conType)
{
}

AUTO_COMMAND_REMOTE;
void RemoteSubscribeToContainer(GlobalType ownerType, ContainerID ownerID, GlobalType conType, ContainerID conID, const char* reason)
{
	dbSubscribeToContainer(ownerType, ownerID, conType, conID, reason);
}

AUTO_COMMAND_REMOTE;
void RemoteUnsubscribeFromContainer(GlobalType ownerType, ContainerID ownerID, GlobalType conType, ContainerID conID, const char* reason)
{
	dbUnsubscribeFromContainer(ownerType, ownerID, conType, conID, reason);
}


AUTO_COMMAND_REMOTE;
void RemoteSubscribeToOnlineContainers(GlobalType ownerType, ContainerID ownerID, GlobalType conType)
{
	dbSubscribeToOnlineContainers(ownerType, ownerID, conType);
}

AUTO_COMMAND_REMOTE;
void RemoteUnsubscribeFromOnlineContainers(GlobalType ownerType, ContainerID ownerID, GlobalType conType)
{
	dbUnsubscribeFromOnlineContainers(ownerType, ownerID, conType);
}

void SendKeepAliveToController(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	PERFINFO_AUTO_START_FUNC();
	RemoteCommand_KeepAliveReturn(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID);
	PERFINFO_AUTO_STOP();
}


AUTO_COMMAND_REMOTE;
void BeginSendingKeepAliveToController(int iSeconds)
{
	assert(iSeconds);
	TimedCallback_Add(SendKeepAliveToController, NULL, iSeconds);
}

void DEFAULT_LATELINK_SendDebugTransferMessageToClient(U32 iCookie, char *pMessage)
{

}


AUTO_COMMAND_REMOTE;
void SendDebugTransferMessage(U32 iCookie, char *pMessage)
{
	SendDebugTransferMessageToClient(iCookie, pMessage);
}



typedef struct TimeMungingStruct
{
	union timeAndPointers
	{
		U64 iTime;
		struct pointers
		{
			void *p1;
			void *p2;
		} pointers;
	} timeAndPointers;
} TimeMungingStruct;



void PingTransServerCB(void *pUserData1, void *pUserData2)
{
	TimeMungingStruct tStruct = {0};
	tStruct.timeAndPointers.pointers.p1 = pUserData1;
	tStruct.timeAndPointers.pointers.p2 = pUserData2;

	printf("Ping time to trans server and back: %"FORM_LL"d milliseconds\n", timeMsecsSince2000() - tStruct.timeAndPointers.iTime);
}

AUTO_COMMAND ACMD_CATEGORY(debug);
void PingTransactionServer(void)
{
	Packet *pPack;
	
	TimeMungingStruct tStruct = {0};

	if (!objLocalManager())
	{
		printf("No local trans manager\n");
	}
	tStruct.timeAndPointers.iTime = timeMsecsSince2000();

	//sending to globaltype_machine, since that will never work
	pPack = GetPacketToSendThroughTransactionServer(objLocalManager(), NULL, GLOBALTYPE_MACHINE, 0, 0, "ping", PingTransServerCB, tStruct.timeAndPointers.pointers.p1, tStruct.timeAndPointers.pointers.p2);

	pktSend(&pPack);
}

void DEFAULT_LATELINK_DoYouOwnContainerReturn_Internal(int iRequestID, bool bIsTrans, bool bIOwn)
{


}

//really "should" be on the object DB, but that would require serverlib to depend on object db, which we might as well avoid if possible
AUTO_COMMAND_REMOTE;
void DoYouOwnContainerReturn(int iRequestID, bool bIsTrans, bool bIOwn)
{
	DoYouOwnContainerReturn_Internal(iRequestID, bIsTrans, bIOwn);
}


//used by DebugCheckContainerLoc in dbRemoteCommands.c
AUTO_COMMAND_REMOTE;
void DebugCheckContainer_DoYouOwnContainer(GlobalType eRequesterType, ContainerID iRequesterID, GlobalType eType, ContainerID iID, int iRequestID, bool bIsForTransServer)
{
	bool bIOwn = false;

	Container *con;
	con = objGetContainerEx(eType, iID, false, false, true);


	if (con)
	{
		if (con->meta.containerOwnerID == GetAppGlobalID() && con->meta.containerOwnerType == GetAppGlobalType())
		{
			bIOwn = true;
		}
		objUnlockContainer(&con);
	}

	RemoteCommand_DoYouOwnContainerReturn(eRequesterType, iRequesterID, iRequestID, bIsForTransServer, bIOwn);
}

AUTO_COMMAND_REMOTE_SLOW(int);
void SlowCmdNeverReturns(int x, int y, SlowRemoteCommandID id)
{
}

static void SlowCmdNeverReturns_ReturnCB(SA_PARAM_NN_VALID TransactionReturnVal *pReturn, void *pUserData)
{
}

void StartGeneratingNeverReturnTransactions_CB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	int iCount = (intptr_t)(userData);
	int i;

	for (i=0; i < iCount; i++)
	{
		RemoteCommand_SlowCmdNeverReturns(objCreateManagedReturnVal(SlowCmdNeverReturns_ReturnCB, NULL), GLOBALTYPE_OBJECTDB, 0, 3, 5);
	}

}



AUTO_COMMAND ACMD_CATEGORY(test);
void StartGeneratingNeverReturnTransactions(int iCount)
{
	TimedCallback_Add(StartGeneratingNeverReturnTransactions_CB, (void*)((intptr_t)iCount), 1.0f);
}

static void serverErrorAlertCallback(ErrorMessage *errMsg, void *userdata, enumAlertLevel eLevel, enumAlertCategory eCategory)
{
	char *msg = NULL;
	char *key = NULL;
	int i;
	bool underline = true;
	char *errString = errorFormatErrorMessage(errMsg);

	// Construct the error message
	estrStackCreate(&msg);
	estrPrintf(&msg, "Error: %s\n", errString);
	estrConcatf(&msg, "Location: %s:%i, count %d\n", errMsg->file, errMsg->line, errMsg->errorCount);

	// Print the error to the console
	consolePushColor();
	consoleSetFGColor(COLOR_RED|COLOR_BRIGHT);
	printf("\n\n%s\n\n", msg);
	consolePopColor();

	// Create alert key.
	// The goal here is to get something short and quickly-identifiable by a human, when viewed on mobile phone SMS,
	// yet long enough to have a reasonable chance of being uniquely-descriptive so that different errors are likely
	// to fall into separate alert key buckets.
	estrStackCreate(&key);
	estrCopy2(&key, "Err_");
	for (i = 0; errMsg && errMsg->estrMsg && errMsg->estrMsg[i] && estrLength(&key) < 20; ++i)
	{
		char c = errMsg->estrMsg[i];
		static const char devassert_string[] = "devassert";

		// Replace "devassert" with "Dev" to keep alert keys short.
		if (strStartsWith(errMsg->estrMsg + i, devassert_string))
		{
			i += sizeof(devassert_string) - 1 - 1;
			estrAppend2(&key, "Dev");
			underline = false;
		}

		// Append Alphanumeric ASCII characters as-is.
		else if (isascii(c) && isalnum(c))
		{
			estrConcatChar(&key, c);
			underline = false;
		}

		// All other strings of characters are replaced by a single "_".
		else if (!underline)
		{
			estrConcatChar(&key, '_');
			underline = true;
		}
	}

	// Send alert to NetOps
	TriggerAlert(key, msg, eLevel, eCategory, 0, GetAppGlobalType(), GetAppGlobalID(), GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0);

	estrDestroy(&msg);
	estrDestroy(&key);

	// Log the error.
	log_printf(LOG_BUG, "(Server Error Msg):\"%s\" in %s:%d", errString, NULL_TO_EMPTY(errMsg->file), errMsg->line);	
}

// Server error handler that generates critical alerts
void serverErrorAlertCallbackCritical(ErrorMessage *errMsg, void *userdata)
{
	serverErrorAlertCallback(errMsg, userdata, ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS);
}

// Server error handler that generates warning alerts
void serverErrorAlertCallbackWarning(ErrorMessage *errMsg, void *userdata)
{
	serverErrorAlertCallback(errMsg, userdata, ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS);
}

// Server error handler that generates programmer alerts
void serverErrorAlertCallbackProgrammer(ErrorMessage *errMsg, void *userdata)
{
	serverErrorAlertCallback(errMsg, userdata, ALERTLEVEL_WARNING, ALERTCATEGORY_PROGRAMMER);
}

void DEFAULT_LATELINK_GoAheadAndDie_ServerSpecific(void)
{
	if (GSM_IsRunning())
	{
		GSM_Quit("Got goAheadAndDie back from controller");
	}
	else
	{
		exit(0);
	}
}

AUTO_COMMAND_REMOTE;
void GoAheadAndDie(void)
{
	GoAheadAndDie_ServerSpecific();


}

//useful for testing RemoteCommandGroups and other debugging purposes
AUTO_COMMAND_REMOTE;
int PrintWithReturn(ACMD_SENTENCE pStr)
{
	printf("%s", pStr);
	return 1;
}

AUTO_COMMAND;
void PrintOnRandomGS(ACMD_SENTENCE pStr)
{
	RemoteCommand_PrintWithReturn(NULL, GLOBALTYPE_GAMESERVER, SPECIAL_CONTAINERID_RANDOM, pStr);
}
/*
AUTO_COMMAND;
void interShardTest(void)
{
	RemoteCommand_SendCommandToOtherShard(GLOBALTYPE_CONTROLLER, 0, ipFromString("awerner"), GLOBALTYPE_MAPMANAGER, 0, "assertnow", "foo");
}*/

static sbControllerReportsShardLocked = false;

AUTO_CMD_INT(sbControllerReportsShardLocked, ControllerReportsShardLocked) ACMD_COMMANDLINE;

AUTO_COMMAND_REMOTE;
void ShardIsLocked(bool bLocked)
{
	sbControllerReportsShardLocked = bLocked;
}

bool ControllerReportsShardLocked(void)
{
	return sbControllerReportsShardLocked;
}


static sbControllerReportsAllowShardVersionMismatch = false;

AUTO_CMD_INT(sbControllerReportsAllowShardVersionMismatch, ControllerReportsAllowShardVersionMismatch) ACMD_COMMANDLINE;


AUTO_COMMAND;
void SetControllerReportsAllowShardVersionMismatch(bool bLocked)
{
	sbControllerReportsAllowShardVersionMismatch = bLocked;
}



bool ControllerReportsAllowShardVersionMismatch(void)
{
	return sbControllerReportsAllowShardVersionMismatch;
}

static int siNumGSMachines = -1;

AUTO_COMMAND;
void SetNumGSMachines(int iNum)
{
	siNumGSMachines = iNum;
}

int DEFAULT_LATELINK_ServerLib_GetNumGSMachines(void)
{
	if (siNumGSMachines == -1)
	{
		siNumGSMachines = 1;
		AssertOrAlert("CANT_QUERY_NUM_GS_MACHINES", "Someone is asking for the number of GS machines, but was never informed by the controller. Set InformAboutNumGameServerMachines in ControllerServerSetup.txt");
	}

	return siNumGSMachines;
}

bool ServerLib_GetIntFromStringPossiblyWithPerGsSuffix(char *pStr, U32 *piOutVal, bool *pbOutHadGSSuffix)
{
	U32 iRetVal;

	if (StringToUint_Paranoid(pStr, &iRetVal))
	{
		if (pbOutHadGSSuffix)
		{
			*pbOutHadGSSuffix = false;
		}

		if (piOutVal)
		{
			*piOutVal = iRetVal;
		}

		return true;
	}

	if (strEndsWith(pStr, PER_GS_MACHINE_SUFFIX))
	{
		char *pTemp = NULL;
		U32 iTemp;

		estrCopy2(&pTemp, pStr);
		estrReplaceOccurrences(&pTemp, PER_GS_MACHINE_SUFFIX, "");

		if (StringToUint_Paranoid(pTemp, &iTemp))
		{
			if (pbOutHadGSSuffix)
			{
				*pbOutHadGSSuffix = true;
			}

			if (piOutVal)
			{
				*piOutVal = iTemp * ServerLib_GetNumGSMachines();
			}
		
			estrDestroy(&pTemp);

			return true;
		}
	}

	return false;
}

bool OVERRIDE_LATELINK_SystemIsReadyForAlerts(void)
{
	if (GetAppGlobalType() == GLOBALTYPE_CONTROLLER)
	{
		return true;
	}

	if (GetControllerLink())
	{
		return true;
	}

	if (objLocalManager())
	{
		return true;
	}

	return false;
}

StashTable sAccountLocationRequests = NULL;
U32 uAccountLocRequestID = 0;

typedef struct OnlineCharacterIDRequest
{
	OnlineCharacterIDFromAccountIDCB pCB;
	void *pUserData;
} OnlineCharacterIDRequest;

AUTO_COMMAND_REMOTE;
void ReturnOnlineCharacterIDForAccountID(U32 requestID, U32 resultID)
{
	OnlineCharacterIDRequest *pRequest = NULL;

	if(!stashIntRemovePointer(sAccountLocationRequests, requestID, &pRequest))
		return;

	if(!devassert(pRequest))
		return;

	(*pRequest->pCB)(resultID, pRequest->pUserData);
	free(pRequest);
}

U32 MakeOnlineCharacterIDRequest(OnlineCharacterIDFromAccountIDCB pCB, void *pUserData)
{
	OnlineCharacterIDRequest *pRequest = NULL;
	U32 requestID = ++uAccountLocRequestID;

	if(!sAccountLocationRequests)
	{
		sAccountLocationRequests = stashTableCreateInt(8);
	}

	pRequest = calloc(1, sizeof(OnlineCharacterIDRequest));
	pRequest->pCB = pCB;
	pRequest->pUserData = pUserData;
	stashIntAddPointer(sAccountLocationRequests, requestID, pRequest, true);

	return requestID;
}

void RequestOnlineCharacterIDFromAccountID(U32 uAccountID, OnlineCharacterIDFromAccountIDCB pCB, void *pUserData)
{
	U32 uRequestID = MakeOnlineCharacterIDRequest(pCB, pUserData);
	RemoteCommand_RequestOnlineCharacterIDFromAccountID(GLOBALTYPE_OBJECTDB, 0, GetAppGlobalType(), GetAppGlobalID(), uRequestID, uAccountID);
}

void DEFAULT_LATELINK_dbOnlineCharacterIDFromAccountID(GlobalType eSourceType, ContainerID uSourceID, U32 uRequestID, U32 uAccountID)
{
	return;
}

AUTO_COMMAND_REMOTE ACMD_NAME(RequestOnlineCharacterIDFromAccountID) ACMD_IFDEF(SERVERLIB);
void RemoteRequestOnlineCharacterIDFromAccountID(GlobalType eSourceType, ContainerID uSourceID, U32 uRequestID, U32 uAccountID)
{
	dbOnlineCharacterIDFromAccountID(eSourceType, uSourceID, uRequestID, uAccountID);
}

//What are these two next functions doing here? Well, AutoTransSupport needs to call a remote command, but it can't because it's
//in utilitiesLib, so we latelink to here. Really, nothing to see here, move along
void OVERRIDE_LATELINK_RemotelyVerifyNoReturnLogging(const char *pAutoTransFuncName, GlobalType eTypeToExecuteOn)
{
	RemoteCommand_CallRemoteVerifyNoReturnLogging(eTypeToExecuteOn, 0, pAutoTransFuncName, GetAppGlobalType());
}
AUTO_COMMAND_REMOTE;
void CallRemoteVerifyNoReturnLogging(char *pAutoTransFuncName, GlobalType eCallingType)
{
	FinalCallRemoteVerifyNoReturnLogging(pAutoTransFuncName, eCallingType);
}



char *DEFAULT_LATELINK_GetControllerTrackerHost(void)
{
	return gServerLibLoadedConfig.newControllerTrackerHost_internal;
}

char *DEFAULT_LATELINK_GetQAControllerTrackerHost(void)
{
	return gServerLibLoadedConfig.qaControllerTrackerHost_internal;
}


void DEFAULT_LATELINK_TextureServerReturn(int iRequestID, TextParserBinaryBlock *pTexture, char *pErrorString)
{

}

//TextureServer calls this to return textures
AUTO_COMMAND_REMOTE;
void HereIsTextureFromTextureServer(int iRequestID, TextParserBinaryBlock *pTexture, char *pErrorString)
{
	TextureServerReturn(iRequestID, pTexture, pErrorString);
}

void VerifyServerTypeExistsInShardEx(GlobalType eType)
{
	RemoteCommand_CheckIfServerTypeExists(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalID(), eType);
}


void DEFAULT_LATELINK_GetHeadShot_ReturnInternal(TextParserBinaryBlock *pData, char *pMessage, U32 iUserData)
{
	assertmsg(0, "Someone calling DEFAULT_LATELINK_GetHeadShot_ReturnInternal, must be overridden");
}

//this is called as the "return value" from RemoteCommand_GetHeadShot
AUTO_COMMAND_REMOTE;
void GetHeadShot_Return(TextParserBinaryBlock *pData, char *pMessage, U32 iUserData)
{
	GetHeadShot_ReturnInternal(pData, pMessage, iUserData);

}


AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void TestIntershardPrintfCommand(char *pStringToPrint)
{
	printf("%s", pStringToPrint);
}

AUTO_COMMAND;
void TestIntershard(char *pShardName, char *pTypeName, U32 iID, char *pString)
{
	GlobalType eType = NameToGlobalType(pTypeName);
	if (!eType)
	{
		printf("Uknown type %s", pTypeName);
		return;
	}

	RemoteCommand_Intershard_TestIntershardPrintfCommand(pShardName, eType, iID, pString);
}




#include "autogen/controllerpub_h_ast.c"
#include "Autogen/serverlib_h_ast.c"
#include "autogen/svrGlobalInfo_h_ast.c"
#include "autogen/GlobalEnums_h_ast.c"
#include "autogen/MapDescription_h_ast.c"
#include "AslJobManagerPub_h_ast.c"
#include "ResourceDBUtils.h"
#include "autogen/UGCProjectCommon_h_ast.c"
#include "autogen/ResourceDBUtils_h_ast.c"
#include "serverlib_c_ast.c"
