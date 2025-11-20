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
#include "structDefines.h"

//so we can check error_occurred without function call overhead
#include "netprivate.h"

#include "textParserCallbacks_inline.h"
#include "TextParserCallbacks.h"
#define TEMP_ESTRING_CHARGE "will_destroy_immediately", __LINE__

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

int fastIntToString(int iValueIn, char outBuf[16])
{
	bool bNeg = false;
	char *pWriteHead;
	U32 iInternalValue;

	if (!iValueIn)
	{
		outBuf[15] = '0';
		return 1;
	}

	if (iValueIn < 0)
	{
		bNeg = true;
		if (iValueIn == INT_MIN)
		{
			iInternalValue = (U32)INT_MAX+1;
		}
		else
		{
			iInternalValue = -iValueIn;
		}
	}
	else
	{
		iInternalValue = iValueIn;
	}

	pWriteHead = outBuf + 15;
	while (iInternalValue)
	{
		*pWriteHead = iInternalValue % 10 + '0';
		iInternalValue /= 10;
		pWriteHead--;
	}

	if (bNeg)
	{
		*pWriteHead = '-';
		pWriteHead--;
	}

	return 15 - (pWriteHead - outBuf);
}


bool int_writetext(FILE* out, ParseTable tpi[], int column, void* structptr, int index, bool showname, bool ignoreInherited, int level, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	char *str = NULL;
	int value = TokenStoreGetInt_inline(tpi, &tpi[column], column, structptr, index, NULL);
	bool bCanSkip = !(tpi[column].type & TOK_REQUIRED) && (value == tpi[column].param) && !SHOULD_WRITE_DEFAULT(iWriteTextFlags, tpi, column, structptr);
	if (showname && bCanSkip) return false; // if default value, not necessary to write field


	if (showname) WriteNString(out, tpi[column].name,ParseTableColumnNameLen(tpi,column), level+1, 0);
	fputc(' ', out);
	
	if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_FLAGS)
	{
		WriteTextIntFlags(out, value, tpi[column].subtable, showname);
	}
	else
	{
		//optimization... when we're just writing a super-plain integer, don't go through all the complicated 
		//logic that makes the pretty printing work
		if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) || (iWriteTextFlags & WRITETEXTFLAG_WRITINGFORSQL) || tpi[column].subtable)
		{
			estrStackCreate(&str);
			int_writestring(tpi, column, structptr, index, &str, iWriteTextFlags, 0, 0, TEMP_ESTRING_CHARGE);
			WriteNString(out, str, estrLength(&str), 0, showname);
			estrDestroy(&str);
		}
		else
		{
			char temp[16];
			int iLen;

			iLen = fastIntToString(value, temp);
			fwrite(temp + 16 - iLen, iLen, 1, out);
			if (showname)
			{
				fwrite("\r\n", 2, 1, out);
			}
		}
	}	

	return !bCanSkip;

}

bool u8_writetext(FILE* out, ParseTable tpi[], int column, void* structptr, int index, bool showname, bool ignoreInherited, int level, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	U8 value = TokenStoreGetU8_inline(tpi, &tpi[column], column, structptr, index, NULL);
	bool bCanSkip = !(tpi[column].type & TOK_REQUIRED) && (value == tpi[column].param) && !SHOULD_WRITE_DEFAULT(iWriteTextFlags, tpi, column, structptr);

	if (showname && bCanSkip) return false; // if default value, not necessary to write field
	if (showname) WriteNString(out, tpi[column].name, ParseTableColumnNameLen(tpi,column), level+1, 0);
	WriteNString(out, " ", 1, 0, 0);

	if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_FLAGS)
	{
		WriteTextIntFlags(out, value, tpi[column].subtable, showname);
	}
	else
	{
		if (tpi[column].subtable)
		{
			WriteInt(out, value, 0, showname, tpi[column].subtable);
		}
		else
		{
			char temp[16];
			int iLen;

			iLen = fastIntToString((int)value, temp);
			fwrite(temp + 16 - iLen, iLen, 1, out);
			if (showname)
			{
				fwrite("\r\n", 2, 1, out);
			}
		}
	}

	return !bCanSkip;
}


bool int16_writetext(FILE* out, ParseTable tpi[], int column, void* structptr, int index, bool showname, bool ignoreInherited, int level, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	S16 value = TokenStoreGetInt16_inline(tpi, &tpi[column], column, structptr, index, NULL);
	bool bCanSkip = !(tpi[column].type & TOK_REQUIRED) && (value == tpi[column].param) && !SHOULD_WRITE_DEFAULT(iWriteTextFlags, tpi, column, structptr);
	if (showname && bCanSkip) return false; // if default value, not necessary to write field
	if (showname) WriteNString(out, tpi[column].name,ParseTableColumnNameLen(tpi,column), level+1, 0);
	WriteNString(out, " ", 1, 0, 0);
	
	if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_FLAGS)
	{
		WriteTextIntFlags(out, value, tpi[column].subtable, showname);
	}
	else
	{

		if (tpi[column].subtable)
		{
			WriteInt(out, value, 0, showname, tpi[column].subtable);
		}
		else
		{
			char temp[16];
			int iLen;

			iLen = fastIntToString((int)value, temp);
			fwrite(temp + 16 - iLen, iLen, 1, out);
			if (showname)
			{
				fwrite("\r\n", 2, 1, out);
			}
		}
	}	
	

	return !bCanSkip;
}


bool bit_writetext(FILE* out, ParseTable tpi[], int column, void* structptr, int index, bool showname, bool ignoreInherited, int level,WriteTextFlags iWriteTextFlags,  StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	U32 value = TokenStoreGetBit_inline(tpi, &tpi[column], column, structptr, index, NULL);
	bool bCanSkip = false;

	if (!(tpi[column].type & TOK_REQUIRED) && !SHOULD_WRITE_DEFAULT(iWriteTextFlags, tpi, column, structptr)) 
	{
		U32 defVal = 0;

		if (tpi[column].type & TOK_SPECIAL_DEFAULT)
		{
			char *pDefaultString;
			if (GetStringFromTPIFormatString(&tpi[column], "SPECIAL_DEFAULT", &pDefaultString))
			{
				defVal = atoi(pDefaultString);
			}
		}		
		
		if (value == defVal)
		{
			bCanSkip = true;
		}
	}

	if (showname && bCanSkip)
	{
		return false;
	}

	if (showname) WriteNString(out, tpi[column].name,ParseTableColumnNameLen(tpi,column), level+1, 0);
	WriteNString(out, " ",1, 0, 0);

	if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_FLAGS)
	{
		WriteTextIntFlags(out, value, tpi[column].subtable, showname);
	}
	else
	{
		if (tpi[column].subtable)
		{
			WriteInt(out, value, 0, showname, tpi[column].subtable);
		}
		else
		{
			char temp[16];
			int iLen;

			iLen = fastIntToString((int)value, temp);
			fwrite(temp + 16 - iLen, iLen, 1, out);
			if (showname)
			{
				fwrite("\r\n", 2, 1, out);
			}
		}
	}

	return !bCanSkip;
}




static bool sbNeedsEscapingBits[256] = {0};
static bool sbNeedsQuoteBits[256] = {0};
static bool sbNeedsQuoteBits_0[256] = {0};

AUTO_RUN_EARLY;
void EscapingAndQuotesInit(void)
{
	sbNeedsEscapingBits['\n'] = true;
	sbNeedsEscapingBits['\r'] = true;
	sbNeedsEscapingBits['\"'] = true;

	sbNeedsQuoteBits[' '] = true;
	sbNeedsQuoteBits[','] = true;
	sbNeedsQuoteBits['\n'] = true;
	sbNeedsQuoteBits['\r'] = true;
	sbNeedsQuoteBits['\t'] = true;
	sbNeedsQuoteBits['|'] = true;

	sbNeedsQuoteBits_0['#'] = true;
	sbNeedsQuoteBits_0['/'] = true;
	sbNeedsQuoteBits_0['<'] = true;
}


static int StringNeedsEscaping(const char* str)
{
	if (strchr(str, '\n')) return 1;
	if (strchr(str, '\r')) return 1;
	if (strchr(str, '\"')) return 1;
	return 0;
}

static int StringNeedsQuotes(const char* str)
{
	if (*str==0) return 1;   // need quotes around empty string
	if (strchr(str, ' ')) return 1;	// standard delimeters between tokens
	if (strchr(str, ',')) return 1;
	if (strchr(str, '\n')) return 1;
	if (strchr(str, '\r')) return 1;
	if (strchr(str, '\t')) return 1;
	if (strchr(str, '|')) return 1; //causes confusion with earrays of strings
	if (str[0] == '#') return 1; // could cause confusion with comments
	if (str[0] == '/') return 1;
	if (str[0] == '<') return 1; // could cause confusion with escaping
	return 0;
}

static int StringNeedsEscaping_Fast(const char* str)
{
	while (*str)
	{
		if (sbNeedsEscapingBits[*str])
		{
			return 1;
		}

		str++;
	}

	return false;
}

static int StringNeedsQuotes_Fast(const char* str)
{
	if (*str==0) return 1;   // need quotes around empty string

	if (sbNeedsQuoteBits_0[*str])
	{
		return 1;
	}

	do
	{
		if (sbNeedsQuoteBits[*str])
		{
			return 1;
		}

		str++;
	}
	while (*str);

	return false;
}


__inline void WriteString(FILE* out, const char* str, int tabs, int eol)
{
	if (tabs > 0)
	{
		if (tabs < 32)
			fwrite("\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t", tabs, sizeof(char), out);
		else
			while (tabs-- > 0) 
				fwrite("\t", 1, sizeof(char), out);
	}

	fwrite(str, strlen(str), sizeof(char), out);
	if (eol) fwrite("\r\n", 2, sizeof(char), out);
}


void WriteQuotedString(FILE* out, const char* str, int tabs, int eol)
{
	int escaped, quoted;
	char *pBuffer;
	int iStartingLen;

	if (!str) str = "";

	escaped = StringNeedsEscaping_Fast(str);
	if (escaped)
	{
		int newlen;
		iStartingLen = (int)strlen(str);

		pBuffer = ScratchAlloc(iStartingLen * 2 + 5);
		newlen = TokenizerEscape(pBuffer, str);
		WriteNString(out, pBuffer, newlen, tabs, eol);
		ScratchFree(pBuffer);
	}
	else
	{
		quoted = StringNeedsQuotes_Fast(str);
		if (quoted) WriteString(out, "\"", tabs, 0);
		WriteString(out, str, 0, 0);
		if (eol)
		{
			if (quoted) 
				fwrite("\"\r\n", 3, 1, out);
			else
				fwrite("\r\n", 2, 1, out);
		}
		else
		{
			if (quoted)
			{
				fputc('"', out);
			}
		}
	}
}


bool string_writetext(FILE* out, ParseTable tpi[], int column, void* structptr, int index, bool showname, bool ignoreInherited, int level, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	char* defaultval = (char*)tpi[column].param;
	const char* str = TokenStoreGetString_inline(tpi, &tpi[column], column, structptr, index, NULL);
	bool bCanSkip = false;

	if (TokenStoreGetStorageType(tpi[column].type)!=TOK_STORAGE_INDIRECT_SINGLE)
	{
		defaultval = NULL;
	
		if (tpi[column].type & TOK_SPECIAL_DEFAULT)
		{
			GetStringFromTPIFormatString(&tpi[column], "SPECIAL_DEFAULT", &defaultval);
		}
	}

	if (!(tpi[column].type & TOK_REQUIRED) && !(iWriteTextFlags & WRITETEXTFLAG_ISREQUIRED))
	{
		if (!defaultval && (!str || !str[0])) 
		{
			bCanSkip = true; // if default value, not necessary to write field
		}
		else if (str && str[0] && defaultval && stricmp(str, defaultval)==0 && !SHOULD_WRITE_DEFAULT(iWriteTextFlags, tpi, column, structptr))
		{
			bCanSkip = true; // default value
		}
	}

	if (showname && bCanSkip)
	{
		return false;
	}

	if (showname) WriteNString(out, tpi[column].name,ParseTableColumnNameLen(tpi,column), level+1, 0);
	
	if (~iWriteTextFlags & WRITETEXTFLAG_WRITINGFORSQL)
		WriteNString(out, " ", 1, 0, 0);

	if (iWriteTextFlags & WRITETEXTFLAG_WRITINGFORSQL)
	{
		if (str && str[0]) 
		{
			WriteString(out, "\"", 0, 0);
			WriteString(out, str, 0, 0);
			WriteString(out, "\"", 0, 0);
		}
	}
	else if (iWriteTextFlags & WRITETEXTFLAG_NO_QUOTING_OR_ESCAPING_STRINGS)
	{
		WriteString(out, str ? str : "", 0, 0);
	}
	else
	{
		WriteQuotedString(out, str, 0, showname);
	}

	return !bCanSkip;
}


#define PART_1 "set "
#define PART_2 " = \""
#define PART_3 "\"\n"

void int_writehdiff(char** estr, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, char *prefix, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	int value = TokenStoreGetInt_inline(tpi, &tpi[column], column, newstruct, index, NULL);
	int format = TOK_GET_FORMAT_OPTIONS(tpi[column].format);

	if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_FLAGS)
	{
		estrConcatf_dbg(estr, caller_fname, line, "set %s = \"", prefix);

		WriteStringIntFlags(estr, value, tpi[column].subtable, caller_fname, line, tpi, column, WRITETEXTFLAG_STRUCT_BEING_WRITTEN);
		
		estrConcatf_dbg(estr, caller_fname, line, "\"\n");
		return;
	}

	// Only do this for reversible formats
	switch (format)
	{
	case TOK_FORMAT_IP:
		estrConcatf_dbg(estr, caller_fname, line, "set %s = \"%s\"\n", prefix, makeIpStr(value));
	xcase TOK_FORMAT_PERCENT:
		estrConcatf_dbg(estr, caller_fname, line, "set %s = \"%02d%%\"\n", prefix, value);
	xcase TOK_FORMAT_UNSIGNED:
		estrConcatf_dbg(estr, caller_fname, line, "set %s = \"%u\"\n", prefix, value);
	xcase TOK_FORMAT_DATESS2000: 
		estrConcatf_dbg(estr, caller_fname, line, "set %s = \"%s\"\n",prefix, timeGetDateStringFromSecondsSince2000(value));
	xdefault:
		// we do reverse define lookup here if possible
		{
			const char* rev = NULL;
			if (tpi[column].subtable)
				rev = StaticDefineIntRevLookup_ForStructWriting(tpi[column].subtable, value, tpi, column, WRITETEXTFLAG_STRUCT_BEING_WRITTEN);
			if (rev)
			{
				int iPrefixLen = (int)strlen(prefix);
				int iRevLen = (int)strlen(rev);

				WriteSetValueStringForTextDiff(estr, prefix, iPrefixLen, rev, iRevLen, caller_fname, line);
			}
			else
			{
				int iPrefixLen = (int)strlen(prefix);
				

				if(eFlags & TEXTDIFFFLAG_JSON_SECS_TO_RFC822 && GetBoolFromTPIFormatString(&tpi[column], "JSON_SECS_TO_RFC822"))
				{
					char intBuff[128];
					int iIntlen = 0;
					
					sprintf(intBuff, "%s", timeGetRFC822StringFromSecondsSince2000(value));
					iIntlen = (int)strlen(intBuff);

					WriteSetValueStringForTextDiff(estr, prefix, iPrefixLen, intBuff, iIntlen,caller_fname,line);
				}
				else
				{
					char intBuff[16];
					int iIntLen = fastIntToString(value, intBuff);

					WriteSetValueStringForTextDiff(estr, prefix, iPrefixLen, intBuff + 16 - iIntLen, iIntLen, caller_fname, line);
				}
			}
		}
	};
}




int int_textdiffwithnull(char** estr, ParseTable tpi[], int column, void* newstruct, int index, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	int iNewVal = TokenStoreGetInt_inline(tpi, &tpi[column], column, newstruct, index, NULL);
	int iDefault;
	int format;

	//special case... for earrays, always want to write out the int, even if it's 0. This is probably not "correct" per se, but
	//keeps the behavior identical to the old diffing code
	if (!(tpi[column].type & TOK_EARRAY))
	{
		iDefault = (tpi[column].type & (TOK_FIXED_ARRAY))?0:tpi[column].param;
	
		if (iDefault == iNewVal)
		{
			return 0;
		}
	}

	format = TOK_GET_FORMAT_OPTIONS(tpi[column].format);


	if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_FLAGS)
	{
		WriteSetValueStringForTextDiff_ThroughQuote(estr, prefix, iPrefixLen, caller_fname, line);
		WriteStringIntFlags(estr, iNewVal, tpi[column].subtable, caller_fname, line, tpi, column, WRITETEXTFLAG_STRUCT_BEING_WRITTEN);		
		estrConcat_dbg_inline(estr, "\"\n", 2, caller_fname, line);
		return 1;
	}

	// Only do this for reversible formats
	switch (format)
	{
	xcase TOK_FORMAT_IP:
		{
			char *pIP = makeIpStr(iNewVal);
			WriteSetValueStringForTextDiff(estr, prefix, iPrefixLen, pIP, (int)strlen(pIP), caller_fname, line);
		}
	xcase TOK_FORMAT_PERCENT:
		WriteSetValueStringForTextDiff_ThroughQuote(estr, prefix, iPrefixLen, caller_fname, line);
		estrConcatf_dbg(estr, caller_fname, line, "%02d%%\"\n", iNewVal);
	xcase TOK_FORMAT_UNSIGNED:
		WriteSetValueStringForTextDiff_ThroughQuote(estr, prefix, iPrefixLen, caller_fname, line);
		estrConcatf_dbg(estr, caller_fname, line, "%u\"\n", iNewVal);

	xcase TOK_FORMAT_DATESS2000: 
		WriteSetValueStringForTextDiff_ThroughQuote(estr, prefix, iPrefixLen, caller_fname, line);
		estrConcatf_dbg(estr, caller_fname, line, "%s\"\n", timeGetDateStringFromSecondsSince2000(iNewVal));

	xdefault:
		// we do reverse define lookup here if possible
		{
			const char* pEnumName = NULL;
			if (tpi[column].subtable)
				pEnumName = StaticDefineIntRevLookup_ForStructWriting(tpi[column].subtable, iNewVal, tpi, column, WRITETEXTFLAG_STRUCT_BEING_WRITTEN);
			if (pEnumName)
			{
				int iEnumLen = (int)strlen(pEnumName);
				WriteSetValueStringForTextDiff(estr, prefix, iPrefixLen, pEnumName, iEnumLen, caller_fname, line);
			}
			else
			{
				char intBuff[16];
				int iIntLen = fastIntToString(iNewVal, intBuff);
				WriteSetValueStringForTextDiff(estr, prefix, iPrefixLen, intBuff + 16 - iIntLen, iIntLen, caller_fname, line);
			}
		}
	};
	return 1;
}

int u8_textdiffwithnull(char** estr, ParseTable tpi[], int column, void* newstruct, int index, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	U8 iNewVal = TokenStoreGetU8_inline(tpi, &tpi[column], column, newstruct, index, NULL);
	U8 iDefault = (tpi[column].type & (TOK_FIXED_ARRAY | TOK_EARRAY))?0:tpi[column].param;

	//u8_writehdiff doesn't handle enums, so we want to just make the behavior identical
	if (iNewVal != iDefault)
	{
		char intBuff[16];
		int iIntLen = fastIntToString(iNewVal, intBuff);
		WriteSetValueStringForTextDiff(estr, prefix, iPrefixLen, intBuff + 16 - iIntLen, iIntLen, caller_fname, line);
		return 1;
	}

	return 0;
}

int int16_textdiffwithnull(char** estr, ParseTable tpi[], int column, void* newstruct, int index, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	S16 iNewVal = TokenStoreGetInt16_inline(tpi, &tpi[column], column, newstruct, index, NULL);
	S16 iDefault = (tpi[column].type & (TOK_FIXED_ARRAY | TOK_EARRAY))?0:tpi[column].param;

	//int16_writehdiff doesn't handle enums, so we want to just make the behavior identical
	if (iNewVal != iDefault)
	{
		char intBuff[16];
		int iIntLen = fastIntToString(iNewVal, intBuff);
		WriteSetValueStringForTextDiff(estr, prefix, iPrefixLen, intBuff + 16 - iIntLen, iIntLen, caller_fname, line);
		return 1;
	}

	return 0;
}

int bit_textdiffwithnull(char** estr, ParseTable tpi[], int column, void* newstruct, int index, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	S16 iNewVal = TokenStoreGetBit_inline(tpi, &tpi[column], column, newstruct, index, NULL);
	
	//if there's a special default, just always write out no matter what. This is minorly inefficient, but
	//properly mimics the behavior of bit_writehdiff
	if (iNewVal || (tpi[column].type & TOK_SPECIAL_DEFAULT))
	{
		char intBuff[16];
		int iIntLen = fastIntToString(iNewVal, intBuff);
		WriteSetValueStringForTextDiff(estr, prefix, iPrefixLen, intBuff + 16 - iIntLen, iIntLen, caller_fname, line);
		return 1;
	}

	return 0;
}


int int64_textdiffwithnull(char** estr, ParseTable tpi[], int column, void* newstruct, int index, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	S64 iNewVal = TokenStoreGetInt64_inline(tpi, &tpi[column], column, newstruct, index, NULL);
	S64 iDefault = (tpi[column].type & (TOK_FIXED_ARRAY | TOK_EARRAY))?0:tpi[column].param;

	//int64_writehdiff doesn't handle enums, so we want to just make the behavior identical
	if (iNewVal != iDefault)
	{
		WriteSetValueStringForTextDiff_ThroughQuote(estr, prefix, iPrefixLen, caller_fname, line);
		estrConcatf_dbg(estr, caller_fname, line, "%"FORM_LL"d\"\n",iNewVal);
		return 1;
	}

	return 0;
}


int string_textdiffwithnull(char** estr, ParseTable tpi[], int column, void* newstruct, int index, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	const char *pNewString = TokenStoreGetString_inline(tpi, &tpi[column], column, newstruct, index, NULL);

	bool bWrite = false;
	const char *pDefaultString;

	int type = TokenStoreGetStorageType(tpi[column].type);

	//special case... always write out the value in an earray, no matter if it's NULL or what
	if (type == TOK_STORAGE_INDIRECT_EARRAY)
	{
		if (!pNewString)
		{
			WriteSetValueStringForTextDiff(estr, prefix, iPrefixLen, NULL, 0, caller_fname, line);
			return 1;
		}
		else
		{
			bWrite = true;
		}
	}
	else
	{

		if (type == TOK_STORAGE_INDIRECT_SINGLE)
		{
			pDefaultString = (char*)tpi[column].param;
		}
		else
		{
			if (tpi[column].type & TOK_SPECIAL_DEFAULT)
			{
				GetStringFromTPIFormatString(&tpi[column], "SPECIAL_DEFAULT", &pDefaultString);
			}
			else
			{
				pDefaultString = NULL;
			}
		}
	

		if (!pNewString && !pDefaultString)
		{
			return 0;
		}
	

		if (!pNewString)
		{
			WriteSetValueStringForTextDiff(estr, prefix, iPrefixLen, NULL, 0, caller_fname, line);
			return 1;
		}

		if (!pDefaultString)
		{
			bWrite = true;
		}
		else
		{
			if (tpi[column].type & TOK_CASE_SENSITIVE)
				bWrite = !!strcmp(pNewString, pDefaultString);
			else
				bWrite = !!stricmp(pNewString, pDefaultString);
		}
	}


	if (bWrite)
	{
		WriteSetValueStringForTextDiff_ThroughQuote(estr, prefix, iPrefixLen, caller_fname, line);
		estrAppendEscaped_dbg(estr, pNewString, caller_fname, line);
		estrConcat_dbg_inline(estr, "\"\n", 2, caller_fname, line);
		return 1;
	}

	return 0;
}

int float_textdiffwithnull(char** estr, ParseTable tpi[], int column, void* newstruct, int index, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	int fa = TOK_GET_FLOAT_ROUNDING(tpi[column].type);
	F32 fDefault;
	F32 fNew = TokenStoreGetF32_inline(tpi, &tpi[column], column, newstruct, index, NULL);
	bool bWrite = false;

	//in an earray of floats, always write the float even if it's zero, because that's the way 
	//writehdiff works
	if (tpi[column].type & TOK_EARRAY)
	{
		bWrite = true;
	}
	else
	{


		fDefault = (tpi[column].type & (TOK_FIXED_ARRAY))?0:GET_FLOAT_FROM_INTPTR(tpi[column].param);
	
		if (!fNew)
			fNew = 0; // Replace -0 with 0

		if (fa)
		{
			int l = ParserPackFloat(fDefault, fa);
			int r = ParserPackFloat(fNew, fa);
			bWrite = (l != r);
		}
		else
		{
			bWrite = fDefault != fNew;
		}
	}

	if (bWrite)
	{
		char temp[32];
		int iDigits;

		iDigits = fastFloatToString_inline(fNew, SAFESTR(temp));
		WriteSetValueStringForTextDiff(estr, prefix, iPrefixLen, temp, iDigits, caller_fname, line);

		return 1;
	}

	return 0;
}

int struct_textdiffwithnull(char** estr, ParseTable tpi[], int column, void* newstruct, int index, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	void* newp = TokenStoreGetPointer_inline(tpi, &tpi[column], column, newstruct, index, NULL);
	
	if (newp)
	{
		//for direct embedded structs, don't print create
		if (!(tpi[column].type & TOK_INDIRECT))
		{
			eFlags |= TEXTDIFFFLAG_DONTWRITECREATE_NONRECURSING;
		}

		return StructTextDiffWithNull_dbg(estr,tpi[column].subtable,newp,prefix, iPrefixLen, iOptionFlagsToMatch,iOptionFlagsToExclude,
			eFlags, caller_fname, line);
	}
	else
	{
		return 0;
	}
}

int poly_textdiffwithnull(char** estr, ParseTable tpi[], int column, void* newstruct, int index, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	AssertOrProgrammerAlert("TRYING_TO_TEXT_DIFF_POLY", "Trying to write out a text diff for field %s in %s, which is a poly type. This is totally illegal",
		tpi[column].name, ParserGetTableName(tpi));
	return 0;



/*	ParseTable* polytable = tpi[column].subtable;
	void* newp = TokenStoreGetPointer_inline(tpi, &tpi[column], column, newstruct, index, NULL);

	if (newp)
	{
		int newpolytype;
		StructDeterminePolyType(polytable, newp, &newpolytype);


		return StructTextDiffWithNull_dbg(estr,polytable[newpolytype].subtable,newp,prefix,iPrefixLen, iOptionFlagsToMatch,iOptionFlagsToExclude,eFlags, caller_fname, line);
	}

	return 0;*/


}


int reference_textdiffwithnull(char** estr, ParseTable tpi[], int column, void* newstruct, int index, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	const char *pNewRefString = TokenStoreGetRefString_inline(tpi, &tpi[column], column, newstruct, index, NULL);
	if (stricmp_safe(pNewRefString, NULL) != 0)
	{
		WriteSetValueStringForTextDiff_ThroughQuote(estr, prefix, iPrefixLen, caller_fname, line);
		estrAppendEscaped_dbg(estr, pNewRefString, caller_fname, line);
		estrConcat_dbg_inline(estr, "\"\n", 2, caller_fname, line);
		return 1;
	}
	return 0;

}

