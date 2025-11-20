#include "chatRelayComm.h"

#include "chatCommonStructs.h"
#include "chatRelay.h"
#include "chatRelayCommandParse.h"
#include "ChatServer/chatBlacklist.h"
#include "cmdparse.h"
#include "ControllerPub.h"
#include "file.h"
#include "GlobalComm.h"
#include "logging.h"
#include "net.h"
#include "memlog.h"
#include "NotifyEnum.h"
#include "objTransactions.h"
#include "RemoteCommandGroup.h"
#include "ServerLib.h"
#include "sock.h"
#include "utils.h"

#include "AutoGen/Controller_autogen_RemoteFuncs.h"
#include "AutoGen/ChatServer_autogen_RemoteFuncs.h"
#include "AutoGen/GameClientLib_autogen_GenericClientCmdWrappers.h"

extern ServerLibState gServerLibState;

bool gbDebugMode = false;
AUTO_CMD_INT(gbDebugMode, DebugMode);

bool gbChatVerbose = false;
AUTO_CMD_INT(gbChatVerbose, chatVerbose);

static int maxSendPacket = 0;
AUTO_CMD_INT(maxSendPacket, maxSendPacket);

// This is only for what port to START trying to listen on
static int siListenPort = STARTING_CHATRELAY_PORT;
AUTO_CMD_INT(siListenPort, Port);

static bool sbChatServerOnline = false;
// NetListens for GameClient connections
static NetListen *sPrivateListen = NULL;
static NetListen *sPublicListen = NULL;

static bool sbLogAllClientPackets = false;
AUTO_CMD_INT(sbLogAllClientPackets, LogClientPackets);
static bool sbLogToFile = true;
AUTO_CMD_INT(sbLogToFile, LogToFile);

typedef struct ChatRelayAuthStruct
{
	U32 uAccountID;
	U32 uLinkID;
} ChatRelayAuthStruct;

static void chatRelay_AuthenticateUserCB(TransactionReturnVal *returnVal, ChatRelayAuthStruct *pAuthData)
{
	ChatAuthRequestData *pData = NULL;
	enumTransactionOutcome eOutcome;
	NetLink *link = linkFindByID(pAuthData->uLinkID);
	ChatRelayUser *user = chatRelayGetUser(pAuthData->uAccountID);

	if (user)
	{
		eOutcome = RemoteCommandCheck_crManager_AuthenticateUser(returnVal, &pData);
		if (eOutcome == TRANSACTION_OUTCOME_SUCCESS && pData && link)
		{
			user->uAccessLevel = pData->uAccountAccessLevel;
			user->bSocialRestricted = pData->bSocialRestricted;
			user->bAuthed = true;
			GClientCmd_ChatAuthSuccess(pAuthData->uAccountID);
		}
		else
		{
			// TODO proper error handling/messaging
			GClientCmd_ChatAuthFailed(pAuthData->uAccountID, "Authentication Failed");
			linkRemove_wReason(&link, "Authentication failed.");
		}
		if (pData)
			StructDestroy(parse_ChatAuthRequestData, pData);
	}
	free(pAuthData);
}

static MemLog sChatRelayLog = {0};
AUTO_COMMAND ACMD_CATEGORY(ChatRelayDebug);
void chatRelayEnableClientPacketLogging(bool bEnable)
{
	sbLogAllClientPackets = bEnable;
}
AUTO_COMMAND ACMD_CATEGORY(ChatRelayDebug);
void chatRelayEnableLogToFile(bool bEnable)
{
	sbLogToFile = bEnable;
}
AUTO_RUN;
void chatRelayMemlogInit(void)
{
	memlog_init(&sChatRelayLog);
}
static char *chatRelayClientLogFilename(void)
{
	static char filename[MAX_PATH] = "";
	if (!filename[0])
	{
		sprintf(filename, "%s/ChatRelayLogs/ChatRelay_%d_log.txt", fileLocalDataDir(), GetAppGlobalID());
	}
	return filename;
}
static void chatRelayLogClientPacketData(char *cmd, char *data)
{
	if (sbLogToFile)
		filelog_printf(chatRelayClientLogFilename(), "%s\t%s", cmd, data);
	memlog_printf(&sChatRelayLog, "%s - %s - %s", timeGetLocalDateString(), cmd, data);
}

__forceinline static bool chatRelayRunCommand(Packet* pak, NetLink *link, ChatRelayUser *relayUser, bool bPrivateCmd)
{
	char *pErrorString = NULL;
	CmdParseStructList structList = {0};
	bool bSuccess = false;

	if (!relayUser || !relayUser->bAuthed)
	{
		// Bad command request - no user or not authed
		linkRemove(&link);
		return false;
	}

	cmdParseGetStructListFromPacket(pak, &structList, &pErrorString, true);
	if (!eaSize(&structList.ppEntries) && pErrorString)
	{
		Errorf("Data corruption in link %s: %s", linkDebugName(link), pErrorString);
		pktSetErrorOccurred(pak, "Data corruption. See errorf.");
	}
	else
	{
		char *msg = pktGetStringTemp(pak);
		U32 iFlags = pktGetBits(pak, 32);
		enumCmdContextHowCalled eHow = pktGetBits(pak, 32);

		if (gbChatVerbose)
			printf ("Received %s Cmd: %d, %s\n", bPrivateCmd ? "Private" : "Public", relayUser->uAccountID, msg);
		if (sbLogAllClientPackets)
			chatRelayLogClientPacketData(bPrivateCmd ? "CMD_PRIVATE" : "CMD_PUBLIC", msg);
		if (bPrivateCmd)
		{
			bool bUnknownCommand = false;
			ChatRelayParsePrivateCommand(msg, iFlags, relayUser, 0, &bUnknownCommand, eHow, &structList);
			if (bUnknownCommand)
				pktSetErrorOccurred(pak, "Unknown private chat relay command.");
			else
				bSuccess = true;
		}
		else
			bSuccess = ChatRelayParsePublicCommand(msg, iFlags, relayUser, NULL, 0, eHow, &structList);
	}
	cmdClearStructList(&structList);
	return bSuccess;
}

// Client handlers
static void chatRelayHandlePacket(Packet* pak, int cmd, NetLink* link, void *user_data)
{
	U32 uAccountID;
	ChatRelayUser *relayUser = (ChatRelayUser*) linkGetUserData(link);

	if (cmd != TOSERVER_GAME_MSG)
		return;
	cmd = pktGetU32(pak);

	if (!sbChatServerOnline)
	{
		if (relayUser && (cmd == CHATRELAY_CMD_PUBLIC || cmd == CHATRELAY_CMD_PRIVATE))
		{
			const char *msg = langTranslateMessageKey(relayUser->eLanguage, "Chat_Offline");
			crNotifySend(relayUser, kNotifyType_Failed, msg);
		}
		return;
	}

	uAccountID = pktGetU32(pak);
	switch (cmd)
	{
	case CHATRELAY_CMD_PUBLIC:
		PERFINFO_AUTO_START("CHATRELAY_CMD_PUBLIC", 1);
		{
			if (!chatRelayRunCommand(pak, link, relayUser, false) && gbChatVerbose)
				printf("Bad command from %d.\n", uAccountID);
		}
		PERFINFO_AUTO_STOP();
	xcase CHATRELAY_CMD_PRIVATE:
		PERFINFO_AUTO_START("CHATRELAY_CMD_PRIVATE", 1);
		{
			if (!chatRelayRunCommand(pak, link, relayUser, true) && gbChatVerbose)
				printf("Bad command from %d.\n", uAccountID);
		}
		PERFINFO_AUTO_STOP();
	xcase CHATRELAY_AUTHENTICATE:
		PERFINFO_AUTO_START("CHATRELAY_AUTHENTICATE", 1);
		{
			U32 uSecret= pktGetU32(pak);
			U32 uSource = pktGetU32(pak);
			ChatRelayAuthStruct *data = malloc(sizeof(ChatRelayAuthStruct));
			if (gbChatVerbose)
				printf("Auth request from %d - secret %d\n", uAccountID, uSecret);
			if (sbLogAllClientPackets)
				chatRelayLogClientPacketData("AUTHENTICATE", "");
			data->uAccountID = uAccountID;
			data->uLinkID = linkID(link);
			// Add user (un-authed) and to link data
			chatRelayAddUser(data->uAccountID, uSource, link);
			RemoteCommand_crManager_AuthenticateUser(objCreateManagedReturnVal(chatRelay_AuthenticateUserCB, data), 
				GLOBALTYPE_CHATSERVER, 0, GetAppGlobalID(), uAccountID, uSecret);
		}
		PERFINFO_AUTO_STOP();
	xcase CHATRELAY_FAKE_AUTHENTICATE:
		if (gbDebugMode)
		{
			PERFINFO_AUTO_START("CHATRELAY_FAKE_AUTHENTICATE", 1);
			{
				U32 uSecret= pktGetU32(pak);
				U32 uSource = pktGetU32(pak); // these are ignored
				ChatRelayAuthStruct *data = malloc(sizeof(ChatRelayAuthStruct));
				if (sbLogAllClientPackets)
					chatRelayLogClientPacketData("FAKE_AUTHENTICATE", "");
				data->uAccountID = uAccountID;
				data->uLinkID = linkID(link);
				chatRelayAddUser(data->uAccountID, CHAT_CONFIG_SOURCE_PC, link);
				RemoteCommand_crManager_FakeAuthenticateUser(objCreateManagedReturnVal(chatRelay_AuthenticateUserCB, data), 
					GLOBALTYPE_CHATSERVER, 0, GetAppGlobalID(), uAccountID, uSecret);
			}
			PERFINFO_AUTO_STOP();
		}
	xdefault:
		PERFINFO_AUTO_START("bad cmd", 1);
		Errorf("Invalid Client msg received: %d", cmd);
		pktSetErrorOccurred(pak, "Invalid client msg received. See errorf.");
		PERFINFO_AUTO_STOP();
	}
}
static void chatRelayClientConnect(NetLink* link, void *data)
{
	PERFINFO_AUTO_START_FUNC();

	if (isProductionMode())
		linkSetTimeout(link,CLIENT_LINK_TIMEOUT);
	else
		linkSetTimeout(link,CLIENT_LINK_DEV_TIMEOUT);

	if (maxSendPacket)
		linkSetMaxAllowedPacket(link,maxSendPacket);
	linkSetIsNotTrustworthy(link, true);

	PERFINFO_AUTO_STOP();
}
static void chatRelayClientDisconnect(NetLink *link, ChatRelayUser *userData)
{
	char *pDisconnectReason = NULL;
	PERFINFO_AUTO_START_FUNC();

	estrStackCreate(&pDisconnectReason);
	linkGetDisconnectReason(link, &pDisconnectReason);
	log_printf(LOG_CLIENTSERVERCOMM, "chatRelayDisconnectCallback hit for link %p. Reason: %s", link, pDisconnectReason);
	estrDestroy(&pDisconnectReason);

	if (userData)
	{
		if (gbChatVerbose)
			printf("User %d disconnected.\n", userData->uAccountID);
		// For disconnects after auth process has started (or finished)
		// Make sure linkID matches
		if (linkID(link) == userData->uLinkID)
			chatRelayRemoveUser(userData->uAccountID);
	}

	PERFINFO_AUTO_STOP();
}

// Chat Relay Manager Handlers
AUTO_COMMAND_REMOTE ACMD_IFDEF(CONTROLLER);
void chatRelay_ChatServerCrashOrClose(void)
{
	sbChatServerOnline = false;
	// Kicks all unauthenticated users since authentication must be restarted
	chatRelayRemoveUnauthenticatedUsers();
	printf("Chat Server crashed or closed.\n");
}

static void chatRelay_RegisterRelayCB(TransactionReturnVal *returnVal, void *data)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		RemoteCommandGroup *pChatCloseGroup = CreateEmptyRemoteCommandGroup();
		AddCommandToRemoteCommandGroup(pChatCloseGroup, GetAppGlobalType(), GetAppGlobalID(), false,
			"chatRelay_ChatServerCrashOrClose");
		RemoteCommand_AddThingToDoOnServerCrash(GLOBALTYPE_CONTROLLER, 0, GLOBALTYPE_CHATSERVER, 0, pChatCloseGroup);
		RemoteCommand_AddThingToDoOnServerClose(GLOBALTYPE_CONTROLLER, 0, GLOBALTYPE_CHATSERVER, 0, pChatCloseGroup);
		DestroyRemoteCommandGroup(pChatCloseGroup);
		
		sbChatServerOnline = true;
		printf("Chat Server connected.\n");
	}
}

static int *seaiReconnectList = NULL;
AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void chatRelay_RegisterRelayWithChatServer(void)
{
	PlayerInfoList list = {0};
	chatRelayGetUserIDList(&list.piAccountIDs, true);
	RemoteCommand_crManager_RegisterRelay(objCreateManagedReturnVal(chatRelay_RegisterRelayCB, NULL), 
		GLOBALTYPE_CHATSERVER, 0, GetAppGlobalID(), makeIpStr(getHostPublicIp()), siListenPort, &list);
	eaiClear(&seaiReconnectList);
	eaiCopy(&seaiReconnectList, &list.piAccountIDs);
	eaiDestroy(&list.piAccountIDs);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void chatRelay_GlobalChatReconnect(void)
{
	// Pull list of users and tell them to relogin (gated)
	eaiClear(&seaiReconnectList);
	chatRelayGetUserIDList(&seaiReconnectList, true);
}

static void chatRelayConnectToManager(TransactionReturnVal *returnVal, void *userData)
{
	Controller_ServerList *pChatServerList;
	enumTransactionOutcome eOutcome;

	eOutcome = RemoteCommandCheck_GetServerList(returnVal, &pChatServerList);

	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		EARRAY_FOREACH_BEGIN(pChatServerList->ppServers, i);
		{
			Controller_SingleServerInfo *serverInfo = pChatServerList->ppServers[i];
			if (strstri(serverInfo->stateString, "ready"))
			{
				chatRelay_RegisterRelayWithChatServer();
				return;
			}
		}
		EARRAY_FOREACH_END;
		ErrorOrAlert("CHATRELAY_MANAGERCONNECT", "No Shard Chat Server found ready.");
		StructDestroy(parse_Controller_ServerList, pChatServerList);
	}
	else if (eOutcome == TRANSACTION_OUTCOME_FAILURE)
	{
		ErrorOrAlert("CHATRELAY_MANAGERCONNECT", "Failure getting list of Shard Chat Servers from Controller.");
	}
}

void chatRelayStart(void)
{
	int result = 0, iPort = siListenPort;

	loadstart_printf("Connecting ChatServer to TransactionServer (%s)... ", gServerLibState.transactionServerHost);
	while (!InitObjectTransactionManager(GetAppGlobalType(),
		gServerLibState.containerID,
		gServerLibState.transactionServerHost,
		gServerLibState.transactionServerPort,
		gServerLibState.bUseMultiplexerForTransactions, NULL))
	{
		Sleep(1000);
	}
	if (!objLocalManager())
	{
		loadend_printf("failed.\n");
		svrExit(0);
		return;
	}
	loadend_printf("connected.\n");

	// Cycle through ports until an open one is found
	do
	{
		result = commListenBoth(commDefault(), 
			LINKTYPE_TOUNTRUSTED_5MEG, LINK_FORCE_FLUSH | LINK_NO_COMPRESS,
			iPort,
			chatRelayHandlePacket, chatRelayClientConnect, chatRelayClientDisconnect,
			0, &sPrivateListen, &sPublicListen);
		if (!result)
		{
			iPort = iPort + 1;
			if (iPort > MAX_CHATRELAY_PORT)
				iPort = STARTING_CHATRELAY_PORT;
		}
	} while (!result && iPort != siListenPort);

	// Asserts if no open ports were found
	if (getHostLocalIp() != getHostPublicIp())
		assertmsg(sPrivateListen && sPublicListen, "No open ports found for Chat Relay [public]");
	else
		assertmsg(sPrivateListen, "No open ports found for Chat Relay [dev]");

	siListenPort = iPort;
	initChatBlacklist();

	RemoteCommand_GetServerList(objCreateManagedReturnVal(chatRelayConnectToManager, NULL), GLOBALTYPE_CONTROLLER, 0, 
		GLOBALTYPE_CHATSERVER);
}

#define GCS_RECONNECT_BATCH_SIZE (200)
#define GCS_RECONNECT_BATCH_PAUSE (1)
void chatRelayCommsMonitor(void)
{
	static U32 suLastReconnectBatchTime = 0;
	int iSize = eaiSize(&seaiReconnectList);
	commMonitor(commDefault());

	if (iSize)
	{
		U32 uTime = timeSecondsSince2000();
		if (uTime - suLastReconnectBatchTime > GCS_RECONNECT_BATCH_PAUSE)
		{
			int i, iCount = min(iSize,GCS_RECONNECT_BATCH_SIZE);
			for (i=0; i<iCount; i++)
				GClientCmd_ChatAuthSuccess(seaiReconnectList[i]);
			ea32RemoveRange(&seaiReconnectList, 0, iCount);
			if (eaiSize(&seaiReconnectList) == 0)
				eaiDestroy(&seaiReconnectList);
			suLastReconnectBatchTime = uTime;
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void blacklist_RelayUpdate(const ChatBlacklist *blacklist, ChatBlacklistUpdate eUpdateType)
{
	blacklist_HandleUpdate(blacklist, eUpdateType);
}