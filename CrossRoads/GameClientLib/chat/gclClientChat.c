#include "gclClientChat.h"

#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "ChatData.h"
#include "Entity.h"
#include "EntityResolver.h"
#include "GameStringFormat.h"
#include "GameAccountData/GameAccountData.h"
#include "gclChatChannelUI.h"
#include "gclChatConfig.h"
#include "gclEntity.h"
#include "gclFriendsIgnore.h"
#include "gclLogin.h"
#include "gclSendToServer.h"
#include "LoginCommon.h"
#include "NotifyCommon.h"
#include "Player.h"
#include "net.h"
#include "sndVoice.h"

#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/ChatData_h_ast.h"
#include "AutoGen/ChatRelay_autogen_GenericServerCmdWrappers.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// NOTE:
// All non-testing/debug commands run through the ChatRelay MUST be access level 0
// Commands through the ChatRelay cannot depend on there being an entActivePlayerPtr to retrieve the access level

ChatAuthData *g_pChatAuthData = NULL;

ChatState g_ChatState = {0};

AUTO_RUN;
void gclClientChat_Init(void)
{
	StructInit(parse_ChatState, &g_ChatState);
}

void gclClientChat_ClearChatState(void)
{
	// Clear this because the struct is about to get nuked by the StructReset
	ClientChat_ClearSelectedAdminChannel();
	StructReset(parse_ChatState, &g_ChatState);
}

ChatState *ClientChat_GetChatState(void)
{
#ifdef USE_CHATRELAY
	return &g_ChatState;
#else
	Entity *pEnt = entActivePlayerPtr();
	if (!pEnt || !pEnt->pPlayer || !pEnt->pPlayer->pUI)
		return NULL;
	return pEnt->pPlayer->pUI->pChatState;
#endif
}

U32 gclClientChat_GetAccountID(void)
{
#ifdef USE_CHATRELAY
	return g_pChatAuthData ? g_pChatAuthData->uAccountID : 0;
#else
	Entity *pEnt = entActivePlayerPtr();
	return pEnt ? entGetAccountID(pEnt) : 0;
#endif
}

const char *gclClientChat_GetAccountDisplayName(void)
{
#ifdef USE_CHATRELAY
	return g_pChatAuthData ? g_pChatAuthData->userLoginData.pDisplayName : NULL;
#else
	Entity *pEnt = entActivePlayerPtr();
	return pEnt ? pEnt->pPlayer->publicAccountName : NULL;
#endif
}

U32 gclClientChat_GetAccessLevel(void)
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt)
		return entGetAccessLevel(pEnt);
	else
#ifdef USE_CHATRELAY
		return g_pChatAuthData ? g_pChatAuthData->userLoginData.uAccessLevel : 0;
#else
		return 0;
#endif
}

AUTO_COMMAND ACMD_GENERICCLIENTCMD ACMD_IFDEF(CHATRELAY) ACMD_PRIVATE ACMD_HIDE ACMD_ACCESSLEVEL(0);
void gclChat_LoginSuccess(void)
{
	Entity *pEnt = entActivePlayerPtr();
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEnt);

	gclChatConnect_LoginDone();
	// PlayerInfo to Chat Server (from Game/Login Server)
	if (pEnt)
		ServerCmd_cmdServerChat_GameLogin();
	else
		gclLogin_PushPlayerUpdate();

	// Status updates
	if (pConfig)
	{
		if (pConfig->status & USERSTATUS_FRIENDSONLY)
			GServerCmd_crToggleFriendsOnlyStatus(GLOBALTYPE_CHATRELAY, true);
		else if (pConfig->status & USERSTATUS_HIDDEN)
			GServerCmd_crToggleHiddenStatus(GLOBALTYPE_CHATRELAY, true);
		GServerCmd_crSetStatusMessage(GLOBALTYPE_CHATRELAY, pConfig->pchStatusMessage);
	}
}

/////////////////////////////////////////
// Friends / Ignores

AUTO_COMMAND ACMD_GENERICCLIENTCMD ACMD_PRIVATE ACMD_HIDE ACMD_ACCESSLEVEL(0);
void gclChatCmd_ReceiveFriends(U32 uAccountID, ChatPlayerList *friendList)
{
	if (!uAccountID || gclClientChat_GetAccountID() != uAccountID)
		return;
	if (friendList && friendList->chatAccounts)
	{
		if (g_ChatState.eaFriends)
			eaDestroyStruct(&g_ChatState.eaFriends, parse_ChatPlayerStruct);
		eaCopyStructs(&friendList->chatAccounts, &g_ChatState.eaFriends, parse_ChatPlayerStruct);
		eaQSort(g_ChatState.eaFriends, ComparePlayerStructs);
	}
}

AUTO_COMMAND ACMD_GENERICCLIENTCMD ACMD_PRIVATE ACMD_HIDE ACMD_ACCESSLEVEL(0);
void gclChatCmd_ReceiveIgnores(U32 uAccountID, ChatPlayerList *ignoreList)
{
	if (!uAccountID || gclClientChat_GetAccountID() != uAccountID)
		return;
	if (ignoreList && ignoreList->chatAccounts)
	{
		if (g_ChatState.eaIgnores)
		{
			FOR_EACH_IN_EARRAY(g_ChatState.eaIgnores, ChatPlayerStruct, pChatPlayer)
			{
				svUserUpdateIgnore(pChatPlayer->accountID, false);
			}
			FOR_EACH_END

			eaDestroyStruct(&g_ChatState.eaIgnores, parse_ChatPlayerStruct);
		}
		eaCopyStructs(&ignoreList->chatAccounts, &g_ChatState.eaIgnores, parse_ChatPlayerStruct);
		eaQSort(g_ChatState.eaIgnores, ComparePlayerStructs);

		FOR_EACH_IN_EARRAY(g_ChatState.eaIgnores, ChatPlayerStruct, pChatPlayer)
		{
			svUserUpdateIgnore(pChatPlayer->accountID, true);
		}
		FOR_EACH_END
	}
}

AUTO_COMMAND ACMD_GENERICCLIENTCMD ACMD_PRIVATE ACMD_HIDE ACMD_ACCESSLEVEL(0);
void gclChatCmd_ReceiveFriendCB(U32 uAccountID, ChatPlayerStruct *pChatPlayer, FriendResponseEnum eType)
{
	Entity *pEnt = entActivePlayerPtr();
	U32 uVShardID = pEnt ? entGetVirtualShardID(pEnt) : 0;
	if (!uAccountID || gclClientChat_GetAccountID() != uAccountID)
		return;

	switch (eType) {
	case FRIEND_REQUEST_SENT:
	case FRIEND_REQUEST_ACCEPTED:
	case FRIEND_ADDED:
	case FRIEND_REQUEST_RECEIVED:
	case FRIEND_REQUEST_ACCEPT_RECEIVED:
	case FRIEND_UPDATED:
		{
			// Keep the existing comment if it exists
			int index = ChatPlayer_FindInList(&g_ChatState.eaFriends, pChatPlayer);
			if (index >= 0 && g_ChatState.eaFriends[index]->comment)
				estrCopy2(&pChatPlayer->comment, g_ChatState.eaFriends[index]->comment);
			ChatPlayer_AddToList(&g_ChatState.eaFriends, pChatPlayer, uVShardID, false);
		}
		break;
	case FRIEND_OFFLINE:
	case FRIEND_ONLINE: // These ALWAY overwrite the pPlayerInfo 
		ChatPlayer_AddToList(&g_ChatState.eaFriends, pChatPlayer, uVShardID, true);
		break;
	case FRIEND_COMMENT:
		{
			int index = ChatPlayer_FindInList(&g_ChatState.eaFriends, pChatPlayer);
			if (index >= 0)
				estrCopy2(&g_ChatState.eaFriends[index]->comment, pChatPlayer->comment);
		}
		break;
	case FRIEND_REQUEST_REJECTED:
	case FRIEND_REMOVED:
	case FRIEND_REQUEST_REJECT_RECEIVED:
	case FRIEND_REMOVE_RECEIVED:
		ChatPlayer_RemoveFromList(&g_ChatState.eaFriends, pChatPlayer);
		break;
	default:
		break;
	}
	if (eType == FRIEND_REQUEST_RECEIVED)
		ClientChat_ReceiveFriendRequest(pChatPlayer);
}

AUTO_COMMAND ACMD_GENERICCLIENTCMD ACMD_PRIVATE ACMD_HIDE ACMD_ACCESSLEVEL(0);
void gclChatCmd_ReceiveFriendActivityUpdate(PlayerStatusChange *pStatusChange)
{
	EARRAY_FOREACH_BEGIN(g_ChatState.eaFriends, i);
	{
		if (g_ChatState.eaFriends[i]->accountID == pStatusChange->uAccountID)
		{
			ChatPlayerStruct *pFriend = g_ChatState.eaFriends[i];			
			if (pStatusChange->uChatServerID == 0 || pStatusChange->uChatServerID == pFriend->pPlayerInfo.uChatServerID)
			{
				SAFE_FREE(pFriend->pPlayerInfo.playerActivity);
				if (pStatusChange->pActivity && *pStatusChange->pActivity)
					pFriend->pPlayerInfo.playerActivity = strdup(pStatusChange->pActivity);
			}
			pFriend->online_status = pStatusChange->eStatus;
			break;
		}
	}
	EARRAY_FOREACH_END;
}

AUTO_COMMAND ACMD_GENERICCLIENTCMD ACMD_PRIVATE ACMD_HIDE ACMD_ACCESSLEVEL(0);
void gclChatCmd_ReceiveIgnoreCB(U32 uAccountID, ChatPlayerStruct *pChatPlayer, IgnoreResponseEnum eType)
{
	Entity *pEnt = entActivePlayerPtr();
	U32 uVShardID = pEnt ? entGetVirtualShardID(pEnt) : 0;
	if (!uAccountID || gclClientChat_GetAccountID() != uAccountID)
		return;
	// Synchronize the ignores list
	switch (eType) {
	case IGNORE_ADDED:
	case IGNORE_UPDATED:
		ChatPlayer_AddToList(&g_ChatState.eaIgnores, pChatPlayer, uVShardID, true);
		svUserUpdateIgnore(pChatPlayer->accountID, true);
	xcase IGNORE_REMOVED:
		ChatPlayer_RemoveFromList(&g_ChatState.eaIgnores, pChatPlayer);
		svUserUpdateIgnore(pChatPlayer->accountID, false);
	xcase IGNORE_NONE:
		// do nothing
	xdefault:
		devassertmsgf(0, "Unexpected IgnoreResponseEnum: %d", eType);
		break;
	}
}

// Returns the pointer to the EArray of friends for the current user
EARRAY_OF(ChatPlayerStruct) *gclChat_GetFriends(void)
{
	if (gclChatIsConnected())
		return &g_ChatState.eaFriends;
	return NULL;
}
// Returns the pointer to the EArray of ignores for the current user
EARRAY_OF(ChatPlayerStruct) *gclChat_GetIgnores(void)
{
	if (gclChatIsConnected())
		return &g_ChatState.eaIgnores;
	return NULL;
}

#ifdef USE_CHATRELAY
static void gclChat_IgnoreInternal(const char *name, bool bSpammer, const char *spamMsg)
{
	char *pResolvedAccountName = NULL;
	U32 uResolvedAccountID = 0;
	if (ResolveNameOrAccountIDNotify(entActivePlayerPtr(), name, &pResolvedAccountName, &uResolvedAccountID))
	{
		GServerCmd_crAddIgnore(GLOBALTYPE_CHATRELAY, uResolvedAccountID, pResolvedAccountName, bSpammer, spamMsg);
	}
	estrDestroy(&pResolvedAccountName);
}
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(ignore) ACMD_CATEGORY(Social);
void gclChat_IgnoreByName(ACMD_SENTENCE name)
{
	gclChat_IgnoreInternal(name, false, NULL);
}
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(ignore_spammer) ACMD_CATEGORY(Social);
void gclChat_IgnoreSpammerByName(ACMD_SENTENCE name)
{
	gclChat_IgnoreInternal(name, true, NULL);
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_IgnoreSpamMessage);
void gclChat_IgnoreSpammerMessage(SA_PARAM_OP_VALID const char *name, SA_PARAM_OP_VALID const char *spamMsg)
{
	gclChat_IgnoreInternal(name, true, spamMsg);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(unignore, RemoveIgnore) ACMD_CATEGORY(Social);
void gclChat_UnignoreByName(ACMD_SENTENCE name)
{
	char *pResolvedAccountName = NULL;
	U32 uResolvedAccountID = 0;
	if (ResolveNameOrAccountIDNotify(entActivePlayerPtr(), name, &pResolvedAccountName, &uResolvedAccountID))
	{
		GServerCmd_crRemoveIgnore(GLOBALTYPE_CHATRELAY, uResolvedAccountID, pResolvedAccountName);
	}
	estrDestroy(&pResolvedAccountName);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(friend) ACMD_CATEGORY(Social);
void gclChat_FriendByName(ACMD_SENTENCE name)
{
	char *pResolvedAccountName = NULL;
	U32 uResolvedAccountID = 0;
	if (ResolveNameOrAccountIDNotify(entActivePlayerPtr(), name, &pResolvedAccountName, &uResolvedAccountID))
	{
		GServerCmd_crAddFriend(GLOBALTYPE_CHATRELAY, uResolvedAccountID, pResolvedAccountName, false);
	}
	estrDestroy(&pResolvedAccountName);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(FriendComment) ACMD_CATEGORY(Social);
void gclChat_AddFriendCommentByName(const char *name, ACMD_SENTENCE pComment)
{
	char *pResolvedAccountName = NULL;
	U32 uResolvedAccountID = 0;
	if (ResolveNameOrAccountIDNotify(entActivePlayerPtr(), name, &pResolvedAccountName, &uResolvedAccountID))
	{
		GServerCmd_crAddFriendComment(GLOBALTYPE_CHATRELAY, pResolvedAccountName, pComment);
	}
	estrDestroy(&pResolvedAccountName);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(AcceptFriend, Befriend) ACMD_CATEGORY(Social);
void gclChat_AcceptFriendByName(ACMD_SENTENCE name)
{
	char *pResolvedAccountName = NULL;
	U32 uResolvedAccountID = 0;
	if (ResolveNameOrAccountIDNotify(entActivePlayerPtr(), name, &pResolvedAccountName, &uResolvedAccountID))
	{
		GServerCmd_crAddFriend(GLOBALTYPE_CHATRELAY, uResolvedAccountID, pResolvedAccountName, true);
	}
	estrDestroy(&pResolvedAccountName);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(RejectFriend) ACMD_CATEGORY(Social);
void gclChat_RejectFriendByName(ACMD_SENTENCE name)
{
	char *pResolvedAccountName = NULL;
	U32 uResolvedAccountID = 0;
	if (ResolveNameOrAccountIDNotify(entActivePlayerPtr(), name, &pResolvedAccountName, &uResolvedAccountID))
	{
		GServerCmd_crRemoveFriend(GLOBALTYPE_CHATRELAY, uResolvedAccountID, pResolvedAccountName, true);
	}
	estrDestroy(&pResolvedAccountName);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(RemoveFriend, Unfriend) ACMD_CATEGORY(Social);
void gclChat_RemoveFriendByName(ACMD_SENTENCE name)
{
	char *pResolvedAccountName = NULL;
	U32 uResolvedAccountID = 0;
	if (ResolveNameOrAccountIDNotify(entActivePlayerPtr(), name, &pResolvedAccountName, &uResolvedAccountID))
	{
		GServerCmd_crRemoveFriend(GLOBALTYPE_CHATRELAY, uResolvedAccountID, pResolvedAccountName, false);
	}
	estrDestroy(&pResolvedAccountName);
}

//Toggles the whitelist for all chat.  If enabled, you will only receive messages from friends, SG members, and Team members
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(Whitelist_Chat) ACMD_CATEGORY(Social);
void gclChat_ToggleWhitelist(U32 enabled)
{
	GServerCmd_crToggleWhitelist(GLOBALTYPE_CHATRELAY, enabled);
}

//Toggles the whitelist for all chat.  If enabled, you will only receive tells from friends, SG members, and Team members
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(Whitelist_Tells) ACMD_CATEGORY(Social);
void gclChat_ToggleWhitelistTells(U32 enabled)
{
	GServerCmd_crToggleWhitelistTells(GLOBALTYPE_CHATRELAY, enabled);
}

//Toggles the whitelist for all chat.  If enabled, you will only receive emails from friends, SG members, and Team members
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(Whitelist_Emails) ACMD_CATEGORY(Social);
void gclChat_ToggleWhitelistEmails(U32 enabled)
{
	GServerCmd_crToggleWhitelistEmails(GLOBALTYPE_CHATRELAY, enabled);
}
#endif

// Channel Commands
#ifdef USE_CHATRELAY
AUTO_COMMAND ACMD_NAME(cinvite, chaninvite, channel_invite) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
void gclClientChat_ChannelInvite(const char *channel_name, ACMD_SENTENCE name)
{
	const char *pMyName = NULL;
	char *pResolvedAccountName = NULL;
	Entity *pEnt = entActivePlayerPtr();

	if (pEnt)
		pMyName = entGetPersistedName(pEnt);
	else
		pMyName = g_pChatAuthData->userLoginData.pDisplayName;
	if (ResolveNameOrAccountIDNotify(pEnt, name, &pResolvedAccountName, NULL))
		GServerCmd_crInviteChatHandleToChannel(GLOBALTYPE_CHATRELAY, channel_name, pMyName, pResolvedAccountName);
	estrDestroy(&pResolvedAccountName);
}

AUTO_COMMAND ACMD_NAME(motd,channel_motd) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
void gclClientChat_SetChannelMotd(const char *channel_name, ACMD_SENTENCE motd)
{
	GServerCmd_crSetChannelMotd(GLOBALTYPE_CHATRELAY, channel_name, motd);
}

AUTO_COMMAND ACMD_NAME(channel_description) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
void gclClientChat_SetChannelDescription(const char *channel_name, ACMD_SENTENCE description)
{
	GServerCmd_crSetChannelDescription(GLOBALTYPE_CHATRELAY, channel_name, description);
}

AUTO_COMMAND ACMD_NAME(caccess,chanaccess,channel_access) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
void gclClientChat_SetChannelAccess(const char *channel_name, char *accessString)
{
	GServerCmd_crSetChannelAccess(GLOBALTYPE_CHATRELAY, channel_name, accessString);
}

AUTO_COMMAND ACMD_NAME(clevel) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
void gclClientChat_ModifyChannelPermissions(const char *channel_name, ChannelUserLevel eLevel, U32 uPermissions)
{
	GServerCmd_crModifyChannelPermissions(GLOBALTYPE_CHATRELAY, channel_name, eLevel, uPermissions);
}

AUTO_COMMAND ACMD_NAME(chanpromote) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
void gclClientChat_ChannelPromoteUser(const char *channel_name, ACMD_SENTENCE targetHandle)
{
	GServerCmd_crPromoteInChannel(GLOBALTYPE_CHATRELAY, channel_name, targetHandle);
}

AUTO_COMMAND ACMD_NAME(chandemote) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
void gclClientChat_ChannelDemoteUser(const char *channel_name, ACMD_SENTENCE targetHandle)
{
	GServerCmd_crDemoteInChannel(GLOBALTYPE_CHATRELAY, channel_name, targetHandle);
}

AUTO_COMMAND ACMD_NAME(channel_kick) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
void gclClientChat_ChannelKickUser(const char *channel_name, ACMD_SENTENCE targetHandle)
{
	GServerCmd_crKickFromChannel(GLOBALTYPE_CHATRELAY, channel_name, targetHandle);
}

AUTO_COMMAND ACMD_NAME(mute) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
void gclClientChat_MuteUser(const char *channel_name, ACMD_SENTENCE targetHandle)
{
	GServerCmd_crMuteInChannel(GLOBALTYPE_CHATRELAY, channel_name, targetHandle);
}

AUTO_COMMAND ACMD_NAME(unmute) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
void gclClientChat_UnmuteUser(const char *channel_name, ACMD_SENTENCE targetHandle)
{
	GServerCmd_crUnmuteInChannel(GLOBALTYPE_CHATRELAY, channel_name, targetHandle);
}

AUTO_COMMAND ACMD_NAME(channel_destroy) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
void gclClientChat_DestroyChannel(ACMD_SENTENCE channel_name)
{
	GServerCmd_crDestroyChannel(GLOBALTYPE_CHATRELAY, channel_name);
}

AUTO_COMMAND ACMD_NAME(channel_uninvite) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
void gclClientChat_ChannelUninvite(const char *channel_name, ACMD_SENTENCE targetHandle)
{
	GServerCmd_crChannelUninvite(GLOBALTYPE_CHATRELAY, channel_name, targetHandle);
}

AUTO_COMMAND ACMD_NAME(channel_decline_invite) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
void gclClientChat_ChannelDeclineInvite(ACMD_SENTENCE channel_name)
{
	GServerCmd_crDeclineInviteToChannel(GLOBALTYPE_CHATRELAY, channel_name);
}
#endif
