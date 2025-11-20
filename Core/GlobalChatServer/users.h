#ifndef _USERS_H
#define _USERS_H

#include "chatdb.h"
#include "chatCommon.h"

typedef struct PlayerInfoStruct PlayerInfoStruct;
typedef struct ChatMailStruct ChatMailStruct;
typedef struct CmdContext CmdContext;
typedef struct NetLink NetLink;
typedef struct ChatLoginData ChatLoginData;
typedef struct ChatGuild ChatGuild;
typedef struct ChatGuildMember ChatGuildMember;

int userCanTalk(ChatUser *user);

void userAddOrUpdate(const ChatLoginData *pLoginData);
void userXmppCreateAndLogin(U32 uStateID, SA_PARAM_NN_STR const char *xmppID, 
	ContainerID uAccountID, SA_PARAM_NN_STR const char *accountName, SA_PARAM_NN_STR const char *handle, SA_PARAM_NN_STR const char *resource, int access_level);
int userAddAndLogin(ContainerID accountID, ContainerID characterID, const char *accountName, const char *displayName, int access_level,
					U32 uChatLinkID, const PlayerInfoStruct *pPlayer);
void userLogin(ChatUser *user, ContainerID characterID, U32 uChatServerID, bool bXmppLogin);
void userLoginOnly(ChatUser *user, const ChatLoginData *pLoginData, U32 uChatLinkID, bool bXmppUser);
void userLogout(ChatUser *user, U32 uChatServerID);
void userLogoutAllByChatID(U32 uChatServerID);
void userLoginPlayerUpdate(ChatUser *user, const PlayerInfoStruct *pPlayerInfo, U32 uChatServerID);
void userPlayerUpdateGuild(U32 uChatLinkID, ChatUser *user, ChatGuild *guild, ChatGuildMember *member);

void userOnlineUpdateMailbox(SA_PARAM_NN_VALID ChatUser *user, NetLink *chatLink);
void userOnlineUpdateMailboxPaged(SA_PARAM_NN_VALID ChatUser *user, NetLink *chatLink, int iPage, int iPageSize);
//Only checks for unread mail; doesn't update the entire mailbox
bool userHasUnreadMail(ChatUser *user);
ChatUser *userAdd(CmdContext *context, ContainerID id, const char *accountName, const char *chatHandle);
int userDelete(U32 uID);
int userAddEmail(ChatUser *sender, ChatUser *recipient, const char *shardName, const char *subject, const char *body,
				 EMailType emailType, const char *sendName, S64 npcEmailID, U32 futureSendTime, bool bRateLimit);
int userImportEmail(ChatUser *sender, ChatUser *recipient, const char *shardName, const char *subject, const char *body, U32 sendDate, short read);
int userAddEmailEx(ChatUser *sender, ChatUser *recipient, const char *shardName, ChatMailStruct *mail);
int userDeleteEmail(ChatUser *user, U32 uMailID);
int userSetMailAsRead(ChatUser *user, U32 uMailID, bool bRead);
int userGetEmailPaged(ChatUser *user, ChatMailList *list, int iPageSize, int iPageOffset, int iOrder, bool bGetBody, bool getFuture);
int userGetEmail(ChatUser *user, ChatMailList *list, bool bGetBody, bool getFuture);
void userSend(ChatUser *user,ContainerID target_id,char *msg);
void userCsrName(ChatUser *user,char *old_name,char *new_name);

int userWatchingCount(ChatUser *user);
void userNotifyOnlineStatusChange(ChatUser *user, UserStatus oldStatus);

int userGetRelativeDbId(ChatUser * user, ChatUser * member);

bool userChangeChatHandle(ContainerID userID, const char *userHandle);
bool userSetAccountName(ChatUser *user, const char *accountName);
void userSetLastLanguage(ChatUser *user, Language eLanguage);
int userChangeStatus (ChatUser *user, UserStatus addStatus, UserStatus removeStatus, const char *msg, bool bSetStatusMessage);

void userAddSpy(SA_PARAM_NN_VALID ChatUser *user, SA_PARAM_NN_VALID ChatUser *target);
void userRemoveSpy(SA_PARAM_NN_VALID ChatUser *user, SA_PARAM_NN_VALID ChatUser *target);

typedef struct ChatMessage ChatMessage;
int userSendPrivateMessage(ChatUser *from, ChatUser *to, ChatMessage *pMsg, NetLink *fromLink);

extern int g_silence_all;

int UserAFK(ContainerID userID, const char *msg);
int UserDND(ContainerID userID, const char *msg);
int UserBack(ContainerID userID);
int UserSetStatus(ContainerID userID, const char *msg);

void UserAFKWithMessage(SA_PARAM_NN_VALID ChatUser *user, const char *msg);
void UserDNDWithMessage(SA_PARAM_NN_VALID ChatUser *user, const char *msg);
void UserBackWithMessage(SA_PARAM_NN_VALID ChatUser *user, const char *msg);

#endif

