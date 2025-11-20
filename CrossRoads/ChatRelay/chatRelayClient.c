#include "chatRelay.h"

#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "ChatServer/chatBlacklist.h"
#include "GlobalTypes.h"
#include "NotifyEnum.h"
#include "StringCache.h"
#include "textparser.h"
#include "utilitiesLib.h"

#include "AutoGen/chatCommon_h_ast.h"
#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/ChatServer_autogen_RemoteFuncs.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"

extern bool gbDebugMode;
#define CMDDATA_TO_RELAYUSER ChatRelayUser *relayUser = (ChatRelayUser*) pCmdData

AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void crDebugForwardPlayerInfoToAll(GenericServerCmdData *pCmdData, PlayerInfoStruct *pInfo)
{
	CMDDATA_TO_RELAYUSER;
	chatRelay_SendPlayerInfoToAll(relayUser, pInfo);
}

AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void crDebugForwardPlayerInfoToUsers(GenericServerCmdData *pCmdData, PlayerInfoStruct *pInfo, ChatContainerIDList *pList)
{
	CMDDATA_TO_RELAYUSER;
	if (gbDebugMode)
		RemoteCommand_ChatServerForwardPlayerUpdates_Debug(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, pInfo, pList);
}

AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void crPurgeChannels(GenericServerCmdData *pCmdData)
{
	CMDDATA_TO_RELAYUSER;
	if (gbDebugMode)
		RemoteCommand_ChatServerPurgeChannels(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID);
}

AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void crUserLogin(GenericServerCmdData *pCmdData, ChatLoginData *pLoginData)
{
	CMDDATA_TO_RELAYUSER;
	pLoginData->uAccountID = relayUser->uAccountID;

	// Login information here should be only preferences, as data from client cannot be trusted
	devassert(!pLoginData->pAccountName && !pLoginData->pDisplayName);

	if (pLoginData->pPlayerInfo)
	{
		// Populates the Game Name here
		if (pLoginData->pPlayerInfo->gamePublicNameKey)
			free(pLoginData->pPlayerInfo->gamePublicNameKey);
		pLoginData->pPlayerInfo->gamePublicNameKey = StructAllocString(GetProductDisplayNameKey());
		relayUser->eLanguage = pLoginData->pPlayerInfo->eLanguage;
		pLoginData->pPlayerInfo->shardName = allocAddString(GetShardNameFromShardInfoString());
	}
	RemoteCommand_ChatServerLoginOnly(GLOBALTYPE_CHATSERVER, 0, pLoginData);
}

AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crUserChangeLanguage(GenericServerCmdData *pCmdData, int eLanguage)
{
	CMDDATA_TO_RELAYUSER;
	relayUser->eLanguage = eLanguage;
}

static void ChatRelay_SendTell(ChatRelayUser *relayUser, ChatMessage *pMsg)
{
	if (!pMsg || !pMsg->pFrom || !pMsg->pTo)
		return;
	// No identifying info for pTo
	if (pMsg->pTo->accountID == 0 && (!pMsg->pTo->pchHandle || !*pMsg->pTo->pchHandle))
		return;
	RemoteCommand_ChatServerPrivateMesssage_Shard(GLOBALTYPE_CHATSERVER, 0, pMsg);
}

AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crSendMessage(GenericServerCmdData *pCmdData, ChatMessage *pMsg)
{
	CMDDATA_TO_RELAYUSER;
	char pchDynamicChannel[1024] = "";
	const char *pchPlayerName = NULL;
	const char *pchTargetName = NULL;
	Entity *pTarget = NULL;
	static char *s_pchNotifyResponse;

	if (!pMsg)
		return;

	estrClear(&s_pchNotifyResponse);
	
	// Make sure the message is trimmed properly and that all newlines are converted to spaces
	estrTrimLeadingAndTrailingWhitespace(&pMsg->pchText);
	estrReplaceOccurrences(&pMsg->pchText, "\n", " ");

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
	pMsg->pFrom = StructCreate(parse_ChatUserInfo);
	if ( pMsg->pFrom == NULL )
		return;
	pMsg->pFrom->accountID = relayUser->uAccountID;
		
	if (blacklist_CheckForViolations(pMsg->pchText, NULL))
		pMsg->bBlacklistViolation = true;

	// Fixup message channel for special (dynamic) channels
	switch (pMsg->eType)
	{
		case kChatLogEntryType_Zone:
		case kChatLogEntryType_Guild:
		case kChatLogEntryType_Officer:
		case kChatLogEntryType_Local:
			// Send it off to the Game Server the user is on after checking against the blacklist
			RemoteCommand_cmdServerChat_SendMessageFromRelay(GLOBALTYPE_ENTITYPLAYER, relayUser->uEntityID, relayUser->uEntityID, pMsg);
			return;
		xcase kChatLogEntryType_Team:
			ADD_MISC_COUNT(1, "TeamMsgIncrement");
		xcase kChatLogEntryType_TeamUp:
			ADD_MISC_COUNT(1, "TeamUpMsgIncrement");
		xcase kChatLogEntryType_Channel:
			ADD_MISC_COUNT(1, "ChanMsgIncrement");
		xcase kChatLogEntryType_Private:
			ADD_MISC_COUNT(1, "PrivateMsgIncrement");
		xdefault:
			// Do nothing
			break;
	}

	if (*pchDynamicChannel)
		estrCopy2(&pMsg->pchChannel, pchDynamicChannel);

	// Now send the message to either the local area or to a specific channel/tell
	if (pMsg->eType == kChatLogEntryType_Private)
	{
		ChatRelay_SendTell(relayUser, pMsg);
	}
	else
	{
		RemoteCommand_ChatServerMessageReceive_Shard(GLOBALTYPE_CHATSERVER, 0, pMsg);
	}
}

AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0);
void crAddIgnore(GenericServerCmdData *pCmdData, U32 uTargetAccountID, const char *targetHandle, bool bSpammer, const char *pSpamMsg)
{
	CMDDATA_TO_RELAYUSER;
	ChatIgnoreData data = {0};
	data.uUserAccountID = relayUser->uAccountID;
	if (uTargetAccountID)
		data.uTargetAccountID = uTargetAccountID;
	else
		data.pTargetHandle = targetHandle;
	data.bSpammer = bSpammer;
	data.pSpamMessage = pSpamMsg;
	RemoteCommand_ChatServerAddIgnoreByStruct(GLOBALTYPE_CHATSERVER, 0, &data);
}

AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0);
void crRemoveIgnore(GenericServerCmdData *pCmdData, U32 uTargetAccountID, const char *targetHandle)
{
	CMDDATA_TO_RELAYUSER;
	if (uTargetAccountID)
		RemoteCommand_ChatServerRemoveIgnoreByID_Shard(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, uTargetAccountID);
	else
		RemoteCommand_ChatServerRemoveIgnoreByHandle_Shard(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, targetHandle);
}

AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0);
void crAddFriend(GenericServerCmdData *pCmdData, U32 uTargetAccountID, const char *targetHandle, bool bWasRequest)
{
	CMDDATA_TO_RELAYUSER;
	if (bWasRequest)
	{
		if (uTargetAccountID)
			RemoteCommand_ChatServerAcceptFriendByID_Shard(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, uTargetAccountID);
		else
			RemoteCommand_ChatServerAcceptFriendByHandle_Shard(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, targetHandle);
	}
	else
	{
		if (uTargetAccountID)
			RemoteCommand_ChatServerAddFriendByID_Shard(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, uTargetAccountID);
		else
			RemoteCommand_ChatServerAddFriendByHandle_Shard(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, targetHandle);
	}
}

AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0);
void crAddFriendComment(GenericServerCmdData *pCmdData, const char *handle, const char *pchComment)
{
	CMDDATA_TO_RELAYUSER;
	RemoteCommand_ChatServerAddFriendComment_Shard(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, handle, pchComment);
}

AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0);
void crRemoveFriend(GenericServerCmdData *pCmdData, U32 uTargetAccountID, const char *targetHandle, bool bWasRequest)
{
	CMDDATA_TO_RELAYUSER;
	if (bWasRequest)
	{
		if (uTargetAccountID)
			RemoteCommand_ChatServerRejectFriendByID_Shard(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, uTargetAccountID);
		else
			RemoteCommand_ChatServerRejectFriendByHandle_Shard(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, targetHandle);
	}
	else
	{
		if (uTargetAccountID)
			RemoteCommand_ChatServerRemoveFriendByID_Shard(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, uTargetAccountID);
		else
			RemoteCommand_ChatServerRemoveFriendByHandle_Shard(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, targetHandle);
	}
}

// Toggle Friends-only status option
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0);
void crToggleFriendsOnlyStatus(GenericServerCmdData *pCmdData, U32 enabled)
{
	CMDDATA_TO_RELAYUSER;
	if (enabled)
		RemoteCommand_UserFriendsOnly(NULL, GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID);
	else
		RemoteCommand_UserVisible(NULL, GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID);
}

// Toggle Hidden status option
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0);
void crToggleHiddenStatus(GenericServerCmdData *pCmdData, U32 enabled)
{
	CMDDATA_TO_RELAYUSER;
	if (enabled)
		RemoteCommand_UserHidden(NULL, GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID);
	else
		RemoteCommand_UserVisible(NULL, GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID);
}

AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0);
void crSetStatusMessage(GenericServerCmdData *pCmdData, const char *pchMessage)
{
	CMDDATA_TO_RELAYUSER;
	RemoteCommand_UserSetStatus(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, pchMessage);
}

//Set the chat whitelist
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0);
void crToggleWhitelist(GenericServerCmdData *pCmdData, U32 enabled)
{
	CMDDATA_TO_RELAYUSER;
	RemoteCommand_ChatServerToggleWhitelist(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, enabled);
}

//Set the tells whitelist
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0);
void crToggleWhitelistTells(GenericServerCmdData *pCmdData, U32 enabled)
{
	CMDDATA_TO_RELAYUSER;
	RemoteCommand_ChatServerToggleWhitelistTells(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, enabled);
}

//Set the emails whitelist
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0);
void crToggleWhitelistEmails(GenericServerCmdData *pCmdData, U32 enabled)
{
	CMDDATA_TO_RELAYUSER;
	RemoteCommand_ChatServerToggleWhitelistEmails(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, enabled);
}

AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crRequestFriendList(GenericServerCmdData *pCmdData)
{
	CMDDATA_TO_RELAYUSER;
	RemoteCommand_ChatServerGetFriendsList(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID);
}

AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crRequestIgnoreList(GenericServerCmdData *pCmdData)
{
	CMDDATA_TO_RELAYUSER;
	RemoteCommand_ChatServerGetIgnoreList(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID);
}

/////////////////////////////////
// Channels

AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crRequestUserChannelList(GenericServerCmdData *pCmdData, U32 uFlags)
{
	CMDDATA_TO_RELAYUSER;
	RemoteCommand_ChatServerGetUserChannels(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, 0, uFlags);
}

AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crRequestFullChannelInfo(GenericServerCmdData *pCmdData, const char *channel_name)
{
	CMDDATA_TO_RELAYUSER;
	RemoteCommand_ChatServerGetChannelInfo(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, 0, channel_name);
}

AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crRequestJoinChannelInfo(GenericServerCmdData *pCmdData, const char *channel_name)
{
	CMDDATA_TO_RELAYUSER;
	RemoteCommand_ChatServerGetJoinChannelInfo(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, 0, channel_name);
}

AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crRequestChannelMemberList(GenericServerCmdData *pCmdData, const char *channel_name)
{
	CMDDATA_TO_RELAYUSER;
	RemoteCommand_ChatServerChannelListMembers(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, channel_name);
}

// Join a channel (must already exist)
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crJoinChannel(GenericServerCmdData *pCmdData, const char *channel_name)
{
	CMDDATA_TO_RELAYUSER;
	if (channel_name && *channel_name)
		RemoteCommand_ChatServerJoinChannel(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, 
			channel_name, CHANNEL_SPECIAL_NONE, false);
}

// Join a channel. Create it if it doesn't exist.
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crJoinOrCreateChannel(GenericServerCmdData *pCmdData, const char *channel_name)
{
	CMDDATA_TO_RELAYUSER;
	if (channel_name && *channel_name)
		RemoteCommand_ChatServerJoinChannel(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, 
			channel_name, CHANNEL_SPECIAL_NONE, true);
}

// Leave a channel
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crLeaveChannel(GenericServerCmdData *pCmdData, const char *channel_name)
{
	CMDDATA_TO_RELAYUSER;
	// not allowed to unsubscribe from built-ins
	if (ChatCommon_IsBuiltInChannel(channel_name))
		return;
	if (channel_name)
		RemoteCommand_ChatServerLeaveChannel(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, channel_name);
}

// Invite a user to join the channel (operator and above only)
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crInviteChatHandleToChannel(GenericServerCmdData *pCmdData, const char *channel_name, const char *pInviterName, char *chatHandle)
{
	CMDDATA_TO_RELAYUSER;
	if (chatHandle && *chatHandle)
	{
		const char *pchHandle = strchr(chatHandle, '@');
		// Don't include character name or @ in handle.
		pchHandle = pchHandle ? pchHandle+1 : chatHandle;

		RemoteCommand_ChatServerInviteByHandle(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, 
			pInviterName, channel_name, pchHandle);
	}
}

// Add a new channel Message of the Day (operator only)
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crSetChannelMotd(GenericServerCmdData *pCmdData, const char *channel_name, const char *motd)
{
	CMDDATA_TO_RELAYUSER;
	if (channel_name && motd)
	{
		RemoteCommand_ChatServerSetMotd(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, channel_name, motd);
	}
}

// Set the channel description (operator only)
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crSetChannelDescription(GenericServerCmdData *pCmdData, const char *channel_name, const char *description)
{
	CMDDATA_TO_RELAYUSER;
	if (channel_name && description)
	{
		RemoteCommand_ChatServerSetChannelDescription(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, channel_name, description);
	}
}

// Set the channel's access level (operator only)
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crSetChannelAccess(GenericServerCmdData *pCmdData, const char *channel_name, char *accessString)
{
	CMDDATA_TO_RELAYUSER;
	RemoteCommand_ChatServerSetChannelAccess(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, channel_name, accessString);
}

// Set the channel's permission levels
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crModifyChannelPermissions(GenericServerCmdData *pCmdData, const char *channel_name, ChannelUserLevel eLevel, U32 uPermissions)
{
	CMDDATA_TO_RELAYUSER;
	RemoteCommand_ChatServerSetChannelLevel(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, channel_name, eLevel, uPermissions);
}

static void crChangeUserPermission(ChatRelayUser *relayUser, const char *channel_name, const char *targetHandle, U32 uAddFlags, U32 uRemoveFlags)
{
	RemoteCommand_ChatServerSetUserAccessByHandleNew(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, channel_name, 
		targetHandle, uAddFlags, uRemoveFlags);
}

// Promote a user in a channel (operator only)
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crPromoteInChannel(GenericServerCmdData *pCmdData, const char *channel_name, const char *targetHandle)
{
	CMDDATA_TO_RELAYUSER;
	crChangeUserPermission(relayUser, channel_name, targetHandle, CHANPERM_PROMOTE, 0);
}

// Demote a user in a channel (operator only)
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crDemoteInChannel(GenericServerCmdData *pCmdData, const char *channel_name, const char *targetHandle)
{
	CMDDATA_TO_RELAYUSER;
	crChangeUserPermission(relayUser, channel_name, targetHandle, 0, CHANPERM_DEMOTE);
}

// Kick a user from a channel (operator only).
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crKickFromChannel(GenericServerCmdData *pCmdData, const char *channel_name, const char * targetHandle)
{
	CMDDATA_TO_RELAYUSER;
	crChangeUserPermission(relayUser, channel_name, targetHandle, 0, CHANPERM_KICK);
}

// Mute a user in a channel (operator only).
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crMuteInChannel(GenericServerCmdData *pCmdData, const char *channel_name, const char * targetHandle)
{
	CMDDATA_TO_RELAYUSER;
	crChangeUserPermission(relayUser, channel_name, targetHandle, 0, CHANPERM_MUTE);
}

// Unmute a user in a channel (operator only).
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crUnmuteInChannel(GenericServerCmdData *pCmdData, const char *channel_name, const char * targetHandle)
{
	CMDDATA_TO_RELAYUSER;
	crChangeUserPermission(relayUser, channel_name, targetHandle, CHANPERM_MUTE, 0);
}

// Unmute a user in a channel (operator only).
AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crDestroyChannel(GenericServerCmdData *pCmdData, const char *channel_name)
{
	CMDDATA_TO_RELAYUSER;
	if (channel_name)
		RemoteCommand_ChatServerDestroyChannel(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, channel_name);
}

AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crChannelUninvite(GenericServerCmdData *pCmdData, const char *channel_name, char *chatHandle)
{
	CMDDATA_TO_RELAYUSER;
	const char *pchHandle = strchr(chatHandle, '@');
	// Don't include character name or @ in handle.
	pchHandle = pchHandle ? pchHandle+1 : chatHandle;
	RemoteCommand_ChatServerChannelUninviteByHandle(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, pchHandle, channel_name);
}

AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crDeclineInviteToChannel(GenericServerCmdData *pCmdData, const char *channel_name)
{	
	CMDDATA_TO_RELAYUSER;
	RemoteCommand_ChatServerDeclineChannelInvite(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID, channel_name);
}

AUTO_COMMAND ACMD_GENERICSERVERCMD ACMD_CATEGORY(ChatRelayCmds) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void crGetActiveMaps(GenericServerCmdData *pCmdData)
{
	CMDDATA_TO_RELAYUSER;
	RemoteCommand_ChatServerGetActiveMaps(GLOBALTYPE_CHATSERVER, 0, relayUser->uAccountID);
}
