#include "chatRelay.h"

#include "chatCommonStructs.h"
#include "chatdb.h"
#include "net.h"
#include "NotifyEnum.h"
#include "qsortG.h"
#include "ResourceInfo.h"
#include "StashTable.h"
#include "textparser.h"

#include "AutoGen/chatRelay_h_ast.h"
#include "AutoGen/ChatServer_autogen_RemoteFuncs.h"
#include "AutoGen/GameClientLib_autogen_GenericClientCmdWrappers.h"

static StashTable stUserStash = NULL;
AUTO_RUN;
void chatRelayInitUserTable(void)
{
	stUserStash = stashTableCreateInt(100);
	resRegisterDictionaryForStashTable("Chat Relay Users", RESCATEGORY_OTHER, 0, stUserStash, parse_ChatRelayUser);
}

AUTO_COMMAND ACMD_CATEGORY(ChatRelayDebug);
void chatRelay_PrintLinkStats(void)
{
	StashTableIterator iter = {0};
	StashElement elem;
	U64 uBytesSent = 0, uBytesReceived = 0;

	stashGetIterator(stUserStash, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		ChatRelayUser *user = stashElementGetPointer(elem);
		if (user && user->link)
		{
			const LinkStats *stats = linkStats(user->link);
			uBytesSent += stats->send.bytes;
			uBytesReceived += stats->recv.bytes;
		}
	}
	printf ("Bytes Sent [%"FORM_LL"u], Received [%"FORM_LL"u]\n", uBytesSent, uBytesReceived);
}

extern void crReceiveFriendCB(U32 uAccountID, ChatPlayerStruct *pChatPlayer, FriendResponseEnum eType, const char *pchNotifyMsg, bool bIsError);
AUTO_COMMAND ACMD_CATEGORY(ChatRelayDebug);
void chatRelay_SendPlayerInfoToAll(ChatRelayUser *relayUser, PlayerInfoStruct *pPlayer)
{
	StashTableIterator iter = {0};
	StashElement elem;
	U64 uBytesSent = 0, uBytesReceived = 0;
	ChatPlayerStruct *pChatPlayer = StructCreate(parse_ChatPlayerStruct);

	pChatPlayer->online_status = 1; // online
	estrCopy2(&pChatPlayer->comment, "PlayerInfoUpdateComment");
	StructCopyAll(parse_PlayerInfoStruct, pPlayer, &pChatPlayer->pPlayerInfo);
	pChatPlayer->accountID = relayUser->uAccountID;

	stashGetIterator(stUserStash, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		ChatRelayUser *user = stashElementGetPointer(elem);
		if (user && linkConnected(user->link) && user->bAuthed && user->uAccountID != relayUser->uAccountID)
		{
			crReceiveFriendCB(user->uAccountID, pChatPlayer, FRIEND_UPDATED, NULL, false);
		}
	}
	StructDestroy(parse_ChatPlayerStruct, pChatPlayer);
}

ChatRelayUser *chatRelayAddUser(U32 uAccountID, DefaultChatConfigSource eSource, NetLink *link)
{
	ChatRelayUser *user;
	if (!devassert(stUserStash))
		chatRelayInitUserTable();
	if (!stashIntFindPointer(stUserStash, uAccountID, &user))
	{
		user = StructCreate(parse_ChatRelayUser);
		user->uAccountID = uAccountID;
		if (eSource == CHAT_CONFIG_SOURCE_PC)
			user->uSource = (U32) CHAT_CONFIG_SOURCE_PC_ACCOUNT;
		else
			user->uSource = (U32) eSource;
		stashIntAddPointer(stUserStash, uAccountID, user, true);
	}
	else if (user->uLinkID && user->uLinkID != linkID(link))
	{
		// User is currently connected, kill the old connection
		NetLink *oldLink = linkFindByID(user->uLinkID);
		if (oldLink)
		{
			linkSetUserData(oldLink, NULL);
			linkRemove(&oldLink);
		}
		if (user->bAuthed) // and logged in
			RemoteCommand_crManager_DropUser(GLOBALTYPE_CHATSERVER, 0, GetAppGlobalID(), uAccountID);
		// Reset bAuthed state
		user->bAuthed = false;
	}
	linkSetUserData(link, user);
	user->uLinkID = linkID(link);
	user->link = link;
	return user;
}

ChatRelayUser *chatRelayGetUser(U32 uAccountID)
{
	ChatRelayUser *user = NULL;
	if (!stUserStash)
		return NULL;
	stashIntFindPointer(stUserStash, uAccountID, &user);
	return user;
}

void chatRelayRemoveUser(U32 uAccountID)
{
	// Removal of NetLink to client must be handled manually by caller of this
	ChatRelayUser *user = NULL;
	if (!stUserStash)
		return;
	stashIntRemovePointer(stUserStash, uAccountID, &user);
	if (user)
	{
		RemoteCommand_crManager_DropUser(GLOBALTYPE_CHATSERVER, 0, GetAppGlobalID(), uAccountID);
		StructDestroy(parse_ChatRelayUser, user);
	}
}

void chatRelayRemoveLink(ChatRelayUser *user)
{
	// Always lookup here to make sure link still exists
	NetLink *link = linkFindByID(user->uLinkID);
	if (link)
		linkRemove(&link);
}

void chatRelayRemoveUnauthenticatedUsers(void)
{
	StashTableIterator iter = {0};
	StashElement elem;
	EARRAY_OF(ChatRelayUser) eaUsersToRemove = NULL;

	stashGetIterator(stUserStash, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		ChatRelayUser *user = stashElementGetPointer(elem);
		if (!user->bAuthed)
			eaPush(&eaUsersToRemove, user);
	}
	EARRAY_FOREACH_BEGIN(eaUsersToRemove, i);
	{
		chatRelayRemoveLink(eaUsersToRemove[i]);
		chatRelayRemoveUser(eaUsersToRemove[i]->uAccountID);
	}
	EARRAY_FOREACH_END;
}

void chatRelayGetUserIDList(INT_EARRAY *eaiList, bool bAuthenticatedOnly)
{

	StashTableIterator iter = {0};
	StashElement elem;
	EARRAY_OF(ChatRelayUser) eaUsersToRemove = NULL;

	stashGetIterator(stUserStash, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		ChatRelayUser *user = stashElementGetPointer(elem);

		if (user->bAuthed || !bAuthenticatedOnly)
			eaiPush(eaiList, user->uAccountID);
	}
}

// Custom ChatRelay implementation for NotifySend_ from NotifyCommon.h - supports everything but headshots
void chatRelay_NotifySend(ChatRelayUser *relayUser, NotifyType eType, const char *pchDisplayString, const char *pchLogicalString, const char *pchSound, const char *pchTexture, const ChatData *pChatData, const char * pchTag)
{
	if (!pchDisplayString || !*pchDisplayString)
		return;
	if (relayUser)
	{
		GClientCmd_NotifyChatSend(relayUser->uAccountID, eType, pchDisplayString, pchLogicalString, pchSound, pchTexture, pChatData, pchTag);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void chatRelay_ProcessGMUpdate(ChatGMUpdate eUpdate, ChatUserMinimalList *pList)
{
	switch (eUpdate)
	{
	case CHATGMUPDATE_FULL:
	xcase CHATGMUPDATE_ADD:
	xcase CHATGMUPDATE_REMOVE:
	xdefault:
		// does nothing
		devassert(0);
	}
}

#include "AutoGen/NotifyEnum_h_ast.c"
#include "AutoGen/chatRelay_h_ast.c"
