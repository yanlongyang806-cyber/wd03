#pragma once

#include "blockEarray.h"
#include "ReferenceSystem.h"
#include "structInternals.h"
#include "textparser.h"
#include "tokenstore.h"
#include "tokenstore_inline.h"

//////////////////////////////////////// TokenStoreXxxx functions


#define SETRESULT(level) if (result) MIN1(*result,(level));

//checks whether a particular type uses blockEarray as opposed to ea32
static __forceinline bool TokenStoreEarrayIsBlockArray(StructTypeField type)
{
	if (TOK_GET_TYPE(type) == TOK_MULTIVAL_X || TOK_GET_TYPE(type) == TOK_STRUCT_X)
	{
		return true;
	}

	return false;
}

static __forceinline int TokenStoreGetNumElems_inline(ParseTable tpi[], ParseTable *ptcc, int column, const void* structptr, TextParserResult *result)
{
	int** ea32;
	void*** ea;
	void** bea;
	U32 storage = TokenStoreGetStorageType(ptcc->type);

	switch (storage) {
	case TOK_STORAGE_INDIRECT_SINGLE: // fall
	case TOK_STORAGE_DIRECT_SINGLE: return 1;
	case TOK_STORAGE_INDIRECT_FIXEDARRAY: // fall
	case TOK_STORAGE_DIRECT_FIXEDARRAY:	return ptcc->param;

	case TOK_STORAGE_DIRECT_EARRAY: 
		if (TokenStoreEarrayIsBlockArray(ptcc->type))
		{
			bea = (void**)((intptr_t)structptr + ptcc->storeoffset);
			return beaSize(bea);
		}

		ea32 = (int**)((intptr_t)structptr + ptcc->storeoffset);
		return ea32Size(ea32);
	case TOK_STORAGE_INDIRECT_EARRAY:
		ea = (void***)((intptr_t)structptr + ptcc->storeoffset);
		return eaSize(ea);
	}
	return 0;
}


static __forceinline int TokenStoreGetInt_inline(ParseTable tpi[], ParseTable *ptcc, int column, const void* structptr, int index, TextParserResult *result)
{
	int** parray;
	int* pint;
	U32 storage = TokenStoreGetStorageType(ptcc->type);

	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_DIRECT_FIXEDARRAY | TOK_STORAGE_DIRECT_EARRAY);

	switch (storage) {
	case TOK_STORAGE_DIRECT_SINGLE:
		pint = (int*)((intptr_t)structptr + ptcc->storeoffset);
		return *pint;
	case TOK_STORAGE_DIRECT_FIXEDARRAY:
		pint = (int*)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= ptcc->param || index < 0)
		{
			SETRESULT(PARSERESULT_ERROR);
			return 0;
		}
		return pint[index];
	case TOK_STORAGE_DIRECT_EARRAY:
		parray = (int**)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= eaiSize(parray) || index < 0)
		{
			SETRESULT(PARSERESULT_ERROR);
			return 0;
		}
		return (*parray)[index];
	};
	return 0;
}

static void TokenStoreSetInt_inline(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, int value, TextParserResult *result, TokenizerHandle tok)
{
	int** parray;
	int* pint;
	U32 storage = TokenStoreGetStorageType(ptcc->type);

	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_DIRECT_FIXEDARRAY | TOK_STORAGE_DIRECT_EARRAY);

	switch (storage) {
	case TOK_STORAGE_DIRECT_SINGLE:
		pint = (int*)((intptr_t)structptr + ptcc->storeoffset);
		*pint = value;
		break;
	case TOK_STORAGE_DIRECT_FIXEDARRAY:
		pint = (int*)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= ptcc->param || index < 0)
		{
			if (tok)
				TokenizerErrorf(tok, "Extra integer parameter");
			SETRESULT(PARSERESULT_ERROR);
		}
		else
		{
			pint[index] = value;
		}
		break;
	case TOK_STORAGE_DIRECT_EARRAY:
		parray = (int**)((intptr_t)structptr + ptcc->storeoffset);
		if (index > eaiSize(parray) || index < 0)
		{
			if (tok)
				TokenizerErrorf(tok, "Invalid array index");
			SETRESULT(PARSERESULT_ERROR);
			return;
		}
		if (index < eaiSize(parray))
			(*parray)[index] = value;
		else {
			ParseTableInfo *info = ParserGetTableInfo(tpi);
			if (info && info->name) {
				eaiPush_dbg(parray, value, info->name, LINENUM_FOR_EARRAYS);
			} else {
				eaiPush(parray, value);
			}
		}
		break;
	};
}

static __forceinline U8 TokenStoreGetU8_inline(ParseTable tpi[], ParseTable *ptcc, int column, const void* structptr, int index, TextParserResult *result)
{
	U8* pint;
	U32 storage = TokenStoreGetStorageType(ptcc->type);

	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_DIRECT_FIXEDARRAY);

	switch (storage) {
	case TOK_STORAGE_DIRECT_FIXEDARRAY:
		pint = (U8*)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= ptcc->param || index < 0)
		{
			SETRESULT(PARSERESULT_ERROR);
			return 0;
		}
		return pint[index];
	case TOK_STORAGE_DIRECT_SINGLE:
		pint = (U8*)((intptr_t)structptr + ptcc->storeoffset);
		return *pint;
	};
	return 0;
}

static __forceinline void TokenStoreSetU8_inline(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, U8 value, TextParserResult *result, TokenizerHandle tok)
{
	U8* pint;
	U32 storage = TokenStoreGetStorageType(ptcc->type);

	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_DIRECT_FIXEDARRAY);

	switch (storage) {
	case TOK_STORAGE_DIRECT_FIXEDARRAY:
		pint = (U8*)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= ptcc->param || index < 0)
		{
			if (tok)
				TokenizerErrorf(tok,"Extra integer parameter");
			SETRESULT(PARSERESULT_ERROR);
		}
		else
		{
			pint[index] = value;
		}
		break;
	case TOK_STORAGE_DIRECT_SINGLE:
		pint = (U8*)((intptr_t)structptr + ptcc->storeoffset);
		*pint = value;
		break;
	};
}

static __forceinline S16 TokenStoreGetInt16_inline(ParseTable tpi[], ParseTable *ptcc, int column, const void* structptr, int index, TextParserResult *result)
{
	S16* pint;
	U32 storage = TokenStoreGetStorageType(ptcc->type);

	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_DIRECT_FIXEDARRAY);

	switch (storage) {
	case TOK_STORAGE_DIRECT_FIXEDARRAY:
		pint = (S16*)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= ptcc->param || index < 0)
		{			
			SETRESULT(PARSERESULT_ERROR);
			return 0;
		}
		return pint[index];
	case TOK_STORAGE_DIRECT_SINGLE:
		pint = (S16*)((intptr_t)structptr + ptcc->storeoffset);
		return *pint;
	};
	return 0;
}


static __forceinline void TokenStoreSetInt16_inline(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, S16 value, TextParserResult *result, TokenizerHandle tok)
{
	S16* pint;
	U32 storage = TokenStoreGetStorageType(ptcc->type);

	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_DIRECT_FIXEDARRAY);

	switch (storage) {
	case TOK_STORAGE_DIRECT_FIXEDARRAY:
		pint = (S16*)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= ptcc->param || index < 0)
		{
			if (tok)
				TokenizerErrorf(tok,"Extra integer parameter");
			SETRESULT(PARSERESULT_ERROR);
		}
		else
		{
			pint[index] = value;
		}
		break;
	case TOK_STORAGE_DIRECT_SINGLE:
		pint = (S16*)((intptr_t)structptr + ptcc->storeoffset);
		*pint = value;
		break;
	};
}

static __forceinline S64 TokenStoreGetInt64_inline(ParseTable tpi[], ParseTable *ptcc, int column, const void* structptr, int index, TextParserResult *result)
{
	S64* pint;
	U32 storage = TokenStoreGetStorageType(ptcc->type);

	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_DIRECT_FIXEDARRAY);

	switch (storage) {
	case TOK_STORAGE_DIRECT_FIXEDARRAY:
		pint = (S64*)((intptr_t)structptr + ptcc->storeoffset);
		if (index > ptcc->param || index < 0)
		{			
			SETRESULT(PARSERESULT_ERROR);
			return 0;
		}
		else
			return pint[index];
	case TOK_STORAGE_DIRECT_SINGLE:
		pint = (S64*)((intptr_t)structptr + ptcc->storeoffset);
		return *pint;
	};
	return 0;
}

static __forceinline void TokenStoreSetInt64_inline(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, S64 value, TextParserResult *result, TokenizerHandle tok)
{
	S64* pint;
	U32 storage = TokenStoreGetStorageType(ptcc->type);

	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_DIRECT_FIXEDARRAY);

	switch (storage) {
	case TOK_STORAGE_DIRECT_FIXEDARRAY:
		pint = (S64*)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= ptcc->param || index < 0)
		{
			if (tok)
				TokenizerErrorf(tok,"Extra integer parameter");
			SETRESULT(PARSERESULT_ERROR);
		}
		else
		{
			pint[index] = value;
		}
		break;
	case TOK_STORAGE_DIRECT_SINGLE:
		pint = (S64*)((intptr_t)structptr + ptcc->storeoffset);
		*pint = value;
		break;
	};
}

static __forceinline U32 TokenStoreGetBit_inline(ParseTable tpi[], ParseTable *ptcc, int column, const void* structptr, int index, TextParserResult *result)
{
	U32 storage = TokenStoreGetStorageType(ptcc->type);

	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE);

	switch (storage) 
	{

	case TOK_STORAGE_DIRECT_SINGLE:
		{			
			int iWord = TextParserBitField_GetWordNum(tpi, column);
			int iBit = TextParserBitField_GetBitNum(tpi, column);
			int iCount  = TextParserBitField_GetBitCount(tpi, column);

			U32 *pWords = (U32*)structptr;

			assert(iBit >= 0 && iBit < 32);
			assert(iCount >= 1 && iCount <= 32);
			assert(iBit + iCount <= 32);

			if (iCount == 32)
			{
				return pWords[iWord];
			}
			else
			{
				return (pWords[iWord] >> iBit) & ((1 << iCount) - 1);
			}
		}
	}

	return 0;
}

static __forceinline void TokenStoreSetBit_inline(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, int value, TextParserResult *result, TokenizerHandle tok)
{
	U32 storage = TokenStoreGetStorageType(ptcc->type);

	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE);

	switch (storage) {

	case TOK_STORAGE_DIRECT_SINGLE:
		{
			int iWord = TextParserBitField_GetWordNum(tpi, column);
			int iBit = TextParserBitField_GetBitNum(tpi, column);
			int iCount  = TextParserBitField_GetBitCount(tpi, column);
			U32 *pWords = (U32*)structptr;


			assert(iBit >= 0 && iBit < 32);
			assert(iCount >= 1 && iCount <= 32);
			assert(iBit + iCount <= 32);

			if (iCount == 32)
			{
				pWords[iWord] = value;
			}
			else
			{
				U32 iValueToUse = ((U32)value) & ((1 << iCount) - 1);
				pWords[iWord] &= ~(((1 << iCount)-1) << iBit);
				pWords[iWord] |= iValueToUse << iBit;
			}
		}
		break;
	}
}


static __forceinline void* TokenStoreGetPointer_inline(ParseTable tpi[], ParseTable *ptcc, int column, const void* structptr, int index, TextParserResult *result)
{
	void*** ea;
	void **bea;
	U32 storage = TokenStoreGetStorageType(ptcc->type);

	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_DIRECT_FIXEDARRAY | TOK_STORAGE_INDIRECT_SINGLE | TOK_STORAGE_INDIRECT_EARRAY | TOK_STORAGE_DIRECT_EARRAY);

	switch (storage) {
	case TOK_STORAGE_DIRECT_SINGLE:
	case TOK_STORAGE_DIRECT_FIXEDARRAY:
		return (void*)((intptr_t)structptr + ptcc->storeoffset);
	case TOK_STORAGE_INDIRECT_SINGLE:
		return *((void**)((intptr_t)structptr + ptcc->storeoffset));
	case TOK_STORAGE_INDIRECT_EARRAY:
		ea = (void***)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= eaSize(ea) || index < 0)
		{
			SETRESULT(PARSERESULT_ERROR);
			return NULL;
		}
		return (*ea)[index];

	case TOK_STORAGE_DIRECT_EARRAY:
		bea = (void**)((intptr_t)structptr + tpi[column].storeoffset);
		if (index >= beaSize(bea) || index < 0)
		{
			SETRESULT(PARSERESULT_ERROR);
			return NULL;
		}
		return ((char*)(*bea)) + index * beaBlockSize(bea);

	};
	return NULL;
}

static __forceinline void TokenStoreSetPointer_inline(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, void* ptr, TextParserResult *result)
{
	U32 storage = TokenStoreGetStorageType(ptcc->type);
	void** pointer;
	void*** parray;

	TS_REQUIRE(TOK_STORAGE_INDIRECT_SINGLE | TOK_STORAGE_INDIRECT_EARRAY);
	switch (storage) {
	case TOK_STORAGE_INDIRECT_SINGLE:
		pointer = (void**)((intptr_t)structptr + ptcc->storeoffset);
		*pointer = ptr;
		break;
	case TOK_STORAGE_INDIRECT_EARRAY:
		parray = (void***)((intptr_t)structptr + ptcc->storeoffset);
		if (index > eaSize(parray) || index < 0)
		{
			SETRESULT(PARSERESULT_ERROR);
			return;
		}
		if (index < eaSize(parray))
			(*parray)[index] = ptr;
		else {
			ParseTableInfo *info = ParserGetTableInfo(tpi);
			if (info && info->name) {
				eaPush_dbg(parray, ptr, info->name, LINENUM_FOR_EARRAYS);
			} else {
				eaPush(parray, ptr);
			}
		}
		break;
	};
}


static __forceinline F32 TokenStoreGetF32_inline(ParseTable tpi[], ParseTable *ptcc, int column, const void* structptr, int index, TextParserResult *result)
{
	F32** parray;
	F32* pfloat;
	U32 storage = TokenStoreGetStorageType(ptcc->type);

	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_DIRECT_FIXEDARRAY | TOK_STORAGE_DIRECT_EARRAY);

	switch (storage) {
	case TOK_STORAGE_DIRECT_SINGLE:
		pfloat = (F32*)((intptr_t)structptr + ptcc->storeoffset);
		return *pfloat;
	case TOK_STORAGE_DIRECT_FIXEDARRAY:
		pfloat = (F32*)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= ptcc->param || index < 0)
		{
			SETRESULT(PARSERESULT_ERROR);
			return 0;
		}
		return pfloat[index];
	case TOK_STORAGE_DIRECT_EARRAY:
		parray = (F32**)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= eafSize(parray) || index < 0)
		{
			SETRESULT(PARSERESULT_ERROR);
			return 0;
		}
		return (*parray)[index];
	};
	return 0;
}

static __forceinline void TokenStoreSetF32_inline(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, F32 value, TextParserResult *result, TokenizerHandle tok)
{
	F32** parray;
	F32* pfloat;
	U32 storage = TokenStoreGetStorageType(ptcc->type);

	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_DIRECT_FIXEDARRAY | TOK_STORAGE_DIRECT_EARRAY);

	switch (storage) {
	case TOK_STORAGE_DIRECT_SINGLE:
		pfloat = (F32*)((intptr_t)structptr + ptcc->storeoffset);
		*pfloat = value;
		break;
	case TOK_STORAGE_DIRECT_FIXEDARRAY:
		pfloat = (F32*)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= ptcc->param || index < 0)
		{
			if (tok)
				TokenizerErrorf(tok,"Extra float parameter");
			SETRESULT(PARSERESULT_ERROR);
		}
		else
			pfloat[index] = value;
		break;
	case TOK_STORAGE_DIRECT_EARRAY:
		parray = (F32**)((intptr_t)structptr + ptcc->storeoffset);
		if (index > eafSize(parray) || index < 0)
		{
			if (tok)
				TokenizerErrorf(tok, "Invalid array index");
			SETRESULT(PARSERESULT_ERROR);
			return;
		}
		if (index < eafSize(parray))
			(*parray)[index] = value;
		else {
			ParseTableInfo *info = ParserGetTableInfo(tpi);
			if (info && info->name) {
				eafPush_dbg(parray, value, info->name, LINENUM_FOR_EARRAYS);
			} else {
				eafPush(parray, value);
			}
		}
		break;
	};
}


SA_RET_OP_STR static __forceinline const char* TokenStoreGetString_inline(ParseTable tpi[], ParseTable *ptcc, int column, const void* structptr, int index, TextParserResult *result)
{
	char** pstr;
	char*** parray;
	U32 storage = TokenStoreGetStorageType(ptcc->type);
	
	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_INDIRECT_SINGLE | TOK_STORAGE_INDIRECT_EARRAY);

	switch (storage) {
	case TOK_STORAGE_DIRECT_SINGLE:
		return (char*)((intptr_t)structptr + ptcc->storeoffset);
	case TOK_STORAGE_INDIRECT_EARRAY:
		parray = (char***)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= eaSize(parray) || index < 0)
		{			
			SETRESULT(PARSERESULT_ERROR);
			return 0;
		}
		return (*parray)[index];
	case TOK_STORAGE_INDIRECT_SINGLE:
		pstr = (char**)((intptr_t)structptr + ptcc->storeoffset);
		return *pstr;
	};
	return 0;
}

// clear the target string WITHOUT freeing it
static __forceinline void TokenStoreClearString_inline(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *result)
{
	U32 storage = TokenStoreGetStorageType(ptcc->type);
	char* str;
	char** pstr;
	char*** parray;

	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_INDIRECT_SINGLE | TOK_STORAGE_INDIRECT_EARRAY);
	switch (storage) {
	case TOK_STORAGE_DIRECT_SINGLE:
		str = (char*)((intptr_t)structptr + ptcc->storeoffset);
		str[0] = 0;
		break;
	case TOK_STORAGE_INDIRECT_SINGLE:
		pstr = (char**)((intptr_t)structptr + ptcc->storeoffset);
		*pstr = 0;
		break;
	case TOK_STORAGE_INDIRECT_EARRAY:
		parray = (char***)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= eaSize(parray) || index < 0)
		{
			SETRESULT(PARSERESULT_ERROR);
			return;
		}
		(*parray)[index] = 0;
		break;
	};
}

static __forceinline int* TokenStoreGetCountField_inline(ParseTable tpi[], ParseTable *ptcc, int column, const void* structptr, TextParserResult *result)
{
	TS_REQUIRE(TOK_STORAGE_INDIRECT_SINGLE);
	return (int*)((intptr_t)structptr + ptcc->param);
}

static __forceinline void*** TokenStoreGetEArray_inline(ParseTable tpi[], ParseTable *ptcc, int column, const void* structptr, TextParserResult *result)
{
	TS_REQUIRE(TOK_STORAGE_INDIRECT_EARRAY);
	return (void***)((intptr_t)structptr + ptcc->storeoffset);
}

static __forceinline int** TokenStoreGetEArrayInt_inline(ParseTable tpi[], ParseTable *ptcc, int column, const void* structptr, TextParserResult *result)
{
	TS_REQUIRE(TOK_STORAGE_DIRECT_EARRAY);
	devassert(!TokenStoreEarrayIsBlockArray(ptcc->type));
	return (int**)((intptr_t)structptr + ptcc->storeoffset);
}

static __forceinline F32** TokenStoreGetEArrayF32_inline(ParseTable tpi[], ParseTable *ptcc, int column, const void* structptr, TextParserResult *result)
{
	TS_REQUIRE(TOK_STORAGE_DIRECT_EARRAY);
	devassert(!TokenStoreEarrayIsBlockArray(ptcc->type));
	return (F32**)((intptr_t)structptr + ptcc->storeoffset);
}

static __forceinline int TokenStoreGetFixedArraySize_inline(ParseTable tpi[], ParseTable *ptcc, int column)
{
	TS_REQUIRE(TOK_STORAGE_DIRECT_FIXEDARRAY | TOK_STORAGE_INDIRECT_FIXEDARRAY);
	return ptcc->param;
}


static __forceinline const char *TokenStoreGetRefString_inline(ParseTable tpi[], ParseTable *ptcc, int column, const void* structptr, int index, TextParserResult *result)
{
	void** pp;
	TS_REQUIRE(TOK_STORAGE_INDIRECT_SINGLE);

	pp = (void**)((intptr_t)structptr + ptcc->storeoffset);

	return RefSystem_StringFromHandle(pp);
}

static __forceinline void **TokenStoreGetRefHandlePointer_inline(ParseTable tpi[], ParseTable *ptcc, int column, const void *structPtr, int index, TextParserResult *result)
{
	TS_REQUIRE(TOK_STORAGE_INDIRECT_SINGLE);
	return (void**)((intptr_t)structPtr + ptcc->storeoffset);
}
