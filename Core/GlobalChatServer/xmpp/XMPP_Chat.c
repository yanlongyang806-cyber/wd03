// Interface between the bulk of the Global Chat Server and XMPP.
// It knows a lot about the GCS but not so much about XMPP.

#include "accountCommon.h"
#include "accountnet.h"
#include "chatCommon.h"

#include "AutoGen/XMPP_Chat_c_ast.h"

#include "aslChatServer.h"
#include "channels.h"
#include "chatGlobal.h"
#include "chatGuild.h"
#include "ChatServer/chatShared.h"
#include "friendsIgnore.h"
#include "users.h"
#include "userPermissions.h"

#include "EString.h"
#include "GlobalTypes.h"
#include "logging.h"
#include "qsortG.h"
#include "rand.h"
#include "ResourceInfo.h"
#include "shardnet.h"
#include "StashTable.h"
#include "timing.h"
#include "XMPP_Chat.h"
#include "XMPP_ChatUtils.h"
#include "XMPP_ChatRoom.h"
#include "XMPP_Gateway.h"

#include "ChatServer/xmppShared.h"

#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/XMPP_Structs_h_ast.h"
#include "AutoGen/xmppShared_h_ast.h"
#include "AutoGen/xmppTypes_h_ast.h"
#include "../common/autogen/globalchatserver_autotransactions_autogen_wrappers.h"
#include "chatdb_h_ast.h"

// Persistent XMPP settings for a ChatUser.
AST_PREFIX(PERSIST)
AUTO_STRUCT AST_CONTAINER;
typedef struct XMPPChat_Settings
{
	// List of roster extra roster items that are not also subscriptions.
	CONST_INT_EARRAY roster;
} XMPPChat_Settings;
AST_PREFIX()

// If true, only let clients list channels they are watching.
int giXmppOnlyListWatching = 0;
AUTO_CMD_INT(giXmppOnlyListWatching, XmppOnlyListWatching);

extern XmppServerState gXmppState;

// XMPP strings for the user interface
#define XMPP_MAP_NAME "Instant Messaging"
#define XMPP_MAP_NAME_MSGKEY "XMPP.CrypticTubesMapName"
#define XMPP_NEIGHBORHOOD_NAME_MSGKEY "XMPP.CrypticTubesNeighborhoodName"

// Replace spaces with '+'s.
void XMPPChat_EscapeSpaces(char *pString)
{
	char *i;
	for (i = pString; *i; ++i)
		if (*i == ' ')
			*i = '+';
}

// Replace '+'s with spaces.
void XMPPChat_DeescapeSpaces(char *pString)
{
	char *i;
	for (i = pString; *i; ++i)
		if (*i == '+')
			*i = ' ';
}

char* XMPPChat_GetEscapedHandle(ChatUser *user)
{
	if (!user->escapedHandle)
	{
		estrCopy2(&user->escapedHandle, user->handle);
		XMPPChat_EscapeSpaces(user->escapedHandle);
	}
	return user->escapedHandle;
}

// Get the contact list for an XMPP client.
// If xmppExtra is true, include XMPP roster items that are not actually presence subscriptions.
static void xmppchat_getroster(SA_PARAM_NN_VALID ChatUser *forUser, SA_PARAM_NN_VALID ChatUser ***roster, bool xmppExtra)
{
	ChatUser * friendUser = NULL;
	U32 *rosterExtra = NULL;
	ChatGuild **guilds = NULL;

	if (gbChatVerbose)
		printf("xmppchat_getroster for [%d]%s\n", forUser->id, forUser->accountName);
	// Clear output array.
	eaClear(roster);

	// Start with raw XMPP roster.
	if (xmppExtra && forUser->xmppSettings)
	{
		EARRAY_INT_CONST_FOREACH_BEGIN(forUser->xmppSettings->roster, i, n);
		{
			if (friendUser = userFindByContainerId(forUser->xmppSettings->roster[i]))
				eaPushUnique(roster, friendUser);
		}
		EARRAY_FOREACH_END;
	}

	// Add friends.
	EARRAY_INT_CONST_FOREACH_BEGIN(forUser->friends, i, n);
	{
		if (friendUser = userFindByContainerId(forUser->friends[i]))
			eaPushUnique(roster, friendUser);
	}
	EARRAY_FOREACH_END;

	// Add requested friends.
	EARRAY_CONST_FOREACH_BEGIN(forUser->befriend_reqs, i, s);
	{
		if (friendUser = userFindByContainerId(forUser->befriend_reqs[i]->targetID))
			eaPushUnique(roster, friendUser);
	}
	EARRAY_FOREACH_END;

	// Add guilds.
	GlobalChatServer_UserGuilds(&guilds, forUser);
	EARRAY_CONST_FOREACH_BEGIN(guilds, i, n);
	{
		EARRAY_INT_CONST_FOREACH_BEGIN(guilds[i]->pGuildMembers, j, m);
		{
			U32 id = guilds[i]->pGuildMembers[j];
			if (id != forUser->id) // filter out self
			{
				ChatUser *user = userFindByContainerId(id);
				if (user)
					eaPushUnique(roster, user);
				else
					chatServer_ForceAccountUpdate(id);
			}
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;
	eaDestroy(&guilds);
}

// Compare two XmppClientState objects for qsort() or bsearch(), ordered by priority first, ID second.
int xmpp_ClientStateCompare(const void *lhs, const void *rhs)
{
	const XmppClientState *const *ppLhs = lhs;
	const XmppClientState *const *ppRhs = rhs;
	const XmppClientState *leftState = *ppLhs, *rightState = *ppRhs;
	U64 leftId, rightId;

	// Sort by priority first.
	devassert(leftState && rightState);
	if (leftState->priority > rightState->priority)
		return -1;
	else if (leftState->priority < rightState->priority)
		return 1;

	// Priority equal; sort by ID.
	leftId = XMPP_UniqueClientId(leftState);
	rightId = XMPP_UniqueClientId(rightState);
	if (leftId > rightId)
		return -1;
	else if (leftId < rightId)
		return 1;

	// Self-comparison.
	devassert(leftState == rightState);
	return 0;
}

// Perform XMPP initialization, if necessary.
void XMPPChat_Init()
{
	PERFINFO_AUTO_START_FUNC();

	// Don't do anything if initialization is complete.
	if (gXmppState.xmppInitialized)
		return;

	// Create client hash table.
	gXmppState.stXmppNodes = stashTableCreateInt(10000);
	gXmppState.stXmppStates = stashTableCreateInt(10000);
	resRegisterDictionaryForStashTable("XmppNodes", RESCATEGORY_OTHER, 0, gXmppState.stXmppNodes, parse_XmppClientNode);

	// Note that initialization is complete.
	gXmppState.xmppInitialized = true;

	PERFINFO_AUTO_STOP_FUNC();
}

// Count the number of resources for each node.
static int CountResources(void *userData, StashElement element)
{
	unsigned long *count = (unsigned long*)userData;
	XmppClientNode *node = stashElementGetPointer(element);
	*count += eaSize(&node->resources);
	return 1;
}

// Log in an XMPP resource.
void XMPPChat_Login(ChatUser *user, const char *resource, U32 accessLevel)
{
	ChatLoginData loginData = {0};
	PlayerInfoStruct playerinfo = {0};

	PERFINFO_AUTO_START_FUNC();

	loginData.uAccountID = user->id;
	loginData.pAccountName = (char*) user->accountName;
	loginData.pDisplayName = (char*) user->handle;
	loginData.uAccessLevel = accessLevel;
	loginData.pPlayerInfo = &playerinfo;
	playerinfo.uChatServerID = XMPP_CHAT_ID; // XMPP Chat ID
	playerinfo.onlinePlayerName = (char*) resource;
	playerinfo.playerMap.iMapInstance = 0;
	playerinfo.playerMap.pchMapName = XMPP_MAP_NAME;
	playerinfo.playerMap.pchMapNameMsgKey = XMPP_MAP_NAME_MSGKEY;
	playerinfo.playerMap.pchMapVars = NULL;
	playerinfo.playerMap.pchNeighborhoodNameMsgKey = XMPP_NEIGHBORHOOD_NAME_MSGKEY;
	playerinfo.onlineCharacterAccessLevel = accessLevel;

	// Log this user in and update player info
	userLoginOnly(user, &loginData, XMPP_CHAT_ID, true);

	// Report login.
	if (gbChatVerbose)
		printf("XMPP Login - @%s\n", user->handle);
	
	PERFINFO_AUTO_STOP();
}

// Log out an XMPP resource.
void XMPPChat_Logout(ChatUser *user, XmppClientState *state)
{
	if (gbChatVerbose)
		printf("XMPP Logout - @%s\n", user->handle);

	// Log out of all chat rooms
	EARRAY_CONST_FOREACH_BEGIN(state->rooms, j, n);
	{
		void *data = XMPPChat_GetChatRoomData(state->rooms[j]->eDomain, state->rooms[j]->room);
		if (data)
			XMPPChatRoom_NotifyLeaveSingle(state, user, state->rooms[j]->eDomain, state->rooms[j]->room, data);
	}
	EARRAY_FOREACH_END;
	userLogout(user, XMPP_CHAT_ID);
}

void XMPPChat_GetPresence(SA_PARAM_NN_VALID XMPP_PresenceData *pData, SA_PARAM_NN_VALID ChatUser *user, bool bGetPlayer)
{
	StructInit(parse_XMPP_PresenceData, pData);
	estrCopy2(&pData->handle, XMPPChat_GetEscapedHandle(user));
	if (!(user->online_status & USERSTATUS_ONLINE) || (user->online_status & USERSTATUS_HIDDEN))
	{
		pData->avail = XMPP_PresenceAvailability_Unavailable;
	}
	else
	{
		if (user->status)
			estrCopy2(&pData->status, user->status);
		// Get availability.
		if (user->online_status & USERSTATUS_AFK)
			pData->avail = XMPP_PresenceAvailability_Away;
		else if (user->online_status & (USERSTATUS_DND | USERSTATUS_DEAF))
			pData->avail = XMPP_PresenceAvailability_Dnd;
		else
			pData->avail = XMPP_PresenceAvailability_Normal;

		// Gets the PlayerInfo with the highest priority with a resource (takes the first one it finds for ties)
		if (bGetPlayer && eaSize(&user->ppPlayerInfo) > 0)
		{
			U32 uPriorityMax = 0;
			PlayerInfoStruct *fromPlayer = NULL;
			U32 priority = 0;

			EARRAY_CONST_FOREACH_BEGIN(user->ppPlayerInfo, i, n);
			{
				PlayerInfoStruct *curPlayer = user->ppPlayerInfo[i];
				// Skip resources with no onlinePlayerName (resource for XMPP)
				if (curPlayer->onlinePlayerName)
				{
					// Check priority for XMPP logins
					if (curPlayer->uChatServerID == XMPP_CHAT_ID)
					{
						XmppClientNode *node = xmpp_node_from_chatuser(user);
						if (node && eaSize(&node->resources))
						{
							U32 curPriority = node->resources[0]->priority;
							if (curPriority < 0) // skip negatives
								continue;
							if (curPriority > priority)
							{
								fromPlayer = curPlayer;
								priority = curPriority;
							}
						}
					}
					if (!fromPlayer)
						fromPlayer = curPlayer;
				}
			}
			EARRAY_FOREACH_END;
			if (fromPlayer)
			{
				estrCopy2(&pData->resource, fromPlayer->onlinePlayerName);
				pData->priority = priority;
			}
		}
	}
}

// Process the initial presence from a client.
void XMPPChat_InitialPresence(XmppClientState *state)
{
	ChatUser *user;
	ChatUser **roster = NULL;

	// Get contacts.
	user = xmpp_chatuser_from_clientstate(state);
	xmppchat_getroster(user, &roster, false);

	// Get presence for each subscribed contact.
	EARRAY_CONST_FOREACH_BEGIN(roster, i, n);
	{
		ChatUser *from = roster[i];
		XMPP_PresenceData pdata = {0};

		XMPPChat_GetPresence(&pdata, from, true);

		// Skip users who are not online or hidden.
		if (pdata.avail == XMPP_PresenceAvailability_Unavailable)
		{
			StructDeInit(parse_XMPP_PresenceData, &pdata);
			continue;
		}

		// Send presence for each player.
		/*EARRAY_CONST_FOREACH_BEGIN(from->ppPlayerInfo, j, m);
		{
			PlayerInfoStruct *fromPlayer = from->ppPlayerInfo[j];
			
			pdata.priority = 0;
			// Skip empty player entries.
			if (fromPlayer->onlinePlayerName)
			{
				estrCopy2(&pdata.resource, fromPlayer->onlinePlayerName);
				// Get source priority.
				if (fromPlayer->uChatServerID == XMPP_CHAT_ID)
				{
					XmppClientNode *fromNode = xmpp_node_from_chatuser(from);
					if (fromNode && eaSize(&fromNode->resources))
						pdata.priority = fromNode->resources[0]->priority;
				}

				// Skip clients with negative priority.
				if (pdata.priority >= 0)
			}
		}
		EARRAY_FOREACH_END;*/
		XMPP_SendPresenceUpdate(state, NULL, &pdata);
		StructDeInit(parse_XMPP_PresenceData, &pdata);
	}
	EARRAY_FOREACH_END;
}

// Set presence for a particular resource.
void XMPPChat_SetPresence(XmppClientState *state, int availability, const char *status_msg)
{
	ChatUser *user = userFindByContainerId(state->uAccountID);
	if (!devassert(user))
		return;
	if (!status_msg)
		status_msg = "";

	// Set user's availability and status message.
	switch (availability)
	{
		// Normal availability
		case XMPP_PresenceAvailability_Chat:
		case XMPP_PresenceAvailability_Normal: 
			UserBackWithMessage(user, status_msg);
			break;

		// Do not disturb
		case XMPP_PresenceAvailability_Dnd:
			UserDNDWithMessage(user, status_msg);
			break;

		// Away and extended away
		case XMPP_PresenceAvailability_Away:
		case XMPP_PresenceAvailability_Xa:
			UserAFKWithMessage(user, status_msg);
			break;

		// Unavailable
		case XMPP_PresenceAvailability_Unavailable: // Does nada
			break;
		default:
			devassertmsg(0, "Trying to set XMPP user status with unknown presence type.");
	}
	if (gbChatVerbose)
		printf("XMPP Presence Change - @%s to %s\n", user->handle, StaticDefineIntRevLookup(XMPP_PresenceAvailabilityEnum, availability));
}

// Propagate presence updates from the Global Chat Server to the XMPP clients.
void XMPPChat_RecvPresence(ChatUser *from, const char *playerName)
{
	ChatUser **friends = NULL;
	XMPP_PresenceData fromPData = {0};

	PERFINFO_AUTO_START_FUNC();

	// Return if XMPP is disabled.
	if (XMPP_IsDisabled())
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	if (gbChatVerbose)
		printf ("XMPP Receive Presence from @%s\n", from->handle);

	// Get roster to find people who would be subscribed to this user's presence.
	xmppchat_getroster(from, &friends, false);
	if (!eaSize(&friends))
	{
		eaDestroy(&friends);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	XMPPChat_GetPresence(&fromPData, from, true);

	// Deliver presence to each potential friend.
	EARRAY_CONST_FOREACH_BEGIN(friends, i, n);
	{
		XmppClientNode *node;

		// Deliver to each resource.
		node = xmpp_node_from_chatuser(friends[i]);
		if (node)
		{
			EARRAY_CONST_FOREACH_BEGIN(node->resources, j, m);
			{
				XmppClientState *friendState = node->resources[j];

				// Skip resources that have not indicated that they are ready to accept presence information.
				if (!friendState->accepting_presence)
					continue;

				// Send from explicitly-provided handle, which is presumably unavailable.
				if (playerName)
				{
					XMPP_PresenceData pdata = {0};
					StructInit(parse_XMPP_PresenceData, &pdata);
					pdata.avail = XMPP_PresenceAvailability_Unavailable;
					estrCopy2(&pdata.handle, XMPPChat_GetEscapedHandle(from));
					estrCopy2(&pdata.resource, playerName);
					if (fromPData.status)
						estrCopy2(&pdata.status, fromPData.status);
					// Send presence from this player to this XMPP resource.
					XMPP_SendPresenceUpdate(friendState, NULL, &pdata);
					StructDeInit(parse_XMPP_PresenceData, &pdata);
				}
				else
				{
					// Send from each connected player.
					/*EARRAY_CONST_FOREACH_BEGIN(from->ppPlayerInfo, k, o);
					{	
						// Skip empty player entries.
						if (!from->ppPlayerInfo[k]->onlinePlayerName)
							continue;

						fromPData.priority = 0;
	
						// Get source priority.
						if (from->ppPlayerInfo[k]->uChatServerID == XMPP_CHAT_ID)
						{
							XmppClientNode *fromNode = xmpp_node_from_chatuser(friends[i]);
							if (fromNode && eaSize(&fromNode->resources))
								fromPData.priority = fromNode->resources[0]->priority;
						}

						// Skip clients with negative priority.
						if (fromPData.priority < 0)
							continue;
						estrCopy2(&fromPData.resource, from->ppPlayerInfo[k]->onlinePlayerName);
	
					}
					EARRAY_FOREACH_END;*/
					// Send presence from this player to this XMPP resource.
					XMPP_SendPresenceUpdate(friendState, NULL, &fromPData);
				}
			}
			EARRAY_FOREACH_END;
		}
	}
	EARRAY_FOREACH_END;

	StructDeInit(parse_XMPP_PresenceData, &fromPData);
	eaDestroy(&friends);

	PERFINFO_AUTO_STOP_FUNC();
}

// Send a private message from a resource.
bool XMPPChat_SendPrivateMessage(XmppClientState *from, const char *to, const char *subject, const char *body, 
								 const char *thread, const char *id, int type)
{
	ChatMessage *message = NULL;
	ChatUser *from_user = NULL;
	ChatUser *to_user = NULL;
	char *to_name;
	ChatUserInfo *fromInfo = NULL, *toInfo = NULL;
	int result;

	PERFINFO_AUTO_START_FUNC();

	// Look up users.
	from_user = xmpp_chatuser_from_clientstate(from);
	to_user = xmpp_chatuser_from_jid(to);
	if (!to_user)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	// Rate limit.
	if (chatRateLimiter(from_user))
		return false;

	// Create chat message.
	fromInfo = ChatCommon_CreateUserInfo(from_user->id, 0, from_user->handle, from->resource);
	to_name = xmpp_make_resource_from_jid(to);
	toInfo = ChatCommon_CreateUserInfo(to_user->id, 0, to_user->handle, to_name);
	free(to_name);
	message = ChatCommon_CreateMsg(fromInfo, toInfo, kChatLogEntryType_Private, NULL, body, NULL);
	StructDestroy(parse_ChatUserInfo, fromInfo);
	StructDestroy(parse_ChatUserInfo, toInfo);
	if (!message)
	{
		Errorf("Could not create chat message struct");
		PERFINFO_AUTO_STOP();
		return false;
	}

	// Copy message data.
	if (subject) estrCopy2(&message->pchSubject, subject);
	if (thread) estrCopy2(&message->pchThread, thread);
	if (id) estrCopy2(&message->pchId, id);
	message->xmppType = type;

	// Send message.
	verbose_printf("Sending message \"%s\" to \"%s\".\n", body, to);
	result = userSendPrivateMessage(from_user, to_user, message, NULL);

	StructDestroy(parse_ChatMessage, message);

	PERFINFO_AUTO_STOP();
	return result == CHATRETURN_FWD_NONE;
}

void XMPPChat_RecvPrivateMessage(const ChatMessage *msg)
{
	XmppClientState *found;
	ChatUser *toUser;

	PERFINFO_AUTO_START_FUNC();

	// Return if XMPP is disabled.
	if (XMPP_IsDisabled())
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Validate parameters.
	if (!msg) 
	{
		Errorf("Trying to send a NULL empty chat message.");
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Look up user.
	toUser = userFindByContainerId(msg->pTo->accountID);
	if (!toUser)
	{
		Errorf("Trying to send a chat message to a user who is not online.");
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Check if its an XMPP user, and if so, deliver.
	found = xmpp_clientstate_from_chatuser(toUser, NULL);
	if (found)
	{
		char *subject = msg->pchSubject;
		char *body = msg->pchText;
		char *thread = msg->pchThread;
		XMPP_MessageType type;
		char *id;

		// Create from JID.
		if (!msg->pFrom || !msg->pFrom->pchHandle) 
		{
			Errorf("Non-player entity trying to send a message to an XMPP client.");
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}

		// Copy message information.
		type = msg->xmppType;
		if (!type)
			type = XMPP_MessageType_Normal;
		if (msg->pchId)
			id = strdup(msg->pchId);
		else
			id = strdupf("%x", randomU32());

		// Deliver message.
		XMPP_SendPrivateMessage(found, id, msg->pFrom->pchHandle,
			msg->pFrom->pchName && msg->pFrom->pchName[0] ? msg->pFrom->pchName : NULL, 
			body, type);

		free(id);
	}
	else
	{
		// All messages directed to this function should be for XMPP.
		devassert(0);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

// Route a message from the Global Chat Server to a special channel.
void XMPPChat_RecvSpecialMessage(const ChatMessage *msg)
{
	PERFINFO_AUTO_START_FUNC();

	// Return if XMPP is disabled.
	if (XMPP_IsDisabled())
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	if (!msg || !msg->pchChannel)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Currently, XMPP only supports guild and officer special channels.
	switch (msg->eType)
	{
	case kChatLogEntryType_Guild:
	case kChatLogEntryType_Officer:
		XMPPChat_RecvGuildChatMessage(msg);
		break;
	default:
		break;
	}
	PERFINFO_AUTO_STOP_FUNC();
}

// Add an item to the XMPP roster without necessarily subscribing.
bool XMPPChat_RosterUpdate(XmppClientState *from, const char *jid, const char *const *groups)
{
	ChatUser *addUser;
	ChatUser *user = xmpp_chatuser_from_clientstate(from);

	// Note: groups are unimplemented and ignored.
	
	// Find this user.
	addUser = xmpp_chatuser_from_jid(jid);
	if (!addUser)
		return false;

	// Update roster.
	AutoTrans_trXmppRosterUpdate(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, addUser->id);
	return true;
}

// Remove an item from the XMPP roster.
bool XMPPChat_RosterRemove(XmppClientState *from, const char *jid)
{
	ChatUser *removeUser;
	ChatUser *user = xmpp_chatuser_from_clientstate(from);

	// Find this user.
	removeUser = xmpp_chatuser_from_jid(jid);
	if (!removeUser)
		return false;

	// Remove item from roster.
	AutoTrans_trXmppRosterRemove(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, removeUser->id);
	return true;
}

// Notify XMPP clients that a user has been added to the guild.
void XMPPChat_GuildListAdd(ChatUser *user, ChatGuild *guild)
{
	XMPP_RosterItemList rosterList = {0};
	XMPP_RosterItemList tempList = {0};
	XmppClientNode *userNode;

	PERFINFO_AUTO_START_FUNC();

	// Return if XMPP is disabled.
	if (XMPP_IsDisabled())
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Validate parameters.
	if (!devassert(user) || !devassert(guild))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	userNode = xmpp_node_from_chatuser(user);

	eaSetSize(&tempList.ppRoster, 1);
	// Loop over each guild member online with XMPP.
	EARRAY_INT_CONST_FOREACH_BEGIN(guild->pGuildMembers, i, n);
	{
		ChatUser *member = userFindByContainerId(guild->pGuildMembers[i]);
		XmppClientNode *node;
		if (!member || member->id == user->id)
			continue;
		node = xmpp_node_from_chatuser(member);
		
		if (node)
		{	// Loop over each of the user's online resources.
			EARRAY_CONST_FOREACH_BEGIN(node->resources, j, m);
			{
				XmppClientState *state = node->resources[j];
				XMPP_RosterItem *rosterItem = XMPPChat_GetFriend(node, user, NULL);
				char *tempRosterString = NULL;

				tempList.ppRoster[0] = rosterItem;
				XMPP_WRITE_STRUCT(&tempRosterString, parse_XMPP_RosterItemList, &tempList);
				sendCommandToLinkEx(XMPP_GetLink(), "XMPP_SendRoster", "%d \"\" %s", 
					state->uStateID, tempRosterString);
				estrDestroy(&tempRosterString);
				StructDestroy(parse_XMPP_RosterItem, rosterItem);
				tempList.ppRoster[0] = NULL; // insurance
			}
			EARRAY_FOREACH_END;
		}
		if (userNode && userOnline(member))
		{
			eaPush(&rosterList.ppRoster, XMPPChat_GetFriend(userNode, member, NULL));
		}
	}
	EARRAY_FOREACH_END;
	eaDestroy(&tempList.ppRoster);

	// Send presence.
	XMPPChat_RecvPresence(user, NULL);

	if (userNode && rosterList.ppRoster)
	{
		// Send roster update to added member
		char *rosterString = NULL;
		XMPP_WRITE_STRUCT(&rosterString, parse_XMPP_RosterItemList, &rosterList);

		EARRAY_CONST_FOREACH_BEGIN(userNode->resources, i, n);
			sendCommandToLinkEx(XMPP_GetLink(), "XMPP_SendRoster", "%d \"\" %s", 
				userNode->resources[i]->uStateID, rosterString);
		EARRAY_FOREACH_END;
		estrDestroy(&rosterString);
	}

	// Free roster.
	StructDeInit(parse_XMPP_RosterItemList, &rosterList);
	PERFINFO_AUTO_STOP_FUNC();
}

void XMPPChat_NotifyGuildPart(ChatGuild *guild, ChatUser *user, bool bWasOfficer, bool bKicked);
// Notify XMPP clients that a user has been removed from the guild.
void XMPPChat_GuildListRemove(ChatUser *user, ChatGuild *guild, bool bWasOfficer, bool bKicked)
{
	XMPP_RosterItemList rosterList = {0};
	XMPP_RosterItemList tempList = {0};
	XmppClientNode *userNode;

	PERFINFO_AUTO_START_FUNC();

	// Return if XMPP is disabled.
	if (XMPP_IsDisabled())
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Validate parameters.
	if (!devassert(user) || !devassert(guild))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	userNode = xmpp_node_from_chatuser(user);

	eaSetSize(&tempList.ppRoster, 1);
	// Loop over each guild member online with XMPP.
	EARRAY_INT_CONST_FOREACH_BEGIN(guild->pGuildMembers, i, n);
	{
		ChatUser *member = userFindByContainerId(guild->pGuildMembers[i]);
		XmppClientNode *node;
		if (!member || member->id == user->id)
			continue;
		node = xmpp_node_from_chatuser(member);
		if (node)
		{	// Loop over each of the user's online resources.
			EARRAY_CONST_FOREACH_BEGIN(node->resources, j, m);
			{
				XmppClientState *state = node->resources[j];
				XMPP_RosterItem *rosterItem = XMPPChat_GetFriend(node, user, NULL);
				char *tempRosterString = NULL;

				tempList.ppRoster[0] = rosterItem;
				XMPP_WRITE_STRUCT(&tempRosterString, parse_XMPP_RosterItemList, &tempList);
				sendCommandToLinkEx(XMPP_GetLink(), "XMPP_SendRoster", "%d \"\" %s", 
					state->uStateID, tempRosterString);
				estrDestroy(&tempRosterString);
				StructDestroy(parse_XMPP_RosterItem, rosterItem);
				tempList.ppRoster[0] = NULL; // insurance
			}
			EARRAY_FOREACH_END;
		}

		if (userNode && member->id != userNode->uAccountID &&  userOnline(member))
		{
			eaPush(&rosterList.ppRoster, XMPPChat_GetFriend(userNode, member, NULL));
		}
	}
	EARRAY_FOREACH_END;
	eaDestroy(&tempList.ppRoster);

	if (userNode && rosterList.ppRoster)
	{
		// Send roster update to removed member
		char *rosterString = NULL;
		XMPP_WRITE_STRUCT(&rosterString, parse_XMPP_RosterItemList, &rosterList);

		EARRAY_CONST_FOREACH_BEGIN(userNode->resources, i, n);
			sendCommandToLinkEx(XMPP_GetLink(), "XMPP_SendRoster", "%d \"\" %s", 
				userNode->resources[i]->uStateID, rosterString);
		EARRAY_FOREACH_END;
		estrDestroy(&rosterString);
	}
	XMPPChat_NotifyGuildPart(guild, user, bWasOfficer, bKicked);

	// Free roster.
	StructDeInit(parse_XMPP_RosterItemList, &rosterList);
	PERFINFO_AUTO_STOP_FUNC();
}

// If a user is logged in with XMPP, notify him of the incoming friends request.
void XMPPChat_NotifyFriendRequest(ChatUser *user, ChatUser *fromUser)
{
	XmppClientNode *node;
	XMPP_ClientStateList list = {0};
	PERFINFO_AUTO_START_FUNC();

	// Return if XMPP is disabled.
	if (XMPP_IsDisabled())
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	// Validate parameters.
	devassert(user && fromUser);
	if (!user || !fromUser)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	// Check if this is an XMPP user.
	node = xmpp_node_from_chatuser(user);
	if (!node)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// If so, send notifications to the connected resources.
	EARRAY_CONST_FOREACH_BEGIN(node->resources, i, n);
	{
		eaiPush(&list.eaiStateIDs, node->resources[i]->uStateID);
	}
	EARRAY_FOREACH_END;
	XMPP_NotifyFriendRequest(XMPPChat_GetEscapedHandle(fromUser), &list);
	StructDeInit(parse_XMPP_ClientStateList, &list);
	PERFINFO_AUTO_STOP_FUNC();
}

// If a user is logged in with XMPP, notify him that he has a new friend.
void XMPPChat_NotifyNewFriend(ChatUser *user, ChatUser *fromUser)
{
	XmppClientNode *node;
	XMPP_ClientStateList list = {0};
	PERFINFO_AUTO_START_FUNC();

	// Return if XMPP is disabled.
	if (XMPP_IsDisabled())
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	// Validate parameters.
	devassert(user && fromUser);
	if (!user || !fromUser)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	// Check if this is an XMPP user.
	node = xmpp_node_from_chatuser(user);
	if (!node)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// If so, send notifications to the connected resources.
	EARRAY_CONST_FOREACH_BEGIN(node->resources, i, n);
	{
		eaiPush(&list.eaiStateIDs, node->resources[i]->uStateID);
	}
	EARRAY_FOREACH_END;
	XMPP_NotifyNewFriend(XMPPChat_GetEscapedHandle(fromUser), &list, NULL);
	StructDeInit(parse_XMPP_ClientStateList, &list);

	// Send presence.
	// TODO optimize this to only send to fromUser
	XMPPChat_RecvPresence(user, NULL);
	PERFINFO_AUTO_STOP_FUNC();
}

// If a user is logged in with XMPP, notify him that he has a new friend.
void XMPPChat_NotifyFriendRemove(ChatUser *user, ChatUser *fromUser)
{
	XmppClientNode *node;
	XMPP_ClientStateList list = {0};
	PERFINFO_AUTO_START_FUNC();

	// Return if XMPP is disabled.
	if (XMPP_IsDisabled())
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	// Validate parameters.
	devassert(user && fromUser);
	if (!user || !fromUser)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	// Check if this is an XMPP user.
	node = xmpp_node_from_chatuser(user);
	if (!node)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// If so, send notifications to the connected resources.
	EARRAY_CONST_FOREACH_BEGIN(node->resources, i, n);
	{
		eaiPush(&list.eaiStateIDs, node->resources[i]->uStateID);
	}
	EARRAY_FOREACH_END;
	XMPP_NotifyFriendRemove(XMPPChat_GetEscapedHandle(fromUser), &list);
	StructDeInit(parse_XMPP_ClientStateList, &list);
	PERFINFO_AUTO_STOP_FUNC();
}

// Get the domain for a particular kind of channel from the Chat Server channel name.
static enum XMPP_RoomDomain XMPPChat_DomainFromChannelName(const char *pName)
{
	enum XMPP_RoomDomain domain;
	if (strStartsWith(pName, ZONE_CHANNEL_PREFIX))
		domain = XMPP_RoomDomain_Unknown;
	else if (strStartsWith(pName, TEAM_CHANNEL_PREFIX))
		domain = XMPP_RoomDomain_Unknown;
	else if (strStartsWith(pName, GUILD_CHANNEL_PREFIX))
		domain = XMPP_RoomDomain_Guild;
	else if (strStartsWith(pName, OFFICER_CHANNEL_PREFIX))
		domain = XMPP_RoomDomain_Officer;
	else
		domain = XMPP_RoomDomain_Channel;
	return domain;
}

// A user has spoken in a channel.
void XMPPChat_NotifyChatMessage(ChatChannel *channel, const ChatMessage *message)
{
	XMPP_ChatRoomMessage chatMsg = {0};
	XMPP_ClientStateList list = {0};

	PERFINFO_AUTO_START_FUNC();

	// Return if XMPP is disabled.
	if (XMPP_IsDisabled())
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Only process normal channels and guilds.
	chatMsg.eDomain = XMPPChat_DomainFromChannelName(channel->name);
	if (chatMsg.eDomain == XMPP_RoomDomain_Unknown)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Escape any spaces in the channel name.
	XMPPChat_GetXMPPChannelInfo(&chatMsg.room, NULL, channel, XMPP_RoomDomain_Channel);
	estrCopy2(&chatMsg.nickname, message->pFrom->pchHandle);
	if (message->pchText)
		estrCopy2(&chatMsg.message, message->pchText);
	if (message->pchSubject)
		estrCopy2(&chatMsg.subject, message->pchSubject);
	if (message->pchThread)
		estrCopy2(&chatMsg.thread, message->pchThread);
	
	// Loop over each channel member.
	EARRAY_INT_CONST_FOREACH_BEGIN(channel->members, i, n);
	{
		ChatUser *memberUser;
		XmppClientNode *node;

		// Look up member and check if its an XMPP client.
		memberUser = userFindByContainerId(channel->members[i]);
		if (!memberUser)
			continue;
		node = xmpp_node_from_chatuser(memberUser);
		if (!node)
			continue;

		// Loop over each connected resource and deliver the message if it is in that channel.
		EARRAY_CONST_FOREACH_BEGIN(node->resources, j, m);
		{
			int found;
			found = eaIndexedFindUsingString(&node->resources[j]->rooms, chatMsg.room);
			if (found != -1)
				eaiPush(&list.eaiStateIDs, node->resources[j]->uStateID);
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;

	XMPP_NotifyChatMessageBatch(message->pchId, &list, &chatMsg);

	StructDeInit(parse_XMPP_ChatRoomMessage, &chatMsg);
	StructDeInit(parse_XMPP_ClientStateList, &list);

	PERFINFO_AUTO_STOP_FUNC();
}

// A ChatUser has joined a global channel from in-game.
void XMPPChat_NotifyChatOnline(ChatChannel *channel, ChatUser *user)
{
	XMPP_RoomDomain domain;
	char *room = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Return if XMPP is disabled.
	if (XMPP_IsDisabled())
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	domain = XMPPChat_DomainFromChannelName(channel->name);
	if (domain != XMPP_RoomDomain_Channel)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	estrStackCreate(&room);
	XMPPChat_GetXMPPChannelInfo(&room, NULL, channel, XMPP_RoomDomain_Channel);
	XMPPChatRoom_NotifyJoin(NULL, user, XMPP_RoomDomain_Channel, room);
	estrDestroy(&room);

	PERFINFO_AUTO_STOP_FUNC();
}

// A ChatUser just logged in for the guild
void XMPPChat_NotifyGuildOnline(ChatGuild *guild, ChatUser *user, bool bIsOfficerChannel)
{
	XMPP_RoomDomain domain;
	char *room = NULL;

	if (XMPP_IsDisabled())
		return;
	estrStackCreate(&room);
	domain = bIsOfficerChannel ? XMPP_RoomDomain_Officer : XMPP_RoomDomain_Guild;

	XMPPChat_GetXMPPChannelInfo(&room, NULL, guild, domain);
	if (devassert(room))
		XMPPChatRoom_NotifyJoin(NULL, user, domain, room);	
	estrDestroy(&room);
}

void XMPPChat_NotifyGuildOffline(ChatGuild *guild, ChatUser *user, bool bIsOfficerChannel)
{
	XMPP_RoomDomain domain;
	char *room = NULL;

	if (XMPP_IsDisabled())
		return;
	estrStackCreate(&room);
	domain = bIsOfficerChannel ? XMPP_RoomDomain_Officer : XMPP_RoomDomain_Guild;

	XMPPChat_GetXMPPChannelInfo(&room, NULL, guild, domain);
	if (devassert(room))
		XMPPChatRoom_NotifyLeaveSingle(NULL, user, domain, room, guild);
	estrDestroy(&room);
}

// A user has left a guild or has gone COMPLETELY offline.
void XMPPChat_NotifyGuildPart(ChatGuild *guild, ChatUser *user, bool bWasOfficer, bool bKicked)
{
	XMPP_RoomDomain domain;
	char *room = NULL, *key = NULL;
	XMPPChat_GuildMembers *membersStruct = NULL;

	PERFINFO_AUTO_START_FUNC();
	// Return if XMPP is disabled.
	devassert(guild && user);
	if (XMPP_IsDisabled() || !guild || !user)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	estrStackCreate(&room);
	// Guild Channel
	domain = XMPP_RoomDomain_Guild;
	XMPPChat_GetXMPPChannelInfo(&room, NULL, guild, domain);
	if (!devassert(room))
	{
		estrDestroy(&room);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	XMPPChatRoom_NotifyLeaveAll(user, domain, room, bKicked);

	// Officer Channel
	if (bWasOfficer)
	{
		domain = XMPP_RoomDomain_Officer;
		XMPPChat_GetXMPPChannelInfo(&room, NULL, guild, domain);
		if (!room)
		{
			estrDestroy(&room);
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}
		XMPPChatRoom_NotifyLeaveAll(user, domain, room, bKicked);
	}

	estrDestroy(&room);
	PERFINFO_AUTO_STOP_FUNC();
}

// A user has left a channel.
void XMPPChat_NotifyChatPart(ChatChannel *channel, ChatUser *user, bool bKicked)
{
	XMPP_RoomDomain domain;
	char *room = NULL;

	PERFINFO_AUTO_START_FUNC();
	// Return if XMPP is disabled.
	if (XMPP_IsDisabled())
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Only process normal channels here
	domain = XMPPChat_DomainFromChannelName(channel->name);
	if (domain != XMPP_RoomDomain_Channel)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	estrStackCreate(&room);
	XMPPChat_GetXMPPChannelInfo(&room, NULL, channel, XMPP_RoomDomain_Channel);
	XMPPChatRoom_NotifyLeaveAll(user, XMPP_RoomDomain_Channel, room, bKicked);
	estrDestroy(&room);

	PERFINFO_AUTO_STOP_FUNC();
}

__forceinline static void XMPPChat_GetGuildGroup(char **estr, ChatGuild *guild)
{
	if (guild)
	{
		GlobalChatLinkStruct *shardData = GlobalChatGetShardData(guild->iLinkID);
		if (shardData)
		{
			const char *productName = GCS_GetProductDisplayName(shardData->pProductName);
			const char *shardName = GCS_GetShardDisplayName(shardData->pProductName, shardData->pShardName);
			if (shardName)
				estrPrintf(estr, "%s [%s:%s]", guild->pchName, productName, shardName);
			else
				estrPrintf(estr, "%s [%s]", guild->pchName, productName);
		}
	}
}

// Return common guilds among these two users.
static void XMPPChat_CommonGuilds(char ***eaGuilds, ChatUser *user, ChatUser *otherUser)
{
	ChatGuild **guilds = NULL;

	// Validate parameters.
	if (!devassert(eaGuilds) || !devassert(user) || !devassert(otherUser))
	{
		return;
	}

	// Clear output strings.
	eaClearEString(eaGuilds);

	// Get the intersection.
	GlobalChatServer_CommonGuilds(&guilds, user, otherUser);

	// Format guild list.
	EARRAY_CONST_FOREACH_BEGIN(guilds, i, n);
	{
		ChatGuild *guild = guilds[i];
		char *name = NULL;
		XMPPChat_GetGuildGroup(&name, guild);
		eaPush(eaGuilds, name);
	}
	EARRAY_FOREACH_END;
	eaDestroy(&guilds);
}

// Populate a single roster item with friends list information.
// The information is about the user 'jid' for the user 'state.'
// Returns a null pointer if something goes wrong.
 XMPP_RosterItem *XMPPChat_GetFriend(XmppClientNode *node, ChatUser *user, const char *jid)
{
	ChatUser *withRespectToUser;
	XMPP_RosterItem *result;
	char **guilds = NULL;

	if (!user && jid)
		user = xmpp_chatuser_from_jid(jid);
	// Validate parameters.
	if (!node || !user)
		return NULL;
	withRespectToUser = xmpp_chatuser_from_node(node);
	if (!withRespectToUser)
		return NULL;

	// Get common guilds.
	XMPPChat_CommonGuilds(&guilds, user, withRespectToUser);
	
	// Create roster item.
	result = XMPP_MakeRosterItem(jid, XMPPChat_GetEscapedHandle(user), userIsFriend(user, withRespectToUser->id), guilds,
		userIsFriendSent(user, withRespectToUser->id));
	eaDestroyEString(&guilds);
	return result;
}

bool XMPPChat_GetFriends(XmppClientState *state, XMPP_RosterItem ***eaRoster)
{
	ChatUser *user;
	ChatUser **roster = NULL;
	XmppClientNode *node;
	int size;

	// Validate parameters.
	if (!state || !eaRoster)
	{
		devassert(eaRoster);
		return false;
	}
	node = XMPPChat_ClientNode(state);
	if (!devassert(node))
	{
		return false;
	}

	// Clear existing array.
	eaClear(eaRoster);

	// Look up this user.
	user = xmpp_chatuser_from_node(node);
	if (!devassert(user))
	{
		return false;
	}

	// Get roster.
	xmppchat_getroster(user, &roster, true);

	// Extract information from it.
	size = eaSize(&roster);
	EARRAY_CONST_FOREACH_BEGIN(roster, i, n);
	{
		eaPush(eaRoster, XMPPChat_GetFriend(node, roster[i], NULL));
	}
	EARRAY_FOREACH_END;

	eaDestroy(&roster);
	return true;
}

// Request friendship.
bool XMPPChat_AddFriend(XmppClientState *state, const char *to)
{
	ChatUser *toUser, *user;
	int result;

	// Validate.
	if (!devassert(state) || !devassert(to))
	{
		return false;
	}
	user = xmpp_chatuser_from_clientstate(state);
	devassert(user);
	if (!user)
		return false;

	// Look up user.
	toUser = xmpp_chatuser_from_jid(to);
	if (!toUser)
		return false;

	// Check if they are already friends.
	if (userIsFriend(user, toUser->id))
		return true;

	// Add friend.
	result = userAddFriend(user, toUser);
	return !!result;
}

// Accept a friend request.
bool XMPPChat_AcceptFriend(XmppClientState *state, const char *to)
{
	ChatUser *toUser, *user;
	int result;

	// Validate.
	if (!devassert(state) || !devassert(to))
	{
		return false;
	}
	user = xmpp_chatuser_from_clientstate(state);
	devassert(user);
	if (!user)
		return false;

	// Look up user.
	toUser = xmpp_chatuser_from_jid(to);
	if (!toUser)
		return false;

	// Check existing state.
	if (userIsFriend(user, toUser->id))
		return true;
	if (!userIsFriendReceived(user, toUser->id))
		return false;

	// Accept friend.
	result = userAcceptFriend(user, toUser);
	return !!result;
}

// Remove friend or decline a friends request, as appropriate.
bool XMPPChat_RemoveFriend(XmppClientState *state, const char *to)
{
	ChatUser *toUser, *user;
	int result;

	// Validate.
	if (!devassert(state) || !devassert(to))
	{
		return false;
	}
	user = xmpp_chatuser_from_clientstate(state);
	devassert(user);
	if (!user)
		return false;

	// Look up user.
	toUser = xmpp_chatuser_from_jid(to);
	if (!toUser)
		return false;
	
	// Deny friend requests, if any.
	if (userIsFriendReceived(user, toUser->id))
	{
		result = userRejectFriend(user, toUser);
		if (!result)
			return false;
	}

	// Remove friend, if they're friends.
	if (userIsFriend(user, toUser->id))
	{
		result = userRemoveFriend(user, toUser->id);
		if (!result)
			return false;
	}
	return true;
}

// Get the list of outstanding incoming friend requests.
void XMPPChat_IncomingFriendRequests(char ***eaRequests, XmppClientState *state)
{
	ChatUser *user;

	// Validate.
	devassert(eaRequests && state);
	if (!eaRequests || !state)
		return;

	// Clear output array.
	eaClearEString(eaRequests);

	// Look up ChatUser.
	user = xmpp_chatuser_from_clientstate(state);
	devassert(user);
	if (!user)
		return;

	// Create list.
	EARRAY_CONST_FOREACH_BEGIN(user->friendReqs_in, i, n);
	{
		U32 friendId = user->friendReqs_in[i]->userID;
		ChatUser *friendUser = userFindByContainerId(friendId);
		if (!friendUser)
			continue;
		eaPush(eaRequests, estrDup(XMPPChat_GetEscapedHandle(friendUser)));
	}
	EARRAY_FOREACH_END;
}

// Return true if this jid is a friend.
bool XMPPChat_IsFriend(XmppClientState *state, const char *jid)
{
	ChatUser *user, *otherUser;

	// Look up user.
	if (!state || !jid || !*jid)
	{
		devassert(0);
		return false;
	}
	user = xmpp_chatuser_from_clientstate(state);
	devassert(user);
	if (!user)
		return false;

	// Look up potential friend.
	otherUser = xmpp_chatuser_from_jid(jid);
	if (!otherUser)
		return false;

	// Return true if they're friends.
	return userIsFriend(user, otherUser->id);
}

// Return the node name for a logged-in user.
const char *XMPPChat_NodeName(XmppClientState *state)
{
	ChatUser *user = xmpp_chatuser_from_clientstate(state);
	if (!user)
		return NULL;
	return XMPPChat_GetEscapedHandle(user);
}

// Return the node name for a logged-in user.
XmppClientNode *XMPPChat_ClientNode(XmppClientState *state)
{
	XmppClientNode *found = NULL;
	stashIntFindPointer(gXmppState.stXmppNodes, state->uAccountID, &found);
	return found;
}

// Return true if the user for this client is a member of a guild.
bool XMPPChat_HasGuilds(XmppClientState *state)
{
	ChatUser *user = xmpp_chatuser_from_clientstate(state);;
	if (!devassert(user))
	{
		devassert(0);
		return false;
	}
	return GlobalChatServer_HasGuilds(user);
}

// Return true if the user for this client is an officer of any guild.
bool XMPPChat_IsOfficer(XmppClientState *state)
{
	ChatUser *user = xmpp_chatuser_from_clientstate(state);
	if (!devassert(user))
	{
		return false;
	}
	return GlobalChatServer_IsOfficer(user);
}

/************************************************************************/
/* Interface for XMPP Gateway: Chat channel commands                    */
/************************************************************************/

// Get guild channel information.
bool XMPPChat_GuildChannelInfo(XmppChannelInfo *pChannel, int domain, const char *room)
{
	ChatGuild *guild;
	ChatUser **members = NULL;

	// Look up guild.
	guild = XMPPChat_GetGuildByRoom(room);
	if (!guild)
		return false;
	XMPPChat_GetXMPPChannelInfo(&pChannel->node, &pChannel->name, guild, domain);


	// Copy channel membership count.
	XMPPChat_GuildOfficersOnline(&members, guild);
	pChannel->members = eaSize(&members);
	eaDestroy(&members);

	return true;
}

enum XMPP_ChannelJoinStatus XMPPChat_ChatJoin(XmppClientState *state, int domain, const char *room)
{
	XMPP_ChannelJoinStatus status = XMPP_ChannelJoinStatus_UnknownFailure;
	// Validate
	if (!state || !room || !*room)
	{
		devassert(0);
		return XMPP_ChannelJoinStatus_UnknownFailure;
	}

	// Handle special channels separately.
	switch (domain)
	{
	case XMPP_RoomDomain_Channel:
		status = XMPPChat_ChannelJoin(state, domain, room);
		break;
	case XMPP_RoomDomain_Guild:
	case XMPP_RoomDomain_Officer: // both Guild and Officer use same function
		status = XMPPChat_GuildChatCanJoin(state, domain, room);
		break;
	}
	if (status == XMPP_ChannelJoinStatus_Success)
		XMPPChatRoom_NotifyJoin(state, xmpp_chatuser_from_clientstate(state), domain, room);
	return status;
}

// Send a message to a channel.
bool XMPPChat_ChatMessage(XmppClientState *state, int domain, const char *room, const char *text)
{
	// Validate
	if (!devassert(state) || !devassert(room && *room))
	{
		return false;
	}

	switch (domain)
	{
	case XMPP_RoomDomain_Channel:
		return XMPPChat_ChannelMessage(state, domain, room, text);
	case XMPP_RoomDomain_Guild:
	case XMPP_RoomDomain_Officer: // Guild and officer are the same
		return XMPPChat_GuildChatMessage(state, domain, room, text);
	}
	return false;
}

// Leave a channel.
bool XMPPChat_ChatLeave(XmppClientState *state, int domain, const char *room)
{
	// Validate
	if (!devassert(state) || !devassert(room && *room))
	{
		return false;
	}

	switch (domain)
	{
	case XMPP_RoomDomain_Channel:
		return XMPPChat_ChannelLeave(state, domain, room);
		break;
	case XMPP_RoomDomain_Guild:
	case XMPP_RoomDomain_Officer: // Guild and officer use the same callback
		return XMPPChat_GuildChatLeave(state, domain, room);
		break;
	}
	return false;
}

// Change room subject.
bool XMPPChat_ChatSubject(XmppClientState *state, int domain, const char *room, const char *message)
{
	ChatUser *user;
	int result;
	bool success;
	char *channel = NULL;

	// Validate
	if (!state || !room || !*room)
	{
		devassert(0);
		return false;
	}

	// Only allow setting the subject on regular channels.
	if (domain != XMPP_RoomDomain_Channel)
		return false;

	// Look up user.
	user = xmpp_chatuser_from_clientstate(state);
	devassert(user);
	if (!user)
		return false;

	// Get channel name and type.
	estrStackCreate(&channel);
	success = XMPPChat_GetChannelName(&channel, domain, room);
	if (!success)
		return false;

	// Set MOTD.
	result = channelSetMotd(user, channel, message);
	estrDestroy(&channel);
	return result == CHATRETURN_FWD_NONE;
}

// Get chat occupant information.
XMPP_ChatOccupant *XMPPChat_ChatOccupant(int domain, const char *room, const char *roomnick)
{
	ChatUser *user;
	char *channelName = NULL;
	ChatChannel *channel;
	Watching *watching;
	XMPP_ChatOccupant *occupant;
	bool success;

	// Handle special channels separately.
	if (domain != XMPP_RoomDomain_Channel)
		return XMPPChat_GuildChatOccupant(domain, room, roomnick);

	// Look up user.
	user = userFindByHandle(roomnick);
	if (!user)
		return NULL;

	// Look up channel.
	estrStackCreate(&channelName);
	success = XMPPChat_GetChannelName(&channelName, domain, room);
	if (!success)
	{
		estrDestroy(&channelName);
		return NULL;
	}
	channel = channelFindByName(channelName);
	estrDestroy(&channelName);
	if (!channel)
		return NULL;

	// Look up user channel data.
	watching = userFindWatching(user, channel);
	if (!watching)
		return NULL;
	occupant = StructCreate(parse_XMPP_ChatOccupant);
	occupant->nick = estrDup(XMPPChat_GetEscapedHandle(user));
	if (channelIsUserSilenced(user->id, channel))
		occupant->role = XMPP_Role_Visitor;
	else switch (watching->ePermissionLevel)
	{
			case CHANUSER_OWNER:
				occupant->affiliation = XMPP_Affiliation_Owner;
				occupant->role = XMPP_Role_Moderator;
				break;
			case CHANUSER_ADMIN:
				occupant->affiliation = XMPP_Affiliation_Admin;
				occupant->role = XMPP_Role_Moderator;
			case CHANUSER_OPERATOR:
				occupant->role = XMPP_Role_Moderator;
				break;
			default:
				occupant->role = XMPP_Role_Participant;
	}
	return occupant;
}

// Get list of room occupants.
void XMPPChat_ChatOccupants(XmppClientState *state, XMPP_ChatOccupant ***eaOccupants, int domain, const char *room)
{
	U32 uSelfID = state ? state->uAccountID : 0;
	// Validate parameters.
	if (!eaOccupants || !room || !*room)
	{
		devassert(0);
		return;
	}

	switch (domain)
	{
	case XMPP_RoomDomain_Channel:
		XMPPChat_ChannelOccupants(eaOccupants, domain, room, uSelfID);
		break;
	case XMPP_RoomDomain_Guild:
	case XMPP_RoomDomain_Officer: // Guild and officer use the same callback
		XMPPChat_GuildChatOccupants(eaOccupants, domain, room, uSelfID);
		break;
	}
}

// Get information about a channel.
bool XMPPChat_ChannelInfo (SA_PARAM_NN_VALID XmppChannelInfo *pChannel, int domain, const char *room)
{
	bool success;
	char *channelName = NULL;
	ChatChannel *channel;

	// Handle special channels separately.
	if (domain != XMPP_RoomDomain_Channel)
		return XMPPChat_GuildChannelInfo(pChannel, domain, room);

	// Look up channel.
	estrStackCreate(&channelName);
	success = XMPPChat_GetChannelName(&channelName, domain, room);
	if (!success)
	{
		estrDestroy(&channelName);
		return false;
	}
	channel = channelFindByName(channelName);
	estrDestroy(&channelName);
	if (!channel)
		return false;

	// Copy channel node.
	estrCopy2(&pChannel->node, channel->name);

	// Copy channel name.
	if (domain == XMPP_RoomDomain_Channel)
		estrCopy(&pChannel->name, &pChannel->node);
	else
	{
		char *bang = strchr(pChannel->node, '!');
		devassert(bang);
		estrClear(&pChannel->name);
		estrConcat(&pChannel->name, pChannel->node, bang - pChannel->node);
		estrConcatf(&pChannel->name, ": %s", bang + 1);
		XMPPChat_DeescapeSpaces(pChannel->name);
	}

	// Copy channel membership count.
	pChannel->members = ea32Size(&channel->online);

	return true;
}

// Get the list of channels that might be interesting to a user.
void XMPPChat_ChannelsForUser(XmppChannelInfo ***eaChannels, int eDomain, XmppClientState *state)
{
	ChatUser *user;
	ChatGuild **guilds = NULL;
	ChatGuild **officerGuilds = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Validate
	if (!state || !eaChannels)
	{
		devassert(0);
		return;
	}

	// Clear output.
	eaClearStruct(eaChannels, parse_XmppChannelInfo);

	// Look up user.
	user = xmpp_chatuser_from_clientstate(state);
	devassert(user);
	if (!user)
		return;

	// Populate the list with available channels from this chat domain.
	switch (eDomain)
	{
		// List interesting channels to join.
		case XMPP_RoomDomain_Channel:
			// If requested, only list channels the user is watching.
			if (giXmppOnlyListWatching)
			{
				EARRAY_CONST_FOREACH_BEGIN(user->watching, i, n);
				{
					ChatChannel *channel = channelFindByID(user->watching[i]->channelID);
					enum XMPP_RoomDomain domain = XMPPChat_DomainFromChannelName(channel->name);
					if (domain == XMPP_RoomDomain_Channel)
					{
						XmppChannelInfo *channelInfo = StructCreate(parse_XmppChannelInfo);
						XMPPChat_GetXMPPChannelInfo(&channelInfo->node, &channelInfo->name, channel, XMPP_RoomDomain_Channel);
						channelInfo->members = ea32Size(&channel->online);
						eaPush(eaChannels, channelInfo);
					}
				}
				EARRAY_FOREACH_END;
			}
			// Otherwise, list all channels that have online users in them.
			else
			{
				ContainerIterator iter;
				Container *currCon;
				objInitContainerIteratorFromType(GLOBALTYPE_CHATCHANNEL, &iter);
				currCon = objGetNextContainerFromIterator(&iter);
				while (currCon)
				{
					ChatChannel *channel = (ChatChannel *)currCon->containerData;
					if (ea32Size(&channel->online))
					{
						enum XMPP_RoomDomain domain = XMPPChat_DomainFromChannelName(channel->name);
						if (domain == XMPP_RoomDomain_Channel)
						{
							XmppChannelInfo *channelInfo = StructCreate(parse_XmppChannelInfo);
							XMPPChat_GetXMPPChannelInfo(&channelInfo->node, &channelInfo->name, channel, XMPP_RoomDomain_Channel);
							channelInfo->members = ea32Size(&channel->online);
							eaPush(eaChannels, channelInfo);
						}
					}
					currCon = objGetNextContainerFromIterator(&iter);
				}
				objClearContainerIterator(&iter);
			}
		// List all of the user's guild channels.
		xcase XMPP_RoomDomain_Guild:
			GlobalChatServer_UserGuilds(&guilds, user);
			EARRAY_CONST_FOREACH_BEGIN(guilds, i, n);
			{
				ChatGuild *guild = guilds[i];
				GlobalChatLinkStruct *shardData = GlobalChatGetShardData(guild->iLinkID);
				if (shardData)
				{
					XmppChannelInfo *channelInfo = StructCreate(parse_XmppChannelInfo);
					ChatUser **members = NULL;
					XMPPChat_GetXMPPChannelInfo(&channelInfo->node, &channelInfo->name, guild, XMPP_RoomDomain_Guild);
					XMPPChat_GuildMembersOnline(&members, guild);
					channelInfo->members = eaSize(&members);
					eaDestroy(&members);
					eaPush(eaChannels, channelInfo);
				}
			}
			EARRAY_FOREACH_END;

		// List all of the channels of guilds for which the user is an officer.
		xcase XMPP_RoomDomain_Officer:
			GlobalChatServer_UserOfficerGuilds(&officerGuilds, user);
			EARRAY_CONST_FOREACH_BEGIN(officerGuilds, i, n);
			{
				ChatGuild *guild = officerGuilds[i];
				GlobalChatLinkStruct *shardData = GlobalChatGetShardData(guild->iLinkID);
				if (shardData)
				{
					XmppChannelInfo *channelInfo = StructCreate(parse_XmppChannelInfo);
					ChatUser **members = NULL;
					XMPPChat_GetXMPPChannelInfo(&channelInfo->node, &channelInfo->name, guild, XMPP_RoomDomain_Officer);
					XMPPChat_GuildOfficersOnline(&members, guild);
					channelInfo->members = eaSize(&members);
					eaDestroy(&members);
					eaPush(eaChannels, channelInfo);
				}
			}
			EARRAY_FOREACH_END;

		xdefault:
			devassert(0);
	}
}

/************************************************************************/
/* XMPP settings container transactions                                 */
/************************************************************************/

// Add a user to another user's XMPP roster.
AUTO_TRANSACTION ATR_LOCKS(user, ".xmppSettings");
enumTransactionOutcome trXmppRosterUpdate(ATR_ARGS, NOCONST(ChatUser) *user, ContainerID id)
{
	int index;

	// Create settings if necessary.
	if (!user->xmppSettings)
		user->xmppSettings = StructCreateNoConst(parse_XMPPChat_Settings);

	// Add user ID if not present.
	index = ea32Find(&user->xmppSettings->roster, id);
	if (index == -1)
		ea32Push(&user->xmppSettings->roster, id);

	return TRANSACTION_OUTCOME_SUCCESS;
}

// Remove a user from another user's roster.
AUTO_TRANSACTION ATR_LOCKS(user, ".xmppSettings");
enumTransactionOutcome trXmppRosterRemove(ATR_ARGS, NOCONST(ChatUser) *user, ContainerID id)
{
	// Create settings if necessary.
	if (!user->xmppSettings)
		user->xmppSettings = StructCreateNoConst(parse_XMPPChat_Settings);

	// Add user ID if not present.
	ea32FindAndRemoveFast(&user->xmppSettings->roster, id);
	return TRANSACTION_OUTCOME_SUCCESS;
}

#include "AutoGen/XMPP_Chat_c_ast.c"
