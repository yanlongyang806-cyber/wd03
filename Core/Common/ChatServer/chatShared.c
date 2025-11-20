#include "chatShared.h"
#include "AutoGen/chatShared_h_ast.h"

#include "AutoTransDefs.h"
#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "chatdb.h"
#include "NotifyCommon.h"
#include "objContainer.h"
#include "qsortG.h"
#include "StringUtil.h"
#include "TextFilter.h"
#include "timing.h"
#include "UtilitiesLibEnums.h"

#include "AutoGen/chatdb_h_ast.h"
#include "AutoGen/chatCommon_h_ast.h"
#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/GlobalChatServer_autotransactions_autogen_wrappers.h"

#ifdef GLOBALCHATSERVER
#include "chatGlobal.h"
#endif

extern ChatGuild **g_eaChatGuilds;

// TODO(Theo) permanent fix for this
bool gbDisableChatVoiceEnum = true;
AUTO_CMD_INT(gbDisableChatVoiceEnum, DisableVoiceEnum) ACMD_CMDLINE;

bool userIsBanned(ChatUser *user)
{
	return (user->uFlags & CHATUSER_FLAG_BANNED);
}

bool DEFAULT_LATELINK_userIsSilenced(ChatUser *user)
{
	if (UserIsAdmin(user))
		return false;
	if (userIsBanned(user))
		return true;
	if (!user->silenced)
		return false;
	else
	{
		U32 uTime = timeSecondsSince2000();
		if (user->silenced > uTime)
			return true;
		return false;
	}
}

void userGetSilenceTimeLeft (ChatUser *user, U32 *hour, U32 *min, U32 *sec)
{
	U32 uDiff = timeSecondsSince2000();
	if (uDiff < user->silenced)
	{
		uDiff = user->silenced - uDiff;
		*sec = uDiff % 60;
		uDiff /= 60;
		*min = uDiff % 60;
		*hour = uDiff / 60;
	}
	else
	{
		*hour = *min = *sec = 0;
	}
}

int userSilencedTime(ChatUser * user)
{
	if (user->silenced <= timeSecondsSince2000())
		return 0;
	else
		return (59 + user->silenced - timeSecondsSince2000()) / 60;
}

bool DEFAULT_LATELINK_channelNameIsReserved(const char *channel_name)
{
	return false;
}

int channelIsNameValid(const char *channel_name, bool adminAccess, int flags)
{
	int retVal = CHATRETURN_NONE;
	char *tempChannelName = NULL;
	int len = 0;

	estrStackCreate(&tempChannelName);
	estrCopy2(&tempChannelName, channel_name);
	estrTrimLeadingAndTrailingWhitespace(&tempChannelName);

	if (strStartsWith(channel_name, TEAM_CHANNEL_PREFIX) && !(flags & CHANNEL_SPECIAL_TEAM))
		retVal = CHATRETURN_CHANNEL_RESERVEDPREFIX; // Error: team channel name from non-team join
	else if (strStartsWith(channel_name, GUILD_CHANNEL_PREFIX) && !(flags & CHANNEL_SPECIAL_GUILD))
		retVal = CHATRETURN_CHANNEL_RESERVEDPREFIX; // Error: guild channel name from non-guild join
	else if (strStartsWith(channel_name, OFFICER_CHANNEL_PREFIX) && !(flags & CHANNEL_SPECIAL_OFFICER))
		retVal = CHATRETURN_CHANNEL_RESERVEDPREFIX; // Error: officer channel name
	else if (strStartsWith(channel_name, ZONE_CHANNEL_NAME) && !(flags & (CHANNEL_SPECIAL_ZONE | CHANNEL_SPECIAL_SHARDGLOBAL)))
		retVal = CHATRETURN_CHANNEL_RESERVEDPREFIX;
	else if (stricmp(channel_name, SHARD_GLOBAL_CHANNEL_NAME) == 0 && !(flags & CHANNEL_SPECIAL_SHARDGLOBAL))
		retVal = CHATRETURN_CHANNEL_RESERVEDPREFIX;
	else if (channelNameIsReserved(channel_name) && !(flags & CHANNEL_SPECIAL_GLOBAL))
		retVal = CHATRETURN_CHANNEL_RESERVEDPREFIX;
	else if (strStartsWith(channel_name, QUEUE_CHANNEL_PREFIX) && !(flags & CHANNEL_SPECIAL_PVP))
		retVal = CHATRETURN_CHANNEL_RESERVEDPREFIX;
	else if (strStartsWith(channel_name, VSHARD_PREFIX) && !IsChannelFlagShardOnly(flags)) // starts with virtual shard prefix and is a GLOBAL channel
		retVal = CHATRETURN_CHANNEL_RESERVEDPREFIX;
	else if (strStartsWith(channel_name, TEAMUP_CHANNEL_PREFIX) && !(flags & CHANNEL_SPECIAL_TEAMUP))
		retVal = CHATRETURN_CHANNEL_RESERVEDPREFIX;
	
	// + and . are used by XMPP for generating JIDs for channels
	if (strchr(channel_name, '+') != NULL)
		retVal = CHATRETURN_CHANNEL_RESERVEDPREFIX;

	len = estrLength(&tempChannelName);
	if (retVal == CHATRETURN_NONE)
	{
		int i;
		for (i=0; i<len; i++)
		{
			if (tempChannelName[i] == '<' || tempChannelName[i] == '>' ||
				tempChannelName[i] == '\'' || tempChannelName[i] == '"')
			{
				retVal = CHATRETURN_INVALIDNAME;
				break;
			}
		}
	}

	if(!adminAccess)
	{
		// special reserved channels skip the rest of the checks (including profanity)
		if (!flags && retVal == CHATRETURN_NONE) // continue checks if no error has been hit yet
		{
			if (len > MAX_PLAYERNAME)
				retVal = CHATRETURN_NAME_LENGTH_LONG;
			else if (len < 2)
				retVal = CHATRETURN_NAME_LENGTH_SHORT;
			else if (IsAnyProfane(tempChannelName)) 
				retVal = CHATRETURN_PROFANITY_NOT_ALLOWED;
		}
	}
	estrDestroy(&tempChannelName);
	return retVal;
}

PlayerInfoStruct * findPlayerInfoByLocalChatServerID(ChatUser *user, U32 uChatServerID)
{
	int i;
	for (i=eaSize(&user->ppPlayerInfo)-1; i>=0; i--)
	{
		if (user->ppPlayerInfo[i]->uChatServerID == uChatServerID)
			return user->ppPlayerInfo[i];
	}
	return NULL;
}

bool userOnline( ChatUser *user )
{
	if(user && (user->online_status & USERSTATUS_ONLINE))
		return true;
	return false;
}

bool userCharacterOnline( ChatUser *user )
{
	return user && userOnline(user) && (eaSize(&user->ppPlayerInfo) || user->pPlayerInfo);
}

ChatUser *userFindByContainerId(ContainerID id)
{
	Container *con = objGetContainer(GLOBALTYPE_CHATUSER, id);
	if (con)
	{
		devassert(((ChatUser*) con->containerData)->id == id);
		return (ChatUser*) con->containerData;
	}
	return NULL;
}

ChatUser *userFindByHandle(const char *handle)
{
	ChatUser *user = NULL;
	// force it to lower case
	stashFindPointer(chat_db.user_names, handle, &user);
	return user;
}

ChatUser *userFindByAccountName(const char *accountName)
{
	ChatUser *user = NULL;
	// force it to lower case
	stashFindPointer(chat_db.account_names, accountName, &user);
	return user;
}

ChatUser *userFindByContainerIdSafe(ChatUser *from, ContainerID id)
{
	ChatUser* user = userFindByContainerId(id);
	if (!user || user == from)
	{
		return NULL;
	}
	return user;
}

int userWatchingCount(ChatUser *user)
{
	return eaSize(&user->watching);
}

bool userIsIgnoringByID (ChatUser *to, ContainerID fromID, bool bIsGm)
{
	// flagged GMs can not be ignored
	if(bIsGm)
	{
		return false;
	}
	// returns if 'to' is ignoring 'from'
	return eaiFind(&to->ignore, fromID) != -1;
}

// returns true if to is ignoring from
bool userIsIgnoring (ChatUser *user, ChatUser *ignore)
{
	// higher access levels will not use ignore
	if (UserIsAdmin(ignore) && (ignore->access_level > user->access_level || ignore->access_level == 9))
	{
		return false;
	}
	// returns if 'to' is ignoring 'from'
	return eaiFind(&user->ignore, ignore->id) != -1;
}

// returns true if the user CAN ignore the ignore target
bool userCanIgnore(ChatUser *user, ChatUser *ignore)
{
	return !UserIsAdmin(ignore) || (ignore->access_level != 9 && ignore->access_level <= user->access_level);
}

bool userIsFriend (ChatUser *user, ContainerID targetID)
{
	return eaiFind(&user->friends, targetID) != -1;
}

// Return true if a friend request has been sent to this user.
bool userIsFriendSent(ChatUser *user, U32 targetID)
{
	return eaIndexedFindUsingInt(&user->friendReqs_out, targetID) != -1;
}

// Return true if a friend request has been received from this user.
bool userIsFriendReceived(ChatUser *user, U32 targetID)
{
	return eaIndexedFindUsingInt(&user->friendReqs_in, targetID) != -1;
}

Watching * userFindWatching(ChatUser *user, ChatChannel *channel)
{
	if (!user || !channel)
		return NULL;
	if (IsChannelFlagShardOnly(channel->reserved))
		return eaIndexedGetUsingString(&user->reserved, channel->name);
	else
		return eaIndexedGetUsingString(&user->watching, channel->name);
	return NULL;
}

bool ChatServerHandleIsValid(const char *targetHandle)
{
	if (strchr(targetHandle, '$') || strchr(targetHandle, '\"'))
	{
		return false;
	}
	return true;
}

extern ParseTable parse_ChatPlayerStruct[];
#define TYPE_parse_ChatPlayerStruct ChatPlayerStruct
extern ParseTable parse_PlayerInfoStruct[];
#define TYPE_parse_PlayerInfoStruct PlayerInfoStruct
// bInitialStatus is TRUE when sending the status to someone who didn't know about you before
// eg. on login, or when accepting friends
// bInitialStatus is FALSE for updates (eg. status [message] changes)
// Will copy full info if user == target, otherwise will hide some info based on USERSTATUS_HIDDEN
ChatPlayerStruct *createChatPlayerStruct (ChatUser *target, ChatUser *user, int flags, U32 uChatServerID, bool bInitialStatus)
{
	int i;
	ChatPlayerStruct *playerStruct = StructCreate(parse_ChatPlayerStruct);
	playerStruct->accountID = target->id;
	playerStruct->chatHandle = StructAllocString(target->handle);
	playerStruct->flags = flags;

	if (user && target != user)
	{
		ChatFriendComment *pComment = eaIndexedGetUsingInt(&user->friend_comments, target->id);
		if (pComment)
			estrCopy2(&playerStruct->comment, pComment->comment);
	}

	// Ignores don't need all this info
	if (flags != FRIEND_FLAG_IGNORE && userCharacterOnline(target) &&
		! ( !!(target->online_status & USERSTATUS_HIDDEN) && target != user) )
	{
		if ((!uChatServerID || uChatServerID == XMPP_CHAT_ID) && target->pPlayerInfo) // XMPP?
		{
			StructCopyAll(parse_PlayerInfoStruct, target->pPlayerInfo, &playerStruct->pPlayerInfo);
			if(target->pPlayerInfo->iPlayerGuild)
			{
				ChatGuild *pGuild = eaIndexedGetUsingInt(&g_eaChatGuilds, target->pPlayerInfo->iPlayerGuild);
				playerStruct->pchGuildName = pGuild ? StructAllocString(pGuild->pchName) : NULL;
			}
		}
		playerStruct->online_status = target->online_status;
		estrCopy2(&playerStruct->status, target->status ? target->status : "");

		if (uChatServerID)
		{
			if (eaSize(&target->ppPlayerInfo))
			{
				if (bInitialStatus)
					StructCopyAll(parse_PlayerInfoStruct, target->ppPlayerInfo[0], &playerStruct->pPlayerInfo);
				for (i=eaSize(&target->ppPlayerInfo)-1; i>=0; i--)
				{
					PlayerInfoStruct *info = target->ppPlayerInfo[i];
					if (info->uChatServerID == uChatServerID)
					{
						if (bInitialStatus)
							StructDeInit(parse_PlayerInfoStruct, &playerStruct->pPlayerInfo);
						StructCopyAll(parse_PlayerInfoStruct, info, &playerStruct->pPlayerInfo);
						if (info->iPlayerGuild)
						{
							ChatGuild *pGuild = eaIndexedGetUsingInt(&g_eaChatGuilds, info->iPlayerGuild);
							playerStruct->pchGuildName = pGuild ? StructAllocString(pGuild->pchName) : NULL;
						}
						break;
					}
				}
			}
		}
	}
	else
	{
		playerStruct->online_status = USERSTATUS_OFFLINE;
		playerStruct->pPlayerInfo.onlineCharacterID = 0;
	}

	return playerStruct;
}

extern bool userPassesFilters(ChatUser *user, PlayerFindFilterStruct *pFilters, U32 uChatServerID);
int getOnlineList(ChatPlayerList *list, PlayerFindFilterStruct *pFilters, U32 uChatServerID)
{
	ContainerIterator iter = {0};
	ChatUser *user;

	if(!pFilters->pCachedSearcher)
	{
		return 0;
	}

	objInitContainerIteratorFromType(GLOBALTYPE_CHATUSER, &iter);
	user = objGetNextObjectFromIterator(&iter);
	while (user)
	{
		if(userCharacterOnline(user) && userPassesFilters(user, pFilters, uChatServerID))
		{
			// compare to ( MAX_FIND_PLAYER_LIST - 1 ) because eaPush() returns index of pushed element,
			//  which is one less than the size after pushing.
			if(eaPush(&list->chatAccounts, createChatPlayerStruct(user, NULL, 0, uChatServerID, false)) >= (MAX_FIND_PLAYER_LIST - 1))
				break;
		}
		user = objGetNextObjectFromIterator(&iter);
	}
	objClearContainerIterator(&iter);

	return eaSize(&list->chatAccounts);
}

void ChatStripNonGlobalData(ChatMessage *pMsg) {
	if (pMsg) {
		// Strip player names/id's from forwarded messages
		// Because they are not valid when sent through global
		// chat server.
		if (pMsg->pFrom) {
			pMsg->pFrom->playerID = 0;
			estrClear(&pMsg->pFrom->pchName);
		}

		if (pMsg->pTo) {
			pMsg->pTo->playerID = 0;
			estrClear(&pMsg->pTo->pchName);
		}
	}
}

// Does not count GMs
int getChannelMemberCount(SA_PARAM_NN_VALID ChatChannel *channel)
{
	int i, count = 0;
	for (i=eaiSize(&channel->members)-1; i>=0; i--)
	{
		ChatUser *user = userFindByContainerId(channel->members[i]);
		// Assume that unknown users are non-GMs
		if (!user || !UserIsAdmin(user))
			count++;
	}
	return count;
}

// Assumes that all the parameters match; if watch == NULL, it assumes it's an invitee
ChatChannelMember *copyChannelMember (ChatUser *user, ChatChannel *channel, Watching *watch)
{
	ChatChannelMember *member = StructCreate(parse_ChatChannelMember);
	member->uID = user->id;
	estrCopy2(&member->handle, user->handle);
	member->bOnline = userOnline(user);

	if (watch)
	{
		member->eUserLevel = watch->ePermissionLevel;
		member->uPermissionFlags = channel->permissionLevels[member->eUserLevel];
		member->bSilenced = channelIsUserSilenced(user->id, channel);
		member->bInvited = false;
		if (UserIsAdmin(user))
		{
			member->eUserLevel = CHANUSER_GM;
			member->uPermissionFlags = CHANPERM_OWNER;
		}
	}
	else
	{
		member->eUserLevel = CHANUSER_USER;
		member->uPermissionFlags = 0;
		member->bSilenced = false;
		member->bInvited = true;
	}
	return member;
}

// user is for copying user info, NULL if you don't care about that
// bShowGMCount is for including GMs in the data - false if GMs should be hidden in the list
ChatChannelInfo *copyChannelInfo(ChatUser *user, ChatChannel *channel, Watching *watching, 
								 bool bCopyMembers, bool bShowGMCount)
{
	int i;
	if (channel) {
		ChatChannelInfo *chaninfo = StructCreate(parse_ChatChannelInfo);
		int motdSize;

		estrCopy2(&chaninfo->pName, channel->name);
		estrCopy2(&chaninfo->pDisplayName, convertExactChannelName(channel->name, channel->reserved));

		if (motdSize = eaSize(&channel->messageOfTheDay)) {
			ChatUser *fromUser;
			const Email *motd = channel->messageOfTheDay[motdSize-1];
			if (motd && motd->body) {
				fromUser = userFindByContainerId(motd->from);

				estrCopy2(&chaninfo->motd.body, motd->body);
				chaninfo->motd.userID = motd->from;
				if (fromUser)
					estrCopy2(&chaninfo->motd.userHandle, fromUser->handle);
				chaninfo->motd.uTime = motd->sent;
			}
		}
		estrCopy2(&chaninfo->pDescription, channel->description);

		if (channel->uMemberCount == 0 || bShowGMCount)
		{
			chaninfo->uMemberCount = eaiSize(&channel->members);
			chaninfo->uOnlineMemberCount = eaiSize(&channel->online);
		}
		else
		{
			chaninfo->uMemberCount = MIN(channel->uMemberCount, eaiUSize(&channel->members));
			chaninfo->uOnlineMemberCount = MIN(channel->uOnlineCount, eaiUSize(&channel->online));
		}
		chaninfo->uInvitedMemberCount = eaiSize(&channel->invites);

		chaninfo->eChannelAccess = channel->access & (CHANFLAGS_COPYMASK);
		if (gbDisableChatVoiceEnum)
			chaninfo->eChannelAccess &= (~CHANFLAGS_VOICE);

		if (channel->reserved) {
			chaninfo->eChannelAccess |= CHATACCESS_RESERVED;
			chaninfo->uReservedFlags = channel->reserved;
		}

		for (i=0; i<CHANUSER_COUNT; i++) {
			chaninfo->permissionLevels[i] = channel->permissionLevels[i];
		}

		chaninfo->voiceEnabled = channel->voiceEnabled;
		chaninfo->voiceId = channel->voiceId;
		chaninfo->voiceURI = StructAllocString(channel->voiceURI);

		if (watching) {
			chaninfo->eUserLevel = watching->ePermissionLevel;
			chaninfo->uPermissionFlags = channel->permissionLevels[watching->ePermissionLevel];
			chaninfo->bSilenced = channelIsUserSilenced(user->id, channel);
			if (user)
			{
				chaninfo->uAccessLevel = user->access_level;
				if (UserIsAdmin(user))
				{   // GM settings - give all permisions
					chaninfo->bSilenced = false;
					chaninfo->eUserLevel = CHANUSER_GM;
					chaninfo->uPermissionFlags = CHANPERM_OWNER;
				}

			}
			chaninfo->bUserSubscribed = true;
		}
		else if (user) // check for invites if no watching
			chaninfo->bUserInvited = eaiFind(&channel->invites, user->id) >= 0;

		if (bCopyMembers)
		{
			int iTotalMembers = eaiSize(&channel->members);
			for (i=0; i < iTotalMembers; i++)
			{
				ChatUser *userMember = userFindByContainerId(channel->members[i]);
				Watching *userWatch = userFindWatching(userMember, channel);
				if (userMember && userWatch && (!UserIsAdmin(userMember) || bShowGMCount))
				{
					ChatChannelMember *member = copyChannelMember(userMember, channel, userWatch);
					eaPush(&chaninfo->ppMembers, member);
				}
			}

			// Add invited members
			for (i=0; i < (int) chaninfo->uInvitedMemberCount; i++) {
				U32 userID = eaiGet(&channel->invites, i);
				ChatUser *userMember = userFindByContainerId(userID);
				if (userMember) {
					ChatChannelMember *member = copyChannelMember(userMember, channel, NULL);
					eaPush(&chaninfo->ppMembers, member);
				}
			}
			chaninfo->bMembersInitialized = true; // set this flag to true
		}
		return chaninfo;
	}
	return NULL;
}

// This is only used for single USER updates; sending updates to all users should be done by sending to the shard
// For _MEMBER_ updates, the user is the member being updated or removed
ChatChannelInfo * ChatServerCreateChannelUpdate (ChatUser *user, SA_PARAM_NN_VALID ChatChannel *channel, 
												 ChannelUpdateEnum eUpdateType)
{
	ChatChannelInfo *pInfo = NULL;
	switch (eUpdateType) // Initialize the channel info based on what the update requires
	{
	case CHANNELUPDATE_UPDATE: // also for adding channels
	case CHANNELUPDATE_UPDATE_NO_MEMBERS: // intentional fall-through
		if (devassert(user))
		{   // Get all of the channel data appropriate for the user
			pInfo = copyChannelInfo(user, channel, channelFindWatching(user, channel->name), 
				eUpdateType == CHANNELUPDATE_UPDATE, UserIsAdmin(user));
		}
	xcase CHANNELUPDATE_REMOVE:
		{
			pInfo = StructCreate(parse_ChatChannelInfo);
			estrCopy2(&pInfo->pName, channel->name);
		}
	// Member changes should also explicitly update all member counts
	xcase CHANNELUPDATE_MEMBER_UPDATE: // also for adding members
	case CHANNELUPDATE_MEMBER_REMOVE: // intentional fall-through - really only care about the id/handle here
		if (devassert(user))
		{
			Watching *watch = channelFindWatching(user, channel->name);
			ChatChannelMember *member = copyChannelMember(user, channel, watch);
			pInfo = StructCreate(parse_ChatChannelInfo);
			estrCopy2(&pInfo->pName, channel->name);
			eaPush(&pInfo->ppMembers, member);

			pInfo->uMemberCount = channel->uMemberCount;
			pInfo->uOnlineMemberCount = channel->uOnlineCount;
			pInfo->uInvitedMemberCount = eaiSize(&channel->invites);
		}
	xcase CHANNELUPDATE_VOICE_ENABLED:
		{
			pInfo = StructCreate(parse_ChatChannelInfo);
			estrCopy2(&pInfo->pName, channel->name);
			pInfo->voiceEnabled = channel->voiceEnabled;
			pInfo->voiceId = channel->voiceId;
			pInfo->voiceURI = StructAllocString(channel->voiceURI);
		}
	xcase CHANNELUPDATE_DESCRIPTION: // Channel description
		{
			pInfo = StructCreate(parse_ChatChannelInfo);
			estrCopy2(&pInfo->pName, channel->name);
			if (channel->description)
				estrCopy2(&pInfo->pDescription, channel->description);
		}
	xcase CHANNELUPDATE_MOTD: // Message of the Day
		{
			int motdSize;
			pInfo = StructCreate(parse_ChatChannelInfo);
			estrCopy2(&pInfo->pName, channel->name);

			if (motdSize = eaSize(&channel->messageOfTheDay)) {
				ChatUser *fromUser;
				const Email *motd = channel->messageOfTheDay[motdSize-1];
				if (motd && motd->body) {
					fromUser = userFindByContainerId(motd->from);

					estrCopy2(&pInfo->motd.body, motd->body);
					pInfo->motd.userID = motd->from;
					if (fromUser)
						estrCopy2(&pInfo->motd.userHandle, fromUser->handle);
					pInfo->motd.uTime = motd->sent;
				}
			}
		}
	xcase CHANNELUPDATE_CHANNEL_PERMISSIONS:
		{
			int i;
			pInfo = StructCreate(parse_ChatChannelInfo);
			estrCopy2(&pInfo->pName, channel->name);

			pInfo->eChannelAccess = channel->access & (CHANFLAGS_COPYMASK);
			if (gbDisableChatVoiceEnum)
				pInfo->eChannelAccess &= (~CHANFLAGS_VOICE);

			if (channel->reserved) {
				pInfo->eChannelAccess |= CHATACCESS_RESERVED;
				pInfo->uReservedFlags = channel->reserved;
			}
			for (i=0; i<CHANUSER_COUNT; i++) {
				pInfo->permissionLevels[i] = channel->permissionLevels[i];
			}
		}
	xcase CHANNELUPDATE_USER_PERMISSIONS:
		if (devassert(user))
		{
			Watching *watching = channelFindWatching(user, channel->name);
			pInfo = StructCreate(parse_ChatChannelInfo);
			estrCopy2(&pInfo->pName, channel->name);
			if (watching)
			{
				pInfo->uAccessLevel = user->access_level;
				if (UserIsAdmin(user))
				{
					pInfo->eUserLevel = CHANUSER_GM;
					pInfo->uPermissionFlags = CHANPERM_OWNER;
				}
				else
				{
					pInfo->bSilenced = channelIsUserSilenced(user->id, channel);
					pInfo->eUserLevel = watching->ePermissionLevel;
					pInfo->uPermissionFlags = channel->permissionLevels[watching->ePermissionLevel];
				}
			}
			else // check for invites if no watching
				pInfo->bUserInvited = eaiFind(&channel->invites, user->id) >= 0;
			if (!pInfo->bUserSubscribed && !pInfo->bUserInvited) // this is an odd case that should never happen
			{
				StructDestroy(parse_ChatChannelInfo, pInfo);
				pInfo = NULL;
			}
		}
	xcase CHANNELUPDATE_USER_ONLINE:
		if (devassert(user))
		{
			Watching *watch = channelFindWatching(user, channel->name);
			ChatChannelMember *member = StructCreate(parse_ChatChannelMember);
			member->uID = user->id;
			estrCopy2(&member->handle, user->handle);
			member->bOnline = true;

			pInfo = StructCreate(parse_ChatChannelInfo);
			estrCopy2(&pInfo->pName, channel->name);
			eaPush(&pInfo->ppMembers, member);
			pInfo->uOnlineMemberCount = channel->uOnlineCount;
		}
	xcase CHANNELUPDATE_USER_OFFLINE:
		if (devassert(user))
		{
			Watching *watch = channelFindWatching(user, channel->name);
			ChatChannelMember *member = StructCreate(parse_ChatChannelMember);
			member->uID = user->id;
			estrCopy2(&member->handle, user->handle);
			member->bOnline = false;

			pInfo = StructCreate(parse_ChatChannelInfo);
			estrCopy2(&pInfo->pName, channel->name);
			eaPush(&pInfo->ppMembers, member);
			pInfo->uOnlineMemberCount = channel->uOnlineCount;
		}
	}
	return pInfo;
}

//Returns true if "from" is whitelisted to talk to "to" on the specified channel
bool isWhitelisted(ChatUser* from, ChatUser* to, const char* channelName, U32 uChatServerID)
{
	S32 toGuild = 0, fromGuild = 0, toTeam = 0, fromTeam = 0;
	PlayerInfoStruct *fromInfo = NULL, *toInfo = NULL;

	//Check to make sure the users aren't NULL
	if(!from || !to)
		return false;
	if(from->id == to->id)
		return true;
	if(UserIsAdmin(from) && (from->access_level > to->access_level || from->access_level == 9))
		return true;

	//Get guild and team info
	if (uChatServerID)
	{
		fromInfo = findPlayerInfoByLocalChatServerID(from, uChatServerID);
		toInfo = findPlayerInfoByLocalChatServerID(to, uChatServerID);
	}
	else
	{
		fromInfo = from->pPlayerInfo;
		toInfo = to->pPlayerInfo;
	}

	if (fromInfo && toInfo)
	{
		fromGuild = fromInfo->iPlayerGuild;
		fromTeam = fromInfo->iPlayerTeam;
		toGuild = toInfo->iPlayerGuild;
		toTeam = toInfo->iPlayerTeam;
	}

	//if whitelist enabled only receive message if it is from friends,SG, orteam
	if(to->chatWhitelistEnabled) {
		//Are the players friends or is the channel the team channel?
		if(!userIsFriend(from, to->id)) {
			//Are the players in different guilds?
			if((!fromGuild || !toGuild) || (fromGuild != toGuild)) {
				//Are the players on a different team?
				if((!fromTeam || !toTeam) || (fromTeam != toTeam)) {
					return false;
				}
			}
		}
	} else if(to->tellWhitelistEnabled) {
		//Only filter private messages
		if(stricmp(channelName, PRIVATE_CHANNEL_NAME) == 0) {
			//Are the players friends or guild members?
			if(!userIsFriend(from, to->id) && ((!fromGuild || !toGuild) || (fromGuild != toGuild))) {
				//Are the players on a different team?
				if((!fromTeam || !toTeam) || (fromTeam != toTeam)) {
					return false;
				}
			}
		}
	}
	return true;
}

//Returns true if "from" is whitelisted to send emails to "to"
bool isEmailWhitelisted(ChatUser* from, ChatUser* to, U32 uChatServerID)
{
	S32 toGuild = 0, fromGuild = 0, toTeam = 0, fromTeam = 0;

	PlayerInfoStruct *fromInfo = NULL, *toInfo = NULL;

	//Check to make sure the users aren't NULL
	if(!from || !to)
		return false;
	if(from->id == to->id)
		return true;
	if(UserIsAdmin(from) && (from->access_level > to->access_level || from->access_level == 9))
		return true;

	//Get guild and team info
	if (uChatServerID)
	{
		fromInfo = findPlayerInfoByLocalChatServerID(from, uChatServerID);
		toInfo = findPlayerInfoByLocalChatServerID(to, uChatServerID);
	}
	else
	{
		fromInfo = from->pPlayerInfo;
		toInfo = to->pPlayerInfo;
	}

	if (fromInfo && toInfo)
	{
		fromGuild = fromInfo->iPlayerGuild;
		fromTeam = fromInfo->iPlayerTeam;
		toGuild = toInfo->iPlayerGuild;
		toTeam = toInfo->iPlayerTeam;
	}

	//if email whitelist enabled only receive emails if it is from friends, SG, or team
	if(to->emailWhitelistEnabled) {
		//Are the players friends?
		if(!userIsFriend(from, to->id) && from->id != to->id) {
			//Are the players in different guilds?
			if((!fromGuild || !toGuild) || (fromGuild != toGuild)) {
				//Are the players on a different team?
				if((!fromTeam || !toTeam) || (fromTeam != toTeam)) {
					return false;
				}
			}
		}
	} 

	return true;
}


ChatChannel *channelFindByName(const char *channel_name)
{
	if (!channel_name)
		return NULL;
	return stashFindPointerReturnPointer(chat_db.channel_names,channel_name);
}

ChatChannel *channelFindByID(U32 id)
{
	Container *con = objGetContainer(GLOBALTYPE_CHATCHANNEL, id);
	if (con)
		return (ChatChannel*) con->containerData;
	return NULL;
}

Watching *channelFindWatching(ChatUser *user,const char *channel_name)
{
	if (!user) return NULL;
	return eaIndexedGetUsingString(&user->watching, channel_name);
}

Watching *channelFindWatchingReserved(ChatUser *user, const char *channel_name)
{
	EARRAY_CONST_FOREACH_BEGIN(user->reserved, i, s);
	{
		if (stricmp(user->reserved[i]->name,channel_name)==0)
			return user->reserved[i];
	}
	EARRAY_FOREACH_END;
	return NULL;
}

Watching *channelFindWatchingReservedByType(ChatUser *user, int reserved_flag)
{
	const char *channel_prefix;
	if (reserved_flag == CHANNEL_SPECIAL_TEAM)
		channel_prefix = TEAM_CHANNEL_PREFIX;
	else if (reserved_flag == CHANNEL_SPECIAL_ZONE)
		channel_prefix = ZONE_CHANNEL_PREFIX;
	else if (reserved_flag == CHANNEL_SPECIAL_GUILD)
		channel_prefix = GUILD_CHANNEL_PREFIX;
	else if (reserved_flag == CHANNEL_SPECIAL_OFFICER)
		channel_prefix = OFFICER_CHANNEL_PREFIX;
	else
		return NULL;
	EARRAY_CONST_FOREACH_BEGIN(user->reserved, i, s);
	{
		if (strStartsWith(user->reserved[i]->name, channel_prefix))
			return user->reserved[i];
	}
	EARRAY_FOREACH_END;
	return NULL;
}

bool channelIsUserSilenced(U32 userID, SA_PARAM_NN_VALID ChatChannel *channel)
{
	int idx = (int) eaiBFind(channel->mutedUsers, userID);
	return (idx < eaiSize(&channel->mutedUsers) && channel->mutedUsers[idx] == userID);
}

#ifdef GLOBALCHATSERVER
#include "chatGlobalConfig.h"
extern GlobalChatServerConfig gGlobalChatServerConfig;
#else
#include "chatShardConfig.h"
extern ShardChatServerConfig gShardChatServerConfig;
#endif
const char *convertExactChannelName (const char *channelName, U32 uReservedFlags)
{
	if (uReservedFlags & CHANNEL_SPECIAL_TEAM)
	{
		return TranslateMessageKeyDefault("ChatServer_Special_" CHAT_TEAM_SHORTCUT, CHAT_TEAM_SHORTCUT);
	} else if (uReservedFlags & CHANNEL_SPECIAL_ZONE)
	{
		return TranslateMessageKeyDefault("ChatServer_Special_" CHAT_ZONE_SHORTCUT, CHAT_ZONE_SHORTCUT);
	} else if (uReservedFlags & CHANNEL_SPECIAL_GUILD)
	{
		return TranslateMessageKeyDefault("ChatServer_Special_" CHAT_GUILD_SHORTCUT, CHAT_GUILD_SHORTCUT);
	} else if (uReservedFlags & CHANNEL_SPECIAL_OFFICER)
	{
		return TranslateMessageKeyDefault("ChatServer_Special_" CHAT_GUILD_OFFICER_SHORTCUT, CHAT_GUILD_OFFICER_SHORTCUT);
	} else if (uReservedFlags & CHANNEL_SPECIAL_TEAMUP)
	{
		return TranslateMessageKeyDefault("ChatServer_Special_" CHAT_TEAMUP_SHORTCUT, CHAT_TEAMUP_SHORTCUT);
	} else if (uReservedFlags & CHANNEL_SPECIAL_SHARDGLOBAL)
	{
		SpecialChannelDisplayNameMap *map;
		const char *strippedName = ShardChannel_StripVShardPrefix(channelName);

#ifdef GLOBALCHATSERVER
		map = eaIndexedGetUsingString(&gGlobalChatServerConfig.ppSpecialChannelMap, strippedName);
#else
		map = eaIndexedGetUsingString(&gShardChatServerConfig.ppSpecialChannelMap, strippedName);
#endif
		if (map && map->displayChannelName && *map->displayChannelName)
		{
			return map->displayChannelName;
		}
		return strippedName;
	}
	return channelName;
}

bool userChangeActivityString (ChatUser *user, const char *msg)
{
#ifdef GLOBALCHATSERVER
	PlayerInfoStruct *pStruct = NULL;
	U32 uChatServerID = GetLocalChatLinkID(chatServerGetCommandLink());

	if (!userOnline(user) || !uChatServerID)
		return false;
	pStruct = findPlayerInfoByLocalChatServerID(user, uChatServerID);
	if (!pStruct)
		return false;
	if (strcmp_safe(msg, pStruct->playerActivity) == 0)
		return false;
	SAFE_FREE(pStruct->playerActivity);
	if (msg && *msg)
		pStruct->playerActivity = strdup(msg);
#else
	if (!user || !user->pPlayerInfo)
		return false;
	if (strcmp_safe(msg, user->pPlayerInfo->playerActivity) == 0)
		return false;
	SAFE_FREE(user->pPlayerInfo->playerActivity);
	if (msg && *msg)
		user->pPlayerInfo->playerActivity = strdup(msg);
#endif
	return true;
}

bool userChangeStatusAndActivity(ChatUser *user, UserStatus addStatus, UserStatus removeStatus, const char *msg, bool bSetStatusMessage)
{
	bool bChanged = false;

	if (!userOnline(user))
		return 0;

	if (msg && bSetStatusMessage)
		ReplaceAnyWordProfane((char *)msg); // evil cast

	if (addStatus && ((U32)addStatus & user->online_status) != (U32)addStatus)
	{
		bChanged = true;
		user->online_status |= addStatus;
	}

	if (removeStatus && (removeStatus & user->online_status) != 0)
	{
		bChanged = true;
		user->online_status &= (~removeStatus);
	}

	if (bSetStatusMessage)
	{
		bChanged = userChangeActivityString(user, msg);
	}
	return bChanged;
}

#include "AutoGen/chatShared_h_ast.c"