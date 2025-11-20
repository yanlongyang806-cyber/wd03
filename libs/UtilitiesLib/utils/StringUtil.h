/***************************************************************************



***************************************************************************/

#ifndef STRINGUTIL_H
#define STRINGUTIL_H
#pragma once
GCC_SYSTEM

#include "AppLocale.h"

#if _XBOX
#include "XUtil.h"
#endif

extern const char *g_pchPlainWordDelimeters;
extern const char *g_pchCommandWordDelimiters;
extern const char g_UTF8CodepointLengths[256];

// Return a pointer to the first occurance of 'c' in 'pString' if it occurs
// within iLength characters; otherwise, return NULL.
SA_RET_OP_STR char *strnchr(SA_PARAM_NN_STR const char *pString, size_t iLength, char c);

// Find a substring.
const char *strnstr(const char *pHaystack, size_t uLength, const char *pNeedle);

// Find the next word in str, or the end (\0), if there is no next word.
// This function only understands ASCII delimiters.
SA_RET_NN_STR const char *strNextWordDelim(SA_PARAM_NN_STR const char *str, SA_PARAM_OP_STR const char *pchDelimiters);
#define strNextWord(str) strNextWordDelim((str), NULL)

//////////////////////////////////////////////////////////////////////////
// Unicode / UCS Support
// If you don't know about Unicode (or even if you think you do), you should
// read http://www.joelonsoftware.com/articles/Unicode.html
// In particular, confusing chars, codepoints, and characters will make me cry.
// -- jfw

//////////////////////////////////////////////////////////////////////////
// Conversion functions.

//if you're doing either of these something is probably very wrong, don't use them
int UTF8ToACP(SA_PARAM_NN_STR const char *str, SA_PARAM_OP_STR char *out, int len);
int ACPToUTF8(SA_PARAM_NN_STR const char* str, SA_PARAM_OP_STR char *out, int len);


//when you call this with outBuffer NULL it returns the number of characters it would write. Then you
//must allocate a buffer ONE LARGER THAN THAT so there's space for the null terminator.
int UTF8ToWideStrConvert(SA_PARAM_NN_STR const unsigned char *str, SA_PARAM_OP_STR unsigned short *outBuffer, int outBufferMaxLength);
int UTF8ToWideStrConvert_Verbose(const unsigned char *str, unsigned short *outBuffer, int outBufferMaxLength);

// If you can pass in a buffer size, this function can avoid needing to realloc
// space if the buffer is already large enough. It also stores the buffer size
// back into the pointer.
SA_RET_NN_STR unsigned short *UTF8ToWideStrConvertAndRealloc_dbg(SA_PARAM_NN_STR const unsigned char *str, SA_PARAM_OP_STR unsigned short *wstr, SA_PARAM_OP_VALID U32 *bufferSize MEM_DBG_PARMS);
#define UTF8ToWideStrConvertAndRealloc(str, wstr, bufferSize) UTF8ToWideStrConvertAndRealloc_dbg(str, wstr, bufferSize MEM_DBG_PARMS_INIT)

#if _PS3
#define CP_UTF8 0
#endif

int WideToEncodingConvert(const unsigned short* str, unsigned char* outBuffer, int outBufferMaxLength, U32 encoding);

unsigned short UTF8ToWideCharConvert(SA_PARAM_NN_STR const unsigned char* str);

U32 UTF8ToCodepoint(const char *str);

int WideToUTF8StrConvert(SA_PARAM_NN_STR const wchar_t* str, SA_PARAM_OP_STR char* outBuffer, int outBufferMaxLength);
SA_RET_NN_STR char* WideToUTF8StrTempConvert(SA_PARAM_NN_STR const unsigned short *str, SA_PARAM_OP_VALID int *newStrLength);
SA_RET_NN_STR char* WideToUTF8CharConvert(unsigned short character);


//////////////////////////////////////////////////////////////////////////
// UTF-8 utility functions.

// Convert a 16-bit codepoint to a UTF-8 string. Returns a static buffer.
SA_RET_NN_STR const char* WideToUTF8CodepointConvert(unsigned short codepoint);

// Returns true if the UTF-8 string is valid.
bool UTF8StringIsValid(const unsigned char *str, const char **err);

// Returns the length, in bytes, of the first codepoint of the UTF-8 string.
__forceinline static U32 UTF8GetCodepointLength(const unsigned char *str) { return g_UTF8CodepointLengths[*str]; }

// Returns a pointer to the character after the end of the starting codepoint.
// If this is the last codepoint in the string ('\0'), return it.
SA_RET_NN_STR char* UTF8GetNextCodepoint(SA_PARAM_NN_STR const unsigned char* str);

// Return a pointer to the indexth codepoint in this string, or NULL if
// index is greater than the length of the string.
SA_RET_OP_STR char* UTF8GetCodepoint(SA_PARAM_NN_STR const unsigned char* str, S32 index);

// Inverse of UTF8GetCodepoint.
S32 UTF8PointerToCodepointIndex(SA_PARAM_NN_STR const unsigned char *str, SA_PARAM_NN_STR const char *codepoint);

// Return the byte offset of the indexth codepoint. Returns -1 if the
// index is greater than the length of the string.
S32 UTF8GetCodepointOffset(SA_PARAM_NN_STR const unsigned char *str, int index);

// Return the length in codepoints of the string.
U32 UTF8GetLength(SA_PARAM_NN_STR const unsigned char* str);

// Returns true if codePt is a letter or number glyph, for a very wide definition of "letter".
// If bSimpleAsciiOnly is true, anything outside 0-127 will return false.
bool UTF8isalnum(U32 codePt, bool bSimpleAsciiOnly);

// Returns true if codePt is a fullwidth space (U+3000), or isspace()
// If bSimpleAsciiOnly is true, anything outside 0-127 will return false.
bool UTF8isspace(U32 codePt, bool bSimpleAsciiOnly);

// Cut these codepoints from the string. THIS FUNCTION IS UNSAFE, IT CAN
// CUT CHARACTERS IN THE MIDDLE.
void UTF8RemoveCodepoints(SA_PARAM_NN_STR unsigned char* str, int beginIndex, int endIndex);

// Returns the length of the string if the codepoint is too far, or this is the last word.
// Note that's *not* the length of the string - 1.
U32 UTF8CodepointOfNextWordDelim(SA_PARAM_NN_STR const char *str, U32 from, SA_PARAM_OP_STR const char *pchDelimiter);
#define UTF8CodepointOfNextWord(str, from) UTF8CodepointOfNextWordDelim((str), (from), NULL)

// Returns 0 if there is no previous word. Uses default set of delimiters
U32 UTF8CodepointOfPreviousWord(SA_PARAM_NN_STR const char *str, U32 from);

// Returns 0 if there is no previous word.  Uses passed in delimiters
U32 UTF8CodepointOfPreviousWordDelim(SA_PARAM_NN_STR const char *str, U32 from, SA_PARAM_OP_STR const char *pchDelimiters);

// Returns the codepoint of the end of the word starting at 'from'.
// If 'from' is a word delimiters, this will return the code point representing 
// the last word delimiter in the sequence of word delimiters starting from
// 'from'.  Returns the UTF8 length of the string if the codepoint is out of bounds.
U32 UTF8CodepointOfWordEndDelim(SA_PARAM_NN_STR const char *str, U32 from, SA_PARAM_OP_STR const char *pchDelimeters);

//destructively tokenizes a string, respecting " " and <& &> quotes, and escaping inside such. Uses space, \t, \n, \r as whitespace
//
//will ASSERT on badly formed input, so shouldn't be used willynilly on strings that might come from the client, or what have you
int TokenizeLineRespectingStrings_Count(char **ppOutTokens, char *pInString, int iOutTokenCount);
#define TokenizeLineRespectingStrings(ppOutTokens, pInString) TokenizeLineRespectingStrings_Count(ppOutTokens, pInString, ARRAY_SIZE(ppOutTokens))

//given a pointer to a buffer (presumably a multi-line loaded text file), and a pointer inside that buffer,
//returns a pointer to the beginning of the entire line (delimited by \n and \r) containing that pointer,
//and the length of the line
char *GetEntireLine(char *pPoint, char *pBuffer, int *piLineLength);

//same as strstr, but returns the last occurrence, not the first one
//
//this is SLOW. If you need to use it somewhere performance-relevant, rewrite it not to suck
char *strrstr(char *pString, char *pToFind);

#define IS_WHITESPACE(c) ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) =='\r')

#define string_mutate(s, operation) { char * character_ptr; for(character_ptr = (s); *character_ptr = operation(*character_ptr); ++character_ptr); }
#define string_tolower(s) string_mutate(s, tolower)
#define string_toupper(s) string_mutate(s, toupper)

bool StringContainsWhitespace(const char *s);

//"how different" two strings are
int levenshtein_distance(const char *s, const char*t);

//Another strtok-esque variant, specifically designed to be useful and flexible for things like BuildScripting
//
//First, trim any trailing whitespace. Then, if
//if there are any newlines, then divide by newlines, leaving all whitespace intact. If not, divide by commas, removing
//all leading and trailing around commas.
void DoVariableListSeparation_dbg(char ***pppOutList, char *pInString, bool bForceUseNewlines MEM_DBG_PARMS);
#define DoVariableListSeparation(pppOutList, pInString, bForceUseNewlines) DoVariableListSeparation_dbg(pppOutList, pInString, bForceUseNewlines MEM_DBG_PARMS_INIT)


char *strdup_removeWhiteSpace_dbg(const char *pInString MEM_DBG_PARMS);
#define strdup_removeWhiteSpace(pInString) strdup_removeWhiteSpace_dbg(pInString MEM_DBG_PARMS_INIT)

#define strdup_uncommented(s, params) strdup_uncommented_dbg(s, params, __FILE__, __LINE__)
typedef enum StrdupStripParams
{
	STRIP_JUST_COMMENTS=0,
	STRIP_LEADING_SPACES=1<<0,
	STRIP_EMPTY_LINES=1<<1,
	STRIP_ALL=~0,
} StrdupStripParams;
char *strdup_uncommented_dbg(const char *src, StrdupStripParams params, const char *caller_fname, int line);

// EArrayItemCopier-compatible strdup()
void* strdupFunc( const void* str );
void strFreeFunc(char* str);

// Returns if a string is null or empty
__forceinline static bool nullStr( const char* str ) { return !str || !str[0]; }

//version of stricmp that deals nicely with NULL or "" strings as either argument (with NULL and "" being equal)
int stricmp_safe(const char *pStr1, const char *pStr2);
//version of strcmp that deals nicely with NULL or "" strings as either argument (with NULL and "" being equal)
int strcmp_safe(const char *pStr1, const char *pStr2);

//returns true on success, false on failure
bool StringToInt(const char *pString, int *pInt);
bool StringToUint(const char *pString, U32 *pInt);

//note: the above two functions actually don't do what you expect, but I'm scared to change their behavior cavalierly. So I'm adding these:
bool StringToInt_Paranoid(const char *pString, int *pInt);
bool StringToUint_Paranoid(const char *pString, U32 *pInt);

bool StringToFloat(const char *pString, float *pFloat);
bool StringToVec3(const char *pString, Vec3 *pVec3);	//x,y,z

static __forceinline void StringToInt_Multisize_AssumeGoodInputEx(const char *pString, void *pInt, int iByteSize)
{
	switch (iByteSize)
	{
	case 1:
		(*((S8*)pInt)) = atoi(pString);
		return;
	case 2:
		(*((S16*)pInt)) = atoi(pString);
		return;
	case 4:
		(*((S32*)pInt)) = atoi(pString);
		return;
	case 8:
		(*((S64*)pInt)) = _atoi64(pString);
		return;
	}
	assertmsgf(0, "%d byte integers? that's crazy talk!", iByteSize);
}

#define StringToInt_Multisize_AssumeGoodInput(pString, pInt) StringToInt_Multisize_AssumeGoodInputEx(pString, pInt, sizeof(*(pInt)))

static __forceinline void StringToUint_Multisize_AssumeGoodInputEx(const char *pString, void *pInt, int iByteSize)
{
	char *pTemp;
	switch (iByteSize)
	{
	case 1:
		(*((U8*)pInt)) = strtoul(pString, &pTemp, 10);
		return;
	case 2:
		(*((U16*)pInt)) = strtoul(pString, &pTemp, 10);
		return;
	case 4:
		(*((U32*)pInt)) = strtoul(pString, &pTemp, 10);
		return;
	case 8:
		(*((U64*)pInt)) = _strtoui64(pString, &pTemp, 10);
		return;
	}
	assertmsgf(0, "%d byte integers? that's crazy talk!", iByteSize);
}

#define StringToUint_Multisize_AssumeGoodInput(pString, pInt) StringToUint_Multisize_AssumeGoodInputEx(pString, pInt, sizeof(*(pInt)))



// Return a duplicated string if new and old are different
void *strdup_ifdiff_dbg(const char *pNewString, char *pOldString MEM_DBG_PARMS);
#define strdup_ifdiff(pNewString, pOldString) strdup_ifdiff_dbg(pNewString, pOldString MEM_DBG_PARMS_INIT)
#define sstrdup_ifdiff(pNewString, pOldString) strdup_ifdiff_dbg(pNewString, pOldString MEM_DBG_PARMS_CALL)
#define ststrdup_ifdiff(pNewString, pOldString, pMemStructPtr) strdup_ifdiff_dbg(pNewString, pOldString MEM_DBG_STRUCT_PARMS_CALL(pMemStructPtr))

//removes from MasterList all strings that are in removelist, then adds all the strings in addlist.
//
//if bCheckValidity is true, then returns false if any of the ones in removelist aren't there, or
//any of the ones in addlist are there, estrPrintfs what happened into ppErrorString
//
//Note that if checkValidity is true, this may do partial work, ie remove two things and then stop when it sees
//something it doesn't like.
bool AddAndRemoveStrings_dbg(char ***pppMasterList, char ***pppAddList, char ***pppRemoveList, bool bCheckValidity, char **ppErrorString MEM_DBG_PARMS);
#define AddAndRemoveStrings(pppMasterList, pppAddList, pppRemoveList, bCheckValidity, ppErrorString) AddAndRemoveStrings_dbg(pppMasterList, pppAddList, pppRemoveList, bCheckValidity, ppErrorString MEM_DBG_PARMS_INIT)

//given an earray of "foo", "bar", "wakka", writes out "foo, bar, wakka"
void MakeCommaSeparatedString_dbg(char ***pppInList, char **ppOutEString MEM_DBG_PARMS);
#define MakeCommaSeparatedString(pppInList, ppOutEString) MakeCommaSeparatedString_dbg(pppInList, ppOutEString MEM_DBG_PARMS_INIT)

bool StringIsAllWhiteSpace(const char *pString);

// Due to various forms of stupidity in RFC 2822 and the various mail systems that may or may not follow it, 
// this function does nothing more than check for an '@' and non-empty local-parts and domains.
// More accurate validity can only be established by contacting the given mail server
bool StringIsValidEmail(const char *pString);

#define ASCII_NAME_MIN_LENGTH 3
#define ASCII_NAME_MAX_LENGTH 50
#define ASCII_SUMMARY_MAX_LENGTH 2000
#define ASCII_DESCRIPTION_MAX_LENGTH 10000
#define ASCII_DISPLAYNAME_MIN_LENGTH 3
#define ASCII_DISPLAYNAME_MAX_LENGTH 25
#define ASCII_ACCOUNTNAME_MIN_LENGTH 3
#define ASCII_ACCOUNTNAME_MAX_LENGTH 50

// Generic name validation function for ASCII-only names

#define STRINGERR_NONE 0
#define STRINGERR_CHARACTERS 1
#define STRINGERR_MIN_LENGTH 2
#define STRINGERR_MAX_LENGTH 3
#define STRINGERR_WHITESPACE 4
#define STRINGERR_PROFANITY	 5
#define STRINGERR_RESERVED	 6
#define STRINGERR_RESTRICTED 7

int StringIsInvalidNameGeneric(const char *pcString, const char *pcSpecialChars, U32 iMinLength, U32 iMaxLength, bool bCheckWhitespace, bool bRestrictSpecialSequences, bool bCheckProfanity, bool bCheckPrefixes, bool bCheckRestricted, bool bSimpleAsciiOnly, U32 iAccessLevel);

#define StringIsInvalidCommonNameInternal(pcName, iNameIndex, iAccessLevel)     StringIsInvalidNameGeneric(pcName, "-._ '", (iNameIndex!=1)*ASCII_NAME_MIN_LENGTH,        ASCII_NAME_MAX_LENGTH,  true,  true, true,  true,  true,  true, iAccessLevel)
#define StringIsInvalidCharacterNameInternal(pcName, iAccessLevel)              StringIsInvalidNameGeneric(pcName, "-._ '",                 ASCII_NAME_MIN_LENGTH,        ASCII_NAME_MAX_LENGTH, false,  true, true,  true,  true,  true, iAccessLevel)
#define StringIsInvalidCostumeNameInternal(pcName)                              StringIsInvalidNameGeneric(pcName, "-._ '",                 ASCII_NAME_MIN_LENGTH,        ASCII_NAME_MAX_LENGTH, false,  true, true, false,  true,  true,            0)
#define StringIsInvalidGuildNameInternal(pcName)                                StringIsInvalidNameGeneric(pcName, "-._ '",                 ASCII_NAME_MIN_LENGTH,        ASCII_NAME_MAX_LENGTH,  true,  true, true, false,  true,  true,            0)
#define StringIsInvalidGuildRankNameInternal(pcName)                            StringIsInvalidNameGeneric(pcName, "-._ '",                 ASCII_NAME_MIN_LENGTH,        ASCII_NAME_MAX_LENGTH,  true,  true, true, false, false,  true,            0)
#define StringIsInvalidGuildBankTabNameInternal(pcName)                         StringIsInvalidNameGeneric(pcName, "-._ '",                 ASCII_NAME_MIN_LENGTH,        ASCII_NAME_MAX_LENGTH,  true,  true, true, false, false,  true,            0)
#define StringIsInvalidMapNameInternal(pcName)                                  StringIsInvalidNameGeneric(pcName, "-._ '",                 ASCII_NAME_MIN_LENGTH,        ASCII_NAME_MAX_LENGTH,  true,  true, true, false, false,  true,            0)
#define StringIsInvalidSummaryInternal(pcName)                                  StringIsInvalidNameGeneric(pcName,    NULL,                                     0,     ASCII_SUMMARY_MAX_LENGTH, false, false, true, false, false,  true,            0)
#define StringIsInvalidDescriptionInternal(pcName)                              StringIsInvalidNameGeneric(pcName,    NULL,                                     0, ASCII_DESCRIPTION_MAX_LENGTH, false, false, true, false, false,  true,            0)

#if _XBOX
#define StringIsInvalidFormalName(pcName, iNameIndex, iAccessLevel) (StringIsInvalidCommonNameInternal(pcName, iNameIndex, iAccessLevel) ? true : !xUtil_IsValidXLiveString(pcName))
#define StringIsInvalidCommonName(pcName, iAccessLevel) StringIsInvalidFormalName(pcName, 0, iAccesslevel)
#define StringIsInvalidCharacterName(pcName, iAccessLevel) (StringIsInvalidCharacterNameInternal(pcName, iAccessLevel) ? true : !xUtil_IsValidXLiveString(pcName))
#define StringIsInvalidCostumeName(pcName) (StringIsInvalidCostumeNameInternal(pcName) ? true : !xUtil_IsValidXLiveString(pcName))
#define StringIsInvalidGuildName(pcName) (StringIsInvalidGuildNameInternal(pcName) ? true : !xUtil_IsValidXLiveString(pcName))
#define StringIsInvalidGuildRankName(pcName) (StringIsInvalidGuildRankNameInternal(pcName) ? true : !xUtil_IsValidXLiveString(pcName))
#define StringIsInvalidGuildBankTabName(pcName) (StringIsInvalidGuildBankTabNameInternal(pcName) ? true : !xUtil_IsValidXLiveString(pcName))
#define StringIsInvalidMapName(pcName) (StringIsInvalidMapNameInternal(pcName) ? true : !xUtil_IsValidXLiveString(pcName))
#define StringIsInvalidDescription(pcName) (StringIsInvalidDescriptionInternal(pcName) ? true : !xUtil_IsValidXLiveString(pcName))
#else
#define StringIsInvalidFormalName(pcName, iNameIndex, iAccessLevel) StringIsInvalidCommonNameInternal(pcName, iNameIndex, iAccessLevel)
#define StringIsInvalidCommonName(pcName, iAccessLevel) StringIsInvalidFormalName(pcName, 0, iAccessLevel)
#define StringIsInvalidCharacterName(pcName, iAccessLevel) StringIsInvalidCharacterNameInternal(pcName, iAccessLevel)
#define StringIsInvalidCostumeName(pcName) StringIsInvalidCostumeNameInternal(pcName)
#define StringIsInvalidGuildName(pcName) StringIsInvalidGuildNameInternal(pcName)
#define StringIsInvalidGuildRankName(pcName) StringIsInvalidGuildRankNameInternal(pcName)
#define StringIsInvalidGuildBankTabName(pcName) StringIsInvalidGuildBankTabNameInternal(pcName)
#define StringIsInvalidMapName(pcName) StringIsInvalidMapNameInternal(pcName)
#define StringIsInvalidDescription(pcName) StringIsInvalidDescriptionInternal(pcName)
#endif

#define ACCOUNTNAME_INVALID_CHARS "-._@"
#define DISPLAYNAME_INVALID_CHARS "-._"
#define StringIsInvalidAccountName(pcName, iAccessLevel) StringIsInvalidNameGeneric(pcName, ACCOUNTNAME_INVALID_CHARS, ASCII_NAME_MIN_LENGTH, ASCII_NAME_MAX_LENGTH, false, true, true, true, true, true, iAccessLevel)
#define StringIsInvalidDisplayName(pcName, iAccessLevel) StringIsInvalidNameGeneric(pcName, DISPLAYNAME_INVALID_CHARS, ASCII_NAME_MIN_LENGTH, ASCII_NAME_MAX_LENGTH, true, true, true, true, true, true, iAccessLevel)


#define StringIsValidAccountName(pcName, iAccessLevel) (!StringIsInvalidAccountName(pcName, iAccessLevel))
#define StringIsValidCommonName(pcName, iAccessLevel) (!StringIsInvalidCommonName(pcName, iAccessLevel))
#define StringIsValidCharacterName(pcName, iAccessLevel) (!StringIsInvalidCharacterName(pcName, iAccessLevel))
#define StringIsValidDisplayName(pcName, iAccessLevel) (!StringIsInvalidDisplayName(pcName, iAccessLevel))
#define StringIsValidCostumeName(pcName) (!StringIsInvalidCostumeName(pcName))
#define StringIsValidGuildName(pcName) (!StringIsInvalidGuildName(pcName))
#define StringIsValidGuildRankName(pcName) (!StringIsInvalidGuildRankName(pcName))
#define StringIsValidGuildBankTabName(pcName) (!StringIsInvalidGuildBankTabName(pcName))
#define StringIsValidMapName(pcName) (!StringIsInvalidMapName(pcName))
#define StringIsValidDescription(pcName) (!StringIsInvalidDescription(pcName))

void StringCreateNameErrorEx(SA_PRE_NN_OP_STR char **msg, int strerr, Language eLang, int iMinNameLength, int iMaxNameLength);
void StringCreateDescriptionErrorEx(SA_PRE_NN_OP_STR char **msg, int strerr, Language eLang );

#ifdef GAMECLIENT
	#define StringCreateNameError(msg, errid) StringCreateNameErrorEx( msg, errid, langGetCurrent(), -1, -1 )
	//Use the following if you want to override the min or max name lengths
	#define StringCreateNameErrorMinMax(msg, errid, minLen, maxLen) StringCreateNameErrorEx( msg, errid, langGetCurrent(), minLen, maxLen )
	#define StringCreateDescriptionError(msg, errid) StringCreateDescriptionErrorEx( msg, errid, langGetCurrent() )
#else
	#define entStringCreateNameError(ent, msg, errid) StringCreateNameErrorEx( msg, errid, entGetLanguage(ent), -1, -1 )
	#define entStringCreateDescriptionError(ent, msg, errid) StringCreateDescriptionErrorEx( msg, errid, entGetLanguage(ent) )
#endif


#define PASSWORD_MIN_LENGTH 3
#define PASSWORD_MAX_LENGTH 100
// ASCII-only passwords
bool StringIsValidPassword(const char *pString);


bool StringKeywordSearchMatches (const char *text, const char *keywordString);

//return the number of lines in the string.
int StringCountLines(const char *str);

// CCase - Canonical?  Cryptic?  Camel?  Take your pick!
bool StringIsCCase(const char *s);
void StringToCCase(char *s);

void StringFormatNumberSignificantDigits(char* pchString, F32 fNumber, int iDigits, bool bInsertCommas, bool bInsertK);

//returns the character found, optionally sets pOutIndex to point to
//where in the string it is
char GetFirstNonWhitespaceChar(const char *pSrc, int *pOutIndex);

// Strips away html-style <tags>. Removes everything between < and the next >, inclusive.
void StringStripTagsSafe_dbg(const char *src, char **pestrDest MEM_DBG_PARMS);
#define StringStripTagsSafe(src, pestr) StringStripTagsSafe_dbg(src, pestr MEM_DBG_PARMS_INIT)

// Strips away html-style <tags>. Inserts spaces in place of <br> tags. Doesn't copy into pestrOutput if no changes are made, unless bCopyIfUnchanged is true.
bool StringStripTagsPrettyPrintEx(SA_PARAM_OP_STR const char *pchInput, SA_PARAM_NN_VALID char **pestrOutput, bool bCopyIfUnchanged);
#define StringStripTagsPrettyPrint(pchInput, pestrOutput) StringStripTagsPrettyPrintEx(pchInput, pestrOutput, true)

// Strips away ONLY the <font > </font> tags. Doesn't copy into pestrOutput if no changes are made, unless bCopyIfUnchanged is true.
bool StringStripFontTagsEx(SA_PARAM_OP_STR const char *pchInput, SA_PARAM_NN_VALID char **pestrOutput, bool bCopyIfUnchanged);
#define StringStripFontTags(pchInput, pestrOutput) StringStripFontTagsEx(pchInput, pestrOutput, true)

//given two earrays of strings, returns an earray of all strings in list 1 not in list 2, and an earray of all strings in list 2 not in list 1.
//Note: this is VERY unoptimized, make it way better if you're going to be putting in big inputs.
typedef bool (*StringComparer)(char *p1, char *p2);
void FindDifferencesBetweenEarraysOfStrings(char ***pppOutList1, char ***pppOutList2, char ***pppInList1, char ***pppInList2, StringComparer pComparer);

// Stores the SMF stripped string in the estrOutput only if there is anything
// to be stripped in pchInput.
bool StringStripSMF(SA_PARAM_OP_STR const char *pchInput, SA_PARAM_NN_VALID char **estrOutput);

wchar_t UnicodeToUpper(wchar_t c);
wchar_t UnicodeToLower(wchar_t c);

AUTO_STRUCT;
typedef struct StringListStruct
{
	const char **list;
} StringListStruct;

static __forceinline char *strdup_s(const char *src, int iCount)
{
	char *pRetVal = (char*)malloc(iCount + 1);
	memcpy(pRetVal, src, iCount);
	pRetVal[iCount] = 0;
	return pRetVal;
}


//if pList is "happy, birthday" returns true for "happy" and "birthday" but not "birth", deals
//with odd amounts of spacing, etc.
bool CommaSeparatedListContainsWord(const char *pList, const char *pWord);


//special string compare function... returns true if two strings match, but also treats
//"1" and "1.0" and "1.0000" as matches. Also treats NULL and "" as matching.
bool StringsMatchRespectingNumbers(const char *pStr1, const char *pStr2);


//Beyond-ASCII
void UTF8ReduceString(const char *pOrig, char **pestrReduced);
void UTF8NormalizeString(const char *pOrig, char **pestrNormalized);
void UTF8NormalizeAllStringsInStruct(ParseTable pti[], void *structptr);

bool UTF8CodepointIsBasic(U32 codePt);
bool UTF8CodepointIsExtended(U32 codePt);
bool UTF8CodepointIsPunctuation(U32 codePt);
bool UTF8CodepointIsSeparator(U32 codePt);

#endif
