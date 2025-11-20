#include "wininclude.h"
#include "StringUtil.h"
#include "TextFilter.h"
#include "simpleparser.h"
#include "estring.h"
#include "ScratchStack.h"
#include "timing.h"
#include "mathutil.h"
#include "Message.h"
#include "StringFormat.h"
#include "AppLocale.h"
#include "AutoGen/StringUtil_h_ast.h"
#include "AutoGen/StringUtil_c_ast.h"
#include "GlobalTypes.h" // For IsClient() and such
#include "UtilitiesLib.h" // For GetDirForBaseConfigFiles()
#include "file.h" // For isDevelopmentMode()
#include "tokenstore.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

// Technically incorrect. UTF-8 was restricted in 2003 to not allow
// the last of these characters (the 5 and 6 long ones). However,
// no harm in leaving them in... right?
const char g_UTF8CodepointLengths[256] = {
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,6,6,1,1
};

static const U32 s_OffsetsFromUTF8[6] = {
	0x00000000UL, 0x00003080UL, 0x000E2080UL, 
	0x03C82080UL, 0xFA082080UL, 0x82082080UL
};

#if _PS3
const uint8_t utf8_bits2bytes[33] = {
    1,
    1,1,1,1,1,1,1,
    2,2,2,2,
    3,3,3,3,3,
    4,4,4,4,4,
    5,5,5,5,5,
    6,6,6,6,6,
    7
};
#endif

const char *g_pchPlainWordDelimeters = ",.(){}[]?|\"\\/:;!@#$%^&*-_`~+= \n";
const char *g_pchCommandWordDelimiters = " \n/;,";

// FIXME: This file definitely needs some kind of automated test suite,
// especially since most of the functions are security-sensitive.

AUTO_RUN;
int disableCRTFillThreshold(void)
{
#if !_PS3
	// This disables doing a pre-fill of 0xFD when calling secure CRT string functions like strncpy_s.
	_CrtSetDebugFillThreshold(0);
#endif
	return 1;
}


/* Function UTF8ToWideStrConvert()
 *	This function will return the given UTF-8 string.
 *	'outputBufferMaxLen' is the total size in 16-bit words of 'outBuffer'.  This can be zero to determine the character count.
 *  The return value is number of characters (16-bit words) needed for conversion not including the terminating null character.
 */
int UTF8ToWideStrConvert(const unsigned char *str, unsigned short *outBuffer, int outBufferMaxLength) {
	int result;
	int bufferSize;
	int strSize;

	// If either the outBuffer or the out buffer length is 0,
	// the user is asking how long the string will be after conversion.
	if(!outBuffer || !outBufferMaxLength){
		outBuffer = NULL;
		bufferSize = 0;

		if('\0' == *str)
			return 0;
	}
	else{
		bufferSize = outBufferMaxLength;

		// If the given string is an empty string, pass back an empty string also.
		if('\0' == *str){
			outBuffer[0] = '\0';
			return 0;
		}
	}
	
	strSize = (int)strlen(str);//(bufferSize ? min(strlen(str), bufferSize) : strlen(str));

#if _PS3
    result = 0;

    {
        if(!bufferSize) {
            int j=0;
           
            while(j<strSize) {
                if( (str[j] & 0xc0) == 0xc0 ) {
                    int bytes = __cntlzw(0xff & ~str[j])-24;
                    j += bytes;
                } else {
                    j++;
                }
                result++;
            }

        } else {
            int j=0;

            assert(outBuffer);
            
            while(j<strSize) {
                uint32_t character;
                if( (str[j] & 0xc0) == 0xc0 ) {
                    int bytes = __cntlzw(0xff & ~str[j])-24;
                    int mask = __BIT(7-bytes)-1;
                    int i;

                    character = str[j]&mask;
                    for(i=1; i<bytes; i++)
                        character = (character<<6) | (str[j+i]&0x3f);

                    j += bytes;
                } else {
                    character = str[j];
                    j++;
                }
                
                if(result+1 < bufferSize)
                    outBuffer[result++] = character;
            }
        }
    }
#else
	result = MultiByteToWideChar(CP_UTF8, 0, str, strSize, outBuffer, bufferSize);

	if(!result)
#if !PLATFORM_CONSOLE
		printWinErr("MultiByteToWideChar", __FILE__, __LINE__, GetLastError());
#else
		assert(0);
#endif
#endif

    if(outBuffer)
	{
		assert(result < outBufferMaxLength);

		outBuffer[result] = 0;
	}

	// Do not count the null terminating character as part of the string.
	return result;
}


int UTF8ToWideStrConvert_Verbose(const unsigned char *str, unsigned short *outBuffer, int outBufferMaxLength) {
	int result;
	int bufferSize;
	int strSize;

	printf("In UTF8ToWideStrConvert_Verbose, converting <<%s>>, bufferMaxLength %d\n", str, outBufferMaxLength);

	// If either the outBuffer or the out buffer length is 0,
	// the user is asking how long the string will be after conversion.
	if(!outBuffer || !outBufferMaxLength){
		outBuffer = NULL;
		bufferSize = 0;

		if('\0' == *str)
		{
			printf("First return\n");
			return 0;
		}
	}
	else{
		bufferSize = outBufferMaxLength;

		// If the given string is an empty string, pass back an empty string also.
		if('\0' == *str){
			outBuffer[0] = '\0';
			printf("Second return\n");
			return 0;
		}
	}
	
	strSize = (int)strlen(str);//(bufferSize ? min(strlen(str), bufferSize) : strlen(str));

	
	printf("Size: %d\n", strSize);

	result = MultiByteToWideChar(CP_UTF8, 0, str, strSize, outBuffer, bufferSize);
	printf("Result: %d\n", result);


	if(!result)
#if !PLATFORM_CONSOLE
		printWinErr("MultiByteToWideChar", __FILE__, __LINE__, GetLastError());
#else
		assert(0);
#endif


    if(outBuffer)
	{
		assert(result < outBufferMaxLength);
		outBuffer[result] = 0;
	}

	// Do not count the null terminating character as part of the string.
	return result;
}




unsigned short *UTF8ToWideStrConvertAndRealloc_dbg(const unsigned char *str, unsigned short *wstr, U32 *bufferSize MEM_DBG_PARMS)
{
	U32 newBufferSize = (bufferSize ? *bufferSize : 0);
	U32 len = UTF8ToWideStrConvert(str, NULL, 0);
	if (len + 1 > newBufferSize || !wstr)
	{
		newBufferSize = len + 1;
		wstr = srealloc(wstr, sizeof(unsigned short) * newBufferSize);
	}
	UTF8ToWideStrConvert(str, wstr, newBufferSize);
	if (bufferSize)
		*bufferSize = newBufferSize;
	return wstr;
}

// TODO: This doesn't handle codepoints U+10000 and higher that map to two wchars
unsigned short UTF8ToWideCharConvert(const unsigned char *str){
	int result;
	unsigned short character = 0;

	if( str && *str )
	{
		// quick optimization for the common case...
        if ((*str & 0xc0) == 0xc0) {
#if _PS3
            {
                int i;
                int bytes = __cntlzw(0xff & ~str[0])-24;
                int mask = __BIT(7-bytes)-1;

                character = str[0]&mask;
                for(i=1; i<bytes; i++)
                    character = (character<<6) | (str[i]&0x3f);
            }
#else
		    result = MultiByteToWideChar(CP_UTF8, 0, str, UTF8GetCodepointLength(str), &character, 1);
		    if(!result)
#if !PLATFORM_CONSOLE
			    printWinErr("MultiByteToWideChar", __FILE__, __LINE__, WSAGetLastError());
#else
    			assert(0);
#endif
#endif
        } else {
            character = *str;
        }
	}
	
	return character;
}

// Converts the UTF-8 code point to the hex value for the code point
U32 UTF8ToCodepoint(const char *str)
{
	int cp_len;
	U32 cp_value = 0;
	int i;

	if (!str || !*str)
		return 0;
	cp_len = UTF8GetCodepointLength(str);

	for (i=cp_len-1; i>=0; i--)
	{
		if (i == 0)
		{
			if (cp_len == 1)
				cp_value = (U32) str[i];
			else
			{
				U32 bitmask = (1 << (7 - cp_len)) - 1;
				U32 byte = (U32) (str[i] & bitmask);
				byte <<= 6*(cp_len-i-1);
				cp_value |= byte;
			}
		}
		else
		{
			U32 byte = (U32) (str[i] & 0x3f);
			byte <<= 6*(cp_len-i-1);
			cp_value |= byte;
		}
	}	
	return cp_value;
}

bool UTF8StringIsValid(const unsigned char *str, const char **err)
{
	int cont = 0;
	if (verify(str))
	{
		for (; *str; ++str)
		{
			unsigned char c = *str;
			switch ( cont )
			{
			case 0:
				if(c < 0x80)		// 7-bit ASCII
					cont = 0;
				else if(c < 0xC0)	// continuation char, invalid
				{
					if (err)
						*err = str;
					return false;
				}
				else if(c < 0xE0)	// two leading bits
					cont = 1;
				else if(c < 0xF0)	// three leading bits
					cont = 2;
				else
					cont = 3;
				break;
			case 1:
			case 2:
			case 3:
			case 4:
				if((c&0xC0)!=0x80) // need 10 for leading bits
				{
					if (err)
						*err = str;
					return false;
				}
				cont--;
				break;
			default:
				verify(0 && "invalid switch value.");
			};
		}
	}
	else
	{
		if (err)
			*err = str;
		return false;
	}

	return cont == 0;
}

int WideToEncodingConvert(const unsigned short* str, unsigned char* outBuffer, int outBufferMaxLength, U32 encoding)
{
	int result;
	int bufferSize;
    int strSize;

	// If either the outBuffer or the out buffer length is 0,
	// the user is asking how long the string will be after conversion.
	if(!outBuffer || !outBufferMaxLength){
		outBuffer = NULL;
		bufferSize = 0;

		if('\0' == *str)
			return 0;
	}
	else{
		bufferSize = outBufferMaxLength;

		// If the given string is an emtpy string, pass back an emtpy string also.
		if('\0' == *str){
			outBuffer[0] = '\0';
			return 0;
		}
	}

    strSize = (int)wcslen(str);

#if _PS3
    result = 0;

    if(encoding == CP_UTF8) {
        if(!bufferSize) {
            int j;
            for(j=0; j<strSize; j++) {
                int val = str[j];
                int bytes = utf8_bits2bytes[32-__cntlzw(val)];

                result += bytes;
            }

        } else {
            int j;

            assert(outBuffer);

            for(j=0; j<strSize; j++) {
                int val = str[j];
                int bytes = utf8_bits2bytes[32-__cntlzw(val)];

                if(result+bytes < bufferSize) {
                    if(bytes > 1) {
                        int mask = __BIT(7-bytes)-1;
                        int i;

                        for(i=bytes; --i>0; ) {
                            outBuffer[result+i] = (val&0x3f) | 0x80;
                            val >>= 6;
                        }

                        outBuffer[result] = (val&mask) | (~mask<<1);
                    } else
                        outBuffer[result] = val;
                    result += bytes;
                }
            }
        }
    } else {
        if(!bufferSize) {
            result = strSize;
        } else {
            int j;

            assert(outBuffer);

            for(j=0; j<strSize; j++) {
                int val = str[j];

                if(result+1 < bufferSize) {
                    outBuffer[result++] = val;
                }
            }
        }
    }
#else
	result = WideCharToMultiByte(encoding, 0, str, strSize, outBuffer, bufferSize, NULL, NULL);

	if(!result)
#if !PLATFORM_CONSOLE
	printWinErr("WideCharToMultiByte", __FILE__, __LINE__, WSAGetLastError());

#else
		assert(0);
#endif
#endif

	if(outBuffer)
	{
		assert(result < outBufferMaxLength);
		outBuffer[result] = 0;
	}

	return result;
}

int UTF8ToACP(const char *str, char *out, int len)
{
	wchar_t *wstr;
	int size = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
	wstr = malloc(size * sizeof(wchar_t));
	MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr, size);
	size = WideCharToMultiByte(CP_ACP, 0, wstr, -1, out, len, NULL, NULL);
	free(wstr);
	return size;
}

int ACPToUTF8(const char* str, char *out, int len)
{
	wchar_t *wstr;
	int size = MultiByteToWideChar(CP_ACP, 0, str, -1, NULL, 0);
	wstr = malloc(size * sizeof(wchar_t));
	MultiByteToWideChar(CP_ACP, 0, str, -1, wstr, size);
	size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, out, len, NULL, NULL);
	free(wstr);
	return size;
}

int WideToUTF8StrConvert(const wchar_t* str, char* outBuffer, int outBufferMaxLength)
{
	return WideToEncodingConvert(str, outBuffer, outBufferMaxLength, CP_UTF8);
}

char* WideToUTF8CharConvert(unsigned short character){
	// Max UTF8 character length should be 6 bytes, add one for null terminating character.
	static char outputBuffer[7];
	unsigned short unicodeString[2];

	unicodeString[0] = character;
	unicodeString[1] = '\0';

	WideToUTF8StrConvert(unicodeString, outputBuffer, 7);
	return outputBuffer;
}

char *WideToUTF8StrTempConvert(const unsigned short *str, int *newStrLength)
{
	int strLength;
	static char outputBuffer[4096];

	strLength = WideToUTF8StrConvert(str, outputBuffer, 4096);
	if (newStrLength)
		*newStrLength = strLength;
	return outputBuffer;
}

const char *WideToUTF8CodepointConvert(unsigned short codepoint)
{
	// Max UTF-8 character length should be 6 bytes, add one for null terminating character.
	static char outputBuffer[7];
	unsigned short unicodeString[2] = {codepoint, 0};
	WideToUTF8StrConvert(unicodeString, outputBuffer, 7);
	return outputBuffer;
}

char *UTF8GetNextCodepoint(const unsigned char *str)
{
	if (devassert(str))
	{
		S32 iLen = str[0] ? UTF8GetCodepointLength(str) : 0;
		while (iLen-- && *str)
			str++;
		return (char *)str;
	}
	else
		return NULL;
}

char *UTF8GetCodepoint(const unsigned char *str, S32 index)
{
	int i;

	for(i = 0; i < index; i++)
	{
		if(!*str)
			return NULL;
		else
		{
			S32 iLen = UTF8GetCodepointLength(str);
			while (iLen-- && *str)
				str++;
		}
	}

	return (char *)str;
}

S32 UTF8PointerToCodepointIndex(const unsigned char *str, const char *codepoint)
{
	S32 offset;
	for (offset = 0; str <= (unsigned char*)codepoint; str += UTF8GetCodepointLength(str))
		offset++;
	return offset - 1;
}

S32 UTF8GetCodepointOffset(const unsigned char *str, int index)
{
	char *ptr = UTF8GetCodepoint(str, index);
	return ptr ? (ptr - (char*)str) : -1;
}

U32 UTF8GetLength(const unsigned char *str)
{
	int count = 0;
	if (devassert(str))
	{
		while (str && *str)
		{
			S32 iLen = UTF8GetCodepointLength(str);
			while (iLen-- && *str)
				str++;
			count++;
		}
	}
	return count;
}

// Copies the first numCodePointsToCopy codepoints from src into dest. Does NOT append a trailing '\0'.
// If the end of the src is found before numCodePointsToCopy codepoints have been copied,
// dest is padded with zeros until a total of numCodePointsToCopy codepoints have been written to it.
// No null-character is implicitly appended to the end of dest, so dest will only be null-terminated if 
// the codepoint length of src is less than numCodePointsToCopy.
//
// NOTE: This rest of this description is similar, but NOT the same as strncpy().
//
// Call with dest=NULL to return the required number of bytes that you must allocate for the dest buffer
// before calling this again.
//
// Returns: Number of BYTES that will be required to complete the copy.
size_t UTF8strncpy(unsigned char *dest, const unsigned char *src, size_t numCodePointsToCopy)
{
	const unsigned char* next;
	if( src == NULL )
		return 0;

	next = src;
	while(*next && numCodePointsToCopy)
	{
		numCodePointsToCopy--;
		next += UTF8GetCodepointLength(next);
	}

	if( dest != NULL )
	{
		memmove(dest, src, next-src);
		memset(dest+(next-src), '\0', numCodePointsToCopy);
	}

	return (next - src) + numCodePointsToCopy;
}

void UTF8RemoveCodepoints(unsigned char *str, int beginIndex, int endIndex)
{
	int characterCount = 0;
	char* beginTruncatePosition = NULL;
	char* endTruncatePosition = NULL;
	int copySize;

	// shortcut out
	if( endIndex - beginIndex <= 0 )
	{
		return;
	}

	// Find the character marked by the begin index.
	beginTruncatePosition = str;

	while(*beginTruncatePosition){
		if(beginIndex <= characterCount)
			break;
		
		characterCount++;
		beginTruncatePosition += UTF8GetCodepointLength(beginTruncatePosition);
	}

	// Find the character before the one marked by the end index.
	endTruncatePosition = beginTruncatePosition;
	while(*endTruncatePosition){
		if(endIndex <= characterCount)
			break;
		
		characterCount++;
		endTruncatePosition += UTF8GetCodepointLength(endTruncatePosition);
	}

	copySize = (int)strlen(endTruncatePosition) + 1;
	memmove(beginTruncatePosition, endTruncatePosition, copySize);
}

char *strnchr(const char *pString, size_t iLength, char c)
{
	while (*pString && iLength)
	{
		if (*pString == c)
		{
			return (char *)pString;
		}

		pString++;
		iLength--;
	}

	return NULL;
}

// Find a substring.
const char *strnstr(const char *pHaystack, size_t uLength, const char *pNeedle)
{
	const char *i;
	size_t len = strlen(pNeedle);
	for (i = pHaystack; i + len <= pHaystack + uLength; ++i)
	{
		if (!memcmp(i, pNeedle, len))
			return i;
	}
	return NULL;
}

const char *strNextWordDelim(SA_PARAM_NN_STR const char *str, SA_PARAM_OP_STR const char *pchDelimiters)
{
	if (!pchDelimiters || !*pchDelimiters) {
		pchDelimiters = g_pchPlainWordDelimeters;
	}

	while (*str && strchr(pchDelimiters, *str))
		str++;
	while (*str && !strchr(pchDelimiters, *str))
		str++;
	while (*str && strchr(pchDelimiters, *str))
		str++;
	return str;
}

const char *strWordEndDelim(SA_PARAM_NN_STR const char *str, SA_PARAM_OP_STR const char *pchDelimiters)
{
	if (*str) {
		if (!pchDelimiters) {
			pchDelimiters = g_pchPlainWordDelimeters;
		}

		if (strchr(pchDelimiters, *str)) {
			// Return position of last separators in sequence of word separators
			while (*str && strchr(pchDelimiters, *str)) {
				str++;
			}
		} else {
			// Return position of last non-separator in sequence of non-word separators
			while (*str && !strchr(pchDelimiters, *str)) {
				str++;
			}
		}

		// Bump str back to the previous character since that was the last
		// one that passed our test.
		str--;
	}

	return str;
}

U32 UTF8CodepointOfNextWordDelim(const char *str, U32 from, SA_PARAM_OP_STR const char *pchDelimiters)
{
	const char *pchCursor = UTF8GetCodepoint(str, from);
	return pchCursor ? UTF8PointerToCodepointIndex(str, strNextWordDelim(pchCursor, pchDelimiters)) : UTF8GetLength(str);
}

// Note: This is a slightly different implementation than
//       the delim version.  At the time I wrote the delim 
//       version I didn't have time to test whether it had
//       behavior that wouldn't break users of this version, 
//       so I left this here as is.  The delim version differs
//       in that it assumes 'from' is a cursor which points between
//       characters and that we want include the character just before
//       the cursor.  It's also different in that if you're in the middle
//       of a word, it'll choose that word as the previous since it
//       begins before the cursor.
U32 UTF8CodepointOfPreviousWord(const char *str, U32 from)
{
	const char *pchCursor = UTF8GetCodepoint(str, from);
	const char *pchTracking = strNextWord(str);
	const char *pchTrailing = NULL;

	if (from && from == UTF8GetLength(str))
		pchCursor = UTF8GetCodepoint(str, from - 1);

	while (pchTracking < pchCursor)
	{
		pchTrailing = pchTracking;
		pchTracking = strNextWord(pchTracking);
	}
	return (pchCursor && pchTrailing) ? UTF8PointerToCodepointIndex(str, pchTrailing) : 0;
}

U32 UTF8CodepointOfPreviousWordDelim(SA_PARAM_NN_STR const char *str, U32 from, SA_PARAM_OP_STR const char *pchDelimiters)
{
	const char *pchCursor = UTF8GetCodepoint(str, from);
	const char *pchTracking = strNextWordDelim(str, pchDelimiters);
	const char *pchTrailing = NULL;

	while (*pchTracking && pchTracking < pchCursor)
	{
		pchTrailing = pchTracking;
		pchTracking = strNextWordDelim(pchTracking, pchDelimiters);
	}

	if (pchCursor && pchTrailing) {
		// Found the previous word
		return UTF8PointerToCodepointIndex(str, pchTrailing);
	}

	// Didn't find it.  The following handles the edge case
	// where the string starts with delimiters.
	pchTrailing = str;
	while (pchTrailing && *pchTrailing && pchTrailing < pchCursor) {
		if (!pchDelimiters || !*pchDelimiters) {
			pchDelimiters = g_pchPlainWordDelimeters;
		}

		if (!strchr(pchDelimiters, *pchTrailing)) {
			return UTF8PointerToCodepointIndex(str, pchTrailing);
		}
		pchTrailing += UTF8GetCodepointLength(pchTrailing);
	}

	// The string is either empty or completely full of delimiters up to the cursor
	return 0;
}

U32 UTF8CodepointOfWordEndDelim(SA_PARAM_NN_STR const char *str, U32 from, SA_PARAM_OP_STR const char *pchDelimiters)
{
	const char *pchFrom = UTF8GetCodepoint(str, from);
	return pchFrom ? UTF8PointerToCodepointIndex(str, strWordEndDelim(pchFrom, pchDelimiters)) : UTF8GetLength(str);
}

// we don't need a state for being inside textparser-style strings, because they are all read
// as a single bit of code, not as part of a loop
typedef enum 
{
	IN_WHITESPACE,
	IN_NORMAL_TOKEN,
	IN_NORMAL_STRING, // "foo \"blah\" "
} enumTokenizeLineState;

int TokenizeLineRespectingStrings_Count(char **ppOutTokens, char *pInString, int iOutTokenCount)
{
	int iCurTokenNum = 0; //the index of the current token being read, or the next token if we're between tokens
	enumTokenizeLineState eState = IN_WHITESPACE;
	char *pReadHead = pInString;

	if (!pInString)
	{
		return 0;
	}

	while (1)
	{
		switch (eState)
		{
		case IN_WHITESPACE:
			if (*pReadHead == 0)
			{
				return iCurTokenNum;
			}

			if (IS_WHITESPACE(*pReadHead))
			{
				pReadHead++;
			}
			else if (*pReadHead == '\"')
			{
				assertmsg(iCurTokenNum < iOutTokenCount, "Token overflow");
				ppOutTokens[iCurTokenNum] = pReadHead;
				eState = IN_NORMAL_STRING;
				pReadHead++;
			}
			else if (*pReadHead == '<' && *(pReadHead + 1) == '&')
			{
				char *pEndQuotes;
				char *pCharAfterEndQuotes;
				assertmsg(iCurTokenNum < iOutTokenCount, "Token overflow");
				ppOutTokens[iCurTokenNum] = pReadHead;


				pEndQuotes = strstr(pReadHead + 2, "&>");
				assertmsg(pEndQuotes, "Malformed textparser-style quoted string");
				pCharAfterEndQuotes = pEndQuotes + 2;

				if (*pCharAfterEndQuotes == 0)
				{
					return iCurTokenNum + 1;
				}
				else if (IS_WHITESPACE(*pCharAfterEndQuotes))
				{
					*pCharAfterEndQuotes = 0;
					iCurTokenNum++;
					pReadHead = pCharAfterEndQuotes + 1;
				}
				else
				{
					assertmsg(0, "Malformed string - expected whitespace or null terminator after <& &>");
				}
			}
			else 
			{
				assertmsg(iCurTokenNum < iOutTokenCount, "Token overflow");
				ppOutTokens[iCurTokenNum] = pReadHead;
				eState = IN_NORMAL_TOKEN;
				pReadHead++;
			}
			break;

		case IN_NORMAL_TOKEN:
			if (*pReadHead == 0)
			{
				return iCurTokenNum + 1;
			}
			else if (IS_WHITESPACE(*pReadHead))
			{
				*pReadHead = 0;
				iCurTokenNum++;
				pReadHead++;
				eState = IN_WHITESPACE;
			}
			else
			{
				pReadHead++;
			}
			break;

		case IN_NORMAL_STRING:
			if (*pReadHead == 0)
			{
				assertmsg(0, "Malformed string = found EOL inside \"-string");
			}
			else if (*pReadHead == '\"')
			{
				char *pCharAfterEndQuotes = pReadHead + 1;

				if (*pCharAfterEndQuotes == 0)
				{
					return iCurTokenNum + 1;
				}
				else if (IS_WHITESPACE(*pCharAfterEndQuotes))
				{
					*pCharAfterEndQuotes = 0;
					iCurTokenNum++;
					pReadHead = pCharAfterEndQuotes + 1;
					eState = IN_WHITESPACE;
				}
				else
				{
					assertmsg(0, "Malformed string - expected whitespace or null terminator after close quotes");
				}
			}
			else if (*pReadHead == '\\')
			{
				assertmsg(*(pReadHead+1) != 0, "Malformed string - apparent escaped null terminator");
				pReadHead+= 2;
			}
			else
			{
				pReadHead++;
			}
			break;
		}
	}
}




char *GetEntireLine(char *pPoint, char *pBuffer, int *piLineLength)
{
	char *pBeginningOfLine = pPoint;
	char *pEndOfLine = pPoint;

	while (pBeginningOfLine >= pBuffer && *pBeginningOfLine != '\n' && *pBeginningOfLine != '\r')
	{
		pBeginningOfLine--;
	}

	while (*pEndOfLine && *pEndOfLine != '\n' && *pEndOfLine != '\r')
	{
		pEndOfLine++;
	}

	*piLineLength = pEndOfLine - pBeginningOfLine - 1;
	return pBeginningOfLine + 1;
}




char *strrstr(char *pString, char *pToFind)
{
	char *pRetVal = NULL;

	do
	{
		char *pTemp = strstr(pString, pToFind);

		if (!pTemp)
		{
			return pRetVal;
		}

		pRetVal = pTemp;
		pString = pTemp + 1;
	} while (1);
}

static int minimum(int a,int b,int c)
/*Gets the minimum of three values*/
{
  int min=a;
  if(b<min)
    min=b;
  if(c<min)
    min=c;
  return min;
}

int levenshtein_distance(const char *s, const char*t)
/*Compute levenshtein distance between s and t*/
{
  //Step 1
  int k,i,j,n,m,cost,*d,distance;

  if (!s || !t)
  {
	  return -1;
  }

  PERFINFO_AUTO_START_FUNC();

  n=(int)strlen(s); 
  m=(int)strlen(t);
  if(n!=0&&m!=0)
  {
    d=ScratchAlloc((sizeof(int))*(m+1)*(n+1));
    m++;
    n++;
    //Step 2	
    for(k=0;k<n;k++)
	d[k]=k;
    for(k=0;k<m;k++)
      d[k*n]=k;
    //Step 3 and 4	
    for(i=1;i<n;i++)
      for(j=1;j<m;j++)
	{
        //Step 5
        if(s[i-1]==t[j-1])
          cost=0;
        else
          cost=1;
        //Step 6			 
        d[j*n+i]=minimum(d[(j-1)*n+i]+1,d[j*n+i-1]+1,d[(j-1)*n+i-1]+cost);
      }
    distance=d[n*m-1];
    ScratchFree(d);
	PERFINFO_AUTO_STOP();
    return distance;
  }
  else 
  {
	PERFINFO_AUTO_STOP();
    return -1; //a negative return value means that one or both strings are empty.
  }
}



void DoVariableListSeparation_dbg(char ***pppOutList, char *pInString, bool bForceUseNewlines MEM_DBG_PARMS)
{
	removeTrailingWhiteSpaces(pInString);

	if (strchr(pInString, '\n'))
	{
		bForceUseNewlines = true;
	}

	if (bForceUseNewlines)
	{
		char *pFirstNewLine;

		while ((pFirstNewLine = strchr(pInString, '\n')))
		{
			*pFirstNewLine = 0;
			if (pFirstNewLine > pInString && *(pFirstNewLine - 1) == '\r')
			{
				*(pFirstNewLine - 1) = 0;
			}
			seaPush(pppOutList, strdup_dbg(pInString, caller_fname, line));
			pInString = pFirstNewLine + 1;
		}

		seaPush(pppOutList, strdup_dbg(pInString, caller_fname, line));
	}
	else
	{
		char *pFirstComma;
		char *pDupString;

		while ((pFirstComma = strchr(pInString, ',')))
		{
			*pFirstComma = 0;
			pDupString = strdup_removeWhiteSpace_dbg(pInString MEM_DBG_PARMS_CALL);

			seaPush(pppOutList, pDupString);
			pInString = pFirstComma + 1;
		}

		pDupString = strdup_removeWhiteSpace_dbg(pInString MEM_DBG_PARMS_CALL);

		seaPush(pppOutList, pDupString);
	}

}



char *strdup_removeWhiteSpace_dbg(const char *pInString MEM_DBG_PARMS)
{
	int iStartingLen = (int)strlen(pInString);
	char *pRetString;
	int iIndex;
	int iNewLen;

	int iTrailingCount = 0;
	int iLeadingCount = 0;

	if (iStartingLen == 0)
	{
		pRetString = smalloc(1);
		*pRetString = 0;
		return pRetString;
	}

	iIndex = iStartingLen - 1;

	while (iIndex >= 0 && IS_WHITESPACE(pInString[iIndex]))
	{
		iIndex--;
		iTrailingCount++;
	}

	if (iTrailingCount == iStartingLen)
	{
		pRetString = smalloc(1);
		*pRetString = 0;
		return pRetString;
	}

	while (IS_WHITESPACE(pInString[iLeadingCount]))
	{
		iLeadingCount++;
	}

	iNewLen = iStartingLen - iTrailingCount - iLeadingCount;
	pRetString = smalloc(iNewLen + 1);
	pRetString[iNewLen] = 0;
	memcpy(pRetString, pInString + iLeadingCount, iNewLen);

	return pRetString;
}

char *strdup_uncommented_dbg(const char *src, StrdupStripParams params, const char *caller_fname, int line)
{
	const char *s;
	int size=0;
	bool inComment;
	char *ret, *out;
	char lastchar;

	inComment = false;
	lastchar='\n';
	for (s=src; *s; s++)
	{
		switch (*s) {
			xcase '\r':
			acase '\n':
				inComment = false;
			xcase '#':
				inComment = true;
			xcase '/':
				if (s[1] == '/')
					inComment = true;
		}
		if (!inComment &&
			!((params & STRIP_EMPTY_LINES) && *s=='\n' && lastchar=='\n') && // Not two consecutive carriage returns
			!((params & STRIP_LEADING_SPACES) && (*s==' ' || *s=='\t') && lastchar=='\n')) // Not leading whitespace
		{
			lastchar = *s;
			size++;
		}
	}

	ret = out = smalloc(size+1);
	inComment = false;
	lastchar='\n';
	for (s=src; *s; s++)
	{
		switch (*s) {
			xcase '\r':
			acase '\n':
				inComment = false;
			xcase '#':
				inComment = true;
			xcase '/':
				if (s[1] == '/')
					inComment = true;
		}
		if (!inComment &&
			!((params & STRIP_EMPTY_LINES) && *s=='\n' && lastchar=='\n') && // Not two consecutive carriage returns
			!((params & STRIP_LEADING_SPACES) && (*s==' ' || *s=='\t') && lastchar=='\n')) // Not leading whitespace
		{
			lastchar = *s;
			*out++ = *s;
		}
	}
	*out++ = '\0';
	assert(out == ret + size + 1);
	return ret;
}

// EArrayItemCopier-compatible strdup()
void* strdupFunc( const void* str )
{
	return strdup(str);
}

void strFreeFunc(char* str)
{
	free(str);
}

int strcmp_safe(const char *pStr1, const char *pStr2)
{
	bool b1Exists = pStr1 && pStr1[0];
	bool b2Exists = pStr2 && pStr2[0];

	if (b1Exists != b2Exists)
	{
		return 1;
	}

	if (!b1Exists)
	{
		return 0;
	}

	return strcmp(pStr1, pStr2);
}

int stricmp_safe(const char *pStr1, const char *pStr2)
{
	bool b1Exists = pStr1 && pStr1[0];
	bool b2Exists = pStr2 && pStr2[0];

	if (b1Exists != b2Exists)
	{
		return 1;
	}

	if (!b1Exists)
	{
		return 0;
	}

	return stricmp(pStr1, pStr2);
}

//returns true on success, false on failure
bool StringToInt(const char *pString, int *pInt)
{
	char *pTemp;
	errno = 0;
	*pInt = (int)strtol(pString, &pTemp, 10);

	if (pString == pTemp || errno)
	{
		return false;
	}

	return true;
}

//returns true on success, false on failure
bool StringToUint(const char *pString, U32 *pInt)
{
	char *pTemp;
	errno = 0;
	*pInt = (int)strtoul(pString, &pTemp, 10);

	if (errno)
	{
		return false;
	}

	return true;
}


//returns true on success, false on failure
bool StringToInt_Paranoid(const char *pString, int *pInt)
{
	char *pTemp;
	errno = 0;
	*pInt = (int)strtol(pString, &pTemp, 10);

	if (errno || pTemp == pString || *pTemp)
	{
		return false;
	}

	return true;
}

//returns true on success, false on failure
bool StringToUint_Paranoid(const char *pString, U32 *pInt)
{
	char *pTemp;
	errno = 0;
	*pInt = (int)strtoul(pString, &pTemp, 10);

	if (errno || pTemp == pString || *pTemp)
	{
		return false;
	}

	return true;
}



void StringToIntMultiSize(const char *pString, void *pInt, int iIntBytesSize)
{
	if (iIntBytesSize < 8)
	{
		S32 iTemp;
		if (!StringToInt(pString, &iTemp))
		{
			return;
		}

		switch (iIntBytesSize)
		{
		case 1:
			(*((S8*)pInt)) = iTemp;
			return;
		case 2:
			(*((S16*)pInt)) = iTemp;
			return;
		case 4:
			(*((S32*)pInt)) = iTemp;
			return;
		}

		return;
	}

	(*((S64*)pInt)) = _atoi64(pString);
}


void StringToUintMultiSize(const char *pString, void *pInt, int iIntBytesSize)
{
	if (iIntBytesSize < 8)
	{
		U32 iTemp;
		if (!StringToUint(pString, &iTemp))
		{
			return;
		}

		switch (iIntBytesSize)
		{
		case 1:
			(*((U8*)pInt)) = iTemp;
			return;
		case 2:
			(*((U16*)pInt)) = iTemp;
			return;
		case 4:
			(*((U32*)pInt)) = iTemp;
			return;
		}

		return;
	}

	(*((U64*)pInt)) = _atoi64(pString);
}
bool StringToFloat(const char *pString, float *pFloat)
{
	char* end;

	errno = 0;
	*pFloat = (float)strtod(pString, &end);
	if (errno || *end)
	{
		return false;
	}

	if (StringIsAllWhiteSpace(pString))
	{
		return false;
	}

	return true;

}

bool StringToVec3(const char *pString, Vec3 *pVec3)
{
	Vec3 result;
	char buf[256];
	char *r = buf;
	char *tok = NULL;
	char *tokContext = NULL;
	int i = 0;

	if (!pVec3 || !pString || !pString[0])
		return false;

	sprintf(buf, "%s", pString);

	while ((tok = strtok_s(r, ", ", &tokContext)) && i < 3)
	{
		float v;
		if (r) r = NULL;
		if (StringToFloat(tok, &v))
			result[i++] = v;
	}
	if (i < 3)
		return false;

	pVec3[0][0] = result[0];
	pVec3[0][1] = result[1];
	pVec3[0][2] = result[2];

	return true;
}
	

void *strdup_ifdiff_dbg(const char *pNewString, char *pOldString MEM_DBG_PARMS)
{
	if (!pNewString)
	{
		if (pOldString) free(pOldString);
		return NULL;
	}
	if (!pOldString)
	{
		return strdup_dbg(pNewString, caller_fname, line);
	}

	if (strcmp(pNewString, pOldString) == 0)
	{
		return pOldString;
	}
	free(pOldString);
	return strdup_dbg(pNewString, caller_fname, line);
}


void MakeCommaSeparatedString_dbg(char ***pppInList, char **ppOutEString MEM_DBG_PARMS)
{
	int i;

	estrClear(ppOutEString);

	for (i=0; i < eaSize(pppInList); i++)
	{
		estrConcatf_dbg(ppOutEString, caller_fname, line, "%s%s", i == 0 ? "" : ", ", (*pppInList)[i]);
	}
}

bool AddAndRemoveStrings_dbg(char ***pppMasterList, char ***pppAddList, char ***pppRemoveList, bool bCheckValidity, char **ppErrorString MEM_DBG_PARMS)
{
	int i;
	int iIndex;

	for (i=0 ; i < eaSize(pppRemoveList); i++)
	{
		iIndex = StringArrayFind(*pppMasterList, (*pppRemoveList)[i]);
		if (iIndex != -1)
		{
			free((*pppMasterList)[iIndex]);
			eaRemove(pppMasterList, iIndex);
		}
		else
		{
			if (bCheckValidity)
			{
				if (ppErrorString)
				{
					estrPrintf_dbg(ppErrorString, caller_fname, line, "ERROR: Couldn't find %s to remove", (*pppRemoveList)[i]);
				}
				return false;
			}
		}
	}

	for (i=0; i < eaSize(pppAddList); i++)
	{

		iIndex = StringArrayFind(*pppMasterList, (*pppAddList)[i]);
	
		if (iIndex != -1)
		{
			if (ppErrorString)
			{
				estrPrintf_dbg(ppErrorString MEM_DBG_PARMS_CALL, "ERROR: %s already present", (*pppAddList)[i]);
			}
			return false;
		}
		else
		{
			seaPush(pppMasterList, strdup_dbg((*pppAddList)[i] MEM_DBG_PARMS_CALL));
		}
	}

	return true;
}
	

bool StringIsAllWhiteSpace(const char *pString)
{
	while (*pString)
	{
		if (!IS_WHITESPACE(*pString))
		{
			return false;
		}
		pString++;
	}

	return true;
}

bool StringIsValidEmail(const char *pString)
{
	int localLen = 0;
	if (!pString || !pString[0])
		return false;

	while (*pString)
	{
		if (*pString == '@')
			break;
		pString++;
	}
	if (*pString == 0)
		return false; // failed to find '@'
	if (!localLen)
		return false; // no local-path string
	if (*(pString+1) == 0)
		return false; // no domain name
	return true;
}

static const char *s_apchReservedPrefixes[] = {
	"Admin",
	"Atari",
	"Cryptic",
	"CS-",
	"CS_",
	"CS ",
	"CSR",
	"Customer-Service",
	"Customer_Service",
	"Customer Service",
	"CustomerService",
	"Customer-Support",
	"Customer_Support",
	"Customer Support",
	"CustomerSupport",
	"Game-Master",
	"Game_Master",
	"Game Master",
	"GameMaster",
	"GM-",
	"GM_",
	"GM ",
	"Moderator",
	"Monitor",
	"OCR",
	"QA-",
	"QA_",
	"QA ",
	"Test-Master",
	"Test_Master",
	"Test Master",
	"TestMaster",
	"Tester",
	NULL
};

__forceinline static bool HasReservedPrefix(const char *pString)
{
	const char **ppPrefix = s_apchReservedPrefixes;
	while (*ppPrefix) {
		if (strStartsWith( pString, *ppPrefix )) {
			return true;
		}
		ppPrefix++;
	}
	return false;
}

// Returns true if the codePt is a letter or number glyph, for a very wide definition of "letter".
// If bSimpleAsciiOnly is true, anything outside 0-127 will return false.
bool UTF8isalnum(U32 codePt, bool bSimpleAsciiOnly)
{
	// The Simple ASCII stuff
	if( codePt <= 0x7F )
		return isalnum((char)(codePt & 0x7F));

	if( bSimpleAsciiOnly )
		return false;

	// (Latin-1 Letters) 0x00C0 ‘À’ - 0x00FF ‘ÿ’ (except 0x00D7 ‘×’ Multiplication sign, 0x00F7 ‘÷’ Division sign)
	if( codePt >= 0x00C0 && codePt <= 0x00FF )
		return (codePt != 0x00D7 && codePt != 0x00F7);

	// (Latin-1 Extended Letters) 0x0100 ‘Ā’ - 0x017F ‘ſ’
	if( codePt >= 0x0100 && codePt <= 0x017F )
		return true;

	// (CJK Ideographs) 0x4E00 ‘一’ - 0x9fff ‘鿿’
	if( codePt >= 0x4E00 && codePt <= 0x9FFF )
		return true;

	// (Fullwidth Specials) 0x3000 ‘　’, 0xFF07 ‘＇’,  0xFF0D ‘－’, 0xFF0E ‘．‘, 0xFF3F ‘＿’, 
	//if( codePt == 0x3000 || codePt == 0xFF07 || codePt == 0xFF0D || codePt == 0xFF0E || codePt == 0xFF3F )
	//	return true;

	// (Fullwidth Digits) 0xFF10 ‘０’ - 0xFF19 ‘９’
	if( codePt >= 0xFF10 && codePt <= 0xFF19 )
		return true;

	// (Fullwidth Upper Case) 0xFF21 ‘Ａ’ - 0xff3A ‘Ｚ’
	if( codePt >= 0xFF21 && codePt <= 0xFF3A )
		return true;

	// (Fullwidth Lower Case) 0xFF41 ‘ａ’ - 0xFF5A ‘ｚ’
	if( codePt >= 0xFF41 && codePt <= 0xFF5A )
		return true;

	return false;
}

// If bSimpleAsciiOnly is true, anything outside 0-127 will return false.
bool UTF8isspace(U32 codePt, bool bSimpleAsciiOnly)
{
	// The Simple ASCII stuff
	if( codePt <= 0x7F )
		return isspace((char)(codePt & 0x7F));

	if( bSimpleAsciiOnly )
		return false;

	// (Fullwidth Specials) 0x3000 ‘　’
	if( codePt == 0x3000 )
		return true;

	return false;
}

// Takes a UTF8 string pcString, checks if all the characters are "letters", or if there are special characters that there aren't 2 in a row.
// Note: pcSpecialChars must be basic ASCII characters, <= 0x7F
int StringIsInvalidNameGeneric(const char *pcString, const char *pcSpecialChars, U32 iMinLength, U32 iMaxLength, bool bCheckWhitespace, bool bRestrictSpecialSequences, bool bCheckProfanity, bool bCheckPrefixes, bool bCheckRestricted, bool bSimpleAsciiOnly, U32 iAccessLevel)
{
	size_t iUTF8Length = 0;
	U32 i, iNumSpecials = 0;
	const char *pC = pcString;
	U32 prevCodePt = '\0';
	U32 codePt = '\0';
	
	if(!pcString) {
		if (0 < iMinLength) {
			return STRINGERR_MIN_LENGTH;
		} else {
			return STRINGERR_NONE;
		}
	}
	
	iUTF8Length = UTF8GetLength(pcString);

	// Check length
	if (iUTF8Length < iMinLength) {
		return STRINGERR_MIN_LENGTH;
	}
	if (iUTF8Length > iMaxLength) {
		return STRINGERR_MAX_LENGTH;
	}
	
	// Check for profanity
	if (bCheckProfanity && IsAnyProfane(pcString)) {
		return STRINGERR_PROFANITY;
	}
	
	// Check for a reserved name
	if (bCheckRestricted && ((bCheckPrefixes && iAccessLevel < ACCESS_GM && HasReservedPrefix(pcString)) || IsAnyRestricted(pcString) || IsDisallowed(pcString))) {
		return STRINGERR_RESTRICTED;
	}
	
	// Check for leading whitespace
	if (bCheckWhitespace && iUTF8Length > 0 && UTF8isspace(UTF8ToCodepoint(pC), bSimpleAsciiOnly) ) {
		return STRINGERR_WHITESPACE;
	}
	
	// Check for invalid characters
	// All characters must be either alphanumeric, or in the pcSpecialChars string
	// Also, no more than 1 special character is allowed in sequence, unless one and only one of them is a space
	for (i = 0; i < iUTF8Length; i++, pC += UTF8GetCodepointLength(pC) )
	{
		prevCodePt = codePt;
		codePt = UTF8ToCodepoint(pC);
		if (UTF8isalnum(codePt, bSimpleAsciiOnly))
		{
			iNumSpecials = 0;
			continue;
		}

		// TODO: Handle full-width specials too - 0x3000 ‘　’, 0xFF07 ‘＇’,  0xFF0D ‘－’, 0xFF0E ‘．‘, 0xFF3F ‘＿’

		if (pcSpecialChars && codePt <= 0x7F)
		{
			const char c = (char)(codePt & 0x7F);
			if (!strchr_fast(pcSpecialChars, c))
			{
				return STRINGERR_CHARACTERS;
			}
			else if (bRestrictSpecialSequences)
			{
				if (iNumSpecials >= 2)
				{
					return STRINGERR_CHARACTERS;
				}
				else if (iNumSpecials && ((c == ' ' && prevCodePt == ' ') || (c != ' ' && prevCodePt != ' ')))
				{
					return STRINGERR_CHARACTERS;
				}
			}
		}
		iNumSpecials++;
	}
	
	// Check for trailing whitespace
	if (bCheckWhitespace && iUTF8Length > 0 && UTF8isspace(codePt, bSimpleAsciiOnly) ) {
		return STRINGERR_WHITESPACE;
	}

	// There must have been at least one alphanumeric character in the string
	if (iUTF8Length > 0 && iNumSpecials == iUTF8Length) {
		return STRINGERR_CHARACTERS;
	}
	return STRINGERR_NONE;
}

// Assumes string is leading and trailing white-space trimmed
bool StringIsValidPassword(const char *pString)
{
	//int len;

	if (!pString)
		return false;
	//len = (int) strlen(pString);

	//if (len < PASSWORD_MIN_LENGTH || len > PASSWORD_MAX_LENGTH)
		//return false; // too short or too long

	while (*pString)
	{
		// allow all regular punctuation and letters and numbers and symbols
		if (*pString < '!' || *pString > '~') // lower and upper limits of punctuation
			return false;
		pString++;
	}
	return true;
}

bool StringKeywordSearchMatches (const char *text, const char *keywordString)
{
	char **ppSearchStrings = NULL;
	char *keywordCopy = strdup(keywordString);
	char *curToken, *nextSpace, *nextQuote;
	int i;

	curToken = keywordCopy;
	nextSpace = strchr(curToken, ' ');
	nextQuote = strchr(curToken, '\"');

	while (curToken && *curToken)
	{
		if (nextQuote && (!nextSpace || nextQuote < nextSpace))
		{
			*nextQuote = 0;
			if (*curToken)
				eaPush(&ppSearchStrings, curToken);
			curToken = nextQuote+1;
			nextQuote = strchr(curToken, '\"');
			if (nextQuote)
				*nextQuote = 0;
			if (*curToken)
				eaPush(&ppSearchStrings, curToken);
			if (!nextQuote)
				break;
			curToken = nextQuote+1;
		}
		else if (nextSpace)
		{
			*nextSpace = 0;
			if (*curToken)
				eaPush(&ppSearchStrings, curToken);
			curToken = nextSpace+1;
		}
		else
		{
			if (*curToken)
				eaPush(&ppSearchStrings, curToken);
			break;
		}
		nextSpace = strchr(curToken, ' ');
		nextQuote = strchr(curToken, '\"');
	}
	for (i=eaSize(&ppSearchStrings)-1; i>=0; i--)
	{
		if (strstri(text, ppSearchStrings[i]) == NULL)
		{
			eaDestroy(&ppSearchStrings);
			free(keywordCopy);
			return false;
		}
	}

	eaDestroy(&ppSearchStrings);
	free(keywordCopy);
	return true;
}

//returns a message which indicates the description formatting error (written for client)
//the error is currently returned from StringIsInvalidDescription
void StringCreateDescriptionErrorEx(char **msg, int strerr, Language eLang)
{
	estrClear(msg);

	switch ( strerr )
	{
	case STRINGERR_MAX_LENGTH:
		langFormatMessageKey(eLang, msg, "DescriptionFormat_MaxLengthError", STRFMT_INT("value", ASCII_DESCRIPTION_MAX_LENGTH),STRFMT_END);
		break;
	case STRINGERR_PROFANITY:
		langFormatMessageKey(eLang, msg, "DescriptionFormat_Profanity", STRFMT_END);
		break;
	case STRINGERR_RESERVED:
		langFormatMessageKey(eLang, msg, "DescriptionFormat_Reserved", STRFMT_END);
		break;
	case STRINGERR_RESTRICTED:
		langFormatMessageKey(eLang, msg, "DescriptionFormat_Restricted", STRFMT_END);
		break;
	case STRINGERR_CHARACTERS:
		langFormatMessageKey(eLang, msg, "DescriptionFormat_NoValidCharacters", STRFMT_END);
		break;
	case STRINGERR_NONE:
		langFormatMessageKey(eLang, msg, "DescriptionFormat_Valid", STRFMT_END);
		break;
	default:
		Errorf( "Invalid description formatting rule: %i", strerr );
	}
}

//returns a message which indicates the name formatting error (written for client)
//the error is currently returned from StringIsInvalidName or StringIsInvalidDisplayName
void StringCreateNameErrorEx(char **msg, int strerr, Language eLang, int iMinNameLength, int iMaxNameLength )
{
	estrClear(msg);

	switch ( strerr )
	{
	case STRINGERR_CHARACTERS:
		langFormatMessageKey(eLang, msg, "NameFormat_InvalidCharacters", STRFMT_END);
		break;
	case STRINGERR_MIN_LENGTH:
		langFormatMessageKey(eLang, msg, "NameFormat_MinLengthError", STRFMT_INT("value", (iMinNameLength < 0 ? ASCII_NAME_MIN_LENGTH : iMinNameLength)),STRFMT_END);
		break;
	case STRINGERR_MAX_LENGTH:
		langFormatMessageKey(eLang, msg, "NameFormat_MaxLengthError", STRFMT_INT("value", (iMaxNameLength < 0 ? ASCII_NAME_MAX_LENGTH : iMaxNameLength)),STRFMT_END);
		break;
	case STRINGERR_WHITESPACE:
		langFormatMessageKey(eLang, msg, "NameFormat_InvalidWhitespace", STRFMT_END);
		break;
	case STRINGERR_PROFANITY:
		langFormatMessageKey(eLang, msg, "NameFormat_Profanity", STRFMT_END);
		break;
	case STRINGERR_RESERVED:
		langFormatMessageKey(eLang, msg, "NameFormat_Reserved", STRFMT_END);
		break;
	case STRINGERR_RESTRICTED:
		langFormatMessageKey(eLang, msg, "NameFormat_Restricted", STRFMT_END);
		break;
	case STRINGERR_NONE:
		langFormatMessageKey(eLang, msg, "NameFormat_Valid", STRFMT_END);
		break;
	default:
		Errorf( "Invalid name formatting rule: %i", strerr );
	}
}
	

int StringCountLines(const char *str)
{
	char *buf;
	char *tok;
	char *context;
	size_t len;
	int count = 0;

	if (!str || !str[0]) return 0;

	//build the tokenizer buffer
	len = strlen(str) + 1;
	buf = (char*)calloc(len, sizeof(char));
	memcpy(buf, str, len);

	//tokenize on newlines
	tok = strtok_r(buf, "\n\r", &context);
	while (tok != NULL)
	{
		count++;
		tok = strtok_r(NULL, "\n\r", &context);
	}
	free(buf);
	return count;
}



void StringFormatNumberSignificantDigits(char* pchString, F32 fNumber, int iDigits, bool bInsertCommas, bool bInsertK)
{
	char pchBuffer[260];
	bool bNeedsToInsertK = false;
	bool bDecimal = false;
	bool bK = false;
	int iPosition = 0;
	int iSigCount = 0;
	int iWholeDigits = 0;
	const char* pchCurr = pchString;
	const char* pchOneThousand = NULL;

	if ( iDigits <= 0 )
		return;

	if ( bInsertK )
	{
		pchOneThousand = TranslateMessageKey("StringUtil_OneThousand_OneLetter");
		
		devassert( pchOneThousand && pchOneThousand[0] );
	}

	fNumber = roundFloatSignificantDigits(fNumber,iDigits);

	sprintf(pchBuffer, "%f", fNumber);

	pchCurr = pchBuffer;

	while (*pchCurr)
	{
		if ( *pchCurr == '.' )
		{
			bDecimal = true;
		}
		if ( bInsertK && *pchCurr == pchOneThousand[0] )
		{
			bK = true;
		}
		if ( !bDecimal )
		{
			iWholeDigits++;
		}
		pchCurr++;
	}

	bDecimal = false;
	pchCurr = pchBuffer;

	while (*pchCurr)
	{
		if ( *pchCurr == '.' )
		{
			if ( !bDecimal )
			{
				pchString[iPosition++] = *pchCurr;
			}
		}
		else if ( *pchCurr >= '0' && *pchCurr <= '9' )
		{
			if ( iSigCount > 0 && iWholeDigits > iSigCount )
			{
				if ( bInsertK && !bK && iWholeDigits - iSigCount == 3 )
				{
					if ( iWholeDigits - iDigits < 3 )
					{
						bDecimal = true;
						pchString[iPosition++] = '.';
					}
					else
					{
						break;
					}
				}
				else if ( bInsertCommas && (iWholeDigits - iSigCount) % 3 == 0 )
				{
					pchString[iPosition++] = ',';
				}
			}
			pchString[iPosition++] = *pchCurr;
			if ( ++iSigCount >= iDigits )
			{
				break;
			}
		}
		pchCurr++;
	}
	if ( bInsertK && !bK && iWholeDigits > 3 )
	{
		if ( iWholeDigits - iDigits > 3 )
		{
			while ( iSigCount++ < iWholeDigits-3 )
			{
				pchString[iPosition++] = '0';
			}
		}

		pchString[iPosition++] = pchOneThousand[0];
	}
	else if ( iWholeDigits - iDigits > 0 )
	{
		while ( iSigCount++ < iWholeDigits )
		{
			pchString[iPosition++] = '0';
		}
	}

	pchString[iPosition] = '\0';
}

char GetFirstNonWhitespaceChar(const char *pSrc, int *pOutIndex)
{
	const char *pPtr = pSrc;
	while (IS_WHITESPACE(*pPtr))
	{
		pPtr++;
	}

	if (pOutIndex)
	{
		*pOutIndex = pPtr - pSrc;
	}

	return *pPtr;
}

void StringStripTagsSafe_dbg(const char *src, char **pestrDest MEM_DBG_PARMS)
{
	const char *start;
	const char *last = src;

	while (last && *last && (start = strchr(last, '<')))
	{
		estrConcat_dbg(pestrDest, last, start-last MEM_DBG_PARMS_CALL);
		last = start+1;
		if(*last)
		{
			last = strchr(last,'>');
			if(last && *last)
			{
				last++;
			}
		}
	}
	if (last && *last)
	{
		estrAppend2_dbg(pestrDest, last MEM_DBG_PARMS_CALL);
	}
}

// Strips away html-style <tags>. Inserts spaces in place of <br> tags. Doesn't copy into pestrOutput if no changes are made, unless bCopyIfUnchanged is true.
bool StringStripTagsPrettyPrintEx(SA_PARAM_OP_STR const char *pchInput, SA_PARAM_NN_VALID char **pestrOutput, bool bCopyIfUnchanged)
{
	const char *pBegin;
	const char *pchLessThanCharPos = NULL;
	bool bDrop = false;
	bool bLastSpace = false;

	if (pchInput == NULL)
	{
		return false;
	}

	// Replaces <br> tags with spaces.
	for (pBegin = pchInput; *pBegin; pBegin++) 
	{
		if (*pBegin == '<') 
		{
			bDrop = true;

			if (pchLessThanCharPos == NULL)
			{
				// First < char is encountered
				U32 iNumBytesToCopy = (U32)((uintptr_t) pBegin - (uintptr_t) pchInput);

				pchLessThanCharPos = pBegin;

				// Copy everything until the first < char
				if (iNumBytesToCopy)
				{
					estrConcat(pestrOutput, pchInput, iNumBytesToCopy);
				}	
			}			

			if (!strnicmp(pBegin, "<br>", 4) && pBegin != pchInput && !bLastSpace) 
			{
				estrConcatChar(pestrOutput, ' ');
				bLastSpace = true;
			}
		} 
		else if (!bDrop && pchLessThanCharPos) 
		{
			estrConcatChar(pestrOutput, *pBegin);			
			bLastSpace = isspace((unsigned char)(*pBegin));
		} 
		else if (*pBegin == '>') 
		{
			bDrop = false;
		}
	}

	if (pchLessThanCharPos && *pestrOutput == NULL)
	{
		estrCreate(pestrOutput);
	}
	else if (!pchLessThanCharPos && bCopyIfUnchanged)
	{
		estrAppend2(pestrOutput, pchInput);
	}

	return pchLessThanCharPos != NULL;
}

bool StringStripFontTagsEx(SA_PARAM_OP_STR const char *pchInput, SA_PARAM_NN_VALID char **pestrOutput, bool bCopyIfUnchanged)
{
	const char *pBegin;
	const char *pchLessThanCharPos = NULL;
	bool bDrop = false;
	bool bLastSpace = false;

	if (pchInput == NULL)
	{
		return false;
	}

	for (pBegin = pchInput; *pBegin; pBegin++)
	{
		if (*pBegin == '<' && !strnicmp(pBegin, "<font ", 6) )
		{
			bDrop = true;

			if (pchLessThanCharPos == NULL)
			{
				// First < char is encountered
				U32 iNumBytesToCopy = (U32)((uintptr_t) pBegin - (uintptr_t) pchInput);

				pchLessThanCharPos = pBegin;

				// Copy everything until the first < char
				if (iNumBytesToCopy)
				{
					estrConcat(pestrOutput, pchInput, iNumBytesToCopy);
				}
			}
		}
		else if (!bDrop && pchLessThanCharPos)
		{
			estrConcatChar(pestrOutput, *pBegin);
			bLastSpace = isspace((unsigned char)(*pBegin));
		}
		else if (*pBegin == '>')
		{
			bDrop = false;
		}
	}

	if (pchLessThanCharPos && *pestrOutput == NULL)
	{
		estrCreate(pestrOutput);
	}
	else if (!pchLessThanCharPos && bCopyIfUnchanged)
	{
		estrAppend2(pestrOutput, pchInput);
	}

	return pchLessThanCharPos != NULL;
}

void TestStringUtil(void) {
    wchar_t ws_src[] = {0xf123, 0xf1, 0xf, 0x8000, 0x80, 0}, ws_test[64];
    char utf8_test[64];
    int i, j, k, l, m, n;

    m = WideToEncodingConvert(ws_src, 0, 0, CP_UTF8);
    n = WideToEncodingConvert(ws_src, utf8_test, sizeof(utf8_test), CP_UTF8);
    assert(m==n);

    l = UTF8ToWideStrConvert(utf8_test, 0, 0);
    k = UTF8ToWideStrConvert(utf8_test, ws_test, sizeof(ws_test)/sizeof(ws_test[0]));
    assert(k==l);

    assert( !memcmp(ws_src, ws_test, sizeof(ws_src)) );

    i = UTF8ToWideCharConvert(utf8_test);
    assert(i == ws_src[0]);
    j = i;
}


void FindDifferencesBetweenEarraysOfStrings(char ***pppOutList1, char ***pppOutList2, char ***pppInList1, char ***pppInList2, StringComparer pComparer)
{
	int i, j;
	for (i=0; i < eaSize(pppInList1); i++)
	{
		bool bFound = false;
		for (j=0; j < eaSize(pppInList2); j++)
		{
			if (pComparer((*pppInList1)[i], (*pppInList2)[j]))
			{
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			eaPush(pppOutList1, (*pppInList1)[i]);
		}
	}

	for (i=0; i < eaSize(pppInList2); i++)
	{
		bool bFound = false;
		for (j=0; j < eaSize(pppInList1); j++)
		{
			if (pComparer((*pppInList2)[i], (*pppInList1)[j]))
			{
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			eaPush(pppOutList2, (*pppInList2)[i]);
		}
	}
}

bool StringContainsWhitespace(const char *s)
{
	if (!s)
		return false;

	while (*s)
	{
		if (IS_WHITESPACE(*s))
		{
			return true;
		}

		s++;
	}

	return false;
}

// Generated from the table on http://publib.boulder.ibm.com/infocenter/iseries/v7r1m0/topic/nls/rbagsuppertolowermaptable.htm
// Trie to convert Uppercase to Lowercase
#define TRIE_MASK 0xF000
#define TRIE_SHIFT 4
#define TRIE_FIRST_SHIFT 12
static U8 s_UnicodeUc2LcTrie[] = {
	0x04, 0x00, 0xf1, 0x01, 0x80, 0x28, 0x03, 0x02, 0x80, 0xc9, 0xfa, 0x0f, 0x80, 0xa0, 0xfa, 0x51,
	0xe3, 0x80, 0x85, 0xfe, 0x80, 0x9c, 0x01, 0x80, 0xf3, 0x01, 0x80, 0x0e, 0xfd, 0x70, 0x10, 0x80,
	0x5f, 0x01, 0x80, 0x5f, 0x01, 0x6d, 0x7e, 0x80, 0x91, 0x00, 0x80, 0x5f, 0x01, 0x80, 0x5f, 0x01,
	0x80, 0xc2, 0x00, 0x80, 0xd3, 0x00, 0x80, 0xf0, 0x00, 0x80, 0x15, 0x01, 0x80, 0x2a, 0x01, 0x80,
	0x3f, 0x01, 0x80, 0x50, 0x01, 0x80, 0x5f, 0x01, 0x80, 0x70, 0x01, 0x08, 0x00, 0x01, 0x02, 0x01,
	0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e, 0x01, 0x08, 0x00, 0x01, 0x02,
	0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e, 0x01, 0x08, 0x00, 0x01,
	0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e, 0x01, 0x08, 0x00,
	0x80, 0x39, 0xff, 0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x09, 0x01, 0x0b, 0x01, 0x0d, 0x01, 0x0f,
	0x01, 0x07, 0x01, 0x01, 0x03, 0x01, 0x05, 0x01, 0x07, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e, 0x01,
	0x08, 0x00, 0x01, 0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e,
	0x01, 0x08, 0x00, 0x01, 0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01,
	0x0e, 0x01, 0x08, 0x00, 0x01, 0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x87, 0x09, 0x01, 0x0b,
	0x01, 0x0d, 0x01, 0x09, 0x01, 0x80, 0xd2, 0x00, 0x02, 0x01, 0x04, 0x01, 0x06, 0x80, 0xce, 0x00,
	0x07, 0x01, 0x0a, 0x80, 0xcd, 0x00, 0x0b, 0x01, 0x0e, 0x80, 0xca, 0x00, 0x0f, 0x80, 0xca, 0x00,
	0x0a, 0x00, 0x80, 0xcb, 0x00, 0x01, 0x01, 0x03, 0x80, 0xcd, 0x00, 0x04, 0x80, 0xcf, 0x00, 0x06,
	0x80, 0xd3, 0x00, 0x07, 0x80, 0xd1, 0x00, 0x08, 0x01, 0x0c, 0x80, 0xd3, 0x00, 0x0d, 0x80, 0xd5,
	0x00, 0x0f, 0x80, 0xd6, 0x00, 0x08, 0x00, 0x01, 0x02, 0x01, 0x04, 0x01, 0x07, 0x01, 0x09, 0x80,
	0xda, 0x00, 0x0c, 0x01, 0x0e, 0x80, 0xda, 0x00, 0x0f, 0x01, 0x07, 0x01, 0x80, 0xd9, 0x00, 0x02,
	0x80, 0xd9, 0x00, 0x03, 0x01, 0x05, 0x01, 0x07, 0x80, 0xdb, 0x00, 0x08, 0x01, 0x0c, 0x01, 0x08,
	0x04, 0x02, 0x05, 0x01, 0x07, 0x02, 0x08, 0x01, 0x0a, 0x02, 0x0b, 0x01, 0x0d, 0x01, 0x0f, 0x01,
	0x07, 0x01, 0x01, 0x03, 0x01, 0x05, 0x01, 0x07, 0x01, 0x09, 0x01, 0x0b, 0x01, 0x0e, 0x01, 0x08,
	0x00, 0x01, 0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e, 0x01,
	0x05, 0x01, 0x02, 0x04, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e, 0x01, 0x20, 0x80, 0x82, 0x01, 0x80,
	0x93, 0x01, 0x08, 0x00, 0x01, 0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c,
	0x01, 0x0e, 0x01, 0x04, 0x00, 0x01, 0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x04, 0x08, 0x80, 0xad,
	0x01, 0x09, 0x80, 0x44, 0xfe, 0x0a, 0x80, 0xcd, 0x01, 0x0e, 0x80, 0xe4, 0x01, 0x07, 0x06, 0x26,
	0x08, 0x25, 0x09, 0x25, 0x0a, 0x25, 0x0c, 0x40, 0x0e, 0x3f, 0x0f, 0x3f, 0x71, 0x0f, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x0b, 0x00, 0x20,
	0x01, 0x20, 0x03, 0x20, 0x04, 0x20, 0x05, 0x20, 0x06, 0x20, 0x07, 0x20, 0x08, 0x20, 0x09, 0x20,
	0x0a, 0x20, 0x0b, 0x20, 0x07, 0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c,
	0x01, 0x0e, 0x01, 0x0d, 0x00, 0x80, 0x28, 0x02, 0x01, 0x80, 0xa9, 0xfd, 0x02, 0x80, 0xa9, 0xfd,
	0x06, 0x80, 0xc9, 0x02, 0x07, 0x80, 0x7a, 0x02, 0x08, 0x80, 0x75, 0xfd, 0x09, 0x80, 0xc9, 0x02,
	0x0a, 0x80, 0xc9, 0x02, 0x0b, 0x80, 0xc9, 0x02, 0x0c, 0x80, 0xc0, 0x02, 0x0d, 0x80, 0xc9, 0x02,
	0x0e, 0x80, 0xda, 0x02, 0x0f, 0x80, 0xe9, 0x02, 0x0e, 0x01, 0x50, 0x02, 0x50, 0x03, 0x50, 0x04,
	0x50, 0x05, 0x50, 0x06, 0x50, 0x07, 0x50, 0x08, 0x50, 0x09, 0x50, 0x0a, 0x50, 0x0b, 0x50, 0x0c,
	0x50, 0x0e, 0x50, 0x0f, 0x50, 0x70, 0x10, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x70, 0x10, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x08, 0x00, 0x01, 0x02, 0x01, 0x04, 0x01,
	0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e, 0x01, 0x08, 0x00, 0x01, 0x02, 0x01, 0x04,
	0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e, 0x01, 0x10, 0x01, 0x08, 0x00, 0x01,
	0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e, 0x01, 0x08, 0x00,
	0x01, 0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e, 0x01, 0x08,
	0x00, 0x01, 0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e, 0x01,
	0x04, 0x01, 0x01, 0x03, 0x01, 0x07, 0x01, 0x0b, 0x01, 0x08, 0x00, 0x01, 0x02, 0x01, 0x04, 0x01,
	0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e, 0x01, 0x07, 0x00, 0x01, 0x02, 0x01, 0x04,
	0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0e, 0x01, 0x04, 0x00, 0x01, 0x02, 0x01, 0x04, 0x01,
	0x08, 0x01, 0x33, 0x80, 0x04, 0xfd, 0x80, 0xf3, 0xfc, 0x80, 0xe1, 0xfc, 0x71, 0x0f, 0x30, 0x30,
	0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x70, 0x10, 0x30,
	0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x70,
	0x07, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x03, 0x00, 0x80, 0xcb, 0xfc, 0x0e, 0x80, 0x96,
	0xfc, 0x0f, 0x80, 0x9c, 0x04, 0x3a, 0x80, 0xaf, 0xfc, 0x80, 0xaf, 0xfc, 0x80, 0x9d, 0xfc, 0x70,
	0x10, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
	0x30, 0x70, 0x10, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
	0x30, 0x30, 0x30, 0x60, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x70, 0x10, 0x80, 0x80, 0x04, 0x80,
	0x80, 0x04, 0x80, 0xbe, 0x03, 0x80, 0x80, 0x04, 0x80, 0x80, 0x04, 0x80, 0x80, 0x04, 0x80, 0x80,
	0x04, 0x80, 0x80, 0x04, 0x80, 0x80, 0x04, 0x80, 0x35, 0x04, 0x80, 0x80, 0x04, 0x80, 0x80, 0x04,
	0x80, 0x80, 0x04, 0x80, 0x80, 0x04, 0x80, 0x80, 0x04, 0x80, 0x91, 0x04, 0x08, 0x00, 0x01, 0x02,
	0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e, 0x01, 0x08, 0x00, 0x01,
	0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e, 0x01, 0x08, 0x00,
	0x01, 0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e, 0x01, 0x08,
	0x00, 0x01, 0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e, 0x01,
	0x08, 0x00, 0x01, 0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e,
	0x01, 0x08, 0x00, 0x01, 0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01,
	0x0e, 0x01, 0x08, 0x00, 0x01, 0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c,
	0x01, 0x0e, 0x01, 0x08, 0x00, 0x01, 0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01,
	0x0c, 0x01, 0x0e, 0x01, 0x08, 0x00, 0x01, 0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a,
	0x01, 0x0c, 0x01, 0x0e, 0x01, 0x03, 0x00, 0x01, 0x02, 0x01, 0x04, 0x01, 0x08, 0x00, 0x01, 0x02,
	0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e, 0x01, 0x08, 0x00, 0x01,
	0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e, 0x01, 0x08, 0x00,
	0x01, 0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e, 0x01, 0x08,
	0x00, 0x01, 0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e, 0x01,
	0x08, 0x00, 0x01, 0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0a, 0x01, 0x0c, 0x01, 0x0e,
	0x01, 0x05, 0x00, 0x01, 0x02, 0x01, 0x04, 0x01, 0x06, 0x01, 0x08, 0x01, 0x0d, 0x00, 0x80, 0xdc,
	0xfa, 0x01, 0x80, 0x0a, 0xfb, 0x02, 0x80, 0xdc, 0xfa, 0x03, 0x80, 0x14, 0xfb, 0x04, 0x80, 0x0a,
	0xfb, 0x05, 0x80, 0xfd, 0x04, 0x06, 0x80, 0xdc, 0xfa, 0x08, 0x80, 0xdc, 0xfa, 0x09, 0x80, 0xdc,
	0xfa, 0x0a, 0x80, 0xdc, 0xfa, 0x0b, 0x80, 0xcc, 0xfa, 0x0d, 0x80, 0xcf, 0xfa, 0x0e, 0x80, 0xcc,
	0xfa, 0x78, 0x08, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0x68, 0xf8, 0xf8, 0xf8, 0xf8,
	0xf8, 0xf8, 0x78, 0x08, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0x78, 0x08, 0xf8, 0xf8,
	0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0x68, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0x04, 0x09, 0xf8,
	0x0b, 0xf8, 0x0d, 0xf8, 0x0f, 0xf8, 0x78, 0x08, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8,
	0x78, 0x08, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0x78, 0x08, 0xf8, 0xf8, 0xf8, 0xf8,
	0xf8, 0xf8, 0xf8, 0xf8, 0x78, 0x08, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0x28, 0xf8,
	0xf8, 0x28, 0xf8, 0xf8, 0x28, 0xf8, 0xf8, 0x14, 0x80, 0xc5, 0xfa, 0x2b, 0x80, 0xbe, 0xfa, 0x80,
	0xb2, 0xfa, 0x76, 0x0a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x70, 0x10,
	0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a,
	0x1f, 0x80, 0x9c, 0xfa, 0x22, 0x80, 0x95, 0xfa, 0x80, 0x84, 0xfa, 0x71, 0x0f, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x70, 0x0b, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
};
// Lookup table to convert Uppercase to Lowercase if character value < 256
static S8 s_UnicodeUc2LcLookup[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// Generated from the table on http://publib.boulder.ibm.com/infocenter/iseries/v7r1m0/topic/nls/rbagslowtoupmaptable.htm
// Trie to convert Lowercase to Uppercase
static U8 s_UnicodeLc2UcTrie[] = {
	0x04, 0x00, 0xf1, 0x01, 0x80, 0x2a, 0x03, 0x02, 0x80, 0xc7, 0xfa, 0x0f, 0x80, 0x9e, 0xfa, 0x51,
	0xe3, 0x80, 0x2b, 0x01, 0x80, 0xae, 0x01, 0x80, 0xff, 0xfd, 0x80, 0x0c, 0xfd, 0x70, 0x10, 0x80,
	0x0f, 0x01, 0x80, 0x0f, 0x01, 0x6d, 0x7e, 0x80, 0x8f, 0x00, 0x80, 0x0f, 0x01, 0x80, 0x0f, 0x01,
	0x80, 0xc2, 0x00, 0x80, 0xd1, 0x00, 0x80, 0xda, 0x00, 0x80, 0xdf, 0x00, 0x80, 0xea, 0x00, 0x80,
	0xf5, 0x00, 0x80, 0xfe, 0x00, 0x80, 0x0f, 0x01, 0x80, 0x20, 0x01, 0x08, 0x01, 0xff, 0x03, 0xff,
	0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d, 0xff, 0x0f, 0xff, 0x08, 0x01, 0xff, 0x03,
	0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d, 0xff, 0x0f, 0xff, 0x08, 0x01, 0xff,
	0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d, 0xff, 0x0f, 0xff, 0x07, 0x01,
	0x80, 0x18, 0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x0a, 0xff, 0x0c, 0xff, 0x0e, 0xff, 0x08,
	0x00, 0xff, 0x02, 0xff, 0x04, 0xff, 0x06, 0xff, 0x08, 0xff, 0x0b, 0xff, 0x0d, 0xff, 0x0f, 0xff,
	0x08, 0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d, 0xff, 0x0f,
	0xff, 0x08, 0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d, 0xff,
	0x0f, 0xff, 0x07, 0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x0a, 0xff, 0x0c, 0xff, 0x0e,
	0xff, 0x04, 0x03, 0xff, 0x05, 0xff, 0x08, 0xff, 0x0c, 0xff, 0x02, 0x02, 0xff, 0x09, 0xff, 0x05,
	0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x08, 0xff, 0x0d, 0xff, 0x05, 0x00, 0xff, 0x04, 0xff, 0x06,
	0xff, 0x09, 0xff, 0x0d, 0xff, 0x04, 0x06, 0xfe, 0x09, 0xfe, 0x0c, 0xfe, 0x0e, 0xff, 0x08, 0x00,
	0xff, 0x02, 0xff, 0x04, 0xff, 0x06, 0xff, 0x08, 0xff, 0x0a, 0xff, 0x0c, 0xff, 0x0f, 0xff, 0x08,
	0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d, 0xff, 0x0f, 0xff,
	0x05, 0x03, 0xfe, 0x05, 0xff, 0x0b, 0xff, 0x0d, 0xff, 0x0f, 0xff, 0x07, 0x00, 0x80, 0x48, 0x01,
	0x01, 0x80, 0x59, 0x01, 0x05, 0x80, 0x62, 0x01, 0x06, 0x80, 0x7b, 0x01, 0x07, 0x80, 0x90, 0x01,
	0x08, 0x80, 0x99, 0x01, 0x09, 0x80, 0x56, 0xfe, 0x08, 0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x07,
	0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d, 0xff, 0x0f, 0xff, 0x04, 0x01, 0xff, 0x03, 0xff, 0x05, 0xff,
	0x07, 0xff, 0x06, 0x03, 0x80, 0x2e, 0xff, 0x04, 0x80, 0x32, 0xff, 0x07, 0x80, 0x33, 0xff, 0x08,
	0x80, 0x36, 0xff, 0x09, 0x80, 0x36, 0xff, 0x0b, 0x80, 0x35, 0xff, 0x05, 0x00, 0x80, 0x33, 0xff,
	0x03, 0x80, 0x31, 0xff, 0x08, 0x80, 0x2f, 0xff, 0x09, 0x80, 0x2d, 0xff, 0x0f, 0x80, 0x2d, 0xff,
	0x02, 0x02, 0x80, 0x2b, 0xff, 0x05, 0x80, 0x2a, 0xff, 0x04, 0x03, 0x80, 0x26, 0xff, 0x08, 0x80,
	0x26, 0xff, 0x0a, 0x80, 0x27, 0xff, 0x0b, 0x80, 0x27, 0xff, 0x12, 0x80, 0x25, 0xff, 0x04, 0x0a,
	0x80, 0x41, 0xfe, 0x0b, 0x80, 0x3c, 0xfe, 0x0c, 0x80, 0xd5, 0x01, 0x0e, 0x80, 0xf2, 0x01, 0x4c,
	0xda, 0xdb, 0xdb, 0xdb, 0x71, 0x0f, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0,
	0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0x0e, 0x00, 0xe0, 0x01, 0xe0, 0x03, 0xe0, 0x04, 0xe0, 0x05, 0xe0,
	0x06, 0xe0, 0x07, 0xe0, 0x08, 0xe0, 0x09, 0xe0, 0x0a, 0xe0, 0x0b, 0xe0, 0x0c, 0xc0, 0x0d, 0xc1,
	0x0e, 0xc1, 0x07, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d, 0xff, 0x0f,
	0xff, 0x73, 0x0d, 0x80, 0xc4, 0xfd, 0x80, 0xc4, 0xfd, 0x80, 0x4e, 0x02, 0x80, 0xcb, 0x02, 0x80,
	0x7c, 0x02, 0x80, 0x73, 0xfd, 0x80, 0xcb, 0x02, 0x80, 0xcb, 0x02, 0x80, 0xcb, 0x02, 0x80, 0xc2,
	0x02, 0x80, 0xcb, 0x02, 0x80, 0xdc, 0x02, 0x80, 0xeb, 0x02, 0x70, 0x10, 0xe0, 0xe0, 0xe0, 0xe0,
	0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0x70, 0x10, 0xe0, 0xe0,
	0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0x0e, 0x01,
	0xb0, 0x02, 0xb0, 0x03, 0xb0, 0x04, 0xb0, 0x05, 0xb0, 0x06, 0xb0, 0x07, 0xb0, 0x08, 0xb0, 0x09,
	0xb0, 0x0a, 0xb0, 0x0b, 0xb0, 0x0c, 0xb0, 0x0e, 0xb0, 0x0f, 0xb0, 0x08, 0x01, 0xff, 0x03, 0xff,
	0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d, 0xff, 0x0f, 0xff, 0x08, 0x01, 0xff, 0x03,
	0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d, 0xff, 0x0f, 0xff, 0x11, 0xff, 0x08,
	0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d, 0xff, 0x0f, 0xff,
	0x08, 0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d, 0xff, 0x0f,
	0xff, 0x08, 0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d, 0xff,
	0x0f, 0xff, 0x04, 0x02, 0xff, 0x04, 0xff, 0x08, 0xff, 0x0c, 0xff, 0x08, 0x01, 0xff, 0x03, 0xff,
	0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d, 0xff, 0x0f, 0xff, 0x07, 0x01, 0xff, 0x03,
	0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0f, 0xff, 0x04, 0x01, 0xff, 0x03, 0xff,
	0x05, 0xff, 0x09, 0xff, 0x36, 0x80, 0x02, 0xfd, 0x80, 0xf1, 0xfc, 0x80, 0xdf, 0xfc, 0x71, 0x0f,
	0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0x70,
	0x10, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0,
	0xd0, 0x70, 0x07, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0x03, 0x00, 0x80, 0xc9, 0xfc, 0x0e,
	0x80, 0x94, 0xfc, 0x0f, 0x80, 0x9e, 0x04, 0x3d, 0x80, 0xad, 0xfc, 0x80, 0xad, 0xfc, 0x80, 0x9b,
	0xfc, 0x70, 0x10, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0,
	0xd0, 0xd0, 0xd0, 0x70, 0x10, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0,
	0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0x60, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0x70, 0x10, 0x80, 0x82,
	0x04, 0x80, 0x82, 0x04, 0x80, 0xc0, 0x03, 0x80, 0x82, 0x04, 0x80, 0x82, 0x04, 0x80, 0x82, 0x04,
	0x80, 0x82, 0x04, 0x80, 0x82, 0x04, 0x80, 0x82, 0x04, 0x80, 0x37, 0x04, 0x80, 0x82, 0x04, 0x80,
	0x82, 0x04, 0x80, 0x82, 0x04, 0x80, 0x82, 0x04, 0x80, 0x82, 0x04, 0x80, 0x93, 0x04, 0x08, 0x01,
	0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d, 0xff, 0x0f, 0xff, 0x08,
	0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d, 0xff, 0x0f, 0xff,
	0x08, 0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d, 0xff, 0x0f,
	0xff, 0x08, 0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d, 0xff,
	0x0f, 0xff, 0x08, 0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d,
	0xff, 0x0f, 0xff, 0x08, 0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff,
	0x0d, 0xff, 0x0f, 0xff, 0x08, 0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b,
	0xff, 0x0d, 0xff, 0x0f, 0xff, 0x08, 0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff,
	0x0b, 0xff, 0x0d, 0xff, 0x0f, 0xff, 0x08, 0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09,
	0xff, 0x0b, 0xff, 0x0d, 0xff, 0x0f, 0xff, 0x03, 0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x08, 0x01,
	0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d, 0xff, 0x0f, 0xff, 0x08,
	0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d, 0xff, 0x0f, 0xff,
	0x08, 0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d, 0xff, 0x0f,
	0xff, 0x08, 0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d, 0xff,
	0x0f, 0xff, 0x08, 0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0b, 0xff, 0x0d,
	0xff, 0x0f, 0xff, 0x05, 0x01, 0xff, 0x03, 0xff, 0x05, 0xff, 0x07, 0xff, 0x09, 0xff, 0x0d, 0x00,
	0x80, 0xda, 0xfa, 0x01, 0x80, 0x08, 0xfb, 0x02, 0x80, 0xda, 0xfa, 0x03, 0x80, 0x12, 0xfb, 0x04,
	0x80, 0x08, 0xfb, 0x05, 0x80, 0xff, 0x04, 0x06, 0x80, 0xda, 0xfa, 0x08, 0x80, 0xda, 0xfa, 0x09,
	0x80, 0xda, 0xfa, 0x0a, 0x80, 0xda, 0xfa, 0x0b, 0x80, 0xca, 0xfa, 0x0d, 0x80, 0xcd, 0xfa, 0x0e,
	0x80, 0xca, 0xfa, 0x70, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x60, 0x08, 0x08,
	0x08, 0x08, 0x08, 0x08, 0x70, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x70, 0x08,
	0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x60, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x04,
	0x01, 0x08, 0x03, 0x08, 0x05, 0x08, 0x07, 0x08, 0x70, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
	0x08, 0x08, 0x70, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x70, 0x08, 0x08, 0x08,
	0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x70, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
	0x20, 0x08, 0x08, 0x20, 0x08, 0x08, 0x20, 0x08, 0x08, 0x14, 0x80, 0xc3, 0xfa, 0x2d, 0x80, 0xbc,
	0xfa, 0x80, 0xaa, 0xfa, 0x70, 0x10, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6,
	0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0x70, 0x0a, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6,
	0xe6, 0xe6, 0x1f, 0x80, 0x9a, 0xfa, 0x24, 0x80, 0x93, 0xfa, 0x80, 0x82, 0xfa, 0x71, 0x0f, 0xe0,
	0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0x70, 0x0b,
	0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0,
};
// Lookup table to convert Lowercase to Uppercase if character value < 256
static S8 s_UnicodeLc2UcLookup[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0,
	0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0,
	0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0x00, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0x79,
};

static __forceinline bool unicodeTrieLookup(U8 *pTrie, U16 uKey, S16 *piValue)
{
	// The script that generated the tables used by this function is in
	// C:\cryptic\tools\programmers\unicode\unicodeCaseConvert.lua
	// You can give your regards when you meet me in hell. -JM
	enum {
		KeyValuePair, LookupTable
	} mode = KeyValuePair;
	S32 iStart = 0;
	S32 iMask = TRIE_MASK;
	S32 iShift = TRIE_FIRST_SHIFT;
	while (true)
	{
		U8 *ptr = pTrie + iStart;
		U16 uFirst, uCount;
		U16 uNodeKey = (iMask & uKey) >> iShift;
		S16 iNodeValue = 0;
		bool bMatched;

		// If the current unit is -128, then the actual unit is stored in the next 2 bytes.
#define TRIE_READ_UNIT() ((S16)(*ptr != 0x80 ? (*ptr >= 128 ? *ptr++ - 256 : *ptr++) : (ptr++, *(*(U16**)&ptr)++)))
		uCount = TRIE_READ_UNIT();
		switch (mode)
		{
		xcase KeyValuePair:
			// Count is raw
			// 2 units per pair: key unit, value unit
			bMatched = false;
			while (uCount-- > 0 && !bMatched)
			{
				U16 match = TRIE_READ_UNIT();
				if (uNodeKey < match)
					break;
				iNodeValue = TRIE_READ_UNIT();
				bMatched = match == uNodeKey;
			}
			if (!bMatched)
				iNodeValue = 0;
		xcase LookupTable:
			// Count is actually a compacted value:
			// - First value in lookup table is stored in lower 4 bits
			// - Actual number of entries is in the upper nibble. If the
			//   upper nibble is 7, then the count is stored in next unit.
			//   This saves 1 byte, since the count will never be more than
			//   16 entries.
			uFirst = uCount & 0xf;
			uCount = (uCount & 0x70) == 0x70 ? TRIE_READ_UNIT() : (uCount & 0x70) >> 4;
			if (uNodeKey >= uFirst && (uNodeKey -= uFirst) < uCount)
			{
				// Consume the ignored units
				while (uNodeKey-- > 0)
					TRIE_READ_UNIT();
				iNodeValue = TRIE_READ_UNIT();
			}
			else
				iNodeValue = 0;
		}
#undef TRIE_READ_UNIT

		if (!iNodeValue)
			break;

		if (!iShift)
		{
			*piValue = iNodeValue;
			return true;
		}

		iMask >>= TRIE_SHIFT;
		iShift -= TRIE_SHIFT;
		iStart = ABS(iNodeValue);
		mode = iNodeValue < 0 ? LookupTable : KeyValuePair;
	}
	return false;
}

wchar_t UnicodeToUpper(wchar_t c)
{
	S16 iOffset;
	if (c >= 0 && c < 256)
		return c + s_UnicodeLc2UcLookup[c];
	if (unicodeTrieLookup(s_UnicodeLc2UcTrie, (U16)c, &iOffset))
		return c + iOffset;
	return c;
}

wchar_t UnicodeToLower(wchar_t c)
{
	S16 iOffset;
	if (c >= 0 && c < 256)
		return c + s_UnicodeUc2LcLookup[c];
	if (unicodeTrieLookup(s_UnicodeUc2LcTrie, (U16)c, &iOffset))
		return c + iOffset;
	return c;
}

bool CommaSeparatedListContainsWord(const char *pList, const char *pWord)
{
	size_t iWordLen = strlen(pWord);
	const char *pStartingList = pList;

	while (1)
	{
		char *pFound = strstri(pList, pWord);

		if (!pFound)
		{
			return false;
		}

		if (pFound == pStartingList || *(pFound - 1) == ',' || IS_WHITESPACE( *(pFound - 1)))
		{
			char after = *(pFound + iWordLen);
			if (after == 0 || after == ',' || IS_WHITESPACE(after))
			{
				return true;
			}
		}

		pList = pFound + iWordLen;
	}
}

bool StringsMatchRespectingNumbers(const char *pStr1, const char *pStr2)
{
	bool b1Exists = pStr1 && pStr1[0];
	bool b2Exists = pStr2 && pStr2[0];
	int iInt1, iInt2;
	float fFloat1, fFloat2;

	if (!b1Exists)
	{
		if (!b2Exists)
		{
			return true;
		}

		return false;
	}

	if (!b2Exists)
	{
		return false;
	}

	if (StringToInt_Paranoid(pStr1, &iInt1))
	{
		if (StringToInt_Paranoid(pStr2, &iInt2))
		{
			return iInt1 == iInt2;
		}
		else if (StringToFloat(pStr2, &fFloat2))
		{
			return iInt1 == fFloat2;
		}
	}
	else if (StringToFloat(pStr1, &fFloat1))
	{
		if (StringToInt_Paranoid(pStr2, &iInt2))
		{
			return fFloat1 == iInt2;
		}
		else if (StringToFloat(pStr2, &fFloat2))
		{
			return fFloat1 == fFloat2;
		}
	}

	return stricmp(pStr1, pStr2) == 0;
}

///////////////////////////////
//
//Beyond-ASCII
//
/////////
// Call MakeSureReductionsAreReady() before accessing this stashtable, except during loading.
StashTable stCodepointReductionInfo;

AUTO_STRUCT;
typedef struct ReductionInfo
{
	// codepoint it will reduce to
	U16 uReducedCodepoint;

	// flags
	U16 bIsBasic : 1;
	U16 bIsExtended : 1;
	U16 bIsPunctuation : 1;
	U16 bIsSeparator : 1;

	// the UTF8 encoded chars in here for quicker re-encoding
	char UTF8Encoded[5]; AST( DEFAULT("") )
} ReductionInfo;


// This structure represents a single UTF8 character.
// A string pointer is used to allow multi-byte and multi-code point characters.
// It appears in the file like:   Value "A"
AUTO_STRUCT;
typedef struct CharValue
{
	char *pcCharValue;   AST(STRUCTPARAM)
} CharValue;

// This structure represents a list of char values
// It appears in the file like:  ValueList "A", "B", "C"
AUTO_STRUCT;
typedef struct CharList
{
	STRING_EARRAY eaCharList;   AST(STRUCTPARAM)
} CharList;

// This structure represents a range of characters from a start char to an end char
// It appears in the file like:  Range "A" "Z"
AUTO_STRUCT;
typedef struct CharRange
{
	CharValue* startChar;  AST(STRUCTPARAM)
	CharValue* endChar;    AST(STRUCTPARAM)
} CharRange;

AUTO_STRUCT;
typedef struct CharCollection
{
	EARRAY_OF(CharRange) eaRange;
	EARRAY_OF(CharValue) eaValue;
	EARRAY_OF(CharList) eaValueList;
} CharCollection;

// This structure represents a set of character mappings as a set of source and target 
// values and ranges.  Validation should ensure that there is a one-to-one mapping between
// source and target ranges and values, and that no character appears twice as a source.
AUTO_STRUCT;
typedef struct CharMapping
{
	EARRAY_OF(CharRange) eaSourceRange;
	EARRAY_OF(CharRange) eaTargetRange;
	EARRAY_OF(CharValue) eaSourceValue;
	EARRAY_OF(CharValue) eaTargetValue;
} CharMapping;

AUTO_STRUCT;
typedef struct CharSetLanguage
{
	const char * pcLang; AST(STRUCTPARAM SUBTABLE(LanguageEnum))
} CharSetLanguage;

// This structure represents a character set definition
// Validation should ensure that a character is not put in more than one category.
AUTO_STRUCT;
typedef struct CharSet
{
	char *pcName; AST(KEY) // The name of the charset
	EARRAY_OF(CharSetLanguage) eaSupportedLanguages; AST(NAME(Language))

	// Defining the character categories
	CharCollection *pBasicChars;
	CharCollection *pExtendedChars;
	CharCollection *pPunctuationChars;
	CharCollection *pSeparatorChars;

	// Defining mappings
	CharMapping *pReductionMapping;
} CharSet;

AUTO_STRUCT;
typedef struct CharSetList
{
	EARRAY_OF(CharSet) eaCharSets; AST(NAME(CharSet))
} CharSetList;

// Test Reduction algorigthm
void TestReduction(void)
{
	char *estrReduced = NULL;
	estrCreate(&estrReduced);

	// blank string
	UTF8ReduceString("", &estrReduced);
	assert(  !strcmp("", estrReduced));

	// U+0021 - U+0040, Codepoints up to A
	UTF8ReduceString("!\"#$%&'()*+,-./0123456789:;<=>?@", &estrReduced);
	assert(  !strcmp("!\"#$%&'()*+,-./0123456789:;<=>?@", estrReduced));

	// U+0041 - U+005A, capital letters
	UTF8ReduceString("ABCDEFGHIJKLMNOPQRSTUVWXYZ", &estrReduced);
	assert(  !strcmp("abcdefghijklmnopqrstuvwxyz", estrReduced));

	// U+005B - U+0060, symbols between alphabets
	UTF8ReduceString("[\\]^_`", &estrReduced);
	assert(  !strcmp("[\\]^_`", estrReduced));

	// U+0061 - U+007A, lower case letters
	UTF8ReduceString("abcdefghijklmnopqrstuvwxyz", &estrReduced);
	assert(  !strcmp("abcdefghijklmnopqrstuvwxyz", estrReduced));

	// U+007B - U+00BF, symbols, up to the accented letters
	UTF8ReduceString("{|}~ ¡¢£¤¥¦§¨©ª«¬­®¯°±²³´µ¶·¸¹º»¼½¾¿", &estrReduced);
	assert(  !strcmp("{|}~ ¡¢£¤¥¦§¨©ª«¬­®¯°±²³´µ¶·¸¹º»¼½¾¿", estrReduced));

	// U+00C0 - U+00DF, accented upper case
	UTF8ReduceString("ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞß", &estrReduced);
	//assert(  !strcmp("AAAAAAAECEEEEIIIIDNOOOOO×OUUUUYÞß", estrReduced));

	// U+00E0 - U+00FF, accented lower case
	UTF8ReduceString("àáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿ", &estrReduced);
	//assert(  !strcmp("aaaaaaaeceeeeiiiionooooo÷ouuuuyþy", estrReduced));

	// TODO Multi-byte encoded strings

	// Test some of the UTF8CodepointIsXXX functions
	assert( UTF8CodepointIsBasic(UTF8ToCodepoint("a")));
	assert( UTF8CodepointIsBasic(UTF8ToCodepoint("A")));
	assert(!UTF8CodepointIsBasic(UTF8ToCodepoint("0")));
	assert(!UTF8CodepointIsBasic(UTF8ToCodepoint("'")));
	assert(!UTF8CodepointIsBasic(UTF8ToCodepoint(" ")));
	assert(!UTF8CodepointIsBasic(UTF8ToCodepoint("!")));
	assert(!UTF8CodepointIsBasic(UTF8ToCodepoint("\x99\xB2")));

	assert(!UTF8CodepointIsExtended(UTF8ToCodepoint("a")));
	assert(!UTF8CodepointIsExtended(UTF8ToCodepoint("A")));
	assert( UTF8CodepointIsExtended(UTF8ToCodepoint("0")));
	assert(!UTF8CodepointIsExtended(UTF8ToCodepoint("'")));
	assert(!UTF8CodepointIsExtended(UTF8ToCodepoint(" ")));
	assert(!UTF8CodepointIsExtended(UTF8ToCodepoint("!")));
	assert(!UTF8CodepointIsExtended(UTF8ToCodepoint("\x99\xB2")));

	assert(!UTF8CodepointIsPunctuation(UTF8ToCodepoint("a")));
	assert(!UTF8CodepointIsPunctuation(UTF8ToCodepoint("A")));
	assert(!UTF8CodepointIsPunctuation(UTF8ToCodepoint("0")));
	assert( UTF8CodepointIsPunctuation(UTF8ToCodepoint("'")));
	assert(!UTF8CodepointIsPunctuation(UTF8ToCodepoint(" ")));
	assert(!UTF8CodepointIsPunctuation(UTF8ToCodepoint("!")));
	assert(!UTF8CodepointIsPunctuation(UTF8ToCodepoint("\x99\xB2")));

	assert(!UTF8CodepointIsSeparator(UTF8ToCodepoint("a")));
	assert(!UTF8CodepointIsSeparator(UTF8ToCodepoint("A")));
	assert(!UTF8CodepointIsSeparator(UTF8ToCodepoint("0")));
	assert(!UTF8CodepointIsSeparator(UTF8ToCodepoint("'")));
	assert( UTF8CodepointIsSeparator(UTF8ToCodepoint(" ")));
	assert(!UTF8CodepointIsSeparator(UTF8ToCodepoint("!")));
	assert(!UTF8CodepointIsSeparator(UTF8ToCodepoint("\x99\xB2")));

	estrDestroy(&estrReduced);
}

U32 CharRangeGetCount(CharRange *pRange)
{
	U32 codePtEnd = UTF8ToCodepoint(pRange->endChar->pcCharValue);
	U32 codePtStart = UTF8ToCodepoint(pRange->startChar->pcCharValue);
	return (codePtEnd - codePtStart);
}


///////////
// A bunch of handy-dandy functions to add ReductionInfo structs to the stashtable, and initialize one of the values on it.
// 
typedef void (*CodePointCallback)(U32, void*);

void AddBasicCodepointToStashTable(U32 codePt, void* pUserData)
{
	ReductionInfo *pReductionInfo;
	StashElement element;
	if( !stashIntFindElement(stCodepointReductionInfo, codePt, &element) )
		stashIntAddPointerAndGetElement(stCodepointReductionInfo, codePt, StructCreate(parse_ReductionInfo), true, &element);

	pReductionInfo = stashElementGetPointer(element);
	pReductionInfo->bIsBasic = true;
	if( pReductionInfo->uReducedCodepoint == 0 )
		pReductionInfo->uReducedCodepoint = codePt;
}

void AddExtendedCodepointToStashTable(U32 codePt, void* pUserData)
{
	ReductionInfo *pReductionInfo;
	StashElement element;
	if( !stashIntFindElement(stCodepointReductionInfo, codePt, &element) )
		stashIntAddPointerAndGetElement(stCodepointReductionInfo, codePt, StructCreate(parse_ReductionInfo), true, &element);

	pReductionInfo = stashElementGetPointer(element);
	pReductionInfo->bIsExtended = true;
	if( pReductionInfo->uReducedCodepoint == 0 )
		pReductionInfo->uReducedCodepoint = codePt;
}

void AddPunctuationCodepointToStashTable(U32 codePt, void* pUserData)
{
	ReductionInfo *pReductionInfo;
	StashElement element;
	if( !stashIntFindElement(stCodepointReductionInfo, codePt, &element) )
		stashIntAddPointerAndGetElement(stCodepointReductionInfo, codePt, StructCreate(parse_ReductionInfo), true, &element);

	pReductionInfo = stashElementGetPointer(element);
	pReductionInfo->bIsPunctuation = true;
	if( pReductionInfo->uReducedCodepoint == 0 )
		pReductionInfo->uReducedCodepoint = codePt;
}

void AddSeparatorCodepointToStashTable(U32 codePt, void* pUserData)
{
	ReductionInfo *pReductionInfo;
	StashElement element;
	if( !stashIntFindElement(stCodepointReductionInfo, codePt, &element) )
		stashIntAddPointerAndGetElement(stCodepointReductionInfo, codePt, StructCreate(parse_ReductionInfo), true, &element);

	pReductionInfo = stashElementGetPointer(element);
	pReductionInfo->bIsSeparator = true;
	if( pReductionInfo->uReducedCodepoint == 0 )
		pReductionInfo->uReducedCodepoint = codePt;
}

void AddReducedCodepointToStashTable(U32 codePt, void* pUserData)
{
	ReductionInfo *pReductionInfo;
	StashElement element;
	if( !stashIntFindElement(stCodepointReductionInfo, codePt, &element) )
		stashIntAddPointerAndGetElement(stCodepointReductionInfo, codePt, StructCreate(parse_ReductionInfo), true, &element);

	pReductionInfo = stashElementGetPointer(element);
	pReductionInfo->uReducedCodepoint = *((U32*)pUserData);
}

// Call the callback on each codepoint in the CharRange.
void CharRangeForEach(CharRange *pRange, CodePointCallback cb, void *pUserData)
{
	U32 codePtEnd = UTF8ToCodepoint(pRange->endChar->pcCharValue);
	U32 codePtStart = UTF8ToCodepoint(pRange->startChar->pcCharValue);
	U32 codePt;
	
	if( !cb )
		return;

	for( codePt = codePtStart; codePt <= codePtEnd; ++codePt )
	{
		cb(codePt, pUserData);
	}
}

// Count the number of codepoints in all the ranges in the collection
U32 CharCollectionGetCount(CharCollection *pCollection)
{
	U32 iCharCount = 0;
	int i;
	for( i=0; i < eaSize(&pCollection->eaRange); ++i )
	{
		CharRange *pRange = pCollection->eaRange[i];
		iCharCount += CharRangeGetCount(pRange);
	}
	return iCharCount;
}

// Call the callback on each codepoint in each part of the CharCollection.
void CharCollectionForEach(CharCollection *pCollection, CodePointCallback cb, void *pUserData)
{
	int i,j;
	for( i=0; i < eaSize(&pCollection->eaRange); ++i )
	{
		CharRange *pRange = pCollection->eaRange[i];
		CharRangeForEach(pRange, cb, pUserData);
	}
	for( i=0; i < eaSize(&pCollection->eaValue); ++i )
	{
		cb(UTF8ToCodepoint(pCollection->eaValue[i]->pcCharValue), pUserData);
	}
	for( i=0; i < eaSize(&pCollection->eaValueList); ++i )
	{
		CharList *pList = pCollection->eaValueList[i];
		for( j=0; j < eaSize(&pList->eaCharList); ++j )
		{
			cb(UTF8ToCodepoint(pList->eaCharList[j]), pUserData);
		}
	}
}

// Copy from pCharSet to stCodepointReductionInfo.
void InitReductionData(CharSet *pCharSet)
{
	// Count the number of characters needed.
	U32 iCharCount = 0;
	if( !pCharSet )
		return; // Do nothing, give-up, something horrible happened on loading the .charset file.

	if( pCharSet->pBasicChars )
	{
		iCharCount += CharCollectionGetCount(pCharSet->pBasicChars);
	}
	if( pCharSet->pExtendedChars )
	{
		iCharCount += CharCollectionGetCount(pCharSet->pExtendedChars);
	}
	if( pCharSet->pPunctuationChars )
	{
		iCharCount += CharCollectionGetCount(pCharSet->pPunctuationChars);
	}
	if( pCharSet->pSeparatorChars )
	{
		iCharCount += CharCollectionGetCount(pCharSet->pSeparatorChars);
	}
	// Note: This might double-count any codepoints that are reduced and in one of the sets above.
	if( pCharSet->pReductionMapping )
	{
		int i;
		EARRAY_OF(CharRange) eaSourceRange = pCharSet->pReductionMapping->eaSourceRange;
		EARRAY_OF(CharRange) eaTargetRange = pCharSet->pReductionMapping->eaTargetRange;
		EARRAY_OF(CharValue) eaSourceValue = pCharSet->pReductionMapping->eaSourceValue;
		EARRAY_OF(CharValue) eaTargetValue = pCharSet->pReductionMapping->eaTargetValue;

		// Count each character in the SourceRange
		assert( eaSize(&eaSourceRange) == eaSize(&eaTargetRange) );
		for( i=0; i<eaSize(&eaSourceRange); ++i )
		{
			U32 codePtSourceStart = UTF8ToCodepoint(eaSourceRange[i]->startChar->pcCharValue);
			U32 codePtSourceEnd   = UTF8ToCodepoint(eaSourceRange[i]->endChar->pcCharValue);
			iCharCount += (codePtSourceEnd - codePtSourceStart);
		}

		// Count each character in the SourceValue.
		assert( eaSize(&eaSourceValue) == eaSize(&eaTargetValue) );
		iCharCount += eaSize(&eaSourceValue);
	}


	// Create a stashtable to map a codepoint to the ReductionInfo
	assert(!stCodepointReductionInfo);
	stCodepointReductionInfo = stashTableCreateInt(2*iCharCount); // Twice the number of ReductionInfos seems like a good initial guess

	// Add a ReductionInfo block for each codepoint in any CharCollection
	if( pCharSet->pBasicChars )
	{
		CharCollectionForEach(pCharSet->pBasicChars, AddBasicCodepointToStashTable, NULL);
	}
	if( pCharSet->pExtendedChars )
	{
		CharCollectionForEach(pCharSet->pExtendedChars, AddExtendedCodepointToStashTable, NULL);
	}
	if( pCharSet->pPunctuationChars )
	{
		CharCollectionForEach(pCharSet->pPunctuationChars, AddPunctuationCodepointToStashTable, NULL);
	}
	if( pCharSet->pSeparatorChars )
	{
		CharCollectionForEach(pCharSet->pSeparatorChars, AddSeparatorCodepointToStashTable, NULL);
	}

	// ReductionMapping
	if( pCharSet->pReductionMapping )
	{
		int i;
		EARRAY_OF(CharRange) eaSourceRange = pCharSet->pReductionMapping->eaSourceRange;
		EARRAY_OF(CharRange) eaTargetRange = pCharSet->pReductionMapping->eaTargetRange;
		EARRAY_OF(CharValue) eaSourceValue = pCharSet->pReductionMapping->eaSourceValue;
		EARRAY_OF(CharValue) eaTargetValue = pCharSet->pReductionMapping->eaTargetValue;

		// For each character in the SourceRange, add a codepoint for the TargetRange.
		assert( eaSize(&eaSourceRange) == eaSize(&eaTargetRange) );
		for( i=0; i<eaSize(&eaSourceRange); ++i )
		{
			U32 codePtSourceStart = UTF8ToCodepoint(eaSourceRange[i]->startChar->pcCharValue);
			U32 codePtSourceEnd   = UTF8ToCodepoint(eaSourceRange[i]->endChar->pcCharValue);
			U32 codePtTargetStart = UTF8ToCodepoint(eaTargetRange[i]->startChar->pcCharValue);
			U32 codePtTargetEnd   = UTF8ToCodepoint(eaTargetRange[i]->endChar->pcCharValue);
			U32 codePtSource, codePtTarget;

			// Make sure the source and target ranges are the same length
			assert( (codePtSourceEnd - codePtSourceStart) == (codePtTargetEnd - codePtTargetStart) );

			for( codePtSource = codePtSourceStart, codePtTarget = codePtTargetStart; codePtSource <= codePtSourceEnd; ++codePtSource, ++codePtTarget)
			{
				AddReducedCodepointToStashTable(codePtSource, &codePtTarget);
			}
		}

		// For each character in the SourceValue, add a codepoint for the TargetValue.
		assert( eaSize(&eaSourceValue) == eaSize(&eaTargetValue) );
		for( i=0; i<eaSize(&eaSourceValue); ++i )
		{
			U32 codePtSource = UTF8ToCodepoint(eaSourceValue[i]->pcCharValue);
			U32 codePtTarget = UTF8ToCodepoint(eaTargetValue[i]->pcCharValue);
			AddReducedCodepointToStashTable(codePtSource, &codePtTarget);
		}
	}
}

// On both client and server, we need to load the language-specific reduction data.
void LoadLanguageCharset(void)
{
	CharSetList *pCharSetList = StructCreate(parse_CharSetList);
	CharSet *pCharSetFound = NULL;
	ParserLoadFiles(GetDirForBaseConfigFiles(), ".charset", "charsets.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, parse_CharSetList, pCharSetList);
	FOR_EACH_IN_EARRAY(pCharSetList->eaCharSets, CharSet, pCharSet)
	{
		// If any of the languages in this charset are supported, use this charset.
		FOR_EACH_IN_EARRAY(pCharSet->eaSupportedLanguages, CharSetLanguage, pLang)
		{
			Language lang = StaticDefineIntGetInt(LanguageEnum, pLang->pcLang);
			if( langIsSupportedThisShard(lang) )
			{
				pCharSetFound = pCharSet;
				InitReductionData(pCharSet);
				break;
			}
		}
		FOR_EACH_END

		// Only one charset can be loaded at a time.
		if(pCharSetFound)
			break;
	}
	FOR_EACH_END
	assertmsg(pCharSetFound, "UTF8ReduceString needs a .charset file before it can be used.");

	StructDestroy(parse_CharSetList, pCharSetList);

	if( isDevelopmentMode() )
		TestReduction(); // Run a little test to make sure nobody did anything stupid with the .charset
}

// Call this before accessing anything from stCodepointReductionInfo, outside of loading.
// It'll load the ReductionInfo if possible, and throw an error if none was loaded.
void MakeSureReductionsAreReady()
{
	if( !stCodepointReductionInfo )
	{
		LoadLanguageCharset();
	}
	assertmsg(stCodepointReductionInfo, "You didn't load a .charset file for this locale. Add one to \"data/server/config/*.charset\"");
}

// Does NOT allocate an EString to pestrReduced
void UTF8ReduceString(const char *pOrig, char **pestrReduced)
{
	U32 codePt;
	const char *pC;
	StashElement element;
	const char *pBuf;
	MakeSureReductionsAreReady();

	assert( pestrReduced );
	//estrReserveCapacity(4*UTF8GetLength(pOrig)); // TODO: Should we reserve some capacity?
	estrClear(pestrReduced);

	if( !pOrig )
		return;

	// Lookup each codepoint in pOrig, reduce it, add it to pestrReduced.
	for(pC = pOrig; *pC; pC = UTF8GetNextCodepoint(pC))
	{
		codePt = UTF8ToCodepoint(pC);
		if( stashIntFindElement(stCodepointReductionInfo, codePt, &element) )
		{
			// Found a reduction, so reduce, then reencode.
			ReductionInfo *pReductionInfo = stashElementGetPointer(element);
			assert(pReductionInfo);
			if( *pReductionInfo->UTF8Encoded == '\0' )
			{
				// Cache the re-encoding in the ReductionInfo, so we only have to do it once per codepoint.
				pBuf = WideToUTF8CodepointConvert(pReductionInfo->uReducedCodepoint); // returns a pointer to a static buffer. Be careful!
				assert(strlen(pBuf) <= 4);
				strncpy(pReductionInfo->UTF8Encoded, pBuf, 5);
			}
			estrConcat(pestrReduced, pReductionInfo->UTF8Encoded, (unsigned int)strlen(pReductionInfo->UTF8Encoded));
		}
		else
		{
			// No reduction needed, so copy the already-encoded bytes.
			estrConcat(pestrReduced, pC, UTF8GetCodepointLength(pC));
		}
	}
}

// If you want to enable USE_NORMALIZ_DLL, you have to make sure you're running on Windows Vista or newer, or you've distributed the Normaliz.dll
// available as part of the Microsoft Internationalized Domain Names (IDN) Mitigation APIs 1.1 at:
// http://www.microsoft.com/en-us/download/details.aspx?id=734
//#define USE_NORMALIZ_DLL

// Use ICU's Norm2 API, statically-linked.
#define USE_ICU_NORM2

// Use the crappy FoldString() API to try to normalize
//#define USE_FOLD_STRING

#if defined(USE_NORMALIZ_DLL)
// Stolen from the winnls.h, so we can use NormalizeString() on Windows XP from Normaliz.dll directly.
// TODO: Change this to use the Normalization.h from Microsoft Internationalized Domain Names (IDN) Migration APIs 1.1
// http://www.microsoft.com/en-us/download/confirmation.aspx?id=734
// otherwise, it won't work on Windows XP, where Normaliz.dll doesn't exist.
typedef enum NORM_FORM
{ 
	NormalizationOther  = 0,
	NormalizationC      = 0x1,
	NormalizationD      = 0x2,
	NormalizationKC     = 0x5,
	NormalizationKD     = 0x6
} NORM_FORM;

typedef int (__stdcall *NormalizeStringFuncPtr)(NORM_FORM, LPCWSTR, int, LPWSTR, int);
NormalizeStringFuncPtr NormalizeString = NULL;

typedef BOOL (__stdcall *IsNormalizedStringFuncPtr)(NORM_FORM, LPCWSTR, int);
IsNormalizedStringFuncPtr IsNormalizedString = NULL;

#elif defined(USE_ICU_NORM2)

#define U_STATIC_IMPLEMENTATION // Link ICU with the static lib interface
#include "../../3rdparty/ICU/include/unicode/unorm2.h" // ICU C API for Normalization
#include "../../3rdparty/ICU/include/unicode/udata.h" // ICU C API for Data loading

#ifdef _WIN64
#pragma comment(lib, "icuuc51d_64.lib")
#pragma comment(lib, "icudt51_64.lib")
#else
#pragma comment(lib, "icuuc51d.lib")
#pragma comment(lib, "icudt51.lib")
#endif

// If you have to generate this data file again, compile this and run it on "3rdparty/ICU/include/data/icudt51l.dat".
// Example arguments: 
//    "C:/src/3rdparty/ICU/include/data/icudt51l.dat" > icudt51l.c
/*
#include <stdio.h>
int main (int argc, char **argv)
{
	FILE *inFile;
	FILE *outFile = stdout;
	int ch, i;

	if (argc != 2)
	{
		printf("Usage: %s infile [> outfile]", argv[0]);
		return 1;
	}

	inFile = fopen(argv[1], "rb");
	if (inFile == NULL)
	{
		printf("Cannot open %s\n", argv[1]);
		return 2;
	}

	i = 0;
	while ((ch = fgetc(inFile)) != EOF)
	{
		if (i++ % 12 == 0)
			fputs ("\n\t", outFile);
		fprintf (outFile, "0x%02X,", ch);
	}
	fputc ('\n', outFile);
	fclose (inFile);
	return (0);
}
*/
const char ICU_CommonData[] =
{
#include "../../3rdparty/ICU/include/data/icudt51l.c"
};

#elif defined(USE_FOLD_STRING)

// Include Stuff

#endif

// Checks if the whole string ONLY contains simple ASCII codepoints, <128 
bool UTF8IsASCII(const char *pUTF8String)
{
	const char *pC = pUTF8String;
	if(!pUTF8String) return true;
	for( ; *pC; ++pC )
	{
		if( *pC & 0x80 )
			return false;
	}
	return true;
}

// Does NOT allocate an EString to pestrNormalized
void UTF8NormalizeString(const char *pOrig, char **pestrNormalized)
{
	// Beyond-ASCII
	assert( pestrNormalized );
	estrClear(pestrNormalized);

	if( UTF8IsASCII(pOrig) )
	{
		// ASCII strings are already normalized
		estrCopy2(pestrNormalized, pOrig);
		return;
	}

#if defined(USE_NORMALIZE_DLL)
	{
	wchar_t *lpSrcString = NULL;	
	wchar_t *lpDstString = NULL;
	int cwDstLength = 0;
	DWORD dwError;
	int i;

	// Make sure NormalizeString() is loaded from the dll
	if( !NormalizeString )
	{
		HINSTANCE hinstLib = LoadLibrary(TEXT("Normaliz.dll"));
		if( hinstLib )
		{
			NormalizeString = (NormalizeStringFuncPtr) GetProcAddress(hinstLib, "NormalizeString");
			IsNormalizedString = (IsNormalizedStringFuncPtr) GetProcAddress(hinstLib, "IsNormalizedString");
		}
	}

	assertmsg(NormalizeString, "Normaliz.dll could not be loaded. Or it doesn't contain NormalizeString()");
	assertmsg(IsNormalizedString, "Normaliz.dll could not be loaded. Or it doesn't contain IsNormalizedString()");

	//
	// Convert from char to wide char.
	//
	{
		int cwSrcLength = UTF8ToWideStrConvert(pOrig, NULL, 0);
		if( cwSrcLength <= 0 )
		{
			// Total failure, give up now
			return;
		}

		// Allocate a buffer for the wide char version long
		lpSrcString = malloc((1+cwSrcLength) * sizeof(wchar_t));
		UTF8ToWideStrConvert(pOrig, lpSrcString, cwSrcLength + 1);
	}


	// Do the semi-quick check if the string is already Normalized, to avoid more allocations
	if( IsNormalizedString(NormalizationC, lpSrcString, -1) )
	{
		// Already Normal, so copy the original
		estrCopy2(pestrNormalized, pOrig);
		free(lpSrcString);
		return;
	}


	// Figure how much space we need after normalization
	cwDstLength = NormalizeString(NormalizationC, lpSrcString, -1, NULL, 0);
	assert(cwDstLength != 0); // returning 0 is an error
	for(i=0; i<10; ++i) // 10 tries to get it right
	{
		free(lpDstString);
		lpDstString = malloc(cwDstLength * sizeof(WCHAR));

		// Do the actual normalize
		cwDstLength = NormalizeString(NormalizationC, lpSrcString, -1, lpDstString, cwDstLength);

		if( cwDstLength > 0 )
			break; // success

		dwError = GetLastError();
		if( dwError == ERROR_NO_UNICODE_TRANSLATION || dwError == ERROR_INSUFFICIENT_BUFFER )
		{
			// If cwDstLength is negative, an invalid Unicode byte was found at -cwDstLength, so truncate to that.
			// Or the outbuffer wasn't big enough, so try again with a bigger buffer.
			cwDstLength = -cwDstLength;
		}
		else
		{
			// something went really wrong, and we can't recover from it
			break;
		}
	}

	// Convert back from wchar to char
	{
		// Figure out how much space we need in UTF8
		cwDstLength = WideToUTF8StrConvert(lpDstString, NULL, 0);

		// Allocate enough capacity in pestrNormalized
		estrSetSize(pestrNormalized, cwDstLength+1);

		// Convert into pestrNormalized
		WideToUTF8StrConvert(lpDstString, *pestrNormalized, cwDstLength);
	}

	// Cleanup the allocations
	free(lpSrcString);
	free(lpDstString);
	}
#elif defined(USE_ICU_NORM2)
	{
	UChar *lpSrcString = NULL;
	UChar *lpDstString = NULL;
	int32_t cwDstLength = 0;
	UErrorCode err = U_ZERO_ERROR;
	const UNormalizer2 * pNormC = NULL;
	udata_setCommonData(ICU_CommonData, &err);
	pNormC = unorm2_getNFCInstance(&err);
	if( U_FAILURE(err) )
		return;
	
	// Convert from char to UChar.
	{
		int cwSrcLength = UTF8ToWideStrConvert(pOrig, NULL, 0);
		if( cwSrcLength <= 0 )
		{
			// Total failure, give up now
			return;
		}

		// Allocate a buffer for the wide char version long
		lpSrcString = malloc((1+cwSrcLength) * sizeof(UChar));
		UTF8ToWideStrConvert(pOrig, lpSrcString, cwSrcLength + 1);
	}
	
	// Do the semi-quick check if the string is already Normalized, to avoid more allocations
	if( unorm2_isNormalized(pNormC, lpSrcString, -1, &err) )
	{
		// Already Normal , so copy the original
		estrCopy2(pestrNormalized, pOrig);
		free(lpSrcString);
		return;
	}
	if( U_FAILURE(err) )
	{
		free(lpSrcString);
		return;
	}

	// Figure how much space we need after normalization
	cwDstLength = unorm2_normalize(pNormC, lpSrcString, -1, NULL, 0, &err);
	if( U_BUFFER_OVERFLOW_ERROR != err )
	{
		// Failed in some unexpected way. Give up.
		estrCopy2(pestrNormalized, pOrig);
		free(lpSrcString);
		return;
	}
	err = U_ZERO_ERROR;
	lpDstString = malloc((cwDstLength+1) * sizeof(UChar));

	// Do the actual normalize
	cwDstLength = unorm2_normalize(pNormC, lpSrcString, -1, lpDstString, cwDstLength, &err);
	lpDstString[cwDstLength] = '\0';
	if( U_FAILURE(err) )
	{
		// Failed in some unexpected way. Give up.
		estrCopy2(pestrNormalized, pOrig);
		free(lpSrcString);
		free(lpDstString);
		return;
	}

	// Convert back from UChar to char
	{
		// Figure out how much space we need in UTF8
		cwDstLength = WideToUTF8StrConvert(lpDstString, NULL, 0);

		// Allocate enough capacity in pestrNormalized
		estrSetSize(pestrNormalized, cwDstLength+1);

		// Convert into pestrNormalized
		WideToUTF8StrConvert(lpDstString, *pestrNormalized, cwDstLength+1);
	}

	// Cleanup the allocations
	free(lpSrcString);
	free(lpDstString);

	}
#elif defined(USE_FOLD_STRING)
	{
	int cwDstLength = 0;
	// FoldString() does NOT do the standard Unicode Normalization form C, it seems. 
	// Even though the documentation says it does. Apparently, there are missing entries
	// in their replacement table. 
	cwDstLength = FoldString(MAP_PRECOMPOSED, pOrig, -1, NULL, 0);
	assert(cwDstLength != 0); // returning 0 is an error
	estrSetSize(pestrNormalized, cwDstLength);
	cwDstLength = FoldString(MAP_PRECOMPOSED, pOrig, -1, *pestrNormalized, cwDstLength);
	}
#endif
}

void UTF8NormalizeAllStringsInStruct( ParseTable pti[], void* structptr )
{
	int it;
	if( !structptr ) {
		return;
	}

	FORALL_PARSETABLE( pti, it ) {
		ParseTable* cur = &pti[ it ];

		if(   TOK_GET_TYPE( cur->type ) == TOK_STRING_X
			  || TOK_GET_TYPE( cur->type ) == TOK_CURRENTFILE_X
			  || TOK_GET_TYPE( cur->type ) == TOK_FILENAME_X
			  || TOK_GET_TYPE( cur->type ) == TOK_STRUCT_X ) {
			int count = 0;
			int jt;
			if( cur->type & TOK_EARRAY ) {
				void*** pea = TokenStoreGetEArray( pti, it, structptr, NULL );
				count = eaSize( pea );
			} else {
				count = 1;
			}

			for( jt = 0; jt != count; ++jt ) {
				if( TOK_GET_TYPE( cur->type ) == TOK_STRUCT_X ) {
					void* substructptr = TokenStoreGetPointer( pti, it, structptr, jt, NULL );
					UTF8NormalizeAllStringsInStruct( cur->subtable, substructptr );
				} else {
					const char* str = TokenStoreGetString( pti, it, structptr, jt, NULL );
					if( str ) {
						char* estrNormalized = NULL;
						UTF8NormalizeString( str, &estrNormalized );
						TokenStoreSetString( pti, it, structptr, jt, estrNormalized, NULL, NULL, NULL, NULL );
						estrDestroy( &estrNormalized );
					}
				}
			}
		}
	}
}

void UTF8NormalizeStringInplace(char** pstr)
{
	if( !nullStr( *pstr )) {
		char* estr = NULL;
		UTF8NormalizeString( *pstr, &estr );
		free( *pstr );
		*pstr = strdup( estr );
		estrDestroy( &estr );
	}
}

bool UTF8CodepointIsBasic(U32 codePt)
{
	StashElement element;
	MakeSureReductionsAreReady();
	if( !stashIntFindElement(stCodepointReductionInfo, codePt, &element) )
		return false;

	return ((ReductionInfo*)stashElementGetPointer(element))->bIsBasic;
}

bool UTF8CodepointIsExtended(U32 codePt)
{
	StashElement element;
	MakeSureReductionsAreReady();
	if( !stashIntFindElement(stCodepointReductionInfo, codePt, &element) )
		return false;

	return ((ReductionInfo*)stashElementGetPointer(element))->bIsExtended;
}

bool UTF8CodepointIsPunctuation(U32 codePt)
{
	StashElement element;
	MakeSureReductionsAreReady();
	if( !stashIntFindElement(stCodepointReductionInfo, codePt, &element) )
		return false;

	return ((ReductionInfo*)stashElementGetPointer(element))->bIsPunctuation;
}

bool UTF8CodepointIsSeparator(U32 codePt)
{
	StashElement element;
	MakeSureReductionsAreReady();
	if( !stashIntFindElement(stCodepointReductionInfo, codePt, &element) )
		return false;

	return ((ReductionInfo*)stashElementGetPointer(element))->bIsSeparator;
}

#include "AutoGen/StringUtil_h_ast.c"
#include "AutoGen/StringUtil_c_ast.c"
