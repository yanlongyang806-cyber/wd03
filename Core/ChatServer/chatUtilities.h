#ifndef CHATUTILITIES_H
#define CHATUTILITIES_H

typedef struct ChatUser ChatUser;
typedef struct ChatUserInfo ChatUserInfo;
typedef struct ChatChannel ChatChannel;
typedef struct Watching Watching;
typedef struct ChatGuild ChatGuild;
typedef struct ChatTeamToJoinList ChatTeamToJoinList;
typedef struct ChatMap ChatMap;
typedef struct ChatMapList ChatMapList;
typedef struct ChatMessage ChatMessage;
typedef struct ChatPlayerList ChatPlayerList;
typedef struct ChatPlayerStruct ChatPlayerStruct;
typedef struct PlayerFindFilterStruct PlayerFindFilterStruct;
typedef enum ChannelAccess ChannelAccess;
typedef struct ChatChannelInfo ChatChannelInfo;

int channelRemoveUser(ChatChannel *channel, ChatUser *user);
void addChatMapCacheToMonitor(void);
void userCharacterChangeMap(const ChatMap *pOldMap, const ChatMap *pNewMap);
int getActiveMaps(ChatMapList *pList);

void ChatFillUserInfo(ChatUserInfo **ppInfo, const ChatUser *pUser);
int getListOfTeams(ChatTeamToJoinList *list, PlayerFindFilterStruct *pFilters);
extern ChatGuild **g_eaChatGuilds;

#endif