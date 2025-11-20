// Performs bulk of XMPP processing.
// It knows very little about the Global Chat Server, but a lot about XMPP.  It gets stanzas from XMPP_Parsing, and sends data back to the
// client with XMPP_Generation.  It manages the actual network connections with XMPP_Net.  To communicates with the rest of the
// Global Chat Server, it uses XMPP_Chat.

#include <string.h>

#include "crypt.h"
#include "earray.h"
#include "estring.h"
#include "error.h"
#include "net.h"
#include "rand.h"
#include "sock.h"
#include "StashTable.h"
#include "StringUtil.h"
#include "timing.h"
#include "chatGlobal.h"
#include "chatCommon.h"
#include "users.h"
#include "ChatServer/chatShared.h"
#include "XMPP_Chat.h"
#include "XMPP_Gateway.h"
#include "XMPP_ChatUtils.h"
#include "ChatServer/xmppShared.h"
#include "AutoGen/XMPP_Structs_h_ast.h"
#include "AutoGen/xmppShared_h_ast.h"

// Checks the link the XMPP command came in on and make sure it's the XMPP Server
#define XMPP_VALIDATE_LINK(xmppLink) if (!devassert(GetLocalChatLinkID(xmppLink) == XMPP_CHAT_ID)) return;
// Checks to make sure the XmppClientState is valid and has a resource specified (required element on login)
#define XMPP_VALIDATE_STATE(state) if (!state || !state->resource) return;
#define XMPP_VALIDATE_ID(xmppID) (xmppID ? xmppID : "\"\"")

// Self-discovered domain name to use for XMPP.
static char *gJIDDomain = NULL;

// Domain name forced by a command-line parameter override.
char gJIDDomainForced[512] = "";
AUTO_CMD_STRING(gJIDDomainForced, setXmppDomain) ACMD_CMDLINE;

// Set to true if XMPP should be disabled.
int giDisableXmpp = 0;
AUTO_CMD_INT(giDisableXmpp, DisableXmpp);

// Set to true if you want to disable XMPP guilds
bool sbXmppGuildDisable = false;
AUTO_CMD_INT(sbXmppGuildDisable, DisableXmppGuilds);

// Return true if XMPP has been disabled or is not connected.
bool XMPP_IsDisabled(void)
{
	if (GlobalChatGetShardData(XMPP_CHAT_ID) == NULL)
		return true;
	return !!giDisableXmpp;
}

NetLink *XMPP_GetLink(void)
{
	GlobalChatLinkStruct *linkData = GlobalChatGetShardData(XMPP_CHAT_ID);
	return linkData ? linkData->localChatLink : NULL;
}

// Return true if this is a well-formed JID.  Note that it may not actually exist.
bool XMPP_ValidateJid(const char *jid)
{
	const char *at, *domain, *slash;
	if (!jid)
		return false;
	at = strchr(jid, '@');
	if (!at)
		return false;
	domain = at + 1;
	if (strchr(domain, '@'))
		return false;
	if (at - jid == 0)
		return false;
	if (!strlen(domain))
		return false;
	slash = strchr(domain, '/');
	if (slash && slash - at == 0)
		return false;
	if (slash && !strlen(slash + 1))
		return false;
	return true;
}

// Decompose a JID into its component parts.
struct JidComponents XMPP_JidDecompose(const char *jid)
{
	struct JidComponents result = {NULL, 0, NULL, 0, NULL, 0};
	const char *ptr, *domainstart, *resourcestart = NULL;

	// Validate parameters.
	devassert(jid && *jid);
	if (!jid || !*jid)
		return result;

	// Find node.
	ptr = strchr(jid, '@');
	if (ptr)
	{
		result.node = jid;
		result.nodelen = ptr - jid;
		domainstart = ptr + 1;
	}
	else
		domainstart = jid;

	// Find domain.
	ptr = strchr(domainstart, '/');
	if (ptr)
		resourcestart = ptr + 1;
	else
		ptr = domainstart + strlen(domainstart);
	result.domain = domainstart;
	result.domainlen = ptr - domainstart;

	// Find resource.
	if (resourcestart && *resourcestart)
	{
		result.resource = resourcestart;
		result.resourcelen = strlen(resourcestart);
	}

	return result;
}

// Compare two strings for equality XMPP-style.
bool XmppStringEqual(const char *lhs, size_t lhsLen, const char *rhs, size_t rhsLen)
{
	if (lhsLen != rhsLen)
		return false;
	return !memcmp(lhs, rhs, lhsLen);
}

// Compare two strings for equality XMPP-style, without case.
static bool XmppStringEqualCaseInsensitive(const char *lhs, size_t lhsLen, const char *rhs, size_t rhsLen)
{
	size_t i;
	if (lhsLen != rhsLen)
		return false;
	for (i = 0; i < lhsLen; i = UTF8GetNextCodepoint(lhs + i) - lhs)
		if (!(lhs[i] == rhs [i] || isascii(lhs[i]) && isascii(rhs[i]) && tolower(lhs[i]) == tolower(rhs[i])))
			return false;
	return true;
}

// Return true if two JIDs are equal, and false otherwise.
bool JidEqual(const char *lhs, const char *rhs)
{
	struct JidComponents lhsComponents, rhsComponents;

	// Validate parameters.
	if (!lhs || !*lhs || !rhs || !*rhs)
		return false;

	// Break each JID into components.
	lhsComponents = XMPP_JidDecompose(lhs);
	rhsComponents = XMPP_JidDecompose(rhs);

	// If the two JIDs do not have the same component parts, they are not equal.
	if (lhsComponents.nodelen != rhsComponents.nodelen
		|| lhsComponents.domainlen != rhsComponents.domainlen
		|| lhsComponents.resourcelen != rhsComponents.resourcelen)
		return false;

	// Compare each component.
	return XmppStringEqualCaseInsensitive(lhsComponents.node, lhsComponents.nodelen, rhsComponents.node, rhsComponents.nodelen)
		&& XmppStringEqualCaseInsensitive(lhsComponents.domain, lhsComponents.domainlen, rhsComponents.domain, rhsComponents.domainlen)
		&& XmppStringEqual(lhsComponents.resource, lhsComponents.resourcelen, rhsComponents.resource, rhsComponents.resourcelen);
}

// Return true if two JIDs are equal, ignoring any resource components.
bool BareJidEqual(const char *lhs, const char *rhs)
{
	struct JidComponents lhsComponents, rhsComponents;

	// Validate parameters.
	devassert(lhs && *lhs && rhs && *rhs);
	if (!lhs || !*lhs || !rhs || !*rhs)
		return false;

	// Break each JID into components.
	lhsComponents = XMPP_JidDecompose(lhs);
	rhsComponents = XMPP_JidDecompose(rhs);

	// If the two JIDs do not have the same component parts, they are not equal.
	if (lhsComponents.nodelen != rhsComponents.nodelen
		|| lhsComponents.domainlen != rhsComponents.domainlen)
		return false;

	// Compare each component.
	return XmppStringEqualCaseInsensitive(lhsComponents.node, lhsComponents.nodelen, rhsComponents.node, rhsComponents.nodelen)
		&& XmppStringEqualCaseInsensitive(lhsComponents.domain, lhsComponents.domainlen, rhsComponents.domain, rhsComponents.domainlen);
}

// Return true if two domains are equal.
bool DomainEqual(const char *lhs, const char *rhs)
{
	struct JidComponents lhsComponents, rhsComponents;

	// Validate parameters.
	devassert(lhs && *lhs && rhs && *rhs);
	if (!lhs || !*lhs || !rhs || !*rhs)
		return false;

	// Break each JID into components.
	lhsComponents = XMPP_JidDecompose(lhs);
	rhsComponents = XMPP_JidDecompose(rhs);

	// Compare each component.
	return XmppStringEqualCaseInsensitive(lhsComponents.domain, lhsComponents.domainlen, rhsComponents.domain, rhsComponents.domainlen);
}

// Return true if this is a full JID.
static bool IsFullJid(const char *jid)
{
	struct JidComponents components;

	// Make sure the JID is valid first.
	if (!jid || !XMPP_ValidateJid(jid))
		return false;

	// Decompose JID.
	components = XMPP_JidDecompose(jid);

	// Return true if it has a resource.
	return !!components.resourcelen;
}

// Make an EString that is the bare JID of some other full JID.
static void MakeBareJid(char **estrBareJid, const char *fullJid)
{
	struct JidComponents components;

	// Validate parameters.
	devassert(estrBareJid);
	if (!estrBareJid)
		return;
	estrClear(estrBareJid);
	if (!fullJid || !XMPP_ValidateJid(fullJid))
		return;

	// Decompose JID.
	components = XMPP_JidDecompose(fullJid);

	// Build bare JID.
	estrConcat(estrBareJid, components.node, (int)components.nodelen);
	estrConcat(estrBareJid, "@", 1);
	estrConcat(estrBareJid, components.domain, (int)components.domainlen);
}

// Make an EString that is the node from a valid JID.
static void MakeNodeFromJid(char **estrNode, const char *jid)
{
	struct JidComponents components;

	// Validate parameters.
	devassert(estrNode);
	if (!estrNode)
		return;
	estrClear(estrNode);
	if (!jid || !XMPP_ValidateJid(jid))
		return;

	// Decompose JID.
	components = XMPP_JidDecompose(jid);

	// Build bare JID.
	estrConcat(estrNode, components.node, (int)components.nodelen);
}

// Make an EString that is the resource from a valid JID.
static void MakeResourceFromJid(char **estrNode, const char *jid)
{
	struct JidComponents components;

	// Validate parameters.
	devassert(estrNode);
	if (!estrNode)
		return;
	estrClear(estrNode);
	if (!jid || !XMPP_ValidateJid(jid))
		return;

	// Decompose JID.
	components = XMPP_JidDecompose(jid);

	// Build bare JID.
	estrConcat(estrNode, components.resource, (int)components.resourcelen);
}

void XMPP_SendStanzaError(NetLink *xmppLink, U32 uStateID, XMPP_StanzaErrorCondition error, XMPP_StanzaErrorType type, 
						  const char *element, const char *id, const char *text)
{
	XMPP_StanzaError stanzaError = {0};
	char *structString = NULL;

	StructInit(parse_XMPP_StanzaError, &stanzaError);
	stanzaError.error = error;
	stanzaError.type = type;
	stanzaError.element = element;
	stanzaError.text = text;

	XMPP_WRITE_STRUCT(&structString, parse_XMPP_StanzaError, &stanzaError);
	sendCommandToLinkEx(xmppLink, "XMPP_ReceiveStanzaError", "%d %s %s", 
		uStateID, id, structString);
	estrDestroy(&structString);
}

// Special Stanza Error function for sending ChatRoom errors for initializing the "to" field
static void XMPP_SendChatRoomStanzaError(NetLink *xmppLink, U32 uStateID, XMPP_StanzaErrorCondition error, XMPP_StanzaErrorType type, 
										 XMPP_RoomDomain eDomain, const char *room, 
										 const char *element, const char *xmppID, const char *text)
{
	XMPP_StanzaError stanzaError = {0};
	char *structString = NULL;

	StructInit(parse_XMPP_StanzaError, &stanzaError);
	stanzaError.error = error;
	stanzaError.type = type;
	stanzaError.element = element;
	stanzaError.text = text;
	stanzaError.eDomain = eDomain;
	stanzaError.room = room;

	XMPP_WRITE_STRUCT(&structString, parse_XMPP_StanzaError, &stanzaError);
	sendCommandToLinkEx(xmppLink, "XMPP_ReceiveStanzaError", "%d %s %s", 
		uStateID, XMPP_VALIDATE_ID(xmppID), structString);
	estrDestroy(&structString);
}

// Process chat-directed presence updates. Only handles joins.
AUTO_COMMAND_REMOTE;
void XMPP_ChannelProcessPresenceUpdate(U32 uStateID, const char *xmppID, XMPP_PresenceData *pData)
{
	XMPP_ChannelJoinStatus result;
	XMPP_RoomDomain domain = pData->eDomain;
	const char *room = pData->room;
	XmppClientState *state = XmppServer_FindClientStateById(uStateID);
	NetLink *xmppLink = chatServerGetCommandLink();

	XMPP_VALIDATE_LINK(xmppLink);
	XMPP_VALIDATE_STATE(state);

	// Get the domain.
	if (domain == XMPP_RoomDomain_Unknown)
	{
		XMPP_SendStanzaError(xmppLink, uStateID, StanzaError_ServiceUnavailable, Stanza_Cancel, "presence", 
			xmppID, NULL);
		return;
	}

	// Try to join channel
	result = XMPPChat_ChatJoin(state, domain, room);
	if (result != XMPP_ChannelJoinStatus_Success)
	{
		const char *text = NULL;
		XMPP_StanzaErrorCondition condition = StanzaError_ServiceUnavailable;
		switch (result)
		{
			case XMPP_ChannelJoinStatus_MaxChannels:
				text = "Your account is on too many channels.";
				break;
			case XMPP_ChannelJoinStatus_Full:
				text = "This channel is full.";
				break;
			case XMPP_ChannelJoinStatus_InvalidName:
				text = "This channel name is not allowed";
				condition = StanzaError_NotAllowed;
				break;
			case XMPP_ChannelJoinStatus_PermissionDenied:
				text = "You cannot join this channel";
				condition = StanzaError_RegistrationRequired;
				break;
		}
		XMPP_SendChatRoomStanzaError(xmppLink, uStateID, condition, Stanza_Cancel, domain, room, "presence", xmppID, text);
	}

	// Send updates if it worked.
	if (result == XMPP_ChannelJoinStatus_Success)
	{
		XMPP_ChatRoomRoster roster = {0};
		int foundRoom;
		char *structString = NULL;

		XMPPChat_ChatOccupants(state, &roster.ppOccupants, domain, room);
		if (pData->resource)
			roster.bRewrite = !!strcmp(pData->resource, XMPPChat_NodeName(state));
		roster.eDomain = domain;
		roster.room = strdup(room);
		
		XMPP_WRITE_STRUCT(&structString, parse_XMPP_ChatRoomRoster, &roster);
		StructDeInit(parse_XMPP_ChatRoomRoster, &roster);
		sendCommandToLinkEx(xmppLink, "XMPP_ReceiveChatRoomRoster", "%d %s %s", 
			uStateID, XMPP_VALIDATE_ID(xmppID), structString);
		estrDestroy(&structString);

		foundRoom = eaIndexedFindUsingString(&state->rooms, room);
		if (foundRoom == -1)
		{
			XmppClientRoom *clientRoom = StructCreate(parse_XmppClientRoom);
			estrCopy2(&clientRoom->room, room);
			clientRoom->eDomain = domain;
			eaPush(&state->rooms, clientRoom);
		}
	}
}

// Process chat-directed unavailability updates.
AUTO_COMMAND_REMOTE;
void XMPP_ProcessChannelPresenceUnavailable(U32 uStateID, const char *xmppID, XMPP_PresenceData *pData)
{
	bool success;
	int foundRoom;
	XmppClientState *state = XmppServer_FindClientStateById(uStateID);
	NetLink *xmppLink = chatServerGetCommandLink();

	XMPP_VALIDATE_LINK(xmppLink);
	XMPP_VALIDATE_STATE(state);

	// Remove the room from the resource's list.
	foundRoom = eaIndexedFindUsingString(&state->rooms, pData->room);
	if (foundRoom != -1)
	{
		StructDestroy(parse_XmppClientRoom, state->rooms[foundRoom]);
		eaRemove(&state->rooms, foundRoom);
	}

	// Leave channel.
	success = XMPPChat_ChatLeave(state, pData->eDomain, pData->room);

	// Send an error if it didn't work out.
	if (!success)
	{
		XMPP_SendStanzaError(xmppLink, uStateID, StanzaError_ServiceUnavailable, Stanza_Cancel, "presence", xmppID, NULL);
	}
}

// Process chat-directed message.
AUTO_COMMAND_REMOTE;
void XMPP_ChannelProcessMessage(U32 uStateID, const char *xmppID, XMPP_ChatRoomMessage *msg)
{
	bool success;
	XmppClientState *state = XmppServer_FindClientStateById(uStateID);
	NetLink *xmppLink = chatServerGetCommandLink();

	XMPP_VALIDATE_LINK(xmppLink);
	XMPP_VALIDATE_STATE(state);
	success = XMPPChat_ChatMessage(state, msg->eDomain, msg->room, msg->message);
	if (!success)
		XMPP_SendStanzaError(xmppLink, uStateID, StanzaError_NotAllowed, Stanza_Modify, "message", xmppID, NULL);
}
// Process chat room subject change
AUTO_COMMAND_REMOTE;
void XMPP_ChannelProcessSubject(U32 uStateID, const char *xmppID, XMPP_ChatRoomMessage *msg)
{
	bool success;
	XmppClientState *state = XmppServer_FindClientStateById(uStateID);
	NetLink *xmppLink = chatServerGetCommandLink();

	XMPP_VALIDATE_LINK(xmppLink);
	XMPP_VALIDATE_STATE(state);
	success = XMPPChat_ChatSubject(state, msg->eDomain, msg->room, msg->subject);
	if (!success)
		XMPP_SendStanzaError(xmppLink, uStateID, StanzaError_NotAllowed, Stanza_Modify, "message", xmppID, NULL);
}

// Process room-directed information service discovery.
AUTO_COMMAND_REMOTE;
void XMPP_ChannelProcessChannelDiscoInfo(U32 uStateID, XMPP_RoomDomain domain, const char *room, const char *xmppID)
{
	XmppClientState *state = XmppServer_FindClientStateById(uStateID);
	NetLink *xmppLink = chatServerGetCommandLink();
	bool success;
	XMPP_ChannelInfoData infoData = {0};
	XmppChannelInfo info = {0};
	char *structString = NULL;

	XMPP_VALIDATE_LINK(xmppLink);
	XMPP_VALIDATE_STATE(state);

	// Look up channel information.
	success = XMPPChat_ChannelInfo(&info, domain, room);
	if (!success)
	{
		XMPP_SendStanzaError(xmppLink, uStateID, StanzaError_ItemNotFound, Stanza_Cancel, "iq", xmppID, NULL);
		return;
	}

	infoData.eDomain = domain;
	infoData.room = estrDup(room);
	estrPrintf(&infoData.name, "%s (%d)", info.name, info.members);
	XMPP_WRITE_STRUCT(&structString, parse_XMPP_ChannelInfoData, &infoData);

	// Send information.
	sendCommandToLinkEx(xmppLink, "XMPP_ReceiveChannelDiscoInfo", 
		"%d %s %s", uStateID, XMPP_VALIDATE_ID(xmppID), structString);
	estrDestroy(&structString);
	StructDeInit(parse_XMPP_ChannelInfoData, &infoData);
	StructDeInit(parse_XmppChannelInfo, &info);
}

// Make a roster item for a roster list or roster push.
XMPP_RosterItem *XMPP_MakeRosterItem(const char *jid, const char *name, bool friends, char **guilds, bool ask)
{
	XMPP_RosterItem *item = StructCreate(parse_XMPP_RosterItem);

	// Create item.
	if (jid)
	{
		item->jid = estrDup(jid);
		estrReplaceOccurrences(&item->jid, " ", "+");
	}
	item->name = name ? strdup(name) : NULL;
	eaCopyEStrings(&guilds, &item->group);
	if (friends)
		eaInsert(&item->group, estrDup("Friends"), 0);
	item->subscription = (eaSize(&guilds) || friends) ? XMPP_RosterSubscriptionState_Both : XMPP_RosterSubscriptionState_None;
	item->ask = ask ? XMPP_RosterSubscribeState_Subscribe : XMPP_RosterSubscribeState_None;
	
	return item;
}

// Get the client's unique identifier.
U64 XMPP_UniqueClientId(const XmppClientState *state)
{
	return state->uStateID;
}

// Get the client's unique identifier, as a string.
char *XMPP_UniqueClientIdStr(const XmppClientState *state, char *buf, int buf_size)
{
	int n = sprintf_s(SAFESTR2(buf), "%"FORM_LL"u", XMPP_UniqueClientId(state));
	return n ? buf : NULL;
}

// Send a roster push, with explicit parameters, to one or more XmppClientStates
void XMPP_RosterPushUser(NetLink *xmppLink, XmppClientState *state, XMPP_ClientStateList *stateList, const char *jid)
{
	XMPP_RosterItem *item;
	XmppClientNode *node;
	char *listString = NULL;
	char *itemString = NULL;

	// Validate parameters.
	devassert(state && jid);
	if (!state || !jid)
		return;
	node = XMPPChat_ClientNode(state);
	if (!node)
		return;
	// Get roster information.
	item = XMPPChat_GetFriend(node, NULL, jid);
	if (!item)
		return;

	XMPP_WRITE_STRUCT(&listString, parse_XMPP_ClientStateList, stateList);
	XMPP_WRITE_STRUCT(&itemString, parse_XMPP_RosterItem, item);
	// Send roster push.
	sendCommandToLinkEx(xmppLink, "XMPP_RosterPushUser", "%s %s", itemString, listString);
	StructDestroy(parse_XMPP_RosterItem, item);
	estrDestroy(&itemString);
	estrDestroy(&listString);
}

// Send a presence update
void XMPP_SendPresenceUpdate(XmppClientState *state, const char *xmppID, XMPP_PresenceData *pData)
{
	char *structString = NULL;
	XMPP_WRITE_STRUCT(&structString, parse_XMPP_PresenceData, pData);
	sendCommandToLinkEx(XMPP_GetLink(), "XMPP_SendPresence", 
		"%d %s %s", 
		state->uStateID, XMPP_VALIDATE_ID(xmppID), structString);
	estrDestroy(&structString);
}

// Only uses avail, priority, and status fields
AUTO_COMMAND_REMOTE;
void XMPP_ProcessRosterPresenceUpdate(U32 uStateID, const char *id, XMPP_PresenceData *pData)
{
	XmppClientState *state = XmppServer_FindClientStateById(uStateID);
	if (!devassert(state))
		return;

	// Save presence information.
	state->availability = pData->avail;
	free(state->presenceStatus);
	if (pData->status)
		state->presenceStatus = strdup(pData->status);
	else
		state->presenceStatus = NULL;
	free(state->presenceId);
	state->presenceId = strdup(id);
	state->priority = pData->priority;

	// If this is the first presence, send initial presence.
	if (!state->accepting_presence && state->availability != XMPP_PresenceAvailability_Unavailable)
	{
		state->accepting_presence = true;
		XMPPChat_InitialPresence(state);
	}

	// Broadcast presence.
	XMPPChat_SetPresence(state, state->availability, state->presenceStatus);	
}

// Process presence unavailability update.
AUTO_COMMAND_REMOTE;
void XMPP_ProcessRosterPresenceUnavailable(U32 uStateID, const char *id, XMPP_PresenceData *pData)
{
	XmppClientState *state = XmppServer_FindClientStateById(uStateID);
	// Validate.
	if (!devassert(state))
		return;
	// Update presence.
	XMPPChat_SetPresence(state, XMPP_PresenceAvailability_Unavailable, pData->status);
}

// Process presence subscribe attempt.
AUTO_COMMAND_REMOTE;
void XMPP_ProcessPresenceSubscribe(U32 uStateID, const char *xmppID, const char *to)
{
	bool result;
	XmppClientState *state = XmppServer_FindClientStateById(uStateID);
	NetLink *xmppLink = chatServerGetCommandLink();
	XMPP_VALIDATE_LINK(xmppLink);
	XMPP_VALIDATE_STATE(state);

	// Send friend request.
	result = XMPPChat_AddFriend(state, to);
	if (!result)
		sendCommandToLinkEx(xmppLink, "XMPP_ReceiveUnsubscribe", "%d %s %s", uStateID, xmppID, to);
}

// Process presence subscription confirmation.
AUTO_COMMAND_REMOTE;
void XMPP_ProcessPresenceSubscribed(U32 uStateID, const char *xmppID, const char *to)
{
	bool result;
	XmppClientState *state = XmppServer_FindClientStateById(uStateID);
	NetLink *xmppLink = chatServerGetCommandLink();
	XMPP_VALIDATE_LINK(xmppLink);
	XMPP_VALIDATE_STATE(state);
	
	// Accept friend request.
	result = XMPPChat_AcceptFriend(state, to);
	if (!result)
		sendCommandToLinkEx(xmppLink, "XMPP_ReceiveUnsubscribe", "%d %s %s", uStateID, xmppID, to);
}

// Process presence unsubscription attempt.
AUTO_COMMAND_REMOTE;
void XMPP_ProcessPresenceUnsubscribe(U32 uStateID, const char *xmppID, const char *to)
{
	XmppClientState *state = XmppServer_FindClientStateById(uStateID);
	NetLink *xmppLink = chatServerGetCommandLink();
	XMPP_VALIDATE_LINK(xmppLink);
	XMPP_VALIDATE_STATE(state);

	XMPPChat_RemoveFriend(state, to);
	sendCommandToLinkEx(xmppLink, "XMPP_ReceiveUnsubscribe", "%d %s %s", uStateID, xmppID, to);
}

void XMPP_SendPrivateMessage(XmppClientState *state, const char *xmppID, const char *fromHandle, const char *fromResource, 
							 const char *body, XMPP_MessageType type)
{
	NetLink *xmppLink = XMPP_GetLink();
	if (linkConnected(xmppLink))
	{
		XMPP_Message msg = {0};
		char *structString = NULL;
		msg.body = body;
		msg.fromHandle = fromHandle;
		msg.fromResource = fromResource;
		msg.type = type;

		XMPP_WRITE_STRUCT(&structString, parse_XMPP_Message, &msg);
		sendCommandToLinkEx( xmppLink, "XMPP_RecvPrivateMessage", "%d %s %s",
			state->uStateID, XMPP_VALIDATE_ID(xmppID), structString);
		estrDestroy(&structString);
	}
}

AUTO_COMMAND_REMOTE;
void XMPP_ProcessPrivateMessage(U32 uStateID, const char *xmppID, const char *to, XMPP_Message *msg)
{
	XmppClientState *state = XmppServer_FindClientStateById(uStateID);
	bool result;
	NetLink *xmppLink = chatServerGetCommandLink();

	XMPP_VALIDATE_LINK(xmppLink);
	XMPP_VALIDATE_STATE(state);

	result = XMPPChat_SendPrivateMessage(state, to, msg->subject, msg->body, msg->thread, xmppID, msg->type);
	if (!result)
		XMPP_SendStanzaError(xmppLink, uStateID, StanzaError_ServiceUnavailable, Stanza_Modify, "message", xmppID, NULL);
}

// Process item discovery.
AUTO_COMMAND_REMOTE;
void XMPP_ProcessDiscoItems(U32 uStateID, const char *from, const char *xmppID)
{
	XmppClientState *state = XmppServer_FindClientStateById(uStateID);
	NetLink *xmppLink = chatServerGetCommandLink();
	XMPP_DiscoItem *pItem = NULL;
	XMPP_DiscoItemList itemList = {0};
	char *structString = NULL;

	XMPP_VALIDATE_LINK(xmppLink);
	XMPP_VALIDATE_STATE(state);

	// Requesting list of available services.
	pItem = StructCreate(parse_XMPP_DiscoItem);
	pItem->eDomain = XMPP_RoomDomain_Channel;
	estrCopy2(&pItem->name, "Channels");
	eaPush(&itemList.ppItems, pItem);

	if (!sbXmppGuildDisable && XMPPChat_IsOfficer(state))
	{
		pItem = StructCreate(parse_XMPP_DiscoItem);
		pItem->eDomain = XMPP_RoomDomain_Guild;
		estrCopy2(&pItem->name, "Guilds");
		eaPush(&itemList.ppItems, pItem);

		pItem = StructCreate(parse_XMPP_DiscoItem);
		pItem->eDomain = XMPP_RoomDomain_Officer;
		estrCopy2(&pItem->name, "Guild Officers");
		eaPush(&itemList.ppItems, pItem);
	}
	else if (!sbXmppGuildDisable && XMPPChat_HasGuilds(state))
	{
		pItem = StructCreate(parse_XMPP_DiscoItem);
		pItem->eDomain = XMPP_RoomDomain_Guild;
		estrCopy2(&pItem->name, "Guilds");
		eaPush(&itemList.ppItems, pItem);
	}

	XMPP_WRITE_STRUCT(&structString, parse_XMPP_DiscoItemList, &itemList);
	StructDeInit(parse_XMPP_DiscoItemList, &itemList);

	sendCommandToLinkEx(xmppLink, "XMPP_SendDiscoItemResponse", "%d %d %s %s", 
		uStateID, XMPP_RoomDomain_Unknown, XMPP_VALIDATE_ID(xmppID), structString);
	estrDestroy(&structString);
}

// Process chat-directed item list service discovery.
AUTO_COMMAND_REMOTE;
void XMPP_ChannelProcessDiscoItems(U32 uStateID, XMPP_RoomDomain domain, const char *from, const char *xmppID)
{
	XmppClientState *state = XmppServer_FindClientStateById(uStateID);
	NetLink *xmppLink = chatServerGetCommandLink();
	XMPP_DiscoItemList itemList = {0};
	int i, size;
	XmppChannelInfo **channels = NULL;
	char *structString = NULL;

	XMPP_VALIDATE_LINK(xmppLink);
	XMPP_VALIDATE_STATE(state);

	// Get interesting channels.
	XMPPChat_ChannelsForUser(&channels, domain, state);

	// Create list.
	size = eaSize(&channels);
	for (i = 0; i != size; ++i)
	{
		XmppChannelInfo *channel = channels[i];
		XMPP_DiscoItem *pItem = StructCreate(parse_XMPP_DiscoItem);

		estrPrintf((char **)&pItem->jid, "%s", channel->node);
		XMPPChat_EscapeSpaces((char*) pItem->jid);
		pItem->eDomain = domain;
		estrPrintf((char **)&pItem->name, "%s (%d)", channel->name, channel->members);
		eaPush(&itemList.ppItems, pItem);
	}

	// Send list.
	XMPP_WRITE_STRUCT(&structString, parse_XMPP_DiscoItemList, &itemList);
	StructDeInit(parse_XMPP_DiscoItemList, &itemList);

	sendCommandToLinkEx(xmppLink, "XMPP_SendDiscoItemResponse", "%d %d %s %s", 
		uStateID, domain, XMPP_VALIDATE_ID(xmppID), structString);
	estrDestroy(&structString);

	eaDestroyStruct(&channels, parse_XmppChannelInfo);
}

AUTO_COMMAND_REMOTE;
void XMPP_Login(U32 uXMPPStateID, const char *xmppID, XMPP_LoginData *pLoginData)
{
	NetLink *xmppLink = chatServerGetCommandLink();

	XMPP_VALIDATE_LINK(xmppLink);
	// new login - XmppClientState is being created, do NOT validate the state here
	if (!pLoginData || pLoginData->uAccountID == 0 || nullStr(pLoginData->accountName) || nullStr(pLoginData->displayName))
	{
		XMPP_SendStanzaError(xmppLink, uXMPPStateID, StanzaError_BadRequest, Stanza_Modify, "iq", xmppID, "Incomplete account information supplied");
		return;
	}
	userXmppCreateAndLogin(uXMPPStateID, XMPP_VALIDATE_ID(xmppID), pLoginData->uAccountID, pLoginData->accountName, pLoginData->displayName, pLoginData->resource, pLoginData->iAccessLevel);
}

AUTO_COMMAND_REMOTE;
void XMPP_Logout(U32 uXMPPStateID, const char *handle)
{
	NetLink *xmppLink = chatServerGetCommandLink();
	ChatUser *user = userFindByHandle(handle);
	XmppClientState *state = XmppServer_FindClientStateById(uXMPPStateID);

	XMPP_VALIDATE_LINK(xmppLink);
	XMPP_VALIDATE_STATE(state);
	if (user)
	{
		XMPPChat_Logout(user, state);
		XmppServer_RemoveClientState(user, state);
	}
}

// Process roster requests.
AUTO_COMMAND_REMOTE;
void XMPP_ProcessRosterGet(U32 uStateID, const char *xmppID)
{
	char *clientjid = NULL;
	XMPP_RosterItemList rosterList = {0};
	char **requests = NULL;
	XmppClientState *state = XmppServer_FindClientStateById(uStateID);
	NetLink *xmppLink = chatServerGetCommandLink();
	XMPP_ClientStateList stateList = {0};
	char *structString = NULL;

	XMPP_VALIDATE_LINK(xmppLink);
	XMPP_VALIDATE_STATE(state);
	// Get friends list.
	XMPPChat_GetFriends(state, &rosterList.ppRoster);

	// Send roster.
	XMPP_WRITE_STRUCT(&structString, parse_XMPP_RosterItemList, &rosterList);
	StructDeInit(parse_XMPP_RosterItemList, &rosterList);
	sendCommandToLinkEx(xmppLink, "XMPP_SendRoster", "%d %s %s", uStateID, xmppID, structString);
	estrDestroy(&structString);

	// Send outstanding presence subscription requests.
	XMPPChat_IncomingFriendRequests(&requests, state);
	EARRAY_CONST_FOREACH_BEGIN(requests, i, n);
	{
		eaPush(&stateList.eaHandles, strdup(requests[i]));
	}
	EARRAY_FOREACH_END;

	XMPP_WRITE_STRUCT(&structString, parse_XMPP_ClientStateList, &stateList);
	sendCommandToLinkEx(xmppLink, "XMPP_NotifyFriendRequestsForUser", "%d %s", 
		uStateID, structString); 
	estrDestroy(&structString);
	eaDestroyEString(&requests);
}

// Process roster adds and updates.
AUTO_COMMAND_REMOTE;
void XMPP_ProcessRosterUpdate(U32 uStateID, const char *xmppID, XMPP_RosterUpdate *update)
{
	bool success;
	XmppClientState *state = XmppServer_FindClientStateById(uStateID);
	NetLink *xmppLink = chatServerGetCommandLink();

	XMPP_VALIDATE_LINK(xmppLink);
	XMPP_VALIDATE_STATE(state);

	// Add this contact to the roster.
	success = XMPPChat_RosterUpdate(state, update->jid, update->ppGroups);

	// Send response.
	if (success)
		sendCommandToLinkEx(xmppLink, "XMPP_ReceiveRosterResult", "%d %s", uStateID, XMPP_VALIDATE_ID(xmppID));
	else
		XMPP_SendStanzaError(xmppLink, state->uStateID, StanzaError_NotAcceptable, Stanza_Modify, "iq", xmppID, NULL);

	// Push new item to all nodes.
	if (success)
	{
		XmppClientNode *node = XMPPChat_ClientNode(state);
		XMPP_ClientStateList list = {0};
		EARRAY_CONST_FOREACH_BEGIN(node->resources, i, n);
		{
			eaiPush(&list.eaiStateIDs, node->resources[i]->uStateID);
		}
		EARRAY_FOREACH_END;

		XMPP_RosterPushUser(xmppLink, state, &list, update->jid);
		StructDeInit(parse_XMPP_ClientStateList, &list);
	}
}

// Process roster removes.
AUTO_COMMAND_REMOTE;
void XMPP_ProcessRosterRemove(U32 uStateID, const char *xmppID, const char *jid)
{
	bool bSuccess, bRemovedFriend;
	XmppClientState *state = XmppServer_FindClientStateById(uStateID);
	NetLink *xmppLink = chatServerGetCommandLink();
	ChatUser *user;

	XMPP_VALIDATE_LINK(xmppLink);
	XMPP_VALIDATE_STATE(state);
	if (!state)
		return;
	user = xmpp_chatuser_from_clientstate(state);
	if (!devassert(user))
		return;

	// Remove this contact from the roster.
	bRemovedFriend = XMPPChat_RemoveFriend(state, jid);
	bSuccess = bRemovedFriend && XMPPChat_RosterRemove(state, jid);

	// Send response.
	if (bSuccess)
		sendCommandToLinkEx(xmppLink, "XMPP_ReceiveRosterResult", "%d %s", uStateID, XMPP_VALIDATE_ID(xmppID));
	else
		XMPP_SendStanzaError(xmppLink, state->uStateID, StanzaError_ItemNotFound, Stanza_Modify, "iq", xmppID, NULL);
	if (bSuccess)
	{
		XmppClientNode *node = XMPPChat_ClientNode(state);
		XMPP_ClientStateList list = {0};
		struct JidComponents jidComp = XMPP_JidDecompose(jid);
		EARRAY_CONST_FOREACH_BEGIN(node->resources, i, n);
		{
			eaiPush(&list.eaiStateIDs, node->resources[i]->uStateID);
		}
		EARRAY_FOREACH_END;
		XMPP_NotifyFriendRemove(jidComp.node, &list);
		StructDeInit(parse_XMPP_ClientStateList, &list);
	}
}

// Notify an XMPP user that he has an incoming friend request.
void XMPP_NotifyFriendRequest(const char *fromHandle, XMPP_ClientStateList *stateList)
{
	NetLink *xmppLink = XMPP_GetLink();
	char *structString = NULL;
	XMPP_WRITE_STRUCT(&structString, parse_XMPP_ClientStateList, stateList);
	sendCommandToLinkEx(xmppLink, "XMPP_NotifyFriendRequest", "%s %s", 
		fromHandle, structString);
	estrDestroy(&structString);
}

// Notify an XMPP user that he has a new friend.
void XMPP_NotifyNewFriend(const char *fromHandle, XMPP_ClientStateList *stateList, XMPP_PresenceData *pData)
{
	NetLink *xmppLink = XMPP_GetLink();
	char *structString = NULL;
	XMPP_WRITE_STRUCT(&structString, parse_XMPP_ClientStateList, stateList);
	sendCommandToLinkEx(xmppLink, "XMPP_NotifyNewFriend", "%s %s", 
		fromHandle, structString);

	/* TODO optimizations
	char *presenceString = NULL;
	XMPP_WRITE_STRUCT(&presenceString, parse_XMPP_PresenceData, pData);
	sendCommandToLinkEx(xmppLink, "XMPP_SendPresenceToList", "%s %s %s", 
		"", structString, presenceString);
	estrDestroy(&presenceString);*/

	estrDestroy(&structString);
}

// Notify an XMPP user that he has lost a friend.
void XMPP_NotifyFriendRemove(const char *fromHandle, XMPP_ClientStateList *stateList)
{
	NetLink *xmppLink = XMPP_GetLink();
	char *structString = NULL;
	XMPP_WRITE_STRUCT(&structString, parse_XMPP_ClientStateList, stateList);
	sendCommandToLinkEx(xmppLink, "XMPP_NotifyFriendRemove", "%s %s", 
		fromHandle, structString);
	estrDestroy(&structString);
}

// A user has spoken in a channel.
void XMPP_NotifyChatMessage(XmppClientState *state, int domain, const char *room, const char *roomnick, 
							const char *message, const char *subject, const char *id)
{
	XMPP_ChatRoomMessage msg = {0};
	char *structString = NULL;

	msg.eDomain = domain;
	estrCopy2(&msg.room, room);
	estrCopy2(&msg.nickname, roomnick);
	estrCopy2(&msg.message, message);
	estrCopy2(&msg.subject, subject);

	XMPP_WRITE_STRUCT(&structString, parse_XMPP_ChatRoomMessage, &msg);
	sendCommandToLinkEx( XMPP_GetLink(), "XMPP_NotifyChatMessage", "%d %s %s", 
		state->uStateID, XMPP_VALIDATE_ID(id), structString);
	estrDestroy(&structString);
	StructDeInit(parse_XMPP_ChatRoomMessage, &msg);
}

void XMPP_NotifyChatMessageBatch(const char *xmppID, XMPP_ClientStateList *list, XMPP_ChatRoomMessage *msg)
{
	char *listString = NULL;
	char *msgString = NULL;

	XMPP_WRITE_STRUCT(&listString, parse_XMPP_ClientStateList, list);
	XMPP_WRITE_STRUCT(&msgString, parse_XMPP_ChatRoomMessage, msg);

	sendCommandToLinkEx( XMPP_GetLink(), "XMPP_NotifyChatMessageBatch", "%s %s %s", 
		XMPP_VALIDATE_ID(xmppID), listString, msgString);
	estrDestroy(&listString);
	estrDestroy(&msgString);
}

// A user has joined a channel.
void XMPP_NotifyChatJoin(U32 uStateID, XMPP_RoomDomain eDomain, const char *room, XMPP_ChatOccupant *occupant, XMPP_ClientStateList *list)
{
	char *occupantString = NULL, *rosterString = NULL;
	XMPP_WRITE_STRUCT(&occupantString, parse_XMPP_ChatOccupant, occupant);
	XMPP_WRITE_STRUCT(&rosterString, parse_XMPP_ClientStateList, list);
	sendCommandToLinkEx( XMPP_GetLink(), "XMPP_NotifyChatJoin", "%d %d %s %s %s", 
		uStateID, eDomain, room, occupantString, rosterString);
	estrDestroy(&rosterString);
	estrDestroy(&occupantString);
}

// A user has left a channel.
void XMPP_NotifyChatPart(U32 uAccountID, XMPP_ChannelInfoData *info, XMPP_ClientStateList *list, bool bKicked)
{
	char *infoString = NULL, *rosterString = NULL;
	NetLink *xmppLink = XMPP_GetLink();
	XMPP_WRITE_STRUCT(&infoString, parse_XMPP_ChannelInfoData, info);
	XMPP_WRITE_STRUCT(&rosterString, parse_XMPP_ClientStateList, list);
	sendCommandToLinkEx(xmppLink, "XMPP_NotifyChatPart", "%d %s %s %d", 
		uAccountID, infoString, rosterString, bKicked);
	estrDestroy(&rosterString);
	estrDestroy(&infoString);
}
