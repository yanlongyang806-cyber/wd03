#ifndef _CHANNELS_H
#define _CHANNELS_H

#include "chatdb.h"

typedef struct ChatMessage ChatMessage;
typedef enum ChatLogEntryType ChatLogEntryType;
typedef struct NOCONST(ChatChannel) NOCONST(ChatChannel);

const char *formatChannelName (SA_PARAM_NN_VALID ChatChannel *channel);
void channelInitialize(SA_PARAM_NN_VALID NOCONST(ChatChannel) *channel, SA_PARAM_NN_STR const char *channel_name, const char *description, U32 uReservedFlags);

bool channelDeleteIfEmpty(ChatDb *db,ChatChannel *channel);
void channelOnline(ChatChannel *channel,ChatUser *user);
void channelSendOnline(ChatChannel *channel,ChatUser *user);
void channelSendOffline(ChatChannel *channel, ChatUser *user);
int channelAddUser(ChatChannel *channel, ChatUser *user, int flags);
int channelJoin(ChatUser *user,const char *channel_name, int flags);
int channelCreate(ChatUser *user,const char *channel_name, int flags, bool adminAccess);
int channelLeave(ChatUser *user,const char *channel_name, int isKicking);
void channelForceKill(ChatChannel *channel);
int channelKill(ChatUser *user,const char *channel_name);
void channelCsrInvite(ChatUser *user,char **args, int count);
bool channelJoinOrCreate_ShardOnly(ChatUser *user, ContainerID uCharacterID, const char *channel_name, int flags);

void channelForward(SA_PARAM_OP_VALID const ChatMessage *pMsg);
void channelSend(SA_PARAM_OP_VALID ChatUser *pFromUser, SA_PARAM_OP_VALID ChatMessage *pMsg);

int channelDonateOwnership (ChatUser *user, ChatUser *target, const char *channel_name);
void channelCsrMembersAccess(ChatUser *user, const char *channel_name, char *option);
void watchingList(ChatUser *user);
void channelList(ChatUser *user, char ** estr);
void channelListMembers(ChatUser *user, const char *channel_name);
void channelGetMotd (ChatUser *user,ChatChannel *channel);
void channelFind(ChatUser *user,char *mode,char *substring);
int channelIdx(ChatUser *user,ChatChannel *channel);

void chooseOwnerIfNecessary(ChatChannel * channel);

#define channelCreateOrJoin(user,channel_name,flags,admin) channelFindByName((channel_name)) ? channelJoin((user),(channel_name), (flags)) : channelCreate((user), (channel_name), (flags), (admin))
bool channelUserHasTargetPermission (ChatChannel *channel, ChatUser *user, ChatUser *target, Watching *userWatching, U32 uAction);
bool channelUserHasPermission (ChatChannel *channel, ChatUser *user, Watching *watching, U32 uAction);

int channelSetLevelPermissions (ChatUser *user, ChatChannel *channel, ChannelUserLevel eLevel, U32 uPermissions);

#endif
