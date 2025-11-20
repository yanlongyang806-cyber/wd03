#include "tokenStore.h"
#include "structinternals.h"

#include "error.h"
#include "sysutil.h"
#include "stringcache.h"
#include "sharedmemory.h"
#include "referencesystem.h"
#include "estring.h"
#include "utilitiesLib.h"
#include "tokenstore_inline.h"
#include "timing.h"
#include "windefinclude.h"


// Thread local variable, set by DoTransactionCommand in objTransactionCommands.c.
typedef struct TokenStoreThreadData
{
	const char *curTransactionName;
} TokenStoreThreadData;

static TokenStoreThreadData *GetTokenStoreThreadData(void)
{
	TokenStoreThreadData *threadData;
	STATIC_THREAD_ALLOC(threadData);
	return threadData;
}

const char *GetCurTransactionName(void)
{
	TokenStoreThreadData *threadData = GetTokenStoreThreadData();
	return threadData->curTransactionName;
}

void SetCurTransactionName(const char *transactionName)
{
	TokenStoreThreadData *threadData = GetTokenStoreThreadData();
	threadData->curTransactionName = transactionName;
}

//textparser can only access the reference system from a single thread, which starts out as the main thread
//in which autoruns are called
extern U32 gTextParserRefSystemThread;
U32 gNoTextParserThreadCheck;
bool g_bDebugTokenStoreReferenceActions = false;

#define ASSERT_CORRECT_THREAD()				assertmsgf(GetCurrentThreadId() == gTextParserRefSystemThread, "TextParser trying to access the reference system from a disallowed thread")
#define ASSERT_CORRECT_THREAD_IF_ENABLED()	if(!gNoTextParserThreadCheck){ASSERT_CORRECT_THREAD();}
#define IS_CORRECT_THREAD()					(GetCurrentThreadId() == gTextParserRefSystemThread)


#define THREAD_REFERENCE_UNINIT			((void*)0xfcfcfcfcfcfcfcfcll)
// the value of a reference that has already been cleared in the main thread
#define REFERENCE_DESTROYED				((void*)0xfffffffffffffffell)

typedef enum TokenStoreQueuedReferenceOp
{
	TSQROP_INVALID =0,
	TSQROP_SET_REF,
	TSQROP_CLEAR_REF,
	TSQROP_DESTROY_REF,
	TSQROP_COPY_REF
} TokenStoreQueuedReferenceOp;

typedef struct TokenStoreQueuedReferenceLookup
{
	TokenStoreQueuedReferenceOp operation;
	DictionaryHandleOrName dict;
	void** param1;
	void** param2;
} TokenStoreQueuedReferenceLookup;

struct  
{
	CRITICAL_SECTION cs;
	TokenStoreQueuedReferenceLookup* data;
	int count, size;
} gDynQueuedReferenceLookups = {0};


AUTO_RUN_FIRST;
void InitQueuedReferenceCS(void)
{
	InitializeCriticalSection(&gDynQueuedReferenceLookups.cs);
}

// if allocated, free
void TokenStoreFreeString(ParseTable tpi[], int column, void* structptr, int index, TextParserResult *result)
{
	ParseTable *ptcc = &tpi[column];
	StructTypeField field_type = ptcc->type;
	U32 storage = TokenStoreGetStorageType(field_type);
	char* str;
	char** pstr;
	char*** parray;

	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_INDIRECT_SINGLE | TOK_STORAGE_INDIRECT_EARRAY);
	switch (storage) {
	case TOK_STORAGE_DIRECT_SINGLE:
		str = (char*)((intptr_t)structptr + ptcc->storeoffset);
		str[0] = 0;
		break; // don't need to free
	case TOK_STORAGE_INDIRECT_SINGLE:
		pstr = (char**)((intptr_t)structptr + ptcc->storeoffset);

		if (field_type & TOK_ESTRING)
		{
			if (*pstr && !isSharedMemory(*pstr)) 
			{
				estrDestroy(pstr);
			}
		}
		else if (! (field_type & TOK_POOL_STRING) )
		{
			if (*pstr) StructFreeString(*pstr);
		}

		*pstr = 0;

		break;
	case TOK_STORAGE_INDIRECT_EARRAY:
		parray = (char***)((intptr_t)structptr + ptcc->storeoffset);
		
		if (index >= eaSize(parray) || index < 0)
		{
			SETRESULT(PARSERESULT_ERROR);
			return;
		}
		
		if ((*parray)[index])
		{	
			if (field_type & TOK_ESTRING)
			{
				estrDestroy(&((*parray)[index]));
			}
			else if (!(field_type & TOK_POOL_STRING))
			{
				StructFreeString((*parray)[index]);
			}
		}
		(*parray)[index] = 0;
		break;
	};
}

// clear the target string WITHOUT freeing it
void TokenStoreClearString(ParseTable tpi[], int column, void* structptr, int index, TextParserResult *result)
{
	TokenStoreClearString_inline(tpi, &tpi[column], column, structptr, index, result);
}

char* TokenStoreSetString(ParseTable tpi[], int column, void* structptr, int index, const char* str, CustomMemoryAllocator memAllocator, void* customData, TextParserResult *result, TokenizerHandle tok)
{
	char* newstr = 0;
	char** pstr = 0;
	char*** parray;
	ParseTable *ptcc = &tpi[column];
	StructTypeField field_type = ptcc->type;
	U32 storage = TokenStoreGetStorageType(field_type);
	
	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_INDIRECT_SINGLE | TOK_STORAGE_INDIRECT_EARRAY);

	if (storage == TOK_STORAGE_DIRECT_SINGLE)
	{
		if (!str) str = "";

		// TODO - different error strategy?
		if ((int)strlen(str) >= ptcc->param)
		{
			if (tok) 
				TokenizerErrorf(tok, "String parameter is too long %s (expected < %d, found %d)\n", str, ptcc->param, strlen(str));
			SETRESULT(PARSERESULT_ERROR);
			return 0;
		}

		newstr = (char*)((intptr_t)structptr + ptcc->storeoffset);
		strcpy_s(newstr, ptcc->param, str);
		return newstr;
	}

	// get string pointer
	if (storage == TOK_STORAGE_INDIRECT_EARRAY)
	{
		parray = (char***)((intptr_t)structptr + ptcc->storeoffset);
		if (index > eaSize(parray) || index < 0)
		{
			if (tok)
				TokenizerErrorf(tok, "Invalid array index");
			SETRESULT(PARSERESULT_ERROR);
			return 0;
		}
		if (index < eaSize(parray))
			pstr = &(*parray)[index];
		else
		{
			ParseTableInfo *info = ParserGetTableInfo(tpi);
			if (info && info->name) {
				index = eaPush_dbg(parray, 0, info->name, LINENUM_FOR_EARRAYS);
			} else {
				index = eaPush(parray, 0);
			}
			pstr = &(*parray)[index];
		}
	}
	else if (storage == TOK_STORAGE_INDIRECT_SINGLE)
	{
		pstr = (char**)((intptr_t)structptr + ptcc->storeoffset);
	}

	assert(pstr);

	// hide difference between null and empty strings - 
	if (str && !str[0]) str = NULL;

	// if not fixed, choose allocation method
	if (!str)
	{
		newstr = NULL;
		if (*pstr)
		{		
			if (field_type & TOK_ESTRING)
			{
				estrDestroy(pstr);
			}
			else if (!(field_type & TOK_POOL_STRING)) // already a string allocated
			{
				StructFreeString(*pstr);
			}
		}
	}
	else if (memAllocator && !(field_type & TOK_POOL_STRING && stringCacheSharingEnabled()))
	{	
		// If string cache sharing enabled, keep as shared
		int len = (int)strlen(str) + 1;
		newstr = memAllocator(customData, len);
		if (newstr) strcpy_s(newstr, len, str);
	}
	else if (field_type & TOK_POOL_STRING)
	{
		ParseTableInfo *info = ParserGetTableInfo(tpi);
		if ((field_type & TOK_GLOBAL_NAME) &&
			g_texture_name_fixup &&
			stricmp(ptcc->subtable, "Texture")==0)
		{
			char buf[MAX_PATH];
			g_texture_name_fixup(str, SAFESTR(buf));
			if (info && info->name) {
				newstr = (char*)allocAddString_dbg(buf, false, true, false, info->name, LINENUM_FOR_STRINGS);
			} else {
				newstr = (char*)allocAddFilename(buf);
			}
		}
		else if (TOK_GET_TYPE(field_type) == TOK_FILENAME_X ||
			TOK_GET_TYPE(field_type) == TOK_CURRENTFILE_X)
		{
			if (info && info->name) {
				newstr = (char*)allocAddString_dbg(str, false, true, false, info->name, LINENUM_FOR_STRINGS);
			} else {
				newstr = (char*)allocAddFilename(str);
			}
		}
		else
		{				
			if (info && info->name) {
				newstr = (char*)allocAddString_dbg(str, true, false, false, info->name, LINENUM_FOR_STRINGS);
			} else {
				newstr = (char*)allocAddString(str);
			}
		}
	}
	else if (field_type & TOK_ESTRING)
	{
		ParseTableInfo *info = ParserGetTableInfo(tpi);
		if (info && info->name) {
			estrCopy2_dbg(pstr, str, info->name, LINENUM_FOR_STRINGS);
		} else {
			estrCopy2(pstr, str);
		}
		return *pstr;
	}
	else
	{
		int newlen = (int)strlen(str);
		ParseTableInfo *info = ParserGetTableInfo(tpi);
		if (*pstr) 
		{
			if (newlen == (int)strlen(*pstr)) 
			{
				strcpy_s(*pstr, newlen + 1, str); // Reuse it if it's the same size buffer
				return *pstr;
			}
			else
			{
				StructFreeString(*pstr);
			}
		}		
		if (info && info->name) {
			newstr = StructAllocStringLen_dbg(str, newlen, info->name, LINENUM_FOR_STRINGS);
		} else {
			newstr = StructAllocStringLen(str, newlen);
		}
	}

	*pstr = newstr;

	return newstr;
}

const char* TokenStoreGetString(ParseTable tpi[], int column, const void* structptr, int index, TextParserResult *result)
{
	return TokenStoreGetString_inline(tpi,&tpi[column],column,structptr,index,result);
}

size_t TokenStoreGetStringMemUsage(ParseTable tpi[], int column, const void* structptr, int index, bool bAbsoluteUsage, TextParserResult *result)
{
	char** pstr;
	char*** parray;
	ParseTable *ptcc = &tpi[column];
	StructTypeField field_type = ptcc->type;
	U32 storage = TokenStoreGetStorageType(field_type);
	
	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_INDIRECT_SINGLE | TOK_STORAGE_INDIRECT_EARRAY);

	switch (storage) {
	case TOK_STORAGE_DIRECT_SINGLE:
		return 0; // string directly stored in struct
	case TOK_STORAGE_INDIRECT_EARRAY:
		parray = (char***)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= eaSize(parray) || index < 0)
		{			
			SETRESULT(PARSERESULT_ERROR);
			return 0;
		}
		if (field_type & TOK_POOL_STRING && (stringCacheSharingEnabled() || bAbsoluteUsage))
			return 0; // string is pooled and shared
		return (*parray)[index] ? strlen((*parray)[index])+1 : 0;
	case TOK_STORAGE_INDIRECT_SINGLE:
		if (field_type & TOK_POOL_STRING && (stringCacheSharingEnabled() || bAbsoluteUsage))
			return 0; // string is pooled and shared
		pstr = (char**)((intptr_t)structptr + ptcc->storeoffset);

		if (field_type & TOK_ESTRING && bAbsoluteUsage)
		{
			return estrAllocSize(pstr);
		}
			
		if (*pstr)
			return strlen(*pstr)+1;
		return 0;
	};
	return 0;
}

void TokenStoreSetInt(ParseTable tpi[], int column, void* structptr, int index, int value, TextParserResult *result, TokenizerHandle tok)
{
	TokenStoreSetInt_inline(tpi, &tpi[column], column, structptr, index, value, result, tok);
}


int TokenStoreGetInt(ParseTable tpi[], int column, const void* structptr, int index, TextParserResult *result)
{
	return TokenStoreGetInt_inline(tpi,&tpi[column],column,structptr,index,result);
}

void TokenStoreSetInt64(ParseTable tpi[], int column, void* structptr, int index, S64 value, TextParserResult *result, TokenizerHandle tok)
{
	TokenStoreSetInt64_inline(tpi, &tpi[column], column, structptr, index, value, result, tok);
}

S64 TokenStoreGetInt64(ParseTable tpi[], int column, const void* structptr, int index, TextParserResult *result)
{
	return TokenStoreGetInt64_inline(tpi, &tpi[column], column, structptr, index, result);
}

void TokenStoreSetInt16(ParseTable tpi[], int column, void* structptr, int index, S16 value, TextParserResult *result, TokenizerHandle tok)
{
	TokenStoreSetInt16_inline(tpi, &tpi[column], column, structptr, index, value, result, tok);
}

S16 TokenStoreGetInt16(ParseTable tpi[], int column, const void* structptr, int index, TextParserResult *result)
{
	return TokenStoreGetInt16_inline(tpi, &tpi[column], column, structptr, index, result);
}

void TokenStoreSetU8(ParseTable tpi[], int column, void* structptr, int index, U8 value, TextParserResult *result, TokenizerHandle tok)
{
	TokenStoreSetU8_inline(tpi, &tpi[column], column, structptr, index, value, result, tok);
}

U8 TokenStoreGetU8(ParseTable tpi[], int column, const void* structptr, int index, TextParserResult *result)
{
	return TokenStoreGetU8_inline(tpi, &tpi[column], column, structptr, index, result);
}

void TokenStoreSetBit(ParseTable tpi[], int column, void* structptr, int index, int value, TextParserResult *result, TokenizerHandle tok)
{
	TokenStoreSetBit_inline(tpi, &tpi[column], column, structptr, index, value, result, tok);
}

U32 TokenStoreGetBit(ParseTable tpi[], int column, const void* structptr, int index, TextParserResult *result)
{
	return TokenStoreGetBit_inline(tpi, &tpi[column], column, structptr, index, result);
}

void TokenStoreSetIntAuto(ParseTable tpi[], int column, void* structptr, int index, S64 value, TextParserResult *result, TokenizerHandle tok)
{
	ParseTable *ptcc = &tpi[column];

	switch (TOK_GET_TYPE(ptcc->type))
	{
		xcase TOK_INT64_X:
			TokenStoreSetInt64_inline(tpi, ptcc, column, structptr, index, value, result, tok);
		xcase TOK_INT_X:
		case TOK_LINENUM_X:
		case TOK_TIMESTAMP_X:
			devassertmsg(((int)value) == value, "Integer truncation");
			TokenStoreSetInt_inline(tpi, ptcc, column, structptr, index, (int)value, result, tok);
		xcase TOK_INT16_X:
			devassertmsg(((S16)value) == value, "Integer truncation");
			TokenStoreSetInt16_inline(tpi, ptcc, column, structptr, index, (S16)value, result, tok);
		xcase TOK_U8_X:
			devassertmsg(((U8)value) == value, "Integer truncation");
			TokenStoreSetU8_inline(tpi, ptcc, column, structptr, index, (U8)value, result, tok);
		xcase TOK_BIT:
			{
				U32 assignedValue = 0;
				TokenStoreSetBit_inline(tpi, ptcc, column, structptr, index, (int)value, result, tok);
				assignedValue = TokenStoreGetBit_inline(tpi, ptcc, column, structptr, index, result);
				devassertmsg(assignedValue == value, "Bitfield truncation");
			}
		xdefault:
			assertmsg(0, "Invalid type passed to TokenStoreSetIntAuto");
	}
}

S64 TokenStoreGetIntAuto(ParseTable tpi[], int column, const void* structptr, int index, TextParserResult *result)
{
	ParseTable *ptcc = &tpi[column];
	switch (TOK_GET_TYPE(ptcc->type))
	{
		xcase TOK_INT64_X:
			return TokenStoreGetInt64_inline(tpi, ptcc, column, structptr, index, result);
		xcase TOK_INT_X:
			return TokenStoreGetInt_inline(tpi, ptcc, column, structptr, index, result);
		xcase TOK_LINENUM_X:
		case TOK_TIMESTAMP_X:
			return TokenStoreGetInt_inline(tpi, ptcc, column, structptr, index, result);
		xcase TOK_INT16_X:
			return TokenStoreGetInt16_inline(tpi, ptcc, column, structptr, index, result);
		xcase TOK_U8_X:
			return TokenStoreGetU8_inline(tpi, ptcc, column, structptr, index, result);
		xcase TOK_BIT:
			return TokenStoreGetBit_inline(tpi, ptcc, column, structptr, index, result);
		xdefault:
			assertmsg(0, "Invalid type passed to TokenStoreSetIntAuto");
			return 0;
	}
}


void TokenStoreSetF32(ParseTable tpi[], int column, void* structptr, int index, F32 value, TextParserResult *result, TokenizerHandle tok)
{
	TokenStoreSetF32_inline(tpi, &tpi[column], column, structptr, index, value, result, tok);
}

F32 TokenStoreGetF32(ParseTable tpi[], int column, const void* structptr, int index, TextParserResult *result)
{
	return TokenStoreGetF32_inline(tpi,&tpi[column],column,structptr,index,result);
}

void* TokenStoreAllocCharged(ParseTable tpi[], int column, void* structptr, int index, U32 size, CustomMemoryAllocator memAllocator, void* customData, TextParserResult *result, ParseTable *pTPIToCharge)
{
	ParseTable *ptcc = &tpi[column];
	U32 storage = TokenStoreGetStorageType(ptcc->type);
	void* p;
	void** pp;
	void*** eap;
	void **bea;

	if (!pTPIToCharge)
		pTPIToCharge = ptcc->subtable;

	if (!pTPIToCharge)
		pTPIToCharge = tpi;

	if (pTPIToCharge == parse_NullStruct && ptcc->subtable == parse_NullStruct)
	{
		devassertmsgf(0, "Trying to allocate a substruct that is LATEBINDed, something is not getting linked in. Presumably it's the field named %s in struct %s",
			ptcc->name, ParserGetTableName(tpi));
	}

	devassertmsg(ParserGetTableInfo(pTPIToCharge) && ParserGetTableInfo(pTPIToCharge)->name, "TokenStoreAllocCharged called on a struct that wasn't initialized with ParserSetTableInfo!");

	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_INDIRECT_SINGLE | TOK_STORAGE_INDIRECT_EARRAY | TOK_STORAGE_DIRECT_EARRAY);

	switch (storage) {
	case TOK_STORAGE_DIRECT_SINGLE: // should be pointing at correct amount of memory already
		devassert((U32)ptcc->param == size && "internal textparser error");
		return (void*)((intptr_t)structptr + ptcc->storeoffset);
	case TOK_STORAGE_INDIRECT_SINGLE:
		pp = (void**)((intptr_t)structptr + ptcc->storeoffset);
		if (memAllocator)
		{
			*pp = memAllocator(customData, size);
		}
		else
		{
			*pp = StructAllocRawCharged(size, pTPIToCharge);
		}

		return *pp;

	case TOK_STORAGE_DIRECT_EARRAY:
		bea = (void**)((intptr_t)structptr + ptcc->storeoffset);
		if (index > beaSize(bea) || index < 0)
		{
			SETRESULT(PARSERESULT_ERROR);
			return 0;
		}

		if (index == beaSize(bea))
		{
			ParseTableInfo *info = ParserGetTableInfo(pTPIToCharge);

			if (info && info->name)
			{
				return beaPushEmptyEx(bea, size, ptcc->subtable, BEAPUSHFLAG_NO_STRUCT_INIT, memAllocator, customData, info->name, LINENUM_FOR_EARRAYS);
			}
			else
			{
				return beaPushEmptyEx(bea, size, ptcc->subtable, BEAPUSHFLAG_NO_STRUCT_INIT, memAllocator, customData MEM_DBG_PARMS_INIT);
			}
		}

		return ((char*)(*bea)) + index * size;



	case TOK_STORAGE_INDIRECT_EARRAY:
		eap = (void***)((intptr_t)structptr + ptcc->storeoffset);

		if (memAllocator)
		{
			p = memAllocator(customData, size);
		}
		else
		{
			p = StructAllocRawCharged(size, pTPIToCharge);
		}

		if (index > eaSize(eap) || index < 0)
		{
			SETRESULT(PARSERESULT_ERROR);
			return 0;
		}

		if (index < eaSize(eap))
		{
			(*eap)[index] = p;
		}
		else
		{
			ParseTableInfo *info = ParserGetTableInfo(pTPIToCharge);

			if (!*eap && ParserColumnIsIndexedEArray(tpi, column, NULL))
			{												
				eaIndexedEnable_dbg(eap,ptcc->subtable, info && info->name?info->name:__FILE__, info && info->name?LINENUM_FOR_EARRAYS:__LINE__);
			}

			if (info && info->name) {
				eaInsert_dbg(eap,p,eaSize(eap),true, info->name, LINENUM_FOR_EARRAYS);
			} else {
				eaInsert_dbg(eap,p,eaSize(eap),true MEM_DBG_PARMS_INIT);
			}
		}

		return p;
	};
	return NULL;
}

void TokenStoreFree(ParseTable tpi[], int column, void* structptr, int index, TextParserResult *result)
{
	ParseTable *ptcc = &tpi[column];
	StructTypeField field_type = ptcc->type;
	U32 storage = TokenStoreGetStorageType(field_type);
	void** pp;
	void*** eap;

	//if there's a subtable, TokenStoreFreeWithTPI should be called instead
	assert(!TOK_HAS_SUBTABLE(field_type));
	

	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_INDIRECT_SINGLE | TOK_STORAGE_INDIRECT_EARRAY);
	switch (storage) {
	case TOK_STORAGE_DIRECT_SINGLE: return; // do nothing
	case TOK_STORAGE_INDIRECT_SINGLE:
		pp = (void**)((intptr_t)structptr + ptcc->storeoffset);
		if (*pp) 
			_StructFree_internal(NULL, *pp);
		*pp = NULL;
		return;
	case TOK_STORAGE_INDIRECT_EARRAY:
		eap = (void***)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= eaSize(eap) || index < 0)
		{			
			SETRESULT(PARSERESULT_ERROR);
			return;
		}
		if ((*eap)[index])
			_StructFree_internal(NULL, (*eap)[index]);
		(*eap)[index] = 0;
		return;
	};
}


void TokenStoreFreeWithTPI(ParseTable tpi[], int column, void* structptr, int index, TextParserResult *result, ParseTable *pTPIOfFreedBlock)
{
	ParseTable *ptcc = &tpi[column];
	U32 storage = TokenStoreGetStorageType(ptcc->type);
	void** pp;
	void*** eap;


	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_INDIRECT_SINGLE | TOK_STORAGE_INDIRECT_EARRAY | TOK_STORAGE_DIRECT_EARRAY);
	switch (storage) {
	case TOK_STORAGE_DIRECT_SINGLE: 
	case TOK_STORAGE_DIRECT_EARRAY: return;// do nothing

	case TOK_STORAGE_INDIRECT_SINGLE:
		pp = (void**)((intptr_t)structptr + ptcc->storeoffset);
		if (*pp) 
			_StructFree_internal(pTPIOfFreedBlock, *pp);
		*pp = NULL;
		return;
	case TOK_STORAGE_INDIRECT_EARRAY:
		eap = (void***)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= eaSize(eap) || index < 0)
		{			
			SETRESULT(PARSERESULT_ERROR);
			return;
		}
		if ((*eap)[index])
			_StructFree_internal(pTPIOfFreedBlock, (*eap)[index]);
		(*eap)[index] = 0;
		return;
	};
}


// get the count field used by TOK_POINTER or TOK_USEDFIELD
int* TokenStoreGetCountField(ParseTable tpi[], int column, const void* structptr, TextParserResult *result)
{
	return TokenStoreGetCountField_inline(tpi, &tpi[column], column, structptr, result);
}

int usedfield_GetAllocSize(ParseTable* tpi)
{
	return 4*((ParserGetTableNumColumns(tpi) >> 5) + 1); // rounded to U32's
}

void TokenStoreAddUsedField(ParseTable tpi[], int column, void *structptr)
{
	int* count = TokenStoreGetCountField_inline(tpi, &tpi[column], column, structptr, NULL);
	U32 size = usedfield_GetAllocSize(tpi);
	if (!count)
	{
		Errorf("Invalid parameter in TOK_USEDFIELD column");
		return;
	}
	TokenStoreAlloc(tpi, column, structptr, 0, size, NULL, NULL, NULL);
	*count = size;
}

void TokenStoreSetPointer(ParseTable tpi[], int column, void* structptr, int index, void* ptr, TextParserResult *result)
{
	TokenStoreSetPointer_inline(tpi, &tpi[column], column, structptr, index, ptr, result);
}

void* TokenStoreGetPointer(ParseTable tpi[], int column, const void* structptr, int index, TextParserResult *result)
{
	return TokenStoreGetPointer_inline(tpi,&tpi[column],column,structptr,index,result);
}

void* TokenStoreRemovePointer(ParseTable tpi[], int column, void* structptr, void* ptr, TextParserResult *result)
{
	void*** ea;
	TS_REQUIRE(TOK_STORAGE_INDIRECT_EARRAY);
	ea = (void***)((intptr_t)structptr + tpi[column].storeoffset);
	eaFindAndRemove(ea, ptr);
	return ptr;
}


void TokenStoreSetRef(ParseTable tpi[], int column, void* structptr, int index, const char* str, TextParserResult *result, TokenizerHandle tok)
{
	void** pp;
	ParseTable *ptcc = &tpi[column];
	TS_REQUIRE(TOK_STORAGE_INDIRECT_SINGLE);

	pp = (void**)((intptr_t)structptr + ptcc->storeoffset);

	if (IS_CORRECT_THREAD())
	{
		char buffer[200];
		buffer[0] = 0;
		if(g_bDebugTokenStoreReferenceActions && GetCurTransactionName()){
			sprintf(buffer, "%s:%s", __FUNCTION__, GetCurTransactionName());
		}
		RefSystem_SetHandleFromStringWithReason((char*)ptcc->subtable,
												str,
												pp,
												buffer[0] ? buffer : __FUNCTION__);
	}
	else
	{
		TokenStoreQueuedReferenceLookup* lookup;
		EnterCriticalSection(&gDynQueuedReferenceLookups.cs);
		lookup = dynArrayAddStruct_no_memset(gDynQueuedReferenceLookups.data, gDynQueuedReferenceLookups.count, gDynQueuedReferenceLookups.size);
		lookup->operation = TSQROP_SET_REF;
		lookup->dict = (char*)ptcc->subtable;
		lookup->param1 = (void**)strdup(str);
		lookup->param2 = pp;
		*pp = THREAD_REFERENCE_UNINIT;
		LeaveCriticalSection(&gDynQueuedReferenceLookups.cs);
	}
	
}

void TokenStoreClearRef(ParseTable tpi[], int column, void* structptr, int index, bool destroy_reference, TextParserResult *result)
{
	void** pp;
	ParseTable *ptcc = &tpi[column];
	U32 storage = TokenStoreGetStorageType(ptcc->type);
	TS_REQUIRE(TOK_STORAGE_INDIRECT_SINGLE);

	pp = (void**)((intptr_t)structptr + ptcc->storeoffset);

	if (*pp == REFERENCE_DESTROYED) //dont try and look at destroyed references. this allows us to clear all the references and later free from a background thread
		return;

#ifdef TOKENSTORE_DETAILED_TIMERS
	{
		ParseTableInfo *info2;
		PERFINFO_AUTO_START_FUNC();
		info2 = ParserGetTableInfo(tpi);
		PERFINFO_AUTO_START_STATIC(info2->name, &info2->piRefClear, 1);
	}
#endif

	if (IS_CORRECT_THREAD())
	{
		char buffer[200];
		buffer[0] = 0;
		assert(*pp != THREAD_REFERENCE_UNINIT);
		if(g_bDebugTokenStoreReferenceActions && GetCurTransactionName()){
			sprintf(buffer, "%s:%s", __FUNCTION__, GetCurTransactionName());
		}
		RefSystem_RemoveHandleWithReason(pp, buffer[0] ? buffer : __FUNCTION__);
		if (destroy_reference) //if this is set we set the reference to a special value to mark it as destroyed to prevent it from getting cleared again
			*pp = REFERENCE_DESTROYED;
	}
	else
	{
		TokenStoreQueuedReferenceLookup* lookup;
		EnterCriticalSection(&gDynQueuedReferenceLookups.cs);
		lookup = dynArrayAddStruct_no_memset(gDynQueuedReferenceLookups.data, gDynQueuedReferenceLookups.count, gDynQueuedReferenceLookups.size);
		lookup->operation = destroy_reference ? TSQROP_DESTROY_REF : TSQROP_CLEAR_REF;
		lookup->param1 = pp;
		lookup->param2 = NULL;
		LeaveCriticalSection(&gDynQueuedReferenceLookups.cs);
	}
	
#ifdef TOKENSTORE_DETAILED_TIMERS
	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
#endif
}

void **TokenStoreGetRefHandlePointer(ParseTable tpi[], int column, const void *structPtr, int index, TextParserResult *result)
{
	return TokenStoreGetRefHandlePointer_inline(tpi, &tpi[column], column, structPtr, index, result);
}

void TokenStoreCopyRef(ParseTable tpi[], int column, void* dest, void* src, int index, TextParserResult *result)
{
	void **ppdest, **ppsrc;
	size_t offset = tpi[column].storeoffset;
	TS_REQUIRE(TOK_STORAGE_INDIRECT_SINGLE);
	
	ppdest = (void**)((intptr_t)dest + offset);
	ppsrc = (void**)((intptr_t)src + offset);

	if (IS_CORRECT_THREAD())
	{
		RefSystem_CopyHandle(ppdest, ppsrc); // ok for ppsrc not to be ref?
	}
	else
	{
		TokenStoreQueuedReferenceLookup* lookup;
		EnterCriticalSection(&gDynQueuedReferenceLookups.cs);
		lookup = dynArrayAddStruct_no_memset(gDynQueuedReferenceLookups.data, gDynQueuedReferenceLookups.count, gDynQueuedReferenceLookups.size);
		lookup->operation = TSQROP_COPY_REF;
		lookup->param1 = ppdest;
		lookup->param2 = ppsrc;
		*ppdest = THREAD_REFERENCE_UNINIT;
		LeaveCriticalSection(&gDynQueuedReferenceLookups.cs);
	}
}

void TokenStoreCopyRef2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest, TextParserResult *result)
{
	void **ppdest, **ppsrc;
	devassert(TokenStoreIsCompatible(src_tpi[src_column].type, TOK_STORAGE_INDIRECT_SINGLE));
	devassert(TokenStoreIsCompatible(dest_tpi[dest_column].type, TOK_STORAGE_INDIRECT_SINGLE));

	ppdest = (void**)((intptr_t)dest + dest_tpi[dest_column].storeoffset);
	ppsrc = (void**)((intptr_t)src + src_tpi[src_column].storeoffset);

	if (IS_CORRECT_THREAD())
	{
		RefSystem_CopyHandle(ppdest, ppsrc); // ok for ppsrc not to be ref?
	}
	else
	{
		TokenStoreQueuedReferenceLookup* lookup;
		EnterCriticalSection(&gDynQueuedReferenceLookups.cs);
		lookup = dynArrayAddStruct_no_memset(gDynQueuedReferenceLookups.data, gDynQueuedReferenceLookups.count, gDynQueuedReferenceLookups.size);
		lookup->operation = TSQROP_COPY_REF;
		lookup->param1 = ppdest;
		lookup->param2 = ppsrc;
		*ppdest = THREAD_REFERENCE_UNINIT;
		LeaveCriticalSection(&gDynQueuedReferenceLookups.cs);
	}
}

const char *TokenStoreGetRefString(ParseTable tpi[], int column, const void* structptr, int index, TextParserResult *result)
{
	ASSERT_CORRECT_THREAD_IF_ENABLED();
	return TokenStoreGetRefString_inline(tpi, &tpi[column], column, structptr, index, result);
}

void TokenStoreWaitForQueuedReferenceLookupsToFlush(void)
{
	bool bDone = false;

	while (1)
	{
		EnterCriticalSection(&gDynQueuedReferenceLookups.cs);
		bDone = (gDynQueuedReferenceLookups.count == 0);
		LeaveCriticalSection(&gDynQueuedReferenceLookups.cs);

		if (bDone)
		{
			return;
		}

		Sleep(1);
		
	}
}

void TokenStoreFlushQueuedReferenceLookups()
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	ASSERT_CORRECT_THREAD();

	EnterCriticalSection(&gDynQueuedReferenceLookups.cs);
	for (i = 0; i < gDynQueuedReferenceLookups.count; i++)
	{
		TokenStoreQueuedReferenceLookup* cur = gDynQueuedReferenceLookups.data + i;
		switch(cur->operation)
		{
			xcase TSQROP_SET_REF:
				assert(*cur->param2 == THREAD_REFERENCE_UNINIT);
				RefSystem_SetHandleFromString(cur->dict, (char*)cur->param1, cur->param2);
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '**cur[8]'"
				free((char*)cur->param1);
			xcase TSQROP_CLEAR_REF:
				assert(*cur->param1 != THREAD_REFERENCE_UNINIT);
				RefSystem_RemoveHandle(cur->param1);
			xcase TSQROP_DESTROY_REF:
				assert(*cur->param1 != THREAD_REFERENCE_UNINIT);
				RefSystem_RemoveHandle(cur->param1);
				*cur->param1 = REFERENCE_DESTROYED;
			xcase TSQROP_COPY_REF:
				assert(*cur->param1 == THREAD_REFERENCE_UNINIT);
				RefSystem_CopyHandle(cur->param1, cur->param2);
			xdefault:
				assertmsg(0, "Invalid queued reference operation");
		}
	}
	gDynQueuedReferenceLookups.count = 0;
	LeaveCriticalSection(&gDynQueuedReferenceLookups.cs);

	PERFINFO_AUTO_STOP_FUNC();
}


void*** TokenStoreGetEArray(ParseTable tpi[], int column, const void* structptr, TextParserResult *result)
{
	return TokenStoreGetEArray_inline(tpi, &tpi[column], column, structptr, result);
}

int** TokenStoreGetEArrayInt(ParseTable tpi[], int column, const void* structptr, TextParserResult *result)
{
	return TokenStoreGetEArrayInt_inline(tpi, &tpi[column], column, structptr, result);
}

F32** TokenStoreGetEArrayF32(ParseTable tpi[], int column, const void* structptr, TextParserResult *result)
{
	return TokenStoreGetEArrayF32_inline(tpi, &tpi[column], column, structptr, result);
}

void** TokenStoreGetBlockEarray(ParseTable tpi[], int column, const void* structptr, TextParserResult *result)
{
	TS_REQUIRE(TOK_STORAGE_DIRECT_EARRAY);
	devassert(TokenStoreEarrayIsBlockArray(tpi[column].type));
	return (void**)((intptr_t)structptr + tpi[column].storeoffset);
}

size_t TokenStoreGetEArrayMemUsage(ParseTable tpi[], int column, const void* structptr, bool bAbsoluteUsage, TextParserResult *result)
{
	int** ea32;
	void*** ea;
	void **ppBlockEarray;
	ParseTable *ptcc = &tpi[column];
	U32 storage = TokenStoreGetStorageType(ptcc->type);
	TS_REQUIRE(TOK_STORAGE_DIRECT_EARRAY | TOK_STORAGE_INDIRECT_EARRAY);

	switch (storage) {
	case TOK_STORAGE_DIRECT_EARRAY: // int** or F32** or blockEArray
		if (TokenStoreEarrayIsBlockArray(ptcc->type))
		{
			ppBlockEarray = (void**)((intptr_t)structptr + ptcc->storeoffset);
			return beaMemUsage(ppBlockEarray, bAbsoluteUsage);
		}
		ea32 = (int**)((intptr_t)structptr + ptcc->storeoffset);
		return ea32MemUsage(ea32, bAbsoluteUsage);
	case TOK_STORAGE_INDIRECT_EARRAY:
		ea = (void***)((intptr_t)structptr + ptcc->storeoffset);
		return eaMemUsage(ea, bAbsoluteUsage);
	}
	return 0;
}

void TokenStoreGetBlockSizeOrTPIForBlockEarray(ParseTable tpi[], int column, int *pOutBlockSize, ParseTable **ppOutParseTable)
{
	ParseTable *ptcc = &tpi[column];
	StructTypeField field_type = TOK_GET_TYPE(ptcc->type);

	TS_REQUIRE(TOK_STORAGE_DIRECT_EARRAY);
	if (field_type == TOK_MULTIVAL_X)
	{
		*pOutBlockSize = sizeof(MultiVal);
		*ppOutParseTable = NULL;
		return;
	}

	if (field_type == TOK_STRUCT_X)
	{
		*ppOutParseTable = ptcc->subtable;
		*pOutBlockSize = ptcc->param;
		return;
	}

	assertmsg(0, "Unsupporte block earray type");
}

void TokenStoreSetEArraySize(ParseTable tpi[], int column, void* structptr, int size, TextParserResult *result)
{
	int** ea32;
	void*** ea;
	void **ppBlockEarray;
	ParseTable *ptcc = &tpi[column];
	StructTypeField field_type = ptcc->type;
	U32 storage = TokenStoreGetStorageType(field_type);
	ParseTableInfo *info = ParserGetTableInfo(tpi);
	TS_REQUIRE(TOK_STORAGE_DIRECT_EARRAY | TOK_STORAGE_INDIRECT_EARRAY);

	if(!structptr)
		return;

	switch (storage) {
	case TOK_STORAGE_DIRECT_EARRAY: // int** or F32** or blockearray
		if (TokenStoreEarrayIsBlockArray(field_type))
		{
			int iBlockSize;
			ParseTable *pBlockTPI;

			ppBlockEarray = (void**)((intptr_t)structptr + ptcc->storeoffset);
			TokenStoreGetBlockSizeOrTPIForBlockEarray(tpi, column, &iBlockSize, &pBlockTPI);

			if (info && info->name)
			{
				beaSetSizeEx(ppBlockEarray, iBlockSize, pBlockTPI, size, 0, NULL, NULL, false, info->name, LINENUM_FOR_EARRAYS);
			}
			else
			{
				beaSetSizeEx(ppBlockEarray, iBlockSize, pBlockTPI, size, 0, NULL, NULL, false, __FILE__, __LINE__);
			}
			break;
		}

		ea32 = (int**)((intptr_t)structptr + ptcc->storeoffset);
		if (info && info->name)
		{
			ea32SetSize_dbg(ea32, size, info->name, LINENUM_FOR_EARRAYS);
		}
		else
		{
			ea32SetSize_dbg(ea32, size, __FILE__, __LINE__);
		}
		break;
	case TOK_STORAGE_INDIRECT_EARRAY:
		ea = (void***)((intptr_t)structptr + ptcc->storeoffset);
		if (info && info->name)
		{
			eaSetSize_dbg(ea, size, info->name, LINENUM_FOR_EARRAYS);
		}
		else
		{
			eaSetSize_dbg(ea, size, __FILE__, __LINE__);
		}
		break;
	}
}


void TokenStoreSetEArrayCapacity(ParseTable tpi[], int column, void* structptr, int size)
{
	int** ea32;
	void*** ea;
	void **ppBlockEarray;
	ParseTable *ptcc = &tpi[column];
	StructTypeField field_type = ptcc->type;
	U32 storage = TokenStoreGetStorageType(field_type);
	ParseTableInfo *info = ParserGetTableInfo(tpi);
	TS_REQUIRE(TOK_STORAGE_DIRECT_EARRAY | TOK_STORAGE_INDIRECT_EARRAY);

	switch (storage) {
	case TOK_STORAGE_DIRECT_EARRAY: // int** or F32** or blockearray
		if (TokenStoreEarrayIsBlockArray(field_type))
		{
			int iBlockSize;
			ParseTable *pBlockTPI;

			ppBlockEarray = (void**)((intptr_t)structptr + ptcc->storeoffset);
			TokenStoreGetBlockSizeOrTPIForBlockEarray(tpi, column, &iBlockSize, &pBlockTPI);

			if (info && info->name)
			{
				beaSetCapacityEx(ppBlockEarray, iBlockSize, pBlockTPI, size, 0, NULL, NULL, info->name, LINENUM_FOR_EARRAYS);
			}
			else
			{
				beaSetCapacityEx(ppBlockEarray, iBlockSize, pBlockTPI, size, 0, NULL, NULL, __FILE__, __LINE__);
			}
			break;
		}

		ea32 = (int**)((intptr_t)structptr + ptcc->storeoffset);
		if (info && info->name)
		{
			ea32SetCapacity_dbg(ea32, size, info->name, LINENUM_FOR_EARRAYS);
		}
		else
		{
			ea32SetCapacity_dbg(ea32, size, __FILE__, __LINE__);
		}
		break;
	case TOK_STORAGE_INDIRECT_EARRAY:
		ea = (void***)((intptr_t)structptr + ptcc->storeoffset);
		if (info && info->name)
		{
			eaSetCapacity_dbg(ea, size, info->name, LINENUM_FOR_EARRAYS);
		}
		else
		{
			eaSetCapacity_dbg(ea, size, __FILE__, __LINE__);
		}
		break;
	}
}

int TokenStoreGetFixedArraySize(ParseTable tpi[], int column)
{
	return TokenStoreGetFixedArraySize_inline(tpi, &tpi[column], column);
}

void TokenStoreRemoveElement(ParseTable tpi[], int column, void* structptr, int index, TextParserResult *result)
{
	int** ea32;
	void*** ea;
	ParseTable *ptcc = &tpi[column];
	StructTypeField field_type = ptcc->type;
	U32 storage = TokenStoreGetStorageType(field_type);

	switch (storage) {
	case TOK_STORAGE_INDIRECT_SINGLE: // fall
	case TOK_STORAGE_DIRECT_SINGLE: // fall
	case TOK_STORAGE_INDIRECT_FIXEDARRAY: // fall
	case TOK_STORAGE_DIRECT_FIXEDARRAY:
		return;
	case TOK_STORAGE_DIRECT_EARRAY: // int** or F32**
		if (TokenStoreEarrayIsBlockArray(field_type))
		{
			int iBlockSize;
			ParseTable *pBlockTPI;
			void **ppBlockEarray;

			TokenStoreGetBlockSizeOrTPIForBlockEarray(tpi, column, &iBlockSize, &pBlockTPI);
			ppBlockEarray = (void**)((intptr_t)structptr + ptcc->storeoffset);
			if (index >= beaSize(ppBlockEarray) || index < 0)
			{			
				SETRESULT(PARSERESULT_ERROR);
				return;
			}

			beaRemoveEx(ppBlockEarray, iBlockSize, pBlockTPI, index);
			break;
		}

		ea32 = (int**)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= eaiSize(ea32) || index < 0)
		{			
			SETRESULT(PARSERESULT_ERROR);
			return;
		}
		ea32Remove(ea32, index);
		break;
	case TOK_STORAGE_INDIRECT_EARRAY:
		ea = (void***)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= eaSize(ea) || index < 0)
		{
			SETRESULT(PARSERESULT_ERROR);
			return;
		}
		eaRemove(ea, index);
		break;
	}
}

void TokenStoreCopyEArray(ParseTable tpi[], int column, void* dest, void* src, CustomMemoryAllocator memAllocator, void* customData, TextParserResult *result)
{
	int **ea32src, **ea32dest;
	void ***easrc, ***eadest;
	ParseTableInfo *info = ParserGetTableInfo(tpi);
	ParseTable *ptcc = &tpi[column];
	StructTypeField field_type = ptcc->type;
	size_t offset = ptcc->storeoffset;
	U32 storage = TokenStoreGetStorageType(field_type);
	TS_REQUIRE(TOK_STORAGE_DIRECT_EARRAY | TOK_STORAGE_INDIRECT_EARRAY);

	switch (storage) {
	case TOK_STORAGE_DIRECT_EARRAY: // int** or F32** or blockEarray
		if (TokenStoreEarrayIsBlockArray(field_type))
		{
			int iBlockSize;
			ParseTable *pBlockTPI;
			void **ppSrcBlockEarray;
			void **ppDestBlockEarray;
			int iSrcSize;

			TokenStoreGetBlockSizeOrTPIForBlockEarray(tpi, column, &iBlockSize, &pBlockTPI);
			ppSrcBlockEarray = (void**)((intptr_t)src + offset);
			ppDestBlockEarray = (void**)((intptr_t)dest + offset);

			*ppDestBlockEarray = NULL;

			iSrcSize = beaSize(ppSrcBlockEarray);

			if (info && info->name)
			{
				beaSetSizeEx(ppDestBlockEarray, iBlockSize, pBlockTPI, iSrcSize, 0, memAllocator, customData, false, info->name, LINENUM_FOR_EARRAYS);
			}
			else
			{
				beaSetSizeEx(ppDestBlockEarray, iBlockSize, pBlockTPI, iSrcSize, 0, memAllocator, customData, false, __FILE__, __LINE__);
			}
			//note that this function is only used in a context in which the contents will be copied separately, oddly enough
			break;
		}
			

		ea32src = (int**)((intptr_t)src + offset);
		ea32dest = (int**)((intptr_t)dest + offset);
		*ea32dest = NULL;
		if (info && info->name) {
			ea32Compress_dbg(ea32dest, ea32src, memAllocator, customData, info->name, LINENUM_FOR_EARRAYS);
		} else {
			ea32Compress(ea32dest, ea32src, memAllocator, customData);
		}
		break;
	case TOK_STORAGE_INDIRECT_EARRAY:
		easrc = (void***)((intptr_t)src + offset);
		eadest = (void***)((intptr_t)dest + offset);
		*eadest = NULL;
		if (info && info->name) {
			eaCompress_dbg(eadest, easrc, memAllocator, customData, info->name, LINENUM_FOR_EARRAYS);
		} else {
			eaCompress(eadest, easrc, memAllocator, customData);
		}
		break;
	}
}

int TokenStoreCheckEArraySharedMemory(ParseTable tpi[], int column, void* structptr, TextParserResult *result)
{
	int** ea32;
	void*** ea;
	ParseTable *ptcc = &tpi[column];
	U32 storage = TokenStoreGetStorageType(ptcc->type);
	TS_REQUIRE(TOK_STORAGE_DIRECT_EARRAY | TOK_STORAGE_INDIRECT_EARRAY);

	switch (storage) {
	case TOK_STORAGE_DIRECT_EARRAY: // int** or F32** or blockearray (just checking a pointer, so works the same)
		ea32 = (int**)((intptr_t)structptr + ptcc->storeoffset);
		if (ea32 && *ea32 && !isSharedMemory(*ea32))
		{
			return 0;
		}
	case TOK_STORAGE_INDIRECT_EARRAY:
		ea = (void***)((intptr_t)structptr + ptcc->storeoffset);
		if (ea && *ea && !isSharedMemory(*ea))
		{
			return 0;
		}
	}
	return 1;
}

void TokenStoreDestroyEArray(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, TextParserResult *result)
{
	int** ea32;
	void*** ea;
	StructTypeField field_type = ptcc->type;
	U32 storage = TokenStoreGetStorageType(field_type);
	TS_REQUIRE(TOK_STORAGE_DIRECT_EARRAY | TOK_STORAGE_INDIRECT_EARRAY);

#ifdef TOKENSTORE_DETAILED_TIMERS
	{
		ParseTableInfo *info2;
		PERFINFO_AUTO_START_FUNC();
		info2 = ParserGetTableInfo(tpi);
		PERFINFO_AUTO_START_STATIC(info2->name, &info2->piEADestroy, 1);
	}
#endif

	switch (storage) {
	case TOK_STORAGE_DIRECT_EARRAY: // int** or F32**
		if (TokenStoreEarrayIsBlockArray(field_type))
		{
			int iBlockSize;
			ParseTable *pBlockTPI;
			ParseTableInfo *info = ParserGetTableInfo(tpi);

			void **ppBlockEarray = (void**)((intptr_t)structptr + ptcc->storeoffset);
			TokenStoreGetBlockSizeOrTPIForBlockEarray(tpi, column, &iBlockSize, &pBlockTPI);

			if(*ppBlockEarray)
			{
				if (!isSharedMemory2(ppBlockEarray, *ppBlockEarray))
				{
					beaDestroyEx(ppBlockEarray, iBlockSize, pBlockTPI);
				}
				else
				{
					*ppBlockEarray = NULL;
				}
			}
			break;
		}

		ea32 = (int**)((intptr_t)structptr + ptcc->storeoffset);
		if(*ea32)
		{
			if (!isSharedMemory2(ea32, EArray32FromHandle(*ea32)))
			{
				ea32Destroy(ea32);
			}
			else
			{
				*ea32 = NULL;
			}
		}
		break;
	case TOK_STORAGE_INDIRECT_EARRAY:
		ea = (void***)((intptr_t)structptr + ptcc->storeoffset);
		if(*ea)
		{
			if (!isSharedMemory2(ea, EArrayFromHandle(*ea)))
			{
				eaDestroy(ea);
			}
			else
			{
				*ea = NULL;
			}
		}
		break;
	}

#ifdef TOKENSTORE_DETAILED_TIMERS
	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
#endif
}

int TokenStoreGetNumElems(ParseTable tpi[], int column, const void* structptr, TextParserResult *result)
{
	return TokenStoreGetNumElems_inline(tpi,&tpi[column],column,structptr,result);
}

void TokenStoreMakeLocalEArray(ParseTable tpi[], int column, void* structptr, TextParserResult *result)
{
	int **ea32, *newea32 = NULL;
	void ***ea, **newea = NULL;
	ParseTable *ptcc = &tpi[column];
	U32 storage = TokenStoreGetStorageType(ptcc->type);
	TS_REQUIRE(TOK_STORAGE_DIRECT_EARRAY | TOK_STORAGE_INDIRECT_EARRAY);

	switch (storage) {
	case TOK_STORAGE_DIRECT_EARRAY: // int** or F32**
		ea32 = (int**)((intptr_t)structptr + ptcc->storeoffset);
		if (isSharedMemory(*ea32))
		{
			ea32Compress(&newea32, ea32, NULL, NULL);
			*ea32 = newea32;
		}
		break;
	case TOK_STORAGE_INDIRECT_EARRAY:
		ea = (void***)((intptr_t)structptr + ptcc->storeoffset);
		if (isSharedMemory(*ea))
		{
			eaCompress(&newea, ea, NULL, NULL);
			*ea = newea;
		}
		break;
	}
}


MultiVal* TokenStoreGetMultiVal(ParseTable tpi[], int column, const void* structptr, int index, TextParserResult *result)
{
	MultiVal*** parray;
	MultiVal*  pmv;
	MultiVal **pBlockEarray;
	ParseTable *ptcc = &tpi[column];
	U32 storage = TokenStoreGetStorageType(ptcc->type);

	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_DIRECT_FIXEDARRAY | TOK_STORAGE_INDIRECT_EARRAY | TOK_STORAGE_DIRECT_EARRAY);

	switch (storage) {
	case TOK_STORAGE_DIRECT_EARRAY:
		pBlockEarray = (MultiVal**)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= beaSize(pBlockEarray) || index < 0)
		{
			SETRESULT(PARSERESULT_ERROR);
			return NULL;
		}
		return (*pBlockEarray) + index;

	case TOK_STORAGE_DIRECT_FIXEDARRAY:
		pmv = (MultiVal*)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= tpi[column].param || index < 0)
		{
			SETRESULT(PARSERESULT_ERROR);
			return NULL;
		}
		return &pmv[index];
	
	case TOK_STORAGE_INDIRECT_EARRAY:
		parray = (MultiVal***)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= eaSize(parray) || index < 0)
		{			
			SETRESULT(PARSERESULT_ERROR);
			return NULL;
		}

		return (*parray)[index];

	case TOK_STORAGE_DIRECT_SINGLE:
		return (MultiVal*)((intptr_t)structptr + ptcc->storeoffset);
	};
	return 0;
}

MultiVal* TokenStoreAllocMultiVal(ParseTable tpi[], int column, void* structptr, int index, TextParserResult *result)
{
	MultiVal*** parray;
	MultiVal*  pmv;
	MultiVal **ppBlockArray;
	int iCurSize;
	ParseTable *ptcc = &tpi[column];
	U32 storage = TokenStoreGetStorageType(ptcc->type);

	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_DIRECT_FIXEDARRAY | TOK_STORAGE_INDIRECT_EARRAY | TOK_STORAGE_DIRECT_EARRAY);

	switch (storage) {
	case TOK_STORAGE_DIRECT_FIXEDARRAY:
		pmv = (MultiVal*)((intptr_t)structptr + ptcc->storeoffset);
		if (index >= tpi[column].param || index < 0)
		{			
			SETRESULT(PARSERESULT_ERROR);
			return 0;
		}
		return &pmv[index];

	case TOK_STORAGE_INDIRECT_EARRAY:
		parray = (MultiVal***)((intptr_t)structptr + ptcc->storeoffset);
		if (index > eaSize(parray) || index < 0)
		{
			SETRESULT(PARSERESULT_ERROR);
			return NULL;
		}
		if (index == eaSizeSlow(parray)) {
			ParseTableInfo *info = ParserGetTableInfo(tpi);
			if (info && info->name) {
				eaPush_dbg(parray, MultiValCreate(), info->name, LINENUM_FOR_EARRAYS);
			} else {
				eaPush(parray, MultiValCreate());
			}
		}
		return (*parray)[index];

	case TOK_STORAGE_DIRECT_EARRAY:
		ppBlockArray = (MultiVal**)((intptr_t)structptr + ptcc->storeoffset);
		iCurSize = beaSize(ppBlockArray);
		if (index > iCurSize || index < 0)
		{
			SETRESULT(PARSERESULT_ERROR);
			return NULL;
		}
		if (index == iCurSize)
		{
			ParseTableInfo *info = ParserGetTableInfo(tpi);
			if (info && info->name)
			{
				beaPushEmptyEx(ppBlockArray, sizeof(MultiVal), NULL, 0, NULL, NULL, info->name, LINENUM_FOR_EARRAYS);
			}
			else
			{
				beaPushEmpty(ppBlockArray);
			}
		}
		return (*ppBlockArray) + index;

	case TOK_STORAGE_DIRECT_SINGLE:
		return (MultiVal*)((intptr_t)structptr + ptcc->storeoffset);
	};
	return 0;
}

void TokenStoreClearMultiVal(ParseTable tpi[], int column, void* structptr, int index, TextParserResult *result)
{
	MultiVal*** parray;
	MultiVal*	pmv;
	MultiVal**	pBlockEarray;
	ParseTable *ptcc = &tpi[column];
	U32 storage = TokenStoreGetStorageType(ptcc->type);
	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_DIRECT_FIXEDARRAY | TOK_STORAGE_INDIRECT_EARRAY | TOK_STORAGE_DIRECT_EARRAY);

	switch (storage) 
	{
		case TOK_STORAGE_DIRECT_FIXEDARRAY:
			pmv = ((MultiVal*)((intptr_t)structptr + ptcc->storeoffset)) + index;
			if (index >= ptcc->param)
			{
				SETRESULT(PARSERESULT_ERROR);
				return;
			}
			MultiValConstruct(pmv, MULTI_NONE, 0);
			return;

		case TOK_STORAGE_DIRECT_EARRAY:
			pBlockEarray = (MultiVal**)((intptr_t)structptr + ptcc->storeoffset);
			if (index >= beaSize(pBlockEarray) || index < 0)
			{
				SETRESULT(PARSERESULT_ERROR);
				return;
			}
			MultiValConstruct(*pBlockEarray + index, MULTI_NONE, 0);
			return;

		case TOK_STORAGE_INDIRECT_EARRAY:
			parray = (MultiVal***)((intptr_t)structptr + ptcc->storeoffset);
			if (index >= eaSize(parray) || index < 0)
			{				
				SETRESULT(PARSERESULT_ERROR);
				return;
			}
			if ((*parray)[index])
				MultiValConstruct((*parray)[index], MULTI_NONE, 0);
// ABW I feel that this line is clearly a memory leak
//			(*parray)[index] = 0;
			return;

		case TOK_STORAGE_DIRECT_SINGLE:
			pmv = (MultiVal*)((intptr_t)structptr + ptcc->storeoffset);
			MultiValConstruct(pmv, MULTI_NONE, 0);
			return;
	}
}

void TokenStoreSetMultiVal(ParseTable tpi[], int column, void* structptr, int index, MultiVal* value, CustomMemoryAllocator memAllocator, void* customData, TextParserResult *result, TokenizerHandle tok)
{
	MultiVal*** parray;
	MultiVal*  pmv;
	MultiVal**	ppBlockArray;
	int iCurSize;
	ParseTable *ptcc = &tpi[column];
	U32 storage = TokenStoreGetStorageType(ptcc->type);
	TS_REQUIRE(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_DIRECT_FIXEDARRAY | TOK_STORAGE_INDIRECT_EARRAY | TOK_STORAGE_DIRECT_EARRAY);

	switch (storage) 
	{
		case TOK_STORAGE_DIRECT_FIXEDARRAY:
			pmv = ((MultiVal*)((intptr_t)structptr + ptcc->storeoffset)) +  index;
			if (index >= ptcc->param || index < 0)
			{
				if (tok)
					TokenizerErrorf(tok,"Extra MultiVal parameter");
				SETRESULT(PARSERESULT_ERROR);
				return;
			}
			MultiValCopyWith(pmv, value, memAllocator, customData, false);
			return;

		case TOK_STORAGE_DIRECT_EARRAY:
			ppBlockArray = (MultiVal**)((intptr_t)structptr + ptcc->storeoffset);
			iCurSize = beaSize(ppBlockArray);
			if (index > iCurSize || index < 0)
			{
				SETRESULT(PARSERESULT_ERROR);
				return;
			}
			if (index == iCurSize)
			{
				ParseTableInfo *info = ParserGetTableInfo(tpi);
				if (info && info->name)
				{
					beaPushEmptyEx(ppBlockArray, sizeof(MultiVal), NULL, 0, NULL, NULL, info->name, LINENUM_FOR_EARRAYS);
				}
				else
				{
					beaPushEmpty(ppBlockArray);
				}
			}
			MultiValCopyWith(*ppBlockArray + index, value, memAllocator, customData, false);
			return;


		case TOK_STORAGE_INDIRECT_EARRAY:
			parray = (MultiVal***)((intptr_t)structptr + ptcc->storeoffset);
			if (index > eaSize(parray) || index < 0)
			{				
				SETRESULT(PARSERESULT_ERROR);
				return;
			}
			if (index == eaSize(parray))
			{
				MultiVal* pnewmv;
				ParseTableInfo *info = ParserGetTableInfo(tpi);
				if (memAllocator)
					pnewmv = memAllocator(customData, sizeof(MultiVal));
				else
					pnewmv = MultiValCreate();
				if (info && info->name) {
					eaPush_dbg(parray, pnewmv, info->name, LINENUM_FOR_EARRAYS);
				} else {
					eaPush(parray, pnewmv);
				}
			}
			if (!(*parray)[index])
			{
				if (memAllocator)
					(*parray)[index] = memAllocator(customData, sizeof(MultiVal));
				else
					(*parray)[index] = MultiValCreate();
			}
			MultiValCopyWith((*parray)[index],value, memAllocator, customData, false);
			return;

		case TOK_STORAGE_DIRECT_SINGLE:
			pmv = (MultiVal*)((intptr_t)structptr + ptcc->storeoffset);
			MultiValCopyWith(pmv, value, memAllocator, customData, false);
			return;

	}
}


