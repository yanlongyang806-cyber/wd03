#pragma once

#include "structInternals.h"
#include "textparser.h"
#include "structnet.h"
#include "tokenstore.h"
#include "net/net.h"
#include "textParserHtml.h"
#include "error.h"
#include "netprivate.h"
#include "structPack.h"
#include "scratchstack.h"
#include "TextParserXML.h"
#include "TextParserJSON.h"
#include "TextParserHDF.h"
#include "tokenStore_inline.h"
#include "serialize.h"
#include "objpath.h"
#include "TextParserUtils_inline.h"
#include "bitstream.h"
#include "ContinuousBuilderSupport.h"
#include "UtilitiesLib.h"
#include "mathutil.h"
#include "crypt.h"

#define MEM_ALIGN(addr, size) if ((addr) % size) { addr += (size - ((addr) % size)); }


// this define is used to check if we are reading garbage data from a bin file
#define MAX_EARRAY_BIN_SIZE 1200000

//random global variables from TextParserCallbacks.c
extern bool gbLateCreateIndexedEArrays;
extern bool g_writeSizeReport;
extern char *g_sizeReportString;

void vec3_applydynop(ParseTable tpi[], int column, void* dstStruct, void* srcStruct, int index, DynOpType optype, const F32* values, U8 uiValuesSpecd, U32* seed);

//for structs with string keys (but NOT int or enum keys or other things that can be converted to strings),
//given a struct, returns a string pointer to its key. This can be embedded or not. Returns NULL if it's not 
//string keyed, or can't get it fast for whatever reason
static __forceinline char *ParserGetInPlaceKeyString_inline(ParseTable table[], void *pStruct)
{
	ParseTableInfo *pTableInfo = ParserGetTableInfo(table);

	if (pTableInfo && pTableInfo->IndexedCompareCache.bCanGetStringKeysFast)
	{

		void *pOffsetPointer = (void*)(((U8*)pStruct) + pTableInfo->IndexedCompareCache.storeoffset);


		switch (pTableInfo->IndexedCompareCache.eCompareType)
		{
		case INDEXCOMPARETYPE_STRING_EMBEDDED:
			return (char*)pOffsetPointer;

		case INDEXCOMPARETYPE_STRING_POINTER:
			return (char*)((*((void**)pOffsetPointer)));
		}
	}


	return NULL;
}

//gets the key of a struct as a string, appends it onto the end of an estring. Will usually run in "fast" mode,
//unless your key is something oddball like a struct or a formatted string
static __forceinline bool ParserGetKeyStringDbg_inline(ParseTable table[], void *pStruct, char **ppOutEString, bool bEscape, const char *caller_fname, int iLine)
{
	ParseTableInfo *pTableInfo = ParserGetTableInfo(table);

	if (pTableInfo && pTableInfo->IndexedCompareCache.bCanGetStringKeysFast)
	{
		const char *pStr;
		int iVal;

		void *pOffsetPointer = (void*)(((U8*)pStruct) + pTableInfo->IndexedCompareCache.storeoffset);


		switch (pTableInfo->IndexedCompareCache.eCompareType)
		{
		case INDEXCOMPARETYPE_STRING_EMBEDDED:
			pStr = (char*)pOffsetPointer;
			goto DoString;

		case INDEXCOMPARETYPE_STRING_POINTER:
			pStr = (char*)((*((void**)pOffsetPointer)));
			goto DoString;

		case INDEXCOMPARETYPE_REF:
			pStr = RefSystem_StringFromHandle(pOffsetPointer);
			goto DoString;

		case INDEXCOMPARETYPE_INT8:
			iVal = *((U8*)pOffsetPointer);
			goto DoInt;
		case INDEXCOMPARETYPE_INT16:
			iVal =  *((S16*)pOffsetPointer);
			goto DoInt;
		case INDEXCOMPARETYPE_INT32:
			iVal =  *((S32*)pOffsetPointer);
			goto DoInt;

		case INDEXCOMPARETYPE_INT64:
			{
				S64 siVal = *((S64*)pOffsetPointer);
				estrConcatf(ppOutEString, "%I64d", siVal);
				return true;
			}

		default:
			assertmsgf(0, "Unsupported compare type %d in ParserGetKeyString", pTableInfo->IndexedCompareCache.eCompareType);
		}


DoInt:
		{
			char temp[16];
			int iDigits = fastIntToString_inline(iVal, temp);
			int iCurLen = estrLength(ppOutEString);
			estrConcat_dbg_inline(ppOutEString, temp + 16 - iDigits, iDigits, caller_fname, iLine);
			return true;
		}


DoString:
		{
			if (!pStr || !pStr[0])
			{
				return false;
			}


			if (!bEscape && isDevelopmentMode())
			{
				if (strpbrk(pStr, "\"\\\n"))
				{
					ErrorOrAlert("BAD_ESCAPING_KEY", "Found %s, key for %s. It contains characters that would need to be escaped. Tell Alex W",
						pStr, ParserGetTableName(table));
				}
			}

			if (bEscape)
			{
				estrAppendEscaped_dbg(ppOutEString, pStr, caller_fname, iLine);
			}
			else
			{
				estrConcat_dbg_inline(ppOutEString, pStr, (int)strlen(pStr), caller_fname, iLine);
			}
			return true;
		}
	}
	else
	{
		if (!bEscape && isDevelopmentMode())
		{
			bool bRetVal;
			int iLen = estrLength(ppOutEString);
			bRetVal = objGetKeyEString(table, pStruct, ppOutEString);
			if (strpbrk((*ppOutEString) + iLen, "\"\\\n"))
			{
				ErrorOrAlert("BAD_ESCAPING_KEY", "(old school) Found %s, key for %s. It contains characters that would need to be escaped. Tell Alex W",
					(*ppOutEString) + iLen, ParserGetTableName(table));
			}
			return bRetVal;
		}

		if (bEscape)
		{
			return objGetEscapedKeyEString(table, pStruct, ppOutEString);
		}
		else
		{
			return objGetKeyEString(table, pStruct, ppOutEString);
		}
	}


}


//used by writeHdiff and textDiffWithNull... basically equivalent to estrConcatf(estr, "set %s = \"%s\"\n");

#define PART_1 "set "
#define PART_2 " = \""
#define PART_3 "\"\n"

static __forceinline void WriteSetValueStringForTextDiff(char **ppOutString, const char *pName, int iNameLen, const char *pVal, int iValLen,
	const char *caller_fname, int line)
{
	int iEstrLen = estrLength(ppOutString);
	char *pWriteHead;

	estrForceSize_dbg(ppOutString, iEstrLen + iNameLen + iValLen + (sizeof(PART_1) + sizeof(PART_2) + sizeof(PART_3) - 3),
		caller_fname, line);

	pWriteHead = (*ppOutString) + iEstrLen;
	memcpy(pWriteHead, PART_1, sizeof(PART_1) - 1);
	pWriteHead += sizeof(PART_1) - 1;

	memcpy(pWriteHead, pName, iNameLen);
	pWriteHead += iNameLen;

	memcpy(pWriteHead, PART_2, sizeof(PART_2) - 1);
	pWriteHead += sizeof(PART_2) - 1;


	if (iValLen)
	{
		memcpy(pWriteHead, pVal, iValLen);
		pWriteHead += iValLen;
	}

	memcpy(pWriteHead, PART_3, sizeof(PART_3) - 1);

}
static __forceinline void WriteSetValueStringForTextDiff_ThroughQuote(char **ppOutString, const char *pName, int iNameLen,
	const char *caller_fname, int line)
{
	int iEstrLen = estrLength(ppOutString);
	char *pWriteHead;

	estrForceSize_dbg(ppOutString, iEstrLen + iNameLen + 8,
		caller_fname, line);

	pWriteHead = (*ppOutString) + iEstrLen;
	memcpy(pWriteHead, PART_1, sizeof(PART_1) - 1);
	pWriteHead += sizeof(PART_1) - 1;

	memcpy(pWriteHead, pName, iNameLen);
	pWriteHead += iNameLen;

	memcpy(pWriteHead, PART_2, sizeof(PART_2) - 1);
}	



// The WRITETEXTFLAG_WRITEDEFAULTSIFUSED flag specifies that we need to write values even if they are default,
// if they are marked as used bits. We could optimize this by putting the offset of the USEDFIELD directly
// into the parsetable somewhere so we don't need to look up the columns...
#define SHOULD_WRITE_DEFAULT(flags, pti, column, structptr) (((flags) & WRITETEXTFLAG_WRITEDEFAULTSIFUSED) && TokenIsSpecified((pti), (column), (structptr), -1))




static __forceinline void WriteNString(FILE* out, const char* str, size_t len, int tabs, int eol)
{
	if (len)
	{
		if (tabs > 0)
		{
			if (tabs < 32) //31 tabs here
				fwrite("\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t", tabs, sizeof(char), out);
			else
				while (tabs-- > 0) 
					fwrite("\t", 1, sizeof(char), out);
		}
		fwrite(str, len, sizeof(char), out);
	}

	if (eol) 
		fwrite("\r\n", 2, sizeof(char), out);
}

static __forceinline bool nonarray_initstruct(ParseTable tpi[], int column, void* structptr, int index)
{
	initstruct_f initstructFunc = TYPE_INFO(tpi[column].type).initstruct;
	if (initstructFunc)
		return initstructFunc(tpi, column, structptr, index);

	return false;
}

static __forceinline bool nonarray_destroystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index)
{
	StructTypeField field_type = TOK_GET_TYPE(ptcc->type);
	switch(field_type) {
	case TOK_STRING_X: 
	case TOK_CURRENTFILE_X:
	case TOK_FILENAME_X:
		return string_destroystruct(tpi, ptcc, column, structptr, index);

	case TOK_REFERENCE_X: return reference_destroystruct(tpi, ptcc, column, structptr, index);
	case TOK_STRUCT_X:    return struct_destroystruct(tpi, ptcc, column, structptr, index);

	default: {
		// This case works for all types using "g_tokentable"
		destroystruct_f destroystructFunc = TYPE_INFO(ptcc->type).destroystruct_func;
		if (destroystructFunc)
			return destroystructFunc(tpi, ptcc, column, structptr, index);
		}
	}
	return DESTROY_NEVER;
}

static __forceinline int nonarray_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult)
{
	// Hardcode most common types in here to avoid indirect function pointer call for more
	// frequent types.  Functions here should always match those in "g_tokentable".
	StructTypeField field_type = TOK_GET_TYPE(ptcc->type);
	switch(field_type) {
	case TOK_IGNORE:   return ignore_parse(tok, tpi, ptcc, column, structptr, index, parseResult);
	case TOK_START:    return 0;
	case TOK_END:      return end_parse(tok, tpi, ptcc, column, structptr, index, parseResult);
	case TOK_U8_X:     return u8_parse(tok, tpi, ptcc, column, structptr, index, parseResult);
	case TOK_INT16_X:  return int16_parse(tok, tpi, ptcc, column, structptr, index, parseResult);
	case TOK_INT_X:    return int_parse(tok, tpi, ptcc, column, structptr, index, parseResult);
	case TOK_INT64_X:  return int64_parse(tok, tpi, ptcc, column, structptr, index, parseResult);
	case TOK_F32_X:    return float_parse(tok, tpi, ptcc, column, structptr, index, parseResult);
	case TOK_STRING_X: return string_parse(tok, tpi, ptcc, column, structptr, index, parseResult);
	case TOK_REFERENCE_X: return reference_parse(tok, tpi, ptcc, column, structptr, index, parseResult);
	case TOK_STRUCT_X: return struct_parse(tok, tpi, ptcc, column, structptr, index, parseResult);
	default: {
		// This case works for all types using "g_tokentable"
		parse_f parseFunc = TYPE_INFO(ptcc->type).parse_func;
		if (parseFunc)
			return parseFunc(tok, tpi, ptcc, column, structptr, index, parseResult);
		return 0;
		}
	}
}

static __forceinline bool nonarray_writetext(FILE* out, ParseTable tpi[], int column, void* structptr, int index, bool showname, bool ignoreInherited, int level, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	writetext_f writetextFunc = TYPE_INFO(tpi[column].type).writetext;
	if (writetextFunc)
		return writetextFunc(out, tpi, column, structptr, index, showname, ignoreInherited, level, iWriteTextFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
	return false;
}

static __forceinline TextParserResult nonarray_writebin(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	writebin_f writebinFunc = TYPE_INFO(tpi[column].type).writebin;
	if (writebinFunc)
	{
		return writebinFunc(file, pLayoutFile, pFileList, tpi, column, structptr, index, datasum, iOptionFlagsToMatch, iOptionFlagsToExclude);
	}
	return PARSERESULT_SUCCESS;
}

static __forceinline TextParserResult nonarray_readbin(SimpleBufHandle file, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	readbin_f readbinFunc;
#ifdef _XBOX
	//LDM: The indirect function calls are a large performance hit on xbox, since 70% of calls are for floats I added a case to avoid them
	if (TOK_GET_TYPE(tpi[column].type) == TOK_F32_X)
		return float_readbin(file, tpi, column, structptr, index, datasum, iOptionFlagsToMatch, iOptionFlagsToExclude);
#endif
	readbinFunc = TYPE_INFO(tpi[column].type).readbin;
	if (readbinFunc)
		return readbinFunc(file, pFileList, tpi, column, structptr, index, datasum, iOptionFlagsToMatch, iOptionFlagsToExclude);
	return PARSERESULT_SUCCESS;
}


static __forceinline void nonarray_writehdiff(char** estr, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, char *prefix, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	writehdiff_f writehdiffFunc;
	int type = TOK_GET_TYPE(tpi[column].type);


	switch (type)
	{
	case TOK_INT_X:
		int_writehdiff(estr, tpi, column, oldstruct, newstruct, index, prefix, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, caller_fname, line);
		return;
	case TOK_F32_X:
		float_writehdiff(estr, tpi, column, oldstruct, newstruct, index, prefix, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, caller_fname, line);
		return;
	case TOK_STRING_X:
		string_writehdiff(estr, tpi, column, oldstruct, newstruct, index, prefix, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, caller_fname, line);
		return;
	case TOK_STRUCT_X:
		struct_writehdiff(estr, tpi, column, oldstruct, newstruct, index, prefix, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, caller_fname, line);
		return;
	}
	
	
	writehdiffFunc = TYPE_INFO(tpi[column].type).writehdiff;
	if (writehdiffFunc)
		writehdiffFunc(estr,tpi,column,oldstruct,newstruct,index,prefix,iOptionFlagsToMatch,iOptionFlagsToExclude, eFlags, caller_fname, line);
}

static __forceinline void nonarray_writebdiff(StructDiff *diff, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, ObjectPath *parentPath, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool invertExcludeFlags, const char *caller_fname, int line)
{
	writebdiff_f writebdiffFunc = TYPE_INFO(tpi[column].type).writebdiff;
	if (writebdiffFunc)
		writebdiffFunc(diff,tpi,column,oldstruct,newstruct,index,parentPath,iOptionFlagsToMatch,iOptionFlagsToExclude, invertExcludeFlags, caller_fname, line);
}


static __forceinline bool nonarray_senddiff(Packet* pak, ParseTable tpi[], int column, const void* oldstruct, const void* newstruct, int index, enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB)
{
	senddiff_f senddiffFunc = TYPE_INFO(tpi[column].type).senddiff;
	if (senddiffFunc)
		return senddiffFunc(pak, tpi, column, oldstruct, newstruct, index, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude, pGenerateFlagsCB);
	return false;
}

static __forceinline bool nonarray_recvdiff(Packet* pak, ParseTable tpi[], int column, void* structptr, int index, enumRecvDiffFlags eFlags)
{
	recvdiff_f recvdiffFunc = TYPE_INFO(tpi[column].type).recvdiff;
	if (recvdiffFunc)
		return recvdiffFunc(pak, tpi, column, structptr, index, eFlags);

	return 1;
}

static __forceinline bool nonarray_bitpack(ParseTable tpi[], int column, const void *structptr, int index, PackedStructStream *pack)
{
	bitpack_f bitpackFunc = TYPE_INFO(tpi[column].type).bitpack;
	assertmsgf(bitpackFunc, "Type %s being bitpacked and missing bitpack_f", TYPE_INFO(tpi[column].type).name_direct_single);
	if (bitpackFunc)
		return bitpackFunc(tpi, column, structptr, index, pack);
	return false;
}

static __forceinline void nonarray_unbitpack(ParseTable tpi[], int column, void *structptr, int index, PackedStructStream *pack)
{
	unbitpack_f unbitpackFunc = TYPE_INFO(tpi[column].type).unbitpack;
	assertmsgf(unbitpackFunc, "Type %s being unbitpacked and missing unbitpack_f", TYPE_INFO(tpi[column].type).name_direct_single);
	if (unbitpackFunc)
		unbitpackFunc(tpi, column, structptr, index, pack);
}


static __forceinline TextParserResult nonarray_writestring(ParseTable tpi[], int column, void* structptr, int index, char** estr, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *caller_fname, int line)
{
	writestring_f writestringFunc = TYPE_INFO(tpi[column].type).writestring;
	if (writestringFunc)
		return writestringFunc(tpi, column, structptr, index, estr, iWriteTextFlags, iOptionFlagsToMatch, iOptionFlagsToExclude, caller_fname, line);
	return PARSERESULT_ERROR;
}

static __forceinline TextParserResult nonarray_readstring(ParseTable tpi[], int column, void* structptr, int index, const char* str, ReadStringFlags eFlags)
{
	readstring_f readstringFunc = TYPE_INFO(tpi[column].type).readstring;
	if (readstringFunc)
		return readstringFunc(tpi, column, structptr, index, str, eFlags);
	return PARSERESULT_ERROR;
}

static __forceinline TextParserResult nonarray_tomulti(ParseTable tpi[], int column, void* structptr, int index, MultiVal* result, bool bDuplicateData, bool dummyOnNULL)
{
	tomulti_f tomultiFunc = TYPE_INFO(tpi[column].type).tomulti;
	if (tomultiFunc)
		return tomultiFunc(tpi, column, structptr, index, result, bDuplicateData, dummyOnNULL);
	return PARSERESULT_ERROR;
}

static __forceinline TextParserResult nonarray_frommulti(ParseTable tpi[], int column, void* structptr, int index, const MultiVal* value)
{
	frommulti_f frommultiFunc = TYPE_INFO(tpi[column].type).frommulti;
	if (frommultiFunc)
		return frommultiFunc(tpi, column, structptr, index, value);
	return PARSERESULT_ERROR;
}

static __forceinline void nonarray_updatecrc(ParseTable tpi[], int column, void* structptr, int index)
{
	updatecrc_f updatecrcFunc = TYPE_INFO(tpi[column].type).updatecrc;
	if (updatecrcFunc)
		updatecrcFunc(tpi, column, structptr, index);
}



static __forceinline int nonarray_compare(ParseTable tpi[], int column, const void* lhs, const void* rhs, int index, enumCompareFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	compare_f compareFunc = TYPE_INFO(tpi[column].type).compare;
	if (compareFunc)
	{
		return compareFunc(tpi, column, lhs, rhs, index,eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
	}
	return 0;
}

static __forceinline  size_t nonarray_memusage(ParseTable tpi[], int column, void* structptr, int index, bool bAbsoluteUsage)
{
	memusage_f memusageFunc = TYPE_INFO(tpi[column].type).memusage;
	if (memusageFunc && !(tpi[column].type & TOK_UNOWNED))
		return memusageFunc(tpi, column, structptr, index, bAbsoluteUsage);
	return 0;
}

static __forceinline void nonarray_calcoffset(ParseTable tpi[], int column, size_t* size)
{
	calcoffset_f calcoffsetFunc = TYPE_INFO(tpi[column].type).calcoffset;
	if (calcoffsetFunc)
	{
		calcoffsetFunc(tpi, column, size);
	}
}

static __forceinline void nonarray_copystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData)
{
	StructTypeField field_type = TOK_GET_TYPE(ptcc->type);
	switch(field_type) {
	case TOK_STRING_X: 
	case TOK_CURRENTFILE_X:
	case TOK_FILENAME_X:
		string_copystruct(tpi, ptcc, column, dest, src, index, memAllocator, customData);
		break;

	case TOK_REFERENCE_X: 
		reference_copystruct(tpi, ptcc, column, dest, src, index, memAllocator, customData);
		break;

	case TOK_STRUCT_X:    
		struct_copystruct(tpi, ptcc, column, dest, src, index, memAllocator, customData);
		break;

	default: {
		// This case works for all types using "g_tokentable"
		copystruct_f copystructFunc = TYPE_INFO(ptcc->type).copystruct_func;
		if (copystructFunc)
			copystructFunc(tpi, ptcc, column, dest, src, index, memAllocator, customData);
		}
	}
}

static __forceinline void nonarray_copyfield(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	StructTypeField field_type = TOK_GET_TYPE(ptcc->type);
	switch(field_type) {
	case TOK_U8_X:        u8_copyfield(tpi, ptcc, column, dest, src, index, memAllocator, customData, iOptionFlagsToMatch, iOptionFlagsToExclude); break;
	case TOK_INT16_X:     int16_copyfield(tpi, ptcc, column, dest, src, index, memAllocator, customData, iOptionFlagsToMatch, iOptionFlagsToExclude); break;
	case TOK_INT_X:       int_copyfield(tpi, ptcc, column, dest, src, index, memAllocator, customData, iOptionFlagsToMatch, iOptionFlagsToExclude); break;
	case TOK_INT64_X:     int64_copyfield(tpi, ptcc, column, dest, src, index, memAllocator, customData, iOptionFlagsToMatch, iOptionFlagsToExclude); break;
	case TOK_F32_X:       float_copyfield(tpi, ptcc, column, dest, src, index, memAllocator, customData, iOptionFlagsToMatch, iOptionFlagsToExclude); break;
	case TOK_STRING_X:    string_copyfield(tpi, ptcc, column, dest, src, index, memAllocator, customData, iOptionFlagsToMatch, iOptionFlagsToExclude); break;
	case TOK_REFERENCE_X: reference_copyfield(tpi, ptcc, column, dest, src, index, memAllocator, customData, iOptionFlagsToMatch, iOptionFlagsToExclude); break;
	case TOK_STRUCT_X:    struct_copyfield(tpi, ptcc, column, dest, src, index, memAllocator, customData, iOptionFlagsToMatch, iOptionFlagsToExclude); break;
	default: {
		// This case works for all types using "g_tokentable"
		copyfield_f copyfieldFunc = TYPE_INFO(ptcc->type).copyfield_func;
		if (copyfieldFunc)
			copyfieldFunc(tpi, ptcc, column, dest, src, index, memAllocator, customData, iOptionFlagsToMatch, iOptionFlagsToExclude);
		}
	}
}

static __forceinline enumCopy2TpiResult nonarray_copyfield2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest, 
	CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, char **ppResultString)
{
	copyfield2tpis_f copyfield2tpisFunc = TYPE_INFO(src_tpi[src_column].type).copyfield2tpis;
	if (copyfield2tpisFunc)
		return copyfield2tpisFunc(src_tpi, src_column, src_index, src, dest_tpi, dest_column, dest_index, dest, memAllocator, customData, iOptionFlagsToMatch, iOptionFlagsToExclude, ppResultString);
	return COPY2TPIRESULT_SUCCESS;
}

static __forceinline enumCopy2TpiResult nonarray_stringifycopy(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest, char **ppResultString)
{
	TextParserResult eTPResult;
	writestring_f pWriteF = TYPE_INFO(src_tpi[src_column].type).writestring;
	readstring_f pReadF = TYPE_INFO(dest_tpi[dest_column].type).readstring;
	char *pTempString = NULL;

	estrStackCreate(&pTempString);

	eTPResult = pWriteF(src_tpi, src_column, src, src_index, &pTempString, 0, 0, 0, __FUNCTION__, __LINE__);

	switch (eTPResult)
	{
	case PARSERESULT_INVALID:
		if (ppResultString)
		{
			estrPrintf(ppResultString, "got PARSERRESULT_INVALID while stringifying field %s\n",
				src_tpi[src_column].name);
			estrDestroy(&pTempString);
			return COPY2TPIRESULT_FAILED_FIELDS;
		}
	case PARSERESULT_ERROR:
		if (ppResultString)
		{
			estrPrintf(ppResultString, "got PARSERRESULT_ERROR while stringifying field %s\n",
				src_tpi[src_column].name);
			estrDestroy(&pTempString);
			return COPY2TPIRESULT_FAILED_FIELDS;
		}
	}

	eTPResult = pReadF(dest_tpi, dest_column, dest, dest_index, pTempString, 0);

	switch (eTPResult)
	{
	case PARSERESULT_INVALID:
		if (ppResultString)
		{
			estrPrintf(ppResultString, "got PARSERRESULT_INVALID while reading stringified field %s from string (%s)\n",
				dest_tpi[dest_column].name, pTempString);
			estrDestroy(&pTempString);
			return COPY2TPIRESULT_FAILED_FIELDS;
		}
	case PARSERESULT_ERROR:
		if (ppResultString)
		{
			estrPrintf(ppResultString, "got PARSERRESULT_ERROR while reading stringified field %s from string (%s)\n",
				dest_tpi[dest_column].name, pTempString);
			estrDestroy(&pTempString);
			return COPY2TPIRESULT_FAILED_FIELDS;
		}
	}


	estrDestroy(&pTempString);
	

	return COPY2TPIRESULT_SUCCESS;
}
static __forceinline void nonarray_endianswap(ParseTable tpi[], int column, void* structptr, int index)
{
	endianswap_f endianswapFunc = TYPE_INFO(tpi[column].type).endianswap;
	if (endianswapFunc)
		endianswapFunc(tpi, column, structptr, index);
}

static __forceinline void nonarray_interp(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 interpParam)
{
	interp_f interpFunc = TYPE_INFO(tpi[column].type).interp;
	if (interpFunc)
		interpFunc(tpi, column, structA, structB, destStruct, index, interpParam);
}

static __forceinline void nonarray_calcrate(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 deltaTime)
{
	calcrate_f calcrateFunc = TYPE_INFO(tpi[column].type).calcrate;
	if (calcrateFunc)
		calcrateFunc(tpi, column, structA, structB, destStruct, index, deltaTime);
}

static __forceinline void nonarray_integrate(ParseTable tpi[], int column, void* valueStruct, void* rateStruct, void* destStruct, int index, F32 deltaTime)
{
	integrate_f integrateFunc = TYPE_INFO(tpi[column].type).integrate;
	if (integrateFunc)
		integrateFunc(tpi, column, valueStruct, rateStruct, destStruct, index, deltaTime);
}

static __forceinline void nonarray_calccyclic(ParseTable tpi[], int column, void* valueStruct, void* ampStruct, void* freqStruct, void* cycleStruct, void* destStruct, int index, F32 fStartTime, F32 deltaTime)
{
	calccyclic_f calccyclicFunc = TYPE_INFO(tpi[column].type).calccyclic;
	if (calccyclicFunc)
		calccyclicFunc(tpi, column, valueStruct, ampStruct, freqStruct, cycleStruct, destStruct, index, fStartTime, deltaTime);
}

static __forceinline void nonarray_applydynop(ParseTable tpi[], int column, void* dstStruct, void* srcStruct, int index, DynOpType optype, const F32* values, U8 uiValuesSpecd, U32* seed)
{
	applydynop_f applydynopFunc = TYPE_INFO(tpi[column].type).applydynop;
	if (applydynopFunc)
		applydynopFunc(tpi, column, dstStruct, srcStruct, index, optype, values, uiValuesSpecd, seed);
}
static __forceinline void nonarray_preparesharedmemoryforfixup(ParseTable tpi[], int column, void* structptr, int index, char **ppFixupData)
{
	preparesharedmemoryforfixup_f preparesharedmemoryforfixupFunc = TYPE_INFO(tpi[column].type).preparesharedmemoryforfixup;
	if (preparesharedmemoryforfixupFunc)
		preparesharedmemoryforfixupFunc(tpi, column, structptr, index, ppFixupData);
}
static __forceinline void nonarray_fixupsharedmemory(ParseTable tpi[], int column, void* structptr, int index, char **ppFixupData)
{
	fixupsharedmemory_f fixupsharedmemoryFunc = TYPE_INFO(tpi[column].type).fixupsharedmemory;
	if (fixupsharedmemoryFunc)
		fixupsharedmemoryFunc(tpi, column, structptr, index, ppFixupData);
}

static __forceinline TextParserResult nonarray_leafFirstFixup(ParseTable tpi[], int column, void* structptr, int index, enumTextParserFixupType eFixupType, void *pExtraData)
{
	leafFirstFixup_f leafFirstFixupFunc = TYPE_INFO(tpi[column].type).leafFirstFixup;
	if (leafFirstFixupFunc)
		return leafFirstFixupFunc(tpi, column, structptr, index, eFixupType, pExtraData);

	return PARSERESULT_SUCCESS;
}

static __forceinline void nonarray_reapplypreparse(ParseTable tpi[], int column, void* structptr, int index, char *pCurrentFile, int iTimeStamp, int iLineNum)
{
	reapplypreparse_f reapplypreparseFunc = TYPE_INFO(tpi[column].type).reapplypreparse;
	if (reapplypreparseFunc)
		reapplypreparseFunc(tpi, column, structptr, index, pCurrentFile, iTimeStamp, iLineNum);
}

static __forceinline int nonarray_checksharedmemory(ParseTable tpi[], int column, void* structptr, int index)
{
	checksharedmemory_f checksharedmemoryFunc = TYPE_INFO(tpi[column].type).checksharedmemory;
	if (checksharedmemoryFunc)
	{
		if (!checksharedmemoryFunc(tpi, column, structptr, index))
		{
			return 0;
		}
	}

	return 1;
}

static __forceinline int nonarray_recvdiff2tpis(Packet *pak, ParseTable src_tpi[], ParseTable dest_tpi[], int src_column, int index, Recv2TpiCachedInfo *pCache, void *data)
{
	int iDestColumn = pCache->pColumns[src_column].iTargetColumn;
	char *pString;
	switch (pCache->pColumns[src_column].eType)
	{
	case RECV2TPITYPE_NORMALRECV:
		return TYPE_INFO(dest_tpi[iDestColumn].type).recvdiff(pak, dest_tpi, iDestColumn, data, index, RECVDIFF_FLAG_ABS_VALUES);

	case RECV2TPITYPE_STRINGRECV: 							
		pString = pktGetStringTemp(pak);
		return TYPE_INFO(dest_tpi[iDestColumn].type).readstring(dest_tpi, iDestColumn, data, index, pString, READSTRINGFLAG_READINGEMPTYSTRINGALWAYSSUCCEEDS);

	case RECV2TPITYPE_U8TOBIT:
		{
			int value = pktGetBitsPack(pak, TOK_GET_PRECISION_DEF(src_tpi[src_column].type, 1));
			if (data) TokenStoreSetBit_inline(dest_tpi, &dest_tpi[iDestColumn], iDestColumn, data, index, value, NULL, NULL);
		}
		return 1;



	case RECV2TPITYPE_STRUCT:	
		{
			void* substruct = data? TokenStoreGetPointer_inline(dest_tpi, &dest_tpi[iDestColumn], iDestColumn, data, index, NULL): 0;
			int gettingstruct = pktGetBits(pak, 1);
			if (!gettingstruct)
			{
				if (data) struct_destroystruct(dest_tpi, &dest_tpi[iDestColumn], iDestColumn, data, index);
				return 1;
			}

			// now we're getting a struct
			if (!substruct && data) 
			{
				substruct = TokenStoreAlloc(dest_tpi, iDestColumn, data, index, dest_tpi[iDestColumn].param, NULL, NULL, NULL);
				StructInitVoid(dest_tpi[iDestColumn].subtable, substruct);
			}

			return ParserRecv2tpis(pak, src_tpi[src_column].subtable, dest_tpi[iDestColumn].subtable, substruct);
		}

	}

	return 1;

}


// ------------------------------------------------------------------------------------------------------

static __forceinline bool nonarray_writehtmlfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteHTMLContext *pContext)
{
	writeHTMLHandleMaxDepthAndCollapsed(0, tpi, column, structptr, index, out, pContext);

	if (TYPE_INFO(tpi[column].type).writehtmlfile)
		return TYPE_INFO(tpi[column].type).writehtmlfile(tpi, column, structptr, index, out, pContext);
	else
	if (TYPE_INFO(tpi[column].type).writetext)
	{
		int iOffset = ftell(out);
		TYPE_INFO(tpi[column].type).writetext(out, tpi, column, structptr, index, 0, 0, 0, 0, 0, 0);
		return (iOffset != ftell(out));
	}
	
	return false;
}

// Non-struct, Non-array
static __forceinline bool nonarray_writexmlfile(ParseTable tpi[], int column, void* structptr, int index, int level, FILE* out, StructFormatField iOptions)
{
	//use the xml handler if it exists
	if (TYPE_INFO(tpi[column].type).writexmlfile)
		return TYPE_INFO(tpi[column].type).writexmlfile(tpi, column, structptr, index, level, out, iOptions);
	//otherwise default to string interpretation
	else if (TYPE_INFO(tpi[column].type).writetext)
	{
		int iOffset = ftell(out);
		//This commented line would just print unhanlded types. Probably not a good idea.
		//TYPE_INFO(tpi[column].type).writetext(out, tpi, column, structptr, index, 0, 0, level, 0, 0, 0);
		return (iOffset != ftell(out));
	}
	
	return false;
}

// Non-struct, Non-array
static __forceinline bool nonarray_writehdf(ParseTable tpi[], int column, void* structptr, int index, HDF *hdf, char *name_override)
{
	//use the hdf handler if it exists
	if (TYPE_INFO(tpi[column].type).writehdf)
		return TYPE_INFO(tpi[column].type).writehdf(tpi, column, structptr, index, hdf, name_override);
	//otherwise default to string interpretation
	else if (TYPE_INFO(tpi[column].type).writetext)
	{
		//int iOffset = ftell(out);
		//This commented line would just print unhanlded types. Probably not a good idea.
		//TYPE_INFO(tpi[column].type).writetext(out, tpi, column, structptr, index, 0, 0, level, 0, 0, 0);
		//return (iOffset != ftell(out));
	}

	return false;
}

// Non-struct, Non-array
static __forceinline bool nonarray_writejsonfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	if (TYPE_INFO(tpi[column].type).writejsonfile)
		return TYPE_INFO(tpi[column].type).writejsonfile(tpi, column, structptr, index, out, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
	else
	if (TYPE_INFO(tpi[column].type).writetext)
	{
		int iOffset = ftell(out);
		TYPE_INFO(tpi[column].type).writetext(out, tpi, column, structptr, index, 0, 0, 0, 0, 0, 0);
		return (iOffset != ftell(out));
	}
	
	return false;
}



static __forceinline bool fixedarray_initstruct(ParseTable tpi[], int column, void* structptr, int index_ignored)
{
	// nothing to initialize for fixed arrays
	return false;
}

static __forceinline bool fixedarray_destroystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index_ignored)
{
	bool iRetVal = false;
	int i, numelems = ptcc->param;
	
	if (!structptr)
	{
		if (numelems == 0)
		{
			return DESTROY_NEVER;
		}

		//tricky case... we can't check whether a fixed array is NULL the same way we can
		//for anything else, so if we would ever have to destroy it, we always have to. Fortunately,
		//fixed arrays are fairly rare in this context
		return nonarray_destroystruct(tpi, ptcc, column, NULL, 0) == DESTROY_NEVER ? DESTROY_NEVER : DESTROY_ALWAYS;
	}

	for (i = numelems-1; i >= 0; i--)
	{
		iRetVal |= nonarray_destroystruct(tpi, ptcc, column, structptr, i);
	}

	return iRetVal;
}

static __forceinline int fixedarray_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index_ignored, TextParserResult *parseResult)
{
	int numelems = ptcc->param;
	int i;
	int done = 0;
	int type = TOK_GET_TYPE(ptcc->type);

	// these types get parsed differently
	if (type == TOK_QUATPYR_X ||	// parsed as pyr
		type == TOK_MATPYR_X)		// parsed as pyr
	{
		return nonarray_parse(tok, tpi, ptcc, column, structptr, 0, parseResult);
	}

	for (i = 0; i < numelems; i++)
	{
		done = done || nonarray_parse(tok, tpi, ptcc, column, structptr, i, parseResult);
	}

	if (numelems==3 && type==TOK_F32_X && TOK_GET_FORMAT_OPTIONS(ptcc->format)==TOK_FORMAT_HSV)
	{
		// validate HSV numbers
		F32 *v = TokenStoreGetPointer_inline(tpi, ptcc, column, structptr, 0, parseResult);
		if (v[0] < 0.f || v[0] > 360.f)
		{
			TokenizerErrorf(tok,"HSV hue of %f out of range (legal range 0 to 360)",v[0]);
			SET_ERROR_RESULT(*parseResult);
			while (v[0] < 0.f)
				v[0] += 360.f;
			while (v[0] >= 360.f)
				v[0] -= 360.f;
		}
		if (v[1] < 0.f || v[1] > 1.f)
		{
			TokenizerErrorf(tok,"HSV saturation of %f out of range (legal range 0 to 1)",v[1]);
			SET_ERROR_RESULT(*parseResult);
			v[1] = CLAMP(v[1], 0, 1);
		}
	}
	return done;
}

static __forceinline bool fixedarray_writetext(FILE* out, ParseTable tpi[], int column, void* structptr, int index_ignored, bool showname, bool ignoreInherited, int level, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int i, default_value = 0, numelems = tpi[column].param;
	int type = TOK_GET_TYPE(tpi[column].type);



	// these types get parsed differently
	if (type == TOK_QUATPYR_X || // written as pyr
		type == TOK_MATPYR_X)	// written as pyr
	{
		return nonarray_writetext(out, tpi, column, structptr, 0, showname, ignoreInherited, level, iWriteTextFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
	}

	if (!showname || (tpi[column].type & TOK_REQUIRED) || SHOULD_WRITE_DEFAULT(iWriteTextFlags, tpi, column, structptr))
	{
		//we don't care if the thing is all defaults... we will write it out no matter what
	}
	else
	{
		//check if our array is all zero (remember that fixed size arrays don't have default values, so the 
		//default value must be zero)
		bool bAllDefaults = true;


		if (type == TOK_F32_X)
		{
			for (i = 0; i < numelems; i++)
			{
				F32 value = TokenStoreGetF32_inline(tpi, &tpi[column], column, structptr, i, NULL);
				if (value != 0)
				{
					bAllDefaults = false;
					break;
				}
			}
		}
		else if (type == TOK_U8_X)
		{
			for (i = 0; i < numelems; i++)
			{
				U8 value = TokenStoreGetU8_inline(tpi, &tpi[column], column, structptr, i, NULL);
				if (value != 0)
				{
					bAllDefaults = false;
					break;
				}			
			}
		}
		else if (type == TOK_INT16_X)
		{
			for (i = 0; i < numelems; i++)
			{
				S16 value = TokenStoreGetInt16_inline(tpi, &tpi[column], column, structptr, i, NULL);
				if (value != 0)
				{
					bAllDefaults = false;
					break;
				}			
			}
		}
		else if (type == TOK_INT_X)
		{
			for (i = 0; i < numelems; i++)
			{
				int value = TokenStoreGetInt_inline(tpi, &tpi[column], column, structptr, i, NULL);
				if (value != 0)
				{
					bAllDefaults = false;
					break;
				}			
			}
		}
		else if (type == TOK_INT64_X)
		{
			for (i = 0; i < numelems; i++)
			{
				U64 value = TokenStoreGetInt64_inline(tpi, &tpi[column], column, structptr, i, NULL);
				if (value != 0)
				{
					bAllDefaults = false;
					break;
				}			
			}
		}

		if (bAllDefaults)
		{
			return false;
		}
	}


	if (showname) WriteNString(out, tpi[column].name, ParseTableColumnNameLen(tpi,column), level+1, 0);
	for (i = 0; i < numelems; i++)
	{
		if (i > 0)
			WriteNString(out, ", ", 2, 0, 0);
		else
			WriteNString(out, " ", 1, 0, 0);
		nonarray_writetext(out, tpi, column, structptr, i, 0, ignoreInherited, 0, iWriteTextFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
	}
	if (showname) WriteNString(out, NULL, 0, 0, 1);

	return true;
}

static __forceinline TextParserResult fixedarray_writebin(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index_ignored, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int i, numelems = tpi[column].param;
	int type = TOK_GET_TYPE(tpi[column].type);

	if (type == TOK_MATPYR_X)	// persisted as pyr
		return nonarray_writebin(file, pLayoutFile, pFileList, tpi, column, structptr, 0, datasum, iOptionFlagsToMatch, iOptionFlagsToExclude);

	for (i = 0; i < numelems; i++)
		SET_MIN_RESULT(ok, nonarray_writebin(file, pLayoutFile, pFileList, tpi, column, structptr, i, datasum, iOptionFlagsToMatch, iOptionFlagsToExclude));
	return ok;
}

static __forceinline TextParserResult fixedarray_readbin(SimpleBufHandle file, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index_ignored, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int i, numelems = tpi[column].param;
	int type = TOK_GET_TYPE(tpi[column].type);

	if (type == TOK_MATPYR_X)	// persisted as pyr
		return nonarray_readbin(file, pFileList, tpi, column, structptr, 0, datasum, iOptionFlagsToMatch, iOptionFlagsToExclude);

	for (i = 0; i < numelems; i++)
		SET_MIN_RESULT(ok,nonarray_readbin(file, pFileList, tpi, column, structptr, i, datasum, iOptionFlagsToMatch, iOptionFlagsToExclude));
	return ok;
}

static __forceinline void fixedarray_writehdiff(char** estr, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index_ignored, char *prefix, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	char *newPath = NULL;
	int i, numelems = tpi[column].param;
	int type = TOK_GET_TYPE(tpi[column].type);

	for (i = 0; i < numelems; i++)
	{
		if ((TYPE_INFO(tpi[column].type).compare(tpi, column, oldstruct, newstruct, i,COMPAREFLAG_NULLISDEFAULT | ((eFlags & TEXTIDFFFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT) ? COMPAREFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT : 0), iOptionFlagsToMatch, iOptionFlagsToExclude)))
		{		
			if (!newPath)
			{
				estrStackCreateSize(&newPath, MIN_STACK_ESTR);
			}
			estrPrintf(&newPath, "%s[%d]", prefix, i);
			nonarray_writehdiff(estr, tpi, column, oldstruct, newstruct, i, newPath, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, caller_fname, line);
		}
	}
	estrDestroy(&newPath);
}

static __forceinline void fixedarray_writebdiff(StructDiff *diff, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, ObjectPath *parentPath, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool invertExcludeFlags, const char *caller_fname, int line)
{
	int i, numelems = tpi[column].param;
	int type = TOK_GET_TYPE(tpi[column].type);
	if (type == TOK_MATPYR_X) // send as pyr
	{
		nonarray_writebdiff(diff, tpi, column, oldstruct, newstruct, 0, NULL/*objectpath*/, iOptionFlagsToMatch, iOptionFlagsToExclude, invertExcludeFlags, caller_fname, line);
		return;
	}
	else
	{
		char *newPath = NULL;
		int keyoffset = 0;
		StructDiffOp *op = eaPop(&diff->ppOps);
		if (op) StructDestroyDiffOp(&op);
	
		for (i = 0; i < numelems; i++)
		{
			if ((TYPE_INFO(tpi[column].type).compare(tpi, column, oldstruct, newstruct, i,COMPAREFLAG_NULLISDEFAULT | (invertExcludeFlags ? COMPAREFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT : 0), iOptionFlagsToMatch, iOptionFlagsToExclude)))
			{		
				ObjectPath *path = ObjectPathCopyAndAppendIndex(parentPath, i, NULL);
				if (!path) break; //XXXXXX: This should report an error somehow.

				op = StructMakeAndAppendDiffOp(diff, path, newstruct, STRUCTDIFF_SET);
				if (!StructDiffIsValid(diff)) break;

				nonarray_writebdiff(diff, tpi, column, oldstruct, newstruct, i, NULL/*objectpath*/, iOptionFlagsToMatch, iOptionFlagsToExclude, invertExcludeFlags, caller_fname, line);
				
				if (!StructDiffIsValid(diff)) break;
			}
		}
		if (newPath) estrDestroy(&newPath);
	}
}

static __forceinline bool fixedarray_senddiff(Packet* pak, ParseTable tpi[], int column, const void* oldstruct, const void* newstruct, int index_ignored, enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB)
{
	int i, numelems = tpi[column].param;
	int type = TOK_GET_TYPE(tpi[column].type);

	bool bSentSomething = false;

	if (type == TOK_MATPYR_X) // send as pyr
	{
		return nonarray_senddiff(pak, tpi, column, oldstruct, newstruct, 0, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude, pGenerateFlagsCB);
	}

	//in "fast compares" mode we need to sent indices along with each element
	if (eFlags & SENDDIFF_FLAG_COMPAREBEFORESENDING)
	{
		for (i=0; i < numelems; i++)
		{
			int iOffsetBefore = pktGetWriteIndex(pak);

			pktSendBitsAuto(pak, i);

			if (nonarray_senddiff(pak, tpi, column, oldstruct, newstruct, i, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude, pGenerateFlagsCB))
			{
				bSentSomething = true;
			}
			else
			{
				pktSetWriteIndex(pak, iOffsetBefore);
			}
		}

		pktSendBitsAuto(pak, -1);

		return bSentSomething;
	}
	else
	{
		for (i = 0; i < numelems; i++)
		{
			bSentSomething |= nonarray_senddiff(pak, tpi, column, oldstruct, newstruct, i, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude, pGenerateFlagsCB);
		}

		return bSentSomething;
	}
}

static __forceinline bool fixedarray_recvdiff(Packet* pak, ParseTable tpi[], int column, void* structptr, int index_ignored, enumRecvDiffFlags eFlags)
{
	int i, numelems = tpi[column].param;
	int type = TOK_GET_TYPE(tpi[column].type);

	if (type == TOK_MATPYR_X) // send as pyr
	{
		return nonarray_recvdiff(pak, tpi, column, structptr, 0, eFlags);
	}


	if (eFlags & RECVDIFF_FLAG_COMPAREBEFORESENDING)
	{
		int iIndex;

		while ((iIndex = pktGetBitsAuto(pak)) != -1)
		{
			if (iIndex < -1 || iIndex >= numelems)
			{
				RECV_FAIL("Bad index for fixed array");
			}
			
			if (!nonarray_recvdiff(pak, tpi, column, structptr, iIndex, eFlags))
			{
				return 0;
			}

			RECV_CHECK_PAK(pak);
		}
	}
	else
	{
		for (i = 0; i < numelems; i++)
		{
			if (!nonarray_recvdiff(pak, tpi, column, structptr, i, eFlags))
			{
				return 0;
			}

			RECV_CHECK_PAK(pak);
		}
	}

	return 1;
}

static __forceinline bool fixedarray_bitpack(ParseTable tpi[], int column, const void *structptr, int index_ignored, PackedStructStream *pack)
{
	int numelems = tpi[column].param;
	int type = TOK_GET_TYPE(tpi[column].type);
	if (type == TOK_MATPYR_X) // send as pyr
	{
		return nonarray_bitpack(tpi, column, structptr, 0, pack);
	}
	return StructBitpackArraySub(tpi, column, structptr, pack, numelems);
}

static __forceinline void fixedarray_unbitpack(ParseTable tpi[], int column, void *structptr, int index_ignored, PackedStructStream *pack)
{
	int numelems = tpi[column].param;
	int type = TOK_GET_TYPE(tpi[column].type);
	if (type == TOK_MATPYR_X) // sent as pyr
	{
		nonarray_unbitpack(tpi, column, structptr, 0, pack);
		return;
	}
	StructUnbitpackArraySub(tpi, column, structptr, pack, numelems);
}

static __forceinline TextParserResult fixedarray_writestring(ParseTable tpi[], int column, void* structptr, int index_ignored, char** estr, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *caller_fname, int line)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int i, numelems = tpi[column].param;
	for (i = 0; i < numelems && RESULT_GOOD(ok); i++)
	{
		if (i > 0)
			estrAppend2_dbg_inline(estr, ", ", caller_fname, line);
		SET_MIN_RESULT(ok, nonarray_writestring(tpi, column, structptr, i, estr, iWriteTextFlags, iOptionFlagsToMatch, iOptionFlagsToExclude, caller_fname, line));			
	}
	return ok;
}

static __forceinline TextParserResult fixedarray_readstring(ParseTable tpi[], int column, void* structptr, int index_ignored, const char* str, ReadStringFlags eFlags)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int i, numelems = tpi[column].param;
	char *param = ScratchAlloc(strlen(str)+1);
	char *next;
	char *strtokcontext = 0;

	assert(param);
	strcpy_s(param, strlen(str)+1, str);
	next = strtok_s(param, ",", &strtokcontext);
	for (i = 0; i < numelems && RESULT_GOOD(ok); i++)
	{
		while (next && next[0] == ' ') next++;
		if (!next || !next[0])
		{
			SET_ERROR_RESULT(ok);
			break;			
		}
		SET_MIN_RESULT(ok, nonarray_readstring(tpi, column, structptr, i, next, eFlags));
		next = strtok_s(NULL, ",", &strtokcontext);
	}
	ScratchFree(param);
	return ok;
}

// basically the strategy here is to lean on the comma-separated format of to/from simple
static __forceinline TextParserResult fixedarray_tomulti(ParseTable tpi[], int column, void* structptr, int index_ignored, MultiVal* result, bool bDuplicateData, bool dummyOnNULL)
{	
	// The code that was here before (convert to string, set as string) makes no sense and is not called
	return PARSERESULT_ERROR;
}

static __forceinline TextParserResult fixedarray_frommulti(ParseTable tpi[], int column, void* structptr, int index_ignored, const MultiVal* value)
{
	// The code that was here before (convert to string, set as string) makes no sense and is not called
	return PARSERESULT_ERROR;
}

static __forceinline void fixedarray_updatecrc(ParseTable tpi[], int column, void* structptr, int index_ignored)
{
	int i, numelems = tpi[column].param;
	int type = TOK_GET_TYPE(tpi[column].type);
	if (type == TOK_MATPYR_X) // send as pyr
	{
		nonarray_updatecrc(tpi, column, structptr, 0);
		return;
	}

	IF_DEBUG_CRCS( if (numelems) printf("%sAbout to CRC each of the %d elements of the fixed array\n", pDebugCRCPrefix, numelems); estrConcatf(&pDebugCRCPrefix, "  ");)

	for (i = 0; i < numelems; i++)
	{
		nonarray_updatecrc(tpi, column, structptr, i);
		IF_DEBUG_CRCS ( printf("%sAfter fixed array element %d, CRC is %u\n", pDebugCRCPrefix, i, cryptAdlerGetCurValue()); )
	}

	IF_DEBUG_CRCS ( estrSetSize(&pDebugCRCPrefix, estrLength(&pDebugCRCPrefix) - 2);)
}

static __forceinline int fixedarray_compare(ParseTable tpi[], int column, const void* lhs, const void* rhs, int index, enumCompareFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int i, numelems = tpi[column].param;
	int type = TOK_GET_TYPE(tpi[column].type);
	int ret = 0;
	compare_f compareFunc;

	if (type == TOK_MATPYR_X) // send as pyr
	{
		return nonarray_compare(tpi, column, lhs, rhs, 0, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
	}
	
	compareFunc = TYPE_INFO(tpi[column].type).compare;
	if (!compareFunc)
		return 0;
	for (i = 0; i < numelems && ret == 0; i++)
	{
		ret = compareFunc(tpi, column, lhs, rhs, i,eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
	}
	return ret;
}

static __forceinline size_t fixedarray_memusage(ParseTable tpi[], int column, void* structptr, int index_ignored, bool bAbsoluteUsage)
{
	size_t ret = 0;
	int i, numelems = tpi[column].param;
	if (!(tpi[column].type & TOK_UNOWNED))
	{
		for (i = 0; i < numelems; i++)
		{
			ret += nonarray_memusage(tpi, column, structptr, i, bAbsoluteUsage);
		}
	}
	return ret;
}

static __forceinline void fixedarray_calcoffset(ParseTable tpi[], int column, size_t* size)
{
	int i, numelems = tpi[column].param;
	size_t firstelem;

	// first element has the correct offset (this code allows for mem alignment)
	nonarray_calcoffset(tpi, column, size);
	firstelem = tpi[column].storeoffset;

	for (i = 1; i < numelems; i++)
		nonarray_calcoffset(tpi, column, size);

	tpi[column].storeoffset = firstelem;
}

static __forceinline void fixedarray_copystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index_ignored, CustomMemoryAllocator memAllocator, void* customData)
{
	int i, numelems = ptcc->param;
	for (i = 0; i < numelems; i++)
		nonarray_copystruct(tpi, ptcc, column, dest, src, i, memAllocator, customData);
}

static __forceinline void fixedarray_copyfield(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index_ignored, CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int i, numelems = ptcc->param;
	for (i = 0; i < numelems; i++)
		nonarray_copyfield(tpi, ptcc, column, dest, src, i, memAllocator, customData, iOptionFlagsToMatch, iOptionFlagsToExclude);
}

static __forceinline enumCopy2TpiResult fixedarray_copyfield2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest, 
	CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, char **ppResultString)
{
	int i, numelems = MIN(src_tpi[src_column].param, dest_tpi[dest_column].param);
	enumCopy2TpiResult eResult = COPY2TPIRESULT_SUCCESS;
	for (i=0; i < numelems; i++)
	{
		enumCopy2TpiResult eRecurseResult = nonarray_copyfield2tpis(src_tpi, src_column, i, src, dest_tpi, dest_column, i, dest, memAllocator, customData, iOptionFlagsToMatch, iOptionFlagsToExclude, ppResultString );
		eResult = MAX(eResult, eRecurseResult);
	}

	return eResult;
}

static __forceinline enumCopy2TpiResult fixedarray_stringifycopy(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest, char **ppResultString)
{
	int i, numelems = MIN(src_tpi[src_column].param, dest_tpi[dest_column].param);
	enumCopy2TpiResult eResult = COPY2TPIRESULT_SUCCESS;
	for (i=0; i < numelems; i++)
	{
		enumCopy2TpiResult eRecurseResult = nonarray_stringifycopy(src_tpi, src_column, i, src, dest_tpi, dest_column, i, dest, ppResultString );
		eResult = MAX(eResult, eRecurseResult);
	}

	return eResult;
}

static __forceinline void fixedarray_endianswap(ParseTable tpi[], int column, void* structptr, int index_ignored)
{
	int i, numelems = tpi[column].param;
	for (i = 0; i < numelems; i++)
		nonarray_endianswap(tpi, column, structptr, i);
}

static __forceinline void fixedarray_interp(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index_ignored, F32 interpParam)
{
	int i, numelems = tpi[column].param;

	int type = TOK_GET_TYPE(tpi[column].type);
	if (type == TOK_MATPYR_X || type == TOK_QUATPYR_X) // custom interps
	{
		nonarray_interp(tpi, column, structA, structB, destStruct, 0, interpParam);
		return;
	}

	for (i = 0; i < numelems; i++)
		nonarray_interp(tpi, column, structA, structB, destStruct, i, interpParam);
}

static __forceinline void fixedarray_calcrate(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index_ignored, F32 deltaTime)
{
	int i, numelems = tpi[column].param;

	int type = TOK_GET_TYPE(tpi[column].type);
	if (type == TOK_MATPYR_X || type == TOK_QUATPYR_X) // custom interps
	{
		nonarray_calcrate(tpi, column, structA, structB, destStruct, 0, deltaTime);
		return;
	}

	for (i = 0; i < numelems; i++)
		nonarray_calcrate(tpi, column, structA, structB, destStruct, i, deltaTime);
}

static __forceinline void fixedarray_integrate(ParseTable tpi[], int column, void* valueStruct, void* rateStruct, void* destStruct, int index_ignored, F32 deltaTime)
{
	int i, numelems = tpi[column].param;

	int type = TOK_GET_TYPE(tpi[column].type);
	if (type == TOK_MATPYR_X || type == TOK_QUATPYR_X) // custom interps
	{
		nonarray_integrate(tpi, column, valueStruct, rateStruct, destStruct, 0, deltaTime);
		return;
	}

	for (i = 0; i < numelems; i++)
		nonarray_integrate(tpi, column, valueStruct, rateStruct, destStruct, i, deltaTime);
}

static __forceinline void fixedarray_calccyclic(ParseTable tpi[], int column, void* valueStruct, void* ampStruct, void* freqStruct, void* cycleStruct, void* destStruct, int index_ignored, F32 fStartTime, F32 deltaTime)
{
	int i, numelems = tpi[column].param;
	for (i = 0; i < numelems; i++)
		nonarray_calccyclic(tpi, column, valueStruct, ampStruct, freqStruct, cycleStruct, destStruct, i, fStartTime, deltaTime);
}

static __forceinline void fixedarray_applydynop(ParseTable tpi[], int column, void* dstStruct, void* srcStruct, int index_ignored, DynOpType optype, const F32* values, U8 uiValuesSpecd, U32* seed)
{
	int i, numelems = tpi[column].param;
	int type = TOK_GET_TYPE(tpi[column].type);
	if (type == TOK_QUATPYR_X) // custom applydynop
	{
		nonarray_applydynop(tpi, column, dstStruct, srcStruct, 0, optype, values, uiValuesSpecd, seed);
		return;
	}
	else if (type == TOK_F32_X && numelems == 3)
	{
		vec3_applydynop(tpi, column, dstStruct, srcStruct, 0, optype, values, uiValuesSpecd, seed);
		return;
	}

	if (uiValuesSpecd >= numelems) // values spread to components // changed so there is only one seed
	{
		for (i = 0; i < numelems; i++)
			nonarray_applydynop(tpi, column, dstStruct, srcStruct, i, optype, values+i, uiValuesSpecd, seed);
	}
	else // same value & seed to each component
	{
		for (i = 0; i < numelems; i++)
			nonarray_applydynop(tpi, column, dstStruct, srcStruct, i, optype, values, uiValuesSpecd, seed);
	}
}

static __forceinline void fixedarray_preparesharedmemoryforfixup(ParseTable tpi[], int column, void* structptr, int index_ignored, char **ppFixupData)
{
	int i, numelems = tpi[column].param;
	for (i = 0; i < numelems; i++)
		nonarray_preparesharedmemoryforfixup(tpi, column, structptr, i, ppFixupData);
}

static __forceinline void fixedarray_fixupsharedmemory(ParseTable tpi[], int column, void* structptr, int index_ignored, char **ppFixupData)
{
	int i, numelems = tpi[column].param;
	for (i = 0; i < numelems; i++)
		nonarray_fixupsharedmemory(tpi, column, structptr, i, ppFixupData);
}

static __forceinline TextParserResult fixedarray_leafFirstFixup(ParseTable tpi[], int column, void* structptr, int index_ignored, enumTextParserFixupType eFixupType, void *pExtraData)
{
	leafFirstFixup_f leafFirstFixupFunc = TYPE_INFO(tpi[column].type).leafFirstFixup;
	int iSucceeded = 1;

	if (leafFirstFixupFunc)
	{
		int i, numelems = tpi[column].param;

		for (i = 0; i < numelems; i++)
		{
			if (!leafFirstFixupFunc(tpi, column, structptr, i, eFixupType, pExtraData))
				iSucceeded = 0;
		}
	}
	return iSucceeded;
}

static __forceinline void fixedarray_reapplypreparse(ParseTable tpi[], int column, void* structptr, int index_ignored, char *pCurrentFile, int iTimeStamp, int iLineNum)
{
	int i, numelems = tpi[column].param;
	for (i = 0; i < numelems; i++)
		nonarray_reapplypreparse(tpi, column, structptr, i, pCurrentFile, iTimeStamp, iLineNum);

}

static __forceinline int fixedarray_checksharedmemory(ParseTable tpi[], int column, void* structptr, int index_ignored)
{
	int iSucceeded = 1;
	int i, numelems = tpi[column].param;
	for (i = 0; i < numelems; i++)
		SET_MIN_RESULT(iSucceeded, nonarray_checksharedmemory(tpi, column, structptr, i));

	return iSucceeded;
}

static __forceinline int fixedarray_recvdiff2tpis(Packet *pak, ParseTable src_tpi[], ParseTable dest_tpi[], int src_column, int index, Recv2TpiCachedInfo *pCache, void *data)
{
	int i, numelems = src_tpi[src_column].param;

	if (TOK_GET_TYPE(src_tpi[src_column].type) == TOK_MATPYR_X) // send as pyr
	{
		return nonarray_recvdiff2tpis(pak, src_tpi, dest_tpi, src_column,0, pCache, data);
	}

	for (i=0; i < numelems; i++)
	{
		if (!nonarray_recvdiff2tpis(pak, src_tpi, dest_tpi, src_column, i, pCache, data))
		{
			return 0;
		}
	}
	return 1;
}

static __forceinline bool fixedarray_writehtmlfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteHTMLContext *pContext)
{
	return all_arrays_writehtmlfile(tpi, column, structptr, index, out, pContext);
}


static __forceinline bool fixedarray_writexmlfile(ParseTable tpi[], int column, void* structptr, int index, int level, FILE* out, StructFormatField iOptions)
{
	return array_writexmlfile(tpi, column, structptr, index, level, out, iOptions);
}


static __forceinline bool fixedarray_writehdf(ParseTable tpi[], int column, void* structptr, int index, HDF *hdf, char *name_override)
{
	return array_writehdf(tpi, column, structptr, index, hdf, name_override);
}

static __forceinline bool fixedarray_writejsonfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	return array_writejsonfile(tpi, column, structptr, index, out, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
}


static __forceinline bool earray_initstruct(ParseTable tpi[], int column, void* structptr, int index_ignored)
{
	int type = TOK_GET_TYPE(tpi[column].type);
	if (!gbLateCreateIndexedEArrays && !(tpi[column].type & TOK_NO_INDEXED_PREALLOC) && ParserColumnIsIndexedEArray(tpi, column, NULL))		
	{
		const char *pTPIName = ParserGetTableName(tpi);
		void ***pppEarray = TokenStoreGetEArray_inline(tpi, &tpi[column], column, structptr, NULL);
		eaIndexedEnable_dbg(pppEarray,tpi[column].subtable, pTPIName?pTPIName:__FILE__, pTPIName?LINENUM_FOR_EARRAYS:__LINE__);
		return true;
	}

	return false;
}

static __forceinline bool earray_destroystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index_ignored)
{
	if (structptr)
	{
		// Avoid work if earray is NULL
		void **ea = (void**)((intptr_t)structptr + ptcc->storeoffset);
		if (ea) 
		{
			int i, numelems = TokenStoreGetNumElems_inline(tpi, ptcc, column, structptr, NULL);
			for (i = numelems-1; i >= 0; i--)
			{
				nonarray_destroystruct(tpi, ptcc, column, structptr, i);
			}
			TokenStoreDestroyEArray(tpi, ptcc, column, structptr, NULL);
		}
	}
	return DESTROY_IF_NON_NULL;
}

static __forceinline int earray_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index_ignored, TextParserResult *parseResult)
{
	int num_elems;
	char* nexttoken;
	int type = TOK_GET_TYPE(ptcc->type);

	// these types get parsed differently
	if (type == TOK_STRUCT_X ||			// parsed one at a time
		type == TOK_POLYMORPH_X ||		// parsed one at a time
		type == TOK_FUNCTIONCALL_X)			// parsed one at a time
	{
		return nonarray_parse(tok, tpi, ptcc, column, structptr, 0, parseResult);
	}

	//for the case of parsing an earray of ints/strings/floats, we allow "5|7|1.5" or what have you, due to a
	//weird compatibility case where a flag field was turned into an earray
	num_elems = TokenStoreGetNumElems_inline(tpi, ptcc, column, structptr, parseResult); // earrays keep adding to the end of the list when parsing
	TokenizerSetExtraDelimiters(tok, "|");
	while (1)
	{
		nexttoken = TokenizerPeek(tok, PEEKFLAG_IGNORECOMMA, parseResult);
		if (!nexttoken || IsEol(nexttoken)) 
			break;
		nonarray_parse(tok, tpi, ptcc, column, structptr, num_elems++, parseResult);
	}
	TokenizerSetExtraDelimiters(tok, NULL);
	return 0;
}

static __forceinline bool earray_writetext(FILE* out, ParseTable tpi[], int column, void* structptr, int index_ignored, bool showname, bool ignoreInherited, int level, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int s, size = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structptr, NULL);
	int type = TOK_GET_TYPE(tpi[column].type);

	// these types get parsed & written individually
	if (type == TOK_STRUCT_X ||
		type == TOK_POLYMORPH_X ||
		type == TOK_FUNCTIONCALL_X)
	{
		return nonarray_writetext(out, tpi, column, structptr, 0, showname, ignoreInherited, level, iWriteTextFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
	}

	if (size)
	{
		if (type == TOK_STRING_X && showname)
		{
			// EArrays of strings should be written one per line for readability.
			for (s = 0; s < size; s++)
				nonarray_writetext(out, tpi, column, structptr, s, true, ignoreInherited, level, iWriteTextFlags | WRITETEXTFLAG_ISREQUIRED, iOptionFlagsToMatch, iOptionFlagsToExclude);
		}
		else
		{
			if (showname) WriteNString(out, tpi[column].name, ParseTableColumnNameLen(tpi,column), level+1, 0);
			for (s = 0; s < size; s++)
			{
				if (s > 0)
					WriteNString(out, ", ", 2, 0, 0);
				else
					WriteNString(out, " ", 1, 0, 0);
				nonarray_writetext(out, tpi, column, structptr, s, 0, ignoreInherited, 0, iWriteTextFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
			}
			if (showname) WriteNString(out, NULL, 0, 0, 1);
		}

		return true;
	}

	return false;
}

static __forceinline TextParserResult earray_writebin(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index_ignored, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int s, size;
	int type = TOK_GET_TYPE(tpi[column].type);

	if (type == TOK_FUNCTIONCALL_X)
		return nonarray_writebin(file, pLayoutFile, pFileList, tpi, column, structptr, 0, datasum, iOptionFlagsToMatch, iOptionFlagsToExclude);

	size = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structptr, NULL);
	if (size > MAX_EARRAY_BIN_SIZE)
	{
		int iMaxSize = MAX_EARRAY_BIN_SIZE;
		if (!GetIntFromTPIFormatString(tpi+column, "MAX_ARRAY_SIZE", &iMaxSize) || size > iMaxSize)
		{
			assertmsgf(0, "While writing %s column %d, tried to write earray with size %d, greater than allowed max of %d",
				ParserGetTableName(tpi), column, size, iMaxSize);
		}
	}

	SET_MIN_RESULT(ok, SimpleBufWriteU32(size, file));
	*datasum += sizeof(int);

	//special case for ints... just write the whole thing in one big block
	if (type == TOK_INT_X)
	{
		if (size)
		{
			int **ea32 = (int**)((intptr_t)structptr + tpi[column].storeoffset);
			SET_MIN_RESULT(ok, SimpleBufWrite((void *) *ea32, sizeof(int) * size, file));
			*datasum += sizeof(int) * size;
		}
		return ok;
	}


	for (s = 0; s < size; s++)
	{
		SET_MIN_RESULT(ok, nonarray_writebin(file, pLayoutFile, pFileList, tpi, column, structptr, s, datasum, iOptionFlagsToMatch, iOptionFlagsToExclude));
	}
	return ok;
}

static __forceinline TextParserResult earray_readbin(SimpleBufHandle file, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index_ignored, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int s, size = 0;
	int type = TOK_GET_TYPE(tpi[column].type);

	if (type == TOK_FUNCTIONCALL_X)
	{
		return nonarray_readbin(file, pFileList, tpi, column, structptr, 0, datasum, iOptionFlagsToMatch, iOptionFlagsToExclude);
	}
	
	if (!SimpleBufReadU32((U32*)&size, file))
	{
		SET_INVALID_RESULT(ok);
		return ok;
	}
	
	if (( !(tpi[column].type & TOK_NO_INDEXED_PREALLOC) || (size > 0) ) 
		&& ParserColumnIsIndexedEArray(tpi, column, NULL))
	{
		const char *pTPIName = ParserGetTableName(tpi);
		void ***pppEarray = TokenStoreGetEArray_inline(tpi, &tpi[column], column, structptr, NULL);
		eaIndexedEnable_dbg(pppEarray,tpi[column].subtable, pTPIName?pTPIName:__FILE__, pTPIName?LINENUM_FOR_EARRAYS:__LINE__);
	}

	if (size > MAX_EARRAY_BIN_SIZE)
	{
		int iMaxSize = MAX_EARRAY_BIN_SIZE;
		if (!GetIntFromTPIFormatString(tpi+column, "MAX_ARRAY_SIZE", &iMaxSize) || size > iMaxSize)
		{
			assertmsgf(0, "While reading file %s, struct %s column %d, tried to read earray with size %d, greater than allowed max of %d",
				SimpleBufGetFilename(file), ParserGetTableName(tpi), column, size, iMaxSize);
		}
	}

	if (size > 0)
	{
		TokenStoreSetEArrayCapacity(tpi, column, structptr, size);
	}
	*datasum += sizeof(int);

	if (type == TOK_INT_X)
	{
		int **ea32 = (int**)((intptr_t)structptr + tpi[column].storeoffset);
		
		//don't want to setSize to zero, as that creates an array that doesn't need to be created
		if (size != ea32Size(ea32))
		{
			const char *pTPIName = ParserGetTableName(tpi);

			ea32SetSize_dbg(ea32, size, pTPIName?pTPIName:__FILE__, pTPIName?LINENUM_FOR_EARRAYS:__LINE__);
		}
	
		if (size)
		{
			SET_MIN_RESULT(ok, SimpleBufRead((void *) *ea32, sizeof(int) * size, file));
			*datasum += sizeof(int) * size;
		}
		return ok;
	}
	
	
	for (s = 0; s < size; s++)
	{
		SET_MIN_RESULT(ok,nonarray_readbin(file, pFileList, tpi, column, structptr, s, datasum, iOptionFlagsToMatch, iOptionFlagsToExclude));
	}
	return ok;
}

static __forceinline size_t earray_memusage(ParseTable tpi[], int column, void* structptr, int index_ignored, bool bAbsoluteUsage)
{
	size_t ret = TokenStoreGetEArrayMemUsage(tpi, column, structptr, bAbsoluteUsage, NULL);
	int i, numelems = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structptr, NULL);
	if (!(tpi[column].type & TOK_UNOWNED))
	{
		for (i = 0; i < numelems; i++)
		{
			ret += nonarray_memusage(tpi, column, structptr, i, bAbsoluteUsage);
		}
	}
	return ret;
}

static __forceinline void earray_writehdiff(char** estr, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index_ignored, char *prefix, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	char *newPath = 0;
	int i, numelems = newstruct?TokenStoreGetNumElems_inline(tpi, &tpi[column], column, newstruct, NULL):0;
	int oldelems = oldstruct?TokenStoreGetNumElems_inline(tpi, &tpi[column], column, oldstruct, NULL):0;
	int type = TOK_GET_TYPE(tpi[column].type);
	int keyColumn;
	bool printCreate = true;

	if (g_writeSizeReport && newstruct)
	{
		size_t size = earray_memusage(tpi, column, newstruct, index_ignored, true);
		estrConcatf(&g_sizeReportString, "EARRAY %s SIZE %d\n", prefix, size);
	}

	if ((eFlags & TEXTIDFFFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT) && !FlagsMatchAll(tpi[column].type,iOptionFlagsToMatch | iOptionFlagsToExclude))
	{
		// We got here on invert exclude flags that didn't totally match
		printCreate = false;
	}

	if (ParserColumnIsIndexedEArray(tpi, column, &keyColumn))			
	{
		ParseTable *subtable = tpi[column].subtable;
		ParserCompareFieldFunction cmp;	
		int oldi = 0, newi = 0;
		int oldmax = oldstruct?TokenStoreGetNumElems_inline(tpi, &tpi[column], column, oldstruct, NULL):0;
		int newmax = numelems;
		ParserSortData sortData;
		void *oldp, *newp;
		int iFixedUpPrefixLen = 0;
		sortData.tpi = subtable;
		sortData.column = keyColumn;

		assert(cmp = ParserGetCompareFunction(subtable,keyColumn));

		while (oldi < oldmax || newi < newmax)
		{

			int result;

			oldp = oldstruct?TokenStoreGetPointer_inline(tpi, &tpi[column], column, oldstruct, oldi, NULL):NULL;
			newp = newstruct?TokenStoreGetPointer_inline(tpi, &tpi[column], column, newstruct, newi, NULL):NULL;

			if (!oldp && !newp)
			{
				estrDestroy(&newPath);
				return;
			}
			else if (!oldp)
			{
				// oldp is past the end, so add the rest of newstruct
				result = 1;
			}
			else if (!newp)
			{
				// newp is past the end, so add the rest of oldstruct
				result = -1;
			}
			else
			{
				result = cmp(&sortData,&oldp,&newp);
			}

			if (!newPath)
			{
				estrStackCreateSize(&newPath, MIN_STACK_ESTR);
				estrCopy2(&newPath, prefix);
				estrConcat_dbg_inline(&newPath, "[\"", 2, caller_fname, line);
				iFixedUpPrefixLen = estrLength(&newPath);
			}
			else
			{
				estrSetSizeUnsafe_inline(&newPath, iFixedUpPrefixLen);
			}

			devassertmsgf(ParserGetKeyStringDbg_inline(subtable, result > 0 ? newp : oldp, &newPath, true, caller_fname, line), "Unable to get key string for %s", ParserGetTableName(subtable));
			estrConcat_dbg_inline(&newPath, "\"]", 2, caller_fname, line);


			if (result == 0)
			{

				StructWriteTextDiffInternal(estr,subtable,oldp,newp,newPath,iOptionFlagsToMatch,iOptionFlagsToExclude,eFlags, printCreate, false, caller_fname, line);
				oldi++;
				newi++;
			}
			else if (result < 0)
			{

				StructWriteTextDiffInternal(estr,subtable,oldp,NULL,newPath,iOptionFlagsToMatch,iOptionFlagsToExclude,eFlags, printCreate, false, caller_fname, line);
				oldi++;
			}
			else
			{

				StructWriteTextDiffInternal(estr,subtable,NULL,newp,newPath,iOptionFlagsToMatch,iOptionFlagsToExclude,eFlags, printCreate, false, caller_fname, line);
				newi++;
			}
		}
		estrDestroy(&newPath);
		return;
	}

	for (i = 0; i < numelems || i < oldelems; i++)
	{
		if (!newPath)
			estrStackCreateSize(&newPath, MIN_STACK_ESTR);

		estrPrintf(&newPath,"%s[%d]",prefix,i);

		if (i >= oldelems)
		{
			// New one
			if (!TOK_HAS_SUBTABLE(tpi[column].type) && printCreate)
			{
				// Add the create now
				estrConcatf_dbg(estr, caller_fname, line, "create %s\n",newPath);
			}
			nonarray_writehdiff(estr,tpi,column,oldstruct,newstruct,i,newPath,iOptionFlagsToMatch,iOptionFlagsToExclude,eFlags, caller_fname, line);
		}
		else if (i >= numelems)
		{
			if (printCreate)
			{				
				// Deleted
				estrConcatf_dbg(estr, caller_fname, line, "destroy %s\n",newPath);
			}
		}
		else if ((TYPE_INFO(tpi[column].type).compare(tpi, column, oldstruct, newstruct, i,COMPAREFLAG_NULLISDEFAULT | ((eFlags & TEXTIDFFFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT) ? COMPAREFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT : 0), iOptionFlagsToMatch, iOptionFlagsToExclude)))
		{		
			// In both, and different
			nonarray_writehdiff(estr,tpi,column,oldstruct,newstruct,i,newPath,iOptionFlagsToMatch,iOptionFlagsToExclude,eFlags, caller_fname, line);
		}

	}
	estrDestroy(&newPath);
}

static __forceinline void earray_writebdiff(StructDiff *diff, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, ObjectPath *parentPath, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool invertExcludeFlags, const char *caller_fname, int line)
{
	int i;
	//ParseTable *tpi = parenttpi;
	//int column;
	int oldelems, numelems;
	int type;
	int keyColumn;
	StructDiffOp *inop = eaPop(&diff->ppOps);
	if (inop) StructDestroyDiffOp(&inop);
	
	//if (!ParserResolvePathComp(parentPath, NULL, &tpi, &column, NULL, NULL, NULL, 0))
	//{
	//	Alertf("Could not resolve path while writing bdiff: %s", parentPath->key->pathString);
	//	return;
	//}
	
	numelems = newstruct?TokenStoreGetNumElems_inline(tpi, &tpi[column], column, newstruct, NULL):0;
	oldelems = oldstruct?TokenStoreGetNumElems_inline(tpi, &tpi[column], column, oldstruct, NULL):0;
	type = TOK_GET_TYPE(tpi[column].type);

	if (ParserColumnIsIndexedEArray(tpi, column, &keyColumn))
	{
		ParseTable *subtable = tpi[column].subtable;
		ParserCompareFieldFunction cmp;
		int oldi = 0, newi = 0;
		int oldmax = oldstruct?TokenStoreGetNumElems_inline(tpi, &tpi[column], column, oldstruct, NULL):0;
		int newmax = numelems;
		ParserSortData sortData;
		void *oldp, *newp;
		char *key = 0;

		sortData.tpi = subtable;
		sortData.column = keyColumn;

		assert(cmp = ParserGetCompareFunction(subtable,keyColumn));

		while (oldi < oldmax || newi < newmax)
		{
			ObjectPath *path;
			int result;
			oldp = oldstruct?TokenStoreGetPointer_inline(tpi, &tpi[column], column, oldstruct, oldi, NULL):NULL;
			newp = newstruct?TokenStoreGetPointer_inline(tpi, &tpi[column], column, newstruct, newi, NULL):NULL;

			if (!oldp && !newp)
			{
				return;
			}
			else if (!oldp)
			{
				// oldp is past the end, so add the rest of newstruct
				result = 1;
			}
			else if (!newp)
			{
				// newp is past the end, so add the rest of oldstruct
				result = -1;
			}
			else
			{
				result = cmp(&sortData,&oldp,&newp);
			}
			if (result == 0)
			{
				// Both have same key
				if (!key) 
					estrStackCreateSize(&key, MIN_STACK_ESTR);
				assert(objGetEscapedKeyEString(subtable, oldp, &key));

				path = ObjectPathCopyAndAppendIndex(parentPath, -1, key);
				ObjectPathTail(path)->descend = true;

				StructMakeDiffInternal(diff,tpi,oldstruct,newstruct,path,iOptionFlagsToMatch,iOptionFlagsToExclude,invertExcludeFlags,false, caller_fname, line);
				oldi++;
				newi++;
			}
			else if (result < 0)
			{
				if (!key) 
					estrStackCreateSize(&key, MIN_STACK_ESTR);
				assert(objGetEscapedKeyEString(subtable, oldp, &key));

				path = ObjectPathCopyAndAppendIndex(parentPath, -1, key);	
				ObjectPathTail(path)->descend = true;

				StructMakeDiffInternal(diff, tpi, oldstruct, NULL, path,iOptionFlagsToMatch,iOptionFlagsToExclude,invertExcludeFlags, false, caller_fname, line);

				oldi++;
			}
			else
			{
				if (!key) 
					estrStackCreateSize(&key, MIN_STACK_ESTR);
				assert(objGetEscapedKeyEString(subtable, newp, &key));

				path = ObjectPathCopyAndAppendIndex(parentPath, -1, key);
				ObjectPathTail(path)->descend = true;

				StructMakeDiffInternal(diff, tpi, NULL, newstruct, path, iOptionFlagsToMatch, iOptionFlagsToExclude, invertExcludeFlags, false, caller_fname, line);
				
				newi++;
			}
			if (key) estrClear(&key);
			if (!StructDiffIsValid(diff)) break;
		}
		if (key) estrDestroy(&key);
	}
	else
	{
		for (i = 0; i < numelems || i < oldelems; i++)
		{
			ObjectPath *path = ObjectPathCopyAndAppendIndex(parentPath, i, NULL);
			if (!path) break; //XXXXXX: This should report an error somehow.

			if (i >= oldelems)
			{
				StructDiffOp *op;
				// New one
				if (!TOK_HAS_SUBTABLE(tpi[column].type))
				{
					op = StructMakeAndAppendDiffOp(diff, path, newstruct, STRUCTDIFF_CREATE);
					if (!StructDiffIsValid(diff)) break;
				}
				op = StructMakeAndAppendDiffOp(diff, path, newstruct, STRUCTDIFF_SET);
				if (!StructDiffIsValid(diff)) break;

				nonarray_writebdiff(diff,tpi,column,oldstruct,newstruct,i, path,iOptionFlagsToMatch,iOptionFlagsToExclude,invertExcludeFlags, caller_fname, line);
			}
			else if (i >= numelems)
			{
				// Deleted
				StructDiffOp *op = StructMakeAndAppendDiffOp(diff, path, oldstruct, STRUCTDIFF_DESTROY);
			}
			else if ((TYPE_INFO(tpi[column].type).compare(tpi, column, oldstruct, newstruct, i,COMPAREFLAG_NULLISDEFAULT | (invertExcludeFlags ? COMPAREFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT : 0), iOptionFlagsToMatch, iOptionFlagsToExclude)))
			{		
				StructDiffOp *op = StructMakeAndAppendDiffOp(diff, path, newstruct, STRUCTDIFF_SET);
				if (!StructDiffIsValid(diff)) break;

				nonarray_writebdiff(diff,tpi,column,oldstruct,newstruct,i, path,iOptionFlagsToMatch,iOptionFlagsToExclude,invertExcludeFlags, caller_fname, line);
			}
			if (!StructDiffIsValid(diff)) break;
		}
	}
}


//NOTE NOTE NOTE if you update this list and it grows beyond a power-of-2 size, remember to change BITS_TO_SEND_FOR_SENDDIFF_TYPE
enum
{
	SENDDIFF_DIFF,
	SENDDIFF_REMOVE,
	SENDDIFF_ADD,
	SENDDIFF_DONE,
	SENDDIFF_IDENTICAL,
};

#define BITS_TO_SEND_FOR_SENDDIFF_TYPE 3

//uses keys to identify matching elements in an earray for a more efficient senddiff
static __forceinline  bool earray_senddiff_withkeys(Packet* pak, ParseTable tpi[], int column, const void* oldStruct, const void* newStruct, int index_ignored,  enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB, int iKeyColumn)
{
	int numOld = oldStruct?TokenStoreGetNumElems_inline(tpi, &tpi[column], column, oldStruct, NULL):0;
	int numNew = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, newStruct, NULL);
	ParseTable *subTable = tpi[column].subtable;

	int iOld = 0;
	int iNew = 0;

	bool bSentSomething = false;

	ParserCompareFieldFunction cmp;
	ParserSortData sortData;
	cmp = ParserGetCompareFunction(subTable,iKeyColumn);
	sortData.tpi = subTable;
	sortData.column = iKeyColumn;

	while (iOld < numOld || iNew < numNew)
	{
		void *oldSubStruct = oldStruct?TokenStoreGetPointer_inline(tpi, &tpi[column], column, oldStruct, iOld, NULL):NULL;
		void *newSubStruct = newStruct?TokenStoreGetPointer_inline(tpi, &tpi[column], column, newStruct, iNew, NULL):NULL;
	
		
		if (iNew == numNew)
		{
			//we've run out of new ones... remove the next old one
			pktSendBits(pak, BITS_TO_SEND_FOR_SENDDIFF_TYPE, SENDDIFF_REMOVE);
			iOld++;
			bSentSomething = true;
		}
		else if (iOld == numOld)
		{
			pktSendBits(pak, BITS_TO_SEND_FOR_SENDDIFF_TYPE, SENDDIFF_ADD);
			ParserSend(subTable, pak, NULL, newSubStruct, (eFlags & ~SENDDIFF_FLAG_COMPAREBEFORESENDING), iOptionFlagsToMatch, iOptionFlagsToExclude, pGenerateFlagsCB);
			iNew++;
			bSentSomething = true;
		}
		else
		{
			int iResult = cmp(&sortData,&oldSubStruct,&newSubStruct);

			if (iResult == 0)
			{
				if (eFlags & SENDDIFF_FLAG_COMPAREBEFORESENDING)
				{
					int iIndexBefore = pktGetWriteIndex(pak);

					pktSendBits(pak, BITS_TO_SEND_FOR_SENDDIFF_TYPE, SENDDIFF_DIFF);
					if (ParserSend(subTable, pak, oldSubStruct, newSubStruct, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude, pGenerateFlagsCB))
					{
						bSentSomething = true;
					}
					else
					{
						pktSetWriteIndex(pak, iIndexBefore);
						pktSendBits(pak, BITS_TO_SEND_FOR_SENDDIFF_TYPE, SENDDIFF_IDENTICAL);
					}
					iOld++;
					iNew++;			
				}
				else
				{
					pktSendBits(pak, BITS_TO_SEND_FOR_SENDDIFF_TYPE, SENDDIFF_DIFF);
					bSentSomething |= ParserSend(subTable, pak, oldSubStruct, newSubStruct, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude, pGenerateFlagsCB);
					iOld++;
					iNew++;
				}
			}
			else if (iResult < 0)
			{
				pktSendBits(pak, BITS_TO_SEND_FOR_SENDDIFF_TYPE, SENDDIFF_REMOVE);
				iOld++;
				bSentSomething = true;
			}
			else
			{
				pktSendBits(pak, BITS_TO_SEND_FOR_SENDDIFF_TYPE, SENDDIFF_ADD);
				ParserSend(subTable, pak, NULL, newSubStruct, (eFlags & ~SENDDIFF_FLAG_COMPAREBEFORESENDING), iOptionFlagsToMatch, iOptionFlagsToExclude, pGenerateFlagsCB);
				iNew++;
				bSentSomething = true;
			}
		}
	}

	pktSendBits(pak, BITS_TO_SEND_FOR_SENDDIFF_TYPE, SENDDIFF_DONE);

	return bSentSomething;
}

//sends the diff in the old fashioned slow way, for earrays not of structs, or of structs without keys
static __forceinline  bool earray_senddiff_simple(Packet* pak, ParseTable tpi[], int column, const void* oldstruct, const void* newstruct, int index_ignored,  enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB)
{
	int numold = oldstruct?TokenStoreGetNumElems_inline(tpi, &tpi[column], column, oldstruct, NULL):0;
	int numnew = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, newstruct, NULL);
	int i;

	pktSendBits(pak, 32, numnew);

	// if the size of the array has changed, refresh everything
	if (numold != numnew)
	{
		pktSendBits(pak, 1, 1); // full refresh
		for (i = 0; i < numnew; i++)
			nonarray_senddiff(pak, tpi, column, NULL, newstruct, i, SENDDIFF_FLAG_FORCEPACKALL, iOptionFlagsToMatch, iOptionFlagsToExclude, pGenerateFlagsCB);

		return true;
	}
	else
	{
		bool bSentSomething = false;
		pktSendBits(pak, 1, 0); // diffs allowed

		if (oldstruct && (eFlags & SENDDIFF_FLAG_COMPAREBEFORESENDING))
		{
			U8	*left_start,*right_start;
			int	size=0;

			simpleEarrayElementCompareParams(tpi,column,oldstruct,newstruct,&left_start,&right_start,&size);
			for (i=0; i < numnew; i++)
			{
				int iOffsetBefore;

				if (!size || memcmp(left_start + i*size, right_start + i*size, size) != 0)
				{
					iOffsetBefore = pktGetWriteIndex(pak);

					pktSendBitsAuto(pak, i);

					if (nonarray_senddiff(pak, tpi, column, oldstruct, newstruct, i, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude, pGenerateFlagsCB))
						bSentSomething = true;
					else
						pktSetWriteIndex(pak, iOffsetBefore);
				}
			}

			pktSendBitsAuto(pak, -1);

			return bSentSomething;
		}
		else
		{

			for (i = 0; i < numnew; i++)
			{
				bSentSomething |= nonarray_senddiff(pak, tpi, column, oldstruct, newstruct, i, eFlags,  iOptionFlagsToMatch, iOptionFlagsToExclude, pGenerateFlagsCB);
			}

			return bSentSomething;
		}
	}
}

static __forceinline bool earray_senddiff(Packet* pak, ParseTable tpi[], int column, const void* oldstruct, const void* newstruct, int index_ignored,  enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB)
{
	int type = TOK_GET_TYPE(tpi[column].type);

	if (type == TOK_FUNCTIONCALL_X)
	{
		return nonarray_senddiff(pak, tpi, column, oldstruct, newstruct, 0, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude, pGenerateFlagsCB);
	}

	if (!(eFlags & SENDDIFF_FLAG_FORCEPACKALL) && oldstruct)
	{
		int keyColumn;
		if (ParserColumnIsIndexedEArray(tpi, column, &keyColumn))
		{
			pktSendBits(pak, 1, 1);
			return earray_senddiff_withkeys(pak, tpi, column, oldstruct, newstruct, 0, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude, pGenerateFlagsCB, keyColumn);
		}
	}

	pktSendBits(pak, 1, 0);
	return earray_senddiff_simple(pak, tpi, column, oldstruct, newstruct, 0, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude, pGenerateFlagsCB);
}

static __forceinline bool earray_recvdiff_simple(Packet* pak, ParseTable tpi[], int column, void* structptr, int index_ignored, enumRecvDiffFlags eFlags)
{
	int i, numnew, numold = structptr? TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structptr, NULL): 1;
	int sentabs;
	numnew = pktGetBits(pak,32);
	sentabs = pktGetBits(pak, 1);

	if (eFlags & RECVDIFF_FLAG_UNTRUSTWORTHY_SOURCE)
	{
		if (numnew < 0 )
		{
			RECV_FAIL("Array size %d received - presumed corruption", numnew);
		}

		if (numnew > MAX_UNTRUSTWORTHY_ARRAY_SIZE)
		{
			int iMaxSize;

			if (!GetIntFromTPIFormatString(tpi + column, "MAX_ARRAY_SIZE", &iMaxSize) || numnew > iMaxSize)
			{
				RECV_FAIL("Array size %d received - presumed corruption", numnew);
			}
		}

	}

	if (sentabs)
	{
		eFlags |= RECVDIFF_FLAG_ABS_VALUES;
		eFlags &= ~RECVDIFF_FLAG_COMPAREBEFORESENDING;
	}
	

	// resize structs as necessary
	while (numold > numnew && structptr)
	{
		nonarray_destroystruct(tpi, &tpi[column], column, structptr, --numold);
		TokenStoreSetEArraySize(tpi, column, structptr, numold, NULL);
	}
	// (growth will happen automatically)

	if (eFlags & RECVDIFF_FLAG_COMPAREBEFORESENDING)
	{
		int iIndex;
		int numold_modified = numold;
	
		while ((iIndex = pktGetBitsAuto(pak)) != -1)
		{
			RECV_CHECK_PAK(pak);

			if (iIndex < -1 || iIndex >= numnew)
			{
				RECV_FAIL("Illegal earray index");
			}
			
			//iIndex == numold_modified is legal because that grows the array by 1. Greater than that is illegal
			if (iIndex > numold_modified)
			{
				RECV_FAIL("Illegal array index while receiving %s column %d. Did you modify something on the client that is maintained on the server?",
					ParserGetTableName(tpi), column);
			}

			
			if (!nonarray_recvdiff(pak, tpi, column, structptr, iIndex, eFlags))
			{
				return 0;
			}

			if (iIndex == numold_modified)
			{
				numold_modified++;
			}
		}
	}
	else
	{
		//when doing a full non-empty receive, set the capacity all at once to avoid repeated resizing
		if (numnew > 0 && numold == 0)
		{
			TokenStoreSetEArrayCapacity(tpi, column, structptr, numnew);

			// Index earray if required
			if ( ParserColumnIsIndexedEArray(tpi, column, NULL) )
			{
				ParseTableInfo *info = ParserGetTableInfo(tpi);
				void ***pppEarray = TokenStoreGetEArray_inline(tpi, &tpi[column], column, structptr, NULL);
				eaIndexedEnable_dbg(pppEarray,tpi[column].subtable, SAFE_MEMBER(info, name)?SAFE_MEMBER(info, name):__FILE__, SAFE_MEMBER(info, name)?LINENUM_FOR_EARRAYS:__LINE__);
			}
		}
		else if (numnew == 0 && numold == 0)
		{
			// Index earray on empty if required
			if ( !(tpi[column].type & TOK_NO_INDEXED_PREALLOC) && ParserColumnIsIndexedEArray(tpi, column, NULL) )
			{
				ParseTableInfo *info = ParserGetTableInfo(tpi);
				void ***pppEarray = TokenStoreGetEArray_inline(tpi, &tpi[column], column, structptr, NULL);
				eaIndexedEnable_dbg(pppEarray,tpi[column].subtable, SAFE_MEMBER(info, name)?SAFE_MEMBER(info, name):__FILE__, SAFE_MEMBER(info, name)?LINENUM_FOR_EARRAYS:__LINE__);
			}
		}


		// finally recv each element
		for (i = 0; i < numnew && !pktEnd(pak); i++)
		{

			RECV_CHECK_PAK(pak);

			if (!nonarray_recvdiff(pak, tpi, column, structptr, i, eFlags))
			{
				return 0;
			}
			
			// Make sure our array is now at least the appropriate size
			if (structptr)
			{
				int numnow = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structptr, NULL);
				if(numnow<i+1)
				{
					TokenStoreSetEArraySize(tpi, column, structptr, i+1, NULL);
				}
			}
		}
	}

	return 1;

}
static __forceinline bool earray_recvdiff_withkeys(Packet* pak, ParseTable tpi[], int column, void* structptr, int index_ignored, enumRecvDiffFlags eFlags)
{
	int numold = structptr?TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structptr, NULL):0;
	int iIndex = 0;
	void ***pppEarray = TokenStoreGetEArray_inline(tpi, &tpi[column], column, structptr, NULL);
	ParseTable *pSubTable = tpi[column].subtable;
	bool bFirstLoop = true;
	U32 bitFlag;

	do
	{
		//means we don't look forever on a corrupt input
		RECV_CHECK_PAK(pak);

		bitFlag = pktGetBits(pak, BITS_TO_SEND_FOR_SENDDIFF_TYPE);

		if (bFirstLoop) 
		{
			bFirstLoop = false;

			// If was zero length and should be indexed
			// and either got something OR is always indexed
			// then force indexing
			if ((numold == 0) && 
				structptr && 
				ParserColumnIsIndexedEArray(tpi, column, NULL) &&
				((bitFlag != SENDDIFF_DONE) || !(tpi[column].type & TOK_NO_INDEXED_PREALLOC))				 
				)
			{
				ParseTableInfo *info = ParserGetTableInfo(tpi);
				eaIndexedEnable_dbg(pppEarray,tpi[column].subtable, SAFE_MEMBER(info, name)?SAFE_MEMBER(info, name):__FILE__, SAFE_MEMBER(info, name)?LINENUM_FOR_EARRAYS:__LINE__);
			}
		}

		switch (bitFlag)
		{
			xcase SENDDIFF_DIFF:{
				void *pStruct = structptr ? eaGet(pppEarray, iIndex) : NULL;
				if (structptr && !pStruct)
				{
					RECV_FAIL(	"Receiving earray element DIFF in %s column %d (%s[%d], type %s), but it doesn't already exist (cur size=%d).",
								ParserGetTableName(tpi),
								column,
								tpi[column].name,
								iIndex,
								ParserGetTableName(pSubTable),
								eaSize(pppEarray));
					return 0;
				}
				else if (!ParserRecv(pSubTable, pak, pStruct, eFlags))
				{
					RECV_FAIL("Earray corruption while receiving %s column %d. Did you modify something on the client that is maintained on the server?",
						ParserGetTableName(tpi), column);
					return 0;
				}
				iIndex++;
			}
			xcase SENDDIFF_IDENTICAL:{
				if(structptr && iIndex >= eaSize(pppEarray))
				{
					RECV_FAIL(	"Receiving earray element INDENTICAL in %s column %d (%s[%d], type %s), but it doesn't already exist (cur size=%d).",
								ParserGetTableName(tpi),
								column,
								tpi[column].name,
								iIndex,
								ParserGetTableName(pSubTable),
								eaSize(pppEarray));
					return 0;
				}
				iIndex++;
			}
			xcase SENDDIFF_REMOVE:{
				if(structptr)
				{
					void *pStruct = eaRemove(pppEarray, iIndex);
					if (pStruct)
					{
						ANALYSIS_ASSUME(pStruct != NULL);
						StructDestroyVoid(pSubTable, pStruct);
					}
					else
					{
						RECV_FAIL(	"Receiving earray element REMOVE in %s column %d (%s[%d], type %s), but it doesn't already exist (cur size=%d).",
									ParserGetTableName(tpi),
									column,
									tpi[column].name,
									iIndex,
									ParserGetTableName(pSubTable),
									eaSize(pppEarray));
						return 0;
					}
				}
			}
			xcase SENDDIFF_ADD:{
				void* pNew = NULL;
				if(structptr)
				{
					if(iIndex > eaSize(pppEarray))
					{
						RECV_FAIL(	"Receiving earray element ADD in %s column %d (%s[%d], type %s), but %d is greater than cur size (%d).",
									ParserGetTableName(tpi),
									column,
									tpi[column].name,
									iIndex,
									ParserGetTableName(pSubTable),
									iIndex,
									eaSize(pppEarray));
						return 0;
					}
					
					{
						ParseTableInfo *info = ParserGetTableInfo(tpi);
						eaInsert_dbg(pppEarray, NULL, iIndex,true, SAFE_MEMBER(info, name)?SAFE_MEMBER(info, name):__FILE__,SAFE_MEMBER(info,name)?LINENUM_FOR_EARRAYS:__LINE__);
					}

					pNew = (*pppEarray)[iIndex] = StructCreateVoid(pSubTable);
				}
				
				if (!ParserRecv(pSubTable, pak, pNew, eFlags & ~RECVDIFF_FLAG_COMPAREBEFORESENDING))
				{
					return 0;
				}
				iIndex++;
			}
			xcase SENDDIFF_DONE:{
				return 1;
			}
		}
	}
	while (1);

	return 1;
}

static __forceinline bool earray_recvdiff(Packet* pak, ParseTable tpi[], int column, void* structptr, int index_ignored, enumRecvDiffFlags eFlags)
{
	int type = TOK_GET_TYPE(tpi[column].type);
	
	if (type == TOK_FUNCTIONCALL_X)
	{
		return nonarray_recvdiff(pak, tpi, column, structptr, 0, eFlags);
	}
	
	if (pktGetBits(pak, 1))
	{
		return earray_recvdiff_withkeys(pak, tpi, column, structptr, 0, eFlags);
	}
	else
	{
		return earray_recvdiff_simple(pak, tpi, column, structptr, 0, eFlags);
	}


}


static __forceinline bool earray_bitpack(ParseTable tpi[], int column, const void *structptr, int index_ignored, PackedStructStream *pack)
{
	int type = TOK_GET_TYPE(tpi[column].type);
	int numelems = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structptr, NULL);

	if (!numelems)
		return false;

	if (type == TOK_FUNCTIONCALL_X)
	{
		return nonarray_bitpack(tpi, column, structptr, 0, pack);
	}

	bsWriteBitsPack(pack->bs, TOK_GET_PRECISION_DEF(tpi[column].type, 1), numelems);
	StructBitpackArraySub(tpi, column, structptr, pack, numelems);
	return true; // At least wrote the length, even if it's an array of empty elements
}

static __forceinline void earray_unbitpack(ParseTable tpi[], int column, void *structptr, int index_ignored, PackedStructStream *pack)
{
	int type = TOK_GET_TYPE(tpi[column].type);
	int numelems;

	if (type == TOK_FUNCTIONCALL_X)
	{
		nonarray_unbitpack(tpi, column, structptr, 0, pack);
		return;
	}

	numelems = bsReadBitsPack(pack->bs, TOK_GET_PRECISION_DEF(tpi[column].type, 1));
	assert(!TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structptr, NULL));
	// Allocate earray and sub-structures
	// Call init fields on all sub-structures
	StructUnbitpackArraySub(tpi, column, structptr, pack, numelems);
}


static __forceinline TextParserResult earray_writestring(ParseTable tpi[], int column, void* structptr, int index_ignored, char** estr, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *caller_fname, int line)
{
	TextParserResult ok = PARSERESULT_SUCCESS;
	int i, numelems = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structptr, &ok);
	for (i = 0; i < numelems && RESULT_GOOD(ok); i++)
	{
		if (i > 0)
			estrAppend2_dbg_inline(estr, ", ", caller_fname, line);
		SET_MIN_RESULT(ok, nonarray_writestring(tpi, column, structptr, i, estr, iWriteTextFlags, iOptionFlagsToMatch, iOptionFlagsToExclude, caller_fname, line));			
	}
	return ok;
}

static __forceinline TextParserResult earray_readstring(ParseTable tpi[], int column, void* structptr, int index_ignored, const char* str, ReadStringFlags eFlags)
{
	ParseTable *ptcc = &tpi[column];
	TextParserResult ok = PARSERESULT_SUCCESS;
	int numelems = TokenStoreGetNumElems_inline(tpi, ptcc, column, structptr, &ok);
	int e, i = 0;
	char* param = ScratchAlloc(strlen(str)+1);
	char *next;
	char *strtokcontext = 0;
	char *pDelims = (eFlags & READSTRINGFLAG_BARSASCOMMAS) ? ",|" : ",";

	strcpy_s(param, strlen(str)+1, str);
	next = strtok_s(param, pDelims, &strtokcontext);
	while (next && next[0] && RESULT_GOOD(ok))
	{
		while (next[0] == ' ') next++;
		if (!next[0]) break;
		SET_MIN_RESULT(ok, nonarray_readstring(tpi, column, structptr, i, next, eFlags));

		next = strtok_s(NULL, pDelims, &strtokcontext);
		i++;
	}
	ScratchFree(param);

	if (!RESULT_GOOD(ok)) return ok;

	// shorten array if necessary
	if (i < numelems)
	{
		for (e = numelems-1; e >= i; e--)
		{
			nonarray_destroystruct(tpi, ptcc, column, structptr, e);
		}
		TokenStoreSetEArraySize(tpi, column, structptr, i, &ok);
	}

	return ok;
}

// basically the strategy here is to lean on the comma-separated format of to/from simple
static __forceinline TextParserResult earray_tomulti(ParseTable tpi[], int column, void* structptr, int index_ignored, MultiVal* result, bool bDuplicateData, bool dummyOnNULL)
{
	// The code that was here before (convert to string, set as string) makes no sense and is not called
	return PARSERESULT_ERROR;
}

static __forceinline TextParserResult earray_frommulti(ParseTable tpi[], int column, void* structptr, int index_ignored, const MultiVal* value)
{
	// The code that was here before (convert to string, set as string) makes no sense and is not called
	return PARSERESULT_ERROR;
}

static __forceinline void earray_updatecrc(ParseTable tpi[], int column, void* structptr, int index_ignored)
{
	int i, numelems = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structptr, NULL);

	IF_DEBUG_CRCS ( if (numelems) printf("%sAbout to CRC each of the %d elements of the earray\n", pDebugCRCPrefix, numelems); estrConcatf(&pDebugCRCPrefix, "  "); )

	for (i = 0; i < numelems; i++)
	{
		nonarray_updatecrc(tpi, column, structptr, i);
		IF_DEBUG_CRCS (	printf("%sAfter earray element %d, CRC is %u\n", pDebugCRCPrefix, i, cryptAdlerGetCurValue()); )
	}

	IF_DEBUG_CRCS ( estrSetSize(&pDebugCRCPrefix, estrLength(&pDebugCRCPrefix) - 2); )
}

static __forceinline int earray_compare(ParseTable tpi[], int column, const void* lhs, const void* rhs, int index, enumCompareFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int numleft = lhs||!(eFlags & COMPAREFLAG_NULLISDEFAULT)?TokenStoreGetNumElems_inline(tpi, &tpi[column], column, lhs, NULL):0;
	int numright = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, rhs, NULL);
	int i, ret = 0;
	compare_f compareFunc;
	U8	*left_start,*right_start;
	int	size=0;
	
	if (numleft != numright)
		return numleft - numright;

	if (numleft == 0)
	{
		return 0;
	}
	if (simpleEarrayElementCompareParams(tpi,column,lhs,rhs,&left_start,&right_start,&size))
	{
		if (memcmp(left_start, right_start, numleft * size) == 0)
			return 0;
	}

	compareFunc = TYPE_INFO(tpi[column].type).compare;
	if (!compareFunc)
		return 0;
	for (i = 0; i < numleft && ret == 0; i++)
	{
		if (!size || memcmp(left_start + i*size, right_start + i*size, size) != 0)
			ret = compareFunc(tpi, column, lhs, rhs, i, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
	}
	return ret;
}



static __forceinline void earray_calcoffset(ParseTable tpi[], int column, size_t* size)
{
	// don't need to refer to subtype
	MEM_ALIGN(*size, sizeof(void*));
	tpi[column].storeoffset = *size;
	(*size) += sizeof(void*);
}

static __forceinline void earray_copystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index_ignored, CustomMemoryAllocator memAllocator, void* customData)
{
	int i, numelems = TokenStoreGetNumElems_inline(tpi, ptcc, column, src, NULL);
	TokenStoreCopyEArray(tpi, column, dest, src, memAllocator, customData, NULL);
	for (i = 0; i < numelems; i++)
	{
		nonarray_copystruct(tpi, ptcc, column, dest, src, i, memAllocator, customData);
	}
}

static __forceinline void earray_copyfield(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index_ignored, CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int i, srcelems = TokenStoreGetNumElems_inline(tpi, ptcc, column, src, NULL);

	earray_destroystruct(tpi, ptcc, column, dest, 0);
	earray_initstruct(tpi, column, dest, 0);

	for (i = 0; i < srcelems; i++)
		nonarray_copyfield(tpi, ptcc, column, dest, src, i, memAllocator, customData, iOptionFlagsToMatch, iOptionFlagsToExclude);
}

static __forceinline enumCopy2TpiResult earray_copyfield2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest,  
	CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, char **ppResultString)
{
	int i, srcelems = TokenStoreGetNumElems_inline(src_tpi, &src_tpi[src_column], src_column, src, NULL);
	enumCopy2TpiResult eResult = COPY2TPIRESULT_SUCCESS;

	earray_destroystruct(dest_tpi, &dest_tpi[dest_column], dest_column, dest, 0);
	earray_initstruct(dest_tpi, dest_column, dest, 0);

	for (i = 0; i < srcelems; i++)
	{
		enumCopy2TpiResult eRecurseResult = nonarray_copyfield2tpis(src_tpi, src_column, i, src, dest_tpi, dest_column, i, dest, memAllocator, customData, iOptionFlagsToMatch, iOptionFlagsToExclude, ppResultString);
		eResult = MAX(eResult, eRecurseResult);
	}

	return eResult;
}

static __forceinline enumCopy2TpiResult earray_stringifycopy(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest, char **ppResultString)
{
	int i, srcelems = TokenStoreGetNumElems_inline(src_tpi, &src_tpi[src_column], src_column, src, NULL);
	enumCopy2TpiResult eResult = COPY2TPIRESULT_SUCCESS;

	earray_destroystruct(dest_tpi, &dest_tpi[dest_column], dest_column, dest, 0);
	earray_initstruct(dest_tpi, dest_column, dest, 0);

	for (i = 0; i < srcelems; i++)
	{
		enumCopy2TpiResult eRecurseResult = nonarray_stringifycopy(src_tpi, src_column, i, src, dest_tpi, dest_column, i, dest, ppResultString);
		eResult = MAX(eResult, eRecurseResult);
	}

	return eResult;
}


static __forceinline void earray_endianswap(ParseTable tpi[], int column, void* structptr, int index_ignored)
{
	int i, numelems = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structptr, NULL);
	for (i = 0; i < numelems; i++)
		nonarray_endianswap(tpi, column, structptr, i);
}

static __forceinline void earray_interp(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index_ignored, F32 interpParam)
{
	int i;
	int numelemsA = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structA, NULL);
	int numelemsB = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structB, NULL);
	if (numelemsB < numelemsA) numelemsA = numelemsB; // use min
	for (i = 0; i < numelemsA; i++)
		nonarray_interp(tpi, column, structA, structB, destStruct, i, interpParam);
}

static __forceinline void earray_calcrate(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index_ignored, F32 deltaTime)
{
	int i;
	int numelemsA = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structA, NULL);
	int numelemsB = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structB, NULL);
	if (numelemsB < numelemsA) numelemsA = numelemsB; // use min
	for (i = 0; i < numelemsA; i++)
		nonarray_calcrate(tpi, column, structA, structB, destStruct, i, deltaTime);
}

static __forceinline void earray_integrate(ParseTable tpi[], int column, void* valueStruct, void* rateStruct, void* destStruct, int index_ignored, F32 deltaTime)
{
	int i;
	int numelemsA = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, valueStruct, NULL);
	int numelemsB = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, rateStruct, NULL);
	if (numelemsB < numelemsA) numelemsA = numelemsB; // use min
	for (i = 0; i < numelemsA; i++)
		nonarray_integrate(tpi, column, valueStruct, rateStruct, destStruct, i, deltaTime);
}

static __forceinline void earray_calccyclic(ParseTable tpi[], int column, void* valueStruct, void* ampStruct, void* freqStruct, void* cycleStruct, void* destStruct, int index_ignored, F32 fStartTime, F32 deltaTime)
{
	int i;
	int numelemsV = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, valueStruct, NULL);
	int numelemsA = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, ampStruct, NULL);
	int numelemsF = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, freqStruct, NULL);
	int numelemsC = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, cycleStruct, NULL);
	numelemsV = min(numelemsV, min(numelemsA, min(numelemsF, numelemsC)));
	for (i = 0; i < numelemsV; i++)
		nonarray_calccyclic(tpi, column, valueStruct, ampStruct, freqStruct, cycleStruct, destStruct, i, fStartTime, deltaTime);
}

static __forceinline void earray_applydynop(ParseTable tpi[], int column, void* dstStruct, void* srcStruct, int index_ignored, DynOpType optype, const F32* values, U8 uiValuesSpecd, U32* seed)
{
	int i, numelems;
	
	// for inherit, dstStruct may be empty, otherwise, dstStruct pretty much governs the operation
	if (optype == DynOpType_Inherit) 
		numelems = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, srcStruct, NULL);
	else
		numelems = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, dstStruct, NULL);

	if (uiValuesSpecd >= numelems) // values & seeds spread to components
	{
		for (i = 0; i < numelems; i++)
			nonarray_applydynop(tpi, column, dstStruct, srcStruct, i, optype, values+i, uiValuesSpecd, seed+i);
	}
	else // save value & seed to each component
	{
		for (i = 0; i < numelems; i++)
			nonarray_applydynop(tpi, column, dstStruct, srcStruct, i, optype, values, uiValuesSpecd, seed);
	}
}

static __forceinline void earray_fixupsharedmemory(ParseTable tpi[], int column, void* structptr, int index_ignored, char **ppFixupData)
{
	int i, numelems = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structptr, NULL);
	for (i = 0; i < numelems; i++)
		nonarray_fixupsharedmemory(tpi, column, structptr, i, ppFixupData);
}

static __forceinline TextParserResult earray_leafFirstFixup(ParseTable tpi[], int column, void* structptr, int index_ignored, enumTextParserFixupType eFixupType, void *pExtraData)
{
	leafFirstFixup_f leafFirstFixupFunc = TYPE_INFO(tpi[column].type).leafFirstFixup;
	int iSucceeded = 1;

	if (leafFirstFixupFunc)
	{
		int i, numelems = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structptr, NULL);
		for (i = 0; i < numelems; i++)
		{
			if (!leafFirstFixupFunc(tpi, column, structptr, i, eFixupType, pExtraData))
				iSucceeded = 0;
		}
	}
	return iSucceeded;
}

static __forceinline void earray_reapplypreparse(ParseTable tpi[], int column, void* structptr, int index_ignored, char *pCurrentFile, int iTimeStamp, int iLineNum)
{
	int i, numelems = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structptr, NULL);
	for (i = 0; i < numelems; i++)
		nonarray_reapplypreparse(tpi, column, structptr, i, pCurrentFile, iTimeStamp, iLineNum);
}

static __forceinline int earray_checksharedmemory(ParseTable tpi[], int column, void* structptr, int index_ignored)
{
	int iSucceeded = 1;
	U32 storage = TokenStoreGetStorageType(tpi[column].type);

	if (!TokenStoreCheckEArraySharedMemory(tpi,column,structptr, NULL))
	{
		const char *structName;
		if (!(structName = ParserGetTableName(tpi)))
		{
			structName = "UNKNOWN";
		}
		assertmsgf(0,"EArray named %s in struct %s is not pointing to valid shared memory! This will crash when loaded from shared memory.",tpi[column].name,structName);
		return 0;
	}
	else
	{	
		int i, numelems = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structptr, NULL);
		for (i = 0; i < numelems; i++)
			SET_MIN_RESULT(iSucceeded, nonarray_checksharedmemory(tpi, column, structptr, i));

		return iSucceeded;
	}
}

static __forceinline void earray_preparesharedmemoryforfixup(ParseTable tpi[], int column, void* structptr, int index_ignored, char **ppFixupData)
{
	int i, numelems = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structptr, NULL);
	for (i = 0; i < numelems; i++)
		nonarray_preparesharedmemoryforfixup(tpi, column, structptr, i, ppFixupData);
}

static __forceinline int earray_recvdiff2tpis(Packet *pak, ParseTable src_tpi[], ParseTable dest_tpi[], int src_column, int index, Recv2TpiCachedInfo *pCache, void *data)
{
	int type = TOK_GET_TYPE(src_tpi[src_column].type);
	int dest_column = pCache->pColumns[src_column].iTargetColumn;
	int numnew;
	int i;

	if (type == TOK_FUNCTIONCALL_X)
	{
		return nonarray_recvdiff2tpis(pak, src_tpi, dest_tpi, src_column, 0, pCache, data);
	}

	if (ParserColumnIsIndexedEArray(dest_tpi, dest_column, NULL))
	{
		ParseTableInfo *info = ParserGetTableInfo(dest_tpi);
		void ***pppEarray = TokenStoreGetEArray_inline(dest_tpi, &dest_tpi[dest_column], dest_column, data, NULL);
		eaIndexedEnable_dbg(pppEarray,dest_tpi[dest_column].subtable, SAFE_MEMBER(info, name)?SAFE_MEMBER(info, name):__FILE__, SAFE_MEMBER(info, name)?LINENUM_FOR_EARRAYS:__LINE__);
	}


	assert(pktGetBits(pak, 1) == 0); //should always be simple mode

	numnew = pktGetBits(pak,32);
	pktGetBits(pak, 1); //don't care about sentabs, which will always be true in a practical sense.

	for (i = 0; i < numnew && !pktEnd(pak); i++)
	{
		if (!nonarray_recvdiff2tpis(pak, src_tpi, dest_tpi, src_column, i, pCache, data))
		{
			return 0;
		}
		
		// Make sure our array is now at least the appropriate size
		if (data)
		{
			int numnow = TokenStoreGetNumElems_inline(dest_tpi, &dest_tpi[dest_column], dest_column, data, NULL);
			if(numnow<i+1)
			{
				TokenStoreSetEArraySize(dest_tpi, dest_column, data, i+1, NULL);
			}
		}
	}



	return 1;
}

static __forceinline bool earray_writehtmlfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteHTMLContext *pContext)
{
	return all_arrays_writehtmlfile(tpi, column, structptr, index, out, pContext);
}

static __forceinline bool earray_writexmlfile(ParseTable tpi[], int column, void* structptr, int index, int level, FILE* out, StructFormatField iOptions)
{
	return array_writexmlfile(tpi, column, structptr, index, level, out, iOptions);
}

static __forceinline bool earray_writehdf(ParseTable tpi[], int column, void* structptr, int index, HDF *hdf, char *name_override)
{
	return array_writehdf(tpi, column, structptr, index, hdf, name_override);
}

static __forceinline bool earray_writejsonfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	return array_writejsonfile(tpi, column, structptr, index, out, eFlags,
		iOptionFlagsToMatch, iOptionFlagsToExclude);
}



static __forceinline int nonarray_textdiffwithnull(char** estr, ParseTable tpi[], int column, void* newstruct, int index, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	int type = TOK_GET_TYPE(tpi[column].type);
	textdiffwithnull_f textdiffwithnullFunc;

	switch (type)
	{
	case TOK_INT_X:
		return int_textdiffwithnull(estr, tpi, column, newstruct, index, prefix, iPrefixLen, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, caller_fname, line);
	case TOK_F32_X:
		return float_textdiffwithnull(estr, tpi, column, newstruct, index, prefix, iPrefixLen, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, caller_fname, line);
	case TOK_STRING_X:
		return string_textdiffwithnull(estr, tpi, column, newstruct, index, prefix, iPrefixLen, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, caller_fname, line);
	case TOK_STRUCT_X:
		return struct_textdiffwithnull(estr, tpi, column, newstruct, index, prefix, iPrefixLen, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, caller_fname, line);
	}

	textdiffwithnullFunc = TYPE_INFO(tpi[column].type).textdiffwithnull;
	if (textdiffwithnullFunc)
	{
		return textdiffwithnullFunc(estr,tpi, column,newstruct,index,prefix,iPrefixLen,iOptionFlagsToMatch,iOptionFlagsToExclude, eFlags, caller_fname, line);
	}

	return 0;
}
static __forceinline int fixedarray_textdiffwithnull(char** estr, ParseTable tpi[], int column, void* newstruct, int index, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	char *newPath = NULL;
	bool bWroteSomething = false;
	int i, numelems = tpi[column].param;
	int type = TOK_GET_TYPE(tpi[column].type);

	for (i = 0; i < numelems; i++)
	{
		char temp[16];
		int iNumDigits;
		
		if (!newPath)
		{
			estrStackCreateSize(&newPath, iPrefixLen + 30);
			memcpy(newPath, prefix, iPrefixLen);
			newPath[iPrefixLen] = '[';
		}

		iNumDigits = fastIntToString_inline(i, temp);

		memcpy(newPath + iPrefixLen + 1, temp + 16 - iNumDigits, iNumDigits);
		newPath[iPrefixLen + 1 + iNumDigits] = ']';
		newPath[iPrefixLen + 2 + iNumDigits] = 0;

		bWroteSomething |= nonarray_textdiffwithnull(estr, tpi, column, newstruct, i, newPath, iPrefixLen + iNumDigits + 2, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, caller_fname, line);
		
	}
	estrDestroy(&newPath);
	return bWroteSomething;
}


static __forceinline int indexedearray_textdiffwithnull(char** estr, ParseTable tpi[], int iKeyColumn, int column, void* newstruct, int index, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	char *newPath = 0;
	int iCount = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, newstruct, NULL);
	int type = TOK_GET_TYPE(tpi[column].type);
	int i;
	int iFixedUpPrefixLen = 0;

	ParseTable *subtable = tpi[column].subtable;

	ParserSortData sortData;
	bool bWroteSomething = false;

	sortData.tpi = subtable;
	sortData.column = iKeyColumn;
	
	for (i = 0; i < iCount; i++)
	{
		void *newp = TokenStoreGetPointer_inline(tpi, &tpi[column], column, newstruct, i, NULL);

		if (!newPath)
		{
			estrStackCreateSize(&newPath, MIN_STACK_ESTR);
			estrSetSize(&newPath, iPrefixLen);
			memcpy(newPath, prefix, iPrefixLen);
			estrConcat_dbg_inline(&newPath, "[\"", 2, caller_fname, line);
			iFixedUpPrefixLen = estrLength(&newPath);
		}
		else
		{
			estrSetSizeUnsafe_inline(&newPath, iFixedUpPrefixLen);
		}


		devassertmsgf(ParserGetKeyStringDbg_inline(subtable, newp, &newPath, true, caller_fname, line), "Unable to get key string for %s", ParserGetTableName(subtable));
		estrConcat_dbg_inline(&newPath, "\"]", 2, caller_fname, line);
			
		bWroteSomething |= StructTextDiffWithNull_dbg(estr,subtable,newp,newPath, estrLength(&newPath), iOptionFlagsToMatch,
			iOptionFlagsToExclude,eFlags, caller_fname, line);

			
	}
	estrDestroy(&newPath);
	return bWroteSomething;
}

static __forceinline int nonindexedearray_textdiffwithnull(char** estr, ParseTable tpi[], int column, void* newstruct, int index, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{	
	char *newPath = 0;
	int i, numelems = newstruct?TokenStoreGetNumElems_inline(tpi, &tpi[column], column, newstruct, NULL):0;
	int type = TOK_GET_TYPE(tpi[column].type);
	bool bWroteSomething = false;

	for (i = 0; i < numelems; i++)
	{
		char temp[16];
		int iNumDigits;

		if (!newPath)
		{
			estrStackCreateSize(&newPath, iPrefixLen + 30);
			memcpy(newPath, prefix, iPrefixLen);
			newPath[iPrefixLen] = '[';
		}

		iNumDigits = fastIntToString_inline(i, temp);

		memcpy(newPath + iPrefixLen + 1, temp + 16 - iNumDigits, iNumDigits);
		newPath[iPrefixLen + 1 + iNumDigits] = ']';
		newPath[iPrefixLen + 2 + iNumDigits] = 0;

		// New one
		if (!TOK_HAS_SUBTABLE(tpi[column].type))
		{
			// Add the create now
			estrConcatf_dbg(estr, caller_fname, line, "create %s\n",newPath);
		}

		bWroteSomething |= nonarray_textdiffwithnull(estr,tpi,column,newstruct,i,newPath, iPrefixLen + 2 + iNumDigits, 
			iOptionFlagsToMatch,iOptionFlagsToExclude,eFlags, caller_fname, line);
	}


	estrDestroy(&newPath);
	return bWroteSomething;
}

static __forceinline int earray_textdiffwithnull(char** estr, ParseTable tpi[], int column, void* newstruct, int index, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{

	int keyColumn;

	if (ParserColumnIsIndexedEArray(tpi, column, &keyColumn))			
	{
		return indexedearray_textdiffwithnull(estr, tpi, keyColumn, column, newstruct, index, prefix, iPrefixLen, iOptionFlagsToMatch, iOptionFlagsToExclude,
			eFlags, caller_fname, line);
	}
	else
	{
		return nonindexedearray_textdiffwithnull(estr, tpi, column, newstruct, index, prefix, iPrefixLen, iOptionFlagsToMatch, iOptionFlagsToExclude,
			eFlags, caller_fname, line);
	}
}
