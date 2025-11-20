#ifndef _MSGSEND_H
#define _MSGSEND_H

#include "chatdb.h"
#include "StringFormat.h"
#include "language\AppLocale.h"

typedef struct NetLink NetLink;
typedef struct ChatPlayerStruct ChatPlayerStruct;
typedef struct ChatRelayUserList ChatRelayUserList;
typedef enum FriendResponseEnum FriendResponseEnum;
typedef enum IgnoreResponseEnum IgnoreResponseEnum;

bool sendChatSystemTranslatedMsg(ChatUser *user, int eType, const char *channel_name, const char *messageKey, ...);
bool sendChatSystemTranslatedMsgV(NetLink *link, ChatUser *user, int eType, const char *channel_name, 
								  Language eLanguage, const char *messageKey, va_list va);

bool sendChatSystemAlert(ChatUser *user, const char *title, const char *msgKey, ...);
//bool sendChatSystemMsg(ChatUser *user, int eType, const char *channel_name, FORMAT_STR char const *fmt, ...);
bool sendChatSystemStaticMsg(ChatUser *user, int eType, const char *channel_name, const char *msg);

void forwardChatMessageToSpys(ChatUser *target, const ChatMessage *pMsg);
void ChatServerMessageSend(ContainerID targetID, const ChatMessage *pMsg);
void ChatServerMessageSendBatch(U32 uChatRelayID, U32 *eaiAccountIDs, const ChatMessage *pMsg);
void ChatServerMessageSendToRelays(EARRAY_OF(ChatRelayUserList) eaRelays, const ChatMessage *pMsg);

void ChatServer_SendFriendUpdate(ChatUser *to, ChatPlayerStruct *friendStruct, FriendResponseEnum eResponse, const char *errorString);
void ChatServer_SendIgnoreUpdate(ChatUser *to, ChatPlayerStruct *IgnoreStruct, IgnoreResponseEnum eResponse, const char *errorString);

bool ChatServerIsValidMessage(const ChatUser *from, const char *msg);

void ChatServerForwardFriendsList(ChatUser *user, ChatPlayerList *friendList);
void ChatServerForwardIgnoreList(ChatUser *user, ChatPlayerList *ignoreList);

typedef struct ChatMailList ChatMailList;
void ChatServerForwardMail(ChatUser *user, ChatMailList *mailList);
void ChatServerForwardUnreadMailBit(ChatUser *user, bool hasUnreadMail);
void ChatServerForwardMailConfirmation(ChatUser *user, U32 auctionLot, const char *errorString);

void ChatServer_SendWhiteListUpdate(ChatUser *to);

// Wrapper for RemoteCommand_ServerChat_ChannelUpdate
void ChannelSendUpdateToUser (ChatUser *user, ChatChannelInfo *info, ChannelUpdateEnum eUpdateType);
void ChannelUpdateFromGlobal (ChatChannelUpdateData *update);

#endif
