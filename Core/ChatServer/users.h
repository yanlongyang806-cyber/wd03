#ifndef _USERS_H
#define _USERS_H

#include "chatdb.h"
#include "chatCommon.h"

typedef struct ChatMailStruct ChatMailStruct;
typedef struct ChatUserMinimal ChatUserMinimal;
typedef struct PlayerInfoStruct PlayerInfoStruct;
typedef struct CmdContext CmdContext;
typedef struct NetLink NetLink;

ChatUser *userFindByContainerId(ContainerID id);
ChatUser *userFindByContainerIdSafe(ChatUser *from, ContainerID id);
ChatUser *userFindByHandle(const char *handle);
ChatUser *userFindByAccountName(const char *accountName);
ChatUser *userFind(ChatUser * from, char * id, int idType);
int userCanTalk(ChatUser *user);
bool userIsSocialRestricted(ChatUser *user);

// Get the locally stored list of logged-in GMs
EARRAY_OF(ChatUserMinimal) userGetGMList(void);
bool userIsGM(U32 uID, const char *handle);

//If the user is online somewhere
bool userOnline( ChatUser *user );
//If the user is online in game, subtle difference
bool userCharacterOnline( ChatUser *user);

// Handles some cleanup on local call of login, logout, and updates
void userLocalLogin(SA_PARAM_NN_VALID ChatUser *user);
void userLocalLogout(SA_PARAM_NN_VALID ChatUser *user);
void userLocalPlayerUpdate(SA_PARAM_NN_VALID ChatUser *user, SA_PARAM_NN_VALID const PlayerInfoStruct *pPlayerInfo, ChatUserUpdateEnum eForwardToGlobalFriends);

void userOnlineUpdateMailbox(SA_PARAM_NN_VALID ChatUser *user, NetLink *chatLink);
//Only checks for unread mail; doesn't update the entire mailbox
void userOnlineCheckMail(SA_PARAM_NN_VALID ChatUser *user, NetLink *chatLink);
int userDelete(U32 uID);
int userSetMailAsRead(ChatUser *user, U32 uMailID, bool bRead);
int userGetEmailPaged(ChatUser *user, ChatMailList *list, int iPageSize, int iPageOffset, int iOrder, bool bGetBody, bool getFuture);
int userGetEmail(ChatUser *user, ChatMailList *list, bool bGetBody, bool getFuture);
//Just checks for unread messages
bool userCheckEmail(ChatUser *user, bool getFuture);
void userSend(ChatUser *user,ContainerID target_id,char *msg);
void userCsrName(ChatUser *user,char *old_name,char *new_name);

int userWatchingCount(ChatUser *user);

int userGetRelativeDbId(ChatUser * user, ChatUser * member);

bool userChangeChatHandle(ContainerID userID, const char *userHandle);
bool userSetAccountName(ChatUser *user, const char *accountName);
void userSetLastLanguage(ChatUser *user, Language eLanguage);

int userBroadcastGlobal(char *msg);
void userAddSpy(SA_PARAM_NN_VALID ChatUser *user, SA_PARAM_NN_VALID ChatUser *target);
void userRemoveSpy(SA_PARAM_NN_VALID ChatUser *user, SA_PARAM_NN_VALID ChatUser *target);

extern int g_silence_all;

#if CHATSERVER
int UserAFK(CmdContext *context, ContainerID userID, const char *msg);
int UserDND(CmdContext *context, ContainerID userID, const char *msg);
int UserBack(CmdContext *context, ContainerID userID);
void UserSetStatus(ContainerID userID, const char *msg);
#endif	//CHATSERVER

void userSendActivityStatus(SA_PARAM_NN_VALID ChatUser *user);
void ChatPlayerInfo_SendActivityStringUpdate(PlayerStatusChange *pStatusChange);

bool userThrottleLocalSpam(SA_PARAM_NN_VALID ChatUser *user);

#endif

