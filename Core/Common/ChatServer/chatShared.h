#ifndef CHATSHARED_H
#define CHATSHARED_H

// Chat Server protocol versions
// WARNING: You must get permission from both an Operations Platform Producer AND an Infrastructure/Platform programmer
// before changing these values.  They will probably say 'no' unless you have a good reason.  This is because any change
// to these will probably immediately break a QA shard and also require deployment of a new Global Chat Server.  In general,
// changes to the Chat Server protocol must be both forward and backward compatible.
// Major Revision Number for Global Chat-to-Shard Chat communication: must match on both sides for chat to work
#define CHATSERVER_MAJOR_REVISION 1
// Minor Revision Number: Revision on GCS must be greater than or equal to the Shard Chat Server's to work
#define CHATSERVER_MINOR_REVISION 5

typedef struct ChatUser ChatUser;
typedef struct ChatUserInfo ChatUserInfo;
typedef struct ChatChannel ChatChannel;
typedef struct Watching Watching;
typedef struct ChatMessage ChatMessage;
typedef struct ChatPlayerList ChatPlayerList;
typedef struct ChatPlayerStruct ChatPlayerStruct;
typedef struct PlayerFindFilterStruct PlayerFindFilterStruct;
typedef struct ChatChannelInfo ChatChannelInfo;
typedef struct ChatChannelMember ChatChannelMember;
typedef struct PlayerInfoStruct PlayerInfoStruct;
typedef enum ChannelUpdateEnum ChannelUpdateEnum;
typedef enum UserStatus UserStatus;

#include "qsortG.h"

// All Functions that take a uChatServerID should have 0 passed in for that parameter on the Shard Chat Server

LATELINK;
bool channelNameIsReserved(const char *channel_name);
int channelIsNameValid(const char *channel_name, bool adminAccess, int flags);

PlayerInfoStruct * findPlayerInfoByLocalChatServerID(ChatUser *user, U32 uChatServerID);
ChatUser *userFindByContainerId(U32 id);
ChatUser *userFindByContainerIdSafe(ChatUser *from, U32 id);
ChatUser *userFindByHandle(const char *handle);
ChatUser *userFindByAccountName(const char *accountName);

//If the user is online somewhere
bool userOnline( ChatUser *user );
//If the user is online in game, subtle difference
bool userCharacterOnline( ChatUser *user);

bool userIsBanned(ChatUser *user);
LATELINK;
bool userIsSilenced(ChatUser *user);
void userGetSilenceTimeLeft (ChatUser *user, U32 *hour, U32 *min, U32 *sec);
int userSilencedTime(ChatUser * user);

bool userIsIgnoringByID (ChatUser *to, U32 fromID, bool bIsGm);
bool userIsIgnoring (ChatUser *user, ChatUser *ignore);
bool userCanIgnore(ChatUser *user, ChatUser *ignore);
bool userIsFriend (ChatUser *user, U32 targetID);
bool userIsFriendSent(ChatUser *user, U32 targetID);
bool userIsFriendReceived(ChatUser *user, U32 targetID);

bool ChatServerHandleIsValid(const char *targetHandle);
ChatPlayerStruct *createChatPlayerStruct (SA_PARAM_NN_VALID ChatUser *target, SA_PARAM_OP_VALID ChatUser *user, int flags, U32 uChatServerID, bool bInitialStatus);
Watching * userFindWatching(ChatUser *user, ChatChannel *channel);
int getOnlineList(ChatPlayerList *list, PlayerFindFilterStruct *pFilters, U32 uChatServerID);

bool userPassesFilters(ChatUser *user, PlayerFindFilterStruct *pFilters, U32 uChatServerID);
void ChatStripNonGlobalData(ChatMessage *pMsg);

int getChannelMemberCount(SA_PARAM_NN_VALID ChatChannel *channel);
ChatChannelMember *copyChannelMember (SA_PARAM_NN_VALID ChatUser *user, SA_PARAM_NN_VALID ChatChannel *channel, Watching *watch);
ChatChannelInfo *copyChannelInfo(ChatUser *user, ChatChannel *channel, Watching *watching, bool bCopyMembers, bool bShowGMCount);
ChatChannelInfo * ChatServerCreateChannelUpdate (ChatUser *user, SA_PARAM_NN_VALID ChatChannel *channel, 
												 ChannelUpdateEnum eUpdateType);

bool isWhitelisted(ChatUser* from, ChatUser* to, const char* channelName, U32 uChatServerID);
bool isEmailWhitelisted(ChatUser* from, ChatUser* to, U32 uChatServerID);

ChatChannel *channelFindByName(const char *channel_name);
ChatChannel *channelFindByID(U32 id);
Watching *channelFindWatching(ChatUser *user, const char *channel_name);
Watching *channelFindWatchingReserved(ChatUser *user, const char *channel_name);
Watching *channelFindWatchingReservedByType(ChatUser *user, int reserved_flag);
bool channelIsUserSilenced(U32 userID, SA_PARAM_NN_VALID ChatChannel *channel);

const char *convertExactChannelName (const char *channelName, U32 uReservedFlags);

bool userChangeActivityString (ChatUser *user, const char *msg);
bool userChangeStatusAndActivity(ChatUser *user, UserStatus addStatus, UserStatus removeStatus, const char *msg, bool bSetStatusMessage);

AUTO_STRUCT;
typedef struct ChatRegisterShardData
{
	char *pShardName;
	char *pShardCategory;
	char *pProduct;
	char *pClusterName;
} ChatRegisterShardData;

// Make sure that the Chat Server protocol version is not accidentally changed.
STATIC_ASSERT_MESSAGE(CHATSERVER_MAJOR_REVISION == 1 && CHATSERVER_MINOR_REVISION == 5,
					  "Please read the comment above the definition of CHATSERVER_MAJOR_REVISION.  If you have permission, "
					  "update the literal in this macro.");

#endif
