// textparsercallbacks.c - provides text and token processing functions

#include <stdio.h>
#include <string.h>

#include "net/net.h"
#include "network/crypt.h"

#include "Estring.h"
#include "objPath.h"
#include "quat.h"
#include "rand.h"
#include "referencesystem.h"
#include "ScratchStack.h"
#include "SharedMemory.h"
#include "serialize.h"
#include "sock.h"
#include "StringCache.h"
#include "structInternals.h"
#include "structNet.h"
#include "structPack.h"
#include "timing.h"
#include "tokenstore.h"
// #define BITSTREAM_DEBUG
#include "BitStream.h"
#include "UnitSpec.h"
#include "Color.h"
#include "FileUtil.h"
#include "StringUtil.h"
#include "ResourceManager.h"
#include "tokenstore_inline.h"
#include "textparserUtils_inline.h"
#include "earray_inline.h"
#include "structInternals_h_ast.h"
#include "ResourceSystem_Internal.h"
#include "fastAtoi.h"
#include "TextParserCallbacks.h"

//so we can check error_occurred without function call overhead
#include "netprivate.h"

#include "textParserCallbacks_inline.h"

#define TEMP_ESTRING_CHARGE "will_destroy_immediately", __LINE__
AUTO_RUN_ANON(memBudgetAddMapping("will_destroy_immediately", BUDGET_EngineMisc););
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););


int g_MVDEBUG = 0;

//Prototypes for TextParser Use
const char*		MultiValTypeToString(MultiValType t);			// Returns 4 byte (3 char + NULL) representation of type
MultiValType	MultiValTypeFromString(const char* str);		// Returns type from 4 byte (3 char + NULL) representation


#define TPC_MEMDEBUG_PARAMS_STRUCT , pStructName ? pStructName : __FILE__, pStructName ? LINENUM_FOR_STRUCTS : __LINE__
#define TPC_MEMDEBUG_PARAMS_STRING , pStructName ? pStructName : __FILE__, pStructName ? LINENUM_FOR_STRINGS: __LINE__
#define TPC_MEMDEBUG_PARAMS_EARRAY , pStructName ? pStructName : __FILE__, pStructName ? LINENUM_FOR_EARRAYS: __LINE__


extern bool g_writeSizeReport;
extern char *g_sizeReportString;

bool gbDontAssertOnSpacesInEnums = false;
AUTO_CMD_INT(gbDontAssertOnSpacesInEnums, DontAssertOnSpacesInEnums) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;

bool gbIgnoreNonNullRefs = false;
bool gbLateCreateIndexedEArrays = false;
void SetGloballyIgnoreNonNullRefs(bool bSet)
{
	gbIgnoreNonNullRefs = bSet;
}

void SetLateCreateIndexedEArrays(bool bSet)
{
	gbLateCreateIndexedEArrays = bSet;
}


//textparser can only access the reference system from a single thread, which starts out as the main thread
//in which autoruns are called
U32 gTextParserRefSystemThread = 0;
AUTO_RUN;
void InitTextParserRefSystemThread(void)
{
	gTextParserRefSystemThread = GetCurrentThreadId();
}

// moved from structNet.c because they're only used here anyway and I want to inline them
typedef struct FloatAccuracyTable {
	FloatAccuracy fa;
	float unpackmult;
	float packmult;
	int	minbits;
} FloatAccuracyTable;

// These must be in the same order as the enum for quick lookup
FloatAccuracyTable floattable[] = {
	{0, 1, 1, 32}, // NO PACKING
	{FLOAT_HUNDREDTHS, 0.01, 100, 8},
	{FLOAT_TENTHS, 0.1, 10, 6},
	{FLOAT_ONES, 1, 1, 7},
	{FLOAT_FIVES, 5, 0.2, 5},
	{FLOAT_TENS, 10, 0.1, 4},
};

float ParserUnpackFloat(int i, FloatAccuracy fa)
{
	devassert(fa);
	devassert(fa < _countof(floattable));
	return (float)i*floattable[fa].unpackmult;
}

int ParserPackFloat(float f, FloatAccuracy fa)
{
	int ret = (int)(f*floattable[fa].packmult + 0.5);
	devassert(fa);
	if (ret == 0 && f>0.0) // Because this is used to send client HP, we don't *ever* want to send a 0 unless it's actually a 0 floating point value
		ret = 1;
	return ret;
}

int ParserFloatMinBits(FloatAccuracy fa) {
	devassert(fa);
	devassert(fa < _countof(floattable));
	return floattable[fa].minbits;
}

void packDelta(Packet *pak, int delta)
{
	pktSendBits(pak, 1, delta!=0);
	if (delta==0)
		return;
	pktSendBits(pak, 1, delta > 0);
	if (delta < 0) {
		pktSendBitsPack(pak, 1, -delta);
	} else {
		pktSendBitsPack(pak, 1, delta);
	}
}

int unpackDelta(Packet *pak)
{
	if (!pktGetBits(pak, 1))
		return 0;
	if (pktGetBits(pak, 1)) {
		// positive
		return pktGetBitsPack(pak, 1);
	} else {
		return -(int)pktGetBitsPack(pak, 1);
	}
}



///////////////////////////////////////////////////////////////////////////////////////// Parser

// clear any remaining tokens on a single line
// don't eat the eol
void ClearParameters(TokenizerHandle tok)
{
	char* nexttoken;
	while (1)
	{
		nexttoken = TokenizerPeek(tok, PEEKFLAG_IGNORECOMMA, NULL);
		if (!nexttoken || IsEol(nexttoken)) return;
		TokenizerGet(tok, PEEKFLAG_IGNORECOMMA, NULL);	// eat the token
	}
}

// report error if we don't have another parameter
char* GetNextParameter(TokenizerHandle tok, TextParserResult *parseResult)
{
	char *resultString = TokenizerPeek(tok, PEEKFLAG_IGNORECOMMA, parseResult);
	if (!resultString || IsEol(resultString))
	{
		int line = TokenizerGetCurLine(tok);
		TokenizerErrorf(tok,"Missing parameter value");
		SET_ERROR_RESULT(*parseResult);
		return NULL;
	}
	else
	{
		TokenizerGet(tok, PEEKFLAG_IGNORECOMMA, parseResult); // eat token
	}
	return resultString;
}

char* GetNextParameter_Int(TokenizerHandle tok, TextParserResult *parseResult)
{
	char *resultString = TokenizerPeek(tok, PEEKFLAG_IGNORECOMMA | PEEKFLAG_CHECKFORSTATICDEFINEINT, parseResult);
	if (!resultString || IsEol(resultString))
	{
		int line = TokenizerGetCurLine(tok);
		TokenizerErrorf(tok,"Missing parameter value");
		SET_ERROR_RESULT(*parseResult);
		return NULL;
	}
	else
	{
		TokenizerGet(tok, PEEKFLAG_IGNORECOMMA | PEEKFLAG_CHECKFORSTATICDEFINEINT, parseResult); // eat token
	}
	return resultString;
}




// just reports with error message if there are extra parameters
void VerifyParametersClear(TokenizerHandle tok, TextParserResult *parseResult)
{
	char* nexttoken = TokenizerGet(tok, PEEKFLAG_IGNORECOMMA, parseResult);
	if (!nexttoken || IsEol(nexttoken)) return; // fine

	// get rid of any extra params
	TokenizerErrorf(tok,"Extra parameter value \"%s\"", nexttoken);
	SET_ERROR_RESULT(*parseResult);
	ClearParameters(tok);
	TokenizerGet(tok, PEEKFLAG_IGNORECOMMA, parseResult); // clear eol
}




// parse the token into an integer, reports errors
static S64 GetInteger64(TokenizerHandle tok, char* token, TextParserResult *parseResult)
{
	char* end;
	S64 result;

	errno = 0;
	result = _strtoi64(token, &end, 10);
	if (errno || *end)
	{
		TokenizerErrorf(tok,"Got %s, expected integer value",token);
		SET_ERROR_RESULT(*parseResult);
	}
	return result;
}

static int GetInteger(TokenizerHandle tok, char* token, TextParserResult *parseResult)
{
	char* end;
	int result;
	int radix = 10;

	if (token[0] == '#')
	{
		radix = 16;
		token += 1;
	}
	else if (token[0] == '0' && token[1] == 'x')
	{
		radix = 16;
		token += 2;
	}

	errno = 0;
	result = (int)strtol(token, &end, radix);
	if (errno) // try unsigned if we couldn't fit it into signed int
	{
		errno = 0;
		result = (int)strtoul(token, &end, radix);
	}
	if (errno || *end)
	{
		TokenizerErrorf(tok,"Got %s, expected integer value",token);
		SET_ERROR_RESULT(*parseResult);
	}
	return result;
}

// parse the token into an IP, reports errors
static int GetIP(TokenizerHandle tok, char* token, TextParserResult *parseResult)
{
	int result;

	result = ipFromString(token);
	if (!result && stricmp(token, "0.0.0.0")!=0)
	{
		TokenizerErrorf(tok,"Got %s, expected IP String in the form of x.x.x.x",token);
		SET_ERROR_RESULT(*parseResult);
	}
	return result;
}

// parse the token into an unsigned integer, reports errors
static unsigned int GetUInteger(TokenizerHandle tok, char* token, TextParserResult *parseResult)
{
	char* end;
	unsigned int result;


	errno = 0;
	result = (unsigned int)strtoul(token, &end, 10);
	if (errno || *end)
	{
		TokenizerErrorf(tok,"Got %s, expected integer value",token);
		SET_ERROR_RESULT(*parseResult);
	}
	return result;
}


//used internally by all the int_parse types
U32 ReadTextIntFlags(TokenizerHandle tok,  TextParserResult *parseResult)
{
	U32 iRetVal = 0;
	while (1)
	{
		char* nexttoken = TokenizerPeek(tok, PEEKFLAG_IGNORECOMMA | PEEKFLAG_CHECKFORSTATICDEFINEINT, parseResult);
		if (!nexttoken || IsEol(nexttoken)) break;
		nexttoken = TokenizerGet(tok, PEEKFLAG_IGNORECOMMA | PEEKFLAG_CHECKFORSTATICDEFINEINT, parseResult);
		
		if (nexttoken == TOK_SPECIAL_INT_RETURN)
		{
			iRetVal |= TokenizerGetReadInt(tok);
		}
		else
		{
			iRetVal |= GetUInteger(tok, nexttoken, parseResult);
		}
	}

	return iRetVal;
}


//shared by all the int_Writetext cases
void WriteTextIntFlags(FILE* out, U32 iValue, void *subtable, bool bShowName)
{
	int i;
	U32 mask = 1;
	int first = 1;

	for (i = 0; i < 32; i++)
	{
		if (iValue & 1)
		{
			if (first) { WriteNString(out, " ",1, 0, 0); first = 0; }
			else { WriteNString(out, ", ",2, 0, 0); }
			WriteUInt(out, mask, 0, 0, subtable);
		}
		mask <<= 1;
		iValue >>= 1;
	}
	if (bShowName) WriteNString(out, NULL, 0, 0, 1);
}

//used internally by all the int_readString cases
U32 ReadStringIntFlags(const char* str, StaticDefineInt *pDefine, TextParserResult *parseResult)
{
	U32 value = 0;
	char* param;
	char *next;
	char *strtokcontext = 0;	
	TextParserResult ok = PARSERESULT_SUCCESS;
	
	if (!str || !str[0])
	{
		return 0;
	}
	param = ScratchAlloc(strlen(str)+1);
	assert(param);
	strcpy_s(param, strlen(str)+1, str);
	next = strtok_s(param, "|", &strtokcontext);
	while (next && RESULT_GOOD(ok))
	{
		int tempValue;
		int iEnum;
		while (next[0] == ' ') next++;
		if (!next[0]) break;
	
		if ((iEnum = StaticDefineInt_FastStringToInt(pDefine, next, INT_MIN)) != INT_MIN)
		{
			value |= iEnum;
		}
		else if (sscanf(next, "%d", &tempValue) == 1)
		{
			value |= tempValue;
		}
		else
		{
			SET_ERROR_RESULT(ok);
		}
		next = strtok_s(NULL, "|", &strtokcontext);
	}
	ScratchFree(param);
	if (!RESULT_GOOD(ok))
	{
		SET_ERROR_RESULT(*parseResult);
		return 0;
	}

	return value;
}

//used internally by all the int_writeString cases
void WriteStringIntFlags(char **estr, U32 iVal, StaticDefineInt *pSubTable, const char *caller_fname, int line, ParseTable *pTPI, int iColumn, WriteTextFlags iFlags)
{
	U32 mask = 1;
	int i;
	int first = 1;

	for (i = 0; i < 32; i++)
	{
		if (iVal & 1)
		{
			const char *define = NULL;
			if (!first)
			{
				estrAppend2_dbg_inline(estr, "| ", caller_fname, line);
			}
			first = 0;

			if (pSubTable) 
			{
				define = StaticDefineIntRevLookup_ForStructWriting(pSubTable, mask, pTPI, iColumn, iFlags);
			}
			if (define)
			{
				estrAppend2_dbg_inline(estr, define, caller_fname, line);
			}
			else
			{
				estrConcatf_dbg(estr, caller_fname, line, "%u",mask);
			}
		}
		mask <<= 1;
		iVal >>= 1;
	}
}


TextParserResult flags_writestring(ParseTable tpi[], int column, void* structptr, int index, char** estr, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *caller_fname, int line)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	U32 val = TokenStoreGetInt_inline(tpi, &tpi[column], column, structptr, 0, &ok);
	U32 mask = 1;
	int i;
	int first = 1;

	if (!RESULT_GOOD(ok)) return ok;

	for (i = 0; i < 32; i++)
	{
		if (val & 1)
		{
			const char *define = NULL;
			if (!first)
			{
				estrAppend2_dbg_inline(estr, "| ", caller_fname, line);
			}
			first = 0;
			if (tpi[column].subtable) 
			{
				define = StaticDefineIntRevLookup_ForStructWriting((StaticDefineInt*)tpi[column].subtable, mask, tpi, column, iWriteTextFlags);
			}
			if (define)
			{
				estrAppend2_dbg_inline(estr, define, caller_fname, line);
			}
			else
			{
				estrConcatf_dbg(estr, caller_fname, line, "%u",mask);
			}
		}
		mask <<= 1;
		val >>= 1;
	}

	return ok;
}


// parse the token into an float, reports errors
static float GetFloat(TokenizerHandle tok, char* token, TextParserResult *parseResult)
{
	char* end;
	float result;

	errno = 0;
	result = (float)strtod(token, &end);
	if (errno || *end)
	{
		TokenizerErrorf(tok,"Got %s, expected float value",token);
		SET_ERROR_RESULT(*parseResult);
	}
	return result;
}

// parse the token into an MultiVal type, reports errors
static MultiValType GetMultiValType(TokenizerHandle tok, char* token, TextParserResult *parseResult)
{
	MultiValType t = MultiValTypeFromString(token);

	if (t == MULTI_INVALID)
	{
		TokenizerErrorf(tok,"Got %s, expected MultiVal type",token);
		SET_ERROR_RESULT(*parseResult);
	}
	return t;
}

static void TokenizerParseFunctionCall(TokenizerHandle tok, StructFunctionCall*** callstructs, ParseTable tpi[], TextParserResult *parseResult)
{
	StructFunctionCall* nextstruct;
	char* nexttoken;
	int length, n;

	while (1)
	{
		const char *pStructTypeName = ParserGetTableName(tpi);
	

		nexttoken = TokenizerPeekEx(tok, PEEKFLAG_IGNORECOMMA, "()", &length, parseResult);
		if (!nexttoken || IsEol(nexttoken)) break;
		if (nexttoken[0] == ')' && length == 1)
		{
			TokenizerGetEx(tok, PEEKFLAG_IGNORECOMMA, "()", &length, parseResult); // eat it, and end recursion
			return;
		}

		// have a string, or an open paren now, going to need at least one struct
		if (!*callstructs)
			eaCreateInternal(callstructs, pStructTypeName?pStructTypeName:__FILE__, pStructTypeName?LINENUM_FOR_EARRAYS:__LINE__);
		nexttoken = TokenizerGetEx(tok, PEEKFLAG_IGNORECOMMA, "()", &length, parseResult); // eat the token

		// open paren means we need to recurse
		if (nexttoken[0] == '(' && length == 1)
		{
			n = eaSize(callstructs);
			if (!n) // error if we didn't actually get a function name
			{
				TokenizerErrorf(tok,"Open paren without function name");
				SET_INVALID_RESULT(*parseResult);
				ClearParameters(tok); // just clear to eol
				return;
			}
			TokenizerParseFunctionCall(tok, &((*callstructs)[n-1]->params), tpi, parseResult);
		}
		else
		{
			// otherwise, add the string like we were a string array
			nextstruct = StructAllocRawCharged(sizeof(*nextstruct), tpi);
			nextstruct->function = StructAllocStringLen_dbg(nexttoken, length, pStructTypeName?pStructTypeName:__FILE__, pStructTypeName?LINENUM_FOR_EARRAYS:__LINE__);
			eaPush_dbg(callstructs, nextstruct, pStructTypeName?pStructTypeName:__FILE__, pStructTypeName?LINENUM_FOR_EARRAYS:__LINE__);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////// UTILITY PROCEDURES

static int cmp8(U8 *v0, U8 *v1, int len) 
{
	int i, ret = 0;
	for (i = 0; i < len && ret == 0; i++)
	{
		ret = v0[i] - v1[i];
	}
	return ret;
}

////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////// TOKEN PRIMITIVES


ParseInfoFieldUsage ignore_interpretfield(ParseTable tpi[], int column, ParseInfoField field)
{
	return NoFieldUsage;
}

int ignore_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	char* nexttoken;

	//super crazy special case where we want to get FIXUPTYPE_HERE_IS_IGNORED_FIELD for IGNORE fields
	//that we read
	if (tpi && ptcc->type & TOK_WANT_FIXUP_WITH_IGNORED_FIELD)
	{
		char *pFullString = NULL;
		nexttoken = TokenizerPeek(tok, PEEKFLAG_IGNORECOMMA, parseResult);
		estrCopy2(&pFullString, "");
		while (nexttoken && !IsEol(nexttoken))
		{
			estrConcatf(&pFullString, "%s%s", estrLength(&pFullString) == 0 ? "" : " ", 
				nexttoken);

			TokenizerGet(tok, PEEKFLAG_IGNORECOMMA, parseResult);
			nexttoken = TokenizerPeek(tok, PEEKFLAG_IGNORECOMMA, parseResult);
		}

		//this call takes ownership of the estring entirely
		TokenizerSetSpecialIgnoreString(tok, structptr, tpi, ptcc->name, pFullString);
	}
	else
	{

		// ignore the rest of the line
		nexttoken = TokenizerPeek(tok, PEEKFLAG_IGNORECOMMA, parseResult);
		while (nexttoken && !IsEol(nexttoken))
		{
			TokenizerGet(tok, PEEKFLAG_IGNORECOMMA, parseResult);
			nexttoken = TokenizerPeek(tok, PEEKFLAG_IGNORECOMMA, parseResult);
		}
	}

	//tpi is NULL, then we were called by parserDoFieldOrStructIgnoring, so treat that the same
	//TOK_IGNORE_STRUCT
	if (!tpi || ptcc->type & TOK_IGNORE_STRUCT)
	{
		nexttoken = TokenizerGet(tok, PEEKFLAG_IGNORECOMMA, parseResult); //eat the EOL
		nexttoken = TokenizerPeek(tok, PEEKFLAG_IGNORECOMMA, parseResult); //peek at the next Token

		if (nexttoken)
		{
			if (strcmp(nexttoken, "{") == 0)
			{
				int iDepth = 1;

				TokenizerGet(tok, PEEKFLAG_IGNORECOMMA, parseResult); //eat the {

				do 
				{
					nexttoken = TokenizerGet(tok, PEEKFLAG_IGNORECOMMA, parseResult);

					if (nexttoken)
					{
						if (strcmp(nexttoken, "{") == 0)
						{
							iDepth++;
						}
						else if (strcmp(nexttoken, "}") == 0)
						{
							iDepth--;
						}
					}
				}
				while (iDepth > 0 && nexttoken);
			}
		}
	}

	return 0;
}


int ignorestruct_parse(TokenizerHandle tok, ParseTable tpi[], int column, void* structptr, int index, TextParserResult *parseResult)
{
	// ignore the rest of the line
	char* nexttoken = TokenizerPeek(tok, PEEKFLAG_IGNORECOMMA, parseResult);
	while (nexttoken && !IsEol(nexttoken))
	{
		TokenizerGet(tok, PEEKFLAG_IGNORECOMMA, parseResult);
		nexttoken = TokenizerPeek(tok, PEEKFLAG_IGNORECOMMA, parseResult);
	}

	return 0;
}


ParseInfoFieldUsage command_interpretfield(ParseTable tpi[], int column, ParseInfoField field)
{
	switch (field)
	{
	case ParamField:
		return PointerToCommandString;
	}
	return NoFieldUsage;
}

// we give a warning and refuse to parse this field
int error_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	TokenizerErrorf(tok,"Found unparsable field %s",ptcc->name);
	SET_ERROR_RESULT(*parseResult);
	return ignore_parse(tok, tpi, ptcc, column, structptr, 0, parseResult); // ignore the rest of the (incorrect) line for convenience
}

bool error_senddiff(Packet* pak, ParseTable tpi[], int column, const void* oldstruct, const void* newstruct, int index, enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB)
{
	Errorf("Trying to send an invalid token type!  (xx_senddiff not implemented)");
	return false;
}

bool error_recvdiff(Packet* pak, ParseTable tpi[], int column, void* structptr, int index, enumRecvDiffFlags eFlags)
{
	RECV_FAIL("Trying to receive an invalid token type!  (xx_recvdiff not implemented)");

	//this return will never be hit, actually
	return 0;
}

int end_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	return 1; // terminate the surrounding struct
}

ParseInfoFieldUsage number_interpretfield(ParseTable tpi[], int column, ParseInfoField field)
{
	switch (field)
	{
	case ParamField:
		if (tpi[column].type & TOK_FIXED_ARRAY)
			return NumberOfElements;
		if (tpi[column].type & TOK_EARRAY)
			return NoFieldUsage;
		return DefaultValue;
	case SubtableField:
		return StaticDefineList;
	}
	return NoFieldUsage;
}

bool u8_initstruct(ParseTable tpi[], int column, void* structptr, int index)
{
	int def = (tpi[column].type & (TOK_FIXED_ARRAY | TOK_EARRAY))?0:tpi[column].param;
	TokenStoreSetU8_inline(tpi, &tpi[column], column, structptr, index, def, NULL, NULL);
	return false;
}


int u8_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	int value;
	char* nexttoken;
	
	if (TOK_GET_FORMAT_OPTIONS(ptcc->format) == TOK_FORMAT_FLAGS)
	{
		U32 iVal = ReadTextIntFlags(tok, parseResult);
		if (iVal > 255)
		{
			TokenizerErrorf(tok, "U8 FLAGS parameter exceeds 255");
			SET_ERROR_RESULT(*parseResult);
			iVal = 0;
		}
		TokenStoreSetU8_inline(tpi, ptcc, column, structptr, index, iVal, parseResult, tok);
		return 0;
	}

	nexttoken = GetNextParameter_Int(tok, parseResult);
	if (!nexttoken) return 0;

	if (nexttoken == TOK_SPECIAL_INT_RETURN)
	{
		value = TokenizerGetReadInt(tok);
	}
	else
	{
		value = GetInteger(tok, nexttoken, parseResult);
	}

	if (value < 0 || value > 255)
	{
		TokenizerErrorf(tok,"U8 parameter exceeds 0..255");
		SET_ERROR_RESULT(*parseResult);
		value = 0;
	}

	TokenStoreSetU8_inline(tpi, ptcc, column, structptr, index, value, parseResult, tok);
	return 0;
}


TextParserResult u8_writebin(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	U8 value = TokenStoreGetU8_inline(tpi, &tpi[column], column, structptr, index, &ok);
	SET_MIN_RESULT(ok, SimpleBufWrite(&value, 1, file));
	*datasum += sizeof(U8);
	return ok;
}

TextParserResult u8_readbin(SimpleBufHandle file, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	U8 value = 0;
	if (!SimpleBufRead(&value, 1, file))
		SET_INVALID_RESULT(ok);
	*datasum += sizeof(U8);
	TokenStoreSetU8_inline(tpi, &tpi[column], column, structptr, index, value, &ok, NULL);
	return ok;
}

void u8_writehdiff(char** estr, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, char *prefix, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	int val = TokenStoreGetU8_inline(tpi, &tpi[column], column, newstruct, index, NULL);
	char intBuff[16];
	int iIntLen = fastIntToString_inline(val, intBuff);
	WriteSetValueStringForTextDiff(estr, prefix, (int)strlen(prefix), intBuff + 16 - iIntLen, iIntLen, caller_fname, line);

}

void u8_writebdiff(StructDiff *diff, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, ObjectPath *parentPath, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool invertExcludeFlags, const char *caller_fname, int line)
{
	StructDiffOp *op = eaTail(&diff->ppOps);
	if (op) {
		op->pOperand = MultiValCreate();
		MultiValSetInt(op->pOperand, TokenStoreGetU8_inline(tpi, &tpi[column], column, newstruct, index, NULL));
	}
}

bool u8_senddiff(Packet* pak, ParseTable tpi[], int column, const void* oldstruct, const void* newstruct, int index, enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB)
{
	if (oldstruct && (eFlags & SENDDIFF_FLAG_COMPAREBEFORESENDING) && u8_compare(tpi,column, oldstruct, newstruct, index, 0, 0, 0) == 0)
	{
		return false;
	}
	

	if (!(eFlags & SENDDIFF_FLAG_ALLOWDIFFS))
		pktSendBitsPack(pak, TOK_GET_PRECISION_DEF(tpi[column].type, 1), TokenStoreGetU8_inline(tpi, &tpi[column], column, newstruct, index, NULL));
	else
	{
		int d = (int)TokenStoreGetU8_inline(tpi, &tpi[column], column, newstruct, index, NULL) - (int)TokenStoreGetU8_inline(tpi, &tpi[column], column, oldstruct, index, NULL);
		packDelta(pak, d);
	}

	return true;
}

bool u8_recvdiff(Packet* pak, ParseTable tpi[], int column, void* structptr, int index, enumRecvDiffFlags eFlags)
{

	if (eFlags & RECVDIFF_FLAG_ABS_VALUES)
	{
		int data = pktGetBitsPack(pak, TOK_GET_PRECISION_DEF(tpi[column].type, 1));
		if (structptr) TokenStoreSetU8_inline(tpi, &tpi[column], column, structptr, index, data, NULL, NULL);
	}
	else
	{
		int data = unpackDelta(pak);
		if (structptr) 
		{
			data += (int)TokenStoreGetU8_inline(tpi, &tpi[column], column, structptr, index, NULL);
			TokenStoreSetU8_inline(tpi, &tpi[column], column, structptr, index, data, NULL, NULL);
		}
	}

	return 1;
}

TextParserResult u8_writestring(ParseTable tpi[], int column, void* structptr, int index, char** estr, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *caller_fname, int line)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int val = TokenStoreGetU8_inline(tpi, &tpi[column], column, structptr, index, &ok);
	if (RESULT_GOOD(ok)) 
	{
		if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_FLAGS)
		{
			WriteStringIntFlags	(estr, val, (StaticDefineInt*)tpi[column].subtable, caller_fname, line, tpi, column, iWriteTextFlags);
				
		}
		else
		{
			const char* rev = NULL;
			if (tpi[column].subtable)
				rev = StaticDefineIntRevLookup_ForStructWriting(tpi[column].subtable, val, tpi, column, iWriteTextFlags);
			if (rev)
				estrConcatf_dbg(estr, caller_fname, line, "%s", rev);
			else
				estrConcatf_dbg(estr, caller_fname, line, "%d", val);
		}
	}
	return ok;
}

TextParserResult u8_readstring(ParseTable tpi[], int column, void* structptr, int index, const char* str, ReadStringFlags eFlags)
{
	int val = 0;
	TextParserResult ok = PARSERESULT_SUCCESS;

	if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_FLAGS)
	{
		val = ReadStringIntFlags(str, tpi[column].subtable, &ok);
	}
	else
	{
		// do define lookup if possible
		if (tpi[column].subtable)
		{
			val = StaticDefine_FastStringToInt(tpi[column].subtable, str, INT_MIN);
			if (val != INT_MIN)
			{
				if (val < 0 || val > 255) SET_ERROR_RESULT(ok);
				if (RESULT_GOOD(ok)) TokenStoreSetU8_inline(tpi, &tpi[column], column, structptr, index, val, &ok, NULL);
				return ok;	
			}
		}
		if (sscanf(str, "%d", &val) != 1) SET_ERROR_RESULT(ok);
	}
	if (val < 0 || val > 255) SET_ERROR_RESULT(ok);
	if (RESULT_GOOD(ok)) TokenStoreSetU8_inline(tpi, &tpi[column], column, structptr, index, val, &ok, NULL);
	return ok;
}

TextParserResult u8_tomulti(ParseTable tpi[], int column, void* structptr, int index, MultiVal* result, bool bDuplicateData, bool dummyOnNULL)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int val;
	if(!structptr && dummyOnNULL)
		MultiValSetDummyType(result, MULTI_INT);
	else
	{
		val = TokenStoreGetU8_inline(tpi, &tpi[column], column, structptr, index, &ok);
		if (RESULT_GOOD(ok)) MultiValSetInt(result, val);
	}
	return ok;
}

TextParserResult u8_frommulti(ParseTable tpi[], int column, void* structptr, int index, const MultiVal* value)
{
	bool mvRes;
	int val = MultiValGetInt(value, &mvRes);
	TextParserResult ok = mvRes;
	if (val < 0 || val > 255) SET_ERROR_RESULT(ok);
	if (RESULT_GOOD(ok)) TokenStoreSetU8_inline(tpi, &tpi[column], column, structptr, index, val, &ok, NULL);
	return ok;
}

void u8_updatecrc(ParseTable tpi[], int column, void* structptr, int index)
{
	U8 value = TokenStoreGetU8_inline(tpi, &tpi[column], column, structptr, index, NULL);
	cryptAdler32Update(&value, sizeof(U8));
}

int u8_compare(ParseTable tpi[], int column, const void* lhs, const void* rhs, int index, enumCompareFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	U8 left;
	U8 right = TokenStoreGetU8_inline(tpi, &tpi[column], column, rhs, index, NULL);

	if (lhs)
		left = TokenStoreGetU8_inline(tpi, &tpi[column], column, lhs, index, NULL);
	else
		left = (tpi[column].type & (TOK_FIXED_ARRAY | TOK_EARRAY))?0:tpi[column].param;

	return (int)left - (int)right;
}

void u8_calcoffset(ParseTable tpi[], int column, size_t* size)
{
	tpi[column].storeoffset = *size;
	(*size) += sizeof(U8);
}

void u8_copyfield(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TokenStoreSetU8_inline(tpi, ptcc, column, dest, index, TokenStoreGetU8_inline(tpi, &tpi[column], column, src, index, NULL), NULL, NULL);
}

enumCopy2TpiResult u8_copyfield2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest,  
	CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, char **ppResultString)
{
	TokenStoreSetU8_inline(dest_tpi, &dest_tpi[dest_column], dest_column, dest, dest_index, TokenStoreGetU8_inline(src_tpi, &src_tpi[src_column], src_column, src, src_index, NULL), NULL, NULL);
	return COPY2TPIRESULT_SUCCESS;
}

void u8_interp(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 interpParam)
{
	U8 result = interpU8(interpParam, TokenStoreGetU8_inline(tpi, &tpi[column], column, structA, index, NULL), TokenStoreGetU8_inline(tpi, &tpi[column], column, structB, index, NULL));
	TokenStoreSetU8_inline(tpi, &tpi[column], column, destStruct, index, result, NULL, NULL);
}

void u8_calcrate(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 deltaTime)
{
	F32 result = ((F32)TokenStoreGetU8_inline(tpi, &tpi[column], column, structB, index, NULL) - (F32)TokenStoreGetU8_inline(tpi, &tpi[column], column, structA, index, NULL)) / deltaTime;
	TokenStoreSetU8_inline(tpi, &tpi[column], column, destStruct, index, round(result), NULL, NULL);
}

void u8_integrate(ParseTable tpi[], int column, void* valueStruct, void* rateStruct, void* destStruct, int index, F32 deltaTime)
{
	F32 result = (F32)TokenStoreGetU8_inline(tpi, &tpi[column], column, valueStruct, index, NULL) + deltaTime * (F32)TokenStoreGetU8_inline(tpi, &tpi[column], column, rateStruct, index, NULL);
	TokenStoreSetU8_inline(tpi, &tpi[column], column, destStruct, index, round(result), NULL, NULL);
}

void u8_calccyclic(ParseTable tpi[], int column, void* valueStruct, void* ampStruct, void* freqStruct, void* cycleStruct, void* destStruct, int index, F32 fStartTime, F32 deltaTime)
{
	F32 freq = TokenStoreGetU8_inline(tpi, &tpi[column], column, freqStruct, index, NULL);
	F32 cycle = TokenStoreGetU8_inline(tpi, &tpi[column], column, cycleStruct, index, NULL);
	F32 fSinDiff = ( sinf( (fStartTime + deltaTime) * freq + cycle * TWOPI) - sinf( ( fStartTime * freq + cycle ) * TWOPI ));
	F32 result = TokenStoreGetU8_inline(tpi, &tpi[column], column, valueStruct, index, NULL) + TokenStoreGetU8_inline(tpi, &tpi[column], column, ampStruct, index, NULL) * fSinDiff;
	TokenStoreSetU8_inline(tpi, &tpi[column], column, destStruct, index, round(result), NULL, NULL);
}

void u8_applydynop(ParseTable tpi[], int column, void* dstStruct, void* srcStruct, int index, DynOpType optype, const F32* values, U8 uiValuesSpecd, U32* seed)
{
	switch (optype)
	{
	case DynOpType_Jitter:
	{
		int iToAdd = round(*values * randomF32Seeded(seed, RandType_BLORN));
		U8 result = CLAMP(TokenStoreGetU8_inline(tpi, &tpi[column], column, dstStruct, index, NULL) + iToAdd, 0, 255);
		TokenStoreSetU8_inline(tpi, &tpi[column], column, dstStruct, index, result, NULL, NULL);
	} break;
	case DynOpType_Inherit:
	{
		if (srcStruct) TokenStoreSetU8_inline(tpi, &tpi[column], column, dstStruct, index, TokenStoreGetU8_inline(tpi, &tpi[column], column, srcStruct, index, NULL), NULL, NULL);
	} break;
	case DynOpType_Add:
	{
		TokenStoreSetU8_inline(tpi, &tpi[column], column, dstStruct, index, CLAMP((int)TokenStoreGetU8_inline(tpi, &tpi[column], column, dstStruct, index, NULL) + round(*values), 0, 255), NULL, NULL);
	} break;
	case DynOpType_Multiply:
	{
		TokenStoreSetU8_inline(tpi, &tpi[column], column, dstStruct, index, CLAMP(round((F32)TokenStoreGetU8_inline(tpi, &tpi[column], column, dstStruct, index, NULL) * *values), 0, 255), NULL, NULL);
	} break;
	case DynOpType_Min:
	{
		TokenStoreSetU8_inline(tpi, &tpi[column], column, dstStruct, index, MIN(CLAMP(round(*values), 0, 255), TokenStoreGetU8_inline(tpi, &tpi[column], column, dstStruct, index, NULL)), NULL, NULL);
	} break;
	case DynOpType_Max:
	{
		TokenStoreSetU8_inline(tpi, &tpi[column], column, dstStruct, index, MAX(CLAMP(round(*values), 0, 255), TokenStoreGetU8_inline(tpi, &tpi[column], column, dstStruct, index, NULL)), NULL, NULL);
	} break;
	}
}

void u8_queryCopyStruct(ParseTable *pTPI, int iColumn, int iIndex, int iOffsetIntoParentStruct, 
	CopyQueryFlags eQueryFlags, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, 
	StructTypeField iOptionFlagsToExclude, void *pUserData)
{
	//if we are not in a fixed array, index will equal 0. So for this and all INT types, we just pretend we're in a fixed
	//array, and offset our offset by the right amount
	StructCopyQueryResult_MarkBytes(!(eQueryFlags & COPYQUERYFLAG_YOU_ARE_FLAGGED_OUT), iOffsetIntoParentStruct + iIndex * sizeof(U8), sizeof(U8), pUserData);
}

bool int16_initstruct(ParseTable tpi[], int column, void* structptr, int index)
{
	int def = (tpi[column].type & (TOK_FIXED_ARRAY | TOK_EARRAY))?0:tpi[column].param;
	TokenStoreSetInt16_inline(tpi, &tpi[column], column, structptr, index, def, NULL, NULL);
	return false;
}

int int16_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	char* nexttoken;
	int value;

	if (TOK_GET_FORMAT_OPTIONS(ptcc->format) == TOK_FORMAT_FLAGS)
	{
		U32 iVal = ReadTextIntFlags(tok, parseResult);
		if (iVal >= (1 << 16))
		{
			TokenizerErrorf(tok, "U16 FLAGS parameter exceeds 65536");
			SET_ERROR_RESULT(*parseResult);
			iVal = 0;
		}
		TokenStoreSetInt16_inline(tpi, ptcc, column, structptr, index, iVal, parseResult, tok);
		return 0;
	}

	nexttoken = GetNextParameter_Int(tok, parseResult);
	if (!nexttoken) 
		return 0;

	if (nexttoken == TOK_SPECIAL_INT_RETURN)
	{
		value = TokenizerGetReadInt(tok);
	}
	else
	{
		value = GetInteger(tok, nexttoken, parseResult);
	}

	TokenStoreSetInt16_inline(tpi, ptcc, column, structptr, index, value, parseResult, tok);
	
	return 0;
}


TextParserResult int16_writebin(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int value = TokenStoreGetInt16_inline(tpi, &tpi[column], column, structptr, index, &ok);
	SET_MIN_RESULT(ok, SimpleBufWriteU32(value, file));
	*datasum += sizeof(int);
	return ok;
}

TextParserResult int16_readbin(SimpleBufHandle file,  FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int value = 0;
	if (!SimpleBufReadU32((U32*)&value, file))
		SET_INVALID_RESULT(ok);
	*datasum += sizeof(int);
	TokenStoreSetInt16_inline(tpi, &tpi[column], column, structptr, index, (S16)value, &ok, NULL);
	return ok;
}

void int16_writehdiff(char** estr, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, char *prefix, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	int val = TokenStoreGetInt16_inline(tpi, &tpi[column], column, newstruct, index, NULL);
	char intBuff[16];
	int iIntLen = fastIntToString_inline(val, intBuff);
	WriteSetValueStringForTextDiff(estr, prefix, (int)strlen(prefix), intBuff + 16 - iIntLen, iIntLen, caller_fname, line);
}

void int16_writebdiff(StructDiff *diff, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, ObjectPath *parentPath, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool invertExcludeFlags, const char *caller_fname, int line)
{
	StructDiffOp *op = eaTail(&diff->ppOps);
	if (op) {
		op->pOperand = MultiValCreate();
		MultiValSetInt(op->pOperand, TokenStoreGetInt16_inline(tpi, &tpi[column], column, newstruct, index, NULL));
	}
}




bool int16_senddiff(Packet* pak, ParseTable tpi[], int column, const void* oldstruct, const void* newstruct, int index, enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB)
{
	if (/*oldstruct &&*/ (eFlags & SENDDIFF_FLAG_COMPAREBEFORESENDING) && int16_compare(tpi,column, oldstruct, newstruct, index, 0, 0, 0) == 0)
	{
		return false;
	}
	

	if (!(eFlags & SENDDIFF_FLAG_ALLOWDIFFS))
		pktSendBitsPack(pak, TOK_GET_PRECISION_DEF(tpi[column].type, 1), TokenStoreGetInt16_inline(tpi, &tpi[column], column, newstruct, index, NULL));
	else
	{
		int d = TokenStoreGetInt16_inline(tpi, &tpi[column], column, newstruct, index, NULL) - TokenStoreGetInt16_inline(tpi, &tpi[column], column, oldstruct, index, NULL);
		packDelta(pak, d);
	}

	return true;
}

bool int16_recvdiff(Packet* pak, ParseTable tpi[], int column, void* structptr, int index, enumRecvDiffFlags eFlags)
{

	if (eFlags & RECVDIFF_FLAG_ABS_VALUES)
	{
		int data = pktGetBitsPack(pak, TOK_GET_PRECISION_DEF(tpi[column].type, 1));
		if (structptr) TokenStoreSetInt16_inline(tpi, &tpi[column], column, structptr, index, data, NULL, NULL);
	}
	else
	{
		int data = unpackDelta(pak);
		if (structptr)
		{
			data += (int)TokenStoreGetInt16_inline(tpi, &tpi[column], column, structptr, index, NULL);
			TokenStoreSetInt16_inline(tpi, &tpi[column], column, structptr, index, data, NULL, NULL);
		}
	}

	return 1;
}

TextParserResult int16_writestring(ParseTable tpi[], int column, void* structptr, int index, char** estr, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *caller_fname, int line)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int val = TokenStoreGetInt16_inline(tpi, &tpi[column], column, structptr, index, NULL);
	if (RESULT_GOOD(ok)) 
	{
		if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_FLAGS)
		{
			WriteStringIntFlags(estr, val, (StaticDefineInt*)tpi[column].subtable, caller_fname, line, tpi, column, iWriteTextFlags);
				
		}
		else
		{
			const char* rev = NULL;
			if (tpi[column].subtable)
				rev = StaticDefineIntRevLookup_ForStructWriting(tpi[column].subtable, val, tpi, column, iWriteTextFlags);
			if (rev)
				estrConcatf_dbg(estr, caller_fname, line, "%s", rev);
			else
				estrConcatf_dbg(estr, caller_fname, line, "%d", val);
		}	
	}
	return ok;
}

TextParserResult int16_readstring(ParseTable tpi[], int column, void* structptr, int index, const char* str, ReadStringFlags eFlags)
{
	int val = 0;
	TextParserResult ok = PARSERESULT_SUCCESS;

	if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_FLAGS)
	{
		val = ReadStringIntFlags(str, tpi[column].subtable, &ok);
	}
	else
	{
		// do define lookup if possible
		if (tpi[column].subtable)
		{
			val = StaticDefine_FastStringToInt(tpi[column].subtable, str, INT_MIN);
			if (val != INT_MIN)
			{
				if (val < SHRT_MIN || val > SHRT_MAX) SET_ERROR_RESULT(ok);
				if (RESULT_GOOD(ok)) TokenStoreSetInt16_inline(tpi, &tpi[column], column, structptr, index, val, &ok, NULL);
				return ok;
			}
		}
		if (sscanf(str, "%d", &val) != 1) SET_ERROR_RESULT(ok);
	}

	if (val < SHRT_MIN || val > SHRT_MAX) SET_ERROR_RESULT(ok);
	if (RESULT_GOOD(ok)) TokenStoreSetInt16_inline(tpi, &tpi[column], column, structptr, index, val, &ok, NULL);
	return ok;
}

TextParserResult int16_tomulti(ParseTable tpi[], int column, void* structptr, int index, MultiVal* result, bool bDuplicateData, bool dummyOnNULL)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int val;

	if(!structptr && dummyOnNULL)
		MultiValSetDummyType(result, MULTI_INT);
	else
	{
		val = TokenStoreGetInt16_inline(tpi, &tpi[column], column, structptr, index, NULL);
		if (RESULT_GOOD(ok)) MultiValSetInt(result, val);
	}
	return true;
}

TextParserResult int16_frommulti(ParseTable tpi[], int column, void* structptr, int index, const MultiVal* value)
{

	bool mvRes;
	int val = MultiValGetInt(value, &mvRes);
	TextParserResult ok = mvRes;
	if (val < SHRT_MIN || val > SHRT_MAX) SET_ERROR_RESULT(ok);
	if (RESULT_GOOD(ok)) TokenStoreSetInt16_inline(tpi, &tpi[column], column, structptr, index, val, &ok, NULL);
	return ok;
}

void int16_updatecrc(ParseTable tpi[], int column, void* structptr, int index)
{
	S16 value = TokenStoreGetInt16_inline(tpi, &tpi[column], column, structptr, index, NULL);
	cryptAdler32Update_AutoEndian((U8*)&value, sizeof(S16));
}

int int16_compare(ParseTable tpi[], int column, const void* lhs, const void* rhs, int index, enumCompareFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	S16 left;
	S16 right = TokenStoreGetInt16_inline(tpi, &tpi[column], column, rhs, index, NULL);

	if (lhs)
		left = TokenStoreGetInt16_inline(tpi, &tpi[column], column, lhs, index, NULL);
	else
		left = (tpi[column].type & (TOK_FIXED_ARRAY | TOK_EARRAY))?0:tpi[column].param;

	return left - right;
}

void int16_calcoffset(ParseTable tpi[], int column, size_t* size)
{
	MEM_ALIGN(*size, sizeof(S16));
	tpi[column].storeoffset = *size;
	(*size) += sizeof(S16);
}

void int16_copyfield(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TokenStoreSetInt16_inline(tpi, ptcc, column, dest, index, TokenStoreGetInt16_inline(tpi, ptcc, column, src, index, NULL), NULL, NULL);
}

enumCopy2TpiResult int16_copyfield2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest,  
	CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, char **ppResultString)
{
	TokenStoreSetInt16_inline(dest_tpi, &dest_tpi[dest_column], dest_column, dest, dest_index, TokenStoreGetInt16_inline(src_tpi, &src_tpi[src_column], src_column, src, src_index, NULL), NULL, NULL);
	return COPY2TPIRESULT_SUCCESS;
}


void int16_endianswap(ParseTable tpi[], int column, void* structptr, int index)
{
	TokenStoreSetInt16_inline(tpi, &tpi[column], column, structptr, index, endianSwapU16(TokenStoreGetInt16_inline(tpi, &tpi[column], column, structptr, index, NULL)), NULL, NULL);
}

void int16_interp(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 interpParam)
{
	S16 result = interpS16(interpParam, TokenStoreGetInt16_inline(tpi, &tpi[column], column, structA, index, NULL), TokenStoreGetInt16_inline(tpi, &tpi[column], column, structB, index, NULL));
	TokenStoreSetInt16_inline(tpi, &tpi[column], column, destStruct, index, result, NULL, NULL);
}

void int16_calcrate(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 deltaTime)
{
	F32 result = ((F32)TokenStoreGetInt16_inline(tpi, &tpi[column], column, structB, index, NULL) - (F32)TokenStoreGetInt16_inline(tpi, &tpi[column], column, structA, index, NULL)) / deltaTime;
	TokenStoreSetInt16_inline(tpi, &tpi[column], column, destStruct, index, round(result), NULL, NULL);
}

void int16_integrate(ParseTable tpi[], int column, void* valueStruct, void* rateStruct, void* destStruct, int index, F32 deltaTime)
{
	F32 result = (F32)TokenStoreGetInt16_inline(tpi, &tpi[column], column, valueStruct, index, NULL) + deltaTime * (F32)TokenStoreGetInt16_inline(tpi, &tpi[column], column, rateStruct, index, NULL);
	TokenStoreSetInt16_inline(tpi, &tpi[column], column, destStruct, index, round(result), NULL, NULL);
}

void int16_calccyclic(ParseTable tpi[], int column, void* valueStruct, void* ampStruct, void* freqStruct, void* cycleStruct, void* destStruct, int index, F32 fStartTime, F32 deltaTime)
{
	F32 freq = TokenStoreGetInt16_inline(tpi, &tpi[column], column, freqStruct, index, NULL);
	F32 cycle = TokenStoreGetInt16_inline(tpi, &tpi[column], column, cycleStruct, index, NULL);
	F32 fSinDiff = ( sinf( (fStartTime + deltaTime) * freq + cycle * TWOPI) - sinf( ( fStartTime * freq + cycle ) * TWOPI ));
	F32 result = TokenStoreGetInt16_inline(tpi, &tpi[column], column, valueStruct, index, NULL) + TokenStoreGetInt16_inline(tpi, &tpi[column], column, ampStruct, index, NULL) * fSinDiff;
	TokenStoreSetInt16_inline(tpi, &tpi[column], column, destStruct, index, round(result), NULL, NULL);
}

void int16_applydynop(ParseTable tpi[], int column, void* dstStruct, void* srcStruct, int index, DynOpType optype, const F32* values, U8 uiValuesSpecd, U32* seed)
{
	switch (optype)
	{
	case DynOpType_Jitter:
	{
		int iToAdd = round(*values * randomF32Seeded(seed, RandType_BLORN));
		S16 result = CLAMP(TokenStoreGetInt16_inline(tpi, &tpi[column], column, dstStruct, index, NULL) + iToAdd, SHRT_MIN, SHRT_MAX);
		TokenStoreSetInt16_inline(tpi, &tpi[column], column, dstStruct, index, result, NULL, NULL);
	} break;
	case DynOpType_Inherit:
	{
		if (srcStruct) TokenStoreSetInt16_inline(tpi, &tpi[column], column, dstStruct, index, TokenStoreGetInt16_inline(tpi, &tpi[column], column, srcStruct, index, NULL), NULL, NULL);
	} break;
	case DynOpType_Add:
	{
		TokenStoreSetInt16_inline(tpi, &tpi[column], column, dstStruct, index, CLAMP((int)TokenStoreGetInt16_inline(tpi, &tpi[column], column, dstStruct, index, NULL) + round(*values), SHRT_MIN, SHRT_MAX), NULL, NULL);
	} break;
	case DynOpType_Multiply:
	{
		TokenStoreSetInt16_inline(tpi, &tpi[column], column, dstStruct, index, CLAMP(round((F32)TokenStoreGetInt16_inline(tpi, &tpi[column], column, dstStruct, index, NULL) * *values), SHRT_MIN, SHRT_MAX), NULL, NULL);
	} break;
	case DynOpType_Min:
	{
		TokenStoreSetInt16_inline(tpi, &tpi[column], column, dstStruct, index, MIN(CLAMP(round(*values), SHRT_MIN, SHRT_MAX), TokenStoreGetInt16_inline(tpi, &tpi[column], column, dstStruct, index, NULL)), NULL, NULL);
	} break;
	case DynOpType_Max:
	{
		TokenStoreSetInt16_inline(tpi, &tpi[column], column, dstStruct, index, MAX(CLAMP(round(*values), SHRT_MIN, SHRT_MAX), TokenStoreGetInt16_inline(tpi, &tpi[column], column, dstStruct, index, NULL)), NULL, NULL);
	} break;
	}
}

void int16_queryCopyStruct(ParseTable *pTPI, int iColumn, int iIndex, int iOffsetIntoParentStruct, 
	CopyQueryFlags eQueryFlags, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, 
	StructTypeField iOptionFlagsToExclude, void *pUserData)
{
	StructCopyQueryResult_MarkBytes(!(eQueryFlags & COPYQUERYFLAG_YOU_ARE_FLAGGED_OUT), iOffsetIntoParentStruct + iIndex * sizeof(U16), sizeof(U16), pUserData);
}

bool int_initstruct(ParseTable tpi[], int column, void* structptr, int index)
{
	int def = (tpi[column].type & (TOK_FIXED_ARRAY | TOK_EARRAY))?0:tpi[column].param;
	TokenStoreSetInt_inline(tpi, &tpi[column], column, structptr, index, def, NULL, NULL);
	return false;
}

int int_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	int value;
	char* nexttoken;
	StructFormatField format_options = TOK_GET_FORMAT_OPTIONS(ptcc->format);
	
	if (format_options == TOK_FORMAT_FLAGS)
	{
		U32 iVal = ReadTextIntFlags(tok, parseResult);
		TokenStoreSetInt_inline(tpi, ptcc, column, structptr, index, iVal, parseResult, tok);
		return 0;
	}
	
	nexttoken = GetNextParameter_Int(tok, parseResult);
	if (!nexttoken) 
		return 0;

	if (nexttoken == TOK_SPECIAL_INT_RETURN)
	{
		value = TokenizerGetReadInt(tok);
	}
	else if (format_options == TOK_FORMAT_IP)
	{
		value = GetIP(tok, nexttoken, parseResult);
	}
	else if (format_options == TOK_FORMAT_DATESS2000)
	{
		value = timeGetSecondsSince2000FromDateString(nexttoken);
		if(!value)
		{
			TokenizerErrorf(tok,"Got %s, expected date value\n", nexttoken);
			SET_ERROR_RESULT(*parseResult);
		}
		else
		{
			TokenizerGet(tok, PEEKFLAG_IGNORECOMMA, parseResult);
		}
	}
	else 
	{
		value = GetInteger(tok, nexttoken, parseResult);
	}

	TokenStoreSetInt_inline(tpi, ptcc, column, structptr, index, value, parseResult, tok);
	return 0;
}

//bool int_writestring(ParseTable tpi[], int column, void* structptr, int index, char** estr, WriteTextFlags iWriteTextFlags);


TextParserResult int_writebin(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int value = TokenStoreGetInt_inline(tpi, &tpi[column], column, structptr, index, &ok);
	SET_MIN_RESULT(ok, SimpleBufWriteU32(value, file));
	*datasum += sizeof(int);
	return ok;
}

TextParserResult int_readbin(SimpleBufHandle file,  FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int value = 0;
	if (!SimpleBufReadU32((U32*)&value, file))
		SET_INVALID_RESULT(ok);
	*datasum += sizeof(int);
	TokenStoreSetInt_inline(tpi, &tpi[column], column, structptr, index, value, &ok, NULL);
	return ok;
}

void int_writebdiff(StructDiff *diff, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, ObjectPath *parentPath, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool invertExcludeFlags, const char *caller_fname, int line)
{
	StructDiffOp *op = eaTail(&diff->ppOps);
	if (op) {
		op->pOperand = MultiValCreate();
		MultiValSetInt(op->pOperand,TokenStoreGetInt_inline(tpi, &tpi[column], column, newstruct, index, NULL));
	}
}

static __forceinline int int_compare_inline(ParseTable tpi[], int column, const void* lhs, const void* rhs, int index, enumCompareFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int left;
	int right = TokenStoreGetInt_inline(tpi, &tpi[column], column, rhs, index, NULL);
	int i, format = TOK_GET_FORMAT_OPTIONS(tpi[column].format);

	if (lhs)
		left = TokenStoreGetInt_inline(tpi, &tpi[column], column, lhs, index, NULL);
	else
		left = (tpi[column].type & (TOK_FIXED_ARRAY | TOK_EARRAY))?0:tpi[column].param;

	if (format == TOK_FORMAT_IP)
	{
		// MAK - is this correct?  not endian compatible, but it looks like
		// makeIpStr does endian swapping later.  I'm just using the existing code.
		for (i = 0; i < sizeof(int); i++) {
			int ret = cmp8(((U8*)&left)+i, ((U8*)&right)+i, 1);
			if (ret)
				return ret;
		}
	}
	else
	{
		return left - right;
	}
	return 0;
}

int int_compare(ParseTable tpi[], int column, const void* lhs, const void* rhs, int index, enumCompareFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	return int_compare_inline(tpi,column,lhs,rhs,index,eFlags,iOptionFlagsToMatch,iOptionFlagsToExclude);
}

bool int_senddiff(Packet* pak, ParseTable tpi[], int column, const void* oldstruct, const void* newstruct, int index, enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB)
{
	if (/*oldstruct &&*/ (eFlags & SENDDIFF_FLAG_COMPAREBEFORESENDING) && int_compare_inline(tpi,column, oldstruct, newstruct, index, 0, 0, 0) == 0)
	{
		return false;
	}
	
	if (!(eFlags & SENDDIFF_FLAG_ALLOWDIFFS))
		pktSendBitsPack(pak, TOK_GET_PRECISION_DEF(tpi[column].type, 1), TokenStoreGetInt_inline(tpi, &tpi[column], column, newstruct, index, NULL));
	else
	{
		int d = TokenStoreGetInt_inline(tpi, &tpi[column], column, newstruct, index, NULL) - TokenStoreGetInt_inline(tpi, &tpi[column], column, oldstruct, index, NULL);
		packDelta(pak, d);
	}
	return true;
}

bool int_recvdiff(Packet* pak, ParseTable tpi[], int column, void* structptr, int index, enumRecvDiffFlags eFlags)
{

	if (eFlags & RECVDIFF_FLAG_ABS_VALUES)
	{
		int data = pktGetBitsPack(pak, TOK_GET_PRECISION_DEF(tpi[column].type, 1));
		if (structptr) TokenStoreSetInt_inline(tpi, &tpi[column], column, structptr, index, data, NULL, NULL);
	}
	else
	{
		int data = unpackDelta(pak);
		if (structptr)
		{
			data += TokenStoreGetInt_inline(tpi, &tpi[column], column, structptr, index, NULL);
			TokenStoreSetInt_inline(tpi, &tpi[column], column, structptr, index, data, NULL, NULL);
		}
	}

	return 1;
}

bool int_bitpack(ParseTable tpi[], int column, const void* structptr, int index, PackedStructStream *pack)
{
	int value = TokenStoreGetIntAuto(tpi, column, structptr, index, NULL);
	int def = (tpi[column].type & (TOK_FIXED_ARRAY | TOK_EARRAY))?0:tpi[column].param;
	if (value != def) {
		bsWriteBitsPack(pack->bs, TOK_GET_PRECISION_DEF(tpi[column].type, 1), value);
		return true;
	} else {
		return false;
	}
}

void int_unbitpack(ParseTable tpi[], int column, void *structptr, int index, PackedStructStream *pack)
{
	int value = bsReadBitsPack(pack->bs, TOK_GET_PRECISION_DEF(tpi[column].type, 1));
	TokenStoreSetIntAuto(tpi, column, structptr, index, value, NULL, NULL);
}

bool timestamp_bitpack(ParseTable tpi[], int column, const void* structptr, int index, PackedStructStream *pack)
{
	int value = TokenStoreGetIntAuto(tpi, column, structptr, index, NULL);
	int def = (tpi[column].type & (TOK_FIXED_ARRAY | TOK_EARRAY))?0:tpi[column].param;
	if (value != def) {
		// strip off the bottom 16 bits of precision (about a day's worth), and shift it
		value >>= 16;
		// write the remaining bits out
		bsWriteBits(pack->bs, (sizeof(int)*8)-16, value);
		return true;
	} else {
		return false;
	}
}

void timestamp_unbitpack(ParseTable tpi[], int column, void *structptr, int index, PackedStructStream *pack)
{
	int value = bsReadBits(pack->bs, 16);
	// shift it back
	value <<= 16;
	TokenStoreSetIntAuto(tpi, column, structptr, index, value, NULL, NULL);
}

TextParserResult int_writestring(ParseTable tpi[], int column, void* structptr, int index, char** estr, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *caller_fname, int line)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int value = TokenStoreGetInt_inline(tpi, &tpi[column], column, structptr, index, &ok);
	int format = TOK_GET_FORMAT_OPTIONS(tpi[column].format);

	if (RESULT_GOOD(ok) && format == TOK_FORMAT_FLAGS)
	{
		WriteStringIntFlags	(estr, value, (StaticDefineInt*)tpi[column].subtable, caller_fname, line, tpi, column, iWriteTextFlags);
		return ok;	
	}

	// most formats should only be printed with prettyprint
	if (iWriteTextFlags & WRITETEXTFLAG_PRETTYPRINT)
	{
		// these are reversible
		if (format >= TOK_FORMAT_UNREADABLE)
			format = 0;
	}
	if (iWriteTextFlags & WRITETEXTFLAG_WRITINGFORSQL)
	{
		format = TOK_FORMAT_UNSIGNED;
	}

	if (!RESULT_GOOD(ok)) return ok;

	switch (format)
	{
	case TOK_FORMAT_IP:
		estrAppend2_dbg_inline(estr,makeIpStr(value), caller_fname, line);
	xcase TOK_FORMAT_KBYTES:
		{
			const UnitSpec* spec;
			spec = usFindProperUnitSpec(kbyteSpec, value);

			if(1 != spec->unitBoundary) {
				estrConcatf_dbg(estr, caller_fname, line, "%6.2f %s", (float)value*spec->ooUnitBoundary,spec->unitName);
			} else {
				estrConcatf_dbg(estr, caller_fname, line, "%6d %s", value,spec->unitName);
			}
		}
	xcase TOK_FORMAT_DATESS2000:
		{		
			char dateString[1024];
			timeMakeDateStringFromSecondsSince2000_s(SAFESTR(dateString),value);
			estrAppend2_dbg_inline(estr, dateString, caller_fname, line);
		}
	xcase TOK_FORMAT_FRIENDLYDATE:
		if (value)
			printDateEstr_dbg(value, estr, caller_fname, line);
		else
			estrAppend2(estr, "N/A");
	xcase TOK_FORMAT_FRIENDLYSS2000:
		if (value)
			printDateEstr_dbg(timeMakeLocalTimeFromSecondsSince2000(value), estr, caller_fname, line);
		else
			estrAppend2(estr, "N/A");
	xcase TOK_FORMAT_PERCENT:
		estrConcatf_dbg(estr, caller_fname, line, "%02d%%", value);
	xcase TOK_FORMAT_UNSIGNED:
		estrConcatf_dbg(estr, caller_fname, line, "%u", value);
	xdefault:
		// we do reverse define lookup here if possible
		{
			const char* rev = NULL;
			if (tpi[column].subtable)
				rev = StaticDefineIntRevLookup_ForStructWriting(tpi[column].subtable, value, tpi, column, iWriteTextFlags);
			if (rev)	
				estrConcatf_dbg(estr, caller_fname, line, "%s", rev);
			else
				estrConcatf_dbg(estr, caller_fname, line, "%d", value);
		}
	};
	return ok;
}

TextParserResult int_readstring(ParseTable tpi[], int column, void* structptr, int index, const char* str, ReadStringFlags eFlags)
{
	int val;
	TextParserResult ok = PARSERESULT_SUCCESS;



	if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_FLAGS)
	{
		val = ReadStringIntFlags(str, tpi[column].subtable, &ok);
	}
	else if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_IP)
	{
		val = ipFromString(str);
		if (!val && stricmp(str, "0.0.0.0")!=0)
			SET_ERROR_RESULT(ok);
	}
	else if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_UNSIGNED)
	{
		if (sscanf(str, "%u", &val) != 1) SET_ERROR_RESULT(ok);
	}
	else
	{
		// do define lookup if possible
		if (tpi[column].subtable)
		{
			val = StaticDefine_FastStringToInt(tpi[column].subtable, str, INT_MIN);
			if (val != INT_MIN)
			{
				TokenStoreSetInt_inline(tpi, &tpi[column], column, structptr, index, val, &ok, NULL);
				return ok;
			}			
		}
		if (sscanf(str, "%d", &val) != 1) SET_ERROR_RESULT(ok);
	}
	if (RESULT_GOOD(ok)) TokenStoreSetInt_inline(tpi, &tpi[column], column, structptr, index, val, &ok, NULL);
	return ok;
}

TextParserResult int_tomulti(ParseTable tpi[], int column, void* structptr, int index, MultiVal* result, bool bDuplicateData, bool dummyOnNULL)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int val;
	if(!structptr && dummyOnNULL)
		MultiValSetDummyType(result, MULTI_INT);
	else
	{
		val = TokenStoreGetInt_inline(tpi, &tpi[column], column, structptr, index, &ok);
		if (RESULT_GOOD(ok)) MultiValSetInt(result, val);
	}
	return ok;
}

TextParserResult int_frommulti(ParseTable tpi[], int column, void* structptr, int index, const MultiVal* value)
{
	bool mvRes;
	int val = MultiValGetInt(value, &mvRes);
	TextParserResult ok = mvRes;
	if (RESULT_GOOD(ok)) TokenStoreSetInt_inline(tpi, &tpi[column], column, structptr, index, val, &ok, NULL);
	return ok;
}

void int_updatecrc(ParseTable tpi[], int column, void* structptr, int index)
{
	int value = TokenStoreGetInt_inline(tpi, &tpi[column], column, structptr, index, NULL);
	cryptAdler32Update_AutoEndian((U8*)&value, sizeof(int));
}

void timestamp_updatecrc(ParseTable tpi[], int column, void* structptr, int index)
{
	int value = TokenStoreGetInt_inline(tpi, &tpi[column], column, structptr, index, NULL);
	// strip off the bottom 16 bits of precision (about a day's worth), and shift it

	U16 val16 = ((U32)value) >> 16;
	cryptAdler32Update_AutoEndian((U8*)&val16, sizeof(U16));
}

void int_calcoffset(ParseTable tpi[], int column, size_t* size)
{
	MEM_ALIGN(*size, sizeof(int));
	tpi[column].storeoffset = *size;
	(*size) += sizeof(int);
}

void int_copyfield(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TokenStoreSetInt_inline(tpi, ptcc, column, dest, index, TokenStoreGetInt_inline(tpi, ptcc, column, src, index, NULL), NULL, NULL);
}

enumCopy2TpiResult int_copyfield2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest,  
	CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, char **ppResultString)
{
	TokenStoreSetInt_inline(dest_tpi, &dest_tpi[dest_column], dest_column, dest, dest_index, TokenStoreGetInt_inline(src_tpi, &src_tpi[src_column], src_column, src, src_index, NULL), NULL, NULL);
	return COPY2TPIRESULT_SUCCESS;
}


void int_endianswap(ParseTable tpi[], int column, void* structptr, int index)
{
	TokenStoreSetInt_inline(tpi, &tpi[column], column, structptr, index, endianSwapU32(TokenStoreGetInt_inline(tpi, &tpi[column], column, structptr, index, NULL)), NULL, NULL);
}

void int_interp(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 interpParam)
{
	int a = TokenStoreGetInt_inline(tpi, &tpi[column], column, structA, index, NULL);
	int b = TokenStoreGetInt_inline(tpi, &tpi[column], column, structB, index, NULL);
	int result;
	if (a == b)
		result = a;
	// If this integer is a color, interpolate it based on its RGBA values, not its integer value.
	else if (tpi[column].format & TOK_FORMAT_COLOR)
		result = lerpRGBAColors(a, b, interpParam); 
	else
		result = interpInt(interpParam, a, b);
	TokenStoreSetInt_inline(tpi, &tpi[column], column, destStruct, index, result, NULL, NULL);
}

void int_calcrate(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 deltaTime)
{
	F32 result = ((F32)TokenStoreGetInt_inline(tpi, &tpi[column], column, structB, index, NULL) - (F32)TokenStoreGetInt_inline(tpi, &tpi[column], column, structA, index, NULL)) / deltaTime;
	TokenStoreSetInt_inline(tpi, &tpi[column], column, destStruct, index, round(result), NULL, NULL);
}

void int_integrate(ParseTable tpi[], int column, void* valueStruct, void* rateStruct, void* destStruct, int index, F32 deltaTime)
{
	F32 result = (F32)TokenStoreGetInt_inline(tpi, &tpi[column], column, valueStruct, index, NULL) + deltaTime * (F32)TokenStoreGetInt_inline(tpi, &tpi[column], column, rateStruct, index, NULL);
	TokenStoreSetInt_inline(tpi, &tpi[column], column, destStruct, index, round(result), NULL, NULL);
}

void int_calccyclic(ParseTable tpi[], int column, void* valueStruct, void* ampStruct, void* freqStruct, void* cycleStruct, void* destStruct, int index, F32 fStartTime, F32 deltaTime)
{
	F32 freq = TokenStoreGetInt_inline(tpi, &tpi[column], column, freqStruct, index, NULL);
	F32 cycle = TokenStoreGetInt_inline(tpi, &tpi[column], column, cycleStruct, index, NULL);
	F32 fSinDiff = sinf( (fStartTime + deltaTime) * freq + cycle * TWOPI) - sinf( ( fStartTime * freq + cycle ) * TWOPI );
	F32 result = TokenStoreGetInt_inline(tpi, &tpi[column], column, valueStruct, index, NULL) + TokenStoreGetInt_inline(tpi, &tpi[column], column, ampStruct, index, NULL) * fSinDiff;
	TokenStoreSetInt_inline(tpi, &tpi[column], column, destStruct, index, round(result), NULL, NULL);
}

void int_applydynop(ParseTable tpi[], int column, void* dstStruct, void* srcStruct, int index, DynOpType optype, const F32* values, U8 uiValuesSpecd, U32* seed)
{
	switch (optype)
	{
	case DynOpType_Jitter:
	{
		int iToAdd = round(*values * randomF32Seeded(seed, RandType_BLORN));
		int result = TokenStoreGetInt_inline(tpi, &tpi[column], column, dstStruct, index, NULL) + iToAdd;
		TokenStoreSetInt_inline(tpi, &tpi[column], column, dstStruct, index, result, NULL, NULL);
	} break;
	case DynOpType_Inherit:
	{
		if (srcStruct) TokenStoreSetInt_inline(tpi, &tpi[column], column, dstStruct, index, TokenStoreGetInt_inline(tpi, &tpi[column], column, srcStruct, index, NULL), NULL, NULL);
	} break;
	case DynOpType_Add:
	{
		TokenStoreSetInt_inline(tpi, &tpi[column], column, dstStruct, index, TokenStoreGetInt_inline(tpi, &tpi[column], column, dstStruct, index, NULL) + round(*values), NULL, NULL);
	} break;
	case DynOpType_Multiply:
	{
		TokenStoreSetInt_inline(tpi, &tpi[column], column, dstStruct, index, round((F32)TokenStoreGetInt_inline(tpi, &tpi[column], column, dstStruct, index, NULL) * *values), NULL, NULL);
	} break;
	case DynOpType_Min:
	{
		TokenStoreSetInt_inline(tpi, &tpi[column], column, dstStruct, index, MIN(round(*values), TokenStoreGetInt_inline(tpi, &tpi[column], column, dstStruct, index, NULL)), NULL, NULL);
	} break;
	case DynOpType_Max:
	{
		TokenStoreSetInt_inline(tpi, &tpi[column], column, dstStruct, index, MAX(round(*values), TokenStoreGetInt_inline(tpi, &tpi[column], column, dstStruct, index, NULL)), NULL, NULL);
	} break;
	}
}

void int_queryCopyStruct(ParseTable *pTPI, int iColumn, int iIndex, int iOffsetIntoParentStruct, 
	CopyQueryFlags eQueryFlags, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, 
	StructTypeField iOptionFlagsToExclude, void *pUserData)
{
	StructCopyQueryResult_MarkBytes(!(eQueryFlags & COPYQUERYFLAG_YOU_ARE_FLAGGED_OUT), iOffsetIntoParentStruct + iIndex * sizeof(int), sizeof(int), pUserData);
}

void int_earrayCopy(ParseTable tpi[], int column, void* dest, void* src, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int **ppSrcEarray = TokenStoreGetEArrayInt_inline(tpi, &tpi[column], column, src, NULL);
	int **ppDestEarray = TokenStoreGetEArrayInt_inline(tpi, &tpi[column], column, dest, NULL);
	const char *pStructName = ParserGetTableName(tpi);
	ea32Copy_dbg(ppDestEarray, ppSrcEarray TPC_MEMDEBUG_PARAMS_EARRAY);
}


bool int64_initstruct(ParseTable tpi[], int column, void* structptr, int index)
{
	int def = (tpi[column].type & (TOK_FIXED_ARRAY | TOK_EARRAY))?0:tpi[column].param;
	TokenStoreSetInt64_inline(tpi, &tpi[column], column, structptr, index, def, NULL, NULL);
	return false;
}

int int64_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	char* nexttoken;
	S64 value;
	
	if (TOK_GET_FORMAT_OPTIONS(ptcc->format) == TOK_FORMAT_FLAGS)
	{
		U32 iVal = ReadTextIntFlags(tok, parseResult);
		TokenStoreSetInt64_inline(tpi, ptcc, column, structptr, index, iVal, parseResult, tok);
		return 0;
	}
	
	nexttoken = GetNextParameter_Int(tok, parseResult);
	if (!nexttoken) 
		return 0;

	if (nexttoken == TOK_SPECIAL_INT_RETURN)
	{
		value = TokenizerGetReadInt(tok);
	}
	else
	{
		value = GetInteger64(tok, nexttoken, parseResult);
	}

	TokenStoreSetInt64_inline(tpi, ptcc, column, structptr, index, value, parseResult, tok);
	return 0;
}

bool int64_writetext(FILE* out, ParseTable tpi[], int column, void* structptr, int index, bool showname, bool ignoreInherited, int level, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	S64 value = TokenStoreGetInt64_inline(tpi, &tpi[column], column, structptr, index, NULL);
	bool bCanSkip = !(tpi[column].type & TOK_REQUIRED) && (value == tpi[column].param) && !SHOULD_WRITE_DEFAULT(iWriteTextFlags, tpi, column, structptr);
	if (showname && bCanSkip) return false; // if default value, not necessary to write field
	if (showname) WriteNString(out, tpi[column].name,ParseTableColumnNameLen(tpi,column), level+1, 0);
	WriteNString(out, " ",1, 0, 0);


	if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_FLAGS)
	{
		WriteTextIntFlags(out, value, tpi[column].subtable, showname);
	}
	else
	{
		WriteInt64(out, value, 0, showname, tpi[column].subtable);
	}

	return !bCanSkip;
}

bool int64_writejsonfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	S64 value = TokenStoreGetInt64_inline(tpi, &tpi[column], column, structptr, index, NULL);

	fprintf(out, "\"%I64d\"", value);
	
	return true;
}


TextParserResult int64_writebin(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	S64 value = TokenStoreGetInt64_inline(tpi, &tpi[column], column, structptr, index, &ok);
	SET_MIN_RESULT(ok, SimpleBufWriteU64(value, file));
	*datasum += sizeof(S64);
	return ok;
}

TextParserResult int64_readbin(SimpleBufHandle file,  FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	S64 value = 0;
	if (!SimpleBufReadU64((U64*)&value, file))
		SET_INVALID_RESULT(ok);
	*datasum += sizeof(S64);
	TokenStoreSetInt64_inline(tpi, &tpi[column], column, structptr, index, value, &ok, NULL);
	return ok;
}

void int64_writehdiff(char** estr, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, char *prefix, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	S64 val = TokenStoreGetInt64_inline(tpi, &tpi[column], column, newstruct, index, NULL);
	WriteSetValueStringForTextDiff_ThroughQuote(estr, prefix, (int)strlen(prefix), caller_fname, line);
	estrConcatf_dbg(estr, caller_fname, line, "%"FORM_LL"d\"\n",val);
}

void int64_writebdiff(StructDiff *diff, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, ObjectPath *parentPath, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool invertExcludeFlags, const char *caller_fname, int line)
{
	StructDiffOp *op = eaTail(&diff->ppOps);
	if (op) {
		op->pOperand = MultiValCreate();
		MultiValSetInt(op->pOperand, TokenStoreGetInt64_inline(tpi, &tpi[column], column, newstruct, index, NULL));
	}
}

bool int64_senddiff(Packet* pak, ParseTable tpi[], int column, const void* oldstruct, const void* newstruct, int index, enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB)
{
	if (/*oldstruct &&*/ (eFlags & SENDDIFF_FLAG_COMPAREBEFORESENDING) && int64_compare(tpi,column, oldstruct, newstruct, index, 0, 0, 0) == 0)
	{
		return false;
	}
	

	if (!(eFlags & SENDDIFF_FLAG_ALLOWDIFFS))
	{
		S64 val = TokenStoreGetInt64_inline(tpi, &tpi[column], column, newstruct, index, NULL);
		U32 low = val & 0xffffffff;
		U32 high = val >> 32;
		pktSendBitsPack(pak, TOK_GET_PRECISION_DEF(tpi[column].type, 1), low);
		pktSendBitsPack(pak, 1, high);
	}
	else
	{
		S64 oldval = TokenStoreGetInt64_inline(tpi, &tpi[column], column, oldstruct, index, NULL);
		S64 newval = TokenStoreGetInt64_inline(tpi, &tpi[column], column, newstruct, index, NULL);
		S64 delta = newval - oldval;
		U32 low = delta & 0xffffffff;
		U32 high = delta >> 32;
		packDelta(pak, low);
		packDelta(pak, high);
	}
	return true;
}

bool int64_recvdiff(Packet* pak, ParseTable tpi[], int column, void* structptr, int index, enumRecvDiffFlags eFlags)
{

	if (eFlags & RECVDIFF_FLAG_ABS_VALUES)
	{
		U32 low = pktGetBitsPack(pak, TOK_GET_PRECISION_DEF(tpi[column].type, 1));
		U32 high = pktGetBitsPack(pak, 1);
		S64 data = ((U64)high << 32) | ((U64)low & 0xffffffff);
		if (structptr) TokenStoreSetInt64_inline(tpi, &tpi[column], column, structptr, index, data, NULL, NULL);
	}
	else
	{
		U32 low = unpackDelta(pak);
		U32 high = unpackDelta(pak);
		S64 data = (((U64)high << 32) | ((U64)low & 0xffffffff));
		if (structptr)
		{
			data += TokenStoreGetInt64_inline(tpi, &tpi[column], column, structptr, index, NULL);
			TokenStoreSetInt64_inline(tpi, &tpi[column], column, structptr, index, data, NULL, NULL);
		}
	}

	return 1;
}

bool int64_bitpack(ParseTable tpi[], int column, const void *structptr, int index, PackedStructStream *pack)
{
	S64 value = TokenStoreGetInt64_inline(tpi, &tpi[column], column, structptr, index, NULL);
	S64 def = (tpi[column].type & (TOK_FIXED_ARRAY | TOK_EARRAY))?0:tpi[column].param;
	if (value != def) {
		U32 low = value & 0xffffffff;
		U32 high = value >> 32LL;
		bsWriteBitsPack(pack->bs, TOK_GET_PRECISION_DEF(tpi[column].type, 1), low);
		bsWriteBitsPack(pack->bs, 1, high);
		return true;
	} else {
		return false;
	}
}

void int64_unbitpack(ParseTable tpi[], int column, void *structptr, int index, PackedStructStream *pack)
{
	U32 low = bsReadBitsPack(pack->bs, TOK_GET_PRECISION_DEF(tpi[column].type, 1));
	U32 high = bsReadBitsPack(pack->bs, 1);
	S64 value = (low) | ((U64)high << 32LL);
	TokenStoreSetInt64_inline(tpi, &tpi[column], column, structptr, index, value, NULL, NULL);
}


TextParserResult int64_writestring(ParseTable tpi[], int column, void* structptr, int index, char** estr, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *caller_fname, int line)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	S64 val = TokenStoreGetInt64_inline(tpi, &tpi[column], column, structptr, index, &ok);
	if (RESULT_GOOD(ok)) 
	{
		if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_FLAGS)
		{
			WriteStringIntFlags(estr, val, (StaticDefineInt*)tpi[column].subtable, caller_fname, line, tpi, column, iWriteTextFlags);
				
		}
		else
		{
			estrConcatf_dbg(estr, caller_fname, line, "%"FORM_LL"d", val);
		}
	}

	return ok;
}

TextParserResult int64_readstring(ParseTable tpi[], int column, void* structptr, int index, const char* str, ReadStringFlags eFlags)
{
	S64 val;
	TextParserResult ok = PARSERESULT_SUCCESS;


	if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_FLAGS)
	{
		val = ReadStringIntFlags(str, tpi[column].subtable, &ok);
	}
	else
	{
		if (sscanf(str, "%"FORM_LL"d", &val) != 1) SET_ERROR_RESULT(ok);
	}
	if (RESULT_GOOD(ok)) TokenStoreSetInt64_inline(tpi, &tpi[column], column, structptr, index, val, &ok, NULL);
	return ok;
}

TextParserResult int64_tomulti(ParseTable tpi[], int column, void* structptr, int index, MultiVal* result, bool bDuplicateData, bool dummyOnNULL)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	S64 val;

	if(!structptr && dummyOnNULL)
		MultiValSetDummyType(result, MULTI_INT);
	else
	{
		val = TokenStoreGetInt64_inline(tpi, &tpi[column], column, structptr, index, &ok);
		if (RESULT_GOOD(ok)) MultiValSetInt(result, val);
	}
	return ok;
}

TextParserResult int64_frommulti(ParseTable tpi[], int column, void* structptr, int index, const MultiVal* value)
{
	bool mvRes;
	S64 val = MultiValGetInt(value, &mvRes);
	TextParserResult ok = mvRes;
	if (RESULT_GOOD(ok)) TokenStoreSetInt64_inline(tpi, &tpi[column], column, structptr, index, val, &ok, NULL);
	return ok;
}

void int64_updatecrc(ParseTable tpi[], int column, void* structptr, int index)
{
	S64 value = TokenStoreGetInt64_inline(tpi, &tpi[column], column, structptr, index, NULL);
	cryptAdler32Update_AutoEndian((U8*)&value, sizeof(S64));
}

int int64_compare(ParseTable tpi[], int column, const void* lhs, const void* rhs, int index, enumCompareFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	S64 left;
	S64 right = TokenStoreGetInt64_inline(tpi, &tpi[column], column, rhs, index, NULL);

	if (lhs)
		left = TokenStoreGetInt64_inline(tpi, &tpi[column], column, lhs, index, NULL);
	else
		left = (tpi[column].type & (TOK_FIXED_ARRAY | TOK_EARRAY))?0:tpi[column].param;

	return (left < right)? -1: (left > right)? 1: 0;
}

void int64_calcoffset(ParseTable tpi[], int column, size_t* size)
{
	MEM_ALIGN(*size, sizeof(S64));
	tpi[column].storeoffset = *size;
	(*size) += sizeof(S64);
}

void int64_copyfield(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TokenStoreSetInt64_inline(tpi, ptcc, column, dest, index, TokenStoreGetInt64_inline(tpi, ptcc, column, src, index, NULL), NULL, NULL);
}

enumCopy2TpiResult int64_copyfield2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest,  
	CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, char **ppResultString)
{
	TokenStoreSetInt64_inline(dest_tpi, &dest_tpi[dest_column], dest_column, dest, dest_index, TokenStoreGetInt64_inline(src_tpi, &src_tpi[src_column], src_column, src, src_index, NULL), NULL, NULL);
	return COPY2TPIRESULT_SUCCESS;
}


void int64_endianswap(ParseTable tpi[], int column, void* structptr, int index)
{
	TokenStoreSetInt64_inline(tpi, &tpi[column], column, structptr, index, endianSwapU64(TokenStoreGetInt64_inline(tpi, &tpi[column], column, structptr, index, NULL)), NULL, NULL);
}

void int64_interp(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 interpParam)
{
	S64 result = interpS64(interpParam, TokenStoreGetInt64_inline(tpi, &tpi[column], column, structA, index, NULL), TokenStoreGetInt64_inline(tpi, &tpi[column], column, structB, index, NULL));
	TokenStoreSetInt64_inline(tpi, &tpi[column], column, destStruct, index, result, NULL, NULL);
}

void int64_calcrate(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 deltaTime)
{
	F64 result = ((F64)TokenStoreGetInt64_inline(tpi, &tpi[column], column, structB, index, NULL) - (F64)TokenStoreGetInt64_inline(tpi, &tpi[column], column, structA, index, NULL)) / deltaTime;
	TokenStoreSetInt64_inline(tpi, &tpi[column], column, destStruct, index, round64(result), NULL, NULL);
}

void int64_integrate(ParseTable tpi[], int column, void* valueStruct, void* rateStruct, void* destStruct, int index, F32 deltaTime)
{
	F64 result = (F64)TokenStoreGetInt64_inline(tpi, &tpi[column], column, valueStruct, index, NULL) + deltaTime * (F64)TokenStoreGetInt64_inline(tpi, &tpi[column], column, rateStruct, index, NULL);
	TokenStoreSetInt64_inline(tpi, &tpi[column], column, destStruct, index, round64(result), NULL, NULL);
}

void int64_calccyclic(ParseTable tpi[], int column, void* valueStruct, void* ampStruct, void* freqStruct, void* cycleStruct, void* destStruct, int index, F32 fStartTime, F32 deltaTime)
{
	F64 freq = TokenStoreGetInt64_inline(tpi, &tpi[column], column, freqStruct, index, NULL);
	F64 cycle = TokenStoreGetInt64_inline(tpi, &tpi[column], column, cycleStruct, index, NULL);
	F64 fSinDiff = sinf( (fStartTime + deltaTime) * freq + cycle * TWOPI) - sinf( ( fStartTime * freq + cycle ) * TWOPI );
	F64 result = TokenStoreGetInt64_inline(tpi, &tpi[column], column, valueStruct, index, NULL) + TokenStoreGetInt64_inline(tpi, &tpi[column], column, ampStruct, index, NULL) * fSinDiff;
	TokenStoreSetInt64_inline(tpi, &tpi[column], column, destStruct, index, round64(result), NULL, NULL);
}

void int64_applydynop(ParseTable tpi[], int column, void* dstStruct, void* srcStruct, int index, DynOpType optype, const F32* values, U8 uiValuesSpecd, U32* seed)
{
	switch (optype)
	{
	case DynOpType_Jitter:
	{
		S64 iToAdd = round64((F64)*values * randomF32Seeded(seed, RandType_BLORN));
		S64 result = TokenStoreGetInt64_inline(tpi, &tpi[column], column, dstStruct, index, NULL) + iToAdd;
		TokenStoreSetInt64_inline(tpi, &tpi[column], column, dstStruct, index, result, NULL, NULL);
	} break;
	case DynOpType_Inherit:
	{
		if (srcStruct) TokenStoreSetInt64_inline(tpi, &tpi[column], column, dstStruct, index, TokenStoreGetInt64_inline(tpi, &tpi[column], column, srcStruct, index, NULL), NULL, NULL);
	} break;
	case DynOpType_Add:
	{
		TokenStoreSetInt64_inline(tpi, &tpi[column], column, dstStruct, index, TokenStoreGetInt64_inline(tpi, &tpi[column], column, dstStruct, index, NULL) + round(*values), NULL, NULL);
	} break;
	case DynOpType_Multiply:
	{
		TokenStoreSetInt64_inline(tpi, &tpi[column], column, dstStruct, index, round64((F64)TokenStoreGetInt64_inline(tpi, &tpi[column], column, dstStruct, index, NULL) * *values), NULL, NULL);
	} break;
	case DynOpType_Min:
	{
		TokenStoreSetInt64_inline(tpi, &tpi[column], column, dstStruct, index, MIN(round64(*values), TokenStoreGetInt64_inline(tpi, &tpi[column], column, dstStruct, index, NULL)), NULL, NULL);
	} break;
	case DynOpType_Max:
	{
		TokenStoreSetInt64_inline(tpi, &tpi[column], column, dstStruct, index, MAX(round64(*values), TokenStoreGetInt64_inline(tpi, &tpi[column], column, dstStruct, index, NULL)), NULL, NULL);
	} break;
	}
}

void int64_queryCopyStruct(ParseTable *pTPI, int iColumn, int iIndex, int iOffsetIntoParentStruct, 
	CopyQueryFlags eQueryFlags, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, 
	StructTypeField iOptionFlagsToExclude, void *pUserData)
{
	StructCopyQueryResult_MarkBytes(!(eQueryFlags & COPYQUERYFLAG_YOU_ARE_FLAGGED_OUT), iOffsetIntoParentStruct  + iIndex * sizeof(U64), sizeof(U64), pUserData);
}

ParseInfoFieldUsage bit_interpretfield(ParseTable tpi[], int column, ParseInfoField field)

{
	switch (field)
	{
		case ParamField:
			return BitOffset;
		case SubtableField:
			return StaticDefineList;
	}
	return NoFieldUsage;
}

bool bit_initstruct(ParseTable tpi[], int column, void* structptr, int index)
{
	if (tpi[column].type & TOK_SPECIAL_DEFAULT)
	{
		char *pDefaultString;
		if (GetStringFromTPIFormatString(&tpi[column], "SPECIAL_DEFAULT", &pDefaultString))
		{
			TokenStoreSetBit_inline(tpi, &tpi[column], column, structptr, index, atoi(pDefaultString), NULL, NULL);
		}
	}

	return false;
}

int bit_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	int value;
	char* nexttoken = NULL;

	if (TOK_GET_FORMAT_OPTIONS(ptcc->format) == TOK_FORMAT_FLAGS)
	{
		U32 iVal = ReadTextIntFlags(tok, parseResult);
		TokenStoreSetBit_inline(tpi, ptcc, column, structptr, index, iVal, parseResult, tok);
		return 0;
	}

	nexttoken = GetNextParameter_Int(tok, parseResult);
	if (!nexttoken) 
		return 0;

	if (nexttoken == TOK_SPECIAL_INT_RETURN)
	{
		value = TokenizerGetReadInt(tok);
	}
	else
	{
		value = GetInteger(tok, nexttoken, parseResult);
	}

	TokenStoreSetBit_inline(tpi, ptcc, column, structptr, index, value, parseResult, tok);
	return 0;
}

TextParserResult bit_writebin(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	U32 value = TokenStoreGetBit_inline(tpi, &tpi[column], column, structptr, index, &ok);
	SET_MIN_RESULT(ok, SimpleBufWriteU32(value, file));
	*datasum += sizeof(int);
	return ok;
}

TextParserResult bit_readbin(SimpleBufHandle file, FileList *pFileList,  ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	U32 value = 0;
	if (!SimpleBufReadU32((U32*)&value, file))
		SET_INVALID_RESULT(ok);
	*datasum += sizeof(int);
	TokenStoreSetBit_inline(tpi, &tpi[column], column, structptr, index, value, &ok, NULL);
	return ok;
}

void bit_writehdiff(char** estr, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, char *prefix, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	U32 val = TokenStoreGetBit_inline(tpi, &tpi[column], column, newstruct, index, NULL);
	char intBuff[16];
	int iIntLen = fastIntToString_inline(val, intBuff);
	WriteSetValueStringForTextDiff(estr, prefix, (int)strlen(prefix), intBuff + 16 - iIntLen, iIntLen, caller_fname, line);
}

void bit_writebdiff(StructDiff *diff, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, ObjectPath *parentPath, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool invertExcludeFlags, const char *caller_fname, int line)
{
	StructDiffOp *op = eaTail(&diff->ppOps);
	if (op) {
		op->pOperand = MultiValCreate();
		MultiValSetInt(op->pOperand,TokenStoreGetBit_inline(tpi, &tpi[column], column, newstruct, index, NULL));
	}
}

bool bit_senddiff(Packet* pak, ParseTable tpi[], int column, const void* oldstruct, const void* newstruct, int index, enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB)
{

	int iNumBits;

	if (/*oldstruct &&*/ (eFlags & SENDDIFF_FLAG_COMPAREBEFORESENDING) && bit_compare(tpi,column, oldstruct, newstruct, index, 0, 0, 0) == 0)
	{
		return false;
	}

	iNumBits = TextParserBitField_GetBitCount(tpi, column);

	pktSendBits(pak, iNumBits, TokenStoreGetBit_inline(tpi, &tpi[column], column, newstruct, index, NULL));
	return true;
}

bool bit_recvdiff(Packet* pak, ParseTable tpi[], int column, void* structptr, int index, enumRecvDiffFlags eFlags)
{
	U32 data;
	int iNumBits = TextParserBitField_GetBitCount(tpi, column);

	data = pktGetBits(pak, iNumBits);
	if (structptr) TokenStoreSetBit_inline(tpi, &tpi[column], column, structptr, index, data, NULL, NULL);

	return 1;
}

bool bit_bitpack(ParseTable tpi[], int column, const void *structptr, int index, PackedStructStream *pack)
{
	U32 value = TokenStoreGetBit_inline(tpi, &tpi[column], column, structptr, index, NULL);
	U32 defVal = 0;

	if (tpi[column].type & TOK_SPECIAL_DEFAULT)
	{
		char *pDefaultString;
		if (GetStringFromTPIFormatString(&tpi[column], "SPECIAL_DEFAULT", &pDefaultString))
		{
			defVal = atoi(pDefaultString);
		}
	}			

	if (value != defVal) 
	{
		bsWriteBitsPack(pack->bs, TextParserBitField_GetBitCount(tpi, column), value);
		return true;
	} 
	else 
	{
		return false;
	}
}

void bit_unbitpack(ParseTable tpi[], int column, void *structptr, int index, PackedStructStream *pack)
{
	U32 value = bsReadBitsPack(pack->bs, TextParserBitField_GetBitCount(tpi, column));
	TokenStoreSetBit_inline(tpi, &tpi[column], column, structptr, index, value, NULL, NULL);
}

TextParserResult bit_writestring(ParseTable tpi[], int column, void* structptr, int index, char** estr, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *caller_fname, int line)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int val = TokenStoreGetBit_inline(tpi, &tpi[column], column, structptr, index, &ok);
	if (RESULT_GOOD(ok))
	{
		if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_FLAGS)
		{
			WriteStringIntFlags(estr, val, (StaticDefineInt*)tpi[column].subtable, caller_fname, line, tpi, column, iWriteTextFlags);
		}
		else
		{
			const char* rev = NULL;
			if (tpi[column].subtable)
				rev = StaticDefineIntRevLookup_ForStructWriting(tpi[column].subtable, val, tpi, column, iWriteTextFlags);
			if (rev)
				estrConcatf_dbg(estr, caller_fname, line, "%s", rev);
			else
				estrConcatf_dbg(estr, caller_fname, line, "%u", val);
		}
	}
	return ok;
}

TextParserResult bit_readstring(ParseTable tpi[], int column, void* structptr, int index, const char* str, ReadStringFlags eFlags)
{
	U32 val = 0;
	TextParserResult ok = PARSERESULT_SUCCESS;

	if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_FLAGS)
	{
		val = ReadStringIntFlags(str, tpi[column].subtable, &ok);
	}
	else
	{
		// do define lookup if possible
		if (tpi[column].subtable)
		{
			val = StaticDefine_FastStringToInt(tpi[column].subtable, str, INT_MIN);
			if (val != INT_MIN)
			{
				TokenStoreSetBit_inline(tpi, &tpi[column], column, structptr, index, val, &ok, NULL);
				return ok;
			}
		}
		if (sscanf(str, "%u", &val) != 1) SET_ERROR_RESULT(ok);
	}
	if (RESULT_GOOD(ok)) TokenStoreSetBit_inline(tpi, &tpi[column], column, structptr, index, val, &ok, NULL);
	return ok;
}

TextParserResult bit_tomulti(ParseTable tpi[], int column, void* structptr, int index, MultiVal* result, bool bDuplicateData, bool dummyOnNULL)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	U32 val;
	if(!structptr && dummyOnNULL)
		MultiValSetDummyType(result, MULTI_INT);
	else
	{
		val = TokenStoreGetBit_inline(tpi, &tpi[column], column, structptr, index, &ok);
		if (RESULT_GOOD(ok)) MultiValSetInt(result, val);
	}
	return ok;
}

TextParserResult bit_frommulti(ParseTable tpi[], int column, void* structptr, int index, const MultiVal* value)
{
	bool mvRes;
	U32 val = MultiValGetInt(value, &mvRes);
	TextParserResult ok = mvRes;
	if (RESULT_GOOD(ok)) TokenStoreSetBit_inline(tpi, &tpi[column], column, structptr, index, val, &ok, NULL);
	return ok;
}

void bit_updatecrc(ParseTable tpi[], int column, void* structptr, int index)
{
	U32 value = TokenStoreGetBit_inline(tpi, &tpi[column], column, structptr, index, NULL);
	cryptAdler32Update_AutoEndian((U8*)&value, sizeof(U32));
}

int bit_compare(ParseTable tpi[], int column, const void* lhs, const void* rhs, int index, enumCompareFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	U32 left;
	U32 right = TokenStoreGetBit_inline(tpi, &tpi[column], column, rhs, index, NULL);

	if (lhs)
		left = TokenStoreGetBit_inline(tpi, &tpi[column], column, lhs, index, NULL);
	else if (!(tpi[column].type & TOK_SPECIAL_DEFAULT))
		left = 0;
	else return -1;

	return left - right;
}

void bit_calcoffset(ParseTable tpi[], int column, size_t* size)
{
	tpi[column].storeoffset = *size;

	//we may already have gotten the param set if this is the sendStructSafe case... if so, use it
	if (tpi[column].param)
	{
		*size += 4;
	}
	else
	{
		tpi[column].param = 32 << 16;
		(*size) += sizeof(U32);
	}
}

void bit_copyfield(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TokenStoreSetBit_inline(tpi, ptcc, column, dest, index, TokenStoreGetBit_inline(tpi, ptcc, column, src, index, NULL), NULL, NULL);
}

enumCopy2TpiResult bit_copyfield2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest,  
	CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, char **ppResultString)
{
	TokenStoreSetBit_inline(dest_tpi, &dest_tpi[dest_column], dest_column, dest, dest_index, TokenStoreGetBit_inline(src_tpi, &src_tpi[src_column], src_column, src, src_index, NULL), NULL, NULL);
	return COPY2TPIRESULT_SUCCESS;
}

void bit_queryCopyStruct(ParseTable *pTPI, int iColumn, int iIndex, int iOffsetIntoParentStruct, 
	CopyQueryFlags eQueryFlags, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, 
	StructTypeField iOptionFlagsToExclude, void *pUserData)
{
	int iWord = TextParserBitField_GetWordNum(pTPI, iColumn);
	int iBit = TextParserBitField_GetBitNum(pTPI, iColumn);
	int iCount  = TextParserBitField_GetBitCount(pTPI, iColumn);

	//fixed arrays of bits don't exist
	assert(!(eQueryFlags & COPYQUERYFLAG_IN_FIXED_ARRAY));

	if (iCount == 32)
	{
		StructCopyQueryResult_MarkBytes(!(eQueryFlags & COPYQUERYFLAG_YOU_ARE_FLAGGED_OUT), iOffsetIntoParentStruct, 4, pUserData);
	}
	else
	{
		U32 iTestWord = ((1 << iCount) - 1) << iBit;
		U8 *pTestU8s = (U8*)&iTestWord;
		int iTempByte;
		int iTempBit;

		for (iTempByte = 0; iTempByte < 4; iTempByte++)
		{
			for (iTempBit = 0; iTempBit < 8; iTempBit++)
			{
				if (pTestU8s[iTempByte] & ( 1 << iTempBit))
				{
					StructCopyQueryResult_MarkBit(!(eQueryFlags & COPYQUERYFLAG_YOU_ARE_FLAGGED_OUT), iOffsetIntoParentStruct + iTempByte, iTempBit, pUserData);
				}
			}
		}
	}
}


bool float_initstruct(ParseTable tpi[], int column, void* structptr, int index)
{
	F32 def = (tpi[column].type & (TOK_FIXED_ARRAY | TOK_EARRAY))?0:GET_FLOAT_FROM_INTPTR(tpi[column].param);
	TokenStoreSetF32_inline(tpi, &tpi[column], column, structptr, index, def, NULL, NULL);
	return false;
}

int float_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	float value;

	char* nexttoken = GetNextParameter(tok, parseResult);
	if (!nexttoken) 
		return 0;

	value = GetFloat(tok, nexttoken, parseResult);

	TokenStoreSetF32_inline(tpi, ptcc, column, structptr, index, value, parseResult, tok);
	return 0;
}

bool float_writetext(FILE* out, ParseTable tpi[], int column, void* structptr, int index, bool showname, bool ignoreInherited, int level, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	F32 value = TokenStoreGetF32_inline(tpi, &tpi[column], column, structptr, index, NULL);
	bool bCanSkip = !(tpi[column].type & TOK_REQUIRED) && (value == GET_FLOAT_FROM_INTPTR(tpi[column].param)) && !SHOULD_WRITE_DEFAULT(iWriteTextFlags, tpi, column, structptr);
	if (showname && bCanSkip) return false; // if default value, not necessary to write field
	if (showname) WriteNString(out, tpi[column].name,ParseTableColumnNameLen(tpi,column), level+1, 0);
	WriteNString(out, " ",1, 0, 0);
	if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_PERCENT)
	{
		char str[20];
		int len = sprintf(str, "%02.0f%%", value*100);
		WriteNString(out, str, len, 0, showname);
	}
	else if (GetBoolFromTPIFormatString(&tpi[column], "HIGH_PREC_FLOAT"))
	{
		WriteFloatHighPrec(out, value, 0, showname);
	}
	else
	{
		WriteFloat(out, value, 0, showname, tpi[column].subtable);
	}

	return !bCanSkip;
}

TextParserResult float_writebin(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	F32 value = TokenStoreGetF32_inline(tpi, &tpi[column], column, structptr, index, &ok);
	SET_MIN_RESULT(ok, SimpleBufWriteF32(value, file));
	*datasum += sizeof(F32);
	return ok;
}

TextParserResult float_readbin(SimpleBufHandle file,  FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	F32 value = 0.0;
	if (!SimpleBufReadF32((F32*)&value, file))
		SET_INVALID_RESULT(ok);
	*datasum += sizeof(F32);
	TokenStoreSetF32_inline(tpi, &tpi[column], column, structptr, index, value, &ok, NULL);
	return ok;
}

void float_writehdiff(char** estr, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, char *prefix, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	F32 val = TokenStoreGetF32_inline(tpi, &tpi[column], column, newstruct, index, NULL);
	char temp[32];
	int iDigits;
	iDigits = fastFloatToString_inline(val, SAFESTR(temp));
	WriteSetValueStringForTextDiff(estr, prefix, (int)strlen(prefix), temp, iDigits, caller_fname, line);
}

void float_writebdiff(StructDiff *diff, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, ObjectPath *parentPath, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool invertExcludeFlags, const char *caller_fname, int line)
{
	StructDiffOp *op = eaTail(&diff->ppOps);
	if (op) {
		op->pOperand = MultiValCreate();
		MultiValSetFloat(op->pOperand,TokenStoreGetF32_inline(tpi, &tpi[column], column, newstruct, index, NULL));
	}
}

bool float_senddiff(Packet* pak, ParseTable tpi[], int column, const void* oldstruct, const void* newstruct, int index, enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB)
{
	int acc = TOK_GET_FLOAT_ROUNDING(tpi[column].type);

	if (/*oldstruct &&*/ (eFlags & SENDDIFF_FLAG_COMPAREBEFORESENDING) && float_compare(tpi,column, oldstruct, newstruct, index, 0, 0, 0) == 0)
	{
		return false;
	}
	

	if (!acc) // not using low accuracy packing
	{
		pktSendF32(pak, TokenStoreGetF32_inline(tpi, &tpi[column], column, newstruct, index, NULL));
	}
	else if (!(eFlags & SENDDIFF_FLAG_ALLOWDIFFS))
	{
		pktSendBitsPack(pak, ParserFloatMinBits(acc), ParserPackFloat(TokenStoreGetF32_inline(tpi, &tpi[column], column, newstruct, index, NULL), acc));
	}
	else
	{
		int df = ParserPackFloat(TokenStoreGetF32_inline(tpi, &tpi[column], column, newstruct, index, NULL), acc) - 
			ParserPackFloat(TokenStoreGetF32_inline(tpi, &tpi[column], column, oldstruct, index, NULL), acc);
		packDelta(pak, df);
		if (pktIsDebug(pak))
			pktSendBitsPack(pak, 1, ParserPackFloat(TokenStoreGetF32_inline(tpi, &tpi[column], column, newstruct, index, NULL), acc));
	}
	return true;
}

bool float_recvdiff(Packet* pak, ParseTable tpi[], int column, void* structptr, int index, enumRecvDiffFlags eFlags)
{
	int acc = TOK_GET_FLOAT_ROUNDING(tpi[column].type);

	if (!acc) // not using low accuracy packing
	{
		F32 data = pktGetF32(pak);
		if (structptr) TokenStoreSetF32_inline(tpi, &tpi[column], column, structptr, index, data, NULL, NULL);
	}
	else if (eFlags & RECVDIFF_FLAG_ABS_VALUES)
	{
		F32 data = ParserUnpackFloat(pktGetBitsPack(pak, ParserFloatMinBits(acc)), acc);
		if (structptr) TokenStoreSetF32_inline(tpi, &tpi[column], column, structptr, index, data, NULL, NULL);
	}
	else
	{
		F32 data;
		int df = unpackDelta(pak);
		if (structptr)
		{
			df += ParserPackFloat(TokenStoreGetF32_inline(tpi, &tpi[column], column, structptr, index, NULL), acc);
			data = ParserUnpackFloat(df, acc);
			TokenStoreSetF32_inline(tpi, &tpi[column], column, structptr, index, data, NULL, NULL);
		}
		if (pktIsDebug(pak))
		{
			extern int g_bAssertOnBitStreamError;
			int debugpacked = pktGetBitsPack(pak, 1);
			if (g_bAssertOnBitStreamError && structptr)
				assert(debugpacked == df);
		}
	}

	return 1;
}

bool float_bitpack(ParseTable tpi[], int column, const void *structptr, int index, PackedStructStream *pack)
{
	F32 value = TokenStoreGetF32_inline(tpi, &tpi[column], column, structptr, index, NULL);
	F32 def = (tpi[column].type & (TOK_FIXED_ARRAY | TOK_EARRAY))?0:GET_FLOAT_FROM_INTPTR(tpi[column].param);
	if (!value)
		value = 0; // Replace -0 with 0
	if (value != def) {
		int acc = TOK_GET_FLOAT_ROUNDING(tpi[column].type);
		if (!acc) // not using low accuracy packing
		{
			bsWriteF32(pack->bs, value);
		}
		else
		{
			bsWriteBitsPack(pack->bs, ParserFloatMinBits(acc), ParserPackFloat(value, acc));
		}
		return true;
	} else {
		return false;
	}
}

void float_unbitpack(ParseTable tpi[], int column, void *structptr, int index, PackedStructStream *pack)
{
	int acc = TOK_GET_FLOAT_ROUNDING(tpi[column].type);
	F32 value;
	if (!acc) // not using low accuracy packing
	{
		value = bsReadF32(pack->bs);
	}
	else
	{
		value = ParserUnpackFloat(bsReadBitsPack(pack->bs, ParserFloatMinBits(acc)), acc);
	}
	TokenStoreSetF32_inline(tpi, &tpi[column], column, structptr, index, value, NULL, NULL);
}


TextParserResult float_writestring(ParseTable tpi[], int column, void* structptr, int index, char** estr, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *caller_fname, int line)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	F32 val = TokenStoreGetF32_inline(tpi, &tpi[column], column, structptr, index, &ok);
	int oldlen = estrLength(estr);
	int len;
	if (RESULT_GOOD(ok))
	{	
		estrConcatf_dbg(estr, caller_fname, line, "%g", val);
		// Truncate annoying trailing 0s
		len = estrLength(estr);
		if (strchr(*estr + oldlen, '.'))
			while ((*estr)[len-1]=='0' && (*estr)[len-2]!='.')
				estrSetSize_dbg(estr, --len, caller_fname, line);
	}
	return ok;
}

TextParserResult float_readstring(ParseTable tpi[], int column, void* structptr, int index, const char* str, ReadStringFlags eFlags)
{
	F32 val;
	TextParserResult ok = PARSERESULT_SUCCESS;
	if (sscanf(str, "%g", &val) != 1) SET_ERROR_RESULT(ok);
	if (RESULT_GOOD(ok)) TokenStoreSetF32_inline(tpi, &tpi[column], column, structptr, index, val, &ok, NULL);
	return ok;
}

TextParserResult float_tomulti(ParseTable tpi[], int column, void* structptr, int index, MultiVal* result, bool bDuplicateData, bool dummyOnNULL)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	F32 val;
	if(!structptr && dummyOnNULL)
		MultiValSetDummyType(result, MULTI_FLOAT);
	else
	{
		val = TokenStoreGetF32_inline(tpi, &tpi[column], column, structptr, index, &ok);
		if (RESULT_GOOD(ok)) MultiValSetFloat(result, val);
	}
	return ok;
}

TextParserResult float_frommulti(ParseTable tpi[], int column, void* structptr, int index, const MultiVal* value)
{
	bool mvRes;
	F32 val = MultiValGetFloat(value, &mvRes);
	TextParserResult ok = mvRes;
	if (RESULT_GOOD(ok)) TokenStoreSetF32_inline(tpi, &tpi[column], column, structptr, index, val, &ok, NULL);
	return ok;
}

void float_updatecrc(ParseTable tpi[], int column, void* structptr, int index)
{
	int fa = TOK_GET_FLOAT_ROUNDING(tpi[column].type);
	F32 value = TokenStoreGetF32_inline(tpi, &tpi[column], column, structptr, index, NULL);
	if (!value)
		value = 0; // Replace -0 with 0

	if (fa)
	{
		int packed_value = ParserPackFloat(value, fa);
		cryptAdler32Update_AutoEndian((U8*)&packed_value, sizeof(int));
	} else {
		cryptAdler32Update_AutoEndian((U8*)&value, sizeof(F32));
	}
}

int float_compare(ParseTable tpi[], int column, const void* lhs, const void* rhs, int index, enumCompareFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int fa = TOK_GET_FLOAT_ROUNDING(tpi[column].type);
	F32 left;
	F32 right = TokenStoreGetF32_inline(tpi, &tpi[column], column, rhs, index, NULL);

	if (lhs)
		left = TokenStoreGetF32_inline(tpi, &tpi[column], column, lhs, index, NULL);
	else
		left = (tpi[column].type & (TOK_FIXED_ARRAY | TOK_EARRAY))?0:GET_FLOAT_FROM_INTPTR(tpi[column].param);

	if (!left)
		left = 0; // Replace -0 with 0
	if (!right)
		right = 0; // Replace -0 with 0
	if (fa)
	{
		int l = ParserPackFloat(left, fa);
		int r = ParserPackFloat(right, fa);
		return l-r;
	}

	if (eFlags & COMPAREFLAG_COMPARE_FLOATS_APPROXIMATELY)
	{
		if (left == 0)
		{
			if (right == 0)
			{
				return 0;
			}

			if (ABS(right) < 0.01f)
			{
				return 0;
			}
		}
		else if (right == 0)
		{
			if (ABS(left) < 0.01f)
			{
				return 0;
			}
		}
		else
		{
			if (left > 0)
			{
				if (right > 0)
				{
					if (ABS(left / right - 1.0f) < 0.01f)
					{
						return 0;
					}
				}
			}
			else if (left < 0)
			{
				if (right < 0)
				{
					if (ABS(left / right - 1.0f) < 0.01f)
					{
						return 0;
					}
				}
			}
		}
	}
	
	return (left < right)? -1: (left > right)? 1: 0;
		
}

void float_calcoffset(ParseTable tpi[], int column, size_t* size)
{
	MEM_ALIGN(*size, sizeof(F32));
	tpi[column].storeoffset = *size;
	(*size) += sizeof(F32);
}

void float_copyfield(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TokenStoreSetF32_inline(tpi, ptcc, column, dest, index, TokenStoreGetF32_inline(tpi, ptcc, column, src, index, NULL), NULL, NULL);
}

enumCopy2TpiResult float_copyfield2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest,  
	CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, char **ppResultString)
{
	TokenStoreSetF32_inline(dest_tpi, &dest_tpi[dest_column], dest_column, dest, dest_index, TokenStoreGetF32_inline(src_tpi, &src_tpi[src_column], src_column, src, src_index, NULL), NULL, NULL);
	return COPY2TPIRESULT_SUCCESS;
}


void float_endianswap(ParseTable tpi[], int column, void* structptr, int index)
{
	// hack - asking to get and set U32 value so I don't have to do conversion
	TokenStoreSetInt_inline(tpi, &tpi[column], column, structptr, index, endianSwapU32(TokenStoreGetInt_inline(tpi, &tpi[column], column, structptr, index, NULL)), NULL, NULL);
}

void float_interp(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 interpParam)
{
	F32 result = interpF32(interpParam, TokenStoreGetF32_inline(tpi, &tpi[column], column, structA, index, NULL), TokenStoreGetF32_inline(tpi, &tpi[column], column, structB, index, NULL));
	TokenStoreSetF32_inline(tpi, &tpi[column], column, destStruct, index, result, NULL, NULL);
}

void float_calcrate(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 deltaTime)
{
	F32 result = (TokenStoreGetF32_inline(tpi, &tpi[column], column, structB, index, NULL) - TokenStoreGetF32_inline(tpi, &tpi[column], column, structA, index, NULL)) / deltaTime;
	TokenStoreSetF32_inline(tpi, &tpi[column], column, destStruct, index, result, NULL, NULL);
}

void float_integrate(ParseTable tpi[], int column, void* valueStruct, void* rateStruct, void* destStruct, int index, F32 deltaTime)
{
	F32 result = TokenStoreGetF32_inline(tpi, &tpi[column], column, valueStruct, index, NULL) + deltaTime * TokenStoreGetF32_inline(tpi, &tpi[column], column, rateStruct, index, NULL);
	TokenStoreSetF32_inline(tpi, &tpi[column], column, destStruct, index, result, NULL, NULL);
}

void float_calccyclic(ParseTable tpi[], int column, void* valueStruct, void* ampStruct, void* freqStruct, void* cycleStruct, void* destStruct, int index, F32 fStartTime, F32 deltaTime)
{
	F32 freq = TokenStoreGetF32_inline(tpi, &tpi[column], column, freqStruct, index, NULL);
	F32 cycle = TokenStoreGetF32_inline(tpi, &tpi[column], column, cycleStruct, index, NULL);
	F32 fSinDiff = sinf( ( (fStartTime + deltaTime) * freq + cycle ) * TWOPI) - sinf( ( fStartTime * freq + cycle ) * TWOPI );
	F32 result = TokenStoreGetF32_inline(tpi, &tpi[column], column, valueStruct, index, NULL) + TokenStoreGetF32_inline(tpi, &tpi[column], column, ampStruct, index, NULL) * fSinDiff;
	TokenStoreSetF32_inline(tpi, &tpi[column], column, destStruct, index, result, NULL, NULL);
}

void float_applydynop(ParseTable tpi[], int column, void* dstStruct, void* srcStruct, int index, DynOpType optype, const F32* values, U8 uiValuesSpecd, U32* seed)
{
	switch (optype)
	{
	case DynOpType_Jitter:
	{
		F32 iToAdd = *values * randomF32Seeded(seed, RandType_BLORN);
		F32 result = TokenStoreGetF32_inline(tpi, &tpi[column], column, dstStruct, index, NULL) + iToAdd;
		TokenStoreSetF32_inline(tpi, &tpi[column], column, dstStruct, index, result, NULL, NULL);
	} break;
	case DynOpType_Inherit:
	{
		if (srcStruct) TokenStoreSetF32_inline(tpi, &tpi[column], column, dstStruct, index, TokenStoreGetF32_inline(tpi, &tpi[column], column, srcStruct, index, NULL), NULL, NULL);
	} break;
	case DynOpType_Add:
	{
		TokenStoreSetF32_inline(tpi, &tpi[column], column, dstStruct, index, TokenStoreGetF32_inline(tpi, &tpi[column], column, dstStruct, index, NULL) + *values, NULL, NULL);
	} break;
	case DynOpType_Multiply:
	{
		TokenStoreSetF32_inline(tpi, &tpi[column], column, dstStruct, index, TokenStoreGetF32_inline(tpi, &tpi[column], column, dstStruct, index, NULL) * *values, NULL, NULL);
	} break;
	case DynOpType_Min:
	{
		TokenStoreSetF32_inline(tpi, &tpi[column], column, dstStruct, index, MIN(*values, TokenStoreGetF32_inline(tpi, &tpi[column], column, dstStruct, index, NULL)), NULL, NULL);
	} break;
	case DynOpType_Max:
	{
		TokenStoreSetF32_inline(tpi, &tpi[column], column, dstStruct, index, MAX(*values, TokenStoreGetF32_inline(tpi, &tpi[column], column, dstStruct, index, NULL)), NULL, NULL);
	} break;
	}
}

void float_queryCopyStruct(ParseTable *pTPI, int iColumn, int iIndex, int iOffsetIntoParentStruct, 
	CopyQueryFlags eQueryFlags, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, 
	StructTypeField iOptionFlagsToExclude, void *pUserData)
{
	StructCopyQueryResult_MarkBytes(!(eQueryFlags & COPYQUERYFLAG_YOU_ARE_FLAGGED_OUT), iOffsetIntoParentStruct + iIndex * sizeof(float), sizeof(float), pUserData);
}

void float_earrayCopy(ParseTable tpi[], int column, void* dest, void* src, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	float **ppSrcEarray = TokenStoreGetEArrayF32_inline(tpi, &tpi[column], column, src, NULL);
	float **ppDestEarray = TokenStoreGetEArrayF32_inline(tpi, &tpi[column], column, dest, NULL);
	eafCopy(ppDestEarray, ppSrcEarray);
}


ParseInfoFieldUsage string_interpretfield(ParseTable tpi[], int column, ParseInfoField field)
{
	switch (field)
	{
	case ParamField:
		if (tpi[column].type & TOK_FIXED_ARRAY)
			return NumberOfElements;
		if (tpi[column].type & TOK_EARRAY)
			return NoFieldUsage;
		if (tpi[column].type & TOK_INDIRECT)
			return PointerToDefaultString;
		return EmbeddedStringLength; // otherwise direct, single
	case SubtableField:
		if (tpi[column].type & TOK_GLOBAL_NAME)
			return PointerToDictionaryName;
		else
			return StaticDefineList;
	}
	return NoFieldUsage;
}

bool string_initstruct(ParseTable tpi[], int column, void* structptr, int index)
{
	int iStorageType = TokenStoreGetStorageType(tpi[column].type);
	// a direct, single string holds its size in the param field, so can't be init'ed
	if (iStorageType == TOK_STORAGE_INDIRECT_SINGLE || iStorageType == TOK_STORAGE_INDIRECT_EARRAY)
	{
		char* str = (char*)tpi[column].param;
		TokenStoreSetString(tpi, column, structptr, index, str, NULL, NULL, NULL, NULL);
		
		//for pool strings, memcpying from the template works fine
		if (str)
		{
			if (tpi[column].type & TOK_POOL_STRING)
			{
				return false;
			}

			return true;
		}
	}
	else
	{
		if (tpi[column].type & TOK_SPECIAL_DEFAULT)
		{
			char *pDefaultString;
			if (GetStringFromTPIFormatString(&tpi[column], "SPECIAL_DEFAULT", &pDefaultString))
			{
				TokenStoreSetString(tpi, column, structptr, index, pDefaultString, NULL, NULL, NULL, NULL);
			

				if ((tpi[column].type & TOK_POOL_STRING) || iStorageType == TOK_STORAGE_DIRECT_SINGLE)
				{
					return false;
				}

				return true;
			}
		}
	}

	//no defaults, copying is fine
	return false;

}

int string_destroystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index)
{
	StructTypeField iFieldType = ptcc->type;
	int iStorageType = TokenStoreGetStorageType(iFieldType);

	if (structptr)
	{
		TokenStoreFreeString(tpi, column, structptr, index, NULL);
	}

	if ((iFieldType & TOK_POOL_STRING) || iStorageType == TOK_STORAGE_DIRECT_SINGLE)
	{
		return DESTROY_NEVER;
	}

	return DESTROY_IF_NON_NULL;

}

int string_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	char* nexttoken = GetNextParameter(tok, parseResult);
	if (!nexttoken) 
		return 0;

	TokenStoreSetString(tpi, column, structptr, index, nexttoken, NULL, NULL, parseResult, tok);
	return 0;
}


TextParserResult string_writebin(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int wr = 0;
	TextParserResult ok = PARSERESULT_SUCCESS;
	const char* str = TokenStoreGetString_inline(tpi, &tpi[column], column, structptr, index, &ok);
	wr = WritePascalString(file, str);
	SET_MIN_RESULT(ok, wr);
	*datasum += wr;
	return ok;
}

TextParserResult string_readbin(SimpleBufHandle file,  FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	char *pTempStr = NULL;
	TextParserResult ok = PARSERESULT_SUCCESS;
	unsigned short strlen;
	int re;

	re = ReadPascalStringLen(file,&strlen);
	if (!re)
	{
		goto Fail;
	}

	pTempStr = alloca(strlen+1);

	re += ReadPascalStringOfLength(file, pTempStr,strlen);
	if (!re)
	{
		goto Fail;
	}

	TokenStoreSetString(tpi, column, structptr, index, pTempStr, NULL, NULL, &ok, NULL);

	goto End;

Fail:
	SET_INVALID_RESULT(ok);

End:	
	*datasum += re;

	return ok;
}





void string_writehdiff(char** estr, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, char *prefix, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	const char* val = TokenStoreGetString_inline(tpi, &tpi[column], column, newstruct, index, NULL);



	if (val) 
	{
		WriteSetValueStringForTextDiff_ThroughQuote(estr, prefix, (int)strlen(prefix), caller_fname, line);
		estrAppendEscaped_dbg(estr, val, caller_fname, line);
		estrConcat_dbg_inline(estr, "\"\n", 2, caller_fname, line);

	}
	else 
	{
		WriteSetValueStringForTextDiff(estr, prefix, (int)strlen(prefix), NULL, 0, caller_fname, line);
	}
}

void string_writebdiff(StructDiff *diff, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, ObjectPath *parentPath, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool invertExcludeFlags, const char *caller_fname, int line)
{
	StructDiffOp *op = eaTail(&diff->ppOps);
	if (op) {
		const char* val = TokenStoreGetString_inline(tpi, &tpi[column], column, newstruct, index, NULL);
		op->pOperand = MultiValCreate();
		MultiValSetString(op->pOperand, (val?val:""));
	}
}

bool string_senddiff(Packet* pak, ParseTable tpi[], int column, const void* oldstruct, const void* newstruct, int index, enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB)
{
	if (/*oldstruct &&*/ (eFlags & SENDDIFF_FLAG_COMPAREBEFORESENDING) && string_compare(tpi,column, oldstruct, newstruct, index, COMPAREFLAG_EMPTY_STRINGS_MATCH_NULL_STRINGS, 0, 0) == 0)
	{
		return false;
	}
	pktSendString(pak, TokenStoreGetString_inline(tpi, &tpi[column], column, newstruct, index, NULL));
	return true;
}

bool string_recvdiff(Packet* pak, ParseTable tpi[], int column, void* structptr, int index, enumRecvDiffFlags eFlags)
{
	char* str;
	U32 storage = TokenStoreGetStorageType(tpi[column].type);

	str = pktGetStringTemp(pak);
	if (!str) str = "";


	if (structptr) 
	{
		//special case for direct fixed size strings to handle buffer overflows properly
	
		if (storage == TOK_STORAGE_DIRECT_SINGLE)
		{
			int iLen = (int)strlen(str);
			char *newstr;

			// TODO - different error strategy?
			if (iLen >= tpi[column].param)
			{
				RECV_FAIL("String of %d bytes being forced into buffer of %d bytes (table %s column %d)", iLen, tpi[column].param,
					ParserGetTableName(tpi), column);
			}

			newstr = (char*)((intptr_t)structptr + tpi[column].storeoffset);
			memcpy(newstr, str, iLen + 1);
			return 1;
		}

		TokenStoreSetString(tpi, column, structptr, index, str, NULL, NULL, NULL, NULL);
	}

	return 1;
}

bool string_bitpack(ParseTable tpi[], int column, const void *structptr, int index, PackedStructStream *pack)
{
	char *def = NULL; // def is the default value for the string
	const char *value = TokenStoreGetString_inline(tpi, &tpi[column], column, structptr, index, NULL);
	// a direct, single string holds its size in the param field, so can't have a default
	if (TokenStoreGetStorageType(tpi[column].type) == TOK_STORAGE_INDIRECT_SINGLE ||
		TokenStoreGetStorageType(tpi[column].type) == TOK_STORAGE_INDIRECT_EARRAY)
	{
		def = (char*)tpi[column].param;
	}
	else
	{
		if (tpi[column].type & TOK_SPECIAL_DEFAULT)
		{
			GetStringFromTPIFormatString(&tpi[column], "SPECIAL_DEFAULT", &def);
		}
	}


	if (!def && !value) // note that if def is not null, but value is null, this will crash, but that should never happen... 
	{
		return false;
	}
	else if (!def || !value || stricmp(def, value)!=0) {
		if (tpi[column].type & TOK_POOL_STRING) {
			StructBitpackPooledStringSub(value, pack);
		} else {
			bsWriteString(pack->bs, value);
		}
		return true;
	} else {
		return false;
	}
}

void string_unbitpack(ParseTable tpi[], int column, void *structptr, int index, PackedStructStream *pack)
{
	if (tpi[column].type & TOK_POOL_STRING) {
		const char *str = StructUnbitpackPooledStringSub(pack);
		TokenStoreSetString(tpi, column, structptr, index, str, NULL, NULL, NULL, NULL);
	} 
	else 
	{
		char *pEString = NULL;
		estrStackCreate(&pEString);
		bsReadString(pack->bs, &pEString);
		TokenStoreSetString(tpi, column, structptr, index, pEString, NULL, NULL, NULL, NULL);
		estrDestroy(&pEString);

	}
}

TextParserResult string_writestring(ParseTable tpi[], int column, void* structptr, int index, char** estr, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *caller_fname, int line)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	const char* val = TokenStoreGetString_inline(tpi, &tpi[column], column, structptr, index, &ok);
	if (RESULT_GOOD(ok) && val)
		estrAppend2_dbg_inline(estr, val, caller_fname, line);
	return ok;
}

TextParserResult string_readstring(ParseTable tpi[], int column, void* structptr, int index, const char* str, ReadStringFlags eFlags)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	if (str && str[0])
		TokenStoreSetString(tpi, column, structptr, index, str, NULL, NULL, &ok, NULL);
	else
	{
		TokenStoreFreeString(tpi, column, structptr, index, &ok);
		if (eFlags & READSTRINGFLAG_READINGEMPTYSTRINGALWAYSSUCCEEDS)
		{
			ok = 1;
		}
	}
	return ok;
}

TextParserResult string_tomulti(ParseTable tpi[], int column, void* structptr, int index, MultiVal* result, bool bDuplicateData, bool dummyOnNULL)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	const char* val;
	if(!structptr && dummyOnNULL)
	{
		MultiValSetDummyType(result, MULTI_STRING);
		return ok;
	}
	val = TokenStoreGetString_inline(tpi, &tpi[column], column, structptr, index, &ok);
	if (!RESULT_GOOD(ok)) return ok;
	if (bDuplicateData)
	{
		MultiValSetString(result, val);
	}
	else
	{
		MultiValReferenceString(result, val);
	}
	return ok;
}

TextParserResult string_frommulti(ParseTable tpi[], int column, void* structptr, int index, const MultiVal* value)
{
	bool mvRes;
	char *val = (char *)MultiValGetString(value, &mvRes);
	TextParserResult ok = mvRes;
	if (RESULT_GOOD(ok)) TokenStoreSetString(tpi, column, structptr, index, val, 0, 0, &ok, NULL);
	return ok;
}

void string_updatecrc(ParseTable tpi[], int column, void* structptr, int index)
{
	int zero = 0;
	const char* str = TokenStoreGetString_inline(tpi, &tpi[column], column, structptr, index, NULL);
	if (str && str[0]) {
		ANALYSIS_ASSUME(str != NULL);
		if (tpi[column].type & TOK_POOL_STRING)
			cryptAdler32Update_IgnoreCase(str, (int)strlen(str));
		else
			cryptAdler32Update(str, (int)strlen(str));
	} else
		cryptAdler32Update((U8*)&zero, sizeof(int));
}

int string_compare(ParseTable tpi[], int column, const void* lhs, const void* rhs, int index, enumCompareFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	{
		const char* left=0;
		const char* right = TokenStoreGetString_inline(tpi, &tpi[column], column, rhs, index, NULL);
		if (lhs)
			left = TokenStoreGetString_inline(tpi, &tpi[column], column, lhs, index, NULL);
		else
		{
			int type = TokenStoreGetStorageType(tpi[column].type);
			if (type == TOK_STORAGE_INDIRECT_SINGLE || type == TOK_STORAGE_INDIRECT_EARRAY)
				left = (char*)tpi[column].param;
			else
			{
				if (tpi[column].type & TOK_SPECIAL_DEFAULT)
				{
					GetStringFromTPIFormatString(&tpi[column], "SPECIAL_DEFAULT", &left);
				}
			}
		}

		if (!left && !right) return 0;

//FIXME turn this on some time when many other related checkins aren't going on
/*		if (!left && !right[0])
		{
			return 0;
		}
		if (!right && !left[0])
		{
			return 0;
		}*/
		ANALYSIS_ASSUME(left != NULL && right != NULL);
		if (eFlags & COMPAREFLAG_EMPTY_STRINGS_MATCH_NULL_STRINGS)
		{
			if ((tpi[column].type & TOK_CASE_SENSITIVE) && left && right)
			{
				return strcmp(left, right);
			}

			return stricmp_safe(left, right);
		}


		if (!left) return -1;
		if (!right) return 1;
		if (tpi[column].type & TOK_CASE_SENSITIVE)
			return strcmp(left, right);
		else
			return stricmp(left, right);
	}
}

size_t string_memusage(ParseTable tpi[], int column, void* structptr, int index, bool bAbsoluteUsage)
{
	return TokenStoreGetStringMemUsage(tpi, column, structptr, index, bAbsoluteUsage, NULL);
}

void string_calcoffset(ParseTable tpi[], int column, size_t* size)
{
	int storage = TokenStoreGetStorageType(tpi[column].type);

	if (storage == TOK_STORAGE_DIRECT_SINGLE)
	{
		tpi[column].storeoffset = *size;
		(*size) += tpi[column].param; // length of buffer
	}
	else
	{
		MEM_ALIGN(*size, sizeof(char*));
		tpi[column].storeoffset = *size;
		(*size) += sizeof(char*);
	}
}

void string_copystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData)
{
	const char* srcstr = TokenStoreGetString_inline(tpi, ptcc, column, src, index, NULL);
	TokenStoreClearString_inline(tpi, ptcc, column, dest, index, NULL); // need to clear so we don't free on the next call
	TokenStoreSetString(tpi, column, dest, index, srcstr, memAllocator, customData, NULL, NULL);
}

void string_copyfield(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TokenStoreSetString(tpi, column, dest, index, TokenStoreGetString_inline(tpi, ptcc, column, src, index, NULL), memAllocator, customData, NULL, NULL);
}

enumCopy2TpiResult string_copyfield2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest,  
	CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, char **ppResultString)
{
	TokenStoreSetString(dest_tpi, dest_column, dest, dest_index, TokenStoreGetString_inline(src_tpi, &src_tpi[src_column], src_column, src, src_index, NULL), memAllocator, customData, NULL, NULL);
	return COPY2TPIRESULT_SUCCESS;
}

void string_applydynop(ParseTable tpi[], int column, void* dstStruct, void* srcStruct, int index, DynOpType optype, const F32* values, U8 uiValuesSpecd, U32* seed)
{
	switch (optype)
	{
	case DynOpType_Inherit:
	{
		if (srcStruct)
		{
			TokenStoreFreeString(tpi, column, dstStruct, index, NULL);
			TokenStoreSetString(tpi, column, dstStruct, index, TokenStoreGetString_inline(tpi, &tpi[column], column, srcStruct, index, NULL), NULL, NULL, NULL, NULL);
		}
	} break;
	};
}

int string_checksharedmemory(ParseTable tpi[], int column, void* structptr, int index)
{
	const char* str = TokenStoreGetString_inline(tpi, &tpi[column], column, structptr, index, NULL);
	if (str)
	{	
		if (!isSharedMemory(str))
		{
			const char *structName;
			if (!(structName = ParserGetTableName(tpi)))
			{
				structName = "UNKNOWN";
			}
			assertmsgf(0,"Element %s (index %d) in struct %s is not pointing to valid shared memory! This will crash when loaded from shared memory. ",tpi[column].name,index,structName);

			return 0;
		}
		/*if (tpi[column].type & TOK_POOL_STRING || tpi[column].type & TOK_ESTRING)
		{
			char *structName;
			if (!(structName = ParserGetTableName(tpi)))
			{
				structName = "UNKNOWN";
			}
			assertmsgf(0,"Element %s (index %d) in struct %s is not pointing to valid shared memory! This will crash when loaded from shared memory. ",tpi[column].name,index,structName);

			return 0; //Non-fatal, but these won't be allocate in the way that is expected
		}*/


	}
	return 1;
}

void string_queryCopyStruct(ParseTable *pTPI, int iColumn, int iIndex, int iOffsetIntoParentStruct, 
	CopyQueryFlags eQueryFlags, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, 
	StructTypeField iOptionFlagsToExclude, void *pUserData)
{
	int storage = TokenStoreGetStorageType(pTPI[iColumn].type);

	//fixed arrays of strings don't exist
	assert(!(eQueryFlags & COPYQUERYFLAG_IN_FIXED_ARRAY));

	//direct embedded strings always just return their size
	if (storage == TOK_STORAGE_DIRECT_SINGLE)
	{
		StructCopyQueryResult_MarkBytes(!(eQueryFlags & COPYQUERYFLAG_YOU_ARE_FLAGGED_OUT), iOffsetIntoParentStruct, pTPI[iColumn].param, pUserData);
	}
	else if (storage == TOK_STORAGE_INDIRECT_SINGLE)
	{
		//pooled string can just be copied by copying the pointer
		if (pTPI[iColumn].type & TOK_POOL_STRING)
		{
			StructCopyQueryResult_MarkBytes(!(eQueryFlags & COPYQUERYFLAG_YOU_ARE_FLAGGED_OUT), iOffsetIntoParentStruct, sizeof(char*), pUserData);
		}
		else //estrings and malloced strings need a callback
		{
			StructCopyQueryResult_MarkBytes(false, iOffsetIntoParentStruct, sizeof(char*), pUserData);
			
			if (!(eQueryFlags & COPYQUERYFLAG_YOU_ARE_FLAGGED_OUT))
			{
				StructCopyQueryResult_NeedCallback(pUserData);
			}
		}
	}
}

void string_earrayCopy(ParseTable tpi[], int column, void* dest, void* src, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	char ***pppSrcEarray = (char***)TokenStoreGetEArray_inline(tpi, &tpi[column], column, src, NULL);
	char ***pppDestEarray = (char***)TokenStoreGetEArray_inline(tpi, &tpi[column], column, dest, NULL);
	
	int iSrcSize = eaSize(pppSrcEarray);
	int iDestSize, iSharedSize;
	int i;
	const char *pStructName = ParserGetTableName(tpi);

	iDestSize = eaSize(pppDestEarray);

	if (tpi[column].type & TOK_POOL_STRING)
	{
		if (iSrcSize == 0 && iDestSize == 0) // Don't waste memory on empty earrays
			return;
		eaSetSize_dbg(pppDestEarray, iSrcSize TPC_MEMDEBUG_PARAMS_EARRAY);
		memcpy((*pppDestEarray), (*pppSrcEarray), iSrcSize * sizeof(char*));
		return;
	}


	iSharedSize = MIN(iSrcSize, iDestSize);


	if (tpi[column].type & TOK_ESTRING)
	{
		for (i=0; i < iSharedSize; i++)
		{
			char **ppSrc = &(*pppSrcEarray)[i];
			char **ppDest = &(*pppDestEarray)[i];

			if (*ppSrc)
			{
				if (!(*ppDest))
				{
					estrCopy_dbg(ppDest, ppSrc TPC_MEMDEBUG_PARAMS_STRING);
				}
				else
				{
					if (strcmp(*ppSrc, *ppDest) != 0)
					{
						estrCopy_dbg(ppDest, ppSrc TPC_MEMDEBUG_PARAMS_STRING);
					}
				}
			}
			else
			{
				if (*ppDest)
				{
					estrDestroy(ppDest);
				}
			}
		}

		//if src array is larger
		for (i=iSharedSize; i < iSrcSize; i++)
		{
			char **ppSrc = &(*pppSrcEarray)[i];

			eaPush_dbg(pppDestEarray, NULL TPC_MEMDEBUG_PARAMS_EARRAY);

			if (*ppSrc)
			{
				estrCopy_dbg(&(*pppDestEarray)[i], ppSrc  TPC_MEMDEBUG_PARAMS_STRING);
			}
			
		}

		//if dest array is larger
		for (i=iDestSize - 1; i >= iSharedSize; i--)
		{	
			assert(*pppDestEarray);
			
			estrDestroy(&(*pppDestEarray)[i]);
			eaPop(pppDestEarray);
		}	
	}
	else
	{
		//MALLOCED STRINGS

		for (i=0; i < iSharedSize; i++)
		{
			char *pSrc = (*pppSrcEarray)[i];
			char *pDest = (*pppDestEarray)[i];

			if (pSrc)
			{
				ANALYSIS_ASSUME(pSrc);
				if (!pDest)
				{
					(*pppDestEarray)[i] = strdup_dbg(pSrc TPC_MEMDEBUG_PARAMS_STRING);
				}
				else
				{
					ANALYSIS_ASSUME(pDest);
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*pDest'"
					if (strcmp(pSrc, pDest) != 0)
					{
						if(!isSharedMemory(pDest))
							free(pDest);
						(*pppDestEarray)[i] = strdup_dbg(pSrc TPC_MEMDEBUG_PARAMS_STRING);
					}
				}
			}
			else
			{
				if (pDest)
				{
					ANALYSIS_ASSUME(pDest);
					if(!isSharedMemory(pDest))
						free(pDest);
					(*pppDestEarray)[i] = NULL;
				}
			}
		}

		//if src array is larger
		for (i=iSharedSize; i < iSrcSize; i++)
		{
			char *pSrc = (*pppSrcEarray)[i];
			if (pSrc)
			{
				eaPush_dbg(pppDestEarray, strdup_dbg(pSrc TPC_MEMDEBUG_PARAMS_STRING) TPC_MEMDEBUG_PARAMS_EARRAY);
			}
			else
			{
				eaPush_dbg(pppDestEarray, NULL TPC_MEMDEBUG_PARAMS_EARRAY);
			}
		}

		//if dest array is larger
		for (i=iDestSize - 1; i >= iSharedSize; i--)
		{	
			char *pDest;
			assert(*pppDestEarray);
			
			pDest = (*pppDestEarray)[i];
			if(pDest && !isSharedMemory(pDest))
			{
				ANALYSIS_ASSUME(pDest);
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*pDest'"
				SAFE_FREE(pDest);
			}
			eaPop(pppDestEarray);
		}
	}
}




//ABW 3/27/07 - NOTE, the pointer_ functions are basically obsolete, but some of them are being
//used by usedfield, plus they may need to be resurrected at some point.
ParseInfoFieldUsage pointer_interpretfield(ParseTable tpi[], int column, ParseInfoField field)
{
	if (field == ParamField) 
		return OffsetOfSizeField;
	return NoFieldUsage;
}

int pointer_destroystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index)
{
	if (structptr)
	{
		int* count = TokenStoreGetCountField_inline(tpi, ptcc, column, structptr, NULL);
		TokenStoreFree(tpi, column, structptr, index, NULL);
		*count = 0;
	}
	return DESTROY_IF_NON_NULL;
}

TextParserResult pointer_writebin(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int* count = TokenStoreGetCountField_inline(tpi, &tpi[column], column, structptr, &ok);
	void* data = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, &ok);
	SET_MIN_RESULT(ok, SimpleBufWriteU32(*count, file));
	*datasum += sizeof(int);
	if (*count)
	{
		SET_MIN_RESULT(ok, SimpleBufWrite(data, *count, file));
		*datasum += *count;
	}
	return ok;
}

TextParserResult pointer_tomulti(ParseTable tpi[], int column, void* structptr, int index, MultiVal* result, bool bDuplicateData, bool dummyOnNULL)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	void* val;
	if(!structptr && dummyOnNULL)
	{
		MultiValSetDummyType(result, MULTI_NP_POINTER);
		return ok;
	}
	val = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, &ok);
	if (!RESULT_GOOD(ok)) return ok;
	if (devassertmsg(!bDuplicateData, "pointer types cannot be copied into MultiVals at this level"))
		MultiValReferencePointer(result, val);
	else
		ok = PARSERESULT_ERROR;
	return ok;
}



//usedfield_writebin is copied from pointer_writebin, but fixed to be endian-safe, hopefully
TextParserResult usedfield_writebin(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int* count = TokenStoreGetCountField_inline(tpi, &tpi[column], column, structptr, &ok);
	U32* data = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, &ok);
	
	//must be in blocks of U32s, as determined by usedfield_GetAllocSize
	assert((*count) % 4 == 0);

	SET_MIN_RESULT(ok, SimpleBufWriteU32(*count, file));
	*datasum += sizeof(int);
	if (*count)
	{
		int iNumWords = (*count) / 4;
		int i;

		for (i=0; i < iNumWords; i++)
		{
			SET_MIN_RESULT(ok, SimpleBufWriteU32(data[i], file));
		}

		*datasum += *count;
	}
	return ok;
}


TextParserResult pointer_readbin(SimpleBufHandle file, FileList *pFileList,  ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int size = 0;
	int* count = TokenStoreGetCountField_inline(tpi, &tpi[column], column, structptr, &ok);
	void* data;

	// read size of data
	if (!SimpleBufReadU32((U32*)&size, file))
		SET_INVALID_RESULT(ok);
	*datasum += sizeof(int);

	// realloc as necessary
	if (*count != size)
	{
		TokenStoreFree(tpi, column, structptr, index, &ok);
		if (size)
			TokenStoreAlloc(tpi, column, structptr, index, size, NULL, NULL, &ok);
		*count = size;
	}
	if (size)
	{
		data = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, &ok);
		if (!SimpleBufRead(data, size, file))
			SET_INVALID_RESULT(ok);
		*datasum += size;
	}
	return ok;
}

void pointer_queryCopyStruct(ParseTable *pTPI, int iColumn, int iIndex, int iOffsetIntoParentStruct, 
	CopyQueryFlags eQueryFlags, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, 
	StructTypeField iOptionFlagsToExclude, void *pUserData)
{
	assertmsg(TokenStoreGetStorageType(pTPI[iColumn].type) == TOK_STORAGE_INDIRECT_SINGLE, "pointers seem to only support indirect single right now");
	assert(!(eQueryFlags & COPYQUERYFLAG_IN_FIXED_ARRAY));

	StructCopyQueryResult_MarkBytes(false, iOffsetIntoParentStruct, sizeof(void*), pUserData);

//need to mark the int that specifies the size of the pointer for non-copying
	StructCopyQueryResult_MarkBytes(false, (int)(iOffsetIntoParentStruct - (int)pTPI[iColumn].storeoffset + (int)pTPI[iColumn].param), sizeof(int), pUserData);

	if (!(eQueryFlags & COPYQUERYFLAG_YOU_ARE_FLAGGED_OUT))
	{
		StructCopyQueryResult_NeedCallback(pUserData);
	}
}


//usedfield_readbin is copied from pointer_readbin, but fixed to be endian-safe, hopefully
TextParserResult usedfield_readbin(SimpleBufHandle file,  FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int size = 0;
	int* count = TokenStoreGetCountField_inline(tpi, &tpi[column], column, structptr, &ok);
	U32* data;

	// read size of data
	if (!SimpleBufReadU32((U32*)&size, file))
		SET_INVALID_RESULT(ok);
	*datasum += sizeof(int);

	//size must be blocks of U32s, as determined by usedfield_GetAllocSize
	assert(size % 4 == 0);


	// realloc as necessary
	if (*count != size)
	{
		TokenStoreFree(tpi, column, structptr, index, &ok);
		if (size)
			TokenStoreAlloc(tpi, column, structptr, index, size, NULL, NULL, &ok);
		*count = size;
	}
	if (size)
	{
		int i;
		int iNumWords = size / 4;

		data = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, &ok);

		for (i=0; i < iNumWords; i++)
		{
			if (!SimpleBufReadU32(&data[i], file))
				SET_INVALID_RESULT(ok);
		}
		*datasum += size;
	}
	return ok;
}

bool usedfield_bitpack(ParseTable tpi[], int column, const void* structptr, int index, PackedStructStream *pack)
{
	int *pcount = TokenStoreGetCountField_inline(tpi, &tpi[column], column, structptr, NULL);
	int count = *pcount;
	U32 *data = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	int num_columns = ParserGetTableNumColumns(tpi);
	int num_u32s = (num_columns+31)/32;
	int expected_count = num_u32s*4;
	int i;
	if (count==0)
		return false;
	assert(count == expected_count);
	for (i=0; i<num_u32s; i++) 
		if (data[i])
			break;
	if (i==num_u32s)
		return false; // No data to send
	for (i=0; i<num_u32s; i++) 
		bsWriteBits(pack->bs, 32, data[i]);
	return true;
}

void usedfield_unbitpack(ParseTable tpi[], int column, void *structptr, int index, PackedStructStream *pack)
{
	int *pcount = TokenStoreGetCountField_inline(tpi, &tpi[column], column, structptr, NULL);
	int num_columns = ParserGetTableNumColumns(tpi);
	int num_u32s = (num_columns+31)/32;
	int expected_count = num_u32s*4;
	U32 *data;
	int i;

	assert(structptr);

	// realloc as necessary
	if (*pcount != expected_count)
	{
		TokenStoreFree(tpi, column, structptr, index, NULL);
		if (expected_count)
			TokenStoreAlloc(tpi, column, structptr, index, expected_count, NULL, NULL, NULL);
		*pcount = expected_count;
	}

	data = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);

	for (i=0; i<num_u32s; i++) 
		data[i] = bsReadBits(pack->bs, 32);
}

#if 0

bool pointer_senddiff(Packet* pak, ParseTable tpi[], int column, const void* oldstruct, const void* newstruct, int index, enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB)
{
	int* count;

	if (oldstruct && (eFlags & SENDDIFF_FLAG_COMPAREBEFORESENDING) && pointer_compare(tpi,column, oldstruct, newstruct, index, 0, 0, 0) == 0)
	{
		return false;
	}
	
	count = TokenStoreGetCountField_inline(tpi, &tpi[column], column, newstruct, NULL);
	pktSendBits(pak, 32, *count);
	if (*count)
		pktSendBytes(pak, *count, TokenStoreGetPointer_inline(tpi, &tpi[column], column, newstruct, index, NULL));
	return true;
}


void pointer_recvdiff(Packet* pak, ParseTable tpi[], int column, void* structptr, int index, enumRecvDiffFlags eFlags)
{
	int* count = TokenStoreGetCountField_inline(tpi, &tpi[column], column, structptr, NULL);
	int size;


	size = pktGetBits(pak,32);

	// realloc as necessary
	if (*count != size && structptr)
	{
		TokenStoreFree(tpi, column, structptr, index, NULL);
		if (size)
			TokenStoreAlloc(tpi, column, structptr, index, size, NULL, NULL, NULL);
		*count = size;
	}

	if (size) // being sent data
	{
		if (structptr)
			pktGetBytes(pak, size, TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL));
		else
		{
			char* temp = malloc(size);
			pktGetBytes(pak, size, temp);
			free(temp);
		}
	}
	// otherwise, I already free'd the data if necessary above
}
#endif


void usedfield_updatecrc(ParseTable tpi[], int column, void* structptr, int index)
{
	int* count = TokenStoreGetCountField_inline(tpi, &tpi[column], column, structptr, NULL);
	void* data = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	int i;
	assert(!((*count) & 0x3)); // Multiple of 4 bytes
	if (*count) {
		for (i=0; i<*count; i+=4) 
			cryptAdler32Update_AutoEndian(((U8*)data) + i, 4);
	}
}

int pointer_compare(ParseTable tpi[], int column, const void* lhs, const void* rhs, int index, enumCompareFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int* lcount = lhs||!(eFlags & COMPAREFLAG_NULLISDEFAULT)?TokenStoreGetCountField_inline(tpi, &tpi[column], column, lhs, NULL):NULL;
	int* rcount = TokenStoreGetCountField_inline(tpi, &tpi[column], column, rhs, NULL);
	void* left = TokenStoreGetPointer_inline(tpi, &tpi[column], column, lhs, index, NULL);
	void* right = TokenStoreGetPointer_inline(tpi, &tpi[column], column, rhs, index, NULL);
	int ret;
	if (!left && !right)
		return 0;
	if (!left || !lcount)
		return -1;
	if (!right)
		return 1;
	if (*lcount != *rcount)
		return *lcount - *rcount;
    ret = cmp8(left, right, *lcount);
	if (ret)
		return ret;
	return 0;
}

size_t pointer_memusage(ParseTable tpi[], int column, void* structptr, int index, bool bAbsoluteUsage)
{
	int* count = TokenStoreGetCountField_inline(tpi, &tpi[column], column, structptr, NULL);
	void* data = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	if (data) return *count;
	return 0;
}

void pointer_calcoffset(ParseTable tpi[], int column, size_t* size)
{
	// put the count field in first
	MEM_ALIGN(*size, sizeof(int));
	tpi[column].param = *size;
	(*size) += sizeof(int);

	// then the pointer
	MEM_ALIGN(*size, sizeof(void*));
	tpi[column].storeoffset = *size;
	(*size) += sizeof(void*);
}

void pointer_copystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData)
{
	int* srccount = TokenStoreGetCountField_inline(tpi, ptcc, column, src, NULL);
	void* destpointer;
	void* srcpointer = TokenStoreGetPointer_inline(tpi, ptcc, column, src, index, NULL);

	TokenStoreSetPointer_inline(tpi, ptcc, column, dest, index,NULL, NULL); // clear so alloc won't free
	if (*srccount)
	{
		destpointer = TokenStoreAlloc(tpi, column, dest, index, *srccount, memAllocator, customData, NULL);
		memcpy(destpointer, srcpointer, *srccount);
	}
}

//used by copy2field2tpis
void pointer_copystruct2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest,  CustomMemoryAllocator memAllocator, void* customData)
{
	int* srccount = TokenStoreGetCountField_inline(src_tpi, &src_tpi[src_column], src_column, src, NULL);
	void* destpointer;
	void* srcpointer = TokenStoreGetPointer_inline(src_tpi, &src_tpi[src_column], src_column, src, src_index, NULL);

	TokenStoreSetPointer_inline(dest_tpi, &dest_tpi[dest_column],dest_column, dest, dest_index,NULL, NULL); // clear so alloc won't free
	if (*srccount)
	{
		destpointer = TokenStoreAlloc(dest_tpi, dest_column, dest, dest_index, *srccount, memAllocator, customData, NULL);
		memcpy(destpointer, srcpointer, *srccount);
	}
}

void pointer_copyfield(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int* srccount = TokenStoreGetCountField_inline(tpi, ptcc, column, src, NULL);
	int* destcount = TokenStoreGetCountField_inline(tpi, ptcc, column, dest, NULL);

	// some optimization if source and target are already same size
	if (*srccount == *destcount)
	{
		void* srcpointer = TokenStoreGetPointer_inline(tpi, ptcc, column, src, index, NULL);
		void* destpointer = TokenStoreGetPointer_inline(tpi, ptcc, column, dest, index, NULL);
		if (!*srccount) return; // both empty
		memcpy(destpointer, srcpointer, *srccount);
	}
	else
	{
		// otherwise, basically the same as copystruct, but we need to free first
		TokenStoreFree(tpi, column, dest, index, NULL);
		pointer_copystruct(tpi, ptcc, column, dest, src, index, memAllocator, customData);
		*destcount = *srccount;
	}
}

enumCopy2TpiResult pointer_copyfield2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest,  
	CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, char **ppResultString)
{
	int* srccount = TokenStoreGetCountField_inline(src_tpi, &src_tpi[src_column], src_column, src, NULL);
	int* destcount = TokenStoreGetCountField_inline(dest_tpi, &dest_tpi[dest_column], dest_column, dest, NULL);

	// some optimization if source and target are already same size
	if (*srccount == *destcount)
	{
		void* srcpointer = TokenStoreGetPointer_inline(src_tpi, &src_tpi[src_column], src_column, src, src_index, NULL);
		void* destpointer = TokenStoreGetPointer_inline(dest_tpi, &dest_tpi[dest_column], dest_column, dest, dest_index, NULL);
		if (!*srccount) return COPY2TPIRESULT_SUCCESS; // both empty
		memcpy(destpointer, srcpointer, *srccount);
	}
	else
	{
		// otherwise, basically the same as copystruct, but we need to free first
		TokenStoreFree(dest_tpi, dest_column, dest, dest_index, NULL);
		pointer_copystruct2tpis(src_tpi, src_column, src_index, src, dest_tpi, dest_column, dest_index, dest, memAllocator, customData);
		*destcount = *srccount;
	}
	return COPY2TPIRESULT_SUCCESS;

}

int pointer_checksharedmemory(ParseTable tpi[], int column, void* structptr, int index)
{
	void* ptr = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	if (ptr && !isSharedMemory(ptr))
	{
		const char *structName;
		if (!(structName = ParserGetTableName(tpi)))
		{
			structName = "UNKNOWN";
		}
		assertmsgf(0,"Element %s (index %d) in struct %s is not pointing to valid shared memory! This will crash when loaded from shared memory. ",tpi[column].name,index,structName);

		return 0;
	}
	return 1;
}

//////////////////////////////////////////////// built-ins

bool ShouldSetCurrentFile(ParseTable *tpi, int column, char *pStr)
{
	/*if (GetAppGlobalType() == GLOBALTYPE_CLIENT && isProductionMode())
	{

		if (tpi[column].type & TOK_NEEDED)
		{
			return true;
		}

		return false;
	}*/

	return true;
}

void currentfile_preparse(ParseTable* tpi, int column, void* structptr, TokenizerHandle tok)
{
	char* str;
	if (!TokenizerShouldParseCurrentFile(tok))
	{	
		str = TokenStoreSetString(tpi, column, structptr, 0, TokenizerGetFileName(tok), NULL, NULL, NULL, NULL);
		//_strupr_s(str, strlen(str)+1);
		forwardSlashes(str);
	}
}

void currentfile_reapplypreparse(ParseTable* tpi, int column, void* structptr, int iIndex, char *pCurrentFile, int iTimeStamp, int iLineNum)
{
	TokenStoreSetString(tpi, column, structptr, 0, pCurrentFile, NULL, NULL, NULL, NULL);
}

int currentfile_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	if (TokenizerShouldParseCurrentFile(tok))
	{
		return string_parse(tok, tpi, ptcc, column, structptr, index, parseResult);
	}
	else
	{
		return ignore_parse(tok, tpi, ptcc, column, structptr, index, parseResult);
	}
}


TextParserResult currentfile_readbin(SimpleBufHandle file,  FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	char *pTempStr = NULL;
	TextParserResult ok = PARSERESULT_SUCCESS;
	int re;
	int iFileListIndex;
	

	if (!SimpleBufReadU32(&iFileListIndex, file))
	{
		SET_INVALID_RESULT(ok);
		return ok;
	}

	*datasum += 4;


	if (iFileListIndex == FILELIST_SPECIALINDEX_NULL)
	{
		TokenStoreSetString(tpi, column, structptr, index, NULL, NULL, NULL, &ok, NULL);
	}
	else if (iFileListIndex == FILELIST_SPECIALINDEX_NOTINLIST)
	{
		estrStackCreate(&pTempStr);
		re = ReadPascalStringIntoEString(file, &pTempStr);
		if (ShouldSetCurrentFile(tpi, column, pTempStr))
		{
			TokenStoreSetString(tpi, column, structptr, index, pTempStr, NULL, NULL, &ok, NULL);
		}
		if (!re)
			SET_INVALID_RESULT(ok);
		*datasum += re;
		estrDestroy(&pTempStr);
	}
	else
	{
		const char *pStringFromList;
		assert(pFileList);
		pStringFromList = FileListGetFromIndex(pFileList, iFileListIndex - FILELIST_INDEX_OFFSET);
		assert(pStringFromList);
		TokenStoreSetString(tpi, column, structptr, index, pStringFromList, NULL, NULL, &ok, NULL);
	}
		
	return ok;
}

bool currentfile_recvdiff(Packet* pak, ParseTable tpi[], int column, void* structptr, int index, enumRecvDiffFlags eFlags)
{
	U32 storage = TokenStoreGetStorageType(tpi[column].type);
	char *str = pktGetStringTemp(pak);

	TS_REQUIRE(TOK_STORAGE_INDIRECT_SINGLE | TOK_STORAGE_INDIRECT_EARRAY);

	if (structptr && ShouldSetCurrentFile(tpi, column, str)) TokenStoreSetString(tpi, column, structptr, index, str, NULL, NULL, NULL, NULL);

	return 1;
}


void timestamp_preparse(ParseTable* tpi, int column, void* structptr, TokenizerHandle tok)
{
	TokenStoreSetInt_inline(tpi, &tpi[column], column, structptr, 0, fileLastChanged(TokenizerGetFileName(tok)), NULL, NULL);
}

void timestamp_reapplypreparse(ParseTable* tpi, int column, void* structptr, int iIndex, char *pCurrentFile, int iTimeStamp, int iLineNum)
{
	TokenStoreSetInt_inline(tpi, &tpi[column], column, structptr, 0, iTimeStamp, NULL, NULL);
}
void linenum_preparse(ParseTable* tpi, int column, void* structptr, TokenizerHandle tok)
{
	TokenStoreSetInt_inline(tpi, &tpi[column], column, structptr, 0, TokenizerGetCurLine(tok), NULL, NULL);
}
void linenum_reapplypreparse(ParseTable* tpi, int column, void* structptr, int iIndex, char *pCurrentFile, int iTimeStamp, int iLineNum)
{
	TokenStoreSetInt_inline(tpi, &tpi[column], column, structptr, 0, iLineNum, NULL, NULL);
}

void usedfield_preparse(ParseTable* tpi, int column, void* structptr, TokenizerHandle tok)
{
	TokenStoreAddUsedField(tpi, column, structptr);
}

int bool_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	int value;

	char* nexttoken = GetNextParameter_Int(tok, parseResult);
	if (!nexttoken) 
		return 0;

	if (nexttoken == TOK_SPECIAL_INT_RETURN)
	{
		value = TokenizerGetReadInt(tok);
	}
	else
	{
		value = GetInteger(tok, nexttoken, parseResult);
	}

	if (value < 0 || value > 1)
	{
		TokenizerErrorf(tok,"Bool parameter should be 0 or 1");
		SET_ERROR_RESULT(*parseResult);

		value = 0;
	}
	assert(sizeof(bool) == sizeof(U8));

	TokenStoreSetU8_inline(tpi, ptcc, column, structptr, index, value, parseResult, tok);
	return 0;
}

bool bool_bitpack(ParseTable tpi[], int column, const void *structptr, int index, PackedStructStream *pack)
{
	bool value = TokenStoreGetU8_inline(tpi, &tpi[column], column, structptr, index, NULL);
	int def = (tpi[column].type & (TOK_FIXED_ARRAY | TOK_EARRAY))?0:tpi[column].param;
	if (value != def) {
		// Don't need to write anything, if we get here, we know the value is the opposite of the default.
		return true;
	} else {
		return false;
	}
}

void bool_unbitpack(ParseTable tpi[], int column, void *structptr, int index, PackedStructStream *pack)
{
	// If this gets called, then it's not the default, so we don't even need any bits, neato!
	int def = (tpi[column].type & (TOK_FIXED_ARRAY | TOK_EARRAY))?0:tpi[column].param;
	TokenStoreSetU8_inline(tpi, &tpi[column], column, structptr, index, !def, NULL, NULL);
}

int flags_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	U32 value = 0;

	while (1)
	{
		char* nexttoken = TokenizerPeek(tok, PEEKFLAG_IGNORECOMMA, parseResult);
		if (!nexttoken || IsEol(nexttoken)) 
			break;

		nexttoken = TokenizerGet(tok, PEEKFLAG_IGNORECOMMA, parseResult);
		value |= GetUInteger(tok, nexttoken, parseResult);
	}

	TokenStoreSetInt_inline(tpi, ptcc, column, structptr, 0, value, parseResult, tok);
	return 0;
}

bool flags_writetext(FILE* out, ParseTable tpi[], int column, void* structptr, int index, bool showname, bool ignoreInherited, int level, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	U32 val = TokenStoreGetInt_inline(tpi, &tpi[column], column, structptr, 0, NULL);
	U32 mask = 1;
	int i;
	int first = 1;
	bool bCanSkip = !(tpi[column].type & TOK_REQUIRED) && val == (U32)tpi[column].param && !SHOULD_WRITE_DEFAULT(iWriteTextFlags, tpi, column, structptr); // if default value, not necessary to write field

	if (showname && bCanSkip)
	{
		return false;
	}

	if (showname) WriteNString(out, tpi[column].name,ParseTableColumnNameLen(tpi,column), level+1, 0);
	for (i = 0; i < 32; i++)
	{
		if (val & 1)
		{
			if (first) { WriteNString(out, " ",1, 0, 0); first = 0; }
			else { WriteNString(out, ", ",2, 0, 0); }
			WriteUInt(out, mask, 0, 0, tpi[column].subtable);
		}
		mask <<= 1;
		val >>= 1;
	}
	if (showname) WriteNString(out, NULL, 0, 0, 1);

	return !bCanSkip;
}


TextParserResult flags_readstring(ParseTable tpi[], int column, void* structptr, int index, const char* str, ReadStringFlags eFlags)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	U32 value = 0;
	char* param;
	char *next;
	char *strtokcontext = 0;	
	
	if (!str || !str[0])
	{
		TokenStoreSetInt_inline(tpi, &tpi[column], column, structptr, 0, 0, &ok, NULL);
		return ok;
	}
	param = ScratchAlloc(strlen(str)+1);
	assert(param);
	strcpy_s(param, strlen(str)+1, str);
	next = strtok_s(param, "|", &strtokcontext);
	while (next && RESULT_GOOD(ok))
	{
		int tempValue;
		int iEnum;
		while (next[0] == ' ') next++;
		if (!next[0]) break;
	
		if ((iEnum = StaticDefineInt_FastStringToInt(tpi[column].subtable, next, INT_MIN)) != INT_MIN)
		{
			value |= iEnum;
		}
		else if (sscanf(next, "%d", &tempValue) == 1)
		{
			value |= tempValue;
		}
		else
		{
			SET_ERROR_RESULT(ok);
		}
		next = strtok_s(NULL, "|", &strtokcontext);
	}
	if (RESULT_GOOD(ok)) TokenStoreSetInt_inline(tpi, &tpi[column], column, structptr, 0, value, &ok, NULL);
	ScratchFree(param);
	return ok;
}




int boolflag_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	// if we hit the field at all, it gets set to 1
	TokenStoreSetU8_inline(tpi, ptcc, column, structptr, index, 1, parseResult, tok);
	return 0;
}

bool boolflag_writetext(FILE* out, ParseTable tpi[], int column, void* structptr, int index, bool showname, bool ignoreInherited, int level, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	bool bCanSkip = !(TokenStoreGetU8_inline(tpi, &tpi[column], column, structptr, index, NULL) || SHOULD_WRITE_DEFAULT(iWriteTextFlags, tpi, column, structptr));
	if (showname && !bCanSkip)
	{
		WriteNString(out, tpi[column].name, ParseTableColumnNameLen(tpi,column), level+1, 1);
	}

	return !bCanSkip;
}

int quatpyr_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	int v;
	F32 pyr[3];
	F32* quat;

	char *nexttoken = GetNextParameter(tok, parseResult);
	if (!nexttoken)
		return 0;

	//quats can be written two ways: "x, y, z", which are pyr angles, or "quat4 x y z w" which is raw quat values
	if (stricmp(nexttoken, "quat4") == 0)
	{
		quat = (F32*)TokenStoreGetPointer_inline(tpi, ptcc, column, structptr, 0, parseResult);

		for (v = 0; v < 4; v++)
		{
			nexttoken = GetNextParameter(tok, parseResult);
			if (!nexttoken) 
				return 0;
			quat[v] = GetFloat(tok, nexttoken, parseResult);
		}

		return 0;
	}

	// Else in PYR format
	for (v = 0; v < 3; v++)
	{
		if (v > 0)
			nexttoken = GetNextParameter(tok, parseResult);
		if (!nexttoken) 
			return 0;
		pyr[v] = GetFloat(tok, nexttoken, parseResult);
	}

	quat = (F32*)TokenStoreGetPointer_inline(tpi, ptcc, column, structptr, 0, parseResult);
	RADVEC3(pyr);
	PYRToQuat(pyr, quat);
	return 0;
}

bool quatpyr_writetext(FILE* out, ParseTable tpi[], int column, void* structptr, int index, bool showname, bool ignoreInherited, int level, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	F32* quat = (F32*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, 0, NULL);

	if (showname) WriteNString(out, tpi[column].name, ParseTableColumnNameLen(tpi,column), level+1, 0);
	
	
	WriteNString(out, " quat4 ", 7, 0, 0);
	WriteFloat(out, quat[0], 0, 0, tpi[column].subtable);
	WriteNString(out, ", ", 2, 0, 0);
	WriteFloat(out, quat[1], 0, 0, tpi[column].subtable);
	WriteNString(out, ", ",2, 0, 0);
	WriteFloat(out, quat[2], 0, 0, tpi[column].subtable);
	WriteNString(out, ", ", 2, 0, 0);
	WriteFloat(out, quat[3], 0, showname, tpi[column].subtable);

	return true;
}

void quatpyr_writebdiff(StructDiff *diff, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, ObjectPath *parentPath, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool invertExcludeFlags, const char *caller_fname, int line)
{
	StructDiffOp *op = eaTail(&diff->ppOps);
	if (op) {
		F32 pyr[3];
		F32* quat = (F32*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, newstruct, 0, NULL);
		quatToPYR(quat, pyr);
		DEGVEC3(pyr);

		op->pOperand = MultiValCreate();
		MultiValSetVec3(op->pOperand, &pyr);
	}
}

void quatpyr_interp(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 interpParam)
{
	F32* quatA = (F32*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, structA, 0, NULL);
	F32* quatB = (F32*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, structB, 0, NULL);
	F32* quatDest = (F32*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, destStruct, 0, NULL);
    quatInterp(interpParam, quatA, quatB, quatDest);
}

void quatpyr_calcrate(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 deltaTime)
{
	Quat qInterpQuat;
	Quat qInverseA;
	F32* quatA = (F32*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, structA, 0, NULL);
	F32* quatB = (F32*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, structB, 0, NULL);
	F32* quatDest = (F32*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, destStruct, 0, NULL);

	// Convert it to angular velocity, and scale
	// Calculate interp quat by multiplying the inverse of a by b
	quatInverse(quatA, qInverseA);
	quatMultiply(quatB, qInverseA, qInterpQuat);
	quatToAxisAngle(qInterpQuat, quatDest, &quatDest[3]);
	quatDest[3] /= deltaTime;
}

void quatpyr_integrate(ParseTable tpi[], int column, void* valueStruct, void* rateStruct, void* destStruct, int index, F32 deltaTime)
{
	F32* quatV = (F32*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, valueStruct, 0, NULL);
	F32* quatR = (F32*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, rateStruct, 0, NULL);
	F32* quatDest = (F32*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, destStruct, 0, NULL);

	// Convert it from angular velocity, and scale
	Quat qIntegral;
	if (quatR[3] > 0.0 && axisAngleToQuat(quatR, quatR[3] * deltaTime, qIntegral) )
	{
		// axisAngleToQuat returns true only if it's a valid axis/angle, otherwise just copy
		quatMultiply(quatV, qIntegral, quatDest);
	}
	else
	{
		copyQuat(quatV, quatDest);
	}
}

void quatpyr_applydynop(ParseTable tpi[], int column, void* dstStruct, void* srcStruct, int index, DynOpType optype, const F32* values, U8 uiValuesSpecd, U32* seed)
{
	switch (optype)
	{
	case DynOpType_Jitter:
	{
		F32* qTarget = (F32*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, dstStruct, 0, NULL);
		Vec3 vRandom;
		Quat qTemp, qTemp2;
		randomVec3Seeded(seed, RandType_BLORN, vRandom);
		mulVecVec3(vRandom, values, vRandom);
		RADVEC3(vRandom);
		PYRToQuat(vRandom, qTemp);
		quatMultiply(qTarget, qTemp, qTemp2);
		copyQuat(qTemp2, qTarget);
	} break;
	case DynOpType_Inherit:
	{
		if (srcStruct)
		{
			F32* srcQuat = (F32*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, srcStruct, 0, NULL);
			F32* dstQuat = (F32*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, dstStruct, 0, NULL);
			Quat qTemp;
			quatMultiply((F32*)dstQuat, (F32*)srcQuat, qTemp);
			copyQuat(qTemp, (F32*)dstQuat);
		}
	} break;
	case DynOpType_Add:
	{
		Errorf("You can't add rotations, maybe you mean multiply?");
	} break;
	case DynOpType_Multiply:
	{
		F32* qTarget = (F32*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, dstStruct, 0, NULL);
		Vec3 vTemp;
		Quat qTemp, qTemp2;
		copyVec3(values, vTemp);
		RADVEC3(vTemp);
		PYRToQuat(vTemp, qTemp);
		quatMultiply(qTarget, qTemp, qTemp2);
		copyQuat(qTemp2, qTarget);
	} break;
	case DynOpType_Min:
	{
		Errorf("You can't get the min of a rotation, maybe you mean multiply?");
	} break;
	case DynOpType_Max:
	{
		Errorf("You can't get the max of a rotation, maybe you mean multiply?");
	} break;
	}
}

void vec3_applydynop(ParseTable tpi[], int column, void* dstStruct, void* srcStruct, int index, DynOpType optype, const F32* values, U8 uiValuesSpecd, U32* seed)
{
	switch (optype)
	{
	case DynOpType_SphereJitter:
	{
		F32* vTarget = (F32*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, dstStruct, 0, NULL);
		Vec3 vAdd;
		randomSphereSliceSeeded(seed, RandType_BLORN, RAD(values[0]), RAD(values[1]), values[2], vAdd);
		addVec3(vTarget, vAdd, vTarget);
	} break;
	case DynOpType_SphereShellJitter:
	{
		Vec3 vAdd;
		F32* vTarget = (F32*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, dstStruct, 0, NULL);
		randomSphereShellSliceSeeded(seed, RandType_BLORN, RAD(values[0]), values[1]/180.0f, values[2], vAdd);
		addVec3(vTarget, vAdd, vTarget);
	} break;

	// For most dynops, just treat it as a fixed array of F32s
	case DynOpType_Jitter:
	case DynOpType_Inherit:
	case DynOpType_Add:
	case DynOpType_Multiply:
	case DynOpType_Min:
	case DynOpType_Max:
	default:
		{
			int i;
			if (uiValuesSpecd >= 3) // values spread to components // changed so there is only one seed
			{
				for (i = 0; i < 3; i++)
					nonarray_applydynop(tpi, column, dstStruct, srcStruct, i, optype, values+i, uiValuesSpecd, seed);
			}
			else // same value & seed to each component
			{
				for (i = 0; i < 3; i++)
					nonarray_applydynop(tpi, column, dstStruct, srcStruct, i, optype, values, uiValuesSpecd, seed);
			}
		} break;
	}
}

int matpyr_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	int v;
	F32 pyr[3];
	Vec3* mat;

	for (v = 0; v < 3; v++)
	{
		char* nexttoken = GetNextParameter(tok, parseResult);
		if (!nexttoken) 
			return 0;
		pyr[v] = GetFloat(tok, nexttoken, parseResult);
	}

	mat = (Vec3*)TokenStoreGetPointer_inline(tpi, ptcc, column, structptr, 0, parseResult);
	RADVEC3(pyr);
	createMat3YPR(mat, pyr);
	return 0;
}

static void getMat3YPRSafe(const Mat3 mat, Vec3 pyr)
{
	if(	vec3IsZero(mat[0]) ||
		vec3IsZero(mat[1]) ||
		vec3IsZero(mat[2]))
	{
		setVec3(pyr, 0, 0, 0);
	}else{
		getMat3YPR(mat, pyr);
	}
}

bool matpyr_writetext(FILE* out, ParseTable tpi[], int column, void* structptr, int index, bool showname, bool ignoreInherited, int level, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	F32 pyr[3];
	Vec3* mat = (Vec3*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, 0, NULL);

	getMat3YPRSafe(mat, pyr);
	DEGVEC3(pyr);
	if (showname) WriteNString(out, tpi[column].name, ParseTableColumnNameLen(tpi,column), level+1, 0);
	WriteNString(out, " ",1, 0, 0);
	WriteFloat(out, pyr[0], 0, 0, tpi[column].subtable);
	WriteNString(out, ", ",2, 0, 0);
	WriteFloat(out, pyr[1], 0, 0, tpi[column].subtable);
	WriteNString(out, ", ",2, 0, 0);
	WriteFloat(out, pyr[2], 0, showname, tpi[column].subtable);

	return true;
}

void matpyr_writebdiff(StructDiff *diff, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, ObjectPath *parentPath, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool invertExcludeFlags, const char *caller_fname, int line)
{
	StructDiffOp *op = eaTail(&diff->ppOps);
	if (op) {
		F32 pyr[3];
		Vec3* mat = (Vec3*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, newstruct, 0, NULL);
		getMat3YPRSafe(mat, pyr);
		DEGVEC3(pyr);

		op->pOperand = MultiValCreate();
		MultiValSetVec3(op->pOperand, &pyr);
	}
}


TextParserResult matpyr_writebin(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int i;
	TextParserResult ok = PARSERESULT_SUCCESS;
	Vec3	pyr;
	Vec3* mat = (Vec3*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, 0, &ok);

	getMat3YPRSafe(mat, pyr);
	for (i = 0; i < 3; i++)
		SET_MIN_RESULT(ok, SimpleBufWriteF32(pyr[i], file));
	*datasum += sizeof(F32)*3;
	return ok;
}

TextParserResult matpyr_readbin(SimpleBufHandle file,  FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int i;
	Vec3	pyr;
	Vec3* mat = (Vec3*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, 0, &ok);

	for (i = 0; i < 3; i++)
	{
		if (!SimpleBufReadF32(pyr+i, file))
			SET_INVALID_RESULT(ok);
	}
	*datasum += sizeof(F32)*3;
	createMat3YPR(mat, pyr);
	return ok;
}

bool matpyr_senddiff(Packet* pak, ParseTable tpi[], int column, const void* oldstruct, const void* newstruct, int index, enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB)
{

	Vec3 newPyr;
	Vec3* newMat = (Vec3*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, newstruct, 0, NULL);

	getMat3YPRSafe(newMat, newPyr);


	if (oldstruct && (eFlags & SENDDIFF_FLAG_COMPAREBEFORESENDING))
	{
		Vec3 *oldMat = (Vec3*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, oldstruct, 0, NULL);
		Vec3 oldPyr;

		getMat3YPRSafe(oldMat, oldPyr);

		if (oldPyr[0] == newPyr[0] && oldPyr[1] == newPyr[1] && oldPyr[2] == newPyr[2])
		{
			return false;
		}
	}
	

	pktSendF32(pak, newPyr[0]);
	pktSendF32(pak, newPyr[1]);
	pktSendF32(pak, newPyr[2]);

	return true;
}

bool matpyr_recvdiff(Packet* pak, ParseTable tpi[], int column, void* structptr, int index, enumRecvDiffFlags eFlags)
{
	Vec3* mat = structptr? (Vec3*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, 0, NULL): 0;
	Vec3 pyr;

	pyr[0] = pktGetF32(pak);
	pyr[1] = pktGetF32(pak);
	pyr[2] = pktGetF32(pak);
	if (structptr)
		createMat3YPR(mat, pyr);

	return 1;
}

bool matpyr_bitpack(ParseTable tpi[], int column, const void *structptr, int index, PackedStructStream *pack)
{
	Vec3 *mat = (Vec3*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, 0, NULL);
	Vec3 pyr;
	int i;

	getMat3YPRSafe(mat, pyr);

	for (i = 0; i < 3; ++i)
	{
		if (!pyr[i])
		{
			bsWriteBits(pack->bs, 1, 0);
		}
		else
		{
			bsWriteBits(pack->bs, 1, 1);
			bsWriteF32(pack->bs, pyr[i]);
		}
	}

	// always need to call matpyr_unbitpack when unpacking, so return true here
	return true;
}

void matpyr_unbitpack(ParseTable tpi[], int column, void *structptr, int index, PackedStructStream *pack)
{
	Vec3* mat = (Vec3*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, 0, NULL);
	Vec3 pyr;
	int i;

	for (i = 0; i < 3; ++i)
	{
		if (bsReadBits(pack->bs, 1))
			pyr[i] = bsReadF32(pack->bs);
		else
			pyr[i] = 0;
	}
	
	createMat3YPR(mat, pyr);
}

void matpyr_updatecrc(ParseTable tpi[], int column, void *structptr, int index)
{
	int fa = TOK_GET_FLOAT_ROUNDING(tpi[column].type);//, i;
	Vec3 pyr;
	Vec3 *mat = (Vec3*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, 0, NULL);

	getMat3YPRSafe(mat, pyr);

	return; // CD: CRCing of matrix PYRs is inherently unstable

	/*
	for (i = 0; i < 3; ++i)
	{
		if (!pyr[i])
			pyr[i] = 0; // Replace -0 with 0

		if (fa)
		{
			int packed_value = ParserPackFloat(pyr[i], fa);
			cryptAdler32Update_AutoEndian((U8*)&packed_value, sizeof(int));
		} else {
			cryptAdler32Update_AutoEndian((U8*)&pyr[i], sizeof(F32));
		}
	}
	*/
}

int matpyr_compare(ParseTable tpi[], int column, const void* lhs, const void* rhs, int index, enumCompareFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int fa = TOK_GET_FLOAT_ROUNDING(tpi[column].type), i;
	Vec3 *mat_left = lhs||!(eFlags & COMPAREFLAG_NULLISDEFAULT)?TokenStoreGetPointer_inline(tpi, &tpi[column], column, lhs, 0, NULL):unitmat;
	Vec3 *mat_right = TokenStoreGetPointer_inline(tpi, &tpi[column], column, rhs, 0, NULL);
	Vec3 pyr_left, pyr_right;

	getMat3YPRSafe(mat_left, pyr_left);
	getMat3YPRSafe(mat_right, pyr_right);

	for (i = 0; i < 3; ++i)
	{
		F32 left = pyr_left[i];
		F32 right = pyr_right[i];

		if (!left)
			left = 0; // Replace -0 with 0
		if (!right)
			right = 0; // Replace -0 with 0
		if (fa)
		{
			int l = ParserPackFloat(left, fa);
			int r = ParserPackFloat(right, fa);
			if (l == r)
				continue;
			return l-r;
		}

//		if (eFlags & COMPAREFLAG_COMPARE_FLOATS_APPROXIMATELY)
		{
			if (left == 0)
			{
				if (right == 0)
				{
					continue;
				}

				if (ABS(right) < 0.01f)
				{
					continue;
				}
			}
			else if (right == 0)
			{
				if (ABS(left) < 0.01f)
				{
					continue;
				}
			}
			else
			{
				if (left > 0)
				{
					if (right > 0)
					{
						if (ABS(left / right - 1.0f) < 0.01f)
						{
							continue;
						}
					}
				}
				else if (left < 0)
				{
					if (right < 0)
					{
						if (ABS(left / right - 1.0f) < 0.01f)
						{
							continue;
						}
					}
				}
			}
		}

		if (left != right)
			return (left < right)? -1 : 1;
	}

	return 0;
}

TextParserResult matpyr_tomulti(ParseTable tpi[], int column, void* structptr, int index, MultiVal* result, bool bDuplicateData, bool dummyOnNULL)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	Vec3* mat;
	if(!structptr && dummyOnNULL)
	{
		devassertmsg(!bDuplicateData, "dummyOnNULL and bDuplicateData are currently not compatible");
		MultiValSetDummyType(result, MULTI_MAT4);
	}
	mat = (Vec3*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, 0, &ok);

	if (!RESULT_GOOD(ok)) return ok;

	if (bDuplicateData)
	{
		MultiValSetMat4(result, mat);
	}
	else
	{
		MultiValReferenceMat4(result, (Mat4*)(&mat));
	}
	return ok;
}

TextParserResult matpyr_frommulti(ParseTable tpi[], int column, void* structptr, int index, const MultiVal* value)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	Vec3* mat = (Vec3*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, 0, &ok);
	Mat4* src = MultiValGetMat4(value, 0);
	if (src && RESULT_GOOD(ok))
	{		
		copyMat4(*src, mat);
		return PARSERESULT_SUCCESS;
	}

	return PARSERESULT_ERROR;
}

void matpyr_interp(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 interpParam)
{
	Vec3* matA = (Vec3*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, structA, 0, NULL);
	Vec3* matB = (Vec3*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, structB, 0, NULL);
	Vec3* matDest = (Vec3*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, destStruct, 0, NULL);
	Vec3 pyrA, pyrB, pyrDest;

	getMat3YPRSafe(matA, pyrA);
	getMat3YPRSafe(matB, pyrB);
	pyrDest[0] = interpF32(interpParam, pyrA[0], pyrB[0]);
	pyrDest[1] = interpF32(interpParam, pyrA[1], pyrB[1]);
	pyrDest[2] = interpF32(interpParam, pyrA[2], pyrB[2]);
	createMat3YPR(matDest, pyrDest);
}

void matpyr_calcrate(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 deltaTime)
{
	Vec3* matA = (Vec3*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, structA, 0, NULL);
	Vec3* matB = (Vec3*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, structB, 0, NULL);
	Vec3* matDest = (Vec3*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, destStruct, 0, NULL);
	Vec3 pyrA, pyrB, pyrDest;

	getMat3YPRSafe(matA, pyrA);
	getMat3YPRSafe(matB, pyrB);
	pyrDest[0] = (pyrB[0] - pyrA[0]) / deltaTime;
	pyrDest[1] = (pyrB[1] - pyrA[1]) / deltaTime;
	pyrDest[2] = (pyrB[2] - pyrA[2]) / deltaTime;
	createMat3YPR(matDest, pyrDest);
}

void matpyr_integrate(ParseTable tpi[], int column, void* valueStruct, void* rateStruct, void* destStruct, int index, F32 deltaTime)
{
	Vec3* matV = (Vec3*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, valueStruct, 0, NULL);
	Vec3* matR = (Vec3*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, rateStruct, 0, NULL);
	Vec3* matDest = (Vec3*)TokenStoreGetPointer_inline(tpi, &tpi[column], column, destStruct, 0, NULL);
	Vec3 pyrV, pyrR, pyrDest;

	getMat3YPRSafe(matV, pyrV);
	getMat3YPRSafe(matR, pyrR);
	pyrDest[0] = pyrV[0] + deltaTime * pyrR[0];
	pyrDest[1] = pyrV[1] + deltaTime * pyrR[1];
	pyrDest[2] = pyrV[2] + deltaTime * pyrR[2];
	createMat3YPR(matDest, pyrDest);
}

int filename_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	char* nexttoken = GetNextParameter(tok,parseResult);
	if (!nexttoken) 
		return 0;

	TokenStoreSetString(tpi, column, structptr, index, forwardSlashes(nexttoken), NULL, NULL, parseResult, tok);
	return 0;
}

TextParserResult filename_readstring(ParseTable tpi[], int column, void* structptr, int index, const char* str, ReadStringFlags eFlags)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	if (str && str[0])
	{
		char *temp;
		strdup_alloca(temp, str);
		//_strupr_s(str, strlen(str)+1);
		TokenStoreSetString(tpi, column, structptr, index, forwardSlashes(temp), NULL, NULL, &ok, NULL);
	}
	else
	{
		TokenStoreFreeString(tpi, column, structptr, index, &ok);
	}
	return ok;
}

TextParserResult filename_writebin(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int wr = 0;
	TextParserResult ok = PARSERESULT_SUCCESS;
	const char* str = TokenStoreGetString_inline(tpi, &tpi[column], column, structptr, index, &ok);

	if (str && g_ccase_string_cache)
	{
		char temp[CRYPTIC_MAX_PATH];
		char *pTemp = temp;

		strcpy(temp, str);
		if (g_assert_verify_ccase_string_cache)
			assert(StringIsCCase(str)); // Should already be lowercase!
		StringToCCase(temp);

		wr = WritePascalString(file, temp);
	}
	else
	{
		wr = WritePascalString(file, str);
	}
	SET_MIN_RESULT(ok, wr);
	*datasum += wr;
	return ok;
}

TextParserResult currentfile_writebin(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int wr = 0;
	TextParserResult ok = PARSERESULT_SUCCESS;
	const char* str = TokenStoreGetString_inline(tpi, &tpi[column], column, structptr, index, &ok);

	if (!str || !str[0])
	{
		SimpleBufWriteU32(FILELIST_SPECIALINDEX_NULL, file);

		*datasum += 4;
		return ok;
	}
	else if (pFileList)
	{
		int iFileListIndex = FileListFindIndex(pFileList, str);
		if (iFileListIndex != -1)
		{
			SimpleBufWriteU32(iFileListIndex + FILELIST_INDEX_OFFSET, file);
			*datasum += 4;
			return ok;
		}
	}

	SimpleBufWriteU32(FILELIST_SPECIALINDEX_NOTINLIST, file);


	if (str && g_ccase_string_cache)
	{
		char temp[CRYPTIC_MAX_PATH];
		char *pTemp = temp;

		strcpy(temp, str);
		if (g_assert_verify_ccase_string_cache)
			assert(StringIsCCase(str)); // Should already be lowercase!
		StringToCCase(temp);

		wr = WritePascalString(file, temp);
	}
	else
	{
		wr = WritePascalString(file, str);
	}
	SET_MIN_RESULT(ok, wr);
	*datasum += wr + 4;
	return ok;
}



ParseInfoFieldUsage reference_interpretfield(ParseTable tpi[], int column, ParseInfoField field)
{
	switch (field)
	{
	case ParamField:
		if (tpi[column].type & TOK_FIXED_ARRAY)
			return NumberOfElements;
		if (tpi[column].type & TOK_EARRAY)
			return NoFieldUsage;
		return PointerToDefaultString;
	case SubtableField:
		return PointerToDictionaryName;
	}
	return NoFieldUsage;
}

bool reference_initstruct(ParseTable tpi[], int column, void* structptr, int index)
{
	char* str = (char*)tpi[column].param;
	if (!str) return false;

	TokenStoreSetRef(tpi, column, structptr, index, str, NULL, NULL);
	return true;

}

int reference_destroystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index)
{
	if (structptr)
	{
		TokenStoreClearRef(tpi, column, structptr, index, false, NULL);
	}
	return DESTROY_IF_NON_NULL;
}

int reference_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	char* nexttoken = GetNextParameter(tok, parseResult);
	if (!nexttoken) 
		return 0;

	if (!nexttoken[0])
	{
		TokenStoreClearRef(tpi, column, structptr, index, false, parseResult);
	}
	else
	{
		TokenStoreSetRef(tpi, column, structptr, index, nexttoken, parseResult, tok);
	}

	if ((ptcc->type & (TOK_NON_NULL_REF | TOK_NON_NULL_REF__ERROR_ONLY)) && 
		!gbIgnoreNonNullRefs && 
		!RefSystem_GetDictionaryIgnoreNullReferences(ptcc->subtable))
	{
        void *referent = (*(void**)((intptr_t)structptr + ptcc->storeoffset));
		if (referent == REFERENT_SET_BUT_ABSENT)
		{
			const char **ppSuggestions = NULL;
			char *pErrorString = NULL;
			char *pNameString = (char*) ptcc->subtable;
			const char *pDictName = RefSystem_GetDictionaryNameFromNameOrHandle(pNameString);

			RefSystem_GetSimilarNames(pNameString, nexttoken, 3, &ppSuggestions);

			estrPrintf(&pErrorString, "%s reference \"%s\" not found, for field %s.", pDictName, nexttoken, ptcc->name);

			if (eaSize(&ppSuggestions))
			{
				int i;

				estrConcatf(&pErrorString, " You may have meant one of: ");
				for (i=0; i < eaSize(&ppSuggestions); i++)
				{
					estrConcatf(&pErrorString, "%s%s ", ppSuggestions[i], i == eaSize(&ppSuggestions) - 1 ? "" : ",");
				}
			}
			else
			{
				estrConcatf(&pErrorString, " No alternate suggestions found.");
			}

			TokenizerErrorf(tok, "%s", pErrorString);

			estrDestroy(&pErrorString);
			eaDestroy(&ppSuggestions);
			if (parseResult)
			{
				if (ptcc->type & TOK_NON_NULL_REF)
				{
					MIN1(*parseResult, PARSERESULT_INVALID);
				}
				else
				{
					MIN1(*parseResult, PARSERESULT_ERROR);
				}
			}
		}
	}

	return 0;
}

bool reference_writetext(FILE* out, ParseTable tpi[], int column, void* structptr, int index, bool showname, bool ignoreInherited, int level, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	const char *buf;
	if (buf = TokenStoreGetRefString_inline(tpi, &tpi[column], column, structptr, index, NULL))
	{
		if (showname) WriteNString(out, tpi[column].name,ParseTableColumnNameLen(tpi,column), level+1, 0);
		WriteNString(out, " ",1, 0, 0);
		WriteQuotedString(out, buf, 0, showname);
		return true;
	}
	else if (tpi[column].type & TOK_REQUIRED || !showname)
	{
		if (showname) WriteNString(out, tpi[column].name,ParseTableColumnNameLen(tpi,column), level+1, 0);
		WriteNString(out, " \"\"",3, 0, showname);

		return !!(tpi[column].type & TOK_REQUIRED);
	}

	return false;
}

TextParserResult reference_writebin(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	const char *buf;
	int wr;
	TextParserResult ok = PARSERESULT_SUCCESS;
	if (buf = TokenStoreGetRefString_inline(tpi, &tpi[column], column, structptr, index, &ok))
	{
		wr = WritePascalString(file, buf);
		SET_MIN_RESULT(ok, wr);
		*datasum += wr;
	}
	else
	{
		wr = WritePascalString(file, "");
		SET_MIN_RESULT(ok, wr);
		*datasum += wr;
	}
	return ok;	
}

TextParserResult reference_readbin(SimpleBufHandle file, FileList *pFileList,  ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int re;
	TextParserResult ok = PARSERESULT_SUCCESS;
	char *pTempStr = NULL;

	estrStackCreate(&pTempStr);

	re = ReadPascalStringIntoEString(file, &pTempStr);
	if (!re) SET_INVALID_RESULT(ok);
	*datasum += re;
	TokenStoreSetRef(tpi, column, structptr, index, pTempStr, &ok, NULL);
	estrDestroy(&pTempStr);
	return ok;
}

void reference_writehdiff(char** estr, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, char *prefix, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	const char *buf;
	if (buf = TokenStoreGetRefString_inline(tpi, &tpi[column], column, newstruct, index, NULL))
	{
		WriteSetValueStringForTextDiff_ThroughQuote(estr, prefix, (int)strlen(prefix), caller_fname, line);
		if(buf && *buf!='\0' && eFlags & TEXTDIFFFLAG_ANNOTATE_REFERENCES)
		{
			estrAppendEscapedf_dbg(estr, caller_fname, line, "@%s[%s]", (char*)(tpi[column].subtable), buf);
		}
		else
		{
			estrAppendEscaped_dbg(estr, buf, caller_fname, line);
		}
		estrConcat_dbg_inline(estr, "\"\n", 2, caller_fname, line);

	}
	else 
	{
		WriteSetValueStringForTextDiff(estr, prefix, (int)strlen(prefix), NULL, 0, caller_fname, line);
	}
}

void reference_writebdiff(StructDiff *diff, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, ObjectPath *parentPath, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool invertExcludeFlags, const char *caller_fname, int line)
{
	StructDiffOp *op = eaTail(&diff->ppOps);
	const char *buf;
	if (op) {
		op->pOperand = MultiValCreate();
		if (buf = TokenStoreGetRefString_inline(tpi, &tpi[column], column, newstruct, index, NULL))
			MultiValSetString(op->pOperand, buf);
		else
			MultiValSetString(op->pOperand, "");
	}
}


bool reference_senddiff(Packet* pak, ParseTable tpi[], int column, const void* oldstruct, const void* newstruct, int index, enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB)
{

	const char *buf;

	if (oldstruct && (eFlags & SENDDIFF_FLAG_COMPAREBEFORESENDING) && reference_compare(tpi,column, oldstruct, newstruct, index, 0, 0, 0) == 0)
	{
		return false;
	}

	if (buf = TokenStoreGetRefString_inline(tpi, &tpi[column], column, newstruct, index, NULL))
	{
		pktSendString(pak, buf);
	}
	else
	{
		pktSendString(pak, "");
	}
	return true;
}

bool reference_recvdiff(Packet* pak, ParseTable tpi[], int column, void* structptr, int index, enumRecvDiffFlags eFlags)
{
	char* str;

	str = pktGetStringTemp(pak);

	RECV_CHECK_PAK(pak);

	if (structptr)
	{
		TokenStoreSetRef(tpi, column, structptr, index, str, NULL, NULL);
	}

	return 1;
}

bool reference_bitpack(ParseTable tpi[], int column, const void *structptr, int index, PackedStructStream *pack)
{
	const char *buf;
	if (buf = TokenStoreGetRefString_inline(tpi, &tpi[column], column, structptr, index, NULL)) {
		bsWriteString(pack->bs, buf);
		return true;
	} else {
		return false;
	}
}

void reference_unbitpack(ParseTable tpi[], int column, void *structptr, int index, PackedStructStream *pack)
{
	char *pEString = NULL;
	estrStackCreate(&pEString);
	bsReadString(pack->bs, &pEString);
	TokenStoreSetRef(tpi, column, structptr, index, pEString, NULL, NULL);
	estrDestroy(&pEString);
}


TextParserResult reference_writestring(ParseTable tpi[], int column, void* structptr, int index, char** estr, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *caller_fname, int line)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	const char *buf;
	buf = TokenStoreGetRefString_inline(tpi, &tpi[column], column, structptr, index, &ok);
	if (RESULT_GOOD(ok) && buf)
	{
		estrAppend2_dbg_inline(estr,buf, caller_fname, line);
	}
	return ok;
}

TextParserResult reference_readstring(ParseTable tpi[], int column, void* structptr, int index, const char* str, ReadStringFlags eFlags)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	TokenStoreSetRef(tpi, column, structptr, index, str, &ok, NULL);
	return ok;
}

TextParserResult reference_tomulti(ParseTable tpi[], int column, void* structptr, int index, MultiVal* result, bool bDuplicateData, bool dummyOnNULL)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	const char *buf;
	if(!structptr && dummyOnNULL)
	{
		MultiValSetDummyType(result, MULTI_STRING);
		return ok;
	}
	buf = TokenStoreGetRefString_inline(tpi, &tpi[column], column, structptr, index, &ok);
	buf = buf ? buf : "";
	if (RESULT_GOOD(ok) && buf)
	{	
		if (bDuplicateData)
		{
			MultiValSetString(result, buf);
		}
		else
		{
			MultiValReferenceString(result, buf);
		}
	}
	return ok;
}

TextParserResult reference_frommulti(ParseTable tpi[], int column, void* structptr, int index, const MultiVal* value)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	char* str = (char*)MultiValGetString(value, 0);
	TokenStoreSetRef(tpi, column, structptr, index, str, &ok, NULL);
	return ok;	
}

void reference_updatecrc(ParseTable tpi[], int column, void* structptr, int index)
{
	int zero = 0;
	const char *buf;
	buf = TokenStoreGetRefString_inline(tpi, &tpi[column], column, structptr, index, NULL);
	if (buf && buf[0])
	{
		ANALYSIS_ASSUME(buf != NULL);
		cryptAdler32Update_IgnoreCase(buf, (int)strlen(buf));
	}
	else
		cryptAdler32Update((U8*)&zero, sizeof(int));
}

int reference_compare(ParseTable tpi[], int column, const void* lhs, const void* rhs, int index, enumCompareFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	const char *left = NULL;
	const char *right = NULL;

	if(lhs && rhs)
	{
		void** leftptr = (void**)((intptr_t)lhs + tpi[column].storeoffset);
		void** rightptr = (void**)((intptr_t)rhs + tpi[column].storeoffset);

		if(*leftptr == *rightptr && (*leftptr != REFERENT_SET_BUT_ABSENT))
			return 0;
	}

	if (lhs || !(eFlags & COMPAREFLAG_NULLISDEFAULT))
	{
		left = TokenStoreGetRefString_inline(tpi, &tpi[column], column, lhs, index, NULL);
	}

	right = TokenStoreGetRefString_inline(tpi, &tpi[column], column, rhs, index, NULL);
	if (!left) left = "";
	if (!right) right = "";
	return stricmp(left, right);
}

void reference_calcoffset(ParseTable tpi[], int column, size_t* size)
{
	MEM_ALIGN(*size, sizeof(ReferenceHandle));
	tpi[column].storeoffset = *size;
	(*size) += sizeof(ReferenceHandle);
}

void reference_copystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData)
{
	TokenStoreSetPointer_inline(tpi, ptcc, column, dest, index,NULL, NULL);
	TokenStoreCopyRef(tpi, column, dest, src, index, NULL);
}

void reference_copyfield(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TokenStoreCopyRef(tpi, column, dest, src, index, NULL);
}

enumCopy2TpiResult reference_copyfield2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest,  
	CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, char **ppResultString)
{
	TokenStoreCopyRef2tpis(src_tpi, src_column, src_index, src, dest_tpi, dest_column, dest_index, dest, NULL);
	return COPY2TPIRESULT_SUCCESS;
}

int reference_checksharedmemory(ParseTable tpi[], int column, void* structptr, int index)
{
	void* ptr = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	if (ptr && ptr != REFERENT_SET_BUT_ABSENT && !isSharedMemory(ptr))
	{
		const char *structName;
		if (!(structName = ParserGetTableName(tpi)))
		{
			structName = "UNKNOWN";
		}
		assertmsgf(0,"Element %s (index %d) in struct %s is not pointing to valid shared memory! This will crash when loaded from shared memory. ",tpi[column].name,index,structName);

		return 0;
	}
	return 1;
}

//appends either ! or <&refdata&> to ppFixupData
void reference_preparesharedmemoryforfixup(ParseTable tpi[], int column,void* structptr, int index, char **ppFixupData)
{
	ReferenceHandle *pHandle = TokenStoreGetRefHandlePointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	const char *pString = NULL;
	char *pEscapedString = NULL;

	assertmsgf(GetCurrentThreadId() == gTextParserRefSystemThread, "TextParser trying to access the reference system from a disallowed thread");

	if (!pHandle)
	{
		return;
	}

	if (!RefSystem_IsHandleActive(pHandle))
	{
		estrAppend2_dbg_inline(ppFixupData, "!", TEMP_ESTRING_CHARGE);
		return;
	}


	pString = RefSystem_StringFromHandle(pHandle);

	estrStackCreateSize(&pEscapedString, (int)strlen(pString) * 2 + 5);

	TokenizerEscape(pEscapedString, pString);
	estrForceSize_dbg(&pEscapedString, (int)strlen(pEscapedString), TEMP_ESTRING_CHARGE);

	estrAppend_dbg(ppFixupData, &pEscapedString, TEMP_ESTRING_CHARGE);

	estrDestroy(&pEscapedString);
}

int g_total_fixup,g_null_fixup;

void reference_fixupsharedmemory(ParseTable tpi[], int column,void* structptr, int index, char **ppFixupData)
{
	void *pReferent;
	const char *pDictName;
	ReferenceHandle *pHandle = TokenStoreGetRefHandlePointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	char *pEndOfString;
	char *pUnescapedString = NULL;
	int iLen;

	if (!pHandle)
	{
		return;
	}

	if (**ppFixupData == '!')
	{
		assert(*pHandle == NULL); // we can't modify it here, but it should already be null
		(*ppFixupData)++;
		return;
	}

	g_total_fixup++;
	pEndOfString = strstr(*ppFixupData, "&>");
	assertmsgf(pEndOfString, "ppFixupData string badly formatted:%s\n", *ppFixupData);

	iLen = pEndOfString - *ppFixupData;
	estrStackCreateSize(&pUnescapedString, iLen);
	TokenizerUnescape(pUnescapedString, *ppFixupData);
	estrForceSize_dbg(&pUnescapedString, (int)strlen(pUnescapedString), TEMP_ESTRING_CHARGE);

	pDictName = tpi[column].subtable;
	if (!(pReferent = RefSystem_ReferentFromString(pDictName, pUnescapedString)))
	{
		pDictName = RefSystem_GetDictionaryNameFromNameOrHandle(pDictName);
		assertmsgf(pDictName, "Can't find dictionary named %s while doing shared memory fixup", pDictName);
		pReferent = RefSystem_ReferentFromString(pDictName, pUnescapedString);
		g_null_fixup++;
	}
	if (!(pReferent == *pHandle || pReferent == NULL && *pHandle == REFERENT_SET_BUT_ABSENT))
	{
		if (stringCacheSharedMemoryChunkForDict(pDictName))
		{
			ResourceDictionary *pDictionary = resGetDictionary(pDictName);
			// Try to preset the reference
			resEditStartDictionaryModification(pDictName);
			assert(resEditSetWorkingCopy(pDictionary, pUnescapedString, *pHandle));
		}
		else
		{
			// If this assert gets hit, the dictionary it is referring to was NOT properly registered as being in shared memory
			// It is 100% absolutely required that TokenStoreSetRef be called on all references, 
			// as otherwise random data corruption will occur
			assertmsgf(0,"Trying to modify locked shared memory reference to dictionary %s", pDictName);
		}
	}

	TokenStoreSetRef(tpi, column, structptr, index, pUnescapedString, NULL, NULL);	

	estrDestroy(&pUnescapedString);

	*ppFixupData += iLen + 2;
}

void reference_queryCopyStruct(ParseTable *pTPI, int iColumn, int iIndex, int iOffsetIntoParentStruct, 
	CopyQueryFlags eQueryFlags, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, 
	StructTypeField iOptionFlagsToExclude, void *pUserData)
{
	//fixed arrays of references don't exist
	assert(!(eQueryFlags & COPYQUERYFLAG_IN_FIXED_ARRAY));

	StructCopyQueryResult_MarkBytes(false, iOffsetIntoParentStruct, sizeof(ReferenceHandle), pUserData);

	if (!(eQueryFlags & COPYQUERYFLAG_YOU_ARE_FLAGGED_OUT))
	{
		StructCopyQueryResult_NeedCallback(pUserData);
	}
}

int functioncall_destroystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index)
{
	if (structptr)
	{
		StructFunctionCall* callstruct = TokenStoreGetPointer_inline(tpi, ptcc, column, structptr, index, NULL);
		if (callstruct) StructFreeFunctionCall(callstruct);
	}
	return DESTROY_IF_NON_NULL;
}

int functioncall_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	StructFunctionCall*** store = (StructFunctionCall***)TokenStoreGetEArray_inline(tpi, ptcc, column, structptr, parseResult);
	TokenizerParseFunctionCall(tok, store, tpi, parseResult);
	return 0;
}

bool functioncall_writetext(FILE* out, ParseTable tpi[], int column, void* structptr, int index, bool showname, bool ignoreInherited, int level, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	StructFunctionCall** ea = *(StructFunctionCall***)TokenStoreGetEArray_inline(tpi, &tpi[column], column, structptr, NULL);
	if (eaSize(&ea))
	{
		if (showname) WriteNString(out, tpi[column].name,ParseTableColumnNameLen(tpi,column), level+1, 0);
		WriteNString(out, " ",1, 0, 0);
		WriteTextFunctionCalls(out, ea);
		if (showname) WriteNString(out, NULL, 0, 0, 1);
		return true;
	}
	else if (tpi[column].type & TOK_REQUIRED)
	{
		Errorf("REQUIRED functioncall field is NULL");
	}

	return false;
}

TextParserResult functioncall_writebin(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	StructFunctionCall** ea = *(StructFunctionCall***)TokenStoreGetEArray_inline(tpi, &tpi[column], column, structptr, &ok);
	return MIN_RESULT(ok, WriteBinaryFunctionCalls(file, ea, datasum));
}

TextParserResult functioncall_readbin(SimpleBufHandle file,  FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	StructFunctionCall*** ea = (StructFunctionCall***)TokenStoreGetEArray_inline(tpi, &tpi[column], column, structptr, &ok);
	return MIN_RESULT(ok, ReadBinaryFunctionCalls(file, ea, datasum, tpi));
}

bool functioncall_senddiff(Packet* pak, ParseTable tpi[], int column, const void* oldstruct, const void* newstruct, int index, enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB)
{
	StructFunctionCall **ea;

	if (oldstruct && (eFlags & SENDDIFF_FLAG_COMPAREBEFORESENDING) && functioncall_compare(tpi,column, oldstruct, newstruct, index, 0, 0, 0) == 0)
	{
		return false;
	}

	ea = *(StructFunctionCall***)TokenStoreGetEArray_inline(tpi, &tpi[column], column, newstruct, NULL);
	SendDiffFunctionCalls(pak, ea);
	return true;
}

bool functioncall_recvdiff(Packet* pak, ParseTable tpi[], int column, void* structptr, int index, enumRecvDiffFlags eFlags)
{
	StructFunctionCall*** ea = (StructFunctionCall***)TokenStoreGetEArray_inline(tpi, &tpi[column], column, structptr, NULL);
	return RecvDiffFunctionCalls(pak, ea, tpi, eFlags);
}

static void BitpackFunctionCalls(PackedStructStream *pack, StructFunctionCall** structarray)
{
	int i, n = eaSize(&structarray);
	bsWriteBitsPack(pack->bs, 1, n);
	for (i = 0; i < n; i++)
	{
		bsWriteString(pack->bs, structarray[i]->function);
		BitpackFunctionCalls(pack, structarray[i]->params);
	}
}
bool functioncall_bitpack(ParseTable tpi[], int column, const void* structptr, int index, PackedStructStream *pack)
{
	StructFunctionCall** ea = *(StructFunctionCall***)TokenStoreGetEArray_inline(tpi, &tpi[column], column, structptr, NULL);
	BitpackFunctionCalls(pack, ea);
	return true;
}

static void UnbitpackFunctionCalls(PackedStructStream *pack, StructFunctionCall*** structarray, ParseTable tpi[])
{
	const char *pTPIName = ParserGetTableName(tpi);
	int i, n;
	char *pEString = NULL;


	assert(!eaSize(structarray));
	// destroy any existing data
	for (i = 0; i < eaSize(structarray); i++)
		StructFreeFunctionCall((*structarray)[i]);

	n = bsReadBitsPack(pack->bs, 1);
	if (n == 0)
	{
		eaDestroy(structarray);
		return;
	}

	estrStackCreate(&pEString);

	eaSetSize_dbg(structarray, n, pTPIName?pTPIName:__FILE__, pTPIName?LINENUM_FOR_EARRAYS:__LINE__);
	for (i = 0; i < n; i++)
	{
		(*structarray)[i] = StructAllocRawCharged(sizeof(StructFunctionCall), tpi);
		if (bsReadString(pack->bs, &pEString))
		{
				(*structarray)[i]->function = StructAllocString_dbg(pEString, pTPIName?pTPIName:__FILE__, pTPIName?LINENUM_FOR_EARRAYS:__LINE__);
		}
		UnbitpackFunctionCalls(pack, &(*structarray)[i]->params, tpi);
	}
	estrDestroy(&pEString);
}
void functioncall_unbitpack(ParseTable tpi[], int column, void *structptr, int index, PackedStructStream *pack)
{
	StructFunctionCall*** ea = (StructFunctionCall***)TokenStoreGetEArray_inline(tpi, &tpi[column], column, structptr, NULL);
	UnbitpackFunctionCalls(pack, ea, tpi);
}

void functioncall_updatecrc(ParseTable tpi[], int column, void* structptr, int index)
{
	StructFunctionCall* call = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	ParserUpdateCRCFunctionCall(call);
}

int CompareFunctionCalls(StructFunctionCall* left, StructFunctionCall* right)
{
	int ret = 0;
	int i, lcount, rcount;

	if (!left && !right) return 0;
	if (!left) return -1;
	if (!right) return 1;

	ret = stricmp(left->function, right->function);
	if (ret) return ret;

	lcount = eaSize(&left->params);
	rcount = eaSize(&right->params);
	if (lcount != rcount)
		return lcount - rcount;

	for (i = 0; i < lcount && ret == 0; i++)
		ret = CompareFunctionCalls(left->params[i], right->params[i]);
	return ret;
}

int functioncall_compare(ParseTable tpi[], int column, const void* lhs, const void* rhs, int index, enumCompareFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	StructFunctionCall* left = lhs||!(eFlags & COMPAREFLAG_NULLISDEFAULT)?TokenStoreGetPointer_inline(tpi, &tpi[column], column, lhs, index, NULL):NULL;
	StructFunctionCall* right = TokenStoreGetPointer_inline(tpi, &tpi[column], column, rhs, index, NULL);
	return CompareFunctionCalls(left, right);
}

size_t functioncall_memusage(ParseTable tpi[], int column, void* structptr, int index, bool bAbsoluteUsage)
{
	StructFunctionCall* callstruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	return ParserGetFunctionCallMemoryUsage(callstruct, bAbsoluteUsage);
}

void functioncall_copystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData)
{
	StructFunctionCall* fromstruct = TokenStoreGetPointer_inline(tpi, ptcc, column, src, index, NULL);
	TokenStoreSetPointer_inline(tpi, ptcc, column, dest, index, ParserCompressFunctionCall(fromstruct, memAllocator, customData, tpi), NULL);
}

void functioncall_copystruct2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest, CustomMemoryAllocator memAllocator, void* customData)
{
	StructFunctionCall* fromstruct = TokenStoreGetPointer_inline(src_tpi, &src_tpi[src_column], src_column, src, src_index, NULL);
	TokenStoreSetPointer_inline(dest_tpi, &dest_tpi[dest_column], dest_column, dest, dest_index, ParserCompressFunctionCall(fromstruct, memAllocator, customData, src_tpi), NULL);
}

void functioncall_copyfield(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	functioncall_destroystruct(tpi, ptcc, column, dest, index);
	functioncall_copystruct(tpi, ptcc, column, dest, src, index, memAllocator, customData);
}

enumCopy2TpiResult functioncall_copyfield2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest, 
	CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, char **ppResultString)
{
	functioncall_destroystruct(dest_tpi, &dest_tpi[dest_column], dest_column, dest, dest_index);
	functioncall_copystruct2tpis(src_tpi, src_column, src_index, src, dest_tpi, dest_column, dest_index, dest, memAllocator, customData);
	return COPY2TPIRESULT_SUCCESS;
}

void functioncall_queryCopyStruct(ParseTable *pTPI, int iColumn, int iIndex, int iOffsetIntoParentStruct, 
	CopyQueryFlags eQueryFlags, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, 
	StructTypeField iOptionFlagsToExclude, void *pUserData)
{
	assertmsg(0, "Function calls must always be in EArrays, I thought.");
}

void CopyFunctionCallEArray(const char *pStructName, StructFunctionCall ***pppDest, StructFunctionCall ***pppSrc)
{
	int iSrcSize = eaSize(pppSrc);
	int iDestSize = eaSize(pppDest);
	int iSharedSize = MIN(iSrcSize, iDestSize);
	int i;

	for (i=0; i < iSharedSize; i++)
	{
		StructFunctionCall *pDest = (*pppDest)[i];
		StructFunctionCall *pSrc = (*pppSrc)[i];

		if (!(pDest->function && pSrc->function))
		{
			ANALYSIS_ASSUME(pDest->function != NULL);
			ANALYSIS_ASSUME(pSrc->function != NULL);
			if (strcmp(pDest->function, pSrc->function) != 0)
			{
				if (pDest->function)
				{
					free(pDest->function);
				}

				if (pSrc->function)
				{
					pDest->function = strdup_dbg(pSrc->function TPC_MEMDEBUG_PARAMS_STRING);
				}
				else
				{
					pDest->function = NULL;
				}

				CopyFunctionCallEArray(pStructName, &pDest->params, &pSrc->params);
			}
		}
	}

	for (i=iSharedSize; i < iSrcSize; i++)
	{
		StructFunctionCall *pSrc = (*pppSrc)[i];
		StructFunctionCall *pNew = calloc(sizeof(StructFunctionCall), 1);
		pNew->function = strdup_dbg(pSrc->function TPC_MEMDEBUG_PARAMS_STRING);
		CopyFunctionCallEArray(pStructName, &pNew->params, &pSrc->params);
		eaPush_dbg(pppDest, pNew TPC_MEMDEBUG_PARAMS_EARRAY);
	}

	for (i=iDestSize - 1; i >= iSharedSize; i--)
	{
		StructFreeFunctionCall((*pppDest)[i]);
		eaPop(pppDest);
	}
}
		


void functioncall_earrayCopy(ParseTable tpi[], int column, void* dest, void* src, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	void ***pppSrcEarray = TokenStoreGetEArray_inline(tpi, &tpi[column], column, src, NULL);
	void ***pppDestEarray = TokenStoreGetEArray_inline(tpi, &tpi[column], column, dest, NULL);

	CopyFunctionCallEArray(ParserGetTableName(tpi), (StructFunctionCall ***)pppDestEarray, (StructFunctionCall ***)pppSrcEarray);
}

ParseInfoFieldUsage struct_interpretfield(ParseTable tpi[], int column, ParseInfoField field)
{
	switch (field)
	{
	case ParamField:
		return SizeOfSubstruct;
	case SubtableField:
		return PointerToSubtable;
	}
	return NoFieldUsage;
}


bool struct_initstruct(ParseTable tpi[], int column, void* structptr, int index)
{
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	if (substruct) // if we already have a pointer, we're embedded, and should init
	{
		StructInitVoid(tpi[column].subtable, substruct);


		//this is basically irrelevant because of how StructInitInfos work... they handle
		//embedded structs already
		return true;
	}
	else
	{
		U32 storage = TokenStoreGetStorageType(tpi[column].type);

		if ((storage == TOK_STORAGE_INDIRECT_SINGLE) && (tpi[column].type & TOK_ALWAYS_ALLOC)) // optional struct
		{
			substruct = TokenStoreAlloc(tpi, column, structptr, index, tpi[column].param, NULL, NULL, NULL);
			StructInitVoid(tpi[column].subtable, substruct);
			return true;
		}

		return false;
	}

	
}

int struct_destroystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index)
{
	if (structptr)
	{
		void* substruct = TokenStoreGetPointer_inline(tpi, ptcc, column, structptr, index, NULL);
		if (substruct)
		{
			StructDeInitVoid(ptcc->subtable, substruct);
			TokenStoreFreeWithTPI(tpi, column, structptr, index, NULL, ptcc->subtable);
		}
	}

	return DESTROY_IF_NON_NULL;
}

int struct_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	int newIndex = TokenStoreGetNumElems_inline(tpi, ptcc, column, structptr, parseResult);
	void* substruct = TokenStoreAlloc(tpi, column, structptr, newIndex, ptcc->param, NULL, NULL, parseResult);
	U32 storage = TokenStoreGetStorageType(ptcc->type);
	void *subtable = ptcc->subtable;
	
	//special case... do not StructInit an embedded struct, because it has already been structInitted by its
	//parent, and in fact may have default values in it due to its parent's constructor that we don't want to have
	//overwritten
	if (storage != TOK_STORAGE_DIRECT_SINGLE)
	{
		StructInitWithCommentVoid(subtable, substruct, "Allocated in struct_parse as part of textparser");
	}

	ParserReadTokenizer(tok, subtable, substruct, false, parseResult);

	if (*parseResult == PARSERESULT_INVALID)
	{
		StructDeInitVoid(subtable, substruct);
		TokenStoreFreeWithTPI(tpi, column, structptr, newIndex, NULL, subtable);
		TokenStoreRemoveElement(tpi, column, structptr, newIndex, NULL);
		*parseResult = PARSERESULT_ERROR;
	}
	return 0;
}

bool is_single_line_struct(ParseTable pti[], StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int i;

	if (!pti)
		return false;

	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_REDUNDANTNAME) continue;
		if (pti[i].type & TOK_STRUCTPARAM) continue;
		if (TOK_GET_TYPE(pti[i].type) == TOK_IGNORE) continue;
		if (TOK_GET_TYPE(pti[i].type) == TOK_CURRENTFILE_X) continue;
		if (!pti[i].name || !pti[i].name[0]) continue; // unnamed fields shouldn't be parsed or written
		if (!FlagsMatchAll(pti[i].type,iOptionFlagsToMatch)) continue;
		if (!FlagsMatchNone(pti[i].type,iOptionFlagsToExclude)) continue;

		if (TOK_GET_TYPE(pti[i].type) == TOK_END)
		{
			if (pti[i].name && pti[i].name[0] == '\n' && !pti[i].name[1])
				continue;
		}
		return false;
	}
	return true;
}

bool struct_writetext(FILE* out, ParseTable tpi[], int column, void* structptr, int index, bool showname, bool ignoreInherited, int level, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int i, numelems = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structptr, NULL);
	bool child_is_single_line;
	bool isDirectEmbedded = (TokenStoreGetStorageType(tpi[column].type) == TOK_STORAGE_DIRECT_SINGLE);
	char *pSpecialFormatString = NULL;

	//special case for writign certain structs for 
	if ((iWriteTextFlags & WRITETEXTFLAG_WRITINGFORHTML) && GetStringFromTPIFormatString(&tpi[column], "HTML_CONVERT_OPT_STRUCT_TO_KEY_STRING", &pSpecialFormatString))
	{
		void *pSubStruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, 0, NULL);
		const char *pKeyString;
		char *pOutString = NULL;

		if (!pSubStruct)
		{
			return false;
		}

		pKeyString = ParserGetStructName(tpi[column].subtable, pSubStruct);

		if (!estrSuperUnescapeString(&pOutString, pSpecialFormatString))
		{
			estrCopy2(&pOutString, pSpecialFormatString);
		}

		estrReplaceOccurrences(&pOutString, "$KEY$", pKeyString);

		if (showname) WriteNString(out, tpi[column].name,ParseTableColumnNameLen(tpi,column), level+1, 0);
	
		WriteNString(out, " ", 1, 0, 0);

		WriteQuotedString(out, pOutString, 0, showname);

		estrDestroy(&pOutString);
		return true;
		
	}

	iOptionFlagsToExclude |= TOK_NO_WRITE;

	if (!numelems && (tpi[column].type & TOK_REQUIRED))
	{
		Errorf("REQUIRED struct field is NULL");
	}

	if (!numelems)
	{
		return false;
	}

	child_is_single_line = is_single_line_struct(tpi[column].subtable, iOptionFlagsToMatch, iOptionFlagsToExclude);

	for (i = 0; i < numelems; i++)
	{
		void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, i, NULL);
		if (substruct)
		{
			size_t iOffsetBeforeName = 0;
			size_t iOffsetAfterName = 0;

			//for direct embedded structs, we don't want to write out the name if we end up writing out no actual data
			if (isDirectEmbedded)
			{
				iOffsetBeforeName = ftell(out);
			}

			if (showname)
			{
				if (!child_is_single_line)
					WriteNString(out, NULL, 0, 0, 1);
				WriteNString(out, tpi[column].name, ParseTableColumnNameLen(tpi,column), level+1, 0);
			}

			if (isDirectEmbedded)
			{
				iOffsetAfterName = ftell(out);
			}			
	
			InnerWriteTextFile(out, tpi[column].subtable, substruct, level+1, ignoreInherited, iWriteTextFlags | (isDirectEmbedded ? (WRITETEXTFLAG_DONTWRITEENDTOKENIFNOTHINGELSEWRITTEN | WRITETEXTFLAG_DONTWRITEEOLIFNOTHINGELSEWRITTEN | WRITETEXTFLAG_DIRECTEMBED) : 0), iOptionFlagsToMatch, iOptionFlagsToExclude);

			if (isDirectEmbedded)
			{
				size_t iFinalOffset = ftell(out);

				if (iFinalOffset == iOffsetAfterName)
				{
					fseek_and_set(out, iOffsetBeforeName, ' ');
				}
			}
		}
	}

	return true;
}

TextParserResult struct_writebin(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int subsum = 0;
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, &ok);
	U32 storage = TokenStoreGetStorageType(tpi[column].type);

	iOptionFlagsToExclude |= TOK_NO_WRITE;


	if (storage == TOK_STORAGE_INDIRECT_SINGLE) // optional struct
	{
		int hassub = substruct? 1: 0;
		SET_MIN_RESULT(ok, SimpleBufWriteU32(hassub, file));
		*datasum += sizeof(int);
		if (!hassub) return ok;
	}
	else devassert(substruct);

	SET_MIN_RESULT(ok, ParserWriteBinaryTable(file, pLayoutFile, pFileList, tpi[column].subtable, substruct, &subsum, iOptionFlagsToMatch, iOptionFlagsToExclude));
	*datasum += subsum;
	return ok;
}

TextParserResult struct_readbin(SimpleBufHandle file,  FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int subsum = 0;
	void* substruct;
	U32 storage = TokenStoreGetStorageType(tpi[column].type);

	if (storage == TOK_STORAGE_INDIRECT_SINGLE) // optional struct
	{
		U32 hassub = 0;
		if (!SimpleBufReadU32(&hassub, file))
			SET_INVALID_RESULT(ok);
		*datasum += sizeof(int);
		if (!hassub || ok != PARSERESULT_SUCCESS)
			return ok;
	}

	// if here, then we have a struct
	substruct = TokenStoreAlloc(tpi, column, structptr, index, tpi[column].param, NULL, NULL, &ok);

	SET_MIN_RESULT(ok,ParserReadBinaryTableEx(file, pFileList, tpi[column].subtable, substruct, &subsum,  iOptionFlagsToMatch,  iOptionFlagsToExclude));
	*datasum += subsum;
	return ok;
}

void struct_writehdiff(char** estr, ParseTable basetpi[], int column, void* oldstruct, void* newstruct, int index, char * prefix, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	void* oldp = oldstruct? TokenStoreGetPointer_inline(basetpi, &basetpi[column], column, oldstruct, index, NULL): 0;
	void* newp = TokenStoreGetPointer_inline(basetpi, &basetpi[column], column, newstruct, index, NULL);
	ParseTable *tpi = basetpi[column].subtable;
	bool printCreate = !!(basetpi[column].type & TOK_INDIRECT);

	if ((basetpi[column].type & TOK_UNOWNED) && (eFlags & TEXTDIFFFLAG_ONLYKEYSFROMUNOWNED))
	{
		iOptionFlagsToMatch |= TOK_KEY;
	}

	if ((eFlags & TEXTIDFFFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT) && !FlagsMatchAll(basetpi[column].type,iOptionFlagsToMatch | iOptionFlagsToExclude))
	{
		// We got here on invert exclude flags that didn't totally match
		printCreate = false;
	}
	
	StructWriteTextDiffInternal(estr,tpi,oldp,newp,prefix,iOptionFlagsToMatch,iOptionFlagsToExclude,eFlags, printCreate,false, caller_fname, line);
}

void struct_writebdiff(StructDiff *diff, ParseTable parenttpi[], int column, void* oldstruct, void* newstruct, int index, ObjectPath *parentPath, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool invertExcludeFlags, const char *caller_fname, int line)
{
	void* oldp;
	void* newp; 
	ParseTable *ntpi;
	ParseTable *otpi;
	ObjectPathSegment *seg = ObjectPathTail(parentPath);
	bool descend = seg->descend;
	bool ok = true;
	StructDiffOp *op = eaPop(&diff->ppOps);
	seg->descend = true;
	if (op) StructDestroyDiffOp(&op);
	
	if (!ParserResolvePathSegment(seg, parenttpi, oldstruct, &otpi, &oldp, NULL, NULL, NULL, iOptionFlagsToMatch))
	{
		oldp = NULL;
		otpi = NULL;
	}
	if (!ParserResolvePathSegment(seg, parenttpi, newstruct, &ntpi, &newp, NULL, NULL, NULL, iOptionFlagsToMatch))
	{
		newp = NULL;
		ntpi = otpi;
	}

	if (!ntpi)
		assert(false);

	seg->descend = descend;

	StructMakeDiffInternal(diff, ntpi, oldp, newp, parentPath, iOptionFlagsToMatch, iOptionFlagsToExclude, invertExcludeFlags, false, caller_fname, line);
}

bool struct_senddiff(Packet* pak, ParseTable tpi[], int column, const void* oldstruct, const void* newstruct, int index, enumSendDiffFlags eFlags, 
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB)
{
	void* oldp = oldstruct? TokenStoreGetPointer_inline(tpi, &tpi[column], column, oldstruct, index, NULL): 0;
	void* newp = TokenStoreGetPointer_inline(tpi, &tpi[column], column, newstruct, index, NULL);

	int allowDiffs;
	enumSendDiffFlags eRecurseFlags;

	iOptionFlagsToExclude |= TOK_NO_NETSEND;


	allowDiffs = (eFlags & SENDDIFF_FLAG_ALLOWDIFFS) && !(eFlags & SENDDIFF_FLAG_FORCEPACKALL) && oldp;
	eRecurseFlags = (eFlags & ~SENDDIFF_FLAG_ALLOWDIFFS) | (allowDiffs ? SENDDIFF_FLAG_ALLOWDIFFS : 0);

	if (newp)
	{
		pktSendBits(pak, 1, 1);
		return ParserSend(tpi[column].subtable, pak, oldp, newp, eRecurseFlags, iOptionFlagsToMatch, iOptionFlagsToExclude, pGenerateFlagsCB) || !oldp;
	}
	else
	{
		pktSendBits(pak, 1, 0);
		return (oldp != NULL);
	}
}

bool struct_recvdiff(Packet* pak, ParseTable tpi[], int column, void* structptr, int index, enumRecvDiffFlags eFlags)
{
	ParseTable *ptcc = &tpi[column];
	void* substruct = structptr? TokenStoreGetPointer_inline(tpi, ptcc, column, structptr, index, NULL): 0;
	int gettingstruct = pktGetBits(pak, 1);
	if (!gettingstruct)
	{
		if (structptr) struct_destroystruct(tpi, ptcc, column, structptr, index);
		return 1;
	}

	// now we're getting a struct
	if (!substruct && structptr) 
	{
		substruct = TokenStoreAlloc(tpi, column, structptr, index, ptcc->param, NULL, NULL, NULL);

		if (eFlags & RECVDIFF_FLAG_GET_GLOBAL_CREATION_COMMENT)
		{
			StructInitWithCommentVoid(ptcc->subtable, substruct, TextParser_GetGlobalStructCreationComment());
		}
		else
		{
			StructInitVoid(ptcc->subtable, substruct);
		}

	}

	return ParserRecv(ptcc->subtable, pak, substruct, eFlags);
}

bool struct_bitpack(ParseTable tpi[], int column, const void *structptr, int index, PackedStructStream *pack)
{
	void* newp = structptr? TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL): 0;

	bsWriteBits(pack->bs, 1, newp? 1: 0);

	if (newp)
	{
		// return
		StructBitpackSub(tpi[column].subtable, newp, pack);
		return true; // Need to send the bit saying there's a default struct, not a NULL (for optional structs)
	} else {
		return false; //true; // Need to send the bit saying the struct is NULL, not a default struct (presumably for EArrays with NULLs, not optional structs?)
	}
}

void struct_unbitpack(ParseTable tpi[], int column, void *structptr, int index, PackedStructStream *pack)
{
	assert(structptr);
	if (bsReadBits(pack->bs, 1)) {
		void *substruct;
		// Has a structure to be unpacked

		// Shouldn't already be allocated
		assert(!(tpi[column].type & TOK_INDIRECT) || !TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL)); // Otherwise need to reuse it or something?
		// Allocate structure
		substruct = TokenStoreAlloc(tpi, column, structptr, index, tpi[column].param, NULL, NULL, NULL);
		StructUnbitpackSub(tpi[column].subtable, substruct, pack);
	} else {
		// No structure here
		assert(!TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL)); // Otherwise need to destroy it or something?  Shouldn't have allocated it in the first place!
	}
}

TextParserResult struct_writestring(ParseTable tpi[], int column, void* structptr, int index, char** estr, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *caller_fname, int line)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, &ok);
	assert(!(iWriteTextFlags & WRITETEXTFLAG_PRETTYPRINT));
	if (!RESULT_GOOD(ok)) return ok;
	if (!substruct) return ok; // Write nothing for a struct that NULL
	return ParserWriteText_dbg(estr,tpi[column].subtable,substruct,iWriteTextFlags, iOptionFlagsToMatch,iOptionFlagsToExclude,caller_fname, line);
}

TextParserResult struct_readstring(ParseTable tpi[], int column, void* structptr, int index, const char* str, ReadStringFlags eFlags)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	bool bCreated = false;

	if (!substruct)
	{
		bCreated = true;
		substruct = TokenStoreAlloc(tpi, column, structptr, index, tpi[column].param, NULL, NULL, &ok);		
	}
	else
	{
		StructDeInitVoid(tpi[column].subtable, substruct);
	}

	StructInitVoid(tpi[column].subtable, substruct);

	if (!RESULT_GOOD(ok)) return ok;

	// Treat empty string as a success with clearing
	if (!str || !str[0])
	{
		if (bCreated)
		{
			StructDeInitVoid(tpi[column].subtable, substruct);
			TokenStoreFreeWithTPI(tpi, column, structptr, index, NULL, tpi[column].subtable);
		}
		return PARSERESULT_SUCCESS;
	}

	// Non-empty strings get parsed
	if (ParserReadText(str,tpi[column].subtable,substruct, 0))
	{
		return PARSERESULT_SUCCESS;
	}
	else if (bCreated)
	{
		// If get an error on a non-empty string, clear the struct to avoid incomplete status
		StructDeInitVoid(tpi[column].subtable, substruct);
		TokenStoreFreeWithTPI(tpi, column, structptr, index, NULL, tpi[column].subtable);
	}
	return PARSERESULT_ERROR;
}


void struct_updatecrc(ParseTable tpi[], int column, void* structptr, int index)
{
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	if (substruct) StructUpdateCRC(tpi[column].subtable, substruct);
}

bool ParserTableHasDirtyBitAndGetIt(ParseTable table[], const void *pStruct, bool *pOutBit)
{
	return ParserTableHasDirtyBitAndGetIt_Inline(table,pStruct,pOutBit);
}

int StructCompare(ParseTable pti[], const void *structptr1, const void *structptr2, enumCompareFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	if(!!structptr1 != !!structptr2)
	{
		return !!structptr1 - !!structptr2;
	}
	else if(structptr1)
	{
		int i;
		bool bDirtyBitVal;

		if ((eFlags & COMPAREFLAG_USEDIRTYBITS) && ParserTableHasDirtyBitAndGetIt_Inline(pti, structptr2, &bDirtyBitVal) && !bDirtyBitVal)
		{
			return 0;
		}

		FORALL_PARSETABLE(pti, i)
		{
			StructTypeField fieldFlagsToMatch = iOptionFlagsToMatch;
			StructTypeField fieldFlagsToExclude = iOptionFlagsToExclude;
			enumCompareFlags fieldCompareFlags = eFlags;
			int ret;
			StructTypeField type = pti[i].type;
			if (type & TOK_REDUNDANTNAME)
				continue;

			if (!FlagsMatchAll(pti[i].type,iOptionFlagsToMatch))
			{
				continue;
			}

			if (eFlags & COMPAREFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT)
			{
				if (FlagsMatchAll(pti[i].type,iOptionFlagsToMatch | iOptionFlagsToExclude))
				{
					// This would have been excluded, so include EVERYTHING below
					fieldCompareFlags &= ~COMPAREFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT;
					fieldFlagsToExclude = 0;
				}				
				else if (!TOK_HAS_SUBTABLE(pti[i].type))
				{
					// Leaf nodes would have been explicitly excluded, but there could be data farther down for structures
					continue;					
				}
			}
			else if (!FlagsMatchNone(pti[i].type,iOptionFlagsToExclude))
			{
				continue;
			}			

			if (FieldsMightDiffer(pti,i,structptr1,structptr2))
			{
				ret = compare_autogen(pti, i, structptr1, structptr2, 0, fieldCompareFlags, fieldFlagsToMatch, fieldFlagsToExclude);
				if (ret)
					return ret;
			}
		}
	}
	return 0;
}


int struct_compare(ParseTable tpi[], int column, const void* lhs, const void* rhs, int index, enumCompareFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	void* left = lhs||!(eFlags & COMPAREFLAG_NULLISDEFAULT)?TokenStoreGetPointer_inline(tpi, &tpi[column], column, lhs, index, NULL):NULL;
	void* right = TokenStoreGetPointer_inline(tpi, &tpi[column], column, rhs, index, NULL);
	int retval;
	PERFINFO_RUN(
		ParseTableInfo* pti = ParserGetTableInfo(tpi);
		PERFINFO_AUTO_START_STATIC(pti->name, &pti->piStructCompare, 1);
	);
	retval = StructCompare(tpi[column].subtable, left, right, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
	PERFINFO_AUTO_STOP();
	return retval;
}

size_t struct_memusage(ParseTable tpi[], int column, void* structptr, int index, bool bAbsoluteUsage)
{
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	U32 storage = TokenStoreGetStorageType(tpi[column].type);
	size_t size = (storage == TOK_STORAGE_DIRECT_SINGLE)? 0: tpi[column].param; // if direct, space counted in parent struct
	if (!substruct) return 0;
	return StructGetMemoryUsage(tpi[column].subtable, substruct, bAbsoluteUsage) + size;
}

void struct_calcoffset(ParseTable tpi[], int column, size_t* size)
{
	int storage = TokenStoreGetStorageType(tpi[column].type);

	if (storage == TOK_STORAGE_DIRECT_SINGLE)
	{
		tpi[column].storeoffset = *size;
		(*size) += tpi[column].param; // size of structure
	}
	else
	{
		MEM_ALIGN(*size, sizeof(void*));
		tpi[column].storeoffset = *size;
		(*size) += sizeof(void*);
	}
}

void struct_copystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData)
{
	void* fromstruct = TokenStoreGetPointer_inline(tpi, ptcc, column, src, index, NULL);
	void* substruct;
	int storage = TokenStoreGetStorageType(ptcc->type);

	if (storage != TOK_STORAGE_DIRECT_SINGLE && storage != TOK_STORAGE_DIRECT_EARRAY)
		TokenStoreSetPointer_inline(tpi, ptcc, column, dest, index,NULL, NULL); // make sure alloc doesn't free
	if (fromstruct)
	{
		substruct = TokenStoreAlloc(tpi, column, dest, index, ptcc->param, memAllocator, customData, NULL);
		StructCompress(ptcc->subtable, fromstruct, substruct, memAllocator, customData);
	}
}

void struct_copyfield(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	void* srcstruct = TokenStoreGetPointer_inline(tpi, ptcc, column, src, index, NULL);
	void* deststruct = TokenStoreGetPointer_inline(tpi, ptcc, column, dest, index, NULL);

	if (!srcstruct)
	{
		if (deststruct)
			struct_destroystruct(tpi, ptcc, column, dest, index);
		return;
	}

	if (!deststruct)
		deststruct = TokenStoreAlloc(tpi, column, dest, index, ptcc->param, memAllocator, customData, NULL);
	StructCopyFieldsVoid(ptcc->subtable, srcstruct, deststruct, iOptionFlagsToMatch, iOptionFlagsToExclude);
}

enumCopy2TpiResult struct_copyfield2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest,  
	CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, char **ppResultString)
{
	ParseTable *src_ptcc = &src_tpi[src_column];
	ParseTable *dest_ptcc = &dest_tpi[dest_column];
	void* srcstruct = TokenStoreGetPointer_inline(src_tpi, src_ptcc, src_column, src, src_index, NULL);
	void* deststruct = TokenStoreGetPointer_inline(dest_tpi, dest_ptcc, dest_column, dest, dest_index, NULL);

	if (!srcstruct)
	{
		if (deststruct)
		{
			U32 storage = TokenStoreGetStorageType(dest_ptcc->type);
			if (storage != TOK_STORAGE_DIRECT_SINGLE)
			{
				struct_destroystruct(dest_tpi, dest_ptcc, dest_column, dest, dest_index);
			}
		}
		return COPY2TPIRESULT_SUCCESS;
	}

	if (!deststruct)
		deststruct = TokenStoreAlloc(dest_tpi, dest_column, dest, dest_index, dest_ptcc->param, memAllocator, customData, NULL);
	return StructCopyFields2tpis(src_ptcc->subtable, srcstruct, dest_ptcc->subtable, deststruct, iOptionFlagsToMatch, iOptionFlagsToExclude, ppResultString);
	
}


void struct_endianswap(ParseTable tpi[], int column, void* structptr, int index)
{
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	if (substruct)
		endianSwapStruct(tpi[column].subtable, substruct);
}

void struct_interp(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 interpParam)
{
	void* subA = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structA, index, NULL);
	void* subB = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structB, index, NULL);
	void* subDest = TokenStoreGetPointer_inline(tpi, &tpi[column], column, destStruct, index, NULL);

	if (!subA || !subB) return;
	if (!subDest) subDest = TokenStoreAlloc(tpi, column, destStruct, index, tpi[column].param, NULL, NULL, NULL);
	StructInterpolate(tpi[column].subtable, subA, subB, subDest, interpParam);
}

void struct_calcrate(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 deltaTime)
{
	void* subA = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structA, index, NULL);
	void* subB = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structB, index, NULL);
	void* subDest = TokenStoreGetPointer_inline(tpi, &tpi[column], column, destStruct, index, NULL);

	if (!subA || !subB) return;
	if (!subDest) subDest = TokenStoreAlloc(tpi, column, destStruct, index, tpi[column].param, NULL, NULL, NULL);
	StructCalcRate(tpi[column].subtable, subA, subB, subDest, deltaTime);
}

void struct_integrate(ParseTable tpi[], int column, void* valueStruct, void* rateStruct, void* destStruct, int index, F32 deltaTime)
{
	void* subV = TokenStoreGetPointer_inline(tpi, &tpi[column], column, valueStruct, index, NULL);
	void* subR = TokenStoreGetPointer_inline(tpi, &tpi[column], column, rateStruct, index, NULL);
	void* subDest = TokenStoreGetPointer_inline(tpi, &tpi[column], column, destStruct, index, NULL);

	if (!subV || !subR) return;
	if (!subDest) subDest = TokenStoreAlloc(tpi, column, destStruct, index, tpi[column].param, NULL, NULL, NULL);
	StructIntegrate(tpi[column].subtable, subV, subR, subDest, deltaTime);
}

void struct_calccyclic(ParseTable tpi[], int column, void* valueStruct, void* ampStruct, void* freqStruct, void* cycleStruct, void* destStruct, int index, F32 fStartTime, F32 deltaTime)
{
	void* subV = TokenStoreGetPointer_inline(tpi, &tpi[column], column, valueStruct, index, NULL);
	void* subA = TokenStoreGetPointer_inline(tpi, &tpi[column], column, ampStruct, index, NULL);
	void* subF = TokenStoreGetPointer_inline(tpi, &tpi[column], column, freqStruct, index, NULL);
	void* subC = TokenStoreGetPointer_inline(tpi, &tpi[column], column, cycleStruct, index, NULL);
	void* subDest = TokenStoreGetPointer_inline(tpi, &tpi[column], column, destStruct, index, NULL);

	if (!subV || !subA || !subF || !subC) return;
	if (!subDest) subDest = TokenStoreAlloc(tpi, column, destStruct, index, tpi[column].param, NULL, NULL, NULL);
	StructCalcCyclic(tpi[column].subtable, subV, subA, subF, subC, subDest, fStartTime, deltaTime);
}

void struct_applydynop(ParseTable tpi[], int column, void* dstStruct, void* srcStruct, int index, DynOpType optype, const F32* values, U8 uiValuesSpecd, U32* seed)
{
	void* subDest = TokenStoreGetPointer_inline(tpi, &tpi[column], column, dstStruct, index, NULL);
	void* subSrc = srcStruct? TokenStoreGetPointer_inline(tpi, &tpi[column], column, srcStruct, index, NULL): 0;
	StructApplyDynOp(tpi[column].subtable, optype, values, uiValuesSpecd, subDest, subSrc, seed);
}

void preparesharedmemoryforfixupStruct(ParseTable pti[], void *structptr, char **ppFixupData)
{
	int i;
	if (structptr == NULL)
		return;
	PERFINFO_AUTO_START_FUNC();
	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_REDUNDANTNAME) continue;
		preparesharedmemoryforfixup_autogen(pti, i, structptr, 0, ppFixupData);
	}
	PERFINFO_AUTO_STOP();
}

void struct_preparesharedmemoryforfixup(ParseTable tpi[], int column, void* structptr, int index, char **ppFixupData)
{
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	if (substruct)
		preparesharedmemoryforfixupStruct(tpi[column].subtable, substruct, ppFixupData);
}

void fixupSharedMemoryStruct(ParseTable pti[], void *structptr, char **ppFixupData)
{
	int i;
	if (structptr == NULL)
		return;
	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_REDUNDANTNAME) continue;
		fixupsharedmemory_autogen(pti, i, structptr, 0, ppFixupData);
	}
}

void ReApplyPreParseToStruct(ParseTable pti[], void *structptr, char *pCurrentFile, int iTimeStamp, int iLineNum)
{
	if (structptr)
	{
		int i;

		FORALL_PARSETABLE(pti, i)
		{
			if (pti[i].type & TOK_REDUNDANTNAME) continue;
			reapplypreparse_autogen(pti, i, structptr, 0, pCurrentFile, iTimeStamp, iLineNum);
		}
	}
}

//returns true if the fixup should continue to recurse, false otherwise
__forceinline static bool DoBuiltInStructFixup(ParseTable pti[], void *structptr, enumTextParserFixupType eFixupType)
{
	switch (eFixupType)
	{
	case FIXUPTYPE__BUILTIN__CLEAR_DIRTY_BITS:
		{
			bool bHasDirtyBit;
			bool bDirtyBitVal;

			bHasDirtyBit = ParserTableHasDirtyBitAndGetIt(pti, structptr, &bDirtyBitVal);

			if (!bHasDirtyBit)
			{
				return ParserChildrenHaveDirtyBit(pti);
			}

			if (bDirtyBitVal)
			{
				ParserClearDirtyBit(pti, structptr);
				return ParserChildrenHaveDirtyBit(pti);
			}
			else
			{
				return false;
			}
		}
		break;



	default:
		assertmsgf(0, "Unknown or invalid builtin fixup type %d", eFixupType);
	}

	return true;
}

	


static __inline TextParserResult FixupStructLeafFirst_inline(ParseTable pti[], void *structptr, enumTextParserFixupType eFixupType, void *pExtraData)
{
	int i;
	TextParserResult eResult = PARSERESULT_SUCCESS;
	TextParserAutoFixupCB *pFixupCB;
	if (structptr == NULL)
		return eResult;

	PERFINFO_AUTO_START_FUNC();

	if (eFixupType <= FIXUPTYPE__BUILTIN__LAST)
	{
		bool bShouldRecurse = DoBuiltInStructFixup(pti, structptr, eFixupType);
		if (!bShouldRecurse)
		{
			PERFINFO_AUTO_STOP();
			return eResult;
		}
	}

	FORALL_PARSETABLE(pti, i)
	{
		leafFirstFixup_f leafFirstFixupFunc = TYPE_INFO(pti[i].type).leafFirstFixup;

		if (!leafFirstFixupFunc || (pti[i].type & (TOK_REDUNDANTNAME | TOK_UNOWNED)))
			continue;
		// handle common cases inline
		if ((TYPE_INFO(pti[i].type).leafFirstFixup == struct_leafFirstFixup && !(pti[i].type & TOK_FIXED_ARRAY)))
		{
			int j,numelems = 1;
			if (pti[i].type & TOK_EARRAY)
				numelems = TokenStoreGetNumElems_inline(pti, &pti[i], i, structptr, NULL);

			for (j = 0; j < numelems; j++)
			{
				void* substruct = TokenStoreGetPointer_inline(pti, &pti[i], i, structptr, j, NULL);
				if (substruct && !FixupStructLeafFirst_inline(pti[i].subtable, substruct, eFixupType, pExtraData))
					eResult = 0;
			}
		}
		else // less common cases use the regular function call madness
		{
			SET_MIN_RESULT(eResult, leafFirstFixup_autogen(pti, i, structptr, 0, eFixupType, pExtraData));
		}
	}

	if (eFixupType <= FIXUPTYPE__BUILTIN__LAST)
	{
		PERFINFO_AUTO_STOP();
		return eResult;
	}


	pFixupCB = ParserGetTableFixupFunc(pti);

	if (pFixupCB)
	{
		SET_MIN_RESULT(eResult, pFixupCB(structptr, eFixupType, pExtraData));
	}

	PERFINFO_AUTO_STOP();
	return eResult;
}

TextParserResult FixupStructLeafFirst(ParseTable pti[], void *structptr, enumTextParserFixupType eFixupType, void *pExtraData)
{
	return FixupStructLeafFirst_inline(pti,structptr,eFixupType,pExtraData);
}

int CheckSharedMemory(ParseTable pti[], void *structptr)
{
	int iSuccess = 1;
	int i;
	if (structptr == NULL)
		return 1;
	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_REDUNDANTNAME) continue;
		SET_MIN_RESULT(iSuccess, checksharedmemory_autogen(pti, i, structptr, 0));
	}
	return iSuccess;
}

void struct_fixupsharedmemory(ParseTable tpi[], int column, void* structptr, int index, char **ppFixupData)
{
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	if (substruct)
		fixupSharedMemoryStruct(tpi[column].subtable, substruct, ppFixupData);
}

int struct_leafFirstFixup(ParseTable tpi[], int column, void* structptr, int index, enumTextParserFixupType eFixupType, void *pExtraData)
{
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	if (substruct)
		return FixupStructLeafFirst_inline(tpi[column].subtable, substruct, eFixupType, pExtraData);

	return 1;
}

void struct_reapplypreparse(ParseTable tpi[], int column, void* structptr, int index, char *pCurrentFile, int iTimeStamp, int iLineNum)
{
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	if (substruct)
		ReApplyPreParseToStruct(tpi[column].subtable, substruct, pCurrentFile, iTimeStamp, iLineNum);
}



int struct_checksharedmemory(ParseTable tpi[], int column, void* structptr, int index)
{
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	if (substruct)
	{
		// Embedded structs are Okay
		if (!TokenStoreIsCompatible(tpi[column].type, TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_DIRECT_FIXEDARRAY) &&
				!isSharedMemory(substruct))
		{
			const char *structName;
			if (!(structName = ParserGetTableName(tpi)))
			{
				structName = "UNKNOWN";
			}
			assertmsgf(0,"Element %s (index %d) in struct %s is not pointing to valid shared memory! This will crash when loaded from shared memory. ",tpi[column].name,index,structName);
			return 0;
		}
		return CheckSharedMemory(tpi[column].subtable, substruct);
	}
	return 1;
}

void struct_queryCopyStruct(ParseTable *pTPI, int iColumn, int iIndex, int iOffsetIntoParentStruct, 
	CopyQueryFlags eQueryFlags, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, 
	StructTypeField iOptionFlagsToExclude, void *pUserData)
{
	assertmsg(TokenStoreGetStorageType(pTPI[iColumn].type) == TOK_STORAGE_INDIRECT_SINGLE, "The only way a struct can get a queryCopy is if it's an optional struct, hopefully");
	assert(!(eQueryFlags & COPYQUERYFLAG_IN_FIXED_ARRAY));

	StructCopyQueryResult_MarkBytes(false, iOffsetIntoParentStruct, sizeof(void*), pUserData);

	if (!(eQueryFlags & COPYQUERYFLAG_YOU_ARE_FLAGGED_OUT))
	{
		StructCopyQueryResult_NeedCallback(pUserData);
	}
}



void struct_newCopyField(ParseTable tpi[], int column, void* dest, void* src, int index, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	const void *pSrcStructPtr;
	void *pDestStructPtr;

	assertmsg(TokenStoreGetStorageType(tpi[column].type) == TOK_STORAGE_INDIRECT_SINGLE, "The only way a struct can get a newCopy callback is if it's an optional struct, hopefully");

	pSrcStructPtr = TokenStoreGetPointer_inline(tpi, &tpi[column], column, src, index, NULL);
	pDestStructPtr = TokenStoreGetPointer_inline(tpi, &tpi[column], column, dest, index, NULL);

	if (pSrcStructPtr)
	{
		if (!pDestStructPtr)
		{
			pDestStructPtr = StructCreateVoid(tpi[column].subtable);
			TokenStoreSetPointer_inline(tpi, &tpi[column], column, dest, index, pDestStructPtr, NULL);
		}

		StructCopyVoid(tpi[column].subtable, pSrcStructPtr, pDestStructPtr, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
	}
	else
	{
		if (pDestStructPtr)
		{
			StructDestroyVoid(tpi[column].subtable, pDestStructPtr);
			TokenStoreSetPointer_inline(tpi, &tpi[column], column, dest, index, NULL, NULL);
		}
	}
}
	
void struct_earrayCopy(ParseTable tpi[], int column, void* dest, void* src, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	if (TokenStoreGetStorageType(tpi[column].type) == TOK_STORAGE_DIRECT_EARRAY)
	{
		void **ppSrcBea = (void**)((intptr_t)src + tpi[column].storeoffset);
		void **ppDestBea = (void**)((intptr_t)dest + tpi[column].storeoffset);

		int iSrcSize = beaSize(ppSrcBea);
		int iDestSize = beaSize(ppDestBea);
		int i;
		int iStructSize = tpi[column].param;

		ParseTable *pTPIToUse = tpi[column].subtable;

		const char *pStructName = ParserGetTableName(tpi);

		//beaSetSize takes care of structInit and structDeInit to make the size correct
		beaSetSizeEx(ppDestBea, iStructSize, pTPIToUse, iSrcSize, 0, NULL, NULL, false TPC_MEMDEBUG_PARAMS_EARRAY);
	
		for (i=0 ; i < iSrcSize; i++)
		{
			StructCopyVoid(pTPIToUse, ((char*)(*ppSrcBea)) + iStructSize * i, ((char*)(*ppDestBea)) + iStructSize * i, 
				eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
		}
	}
	else
	{
		void ***pppSrcEarray = TokenStoreGetEArray_inline(tpi, &tpi[column], column, src, NULL);
		void ***pppDestEarray = TokenStoreGetEArray_inline(tpi, &tpi[column], column, dest, NULL);
		ParseTable *pTPIToUse = tpi[column].subtable;

		ParseTable *pSrcIndexedTable = eaIndexedGetTable_inline(pppSrcEarray);
		ParseTable *pDestIndexedTable = eaIndexedGetTable_inline(pppDestEarray);

		const char *pStructName = ParserGetTableName(tpi);


		int iSrcSize = eaSize(pppSrcEarray);

		int i;

		//not sure how likely/possible this is, might as well cover the case
		//
		//this should be uncommon enough that performance is pretty unimportant, so to be safe we'll just blow away the dest
		//array and rebuild it from scratch
		if (pSrcIndexedTable != pDestIndexedTable)
		{
			eaDestroyStructVoid(pppDestEarray, pTPIToUse);
			if (pSrcIndexedTable)
			{
				assert(pSrcIndexedTable == pTPIToUse);
				eaIndexedEnable_dbg(pppDestEarray, pTPIToUse TPC_MEMDEBUG_PARAMS_EARRAY);
			}

			for (i=0; i < iSrcSize; i++)
			{
				void *pSrc  = (*pppSrcEarray)[i];

				if (pSrc)
				{
					void *pNew = StructCreateVoid(pTPIToUse);
					StructCopyVoid(pTPIToUse, pSrc, pNew, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
					eaPush_dbg(pppDestEarray, pNew TPC_MEMDEBUG_PARAMS_EARRAY);
				}
				else
				{
					eaPush_dbg(pppDestEarray, NULL TPC_MEMDEBUG_PARAMS_EARRAY);
				}
			}

			return;
		}

		//do special copying which takes advantage of sorted keyed lists and tries to copy like-named structs on top
		//of each other
		if (pSrcIndexedTable)
		{
			ParserCompareFieldFunction cmp;
			int iKeyColumn;

			int iSrcIndex = 0;
			int iDestIndex = 0;
			int iDestSize = eaSize(pppDestEarray);
			ParserSortData sortData;

			assert(pSrcIndexedTable == pTPIToUse);

			iKeyColumn = ParserGetTableKeyColumn(pTPIToUse);
			assert(iKeyColumn != -1);

			cmp = ParserGetCompareFunction(pTPIToUse,iKeyColumn);
			sortData.tpi = pTPIToUse;
			sortData.column = iKeyColumn;
		
			while (iSrcIndex < iSrcSize)
			{
				void *pSrcStruct = (*pppSrcEarray)[iSrcIndex];

				if (iDestIndex >= iDestSize)
				{
					void *pNew = StructCreateVoid(pTPIToUse);
					StructCopyVoid(pTPIToUse, pSrcStruct, pNew, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
					eaInsert_dbg(pppDestEarray, pNew, iDestIndex, true TPC_MEMDEBUG_PARAMS_EARRAY);
					iSrcIndex++;
					iDestSize++;
					iDestIndex++;
				}
				else
				{
					void *pDestStruct = (*pppDestEarray)[iDestIndex];
					int iCompRet = cmp(&sortData, &pSrcStruct, &pDestStruct);
					if (iCompRet == 0)
					{
						StructCopyVoid(pTPIToUse, pSrcStruct, pDestStruct, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
						iSrcIndex++;
						iDestIndex++;
					}
					else if (iCompRet < 0)
					{
						void *pNew = StructCreateVoid(pTPIToUse);
						StructCopyVoid(pTPIToUse, pSrcStruct, pNew, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
						eaInsert_dbg(pppDestEarray, pNew, iDestIndex, true TPC_MEMDEBUG_PARAMS_EARRAY);
						iSrcIndex++;
						iDestIndex++;
						iDestSize++;
					}
					else //iCompRet > 0
					{
						StructDestroyVoid(pTPIToUse, pDestStruct);
						eaRemove(pppDestEarray, iDestIndex);
						iDestSize--;
					}
				}
			}

			while (iDestSize > iSrcSize)
			{
				void *pDestStruct = (*pppDestEarray)[iDestSize - 1];
				StructDestroyVoid(pTPIToUse, pDestStruct);
				eaRemove(pppDestEarray, iDestSize - 1);
				iDestSize--;
			}
		}
		else
		{
			int iDestSize = eaSize(pppDestEarray);
			int iSharedSize = MIN(iSrcSize, iDestSize);

			assertmsg(eaIndexedGetTable_inline(pppSrcEarray) == NULL && eaIndexedGetTable_inline(pppDestEarray) == NULL, "indexed earray confusion during struct copying");

			for (i=0; i < iSharedSize; i++)
			{
				void *pCurSrcStruct = (*pppSrcEarray)[i];
				void *pCurDestStruct = (*pppDestEarray)[i];

				if (pCurSrcStruct == NULL)
				{
					if (pCurDestStruct)
					{
						//might as well just insert a NULL into the dest earray here... might not have to do a structDestroy later
						eaInsert_dbg(pppDestEarray, NULL, i, false TPC_MEMDEBUG_PARAMS_EARRAY);
						iDestSize++;
						if (iSharedSize < iSrcSize)
						{
							iSharedSize++;
						}
					}
				}
				else
				{
					if (!pCurDestStruct)
					{
						(*pppDestEarray)[i] = StructCreateVoid(pTPIToUse);
					}

					StructCopyVoid(pTPIToUse, pCurSrcStruct, (*pppDestEarray)[i], eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
				}
			}

			if (iSrcSize > iSharedSize)
			{
				for (i = iSharedSize; i < iSrcSize; i++)
				{
					void *pCurSrcStruct = (*pppSrcEarray)[i];
					if (pCurSrcStruct)
					{
						eaPush_dbg(pppDestEarray, StructCreateVoid(pTPIToUse) TPC_MEMDEBUG_PARAMS_EARRAY);
						StructCopyVoid(pTPIToUse, pCurSrcStruct, (*pppDestEarray)[i], eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
					}
					else
					{
						eaPush_dbg(pppDestEarray, NULL TPC_MEMDEBUG_PARAMS_EARRAY);
					}
				}
			}
			else
			{
				for (i = iDestSize - 1; i >= iSharedSize; i--)
				{
					void *pCurDestStruct = (*pppDestEarray)[i];
					if (pCurDestStruct)
					{
						StructDestroyVoid(pTPIToUse, pCurDestStruct);
					}
					eaPop(pppDestEarray);
				}
			}
		}
	}
}



ParseInfoFieldUsage unownedstruct_interpretfield(ParseTable tpi[], int column, ParseInfoField field)
{
	switch (field)
	{
	case SubtableField:
		return PointerToSubtable;
	}
	return NoFieldUsage;
}

int unownedstruct_checksharedmemory(ParseTable tpi[], int column, void* structptr, int index)
{
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	if (substruct)
	{
		if (!isSharedMemory(substruct))
		{
			const char *structName;
			if (!(structName = ParserGetTableName(tpi)))
			{
				structName = "UNKNOWN";
			}
			assertmsgf(0,"Element %s (index %d) in struct %s is not pointing to valid shared memory! This will crash when loaded from shared memory. ",tpi[column].name,index,structName);
			return 0;
		}
	}
	return 1;
}
void unownedstruct_copyfield(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TokenStoreSetPointer_inline(tpi, ptcc, column, dest, index, TokenStoreGetPointer_inline(tpi, &tpi[column], column, src, index, NULL), NULL);
}

enumCopy2TpiResult unownedstruct_copyfield2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest, 
	CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, char **ppResultString)
{
	TokenStoreSetPointer_inline(dest_tpi, &dest_tpi[dest_column], dest_column, dest, dest_index, TokenStoreGetPointer_inline(src_tpi, &src_tpi[src_column], src_column, src, src_index, NULL), NULL);
	return COPY2TPIRESULT_SUCCESS;
}


ParseInfoFieldUsage poly_interpretfield(ParseTable tpi[], int column, ParseInfoField field)
{
	switch (field)
	{
	case ParamField:
		if (tpi[column].type & TOK_INDIRECT)
			return NoFieldUsage;	// extend to be size of base struct later?
		return SizeOfSubstruct;	// only if embedded does this describe size of substruct
	case SubtableField:
		return PointerToSubtable;
	}
	return NoFieldUsage;
}

bool poly_initstruct(ParseTable tpi[], int column, void* structptr, int index)
{
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	if (substruct) // if we already have a pointer, we're embedded, and should init
	{
		// (essentially we end up looking for the poly type that has an object type of zero)
		ParseTable* polytable = tpi[column].subtable;
		int polycol = -1;
		if (StructDeterminePolyType(polytable, substruct, &polycol))
		{
			StructInitVoid(polytable[polycol].subtable, substruct);
		}
		// otherwise, leave it at zero
	}

	return true;
}

int poly_destroystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index)
{
	if (structptr)
	{
		void* substruct = TokenStoreGetPointer_inline(tpi, ptcc, column, structptr, index, NULL);
		if (substruct)
		{
			ParseTable* polytable = ptcc->subtable;
			int polycol = -1;
			if (StructDeterminePolyType(polytable, substruct, &polycol))
			{
				StructDeInitVoid(polytable[polycol].subtable, substruct);
				TokenStoreFreeWithTPI(tpi, column, structptr, index, NULL, polytable[polycol].subtable);
			}
			else
				devassertmsgf(0, "%s couldn't determine poly type!", __FUNCTION__);
		}
	}

	return DESTROY_IF_NON_NULL;
}

int poly_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	ParseTable* polytable = ptcc->subtable;
	char* polytoken;
	int i;
	
	// next identifier should determine the polytype
	polytoken = GetNextParameter(tok, parseResult);
	if (!polytoken) 
		return 0;

	FORALL_PARSETABLE(polytable, i)
	{
		ParseTable *polycc = &polytable[i];
		if (polycc->name && stricmp(polycc->name, polytoken) == 0)
		{
			int newIndex = TokenStoreGetNumElems_inline(tpi, ptcc, column, structptr, parseResult);
			ParseTable *poly_subtable = (ParseTable*)polycc->subtable;
			void* substruct = TokenStoreAllocCharged(tpi, column, structptr, newIndex, polycc->param, NULL, NULL, parseResult, poly_subtable);
			StructInitVoid(poly_subtable, substruct);
			ParserReadTokenizer(tok, poly_subtable, substruct, false, parseResult);

			if (*parseResult == PARSERESULT_INVALID)
			{
				StructDeInitVoid(poly_subtable, substruct);
				TokenStoreFreeWithTPI(tpi, column, structptr, newIndex, NULL, poly_subtable);
				TokenStoreRemoveElement(tpi, column, structptr, newIndex, NULL);
				*parseResult = PARSERESULT_ERROR;
			}
		}
	}
	return 0;
}

bool poly_writetext(FILE* out, ParseTable tpi[], int column, void* structptr, int index, bool showname, bool ignoreInherited, int level, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int i, numelems = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structptr, NULL);

	if (!numelems && (tpi[column].type & TOK_REQUIRED))
	{
		Errorf("REQUIRED poly field is NULL");
	}

	iOptionFlagsToExclude |= TOK_NO_WRITE;

	if (!numelems)
	{
		return false;
	}

	for (i = 0; i < numelems; i++)
	{
		ParseTable* polytable = tpi[column].subtable;
		int polycol = -1;
		void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, i, NULL);

		if (!substruct)
		{
			//this is only legal if it's an optional struct
			U32 storage = TokenStoreGetStorageType(tpi[column].type);
		
			if (storage == TOK_STORAGE_INDIRECT_SINGLE)
			{
				return false;
			}
		}


		if (substruct && StructDeterminePolyType(polytable, substruct, &polycol))
		{
			if (showname) WriteNString(out, tpi[column].name, ParseTableColumnNameLen(tpi,column), level+1, 0);
			WriteNString(out, " ", 1, 0, 0);
			WriteString(out, polytable[polycol].name, 0, 0);
			InnerWriteTextFile(out, polytable[polycol].subtable, substruct, level+1, ignoreInherited, iWriteTextFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
			if (showname) WriteNString(out, NULL, 0, 0, 1);
		}
		else
			devassertmsgf(0, "%s couldn't determine poly type!", __FUNCTION__);
	}

	return true;
}

TextParserResult poly_writebin(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int subsum = 0;
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, &ok);
	U32 storage = TokenStoreGetStorageType(tpi[column].type);
	ParseTable* polytable = tpi[column].subtable;
	int polycol = -1;

	iOptionFlagsToExclude |= TOK_NO_WRITE;


	// optional struct bit
	if (storage == TOK_STORAGE_INDIRECT_SINGLE)
	{
		int hassub = substruct? 1: 0;
		SET_MIN_RESULT(ok, SimpleBufWriteU32(hassub, file));
		*datasum += sizeof(U32);
		if (!hassub) return ok;
	}
	else devassert(substruct);

	// poly type
	if (!StructDeterminePolyType(polytable, substruct, &polycol))
	{
		devassertmsgf(0, "%s couldn't determine poly type!", __FUNCTION__);
		return false;
	}
	SET_MIN_RESULT(ok, SimpleBufWriteU32(polycol, file));
	*datasum += sizeof(U32);

	// substructure
	SET_MIN_RESULT(ok, ParserWriteBinaryTable(file, pLayoutFile, pFileList, polytable[polycol].subtable, substruct, &subsum, iOptionFlagsToMatch, iOptionFlagsToExclude));
	*datasum += subsum;
	return ok;
}

TextParserResult poly_readbin(SimpleBufHandle file,  FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int subsum = 0;
	void* substruct;
	U32 storage = TokenStoreGetStorageType(tpi[column].type);
	ParseTable* polytable = tpi[column].subtable;
	U32 polycol = -1;

	// optional struct bit
	if (storage == TOK_STORAGE_INDIRECT_SINGLE) 
	{
		U32 hassub = 0;
		if (!SimpleBufReadU32(&hassub, file))
			SET_INVALID_RESULT(ok);
		*datasum += sizeof(U32);
		if (!hassub || ok != PARSERESULT_SUCCESS)
			return ok;
	}

	// poly type
	if (!SimpleBufReadU32(&polycol, file))
		SET_INVALID_RESULT(ok);
	*datasum += sizeof(U32);

	// if here, then we have a struct
	substruct = TokenStoreAllocCharged(tpi, column, structptr, index, polytable[polycol].param, NULL, NULL, &ok, polytable[polycol].subtable);
	SET_MIN_RESULT(ok,ParserReadBinaryTableEx(file, pFileList, polytable[polycol].subtable, substruct, &subsum,  iOptionFlagsToMatch,  iOptionFlagsToExclude));
	*datasum += subsum;
	return ok;
}

void poly_writehdiff(char** estr, ParseTable basetpi[], int column, void* oldstruct, void* newstruct, int index, char * prefix, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{

	

	ParseTable* polytable = basetpi[column].subtable;
	void* oldp = oldstruct? TokenStoreGetPointer_inline(basetpi, &basetpi[column], column, oldstruct, index, NULL): 0;
	void* newp = TokenStoreGetPointer_inline(basetpi, &basetpi[column], column, newstruct, index, NULL);
	bool printCreate = !!(basetpi[column].type & TOK_INDIRECT);
	int oldpolytype = -1;
	int newpolytype = -1;
	bool forceAbsoluteDiff = false;	

	if (newp && !oldp)
	{
		AssertOrProgrammerAlert("TRYING_TO_TEXT_DIFF_POLY", "Trying to write out a text diff for field %s in %s, which is a poly type. This is totally illegal",
			basetpi[column].name, ParserGetTableName(basetpi));
		return;
	}



	StructDeterminePolyType(polytable, newp, &newpolytype);
	if (oldp) StructDeterminePolyType(polytable, oldp, &oldpolytype);
	if (oldp && oldpolytype != newpolytype) forceAbsoluteDiff = true;

	if ((eFlags & TEXTIDFFFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT) && !FlagsMatchAll(basetpi[column].type,iOptionFlagsToMatch | iOptionFlagsToExclude))
	{
		// We got here on invert exclude flags that didn't totally match
		printCreate = false;
	}

	StructWriteTextDiffInternal(estr,polytable[newpolytype].subtable,oldp,newp,prefix,iOptionFlagsToMatch,iOptionFlagsToExclude,eFlags, printCreate,forceAbsoluteDiff, caller_fname, line);
	
}

bool poly_senddiff(Packet* pak, ParseTable tpi[], int column, const void* oldstruct, const void* newstruct, int index,  enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB)
{
	ParseTable* polytable = tpi[column].subtable;
	void* oldp = oldstruct? TokenStoreGetPointer_inline(tpi, &tpi[column], column, oldstruct, index, NULL): 0;
	void* newp = TokenStoreGetPointer_inline(tpi, &tpi[column], column, newstruct, index, NULL);
	int oldpolytype = -1;
	int newpolytype = -1;
	int allowDiffs;
	enumSendDiffFlags eRecurseFlags;

	iOptionFlagsToExclude |= TOK_NO_NETSEND;


	if (oldp) StructDeterminePolyType(polytable, oldp, &oldpolytype);
	if (newp) StructDeterminePolyType(polytable, newp, &newpolytype);

	allowDiffs = (eFlags & SENDDIFF_FLAG_ALLOWDIFFS) && !(eFlags & SENDDIFF_FLAG_FORCEPACKALL) && oldp && oldpolytype == newpolytype;
	eRecurseFlags = (eFlags & ~SENDDIFF_FLAG_ALLOWDIFFS) | (allowDiffs ? SENDDIFF_FLAG_ALLOWDIFFS : 0);

	pktSendBits(pak, 1, newp? 1: 0);

	if (newp)
	{
		bool bSentSomething = oldpolytype != newpolytype;
		pktSendBits(pak,1,oldpolytype != newpolytype); //poly type changed
		pktSendBitsPack(pak, 1, newpolytype);
		bSentSomething |= ParserSend(polytable[newpolytype].subtable, pak, oldp, newp, eRecurseFlags, iOptionFlagsToMatch, iOptionFlagsToExclude, pGenerateFlagsCB);

		return bSentSomething;
	}
	else
	{
		return oldp != 0;
	}
}

bool poly_recvdiff(Packet* pak, ParseTable tpi[], int column, void* structptr, int index, enumRecvDiffFlags eFlags)
{
	ParseTable *ptcc = &tpi[column];
	ParseTable* polytable = ptcc->subtable;
	void* substruct = structptr? TokenStoreGetPointer_inline(tpi, ptcc, column, structptr, index, NULL): 0;
	int gettingstruct = pktGetBits(pak, 1);
	int polytype = -1;
	int polychanged = 0;
	int i;
	bool bOverflow = true;

	if (!gettingstruct)
	{
		if (structptr) poly_destroystruct(tpi, ptcc, column, structptr, index);
		return 1;
	}

	// now we're getting a struct
	polychanged = pktGetBits(pak,1);
	polytype = pktGetBitsPack(pak,1);

	//make sure our polytype is valid
	if (eFlags & RECVDIFF_FLAG_UNTRUSTWORTHY_SOURCE)
	{
		FORALL_PARSETABLE(tpi, i)
		{
			if (i >= polytype)
			{
				bOverflow = false;
				break;
			}
		}

		if (bOverflow)
		{
			RECV_FAIL("Invalid poly type");
		}
	}

	//if the poly type changed, have to destroy the old one because it may be a different
	//size
	if (polychanged && substruct)
	{
		poly_destroystruct(tpi, ptcc, column, structptr, index);
		substruct = NULL;
	}

	// finally ready to unpack based on given struct type
	if (!substruct && structptr) 
		substruct = TokenStoreAllocCharged(tpi, column, structptr, index, polytable[polytype].param, NULL, NULL, NULL, polytable[polytype].subtable);
	
	return ParserRecv(polytable[polytype].subtable, pak, substruct, eFlags);
}

bool poly_bitpack(ParseTable tpi[], int column, const void* structptr, int index, PackedStructStream *pack)
{
	ParseTable* polytable = tpi[column].subtable;
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	int newpolytype = -1;

	if (substruct)
		StructDeterminePolyType(polytable, substruct, &newpolytype);

	bsWriteBits(pack->bs, 1, substruct? 1: 0);

	if (substruct)
	{
		bsWriteBitsPack(pack->bs, 1, newpolytype);
		return StructBitpackSub(polytable[newpolytype].subtable, substruct, pack);
	} else {
		return true; // Need to send at least the bit saying it's NULL
	}
}

TextParserResult poly_writestring(ParseTable tpi[], int column, void* structptr, int index, char** estr, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *caller_fname, int line)
{
	int i, numelems = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structptr, NULL);

	if (!numelems && (tpi[column].type & TOK_REQUIRED))
	{
		Errorf("REQUIRED poly field is NULL");
	}
	assert(!(iWriteTextFlags & WRITETEXTFLAG_PRETTYPRINT));

	iOptionFlagsToExclude |= TOK_NO_WRITE;

	for (i = 0; i < numelems; i++)
	{
		ParseTable* polytable = tpi[column].subtable;
		int polycol = -1;
		void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, i, NULL);

		if (!substruct)
		{
			//this is only legal if it's an optional struct
			U32 storage = TokenStoreGetStorageType(tpi[column].type);
		
			if (storage == TOK_STORAGE_INDIRECT_SINGLE)
			{
				return PARSERESULT_ERROR;
			}
		}


		if (substruct && StructDeterminePolyType(polytable, substruct, &polycol))
		{
			estrConcat(estr, polytable[polycol].name, (int)strlen(polytable[polycol].name));
			estrConcat(estr, " ", 1);
			ParserWriteText_dbg(estr,polytable[polycol].subtable,substruct,iWriteTextFlags, iOptionFlagsToMatch,iOptionFlagsToExclude,caller_fname, line);
		}
		else
			devassertmsgf(0, "%s couldn't determine poly type!", __FUNCTION__);
	}

	return PARSERESULT_SUCCESS;
}

TextParserResult poly_readstring(ParseTable tpi[], int column, void* structptr, int index, const char* str, ReadStringFlags eFlags)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	ParseTable* polytable = tpi[column].subtable;
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	bool bCreated = false;
	char name[260];
	char *ptr;
	int polycol;
	bool bFound = false;

	// Determine which poly struct to use
	ptr = strchr(str, ' ');
	if (!ptr)
		return PARSERESULT_ERROR;
	strncpy(name, str, ptr-str);
	name[ptr-str] = '\0';
	str = ptr+1;

	FORALL_PARSETABLE(polytable, polycol)
	{
		if (stricmp(name, polytable[polycol].name) == 0)
		{
			bFound = true;
			break;
		}
	}
	if (!bFound)
		return PARSERESULT_ERROR;

	// Parse using proper poly table
	if (!substruct)
	{
		bCreated = true;
		substruct = TokenStoreAllocCharged(tpi, column, structptr, index, polytable[polycol].param, NULL, NULL, &ok, polytable[polycol].subtable);
	}
	else
	{
		StructDeInitVoid(tpi[column].subtable, substruct);
	}

	StructInitVoid(polytable[polycol].subtable, substruct);

	if (!RESULT_GOOD(ok)) return ok;

	if (ParserReadText(str,polytable[polycol].subtable,substruct, 0))
	{
		return PARSERESULT_SUCCESS;
	}
	else if (bCreated)
	{
		StructDeInitVoid(polytable[polycol].subtable, substruct);
		TokenStoreFreeWithTPI(tpi, column, structptr, index, NULL, polytable[polycol].subtable);
	}
	return PARSERESULT_ERROR;
}


void poly_unbitpack(ParseTable tpi[], int column, void *structptr, int index, PackedStructStream *pack)
{
	assert(structptr);
	if (bsReadBits(pack->bs, 1)) {
		ParseTable* polytable = tpi[column].subtable;
		void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
		int polytype = -1;
		int polychanged = 0;

		// now we're getting a struct
		polytype = bsReadBitsPack(pack->bs,1);

		// finally ready to unpack based on given struct type
		if (!substruct) 
			substruct = TokenStoreAllocCharged(tpi, column, structptr, index, polytable[polytype].param, NULL, NULL, NULL, polytable[polytype].subtable);
		StructUnbitpackSub(polytable[polytype].subtable, substruct, pack);
	} else {
		// No structure here
		assert(!TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL)); // Otherwise need to destroy it or something?  Shouldn't have allocated it in the first place!
	}
}

void poly_updatecrc(ParseTable tpi[], int column, void* structptr, int index)
{
	ParseTable* polytable = tpi[column].subtable;
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	int polycol = -1;

	if (!substruct) return;
	if (!StructDeterminePolyType(polytable, substruct, &polycol))
	{
		devassertmsgf(0, "%s couldn't determine poly type!", __FUNCTION__);		return;	}
	StructUpdateCRC(polytable[polycol].subtable, substruct);
}

int poly_compare(ParseTable tpi[], int column, const void* lhs, const void* rhs, int index, enumCompareFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	ParseTable* polytable = tpi[column].subtable;
	void* left = lhs||!(eFlags & COMPAREFLAG_NULLISDEFAULT)?TokenStoreGetPointer_inline(tpi, &tpi[column], column, lhs, index, NULL):NULL;
	void* right = TokenStoreGetPointer_inline(tpi, &tpi[column], column, rhs, index, NULL);
	int lpolycol = -1;
	int rpolycol = -1;

	if (!left && !right) return 0;
	if (!left) return -1;
	if (!right) return 1;
	assert(StructDeterminePolyType(polytable, left, &lpolycol));
	assert(StructDeterminePolyType(polytable, right, &rpolycol));
	if (lpolycol != rpolycol) return lpolycol - rpolycol;

	return StructCompare(polytable[lpolycol].subtable, left, right, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
}

size_t poly_memusage(ParseTable tpi[], int column, void* structptr, int index, bool bAbsoluteUsage)
{
	ParseTable* polytable = tpi[column].subtable;
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	U32 storage = TokenStoreGetStorageType(tpi[column].type);
	size_t size;
	int polycol = -1;

	if (!substruct) return 0;
	assert(StructDeterminePolyType(polytable, substruct, &polycol));
	size = (storage == TOK_STORAGE_DIRECT_SINGLE)? 0: polytable[polycol].param; // if direct, space counted in parent struct
	return StructGetMemoryUsage(polytable[polycol].subtable, substruct, bAbsoluteUsage) + size;
}

void poly_calcoffset(ParseTable tpi[], int column, size_t* size)
{
	int storage = TokenStoreGetStorageType(tpi[column].type);
	if (storage == TOK_STORAGE_DIRECT_SINGLE)
	{
		tpi[column].storeoffset = *size;
		(*size) += tpi[column].param; // we will have size here if we're direct
	}
	else
	{
		MEM_ALIGN(*size, sizeof(void*));
		tpi[column].storeoffset = *size;
		(*size) += sizeof(void*);
	}
}

void poly_copystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData)
{
	ParseTable* polytable = ptcc->subtable;
	void* fromstruct = TokenStoreGetPointer_inline(tpi, ptcc, column, src, index, NULL);
	void* substruct;
	int storage = TokenStoreGetStorageType(ptcc->type);

	if (storage != TOK_STORAGE_DIRECT_SINGLE)
		TokenStoreSetPointer_inline(tpi, ptcc, column, dest, index,NULL, NULL); // make sure alloc doesn't free
	if (fromstruct)
	{
		int polycol = -1;
		assert(StructDeterminePolyType(polytable, fromstruct, &polycol));
		substruct = TokenStoreAllocCharged(tpi, column, dest, index, polytable[polycol].param, memAllocator, customData, NULL, polytable[polycol].subtable);
		StructCompress(polytable[polycol].subtable, fromstruct, substruct, memAllocator, customData);
	}
}


void poly_copyfield(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	ParseTable* polytable = ptcc->subtable;
	void* srcstruct = TokenStoreGetPointer_inline(tpi, ptcc, column, src, index, NULL);
	void* deststruct = TokenStoreGetPointer_inline(tpi, ptcc, column, dest, index, NULL);
	int polycol = -1;

	if (!srcstruct)
	{
		if (deststruct)
			poly_destroystruct(tpi, ptcc, column, dest, index);
		return;
	}

	assert(StructDeterminePolyType(polytable, srcstruct, &polycol));
	if (!deststruct)
		deststruct = TokenStoreAllocCharged(tpi, column, dest, index, polytable[polycol].param, memAllocator, customData, NULL, polytable[polycol].subtable);
	StructCopyFieldsVoid(polytable[polycol].subtable, srcstruct, deststruct, iOptionFlagsToMatch, iOptionFlagsToExclude);
}

enumCopy2TpiResult poly_copyfield2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest, 
	CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, char **ppResultString)
{
	ParseTable* src_polytable = src_tpi[src_column].subtable;
	ParseTable* dest_polytable = dest_tpi[dest_column].subtable;
	void* srcstruct = TokenStoreGetPointer_inline(src_tpi, &src_tpi[src_column], src_column, src, src_index, NULL);
	void* deststruct = TokenStoreGetPointer_inline(dest_tpi, &dest_tpi[dest_column], dest_column, dest, dest_index, NULL);
	int src_polycol = -1;
	int dest_polycol = -1;
	int i;

	if (!srcstruct)
	{
		if (deststruct)
			poly_destroystruct(dest_tpi, &dest_tpi[dest_column], dest_column, dest, dest_index);
		return COPY2TPIRESULT_SUCCESS;

	}
	assert(StructDeterminePolyType(src_polytable, srcstruct, &src_polycol));
	
	//find which column in the dest polytable corresponds to the column in the source polytable
	FORALL_PARSETABLE(dest_polytable, i)
	{
		if (strcmp(src_polytable[src_polycol].name, dest_polytable[i].name) == 0)
		{
			dest_polycol = i;
			break;
		}
	}

	//TODO maybe return some error here (this is the case where someone is trying to copy a poly from one struct to another
	//using the 2-TPIs method (which usually means to only copy things that are reasonably copyable) and the source poly type
	//doesn't exist in the dest.
	if (dest_polycol == -1)
	{
		if (ppResultString)
		{
			estrConcatf(ppResultString, "Couldn't find matching poly type for source poly type %s while copying from field %s to %s\n",
				src_polytable[src_polycol].name, src_tpi[src_column].name, dest_tpi[dest_column].name);
			return COPY2TPIRESULT_FAILED_FIELDS;
		}
	}
	
	if (!deststruct)
		deststruct = TokenStoreAllocCharged(dest_tpi, dest_column, dest, dest_index, dest_polytable[dest_polycol].param, memAllocator, customData, NULL, dest_polytable[dest_polycol].subtable);
	return StructCopyFields2tpis(src_polytable[src_polycol].subtable, srcstruct, dest_polytable[dest_polycol].subtable, deststruct, iOptionFlagsToMatch, iOptionFlagsToExclude, ppResultString);
}



void poly_endianswap(ParseTable tpi[], int column, void* structptr, int index)
{
	// this operation doesn't let is know if it is _fixing_ or _breaking_ endianness, so we can't
	// test our children's type field to figure out what type of struct we're pointing at
	// - a possible way to fix this would be to add a parameter to let textparser know if the struct
	// is being fixed or broken and then conditionally swap an extra time in here to get the info
	assertmsg(0, "It's not possible to endian swap a poly struct!\n");
}

void poly_interp(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 interpParam)
{
	ParseTable* polytable = tpi[column].subtable;
	void* subA = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structA, index, NULL);
	void* subB = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structB, index, NULL);
	void* subDest = TokenStoreGetPointer_inline(tpi, &tpi[column], column, destStruct, index, NULL);
	int polycolA = -1;
	int polycolB = -1;

	if (!subA || !subB) return;
	assert(StructDeterminePolyType(polytable, subA, &polycolA));
	assert(StructDeterminePolyType(polytable, subB, &polycolB));
	if (polycolA != polycolB)
	{
		Errorf("%s found different poly types for two sides?", __FUNCTION__);
		return;
	}
	if (!subDest) subDest = TokenStoreAllocCharged(tpi, column, destStruct, index, polytable[polycolA].param, NULL, NULL, NULL, polytable[polycolA].subtable);
	StructInterpolate(polytable[polycolA].subtable, subA, subB, subDest, interpParam);
}

void poly_calcrate(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 deltaTime)
{
	ParseTable* polytable = tpi[column].subtable;
	void* subA = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structA, index, NULL);
	void* subB = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structB, index, NULL);
	void* subDest = TokenStoreGetPointer_inline(tpi, &tpi[column], column, destStruct, index, NULL);
	int polycolA = -1;
	int polycolB = -1;

	if (!subA || !subB) return;
	assert(StructDeterminePolyType(polytable, subA, &polycolA));
	assert(StructDeterminePolyType(polytable, subB, &polycolB));
	if (polycolA != polycolB)
	{
		Errorf("%s found different poly types for two sides?", __FUNCTION__);
		return;
	}
	if (!subDest) subDest = TokenStoreAllocCharged(tpi, column, destStruct, index, polytable[polycolA].param, NULL, NULL, NULL, polytable[polycolA].subtable);
	StructCalcRate(polytable[polycolA].subtable, subA, subB, subDest, deltaTime);
}

void poly_integrate(ParseTable tpi[], int column, void* valueStruct, void* rateStruct, void* destStruct, int index, F32 deltaTime)
{
	ParseTable* polytable = tpi[column].subtable;
	void* subV = TokenStoreGetPointer_inline(tpi, &tpi[column], column, valueStruct, index, NULL);
	void* subR = TokenStoreGetPointer_inline(tpi, &tpi[column], column, rateStruct, index, NULL);
	void* subDest = TokenStoreGetPointer_inline(tpi, &tpi[column], column, destStruct, index, NULL);
	int polycolV = -1;
	int polycolR = -1;

	if (!subV || !subR) return;
	assert(StructDeterminePolyType(polytable, subV, &polycolV));
	assert(StructDeterminePolyType(polytable, subR, &polycolR));
	if (polycolV != polycolR)
	{
		Errorf("%s found different poly types for two sides?", __FUNCTION__);
		return;
	}
	if (!subDest) subDest = TokenStoreAllocCharged(tpi, column, destStruct, index, polytable[polycolV].param, NULL, NULL, NULL, polytable[polycolV].subtable);
	StructIntegrate(polytable[polycolV].subtable, subV, subR, subDest, deltaTime);
}

void poly_calccyclic(ParseTable tpi[], int column, void* valueStruct, void* ampStruct, void* freqStruct, void* cycleStruct, void* destStruct, int index, F32 fStartTime, F32 deltaTime)
{
	ParseTable* polytable = tpi[column].subtable;
	void* subV = TokenStoreGetPointer_inline(tpi, &tpi[column], column, valueStruct, index, NULL);
	void* subA = TokenStoreGetPointer_inline(tpi, &tpi[column], column, ampStruct, index, NULL);
	void* subF = TokenStoreGetPointer_inline(tpi, &tpi[column], column, freqStruct, index, NULL);
	void* subC = TokenStoreGetPointer_inline(tpi, &tpi[column], column, cycleStruct, index, NULL);
	void* subDest = TokenStoreGetPointer_inline(tpi, &tpi[column], column, destStruct, index, NULL);
	int polycolV = -1;
	int polycolA = -1;
	int polycolF = -1;
	int polycolC = -1;

	if (!subV || !subA || !subF || !subC) return;
	assert(StructDeterminePolyType(polytable, subV, &polycolV));
	assert(StructDeterminePolyType(polytable, subA, &polycolA));
	assert(StructDeterminePolyType(polytable, subF, &polycolF));
	assert(StructDeterminePolyType(polytable, subC, &polycolC));
	if (polycolV != polycolA || polycolV != polycolF || polycolV != polycolC)
	{
		Errorf("%s found different poly types for the sides?", __FUNCTION__);
		return;
	}
	if (!subDest) subDest = TokenStoreAllocCharged(tpi, column, destStruct, index, polytable[polycolV].param, NULL, NULL, NULL, polytable[polycolV].subtable);
	StructCalcCyclic(polytable[polycolV].subtable, subV, subA, subF, subC, subDest, fStartTime, deltaTime);
}

void poly_applydynop(ParseTable tpi[], int column, void* dstStruct, void* srcStruct, int index, DynOpType optype, const F32* values, U8 uiValuesSpecd, U32* seed)
{
	ParseTable* polytable = tpi[column].subtable;
	void* subDest = TokenStoreGetPointer_inline(tpi, &tpi[column], column, dstStruct, index, NULL);
	void* subSrc = srcStruct? TokenStoreGetPointer_inline(tpi, &tpi[column], column, srcStruct, index, NULL): 0;
	int polycolD = -1;
	int polycolS = -1;

	assert(StructDeterminePolyType(polytable, subDest, &polycolD));
	if (subSrc)
	{
		assert(StructDeterminePolyType(polytable, subSrc, &polycolS));
		if (polycolD != polycolS)
		{
			Errorf("%s found different poly types for two sides?", __FUNCTION__);
			return;
		}
	}
	StructApplyDynOp(polytable[polycolD].subtable, optype, values, uiValuesSpecd, subDest, subSrc, seed);
}

void poly_preparesharedmemoryforfixup(ParseTable tpi[], int column, void* structptr, int index, char **ppFixupData)
{
	ParseTable* polytable = tpi[column].subtable;
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	int polycol = -1;

	if (!substruct) return;
	if (!StructDeterminePolyType(polytable, substruct, &polycol))
	{
		devassertmsgf(0, "%s couldn't determine poly type!", __FUNCTION__);		return;
	}
	preparesharedmemoryforfixupStruct(polytable[polycol].subtable, substruct, ppFixupData);
}

void poly_fixupsharedmemory(ParseTable tpi[], int column, void* structptr, int index, char **ppFixupData)
{
	ParseTable* polytable = tpi[column].subtable;
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	int polycol = -1;

	if (!substruct) return;
	if (!StructDeterminePolyType(polytable, substruct, &polycol))
	{
		devassertmsgf(0, "%s couldn't determine poly type!", __FUNCTION__);		return;
	}
	fixupSharedMemoryStruct(polytable[polycol].subtable, substruct, ppFixupData);
}

int poly_leafFirstFixup(ParseTable tpi[], int column, void* structptr, int index, enumTextParserFixupType eFixupType, void *pExtraData)
{
	ParseTable* polytable = tpi[column].subtable;
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	int polycol = -1;

	if (!substruct) return 1;
	if (!StructDeterminePolyType(polytable, substruct, &polycol))
	{
		devassertmsgf(0, "%s couldn't determine poly type!", __FUNCTION__);		return 0;
	}
	return FixupStructLeafFirst_inline(polytable[polycol].subtable, substruct, eFixupType, pExtraData);
}

void poly_reapplypreparse(ParseTable tpi[], int column, void* structptr, int index, char *pCurrentFile, int iTimeStamp, int iLineNum)
{
	ParseTable* polytable = tpi[column].subtable;
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	int polycol = -1;

	if (!substruct) return;
	if (!StructDeterminePolyType(polytable, substruct, &polycol))
	{
		devassertmsgf(0, "%s couldn't determine poly type!", __FUNCTION__);	}
	ReApplyPreParseToStruct(polytable[polycol].subtable, substruct, pCurrentFile, iTimeStamp, iLineNum);
}

int poly_checksharedmemory(ParseTable tpi[], int column, void* structptr, int index)
{
	ParseTable* polytable = tpi[column].subtable;
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	int polycol = -1;

	if (!substruct) return 1;
	if (!isSharedMemory(substruct))
	{
		const char *structName;
		if (!(structName = ParserGetTableName(tpi)))
		{
			structName = "UNKNOWN";
		}
		assertmsgf(0,"Element %s (index %d) in struct %s is not pointing to valid shared memory! This will crash when loaded from shared memory. ",tpi[column].name,index,structName);
		return 0;
	}
	if (!StructDeterminePolyType(polytable, substruct, &polycol))
	{
		devassertmsgf(0, "%s couldn't determine poly type!", __FUNCTION__);		return 0;
	}
	return CheckSharedMemory(polytable[polycol].subtable, substruct);	
}

void poly_queryCopyStruct(ParseTable *pTPI, int iColumn, int iIndex, int iOffsetIntoParentStruct, 
	CopyQueryFlags eQueryFlags, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, 
	StructTypeField iOptionFlagsToExclude, void *pUserData)
{
	assertmsg(TokenStoreGetStorageType(pTPI[iColumn].type) == TOK_STORAGE_INDIRECT_SINGLE, "The only way a poly can get a queryCopy is if it's an optional poly, hopefully");
	assert(!(eQueryFlags & COPYQUERYFLAG_IN_FIXED_ARRAY));

	StructCopyQueryResult_MarkBytes(false, iOffsetIntoParentStruct, sizeof(void*), pUserData);

	if (!(eQueryFlags & COPYQUERYFLAG_YOU_ARE_FLAGGED_OUT))
	{
		StructCopyQueryResult_NeedCallback(pUserData);
	}
}


void poly_newCopyField(ParseTable tpi[], int column, void* dest, void* src, int index, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	void *pSrcStructPtr;
	void *pDestStructPtr;
	ParseTable *polytable = tpi[column].subtable;
	ParseTable *pSrcTPI = NULL, *pDestTPI = NULL;
	int iSrcPolyType, iDestPolyType;

	assertmsg(TokenStoreGetStorageType(tpi[column].type) == TOK_STORAGE_INDIRECT_SINGLE, "The only way a poly can get a newCopy callback is if it's an optional poly, hopefully");

	pSrcStructPtr = TokenStoreGetPointer_inline(tpi, &tpi[column], column, src, index, NULL);
	pDestStructPtr = TokenStoreGetPointer_inline(tpi, &tpi[column], column, dest, index, NULL);

	if (pSrcStructPtr)
	{
		if (!StructDeterminePolyType(polytable, pSrcStructPtr, &iSrcPolyType))
		{
			devassertmsgf(0, "%s couldn't determine poly type!", __FUNCTION__);			return;
		}

		pSrcTPI = polytable[iSrcPolyType].subtable;
	}

	if (pDestStructPtr)
	{
		if (!StructDeterminePolyType(polytable, pDestStructPtr, &iDestPolyType))
		{
			devassertmsgf(0, "%s couldn't determine poly type!", __FUNCTION__);			return;
		}

		pDestTPI = polytable[iDestPolyType].subtable;
	}


	if (pSrcStructPtr)
	{
		if (pDestStructPtr && pDestTPI != pSrcTPI)
		{
			StructDestroyVoid(pDestTPI, pDestStructPtr);
			TokenStoreSetPointer_inline(tpi, &tpi[column], column, dest, index, NULL, NULL);
			pDestStructPtr = NULL;
		}

		if (!pDestStructPtr)
		{
			pDestStructPtr = StructCreateVoid(pSrcTPI);
			TokenStoreSetPointer_inline(tpi, &tpi[column], column, dest, index, pDestStructPtr, NULL);
		}

		StructCopyVoid(pSrcTPI, pSrcStructPtr, pDestStructPtr, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
	}
	else
	{
		if (pDestStructPtr)
		{
			StructDestroyVoid(pDestTPI, pDestStructPtr);
			TokenStoreSetPointer_inline(tpi, &tpi[column], column, dest, index, NULL, NULL);
		}
	}
}

ParseTable *PolyGetTPI(ParseTable *pPolyTable, void *pStruct)
{
	int iColumn;

	if (!StructDeterminePolyType(pPolyTable, pStruct, &iColumn))
	{
		devassertmsgf(0, "%s couldn't determine poly type!", __FUNCTION__);		return NULL;
	}

	return pPolyTable[iColumn].subtable;

}

void PolyDestroy(ParseTable *pPolyTable, void *pStruct)
{
	if (pStruct)
	{
		ParseTable *pTPI = PolyGetTPI(pPolyTable, pStruct);
		StructDestroyVoid(pTPI, pStruct);
	}
}

void *PolyClone(ParseTable *pPolyTable, void *pSrcStruct, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	if (pSrcStruct)
	{
		ParseTable *pTPI = PolyGetTPI(pPolyTable, pSrcStruct);
		void *pNew = StructCreateVoid(pTPI);
		StructCopyVoid(pTPI, pSrcStruct, pNew, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
		return pNew;
	}
	else
	{
		return NULL;
	}
}

void PolyCopy(ParseTable *pPolyTable, void *pSrc, void **ppDest, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	if (pSrc)
	{
		if (*ppDest)
		{
			ParseTable *pSrcTPI = PolyGetTPI(pPolyTable, pSrc);
			ParseTable *pDestTPI = PolyGetTPI(pPolyTable, *ppDest);

			if (pSrcTPI == pDestTPI)
			{
				StructCopyVoid(pSrcTPI, pSrc, *ppDest, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
			}
			else
			{
				StructDestroyVoid(pDestTPI, *ppDest);
				*ppDest = StructCreateVoid(pSrcTPI);
				StructCopyVoid(pSrcTPI, pSrc, *ppDest, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
			}
		}
		else
		{
			*ppDest = PolyClone(pPolyTable, pSrc, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
		}
	}
	else
	{
		PolyDestroy(pPolyTable, *ppDest);
		*ppDest = NULL;
	}

}

void poly_earrayCopy(ParseTable tpi[], int column, void* dest, void* src, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	void ***pppSrcEarray = TokenStoreGetEArray_inline(tpi, &tpi[column], column, src, NULL);
	void ***pppDestEarray = TokenStoreGetEArray_inline(tpi, &tpi[column], column, dest, NULL);
	ParseTable *pPolyTable = tpi[column].subtable;

	const char *pStructName = ParserGetTableName(tpi);

	ParseTable *pSrcIndexedTable = eaIndexedGetTable_inline(pppSrcEarray);
	ParseTable *pDestIndexedTable = eaIndexedGetTable_inline(pppDestEarray);

	int iSrcSize = eaSize(pppSrcEarray);

	int i;

	//not sure how likely/possible this is, might as well cover the case
	//
	//this should be uncommon enough that performance is pretty unimportant, so to be safe we'll just blow away the dest
	//array and rebuild it from scratch
	if (pSrcIndexedTable != pDestIndexedTable)
	{
		int iDestSize = eaSize(pppDestEarray);

		for (i=0; i < iDestSize; i++)
		{
			PolyDestroy(pPolyTable, (*pppDestEarray)[i]);
		}
		eaDestroy(pppDestEarray);

		if (pSrcIndexedTable)
		{
			eaIndexedEnableVoid(pppDestEarray, pSrcIndexedTable);
		}
		

		for (i=0; i < iSrcSize; i++)
		{
			void *pSrc  = (*pppSrcEarray)[i];

			eaPush_dbg(pppDestEarray, PolyClone(pPolyTable, pSrc, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude) TPC_MEMDEBUG_PARAMS_EARRAY);
		}

		return;
	}

	//do special copying which takes advantage of sorted keyed lists and tries to copy like-named structs on top
	//of each other
	if (pSrcIndexedTable)
	{
		ParserCompareFieldFunction cmp;
		int iKeyColumn;

		int iSrcIndex = 0;
		int iDestIndex = 0;
		int iDestSize = eaSize(pppDestEarray);
		ParserSortData sortData;

		iKeyColumn = ParserGetTableKeyColumn(pSrcIndexedTable);
		assert(iKeyColumn != -1);

		cmp = ParserGetCompareFunction(pSrcIndexedTable,iKeyColumn);
		sortData.tpi = pSrcIndexedTable;
		sortData.column = iKeyColumn;
	
		while (iSrcIndex < iSrcSize)
		{
			void *pSrcStruct = (*pppSrcEarray)[iSrcIndex];

			if (iDestIndex > iDestSize)
			{
				eaIndexedInsert(pppDestEarray, PolyClone(pPolyTable, pSrcStruct, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude), iDestIndex);
				iSrcIndex++;
				iDestSize++;
				iDestIndex++;
			}
			else
			{
				void *pDestStruct = (*pppDestEarray)[iDestIndex];
				int iCompRet = cmp(&sortData, &pSrcStruct, &pDestStruct);
				if (iCompRet == 0)
				{
					PolyCopy(pPolyTable, pSrcStruct, (*pppDestEarray) + iDestIndex, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
					iSrcIndex++;
					iDestIndex++;
				}
				else if (iCompRet < 0)
				{
					eaIndexedInsert(pppDestEarray, PolyClone(pPolyTable, pSrcStruct, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude), iDestIndex);
					iSrcIndex++;
					iDestIndex++;
					iDestSize++;
				}
				else //iCompRet > 0
				{
					PolyDestroy(pPolyTable, pDestStruct);
					eaRemove(pppDestEarray, iDestIndex);
					iDestSize--;
				}
			}
		}

		while (iDestSize > iSrcSize)
		{
			void *pDestStruct = (*pppDestEarray)[iDestSize - 1];
			PolyDestroy(pPolyTable, pDestStruct);
			eaRemove(pppDestEarray, iDestSize - 1);
			iDestSize--;
		}
	}
	else
	{
		int iDestSize = eaSize(pppDestEarray);
		int iSharedSize = MIN(iSrcSize, iDestSize);

		assertmsg(eaIndexedGetTable_inline(pppSrcEarray) == NULL && eaIndexedGetTable_inline(pppDestEarray) == NULL, "indexed earray confusion during struct copying");

		for (i=0; i < iSharedSize; i++)
		{
			void *pCurSrcStruct = (*pppSrcEarray)[i];
			void *pCurDestStruct = (*pppDestEarray)[i];

			if (pCurSrcStruct == NULL)
			{
				if (pCurDestStruct)
				{
					//might as well just insert a NULL into the dest earray here... might not have to do a structDestroy later
					eaInsert(pppDestEarray, NULL, i);
					iDestSize++;
					if (iSharedSize < iSrcSize)
					{
						iSharedSize++;
					}
				}
			}
			else
			{
				PolyCopy(pPolyTable, pCurSrcStruct, (*pppDestEarray) + i, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
			}
		}

		if (iSrcSize > iSharedSize)
		{
			for (i = iSharedSize; i < iSrcSize; i++)
			{
				void *pCurSrcStruct = (*pppSrcEarray)[i];
				
				eaPush_dbg(pppDestEarray, PolyClone(pPolyTable, pCurSrcStruct, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude) TPC_MEMDEBUG_PARAMS_EARRAY);
			}
		}
		else
		{
			for (i = iDestSize - 1; i >= iSharedSize; i--)
			{

				void *pCurDestStruct = (*pppDestEarray)[i];

				PolyDestroy(pPolyTable, pCurDestStruct);

				eaPop(pppDestEarray);
			}
		}
	}
}




int stashtable_destroystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index)
{
	if (structptr)
	{
		StashTable st = TokenStoreGetPointer_inline(tpi, ptcc, column, structptr, index, NULL);
		if (st && !isSharedMemory(st))
			stashTableDestroy(st);
		TokenStoreSetPointer_inline(tpi, ptcc, column, structptr, index,NULL, NULL);
	}

	return DESTROY_IF_NON_NULL;
}

size_t stashtable_memusage(ParseTable tpi[], int column, void* structptr, int index, bool bAbsoluteUsage)
{
	StashTable st = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	if (st) return stashGetCopyTargetAllocSize(st);
	return 0;
}

void stashtable_calcoffset(ParseTable tpi[], int column, size_t* size)
{
	MEM_ALIGN(*size, sizeof(void*));
	tpi[column].storeoffset = *size;
	(*size) += sizeof(void*);
}

void stashtable_copystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData)
{
	StashTable srcst = TokenStoreGetPointer_inline(tpi, ptcc, column, src, index, NULL);
	if (srcst) TokenStoreSetPointer_inline(tpi, ptcc, column, dest, index, stashTableClone(srcst, memAllocator, customData), NULL);
}

void stashtable_copyfield(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	stashtable_destroystruct(tpi, ptcc, column, dest, index);
	stashtable_copystruct(tpi, ptcc, column, dest, src, index, memAllocator, customData);
}
void stashtable_copystruct2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest, CustomMemoryAllocator memAllocator, void* customData)
{
	StashTable srcst = TokenStoreGetPointer_inline(src_tpi, &src_tpi[src_column], src_column, src, src_index, NULL);
	if (srcst) TokenStoreSetPointer_inline(dest_tpi, &dest_tpi[dest_column], dest_column, dest, dest_index, stashTableClone(srcst, memAllocator, customData), NULL);
}

enumCopy2TpiResult stashtable_copyfield2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest, 
	CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, char **ppResultString)
{
	stashtable_destroystruct(dest_tpi, &dest_tpi[dest_column], dest_column, dest, dest_index);
	stashtable_copystruct2tpis(src_tpi, src_column, src_index, src, dest_tpi, dest_column, dest_index, dest, memAllocator, customData);
	return COPY2TPIRESULT_SUCCESS;
}

int stashtable_checksharedmemory(ParseTable tpi[], int column, void* structptr, int index)
{
	StashTable st = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
	if (st)
	{
		bool checkKeys = false, checkValues = false;
		StashTableIterator iterator;
		StashElement element;

		if (!isSharedMemory(st))
		{
			const char *structName;
			if (!(structName = ParserGetTableName(tpi)))
			{
				structName = "UNKNOWN";
			}
			assertmsgf(0,"StashTable %s in struct %s is not pointing to valid shared memory! This will crash when loaded from shared memory. ",tpi[column].name,structName);

			return 0;
		}

		if (stashTableGetKeyType(st) == StashKeyTypeAddress ||
			(stashTableGetKeyType(st) == StashKeyTypeStrings && 
			!(stashTableGetMode(st) & StashDeepCopyKeys_NeverRelease)))
		{
			checkKeys = true;
		}
		if (stashTableAreAllValuesPointers(st))
		{
			checkValues = true;
		}

		stashGetIterator(st, &iterator);

		while (stashGetNextElement(&iterator, &element))
		{
			void *pKey = stashElementGetKey(element);
			void *pData = stashElementGetPointer(element);

			if (checkKeys)
			{
				if (pKey && !isSharedMemory(pKey))
				{
					const char *structName;
					if (!(structName = ParserGetTableName(tpi)))
					{
						structName = "UNKNOWN";
					}
					assertmsgf(0,"Stash key in Table %s in struct %s is not pointing to valid shared memory! This will crash when loaded from shared memory. ",tpi[column].name,structName);

					return 0;
				}
			}
			if (checkValues)
			{
				if (pData && !isSharedMemory(pData))
				{
					const char *structName;
					if (!(structName = ParserGetTableName(tpi)))
					{
						structName = "UNKNOWN";
					}
					assertmsgf(0,"Stash value in Table %s in struct %s is not pointing to valid shared memory! This will crash when loaded from shared memory. ",tpi[column].name,structName);

					return 0;
				}
			}
		}
	}
	return 1;
}

void stashtable_queryCopyStruct(ParseTable *pTPI, int iColumn, int iIndex, int iOffsetIntoParentStruct, 
	CopyQueryFlags eQueryFlags, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, 
	StructTypeField iOptionFlagsToExclude, void *pUserData)
{
	Errorf("Stashtable copying probably needs to be examined more carefully here, given that there are currently none in the code so I don't really know how they're supposed to work...");
	assert(!(eQueryFlags & COPYQUERYFLAG_IN_FIXED_ARRAY));

	StructCopyQueryResult_MarkBytes(false, iOffsetIntoParentStruct, sizeof(StashTable), pUserData);

	if (!(eQueryFlags & COPYQUERYFLAG_YOU_ARE_FLAGGED_OUT))
	{
		StructCopyQueryResult_NeedCallback(pUserData);
	}
}
//////////////////////////////////////////////////////////////// array stuff




/*
void fixedarray_queryCopyStruct(ParseTable *pTPI, int iColumn, int iIndex, int iOffsetIntoParentStruct, 
	CopyQueryFlags eQueryFlags, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, 
	StructTypeField iOptionFlagsToExclude, void *pUserData)
{
	assert(0);
}*/

/*
void earray_queryCopyStruct(ParseTable *pTPI, int iColumn, int iIndex, int iOffsetIntoParentStruct, 
	CopyQueryFlags eQueryFlags, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, 
	StructTypeField iOptionFlagsToExclude, void *pUserData)
{
	assert(0);
}*/

#define _DEBUGMV
#ifdef _DEBUGMV
int	    g_FirstRun= 1;
#define DB_MV_INFO(name, pmv) DebugMultiVal(#name, pmv);
__forceinline static void DebugMultiVal(const char *name, MultiVal* pmv)		
{ 
	MultiInfo info;

	if (g_MVDEBUG) 
	{
		FILE* f = fopen("c:\\dbgmv.txt", "at");
		if (f) {
			MultiValInfo(pmv, &info);
			if (g_FirstRun)
				fprintf(f, "\n\n*****************************************************\n");
			fprintf(f,"%s %s size %d\n", name, MultiValTypeToString(pmv->type), info.size); 
			fclose(f);
			g_FirstRun = 0;
		}
	}
}
#else
#define DB_MV_INFO(x,y) ;
#endif


#define MV_CASE_OTHER	default: assertmsg(0, "This type of MultiVal is unsupported.");	break;
#define INFO_TO_REF(type, cnt)  *((type *) InfoToPointer(&info, cnt))
__forceinline static void *InfoToPointer(MultiInfo* info, U32 cnt)
{
	U8* ptr = (U8*) info->ptr;
	return &ptr[info->width * cnt];
}

bool MultiVal_initstruct(ParseTable tpi[], int column, void* structptr, int index)
{
	MultiVal*  pmv = TokenStoreGetMultiVal(tpi, column, structptr, index, NULL);
	
	if (pmv)
	{
		return false;
	}

	pmv = TokenStoreAllocMultiVal(tpi, column, structptr, index, NULL);
	MultiValConstruct(pmv, MULTI_NONE, 0);
	DB_MV_INFO(initstruct, pmv);

	return true;
}

int MultiVal_destroystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index)
{
	if (structptr)
	{
		U32 storage;
		MultiVal*  pmv;
		
#ifdef TOKENSTORE_DETAILED_TIMERS
		PERFINFO_AUTO_START_FUNC();
#endif
		
		storage = TokenStoreGetStorageType(ptcc->type);

		pmv = TokenStoreGetMultiVal(tpi, column, structptr, index, NULL);

		if (pmv && !isSharedMemory(pmv))
		{
			ANALYSIS_ASSUME(pmv != NULL);
			DB_MV_INFO(destroystruct, pmv);

			if (storage == TOK_STORAGE_INDIRECT_EARRAY)
			{
				MultiValDestroy(pmv);
			}
			else
			{
				MultiValClear(pmv);
			}
		}

#ifdef TOKENSTORE_DETAILED_TIMERS
		PERFINFO_AUTO_STOP();
#endif
	}

	return DESTROY_ALWAYS;
}

#define PTRINDEX(type, index)	((type *) ptr)[index]
int MultiVal_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	MultiInfo	info;
	MultiVal*   pmv			= TokenStoreAllocMultiVal(tpi, column, structptr, index, parseResult);
	char*		tokenType	= GetNextParameter(tok, parseResult);
	char*		tokenVal;
	U32			i			= 0;
	void*		ptr			= 0;
	MultiValType t;
	ParseTableInfo *tpi_info = NULL;

	if (!tokenType)
		return 0;

	t = GetMultiValType(tok, tokenType, parseResult);
	tokenType = NULL;

	tokenVal = TokenizerPeek(tok, PEEKFLAG_IGNORECOMMA, parseResult);
	if (tokenVal) 
		i = strtol(tokenVal, 0, 10);

	//Clear out the old MultiVal and set up this one
	MultiValClear(pmv);
	MultiValConstruct(pmv, t, i);
	MultiValInfo(pmv, &info);

	DB_MV_INFO(parse, pmv)

	//Consume Size from stream if this is an EArray
	if (info.atype == MMA_EARRAY) 
		TokenizerGet(tok, PEEKFLAG_IGNORECOMMA, parseResult);
	ptr = ScratchAlloc(info.count * info.width);
	
	//Iterate through count and load
	for (i =0; i < info.count; i++)
	{
		tokenVal = GetNextParameter(tok, parseResult);
		if (!tokenVal)
		{
			ScratchFree(ptr);
			return 0;
		}

		switch(info.dtype)
		{
			case MMT_INT32:		PTRINDEX(S32,i)    = GetInteger(tok, tokenVal, parseResult);	break;
			case MMT_INT64:		PTRINDEX(S64,i)    = GetInteger(tok, tokenVal, parseResult);	break;
			case MMT_FLOAT32:	PTRINDEX(F32,i)    = GetFloat(tok, tokenVal, parseResult);		break;
			case MMT_FLOAT64:	PTRINDEX(F64,i)	   = GetFloat(tok, tokenVal, parseResult);		break;
			case MMT_STRING:
				tpi_info = ParserGetTableInfo(tpi);
				PTRINDEX(const char*, i) = allocAddString_dbg(tokenVal, true, false, false, SAFE_MEMBER(tpi_info, name)?SAFE_MEMBER(tpi_info, name):__FILE__, LINENUM_FOR_STRINGS);
				break;
			MV_CASE_OTHER
		}
	}

	MultiValFill(pmv, ptr);
	ScratchFree(ptr);

	return 0;
}

bool MultiVal_writetext(FILE* out, ParseTable tpi[], int column, void* structptr, int index, bool showname, bool ignoreInherited, int level, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	MultiInfo	info;
	MultiVal*   pmv			= TokenStoreGetMultiVal(tpi, column, structptr, index, NULL);
	U32			i;

	//Store name
	if (showname) WriteNString(out, tpi[column].name,ParseTableColumnNameLen(tpi,column), level+1, 0);

	//Store type
	WriteNString(out, " ",1, 0, 0);
	WriteString(out, MultiValTypeToString(pmv->type), 0, 0);
	WriteNString(out, " ",1, 0, 0);


	MultiValInfo(pmv, &info);
	DB_MV_INFO(writetext, pmv)

	//Consume Size from stream if this is an EArray
	if (info.atype == MMA_EARRAY) 
	{
		WriteInt(out, info.count, 0, 0, 0);
		WriteNString(out, " ",1, 0, 0);
	}

	for (i =0; i < info.count; i++)
	{
		switch(info.dtype)
		{
			case MMT_INT32:		WriteInt(out,   INFO_TO_REF(U32, i), 0, 0, 0);	break;
			case MMT_INT64:		WriteInt64(out, INFO_TO_REF(U64, i), 0, 0, 0);	break;
			case MMT_FLOAT32:	WriteFloat(out, INFO_TO_REF(F32, i), 0, 0, 0);	break;
			case MMT_FLOAT64:	WriteFloat(out, (float) INFO_TO_REF(F64, i), 0, 0, 0);	break;
			
			case MMT_STRING:	
				WriteQuotedString(out, (char*)pmv->str, 0, 0);
				assert(i==0);
				break;

			MV_CASE_OTHER
		}
		WriteNString(out, " ",1, 0, 0);
	}

	WriteNString(out, NULL, 0, 0, showname);
	return true;
}

TextParserResult MultiVal_writebin(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	MultiInfo	info;
	TextParserResult ok = PARSERESULT_SUCCESS;
	MultiVal*	pmv = TokenStoreGetMultiVal(tpi, column, structptr, index, &ok);
	const char* type= MultiValTypeToString(pmv->type);
	int	len;
	U32 add = 0;
	U32 i, other = 0;

	MultiValInfo(pmv, &info);
	DB_MV_INFO(writebin, pmv)

	//Write the type
	len = SimpleBufWrite((void *) MultiValTypeToString(pmv->type), 4, file);

	//Write number of items of this is an Earray
	if (info.atype == MMA_EARRAY) 
	{
		len += SimpleBufWriteU32(info.count, file);
	}

	for (i =0; i < info.count; i++)
	{
		switch(info.dtype)
		{
			case MMT_INT32:		add += SimpleBufWriteU32(INFO_TO_REF(U32, i), file);	break;
			case MMT_INT64:		add += SimpleBufWriteU64(INFO_TO_REF(U64, i), file);	break;
			case MMT_FLOAT32:	add += SimpleBufWriteF32(INFO_TO_REF(F32, i), file);	break;
			case MMT_FLOAT64:	add += SimpleBufWriteF64(INFO_TO_REF(F64, i), file);	break;
			
			case MMT_STRING:
			{
				add += WritePascalString(file, INFO_TO_REF(const char*, i));
				break;
			}

			MV_CASE_OTHER
		}
	}


//	printf("Wrote %d\n", len + add + other);
	*datasum += (len + add + other);

	if (info.dtype != MMT_STRING)
		SET_MIN_RESULT(ok, len && (add == info.size));
	else
		SET_MIN_RESULT(ok, len);
	return ok;
}

TextParserResult MultiVal_readbin(SimpleBufHandle file,  FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
//	static int cnt = 0;
	char typeString[4];
	MultiVal* pmv;
	MultiInfo info;
	U32		  len, add = 0, other = 0;
	U32		  cnt = 0, i;
	void*	  ptr = 0;

	PERFINFO_AUTO_START_FUNC();

	pmv = TokenStoreAllocMultiVal(tpi, column, structptr, index, NULL);
	//Clear old data
	MultiValClear(pmv);

	//Read type
	len = SimpleBufRead(typeString, 4, file);
	
	//Get Info for this type
	pmv->type = MultiValTypeFromString(typeString);
	if (pmv->type == MULTI_INVALID) return false;
	MultiValInfo(pmv, &info);

	//If this is an array, get the count
	if (info.atype == MMA_EARRAY) 
		len += SimpleBufReadU32(&cnt, file);

	//Construct setting sizes
	MultiValConstruct(pmv, pmv->type, cnt);
	
	//Update Pointer
	MultiValInfo(pmv, &info);
	ptr = ScratchAlloc(info.count * info.width);

	//Iterate through count and load
	for (i =0; i < info.count; i++)
	{
		switch(info.dtype)
		{
			case MMT_INT32:		add += SimpleBufReadU32(&PTRINDEX(U32, i), file);	break;
			case MMT_INT64:		add += SimpleBufReadU64(&PTRINDEX(U64, i), file);	break;
			case MMT_FLOAT32:	add += SimpleBufReadF32(&PTRINDEX(F32, i), file);	break;
			case MMT_FLOAT64:	add += SimpleBufReadF64(&PTRINDEX(F64, i), file);	break;
			case MMT_STRING:
			{
				char *pTempStr = NULL;
				ParseTableInfo *tpi_info = ParserGetTableInfo(tpi);
				estrStackCreate(&pTempStr);
				add += ReadPascalStringIntoEString(file, &pTempStr);
				PTRINDEX(const char*, i) = allocAddString_dbg(pTempStr, true, false, false, SAFE_MEMBER(tpi_info, name)?SAFE_MEMBER(tpi_info, name):__FILE__, LINENUM_FOR_STRINGS);	
				estrDestroy(&pTempStr);
				break;
			}

			MV_CASE_OTHER
		}
	}

	//Update Pointer
	MultiValFill(pmv, ptr);
	ScratchFree(ptr);

	MultiValInfo(pmv, &info);

	DB_MV_INFO(readbin, pmv)

	*datasum += (len + add + other);
//	printf("Read %d\n", add + len + other);

	PERFINFO_AUTO_STOP();
	if (info.dtype != MMT_STRING)
		return len && (add == info.size);
	else
		return len != 0;
}

void MultiVal_writehdiff(char** estr, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, char *prefix, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	MultiInfo	info;
	MultiVal*   pmv			= TokenStoreGetMultiVal(tpi, column, newstruct, index, NULL);
	U32			i;

	MultiValInfo(pmv, &info);
	estrConcatf_dbg(estr, caller_fname, line, "set %s = \"%08X ", prefix, pmv->type);
	
	DB_MV_INFO(writehdiff, pmv)

	//Consume Size from stream if this is an EArray
	if (info.atype == MMA_EARRAY) 
	{
		estrConcatf_dbg(estr, caller_fname, line, "%d ", info.count);
	}

	for (i =0; i < info.count; i++)
	{
		switch(info.dtype)
		{
		case MMT_INT32:
			estrConcatf_dbg(estr, caller_fname, line, "%"FORM_LL"d ", INFO_TO_REF(U64, i));
			break;
		case MMT_INT64:
			estrConcatf_dbg(estr, caller_fname, line, "%"FORM_LL"d ", INFO_TO_REF(U64, i));
			break;
		case MMT_FLOAT32:
			estrConcatf_dbg(estr, caller_fname, line, "%g ", INFO_TO_REF(F32, i));
			break;
		case MMT_FLOAT64:	
			estrConcatf_dbg(estr, caller_fname, line, "%g ", INFO_TO_REF(F64, i));
			break;
		case MMT_STRING:	
			estrConcatf_dbg(estr, caller_fname, line, "%s ", pmv->str);
			assert (i == 0);
			break;

		default:
			break;
		}
	}
	estrAppend2_dbg_inline(estr, "\"\n", caller_fname, line);
}

void MultiVal_writebdiff(StructDiff *diff, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, ObjectPath *parentPath, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool invertExcludeFlags, const char *caller_fname, int line)
{
	StructDiffOp *op = eaTail(&diff->ppOps);
	if (op) {
		MultiVal *val = TokenStoreGetMultiVal(tpi, column, newstruct, index, NULL);
		op->pOperand = MultiValCreate();
		MultiValCopy(op->pOperand, val);
	}
}

bool MultiVal_senddiff(Packet* pak, ParseTable tpi[], int column, const void* oldstruct, const void* newstruct, int index, enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB)
{
	if (oldstruct && (eFlags & SENDDIFF_FLAG_COMPAREBEFORESENDING) && MultiVal_compare(tpi,column, oldstruct, newstruct, index, 0, 0, 0) == 0)
	{
		return false;
	}
	else
	{
		MultiInfo	info;
		MultiVal*	pmv = TokenStoreGetMultiVal(tpi, column, newstruct, index, NULL);
		int	ok = 1;
		U32 add = 0;
		U32 i;

		MultiValInfo(pmv, &info);
		DB_MV_INFO(senddiff, pmv)

		//Send type
		pktSendBits(pak, 32, pmv->type);

		//Write number of items if this is an Earray
		if (info.atype == MMA_EARRAY) 
		{
			pktSendBitsPack(pak, 1, info.count);
		}

		for (i =0; i < info.count; i++)
		{
			switch(info.dtype)
			{
				case MMT_INT32:		
				case MMT_FLOAT32:	
					pktSendBitsPack(pak, TOK_GET_PRECISION_DEF(tpi[column].type, 1), INFO_TO_REF(U32, i));
					break;

				case MMT_INT64:		
				case MMT_FLOAT64:
				{
					S64 val = INFO_TO_REF(U64, i);
					U32 low  = val & 0xffffffff;
					U32 high = val >> 32;
					pktSendBitsPack(pak, 1, low);
					pktSendBitsPack(pak, 1, high);
					break;
				}
				
				case MMT_STRING:
				{
					const char  *str  = INFO_TO_REF(const char*, i);
					pktSendString(pak, str);
					break;
				}

				MV_CASE_OTHER
			}
		}
		
		if (g_MVDEBUG)
		{

			FILE* f = fopen("c:\\dbgmv.txt", "at");
			fwrite("SENDDIFF ", 9, 1, f);
			MultiVal_writetext(f, tpi, column, (void*)newstruct, index, 1, 0, 0, 0, 0, 0);
			fclose(f);
		}
	}
	return true;
}

bool MultiVal_recvdiff(Packet* pak, ParseTable tpi[], int column, void* structptr, int index, enumRecvDiffFlags eFlags)
{
	MultiVal* pmv = TokenStoreAllocMultiVal(tpi, column, structptr, index, NULL);
	MultiVal  mv;
	U32		  cnt=0, i;
	MultiInfo info;
	void*	  ptr;


	//Get Type and Size
	mv.intval	= 0;
	mv.type		= pktGetBits(pak, 32);
	MultiValInfo(&mv, &info);

	//Get the count if it's an array
	if (info.atype == MMA_EARRAY) 
		cnt = pktGetBitsPack(pak, 1);

	if (eFlags & RECVDIFF_FLAG_UNTRUSTWORTHY_SOURCE)
	{
		if (cnt > MAX_UNTRUSTWORTHY_ARRAY_SIZE)
		{
			RECV_FAIL("Multival size too large");
		}
	}


	MultiValConstruct(&mv, mv.type, cnt);

	//Update Pointer
	MultiValInfo(&mv, &info);
	ptr = ScratchAlloc(info.count * info.width);

	DB_MV_INFO(recvdiff, &mv)

	//Itereate through count and load
	for (i =0; i < info.count; i++)
	{
		RECV_CHECK_PAK(pak);

		switch(info.dtype)
		{
			case MMT_INT32:		
			case MMT_FLOAT32:	
				PTRINDEX(U32, i) = pktGetBitsPack(pak, TOK_GET_PRECISION_DEF(tpi[column].type, 1));
				break;
 
			case MMT_INT64:		
			case MMT_FLOAT64:
			{
				U32 low, high;
				low = pktGetBitsPack(pak, 1);
				high= pktGetBitsPack(pak, 1);

				PTRINDEX(U64, i) = (((S64) high) << 32) | ((S64) low);
				break;
			}
			
			case MMT_STRING:
			{
				ParseTableInfo *tpi_info = ParserGetTableInfo(tpi);
				char  *pstr;
				//int	   len;
				pstr = pktGetStringTemp(pak);

				if (eFlags & RECVDIFF_FLAG_UNTRUSTWORTHY_SOURCE)
				{
					RECV_FAIL("Can't accept pooled strings from an untrustworthy source");
				}

				//pstr = pktGetStringAndLength(pak, &len);
				PTRINDEX(char *, i) = (char* ) allocAddString_dbg(pstr, true, false, false, SAFE_MEMBER(tpi_info, name)?SAFE_MEMBER(tpi_info, name):__FILE__, LINENUM_FOR_STRINGS);

				break;
			}

			MV_CASE_OTHER
		}
	}


	if (structptr)
	{
		MultiValClear(pmv);
		pmv->type  = mv.type;
		pmv->intval= mv.intval;
		MultiValFill(pmv, ptr);
	}

	if (g_MVDEBUG)
	{

		FILE* f = fopen("c:\\dbgmv.txt", "at");
		fwrite("RECVDIFF ", 9, 1, f);
		MultiVal_writetext(f, tpi, column, structptr, index, 1, 0, 0, 0, 0, 0);
		fclose(f);
	}

	ScratchFree(ptr);
	return 1;
}

bool MultiVal_bitpack(ParseTable tpi[], int column, const void* structptr, int index, PackedStructStream *pack)
{
	MultiInfo	info;
	MultiVal*	pmv = TokenStoreGetMultiVal(tpi, column, structptr, index, NULL);
	int	ok = 1;
	U32 add = 0;
	U32 i;
	bool ret=true; // No smart packing here

	MultiValInfo(pmv, &info);
	DB_MV_INFO(bitpack, pmv)

	//Send type
	bsWriteBits(pack->bs, 32, pmv->type);

	//Write number of items of this is an Earray
	if (info.atype == MMA_EARRAY) 
	{
		bsWriteBitsPack(pack->bs, 1, info.count);
	}

	//Iterate through count and load
	for (i =0; i < info.count; i++)
	{
		switch(info.dtype)
		{
			case MMT_INT32:
			case MMT_FLOAT32:
				if (INFO_TO_REF(U32, i) != 0)
					ret = true;
				bsWriteBitsPack(pack->bs, TOK_GET_PRECISION_DEF(tpi[column].type, 1), INFO_TO_REF(U32, i));
				break;

			case MMT_INT64:
			case MMT_FLOAT64:
			{
				S64 val = INFO_TO_REF(U64, i);
				bsWriteBits64(pack->bs, 64, val);
				break;
			}
			
			case MMT_STRING:
			{
				const char *str  = INFO_TO_REF(const char*, i);
				bsWriteString(pack->bs, str?str:"");
				break;
			}

			MV_CASE_OTHER
		}
	}
	
	if (g_MVDEBUG)
	{

		FILE* f = fopen("c:\\dbgmv.txt", "at");
		fwrite("BITPACK ", 9, 1, f);
		MultiVal_writetext(f, tpi, column, (void*)structptr, index, 1, 0, 0, 0, 0, 0);
		fclose(f);
	}
	return ret;
}

void MultiVal_unbitpack(ParseTable tpi[], int column, void *structptr, int index, PackedStructStream *pack)
{
	MultiVal* pmv = TokenStoreAllocMultiVal(tpi, column, structptr, index, NULL);
	MultiVal  mv;
	U32		  cnt=0, i;
	MultiInfo info;
	void*	  ptr;

	//Get Type and Size
	mv.intval	= 0;
	mv.type		= bsReadBits(pack->bs, 32);
	MultiValInfo(&mv, &info);

	//Get the count if it's an array
	if (info.atype == MMA_EARRAY) 
		cnt = bsReadBitsPack(pack->bs, 1);

	MultiValConstruct(&mv, mv.type, cnt);

	//Update Pointer
	MultiValInfo(&mv, &info);
	ptr = ScratchAlloc(info.count * info.width);

	DB_MV_INFO(unbitpack, &mv)

	for (i =0; i < info.count; i++)
	{
		switch(info.dtype)
		{
			case MMT_INT32:		
			case MMT_FLOAT32:	
				PTRINDEX(U32, i) = bsReadBitsPack(pack->bs, TOK_GET_PRECISION_DEF(tpi[column].type, 1));
				break;
 
			case MMT_INT64:		
			case MMT_FLOAT64:
			{
				PTRINDEX(U64, i) = bsReadBits64(pack->bs, 64);
				break;
			}
			
			case MMT_STRING:
			{
				ParseTableInfo *tpi_info = ParserGetTableInfo(tpi);
				char  *pEString = NULL;
				estrStackCreate(&pEString);
				bsReadString(pack->bs, &pEString);
				PTRINDEX(const char *, i) = allocAddString_dbg(pEString, true, false, false, SAFE_MEMBER(tpi_info, name)?SAFE_MEMBER(tpi_info, name):__FILE__, LINENUM_FOR_STRINGS);
				estrDestroy(&pEString);

				break;
			}

			MV_CASE_OTHER
		}
	}

	if (structptr)
	{
		MultiValClear(pmv);
		pmv->type  = mv.type;
		pmv->intval= mv.intval;
		MultiValFill(pmv, ptr);
	}

	if (g_MVDEBUG)
	{
		FILE* f = fopen("c:\\dbgmv.txt", "at");
		fwrite("UNBITPACK ", 9, 1, f);
		MultiVal_writetext(f, tpi, column, structptr, index, 1, 0, 0, 0, 0, 0);
		fclose(f);
	}

	ScratchFree(ptr);
	return;
}

TextParserResult MultiVal_writestring(ParseTable tpi[], int column, void* structptr, int index, char** estr, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *caller_fname, int line)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	MultiInfo	info;
	MultiVal*   pmv			= TokenStoreGetMultiVal(tpi, column, structptr, index, &ok);
	U32			i;

	if (!RESULT_GOOD(ok)) return ok;

	if (iWriteTextFlags & WRITETEXTFLAG_PRETTYPRINT)
	{
		MultiValToEString(pmv, estr);
		return PARSERESULT_SUCCESS;
	}

	MultiValInfo(pmv, &info);
	estrConcatf_dbg(estr, caller_fname, line, "%08X ", pmv->type);
	DB_MV_INFO(writestring, pmv)

	//Consume Size from stream if this is an EArray
	if (info.atype == MMA_EARRAY) 
	{
		if (info.width == sizeof(U32))
			estrConcatf_dbg(estr, caller_fname, line, "%d ", eaiSize((int **) &pmv->ptr));
		else
			estrConcatf_dbg(estr, caller_fname, line, "%d ", eaSize((int ***) &pmv->ptr));
	}

	//Itereate through count and load
	for (i =0; i < info.count; i++)
	{
		switch(info.dtype)
		{
			case MMT_INT32:		estrConcatf_dbg(estr, caller_fname, line, "%"FORM_LL"d ", INFO_TO_REF(U64, i));	break;
			case MMT_INT64:		estrConcatf_dbg(estr, caller_fname, line, "%"FORM_LL"d ", INFO_TO_REF(U64, i));	break;
			case MMT_FLOAT32:	estrConcatf_dbg(estr, caller_fname, line, "%g ", INFO_TO_REF(F32, i));	break;
			case MMT_FLOAT64:	estrConcatf_dbg(estr, caller_fname, line, "%g ", INFO_TO_REF(F64, i));	break;

			case MMT_STRING:	
				estrConcatf_dbg(estr, caller_fname, line, "%s ", pmv->str);
				assert(i==0);
				break;

			default: return PARSERESULT_ERROR;
		}
	}

	return PARSERESULT_SUCCESS;
}

TextParserResult MultiVal_readstring(ParseTable tpi[], int column, void* structptr, int index, const char* str, ReadStringFlags eFlags)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	MultiInfo	info;
	MultiVal*   pmv		= TokenStoreAllocMultiVal(tpi, column, structptr, index, &ok);
	U32			i		= 0;
	U32			cnt		= 1;
	U32			len     = 0;
	void*		ptr		= 0;
	U32			type;

	if (!RESULT_GOOD(ok)) return ok;
	
	type = strtol(str, (char**)&str, 10);

	//Clear out the old one
	MultiValClear(pmv);
	pmv->type	= type;
	pmv->intval	= 0;

	//Consume Size from stream if this is an EArray
	MultiValInfo(pmv, &info);
	if (info.atype == MMA_EARRAY) 
		cnt = strtol(str, (char**)&str, 10);

	MultiValConstruct(pmv, type, cnt);
	MultiValInfo(pmv, &info);
	ptr = ScratchAlloc(info.count * info.width);

	DB_MV_INFO(readstring, pmv)

	//Ittereate through count and load
	for (i =0; i < info.count; i++)
	{
		switch(info.dtype)
		{
			case MMT_INT32:		PTRINDEX(U32, i)   = strtol(str, (char**)&str, 10);	break;
			case MMT_INT64:		PTRINDEX(U64, i)   = strtol(str, (char**)&str, 10);	break;
			case MMT_FLOAT32:	PTRINDEX(F32, i)   = strtod(str, (char**)&str);		break;
			case MMT_FLOAT64:	PTRINDEX(F64, i)   = strtod(str, (char**)&str);		break;
			case MMT_STRING:
			{
				//Find start and end
				ParseTableInfo *tpi_info = ParserGetTableInfo(tpi);
				char temp[2048];

				while(isspace((unsigned char) *str)) str++;
				while(isalnum((unsigned char) str[len])) len++;

				assert(len < ARRAY_SIZE(temp)); // If it's larger than this, then it must be dynamic, and we don't want it in the allocAddString table anyway!
				strncpy_s(SAFESTR(temp), str, len);

				PTRINDEX(const char*, i) = allocAddString_dbg(temp, true, false, false, SAFE_MEMBER(tpi_info, name)?SAFE_MEMBER(tpi_info, name):__FILE__, LINENUM_FOR_STRINGS);

				break;
			}

			MV_CASE_OTHER
		}
	}
	
	MultiValFill(pmv, ptr);
	ScratchFree(ptr);
	return PARSERESULT_SUCCESS;
}

TextParserResult MultiVal_tomulti(ParseTable tpi[], int column, void* structptr, int index, MultiVal* result, bool bDuplicateData, bool dummyOnNULL)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	MultiVal*   pmv;

	if (!structptr && dummyOnNULL)
	{
		result->type = MULTI_NONE;
		result->ptr = NULL;
	}
	else
	{
		pmv	= TokenStoreGetMultiVal(tpi, column, structptr, index, &ok);
		if (RESULT_GOOD(ok)) MultiValCopy(result, pmv);
	}
	return ok;
}

TextParserResult MultiVal_frommulti(ParseTable tpi[], int column, void* structptr, int index, const MultiVal* value)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	MultiVal*   pmv		= TokenStoreAllocMultiVal(tpi, column, structptr, index, NULL);
	if (RESULT_GOOD(ok)) MultiValCopy(pmv, value);
	return ok;
}

void MultiVal_updatecrc(ParseTable tpi[], int column, void* structptr, int index)
{
	unsigned int i;
	MultiVal* pmv = TokenStoreGetMultiVal(tpi, column, structptr, index, NULL);
	MultiInfo info;

	DB_MV_INFO(updatecrc, pmv)		

	MultiValInfo(pmv, &info);
	cryptAdler32Update_AutoEndian((U8*)&pmv->type, sizeof(pmv->type));

	for (i =0; i < info.count; i++)
	{
		switch(info.dtype)
		{
		xcase MMT_INT32:
		acase MMT_INT64:
		acase MMT_FLOAT32:
		acase MMT_FLOAT64:
			cryptAdler32Update_AutoEndian(InfoToPointer(&info, i), info.width);

		xcase MMT_STRING:
		{
			char *s = *(char**)InfoToPointer(&info, i);
			assert(i == 0);
			if (s)
				cryptAdler32Update_IgnoreCase(s, info.size);
			else
				cryptAdler32Update_IgnoreCase("", 1);
			break;
		}

		MV_CASE_OTHER
		}
	}
}

int MultiVal_compare(ParseTable tpi[], int column, const void* lhs, const void* rhs, int index, enumCompareFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	MultiVal* pL = lhs||!(eFlags & COMPAREFLAG_NULLISDEFAULT)?TokenStoreGetMultiVal(tpi, column, lhs, index, NULL):NULL;
	MultiVal* pR = TokenStoreGetMultiVal(tpi, column, rhs, index, NULL);
	MultiVal  mv;
	int		 diff= 0;

	//Add a case for NULL left side
	MultiValConstruct(&mv, MULTI_NONE, 0);
	if (pL == 0)
		pL = &mv;

	//If types differ, compare by type
	if (MULTI_GET_TYPE(pL->type) != MULTI_GET_TYPE(pR->type))
		diff = (int) MULTI_GET_TYPE(pL->type) - (int) MULTI_GET_TYPE(pR->type);
	else
	{

		//If types are the same, compare by value, unless an OP
		switch(MULTI_GET_TYPE(pL->type))
		{
			case MMT_FLOAT64:	
				if (pL->floatval == pR->floatval) diff = 0;
				else if (pL->floatval <  pR->floatval) diff = -1;
				else diff = 1;
				break;
		
			case MMT_INT64:		
				diff = (int) (pL->intval - pR->intval);
				break;

			case MMT_NP_POINTER:
				diff = (int) (pL->str - pR->str);
				break;

			case MMT_STRING:
				diff = stricmp(pL->str, pR->str);
				break;

			default:
				{
					MultiInfo	iLeft, iRight;
					MultiValInfo(pL, &iLeft);
					MultiValInfo(pR, &iRight);

					if (iLeft.size != iRight.size)
						return iLeft.size - iRight.size;
					
					if (iLeft.size)
						return memcmp(iLeft.ptr, iRight.ptr, iLeft.size);
					return pL->type - pR->type;
				}
				
				break;
		}
	}
	return diff;
}

size_t MultiVal_memusage(ParseTable tpi[], int column, void* structptr, int index, bool bAbsoluteUsage)
{
	MultiInfo info;
	MultiVal* pmv = TokenStoreGetMultiVal(tpi, column, structptr, index, NULL);
	U32 storage = TokenStoreGetStorageType(tpi[column].type);
	size_t memUsage = 0;

	DB_MV_INFO(memusage, pmv)
	MultiValInfo(pmv, &info);
	switch(info.atype)
	{
		case MMA_NONE:		memUsage = 0;
		case MMA_FIXED:		memUsage = info.size;
		case MMA_EARRAY:	memUsage = info.size + sizeof(EArray) - sizeof(void*);
	}

	if (storage == TOK_STORAGE_INDIRECT_EARRAY)
		memUsage += sizeof(MultiVal);

	return memUsage;
}

void MultiVal_calcoffset(ParseTable tpi[], int column, size_t* size)
{
	MEM_ALIGN(*size, sizeof(MultiVal));
	tpi[column].storeoffset = *size;
	(*size) += sizeof(MultiVal);
}

//note that this function, like all _copystruct, should NOT ever free memory, because this is only called after
//struct_deinit has already been called, so any pointers it thinks it seems are in fact just fake ones that got memcopied
void MultiVal_copystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData)
{
	MultiVal*	 psrc = TokenStoreGetMultiVal(tpi, column, src, index, NULL);
	U32			 storage = TokenStoreGetStorageType(ptcc->type);
	
	DB_MV_INFO(copystruct, psrc);
	if (!src || !dest) return;
	
	if (storage == TOK_STORAGE_INDIRECT_EARRAY)
		TokenStoreSetPointer_inline(tpi, ptcc, column, dest, index, NULL, NULL);	//Do magic for array'd struct pointers

	TokenStoreClearMultiVal(tpi, column, dest, index, NULL);

	TokenStoreSetMultiVal(tpi, column, dest, index, psrc, memAllocator, customData, NULL, NULL);

}

void MultiVal_copyfield(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	MultiVal*	 psrc = TokenStoreGetMultiVal(tpi, column, src, index, NULL);

	DB_MV_INFO(copyfield, psrc)
	TokenStoreSetMultiVal(tpi, column, dest, index, psrc, memAllocator, customData, NULL, NULL);
}

enumCopy2TpiResult MultiVal_copyfield2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest,  
	CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, char **ppResultString)
{
	MultiVal*	 psrc = TokenStoreGetMultiVal(src_tpi, src_column, src, src_index, NULL);

	DB_MV_INFO(copyfield, psrc)
	TokenStoreSetMultiVal(dest_tpi, dest_column, dest, dest_index, psrc, memAllocator, customData, NULL, NULL);
	return COPY2TPIRESULT_SUCCESS;
}


void MultiVal_endianswap(ParseTable tpi[], int column, void* structptr, int index)
{
	MultiVal*	 psrc = TokenStoreGetMultiVal(tpi, column, structptr, index, NULL);

	DB_MV_INFO(copyfield, psrc)
	assertmsg(0, "EndianSwap is non-reversable for MultiVals");
}

void MultiVal_queryCopyStruct(ParseTable *pTPI, int iColumn, int iIndex, int iOffsetIntoParentStruct, 
	CopyQueryFlags eQueryFlags, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, 
	StructTypeField iOptionFlagsToExclude, void *pUserData)
{
	StructCopyQueryResult_MarkBytes(false, iOffsetIntoParentStruct + iIndex * sizeof(MultiVal), sizeof(MultiVal), pUserData);

	if (!(eQueryFlags & COPYQUERYFLAG_YOU_ARE_FLAGGED_OUT))
	{
		StructCopyQueryResult_NeedCallback(pUserData);
	}
}

void MultiVal_earrayCopy(ParseTable tpi[], int column, void* dest, void* src, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	const char *pStructName = ParserGetTableName(tpi);
	int i;

	//two possibilities... Multival** (indirect earray) or Multival* (direct earray, ie blockearray)
	if (TokenStoreGetStorageType(tpi[column].type) == TOK_STORAGE_INDIRECT_EARRAY)
	{
		void ***pppSrcEarray = TokenStoreGetEArray_inline(tpi, &tpi[column], column, src, NULL);
		void ***pppDestEarray = TokenStoreGetEArray_inline(tpi, &tpi[column], column, dest, NULL);
		int iSrcSize = eaSize(pppSrcEarray);
		int iDestSize = eaSize(pppDestEarray);



		int iSharedSize = MIN(iSrcSize, iDestSize);


		for (i=0; i < iSharedSize; i++)
		{
			MultiVal *pSrcMVal = (*pppSrcEarray)[i];
			MultiVal *pDestMVal = (*pppDestEarray)[i];

			if (pSrcMVal == NULL)
			{
				if (pDestMVal)
				{
					//might as well just insert a NULL into the dest earray here... might not have to do a multivaldestroy later
					eaInsert(pppDestEarray, NULL, i);
					iDestSize++;
					if (iSharedSize < iSrcSize)
					{
						iSharedSize++;
					}
				}
			}
			else
			{
				if (!pDestMVal)
				{
					(*pppDestEarray)[i] = MultiValCreate();
				}

				MultiValCopyWith((MultiVal*)((*pppDestEarray)[i]), pSrcMVal, NULL, NULL, false);
			}
		}

		if (iSrcSize > iSharedSize)
		{
			for (i = iSharedSize; i < iSrcSize; i++)
			{
				MultiVal *pSrcMVal = (*pppSrcEarray)[i];
				if (pSrcMVal)
				{
					eaPush_dbg(pppDestEarray, MultiValCreate() TPC_MEMDEBUG_PARAMS_EARRAY);
					MultiValCopyWith((MultiVal*)((*pppDestEarray)[i]), pSrcMVal, NULL, NULL, false);
				}
				else
				{
					eaPush_dbg(pppDestEarray, NULL TPC_MEMDEBUG_PARAMS_EARRAY);
				}
			}
		}
		else
		{
			for (i = iDestSize - 1; i >= iSharedSize; i--)
			{
				MultiVal *pDestMVal = (*pppDestEarray)[i];
				if (pDestMVal)
				{
					MultiValDestroy(pDestMVal);
				}
				eaPop(pppDestEarray);
			}
		}
	}
	else
	{
		MultiVal **ppSrcBlockEarray = (MultiVal**)TokenStoreGetBlockEarray(tpi, column, src, NULL);
		MultiVal **ppDestBlockEarray = (MultiVal**)TokenStoreGetBlockEarray(tpi, column, dest, NULL);

		int iSrcSize = beaSize(ppSrcBlockEarray);
		int iDestSize = beaSize(ppDestBlockEarray);

		if (iDestSize > iSrcSize)
		{

			for (i=iSrcSize; i < iDestSize; i++)
			{
				MultiValClear((*ppDestBlockEarray) + i);
			}
		}

		beaSetSizeEx(ppDestBlockEarray, sizeof(MultiVal), NULL, iSrcSize, 0, NULL, NULL, false TPC_MEMDEBUG_PARAMS_EARRAY);

		for (i=0; i < iSrcSize; i++)
		{
			MultiValCopyWith((*ppDestBlockEarray) + i, (*ppSrcBlockEarray) + i, NULL, NULL, false);
		}
	}
}


int MultiVal_textdiffwithnull(char** estr, ParseTable tpi[], int column, void* newstruct, int index, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	MultiInfo	info;
	MultiVal*   pmv			= TokenStoreGetMultiVal(tpi, column, newstruct, index, NULL);
	U32			i;

	MultiValInfo(pmv, &info);

	if (info.dtype == MMT_NONE)
	{
		return 0;
	}

	estrConcatf_dbg(estr, caller_fname, line, "set %s = \"%08X ", prefix, pmv->type);

	DB_MV_INFO(textdiffwithnull, pmv)

	//Consume Size from stream if this is an EArray
	if (info.atype == MMA_EARRAY) 
	{
		estrConcatf_dbg(estr, caller_fname, line, "%d ", info.count);
	}

	for (i =0; i < info.count; i++)
	{
		switch(info.dtype)
		{
		case MMT_INT32:
			estrConcatf_dbg(estr, caller_fname, line, "%"FORM_LL"d ", INFO_TO_REF(U64, i));
			break;
		case MMT_INT64:
			estrConcatf_dbg(estr, caller_fname, line, "%"FORM_LL"d ", INFO_TO_REF(U64, i));
			break;
		case MMT_FLOAT32:
			estrConcatf_dbg(estr, caller_fname, line, "%g ", INFO_TO_REF(F32, i));
			break;
		case MMT_FLOAT64:	
			estrConcatf_dbg(estr, caller_fname, line, "%g ", INFO_TO_REF(F64, i));
			break;
		case MMT_STRING:	
			estrConcatf_dbg(estr, caller_fname, line, "%s ", pmv->str);
			assert (i == 0);
			break;

		default:
			break;
		}
	}
	estrConcat_dbg_inline(estr, "\"\n", 2, caller_fname, line);

	return 1;
}




