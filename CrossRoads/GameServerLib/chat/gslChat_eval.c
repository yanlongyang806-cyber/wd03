/***************************************************************************
*     Copyright (c) 2005-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "chatCommonStructs.h"
#include "GameAccountDataCommon.h"
#include "gslChat.h"
#include "gslFriendsIgnore.h"
#include "gslPartition.h"
#include "StringUtil.h"
#include "Guild.h"
#include "Team.h"
#include "Entity.h"
#include "EntityLib.h"
#include "Player.h"
#include "estring.h"
#include "GameServerLib.h"

#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/Player_h_ast.h"
#include "AutoGen/ChatServer_autogen_remotefuncs.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

//
// Chat Commands
//

static const char * formatChannelName (Entity *pEnt, const char *channel_name)
{
	// TODO make these into game-specific translations (tied to ChatServer\channels.c)
	static char buffer[ASCII_NAME_MAX_LENGTH];
	if (!channel_name) return NULL;
	if (strlen(channel_name) > ASCII_NAME_MAX_LENGTH -1)
		return channel_name;
	if (stricmp(channel_name, CHAT_TEAM_SHORTCUT) == 0)
	{
		int teamID = team_GetTeamID(pEnt);
		if (teamID)
			return team_MakeTeamChannelNameFromID(SAFESTR(buffer), teamID);
	} else if (stricmp(channel_name, CHAT_ZONE_SHORTCUT) == 0)
	{
		if (GAMESERVER_IS_UGCEDIT)
			return UGCEDIT_CHANNEL_NAME;
		else
			return getZoneChannelName(pEnt);
	} else if (stricmp(channel_name, CHAT_GUILD_SHORTCUT) == 0)
	{
		int guildID = guild_GetGuildID(pEnt);
		if (guildID)
			return guild_GetGuildChannelNameFromID(SAFESTR(buffer), guildID, GAMESERVER_VSHARD_ID);
	} else if (stricmp(channel_name, CHAT_GUILD_OFFICER_SHORTCUT) == 0)
	{
		int guildID = guild_GetGuildID(pEnt);
		if (guildID)
			return guild_GetOfficerChannelNameFromID(SAFESTR(buffer), guildID, GAMESERVER_VSHARD_ID);
	}
	return channel_name;
}

static void ServerChat_SendMessageResponse(U32 uEntityID, U32 uQueueID, ChatEntityCommandReturnCode eReturnCode)
{
	RemoteCommand_ChatLocal_MessageResponse(GLOBALTYPE_CHATSERVER, 0, uEntityID, uQueueID, eReturnCode);
}

AUTO_COMMAND_REMOTE;
void cmdServerChat_ReconnectToChatServer(bool bOnline) {
	GameServer_ReconnectGlobalChat(bOnline);
}

#ifndef USE_CHATRELAY
AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void cmdServerChat_SendChannelSystemMessage(ContainerID entID, int eType, const char *channel_name, const char *msg, U32 uQueueID) {
	ServerChat_SendMessageResponse(entID, uQueueID, ServerChat_SendChannelSystemMessage(entID, eType, channel_name, msg));
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void cmdServerChat_SendSystemAlert(ContainerID entID, const char *title, const char *text, U32 uQueueID) {
	ServerChat_SendMessageResponse(entID, uQueueID, ServerChat_SendSystemAlert(entID, title, text));
}

// Create and join a new channel
AUTO_COMMAND ACMD_NAME(create,channel_create) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Social);
void cmdServerChat_CreateChannel(Entity *pEnt, const char *channel_name) {
	ServerChat_CreateChannel(pEnt, formatChannelName(pEnt, channel_name));
}

// Join a channel (must already exist)
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChat_JoinChannel(Entity *pEnt, const char *channel_name) {
	ServerChat_JoinChannel(pEnt, formatChannelName(pEnt, channel_name), false);
}

// Join a channel. Create it if it doesn't exist.
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChat_JoinOrCreateChannel(Entity *pEnt, const char *channel_name) {
	ServerChat_JoinChannel(pEnt, formatChannelName(pEnt, channel_name), true);
}

// Leave a channel
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChat_LeaveChannel(Entity *pEnt, const char *channel_name) {
	ServerChat_LeaveChannel(pEnt, formatChannelName(pEnt, channel_name));
}

// Invite a user to join the channel (operator and above only)
AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(cinvite, chaninvite, channel_invite) ACMD_ACCESSLEVEL(0);
void cmdServerChat_InviteChatHandleToChannel(Entity *pEnt, const char *channel_name, char *chatHandle) {
	ServerChat_InviteChatHandleToChannel(pEnt, channel_name, chatHandle);
}

// Add a new channel Message of the Day (operator only)
AUTO_COMMAND ACMD_NAME(motd,channel_motd) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Chat);
void cmdServerChat_SetChannelMotd(Entity *pEnt, const char *channel_name, const ACMD_SENTENCE motd) {
	ServerChat_SetChannelMotd(pEnt, formatChannelName(pEnt, channel_name), motd);
}

// Set the channel description (operator only)
AUTO_COMMAND ACMD_NAME(channel_description) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Chat);
void cmdServerChat_SetChannelDescription(Entity *pEnt, const char *channel_name, const ACMD_SENTENCE description) {
	ServerChat_SetChannelDescription(pEnt, formatChannelName(pEnt, channel_name), description);
}

// Set the channel's access level (operator only)
AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(caccess,chanaccess,channel_access) ACMD_ACCESSLEVEL(0);
void cmdServerChat_SetChannelAccess(Entity *pEnt, const char *channel_name, char *accessString) {
	ServerChat_SetChannelAccess(pEnt, formatChannelName(pEnt, channel_name), accessString);
}

// Set the channel's permission levels
AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(clevel) ACMD_ACCESSLEVEL(0);
void cmdServerChat_ModifyChannelPermissions(Entity *pEnt, const char *channel_name, ChannelUserLevel eLevel, U32 uPermissions) {
	ServerChat_ModifyChannelPermissions(pEnt, formatChannelName(pEnt, channel_name), eLevel, uPermissions);
}

// Promote a user in a channel (operator only)
AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(chanpromote) ACMD_ACCESSLEVEL(0);
void cmdServerChat_PromoteInChannel(Entity *pEnt, const char *channel_name, char *targetHandle) {
	ServerChat_SetUserAccess(pEnt, formatChannelName(pEnt, channel_name), targetHandle, CHANPERM_PROMOTE, 0);
}

// Demote a user in a channel (operator only)
AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(chandemote) ACMD_ACCESSLEVEL(0);
void cmdServerChat_DemoteInChannel(Entity *pEnt, const char *channel_name, char *targetHandle) {
	ServerChat_SetUserAccess(pEnt, formatChannelName(pEnt, channel_name), targetHandle, 0, CHANPERM_DEMOTE);
}

// Kick a user from a channel (operator only).
AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(channel_kick) ACMD_ACCESSLEVEL(0);
void cmdServerChat_KickFromChannel(Entity *pEnt, const char *channel_name, ACMD_SENTENCE chatHandle) {
	ServerChat_SetUserAccess(pEnt, formatChannelName(pEnt, channel_name), chatHandle, 0, CHANPERM_KICK);
}

// Mute a user in a channel (operator only).
AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(mute) ACMD_ACCESSLEVEL(0);
void cmdServerChat_MuteInChannel(Entity *pEnt, const char *channel_name, ACMD_SENTENCE chatHandle) {
	ServerChat_SetUserAccess(pEnt, formatChannelName(pEnt, channel_name), chatHandle, 0, CHANPERM_MUTE);
}

// Unmute a user in a channel (operator only).
AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(unmute) ACMD_ACCESSLEVEL(0);
void cmdServerChat_UnmuteInChannel(Entity *pEnt, const char *channel_name, ACMD_SENTENCE chatHandle) {
	ServerChat_SetUserAccess(pEnt, formatChannelName(pEnt, channel_name), chatHandle, CHANPERM_MUTE, 0);
}

// Unmute a user in a channel (operator only).
AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(channel_destroy) ACMD_ACCESSLEVEL(0);
void cmdServerChat_DestroyChannel(Entity *pEnt, const char *channel_name) {
	ServerChat_DestroyChannel(pEnt, channel_name);
}
#endif

// This is no longer called from client since all routing is done through ChatRelays now
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChat_SendMessage(Entity *pFromEnt, SA_PARAM_NN_VALID ChatMessage *pMsg) {
	if (pMsg->pchChannel)
	{
		int iPartitionIdx = entGetPartitionIdx(pFromEnt);
		const char *formatted_name = formatChannelName(pFromEnt, pMsg->pchChannel);
		if (stricmp(pMsg->pchChannel, "Zone") == 0 || strnicmp(pMsg->pchChannel, ZONE_CHANNEL_PREFIX, strlen(ZONE_CHANNEL_PREFIX)) == 0)
			pMsg->iInstanceIndex = partition_PublicInstanceIndexFromIdx(iPartitionIdx);
		if (formatted_name != pMsg->pchChannel)
			estrCopy2(&pMsg->pchChannel, formatted_name);
	}
	ServerChat_SendMessage(pFromEnt, pMsg);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATRELAY);
void cmdServerChat_SendMessageFromRelay(U32 uEntityConID, SA_PARAM_NN_VALID ChatMessage *pMsg)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uEntityConID);
	if (pEnt)
		cmdServerChat_SendMessage(pEnt, pMsg);
}

#ifndef USE_CHATRELAY
AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void cmdServerChat_MessageReceive(ContainerID targetID, SA_PARAM_NN_VALID ChatMessage *pMsg, U32 uQueueID) {
	if (pMsg->pchChannel && stricmp(pMsg->pchChannel, "Zone") == 0)
	{
		// TODO(Theo) fix this for partitions - optional since this is deprecated code
		estrCopy2(&pMsg->pchChannel, getZoneChannelName());
	}
	ServerChat_SendMessageResponse(targetID, uQueueID, ServerChat_MessageReceive(targetID, pMsg));
}
#endif

// Send an admin message to all players on the server
AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(4);
void cmdServerChat_BroadcastMessage(ACMD_SENTENCE msg, S32 eNotifyType) {
	ServerChat_BroadcastMessage(msg, eNotifyType);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChat_FindPlayersSimple(Entity *pEnt, bool bSendList, const char *pchFilter)
{
	ServerChat_FindPlayersSimple(pEnt, bSendList, pchFilter);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChat_FindPlayers(Entity *pEnt, PlayerFindFilterStruct *pFilters)
{
	ServerChat_FindPlayers(pEnt, pFilters);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void cmdServerChat_FindTeams(Entity *pEnt, PlayerFindFilterStruct *pFilters)
{
	ServerChat_FindTeams(pEnt, pFilters);
}

// Toggle anonymous status
AUTO_COMMAND ACMD_NAME(ChatHidden, anon, hide) ACMD_ACCESSLEVEL(0);
void cmdServerChat_SetHidden(Entity *pEnt)
{
	ServerChat_SetHidden(pEnt);
}

AUTO_COMMAND ACMD_NAME(ChatFriendsOnly, FriendsOnly) ACMD_ACCESSLEVEL(0);
void cmdServerChat_SetFriendsOnly(Entity *pEnt)
{
	ServerChat_SetFriendsOnly(pEnt);
}

AUTO_COMMAND ACMD_NAME(ChatVisible, unanon, unhide) ACMD_ACCESSLEVEL(0);
void cmdServerChat_SetVisible(Entity *pEnt)
{
	ServerChat_SetVisible(pEnt);
}

// Toggle Looking For Group status
AUTO_COMMAND ACMD_NAME(lfg, lft) ACMD_ACCESSLEVEL(0);
void cmdServerChat_ToggleLFG(Entity *pEnt)
{
	if (!pEnt || !pEnt->pPlayer)
		return;
	if (pEnt->pPlayer->eLFGMode == TeamMode_Closed || pEnt->pPlayer->eLFGMode == TeamMode_Prompt)
	{
		ServerChat_SetLFGMode(pEnt, TeamMode_Open);
	}
	else
	{
		ServerChat_SetLFGMode(pEnt, TeamMode_Closed);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(LFG_Mode);
void cmdServerChat_SetLFGMode(Entity *pEnt, ACMD_NAMELIST(TeamModeEnum, STATICDEFINE) char *pcMode)
{
	TeamMode eMode;

	eMode = StaticDefineIntGetInt(TeamModeEnum, pcMode);
	if (eMode < 0) 
	{		
		return;
	}

	ServerChat_SetLFGMode(pEnt, eMode);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(LFGDifficulty_Mode);
void cmdServerChat_SetLFGDifficultyMode(Entity *pEnt, ACMD_NAMELIST(LFGDifficultyModeEnum, STATICDEFINE) char *pcMode)
{
	LFGDifficultyMode eMode;

	eMode = StaticDefineIntGetInt(LFGDifficultyModeEnum, pcMode);
	if (eMode < 0) 
	{		
		return;
	}

	ServerChat_SetLFGDifficultyMode(pEnt, eMode);
}

#ifndef USE_CHATRELAY
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat) ACMD_PRIVATE;
void cmdServerChat_RequestUserChannelList(Entity *pEnt, U32 uFlags) {
	ServerChat_RequestUserChannelList(pEnt, uFlags);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat) ACMD_PRIVATE;
void cmdServerChat_RequestFullChannelInfo(Entity *pEnt, ACMD_SENTENCE channel_name) {
	ServerChat_RequestFullChannelInfo(pEnt, channel_name);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat) ACMD_PRIVATE;
void cmdServerChat_RequestJoinChannelInfo(Entity *pEnt, ACMD_SENTENCE channel_name) {
	ServerChat_RequestJoinChannelInfo(pEnt, channel_name);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void cmdServerChat_ReceiveUserChannelList (U32 entID, ChatChannelInfoList *pList) {
	ServerChat_ReceiveUserChannelList(entID, pList);
}
#endif

#ifndef USE_CHATRELAY
AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void cmdServerChat_LoginSucceeded (U32 entID) {
	ServerChat_LoginSucceeded(entID);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void cmdServerChat_ReceiveJoinChannelInfo (U32 entID, ChatChannelInfo *pInfo) {
	ServerChat_ReceiveJoinChannelInfo (entID, pInfo);
}
#endif

// Handles GameServer-specific Chat Login procedures
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_HIDE;
void cmdServerChat_GameLogin(Entity *pEnt)
{
	ServerChat_Login(pEnt);
}

//
// Friend/Ignore
//

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void gslChat_ForwardFriendsCallbackCmd(ContainerID entID, ChatPlayerStruct *chatPlayerStruct, FriendResponseEnum eType, const char *errorString)
{
	gslChat_ForwardFriendsCallback(entID, chatPlayerStruct, eType, errorString);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void gslChat_ForwardIgnoreCallbackCmd(ContainerID entID, ChatPlayerStruct *chatPlayerStruct, IgnoreResponseEnum eType, const char *errorString)
{
	gslChat_ForwardIgnoreCallback(entID, chatPlayerStruct, eType, errorString);
}

AUTO_COMMAND_REMOTE;
void gslChat_RefreshFriendListReturnCmd(U32 uID, ChatPlayerList *friendList)
{
	gslChat_RefreshFriendListReturn(uID, friendList);
}

AUTO_COMMAND_REMOTE;
void gslChat_RefreshClientIgnoreListReturnCmd(U32 uID, ChatPlayerList *ignoreList)
{
	gslChat_RefreshClientIgnoreListReturn(uID, ignoreList);
}

#ifndef USE_CHATRELAY
// Ignore a player.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(Ignore) ACMD_CATEGORY(Social) ACMD_SERVERCMD;
void gslChat_AddIgnoreCmd(Entity *pEnt, const ACMD_SENTENCE ignoreName) {
	gslChat_AddIgnore(pEnt, ignoreName);
} 

//ignore a player and mark them as a spammer
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(Ignore_Spammer) ACMD_CATEGORY(Social) ACMD_SERVERCMD;
void gslChat_AddIgnoreSpammerCmd(Entity *pEnt, const ACMD_SENTENCE ignoreName) {
	gslChat_AddIgnoreSpammer(pEnt, ignoreName);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslChat_AddIgnoreByAccountIDCmd(Entity *pEnt, U32 ignoreAccountID) {
	gslChat_AddIgnoreByAccountID(pEnt, ignoreAccountID);
}

// Stop ignoring a player.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(Unignore, RemoveIgnore) ACMD_CATEGORY(Social);
void gslChat_RemoveIgnoreCmd(Entity *pEnt, const ACMD_SENTENCE ignoreName) {
	gslChat_RemoveIgnore(pEnt, ignoreName);
}

//Adds a friend to your friends list using "<name>" "@<chathandle>" or "<name>@<chathandle>" syntax
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(Friend) ACMD_CATEGORY(Social) ACMD_SERVERCMD;
void gslChat_AddFriendCmd(Entity *pEnt, ACMD_SENTENCE friendHandleOrName) {
	gslChat_AddFriend(pEnt, friendHandleOrName);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslChat_AddFriendByAccountIDCmd(Entity *pEnt, U32 friendAccountID) {
	gslChat_AddFriendByAccountID(pEnt, friendAccountID);
}

//Adds a friend to your friends list using "<name>" "@<chathandle>" or "<name>@<chathandle>" syntax
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(FriendComment) ACMD_CATEGORY(Social) ACMD_SERVERCMD;
void gslChat_AddFriendCommentCmd(Entity *pEnt, char *friendHandleOrName, ACMD_SENTENCE pcComment) {
	gslChat_AddFriendComment(pEnt, friendHandleOrName, pcComment);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslChat_AddFriendCommentByAccountIDCmd(Entity *pEnt, U32 friendAccountID, ACMD_SENTENCE pcComment) {
	gslChat_AddFriendCommentByAccountID(pEnt, friendAccountID, pcComment);
}

//Accepts a friend request using "<name>" "@<chathandle>" or "<name>@<chathandle>" syntax
AUTO_COMMAND ACMD_NAME(AcceptFriend, Befriend) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Social) ACMD_SERVERCMD;
void gslChat_AcceptFriendCmd(Entity *pEnt, ACMD_SENTENCE friendHandleOrName) {
	gslChat_AcceptFriend(pEnt, friendHandleOrName);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslChat_AcceptFriendByAccountIDCmd(Entity *pEnt, U32 friendAccountID) {
	gslChat_AcceptFriendByAccountID(pEnt, friendAccountID);
}
AUTO_COMMAND ACMD_NAME(RejectFriend) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Social) ACMD_SERVERCMD;
void gslChat_RejectFriendCmd(Entity *pEnt, ACMD_SENTENCE friendHandleOrName) {
	gslChat_RejectFriend(pEnt, friendHandleOrName);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslChat_RejectFriendByAccountIDCmd(Entity *pEnt, U32 friendAccountID) {
	gslChat_RejectFriendByAccountID(pEnt, friendAccountID);
}

//Removes a friend from your friends list using "<name>" "@<chathandle>" or "<name>@<chathandle>" syntax
AUTO_COMMAND ACMD_NAME(RemoveFriend, Unfriend) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Social) ACMD_SERVERCMD;
void gslChat_RemoveFriendCmd(Entity *pEnt, ACMD_SENTENCE friendHandleOrName) {
	gslChat_RemoveFriend(pEnt, friendHandleOrName);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslChat_RemoveFriendByAccountIDCmd(Entity *pEnt, U32 friendAccountID) {
	gslChat_RemoveFriendByAccountID(pEnt, friendAccountID);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslChat_AddFriendCommentByContainerIDCmd(Entity *pEnt, U32 friendContainerID, ACMD_SENTENCE pcComment) {
	gslChat_AddFriendCommentByContainerID(pEnt, friendContainerID, pcComment);
}
#endif

// Friend/Ignore by ContainerID commands
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslChat_AddIgnoreByContainerIDCmd(Entity *pEnt, U32 ignoreContainerID) {
	gslChat_AddIgnoreByContainerID(pEnt, ignoreContainerID);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslChat_AddFriendByContainerIDCmd(Entity *pEnt, U32 friendContainerID) {
	gslChat_AddFriendByContainerID(pEnt, friendContainerID);
}
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslChat_AcceptFriendByContainerIDCmd(Entity *pEnt, U32 friendContainerID) {
	gslChat_AcceptFriendByContainerID(pEnt, friendContainerID);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslChat_RejectFriendByContainerIDCmd(Entity *pEnt, U32 friendContainerID) {
	gslChat_RejectFriendByContainerID(pEnt, friendContainerID);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslChat_RemoveFriendByContainerIDCmd(Entity *pEnt, U32 friendContainerID) {
	gslChat_RemoveFriendByContainerID(pEnt, friendContainerID);
}

#ifndef USE_CHATRELAY
//Toggles the whitelist for all chat.  If enabled, you will only receive messages from friends, SG members, and Team members
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(Whitelist_Chat) ACMD_CATEGORY(Social) ACMD_SERVERCMD;
void gslChat_ToggleWhitelistCmd(Entity *pEnt, U32 enabled)
{
	gslChat_ToggleWhitelist(pEnt, enabled);
}

//Toggles the whitelist for all chat.  If enabled, you will only receive tells from friends, SG members, and Team members
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(Whitelist_Tells) ACMD_CATEGORY(Social) ACMD_SERVERCMD;
void gslChat_ToggleWhitelistTellsCmd(Entity *pEnt, U32 enabled)
{
	gslChat_ToggleWhitelistTells(pEnt, enabled);
}

//Toggles the whitelist for all chat.  If enabled, you will only receive emails from friends, SG members, and Team members
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(Whitelist_Emails) ACMD_CATEGORY(Social) ACMD_SERVERCMD;
void gslChat_ToggleWhitelistEmailsCmd(Entity *pEnt, U32 enabled)
{
	gslChat_ToggleWhitelistEmails(pEnt, enabled);
}
#endif

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9);
void gslChat_RunCommandAsUser(Entity *pEnt, const char *displayName, const char *command, ACMD_SENTENCE parameterString)
{
	if (pEnt && pEnt->pPlayer)
		RemoteCommand_ChatServerRunCommandAsUserByName(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, 
			command, displayName, parameterString);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(ChatAccessLevel) ACMD_ACCESSLEVEL(4);
void gslChat_ForceChatAccessLevel (Entity *pEnt, U32 uAccessLevel)
{
	if (pEnt && pEnt->pPlayer && (U32) entGetAccessLevel(pEnt) >= uAccessLevel)
	{
		GameAccountData *pGAD = entity_GetGameAccount(pEnt);
		RemoteCommand_ChatServerForceAccessLevel(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, uAccessLevel);
		if (pGAD && (U32)gad_GetLastChatAccessLevel(pGAD) != uAccessLevel)
			AutoTrans_GameAccount_tr_LastChatAccessLevel(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GAMEACCOUNTDATA, 
				entGetAccountID(pEnt), uAccessLevel);
	}
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(ChatAccessLevel);
void GetChatAccessLevel(Entity *pEnt, CmdContext *pCmdContext)
{
	if (pEnt && pEnt->pPlayer)
		RemoteCommand_ChatServerGetAccessLevel(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID);
}


AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(4) ACMD_NAME(ChatBanUser);
void gslChat_BanUserByHandle(Entity *pEnt, ACMD_SENTENCE handle)
{
	if (pEnt && pEnt ->pPlayer)
	{
		char *passedHandle = (handle && handle[0] == '@') ? handle+1 : handle;
		RemoteCommand_userBanByHandle(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, passedHandle);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(4) ACMD_NAME(ChatUnbanUser);
void gslChat_UnbanUserByHandle(Entity *pEnt, ACMD_SENTENCE handle)
{
	if (pEnt && pEnt ->pPlayer)
	{
		char *passedHandle = (handle && handle[0] == '@') ? handle+1 : handle;
		RemoteCommand_userUnbanByHandle(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, passedHandle);
	}
}
