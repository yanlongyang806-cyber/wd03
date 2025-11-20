#pragma once

AUTO_ENUM;
typedef enum ChatBlacklistUpdate
{
	CHATBLACKLIST_REPLACE = 0,
	CHATBLACKLIST_ADD,
	CHATBLACKLIST_REMOVE,
} ChatBlacklistUpdate;

AUTO_STRUCT;
typedef struct ChatBlacklistString
{
	char *string; AST(KEY)
	char *reason;
	U32 uTimeAdded;
} ChatBlacklistString;

AUTO_STRUCT;
typedef struct ChatBlacklist
{
	EARRAY_OF(ChatBlacklistString) eaStrings;
} ChatBlacklist;

const ChatBlacklist *blacklist_GetList(void);
void blacklist_AddStringStruct(ChatBlacklistString *blString);
ChatBlacklistString *blacklist_RemoveString_Internal(char *string, bool bDestroy);
void blacklist_ReplaceBlacklist(const ChatBlacklist *blacklist);
ChatBlacklistString *blacklist_LookupString(const char *string);
void blacklist_HandleUpdate(const ChatBlacklist *blacklist, ChatBlacklistUpdate eUpdateType);

// Check the string for possible blacklist violations, returns true if a violation was found
// If ppViolation is not NULL, it also sets the pointer to the blacklist rule violated (or NULL the string is clean)
bool blacklist_CheckForViolations(const char *string, const ChatBlacklistString **ppViolation);

#ifdef GLOBALCHATSERVER
void saveBlacklistFile(void);
#endif
void initChatBlacklist(void);