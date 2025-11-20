#pragma once
GCC_SYSTEM

#ifndef TOKENSTORE_H
#define TOKENSTORE_H

#include "TextParserEnums.h"

typedef struct MultiVal MultiVal;
typedef void* TokenizerHandle;
typedef void* (*CustomMemoryAllocator)(void* data, size_t size);


///////////////////////////////////////////////////// TokenStoreXxx functions
// these are all pretty self-explanatory.  
// index always refers to the position in the array that you want to manipulate,
// should be zero for non-arrays
// if either the result or tok pointers are NULL, they will be safely ignored

int TokenStoreGetNumElems(ParseTable tpi[], int column, const void* structptr, TextParserResult *result); // valid for any or token type
void TokenStoreRemoveElement(ParseTable tpi[], int column, void* structptr, int index, TextParserResult *result);

void TokenStoreFreeString(ParseTable tpi[], int column, void* structptr, int index, TextParserResult *result);
void TokenStoreClearString(ParseTable tpi[], int column, void* structptr, int index, TextParserResult *result);
char* TokenStoreSetString(ParseTable tpi[], int column, void* structptr, int index, const char* str, CustomMemoryAllocator memAllocator, void* customData, TextParserResult *result, TokenizerHandle tok);
SA_RET_OP_STR const char* TokenStoreGetString(ParseTable tpi[], int column, const void* structptr, int index, TextParserResult *result);
size_t TokenStoreGetStringMemUsage(ParseTable tpi[], int column, const void* structptr, int index, bool bAbsoluteUsage, TextParserResult *result);

void TokenStoreSetIntAuto(ParseTable tpi[], int column, void* structptr, int index, S64 value, TextParserResult *result, TokenizerHandle tok);
S64 TokenStoreGetIntAuto(ParseTable tpi[], int column, const void* structptr, int index, TextParserResult *result);

void TokenStoreSetInt(ParseTable tpi[], int column, void* structptr, int index, int value, TextParserResult *result, TokenizerHandle tok);
int TokenStoreGetInt(ParseTable tpi[], int column, const void* structptr, int index, TextParserResult *result);
void TokenStoreSetInt64(ParseTable tpi[], int column, void* structptr, int index, S64 value, TextParserResult *result, TokenizerHandle tok);
S64 TokenStoreGetInt64(ParseTable tpi[], int column, const void* structptr, int index, TextParserResult *result);
void TokenStoreSetInt16(ParseTable tpi[], int column, void* structptr, int index, S16 value, TextParserResult *result, TokenizerHandle tok);
S16 TokenStoreGetInt16(ParseTable tpi[], int column, const void* structptr, int index, TextParserResult *result);
void TokenStoreSetU8(ParseTable tpi[], int column, void* structptr, int index, U8 value, TextParserResult *result, TokenizerHandle tok);
U8 TokenStoreGetU8(ParseTable tpi[], int column, const void* structptr, int index, TextParserResult *result);
void TokenStoreSetBit(ParseTable tpi[], int column, void* structptr, int index, int value, TextParserResult *result, TokenizerHandle tok);
U32 TokenStoreGetBit(ParseTable tpi[], int column, const void* structptr, int index, TextParserResult *result);

void TokenStoreSetF32(ParseTable tpi[], int column, void* structptr, int index, F32 value, TextParserResult *result, TokenizerHandle tok);
F32 TokenStoreGetF32(ParseTable tpi[], int column, const void* structptr, int index, TextParserResult *result);

void	  TokenStoreSetMultiVal(ParseTable tpi[], int column, void* structptr, int index, MultiVal* value, CustomMemoryAllocator memAllocator, void* customData, TextParserResult *result, TokenizerHandle tok);
MultiVal* TokenStoreAllocMultiVal(ParseTable tpi[], int column, void* structptr, int index, TextParserResult *result);
MultiVal* TokenStoreGetMultiVal(ParseTable tpi[], int column, const void* structptr, int index, TextParserResult *result);
void	  TokenStoreClearMultiVal(ParseTable tpi[], int column, void* structptr, int index, TextParserResult *result);

#define TokenStoreAlloc(tpi, column, structptr, index, size, memAllocator, customData, result) TokenStoreAllocCharged(tpi, column, structptr, index, size, memAllocator, customData, result, NULL)
void* TokenStoreAllocCharged(ParseTable tpi[], int column, void* structptr, int index, U32 size, CustomMemoryAllocator memAllocator, void* customData, TextParserResult *result, ParseTable *pTPIToCharge);
void TokenStoreFree(ParseTable tpi[], int column, void* structptr, int index, TextParserResult *result);
void TokenStoreFreeWithTPI(ParseTable tpi[], int column, void* structptr, int index, TextParserResult *result, ParseTable *pTPIOfFreedBlock);
int* TokenStoreGetCountField(ParseTable tpi[], int column, const void* structptr, TextParserResult *result);
void TokenStoreSetPointer(ParseTable tpi[], int column, void* structptr, int index, void* ptr, TextParserResult *result);
void* TokenStoreGetPointer(ParseTable tpi[], int column, const void* structptr, int index, TextParserResult *result);
void* TokenStoreRemovePointer(ParseTable tpi[], int column, void* structptr, void* ptr, TextParserResult *result);
void TokenStoreAddUsedField(ParseTable tpi[], int column, void *structptr);

void TokenStoreSetRef(ParseTable tpi[], int column, void* structptr, int index, const char* str, TextParserResult *result, TokenizerHandle tok);
void TokenStoreClearRef(ParseTable tpi[], int column, void* structptr, int index, bool destroy_reference, TextParserResult *result);
void TokenStoreCopyRef(ParseTable tpi[], int column, void* dest, void* src, int index, TextParserResult *result);
void TokenStoreCopyRef2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest, TextParserResult *result);
const char *TokenStoreGetRefString(ParseTable tpi[], int column, const void* structptr, int index, TextParserResult *result);
void **TokenStoreGetRefHandlePointer(ParseTable tpi[], int column, const void *structPtr, int index, TextParserResult *result);

//Call this after you have loaded structs in another thread, this will hookup the references
//since they must be done in the main thread
void TokenStoreFlushQueuedReferenceLookups();

//call this from a background thread that was loading structures with references... BUT, this is in no way optimized,
//just adding it for debugging purposes, if you actually need to do this, something funky is going on
void TokenStoreWaitForQueuedReferenceLookupsToFlush(void);


void*** TokenStoreGetEArray(ParseTable tpi[], int column, const void* structptr, TextParserResult *result);
int** TokenStoreGetEArrayInt(ParseTable tpi[], int column, const void* structptr, TextParserResult *result);
F32** TokenStoreGetEArrayF32(ParseTable tpi[], int column, const void* structptr, TextParserResult *result);
void** TokenStoreGetBlockEarray(ParseTable tpi[], int column, const void* structptr, TextParserResult *result);
size_t TokenStoreGetEArrayMemUsage(ParseTable tpi[], int column, const void* structptr, bool bAbsoluteUsage, TextParserResult *result);
void TokenStoreSetEArraySize(ParseTable tpi[], int column, void* structptr, int size, TextParserResult *result);
void TokenStoreSetEArrayCapacity(ParseTable tpi[], int column, void* structptr, int size);

//BEWARE BEWARE DO NOT USE THIS FUNCTION unless you are sure you know what you are doing, it doesn't always copy the contents
//of the earray, and is used only internally in the move-earray-into-shared-memory code
void TokenStoreCopyEArray(ParseTable tpi[], int column, void* dest, void* src, CustomMemoryAllocator memAllocator, void* customData, TextParserResult *result);


void TokenStoreDestroyEArray(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, TextParserResult *result);
void TokenStoreMakeLocalEArray(ParseTable tpi[], int column, void* structptr, TextParserResult *result);
int TokenStoreCheckEArraySharedMemory(ParseTable tpi[], int column, void* structptr, TextParserResult *result);

int TokenStoreGetFixedArraySize(ParseTable tpi[], int column);

//////////////////////////////////////// getting the storage type for a field

// these storage enums are for verification of parse tables - each token type is
// compatible with different methods of storage
#define TOK_STORAGE_DIRECT_SINGLE		(1 << 0)		// int member;
#define TOK_STORAGE_DIRECT_FIXEDARRAY	(1 << 1)		// int members[3];
#define TOK_STORAGE_DIRECT_EARRAY		(1 << 2)		// int* members;
#define TOK_STORAGE_INDIRECT_SINGLE		(1 << 3)		// char* str;
#define TOK_STORAGE_INDIRECT_FIXEDARRAY (1 << 4)		// char* strs[3];
#define TOK_STORAGE_INDIRECT_EARRAY		(1 << 5)		// char** strs;
// ORDER OF THESE IS IMPORTANT - check TokenStoreGetStorageType to see hack


#define TokenStoreGetStorageType(type) ((type & TOK_FIXED_ARRAY ? TOK_STORAGE_DIRECT_FIXEDARRAY : (type & TOK_EARRAY ? TOK_STORAGE_DIRECT_EARRAY : TOK_STORAGE_DIRECT_SINGLE))\
	<< (type & TOK_INDIRECT ? 3 : 0))

#define TokenStoreStorageTypeIsAnArray(type) ((type != TOK_STORAGE_DIRECT_SINGLE) && (type != TOK_STORAGE_INDIRECT_SINGLE))

#define TokenStoreStorageTypeIsFixedArray(type) ((type) == TOK_STORAGE_DIRECT_FIXEDARRAY || (type) == TOK_STORAGE_INDIRECT_FIXEDARRAY)
#define TokenStoreStorageTypeIsEArray(type) ((type) == TOK_STORAGE_DIRECT_EARRAY || (type) == TOK_STORAGE_INDIRECT_EARRAY)

#define TokenStoreIsCompatible(type, type_compat_bits) (TokenStoreGetStorageType(type) & (type_compat_bits))



#define TS_REQUIRE(compat_bits) devassert(TokenStoreIsCompatible(tpi[column].type, compat_bits) && "internal textparser error")



// Defines passed to malloc for line numbers for memory tracking
#define LINENUM_FOR_STRUCTS 100001
#define LINENUM_FOR_STRINGS 100002
#define LINENUM_FOR_EARRAYS 100003
#define LINENUM_FOR_POOLED_STRUCTS 100004
#define LINENUM_FOR_TS_POOLED_STRUCTS 100005
#define LINENUM_FOR_POOLED_STRINGS 100006


#endif //TOKENSTORE_H