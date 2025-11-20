#include "ChatServer/chatShared.h"
#include "AutoGen\AppLocale_h_ast.h"
#include "users.h"
#include "users_c_ast.h"
#include "AutoGen\chatdb_h_ast.h"
#include "chatCommonStructs_h_ast.h"
#include "chatGlobal.h"
#include "chatGlobalConfig.h"
#include "chatGuild.h"
#include "channels.h"
#include "chatCommandStrings.h"
#include "chatUtilities.h"
#include "userPermissions.h"

#include <string.h>
#include "StringCache.h"
#include "StringUtil.h"
#include "ChatServer.h"
#include "msgsend.h"
#include "objContainerIO.h"
#include "objTransactions.h"
#include "friendsIgnore.h"
#include "shardnet.h"
#include "TextFilter.h"
#include "xmpp/XMPP_Gateway.h"
#include "xmpp/XMPP_Chat.h"
#include "xmpp/XMPP_ChatRoom.h"
#include "xmpp/XMPP_ChatUtils.h"
#include "xmpp/XMPP_Structs.h"
#include "utilitiesLib.h"

#include "AutoGen/GlobalChatServer_autotransactions_autogen_wrappers.h"
#include "AutoTransDefs.h"

extern bool gbGlobalChatResponse;
int g_silence_all;

static __forceinline void _userSendSingleUpdate(NetLink *localChatLink, U32 uChatLinkID, U32 uAccountID)
{
	U32 *piAccountIDs = NULL;
	// Send account update
	eaiPush(&piAccountIDs, uAccountID);
	sendAccountList(localChatLink, uChatLinkID, piAccountIDs);
	eaiDestroy(&piAccountIDs);
}

int userCanTalk(ChatUser *user)
{
	int time;
	if (g_silence_all)
	{
		return 0;
	}
	time = userSilencedTime(user);
	if(!time)
		return 1;
	return 0;
}

typedef struct UserCreateLoginStruct
{
	ContainerID characterID;
	U32 uChatLinkID;
	PlayerInfoStruct *pPlayer;
} UserCreateLoginStruct;

typedef struct UserCreateXMPPLoginStruct
{
	U32 uStateID;
	char *xmppID;
	char *resource;
} UserCreateXMPPLoginStruct;

void trCreateUserAndLogin_CB(TransactionReturnVal *returnVal, UserCreateLoginStruct *pLoginInfo)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		U32 uID = atoi(returnVal->pBaseReturnVals->returnString);
		ChatUser *user = userFindByContainerId(uID);
		if (user)
		{
			NetLink *localChatLink = GetLocalChatLink(pLoginInfo->uChatLinkID);
			stashAddPointer(chat_db.user_names, user->handle, user, true); // Always overwrite new handles
			stashAddPointer(chat_db.account_names, user->accountName, user, false);

			userLogin(user, pLoginInfo->characterID, pLoginInfo->uChatLinkID, pLoginInfo->characterID == 0);
			if (localChatLink && linkConnected(localChatLink))
			{
				char commandBuffer[256];
				// Send account update
				_userSendSingleUpdate(localChatLink, pLoginInfo->uChatLinkID, user->id);

				sprintf(commandBuffer, "ChatServerLogin %d %d \"%s\"", user->id, pLoginInfo->characterID, 
					user->handle);
				sendCommandToLink(localChatLink, commandBuffer);

				// Send player information.
				if (pLoginInfo->pPlayer)
				{
					userLoginPlayerUpdate(user, pLoginInfo->pPlayer, pLoginInfo->uChatLinkID);
					userSendUpdateNotifications(user, pLoginInfo->uChatLinkID);
					ChatServerForwardWhiteListInfo(user);
				}
			}
		}
	}
	if (pLoginInfo->pPlayer)
		StructDestroy(parse_PlayerInfoStruct, pLoginInfo->pPlayer);
	free(pLoginInfo);
}

void trCreateUserAndXMPPLogin_CB(TransactionReturnVal *returnVal, UserCreateXMPPLoginStruct *pLoginInfo)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		U32 uID = atoi(returnVal->pBaseReturnVals->returnString);
		ChatUser *user = userFindByContainerId(uID);
		if (user)
		{
			XmppClientState *state = XmppServer_AddClientState(user, pLoginInfo->uStateID, pLoginInfo->resource);
			stashAddPointer(chat_db.user_names, user->handle, user, true); // Always overwrite new handles
			stashAddPointer(chat_db.account_names, user->accountName, user, false);

			XMPPChat_Login(user, state->resource, 0);
			sendCommandToLinkEx( XMPP_GetLink(), "XMPP_ProcessBindResponse", "%d %s \"%s\"",
				pLoginInfo->uStateID, pLoginInfo->xmppID, state->resource);
		}
	}
	else
		XMPP_SendStanzaError(XMPP_GetLink(), pLoginInfo->uStateID, StanzaError_Forbidden, Stanza_Auth, "iq", pLoginInfo->xmppID, "User not found");

	if (pLoginInfo->xmppID)
		free(pLoginInfo->xmppID);
	if (pLoginInfo->resource)
		free(pLoginInfo->resource);
	free(pLoginInfo);
}

void trAddUser_CB(TransactionReturnVal *returnVal, void *uData)
{
	U32 uChatLinkID = (U32)(intptr_t) uData;
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		U32 uID = atoi(returnVal->pBaseReturnVals->returnString);
		ChatUser *user = userFindByContainerId(uID);
		if (user)
		{
			NetLink *localChatLink = GetLocalChatLink(uChatLinkID);
			stashAddPointer(chat_db.user_names, user->handle, user, true); // Always overwrite new handles
			stashAddPointer(chat_db.account_names, user->accountName, user, false);

			if (localChatLink && linkConnected(localChatLink))
			{
				// Send account update
				_userSendSingleUpdate(localChatLink, uChatLinkID, user->id);
			}
		}
	}
}

static NOCONST(ChatUser) *userCreateNew(U32 uAccountID, const char *pAccountName, const char *pDisplayName, U32 uAccessLevel)
{
	NOCONST(ChatUser) * userNew = StructCreateNoConst(parse_ChatUser);

	userNew->id = uAccountID;
	if (pDisplayName && pDisplayName[0])
	{
		userNew->uHandleUpdateTime = timeSecondsSince2000();
		strcpy(userNew->handle, pDisplayName);
	}
	else
		sprintf(userNew->handle, "RandomUser%d", uAccountID);
	if (pAccountName && pAccountName[0])
		strcpy(userNew->accountName, pAccountName);
	userNew->access_level = uAccessLevel;

	return userNew;
}

// Changes the user visibility in channels to regular users - assumes that visibility IS changing
static void userModifyChannelMemberVisibility(SA_PARAM_NN_VALID ChatUser *user, bool bVisible)
{
	int iChange = bVisible ? 1 : -1;
	EARRAY_FOREACH_BEGIN(user->watching, i);
	{
		ChatChannel *channel = channelFindByID(user->watching[i]->channelID);
		if (channel && eaiFind(&channel->members, user->id) >= 0)
		{
			objRequestTransactionSimplef(NULL, GLOBALTYPE_CHATCHANNEL, channel->uKey, 
				"changeChannelMemberCount", "set uMemberCount = %d", channel->uMemberCount + iChange);
			if (eaiFind(&channel->online, user->id) >= 0)
				channel->uOnlineCount += iChange;
			sendChannelUpdate(channel);
		}
	}
	EARRAY_FOREACH_END;
}

static bool userSetAccessLevel(SA_PARAM_NN_VALID ChatUser *user, U32 uAccessLevel)
{
	if (user->access_level == uAccessLevel)
		return false;

	if (user->access_level < uAccessLevel)
	{
		if (!UserIsAdmin(user))
			userModifyChannelMemberVisibility(user, false);
	}
	else if (uAccessLevel == 0)
	{
		userModifyChannelMemberVisibility(user, true);
	}
	objRequestTransactionSimplef(NULL, GLOBALTYPE_CHATUSER, user->id, "userLoginSetAccessLevel", "set access_level = %d", uAccessLevel);
	return true;
}

static void userUpdate(NetLink *chatLink, U32 uChatID, SA_PARAM_NN_VALID ChatUser *user, SA_PARAM_NN_VALID const ChatLoginData *pLoginData)
{
	if (pLoginData->pDisplayName && *pLoginData->pDisplayName && stricmp (user->handle, pLoginData->pDisplayName))
		userChangeChatHandle(user->id, pLoginData->pDisplayName);
	if (pLoginData->pAccountName && *pLoginData->pAccountName && !user->accountName[0])
		userSetAccountName(user, pLoginData->pAccountName);

	if (eaSize(&user->ppPlayerInfo) > 0)
	{
		if (user->access_level < pLoginData->uAccessLevel)
			userSetAccessLevel(user, pLoginData->uAccessLevel);
	}
	else if (user->access_level != pLoginData->uAccessLevel)
	{
		userSetAccessLevel(user, pLoginData->uAccessLevel);
	}

	if (chatLink)
		_userSendSingleUpdate(chatLink, uChatID, user->id);
}


void userAddOrUpdate(const ChatLoginData *pLoginData)
{
	ChatUser *user = userFindByContainerId(pLoginData->uAccountID);
	NetLink *localChatLink = chatServerGetCommandLink();
	U32 uChatID = localChatLink ? GetLocalChatLinkID(localChatLink) : 0;

	if (user)
	{
		userUpdate(localChatLink, uChatID, user, pLoginData);
	}
	else
	{
		NOCONST(ChatUser) * userNew = userCreateNew(pLoginData->uAccountID, pLoginData->pAccountName, pLoginData->pDisplayName, 
			pLoginData->uAccessLevel);
		objRequestContainerCreateLocal(objCreateManagedReturnVal(trAddUser_CB, (void*)(intptr_t)uChatID), 
			GLOBALTYPE_CHATUSER, userNew);
	}
}

void userXmppCreateAndLogin(U32 uStateID, const char *xmppID, ContainerID uAccountID, const char *accountName, const char *handle, const char *resource, int access_level)
{
	ChatLoginData loginData = {0};
	ChatUser *user = userFindByContainerId(uAccountID);
	NetLink *xmppLink = XMPP_GetLink();

	if (!user)
	{
		user = userFindByAccountName(accountName);
		if (!user) // Account name is ALSO unknown - Create the user!
		{
			NOCONST(ChatUser) * userNew = userCreateNew(uAccountID, accountName, handle, access_level);
			UserCreateXMPPLoginStruct *pLoginInfo = malloc (sizeof(UserCreateLoginStruct));
			pLoginInfo->uStateID = uStateID;
			pLoginInfo->xmppID = strdup(xmppID);
			pLoginInfo->resource = nullStr(resource) ? NULL : strdup(resource);
			objRequestContainerCreateLocal(objCreateManagedReturnVal(trCreateUserAndXMPPLogin_CB, pLoginInfo), 
				GLOBALTYPE_CHATUSER, userNew);
		}
		else // Bad state! User with account name already exists, but does NOT have the same account ID!
		{
			XMPP_SendStanzaError(xmppLink, uStateID, StanzaError_Forbidden, Stanza_Auth, "iq", xmppID, "User not found");
			devassertmsgf(0, "Conflicting user tried to log in through XMPP - ID: %d, Name: %s, Handle: %s", 
				uAccountID, accountName, handle);
		}
		return;
	}
	loginData.uAccountID = user->id;
	loginData.pAccountName = (char*) user->accountName;
	loginData.pDisplayName = (char*) user->handle;
	loginData.uAccessLevel = access_level;
	userUpdate(NULL, 0, user, &loginData);

	if (userIsBanned(user))
	{   // Banned user, kick them off!
		sendCommandToLinkEx(xmppLink, "XMPP_LoginUnauthorized", "%d", uStateID);
	}
	else
	{
		XmppClientState *state = XmppServer_AddClientState(user, uStateID, resource);
		XMPPChat_Login(user, state->resource, access_level);
		sendCommandToLinkEx( xmppLink, "XMPP_ProcessBindResponse", "%d %s \"%s\"",
			uStateID, xmppID, state->resource);
	}
}

// Login a player.  Add the user if not recognized.
// If pPlayer is non-null, call userLoginPlayerUpdate() when complete.
int userAddAndLogin(ContainerID accountID, ContainerID characterID, const char *accountName, const char *displayName, int access_level,
					U32 uChatLinkID, const PlayerInfoStruct *pPlayer)
{
	ChatUser *user;
	user = userFindByContainerId(accountID);
	if (user)
	{
		NetLink *localChatLink = GetLocalChatLink(uChatLinkID);
		if (stricmp (user->handle, displayName))
			userChangeChatHandle(accountID, displayName);
		if (!user->accountName[0])
			userSetAccountName(user, accountName);
		userLogin(user, characterID, uChatLinkID, characterID == 0);
		if (localChatLink)
		{
			_userSendSingleUpdate(localChatLink, uChatLinkID, accountID);
		}
		if (pPlayer)
		{
			userLoginPlayerUpdate(user, pPlayer, uChatLinkID);
			userSendUpdateNotifications(user, uChatLinkID);
			ChatServerForwardWhiteListInfo(user);
		}
		return CHATRETURN_NONE;
	}
	else
	{
		NOCONST(ChatUser) * userNew = userCreateNew(accountID, accountName, displayName, access_level);
		UserCreateLoginStruct *pLoginInfo = malloc (sizeof(UserCreateLoginStruct));

		pLoginInfo->characterID = characterID;
		pLoginInfo->uChatLinkID = uChatLinkID;

		// Copy player info for sending after user is created.
		if (pPlayer)
			pLoginInfo->pPlayer = StructClone(parse_PlayerInfoStruct, pPlayer);
		else
			pLoginInfo->pPlayer = NULL;

		objRequestContainerCreateLocal(objCreateManagedReturnVal(trCreateUserAndLogin_CB, pLoginInfo), 
			GLOBALTYPE_CHATUSER, userNew);

		return CHATRETURN_FWD_NONE;
	}
}

void userLogin(ChatUser *user, ContainerID characterID, U32 uChatServerID, bool bXmppLogin)
{
	int		i;
	int		refresh = 1;

	if (!user)
		return;

	CONTAINER_NOCONST(ChatUser, user)->access &= ~CHANFLAGS_ADMIN; // TODO figure out how to set flags

	if(user->online_status == USERSTATUS_OFFLINE)
	{
		chat_db.online_count++;
		user->online_status |= USERSTATUS_ONLINE;
		user->bOnlineChange = true;
	}
	if (bXmppLogin)
	{
		user->online_status |= USERSTATUS_XMPP;
	}

	refresh = 0;

	for(i=eaSize(&user->watching)-1;i>=0;i--)
	{
		ChatChannel *chan = channelFindByID(user->watching[i]->channelID);
		if (chan)
		{
			channelOnline(chan,user);
			if (uChatServerID >= LOCAL_CHAT_ID_START)
				XMPPChat_NotifyChatOnline(chan, user);
		}
	}
	// TODO guild stuff for shard here?

	// Set this on login so it semi-updates even if logout never happens (eg. Chat Server dies)
	GlobalChatLoginUserByLinkID(user, uChatServerID);
	objRequestTransactionSimplef(NULL, GLOBALTYPE_CHATUSER, user->id, "userLoginSetLastOnline", "set last_online = %d", timeSecondsSince2000());
}

void userLoginOnly(ChatUser *user, const ChatLoginData *pLoginData, U32 uChatLinkID, bool bXmppUser)
{
	U32 uCharacterID = pLoginData->pPlayerInfo ? pLoginData->pPlayerInfo->onlineCharacterID : 0;
	NetLink *link = GetLocalChatLink(uChatLinkID);
	userLogin(user, uCharacterID, uChatLinkID, bXmppUser);
	if (pLoginData->pPlayerInfo)
		userLoginPlayerUpdate(user, pLoginData->pPlayerInfo, uChatLinkID);
	if (link)
		_userSendSingleUpdate(link, uChatLinkID, user->id);
	if (pLoginData->pPlayerInfo)
		userSendUpdateNotifications(user, uChatLinkID);
}

AUTO_TRANSACTION ATR_LOCKS(user, ".eaCharacterShards");
enumTransactionOutcome trUserAddShard(ATR_ARGS, NOCONST(ChatUser) *user, const char *shardName)
{
	const char *shardNamePool = allocAddString(shardName);
	eaPush(&user->eaCharacterShards, (char*) shardNamePool);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(user, ".eaCharacterShards");
enumTransactionOutcome trUserRemoveShard(ATR_ARGS, NOCONST(ChatUser) *user, const char *shardName)
{
	const char *shardNamePool = allocAddString(shardName);
	int idx = eaFind(&user->eaCharacterShards, (char*) shardNamePool);
	if (idx != -1)
		eaRemove(&user->eaCharacterShards, idx);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void userShardCharactersDeleted(U32 uAccountID, const char *shardName)
{
	ChatUser *user;
	const char *shardNamePooled;
	
	if (nullStr(shardName))
		return;
	user = userFindByContainerId(uAccountID);
	if (!user)
		return;

	shardNamePooled = allocAddString(shardName);
	if (eaFind(&user->eaCharacterShards, shardNamePooled) != -1)
		AutoTrans_trUserRemoveShard(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, shardNamePooled);
}

void userLoginPlayerUpdate(ChatUser *user, const PlayerInfoStruct *pPlayerInfo, U32 uChatServerID)
{
	if (uChatServerID)
	{
		bool bXmppGuildChange = false, bXmppOfficerChange = false;
		PlayerInfoStruct *pInfo = findPlayerInfoByLocalChatServerID(user, uChatServerID);

		if (pInfo)
		{
			if (pInfo->iPlayerGuild == 0)
			{
				bXmppGuildChange = pPlayerInfo->bCanGuildChat;
				bXmppOfficerChange = pPlayerInfo->bIsOfficer;
			}
			else 
			{
				if (pPlayerInfo->bCanGuildChat != pInfo->bCanGuildChat)
					bXmppGuildChange = true;
				if (pPlayerInfo->bIsOfficer != pInfo->bIsOfficer)
					bXmppOfficerChange = true;
			}

			StructCopyAll(parse_PlayerInfoStruct, pPlayerInfo, pInfo);
		}
		else
		{
			pInfo = StructCreate(parse_PlayerInfoStruct);
			StructCopyAll(parse_PlayerInfoStruct, pPlayerInfo, pInfo);
			eaPush(&user->ppPlayerInfo, pInfo);
		}
		pInfo->uChatServerID = uChatServerID;
		pInfo->uShardHash = GetShardHashById(uChatServerID);

		if (pPlayerInfo->shardName && pPlayerInfo->onlineCharacterID && eaFind(&user->eaCharacterShards, pPlayerInfo->shardName) == -1)
		{
			AutoTrans_trUserAddShard(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, pInfo->shardName);
		}

		if (bXmppGuildChange || bXmppOfficerChange)
		{
			ChatGuild *guild = GlobalChatFindGuild(uChatServerID, pInfo->iPlayerGuild);
			if (guild)
			{
				if (bXmppGuildChange)
					XMPPChat_NotifyGuildOnline(guild , user, false);
				if (bXmppOfficerChange)
					XMPPChat_NotifyGuildOnline(guild, user, true);
			}
		}
	}
	userSetLastLanguage(user, pPlayerInfo->eLanguage);
}

void userPlayerUpdateGuild(U32 uChatLinkID, ChatUser *user, ChatGuild *guild, ChatGuildMember *member)
{	
	PlayerInfoStruct *pInfo = findPlayerInfoByLocalChatServerID(user, uChatLinkID);
	NetLink *link;
	if (!pInfo)
		return;
	if (!devassert(member->uCharacterID == 0 || member->uCharacterID == pInfo->onlineCharacterID))
		return;
	pInfo->iPlayerGuild = guild ? guild->iGuildID : 0;
	if (guild)
	{
		pInfo->bCanGuildChat = member->bCanChat;
		pInfo->bIsOfficer = member->bIsOfficer;
		if (member->bCanChat)
			XMPPChat_NotifyGuildOnline(guild, user, false); 
		if (member->bIsOfficer)
			XMPPChat_NotifyGuildOnline(guild, user, true); 
	}
	else
		pInfo->bCanGuildChat = pInfo->bIsOfficer = false;
	link = GetLocalChatLink(uChatLinkID);
	if (link)
		_userSendSingleUpdate(link, uChatLinkID, user->id);
}

void userLogout(ChatUser *user, U32 uChatServerID)
{
	int		i;
	PlayerInfoStruct *shardInfo = NULL;
	bool bOffline = false;
	UserStatus oldStatus;
	U32 uOldShardHash = 0;

	if (!user)
		return;

	// Clean up various items
	if(userOnline(user))
	{
		GlobalChatLogoutUserByLinkID(user, uChatServerID);
		objRequestTransactionSimplef(NULL, GLOBALTYPE_CHATUSER, user->id, "userLogoutSetLastOnline", "set last_online = %d", timeSecondsSince2000());
	}

	oldStatus = user->online_status;
	shardInfo = findPlayerInfoByLocalChatServerID(user, uChatServerID);
	if (shardInfo)
	{
		if (shardInfo->onlinePlayerName)
			XMPPChat_RecvPresence(user, shardInfo->onlinePlayerName);
		if (shardInfo->uChatServerID == XMPP_CHAT_ID)
		{
			// Remove the XMPP flag
			user->online_status &= (~USERSTATUS_XMPP);
		}
		eaFindAndRemove(&user->ppPlayerInfo, shardInfo);
		uOldShardHash = shardInfo->uShardHash;
		StructDestroy(parse_PlayerInfoStruct, shardInfo);
	}
	bOffline = eaSize(&user->ppPlayerInfo) == 0; // Only completely offline if not on anywhere

	if (bOffline)
	{
		Watching **watchingCopy = NULL;
		for(i=eaSize(&user->watching)-1;i>=0;i--)
		{
			ChatChannel * chan = channelFindByID(user->watching[i]->channelID);
			if (chan)
			{
				channelOffline(chan,user);
				if (uChatServerID >= LOCAL_CHAT_ID_START)
					XMPPChatRoom_NotifyLeaveGame(user, XMPP_RoomDomain_Channel, chan);
					//XMPPChat_NotifyChatPart(chan, user, false);
			}
		}
		chat_db.online_count--;
		user->online_status = USERSTATUS_OFFLINE;
		userNotifyOnlineStatusChange(user, oldStatus);

		// Clear mail-read cache so it has a reset path if it gets out of sync somehow
		user->bMailReadInit = user->bHasUnreadMail = false;
	}
	else
	{
		if ((user->online_status & USERSTATUS_HIDDEN) == 0) // Not hidden
		{
			int j,k;
			for (j=eaiSize(&user->friends)-1; j>=0; j--)
			{
				ChatUser *target = userFindByContainerId(user->friends[j]);
				if (target)
				{
					for (k=eaSize(&target->ppPlayerInfo)-1; k>=0; k--)
					{
						if (target->ppPlayerInfo[k]->uChatServerID >= LOCAL_CHAT_ID_START)
						{
							ChatPlayerStruct *chatPlayer = userCreateChatPlayerStruct(user, target, FRIEND_FLAG_NONE, 
								target->ppPlayerInfo[k]->uChatServerID, true);
							if (uOldShardHash)
							{
								PlayerInfoStruct *dummyPInfo = StructCreate(parse_PlayerInfoStruct);
								dummyPInfo->uShardHash = uOldShardHash;
								eaPush(&chatPlayer->ppExtraPlayerInfo, dummyPInfo);
							}
							ChatServer_SendFriendUpdateSingleShard(target, chatPlayer, FRIEND_UPDATED, 
								target->ppPlayerInfo[k]->uChatServerID);
							StructDestroy(parse_ChatPlayerStruct, chatPlayer);
						}
					}
				}
			}
		}
	}
	if (uChatServerID >= LOCAL_CHAT_ID_START)
	{
		GlobalChatLinkUserGuilds *guildList = GlobalChatServer_UserGuildsByShard(user, uChatServerID);
		if (guildList)
		{
			int size;
			size = eaiSize(&guildList->guilds);
			for (i=0; i<size; i++)
			{
				ChatGuild *guild = GlobalChatFindGuild(uChatServerID, guildList->guilds[i]);
				if (guild)
					XMPPChatRoom_NotifyLeaveGame(user, XMPP_RoomDomain_Guild, guild);
			}
			size = eaiSize(&guildList->officerGuilds);
			for (i=0; i<size; i++)
			{
				ChatGuild *guild = GlobalChatFindGuild(uChatServerID, guildList->officerGuilds[i]);
				if (guild)
					XMPPChatRoom_NotifyLeaveGame(user, XMPP_RoomDomain_Officer, guild);
			}
		}
	}
	//friendStatus(user,0);
	// TODO notify guild chat leaves

	//update everyone (including XMPP clients) - does not update the shard chat server that was logged out
	sendUserUpdate(user);
}

void trCreateUser_CB(TransactionReturnVal *returnVal, CmdContext *context)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		U32 uID = atoi(returnVal->pBaseReturnVals->returnString);
		ChatUser *user = userFindByContainerId(uID);
		if (user)
		{
			stashAddPointer(chat_db.user_names, user->handle, user, true); // Always overwrite new handles
			stashAddPointer(chat_db.account_names, user->accountName, user, false);
			if (context && context->commandString)
			{
				gbGlobalChatResponse = true;
				ChatServerCmdParseFunc(context->commandString, NULL, context);
				gbGlobalChatResponse = false;
			}
		}
	}
	if (context)
	{
		if (context->commandString)
			free((char*) context->commandString);
		free(context);
	}
}

// Command Context is used for Global Chat Server and lazy account creation on Chat Handle resolution
ChatUser *userAdd(CmdContext *context, ContainerID id, const char *accountName, const char *chatHandle)
{
	NOCONST(ChatUser) * user = StructCreateNoConst(parse_ChatUser);

	user->id = id;
	if (chatHandle && chatHandle[0])
	{
		user->uHandleUpdateTime = timeSecondsSince2000();
		strcpy(user->handle, chatHandle);
	}
	else
		sprintf(user->handle, "RandomUser%d", id);
	if (accountName && accountName[0])
		strcpy(user->accountName, accountName);

	objRequestContainerCreateLocal(objCreateManagedReturnVal(trCreateUser_CB, context), GLOBALTYPE_CHATUSER, user);
	user = CONTAINER_NOCONST(ChatUser, userFindByHandle(chatHandle));
	return (ChatUser*) user;
}

///////////////////////////////////////////
// Mailing Functions

static void ChatServerLogLastMail (ChatUser *recipient)
{
	if (recipient)
	{
		int mailSize = eaSize(&recipient->email);
		Email *mail = mailSize > 0 ? recipient->email[mailSize-1] : NULL;
		if (mail && mail->eTypeOfEmail == EMAIL_TYPE_PLAYER)
		{
			ChatUser *sender = userFindByContainerId(mail->from);
			char *message = NULL;
			estrStackCreate(&message);
			ParserWriteText(&message, parse_Email, mail, 0, 0, 0);
			chatServerLogUserCommand(LOG_CHATMAIL, CHATCOMMAND_SENDMAIL, CHATCOMMAND_RECEIVEMAIL, sender, recipient, message);
			estrDestroy(&message);
		}
	}
}

void userOnlineUpdateMailbox(ChatUser *user, NetLink *chatLink)
{
	ChatMailList list = {0};
	char *listString = NULL;
	char *command = NULL;

	if (!chatLink)
		return; // does nothing
	userGetEmail(user, &list, true, false);

	ParserWriteTextEscaped(&listString, parse_ChatMailList, &list, 0, 0, 0);
	estrPrintf(&command, "ChatServerForwardMailFromGlobal %s", listString);
	sendCommandToLink(chatLink, command);
	estrDestroy(&command);
	estrDestroy(&listString);
	StructDeInit(parse_ChatMailList, &list);
}

void userOnlineUpdateMailboxPaged(ChatUser *user, NetLink *chatLink, int iPage, int iPageSize)
{
	ChatMailList list = {0};
	char *listString = NULL;
	char *command = NULL;

	if (!chatLink)
		return; // does nothing
	userGetEmailPaged(user, &list, iPageSize, iPage, 1, true, false); // Get in descending [time] order

	ParserWriteTextEscaped(&listString, parse_ChatMailList, &list, 0, 0, 0);
	estrPrintf(&command, "ChatServerForwardMailFromGlobal %s", listString);
	sendCommandToLink(chatLink, command);
	estrDestroy(&command);
	estrDestroy(&listString);
	StructDeInit(parse_ChatMailList, &list);
	// TODO fix 'future' mails to show up in the proper place!
}

AUTO_STRUCT;
typedef struct EmailTransactionData
{
	char *shardName;
	char *subject;
	char *body;
	U32 uLotID;
	U32 emailType;
	char *sendName;
	S64 npcEmailID;
	U32 futureSendTime;
} EmailTransactionData;

static EmailTransactionData *createEmailTransactionData(const char *shardName, const char *subject, const char *body, U32 uLotID, 
														U32 emailType, const char *sendName, S64 npcEmailID, U32 futureSendTime)
{
	EmailTransactionData *data = StructCreate(parse_EmailTransactionData);
	data->shardName = StructAllocString(shardName);
	data->subject = StructAllocString(subject);
	data->body = StructAllocString(body);
	data->uLotID = uLotID;
	data->emailType = emailType;
	data->sendName = StructAllocString(sendName);
	data->npcEmailID = npcEmailID;
	data->futureSendTime = futureSendTime;
	return data;
}

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
	{
		U32 uTime = timeSecondsSince2000();
		if (email->expireTime <= uTime)
		{
			StructDestroy(parse_ChatMailStruct, mail);
			return NULL;
		}
		mail->uTimeLeft = email->expireTime - uTime;
	}

	if(!mail->fromHandle && from)
	{
		mail->fromHandle = StructAllocString(from->handle);
	}
	return mail;
}

void ChatServerUpdateMailByID(ContainerID userID, U32 uMailID)
{
	ChatUser *user = userFindByContainerId(userID);
	char *mailString = NULL;
	char *commandString = NULL;
	if (user)
	{
		Email *email = eaIndexedGetUsingInt(&user->email, uMailID);

		if (!email)
			return;
		ParserWriteTextEscaped(&mailString, parse_Email, email, 0, 0, 0);

		estrConcatf(&commandString, "ChatServerForwardNewMail %d %s", user->id, mailString);
		sendCommandToUserLocal(user, commandString, NULL);
		estrDestroy(&commandString);
		estrDestroy(&mailString);
	}
	// This has a shard dependency At revision: 83563
	/*	ChatUser *user = userFindByContainerId(userID);
	if (user)
	{
		Email *email = eaIndexedGetUsingInt(&user->email, uMailID);
		ChatMailStruct *mail;

		if (!email)
			return;
		mail = createClientMailStruct(user, email, true);
		if (mail)
		{
			char *mailString = NULL;
			char *commandString = NULL;
			ParserWriteTextEscaped(&mailString, parse_ChatMailStruct, mail, 0, 0);
			estrConcatf(&commandString, "ChatServerForwardSingleMail %d %s", user->id, mailString);
			sendCommandToUserLocal(user, commandString, NULL);
			estrDestroy(&commandString);
			estrDestroy(&mailString);
			StructDestroy(parse_ChatMailStruct, mail);
		}
	}*/
}

void trUpdateMail_CB(TransactionReturnVal *returnVal, ContainerID *userID)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		U32 uMailID = atoi(objAutoTransactionGetResult(returnVal));
		ChatServerUpdateMailByID(*userID, uMailID);
		ChatServerLogLastMail(userFindByContainerId(*userID));
	}
	if (userID)
		free(userID);
}

AUTO_TRANS_HELPER;
void trAddMail_Helper(ATH_ARG NOCONST(ChatUser) *user, NOCONST(Email) *email)
{
	if (ChatServerIsMailTimeoutEnabled())
		email->expireTime = email->sent + ChatServerGetMailTimeout();
	email->uID = ++user->uLastEmailID;
	eaIndexedAdd(&user->email, email);
}

AUTO_TRANSACTION ATR_LOCKS(recipient, ".uLastEmailID, .email[AO]");
enumTransactionOutcome trAddMail(ATR_ARGS, NOCONST(ChatUser) *recipient, const ContainerID senderID, NON_CONTAINER EmailTransactionData *data)
{
	NOCONST(Email) *email = StructCreateNoConst(parse_Email);

	if(!email)
	{
		StructDestroy(parse_EmailTransactionData, data);
		return TRANSACTION_OUTCOME_FAILURE;
	}
	if (data->futureSendTime == 0 ||	data->emailType == EMAIL_TYPE_PLAYER)
		email->sent = timeSecondsSince2000();
	else
		email->sent = data->futureSendTime;
	email->shardName = allocAddString(data->shardName);
	email->subject = StructAllocString(data->subject);
	email->body = StructAllocString(data->body);
	email->uID = ++recipient->uLastEmailID;
	email->uLotID = data->uLotID;
	email->eTypeOfEmail = data->emailType;
	email->iNPCEMailID = (data->npcEmailID & 0x00000000ffffffff);
	if(NONNULL(data->sendName))
	{
		// add sender name to email (generic npc)
		email->senderName = StructAllocString(data->sendName);
	}
	email->from = senderID;

	switch(data->emailType)
	{
	case EMAIL_TYPE_NPC_FROM_PLAYER:
		{
			email->senderContainerID = (data->npcEmailID>>32);
			email->recipientContainerID = (data->npcEmailID>>32);
			break;
		}
	}

	trAddMail_Helper(recipient, email);
	TRANSACTION_RETURN_SUCCESS("%d", email->uID);
}

AUTO_TRANSACTION ATR_LOCKS(recipient, ".uLastEmailID, .email[AO]") ATR_LOCKS(sender, "id");
enumTransactionOutcome trImportMail(ATR_ARGS, NOCONST(ChatUser) *recipient, NOCONST(ChatUser) *sender, const char *shardName, const char *subject, const char *body, U32 uLotID, U32 sentOverride, short read)
{
	NOCONST(Email) *email = StructCreateNoConst(parse_Email);

	if (!email)
		return TRANSACTION_OUTCOME_FAILURE;
	if (sentOverride)
		email->sent = sentOverride;
	else
		email->sent = timeSecondsSince2000();
	if (ChatServerIsMailTimeoutEnabled())
		email->expireTime = email->sent + ChatServerGetMailTimeout();
	email->shardName = allocAddString(shardName);
	email->subject = StructAllocString(subject);
	email->body = StructAllocString(body);
	email->from = sender->id;
	email->uID = ++recipient->uLastEmailID;
	email->uLotID = uLotID;
	email->bRead = (bool) read;
	eaIndexedAdd(&recipient->email, email);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(XMLRPC, Mail);
int chatCommand_SendMail(const char *senderAccountName, const char *recipientDisplayName, const char *shardName, const char *subject, const char *body)
{
	ChatUser *sender, *recipient;
	int result;
	sender = userFindByAccountName(senderAccountName);
	recipient = userFindByHandle(recipientDisplayName);

	switch (result = userAddEmail(sender, recipient, shardName, subject, body, EMAIL_TYPE_PLAYER, NULL, 0, 0, true))
	{
	case CHATRETURN_UNSPECIFIED:
		Errorf("sender_not_found");
		return 0;
	xcase CHATRETURN_USER_DNE:
		Errorf("recipient_not_found");
		return 0;
	xcase CHATRETURN_USER_IGNORING:
		Errorf("recipient_ignoring_sender");
		return 0;
	xcase CHATRETURN_MAILBOX_FULL:
		Errorf("recipient_mailbox_full");
		return 0;
	xcase CHATRETURN_USER_PERMISSIONS:
		Errorf("sender_silenced");
		return 0;
	xdefault:
		return CHATRETURN_NONE;
	}
	return result;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(XMLRPC, Mail);
int chatCommand_SendSystemMail(const char *recipientAccountName, const char *shardName, const char *subject, const char *body)
{
	ChatUser *recipient = userFindByAccountName(recipientAccountName);
	int result;
	switch (result = userAddEmail(recipient, recipient, shardName, subject, body, EMAIL_TYPE_NPC_NO_REPLY, NULL, 0, 0, true))
	{
	case CHATRETURN_UNSPECIFIED:
		Errorf("sender_not_found");
		return 0;
	xcase CHATRETURN_USER_DNE:
		Errorf("recipient_not_found");
		return 0;
	xcase CHATRETURN_USER_IGNORING:
		Errorf("recipient_ignoring_sender");
		return 0;
	xdefault:
		return CHATRETURN_NONE;
	}
	return result;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(XMLRPC, Mail);
int chatCommand_ImportMail(const char *senderDisplayName, const char *recipientDisplayName, const char *shardName, const char *subject, const char *body, U32 sendDate, short isRead)
{
	ChatUser *sender, *recipient;
	int result;
	sender = userFindByHandle(senderDisplayName);
	recipient = userFindByHandle(recipientDisplayName);

	if (!recipient)
		recipient = userFindByAccountName(recipientDisplayName);
	if (!sender)
		sender = userFindByAccountName(senderDisplayName);

	switch (result = userImportEmail(sender, recipient, shardName, subject, body, sendDate, isRead))
	{
	case CHATRETURN_UNSPECIFIED:
		Errorf("sender_not_found");
		return 0;
		xcase CHATRETURN_USER_DNE:
		Errorf("recipient_not_found");
		return 0;
		xcase CHATRETURN_USER_IGNORING:
		Errorf("recipient_ignoring_sender");
		return 0;
xdefault:
		return CHATRETURN_NONE;
	}
	return result;
}

U32 userGetContainerIDFrom(ChatUser *user)
{
	if(user && user->pPlayerInfo)
	{
		return user->pPlayerInfo->onlineCharacterID;
	}

	return 0;
}

static bool userMailBoxIsFull(ChatUser *recipient)
{
	if (ChatServerGetMailLimit() == 0) // mail limit is disabled
		return false;
	// internal function, recipient has been checked
	if(eaSize(&recipient->email) > ChatServerGetMailLimit())
	{
		// count EMAIL_TYPE_PLAYER emails before returning false
		S32 i, iCount = 0, size =  eaSize(&recipient->email);
		for(i= 0; i < size; ++i)
		{
			if(recipient->email[i]->eTypeOfEmail == EMAIL_TYPE_PLAYER)		
			{
				++iCount;
				if(iCount >  ChatServerGetMailLimit())
				{
					return true;
				}
			}
		}
	}
	return false;
}

__forceinline static void userIncrementUnreadMailCounters(ChatUser *user, U32 uFutureTime)
{
	if (user->uUnreadCount >= 0)
		user->uUnreadCount++;
	if (!uFutureTime || uFutureTime <= timeSecondsSince2000())
		user->bHasUnreadMail = true;
	else if (!user->uNextFutureMail || uFutureTime < user->uNextFutureMail)
		user->uNextFutureMail = uFutureTime;
}

__forceinline static void userDecrementUnreadMailCounters(ChatUser *user)
{
	user->bHasUnreadMail = user->bMailReadInit = false;
	if (user->uUnreadCount > 0)
		user->uUnreadCount--;
}

// Deprecated - Function used for older shards. Use userAddEmailEx instead
int userAddEmail(ChatUser *sender, ChatUser *recipient, const char *shardName, const char *subject, const char *body,
				 EMailType emailType, const char *sendName, S64 npcEmailID, U32 futureSendTime, bool bRateLimit)
{
	EmailTransactionData *data;
	if (!sender)
		return CHATRETURN_UNSPECIFIED;
	if (!recipient)
		return CHATRETURN_USER_DNE;

	//If sender is sending messages too fast, silence them
	if(bRateLimit && sender->id != recipient->id && !UserIsAdmin(sender)) {
		if(mailRateLimiter(sender)) {
			return CHATRETURN_USER_PERMISSIONS;
		}
	}
	if (recipient->uFlags & CHATUSER_FLAG_BOT)
		return CHATRETURN_USER_DNE;

	if (userIsIgnoring(recipient, sender) || !isEmailWhitelisted(sender, recipient, chatServerGetCommandLinkID()))
	{
		return CHATRETURN_USER_IGNORING;
	}
	if (userIsSilenced(sender))
	{
		return CHATRETURN_USER_PERMISSIONS;
	}
	if(emailType == EMAIL_TYPE_PLAYER && !UserIsAdmin(sender) && userMailBoxIsFull(recipient))
	{
		return CHATRETURN_MAILBOX_FULL;
	}

	// prevent duplicate copies of NPC email (due to multiple syncs at near same time)
	if(emailType == EMAIL_TYPE_NPC_FROM_PLAYER)
	{
		S32 i;
		for(i = 0; i < eaSize(&recipient->email); ++i)
		{
			if(recipient->email[i]->eTypeOfEmail == EMAIL_TYPE_NPC_FROM_PLAYER && recipient->email[i]->iNPCEMailID == (npcEmailID & 0x00000000ffffffff) &&
				recipient->email[i]->senderContainerID == (npcEmailID>>32) )
			{
				return CHATRETURN_FWD_NONE;
			}
		}
	}
	else if(emailType == EMAIL_TYPE_PLAYER)
	{
		// pass in pointer to character name
		if(sender->pPlayerInfo && sender->pPlayerInfo->onlinePlayerName)
		{
			sendName = sender->pPlayerInfo->onlinePlayerName;
		}
	}

	data = createEmailTransactionData(shardName, subject, body, 0, emailType, sendName, npcEmailID, futureSendTime);
	{
		ContainerID *pID = malloc(sizeof(ContainerID));
		*pID = recipient->id;
		AutoTrans_trAddMail(objCreateManagedReturnVal(trUpdateMail_CB, pID), GLOBALTYPE_GLOBALCHATSERVER, 
			GLOBALTYPE_CHATUSER, recipient->id, sender->id, data);
		userIncrementUnreadMailCounters(recipient, futureSendTime);
	}
	StructDestroy(parse_EmailTransactionData, data);
	return CHATRETURN_FWD_NONE;
}

int userImportEmail(ChatUser *sender, ChatUser *recipient, const char *shardName, const char *subject, const char *body, U32 sendDate, short read)
{
	if (!sender)
		return CHATRETURN_UNSPECIFIED;
	if (!recipient)
		return CHATRETURN_USER_DNE;

	if (userIsIgnoring(recipient, sender))
	{
		return CHATRETURN_USER_IGNORING;
	}

	AutoTrans_trImportMail(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, recipient->id, GLOBALTYPE_CHATUSER, sender->id, shardName, subject, body, 0, sendDate, read);
	if (!read)
		userIncrementUnreadMailCounters(recipient, sendDate);
	return CHATRETURN_FWD_NONE;
}

// Function used by newer shards
int userAddEmailEx(ChatUser *sender, ChatUser *recipient, const char *shardName, ChatMailStruct *mail)
{
	S64 fullNPCID;
	EmailTransactionData *data;
	if (!sender)
		return CHATRETURN_UNSPECIFIED;
	if (!recipient)
		return CHATRETURN_USER_DNE;
	if (recipient->uFlags & CHATUSER_FLAG_BOT)
		return CHATRETURN_USER_DNE;

	//If sender is sending messages too fast, silence them
	if(sender->id != recipient->id && !UserIsAdmin(sender)) {
		if(mailRateLimiter(sender)) {
			return CHATRETURN_USER_PERMISSIONS;
		}
	}
	if (recipient->uFlags & CHATUSER_FLAG_BOT)
		return CHATRETURN_USER_DNE;
	if (userIsIgnoring(recipient, sender) || !isEmailWhitelisted(sender, recipient, chatServerGetCommandLinkID()))
	{
		return CHATRETURN_USER_IGNORING;
	}
	if (userIsSilenced(sender))
	{
		return CHATRETURN_USER_PERMISSIONS;
	}
	if (mail->eTypeOfEmail == EMAIL_TYPE_PLAYER && userMailBoxIsFull(recipient))
	{
		return CHATRETURN_MAILBOX_FULL;
	}

	if(mail->eTypeOfEmail == EMAIL_TYPE_NPC_FROM_PLAYER)
	{
		// prevent duplicate copies of NPC email (due to multiple syncs at near same time)
		EARRAY_FOREACH_BEGIN(recipient->email, i);
		{
			if (recipient->email[i]->eTypeOfEmail == EMAIL_TYPE_NPC_FROM_PLAYER && 
				recipient->email[i]->iNPCEMailID ==  mail->iNPCEMailID &&
				recipient->email[i]->senderContainerID == mail->toContainerID)
			{
				return CHATRETURN_FWD_NONE;
			}
		}
		EARRAY_FOREACH_END;
	}

	fullNPCID = ((S64)mail->iNPCEMailID) + (((S64)mail->toContainerID) << 32);
	data = createEmailTransactionData(shardName, mail->subject, mail->body, mail->uLotID, mail->eTypeOfEmail, mail->fromName, fullNPCID, mail->uFutureSendTime);
	{
		ContainerID *pID = malloc(sizeof(ContainerID));
		*pID = recipient->id;
		AutoTrans_trAddMail(objCreateManagedReturnVal(trUpdateMail_CB, pID), GLOBALTYPE_GLOBALCHATSERVER, 
			GLOBALTYPE_CHATUSER, recipient->id, sender->id, data);
		userIncrementUnreadMailCounters(recipient, mail->uFutureSendTime);
	}
	StructDestroy(parse_EmailTransactionData, data);
	return CHATRETURN_FWD_NONE;
}

AUTO_TRANSACTION ATR_LOCKS(user, ".email[]");
enumTransactionOutcome trDeleteMail(ATR_ARGS, NOCONST(ChatUser) *user, U32 uMailID)
{
	NOCONST(Email) *email = eaIndexedRemoveUsingInt(&user->email, uMailID);
	if (email)
	{
		StructDestroyNoConst(parse_Email, email);
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(XMLRPC, Mail);
int chatCommand_DeleteMail(const char *accountName, U32 uMailID)
{
	ChatUser *user = userFindByAccountName(accountName);
	if (!user)
	{
		Errorf("user_not_found");
		return 0;
	}
	userDeleteEmail(user, uMailID);
	chatServerLogUserCommand(LOG_CHATMAIL, CHATCOMMAND_DELETEMAIL, NULL, user, NULL, STACK_SPRINTF("%d", uMailID));
	return CHATRETURN_NONE;
}


int userDeleteEmail(ChatUser *user, U32 uMailID)
{
	NOCONST(Email) *email;
	if (!user)
		return CHATRETURN_UNSPECIFIED;
	email = eaIndexedGetUsingInt(&user->email, uMailID);
	if (email && !email->bRead)
		userDecrementUnreadMailCounters(user);
	AutoTrans_trDeleteMail(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, uMailID);
	return CHATRETURN_NONE;
}

AUTO_TRANSACTION ATR_LOCKS(user, ".email[]");
enumTransactionOutcome trReadMail(ATR_ARGS, NOCONST(ChatUser) *user, U32 uMailID, int iRead)
{
	NOCONST(Email) *email = eaIndexedGetUsingInt(&user->email, uMailID);
	if (email)
	{
		if (email->bRead == (bool) iRead)
			return TRANSACTION_OUTCOME_SUCCESS;
		email->bRead = iRead;
		if (email->bRead && ChatServerIsMailReadTimeoutReset())
			email->expireTime = timeSecondsSince2000() + ChatServerGetMailReadTimeout(); // Reset the expire time based on curtime
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(XMLRPC, Mail);
int chatCommand_MarkMailRead (const char *accountName, U32 uMailID, bool bRead)
{
	ChatUser *user = userFindByAccountName(accountName);
	if (!user)
	{
		Errorf("user_not_found");
		return 0;
	}
	userSetMailAsRead(user, uMailID, bRead);
	return CHATRETURN_NONE;
}

int userSetMailAsRead(ChatUser *user, U32 uMailID, bool bRead)
{
	NOCONST(Email) *email;
	if (!user)
		return CHATRETURN_UNSPECIFIED;
	email = eaIndexedGetUsingInt(&user->email, uMailID);
	if (email && !email->bRead)
		userDecrementUnreadMailCounters(user);
	AutoTrans_trReadMail(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, uMailID, bRead);
	return CHATRETURN_NONE;
}

// Get the mail list remotely
AUTO_COMMAND_REMOTE;
int chatCommandRemote_GetMailbox(CmdContext *context, int accountID)
{
	ChatMailList *list;
	ChatUser *user = userFindByContainerId(accountID);
	char *listString = NULL;
	char *command = NULL;

	if(!user)
	{
		return CHATRETURN_FWD_NONE;
	}

	list = StructCreate(parse_ChatMailList);
	userGetEmail(user, list, false, true);

	ParserWriteTextEscaped(&listString, parse_ChatMailList, list, 0, 0, 0);
	estrPrintf(&command, "GetMailbox_ForwardFromGlobal %s", listString);
	sendCommandToLink(chatServerGetCommandLink(), command);
	estrDestroy(&command);
	estrDestroy(&listString);

	StructDestroy(parse_ChatMailList, list);
	list = NULL;
	return CHATRETURN_FWD_NONE;
}

static int userGetNPCEmail(ChatUser *user, ChatMailList *list)
{
	int	i, count, size;
	count = 0;
	size = eaSize(&user->email);
	for(i=0;i<size;i++)
	{
		if (!user->email[i])
			break;
		switch(user->email[i]->eTypeOfEmail)
		{
		case EMAIL_TYPE_NPC_FROM_PLAYER:
			{
				ChatMailStruct *mail = createClientMailStruct(user, user->email[i], false);
				if (!mail) continue;
				eaPush(&list->mail, mail);
				count++;
			}
		}
	}
	list->uID = user->id;
	list->uTotalMail = count;
	return count;
}

// Get the NPC mails list remotely
AUTO_COMMAND_REMOTE;
int chatCommandRemote_GetNPCMail(CmdContext *context, int accountID)
{
	ChatMailList *list;
	ChatUser *user = userFindByContainerId(accountID);
	char *listString = NULL;
	char *command = NULL;

	if(!user)
	{
		return CHATRETURN_FWD_NONE;
	}

	list = StructCreate(parse_ChatMailList);
	userGetNPCEmail(user, list);

	ParserWriteTextEscaped(&listString, parse_ChatMailList, list, 0, 0, 0);
	estrPrintf(&command, "GetNPCMail_ForwardFromGlobal %s", listString);
	sendCommandToLink(chatServerGetCommandLink(), command);
	estrDestroy(&command);
	estrDestroy(&listString);

	StructDestroy(parse_ChatMailList, list);
	list = NULL;
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(XMLRPC, Mail);
int chatCommand_GetMailCount(const char *accountName)
{
	ChatUser *user = userFindByAccountName(accountName);

	if (!user)
	{
		Errorf("user_not_found");
		return -1;
	}
	return eaSize(&user->email);
}
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(XMLRPC, Mail);
int chatCommand_GetUnreadMailCount(const char *accountName)
{
	ChatUser *user = userFindByAccountName(accountName);
	if (!user)
	{
		Errorf("user_not_found");
		return -1;
	}
	if (user->uUnreadCount < 0)
	{
		int i;
		user->uUnreadCount = 0;
		for (i=eaSize(&user->email)-1; i>=0; i--)
		{
			if (!user->email[i]->bRead)
				user->uUnreadCount++;
		}
	}	
	return user->uUnreadCount;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(XMLRPC, Mail);
ChatMailList *chatCommand_GetMailboxOffset(CmdContext *context, const char *accountName, int iPageSize, int iPageOffset, int iOrder)
{
	ChatMailList *list;
	ChatUser *user = userFindByAccountName(accountName);

	if (!user)
	{
		Errorf("user_not_found");
		return NULL;
	}
	if (iPageSize < 1)
	{
		Errorf("invalid_page_size");
		return NULL;
	}
	list = StructCreate(parse_ChatMailList);
	userGetEmailPaged(user, list, iPageSize, iPageOffset, iOrder, false, false);
	return list;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(XMLRPC, Mail);
ChatMailList * chatCommand_GetMailbox(const char *accountName)
{
	ChatMailList *list;
	ChatUser *user = userFindByAccountName(accountName);

	if (!user)
	{
		Errorf("user_not_found");
		return NULL;
	}
	list = StructCreate(parse_ChatMailList);
	userGetEmail(user, list, false, false);
	return list;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(XMLRPC, Mail);
ChatMailStruct *chatCommand_GetMail(const char *accountName, U32 uMailID)
{
	ChatUser *user = userFindByAccountName(accountName);
	int size;
	if (!user)
	{
		Errorf("user_not_found");
		return NULL;
	}
	size = eaSize(&user->email);
	if (size)
	{
		Email *email = eaIndexedGetUsingInt(&user->email, uMailID);
		if (email)
		{
			ChatMailStruct *mail = createClientMailStruct(user, email, true);
			if (mail)
				return mail;
		}
	}
	Errorf("mail_not_found");
	return NULL;
}

static void userEmailCleanup(ChatUser *user)
{
	AutoTrans_trRemoveOldMails(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id);
}

// Run the mail cleanup code on the user
AUTO_COMMAND ACMD_CATEGORY(ChatAdmin);
void ChatMail_RunMailCleanup(U32 uID)
{
	ChatUser *user = userFindByContainerId(uID);
	if (!user)
		return;
	userEmailCleanup(user);
}

int userGetEmailPaged(ChatUser *user, ChatMailList *list, int iPageSize, int iPageOffset, int iOrder, bool bGetBody, bool getFuture)
{
	int	i, iStart, count, size;
	Email *email;
	U32 curTime = timeSecondsSince2000();

	count = 0;
	size = eaSize(&user->email);
	list->uID = user->id;
	list->uPage = iPageOffset;
	list->uPageSize = iPageSize;
	list->uTotalMail = iPageSize * iPageOffset;
	if (size == 0)
		return 0;
	if (iOrder)  // descending order, newest mail first
	{
		iStart = size - 1 - iPageSize * iPageOffset;
		for (i=iStart; i >= 0 /*&& i > iStart-iPageSize*/; i--)
		{
			email = user->email[i];
			if (!email)
				break;
			if(!getFuture)
			{  // Mail from NPCs can be in the future, dont send those emails to client
				switch(email->eTypeOfEmail)
				{
				case EMAIL_TYPE_NPC_NO_REPLY:
				case EMAIL_TYPE_NPC_FROM_PLAYER:
					{
						if(email->sent > curTime) continue;
						break;
					}
				}
			}
			if (count < iPageSize)
			{
				ChatMailStruct *mail = createClientMailStruct(user, user->email[i], bGetBody);
				if (!mail) continue;
				eaPush(&list->mail, mail);
				count++;
			}
			list->uTotalMail++;
		}
	}
	else
	{
		iStart = iPageOffset * iPageSize;
		for(i=iStart; i<size/* && i<iStart+iPageSize*/; i++)
		{
			email = user->email[i];
			if (!email)
				break;
			if(!getFuture)
			{  // Mail from NPCs can be in the future, dont send those emails to client
				switch(email->eTypeOfEmail)
				{
				case EMAIL_TYPE_NPC_NO_REPLY:
				case EMAIL_TYPE_NPC_FROM_PLAYER:
					{
						if(email->sent > curTime) continue;
						break;
					}
				}
			}
			if (count < iPageSize)
			{
				ChatMailStruct *mail = createClientMailStruct(user, user->email[i], bGetBody);
				if (!mail) continue;
				eaPush(&list->mail, mail);
				count++;
			}
			list->uTotalMail++;
		}
	}
	return count;
}

int userGetEmail(ChatUser *user, ChatMailList *list, bool bGetBody, bool getFuture)
{
	int	i, count, size;
	Email *email;
	U32 curTime = timeSecondsSince2000();

	count = 0;
	size = eaSize(&user->email);
	for(i=0;i<size;i++)
	{
		ChatMailStruct *mail;
		email = user->email[i];
		if (!email)
			break;
		if(!getFuture)
		{
			// Mail from NPCs can be in the future, dont send those emails to client
			switch(email->eTypeOfEmail)
			{
			case EMAIL_TYPE_NPC_NO_REPLY:
			case EMAIL_TYPE_NPC_FROM_PLAYER:
				{
					if(email->sent > curTime)
					{
						continue;
					}
					break;
				}
			}
		}
		mail = createClientMailStruct(user, user->email[i], bGetBody);
		if (!mail) continue;
		eaPush(&list->mail, mail);
		count++;
	}
	list->uID = user->id;
	list->uTotalMail = count;
	return count;
}

// Returns true if user has unread email
bool userCheckEmail(ChatUser *user, U32 curTime, bool getFuture)
{
	int	i, size;
	Email *email;

	user->uNextFutureMail = 0;
	size = eaSize(&user->email);
	for(i=0;i<size;i++)
	{
		email = user->email[i];
		if (!email)
			break;

		if(!getFuture)
		{
			// Mail from NPCs can be in the future, dont send those emails to client
			switch(email->eTypeOfEmail)
			{
			case EMAIL_TYPE_NPC_NO_REPLY:
			case EMAIL_TYPE_NPC_FROM_PLAYER:
				{
					if(email->sent > curTime)
					{
						if (!user->uNextFutureMail || email->sent < user->uNextFutureMail)
							user->uNextFutureMail = email->sent;
						continue;
					}
					// Player check is bugged and needs to be reworked
					break;
				}
			}
		}

		if(!email->bRead)
			return true;
	}
	return false;
}

bool userHasUnreadMail(ChatUser *user)
{
	U32 uTime;
	if (user->bHasUnreadMail)
		return true;
	uTime = timeSecondsSince2000();
	if (!user->bMailReadInit)
	{
		user->bHasUnreadMail = userCheckEmail(user, uTime, false);
		user->bMailReadInit = true;
	}
	else if (user->uNextFutureMail && uTime > user->uNextFutureMail)
	{
		user->bHasUnreadMail = userCheckEmail(user, uTime, false);
	}
	return user->bHasUnreadMail;
}

AUTO_TRANSACTION ATR_LOCKS(user, ".uHandleUpdateTime, .handle");
enumTransactionOutcome trChangeChatHandle(ATR_ARGS, NOCONST(ChatUser) *user, const char *userHandle)
{
	user->uHandleUpdateTime = timeSecondsSince2000();
	strcpy(user->handle, userHandle);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND ACMD_CATEGORY(ChatDebug);
void userChangeChatHandleTest(ContainerID userID, const char *userHandle)
{
	ChatUser *user = userFindByContainerId(userID);
	if (user)
	{
		if (userChangeChatHandle(userID, userHandle))
			sendUserUpdate(user);
	}
}

bool userChangeChatHandle(ContainerID userID, const char *userHandle)
{
	ChatUser *user = userFindByContainerId(userID);

	if (user && userHandle)
	{
		if (stricmp(userHandle, user->handle) == 0)
		{
			// Update time
			objRequestTransactionSimplef(NULL, GLOBALTYPE_CHATUSER, user->id, "userChangeChatHandleSetUpdateTime", "set uHandleUpdateTime = \"%d\"", timeSecondsSince2000());
			stashAddPointer(chat_db.user_names, user->handle, user, true); // Always overwrite new handles
			chatServerLogUserCommand(LOG_CHATSERVER, CHATCOMMAND_USER_HANDLE, NULL, user, NULL, 
				STACK_SPRINTF("Success - Handle was already '%s'", userHandle));
		}
		else
		{
			// This always succeeds, even if it causes a conflict
			// Account with existing handle will be removed from stash lookup on Chat Handles
			char *oldHandle = NULL;
			if (user->handle)
			{
				oldHandle = strdup(user->handle);
				stashRemovePointer(chat_db.user_names, user->handle, NULL);
			}
			AutoTrans_trChangeChatHandle(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, userHandle);
			stashAddPointer(chat_db.user_names, user->handle, user, true); // Always overwrite new handles

			chatServerLogUserCommand(LOG_CHATSERVER, CHATCOMMAND_USER_HANDLE, NULL, user, NULL, 
				STACK_SPRINTF("Success - Changed handle from '%s' to '%s'", oldHandle, userHandle));
			if (oldHandle)
				free(oldHandle);
			estrDestroy(&user->escapedHandle);
			return true;
		}
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
			objRequestTransactionSimplef(NULL, GLOBALTYPE_CHATUSER, user->id, "SetAccountName", "set accountName = \"%s\"", accountName);
			stashAddPointer(chat_db.account_names, user->accountName, user, false);

			chatServerLogUserCommand(LOG_CHATSERVER, CHATCOMMAND_USER_ACCOUNTNAME, NULL, user, NULL,
				STACK_SPRINTF("Success - Changed account name from '%s' to '%s'", oldAccountName, accountName));
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
		int i;
		for(i=eaSize(&user->watching)-1;i>=0;i--)
		{
			ChatChannel * chan = channelFindByID(user->watching[i]->channelID);
			if (chan)
				channelLeave(user, chan->name, 0);
		}

		if (user->friends) // Compiler doesn't like reverse for-loops and wants this check
		{
			for (i=eaiSize(&user->friends); i>=0; i--)
			{
				AutoTrans_trUserRemoveFriend(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, user->id, 
					GLOBALTYPE_CHATUSER, user->friends[i], user->friends[i]);
			}
		}
		if (user->befriend_reqs)
		{
			for (i=eaSize(&user->befriend_reqs); i>=0; i--)
			{
				AutoTrans_trUserRejectFriend(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, user->id,
					GLOBALTYPE_CHATUSER, user->befriend_reqs[i]->targetID, user->befriend_reqs[i]->targetID);
			}
		}
		if (user->befrienders)
		{
			for (i=eaSize(&user->befrienders); i>=0; i--)
			{
				AutoTrans_trUserRejectFriend(NULL, GLOBALTYPE_GLOBALCHATSERVER, 
					GLOBALTYPE_CHATUSER, user->befrienders[i]->userID, user->befrienders[i]->userID,
					GLOBALTYPE_CHATUSER, user->id, user->id);
			}
		}
		// TODO remove all friends and update users

		stashRemovePointer(chat_db.account_names, user->accountName, NULL);
		stashRemovePointer(chat_db.user_names, user->handle, NULL);
		objRemoveContainerFromRepository(GLOBALTYPE_CHATUSER, user->id);
		return 1;
	}
	return 0;
}

int userChangeStatus (ChatUser *user, UserStatus addStatus, UserStatus removeStatus, const char *msg, bool bSetStatusMessage)
{
	bool bChanged = false;
	if (!userOnline(user))
		return 0;
	if (msg && bSetStatusMessage)
		ReplaceAnyWordProfane((char *)msg); // evil cast
	if (addStatus && (addStatus & user->online_status) != addStatus)
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
		if (msg)
		{
			if (stricmp(msg, user->status))
			{
				estrCopy2(&user->status, msg);
				bChanged = true;
			}
		}
		else if (user->status && *user->status)
		{
			estrClear(&user->status);
			bChanged = true;
		}
	}
	return bChanged;
}

void userNotifyOnlineStatusChange(ChatUser *user, UserStatus oldStatus)
{
	bool bWasOnline = !!(oldStatus & USERSTATUS_ONLINE) && !(oldStatus & USERSTATUS_HIDDEN) && !user->bOnlineChange;
	bool bIsOnline = userOnline(user) && !(user->online_status & USERSTATUS_HIDDEN);

	user->bOnlineChange = false;
	// TODO login actions:
	// Online to channels
	// Online to friends + guild (no guild yet though)
	if (user && bWasOnline != bIsOnline)
	{
		int i;
		XMPPChat_RecvPresence(user, NULL);

		for (i=eaiSize(&user->friends)-1; i>=0; i--) {
			ChatUser *pTargetFriend = userFindByContainerId(user->friends[i]);
			if(userOnline(pTargetFriend))
				ChatServerUserFriendOnlineNotify(user, pTargetFriend, bIsOnline);
		}
	}
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
	AutoTrans_trAddUserSpy(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id,GLOBALTYPE_CHATUSER, target->id);
}

void userRemoveSpy(ChatUser *user, ChatUser *target)
{
	AutoTrans_trRemoveUserSpy(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id,GLOBALTYPE_CHATUSER, target->id);
}

void userSetLastLanguage(ChatUser *user, Language eLanguage)
{
	if (!user)
		return;
	if (user->eLastLanguage != eLanguage)
		objRequestTransactionSimplef(NULL, GLOBALTYPE_CHATUSER, user->id, "SetLastLanguage", "set eLastLanguage = %d", eLanguage);
}

AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
int UserHidden(ContainerID userID)
{
	ChatUser *user = userFindByContainerId(userID);
	if (userOnline(user))
	{
		UserStatus oldStatus = user->online_status;
		if (userChangeStatus(user, USERSTATUS_HIDDEN, USERSTATUS_FRIENDSONLY, NULL, false))
		{
			// TODO(Theo) make this not send double XMPP_RecvPresence updates by adding custom user status update push.
			sendUserUpdate(user);
			userNotifyOnlineStatusChange(user, oldStatus);
		}
	}
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
int UserVisible(ContainerID userID)
{
	ChatUser *user = userFindByContainerId(userID);
	if (userOnline(user))
	{
		UserStatus oldStatus = user->online_status;
		if (userChangeStatus(user, 0, USERSTATUS_HIDDEN | USERSTATUS_FRIENDSONLY, NULL, false))
		{
			sendUserUpdate(user);
			userNotifyOnlineStatusChange(user, oldStatus);
		}
	}
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
int UserFriendsOnly(ContainerID userID)
{
	ChatUser *user = userFindByContainerId(userID);
	if (userOnline(user))
	{
		UserStatus oldStatus = user->online_status;
		if (userChangeStatus(user, USERSTATUS_FRIENDSONLY, USERSTATUS_HIDDEN, NULL, false))
		{
			sendUserUpdate(user);
			userNotifyOnlineStatusChange(user, oldStatus);
		}
	}
	return CHATRETURN_FWD_NONE;
}

void UserAFKWithMessage(SA_PARAM_NN_VALID ChatUser *user, const char *msg)
{
	if (userChangeStatus(user, USERSTATUS_AFK, USERSTATUS_DND, msg, true))
	{
		sendUserUpdate(user);
		userSendUpdateNotifications(user, GetLocalChatLinkID(chatServerGetCommandLink()));
	}
}

// The status string updates are deprecated, as it is sent through another mechanism
AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
int UserAFK(ContainerID userID, const char *msg)
{
	ChatUser *user = userFindByContainerId(userID);
	if (user && userChangeStatus(user, USERSTATUS_AFK, USERSTATUS_DND, NULL, false))
	{
		ANALYSIS_ASSUME(user);
		sendUserUpdate(user);
		userSendUpdateNotifications(user, GetLocalChatLinkID(chatServerGetCommandLink()));
	}
	return CHATRETURN_FWD_NONE;
}

void UserDNDWithMessage(SA_PARAM_NN_VALID ChatUser *user, const char *msg)
{
	if (userChangeStatus(user, USERSTATUS_DND, USERSTATUS_AFK, msg, true))
	{
		sendUserUpdate(user);
		userSendUpdateNotifications(user, GetLocalChatLinkID(chatServerGetCommandLink()));
	}
}

AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
int UserDND(ContainerID userID, const char *msg)
{
	ChatUser *user = userFindByContainerId(userID);
	if (user && userChangeStatus(user, USERSTATUS_DND, USERSTATUS_AFK, NULL, false))
	{
		ANALYSIS_ASSUME(user);
		sendUserUpdate(user);
		userSendUpdateNotifications(user, GetLocalChatLinkID(chatServerGetCommandLink()));
	}
	return CHATRETURN_FWD_NONE;
}

void UserBackWithMessage(SA_PARAM_NN_VALID ChatUser *user, const char *msg)
{
	if (userChangeStatus(user, 0, USERSTATUS_DND | USERSTATUS_AFK, msg, true))
	{
		sendUserUpdate(user);
		userSendUpdateNotifications(user, GetLocalChatLinkID(chatServerGetCommandLink()));
	}
}

AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
int UserBack(ContainerID userID)
{
	ChatUser *user = userFindByContainerId(userID);
	if (user && userChangeStatus(user, 0, USERSTATUS_DND | USERSTATUS_AFK, NULL, false))
	{
		ANALYSIS_ASSUME(user);
		sendUserUpdate(user);
		userSendUpdateNotifications(user, GetLocalChatLinkID(chatServerGetCommandLink()));
	}
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat);
int UserSetStatus(ContainerID userID, const char *msg)
{
	ChatUser *user = userFindByContainerId(userID);
	if (user && userChangeStatus(user, 0, 0, msg, true))
	{
		ANALYSIS_ASSUME(user);
		sendUserUpdate(user);
		userSendUpdateNotifications(user, GetLocalChatLinkID(chatServerGetCommandLink()));
	}
	return CHATRETURN_FWD_NONE;
}

int userSendPrivateMessage(ChatUser *from, ChatUser *to, ChatMessage *pMsg, NetLink *fromLink)
{
	if (!pMsg) {
		return CHATRETURN_NONE;
	}

	if (!from) {
		return CHATRETURN_USER_DNE;
	}

	if (!ChatServerIsValidMessage(from, pMsg->pchText)) {
		return CHATRETURN_UNSPECIFIED;
	}

	// Only XMPP hasn't checked this yet
	if (!fromLink && userIsSilenced(from) && to && !UserIsAdmin(to)) 
	{
		return CHATRETURN_USER_PERMISSIONS;
	}

	// Fill in FROM & TO information as best as possible
	ChatFillUserInfo(&pMsg->pFrom, from);
	ChatFillUserInfo(&pMsg->pTo, to);

	if (!to || (to->uFlags & CHATUSER_FLAG_BOT) != 0 || 
		(to->online_status & USERSTATUS_DEAF && to->deafen_exceptionID != from->id) )
	{
		if (pMsg->pTo && pMsg->pTo->pchName)
		{
			sendTranslatedMessageToUser(from, kChatLogEntryType_System, PRIVATE_CHANNEL_NAME, "ChatServer_PlayerDNE", 
				STRFMT_STRING("User", pMsg->pTo->pchName), STRFMT_END);
		}
		else if (pMsg->pTo && pMsg->pTo->pchHandle)
		{
			sendTranslatedMessageToUser(from, kChatLogEntryType_System, PRIVATE_CHANNEL_NAME, "ChatServer_UserDNE", 
				STRFMT_STRING("User", pMsg->pTo->pchHandle), STRFMT_END);
		}
		else
		{
			sendTranslatedMessageToUser(from, kChatLogEntryType_System, PRIVATE_CHANNEL_NAME, "ChatServer_UnknownUser", STRFMT_END);
		}
		chatServerLogUserCommandWithReturnCode(LOG_CHATPRIVATE, CHATCOMMAND_PRIVATE_SEND, CHATCOMMAND_PRIVATE_RECEIVE, from, to, CHATRETURN_USER_DNE);

		return CHATRETURN_USER_DNE;
	} 
	else if (!userOnline(to))
	{
		sendTranslatedMessageToUser(from, kChatLogEntryType_System, PRIVATE_CHANNEL_NAME, "ChatServer_UserOffline", 
			STRFMT_STRING("User", to->handle), STRFMT_END);
		chatServerLogUserCommandWithReturnCode(LOG_CHATPRIVATE, CHATCOMMAND_PRIVATE_SEND, CHATCOMMAND_PRIVATE_RECEIVE, from, to, CHATRETURN_USER_OFFLINE);
		return CHATRETURN_USER_OFFLINE;
	}
	else if (userIsIgnoring(to, from) || !isWhitelisted(from, to, PRIVATE_CHANNEL_NAME, chatServerGetCommandLinkID()))
	{
		sendTranslatedMessageToUser(from, kChatLogEntryType_System, PRIVATE_CHANNEL_NAME, "ChatServer_BeingIgnored", 
			STRFMT_STRING("User", to->handle), STRFMT_END);
		chatServerLogUserCommandWithReturnCode(LOG_CHATPRIVATE, CHATCOMMAND_PRIVATE_SEND, CHATCOMMAND_PRIVATE_RECEIVE, from, to, CHATRETURN_USER_IGNORING);
		return CHATRETURN_USER_IGNORING;
	}
	else
	{
		char *buffer = NULL;
		char *encodedMsg = NULL;

		if (pMsg->pTo) {
			pMsg->pTo->accountID = to->id;
		}

		if (to->online_status & USERSTATUS_XMPP) // Send to XMPP and forward spying
		{
			ChatServerMessageSend(to->id, pMsg);
			forwardChatMessageToSpys(from, pMsg); // TODO (Theo) - fix spying so it always happens, but only ONCE
			forwardChatMessageToSpys(to, pMsg);
			chatServerLogUserCommand(LOG_CHATPRIVATE, CHATCOMMAND_PRIVATE_SEND, CHATCOMMAND_PRIVATE_RECEIVE, from, to, pMsg->pchText);
		}

		ChatCommon_EncodeMessage(&encodedMsg, pMsg);
		estrPrintf(&buffer, "ChatServerPrivateEncodedMessageFromGlobal %s", encodedMsg);
		sendCommandToUserLocal(to, buffer, fromLink);
		sendCommandToLink(fromLink, buffer); // always send back to from link so that the sender gets the receipt
		chatServerLogUserCommand(LOG_CHATPRIVATE, CHATCOMMAND_PRIVATE_SEND, CHATCOMMAND_PRIVATE_RECEIVE, from, to, encodedMsg);
		estrDestroy(&encodedMsg);
		estrDestroy(&buffer);
		return CHATRETURN_FWD_NONE;
	}
}

AUTO_TRANSACTION ATR_LOCKS(user, ".email[]");
enumTransactionOutcome trRemoveMailItem(ATR_ARGS, NOCONST(ChatUser) *user, int id)
{
	NOCONST(Email) *email = eaIndexedGetUsingInt(&user->email, id);
	if (email)
		email->uLotID = 0;
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(user, ".email");
enumTransactionOutcome trRemoveOldMails(ATR_ARGS, NOCONST(ChatUser) *user)
{
	int i;
	U32 uTime = timeSecondsSince2000();	
	U32 uTimeSinceSend;
	U32 uUnreadExpire = ChatServerGetMailTimeout();
	U32 uReadExpire = ChatServerGetMailReadTimeout();

	for (i=eaSize(&user->email)-1; i>=0; i--)
	{
		// Skip future mails
		if (uTime < user->email[i]->sent)
			continue;
		uTimeSinceSend = uTime - user->email[i]->sent;
		if (uUnreadExpire && uTimeSinceSend > uUnreadExpire || 
			uReadExpire && user->email[i]->bRead && uTimeSinceSend > uReadExpire)
			eaRemove(&user->email, i);
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(user, ".email");
enumTransactionOutcome trInitializeEmailExpirations(ATR_ARGS, NOCONST(ChatUser) *user)
{
	int i, size;
	U32 uTime = timeSecondsSince2000() + ChatServerGetMailReadTimeout();
	size = eaSize(&user->email);
	for (i=0; i<size; i++)
	{
		if (user->email[i]->bRead)
		{
			user->email[i]->expireTime = uTime;
		}
		else
		{
			user->email[i]->expireTime = user->email[i]->sent + ChatServerGetMailTimeout();
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(ChatServer);
void chatCommand_InitializeEmailExpirations(void)
{
	ContainerIterator iter = {0};
	ChatUser *user;
	objInitContainerIteratorFromType(GLOBALTYPE_CHATUSER, &iter);
	user = objGetNextObjectFromIterator(&iter);
	while (user)
	{
		AutoTrans_trInitializeEmailExpirations(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id);
		user = objGetNextObjectFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(ChatServer);
void chatCommand_RemoveOldMailByDay(int dayOfWeek)
{
	ContainerIterator iter = {0};
	ChatUser *user;

	dayOfWeek %= 7;
	objInitContainerIteratorFromType(GLOBALTYPE_CHATUSER, &iter);
	user = objGetNextObjectFromIterator(&iter);
	while (user)
	{
		if (user->id % 7 == dayOfWeek)
		{
			AutoTrans_trRemoveOldMails(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id);
		}
		user = objGetNextObjectFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(ChatServer);
void chatCommand_RemoveOldMail(void)
{
	struct tm timeStruct = {0};
	timeMakeLocalTimeStructFromSecondsSince2000(timeSecondsSince2000(), &timeStruct);
	chatCommand_RemoveOldMailByDay(timeStruct.tm_wday);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(ChatServer);
void chatCommand_RemoveOldMailItems (U32 uLastTime)
{
	ContainerIterator iter = {0};
	ChatUser *user;

	objInitContainerIteratorFromType(GLOBALTYPE_CHATUSER, &iter);
	user = objGetNextObjectFromIterator(&iter);
	while (user)
	{
		int i, size;
		size = eaSize(&user->email);
		for (i=0; i<size; i++)
		{
			if (user->email[i]->sent < uLastTime)
			{
				if (user->email[i]->uLotID)
				{
					AutoTrans_trRemoveMailItem(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, user->email[i]->uID);
				}
			}
			else
				break;
		}
		user = objGetNextObjectFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
}

static ChatMailStruct *returnedAuction_CreateMail(ChatUser *user, MailedAuctionLots *pAuctionLots, U32 uLotID)
{
	ChatMailStruct *pMail = StructCreate(parse_ChatMailStruct);
	pMail->eTypeOfEmail = EMAIL_TYPE_NPC_NO_REPLY;
	pMail->uLotID = uLotID;

	if (pAuctionLots->pReturnedSubject && *pAuctionLots->pReturnedSubject)
		pMail->subject = StructAllocString(pAuctionLots->pReturnedSubject);
	else
		pMail->subject = StructAllocString("Auction lot returned");

	if (pAuctionLots->pReturnedBody && *pAuctionLots->pReturnedBody)
		pMail->body = StructAllocString(pAuctionLots->pReturnedBody);
	else
		pMail->body = StructAllocString("Auction lot returned");

	if (pAuctionLots->pReturnedFrom && *pAuctionLots->pReturnedFrom)
		pMail->fromName = StructAllocString(pAuctionLots->pReturnedFrom);
	else
		pMail->fromName = StructAllocString("MailSystem:NoReply");

	pMail->sent = timeSecondsSince2000();
	pMail->fromHandle = StructAllocString(user->accountName);
	pMail->fromID = pAuctionLots->iOwnerAccountID;
	pMail->toContainerID = pAuctionLots->iRecipientEntityID;
	return pMail;
}

// A list of auction lots that should be included in mail to this account
// if not create a mail message
AUTO_COMMAND_REMOTE;
int chatCommandRemote_CheckMailAuctionLots(CmdContext *context, MailedAuctionLots *pAuctionLots)
{
	if(pAuctionLots && pAuctionLots->pchShardName)	
	{
		ChatMailList *list;
		ChatUser *user = userFindByContainerId(pAuctionLots->iOwnerAccountID);
		S32 i, j, iCount = 0;

		if(!user)
		{
			return CHATRETURN_FWD_NONE;
		}

		list = StructCreate(parse_ChatMailList);
		userGetEmail(user, list, false, true);

		// go through the mail and see if all of the mail lots are represented.
		// if not create them
		for(i = 0; i < eaSize(&list->mail); ++i)
		{
			if(list->mail[i]->uLotID != 0)
			{
				for(j = 0; j < eaiSize(&pAuctionLots->uLotIds); ++j)
				{
					if(pAuctionLots->uLotIds[j] == list->mail[i]->uLotID)
					{
						// we have this one
						pAuctionLots->uLotIds[j] = 0;
						++iCount;
						break;
					}
				}
			}
		}

		if(iCount < eaiSize(&pAuctionLots->uLotIds))
		{
			// auction lot without mail link
			for(j = 0; j < eaiSize(&pAuctionLots->uLotIds); ++j)
			{
				if(pAuctionLots->uLotIds[j] != 0)
				{
					// no mail for this lot, create one
					ChatMailStruct *pMail = returnedAuction_CreateMail(user, pAuctionLots, pAuctionLots->uLotIds[j]);
					userAddEmailEx(user, user, pAuctionLots->pchShardName, pMail);
					StructDestroy(parse_ChatMailStruct, pMail);
				}
			}
		}

		StructDestroy(parse_ChatMailList, list);
		list = NULL;
	}

	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE;
int ChatServerForceAccessLevel(U32 uID, U32 uAccessLevel)
{
	ChatUser *user = userFindByContainerId(uID);

	if (user)
	{
		ANALYSIS_ASSUME(user);
		if (userSetAccessLevel(user, uAccessLevel))
		{
			sendUserUpdate(user);
		}
	}

	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND ACMD_CATEGORY(ChatDebug);
void ForceSetChatAccessLevel(U32 uID, U32 uAccessLevel)
{
	ChatServerForceAccessLevel(uID, uAccessLevel);
}

// Returns the specified user's last language by two-letter language code
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(XMLRPC, ChatUser);
const char *chatCommand_GetUserLanguage(char *accountName)
{
	ChatUser *user = userFindByAccountName(accountName);
	int localeID;
	if (!user)
	{
		Errorf("user_not_found");
		return locGetCode(0);
	}
	localeID = locGetIDByLanguage(user->eLastLanguage);
	return locGetCode(localeID);
}

#include "users_c_ast.c"
