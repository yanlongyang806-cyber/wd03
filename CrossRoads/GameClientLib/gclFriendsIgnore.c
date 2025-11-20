#include "gclFriendsIgnore.h"
#include "gclDialogBox.h"
#include "gclEntity.h"
#include "EntityLib.h"
#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "GameAccountDataCommon.h"
#include "gclChat.h"
#include "gclChatConfig.h"
#include "gclUIGen.h"
#include "estring.h"
#include "queue.h"
#include "GameStringFormat.h"
#include "Player.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "TextFilter.h"
#include "WorldGrid.h"
#include "UIGen.h"
#include "EntitySavedData.h"
#include "MapDescription.h"
#include "../StaticWorld/ZoneMap.h"
#include "Guild.h"
#include "PlayerDifficultyCommon.h"
#include "CharacterClass.h"
#include "chat/gclClientChat.h"
#include "LoginCommon.h"
#include "gclLogin.h"

#include "Autogen/chatCommon_h_ast.h"
#include "Autogen/chatCommonStructs_h_ast.h"
#include "AutoGen/ChatRelay_autogen_GenericServerCmdWrappers.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

// Typedefs
//
extern ParseTable parse_ClientChatMap[];
#define TYPE_parse_ClientChatMap ClientChatMap
extern ParseTable parse_ClientPlayerStruct[];
#define TYPE_parse_ClientPlayerStruct ClientPlayerStruct
extern ParseTable parse_ClientTeamMemberStruct[];
#define TYPE_parse_ClientTeamMemberStruct ClientTeamMemberStruct
extern ParseTable parse_PlayerLevelFilterRange[];
#define TYPE_parse_PlayerLevelFilterRange PlayerLevelFilterRange
extern ParseTable parse_ClientPlayerFilterInfo[];
#define TYPE_parse_ClientPlayerFilterInfo ClientPlayerFilterInfo

AUTO_STRUCT;
typedef struct ClientTeamMemberStruct
{
	char *pchHandle;			AST(NAME(Handle) ESTRING)
	char *pchName;				AST(NAME(Name) ESTRING)		
	S32 iPlayerLevel;			AST(NAME(PlayerLevel))
	S32 iPlayerRank;			AST(NAME(PlayerRank))
	char *pchPlayingStyles;		AST(NAME(PlayingStyles) ESTRING)
} ClientTeamMemberStruct;

AUTO_STRUCT;
typedef struct ClientChatMap
{
	char *pchMapName;			AST(NAME(MapName) ESTRING)
	char *pchNeighborhoodName;	AST(NAME(NeighborhoodName) ESTRING)
} ClientChatMap;

AUTO_STRUCT;
typedef struct PlayerLevelFilterRange
{
	S32 iMinLevel;
	S32 iMaxLevel;
} PlayerLevelFilterRange;

AUTO_STRUCT;
typedef struct ClientPlayerFilterInfo
{
	PlayerLevelFilterRange **eaLevelFilters;
	ClientChatMap **eaMapFilters;
	char **eaPlayingStyles;		AST(NAME(PlayingStyles) UNOWNED)
	char *pchFilterString;
} ClientPlayerFilterInfo;

// Static globals
//
static ChatPlayerStruct **g_eaFoundPlayers = NULL;
static S32 g_iFoundPlayerCount = -1;
static ChatTeamToJoin **g_eaFoundTeams = NULL;
static S32 g_iFoundTeamCount = -1;

static ClientChatMap **g_eaMapList = NULL;

static ClientPlayerFilterInfo g_PlayerFilterInfo = {0};

static char *g_pchDefaultMap = NULL;
static char *g_pchDefaultMapNameKey = NULL;
static char *g_pchDefaultNeighborhoodNameKey = NULL;
static const char *g_pchDefaultMapName = NULL;
static const char *g_pchDefaultNeighborhoodName = NULL;

static bool g_bWhoSentList = false;
static bool g_bTeamSearchSentList = false;

static ChatPlayerStruct* FindFriendByAccount(U32 accountID);

static __forceinline int GetCurrentInstance(Entity *pEnt)
{
	SavedMapDescription *pCurrentMap = entity_GetLastMap(pEnt);
	if(pCurrentMap)
	{
		return pCurrentMap->mapInstanceIndex;
	}
	return -1;
}

// Static functions
//
static void ClientChat_FillClientPlayer(SA_PARAM_NN_VALID ClientPlayerStruct *pClientPlayer, SA_PARAM_OP_VALID ChatPlayerStruct *pChatPlayer, bool bAssumeFriend, bool bAssumeIgnored)
{
	Entity *pEnt = entActivePlayerPtr();
	Guild *pGuild = guild_GetGuild(pEnt);
	int iPlayerInstance = GetCurrentInstance(pEnt);
	const char *pchMap = zmapInfoGetPublicName(NULL);
	const GameAccountData *pData = entity_GetGameAccount(pEnt);
	char tempString[1028];

	pClientPlayer->accountID = pChatPlayer->accountID;
	pClientPlayer->onlineCharacterID = pChatPlayer->pPlayerInfo.onlineCharacterID;
	pClientPlayer->uLoginServerID = pChatPlayer->pPlayerInfo.uLoginServerID;
	pClientPlayer->iPlayerLevel = pChatPlayer->pPlayerInfo.iPlayerLevel;
	pClientPlayer->iPlayerRank = pChatPlayer->pPlayerInfo.iPlayerRank;
	pClientPlayer->pchAllegiance = allocAddString(pChatPlayer->pPlayerInfo.onlinePlayerAllegiance);
	pClientPlayer->pchClassName = pChatPlayer->pPlayerInfo.pchClassName;
	pClientPlayer->bDifferentGame = false;
	if (pClientPlayer->pchClassName)
	{
		CharacterClass *pClass = RefSystem_ReferentFromString("CharacterClass", pClientPlayer->pchClassName);
		if (pClass)
			estrCopy2(&pClientPlayer->pchClassDispName, TranslateDisplayMessage(pClass->msgDisplayName));
	}
	pClientPlayer->pchPathName = pChatPlayer->pPlayerInfo.pchPathName;
	pClientPlayer->iPlayerTeam = pChatPlayer->pPlayerInfo.iPlayerTeam;
	estrCopy2(&pClientPlayer->pchTeamStatusMessage, pChatPlayer->pPlayerInfo.pchTeamStatusMessage);
	if (pChatPlayer->pPlayerInfo.iPlayerTeam)
	{
		// Use team LFG status if the player is leader
		if (pChatPlayer->pPlayerInfo.eTeamMode != TeamMode_Prompt)
		{
			pClientPlayer->eLFGMode = pChatPlayer->pPlayerInfo.eTeamMode;
		}
		else
		{
			pClientPlayer->eLFGMode = TeamMode_Closed;
		}
	}
	else
	{		
		pClientPlayer->eLFGMode = pChatPlayer->pPlayerInfo.eLFGMode;
	}
	pClientPlayer->eLFGDifficultyMode = pChatPlayer->pPlayerInfo.eLFGDifficultyMode;
	{
		PlayerDifficulty *pDifficulty = pd_GetDifficulty(pChatPlayer->pPlayerInfo.iDifficulty);
		if (pDifficulty)
		{
			const char *text = TranslateMessageRef(pDifficulty->hName);
			if (text) pClientPlayer->pcDifficulty = StructAllocString(text);

			pClientPlayer->iDifficultyIdx = pChatPlayer->pPlayerInfo.iDifficulty;
		}
	}
	pClientPlayer->eOnlineStatus = pChatPlayer->online_status & USERSTATUS_ONLINE;
	pClientPlayer->bSameInstance = pChatPlayer->pPlayerInfo.playerMap.iMapInstance == iPlayerInstance && !stricmp_safe(pChatPlayer->pPlayerInfo.playerMap.pchMapName, pchMap);
	pClientPlayer->bAfk = !!(pChatPlayer->online_status & (USERSTATUS_AFK | USERSTATUS_AUTOAFK));
	pClientPlayer->bDnd = !!(pChatPlayer->online_status & USERSTATUS_DND);

	pClientPlayer->bFriend = bAssumeFriend || gclClientChat_GetAccountID() == pClientPlayer->accountID;
	if (!pClientPlayer->bFriend)
	{
		ChatPlayerStruct *pFriend = !bAssumeFriend ? FindFriendByAccount(pClientPlayer->accountID) : NULL;
		pClientPlayer->bFriend = pFriend ? !(pFriend->flags & (FRIEND_FLAG_PENDINGREQUEST | FRIEND_FLAG_RECEIVEDREQUEST)) : false;
	}
	pClientPlayer->bIgnored = bAssumeIgnored || ClientChat_IsIgnoredAccount(pClientPlayer->accountID);
	pClientPlayer->bGuildMate = pGuild && guild_FindMemberInGuildEntID(pClientPlayer->onlineCharacterID, pGuild);

	pClientPlayer->bBuddy = GAD_AccountIsRecruit(pData, pChatPlayer->accountID) || GAD_AccountIsRecruiter(pData, pChatPlayer->accountID);

	estrPrintf(&pClientPlayer->pchHandle, "@%s", pChatPlayer->chatHandle);
	if (pChatPlayer->pchGuildName)
		estrCopy2(&pClientPlayer->pchGuildName, pChatPlayer->pchGuildName);
	else
		estrDestroy(&pClientPlayer->pchGuildName);
	if (pChatPlayer->pPlayerInfo.playingStyles)
	{
		sprintf(tempString,"PlayingStyle_%s", pChatPlayer->pPlayerInfo.playingStyles);
		estrCopy2(&pClientPlayer->pchPlayingStyles, TranslateMessageKey(tempString));
	}
	else
		estrDestroy(&pClientPlayer->pchPlayingStyles);

	//TODO: This probably shouldn't check the hidden flag on the client for security reasons
	if(pChatPlayer->online_status == USERSTATUS_OFFLINE || (pChatPlayer->online_status & USERSTATUS_HIDDEN)) {		
		estrCopy2(&pClientPlayer->pchName, TranslateMessageKey("Player.Offline"));
	} else if(pChatPlayer->pPlayerInfo.onlinePlayerName) {
		estrCopy2(&pClientPlayer->pchName, pChatPlayer->pPlayerInfo.onlinePlayerName);
	} else {
		//XMPP clients and people in other games don't have an onlineCharacterID.
		estrCopy2(&pClientPlayer->pchName, "");
	}

	if (pChatPlayer->status)
		estrCopy2(&pClientPlayer->pchStatus, pChatPlayer->status);
	else
		estrDestroy(&pClientPlayer->pchStatus);
	if (pChatPlayer->comment)
		estrCopy2(&pClientPlayer->pchComment, pChatPlayer->comment);
	else
		estrDestroy(&pClientPlayer->pchComment);
	if (pChatPlayer->pPlayerInfo.playerActivity)
		estrCopy2(&pClientPlayer->pchActivity, pChatPlayer->pPlayerInfo.playerActivity);
	else
		estrDestroy(&pClientPlayer->pchActivity);
	
	estrClear(&pClientPlayer->pchMapNameAndInstance);

	if(pChatPlayer->flags & FRIEND_FLAG_PENDINGREQUEST)	{
		estrCopy2(&pClientPlayer->pchLocation, TranslateMessageKey("Friends.SentRequest"));
	} else if(pChatPlayer->flags & FRIEND_FLAG_RECEIVEDREQUEST)	{
		estrCopy2(&pClientPlayer->pchLocation, TranslateMessageKey("Friends.ReceivedRequest"));
	} else if(pChatPlayer->online_status == USERSTATUS_OFFLINE || (pChatPlayer->online_status & USERSTATUS_HIDDEN)) {
		estrCopy2(&pClientPlayer->pchLocation, TranslateMessageKey("Player.Offline"));
	} 
	else if (pEnt && PlayerInfo_GetDisplayPriority(&pChatPlayer->pPlayerInfo, entGetVirtualShardID(pEnt)) != PINFO_PRIORITY_VSHARD  && pChatPlayer->pPlayerInfo.gamePublicNameKey)
	{
		// Not the exact same virtual shard, only display generic where info
		estrClear(&pClientPlayer->pchLocation);
		FormatMessageKey(&pClientPlayer->pchLocation, pChatPlayer->pPlayerInfo.gamePublicNameKey, STRFMT_END);
		pClientPlayer->bDifferentGame = true;
		pClientPlayer->bDifferentVShard = (PlayerInfo_GetDisplayPriority(&pChatPlayer->pPlayerInfo, entGetVirtualShardID(pEnt)) == PINFO_PRIORITY_PSHARD);
		pClientPlayer->bUGCShard = false;
        if (pClientPlayer->bDifferentVShard)
		{
            // LOGIN2UGC - need a better way of figuring out whether the player is on a UGC virtual shard.  Needs to understand multi-shard.
		}
	}
	else if (pChatPlayer->pPlayerInfo.playerMap.pchMapName) 
	{
		ChatMap *pPlayerMap = &pChatPlayer->pPlayerInfo.playerMap;
		const char *pchMapName = gclRequestMapDisplayName(pPlayerMap->pchMapNameMsgKey);
		const char *pchNeighborhood = gclRequestMapDisplayName(pPlayerMap->pchNeighborhoodNameMsgKey);

		estrClear(&pClientPlayer->pchLocation);

		if (pchNeighborhood) {
			FormatMessageKey(&pClientPlayer->pchLocation, "friends.maplocationwithneighborhood",
				STRFMT_STRING("MapName", pchMapName),
				STRFMT_INT("MapInstance", pChatPlayer->pPlayerInfo.playerMap.iMapInstance),
				STRFMT_STRING("MapNeighborhood", pchNeighborhood),
				STRFMT_END);
		} else if (pChatPlayer->pPlayerInfo.playerMap.iMapInstance) {
			FormatMessageKey(&pClientPlayer->pchLocation, "friends.maplocation",
				STRFMT_STRING("MapName", pchMapName),
				STRFMT_INT("MapInstance", pChatPlayer->pPlayerInfo.playerMap.iMapInstance),
				STRFMT_END);
		} else {
			FormatMessageKey(&pClientPlayer->pchLocation, "friends.maplocation.zero",
				STRFMT_STRING("MapName", pchMapName),
				STRFMT_END);
		}

		FormatMessageKey(&pClientPlayer->pchMapNameAndInstance, "friends.maplocation",
			STRFMT_STRING("MapName", pchMapName),
			STRFMT_INT("MapInstance", pChatPlayer->pPlayerInfo.playerMap.iMapInstance),
			STRFMT_END);
	}
	else if (pChatPlayer->pPlayerInfo.pLocationMessageKey)
	{
		estrClear(&pClientPlayer->pchLocation);
		entFormatGameMessageKey(pEnt, &pClientPlayer->pchLocation, pChatPlayer->pPlayerInfo.pLocationMessageKey, STRFMT_END);
	}
	else
	{
		estrClear(&pClientPlayer->pchLocation);
	}
}


void ClientChat_FillClientPlayerStructs(SA_PARAM_NN_VALID ClientPlayerStruct ***peaClientPlayers, SA_PARAM_OP_VALID ChatPlayerStruct ***peaChatPlayers, bool bSearchResults, bool bAssumeFriend, bool bAssumeIgnored)
{
	S32 i, j;

	if (peaChatPlayers == NULL || !eaSize(peaChatPlayers)) {
		eaClearStruct(peaClientPlayers, parse_ClientPlayerStruct);
		return;
	} else {
		eaSetSizeStruct(peaClientPlayers, parse_ClientPlayerStruct, eaSize(peaChatPlayers));	
	}

	for(i = 0, j = 0; i < eaSize(peaChatPlayers); i++, j++)
	{
		ChatPlayerStruct *pChatPlayer = eaGet(peaChatPlayers, i);
		ClientPlayerStruct *pClientPlayer = eaGet(peaClientPlayers, j);
		ClientChat_FillClientPlayer(pClientPlayer, pChatPlayer, bAssumeFriend, bAssumeIgnored);
	}
	if (j < i)
	{
		eaSetSizeStruct(peaClientPlayers, parse_ClientPlayerStruct, eaSize(peaChatPlayers) - (i - j));
	}
	g_iFoundPlayerCount = eaSize(peaClientPlayers);
}

static void ClientChat_FillClientPlayerStructsFromTeams(SA_PARAM_NN_VALID ClientPlayerStruct ***peaClientPlayers, SA_PARAM_OP_VALID ChatTeamToJoin ***peaTeams)
{
	S32 i;

	if (peaTeams == NULL || !eaSize(peaTeams)) {
		eaClearStruct(peaClientPlayers, parse_ClientPlayerStruct);
		return;
	} else {
		eaSetSizeStruct(peaClientPlayers, parse_ClientPlayerStruct, eaSize(peaTeams));	
	}

	for(i = 0; i < eaSize(peaTeams); i++)
	{
		ChatTeamToJoin *pChatTeam = eaGet(peaTeams, i);
		ChatPlayerStruct *pChatPlayer = pChatTeam->pLeader;
		ClientPlayerStruct *pClientPlayer = eaGet(peaClientPlayers, i);
		int j;
		ClientChat_FillClientPlayer(pClientPlayer, pChatPlayer, false, false);
		pClientPlayer->bFriend = pChatTeam->bHasFriend;
		pClientPlayer->iTeamMembers = 1 + eaSize(&pChatTeam->ppTeamMembers);

		eaSetSizeStruct(&pClientPlayer->ppExtraMembers, parse_ClientTeamMemberStruct, eaSize(&pChatTeam->ppTeamMembers));	

		for (j = 0; j < eaSize(&pChatTeam->ppTeamMembers); j++)
		{
			ClientTeamMemberStruct *pClientMember = eaGet(&pClientPlayer->ppExtraMembers, j);
			ChatTeamMemberStruct *pChatMember = eaGet(&pChatTeam->ppTeamMembers, j);
			char tempString[1028];

			pClientMember->iPlayerLevel = pChatMember->iPlayerLevel;
			pClientMember->iPlayerRank = pChatMember->iPlayerRank;
			estrPrintf(&pClientMember->pchHandle, "@%s", pChatMember->chatHandle);

			sprintf(tempString,"PlayingStyle_%s", pChatMember->playingStyles);
			estrCopy2(&pClientMember->pchPlayingStyles, TranslateMessageKey(tempString));

			estrCopy2(&pClientMember->pchName, pChatMember->onlinePlayerName);
		}
		
	}
}


static int SortMaps(const ClientChatMap **a, const ClientChatMap **b)
{
	int iCmp = stricmp_safe((*a)->pchMapName, (*b)->pchMapName);
	if (iCmp != 0) {
		return iCmp;
	}

	iCmp = stricmp_safe((*a)->pchNeighborhoodName, (*b)->pchNeighborhoodName);
	return iCmp;
}

__forceinline static int gclChat_FindClientChatMap(ClientChatMap ***peaMapFilters, ClientChatMap *pMap)
{
	// TODO: Binary search?
	int i;
	if (pMap && peaMapFilters) {
		for (i = eaSize(peaMapFilters) - 1; i >= 0; i--) {
			if (!(*peaMapFilters)[i] ||
				stricmp_safe((*peaMapFilters)[i]->pchMapName, pMap->pchMapName) ||
				stricmp_safe((*peaMapFilters)[i]->pchNeighborhoodName, pMap->pchNeighborhoodName)) {
				//(((*peaMapFilters)[i]->pchMapName == NULL) ^ (pMap->pchMapName == NULL)) || // both null or both set
				//(((*peaMapFilters)[i]->pchNeighborhoodName == NULL) ^ (pMap->pchNeighborhoodName == NULL)) ||
				//((*peaMapFilters)[i]->pchMapName != NULL && 0 != stricmp((*peaMapFilters)[i]->pchMapName, pMap->pchMapName)) || // both non-null & names match
				//((*peaMapFilters)[i]->pchNeighborhoodName != NULL && 0 != stricmp((*peaMapFilters)[i]->pchNeighborhoodName, pMap->pchNeighborhoodName))) {
				continue;
			}
			return i;
		}
	}
	return -1;
}

static ChatPlayerStruct *FindPlayerInListByHandle(ChatPlayerStruct ***peaList, const char *pchHandle) {
	if (peaList && pchHandle && *pchHandle) {
		int i;
		if (*pchHandle == '@') {
			pchHandle++;
		}

		for (i=0; i < eaSize(peaList); i++) {
			ChatPlayerStruct *pPlayer = eaGet(peaList, i);
			if (pPlayer) {
				if (stricmp_safe(pPlayer->chatHandle, pchHandle) == 0) {
					return pPlayer;
				}
			}
		}
	}

	return NULL;
}

static ChatPlayerStruct *FindPlayerInListByContainerID(ChatPlayerStruct ***peaList, U32 containerID) {
	if (peaList) {
		int i;

		for (i=0; i < eaSize(peaList); i++) {
			ChatPlayerStruct *pPlayer = eaGet(peaList, i);
			if (pPlayer && pPlayer->pPlayerInfo.onlineCharacterID == containerID) {
				return pPlayer;
			}
		}
	}

	return NULL;
}

static ChatPlayerStruct *FindLeaderInTeamListByHandle(ChatTeamToJoin ***peaList, const char *pchHandle) {
	if (peaList && pchHandle && *pchHandle) {
		int i;
		if (*pchHandle == '@') {
			pchHandle++;
		}

		for (i=0; i < eaSize(peaList); i++) {
			ChatTeamToJoin *pTeam = eaGet(peaList, i);
			ChatPlayerStruct *pPlayer = pTeam->pLeader;
			if (pPlayer) {
				if (stricmp_safe(pPlayer->chatHandle, pchHandle) == 0) {
					return pPlayer;
				}
			}
		}
	}

	return NULL;
}

static ChatPlayerStruct *FindLeaderInTeamListByAccount(ChatTeamToJoin ***peaList, U32 accountID) {
	if (peaList) {
		int i;

		for (i=0; i < eaSize(peaList); i++) {
			ChatTeamToJoin *pTeam = eaGet(peaList, i);
			ChatPlayerStruct *pPlayer = pTeam->pLeader;
			if (pPlayer && pPlayer->accountID == accountID) {
				return pPlayer;
			}
		}
	}

	return NULL;
}


static ChatPlayerStruct *FindLeaderInTeamListByContainerID(ChatTeamToJoin ***peaList, U32 containerID) {
	if (peaList) {
		int i;

		for (i=0; i < eaSize(peaList); i++) {
			ChatTeamToJoin *pTeam = eaGet(peaList, i);
			ChatPlayerStruct *pPlayer = pTeam->pLeader;
			if (pPlayer && pPlayer->pPlayerInfo.onlineCharacterID == containerID) {
				return pPlayer;
			}
		}
	}

	return NULL;
}

static ChatPlayerStruct* FindFriendByAccount(U32 accountID)
{
#ifdef USE_CHATRELAY
	return ChatCommon_FindPlayerInListByAccount(gclChat_GetFriends(), accountID);
#else
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI && pEnt->pPlayer->pUI->pChatState) {
		return ChatCommon_FindPlayerInListByAccount(&pEnt->pPlayer->pUI->pChatState->eaFriends, accountID);
	}
	return NULL;
#endif
}

ChatPlayerStruct* FindFriendByHandle(const char *pchHandle)
{
#ifdef USE_CHATRELAY
	return FindPlayerInListByHandle(gclChat_GetFriends(), pchHandle);
#else
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI && pEnt->pPlayer->pUI->pChatState) {
		return FindPlayerInListByHandle(&pEnt->pPlayer->pUI->pChatState->eaFriends, pchHandle);
	}
	return NULL;
#endif
}

static ChatPlayerStruct* FindIgnoredByAccount(U32 accountID)
{
#ifdef USE_CHATRELAY
	return ChatCommon_FindPlayerInListByAccount(gclChat_GetIgnores(), accountID);
#else
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI && pEnt->pPlayer->pUI->pChatState) {
		return ChatCommon_FindPlayerInListByAccount(&pEnt->pPlayer->pUI->pChatState->eaIgnores, accountID);
	}
	return NULL;
#endif
}

static ChatPlayerStruct* FindIgnoredByHandle(const char *pchHandle)
{
#ifdef USE_CHATRELAY
	return FindPlayerInListByHandle(gclChat_GetIgnores(), pchHandle);
#else
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI && pEnt->pPlayer->pUI->pChatState) {
		return FindPlayerInListByHandle(&pEnt->pPlayer->pUI->pChatState->eaIgnores, pchHandle);
	}
	return NULL;
#endif
}

ChatPlayerStruct *FindChatPlayerByHandle(SA_PARAM_OP_STR const char *pchHandle)
{
	ChatPlayerStruct *pStruct = FindFriendByHandle(pchHandle);
	if (pStruct) 
		return pStruct;

	pStruct = FindPlayerInListByHandle(&g_eaFoundPlayers, pchHandle);
	if (pStruct)
		return pStruct;

	return FindLeaderInTeamListByHandle(&g_eaFoundTeams, pchHandle);
}

ChatPlayerStruct *FindChatPlayerByAccountID(U32 accountID)
{
	ChatPlayerStruct *pStruct = FindFriendByAccount(accountID);
	if (pStruct) 
		return pStruct;

	pStruct = ChatCommon_FindPlayerInListByAccount(&g_eaFoundPlayers, accountID);
	if (pStruct)
		return pStruct;

	return FindLeaderInTeamListByAccount(&g_eaFoundTeams, accountID);
}
ChatPlayerStruct *FindChatPlayerByPlayerID(U32 containerID)
{
	ChatPlayerStruct *pStruct = NULL;
#ifdef USE_CHATRELAY
	pStruct = FindPlayerInListByContainerID(gclChat_GetFriends(), containerID);
#else
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI && pEnt->pPlayer->pUI->pChatState) {
		pStruct = FindPlayerInListByContainerID(&pEnt->pPlayer->pUI->pChatState->eaFriends, containerID);
	}
#endif
	if (pStruct)
		return pStruct;

	pStruct = FindPlayerInListByContainerID(&g_eaFoundPlayers, containerID);
	if (pStruct)
		return pStruct;

	return FindLeaderInTeamListByContainerID(&g_eaFoundTeams, containerID);
}


// Global functions
//
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_AcceptFriend);
void ClientChat_AcceptFriend(SA_PARAM_OP_VALID Entity *pEnt)
{
	if (pEnt)
		ServerCmd_gslChat_AcceptFriendByContainerIDCmd(pEnt->myContainerID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_AcceptFriendByAccount);
void ClientChat_AcceptFriendByAccount(U32 friendAccountID)
{
#ifdef USE_CHATRELAY
	GServerCmd_crAddFriend(GLOBALTYPE_CHATRELAY, friendAccountID, NULL, true);
#else
	ServerCmd_gslChat_AcceptFriendByAccountIDCmd(friendAccountID);
#endif
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_RejectFriend);
void ClientChat_RejectFriend(SA_PARAM_OP_VALID Entity *pEnt)
{
	if (pEnt)
		ServerCmd_gslChat_RejectFriendByContainerIDCmd(pEnt->myContainerID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_RejectFriendByAccount);
void ClientChat_RejectFriendByAccount(U32 friendAccountID)
{
#ifdef USE_CHATRELAY
	GServerCmd_crRemoveFriend(GLOBALTYPE_CHATRELAY, friendAccountID, NULL, true);
#else
	ServerCmd_gslChat_RejectFriendByAccountIDCmd(friendAccountID);
#endif
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_RemoveFriend);
void ClientChat_RemoveFriend(SA_PARAM_OP_VALID Entity *pEnt)
{
	if (pEnt)
		ServerCmd_gslChat_RemoveFriendByContainerIDCmd(pEnt->myContainerID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_RemoveFriendByAccount);
void ClientChat_RemoveFriendByAccount(U32 friendAccountID)
{
#ifdef USE_CHATRELAY
	GServerCmd_crRemoveFriend(GLOBALTYPE_CHATRELAY, friendAccountID, NULL, false);
#else
	ServerCmd_gslChat_RemoveFriendByAccountIDCmd(friendAccountID);
#endif
}

static bool FriendAccountRequestPendingInternal(ChatPlayerStruct *pPlayer)
{
	if (pPlayer) {
		return pPlayer->flags & FRIEND_FLAG_PENDINGREQUEST;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_FriendAccountRequestPending);
bool ClientChat_FriendAccountRequestPending(U32 accountId)
{
	ChatPlayerStruct *pPlayer = FindFriendByAccount(accountId);
	return FriendAccountRequestPendingInternal(pPlayer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_FriendRequestPending);
bool ClientChat_FriendRequestPendingByHandle(const char *pchHandle)
{
	ChatPlayerStruct *pPlayer = FindFriendByHandle(pchHandle);
	return FriendAccountRequestPendingInternal(pPlayer);
}

static bool FriendAccountRequestReceivedInternal(ChatPlayerStruct *pPlayer)
{
	if (pPlayer) {
		return pPlayer->flags & FRIEND_FLAG_RECEIVEDREQUEST;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_FriendAccountRequestReceived);
bool ClientChat_FriendAccountRequestReceived(U32 accountId)
{
	ChatPlayerStruct *pPlayer = FindFriendByAccount(accountId);
	return FriendAccountRequestReceivedInternal(pPlayer);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_FriendRequestReceived);
bool ClientChat_FriendRequestReceivedByHandle(const char *pchHandle)
{
	ChatPlayerStruct *pPlayer = FindFriendByHandle(pchHandle);
	return FriendAccountRequestReceivedInternal(pPlayer);
}

static bool IsFriendInternal(ChatPlayerStruct *pPlayer)
{
	if (pPlayer) {
		//TODO: Think about this.  It's possible to be both a friend and an ignored.  Is this the right answer?
		return (pPlayer->flags & FRIEND_FLAG_PENDINGREQUEST) || (pPlayer->flags == FRIEND_FLAG_NONE);
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_IsFriend);
bool ClientChat_IsFriend(const char *pchHandle)
{
	ChatPlayerStruct *pPlayer = FindFriendByHandle(pchHandle);
	return IsFriendInternal(pPlayer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_IsFriendAccount);
bool ClientChat_IsFriendAccount(U32 accountId)
{
	ChatPlayerStruct *pPlayer = FindFriendByAccount(accountId);
	return IsFriendInternal(pPlayer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_IsIgnored);
bool ClientChat_IsIgnored(const char *pchHandle)
{
	ChatPlayerStruct *pPlayer = FindIgnoredByHandle(pchHandle);
	return pPlayer != NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_IsIgnoredAccount);
bool ClientChat_IsIgnoredAccount(U32 accountId)
{
	ChatPlayerStruct *pPlayer = FindIgnoredByAccount(accountId);
	return pPlayer != NULL;
}

//static int s_iFriendRequestsReceived = 0;
//static int s_iLastFriendRequestAccount = 0;
static Queue s_qFriendRequestAccounts = NULL;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_HasReceivedFriendRequest);
bool ClientChat_FriendRequestReceived(void)
{
	return s_qFriendRequestAccounts ? !qIsEmpty(s_qFriendRequestAccounts) : false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_FriendRequestsReceived);
int ClientChat_FriendRequestsReceived(void)
{
	return s_qFriendRequestAccounts ? qGetSize(s_qFriendRequestAccounts) : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_HandleFriendRequest);
void ClientChat_HandleFriendRequest(void)
{
	ChatPlayerStruct *pPlayer = s_qFriendRequestAccounts ? qDequeue(s_qFriendRequestAccounts) : NULL;
	if (pPlayer) {
		StructDestroy(parse_ChatPlayerStruct, pPlayer);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_AcceptLastFriendRequest);
void ClientChat_AcceptLastFriendRequest(void)
{
	ChatPlayerStruct *pPlayer = s_qFriendRequestAccounts ? qDequeue(s_qFriendRequestAccounts) : NULL;
	if (pPlayer)
	{
#ifdef USE_CHATRELAY
		GServerCmd_crAddFriend(GLOBALTYPE_CHATRELAY, pPlayer->accountID, NULL, true);
#else
		ServerCmd_gslChat_AcceptFriendByAccountIDCmd(pPlayer->accountID);
#endif
		free(pPlayer);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_RejectLastFriendRequest);
void ClientChat_RejectLastFriendRequest(void)
{
	ChatPlayerStruct *pPlayer = s_qFriendRequestAccounts ? qDequeue(s_qFriendRequestAccounts) : NULL;
	if (pPlayer)
	{
#ifdef USE_CHATRELAY
		GServerCmd_crRemoveFriend(GLOBALTYPE_CHATRELAY, pPlayer->accountID, NULL, true);
#else
		ServerCmd_gslChat_RejectFriendByAccountIDCmd(pPlayer->accountID);
#endif
		free(pPlayer);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_GetLastFriendRequestName);
char *ClientChat_GetLastFriendRequestName(void)
{
	static char pcNameBuffer[256];
	ChatPlayerStruct *pPlayer = s_qFriendRequestAccounts ? qPeek(s_qFriendRequestAccounts) : NULL;
	if (pPlayer) {
		sprintf(pcNameBuffer, "%s@%s", NULL_TO_EMPTY(pPlayer->pPlayerInfo.onlinePlayerName), pPlayer->chatHandle);
	} else {
		pcNameBuffer[0] = '\0';
	}
	return pcNameBuffer;
}

void ClientChat_ReceiveFriendRequest(ChatPlayerStruct *friendStruct) {
	if (friendStruct) {
		if (!s_qFriendRequestAccounts) {
			s_qFriendRequestAccounts = createQueue();
		}
		qEnqueue(s_qFriendRequestAccounts, StructClone(parse_ChatPlayerStruct, friendStruct));
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_FindPlayersSimple);
void gclChat_FindPlayersSimple(UIGen *pGen, const char *pchFilter)
{
	//Clear the previous find
	eaClearStruct(&g_eaFoundPlayers, parse_ChatPlayerStruct);
	g_iFoundPlayerCount = -1;

	ServerCmd_cmdServerChat_FindPlayersSimple(pGen != NULL, pchFilter);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_FindPlayersSetFilter);
void gclChat_AddFilter(SA_PARAM_NN_VALID UIGen *pGen, const char *pchFilter)
{
	if (g_PlayerFilterInfo.pchFilterString) StructFreeStringSafe(&g_PlayerFilterInfo.pchFilterString);
	if (pchFilter) g_PlayerFilterInfo.pchFilterString = StructAllocString(pchFilter);
}

void gclChat_FindPlayersSimple_ErrorFunc(void)
{
	ServerCmd_cmdServerChat_FindPlayersSimple(false, "");
}

void gclChat_UpdateFoundPlayers(bool bFromSimple, ChatPlayerList *pList)
{
	Entity *pPlayer = entActivePlayerPtr();
	ChatConfig *pChatConfig = ClientChatConfig_GetChatConfig(pPlayer);
	bool bFilterProfanity = !!pChatConfig && pChatConfig->bProfanityFilter;
	int i;
	SavedMapDescription *pCurrentMap;

	g_bWhoSentList = bFromSimple;

	eaCopyStructs(&pList->chatAccounts, &g_eaFoundPlayers, parse_ChatPlayerStruct);

	// Remove the current player from list
	for (i = 0; i < eaSize(&g_eaFoundPlayers); i++) {
		if (g_eaFoundPlayers[i]->accountID == entGetAccountID(pPlayer)) {
			StructDestroy(parse_ChatPlayerStruct, eaRemove(&g_eaFoundPlayers, i));
			break;
		}
	}

	// Save the counter
	g_iFoundPlayerCount = eaSize(&g_eaFoundPlayers);

	// Profanity filtering the status should happen only once
	// so it can't be done when it fills the ClientPlayerStructs
	if (bFilterProfanity) {
		for (i = 0; i < eaSize(&g_eaFoundPlayers); i++) {
			if (g_eaFoundPlayers[i]->status) {
				ReplaceAnyWordProfane(g_eaFoundPlayers[i]->status);
			}
		}
	}

	// Sort players in current instance to the top
	pCurrentMap = entity_GetLastMap(pPlayer);
	if(pCurrentMap)
	{
		int iInstanceNumber = pCurrentMap->mapInstanceIndex;
		int iInsert = 0;
		const char *pch = zmapInfoGetPublicName(NULL);
		for (i = 0; i < eaSize(&g_eaFoundPlayers); i++)
		{
			if (g_eaFoundPlayers[i]->pPlayerInfo.playerMap.iMapInstance == iInstanceNumber &&
				!stricmp_safe(g_eaFoundPlayers[i]->pPlayerInfo.playerMap.pchMapName, pch) &&
				iInsert < i)
			{
				eaInsert(&g_eaFoundPlayers, eaRemove(&g_eaFoundPlayers, i), iInsert++);
			}
		}
	}
}

void gclChat_UpdateFoundTeams(ChatTeamToJoinList *pList)
{
	ChatConfig *pChatConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
	bool bFilterProfanity = !!pChatConfig && pChatConfig->bProfanityFilter;
	Entity *pEnt = entActivePlayerPtr();

	g_bTeamSearchSentList = true;

	eaCopyStructs(&pList->chatAccounts, &g_eaFoundTeams, parse_ChatTeamToJoin);
	g_iFoundTeamCount = eaSize(&g_eaFoundTeams);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_GetDefaultSearchMapName);
const char *gclChat_GetDefaultSearchMapName() {
	if (!g_pchDefaultMapName) {
		g_pchDefaultMapName = TranslateMessageKey(g_pchDefaultMapNameKey);
		if (!g_pchDefaultMapName) {
			return g_pchDefaultMap;
		}
	}

	return g_pchDefaultMapName;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_GetDefaultSearchNeighborhoodName);
const char *gclChat_GetDefaultSearchNeighborhoodName() {
	if (!g_pchDefaultNeighborhoodName) {
		if (g_pchDefaultNeighborhoodNameKey) {
			g_pchDefaultNeighborhoodName = TranslateMessageKey(g_pchDefaultNeighborhoodNameKey);
			if (!g_pchDefaultNeighborhoodName) {
				return ""; // Temporary until we can resolve the message key
			}
		} else {
			g_pchDefaultNeighborhoodName = "";
		}
	}

	return g_pchDefaultNeighborhoodName;

}

void gclChat_UpdateMaps(ChatMapList *pList)
{
	int i;
	Entity *pEnt = entActivePlayerPtr();
	SavedMapDescription *pCurrentMap = entity_GetLastMap(pEnt);

	// Set the defaults to point to the current map
	if(pCurrentMap)	{
		if (g_pchDefaultMap) {
			free(g_pchDefaultMap);
			g_pchDefaultMap = NULL;
		}
		if (g_pchDefaultMapNameKey) {
			free(g_pchDefaultMapNameKey);
			g_pchDefaultMapNameKey = NULL;
		}
		if (g_pchDefaultNeighborhoodNameKey) {
			free(g_pchDefaultNeighborhoodNameKey);
			g_pchDefaultNeighborhoodNameKey = NULL;
		}
		g_pchDefaultMapName = NULL;
		g_pchDefaultNeighborhoodName = NULL;
		
		g_pchDefaultMap = pCurrentMap->mapDescription ? strdup(pCurrentMap->mapDescription) : NULL;
		g_pchDefaultMapNameKey = pCurrentMap->mapVariables ? strdup(pCurrentMap->mapVariables) : NULL;

		g_pchDefaultNeighborhoodNameKey = NULL;
		if(pEnt->currentNeighborhood) {
			Message *pMsg = GET_REF(pEnt->currentNeighborhood->hMessage);
			if (pMsg)
				g_pchDefaultNeighborhoodNameKey = strdup(pMsg->pcMessageKey);
		}
	}

	// Copy the maps
	eaSetSizeStruct(&g_eaMapList, parse_ClientChatMap, eaSize(&pList->ppMapList));
	for (i=0; i < eaSize(&g_eaMapList); i++) {
		ChatMap *pChatMap = pList->ppMapList[i];
		ClientChatMap *pClientMap = g_eaMapList[i];
		estrCopy2(&pClientMap->pchMapName, TranslateMessageKeyDefault(pChatMap->pchMapNameMsgKey, pChatMap->pchMapName));
		estrCopy2(&pClientMap->pchNeighborhoodName, TranslateMessageKey(pChatMap->pchNeighborhoodNameMsgKey));
	}

	eaQSort(g_eaMapList, SortMaps);

	// This shouldn't be necessary, but for some reason the chat server sends duplicates in the list
	for (i=0; i < eaSize(&g_eaMapList) - 1; i++) {
		while (i + 1 < eaSize(&g_eaMapList) && SortMaps(&g_eaMapList[i], &g_eaMapList[i + 1]) == 0) {
			StructDestroy(parse_ClientChatMap, eaRemove(&g_eaMapList, i + 1));
		}
	}

	for (i=0; i < eaSize(&g_PlayerFilterInfo.eaMapFilters); i++) {
		if (gclChat_FindClientChatMap(&g_eaMapList, g_PlayerFilterInfo.eaMapFilters[i]) < 0) {
			StructDestroy(parse_ClientChatMap, eaRemove(&g_PlayerFilterInfo.eaMapFilters, i));
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_ClearCache);
void gclChat_ClearFindCache(SA_PARAM_OP_VALID UIGen *pGen)
{
	g_iFoundPlayerCount = -1;
	eaClearStruct(&g_eaFoundPlayers, parse_ChatPlayerStruct);
	eaClearStruct(&g_eaMapList, parse_ClientChatMap);
	eaClearStruct(&g_PlayerFilterInfo.eaMapFilters, parse_ClientChatMap);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_RequestActiveMaps);
void gclChat_RequestActiveMaps(void)
{
	GServerCmd_crGetActiveMaps(GLOBALTYPE_CHATRELAY);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_FindPlayers);
void gclChat_FindPlayers(SA_PARAM_NN_VALID UIGen *pGen,
		const char *pchNameFilter, const char *pchHandleFilter,
		const char *pchGuildFilter, 
		bool bOpen, bool bRequestOnly, bool bClosed)
{
	//Setup the filters
	PlayerFindFilterStruct *pFilters = StructCreate(parse_PlayerFindFilterStruct);
	FindFilterTokenStruct *pToken;
	FindFilterTokenStruct *pGroupToken;
	FindFilterTokenStruct *pLevelToken = NULL;
	FindFilterTokenStruct *pMapToken = NULL;
	FindFilterTokenStruct *pNeighborhoodToken = NULL;
	FindFilterTokenStruct *pPlayingStylesToken = NULL;
	const char *pchPlainHandleFilter = pchHandleFilter;
	int i, j;

	if (*pchPlainHandleFilter == '@') {
		pchPlainHandleFilter++;
	}

	if (pchNameFilter && *pchNameFilter)
	{
		pToken = StructCreate(parse_FindFilterTokenStruct);
		pToken->bCheckName = true;
		estrCopy2(&pToken->pchToken, pchNameFilter);
		eaPush(&pFilters->eaRequiredFilters, pToken);
	}

	if (pchPlainHandleFilter && *pchPlainHandleFilter)
	{
		pToken = StructCreate(parse_FindFilterTokenStruct);
		pToken->bCheckHandle = true;
		estrCopy2(&pToken->pchToken, pchPlainHandleFilter);
		eaPush(&pFilters->eaRequiredFilters, pToken);
	}

	if (pchGuildFilter && *pchGuildFilter)
	{
		pToken = StructCreate(parse_FindFilterTokenStruct);
		pToken->bCheckGuild = true;
		estrCopy2(&pToken->pchToken, pchGuildFilter);
		eaPush(&pFilters->eaRequiredFilters, pToken);
	}

	for (i = eaSize(&g_PlayerFilterInfo.eaMapFilters) - 1; i >= 0; i--)
	{
		const char *pchMapName = g_PlayerFilterInfo.eaMapFilters[i]->pchMapName;
		const char *pchNeighborhoodName = g_PlayerFilterInfo.eaMapFilters[i]->pchNeighborhoodName;

		if (pchMapName && *pchMapName)
		{
			pToken = StructCreate(parse_FindFilterTokenStruct);
			pToken->bCheckMap = true;
			pToken->bExact = true;
			estrCopy2(&pToken->pchToken, pchMapName);

			if (!pMapToken)
			{
				// Set token as top level
				pMapToken = pToken;
			}
			else if (eaSize(&pMapToken->eaAcceptableTokens))
			{
				// Add token to the list
				eaPush(&pMapToken->eaAcceptableTokens, pToken);
			}
			else
			{
				// Convert the top level token and the new token to a list
				pGroupToken = StructCreate(parse_FindFilterTokenStruct);
				eaPush(&pGroupToken->eaAcceptableTokens, pMapToken);
				eaPush(&pGroupToken->eaAcceptableTokens, pToken);
				pMapToken = pGroupToken;
			}
		}

		if (pchNeighborhoodName && *pchNeighborhoodName)
		{
			pToken = StructCreate(parse_FindFilterTokenStruct);
			pToken->bCheckNeighborhood = true;
			pToken->bExact = true;
			estrCopy2(&pToken->pchToken, pchNeighborhoodName);

			if (!pNeighborhoodToken)
			{
				// Set token as top level
				pNeighborhoodToken = pToken;
			}
			else if (eaSize(&pNeighborhoodToken->eaAcceptableTokens))
			{
				// Add token to the list
				eaPush(&pNeighborhoodToken->eaAcceptableTokens, pToken);
			}
			else
			{
				// Convert the top level token and the new token to a list
				pGroupToken = StructCreate(parse_FindFilterTokenStruct);
				eaPush(&pGroupToken->eaAcceptableTokens, pNeighborhoodToken);
				eaPush(&pGroupToken->eaAcceptableTokens, pToken);
				pNeighborhoodToken = pGroupToken;
			}
		}
	}

	if (pMapToken)
	{
		// Cull duplicated names
		for (i = eaSize(&pMapToken->eaAcceptableTokens) - 1; i >= 0; i--)
		{
			for (j = i - 1; j >= 0; j--)
			{
				if (!stricmp(pMapToken->eaAcceptableTokens[j]->pchToken, pMapToken->eaAcceptableTokens[i]->pchToken))
				{
					eaRemove(&pMapToken->eaAcceptableTokens, i);
					break;
				}
			}
		}

		eaPush(&pFilters->eaRequiredFilters, pMapToken);
	}

	if (pNeighborhoodToken)
	{
		// Cull duplicated names
		for (i = eaSize(&pNeighborhoodToken->eaAcceptableTokens) - 1; i >= 0; i--)
		{
			for (j = i - 1; j >= 0; j--)
			{
				if (!stricmp(pNeighborhoodToken->eaAcceptableTokens[j]->pchToken, pNeighborhoodToken->eaAcceptableTokens[i]->pchToken))
				{
					eaRemove(&pNeighborhoodToken->eaAcceptableTokens, i);
					break;
				}
			}
		}

		eaPush(&pFilters->eaRequiredFilters, pNeighborhoodToken);
	}

	for (i = eaSize(&g_PlayerFilterInfo.eaLevelFilters) - 1; i >= 0; i--) {
		pToken = StructCreate(parse_FindFilterTokenStruct);
		pToken->bCheckRank = true;
		pToken->iRangeLow = g_PlayerFilterInfo.eaLevelFilters[i]->iMinLevel;
		pToken->iRangeHigh = g_PlayerFilterInfo.eaLevelFilters[i]->iMaxLevel;

		if (!pLevelToken)
		{
			// Set token as top level
			pLevelToken = pToken;
		}
		else if (eaSize(&pLevelToken->eaAcceptableTokens))
		{
			// Add token to the list
			eaPush(&pLevelToken->eaAcceptableTokens, pToken);
		}
		else
		{
			// Convert the top level token and the new token to a list
			pGroupToken = StructCreate(parse_FindFilterTokenStruct);
			eaPush(&pGroupToken->eaAcceptableTokens, pLevelToken);
			eaPush(&pGroupToken->eaAcceptableTokens, pToken);
			pLevelToken = pGroupToken;
		}
	}
	if (pLevelToken)
		eaPush(&pFilters->eaRequiredFilters, pLevelToken);

	for (i = eaSize(&g_PlayerFilterInfo.eaPlayingStyles) - 1; i >= 0; i--) {
		pToken = StructCreate(parse_FindFilterTokenStruct);
		pToken->bCheckPlayingStyles = true;
		estrCopy2(&pToken->pchToken, g_PlayerFilterInfo.eaPlayingStyles[i]);

		if (!pPlayingStylesToken)
		{
			pPlayingStylesToken = pToken;
		}
		else if (eaSize(&pPlayingStylesToken->eaAcceptableTokens))
		{
			eaPush(&pPlayingStylesToken->eaAcceptableTokens, pToken);
		}
		else
		{
			pGroupToken = StructCreate(parse_FindFilterTokenStruct);
			eaPush(&pGroupToken->eaAcceptableTokens, pPlayingStylesToken);
			eaPush(&pGroupToken->eaAcceptableTokens, pToken);
			pPlayingStylesToken = pGroupToken;
		}
	}
	if (pPlayingStylesToken)
		eaPush(&pFilters->eaRequiredFilters, pPlayingStylesToken);

	if (bOpen || bRequestOnly || bClosed)
	{
		pToken = StructCreate(parse_FindFilterTokenStruct);
		pToken->bCheckOpen = bOpen;
		pToken->bCheckRequestOnly = bRequestOnly;
		pToken->bCheckClosed = bClosed;
		pToken->bSoft = true;
		pToken->bFlag = true;
		eaPush(&pFilters->eaRequiredFilters, pToken);
	}

	//Clear the previous find
	eaClearStruct(&g_eaFoundPlayers, parse_ChatPlayerStruct);
	g_iFoundPlayerCount = -1;

	if (g_PlayerFilterInfo.pchFilterString)
	{
		pFilters->pchFilterString = StructAllocString(g_PlayerFilterInfo.pchFilterString);
	}

	//Find players!
	ServerCmd_cmdServerChat_FindPlayers(pFilters);

	StructDestroy(parse_PlayerFindFilterStruct, pFilters);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_FindTeams);
void gclChat_FindTeams(UIGen *pGen, bool bFindTeams, bool bFindSolosForTeam)
{
	PlayerFindFilterStruct *pFilters = StructCreate(parse_PlayerFindFilterStruct);

	eaClearStruct(&g_eaFoundTeams, parse_ChatTeamToJoin);
	g_iFoundTeamCount = -1;

	pFilters->bFindTeams = bFindTeams;
	pFilters->bFindSoloForTeam = bFindSolosForTeam;

	ServerCmd_cmdServerChat_FindTeams(pFilters);

	StructDestroy(parse_PlayerFindFilterStruct, pFilters);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_WhoSentList);
bool gclChat_WhoSentList(void)
{
	return g_bWhoSentList;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_TeamSearchSentList);
bool gclChat_TeamSearchSentList(void)
{
	return g_bTeamSearchSentList;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_GetFoundResult);
void gclChat_FillFoundPlayers(SA_PARAM_NN_VALID UIGen *pGen)
{
	static ClientPlayerStruct **s_eaPlayers = NULL;

	g_bWhoSentList = false;

	ClientChat_FillClientPlayerStructs(&s_eaPlayers, &g_eaFoundPlayers, true, false, false);
	ui_GenSetManagedListSafe(pGen, &s_eaPlayers, ClientPlayerStruct, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_GetTeamFoundResult);
void gclChat_FillFoundTeams(SA_PARAM_NN_VALID UIGen *pGen)
{
	static ClientPlayerStruct **s_eaPlayers = NULL;

	g_bTeamSearchSentList = false;

	ClientChat_FillClientPlayerStructsFromTeams(&s_eaPlayers, &g_eaFoundTeams);
	ui_GenSetManagedListSafe(pGen, &s_eaPlayers, ClientPlayerStruct, false);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_GetMaps);
void gclChat_FillMaps(SA_PARAM_NN_VALID UIGen *pGen)
{
	static ClientChatMap **s_eaMaps = NULL;

	eaClearStruct(&s_eaMaps, parse_ClientChatMap);

	if(eaSize(&g_eaMapList))
	{	
		eaCopyStructs(&g_eaMapList, &s_eaMaps, parse_ClientChatMap);
	}
	ui_GenSetManagedListSafe(pGen, &s_eaMaps, ClientChatMap, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_GetFriendList);
void gclChat_FillFriendList(UIGen *pGen, Entity *pEnt)
{
	static ClientPlayerStruct **s_eaFriends = NULL;
	gclChat_FillFriendListStructs(&s_eaFriends, pEnt);
	ui_GenSetManagedListSafe(pGen, &s_eaFriends, ClientPlayerStruct, false);
}

void gclChat_FillFriendListStructs(ClientPlayerStruct*** peaFriends, Entity* pEnt)
{
#ifdef USE_CHATRELAY
	ClientChat_FillClientPlayerStructs(peaFriends, gclChat_GetFriends(), false, true, false);
#else
	if (pEnt && pEnt->pPlayer) {
		ClientChat_FillClientPlayerStructs(peaFriends, &pEnt->pPlayer->pUI->pChatState->eaFriends, false, true, false);
	} else {
		ClientChat_FillClientPlayerStructs(peaFriends, NULL, false, true, false);
	}
#endif
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_GetFriendCount);
S32 gclChat_GetFriendCount(SA_PARAM_OP_VALID Entity *pEnt, bool bCountOnlineOnly)
{
	const ChatPlayerStruct * const * const * peaFriendList = NULL;
	S32 iFriendCount = 0;

#ifdef USE_CHATRELAY
	peaFriendList = gclChat_GetFriends();
#else
	if (pEnt && pEnt->pPlayer)
	{
		peaFriendList = &pEnt->pPlayer->pUI->pChatState->eaFriends;
	}
#endif

	if (peaFriendList)
	{
		FOR_EACH_IN_CONST_EARRAY_FORWARDS(*peaFriendList, ChatPlayerStruct, pChatPlayer)
		{
			if (!bCountOnlineOnly || (pChatPlayer->online_status & USERSTATUS_ONLINE))
			{
				++iFriendCount;
			}
		}
		FOR_EACH_END
	}

	return iFriendCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_GetIgnoreList);
void gclChat_FillIgnoreList(UIGen *pGen, Entity *pEnt)
{
	static ClientPlayerStruct **s_eaIgnores = NULL;

#ifdef USE_CHATRELAY
	ClientChat_FillClientPlayerStructs(&s_eaIgnores, gclChat_GetIgnores(), false, true, false);
#else
	if (pEnt && pEnt->pPlayer) {
		ClientChat_FillClientPlayerStructs(&s_eaIgnores, &pEnt->pPlayer->pUI->pChatState->eaIgnores, false, false, true);
	} else {
		ClientChat_FillClientPlayerStructs(&s_eaIgnores, NULL, false, false, true);
	}
#endif

	ui_GenSetManagedListSafe(pGen, &s_eaIgnores, ClientPlayerStruct, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_FindPlayersAddMapFilter);
void gclChat_AddMapFilter(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID ClientChatMap *pMap)
{
	if (gclChat_FindClientChatMap(&g_PlayerFilterInfo.eaMapFilters, pMap) < 0) {
		eaPush(&g_PlayerFilterInfo.eaMapFilters, StructClone(parse_ClientChatMap, pMap));
		eaQSort(g_PlayerFilterInfo.eaMapFilters, SortMaps);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_FindPlayersRemoveMapFilter);
void gclChat_RemoveMapFilter(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID ClientChatMap *pMap)
{
	int pos = gclChat_FindClientChatMap(&g_PlayerFilterInfo.eaMapFilters, pMap);
	if (pos >= 0) {
		StructDestroy(parse_ClientChatMap, eaRemove(&g_PlayerFilterInfo.eaMapFilters, pos));
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_FindPlayersIsMapFilter);
bool gclChat_IsMapFilter(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID ClientChatMap *pMap)
{
	return gclChat_FindClientChatMap(&g_PlayerFilterInfo.eaMapFilters, pMap) >= 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_FindPlayersResetMapFilter);
void gclChat_ResetMapFilter(SA_PARAM_NN_VALID UIGen *pGen)
{
	eaClearStruct(&g_PlayerFilterInfo.eaMapFilters, parse_ClientChatMap);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_FindPlayersAddLevelRange);
void gclChat_AddLevelRange(SA_PARAM_NN_VALID UIGen *pGen, int iMinLevel, int iMaxLevel)
{
	int i;

	for (i = eaSize(&g_PlayerFilterInfo.eaLevelFilters) - 1; i >= 0; i--) {
		if (g_PlayerFilterInfo.eaLevelFilters[i]->iMinLevel == iMinLevel && g_PlayerFilterInfo.eaLevelFilters[i]->iMaxLevel == iMaxLevel) {
			break;
		}
	}

	if (i < 0 && iMaxLevel >= iMinLevel && iMinLevel > 0) {
		PlayerLevelFilterRange *pRange = StructCreate(parse_PlayerLevelFilterRange);
		pRange->iMinLevel = iMinLevel;
		pRange->iMaxLevel = iMaxLevel;
		eaPush(&g_PlayerFilterInfo.eaLevelFilters, pRange);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_FindPlayersRemoveLevelRange);
void gclChat_RemoveLevelRange(SA_PARAM_NN_VALID UIGen *pGen, int iMinLevel, int iMaxLevel)
{
	int i;

	for (i = eaSize(&g_PlayerFilterInfo.eaLevelFilters) - 1; i >= 0; i--) {
		if (g_PlayerFilterInfo.eaLevelFilters[i]->iMinLevel == iMinLevel && g_PlayerFilterInfo.eaLevelFilters[i]->iMaxLevel == iMaxLevel) {
			eaRemove(&g_PlayerFilterInfo.eaLevelFilters, i);
			break;
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_FindPlayersIsLevelRange);
bool gclChat_IsLevelRangeIncluded(SA_PARAM_NN_VALID UIGen *pGen, int iMinLevel, int iMaxLevel)
{
	int i;

	for (i = eaSize(&g_PlayerFilterInfo.eaLevelFilters) - 1; i >= 0; i--) {
		if (g_PlayerFilterInfo.eaLevelFilters[i]->iMinLevel == iMinLevel && g_PlayerFilterInfo.eaLevelFilters[i]->iMaxLevel == iMaxLevel) {
			return true;
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_FindPlayersResetLevelRange);
void gclChat_ResetLevelRange(SA_PARAM_NN_VALID UIGen *pGen)
{
	eaClearStruct(&g_PlayerFilterInfo.eaLevelFilters, parse_PlayerLevelFilterRange);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_GetFoundPlayers);
int gclChat_GetFoundPlayers(void)
{
	return g_iFoundPlayerCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_GetMaxFoundPlayers);
int gclChat_GetMaxFoundPlayers(void)
{
	return MAX_FIND_PLAYER_LIST;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_GetFoundTeams);
int gclChat_GetFoundTeams(void)
{
	return g_iFoundTeamCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_IsSearchingPlayingStyle);
bool gclChat_IsSearchingPlayingStyle(const char *pchType)
{
	int i;

	if (!pchType || !*pchType)
		return false;

	for (i = eaSize(&g_PlayerFilterInfo.eaPlayingStyles) - 1; i >= 0; i--)
	{
		if (!stricmp(g_PlayerFilterInfo.eaPlayingStyles[i], pchType))
		{
			return true;
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_SearchAddPlayingStyle);
void gclChat_SearchAddPlayingStyle(const char *pchType)
{
	int i;

	if (!pchType || !*pchType)
		return;

	for (i = eaSize(&g_PlayerFilterInfo.eaPlayingStyles) - 1; i >= 0; i--)
	{
		if (!stricmp(g_PlayerFilterInfo.eaPlayingStyles[i], pchType))
		{
			return;
		}
	}

	eaPush(&g_PlayerFilterInfo.eaPlayingStyles, StructAllocString(pchType));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_SearchRemovePlayingStyle);
void gclChat_SearchRemovePlayingStyle(const char *pchType)
{
	int i;

	if (!pchType || !*pchType)
		return;

	for (i = eaSize(&g_PlayerFilterInfo.eaPlayingStyles) - 1; i >= 0; i--)
	{
		if (!stricmp(g_PlayerFilterInfo.eaPlayingStyles[i], pchType))
		{
			StructFreeString(eaRemove(&g_PlayerFilterInfo.eaPlayingStyles, i));
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(social_SearchResetPlayingStyle);
void gclChat_SearchResetPlayingStyle(void)
{
	int i;
	for (i = eaSize(&g_PlayerFilterInfo.eaPlayingStyles) - 1; i >= 0; i--)
	{
		StructFreeString(eaRemove(&g_PlayerFilterInfo.eaPlayingStyles, i));
	}
}

#include "AutoGen/gclFriendsIgnore_h_ast.c"
#include "AutoGen/gclFriendsIgnore_c_ast.c"
