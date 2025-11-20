#pragma once

#define XMPP_WRITE_STRUCT(estr, pti, obj) estrStackCreate(estr);\
	ParserWriteTextEscaped(estr, pti, obj, 0, 0, 0);

#include "xmppTypes.h"

AUTO_STRUCT;
typedef struct XmppClientIdentifier
{
	U32 uStateID; AST(KEY)
	U32 uAccountID;
} XmppClientIdentifier;

// A discovered item
AUTO_STRUCT;
typedef struct XMPP_DiscoItem {
	XMPP_RoomDomain eDomain;
	char *jid; AST(ESTRING)
	char *name; AST(ESTRING)
} XMPP_DiscoItem;

AUTO_STRUCT;
typedef struct XMPP_DiscoItemList {
	EARRAY_OF(XMPP_DiscoItem) ppItems;
} XMPP_DiscoItemList;

// An item in the roster
AUTO_STRUCT;
typedef struct XMPP_ChatOccupant
{
	U32 uStateID;
	char *nick;									AST(ESTRING)		// Room nick of occupant
	XMPP_Affiliation affiliation;									// Occupant room affilation
	XMPP_Role role;													// Occupant room role
	bool own;														// True if this occupant should be marked as the user's own occupant.
} XMPP_ChatOccupant;

// An item in the roster
AUTO_STRUCT;
typedef struct XMPP_RosterItem {
	char *jid;									AST(ESTRING)		// JID of contact
	char *name;														// Listed name of contact
	char **group;								AST(ESTRING)		// Group that contact is in
	XMPP_RosterSubscriptionState subscription;						// Presence relationship to this contact
	XMPP_RosterSubRequestState ask;									// Subscription request state
} XMPP_RosterItem;


AUTO_STRUCT;
typedef struct XMPP_RosterItemList {
	EARRAY_OF(XMPP_RosterItem) ppRoster;
} XMPP_RosterItemList;

///////////////////////////
// Structs used for passing data between GCS and XMPP Servers

AUTO_STRUCT;
typedef struct XMPP_RosterUpdate
{
	char *jid; AST(ESTRING)
	char **ppGroups;
} XMPP_RosterUpdate;

AUTO_STRUCT;
typedef struct XMPP_ClientStateList
{
	INT_EARRAY eaiStateIDs; // identify by state IDs
	STRING_EARRAY eaHandles; // identify by handles
} XMPP_ClientStateList;

AUTO_STRUCT;
typedef struct XMPP_Message
{
	const char *fromHandle;
	const char *fromResource;
	const char *body;
	const char *subject;
	const char *thread;
	XMPP_MessageType type;
} XMPP_Message;

AUTO_STRUCT;
typedef struct XMPP_StanzaError
{
	XMPP_StanzaErrorCondition error;
	XMPP_StanzaErrorType type;
	const char *element;
	const char *text;

	// For sending ChatRoom errors for the domain
	XMPP_RoomDomain eDomain; AST(DEFAULT(XMPP_RoomDomain_Unknown))
	const char *room;
} XMPP_StanzaError;

AUTO_STRUCT;
typedef struct XMPP_PresenceData
{
	XMPP_PresenceAvailability avail;
	char *handle; AST(ESTRING)
	char *resource; AST(ESTRING)
	U32 priority;
	char *status; AST(ESTRING)

	// For ChatRooms
	XMPP_RoomDomain eDomain; AST(DEFAULT(XMPP_RoomDomain_Unknown))
	char *room; AST(ESTRING)
} XMPP_PresenceData;

AUTO_STRUCT;
typedef struct XMPP_ChatRoomRoster
{
	XMPP_RoomDomain eDomain;
	const char *room;

	XMPP_ChatOccupant **ppOccupants;
	bool bRewrite;
} XMPP_ChatRoomRoster;

// Information about a specific XMPP-accessible channel, for service discovery.
AUTO_STRUCT;
typedef struct XMPP_ChannelInfoData
{
	XMPP_RoomDomain eDomain;
	char *room; AST(ESTRING)
	char *name; AST(ESTRING)
} XMPP_ChannelInfoData;

AUTO_STRUCT;
typedef struct XMPP_ChatRoomMessage
{
	XMPP_RoomDomain eDomain;
	char *room; AST(ESTRING)
	char *nickname; AST(ESTRING)
	char *message; AST(ESTRING)
	char *subject; AST(ESTRING)
	char *thread; AST(ESTRING)
} XMPP_ChatRoomMessage;

AUTO_STRUCT;
typedef struct XMPP_LoginData
{
	U32 uAccountID;
	int iAccessLevel;
	char *accountName;
	char *displayName;
	char *resource;
} XMPP_LoginData;