#ifndef MESSAGE_H
#define MESSAGE_H
#pragma once
GCC_SYSTEM

// Functions for dealing with the Message object, which is how you translate and display
// strings for end users.

#include "AppLocale.h"
#include "referencesystem.h"
#include "textparser.h"

typedef const void *DictionaryHandle;
typedef struct StrFmtParam StrFmtParam;
typedef enum GlobalType GlobalType;

// These need to be unique.  Message key prefixes should
// generally be correlated with the dictionaries which 
// contain objects with messages.  They should not be per
// editor.
//
// Do not reuse values of existing keys because
// that will increase the odds of name collisions.
//
// Values should be as short as possible.  Message built
// using these prefixes are intended to be terse to avoid
// using too much memory.
// 
// In case it's not obvious, MKP = Message Key Prefix
// 
// These are sorted by value.  Please keep it that
// way because it makes it easier to avoid conflicts.
#define MKP_CUTSCENESUBTITLE "CS"
#define MKP_COSTUME_GEO "G"
#define MKP_COSTUME_MAT "M"
#define MKP_COSTUME_TEX "T"
// Future message keys we may need...
#define MKP_GAMEACTION "GA"
//#define MKP_COSTUME "C"
//#define MKP_ENCOUNTER "E"
//#define MKP_CRITTERGROUP "CG"
//#define MKP_INTERACT "I"
//#define MKP_CONTACT "K"
#define MKP_MISSIONNAME "MN"
#define MKP_MISSIONUISTR "MU"
#define MKP_MISSIONSUMMARY "MS"
#define MKP_MISSIONDETAIL "MD"
#define MKP_MISSIONRETURN "MR"
#define MKP_MISSIONFAIL "MF"
#define MKP_MISSIONFAILRETURN "MX"
#define MKP_MISSIONSFXTEXT "MT"
#define MKP_MISSIONTEAMUPTEXT "MP"
#define MKP_MISSIONPARAM "MM"

//#define MKP_OPTIONALACTION "O"
//#define MKP_POWER "P"
#define MKP_POWERNAME "PN"
#define MKP_POWERDESC "PD"
#define MKP_POWERDESCLONG "PL"
#define MKP_POWERFLAV "PF"
//#define MKP_POWERSTORE "PS"
//#define MKP_POWERTREE "PT"
//#define MKP_CRITTER "R"
//#define MKP_PLAYERSTAT "PS"
//#define MKP_ITEMPOWER "IP"
//#define MKP_QUEUE "Q"
//#define MKP_STORE "S"
//#define MKP_ITEM "T"
//#define MKP_WORLD "W"
#define MKP_ITEMNAME "IN"
#define MKP_ITEMDESC "ID"
#define MKP_ITEMDESCSHORT "IS"
#define MKP_ITEMDESCUNIDENTIFIED "IZ" //Wanted to keep 2 chars, but didn't have a good one, so just went with z and q
#define MKP_ITEMNAMEUNIDENTIFIED "IQ"
#define MKP_ITEMEVENTTOOLTIP "IE"
#define MKP_ITEMAUTODESC "IA"
#define MKP_ITEMCALLOUT "IC"
#define MKP_UNPACK "IU"
#define MKP_UNPACKFAIL "IF"

AUTO_ENUM;
typedef enum Gender
{
	Gender_Unknown = 0,
	Gender_Female,
	Gender_Male,
	Gender_Neuter, ENAMES(Neuter Neutral None)
	Gender_MAX, EIGNORE
} Gender;
extern StaticDefineInt GenderEnum[];

AUTO_STRUCT
AST_IGNORE(Scope)
AST_IGNORE(Description)
AST_IGNORE(DefaultString)
AST_IGNORE(Updated)
AST_IGNORE(ClientMsg)
AST_IGNORE(CoreMsg)
AST_IGNORE(Duplicate)
AST_IGNORE(MsgFileName);
typedef struct TranslatedMessage
{
	// This key is used to match the translation with the primary message
	const char *pcMessageKey;	AST(KEY POOL_STRING NAME("MessageKey","MessageKeyVer3") )

	// The translated string for the given locale
	char *pcTranslatedString;   AST(CASE_SENSITIVE)
} TranslatedMessage; 

AUTO_STRUCT;
typedef struct TranslatedMessageDictionary
{
	// All the specific translated messages
	TranslatedMessage **ppTranslatedMessages; AST(NAME("Message"))

	// Shared memory stash table to lookup messages
	StashTable sLookupTable;
} TranslatedMessageDictionary;

// NOTICE:  The User Option Bit is there to tell MultiEditField not to check this
// IF YOU ARE ADDING FIELDS TO THIS STRUCTURE THAT ARE NOT EDITABLE BY THE DESIGNER, YOU SHOULD PROBABLY BE USING TOK_USEROPTIONBIT_1
// Talk to Stephen, Alex W, or Rob M if you have any questions about this.
AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct Message
{
	// The key for the message, can be stripped out before sent to client
	const char *pcMessageKey;	AST(KEY POOL_STRING NAME("MessageKey","MessageKeyVer3") USERFLAG(TOK_USEROPTIONBIT_1) )

	// File loaded from, can be stripped for client
	const char *pcFilename;		AST(CURRENTFILE USERFLAG(TOK_USEROPTIONBIT_1))

	// Scope that defines where the message fits in the system.
	// Slash delimited like a file path.
	const char *pcScope;		AST(POOL_STRING SERVER_ONLY )

	// Description to be used by translators
	char *pcDescription;		AST(CASE_SENSITIVE SERVER_ONLY)

	// String to use if no translation is found
	char *pcDefaultString;      AST(CASE_SENSITIVE)

	//line number in CURRENTFILE
	int iLineNum; AST(LINENUM USERFLAG(TOK_USEROPTIONBIT_1))

	// Disables editing because message is finalized
	U32 bFinal:1;				AST(SERVER_ONLY)

	// May be required to use a message but don't want it translated for some reason
	U32 bDoNotTranslate:1;		AST(SERVER_ONLY)

	// Indicates that the message in local memory has already been translated
	U32 bLocallyTranslated:1;	AST(NO_TEXT_SAVE USERFLAG(TOK_USEROPTIONBIT_1))

	// Indicates that the message in local memory has already been translated
	U32 bFailedLocalTranslation:1;	AST(NO_TEXT_SAVE USERFLAG(TOK_USEROPTIONBIT_1))

	//used internally during PO export
	U32 bMatchedACategory:1; NO_AST
	U32 bMatchedThisCategory : 1; NO_AST
} Message;
extern ParseTable parse_Message[];
#define TYPE_parse_Message Message
// IF YOU ARE ADDING FIELDS TO THIS STRUCTURE, SEE THE COMMENT AT THE TOP

// DisplayMessage is a fancy form of a reference to a message that includes
// some scratch space for editors to use.
// The user bit is there so compares can be done without including that field
AUTO_STRUCT;
typedef struct DisplayMessage
{
	REF_TO(Message) hMessage;  AST(STRUCTPARAM NAME(Message) REFDICT(Message) VITAL_REF)
	bool bEditorCopyIsServer;  AST(NO_TEXT_SAVE USERFLAG(TOK_USEROPTIONBIT_1))
	Message *pEditorCopy;	   AST(NAME(EditorCopy) NO_TEXT_SAVE)
} DisplayMessage;
extern ParseTable parse_DisplayMessage[];
#define TYPE_parse_DisplayMessage DisplayMessage

// DisplayMessageWithVO is a message with the ability to have VO
// generated for it.
AUTO_STRUCT;
typedef struct DisplayMessageWithVO
{
	DisplayMessage msg;					AST(NAME(Msg) EMBEDDED_FLAT)
	const char* astrLegacyAudioEvent;	AST(NAME(LegacyAudioEvent) POOL_STRING)
	bool bHasVO;						AST(NAME(HasVO))
} DisplayMessageWithVO;
extern ParseTable parse_DisplayMessageWithVO[];
#define TYPE_parse_DisplayMessageWithVO DisplayMessageWithVO

AUTO_STRUCT;
typedef struct DisplayMessageList
{
	DisplayMessage **eaMessages; AST(ADDNAMES(Message))
} DisplayMessageList;

AUTO_STRUCT;
typedef struct MessageList
{
	Message **eaMessages; AST(NAME(Message))
} MessageList;
extern ParseTable parse_MessageList[];
#define TYPE_parse_MessageList MessageList

AUTO_STRUCT;
typedef struct MessageStruct
{
	// Message Key or full string
	char *pchKey; AST(NAME(Key))
	// Parameters for Message
	EARRAY_OF(StrFmtParam) ppParameters; AST(NAME(Params))
	
	// Only send to players in the specified language; LANGUAGE_DEFAULT = send to everyone; 
	Language eLangSendRestriction;
} MessageStruct;

extern DictionaryHandle gMessageDict;

// Use these accessors to account for message fields not existing in some builds.
SA_RET_OP_STR const char *msgGetFilename(SA_PARAM_OP_VALID Message *pMessage);
SA_RET_OP_STR const char *msgGetScope(SA_PARAM_OP_VALID Message *pMessage);
SA_RET_OP_STR const char *msgGetDescription(SA_PARAM_OP_VALID Message *pMessage);
bool msgGetDoNotTranslate(SA_PARAM_OP_VALID Message *pMessage);
bool msgGetFinal(SA_PARAM_OP_VALID Message *pMessage);
bool msgGetLocallyTranslated(SA_PARAM_OP_VALID Message *pMessage);

SA_ORET_OP_STR const char *msgSetFilename(SA_PARAM_OP_VALID Message *pMessage, SA_PARAM_OP_STR const char *pcFilename);
SA_ORET_OP_STR const char *msgSetScope(SA_PARAM_OP_VALID Message *pMessage, SA_PARAM_OP_STR const char *pcScope);
SA_ORET_OP_STR const char *msgSetDescription(Message *pMessage, SA_PARAM_OP_STR const char *pcDescription);
void msgSetDoNotTranslate(SA_PARAM_OP_VALID Message *pMessage, bool bDoNotTranslate);
void msgSetFinal(SA_PARAM_OP_VALID Message *pMessage, bool bFinal);
void msgSetLocallyTranslated(SA_PARAM_OP_VALID Message *pMessage, bool bLocallyTranslated);

// ---------------------------------------------
// Functions that take in an explicit language
// Designed for use by server or message editors
// "Default" variants return the given default string when the message is invalid/not found, as opposed to returning NULL.

// Finds a particular translated message
TranslatedMessage *langFindTranslatedMessage(Language langID, Message *pMessage);

// Translates message with no argument replacement
SA_ORET_OP_STR const char *langTranslateMessage(Language langID, Message *pMessage);
const char *langTranslateMessageDefault(Language langID, Message *pMessage, const char *pcDefault);

// Wrapper for Message Refs
#define langTranslateMessageRef(langID, handle) langTranslateMessage((langID), GET_REF(handle))
#define langTranslateMessageRefDefault(langID, handle, pcDefault) langTranslateMessageDefault((langID), GET_REF(handle), pcDefault)
#define langTranslateDisplayMessage(langID, displayStruct) langTranslateMessageRef((langID), ((displayStruct).hMessage))
#define langTranslateDisplayMessageOrEditCopy(langID, displayStruct) ((displayStruct).pEditorCopy ? langTranslateMessage((langID), (displayStruct).pEditorCopy) : langTranslateMessage((langID), GET_REF((displayStruct).hMessage)))

// Takes in a key string instead of message pointer
SA_ORET_NN_STR const char *langTranslateMessageKey(Language langID, const char *pKey);
const char *langTranslateMessageKeyDefault(Language langID, const char *pKey, const char *pcDefault);

// Takes a StaticDefineInt name and enum value or key
const char *langTranslateStaticDefineInt(Language langID, StaticDefineInt *pDefine, S32 iValue);
const char *langTranslateStaticDefineIntKey(Language langID, StaticDefineInt *pDefine, const char* pchKey);
const char *langTranslateStaticDefineIntName(Language langID, const char* pchName, S32 iValue);
const char *langTranslateStaticDefineIntNameKey(Language langID, const char* pchName, const char* pchKey);

// Formats a message, including format arguments
void langFormatMessage(Language langID, char **eString, Message *pMessage, ...);
void langFormatMessageKey(Language langID, char **eString, const char *pKey, ...);
void langFormatMessageKeyDefault(Language langID, char **eString, const char *pKey, const char *pcDefault, ...);
void langFormatMessageKeyV(Language langID, char **eString, const char *pKey, va_list va);
void langFormatMessageKeyDefaultV(Language langID, char **eString, const char *pKey, const char *pcDefault, va_list va);
#define langFormatMessageRef(langID, eString, handle, ...) langFormatMessage((langID), eString, GET_REF(handle), __VA_ARGS__)

// Takes in a MessageStruct
void langFormatMessageStructDefault(Language langID, char **eString, MessageStruct *pFmt, const char *pcDefault);

// ---------------------------------------------
// Wrappers that use current language
// Designed for use on the client or other standalone programs

#define langGetCurrent() locGetLanguage(getCurrentLocale())

#define TranslateDisplayMessage(displayStruct) langTranslateMessageRef(locGetLanguage(getCurrentLocale()), ((displayStruct).hMessage))
#define TranslateDisplayMessageOrEditCopy(displayStruct) langTranslateDisplayMessageOrEditCopy(locGetLanguage(getCurrentLocale()), (displayStruct))
#define TranslateMessagePtr(message) langTranslateMessage(locGetLanguage(getCurrentLocale()), (message))
#define TranslateMessageRef(handle) langTranslateMessageRef(locGetLanguage(getCurrentLocale()), (handle))
#define TranslateMessageRefDefault(handle, pDefault) langTranslateMessageRefDefault(locGetLanguage(getCurrentLocale()), (handle), (pDefault))
#define TranslateMessageKey(pKey) langTranslateMessageKey(locGetLanguage(getCurrentLocale()), (pKey))
#define TranslateMessageKeyDefault(pKey, pDefault) langTranslateMessageKeyDefault(locGetLanguage(getCurrentLocale()), (pKey), (pDefault))

#define FormatDisplayMessage(eString, displayStruct, ...) langFormatMessageRef(locGetLanguage(getCurrentLocale()), (eString), (displayStruct).hMessage, __VA_ARGS__)
#define FormatMessagePtr(eString, message, ...) langFormatMessage(locGetLanguage(getCurrentLocale()), (eString), (message), __VA_ARGS__)
#define FormatMessageRef(eString, handle, ...) langFormatMessageRef(locGetLanguage(getCurrentLocale()), (eString), (handle), __VA_ARGS__)
#define FormatMessageKey(eString, pKey, ...) langFormatMessageKey(locGetLanguage(getCurrentLocale()), (eString), (pKey), __VA_ARGS__)
#define FormatMessageKeyDefault(eString, pKey, pDefault, ...) langFormatMessageKeyDefault(locGetLanguage(getCurrentLocale()), (eString), (pKey), (pDefault), __VA_ARGS__)
#define FormatMessageKeyV(eString, pKey, va) langFormatMessageKeyV(locGetLanguage(getCurrentLocale()), (eString), (pKey), va)
#define FormatMessageKeyDefaultV(eString, pKey, pDefault, va) langFormatMessageKeyDefaultV(locGetLanguage(getCurrentLocale()), (eString), (pKey), (pDefault), va)

// "Safe" translation macros that will not return NULL.
#define TranslateMessagePtrSafe(message, pcDefault) langTranslateMessageDefault(locGetLanguage(getCurrentLocale()), (message), (pcDefault))
#define TranslateMessageKeySafe(pKey) langTranslateMessageKeyDefault(locGetLanguage(getCurrentLocale()), (pKey), (pKey))

// A version of FieldToSimpleEString that also understands how to translate Message and DisplayMessage structures.
#define langFieldToSimpleEString(langID, tpi, column, structptr, index, ppEString, iWriteTextFlags) langFieldToSimpleEString_dbg(langID, tpi, column, structptr, index, ppEString, iWriteTextFlags, __FILE__, __LINE__)
bool langFieldToSimpleEString_dbg(Language langID, ParseTable tpi[], int column, const void* structptr, int index, char **ppEString, WriteTextFlags iWriteTextFlags, const char *caller_fname, int line);


//#ifndef NO_EDITORS

// ---------------------------------------------
// Editing functions for message structures

// Utility function to process each DisplayMessage in pStruct.
void langForEachDisplayMessage(ParseTable pti[], SA_PARAM_NN_VALID void *pStruct, void (*pCB)(DisplayMessage *pDisplayMessage, void *userdata), void *userdata);
void langForEachMessageRef(ParseTable pti[], SA_PARAM_NN_VALID void *pStruct, void (*pCB)(const char* messageKey, void *userdata), void *userdata);

// Checks to see if there are any messages in use on this object
bool langHasActiveMessage(ParseTable pParseTable[], void *pStruct);

// Makes editor copy of all DisplayMessage structs in within the parse table,
// and unreferences the handles.
// This is safe to call repeatedly on the same data structure.
void langMakeEditorCopy(ParseTable pParseTable[], void *pStruct, bool bCreateIfMissing);

// Pushes the editor copy into the Message dictionary and makes the DisplayMessage
// structures back to using handles (it deletes the editor copy).  The orig struct
// should have editor copies.  The filename of the Message structures is generated 
// based on the CURRENTFILE of the root structure (if one is defined).  The message
// files are saved to disk.
void langApplyEditorCopy(ParseTable pParseTable[], void *pStruct, void *pOrigStruct, bool bRemoveEmpty, bool bNoSave);
// For fix up use only
void langApplyEditorCopy2(ParseTable pParseTable[], void *pStruct, void *pOrigStruct, bool bRemoveEmpty, bool bNoSave, bool bUseErrorDialog);

// Just like langApplyEditorCopy(), but it assumes that pStruct
// contains the entire file data, and is the only struct to contain
// that data.
//
// Basicly, all the messages for pStruct's file are assumed to be all
// the messages in that file.  This is useful for the world editor,
// where this assumption is valid.
void langApplyEditorCopySingleFile(ParseTable pParseTable[], SA_PARAM_NN_VALID void *pStruct, bool bRemoveEmpty, bool bNoSave);

// Deletes all messages found in the struct from the dictionary.
// This is used when deleting an object
void langDeleteMessages(ParseTable pParseTable[], void *pStruct);

// Pushes the editor copy into the Message dictionary and makes the DisplayMessage
// structures back to using handles (it deletes the editor copy).  The orig struct should
// have references and not editor copies.  The filename of the Message structures is
// generated based on the CURRENTFILE of the root structure (if one is defined).  The 
// message files are saved to disk.  If pStruct is NULL, deletes entries from pOrigStruct.
void langApplyEditorCopyServerSide(ParseTable pParseTable[], void *pStruct, char *pOrigStruct, bool bRemoveEmpty);


// ---------------------------------------------
// Editing functions for display message list structures

// These functions all work on the editor copy only

// Adds a message to a DisplayMessageList.
// If there is already a message with that key in the list, it is replaced.
// Returns the added DisplayMessage
DisplayMessage *langAddDisplayMessageToList(DisplayMessageList *pList, const Message *pMessage);

// Adds a message to a DisplayMessageList, as a reference (not an editor copy).
// If there is already a message with that key in the list, does nothing.
// Returns the added DisplayMessage
DisplayMessage *langAddDisplayMessageReferenceToList(DisplayMessageList *pList, const char *pchMessageKey);

// Gets a display message from a DisplayMessageList
// If not in the list, but in the message dictionary, it loads it into the list.
// Returns NULL if the message does not exist
DisplayMessage *langGetDisplayMessageFromList(DisplayMessageList *pList, const char *pcMessageKey, bool bAddIfNotFound);

// Gets a message from a DisplayMessageList
// If not in the list, but in the message dictionary, returns the version from the dictionary.
const Message* langGetMessageFromListOrDictionary(DisplayMessageList *pList, const char *pcMessageKey);

// Gets a display message from a DisplayMessageList.
// If not in the list, but in the message dictionary, it loads it into the list.
// If no entry exists for the provided key, one is created using the provided data.
DisplayMessage *langGetOrCreateDisplayMessageFromList(DisplayMessageList *pList, const char *pcMessageKey,
													  const char *pcDescription, const char *pcScope);

// Removes a display message from the list
void langRemoveDisplayMessageFromList(DisplayMessageList *pList, const char *pcMessageKey);

//#endif

// ---------------------------------------------
// Utility functions

// Create a simple message structure with the given key used as is
Message *langCreateMessage(const char *pcKey, const char *pcDescription, const char *pcScope, const char *pcDefaultString);

// Creates a simple message structure.  The resulting key will be made terse using the prefix and given key seed.
Message *langCreateMessageWithTerseKey(const char *pcKeyPrefix, const char *pcKeySeed, const char *pcDescription, const char *pcScope, const char *pcDefaultString);

// Creates a simple message structure only filling in the default text.
Message *langCreateMessageDefaultOnly(const char *pcDefaultString);

// Returns true if the message is sufficiently populated
bool isValidMessage(const Message *pMessage);

// Returns true if the message exists
bool msgExists(const char *msgKey);

// Determine if a message is active
#define IsActiveDisplayMessage(displayStruct) IS_HANDLE_ACTIVE((displayStruct).hMessage)

// Fix up fields in a message based on provided values
void langFixupMessage(Message* pMessage, const char* key, const char* desc, const char* scope);

// Fix up fields in a message based on provided values.  The resulting key will be made terse using the prefix and given key seed.
void langFixupMessageWithTerseKey(Message *pMessage, const char *prefix, const char* keySeed, const char* desc, const char* scope);

// Construct a terse message key.  The key returned will have the given
// prefix followed by an encoding (currently CRC) of the message key seed.
// To avoid duplicate prefix usage, please use one of the prefixes defined
// in MSGKEY_PREFIX_* or create your own MSGKEY_PREFIX_* definition.
SA_RET_NN_STR const char *msgCreateTerseKey(SA_PARAM_NN_STR const char *pcPrefix, SA_PARAM_NN_STR const char *pcMessageKeySeed);

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
// and the next generated key is tested.  Repeat until we have a unique key.
SA_RET_NN_STR const char *msgCreateUniqueKey(SA_PARAM_NN_STR const char *pcPrefix, SA_PARAM_NN_STR const char *pcMessageKeySeed, SA_PARAM_OP_STR const char *pchOriginalKey);

// Return true if the character can possibly be an encoded key character.
bool msgCharIsEncoded(const char c);

// ---------------------------------------------
// System functions

// Load all messages into memory, and set up the reload callback
// Don't call this on the client
int msgLoadAllMessages(void);

void msgWriteOutRequestedPOFilesIfAny(void);

//very-special-case-use only during PO file export
void msgRemoveServerOnlyFlagsFromMsgTPI(void);

// If true, Load all the message translations, even if in development mode.
bool forceLoadAllTranslations(void);

extern bool quickLoadMessages;
extern int giMakeOneLocaleOPFilesAndExit;
extern int *giAllLocalesToMakeAndExit;

//a filtered category of messages, for purposes of making logically grouped
//PO files for the translators instead of just everything in one huge file
//
//if the filename of the message includes any of the substrings, then it is in this category
AUTO_STRUCT;
typedef struct MessageCategoryForLocalization
{
	char *pCategoryName; AST(STRUCTPARAM)
	char **ppFileNameSubStrings; AST(NAME(FileNameSubString))
} MessageCategoryForLocalization;

AUTO_STRUCT;
typedef struct MessageCategoryForLocalizationList
{
	MessageCategoryForLocalization **ppCategories; AST(NAME(Category))
} MessageCategoryForLocalizationList;


void msg_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio);

#endif
