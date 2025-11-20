#include "chatdb.h"
#include "chatCommonStructs.h"

#include "ChatServer/chatShared.h"
#include "chatGlobal.h"
#include "chatGuild.h"
#include "chatUtilities.h"
#include "channels.h"

#include "XMPP_Chat.h"
#include "XMPP_ChatRoom.h"
#include "XMPP_ChatUtils.h"
#include "XMPP_Gateway.h"
#include "ChatServer/xmppShared.h"

#include "earray.h"
#include "estring.h"
#include "error.h"
#include "qsortG.h"
#include "StashTable.h"
#include "textparser.h"
#include "AutoGen/xmppShared_h_ast.h"

// Indexed by XMPP_RoomDomain enums
extern XmppServerState gXmppState;
extern ParseTable parse_XmppChatRoom[];
#define TYPE_parse_XmppChatRoom XmppChatRoom
extern ParseTable parse_XmppChatMember[];
#define TYPE_parse_XmppChatMember XmppChatMember
extern ParseTable parse_XmppClientRoom[];
#define TYPE_parse_XmppClientRoom XmppClientRoom

extern XmppServerState gXmppState;

void XMPPChat_GetXMPPChannelInfo (char **estrNode, char **estrDescription, void *data, int eDomain)
{
	GlobalChatLinkStruct *shardData = NULL;
	switch (eDomain)
	{
	case XMPP_RoomDomain_Channel:
		{
			ChatChannel *channel = (ChatChannel*) data;
			if (estrNode)
				estrCopy2(estrNode, channel->name);
			if (estrDescription)
				estrCopy2(estrDescription, channel->name);
		}
	xcase XMPP_RoomDomain_Guild:
	case XMPP_RoomDomain_Officer: // intentional fall-through
		{
			ChatGuild *guild = (ChatGuild*) data;
			shardData = GlobalChatGetShardData(guild->iLinkID);
			if (shardData) // this is set for Guild/Officer
			{
				const char *productName = GCS_GetProductDisplayName(shardData->pProductName);
				const char *shardName = GCS_GetShardDisplayName(shardData->pProductName, shardData->pShardName);
				if (estrNode) // default to the internal shard name if the display one isn't set
					estrPrintf(estrNode, "%s!%s", shardName ? shardName : shardData->pShardName, guild->pchName);

				if (estrDescription)
				{
					if (shardName)
						estrPrintf(estrDescription, "%s (%s): %s", productName, shardName, guild->pchName);
					else
						estrPrintf(estrDescription, "%s: %s", productName, guild->pchName);

					if (eDomain == XMPP_RoomDomain_Officer)
						estrConcatf(estrDescription, " Officers");
				}
			}
		}
	}
	if (estrNode)
		XMPPChat_EscapeSpaces(*estrNode);
}

XmppChatRoom * XMPPChatRoom_GetRoom (int eDomain, const char *room)
{
	if (!devassert(0 <= eDomain && eDomain < XMPP_RoomDomain_Max))
		return NULL;
	if (gXmppState.stChatRooms[eDomain])
	{
		XmppChatRoom *chatRoom = NULL;
		stashFindPointer(gXmppState.stChatRooms[eDomain], room, &chatRoom);
		return chatRoom;
	}
	return NULL;
}

// Join the ChatRoom; state is optional - if non-NULL, the join was from XMPP; if NULL, the join was from the game
// XmppChatRoom is NOT auto-created if state == NULL
void XMPPChatRoom_NotifyJoin (XmppClientState *state, ChatUser *user, int eDomain, const char *room)
{
	XmppChatRoom *chatRoom = XMPPChatRoom_GetRoom(eDomain, room);
	XmppChatMember *chatMember = NULL;
	bool bSendUpdates = false;

	if (!devassert(user))
		return;
	if (state)
	{
		XmppClientNode *node = xmpp_node_from_chatuser(user);
		if (!devassert(node))
			return;
		if (!chatRoom)
		{
			chatRoom = StructCreate(parse_XmppChatRoom);
			chatRoom->eDomain = eDomain;
			estrCopy2(&chatRoom->info.name, room);
			// doesn't fill in the description or # of members here
			eaIndexedEnable(&chatRoom->ppMembers, parse_XmppChatMember);
			if (!gXmppState.stChatRooms[eDomain])
				gXmppState.stChatRooms[eDomain] = stashTableCreateWithStringKeys(256, StashDeepCopyKeys);
			assert(stashAddPointer(gXmppState.stChatRooms[eDomain], room, chatRoom, false));
		}
		chatMember = eaIndexedGetUsingInt(&chatRoom->ppMembers, node->uAccountID);
		if (!chatMember)
		{
			chatMember = StructCreate(parse_XmppChatMember);
			chatMember->uID = node->uAccountID;
			eaIndexedAdd(&chatRoom->ppMembers, chatMember);
		}

		if (eaiFind(&chatMember->eaiResourceStateIDs, state->uStateID) == -1)
		{
			bSendUpdates = true;
			eaiPush(&chatMember->eaiResourceStateIDs, state->uStateID);
			//eaPush(&state->rooms, estrDup(room)); This is done in XMPP_Gateway
		}
	}
	else if (chatRoom)
	{
		bSendUpdates = true;
	}
	if (bSendUpdates)
	{
		XMPP_ChatOccupant *occupant = XMPPChat_ChatOccupant(eDomain, room, user->handle);
		XMPP_ClientStateList stateList = {0};

		if (!occupant)
		{
			occupant = StructCreate(parse_XMPP_ChatOccupant);
			occupant->nick = estrDup(user->handle);
			occupant->affiliation = XMPP_Affiliation_Member; // TODO fix this to get the proper affil for the occupant
		}
		// Send notify to all resources NOT INCLUDING this one
		EARRAY_CONST_FOREACH_BEGIN(chatRoom->ppMembers, i, n);
		{
			XmppChatMember *curMember = chatRoom->ppMembers[i];
			EARRAY_INT_CONST_FOREACH_BEGIN(curMember->eaiResourceStateIDs, j, o);
			{
				if (state && state->uStateID == curMember->eaiResourceStateIDs[j])
					continue;
				eaiPush(&stateList.eaiStateIDs, curMember->eaiResourceStateIDs[j]);
			}
			EARRAY_FOREACH_END;
		}
		EARRAY_FOREACH_END;

		XMPP_NotifyChatJoin(state ? state->uStateID : 0, eDomain, room, occupant, &stateList);
		StructDeInit(parse_XMPP_ClientStateList, &stateList);
		StructDestroy(parse_XMPP_ChatOccupant, occupant);
	}
}

// Leave the ChatRoom from everything
void XMPPChatRoom_NotifyLeaveAll (ChatUser *user, int eDomain, const char *room, bool bKicked)
{
	XmppClientNode *node = xmpp_node_from_chatuser(user);
	XmppChatRoom *chatRoom = XMPPChatRoom_GetRoom(eDomain, room);
	XmppChatMember *chatMember = NULL;

	if (!devassert(user))
		return;
	if (!chatRoom) // This means no one was online in XMPP in this room, early out
		return;
	if (node)
	{
		XMPP_ChannelInfoData occupant = {0};
		XMPP_ClientStateList stateList = {0};
		int idx = eaIndexedFindUsingInt(&chatRoom->ppMembers, node->uAccountID);
		if (idx >= 0)
			chatMember = eaRemove(&chatRoom->ppMembers, idx);

		estrCopy2(&occupant.name, user->handle);
		estrCopy2(&occupant.room, room);
		occupant.eDomain = eDomain;

		// Send notify to all resources for OTHER users
		EARRAY_CONST_FOREACH_BEGIN(chatRoom->ppMembers, i, n);
		{
			eaiAppend(&stateList.eaiStateIDs, &chatRoom->ppMembers[i]->eaiResourceStateIDs);
		}
		EARRAY_FOREACH_END;
		
		if (chatMember)
		{
			// Send notify to this user's resources, and remove room strings from the XmppClientStates
			EARRAY_INT_CONST_FOREACH_BEGIN(chatMember->eaiResourceStateIDs, i, o);
			{
				XmppClientState *state = eaIndexedGetUsingInt(&node->resources, chatMember->eaiResourceStateIDs[i]);
				if (state)
				{
					int idx2;
					eaiPush(&stateList.eaiStateIDs, state->uStateID);
					idx2 = eaIndexedFindUsingString(&state->rooms, room);
					if (idx2 >= 0)
					{
						StructDestroy(parse_XmppClientRoom, state->rooms[idx2]);
						eaRemove(&state->rooms, idx2);
					}
				}
			}
			EARRAY_FOREACH_END;
			// Destroy the XmppChatMember struct
			StructDestroy(parse_XmppChatMember, chatMember);
		}

		XMPP_NotifyChatPart(user->id, &occupant, &stateList, bKicked);
		StructDeInit(parse_XMPP_ClientStateList, &stateList);
		StructDeInit(parse_XMPP_ChannelInfoData, &occupant);
	}
}

static bool XMPPChatRoom_IsUserOnline(ChatUser *user, int eDomain, const char *room, void *data)
{
	switch (eDomain)
	{
	case XMPP_RoomDomain_Channel:
		{
			ChatChannel *channel = (ChatChannel*) data;
			return XMPPChat_ChannelSubscriberIsOnline(user, channel, room);
		}
	xcase XMPP_RoomDomain_Guild:
		{
			ChatGuild *guild = (ChatGuild*) data;
			return XMPPChat_GuildMemberIsOnline(user, guild, room);
		}
	xcase XMPP_RoomDomain_Officer:
		{
			ChatGuild *guild = (ChatGuild*) data;
			return XMPPChat_GuildOfficerIsOnline(user, guild, room);
		}
	}
	return false;
}

// User logged IN from the game for this chatroom; send notifies for appropriate chat channels - data is dependent on eDomain
// REDUNDANT - use XMPPChatRoom_NotifyJoin with a NULL XmppClientState
void XMPPChatRoom_NotifyGameJoin (ChatUser *user, int eDomain, void *data)
{
	char *room = NULL;
	XmppChatRoom *chatRoom = NULL;

	if (!devassert(user))
		return;
	estrStackCreate(&room);
	XMPPChat_GetXMPPChannelInfo(&room, NULL, data, eDomain);
	if (!devassert(room))
	{
		estrDestroy(&room);
		return;
	}
	chatRoom = XMPPChatRoom_GetRoom(eDomain, room);
	if (!chatRoom)
	{
		estrDestroy(&room);
		return;
	}
	// User was already online
	if (eaIndexedFindUsingInt(&chatRoom->ppMembers, user->id) >= 0)
	{
		estrDestroy(&room);
		return;
	}
	{
		XMPP_ChatOccupant *occupant = XMPPChat_ChatOccupant(eDomain, room, user->handle);
		XMPP_ClientStateList stateList = {0};

		if (!occupant)
		{
			occupant = StructCreate(parse_XMPP_ChatOccupant);
			occupant->nick = estrDup(user->handle);
			occupant->affiliation = XMPP_Affiliation_Member; // TODO fix this to get the proper affil for the occupant
		}

		EARRAY_CONST_FOREACH_BEGIN(chatRoom->ppMembers, i, n);
		{
			eaiAppend(&stateList.eaiStateIDs, &chatRoom->ppMembers[i]->eaiResourceStateIDs);
		}
		EARRAY_FOREACH_END;

		XMPP_NotifyChatJoin(0, eDomain, room, occupant, &stateList);
		StructDeInit(parse_XMPP_ClientStateList, &stateList);
		StructDestroy(parse_XMPP_ChatOccupant, occupant);
	}
}

// User logged OUT from the game(s) (for this chatroom); send notifies for appropriate chat channels - data is dependent on eDomain
void XMPPChatRoom_NotifyLeaveGame (ChatUser *user, int eDomain, void *data)
{
	char *room = NULL;
	XmppChatRoom *chatRoom = NULL;

	if (!devassert(user))
		return;
	estrStackCreate(&room);
	XMPPChat_GetXMPPChannelInfo(&room, NULL, data, eDomain);
	if (!devassert(room))
		return;
	chatRoom = XMPPChatRoom_GetRoom(eDomain, room);
	if (!chatRoom)
	{
		estrDestroy(&room);
		return;
	}

	if (!XMPPChatRoom_IsUserOnline(user, eDomain, room, data))
	{   // Sending offlines to all OTHER users
		XMPP_ChannelInfoData occupant = {0};
		XMPP_ClientStateList stateList = {0};

		estrCopy2(&occupant.name, user->handle);
		estrCopy2(&occupant.room, room);
		occupant.eDomain = eDomain;
		EARRAY_CONST_FOREACH_BEGIN(chatRoom->ppMembers, i, n);
		{
			eaiAppend(&stateList.eaiStateIDs, &chatRoom->ppMembers[i]->eaiResourceStateIDs);
		}
		EARRAY_FOREACH_END;

		XMPP_NotifyChatPart(user->id, &occupant, &stateList, false);
		StructDeInit(parse_XMPP_ClientStateList, &stateList);
		StructDeInit(parse_XMPP_ChannelInfoData, &occupant);
	}
	estrDestroy(&room);
}

//void XMPPChatRoom_NotifyJoin (XmppClientState *state, ChatUser *user, int eDomain, const char *room)
// Leave the ChatRoom from a single XMPP resource; data's type depends on eDomain
// State is optional
void XMPPChatRoom_NotifyLeaveSingle (XmppClientState *state, ChatUser *user, int eDomain, const char *room, void *data)
{
	XmppChatRoom *chatRoom = XMPPChatRoom_GetRoom(eDomain, room);
	XmppChatMember *chatMember = NULL;
	bool bXmppOnline = false;
	bool bStillOnline = false;
	bool bSendUpdate = true;
	int idx;

	if (!devassert(user && chatRoom))
		return;

	if (state)
	{
		XmppClientNode *node = xmpp_node_from_chatuser(user);
		if (!devassert(node))
			return;
		idx = eaIndexedFindUsingInt(&chatRoom->ppMembers, node->uAccountID);
		if (devassert(idx != -1))
		{
			
			chatMember = chatRoom->ppMembers[idx];
			eaiFindAndRemove(&chatMember->eaiResourceStateIDs, state->uStateID);
			if (eaiSize(&chatMember->eaiResourceStateIDs) == 0)
			{
				eaRemove(&chatRoom->ppMembers, idx);
				StructDestroy(parse_XmppChatMember, chatMember);
				chatMember = NULL;
			}
		}
		else
			bSendUpdate = false;
	}
	
	// Sending XMPP offline messages
	if (bSendUpdate)
	{
		XMPP_ChannelInfoData occupant = {0};
		XMPP_ClientStateList stateList = {0};

		estrCopy2(&occupant.name, user->handle);
		estrCopy2(&occupant.room, room);
		occupant.eDomain = eDomain;

		bStillOnline = XMPPChatRoom_IsUserOnline(user, eDomain, room, data);
		if (!bStillOnline)
		{
			// User is completely offline, send the notify to all OTHER resources
			EARRAY_CONST_FOREACH_BEGIN(chatRoom->ppMembers, i, n);
			{
				eaiAppend(&stateList.eaiStateIDs, &chatRoom->ppMembers[i]->eaiResourceStateIDs);
			}
			EARRAY_FOREACH_END;
		}
		// ALWAYS sends the offline notify to the passed-in state
		if (state)
			eaiPush(&stateList.eaiStateIDs, state->uStateID);

		XMPP_NotifyChatPart(user->id, &occupant, &stateList, false);
		StructDeInit(parse_XMPP_ClientStateList, &stateList);
		StructDeInit(parse_XMPP_ChannelInfoData, &occupant);
	}
}

// Returns the number of XmppClientStates that the are in the room
// Optionally returns the states if eaStates is set
static int xmpp_clientnode_inroom(const XmppClientNode *node, int eDomain, const char *room, XmppClientState ***eaStates)
{
	XmppChatRoom *chatRoom = XMPPChatRoom_GetRoom(eDomain, room);
	XmppChatMember *chatMember;

	if (!chatRoom)
		return 0;
	chatMember = eaIndexedGetUsingInt(&chatRoom->ppMembers, node->uAccountID);
	if (!chatMember)
		return 0;
	return eaiSize(&chatMember->eaiResourceStateIDs);
}

// Returns the ChatChannel for XMPP_RoomDomain_Channel
// Returns the ChatGuild for XMPP_RoomDomain_Guild/Officer
void *XMPPChat_GetChatRoomData(enum XMPP_RoomDomain eDomain, const char *pName)
{
	switch (eDomain)
	{
	case XMPP_RoomDomain_Channel:
		{
			char *channelName = NULL;
			ChatChannel *channel;
			
			estrStackCreate(&channelName);
			estrCopy2(&channelName, pName);
			XMPPChat_DeescapeSpaces(channelName);
			channel = channelFindByName(channelName);
			estrDestroy(&channelName);
			return channel;
		}
	case XMPP_RoomDomain_Guild:
	case XMPP_RoomDomain_Officer:
		{
			char *shardName = NULL, *guildName = NULL;
			ChatGuild *guild = NULL;
			// Separate channel name.
			estrStackCreate(&shardName);
			estrStackCreate(&guildName);
			if (XMPPChat_SeparateSpecialChannel(&shardName, &guildName, pName))
			{
				// Look up guild.
				int shardId = GetLocalChatLinkIdByShardName(shardName);
				int guildId = shardId ? GlobalChatGuildIdByName(shardId, guildName) : 0;

				if (guildId)
					guild = GlobalChatFindGuild(shardId, guildId);
			}
			estrDestroy(&guildName);
			estrDestroy(&shardName);
			return guild;
		}
	}
	return NULL;
}

// Create the Chat Server channel name from an XMPP channel name.
bool XMPPChat_GetChannelName(char **estrName, enum XMPP_RoomDomain eDomain, const char *pName)
{
	char *shardName = NULL;
	char *guildName = NULL;
	U32 shardId;
	U32 guildId;
	bool success = true;

	// Format the name depending on what domain its for.
	switch (eDomain)
	{
		// Normal channel
	case XMPP_RoomDomain_Channel:
		estrCopy2(estrName, pName);
		break;

		// Guild channel or guild officer channel
	case XMPP_RoomDomain_Guild:
	case XMPP_RoomDomain_Officer:

		// Separate channel name.
		estrStackCreate(&shardName);
		estrStackCreate(&guildName);
		success = XMPPChat_SeparateSpecialChannel(&shardName, &guildName, pName);
		if (!success)
			break;

		// Look up guild.
		shardId = GetLocalChatLinkIdByShardName(shardName);
		if (!shardId)
		{
			success = false;
			break;
		}
		guildId = GlobalChatGuildIdByName(shardId, guildName);
		if (!guildId)
		{
			success = false;
			break;
		}

		// Create channel name.
		estrPrintf(estrName, "%s%s%lu",
			eDomain == XMPP_RoomDomain_Officer ? OFFICER_CHANNEL_PREFIX : GUILD_CHANNEL_PREFIX,
			shardName,
			guildId);
		break;

	default:
		devassert(0);
	}
	estrDestroy(&shardName);
	estrDestroy(&guildName);

	// Deescape spaces.
	XMPPChat_DeescapeSpaces(*estrName);
	return success;
}

// Get the shard name and guild name from a special XMPP channel name.
bool XMPPChat_SeparateSpecialChannel(char **estrShard, char **estrGuild, const char *pName)
{
	const char *bang, *guild;

	// Find bang and validate.
	bang = strchr(pName, '!');
	if (!bang || bang == pName)
		return false;
	guild = bang + 1;
	if (!*guild)
		return false;

	// Copy shard name.
	estrClear(estrShard);
	estrConcat(estrShard, pName, bang - pName);

	// Copy guild name.
	estrCopy2(estrGuild, guild);
	XMPPChat_DeescapeSpaces(*estrGuild);

	return true;
}

// Find the shard name and guild identifier in a channel name.
static bool XMPPChat_SplitSpecialChannel(char **estrShardName, U32 *guildId, const char *pName)
{
	const char *shard;
	U32 *shardIds = NULL;
	int i;
	const char *guild = NULL;
	char *end;

	// Find shard name.
	if (strStartsWith(pName, GUILD_CHANNEL_PREFIX))
		shard = pName + (sizeof(GUILD_CHANNEL_PREFIX) - 1);
	else
		shard = pName + (sizeof(OFFICER_CHANNEL_PREFIX) - 1);

	// Find guild identifier.
	GetShardIds(&shardIds);
	for (i = 0; i != ea32Size(&shardIds); ++i)
	{
		const char *shardName = GetShardNameById(shardIds[i]);
		if (strStartsWith(shard, shardName))
		{
			guild = shard + strlen(shardName);
			break;
		}
	}
	ea32Destroy(&shardIds);
	if (!guild)
		return false;

	// Convert guild identifier.
	*guildId = strtoul(guild, &end, 10);
	if (!*guildId || *end)
		return false;

	// Copy shard.
	estrClear(estrShardName);
	estrConcat(estrShardName, shard, guild - shard);

	return !!*guildId;
}

// Look up the guild associated with a room name.
ChatGuild *XMPPChat_GetGuildByRoom(const char *room)
{
	char *shardName = NULL;
	char *guildName = NULL;
	bool success;
	U32 shardId;
	U32 guildId;
	ChatGuild *guild = NULL;

	// Separate shard and guild from room name.
	estrStackCreate(&shardName);
	estrStackCreate(&guildName);
	success = XMPPChat_SeparateSpecialChannel(&shardName, &guildName, room);
	if (!success)
	{
		estrDestroy(&shardName);
		estrDestroy(&guildName);
		return NULL;
	}

	// Look up shard.
	shardId = GetLocalChatLinkIdByShardName(shardName);
	if (!shardId)
	{
		estrDestroy(&shardName);
		estrDestroy(&guildName);
		return NULL;
	}

	// Look up guild.
	guildId = GlobalChatGuildIdByName(shardId, guildName);
	estrDestroy(&shardName);
	estrDestroy(&guildName);
	if (guildId)
		guild = GlobalChatFindGuild(shardId, guildId);

	return guild;
}

void XMPPChatRoom_GetClientNodes(SA_PARAM_NN_VALID XmppClientNode ***eaNodes, int eDomain, const char *room)
{
	XmppChatRoom *chatRoom = XMPPChatRoom_GetRoom(eDomain, room);
	if (!chatRoom)
		return;
	EARRAY_CONST_FOREACH_BEGIN(chatRoom->ppMembers, i, n);
	{
		XmppClientNode *node = xmpp_node_from_id(chatRoom->ppMembers[i]->uID);
		if (node)
			eaPush(eaNodes, node);
	}
	EARRAY_FOREACH_END;
}
void XMPPChatRoom_GetClientStates(SA_PARAM_NN_VALID XmppClientState ***eaStates, int eDomain, const char *room)
{
	XmppChatRoom *chatRoom = XMPPChatRoom_GetRoom(eDomain, room);
	if (!chatRoom)
		return;
	EARRAY_CONST_FOREACH_BEGIN(chatRoom->ppMembers, i, n);
	{
		XmppChatMember *curMember = chatRoom->ppMembers[i];
		EARRAY_INT_CONST_FOREACH_BEGIN(curMember->eaiResourceStateIDs, j, o);
		{
			XmppClientState *state = XmppServer_FindClientStateById(curMember->eaiResourceStateIDs[j]);
			if (state)
				eaPush(eaStates, state);
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;
}

//////////////////////////////////
// Functions for checking if the user is online in the room
bool XMPPChat_ChannelSubscriberIsOnline(ChatUser *user, ChatChannel *channel, const char *room)
{
	XmppClientNode *node = xmpp_node_from_chatuser(user);
	int iOnlineCount = eaSize(&user->ppPlayerInfo);
	if (node && xmpp_clientnode_inroom(node, XMPP_RoomDomain_Channel, room, NULL) > 0)
	{   // In an XMPP ChatRoom for this channel
		return true;
	}
	// Otherwise, look for online game shards
	if (iOnlineCount == 1)
	{   // Only online at one place, if it's XMPP, then we already know they weren't in a room
		return (user->ppPlayerInfo[0]->uChatServerID != XMPP_CHAT_ID);
	}
	return iOnlineCount != 0;
}

bool XMPPChat_GuildMemberIsOnline(ChatUser *user, ChatGuild *guild, const char *room)
{
	EARRAY_CONST_FOREACH_BEGIN(user->ppPlayerInfo, i, n);
	{
		if (user->ppPlayerInfo[i]->uChatServerID == XMPP_CHAT_ID)
		{
			XmppClientNode *node = xmpp_node_from_chatuser(user);
			if (node && xmpp_clientnode_inroom(node, XMPP_RoomDomain_Guild, room, NULL) > 0)
			{
				return true;
			}
		}
		else if (user->ppPlayerInfo[i]->uChatServerID == guild->iLinkID)
		{
			return (user->ppPlayerInfo[i]->iPlayerGuild == guild->iGuildID && user->ppPlayerInfo[i]->bCanGuildChat);
		}
	}
	EARRAY_FOREACH_END;
	return false;
}

bool XMPPChat_GuildOfficerIsOnline(ChatUser *user, ChatGuild *guild, const char *room)
{
	EARRAY_CONST_FOREACH_BEGIN(user->ppPlayerInfo, i, n);
	{
		if (user->ppPlayerInfo[i]->uChatServerID == XMPP_CHAT_ID)
		{
			XmppClientNode *node = xmpp_node_from_chatuser(user);
			if (node && xmpp_clientnode_inroom(node, XMPP_RoomDomain_Officer, room, NULL) > 0)
			{
				return true;
			}
		}
		else if (user->ppPlayerInfo[i]->uChatServerID == guild->iLinkID)
		{
			return (user->ppPlayerInfo[i]->iPlayerGuild == guild->iGuildID && user->ppPlayerInfo[i]->bIsOfficer);
		}
	}
	EARRAY_FOREACH_END;
	return false;
}

///////////////////////
// Guild Chat Room

// Get list of guild members online.
void XMPPChat_GuildMembersOnline(ChatUser ***eaMembers, ChatGuild *guild)
{
	char *room = NULL;
	estrStackCreate(&room);
	XMPPChat_GetXMPPChannelInfo(&room, NULL, guild, XMPP_RoomDomain_Guild);
	EARRAY_INT_CONST_FOREACH_BEGIN(guild->pGuildMembers, i, n);
	{
		ChatUser *user = userFindByContainerId(guild->pGuildMembers[i]);
		devassert(user);
		if (user && XMPPChat_GuildMemberIsOnline(user, guild, room))
		{
			eaPush(eaMembers, user);
		}
	}
	EARRAY_FOREACH_END;
	estrDestroy(&room);
}

// Get list of guild officers online.
void XMPPChat_GuildOfficersOnline(ChatUser ***eaMembers, ChatGuild *guild)
{
	char *room = NULL;
	estrStackCreate(&room);
	XMPPChat_GetXMPPChannelInfo(&room, NULL, guild, XMPP_RoomDomain_Officer);
	EARRAY_INT_CONST_FOREACH_BEGIN(guild->pGuildMembers, i, n);
	{
		ChatUser *user = userFindByContainerId(guild->pGuildMembers[i]);
		devassert(user);
		if (user && XMPPChat_GuildOfficerIsOnline(user, guild, room))
		{
			eaPush(eaMembers, user);
		}
	}
	EARRAY_FOREACH_END;
	estrDestroy(&room);
}

// Can the user join a guild channel.
XMPP_ChannelJoinStatus XMPPChat_GuildChatCanJoin(XmppClientState *state, XMPP_RoomDomain domain, const char *room)
{
	ChatUser *user;
	char *key = NULL;
	ChatGuild *guild;

	// Check: Look up user.
	user = xmpp_chatuser_from_clientstate(state);
	if (!devassert(user))
		return XMPP_ChannelJoinStatus_UnknownFailure;
	// Check: Look up guild.
	guild = XMPPChat_GetGuildByRoom(room);
	if (!guild)
		return XMPP_ChannelJoinStatus_UnknownFailure;

	// Make sure user is in guild, and an officer if necessary
	if (GlobalChatServer_IsInGuild(user, guild, domain == XMPP_RoomDomain_Officer))
		return XMPP_ChannelJoinStatus_Success;
	return XMPP_ChannelJoinStatus_PermissionDenied;
}

// Send a message to the XMPP guild channel from the game.
void XMPPChat_RecvGuildChatMessage (const ChatMessage *msg)
{
	char *shardName = NULL;
	U32 guildId;
	bool success;
	ChatGuild *guild;
	U32 shardId;
	char *key = NULL;
	XMPPChat_GuildMembers *membersStruct = NULL;
	XMPP_ChatRoomMessage chatMsg = {0};

	// Decode channel name.
	estrStackCreate(&shardName);
	success = XMPPChat_SplitSpecialChannel(&shardName, &guildId, msg->pchChannel);
	if (!success || !guildId)
	{
		estrDestroy(&shardName);
		return;
	}

	// Get shard identifier.
	shardId = GetLocalChatLinkIdByShardName(shardName);
	if (!shardId)
	{
		estrDestroy(&shardName);
		return;
	}

	// Look up the guild.
	guild = GlobalChatFindGuild(shardId, guildId);
	if (!guild)
	{
		estrDestroy(&shardName);
		return;
	}

	// Check if this is an officer channel.
	if (strStartsWith(msg->pchChannel, OFFICER_CHANNEL_PREFIX))
		chatMsg.eDomain = XMPP_RoomDomain_Officer;
	else 
		chatMsg.eDomain = XMPP_RoomDomain_Guild;
	XMPPChat_GetXMPPChannelInfo(&chatMsg.room, NULL, guild, chatMsg.eDomain);

	{
		XmppClientState **ppMembers = NULL;
		XMPP_ClientStateList list = {0};

		XMPPChatRoom_GetClientStates(&ppMembers, chatMsg.eDomain, chatMsg.room);

		estrCopy2(&chatMsg.nickname, msg->pFrom->pchHandle);
		if (msg->pchText)
			estrCopy2(&chatMsg.message, msg->pchText);
		if (msg->pchSubject)
			estrCopy2(&chatMsg.subject, msg->pchSubject);
		if (msg->pchThread)
			estrCopy2(&chatMsg.thread, msg->pchThread);

		// Send message to all members.
		EARRAY_CONST_FOREACH_BEGIN(ppMembers, i, n);
		{
			XmppClientState *state = ppMembers[i];
			eaiPush(&list.eaiStateIDs, ppMembers[i]->uStateID);
		}
		EARRAY_FOREACH_END;

		XMPP_NotifyChatMessageBatch(NULL, &list, &chatMsg);

		eaDestroy(&ppMembers);
		StructDeInit(parse_XMPP_ClientStateList, &list);
	}
	StructDeInit(parse_XMPP_ChatRoomMessage, &chatMsg);
	estrDestroy(&shardName);
}

// Send a message to a guild channel from XMPP.
bool XMPPChat_GuildChatMessage(XmppClientState *state, int domain, const char *room, const char *text)
{
	ChatUser *user;
	ChatGuild *guild;
	char *key = NULL;
	XmppClientState **ppMembers = NULL;
	bool officer = domain == XMPP_RoomDomain_Officer;
	int index;
	bool success;
	char *channel = NULL;
	ChatUserInfo *fromInfo;
	ChatMessage *message;

	// Look up user.
	user = xmpp_chatuser_from_clientstate(state);
	devassert(user);
	if (!user)
		return false;
	// Rate limit.
	if (chatRateLimiter(user))
		return false;
	// Look up guild.
	guild = XMPPChat_GetGuildByRoom(room);
	if (!guild)
		return false;

	XMPPChatRoom_GetClientStates(&ppMembers, domain, room);

	// Make sure this is a channel member.
	index = eaFind(&ppMembers, state);
	eaDestroy(&ppMembers);
	if (index == -1)
		return false;

	// Get channel name and type.
	estrStackCreate(&channel);
	success = XMPPChat_GetChannelName(&channel, domain, room);
	if (!success)
	{
		estrDestroy(&channel);
		return false;
	}

	// Send message.
	fromInfo = ChatCommon_CreateUserInfo(user->id, 0, user->handle, state->resource);
	message = ChatCommon_CreateMsg(fromInfo, NULL, kChatLogEntryType_Channel, NULL, text, NULL);
	StructDestroy(parse_ChatUserInfo, fromInfo);
	if (!message)
	{
		estrDestroy(&channel);
		devassert(0);
		return false;
	}
	message->pchChannel = estrDup(channel);
	message->eType = officer ? kChatLogEntryType_Officer : kChatLogEntryType_Guild;
	verbose_printf("Sending %s channel message \"%s\" to \"%s\".\n", officer ? "officer" : "guild", text, channel);
	channelSendSpecial(user, message, guild->iLinkID);
	StructDestroy(parse_ChatMessage, message);
	estrDestroy(&channel);
	return true;
}

// Leave a guild channel - nothing special here.
bool XMPPChat_GuildChatLeave(XmppClientState *state, int domain, const char *room)
{
	ChatGuild *guild;
	ChatUser *user = xmpp_chatuser_from_clientstate(state);
	
	// Look up guild.
	guild = XMPPChat_GetGuildByRoom(room);
	if (!guild || !user)
		return false;
	// Remove from list.
	XMPPChatRoom_NotifyLeaveSingle(state, user, domain, room, guild);
	return true;
}

// Get guild chat occupant information.
XMPP_ChatOccupant *XMPPChat_GuildChatOccupant(int domain, const char *room, const char *roomnick)
{
	XMPP_ChatOccupant *occupant;
	ChatUser *user;

	// Look up user.
	user = userFindByHandle(roomnick);
	if (!user)
		return NULL;

	// Create occupant data.
	occupant = StructCreate(parse_XMPP_ChatOccupant);
	occupant->nick = estrDup(user->handle);
	occupant->role = XMPP_Role_Participant;
	return occupant;
}

// Get list of guild room occupants.
void XMPPChat_GuildChatOccupants(XMPP_ChatOccupant ***eaOccupants, int domain, const char *room, U32 uSelfID)
{
	ChatUser **members = NULL;
	bool officer = domain == XMPP_RoomDomain_Officer;
	ChatGuild *guild;

	// Clear output.
	eaClear(eaOccupants);

	// Look up guild.
	guild = XMPPChat_GetGuildByRoom(room);
	if (!guild)
		return;

	// Get members list.
	if (officer)
		XMPPChat_GuildOfficersOnline(&members, guild);
	else
		XMPPChat_GuildMembersOnline(&members, guild);

	// Collect information for each occupant.
	EARRAY_CONST_FOREACH_BEGIN(members, i, n);
	{
		XMPP_ChatOccupant *occupant = XMPPChat_GuildChatOccupant(domain, room, members[i]->handle);
		if (occupant)
		{
			if (uSelfID) 
				occupant->own = members[i]->id == uSelfID;
			eaPush(eaOccupants, occupant);
		}
	}
	EARRAY_FOREACH_END;
	eaDestroy(&members);
}

/////////////////////////////
// Channel Chat Room

// Join a channel.
XMPP_ChannelJoinStatus XMPPChat_ChannelJoin(XmppClientState *state, XMPP_RoomDomain domain, const char *room)
{
	ChatUser *user;
	ChatChannel *channel;
	int result;
	char *roomDeescaped = NULL;
	char *channelName = NULL;
	bool success;
	enum XMPP_ChannelJoinStatus status;

	// Get channel name and type.
	estrStackCreate(&roomDeescaped);
	estrCopy2(&roomDeescaped, room);
	XMPPChat_DeescapeSpaces(roomDeescaped);
	estrStackCreate(&channelName);
	success = XMPPChat_GetChannelName(&channelName, domain, roomDeescaped);
	estrDestroy(&roomDeescaped);
	if (!success)
	{
		estrDestroy(&channelName);
		return XMPP_ChannelJoinStatus_UnknownFailure;
	}

	// Look up user.
	user = xmpp_chatuser_from_clientstate(state);
	if (!devassert(user))
	{
		estrDestroy(&channelName);
		return XMPP_ChannelJoinStatus_UnknownFailure;
	}

	channel = channelFindByName(channelName);
	if (userFindWatching(user, channel))
	{
		result = CHATRETURN_CHANNEL_ALREADYMEMBER;
	}
	else
	{	// Join channel.
		result = channelCreateOrJoin(user, channelName, 0, false);
	}

	// Translate result.
	switch (result)
	{
	case CHATRETURN_FWD_NONE:
	case CHATRETURN_CHANNEL_ALREADYMEMBER:
		status = XMPP_ChannelJoinStatus_Success;
		break;
	case CHATRETURN_CHANNEL_WATCHINGMAX:
		status = XMPP_ChannelJoinStatus_MaxChannels;
		break;
	case CHATRETURN_CHANNEL_FULL:
		status = XMPP_ChannelJoinStatus_Full;
		break;
	case CHATRETURN_INVALIDNAME:
		status = XMPP_ChannelJoinStatus_InvalidName;
		break;
	default:
		status = XMPP_ChannelJoinStatus_UnknownFailure;
	}

	// If this resource caused this user to join the channel, make a note of it so that when this resource leaves, we will leave it.
	if (status == XMPP_ChannelJoinStatus_Success && result != CHATRETURN_CHANNEL_ALREADYMEMBER)
	{
		int i = (int)eaBFind(state->firstJoinedRooms, strCmp, channelName);
		eaInsert(&state->firstJoinedRooms, estrDup(channelName), i);
	}
	estrDestroy(&channelName);
	return status;
}

// Send a message to the channel from XMPP
bool XMPPChat_ChannelMessage(XmppClientState *state, int domain, const char *room, const char *text)
{
	ChatUser *user;
	ChatUserInfo *fromInfo;
	ChatMessage *message;
	char *channel = NULL;
	bool success;

	// Look up user.
	user = xmpp_chatuser_from_clientstate(state);
	devassert(user);
	if (!user)
		return false;
	// Rate limit.
	if (chatRateLimiter(user))
		return false;

	// Get channel name and type.
	estrStackCreate(&channel);
	success = XMPPChat_GetChannelName(&channel, domain, room);
	if (!success)
		return false;

	// Send message.
	fromInfo = ChatCommon_CreateUserInfo(user->id, 0, user->handle, state->resource);
	message = ChatCommon_CreateMsg(fromInfo, NULL, kChatLogEntryType_Channel, NULL, text, NULL);
	StructDestroy(parse_ChatUserInfo, fromInfo);
	if (!message)
	{
		devassert(0);
		return false;
	}
	message->pchChannel = estrDup(channel);
	verbose_printf("Sending channel message \"%s\" to \"%s\".\n", text, channel);
	channelSend(user, message);
	StructDestroy(parse_ChatMessage, message);
	estrDestroy(&channel);
	return true;
}

bool XMPPChat_ChannelLeave(XmppClientState *state, int domain, const char *room)
{
	ChatUser *user;
	int result;
	ChatChannel *channel;
	char *channelName = NULL;
	bool success;
	int idx;

	// Look up user.
	user = xmpp_chatuser_from_clientstate(state);
	devassert(user);
	if (!user)
		return false;
	// Get channel name and type.
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

	// Check if this was a channel that this resource joined first, and if so, leave it, for the entire ChatUser.
	if (state->firstJoinedRooms)
	{
		idx = (int)eaBFind(state->firstJoinedRooms, strCmp, channel->name);
		if (idx < eaSize(&state->firstJoinedRooms) && !stricmp(state->firstJoinedRooms[idx], channel->name))
		{
			result = channelLeave(user, channel->name, false);
			estrDestroy(&state->firstJoinedRooms[idx]);
			eaRemove(&state->firstJoinedRooms, idx);
			return result == CHATRETURN_FWD_NONE;
		}
	}
	XMPPChatRoom_NotifyLeaveSingle(state, user, domain, room, channel);
	return true;
}

void XMPPChat_ChannelOccupants(XMPP_ChatOccupant ***eaOccupants, int domain, const char *room, U32 uSelfID)
{
	ChatUser **ppSubscribers = NULL;
	char *channelName = NULL;
	bool success;
	ChatChannel *channel;

	// Look up channel.
	estrStackCreate(&channelName);
	success = XMPPChat_GetChannelName(&channelName, domain, room);
	if (!success)
	{
		estrDestroy(&channelName);
		return;
	}
	channel = channelFindByName(channelName);
	estrDestroy(&channelName);
	if (!channel)
		return;

	// Get list of channel occupants.
	channelGetListOnline(&ppSubscribers, channel->name);

	// Create XMPP occupant list.
	eaSetCapacity(eaOccupants, eaSize(&ppSubscribers));
	EARRAY_CONST_FOREACH_BEGIN(ppSubscribers, i, n);
	{
		ChatUser *user = ppSubscribers[i];
		if (XMPPChat_ChannelSubscriberIsOnline(user, channel, room))
		{
			XMPP_ChatOccupant *occupant;
			Watching *watching = userFindWatching(user, channel);

			if (!devassert(watching)) // Something is weird! They're not really in the channel!
				continue;
			occupant = StructCreate(parse_XMPP_ChatOccupant);

			// Copy user's channel data.
			occupant->nick = estrDup(user->handle);
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
			if (uSelfID)
				occupant->own = user->id == uSelfID;
			eaPush(eaOccupants, occupant);
		}
	}
	EARRAY_FOREACH_END;
	eaDestroy(&ppSubscribers);
}
