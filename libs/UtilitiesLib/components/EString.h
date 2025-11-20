#ifndef ESTRING_H
#define ESTRING_H
GCC_SYSTEM

typedef struct Packet Packet;

//---------------------------------------------------------------------------------
// EString: binary interface
//---------------------------------------------------------------------------------
void estrInsert_dbg(SA_PARAM_OP_OP_STR char** dst, unsigned int insertByteIndex, SA_PARAM_OP_STR const char* buffer, unsigned int byteCount, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrInsert(dst, insertByteIndex, buffer, byteCount) estrInsert_dbg(dst, insertByteIndex, buffer, byteCount, __FILE__, __LINE__)
void estrRemove(SA_PARAM_OP_OP_STR char** str, unsigned int removeByteIndex, unsigned int byteCount);
void estrConcat_dbg(SA_PARAM_OP_OP_STR char** str, SA_PARAM_OP_STR const char* buffer, unsigned int byteCount, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrConcat(str, buffer, byteCount) estrConcat_dbg(str, buffer, byteCount, __FILE__, __LINE__)

unsigned int estrLength(SA_PARAM_OP_OP_STR const char* const * str);
unsigned int estrSetSize_dbg(SA_PARAM_OP_OP_STR char** str, unsigned int size, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrSetSize(str, size) estrSetSize_dbg(str, size, __FILE__, __LINE__)
unsigned int estrForceSize_dbg(SA_PARAM_OP_OP_STR char** str, unsigned int size, SA_PARAM_NN_STR const char *caller_fname, int line); // Doesn't clear the new string
#define estrForceSize(str, size) estrForceSize_dbg(str, size, __FILE__, __LINE__)
unsigned int estrAllocSize(SA_PARAM_OP_OP_STR const char* const * str);

//---------------------------------------------------------------------------------
// EString: narrow/utf8 character interface
//---------------------------------------------------------------------------------
void estrCreate_dbg(SA_PARAM_OP_OP_STR char** str, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrCreate(str) estrCreate_dbg(str, __FILE__, __LINE__)
void estrHeapCreate_dbg(SA_PARAM_OP_OP_STR char** str, unsigned int initSize, int special_heap, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrHeapCreate(str, initSize, special_heap) estrHeapCreate_dbg(str, initSize, special_heap, __FILE__, __LINE__)

SA_RET_NN_STR char *estrCreateFromStr_dbg(SA_PARAM_NN_STR const char *str, SA_PARAM_NN_STR const char *caller_fname, int line); // create an estring from a normal string
#define estrCreateFromStr(str) estrCreateFromStr_dbg(str, __FILE__, __LINE__)
SA_RET_NN_STR char *estrStackCreateFromStr_dbg(SA_PARAM_NN_STR const char *str, SA_PARAM_NN_STR const char *caller_fname, int line); // create an estring from a normal string
#define estrStackCreateFromStr(str) estrStackCreateFromStr_dbg(str, __FILE__, __LINE__)

//creates an estring which uses a buffer (which the estring system will not own) as its
//initial storage
void estrBufferCreate(SA_PARAM_OP_OP_STR char **str, char *pBuffer, unsigned int iBufferSize);

void estrDestroy(SA_PRE_OP_OP_STR SA_POST_OP_NULL char** str);
void estrClear(SA_PARAM_OP_OP_STR char** str);
// set all characters in the estring to the given character
void estrClearTo(SA_PARAM_OP_OP_STR char** str, char c);

static __forceinline void estrZeroAndDestroy(char **str)
{
	int iLen = estrLength(str);
	if (iLen)
	{
		memset(*str, 0, iLen);
	}
	estrDestroy(str);
}

static __forceinline void estrZeroAndClear(char **str)
{
	int iLen = estrLength(str);
	if (iLen)
	{
		memset(*str, 0, iLen);
	}
	estrClear(str);
}



void estrAppend_dbg(SA_PARAM_OP_OP_STR char** dst, SA_PARAM_OP_OP_STR const char** src, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrAppend(dst, src) estrAppend_dbg(dst, src, __FILE__, __LINE__)
void estrAppend2_dbg(SA_PARAM_OP_OP_STR char** dst, SA_PARAM_OP_STR const char* src, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrAppend2(dst, src) estrAppend2_dbg(dst, src, __FILE__, __LINE__)
#define estrAppend2_inline(dst, src) estrAppend2_dbg_inline(dst, src, __FILE__, __LINE__)

void estrAppendFromPacket_dbg(SA_PARAM_OP_OP_STR char **str, SA_PARAM_NN_VALID Packet *pPak, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrAppendFromPacket(str, pPak) estrAppendFromPacket_dbg(str, pPak, __FILE__, __LINE__)

void estrCopyFromPacket_dbg(SA_PARAM_OP_OP_STR char **str, SA_PARAM_NN_VALID Packet *pPak, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrCopyFromPacket(str, pPak) estrCopyFromPacket_dbg(str, pPak, __FILE__, __LINE__)

void estrCopyFromPacketNonEmpty_dbg(SA_PARAM_OP_OP_STR char **str, SA_PARAM_NN_VALID Packet *pPak, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrCopyFromPacketNonEmpty(str, pPak) estrCopyFromPacketNonEmpty_dbg(str, pPak, __FILE__, __LINE__)

unsigned int estrPrintf_dbg(SA_PARAM_OP_OP_STR char** str, SA_PARAM_NN_STR const char *caller_fname, int line, FORMAT_STR const char* format, ...);
#define estrPrintf(str, fmt, ...) estrPrintf_dbg(str, __FILE__, __LINE__, FORMAT_STRING_CHECKED(fmt), ##__VA_ARGS__)

//exactly like the above, but does NOT do the static checking on the printf format specifiers, so you can use our
//home-built cryptic ones
unsigned int estrPrintfUnsafe_dbg(SA_PRE_OP_OP_STR SA_POST_NN_NN_STR char** str, SA_PARAM_NN_STR const char *caller_fname, int line, const char* format, ...);
#define estrPrintfUnsafe(str, fmt, ...) estrPrintfUnsafe_dbg(str, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

unsigned int estrConcatf_dbg(SA_PRE_OP_OP_STR SA_POST_NN_NN_STR char** str, SA_PARAM_NN_STR const char *caller_fname, int line, FORMAT_STR const char* format, ...);
#define estrConcatf(str, fmt, ...) estrConcatf_dbg(str, __FILE__, __LINE__, FORMAT_STRING_CHECKED(fmt), ##__VA_ARGS__)
unsigned int estrConcatfv_dbg(SA_PRE_OP_OP_STR SA_POST_NN_NN_STR char** str, SA_PARAM_NN_STR const char *caller_fname, int line, FORMAT_STR const char* format, va_list args);
#define estrConcatfv(str, format, valist) estrConcatfv_dbg(str, __FILE__, __LINE__, format, valist)

unsigned int estrInsertf_dbg(char** str, int iInsertByteIndex, const char *caller_fname, int line, FORMAT_STR const char* format, ...);
#define estrInsertf(str, iInsertByteIndex, fmt, ...) estrInsertf_dbg(str, iInsertByteIndex, __FILE__, __LINE__, FORMAT_STRING_CHECKED(fmt), ##__VA_ARGS__)


// (optimized) quick append functions
void estrConcatString_dbg(SA_PRE_OP_OP_STR SA_POST_NN_NN_STR char** str, SA_PARAM_NN_STR const char* appendme, unsigned int width, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrConcatString(str, appendme, width) estrConcatString_dbg(str, appendme, width, __FILE__, __LINE__)
#define estrConcatStatic(STR, APPENDME) estrConcatString(STR, APPENDME, ARRAY_SIZE_CHECKED(APPENDME) - 1)
void estrConcatChar_dbg(SA_PRE_OP_OP_STR SA_POST_NN_NN_STR char** str, char appendme, int count, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrConcatChar(str, appendme) estrConcatChar_dbg(str, appendme, 1, __FILE__, __LINE__)
#define estrConcatCharCount(str, appendme, count) estrConcatChar_dbg(str, appendme, count, __FILE__, __LINE__)

void estrCopy_dbg(SA_PRE_OP_OP_STR SA_POST_NN_NN_STR char** dst, SA_PARAM_OP_OP_STR const char** src, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrCopy(dst, src) estrCopy_dbg(dst, src, __FILE__, __LINE__)
void estrCopy2_dbg(SA_PRE_OP_OP_STR SA_POST_NN_NN_STR char** dst, SA_PARAM_OP_STR const char* src, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrCopy2(dst, src) estrCopy2_dbg(dst, src, __FILE__, __LINE__)

unsigned int estrReserveCapacity_dbg(SA_PARAM_OP_OP_STR char** str, unsigned int reserveSize, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrReserveCapacity(str, reserveSize) estrReserveCapacity_dbg(str, reserveSize, __FILE__, __LINE__)
unsigned int estrGetCapacity(SA_PARAM_OP_OP_STR const char** str);

unsigned int estrAppendEscaped_dbg(SA_PARAM_OP_OP_STR char **dst, SA_PARAM_OP_STR const char *src, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrAppendEscaped(dst, src) estrAppendEscaped_dbg(dst, src, __FILE__, __LINE__)
unsigned int estrAppendEscapedf_dbg(SA_PARAM_OP_OP_STR char** str, SA_PARAM_NN_STR const char *caller_fname, int line, FORMAT_STR const char* format, ...);
#define estrAppendEscapedf(str, format, ...) estrAppendEscapedf_dbg(str, __FILE__, __LINE__, FORMAT_STRING_CHECKED(format), ##__VA_ARGS__)
unsigned int estrAppendUnescaped_dbg(SA_PARAM_OP_OP_STR char **dst, SA_PARAM_NN_STR const char *src, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrAppendUnescaped(dst, src) estrAppendUnescaped_dbg(dst, src, __FILE__, __LINE__)

unsigned int estrAppendEscapedCount_dbg(SA_PARAM_OP_OP_STR char **dst, SA_PARAM_OP_STR const char *src, unsigned int chars, bool ignoreNULL, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrAppendEscapedCount(dst, src, chars, ignoreNULL) estrAppendEscapedCount_dbg(dst, src, chars, ignoreNULL, __FILE__, __LINE__)
unsigned int estrAppendUnescapedCount_dbg(SA_PARAM_OP_OP_STR char **dst, SA_PARAM_OP_STR const char *src, unsigned int chars, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrAppendUnescapedCount(dst, src, chars) estrAppendUnescapedCount_dbg(dst, src, chars, __FILE__, __LINE__)

//given an estring, finds all occurrences in it of pWhatToFind and replaces them with pWhatToReplaceItWith, resizing as necessary
//
//returns true if at least one occurrence was replaced
bool estrReplaceOccurrences_dbg(bool bCaseInsensitive, SA_PARAM_OP_OP_STR char **str, SA_PARAM_NN_STR const char* pWhatToFind, SA_PARAM_OP_STR const char *pWhatToReplaceItWith, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrReplaceOccurrences(str, pWhatToFind, pWhatToReplaceItWith) estrReplaceOccurrences_dbg(false, str, pWhatToFind, pWhatToReplaceItWith, __FILE__, __LINE__)
#define estrReplaceOccurrences_CaseInsensitive(str, pWhatToFind, pWhatToReplaceItWith) estrReplaceOccurrences_dbg(true, str, pWhatToFind, pWhatToReplaceItWith, __FILE__, __LINE__)

//replaces any of the chars in pCharString with the replacement char
void estrReplaceMultipleChars(SA_PARAM_OP_OP_STR char **dst, const char *pCharString, char replacementChar);

//given an estring, replaces all "\n" with "\r\n"
void estrFixupNewLinesForWindows_dbg(SA_PARAM_OP_OP_STR char **str, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrFixupNewLinesForWindows(str) estrFixupNewLinesForWindows_dbg(str, __FILE__, __LINE__)
				
//given an estring, trims all leading and trailing whitespace. If the string is all whitespace, it will end up as ""
//returns # chars removed
int estrTrimLeadingAndTrailingWhitespaceEx(SA_PARAM_OP_OP_STR char **str, const char *pExtraWhitespaceChars);
#define estrTrimLeadingAndTrailingWhitespace(str) estrTrimLeadingAndTrailingWhitespaceEx(str, NULL)


//combines estrDup and estrTrimLeadingAndTrailingWhitespace
char *estrDupAndTrim_dbg(SA_PARAM_NN_STR const char *pInStr, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrDupAndTrim(pInStr) estrDupAndTrim_dbg(pInStr, __FILE__, __LINE__)

//given a string, copies it into an estring and does any escaping necessary for html display
void estrCopyWithHTMLEscaping(SA_PARAM_OP_OP_STR char **str, SA_PARAM_OP_STR const char* src, bool escape_newline);
//will not leave str as a NULL string
void estrCopyWithHTMLEscapingSafe(SA_PARAM_OP_OP_STR char **str, SA_PARAM_OP_STR const char* src, bool escape_newline);

// Copies only valid XML characters
void estrCopyValidXMLOnly(SA_PARAM_OP_OP_STR char **dst, SA_PARAM_NN_STR const char *src);

//replace %escapes in a URI
void estrCopyWithURIUnescaping(SA_PARAM_OP_OP_STR char **str, SA_PARAM_OP_STR const char* src);

void estrCopyWithURIEscaping(SA_PARAM_OP_OP_STR char **str, SA_PARAM_OP_STR const char* src);


//given an HTML string, copies it into an estring and does any unescaping necessary
void estrCopyWithHTMLUnescaping(SA_PARAM_OP_OP_STR char **str, SA_PARAM_OP_STR const char* src);
//will not leave str as a NULL string
void estrCopyWithHTMLUnescapingSafe(SA_PARAM_OP_OP_STR char **str, SA_PARAM_OP_STR const char* src);

//"super escape" and unescape a string. This takes any null-terminated string and turns it into a string
//containing only alphanumerical characters and underscores. No spaces, no punctuation, no special characters.
//
//It's particularly useful for passing complicated string arguments on command lines, where the normal
//command line parsing will get confused by quotes, dashes, etc.
//
//estrSuperEscapeString_shorter is compatible with estrSuperUnescapeString, but uses only numeric
//codes, so is less human-legible
void estrSuperEscapeString_dbg(SA_PARAM_OP_OP_STR char **ppOutString, SA_PARAM_OP_STR const char* pInString, SA_PARAM_NN_STR const char *caller_fname, int line);
void estrSuperEscapeString_shorter_dbg(SA_PARAM_OP_OP_STR char **ppOutString, SA_PARAM_OP_STR const char* pInString, SA_PARAM_NN_STR const char *caller_fname, int line);
bool estrSuperUnescapeString_dbg(SA_PARAM_OP_OP_STR char **ppOutString, SA_PARAM_OP_STR const char* pInString, SA_PARAM_NN_STR const char *caller_fname, int line);

#define estrSuperEscapeString(ppOutString, pInString) estrSuperEscapeString_dbg(ppOutString, pInString, __FILE__, __LINE__)
#define estrSuperEscapeString_shorter(ppOutString, pInString) estrSuperEscapeString_shorter_dbg(ppOutString, pInString, __FILE__, __LINE__)
#define estrSuperUnescapeString(ppOutString, pInString) estrSuperUnescapeString_dbg(ppOutString, pInString, __FILE__, __LINE__)

//escape a string following the rules for JSON string literals (same rules as C literals too)
void estrCopyWithJSONEscaping(SA_PARAM_OP_OP_STR char **dst, SA_PARAM_NN_STR const char *src);

//truncates an eString at the first occurrence of a given character
void estrTruncateAtFirstOccurrence(SA_PARAM_OP_OP_STR char **ppString, char c);

//truncates an eString at the last occurrence of a given character
void estrTruncateAtLastOccurrence(SA_PARAM_OP_OP_STR char **ppString, char c);

//truncates an eString at the nth occurrence of a given character, if there is one
void estrTruncateAtNthOccurrence(SA_PARAM_OP_OP_STR char **ppString, char c, int n);

//removes a prefix from the string ending with the first occurrence of the given character. So calling this on
//"foo;bar;wacka" with c = ; would result in "bar;wacka", then "wacka", then ""
void estrRemoveUpToFirstOccurrence(SA_PARAM_OP_OP_STR char **ppString, char c);

//like above, but removes at the last occurrence (including c itself). Also differs in that it does nothing if there are no
//occurences
void estrRemoveUpToLastOccurrence(SA_PARAM_OP_OP_STR char **ppString, char c);


//truncates an eString at the first whitespace
void estrTruncateAtFirstWhitespace(SA_PARAM_OP_OP_STR char **ppString);


//replaces every character in an estring that is not alphanum with an _ (useful for making this legal filenames)
void estrMakeAllAlphaNumAndUnderscores(SA_PARAM_OP_OP_STR char **ppString);
void estrMakeAllAlphaNumAndUnderscoresEx(SA_PARAM_OP_OP_STR char **ppString, char *pOKExtraChars);

void estrTrimLeadingAndTrailingUnderscores(SA_PARAM_OP_OP_STR char **ppString);

//does simple character-count-based word wrapping (wraps only in white space, not punctuation)
void estrWordWrap_dbg(SA_PARAM_OP_OP_STR char **ppOutString, SA_PARAM_OP_STR const char* pInString, int iCharsPerLine, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrWordWrap(ppOutString, pInString, iCharsPerLine) estrWordWrap_dbg(ppOutString, pInString, iCharsPerLine, __FILE__, __LINE__)

//counts the occurrences of character c in the string
int estrCountChars(SA_PARAM_OP_OP_STR char **ppString, char c);

//converts an int number of bytes into "3.5 MEGs" or whatever
void estrMakePrettyBytesString_dbg(SA_PARAM_OP_OP_STR char **ppOutString, U64 iNumBytes, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrMakePrettyBytesString(ppOutString, iNumBytes) estrMakePrettyBytesString_dbg(ppOutString, iNumBytes, __FILE__, __LINE__)

//adds a prefix to every new line (that is, the beginning of the string, and after every \n\r)
void estrAddPrefixToLines_dbg(SA_PARAM_OP_OP_STR char **ppOutString, SA_PARAM_NN_STR const char *pPrefix, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrAddPrefixToLines(ppOutString, pPrefix) estrAddPrefixToLines_dbg(ppOutString, pPrefix, __FILE__, __LINE__)

// Encodes contents of pBuffer into a base64 encoded string and appends it to ppOutString
void estrBase64Encode(SA_PARAM_OP_OP_STR char **ppOutString, const void *pBuffer, int iBufSize);

// Decodes the base64 string in the buffer and sets the result in ppOutString (don't trust null termination)
void estrBase64Decode(SA_PARAM_OP_OP_STR char **ppOutString, const void *pBuffer, int iBufSize);

//takes "c:/foo/bar/wakka.txt" and puts "c:/foo/bar" in one string and "wakka.txt" in the other. This is currently
//naive and just looks for the last forward or backslash, so may not work correctly with things inside hogg files
//or what have you
//
//dir or filename can be NULL if you only want one
void estrGetDirAndFileName(SA_PARAM_NN_STR const char *pInString, SA_PARAM_OP_OP_STR char **ppOutDirName, SA_PARAM_OP_OP_STR char **ppOutFileName);

//like the above, but also divides main fname from extension
void estrGetDirAndFileNameAndExtension(SA_PARAM_NN_STR const char *pInstring, SA_PARAM_OP_OP_STR char **ppOutDirName, SA_PARAM_OP_OP_STR char **ppOutFileName, SA_PARAM_OP_OP_STR char **ppOutExt);


//---------------------------------------------------------------------------------
// EString: Private bits needed by stack functions below
//---------------------------------------------------------------------------------

#define ESTR_SHRINK_AMOUNT (EStrHeaderSize + EStrTerminatorSize) // Fudge total allocation size to powers of 2
#define ESTR_DEFAULT_SIZE (64 - ESTR_SHRINK_AMOUNT)
#define ESTR_HEADER "ESTR"

#define ESTR_TYPE_HEAP 0
#define ESTR_TYPE_ALLOCA 1
#define ESTR_TYPE_SCRATCH 2

//a "buffer" estring is one where the estring has been handed a buffer and told "use this buffer initially". If it 
//resizes over that it will resize to heap, just like any other resizing estring. The usage case here is where
//you have some structure that you create all the time that has an estring in it, and you can afford to waste
//some memory in order to make the creation/destruction of this structure very fast, so your initial structure
//creation points the starting estring to a buffer that is already part of your structure.
#define ESTR_TYPE_BUFFER 3

// If the size of this struct is changed in any way, including changing the size of the elements or adding or 
// removing elements, check estrStackCreate_dbg, estrHeapCreate_dbg, and estrBufferCreate to make sure that they
// Are still initializing the EString struct correctly.
typedef struct 
{
	char header[4];				// should contain "ESTR"
	unsigned int bufferLength;
	unsigned int stringLength;
	U8 estrType;
	char str[1];
} EString;

#define EStrHeaderSize offsetof(EString, str)
#define EStrTerminatorSize 1

__forceinline static void estrTerminateString(SA_PARAM_NN_VALID EString* estr)
{
	estr->str[estr->stringLength] = 0;
}

//---------------------------------------------------------------------------------
// EString: Stack Strings. Try to allocate the EString on the stack, going to heap on realloc
//---------------------------------------------------------------------------------

#define MAX_STACK_ESTR 10000 // maximum size of EString allowed to be allocated on stack
#define MIN_STACK_ESTR (1024 - EStrHeaderSize - 1)  // minimum size of stack EString(prevent reallocating)

// Creates an estring using the ScratchStack.
void estrStackCreate_dbg(SA_PARAM_OP_OP_STR char** str, unsigned int initSize, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrStackCreateSize(str, initSize) estrStackCreate_dbg(str, initSize, __FILE__, __LINE__)
#define estrStackCreate(str) estrStackCreate_dbg(str, MIN_STACK_ESTR, __FILE__, __LINE__)

/*#define estrStackCreate estrHeapCreate
#define estrAllocaCreate estrHeapCreate*/

// Creates an estring using alloca
#define estrAllocaCreate(strptr, initSize) \
	if((strptr) && (initSize) > 0 && (initSize) <= MAX_STACK_ESTR) {\
	int allocSize = ((initSize) < MIN_STACK_ESTR) ? MIN_STACK_ESTR : (initSize);\
	EString* estringptr = _alloca(EStrHeaderSize + EStrTerminatorSize + (allocSize));\
	memcpy(estringptr->header, "ESTR", 4);\
	estringptr->bufferLength = allocSize;\
	estringptr->stringLength = 0;\
	estringptr->estrType = ESTR_TYPE_ALLOCA;\
	estrTerminateString(estringptr);\
	*(strptr) = estringptr->str;\
	} else {\
	estrHeapCreate((strptr),(initSize),0);\
	}



__forceinline static EString* estrFromStr_sekret(SA_PARAM_NN_STR char* str)
{
	return (EString*)(str - EStrHeaderSize);
}

static __forceinline void estrConcat_dbg_inline(SA_PARAM_OP_OP_STR char** dst, SA_PARAM_NN_STR const char* src, unsigned int srcLength, SA_PARAM_NN_STR const char *caller_fname, int line)
{
	EString* estr;
	if(!dst)
		return;
	if(*dst){
		estr = estrFromStr_sekret(*dst);
		if(estr->bufferLength < estr->stringLength + srcLength + EStrTerminatorSize)
		{
			estrReserveCapacity_dbg(dst, estr->stringLength + srcLength, caller_fname, line);
			estr = estrFromStr_sekret(*dst);
		}
	}else{
		estrReserveCapacity_dbg(dst, srcLength, caller_fname, line);
		estr = estrFromStr_sekret(*dst);
	}

	memcpy(estr->str + estr->stringLength, src, srcLength);
	estr->stringLength += srcLength;
	estrTerminateString(estr);
}


static __forceinline void estrConcatChar_dbg_inline(SA_PARAM_OP_OP_STR char** dst, char c, SA_PARAM_NN_STR const char *caller_fname, int line)
{
	EString* estr;
	if(!dst)
		return;
	if(*dst)
	{
		estr = estrFromStr_sekret(*dst);
		if(estr->bufferLength < estr->stringLength + 1 + EStrTerminatorSize)
		{
			estrReserveCapacity_dbg(dst, estr->stringLength + 1, caller_fname, line);
			estr = estrFromStr_sekret(*dst);
		}
		// hopefully end up in this case most of the time...
	}
	else
	{
		estrReserveCapacity_dbg(dst, 1, caller_fname, line);
		estr = estrFromStr_sekret(*dst);
	}

	estr->str[estr->stringLength] = c;
	estr->stringLength++;
	estrTerminateString(estr);
}


static __forceinline void estrAppend2_dbg_inline(SA_PARAM_OP_OP_STR char** dst, SA_PARAM_OP_STR const char* src, SA_PARAM_NN_STR const char *caller_fname, int line)
{
	if(!src)
		return;

	estrConcat_dbg_inline(dst, src, (int)strlen(src), caller_fname, line);
}

//Does not have the normal protections against calling with a NULL estring, etc... 
//if you call this some number of times, you MUST call estrTerminateString_Unsafe_inline before you're done
static __forceinline void estrConcatCharNonTerminating_Unsafe_dbg_inline(SA_PARAM_OP_OP_STR char **dst, char c, SA_PARAM_NN_STR const char *caller_fname, int line)
{
	EString* estr = estrFromStr_sekret(*dst);

	if (estr->stringLength + 1 + EStrTerminatorSize > estr->bufferLength) {
		estrReserveCapacity_dbg(dst, estr->stringLength + 1, caller_fname, line);
		estr = estrFromStr_sekret(*dst);
	}

	estr->str[estr->stringLength++] = c;
}

//can only be used to shrink a string, or size it up to a size already known to fit inside its buffer
static __forceinline void estrSetSizeUnsafe_inline(char **dst, int iSize)
{
	EString* estr = estrFromStr_sekret(*dst);
	estr->stringLength = iSize;
	estrTerminateString(estr);
}

static __forceinline void estrTerminateString_Unsafe_inline(char **dst)
{
	EString* estr = estrFromStr_sekret(*dst);
	estrTerminateString(estr);
}

#define estrConcatCharNonTerminating_Unsafe_inline(dst, c) estrConcatCharNonTerminating_Unsafe_dbg_inline(dst, c, __FILE__, __LINE__) 

// disable scratchstack estrings for now
//#define estrStackCreate estrAllocaCreate


//takes in a normal string pointer and returns an estring pointer. Note that this returns a pointer, not a handle.
//This is rarely useful, but is good for a case where you have a bunch of normal strings and want to make an 
//earray of estrings
char *estrDup_dbg(SA_PARAM_NN_STR const char *pInStr, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrDup(pInStr) estrDup_dbg(pInStr, __FILE__, __LINE__)

// Duplicate a estring only if it's non-empty.
// This is like estrDup(), except that it folds "" to NULL.
char *estrDupIfNonempty(const char *pInStr);

// Duplicate functions from EString sources
char *strDupFromEString(SA_PRE_NN_NN_STR const char **ppString);
char *estrDupFromEString(SA_PRE_NN_NN_STR const char **ppString);



//replaces the count bytes starting at idx with another entire string
void estrReplaceRangeWithString_dbg(SA_PRE_NN_NN_STR char **ppString, int idx, int count, SA_PARAM_NN_STR const char *pOtherString, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrReplaceRangeWithString(ppString,  idx,  count, pOtherString) estrReplaceRangeWithString_dbg(ppString,  idx,  count, pOtherString, __FILE__, __LINE__)

//makes it easy to get a format string and var args and put them into an estring. The estring should start out NULL.
#define estrGetVarArgs(ppOutEString, pInStringArg) { if (pInStringArg){ va_list ap; va_start(ap, pInStringArg); estrConcatfv(ppOutEString, pInStringArg, ap); va_end(ap); }}

//assumes that the string is something like "-foo x y -bar z -happy". If passed in "-bar" will look for -bar and then
//remove it and everything up to the next - or end of string
void estrRemoveCmdLineStyleArgIfPresent_dbg(SA_PARAM_OP_OP_STR char **ppString, SA_PARAM_NN_STR const char *pCommand, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrRemoveCmdLineStyleArgIfPresent(ppString, pCommand) estrRemoveCmdLineStyleArgIfPresent_dbg(ppString, pCommand, __FILE__, __LINE__)


//given an earray of strings, concats them one by one onto the end of an estring, adding a separator between them (but not
//before the first or after the last). Typical separator string would be ", "
void estrConcatSeparatedStringEarray_dbg(SA_PARAM_OP_OP_STR char **ppString, SA_PARAM_OP_OP_STR char ***pppEarray, SA_PARAM_NN_STR const char *pSeparator, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrConcatSeparatedStringEarray(ppString, pppEarray, pSeparator) estrConcatSeparatedStringEarray_dbg(ppString, pppEarray, pSeparator, __FILE__, __LINE__)

//finds all occurrences of "FOO(x)", where "FOO" is the macro name passed in, and x is any parentheses-balanced set of characters. Then either replaces it with "x" (on) or with "" (off).
//note that "FOO (x)" will be ignored.
//Returns the number of macros replaced (regardless of whether replaced with "x" or "")
int estrResolveOnOffParenMacro_dbg(SA_PARAM_OP_OP_STR char **str, SA_PARAM_NN_STR const char* pMacroName, bool bOn, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrResolveOnOffParenMacro(str, pMacroName, bOn) estrResolveOnOffParenMacro_dbg(str, pMacroName, bOn, __FILE__, __LINE__)

/*suppose you have a multi-line estring that looks like this:
name/tdesc/thealth
bob\ta guy\t6
joe\ta different guy\t500
SuperLongNameCrazyMan\tA guy with a very long name\t4
presumably you put the tabs in so the columns would all line up. But if the things in each column have different lengths, then,
depending on the tab width, your clever tabbing all goes for naught. This function chops up an estring like that, figures out the
max length in each column, and reassembles it using only spaces to do the column alignment. It ignores any line with no tabs.

iPadding is the number of spaces that will end up between the longest word in each column and the beginning of the next
*/
void estrFixupTabsIntoSpacesAcrossMultipleLines_dbg(SA_PARAM_OP_OP_STR char **ppString, int iPadding, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrFixupTabsIntoSpacesAcrossMultipleLines(ppString, iPadding) estrFixupTabsIntoSpacesAcrossMultipleLines_dbg(ppString, iPadding, __FILE__, __LINE__)

//NOTE NOTE NOTE this is not heavily tested
void estrGetEnvironmentVariable_dbg(SA_PARAM_OP_OP_STR char **ppString, const char *pEnvVarName, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrGetEnvironmentVariable(ppString, pEnvVarName) estrGetEnvironmentVariable_dbg(ppString, pEnvVarName, __FILE__, __LINE__)

void estrTokenize_dbg(char ***estrDest, const char *tokens, const char *src, SA_PARAM_NN_STR const char *caller_fname, int line);
#define estrTokenize(estrDest, tokens, src) estrTokenize_dbg(estrDest, tokens, src, __FILE__, __LINE__)

#endif
