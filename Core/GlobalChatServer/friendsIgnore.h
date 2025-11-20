#ifndef FRIENDSIGNORE_H
#define FRIENDSIGNORE_H

#include "LocalTransactionManager.h"

typedef struct ChatUser ChatUser;
typedef struct ChatPlayerList ChatPlayerList;

int userIgnore (ChatUser *user, ChatUser *target, bool spammer, const char *pSpamMsg);
int userRemoveIgnore (ChatUser *user, ChatUser *target);

int userAddFriend(ChatUser *user, ChatUser *target);
int userSetFriendComment(ChatUser *user, ChatUser *target, char *pcComment);
int userAcceptFriend(ChatUser *user, ChatUser *target);
int userRejectFriend(ChatUser *user, ChatUser *target);
int userRemoveFriend(ChatUser *user, U32 uTargetID);

typedef struct ChatPlayerStruct ChatPlayerStruct;
typedef struct ChatPlayerList ChatPlayerList;
int userCreateFriendsList(ChatUser *user, ChatPlayerList *list, U32 uChatServerID);
int userCreateIgnoreList(ChatUser *user, ChatPlayerList *list);
void userSendUpdateNotifications(SA_PARAM_NN_VALID ChatUser *updatedUser, U32 uChatServerID);
void userSendUpdateTargetNotifications(SA_PARAM_NN_VALID ChatUser *updatedUser, SA_PARAM_NN_VALID ChatUser *targetFriend, U32 uChatServerID);

typedef enum FriendsReturnCode
{
	FRIENDS_RETURN_ERROR = 0,
	FRIENDS_RETURN_ERROR_ALREADY_FRIENDS,
	FRIENDS_RETURN_ERROR_ALREADY_REQUESTED,
	FRIENDS_RETURN_ERROR_ALREADY_IGNORED,
	FRIENDS_RETURN_ERROR_NOTFRIENDS,
	FRIENDS_RETURN_ERROR_IGNORED,
	FRIENDS_RETURN_ERROR_NOTIGNORED,
	FRIENDS_RETURN_REQUEST_SENT,
	FRIENDS_RETURN_REQUEST_ACCEPTED,
	FRIENDS_RETURN_REQUEST_REJECTED,
	FRIENDS_RETURN_FRIEND_ADDED,
	FRIENDS_RETURN_FRIEND_REMOVED,
	FRIENDS_RETURN_USER_IGNORED,
	FRIENDS_RETURN_USER_IGNORE_REMOVED,
	FRIENDS_RETURN_USER_DNE,
	FRIENDS_RETURN_USER_IS_SELF
} FriendsReturnCode;

ChatPlayerStruct *userCreateChatPlayerStruct(ChatUser *target, ChatUser *user, int flags, U32 uChatServerID, bool bInitialStatus);

#endif