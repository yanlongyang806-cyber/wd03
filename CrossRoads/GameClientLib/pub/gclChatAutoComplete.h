#ifndef _GCL_CHAT_AUTOCOMPLETE_H_
#define _GCL_CHAT_AUTOCOMPLETE_H_
#pragma once
GCC_SYSTEM

typedef struct UIGen UIGen;
typedef struct NameList NameList;

AUTO_STRUCT;
typedef struct ChatAutoCompleteEntry {
	char *pchCommand; AST(STRUCTPARAM REQUIRED)
	U32 iAccessLevel; AST(STRUCTPARAM DEFAULT(0))
	bool bHasPlayerArgument : 1; AST(STRUCTPARAM DEFAULT(0))
	bool bUseCommaForPlayerName : 1; AST(STRUCTPARAM DEFAULT(0))
	bool bHasNoArguments : 1; AST(STRUCTPARAM DEFAULT(0))
} ChatAutoCompleteEntry;

AUTO_STRUCT;
typedef struct ChatAutoCompleteEntryList {
	char *pchName; AST(STRUCTPARAM REQUIRED)
	ChatAutoCompleteEntry **eaEntries; AST(NAME(Command) REQUIRED)
} ChatAutoCompleteEntryList;

AUTO_STRUCT;
typedef struct ChatAutoCompleteEntryListList {
	ChatAutoCompleteEntryList **eaLists; AST(NAME(AutoCompleteList) REQUIRED)
} ChatAutoCompleteEntryListList;

extern void gclChat_AutoComplete(SA_PARAM_NN_VALID UIGen *pListGen, const char *pchFullCommand, S32 iPosition, S32 iMaxListSize);
extern void gclChat_DumpCommandListForAutoComplete();

//run in dev mode the first time a client connects to a server, generates errorfs for any commands in the
//auto-complete list that are unknown, that don't have the right access level, that are hidden, or that are private
void gclChatAutoComplete_VerifyCommands(void);

//should generally only be used for debugging/reporting
NameList *gclChatGetAllNamesNameList(void);

#endif _GCL_CHAT_AUTOCOMPLETE_H_