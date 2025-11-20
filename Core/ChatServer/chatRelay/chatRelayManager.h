#pragma once

typedef struct NetLink NetLink;
typedef struct ChatUser ChatUser;
typedef enum ChatGMUpdate ChatGMUpdate;
typedef struct ChatUserMinimalList ChatUserMinimalList;

AUTO_STRUCT;
typedef struct ChatRelayServer
{
	U32 uID; AST(KEY)
	char *ipStr; AST(ESTRING) // For the public IP
	int iPort; // ChatRelayServer's public listening port

	INT_EARRAY eaiUsers;
} ChatRelayServer;

AUTO_STRUCT;
typedef struct ChatRelayUserList
{
	U32 uChatRelayID; AST(KEY)
	U32 *eaiUsers;
} ChatRelayUserList;

void ChatServerAddToRelayUserList(SA_PARAM_NN_VALID EARRAY_OF(ChatRelayUserList) *eaRelays, SA_PARAM_NN_VALID ChatUser *user);

void initChatRelayManager(void);
NetLink *ChatRelayManager_GetUserLink(ChatUser *user);

// Called upon receiving ChatUser struct from GlobalChatServer
void crManager_ReceiveChatUser (ChatUser *user);
void crManager_InformRelayGCSReconnect(void);

void crSendGMUpdate(ChatGMUpdate eUpdate, ChatUserMinimalList *pList);
