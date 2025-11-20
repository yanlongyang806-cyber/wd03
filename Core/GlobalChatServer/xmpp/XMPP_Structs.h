#pragma once

#include "ChatServer/xmppTypes.h"

AUTO_STRUCT;
typedef struct XmppClientRoom
{
	char *room; AST(KEY ESTRING)
	XMPP_RoomDomain eDomain;
} XmppClientRoom;

// A particular client link
AUTO_STRUCT;
typedef struct XmppClientState
{
	// Equivalent to XmppClientIdentifier
	U32 uStateID; AST(KEY)
	U32 uAccountID;

	// Client state
	char *resource;														// Identifier of this specific connection; analogous to character name
	char *presenceStatus;												// Client status string
	char *presenceId;													// 'id' attribute of presence
	XMPP_PresenceAvailability availability;								// Client availability
	XMPP_Priority priority;						AST(INT)				// Client priority
	bool accepting_presence;											// if true, we should send presence updates.
	EARRAY_OF(XmppClientRoom) rooms;									// List of rooms that this XMPP client is in.
	STRING_EARRAY firstJoinedRooms;				AST(ESTRING)			// List of rooms that were joined by this XMPP client specifically.
} XmppClientState;

// Collection of connected resources.
AUTO_STRUCT;
typedef struct XmppClientNode
{
	U32 uAccountID;
	XmppClientState **resources; // AUTO_FIXUP ensures that this isn't destroyed here
} XmppClientNode;

AUTO_STRUCT;
typedef struct XmppChatMember
{
	// Account ID
	U32 uID; AST(KEY)
	// XMPP Resources that are in the chat room
	INT_EARRAY eaiResourceStateIDs;
} XmppChatMember;

// Information about a specific XMPP-accessible channel, for service discovery.
AUTO_STRUCT;
typedef struct XmppChannelInfo
{
	char *node;			AST(ESTRING)	// Node of this channel
	char *name;			AST(ESTRING)	// Human-readable channel name
	int members;						// Number of members
} XmppChannelInfo;

// Cached information about a specific XMPP Chat Room
AUTO_STRUCT;
typedef struct XmppChatRoom
{
	int eDomain; // XMPP_RoomDomain
	XmppChannelInfo info; AST(EMBEDDED_FLAT)
	EARRAY_OF(XmppChatMember) ppMembers; AST(NAME(XmppMembers))
} XmppChatRoom;