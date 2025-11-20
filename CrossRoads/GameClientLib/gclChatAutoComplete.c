#include "gclChatAutoComplete.h"

#include "cmdparse.h"
#include "earray.h"
#include "Entity.h"
#include "error.h"
#include "EString.h"
#include "FolderCache.h"
#include "chat/gclChatLog.h"
#include "chat/gclClientChat.h"
#include "gclCommandAlias.h"
#include "gclEntity.h"
#include "gclUIGen.h"
#include "NameList.h"
#include "Player.h"
#include "SimpleParser.h"
#include "StringUtil.h"
#include "textparser.h"
#include "UIGen.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "gclCommandParse.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

extern ParseTable parse_ChatAutoCompleteEntry[];
#define TYPE_parse_ChatAutoCompleteEntry ChatAutoCompleteEntry
extern ParseTable parse_ChatAutoCompleteEntryList[];
#define TYPE_parse_ChatAutoCompleteEntryList ChatAutoCompleteEntryList
extern ParseTable parse_ChatAutoCompleteEntryListList[];
#define TYPE_parse_ChatAutoCompleteEntryListList ChatAutoCompleteEntryListList
extern ParseTable parse_UIGenTextEntryCompletion[];
#define TYPE_parse_UIGenTextEntryCompletion UIGenTextEntryCompletion

static NameList **eaChatAutoCompleteNames = NULL;
static NameList *pAllChatAutoCompleteNames = NULL;
static char **eaCmdsToEscapePlayersWithCommas = NULL;
static char **eaPlayerArgCmds = NULL;
static char **eaNoArgCmds = NULL;

NameList *gclChatGetAllNamesNameList(void)
{
	return pAllChatAutoCompleteNames;
}

static int CACEComparator(const ChatAutoCompleteEntry **c1, const ChatAutoCompleteEntry **c2) {
	if (c1 == c2) {
		return 0;
	}

	if (!c1 || !c2) {
		return c1 ? 1 : -1;
	}

	if (*c1 == *c2) {
		return 0;
	}

	if (!*c1 || !*c2) {
		return *c1 ? 1 : -1;
	}

	if ((*c1)->iAccessLevel != (*c2)->iAccessLevel) {
		return (*c1)->iAccessLevel - (*c2)->iAccessLevel;
	}

	return stricmp((*c1)->pchCommand, (*c2)->pchCommand);
}

static bool dumpShouldUseComma(const char *pStr) {
	if (!stricmp(pStr, "t") || !stricmp(pStr, "tell") || !stricmp(pStr, "whisper")) {
		return true;
	}

	return false;
}

static ChatAutoCompleteEntryList *dumpKnownCmdList(NameList *pList, const char *pchName) {
	ChatAutoCompleteEntryList *pAutoList = StructCreate(parse_ChatAutoCompleteEntryList);
	const char *pStr;

	pAutoList->pchName = strdup(pchName);

	pList->pResetCB(pList);

	while (pStr = pList->pGetNextCB(pList, false))
	{
		Cmd *cmd = cmdListFind(&gGlobalCmdList, pStr);
		if (!cmd) {
			cmd = cmdListFind(&g_AliasList, pStr);
		}

		if (cmd &&
			!(cmd->flags & CMDF_HIDEPRINT) &&
			!NameList_HasEarlierDupe(pList, pStr))
		{
			ChatAutoCompleteEntry *pEntry = StructCreate(parse_ChatAutoCompleteEntry);
			pEntry->pchCommand = strdup(pStr);
			pEntry->iAccessLevel = cmd->access_level;
			pEntry->bUseCommaForPlayerName = dumpShouldUseComma(pStr);
			eaPush(&pAutoList->eaEntries, pEntry);

		}
	}

	eaQSort(pAutoList->eaEntries, CACEComparator);

	return pAutoList;
}

static ChatAutoCompleteEntryList *dumpKnownAccessLevelBucketList(NameList *pList, const char *pchName) {
	ChatAutoCompleteEntryList *pAutoList = StructCreate(parse_ChatAutoCompleteEntryList);
	const char *pStr;

	pAutoList->pchName = strdup(pchName);

	pList->pResetCB(pList);

	while (pStr = pList->pGetNextCB(pList, true))
	{
		ChatAutoCompleteEntry *pEntry = StructCreate(parse_ChatAutoCompleteEntry);
		pEntry->pchCommand = strdup(pStr);
		pEntry->iAccessLevel = NameList_AccessLevelBucket_GetAccessLevel(pList, pStr);
		pEntry->bUseCommaForPlayerName = dumpShouldUseComma(pStr);
		eaPush(&pAutoList->eaEntries, pEntry);
	}

	eaQSort(pAutoList->eaEntries, CACEComparator);

	return pAutoList;
}

static void dumpKnownCommandLists() {
	static const char *pchFilename = "c:/ChatAutoCompleteSrc.def";
	char *pchMessage = NULL;

	ChatAutoCompleteEntryListList ListList = { 0 };

	eaPush(&ListList.eaLists, dumpKnownCmdList(pGlobCmdListNames, "ClientCommands"));
	eaPush(&ListList.eaLists, dumpKnownAccessLevelBucketList(pAddedCmdNames, "ServerCommands"));
	eaPush(&ListList.eaLists, dumpKnownCmdList(pGlobAliasNameList, "Aliases"));
	ParserWriteTextFile(pchFilename, parse_ChatAutoCompleteEntryListList, &ListList, 0, 0);

	estrPrintf(&pchMessage, "Dumped known command list into %s", pchFilename);
	ChatLog_AddSystemMessage(pchMessage, NULL);
	estrDestroy(&pchMessage);

	StructDeInit(parse_ChatAutoCompleteEntryListList, &ListList);
}

extern void gclChat_DumpCommandListForAutoComplete() {
	dumpKnownCommandLists();
}

static NameList *gclChat_CreateNameList(SA_PARAM_NN_VALID ChatAutoCompleteEntryList *pListFromDataFile) {
	NameList *pList = CreateNameList_AccessLevelBucket(9);
	S32 i;

	NameList_AssignName(pList, pListFromDataFile->pchName);
	for (i=0; i < eaSize(&pListFromDataFile->eaEntries); i++) {
		ChatAutoCompleteEntry *pEntry = pListFromDataFile->eaEntries[i];
		NameList_AccessLevelBucket_AddName(pList, pEntry->pchCommand, pEntry->iAccessLevel);

		if (pEntry->bHasPlayerArgument) {
			if (eaFindString(&eaPlayerArgCmds, pEntry->pchCommand) < 0) {
				eaPush(&eaPlayerArgCmds, strdup(pEntry->pchCommand));
			}
		}

		if (pEntry->bUseCommaForPlayerName) {
			if (eaFindString(&eaCmdsToEscapePlayersWithCommas, pEntry->pchCommand) < 0) {
				eaPush(&eaCmdsToEscapePlayersWithCommas, strdup(pEntry->pchCommand));
			}
		}

		if (pEntry->bHasNoArguments) {
			if (eaFindString(&eaNoArgCmds, pEntry->pchCommand) < 0) {
				eaPush(&eaNoArgCmds, strdup(pEntry->pchCommand));
			}
		}
	}

	return pList;
}

static void gclChat_FreeString(void *str)
{
	free(str);
}

static void gclChat_CreateNameLists(ChatAutoCompleteEntryListList autoCompleteNameListList) {
	S32 i;

	// Free name list data
	if (pAllChatAutoCompleteNames) {
		FreeNameList(pAllChatAutoCompleteNames);
	}
	eaClear(&eaChatAutoCompleteNames);
	eaDestroyEx(&eaCmdsToEscapePlayersWithCommas, gclChat_FreeString);
	eaDestroyEx(&eaPlayerArgCmds, gclChat_FreeString);
	eaDestroyEx(&eaNoArgCmds, gclChat_FreeString);

	// Create name list data
	pAllChatAutoCompleteNames = CreateNameList_MultiList();
	for (i=0; i < eaSize(&autoCompleteNameListList.eaLists); i++) {
		NameList *pList = gclChat_CreateNameList(autoCompleteNameListList.eaLists[i]);
		NameList_MultiList_AddList(pAllChatAutoCompleteNames, pList);
		eaPush(&eaChatAutoCompleteNames, pList);
	}
}

static void gclChat_ReloadAutoCompleteNames(const char *pchPath, S32 iWhen) {
	ChatAutoCompleteEntryListList autoCompleteNameListList = { 0 };

	loadstart_printf("Loading chat auto-complete command names... ");
	ParserLoadFiles(NULL, "defs/config/ChatAutoComplete.def", "ChatAutoComplete.bin", 0, parse_ChatAutoCompleteEntryListList, &autoCompleteNameListList);
	gclChat_CreateNameLists(autoCompleteNameListList);
	loadend_printf("done.");

	StructDeInit(parse_ChatAutoCompleteEntryListList, &autoCompleteNameListList);
}

AUTO_STARTUP(Chat);
void gclChat_LoadAutoCompleteNames(void) {
	gclChat_ReloadAutoCompleteNames("", 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/ChatAutoComplete.def", gclChat_ReloadAutoCompleteNames);
}

static bool shouldEscapeWithComma(SA_PARAM_OP_STR const char *pchCommand) {
	if (pchCommand && *pchCommand) {
		return eaFindString(&eaCmdsToEscapePlayersWithCommas, pchCommand) >= 0;
	}

	return false;
}

static bool isPlayerCommand(SA_PARAM_OP_STR const char *pchCommand) {
	if (pchCommand && *pchCommand) {
		return eaFindString(&eaPlayerArgCmds, pchCommand) >= 0;
	}

	return false;
}

static void buildCommandName(char **ppchCommand, SA_PARAM_NN_STR const char *pchFullCommand) {
	const char *pchIndex = strchr(pchFullCommand, ' ');
	if (*pchFullCommand == '/') {
		pchFullCommand++;
	}

	if (pchIndex) {
		estrClear(ppchCommand);
		estrInsert(ppchCommand, 0, pchFullCommand, pchIndex-pchFullCommand);
	} else {
		estrCopy2(ppchCommand, pchFullCommand);
	}
}

static void getPrefix(const char *pchFullCommand, U32 iPosition, char **ppchPrefix, S32 *piPrefixReplaceFrom, S32 *piPrefixReplaceTo) {
	estrSetSize(ppchPrefix, 0); // Force this to be non-null. estrClear() doesn't do this.
	*piPrefixReplaceFrom = 0;
	*piPrefixReplaceTo = -1;

	if (iPosition > 0)
	{
		U32 iPrevious = UTF8CodepointOfPreviousWordDelim(pchFullCommand, iPosition, g_pchCommandWordDelimiters);
		U32 iPreviousEnd = UTF8CodepointOfWordEndDelim(pchFullCommand, iPrevious, g_pchCommandWordDelimiters);
		const char *pchPrev;
		const char *pchPrevEnd;
		S32 iPreviousEndLength;
		S32 iRawLength;

		// We don't want the entire previous word, just up to 'iPosition'.
		if (iPreviousEnd > iPosition) {
			iPreviousEnd = iPosition;
		}

		pchPrev = UTF8GetCodepoint(pchFullCommand, iPrevious);
		pchPrevEnd = UTF8GetCodepoint(pchFullCommand, iPreviousEnd);
		iPreviousEndLength = pchPrevEnd ? UTF8GetCodepointLength(pchPrevEnd) : 0;
		iRawLength = pchPrevEnd - pchPrev + iPreviousEndLength;

		if (iRawLength > 0) {
			estrConcat(ppchPrefix, pchPrev, iRawLength);
			*piPrefixReplaceFrom = iPrevious;
			*piPrefixReplaceTo = iPreviousEnd;
		}
	}
}

static U32 getCommandArgNumber(const char *pchFullCommand, S32 iPosition) {
	int iArgNum = 0;
	int iCur = 0;
	const char *pchCur = pchFullCommand;
	const char *pchStopAt = UTF8GetCodepoint(pchFullCommand, iPosition);

	pchCur = strNextWordDelim(pchFullCommand, g_pchCommandWordDelimiters);
	while (pchCur && pchCur < pchStopAt) {
		iArgNum++;
		pchCur = strNextWordDelim(pchCur, g_pchCommandWordDelimiters);
	}

	return iArgNum;
}

/************************************************************************
 This function gets a viable prefix to use for entity name matching.
 It finds the longest possible string that could be used to match for
 entity names.

 This is particularly complex because:
  - Entity names may contain spaces
  - We have enough context to know that we're in a command so we can remove the
    command name from the prefix, but we don't know enough else about the 
	command because it may not exist on the client and we therefore can't process
	the argument list.
  - We need to account for commas (which should act as name separators)
  - We need to account for double-quotes (solo, paired, and at end of paired)
  - We need to keep track of which original character positions we will eventually
    need to replace if a suggestion based on this prefix is chosen.
************************************************************************/
static void getFullEntityPrefix(const char *pchFullCommand, U32 iPosition, bool bIsCommand,
								char **ppchPrefix, bool *pbInQuote, S32 *piPrefixReplaceFrom, S32 *piPrefixReplaceTo) {
	estrSetSize(ppchPrefix, 0); // Force this to be non-null. estrClear() doesn't do this.
	*pbInQuote = false;
	*piPrefixReplaceFrom = 0;
	*piPrefixReplaceTo = -1;

	if (iPosition > 0)
	{
		U32 iPrevious = UTF8CodepointOfPreviousWordDelim(pchFullCommand, iPosition, g_pchCommandWordDelimiters);
		U32 iPreviousEnd = UTF8CodepointOfWordEndDelim(pchFullCommand, iPrevious, g_pchCommandWordDelimiters);
		U32 iStart = 0;
		const char *pchStart = pchFullCommand;
		const char *pchPrevEnd;
		const char *pchComma = strchr(pchFullCommand, ',');
		const char *pchTmp;
		S32 iPreviousEndLength;
		S32 iRawLength;
		S32 iQuoteCount;

		if (bIsCommand) {
			// Ignore the first argument
			iStart = UTF8CodepointOfNextWordDelim(pchFullCommand, 0, g_pchCommandWordDelimiters);
			pchStart = UTF8GetCodepoint(pchFullCommand, iStart);
		}

		// We don't want the entire previous word, just up to 'iPosition'.
		if (iPreviousEnd > iPosition) {
			iPreviousEnd = iPosition;
		}

		pchPrevEnd = UTF8GetCodepoint(pchFullCommand, iPreviousEnd);
		iPreviousEndLength = pchPrevEnd ? UTF8GetCodepointLength(pchPrevEnd) : 0;
		iRawLength = pchPrevEnd - pchStart + iPreviousEndLength;

		if (iRawLength > 0) {
			estrConcat(ppchPrefix, pchStart, iRawLength);
			*piPrefixReplaceFrom = iStart;
			*piPrefixReplaceTo = iPreviousEnd;
			if (bIsCommand) {
				// Account for leading '/' in original text
				(*piPrefixReplaceFrom)++;
				(*piPrefixReplaceTo)++;
			}
		}

		// If a comma exists anywhere within this string, then only match what's after the comma.
		pchTmp = strrchr(*ppchPrefix, ',');
		if (pchTmp) {
			S32 iTmpPos;
			pchTmp = removeLeadingWhiteSpaces(pchTmp+1); // Move pchTmp to the first non-whitespace after the comma
			iTmpPos = UTF8PointerToCodepointIndex(*ppchPrefix, pchTmp);
			(*piPrefixReplaceFrom) += iTmpPos;

			estrRemoveUpToLastOccurrence(ppchPrefix, ',');
			estrTrimLeadingAndTrailingWhitespace(ppchPrefix);
		}

		// If a comma exists AFTER iPosition, then consider this to be a quoted entry
		if (bIsCommand && pchComma) {
			ANALYSIS_ASSUME(pchComma != NULL);
			if (UTF8PointerToCodepointIndex(pchFullCommand, pchComma) >= (S32) iPosition) {
				*pbInQuote = true;
			}
		}

		// If a double-quote exists anywhere within the string and there is an odd-number of double-quotes,
		// then only count anything after the double-quotes as a match prefix.  If there are an even number
		// of double-quotes (2 or more), then if the last double-quote is the last character of the string,
		// then make the match prefix be the contents of the last fully quoted string.  Otherwise, make the
		// match prefix be everything after the last double-quote.
		iQuoteCount = estrCountChars(ppchPrefix, '\"');
		if (iQuoteCount > 0) {
			S32 iTmpPos;

			if ((iQuoteCount & 1) == 1) {
				// We have an odd number of quotes.
				*pbInQuote = true;

				// Replace should start at the last quote (so that
				// we include/overwrite it.
				pchTmp = strrchr(*ppchPrefix, '\"'); // Replace should start with the quote
				iTmpPos = UTF8PointerToCodepointIndex(*ppchPrefix, pchTmp);
				(*piPrefixReplaceFrom) += iTmpPos;
			} else if ((*ppchPrefix)[estrLength(ppchPrefix)-1] == '\"') {
				// We have an even number of quotes and the last character is a quote
				// so we want what's inside the quotes
				*pbInQuote = true;

				// Replace should end at the last existing quote
				estrTruncateAtLastOccurrence(ppchPrefix, '\"');

				// Replace should start at the matching quote
				pchTmp = strrchr(*ppchPrefix, '\"'); // Replace should start with the quote
				iTmpPos = UTF8PointerToCodepointIndex(*ppchPrefix, pchTmp);
				iTmpPos = UTF8PointerToCodepointIndex(*ppchPrefix, pchTmp);
				(*piPrefixReplaceFrom) += iTmpPos;
			} else {
				// Replace should start at the first non-whitespace after
				// the last quote
				pchTmp = strrchr(*ppchPrefix, '\"'); // Replace should start with the quote
				pchTmp = removeLeadingWhiteSpaces(pchTmp+1);
				iTmpPos = UTF8PointerToCodepointIndex(*ppchPrefix, pchTmp);
				(*piPrefixReplaceFrom) += iTmpPos;
			}

			estrRemoveUpToLastOccurrence(ppchPrefix, '\"');
			estrTrimLeadingAndTrailingWhitespace(ppchPrefix);
		}
	}
}

static int completionComparator(const UIGenTextEntryCompletion * const *c1, const UIGenTextEntryCompletion * const *c2) {
	if (c1 == c2) {
		return 0;
	}

	if (c1 == NULL || c2 == NULL) {
		return c1 ? 1 : -1;
	}

	if (*c1 == *c2) {
		return 0;
	}

	if (*c1 == NULL || *c2 == NULL) {
		return *c1 ? 1 : -1;
	}

	return stricmp((*c1)->pchSuggestion, (*c2)->pchSuggestion);
}

static void fillCommandArgCompletionList(NameList *pNameList, const char *pchPrefix, UIGenTextEntryCompletion ***peaCompletion, S32 iPrefixReplaceFrom, S32 iPrefixReplaceTo) {
	int iNumEntries = 0;

	if (strlen(pchPrefix) >= MIN_COMPLETE_PREFIX_LENGTH)
	{
		int i;
		static char **ppStrings = NULL;
		eaClear(&ppStrings);
		NameList_FindAllPrefixMatchingStrings(pNameList, pchPrefix, &ppStrings);
		for (i=0; i < eaSize(&ppStrings); i++)
		{
			UIGenTextEntryCompletion *pComp = eaGetStruct(peaCompletion, parse_UIGenTextEntryCompletion, i);

			estrPrintf(&pComp->pchSuggestion, "%s ", ppStrings[i]);
			estrCopy2(&pComp->pchDisplay, ppStrings[i]);
			pComp->iPrefixReplaceFrom = iPrefixReplaceFrom;
			pComp->iPrefixReplaceTo = iPrefixReplaceTo;

			iNumEntries++;
		}
	}

	eaSetSizeStruct(peaCompletion, parse_UIGenTextEntryCompletion, iNumEntries);
	eaQSort(*peaCompletion, completionComparator);
}

static void fillCommandCompletionList(NameList *pNameList, const char *pchPrefix, UIGenTextEntryCompletion ***peaCompletion, S32 iPrefixReplaceFrom, S32 iPrefixReplaceTo, bool bAnnotateAccessLevel) {
	static char *ppchAccessLevelColors[] = {
		"ffffffff", // Access Level 0
		"ffff00ff", // Access Level 1
		"ffdf00ff", // Access Level 2
		"ffbf00ff", // Access Level 3
		"ff9f00ff", // Access Level 4
		"ff7f00ff", // Access Level 5
		"ff5f00ff", // Access Level 6
		"ff3f00ff", // Access Level 7
		"ff1f00ff", // Access Level 8
		"ff0000ff", // Access Level 9
	};

	int iNumEntries = 0;
	Entity *pEntity = entActivePlayerPtr();

	if (pEntity && strlen(pchPrefix) >= MIN_COMPLETE_PREFIX_LENGTH) {
		static char **ppStrings = NULL;
		eaClear(&ppStrings);
		NameList_FindAllPrefixMatchingStrings(pNameList, pchPrefix, &ppStrings);

		if (eaSize(&ppStrings))
		{
			int i;
			for (i=0; i < eaSize(&ppStrings); i++)
			{
				Cmd *cmd = cmdListFind(&gGlobalCmdList, ppStrings[i]);
				S32 iAccessLevel;

				if (!cmd) {
					cmd = cmdListFind(&g_AliasList, ppStrings[i]);
				}
				
				iAccessLevel = cmd ? cmd->access_level : NameList_AccessLevelBucket_GetAccessLevel(pAddedCmdNames, ppStrings[i]);
				
				//special case... treat all CMDLINE commands as at least accesslevel ugc+1, so they don't show up in autocomplete for end users
				if (cmd && iAccessLevel == 0 && (cmd->flags & CMDF_COMMANDLINEONLY))
				{
					iAccessLevel = ACCESS_UGC+1;
				}

				iAccessLevel = CLAMP(iAccessLevel, 0, 9);

				if (iAccessLevel <= pEntity->pPlayer->accessLevel) {
					UIGenTextEntryCompletion *pComp = eaGetStruct(peaCompletion, parse_UIGenTextEntryCompletion, iNumEntries);

					estrPrintf(&pComp->pchSuggestion, "%s ", ppStrings[i]);

					// Construct display name of command
					if (bAnnotateAccessLevel) {
						if (iAccessLevel) {
							estrPrintf(&pComp->pchDisplay, "<font color=#%s>%s (%d)</font>", ppchAccessLevelColors[iAccessLevel], ppStrings[i], iAccessLevel);
						} else {
							estrPrintf(&pComp->pchDisplay, "<font color=#%s>%s</font>", ppchAccessLevelColors[iAccessLevel], ppStrings[i]);
						}
					} else {
						estrCopy2(&pComp->pchDisplay, ppStrings[i]);
					}
					pComp->iPrefixReplaceFrom = iPrefixReplaceFrom;
					pComp->iPrefixReplaceTo = iPrefixReplaceTo;
					pComp->bFinish = cmd ? cmd->iNumReadArgs == 0 : eaFindString(&eaNoArgCmds, ppStrings[i]) >= 0;

					iNumEntries++;
				}
			}
		}

		eaSetSizeStruct(peaCompletion, parse_UIGenTextEntryCompletion, iNumEntries);
		eaQSort(*peaCompletion, completionComparator);
	}
}

static void SetMaxAccessLevelForAutoComplete(int iMaxAccessLevel) {
	int i;

	for (i=0; i < eaSize(&eaChatAutoCompleteNames); i++) {
		NameList_AccessLevelBucket_SetAccessLevel(eaChatAutoCompleteNames[i], iMaxAccessLevel);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_AutoComplete);
void gclChat_AutoComplete(SA_PARAM_NN_VALID UIGen *pListGen, const char *pchFullCommand, S32 iPosition, S32 iMaxListSize)
{
	UIGenTextEntryCompletion ***peaCompletion = ui_GenGetManagedListSafe(pListGen, UIGenTextEntryCompletion);
	Entity *pRequester = entActivePlayerPtr();
	bool bIsCommand = pchFullCommand ? *pchFullCommand == '/' : false;
	char *pchPrefix = NULL;
	const char *pchPosition;
	S32 iPrefixReplaceFrom;
	S32 iPrefixReplaceTo;

	if (bIsCommand) {
		pchFullCommand++;
		iPosition--;
	}

	// iPosition is a cursor index and it points just after the character
	// we're interested in using as the last character of the prefix.
	// So, we need to bump it down.  It's OK if it becomes negative, because
	// that just means that the cursor was before the first character.
	iPosition--;

	// If we're positioned on a delimiter or before the beginning of
	// the line, then abort.
	pchPosition = iPosition >= 0 && pchFullCommand ? UTF8GetCodepoint(pchFullCommand, iPosition) : NULL;
	if (!pchPosition || strchr(g_pchCommandWordDelimiters, *pchPosition)) {
		eaSetSizeStruct(peaCompletion, parse_UIGenTextEntryCompletion, 0);
		ui_GenSetManagedListSafe(pListGen, peaCompletion, UIGenTextEntryCompletion, true);
		return;
	}

	getPrefix(pchFullCommand, iPosition, &pchPrefix, &iPrefixReplaceFrom, &iPrefixReplaceTo);
	if (bIsCommand) {
		// Account for initial '/'
		iPrefixReplaceFrom++;
		iPrefixReplaceTo++;
	}

	if (bIsCommand) {
		char *pchCommand = NULL;
		buildCommandName(&pchCommand, pchFullCommand);

		// If we're on the first word (command name), then find a suitable list
		// of command names to match.  Otherwise, analyze the command arguments.
		// Keep in mind that iPosition could be positioned anywhere within the
		// word contained in pchPrefix (i.e. it's not always at the end).
		if (iPosition - UTF8GetLength(pchPrefix) + 1 <= 0) {
			NameList *pNameList = pAllChatAutoCompleteNames;

			// Get list of command names for auto-complete
			SetMaxAccessLevelForAutoComplete(gclClientChat_GetAccessLevel());
			fillCommandCompletionList(pAllChatAutoCompleteNames, pchPrefix, peaCompletion, iPrefixReplaceFrom, iPrefixReplaceTo, true);
			SetMaxAccessLevelForAutoComplete(9); // Restore
		} else {
			char *pchFullCommandUpToPosition = NULL;
			NameList *pListToUse = NULL;
			enumNameListType eNameListType;

			estrConcat(&pchFullCommandUpToPosition, pchFullCommand, iPosition+1);
			eNameListType = cmdGetNameListTypeFromPartialCommandString(pchFullCommandUpToPosition);
			if (eNameListType == NAMELISTTYPE_COMMANDLIST) {
				pListToUse = pAllChatAutoCompleteNames;
			} else if (eNameListType != NAMELISTTYPE_NONE) {
				pListToUse = cmdGetNameListFromPartialCommandString(pchFullCommandUpToPosition);
			}

			// If we get the debug console auto completion list, override it with the chat version
			if (pListToUse == pAllCmdNamesForAutoComplete) {
				pListToUse = pAllChatAutoCompleteNames;
			}

			if (pListToUse == pAllChatAutoCompleteNames) {
				fillCommandCompletionList(pListToUse, pchPrefix, peaCompletion, iPrefixReplaceFrom, iPrefixReplaceTo, true);
			} else if (pListToUse) {
				fillCommandArgCompletionList(pListToUse, pchPrefix, peaCompletion, iPrefixReplaceFrom, iPrefixReplaceTo);
			} else {
				// If we don't have a well-known type list to draw from for the args
				// and we have a "player arg" command, then suggest character & account 
				// names that match.
				if (isPlayerCommand(pchCommand)) {
					static char *pchFullEntityPrefix = NULL;
					bool bAddCommaIfNeeded = shouldEscapeWithComma(pchCommand);
					bool bInQuotes;
					
					getFullEntityPrefix(pchFullCommand, iPosition, bIsCommand, &pchFullEntityPrefix, &bInQuotes, &iPrefixReplaceFrom, &iPrefixReplaceTo);

					if (bAddCommaIfNeeded) {
						// Check to make sure we're working with the first argument
						// because we shouldn't use comma's after that.  Note that there
						// is additional logic within gclTestAndAddEntitySuggestions
						// for negating this boolean.  We need both.  This one tests
						// that we're dealing with the first command argument.  The
						// deeper one checks whether the first word is being used for
						// completion and if not, removes the comma requirement.
						if (getCommandArgNumber(pchFullCommand, iPrefixReplaceFrom) != 1) {
							bAddCommaIfNeeded = false;
						}
					}

					//Note: only tells really support the command and appending the recent
					//      chat receivers is really only for use with tells (so the user can
					//      change who they want to send a message to after hitting backspace (reply),
					//      for example.  This is why we use bAddCommaIfNeeded to determine whether
					//      or not to append chat receivers.
					
					gclGenFillEntityNameSuggestions(peaCompletion, pchFullEntityPrefix, iPrefixReplaceFrom, iPrefixReplaceTo, !bInQuotes, false, true, bAddCommaIfNeeded, true, bAddCommaIfNeeded);
				}
			}

			estrDestroy(&pchFullCommandUpToPosition);
			estrDestroy(&pchCommand);
		}
	} else {
		static char *pchFullEntityPrefix = NULL;
		bool bInQuotes;

		getFullEntityPrefix(pchFullCommand, iPosition, bIsCommand, &pchFullEntityPrefix, &bInQuotes, &iPrefixReplaceFrom, &iPrefixReplaceTo);
		gclGenFillEntityNameSuggestions(peaCompletion, pchFullEntityPrefix, iPrefixReplaceFrom, iPrefixReplaceTo, true, true, bInQuotes, false, false, false);
	}

	if (iMaxListSize && eaSize(peaCompletion) > iMaxListSize) {
		eaSetSizeStruct(peaCompletion, parse_UIGenTextEntryCompletion, iMaxListSize);
	}

	ui_GenSetManagedListSafe(pListGen, peaCompletion, UIGenTextEntryCompletion, true);
	estrDestroy(&pchPrefix);
}

void VerifyCommand(const char *pCommandName, int iAccessLevel)
{
	//if it finds it locally, does all verification, otherwise sticks it in a list to send to the server
	Cmd *pCmd = cmdListFind(&gGlobalCmdList, pCommandName);
	if (pCmd)
	{
		if (pCmd->access_level != iAccessLevel)
		{
			Errorf("ChatAutoComplete.def thinks that command %s has access level %d, actual access level is %d\n",
				pCommandName, iAccessLevel, pCmd->access_level);
		}

		if (pCmd->flags & (CMDF_HIDEPRINT | CMDF_EARLYCOMMANDLINE | CMDF_COMMANDLINEONLY))
		{
			Errorf("ChatAutoComplete.def contains command %s, but it's hidden, or command line only",
				pCommandName);
		}

		return;
	}

	pCmd = cmdListFind(&gPrivateCmdList, pCommandName);
	if (pCmd)
	{
		Errorf("ChatAutoComplete.def contains command %s, but it's private",
			pCommandName);
		return;
	}

	pCmd = cmdListFind(&gEarlyCmdList, pCommandName);
	if (pCmd)
	{
		Errorf("ChatAutoComplete.def contains command %s, but it's earlyCommandLine",
			pCommandName);
		return;
	}
	
	ServerCmd_VerifyCommandForGclAutoComplete(pCommandName, iAccessLevel);
}

void gclChatAutoComplete_VerifyCommands(void)
{
	//we just load an entire extra copy of all of this stuff, so we don't screw up the stuff already in RAM, and have
	//easy access to the access levels and so forth
	ChatAutoCompleteEntryListList autoCompleteNameListList = { 0 };

	loadstart_printf("Loading chat auto-complete command names for gclChat_VerifyCommands... ");
	ParserLoadFiles(NULL, "defs/config/ChatAutoComplete.def", "ChatAutoComplete.bin", 0, parse_ChatAutoCompleteEntryListList, &autoCompleteNameListList);
	loadend_printf("Done...");

	FOR_EACH_IN_EARRAY(autoCompleteNameListList.eaLists, ChatAutoCompleteEntryList, pList)
	{
		FOR_EACH_IN_EARRAY(pList->eaEntries, ChatAutoCompleteEntry, pEntry)
		{
			const char *pAliased = gclCommandAliasGetToExecute(pEntry->pchCommand);
			char *pTempAliased = NULL;
			if (pAliased)
			{
				estrCopy2(&pTempAliased, pAliased);
				estrTruncateAtFirstOccurrence(&pTempAliased, ' ');
			}

			VerifyCommand(pAliased ? pTempAliased : pEntry->pchCommand, pEntry->iAccessLevel);
			estrDestroy(&pTempAliased);
		}
		FOR_EACH_END;	
	}
	FOR_EACH_END;

	StructDeInit(parse_ChatAutoCompleteEntryListList, &autoCompleteNameListList);
}

#include "AutoGen/gclChatAutoComplete_h_ast.c"
