#pragma once

#include "StringUtil.h"

void WriteTextIntFlags(FILE* out, U32 iValue, void *subtable, bool bShowName);

void WriteQuotedString(FILE* out, const char* str, int tabs, int eol);

void WriteStringIntFlags(char **estr, U32 iVal, StaticDefineInt *pSubTable, const char *caller_fname, int line, ParseTable *pTPI, int iColumn, WriteTextFlags iFlags);

extern bool gbDontAssertOnSpacesInEnums;

static __forceinline const char *StaticDefineIntRevLookup_ForStructWriting(StaticDefineInt* list, int value, ParseTable *pTPI, int iColumn, WriteTextFlags iWriteTextFlags)
{
	const char *pRetVal = StaticDefineInt_FastIntToString(list, value);

	if ((iWriteTextFlags & WRITETEXTFLAG_STRUCT_BEING_WRITTEN) && !(iWriteTextFlags & WRITETEXTFLAG_SPACES_OK_IN_ENUMS))
	{
		if (!gbDontAssertOnSpacesInEnums)
			assertmsgf(!StringContainsWhitespace(pRetVal), "while writing parsetable %s column %d, trying to write out enum value \"%s\", which contains whitespace. This is generally illegal because it won't be able to be read back in. You can avoid this by adding WRITETEXTFLAG_SPACES_OK_IN_ENUMS to the writing call, or turn off this assert entirely (temporarily) by adding -DontAssertOnSpacesInEnums to the shared command line",
				ParserGetTableName(pTPI), iColumn, pRetVal);
	}


	return pRetVal;
}

void fixupSharedMemoryStruct(ParseTable pti[], void *structptr, char **ppFixupData);