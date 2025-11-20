#include <string.h>
#include <stdlib.h>
#include "crypt.h"
#include "fileutil.h"
#include "foldercache.h"
#include "HttpXpathSupport.h"
#include "MemoryPool.h"
#include "Message.h"
#include "ResourceManager.h"
#include "StringFormat.h"
#include "StringCache.h"
#include "textparserinheritance.h"
#include "tokenstore.h"
#include "utilitieslib.h"
#include "CommandTranslation.h"
#include "cmdparse.h"
#include "StringUtil.h"
#include "winutil.h"
#include "qsortG.h"
#include "continuousBuilderSupport.h"
#include "structInternals.h"

#include "AutoGen/AppLocale_h_ast.h"
#include "AutoGen/Message_h_ast.h"
#include "Autogen/Message_c_ast.h"

#define PO_CATEGORIES_FILENAME "server/config/POExportCategories.txt"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


char *msgExportPOFiles(int iLocale);

// ----------------------------------------------------------------------------
// Static data
// ----------------------------------------------------------------------------

DictionaryHandle gMessageDict;
TranslatedMessageDictionary **gTranslationDicts;
bool *bLoadedTranslations;

static bool s_bLoading = false;
static int s_makeBinsLoc = 0;

static int siLocaleCurrentlyLoadingTranslations = -1;

//s_pcClientTranslationFolders is where the Client .translation files might be
const static char *s_pcClientTranslationFolders = "ui;messages;defs/costumes/definitions;defs/species;defs/classes;defs/config;genesis/UGCInfo_SeriesEditor";

//s_pcAllMessageFolders is all the folders where .ms files might be. This is used by the server, and should always include
//everything that's in s_pcClientMessageFolders.
const static char *s_pcAllMessageFolders = "ui;messages;defs;ai;maps;object_library;genesis;gateway";

//s_pcClientMessageFolders is where the .ms files that go in the ClientMessage<Language>.bin come from.
//If you add anything to this list, consider adding it to the "Config_<Language>.txt" that is used to convert from *.po to *.translation, via POToTransateFiles.
//If you add anything to this list, add it down below in the FolderCacheSetCallback() section.
//NOTE: We assume that the client messages a subset of the server messages. If you add anything here that's not loaded by the server, we'll have to 
//actually process the client .po files. If this stays a subset, we can save a bunch of translation effort by only translating the Server .po files.
const static char *s_pcClientMessageFolders = "ui;messages;maps;defs/costumes/definitions;defs/species;defs/classes;defs/config;genesis/UGCInfo_SeriesEditor";

char *gpRestrictPoScope = NULL;
AUTO_CMD_ESTRING(gpRestrictPoScope, RestrictPoScope) ACMD_HIDE ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

//if true, then when loading translations, check if the default string has changed (ie, the english 
//string has changed since the strings were translated)
static bool sbCheckForChangedDefaultStringsForTranslations = false;
AUTO_CMD_INT(sbCheckForChangedDefaultStringsForTranslations, CheckForChangedDefaultStringsForTranslations);

// A memory pool for messages
MP_DEFINE(Message);

bool quickLoadMessages;
// Disables loading of messages for faster startup in development
AUTO_CMD_INT(quickLoadMessages, quickLoadMessages) ACMD_CMDLINE ACMD_CALLBACK(quickLoadMessagesCallback);

void quickLoadMessagesCallback(CMDARGS)
{
	if (quickLoadMessages && getCurrentLocale() != DEFAULT_LOCALE_ID)
	{
		Errorf("-quickLoadMessages may not be used with a non-default locale.");
		quickLoadMessages = false;
	}
}

static bool s_bAddFakeTranslations = false;
AUTO_CMD_INT(s_bAddFakeTranslations, AddFakeTranslations) ACMD_CMDLINE;

static bool s_bForceLoadAllTranslations = false;
AUTO_CMD_INT(s_bForceLoadAllTranslations, ForceLoadAllTranslations) ACMD_CMDLINE;

static bool s_bDisableTranslation = false;
AUTO_CMD_INT(s_bDisableTranslation, DisableTranslation) ACMD_CMDLINE;

static bool s_bShowKeyIfNoTranslation = false;
AUTO_CMD_INT(s_bShowKeyIfNoTranslation, ShowKeyIfNoTranslation) ACMD_CMDLINE;

static bool s_bVerifyAllLoadedMessagesAreTranslated = false;
AUTO_CMD_INT(s_bVerifyAllLoadedMessagesAreTranslated, VerifyAllLoadedMessagesAreTranslated) ACMD_CMDLINE;

bool forceLoadAllTranslations(void)
{
	return IsGatewayServer() || IsGatewayLoginLauncher() || s_bForceLoadAllTranslations;
}

// This is the set of characters that will be used to encode terse message keys.
// The key may only consist of characters valid for resource names (excluding space).
// Keys are also case-insensitive, so we can only use the alphabet once.
// Specifically don't include symbols in the key characters.
static const unsigned char s_baseEncodeChars[] = "0123456789abcdefghijklmnopqrstuvwxyz";

static void langLoadTranslations(LocaleID locale);

//during .pot file writeout (only), we need to know what the old default strings were if they have changed.
static void SetDefaultStringFromTranslationHasChanged(int iLocale, const char *pMessageKey, const char *pOldDefaultString);
static char *GetOldDefaultString(int iLocale, const char *pMessageKey);

// ----------------------------------------------------------------------------
// Type definitions
// ----------------------------------------------------------------------------

typedef struct MsgKeyInfo
{
	char *pcMessageKey;
	const char *pcFilename; // allocAddFilename pooled
} MsgKeyInfo;

typedef struct LangMessageOpenData 
{
	const char **keys;

	void (*pCB)(void *);
	void *pData;
} LangMessageOpenData;

#define RECURSE_MODE_MAIN    1
#define RECURSE_MODE_ORIG    2
#define RECURSE_MODE_DELETE  3


// ----------------------------------------------------------------------------
// Formating and translation functions
// ----------------------------------------------------------------------------

TranslatedMessage *langFindTranslatedMessage(Language langID, Message *pMessage)
{
	TranslatedMessage *pTrans = NULL;
	int loc;

	if (!pMessage) {
		return NULL;
	}

	loc = locGetIDByLanguage(langID);
	if (loc && !IsClient() && !gTranslationDicts[loc]) {
		langLoadTranslations(loc);
		if (!IsLoginServer() && !IsAuctionServer() && !IsUGCSearchManager()) {
			PERFINFO_AUTO_START("GenerateMapForLanguage", 1);
			CmdListGenerateMapForLanguage(&gGlobalCmdList, locGetLanguage(loc));
			PERFINFO_AUTO_STOP();
		}
		assert(gTranslationDicts[loc]);
	}
	if (gTranslationDicts[loc] && stashFindPointer(gTranslationDicts[loc]->sLookupTable, pMessage->pcMessageKey, &pTrans))
		return pTrans;		

	return NULL;
}



AUTO_FIXUPFUNC;
TextParserResult fixupTranslatedMessage(TranslatedMessage *pTranslatedMessage, enumTextParserFixupType eType, void *pExtraData)
{
	int success = true;

	switch (eType)
	{
	case FIXUPTYPE_HERE_IS_IGNORED_FIELD:
		{
			SpecialIgnoredField *pField = pExtraData;
			Message *pMessage = RefSystem_ReferentFromString(gMessageDict, pTranslatedMessage->pcMessageKey);
			if (pMessage)
			{
				if (stricmp_safe(pMessage->pcDefaultString, pField->pString) != 0)
				{
					SetDefaultStringFromTranslationHasChanged(siLocaleCurrentlyLoadingTranslations,
						pTranslatedMessage->pcMessageKey, pField->pString);
				}
			}

		}
		break;
	}
	return success;
}


AUTO_FIXUPFUNC;
TextParserResult fixupTranslatedMessageDict(TranslatedMessageDictionary* pDict, enumTextParserFixupType eType, void *pExtraData)
{
	int success = true;

	switch (eType)
	{
	case FIXUPTYPE_POST_BINNING_DURING_LOADFILES:
	case FIXUPTYPE_POST_RELOAD:
		{
			int i;
			if (pDict->sLookupTable)
			{
				// Clear out old table if it exists
				stashTableDestroy(pDict->sLookupTable);
				pDict->sLookupTable = NULL;
			}
			pDict->sLookupTable = stashTableCreateAddress(128); // Maps from guaranteed-shared strings to other pointers

			for (i = 0; i < eaSize(&pDict->ppTranslatedMessages); i++)
			{
				TranslatedMessage *pTranslated = pDict->ppTranslatedMessages[i];

				stashAddPointer(pDict->sLookupTable, pTranslated->pcMessageKey, pTranslated, false);
			}
		}
		break;
	case FIXUPTYPE_POST_LOAD_DURING_LOADFILES_FINAL_LOCATION:
		{
			int i;
			// Fixup pointers
			if (pDict->sLookupTable)
			{
				for (i = 0; i < eaSize(&pDict->ppTranslatedMessages); i++)
				{
					TranslatedMessage *pTranslated = pDict->ppTranslatedMessages[i];

					stashAddPointer(pDict->sLookupTable, pTranslated->pcMessageKey, pTranslated, true);
				}
			}

		}
	}

	return success;
}

const char *langTranslateMessageDefault(Language langID, Message *pMessage, const char *pcDefault)
{
	const char *pcReturn = NULL;
	if (pMessage) {
		if (s_bDisableTranslation) {
			// This is used to force use of message keys on display
			pcReturn = pMessage->pcMessageKey;
		} else if (pMessage->bLocallyTranslated) {
			// This is used on GameClient, where messages are pre-translated.

 			if (pMessage->bFailedLocalTranslation && s_bShowKeyIfNoTranslation) {
				// No translation and prefer key over failure
				pcReturn = pMessage->pcMessageKey;
				//Errorf("Missing translation lang=\"%s\": Key=\"%s\", DefaultString=\"%s\"", locGetName(locGetIDByLanguage(langID)), pMessage->pcMessageKey,  pMessage->pcDefaultString);
			} else {
				// Allow for how text parser stores an empty string as NULL
				pcReturn = pMessage->pcDefaultString ? pMessage->pcDefaultString : "";
			}
		} else {
			// Get the translation (if available)
			TranslatedMessage *pTranslated = langFindTranslatedMessage(langID, pMessage);
			if (pTranslated) {
				// Found a translation, now return it

				// Allow for how text parser stores an empty string as NULL
				pcReturn = pTranslated->pcTranslatedString ? pTranslated->pcTranslatedString : "";
			} else if (s_bShowKeyIfNoTranslation) {
				// No translation and prefer key over failure
				pcReturn = pMessage->pcMessageKey;
				//Errorf("Missing translation lang=\"%s\": Key=\"%s\", DefaultString=\"%s\"", locGetName(locGetIDByLanguage(langID)), pMessage->pcMessageKey,  pMessage->pcDefaultString);
			} else {
				// No translation.  Check for possible alternate language
				int altLang = locGetAlternateLanguageFromLang(langID);
				if (altLang == LANGUAGE_DEFAULT) {
					// Use the default string

					// Allow for how text parser stores an empty string as NULL
					pcReturn = pMessage->pcDefaultString ? pMessage->pcDefaultString : "";
				} else if (altLang == LANGUAGE_NONE) {
					// Do not use a default string.  Force it to empty string.
					pcReturn = "";
				} else {
					// Attempt alternate language
					pTranslated = langFindTranslatedMessage(langID, pMessage);
					if (pTranslated) {
						// Found alternate translation, now return it.

						// Allow for how text parser stores an empty string as NULL
						pcReturn = pTranslated->pcTranslatedString ? pTranslated->pcTranslatedString : "";
					} else {
						// No translation found
						pcReturn = NULL;
					}
				}
			}
		}
	}
	return pcReturn ? pcReturn : pcDefault;
}


const char *langTranslateMessage(Language langID, Message *pMessage)
{
	return langTranslateMessageDefault(langID, pMessage, NULL);
}


const char *langTranslateMessageKeyDefault(Language langID, const char *pKey, const char *pcDefault)
{
	Message *pMessage = RefSystem_ReferentFromString(gMessageDict, pKey);
	const char *pcReturn = langTranslateMessageDefault(langID, pMessage, pcDefault);
	return pcReturn ? pcReturn : pcDefault;
}


const char *langTranslateMessageKey(Language langID, const char *pKey)
{
	return langTranslateMessageKeyDefault(langID, pKey, NULL);
}


const char *langTranslateStaticDefineIntNameKey(Language langID, const char* pchName, const char* pchKey)
{
	char achMessageKey[2048];
	sprintf(achMessageKey, "StaticDefine_%s_%s", pchName, pchKey);
	return langTranslateMessage(langID, RefSystem_ReferentFromString(gMessageDict, achMessageKey));
}


const char *langTranslateStaticDefineIntKey(Language langID, StaticDefineInt *pDefine, const char* pchKey)
{
	if (pDefine)
	{
		return langTranslateStaticDefineIntNameKey(langID, FindStaticDefineName(pDefine), pchKey);
	}
	return "Invalid StaticDefine/Key";
}


const char *langTranslateStaticDefineIntName(Language langID, const char* pchName, S32 iValue)
{
	StaticDefineInt *pDefine = FindNamedStaticDefine(pchName);
		if (pDefine)
	{
		const char *pchKey = StaticDefineIntRevLookup(pDefine, iValue);
		return langTranslateStaticDefineIntNameKey(langID, pchName, pchKey);
	}
	return "Invalid StaticDefine/Key";
}


const char *langTranslateStaticDefineInt(Language langID, StaticDefineInt *pDefine, S32 iValue)
{
	if (pDefine)
	{
		const char *pchKey = StaticDefineIntRevLookup(pDefine, iValue);
		return langTranslateStaticDefineIntNameKey(langID, FindStaticDefineName(pDefine), pchKey);
	}
	return "Invalid StaticDefine/Key";
}


void langFormatMessage(Language langID, char **eString, Message *pMessage, ...)
{
	va_list va;
	const char *pcString = langTranslateMessageDefault(langID, pMessage, NULL);
	if (pcString) {
		va_start(va, pMessage);
		strfmt_FromListEx(eString, pcString, true, langID, va);
		va_end(va);
	}
}


void langFormatMessageKey(Language langID, char **eString, const char *pKey, ...)
{
	Message *pMessage = RefSystem_ReferentFromString(gMessageDict, pKey);
	va_list va;
	const char *pcString = langTranslateMessageDefault(langID, pMessage, NULL);
	if (pcString) {
		va_start(va, pKey);
		strfmt_FromListEx(eString, pcString, true, langID, va);
		va_end(va);
	} else if (locGetAlternateLanguageFromLang(langID) != LANGUAGE_NONE) {
		if (!quickLoadMessages)
			Errorf("Invalid message key %s.  Putting '[UNTRANSLATED]%s' in the text as placeholder.", pKey, pKey);
		estrConcatf(eString, "[UNTRANSLATED]%s", pKey);
	}
}


void langFormatMessageKeyV(Language langID, char **eString, const char *pKey, va_list va)
{
	Message *pMessage = RefSystem_ReferentFromString(gMessageDict, pKey);
	const char *pcString = langTranslateMessageDefault(langID, pMessage, NULL);
	if (pcString) {
		strfmt_FromListEx(eString, pcString, true, langID, va);
	} else if (locGetAlternateLanguageFromLang(langID) != LANGUAGE_NONE) {
		if (!quickLoadMessages)
			Errorf("Invalid message key %s.  Putting '[UNTRANSLATED]%s' in the text as placeholder.", pKey, pKey);
		estrConcatf(eString, "[UNTRANSLATED]%s", pKey);
	}
}


void langFormatMessageKeyDefaultV(Language langID, char **eString, const char *pKey, const char *pDefault, va_list va)
{
	Message *pMessage = RefSystem_ReferentFromString(gMessageDict, pKey);
	const char *pcString = langTranslateMessageDefault(langID, pMessage, pDefault);
	if (pcString) {
		strfmt_FromListEx(eString, pcString, true, langID, va);
	} else if (locGetAlternateLanguageFromLang(langID) != LANGUAGE_NONE) {
		if (!quickLoadMessages)
			Errorf("Invalid message key %s.  Putting '[UNTRANSLATED]%s' in the text as placeholder.", pKey, pKey);
		estrConcatf(eString, "[UNTRANSLATED]%s", pKey);
	}
}


void langFormatMessageKeyDefault(Language langID, char **eString, const char *pKey, const char *pcDefault, ...)
{
	Message *pMessage = RefSystem_ReferentFromString(gMessageDict, pKey);
	va_list va;
	const char *pcString = langTranslateMessageDefault(langID, pMessage, pcDefault);
	if (pcString) {
		va_start(va, pcDefault);
		strfmt_FromListEx(eString, pcString, true, langID, va);
		va_end(va);
	} else if (locGetAlternateLanguageFromLang(langID) != LANGUAGE_NONE) {
		if (!quickLoadMessages)
			Errorf("Invalid message key %s.  Putting '[UNTRANSLATED]%s' in the text as placeholder.", pKey, pKey);
		estrConcatf(eString, "[UNTRANSLATED]%s", pKey);
	}
}


void langFormatMessageStructDefault(Language langID, char **eString, MessageStruct *pFmt, const char *pcDefault)
{
	Message *pMessage = RefSystem_ReferentFromString(gMessageDict, pFmt->pchKey);
	const char *pcString = langTranslateMessageDefault(langID, pMessage, pcDefault);
	if (pcString) {
		strfmt_FromStructEx(eString, pcString, pFmt->ppParameters, true, langID);
	} else if (locGetAlternateLanguageFromLang(langID) != LANGUAGE_NONE) {
		if (!quickLoadMessages)
			Errorf("Invalid message key %s.  Putting '[UNTRANSLATED]%s' in the text as placeholder.", pFmt->pchKey, pFmt->pchKey);
		estrConcatf(eString, "[UNTRANSLATED]%s", pFmt->pchKey);
	}	
}


bool langFieldToSimpleEString_dbg(Language langID, ParseTable tpi[], int column, const void* structptr, int index, char **ppEString, WriteTextFlags iWriteTextFlags, const char *caller_fname, int line)
{
	Message *pMessage = NULL;
	// If the field looked up is a DisplayMessage, use the Message reference in it.
	if (tpi[column].subtable == parse_DisplayMessage) {
		DisplayMessage *pDisplayMessage = TokenStoreGetPointer(tpi, column, structptr, index, NULL);
		if (pDisplayMessage) {
			pMessage = GET_REF(pDisplayMessage->hMessage);
			if (!pMessage && IS_HANDLE_ACTIVE(pDisplayMessage->hMessage)) {
				estrConcatf_dbg(ppEString, caller_fname, line, "[UNTRANSLATED: %s]", REF_STRING_FROM_HANDLE(pDisplayMessage->hMessage));
				return true;
			}
		}
	}
	else if ((TOK_GET_TYPE(tpi[column].type) == TOK_REFERENCE_X) &&
		(tpi[column].subtable == gMessageDict || !stricmp("Message", tpi[column].subtable))) {
		Message **ppMessage = (Message **)TokenStoreGetRefHandlePointer(tpi, column, structptr, index, NULL);
		if (ppMessage && *ppMessage && *ppMessage != REFERENT_SET_BUT_ABSENT)
			pMessage = *ppMessage;
		else if (ppMessage)
		{
			const char *pchHandle = TokenStoreGetRefString(tpi, column, structptr, index, NULL);
			if (pchHandle)
			{
				estrConcatf_dbg(ppEString, caller_fname, line, "[UNTRANSLATED: %s]", pchHandle);
				return true;
			}
		}

	} else if (tpi[column].subtable == parse_Message) {
		// Or, in the rare case it's actually a Message pointer, use that.
		pMessage = TokenStoreGetPointer(tpi, column, structptr, index, NULL);

	} else {
		return FieldWriteText_dbg(tpi, column, structptr, index, ppEString, iWriteTextFlags, 0, 0, caller_fname, line);
	}

	estrAppend2_dbg(ppEString, pMessage ? langTranslateMessageDefault(langID, pMessage, "") : "(null)", caller_fname, line);
	return true;
}


// ----------------------------------------------------------------------------
// Utility functions
// ----------------------------------------------------------------------------

bool msgExists(const char *msgKey)
{
	Message *pMessage = RefSystem_ReferentFromString(gMessageDict, msgKey);

	return !!pMessage;
}

bool isValidMessage(const Message *pMessage)
{
	return (pMessage &&
			pMessage->pcMessageKey && pMessage->pcMessageKey[0] &&
			pMessage->pcDefaultString && pMessage->pcDefaultString[0] &&
			UTF8StringIsValid(pMessage->pcDefaultString, NULL)
	);
}


void langFixupMessage(Message* pMessage, const char* pcKey, const char* pcDesc, const char* pcScope)
{
	bool reset = false;	// If true, the key changed and all other fields should be reset

	if (!pMessage) {
		return;
	}

	// If the message has no key or if its key doesn't correspond to the provided value, fix the key
	if (!pMessage->pcMessageKey || (stricmp(pcKey, pMessage->pcMessageKey) != 0)) {
		pMessage->pcMessageKey = allocAddString(pcKey);
		reset = true;
	}

	// If it has no description, set its description
	if (reset || !msgGetDescription(pMessage)) {
		msgSetDescription(pMessage, pcDesc);
	}

	// If it doesn't have a scope, set its scope
	if (reset || !msgGetScope(pMessage)) {
		msgSetScope(pMessage, pcScope);
	}
}

// Fix up fields in a message based on provided values.  Make the key terse using msgCreateTerseKey().
void langFixupMessageWithTerseKey(Message *pMessage, const char *pcKeyPrefix, const char* pcKeySeed, const char* pcDesc, const char* pcScope) {
	const char *pcKey = msgCreateUniqueKey(pcKeyPrefix, pcKeySeed, pMessage ? pMessage->pcMessageKey : NULL);
	langFixupMessage(pMessage, pcKey, pcDesc, pcScope);
}

// ----------------------------------------------------------------------------
// Message editing functions
// ----------------------------------------------------------------------------

Message *langCreateMessage(const char *pcMessageKey, const char *pcDescription, const char *pcScope, const char *pcDefaultString)
{
	Message *pMsg = StructCreate(parse_Message);
	if( pcMessageKey )
		pMsg->pcMessageKey = allocAddString(pcMessageKey);
	if( pcDescription )
		msgSetDescription(pMsg, pcDescription);
	if( pcScope )
		msgSetScope(pMsg, pcScope);
	if( pcDefaultString )
		pMsg->pcDefaultString = StructAllocString(pcDefaultString);

	return pMsg;
}

Message *langCreateMessageWithTerseKey(const char *pcKeyPrefix, const char *pcMessageKey, const char *pcDescription, const char *pcScope, const char *pcDefaultString) {
	const char *pcTerseKey = msgCreateUniqueKey(pcKeyPrefix, pcMessageKey, NULL);
	return langCreateMessage(pcTerseKey, pcDescription, pcDefaultString, pcScope);
}

Message *langCreateMessageDefaultOnly(const char *pcDefaultString) {
	return langCreateMessage(NULL, NULL, NULL, pcDefaultString);
}

//#ifndef NO_EDITORS

static void langMakeEditorCopyHelper(DisplayMessage *pDispMsg, bool bCreateIfMissing, ParseTable *pRootTable, void *pRootStruct, const char *pcPathString, const char *pcFilename)
{
	Message *pMsg = GET_REF(pDispMsg->hMessage);
	bool bInherited = false;
	if( !pMsg && pDispMsg->pEditorCopy && !pDispMsg->bEditorCopyIsServer)
	{
		pMsg = resGetObject(gMessageDict, pDispMsg->pEditorCopy->pcMessageKey);
	}

	// Check for inheriting from parent
	if (pRootStruct && StructInherit_IsInheriting(pRootTable, pRootStruct)) {
		char path[1024];
		sprintf(path, "%s.Message", pcPathString);
		if (StructInherit_GetOverrideType(pRootTable, pRootStruct, path) != OVERRIDE_NONE) {
			// Field is overridden from parent in exact form, so set up same override for editor copy
			sprintf(path, "%s.EditorCopy", pcPathString);
			StructInherit_CreateFieldOverride(pRootTable, pRootStruct, path);
		} else if (StructInherit_GetOverrideTypeEx(pRootTable, pRootStruct, path, true) == OVERRIDE_NONE) {
			// Field is not overridden at any level
			bInherited = true;
		}
	}

	if (pMsg) {
		// Set the editor copy and clear the handle
		if (pDispMsg->pEditorCopy) {
			StructCopyAll(parse_Message, pMsg, pDispMsg->pEditorCopy);
		} else {
			pDispMsg->pEditorCopy = StructClone(parse_Message, pMsg);
		}
		assert(pDispMsg->pEditorCopy);
		pDispMsg->bEditorCopyIsServer = resIsEditingVersionAvailable(gMessageDict, pDispMsg->pEditorCopy->pcMessageKey);
	}
	REMOVE_HANDLE(pDispMsg->hMessage);

	if (bCreateIfMissing && !pDispMsg->pEditorCopy) {
		// Create a struct if none present
		pMsg = StructCreate(parse_Message);
		pDispMsg->pEditorCopy = pMsg;
		pDispMsg->bEditorCopyIsServer = true;
	}
	if (pcFilename && pDispMsg->pEditorCopy && !bInherited) {
		// Reset filename if filename available and message is not inherited
		pDispMsg->pEditorCopy->pcFilename = pcFilename;
	}
}


// Makes editor copy of all DisplayMessage structs in within the parse table.
void langMakeEditorCopyInternal(ParseTable pti[], void *pStruct, bool bCreateIfMissing, ParseTable *pRootTable, void *pRootStruct, const char *pcPathString, const char *pcFilename)
{
	int i;

	// Check if this struct is itself a DisplayMessage or DisplayMessageWithVO
	if (stricmp("DisplayMessage", pti[0].name) == 0 && !(pti[0].type & TOK_USEROPTIONBIT_3)) {
		langMakeEditorCopyHelper((DisplayMessage*)pStruct, bCreateIfMissing, pRootTable, pRootStruct, pcPathString, pcFilename);
		return;
	}
	if (stricmp("DisplayMessageWithVO", pti[0].name) == 0 && !(pti[0].type & TOK_USEROPTIONBIT_3)) {
		langMakeEditorCopyHelper(&((DisplayMessageWithVO*)pStruct)->msg, bCreateIfMissing, pRootTable, pRootStruct, pcPathString, pcFilename);
		return;
	}

	FORALL_PARSETABLE(pti, i) {
		if (TOK_GET_TYPE(pti[i].type) == TOK_STRUCT_X && !(pti[i].type & TOK_USEROPTIONBIT_3)) {
			if (pti[i].subtable) {
				ParseTable *pSubtable = (ParseTable*)pti[i].subtable;
				char path[1024];
				if ((pti[i].type & TOK_EARRAY)) {
					// Recurse into earray of structs
					void ***peaStructs = TokenStoreGetEArray(pti, i, pStruct, NULL);
					int iKeyCol = ParserGetTableKeyColumn(pSubtable);
					int j;
					for(j=0; j<eaSize(peaStructs); ++j) {
						if (pRootStruct) {
							if (iKeyCol >= 0) {
								sprintf(path, "%s.%s[\"%d\"]", pcPathString, pti[i].name, TokenStoreGetInt(pSubtable, iKeyCol, (*peaStructs)[j], 0, NULL));
							} else {
								sprintf(path, "%s.%s[\"%d\"]", pcPathString, pti[i].name, j);
							}
						} else {
							path[0] = '\0';
						}
						langMakeEditorCopyInternal(pSubtable, (*peaStructs)[j], bCreateIfMissing, pRootTable, pRootStruct, path, pcFilename);
					}
				} else if (pSubtable[0].name && stricmp("DisplayMessage", pSubtable[0].name) == 0) {
					// DisplayMessage structures are the ones we're looking for
					DisplayMessage *pDispMsg = (DisplayMessage*)TokenStoreGetPointer(pti, i, pStruct, 0, NULL);
					sprintf(path, "%s.%s", pcPathString, pti[i].name);
					langMakeEditorCopyHelper(pDispMsg, bCreateIfMissing, pRootTable, pRootStruct, path, pcFilename);
				} else if (pSubtable[0].name && stricmp("DisplayMessageWithVO", pSubtable[0].name) == 0) {
					// DisplayMessage structures are the ones we're looking for
					DisplayMessageWithVO *pDispMsg = (DisplayMessageWithVO*)TokenStoreGetPointer(pti, i, pStruct, 0, NULL);
					sprintf(path, "%s.%s", pcPathString, pti[i].name);
					langMakeEditorCopyHelper(&pDispMsg->msg, bCreateIfMissing, pRootTable, pRootStruct, path, pcFilename);
				} else {
					// Recurse into non-array struct
					void *pSubstruct = TokenStoreGetPointer(pti, i, pStruct, 0, NULL);
					if (pSubstruct) {
						if (pRootStruct) {
							sprintf(path, "%s.%s", pcPathString, pti[i].name);
						} else {
							path[0] = '\0';
						}
						langMakeEditorCopyInternal(pSubtable, pSubstruct, bCreateIfMissing, pRootTable, pRootStruct, path, pcFilename);
					}
				}
			}
		}
	}
}

void langForEachDisplayMessage(ParseTable pti[], void *pStruct, void (*pCB)(DisplayMessage *pDisplayMessage, void *userdata), void *userdata)
{
	int i;

	// Check if this struct is itself a DisplayMessage or a DisplayMessageWithVO
	if (stricmp("DisplayMessage", pti[0].name) == 0) {
		if (pStruct)
			pCB((DisplayMessage*)pStruct, userdata);
		return;
	}
	if (stricmp("DisplayMessageWithVO", pti[0].name) == 0) {
		if (pStruct)
			pCB(&((DisplayMessageWithVO*)pStruct)->msg, userdata);
		return;
	}

	FORALL_PARSETABLE(pti, i) {
		if (TOK_GET_TYPE(pti[i].type) == TOK_STRUCT_X) {
			if (pti[i].subtable) {
				ParseTable *pSubtable = (ParseTable*)pti[i].subtable;
				if (pti[i].type & TOK_EARRAY) {
					// Recurse into earray of structs
					void ***peaStructs = TokenStoreGetEArray(pti, i, pStruct, NULL);
					int iKeyCol = ParserGetTableKeyColumn(pSubtable);
					int j;
					for(j=0; j<eaSize(peaStructs); ++j) {
						langForEachDisplayMessage(pSubtable, (*peaStructs)[j], pCB, userdata);
					}
				} else if (pSubtable[0].name && stricmp("DisplayMessage", pSubtable[0].name) == 0) {
					// DisplayMessage structures are the ones we're looking for
					DisplayMessage *pDispMsg = (DisplayMessage*)TokenStoreGetPointer(pti, i, pStruct, 0, NULL);
					langForEachDisplayMessage(pSubtable, pDispMsg, pCB, userdata);
				} else if (pSubtable[0].name && stricmp("DisplayMessageWithVO", pSubtable[0].name) == 0) {
					DisplayMessageWithVO *pDispMsg = (DisplayMessageWithVO*)TokenStoreGetPointer(pti, i, pStruct, 0, NULL);
					langForEachDisplayMessage(pSubtable, pDispMsg, pCB, userdata);
				} else {
					// Recurse into non-array struct
					void *pSubstruct = TokenStoreGetPointer(pti, i, pStruct, 0, NULL);
					if (pSubstruct) {
						langForEachDisplayMessage(pSubtable, pSubstruct, pCB, userdata);
					}
				}
			}
		}
	}
}

void langForEachMessageRef(ParseTable pti[], SA_PARAM_NN_VALID void *pStruct, void (*pCB)(const char* messageKey, void *userdata), void *userdata)
{
	int i;

	FORALL_PARSETABLE(pti, i) {
		if (TOK_GET_TYPE(pti[i].type) == TOK_REFERENCE_X) {
			// Check if this ref is to a Message
			if (stricmp(pti[i].subtable, "Message") == 0) {
				const char* refStr = TokenStoreGetRefString(pti, i, pStruct, 0, NULL);
				if( refStr ) {
					pCB( refStr, userdata );
				}
			}
		} else if (TOK_GET_TYPE(pti[i].type) == TOK_STRUCT_X) {
			if (pti[i].subtable) {
				ParseTable *pSubtable = (ParseTable*)pti[i].subtable;
				if (pti[i].type & TOK_EARRAY) {
					// Recurse into earray of structs
					void ***peaStructs = TokenStoreGetEArray(pti, i, pStruct, NULL);
					int iKeyCol = ParserGetTableKeyColumn(pSubtable);
					int j;
					for(j=0; j<eaSize(peaStructs); ++j) {
						langForEachMessageRef(pSubtable, (*peaStructs)[j], pCB, userdata);
					}
				} else if (pSubtable[0].name && stricmp("DisplayMessage", pSubtable[0].name) == 0) {
					// DisplayMessage structures are the ones we're looking for
					DisplayMessage *pDispMsg = (DisplayMessage*)TokenStoreGetPointer(pti, i, pStruct, 0, NULL);
					langForEachMessageRef(pSubtable, pDispMsg, pCB, userdata);
				} else if (pSubtable[0].name && stricmp("DisplayMessageWithVO", pSubtable[0].name) == 0) {
					// DisplayMessage structures are the ones we're looking for
					DisplayMessageWithVO *pDispMsg = (DisplayMessageWithVO*)TokenStoreGetPointer(pti, i, pStruct, 0, NULL);
					langForEachMessageRef(pSubtable, pDispMsg, pCB, userdata);
				} else {
					// Recurse into non-array struct
					void *pSubstruct = TokenStoreGetPointer(pti, i, pStruct, 0, NULL);
					if (pSubstruct) {
						langForEachMessageRef(pSubtable, pSubstruct, pCB, userdata);
					}
				}
			}
		}
	}
}

static void langThisMessageIsActive(DisplayMessage *pDisplayMessage, bool *pbActive)
{
	if(pDisplayMessage->pEditorCopy || REF_HANDLE_IS_ACTIVE(pDisplayMessage->hMessage))
		*pbActive = true;
}

bool langHasActiveMessage(ParseTable pParseTable[], void *pStruct)
{
	bool bRet = false;
	langForEachDisplayMessage(pParseTable, pStruct, langThisMessageIsActive, &bRet);
	return bRet;
}

// Makes editor copy of all DisplayMessage structs in within the parse table.
void langMakeEditorCopy(ParseTable pti[], void *pStruct, bool bCreateIfMissing)
{
	bool bHasParent = (StructInherit_GetParentName(pti, pStruct) != NULL);
	const char *pcFilename = ParserGetFilename(pti, pStruct);
	if (pcFilename) {
		char buf[1024];
		sprintf(buf, "%s.ms", pcFilename);
		pcFilename = allocAddFilename(buf);
	}

	langMakeEditorCopyInternal(pti, pStruct, bCreateIfMissing, bHasParent ? pti : NULL, bHasParent ? pStruct : NULL, "", pcFilename);
}


// Used by langApplyEditorCopyInternal
static void langAddKeyInfo(const char *pcMessageKey, const char *pcFilename, MsgKeyInfo ***peaKeyInfo)
{
	MsgKeyInfo *pInfo;
	int i;

	if (!pcMessageKey) {
		return;
	}

	// See if already in info
	for(i=eaSize(peaKeyInfo)-1; i>=0; --i) {
		if (stricmp(pcMessageKey,(*peaKeyInfo)[i]->pcMessageKey) == 0) {
			return;
		}
	}

	// Add to add info list
	pInfo = calloc(1,sizeof(MsgKeyInfo));
	pInfo->pcMessageKey = strdup(pcMessageKey);
	pInfo->pcFilename = allocAddFilename(pcFilename);
	eaPush(peaKeyInfo, pInfo);
}


// Used by langApplyEditorCopyInternal
static void langRemoveKeyInfo(const char *pcMessageKey, MsgKeyInfo ***peaKeyInfo)
{
	int i;

	for(i=eaSize(peaKeyInfo)-1; i>=0; --i) {
		if (stricmp(pcMessageKey,(*peaKeyInfo)[i]->pcMessageKey) == 0) {
			free((*peaKeyInfo)[i]->pcMessageKey);
			free((*peaKeyInfo)[i]);
			eaRemove(peaKeyInfo, i);
			return;
		}
	}
}

static void langApplyRemoveDuplicateFilenames(char ***peaOldFilenames, char ***peaNewFilenames)
{
	int i,j;

	// Remove filenames from the old list that are also in the new list
	for(i=eaSize(peaOldFilenames)-1; i>=0; --i) {
		for(j=eaSize(peaNewFilenames)-1; j>=0; --j) {
			if (stricmp((*peaOldFilenames)[i], (*peaNewFilenames)[j]) == 0) {
				free((*peaOldFilenames)[i]);
				eaRemove(peaOldFilenames, i);
				break;
			}
		}
	}
}

static void langApplyAddFilename(char ***peaFilenames, const char *pcFilename)
{
	int i;

	// Only push unique filenames
	for(i=eaSize(peaFilenames)-1; i>=0; --i) {
		if (stricmp((*peaFilenames)[i], pcFilename) == 0) {
			return;
		}
	}
	eaPush(peaFilenames, strdup(pcFilename));
}

static void langApplyEditorCopyHelper(DisplayMessage *pDispMsg, ParseTable pti[], int mode, void *pStruct, const char *pcFilename, 
										char ***peaOldFilenames, char ***peaNewFilenames, MsgKeyInfo ***peaKeyInfo, bool bRemoveEmpty, 
										bool bHasParent, ParseTable *pRootTable, void *pRootStruct, const char *pcPathString)
{
	Message *pStoredMsg = NULL;
	bool bNeedUpdateFromStruct = false;
	char path[1024];
	char path2[1024];
	Message *pMsg;
	bool bAddStoredMessageToOldFilenames = true;

	if (!pDispMsg) {
		return;
	}

	pMsg = pDispMsg->pEditorCopy;
	if (!pMsg) {
		pMsg = GET_REF(pDispMsg->hMessage);
	}

	if ((mode == RECURSE_MODE_ORIG) || (mode == RECURSE_MODE_DELETE)) {
		// This is for recursing on the original struct to collect used key info
		// This is the only thing different between the original/delete recurse and the
		// main recurse.
		if (pMsg) {
			// If no parent, or if overriding parent, record the key info
			sprintf(path, "%s.EditorCopy", pcPathString);
			sprintf(path2, "%s.Message", pcPathString);
			if (!bHasParent || (StructInherit_GetOverrideTypeEx(pRootTable, pRootStruct, path, true) != OVERRIDE_NONE) || (StructInherit_GetOverrideTypeEx(pRootTable, pRootStruct, path2, true) != OVERRIDE_NONE)) {
				langAddKeyInfo(pMsg->pcMessageKey, pMsg->pcFilename, peaKeyInfo);
			}
		}
		// No other processing required
		return;
	}

	// Check for inheriting from parent
	if (bHasParent) {
		sprintf(path, "%s.EditorCopy", pcPathString);
		sprintf(path2, "%s.Message", pcPathString);
		if (StructInherit_GetOverrideTypeEx(pRootTable, pRootStruct, path, true) == OVERRIDE_NONE) {
			// Field is inherited from parent, so clear override (if any)
			if (StructInherit_GetOverrideType(pRootTable, pRootStruct, path2) != OVERRIDE_NONE) {
				StructInherit_DestroyOverride(pRootTable, pRootStruct, path2);
			}

			// Then clear editor copy and reset message handle
			if (pDispMsg->pEditorCopy) {
				pStoredMsg = RefSystem_ReferentFromString(gMessageDict, pDispMsg->pEditorCopy->pcMessageKey);
				if (pStoredMsg) {
					SET_HANDLE_FROM_REFERENT(gMessageDict, pStoredMsg, pDispMsg->hMessage);
				}
				StructDestroy(parse_Message, pDispMsg->pEditorCopy);						
				pDispMsg->pEditorCopy = NULL;
			}
			// Skip other processing if inheriting
			return;
		}

		// Field overrides parent, so make sure same is true for message
		// But also clear the editor copy override
		if (StructInherit_GetOverrideType(pRootTable, pRootStruct, path) != OVERRIDE_NONE)
		{
			StructInherit_DestroyOverride(pRootTable, pRootStruct, path);
			StructInherit_CreateFieldOverride(pRootTable, pRootStruct, path2);
			bNeedUpdateFromStruct = true;
		}
	}
	
	// Mark dictionary copy's file for this key as touched
	if (pMsg) {
		pStoredMsg = RefSystem_ReferentFromString(gMessageDict, pMsg->pcMessageKey);
	}

	// Remove invalid entry if asked to do so
	if (bRemoveEmpty && pDispMsg->pEditorCopy && !isValidMessage(pDispMsg->pEditorCopy)) {
		StructDestroy(parse_Message, pDispMsg->pEditorCopy);						
		pDispMsg->pEditorCopy = NULL;
		pMsg = NULL;
		bAddStoredMessageToOldFilenames = true;
	} 

	if (pMsg) {
		// Remove the key from the info list (if either editor copy or message)
		langRemoveKeyInfo(pMsg->pcMessageKey, peaKeyInfo);
	}

	if (pDispMsg->pEditorCopy) {
		// Only perform modify if have an editor copy
		assertmsg(isValidMessage(pDispMsg->pEditorCopy), "Messages must have a key, filename, and default string");

		// Update filename value if necessary
		if (pcFilename) {
			pDispMsg->pEditorCopy->pcFilename = allocAddFilename(pcFilename);
		}

		// TOK_USEROPTIONBIT_1 means don't compare for messages
		if (!pStoredMsg || (StructCompare(parse_Message, pStoredMsg, pDispMsg->pEditorCopy, 0, 0, TOK_USEROPTIONBIT_1) != 0) || strcmp(pStoredMsg->pcFilename,pDispMsg->pEditorCopy->pcFilename)) {
			// Message was changed, so we need to save off the changes

			// Mark filename as affected
			if (peaNewFilenames && pDispMsg->pEditorCopy->pcFilename) {
				langApplyAddFilename(peaNewFilenames, pDispMsg->pEditorCopy->pcFilename);
			}

			// Store the editor copy into the dictionary
			if (pStoredMsg) {
				RefSystem_MoveReferent(pDispMsg->pEditorCopy, pStoredMsg);
			} else {
				RefSystem_AddReferent(gMessageDict, pDispMsg->pEditorCopy->pcMessageKey, pDispMsg->pEditorCopy);
			}
			SET_HANDLE_FROM_REFERENT(gMessageDict, pDispMsg->pEditorCopy, pDispMsg->hMessage);
			bAddStoredMessageToOldFilenames = true;
		} else {
			// Entry not changed, so just reference existing message
			SET_HANDLE_FROM_REFERENT(gMessageDict, pStoredMsg, pDispMsg->hMessage);
		}
		pDispMsg->pEditorCopy = NULL;
	}

	if (bAddStoredMessageToOldFilenames)
	{
		if (pStoredMsg && pStoredMsg->pcFilename && peaOldFilenames) {
			// Existing message goes on old filenames list
 			langApplyAddFilename(peaOldFilenames, pStoredMsg->pcFilename);
		}
	}

	if (bNeedUpdateFromStruct) {
		StructInherit_UpdateFromStruct(pRootTable, pRootStruct, false);
	}
}


// Used by langApplyEditorCopy for recursion
static void langApplyEditorCopyInternal(ParseTable pti[], int mode, void *pStruct, const char *pcFilename, 
										char ***peaOldFilenames, char ***peaNewFilenames, MsgKeyInfo ***peaKeyInfo, bool bRemoveEmpty, 
										bool bHasParent, ParseTable *pRootTable, void *pRootStruct, const char *pcPathString)
{
	int i;
	// Check if this struct is itself a DisplayMessage or a DisplayMessageWithVO
	if (stricmp("DisplayMessage", pti[0].name) == 0 && !(pti[0].type & TOK_USEROPTIONBIT_3)) {
		langApplyEditorCopyHelper((DisplayMessage*)pStruct, pti, mode, pStruct, pcFilename,
									peaOldFilenames, peaNewFilenames, peaKeyInfo, bRemoveEmpty, 
									bHasParent, pRootTable, pRootStruct, pcPathString);
		return;
	}
	if (stricmp("DisplayMessageWithVO", pti[0].name) == 0 && !(pti[0].type & TOK_USEROPTIONBIT_3)) {
		langApplyEditorCopyHelper(&((DisplayMessageWithVO*)pStruct)->msg, pti, mode, pStruct, pcFilename,
									peaOldFilenames, peaNewFilenames, peaKeyInfo, bRemoveEmpty, 
									bHasParent, pRootTable, pRootStruct, pcPathString);
		return;
	}

	FORALL_PARSETABLE(pti, i) {
		// Only care about structs with subtables
		if ((TOK_GET_TYPE(pti[i].type) == TOK_STRUCT_X) && (pti[i].subtable) && !(pti[i].type & TOK_USEROPTIONBIT_3)) {
			ParseTable *pSubtable = (ParseTable*)pti[i].subtable;
			char path[1024];

			if (pti[i].type & TOK_EARRAY) {
				// Recurse into earray of structs
				void ***peaStructs = TokenStoreGetEArray(pti, i, pStruct, NULL);
				int j;
				int iKeyCol = ParserGetTableKeyColumn(pSubtable);

				for(j=0; j<eaSize(peaStructs); ++j) {
					void *pSubstruct = (*peaStructs)[j];

					if (bHasParent) {
						if (iKeyCol >= 0) {
							sprintf(path, "%s.%s[\"%d\"]", pcPathString, pti[i].name, TokenStoreGetInt(pSubtable, iKeyCol, pSubstruct, 0, NULL));
						} else {
							sprintf(path, "%s.%s[\"%d\"]", pcPathString, pti[i].name, j);
						}
					} else {
						path[0] = '\0';
					}

					langApplyEditorCopyInternal(pSubtable, mode, pSubstruct, pcFilename, 
												peaOldFilenames, peaNewFilenames, peaKeyInfo, bRemoveEmpty, 
												bHasParent, pRootTable, pRootStruct, path);
				}

			} else if (pSubtable[0].name && stricmp("DisplayMessage", pSubtable[0].name) == 0) {
				// DisplayMessage structures are the ones we're looking for
				DisplayMessage *pDispMsg = (DisplayMessage*)TokenStoreGetPointer(pti, i, pStruct, 0, NULL);
				sprintf(path, "%s.%s", pcPathString, pti[i].name);
				langApplyEditorCopyHelper(pDispMsg, pti, mode, pStruct, pcFilename,
											peaOldFilenames, peaNewFilenames, peaKeyInfo, bRemoveEmpty, 
											bHasParent, pRootTable, pRootStruct, path);
			} else if (pSubtable[0].name && stricmp("DisplayMessageWithVO", pSubtable[0].name) == 0) {
				// DisplayMessage structures are the ones we're looking for
				DisplayMessageWithVO *pDispMsg = (DisplayMessageWithVO*)TokenStoreGetPointer(pti, i, pStruct, 0, NULL);
				sprintf(path, "%s.%s", pcPathString, pti[i].name);
				langApplyEditorCopyHelper(SAFE_MEMBER_ADDR(pDispMsg, msg), pti, mode, pStruct, pcFilename,
											peaOldFilenames, peaNewFilenames, peaKeyInfo, bRemoveEmpty, 
											bHasParent, pRootTable, pRootStruct, path);
			} else {
				// Recurse into non-array struct that isn't a DisplayMessage
				void *pSubstruct = TokenStoreGetPointer(pti, i, pStruct, 0, NULL);
				if (pRootStruct) {
					sprintf(path, "%s.%s", pcPathString, pti[i].name);
				} else {
					path[0] = '\0';
				}
				if (pSubstruct) {
					langApplyEditorCopyInternal(pSubtable, mode, pSubstruct, pcFilename, 
												peaOldFilenames, peaNewFilenames, peaKeyInfo, bRemoveEmpty, 
												bHasParent, pRootTable, pRootStruct, path);
				}
			}
		}
	}
}


// Pushes the editor copy into the Message dictionary and makes the DisplayMessage
// structures back to using handles.  The filename of the Message structures is generated
// based on the CURRENTFILE of the root structure (if one is defined).  The message files
// are saved to disk.
void langApplyEditorCopy2(ParseTable pti[], void *pStruct, void *pOrigStruct, bool bRemoveEmpty, bool bNoSave, bool bUseErrorDialog)
{
	// Figure out the message file to use
	const char *pcFilename;
	char **eaOldFilenames = NULL;
	char **eaNewFilenames = NULL;
	MsgKeyInfo **eaKeyInfo = NULL;
	char buf[1024];
	int i;
	bool bHasParent;

	pcFilename = ParserGetFilename(pti, pStruct);
	if (pcFilename) {
		sprintf(buf, "%s.ms", pcFilename);
		pcFilename = allocAddFilename(buf);
	}

	bHasParent = (StructInherit_GetParentName(pti, pStruct) != NULL);

	if (pOrigStruct) {
		bool bHasOrigParent = pOrigStruct && (StructInherit_GetParentName(pti, pOrigStruct) != NULL);

		// Recurse the original structure collecting key info
		langApplyEditorCopyInternal(pti, RECURSE_MODE_ORIG, pOrigStruct, pcFilename, 
									&eaOldFilenames, &eaNewFilenames, &eaKeyInfo, bRemoveEmpty, 
									bHasOrigParent, pti, pOrigStruct, "");
	}

	// Recurse the current structure, applying the editor copy
	langApplyEditorCopyInternal(pti, RECURSE_MODE_MAIN, pStruct, pcFilename, 
								&eaOldFilenames, &eaNewFilenames, &eaKeyInfo, bRemoveEmpty, 
								bHasParent, pti, pStruct, "");

	// Do the key removals here
	for(i=eaSize(&eaKeyInfo)-1; i>=0; --i) {
		Message *pMsg = RefSystem_ReferentFromString(gMessageDict, eaKeyInfo[i]->pcMessageKey);
		if (pMsg) {
			RefSystem_RemoveReferent(pMsg, false);
			langApplyAddFilename(&eaOldFilenames, eaKeyInfo[i]->pcFilename);
		}
		free(eaKeyInfo[i]->pcMessageKey);
		free(eaKeyInfo[i]);
	}
	eaDestroy(&eaKeyInfo);

	if (!bNoSave) {
		// Write out any message files that got modified
		// Write out old (message removed from) files before new (messages added to) files
		langApplyRemoveDuplicateFilenames(&eaOldFilenames, &eaNewFilenames);
		
		for(i=eaSize(&eaOldFilenames)-1; i>=0; --i) {
			makeDirectoriesForFile(eaOldFilenames[i]);
			if( !ParserWriteTextFileFromDictionary(eaOldFilenames[i], gMessageDict, 0, 0)) {
				#if !PLATFORM_CONSOLE
				{
					char buffer[ 1024 ];
					sprintf( buffer, "An internal error saving messages happened.  Jared F needs to look at your computer RIGHT NOW.  Don't click \"OK\".  Get him immediately.\n\nFile: %s -- Could not write to file.", eaOldFilenames[ i ]);
					if (bUseErrorDialog) {
						errorDialogInternal(NULL, buffer, "Get Jared F immediately!", "Get Jared F immediately!", 1);
					} else {
						printf("%s", buffer);
					}
				}
				#endif
			}
		}
		for(i=eaSize(&eaNewFilenames)-1; i>=0; --i) {
			makeDirectoriesForFile(eaNewFilenames[i]);
			if( !ParserWriteTextFileFromDictionary(eaNewFilenames[i], gMessageDict, 0, 0)) {
				#if !PLATFORM_CONSOLE
				{
					char buffer[ 1024 ];
					sprintf( buffer, "An internal error saving messages happened.  Jared F needs to look at your computer RIGHT NOW.  Don't click \"OK\".  Get him immediately.\n\nFile: %s -- Could not write to file.", eaNewFilenames[ i ]);
					if (bUseErrorDialog) {
						errorDialogInternal(NULL, buffer, "Get Jared F immediately!", "Get Jared F immediately!", 1);
					} else {
						printf("%s", buffer);
					}
				}
				#endif
			}
		}
	}
	eaDestroyEx(&eaOldFilenames, NULL);
	eaDestroyEx(&eaNewFilenames, NULL);
}

// Pushes the editor copy into the Message dictionary and makes the DisplayMessage
// structures back to using handles.  The filename of the Message structures is generated
// based on the CURRENTFILE of the root structure (if one is defined).  The message files
// are saved to disk.
void langApplyEditorCopy(ParseTable pti[], void *pStruct, void *pOrigStruct, bool bRemoveEmpty, bool bNoSave)
{
	langApplyEditorCopy2(pti, pStruct, pOrigStruct, bRemoveEmpty, bNoSave, true);
}

void langApplyEditorCopySingleFile(ParseTable pParseTable[], void *pStruct, bool bRemoveEmpty, bool bNoSave)
{
	// Figure out the message file to use
	const char *pcFilename;
	MsgKeyInfo **eaKeyInfo = NULL;
	char buf[1024];
	int i;

	pcFilename = ParserGetFilename(pParseTable, pStruct);
	assert(pcFilename);
	sprintf(buf, "%s.ms", pcFilename);
	pcFilename = allocAddFilename(buf);

	// Assume all mesages in the dictionary were in this struct
	FOR_EACH_IN_REFDICT(gMessageDict, Message, msg)
	{
		if (msg->pcFilename == pcFilename)
		{
			langAddKeyInfo(msg->pcMessageKey, msg->pcFilename, &eaKeyInfo);
		}
	}
	FOR_EACH_END;
	

	// Recurse the current structure, applying the editor copy
	langApplyEditorCopyInternal(pParseTable, RECURSE_MODE_MAIN, pStruct, pcFilename, 
								NULL, NULL, &eaKeyInfo, bRemoveEmpty, false,
								pParseTable, pStruct, "");

	// Do the key removals here
	for(i=eaSize(&eaKeyInfo)-1; i>=0; --i) {
		Message *pMsg = RefSystem_ReferentFromString(gMessageDict, eaKeyInfo[i]->pcMessageKey);
		if (pMsg) {
			RefSystem_RemoveReferent(pMsg, false);
		}
		free(eaKeyInfo[i]->pcMessageKey);
		free(eaKeyInfo[i]);
	}
	eaDestroy(&eaKeyInfo);

	if (!bNoSave) {
		makeDirectoriesForFile(pcFilename);
		if (!ParserWriteTextFileFromDictionary(pcFilename, gMessageDict, 0, 0)) {
			#if !PLATFORM_CONSOLE
			{
				char buffer[ 1024 ];
				sprintf( buffer, "An internal error saving messages happened.  Jared F needs to look at your computer RIGHT NOW.  Don't click \"OK\".  Get him immediately.\n\nFile: %s -- Could not write to file.", pcFilename);
				errorDialogInternal(NULL, buffer, "Get Jared F immediately!", "Get Jared F immediately!", 1);
			}
			#endif
		}
	}
}


// Deletes all messages found in the struct from the dictionary.
// This is used when deleting an object
void langDeleteMessages(ParseTable pti[], void *pStruct)
{
	char **eaOldFilenames = NULL;
	MsgKeyInfo **eaKeyInfo = NULL;
	int i;
	bool bHasParent;

	bHasParent = (StructInherit_GetParentName(pti, pStruct) != NULL);

	// Recurse the current structure, applying the editor copy
	langApplyEditorCopyInternal(pti, RECURSE_MODE_DELETE, pStruct, NULL, 
								&eaOldFilenames, NULL, &eaKeyInfo, false, 
								bHasParent, pti, pStruct, "");

	// Do the key removals here
	for(i=eaSize(&eaKeyInfo)-1; i>=0; --i) {
		Message *pMsg = RefSystem_ReferentFromString(gMessageDict, eaKeyInfo[i]->pcMessageKey);
		if (pMsg) {
			RefSystem_RemoveReferent(pMsg, false);
			langApplyAddFilename(&eaOldFilenames, eaKeyInfo[i]->pcFilename);
		}
		free(eaKeyInfo[i]->pcMessageKey);
		free(eaKeyInfo[i]);
	}
	eaDestroy(&eaKeyInfo);

	// Write out any message files that got modified
	for(i=eaSize(&eaOldFilenames)-1; i>=0; --i) {
		ParserWriteTextFileFromDictionary(eaOldFilenames[i], gMessageDict, 0, 0);
	}
	eaDestroyEx(&eaOldFilenames, NULL);
}


// Pushes the editor copy into the Message dictionary and makes the DisplayMessage
// structures back to using handles (it deletes the editor copy).  The orig struct should
// have references and not editor copies.  The filename of the Message structures is
// generated based on the CURRENTFILE of the root structure (if one is defined).  The 
// message files are saved to disk.  If pStruct is NULL, deletes entries from pOrigStruct.
void langApplyEditorCopyServerSide(ParseTable pParseTable[], void *pStruct, char *pOrigStruct, bool bRemoveEmpty)
{
	if (!pStruct && pOrigStruct) {
		langDeleteMessages(pParseTable, pOrigStruct);
	} else if (pStruct) {
		langApplyEditorCopy(pParseTable, pStruct, pOrigStruct, bRemoveEmpty, false);
	}
}


// ----------------------------------------------------------------------------
// Display message list functions
// ----------------------------------------------------------------------------


// Adds a message to a DisplayMessageList.
// If there is already a message with that key in the list, it is replaced.
// Returns the added message
DisplayMessage *langAddDisplayMessageToList(DisplayMessageList *pList, const Message *pMessage)
{
	DisplayMessage *pNewMsg;
	int i;

	// Look for an existing message
	for(i=eaSize(&pList->eaMessages)-1; i>=0; --i) {
		if (pList->eaMessages[i]->pEditorCopy && stricmp(pList->eaMessages[i]->pEditorCopy->pcMessageKey, pMessage->pcMessageKey) == 0) {
			// Replace it
			StructCopyAll(parse_Message, pMessage, pList->eaMessages[i]->pEditorCopy);
			return pList->eaMessages[i];
		}
	}

	// Add a new message
	pNewMsg = StructCreate(parse_DisplayMessage);
	pNewMsg->pEditorCopy = StructClone(parse_Message, (Message*)pMessage);
	eaPush(&pList->eaMessages, pNewMsg);

	return pNewMsg;
}

// Adds a message to a DisplayMessageList, as a reference (not an editor copy).
// If there is already a message with that key in the list, does nothing.
// Returns the added message
DisplayMessage *langAddDisplayMessageReferenceToList(DisplayMessageList *pList, const char *pchMessageKey)
{
	DisplayMessage *pNewMsg;
	int i;

	if (!pchMessageKey)
		return NULL;

	// Look for an existing message
	for(i=eaSize(&pList->eaMessages)-1; i>=0; --i) {
		if ((pList->eaMessages[i]->pEditorCopy && stricmp(pList->eaMessages[i]->pEditorCopy->pcMessageKey, pchMessageKey) == 0)
			|| (IS_HANDLE_ACTIVE(pList->eaMessages[i]->hMessage) && (0 == stricmp(REF_STRING_FROM_HANDLE(pList->eaMessages[i]->hMessage), pchMessageKey))))
		{
			return pList->eaMessages[i];
		}
	}

	// Add a new message
	pNewMsg = StructCreate(parse_DisplayMessage);
	SET_HANDLE_FROM_STRING(gMessageDict, pchMessageKey, pNewMsg->hMessage);
	eaPush(&pList->eaMessages, pNewMsg);

	return pNewMsg;
}


// Gets a display message from a DisplayMessageList
// If not in the list, but in the message dictionary, it loads it into the list.
// Returns NULL if the message does not exist
DisplayMessage *langGetDisplayMessageFromList(DisplayMessageList *pList, const char *pcMessageKey, bool bAddIfNotFound)
{
	Message *pMessage;
	int i;

	// Look for an existing message
	for(i=eaSize(&pList->eaMessages)-1; i>=0; --i) {
		if (pList->eaMessages[i]->pEditorCopy && stricmp(pList->eaMessages[i]->pEditorCopy->pcMessageKey, pcMessageKey) == 0) {
			return pList->eaMessages[i];
		}
	}

	if (bAddIfNotFound)
	{
		// Check if it is in the dictionary
		pMessage = RefSystem_ReferentFromString(gMessageDict, pcMessageKey);
		if (pMessage) {
			// Add a new message
			DisplayMessage *pNewMsg = StructCreate(parse_DisplayMessage);
			pNewMsg->pEditorCopy = StructClone(parse_Message, pMessage);
			eaPush(&pList->eaMessages, pNewMsg);
			return pNewMsg;
		}
	}

	return NULL;
}

// Gets a message from a DisplayMessageList
// If not in the list, but in the message dictionary, returns the version from the dictionary.
const Message* langGetMessageFromListOrDictionary(DisplayMessageList *pList, const char *pcMessageKey)
{
	// Look for message in the display message list.  Don't add to list in case this message
	// came from an EncounterDef
	DisplayMessage *pDispMessage = langGetDisplayMessageFromList(pList, pcMessageKey, false);
	if (pDispMessage && pDispMessage->pEditorCopy)
		return pDispMessage->pEditorCopy;

	// If it wasn't in the list, try the dictionary.
	// This should work because the StaticEncounter has a reference to the EncounterDef					
	return RefSystem_ReferentFromString(gMessageDict, pcMessageKey);
}


// Gets a display message from a DisplayMessageList.
// If not in the list, but in the message dictionary, it loads it into the list.
// If no entry exists for the provided key, one is created using the provided data.
DisplayMessage *langGetOrCreateDisplayMessageFromList(DisplayMessageList *pList, const char *pcMessageKey,
													  const char *pcDescription, const char *pcScope)
{
	DisplayMessage *pNewMsg;
	Message *pMessage;
	int i;

	assertmsg(pcMessageKey, "Must provide a key when adding to the list");

	// Look for an existing message
	for(i=eaSize(&pList->eaMessages)-1; i>=0; --i) {
		if (pList->eaMessages[i]->pEditorCopy && pList->eaMessages[i]->pEditorCopy->pcMessageKey && stricmp(pList->eaMessages[i]->pEditorCopy->pcMessageKey, pcMessageKey) == 0) {
			return pList->eaMessages[i];
		}
	}

	// Add a new message
	pNewMsg = StructCreate(parse_DisplayMessage);

	// Check if it is in the dictionary
	pMessage = RefSystem_ReferentFromString(gMessageDict, pcMessageKey);
	if (pMessage) {
		pNewMsg->pEditorCopy = StructClone(parse_Message, pMessage);
	} else {
		// Not in the dictionary so create one
		pNewMsg->pEditorCopy = StructCreate(parse_Message);
		pNewMsg->pEditorCopy->pcMessageKey = StructAllocString(pcMessageKey);
		msgSetScope(pNewMsg->pEditorCopy, pcScope);
		msgSetDescription(pNewMsg->pEditorCopy, pcDescription);
	}
	eaPush(&pList->eaMessages, pNewMsg);

	return pNewMsg;
}


// Removes a display message from the list
void langRemoveDisplayMessageFromList(DisplayMessageList *pList, const char *pcMessageKey)
{
	int i;

	// Look for an existing message
	for(i=eaSize(&pList->eaMessages)-1; i>=0; --i) {
		if (pList->eaMessages[i]->pEditorCopy && stricmp(pList->eaMessages[i]->pEditorCopy->pcMessageKey, pcMessageKey) == 0) {
			// Remove it
			StructDestroy(parse_Message, pList->eaMessages[i]->pEditorCopy);
			eaRemove(&pList->eaMessages, i);
			return;
		}
	}
}

//#endif

// ----------------------------------------------------------------------------
// Dictionary lifecycle functions
// ----------------------------------------------------------------------------


static void stripDownMessage(Message *pMessage) 
{
	msgSetDescription(pMessage, NULL);
	msgSetScope(pMessage, NULL);
	pMessage->bFinal = false;
	pMessage->bDoNotTranslate = false;
}


static void validateMessage(Message *pMessage) 
{
	const char *pErr = NULL;
	if (!pMessage->pcDefaultString) {
		ErrorFilenamef(pMessage->pcFilename, "Message loaded with no Default String!");
	}
	else if (!UTF8StringIsValid(pMessage->pcDefaultString, &pErr)) {
		ErrorFilenamef(pMessage->pcFilename, "Message %s: Translation is not a valid UTF-8 string! Problem starts at: %s", pMessage->pcMessageKey, pErr);
	}

	if (!pMessage->pcMessageKey) {
		ErrorFilenamef(pMessage->pcFilename, "Message loaded with no Message Key!");
	}
	if( !resIsValidExtendedName(pMessage->pcMessageKey) ) {
		ErrorFilenamef( pMessage->pcFilename, "Message key is illegal: '%s'", pMessage->pcMessageKey );
	}
	if( !resIsValidExtendedScope(msgGetScope(pMessage))) {
		ErrorFilenamef( pMessage->pcFilename, "Message key scope is illegal: '%s'", msgGetScope(pMessage));
	}
	
}


static void populateFakeTranslation(Message *pMessage, int loc)
{
	TranslatedMessage *pTrans = langFindTranslatedMessage(locGetLanguage(loc), pMessage);
	if (!pTrans) {
		char *estrText = NULL;

		pTrans = StructCreate(parse_TranslatedMessage);
		assert(pTrans);
		estrPrintf(&estrText,"(%s %s)", locGetCrypticSpecific2LetterIdentifier(loc), pMessage->pcDefaultString ? pMessage->pcDefaultString : "");
		pTrans->pcMessageKey = pMessage->pcMessageKey;
		pTrans->pcTranslatedString = StructAllocString(estrText);
		eaPush(&gTranslationDicts[loc]->ppTranslatedMessages, pTrans);
		stashAddPointer(gTranslationDicts[loc]->sLookupTable, pTrans->pcMessageKey, pTrans, false);
		//Errorf("Adding FakeTranslation for lang=\"%s\": Key=\"%s\", DefaultString=\"%s\"", locGetName(loc), pMessage->pcMessageKey,  pMessage->pcDefaultString);
		estrDestroy(&estrText);
	}
}



static int fixupMessage(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, Message *pMessage, U32 userID)
{
	int loc;

	switch (eType)
	{	
	xcase RESVALIDATE_POST_TEXT_READING:
		validateMessage(pMessage);

		// This is true only if on client and only if loading
		if (s_bLoading && IsClient()) {
			// For client, only take messages in maps folder if in a zone file
			if (strStartsWith(pMessage->pcFilename, "maps/")) {
				if (strstri(pMessage->pcFilename, ".zone.ms") == NULL) {
					resDoNotLoadCurrentResource(); // Skips the message
					return VALIDATE_HANDLED;
				}
			}

			if (!giMakeOneLocaleOPFilesAndExit)
			{
				// For client locally loaded messages clear memory prior to binning
				stripDownMessage(pMessage);
			}

			// Create a fake translation if one isn't available while in dev mode
			loc = getCurrentLocale();
			if (gbMakeBinsAndExit) { // Override locale if non-zero
				loc = s_makeBinsLoc;
			}
			if (s_bAddFakeTranslations && (loc != LOCALE_ID_ENGLISH) && isDevelopmentMode() && !resIsDictionaryEditMode(gMessageDict) && (!IsClient() || s_bLoading)) {
				populateFakeTranslation(pMessage, loc);
			}

			// On client when loading, perform translation and replace text in message with the translated version
			if (!pMessage->bLocallyTranslated) {
				TranslatedMessage *pTrans = langFindTranslatedMessage(locGetLanguage(loc), pMessage);
				if (pTrans) {
					// Replace default string with translation
					StructFreeString(pMessage->pcDefaultString);
					pMessage->pcDefaultString = StructAllocString(pTrans->pcTranslatedString);
				} else {
					// Check if there is an alternate translation to use
					int altLang = locGetAlternateLanguage(loc);
					if (altLang == LANGUAGE_NONE) {
						// If request "none", then blank the message
						StructFreeString(pMessage->pcDefaultString);
						pMessage->pcDefaultString = StructAllocString("");
					} else if (altLang != LANGUAGE_DEFAULT) {
						// If request is something other than default, try getting that language
						pTrans = langFindTranslatedMessage(altLang, pMessage);
						if (pTrans) {
							// Found alternate translation
							StructFreeString(pMessage->pcDefaultString);
							pMessage->pcDefaultString = StructAllocString(pTrans->pcTranslatedString);
						}
						// Else use default... although might be better to chain to other languages at some point
					}
					pMessage->bFailedLocalTranslation = true;
				}
				pMessage->bLocallyTranslated = true;
			}

		} else if (s_bLoading && !IsClient()) {
			// Use stripped down messages when in loading mode
			stripDownMessage(pMessage);
		}
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_CHECK_REFERENCES:
		//can't do this when UGC is going on, even not on the edit server itself, 
		//it will assert whenever receiving a message from the resource DB
		if (!gConf.bUserContent &&IsServer() && isProductionMode() && !isProductionEditMode())
			RefSystem_LockDictionaryReferents(pDictName);
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}


AUTO_RUN;
void MessageDictionary_Register(void)
{
	MP_CREATE(Message,2000);

	gMessageDict = RefSystem_RegisterSelfDefiningDictionary("Message", false, parse_Message, true, false, NULL);

	// Create translation dictionaries based on applocale data
	//
	// Due to confusing startup-time issues, we can't trust locGetMaxLocaleCount here, so we just 
	//allocate LOCALE_ID_COUNT of them always, they're just pointers
	gTranslationDicts = calloc(sizeof(void *), LOCALE_ID_COUNT);

	resDictSetUseExtendedName(gMessageDict, true);
	resDictManageValidation(gMessageDict, fixupMessage);

	if (IsServer())
	{
		// Only maintain index on game servers in development mode
		if (IsGameServerBasedType() && isDevelopmentMode()) {
			resDictMaintainInfoIndex(gMessageDict, ".Name", ".Scope", NULL, NULL, NULL);
		}
		resDictProvideMissingResources(gMessageDict);

		resDictGetMissingResourceFromResourceDBIfPossible((void*)gMessageDict);
	} 
	else
	{
		// Use zero to keep all messages once downloaded
		resDictRequestMissingResources(gMessageDict, 100, false, resClientRequestSendReferentCommand);
	}
}


static void msgReloadCallback(const char *relpath, int when)
{
	if (resIsDictionaryEditMode(gMessageDict)) {
		// When in edit mode, all messages come only from server
		return;
	}
	loadstart_printf("Reloading Messages...");
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	s_bLoading = true;
	if(!ParserReloadFileToDictionaryWithFlags(relpath, gMessageDict, PARSER_OPTIONALFLAG))
	{
		ErrorFilenamef(relpath, "Error reloading message file: %s", relpath);
	}
	s_bLoading = false;

	loadend_printf("done");
}


static void langReloadTranslations(FolderCache* fc, FolderNode* node, int virtual_location, const char *pchRelPath, int when, void * localePointer)
{
	char buf[256];
	int locale = PTR_TO_S32(localePointer);
	const char *pcName = locGetName(locale);
	sprintf(buf, ".%s.translation", pcName);	

	if (strEndsWith(pchRelPath, ".bak"))
		return;

	loadstart_printf("Reloading %s translation...", pcName);
	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	StructReset(parse_TranslatedMessageDictionary, gTranslationDicts[locale]);

	ParserLoadFiles(s_pcAllMessageFolders, buf, NULL, PARSER_OPTIONALFLAG, parse_TranslatedMessageDictionary, gTranslationDicts[locale]);

	if (!IsLoginServer() && !IsAuctionServer() && !IsUGCSearchManager())
	{
		// Fake a resource modify callback
		CommandTranslationDictEventCallback(RESEVENT_RESOURCE_MODIFIED, NULL, "Command_Fake", NULL, S32_TO_PTR(locGetLanguage(locale)));
	}
	loadend_printf("Done.");
}


static void langLoadTranslations(LocaleID locale)
{

	char buf[256];
	char binName[256];
	const char *pcName = locGetName(locale);
	if(!pcName)
	{
		printf("A language translation was requested for locale %d, that is not supported.", locale);
		return;
	}

	sprintf(buf, ".%s.translation", pcName);	
	sprintf(binName, "Translation%s", pcName);

	//needed for crazy hacky loading of default strings from .translate files
	siLocaleCurrentlyLoadingTranslations = locale;

	if (!quickLoadMessages) 
	{
		char *pSharedMemoryName = NULL;
		MakeSharedMemoryName(binName, &pSharedMemoryName);
		strcat(binName, ".bin");

		if (!gTranslationDicts[locale])
			gTranslationDicts[locale] = StructCreate(parse_TranslatedMessageDictionary);

		loadstart_printf("Loading %s translations...", pcName);

		if (sbCheckForChangedDefaultStringsForTranslations)
		{
			ParserSetWantSpecialIgnoredFieldCallbacks(parse_TranslatedMessage, "DefaultString");
		}


		//if sbCheckForChangedDefaultStringsForTranslations is set then (a) we want to force reading from text files, because
		//that's where we'll actually get the changed default strings, and (b) we do NOT want to write bin files, because doing 
		//so will end up writing bin files with the wrong CRC. Fortunately we can accomplish both of those at once by
		//just not passing in a bin file name
		ParserLoadFilesShared(pSharedMemoryName, s_pcAllMessageFolders, buf, 
			
			sbCheckForChangedDefaultStringsForTranslations ? NULL : binName, 
			PARSER_OPTIONALFLAG , 
			parse_TranslatedMessageDictionary, gTranslationDicts[locale]);

		

		loadend_printf("done (%d loaded)", eaSize(&gTranslationDicts[locale]->ppTranslatedMessages));

		estrDestroy(&pSharedMemoryName);

		if(isDevelopmentMode())
		{
			int i;
			char **ppFileSpecs = NULL;

			MakeFileSpecFromDirFilename(s_pcAllMessageFolders, buf, &ppFileSpecs);
			for (i = 0; i < eaSize(&ppFileSpecs); i++)
			{
				FolderCacheSetCallbackEx(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, ppFileSpecs[i], langReloadTranslations, S32_TO_PTR(locale));
			}
			eaDestroyEx(&ppFileSpecs, NULL);

			// Verify all translations loaded.
			if (locale != LOCALE_ID_ENGLISH && s_bVerifyAllLoadedMessagesAreTranslated)
			{
				// For every message in English, make sure there's a message in the current language.
				ResourceIterator iter;
				Message *pMessage;
				Language langID = locGetLanguage(locale);
				TranslatedMessage *pTrans;

				resInitIterator(gMessageDict, &iter);
				while (resIteratorGetNext(&iter, NULL, &pMessage))
				{	
					const char *pErr = NULL;
					if (pMessage->bDoNotTranslate)
						continue;
					
					pTrans = langFindTranslatedMessage(langID, pMessage);

					if ( (pMessage->bLocallyTranslated && pMessage->bFailedLocalTranslation) || (!pTrans) )
					{
						// Print an error
						Errorf("Untranslated message for lang=%s key=\"%s\" default=\"%s\"", locGetName(locale), pMessage->pcMessageKey, pMessage->pcDefaultString);
						continue;
					}

					// Will error if the .po file was saved in the wrong encoding 
					// somewhere along the way.
					// You can fix it in EditPlus by:
					//   1) Open the .po file
					//   2)	File->"Save As..." and choose the Encoding "ANSI".
					//   3)	Close the file.
					//   4)	File->Open and choose the Encoding "UTF-8".
					//   5)	File->"Save As..." and choose the Encoding "UTF-8".
					//   6) Re-run POToTranslate
					if (pTrans && pTrans->pcTranslatedString[0] == '\xc3')
					{
						Errorf("Possible bad encoding in translated string for lang=%s key=\"%s\" default=\"%s\"", locGetName(locale), pMessage->pcMessageKey, pMessage->pcDefaultString);
					}
				}
				resFreeIterator(&iter);
			}
		}
	}
}

AUTO_STARTUP(AS_Messages);
int msgLoadAllMessages(void)
{
	static int loadedOnce = false;

	int result=PARSERESULT_SUCCESS;
	int i;
	int numLocales;
	char buf[256];
	char buf2[256];

	if (loadedOnce)
	{
		return 1;
	}

	// Most messages load from server, but we need some loaded before we connect to a server
	// They are loaded here.

	loadstart_printf("Loading Messages...");

	// Warn if -quickLoadMessages was specified with -locale.
	if (quickLoadMessages && getCurrentLocale() != DEFAULT_LOCALE_ID)
		Errorf("-quickLoadMessages overrides non-default locale setting \"%s\".", locGetName(getCurrentLocale()));

	if (quickLoadMessages)
		RefSystem_SetDictionaryIgnoreNullReferences(gMessageDict, true);

	if (IsGameServerBasedType() || IsLoginServer() || IsChatServer() || IsTicketTracker() || IsAuctionServer() || IsUGCSearchManager() || IsGuildServer() || IsGatewayServer() || IsGatewayLoginLauncher())
	{
		numLocales = locGetMaxLocaleCount();

		// Load translation files
		// Only do this in production OR if shared memory enabled in dev mode OR Making bins OR if using fake translations
		// Otherwise translation dictionaries are loaded on demand on the first request to translate
		if (isProductionMode() || forceLoadAllTranslations() || (sharedMemoryGetMode() == SMM_ENABLED) || gbMakeBinsAndExit || s_bAddFakeTranslations) {
			for(i=1; i<numLocales; ++i) {
				if (!locIsImplemented(i)) {
					continue;
				}
				langLoadTranslations(i);
			}
		}

		if (!quickLoadMessages) {
			// These two loads should be mutually exclusive, except when making bins and exiting
			if (!IsGameServerBasedType() || !isDevelopmentMode()  || gbMakeBinsAndExit || giMakeOneLocaleOPFilesAndExit) {
				// When not in dev mode, or when not on a game server, load stripped down messages
				// by setting s_bLoading to true during the read
				s_bLoading = true;
				resLoadResourcesFromDisk(gMessageDict, s_pcAllMessageFolders, ".ms", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | RESOURCELOAD_USERDATA);
				s_bLoading = false;
			}
			if (gbMakeBinsAndExit || giMakeOneLocaleOPFilesAndExit) {
				// Empty message dictionary between the two binning attempts
				loadstart_printf("Clearing ref dictionary between binnings...");
				RefSystem_ClearDictionary(gMessageDict, false);
				loadend_printf("Done");
			}
			if ((IsGameServerBasedType() && isDevelopmentMode())  || gbMakeBinsAndExit || giMakeOneLocaleOPFilesAndExit) {
				// In game server dev mode load with "s_bLoading = false" so you get full messages usable for editing
				s_bLoading = false;
				resLoadResourcesFromDisk(gMessageDict, s_pcAllMessageFolders, ".ms", "Message-Dev.bin", RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | RESOURCELOAD_USERDATA);
			}
		}
		resDictRegisterEventCallback(gMessageDict, CommandTranslationDictEventCallback, S32_TO_PTR(0));


		// On the server fill in fake translations as needed
		// Don't do language 0, which is English
		if (isDevelopmentMode() && s_bAddFakeTranslations) 
		{
			ResourceIterator iter;
			Message *pMessage;

			loadstart_printf("Adding Fake Translations...");

			resInitIterator(gMessageDict, &iter);
			while (resIteratorGetNext(&iter, NULL, &pMessage))
			{			
				for(i=locGetMaxLocaleCount()-1; i>=1; --i) {
					if (!locIsImplemented(i)) {
						continue;
					}
					populateFakeTranslation(pMessage, i);
				}
			}
			resFreeIterator(&iter);

			loadend_printf(" done.");
		}

		for(i=0; i<numLocales; ++i) 
		{
			if (!gTranslationDicts[i] || IsLoginServer() || IsAuctionServer() || IsUGCSearchManager()) 
			{
				continue;
			}
			PERFINFO_AUTO_START("GenerateMapForLanguage", 1);
			CmdListGenerateMapForLanguage(&gGlobalCmdList, locGetLanguage(i));
			PERFINFO_AUTO_STOP();
		}

	}
	else if (IsClient())
	{
		const char *pcName;

		//on the client, when we're writing .po files, we hacked parse_Message, so we need to force reloading from text files
		int iForceReloadFlag = giMakeOneLocaleOPFilesAndExit ? (PARSER_FORCEREBUILD | PARSER_DONTREBUILD) : 0;

		s_bLoading = true;

		if (gbMakeBinsAndExit && gpcMakeBinsAndExitNamespace == NULL) {
			// Need to make bins for all locales
			loadstart_printf("Making multi-locale client bins");

			// Count in reverse order so we end with English being active
			numLocales = locGetMaxLocaleCount();
			for(i=numLocales-1; i>=0; --i) 
			{
				char fullFileName[CRYPTIC_MAX_PATH];
				DependencyList dependencyList = NULL;

				if (!locIsImplemented(i)) {
					continue;
				}

				pcName = locGetName(i);
				s_makeBinsLoc = i;

				// Empty message dictionary of previous locales
				RefSystem_ClearDictionary(gMessageDict, false);

				// Load translation files.  Do not make bins for them since the bins are unnecessary.
				sprintf(buf, ".%s.translation", pcName);

				if (!gTranslationDicts[i])
					gTranslationDicts[i] = StructCreate(parse_TranslatedMessageDictionary);

				loadstart_printf("Loading %s translations...", pcName);

				ParserLoadFiles(s_pcClientTranslationFolders, buf, NULL, iForceReloadFlag | PARSER_OPTIONALFLAG, parse_TranslatedMessageDictionary, gTranslationDicts[i]);

				loadend_printf("done (%d loaded)", eaSize(&gTranslationDicts[i]->ppTranslatedMessages));

				sprintf(fullFileName, "messages/ClientMessages%s", buf);

	
				// Load messages... which will cause local translation
				// Note: "maps" folder is scanned, but only messages in ".zone.ms" files are kept by fixup function
				sprintf(buf, "ClientMessages%s.bin", pcName);

				//the translated bin file depends on the clientMessages, so add that dependency
				DependencyListCreate(&dependencyList);
				DependencyListInsert(&dependencyList, DEPTYPE_FILE, fullFileName , 0);

				result = ParserLoadFilesToDictionaryEx(s_pcClientMessageFolders, ".ms", buf, iForceReloadFlag | PARSER_OPTIONALFLAG, gMessageDict, dependencyList);

				DependencyListDestroy(&dependencyList);
			}

			s_makeBinsLoc = 0;
			loadend_printf("done");
		} else {
			// This path is for normal client behavior
			pcName = locGetName(getCurrentLocale());

			// Load Translation for current locale only if in development mode
			// In production mode, the client messages bin will already be translated
			if (isDevelopmentMode()) {
				sprintf(buf, "ClientTranslation%s.bin", pcName);
				sprintf(buf2, ".%s.translation", pcName);
				if (!quickLoadMessages)
				{
					if (!gTranslationDicts[getCurrentLocale()])
						gTranslationDicts[getCurrentLocale()] = StructCreate(parse_TranslatedMessageDictionary);

					loadstart_printf("Loading %s translations...", pcName);

					result = ParserLoadFiles(s_pcClientTranslationFolders, buf2, buf, iForceReloadFlag | PARSER_OPTIONALFLAG, parse_TranslatedMessageDictionary, gTranslationDicts[getCurrentLocale()]);

					loadend_printf("done (%d loaded)", eaSize(&gTranslationDicts[getCurrentLocale()]->ppTranslatedMessages));
				}					
			}

			// Load Messages for current locale
			sprintf(buf, "ClientMessages%s.bin", pcName);
			if (!quickLoadMessages)
				result = ParserLoadFilesToDictionary(s_pcClientMessageFolders, ".ms", buf, iForceReloadFlag | PARSER_OPTIONALFLAG, gMessageDict);
			resDictRegisterEventCallback(gMessageDict, CommandTranslationDictEventCallback, S32_TO_PTR(0));

			// In development mode, listen for changes to the message files and reload as appropriate
			if(isDevelopmentMode()) 
			{
				char **ppFileSpecs = NULL;

				MakeFileSpecFromDirFilename(s_pcClientTranslationFolders, buf2, &ppFileSpecs);
				for (i = 0; i < eaSize(&ppFileSpecs); i++)
				{
					FolderCacheSetCallbackEx(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, ppFileSpecs[i], langReloadTranslations, S32_TO_PTR(getCurrentLocale()));
				}
				eaDestroyEx(&ppFileSpecs, NULL);

				FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "ui/*.ms", msgReloadCallback);
				FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "messages/*.ms", msgReloadCallback);
				FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "maps/*.ms", msgReloadCallback);
				FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "defs/costumes/definitions/*.ms", msgReloadCallback);
				FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "defs/species/*.ms", msgReloadCallback);
				FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "defs/classes/*.ms", msgReloadCallback);
				FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "defs/config/*.ms", msgReloadCallback);
				FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "genesis/UGCInfo_SeriesEditor/*.ms", msgReloadCallback);
			}
		}

		CmdListGenerateMapForLanguage(&gGlobalCmdList, locGetLanguage(getCurrentLocale()));

		s_bLoading = false;
	}

	loadedOnce = true;

	// If shared memory is on, this frees a bunch of memory
	mpCompactPool(memPoolMessage);

	if (giMakeOneLocaleOPFilesAndExit)
	{
		printf("We have %d languages to export PO files for...\n", ea32Size(&giAllLocalesToMakeAndExit));

		for (i = 0; i < ea32Size(&giAllLocalesToMakeAndExit); i++)
		{
			char *pResult;
			
			printf("Going to try to export PO files for %s\n", locGetName(giAllLocalesToMakeAndExit[i]));
			SendStringToCB(CBSTRING_COMMENT, "Exporting PO files for %s\n", locGetName(giAllLocalesToMakeAndExit[i]));
			pResult = msgExportPOFiles(giAllLocalesToMakeAndExit[i]);
			assertmsgf(!pResult, "While exporting PO files for %s, got error: %s", locGetName(giAllLocalesToMakeAndExit[i]), pResult);
			
		}

		exit(0);
	}

	loadend_printf("done (%d messages)", RefSystem_GetDictionaryNumberOfReferents(gMessageDict));
	return 1;
}

static void msgEncodeKeySuffix(SA_PARAM_NN_STR char *pchEncodedResult, U32 iNumber) {
	U32 iBase = (U32) strlen(s_baseEncodeChars);

	// This generates the number using base-iBase in reverse order
	while (iNumber) {
		U32 iDigit = iNumber % iBase;
		const unsigned char c = s_baseEncodeChars[iDigit];
		*pchEncodedResult = c;
		pchEncodedResult++;
		iNumber /= iBase;
	}

	// Terminate & reverse the string
	*pchEncodedResult = '\0';
#ifndef _PS3
	_strrev(pchEncodedResult);
#endif
}

bool msgCharIsEncoded(const char c)
{
	return (bool)strchr(s_baseEncodeChars, tolower(c));
}

// Construct a terse message key.  The key returned will have the given
// prefix followed by an encoding (currently CRC) of the message key seed.
// To avoid duplicate prefix usage, please use one of the prefixes defined
// in MSGKEY_PREFIX_* or create your own MSGKEY_PREFIX_* definition.
SA_RET_NN_STR const char *msgCreateTerseKey(SA_PARAM_NN_STR const char *pcPrefix, SA_PARAM_NN_STR const char *pcMessageKeySeed) {
	char buf[1024];
	char suffix[1024];
	const char *pchKey;
	U32 iCrc;

	#if !_PS3
	if (!devassertmsgf(pcMessageKeySeed && *pcMessageKeySeed, "Null or empty message key seed used with msgCreateTerseKey()!")) {
		return "";
	}
	if (!devassertmsgf(pcPrefix && *pcPrefix, "Null or empty prefix used with msgCreateTerseKey()!")) {
		return pcMessageKeySeed;
	}
	#endif //_PS3

	cryptAdler32Init();
	cryptAdler32Update(pcMessageKeySeed, (int) strlen(pcMessageKeySeed));
	iCrc = cryptAdler32Final();

	msgEncodeKeySuffix(suffix, iCrc);
	sprintf(buf, "%s.%s", pcPrefix, suffix);
	pchKey = allocAddString(buf);

	return pchKey;
}

// Construct a terse message key that is unique.  The key returned will have the given
// prefix followed by an encoding (currently CRC) of the message key seed.
// To avoid duplicate prefix usage, please use one of the prefixes defined
// in MSGKEY_PREFIX_* or create your own MSGKEY_PREFIX_* definition.
//
// If there is a message key conflict (i.e. a generated key is already in
// use), then pchOriginalKey is checked against the generated key.  If they
// are the same, then the original key is returned since it is already in a 
// terse form and shouldn't change unless the message key seed is actually 
// different.  If pchOriginalKey is null/empty, then the CRC is incremented
// and the next generated key is tested.  Repeat until we have no conflict
// or have a match with pchOriginalKey.
const char *msgCreateUniqueKey(const char *pcPrefix, const char *pcMessageKeySeed, const char *pchOriginalKey) {
	char buf[1024];
	char suffix[1024];
	const char *pchKey;
	char ns[RESOURCE_NAME_MAX_SIZE], base[RESOURCE_NAME_MAX_SIZE];
	U32 iCrc;

	#if !_PS3
	if (!devassertmsgf(pcMessageKeySeed && *pcMessageKeySeed, "Null or empty message key seed used with msgCreateUniqueKey()!")) {
		return "";
	}
	if (!devassertmsgf(pcPrefix && *pcPrefix, "Null or empty prefix used with msgCreateUniqueKey()!")) {
		return pcMessageKeySeed;
	}
	#endif //_PS3

	cryptAdler32Init();
	cryptAdler32Update(pcMessageKeySeed, (int) strlen(pcMessageKeySeed));
	iCrc = cryptAdler32Final();

	while (1) {
		bool bMessageExists;

		msgEncodeKeySuffix(suffix, iCrc);
		if (resExtractNameSpace(pcMessageKeySeed, ns, base))
			sprintf(buf, "%s:%s.%s", ns, pcPrefix, suffix);
		else
			sprintf(buf, "%s.%s", pcPrefix, suffix);

		bMessageExists = msgExists(buf);
		if (bMessageExists && stricmp_safe(buf, pchOriginalKey) == 0) {
			// We found an existing message with the same key and
			// it's for the same message object, so use the current key.
			break;
		} else if (bMessageExists) {
			// The message is not unique, bump up the CRC and try again
			iCrc++;
		} else {
			// The message key is unique, use it.
			break;
		}
	}

	pchKey = allocAddString(buf);

	return pchKey;
}

// ----------------------------------------------------------------------------
// Message HTTP Stuff
// ----------------------------------------------------------------------------


AUTO_STRUCT;
typedef struct MessageHttpWrapper
{
	char *pMessageKey;
	Language iLanguage;
	const char *pTranslated;
} MessageHttpWrapper;

AUTO_STRUCT;
typedef struct SingleMessageNameValuePair
{
	const char *pName; AST(KEY, POOL_STRING)
		char *pValue; 
} SingleMessageNameValuePair;

AUTO_STRUCT;
typedef struct AllMessagesList
{
	SingleMessageNameValuePair **ppMessages;
} AllMessagesList;


bool ProcessMessageIntoStructInfoForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	char *pFirstBracket;
	Language langID;
	const char *pTranslatedString;
	MessageHttpWrapper wrapper;

	if (pLocalXPath[0] != '[')
	{
		AllMessagesList *pList;
		RefDictIterator iterator;
		Message *pMessage;
		bool bRetVal;

		langID = StaticDefineIntGetInt(LanguageEnum, pLocalXPath + 1);

		if (langID == -1)	
		{
			GetMessageForHttpXpath("Error - expected [ or .Language (format should be .message[messagekey].language or message.language)", pStructInfo, true);
			return true;
		}

		pList = StructCreate(parse_AllMessagesList);

		RefSystem_InitRefDictIterator(gMessageDict, &iterator);
		
		while ((pMessage = RefSystem_GetNextReferentFromIterator(&iterator)))
		{
			SingleMessageNameValuePair *pPair = StructCreate(parse_SingleMessageNameValuePair);
			pPair->pName = pMessage->pcMessageKey;
			pPair->pValue = strdup(langTranslateMessageKey(langID, pPair->pName));

			eaPush(&pList->ppMessages, pPair);
		}

		bRetVal = ProcessStructIntoStructInfoForHttp("", pArgList,
			pList, parse_AllMessagesList, iAccessLevel, 0, pStructInfo, eFlags);

		StructDestroy(parse_AllMessagesList, pList);

		return bRetVal;


	}

	pFirstBracket = strchr(pLocalXPath, ']');
	if (!pFirstBracket)
	{
		GetMessageForHttpXpath("Error - expected ] (format should be .message[messagekey].language)", pStructInfo, true);
		return true;
	}

	if (*(pFirstBracket + 1) != '.')
	{
		GetMessageForHttpXpath("Error - expected . after ] (format should be .message[messagekey].language)", pStructInfo, true);
		return true;
	}


	langID = StaticDefineIntGetInt(LanguageEnum, pFirstBracket + 2);
	if (langID == -1)
	{
		GetMessageForHttpXpath(STACK_SPRINTF("Error - didn't recognize language %s (format should be .message[messagekey].language)" , pFirstBracket + 2), pStructInfo, true);
		return true;
	}

	*pFirstBracket = 0;
	pTranslatedString = langTranslateMessageKey(langID, pLocalXPath + 1);

	if (!pTranslatedString)
	{
		GetMessageForHttpXpath(STACK_SPRINTF("Error - didn't recognize message key %s (format should be .message[messagekey].language)" , pLocalXPath + 1), pStructInfo, true);
		*pFirstBracket = ']';
		return true;
	}

	wrapper.pMessageKey = pFirstBracket + 2;
	wrapper.iLanguage = langID;
	wrapper.pTranslated = pTranslatedString;

	*pFirstBracket = ']';

	return ProcessStructIntoStructInfoForHttp("", pArgList,
		&wrapper, parse_MessageHttpWrapper, iAccessLevel, 0, pStructInfo, eFlags);
}

const char *msgGetFilename(Message *pMessage)
{
	return pMessage ? pMessage->pcFilename : NULL;
}

const char *msgSetFilename(Message *pMessage, const char *pcFilename)
{
	if (pMessage)
	{
		pMessage->pcFilename = allocAddCaseSensitiveString(pcFilename);
		return pMessage->pcFilename;
	}
	else
		return NULL;
}

const char *msgGetScope(Message *pMessage)
{
	return pMessage ? pMessage->pcScope : NULL;
}

const char *msgSetScope(Message *pMessage, const char *pcScope)
{
	if (pMessage)
	{
		pMessage->pcScope = allocAddString(pcScope);
		return pMessage->pcScope;
	}
	else
		return NULL;
}

const char *msgGetDescription(Message *pMessage)
{
	return pMessage ? pMessage->pcDescription : NULL;
}

const char *msgSetDescription(Message *pMessage, const char *pcDescription)
{
	if (pMessage)
	{
		char *pcNewDescription = StructAllocString(pcDescription);
		if (pMessage->pcDescription)
			free(pMessage->pcDescription);
		pMessage->pcDescription = pcNewDescription;
		return pMessage->pcDescription;
	}
	else
		return NULL;
}

bool msgGetDoNotTranslate(Message *pMessage)
{
	return pMessage ? pMessage->bDoNotTranslate : false;
}

void msgSetDoNotTranslate(Message *pMessage, bool bDoNotTranslate)
{
	if (pMessage)
		pMessage->bDoNotTranslate = bDoNotTranslate;
}


bool msgGetFinal(Message *pMessage)
{
	return pMessage ? pMessage->bFinal : false;
}

void msgSetFinal(Message *pMessage, bool bFinal)
{
	if (pMessage)
		pMessage->bFinal = bFinal;
}

bool msgGetLocallyTranslated(Message *pMessage)
{
	return pMessage ? pMessage->bLocallyTranslated : false;
}

void msgSetLocallyTranslated(Message *pMessage, bool bLocallyTranslated)
{
	if (pMessage)
		pMessage->bLocallyTranslated = bLocallyTranslated;
}

AUTO_RUN;
void initMessageHTTP(void)
{
	RegisterCustomXPathDomain(".message", ProcessMessageIntoStructInfoForHttp, NULL);
}

AUTO_COMMAND;
void LoadAllTranslations(void)
{
	langLoadTranslations(LOCALE_ID_GERMAN);
	langLoadTranslations(LOCALE_ID_FRENCH);
}

static StashTable sOldDefaultStrings[LOCALE_ID_COUNT] = {0};

static void SetDefaultStringFromTranslationHasChanged(int iLocale, const char *pMessageKey, const char *pOldDefaultString)
{
	assert(iLocale >= 0 && iLocale < LOCALE_ID_COUNT);
	if (!sOldDefaultStrings[iLocale])
	{
		sOldDefaultStrings[iLocale] = stashTableCreateAddress(16);
	}

	stashAddPointer(sOldDefaultStrings[iLocale], pMessageKey, strdup(pOldDefaultString), true);
}

static char *GetOldDefaultString(int iLocale, const char *pMessageKey)
{
	char *pRetVal = NULL;
	assert(iLocale >= 0 && iLocale < LOCALE_ID_COUNT);
	stashFindPointer(sOldDefaultStrings[iLocale], pMessageKey, &pRetVal);
	return pRetVal;
}

//when exporting .pot and .po files, we group together all messages that have the same description and default string
AUTO_STRUCT;
typedef struct MessageGroupForExport
{
	char *pCombinedString;
	const char **ppMessageKeys; AST(POOL_STRING)
} MessageGroupForExport;

StashTable sMessageGroupsByCombinedString = NULL;


static char *spPOHeaderTemplate[] = {
"# Translations template for {Product}_class.",
"# Copyright (C) 2011 ORGANIZATION",
"# This file is distributed under the same license as the {Product}_class",
"# project.",
"# FIRST AUTHOR <EMAIL@ADDRESS>, 2011.",
"#",
"#, fuzzy",
"msgid \"\"",
"msgstr \"\"",
"\"Project-Id-Version: startrek_class VERSION\\n\"",
"\"Report-Msgid-Bugs-To: EMAIL@ADDRESS\\n\"",
"\"POT-Creation-Date: {date}\\n\"", //2011-10-13 14:55-0700
"\"PO-Revision-Date: YEAR-MO-DA HO:MI+ZONE\\n\"",
"\"Last-Translator: FULL NAME <EMAIL@ADDRESS>\\n\"",
"\"Language-Team: LANGUAGE <LL@li.org>\\n\"",
"\"MIME-Version: 1.0\\n\"",
"\"Content-Type: text/plain; charset=utf-8\\n\"",
"\"Content-Transfer-Encoding: 8bit\\n\"",
"\"Generated-By: Babel None\\n\"",
"",
};

char *GetPOHeader(void)
{
	static char *spRetVal = NULL;
	int i;
	char *pDateString = NULL;

	estrClear(&spRetVal);
	for (i = 0; i < ARRAY_SIZE(spPOHeaderTemplate); i++)
	{
		estrConcatf(&spRetVal, "%s\n", spPOHeaderTemplate[i]);
	}

	estrReplaceOccurrences(&spRetVal, "{Product}", GetProductName());

	estrCopy2(&pDateString, timeGetDateStringFromSecondsSince2000(timeSecondsSince2000()));
	
	//chop off seconds
	estrSetSize(&pDateString, estrLength(&pDateString)-3);
	estrConcatf(&pDateString, "+0000");

	estrReplaceOccurrences(&spRetVal, "{date}", pDateString);
	estrDestroy(&pDateString);

	return spRetVal;
}

void AppendWithCEscaping(char **ppOutString, const char *pInString)
{
	while (*pInString)
	{
		switch (*pInString)
		{
		case '\n':
			estrConcatf(ppOutString, "\\n");
			break;
		case '\r':
			estrConcatf(ppOutString, "\\r");
			break;
		case '"':
			estrConcatf(ppOutString, "\\\"");
			break;
		case '\\':
			estrConcatf(ppOutString, "\\\\");
			break;
		default:
			estrConcatChar(ppOutString, *pInString);
		}

		pInString++;
	}
}

char *GetPOFileRootName(int iLocale)
{
	static char *spRetVal = NULL;
	estrPrintf(&spRetVal, "%s/translations/%s_%s", fileDataDir(), locGetName(iLocale), GlobalTypeToName(GetAppGlobalType()));
	return spRetVal;
}

static int countWords(char *pStr)
{
	int iCount = 0;

	if (!pStr || !pStr[0])
	{
		return 0;
	}

	while (1)
	{
		while (*pStr && IS_WHITESPACE(*pStr))
		{
			pStr++;
		}

		if (!(*pStr))
		{
			return iCount;
		}

		iCount++;
		while (*pStr && !IS_WHITESPACE(*pStr))
		{
			pStr++;
		}
	}
}

int SortMessageGroup(const MessageGroupForExport **left, const MessageGroupForExport **right)
{
	eaQSort((*right)->ppMessageKeys, strCmp);
	eaQSort((*left)->ppMessageKeys, strCmp);

	return stricmp((*right)->ppMessageKeys[0], (*left)->ppMessageKeys[0]);
}

const char * s_pSpecialMessageKeys[] =
{
	//"Attrib_Description_Pet_Stat_Crit",
	//"Staticdefine_*",
	""
};

bool MessageMatchesSpecialExport(Message *pMessage, const char *pCategoryName)
{
	int i;
	if( stricmp(pCategoryName, "Priority") )
		return false;

	for(i=0; i<ARRAY_SIZE(s_pSpecialMessageKeys); ++i)
	{
		if( isWildcardMatch(s_pSpecialMessageKeys[i], pMessage->pcMessageKey, false, true) )
			return true;
	}

	return false;
}

bool MessageMatchesCategory(Message *pMessage, MessageCategoryForLocalization *pCategory, const char *pCategoryName)
{
	int i;

	if( MessageMatchesSpecialExport(pMessage, pCategoryName) )
	{
		return true;
	}
	else if( !stricmp(pCategoryName, "Priority") )
	{
		return false;
	}
	
	if (!pCategory)
	{
		return !pMessage->bMatchedACategory;
	}
	
	//if we already were in a category, then we can't be in another one... this lets multiple categories
	//in the category file to work properly, with earlier ones overriding later ones
	if (pCategory && pMessage->bMatchedACategory)
	{
		return false;
	}

	for (i = 0; i < eaSize(&pCategory->ppFileNameSubStrings); i++)
	{
		if (strstri(pMessage->pcFilename, pCategory->ppFileNameSubStrings[i]))
		{
			return true;
		}
	}

	return false;
}

//returns an error string or NULL
char *ExportOneSetOfPOFiles(MessageGroupForExport **ppAllMessageGroups,
	MessageCategoryForLocalization *pCategory, char *pCategoryName, int iLocale, Language iLangID)
{

	FILE *pOutFile_All = NULL;
	FILE *pOutFile_Untranslated = NULL;
	FILE *pOutFile_TransChanged = NULL;
	FILE *pOutFile_Dupes = NULL;
	char outFileName[CRYPTIC_MAX_PATH];
	char *pPOHeader = GetPOHeader();
	char *pCurOutString = NULL;
	Message *pMessage = NULL;
	char *pTempString = NULL;
	int iWordCount_All = 0;
	int iWordCount_Untranslated = 0;
	int iWordCount_TransChanged = 0;
	int iWordCount_Dupes = 0;
	bool bFoundAlternateTrans = false;

	if( !pCategoryName )
		return "NULL pCategoryName";

	sprintf(outFileName, "%s_All/%s.po", GetPOFileRootName(iLocale), pCategoryName);
	mkdirtree_const(outFileName);
	pOutFile_All = fopen(outFileName, "wt");
	if (!pOutFile_All)
	{
		return "Couldn't create out file";
	}

	sprintf(outFileName, "%s_Untranslated/%s.po", GetPOFileRootName(iLocale), pCategoryName);
	mkdirtree_const(outFileName);
	pOutFile_Untranslated = fopen(outFileName, "wt");
	if (!pOutFile_Untranslated)
	{
		fclose(pOutFile_All);
		return "Couldn't create out file";
	}

	sprintf(outFileName, "%s_TransChanged/%s.po", GetPOFileRootName(iLocale), pCategoryName);
	mkdirtree_const(outFileName);
	pOutFile_TransChanged = fopen(outFileName, "wt");
	if (!pOutFile_TransChanged)
	{
		fclose(pOutFile_All);
		fclose(pOutFile_Untranslated);
		return "Couldn't create out file";
	}

	// Only build the _Dupes .po files on the GameServer, since
	// that'll include all client messages too.
	// And only for the "Everything" category, so we only have 
	// to run one file through POToTranslate.
	if( IsGameServerBasedType() && !stricmp(pCategoryName, "Everything") )
	{
		sprintf(outFileName, "%s_Dupes/%s.po", GetPOFileRootName(iLocale), pCategoryName);
		mkdirtree_const(outFileName);
		pOutFile_Dupes = fopen(outFileName, "wt");
		if (!pOutFile_Dupes)
		{
			fclose(pOutFile_All);
			fclose(pOutFile_Untranslated);
			fclose(pOutFile_TransChanged);
			return "Couldn't create out file";
		}
	}

	fprintf(pOutFile_All, "%s", pPOHeader);
	fprintf(pOutFile_Untranslated, "%s", pPOHeader);
	fprintf(pOutFile_TransChanged, "%s", pPOHeader);
	if(pOutFile_Dupes) fprintf(pOutFile_Dupes, "%s", pPOHeader);


	FOR_EACH_IN_EARRAY(ppAllMessageGroups, MessageGroupForExport, pMessageGroup)
	{
		TranslatedMessage *pTranslated;
		char fileName[CRYPTIC_MAX_PATH] = "";
		int i;
		const char **ppScopes = NULL;
		const char **ppTranslatedStrings = NULL;
		const char **ppKeys = NULL;
		char *pDescription = NULL;
		bool bTransChanged = false;
		bool bAtLeastOneRealScope = false;
		int iWordCount;
		bool bAtLeastOneMatchesCategory = false;
		bool bAtLeastOneUntranslatedKey = false;
	
		estrClear(&pCurOutString);

		for(i = 0; i < eaSize(&pMessageGroup->ppMessageKeys); i++)
		{
			pMessage = RefSystem_ReferentFromString(gMessageDict, pMessageGroup->ppMessageKeys[i]);
			
			if (MessageMatchesCategory(pMessage, pCategory, pCategoryName))
			{
				bAtLeastOneMatchesCategory = true;
				if (pCategory)
				{
					pMessage->bMatchedACategory = true;
				}
				pMessage->bMatchedThisCategory = true;
			}
			else
			{
				pMessage->bMatchedThisCategory = false;
				continue;
			}

			pTranslated = langFindTranslatedMessage(iLangID, pMessage);

			if (pMessage->pcScope && pMessage->pcScope[0])
			{
				eaPush(&ppScopes, pMessage->pcScope);	
				bAtLeastOneRealScope = true;
			}
			else
			{
				eaPush(&ppScopes, "");
			}

			eaPush(&ppKeys, pMessage->pcMessageKey);

			if (!pDescription && pMessage->pcDescription && pMessage->pcDescription[0])
			{
				pDescription = pMessage->pcDescription;
			}

			// If this key has a translation that's not blank, add the translation to ppTranslatedStrings
			if (pTranslated && pTranslated->pcTranslatedString && pTranslated->pcTranslatedString[0])
			{
				if (eaFindString(&ppTranslatedStrings, pTranslated->pcTranslatedString) == -1)
				{
					eaPush(&ppTranslatedStrings, pTranslated->pcTranslatedString);
				}
			}
			else
			{
				bAtLeastOneUntranslatedKey = true;
			}

			if (GetOldDefaultString(iLocale, pMessage->pcMessageKey))
			{
				bTransChanged = true;
			}
		}

		if (bAtLeastOneMatchesCategory)
		{

			if (pDescription)
			{
				estrClear(&pTempString);
				AppendWithCEscaping(&pTempString,pDescription);
				estrConcatf(&pCurOutString, "#. description=%s\n", pTempString);
			}

			if (bAtLeastOneRealScope)
			{
				for (i = 0; i < eaSize(&ppScopes); i++)
				{
					estrConcatf(&pCurOutString, "#. scope=%s\n", ppScopes[i]);
				}
			}

			for (i = 0; i < eaSize(&ppKeys); i++)
			{
				estrConcatf(&pCurOutString, "#. key=%s\n", ppKeys[i]);
			}

			for(i = 0; i < eaSize(&pMessageGroup->ppMessageKeys); i++)
			{
				pMessage = RefSystem_ReferentFromString(gMessageDict, pMessageGroup->ppMessageKeys[i]);

				if (pMessage->bMatchedThisCategory)
				{
					fileLocateWrite(pMessage->pcFilename, fileName);
					estrConcatf(&pCurOutString, "#: %s:%d\n", fileName, pMessage->iLineNum);
				}
			}

			if (eaSize(&ppTranslatedStrings) > 1)
			{
				bFoundAlternateTrans = true;
				for (i = 1; i < eaSize(&ppTranslatedStrings); i++)
				{
					estrClear(&pTempString);
					AppendWithCEscaping(&pTempString,ppTranslatedStrings[i]);
					estrConcatf(&pCurOutString, "#. alternateTrans=\"%s\"\n", pTempString);
				}
			}

			estrClear(&pTempString);
			AppendWithCEscaping(&pTempString, pDescription ? pDescription : "");
			estrConcatf(&pCurOutString, "msgctxt \"%s\"\n", pTempString);
	
			estrClear(&pTempString);
			AppendWithCEscaping(&pTempString, pMessage->pcDefaultString);
			estrConcatf(&pCurOutString, "msgid \"%s\"\n", pTempString);

			iWordCount = countWords(pMessage->pcDefaultString);

			if (eaSize(&ppTranslatedStrings))
			{
				estrClear(&pTempString);
				AppendWithCEscaping(&pTempString,ppTranslatedStrings[0]);
				estrConcatf(&pCurOutString, "msgstr \"%s\"\n", pTempString);
			
			}
			else
			{
				estrConcatf(&pCurOutString, "msgstr \"\"\n");
			}

			estrConcatf(&pCurOutString, "\n");

			fprintf(pOutFile_All, "%s", pCurOutString);
			iWordCount_All += iWordCount;

			if (!eaSize(&ppTranslatedStrings))
			{
				fprintf(pOutFile_Untranslated, "%s", pCurOutString);
				iWordCount_Untranslated += iWordCount;
			}
			else if( bAtLeastOneUntranslatedKey && pOutFile_Dupes )
			{
				// This messagegroup has already been translated.
				fprintf(pOutFile_Dupes, "%s", pCurOutString);
				iWordCount_Dupes += iWordCount;
			}

			if (bTransChanged)
			{
				fprintf(pOutFile_TransChanged, "%s", pCurOutString);
				iWordCount_TransChanged += iWordCount;
			}
		}

		eaDestroy(&ppKeys);
		eaDestroy(&ppScopes);
		eaDestroy(&ppTranslatedStrings);
	}
	FOR_EACH_END;

	fprintf(pOutFile_All, "\n\n# Wordcount: %d\n", iWordCount_All);
	fprintf(pOutFile_Untranslated, "\n\n# Wordcount: %d\n", iWordCount_Untranslated);
	fprintf(pOutFile_TransChanged, "\n\n# Wordcount: %d\n", iWordCount_TransChanged);
	if(pOutFile_Dupes) fprintf(pOutFile_Dupes, "\n\n# Wordcount: %d\n", iWordCount_Dupes);

	// Fire a run-time error so the producer in charge of translations knows to process the dupes folder now.
	if( iWordCount_Dupes > 0 )
	{
		char tempFileName[CRYPTIC_MAX_PATH];
		sprintf(tempFileName, "defs/ServerMessages.%s.translation", locGetName(locGetIDByLanguage(iLangID)));
		ErrorFilenamef(tempFileName, "There are %d words of new dupe messages for lang=%s. You need to run POToTranslate on the *_Dupes/*.po for this language.", iWordCount_Dupes, locGetName(locGetIDByLanguage(iLangID)));
	}

	// Fire a run-time error so the producer in charge of translations knows to fix the alternateTrans now.
	// An alternateTrans happens when two strings used to be different, got translated independently, then we later changed the English or Descriptions so the 2 strings got grouped together.
	// We don't know which of the 2+ translations is right for the group.
	if( bFoundAlternateTrans )
	{
		char tempFileName[CRYPTIC_MAX_PATH];
		sprintf(tempFileName, "defs/ServerMessages.%s.translation", locGetName(locGetIDByLanguage(iLangID)));
		ErrorFilenamef(tempFileName, "There are #.alternateTrans in the %s.po files for lang=%s. Someone needs to decide whether the #.alternateTrans or the msgstr is right for each instance and fix the .translate file.", pCategoryName, locGetName(locGetIDByLanguage(iLangID)));
	}

	fclose(pOutFile_All);
	fclose(pOutFile_Untranslated);
	fclose(pOutFile_TransChanged);
	fclose(pOutFile_Dupes);

	estrDestroy(&pCurOutString);
	estrDestroy(&pTempString);

	return NULL;
}

//returns an error string on failure, NULL on success
char *msgExportPOFiles(int iLocale)
{
	RefDictIterator iter = {0};
	Message *pMessage;
	char *pTempString = NULL;
	char *pCurOutString = NULL;


	Language iLangID = locGetLanguage(iLocale);



	MessageGroupForExport **ppAllMessageGroups = NULL;




	stashTableDestroyStructSafe(&sMessageGroupsByCombinedString, NULL, parse_MessageGroupForExport);
	sMessageGroupsByCombinedString = stashTableCreateWithStringKeys(16, StashDefault);

	estrStackCreate(&pTempString);

	sbCheckForChangedDefaultStringsForTranslations = true;
	langLoadTranslations(iLocale);

	//reset the temp flags on all messages
	RefSystem_InitRefDictIterator(gMessageDict, &iter);
	while (pMessage = RefSystem_GetNextReferentFromIterator(&iter))
	{
		pMessage->bMatchedACategory = pMessage->bMatchedThisCategory = false;
	}


	RefSystem_InitRefDictIterator(gMessageDict, &iter);
	while (pMessage = RefSystem_GetNextReferentFromIterator(&iter))
	{
		if (!pMessage->bDoNotTranslate)
		{
			MessageGroupForExport *pMessageGroup;

			// If scope restriction is requested, skip messages outside that scope.
			if (gpRestrictPoScope && !strstri(NULL_TO_EMPTY(pMessage->pcScope), NULL_TO_EMPTY(gpRestrictPoScope)))
				continue;

			estrPrintf(&pTempString, "\"");
			estrAppendEscaped(&pTempString, pMessage->pcDefaultString ? pMessage->pcDefaultString : "");
			estrConcatf(&pTempString, "\" \"");
			estrAppendEscaped(&pTempString, pMessage->pcDescription ? pMessage->pcDescription : "");
			estrConcatf(&pTempString, "\"");

			if (stashFindPointer(sMessageGroupsByCombinedString, pTempString, &pMessageGroup))
			{
				eaPush(&pMessageGroup->ppMessageKeys, pMessage->pcMessageKey);
			}
			else
			{
				pMessageGroup = StructCreate(parse_MessageGroupForExport);
				pMessageGroup->pCombinedString = strdup(pTempString);
				eaPush(&pMessageGroup->ppMessageKeys, pMessage->pcMessageKey);
				stashAddPointer(sMessageGroupsByCombinedString, pMessageGroup->pCombinedString, pMessageGroup, true);
			}
		}
	}


/*
#. description=CharacterClass Display Name
#. scope=CharacterClass/Away_Team_Bekk
#. key=CharacterClass.Away_Team_Bekk.DisplayName
#: C:/StarTrek/data/defs/classes/Away_Team_Bekk.class.ms:1
msgid "Bekk"
msgstr "Bekk"*/



	FOR_EACH_IN_STASHTABLE(sMessageGroupsByCombinedString, MessageGroupForExport, pMessageGroup)
	{
		eaPush(&ppAllMessageGroups, pMessageGroup);
	}
	FOR_EACH_END;

	eaQSort(ppAllMessageGroups, SortMessageGroup);



	ExportOneSetOfPOFiles(ppAllMessageGroups, NULL, "Everything", iLocale, iLangID);
	ExportOneSetOfPOFiles(ppAllMessageGroups, NULL, "Priority", iLocale, iLangID);
	
	if (fileExists(PO_CATEGORIES_FILENAME))
	{
		MessageCategoryForLocalizationList *pList = StructCreate(parse_MessageCategoryForLocalizationList);
		if (!ParserReadTextFile(PO_CATEGORIES_FILENAME, parse_MessageCategoryForLocalizationList, pList, 0))
		{
			assertmsgf(0, "Failed to read %s", PO_CATEGORIES_FILENAME);
		}

		FOR_EACH_IN_EARRAY_FORWARDS(pList->ppCategories, MessageCategoryForLocalization, pCategory)
		{
			ExportOneSetOfPOFiles(ppAllMessageGroups, pCategory, pCategory->pCategoryName, iLocale, iLangID);
		}
		FOR_EACH_END;

		ExportOneSetOfPOFiles(ppAllMessageGroups, NULL, "Uncategorized", iLocale, iLangID);

		StructDestroy(parse_MessageCategoryForLocalizationList, pList);
	}
	
	eaDestroy(&ppAllMessageGroups);

	

	estrDestroy(&pTempString);
	estrDestroy(&pCurOutString);
	return NULL;
}

static int *piLocalesForPOWriteOut = NULL;

AUTO_COMMAND ACMD_COMMANDLINE;
void WriteOutPOFiles(int iLocale)
{
	ea32PushUnique(&piLocalesForPOWriteOut, iLocale);
}

void msgWriteOutRequestedPOFilesIfAny(void)
{
	int i;

	for (i = 0; i < ea32Size(&piLocalesForPOWriteOut); i++)
	{
		char *pResult;
		
		loadstart_printf("Going to write out PO files for %s... ", locGetName( piLocalesForPOWriteOut[i]));
		pResult = msgExportPOFiles(piLocalesForPOWriteOut[i]);
		loadend_printf("done\n");
		
		assertmsgf(!pResult, "While exporting PO files for %s, got error: %s", locGetName( piLocalesForPOWriteOut[i]), pResult);
	}
}

void msgRemoveServerOnlyFlagsFromMsgTPI(void)
{
	int i;

	FORALL_PARSETABLE(parse_Message, i)
	{
		parse_Message[i].type &= ~TOK_SERVER_ONLY;
	}
}

//Counts the total number of words in messages in the given directory and all subfolders.
AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(9);
int msgDirectoryWordCount(const char* pchDir)
{
	Message *pMessage;
	RefDictIterator iter;
	int iCount = 0;
	char* estrForwardSlashes = estrCreateFromStr(pchDir);
	strchrReplace(estrForwardSlashes, '\\', '/');
	RefSystem_InitRefDictIterator(gMessageDict, &iter);
	while(pMessage = (Message*)RefSystem_GetNextReferentFromIterator(&iter))
	{
		if (strstri(pMessage->pcFilename, estrForwardSlashes) == pMessage->pcFilename)//begins with pchDir
			iCount += countWords(pMessage->pcDefaultString);
	}
	estrDestroy(&estrForwardSlashes);
	return iCount;
}

static bool msg_GetAudioAssets_HandleString(const char *pcFilename, const char *pcString, const char ***peaStrings)
{
	bool bStringHadSound = false;

	if (pcString)
	{
		const char *pcParse = pcString;

		//if (strstri(pcParse,"<sound")) {
		//	printf("%s\n",pcParse);
		//}

		while (*pcParse)
		{
			if (strnicmp(pcParse,"<sound",6) == 0)
			{
				if (pcParse[6] == ' ' ||
					pcParse[6] == '\t')
				{
					const char *pcCopyFrom = pcParse + 6;
					S32 iStrLength = 0;

					//skip any white space
					while ( *pcCopyFrom == ' ' ||
							*pcCopyFrom == '\t')
					{
						pcCopyFrom++;
					}

					//determine the length of the audio signal
					while (	'A'  <= pcCopyFrom[iStrLength] && pcCopyFrom[iStrLength] <= 'Z' ||
							'a'  <= pcCopyFrom[iStrLength] && pcCopyFrom[iStrLength] <= 'z' ||
							'0'  <= pcCopyFrom[iStrLength] && pcCopyFrom[iStrLength] <= '9' ||
							'_'  == pcCopyFrom[iStrLength] ||
							'\'' == pcCopyFrom[iStrLength] ||
							'/'  == pcCopyFrom[iStrLength] ||
							' '  == pcCopyFrom[iStrLength] ||
							'\t' == pcCopyFrom[iStrLength] )
					{
						iStrLength++;
					}

					//add the word
					if (iStrLength > 0 &&
						pcCopyFrom[iStrLength] == '>')
					{
						//remove any trailing whitespace
						while (	0 <= iStrLength-1 &&
								(	pcCopyFrom[iStrLength-1] == ' ' ||
									pcCopyFrom[iStrLength-1] == '\t' ))
						{
							iStrLength--;
						}

						if (iStrLength > 0)
						{
							char *pcNewString;
							S32 iCopy;

							pcNewString = malloc(iStrLength+1);
							for (iCopy = 0; iCopy < iStrLength; iCopy++) {
								pcNewString[iCopy] = pcCopyFrom[iCopy];
							}
							pcNewString[iStrLength] = '\0';
							eaPush(peaStrings, pcNewString);
							bStringHadSound = true;

							//printfColor(COLOR_BRIGHT,"%s\n",pcNewString);
						}
						else
						{
							Errorf("%s : Found poorly formated sound tag in message %s\n", pcFilename, pcString);
						}
					}
					else
					{
						Errorf("%s : Found poorly formated sound tag in message %s\n", pcFilename, pcString);
					}
				}
				else
				{
					Errorf("%s : Found poorly formated sound tag in message %s\n", pcFilename, pcString);
				}
			}

			pcParse++;
		}
	}

	return bStringHadSound;
}

void msg_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio)
{
	Message *pMessage;
	ResourceIterator rI;

	*ppcType = strdup("Messages");

	resInitIterator(gMessageDict, &rI);
	while (resIteratorGetNext(&rI, NULL, &pMessage))
	{
		*puiNumData = *puiNumData + 1;
		if (msg_GetAudioAssets_HandleString(pMessage->pcFilename, pMessage->pcDefaultString, peaStrings)) {
			*puiNumDataWithAudio = *puiNumDataWithAudio + 1;
		}
	}
	resFreeIterator(&rI);
}

#include "AutoGen/Message_h_ast.c"
#include "AutoGen/Message_c_ast.c"
