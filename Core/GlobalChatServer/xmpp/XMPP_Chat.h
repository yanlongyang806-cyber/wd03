// Interface between the bulk of the Global Chat Server and XMPP.
// It knows a lot about the GCS but not so much about XMPP.

#pragma once
GCC_SYSTEM

typedef struct ChatUser ChatUser;
typedef struct TlsSession TlsSession;
typedef struct NetLink NetLink;
typedef struct ChatMessage ChatMessage;
typedef struct ChatGuild ChatGuild;
typedef struct XmppClientState XmppClientState;
typedef struct XMPP_RosterItem XMPP_RosterItem;
typedef struct XMPP_ChatOccupant XMPP_ChatOccupant;

#include "XMPP_Structs.h"

// Replace spaces with '+'s.
void XMPPChat_EscapeSpaces(char *pString);
// Replace '+'s with spaces.
void XMPPChat_DeescapeSpaces(char *pString);

/************************************************************************/
/* Interface for Global Chat Server                                     */
/************************************************************************/

// Initialize XMPP.
void XMPPChat_Init(void);

// Process pending XMPP activity.
void XMPPChat_Tick(F32 elapsed);

// Propagate presence updates from the Global Chat Server to the XMPP clients.
void XMPPChat_RecvPresence(ChatUser *from, const char *handle);

// Route a message from the Global Chat Server to an XMPP client.
void XMPPChat_RecvPrivateMessage(const ChatMessage *msg);

// Route a message from the Global Chat Server to special channel.
void XMPPChat_RecvSpecialMessage(const ChatMessage *msg);

// Notify XMPP clients that a user has been added to the guild.
void XMPPChat_GuildListAdd(ChatUser *user, ChatGuild *guild);

// Notify XMPP clients that a user has been removed from the guild.
void XMPPChat_GuildListRemove(ChatUser *user, ChatGuild *guild, bool bWasOfficer, bool bKicked);

// If a user is logged in with XMPP, notify him of the incoming friends request.
void XMPPChat_NotifyFriendRequest(ChatUser *user, ChatUser *fromUser);

// If a user is logged in with XMPP, notify him that he has a new friend.
void XMPPChat_NotifyNewFriend(ChatUser *user, ChatUser *fromUser);

// If a user is logged in with XMPP, notify him that he has a new friend.
void XMPPChat_NotifyFriendRemove(ChatUser *user, ChatUser *fromUser);

// A user has spoken in a channel.
void XMPPChat_NotifyChatMessage(ChatChannel *channel, const ChatMessage *message);

// A ChatUser has joined a global channel from in-game.
void XMPPChat_NotifyChatOnline(ChatChannel *channel, ChatUser *user);

// A user has left a channel or been kicked.
void XMPPChat_NotifyChatPart(ChatChannel *channel, ChatUser *user, bool bKicked);

// A ChatUser just logged in for the guild
void XMPPChat_NotifyGuildOnline(ChatGuild *guild, ChatUser *user, bool bIsOfficer);

// A ChatUser just logged out for the guild
void XMPPChat_NotifyGuildOffline(ChatGuild *guild, ChatUser *user, bool bIsOfficer);

/************************************************************************/
/* Interface for XMPP Gateway                                           */
/************************************************************************/

// Log in an XMPP resource.
void XMPPChat_Login(ChatUser *user, const char *resource, U32 accessLevel);

// Log out an XMPP resource.
void XMPPChat_Logout(ChatUser *user, XmppClientState *state);

// Process the initial presence from a client.
void XMPPChat_InitialPresence(SA_PARAM_NN_VALID XmppClientState *state);

// Set presence for a particular resource.
void XMPPChat_SetPresence(XmppClientState *state, int availability, const char *status_msg);

// Send a private message from a resource.
bool XMPPChat_SendPrivateMessage(XmppClientState *from, const char *to, const char *subject, const char *body, const char *thread,
								 const char *id, int type);

// Add an item to the XMPP roster without necessarily subscribing.
bool XMPPChat_RosterUpdate(XmppClientState *from, const char *jid, const char *const *groups);

// Remove an item from the XMPP roster.
bool XMPPChat_RosterRemove(XmppClientState *from, const char *jid);

// Return the full JID of an XMPP client.
const char *xmpp_client_jid(const XmppClientState *state);

// Return the bare JID of an XMPP client.
const char *xmpp_client_bare_jid(const XmppClientState *state);

// Get a roster item for a particular JID.
XMPP_RosterItem *XMPPChat_GetFriend(XmppClientNode *node, ChatUser *user, const char *jid);

// Get friends list.
bool XMPPChat_GetFriends(XmppClientState *state, XMPP_RosterItem ***eaRoster);

// Get the XMPP username associated with an XMPP client.
const char *XMPPChat_GetXmppUsername(XmppClientState *state);

// Request friendship.
bool XMPPChat_AddFriend(XmppClientState *state, const char *to);

// Accept a friend request.
bool XMPPChat_AcceptFriend(XmppClientState *state, const char *to);

// Remove friend or decline a friends request, as appropriate.
bool XMPPChat_RemoveFriend(XmppClientState *state, const char *to);

// Get the list of outstanding incoming friend requests.
void XMPPChat_IncomingFriendRequests(char ***eaRequests, XmppClientState *state);

// Return true if this jid is a friend.
bool XMPPChat_IsFriend(XmppClientState *state, const char *jid);

// Return the node name for a logged-in user.
const char *XMPPChat_NodeName(XmppClientState *state);

// Return the node name for a logged-in user.
XmppClientNode *XMPPChat_ClientNode(XmppClientState *state);

// Return true if the user for this client is a member of a guild.
bool XMPPChat_HasGuilds(XmppClientState *state);

// Return true if the user for this client is an officer of any guild.
bool XMPPChat_IsOfficer(XmppClientState *state);

/************************************************************************/
/* Interface for XMPP Gateway: Chat channel commands                    */
/************************************************************************/

// Join a channel.
enum XMPP_ChannelJoinStatus XMPPChat_ChatJoin(XmppClientState *state, int domain, const char *room);

// Send a message to a channel.
bool XMPPChat_ChatMessage(XmppClientState *state, int domain, const char *room, const char *text);

// Leave a channel.
bool XMPPChat_ChatLeave(XmppClientState *state, int domain, const char *room);

// Change room subject.
bool XMPPChat_ChatSubject(XmppClientState *state, int domain, const char *room, const char *text);

// Get chat occupant information.
XMPP_ChatOccupant *XMPPChat_ChatOccupant(int domain, const char *room, const char *roomnick);

// Get list of room occupants.
void XMPPChat_ChatOccupants(XmppClientState *state, XMPP_ChatOccupant ***eaOccupants, int domain, const char *room);

// Get information about a channel.
bool XMPPChat_ChannelInfo(SA_PARAM_NN_VALID XmppChannelInfo *channel, int domain, const char *room);

// Get the list of channels that might be interesting to a user.
void XMPPChat_ChannelsForUser(XmppChannelInfo ***eaChannels, int eDomain, XmppClientState *state);

// Get the node and the description (name) for the Channel Domain type 
// data == ChatGuild for XMPP_RoomDomain_Guild/Officer, == ChatChannel for XMPP_RoomDomain_Channel
void XMPPChat_GetXMPPChannelInfo (char **estrNode, char **estrDescription, void *data, int eDomain);
