#ifndef CHATGLOBAL_H
#define CHATGLOBAL_H

#include "AppLocale.h"

typedef struct Packet Packet;
typedef struct NetLink NetLink;
typedef struct NetComm NetComm;
typedef struct ChatUser ChatUser;
typedef struct ChatChannel ChatChannel;
typedef struct PlayerInfoStruct PlayerInfoStruct;
typedef struct CmdContext CmdContext;
typedef enum Language Language;

#define XMPP_CHAT_ID 1

void sendMessageToUser(SA_PARAM_NN_VALID ChatUser *user, int eChatType, const char *channel_name, const char *fmt, ...);
void sendMessageToUserStatic(SA_PARAM_NN_VALID ChatUser *user, int eChatType, const char *channel_name, const char *msg);

Language userGetLastLanguage(ChatUser *user);
void translateMessageForUser (char **estr, SA_PARAM_NN_VALID ChatUser *user, const char *messageKey, ...);
void sendTranslatedMessageToUser(SA_PARAM_NN_VALID ChatUser *user, int eChatType, const char *channel_name, const char *messageKey, ...);
void broadcastTranslatedMessageToUser(SA_PARAM_NN_VALID ChatUser *user, int eChatType, const char *channel_name, const char *messageKey, ...);

typedef struct ChatGuild ChatGuild;
ChatGuild *GlobalChatKnowsGuild (U32 uChatServerID, U32 uGuildID);
void GlobalChatRequestGuildUpdate (U32 uChatServerID, U32 uGuildID);
void GlobalChatServer_UpdateGuildName (U32 uChatServerID, U32 uGuildID, char *pchGuildName);
void GlobalChatServer_InitializeGuildNameBatch (U32 uChatServerID, ChatGuild **ppGuilds);

NetComm *getChatServerComm(void);
void getAccountListFromGlobalChatServer (U32 *puAccountIDList);
void getChannelListFromGlobalChatServer (void);
void sendCommandToGlobalChatServer(const char *pCommandString);
void sendCmdAndParamsToGlobalChat(SA_PARAM_NN_STR const char *pCommand, const char *pParamFormat, ...);

bool ChatServerIsConnectedToGlobal(void);

void initLocalChatServer(void);
void ChatServerLocalTick(void);

typedef struct CmdContext CmdContext;
int ChatServerCmdParseFunc(const char *cmdOrig, char **ppReturnString, CmdContext *pContext);

AUTO_STRUCT;
typedef struct ChatServerData
{
	ChatUser **ppUsers;
	ChatChannel **ppChannels;
} ChatServerData;

void localChatAddCommmandWaitQueue (U32 userID, const char *userHandle, U32 channelID, const char *channelName, SA_PARAM_NN_STR const char * commandString);

void blacklist_Violation(ChatUser *user, const char *string);

#endif