#ifndef _MSGSEND_H
#define _MSGSEND_H

#include "chatdb.h"
#include "StringFormat.h"
#include "language\AppLocale.h"

typedef struct NetLink NetLink;
typedef struct ChatPlayerStruct ChatPlayerStruct;
typedef enum FriendResponseEnum FriendResponseEnum;
typedef enum IgnoreResponseEnum IgnoreResponseEnum;

bool sendChatSystemTranslatedMsgV(NetLink *link, ChatUser *user, int eType, const char *channel_name, 
								  Language eLanguage, const char *messageKey, va_list va);
bool sendChatSystemStaticMsg(ChatUser *user, int eType, const char *channel_name, const char *msg);

void forwardChatMessageToSpys(ChatUser *target, const ChatMessage *pMsg);
void ChatServerMessageSend(ContainerID targetID, const ChatMessage *pMsg);

void ChatServerChannelJoinSuccess(ChatUser *user, const char *channel_name);
void ChatServerChannelLeaveSuccess(ChatUser *user, const char *channel_name);

void ChatServer_SendFriendUpdateSingleShard(ChatUser *to, ChatPlayerStruct *friendStruct, FriendResponseEnum eResponse, U32 uChatServerID);

#define ChatServer_SendFriendUpdate(to,friendStruct,eResponse) ChatServer_SendFriendUpdateInternal(to,friendStruct,eResponse,NULL,NULL,NULL)
#define ChatServer_SendFriendUpdateError(to,eResponse,error,handle) ChatServer_SendFriendUpdateInternal(to,NULL,eResponse,error,handle,NULL)
void ChatServer_SendFriendUpdateInternal(ChatUser *to, ChatPlayerStruct *friendStruct, FriendResponseEnum eResponse, const char *errorString, const char *errorHandle, NetLink *originator);

#define ChatServer_SendIgnoreUpdate(to,ignoreStruct,eResponse) ChatServer_SendIgnoreUpdateInternal(to,ignoreStruct,eResponse,NULL,NULL,NULL)
#define ChatServer_SendIgnoreUpdateError(to,eResponse,error,handle) ChatServer_SendIgnoreUpdateInternal(to,NULL,eResponse,error,handle,NULL)
void ChatServer_SendIgnoreUpdateInternal(ChatUser *to, ChatPlayerStruct *ignoreStruct, IgnoreResponseEnum eResponse, const char *errorString, const char *errorHandle, NetLink *originator);

void ChatServerUserFriendRequest(ChatUser *from, ChatUser *to, U32 uChatServerID);
void ChatServerUserFriendAccept(ChatUser *user, ChatUser *target, U32 uChatServerID);
void ChatServerUserFriendReject(ChatUser *user, ChatUser *target);
void ChatServerUserFriendRemove(ChatUser *user, ChatUser *target);
void ChatServerUserFriendOnlineNotify(ChatUser *user, ChatUser *target, bool bOnline);

void ChatServerUserIgnoreAdd(ChatUser *user, ChatUser *target);
void ChatServerUserIgnoreRemove(ChatUser *user, ChatUser *target);

bool ChatServerIsValidMessage(const ChatUser *from, const char *msg);

void ChatServerForwardWhiteListInfo(ChatUser *user);

#endif
