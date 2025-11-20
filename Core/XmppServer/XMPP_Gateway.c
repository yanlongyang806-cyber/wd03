// Performs bulk of XMPP processing.
// It knows very little about the Global Chat Server, but a lot about XMPP.  It gets stanzas from XMPP_Parsing, and sends data back to the
// client with XMPP_Generation.  It manages the actual network connections with XMPP_Net.  To communicates with the rest of the
// Global Chat Server, it uses XMPP_Chat.

#include <string.h>

#include "accountnet.h"
#include "crypt.h"
#include "earray.h"
#include "estring.h"
#include "error.h"
#include "logging.h"
#include "net.h"
#include "rand.h"
#include "sock.h"
#include "StashTable.h"
#include "StringUtil.h"
#include "timing.h"
#include "XMPP_Gateway.h"
#include "XMPP_Generation.h"
#include "XMPP_Login.h"
#include "XMPP_Net.h"
#include "XMPP_Connect.h"
#include "XMPP_ChatOutgoing.h"

#include "ChatServer/xmppShared.h"
#include "AutoGen/xmppShared_h_ast.h"
#include "AutoGen/xmppTypes_h_ast.h"

// Count of stanzas received.
static U64 siStanzasReceived = 0;

// StashTable of Client States
static StashTable stClientStates = NULL;
#define XMPPCLIENTSTATE_DEFAULT_STASH_SIZE 10000

void XMPP_ClearServerData(void)
{
	if (stClientStates)
	{
		StashTableIterator iter = {0};
		StashElement elem = NULL;

		stashGetIterator(stClientStates, &iter);
		while (stashGetNextElement(&iter, &elem))
		{
			XmppClientState *state = stashElementGetPointer(elem);
			if (state)
			{
				if (state->link)
				{
					xmpp_Disconnect(state->link, "Server disconnected");
					if (!xmpp_ClientIsConnected(state->link))
						xmpp_DestroyClientLink(state->link);
				}
			}
		}
		// Redundant extra clear; xmpp_Disconnect should clear everything out
		stashTableClearStruct(stClientStates, NULL, parse_XmppClientState);
	}
}

// Create and/or return the escaped display name
const char *xmpp_client_get_escaped_name(const XmppClientState *state)
{
	if (!state || !state->ticket || !state->ticket->displayName)
		return NULL;
	if (!state->cachedEscapedName)
	{
		estrCopy2((char**) &state->cachedEscapedName, state->ticket->displayName);
		estrReplaceOccurrences((char**) &state->cachedEscapedName, " ", "+");
	}
	return state->cachedEscapedName;
}

// Return the full JID of an XMPP client.
const char *xmpp_client_jid(const XmppClientState *state)
{
	// Return a null pointer if this client is not completely logged in.
	if (!XMPP_IsLoggedIn(state))
		return NULL;

	// Return the cached full JID if there is one.
	if (state->cachedFullJid)
		return state->cachedFullJid;

	// Otherwise, build the JID.
	xmpp_client_bare_jid(state);
	estrCopy((char **)&state->cachedFullJid, (char **)&state->cachedBareJid);
	estrConcatf((char **)&state->cachedFullJid, "/%s", state->resource);
	return state->cachedFullJid;
}

// Return the bare JID of an XMPP client.
const char *xmpp_client_bare_jid(const XmppClientState *state)
{
	// Return a null pointer if this client is not completely logged in.
	if (!XMPP_IsLoggedIn(state))
		return NULL;

	// Build the bare JID if necessary.
	if (!state->cachedBareJid)
	{
		devassert(state->ticket->displayName);
		estrPrintf((char **)&state->cachedBareJid, "%s@%s", xmpp_client_get_escaped_name(state), XMPP_Domain());
	}
	return state->cachedBareJid;
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
static bool XmppStringEqual(const char *lhs, size_t lhsLen, const char *rhs, size_t rhsLen)
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

AUTO_COMMAND_REMOTE;
void XMPP_ReceiveChannelDiscoInfo(U32 uStateID, const char *xmppID, XMPP_ChannelInfoData *cinfoData)
{
	static const char *const chatFeatures[] = {
		"http://jabber.org/protocol/muc",
		"muc_public",
		"muc_persistent",
		"muc_open",
		"muc_nonanonymous",
		"muc_unmoderated",
		"muc_unsecured"
	};
	const char *jid;
	char *fromDomain = NULL;
	XmppClientState *state = XMPP_FindClientById(uStateID);
	if (!XMPP_IsLoggedIn(state))
		return;

	jid = xmpp_client_jid(state);
	estrStackCreate(&fromDomain);
	estrPrintf(&fromDomain, "%s@%s", cinfoData->room, XMPP_RoomDomainName(cinfoData->eDomain));

	xmpp_s_senddiscoinfo(state->generator, xmppID, fromDomain, jid, chatFeatures, sizeof(chatFeatures)/sizeof(*chatFeatures),
		cinfoData->name, XMPP_DiscoItemType_Conference);
	estrDestroy(&fromDomain);
}

// Process chat-directed information service discovery.
static void XMPP_ChannelProcessDiscoInfo(XmppClientState *state, const char *to, const char *from, const char *id)
{
	static const char *const chatFeatures[] = {
		"http://jabber.org/protocol/muc"
	};
	enum XMPP_RoomDomain domain;

	// Get the domain.
	domain = XMPP_GetRoomDomainEnum(to);
	if (domain == XMPP_RoomDomain_Unknown)
	{
		XMPP_GenerateStanzaError(state->generator, StanzaError_ServiceUnavailable, Stanza_Cancel, 
			XMPP_Domain(), NULL, "presence", id, NULL);
		return;
	}

	// Reply to chat domain queries.
	if (JidEqual(to, XMPP_RoomDomainName(domain)))
	{
		const char *name = "";
		switch (domain)
		{
			case XMPP_RoomDomain_Channel:
				name = "Channels";
				break;
			case XMPP_RoomDomain_Guild:
				name = "Guilds";
				break;
			case XMPP_RoomDomain_Officer:
				name = "Guild Officers";
				break;
			default:
				devassert(0);
		}
		xmpp_s_senddiscoinfo(state->generator, id, XMPP_RoomDomainName(domain), from, chatFeatures, sizeof(chatFeatures)/sizeof(*chatFeatures),
			name, XMPP_DiscoItemType_Conference);
	}
	// Reply to requests for information about a channel.
	else if (DomainEqual(to, XMPP_RoomDomainName(domain)) && !IsFullJid(to))
	{
		char *room = NULL;
		estrStackCreate(&room);
		MakeNodeFromJid(&room, to);
		XMPP_SendGlobalChatQuery( GetXmppGlobalChatLink(), "XMPP_ChannelProcessChannelDiscoInfo", 
			"%d %d \"%s\" %s", 
			state->uStateID, domain, room, id);
		estrDestroy(&room);
	}
	// Reject requests for information about a channel user.
	else if (DomainEqual(to, XMPP_RoomDomainName(domain)) && IsFullJid(to))
		XMPP_GenerateStanzaError(state->generator, StanzaError_NotAllowed, Stanza_Cancel, XMPP_Domain(), from, "iq", id, NULL);
}

// Make a roster item for a roster list or roster push.
XMPP_RosterItem *XMPP_MakeRosterItem(const char *jid, const char *name, bool friends, char **guilds, bool ask)
{
	XMPP_RosterItem *item = StructCreate(parse_XMPP_RosterItem);

	// Validate parameters.
	devassert(jid);
	if (!jid)
		return NULL;

	// Create item.
	item->jid = estrDup(jid);
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
	if (devassert(state && state->link))
		return XMPP_NetUniqueClientId(state->link);
	else
		return 0;
}

// Get the client's unique identifier, as a string.
char *XMPP_UniqueClientIdStr(const XmppClientState *state, char *buf, int buf_size)
{
	int n = sprintf_s(SAFESTR2(buf), "%"FORM_LL"u", XMPP_UniqueClientId(state));
	return n ? buf : NULL;
}

// Total number of connected XMPP clients.
unsigned long XMPP_TotalConnections(void)
{
	return XMPP_NetTotalConnections();
}

// Total number of XMPP bytes sent.
U64 XMPP_BytesSent(void)
{
	return XMPP_NetBytesSent();
}

// Total number of XMPP bytes received.
U64 XMPP_BytesReceived(void)
{
	return XMPP_NetBytesReceived();
}

// Total number of XMPP stanzas sent.
U64 XMPP_StanzasSent(void)
{
	return XMPP_StanzasGenerated();
}

// Total number of XMPP stanzas received.
U64 XMPP_StanzasReceived(void)
{
	return siStanzasReceived;
}

// Send a roster push with one or more items.
void XMPP_RosterPushItems(XmppClientState *state, XMPP_RosterItem **ppItems)
{
	// Validate parameters.
	devassert(state && eaSize(&ppItems) > 0);
	if (!XMPP_IsLoggedIn(state) || eaSize(&ppItems) == 0)
		return;
	// Send roster push.
	xmpp_s_sendrosterpush(state->generator, NULL, xmpp_client_jid(state), ppItems);
}

// Send a roster push.
void XMPP_RosterPush(XmppClientState *state, const char *jid, const char *name, bool friends, char **guilds, bool ask)
{
	XMPP_RosterItem **eaItem = NULL;

	eaSetSize(&eaItem, 1);
	// Validate parameters.
	devassert(state && jid);
	if (!XMPP_IsLoggedIn(state) || !jid)
		return;

	// Create roster item for this user.
	eaItem[0] = XMPP_MakeRosterItem(jid, name, friends, guilds, ask);
	if (!eaItem[0])
	{
		eaDestroy(&eaItem);
		devassert(0);
		return;
	}

	// Send roster push.
	XMPP_RosterPushItems(state, eaItem);
	StructDestroy(parse_XMPP_RosterItem, eaItem[0]);
	eaDestroy(&eaItem);
}

// Send a roster push, with explicit parameters, to multiple users
AUTO_COMMAND_REMOTE;
void XMPP_RosterPushUser(XMPP_RosterItem *item, XMPP_ClientStateList *list)
{
	int i, size;
	XMPP_RosterItem **ppItems = NULL;
	size = eaiSize(&list->eaiStateIDs);
	eaSetSize(&ppItems, 1);
	ppItems[0] = item;
	for (i=0; i<size; i++)
	{
		XmppClientState *state = XMPP_FindClientById(list->eaiStateIDs[i]);
		if (XMPP_IsLoggedIn(state))
			XMPP_RosterPushItems(state, ppItems);
	}
	eaDestroy(&ppItems);
}

AUTO_COMMAND_REMOTE;
void XMPP_ReceiveChatRoomRoster(U32 uStateID, const char *xmppID, XMPP_ChatRoomRoster *rosterData)
{
	XmppClientState *state = XMPP_FindClientById(uStateID);
	if (XMPP_IsLoggedIn(state))
	{
		xmpp_s_sendroomoccupants(state->generator, XMPP_RoomDomainName(rosterData->eDomain), rosterData->room, 
			xmpp_client_jid(state), rosterData->ppOccupants, rosterData->bRewrite);
	}
}

// Process presence update
void XMPP_ProcessPresenceUpdate(XmppClientState *state, XMPP_PresenceAvailability availability, const char *from, const char *to,
								const char *id, XMPP_Priority priority, const char *status)
{
	XMPP_PresenceData pdata = {0};
	char *structString = NULL;

	// Validate.
	if (!devassert(state))
		return;

	// Don't accept forged presence.
	if (from && !BareJidEqual(from, xmpp_client_jid(state)))
		return;

	StructInit(parse_XMPP_PresenceData, &pdata);
	pdata.avail = availability;
	pdata.status = estrDup(status);
	pdata.priority = priority;

	// Process chat command.
	if (to && XMPP_IsAtChatDomain(to) && XMPP_ValidateJid(to))
	{
		pdata.eDomain = XMPP_GetRoomDomainEnum(to);
		MakeNodeFromJid(&pdata.room, to);
		MakeResourceFromJid(&pdata.resource, to);

		XMPP_WRITE_STRUCT(&structString, parse_XMPP_PresenceData, &pdata);
		XMPP_SendGlobalChatQuery( GetXmppGlobalChatLink(), "XMPP_ChannelProcessPresenceUpdate", 
			"%d %s %s", 
			state->uStateID, id, structString);
	}
	else
	{
		// Directed presence is not implemented.  Ignore it.
		if (to && !BareJidEqual(to, XMPP_Domain()))
			return;
		XMPP_WRITE_STRUCT(&structString, parse_XMPP_PresenceData, &pdata);
		XMPP_SendGlobalChatQuery( GetXmppGlobalChatLink(), "XMPP_ProcessRosterPresenceUpdate", 
			"%d %s %s", 
			state->uStateID, id, structString);
	}
	StructDeInit(parse_XMPP_PresenceData, &pdata);
	estrDestroy(&structString);
}

// Process presence unavailability update.
void XMPP_ProcessPresenceUnavailable(XmppClientState *state, const char *from, const char *to, const char *id, const char *status)
{
	XMPP_PresenceData pdata = {0};
	char *structString = NULL;
	const char *cmd;

	// Validate.
	if (!devassert(state))
		return;

	StructInit(parse_XMPP_PresenceData, &pdata);

	// Process chat command.
	if (to && XMPP_IsAtChatDomain(to) && XMPP_ValidateJid(to))
	{	// Get the domain, room, and resource
		pdata.eDomain = XMPP_GetRoomDomainEnum(to);
		if (pdata.eDomain == XMPP_RoomDomain_Unknown)
		{
			XMPP_GenerateStanzaError(state->generator, StanzaError_ServiceUnavailable, Stanza_Cancel, 
				XMPP_Domain(), NULL, "presence", id, NULL);
			return;
		}

		MakeNodeFromJid(&pdata.room, to);
		MakeResourceFromJid(&pdata.resource, to);

		cmd = "XMPP_ProcessChannelPresenceUnavailable";
	}
	else
	{
		cmd = "XMPP_ProcessRosterPresenceUnavailable";
	}
	// Global stuff
	pdata.avail = XMPP_PresenceAvailability_Unavailable;
	if (status)
		pdata.status = estrDup(status);

	XMPP_WRITE_STRUCT(&structString, parse_XMPP_PresenceData, &pdata);
	XMPP_SendGlobalChatQuery( GetXmppGlobalChatLink(), cmd, 
		"%d %s %s", 
		state->uStateID, id, structString);
	StructDeInit(parse_XMPP_PresenceData, &pdata);
	estrDestroy(&structString);
}

// Process presence subscribe attempt.
void XMPP_ProcessPresenceSubscribe(XmppClientState *state, const char *from, const char *to, const char *id)
{
	// Validate.
	devassert(state);
	if (!XMPP_IsLoggedIn(state))
		return;
	if (!to || !*to || !XMPP_ValidateJid(to))
		XMPP_GenerateStanzaError(state->generator, StanzaError_JIDMalformed, Stanza_Modify, XMPP_Domain(), NULL, "message", id, NULL);
	else
		XMPP_SendGlobalChatQuery( GetXmppGlobalChatLink(), "XMPP_ProcessPresenceSubscribe", "%d %s \"%s\"",
			state->uStateID, id, to);
}

// Process presence subscription confirmation.
void XMPP_ProcessPresenceSubscribed(XmppClientState *state, const char *from, const char *to, const char *id)
{
	// Validate.
	devassert(state);
	if (!XMPP_IsLoggedIn(state))
		return;
	if (!to || !*to || !XMPP_ValidateJid(to))
		XMPP_GenerateStanzaError(state->generator, StanzaError_JIDMalformed, Stanza_Modify, XMPP_Domain(), NULL, "message", id, NULL);
	else
		XMPP_SendGlobalChatQuery( GetXmppGlobalChatLink(), "XMPP_ProcessPresenceSubscribed", "%d %s \"%s\"",
			state->uStateID, id, to);
}

// Process presence unsubscription attempt.
void XMPP_ProcessPresenceUnsubscribe(XmppClientState *state, const char *from, const char *to, const char *id)
{
	// Validate.
	devassert(state);
	if (!XMPP_IsLoggedIn(state))
		return;
	if (!to || !*to || !XMPP_ValidateJid(to))
		XMPP_GenerateStanzaError(state->generator, StanzaError_JIDMalformed, Stanza_Modify, XMPP_Domain(), NULL, "message", id, NULL);
	else
		XMPP_SendGlobalChatQuery( GetXmppGlobalChatLink(), "XMPP_ProcessPresenceUnsubscribe", "%d %s \"%s\"",
			state->uStateID, id, to);
}

// Process presence unsubscription.
void XMPP_ProcessPresenceUnsubscribed(XmppClientState *state, const char *from, const char *to, const char *id)
{
	// Because we only implement bidirectional presence subscription, do the same as above.
	XMPP_ProcessPresenceUnsubscribe(state, from, to, id);
}

AUTO_COMMAND_REMOTE;
void XMPP_ReceiveUnsubscribe(U32 uStateID, const char *xmppID, const char *to)
{
	XmppClientState *state = XMPP_FindClientById(uStateID);
	if (XMPP_IsLoggedIn(state))
	{
		// TODO should "to" = XMPP_Domain() ?
		xmpp_s_sendpresenceunsubscribed(state->generator, to, xmpp_client_jid(state), xmppID);
	}
}

// Process presence probes.
void XMPP_ProcessPresenceProbe(XmppClientState *state, const char *from, const char *to, const char *id)
{
	// Send error.
	XMPP_GenerateStanzaError(state->generator, StanzaError_BadRequest, Stanza_Modify, XMPP_Domain(), NULL, "message", id, NULL);
}

// Process presence errors.
void XMPP_ProcessPresenceError(XmppClientState *state, const char *from, const char *to, const char *id)
{
	// Ignore presence errors from clients.
}

AUTO_COMMAND_REMOTE;
void XMPP_RecvPrivateMessage(U32 uToStateID, const char *xmppID, XMPP_Message *msg)
{
	XmppClientState *state = XMPP_FindClientById(uToStateID);
	if (XMPP_IsLoggedIn(state))
	{
		const char *to = xmpp_client_jid(state);
		char *from = NULL;
		estrStackCreate(&from);
		if (msg->fromResource)
			estrPrintf(&from, "%s@%s/%s", msg->fromHandle, XMPP_Domain(), msg->fromResource);
		else
			estrPrintf(&from, "%s@%s", msg->fromHandle, XMPP_Domain());
		xmpp_s_sendmessage(state->generator, to, from, xmppID, msg->body, msg->subject, msg->thread, msg->type);
		estrDestroy(&from);
	}
}

void XMPP_ProcessMessage(XmppClientState *state, const char *to, const char *from, const char *id,
						 const char *body, const char *subject, const char *thread, XMPP_MessageType type)
{
	// Validate.
	devassert(state);
	if (!state)
		return;
	if (!from)
		from = xmpp_client_jid(state);
	if (!XMPP_ValidateJid(to) || !XMPP_ValidateJid(from))
	{
		XMPP_GenerateStanzaError(state->generator, StanzaError_JIDMalformed, Stanza_Modify, XMPP_Domain(), NULL, "message", id, NULL);
		return;
	}

	// Process chat messages.
	if (type == XMPP_MessageType_Groupchat)
	{
		XMPP_ChatRoomMessage message = {0};
		char *structString = NULL;
		if (!XMPP_IsAtChatDomain(to))
		{
			XMPP_GenerateStanzaError(state->generator, StanzaError_BadRequest, Stanza_Modify, XMPP_Domain(), NULL, "message", id, NULL);
			return;
		}
		// Get the domain.
		message.eDomain = XMPP_GetRoomDomainEnum(to);
		if (message.eDomain == XMPP_RoomDomain_Unknown)
		{
			XMPP_GenerateStanzaError(state->generator, StanzaError_ServiceUnavailable, Stanza_Cancel, 
				XMPP_Domain(), NULL, "presence", id, NULL);
			return;
		}
		// Get the room name.
		MakeNodeFromJid(&message.room, to);

		if (subject && *subject)
		{
			estrCopy2(&message.subject, subject);
			XMPP_WRITE_STRUCT(&structString, parse_XMPP_ChatRoomMessage, &message);
			XMPP_SendGlobalChatQuery( GetXmppGlobalChatLink(), "XMPP_ChannelProcessSubject",
				"%d %s %s", 
				state->uStateID, id, structString);
		}
		else if (body && *body)
		{
			estrCopy2(&message.message, body);
			XMPP_WRITE_STRUCT(&structString, parse_XMPP_ChatRoomMessage, &message);
			XMPP_SendGlobalChatQuery( GetXmppGlobalChatLink(), "XMPP_ChannelProcessMessage",
				"%d %s %s", 
				state->uStateID, id, structString);
		}
		else
			XMPP_GenerateStanzaError(state->generator, StanzaError_NotAcceptable, Stanza_Modify, XMPP_Domain(), NULL, "message", id, NULL);
		StructDeInit(parse_XMPP_ChatRoomMessage, &message);
		estrDestroy(&structString);
	}
	else
	{
		// Don't allow empty messages.
		if (body && *body)
		{
			XMPP_Message msg = {0};
			char *msgString = NULL;

			msg.body = body;
			msg.subject = subject;
			msg.thread = thread;
			msg.type = type;

			// Process directed message.
			XMPP_WRITE_STRUCT(&msgString, parse_XMPP_Message, &msg);
			XMPP_SendGlobalChatQuery( GetXmppGlobalChatLink(), "XMPP_ProcessPrivateMessage",
				"%d %s \"%s\" %s", 
				state->uStateID, id, to, msgString);
			estrDestroy(&msgString);
		}
		else
			XMPP_GenerateStanzaError(state->generator, StanzaError_NotAcceptable, Stanza_Modify, XMPP_Domain(), NULL, "message", id, NULL);
	}
}

// Process information discovery.
void XMPP_ProcessDiscoInfo(XmppClientState *state, const char *to, const char *from, const char *id)
{
	char *estrFrom = NULL;
	static const char *const features[] = {
		"http://jabber.org/protocol/disco#info",
		"http://jabber.org/protocol/disco#items",
		"jabber:iq:roster",
		"urn:xmpp:ping"
	};

	// Make sure there's a valid from attribute.
	if (!from)
		from = xmpp_client_jid(state);

	// Reply to queries to the domain: Send identity and features.
	if (JidEqual(to, XMPP_Domain()))
		xmpp_s_senddiscoinfo(state->generator, id, XMPP_Domain(), from, features, sizeof(features)/sizeof(*features),
			"CrypticSpace", XMPP_DiscoItemType_Server);
	// Reply to chat inqueries.
	else if (XMPP_IsAtChatDomain(to))
		XMPP_ChannelProcessDiscoInfo(state, to, from, id);
	// Don't allow queries to anything else.
	else
		XMPP_GenerateStanzaError(state->generator, StanzaError_ServiceUnavailable, Stanza_Cancel, XMPP_Domain(), from, "iq", id, NULL);
}

AUTO_COMMAND_REMOTE;
void XMPP_SendDiscoItemResponse (U32 uStateID, XMPP_RoomDomain domain, const char *xmppID, XMPP_DiscoItemList *pItemList)
{
	XmppClientState *state = XMPP_FindClientById(uStateID);
	if (XMPP_IsLoggedIn(state))
	{
		const char *from = xmpp_client_jid(state);
		int i;
		for (i=eaSize(&pItemList->ppItems)-1; i>=0; i--)
		{
			if (pItemList->ppItems[i]->eDomain != XMPP_RoomDomain_Unknown)
			{
				if (pItemList->ppItems[i]->jid)
					estrConcatf(&pItemList->ppItems[i]->jid, "@%s", XMPP_RoomDomainName(pItemList->ppItems[i]->eDomain));
				else
					estrPrintf(&pItemList->ppItems[i]->jid, "%s", XMPP_RoomDomainName(pItemList->ppItems[i]->eDomain));
			}
		}
		xmpp_s_senddiscoitem(state->generator, xmppID, 
			domain == XMPP_RoomDomain_Unknown ? XMPP_Domain() : XMPP_RoomDomainName(domain), 
			from, pItemList->ppItems);
	}
}

// Process item discovery.
void XMPP_ProcessDiscoItems(XmppClientState *state, const char *to, const char *from, const char *id)
{
	// Make sure there's a valid from attribute.
	if (!from)
		from = xmpp_client_jid(state);

	// Requesting list of available services.
	if (JidEqual(to, XMPP_Domain()))
	{
		XMPP_SendGlobalChatQuery(GetXmppGlobalChatLink(), "XMPP_ProcessDiscoItems", 
			"%d \"%s\" %s", state->uStateID, from, id);
	}
	else if (XMPP_IsAtChatDomain(to))
	{
		// Get the domain.
		enum XMPP_RoomDomain domain = XMPP_GetRoomDomainEnum(to);
		if (domain == XMPP_RoomDomain_Unknown)
		{
			XMPP_GenerateStanzaError(state->generator, StanzaError_ServiceUnavailable, Stanza_Cancel, 
				XMPP_Domain(), NULL, "presence", id, NULL);
			return;
		}

		// Requesting list of chat channels.
		if (JidEqual(to, XMPP_RoomDomainName(domain)))
		{
			XMPP_SendGlobalChatQuery(GetXmppGlobalChatLink(), "XMPP_ChannelProcessDiscoItems", 
				"%d %d \"%s\" %s", state->uStateID, domain, from, id);
		}
		// Requesting list of members of a chat channel
		else if (DomainEqual(to, XMPP_RoomDomainName(domain)) && !IsFullJid(to))
		{
			XMPP_GenerateStanzaError(state->generator, StanzaError_NotAllowed, Stanza_Cancel, XMPP_Domain(), from, "iq", id, NULL);
		}
		// Requesting information about a member of a chat channel.
		else if (DomainEqual(to, XMPP_RoomDomainName(domain)) && IsFullJid(to))
		{
			XMPP_GenerateStanzaError(state->generator, StanzaError_NotAllowed, Stanza_Cancel, XMPP_Domain(), from, "iq", id, NULL);
		}
	}
	// Request to some unavailable entity.
	else
		XMPP_GenerateStanzaError(state->generator, StanzaError_ServiceUnavailable, Stanza_Cancel, XMPP_Domain(), from, "iq", id, NULL);
}

// Handle a stream error generated by the parser.
void XMPP_HandleStreamError(XmppClientState *state, enum XMPP_StreamErrorCondition error, const char *text)
{
	// Print error.
	verbose_printf("Client %"FORM_LL"u: Stream error %s \"%s\"\n", XMPP_UniqueClientId(state),
		StaticDefineIntRevLookup(XMPP_StreamErrorConditionEnum, error), NULL_TO_EMPTY(text));

	// Open stream if necessary.
	if (!state->stream_open)
		xmpp_s_openstream(state->generator, XMPP_Domain(), XMPP_UniqueClientId(state));

	// Send error.
	XMPP_GenerateStreamError(state->generator, error, text);
	XMPP_EndStream(state, "Stream error");
}

// Handle a SASL error generated by the parser.
void XMPP_HandleSaslError(XmppClientState *state, enum XMPP_SaslError error, const char *text)
{
	// Print error.
	verbose_printf("Client %"FORM_LL"u: SASL error %s \"%s\"\n", XMPP_UniqueClientId(state),
		StaticDefineIntRevLookup(XMPP_SaslErrorEnum, error), NULL_TO_EMPTY(text));

	// Send error.
	XMPP_GenerateSaslError(state->generator, error, text);
}

// Handle a stanza error generated by the parser.
void XMPP_HandleStanzaError(XmppClientState *state, enum XMPP_StanzaErrorCondition error, XMPP_StanzaErrorType type,
							const char *element, const char *id, const char *text)
{
	const char *to = xmpp_client_jid(state);

	// Print error.
	verbose_printf("Client %"FORM_LL"u: Stanza error %s \"%s\"\n", XMPP_UniqueClientId(state),
		StaticDefineIntRevLookup(XMPP_StanzaErrorConditionEnum, error), NULL_TO_EMPTY(text));

	// Send error.
	XMPP_GenerateStanzaError(state->generator, error, type, XMPP_Domain(), to, element, id, text);
}

// Authentication has begun.
void XMPP_HandleAuthBegin(XmppClientState *state)
{
	XMPP_NetRedact(state->link, true);
}

// Authentication has ended.
void XMPP_HandleAuthEnd(XmppClientState *state)
{
	XMPP_NetRedact(state->link, false);
}

// Redact a specific authentication string.
void XMPP_RedactAuthString(XmppClientState *state, const char *pString)
{
	XMPP_NetRedactString(state->link, pString);
}

// Start a stream element in the appropriate state.
void XMPP_StartStream(XmppClientState *state, enum XMPP_LoginState loginState)
{
	switch (loginState)
	{
		// Just connected, or just established TLS session
		case XMPP_LoginState_Connected:
			{
				bool allowTls = xmpp_TlsAllowed() && !xmpp_UsingTls(state->link);
				bool requireTls = allowTls && xmpp_TlsRequired(state->link);
				xmpp_s_openstream(state->generator, XMPP_Domain(), XMPP_UniqueClientId(state));
				state->stream_open = true;
				xmpp_s_sendauthstreamfeatures(state->generator, allowTls, requireTls);
			}
			break;

		// Restarted after authentication
		case XMPP_LoginState_Authenticated:
			xmpp_s_openstream(state->generator, XMPP_Domain(), XMPP_UniqueClientId(state));
			state->stream_open = true;
			xmpp_s_sendstreamfeatures(state->generator);
			break;

		// These should never happen.
		case XMPP_LoginState_LoggedIn:
		default:
			devassert(0);
	}
}

// The client has ended the stream prior to full login.
void XMPP_LoginAborted(XmppClientState *state, enum XMPP_LoginState loginState)
{
	devassert(loginState != XMPP_LoginState_LoggedIn);
	verbose_printf("Client %"FORM_LL"u aborted login", XMPP_UniqueClientId(state));
}

// The client has ended the stream.
void XMPP_EndStream(XmppClientState *state, const char *reason)
{
	if (!state->stream_closed)
	{
		// Close stream.
		xmpp_s_closestream(state->generator);
		state->stream_closed = true;

		// This is incorrect according to RFC 6120 - XMPP Core, 4.4
		// Stream closures initiated by the server are supposed to wait for a matching close stream response from the client
		// or after a reasonable amount of time before killing the link
		// TLS is also supposed to send/receive a close notify before the underlying TCP connection is closed,
		// for preventing truncation attacks through TLS's resume feature which is not supporte	d
		xmpp_Disconnect(state->link, reason);
	}
}

// Log XMPP statistics.
static void LogXmppStats()
{
	static U32 uLastTime = 0;
	U32 uThisTime;

	PERFINFO_AUTO_START_FUNC();

	uThisTime = timeSecondsSince2000();
	if (uThisTime - uLastTime > 60)
	{
		unsigned long resCount = 0;
		char connections[11];
		//char nodes[11];
		char resources[11];
		char bytesSent[21];
		char bytesReceived[21];
		char stanzasSent[21];
		char stanzasReceived[21];

		resCount = stClientStates ? stashGetCount(stClientStates) : 0;
		//sprintf(nodes, "%lu", stashGetCount(gXmppState.stXmppNodes));
		sprintf(connections, "%lu", XMPP_TotalConnections());
		sprintf(resources, "%lu", resCount);
		sprintf(bytesSent, "%"FORM_LL"u", XMPP_BytesSent());
		sprintf(bytesReceived, "%"FORM_LL"u", XMPP_BytesReceived());
		sprintf(stanzasSent, "%"FORM_LL"u", XMPP_StanzasSent());
		sprintf(stanzasReceived, "%"FORM_LL"u", XMPP_StanzasReceived());
		servLogWithPairs(LOG_XMPP_GENERAL, "XmppStats",
			"connections", connections,
			//"nodes", nodes,
			"resources", resources,
			"bytesSent", bytesSent,
			"bytesReceived", bytesReceived,
			"stanzasSent", stanzasSent,
			"stanzasReceived", stanzasReceived,
			NULL);
		uLastTime = uThisTime;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

// Process pending XMPP activity.
void XMPP_Tick(F32 elapsed)
{
	PERFINFO_AUTO_START_FUNC();

	// Check for pending client activity.
	XMPP_NetTick(elapsed);

	// Try to validate any pending logins.
	XMPP_LoginValidatorTick();

	// Log XMPP statistics.
	LogXmppStats();

	PERFINFO_AUTO_STOP_FUNC();
}

bool XMPP_Begin()
{
	return XMPP_NetBegin();
}

// Increment the count of stanzas received.
void XMPP_StatsIncrementStanzaCount(void)
{
	++siStanzasReceived;
}

// Process request for STARTTLS negotiation.
void XMPP_ProcessStartTls(XmppClientState *state)
{
	xmpp_s_starttls(state->generator);
	XMPP_RestartGenerator(state->generator);
	state->stream_open = false;
	XMPP_NetStartTls(state->link);
}

AUTO_COMMAND_REMOTE;
void XMPP_ReceiveStanzaError(U32 uStateID, const char *id, XMPP_StanzaError *pError)
{
	XmppClientState *state = XMPP_FindClientById(uStateID);
	if (XMPP_IsLoggedIn(state))
	{
		const char *jid = NULL;
		char *domainString = NULL;

		estrStackCreate(&domainString);
		if (pError->eDomain != XMPP_RoomDomain_Unknown)
			estrPrintf(&domainString, "%s@%s", pError->room, XMPP_RoomDomainName(pError->eDomain));
		else
			estrCopy2(&domainString, XMPP_Domain());

		if (state->resource)
			jid = xmpp_client_jid(state);
		XMPP_GenerateStanzaError(state->generator, pError->error, pError->type, domainString, 
			jid, pError->element, id, pError->text);
		estrDestroy(&domainString);
	}
}

// Process resource bind request.
bool XMPP_ProcessBind(XmppClientState *state, const char *id, const char *resource)
{
	NetLink *xmppLink;
	// Verify that the provided resource is acceptable; generate a resource if the client did not provide one.
	if (resource && *resource)
	{
		if (StringIsInvalidCharacterName(resource, 0))
		{
			XMPP_GenerateStanzaError(state->generator, StanzaError_NotAcceptable, Stanza_Modify, NULL, NULL, "iq", id,
				"The provided resource name is not acceptable.");
			return false;
		}
	}
	xmppLink = GetXmppGlobalChatLink();
	if (linkConnected(xmppLink))
	{
		XMPP_LoginData data = {0};
		char *pLoginString = NULL;
		data.uAccountID = state->ticket->accountID;
		data.accountName = state->ticket->accountName;
		data.displayName = state->ticket->displayName;
		data.iAccessLevel = state->iAccessLevel;
		data.resource = (char*) resource;
		XMPP_WRITE_STRUCT(&pLoginString, parse_XMPP_LoginData, &data);
		// This uses the exact display name
		XMPP_SendGlobalChatQuery( xmppLink, "XMPP_Login", "%d \"%s\" %s", 
			state->uStateID, id, pLoginString);
		estrDestroy(&pLoginString);
		return true;
	}
	else
	{
		XMPP_HandleStreamError(state, XMPP_StreamError_RemoteConnectionFailed, "Global Chat Server is unavailable.");
		return false;
	}
}

AUTO_COMMAND_REMOTE;
void XMPP_LoginUnauthorized(U32 uStateID)
{
	XmppClientState *state = XMPP_FindClientById(uStateID);
	if (state)
	{
		char ip[17];
		char id[21];
		servLogWithPairs(LOG_XMPP_GENERAL, "XmppAuthFinishedLateFail",
			"id", XMPP_UniqueClientIdStr(state, id, sizeof(id)),
			"ip", XMPP_GetIpStr(state, ip, sizeof(ip)),
			"success", "0",
			"temporary", "0",
			"type", "NotAllowed", NULL);
		XMPP_HandleStreamError(state, XMPP_StreamError_PolicyViolation, "This account is not allowed to login with XMPP.");
	}
}

AUTO_COMMAND_REMOTE;
void XMPP_ProcessBindResponse(U32 uStateID, const char *xmppID, const char *resource)
{
	XmppClientState *state = XMPP_FindClientById(uStateID);
	char ip[17];
	char id[21];
	if (!state)
		return;
	state->resource = strdup(resource);
	XMPPLink_SetLoginState(state, XMPP_LoginState_LoggedIn);

	servLogWithPairs(LOG_XMPP_GENERAL, "XmppLogin",
		"id", XMPP_UniqueClientIdStr(state, id, sizeof(id)),
		"handle", state->ticket->displayName,
		"ip", XMPP_GetIpStr(state, ip, sizeof(ip)),
		"jid", xmpp_client_jid(state) ? xmpp_client_jid(state) : "", NULL);
	xmpp_s_sendbindresponse(state->generator, xmppID, state->resource, xmpp_client_get_escaped_name(state), XMPP_Domain());
}

AUTO_COMMAND_REMOTE;
void XMPP_SendPresence(U32 uStateID, const char *xmppID, XMPP_PresenceData *pData)
{
	XmppClientState *state = XMPP_FindClientById(uStateID);
	const char *to = NULL;
	char *from = NULL;
	if (!state || !pData->handle || !pData->resource || !XMPP_IsLoggedIn(state))
		return;
	estrStackCreate(&from);
	estrPrintf(&from, "%s@%s/%s", pData->handle, XMPP_Domain(), pData->resource);
	to = xmpp_client_bare_jid(state);
	xmpp_s_sendpresence(state->generator, pData->avail, from, to, xmppID, pData->priority, pData->status);
	estrDestroy(&from);
}

AUTO_COMMAND_REMOTE;
void XMPP_SendPresenceToList(const char *xmppID, XMPP_ClientStateList *stateList, XMPP_PresenceData *pData)
{
	if (pData->handle && pData->resource)
	{
		int i, size;
		char *from = NULL;

		estrStackCreate(&from);
		estrPrintf(&from, "%s@%s/%s", pData->handle, XMPP_Domain(), pData->resource);
		size = eaiSize(&stateList->eaiStateIDs);
		for (i=0; i<size; i++)
		{
			XmppClientState *state = XMPP_FindClientById(stateList->eaiStateIDs[i]);
			
			if (XMPP_IsLoggedIn(state))
			{
				const char *to = xmpp_client_jid(state);
				xmpp_s_sendpresence(state->generator, pData->avail, from, to, xmppID, pData->priority, pData->status);
			}
		}
		estrDestroy(&from);
	}
}

// Process session initiation.
void XMPP_ProcessSession(XmppClientState *state, char *id)
{
	xmpp_s_sendsessionresponse(state->generator, id, XMPP_Domain());
}

AUTO_COMMAND_REMOTE;
void XMPP_SendRoster(U32 uStateID, const char *xmppID, XMPP_RosterItemList * rosterList)
{
	XmppClientState *state = XMPP_FindClientById(uStateID);
	if (XMPP_IsLoggedIn(state))
	{
		if (xmppID && *xmppID)
			xmpp_s_sendroster(state->generator, xmppID, xmpp_client_jid(state), rosterList->ppRoster);
		else
			xmpp_s_sendrosterpush(state->generator, xmppID, xmpp_client_jid(state), rosterList->ppRoster);
	}
}

// Process roster requests.
void XMPP_ProcessRosterGet(XmppClientState *state, const char *id, const char *to, const char *from)
{
	// Validate parameters.
	if (from && !BareJidEqual(from, xmpp_client_jid(state)) )
	{
		XMPP_GenerateStanzaError(state->generator, StanzaError_BadRequest, Stanza_Modify, XMPP_Domain(), xmpp_client_jid(state), "iq", id, NULL);
		return;
	}
	// Sanity check.
	if (!XMPP_IsLoggedIn(state))
		return;

	XMPP_SendGlobalChatQuery( GetXmppGlobalChatLink(), "XMPP_ProcessRosterGet", "%d %s", state->uStateID, id);
}

AUTO_COMMAND_REMOTE;
void XMPP_ReceiveRosterResult(U32 uStateID, const char *xmppID)
{
	XmppClientState *state = XMPP_FindClientById(uStateID);
	if (XMPP_IsLoggedIn(state))
	{
		xmpp_s_sendrosterresult(state->generator, xmppID, xmpp_client_jid(state));
	}
}

// Process roster adds and updates.
void XMPP_ProcessRosterUpdate(XmppClientState *state, const char *id, const char *to, const char *from,
							  const char *jid, const char *name, const char *const *groups)
{
	XMPP_RosterUpdate roster = {0};
	char *rosterString = NULL;

	// Validate parameters.
	if (from && !BareJidEqual(from, xmpp_client_jid(state)))
	{
		XMPP_GenerateStanzaError(state->generator, StanzaError_BadRequest, Stanza_Modify, XMPP_Domain(), xmpp_client_jid(state), "iq", id, NULL);
		return;
	}
	estrCopy2(&roster.jid, jid);
	// groups are unimplemented

	XMPP_WRITE_STRUCT(&rosterString, parse_XMPP_RosterUpdate, &roster);
	StructDeInit(parse_XMPP_RosterUpdate, &roster);
	XMPP_SendGlobalChatQuery( GetXmppGlobalChatLink(), "XMPP_ProcessRosterUpdate", "%d %s %s", 
		state->uStateID, id, rosterString);
	estrDestroy(&rosterString);
}

// Process roster removes.
void XMPP_ProcessRosterRemove(XmppClientState *state, const char *id, const char *to, const char *from, const char *jid)
{
	// Validate parameters.
	if (from && !BareJidEqual(from, xmpp_client_jid(state)))
	{
		XMPP_GenerateStanzaError(state->generator, StanzaError_BadRequest, Stanza_Modify, XMPP_Domain(), xmpp_client_jid(state), "iq", id, NULL);
		return;
	}
	XMPP_SendGlobalChatQuery( GetXmppGlobalChatLink(), "XMPP_ProcessRosterRemove", "%d %s \"%s\"", 
		state->uStateID, id, jid);
}

// Process ping.
void XMPP_ProcessPing(XmppClientState *state, char *id)
{
	xmpp_s_sendpong(state->generator, XMPP_Domain(), id, NULL);
}

XmppClientState *XMPP_FindClientById(U32 uID)
{
	XmppClientState *state = NULL;
	if (stClientStates)
		stashIntFindPointer(stClientStates, uID, &state);
	return state;
}

// Create a client. 
void *XMPP_CreateClient(XmppClientLink *link)
{
	static int siNextStateId = 1; // first ID = 1
	XmppClientState *state = StructCreate(parse_XmppClientState);
	state->uStateID = siNextStateId++;
	state->link = link;
	state->generator = XMPP_CreateGenerator(link);
	if (!stClientStates)
		stClientStates = stashTableCreateInt(XMPPCLIENTSTATE_DEFAULT_STASH_SIZE);
	assert(stashIntAddPointer(stClientStates, state->uStateID, state, false));
	return state;
}

// Destroy a client.
void XMPP_DestroyClient(XmppClientState *state)
{
	if (stClientStates)
	{   // Remove from stash immediately
		stashIntRemovePointer(stClientStates, state->uStateID, NULL);
	}

	// This uses the exact display name
	if (state->ticket)
		XMPP_SendGlobalChatQuery( GetXmppGlobalChatLink(), "XMPP_Logout", "%d \"%s\"", 
			state->uStateID, state->ticket->displayName);

	// Stop any in-progress validation and destroy validator.
	XMPP_DestroyValidator(state);

	XMPP_DestroyGenerator(state->generator);
	state->generator = NULL;

	StructDestroy(parse_XmppClientState, state);
}

// Get the chat domain name.
const char *XMPP_RoomDomainName(int domain)
{
	static char *channelName = NULL;
	static char *guildName = NULL;
	static char *officerName = NULL;
	const char *result;

	// Select the proper domain, and initialize it if necessary.
	switch (domain)
	{
		case XMPP_RoomDomain_Channel:
			if (!channelName)
				estrPrintf(&channelName, "channels.%s", XMPP_Domain());
			result = channelName;
			break;
		case XMPP_RoomDomain_Guild:
			if (!guildName)
				estrPrintf(&guildName, "guilds.%s", XMPP_Domain());
			result = guildName;
			break;
		case XMPP_RoomDomain_Officer:
			if (!officerName)
				estrPrintf(&officerName, "officer.%s", XMPP_Domain());
			result = officerName;
			break;
		default:
			devassert(0);
			result = "";
	}
	return result;
}

// Get the chat domain enum value from a JID.
int XMPP_GetRoomDomainEnum(const char *jid)
{
	int i;
	for (i = 0; i < XMPP_RoomDomain_Max; ++i)
	{
		if (DomainEqual(jid, XMPP_RoomDomainName(i)))
		{
			return i;
		}
	}
	devassert(0);
	return XMPP_RoomDomain_Unknown;
}

// Return true if this is a chat domain.
bool XMPP_IsAtChatDomain(const char *jid)
{
	int i;
	for (i = 0; i < XMPP_RoomDomain_Max; ++i)
		if (DomainEqual(jid, XMPP_RoomDomainName(i)))
			return true;
	return false;
}

// Authentication is complete.
void XMPP_AuthComplete(XmppClientState *state, bool success, XMPP_SaslError error, const char *reason)
{
	// Notify client link handler that authentication is complete.
	XMPP_NetAuthComplete(state->link, success);

	// Send the response to the client.
	if (success)
	{
		xmpp_s_sendauthresponse(state->generator);
		XMPP_RestartGenerator(state->generator);
		state->stream_open = false;
	}
	else
		XMPP_GenerateSaslError(state->generator, error, reason);
}

// Notify an XMPP user that he has an incoming friend request.
AUTO_COMMAND_REMOTE;
void XMPP_NotifyFriendRequest(const char *fromHandle, XMPP_ClientStateList *stateList)
{
	char *fromBareJid = NULL;
	int i, size;

	size = eaiSize(&stateList->eaiStateIDs);
	if (!fromHandle || !*fromHandle || size == 0)
		return;

	estrStackCreate(&fromBareJid);
	estrPrintf(&fromBareJid, "%s@%s", fromHandle, XMPP_Domain());
	for (i=0; i<size; i++)
	{
		XmppClientState *state = XMPP_FindClientById(stateList->eaiStateIDs[i]);
		if (XMPP_IsLoggedIn(state))
			xmpp_s_sendpresencesubscribe(state->generator, fromBareJid, xmpp_client_bare_jid(state), NULL);
	}
	estrDestroy(&fromBareJid);
}

AUTO_COMMAND_REMOTE;
void XMPP_NotifyFriendRequestsForUser(U32 uStateID, XMPP_ClientStateList *stateList)
{
	XmppClientState *state = XMPP_FindClientById(uStateID);
	char *pBareJid = NULL;
	const char *toJid;
	int i, size;

	if (!XMPP_IsLoggedIn(state))
		return;
	toJid = xmpp_client_bare_jid(state);

	size = eaSize(&stateList->eaHandles);
	for (i=0; i<size; i++)
	{
		estrClear(&pBareJid);
		estrPrintf(&pBareJid, "%s@%s", stateList->eaHandles[i], XMPP_Domain());
		xmpp_s_sendpresencesubscribe(state->generator, pBareJid, toJid, NULL);
	}
}


// Notify an XMPP user that he has a new friend.
AUTO_COMMAND_REMOTE;
void XMPP_NotifyNewFriend(const char *fromHandle, XMPP_ClientStateList *stateList)
{
	char *fromBareJid = NULL;
	int i, size;

	size = eaiSize(&stateList->eaiStateIDs);
	if (!fromHandle || !*fromHandle || size == 0)
		return;

	estrStackCreate(&fromBareJid);
	estrPrintf(&fromBareJid, "%s@%s", fromHandle, XMPP_Domain());
	for (i=0; i<size; i++)
	{
		XmppClientState *state = XMPP_FindClientById(stateList->eaiStateIDs[i]);
		if (XMPP_IsLoggedIn(state))
		{
			// Send presence notification.
			xmpp_s_sendpresencesubscribed(state->generator, fromBareJid, xmpp_client_bare_jid(state), NULL);
			// Send roster push.
			XMPP_RosterPush(state, fromBareJid, fromHandle, true, NULL, false);
		}
	}
	estrDestroy(&fromBareJid);
}

// Notify an XMPP user that he has lost a friend.
AUTO_COMMAND_REMOTE;
void XMPP_NotifyFriendRemove(const char *fromHandle, XMPP_ClientStateList *stateList)
{
	char *fromBareJid = NULL;
	int i, size;
	
	size = eaiSize(&stateList->eaiStateIDs);
	if (!fromHandle || !*fromHandle || size == 0)
		return;

	estrStackCreate(&fromBareJid);
	estrPrintf(&fromBareJid, "%s@%s", fromHandle, XMPP_Domain());
	for (i=0; i<size; i++)
	{
		XmppClientState *state = XMPP_FindClientById(stateList->eaiStateIDs[i]);
		if (XMPP_IsLoggedIn(state))
			xmpp_s_sendrosterpushremove(state->generator, NULL, xmpp_client_bare_jid(state), fromBareJid);
	}
	estrDestroy(&fromBareJid);
}

// A user has spoken in a channel.
AUTO_COMMAND_REMOTE;
void XMPP_NotifyChatMessage(U32 uStateID, const char *xmppID, XMPP_ChatRoomMessage *msg)
{
	XmppClientState *state = XMPP_FindClientById(uStateID);
	if (XMPP_IsLoggedIn(state))
	{
		char *from = NULL;
		estrPrintf(&from, "%s@%s/%s", msg->room, XMPP_RoomDomainName(msg->eDomain), msg->nickname);
		xmpp_s_sendmessage(state->generator, xmpp_client_jid(state), from, xmppID, msg->message, msg->subject, NULL, XMPP_MessageType_Groupchat);
		estrDestroy(&from);
	}
}

AUTO_COMMAND_REMOTE;
void XMPP_NotifyChatMessageBatch(const char *xmppID, XMPP_ClientStateList *list, XMPP_ChatRoomMessage *msg)
{
	int i, size = eaiSize(&list->eaiStateIDs);
	char *from = NULL;
	estrPrintf(&from, "%s@%s/%s", msg->room, XMPP_RoomDomainName(msg->eDomain), msg->nickname);
	for (i=0; i<size; i++)
	{
		XmppClientState *state = XMPP_FindClientById(list->eaiStateIDs[i]);
		if (XMPP_IsLoggedIn(state))
			xmpp_s_sendmessage(state->generator, xmpp_client_jid(state), from, xmppID, 
				msg->message, msg->subject, msg->thread, XMPP_MessageType_Groupchat);
	}
	estrDestroy(&from);
}

// A user has joined a channel.
AUTO_COMMAND_REMOTE;
void XMPP_NotifyChatJoin (U32 uStateID, XMPP_RoomDomain eDomain, const char *room, XMPP_ChatOccupant *occupant, XMPP_ClientStateList *roster)
{
	XmppClientState *state = XMPP_FindClientById(uStateID);
	int i, size;

	// State is OPTIONAL - 0 means that the login was from the game
	if (uStateID != 0 && !XMPP_IsLoggedIn(state)) return;

	size = eaiSize(&roster->eaiStateIDs);
	for (i=0; i<size; i++)
	{
		// Get the member to send the update to
		XmppClientState *memberState = XMPP_FindClientById(roster->eaiStateIDs[i]);

		if (XMPP_IsLoggedIn(memberState))
		{
			// Send join information.
			xmpp_s_sendroompresence(memberState->generator, XMPP_RoomDomainName(eDomain), room, 
				xmpp_client_jid(memberState), occupant, false, false, 
				state == memberState);
		}
	}
}

// A user has left a channel.
AUTO_COMMAND_REMOTE;
void XMPP_NotifyChatPart (U32 uAccountID, XMPP_ChannelInfoData *leaver, XMPP_ClientStateList *roster, bool bKicked)
{
	XmppClientState *state;
	XMPP_ChatOccupant occupant = {0};
	int i, size;
	const char *domainName = XMPP_RoomDomainName(leaver->eDomain);
	// Init occupant
	occupant.nick = leaver->name; // no copy here so no DeInit is necessary
	occupant.affiliation = XMPP_Affiliation_Member;
	occupant.role = XMPP_Role_None;

	// Send to other users
	size = eaiSize(&roster->eaiStateIDs);
	for (i=0; i<size; i++)
	{
		state = XMPP_FindClientById(roster->eaiStateIDs[i]);
		if (XMPP_IsLoggedIn(state))
		{
			bool bSelf = state->ticket->accountID == uAccountID;
			xmpp_s_sendroompresence(state->generator, domainName, leaver->room, xmpp_client_jid(state), &occupant, 
				true, bKicked, bSelf);
		}
	}
}

// Return true if this client is trusted by virtue on being on a trusted link.
bool XMPP_Trusted(XmppClientState *state)
{
	return XMPP_NetTrustedLink(state->link);
}

// Copy the IP address of this link.
char *XMPP_GetIpStr(XmppClientState *state, char *buf, int buf_size)
{
	return XMPP_NetIpStr(state->link, buf, buf_size);
}

#include "XMPP_Gateway_h_ast.c"
