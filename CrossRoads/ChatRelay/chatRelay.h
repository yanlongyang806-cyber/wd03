#pragma once

typedef struct ChatData ChatData;
typedef struct ChatPlayerStruct ChatPlayerStruct;
typedef struct ChatUser ChatUser;
typedef struct Packet Packet;
typedef struct NetLink NetLink;
typedef struct PlayerInfoStruct PlayerInfoStruct;
typedef enum NotifyType NotifyType;
typedef enum DefaultChatConfigSource DefaultChatConfigSource;

AUTO_STRUCT;
typedef struct ChatRelayUser
{
	U32 uAccountID;
	ChatUser *user;
	U32 uAccessLevel;
	bool bSocialRestricted;
	int eLanguage;

	U32 uLinkID; // Link to GameClient used by Chat Relay
	bool bAuthed; // If auth is complete and successful
	U32 uSource; // Either CHAT_CONFIG_SOURCE_PC or _XBOX

	NetLink *link; NO_AST
	STRING_EARRAY eaSubscribedChannels; AST(ESTRING)

	// Online Game Info
	U32 uEntityID;
	
	U32 uLastConfigInitTime; NO_AST
} ChatRelayUser;

ChatRelayUser *chatRelayAddUser(U32 uAccountID, DefaultChatConfigSource eSource, NetLink *link);
ChatRelayUser *chatRelayGetUser(U32 uAccountID);
void chatRelayRemoveUser(U32 uAccountID);

typedef void (*ChatRelayUserIterateFunc) (ChatRelayUser*);
void chatRelayIterate(ChatRelayUserIterateFunc cb);

// Called when ChatServer dies - unauthenticated users cannot be validated
void chatRelayRemoveUnauthenticatedUsers(void);

void chatRelayGetUserIDList(SA_PARAM_NN_VALID INT_EARRAY *eaiList, bool bAuthenticatedOnly);

#define chatRelayUser_GetVShardID(relayUser) (relayUser && relayUser->pPlayerInfo ? relayUser->pPlayerInfo->uVirtualShardID : 0)

// Chat Relay Notify implementation
void chatRelay_NotifySend(ChatRelayUser *relayUser, NotifyType eType, const char *pchDisplayString, const char *pchLogicalString, const char *pchSound, const char *pchTexture, const ChatData *pChatData, const char * pchTag);
#define crNotifySend(relayUser, eType, pchDisplayString) chatRelay_NotifySend(relayUser, eType, pchDisplayString, NULL, NULL, NULL, NULL, NULL)
#define crNotifySendData(relayUser, eType, pchDisplayString, pChatData) chatRelay_NotifySend(relayUser, eType, pchDisplayString, NULL, NULL, NULL, pChatData, NULL)

void chatRelay_SendPlayerInfoToAll(ChatRelayUser *relayUser, PlayerInfoStruct *pPlayer);