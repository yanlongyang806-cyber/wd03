#pragma once

typedef struct ChatUser ChatUser;
typedef struct PlayerInfoStruct PlayerInfoStruct;
typedef struct XmppClientState XmppClientState;
typedef struct XmppClientNode XmppClientNode;
typedef struct StashTableImp* StashTable;

typedef struct XmppServerState
{
	// False if XMPP has not been initialized by XMPPChat_Init().
	bool xmppInitialized;

	// Currently logged-in XMPP clients, by node, indexed by user ID
	StashTable stXmppNodes;

	// Currently logged-in XMPP clients, by state, indexed by state ID
	StashTable stXmppStates;

	// Chat Room data, indexed by XMPP_RoomDomain enums and room name
	StashTable stChatRooms[XMPP_RoomDomain_Max];
} XmppServerState;

void XMPP_ClearServerState(void);
XmppServerState * xmpp_getserverstate(void);
XmppClientState *XmppServer_AddClientState(SA_PARAM_NN_VALID ChatUser *user, U32 uStateID, const char *resource);
void XmppServer_RemoveClientState(SA_PARAM_NN_VALID ChatUser *user, XmppClientState *state);
XmppClientState * XmppServer_FindClientStateById(U32 uStateID);

// Get a resource name from a JID, if any.
char *xmpp_make_resource_from_jid(const char *jid);

// Get a ChatUser that matches a JID, if any.
PlayerInfoStruct *xmpp_playerinfo_from_jid(const char *jid, ChatUser *user);

// Get a ChatUser that matches a JID, if any.
ChatUser *xmpp_chatuser_from_jid(const char *jid);

// Get the ChatUser object associated with a XmppClientState object.
ChatUser *xmpp_chatuser_from_clientstate(const XmppClientState *state);

// If this user is logged in with XMPP, return their client node.  Otherwise, return NULL.
XmppClientNode *xmpp_node_from_id(U32 uAccountID);
XmppClientNode *xmpp_node_from_chatuser(const ChatUser *user);

// Return the chat user from the client node.
ChatUser *xmpp_chatuser_from_node(const XmppClientNode *node);

// If this user is logged in with XMPP, return their client state object.  Otherwise, return NULL.
XmppClientState *xmpp_clientstate_from_chatuser(const ChatUser *user, const char *resource);