#ifndef _CHANNELS_H
#define _CHANNELS_H

#include "chatdb.h"

typedef struct ChatMessage ChatMessage;
typedef struct ChatChannelUpdateData;
typedef enum ChatLogEntryType ChatLogEntryType;
typedef struct NOCONST(ChatChannel) NOCONST(ChatChannel);

void channelInitialize(SA_PARAM_NN_VALID NOCONST(ChatChannel) *channel, SA_PARAM_NN_STR const char *channel_name, const char *description, U32 uReservedFlags);

bool channelDeleteIfEmpty(ChatDb *db,ChatChannel *channel);
void channelOnline(ChatChannel *channel,ChatUser *user);
void channelOffline(ChatChannel *channel,ChatUser *user);
int channelAddUser(ChatChannel *channel, ChatUser *user, int flags);
int channelJoin(ChatUser *user,const char *channel_name, int flags);
int channelCreate(ChatUser *user,const char *channel_name, int flags, bool adminAccess);
int channelJoinOrCreate(ChatUser *user,const char *chan_name, int flags, bool adminAccess);
int channelLeave(ChatUser *user,const char *channel_name, bool bIsKicking);
void channelForceKill(ChatChannel *channel);
int channelKill(ChatUser *user,const char *channel_name);
int channelInviteGlobal(const char * inviter_handle, const char *channel_name, ChatUser* invitee);
int channelInvite(ChatUser * user, const char *channel_name, ChatUser* invitee);
int channelUninvite(ChatUser * user, ChatUser *target, const char *channel_name, bool bOverride);
int channelDeclineInvite(ChatUser * user, const char *channel_name);
void channelCsrInvite(ChatUser *user,char **args, int count);

void channelForward(SA_PARAM_OP_VALID const ChatMessage *pMsg);
void channelSend(SA_PARAM_OP_VALID ChatUser *pFromUser, SA_PARAM_OP_VALID const ChatMessage *pMsg);
void channelSendSpecial(SA_PARAM_OP_VALID ChatUser *pFromUser, SA_PARAM_OP_VALID const ChatMessage *pMsg, U32 shardId);

int channelSetUserAccess(ChatUser *user, const char *channel_name, ChatUser *target, U32 uAddFlags, U32 uRemoveFlags);
int channelSetChannelAccess(ChatUser *user,const char *channel_name,char *option);
int channelDonateOwnership (ChatUser *user, ChatUser *target, const char *channel_name);
void channelCsrMembersAccess(ChatUser *user, const char *channel_name, char *option);
void channelListMembers(ChatUser *user, const char *channel_name);
void channelGetList(ChatUser ***eaMembers, const char *channel_name);
void channelGetListOnline(ChatUser ***eaMembers, const char *channel_name);
int channelSetMotd(ChatUser *user,const char *channel_name,const char *motd);
void channelGetMotd (ChatUser *user,ChatChannel *channel, bool bForceSend);
int channelSetDescription(ChatUser *user, const char *channel_name, char *description);
void channelFind(ChatUser *user,char *mode,char *substring);
int channelIdx(ChatUser *user,ChatChannel *channel);

void chooseOwnerIfNecessary(ChatChannel * channel);

#define channelCreateOrJoin(user,channel_name,flags,admin) channelFindByName((channel_name)) ? channelJoin((user),(channel_name), (flags)) : channelCreate((user), (channel_name), (flags), (admin))
bool channelUserHasTargetPermission (ChatChannel *channel, ChatUser *user, ChatUser *target, Watching *userWatching, U32 uAction);
bool channelUserHasPermission (ChatChannel *channel, ChatUser *user, Watching *watching, U32 uAction);

int channelSetLevelPermissions (ChatUser *user, ChatChannel *channel, ChannelUserLevel eLevel, U32 uPermissions);

// Functions for pushing channel-related updates to users
void channelUpdateSendToShards(ChatUser *user, ChatChannelUpdateData *data);
void channelGeneralInfoUpdate (ChatChannel *channel, ChannelUpdateEnum eUpdateType);
void channelMemberInfoUpdate(ChatUser *user, ChatChannel *channel);
void channelUserJoinUpdate(ChatUser *user, ChatChannel *channel, bool bInvite);
void channelUserRemoveUpdate(ChatUser *user, ChatChannel *channel);

#endif
