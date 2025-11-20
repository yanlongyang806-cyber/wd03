#include "structTokenizer.h"
#include "structInternals.h"
#include "estring.h"
#include "error.h"
#include "ScratchStack.h"
#include "StringUtil.h"
#include "StringCache.h"
#include "StashTable.h"
#include "timing.h"
#include "StructTokenizer_c_ast.h"
#include "TextParserUtils_h_ast.h"
#include "ThreadSafeMemoryPool.h"

#if PLATFORM_CONSOLE
bool gbFixedTokenizerBufferSize = true;
#else
bool gbFixedTokenizerBufferSize = false;
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc);); // Should be 0 after startup

// NOTE: in future, if more flexibility is required, we can
// separate INCLUDE handling to a layer above tokenizer.
// That would simplify the Tokenizer code.

char *TOK_SPECIAL_INT_RETURN = "__If you see this string, then someone is misusing StructTokenizer";


AUTO_STRUCT;
typedef struct SpecialIgnoredFields
{
	ParseTable *pTPI; NO_AST
	SpecialIgnoredField **ppFields;
} SpecialIgnoredFields;

typedef struct TokenizerContext
{
	FILE* handle;
	S64 curoffset;
	struct TokenizerContext* previous;
	int curline;
	int nextline;
	int lasterrorline;

	//report all errors as coming from this line num... presumably because a string was read in from a text file
	//and then unescaped or something, but we want to use its original file/line for error reporting
	int iOverrideLineNumForErrorReporting;
	
	
	char filename[CRYPTIC_MAX_PATH];
	union
	{
		const char* constString;
		char *noConstString;
	};

	unsigned int contextisstring:1;		// this context is a placeholder because we're processing a string
	unsigned int ownsstring:1; //should free the string when destroyed
} TokenizerContext;


typedef struct Tokenizer
{
	TokenizerContext* context;
	StaticDefine* staticdefines;		// simple defines with no scoping
	TextParserState *pTextParserState;
	
	char *pBuffer;
	char *pEscapedBuffer;
	S64 iBufferAllocedSize;

	char *pExtraDelimiters;


	S64 offsetinbuffer;
	S64 lengthofbuffer;
	S64 pastescape;					// offset past the escaped string
	S64 exoffset;						// offset for Ex processing

	unsigned int commanext:1;
	unsigned int eolnext:1;
	unsigned int quotenext:1;
	unsigned int commaaftertoken:1;
	unsigned int eolaftertoken:1;
	unsigned int quoteaftertoken:1;
	unsigned int instring:1;			// we just returned a string from Peek (don't tokenize on further calls)
	unsigned int inescape:1;
	unsigned int bNoErrorfs:1; //if true, generate no errorfs when encountering a parse error (but still return bad return values)
	unsigned int bParseCurrentFile:1; // if true, parse current file from file

	U32 iFlags;

	//all "unknown" ignored fields are counted when PARSER_IGNORE_ALL_UNKNOWN is set
	StashTable sIgnoredFields;

	//big optimization to avoid double-peeking every time... when you peek, stick what you find in here, then when you get
	//you don't have to peek again
	char *pPeekedToken;
	U32 iFlagsForPeekedToken;

	//when TokenizerPeek is called with PEEKFLAG_CHECKFORSTATICDEFINEINT, if it finds an int it
	//returns TOK_SPECIAL_INT_RETURN and sticks its int here
	int iIntFoundFromStaticDefine;

	StashTable sSpecialIgnoreStringsByStructPtr;
} Tokenizer;

TSMP_DEFINE(Tokenizer);
TSMP_DEFINE(TokenizerContext);

Tokenizer *callocTokenizer()
{
	ATOMIC_INIT_BEGIN;
	TSMP_SMART_CREATE(Tokenizer, 256, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ATOMIC_INIT_END;

	return TSMP_CALLOC(Tokenizer);
}

void safefreeTokenizer(Tokenizer **t)
{
	if(!t || !*t)
		return;

	TSMP_FREE(Tokenizer, *t);
	*t = NULL;
}

TokenizerContext *callocTokenizerContext()
{
	ATOMIC_INIT_BEGIN;
	TSMP_SMART_CREATE(TokenizerContext, 256, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ATOMIC_INIT_END;

	return TSMP_CALLOC(TokenizerContext);
}

void safefreeTokenizerContext(TokenizerContext **t)
{
	if(!t || !*t)
		return;

	TSMP_FREE(TokenizerContext, *t);
	*t = NULL;
}

// static strings as placeholders for newlines and commas
char g_comma[] = ",";
char g_eol[] = "\n";


const char UnicodeSig[2] = { 0xff, 0xfe };
const char UnicodeBESig[2] = { 0xfe, 0xff };
const char UTF8Sig[3] = { 0xef, 0xbb, 0xbf };


// open a file and sets up a context, returns NULL on failure
static TokenizerContext* OpenTokenFile(const char* filename, TextParserState *pTPS)
{
	char opensig[3];

	// try to open file
	TokenizerContext* ret = callocTokenizerContext();
	ret->previous = NULL;
	ret->handle = fileOpen(filename, "rb");
	ret->curoffset = 0;
	fileRelativePath(filename, ret->filename);
	ret->curline = 1;
	ret->nextline = 1;
	ret->lasterrorline = 0;
	ret->contextisstring = 0;


	if (!ret->handle)
	{
		verbose_printf("Tokenizer: couldn't open %s\n", filename);
		safefreeTokenizerContext(&ret);
		return NULL;
	}

	// test for a UTF-16 or UTF-32 file
	fread(opensig, sizeof(char), 3, ret->handle);
	if (!strncmp(opensig, UnicodeSig, 2) ||
		!strncmp(opensig, UnicodeBESig, 2))
	{
		ErrorFilenamef(filename, "Tokenizer: %s is a unicode file and cannot be opened\n", filename);
		fileClose(ret->handle);
		safefreeTokenizerContext(&ret);
		return NULL;
	}
	// we're fine with UTF-8
	if (!strncmp(opensig, UTF8Sig, 3))
	{
		//verbose_printf("Tokenizer: skipping UTF-8 header on %s\n", filename);
		ret->curoffset = 3;
	}

	// record every file tokenized
	if (pTPS->parselist)
	{
		if (pTPS->flags & PARSER_USE_CRCS)
			FileListInsertChecksum(&pTPS->parselist, filename, 0);
		else
			FileListInsert(&pTPS->parselist, filename, 0);
	}

	return ret;
}

const char* TokenizerGetFileName(TokenizerHandle tokenizer)
{
	Tokenizer* tok = (Tokenizer*)tokenizer;
	if (!tok || !tok->context) return NULL;
	return tok->context->filename;
}

int TokenizerGetCurLine(TokenizerHandle tokenizer)
{
	Tokenizer* tok = (Tokenizer*)tokenizer;
	if (!tok || !tok->context) return 0;
	return tok->context->curline;
}

void TokenizerSetLineNumForErrorReporting(TokenizerHandle tokenizer, int iLineNum)
{
	Tokenizer* tok = (Tokenizer*)tokenizer;
	if (!tok || !tok->context) return;
	tok->context->iOverrideLineNumForErrorReporting = iLineNum;
}


void TokenizerAppendFileAndLine(TokenizerHandle tokenizer, char **ppEString)
{
	Tokenizer* tok = (Tokenizer*)tokenizer;
	if (!tok || !tok->context) return;
	estrConcatf(ppEString, "%s, line %i", tok->context->filename, tok->context->iOverrideLineNumForErrorReporting ? tok->context->iOverrideLineNumForErrorReporting :  tok->context->curline);
}

// all eol tokens will be returned this way
int IsEol(char* token)
{
	// MAK - need to use actual pointer to distinguish the g_eol marker from a string containing just eol
	return token == g_eol;
	// return token[0] == '\n' && token[1] == 0;
}

// closes file and frees context, doesn't do anything to list
static void CloseTokenFile(TokenizerContext* context)
{
	fileClose(context->handle);
	safefreeTokenizerContext(&context);
}

// reload buffer with offset passed, and set buffer vars correctly
// lengthofbuffer set to zero on error or eof
static void LoadBuffer(Tokenizer* tok, S64 offset)
{
	tok->context->curoffset = offset;	// record where our buffer is

	if(tok->context->contextisstring)
	{
		S64 len = strlen(tok->context->constString);
		if(offset < len)
		{
			tok->lengthofbuffer = min(len-offset, tok->iBufferAllocedSize-1);
			strncpy_s(tok->pBuffer, tok->iBufferAllocedSize, tok->context->constString + offset, tok->lengthofbuffer);
		}
		else
		{
			tok->lengthofbuffer = 0;
			return;
		}
	}
	else
	{
		fseek(tok->context->handle, offset, SEEK_SET);
		tok->lengthofbuffer = (S64)fread(tok->pBuffer, sizeof(char), tok->iBufferAllocedSize, tok->context->handle);
	}

	if (tok->lengthofbuffer < tok->iBufferAllocedSize) tok->pBuffer[tok->lengthofbuffer] = 0;
	tok->offsetinbuffer = 0;
}

void TokenizerInitBuffers(Tokenizer *pTok, size_t iSizeNeeded)
{
	PERFINFO_AUTO_START_FUNC();
	if (gbFixedTokenizerBufferSize)
	{
		pTok->pBuffer = ScratchAllocUninitialized(TOKEN_BUFFER_LENGTH);
		pTok->pBuffer[0] = 0;
		pTok->pEscapedBuffer = ScratchAllocUninitialized(TOKEN_BUFFER_LENGTH);
		pTok->pEscapedBuffer[0] = 0;
		pTok->iBufferAllocedSize = TOKEN_BUFFER_LENGTH;
	}
	else
	{
#ifndef _M_X64
		assert(iSizeNeeded + MAX_STRING_LENGTH + 2 < 0x7fffffff);
#endif
		pTok->pBuffer = ScratchAllocUninitialized(iSizeNeeded + MAX_STRING_LENGTH + 2);
		pTok->pBuffer[0] = 0;
		pTok->pEscapedBuffer = ScratchAllocUninitialized(iSizeNeeded + MAX_STRING_LENGTH + 2);
		pTok->pEscapedBuffer[0] = 0;
		pTok->iBufferAllocedSize = iSizeNeeded + MAX_STRING_LENGTH + 2;
	}
	PERFINFO_AUTO_STOP_FUNC();
}


TokenizerHandle TokenizerCreateString(const char* string, const char *filename, int iFlags)
{
	Tokenizer* tok;

	TokenizerContext* ret;

	if(!string || !string[0])
	{
		if (!(iFlags & PARSER_NOERRORFSONPARSE))
			Errorf("Empty string passed into TokenizerCreateString()");
		return NULL;
	}

	tok = callocTokenizer();

	ret = callocTokenizerContext();
	ret->filename[0] = 0;
	if (filename)
	{
		strcpy(ret->filename, filename);
		if (g_ccase_string_cache)
			StringToCCase(ret->filename);
	}
	ret->previous = NULL;
	ret->curoffset = 0;
	ret->curline = 1;
	ret->nextline = 1;
	ret->lasterrorline = 0;
	ret->constString = string;
	ret->contextisstring = 1;

	tok->context = ret;

	tok->staticdefines = 0;
	tok->instring = 0;

	tok->iFlags = iFlags;

	tok->bNoErrorfs = (iFlags & PARSER_NOERRORFSONPARSE) ? 1 : 0;
	tok->bParseCurrentFile = (iFlags & PARSER_PARSECURRENTFILE) ? 1 : 0;

	tok->commanext = tok->eolnext = tok->commaaftertoken = tok->eolaftertoken = 0;

	TokenizerInitBuffers(tok, strlen(string) + 1);




	LoadBuffer(tok, tok->context->curoffset);
/*
	tok->lengthofbuffer = min(TOKEN_BUFFER_LENGTH, strlen(string));
	strncpy(tok->pBuffer, string, tok->lengthofbuffer);
	if (tok->lengthofbuffer < TOKEN_BUFFER_LENGTH) tok->pBuffer[tok->lengthofbuffer] = 0;
*/

	return (TokenizerHandle)tok;
}

// external TokenizerCreate function, returns a handle or NULL on failure
TokenizerHandle TokenizerCreateEx(const char* filename, TextParserState *pTextParserState, int ignore_empty)
{
	Tokenizer* tok = callocTokenizer();

	tok->context = OpenTokenFile(filename, pTextParserState);
	if (!tok->context)
	{
		return NULL;
	}

	tok->staticdefines = 0;
	tok->instring = 0;

	TokenizerInitBuffers(tok, fileSize(filename) + 1);

	tok->bNoErrorfs = pTextParserState->lf_noErrorfsOnRead;

	tok->pTextParserState = pTextParserState;

	tok->commanext = tok->eolnext = tok->quotenext =
		tok->commaaftertoken = tok->eolaftertoken = tok->quoteaftertoken = 0;
	LoadBuffer(tok, tok->context->curoffset);
	if (tok->lengthofbuffer == 0)
	{
		CloseTokenFile(tok->context);
		ScratchFree(tok->pBuffer);
		ScratchFree(tok->pEscapedBuffer);
		tok->pBuffer = NULL;
		tok->pEscapedBuffer = NULL;
		safefreeTokenizer(&tok);
		if (!ignore_empty)
			ErrorFilenamef(filename, "Tokenizer: %s is empty\n", filename);
	}
	
	return (TokenizerHandle)tok;
}


TokenizerHandle TokenizerCreateLoadedFile(char *pBuffer, const char *filename, TextParserState *pTextParserState, int ignore_empty)
{
	Tokenizer* tok;

	TokenizerContext* ret;
	int iStartingOffset = 0;

	if(!pBuffer || !pBuffer[0])
	{
		if (!ignore_empty)
			Errorf("Empty string passed into TokenizerCreateString()");
		return NULL;
	}

	if (!strncmp(pBuffer, UnicodeSig, 2) ||
		!strncmp(pBuffer, UnicodeBESig, 2))
	{
		Errorf("Tokenizer: %s is a unicode file and cannot be opened in RAM\n", filename);
		return NULL;
	}

	// we're fine with UTF-8
	if (!strncmp(pBuffer, UTF8Sig, 3))
	{
		//verbose_printf("Tokenizer: skipping UTF-8 header on %s\n", filename);
		iStartingOffset = 3;
	}

	tok = callocTokenizer();

	TokenizerInitBuffers(tok, strlen(pBuffer) + 1);

	ret = callocTokenizerContext();
	strcpy(ret->filename, filename);
	ret->previous = NULL;
	ret->curoffset = iStartingOffset;
	ret->curline = 1;
	ret->nextline = 1;
	ret->lasterrorline = 0;
	ret->constString = pBuffer;
	ret->ownsstring = 1;
	ret->contextisstring = 1;

	tok->context = ret;

	tok->staticdefines = 0;
	tok->instring = 0;
	tok->pTextParserState = pTextParserState;

	tok->commanext = tok->eolnext = tok->commaaftertoken = tok->eolaftertoken = 0;
	LoadBuffer(tok, tok->context->curoffset);
/*
	tok->lengthofbuffer = min(TOKEN_BUFFER_LENGTH, strlen(string));
	strncpy(tok->pBuffer, string, tok->lengthofbuffer);
	if (tok->lengthofbuffer < TOKEN_BUFFER_LENGTH) tok->pBuffer[tok->lengthofbuffer] = 0;
*/

	return (TokenizerHandle)tok;
}


void TokenizerReportIgnoredFields(Tokenizer *tok)
{
	char *pErrorString = NULL;
	StashTableIterator stashIter;
	StashElement element;

	stashGetIterator(tok->sIgnoredFields, &stashIter);

	while (stashGetNextElement(&stashIter, &element))
	{
		char *pName = stashElementGetStringKey(element);
		int iCount = stashElementGetInt(element);

		estrConcatf(&pErrorString, "%d occurrence%s of %s. ", iCount, iCount > 1 ? "s" : "", pName);
	}

	TokenizerErrorf(tok, "While ignoring all unknown fields, ignored the following: %s", pErrorString);
	
	estrDestroy(&pErrorString);
}

// external TokenizerDestroy function, closes any open files and frees mem
void TokenizerDestroy(TokenizerHandle tokenizer)
{
	TokenizerContext* topcontext;
	Tokenizer* tok = (Tokenizer*)tokenizer;

	PERFINFO_AUTO_START_FUNC();

	if (tok == NULL)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	if (tok->sIgnoredFields)
	{
		TokenizerReportIgnoredFields(tok);
		stashTableDestroy(tok->sIgnoredFields);
	}

	PERFINFO_AUTO_START("Freeing buffers", 1);
	ScratchFree(tok->pBuffer);
	ScratchFree(tok->pEscapedBuffer);
	PERFINFO_AUTO_STOP();

	// destroy each context and close files
	for (topcontext = tok->context; topcontext != NULL; topcontext = tok->context)
	{
		if (topcontext->ownsstring)
		{
			free(topcontext->noConstString);
			topcontext->ownsstring = 0;
		}

		tok->context = topcontext->previous;
		if(!topcontext->contextisstring)
			CloseTokenFile(topcontext);
		else
			safefreeTokenizerContext(&topcontext);
	}

	stashTableDestroyStructSafe(&tok->sSpecialIgnoreStringsByStructPtr, NULL, parse_SpecialIgnoredFields);

	safefreeTokenizer(&tok);
	PERFINFO_AUTO_STOP_FUNC();
}

// Escapes the given string and put it in target
int TokenizerEscapeLen(char * target,const char* source, int len)
{
	char *start = target;
	const char *sourceEnd = NULL;

	if (len) sourceEnd = source+len;

	*target++ = '<';
	*target++ = '&';
	while (*source && source != sourceEnd)
	{
		if (*source == '\\')
		{
			*target++ = '\\';
			*target++ = '\\';
		}
		else if (*source == '>')
		{
			*target++ = '\\';
			*target++ = '>';
		}
		else if (*source == '&')
		{
			*target++ = '\\';
			*target++ = '&';
		}
		else if (*source == '\n')
		{
			*target++ = '\\';
			*target++ = 'n';
		}
		else if (*source == '\r')
		{
			*target++ = '\\';
			*target++ = 'r';
		}
		else
			*target++ = *source;
		source++;
	}
	*target++ = '&';
	*target++ = '>';
	*target = 0;

	return target - start; //returns new size
}


// Unescapes the string, ignoring initial and ending tags if necessary
int TokenizerUnescape(char* target, const char* source)
{
	char *start = target;
	while (*source)
	{
		if (*source == '<' && *(source+1) == '&')
		{
			source++; source++;
			continue;
		}
		else if (*source == '&' && *(source+1) == '>')
		{
			source++; source++;
			break;
		}		
		else if (*source == '\\') 
		{
			source++;
			if (*source == 'n')
			{
				*target++ = '\n';
				source++;
			}
			else if (*source == 'r')
			{
				*target++ = '\r';
				source++;
			}
			else
				*target++ = *source++;
		}
		else
			*target++ = *source++;
	}
	*target = 0;
	return target - start; //returns new size
}


TextParserState *TokenizerGetTextParserState(TokenizerHandle tokenizer)
{
	Tokenizer* tok = (Tokenizer*)tokenizer;

	if (tok)
	{
		return tok->pTextParserState;
	}

	return NULL;
}


int TokenizerGetReadInt(TokenizerHandle tokenizer)
{
	Tokenizer* tok = (Tokenizer*)tokenizer;
	return tok->iIntFoundFromStaticDefine;
}

#define PEEK_RETURN(ret) { if (tok) {tok->pPeekedToken  = ret; tok->iFlagsForPeekedToken = iFlags;} return ret; }

// external TokenizerPeek function, advances to next token and returns it,
// returns NULL at end of file
char* TokenizerPeek(TokenizerHandle tokenizer, U32 iFlags, TextParserResult *parseResult)
{
	S64 i;
	Tokenizer* tok = (Tokenizer*)tokenizer;
	char filename[CRYPTIC_MAX_PATH];
	TokenizerContext* savecontext;
	char* ret;
	const char* define;

	char delims[256] = " ,\n\t\r";


	if (tok == NULL) PEEK_RETURN(NULL);

	if (tok->pExtraDelimiters)
	{
		strcat(delims, tok->pExtraDelimiters);
	}

	// short circuit if we're currently in a string
	if (tok->instring)
	{
		if (!(iFlags & PEEKFLAG_IGNORECOMMA) && tok->commanext) PEEK_RETURN(g_comma);
		if (!(iFlags & PEEKFLAG_IGNORELINEBREAK) && tok->eolnext) PEEK_RETURN(g_eol);
		PEEK_RETURN(tok->pBuffer + tok->offsetinbuffer);
	}
	if (tok->inescape)
	{
		if (!(iFlags & PEEKFLAG_IGNORECOMMA) && tok->commanext) PEEK_RETURN(g_comma);
		if (!(iFlags & PEEKFLAG_IGNORELINEBREAK) && tok->eolnext) PEEK_RETURN(g_eol);
		PEEK_RETURN(tok->pEscapedBuffer);
	}

	// main loop to look for tokens, we go back to here after comments, etc.
	while (1)
	{
		// advance buffer if needed
		if (tok->offsetinbuffer >= tok->lengthofbuffer ||
			tok->offsetinbuffer > tok->iBufferAllocedSize - MAX_TOKEN_LENGTH - 2)
		{
			LoadBuffer(tok, tok->context->curoffset + tok->offsetinbuffer);
		}

		// check and see if we've run out of space
		while (tok->lengthofbuffer == 0)
		{
			// try and pop context
			if (tok->context->previous)
			{
				savecontext = tok->context->previous;
				CloseTokenFile(tok->context);
				tok->context = savecontext;
				LoadBuffer(tok, tok->context->curoffset);
				tok->eolnext = 1;	// make sure to set eol between contexts
			}
			else break;
		}
		if (tok->lengthofbuffer == 0) PEEK_RETURN(NULL); // if no more contexts, PEEK_RETURN null

		// advance past white space
		while (strchr_fast(delims, tok->pBuffer[tok->offsetinbuffer]))
		{
			if (tok->pBuffer[tok->offsetinbuffer] == ',') tok->commanext = 1;
			if (tok->pBuffer[tok->offsetinbuffer] == '\n') { tok->eolnext = 1; tok->context->nextline++; }
			tok->offsetinbuffer++;

			// advance buffer if we get close to end
			if (tok->offsetinbuffer >= tok->lengthofbuffer ||
				tok->offsetinbuffer > tok->iBufferAllocedSize - MAX_TOKEN_LENGTH - 2)
			{
				LoadBuffer(tok, tok->context->curoffset + tok->offsetinbuffer);
				if (tok->lengthofbuffer == 0)
				{
					break; // ran out of text, need to continue and load new context
				}
			}
		}
		if (tok->lengthofbuffer == 0) continue; // try and load new context

		// take care of string
		if (tok->pBuffer[tok->offsetinbuffer] == '"' || tok->quotenext)
		{
			// advance buffer if we are too close to end for a string
			if (tok->offsetinbuffer > tok->iBufferAllocedSize - MAX_STRING_LENGTH - 2)
			{
				LoadBuffer(tok, tok->context->curoffset + tok->offsetinbuffer);
			}

			// advance real quick looking for a terminator or eol
			i = tok->offsetinbuffer;
			if (!tok->quotenext) i++; // past open string marker
			while (i < tok->lengthofbuffer && tok->pBuffer[i] != '"' && tok->pBuffer[i] != '\n') i++;
			if (i >= tok->lengthofbuffer || tok->pBuffer[i] == '\n')
			{
				TokenizerErrorf(tok, "No end of string found");
				if (parseResult)
				{
					SET_ERROR_RESULT(*parseResult);
				}
				tok->offsetinbuffer = i;
				LoadBuffer(tok, tok->context->curoffset + tok->offsetinbuffer);
				continue; // start over again and try to recover
			}

			// now we found the end string marker
			tok->pBuffer[i] = '\0';
			if (!tok->quotenext) tok->offsetinbuffer++; // get past open string marker
			tok->instring = 1;
			tok->quotenext = 0;
			if (!(iFlags & PEEKFLAG_IGNORECOMMA) && tok->commanext) PEEK_RETURN(g_comma);
			if (!(iFlags & PEEKFLAG_IGNORELINEBREAK) && tok->eolnext) PEEK_RETURN(g_eol);
			PEEK_RETURN(tok->pBuffer + tok->offsetinbuffer);	// PEEK_RETURN complete string, no defines processing
		}

		// take care of multi-line strings & escaped strings
		if ((tok->pBuffer[tok->offsetinbuffer] == '<' && tok->pBuffer[tok->offsetinbuffer+1] == '<') ||
			(tok->pBuffer[tok->offsetinbuffer] == '<' && tok->pBuffer[tok->offsetinbuffer+1] == '&'))
		{
			int escapedstring = tok->pBuffer[tok->offsetinbuffer+1] == '&';

			// advance buffer if we are too close to end for a string
			if (tok->offsetinbuffer > tok->iBufferAllocedSize - MAX_STRING_LENGTH - 4)
			{
				LoadBuffer(tok, tok->context->curoffset + tok->offsetinbuffer);
			}

			// advance real quick looking for the string terminator
			i = tok->offsetinbuffer+2;
			while (i < tok->lengthofbuffer-1)
			{
				if (!escapedstring && tok->pBuffer[i] == '>' && tok->pBuffer[i+1] == '>') break;
				if (escapedstring && tok->pBuffer[i] == '&' && tok->pBuffer[i+1] == '>') break;
				if (tok->pBuffer[i] == '\n') { tok->context->nextline++; }
				i++;
			}
			if (i >= tok->lengthofbuffer-1)
			{
				TokenizerErrorf(tok, "No end of multi-line string string found");
				if (parseResult) 
				{
					SET_ERROR_RESULT(*parseResult);
				}
				tok->offsetinbuffer = i+1;
				LoadBuffer(tok, tok->context->curoffset + tok->offsetinbuffer);
				continue; // start over again and try to recover (after eating to EOF)
			}

			// now we found the end string marker
			tok->pBuffer[i] = '\0';
			tok->pBuffer[i+1] = '\0';
			tok->offsetinbuffer += 2; // get past open string marker

			if (escapedstring)
			{
				tok->pastescape = i+2;
				tok->inescape = 1;
				TokenizerUnescape(tok->pEscapedBuffer, tok->pBuffer + tok->offsetinbuffer);
				if (!(iFlags & PEEKFLAG_IGNORECOMMA) && tok->commanext) PEEK_RETURN(g_comma);
				if (!(iFlags & PEEKFLAG_IGNORELINEBREAK) && tok->eolnext) PEEK_RETURN(g_eol);
				PEEK_RETURN(tok->pEscapedBuffer);
			}
			else
			{
				// skip past initial EOL if found
				if (tok->pBuffer[tok->offsetinbuffer] == '\n') tok->offsetinbuffer++;
				else if (tok->pBuffer[tok->offsetinbuffer] == '\r' && tok->pBuffer[tok->offsetinbuffer + 1] == '\n') tok->offsetinbuffer += 2;
				tok->instring = 1;
				if (!(iFlags & PEEKFLAG_IGNORECOMMA) && tok->commanext) PEEK_RETURN(g_comma);
				if (!(iFlags & PEEKFLAG_IGNORELINEBREAK) && tok->eolnext) PEEK_RETURN(g_eol);
				PEEK_RETURN(tok->pBuffer + tok->offsetinbuffer);	// PEEK_RETURN complete string, no defines processing
			}
		}

		// take care of comment
		else if (tok->pBuffer[tok->offsetinbuffer] == '#' ||
			(tok->pBuffer[tok->offsetinbuffer] == '/' && tok->pBuffer[tok->offsetinbuffer+1] == '/'))
		{
			// advance buffer if we are too close to end to get complete comment in buffer
			if (tok->offsetinbuffer > tok->iBufferAllocedSize - MAX_STRING_LENGTH - 2)
			{
				LoadBuffer(tok, tok->context->curoffset + tok->offsetinbuffer);
			}

			// advance real quick looking for an eol
			i = tok->offsetinbuffer+2;
			while (i < tok->lengthofbuffer && tok->pBuffer[i] != '\n') i++;
			i++; // past eol
			if (i >= tok->lengthofbuffer) LoadBuffer(tok, tok->context->curoffset + i);
			else tok->offsetinbuffer = i;

			// now we're past eol, just start over
			tok->eolnext = 1;
			tok->context->nextline++;
		}

		// take care of INCLUDE
		else if ((tok->eolnext || (tok->offsetinbuffer == 0 && tok->context->curline == 1))  // at beginning of line
			&& (_strnicmp(tok->pBuffer+tok->offsetinbuffer, "include ", 8) == 0) && !(tok->iFlags & PARSER_NOINCLUDES)) // found "include "
		{
			// advance to the filename
			tok->offsetinbuffer += 8;
			while (tok->offsetinbuffer < tok->lengthofbuffer && strchr_fast(" \t", tok->pBuffer[tok->offsetinbuffer]))
				tok->offsetinbuffer++;
			// ok to fall through end of buffer for a minute

			// look for end of filename
			i = tok->offsetinbuffer + 1;
			while (i < tok->lengthofbuffer && !strchr_fast(delims, tok->pBuffer[i])) i++;
			if (i >= tok->iBufferAllocedSize)
			{
				// actually hit end of buffer.  this is an error, as we should have
				// had plenty of room for filename (MAX_TOKEN_LENGTH > CRYPTIC_MAX_PATH)
				TokenizerErrorf(tok, "Error reading include filename in %s, line %d",
					TokenizerGetFileName(tok), tok->context->nextline);
				if (parseResult)
				{
					SET_ERROR_RESULT(*parseResult);
				}
				LoadBuffer(tok, tok->context->curoffset + tok->offsetinbuffer);
				continue; // start over

			}
			// otherwise, we hit the end of the file
			if (i >= tok->lengthofbuffer)
			{
				tok->pBuffer[i] = 0;	// chop the end of the buffer
			}
			if (tok->pBuffer[i] == '\n') tok->context->nextline++;
			tok->pBuffer[i] = 0; // chop filename
			strcpy(filename, tok->pBuffer + tok->offsetinbuffer); // copy it out
			tok->offsetinbuffer = i+1; // reset stream

			// open new file
			tok->context->curoffset += tok->offsetinbuffer;
			forwardSlashes(filename); // required for prefix match
			savecontext = tok->context;
			tok->context = OpenTokenFile(filename, tok->pTextParserState);
			if (!tok->context)
			{
				tok->context = savecontext;
				TokenizerErrorf(tok,"Error opening include file %s referenced in %s, line %i\n",
					filename, TokenizerGetFileName(tok), tok->context->nextline);
				if (parseResult)
				{
					SET_ERROR_RESULT(*parseResult);
				}
				continue; // fall through and continue
			}
			else
			{
				// push context
				tok->context->previous = savecontext;
				LoadBuffer(tok, tok->context->curoffset);
				tok->eolnext = 1;
				continue; // fall through and continue
			}
		}

		// otherwise, chop off a regular token
		else
		{
			// look for end of token
			i = tok->offsetinbuffer;
			while (i < tok->lengthofbuffer && !strchr_fast(delims, tok->pBuffer[i]) && tok->pBuffer[i] != '"') i++;
			if (i >= tok->lengthofbuffer)
			{
				if (i == tok->lengthofbuffer && tok->lengthofbuffer < tok->iBufferAllocedSize)
				{
					// just hit end of file, it's ok..
				}
				else
				{
					TokenizerErrorf(tok,"Token too long");
					if (parseResult)
					{
						SET_ERROR_RESULT(*parseResult);
					}
					LoadBuffer(tok, tok->context->curoffset+i);
					continue; // start again
				}
			}

			// check for important whitespace after token
			if (tok->pBuffer[i] == ',') tok->commaaftertoken = 1;
			else if (tok->pBuffer[i] == '\n') { tok->eolaftertoken = 1; tok->context->nextline++; }
			else if (tok->pBuffer[i] == '"') tok->quoteaftertoken = 1;

			// chop off token and PEEK_RETURN
			tok->pBuffer[i] = 0;
			if (!(iFlags & PEEKFLAG_IGNORECOMMA) && tok->commanext) PEEK_RETURN(g_comma);
			if (!(iFlags & PEEKFLAG_IGNORELINEBREAK) && tok->eolnext) PEEK_RETURN(g_eol);
			ret = tok->pBuffer + tok->offsetinbuffer;
			break; // do define processing
		}
	} // forever

	// have token in <ret>, process through defines
	if (tok->staticdefines)
	{
		if (iFlags & PEEKFLAG_CHECKFORSTATICDEFINEINT)
		{
			int iFound = StaticDefine_FastStringToInt(tok->staticdefines, ret, INT_MIN);
			if (iFound != INT_MIN)
			{
				tok->iIntFoundFromStaticDefine = iFound;
				PEEK_RETURN(TOK_SPECIAL_INT_RETURN);
			}
		}
		else
		{
			define = StaticDefineLookup(tok->staticdefines, ret);
			if (define) PEEK_RETURN((char *)define);
		}
	}
	PEEK_RETURN(ret);
}

// external TokenizerGet function, as PeekToken but eats token
char* TokenizerGet(TokenizerHandle tokenizer, U32 iFlags, TextParserResult *parseResult)
{
	Tokenizer* tok = (Tokenizer*)tokenizer;
	char* ret;

	if (tok->pPeekedToken && tok->iFlagsForPeekedToken == iFlags)
	{
		ret = tok->pPeekedToken;
	}
	else
	{
		ret = TokenizerPeek(tokenizer, iFlags, parseResult);
	}

	tok->pPeekedToken = NULL;

	if (ret == NULL) 
	{
		return NULL; // should grab case where tok == NULL
	}

	if (tok->eolnext) tok->context->curline = tok->context->nextline;
	if (!(iFlags & PEEKFLAG_IGNORECOMMA) && tok->commanext) { tok->commanext = 0; return ret; }
	if (!(iFlags & PEEKFLAG_IGNORELINEBREAK) && tok->eolnext) { tok->eolnext = 0; return ret; }
	tok->commanext = 0;
	tok->eolnext = 0;

	// eat token
	if (tok->inescape)
		tok->offsetinbuffer = tok->pastescape;
	else
	{
		while (*(tok->pBuffer + tok->offsetinbuffer)) tok->offsetinbuffer++; // find NULL
		tok->offsetinbuffer++;
	}
	tok->inescape = 0;
	tok->instring = 0;
	tok->exoffset = 0;

	// promote flags
	if (tok->commaaftertoken) { tok->commaaftertoken = 0; tok->commanext = 1; }
	if (tok->eolaftertoken) { tok->eolaftertoken = 0; tok->eolnext = 1; }
	if (tok->quoteaftertoken) { tok->quoteaftertoken = 0; tok->quotenext = 1; }

	return ret;
}

char* TokenizerPeekEx(TokenizerHandle tokenizer, U32 iFlags, char* seps, int* length, TextParserResult *parseResult)
{
	Tokenizer* tok = (Tokenizer*)tokenizer;
	char *end, *peek = TokenizerPeek(tokenizer, iFlags, parseResult);
	int len;
	if (peek == NULL) return NULL;

	// sanity check ourselves first..
	len = (int)strlen(peek);
	if (len <= tok->exoffset)
	{
		SET_INVALID_RESULT(*parseResult);
		Errorf("Internal tokenizer error");
		return peek;
	}

	// inside strings we don't look for separators
	if (tok->instring || tok->inescape)
	{
		*length = len;
		return peek;
	}

	// exoffset should point to our current subtoken, figure out correct length of it
	peek += tok->exoffset;
	if (strchr_fast(seps, *peek)) // pointing to seperator
	{
		*length = 1; // all seperators are considered separate subtokens
	}
	else // pointing to non-seperator, get whole subtoken
	{
		end = peek+1;
		while (*end && !strchr_fast(seps, *end)) end++;
		*length = end - peek;
	}
	return peek;
}

char* TokenizerGetEx(TokenizerHandle tokenizer, U32 iFlags, char* seps, int* length, TextParserResult *parseResult)
{
	Tokenizer* tok = (Tokenizer*)tokenizer;
	char *peek = TokenizerPeekEx(tokenizer, iFlags, seps, length, parseResult);
	int len;

	// eat subtoken if possible, otherwise degenerate to eating the rest of the token
	len = (int)strlen(peek);
	if (*length < len)
	{
		tok->exoffset += *length;
	}
	else
	{
		TokenizerGet(tokenizer, iFlags, parseResult);
	}
	return peek;
}


bool TokenizerShouldParseCurrentFile(TokenizerHandle tokenizer)
{
	Tokenizer* tok = (Tokenizer*)tokenizer;
	if (!tok) return false;
	return tok->bParseCurrentFile;
}





void TokenizerSetStaticDefines(TokenizerHandle tokenizer, StaticDefine* list)
{
	Tokenizer* tok = (Tokenizer*)tokenizer;
	if (!tok) return;
	tok->staticdefines = list;
}


void TokenizerSetExtraDelimiters(TokenizerHandle tokenizer, char *pDelims)
{
	Tokenizer *tok = (Tokenizer*)tokenizer;
	if (!tok)
	{
		return;
	}

	tok->pExtraDelimiters = pDelims;
}


void TokenizerErrorfInternal(TokenizerHandle tokenizer, const char *file, int line, const char *format, ...)
{
	Tokenizer* tok = (Tokenizer*)tokenizer;

	if (tok)
	{	
		char *errorString = NULL;
		if (tok->bNoErrorfs)
		{
			return;
		}

		if (tok->context->lasterrorline == tok->context->curline)
		{
			// report one per line
			return;
		}

		tok->context->lasterrorline = tok->context->curline;

		estrStackCreate(&errorString);
	
		estrPrintf(&errorString,"Parser error in ");
		TokenizerAppendFileAndLine(tokenizer, &errorString);
		estrConcatf(&errorString, ": ");

		VA_START(args, format);
		estrConcatfv(&errorString, format, args);
		VA_END();

		ErrorFilenamefInternal(file, line, TokenizerGetFileName(tok),"%s",errorString);
		estrDestroy(&errorString);
	}
	else
	{
		VA_START(args, format);
		ErrorvInternal(true, file, line, format,args);
		VA_END();
	}

}

void TokenizerSetFlags(TokenizerHandle tokenizer, U32 iFlags)
{
	Tokenizer* tok = (Tokenizer*)tokenizer;

	tok->iFlags = iFlags;
}
U32 TokenizerGetFlags(TokenizerHandle tokenizer)
{
	Tokenizer* tok = (Tokenizer*)tokenizer;

	return tok->iFlags;
}

void TokenizerReportUnknown(TokenizerHandle tokenizer, char *pToken)
{
	Tokenizer* tok = (Tokenizer*)tokenizer;
	int iCurCount;
	
	if (tok->iFlags & PARSER_NOERROR_ONIGNORE)
		return;

	if (!tok->sIgnoredFields)
	{
		tok->sIgnoredFields = stashTableCreateWithStringKeys(2, StashDeepCopyKeys_NeverRelease);
	}

	if (stashFindInt(tok->sIgnoredFields, pToken, &iCurCount))
	{
		iCurCount++;
		stashAddInt(tok->sIgnoredFields, pToken, iCurCount, true);
	}
	else
	{
		stashAddInt(tok->sIgnoredFields, pToken, 1, true);
	}
}

//internally reports a string that will later be sent to a FIXUPTYPE_HERE_IS_IGNORED_FIELD
void TokenizerSetSpecialIgnoreString(TokenizerHandle tokenizer, void *pStructPtr, ParseTable *pTPI, const char *pFieldName, char *pEString)
{
	Tokenizer* tok = (Tokenizer*)tokenizer;
	SpecialIgnoredFields *pIgnoreFields;
	SpecialIgnoredField *pField = StructCreate(parse_SpecialIgnoredField);

	if (!tok->sSpecialIgnoreStringsByStructPtr)
	{
		tok->sSpecialIgnoreStringsByStructPtr = stashTableCreateAddress(16);
	}

	if (!stashFindPointer(tok->sSpecialIgnoreStringsByStructPtr, pStructPtr, &pIgnoreFields))
	{
		pIgnoreFields = StructCreate(parse_SpecialIgnoredFields);
		pIgnoreFields->pTPI = pTPI;
		stashAddPointer(tok->sSpecialIgnoreStringsByStructPtr, pStructPtr, pIgnoreFields, true);
	}
	else
	{
		assert(pIgnoreFields->pTPI == pTPI);
	}

	pField->pFieldName = pFieldName;
	pField->pString = pEString;

	eaPush(&pIgnoreFields->ppFields, pField);
}
	

 
//does all FIXUPTYPE_HERE_IS_IGNORED_FIELD callbacks, if any (almost never are any)
void TokenizerDoSpecialIgnoreCallbacks(TokenizerHandle tokenizer, void *pStructPtr)
{
	Tokenizer* tok = (Tokenizer*)tokenizer;
	SpecialIgnoredFields *pIgnoreFields;


	if (tok->sSpecialIgnoreStringsByStructPtr && stashRemovePointer(tok->sSpecialIgnoreStringsByStructPtr, pStructPtr, &pIgnoreFields))
	{

		TextParserAutoFixupCB *pFixupCB = ParserGetTableFixupFunc(pIgnoreFields->pTPI);

		FOR_EACH_IN_EARRAY_FORWARDS(pIgnoreFields->ppFields, SpecialIgnoredField, pField)
		{
			pFixupCB(pStructPtr, FIXUPTYPE_HERE_IS_IGNORED_FIELD, pField);
		}
		FOR_EACH_END;

		StructDestroy(parse_SpecialIgnoredFields, pIgnoreFields);
	}

}




#include "StructTokenizer_c_ast.c"

