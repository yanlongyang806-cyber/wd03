#include "ControllerLink.h"
#include "logging.h"
#include "Timing.h"
#include "GlobalComm.h"
#include "HttpXpathSupport.h"
#include "utilitieslib.h"
#include "GenericFileServing.h"
#include "timedCallback.h"
#include "ContinuousBuilderSupport.h"
#include "estring.h"
#include "hoglib.h"
#include "NotesServerComm.h"
#include "StructNet.h"
#include "..\..\Utilities\NotesServer\NotesServer_pub.h"
#include "NotesServer_pub_h_ast.h"
#include "winutil.h"
#include "..\..\libs\Serverlib\pub\ShardCluster.h"
#include "ShardCluster_h_ast.h"
#include "GlobalTypes_h_ast.h"
#include "Alerts.h"
#include "GlobalComm_h_ast.h"


//the editors budget should be 0 bytes in clients in prod mode, so I'm using it as a "Debug" memory budget.
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


static NetComm *controller_comm;

int giTimeDifferenceWithController = 0;

NetLink *gpControllerLink = NULL;
static bool sbControllerConnectionSucceeded;
static PacketCallback *spRequestedAuxMCPMessageHandler = NULL;
static PacketCallback *spAuxMCPMessageHandler = NULL;
static bool sbCanSurviveControllerDisconnect = false;
static bool sbUnfailableConnectionAttempted = false;
static bool sbWasConnected = false;

bool gbNeverConnectToController = false;
AUTO_CMD_INT(gbNeverConnectToController, NeverConnectToController) ACMD_CMDLINE;

bool gbCanAlwaysSurviveControllerDisconnect = false;


static char *spControllerLinkDisconnectReason = NULL;

char *DEFAULT_LATELINK_GetControllerHost(void)
{
	return "localhost";
}


U32 DEFAULT_LATELINK_GetAntiZombificationCookie(void)
{
	return 0;
}


void DEFAULT_LATELINK_IncAntiZombificationCoookie(void)
{
}

void DEFAULT_LATELINK_AutoSetting_PacketOfCommandsFromController(Packet *pPack)
{
	assertmsgf(0, "Auto setting commands being sent to something without serverlib?");
}

void ControllerLink_SetNeverConnect(bool bSet)
{
	gbNeverConnectToController = true;
}

void DEFAULT_LATELINK_ControllerLink_ProcessTimeDifference(int iTimeDiff)
{


}

static int ControllerHandshakeResultCB(Packet *pak, int cmd, NetLink *link, void *user_data)
{

	sbControllerConnectionSucceeded = (bool)(pktGetBits(pak, 1));

	if (!sbControllerConnectionSucceeded)
	{
		bool bFatalError = (bool)(pktGetBits(pak, 1));

		if (bFatalError)
		{
			Errorf("Controller connection error: %s", pktGetStringTemp(pak));
//			assertmsgf(0, "Controller connection error: %s", pktGetStringTemp(pak));
		}
	}
	else
	{
		spAuxMCPMessageHandler = spRequestedAuxMCPMessageHandler;
	}


	return 1;
}

void FileServingPacketFulfillCB(int iRequestID, char *pErrorString,
	 U64 iTotalSize, U64 iCurBeginByteOffset, U64 iCurNumBytes, U8 *pCurData)
{
	if (gpControllerLink)
	{
		Packet *pPak = pktCreate(gpControllerLink, TO_CONTROLLER_FILE_SERVING_FULFILLED);

		pktSendBits(pPak, 32, iRequestID);
		if (pErrorString)
		{
			pktSendBits(pPak, 1, 1);
			pktSendString(pPak, pErrorString);
		}
		else
		{
			pktSendBits(pPak, 1, 0);
		}

		pktSendBits64(pPak, 64, iTotalSize);
		pktSendBits64(pPak, 64, iCurBeginByteOffset);
		pktSendBits64(pPak, 64, iCurNumBytes);

		if (iCurNumBytes)
		{
			pktSendBytes(pPak, iCurNumBytes, pCurData);
		}

		if (pCurData)
		{
			free(pCurData);
		}

		pktSend(&pPak);

	}

}

void HandleFileServingRequestFromPacket(Packet *pPacket, NetLink *pNetLink)
{
	char *pFileName = pktGetStringTemp(pPacket);
	int iReqID = pktGetBits(pPacket, 32);
	enumFileServingCommand eCommand = pktGetBits(pPacket, 32);
	S64 iBytesRequested = pktGetBits64(pPacket, 64);

	//for now, all commands must come from controller
	assert(pNetLink == gpControllerLink);

	GenericFileServing_CommandCallBack(pFileName, iReqID, eCommand, iBytesRequested, FileServingPacketFulfillCB);
}

void SendKeepAlivePacketToController(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	if (gpControllerLink)
	{
		Packet *pPak;
		PERFINFO_AUTO_START_FUNC();
		pPak = pktCreate(gpControllerLink, TO_CONTROLLER_KEEP_ALIVE);
		PutContainerTypeIntoPacket(pPak, GetAppGlobalType());
		PutContainerIDIntoPacket(pPak, GetAppGlobalID());
		pktSend(&pPak);
		PERFINFO_AUTO_STOP();
	}
}

void HandleKeepAliveRequest(Packet *pak)
{
	int iSecsDelay = pktGetBits(pak, 32);
	TimedCallback_Add(SendKeepAlivePacketToController, NULL, iSecsDelay);
}

void HandleSingleNote(Packet *pak)
{
	SingleNote *pNote = StructCreate(parse_SingleNote);
	ParserRecv(parse_SingleNote, pak, pNote, 0);
	NotesServer_SetSingleNote(pNote);
	StructDestroy(parse_SingleNote, pNote);
}

void HandleShardClusterOverview(Packet *pPak)
{
	Cluster_Overview *pOverview = StructCreate(parse_Cluster_Overview);
	ParserRecv(parse_Cluster_Overview, pPak, pOverview, 0);
	SetShardClusterOverview(pOverview);
}


void HandleHereIsTime(Packet *pPak);

void ControllerLinkCallback(Packet *pak, int cmd, NetLink *link, void *userdata)
{
	char *pTempChar;

	if (!sbControllerConnectionSucceeded)
	{
		assert(cmd == FROM_CONTROLLER_CONNECTIONRESULT);
		ControllerHandshakeResultCB(pak,cmd,link,userdata);
		return;
	}

	COARSE_AUTO_START_STATIC_DEFINE(pTempChar, cmd, FromControllerMsgEnum);

	switch (cmd)
	{
	xcase FROM_CONTROLLER_KILLYOURSELF:
		log_printf(LOG_CRASH, "Controller told us to die");
		// 
		if (g_isContinuousBuilder)
			assert(!hogTotalPendingOperationCount());	// COR-15323 : See if the process is being killed before all hogg operations have completed.
		exit(-1);
	xcase FROM_CONTROLLER_INCCOOKIE:
		IncAntiZombificationCoookie();
//		gServerLibState.antiZombificationCookie++;
	xcase FROM_CONTROLLER_REQUESTING_MONITORING_INFO:
		HandleMonitoringInfoRequestFromPacket(pak, link);
	xcase FROM_CONTROLLER_REQUESTING_MONITORING_COMMAND:
		HandleMonitoringCommandRequestFromPacket(pak, link);
	xcase FROM_CONTROLLER_REQUESTING_MONITORING_JPEG:
		HandleMonitoringJpegRequestFromPacket(pak, link);
	xcase FROM_CONTRLLER_REQUESTING_FILE_SERVING:
		HandleFileServingRequestFromPacket(pak, link);
	xcase FROM_CONTROLLER_REQUEST_BEGIN_KEEPALIVE:
		HandleKeepAliveRequest(pak);
	xcase FROM_CONTROLLER_AUTO_SETTING_COMMANDS:
		AutoSetting_PacketOfCommandsFromController(pak);
	xcase FROM_CONTROLLER_HERE_IS_LOCAL_NOTE:
		HandleSingleNote(pak);
	xcase FROM_CONTROLLER_HERE_IS_SHARD_CLUSTER_OVERVIEW:
		HandleShardClusterOverview(pak);
	xcase FROM_CONTROLLER_HERE_IS_TIMESS2000:
		HandleHereIsTime(pak);
	xdefault:
		if (spAuxMCPMessageHandler)
		{
			spAuxMCPMessageHandler(pak, cmd, link, userdata);
		}
		else
		{
			assertmsgf(0, "Got unknown message %d from controller, have no aux handler. Probable race condition\n",
				cmd);
		}

	}

	COARSE_AUTO_STOP_STATIC_DEFINE(pTempChar);

}

static void ControllerLinkDisconnect(NetLink* link,void *user_data)
{
	linkGetDisconnectReason(link, &spControllerLinkDisconnectReason);
	gpControllerLink = NULL;
}

static void ControllerLinkConnect(NetLink *link, void *user_data)
{
	sbWasConnected = true;
	linkInitReceiveStats(link, LauncherQueryCommandsEnum);
}

#define FAIL(str) { printf("%s", str); if (!bCanFail) { log_printf(LOG_CRASH, "%s", str); if (g_isContinuousBuilder) assertmsg(0, str); exit(-1); } }

void AttemptToConnectToController(bool bCanFail, PacketCallback *pAuxMessageHandler, bool bCanSurviveDisconnect)
{
	Packet *pPacket;
	int ret;

	if (GetAppGlobalType() == GLOBALTYPE_CONTROLLER)
	{
		return;
	}

	if (gpControllerLink)
	{
		return;
	}

	if (gbNeverConnectToController)
	{
		return;
	}

	printf("Attempting to connect to controller (%s)...", GetControllerHost());

	if (!bCanFail)
	{
		sbUnfailableConnectionAttempted = true;
	}

	if (!controller_comm)
		controller_comm = commCreate(0,0);
	sbCanSurviveControllerDisconnect = bCanSurviveDisconnect;
	spRequestedAuxMCPMessageHandler = pAuxMessageHandler;

	gpControllerLink = commConnect(controller_comm, LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,GetControllerHost(),DEFAULT_CONTROLLER_PORT,ControllerLinkCallback,ControllerLinkConnect,ControllerLinkDisconnect,0);

	if (!gpControllerLink)
	{
		FAIL("initial commconnect to controller failed.");
		return;
	}

	if (!linkConnectWait(&gpControllerLink,15))
	{
		FAIL("LinkConnectWait to controller failed after 15 seconds");
		return;
	}

	printf("Connected to controller... doing handshaking\n");

	pPacket = pktCreate(gpControllerLink, TO_CONTROLLER_CONNECT);

	pktSendBitsPack(pPacket, 1, GetAppGlobalType());
	pktSendBitsPack(pPacket, 1, GetAppGlobalID());
// FIXME we are not currently using these cookies and they do nothing
	pktSendBitsPack(pPacket, 1, GetAntiZombificationCookie());
//	pktSendBitsPack(pPacket, 1, gServerLibState.antiZombificationCookie);
	pktSendString(pPacket, GetProductName());
#if PLATFORM_CONSOLE
    pktSendBits(pPacket, 32, 0);
#else
	pktSendBits(pPacket, 32, getpid());
#endif
	pktSendString(pPacket, GetUsefulVersionString());

	pktSend(&pPacket);

	sbControllerConnectionSucceeded = false;

	ret = linkWaitForPacket(gpControllerLink, 0, 1000.0f);
	if (!ret)
	{
		linkRemove(&gpControllerLink);

		if (!bCanFail)
		{	
			Errorf("Never got handshake back from Controller");
			log_printf(LOG_CRASH, "couldn't connect to Controller");
			exit(-1);
		}
		else
		{
			printf("failed.\n");
			return;
		}
	}

	if (!sbControllerConnectionSucceeded)
	{
		linkRemove(&gpControllerLink);

		if (!bCanFail)
		{
			log_printf(LOG_CRASH, "couldn't connect to Controller");
			exit(-1);
		}
		else
		{
			printf("failed.\n");
			return;
		}
	}

	ConnectedToController_ServerSpecificStuff();

	printf("connected.\n");
}

void DEFAULT_LATELINK_ControllerLink_ExtraDisconnectLogging(void)
{

}

void DEFAULT_LATELINK_ConnectedToController_ServerSpecificStuff(void)
{

}

static void DoInternalDisconnectStuff(void)
{
	if (!(sbCanSurviveControllerDisconnect || gbCanAlwaysSurviveControllerDisconnect))
	{
		if (!gpControllerLink && !spControllerLinkDisconnectReason)
		{
			estrConcatf(&spControllerLinkDisconnectReason, "Link ended up NULL somehow");
		}
			
		log_printf(LOG_CRASH, "Lost connection to Controller. Reason: %s", spControllerLinkDisconnectReason);
		

		ControllerLink_ExtraDisconnectLogging();

		//let's just pretend tha the MCP itself told us it was dying, rather than just dying.
		if (spRequestedAuxMCPMessageHandler)
		{
			spRequestedAuxMCPMessageHandler(NULL, FROM_CONTROLLER_IAMDYING, NULL, NULL);
		}

		exit(-1);
	}
	else
	{
		if (spRequestedAuxMCPMessageHandler)
		{
			spRequestedAuxMCPMessageHandler(NULL, FROM_CONTROLLER_IAMDYING, NULL, NULL);
		}

		ClearControllerConnection();
	}
}

static bool sbWantToCalcTimeDifference = false;
static U32 siLastTimeAskedForTimeDifferential = 0;

#define MAX_TIME_CHECKS_BEFORE_GIVING_UP 10

void RequestTimeDifferenceWithController(void)
{
	sbWantToCalcTimeDifference = true;
}

void HandleHereIsTime(Packet *pPak)
{
	if (timeSecondsSince2000() < siLastTimeAskedForTimeDifferential + 2)
	{
		U32 iControllerTime = pktGetBits(pPak, 32);
		U32 iCurTime = timeSecondsSince2000();

		if (iCurTime > iControllerTime)
		{
			ControllerLink_ProcessTimeDifference(iCurTime - iControllerTime);
		}
		else
		{
			ControllerLink_ProcessTimeDifference(iControllerTime - iCurTime);
		}
		sbWantToCalcTimeDifference = false;
	}

	siLastTimeAskedForTimeDifferential = 0;

}


static __forceinline void CheckForTimeDifferential(void)
{
	static int siNumTimeChecks = 0;

	if (!siLastTimeAskedForTimeDifferential)
	{
		if (siNumTimeChecks >= MAX_TIME_CHECKS_BEFORE_GIVING_UP)
		{
			CRITICAL_NETOPS_ALERT("COULDNT_GET_CLOCK_DIFF", "While trying to get the clock diff between this machine and the controller machine, never got a response back in < 1 second, tried %d times, giving up",
				MAX_TIME_CHECKS_BEFORE_GIVING_UP);
			sbWantToCalcTimeDifference = false;
		}
		else
		{
			Packet *pPak = pktCreate(gpControllerLink, TO_CONTROLLER_REQUESTING_TIMESS2000);
			pktSend(&pPak);
			siLastTimeAskedForTimeDifferential = timeSecondsSince2000();
			siNumTimeChecks++;
		}
	}
}


void UpdateControllerConnection(void)
{
	PERFINFO_AUTO_START_FUNC();

	//just to make sure there's no weird corner case where the link gets disconnected and removed somehow before the
	//disconnect stuff gets all set up
	if (!sbWasConnected && sbUnfailableConnectionAttempted && !gpControllerLink)
	{	
		sbUnfailableConnectionAttempted = false;
		COARSE_WRAP(DoInternalDisconnectStuff());
	}
	else if (gpControllerLink)
	{
		COARSE_WRAP(commMonitor(controller_comm));
			
		if (linkDisconnected(gpControllerLink))
		{
			COARSE_WRAP(DoInternalDisconnectStuff());
		}
		else if (sbWantToCalcTimeDifference)
		{
			COARSE_WRAP(CheckForTimeDifferential());
		}
	}
	else if (sbWasConnected)
	{
		COARSE_WRAP(DoInternalDisconnectStuff());
	}
	PERFINFO_AUTO_STOP();
}

void ClearControllerConnection(void)
{
	if (gpControllerLink)
	{
		linkRemove(&gpControllerLink);
	}

	estrDestroy(&spControllerLinkDisconnectReason);
	sbWasConnected = false;
}

void DirectlyInformControllerOfState(char *pStateString)
{
	if (gpControllerLink)
	{
		Packet *pPak = pktCreate(gpControllerLink, TO_CONTROLLER_SETTING_SERVER_STATE);
		
		PutContainerTypeIntoPacket(pPak, GetAppGlobalType());
		PutContainerIDIntoPacket(pPak, GetAppGlobalID());

		pktSendString(pPak, pStateString);

		pktSend(&pPak);
	}
}

void sendErrorsToControllerCB(ErrorMessage *errMsg, void *userdata)
{
	if (GetControllerLink())
	{
		Packet *pPak;
		
		pktCreateWithCachedTracker(pPak, GetControllerLink(), TO_CONTROLLER_ERROR_DIALOG_FOR_MCP);

		pktSendString(pPak, STACK_SPRINTF("%s (reported by %s)", errorFormatErrorMessage(errMsg), GlobalTypeToName(GetAppGlobalType())));
		pktSendString(pPak, "");
		pktSendString(pPak, "");
		pktSendBitsPack(pPak, 32, 0);

		pktSend(&pPak);
	}
}

void SendErrorsToController(void)
{
	ErrorfPushCallback(sendErrorsToControllerCB, NULL);
}

//here for lack of anywhere better to put it
void DEFAULT_LATELINK_SetShardClusterOverview(Cluster_Overview *pOverview)
{
	assertmsgf(0, "Can't call SetShardClusterOverview without ServerLib");
}

void DEFAULT_LATELINK_ShardClusterOverviewChanged(void)
{

}

#include "ShardCluster_h_ast.c"