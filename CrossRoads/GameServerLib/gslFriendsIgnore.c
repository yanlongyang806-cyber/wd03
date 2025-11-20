#include "gslChat.h"
#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "ChatData.h"
#include "EntityLib.h"
#include "EntityResolver.h"
#include "gslTransactions.h"
#include "GameStringFormat.h"
#include "StringUtil.h"
#include "gslSendToClient.h"
#include "NotifyCommon.h"
#include "GameAccountDataCommon.h"
#include "Player.h"
#include "GameServerLib.h"
#include "GamePermissionsCommon.h"

#include "AutoGen/ChatServer_autogen_remotefuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/ChatData_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "Player_h_ast.h"

typedef struct AddToListData
{
	EntityRef *pRef;
	ContainerID id;
	char *name;
	U32 listToAdd;
} AddToListData;

extern bool GameServer_RequireGlobalChat(Entity *pEnt);

/////////////////////////
// Chat Server Responses
/////////////////////////

#ifndef USE_CHATRELAY
static void gslChat_SendCallbackMsg(Entity *pEntity, NotifyType eNotifyType, const char *pchMessageKey, const char *pchRawHandle) {
	char *pchMessage = NULL;
	char *pchHandle = NULL;
	ChatData *pData = NULL;

	estrPrintf(&pchHandle, "@%s", pchRawHandle);
	entFormatGameMessageKey(pEntity, &pchMessage, pchMessageKey, STRFMT_STRING("User", pchRawHandle), STRFMT_END);

	pData = ChatData_CreatePlayerHandleDataFromMessage(pchMessage, pchHandle, false, false);
	notify_NotifySendWithData(pEntity, eNotifyType, pchMessage, NULL, NULL, NULL, pData);

	StructDestroy(parse_ChatData, pData); // Includes pLinkInfo & pLink
	estrDestroy(&pchMessage);
	estrDestroy(&pchHandle);
}
#endif

void gslChat_ForwardFriendsCallback(ContainerID entID, ChatPlayerStruct *pChatPlayer, FriendResponseEnum eType, const char *pchError)
{
	Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entID);

	if (pEntity)
	{
#ifndef USE_CHATRELAY
		if (pchError && *pchError) {
			notify_NotifySend(pEntity, kNotifyType_ChatFriendError, pchError, NULL, NULL);
		} else {
			switch (eType) {
				case FRIEND_UPDATED:
				case FRIEND_COMMENT:
				case FRIEND_NONE:
					break; // Ignore

				default: 
				if (pChatPlayer)
				{
					char *pchMessageKey = NULL;
					estrPrintf(&pchMessageKey, "FriendResponse.%s", FriendResponseEnumEnum[eType+1].key);
					gslChat_SendCallbackMsg(pEntity, kNotifyType_ChatFriendNotify, pchMessageKey, pChatPlayer->chatHandle);
					estrDestroy(&pchMessageKey);
				}
			}
		}
#endif

		// Synchronize the friends list
		if (pChatPlayer && pEntity->pPlayer && pEntity->pPlayer->pUI && pEntity->pPlayer->pUI->pChatState) {
			ChatState *pChatState = pEntity->pPlayer->pUI->pChatState;
			switch (eType) {
				case FRIEND_REQUEST_SENT:
				case FRIEND_REQUEST_ACCEPTED:
				case FRIEND_ADDED:
				case FRIEND_REQUEST_RECEIVED:
				case FRIEND_REQUEST_ACCEPT_RECEIVED:
				case FRIEND_UPDATED:
					ChatPlayer_AddToList(&pChatState->eaFriends, pChatPlayer, GAMESERVER_VSHARD_ID, false);
					break;

				case FRIEND_OFFLINE:
				case FRIEND_ONLINE: // These ALWAY overwrites the pPlayerInfo 
					ChatPlayer_AddToList(&pChatState->eaFriends, pChatPlayer, GAMESERVER_VSHARD_ID, true);
					break;

				case FRIEND_COMMENT:
					if (pChatPlayer)
					{
						int index = ChatPlayer_FindInList(&pChatState->eaFriends, pChatPlayer);
						if (index >= 0)
						{
							estrCopy2(&pChatState->eaFriends[index]->comment, pChatPlayer->comment);
						}
					}
					break;

				case FRIEND_REQUEST_REJECTED:
				case FRIEND_REMOVED:
				case FRIEND_REQUEST_REJECT_RECEIVED:
				case FRIEND_REMOVE_RECEIVED:
					ChatPlayer_RemoveFromList(&pChatState->eaFriends, pChatPlayer);
					break;

				case FRIEND_NONE:
					break; // do nothing

				default:
					devassertmsgf(0, "Unexpected FriendResponseEnum: %d", eType);
					break;
			}
		}

#ifndef USE_CHATRELAY
		if (eType == FRIEND_REQUEST_RECEIVED) {
			ClientCmd_cmdClientChat_ReceiveFriendRequest(pEntity, pChatPlayer);
		}
#endif
		if (pEntity->pPlayer && pEntity->pPlayer->pUI)
		{
			entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
			entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
		}
	}
}

void gslChat_ForwardIgnoreCallback(ContainerID entID, ChatPlayerStruct *pChatPlayer, IgnoreResponseEnum eType, const char *pchError)
{
	Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entID);

	if (pEntity)
	{
#ifndef USE_CHATRELAY
		if (pchError && *pchError) {
			notify_NotifySend(pEntity, kNotifyType_ChatIgnoreError, pchError, NULL, NULL);
		} else {
			switch (eType) {
				case IGNORE_NONE:
				case IGNORE_UPDATED:
					break; // Do nothing
				default:
				{
					char *pchMessageKey = NULL;
					estrPrintf(&pchMessageKey, "IgnoreResponse.%s", IgnoreResponseEnumEnum[eType+1].key);
					gslChat_SendCallbackMsg(pEntity, kNotifyType_ChatIgnoreNotify, pchMessageKey, pChatPlayer->chatHandle);
					estrDestroy(&pchMessageKey);
				}
			}
		}
#endif

		// Synchronize the ignores list
		if (pChatPlayer && pEntity->pPlayer && pEntity->pPlayer->pUI && pEntity->pPlayer->pUI->pChatState) {
			ChatState *pChatState = pEntity->pPlayer->pUI->pChatState;
			switch (eType) {
				case IGNORE_ADDED:
				case IGNORE_UPDATED:
					ChatPlayer_AddToList(&pChatState->eaIgnores, pChatPlayer, GAMESERVER_VSHARD_ID, true);
					break;

				case IGNORE_REMOVED:
					ChatPlayer_RemoveFromList(&pChatState->eaIgnores, pChatPlayer);
					break;

				case IGNORE_NONE:
					// do nothing
					break;

				default:
					devassertmsgf(0, "Unexpected IgnoreResponseEnum: %d", eType);
					break;
			}
		}
		if (pEntity->pPlayer && pEntity->pPlayer->pUI)
		{
			entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
			entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
		}
	}
}

/////////////////////////
// Client Commands
/////////////////////////

void gslChat_AddIgnore(Entity *pEnt, const ACMD_SENTENCE ignoreName)
{
	if(pEnt && pEnt->pPlayer && ignoreName && *ignoreName)
	{
		char *pchAccountName = NULL;
		U32 iAccountId;

		if (ResolveNameOrAccountIDNotify(pEnt, ignoreName, &pchAccountName, &iAccountId)) {
			if (iAccountId) {
				RemoteCommand_ChatServerAddIgnoreByID_Shard(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, iAccountId, false);
			} else {
				RemoteCommand_ChatServerAddIgnoreByHandle_Shard(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, pchAccountName, false);
			}
		}
		estrDestroy(&pchAccountName);
	}
}

//Ignore player and flag them as a spammer
void gslChat_AddIgnoreSpammer(Entity *pEnt, const ACMD_SENTENCE ignoreName)
{
	if(pEnt && pEnt->pPlayer && ignoreName && *ignoreName)
	{
		char *pchAccountName = NULL;
		U32 iAccountId;

		if (ResolveNameOrAccountIDNotify(pEnt, ignoreName, &pchAccountName, &iAccountId)) {
			if (iAccountId) {
				RemoteCommand_ChatServerAddIgnoreByID_Shard(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, iAccountId, true);
			} else {
				RemoteCommand_ChatServerAddIgnoreByHandle_Shard(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, pchAccountName, true);
			}
		}
		estrDestroy(&pchAccountName);
	}
}

#ifndef USE_CHATRELAY
//Set the chat whitelist
void gslChat_ToggleWhitelist(Entity *pEnt, U32 enabled)
{
	if(pEnt && pEnt->pPlayer)
	{
		RemoteCommand_ChatServerToggleWhitelist(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, enabled);
	}
}

//Set the tells whitelist
void gslChat_ToggleWhitelistTells(Entity *pEnt, U32 enabled)
{
	if(pEnt && pEnt->pPlayer)
	{
		RemoteCommand_ChatServerToggleWhitelistTells(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, enabled);
	}
}

//Set the emails whitelist
void gslChat_ToggleWhitelistEmails(Entity *pEnt, U32 enabled)
{
	if(pEnt && pEnt->pPlayer)
	{
		RemoteCommand_ChatServerToggleWhitelistEmails(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, enabled);
	}
}
#endif

void gslChat_AddIgnoreByAccountID(Entity *pEnt, U32 ignoreAccountID)
{
	RemoteCommand_ChatServerIgnoreByID_Shard(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, ignoreAccountID);
}

void gslChat_AddIgnoreByContainerID(Entity *pEnt, U32 ignoreContainerID)
{
	Entity *pIgnoreEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, ignoreContainerID);
	if (pIgnoreEnt && pIgnoreEnt->pPlayer)
		gslChat_AddIgnoreByAccountID(pEnt, pIgnoreEnt->pPlayer->accountID);
}

void gslChat_RemoveIgnore(Entity *pEnt, const ACMD_SENTENCE ignoreName)
{
	if(pEnt && pEnt->pPlayer && ignoreName && *ignoreName)
	{
		char *pchAccountName = NULL;
		U32 iAccountId;

		if (ResolveNameOrAccountIDNotify(pEnt, ignoreName, &pchAccountName, &iAccountId)) {
			if (iAccountId) {
				RemoteCommand_ChatServerRemoveIgnoreByID_Shard(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, iAccountId);
			} else {
				RemoteCommand_ChatServerRemoveIgnoreByHandle_Shard(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, pchAccountName);
			}
		}

		estrDestroy(&pchAccountName);
	}
}

//Adds a friend to your friends list using "<name>" "@<chathandle>" or "<name>@<chathandle>" syntax
void gslChat_AddFriend(Entity *pEnt, ACMD_SENTENCE friendHandleOrName)
{
	if
	(	pEnt 
		&& gamePermission_Enabled() 
		&& !GamePermission_EntHasToken(pEnt, GAME_PERMISSION_CAN_ADD_FRIEND))
	{
		notify_NotifySend(pEnt, kNotifyType_Failed, entTranslateMessageKey(pEnt, "Chat_NoTrialFriend"), NULL, NULL);
		return;
	}
	
	if(pEnt && pEnt->pPlayer && friendHandleOrName && *friendHandleOrName)
	{
		char *pchAccountName = NULL;
		U32 iAccountId;

		if (!GameServer_RequireGlobalChat(pEnt))
			return;

		if (ResolveNameOrAccountIDNotify(pEnt, friendHandleOrName, &pchAccountName, &iAccountId)) {
			if (iAccountId) {
				RemoteCommand_ChatServerAddFriendByID_Shard(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, iAccountId);
			} else {
				RemoteCommand_ChatServerAddFriendByHandle_Shard(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, pchAccountName);
			}
		}
		estrDestroy(&pchAccountName);
	}
}

void gslChat_AddFriendByAccountID(Entity *pEnt, U32 friendAccountID)
{
	if (!GameServer_RequireGlobalChat(pEnt))
		return;
	RemoteCommand_ChatServerAddFriendByID_Shard(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, friendAccountID);
}

void gslChat_AddFriendByContainerID(Entity *pEnt, U32 friendContainerID)
{
	Entity *pFriendEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, friendContainerID);
	if (!GameServer_RequireGlobalChat(pEnt))
		return;
	if (pFriendEnt && pFriendEnt->pPlayer)
		gslChat_AddFriendByAccountID(pEnt, pFriendEnt->pPlayer->accountID);
}

#ifndef USE_CHATRELAY
void gslChat_AddFriendCommentByAccountID(Entity *pEnt, U32 friendAccountID, ACMD_SENTENCE pcComment)
{
	int i;

	if (strlen(pcComment) > 256)
	{
		notify_NotifySend(pEnt, kNotifyType_Failed, entTranslateMessageKey(pEnt, "Friend_CommentTooLong"), NULL, NULL);
		return;
	}

	if (!GameServer_RequireGlobalChat(pEnt))
		return;
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI && pEnt->pPlayer->pUI->pChatState)
	{
		for (i = eaSize(&pEnt->pPlayer->pUI->pChatState->eaFriends)-1; i >= 0; --i)
		{
			if (pEnt->pPlayer->pUI->pChatState->eaFriends[i]->accountID == friendAccountID)
			{
				RemoteCommand_ChatServerAddFriendComment_Shard(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, friendAccountID, pcComment);
				break;
			}
		}
	}
}

//Adds a friend to your friends list using "<name>" "@<chathandle>" or "<name>@<chathandle>" syntax
void gslChat_AddFriendComment(Entity *pEnt, char *friendHandleOrName, ACMD_SENTENCE pcComment)
{
	if(pEnt && pEnt->pPlayer && friendHandleOrName && *friendHandleOrName)
	{
		char *pchAccountName = NULL;
		U32 iAccountId;

		if (ResolveNameOrAccountIDNotify(pEnt, friendHandleOrName, &pchAccountName, &iAccountId)) {
			if (iAccountId) {
				gslChat_AddFriendCommentByAccountID(pEnt, iAccountId, pcComment);
			} else if (pEnt->pPlayer->pUI && pEnt->pPlayer->pUI->pChatState && pchAccountName) {
				int i;
				for (i = eaSize(&pEnt->pPlayer->pUI->pChatState->eaFriends)-1; i >= 0; --i)
				{
					if (!pEnt->pPlayer->pUI->pChatState->eaFriends[i]->chatHandle) continue;
					if (!stricmp(pEnt->pPlayer->pUI->pChatState->eaFriends[i]->chatHandle,pchAccountName))
					{
						gslChat_AddFriendCommentByAccountID(pEnt, pEnt->pPlayer->pUI->pChatState->eaFriends[i]->accountID, pcComment);
						break;
					}
				}
			}
		}
		estrDestroy(&pchAccountName);
	}
}

void gslChat_AddFriendCommentByContainerID(Entity *pEnt, U32 friendContainerID, ACMD_SENTENCE pcComment)
{
	Entity *pFriendEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, friendContainerID);
	if (pFriendEnt && pFriendEnt->pPlayer)
		gslChat_AddFriendCommentByAccountID(pEnt, pFriendEnt->pPlayer->accountID, pcComment);
}
#endif

void gslChat_AcceptFriend(Entity *pEnt, ACMD_SENTENCE friendHandleOrName)
{
	if(pEnt && pEnt->pPlayer && friendHandleOrName && *friendHandleOrName)
	{
		char *pchAccountName = NULL;
		U32 iAccountId;

		if (!GameServer_RequireGlobalChat(pEnt))
			return;

		if (ResolveNameOrAccountIDNotify(pEnt, friendHandleOrName, &pchAccountName, &iAccountId)) {
			if (iAccountId) {
				RemoteCommand_ChatServerAcceptFriendByID_Shard(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, iAccountId);
			} else {
				RemoteCommand_ChatServerAcceptFriendByHandle_Shard(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, pchAccountName);
			}
		}
		estrDestroy(&pchAccountName);
	}
}

void gslChat_AcceptFriendByAccountID(Entity *pEnt, U32 friendAccountID)
{
	if (!GameServer_RequireGlobalChat(pEnt))
		return;
	RemoteCommand_ChatServerAcceptFriendByID_Shard(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, friendAccountID);
}

void gslChat_AcceptFriendByContainerID(Entity *pEnt, U32 friendContainerID)
{
	Entity *pFriendEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, friendContainerID);
	if (!GameServer_RequireGlobalChat(pEnt))
		return;
	if (pFriendEnt && pFriendEnt->pPlayer)
		gslChat_AcceptFriendByAccountID(pEnt, pFriendEnt->pPlayer->accountID);
}

void gslChat_RejectFriend(Entity *pEnt, ACMD_SENTENCE friendHandleOrName)
{
	if(pEnt && pEnt->pPlayer && friendHandleOrName && *friendHandleOrName)
	{
		char *pchAccountName = NULL;
		U32 iAccountId;

		if (!GameServer_RequireGlobalChat(pEnt))
			return;

		if (ResolveNameOrAccountIDNotify(pEnt, friendHandleOrName, &pchAccountName, &iAccountId)) {
			if (iAccountId) {
				RemoteCommand_ChatServerRejectFriendByID_Shard(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, iAccountId);
			} else {
				RemoteCommand_ChatServerRejectFriendByHandle_Shard(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, pchAccountName);
			}
		}
		estrDestroy(&pchAccountName);
	}
}

void gslChat_RejectFriendByAccountID(Entity *pEnt, U32 friendAccountID)
{
	if (!GameServer_RequireGlobalChat(pEnt))
		return;
	RemoteCommand_ChatServerRejectFriendByID_Shard(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, friendAccountID);
}

void gslChat_RejectFriendByContainerID(Entity *pEnt, U32 friendContainerID)
{
	Entity *pFriendEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, friendContainerID);
	if (!GameServer_RequireGlobalChat(pEnt))
		return;
	if (pFriendEnt && pFriendEnt->pPlayer)
		gslChat_RejectFriendByAccountID(pEnt, pFriendEnt->pPlayer->accountID);
}

void gslChat_RemoveFriend(Entity *pEnt, ACMD_SENTENCE friendHandleOrName)
{
	if(pEnt && pEnt->pPlayer && friendHandleOrName && *friendHandleOrName)
	{
		char *pchAccountName = NULL;
		U32 iAccountId;

		if (!GameServer_RequireGlobalChat(pEnt))
			return;

		if (ResolveNameOrAccountIDNotify(pEnt, friendHandleOrName, &pchAccountName, &iAccountId)) {
			if (iAccountId) {
				RemoteCommand_ChatServerRemoveFriendByID_Shard(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, iAccountId);
			} else {
				RemoteCommand_ChatServerRemoveFriendByHandle_Shard(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, pchAccountName);
			}
		}
		estrDestroy(&pchAccountName);
	}
}

void gslChat_RemoveFriendByAccountID(Entity *pEnt, U32 friendAccountID)
{
	if (!GameServer_RequireGlobalChat(pEnt))
		return;
	RemoteCommand_ChatServerRemoveFriendByID_Shard(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, friendAccountID);
}

void gslChat_RemoveFriendByContainerID(Entity *pEnt, U32 friendContainerID)
{
	Entity *pFriendEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, friendContainerID);
	if (pFriendEnt && pFriendEnt->pPlayer)
		gslChat_RemoveFriendByAccountID(pEnt, pFriendEnt->pPlayer->accountID);
}

void gslChat_RefreshFriendListReturn(U32 uID, ChatPlayerList *friendList)
{
	Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uID);
	if (friendList && friendList->chatAccounts && pEntity && pEntity->pPlayer && pEntity->pPlayer->pUI && pEntity->pPlayer->pUI) {	
		ChatState *pChatState = pEntity->pPlayer->pUI->pChatState;
		eaCopyStructs(&friendList->chatAccounts, &pChatState->eaFriends, parse_ChatPlayerStruct);
		eaQSort(pChatState->eaFriends, ComparePlayerStructs);
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

void gslChat_RefreshClientIgnoreListReturn(U32 uID, ChatPlayerList *ignoreList)
{
	Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uID);
	if (ignoreList && ignoreList->chatAccounts && pEntity && pEntity->pPlayer && pEntity->pPlayer->pUI && pEntity->pPlayer->pUI) {	
		ChatState *pChatState = pEntity->pPlayer->pUI->pChatState;
		eaCopyStructs(&ignoreList->chatAccounts, &pChatState->eaIgnores, parse_ChatPlayerStruct);
		eaQSort(pChatState->eaIgnores, ComparePlayerStructs);
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

void ServerChat_RefreshClientNameLists(Entity *pEnt)
{
	RemoteCommand_ChatServerGetIgnoreList(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID);
	RemoteCommand_ChatServerGetFriendsList(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID);
}