#ifndef FRIENDSIGNORE_H
#define FRIENDSIGNORE_H

#include "LocalTransactionManager.h"

typedef struct ChatUser ChatUser;
typedef struct ChatPlayerList ChatPlayerList;

bool userIsIgnoringByID (ChatUser *to, ContainerID fromID, bool bIsGm);
bool userIsIgnoring (ChatUser *to, ChatUser *from);
bool userIsFriend (ChatUser *user, U32 targetID);

typedef struct ChatPlayerStruct ChatPlayerStruct;
typedef struct ChatPlayerList ChatPlayerList;
void userSendUpdateNotifications(SA_PARAM_NN_VALID ChatUser *updatedUser);

void banSpammer(ChatUser *user);

typedef struct NOCONST(ChatUser) NOCONST(ChatUser);
int trRemoveFriendRequests(ATH_ARG NOCONST(ChatUser) *user, ATH_ARG NOCONST(ChatUser) *target);

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

#endif