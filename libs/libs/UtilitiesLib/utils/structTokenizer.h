#ifndef STRUCTTOKENIZER_H
#define STRUCTTOKENIZER_H
GCC_SYSTEM

#include "TextParserEnums.h"

// limitations on tokens
#define MAX_TOKEN_LENGTH 300	// need to allow space for a full filename after #include
#define MAX_STRING_LENGTH (MAX_TOKEN_LENGTH * 200)
#define TOKEN_BUFFER_LENGTH (MAX_STRING_LENGTH * 2)

// basic tokenizer struct.  You can create the tokenizer from a file or in-memory string.
typedef void* TokenizerHandle;
typedef struct DefineContext DefineContext;
typedef struct TextParserState TextParserState;


TokenizerHandle TokenizerCreateEx(const char* filename, TextParserState *pTextParserState, int ignore_empty);	// opens up file and prepares for tokenizing
__forceinline static TokenizerHandle TokenizerCreate(const char* filename, TextParserState *pTextParserState) { return TokenizerCreateEx(filename, pTextParserState, 0); }

TokenizerHandle TokenizerCreateLoadedFile(char *pBuffer, const char *filename, TextParserState *pTextParserState, int ignore_empty);

//for now, uses the same flags as ParserLoadFiles, most of which it ignores. Currently only PARSER_NOERRORFSONPARSE is relevant
TokenizerHandle TokenizerCreateString(const char* string, const char *filename, int iFlags);// sets up parsing for a string



void TokenizerDestroy(TokenizerHandle tokenizer);	// closes all file handles and memory
const char* TokenizerGetFileName(TokenizerHandle tokenizer); // returns file name of current context
int TokenizerGetCurLine(TokenizerHandle tokenizer);
void TokenizerGetFileAndLine(TokenizerHandle tokenizer);

bool TokenizerShouldParseCurrentFile(TokenizerHandle toeknizer);

TextParserState *TokenizerGetTextParserState(TokenizerHandle tokenizer);

int IsEol(char* token); // ! only way to distinguish between an actual eol returned by the Tokenizer and a string containing only \n
#define TokenizerEscape(target,source) TokenizerEscapeLen(target,source,0);
int TokenizerEscapeLen(char* target, const char* source, int len);
int TokenizerUnescape(char* target, const char* source);

extern char *TOK_SPECIAL_INT_RETURN;

//"peekflag" is used for both Peek and Get
#define PEEKFLAG_IGNORELINEBREAK (1 << 0)
#define PEEKFLAG_IGNORECOMMA (1 << 1)
//if this flag is set, and a staticDefine is set, and an enum int is read, then TOK_SPECIAL_INT_RETURN will be returned, and 
//the int that was read will be available through TokenizerGetReadInt
#define PEEKFLAG_CHECKFORSTATICDEFINEINT (1 << 2)

// the key peek and get functions
char* TokenizerPeek(TokenizerHandle tokenizer, U32 iFlags, TextParserResult *parseResult);
	// returns next token or NULL at end of file, VOLATILE RESULT: you must copy result between calls
char* TokenizerGet(TokenizerHandle tokenizer, U32 iFlags, TextParserResult *parseResult);
	// as PeekToken but eats token, VOLATILE RESULT: you must copy result between calls

// The Ex functions let you break down tokens further by seperating along any characters in the <seps> parameter.
// Return values are NOT broken with NULL's like Peek/Get.  You must use the length return value to determine correct
// length of string.  They are compatible with Peek/Get and only return incrementally smaller tokens.
char* TokenizerPeekEx(TokenizerHandle tokenizer, U32 iFlags, char* seps, int* length, TextParserResult *parseResult);
char* TokenizerGetEx(TokenizerHandle tokenizer, U32 iFlags, char* seps, int* length, TextParserResult *parseResult);

void TokenizerSetStaticDefines(TokenizerHandle tokenizer, StaticDefine* list);

//don't call this unless you just got back TOK_SPECIAL_INT_RETURN from TokenizerGet
int TokenizerGetReadInt(TokenizerHandle tokenizer);

//sets extra characters that will be treated as whitespace by TokenizerPeek, etc. Set to NULL to clear/reset
void TokenizerSetExtraDelimiters(TokenizerHandle tokenizer, char *pDelims);

//tells the tokenizer that all error should be reported with this specified line number. Presumably used when the 
//tokenizer is processing an escaped string that was all read out of one line of a data file, or something like that
void TokenizerSetLineNumForErrorReporting(TokenizerHandle tokenizer, int iLineNum);

#define TokenizerErrorf(tokenizer, fmt, ...)  TokenizerErrorfInternal(tokenizer, __FILE__, __LINE__, FORMAT_STRING_CHECKED(fmt), ##__VA_ARGS__)
void TokenizerErrorfInternal(TokenizerHandle tokenizer, const char *file, int line, FORMAT_STR const char *format, ...);

//uses the same flags as ParserLoadFiles
void TokenizerSetFlags(TokenizerHandle tokenizer, U32 iFlags);
U32 TokenizerGetFlags(TokenizerHandle tokenizer);

//when PARSER_IGNORE_ALL_UNKNOWN is set, the tokenizer tracks all "unknown" tokens
void TokenizerReportUnknown(TokenizerHandle tokenizer, char *pToken);

//internally reports a string that will later be sent to a FIXUPTYPE_HERE_IS_IGNORED_FIELD
void TokenizerSetSpecialIgnoreString(TokenizerHandle tokenizer, void *pStrucPtr, ParseTable *pTPI, const char *pFieldName, char *pEString);
 
//does all FIXUPTYPE_HERE_IS_IGNORED_FIELD callbacks, if any (almost never are any)
void TokenizerDoSpecialIgnoreCallbacks(TokenizerHandle tokenizer, void *pStrucPtr);
#endif // STRUCTTOKENIZER_H