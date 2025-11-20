#include "channels.h"
#include "chatCommandStrings.h"
#include "chatdb.h"
#include "chatGlobal.h"
#include "chatLocal.h"
#include "chatRelay/chatRelayManager.h"
#include "ChatServer.h"
#include "ChatServer/chatShared.h"
#include "chatShardConfig.h"
#include "chatUtilities.h"
#include "friendsIgnore.h"
#include "msgsend.h"
#include "NotifyCommon.h"
#include "objContainer.h"
#include "shardnet.h"
#include "users.h"

#include "AutoGen/chatCommonStructs_h_ast.h"
#include "Autogen/ChatRelay_autogen_RemoteFuncs.h"
#include "AutoGen/chatRelayManager_h_ast.h"
#include "AutoGen/chatdb_h_ast.h"
#include "Autogen/GameServerLib_autogen_RemoteFuncs.h"
//#include "profanity.h"
//#include "reserved_names.h"

extern CmdList gRemoteCmdList;
extern bool gbGlobalChatResponse;
extern bool gbDebugMode;

void updateCrossShardStats(ChatUser * from, ChatUser * to)
{
}

void ChatServerLeaveChannel(ContainerID id, ACMD_SENTENCE channel_name);
AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATRELAY);
void ChatServerPurgeChannels(ContainerID uAccountID)
{
	ChatUser *user = userFindByContainerId(uAccountID);
	if (!user)
		return;
	EARRAY_FOREACH_BEGIN(user->watching, i);
	{
		ChatServerLeaveChannel(uAccountID, (char*) user->watching[i]->name);
	}
	EARRAY_FOREACH_END;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATRELAY);
void ChatServerForwardPlayerUpdates_Debug(U32 uAccountID, PlayerInfoStruct *pInfo, ChatContainerIDList *pList)
{
	ChatPlayerStruct *pChatPlayer;
	if (!gbDebugMode)
		return;

	pChatPlayer = StructCreate(parse_ChatPlayerStruct);
	pChatPlayer->online_status = 1; // online
	estrCopy2(&pChatPlayer->comment, "PlayerInfoUpdateComment");
	StructCopyAll(parse_PlayerInfoStruct, pInfo, &pChatPlayer->pPlayerInfo);
	pChatPlayer->accountID = uAccountID;

	EARRAY_INT_CONST_FOREACH_BEGIN(pList->piContainerIDList, i, n);
	{
		ChatUser *user = userFindByContainerId(pList->piContainerIDList[i]);
		if (userOnline(user) && user->uChatRelayID)
		{
			RemoteCommand_crReceiveFriendCB(GLOBALTYPE_CHATRELAY, user->uChatRelayID, user->id,  pChatPlayer, FRIEND_UPDATED, NULL, false);
		}
	}
	EARRAY_FOREACH_END;
	StructDestroy(parse_ChatPlayerStruct, pChatPlayer);
}

// Returns the relation the target has to the user
AUTO_COMMAND_REMOTE;
int ChatServerGetUserRelation(CmdContext *context, ContainerID userAccountID, ContainerID targetAccountID)
{
	ChatUser *user = userFindByContainerId(userAccountID);
	int iRelation = USERRELATION_NONE;
	if (!user)
		return USERRELATION_USER_DNE;
	if (userIsIgnoringByID(user, targetAccountID, false))
		iRelation |= USERRELATION_IGNORED;
	if (userIsFriend(user, targetAccountID))
		iRelation |= USERRELATION_FRIENDS;
	return iRelation;
}

void ChatServerGetUserChannels(U32 uAccountID, U32 uCharacterID, U32 uChannelRequests);

AUTO_COMMAND_REMOTE;
bool ChatServerChangeDisplayName (CmdContext *context, ContainerID accountID, char *handleName)
{
	// TODO convert this to match int error codes
	if (gbGlobalChatResponse)
	{
		return userChangeChatHandle(accountID, handleName);
	}
	else
	{
		sendCommandToGlobalChatServer(context->commandString);
		return true;
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER) ACMD_IFDEF(CHATRELAY);
void ChatServerPlayerUpdateEx(ContainerID accountID, PlayerInfoStruct *pPlayerInfo, ChatUserUpdateEnum eForwardToGlobalFriends)
{
	ChatUser *user = userFindByContainerId(accountID);
	 // Local chat servers apply this immediately
	if (!gbGlobalChatResponse)
	{
		char *escapedData = NULL;

		if (userOnline(user) && pPlayerInfo)
			userLocalPlayerUpdate(user, pPlayerInfo, eForwardToGlobalFriends);

		ParserWriteTextEscaped(&escapedData, parse_PlayerInfoStruct, pPlayerInfo, 0, 0, 0);
		sendCmdAndParamsToGlobalChat("ChatServerPlayerUpdateEx", "%d %s %d", accountID, escapedData, eForwardToGlobalFriends);
		estrDestroy(&escapedData);
	}
}

static __forceinline void sendLoginData(const char *cmd, ChatLoginData *pData)
{
	char *escapedData = NULL;
	ParserWriteTextEscaped(&escapedData, parse_ChatLoginData, pData, 0, 0, 0);
	sendCmdAndParamsToGlobalChat(cmd, "%s", escapedData);
	estrDestroy(&escapedData);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER) ACMD_IFDEF(GAMESERVER);
void ChatServerAddOrUpdateUser(ChatLoginData *pLoginData)
{
	// This data must come directly from the servers
	if (devassert(!gbGlobalChatResponse))
		sendLoginData("ChatServerAddOrUpdateUser", pLoginData);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATRELAY);
void ChatServerLoginOnly(ChatLoginData *pLoginData)
{
	// Used with ChatRelay
	if (gbGlobalChatResponse)
	{
		ChatUser *user = userFindByContainerId(pLoginData->uAccountID);
		if (user)
		{
			userLocalLogin(user);
			if (userOnline(user) && user->uChatRelayID)
			{
				// Send list of channels user is watching on login, and confirm that the player has logged in
				RemoteCommand_crLoginSucceeded(GLOBALTYPE_CHATRELAY, user->uChatRelayID, user->id);
				ChatServerGetUserChannels(user->id, 0, USER_CHANNEL_SUBSCRIBED | USER_CHANNEL_RESERVED | USER_CHANNEL_INVITED);
			}
		}
	}
	else
	{
		sendLoginData("ChatServerLoginOnly", pLoginData);
	}
}

// Deprecated for Chat Relay usage
AUTO_COMMAND_REMOTE;
void ChatServerLogin(ContainerID accountID, ContainerID characterID, const char *accountName, const char *displayName, U32 access_level)
{
	if (gbGlobalChatResponse)
	{
		ChatUser *user = userFindByContainerId(accountID);

		if (user)
		{
			userLocalLogin(user);
			// Send list of channels user is watching on login, and confirm that the player has logged in
			RemoteCommand_cmdServerChat_LoginSucceeded(GLOBALTYPE_ENTITYPLAYER, characterID, characterID);
			ChatServerGetUserChannels(user->id, characterID, USER_CHANNEL_SUBSCRIBED | USER_CHANNEL_RESERVED | USER_CHANNEL_INVITED);
		}
		else
		{
			// TODO does nothing so far?
		}
	}
	else
	{
		sendCmdAndParamsToGlobalChat("ChatServerLogin", "%d %d \"%s\" \"%s\" %d", accountID, characterID, accountName, displayName, access_level);
	}
}

// Group Login on GCS reconnection
// Deprecated for Chat Relay usage
AUTO_COMMAND_REMOTE;
void ChatServerGroupLogin(const char *zoneChannelName, PlayerInfoList *accounts)
{
	int i,j;

	if (!accounts || !accounts->piAccountIDs || !accounts->ppPlayerInfos)
		return;
	if (!gbGlobalChatResponse)
	{
		char *pInfoString = NULL;
		char *escapedChannel = NULL;
		estrStackCreate(&pInfoString);
		estrStackCreate(&escapedChannel);
		estrAppendEscaped(&escapedChannel, zoneChannelName);
		ParserWriteTextEscaped(&pInfoString, parse_PlayerInfoList, accounts, 0, 0, 0);
		sendCmdAndParamsToGlobalChat("ChatServerGroupLogin", "\"%s\" %s", escapedChannel, pInfoString);
		estrDestroy(&pInfoString);
		estrDestroy(&escapedChannel);
		return;
	}

	for (j=eaiSize(&accounts->piAccountIDs)-1; j>=0; j--)
	{
		ChatUser *user = userFindByContainerId(accounts->piAccountIDs[j]);
		if (user)
		{
			for(i=eaSize(&user->watching)-1;i>=0;i--)
			{
				ChatChannel *chan = channelFindByID(user->watching[i]->channelID);
				if (chan) 
				{
					channelOnline(chan,user);
					channelSendOnline(chan, user);
				}
			}
			channelJoin(user, zoneChannelName, CHANNEL_SPECIAL_ZONE);
			if (accounts->ppPlayerInfos[j]->iPlayerTeam)
			{
				char teamChannel[128];
				team_MakeTeamChannelNameFromID(SAFESTR(teamChannel), accounts->ppPlayerInfos[j]->iPlayerTeam);
				channelCreateOrJoin(user, teamChannel, CHANNEL_SPECIAL_TEAM, true);
				// TODO figure out how to set operator
			}
			if (accounts->ppPlayerInfos[j]->bIsAutoAFK)
			{
				user->online_status |= USERSTATUS_AUTOAFK;
			}
			RemoteCommand_cmdServerChat_LoginSucceeded(GLOBALTYPE_ENTITYPLAYER, accounts->ppPlayerInfos[j]->onlineCharacterID, 
				accounts->ppPlayerInfos[j]->onlineCharacterID);
			// Send list of channels user is watching on login
			ChatServerGetUserChannels(accounts->piAccountIDs[j], accounts->ppPlayerInfos[j]->onlineCharacterID, 
				USER_CHANNEL_SUBSCRIBED | USER_CHANNEL_RESERVED | USER_CHANNEL_INVITED);
		}
	}
	ChatLocal_AddToLoginQueue(&accounts->piAccountIDs);
}

AUTO_COMMAND_REMOTE;
void ChatServerLogoutEx(ContainerID accountID, ContainerID characterID, U32 uChatRelay)
{
	if (gbGlobalChatResponse)
	{ // does nothing right now
	}
	else
	{
		ChatUser *user = userFindByContainerId(accountID);
		if (user)
		{
#ifndef USE_CHATRELAY
			// If the character in question is NOT the same character as the Chat Server thinks is logged on, do not logout
			// This will never get hit by the ChatRelay
			if (user->pPlayerInfo && user->pPlayerInfo->onlineCharacterID != 0 && user->pPlayerInfo->onlineCharacterID != characterID)
				return;
#endif
			userLocalLogout(user);
		}
		sendCmdAndParamsToGlobalChat("ChatServerLogout", "%d", accountID);
	}
}

// ------------------------------------------------------
// Channel Information

AUTO_COMMAND_REMOTE;
void ChatServerChannelList(ContainerID accountID)
{
	ChatUser *user = userFindByContainerId(accountID);
	if (user)
		channelList(user, NULL);
}

AUTO_COMMAND_REMOTE;
void ChatServerChannelListMembers(ContainerID accountID, char *channel_name)
{
	ChatChannel *channel = channelFindByName(channel_name);
	if (channel && channel->reserved)
	{
		ChatUser *user = userFindByContainerId(accountID);
		if (user)
			channelListMembers(user, channel_name);
	}
	else
	{
		char *escapedChannel = NULL;
		estrStackCreate(&escapedChannel);
		estrAppendEscaped(&escapedChannel, channel_name);
		sendCmdAndParamsToGlobalChat("ChatServerChannelListMembers", "%d \"%s\"", accountID, escapedChannel);
		estrDestroy(&escapedChannel);
	}
}

// ------------------------------------------------------
// Channel Joining / Leaving

AUTO_COMMAND_REMOTE;
int ChatServerJoinChannelFromGlobal(CmdContext *context, ContainerID id, char *channel_name)
{
	// does nothing anymore
	return CHATRETURN_NONE;
}

AUTO_COMMAND_REMOTE;
int ChatServerLeaveChannelFromGlobal(CmdContext *context, ContainerID id, char *channel_name)
{
	// does nothing anymore
	return CHATRETURN_NONE;
}

extern bool gbChatVerbose;

AUTO_COMMAND_REMOTE;
void ChatServerJoinChannel(ContainerID id, char *channel_name, int flags, bool bCreate)
{
	// special global channels are passed up to GCS
	if (IsChannelFlagShardOnly(flags))
	{
		ChatUser *user = userFindByContainerId(id);
		if (!user)
		{
			char *commandString = NULL;
			char *escapedChannel = NULL;
			estrStackCreate(&escapedChannel);
			estrStackCreate(&commandString);
			estrAppendEscaped(&escapedChannel, channel_name);
			estrPrintf(&commandString, "ChatServerJoinChannel %d \"%s\" %d %d", id, escapedChannel, flags, bCreate);
			localChatAddCommmandWaitQueue(id, 0, 0, 0, commandString);
			estrDestroy(&escapedChannel);
			estrDestroy(&commandString);
			if (gbChatVerbose)
				printf("\nLocal: Queuing user to join special channel type '%d'\n", flags);
			return;
		}
		if (gbChatVerbose) 
			printf("\nLocal: User '%s' Joining special channel type '%d'\n", user->handle, flags);

		if (bCreate)
		{
			channelCreateOrJoin(user, channel_name, flags, UserIsAdmin(user));
			return;
		} 
		else 
		{
			channelJoin(user, channel_name, flags);
			return;
		}
	}
	else
	{
		char *escapedChannel = NULL;
		estrStackCreate(&escapedChannel);
		estrAppendEscaped(&escapedChannel, channel_name);
		sendCmdAndParamsToGlobalChat("ChatServerJoinChannel", "%d \"%s\" %d %d", id, escapedChannel, flags, bCreate);
		estrDestroy(&escapedChannel);
	}
}

AUTO_COMMAND_REMOTE;
void ChatServerCreateChannel(ContainerID id, char *channel_name, int flags, bool adminAccess)
{
	// special global channels are passed up to GCS
	if (IsChannelFlagShardOnly(flags))
	{
		ChatUser *user = userFindByContainerId(id);
		if (!user)
			return;
		channelCreate(user, channel_name, flags, adminAccess);
	}
	else
	{
		char *escapedChannel = NULL;
		estrStackCreate(&escapedChannel);
		estrAppendEscaped(&escapedChannel, channel_name);
		sendCmdAndParamsToGlobalChat("ChatServerCreateChannel", "%d \"%s\" %d %d", id, escapedChannel, flags, adminAccess);
		estrDestroy(&escapedChannel);
	}
}

AUTO_COMMAND_REMOTE;
void ChatServerDestroyChannel(ContainerID id, char *channel_name)
{
	ChatChannel *channel = channelFindByName(channel_name);
	if (channel && IsChannelFlagShardOnly(channel->reserved))
	{
		ChatUser *user = userFindByContainerId(id);
		if (!user)
			return;
		channelKill(user, channel_name);
	}
	else
	{
		char *escapedChannel = NULL;
		estrStackCreate(&escapedChannel);
		estrAppendEscaped(&escapedChannel, channel_name);
		sendCmdAndParamsToGlobalChat("ChatServerDestroyChannel", "%d \"%s\"", id, escapedChannel);
		estrDestroy(&escapedChannel);
	}
}

// This needs to be transacted since the Guild server uses a callback with this
AUTO_COMMAND_REMOTE;
void ChatServerLeaveChannel(ContainerID id, ACMD_SENTENCE channel_name)
{
	ChatChannel *channel = channelFindByName(channel_name);
	if (channel && IsChannelFlagShardOnly(channel->reserved))
	{
		ChatUser *user = userFindByContainerId(id);
		if (!user)
			return;
		channelLeave(user, channel_name, 0);
	}
	else
	{
		char *escapedChannel = NULL;
		estrStackCreate(&escapedChannel);
		estrAppendEscaped(&escapedChannel, channel_name);
		sendCmdAndParamsToGlobalChat("ChatServerLeaveChannel", "%d \"%s\"", id, escapedChannel);
		estrDestroy(&escapedChannel);
	}
}
AUTO_COMMAND_REMOTE; // Version for ONLY leaving special shard-specific channels (eg. Guild, Team)
void ChatServerLeaveChannel_Special(ContainerID id, ContainerID uCharacterID, ACMD_SENTENCE channel_name)
{
	ChatChannel *channel = channelFindByName(channel_name);
	if (channel && IsChannelFlagShardOnly(channel->reserved))
	{
		ChatUser *user = userFindByContainerId(id);
		U32 uFlags = channel->reserved;
		if (!user)
			return;
		channelLeave(user, channel_name, 0);
		RemoteCommand_ServerChat_ShardChannelLeave(GLOBALTYPE_ENTITYPLAYER, uCharacterID, uCharacterID, uFlags); 
	}
	else
	{
		U32 uFlags = 0;
		if (strStartsWith(channel_name, TEAM_CHANNEL_PREFIX))
			uFlags = CHANNEL_SPECIAL_TEAM;
		else if (strStartsWith(channel_name, GUILD_CHANNEL_PREFIX))
			uFlags = CHANNEL_SPECIAL_GUILD;
		else if (strStartsWith(channel_name, OFFICER_CHANNEL_PREFIX))
			uFlags = CHANNEL_SPECIAL_OFFICER;
		RemoteCommand_ServerChat_ShardChannelLeave(GLOBALTYPE_ENTITYPLAYER, uCharacterID, uCharacterID, uFlags); 
	}
}

AUTO_COMMAND_REMOTE;
void ChatServerJoinOrCreateChannel(ContainerID id, char *channel_name, int flags, bool adminAccess)
{
	if (IsChannelFlagShardOnly(flags) || gbGlobalChatResponse)
	{
		ChatUser *user = userFindByContainerId(id);
		NOCONST(ChatChannel) *channel = CONTAINER_NOCONST(ChatChannel, channelFindByName(channel_name));
		if (!user)
			return;
		if (!channel)
			channelCreate(user, channel_name, flags, adminAccess);
		else
			channelJoin(user, channel_name, flags);
	}
	else
	{
		char *escapedChannel = NULL;
		estrStackCreate(&escapedChannel);
		estrAppendEscaped(&escapedChannel, channel_name);
		sendCmdAndParamsToGlobalChat("ChatServerJoinOrCreateChannel", "%d \"%s\" %d %d", id, escapedChannel, 
			flags, adminAccess);
		estrDestroy(&escapedChannel);
	}
}

AUTO_COMMAND_REMOTE; // Version for ONLY joining special shard-specific channels (eg. Guild, Team)
void ChatServerJoinOrCreateChannel_Special(ContainerID id, ContainerID uCharacterID, char *channel_name, int flags)
{
	ChatUser *user = userFindByContainerId(id);
	channelJoinOrCreate_ShardOnly(user, uCharacterID, channel_name, flags);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void ChatServerLeaveZoneChannel(char * zone_channel_name, ChatUserBatch *userList)
{
	int i;
	ChatChannel *channel;
	if (!strStartsWith(zone_channel_name, ZONE_CHANNEL_NAME))
		return;
	channel = channelFindByName(zone_channel_name);
	if (!channel)
		return;
	for (i=eaiSize(&userList->eaiAccountIDs)-1; i>=0; i--)
	{
		ChatUser *user = userFindByContainerId(userList->eaiAccountIDs[i]);
		if (user)
			channelLeave(user, zone_channel_name, 0);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void ChatServerCreateZoneChannel(char * zone_channel_name)
{
	if (!strStartsWith(zone_channel_name, ZONE_CHANNEL_NAME))
		return;
	if (!stashFindPointer(chat_db.channel_names, zone_channel_name, NULL))
	{
		NOCONST(ChatChannel) *channel = StructAllocNoConst(parse_ChatChannel);
		channelInitialize(channel, zone_channel_name, "", CHANNEL_SPECIAL_ZONE);
		stashAddPointer(chat_db.channel_names, channel->name, channel, false);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void ChatServerDestroyZoneChannel(char * zone_channel_name)
{
	ChatChannel *channel = NULL;
	if (!strStartsWith(zone_channel_name, ZONE_CHANNEL_NAME))
		return;
	if (stashFindPointer(chat_db.channel_names, zone_channel_name, &channel))
	{
		channelForceKill(channel);
	}
}

// ------------------------------------------------------

AUTO_COMMAND_REMOTE;
int ChatServerChannelForwardEncoded(CmdContext *context, ChatMessage *pMsg)
{
	//ChatStripNonGlobalData(pMsg);
	channelForward(pMsg);
	return CHATRETURN_NONE;
}

AUTO_COMMAND_REMOTE;
int ChatServerPrivateEncodedMessageFromGlobal(CmdContext *context, ChatMessage *pMsg)
{
	ChatUser *to = pMsg && pMsg->pTo ? userFindByContainerId(pMsg->pTo->accountID) : NULL;
	ChatUser *from = pMsg && pMsg->pFrom ? userFindByContainerId(pMsg->pFrom->accountID) : NULL;

	if (from)
	{
		ChatLogEntryType eTempType = pMsg->eType;

		// Send "receipt" to sender
		pMsg->eType = kChatLogEntryType_Private_Sent;
		ChatServerMessageSend(from->id, pMsg);
		pMsg->eType = eTempType;
	}
	if (to)
	{
		//ChatStripNonGlobalData(pMsg);
		ChatServerMessageSend(to->id, pMsg);
		forwardChatMessageToSpys(to, pMsg);
	}
	return CHATRETURN_NONE;
}

AUTO_COMMAND_REMOTE;
void ChatServerPrivateMesssage_Shard(ChatMessage *pMsg)
{
	char *encodedMsg = NULL;
	ChatUser *user;
	if (!pMsg || !pMsg->pFrom || !pMsg->pTo) {
		return;
	}
	user = userFindByContainerId(pMsg->pFrom->accountID);
	if (!user)
		return;
	
	if (!UserIsAdmin(user) && pMsg->bBlacklistViolation)
	{
		blacklist_Violation(user, pMsg->pchText);
		return;
	}

	// If the player is on a trial account make sure the target is on their friend list, team, or guild
	if (userIsSocialRestricted(user))
	{
		bool bCanTalkToUser = false;
		
		// Check GM
		bCanTalkToUser = userIsGM(pMsg->pTo->accountID, pMsg->pTo->pchHandle);

		// Check Friends
		if (!bCanTalkToUser && ((pMsg->pTo->accountID && eaiFind(&user->friends, pMsg->pTo->accountID) != -1) ||
			(pMsg->pTo->pchHandle && eaIndexedFindUsingString(&user->eaCachedFriendInfo, pMsg->pTo->pchHandle) != -1)))
			bCanTalkToUser = true;
		
		// Check Team and Guild
		if (!bCanTalkToUser)
		{
			U32 uTargetID;
			if (pMsg->pTo->accountID)
				uTargetID = pMsg->pTo->accountID;
			else
			{
				ChatUser *target = userFindByHandle(pMsg->pTo->pchHandle);
				uTargetID = target ? target->id : 0;
			}
			if (uTargetID)
			{
				Watching *watch = channelFindWatchingReservedByType(user, CHANNEL_SPECIAL_TEAM);
				ChatChannel *channel;
				if (watch)
				{
					channel = channelFindByName(watch->name);
					if (channel && eaiFind(&channel->online, uTargetID) != -1)
						bCanTalkToUser = true;
				}
				if (!bCanTalkToUser)
				{
					watch = channelFindWatchingReservedByType(user, CHANNEL_SPECIAL_GUILD);
					if (watch)
					{
						channel = channelFindByName(watch->name);
						if (channel && eaiFind(&channel->online, uTargetID) != -1)
							bCanTalkToUser = true;
					}
				}
			}
		}
		if (!bCanTalkToUser)
		{
			char *pErrorMsg = NULL;	
			estrStackCreate(&pErrorMsg);
			langFormatMessageKey(userGetLastLanguage(user), &pErrorMsg, "Chat_NoTrialTells", STRFMT_END);
			RemoteCommand_crReceiveNotify(GLOBALTYPE_CHATRELAY, user->uChatRelayID, user->id, kNotifyType_Failed, pErrorMsg);
			estrDestroy(&pErrorMsg);
			return;
		}
	}
	
	ChatFillUserInfo(&pMsg->pFrom, user);
	// Turn it into a string
	ChatCommon_EncodeMessage(&encodedMsg, pMsg);
	sendCmdAndParamsToGlobalChat("ChatServerPrivateMesssage", "%s", encodedMsg);
	estrDestroy(&encodedMsg);
	return;
}

AUTO_COMMAND_REMOTE;
void ChatServerBatchSendLocalChat_Shard(ChatContainerIDList *pTargetAccountIDList, ChatMessage *pMsg)
{
	ChatUser *pFromUser;

	if (!pMsg || !pMsg->pFrom)
		return;
	pFromUser = userFindByContainerId(pMsg->pFrom->accountID);
    
	if (!ChatServerIsValidMessage(pFromUser, pMsg->pchText) || !pTargetAccountIDList)
		return;
	if (!userCharacterOnline(pFromUser))
		return;
	// Rate limiting is done on the GCS
	
	if (!UserIsAdmin(pFromUser) && pMsg->bBlacklistViolation)
	{
		blacklist_Violation(pFromUser, pMsg->pchText);
		return;
	}

	if (userIsSilenced(pFromUser))
	{
		U32 hour, min, sec;
		userGetSilenceTimeLeft(pFromUser, &hour, &min, &sec);
		sendTranslatedMessageToUser(pFromUser, kChatLogEntryType_System, NULL, "ChatServer_Silenced", 
			STRFMT_INT("H", hour), STRFMT_INT("M", min), STRFMT_INT("S", sec), STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_SEND, pMsg->pchChannel, pFromUser, CHATRETURN_USER_PERMISSIONS);
		return;
	}
	if (!userCanTalk(pFromUser))
	{
		sendTranslatedMessageToUser(pFromUser, 0, NULL, "ChatServer_PermissionDenied", STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_SEND, pMsg->pchChannel, pFromUser, CHATRETURN_USER_PERMISSIONS);
		return;
	}
	
	// Fill in missing FROM user info as best as possible. 
	// DO NOT FILL in pMsg->pTo (even if we know at this point) because it's supposed to be NULL
	ChatFillUserInfo(&pMsg->pFrom, pFromUser);

	if (!userThrottleLocalSpam(pFromUser))
	{
		EARRAY_OF(ChatRelayUserList) eaRelays = NULL;
		forwardChatMessageToSpys(pFromUser, pMsg);

		eaStackCreate(&eaRelays, ChatConfig_GetNumberOfChatRelays());
		eaIndexedEnable(&eaRelays, parse_ChatRelayUserList);

		EARRAY_INT_CONST_FOREACH_BEGIN(pTargetAccountIDList->piContainerIDList, i, n);
		{
			ChatUser *pTargetUser = userFindByContainerId(pTargetAccountIDList->piContainerIDList[i]);
			if (userCharacterOnline(pTargetUser) && pTargetUser->uChatRelayID &&
				!userIsIgnoring(pTargetUser, pFromUser) && isWhitelisted(pFromUser, pTargetUser, LOCAL_CHANNEL_NAME, 0))
			{
				ChatServerAddToRelayUserList(&eaRelays, pTargetUser);
			}
		}
		EARRAY_FOREACH_END;

		ChatServerMessageSendToRelays(eaRelays, pMsg);
		eaDestroyStruct(&eaRelays, parse_ChatRelayUserList);
	}
	else
	{
		// These messages are still treated as sent for spamming purposes (and forwarded to GCS),
		// even though they don't actually get sent
		sendTranslatedMessageToUser(pFromUser, 0, NULL, "ChatServer_LocalChatThrottled", STRFMT_INT("THROTTLE_PERIOD", LOCAL_CHAT_THROTTLE), STRFMT_END);
	}

	{ // Send to Global for logging
		char *encodedMsg = NULL;
		char *encodedUsers = NULL;
		ChatCommon_EncodeMessage(&encodedMsg, pMsg);
		ParserWriteTextEscaped(&encodedUsers, parse_ChatContainerIDList, pTargetAccountIDList, 0, 0, 0);
		sendCmdAndParamsToGlobalChat("ChatServerBatchSendLocalChatGlobal", "%s %s", encodedUsers, encodedMsg);
		estrDestroy(&encodedMsg);
		estrDestroy(&encodedUsers);
	}
	chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_SEND, LOCAL_CHANNEL_NAME, pFromUser, pMsg->pchText);
}

static bool channelMessage_CleanupName(SA_PARAM_NN_VALID ChatUser *user, SA_PARAM_NN_VALID ChatMessage *pMsg)
{
	// Only team is specially handled - other specials are cleaned up on the Game Server
	switch (pMsg->eType)
	{
	xcase kChatLogEntryType_Team:
		if (!pMsg->pchChannel || !*pMsg->pchChannel || stricmp(pMsg->pchChannel, CHAT_TEAM_SHORTCUT) == 0)
		{
			if (user->pPlayerInfo && user->pPlayerInfo->iPlayerTeam)
			{
				char buffer[128] = "";
				team_MakeTeamChannelNameFromID(buffer, ARRAY_SIZE_CHECKED(buffer), user->pPlayerInfo->iPlayerTeam);
				estrCopy2(&pMsg->pchChannel, buffer);
			}
			else // user is not on a team, but trying to send to a team
			{
#ifdef USE_CHATRELAY
				if (user->uChatRelayID)
				{
					char *pErrorMsg = NULL;
					translateMessageForUser(&pErrorMsg, user, "Chat_TeamError", STRFMT_END);
					RemoteCommand_crReceiveNotify(GLOBALTYPE_CHATRELAY, user->uChatRelayID, user->id, kNotifyType_Failed, pErrorMsg);
					estrDestroy(&pErrorMsg);
				}
#endif
				return false;
			}
		}
	}
	return true;
}

AUTO_COMMAND_REMOTE;
void ChatServerMessageReceive_Shard(ChatMessage *pMsg)
{
	ChatUser *pFromUser= NULL;

	if (pMsg && pMsg->pFrom)
		pFromUser = userFindByContainerId(pMsg->pFrom->accountID);
	if (!pFromUser)
		return;
	// Rate limiting done on the GCS

	if (userIsSilenced(pFromUser))
	{
		U32 hour, min, sec;
		userGetSilenceTimeLeft(pFromUser, &hour, &min, &sec);
		sendTranslatedMessageToUser(pFromUser, kChatLogEntryType_System, NULL, "ChatServer_Silenced", 
			STRFMT_INT("H", hour), STRFMT_INT("M", min), STRFMT_INT("S", sec), STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_SEND, pMsg->pchChannel, pFromUser, CHATRETURN_USER_PERMISSIONS);
		return;
	}

	if (!UserIsAdmin(pFromUser) && pMsg->bBlacklistViolation)
	{
		blacklist_Violation(pFromUser, pMsg->pchText);
		return;
	}

	// Fill in missing FROM user info as best as possible. 
	// DO NOT FILL in pMsg->pTo (even if we know at this point) because it's supposed to be NULL
	ChatFillUserInfo(&pMsg->pFrom, pFromUser);

	// Dispatch message to appropriate handler
	// TODO: do intelligent splitting out between local, channel, and PM's so have we 
	//       single point of entry with ChatServerMessageReceive
	{
		ChatChannel *channel;
		char *encodedMsg = NULL;

		if (!channelMessage_CleanupName(pFromUser, pMsg))
			return; // silent failure
		channel = channelFindByName(pMsg->pchChannel);

		if (channel)
		{
			// populate the display name
			estrCopy2(&pMsg->pchChannelDisplay, convertExactChannelName(channel->name, channel->reserved));
			forwardChatMessageToSpys(pFromUser, pMsg);//pFromUser, channel_name, pFromUser->id, fromPlayerName, pFromUser->handle, 0, 0, 0, msg);
			channelSend(pFromUser, pMsg);
		}
		ChatCommon_EncodeMessage(&encodedMsg, pMsg);
		if (!channel || !IsChannelFlagShardOnly(channel->reserved))
		{
			sendCmdAndParamsToGlobalChat("ChatServerMessageReceive", "%s", encodedMsg);
		}
		else if (IsChannelFlagShardOnly(channel->reserved))
		{
			sendCmdAndParamsToGlobalChat("ChatServerReservedChannelLog", "%s", encodedMsg);
		}
		estrDestroy(&encodedMsg);
	}
}

// ------------------------------------------------------
// Channel Management (OP commands)

AUTO_COMMAND_REMOTE;
void ChatServerSetMotd(ContainerID id, char *channel_name, ACMD_SENTENCE motd)
{
	ChatChannel * channel = channelFindByName(channel_name);
	if (channel && channel->reserved)
		return;
	{
		char *msg = NULL;
		char *escapedChannel = NULL;
		estrStackCreate(&escapedChannel);
		estrStackCreate(&msg);
		estrAppendEscaped(&escapedChannel, channel_name);
		estrAppendEscaped(&msg, motd);
		sendCmdAndParamsToGlobalChat("ChatServerSetMotd", "%d \"%s\" \"%s\"", id, escapedChannel, msg);
		estrDestroy(&msg);
		estrDestroy(&escapedChannel);
	}
}

AUTO_COMMAND_REMOTE;
void ChatServerSetChannelDescription(ContainerID id, char *channel_name, ACMD_SENTENCE description)
{
	ChatChannel * channel = channelFindByName(channel_name);
	if (channel && channel->reserved)
		return;
	{
		char *msg = NULL;
		char *escapedChannel = NULL;
		estrStackCreate(&escapedChannel);
		estrStackCreate(&msg);
		estrAppendEscaped(&escapedChannel, channel_name);
		estrAppendEscaped(&msg, description);
		sendCmdAndParamsToGlobalChat("ChatServerSetChannelDescription", "%d \"%s\" \"%s\"", id, escapedChannel, msg);
		estrDestroy(&msg);
		estrDestroy(&escapedChannel);
	}
}

AUTO_COMMAND_REMOTE;
void ChatServerChannelUninviteByID(ContainerID userID, ContainerID targetID, ACMD_SENTENCE channel_name)
{
	char *escapedChannel = NULL;
	estrAppendEscaped(&escapedChannel, channel_name);
	sendCmdAndParamsToGlobalChat("ChatServerChannelUninviteByID", "%d %d \"%s\"", userID, targetID, channel_name);
	estrDestroy(&escapedChannel);
}

AUTO_COMMAND_REMOTE;
void ChatServerChannelUninviteByHandle(ContainerID userID, char *targetHandle, ACMD_SENTENCE channel_name)
{
	char *escapedChannel = NULL;
	estrAppendEscaped(&escapedChannel, channel_name);
	sendCmdAndParamsToGlobalChat("ChatServerChannelUninviteByHandle", "%d \"%s\" \"%s\"", userID, targetHandle, channel_name);
	estrDestroy(&escapedChannel);
}

AUTO_COMMAND_REMOTE;
void ChatServerDeclineChannelInvite(ContainerID userID, ACMD_SENTENCE channel_name)
{
	char *escapedChannel = NULL;
	estrAppendEscaped(&escapedChannel, channel_name);
	sendCmdAndParamsToGlobalChat("ChatServerDeclineChannelInvite", "%d \"%s\"", userID, channel_name);
	estrDestroy(&escapedChannel);
}

AUTO_COMMAND_REMOTE;
void ChatServerInviteByID(ContainerID idInviter, char *playerNameInviter, char *channel_name, ContainerID idInvitee)
{
	ChatChannel * channel = channelFindByName(channel_name);
	if (channel && channel->reserved)
		return;
	if (!gbGlobalChatResponse)
	{
		char *escapedChannel = NULL, *escapedName = NULL;
		estrStackCreate(&escapedChannel);
		estrStackCreate(&escapedName);
		estrAppendEscaped(&escapedChannel, channel_name);
		estrAppendEscaped(&escapedName, playerNameInviter);
		sendCmdAndParamsToGlobalChat("ChatServerInviteByID", "%d \"%s\" \"%s\" %d", idInviter, escapedName, 
			escapedChannel, idInvitee);
		estrDestroy(&escapedChannel);
		estrDestroy(&escapedName);
	}
}

AUTO_COMMAND_REMOTE;
void ChatServerInviteByHandle(ContainerID idInviter, char *playerNameInviter, char *channel_name, char *handleInvitee)
{
	ChatChannel * channel = channelFindByName(channel_name);
	if (channel && channel->reserved)
		return;
	{
		char *escapedChannel = NULL, *escapedName = NULL, *escapedHandle = NULL;
		estrStackCreate(&escapedChannel);
		estrStackCreate(&escapedName);
		estrStackCreate(&escapedHandle);
		estrAppendEscaped(&escapedChannel, channel_name);
		estrAppendEscaped(&escapedName, playerNameInviter);
		estrAppendEscaped(&escapedHandle, handleInvitee);
		sendCmdAndParamsToGlobalChat("ChatServerInviteByHandle", "%d \"%s\" \"%s\" \"%s\"", idInviter, escapedName, 
			escapedChannel, escapedHandle);
		estrDestroy(&escapedChannel);
		estrDestroy(&escapedName);
		estrDestroy(&escapedHandle);
	}
}

AUTO_COMMAND_REMOTE;
void ChatServerSetChannelAccess(ContainerID userID, char *channel_name, char *pAccessString)
{
	ChatChannel * channel = channelFindByName(channel_name);
	if (channel && channel->reserved)
		return;
	{
		char *escapedChannel = NULL;
		estrStackCreate(&escapedChannel);
		estrAppendEscaped(&escapedChannel, channel_name);
		sendCmdAndParamsToGlobalChat("ChatServerSetChannelAccess", "%d \"%s\" \"%s\"", userID, escapedChannel, 
			pAccessString);
		estrDestroy(&escapedChannel);
	}
}

AUTO_COMMAND_REMOTE;
void ChatServerSetUserAccessByHandleNew(ContainerID userID, char *channel_name, char *targetHandle, 
									   U32 uAddFlags, U32 uRemoveFlags)
{
	char *escapedChannel = NULL, *escapedHandle = NULL;
	estrStackCreate(&escapedChannel);
	estrStackCreate(&escapedHandle);
	estrAppendEscaped(&escapedChannel, channel_name);
	estrAppendEscaped(&escapedHandle, targetHandle);
	sendCmdAndParamsToGlobalChat("ChatServerSetUserAccessByHandleNew", "%d \"%s\" \"%s\" %d %d", userID, escapedChannel, 
		escapedHandle, uAddFlags, uRemoveFlags);
	estrDestroy(&escapedChannel);
	estrDestroy(&escapedHandle);
}

AUTO_COMMAND_REMOTE;
void ChatServerSetUserAccessByHandle(ContainerID userID, char *channel_name, char *targetHandle, char *pAccessString)
{
	char *escapedChannel = NULL, *escapedHandle = NULL;
	estrStackCreate(&escapedChannel);
	estrStackCreate(&escapedHandle);
	estrAppendEscaped(&escapedChannel, channel_name);
	estrAppendEscaped(&escapedHandle, targetHandle);
	sendCmdAndParamsToGlobalChat("ChatServerSetUserAccessByHandle", "%d \"%s\" \"%s\" %s", userID, escapedChannel, 
		escapedHandle, pAccessString);
	estrDestroy(&escapedChannel);
	estrDestroy(&escapedHandle);
}

AUTO_COMMAND_REMOTE;
void ChatServerSetUserAccessByID(ContainerID userID, char *channel_name, ContainerID targetID, U32 uAddFlags, U32 uRemoveFlags)
{
	char *escapedChannel = NULL;
	estrStackCreate(&escapedChannel);
	estrAppendEscaped(&escapedChannel, channel_name);
	sendCmdAndParamsToGlobalChat("ChatServerSetUserAccessByID", "%d \"%s\" %d %d %d", userID, escapedChannel, 
		targetID, uAddFlags, uRemoveFlags);
	estrDestroy(&escapedChannel);
}

/*AUTO_COMMAND_REMOTE;
void ChatServerDonateOperator(CmdContext *context, ContainerID userID, char *channel_name, ContainerID targetID)
{
ChatUser *user = userFindByContainerId(userID);
ChatUser *target = userFindByContainerId(targetID);

if (user && target)
channelDonateOperatorAccess (user, channel_name, target);
}*/

// ------------------------------------------------------
// Friends and Ignores

AUTO_COMMAND_REMOTE;
int ChatServerForwardFriendResponseError(CmdContext *context, ContainerID userID, FriendResponseEnum eResponse, 
										 const char *errorKey, const char *errorHandle)
{
	ChatUser *user = userFindByContainerId(userID);
	if (user)
	{
		char *translated = NULL;
		if (errorHandle && *errorHandle)
			translateMessageForUser(&translated, user, errorKey, STRFMT_STRING("User", errorHandle), STRFMT_END);
		else
			translateMessageForUser(&translated, user, errorKey, STRFMT_END);
		ChatServer_SendFriendUpdate(user, NULL, eResponse, translated);
		estrDestroy(&translated);
	}
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE;
int ChatServerForwardFriendResponse(CmdContext *context, ContainerID userID, ChatPlayerStruct *friendStruct, FriendResponseEnum eResponse, const char *errorString)
{
	ChatUser *user = userFindByContainerId(userID);
	if (user)
	{
		char *unescaped = NULL;
		if (errorString)
			estrSuperUnescapeString(&unescaped, errorString);
		ChatServer_SendFriendUpdate(user, friendStruct, eResponse, unescaped);
		estrDestroy(&unescaped);
	}
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE;
int ChatServerForwardIgnoreResponseError(CmdContext *context, ContainerID userID, IgnoreResponseEnum eResponse, 
										 const char *errorKey, const char *errorHandle)
{
	ChatUser *user = userFindByContainerId(userID);
	if (user)
	{
		char *translated = NULL;
		if (errorHandle && *errorHandle)
			translateMessageForUser(&translated, user, errorKey, STRFMT_STRING("User", errorHandle), STRFMT_END);
		else
			translateMessageForUser(&translated, user, errorKey, STRFMT_END);
		ChatServer_SendIgnoreUpdate(user, NULL, eResponse, translated);
		estrDestroy(&translated);
	}
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE;
int ChatServerForwardIgnoreResponse(CmdContext *context, ContainerID userID, ChatPlayerStruct *ignoreStruct, IgnoreResponseEnum eResponse, const char *errorString)
{
	ChatUser *user = userFindByContainerId(userID);
	if (user)
	{
		char *unescaped = NULL;
		if (errorString)
			estrSuperUnescapeString(&unescaped, errorString);
		ChatServer_SendIgnoreUpdate(user, ignoreStruct, eResponse, unescaped);
		estrDestroy(&unescaped);
	}
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE;
void ChatServerAddFriendByHandle_Shard(ContainerID userID, ACMD_SENTENCE targetHandle)
{
	if (!ChatServerHandleIsValid(targetHandle)) 
	{
		ChatUser *user = userFindByContainerId(userID);
		if (user)
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_UserDNE", STRFMT_STRING("User", targetHandle), STRFMT_END);
		return;
	}
	sendCmdAndParamsToGlobalChat("ChatServerAddFriendByHandle", "%d \"%s\"", userID, targetHandle);
}

AUTO_COMMAND_REMOTE;
void ChatServerAcceptFriendByHandle_Shard(ContainerID userID, ACMD_SENTENCE targetHandle)
{
	if (!ChatServerHandleIsValid(targetHandle))
	{
		ChatUser *user = userFindByContainerId(userID);
		if (user)
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_UserDNE", STRFMT_STRING("User", targetHandle), STRFMT_END);
		return;
	}
	sendCmdAndParamsToGlobalChat("ChatServerAcceptFriendByHandle", "%d \"%s\"", userID, targetHandle);
}

AUTO_COMMAND_REMOTE;
void ChatServerRejectFriendByHandle_Shard(ContainerID userID, ACMD_SENTENCE targetHandle)
{
	if (!ChatServerHandleIsValid(targetHandle))
	{
		ChatUser *user = userFindByContainerId(userID);
		if (user)
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_UserDNE", STRFMT_STRING("User", targetHandle), STRFMT_END);
		return;
	}
	sendCmdAndParamsToGlobalChat("ChatServerRejectFriendByHandle", "%d \"%s\"", userID, targetHandle);
}

AUTO_COMMAND_REMOTE;
void ChatServerRemoveFriendByHandle_Shard(ContainerID userID, ACMD_SENTENCE targetHandle)
{
	if (!ChatServerHandleIsValid(targetHandle))
	{
		ChatUser *user = userFindByContainerId(userID);
		if (user)
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_UserDNE", STRFMT_STRING("User", targetHandle), STRFMT_END);
		return;
	}
	sendCmdAndParamsToGlobalChat("ChatServerRemoveFriendByHandle", "%d \"%s\"", userID, targetHandle);
}

AUTO_COMMAND_REMOTE;
void ChatServerAddIgnoreByHandle_Shard(ContainerID userID, ACMD_SENTENCE targetHandle, bool spammer)
{
	if (!ChatServerHandleIsValid(targetHandle))
	{
		ChatUser *user = userFindByContainerId(userID);
		if (user)
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_UserDNE", STRFMT_STRING("User", targetHandle), STRFMT_END);
		return;
	}
	sendCmdAndParamsToGlobalChat("ChatServerAddIgnoreByHandle", "%d \"%s\" %d", userID, targetHandle, spammer);
}

AUTO_COMMAND_REMOTE;
void ChatServerAddIgnoreByID_Shard(ContainerID userID, ContainerID targetID, bool spammer)
{
	sendCmdAndParamsToGlobalChat("ChatServerAddIgnoreByID", "%d %d %d", userID, targetID, spammer);
}

AUTO_COMMAND_REMOTE;
void ChatServerRemoveIgnoreByHandle_Shard(ContainerID userID, ACMD_SENTENCE targetHandle)
{
	if (!ChatServerHandleIsValid(targetHandle))
	{
		ChatUser *user = userFindByContainerId(userID);
		if (user)
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_UserDNE", STRFMT_STRING("User", targetHandle), STRFMT_END);
		return;
	}
	sendCmdAndParamsToGlobalChat("ChatServerRemoveIgnoreByHandle", "%d \"%s\"", userID, targetHandle);
}

AUTO_COMMAND_REMOTE;
void ChatServerRemoveIgnoreByID_Shard(ContainerID userID, ContainerID targetID)
{
	sendCmdAndParamsToGlobalChat("ChatServerRemoveIgnoreByID", "%d %d", userID, targetID);
}

AUTO_COMMAND_REMOTE;
void  ChatServerAddFriendByID_Shard(ContainerID userID, ContainerID targetID)
{
	sendCmdAndParamsToGlobalChat("ChatServerAddFriendByID", "%d %d", userID, targetID);
}

AUTO_COMMAND_REMOTE;
void  ChatServerAddFriendComment_Shard(ContainerID userID, const char *handle, ACMD_SENTENCE pcComment)
{
	char *pEscaped = NULL;
	estrAppendEscaped(&pEscaped, pcComment);
	sendCmdAndParamsToGlobalChat("ChatServerAddFriendCommentByHandle", "%d \"%s\" \"%s\"", userID, handle, pEscaped);
	estrDestroy(&pEscaped);
}

AUTO_COMMAND_REMOTE;
void ChatServerAcceptFriendByID_Shard(ContainerID userID, ContainerID targetID)
{
	sendCmdAndParamsToGlobalChat("ChatServerAcceptFriendByID", "%d %d", userID, targetID);
}

AUTO_COMMAND_REMOTE;
void ChatServerRejectFriendByID_Shard(ContainerID userID, ContainerID targetID)
{
	sendCmdAndParamsToGlobalChat("ChatServerRejectFriendByID", "%d %d", userID, targetID);
}

AUTO_COMMAND_REMOTE;
void ChatServerRemoveFriendByID_Shard(ContainerID userID, ContainerID targetID)
{
	sendCmdAndParamsToGlobalChat("ChatServerRemoveFriendByID", "%d %d", userID, targetID);
}

AUTO_COMMAND_REMOTE;
void ChatServerIgnoreByID_Shard(ContainerID userID, ContainerID targetID)
{
	sendCmdAndParamsToGlobalChat("ChatServerIgnoreByID", "%d %d", userID, targetID);
}

// ------------------------------------------------------

AUTO_COMMAND_REMOTE;
char * ChatServerGetChatHandle(ContainerID userID)
{
	ChatUser *user = userFindByContainerId(userID);
	if (user)
		return (char*) user->handle;
	return NULL;
}

extern ParseTable parse_ChatPlayerList[];
#define TYPE_parse_ChatPlayerList ChatPlayerList
extern ParseTable parse_ChatChannel[];
#define TYPE_parse_ChatChannel ChatChannel

AUTO_COMMAND_REMOTE;
int ChatServerForwardFriendsFromGlobal(CmdContext *context, ContainerID accountID, ChatPlayerList *friendList)
{
	ChatUser *user = userFindByContainerId(accountID);
	ChatServerForwardFriendsList(user, friendList);
	return CHATRETURN_NONE;
}

AUTO_COMMAND_REMOTE;
int ChatServerForwardIgnoresFromGlobal(CmdContext *context, ContainerID accountID, ChatPlayerList *ignoreList)
{
	ChatUser *user = userFindByContainerId(accountID);
	ChatServerForwardIgnoreList(user, ignoreList);
	return CHATRETURN_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(CHATRELAY);
void ChatServerGetFriendsList(ContainerID userID)
{
	sendCmdAndParamsToGlobalChat("ChatServerGetFriendsList", "%d", userID);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(CHATRELAY);
void ChatServerGetIgnoreList(ContainerID userID)
{
	sendCmdAndParamsToGlobalChat("ChatServerGetIgnoreList", "%d", userID);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
ChatPlayerList * ChatServerGetOnlinePlayers(PlayerFindFilterStruct *pFilters)
{
	ChatPlayerList *list = StructCreate(parse_ChatPlayerList);	
	pFilters->pCachedSearcher = userFindByContainerId(pFilters->searchAccountID);
	if (pFilters->pCachedSearcher && !pFilters->pCachedSearcher->online_status)
	{
		pFilters->pCachedSearcher = NULL;
	}

	getOnlineList(list, pFilters, 0);

	devassert(eaSize(&list->chatAccounts) <= MAX_FIND_PLAYER_LIST);

	return list;
}


AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
ChatTeamToJoinList * ChatServerGetTeamsToJoin(PlayerFindFilterStruct *pFilters)
{
	ChatTeamToJoinList *list = StructCreate(parse_ChatTeamToJoinList);
	pFilters->pCachedSearcher = userFindByContainerId(pFilters->searchAccountID);
	if (pFilters->pCachedSearcher && !pFilters->pCachedSearcher->online_status)
	{
		pFilters->pCachedSearcher = NULL;
	}

	getListOfTeams(list,pFilters);

	devassert(eaSize(&list->chatAccounts) <= MAX_FIND_PLAYER_LIST);

	return list;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATRELAY);
void ChatServerGetActiveMaps(U32 uAccountID)
{
	ChatUser *user = userFindByContainerId(uAccountID);
	if (user && user->uChatRelayID)
	{
		ChatMapList list = {0};
		getActiveMaps(&list);
		RemoteCommand_crActiveMapsUpdate(GLOBALTYPE_CHATRELAY, user->uChatRelayID, user->id, &list);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(CHATRELAY);
void ChatServerGetUserChannels(U32 uAccountID, U32 uCharacterID, U32 uChannelRequests)
{
	ChatUser *user = userFindByContainerId(uAccountID);
#ifdef USE_CHATRELAY
	if (userOnline(user) && user->uChatRelayID)
#else
	if (user)
#endif
	{
		int i, size;
		ChatChannelInfoList list = {0};
		bool bIsGM = UserIsAdmin(user);

		if (uChannelRequests & USER_CHANNEL_SUBSCRIBED)
		{
			size = eaSize(&user->watching);
			for (i=0; i<size; i++)
			{
				ChatChannel *chan = channelFindByName(user->watching[i]->name);
				ChatChannelInfo *chaninfo = copyChannelInfo(user, chan, user->watching[i], false, bIsGM);
				if (chaninfo)
				{
					eaPush(&list.ppChannels, chaninfo);
				}
			}
		}

		if (uChannelRequests & USER_CHANNEL_RESERVED)
		{
			size = eaSize(&user->reserved);
			for (i=0; i<size; i++)
			{
				ChatChannel *chan = channelFindByName(user->reserved[i]->name);
				ChatChannelInfo *chaninfo = copyChannelInfo(user, chan, user->reserved[i], false, bIsGM);
				if (chaninfo)
				{
					eaPush(&list.ppChannels, chaninfo);
				}
			}
		}
		
		if (uChannelRequests & USER_CHANNEL_INVITED)
		{
			size = eaiSize(&user->invites);
			for (i=0; i<size; i++)
			{
				ChatChannel *chan = channelFindByID(user->invites[i]);
				if (chan)
				{
					if (eaiFind(&chan->invites, user->id) >= 0) // double check
					{
						ChatChannelInfo *chaninfo = copyChannelInfo(user, chan, NULL, false, bIsGM);
						if (chaninfo)
						{
							eaPush(&list.ppChannels, chaninfo);
						}
					}
				}
			}
		}
#ifdef USE_CHATRELAY
		RemoteCommand_crReceiveChannelList(GLOBALTYPE_CHATRELAY, user->uChatRelayID, user->id, &list);
#else
		RemoteCommand_cmdServerChat_ReceiveUserChannelList(GLOBALTYPE_ENTITYPLAYER, uCharacterID, uCharacterID, &list);
#endif
		StructDeInit(parse_ChatChannelInfoList, &list);
	}
}

AUTO_COMMAND_REMOTE;
int ChatServerForwardChannelInfo (CmdContext *context, U32 uAccountID, ChatChannelInfo *chaninfo)
{
	// does nothing anymore
	return CHATRETURN_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(CHATRELAY);
void ChatServerGetChannelInfo(U32 uAccountID, U32 uCharacterID, const char *channel_name)
{
	ChatChannel *channel = channelFindByName(channel_name);

	if (channel)
	{
		char *escapedChannel = NULL;
		estrStackCreate(&escapedChannel);
		estrAppendEscaped(&escapedChannel, channel_name);
		sendCmdAndParamsToGlobalChat("ChatServerGetChannelInfo", "%d %d \"%s\"", uAccountID, uCharacterID, escapedChannel);
		estrDestroy(&escapedChannel);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(CHATRELAY);
void ChatServerGetJoinChannelInfo(U32 uAccountID, U32 uCharacterID, const char *channel_name)
{
	ChatUser *user = userFindByContainerId(uAccountID);
	ChatChannel *channel = channelFindByName(channel_name);
	if (user && channel)
	{
		ChatChannelInfo *info = copyChannelInfo(user, channel, channelFindWatching(user, channel_name), false, false);
		if (info)
		{
#ifdef USE_CHATRELAY
			if (user->uChatRelayID)
				RemoteCommand_crReceiveJoinChannelInfo(GLOBALTYPE_CHATRELAY, user->uChatRelayID, user->id, info);
#else
			RemoteCommand_cmdServerChat_ReceiveJoinChannelInfo(GLOBALTYPE_ENTITYPLAYER, uCharacterID, uCharacterID, info);
#endif
			StructDestroy(parse_ChatChannelInfo, info);
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(CHATRELAY);
void ChatServerSetChannelLevel (ContainerID accountID, char *channel_name, ChannelUserLevel eLevel, U32 uPermissions)
{
	char *escapedChannel = NULL;
	estrStackCreate(&escapedChannel);
	estrAppendEscaped(&escapedChannel, channel_name);
	sendCmdAndParamsToGlobalChat("ChatServerSetChannelLevel", "%d \"%s\" %d %d", accountID, escapedChannel, 
		eLevel, uPermissions);
	estrDestroy(&escapedChannel);
}

//Enables the user's whitelist for all chat
AUTO_COMMAND_REMOTE;
void ChatServerToggleWhitelist(ContainerID userID, bool enabled)
{
	sendCmdAndParamsToGlobalChat("ChatServerToggleWhitelist", "%d %d", userID, enabled);
}

//Enables the user's whitelist for tells only
AUTO_COMMAND_REMOTE;
void ChatServerToggleWhitelistTells(ContainerID userID, bool enabled)
{
	sendCmdAndParamsToGlobalChat("ChatServerToggleWhitelistTells", "%d %d", userID, enabled);
}

//Enables the user's whitelist for emails
AUTO_COMMAND_REMOTE;
void ChatServerToggleWhitelistEmails(ContainerID userID, bool enabled)
{
	sendCmdAndParamsToGlobalChat("ChatServerToggleWhitelistEmails", "%d %d", userID, enabled);
}

AUTO_COMMAND_REMOTE ACMD_CATEGORY(ChatServer) ACMD_ACCESSLEVEL(9) ACMD_IFDEF(GAMESERVER);
void ChatServerRunCommandAsUserByName(CmdContext *context, U32 uID, const char *command, const char *displayName, const char *parameterString)
{
	sendCommandToGlobalChatServer(context->commandString);
}

AUTO_COMMAND_REMOTE ACMD_CATEGORY(ChatServer) ACMD_ACCESSLEVEL(9) ACMD_IFDEF(GAMESERVER);
void ChatServerRunCommandAsUserByID(CmdContext *context, U32 uID, const char *command, U32 accountID, const char *parameterString)
{
	sendCommandToGlobalChatServer(context->commandString);
}

AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(9) ACMD_IFDEF(GAMESERVER);
void ChatServerForceAccessLevel(U32 uID, U32 uAccessLevel)
{
	sendCmdAndParamsToGlobalChat("ChatServerForceAccessLevel", "%d %d", uID, uAccessLevel);
}

AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(9) ACMD_IFDEF(GAMESERVER);
void ChatServerGetAccessLevel(U32 uID)
{
	ChatUser *user = userFindByContainerId(uID);
	if (user && user->uChatRelayID)
		RemoteCommand_crReceiveChatAccessLevel(GLOBALTYPE_CHATRELAY, user->uChatRelayID, uID, user->access_level);
}

AUTO_COMMAND_REMOTE;
int ChatServerForwardWhiteListInfo(CmdContext *context, ContainerID userID, bool bChatEnable, bool bTellsEnable, bool bEmailsEnable)
{
	ChatUser *user = userFindByContainerId(userID);
	if (user)
	{
		NOCONST(ChatUser)* userMod = CONTAINER_NOCONST(ChatUser, user);
		userMod->chatWhitelistEnabled = bChatEnable;
		userMod->tellWhitelistEnabled = bTellsEnable;
		userMod->emailWhitelistEnabled = bEmailsEnable;
		ChatServer_SendWhiteListUpdate(user);
	}
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void ChatPlayerInfo_UpdateActivityString(ContainerID uID, const char *pActivityString)
{
	ChatUser *user = userFindByContainerId(uID);
	char *pEscapedActivity = NULL;

	estrStackCreate(&pEscapedActivity);
	estrAppendEscaped(&pEscapedActivity, pActivityString);
	sendCmdAndParamsToGlobalChat("ChatPlayerInfo_UpdateActivityString", "%d \"%s\"", uID, pEscapedActivity);
	estrDestroy(&pEscapedActivity);

	if (user && user->pPlayerInfo)
	{
		PlayerInfoList list = {0};
		ANALYSIS_ASSUME(user);
		SAFE_FREE(user->pPlayerInfo->playerActivity);
		if (pActivityString && *pActivityString)
			user->pPlayerInfo->playerActivity = strdup(pActivityString);
		userSendActivityStatus(user);
	}
}
