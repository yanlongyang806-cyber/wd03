/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "UtilitiesLibEnums.h"
#include "gclBaseStates.h"
#include "gclSendToServer.h"
#include "gclLoading.h"
#include "gclHandleMsg.h"
#include "gclLogin.h"
#include "GameClientLib.h"
#include "EntityNet.h"
#include "EntityMovementManager.h"
#include "gclEntity.h"
#include "WorldGrid.h"
#include "cmdParse.h"
#include "logging.h"
#include "GlobalStateMachine.h"
#include "assert.h"
#include "wininclude.h"
#include "sock.h"
#include "chatCommonStructs.h"
#include "chat/gclClientChat.h"
#include "GameStringFormat.h"
#include "Player.h"
#include "NotifyCommon.h"
#include "gclDemo.h"
#include "netprivate.h"
#include "AutoGen/gclSendToServer_c_ast.h"
#include "AutoGen/ChatRelay_autogen_GenericServerCmdWrappers.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););

static GCLServerConnectionInfo connInfoCur;
static GCLServerConnectionInfo connInfoPrev;

//pointer to pointer, so that when it frees the link it will free the original variable
NetLink**	gppLastLink;
NetLink*	gServerLink;
static U32	serverLinkInstance;
static U32	msTimeReportedProblemLink;
Packet*		gClientInputPak;
static U32	myIPOnTheGameServer;
static U32	myPortOnTheGameServer;

static int s_iCloseServerConnectionOnAssert;
AUTO_CMD_INT(s_iCloseServerConnectionOnAssert, s_iCloseServerConnectionOnAssert) ACMD_CMDLINE;

static void gclCheckForLinkProblems(void){
	if(	gclServerTimeSinceRecv() > 5.0f
		&&
		(	!msTimeReportedProblemLink ||
			timeGetTime() - msTimeReportedProblemLink >= 60 * 1000))
	{
		msTimeReportedProblemLink = timeGetTime();
		
		if(isProductionMode()){
			char ipString[100];
			
			linkGetIpListenPortStr(gServerLink, SAFESTR(ipString));

			ErrorDetailsf(	"GameServer address %s, client link instance %d",
							ipString,
							serverLinkInstance);

			Errorf("GameServer not responding");
		}

        log_printf(LOG_CLIENTSERVERCOMM, "GameServer link unresponsive for more than 5 seconds.");
    }
}

static void gclConnInfoUpdate(	GCLServerConnectionInfo* info,
								NetLink* link)
{
	const LinkStats* stats = linkStats(link);
	
	if(!link){
		return;
	}
	
	info->ip = linkGetIp(link);
	info->port = linkGetListenPort(link);
	
	if(stats){
		info->bytes.received.compressed = stats->recv.real_bytes;
		info->bytes.received.uncompressed = stats->recv.bytes;
		info->bytes.sent.compressed = stats->send.real_bytes;
		info->bytes.sent.uncompressed = stats->send.bytes;
	}
}

void gclServerMonitorConnection(void){	
	PerfInfoGuard* piGuard;

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);
	commMonitor(commDefault()); 
	gclCheckForLinkProblems();
	gclConnInfoUpdate(&connInfoCur, gServerLink);
	PERFINFO_AUTO_STOP_GUARD(&piGuard);
}

void gclServerRequestWorldUpdate(int full_update){
	Packet *pak = gclServerCreateRealPacket(TOSERVER_REQ_UPDATE);
	if(pak){
		pktSendBits(pak, 1, !!full_update);
		pktSend(&pak);
	}
}

void gclServerSendLockedUpdates(void){
	worldSendLockedUpdate(gServerLink, TOSERVER_LOCKED_UPDATE);
}

S32 gclServerIsConnected(void){
	return linkConnected(gServerLink);
}

S32 gclServerIsDisconnected(void){
	return	!gServerLink ||
			linkDisconnected(gServerLink);
}

F32 gclServerTimeSinceRecv(void){
	return linkRecvTimeElapsed(gServerLink);
}

Packet* gclServerCreateRealPacket(U32 cmd)
{

	if (!gServerLink)
	{
		return NULL;
	}
	else
	{
		static PacketTracker *pTracker;
		ONCE(pTracker = PacketTrackerFind("gclServerCreateRealPacket", 0, NULL));

		return pktCreateWithTracker(gServerLink, cmd, pTracker);
	}
}

void gclServerNewInputPacket(void){
	if(!gClientInputPak){
		gClientInputPak = gclServerCreateRealPacket(TOSERVER_GAME_MSG);
	}
}

void gclServerDestroyInputPacket(void){
	pktFree(&gClientInputPak);
}

static S32 sendMovementInputPackets = 1;
AUTO_CMD_INT(sendMovementInputPackets, sendMovementInputPackets);

static void gclServerSendPhysicsUpdate(void){
	Entity* e = entActivePlayerPtr();
	if(	sendMovementInputPackets &&
		e)
	{
		START_INPUT_PACKET(e, pak, GAMECLIENTLIB_SEND_PHYSICS)
		mmSendToServer(pak);
		END_INPUT_PACKET
	}
}

static void gclServerSendRefDictDataRequests(void){
	Entity *e = entActivePlayerPtr();
	if (e && resClientAreTherePendingRequests())
	{
		START_INPUT_PACKET(e, pak, GAMECLIENTLIB_SEND_REFDICT_DATA_REQUESTS)
		resClientSendRequestsToServer(pak);
		END_INPUT_PACKET
	}
}

extern Packet *gpPacketForTestClientCommands;

static void gclServerSendTestClientCommandRequests(void){
	Entity *e = entActivePlayerPtr();
	if (gpPacketForTestClientCommands && e)
	{
		START_INPUT_PACKET(e, pak, GAMECLIENTLIB_SEND_TESTCLIENT_COMMAND_REQUESTS)
		pktAppend(pak, gpPacketForTestClientCommands, 0);
		pktSendU32(pak, 0);
		END_INPUT_PACKET

		pktFree(&gpPacketForTestClientCommands);
	}
}

AUTO_COMMAND ACMD_NAME("sendToServerInterval");
void gclSendToServerInterval(F32 interval)
{
	gGCLState.sendToServerInterval = MINMAX(interval, 0.f, 1.f);
}

AUTO_RUN;
void gclSendToServerIntervalInit(void)
{
	// This used to be 0.1, but we've turned it up to improve responsiveness.  The server impact
	//  should be negligible.
	gGCLState.sendToServerInterval = 0.03333f;
}

static S32 gclSendWaitForTrigger;
AUTO_CMD_INT(gclSendWaitForTrigger, gclSendWaitForTrigger);
static U32 gclSendTrigger;
AUTO_CMD_INT(gclSendTrigger, gclSendTrigger);
static S32 gclPrintSendToServerRate;
AUTO_CMD_INT(gclPrintSendToServerRate, gclPrintSendToServerRate);

void gclServerSendInputPacket(F32 deltaSeconds)
{
	static F32		deltaSecondsAcc;
	static U32		msTimeLast;
	U32				msTimeCur = timeGetTime();
	PerfInfoGuard*	piGuard;

	if(!gclServerIsConnected()){
		return;
	}
	
	PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);

	deltaSecondsAcc += deltaSeconds;
	
	if(gclPrintSendToServerRate){
		printf(	"%1.3f seconds acc, %1.3f seconds delta, %ums delta\n",
				deltaSecondsAcc,
				deltaSeconds,
				msTimeCur - msTimeLast);
	}
	
	if(deltaSecondsAcc >= gGCLState.sendToServerInterval)
	{
		Entity *e;
		
		if(gGCLState.sendToServerInterval > 0.f){
			deltaSecondsAcc = fmod(deltaSecondsAcc, gGCLState.sendToServerInterval);
		}else{
			deltaSecondsAcc = 0.f;
		}
		
		if(gclPrintSendToServerRate){
			printf("sending to server, %1.3f seconds remain\n", deltaSecondsAcc);
		}
		
		if(gclSendWaitForTrigger){
			if(!gclSendTrigger){
				PERFINFO_AUTO_STOP_GUARD(&piGuard);
				return;
			}
			
			gclSendTrigger--;
		}

		e = entActivePlayerPtr();

		if(	e && 
			gclServerIsConnected() &&
			(	gclServerTimeSinceRecv() < 5.f ||
				gclSendWaitForTrigger))
		{
			PERFINFO_AUTO_START_FUNC_PIX();

			// fill in messages
			gclServerSendPhysicsUpdate();

			gclServerSendRefDictDataRequests();

			gclServerSendTestClientCommandRequests();

			// actually send the message packet off

			pktSend(&gClientInputPak);

			PERFINFO_AUTO_STOP_PIX();
		}
	}
	
	msTimeLast = msTimeCur;

	PERFINFO_AUTO_STOP_GUARD(&piGuard);
}

void gclSendPublicCommand(CmdContextFlag iFlags, const char *s, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{
	Entity *player = entActivePlayerPtr();
	if (player && gclServerIsConnected())
	{	
		char *pComment = NULL;
		if (pStructs)
		{
			estrStackCreate(&pComment);
			estrPrintf(&pComment, "Struct being sent client-to-server via gclSendPublicCommand for command %s", s);
		}

		START_INPUT_PACKET(player, pak, GAMECLIENTLIB_CMD_PUBLIC);
		cmdParsePutStructListIntoPacket(pak, pStructs, pComment);
		pktSendString(pak, s);
		pktSendBits(pak, 32, iFlags);
		pktSendBits(pak, 32, eHow);
		END_INPUT_PACKET

		estrDestroy(&pComment);
	}
}


void gclSendPrivateCommand(CmdContextFlag iFlags, const char *s, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{
	Entity *e = entActivePlayerPtr();
	if (e && gclServerIsConnected())
	{	
		char *pComment = NULL;
		if (pStructs)
		{
			estrStackCreate(&pComment);
			estrPrintf(&pComment, "Struct being sent client-to-server via gclSendPublicCommand for command %s", s);
		}

		START_INPUT_PACKET(e, pak, GAMECLIENTLIB_CMD_PRIVATE);
		cmdParsePutStructListIntoPacket(pak, pStructs, pComment);
		pktSendString(pak, s);
		pktSendBits(pak, 32, iFlags);
		pktSendBits(pak, 32, eHow);
		END_INPUT_PACKET

		estrDestroy(&pComment);
	}
}

///////////////////////////////
// Chat Relay stuff
extern ChatAuthData *g_pChatAuthData;
#define CHATAUTHDATA_RELAYLINK (g_pChatAuthData ? g_pChatAuthData->pChatRelayLink : NULL)
#define CHATOFFLINE_SPAMTIME (3)
static void gclChatRelayHandlePacket(Packet* pak, int cmd, NetLink* link, void *user_data)
{
	gclHandlePacketFromGameServer(pak, cmd, link, user_data);
}

void gclSendChatRelayCommand(CmdContextFlag iFlags, const char *s, bool bPrivate, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{
	if (g_pChatAuthData && g_pChatAuthData->bAuthenticated && linkConnected(g_pChatAuthData->pChatRelayLink))
	{
		Packet *pak = pktCreate(g_pChatAuthData->pChatRelayLink, TOSERVER_GAME_MSG);
		pktSendU32(pak, bPrivate ? CHATRELAY_CMD_PRIVATE : CHATRELAY_CMD_PUBLIC);
		pktSendU32(pak, g_pChatAuthData->uAccountID);
		cmdParsePutStructListIntoPacket(pak, pStructs, NULL);
		pktSendString(pak, s);
		pktSendBits(pak, 32, iFlags);
		pktSendBits(pak, 32, eHow);
		pktSend(&pak);
	}
	else
	{
		static U32 uLastOffline = 0; // so it doesn't spam this in the case of multiple calls
		U32 uTime = timeSecondsSince2000();
		if (uTime - uLastOffline > CHATOFFLINE_SPAMTIME)
		{
			Entity *pEnt = entActivePlayerPtr();
			const char *msg = entTranslateMessageKey(pEnt, "Chat_Offline");
			notify_NotifySend(pEnt, kNotifyType_Failed, msg, NULL, NULL);
			uLastOffline = uTime;
		}
	}
}

void gclSendChatRelayAuthenticateRequest(U32 uSecret)
{
	if (g_pChatAuthData && linkConnected(g_pChatAuthData->pChatRelayLink))
	{
		Packet *pak = pktCreate(g_pChatAuthData->pChatRelayLink, TOSERVER_GAME_MSG);
		pktSendU32(pak, CHATRELAY_AUTHENTICATE);
		pktSendU32(pak, g_pChatAuthData->uAccountID);
		pktSendU32(pak, uSecret);
		pktSendU32(pak, ChatCommon_GetChatConfigSourceForEntity(NULL));
		pktSend(&pak);
	}
}

// Chat Connection States
extern NetLink *gpLoginLink;
#define CHAT_CONNECT_RETRY_PERIOD (3)
#define CHAT_AUTHDATA_TIMEOUT (10)
#define CHAT_AUTHREQUEST_TIMEOUT (10)

AUTO_ENUM;
typedef enum ChatConnectState
{
	CHATCONNECT_UNCONNECTED = 0,
	CHATCONNECT_WAITING_FOR_DATA,
	CHATCONNECT_WAITING_FOR_RELAY_CONNECTION,
	CHATCONNECT_WAITING_FOR_RELAY_AUTHENTICATION,
	CHATCONNECT_WAITING_FOR_LOGIN,
	CHATCONNECT_CONNECTED,
} ChatConnectState;
static ChatConnectState seChatState = CHATCONNECT_UNCONNECTED;
static U32 suStateStartTime = 0;
static U32 suLastConnectTryTime = 0;

static void gclChatConnect_SetState(U32 uTime, ChatConnectState eState)
{
	suStateStartTime = uTime ? uTime : timeSecondsSince2000();
	seChatState = eState;
}

static void gclChatRelayConnectCallback(NetLink* link,void *user_data)
{
	if (seChatState == CHATCONNECT_WAITING_FOR_RELAY_CONNECTION)
	{
		gclSendChatRelayAuthenticateRequest(g_pChatAuthData->uSecretValue);
		gclChatConnect_SetState(0, CHATCONNECT_WAITING_FOR_RELAY_AUTHENTICATION);
	}
}

static void gclChatRelayDisconnectCallback(NetLink* link,void *user_data)
{
	const U32	errorCode = linkGetDisconnectErrorCode(link);
	S32			isUnexpectedDisconnect = 0;
	char		ipString[100];
	char *pDisconnectReason = NULL;

	estrStackCreate(&pDisconnectReason);
	linkGetDisconnectReason(link, &pDisconnectReason);

	linkGetIpListenPortStr(link, SAFESTR(ipString));

	log_printf(	LOG_CLIENTSERVERCOMM,
		"ChatRelay link disconnected (%s). Reason: %s (socket error %d: %s)",
		ipString,
		pDisconnectReason,
		errorCode,
		sockGetReadableError(errorCode));

	estrDestroy(&pDisconnectReason);

	if (g_pChatAuthData && link == g_pChatAuthData->pChatRelayLink){
		// Unexpected disconnect.
		linkRemove(&g_pChatAuthData->pChatRelayLink);
	}

	gclChatConnect_SetState(0, CHATCONNECT_UNCONNECTED);
}

bool gclConnectChatRelay(const char *serverName, U32 serverPort)
{
	if (g_pChatAuthData)
	{
		g_pChatAuthData->pChatRelayLink= commConnect( commDefault(),
			LINKTYPE_UNSPEC, LINK_FORCE_FLUSH | LINK_NO_COMPRESS,
			serverName, serverPort,
			gclChatRelayHandlePacket,
			gclChatRelayConnectCallback,
			gclChatRelayDisconnectCallback,
			0);

		if(!g_pChatAuthData->pChatRelayLink)
			return false;

		linkSetMaxAllowedPacket(g_pChatAuthData->pChatRelayLink,1<<19);

#if !PLATFORM_CONSOLE
		if(s_iCloseServerConnectionOnAssert)
			closeSockOnAssert(linkGetSocket(g_pChatAuthData->pChatRelayLink));
#endif
		linkAutoPing(g_pChatAuthData->pChatRelayLink,1);
		linkSetKeepAlive(g_pChatAuthData->pChatRelayLink);
	}
	return true;
}

static void gclChatConnect_Start(void)
{
	if (g_pChatAuthData)
	{
		StructDestroy(parse_ChatAuthData, g_pChatAuthData);
		g_pChatAuthData = NULL;
	}

	// Check for active state and connected links
    if (GSM_IsStateActive(GCL_GAMEPLAY))
	{
		if (gclServerIsConnected())
		{
			gclChatConnect_SetState(0, CHATCONNECT_WAITING_FOR_DATA);
			ServerCmd_gslChat_RequestChatRelayData();
		}
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void  gclChatConnect_ReceiveData(ChatAuthData *data)
{
	if (seChatState == CHATCONNECT_WAITING_FOR_DATA)
	{
		if (g_pChatAuthData)
			StructDestroy(parse_ChatAuthData, g_pChatAuthData);
		g_pChatAuthData = StructClone(parse_ChatAuthData, data);

		if (g_pChatAuthData->pRelayIPString && g_pChatAuthData->uRelayPort &&
			gclConnectChatRelay(g_pChatAuthData->pRelayIPString, g_pChatAuthData->uRelayPort))
		{
			gclChatConnect_SetState(0, CHATCONNECT_WAITING_FOR_RELAY_CONNECTION);
		}
		else
		{
			// Connect failed
			gclChatConnect_SetState(0, CHATCONNECT_UNCONNECTED);
		}
	}
}

void gclChatConnect_Logout(void)
{
	if (g_pChatAuthData)
	{
		if (g_pChatAuthData->pChatRelayLink)
			linkRemove(&g_pChatAuthData->pChatRelayLink);
		StructDestroy(parse_ChatAuthData, g_pChatAuthData);
		g_pChatAuthData = NULL;
	}
	gclClientChat_ClearChatState();
	gclChatConnect_SetState(0, CHATCONNECT_UNCONNECTED);
}

void gclChatConnect_Tick(void)
{
	PerfInfoGuard* piGuard;

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);

	switch (seChatState)
	{
	case CHATCONNECT_UNCONNECTED:
	{
		U32 uCurTime = timeSecondsSince2000();
		if (uCurTime - suLastConnectTryTime > CHAT_CONNECT_RETRY_PERIOD)
		{
			suLastConnectTryTime = uCurTime;
			gclChatConnect_Start();
		}
	}
	xcase CHATCONNECT_WAITING_FOR_DATA:
	case CHATCONNECT_WAITING_FOR_RELAY_CONNECTION:
	case CHATCONNECT_WAITING_FOR_RELAY_AUTHENTICATION: 
	case CHATCONNECT_WAITING_FOR_LOGIN: // intentional fall-through
		// timeouts at any authentication state reset connection status
		{
			U32 uTime = timeSecondsSince2000();
			if (suStateStartTime + CHAT_AUTHDATA_TIMEOUT < uTime)
			{
				gclChatConnect_Logout();
			}
		}
	xcase CHATCONNECT_CONNECTED:
		// Make sure we're actually logged in
		if (!gclChatIsConnected())
			gclChatConnect_Logout();
	default:
		// nothing to do
		break;
	}

	PERFINFO_AUTO_STOP_GUARD(&piGuard);
}
void gclChatConnect_RetryImmediate(void)
{
	if (seChatState != CHATCONNECT_UNCONNECTED)
		return;
	suLastConnectTryTime = timeSecondsSince2000();
	gclChatConnect_Start();
}

bool gclChatIsConnected(void)
{
	return (g_pChatAuthData && g_pChatAuthData->bAuthenticated && g_pChatAuthData->bLoggedIn && linkConnected(g_pChatAuthData->pChatRelayLink));
}

AUTO_COMMAND ACMD_GENERICCLIENTCMD ACMD_IFDEF(CHATRELAY) ACMD_PRIVATE ACMD_HIDE ACMD_ACCESSLEVEL(0);
void ChatAuthFailed(ACMD_SENTENCE error)
{
	// Reset connection state
	gclChatConnect_Logout();
}

AUTO_COMMAND ACMD_GENERICCLIENTCMD ACMD_IFDEF(CHATRELAY) ACMD_PRIVATE ACMD_HIDE ACMD_ACCESSLEVEL(0);
void ChatAuthSuccess(void)
{
	ChatLoginData loginData = {0};

	gclClientChat_ClearChatState(); // Make sure it's clear
	devassert(g_pChatAuthData || demo_playingBack() );
	if (g_pChatAuthData)
		g_pChatAuthData->bAuthenticated = true;

	loginData.pPlayerInfo = StructCreate(parse_PlayerInfoStruct);
	loginData.pPlayerInfo->eLanguage = entGetActiveLanguage();

	gclChatConnect_SetState(0, CHATCONNECT_WAITING_FOR_LOGIN);
	GServerCmd_crUserLogin(GLOBALTYPE_CHATRELAY, &loginData);
	StructDeInit(parse_ChatLoginData, &loginData);
}

void gclChatConnect_LoginDone(void)
{
	g_pChatAuthData->bLoggedIn = true;
	gclChatConnect_SetState(0, CHATCONNECT_CONNECTED);
}

// End ChatRelay stuff
//////////////////////////////

#undef gclSendPublicCommandf
void gclSendPublicCommandf(CmdContextFlag iFlags, const char *s, ...)
{
	va_list va;
	char *commandstr = NULL;
	estrStackCreate(&commandstr);

	va_start( va, s );
	estrConcatfv(&commandstr,s,va);
	va_end( va );

	gclSendPublicCommand(iFlags, commandstr,CMD_CONTEXT_HOWCALLED_UNSPECIFIED, NULL);
	estrDestroy(&commandstr);
}


#undef gclSendPrivateCommandf
void gclSendPrivateCommandf(CmdContextFlag iFlags, const char *s, ...)
{
	va_list va;
	char *commandstr = NULL;
	estrStackCreate(&commandstr);

	va_start( va, s );
	estrConcatfv(&commandstr,s,va);
	va_end( va );

	gclSendPrivateCommand(iFlags, commandstr, CMD_CONTEXT_HOWCALLED_UNSPECIFIED, NULL);
	estrDestroy(&commandstr);
}

int gclServerSendTicketAndScreenshot(U32 uType, const char *pData, const char *pExtraData, const char *pImageBuffer, U32 uImageSize)
{
	Entity *e = entActivePlayerPtr();
	if (e && gclServerIsConnected())
	{
		START_INPUT_PACKET(e, pak, GAMECLIENTLIB_SEND_SCREENSHOT);
		pktSendU32(pak, uType);
		pktSendString(pak, pData);
		pktSendString(pak, pExtraData);
		pktSendU32(pak, uImageSize);
		pktSendBytes(pak, uImageSize, (void*) pImageBuffer);
		END_INPUT_PACKET
		
		return 1;
	}
	return 0;
}

static void gclServerCleanupOnDisconnect(void){
	gclServerDestroyInputPacket();
	objDestroyAllContainers();
	mmClientDisconnectedFromServer();
}

U32 gclServerMyIPOnTheServer(void){
	return myIPOnTheGameServer;
}

AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0);
void thisIsYourIPOnTheGameServer(U32 ip, U32 port)
{
	char ipBuf[100];
	
	myIPOnTheGameServer = ip;
	myPortOnTheGameServer = port;
	
	printf("GameServer says my ip:port is %s:%d.\n", GetIpStr(ip, SAFESTR(ipBuf)), port);
}

static void gclServerConnectCallback(NetLink* link,void *user_data){
	char		ipString[100];

	assert(link == gServerLink);

	linkGetIpListenPortStr(link, SAFESTR(ipString));

	log_printf(	LOG_CLIENTSERVERCOMM,
				"GameServer link connected (%s).",
				ipString);

	mmClientConnectedToServer();

	gclConnInfoUpdate(&connInfoCur, link);
}

static void gclServerDisconnectCallback(NetLink* link,void *user_data){
	const U32	errorCode = linkGetDisconnectErrorCode(link);
	S32			isUnexpectedDisconnect = 0;
	char		ipString[100];
	char *pDisconnectReason = NULL;

	estrStackCreate(&pDisconnectReason);
	linkGetDisconnectReason(link, &pDisconnectReason);

	linkGetIpListenPortStr(link, SAFESTR(ipString));

	log_printf(	LOG_CLIENTSERVERCOMM,
				"GameServer link disconnected (%s). Reason: %s (socket error %d: %s)",
				ipString,
				pDisconnectReason,
				errorCode,
				sockGetReadableError(errorCode));

	estrDestroy(&pDisconnectReason);

	if(link == gServerLink){
		// Unexpected disconnect.
		
		isUnexpectedDisconnect = 1;

		connInfoPrev = connInfoCur;
		ZeroStruct(&connInfoCur);
		connInfoPrev.disconnectErrorCode = errorCode;
		gclConnInfoUpdate(&connInfoPrev, link);
		linkRemove(&gServerLink);

		gclServerCleanupOnDisconnect();
	}

	gclLoadingHandleDisconnect(isUnexpectedDisconnect);
}

static void gclServerHandlePacket(Packet* pak, int cmd, NetLink* link, void *user_data){
	gclConnInfoUpdate(&connInfoCur, link);
	gclHandlePacketFromGameServer(pak, cmd, link, user_data);
}

void gclServerForceDisconnect(const char* reason){
	char ipString[100];
	
	if(!gServerLink){
		return;
	}
	
	linkGetIpListenPortStr(gServerLink, SAFESTR(ipString));
	
	log_printf(	LOG_CLIENTSERVERCOMM,
				"GameServer link being forcibly disconnected by client (%s): %s",
				ipString,
				reason);

	connInfoPrev = connInfoCur;
	connInfoPrev.flags.disconnectedFromClient = 1;
	ZeroStruct(&connInfoCur);
	linkRemove_wReason(&gServerLink, reason);
	gclServerCleanupOnDisconnect();
}

static void gclMovementGlobalMsgHandler(const MovementGlobalMsg* msg){
	switch(msg->msgType){
		xcase MG_MSG_CREATE_PACKET_TO_SERVER:{
			*msg->createPacketToServer.pakOut = gclServerCreateRealPacket(TOSERVER_MOVEMENT_CLIENT);
		}
		
		xcase MG_MSG_SEND_PACKET_TO_SERVER:{
			Packet* pak = msg->sendPacketToServer.pak;
			pktSend(&pak);
		}

		xdefault:{
			entCommonMovementGlobalMsgHandler(msg);
		}
	}
}

S32 gclServerConnect(	const char* serverName,
						U32 serverPort)
{
	mmGlobalSetMsgHandler(gclMovementGlobalMsgHandler);

	gclServerForceDisconnect("Connecting to new server");
	linkRemove(gppLastLink);

	log_printf(	LOG_CLIENTSERVERCOMM,
				"GameServer link attempting connect (%s:%d).",
				serverName,
				serverPort);

	msTimeReportedProblemLink = 0;
	serverLinkInstance++;

	gServerLink = commConnect(	commDefault(),
								LINKTYPE_UNSPEC,
								LINK_FORCE_FLUSH,
								serverName,
								serverPort,
								gclServerHandlePacket,
								gclServerConnectCallback,
								gclServerDisconnectCallback,
								0);

	if(!gServerLink){
		gclLoginFail("Can't create link to GameServer.");
		return 0;
	}

	linkSetMaxAllowedPacket(gServerLink,1<<19);

	{
		extern int giDebugPacketDisconnect;
		if(giDebugPacketDisconnect){
			linkSetPacketDisconnect(gServerLink, giDebugPacketDisconnect);
			giDebugPacketDisconnect = 0;
		}
	}
	
	#if !PLATFORM_CONSOLE
		if(s_iCloseServerConnectionOnAssert){
			closeSockOnAssert(linkGetSocket(gServerLink));
		}
	#endif
	
	linkAutoPing(gServerLink,1);
	linkSetKeepAlive(gServerLink);
	
	return 1;
}

void gclServerGetConnectionInfo(const GCLServerConnectionInfo** infoCurOut,
								const GCLServerConnectionInfo** infoPrevOut)
{
	if(	connInfoCur.ip &&
		infoCurOut)
	{
		*infoCurOut = &connInfoCur;
	}
	
	if(	connInfoPrev.ip &&
		infoPrevOut)
	{
		*infoPrevOut = &connInfoPrev;
	}
}

F32 gclServerGetCurrentPing()
{
	if (gServerLink)
	{
		return gServerLink->stats.history[gServerLink->stats.last_recv_idx].elapsed;
	}
	return 0.f;
}

#include "AutoGen/gclSendToServer_c_ast.c"
