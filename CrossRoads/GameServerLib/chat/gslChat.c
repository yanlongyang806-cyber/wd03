/***************************************************************************
*     Copyright (c) 2005-2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslChat.h"
#include "gslChatConfig.h"
#include "gslFriendsIgnore.h"
#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "ChatData.h"
#include "GameAccountDataCommon.h"
#include "EntityLib.h"
#include "team.h"
#include "Guild.h"
#include "EntityIterator.h"
#include "gslEntity.h"
#include "gslPartition.h"
#include "gslTransactions.h"
#include "GameServerLib.h"
#include "utilitiesLib.h"
#include "itemCommon.h"
#include "rewardCommon.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "../StaticWorld/ZoneMap.h"
#include "WorldGrid.h"
#include "gslSendToClient.h"
#include "NotifyCommon.h"
#include "OfficerCommon.h"
#include "Player.h"
#include "aiLib.h"
#include "Character.h"
#include "CharacterClass.h"

#include "AutoGen/ChatServer_autogen_remotefuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/chatCommon_h_ast.h"
#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/gslChat_c_ast.h"
#include "Player_h_ast.h"

#include "SimpleParser.h"
#include "GameStringFormat.h"
#include "AutoTransDefs.h"
#include "gslMapTransfer.h"
#include "gslCommandParse.h"
#include "HashFunctions.h"

#include "GamePermissionsCommon.h"

#define LOCAL_CHAT_DIST 100.0f

#define CONFIRM_GLOBALCHAT_STATUS_PERIOD 20 // 20 seconds between each check if Global Chat is offline or not; 
											// Happens on every command that requires global chat

static bool sbGlobalChatOnline = false;
static U32 uLastGlobalChatTimeChecked = 0;

// Used for basic custom channel and private messages
static bool gslChat_UserCanChat(Entity *pEnt)
{
	if (gamePermission_Enabled())
		return GamePermission_EntHasToken(pEnt, GAME_PERMISSION_CAN_SEND_TELL);
	else
		return true;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gslChat_RequestChatRelayData(Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer)
	{
		ChatAuthRequestData authRequest = {0};
		authRequest.uAccountID = entGetAccountID(pEnt);
		if (authRequest.uAccountID)
		{
			ChatLoginData loginData = {0};
			GameAccountData *pGAD = entity_GetGameAccount(pEnt);

			// Use account access level for updating ChatUser
			authRequest.uCharacterID = entGetContainerID(pEnt);
			if (pGAD && pGAD->iLastChatAccessLevel &&
				gad_GetLastChatAccessLevel(pGAD) <= pEnt->pPlayer->accountAccessLevel)
			{
				loginData.uAccessLevel = authRequest.uAccountAccessLevel = gad_GetLastChatAccessLevel(pGAD);
			}
			else
			{
				loginData.uAccessLevel = authRequest.uAccountAccessLevel = pEnt->pPlayer->accountAccessLevel;
				AutoTrans_GameAccount_tr_LastChatAccessLevel(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GAMEACCOUNTDATA, 
					entGetAccountID(pEnt), loginData.uAccessLevel);
			}
			authRequest.bSocialRestricted = !gslChat_UserCanChat(pEnt);

			loginData.uAccountID = authRequest.uAccountID;
			loginData.pAccountName = StructAllocString(pEnt->pPlayer->privateAccountName);
			loginData.pDisplayName = StructAllocString(pEnt->pPlayer->publicAccountName);

			RemoteCommand_ChatServerAddOrUpdateUser(GLOBALTYPE_CHATSERVER, 0, &loginData);
			RemoteCommand_crManager_GetUserSecretHash(GLOBALTYPE_CHATSERVER, 0, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), 
				&authRequest);
			StructDeInit(parse_ChatLoginData, &loginData);
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void gslChat_ReceiveChatRelayData(ChatAuthData *data)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, data->uCharacterID);
	if (pEnt)
		ClientCmd_gclChatConnect_ReceiveData(pEnt, data);
}

void gslChat_Tick(void)
{
	if (!sbGlobalChatOnline)
	{
		U32 uTime = timeSecondsSince2000();
		if (uTime - uLastGlobalChatTimeChecked  > CONFIRM_GLOBALCHAT_STATUS_PERIOD)
		{
			uLastGlobalChatTimeChecked = uTime;
			RemoteCommand_RequestGlobalChatUpdate(GLOBALTYPE_CHATSERVER, 0, GetAppGlobalType(), GetAppGlobalID());
		}
	}
	MailItemQueueTick();
}

////////////////////////////////
// Zone channel stuff

AUTO_STRUCT;
typedef struct ZoneChannelName
{
	U32 uPartitionID; AST(KEY)
	char channelName[128];
} ZoneChannelName;

typedef struct GameServerZoneChannels
{
	char *mapName;
	char shortMapName[64];
	char baseZoneChannelName[128];
	EARRAY_OF(ZoneChannelName) ppZoneChannels;
} GameServerZoneChannels;

static GameServerZoneChannels sZoneChannels = {0};
static const char *getZoneChannelNameByPartition(U32 uPartitionID)
{
	const char *mapName = zmapGetFilename(NULL);
	ZoneMapType eMapType = zmapInfoGetMapType(NULL);
	ZoneChannelName *pZoneChannel;

	if (!devassert(mapName))
		return NULL;
	
	if (!sZoneChannels.mapName || stricmp(mapName, sZoneChannels.mapName))
	{
		// Map has changed or is not initialized
		getFileNameNoExtNoDirs(sZoneChannels.shortMapName, mapName);
		zone_GetZoneChannelNameFromMapName(sZoneChannels.baseZoneChannelName, ARRAY_SIZE_CHECKED(sZoneChannels.baseZoneChannelName), 
			sZoneChannels.shortMapName, 0, eMapType, GAMESERVER_VSHARD_ID);

		if (sZoneChannels.mapName)
			free(sZoneChannels.mapName);
		sZoneChannels.mapName = StructAllocString(mapName);
		if (sZoneChannels.ppZoneChannels)
			eaDestroyStruct(&sZoneChannels.ppZoneChannels, parse_ZoneChannelName);
	}
	if (uPartitionID == 0)
		return sZoneChannels.baseZoneChannelName;
	pZoneChannel = eaIndexedGetUsingInt(&sZoneChannels.ppZoneChannels, uPartitionID);
	if (!pZoneChannel)
	{
		pZoneChannel = calloc(1, sizeof(ZoneChannelName));
		pZoneChannel->uPartitionID = uPartitionID;

		zone_GetZoneChannelNameFromMapName(pZoneChannel->channelName, ARRAY_SIZE_CHECKED(pZoneChannel->channelName), 
			sZoneChannels.shortMapName, uPartitionID, eMapType, GAMESERVER_VSHARD_ID);
		if (!sZoneChannels.ppZoneChannels)
			eaIndexedEnable(&sZoneChannels.ppZoneChannels, parse_ZoneChannelName);
		eaIndexedAdd(&sZoneChannels.ppZoneChannels, pZoneChannel);
	}
	return pZoneChannel->channelName;
}

const char *getZoneChannelName(Entity *pEnt)
{
	U32 uPartitionID = gslEntGetPartitionID(pEnt);
	return getZoneChannelNameByPartition(uPartitionID);
}

static void GameServer_InitializePartitionZoneChannel(int iPartitionIdx)
{
	U32 uID = partition_IDFromIdx(iPartitionIdx);
	RemoteCommand_ChatServerCreateZoneChannel(GLOBALTYPE_CHATSERVER, 0, getZoneChannelNameByPartition(uID));
}

void ServerChat_InitializeZoneChannel(void)
{
	if (!GAMESERVER_IS_UGCEDIT)
		partition_ExecuteOnEachPartition(GameServer_InitializePartitionZoneChannel);
}

void ServerChat_MapLoad(void)
{
	if (!GAMESERVER_IS_UGCEDIT)
	{
		Entity* currEnt;
		EntityIterator* iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);

		while ((currEnt = EntityIteratorGetNext(iter)))
		{
			RemoteCommand_ChatServerJoinChannel(GLOBALTYPE_CHATSERVER, 0, entGetAccountID(currEnt), getZoneChannelName(currEnt), 
				CHANNEL_SPECIAL_ZONE, true);
		}
		EntityIteratorRelease(iter);
	}
}

void ServerChat_MapUnload(void)
{
	if (!GAMESERVER_IS_UGCEDIT)
	{
		Entity* currEnt;
		EntityIterator* iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);

		while ((currEnt = EntityIteratorGetNext(iter)))
		{
			RemoteCommand_ChatServerLeaveChannel(GLOBALTYPE_CHATSERVER, 0, entGetAccountID(currEnt), getZoneChannelName(currEnt));
		}
		EntityIteratorRelease(iter);
	}
}

// Game Server Chat Reconnect Initialization
void GameServer_ReconnectGlobalChat(bool bOnline)
{
	sbGlobalChatOnline = bOnline;
	if (sbGlobalChatOnline)
	{
		// Re-send all logins
		ServerChat_ReconnectToChatServer();
	}
	else
		uLastGlobalChatTimeChecked = timeSecondsSince2000();
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void GameServer_GlobalChatStatusReturn(bool bGCSOnline)
{
	GameServer_ReconnectGlobalChat(bGCSOnline);
}

// Always returns true if Shard Chat Server is in Local-Only mode
extern bool GameServer_IsGlobalChatOnline(void)
{
	return sbGlobalChatOnline;
}

extern bool GameServer_RequireGlobalChat(Entity *pEnt)
{
	if (!GameServer_IsGlobalChatOnline())
	{
		char *error = NULL;
		devassert(pEnt);
		entFormatGameMessageKey(pEnt, &error, "ChatServer_GlobalOffline", STRFMT_END);
		if (!error)
			estrCopy2(&error, "Global Chat Server is offline.");
		ClientCmd_NotifySend(pEnt, kNotifyType_ServerOffline, error, NULL, NULL);
		estrDestroy(&error);
		return false;
	}
	return true;
}

typedef struct ChatReturnCallback
{
	TransactionReturnCallback successCB;
	TransactionReturnCallback failureCB;

	void *userData;
} ChatReturnCallback;

void ServerChat_DefaultChatReturn(TransactionReturnVal *pReturnVal, EntityRef *pEntRef)
{
	Entity *pEnt = entFromEntityRefAnyPartition(*pEntRef);
	if (pReturnVal->eOutcome == TRANSACTION_OUTCOME_FAILURE)
	{
		char *error = NULL;
		entFormatGameMessageKey(pEnt, &error, "ChatServer_Offline", STRFMT_END);
		if (!error) {
			estrCopy2(&error, "Shard Chat Server is offline.");
		}

		if (pEnt) {
			notify_NotifySend(pEnt, kNotifyType_ServerOffline, error, NULL, NULL);
		}
		estrDestroy(&error);
	}

	if (pEntRef) {
		free(pEntRef);
	}
}

void* ServerChat_CreateDefaultData(EntityRef entRef)
{
	EntityRef *pEntRef = calloc(1, sizeof(EntityRef));
	*pEntRef = entRef;
	return pEntRef;
}
//#define CHATCOMMAND_DEFAULT_RETURN(ent) objCreateManagedReturnVal(ServerChat_DefaultChatReturn, ServerChat_CreateDefaultData(entGetRef(ent)))
#define CHATCOMMAND_DEFAULT_RETURN(ent) NULL

ChatUserInfo *ServerChat_CreateLocalizedUserInfoFromEnt(Entity *pEntFrom, Entity *pEntTo) {
	if (pEntFrom) {
		ChatUserInfo *pUser = StructCreate(parse_ChatUserInfo);

		if (pEntFrom->pPlayer) {
			const char *pchHandle = pEntFrom->pPlayer->publicAccountName && pEntFrom->pPlayer->publicAccountName[0]
			? pEntFrom->pPlayer->publicAccountName 
				: pEntFrom->pPlayer->privateAccountName;

			pUser->accountID = pEntFrom->pPlayer->accountID;
			estrCopy2(&pUser->pchHandle, pchHandle);
			pUser->playerID = pEntFrom->myContainerID;
		} else {
			pUser->nonPlayerEntityRef = pEntFrom->myRef;
		}

		estrCopy2(&pUser->pchName, entGetLangName(pEntFrom, entGetLanguage(pEntTo)));

		return pUser;
	}

	return NULL;
}

static PlayerInfoStruct *createPlayerInfo(SA_PARAM_NN_VALID Entity *pEnt)
{
	PlayerInfoStruct *pPlayerInfo = NULL;
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	if(pEnt->pPlayer && !entCheckFlag(pEnt, ENTITYFLAG_DESTROY) && (iPartitionIdx != PARTITION_ENT_BEING_DESTROYED))
	{
		GameAccountData *pAccountData;
		NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEnt);
		const char *pchPlayingStyles = SAFE_MEMBER4(pEnt, pPlayer, pUI, pLooseUI, pchPlayingStyles);
		pPlayerInfo = StructCreate(parse_PlayerInfoStruct);

		pPlayerInfo->onlineCharacterID = pEnt->myContainerID;
		pPlayerInfo->onlineCharacterAccessLevel = pEnt->pPlayer->accessLevel;
		pPlayerInfo->onlinePlayerName = StructAllocString(entGetLocalName(pEnt));
		pPlayerInfo->onlinePlayerAllegiance = allocAddString(REF_STRING_FROM_HANDLE(pEnt->hAllegiance));
		
		pPlayerInfo->gamePublicNameKey = StructAllocString(GetProductDisplayNameKey());
		pPlayerInfo->shardName = allocAddString(GetShardNameFromShardInfoString());
		pPlayerInfo->iPlayerLevel = entity_GetSavedExpLevel(pEnt);
		pPlayerInfo->iPlayerRank = Officer_GetRank(pEnt);
		if (pEnt->pChar) 
		{
			CharacterPath* pPath = entity_GetPrimaryCharacterPath(pEnt);
			pPlayerInfo->pchClassName = allocAddString(REF_STRING_FROM_HANDLE(pEnt->pChar->hClass));
			pPlayerInfo->pchPathName = pPath ? allocAddString(pPath->pchName) : NULL;
		}
		if (guild_IsMember(pEnt))
		{
			pPlayerInfo->iPlayerGuild = guild_GetGuildID(pEnt);
			if (devassert(pPlayerInfo->iPlayerGuild))
			{
				Guild *pGuild = guild_GetGuild(pEnt);
				GuildMember *pMember = guild_FindMember(pEnt);
				if (pGuild && pMember)
				{
					pPlayerInfo->bCanGuildChat = guild_HasPermission(pMember->iRank, pGuild, GuildPermission_Chat);
					pPlayerInfo->bIsOfficer = guild_HasPermission(pMember->iRank, pGuild, GuildPermission_OfficerChat);
				}
				else
					pPlayerInfo->bCanGuildChat = false;
			}
		}
		pPlayerInfo->iPlayerTeam = team_GetTeamID(pEnt);
		if (pPlayerInfo->iPlayerTeam)
		{
			Team *t = team_GetTeam(pEnt);
			if (t)
			{
				pPlayerInfo->iDifficulty = t->iDifficulty;
			}
			else
			{
				pPlayerInfo->iDifficulty = pEnt->pPlayer->iDifficulty;
			}
		}
		else
		{
			pPlayerInfo->iDifficulty = pEnt->pPlayer->iDifficulty;
		}

		if (team_IsTeamLeader(pEnt))
		{
			Team *pTeam = team_GetTeam(pEnt);
			if (pTeam)
			{
				if (team_NumTotalMembers(pTeam) >= TEAM_MAX_SIZE)
					pPlayerInfo->eTeamMode = TeamMode_Closed; // Team is full
				else
					pPlayerInfo->eTeamMode = pTeam->eMode;
				estrCopy2(&pPlayerInfo->pchTeamStatusMessage, pTeam->pchStatusMessage);
			}
			else
			{
				pPlayerInfo->eTeamMode = TeamMode_Prompt;
				estrClear(&pPlayerInfo->pchTeamStatusMessage);
			}
		}
		else
		{
			pPlayerInfo->eTeamMode = TeamMode_Prompt;
			estrClear(&pPlayerInfo->pchTeamStatusMessage);
		}

		pPlayerInfo->eLanguage = entGetLanguage(pEnt);
		pPlayerInfo->bIsGM = pEnt->pPlayer->bIsGM;
		pPlayerInfo->bIsDev = pEnt->pPlayer->bIsDev;

		if (pConfig && (pConfig->status & USERSTATUS_AUTOAFK))
			pPlayerInfo->bIsAutoAFK = true;
		pPlayerInfo->eLFGMode = pEnt->pPlayer->eLFGMode;
		pPlayerInfo->eLFGDifficultyMode = pEnt->pPlayer->eLFGDifficultyMode;
		
		if (pEnt->pPlayer->pchActivityString)
			pPlayerInfo->playerActivity = strdup(pEnt->pPlayer->pchActivityString);

		if (pchPlayingStyles && *pchPlayingStyles)
		{
			pPlayerInfo->playingStyles = allocAddString(pchPlayingStyles);
		}
		else
		{			
			pPlayerInfo->playingStyles = character_GetDefaultPlayingStyle(pEnt->pChar);
		}

		if(gGSLState.gameServerDescription.baseMapDescription.mapDescription &&
			gGSLState.gameServerDescription.baseMapDescription.mapDescription[0])
		{
			ZoneMapInfo *zminfo = worldGetZoneMapByPublicName(gGSLState.gameServerDescription.baseMapDescription.mapDescription);
			const char *mapName = zmapGetName(NULL);

			if (GAMESERVER_IS_UGCEDIT)
			{
				pPlayerInfo->playerMap.pchMapName = allocAddString(entTranslateMessageKey(pEnt, "UGC.TheFoundry"));
				pPlayerInfo->playerMap.pchMapNameMsgKey = allocAddString("UGC.TheFoundry");
				pPlayerInfo->playerMap.iMapInstance = 0;
			}
			else if (resNamespaceIsUGC(zmapGetName(NULL)))
			{
				pPlayerInfo->playerMap.pchMapName = allocAddString("UGC.FoundryMap");
				pPlayerInfo->playerMap.pchMapNameMsgKey = allocAddString("UGC.FoundryMap");
				pPlayerInfo->playerMap.iMapInstance = 0;
			}
			else
			{
				pPlayerInfo->playerMap.pchMapName = gGSLState.gameServerDescription.baseMapDescription.mapDescription; // treat as pooled string
				pPlayerInfo->playerMap.pchMapNameMsgKey = zminfo ? zmapInfoGetDisplayNameMsgKey(zminfo) : NULL; // treat as pooled string
				pPlayerInfo->playerMap.iMapInstance = partition_PublicInstanceIndexFromIdx(iPartitionIdx);
			}
			pPlayerInfo->playerMap.pchMapVars = StructAllocString(partition_MapVariablesFromIdx(iPartitionIdx));
			pPlayerInfo->playerMap.eMapType = zminfo ? zmapInfoGetMapType(zminfo) : ZMTYPE_UNSPECIFIED;
		}

		if (pEnt->currentNeighborhood) {
			Message *pMsg = GET_REF(pEnt->currentNeighborhood->hMessage);
			if  (pMsg) {
				pPlayerInfo->playerMap.pchNeighborhoodNameMsgKey = pMsg->pcMessageKey; // treat as pooled string
			}
		}
		pPlayerInfo->uVirtualShardID = GAMESERVER_VSHARD_ID;
		
		if(GamePermission_EntHasToken(pEnt, GAME_PERMISSION_CHAT_MAIL_FULL_RATE))
		{
			// full rates if not on or has token
			pPlayerInfo->eGamePermissionInfo = CHAT_GAME_PERMISSION_INFO_NONE;
		}
		else
		{
			// lower chat and mail rates
			pPlayerInfo->eGamePermissionInfo = CHAT_GAME_PERMISSION_INFO_RESTRICTED;		
		}
		pPlayerInfo->bSocialRestricted = !gslChat_UserCanChat(pEnt);

		pPlayerInfo->bGoldSubscriber = pEnt->pPlayer->playerType == kPlayerType_Premium;
		pAccountData = entity_GetGameAccount(pEnt);
		if (pAccountData)
		{
			EARRAY_FOREACH_BEGIN(pAccountData->eaAllPurchases, i);
			{
				MicroTransaction *trans = pAccountData->eaAllPurchases[i];
				int iNum = eaSize(&trans->ppPurchaseStamps);
				if (iNum > 0)
				{
					U32 uPurchaseTime = trans->ppPurchaseStamps[iNum-1]->uiPurchaseTime;
					if (uPurchaseTime > pPlayerInfo->uLastPurchase)
						pPlayerInfo->uLastPurchase = uPurchaseTime;
				}
			}
			EARRAY_FOREACH_END;
		}
	}

	return( pPlayerInfo );
}

void ServerChat_ReconnectToChatServer(void)
{
#ifndef USE_CHATRELAY
	PlayerInfoList list = {0};
	Entity* currEnt;
	EntityIterator* iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);

	// Make sure zone channel is created
	ServerChat_InitializeZoneChannel();

	while ((currEnt = EntityIteratorGetNext(iter)))
	{
		NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(currEnt);
		PlayerInfoStruct *pPlayerInfo = createPlayerInfo(currEnt);
		PlayerExtraInfo *pExtra = StructCreate(parse_PlayerExtraInfo);
		estrCopy2(&pExtra->pAccountName, currEnt->pPlayer->privateAccountName);
		estrCopy2(&pExtra->pDisplayName, currEnt->pPlayer->publicAccountName);
		pExtra->uAccessLevel = currEnt->pPlayer->accountAccessLevel;
		if (pConfig && pConfig->pchStatusMessage)
		{
			estrCopy2(&pExtra->pStatus, pConfig->pchStatusMessage);
		}

		eaiPush(&list.piAccountIDs, currEnt->pPlayer->accountID);
		eaPush(&list.ppPlayerNames, pExtra);
		eaPush(&list.ppPlayerInfos, pPlayerInfo);
	}
	EntityIteratorRelease(iter);
	RemoteCommand_ChatServerGroupLogin (GLOBALTYPE_CHATSERVER, 0, getZoneChannelName(), &list);
	eaiDestroy(&list.piAccountIDs);
	eaDestroyStruct(&list.ppPlayerNames, parse_PlayerExtraInfo);
	eaDestroyStruct(&list.ppPlayerInfos, parse_PlayerInfoStruct);
#endif
}

void ServerChat_PlayerUpdate(Entity *pEnt, ChatUserUpdateEnum eForwardToGlobalFriends)
{
	PlayerInfoStruct *pPlayerInfo = NULL;
	if (pEnt && pEnt->pPlayer)
	{
		//Create the PlayerInfoStruct
		pPlayerInfo = createPlayerInfo(pEnt);
		if(pPlayerInfo)
		{
			RemoteCommand_ChatServerPlayerUpdateEx(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, pPlayerInfo, eForwardToGlobalFriends);
			StructDestroy(parse_PlayerInfoStruct, pPlayerInfo);
		}
	}
}

void ServerChat_StatusUpdate(Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer)
	{
		NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEnt);

		if(pConfig)
		{
			RemoteCommand_UserSetStatus(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, pConfig->pchStatusMessage);
		}
	}
}

#ifndef USE_CHATRELAY
void ServerChat_LoginSucceeded(ContainerID entID)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entID);
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEnt);
	int i,size;
	if (!pEnt || !pConfig)
		return;
	// These are things that can't happen until the player has actually logged in to 
	// the global chat server, otherwise they are lost.
	if (pConfig->status & USERSTATUS_FRIENDSONLY)
	{
		ServerChat_SetFriendsOnly(pEnt);
	}
	else if (pConfig->status & USERSTATUS_HIDDEN)
	{
		ServerChat_SetHidden(pEnt);
	}

	ServerChat_RefreshClientNameLists(pEnt);
	ServerChat_PlayerUpdate(pEnt, CHATUSER_UPDATE_GLOBAL);
	ServerChat_StatusUpdate(pEnt);

	size = eaSize(&pConfig->ppShardGlobalChannels);
	for (i=0; i<size; i++)
	{
		ServerChat_JoinSpecialChannel(pEnt, pConfig->ppShardGlobalChannels[i]);
	}
}
#endif

void ServerChat_Login(Entity *pEnt)
{
	PERFINFO_AUTO_START_FUNC();

	if (pEnt && pEnt->pPlayer)
	{
		GameAccountData *gad = entity_GetGameAccount(pEnt);

#ifdef USE_CHATRELAY
		NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEnt);
		// Send updates - should already be logged in
		if (pConfig->status & USERSTATUS_FRIENDSONLY)
			ServerChat_SetFriendsOnly(pEnt);
		else if (pConfig->status & USERSTATUS_HIDDEN)
			ServerChat_SetHidden(pEnt);
		ServerChat_PlayerUpdate(pEnt, CHATUSER_UPDATE_GLOBAL);
		ServerChat_StatusUpdate(pEnt);
		ServerChat_RefreshClientNameLists(pEnt);
#endif
#ifndef USE_CHATRELAY
		RemoteCommand_ChatServerLogin(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, pEnt->myContainerID, 
			pEnt->pPlayer->privateAccountName, pEnt->pPlayer->publicAccountName[0] ? pEnt->pPlayer->publicAccountName : pEnt->pPlayer->privateAccountName, 
			pEnt->pPlayer->accessLevel);
		// This command can take an indeterminate amount of time, depending on global chat server lag
		// It is NOT safe to queue up updates to the local chat server
		// The ChatServer will call ServerChat_LoginSucceeded after the player is logged in
#endif

		if(gad)
		{
			RemoteCommand_ChatServerUserLoggedIn(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID);
		}

		if (!pEnt->pPlayer->pUI->pLooseUI->pchPlayingStyles && pEnt->pChar)
		{
			// If we don't already have a playing style set, set it from the character
			const char *pchStyles = character_GetDefaultPlayingStyle(pEnt->pChar);

			if (pchStyles)
			{			
				ServerChat_SetPlayingStyles(pEnt, pchStyles, false);
			}
		}

		ServerChatConfig_PlayerUpdate(pEnt);
		// A player that just logged in can't be away.
		if (pEnt->pPlayer->pUI && (pEnt->pPlayer->pUI->pChatConfig->status & USERSTATUS_AUTOAFK) != 0)
		{
			GameServerParsePublic("AutoAFKReturn", 0, pEnt, NULL, -1, CMD_CONTEXT_HOWCALLED_UNSPECIFIED, NULL);
		}
		
		if (!GAMESERVER_IS_UGCEDIT)
			RemoteCommand_ChatServerJoinChannel(GLOBALTYPE_CHATSERVER, 0, entGetAccountID(pEnt), getZoneChannelName(pEnt), CHANNEL_SPECIAL_ZONE, true);
		// It doesn't need to request channels here because login already sends them
	}

	PERFINFO_AUTO_STOP();
}


//AUTO_TRANSACTION;
//enumTransactionOutcome ServerChat_SetXUIDTrans(ATR_ARGS, NOCONST(Entity) *pEnt, U64 xuid)
//{
//
//	pEnt->pPlayer->xuid = xuid;
//	return TRANSACTION_OUTCOME_SUCCESS;
//
//}

//AUTO_COMMAND ACMD_SERVERCMD;
//void ServerChat_SetXUID(Entity *pEnt, U64 xuid)
//{
//	//AutoTrans_ServerChat_SetXUIDTrans(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYPLAYER, 
//	//			pEnt->myContainerID, xuid);
//	pEnt->pPlayer->xuid = xuid;
//	printf("%"FORM_LL"d - %s\n", pEnt->pPlayer->xuid, pEnt->name);
//}

void ServerChat_PlayerEnteredMap(Entity *pEnt)
{
	PERFINFO_AUTO_START_FUNC();

	if (pEnt->pPlayer->eLFGMode == TeamMode_Prompt)
	{
		pEnt->pPlayer->eLFGMode = TeamMode_Open;
	}
	ServerChat_Login(pEnt);

	PERFINFO_AUTO_STOP();
}

void ServerChat_PlayerLeftMap(Entity *pEnt)
{
	if (!GAMESERVER_IS_UGCEDIT)
		RemoteCommand_ChatServerLeaveChannel(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, getZoneChannelName(pEnt));
	// Chat Logout occurs when the Player Entity is returned to the ObjectDB
}


#if ContainerID == U32
	#define EAPUSH(ea, val) eaiPush((ea), (val))
	#define EAFIND(ea, val) eaiFind((ea), (val))
	#define EAFINDANDREMOVE(ea, val) eaiFindAndRemove((ea), (val))
#elif ContainerID == U64
	#define EAPUSH(ea, val) eai64Push((ea), (val))
	#define EAFIND(ea, val) eai64Find((ea), (val))
	#define EAFINDANDREMOVE(ea, val) eai64FindAndRemove((ea), (val))
#else
	STATIC_ASSERT(!"Unknown ContainerID size");
#endif

#ifdef EAPUSH
#undef EAPUSH
#undef EAFIND
#undef EAFINDANDREMOVE
#endif

extern int ServerChat_SendChatMessage(SA_PARAM_OP_VALID Entity *pToEnt, ChatLogEntryType eType, SA_PARAM_OP_VALID const char *pchText, SA_PARAM_OP_VALID const ChatData *pData) {
	ChatMessage *pMsg = ChatCommon_CreateMsg(NULL, NULL, eType, NULL, pchText, pData);
	int retval = CHATENTITY_NOTFOUND;
	if (pToEnt) {
		if (!characterIsTransferring(pToEnt))
		{
			ClientCmd_cmdChatLog_AddMessage(pToEnt, pMsg);
			retval = CHATENTITY_SUCCESS;
		}
		else
			retval = CHATENTITY_MAPTRANSFERRING;
	}
	StructDestroy(parse_ChatMessage, pMsg);
	return retval;
}

#ifndef USE_CHATRELAY
// Change Channel Level permissions
void ServerChat_ModifyChannelPermissions(Entity *pEnt, const char *channel_name, ChannelUserLevel eLevel, U32 uPermissions)
{
	if (channel_name && pEnt && pEnt->pPlayer && !strStartsWith(channel_name, TEAM_CHANNEL_PREFIX))
	{
		if (!GameServer_RequireGlobalChat(pEnt))
			return;
		RemoteCommand_ChatServerSetChannelLevel(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, channel_name, eLevel, uPermissions);
	}
}
#endif

// Create and join a new channel
void ServerChat_CreateChannel(Entity *pEnt, const char *channel_name)
{
	if (channel_name && pEnt && pEnt->pPlayer)
	{
		if (!GameServer_RequireGlobalChat(pEnt))
			return;
		RemoteCommand_ChatServerCreateChannel(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, channel_name, CHANNEL_SPECIAL_NONE, 0);
	}
}

#ifndef USE_CHATRELAY
// Destroy a channel
void ServerChat_DestroyChannel(Entity *pEnt, const char *channel_name) {
	if (channel_name && pEnt && pEnt->pPlayer)
	{
		if (!GameServer_RequireGlobalChat(pEnt))
			return;
		RemoteCommand_ChatServerDestroyChannel(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, channel_name);
	}
}


// Join a channel (must already exist)
void ServerChat_JoinChannel(Entity *pEnt, const char *channel_name, bool bCreate)
{
	if (channel_name && pEnt && pEnt->pPlayer)
	{
		if (!GameServer_RequireGlobalChat(pEnt))
			return;
		RemoteCommand_ChatServerJoinChannel(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, channel_name, CHANNEL_SPECIAL_NONE, bCreate);
	}
}

// Leave a channel
void ServerChat_LeaveChannel(Entity *pEnt, const char *channel_name)
{
	// not allowed to unsubscribe from built-ins
	if (ChatCommon_IsBuiltInChannel(channel_name)) {
		return;
	}

	if (channel_name && pEnt && pEnt->pPlayer)
	{
		if (!GameServer_RequireGlobalChat(pEnt))
			return;
		RemoteCommand_ChatServerLeaveChannel(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, channel_name);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(channel_uninvite) ACMD_ACCESSLEVEL(0);
void cmdServerChat_Uninvite(Entity *pEnt, const char *channel_name, char *chatHandle)
{
	if (pEnt && pEnt->pPlayer)
	{
		const char *pchHandle = strchr(chatHandle, '@');
		if (!GameServer_RequireGlobalChat(pEnt))
			return;
		// Don't include character name or @ in handle.
		pchHandle = pchHandle ? pchHandle+1 : chatHandle;
		RemoteCommand_ChatServerChannelUninviteByHandle(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, pchHandle, channel_name);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(channel_decline_invite) ACMD_ACCESSLEVEL(0);
void cmdServerChat_DeclineInviteToChannel(Entity *pEnt, const char *channel_name)
{	
	if (pEnt && pEnt->pPlayer)
	{
		if (!GameServer_RequireGlobalChat(pEnt))
			return;
		RemoteCommand_ChatServerDeclineChannelInvite(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, channel_name);
	}
}
#endif

// Invite a player to join the channel (operator only)
void ServerChat_InvitePlayerToChannel(Entity *pEnt, const char *channel_name, ContainerID playerID)
{
	Entity *pInvitee = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, playerID);

	if (pEnt && pInvitee && pEnt->pPlayer && pInvitee->pPlayer)
	{
		if (!GameServer_RequireGlobalChat(pEnt))
			return;
		RemoteCommand_ChatServerInviteByID(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, 
			entGetPersistedName(pEnt), channel_name, pInvitee->pPlayer->accountID);
	}
}

#ifndef USE_CHATRELAY
// Invite a user to join the channel (operator only)
void ServerChat_InviteChatHandleToChannel(Entity *pEnt, const char *channel_name, char *chatHandle)
{
	if (pEnt && pEnt->pPlayer && chatHandle && *chatHandle)
	{
		const char *pchHandle = strchr(chatHandle, '@');
		if (!GameServer_RequireGlobalChat(pEnt))
			return;
		
		// Don't include character name or @ in handle.
		pchHandle = pchHandle ? pchHandle+1 : chatHandle;

		RemoteCommand_ChatServerInviteByHandle(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, 
			entGetPersistedName(pEnt), channel_name, pchHandle);
	}
}

// List the channels the player is watching
void ServerChat_ChannelList(Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer)
	{
		RemoteCommand_ChatServerChannelList(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID);
	}
}

// List the members of the channel
void ServerChat_ChannelListMembers(Entity *pEnt, const char *channel_name)
{
	if (pEnt && pEnt->pPlayer)
	{
		RemoteCommand_ChatServerChannelListMembers(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, channel_name);
	}
}

// Add a new channel Message of the Day (operator only)
void ServerChat_SetChannelMotd(Entity *pEnt, const char *channel_name, const ACMD_SENTENCE motd)
{
	if (channel_name && motd && pEnt && pEnt->pPlayer)
	{
		if (!GameServer_RequireGlobalChat(pEnt))
			return;
		RemoteCommand_ChatServerSetMotd(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, channel_name, motd);
	}
}

// Set the channel description (operator only)
void ServerChat_SetChannelDescription(Entity *pEnt, const char *channel_name, const ACMD_SENTENCE description)
{
	if (channel_name && description && pEnt && pEnt->pPlayer)
	{
		if (!GameServer_RequireGlobalChat(pEnt))
			return;
		RemoteCommand_ChatServerSetChannelDescription(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, channel_name, description);
	}
}

// Set the channel's access level (operator only)
void ServerChat_SetChannelAccess(Entity *pEnt, const char *channel_name, char *accessString)
{
	if (pEnt && pEnt->pPlayer)
	{
		if (!GameServer_RequireGlobalChat(pEnt))
			return;
		RemoteCommand_ChatServerSetChannelAccess(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, channel_name, accessString);
	}
}
#endif

static void ServerChat_TellError(ChatMessage *pMsg, char *pchError) {
	if (pMsg) {
		char *pchFrom = NULL;
		char *pchTo = NULL;

		if (pMsg->pFrom) {
			estrPrintf(&pchFrom, "%s@%s [%d@%d]", 
				pMsg->pFrom->pchName ? pMsg->pFrom->pchName : "",
				pMsg->pFrom->pchHandle ? pMsg->pFrom->pchHandle : "",
				pMsg->pFrom->playerID,
				pMsg->pFrom->accountID
				);
		} else {
			estrCopy2(&pchFrom, "NULL");
		}
		if (pMsg->pTo) {
			estrPrintf(&pchTo, "%s@%s [%d@%d]", 
				pMsg->pTo->pchName ? pMsg->pTo->pchName : "",
				pMsg->pTo->pchHandle ? pMsg->pTo->pchHandle : "",
				pMsg->pTo->playerID,
				pMsg->pTo->accountID
				);
		} else {
			estrCopy2(&pchTo, "NULL");
		}

		Errorf("TELL ERROR - %s (From: '%s' To: '%s')", pchError, pchFrom, pchTo);
		estrDestroy(&pchFrom);
		estrDestroy(&pchTo);
	} else {
		Errorf("TELL ERROR - NULL ChatMessage");
	}

}

static void ServerChat_SendTell(Entity *pEnt, ChatMessage *pMsg)
{
	if (pEnt && pMsg && pMsg->pFrom && pMsg->pTo) {
		if (!GameServer_RequireGlobalChat(pEnt))
			return;
		if ((pMsg->pTo->pchHandle && *pMsg->pTo->pchHandle) || pMsg->pTo->accountID) {
			// Send by handle
			if (!gslChat_UserCanChat(pEnt)) {
				// If the player is on a trial account make sure the target is on their friend list
				S32 i = 0;
				if (SAFE_MEMBER4(pEnt, pPlayer, pUI, pChatState, eaFriends)) {
					for (i = 0; i < eaSize(&pEnt->pPlayer->pUI->pChatState->eaFriends); i++) {
						ChatPlayerStruct *pFriend = pEnt->pPlayer->pUI->pChatState->eaFriends[i];
						if (!ChatFlagIsFriend(pFriend->flags))
							continue;
						if (pFriend->accountID == pMsg->pTo->accountID || !stricmp(NULL_TO_EMPTY(pFriend->chatHandle), NULL_TO_EMPTY(pMsg->pTo->pchHandle))) {
							break;
						}
					}
				}
				if (i == eaSize(&pEnt->pPlayer->pUI->pChatState->eaFriends)) {
					notify_NotifySend(pEnt, kNotifyType_Failed, entTranslateMessageKey(pEnt, "Chat_NoTrialTells"), NULL, NULL);
					return;
				} else {
					RemoteCommand_ChatServerPrivateMesssage_Shard(GLOBALTYPE_CHATSERVER, 0, pMsg);
				}
			} else {
				RemoteCommand_ChatServerPrivateMesssage_Shard(GLOBALTYPE_CHATSERVER, 0, pMsg);
			}
		} else if (pMsg->pTo->pchName && *pMsg->pTo->pchName) {
			// Send by character name - Not supported.  A handled MUST be provided
			char *error = NULL;
			entFormatGameMessageKey(pEnt, &error, "ChatServer_Local_UserDNE", STRFMT_STRING("User", pMsg->pTo->pchName), STRFMT_END);
			if (!error) {
				estrCopy2(&error, "User does not exist or there are multiple users with the character name");
			}

			notify_NotifySend(pEnt, kNotifyType_ChatLookupError, error, NULL, NULL);
			estrDestroy(&error);
		} else {
			ServerChat_TellError(pMsg, "Tell received with no TO name or handle");
		}
	} else {
		ServerChat_TellError(pMsg, "Tell received by GameServer with some NULL data");
	}
}

// Send chat to other players in your vicinity
static void ServerChat_SendLocalChatInternal(Entity *pFromEnt, const ChatMessage *pMsg)
{
	int i;
	Vec3 pos;
	Entity* pToEnt;
	static Entity** ents = NULL;
	ContainerID *piAccountIDs = NULL;

	if (!GameServer_RequireGlobalChat(pFromEnt))
		return;
	if (!pFromEnt || !pFromEnt->pPlayer || !pMsg) {
		return;
	}

	eaClear(&ents);
	entGetPos(pFromEnt, pos);
	entGridProximityLookupExEArray(entGetPartitionIdx(pFromEnt), pos, &ents, LOCAL_CHAT_DIST, 0, ENTITYFLAG_IGNORE, pFromEnt);

	for(i=eaSize(&ents)-1; i>=0; i--)
	{
		pToEnt = ents[i];
		switch(entGetType(pToEnt))
		{
			xcase GLOBALTYPE_ENTITYPLAYER: {
				eaiPush(&piAccountIDs, pToEnt->pPlayer->accountID);
			}
			xcase GLOBALTYPE_ENTITYCRITTER:
			acase GLOBALTYPE_ENTITYSAVEDPET: {
				aiMessageProcessChat(pToEnt, pFromEnt, pMsg->pchText);
			}
		}
	}

	if (piAccountIDs)
	{
		ChatContainerIDList idList = {0};
		idList.piContainerIDList = piAccountIDs;
		RemoteCommand_ChatServerBatchSendLocalChat_Shard(GLOBALTYPE_CHATSERVER, 0, &idList, pMsg);
		eaiDestroy(&piAccountIDs);
	}
}

void ServerChat_SendMessage(Entity *pFromEnt, SA_PARAM_NN_VALID ChatMessage *pMsg) {
	char pchDynamicChannel[1024];
	const char *pchPlayerName = NULL;
	const char *pchTargetName = NULL;
	Entity *pTarget = NULL;
	static char *s_pchNotifyResponse;

	if (!pMsg) {
		return;
	}

	pchDynamicChannel[0] = 0;

	estrClear(&s_pchNotifyResponse);
	
	if (pMsg->eType == kChatLogEntryType_Zone && gamePermission_Enabled() && !GamePermission_EntHasToken(pFromEnt, GAME_PERMISSION_CAN_SEND_ZONE)) {
		entFormatGameMessageKey(pFromEnt, &s_pchNotifyResponse, "Chat_NoTrialZoneChat", STRFMT_END);
		notify_NotifySend(pFromEnt, kNotifyType_Failed, s_pchNotifyResponse, NULL, NULL);
		return;
	}
	
	// Make sure the message is trimmed properly and that all newlines are converted to spaces
	estrTrimLeadingAndTrailingWhitespace(&pMsg->pchText);
	estrReplaceOccurrences(&pMsg->pchText, "\n", " ");

	pchPlayerName = entGetLangName(pFromEnt, entGetLanguage(pFromEnt));
	if ((pTarget = entity_GetTarget(pFromEnt)))
		pchTargetName = entGetLangName(pTarget, entGetLanguage(pFromEnt));
	if (!pchPlayerName)
		pchPlayerName = "";
	if (!pchTargetName)
		pchTargetName = "";
	estrReplaceOccurrences_CaseInsensitive(&pMsg->pchText, "$playername", pchPlayerName);
	estrReplaceOccurrences_CaseInsensitive(&pMsg->pchText, "$player", pchPlayerName);
	estrReplaceOccurrences_CaseInsensitive(&pMsg->pchText, "$target", pchTargetName);

	// The client sent a "from" field, but we just ignore it and use our own idea of 
	//  who the player is.
	if ( pMsg->pFrom != NULL )
	{
		// free up any "from" field that the client might have sent
		StructDestroy(parse_ChatUserInfo, pMsg->pFrom);
	}

	// Fill in the "from" fields based on the player entity associated with
	// the client that the request came from.  We don't trust the client to
	// fill in the "from" fields, because a hacked client could spoof chat
	// messages from other players.
	pMsg->pFrom = ServerChat_CreateLocalizedUserInfoFromEnt(pFromEnt, NULL);
	if ( pMsg->pFrom == NULL )
	{
		return;
	}

	// Fixup message channel for special (dynamic) channels
	switch (pMsg->eType) {
		case kChatLogEntryType_Zone: {
			if (GAMESERVER_IS_UGCEDIT)
				estrCopy2(&pMsg->pchChannel, UGCEDIT_CHANNEL_NAME);
			else
				estrCopy2(&pMsg->pchChannel, getZoneChannelName(pFromEnt));
			pMsg->iInstanceIndex =partition_PublicInstanceIndexFromIdx(entGetPartitionIdx(pFromEnt));
		}
		xcase kChatLogEntryType_Team: {
			if (team_IsMember(pFromEnt) && pFromEnt->pTeam->iInChat) {
				team_MakeTeamChannelNameFromID(SAFESTR(pchDynamicChannel), pFromEnt->pTeam->iInChat);
			}
			else
			{
				entFormatGameMessageKey(pFromEnt, &s_pchNotifyResponse, "Chat_TeamError", STRFMT_END);
				notify_NotifySend(pFromEnt, kNotifyType_Failed, s_pchNotifyResponse, NULL, NULL);
				return;
			}
		}
		xcase kChatLogEntryType_Guild: {
			if (pFromEnt->pPlayer && pFromEnt->pPlayer->pGuild && pFromEnt->pPlayer->pGuild->iGuildChat) {
				guild_GetGuildChannelNameFromID(SAFESTR(pchDynamicChannel), pFromEnt->pPlayer->pGuild->iGuildChat, GAMESERVER_VSHARD_ID);
			}
			else
			{
				entFormatGameMessageKey(pFromEnt, &s_pchNotifyResponse, "Chat_GuildError", STRFMT_END);
				notify_NotifySend(pFromEnt, kNotifyType_Failed, s_pchNotifyResponse, NULL, NULL);
				return;
			}
		}
		xcase kChatLogEntryType_Officer: {
			if (pFromEnt->pPlayer && pFromEnt->pPlayer->pGuild && pFromEnt->pPlayer->pGuild->iGuildChat) {
				guild_GetOfficerChannelNameFromID(SAFESTR(pchDynamicChannel), pFromEnt->pPlayer->pGuild->iGuildChat, GAMESERVER_VSHARD_ID);
			}
			else
			{
				entFormatGameMessageKey(pFromEnt, &s_pchNotifyResponse, "Chat_OfficerError", STRFMT_END);
				notify_NotifySend(pFromEnt, kNotifyType_Failed, s_pchNotifyResponse, NULL, NULL);
				return;
			}
			break;
		}
		xdefault:
			// Do nothing
			break;
	}

	if (*pchDynamicChannel) {
		estrCopy2(&pMsg->pchChannel, pchDynamicChannel);
	}

	// Now send the message to either the local area or to a specific channel/tell
	if (pMsg->eType == kChatLogEntryType_Private) {
		ServerChat_SendTell(pFromEnt, pMsg);
	} else if (CHATTYPE_ISLOCAL(pMsg->eType)) {
		// Local messages need to run through AI and look up nearby ents.
		ServerChat_SendLocalChatInternal(pFromEnt, pMsg);
	} else {
		RemoteCommand_ChatServerMessageReceive_Shard(GLOBALTYPE_CHATSERVER, 0, pMsg);
	}
}

// Send emote chat to other players in your vicinity
void ServerChat_SendEmoteChatMsg(Entity *pFromEnt, const char *pchText, const ChatData *pData)
{
	ChatMessage *pMsg = ChatCommon_CreateMsg(NULL, NULL, kChatLogEntryType_Emote, LOCAL_CHANNEL_NAME, pchText, pData);
	if (pMsg)
		ServerChat_SendMessage(pFromEnt, pMsg);
	StructDestroy(parse_ChatMessage, pMsg);
}

// Set the user's access permissions for a channel (operator only)
void ServerChat_SetUserAccess(Entity *pEnt, const char *channel_name, char *targetHandle, U32 uAddFlags, U32 uRemoveFlags)
{
	if (pEnt && pEnt->pPlayer)
	{
		if (!GameServer_RequireGlobalChat(pEnt))
			return;
		RemoteCommand_ChatServerSetUserAccessByHandleNew(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, channel_name, 
			targetHandle, uAddFlags, uRemoveFlags);
	}
}

// Chat Relay ONLY uses this for filter setting
extern const char ***ServerChat_GetSubscribedCustomChannels(Entity *pEntity) {
	if (pEntity && pEntity->pPlayer) {
		return &pEntity->pPlayer->eaSubscribedChannels;
	}

	return NULL;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void ServerChat_SetSubscribedCustomChannels (Entity *pEnt, ChatChannelInfoList *pChannels)
{
	if (!pEnt || !pEnt->pPlayer)
		return;
	if (pEnt->pPlayer->eaSubscribedChannels)
	{
		EARRAY_FOREACH_BEGIN(pEnt->pPlayer->eaSubscribedChannels, i);
		{
			free(pEnt->pPlayer->eaSubscribedChannels[i]);
		}
		EARRAY_FOREACH_END;
		eaDestroy(&pEnt->pPlayer->eaSubscribedChannels);
	}
	EARRAY_FOREACH_BEGIN(pChannels->ppChannels, i);
	{
		if (pChannels->ppChannels[i]->pName)
			eaPush(&pEnt->pPlayer->eaSubscribedChannels, strdup(pChannels->ppChannels[i]->pName));
	}
	EARRAY_FOREACH_END;
}

#ifndef USE_CHATRELAY
extern int ServerChat_MessageReceive(ContainerID targetID, SA_PARAM_NN_VALID ChatMessage *pMsg) {
	if (targetID && pMsg) {
		Entity *pTargetEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, targetID);
		if (pTargetEntity) {
			if (!characterIsTransferring(pTargetEntity))
			{
				ClientCmd_cmdClientChat_MessageReceive(pTargetEntity, pMsg);
				return CHATENTITY_SUCCESS;
			}
			return CHATENTITY_MAPTRANSFERRING;
		}
		return CHATENTITY_NOTFOUND;
	}
	return CHATENTITY_SUCCESS;
}

extern int ServerChat_SendSystemAlert(ContainerID entID, const char *title, const char *text)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entID);
	if (pEnt)
	{
		if (!characterIsTransferring(pEnt))
		{
			notify_NotifySend(pEnt, kNotifyType_ServerBroadcast, text, NULL, NULL);
			return CHATENTITY_SUCCESS;
		}
		return CHATENTITY_MAPTRANSFERRING;
	}
	return CHATENTITY_NOTFOUND;
}
#endif

extern int ServerChat_SendChannelSystemMessage(ContainerID entID, int eType, const char *channel_name, const char *msg)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entID);
	if (pEnt)
	{
		if (!characterIsTransferring(pEnt))
		{
			ChatMessage *pMsg = ChatCommon_CreateMsg(NULL, NULL, eType, channel_name, msg, NULL);
			ClientCmd_cmdChatLog_AddMessage(pEnt, pMsg);
			StructDestroy(parse_ChatMessage, pMsg);
			return CHATENTITY_SUCCESS;
		}
		return CHATENTITY_MAPTRANSFERRING;
	}
	return CHATENTITY_NOTFOUND;
}

// Send an admin message to all players on the server
void ServerChat_BroadcastMessage(ACMD_SENTENCE msg, S32 eNotifyType)
{
	Entity* currEnt;
	EntityIterator* iter;

	if (!msg || !*msg)
		return;

	iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);

	while ((currEnt = EntityIteratorGetNext(iter)))
	{
		notify_NotifySend(currEnt, (NotifyType)eNotifyType, msg, NULL, NULL);
	}
	EntityIteratorRelease(iter);
}

bool ServerChat_ExtractRange(const char *pchToken, S32 *piRangeMin, S32 *piRangeMax)
{
	S32 tmp;

	piRangeMin = FIRST_IF_SET(piRangeMin, &tmp);
	piRangeMax = FIRST_IF_SET(piRangeMax, &tmp);

	if (isdigit(*pchToken)) {
		*piRangeMin = 0;
		*piRangeMax = -1;

		while (isdigit(*pchToken)) {
			*piRangeMin = (*piRangeMin * 10) + (*pchToken - '0');
			pchToken++;
		}

		if (*pchToken == '-') {
			*pchToken++;

			if (isdigit(*pchToken)) {
				*piRangeMax = 0;

				while (isdigit(*pchToken)) {
					*piRangeMax = (*piRangeMax * 10) + (*pchToken - '0');
					pchToken++;
				}
			}
		}

		if (*pchToken != '\0' && *pchToken != ' ') {
			return false;
		}

		MAX1(*piRangeMax, *piRangeMin);
		return true;
	}

	return false;
}

void ServerChat_AddFilterToken(int iPartitionIdx, PlayerFindFilterStruct *pFilters, const char **pchToken)
{
	bool bQuoted = false;
	bool bExact = false;
	bool bNot = false;
	char last = '\0';
	char *estrTag = NULL;
	const char *pchBacktrack = NULL;
	FindFilterTokenStruct *pToken = NULL;
	FindFilterTokenStruct *pAccountToken = NULL;

	while (**pchToken == ' ' || **pchToken == ',')
		(*pchToken)++;

	if (**pchToken == '+' || **pchToken == '-') {
		bNot = (**pchToken == '-');
		(*pchToken)++;
	}

	if (**pchToken == '=') {
		bExact = true;
		(*pchToken)++;
	}

	pchBacktrack = *pchToken;
	estrStackCreate(&estrTag);
	while (**pchToken != '\0' && **pchToken != ':' && **pchToken != '\"' && **pchToken != ' ') {
		estrConcatChar(&estrTag, **pchToken);
		(*pchToken)++;
	}
	if (**pchToken != ':') {
		*pchToken = pchBacktrack;
		estrDestroy(&estrTag);
	} else {
		(*pchToken)++;
	}

	if (**pchToken == '\"') {
		bQuoted = true;
		(*pchToken)++;
	}

	while (**pchToken != '\0') {
		if (bQuoted && **pchToken == '\"') {
			(*pchToken)++;
			break;
		}

		if (!bQuoted && **pchToken == ' ') {
			break;
		}

		if (**pchToken != ' ' || (last != ' ' && last != '\0'))
		{
			if (!pToken) {
				pToken = StructCreate(parse_FindFilterTokenStruct);
			}

			last = **pchToken;
			estrConcatChar(&pToken->pchToken, last);
		}

		(*pchToken)++;
	}

	if (pToken)
	{
		S32 iLow, iHigh;
		pToken->bExact = bExact;

		if (pToken->pchToken[estrLength(&pToken->pchToken) - 1] == ' ') {
			estrRemove(&pToken->pchToken, estrLength(&pToken->pchToken) - 1, 1);
		}

		if (!estrTag || !*estrTag)
		{
			pToken->bSoft = true;
			if (strchr(pToken->pchToken, '@'))
			{
				U32 uiAt = 0;

				while (pToken->pchToken[uiAt] != '@')
					uiAt++;

				if (uiAt == 0)
				{
					estrRemove(&pToken->pchToken, 0, 1);
					pToken->bCheckHandle = true;
				}
				else
				{
					if (pToken->pchToken[uiAt + 1] != '\0') {
						pAccountToken = StructClone(parse_FindFilterTokenStruct, pToken);
						estrRemove(&pAccountToken->pchToken, 0, uiAt + 1);
						pAccountToken->bCheckHandle = true;
					}

					estrRemove(&pToken->pchToken, uiAt, estrLength(&pToken->pchToken) - uiAt);
					pToken->bCheckName = true;
				}
			}
			else if (!bQuoted && ServerChat_ExtractRange(pToken->pchToken, &iLow, &iHigh))
			{
				pToken->iRangeLow = iLow;
				pToken->iRangeHigh = iHigh;
				estrDestroy(&pToken->pchToken);

				pToken->bCheckRank = true;
			}
			else if (!bQuoted && *pToken->pchToken == '#' &&
				ServerChat_ExtractRange(pToken->pchToken + 1, &iLow, &iHigh))
			{
				pToken->iRangeLow = iLow;
				pToken->iRangeHigh = iHigh;
				estrDestroy(&pToken->pchToken);

				pToken->bCheckInstance = true;
			}
			else if (!bQuoted && (!stricmp(pToken->pchToken, "open") || !stricmp(pToken->pchToken, "lfg")))
			{
				pToken->bFlag = true;
				estrDestroy(&pToken->pchToken);

				pToken->bCheckOpen = true;
			}
			else if (!bQuoted && (!stricmp(pToken->pchToken, "request")))
			{
				pToken->bFlag = true;
				estrDestroy(&pToken->pchToken);

				pToken->bCheckRequestOnly = true;
			}
			else if (!bQuoted && (!stricmp(pToken->pchToken, "closed")))
			{
				pToken->bFlag = true;
				estrDestroy(&pToken->pchToken);

				pToken->bCheckClosed = true;
			}
			else
			{
				pToken->bCheckPlayingStyles = true;
				pToken->bCheckName = true;
				pToken->bCheckHandle = true;
				pToken->bCheckMap = true;
				pToken->bCheckNeighborhood = true;
				pToken->bCheckGuild = true;
				pToken->bCheckStatus = true;
			}
		}
		// TODO: make non-hardcoded
		else if (!stricmp(estrTag, "name"))
			pToken->bCheckName = true;
		else if (!stricmp(estrTag, "handle"))
			pToken->bCheckHandle = true;
		else if (!stricmp(estrTag, "map"))
			pToken->bCheckMap = true;
		else if (!stricmp(estrTag, "neighborhood") || !stricmp(estrTag, "'hood"))
			pToken->bCheckNeighborhood = true;
		else if (!stricmp(estrTag, "guild") || !stricmp(estrTag, "sg") || !stricmp(estrTag, "fleet"))
			pToken->bCheckGuild = true;
		else if (!stricmp(estrTag, "status"))
			pToken->bCheckStatus = true;
		else if (!stricmp(estrTag, "role") || !stricmp(estrTag, "class"))
			pToken->bCheckPlayingStyles = true;
		else if (!stricmp(estrTag, "instance") && (!stricmp(pToken->pchToken, "mine") || !stricmp(pToken->pchToken, "current")))
		{
			pToken->iRangeLow = pToken->iRangeHigh = partition_PublicInstanceIndexFromIdx(iPartitionIdx);
			estrDestroy(&pToken->pchToken);

			pToken->bCheckInstance = true;
		}
		else if (ServerChat_ExtractRange(pToken->pchToken, &iLow, &iHigh))
		{
			pToken->iRangeLow = iLow;
			pToken->iRangeHigh = iHigh;
			estrDestroy(&pToken->pchToken);

			if (!stricmp(estrTag, "level") || !stricmp(estrTag, "lvl"))
				pToken->bCheckRank = true;
			else if (!stricmp(estrTag, "instance"))
				pToken->bCheckInstance = true;
		}
		else
		{
			StructDestroy(parse_FindFilterTokenStruct, pToken);
			pToken = NULL;
		}

		if (pToken)
		{
			if (bNot)
			{
				eaPush(&pFilters->eaExcludeFilters, pToken);
				if (pAccountToken)
					eaPush(&pFilters->eaExcludeFilters, pAccountToken);
			}
			else
			{
				eaPush(&pFilters->eaRequiredFilters, pToken);
				if (pAccountToken)
					eaPush(&pFilters->eaRequiredFilters, pAccountToken);
			}
		}
	}

	if (estrTag) {
		estrDestroy(&estrTag);
	}
}

void ServerChat_CombineFilters(FindFilterTokenStruct ***peaTokenList)
{
	int iRankFilters = 0;
	int iInstanceFilters = 0;
	FindFilterTokenStruct *pRankFilters = NULL;
	FindFilterTokenStruct *pInstanceFilters = NULL;
	int i;

	// Join level searches
	for (i = eaSize(peaTokenList) - 1; i >= 0; i--)
	{
		if ((*peaTokenList)[i]->bCheckRank)
			// exclusively check rank/skill level
			iRankFilters++;
		if ((*peaTokenList)[i]->bCheckInstance)
			// exclusively check map instance
			iInstanceFilters++;
	}

	if (iRankFilters > 1)
		pRankFilters = StructCreate(parse_FindFilterTokenStruct);
	if (iInstanceFilters > 1)
		pInstanceFilters = StructCreate(parse_FindFilterTokenStruct);

	for (i = eaSize(peaTokenList) - 1; i >= 0; i--)
	{
		if (pRankFilters && (*peaTokenList)[i]->bCheckRank)
		{
			// exclusively check rank/skill level
			eaPush(&pRankFilters->eaAcceptableTokens, eaRemove(peaTokenList, i));
			continue;
		}
		else if (pInstanceFilters && (*peaTokenList)[i]->bCheckInstance)
		{
			// exclusively check map instance
			eaPush(&pInstanceFilters->eaAcceptableTokens, eaRemove(peaTokenList, i));
			continue;
		}
	}

	if (pRankFilters)
		eaPush(peaTokenList, pRankFilters);
	if (pInstanceFilters)
		eaPush(peaTokenList, pInstanceFilters);
}

static void ServerChat_BuildFilterFromString(int iPartitionIdx, PlayerFindFilterStruct *pFilters, SA_PARAM_NN_STR const char *pchFilterString)
{
	const char *pchFilter = NULL;
	FindFilterTokenStruct *pToken;

	if (!*pchFilterString)
	{
		if(gGSLState.gameServerDescription.baseMapDescription.mapDescription &&
			gGSLState.gameServerDescription.baseMapDescription.mapDescription[0])
		{
			ZoneMapInfo *pZoneMap = worldGetZoneMapByPublicName(gGSLState.gameServerDescription.baseMapDescription.mapDescription);
			const char *pchMessageKey = pZoneMap ? zmapInfoGetDisplayNameMsgKey(pZoneMap) : NULL;

			pToken = StructCreate(parse_FindFilterTokenStruct);
			pToken->bCheckMap = true;

			if (pchMessageKey && *pchMessageKey)
			{
				estrAppendUnescaped(&pToken->pchToken, langTranslateMessageKeyDefault(pFilters->eLanguage, pchMessageKey, gGSLState.gameServerDescription.baseMapDescription.mapDescription));
			}
			else
			{
				estrAppendUnescaped(&pToken->pchToken, gGSLState.gameServerDescription.baseMapDescription.mapDescription);
			}

			eaPush(&pFilters->eaRequiredFilters, pToken);

			pToken = StructCreate(parse_FindFilterTokenStruct);
			pToken->bCheckInstance = true;
			pToken->iRangeLow = pToken->iRangeHigh = partition_PublicInstanceIndexFromIdx(iPartitionIdx);
			eaPush(&pFilters->eaRequiredFilters, pToken);
		}

		return;
	}

	pchFilter = pchFilterString;

	while (*pchFilter != '\0') {
		ServerChat_AddFilterToken(iPartitionIdx, pFilters, &pchFilter);
	}

	ServerChat_CombineFilters(&pFilters->eaRequiredFilters);
}

void ServerChat_FindPlayersTransactionReturn(TransactionReturnVal *returnVal, EntityRef *pRef);

void ServerChat_FindPlayersSimple(Entity *pEnt, bool bSendList, const char *pchFilter)
{
	PlayerFindFilterStruct *pFilters = NULL;
	if(pEnt)
	{		
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		EntityRef *pRef = calloc(1, sizeof(EntityRef));
		*(pRef) = entGetRef(pEnt);
	
		pFilters = StructCreate(parse_PlayerFindFilterStruct);
		pFilters->eLanguage = entGetLanguage(pEnt);

		if (*pchFilter || !bSendList)
			ServerChat_BuildFilterFromString(iPartitionIdx, pFilters, pchFilter);

		devassert(pEnt->pPlayer);

		if(pEnt->pPlayer->accessLevel < ACCESS_GM)
		{
			pFilters->bFindAnonymous = false;
		}
		else
		{
			pFilters->bFindAnonymous = true;
		}

		// Prevent normal players from seeing GMs.
		pFilters->iMaxAccessLevel = pEnt->pPlayer->accessLevel;
		pFilters->searchAccountID = pEnt->pPlayer->accountID;

		// Always return results using ServerChat_FindPlayersTransactionReturn
		// Since ServerChat_FindPlayersSimpleTransactionReturn can potentially
		// return a message "No players found" instead of opening the search UI.
		gslFindPlayers(pFilters, ServerChat_FindPlayersTransactionReturn, (void*) pRef);

		StructDestroy(parse_PlayerFindFilterStruct, pFilters);
	}
}

void ServerChat_FindPlayersTransactionReturn(TransactionReturnVal *returnVal, EntityRef *pRef)
{
	ChatPlayerList *pList = StructCreate(parse_ChatPlayerList);
	enumTransactionOutcome eOutcome = gslFindPlayersReturn(returnVal, &pList);
	Entity *pEnt = entFromEntityRefAnyPartition(*pRef);
	if (pEnt)
	{
		ClientCmd_gclChat_UpdateFoundPlayersCmd(pEnt, true, pList);
	}
	SAFE_FREE(pRef);
	StructDestroy(parse_ChatPlayerList, pList);
}

void ServerChat_FindPlayers(Entity *pEnt, PlayerFindFilterStruct *pFilters)
{
	if(pEnt && pFilters)
	{
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		EntityRef *pRef = calloc(1, sizeof(EntityRef));
		*(pRef) = entGetRef(pEnt);
		
		if(pEnt->pPlayer->accessLevel < ACCESS_GM)
		{
			pFilters->bFindAnonymous = false;
		}
		else
		{
			pFilters->bFindAnonymous = true;
		}

		if (pFilters->pchFilterString)
		{
			const char *pchFilter = NULL;

			pchFilter = pFilters->pchFilterString;

			while (*pchFilter != '\0') {
				ServerChat_AddFilterToken(iPartitionIdx, pFilters, &pchFilter);
			}

			ServerChat_CombineFilters(&pFilters->eaRequiredFilters);
		}

		// Prevent normal players from seeing GMs.
		pFilters->iMaxAccessLevel = pEnt->pPlayer->accessLevel;
		pFilters->searchAccountID = pEnt->pPlayer->accountID;

		gslFindPlayers(pFilters, ServerChat_FindPlayersTransactionReturn, (void*) pRef);
	}
}

void ServerChat_FindTeamsTransactionReturn(TransactionReturnVal *returnVal, EntityRef *pRef)
{
	ChatTeamToJoinList *pList = StructCreate(parse_ChatTeamToJoinList);
	enumTransactionOutcome eOutcome = gslFindTeamsReturn(returnVal, &pList);
	Entity *pEnt = entFromEntityRefAnyPartition(*pRef);
	if (pEnt)
	{
		ClientCmd_gclChat_UpdateFoundTeamsCmd(pEnt, pList);
	}
	SAFE_FREE(pRef);
	StructDestroy(parse_ChatTeamToJoinList, pList);
}

void ServerChat_FindTeams(Entity *pEnt, PlayerFindFilterStruct *pFilters)
{
	if(pEnt && pFilters)
	{
		EntityRef *pRef = calloc(1, sizeof(EntityRef));
		*(pRef) = entGetRef(pEnt);

		// Prevent normal players from seeing GMs.
		pFilters->iMaxAccessLevel = pEnt->pPlayer->accessLevel;
		pFilters->searchAccountID = pEnt->pPlayer->accountID;

		gslFindTeams(pFilters, ServerChat_FindTeamsTransactionReturn, (void*) pRef);
	}
}

void ServerChat_SetHidden(Entity *pEnt)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEnt);
	if(pEnt && pEnt->pPlayer && pConfig)
	{		
		char *pchChatMsg = NULL;
		estrStackCreate(&pchChatMsg);

		pConfig->status |= USERSTATUS_HIDDEN;
		pConfig->status &= ~USERSTATUS_FRIENDSONLY;

		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		
		RemoteCommand_UserHidden(NULL, GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID);

		entFormatGameMessageKey(pEnt, &pchChatMsg, "ChatServer_Hidden", STRFMT_END);

		notify_NotifySend(pEnt, kNotifyType_ChatAnonymous, pchChatMsg, NULL, NULL);
		estrDestroy(&pchChatMsg);
	}
}

void ServerChat_SetFriendsOnly(Entity *pEnt)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEnt);
	if(pEnt && pEnt->pPlayer && pConfig)
	{		
		char *pchChatMsg = NULL;
		estrStackCreate(&pchChatMsg);

		pConfig->status &= ~USERSTATUS_HIDDEN;
		pConfig->status |= USERSTATUS_FRIENDSONLY;

		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);

		RemoteCommand_UserFriendsOnly(NULL, GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID);
		ServerChat_PlayerUpdate(pEnt, CHATUSER_UPDATE_GLOBAL);

		entFormatGameMessageKey(pEnt, &pchChatMsg, "ChatServer_FriendsOnly", STRFMT_END);

		notify_NotifySend(pEnt, kNotifyType_ChatAnonymous, pchChatMsg, NULL, NULL);
		estrDestroy(&pchChatMsg);
	}
}

void ServerChat_SetVisible(Entity *pEnt)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEnt);
	if(pEnt && pEnt->pPlayer && pConfig)
	{		
		char *pchChatMsg = NULL;
		estrStackCreate(&pchChatMsg);

		pConfig->status &= ~USERSTATUS_HIDDEN;
		pConfig->status &= ~USERSTATUS_FRIENDSONLY;

		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);

		RemoteCommand_UserVisible(NULL, GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID);
		ServerChat_PlayerUpdate(pEnt, CHATUSER_UPDATE_GLOBAL);

		entFormatGameMessageKey(pEnt, &pchChatMsg, "ChatServer_Visible", STRFMT_END);

		notify_NotifySend(pEnt, kNotifyType_ChatAnonymous, pchChatMsg, NULL, NULL);
		estrDestroy(&pchChatMsg);
	}
}

void ServerChat_SetLFGMode(Entity *pEnt, TeamMode mode)
{
	if(pEnt && pEnt->pPlayer && mode != TeamMode_Prompt)
	{
		char *pchChatMsg = NULL;
		estrStackCreate(&pchChatMsg);

		pEnt->pPlayer->eLFGMode = mode;		
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		if(pEnt->pPlayer->eLFGMode == TeamMode_Open) {
			entFormatGameMessageKey(pEnt, &pchChatMsg, "ChatServer_LFG", STRFMT_END);
		} else if(pEnt->pPlayer->eLFGMode == TeamMode_RequestOnly) {
			entFormatGameMessageKey(pEnt, &pchChatMsg, "ChatServer_LFGRequestOnly", STRFMT_END);
		} else {
			entFormatGameMessageKey(pEnt, &pchChatMsg, "ChatServer_NoLongerLFG", STRFMT_END);
		}
		ServerChat_PlayerUpdate(pEnt, CHATUSER_UPDATE_SHARD);
		
		notify_NotifySend(pEnt, kNotifyType_ChatLFG, pchChatMsg, NULL, NULL);

		estrDestroy(&pchChatMsg);
	}
}

void ServerChat_SetLFGDifficultyMode(Entity *pEnt, LFGDifficultyMode eMode)
{
	if (pEnt && pEnt->pPlayer)
	{
		char *pchChatMsg = NULL;
		estrStackCreate(&pchChatMsg);

		pEnt->pPlayer->eLFGDifficultyMode = eMode;		
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		if (pEnt->pPlayer->eLFGDifficultyMode == LFGDifficultyMode_Player) {
			entFormatGameMessageKey(pEnt, &pchChatMsg, "ChatServer_LFGDifficultyPlayer", STRFMT_END);
		} else {
			entFormatGameMessageKey(pEnt, &pchChatMsg, "ChatServer_LFGDifficultyAny", STRFMT_END);
		}
		ServerChat_PlayerUpdate(pEnt, CHATUSER_UPDATE_SHARD);
		
		notify_NotifySend(pEnt, kNotifyType_ChatLFG, pchChatMsg, NULL, NULL);

		estrDestroy(&pchChatMsg);
	}
}

void ServerChat_SetPlayingStyles(Entity *pEnt, const char *pchPlayStyles, bool bSendUpdate)
{
	// artificially limit the length
	if (pchPlayStyles && *pchPlayStyles && strlen(pchPlayStyles) >= 127)
		return;

	if (SAFE_MEMBER3(pEnt, pPlayer, pUI, pLooseUI))
	{
		if (pchPlayStyles && *pchPlayStyles)
		{
			if (pEnt->pPlayer->pUI->pLooseUI->pchPlayingStyles &&
				stricmp(pEnt->pPlayer->pUI->pLooseUI->pchPlayingStyles, pchPlayStyles) != 0)
			{
				// reset, different
				StructFreeString(pEnt->pPlayer->pUI->pLooseUI->pchPlayingStyles);
				pEnt->pPlayer->pUI->pLooseUI->pchPlayingStyles = NULL;
			}

			if (!pEnt->pPlayer->pUI->pLooseUI->pchPlayingStyles)
			{
				pEnt->pPlayer->pUI->pLooseUI->pchPlayingStyles = StructAllocString(pchPlayStyles);
				entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
				entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, true);
				if (bSendUpdate)
					ServerChat_PlayerUpdate(pEnt, CHATUSER_UPDATE_SHARD);
			}
		}
		else if (pEnt->pPlayer->pUI->pLooseUI->pchPlayingStyles)
		{
			StructFreeString(pEnt->pPlayer->pUI->pLooseUI->pchPlayingStyles);
			pEnt->pPlayer->pUI->pLooseUI->pchPlayingStyles = NULL;
			entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, true);
			if (bSendUpdate)
				ServerChat_PlayerUpdate(pEnt, CHATUSER_UPDATE_SHARD);
		}
	}
}

// Request the full list of channels for the user from the Chat Server
// Flags: USER_CHANNEL_SUBSCRIBED - request subscribed global channels
//        USER_CHANNEL_INVITED    - request invited channels
//        USER_CHANNEL_RESERVED   - request subscribed reserved channels
#ifndef USE_CHATRELAY
void ServerChat_RequestUserChannelList(Entity *pEnt, U32 uFlags)
{
	if (pEnt && pEnt->pPlayer)
	{
		RemoteCommand_ChatServerGetUserChannels(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, pEnt->myContainerID, uFlags);
	}
}

void ServerChat_RequestFullChannelInfo(Entity *pEnt, ACMD_SENTENCE channel_name)
{
	if (pEnt && pEnt->pPlayer)
	{
		RemoteCommand_ChatServerGetChannelInfo(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, pEnt->myContainerID, channel_name);
	}
}

void ServerChat_RequestJoinChannelInfo(Entity *pEnt, ACMD_SENTENCE channel_name)
{
	if (pEnt && pEnt->pPlayer)
	{
		RemoteCommand_ChatServerGetJoinChannelInfo(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, pEnt->myContainerID, channel_name);
	}
}

void ServerChat_ReceiveUserChannelList (U32 entID, ChatChannelInfoList *pList)
{
	Entity *ent = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entID);
	if (ent)
	{
		// Make sure entity is in sync
		while (eaSize(&ent->pPlayer->eaSubscribedChannels) > 0) {
			free(eaPop(&ent->pPlayer->eaSubscribedChannels));
		}

		if (pList && pList->ppChannels) {
			int i, size;
			size = eaSize(&pList->ppChannels);
			for (i=0; i < size; i++) {
				ChatChannelInfo *pInfo = eaGet(&pList->ppChannels, i);
				// Only include non-reserved subscribed+invited channels in this list
				if ((pInfo->bUserSubscribed || pInfo->bUserInvited ) && !ChatChannelIsShardReserved(pInfo)) {
					eaPush(&ent->pPlayer->eaSubscribedChannels, strdup(pInfo->pName));
				}
			}
		}

		// Notify the client
		ClientCmd_ClientChat_ReceiveUserChannelList(ent, pList);
	}
}

void ServerChat_ReceiveJoinChannelInfo (U32 entID, ChatChannelInfo *pInfo)
{
	Entity *ent = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entID);
	if (ent)
	{
		ClientCmd_cmdClientChat_ReceiveJoinChannelInfo(ent, pInfo);
	}
}
#endif

//Send a notification to an entity given their Entity ID
AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void ServerChat_NotifyUser(ContainerID id, NotifyType eType, const char *pchDisplayString, const char *pchLogicalString, const char *pchTexture) {
	ClientCmd_NotifySend(entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, id), eType, pchDisplayString, pchLogicalString, pchTexture);
}

static void ServerChat_AddShardGlobalChannel(Entity *pEnt, const char *channel_name)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEnt);
	if (pConfig) {
		if (eaFindString(&pConfig->ppShardGlobalChannels, channel_name) == -1)
		{
			eaPush(&pConfig->ppShardGlobalChannels, estrDup(channel_name));
			entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}
	}
}
static void ServerChat_RemoveShardGlobalChannel(Entity *pEnt, const char *channel_name)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEnt);
	if (pConfig) {
		int idx = eaFindString(&pConfig->ppShardGlobalChannels, channel_name);
		if (idx != -1)
		{
			estrDestroy(&pConfig->ppShardGlobalChannels[idx]);
			eaRemove(&pConfig->ppShardGlobalChannels, idx);
			entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}
	}
}

static bool IsLifetimeSubscriptionWrapper(Entity *pEnt)
{
	return entity_LifetimeSubscription(pEnt);
}

typedef bool (*ShardGlobal_IsValidEntity) (Entity *pEnt);
typedef struct ShardGlobalChannelStruct
{
	char *name; AST(ESTRING)
	ShardGlobal_IsValidEntity validateCB;
	bool bVShardUnique; // true if each virtual shard should have a different instance of this shard-global channel
} ShardGlobalChannelStruct;
static ShardGlobalChannelStruct **seaShardGlobalValidators = NULL;

static void addShardGlobalValidator(SA_PARAM_NN_STR const char *channel_name, ShardGlobal_IsValidEntity validator, bool bVShardUnique)
{
	ShardGlobalChannelStruct *data = malloc(sizeof(ShardGlobalChannelStruct));
	data->name = estrDup(channel_name);
	data->validateCB = validator;
	data->bVShardUnique = bVShardUnique;
	eaPush(&seaShardGlobalValidators, data);
}
AUTO_RUN;
void initializeShardGlobalValidators(void)
{
	// New Shard Global channels that require some sort of user validation MUST be added here
	addShardGlobalValidator(SHARD_LIFETIME_CHANNEL_NAME, IsLifetimeSubscriptionWrapper, true);
	addShardGlobalValidator(SHARD_HELP_CHANNEL_NAME, NULL, false);
	addShardGlobalValidator(SHARD_GLOBAL_CHANNEL_NAME, NULL, true);
}

// Returns true if the channel name is a valid Shard Global one AND the player is allowed to join it
static bool ServerChat_IsValidShardGlobalChannel (Entity *pEnt, const char *channel_name)
{
	int i, size;
	const char *strippedName;
	if (!pEnt || !channel_name || !*channel_name)
		return false;
	
	// First strip any virtual shard prefix from the string
	strippedName = ShardChannel_StripVShardPrefix(channel_name);
	size = eaSize(&seaShardGlobalValidators);
	for (i=0; i<size; i++)
	{
		if (stricmp(strippedName, seaShardGlobalValidators[i]->name) == 0)
		{
			if (seaShardGlobalValidators[i]->validateCB)
				return seaShardGlobalValidators[i]->validateCB(pEnt);
			return true;
		}
	}
	if (eaFindString(&gProjectGameServerConfig.ppShardGlobalChannels, strippedName) != -1)
		return true;
	return false;
}

static bool ServerChat_IsChannelVShardUnique(SA_PARAM_NN_STR const char *channel_name)
{
	int i, size;
	size = eaSize(&seaShardGlobalValidators);
	for (i=0; i<size; i++)
	{
		if (stricmp(channel_name, seaShardGlobalValidators[i]->name) == 0)
		{
			return seaShardGlobalValidators[i]->bVShardUnique;
		}
	}
	return false;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void ServerChat_JoinSpecialChannel(Entity *pEnt, const char *channel_name)
{
	if (ServerChat_IsValidShardGlobalChannel(pEnt, channel_name))
	{
		char buffer[256];
		buffer[0] = 0;
		if (GAMESERVER_VSHARD_ID && ServerChat_IsChannelVShardUnique(channel_name))
			sprintf(buffer, "%s%d_%s", VSHARD_PREFIX, GAMESERVER_VSHARD_ID, channel_name);
		else
			sprintf(buffer, "%s", channel_name);
		RemoteCommand_ChatServerJoinOrCreateChannel(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, 
			buffer, CHANNEL_SPECIAL_SHARDGLOBAL, true);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void ServerChat_JoinLifetimeChannel(Entity *pEnt)
{
	if (entity_LifetimeSubscription(pEnt))
	{
		ServerChat_JoinSpecialChannel(pEnt, SHARD_LIFETIME_CHANNEL_NAME);
	}
	else
	{
		char *error = NULL;
		entFormatGameMessageKey(pEnt, &error, "ChatAccount_NotLifetime", STRFMT_END);
		ClientCmd_NotifySend(pEnt, kNotifyType_ServerOffline, error, NULL, NULL);
		estrDestroy(&error);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void ServerChat_LeaveSpecialChannel(Entity *pEnt, const char *channel_name)
{
	char buffer[256];
	buffer[0] = 0;
	if (GAMESERVER_VSHARD_ID && ServerChat_IsChannelVShardUnique(channel_name))
		sprintf(buffer, "%s%d_%s", VSHARD_PREFIX, GAMESERVER_VSHARD_ID, channel_name);
	else
		sprintf(buffer, "%s", channel_name);
	ServerChat_RemoveShardGlobalChannel(pEnt, channel_name);
	RemoteCommand_ChatServerLeaveChannel(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, buffer);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void ServerChat_VerifyVoice(Entity *pEnt)
{
	RemoteCommand_ChatServerVoice_Verify(GLOBALTYPE_CHATSERVER, 0, entGetAccountID(pEnt));
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(4) ACMD_HIDE ACMD_NAME(LifetimeChannelJoin);
void ServerChat_JoinLifetimeChannel_Test(Entity *pEnt)
{
	ServerChat_JoinLifetimeChannel(pEnt);
}
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(4) ACMD_HIDE ACMD_NAME(SpecialChannelJoin);
void ServerChat_JoinSpecialChannel_Test(Entity *pEnt, const char *channel_name)
{
	ServerChat_JoinSpecialChannel(pEnt, channel_name);
}
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(4) ACMD_HIDE ACMD_NAME(SpecialChannelLeave);
void ServerChat_LeaveSpecialChannel_Test(Entity *pEnt, const char *channel_name)
{
	ServerChat_LeaveSpecialChannel(pEnt, channel_name);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void ServerChat_ShardChannelLeave(ContainerID characterID, U32 uReservedFlags)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, characterID);
	if (uReservedFlags & CHANNEL_SPECIAL_TEAM)
	{
		if (pEnt && pEnt->pTeam)
			pEnt->pTeam->iInChat = 0;
	}
	else if (uReservedFlags & CHANNEL_SPECIAL_GUILD)
	{
		if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild)
			pEnt->pPlayer->pGuild->iGuildChat = 0;
	}
	else if (uReservedFlags & CHANNEL_SPECIAL_OFFICER)
	{
		if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild)
			pEnt->pPlayer->pGuild->iOfficerChat = 0;
	}
}
AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void ServerChat_ShardChannelJoin(ContainerID characterID, U32 uReservedFlags)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, characterID);
	if (uReservedFlags & CHANNEL_SPECIAL_TEAM)
	{
		if (pEnt && pEnt->pTeam)
			pEnt->pTeam->iInChat = pEnt->pTeam->iTeamID;
	}
	else if (uReservedFlags & CHANNEL_SPECIAL_GUILD)
	{
		if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild)
			pEnt->pPlayer->pGuild->iGuildChat = pEnt->pPlayer->pGuild->iGuildID;
	}
	else if (uReservedFlags & CHANNEL_SPECIAL_OFFICER)
	{
		if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild)
			pEnt->pPlayer->pGuild->iOfficerChat = pEnt->pPlayer->pGuild->iGuildID;
	}
}

AUTO_COMMAND_REMOTE;
void ServerChat_ForwardWhiteListInfo(ContainerID characterID, bool bChatEnable, bool bTellsEnable, bool bEmailEnable)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, characterID);
	if (pEnt)
	{
		ClientCmd_cmdClientChat_ReceiveWhitelistInfo(pEnt, bChatEnable, bTellsEnable, bEmailEnable);
	}
}

AUTO_COMMAND_REMOTE;
void ServerChat_ChannelUpdate(ContainerID characterID, ChatChannelInfo *channel_info, ChannelUpdateEnum eChangeType)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, characterID);
	if (pEnt)
	{
		if (channel_info->uReservedFlags & CHANNEL_SPECIAL_SHARDGLOBAL)
		{
			ServerChat_AddShardGlobalChannel(pEnt, ShardChannel_StripVShardPrefix(channel_info->pName));
		}
		ClientCmd_ClientChat_ChannelUpdate(pEnt, channel_info, eChangeType);
	}
}

#include "AutoGen/gslChat_c_ast.c"
