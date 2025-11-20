#ifndef STRING_FORMAT_H
#define STRING_FORMAT_H
#pragma once
GCC_SYSTEM

#include "applocale.h"

typedef struct ExprContext ExprContext;
typedef struct StashTableImp * StashTable;

//////////////////////////////////////////////////////////////////////////
// These are functions to handle a keyword-based format string language
// similar to CoH's translation text e.g. "Hello, {User}" where "User"
// gets replaced by some variable.
//
// It also supports conditional replacement using {condition?true|false},
// where true and false may also contain {}. "condition" is passed to
// a callback function.
//
// \, {, }, |, and ? cannot be used inside token names themselves.
// 
// It is safe to modify (but not extend) the token/condition in the callbacks.
//
// All text passed in should be in UTF-8 format.

#define STRFMT_TOKEN_START '{'
#define STRFMT_TOKEN_END '}'
#define STRFMT_TOKEN_ESCAPE '\\'
#define STRFMT_TOKEN_CHOICE '?'
#define STRFMT_TOKEN_ALTERNATE '|'

#define STRFMT_TOKEN_LENGTH 1024

// Called on each token found; this should append the appropriate data (as computed
// via the token and userdata) to the ppchResult estring.
typedef void (*StringFormatTokenFunc)(unsigned char **ppchResult, const unsigned char *pchToken, UserData pFormatData);

// Called on each condition found. ppchResult is the string so far (and can be modified).
typedef bool (*StringFormatConditionFunc)(unsigned char **ppchResult, const unsigned char *pchCondition, UserData pConditionData);

void strfmt_Format(unsigned char **ppchResult, const unsigned char *pchFormat,
				   StringFormatTokenFunc cbTokenFmt, UserData pFormatData,
				   StringFormatConditionFunc cbCondition, UserData pConditionData);

//////////////////////////////////////////////////////////////////////////
// Format strings based on a varargs list of 3-tuples or 4-tuples of 
// (name, type, value) or (name, type, value, parsetable). Use the provided macros
// to pass arguments sanely.
//
// Tokens in this format can use full object path lookups (e.g. {Player.name});
// in this case, you should pass a STRFMT_STRUCT("Player", pPlayer, parse_Player),
// and the rest of the string is used as the object path.
//
// Internally this uses some static memory, and so this is not thread-safe or reentrant.
// It can be made trivially thread-safe by adding a critical section.
// It can be made trivially reentrant by adding a stack of stash tables.

typedef struct StrFmtContext
{
	// Stash table mapping argument names to StrFmtContainers.
	StashTable stArgs;

	Language langID;

	// Whether we should translate message keys found in object paths.
	bool bTranslate;
} StrFmtContext;

typedef struct StrFmtContainer
{
	char chType;

	union
	{
		S32 iValue;
		F64 fValue;
		const char *pchValue;
		struct
		{
			void *pValue;
			ParseTable *pTable;
		};
	};
	void *pValue2; // some game data needs a separate pointer for e.g. both Entity*, Item*.
	const char *pchUsage; // if it has pValue2 then this holds the usage information
} StrFmtContainer;

AUTO_STRUCT;
typedef struct StrFmtParam
{
	char *key;

	const char *pchStrValue;	AST(NAME(StrValue))
	int iIntValue;				AST(NAME(IntValue))
	float fFloatValue;			AST(NAME(FloatValue))
	U8 code; // Does not support struct-related codes
} StrFmtParam;

extern ParseTable parse_StrFmtContainer[];
#define TYPE_parse_StrFmtContainer StrFmtContainer

// These codes will all be lowercase; game extensions are uppercase.
#define STRFMT_CODE_INT 'd'
#define STRFMT_CODE_STRING 's'
#define STRFMT_CODE_FLOAT 'g'
#define STRFMT_CODE_STRUCT 'p'

#define STRFMT_CODE_MESSAGE 'm'
#define STRFMT_CODE_MESSAGEKEY 'k'

#define STRFMT_END ((char *)NULL)

// Integer parameters are formatted with %d.
#define STRFMT_INT(pchName, iValue) (pchName), (char)STRFMT_CODE_INT, (S32)(iValue)

// Float parameters are formatted with %g.
#define STRFMT_FLOAT(pchName, fValue) (pchName), (char)STRFMT_CODE_FLOAT, (F64)(fValue)

// String parameters are formatted with %s.
#define STRFMT_STRING(pchName, pchValue) (pchName), (char)STRFMT_CODE_STRING, (unsigned const char *)(pchValue)

// Struct parameters are formatted using textparser.
#define STRFMT_STRUCT(pchName, pValue, pTable) (pchName), (char)STRFMT_CODE_STRUCT, (pValue), (pTable)

// Message structures can be passed intelligently.
#define STRFMT_MESSAGE(pchName, pValue) (pchName), (char)STRFMT_CODE_MESSAGE, (pValue)
#define STRFMT_MESSAGEREF(pchName, hValue) (pchName), (char)STRFMT_CODE_MESSAGE, (GET_REF(hValue))

// Display messages can also be passed...
#define STRFMT_DISPLAYMESSAGE(pchName, pValue) (pchName), (char)STRFMT_CODE_MESSAGE, (GET_REF(pValue.hMessage))

// Messages can also be passed by key
#define STRFMT_MESSAGEKEY(pchName, pchKey) (pchName), (char)STRFMT_CODE_MESSAGEKEY, (pchKey)

#define StringFormatError(pchField, pchType) \
	Errorf("Invalid field for " pchType ": %s", pchField);

#define StringConditionError(pchField, pchType) \
	Errorf("Invalid condition for " pchType ": %s", pchField);

#define StringFormatErrorReturn(pchField, pchType) \
	do { StringFormatError(pchField, pchType); return false; } while(0)

#define StringConditionErrorReturn(pchField, pchType) \
	do { StringConditionError(pchField, pchType); return false; } while(0)

void strfmt_FromListEx(unsigned char **ppchResult, const unsigned char *pchFormat, bool bTranslate, Language langID, va_list va);
void strfmt_FromArgsEx(unsigned char **ppchResult, const unsigned char *pchFormat, bool bTranslate, Language langID, ...);

// Untranslated string formatting, STRFMT_MESSAGEs are not supported.
void strfmt_FromArgs(unsigned char **ppchResult, const unsigned char *pchFormat, ...);
void strfmt_FromList(unsigned char **ppchResult, const unsigned char *pchFormat, va_list va);

// Append the string representation of pContainer onto ppchResult.
void strfmt_AppendContainer(unsigned char **ppchResult, StrFmtContainer *pContainer, const char *pchPath, StrFmtContext *pContext);

// Check if a numeric value meets a static condition, e.g. "Foo.Bar > 12".
bool strfmt_NumericCondition(F64 dValue, const unsigned char *pchField, const char *pchFilename);

// Append a translation of pchMessageKey to ppchResult, with some hacky munging for case.
bool strfmt_AppendMessageKey(unsigned char **ppchResult, const unsigned char *pchMessageKey, Language eLang);

void strfmt_FromStructEx(char **ppchResult, const char *pchFormat, EARRAY_OF(StrFmtParam) ppFmtParams, bool bTranslate, Language langID);
#define strfmt_FromStruct(ppchResult, pchFormat, ppParams, langID) strfmt_FromStructEx(ppchResult, pchFormat, ppParams, false, langID)

#endif