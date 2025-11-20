#include "chatGlobal.h"
#include "AutoGen/chatGlobal_h_ast.h"
#include "AutoGen/chatGlobal_c_ast.h"

#include "aslChatServer.h"
#include "channels.h"
#include "chatdb.h"
#include "chatGlobalConfig.h"
#include "chatGuild.h"
#include "ChatServer/chatBlacklist.h"
#include "ChatServer/chatShared.h"
#include "chatShardCluster.h"
#include "msgsend.h"
#include "userPermissions.h"
#include "users.h"
#include "xmpp/XMPP_Chat.h"
#include "xmpp/XMPP_ChatUtils.h"

#include "accountnet.h"
#include "error.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "GameStringFormat.h"
#include "HashFunctions.h"
#include "logging.h"
#include "objContainer.h"
#include "qsortG.h"
#include "ResourceInfo.h"
#include "ServerLib.h"
#include "sock.h"
#include "StashTable.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "timing.h"
#include "utilitiesLib.h"

#include "AutoGen/chatCommon_h_ast.h"
#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/chatdb_h_ast.h"
#include "AutoGen/chatGuild_h_ast.h"
#include "AutoGen/chatShared_h_ast.h"
#include "AutoGen/Controller_autogen_RemoteFuncs.h"

#define CHATSERVER_CONNECT_TIMEOUT 30
#define CHATSERVER_RECONNECT_TIME 60 // starts counting from when it first tries to connect, not after timeout

static bool sbEnableGlobalChatTranslations = false; // false means translations are pushed to the shard chat servers
AUTO_CMD_INT(sbEnableGlobalChatTranslations, EnableTranslations) ACMD_CMDLINE;

static bool sbEnableDisconnectAlerts = false; // Alertf() when a "live" shard disconnects from the Global Chat Server
AUTO_CMD_INT(sbEnableDisconnectAlerts, EnableDisconnectAlerts) ACMD_CMDLINE;

// TODO remove this eventually
extern bool gbDisableChatVoiceEnum;

extern GlobalChatServerConfig gGlobalChatServerConfig;

extern bool gbTestingMetrics;
extern TimingHistory *gChatHistory;
extern bool gbConnectGlobalChatToAccount;
bool gbGlobalChatResponse = false;

// Local Chat Server defs
extern ParseTable parse_PlayerInfoList[];
#define TYPE_parse_PlayerInfoList PlayerInfoList
extern ParseTable parse_ChatUser[];
#define TYPE_parse_ChatUser ChatUser
extern ParseTable parse_ChatChannel[];
#define TYPE_parse_ChatChannel ChatChannel
static NetComm *spChatServerComm = NULL; // Comm for link to Account Server

static NetLink *spAccountServerLink = NULL;
static NetLink *sDummyXmppLink;			// Dummy NetLink to identify XMPP command context.
static bool sbSentAccountServerConnection = false;

static void connectToAccountServer(void);

// Global Chat Server defs

static StashTable stGlobalChatLinks = NULL; // key = ID, value = GlobalChatLinkStruct pointer
static StashTable stGlobalChatReverseLink = NULL; // key = link pointer, value = ID
static StashTable stGlobalChatMonitorTable = NULL; // key = Shard name, for server monitor sorting

// For debugging
extern StashTable stGlobalChatAllGuilds;

NetComm *getChatServerComm(void)
{
	if (!spChatServerComm)
		spChatServerComm = commCreate(0,1);
	return spChatServerComm;
}

GlobalChatLinkStruct *GlobalChatGetShardData(U32 uLinkID)
{
	GlobalChatLinkStruct *linkStruct = NULL;
	stashIntFindPointer(stGlobalChatLinks, uLinkID, &linkStruct);
	return linkStruct;
}

void GlobalChatGetShardListIterator(StashTableIterator *iter)
{
	stashGetIterator(stGlobalChatLinks, iter);
}

void GlobalChatLoginUserByLinkID(ChatUser *user, U32 uLinkID)
{
	GlobalChatLinkStruct *linkStruct = NULL;
	stashIntFindPointer(stGlobalChatLinks, uLinkID, &linkStruct);
	if (linkStruct)
	{
		eaiPushUnique(&linkStruct->ppOnlineUserIDs, user->id);
	}
}
void GlobalChatLogoutUserByLinkID(ChatUser *user, U32 uLinkID)
{
	GlobalChatLinkStruct *linkStruct = NULL;
	stashIntFindPointer(stGlobalChatLinks, uLinkID, &linkStruct);
	if (linkStruct)
	{
		eaiFindAndRemove(&linkStruct->ppOnlineUserIDs, user->id);
	}
}

static void GlobalChatConnect(NetLink* link,void *user_data)
{
	static U32 uNextID = LOCAL_CHAT_ID_START;
	GlobalChatLinkStruct *linkStruct;

	// Allocate link struct for this shard Chat Server.
	linkStruct = StructCreate(parse_GlobalChatLinkStruct);
	linkStruct->uID = uNextID;
	linkStruct->localChatLink = link;
	linkStruct->uConnectedTime = timeSecondsSince2000();
	linkStruct->shardGuildStash = stashTableCreateInt(25);
	linkStruct->shardGuildNameStash = stashTableCreateWithStringKeys(25, StashDefault);
	linkStruct->stUserGuilds = stashTableCreateInt(25);

	stashIntAddPointer(stGlobalChatLinks, uNextID, linkStruct, true);
	stashAddInt(stGlobalChatReverseLink, link, uNextID, true);
	uNextID++;
	{ // send the revision info here
		Packet *pkt = pktCreate(link, FROM_GLOBALCHATSERVER_VERSIONINFO);
		pktSendU32(pkt, CHATSERVER_MAJOR_REVISION);
		pktSendU32(pkt, CHATSERVER_MINOR_REVISION);
		pktSend(&pkt);
	}
	
	// send the blacklist update here; ignored by shards that don't support it
	blacklist_InitShardChatServer(link);
}

static void GlobalChatDisconnectNotify(GlobalChatLinkStruct *linkStruct)
{
	StashTableIterator linkIterator;
	StashElement linkElement;

	// Loop over each member of each guild on this shard.
	stashGetIterator(linkStruct->shardGuildStash, &linkIterator);
	while (stashGetNextElement(&linkIterator, &linkElement))
	{
		ChatGuild *guild = stashElementGetPointer(linkElement);
		EARRAY_INT_CONST_FOREACH_BEGIN(guild->pGuildMembers, i, n);
		{
			ChatUser *user = userFindByContainerId(guild->pGuildMembers[i]);
			if (user)
			{
				GlobalChatLinkUserGuilds *guilds = NULL;
				bool bOfficer = false;
				if (stashIntFindPointer(linkStruct->stUserGuilds, user->id, &guilds))
				{
					if (eaiFind(&guilds->officerGuilds, guild->iGuildID) >= 0)
						bOfficer = true;
				}
				XMPPChat_GuildListRemove(user, guild, bOfficer, false);
			}
		}
		EARRAY_FOREACH_END;
	}

	if (linkStruct->uShardID)
	{
		ChatShard *shard = findShardByID(linkStruct->uShardID);
		if (shard)
			shard->linkStructID = 0;
	}
}

static void GlobalChatDisconnect(NetLink* link,void *user_data)
{
	U32 uID = 0;
	stashRemoveInt(stGlobalChatReverseLink, link, &uID);
	if (uID)
	{
		GlobalChatLinkStruct *linkStruct = NULL;
		stashIntRemovePointer(stGlobalChatLinks, uID, &linkStruct);
		if (linkStruct)
		{
			int i;
			for (i=eaiSize(&linkStruct->ppOnlineUserIDs)-1; i>=0; i--)
			{
				ChatUser *user = userFindByContainerId(linkStruct->ppOnlineUserIDs[i]);
				if (user)
					userLogout(user, uID);
			}
			GlobalChatDisconnectNotify(linkStruct);
			{
				StashTableIterator iter = {0};
				StashElement elem;
				stashGetIterator(linkStruct->shardGuildStash, &iter);
				while (stashGetNextElement(&iter, &elem))
				{
					char name[64] = "";
					ChatGuild *pGuild = stashElementGetPointer(elem);
					if (pGuild)
					{
						sprintf(name, "%s-%d", linkStruct->pShardName, pGuild->iGuildID);
						stashRemovePointer(stGlobalChatAllGuilds, name, NULL);
					}
				}
			}

			stashTableDestroyStruct(linkStruct->shardGuildStash, NULL, parse_ChatGuild);
			stashTableDestroy(linkStruct->shardGuildNameStash);
			stashTableDestroyStruct(linkStruct->stUserGuilds, NULL, parse_GlobalChatLinkUserGuilds);
			if (linkStruct->pShardName)
			{
				log_printf(LOG_CHATSERVER, "Global Chat Server: [%s] has disconnected from Global Chat.\n", linkStruct->pShardName);
				printf("\nGlobal Chat Server: [%s] has disconnected from Global Chat.\n", linkStruct->pShardName);
				stashRemovePointer(stGlobalChatMonitorTable, linkStruct->pShardName, NULL);
			}
			if (sbEnableDisconnectAlerts && linkStruct->pShardCategoryName && stricmp(linkStruct->pShardCategoryName, "Live") == 0)
			{
				Alertf("Live Shard '%s' disconnected from Global Chat Server.\n", linkStruct->pShardName);
			}
			StructDestroy(parse_GlobalChatLinkStruct, linkStruct);
			if (uID == XMPP_CHAT_ID)
				XMPP_ClearServerState(); // clear server state for XMPP on disconnect
		}
	}
}

// Get the name of a connected shard.
const char *GetShardNameById(U32 uLinkId)
{
	GlobalChatLinkStruct *linkStruct = NULL;
	stashIntFindPointer(stGlobalChatLinks, uLinkId, &linkStruct);
	if (linkStruct)
	{
		return linkStruct->pShardName;
	}
	return "Unknown";
}

// Verify that there are no shard hash name collisions.
static void CheckShardHashCollisions(U32 uHash, const char *pUniqueId)
{
	static StashTable stHashValues = NULL;
	char *pValue = NULL;
	static char collided[] = "";

	PERFINFO_AUTO_START_FUNC();

	// Create stash table if necessary.
	if (!stHashValues)
		stHashValues = stashTableCreateInt(0);

	// Look up this hash value in the table.
	// If found, alert on a collision.  If not found, add it.
	stashIntFindPointer(stHashValues, uHash, &pValue);
	if (pValue)
	{
		if (pValue != collided && stricmp(pValue, pUniqueId))
		{
			AssertOrAlert("CHATSERVER.HASH_COLLISION", "The shard hash value for \"%s\" (%lx) has collided with \"%s\".  Please rename one of them.",
				pUniqueId, uHash, pValue);
			stashIntAddPointer(stHashValues, uHash, collided, true);
		}
	}
	else
	{
		bool success;
		success = stashIntAddPointer(stHashValues, uHash, strdup(pUniqueId), false);
		devassert(success);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

// Get shard hash.
U32 GetShardHashById(U32 uLinkId)
{
	GlobalChatLinkStruct *linkStruct = NULL;
	char *uniqueId = NULL;
	U32 hash = 0;

	PERFINFO_AUTO_START_FUNC();

	stashIntFindPointer(stGlobalChatLinks, uLinkId, &linkStruct);
	if (linkStruct)
	{
		// Generate hash value for this shard.
		estrStackCreate(&uniqueId);
		if (uLinkId == XMPP_CHAT_ID)
			estrCopy2(&uniqueId, "XMPP");
		else
			estrPrintf(&uniqueId, "%s,%s,%s", linkStruct->pShardName, linkStruct->pShardCategoryName, linkStruct->pProductName);
		hash = hashStringInsensitive(uniqueId);

		// Check for collisions.
		CheckShardHashCollisions(hash, uniqueId);
		estrDestroy(&uniqueId);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return hash;
}

const char *GetLocalChatLinkShardName(NetLink *link)
{
	U32 id = 0;
	GlobalChatLinkStruct *linkStruct = NULL;
	if (!link)
		return NULL;
	if (!stashFindInt(stGlobalChatReverseLink, link, &id))
		return NULL;
	if (!stashIntFindPointer(stGlobalChatLinks, id, &linkStruct))
		return NULL;
	return linkStruct->pShardName;
}
U32 GetLocalChatLinkID(NetLink *link)
{
	U32 id = 0;
	if (!link)
		return 0;
	stashFindInt(stGlobalChatReverseLink, link, &id);
	return id;
}
NetLink * GetLocalChatLink(U32 id)
{
	GlobalChatLinkStruct *linkStruct = NULL;
	if (!id || id == XMPP_CHAT_ID)
		return NULL;
	stashIntFindPointer(stGlobalChatLinks, id, &linkStruct);
	return linkStruct ? linkStruct->localChatLink : NULL;
}

U32 GetLocalChatLinkIdByShardName(const char *pchShardName)
{
	StashTableIterator iter = {0};
	StashElement elem;
	U32 id = 0;

	stashGetIterator(stGlobalChatLinks, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		GlobalChatLinkStruct *localChat = stashElementGetPointer(elem);
		if (!stricmp_safe(pchShardName, localChat->pShardName))
		{
			id = localChat->uID;
			break;
		}
	}
	return id;
}

// Make an earray of shard identifiers.
void GetShardIds(U32 **eaShardIds)
{
	StashTableIterator iter = {0};
	StashElement elem;

	// Clear output.
	ea32Clear(eaShardIds);

	// Add each identifier.
	stashGetIterator(stGlobalChatLinks, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		GlobalChatLinkStruct *localChat = stashElementGetPointer(elem);
		ea32Push(eaShardIds, localChat->uID);
	}
}

static void GlobalChatRegisterShard(NetLink *link, GlobalChatLinkStruct *linkStruct, ChatRegisterShardData *shardData)
{
	GlobalChatLinkStruct *oldLinkStruct = NULL;
	struct in_addr ina = {0};
	char *ipString;
	ina.S_un.S_addr = linkGetIp(link);
	ipString = inet_ntoa(ina);

	linkStruct->pShardName = allocAddString(shardData->pShardName);
	linkStruct->pShardCategoryName = allocAddString(shardData->pShardCategory);
	linkStruct->pProductName = allocAddString(shardData->pProduct);
	if (!nullStr(shardData->pClusterName))
		linkStruct->pClusterName = allocAddString(shardData->pClusterName);

	if (stricmp(linkStruct->pProductName, "XMPP") == 0)
	{
		linkStruct->uID = XMPP_CHAT_ID;
		stashAddInt(stGlobalChatReverseLink, link, XMPP_CHAT_ID, true);
		stashIntRemovePointer(stGlobalChatLinks, linkStruct->uID, NULL);
		stashIntAddPointer(stGlobalChatLinks, XMPP_CHAT_ID, linkStruct, true);
	}

	if (stashRemovePointer(stGlobalChatMonitorTable, linkStruct->pShardName, &oldLinkStruct))
	{
		char buf[17];
		// So that when this link dies, it doesn't remove the wrong link from the stGlobalChatMonitorTable
		linkRemove(&oldLinkStruct->localChatLink);
		Errorf("Shard with duplicate name connected to Global Chat Server (name = %s, new ip = %s, old ip = %s)", linkStruct->pShardName,
			ipString, linkGetIpStr(link, buf, sizeof(buf)));
	}

	if (stashAddPointer(stGlobalChatMonitorTable, linkStruct->pShardName, linkStruct, false))
	{
		printf("Global Chat Server: [%s] has connected from IP [%s].\n", linkStruct->pShardName, ipString);
	}

	{
		char *logline = NULL;
		estrStackCreate(&logline);
		logAppendPairs(&logline,
			logPair("shardname", "%s", linkStruct->pShardName),
			logPair("clustername", "%s", linkStruct->pClusterName ? linkStruct->pClusterName : "none"),
			logPair("shardcategory", "%s", linkStruct->pShardCategoryName),
			logPair("product", "%s", linkStruct->pProductName),
			logPair("ip", "%s", ipString), NULL);
		log_printf(LOG_CHATSERVER, "Shard Connected: %s", logline);
		estrDestroy(&logline);
		registerShard(linkStruct);
	}
}

AUTO_COMMAND ACMD_CATEGORY(ChatDebug);
int GlobalChatTestCommandString(ACMD_SENTENCE pCommandString)
{
	CmdContext context = {0};
	int retVal;
	context.commandData = NULL;
	retVal = ChatServerCmdParseFunc(pCommandString, NULL, &context);
	return retVal;
}

extern ChatDb chat_db;
static void GlobalChatHandleMessage(Packet *pkt,int cmd,NetLink* link,void *user_data)
{
	Packet *response;

	PERFINFO_AUTO_START_FUNC();

	switch (cmd)
	{
	xcase TO_GLOBALCHATSERVER_CONNECT:
		{
			//char *shardName = pktGetStringTemp(pkt);
		}
	xcase TO_GLOBALCHATSERVER_COMMAND:
		{
			char *pCommandString = pktGetStringTemp(pkt);
			CmdContext context = {0};
			int retVal;
				
			context.commandData = link;
			retVal = ChatServerCmdParseFunc(pCommandString, NULL, &context);

			if (gbChatVerbose)
				printf ("Global: %s\n", pCommandString);

			switch (retVal)
			{
			case CHATRETURN_FWD_NONE:
			case CHATRETURN_VOIDRETURN:
			case CHATRETURN_UNKNOWN_COMMAND:
				// does nothing
			xcase CHATRETURN_FWD_ALLLOCAL:
				{
					sendCommandToAllLocal(pCommandString, true);
				}
			xcase CHATRETURN_FWD_ALLLOCAL_MINUSENDER:
				{
					sendCommandToAllLocalMinusSender(link, pCommandString);
				}
			xdefault:
				response = pktCreate(link, FROM_GLOBALCHATSERVER_COMMAND);
				pktSendU32(response, retVal);
				pktSendString(response, pCommandString);
				pktSend(&response);
			}
		}
	xcase TO_GLOBALCHATSERVER_ACCOUNT_REQUEST:
		{
			char *accountIDString = pktGetStringTemp(pkt);
			PlayerInfoList playerList = {0};
			U32 uID = link ? GetLocalChatLinkID(link) : 0;

			if (gbChatVerbose)
				printf ("Received account list request.\n");
			ParserReadText(accountIDString, parse_PlayerInfoList, &playerList, PARSER_IGNORE_ALL_UNKNOWN | PARSER_NOERROR_ONIGNORE);

			sendAccountList(link, uID, playerList.piAccountIDs);
			StructDeInit(parse_PlayerInfoList, &playerList);
		}
	xcase TO_GLOBALCHATSERVER_CHANNEL_REQUEST:
		{
			sendChannelList(link);
		}
	xcase TO_GLOBALCHATSERVER_RESET_METRICS:
		{
			timingHistoryClear(gChatHistory);
		}
	xcase TO_GLOBALCHATSERVER_METRICS_REQUEST:
		{
			response = pktCreate(link, FROM_GLOBALCHATSERVER_METRICS);
			pktSendU32(response, timingHistoryAverageInInterval(gChatHistory, 1));
			pktSend(&response);
		}
	xcase TO_GLOBALCHATSERVER_REGISTER_SHARD:
		{
			U32 uID = 0;
			if (stashFindInt(stGlobalChatReverseLink, link, &uID))
			{
				GlobalChatLinkStruct *linkStruct = NULL;
				if (stashIntFindPointer(stGlobalChatLinks, uID, &linkStruct))
				{
					ChatRegisterShardData shardData = {0};
					shardData.pShardName = pktGetStringTemp(pkt);
					shardData.pShardCategory = pktGetStringTemp(pkt);
					shardData.pProduct = pktGetStringTemp(pkt);
					GlobalChatRegisterShard(link, linkStruct, &shardData);
				}
			}
		}
	xcase TO_GLOBALCHATSERVER_REGISTER_SHARD_EX:
		{
			U32 uID = 0;
			if (stashFindInt(stGlobalChatReverseLink, link, &uID))
			{
				GlobalChatLinkStruct *linkStruct = NULL;
				if (stashIntFindPointer(stGlobalChatLinks, uID, &linkStruct))
				{
					ChatRegisterShardData shardData = {0};
					char *shardString = pktGetStringTemp(pkt);

					if (shardString && ParserReadText(shardString, parse_ChatRegisterShardData, &shardData, PARSER_IGNORE_ALL_UNKNOWN | PARSER_NOERROR_ONIGNORE))
						GlobalChatRegisterShard(link, linkStruct, &shardData);

					StructDeInit(parse_ChatRegisterShardData, &shardData);
				}
			}
		}
	xdefault:
		break;
	}
	if(gbTestingMetrics)
		timingHistoryPush(gChatHistory);

	PERFINFO_AUTO_STOP_FUNC();
}

///////////////////////////////////////////
// Global Chat Server Functions
///////////////////////////////////////////

void sendShardInfoRequest(NetLink *link)
{
	if (linkConnected(link))
	{
		// Make sure GCS knows about this link
		U32 uChatID = GetLocalChatLinkID(link);
		if (uChatID)
		{
			Packet *pkt = pktCreate(link, FROM_GLOBALCHATSERVER_SHARDINFO_REQUEST);
			pktSend(&pkt);
		}
	}
}

void sendAccountList (NetLink *link, U32 uChatServerID, U32 *piAccountIDList)
{
	if (linkConnected(link))
	{
		Packet *pkt;
		ChatServerData data = {0};
		char *dataString = NULL;
		int i, size;

		size = eaiSize(&piAccountIDList);
		for (i=0; i<size; i++)
		{
			ChatUser *user = userFindByContainerId(piAccountIDList[i]);
			if (user)
			{
				PlayerInfoStruct *pInfo = findPlayerInfoByLocalChatServerID(user, uChatServerID);
				if (pInfo)
					user->pPlayerInfo = pInfo;
				else
					user->pPlayerInfo = NULL;
				eaPush(&data.ppUsers, user);

				// This should not be set anyway
				if (gbDisableChatVoiceEnum && (user->access & CHANFLAGS_VOICE) != 0)
					((NOCONST(ChatUser)*) user)->access &= (~CHANFLAGS_VOICE);
			}
		}

		ParserWriteText(&dataString, parse_ChatServerData, &data, 0, 0, TOK_NO_NETSEND);

		size = eaSize(&data.ppUsers);
		for (i=0; i<size; i++)
		{
			data.ppUsers[i]->pPlayerInfo = NULL; // Always unset this
		}

		pkt = pktCreate(link, FROM_GLOBALCHATSERVER_ACCOUNT_LIST);
		pktSendString(pkt, dataString);
		pktSend(&pkt);
		eaDestroy(&data.ppUsers);
		estrDestroy(&dataString);
	}
}

void sendChannelList (NetLink *link)
{
	if (linkConnected(link))
	{
		Packet *pkt;
		ContainerIterator iter = {0};
		ChatChannel *channel;	
		ChatServerData data = {0};
		char *dataString = NULL;

		objInitContainerIteratorFromType(GLOBALTYPE_CHATCHANNEL, &iter);
		channel = objGetNextObjectFromIterator(&iter);
		while (channel)
		{
			// This value should not be set anyway
			if (gbDisableChatVoiceEnum && (channel->access & CHANFLAGS_VOICE) != 0)
				((NOCONST(ChatChannel)*) channel)->access &= (~CHANFLAGS_VOICE);
			eaPush(&data.ppChannels, channel);
			channel = objGetNextObjectFromIterator(&iter);
		}
		objClearContainerIterator(&iter);

		ParserWriteText(&dataString, parse_ChatServerData, &data, 0, 0, TOK_NO_NETSEND);
		pkt = pktCreate(link, FROM_GLOBALCHATSERVER_CHANNEL_LIST);
		pktSendString(pkt, dataString);
		pktSend(&pkt);
		eaDestroy(&data.ppChannels);
		estrDestroy(&dataString);
	}
}

void sendCommandToAllLocal(const char *commandString, bool bSendToXmpp)
{
	StashTableIterator iter;
	StashElement pElem;
	
	stashGetIterator(stGlobalChatLinks, &iter);
	while (stashGetNextElement(&iter, &pElem))
	{
		GlobalChatLinkStruct* linkStruct = (GlobalChatLinkStruct*) stashElementGetPointer(pElem);
		if (bSendToXmpp || linkStruct->uID != XMPP_CHAT_ID)
		{
			NetLink *link = linkStruct->localChatLink;
			Packet *response = pktCreate(link, FROM_GLOBALCHATSERVER_COMMAND);
			pktSendU32(response, CHATRETURN_NONE);
			pktSendString(response, commandString);
			pktSend(&response);
		}
	}
}

void sendCommandToAllLocalMinusSender(NetLink *sender, const char *commandString)
{
	StashTableIterator iter;
	StashElement pElem;
	
	stashGetIterator(stGlobalChatLinks, &iter);
	while (stashGetNextElement(&iter, &pElem))
	{
		GlobalChatLinkStruct* linkStruct = (GlobalChatLinkStruct*) stashElementGetPointer(pElem);
		NetLink *link = linkStruct->localChatLink;
		
		if (link != sender)
		{
			Packet *response = pktCreate(link, FROM_GLOBALCHATSERVER_COMMAND);
			pktSendU32(response, CHATRETURN_NONE);
			pktSendString(response, commandString);
			pktSend(&response);
		}
	}
}

// This will skip sending to XMPP servers
void sendCommandToUserLocal(ChatUser *user, const char *commandString, NetLink *originator)
{
	int i;
	for (i=eaSize(&user->ppPlayerInfo)-1; i>=0; i--)
	{
		NetLink *link = GetLocalChatLink(user->ppPlayerInfo[i]->uChatServerID);

		if (originator && link == originator || user->ppPlayerInfo[i]->uChatServerID == XMPP_CHAT_ID)
			continue;
		if (link && linkConnected(link))
		{
			Packet *pkt = pktCreate(link, FROM_GLOBALCHATSERVER_COMMAND);
			pktSendU32 (pkt, CHATRETURN_NONE);
			pktSendString(pkt, commandString);
			pktSend(&pkt);
		}
		else
		{
			StructDestroy(parse_PlayerInfoStruct, user->ppPlayerInfo[i]);
			eaRemoveFast(&user->ppPlayerInfo, i);
		}
	}
}

void sendCommandToUserLocalEx(ChatUser *user, NetLink *originator, const char *pCommand, const char *pParamFormat, ...)
{
	char *fullCommand = NULL;
	char *pParameters = NULL;

	if (pParamFormat)
		estrGetVarArgs(&pParameters, pParamFormat);
	if (pParameters)
		estrPrintf(&fullCommand, "%s %s", pCommand, pParameters);
	else
		estrPrintf(&fullCommand, "%s", pCommand);
	estrDestroy(&pParameters);

	sendCommandToUserLocal(user, fullCommand, originator);
	estrDestroy(&fullCommand);
}

void sendCommandToLink(NetLink *link, const char *commandString)
{
	if (link && linkConnected(link))
	{
		Packet *pkt = pktCreate(link, FROM_GLOBALCHATSERVER_COMMAND);
		pktSendU32 (pkt, CHATRETURN_NONE);
		pktSendString(pkt, commandString);
		pktSend(&pkt);
	}
}

void sendCommandToLinkEx(NetLink *link, const char *pCommand, const char *pParamFormat, ...)
{
	PERFINFO_AUTO_START_FUNC();
	if (link && linkConnected(link))
	{
		char *fullCommand = NULL;
		char *pParameters = NULL;
		Packet *pkt;

		if (pParamFormat)
			estrGetVarArgs(&pParameters, pParamFormat);
		if (pParameters)
			estrPrintf(&fullCommand, "%s %s", pCommand, pParameters);
		else
			estrPrintf(&fullCommand, "%s", pCommand);

		pkt = pktCreate(link, FROM_GLOBALCHATSERVER_COMMAND);
		pktSendU32 (pkt, CHATRETURN_NONE);
		pktSendString(pkt, fullCommand);
		pktSend(&pkt);

		estrDestroy(&fullCommand);
		estrDestroy(&pParameters);
	}
	PERFINFO_AUTO_STOP_FUNC();
}

PlayerInfoStruct *userFindPlayerInfoByLinkID(ChatUser *user, U32 uChatLinkID)
{
	int i;
	if (!user)
		return NULL;
	for (i=eaSize(&user->ppPlayerInfo)-1; i>=0; i--)
	{
		if (user->ppPlayerInfo[i]->uChatServerID == uChatLinkID)
			return user->ppPlayerInfo[i];
	}
	return NULL;
}

Language userGetLastLanguage(ChatUser *user)
{
	Language eLanguage = user->eLastLanguage;
	NetLink *link = chatServerGetCommandLink();	
	if (link && linkConnected(link))
	{
		U32 uChatLink = GetLocalChatLinkID(link);
		PlayerInfoStruct *playerInfo = userFindPlayerInfoByLinkID(user, uChatLink);
		if (playerInfo)
			eLanguage = playerInfo->eLanguage;
	}
	return eLanguage;
}

static void populateTranslationParameters(SA_PARAM_NN_VALID ChatTranslation *translation, va_list va)
{
	S32 iContainer;
	const unsigned char *pchKey;
	for (iContainer = 0; (pchKey = va_arg(va, const unsigned char *)) != 0; iContainer++)
	{
		char chType = va_arg(va, char);
		ChatTranslationParam *param = StructCreate(parse_ChatTranslationParam);
		param->strFmt_Code = chType;
		param->key = StructAllocString(pchKey);
		switch (chType)
		{
		case STRFMT_CODE_INT:
		case STRFMT_CODE_TIMER:
			param->iIntValue = va_arg(va, S32);
		xcase STRFMT_CODE_STRING:
		case STRFMT_CODE_MESSAGEKEY:
			{
				char *pchValue = va_arg(va, unsigned char *);
				if (!pchValue)
					param->pchStringValue = StructAllocString("");
				else
					param->pchStringValue = StructAllocString(pchValue);
			}
		xcase STRFMT_CODE_FLOAT:
		case STRFMT_CODE_STRUCT:
		case STRFMT_CODE_MESSAGE:
			devassertmsgf(0, "Global Chat Server currently does not support Messages with '%c' variables", chType);
			StructDestroy(parse_ChatTranslationParam, param);
			param = NULL;
		xdefault:
			devassertmsgf(0, "Invalid type code passed to %s, did you forget STRFMT_END?", __FUNCTION__);
		// TODO add STRFMT_CODE_DATETIME support?
		}
		if (param) eaPush(&translation->ppParameters, param);
	}
}

ChatTranslation * constructTranslationMessageVa(const char *messageKey, va_list ap)
{
	ChatTranslation *msg = NULL;
	if (messageKey)
	{
		msg = StructCreate(parse_ChatTranslation);
		msg->key = StructAllocString(messageKey);
		populateTranslationParameters(msg, ap);
	}
	return msg;
}

ChatTranslation * constructTranslationMessage(const char *messageKey, ...)
{
	ChatTranslation *trans;
	va_list	ap;
	va_start(ap, messageKey);
	trans = constructTranslationMessageVa(messageKey, ap);
	va_end(ap);
	return trans;
}

void sendTranslatedMessageToUser(ChatUser *user, int eChatType, const char *channel_name, const char *messageKey, ...)
{
	NetLink *link = chatServerGetCommandLink();
	va_list	ap;
	if (!user)
		return;
	if (sbEnableGlobalChatTranslations)
	{	
		Language eLanguage = user->eLastLanguage;
		if (linkConnected(link))
		{
			U32 uChatLink = GetLocalChatLinkID(link);
			PlayerInfoStruct *playerInfo = userFindPlayerInfoByLinkID(user, uChatLink);
			if (playerInfo)
				eLanguage = playerInfo->eLanguage;
		}	
		va_start(ap, messageKey);
		sendChatSystemTranslatedMsgV(link, user, eChatType, channel_name, eLanguage, messageKey, ap);
		va_end(ap);
	}
	else
	{
		if (userOnline(user))
		{
			ChatTranslation translation = {0};
			char *commandString = NULL;
			char *translationParse = NULL;

			translation.key = StructAllocString(messageKey);
			translation.eType = eChatType;
			translation.channel_name = StructAllocString(channel_name);
			va_start(ap, messageKey);
			populateTranslationParameters(&translation, ap);
			va_end(ap);

			ParserWriteTextEscaped(&translationParse, parse_ChatTranslation, &translation, 0, 0, 0);
			estrPrintf(&commandString, "ChatServer_ForwardTranslation %d \"%s\"", user->id, translationParse);
			if (link && linkConnected(link))
			{   // do not send translations to XMPP
				if (GetLocalChatLinkID(link) != XMPP_CHAT_ID)
					sendCommandToLink(link, commandString);
			}
			else
				sendCommandToUserLocal(user, commandString, NULL);
			StructDeInit(parse_ChatTranslation, &translation);
			estrDestroy(&commandString);
			estrDestroy(&translationParse);
		}
	}
}

void broadcastTranslatedMessageToUser(ChatUser *user, int eChatType, const char *channel_name, const char *messageKey, ...)
{
	va_list	ap;
	if (sbEnableGlobalChatTranslations)
	{
		int i;
		Language eLanguage = user->eLastLanguage;
		for (i=eaSize(&user->ppPlayerInfo)-1; i>=0; i--)
		{
			NetLink *link = GetLocalChatLink(user->ppPlayerInfo[i]->uChatServerID);
			eLanguage = user->ppPlayerInfo[i]->eLanguage;
			if (link && linkConnected(link))
			{
				va_start(ap, messageKey);
				sendChatSystemTranslatedMsgV(link, user, eChatType, channel_name, eLanguage, messageKey, ap);
				va_end(ap);
			}
			else if (user->ppPlayerInfo[i]->uChatServerID != XMPP_CHAT_ID)
			{
				StructDestroy(parse_PlayerInfoStruct, user->ppPlayerInfo[i]);
				eaRemoveFast(&user->ppPlayerInfo, i);
			}
		}
	}
	else
	{
		if (userOnline(user))
		{
			ChatTranslation translation = {0};
			char *commandString = NULL;
			char *translationParse = NULL;

			translation.key = StructAllocString(messageKey);
			translation.eType = eChatType;
			translation.channel_name = StructAllocString(channel_name);
			va_start(ap, messageKey);
			populateTranslationParameters(&translation, ap);
			va_end(ap);

			ParserWriteTextEscaped(&translationParse, parse_ChatTranslation, &translation, 0, 0, 0);
			estrPrintf(&commandString, "ChatServer_ForwardTranslation %d \"%s\"", user->id, translationParse);
			sendCommandToUserLocal(user, commandString, NULL);
			StructDeInit(parse_ChatTranslation, &translation);
			estrDestroy(&commandString);
			estrDestroy(&translationParse);
		}
	}
}

void sendMessageToUser(ChatUser *user, int eChatType, const char *channel_name, const char *fmt, ...)
{
	char buffer[1024];
	va_list	ap;

	va_start(ap, fmt);
	vsprintf(buffer,fmt,ap);
	va_end(ap);

	sendMessageToUserStatic(user, eChatType, channel_name, buffer);
}

void sendMessageToUserStatic(ChatUser *user, int eChatType, const char *channel_name, const char *msg)
{
	int i;
	for (i=eaSize(&user->ppPlayerInfo)-1; i>=0; i--)
	{
		NetLink *link = GetLocalChatLink(user->ppPlayerInfo[i]->uChatServerID);

		if (link && linkConnected(link))
		{
			Packet *pkt = pktCreate(link, FROM_GLOBALCHATSERVER_MESSAGE);
			pktSendU32(pkt, user->id);
			pktSendU32(pkt, eChatType);
			pktSendString(pkt, channel_name);
			pktSendString(pkt, msg);
			pktSend(&pkt);
		}
		else if (user->ppPlayerInfo[i]->uChatServerID != XMPP_CHAT_ID)
		{
			StructDestroy(parse_PlayerInfoStruct, user->ppPlayerInfo[i]);
			eaRemoveFast(&user->ppPlayerInfo, i);
		}
	}
}

void sendMessageToLink(NetLink *link, ChatUser *user, int eChatType, const char *channel_name, const char *msg)
{
	if (link && linkConnected(link))
	{
		Packet *pkt = pktCreate(link, FROM_GLOBALCHATSERVER_MESSAGE);
		pktSendU32(pkt, user->id);
		pktSendU32(pkt, eChatType);
		pktSendString(pkt, channel_name);
		pktSendString(pkt, msg);
		pktSend(&pkt);
	}
}

void sendAlertToUser(ChatUser *user, const char *title, const char *msg)
{
	int i;	
	for (i=eaSize(&user->ppPlayerInfo)-1; i>=0; i--)
	{
		NetLink *link = GetLocalChatLink(user->ppPlayerInfo[i]->uChatServerID);

		if (link && linkConnected(link))
		{
			Packet *pkt = pktCreate(link, FROM_GLOBALCHATSERVER_ALERT);
			pktSendU32(pkt, user->id);
			pktSendString(pkt, title);
			pktSendString(pkt, msg);
			pktSend(&pkt);
		}
		else if (user->ppPlayerInfo[i]->uChatServerID != XMPP_CHAT_ID)
		{
			StructDestroy(parse_PlayerInfoStruct, user->ppPlayerInfo[i]);
			eaRemoveFast(&user->ppPlayerInfo, i);
		}
	}
}

void sendAlertToLink(NetLink *link, ChatUser *user, const char *title, const char *msg)
{	
	if (link && linkConnected(link))
	{
		Packet *pkt = pktCreate(link, FROM_GLOBALCHATSERVER_ALERT);
		pktSendU32(pkt, user->id);
		pktSendString(pkt, title);
		pktSendString(pkt, msg);
		pktSend(&pkt);
	}
}

void sendChannelUpdate(ChatChannel *channel)
{
	StashTableIterator iter;
	StashElement pElem;
	ChatServerData data = {0};
	char *dataString = NULL;

	eaPush(&data.ppChannels, channel);
	ParserWriteText(&dataString, parse_ChatServerData, &data, 0, 0, TOK_NO_NETSEND);
	eaDestroy(&data.ppUsers);	
	
	stashGetIterator(stGlobalChatLinks, &iter);

	while (stashGetNextElement(&iter, &pElem))
	{
		GlobalChatLinkStruct* linkStruct = (GlobalChatLinkStruct*) stashElementGetPointer(pElem);
		NetLink *link = linkStruct->localChatLink;
		if (link && linkConnected(link))
		{
			Packet *response = pktCreate(link, FROM_GLOBALCHATSERVER_CHANNEL_LIST);
			pktSendString(response, dataString);
			pktSend(&response);
		}
	}
	estrDestroy(&dataString);
}

void sendChannelDeletion(U32 uID)
{
	StashTableIterator iter;
	StashElement pElem;
	
	stashGetIterator(stGlobalChatLinks, &iter);
	while (stashGetNextElement(&iter, &pElem))
	{
		GlobalChatLinkStruct* linkStruct = (GlobalChatLinkStruct*) stashElementGetPointer(pElem);
		NetLink *link = linkStruct->localChatLink;
		if (link && linkConnected(link))
		{
			Packet *response = pktCreate(link, FROM_GLOBALCHATSERVER_CHANNEL_DELETE);
			pktSendU32(response, uID);
			pktSend(&response);
		}
	}
}

void sendUserUpdate(ChatUser *user)
{
	int i;
	//broadcast to xmpp clients
	XMPPChat_RecvPresence(user, NULL);

	for (i=eaSize(&user->ppPlayerInfo)-1; i>=0; i--)
	{
		NetLink *link = GetLocalChatLink(user->ppPlayerInfo[i]->uChatServerID);

		if (link && linkConnected(link))
		{
			ChatServerData data = {0};
			char *dataString = NULL;
			Packet *pkt;

			user->pPlayerInfo = user->ppPlayerInfo[i];
			eaPush(&data.ppUsers, user);

			ParserWriteText(&dataString, parse_ChatServerData, &data, 0, 0, TOK_NO_NETSEND);

			user->pPlayerInfo = NULL; // Always unset this

			pkt = pktCreate(link, FROM_GLOBALCHATSERVER_ACCOUNT_LIST);
			pktSendString(pkt, dataString);
			pktSend(&pkt);
			eaDestroy(&data.ppUsers);
			estrDestroy(&dataString);
		}
		else if (user->ppPlayerInfo[i]->uChatServerID != XMPP_CHAT_ID)
		{
			StructDestroy(parse_PlayerInfoStruct, user->ppPlayerInfo[i]);
			eaRemoveFast(&user->ppPlayerInfo, i);
		}
	}
}

///////////////////////////////////////////
// Command Parse Functions
///////////////////////////////////////////

extern CmdList gGlobalCmdList;
extern CmdList gRemoteCmdList;
// This is a carbon-copy of defaultCmdParseFunc that only checks the remote command list for the command
static CmdContext *pCmdContext = NULL;
const char *chatServerGetCommandString(void)
{
	if (pCmdContext)
		return pCmdContext->commandString;
	return NULL;
}

// For global chat server; returns link to the local chat server that sent the command
NetLink *chatServerGetCommandLink(void)
{
	if (pCmdContext)
		return pCmdContext->commandData;
	return NULL;
}

void forceSetCommandContext(CmdContext *context)
{
	pCmdContext = context;
}
void unsetCommandContext(void)
{
	pCmdContext = NULL;
}

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

int ChatServerCmdParseFunc(const char *cmdOrig, char **ppReturnString, CmdContext *pContext)
{
	char *pReturnStringInternal = NULL;
	char *cmd, *cmdStart = NULL;
	int retVal = -1;
	CmdContext *pLastContext = pCmdContext;

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
		if (!cmdParseAndExecute(&gRemoteCmdList, cmd, pContext))
		{
			estrClear(pContext->output_msg);
			if (!cmdParseAndExecute(&gGlobalCmdList, cmd, pContext))
			{
				log_printf(LOG_CHATSERVER, "[Error: Unknown Command] %s", cmd);
				Errorf("Unknown Command: %s", cmd);
				estrDestroy(&pReturnStringInternal);
				estrDestroy(&cmdStart);
				return CHATRETURN_UNKNOWN_COMMAND; // did not find command;
			}
		}

		if (pContext->output_msg && estrLength(pContext->output_msg))
			retVal = pContext->return_val.intval;
		else
			retVal = CHATRETURN_VOIDRETURN;
		if (gbChatVerbose) {
			cmdPrintPrettyOutput(pContext, printf);
		}
	}

	estrDestroy(&pReturnStringInternal);
	estrDestroy(&cmdStart);
	pCmdContext = pLastContext;
	return retVal;
}

static char **sppControllerTrackerIPs = NULL;
void initGlobalChatServer(void)
{
	NetListen *listen = commListen(commDefault(), LINKTYPE_SHARD_NONCRITICAL_10MEG, LINK_COMPRESS|LINK_FORCE_FLUSH, DEFAULT_GLOBAL_CHATSERVER_PORT, 
		GlobalChatHandleMessage, GlobalChatConnect, GlobalChatDisconnect, 0);
	NetLink *controllerTrackerLink;
	int i;

	GetAllUniqueIPs(GetControllerTrackerHost(), &sppControllerTrackerIPs);
	
	stGlobalChatLinks = stashTableCreateInt(50);
	stGlobalChatReverseLink = stashTableCreateAddress(50);
	stGlobalChatMonitorTable = stashTableCreateWithStringKeys(50, StashDefault);
	stGlobalChatAllGuilds = stashTableCreateWithStringKeys(50, StashDeepCopyKeys);

	resRegisterDictionaryForStashTable("Shards", RESCATEGORY_OTHER, 0, stGlobalChatMonitorTable, parse_GlobalChatLinkStruct);
	InitGlobalChatServerConfig();
	initChatBlacklist();

	// Initialize XMPP.
	XMPPChat_Init();
	// Initialize Chat Cluster Guild Names
	ChatCluster_InitGuildStash();
	
	if (isProductionMode())
	{
		for (i = eaSize(&sppControllerTrackerIPs)-1; i>=0; i--)
		{
			controllerTrackerLink = commConnect(commDefault(), LINKTYPE_SHARD_NONCRITICAL_10MEG, LINK_FORCE_FLUSH, sppControllerTrackerIPs[i], CONTROLLERTRACKER_SHARD_INFO_PORT, 0, 0, 0, 0);
		
			if (linkConnectWait(&controllerTrackerLink, 2.0))
			{
				Packet *pkt = pktCreate(controllerTrackerLink, TO_NEWCONTROLLERTRACKER_FROM_GLOBAL_CHATSERVER_ONLINE_MESSAGE);
				pktSend(&pkt);
				Sleep(100);
				linkRemove(&controllerTrackerLink);
				break;
			}
		}
	}
	assertmsg(listen, "Could not listen on Global Chat Server port");
	if (gbConnectGlobalChatToAccount)
		connectToAccountServer();
}

//AUTO_COMMAND_REMOTE;
//void chatGlobalModifyAccount(U32 accountID, const char *accountName, const char *displayName)
//{
//	ChatUser *user = userFindByContainerId(accountID);
//
//	// Create Account if it doesn't exist
//	userAdd(accountID, accountName, displayName);
//}

/////////////////////////////////////////
// Global Chat to Account Server
/////////////////////////////////////////

AUTO_STRUCT;
typedef struct GlobalChatCommandWait
{
	U32 uRequestID;
	U32 uChatServerID;

	char *chatHandle;
	char *commandString;

	U32 uStartTime;

	GlobalCommandTimeoutCB cb; NO_AST
	void *userData; NO_AST
} GlobalChatCommandWait;

static GlobalChatCommandWait **sppGlobalCommandWaitQueue = NULL;
static U32 uLastAccountRequestID = 0;

static U32 globalChatAddCommandWaitQueue (U32 uChatServerID, const char *chatHandle, const char * commandString, GlobalCommandTimeoutCB cb, void *data)
{
	ChatUser *user = userFindByHandle(chatHandle);
	U32 uTime = timeSecondsSince2000();

	if (user && user->uHandleLastChecked + gGlobalChatServerConfig.iHandleCacheDuration > uTime)
	{
		// Don't bother checking if handle cache is still valid
		return 0;
	}
	else if (gbConnectGlobalChatToAccount && linkConnected(spAccountServerLink))
	{
		GlobalChatCommandWait *queue;
		Packet *pkt;

		if (!uChatServerID || !chatHandle || !*chatHandle || !commandString || !*commandString)
			return 0;
		queue = StructCreate(parse_GlobalChatCommandWait);
		queue->uChatServerID = uChatServerID;
		queue->chatHandle  = StructAllocString(chatHandle);
		queue->commandString = strdup(commandString);
		queue->uStartTime = uTime;
		queue->uRequestID = ++uLastAccountRequestID;
		queue->cb = cb;
		queue->userData = data;
		eaPush(&sppGlobalCommandWaitQueue, queue);

		pkt = pktCreate(spAccountServerLink, TO_ACCOUNTSERVER_DISPLAYNAME_REQUEST);
		pktSendU32(pkt, queue->uRequestID);
		pktSendString(pkt, queue->chatHandle);
		pktSend(&pkt);
		return queue->uRequestID;
	}
	return 0;
}

U32 globalChatAddCommandWaitQueueByLink (NetLink *link, const char *chatHandle, const char * commandString)
{
	return globalChatAddCommandWaitQueueByLinkEx(link, chatHandle, commandString, NULL, NULL);
}

U32 globalChatAddCommandWaitQueueByLinkEx (NetLink *link, const char *chatHandle, const char * commandString, GlobalCommandTimeoutCB cb, void *data)
{
	U32 uChatID = GetLocalChatLinkID(link);
	if (uChatID)
	{
		return globalChatAddCommandWaitQueue(uChatID, chatHandle, commandString, cb, data);
	}
	return 0;
}

GlobalChatCommandWait *findGlobalCommandWait(U32 uRequestID)
{
	int i;
	if (!uRequestID)
		return NULL;
	for (i=eaSize(&sppGlobalCommandWaitQueue)-1; i>=0; i--)
	{
		GlobalChatCommandWait *queue = sppGlobalCommandWaitQueue[i];
		if (queue->uRequestID == uRequestID)
		{
			return queue;
		}
	}
	return NULL;
}

static void executeGlobalCommandWait (U32 uRequestID)
{
    int i, j;
    U32 uTime = timeSecondsSince2000();
    int expiredCount = 0;

    if (!uRequestID)
    {
        return;
    }
    for (i=0; i < eaSize(&sppGlobalCommandWaitQueue); i++)
    {
        GlobalChatCommandWait *queue = sppGlobalCommandWaitQueue[i];
        if (queue->uRequestID == uRequestID)
        {
            CmdContext context = {0};
			ChatUser *user = userFindByHandle(queue->chatHandle);

			if (user) // update the last checked time
				user->uHandleLastChecked = timeSecondsSince2000();
            context.commandData = GetLocalChatLink(queue->uChatServerID);

            gbGlobalChatResponse = true;
            ChatServerCmdParseFunc(queue->commandString, NULL, &context);
            gbGlobalChatResponse = false;

			if (queue->cb)
				queue->cb(TRANSACTION_OUTCOME_SUCCESS, queue->userData);
            StructDestroy(parse_GlobalChatCommandWait, queue);
            eaRemove(&sppGlobalCommandWaitQueue, i);

            // get rid of any expired commands that were in the queue before this one
            if (expiredCount > 0)
            {
                for (j=expiredCount-1; j>=0; j--)
                {
					queue = sppGlobalCommandWaitQueue[j];
					if (queue->cb)
						queue->cb(TRANSACTION_OUTCOME_FAILURE, queue->userData);
					printf("Command timed out: time difference = %d\n", uTime - queue->uStartTime);
                    StructDestroy(parse_GlobalChatCommandWait, queue);
                }
                eaRemoveRange(&sppGlobalCommandWaitQueue, 0, expiredCount);
            }
            return;
        }
        if (gGlobalChatServerConfig.iHandleQueryTimeout && 
			queue->uStartTime + gGlobalChatServerConfig.iHandleQueryTimeout < uTime)
        {
            expiredCount++;
        }
    }
}

void GlobalCommandWaitTick(void)
{
	if (gGlobalChatServerConfig.iHandleQueryTimeout)
	{
		int i;
		U32 uTime = timeSecondsSince2000();
		int expiredCount = 0;

		for (i=0; i < eaSize(&sppGlobalCommandWaitQueue); i++)
		{
			GlobalChatCommandWait *queue = sppGlobalCommandWaitQueue[i];
			if (queue->uStartTime + gGlobalChatServerConfig.iHandleQueryTimeout < uTime)
				expiredCount++;
			else
				break;
		}
		if (expiredCount > 0)
		{
			for (i=expiredCount-1; i>=0; i--)
			{
				GlobalChatCommandWait *queue = sppGlobalCommandWaitQueue[i];
				printf("Command expired: time difference = %d\n", uTime - queue->uStartTime);
				if (queue->cb)
					queue->cb(TRANSACTION_OUTCOME_FAILURE, queue->userData);
				StructDestroy(parse_GlobalChatCommandWait, queue);
			}
			eaRemoveRange(&sppGlobalCommandWaitQueue, 0, expiredCount);
		}
	}
}

void ChatServerGlobalTick(F32 fElapsed)
{
	static U32 uLastConnectTryTime = 0;
	static U32 uLastPrintTime = 0;
	CmdContext xmppContext = {0};
	U32 uTime;

	PERFINFO_AUTO_START_FUNC();

	uTime = timeSecondsSince2000();

	// Process pending XMPP requests.
	xmppContext.commandData = sDummyXmppLink;
	forceSetCommandContext(&xmppContext);
	unsetCommandContext();

	if(gbChatVerbose && gbTestingMetrics && linkConnected(spAccountServerLink) && stashGetOccupiedSlots(stGlobalChatLinks) > 0)
	{
		if(uTime > uLastPrintTime)
		{
			printf("Most chat packets handled in 1.0 seconds: %d\n", timingHistoryMostInInterval(gChatHistory, 1.0));
			printf("%d users with %d channels\n", objCountTotalContainersWithType(GLOBALTYPE_CHATUSER), objCountTotalContainersWithType(GLOBALTYPE_CHATCHANNEL));
			uLastPrintTime = uTime;
		}
	}
	processShardRegisterQueue();

	if (!gbConnectGlobalChatToAccount)
		return;
	if (!spAccountServerLink)
	{
		if (!uLastConnectTryTime || uLastConnectTryTime + CHATSERVER_RECONNECT_TIME < uTime)
			connectToAccountServer();
		else
		{
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}
		uLastConnectTryTime = uTime;
	}

	if (linkConnected(spAccountServerLink))
	{
		if (!sbSentAccountServerConnection)
		{
			// Tell the Account Server that this is the Global Chat Server
			Packet *pkt = pktCreate(spAccountServerLink, TO_ACCOUNTSERVER_CHATSERVER_CONNECT);
			pktSend(&pkt);
			sbSentAccountServerConnection = true;
		}
	}
	else if (spAccountServerLink && uLastConnectTryTime + CHATSERVER_CONNECT_TIMEOUT < uTime)
	{
		linkRemove(&spAccountServerLink);
	}
	GlobalCommandWaitTick();

	PERFINFO_AUTO_STOP_FUNC();
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
int chatServer_ForceAccountUpdate(U32 uAccountID)
{
	if (linkConnected(spAccountServerLink) && sbSentAccountServerConnection)
	{
		Packet *pkt = pktCreate(spAccountServerLink, TO_ACCOUNTSERVER_UNKNOWN_ACCOUNT);
		pktSendU32(pkt, 0); // No request ID
		pktSendU32(pkt, uAccountID);
		pktSend(&pkt);
		return 0;
	}
	Errorf("account_server_offline");
	return -1;
}

static void GlobalChat_AccountServerConnect(NetLink* link,void *user_data)
{
}
static void GlobalChat_AccountServerDisconnect(NetLink* link,void *user_data)
{
	spAccountServerLink = NULL;
	sbSentAccountServerConnection = false;
}
static void GlobalChat_HandleAccountServerMessage(Packet *pkt,int cmd,NetLink* link,void *user_data)
{
	switch (cmd)
	{
	case (TO_GLOBALCHATSERVER_DISPLAYNAME_TRANS):
		{
			U32 requestID = pktGetU32(pkt);
			U32 accountID = pktGetU32(pkt);

			if (requestID)
			{
				ChatUser *user = userFindByContainerId(accountID);
				GlobalChatCommandWait *queue = findGlobalCommandWait(requestID);
				if (user && queue)
				{
					userChangeChatHandle(accountID, queue->chatHandle);
					executeGlobalCommandWait(requestID);
				}
				else if (queue)
				{
					// Does nothing with the user - no deletion should be handled here
					user = userFindByHandle(queue->chatHandle);
					executeGlobalCommandWait(requestID);
				}
			}
			else
			{
				Packet *response = pktCreate(spAccountServerLink, TO_ACCOUNTSERVER_UNKNOWN_ACCOUNT);
				pktSendU32(response, requestID);
				pktSendU32(response, accountID);
				pktSend(&response);
			}
		}
	xcase (TO_GLOBALCHATSERVER_CREATEACCOUNT):
		{
			U32 requestID = pktGetU32(pkt);
			U32 accountID = pktGetU32(pkt);
			char *displayName = pktGetStringTemp(pkt);
			char *accountName = pktGetStringTemp(pkt);
			GlobalChatCommandWait *queue = findGlobalCommandWait(requestID);
			ChatUser *user = userFindByContainerId(accountID);

			if (accountID && accountName && displayName && *accountName && *displayName)
			{
				if (user)
				{
					bool bValidAccountName = true;
					if (stricmp(accountName, user->accountName))
						bValidAccountName = userSetAccountName(user, accountName);
					if (!bValidAccountName)
						ErrorOrAlert("ChatServer", "Tried to register a duplicate account name: %s", accountName);
					if (stricmp(displayName, user->handle))
						userChangeChatHandle(user->id, displayName);
					executeGlobalCommandWait(requestID);
				}
				else
				{
					CmdContext *context = NULL;
					if (queue)
					{
						context = calloc(1, sizeof(CmdContext));
						context->commandData = GetLocalChatLink(queue->uChatServerID);
						context->commandString =  queue->commandString ? strdup(queue->commandString) : NULL;
					}
					userAdd(context, accountID, accountName, displayName);
					if (queue)
					{
						StructDestroy(parse_GlobalChatCommandWait, queue);
						eaFindAndRemove(&sppGlobalCommandWaitQueue, queue);
					}
				}
			}
			else if (queue)
			{
				executeGlobalCommandWait(requestID);
			}
		}
	xcase (TO_GLOBALCHATSERVER_DISPLAYNAME_UPDATE):
		{
			U32 requestID = pktGetU32(pkt);
			U32 accountID = pktGetU32(pkt);
			char *displayName = pktGetStringTemp(pkt);
			ChatUser *user = userFindByContainerId(accountID);
			if (user)
			{
				userChangeChatHandle(accountID, displayName);
				if (requestID)
					executeGlobalCommandWait(requestID);
			}
			else
			{
				Packet *response = pktCreate(spAccountServerLink, TO_ACCOUNTSERVER_UNKNOWN_ACCOUNT);
				pktSendU32(response, requestID);
				pktSendU32(response, accountID);
				pktSend(&response);
			}
		}
	xcase (TO_GLOBALCHATSERVER_REMOVEACCOUNT):
		{
			U32 requestID = pktGetU32(pkt);
			U32 accountID = pktGetU32(pkt);
			GlobalChatCommandWait *queue = findGlobalCommandWait(requestID);

			userDelete(accountID); // Remove the account and name associations
			// Execute the command, which should return an error because the name associations are gone
			executeGlobalCommandWait(requestID);
		}
	xdefault:
		{
			log_printf(LOG_CHATSERVER, "[Error: Unknown packet received from Account Server]");
		}
	}
}

static void connectToAccountServer(void)
{
	if (!spAccountServerLink)
	{
		spAccountServerLink = commConnect(getChatServerComm(), LINKTYPE_SHARD_NONCRITICAL_10MEG, LINK_FORCE_FLUSH, getAccountServer(), DEFAULT_ACCOUNTSERVER_GLOBALCHATSERVER_PORT, 
			GlobalChat_HandleAccountServerMessage, GlobalChat_AccountServerConnect,GlobalChat_AccountServerDisconnect,0);
		if (!spAccountServerLink)
			return;
		linkSetKeepAliveSeconds(spAccountServerLink, 5.);
	}
}

// Debugging Functions
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(ChatServer);
int ChatServerDebug_ResolveAccountName(const char *accountName)
{
	ChatUser *user = userFindByAccountName(accountName);
	return user ? user->id : 0;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(ChatServer);
int ChatServerDebug_ResolveDisplayName(const char *displayName)
{
	ChatUser *user = userFindByHandle(displayName);
	return user ? user->id : 0;
}

void trDefaultUserUpdate_CB(TransactionReturnVal *returnVal, U32 *userID)
{
	if (userID)
	{
		if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		{
			ChatUser *user = userFindByContainerId(*userID);
			if (user)
				sendUserUpdate(user);
		}
		free(userID);
	}
}

void trDefaultChannelUpdate_CB(TransactionReturnVal *returnVal, U32 *channelID)
{
	if (channelID)
	{
		if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		{
			ChatChannel *channel = channelFindByID(*channelID);
			if (channel)
				sendChannelUpdate(channel);
			else
				sendChannelDeletion(*channelID);
		}
		free(channelID);
	}
}

static int runCommandAsUser(CmdContext *context, ChatUser *user, ChatUser *target, const char *command, const char *parameterString)
{
	char *commandString = NULL;
	int result;
	if (!target)
		result = CHATRETURN_USER_DNE;
	else
	{
		CmdContext innerContext = {0};
		estrPrintf(&commandString, "%s %d %s", command, target->id, parameterString);
		innerContext.commandData = context->commandData;
		result = ChatServerCmdParseFunc(commandString, NULL, &innerContext);
	}

	if (user && CHATRETURN_SUCCESS(result))
	{
		sendMessageToUser(user, kChatLogEntryType_System, NULL, "Command '%s' successfully run.", command);
	}
	else if (result >= CHATRETURN_NONE && result < CHATRETURN_COUNT)
	{
		sendMessageToUser(user, kChatLogEntryType_System, NULL, "Command '%s' failed: %s.", command, 
			StaticDefineIntRevLookup(ChatServerReturnCodesEnum,result));
	}
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_CATEGORY(ChatServer) ACMD_ACCESSLEVEL(9);
int ChatServerRunCommandAsUserByName(CmdContext *context, U32 uID, const char *command, const char *displayName, ACMD_SENTENCE parameterString)
{
	ChatUser *user = userFindByContainerId(uID);
	ChatUser *target = userFindByHandle(displayName);
	return runCommandAsUser(context, user, target, command, parameterString);
}

AUTO_COMMAND_REMOTE ACMD_CATEGORY(ChatServer) ACMD_ACCESSLEVEL(9);
int ChatServerRunCommandAsUserByID(CmdContext *context, U32 uID, const char *command, U32 targetID, ACMD_SENTENCE parameterString)
{
	ChatUser *user = userFindByContainerId(uID);
	ChatUser *target = userFindByContainerId(targetID);
	return runCommandAsUser(context, user, target, command, parameterString);
}

static int sortByMailboxSize (const ChatUser **user1, const ChatUser **user2)
{
	int size1 = eaSize(&(*user1)->email), size2 = eaSize(&(*user2)->email);
	if (size1 < size2)
		return -1;
	if (size2 < size1)
		return 1;
	return 0;
}

static int sortByFriendsListSize (const ChatUser **user1, const ChatUser **user2)
{
	int size1 = eaiSize(&(*user1)->friends), size2 = eaiSize(&(*user2)->friends);
	if (size1 < size2)
		return -1;
	if (size2 < size1)
		return 1;
	return 0;
}

AUTO_COMMAND ACMD_CATEGORY(ChatDebug);
void ChatServerPrintLargestMailboxes(void)
{
	ContainerIterator iter = {0};
	ChatUser *user;
	ChatUser **ppBiggest = NULL;
	int count = 0, minMailbox = 0, i;
	objInitContainerIteratorFromType(GLOBALTYPE_CHATUSER, &iter);
	while (user = objGetNextObjectFromIterator(&iter))
	{
		if (count < 10)
		{
			eaPush(&ppBiggest, user);
			count++;
			if (minMailbox == 0)
				minMailbox = eaSize(&user->email);
			else
				minMailbox = min(minMailbox, eaSize(&user->email));
			eaQSort(ppBiggest, sortByMailboxSize);
		}
		else if (eaSize(&user->email) > minMailbox)
		{
			ppBiggest[0] = user;
			minMailbox = eaSize(&user->email);
			eaQSort(ppBiggest, sortByMailboxSize);
		}
		// TODO
	}
	objClearContainerIterator(&iter);
	printf("\nMailbox Sizes:\n");
	for (i=eaSize(&ppBiggest)-1; i>=0; i--)
	{
		printf("[%d] %s - %d\n", ppBiggest[i]->id, ppBiggest[i]->accountName, eaSize(&ppBiggest[i]->email));
	}
	eaDestroy(&ppBiggest);
}

AUTO_COMMAND ACMD_CATEGORY(ChatDebug);
void ChatServerPrintLargestFriendsList(void)
{
	ContainerIterator iter = {0};
	ChatUser *user;	
	ChatUser **ppBiggest = NULL;
	int count = 0, minFriends = 0, i;
	objInitContainerIteratorFromType(GLOBALTYPE_CHATUSER, &iter);
	while (user = objGetNextObjectFromIterator(&iter))
	{
		if (count < 10)
		{
			eaPush(&ppBiggest, user);
			count++;
			if (minFriends == 0)
				minFriends = eaiSize(&user->friends);
			else
				minFriends = min(minFriends, eaiSize(&user->friends));
			eaQSort(ppBiggest, sortByFriendsListSize);
		}
		else if (eaiSize(&user->friends) > minFriends)
		{
			ppBiggest[0] = user;
			minFriends = eaiSize(&user->friends);
			eaQSort(ppBiggest, sortByFriendsListSize);
		}
		// TODO
	}
	objClearContainerIterator(&iter);
	printf("\nFriends List Sizes:\n");
	for (i=eaSize(&ppBiggest)-1; i>=0; i--)
	{
		printf("[%d] %s - %d\n", ppBiggest[i]->id, ppBiggest[i]->accountName, eaiSize(&ppBiggest[i]->friends));
	}
	eaDestroy(&ppBiggest);
}

const char *GCS_GetProductDisplayName(const char *internalProductName)
{
	ProductToDisplayNameMap *map = eaIndexedGetUsingString(&gGlobalChatServerConfig.ppProductDisplayName, internalProductName);
	if (map)
		return map->productDisplayName;
	return NULL;
}

const char *GCS_GetShardDisplayName(const char *internalProductName, const char *shardName)
{
	ProductToDisplayNameMap *prodMap = eaIndexedGetUsingString(&gGlobalChatServerConfig.ppProductDisplayName, internalProductName);
	if (prodMap)
	{
		ProductShardToDisplayMap *shardMap = eaIndexedGetUsingString(&prodMap->ppShardMap, shardName);
		if (shardMap)
		{
			if (shardMap->shardDisplayName && *shardMap->shardDisplayName)
				return shardMap->shardDisplayName;
			return NULL;
		}
	}
	return shardName;
}

#include "chatGlobal_h_ast.c"
#include "chatGlobal_c_ast.c"
