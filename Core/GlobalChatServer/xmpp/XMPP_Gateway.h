// Performs bulk of XMPP processing.
// It knows very little about the Global Chat Server, but a lot about XMPP.  It gets stanzas from XMPP_Parsing, and sends data back to the
// client with XMPP_Generation.  It manages the actual network connections with XMPP_Net.  To communicates with the rest of the
// Global Chat Server, it uses XMPP_Chat.

#pragma once

#include "ChatServer/xmppTypes.h"
typedef struct XMPP_RosterItem XMPP_RosterItem;
typedef struct XmppClientState XmppClientState;
typedef struct XMPP_ClientStateList XMPP_ClientStateList;
typedef struct XMPP_PresenceData XMPP_PresenceData;
typedef struct XMPP_ChatRoomMessage XMPP_ChatRoomMessage;
typedef struct XMPP_ChannelInfoData XMPP_ChannelInfoData;
typedef struct XMPP_ChatOccupant XMPP_ChatOccupant;
typedef struct NetLink NetLink;

#define CMD_NULL_STRUCT "<& __NULL__ &>"

// Decomposed parts of a JID, used by JidDecompose().
struct JidComponents
{
	const char *node;
	size_t nodelen;
	const char *domain;
	size_t domainlen;
	const char *resource;
	size_t resourcelen;
};

void XMPP_SendStanzaError(NetLink *xmppLink, U32 uStateID, XMPP_StanzaErrorCondition error, XMPP_StanzaErrorType type, 
						  const char *element, const char *id, const char *text);

/************************************************************************/
/* General utility                                                      */
/************************************************************************/

// Return true if XMPP has been disabled.
bool XMPP_IsDisabled(void);

typedef struct NetLink NetLink;
NetLink *XMPP_GetLink(void);

// Return true if this is a well-formed JID.  Note that it may not actually exist.
bool XMPP_ValidateJid(const char *jid);

// Decompose a JID into its component parts.
struct JidComponents XMPP_JidDecompose(const char *jid);

// Return true if two JIDs are equal, and false otherwise.
bool JidEqual(const char *lhs, const char *rhs);

// Return true if two JIDs are equal, ignoring any resource components.
bool BareJidEqual(const char *lhs, const char *rhs);

// Return true if two domains are equal.
bool DomainEqual(const char *lhs, const char *rhs);

// Create a roster item.
XMPP_RosterItem *XMPP_MakeRosterItem(const char *jid, const char *name, bool friends, char **guilds, bool ask);

// Get the client's unique identifier.
U64 XMPP_UniqueClientId(const XmppClientState *state);

// Get the client's unique identifier, as a string.
char *XMPP_UniqueClientIdStr(const XmppClientState *state, char *buf, int buf_size);

bool XmppStringEqual(const char *lhs, size_t lhsLen, const char *rhs, size_t rhsLen);

/************************************************************************/
/* Process incoming XMPP sessions and stanzas                           */
/************************************************************************/

// Send a private message to the XmppClient.
void XMPP_SendPrivateMessage(XmppClientState *state, const char *xmppID, const char *fromHandle, const char *fromResource, 
							 const char *body, XMPP_MessageType type);

// Send a roster push, with explicit parameters.
void XMPP_RosterPushUser(NetLink *xmppLink, XmppClientState *state, XMPP_ClientStateList *stateList, const char *jid);

// Send a presence update
void XMPP_SendPresenceUpdate(XmppClientState *state, const char *xmppID, XMPP_PresenceData *pData);

/************************************************************************/
/* XMPPChat interface to XMPP Gateway                                   */
/************************************************************************/

// Notify an XMPP user that he has an incoming friend request.
void XMPP_NotifyFriendRequest(const char *fromHandle, XMPP_ClientStateList *stateList);

// Notify an XMPP user that he has a new friend.
void XMPP_NotifyNewFriend(const char *fromHandle, XMPP_ClientStateList *stateList, XMPP_PresenceData *pData);

// Notify an XMPP user that he has lost a friend.
void XMPP_NotifyFriendRemove(const char *fromHandle, XMPP_ClientStateList *stateList);

// A user has spoken in a channel.
void XMPP_NotifyChatMessage(XmppClientState *state, int domain, const char *room, const char *roomnick, const char *message, const char *subject, const char *id);
void XMPP_NotifyChatMessageBatch(const char *xmppID, XMPP_ClientStateList *list, XMPP_ChatRoomMessage *msg);

// A user has joined a channel.
void XMPP_NotifyChatJoin(U32 uStateID, XMPP_RoomDomain eDomain, const char *room, XMPP_ChatOccupant *occupant, XMPP_ClientStateList *list);

// A user has left a channel (or been kicked).
void XMPP_NotifyChatPart(U32 uAccountID, XMPP_ChannelInfoData *info, XMPP_ClientStateList *list, bool bKicked);
