#include "chatBlacklist.h"

#include "GlobalComm.h"
#include "error.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "ResourceInfo.h"
#include "StringUtil.h"
#include "timing_profiler.h"

#include "AutoGen/chatBlacklist_h_ast.h"

// Shared Common File for Chat Blacklist - Global Chat Server, Shard Chat Server, Chat Relay
static ChatBlacklist sChatBlacklist = {0};

const ChatBlacklist *blacklist_GetList(void)
{
	return &sChatBlacklist;
}

// Does not check if blacklist string is already in list
void blacklist_AddStringStruct(ChatBlacklistString *blString)
{
	if (!sChatBlacklist.eaStrings)
		eaIndexedEnable(&sChatBlacklist.eaStrings, parse_ChatBlacklistString);
	eaIndexedAdd(&sChatBlacklist.eaStrings, blString);
}

ChatBlacklistString *blacklist_RemoveString_Internal(char *string, bool bDestroy)
{
	ChatBlacklistString *blString = NULL;
	if (nullStr(string) || !sChatBlacklist.eaStrings)
		return NULL;
	blString = eaIndexedRemoveUsingString(&sChatBlacklist.eaStrings, string);
	if (blString && bDestroy)
	{
		StructDestroy(parse_ChatBlacklistString, blString);
		return NULL;
	}
	return blString;
}

void blacklist_ReplaceBlacklist(const ChatBlacklist *blacklist)
{
	StructDeInit(parse_ChatBlacklist, &sChatBlacklist);
	StructCopy(parse_ChatBlacklist, blacklist, &sChatBlacklist, 0, 0, 0);
}

bool blacklist_CheckForViolations(const char *string, const ChatBlacklistString **ppViolation)
{
	PERFINFO_AUTO_START_FUNC();
	EARRAY_CONST_FOREACH_BEGIN(sChatBlacklist.eaStrings, i, s);
	{
		if (strstri(string, sChatBlacklist.eaStrings[i]->string))
		{
			if (ppViolation)
				*ppViolation = sChatBlacklist.eaStrings[i];
			return true;
		}
	}
	EARRAY_FOREACH_END;
	if (ppViolation)
		*ppViolation = NULL;
	PERFINFO_AUTO_STOP_FUNC();
	return false;
}

ChatBlacklistString *blacklist_LookupString(const char *string)
{
	if (sChatBlacklist.eaStrings)
		return eaIndexedGetUsingString(&sChatBlacklist.eaStrings, string);
	return NULL;
}

void blacklist_HandleUpdate(const ChatBlacklist *blacklist, ChatBlacklistUpdate eUpdateType)
{
#ifndef GLOBALCHATSERVER
	switch (eUpdateType)
	{
	case CHATBLACKLIST_REPLACE:
		blacklist_ReplaceBlacklist(blacklist);
		break;
	case CHATBLACKLIST_ADD:
		EARRAY_CONST_FOREACH_BEGIN(blacklist->eaStrings, i, s);
		{
			ChatBlacklistString *blString = NULL;
			if (nullStr(blacklist->eaStrings[i]->string))
				continue;
			blString = eaIndexedGetUsingString(&sChatBlacklist.eaStrings, blacklist->eaStrings[i]->string);
			if (!blString)
				eaIndexedAdd(&sChatBlacklist.eaStrings, StructClone(parse_ChatBlacklistString, blacklist->eaStrings[i]));
		}
		EARRAY_FOREACH_END;
		break;
	case CHATBLACKLIST_REMOVE:
		EARRAY_CONST_FOREACH_BEGIN(blacklist->eaStrings, i, s);
		{
			blacklist_RemoveString_Internal(blacklist->eaStrings[i]->string, true);
		}
		EARRAY_FOREACH_END;
		break;
	}
#endif
}

// Blacklist Initialization and Persistence - Only Occurs on GCS
#ifdef GLOBALCHATSERVER
#define CHAT_BLACKLIST_FILENAME "server/ChatBlacklist.txt"
void saveBlacklistFile(void)
{
	char fileAbsoluteLoc[MAX_PATH];	
	sprintf(fileAbsoluteLoc, "%s/%s", fileLocalDataDir(), CHAT_BLACKLIST_FILENAME);
	ParserWriteTextFile(fileAbsoluteLoc, parse_ChatBlacklist, &sChatBlacklist, 0, 0);
}

static void blacklistReloadCallback(const char *relpath, int when)
{
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);
	if (!fileExists(relpath))
		; // File was deleted, do we care here?
	
	StructReset(parse_ChatBlacklist, &sChatBlacklist);
	if(!ParserReadTextFile(relpath, parse_ChatBlacklist, &sChatBlacklist, PARSER_IGNORE_ALL_UNKNOWN | PARSER_NOERROR_ONIGNORE | PARSER_OPTIONALFLAG));
}
#endif

void initChatBlacklist(void)
{
#ifdef GLOBALCHATSERVER
	char fileAbsoluteLoc[MAX_PATH];	
	sprintf(fileAbsoluteLoc, "%s/%s", fileLocalDataDir(), CHAT_BLACKLIST_FILENAME);
	blacklistReloadCallback(fileAbsoluteLoc, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, CHAT_BLACKLIST_FILENAME, blacklistReloadCallback);
#endif	
	resRegisterDictionaryForEArray("Chat Blacklist", RESCATEGORY_OTHER, 0, &sChatBlacklist.eaStrings, parse_ChatBlacklistString);
}

#include "AutoGen/chatBlacklist_h_ast.c"