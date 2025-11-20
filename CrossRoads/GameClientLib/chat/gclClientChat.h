#pragma once
GCC_SYSTEM

typedef struct ChatConfig ChatConfig;
typedef struct ChatPlayerStruct ChatPlayerStruct;
typedef struct ChatState ChatState;

ChatConfig * gclClientChat_GetAccountChatConfig(void);
const char *gclClientChat_GetAccountDisplayName(void);
U32 gclClientChat_GetAccountID(void);
U32 gclClientChat_GetAccessLevel(void);

ChatState *ClientChat_GetChatState(void);
void gclClientChat_ClearChatState(void);
void gclChat_FriendByName(ACMD_SENTENCE name);

EARRAY_OF(ChatPlayerStruct) *gclChat_GetFriends(void);
EARRAY_OF(ChatPlayerStruct) *gclChat_GetIgnores(void);

// Channel commands
#ifdef USE_CHATRELAY
void gclClientChat_ChannelPromoteUser(const char *channel_name, ACMD_SENTENCE targetHandle);
void gclClientChat_ChannelDemoteUser(const char *channel_name, ACMD_SENTENCE targetHandle);
void gclClientChat_ChannelKickUser(const char *channel_name, ACMD_SENTENCE targetHandle);
void gclClientChat_MuteUser(const char *channel_name, ACMD_SENTENCE targetHandle);
void gclClientChat_UnmuteUser(const char *channel_name, ACMD_SENTENCE targetHandle);

void gclClientChat_ChannelInvite(const char *channel_name, ACMD_SENTENCE handle);
void gclClientChat_SetChannelMotd(const char *channel_name, ACMD_SENTENCE motd);
void gclClientChat_SetChannelDescription(const char *channel_name, ACMD_SENTENCE description);
void gclClientChat_DestroyChannel(ACMD_SENTENCE channel_name);

void gclClientChat_SetChannelAccess(const char *channel_name, char *accessString);
#endif
