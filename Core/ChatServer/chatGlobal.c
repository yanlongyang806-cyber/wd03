#include "chatdb.h"
#include "chatdb_h_ast.h"
#include "chatGlobal.h"
#include "chatShardConfig.h"
#include "chatLocal.h"
#include "chatCommonStructs_h_ast.h"
#include "chatGlobal_h_ast.h"
#include "chatGlobal_c_ast.h"
#include "chatMail.h"
#include "chatUtilities.h"
#include "users.h"
#include "channels.h"
#include "msgsend.h"
#include "ChatServer/chatBlacklist.h"
#include "ChatServer/chatShared.h"
#include "ChatServer/chatShard_Shared.h"

#include "accountnet.h"
#include "file.h"
#include "logging.h"
#include "mathutil.h"
#include "objContainer.h"
#include "ResourceInfo.h"
#include "ServerLib.h"
#include "ShardCommon.h"
#include "sock.h"
#include "StashTable.h"
#include "StringCache.h"
#include "timing.h"
#include "utilitiesLib.h"

#include "AutoGen/chatShared_h_ast.h"
#include "AutoGen/Controller_autogen_RemoteFuncs.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
//#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"
#include "AutoGen/ServerLib_autotransactions_autogen_wrappers.h"

#ifdef USE_CHATRELAY
#include "chatRelay/chatRelayManager.h"
#endif

#define LOCAL_CHAT_ID_START 2

extern bool gbTestingMetrics;
extern TimingHistory *gChatHistory;
extern bool gbChatVerbose;
extern ShardChatServerConfig gShardChatServerConfig;

bool gbGlobalChatResponse = false;

// Local Chat Server defs
extern ParseTable parse_PlayerInfoList[];
#define TYPE_parse_PlayerInfoList PlayerInfoList
extern ParseTable parse_ChatUser[];
#define TYPE_parse_ChatUser ChatUser
extern ParseTable parse_ChatChannel[];
#define TYPE_parse_ChatChannel ChatChannel

static bool spGlobalLinkActive = false; // If it was recently told Global Chat is online or not
static NetLink *spGlobalLink = NULL; // Link TO the Global Chat server from the Local Chat Servers
static bool sbSentLocalChatInfo = false;
static bool sbVersionMismatch = false; // TRUE if this initially connected to a GCS with an older minor version

AUTO_STRUCT;
typedef struct LocalChatCommandWait
{
	U32 userID;
	U32 channelID;
	char *userHandle;
	char *channelName;

	char *commandString;
	U32 uStartTime; // Time when it was added to queue
} LocalChatCommandWait;
static LocalChatCommandWait **sppLocalCommandWaitQueue = NULL;
#define LOCAL_COMMAND_WAIT_MAX_TIME 10 // max 10 seconds wait

AUTO_COMMAND ACMD_CATEGORY(ChatServer) ACMD_ACCESSLEVEL(9);
void RegisterShardWithGlobalChatServer(void)
{
	if (!sbSentLocalChatInfo && linkConnected(spGlobalLink))
	{
		Packet *pkt;
		if (ShardCommon_GetClusterName())
		{
			ChatRegisterShardData shardData = {0};
			char *shardString = NULL;

			pkt = pktCreate(spGlobalLink, TO_GLOBALCHATSERVER_REGISTER_SHARD_EX);
			shardData.pShardName = GetShardNameFromShardInfoString();
			shardData.pShardCategory = GetShardCategoryFromShardInfoString();
			shardData.pProduct = GetProductName();
			shardData.pClusterName = ShardCommon_GetClusterName();
			ParserWriteText(&shardString, parse_ChatRegisterShardData, &shardData, 0, 0, 0);
			pktSendString(pkt, shardString);
			estrDestroy(&shardString);
		}
		else
		{
			pkt = pktCreate(spGlobalLink, TO_GLOBALCHATSERVER_REGISTER_SHARD);
			pktSendString(pkt, GetShardNameFromShardInfoString());
			pktSendString(pkt, GetShardCategoryFromShardInfoString());
			pktSendString(pkt, GetProductName());
		}
		pktSend(&pkt);
		sbSentLocalChatInfo = true;
#ifdef USE_CHATRELAY
		crManager_InformRelayGCSReconnect();
#endif
	}
}

void localChatAddCommmandWaitQueue (U32 userID, const char *userHandle, U32 channelID, const char *channelName, const char * commandString)
{
	LocalChatCommandWait *queue;
	if (!userID && (!userHandle || !*userHandle) && !channelID && (!channelName|| !*channelName))
		return;
	queue = StructCreate(parse_LocalChatCommandWait);
	queue->userID = userID;
	queue->userHandle  = StructAllocString(userHandle);
	queue->channelID = channelID;
	queue->channelName = StructAllocString(channelName);
	queue->commandString = strdup(commandString);
	queue->uStartTime = timeSecondsSince2000();

	eaPush(&sppLocalCommandWaitQueue, queue);
}

static void executeUserCommandWait (const U32 userID, const char *userHandle)
{
	int i, j;
	U32 uTime = timeSecondsSince2000();
	for (i=eaSize(&sppLocalCommandWaitQueue)-1; i>=0; i--)
	{
		LocalChatCommandWait *queue = sppLocalCommandWaitQueue[i];
		if ((userID && userID == queue->userID) || 
			(userHandle && queue->userHandle && stricmp(userHandle, queue->userHandle) == 0))
		{
			CmdContext context = {0};

			gbGlobalChatResponse = true;
			ChatServerCmdParseFunc(queue->commandString, NULL, &context);
			gbGlobalChatResponse = false;
			StructDestroy(parse_LocalChatCommandWait, queue);
			eaRemove(&sppLocalCommandWaitQueue, i);
			return;
		}
		if (queue->uStartTime + LOCAL_COMMAND_WAIT_MAX_TIME > uTime)
		{
			break;
		}
	}
	if (i>=0)
	{
		for (j=i; j>=0; j--)
		{
			StructDestroy(parse_LocalChatCommandWait, sppLocalCommandWaitQueue[j]);
		}
		eaRemoveRange(&sppLocalCommandWaitQueue, 0, i+1);
	}
}

// Global Chat Server defs
AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "uID, ShardName, ShardCategoryName, ProductName");
typedef struct GlobalChatLinkStruct
{
	U32 uID;
	NetLink *localChatLink; NO_AST
	ContainerID *ppOnlineUserIDs;

	char *pShardName; AST(ESTRING)
	char *pShardCategoryName; AST(ESTRING)
	char *pProductName; AST(ESTRING)

	StashTable shardGuildStash; NO_AST // Table of ChatGuild's 
	U32 uConnectedTime;
} GlobalChatLinkStruct;

static StashTable stGlobalChatLinks = NULL; // key = ID, value = GlobalChatLinkStruct pointer
static StashTable stGlobalChatReverseLink = NULL; // key = link pointer, value = ID
static StashTable stGlobalChatMonitorTable = NULL; // key = Shard name, for server monitor sorting

ChatGuild *GlobalChatKnowsGuild (U32 uChatServerID, U32 uGuildID)
{
	GlobalChatLinkStruct *linkStruct = NULL;
	stashIntFindPointer(stGlobalChatLinks, uChatServerID, &linkStruct);
	if (linkStruct)
	{
		ChatGuild *pGuild = NULL;
		if (stashIntFindPointer(linkStruct->shardGuildStash, uGuildID, &pGuild))
			return pGuild;
	}
	return NULL;
}

void GlobalChatServer_UpdateGuildName (U32 uChatServerID, U32 uGuildID, char *pchGuildName)
{
	GlobalChatLinkStruct *linkStruct = NULL;
	stashIntFindPointer(stGlobalChatLinks, uChatServerID, &linkStruct);

	if (linkStruct)
	{
		ChatGuild *pGuild = NULL;
		stashIntFindPointer(linkStruct->shardGuildStash, uGuildID, &pGuild);
		if (pGuild)
		{
			pGuild->pchName = allocAddString(pchGuildName); // Update the guild name
		}
		else
		{
			pGuild = StructCreate(parse_ChatGuild);
			pGuild->iGuildID = uGuildID;
			pGuild->pchName = allocAddString(pchGuildName);
			stashIntAddPointer(linkStruct->shardGuildStash, pGuild->iGuildID, pGuild, false);
		}
	}
}

void GlobalChatServer_InitializeGuildNameBatch (U32 uChatServerID, ChatGuild **ppGuilds)
{
	GlobalChatLinkStruct *linkStruct = NULL;
	stashIntFindPointer(stGlobalChatLinks, uChatServerID, &linkStruct);
	if (linkStruct)
	{
		int i,size;
		size = eaSize(&ppGuilds);
		for (i=0; i<size; i++)
		{
			ChatGuild *pGuild = NULL;
			stashIntFindPointer(linkStruct->shardGuildStash, ppGuilds[i]->iGuildID, &pGuild);
			if (pGuild)
			{
				pGuild->pchName = allocAddString(ppGuilds[i]->pchName); // Update the guild name
			}
			else
			{
				pGuild = StructClone(parse_ChatGuild, ppGuilds[i]);
				if (pGuild)
					stashIntAddPointer(linkStruct->shardGuildStash, pGuild->iGuildID, pGuild, false);
			}
		}
	}
}

static void GlobalChatConnect(NetLink* link,void *user_data)
{
	struct in_addr ina = {0};
	char *ipString;
	ina.S_un.S_addr = linkGetIp(link);
	ipString = inet_ntoa(ina);

	printf("Local Chat Server: Connected to Global Chat Server at IP [%s].\n", ipString);
	if (!spGlobalLinkActive)
	{
		RemoteCommand_BroadcastGlobalChatServerOnline(GLOBALTYPE_CONTROLLER, 0, true);
		spGlobalLinkActive = true;
	}
}

static void GlobalChatDisconnect(NetLink* link,void *user_data)
{
	printf("Local Chat Server: Disconnected from Global Chat Server.\n");
	RemoteCommand_BroadcastGlobalChatServerOnline(GLOBALTYPE_CONTROLLER, 0, false);
	ChatServerMail_GCSDisconnect();
	linkRemove(&spGlobalLink);
	spGlobalLinkActive = false;
	sbSentLocalChatInfo = false;
}

__forceinline static void ShardChatAddNewUser(ChatUser *pNewUserData)
{
	GADRef *r = StructCreate(parse_GADRef);
	char buf[50];

	sprintf(buf, "%d", pNewUserData->id);
	SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GAMEACCOUNTDATA), buf, r->hGAD);
	stashIntAddPointer(chat_db.gad_by_id, pNewUserData->id, r, true);
	stashAddPointer(chat_db.user_names, pNewUserData->handle, pNewUserData, true);
	objAddExistingContainerToRepository(GLOBALTYPE_CHATUSER, pNewUserData->id, pNewUserData);
#ifdef USE_CHATRELAY
	crManager_ReceiveChatUser(userFindByContainerId(pNewUserData->id));
#endif
	executeUserCommandWait(pNewUserData->id, pNewUserData->handle);
}

__forceinline static void ShardChatUpdateUser(ChatUser *user, ChatUser *pNewUserData)
{
	bool bHandleChanged = stricmp(user->handle, pNewUserData->handle) != 0;
	// Save this data
	bool bAutoAFK = user->online_status & USERSTATUS_AUTOAFK;
	
	if (bHandleChanged)
		stashRemovePointer(chat_db.user_names, user->handle, NULL);	
	StructCopy(parse_ChatUser, pNewUserData, user, STRUCTCOPYFLAG_DONT_COPY_NO_ASTS, 0, TOK_NO_NETSEND);
	if (bHandleChanged)
		stashAddPointer(chat_db.user_names, user->handle, user, true);

	// Reassigning fields that were saved or have special values
	if (bAutoAFK)
		user->online_status |= USERSTATUS_AUTOAFK;
}

extern ChatDb chat_db;
static void GlobalChatHandleMessage(Packet *pkt,int cmd,NetLink* link,void *user_data)
{
	switch (cmd)
	{
	xcase FROM_GLOBALCHATSERVER_COMMAND:
	{
		U32 retVal = pktGetU32(pkt);
		char *pCommandString = pktGetStringTemp(pkt);

		if (gbChatVerbose)
			printf ("Local: %s\n", pCommandString);
		if (retVal == CHATRETURN_NONE)
		{
			CmdContext context = {0};
			gbGlobalChatResponse = true;
			ChatServerCmdParseFunc(pCommandString, NULL, &context);
			gbGlobalChatResponse = false;
		}
	}
	xcase FROM_GLOBALCHATSERVER_MESSAGE:
	{
		U32 uAccountID = pktGetU32(pkt);
		int eChatType = pktGetU32(pkt);
		const char *channel = pktGetStringTemp(pkt);
		const char *message = pktGetStringTemp(pkt);

		ChatUser *user = userFindByContainerId(uAccountID);

		if (message && user)
			sendChatSystemStaticMsg(user, eChatType, channel, message);
	}
	xcase FROM_GLOBALCHATSERVER_ALERT:
	{
		U32 uAccountID = pktGetU32(pkt);
		const char *title = pktGetStringTemp(pkt);
		const char *message = pktGetStringTemp(pkt);

		ChatUser *user = userFindByContainerId(uAccountID);

		if (message && user)
			sendChatSystemAlert(user, title, message);
	}
	xcase FROM_GLOBALCHATSERVER_ACCOUNT_LIST: // These two are handled identically
	case FROM_GLOBALCHATSERVER_CHANNEL_LIST:
		{
			char *dataString = pktGetStringTemp(pkt);
			ChatServerData data = {0};

			if (gbChatVerbose)
				printf ("++ %s Update\n", cmd == FROM_GLOBALCHATSERVER_CHANNEL_LIST ? "Channel" : "Account");
			if (ParserReadText(dataString, parse_ChatServerData, &data, PARSER_IGNORE_ALL_UNKNOWN | PARSER_NOERROR_ONIGNORE))
			{
				int i, size;
				size = eaSize(&data.ppUsers);
				for (i=0; i<size; i++)
				{
					ChatUser *pNewUserData = data.ppUsers[i];
					ChatUser *user = userFindByContainerId(pNewUserData->id);
					if (user)
					{
						if (devassert(stricmp(user->accountName, pNewUserData->accountName) == 0))
						{
							ShardChatUpdateUser(user, pNewUserData);
							// Cleanup
							StructDestroy(parse_ChatUser, pNewUserData);
							data.ppUsers[i] = NULL;
#ifdef USE_CHATRELAY
							crManager_ReceiveChatUser(user);
#endif
						}
						else
						{
							userDelete(user->id);
							ShardChatAddNewUser(pNewUserData);
						}
					}
					else
					{
						ShardChatAddNewUser(pNewUserData);
					}
				}

				size = eaSize(&data.ppChannels);
				for (i=0; i<size; i++)
				{
					ChatChannel *channel = channelFindByID(data.ppChannels[i]->uKey);
					if (channel)
					{
						StructCopyAll(parse_ChatChannel, data.ppChannels[i], channel);
						StructDestroy(parse_ChatChannel, data.ppChannels[i]);
						data.ppChannels[i] = NULL;
					}
					else
					{
						stashAddPointer(chat_db.channel_names, data.ppChannels[i]->name, data.ppChannels[i], true);
						objAddExistingContainerToRepository(GLOBALTYPE_CHATCHANNEL, data.ppChannels[i]->uKey, data.ppChannels[i]);
					}
				}
				eaDestroy(&data.ppUsers);
				eaDestroy(&data.ppChannels);
			}
		}
	xcase FROM_GLOBALCHATSERVER_CHANNEL_DELETE:
		{
			U32 uID = pktGetU32(pkt);
			ChatChannel *channel = channelFindByID(uID);
			if (channel)
			{
				ChatChannelInfo updateInfo = {0};
				int i, size = eaiSize(&channel->members);
				estrCopy2(&updateInfo.pName, channel->name);
				for (i=0; i<size; i++)
				{
					ChatUser *user = userFindByContainerId(channel->members[i]);
					if (userOnline(user))
					{
						ChannelSendUpdateToUser(user, &updateInfo, CHANNELUPDATE_REMOVE);
					}
				}
				size = eaiSize(&channel->invites);
				for (i=0; i<size; i++)
				{
					ChatUser *user = userFindByContainerId(channel->invites[i]);
					if (userOnline(user))
					{
						ChannelSendUpdateToUser(user, &updateInfo, CHANNELUPDATE_REMOVE);
					}
				}
				StructDeInit(parse_ChatChannelInfo, &updateInfo);

				stashRemovePointer(chat_db.channel_names, channel->name, NULL);
				objRemoveContainerFromRepository(GLOBALTYPE_CHATCHANNEL, uID);
			}
		}
	xcase FROM_GLOBALCHATSERVER_SHARDINFO_REQUEST:
		{
			RegisterShardWithGlobalChatServer();
		}
	xcase FROM_GLOBALCHATSERVER_VERSIONINFO:
		{
			U32 uMajorRevision = pktGetU32(pkt);
			U32 uMinorRevision = pktGetU32(pkt);
			if (uMajorRevision != CHATSERVER_MAJOR_REVISION)
			{
				AssertOrAlert("CHAT_MAJOR_VERSION_MISMATCH", "Chat Major Version Mismatch - GCS v.%d, Shard Chat v.%d", 
					uMajorRevision, CHATSERVER_MAJOR_REVISION);
				sbVersionMismatch = true;
				linkRemove_wReason(&link, "Major version mismatch");
			}
			else if (uMinorRevision < CHATSERVER_MINOR_REVISION)
			{
				AssertOrAlert("CHAT_MINOR_VERSION_MISMATCH", "Chat Minor Version Mismatch - GCS v.%d, Shard Chat v.%d", 
					uMinorRevision, CHATSERVER_MINOR_REVISION);
				sbVersionMismatch = true;
				linkRemove_wReason(&link, "Minor version mismatch");
			}
			else
				sbVersionMismatch = false;
			printf ("Version Received: Major %d, Minor %d\n", uMajorRevision, uMinorRevision);
		}
	xdefault:
		log_printf(LOG_CHATSERVER, "[Error: Unknown packet received]");
		break;
	}
	if(gbTestingMetrics)
		timingHistoryPush(gChatHistory);
}

///////////////////////////////////////////
// Global Chat Server Functions
///////////////////////////////////////////

Language userGetLastLanguage(ChatUser *user)
{
	if (user)
	{
		Language eLanguage = user->eLastLanguage;
		if (user->pPlayerInfo)
			eLanguage = user->pPlayerInfo->eLanguage;
		return eLanguage;
	}
	return LANGUAGE_DEFAULT;
}

void translateMessageForUser (char **estr, SA_PARAM_NN_VALID ChatUser *user, const char *messageKey, ...)
{
	Language eLanguage = user->eLastLanguage;
	va_list	ap;
	if (user->pPlayerInfo)
		eLanguage = user->pPlayerInfo->eLanguage;
	va_start(ap, messageKey);	
	langFormatMessageKeyV(eLanguage, estr, messageKey, ap);
	va_end(ap);
}

void sendTranslatedMessageToUser(SA_PARAM_NN_VALID ChatUser *user, int eChatType, const char *channel_name, const char *messageKey, ...)
{
	Language eLanguage = user->eLastLanguage;
	va_list	ap;
	if (user->pPlayerInfo)
		eLanguage = user->pPlayerInfo->eLanguage;

	va_start(ap, messageKey);
	sendChatSystemTranslatedMsgV(NULL, user, eChatType, channel_name, eLanguage, messageKey, ap);
	va_end(ap);
}

void broadcastTranslatedMessageToUser(SA_PARAM_NN_VALID ChatUser *user, int eChatType, const char *channel_name, const char *messageKey, ...)
{
	Language eLanguage = user->eLastLanguage;
	va_list	ap;
	if (user->pPlayerInfo)
		eLanguage = user->pPlayerInfo->eLanguage;
	va_start(ap, messageKey);
	sendChatSystemTranslatedMsgV(NULL, user, eChatType, channel_name, eLanguage, messageKey, ap);
	va_end(ap);
}

void sendMessageToUser(SA_PARAM_NN_VALID ChatUser *user, int eChatType, const char *channel_name, const char *fmt, ...)
{
	char buffer[1024];
	va_list	ap;
	va_start(ap, fmt);
	vsprintf(buffer,fmt,ap);
	va_end(ap);
	sendMessageToUserStatic(user, eChatType, channel_name, buffer);
}

void sendMessageToUserStatic(SA_PARAM_NN_VALID ChatUser *user, int eChatType, const char *channel_name, const char *msg)
{
	sendChatSystemStaticMsg(user, eChatType, channel_name, msg);
}

///////////////////////////////////////////
// Local Chat Server Functions
///////////////////////////////////////////

#define MAX_COMMANDS_PER_TICK 200
#define MAX_COMMAND_QUEUE_SIZE 100000

// Commands that need to be sent to the Global Chat Server
AUTO_STRUCT;
typedef struct LocalChatCommand
{
	U32 uTime; // time this command was queued
	char *commandString; AST(ESTRING)

	U32 uCommandFlag; // for special commands
} LocalChatCommand;

#define CHATCOMMAND_ACCOUNT_LIST 0x01
#define CHATCOMMAND_CHANNEL_LIST 0x02

static LocalChatCommand **sppLocalChatCommandQueue = NULL;

static void addCommandToQueue(const char *commandString, U32 uCommandFlag)
{
	if (eaSize(&sppLocalChatCommandQueue) < MAX_COMMAND_QUEUE_SIZE)
	{
		LocalChatCommand *cmd = StructCreate(parse_LocalChatCommand);

		if (commandString)
			estrCopy2(&cmd->commandString, commandString);
		cmd->uCommandFlag = uCommandFlag; 
		cmd->uTime = timeSecondsSince2000();
		eaPush(&sppLocalChatCommandQueue, cmd);
	} // else this is a silent failure
}

AUTO_COMMAND_REMOTE;
void ReconnectToGlobalChat(void)
{
	if (!linkConnected(spGlobalLink))
	{
		printf ("Reconnecting to Global Chat Server...\n\n");
		if (spGlobalLink)
			linkRemove(&spGlobalLink); // clear the link first so we don't have a phantom link
		spGlobalLink = commConnect(getCommToGlobalChat(), LINKTYPE_UNSPEC, LINK_COMPRESS|LINK_FORCE_FLUSH, getGlobalChatServer(), DEFAULT_GLOBAL_CHATSERVER_PORT,
			GlobalChatHandleMessage, GlobalChatConnect, GlobalChatDisconnect, 0);
		sbSentLocalChatInfo = false;

		// Clear the databases?
		getChannelListFromGlobalChatServer();
	}
}

// TODO how long should commands be in queue?
void ChatServerLocalTick(void)
{
	static U32 uLastConnectTryTime = 0;
	U32 uTime = timeSecondsSince2000();
	if (!spGlobalLink)
	{
		U32 uReconnectTime = uLastConnectTryTime + 
			(sbVersionMismatch ? CHATSERVER_VERSION_RECONNECT_TIME : CHATSERVER_RECONNECT_TIME);
		if (!uLastConnectTryTime || uReconnectTime < uTime)
			ReconnectToGlobalChat();
		else
			return;
		uLastConnectTryTime = uTime;
	}

	if (linkConnected(spGlobalLink))
	{
		int size = eaSize(&sppLocalChatCommandQueue);
		int count = 0;

		if (!sbSentLocalChatInfo)
		{
			RegisterShardWithGlobalChatServer();
		}

		while (count < MAX_COMMANDS_PER_TICK && size > 0)
		{
			Packet *pkt;
			if (sppLocalChatCommandQueue[count]->uCommandFlag == CHATCOMMAND_ACCOUNT_LIST)
			{
				pkt = pktCreate(spGlobalLink, TO_GLOBALCHATSERVER_ACCOUNT_REQUEST);
				pktSendString(pkt, sppLocalChatCommandQueue[count]->commandString);
			}
			else if (sppLocalChatCommandQueue[count]->uCommandFlag == CHATCOMMAND_CHANNEL_LIST)
			{
				pkt = pktCreate(spGlobalLink, TO_GLOBALCHATSERVER_CHANNEL_REQUEST);
			}
			else
			{
				pkt = pktCreate(spGlobalLink, TO_GLOBALCHATSERVER_COMMAND);
				pktSendString(pkt, sppLocalChatCommandQueue[count]->commandString);
			}
			pktSend(&pkt);
			StructDestroy(parse_LocalChatCommand, sppLocalChatCommandQueue[count]);
			count++;
			size--;
		}
		if (count) eaRemoveRange(&sppLocalChatCommandQueue, 0, count);
	}
	else if (spGlobalLink && uLastConnectTryTime + CHATSERVER_CONNECT_TIMEOUT < uTime)
	{
		linkRemove(&spGlobalLink);
	}
}

void getAccountListFromGlobalChatServer (U32 *piAccountIDList)
{
	char *tempString = NULL;
	PlayerInfoList list = {0};

	list.piAccountIDs = piAccountIDList;
	ParserWriteText(&tempString, parse_PlayerInfoList, &list, 0, 0, 0);
	addCommandToQueue(tempString, CHATCOMMAND_ACCOUNT_LIST);
	estrDestroy(&tempString);
}

void getChannelListFromGlobalChatServer (void)
{
	if (eaSize(&sppLocalChatCommandQueue) == 0 || sppLocalChatCommandQueue[0]->uCommandFlag != CHATCOMMAND_CHANNEL_LIST)
	{
		LocalChatCommand *cmd = StructCreate(parse_LocalChatCommand);
		cmd->uCommandFlag = CHATCOMMAND_CHANNEL_LIST;
		cmd->uTime = timeSecondsSince2000();
		eaInsert(&sppLocalChatCommandQueue, cmd, 0); // insert as the first command
	}
}

void sendCommandToGlobalChatServer(const char *pCommandString)
{
	if (!sendCommandToGlobalChat(spGlobalLink, pCommandString))
		addCommandToQueue(pCommandString, 0);
}

void sendCmdAndParamsToGlobalChat(const char *pCommand, const char *pParamFormat, ... )
{
	char *fullCommand = NULL;
	char *pParameters = NULL;
	if (pParamFormat)
		estrGetVarArgs(&pParameters, pParamFormat);
	if (pParameters)
		estrPrintf(&fullCommand, "%s %s", pCommand, pParameters);
	else
		estrPrintf(&fullCommand, "%s", pCommand);

	if (!sendCommandToGlobalChat(spGlobalLink, fullCommand))
		addCommandToQueue(fullCommand, 0);
	estrDestroy(&fullCommand);
	estrDestroy(&pParameters);
}

bool ChatServerIsConnectedToGlobal(void)
{
	return linkConnected(spGlobalLink);
}

///////////////////////////////////////////
// Command Parse Functions
///////////////////////////////////////////

extern CmdList gGlobalCmdList;
extern CmdList gRemoteCmdList;
static CmdContext *pCmdContext = NULL;

// Actually execute a remote command
int ChatServerRemoteCommandCB(TransactionCommand *command)
{
	CmdContext context = {0};

	pCmdContext = &context;
	if (command->bCheckingIfPossible)
	{
		return 1;
	}

	context.output_msg = command->returnString;
	context.access_level = 9;
	context.multi_line = true;
	context.clientType = command->objectType;
	context.clientID = command->objectID;

	if (!cmdParseAndExecute(&gRemoteCmdList,command->stringData,&context))
	{
		log_printf(LOG_REMOTECOMMANDS, "Remote command \"%s\" failed with output message \"%s\"", 
			command->stringData, (context.output_msg && *context.output_msg) ? *context.output_msg : "(no message)");
		return 0;
	}
	pCmdContext = NULL;
	return 1;
}

// Actually execute a slow remote command
int ChatServerSlowRemoteCommandCB(TransactionCommand *command)
{
	int result = 0;
	CmdContext context = {0};

	pCmdContext = &context;
	if (command->bCheckingIfPossible)
	{
		return 1;
	}

	context.output_msg = command->returnString;
	context.access_level = 9;
	context.iSlowCommandID = command->slowTransactionID;
	context.multi_line = true;
	context.clientType = command->objectType;
	context.clientID = command->objectID;

	command->commandState = TRANSTATE_WAITING;

	if (!cmdParseAndExecute(&gSlowRemoteCmdList,command->stringData,&context))
	{
		return 0;
	}
	pCmdContext = NULL;
	return 1;
}

// This is a carbon-copy of defaultCmdParseFunc that only checks the remote command list for the command
int ChatServerCmdParseFunc(const char *cmdOrig, char **ppReturnString, CmdContext *pContext)
{
	char *pReturnStringInternal = NULL;
	char *cmd, *cmdStart = NULL;
	int retVal = -1;

	pCmdContext = pContext;

	if (ppReturnString)
	{
		pContext->output_msg = ppReturnString;
	}
	else
	{
		estrStackCreate(&pReturnStringInternal);
		pContext->output_msg = &pReturnStringInternal;
	}

	pContext->access_level = 10;
	pContext->flags |= CMD_CONTEXT_FLAG_IGNORE_UNKNOWN_FIELDS;
	pContext->eHowCalled = CMD_CONTEXT_HOWCALLED_COMMANDLINE;

	estrStackCreate(&cmdStart);
	estrCopy2(&cmdStart,cmdOrig);
	cmd = cmdStart;

	if (cmd)
	{
		estrClear(pContext->output_msg);
		
		if (!cmdParseAndExecute(&gGlobalCmdList, cmd, pContext))
		{
			estrClear(pContext->output_msg);
			if (!cmdParseAndExecute(&gRemoteCmdList, cmd, pContext))
			{
				log_printf(LOG_CHATSERVER, "[Error: Unknown Command] %s", cmd);
				Errorf("Unknown Command: %s", cmd);
				estrDestroy(&pReturnStringInternal);
				estrDestroy(&cmdStart);
				if (gbChatVerbose)
					cmdPrintPrettyOutput(pContext, printf);
				return CHATRETURN_UNKNOWN_COMMAND; // did not find command;
			}
		}

		retVal = pContext->return_val.intval;
		if (gbChatVerbose) {
			cmdPrintPrettyOutput(pContext, printf);
		}
	}

	estrDestroy(&pReturnStringInternal);
	estrDestroy(&cmdStart);
	pCmdContext = NULL;
	return retVal;
}

void initLocalChatServer(void)
{
	int i;
	char* classname = 0;
	ContainerStore *store = objFindContainerStoreFromType(GLOBALTYPE_CHATUSER);

	if(store)
	{
		objLockContainerStore_ReadOnly(store);
		for (i = eaSize(&store->containers)-1; i>=0; i--)
		{
			Container *object = store->containers[i];
			objChangeContainerState(object, CONTAINERSTATE_OWNED, GetAppGlobalType(), objServerID());
		}
		objUnlockContainerStore_ReadOnly(store);
	}
	store = objFindContainerStoreFromType(GLOBALTYPE_CHATCHANNEL);
	if(store)
	{
		objLockContainerStore_ReadOnly(store);
		for (i = eaSize(&store->containers)-1; i>=0; i--)
		{
			Container *object = store->containers[i];
			objChangeContainerState(object, CONTAINERSTATE_OWNED, GetAppGlobalType(), objServerID());
		}
		objUnlockContainerStore_ReadOnly(store);
	}
	SetRemoteCommandCB(ChatServerRemoteCommandCB);
	SetSlowRemoteCommandCB(ChatServerSlowRemoteCommandCB);
	// Get channel list from Global Chat
	RemoteCommand_BroadcastGlobalChatServerOnline(GLOBALTYPE_CONTROLLER, 0, false); // initialize Global Chat to offline until it connects
	getChannelListFromGlobalChatServer();

	// Inform existing Game Servers that Chat Server is now online - Game Servers will send player lists to chat server
	//RemoteCommand_BroadcastChatServerOnline(GLOBALTYPE_CONTROLLER, 0);
	resRegisterDictionaryForStashTable("Full Channel List", RESCATEGORY_OTHER, 0, chat_db.channel_names, parse_ChatChannel);
	addChatMapCacheToMonitor();
	InitShardChatServerConfig();
	initChatBlacklist();

#ifdef USE_CHATRELAY
	initChatRelayManager();
#endif
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void RequestGlobalChatUpdate(GlobalType eType, ContainerID eServerID)
{
	if (eType == GLOBALTYPE_GAMESERVER)
	{
		RemoteCommand_GameServer_GlobalChatStatusReturn(eType, eServerID, (spGlobalLink && linkConnected(spGlobalLink)));
	}
	// Must add handling for other servers if desired
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(ChatDebug);
int ChatServerDebug_ResolveAccountName(const char *accountName)
{
	ChatUser *user = userFindByAccountName(accountName);
	return user ? user->id : 0;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(ChatDebug);
int ChatServerDebug_ResolveDisplayName(const char *displayName)
{
	ChatUser *user = userFindByHandle(displayName);
	return user ? user->id : 0;
}

void blacklist_Violation(ChatUser *user, const char *string)
{
	U32 uTime;
	if (!user || userIsBanned(user))
		return;
	uTime = timeSecondsSince2000();
	if (uTime - user->uBlacklistLastTime > gShardChatServerConfig.uBlacklistDuration)
		user->uBlacklistCount = 0;
	user->uBlacklistCount++;
	user->uBlacklistLastTime = uTime;

	if (user->uBlacklistCount >= gShardChatServerConfig.uMaxBlacklistNum)
	{
		char *escapedText = NULL;
		estrAppendEscaped(&escapedText, string);
		sendCmdAndParamsToGlobalChat("blacklist_ViolationBan", "%d \"%s\"", user->id, escapedText);
		estrDestroy(&escapedText);
		//RemoteCommand_dbBootPlayerByAccountID_Remote(GLOBALTYPE_OBJECTDB, 0, user->id);
		if (user->pPlayerInfo && user->pPlayerInfo->onlineCharacterID)
			RemoteCommand_gslAP_BanAccount(GLOBALTYPE_ENTITYPLAYER, user->pPlayerInfo->onlineCharacterID, user->pPlayerInfo->onlineCharacterID);
	}
	else
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_BlacklistWarning", STRFMT_END);
	}
}

#include "chatGlobal_h_ast.c"
#include "chatGlobal_c_ast.c"
