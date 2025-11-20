#include "chatUtilities.h"

#include "AutoTransDefs.h"
#include "channels.h"
#include "chatCommandStrings.h"
#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "chatdb.h"
#include "chatGlobal.h"
#include "ChatServer.h"
#include "ChatServer/chatShared.h"
#include "chatShardConfig.h"
#include "chatVoice.h"
#include "entEnums.h"
#include "friendsIgnore.h"
#include "msgsend.h"
#include "NotifyCommon.h"
#include "objContainer.h"
#include "ResourceInfo.h"
#include "StringUtil.h"
#include "timing.h"
#include "timing_profiler.h"
#include "users.h"

#include "AutoGen/chatdb_h_ast.h"
#include "AutoGen/chatCommon_h_ast.h"
#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/ChatServer_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"

extern ShardChatServerConfig gShardChatServerConfig;
extern bool gbNoShardMode;

void reservedChannelRemoveUser(ChatUser *user, const char *channel_name)
{
	int idx;
	NOCONST(ChatChannel) *channel = CONTAINER_NOCONST(ChatChannel, channelFindByName(channel_name));
	if (!channel || ISNULL(user))
		return;

	idx = eaIndexedFindUsingString(&user->reserved, channel->name);
	if (idx >= 0)
	{
		Watching *watch = user->reserved[idx];
		eaRemove(&user->reserved, idx);
		StructDestroy(parse_Watching, watch);	
	}
	eaiFindAndRemove(&channel->members, user->id);
}

// Only called for reserved channels on the Shard
int channelRemoveUser(ChatChannel *channel, ChatUser *user)
{
	NOCONST(ChatChannel)* channelMod = CONTAINER_NOCONST(ChatChannel, channel);
	bool bSubscribed = false;

	if (!channel || !user || !channel->reserved)
		return 0;
	
	if (eaiFind(&channel->members, user->id) >= 0)
		bSubscribed = true;
	else if (eaIndexedFindUsingString(&user->reserved, channel->name) >= 0)
		bSubscribed = true;

	reservedChannelRemoveUser(user, channel->name);
	// These are non-persisted fields and can't be removed in transactions
	eaiFindAndRemove(&channelMod->online, user->id);

	if(cvIsVoiceEnabled() && channel->voiceEnabled)
		chatVoiceLeave(channel, user);

	return bSubscribed;
}

bool userTokenFilter(ChatUser *user, FindFilterTokenStruct *pToken, const char *pchMapName, const char *pchNeighborhoodName, ChatGuild *pGuild)
{	
	if (pToken->bExact)
	{
		if (pToken->bSoft)
		{
			// need exact string matches, but not all the searched fields need to be checked
			if (pToken->bCheckRank && pToken->iRangeLow <= user->pPlayerInfo->iPlayerLevel && user->pPlayerInfo->iPlayerLevel <= pToken->iRangeHigh)
				return true;
			if (pToken->bCheckInstance && pToken->iRangeLow <= user->pPlayerInfo->playerMap.iMapInstance && user->pPlayerInfo->playerMap.iMapInstance <= pToken->iRangeHigh)
				return true;

			if (pToken->bCheckOpen && pToken->bFlag && user->pPlayerInfo->eLFGMode == TeamMode_Open)
				return true;
			if (pToken->bCheckRequestOnly && pToken->bFlag && user->pPlayerInfo->eLFGMode == TeamMode_RequestOnly)
				return true;
			if (pToken->bCheckClosed && pToken->bFlag && user->pPlayerInfo->eLFGMode == TeamMode_Closed)
				return true;

			if (pToken->pchToken && *pToken->pchToken)
			{
				if (pToken->bCheckName && user->pPlayerInfo->onlinePlayerName && !stricmp(user->pPlayerInfo->onlinePlayerName, pToken->pchToken))
					return true;
				if (pToken->bCheckHandle && user->handle && !stricmp(user->handle, pToken->pchToken))
					return true;
				if (pToken->bCheckStatus && user->status && !stricmp(user->status, pToken->pchToken))
					return true;
				if (pToken->bCheckMap && pchMapName && !stricmp(pchMapName, pToken->pchToken))
					return true;
				if (pToken->bCheckNeighborhood && pchNeighborhoodName && !stricmp(pchNeighborhoodName, pToken->pchToken))
					return true;
				if (pToken->bCheckGuild && pGuild && pGuild->pchName && !stricmp(pGuild->pchName, pToken->pchToken))
					return true;
				if (pToken->bCheckPlayingStyles && user->pPlayerInfo->playingStyles && !stricmp(user->pPlayerInfo->playingStyles, pToken->pchToken))
					return true;
			}

			return false;
		}
		else
		{
			// need exact string matches, and all the searched fields must be checked
			if (pToken->bCheckRank && (user->pPlayerInfo->iPlayerLevel < pToken->iRangeLow || pToken->iRangeHigh < user->pPlayerInfo->iPlayerLevel))
				return false;
			if (pToken->bCheckInstance && (user->pPlayerInfo->playerMap.iMapInstance < pToken->iRangeLow || pToken->iRangeHigh < user->pPlayerInfo->playerMap.iMapInstance))
				return false;

			if (pToken->bCheckOpen && pToken->bFlag && user->pPlayerInfo->eLFGMode != TeamMode_Open)
				return false;
			if (pToken->bCheckRequestOnly && pToken->bFlag && user->pPlayerInfo->eLFGMode != TeamMode_RequestOnly)
				return false;
			if (pToken->bCheckClosed && pToken->bFlag && user->pPlayerInfo->eLFGMode != TeamMode_Closed)
				return false;

			if (pToken->pchToken && *pToken->pchToken)
			{
				if (pToken->bCheckName && (!user->pPlayerInfo->onlinePlayerName || stricmp(user->pPlayerInfo->onlinePlayerName, pToken->pchToken)))
					return false;
				if (pToken->bCheckHandle && (!user->handle || stricmp(user->handle, pToken->pchToken)))
					return false;
				if (pToken->bCheckStatus && (!user->status || stricmp(user->status, pToken->pchToken)))
					return false;
				if (pToken->bCheckMap && (!pchMapName || stricmp(pchMapName, pToken->pchToken)))
					return false;
				if (pToken->bCheckNeighborhood && (!pchNeighborhoodName || stricmp(pchNeighborhoodName, pToken->pchToken)))
					return false;
				if (pToken->bCheckGuild && (!pGuild || !pGuild->pchName || stricmp(pGuild->pchName, pToken->pchToken)))
					return false;
				if (pToken->bCheckPlayingStyles && (!user->pPlayerInfo->playingStyles || stricmp(user->pPlayerInfo->playingStyles, pToken->pchToken)))
					return false;
			}

			return true;
		}
	}
	else
	{
		if (pToken->bSoft)
		{
			// does not require exact string matches, and doesn't require all the fields to be checked
			if (pToken->bCheckRank && pToken->iRangeLow <= user->pPlayerInfo->iPlayerLevel && user->pPlayerInfo->iPlayerLevel <= pToken->iRangeHigh)
				return true;
			if (pToken->bCheckInstance && pToken->iRangeLow <= user->pPlayerInfo->playerMap.iMapInstance && user->pPlayerInfo->playerMap.iMapInstance <= pToken->iRangeHigh)
				return true;

			if (pToken->bCheckOpen && pToken->bFlag && user->pPlayerInfo->eLFGMode == TeamMode_Open)
				return true;
			if (pToken->bCheckRequestOnly && pToken->bFlag && user->pPlayerInfo->eLFGMode == TeamMode_RequestOnly)
				return true;
			if (pToken->bCheckClosed && pToken->bFlag && user->pPlayerInfo->eLFGMode == TeamMode_Closed)
				return true;

			if (pToken->pchToken && *pToken->pchToken)
			{
				if (pToken->bCheckName && user->pPlayerInfo->onlinePlayerName && strstri(user->pPlayerInfo->onlinePlayerName, pToken->pchToken))
					return true;
				if (pToken->bCheckHandle && user->handle && strstri(user->handle, pToken->pchToken))
					return true;
				if (pToken->bCheckStatus && user->status && strstri(user->status, pToken->pchToken))
					return true;
				if (pToken->bCheckMap && pchMapName && strstri(pchMapName, pToken->pchToken))
					return true;
				if (pToken->bCheckNeighborhood && pchNeighborhoodName && strstri(pchNeighborhoodName, pToken->pchToken))
					return true;
				if (pToken->bCheckGuild && pGuild && pGuild->pchName && strstri(pGuild->pchName, pToken->pchToken))
					return true;
				if (pToken->bCheckPlayingStyles && user->pPlayerInfo->playingStyles && strstri(user->pPlayerInfo->playingStyles, pToken->pchToken))
					return true;
			}

			return false;
		}
		else
		{
			// does not require exact string matches, but does require all the fields to be checked
			if (pToken->bCheckRank && (user->pPlayerInfo->iPlayerLevel < pToken->iRangeLow || pToken->iRangeHigh < user->pPlayerInfo->iPlayerLevel))
				return false;
			if (pToken->bCheckInstance && (user->pPlayerInfo->playerMap.iMapInstance < pToken->iRangeLow || pToken->iRangeHigh < user->pPlayerInfo->playerMap.iMapInstance))
				return false;

			if (pToken->bCheckOpen && pToken->bFlag && user->pPlayerInfo->eLFGMode != TeamMode_Open)
				return false;
			if (pToken->bCheckRequestOnly && pToken->bFlag && user->pPlayerInfo->eLFGMode != TeamMode_RequestOnly)
				return false;
			if (pToken->bCheckClosed && pToken->bFlag && user->pPlayerInfo->eLFGMode != TeamMode_Closed)
				return false;

			if (pToken->pchToken && *pToken->pchToken)
			{
				if (pToken->bCheckName && (!user->pPlayerInfo->onlinePlayerName || !strstri(user->pPlayerInfo->onlinePlayerName, pToken->pchToken)))
					return false;
				if (pToken->bCheckHandle && (!user->handle || !strstri(user->handle, pToken->pchToken)))
					return false;
				if (pToken->bCheckStatus && (!user->status || !strstri(user->status, pToken->pchToken)))
					return false;
				if (pToken->bCheckMap && (!pchMapName || !strstri(pchMapName, pToken->pchToken)))
					return false;
				if (pToken->bCheckNeighborhood && (!pchNeighborhoodName || !strstri(pchNeighborhoodName, pToken->pchToken)))
					return false;
				if (pToken->bCheckGuild && (!pGuild || !pGuild->pchName || !strstri(pGuild->pchName, pToken->pchToken)))
					return false;
				if (pToken->bCheckPlayingStyles && (!user->pPlayerInfo->playingStyles || !strstri(user->pPlayerInfo->playingStyles, pToken->pchToken)))
					return false;
			}

			return true;
		}
	}
}

bool userPassesFilters(ChatUser *user, PlayerFindFilterStruct *pFilters, U32 uChatServerID)
{
	ChatGuild *pGuild = NULL;
	const char *pchMapName = NULL;
	const char *pchNeighborhoodName = NULL;
	int i, j;
	bool bHasList, bPassList;
	ChatUser *searchUser = pFilters->pCachedSearcher;
	bool bFoundFriend = false;
	bool bAddToTeammates = false;

	if(!user || !user->pPlayerInfo)
		return false;

	if(user->pPlayerInfo->onlineCharacterAccessLevel > pFilters->iMaxAccessLevel)
		return false;
	
	if ((user->online_status & USERSTATUS_HIDDEN) && !pFilters->bFindAnonymous)
		return false;

	if (((user->online_status & USERSTATUS_FRIENDSONLY) && !pFilters->bFindAnonymous) ||
		pFilters->bFindSoloForTeam || pFilters->bFindTeams)
	{
		// Check for friend if FRIENDSONLY is set, or we're doing a team search
		if (searchUser == user)
		{
			bFoundFriend = true;
		}
		else if (searchUser->pPlayerInfo && searchUser->pPlayerInfo->iPlayerTeam && searchUser->pPlayerInfo->iPlayerTeam == user->pPlayerInfo->iPlayerTeam)
		{
			bFoundFriend = true;
		}
		else if (searchUser->pPlayerInfo && searchUser->pPlayerInfo->iPlayerGuild && searchUser->pPlayerInfo->iPlayerGuild == user->pPlayerInfo->iPlayerGuild)
		{
			bFoundFriend = true;
		}
		else
		{
			for (i = eaiSize(&user->friends) - 1; i >= 0; i--)
			{
				if (user->friends[i] == searchUser->id)
				{
					bFoundFriend = true;
					break;
				}
			}
		}
	}

	if ((user->online_status & USERSTATUS_FRIENDSONLY) && !pFilters->bFindAnonymous)
	{
		// Only allow search if they are teamed, guilded, or friended
		if (!bFoundFriend)
			return false;
	}

	if (pFilters->bFindTeams || pFilters->bFindSoloForTeam)
	{
		user->searchFriend = bFoundFriend;		

		if (searchUser == user || (user->pPlayerInfo->iPlayerTeam && searchUser->pPlayerInfo && user->pPlayerInfo->iPlayerTeam == searchUser->pPlayerInfo->iPlayerTeam))
		{
			// Always skip yourself and your team
			return false;
		}				
		else if (pFilters->bFindTeams && user->pPlayerInfo->iPlayerTeam != 0 && user->pPlayerInfo->eTeamMode == TeamMode_Prompt)
		{
			// Possible teammate, but isn't a valid leader
			eaPush(&pFilters->ppPossibleTeammates, user);
			return false;
		}		
		else if (!(pFilters->bFindTeams && (user->pPlayerInfo->eTeamMode == TeamMode_Open || user->pPlayerInfo->eTeamMode == TeamMode_RequestOnly)) &&
			!(pFilters->bFindSoloForTeam && user->pPlayerInfo->iPlayerTeam == 0 && (user->pPlayerInfo->eLFGMode == TeamMode_Open || user->pPlayerInfo->eLFGMode == TeamMode_RequestOnly)))
		{		
			// If we're looking for teams or solos for teams, exclude everyone else
			return false;
		}
	}

	if(!pFilters || !eaSize(&pFilters->eaExcludeFilters) && !eaSize(&pFilters->eaRequiredFilters))
		return true;

	pchMapName = langTranslateMessageKeyDefault(pFilters->eLanguage, user->pPlayerInfo->playerMap.pchMapNameMsgKey, user->pPlayerInfo->playerMap.pchMapName);
	pchNeighborhoodName = langTranslateMessageKey(pFilters->eLanguage, user->pPlayerInfo->playerMap.pchNeighborhoodNameMsgKey);

	if (user->pPlayerInfo->iPlayerGuild)
	{
		pGuild = eaIndexedGetUsingInt(&g_eaChatGuilds, user->pPlayerInfo->iPlayerGuild);
	}

	bHasList = false;
	for (i = eaSize(&pFilters->eaExcludeFilters) - 1; i >= 0; i--)
	{
		if (eaSize(&pFilters->eaExcludeFilters[i]->eaAcceptableTokens))
		{
			bHasList = true;
		}
		else
		{
			if (userTokenFilter(user, pFilters->eaExcludeFilters[i], pchMapName, pchNeighborhoodName, pGuild))
				return false;
		}
	}

	if (bHasList)
	{
		for (i = eaSize(&pFilters->eaExcludeFilters) - 1; i >= 0; i--)
		{
			if (!eaSize(&pFilters->eaExcludeFilters[i]->eaAcceptableTokens))
				continue;

			bPassList = true;
			for (j = eaSize(&pFilters->eaExcludeFilters[i]->eaAcceptableTokens) - 1; j >= 0; j--)
			{
				if (!userTokenFilter(user, pFilters->eaExcludeFilters[i]->eaAcceptableTokens[j], pchMapName, pchNeighborhoodName, pGuild))
				{
					bPassList = false;
					break;
				}
			}

			if (bPassList)
				return false;
		}
	}

	bHasList = false;
	for (i = eaSize(&pFilters->eaRequiredFilters) - 1; i >= 0; i--)
	{
		if (eaSize(&pFilters->eaRequiredFilters[i]->eaAcceptableTokens))
		{
			bHasList = true;
		}
		else
		{
			if (!userTokenFilter(user, pFilters->eaRequiredFilters[i], pchMapName, pchNeighborhoodName, pGuild))
				return false;
		}
	}

	if (bHasList)
	{
		for (i = eaSize(&pFilters->eaRequiredFilters) - 1; i >= 0; i--)
		{
			if (!eaSize(&pFilters->eaRequiredFilters[i]->eaAcceptableTokens))
				continue;

			bPassList = false;
			for (j = eaSize(&pFilters->eaRequiredFilters[i]->eaAcceptableTokens) - 1; j >= 0; j--)
			{
				if (userTokenFilter(user, pFilters->eaRequiredFilters[i]->eaAcceptableTokens[j], pchMapName, pchNeighborhoodName, pGuild))
				{
					bPassList = true;
					break;
				}
			}

			if (!bPassList)
				return false;
		}
	}

	return true;
}

int cmpTeamID(const ChatUser **left, const ChatUser **right)
{
	int team_left =  (*left)->pPlayerInfo->iPlayerTeam;
	int team_right =  (*right)->pPlayerInfo->iPlayerTeam;

	return team_left < team_right ? -1 : 1;
	
}

int cmpTeamRating(const ChatTeamToJoin **left, const ChatTeamToJoin **right)
{
	F32 rating_left =  (*left)->fTeamRating;
	F32 rating_right =  (*right)->fTeamRating;

	// Sort higher first
	return rating_left < rating_right ? 1 : -1;
}

#define MAX_TEAMS_RETURNED 25

F32 computeTeamRating(ChatTeamToJoin *pTeam, ChatUser *pSearcher)
{
	// Add more complicated metrics, for now it's based on average level and if you are friends
	F32 rating = 0.0f;
	F32 levelDiff;

	if (!pSearcher || !pSearcher->pPlayerInfo)
	{
		return 0;
	}
	if (pTeam->bHasFriend)
	{
		rating += 50.0f;
	}
	
	levelDiff = ABS((F32)pSearcher->pPlayerInfo->iPlayerLevel - pTeam->fAverageLevel);

	rating -= levelDiff;

	if (stricmp(pSearcher->pPlayerInfo->playerMap.pchMapName, pTeam->pLeader->pPlayerInfo.playerMap.pchMapName) == 0)
	{
		rating += 10.0f;
		if (pSearcher->pPlayerInfo->playerMap.iMapInstance == pTeam->pLeader->pPlayerInfo.playerMap.iMapInstance)
		{
			rating += 10.0f;
			if (stricmp(pSearcher->pPlayerInfo->playerMap.pchNeighborhoodNameMsgKey, pTeam->pLeader->pPlayerInfo.playerMap.pchNeighborhoodNameMsgKey) == 0)
			{
				rating += 10.0f;
			}
		}
	}

	return rating;
}

int getListOfTeams(ChatTeamToJoinList *list, PlayerFindFilterStruct *pFilters)
{
	ContainerIterator iter = {0};
	ChatUser *user;
	int i;

	if (!pFilters->pCachedSearcher)
	{
		return 0;
	}

	objInitContainerIteratorFromType(GLOBALTYPE_CHATUSER, &iter);
	user = objGetNextObjectFromIterator(&iter);
	while (user)
	{
		if(userCharacterOnline(user) && userPassesFilters(user, pFilters, 0))
		{
			ChatTeamToJoin *pTeam = StructCreate(parse_ChatTeamToJoin);
			pTeam->pLeader = createChatPlayerStruct(user, user, 0, 0, false);
			pTeam->bHasFriend = user->searchFriend;
			pTeam->leaderUser = user;
			eaPush(&list->chatAccounts, pTeam);
		}
		user = objGetNextObjectFromIterator(&iter);

		if (eaSize(&list->chatAccounts) >= MAX_FIND_PLAYER_LIST - 1)
			break;
	}
	objClearContainerIterator(&iter);

	eaQSort(pFilters->ppPossibleTeammates, cmpTeamID);

	// Match up leader with team

	for (i = 0; i < eaSize(&list->chatAccounts); i++)
	{
		ChatTeamToJoin *pTeam = list->chatAccounts[i];
		ContainerID leaderTeam = pTeam->leaderUser->pPlayerInfo->iPlayerTeam;
		int totalLevel = pTeam->leaderUser->pPlayerInfo->iPlayerLevel;
		if (leaderTeam > 0)
		{		
			int index = 0;
			while (index < eaSize(&pFilters->ppPossibleTeammates) && (ContainerID)pFilters->ppPossibleTeammates[index]->pPlayerInfo->iPlayerTeam != leaderTeam)
			{
				index++;
			}
			while (index < eaSize(&pFilters->ppPossibleTeammates) && (ContainerID)pFilters->ppPossibleTeammates[index]->pPlayerInfo->iPlayerTeam == leaderTeam)
			{
				ChatUser *pTeamUser = pFilters->ppPossibleTeammates[index];
				ChatTeamMemberStruct *pTeamMember = StructCreate(parse_ChatTeamMemberStruct);
				pTeamMember->playingStyles = pTeamUser->pPlayerInfo->playingStyles;
				pTeamMember->iPlayerLevel = pTeamUser->pPlayerInfo->iPlayerLevel;
				pTeamMember->iPlayerRank = pTeamUser->pPlayerInfo->iPlayerRank;
				pTeamMember->chatHandle = strdup(pTeamUser->accountName);
				pTeamMember->onlinePlayerName = strdup(pTeamUser->pPlayerInfo->onlinePlayerName);

				if (pTeamUser->searchFriend) 
					pTeam->bHasFriend = true;

				totalLevel += pTeamUser->pPlayerInfo->iPlayerLevel;

				eaPush(&pTeam->ppTeamMembers, pTeamMember);
				index++;
			}
		}
		pTeam->fAverageLevel = (F32)totalLevel / (eaSize(&pTeam->ppTeamMembers) + 1);
		pTeam->fTeamRating = computeTeamRating(pTeam, pFilters->pCachedSearcher);
	}

	eaQSort(list->chatAccounts, cmpTeamRating);

	for (i = eaSize(&list->chatAccounts) - 1; i >= MAX_TEAMS_RETURNED; i--)
	{
		StructDestroy(parse_ChatTeamToJoin, eaRemove(&list->chatAccounts, i));
	}

	// This has weak references
	eaDestroy(&pFilters->ppPossibleTeammates);

	return eaSize(&list->chatAccounts);
}

static EARRAY_OF(ChatMap) sMapListCache = NULL;

void addChatMapCacheToMonitor(void)
{
	if (!sMapListCache)
		eaIndexedEnable(&sMapListCache, parse_ChatMap);
	resRegisterDictionaryForEArray("ChatMap Cache", RESCATEGORY_OTHER, 0, &sMapListCache, parse_ChatMap);
}

static int findMapInCache(const ChatMap *pMap)
{
	static char buffer[256];
	sprintf(buffer, "%s - %s", 
		nullStr(pMap->pchMapName) ? "???" : pMap->pchMapName,
		nullStr(pMap->pchNeighborhoodNameMsgKey) ? "???" : pMap->pchNeighborhoodNameMsgKey);
	return eaIndexedFindUsingString(&sMapListCache, buffer);
}

void userCharacterChangeMap(const ChatMap *pOldMap, const ChatMap *pNewMap)
{
	int idx;
	if (!( (pOldMap && pOldMap->pchMapName) || (pNewMap && pNewMap->pchMapName) ))
		return;
	if (pOldMap && pNewMap && pOldMap->pchMapName == pNewMap->pchMapName &&
		pOldMap->pchNeighborhoodNameMsgKey == pNewMap->pchNeighborhoodNameMsgKey)
		return;
	PERFINFO_AUTO_START_FUNC();
	if (pOldMap)
	{
		idx = findMapInCache(pOldMap);
		if (devassert(idx != -1 && sMapListCache[idx]->uNumPlayers > 0))
			sMapListCache[idx]->uNumPlayers--;
		if (idx != -1 && sMapListCache[idx]->uNumPlayers == 0)
		{
			StructDestroy(parse_ChatMap, sMapListCache[idx]);
			eaRemove(&sMapListCache, idx);
		}
	}
	if (pNewMap)
	{
		idx = findMapInCache(pNewMap);
		if (idx == -1)
		{
			ChatMap *pExisting = StructCreate(parse_ChatMap);
			pExisting->pchMapName = pNewMap->pchMapName;
			pExisting->pchMapNameMsgKey = pNewMap->pchMapNameMsgKey;
			pExisting->pchNeighborhoodNameMsgKey = pNewMap->pchNeighborhoodNameMsgKey;
			pExisting->eMapType = pNewMap->eMapType;
			pExisting->uNumPlayers = 1;
			estrPrintf(&pExisting->pKey, "%s - %s",
				nullStr(pNewMap->pchMapName) ? "???" : pNewMap->pchMapName,
				nullStr(pNewMap->pchNeighborhoodNameMsgKey) ? "???" : pNewMap->pchNeighborhoodNameMsgKey);
			if (!sMapListCache)
				eaIndexedEnable(&sMapListCache, parse_ChatMap);
			eaIndexedAdd(&sMapListCache, pExisting);
		}
		else
			sMapListCache[idx]->uNumPlayers++;
	}
	PERFINFO_AUTO_STOP_FUNC();
}

// Note: We explicitly do not include map instance as a key
//       because that may create too many combinations.
//       This function is primarily used to populate a combo box
//       in "who/search" UI.
int getActiveMaps(ChatMapList *pList)
{
	pList->ppMapList = sMapListCache;
	return eaSize(&pList->ppMapList);
}

// Fill in missing ChatUserInfo data from the ChatUser
void ChatFillUserInfo(ChatUserInfo **ppInfo, const ChatUser *pUser) {
	ChatUserInfo *pInfo;
	PlayerInfoStruct *pPlayerInfo = NULL;

	if (!ppInfo || !pUser) {
		return;
	}

	if (!*ppInfo) {
		*ppInfo = StructCreate(parse_ChatUserInfo);
	}
	pInfo = *ppInfo;

	// Find player info struct using either the player ID or name from the pInfo
	pPlayerInfo = pUser->pPlayerInfo;

	// Set the account ID
	if (!pInfo->accountID) {
		pInfo->accountID = pUser->id;
	}

	// Set or overwrite the handle
	if (pUser->handle && *pUser->handle) {
		estrCopy2(&pInfo->pchHandle, pUser->handle);
	}

	// Set or overwrite the player ID, if known
	if (pPlayerInfo) {
		pInfo->playerID = pPlayerInfo->onlineCharacterID;
		pInfo->bIsGM = pPlayerInfo->bIsGM;
		pInfo->bIsDev = pPlayerInfo->bIsDev;
	}

	// Set or overwrite the player name, if known
	if (pPlayerInfo && pPlayerInfo->onlinePlayerName && *pPlayerInfo->onlinePlayerName) {
		estrCopy2(&pInfo->pchName, pPlayerInfo->onlinePlayerName);
	}
}
