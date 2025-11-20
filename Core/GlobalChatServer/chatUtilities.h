#ifndef CHATUTILITIES_H
#define CHATUTILITIES_H

typedef struct ChatUser ChatUser;
typedef struct ChatUserInfo ChatUserInfo;
typedef struct ChatChannel ChatChannel;
typedef struct ChatGuild ChatGuild;
typedef struct ChatPlayerList ChatPlayerList;
typedef struct ChatPlayerStruct ChatPlayerStruct;
typedef struct PlayerFindFilterStruct PlayerFindFilterStruct;
typedef enum ChannelAccess ChannelAccess;

int channelRemoveUser(ChatChannel *channel, ChatUser *user);
int parseAccessString(ChannelAccess *pAccess,  const char *options);
int parseUserAccessStringChanges (U32 *pAdd, U32 *pRemove, const char *options);

bool userPassesFilters(ChatUser *user, PlayerFindFilterStruct *pFilters, U32 uChatServerID);
void ChatFillUserInfo(ChatUserInfo **ppInfo, const ChatUser *pUser);
bool mailRateLimiter(ChatUser *user);
bool chatRateLimiter(ChatUser* user);

#endif