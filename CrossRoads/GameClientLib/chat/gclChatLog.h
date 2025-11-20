/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#pragma once
GCC_SYSTEM

typedef struct ChatData ChatData;
typedef struct ChatLink ChatLink;
typedef struct ChatLinkInfo ChatLinkInfo;
typedef struct ChatLogFilter ChatLogFilter;
typedef struct ChatMessage ChatMessage;
typedef struct ChatUserInfo ChatUserInfo;
typedef struct Item Item;
typedef enum ChatLogEntryType ChatLogEntryType;

#define MAX_COMMAND_HISTORY 20

#if PLATFORM_CONSOLE
	#define MAX_CHAT_LOG_ENTRIES 100
#else
	#define MAX_CHAT_LOG_ENTRIES 10000
#endif

AUTO_ENUM;
typedef enum ChatLogSpanType {
	kChatLogSpanType_Default,
	kChatLogSpanType_To,
	kChatLogSpanType_From,
	kChatLogSpanType_Time,
	kChatLogSpanType_Spy,
	kChatLogSpanType_Channel,
	kChatLogSpanType_Message,
} ChatLogSpanType;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct CommandHistoryEntry
{
	char *command;
	bool nonTemporary;
	ChatData *pData;
} CommandHistoryEntry;

AUTO_STRUCT;
typedef struct ChatLogFormatSpan {
	ChatLink *pLink; AST(UNOWNED)
	S32 iStart;
	S16 iLength;
	ChatLogSpanType eType;
	Item *pItem;
} ChatLogFormatSpan;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct ChatLogEntry
{
	S32 id;
	const ChatMessage *pMsg;
	U32 iTimestamp;

	// The following fields are used for rendering
	char *pchFormattedText; AST(FORMATSTRING(XML_ENCODE_BASE64=1) ESTRING)
	ChatLogFormatSpan **eaFormatSpans;
	ChatLink *pFromLink;
	ChatLink *pToLink;
} ChatLogEntry;
extern ParseTable parse_ChatLogEntry[];
#define TYPE_parse_ChatLogEntry ChatLogEntry

AUTO_STRUCT;
typedef struct PrivateChatWindow
{
	// The handle of the player who is chatting with the active player
	char *pchHandle;		AST(KEY ESTRING)

	// The character name for the player
	char *pchName;			AST(ESTRING)

	// Window creation time
	U32 iTimeCreated;

	// Player account ID
	U32 accountID;

	// Online character entity container ID
	U32 onlineCharacterID;

	// If the player is in the login server, this indicates the login server ID
	U32 uLoginServerID;

	// Character class
	const char *pchClassName;			AST(NAME(ClassName) POOL_STRING)

	// Character path
	const char *pchPathName;			AST(NAME(PathName) POOL_STRING)

	// Message key indicating the character's location
	char *pchLocationMessageKey;	AST(ESTRING)

	// Player level
	S32	iPlayerLevel;

	// Player's team ID
	S32 iPlayerTeam;

	// Indicates whether the window has an unread message
	U32 bHasUnreadMessage : 1;

	// Indicates whether the window is minimized
	U32 bMinimized : 1;

	// Indicates whether the player is online
	U32 bOnline : 1;

	// This window is marked for deletion in the next frame
	U32 bDeleteOnNextFrame : 1;

} PrivateChatWindow;

AUTO_STRUCT;
typedef struct ChatTabState
{
	// The name of the tab
	char *pchTabName; AST(KEY)

	// The timestamp of the last message received in this tab
	U32 uLastTimestamp;

	// The ID of the last message received in this tab
	U32 uLastID;

	// A bit that can be used to indicate that there are unseen messages in this tab
	bool bNew : 1;

	// A bit that's used to help locate deleted tabs
	bool bUpdated : 1;
} ChatTabState;

extern ChatLogEntry **g_ChatLog;
extern S32 g_ChatLogVersion;

void ChatLog_Initialize(void);
void ChatLog_FilterMessages(ChatLogEntry ***eaChatMessages, ChatLogFilter *pFilter);

// ChatLog
void ChatBubble_Say(EntityRef ref, const char *string, F32 duration, const char* pBubbleStyle);
void ChatLog_AddMessage(SA_PARAM_NN_VALID ChatMessage *pMsg, bool bSendToTestClient);
void ChatLog_AddChatMessage(ChatLogEntryType eType, SA_PARAM_OP_VALID const char *pchText, SA_PARAM_OP_VALID const ChatData *pData);
void ChatLog_AddEntityMessage(ChatUserInfo *pFrom, ChatLogEntryType eType, SA_PARAM_OP_VALID const char *pchText, SA_PARAM_OP_VALID const ChatData *pData);
void ChatLog_AddSystemMessage(SA_PARAM_OP_VALID const char *pchText, SA_PARAM_OP_VALID const char *pchFromTag);
void ChatLog_AddSystemMessageWithData(SA_PARAM_OP_VALID const char *pchText, SA_PARAM_OP_VALID const char *pchFromTag, SA_PARAM_OP_VALID const ChatData *pChatData);
void ChatLog_Clear(void);
void ChatLog_Echo(const char *pchFilename, const char *pchText);
void ChatLog_GetLastLines(S32 iLineCount, char ***pppchLines); // Takes the number of lines you want, and an earray.  Strings are allocated using StructAllocString().
void ChatLog_OncePerFrame(void);
ChatUserInfo ***ChatLog_GetRecentChatSenders(void);
ChatUserInfo ***ChatLog_GetRecentChatReceivers(void);
ChatUserInfo ***ChatLog_GetRecentTellCorrespondents(void);

void CommandHistory_AddNewMessage(const char *msg, SA_PARAM_OP_VALID ChatData *pData);
const char * ClientChat_LastTellSender(void);
const char * ClientChat_LastTellCorrespondent(void);

// Inspecting the ChatLog states for a given ChatTabConfig.
bool ChatLog_HasReceivedNewMessage(const char *pchTabName);
S32 ChatLog_GetLastMessageTime(const char *pchTabName);
void ChatLog_ClearReceivedNewMessage(const char *pchTabName);
