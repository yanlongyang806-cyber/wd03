#ifndef CHATGLOBAL_H
#define CHATGLOBAL_H

#include "AppLocale.h"
#include "StashTable.h"

typedef struct Packet Packet;
typedef struct NetLink NetLink;
typedef struct NetComm NetComm;
typedef struct ChatUser ChatUser;
typedef struct ChatChannel ChatChannel;
typedef struct ChatTranslation ChatTranslation;
typedef struct PlayerInfoStruct PlayerInfoStruct;
typedef struct CmdContext CmdContext;

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "uID, ShardName, ShardCategoryName, ProductName");
typedef struct GlobalChatLinkStruct
{
	U32 uID;
	NetLink *localChatLink;				NO_AST
	U32 *ppOnlineUserIDs;

	const char *pShardName;					AST(POOL_STRING)
	const char *pShardCategoryName;			AST(POOL_STRING)
	const char *pProductName;				AST(POOL_STRING)
	const char *pClusterName;				AST(POOL_STRING)

	StashTable shardGuildStash;			NO_AST			// key = guild id, value = ChatGuild * (owned)
	StashTable shardGuildNameStash;		NO_AST			// key = guild name, value = guild id
	StashTable stUserGuilds;			NO_AST			// key = account ID, value = GlobalChatLinkUserGuilds * (owned)
	U32 uConnectedTime;
	
	// Keys to Persisted Containers
	U32 uClusterID;
	U32 uShardID;
} GlobalChatLinkStruct;
GlobalChatLinkStruct *GlobalChatGetShardData(U32 uLinkID);
void GlobalChatGetShardListIterator(StashTableIterator *iter);

void setGlobalChatServer(const char *pChatServer);
const char *getGlobalChatServer(void);

void sendAccountList (NetLink *link, U32 uChatServerID, U32 *piAccountIDList);
void sendChannelList (NetLink *link);

void sendCommandToAllLocal(const char *commandString, bool bSendToXmpp);
void sendCommandToAllLocalMinusSender(NetLink *sender, const char *commandString);
void sendCommandToUserLocal(ChatUser *user, const char *commandString, NetLink *originator);
void sendCommandToUserLocalEx(ChatUser *user, NetLink *originator, const char *pCommand, const char *pParamFormat, ...);
void sendCommandToLink(NetLink *link, const char *commandString);
void sendCommandToLinkEx(NetLink *link, const char *pCommand, const char *pParamFormat, ...);
void sendMessageToUser(ChatUser *user, int eChatType, const char *channel_name, const char *fmt, ...);
void sendMessageToUserStatic(ChatUser *user, int eChatType, const char *channel_name, const char *msg);
void sendMessageToLink(NetLink *link, ChatUser *user, int eChatType, const char *channel_name, const char *msg);
void sendAlertToUser(ChatUser *user, const char *title, const char *msg);
void sendAlertToLink(NetLink *link, ChatUser *user, const char *title, const char *msg);

PlayerInfoStruct *userFindPlayerInfoByLinkID(ChatUser *user, U32 uChatLinkID);
Language userGetLastLanguage(ChatUser *user);
ChatTranslation * constructTranslationMessage(const char *messageKey, ...);
ChatTranslation * constructTranslationMessageVa(const char *messageKey, va_list ap);
void sendTranslatedMessageToUser(ChatUser *user, int eChatType, const char *channel_name, const char *messageKey, ...);
void broadcastTranslatedMessageToUser(ChatUser *user, int eChatType, const char *channel_name, const char *messageKey, ...);

void sendChannelUpdate(ChatChannel *channel);
void sendChannelDeletion(U32 uID);
void sendUserUpdate(ChatUser *user);
void GlobalChatLoginUserByLinkID(ChatUser *user, U32 uLinkID);
void GlobalChatLogoutUserByLinkID(ChatUser *user, U32 uLinkID);

void initGlobalChatServer(void);
NetComm *getChatServerComm(void);
void ChatServerGlobalTick(F32 fElapsed);

// Force account update.
int chatServer_ForceAccountUpdate(U32 uAccountID);

const char *GetShardNameById(U32 uLinkId);
U32 GetShardHashById(U32 uLinkId);
const char *GetLocalChatLinkShardName(NetLink *link);
U32 GetLocalChatLinkID(NetLink *link);
NetLink * GetLocalChatLink(U32 id);
U32 GetLocalChatLinkIdByShardName(const char *pchShardName);
void GetShardIds(U32 **eaShardIds);
void forceSetCommandContext(CmdContext *context);
void unsetCommandContext(void);

typedef struct CmdContext CmdContext;
int ChatServerCmdParseFunc(const char *cmdOrig, char **ppReturnString, CmdContext *pContext);
const char *chatServerGetCommandString(void);
NetLink *chatServerGetCommandLink(void);

#define chatServerGetCommandLinkID() GetLocalChatLinkID(chatServerGetCommandLink())

AUTO_STRUCT;
typedef struct ChatServerData
{
	ChatUser **ppUsers;
	ChatChannel **ppChannels;
} ChatServerData;

void localChatAddCommmandWaitQueue (U32 userID, const char *userHandle, U32 channelID, const char *channelName, SA_PARAM_NN_STR const char * commandString);

typedef enum enumTransactionOutcome enumTransactionOutcome;
typedef void (*GlobalCommandTimeoutCB)(enumTransactionOutcome eResult, void *userData);

void GlobalCommandWaitTick(void);
U32 globalChatAddCommandWaitQueueByLink (NetLink *link, const char *chatHandle, const char * commandString);
U32 globalChatAddCommandWaitQueueByLinkEx (NetLink *link, const char *chatHandle, const char * commandString, GlobalCommandTimeoutCB cb, void *data);

typedef struct TransactionReturnVal TransactionReturnVal;
void trDefaultUserUpdate_CB(TransactionReturnVal *returnVal, U32 *userID);
void trDefaultChannelUpdate_CB(TransactionReturnVal *returnVal, U32*channelID);

const char *GCS_GetProductDisplayName(const char *internalProductName);
// Returns the shard name back if no mapping is found
// Only returns NULL if a valid mapping with a NULL or empty string display name
const char *GCS_GetShardDisplayName(const char *internalProductName, const char *shardName);


#endif