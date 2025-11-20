#include "users.h"

#include "channels.h"
#include "chatCommandStrings.h"
#include "chatGlobal.h"
#include "chatLocal.h"
#include "chatRelay/chatRelayManager.h"
#include "ChatServer.h"
#include "ChatServer/chatShared.h"
#include "chatShardConfig.h"
#include "chatUtilities.h"
#include "friendsIgnore.h"
#include "msgsend.h"
#include "NotifyEnum.h"
#include "objContainerIO.h"
#include "shardnet.h"
#include <string.h>
#include "StringUtil.h"
#include "TextFilter.h"
#include "utilitiesLib.h"

#include "AutoGen/chatdb_h_ast.h"
#include "AutoGen/AppLocale_h_ast.h"
#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/chatRelayManager_h_ast.h"
#include "AutoGen/Controller_autogen_RemoteFuncs.h"
#include "AutoGen/ChatRelay_autogen_RemoteFuncs.h"
#include "AutoGen/ChatServer_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"

#include "AutoTransDefs.h"

int g_silence_all;

static EARRAY_OF(ChatUserMinimal) seaGMUsers = NULL;
extern ShardChatServerConfig gShardChatServerConfig;

static void userAddGM(SA_PARAM_NN_VALID ChatUser *user)
{
	int idx = eaIndexedFindUsingInt(&seaGMUsers, user->id);
	ChatUserMinimal *pPlayer;

	if (!seaGMUsers)
		eaIndexedEnable(&seaGMUsers, parse_ChatUserMinimal);
	if (idx == -1)
	{
		pPlayer = StructCreate(parse_ChatUserMinimal);
		pPlayer->id = user->id;
		pPlayer->handle = strdup(user->handle);
		eaIndexedAdd(&seaGMUsers, pPlayer);
	}
	else
		pPlayer = seaGMUsers[idx];
}
static void userRemoveGM(SA_PARAM_NN_VALID ChatUser *user)
{
	int idx = eaIndexedFindUsingInt(&seaGMUsers, user->id);
	if (idx != -1)
	{
		StructDestroy(parse_ChatUserMinimal, seaGMUsers[idx]);
		eaRemove(&seaGMUsers, idx);
	}
}
EARRAY_OF(ChatUserMinimal) userGetGMList(void)
{
	return seaGMUsers;
}
bool userIsGM(U32 uID, const char *handle)
{
	if (uID == 0 && (handle == NULL || *handle == 0))
		return false;
	if (uID)
		return eaIndexedFindUsingInt(&seaGMUsers, uID) != -1;
	EARRAY_FOREACH_BEGIN(seaGMUsers, i);
	{
		if (stricmp(seaGMUsers[i]->handle, handle) == 0)
			return true;
	}
	EARRAY_FOREACH_END;
	return false;
}
bool userIsSocialRestricted(ChatUser *user)
{
	if (user)
	{
		if (UserIsAdmin(user))
			return false;
		if (user->pPlayerInfo)
			return user->pPlayerInfo->bSocialRestricted;
	}
	return true;
}

int userCanTalk(ChatUser *user)
{
	int time;
	if (g_silence_all)
	{
		//sendChatSystemMsg(user,0,NULL,"AdminSilenceAll");
		return 0;
	}
	time = userSilencedTime(user);
	if(!time)
		return 1;
	//sendChatSystemMsg(user,0,NULL,"NotifySilenced %d",time);
	return 0;
}

void userLogin(ChatUser *user, ContainerID characterID, int access_level, U32 uChatServerID)
{
	// not handled on Shard Chat Server
}

void userLocalLogin(SA_PARAM_NN_VALID ChatUser *user)
{
	char *list = NULL;
	int i;

	if (user->pPlayerInfo && user->pPlayerInfo->bIsGM)
		userAddGM(user);
	for(i=eaSize(&user->watching)-1;i>=0;i--)
	{
		ChatChannel *chan = channelFindByID(user->watching[i]->channelID);
		if (chan) 
		{
			channelOnline(chan,user);
			channelSendOnline(chan, user);
		}
	}
	for (i=eaSize(&user->reserved)-1; i>=0; i--)
	{
		if (!channelFindByName(user->reserved[i]->name))
		{
			StructDestroy(parse_Watching, user->reserved[i]);
			eaRemove(&CONTAINER_NOCONST(ChatUser, user)->reserved, i);
		}
	}
}

void userLocalLogout(SA_PARAM_NN_VALID ChatUser *user)
{
	int i;
	for (i=eaSize(&user->watching)-1; i>=0; i--)
	{
		Watching * watching = user->watching[i];
		ChatChannel * chan = channelFindByName(watching->name);
		if (chan)
			channelSendOffline(chan, user);
	}
	// Logout of local reserved channels
	for(i=eaSize(&user->reserved)-1;i>=0;i--)
	{
		Watching * watching = user->reserved[i];
		ChatChannel * chan = channelFindByName(watching->name);
		channelLeave(user, watching->name, 0);
	}
	// clear online status and remove info
	if (user->pPlayerInfo)
	{
		if (user->pPlayerInfo->bIsGM)
			userRemoveGM(user);
		userCharacterChangeMap(&user->pPlayerInfo->playerMap, NULL);
		StructDestroy(parse_PlayerInfoStruct, user->pPlayerInfo);
		user->pPlayerInfo = NULL;
	}
	user->online_status = USERSTATUS_OFFLINE;
	userDelete(user->id);
}

void userLocalPlayerUpdate(SA_PARAM_NN_VALID ChatUser *user, SA_PARAM_NN_VALID const PlayerInfoStruct *pPlayerInfo, ChatUserUpdateEnum eForwardToGlobalFriends)
{
	if (user->uChatRelayID)
		RemoteCommand_crPlayerEntityUpdate(GLOBALTYPE_CHATRELAY, user->uChatRelayID, user->id, pPlayerInfo->onlineCharacterID);
	if (user->pPlayerInfo)
	{
		if (user->pPlayerInfo->bIsGM != pPlayerInfo->bIsGM)
		{
			if (pPlayerInfo->bIsGM)
				userAddGM(user);
			else
				userRemoveGM(user);
		}
		userCharacterChangeMap(&user->pPlayerInfo->playerMap, &pPlayerInfo->playerMap);
		StructCopyAll(parse_PlayerInfoStruct, pPlayerInfo, user->pPlayerInfo);
	}
	else
	{
		if (pPlayerInfo->bIsGM)
			userAddGM(user);
		userCharacterChangeMap(NULL, &pPlayerInfo->playerMap);
		user->pPlayerInfo = StructClone(parse_PlayerInfoStruct, pPlayerInfo);
	}
	if (eForwardToGlobalFriends >= CHATUSER_UPDATE_SHARD)
		userSendUpdateNotifications(user);

	if (pPlayerInfo->iPlayerTeam)
	{
		char teamChannel[128];
		team_MakeTeamChannelNameFromID(SAFESTR(teamChannel), pPlayerInfo->iPlayerTeam);
		channelJoinOrCreate_ShardOnly(user, pPlayerInfo->onlineCharacterID, teamChannel, CHANNEL_SPECIAL_TEAM);
	}
	if (pPlayerInfo->iPlayerGuild)
	{
		char guildChannel[128];
		guild_GetGuildChannelNameFromID(SAFESTR(guildChannel), pPlayerInfo->iPlayerGuild, pPlayerInfo->uVirtualShardID);
		channelJoinOrCreate_ShardOnly(user, pPlayerInfo->onlineCharacterID, guildChannel, CHANNEL_SPECIAL_GUILD);
		if (pPlayerInfo->bIsOfficer)
		{
			guild_GetOfficerChannelNameFromID(SAFESTR(guildChannel), pPlayerInfo->iPlayerGuild, pPlayerInfo->uVirtualShardID);
			channelJoinOrCreate_ShardOnly(user, pPlayerInfo->onlineCharacterID, guildChannel, CHANNEL_SPECIAL_OFFICER);
		}
	}
}

///////////////////////////////////////////
// Mailing Functions

static ChatMailStruct *createClientMailStruct(ChatUser *user, Email *email, bool bGetBody)
{
	ChatUser *from;
	ChatMailStruct *mail;

	if (!email)
		return NULL;
	from = userFindByContainerId(email->from);

	mail = StructCreate(parse_ChatMailStruct);
	mail->fromID = email->from;	// account from
	mail->uID = email->uID;
	mail->shardName = email->shardName;
	mail->bRead = email->bRead;
	mail->sent = email->sent;

	mail->subject = StructAllocString(email->subject);
	if (bGetBody)
		mail->body = StructAllocString(email->body);
	mail->uLotID = email->uLotID;

	mail->eTypeOfEmail = email->eTypeOfEmail;
	mail->iNPCEMailID = email->iNPCEMailID;
	mail->toContainerID = email->recipientContainerID;			// which character this is to

	switch(email->eTypeOfEmail)
	{
	case EMAIL_TYPE_PLAYER:
		{
			if(email->senderName)
			{
				mail->fromName = StructAllocString(email->senderName);
			}
			break;
		}
	case EMAIL_TYPE_NPC_NO_REPLY:
	case EMAIL_TYPE_NPC_FROM_PLAYER:
		{
			if(email->senderName)
			{
				mail->fromHandle = StructAllocString(email->senderName);
			}
			break;
		}
	}
	if (email->expireTime)
		mail->uTimeLeft = email->expireTime - timeSecondsSince2000();

	if(!mail->fromHandle && from)
	{
		mail->fromHandle = StructAllocString(from->handle);
	}
	return mail;
}

// deprecated - should eventually be removed. Kept around temporarily for backwards compatibility with old GCS
AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
int ChatServerForwardNewMail (ContainerID id, Email *email)
{
	ChatUser *user;
	ChatMailStruct *mail;
	user = userFindByContainerId(id);
	if (!userCharacterOnline(user))
		return CHATRETURN_USER_OFFLINE;
	if (eaIndexedFind(&CONTAINER_NOCONST(ChatUser, user)->email, email) == -1)
	{
		NOCONST(Email)* emailCopy = StructCloneDeConst(parse_Email, email);
		eaPush(&CONTAINER_NOCONST(ChatUser, user)->email, emailCopy);
	}
	mail = createClientMailStruct(user, email, true);
	if (mail)
		RemoteCommand_ServerChat_PushNewMail(GLOBALTYPE_ENTITYPLAYER, user->pPlayerInfo->onlineCharacterID,  
			user->pPlayerInfo->onlineCharacterID, mail);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
int ChatServerForwardSingleMail (ContainerID id, ChatMailStruct *mail)
{
	ChatUser *user;
	user = userFindByContainerId(id);
	if (!userCharacterOnline(user))
		return CHATRETURN_USER_OFFLINE;
	if (mail)
		RemoteCommand_ServerChat_PushNewMail(GLOBALTYPE_ENTITYPLAYER, user->pPlayerInfo->onlineCharacterID,  
			user->pPlayerInfo->onlineCharacterID, mail);
	return CHATRETURN_FWD_NONE;
}

U32 userGetContainerIDFrom(ChatUser *user)
{
	if(user && user->pPlayerInfo)
	{
		return user->pPlayerInfo->onlineCharacterID;
	}
	
	return 0;
}

AUTO_COMMAND_REMOTE;
int GetMailbox_ForwardFromGlobal(CmdContext *context, ChatMailList *mailList)
{
	ChatUser *user = userFindByContainerId(mailList->uID);
	if (userCharacterOnline(user))
	{
		RemoteCommand_gslMailNPC_GlobalChatSync(GLOBALTYPE_ENTITYPLAYER, user->pPlayerInfo->onlineCharacterID,
			user->pPlayerInfo->onlineCharacterID, mailList);
	}
	return CHATRETURN_NONE;
}

AUTO_COMMAND_REMOTE;
int GetNPCMail_ForwardFromGlobal(CmdContext *context, ChatMailList *mailList)
{
	ChatUser *user = userFindByContainerId(mailList->uID);
	if (userCharacterOnline(user))
	{
		RemoteCommand_gslMailNPC_GlobalChatSync(GLOBALTYPE_ENTITYPLAYER, user->pPlayerInfo->onlineCharacterID,
			user->pPlayerInfo->onlineCharacterID, mailList);
	}
	return CHATRETURN_NONE;
}

bool userChangeChatHandle(ContainerID userID, const char *userHandle)
{
	ChatUser *user = userFindByContainerId(userID);

	if (user && userHandle)
	{
		// This always succeeds, even if it causes a conflict
		// Account with existing handle will be removed from stash lookup on Chat Handles
		char *oldHandle = NULL;
		if (user->handle)
		{
			oldHandle = strdup(user->handle);
			stashRemovePointer(chat_db.user_names, user->handle, NULL);
		}
		strcpy_s((char*) user->handle, ARRAY_SIZE_CHECKED(user->handle), userHandle);
		objContainerMarkModified(objGetContainer(GLOBALTYPE_CHATUSER, userID));
		stashAddPointer(chat_db.user_names, user->handle, user, true); // Always overwrite new handles

		chatServerLogUserCommand(LOG_CHATSERVER, CHATCOMMAND_USER_HANDLE, NULL, user, NULL, 
			STACK_SPRINTF("Success - Changed handle from '%s' to '%s'", oldHandle, userHandle));
		if (oldHandle)
			free(oldHandle);
		return true;
	}
	chatServerLogUserCommand(LOG_CHATSERVER, CHATCOMMAND_USER_HANDLE, NULL, user, NULL, STACK_SPRINTF("Failed changing to '%s'", userHandle));
	return false;
}

bool userSetAccountName(ChatUser *user, const char *accountName)
{
	if (user && accountName)
	{
		if (!stashFindPointer(chat_db.account_names, accountName, NULL))
		{
			char *oldAccountName = NULL;
			if (user->accountName)
			{
				oldAccountName = strdup(user->accountName);
				stashRemovePointer(chat_db.account_names, user->accountName, NULL);
			}
			strcpy_s((char*) user->accountName, ARRAY_SIZE_CHECKED(user->accountName), accountName);
			objContainerMarkModified(objGetContainer(GLOBALTYPE_CHATUSER, user->id));
			stashAddPointer(chat_db.account_names, user->accountName, user, false);

			if (oldAccountName)
				free(oldAccountName);
			return true;
		}
	}
	chatServerLogUserCommand(LOG_CHATSERVER, CHATCOMMAND_USER_ACCOUNTNAME, NULL, user, NULL, STACK_SPRINTF("Failed to change to '%s'", accountName));
	return false;
}
					
AUTO_COMMAND ACMD_CATEGORY(ChatAdmin);
int userDelete(U32 uID)
{
	Container *con = objGetContainer(GLOBALTYPE_CHATUSER, uID);
	ChatUser *user= (ChatUser*) con->containerData;
	
	if (user)
	{
		GADRef *r = NULL;
		stashIntRemovePointer(chat_db.gad_by_id, user->id, &r);
		if(r)
			StructDestroy(parse_GADRef, r);
		stashRemovePointer(chat_db.account_names, user->accountName, NULL);
		stashRemovePointer(chat_db.user_names, user->handle, NULL);

		objRemoveContainerFromRepository(GLOBALTYPE_CHATUSER, user->id);
		return 1;
	}
	return 0;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void userBroadcastGlobalEx(ACMD_SENTENCE msg, S32 eNotifyType)
{
	RemoteCommand_BroadcastChatMessageToGameServers(GLOBALTYPE_CONTROLLER, 0, msg, eNotifyType);
	chatServerLogChannelCommand(CHATCOMMAND_GLOBAL_BROADCAST, NULL, NULL, msg);
}

AUTO_COMMAND ACMD_CATEGORY(ChatAdmin) ACMD_ACCESSLEVEL(4) ACMD_NAME(announce);
int userBroadcastGlobal(ACMD_SENTENCE msg)
{
	userBroadcastGlobalEx(msg, kNotifyType_ServerAnnounce);
	return 1;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void userSilenceByAccountName (U32 uUserID, const char *accountName, U32 uDuration)
{
	char *escapedAccountName = NULL;
	estrStackCreate(&escapedAccountName);
	estrAppendEscaped(&escapedAccountName, accountName);

	sendCmdAndParamsToGlobalChat("userSilenceByAccountName", "%d \"%s\" %d", uUserID, escapedAccountName, uDuration);
	estrDestroy(&escapedAccountName);
}
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void userSilenceByDisplayName(U32 uUserID, const char *displayName, U32 uDuration)
{
	char *escapedHandle = NULL;
	estrStackCreate(&escapedHandle);
	estrAppendEscaped(&escapedHandle, displayName);

	sendCmdAndParamsToGlobalChat("userSilenceByDisplayName", "%d \"%s\" %d", uUserID, escapedHandle, uDuration);
	estrDestroy(&escapedHandle);
}
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void userUnsilenceByDisplayName(U32 uUserID, const char *displayName)
{
	char *escapedHandle = NULL;
	estrStackCreate(&escapedHandle);
	estrAppendEscaped(&escapedHandle, displayName);

	sendCmdAndParamsToGlobalChat("userUnsilenceByDisplayName", "%d \"%s\"", uUserID, escapedHandle);
	estrDestroy(&escapedHandle);
}
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void userBanByHandle(U32 uID, ACMD_SENTENCE displayName)
{
	char *escapedHandle = NULL;
	estrStackCreate(&escapedHandle);
	estrAppendEscaped(&escapedHandle, displayName);

	sendCmdAndParamsToGlobalChat("userBanByHandle", "%d \"%s\"", uID, escapedHandle);
	estrDestroy(&escapedHandle);
}
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void userUnbanByHandle(U32 uID, ACMD_SENTENCE displayName)
{
	char *escapedHandle = NULL;
	estrStackCreate(&escapedHandle);
	estrAppendEscaped(&escapedHandle, displayName);

	sendCmdAndParamsToGlobalChat("userUnbanByHandle", "%d \"%s\"", uID, escapedHandle);
	estrDestroy(&escapedHandle);
}

AUTO_TRANSACTION ATR_LOCKS(user, ".id, .spyingOn") ATR_LOCKS(target, ".id, .spying");
enumTransactionOutcome trAddUserSpy(ATR_ARGS, NOCONST(ChatUser) *user, NOCONST(ChatUser) *target)
{
	if (eaiFind(&target->spying, user->id) == -1)
	{
		eaiPush(&target->spying, user->id);
	}
	if (eaiFind(&user->spyingOn, target->id) == -1)
	{
		eaiPush(&user->spyingOn, target->id);
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(user, ".id, .spyingOn") ATR_LOCKS(target, ".id, .spying");
enumTransactionOutcome trRemoveUserSpy(ATR_ARGS, NOCONST(ChatUser) *user, NOCONST(ChatUser) *target)
{
	eaiFindAndRemove(&target->spying, user->id);
	eaiFindAndRemove(&user->spyingOn, target->id);
	return TRANSACTION_OUTCOME_SUCCESS;
}

void userAddSpy(ChatUser *user, ChatUser *target)
{
	trAddUserSpy(NULL, NULL, CONTAINER_NOCONST(ChatUser, user), CONTAINER_NOCONST(ChatUser, target));
	objContainerMarkModified(objGetContainer(GLOBALTYPE_CHATUSER, target->id));
	objContainerMarkModified(objGetContainer(GLOBALTYPE_CHATUSER, user->id));
}

void userRemoveSpy(ChatUser *user, ChatUser *target)
{
	trRemoveUserSpy(NULL, NULL, CONTAINER_NOCONST(ChatUser, user), CONTAINER_NOCONST(ChatUser, target));
	objContainerMarkModified(objGetContainer(GLOBALTYPE_CHATUSER, user->id));
	objContainerMarkModified(objGetContainer(GLOBALTYPE_CHATUSER, target->id));
}

void userSendActivityStatus(SA_PARAM_NN_VALID ChatUser *user)
{
	PlayerStatusChange statusChange = {0};
	char *cmdString = NULL;
	char *statusString = NULL;

	statusChange.uAccountID = user->id;
	statusChange.eStatus = user->online_status;
	statusChange.eaiAccountIDs = CONTAINER_NOCONST(ChatUser, user)->friends;
	if (user->pPlayerInfo)
		statusChange.pActivity = user->pPlayerInfo->playerActivity;
	ChatPlayerInfo_SendActivityStringUpdate(&statusChange);
}

AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
int UserHidden(CmdContext *context, ContainerID userID)
{
	sendCommandToGlobalChatServer(context->commandString);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
int UserVisible(CmdContext *context, ContainerID userID)
{
	sendCommandToGlobalChatServer(context->commandString);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
int UserFriendsOnly(CmdContext *context, ContainerID userID)
{
	sendCommandToGlobalChatServer(context->commandString);
	return CHATRETURN_FWD_NONE;
}

// Forwards Auto AFK message to friends on the shard
AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
void UserAutoAFK(ContainerID userID)
{
	ChatUser *user = userFindByContainerId(userID);
	if (userCharacterOnline(user))
	{
		user->online_status |= USERSTATUS_AUTOAFK;
		userSendActivityStatus(user);
	}
}

// Clears Auto AFK message from friends on the shard
AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
void UserClearAutoAFK(ContainerID userID)
{
	ChatUser *user = userFindByContainerId(userID);
	if (userCharacterOnline(user))
	{
		user->online_status &= (~USERSTATUS_AUTOAFK);
		userSendActivityStatus(user);
	}
}

AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat) ACMD_IFDEF(GAMESERVER);
void UserChangeStatus(CmdContext *context, ContainerID userID, UserStatusChange eStatusChange, const char *msg)
{
	ChatUser *user = userFindByContainerId(userID);
	switch (eStatusChange)
	{
	case USERSTATUSCHANGE_AFK:
		sendCommandToGlobalChatServer(context->commandString);
		if (user && userChangeStatusAndActivity(user, USERSTATUS_AFK, USERSTATUS_DND, msg, true))
		{
			ANALYSIS_ASSUME(user);
			userSendActivityStatus(user);
		}
	xcase USERSTATUSCHANGE_DND:
		sendCommandToGlobalChatServer(context->commandString);
		if (user && userChangeStatusAndActivity(user, USERSTATUS_DND, USERSTATUS_AFK, msg, true))
		{
			ANALYSIS_ASSUME(user);
			userSendActivityStatus(user);
		}
	xcase USERSTATUSCHANGE_BACK:
		sendCommandToGlobalChatServer(context->commandString);
		if (user && userChangeStatusAndActivity(user, 0, USERSTATUS_AFK | USERSTATUS_DND, msg, true))
		{
			ANALYSIS_ASSUME(user);
			userSendActivityStatus(user);
		}
	xcase USERSTATUSCHANGE_AUTOAFK:
		UserAutoAFK(userID);
	xcase USERSTATUSCHANGE_REMOVE_AUTOAFK:
		UserClearAutoAFK(userID);
	}
}

AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
void UserSetStatusWithFlag(CmdContext *context, ContainerID userID, UserStatus status, const char *msg)
{
	sendCommandToGlobalChatServer(context->commandString);
}

AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
void UserSetStatus(ContainerID userID, const char *msg)
{
	char *escapedMsg = NULL;
	estrStackCreate(&escapedMsg);
	estrAppendEscaped(&escapedMsg, msg);
	sendCmdAndParamsToGlobalChat("UserSetStatus", "%d \"%s\"", userID, escapedMsg); 
	estrDestroy(&escapedMsg);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
int userSilencedEndTime(const char *accountName)
{
	ChatUser *user = userFindByAccountName(accountName);

	if (user)
	{
		if (userIsSilenced(user))
			return user->silenced;
		return 0;
	}
	Errorf("Could not find user with account name '%s'", accountName);
	return -1;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void ChatPlayerInfo_SendActivityStringUpdate(PlayerStatusChange *pStatusChange)
{
	EARRAY_OF(ChatRelayUserList) eaRelays = NULL;
	INT_EARRAY eaiAccountIDs;
	eaStackCreate(&eaRelays, ChatConfig_GetNumberOfChatRelays());
	eaIndexedEnable(&eaRelays, parse_ChatRelayUserList);
	
	eaiAccountIDs = pStatusChange->eaiAccountIDs;
	pStatusChange->eaiAccountIDs = NULL;
	EARRAY_INT_CONST_FOREACH_BEGIN(eaiAccountIDs, i, n);
	{
		ChatUser *user = userFindByContainerId(eaiAccountIDs[i]);
		if (user && user->uChatRelayID)
		{
			ANALYSIS_ASSUME(user);
			ChatServerAddToRelayUserList(&eaRelays, user);
		}
	}
	EARRAY_FOREACH_END;

	EARRAY_FOREACH_BEGIN(eaRelays, i);
	{
		pStatusChange->eaiAccountIDs = eaRelays[i]->eaiUsers; // temporarily set this here
		RemoteCommand_crReceiveBatchActivityUpdate(GLOBALTYPE_CHATRELAY, eaRelays[i]->uChatRelayID, pStatusChange);
	}
	EARRAY_FOREACH_END;
	eaDestroyStruct(&eaRelays, parse_ChatRelayUserList);
	pStatusChange->eaiAccountIDs = eaiAccountIDs; // Set this back so it gets properly freed by the cmd handler
}

// Returns whether or not the user should be prevented from sending to Local chat
bool userThrottleLocalSpam(SA_PARAM_NN_VALID ChatUser *user)
{
	U32 uTime;
	
	if (!gShardChatServerConfig.iLocalChatThrottlePeriod)
		return false; // Throttle disabled

	uTime = timeSecondsSince2000();
	if (!user->pPlayerInfo)
		return true;
	if (user->pPlayerInfo->bGoldSubscriber)
		return false;
	// Check if previous burst count has expired and should be reset
	if (!user->uLastLocalMessage || user->uLastLocalMessage + gShardChatServerConfig.iLocalChatThrottlePeriod < uTime)
	{
		// reset stats
		user->uLastLocalMessage = uTime;
		user->uMessageBufferUsed = 1;
		return false;
	}
	// Check burst limits; time is only updated for the first message in a burst in previous case
	if (user->uMessageBufferUsed < gShardChatServerConfig.iLocalChatBufferSize)
	{
		user->uMessageBufferUsed++;
		return false;
	}
	return true;
}

AUTO_COMMAND_REMOTE;
void userShardCharactersDeleted(CmdContext *context, U32 uAccountID, const char *shardName)
{
	sendCommandToGlobalChatServer(context->commandString);
}