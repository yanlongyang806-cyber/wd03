#pragma once

typedef struct ChatUser ChatUser;
typedef struct ChatChannel ChatChannel;
typedef struct ChatGuild ChatGuild;
typedef struct ChatMessage ChatMessage;
typedef struct PlayerInfoStruct PlayerInfoStruct;
typedef struct XmppClientState XmppClientState;
typedef struct XmppClientNode XmppClientNode;
typedef struct XMPPChat_GuildMembers XMPPChat_GuildMembers;
typedef enum XMPP_RoomDomain XMPP_RoomDomain;

// Join the ChatRoom; state is optional - if non-NULL, the join was from XMPP; if NULL, the join was from the game
// ChatUser is required
void XMPPChatRoom_NotifyJoin (XmppClientState *state, ChatUser *user, int eDomain, const char *room);
// Leave the ChatRoom from everything
void XMPPChatRoom_NotifyLeaveAll (ChatUser *user, int eDomain, const char *room, bool bKicked);
// Leave the ChatRoom from a single state
void XMPPChatRoom_NotifyLeaveSingle (XmppClientState *state, ChatUser *user, int eDomain, const char *room, void *data);

// Gets the channel info for the specified type of room
void XMPPChat_GetXMPPChannelInfo (char **estrNode, char **estrDescription, void *data, int eDomain);

// Returns the ChatChannel for XMPP_RoomDomain_Channel
// Returns the ChatGuild for XMPP_RoomDomain_Guild/Officer
void *XMPPChat_GetChatRoomData(XMPP_RoomDomain eDomain, const char *pName);

// Notifies for joining/leaving the game for channels
void XMPPChatRoom_NotifyGameJoin (ChatUser *user, int eDomain, void *data);
void XMPPChatRoom_NotifyLeaveGame (ChatUser *user, int eDomain, void *data);

// Converts the XMPP-version of the channel name into the GCS-version
bool XMPPChat_GetChannelName(char **estrName, enum XMPP_RoomDomain eDomain, const char *pName);
// Get the shard name and guild name from a special XMPP channel name.
bool XMPPChat_SeparateSpecialChannel(char **estrShard, char **estrGuild, const char *pName);

// Functions for checking if the user is online in the room
bool XMPPChat_ChannelSubscriberIsOnline(ChatUser *user, ChatChannel *channel, const char *room);
bool XMPPChat_GuildMemberIsOnline(ChatUser *user, ChatGuild *guild, const char *room);
bool XMPPChat_GuildOfficerIsOnline(ChatUser *user, ChatGuild *guild, const char *room);

// Guild Chat Room Functions
XMPP_ChannelJoinStatus XMPPChat_GuildChatCanJoin(XmppClientState *state, XMPP_RoomDomain domain, const char *room);
bool XMPPChat_GuildChatMessage(XmppClientState *state, int domain, const char *room, const char *text);
bool XMPPChat_GuildChatLeave(XmppClientState *state, int domain, const char *room);
void XMPPChat_RecvGuildChatMessage (const ChatMessage *msg);

XMPP_ChatOccupant *XMPPChat_GuildChatOccupant(int domain, const char *room, const char *roomnick);
void XMPPChat_GuildChatOccupants(XMPP_ChatOccupant ***eaOccupants, int domain, const char *room, U32 uSelfID);
void XMPPChat_GuildMembersOnline(ChatUser ***eaMembers, ChatGuild *guild);
void XMPPChat_GuildOfficersOnline(ChatUser ***eaMembers, ChatGuild *guild);
ChatGuild *XMPPChat_GetGuildByRoom(const char *room);
//void XMPPChat_CreateGuildKey(char **estrKey, const char *shardName, U32 guildId);


// Channel Chat Room Functions
XMPP_ChannelJoinStatus XMPPChat_ChannelJoin(XmppClientState *state, XMPP_RoomDomain domain, const char *room);
bool XMPPChat_ChannelMessage(XmppClientState *state, int domain, const char *room, const char *text);
bool XMPPChat_ChannelLeave(XmppClientState *state, int domain, const char *room);
void XMPPChat_ChannelOccupants(XMPP_ChatOccupant ***eaOccupants, int domain, const char *room, U32 uSelfID);