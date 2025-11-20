#include "chatUtilities.h"
#include "ChatServer/chatShared.h"

#include "AutoTransDefs.h"
#include "channels.h"
#include "chatCommandStrings.h"
#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "chatdb.h"
#include "chatGlobal.h"
#include "chatGlobalConfig.h"
#include "chatGuild.h"
#include "ChatServer.h"
#include "friendsIgnore.h"
#include "msgsend.h"
#include "NotifyCommon.h"
#include "objContainer.h"
#include "StringUtil.h"
#include "timing.h"
#include "users.h"
#include "userPermissions.h"
#include "UtilitiesLibEnums.h"

#include "AutoGen/chatdb_h_ast.h"
#include "AutoGen/chatCommon_h_ast.h"
#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/GlobalChatServer_autotransactions_autogen_wrappers.h"

extern GlobalChatServerConfig gGlobalChatServerConfig;

bool OVERRIDE_LATELINK_channelNameIsReserved(const char *channel_name)
{
	int i;
	for (i=eaSize(&gGlobalChatServerConfig.ppReservedChannelNames)-1; i>=0; i--)
	{
		if (stricmp(gGlobalChatServerConfig.ppReservedChannelNames[i], channel_name) == 0)
			return true;
	}
	return false;
}

AUTO_TRANSACTION
	ATR_LOCKS(user, ".id, .watching, .invites, .access_level")
	ATR_LOCKS(channel, ".name, .uKey, .invites, .members, .uMemberCount");
enumTransactionOutcome trChannelRemoveUser(ATR_ARGS, NOCONST(ChatUser) *user, NOCONST(ChatChannel) *channel)
{
	int idx = eaIndexedFindUsingString(&user->watching, channel->name);
	if (ISNULL(channel) || ISNULL(user))
		return TRANSACTION_OUTCOME_FAILURE;
	if (idx >= 0)
	{
		NOCONST(Watching) *watch = user->watching[idx];
		eaRemove(&user->watching, idx);
		StructDestroyNoConst(parse_Watching, watch);	
	}
	if (eaiFindAndRemove(&channel->members, user->id) >= 0 && user->access_level < ACCESS_GM) // Using UserIsAdmin doesn't work with ATR locking
	{
		channel->uMemberCount--;
	}
	eaiFindAndRemove(&channel->invites, user->id);
	eaiFindAndRemove(&user->invites, channel->uKey);
	return TRANSACTION_OUTCOME_SUCCESS;
}

int channelRemoveUser(ChatChannel *channel, ChatUser *user)
{
	NOCONST(ChatUser)* userMod = CONTAINER_NOCONST(ChatUser, user);
	NOCONST(ChatChannel)* channelMod = CONTAINER_NOCONST(ChatChannel, channel);
	bool bSubscribed = false;

	if (!channel || !user)
		return 0;

	// Considered as a "member" if either of the following apply:
	//   Channels thinks user is member
	//   User thinks they are a member
	if (eaiFind(&channel->members, user->id) >= 0)
		bSubscribed = true;
	else if (eaIndexedFindUsingString(&user->watching, channel->name) >= 0)
		bSubscribed = true;
	
	AutoTrans_trChannelRemoveUser(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, GLOBALTYPE_CHATCHANNEL, channel->uKey);
	// Non-persisted field that can't be removed in transactions
	eaiFindAndRemove(&channelMod->online, user->id);
	return bSubscribed;
}

//int changeAccess(ChannelAccess *pAccess,  char *options)
int parseAccessString(ChannelAccess *pAccess, const char *options)
{
	unsigned char	*s;
	U32				flag;
	ChannelAccess access = *pAccess;
	char * optionCopy = strdup(options);
	char *tokContext = NULL;
	char *curTok;
	bool bFailedParse = false;

	curTok = strtok_s(optionCopy, " ", &tokContext);

	while (curTok)
	{
		s = curTok;
		if(!*s || (*s != '-' && *s != '+'))
		{
			bFailedParse = true;
			break;
		}
		s++;
		flag = 0;
		if (stricmp(s,"Join")==0)
			flag = CHANFLAGS_JOIN;
		else if (stricmp(s,"Send")==0)
			flag = CHANFLAGS_SEND;
		else if (stricmp(s,"Operator")==0)
			flag = CHANFLAGS_OPERATOR;
		else
		{
			bFailedParse = true;
			break;
		}
		if (s[-1] == '-')
			access &= ~flag;
		else
			access |= flag;

		curTok = strtok_s(NULL, " ", &tokContext);
	}

	free(optionCopy);
	if (bFailedParse)
	{
		return 0;
	}
	*pAccess = access;
	return 1;
}

int parseUserAccessStringChanges (U32 *pAdd, U32 *pRemove, const char *options)
{
	unsigned char	*s;
	U32				flag;
	char * optionCopy = strdup(options);
	char *tokContext = NULL;
	char *curTok;
	bool bFailedParse = false;

	curTok = strtok_s(optionCopy, " ", &tokContext);

	*pAdd = *pRemove = 0; // clear these
	while (curTok)
	{
		s = curTok;
		if(!*s || (*s != '-' && *s != '+'))
		{
			bFailedParse = true;
			break;
		}
		s++;
		flag = 0;
		if (stricmp(s,"Join")==0)
			flag = CHANPERM_KICK;
		else if (stricmp(s,"Send")==0)
			flag = CHANPERM_MUTE;
		else if (stricmp(s,"Operator")==0)
			flag = CHANPERM_PROMOTE;
		else
		{
			bFailedParse = true;
			break;
		}
		if (s[-1] == '-')
		{
			if (flag == CHANPERM_PROMOTE)
				*pRemove |= CHANPERM_DEMOTE;
			else
				*pRemove |= flag;
		}
		else
		{
			*pAdd |= flag;
		}

		curTok = strtok_s(NULL, " ", &tokContext);
	}

	free(optionCopy);
	if (bFailedParse)
	{
		return 0;
	}
	return 1;
}


static bool userTokenFilter(ChatUser *user, FindFilterTokenStruct *pToken, const char *pchMapName, const char *pchNeighborhoodName, ChatGuild *pGuild)
{
	if (pToken->bExact)
	{
		if (pToken->bSoft)
		{
			if (pToken->bCheckRank && pToken->iRangeLow <= user->pPlayerInfo->iPlayerLevel && user->pPlayerInfo->iPlayerLevel <= pToken->iRangeHigh)
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
			}

			return false;
		}
		else
		{
			if (pToken->bCheckRank && (user->pPlayerInfo->iPlayerLevel < pToken->iRangeLow || pToken->iRangeHigh < user->pPlayerInfo->iPlayerLevel))
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
			}

			return true;
		}
	}
	else
	{
		if (pToken->bSoft)
		{
			if (pToken->bCheckRank && pToken->iRangeLow <= user->pPlayerInfo->iPlayerLevel && user->pPlayerInfo->iPlayerLevel <= pToken->iRangeHigh)
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
			}

			return false;
		}
		else
		{
			if (pToken->bCheckRank && (user->pPlayerInfo->iPlayerLevel < pToken->iRangeLow || pToken->iRangeHigh < user->pPlayerInfo->iPlayerLevel))
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

	if(!user || !user->pPlayerInfo)
		return false;

	if(user->pPlayerInfo->onlineCharacterAccessLevel > pFilters->iMaxAccessLevel)
		return false;

	if(!pFilters || !eaSize(&pFilters->eaExcludeFilters) && !eaSize(&pFilters->eaRequiredFilters))
		return true;

	pchMapName = langTranslateMessageKeyDefault(pFilters->eLanguage, user->pPlayerInfo->playerMap.pchMapNameMsgKey, user->pPlayerInfo->playerMap.pchMapName);
	pchNeighborhoodName = langTranslateMessageKey(pFilters->eLanguage, user->pPlayerInfo->playerMap.pchNeighborhoodNameMsgKey);

	if (user->pPlayerInfo->iPlayerGuild)
	{
		pGuild = GlobalChatFindGuild(uChatServerID, user->pPlayerInfo->iPlayerGuild);
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

// Fill in missing ChatUserInfo data from the ChatUser
void ChatFillUserInfo(ChatUserInfo **ppInfo, const ChatUser *pUser) {
	ChatUserInfo *pInfo;
	PlayerInfoStruct *pPlayerInfo = NULL;
	int i;

	if (!ppInfo || !pUser) {
		return;
	}

	if (!*ppInfo) {
		*ppInfo = StructCreate(parse_ChatUserInfo);
	}
	pInfo = *ppInfo;

	// Find player info struct using either the player ID or name from the pInfo
	if (pInfo->playerID || pInfo->pchName) {
		for (i=eaSize(&pUser->ppPlayerInfo)-1; i>=0; i--) {
			PlayerInfoStruct *pCandiatePlayer = eaGet(&pUser->ppPlayerInfo, i);
			if (pCandiatePlayer && pCandiatePlayer->onlineCharacterID == pInfo->playerID) {
				pPlayerInfo = pCandiatePlayer;
				break;
			}

			if (!stricmp_safe(pCandiatePlayer->onlinePlayerName, pInfo->pchName)) {
				pPlayerInfo = pCandiatePlayer;
				break;
			}
		}
	}

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

// Return true if all characters that are onle are restricted (chat && mail)
bool UserIsChatMailRestricted(ChatUser *pUser)
{
	if(pUser)
	{
		S32 i;
		for(i = 0; i < eaSize(&pUser->ppPlayerInfo); ++i)
		{
			PlayerInfoStruct *pInfo = pUser->ppPlayerInfo[i];
			if(!pInfo || pInfo->eGamePermissionInfo != CHAT_GAME_PERMISSION_INFO_RESTRICTED)
			{
				// a character was not restricted
				return false;
			}
		}
		
		// all online characters are restricted
		return true;
	}
	
	return false;
}

U32 GetMailRate(ChatUser *pUser)
{
	if(gGlobalChatServerConfig.iRestrictedMailRate && UserIsChatMailRestricted(pUser))
	{
		// restricted rate
		return gGlobalChatServerConfig.iRestrictedMailRate;
	}
	
	// normal rate
	return gGlobalChatServerConfig.iMailRate;
}

bool mailRateLimiter(ChatUser *user)
{
	U32 curTime = timeSecondsSince2000();
	U32 duration;
	U32 uMailRate;

	//Check to make sure gGlobalChatServerConfig was initialized
	if(!gGlobalChatServerConfig.iMailRate || !gGlobalChatServerConfig.iMaxSpamMail) {
		return false;
	}

	uMailRate = GetMailRate(user);

	//If time between messages is less than allowed chat rate, increment spam messages
	if (user->lastMailTime)
	{
		if((curTime - user->lastMailTime) < uMailRate) {	
			user->spamMails++;
		} else {
            // Decrement the spam counter once for every uMailRate seconds it has been since the last message.
            user->spamMails -= ( (curTime - user->lastMailTime) / uMailRate );
			if(user->spamMails < 0) {
				user->spamMails = 0;
			}
		}
	}
	user->lastMailTime = curTime;

	//If user has more spam messages than the allowed number, silence them
	if(user->spamMails > gGlobalChatServerConfig.iMaxSpamMail)
	{
		userSpamSilence(user, curTime);
		user->spamMails = gGlobalChatServerConfig.iResetSpamMail;
		duration = user->silenced-curTime;

		sendTranslatedMessageToUser(user, kChatLogEntryType_System, PRIVATE_CHANNEL_NAME, "ChatServer_MsgRateExceeded", 
			STRFMT_INT("H", duration/(3600)), STRFMT_INT("M", (duration/60)%60), STRFMT_INT("S", duration%60), STRFMT_END);

		//Increment the naughty value
		userIncrementNaughtyValue(user, gGlobalChatServerConfig.iChatRateNaughtyIncrement, "Mail Spam");

		//Make log entry
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_SEND, PRIVATE_CHANNEL_NAME, user, CHATRETURN_USER_PERMISSIONS);
		return true;
	} else if(userIsSilenced(user)) {
		//If user is already silenced, let them know they still can't talk
		duration = user->silenced-curTime;
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, PRIVATE_CHANNEL_NAME, "ChatServer_Silenced", 
			STRFMT_INT("H", duration/(3600)), STRFMT_INT("M", (duration/60)%60), STRFMT_INT("S", duration%60), STRFMT_END);

		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_SEND, PRIVATE_CHANNEL_NAME, user, CHATRETURN_USER_PERMISSIONS);
		return true;
	}
	return false;
}

U32 GetChatRate(ChatUser *pUser)
{
	if(gGlobalChatServerConfig.iRestrictedChatRate && UserIsChatMailRestricted(pUser))
	{
		// restricted rate
		return gGlobalChatServerConfig.iRestrictedChatRate;
	}

	// normal rate
	return gGlobalChatServerConfig.iChatRate;
}

//Checks to see if the user is sending messages too fast.  If so, the user will be silenced.
bool chatRateLimiter(ChatUser* user)
{
	U32 curTime = timeSecondsSince2000();
	U32 duration;
	U32 uChatRate;

	//Check to make sure gGlobalChatServerConfig was initialized
	if(!gGlobalChatServerConfig.iChatRate || !gGlobalChatServerConfig.iMaxSpamMessages) {
		return false;
	}
	
	uChatRate = GetChatRate(user);
	
	//If time between messages is less than allowed chat rate, increment spam messages
	if (user->lastMessageTime)
	{
		if((curTime - user->lastMessageTime) < uChatRate) {	
			user->spamMessages++;
		} else {
            // Decrement the spam counter once for every uChatRate seconds it has been since the last message.
            user->spamMessages -= ( (curTime - user->lastMessageTime) / uChatRate );
			if(user->spamMessages < 0) {
				user->spamMessages = 0;
			}
		}
	}
	user->lastMessageTime = curTime;

	//If user has more spam messages than the allowed number, silence them
	if(user->spamMessages > gGlobalChatServerConfig.iMaxSpamMessages)
	{
		userSpamSilence(user, curTime);
		user->spamMessages = gGlobalChatServerConfig.iResetSpamMessages;
		duration = user->silenced-curTime;
		
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, PRIVATE_CHANNEL_NAME, "ChatServer_MsgRateExceeded", 
			STRFMT_INT("H", duration/(3600)), STRFMT_INT("M", (duration/60)%60), STRFMT_INT("S", duration%60), STRFMT_END);
		
		//Increment the naughty value
		userIncrementNaughtyValue(user, gGlobalChatServerConfig.iChatRateNaughtyIncrement, "Chat Spam");

		//Make log entry
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_SEND, PRIVATE_CHANNEL_NAME, user, CHATRETURN_USER_PERMISSIONS);
		return true;
	} else if(userIsSilenced(user)) {
		//If user is already silenced, let them know they still can't talk
		duration = user->silenced-curTime;

		sendTranslatedMessageToUser(user, kChatLogEntryType_System, PRIVATE_CHANNEL_NAME, "ChatServer_Silenced", 
			STRFMT_INT("H", duration/(3600)), STRFMT_INT("M", (duration/60)%60), STRFMT_INT("S", duration%60), STRFMT_END);

		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_SEND, PRIVATE_CHANNEL_NAME, user, CHATRETURN_USER_PERMISSIONS);
		return true;
	}
	return false;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void ChatServerReassociateUser (U32 uID)
{
	ChatUser *user = userFindByContainerId(uID), *existingUser;
	if (!user)
		return;
	if (stashFindPointer(chat_db.user_names, user->handle, &existingUser))
	{
		if (user->uHandleUpdateTime > existingUser->uHandleUpdateTime)
			stashAddPointer(chat_db.user_names, user->handle, user, true);
	}
	else stashAddPointer(chat_db.user_names, user->handle, user, true);

	if (!stashFindPointer(chat_db.account_names, user->accountName, &existingUser))
	{
		stashAddPointer(chat_db.account_names, user->accountName, user, true);
	}
}
