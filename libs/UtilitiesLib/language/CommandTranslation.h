#pragma once
GCC_SYSTEM

AUTO_STRUCT;
typedef struct CommandNameList
{
	const char **eaNames;
} CommandNameList;

typedef struct Cmd Cmd;
typedef struct CmdList CmdList;

// If this command has an alternate name in the given language, use it.
// Otherwise, return the default name.
const char *CmdTranslatedName(const char *pchName, Language eLanguage);

// If this command has an alternate help string in the given language, use it.
// Otherwise, return the default help string.
const char *CmdTranslatedHelp(Cmd *pCommand, Language eLanguage);

// Generate a list of translated command names for the given language.
// Once this function is called on a CmdList, it cannot be freed later,
// since the translations in it will be reprocessed during data reload.
void CmdListGenerateMapForLanguage(CmdList *pList, Language eLanguage);

void CmdParseDumpMessages(const char *pchPrefix, CommandNameList *pExcept, CommandNameList *pAdded, const char *pchRoot);

void CommandTranslationDictEventCallback(enumResourceEventType eType, const char *pDictName, const char *pResourceName, void *pResource, void *pUserData);

