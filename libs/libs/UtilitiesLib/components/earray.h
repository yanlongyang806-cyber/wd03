// earray.h - provides yet another type of expandable array
// these arrays are differentiated in that you generally declare them as
// MyStruct** earray; and access them like a normal array of pointers
// NOTE - EArrays are now threadsafe

#ifndef __EARRAY_H
#define __EARRAY_H
#pragma once
GCC_SYSTEM

#ifndef EXTERNAL_TEST
#include "stdtypes.h"
#else
#include "test.h"
#endif

C_DECLARATIONS_BEGIN

//forward declaration
typedef struct ParseTable ParseTable;

// Struct-related inlined functions
typedef struct EArray
{
	int count;
	int size; // if negative this is a stack earray
	intptr_t tableandflags; // a parse table pointer, and some flags packed in
	void* structptrs[1];
} EArray;

#define EARRAY_HEADER_SIZE	OFFSETOF(EArray,structptrs)	

typedef struct EArray32
{
	int count;
	int size;
	U32 values[1];
} EArray32;

typedef struct EArray64
{
	int count;
	int size;
	U64 values[1];
} EArray64;

#define EARRAY32_HEADER_SIZE	OFFSETOF(EArray32,values)	
#define EARRAY64_HEADER_SIZE	OFFSETOF(EArray64,values)

#define EArrayFromHandle(handle) ((EArray*)(((char*)handle) - EARRAY_HEADER_SIZE))
#define HandleFromEArray(array) ((EArrayHandle)(((char*)array) + EARRAY_HEADER_SIZE))

#define EArrayTypeFromHandle(t, handle)					((EArray##t*)(((char*)handle) - EARRAY##t##_HEADER_SIZE))
#define HandleFromEArrayType(t, array)					((U##t*)(((char*)array) + EARRAY##t##_HEADER_SIZE))
#define eaTypeSize(t, handle)							(*(handle)? EArray##t##FromHandle(*(handle))->count : 0)

#if _PS3 || defined(__cplusplus)
#define EAPtrTest(h) ((**(h)),0) // Can't do three *s, since it might be a pointer to an anonymous type
#else
__forceinline static int EAPtrTest(const void * const * const *ptr) { return 0; }
#endif

// Use eags() in a debugger
#define eaSize(handle)			((void)(0 ? EAPtrTest(handle) : 1), (*(handle)? EArrayFromHandle(*(handle))->count : 0))
#define eaUSize(handle)			((U32)eaSize(handle))

// This should only be used when you have more or less levels of indirection than void *** (e.g, an earray of void *s).
#define eaSizeUnsafe(handle)	(*(handle)? EArrayFromHandle(*(handle))->count : 0)



//////////////////////////////////////////////////// EArray's (64-bit compatible)
// you will usually declare these as MyStruct**, and then use them to
// store pointers to stuff of interest.  

typedef void** EArrayHandle;									// pointer to a list of pointers
typedef const void ** cEArrayHandle;							// non-const pointer to a non-const list of const pointers
typedef const void * const * const ccEArrayHandle;				// const pointer to a const list of const pointers

typedef U32* EArray32Handle;									// pointer to a list of ints
typedef const U32* cEArray32Handle;								// non-const pointer to a non-const list of const ints
typedef const U32* const ccEArray32Handle;						// const pointer to a const list of const ints

typedef U64* EArray64Handle;									// pointer to a list of 64 bit ints
typedef const U64* cEArray64Handle;								// non-const pointer to a non-const list of const ints
typedef const U64* const ccEArray64Handle;						// const pointer to a const list of const 64 bit ints

typedef F32* EArrayF32Handle;									// pointer to a list of ints
typedef const F32* cEArrayF32Handle;							// non-const pointer to a non-const list of const ints
typedef const F32* const ccEArrayF32Handle;						// const pointer to a const list of const ints

typedef void (*EArrayItemCallback)(void*);
typedef void (*EArrayItemFileLineCallback)(void*, const char*, int);
typedef void *(*EArrayItemCopier)(const void*);
typedef void* (*CustomMemoryAllocator)(void* data, size_t size);
typedef int (*EArrayStructCompare)(const void *pArrayObj, const void *pUserInput);


__forceinline static int eaSizeSlow(SA_PRE_NN_VALID SA_POST_NN_RELEMS(1) cEArrayHandle *handle)
{
	EArray* pArray;
	if (!(*handle))
		return 0;
	pArray = EArrayFromHandle(*handle);
	return pArray->count;
}

__forceinline static void eaStackInit(SA_PARAM_NN_VALID cEArrayHandle* handle, EArray *pArray, int size)
{
	assert(size > 0);
	memset(pArray->structptrs, 0, size * sizeof(pArray->structptrs[0]));
	pArray->count = 0;
	pArray->size = -size;
	pArray->tableandflags = 0;
	*handle = (cEArrayHandle)HandleFromEArray(pArray);
}

#define eaStackCreate(handle,size)													\
{																					\
	EArray *pArray = alloca(sizeof(EArray) + (size) * sizeof(pArray->structptrs[0]));	\
	eaStackInit(handle, pArray, size);												\
}

void	eaCreateInternal(SA_PRE_NN_VALID SA_POST_NN_RELEMS(1) cEArrayHandle* handle MEM_DBG_PARMS);	// creates a size zero array
#define eaCreate(handle) eaCreateInternal(handle MEM_DBG_PARMS_INIT)

void	eaDestroy(SA_PRE_NN_VALID SA_POST_NN_NULL cEArrayHandle* handle);								// free array
#define eaAssertEmptyAndDestroy(handle) (assert(!eaSize(handle)),eaDestroy(handle))
void	eaSetSize_dbg(SA_PRE_NN_VALID SA_POST_NN_ELEMS_VAR(size) cEArrayHandle* handle, int size MEM_DBG_PARMS);		// grows or shrinks to size, adds NULL entries if required
#define eaSetSize(handle,size) eaSetSize_dbg(handle,size MEM_DBG_PARMS_INIT);
#define seaSetSize(handle,size) eaSetSize_dbg(handle,size MEM_DBG_PARMS_CALL);
#define steaSetSize(handle,size,mem_struct_ptr) eaSetSize_dbg(handle,size MEM_DBG_STRUCT_PARMS_CALL(mem_struct_ptr));
void	eaSetCapacity_dbg(SA_PRE_NN_VALID SA_POST_NN_ELEMS_VAR(size) cEArrayHandle* handle, int size MEM_DBG_PARMS);	// set the current capacity to size, may reduce size
#define eaSetCapacity(handle, size) eaSetCapacity_dbg(handle,size MEM_DBG_PARMS_INIT)
#define seaSetCapacity(handle, size) eaSetCapacity_dbg(handle,size MEM_DBG_PARMS_CALL)
#define steaSetCapacity(handle, size, mem_struct_ptr) eaSetCapacity_dbg(handle,size MEM_DBG_STRUCT_PARMS_CALL(mem_struct_ptr))
int		eaCapacity(SA_PRE_NN_OP_VALID ccEArrayHandle* handle);							// get the current number of items that can be held without growing
size_t	eaMemUsage(ccEArrayHandle* handle, bool bAbsoluteUsage);							// get the amount of memory actually used (not counting slack allocated)
void	eaCompress_dbg(EArrayHandle *dst, cEArrayHandle *src, CustomMemoryAllocator memAllocator, void *customData MEM_DBG_PARMS);
#define eaCompress(dst, src, memAllocator, customData) eaCompress_dbg(dst, src, memAllocator, customData MEM_DBG_PARMS_INIT)

int		eaPush_dbg(cEArrayHandle* handle, const void* structptr MEM_DBG_PARMS);		// add to the end of the list, returns the index it was added at (the old size)
#define eaVerifyType(handle,structptr)		(1?0:(EAPtrTest(handle),**(handle)=(structptr),*(handle)=*(handle),0))
#define eaVerifyTypeConst(handle,structptr)	(1?0:(EAPtrTest(handle),**(handle)==(structptr),0))
#define eaPush(handle,structptr) ((void)eaVerifyType(handle,structptr),eaPush_dbg((cEArrayHandle*)(handle),structptr MEM_DBG_PARMS_INIT))
#define seaPush(handle,structptr) (eaVerifyType(handle,structptr),eaPush_dbg((cEArrayHandle*)(handle),structptr MEM_DBG_PARMS_CALL))
#define steaPush(handle,structptr,mem_struct_ptr) (eaVerifyType(handle,structptr),eaPush_dbg((cEArrayHandle*)(handle),structptr MEM_DBG_STRUCT_PARMS_CALL(mem_struct_ptr)))
int		eaPushUnique_dbg(cEArrayHandle* handle, const void* structptr MEM_DBG_PARMS); // add to the end of the list if not already in the list, returns the index it was added at (the new size)
#define eaPushUnique(handle,structptr) (eaVerifyType(handle,structptr),eaPushUnique_dbg((cEArrayHandle*)(handle),structptr MEM_DBG_PARMS_INIT))
int		eaPushEArray_dbg(cEArrayHandle* handle, ccEArrayHandle* src MEM_DBG_PARMS); // add an earray to the end of the list, returns the index it was added at
#define eaPushEArray(handle,src) (eaVerifyType(handle,**(src)),eaPushEArray_dbg((cEArrayHandle*)(handle),(ccEArrayHandle*)(src) MEM_DBG_PARMS_INIT))
int		eaPushArray_dbg(cEArrayHandle* handle, const void * const *structptrs, int count MEM_DBG_PARMS); // add the elements of a regular array to an earray
#define eaPushArray(handle,structptrs,count) (eaVerifyType(handle,*structptrs),eaPushArray_dbg((cEArrayHandle*)(handle),structptrs,count MEM_DBG_PARMS_INIT))
__forceinline static int eaPushIf(cEArrayHandle* handle, const void *structptr, int cond)  { return (cond)?(eaPush(handle,structptr)):eaSize(handle); }
__forceinline static int eaPushIfNotNull(cEArrayHandle* handle, const void *structptr)  { return eaPushIf(handle,structptr,structptr!=NULL); }
SA_ORET_OP_VALID void*	eaPop(cEArrayHandle* handle);						// remove the last item from the list
void	eaReverse(cEArrayHandle* handle);
void	eaReverseEx(cEArrayHandle* handle, int start_idx, int end_idx);
void	eaClear_dbg(cEArrayHandle* handle);						// removes all items, without freeing them and zeroes the memory
#define eaClear(handle) (eaVerifyType(handle,NULL),eaClear_dbg((cEArrayHandle*)handle),0)
void	eaClearFast(cEArrayHandle* handle);						// removes all items, without freeing them and doesn't zero the memory
void	eaCopy_dbg(cEArrayHandle* dest, ccEArrayHandle* src MEM_DBG_PARMS);		// create dest if needed, copy pointers
#define eaCopy(dest,src) (eaVerifyType(dest,**(src)),eaCopy_dbg((cEArrayHandle*)(dest),(ccEArrayHandle*)(src) MEM_DBG_PARMS_INIT))
#define seaCopy(dest,src) (eaVerifyType(dest,**(src)),eaCopy_dbg((cEArrayHandle*)(dest),(ccEArrayHandle*)(src) MEM_DBG_PARMS_CALL))
#define steaCopy(dest,src,mem_struct_ptr) (eaVerifyType(dest,**(src)),eaCopy_dbg((cEArrayHandle*)(dest),(ccEArrayHandle*)(src) MEM_DBG_STRUCT_PARMS_CALL(mem_struct_ptr)))

void	eaPushStructs_dbg(EArrayHandle* dst, ccEArrayHandle* src, ParseTable *pti MEM_DBG_PARMS);
#define eaPushStructsVoid(dst,src,pti) eaPushStructs_dbg(dst,src,pti MEM_DBG_PARMS_INIT)
#define eaPushStructs(dst,src,pti) eaPushStructsVoid(STRUCT_TYPESAFE_PTR_PTR_PTR(pti,dst),STRUCT_TYPESAFE_PTR_PTR_PTR(pti,src),pti)
#define eaPushStructsDeConst(dst,src,pti) eaPushStructsVoid(STRUCT_NOCONST_TYPESAFE_PTR_PTR_PTR(pti,dst),STRUCT_TYPESAFE_PTR_PTR_PTR(pti,src),pti)
#define eaPushStructsNoConst(dst,src,pti) eaPushStructsVoid(STRUCT_NOCONST_TYPESAFE_PTR_PTR_PTR(pti,dst),STRUCT_NOCONST_TYPESAFE_PTR_PTR_PTR(pti,src),pti)

// Takes in two earrays and adds objects that are in left but not right to result
// Check is right_func(right[i])==left_func(left[i]).  Adds left[i], not left_func(left[i])!  
// If func is null, func is identity
typedef const void* (*EAAddrFunc)(const void *object);
#define eaDiffAddr(left, right, result) eaDiffAddrEx((left), NULL, (right), NULL, result)
#define eaDiffAddrEx(left,left_func,right,right_func,result) eaDiffAddrEx_dbg(left,left_func,right,right_func,result MEM_DBG_PARMS_INIT)
void	eaDiffAddrEx_dbg(cEArrayHandle *left, EAAddrFunc left_func, cEArrayHandle *right, EAAddrFunc right_func, cEArrayHandle *result MEM_DBG_PARMS);
// Takes in two earrays and adds objects that are in both to result
#define eaIntersectAddr(left, right, result) eaIntersectAddrEx((left), NULL, (right), NULL, result)
#define eaIntersectAddrEx(left,left_func,right,right_func,result) eaIntersectAddrEx_dbg(left,left_func,right,right_func,result MEM_DBG_PARMS_INIT)
void	eaIntersectAddrEx_dbg(cEArrayHandle *left, EAAddrFunc left_func, cEArrayHandle *right, EAAddrFunc right_func, cEArrayHandle *result MEM_DBG_PARMS);

void	eaSet_dbg(cEArrayHandle* handle, const void* structptr, int i MEM_DBG_PARMS); // set i'th element (zero-based)
#define eaSet(handle, structptr, i) (eaVerifyType(handle,structptr),eaSet_dbg((cEArrayHandle*)(handle), structptr, i MEM_DBG_PARMS_INIT))
size_t	eaGetIndexOrOffsetToNULL(ccEArrayHandle* handle, U32 index);
void*	eaGetVoid(ccEArrayHandle* handle, U32 index);	// get index'th element (zero-based), NULL on error
#define eaGet(handle, index) ((*(handle))[eaGetIndexOrOffsetToNULL((handle),(index))])
#define eaGetLast(handle) (eaSize(handle) > 0 ? (*(handle))[eaSize(handle) - 1] : NULL)
int		eaInsert_dbg(cEArrayHandle* handle, const void* structptr, int i, bool bIndexed MEM_DBG_PARMS); // insert before i'th position, will not insert on error (i == -1, etc.)
#define eaInsert(handle,structptr,i) (eaVerifyType(handle,structptr),eaInsert_dbg((cEArrayHandle*)(handle),structptr,i,false MEM_DBG_PARMS_INIT))
#define seaInsert(handle,structptr,i) (eaVerifyType(handle,structptr),eaInsert_dbg((cEArrayHandle*)(handle),structptr,i,false MEM_DBG_PARMS_CALL))
#define steaInsert(handle,structptr,i,mem_struct_ptr) (eaVerifyType(handle,structptr),eaInsert_dbg((cEArrayHandle*)(handle),structptr,i,false MEM_DBG_STRUCT_PARMS_CALL(mem_struct_ptr)))
int		eaInsertEArray_dbg(cEArrayHandle* handle, ccEArrayHandle* src, int i MEM_DBG_PARMS); // insert before i'th position, will not insert on error (i == -1, etc.)
#define eaInsertEArray(handle,src,i) (eaVerifyType(handle,**src),eaInsertEArray_dbg((cEArrayHandle*)(handle),(ccEArrayHandle*)(src),i MEM_DBG_PARMS_INIT))
void*	eaRemoveVoid(cEArrayHandle* handle, U32 index);		// remove the index'th element, NULL on error
size_t	eaGetIndexToRemovedOrOffsetToNULL(cEArrayHandle* handle, U32 index);
#define eaRemove(handle, index) ((*(handle))[eaGetIndexToRemovedOrOffsetToNULL((handle),(index))])
void*   eaRemoveFastVoid(cEArrayHandle* handle, U32 index); // remove the i'th element, and move the last element into this place DOES NOT KEEP ORDER
size_t	eaGetIndexToRemovedFastOrOffsetToNULL(cEArrayHandle* handle, U32 index);
#define eaRemoveFast(handle, index) ((*(handle))[eaGetIndexToRemovedFastOrOffsetToNULL((handle),(index))])
void	eaRemoveRange(cEArrayHandle* handle, int start, int count); // remove count elements, starting with start
void	eaRemoveTail(cEArrayHandle* handle, int start); // remove trailing elements, starting with start
void	eaRemoveTailEx(cEArrayHandle* handle, int start, EArrayItemCallback destructor); // remove trailing elements, starting with start, and call destructor on them (or free if none specified)
int		eaFind_dbg(ccEArrayHandle* handle, SA_PRE_OP_VALID const void* structptr);	// find the first element that matches structptr, returns -1 on error
#define eaFind(handle,structptr) (eaVerifyTypeConst(handle,structptr),eaFind_dbg((ccEArrayHandle*)(handle),structptr))
int		eaFindCmp(ccEArrayHandle* handle, const void* structptr, EArrayStructCompare compareFn); // like eaFind, but compares with a function
int		eaFindAndRemove_dbg(cEArrayHandle* handle, const void* structptr); // finds element, deletes it, returns same value as eaFind
#define eaFindAndRemove(handle,structptr) (eaVerifyType(handle,NULL),eaVerifyTypeConst(handle,structptr),eaFindAndRemove_dbg((cEArrayHandle*)(handle),structptr))
int		eaFindAndRemoveFast_dbg(cEArrayHandle* handle, const void* structptr); // finds element, deletes it, returns same value as eaFind DOES NOT KEEP ORDER
#define eaFindAndRemoveFast(handle,structptr) (eaVerifyType(handle,NULL),eaVerifyTypeConst(handle,structptr),eaFindAndRemoveFast_dbg((cEArrayHandle*)(handle),structptr))
void	eaSwap(cEArrayHandle* handle, int i, int j);			// exchange the i'th element with the j'th element
void	eaMove(cEArrayHandle* handle, int dest, int src);	// shift left or right to move the src'th element to dest
void*	eaRandChoice(ccEArrayHandle* handle);
int		eaRandIndex(ccEArrayHandle* handle);

void	eaRandomize(void ***pppArray);

int     eaFindString(CONST_STRING_EARRAY * eaArray, const char* stringToFind); // this is case insensitive

void	eaRemoveDuplicateStrings(char *** eaArray); //for an array of MALLOC'd strings, removes 2nd+ copies of any (case insensitive)
void	eaRemoveDuplicateEStrings(char *** eaArray); //for an array of EStrings, removes 2nd+ copies of any (case insensitive)
	//duplicate strings
	//DO NOT USE THIS ON BIG ARRAYS without optimizing it. It's being written for utility, not performance.
void	eaRemoveSequentialDuplicates(void ***peaArray);

#define eaHead(handle) (((handle) && eaSize(handle)) ? (*(handle))[0] : NULL)
#define eaTail(handle) (((handle) && eaSize(handle)) ? (*(handle))[eaSize(handle) - 1] : NULL)

// deep copy stuff..
void	eaClearEx(EArrayHandle* handle, EArrayItemCallback destructor); // calls destructor or free on each element in the array
#define eaClearExFileLine(handle, destructor) eaClearExFileLineEx(handle, destructor, __FILE__, __LINE__)
void	eaClearExFileLineEx(EArrayHandle* handle, EArrayItemFileLineCallback destructor, const char* file, int line);  // Calls destructor or free on each element with file and line
void	eaDestroyEx(EArrayHandle *handle,EArrayItemCallback destructor); // calls destroy contents, then destroy; if destructor is null, uses free()
#define eaDestroyExFileLine(handle, destructor) eaDestroyExFileLineEx(handle, destructor, __FILE__, __LINE__)
void	eaDestroyExFileLineEx(EArrayHandle *handle, EArrayItemFileLineCallback destructor, const char* file, int line);
void	eaCopyEx_dbg(EArrayHandle* src, EArrayHandle* dst, EArrayItemCopier copyFn, EArrayItemCallback destroyFn MEM_DBG_PARMS); // calls constructor or malloc for each element
#define eaCopyEx(src,dst,copyFn,destroyFn) eaCopyEx_dbg(src,dst,copyFn,destroyFn MEM_DBG_PARMS_INIT)
void	eaCopyStructs_dbg(ccEArrayHandle* src, EArrayHandle* dst, ParseTable *pti MEM_DBG_PARMS); // calls StructCopyAll on each element
#define eaCopyStructsVoid(src,dst,pti) (eaVerifyTypeConst(src, **dst), eaCopyStructs_dbg(src,dst,pti MEM_DBG_PARMS_INIT))
#define eaCopyStructsVoidNoTypeCheck(src,dst,pti) (eaCopyStructs_dbg(src,dst,pti MEM_DBG_PARMS_INIT))
#define eaCopyStructs(src,dst,pti) eaCopyStructsVoid(STRUCT_TYPESAFE_PTR_PTR_PTR(pti,src),STRUCT_TYPESAFE_PTR_PTR_PTR(pti,dst),pti)
#define eaCopyStructsNoConst(src,dst,pti) eaCopyStructsVoid(STRUCT_NOCONST_TYPESAFE_PTR_PTR_PTR(pti,src),STRUCT_NOCONST_TYPESAFE_PTR_PTR_PTR(pti,dst),pti)
#define eaCopyStructsDeConst(src,dst,pti) eaCopyStructsVoidNoTypeCheck(STRUCT_TYPESAFE_PTR_PTR_PTR(pti,src),STRUCT_NOCONST_TYPESAFE_PTR_PTR_PTR(pti,dst),pti)
#define eaCopyStructsReConst(src,dst,pti) eaCopyStructsVoidNoTypeCheck(STRUCT_NOCONST_TYPESAFE_PTR_PTR_PTR(pti,src),STRUCT_TYPESAFE_PTR_PTR_PTR(pti,dst),pti)

void	eaCopyEStrings_dbg(ccEArrayHandle* src, EArrayHandle *dst MEM_DBG_PARMS);
#define eaCopyEStrings(src,dst) eaCopyEStrings_dbg(src,dst MEM_DBG_PARMS_INIT);

void	eaForEach(cEArrayHandle* handle, EArrayItemCallback callback); // calls callback on each valid element in the array

//calls StructDestroy on each element of the earray with the given TPI, then clears the earray
void    eaClearStructVoid(cEArrayHandle* handle, ParseTable *pti);
#define eaClearStruct(handle, pti) eaClearStructVoid(STRUCT_TYPESAFE_PTR_PTR_PTR(pti, handle), pti)
#define eaClearStructNoConst(handle, pti) eaClearStructVoid(STRUCT_NOCONST_TYPESAFE_PTR_PTR_PTR(pti, handle), pti)

//calls StructDestroy on each element of the earray with the given TPI, then destroys the earray
void	eaDestroyStructVoid(cEArrayHandle* handle, ParseTable *pti);
#define eaDestroyStruct(handle, pti) eaDestroyStructVoid(STRUCT_TYPESAFE_PTR_PTR_PTR(pti, handle), pti)
#define eaDestroyStructNoConst(handle, pti) eaDestroyStructVoid(STRUCT_NOCONST_TYPESAFE_PTR_PTR_PTR(pti, handle), pti)

//sets the earray to contain this many structs, StructDestroying or StructCreating at the end
void	eaSetSizeStruct_dbg(EArrayHandle* handle, ParseTable *pti, int size MEM_DBG_PARMS);
#define eaSetSizeStructVoid(handle, pti, size) eaSetSizeStruct_dbg(handle, pti, size MEM_DBG_PARMS_INIT)
#define eaSetSizeStruct(handle, pti, size) eaSetSizeStructVoid(STRUCT_TYPESAFE_PTR_PTR_PTR(pti, handle), pti, size)
#define eaSetSizeStructNoConst(handle, pti, size) eaSetSizeStructVoid(STRUCT_NOCONST_TYPESAFE_PTR_PTR_PTR(pti, handle), pti, size)

// Gets the index from the handle, filling it with StructCreated structs if necessary
void*	eaGetStruct_dbg(EArrayHandle* handle, ParseTable *pti, int index MEM_DBG_PARMS);
#define eaGetStructVoid(handle, pti, index) eaGetStruct_dbg(handle, pti, index MEM_DBG_PARMS_INIT)
#define eaGetStruct(handle, pti, index) eaGetStructVoid(STRUCT_TYPESAFE_PTR_PTR_PTR(pti, handle), pti, index)
#define eaGetStructNoConst(handle, pti, index) eaGetStructVoid(STRUCT_NOCONST_TYPESAFE_PTR_PTR_PTR(pti, handle), pti, index)

//calls estrDestroy on each element of the earray
void    eaClearEString(cEArrayHandle* handle);

//calls estrDestroy on each element of the earray, then destroys the earray
void	eaDestroyEString(EArrayHandle* handle);

//sets the size of an array of EString elements
void	eaSetSizeEString_dbg(EArrayHandle* handle, int size MEM_DBG_PARMS);
#define eaSetSizeEString(handle, size) eaSetSizeEString_dbg(handle, size MEM_DBG_PARMS_INIT)

//returns true if two things match, false otherwise
typedef bool (*DuplicationCompareCB)(void *p1, void *p2);

//goes through an earray, uses above compare func to find any elements that match, removes all but the last, either frees or StructDestroys elements being removed
//(no effort has been made to make this super efficient)
void eaRemoveDuplicates(EArrayHandle* handle, DuplicationCompareCB pCB, ParseTable *pTPI);

//same as above, but before removing something, structOverrides the later one onto the earlier one. 
void eaOverrideAndRemoveDuplicates(EArrayHandle* handle, DuplicationCompareCB pCB, ParseTable *pTPI);

EArrayHandle eaTemp(void* ptr);	// convert temporarily, returns static buffer

#if _PS3

void qsort_s (
    void *base,
    size_t num,
    size_t width,
    int (*comp)(void *, const void *, const void *),
    void *context
    );

#endif

// Search and sorting macros

#define eaQSortVerify(handle)							(0?((*(handle)=NULL),0):0)
#define eaQSort(handle, comparator)						(eaQSortVerify(handle),((handle)?(qsort((void*)(handle), eaSize(&(handle)), sizeof((handle)[0]), (comparator)),0):0))
#define eaQSortCPP(handle, comparator, size)			(eaQSortVerify(handle),((handle)?(qsort((void*)(handle), size, sizeof((handle)[0]), (comparator)),0):0))
#define eaQSortCPP_s(handle, comparator, size, context)	(eaQSortVerify(handle),qsort_s((void*)(handle), size, sizeof((handle)[0]), (comparator), (context)))
#define eaQSort_s(handle, comparator, context)			(eaQSortVerify(handle),qsort_s((void*)(handle), eaSize(&(handle)), sizeof((handle)[0]), (comparator), (context)))
#define eaQSortG(handle, comparator)					(eaQSortVerify(handle),qsortG((void*)(handle), eaSize(&(handle)), sizeof((handle)[0]), (comparator)))
#define eaQSortG_s(handle, comparator,context)			(eaQSortVerify(handle),qsortG_s((void*)(handle), eaSize(&(handle)), sizeof((handle)[0]), (comparator), (context)))
//now uses mergeSort.
#define eaStableSort(handle, context, comparator)		(eaQSortVerify(handle),((handle)?(stableSort((void*)(handle), eaSize(&(handle)), sizeof((handle)[0]), context, (comparator)),0):0))
#define eaBSearch(handle, comparator, key)				bsearch(&(key), (handle), eaSize(&(handle)), sizeof((handle)[0]), (comparator))
#define eaBSearchCPP(handle, comparator, key, size)		bsearch(&(key), (handle), size, sizeof((handle)[0]), (comparator))
#define eaBSearch_s(handle, comparator, context, key)	bsearch_s(&(key), (handle), eaSize(&(handle)), sizeof((handle)[0]), (comparator), (context))
#define eaBFind(handle, comparator, key)				bfind(&(key), (handle), eaSize(&(handle)), sizeof((handle)[0]), (comparator))
#define eaBFind_s(handle, comparator, context, key)		bfind_s(&(key), (handle), eaSize(&(handle)), sizeof((handle)[0]), (comparator), (context))

// Indexed EArray support, which is for dealing with earrays of textparser structs that have a key

// Sort an earray, using the keyfield. Can be used on any EArray
int eaSortUsingKeyVoid(cEArrayHandle *handle, ParseTable pti[]);
#define eaSortUsingKey(handle, pti) eaSortUsingKeyVoid(STRUCT_TYPESAFE_PTR_PTR_PTR(pti,handle), pti)

// This function uses mergeSort O(n log n) and requires a scratch swap that is half the size of the array (rounding up to power of 2)
int eaStableSortUsingColumnVoid(cEArrayHandle *handle, ParseTable pti[], int col);
#define eaStableSortUsingColumn(handle, pti, col) eaStableSortUsingColumnVoid(STRUCT_TYPESAFE_PTR_PTR_PTR(pti,handle), pti, col)

// Returns the parsetable associated with an indexed earray, or NULL if the earray is not indexed
void *eaIndexedGetTable(cEArrayHandle *handle);

// Turn on indexed mode, which asserts on invalid actions, and makes eaPush do IndexedAdd
// When in this mode, certain operations like eaRemoveFast will assert
int eaIndexedEnable_dbg(cEArrayHandle *handle, ParseTable pti[] MEM_DBG_PARMS);
#define eaIndexedEnableVoid(handle,pti) eaIndexedEnable_dbg(handle,pti MEM_DBG_PARMS_INIT)
#define eaIndexedEnable(handle,pti) eaIndexedEnableVoid(STRUCT_TYPESAFE_PTR_PTR_PTR(pti,handle),pti)
#define eaIndexedEnableNoConst(handle,pti) eaIndexedEnableVoid(STRUCT_NOCONST_TYPESAFE_PTR_PTR_PTR(pti,handle),pti)
#define seaIndexedEnableVoid(handle,pti) eaIndexedEnable_dbg(handle,pti MEM_DBG_PARMS_CALL)
#define seaIndexedEnable(handle,pti) seaIndexedEnableVoid(STRUCT_TYPESAFE_PTR_PTR_PTR(pti,handle),pti)
#define steaIndexedEnableVoid(handle,pti,mem_struct_ptr) eaIndexedEnable_dbg(handle,pti MEM_DBG_STRUCT_PARMS_CALL(mem_struct_ptr))
#define steaIndexedEnable(handle,pti,mem_struct_ptr) steaIndexedEnableVoid(STRUCT_TYPESAFE_PTR_PTR_PTR(pti,handle),pti,mem_struct_ptr)

// Turn off indexed mode
int eaIndexedDisable_dbg(cEArrayHandle *handle MEM_DBG_PARMS);
#define eaIndexedDisable(handle) eaIndexedDisable_dbg(handle MEM_DBG_PARMS_INIT)

// Clean up an indexed earray template.
void eaIndexedTemplateDestroy(ParseTable pti[]);

// Add a substructure to an earray. This only works on already indexed EArrays
int eaIndexedAdd_dbg(cEArrayHandle *handle, const void *substructure MEM_DBG_PARMS);
#define eaIndexedAdd(handle,substructure) eaIndexedAdd_dbg(handle,substructure MEM_DBG_PARMS_INIT)
#define seaIndexedAdd(handle,substructure) eaIndexedAdd_dbg(handle,substructure MEM_DBG_PARMS_CALL)
#define steaIndexedAdd(handle,substructure,mem_struct_ptr) eaIndexedAdd_dbg(handle,substructure MEM_DBG_STRUCT_PARMS_CALL(mem_struct_ptr))

// This assumes that you already know the correct index. Use eaIndexedAdd to be safer
#define eaIndexedInsert(handle,structptr,i) eaInsert_dbg(handle,structptr,i,true MEM_DBG_PARMS_INIT)
#define seaIndexedInsert(handle,structptr,i) eaInsert_dbg(handle,structptr,i,true MEM_DBG_PARMS_CALL)
#define steaIndexedInsert(handle,structptr,i,mem_struct_ptr) eaInsert_dbg(handle,structptr,i,true MEM_DBG_STRUCT_PARMS_CALL(mem_struct_ptr))

// Find the index of a struct with the same key
int eaIndexedFind(cEArrayHandle * handle, const void *substructure);

// Get a structure from an indexed earray, given an int or string key
int eaIndexedFindUsingInt(ccEArrayHandle* handle, S64 key);
int eaIndexedFindUsingString(ccEArrayHandle* handle, const char* key);

void *eaIndexedGetUsingInt(ccEArrayHandle* handle, S64 key);
void *eaIndexedGetUsingString(ccEArrayHandle* handle, const char* key);

#define eaIndexedGetUsingInt_FailOnNULL eaIndexedGetUsingInt
#define eaIndexedGetUsingString_FailOnNULL eaIndexedGetUsingString

void *eaIndexedRemoveUsingInt(cEArrayHandle* handle, S64 key);
void *eaIndexedRemoveUsingString(cEArrayHandle* handle, const char* key);

#define eaIndexedRemoveUsingInt_FailOnNULL eaIndexedRemoveUsingInt
#define eaIndexedRemoveUsingString_FailOnNULL eaIndexedRemoveUsingString


//special functions that AUTO_TRANS recognizes... the key passed in must be the same as
//the key of the object, obviously. Returns true if an element with that key was not 
//already in the array, and thus the new one was pushed
bool eaIndexedPushUsingStringIfPossible_dbg(cEArrayHandle* handle, const char *key, const void *substructure MEM_DBG_PARMS);
#define eaIndexedPushUsingStringIfPossible(handle, key, substructure) eaIndexedPushUsingStringIfPossible_dbg(handle, key, substructure MEM_DBG_PARMS_INIT)

//note (also potentially applies to other usingInt functions): because keys get cast to S64, there's some chance that you'll have to
//make sure you don't mix and match S32s and U32s here. Make sure to test your high-bit-set cases separately, in any case.
bool eaIndexedPushUsingIntIfPossible_dbg(cEArrayHandle* handle, S64 key, const void *substructure MEM_DBG_PARMS);
#define eaIndexedPushUsingIntIfPossible(handle, key, substructure) eaIndexedPushUsingIntIfPossible_dbg(handle, key, substructure MEM_DBG_PARMS_INIT)



typedef void (*eaMergeCallback)(EArrayHandle *handle, int index, void *pUserData);

// Merge two indexed lists together, into the first. Destroys the second list.
// If mergeCB is set, it gets called whenever one of the input items is added to the first array
int eaIndexedMerge_dbg(EArrayHandle *handle, EArrayHandle *extraHandle, eaMergeCallback mergeCB, void *pUserData MEM_DBG_PARMS);
#define eaIndexedMerge(handle, extraHandle, mergeCB, pUserData) eaIndexedMerge_dbg(handle, extraHandle, mergeCB, pUserData MEM_DBG_PARMS_INIT)

#define eaContains(handle,val) (-1!=eaFind(handle,val))


///////////////////////////////////////////////////////////////////////////////////////////////////
//oddball thing which could live here or in textparser
//
//given an earray of structs and a TPI, put then into an estring
void eaStructArrayToString_dbg(EArrayHandle *handle, ParseTable *pti, char **ppOutString  MEM_DBG_PARMS);
#define eaStructArrayToString(handle, pti, ppOutString) eaStructArrayToString_dbg(handle, pti, ppOutString MEM_DBG_PARMS_INIT)
//
//reverse the above process
void eaStructArrayFromString_dbg(EArrayHandle *handle, ParseTable *pti, char *pInString MEM_DBG_PARMS);
#define eaStructArrayFromString(handle, pti, pInString) eaStructArrayFromString_dbg(handle, pti, pInString MEM_DBG_PARMS_INIT)

//////////////////////////////////////////////////// EArray32's (int, f32)
// you will usually declare these as MyStruct**, and then use them to
// store pointers to stuff of interest.  

typedef U32* EArray32Handle;									// pointer to a list of ints
typedef const U32* cEArray32Handle;								// non-const pointer to a non-const list of const ints
typedef const U32* const ccEArray32Handle;						// const pointer to a const list of const ints

#define EArray32FromHandle(handle)						EArrayTypeFromHandle(32, handle)
#define HandleFromEArray32(array)						HandleFromEArrayType(32, array)

__forceinline static void ea32StackInit(cEArray32Handle* handle, EArray32 *pArray, int size)
{
	assert(size > 0);
	memset(pArray->values, 0, size * sizeof(pArray->values[0]));
	pArray->count = 0;
	pArray->size = -size;
	*handle = (cEArray32Handle)HandleFromEArray32(pArray);
}

#define ea32StackCreate(handle,size)												\
{																					\
	EArray32 *pArray = alloca(sizeof(EArray32) + size * sizeof(pArray->values[0]));	\
	ea32StackInit((cEArray32Handle*)handle, pArray, size);											\
}

void	ea32Create_dbg(EArray32Handle* handle MEM_DBG_PARMS);
#define ea32Create(handle) ea32Create_dbg(handle MEM_DBG_PARMS_INIT)
void	ea32Destroy(EArray32Handle* handle);				// free list
void	ea32SetSize_dbg(EArray32Handle* handle, int size MEM_DBG_PARMS);		// grows or shrinks to i, adds NULL entries if required
#define ea32SetSize(handle,size) ea32SetSize_dbg(handle,size MEM_DBG_PARMS_INIT)
void	ea32SetSizeFast_dbg(EArray32Handle* handle, int size MEM_DBG_PARMS);		// grows or shrinks to i, does NOT zero memory
#define ea32SetSizeFast(handle,size) ea32SetSizeFast_dbg(handle,size MEM_DBG_PARMS_INIT)
// int	ea32Size(EArrayHandle* handle);						// #defined below
void	ea32SetCapacity_dbg(EArray32Handle* handle, int capacity MEM_DBG_PARMS);	// set the current capacity to size, may reduce size
#define ea32SetCapacity(handle, size) ea32SetCapacity_dbg(handle,size MEM_DBG_PARMS_INIT)
int		ea32Capacity(ccEArray32Handle* handle);				// get the current number of items that can be held without growing
size_t	ea32MemUsage(ccEArray32Handle* handle, bool bAbsoluteUsage);				// get the amount of memory actually used (not counting slack allocated)
void	ea32Compress_dbg(EArray32Handle *dst, ccEArray32Handle *src, CustomMemoryAllocator memAllocator, void *customData MEM_DBG_PARMS);
#define ea32Compress(dst, src, memAllocator, customData) ea32Compress_dbg(dst, src, memAllocator, customData MEM_DBG_PARMS_INIT)

int		ea32Push_dbg(EArray32Handle* handle, U32 value MEM_DBG_PARMS);		// add to the end of the list, returns the index it was added at (the new size)
#define ea32Push(handle,value) ea32Push_dbg(handle,value MEM_DBG_PARMS_INIT)
#define sea32Push(handle,value) ea32Push_dbg(handle,value MEM_DBG_PARMS_CALL)
#define stea32Push(handle,value,mem_struct_ptr) ea32Push_dbg(handle,value MEM_DBG_STRUCT_PARMS_CALL(mem_struct_ptr))
int		ea32PushUnique_dbg(EArray32Handle* handle, U32 value MEM_DBG_PARMS);	// add to the end of the list if not already in the list, returns the index it was added at (the new size)
#define ea32PushUnique(handle,value) ea32PushUnique_dbg(handle,value MEM_DBG_PARMS_INIT)
int		ea32PushArray_dbg(EArray32Handle* handle, ccEArray32Handle* src MEM_DBG_PARMS); // add an earray to the end of the list, returns the index it was added at
#define ea32PushArray(handle,src) ea32PushArray_dbg(handle,src MEM_DBG_PARMS_INIT)
U32		ea32Pop(EArray32Handle* handle);					// remove the last item from the list
void	ea32Reverse(EArray32Handle* handle);
void	ea32Clear(EArray32Handle* handle);					// sets all elements to 0
void	ea32ClearFast(EArray32Handle* handle);				// sets size to 0 but doesn't memset
void	ea32Copy_dbg(EArray32Handle* dest, ccEArray32Handle* src MEM_DBG_PARMS);// create dest if needed, copy pointers
#define ea32Copy(dest,src) ea32Copy_dbg(dest,src MEM_DBG_PARMS_INIT)
void	ea32Append_dbg(EArray32Handle* dest, ccEArray32Handle* src MEM_DBG_PARMS); // create dest if needed, copy pointers
#define ea32Append(dest,src) ea32Append_dbg(dest,src MEM_DBG_PARMS_INIT)
void	ea32Prepend_dbg(EArray32Handle* dest, ccEArray32Handle* src MEM_DBG_PARMS); // create dest if needed, copy pointers
#define ea32Prepend(dest,src) ea32Prepend_dbg(dest,src MEM_DBG_PARMS_INIT)

void	ea32Set_dbg(EArray32Handle* handle, U32 value, int i MEM_DBG_PARMS);	// set i'th element (zero-based)
#define ea32Set(handle, value, i) ea32Set_dbg(handle, value, i MEM_DBG_PARMS_INIT)
U32		ea32Get(ccEArray32Handle* handle, int i);				// get i'th element (zero-based), NULL on error
void	ea32Insert_dbg(EArray32Handle* handle, U32 value, int i MEM_DBG_PARMS); // insert before i'th position, will not insert on error (i == -1, etc.)
#define ea32Insert(handle,value,i) ea32Insert_dbg(handle,value,i MEM_DBG_PARMS_INIT)
U32		ea32Remove(EArray32Handle* handle, int i);			// remove the i'th element, NULL on error
U32		ea32RemoveFast(EArray32Handle* handle, int i);		// remove the i'th element, and move the last element into this place DOES NOT KEEP ORDER
void	ea32RemoveRange(EArray32Handle* handle, int start, int count); // remove count elements, starting with start
int		ea32Find(ccEArray32Handle* handle, U32 value);	// find the first element that matches structptr, returns -1 on error
int		ea32FindAndRemove(EArray32Handle* handle, U32 value); // finds element, deletes it, returns same value as eaFind
int		ea32FindAndRemoveFast(EArray32Handle* handle, U32 value); // finds element, deletes it, returns same value as eaFind
void	ea32Swap(EArray32Handle* handle, int i, int j);		// exchange the i'th element with the j'th element
void	ea32Move(EArray32Handle* handle, int dest, int src);// shift left or right to move the src'th element to dest
int     ea32BInsert_dbg(EArray32Handle* handle, U32 value MEM_DBG_PARMS); // insert into the sorted position
int     ea32BInsertUnique_dbg(EArray32Handle* handle, U32 value MEM_DBG_PARMS); // inserts a unique value into the sorted position
U32     ea32BFindAndRemove(EArray32Handle* handle, U32 value);

void ea32Randomize(int **ppArray);


//for these two functions, "bTrustworthy" should be false if the string came from, or being sent over,
//an untrustworthy net link.
//always succeeds
void ea32ToZipString_dbg(EArray32Handle *handle, SA_PARAM_OP_VALID char **ppOutEString, bool bTrustworthy MEM_DBG_PARMS);
#define ea32ToZipString(handle, ppOutEString, bTrustworthy) ea32ToZipString_dbg(handle, ppOutEString, bTrustworthy MEM_DBG_PARMS_INIT)

//returns true on success, false on failure
bool ea32FromZipString_dbg(EArray32Handle *handle, char *pInString, bool bTrustworthy MEM_DBG_PARMS);
#define ea32FromZipString(handle, pInString, bTrustworthy) ea32FromZipString_dbg(handle, pInString, bTrustworthy MEM_DBG_PARMS_INIT)

EArray32Handle ea32Temp(U32 value);							// convert temporarily, returns static buffer

#define ea32QSort(earray, comparator) if (earray) qsort((earray), ea32Size(&(earray)), sizeof((earray)[0]), (comparator))
#define ea32BFind(earray, comparator, key) bfind(&(key), (earray), ea32Size(&(earray)), sizeof((earray)[0]), (comparator))

//utility function... given a string containing some number of unsigned ints separated by non-digits, pushes them all
//into the given earray
void	ea32PushUIntsFromString_dbg(EArray32Handle *handle, char *pString MEM_DBG_PARMS);
#define ea32PushUIntsFromString(handle, pString) ea32PushUIntsFromString_dbg(handle, pString MEM_DBG_PARMS_INIT)

//This returns 0 on underflow. Not the best behavior.
U32 ea32Tail(EArray32Handle* handle);
#define ea32Contains(handle,val) (-1!=ea32Find(handle,val))


//functions for sorted lists of ea32s.
//looks for a specified int in a sorted list of U32s. Returns true if it found it, and the index. Returns false if it didn't find it,
//and the index of where it would have to be inserted.
bool ea32SortedFindIntOrPlace(EArray32Handle *handle, U32 iVal, int *pOutPlace);

//////////////////////////////////////////////////// EArray64's (U64, F64)
// you will usually declare these as U64* or F64*, and then use them to
// store pointers to stuff of interest.  

typedef U64* EArray64Handle;									// pointer to a list of 64 bit ints
typedef const U64* cEArray64Handle;								// non-const pointer to a non-const list of const 64 bit ints
typedef const U64* const ccEArray64Handle;						// const pointer to a const list of const 64 bit ints

#define EArray64FromHandle(handle)						EArrayTypeFromHandle(64, handle)
#define HandleFromEArray64(array)						HandleFromEArrayType(64, array)

__forceinline static void ea64StackInit(cEArray64Handle* handle, EArray64 *pArray, int size)
{
	assert(size > 0);
	memset(pArray->values, 0, size * sizeof(pArray->values[0]));
	pArray->count = 0;
	pArray->size = -size;
	*handle = (cEArray64Handle)HandleFromEArray64(pArray);
}

#define ea64StackCreate(handle,size)												\
{																					\
	EArray64 *pArray = alloca(sizeof(EArray64) + size * sizeof(pArray->values[0]));	\
	ea64StackInit(handle, pArray, size);											\
}

void	ea64Create_dbg(cEArray64Handle* handle MEM_DBG_PARMS);
#define ea64Create(handle) ea64Create_dbg(handle MEM_DBG_PARMS_INIT)
void	ea64Destroy(EArray64Handle* handle);				// free list
void	ea64SetSize_dbg(EArray64Handle* handle, int size MEM_DBG_PARMS);		// grows or shrinks to i, adds NULL entries if required
#define ea64SetSize(handle,size) ea64SetSize_dbg(handle,size MEM_DBG_PARMS_INIT)
// int	ea64Size(EArrayHandle* handle);						// #defined below
void	ea64SetCapacity_dbg(EArray64Handle* handle, int capacity MEM_DBG_PARMS);	// set the current capacity to size, may reduce size
#define ea64SetCapacity(handle, size) ea64SetCapacity_dbg(handle,size MEM_DBG_PARMS_INIT)
int		ea64Capacity(ccEArray64Handle* handle);				// get the current number of items that can be held without growing
size_t	ea64MemUsage(ccEArray64Handle* handle, bool bAbsoluteUsage);				// get the amount of memory actually used (not counting slack allocated)
void	ea64Compress_dbg(EArray64Handle *dst, ccEArray64Handle *src, CustomMemoryAllocator memAllocator, void *customData MEM_DBG_PARMS);
#define ea64Compress(dst, src, memAllocator, customData) ea64Compress_dbg(dst, src, memAllocator, customData MEM_DBG_PARMS_INIT)

int		ea64Push_dbg(EArray64Handle* handle, U64 value MEM_DBG_PARMS);		// add to the end of the list, returns the index it was added at (the new size)
#define ea64Push(handle,value) ea64Push_dbg(handle,value MEM_DBG_PARMS_INIT)
int		ea64PushUnique_dbg(EArray64Handle* handle, U64 value MEM_DBG_PARMS);	// add to the end of the list if not already in the list, returns the index it was added at (the new size)
#define ea64PushUnique(handle,value) ea64PushUnique_dbg(handle,value MEM_DBG_PARMS_INIT)
int		ea64PushArray_dbg(EArray64Handle* handle, ccEArray64Handle* src MEM_DBG_PARMS); // add an earray to the end of the list, returns the index it was added at
#define ea64PushArray(handle,src) ea64PushArray_dbg(handle,src MEM_DBG_PARMS_INIT)
U64		ea64Pop(EArray64Handle* handle);					// remove the last item from the list
void	ea64Reverse(EArray64Handle* handle);
void	ea64Clear(EArray64Handle* handle);					// sets all elements to 0
void	ea64Copy_dbg(EArray64Handle* dest, ccEArray64Handle* src MEM_DBG_PARMS);// create dest if needed, copy pointers
#define ea64Copy(dest,src) ea64Copy_dbg(dest,src MEM_DBG_PARMS_INIT)

void	ea64Set_dbg(EArray64Handle* handle, U64 value, int i MEM_DBG_PARMS);	// set i'th element (zero-based)
#define ea64Set(handle, value, i) ea64Set_dbg(handle, value, i MEM_DBG_PARMS_INIT)
U64		ea64Get(ccEArray64Handle* handle, int i);				// get i'th element (zero-based), NULL on error
void	ea64Insert_dbg(EArray64Handle* handle, U64 value, int i MEM_DBG_PARMS); // insert before i'th position, will not insert on error (i == -1, etc.)
#define ea64Insert(handle,value,i) ea64Insert_dbg(handle,value,i MEM_DBG_PARMS_INIT)
U64		ea64Remove(EArray64Handle* handle, int i);			// remove the i'th element, NULL on error
U64		ea64RemoveFast(EArray64Handle* handle, int i);		// remove the i'th element, and move the last element into this place DOES NOT KEEP ORDER
int		ea64Find(ccEArray64Handle* handle, U64 value);	// find the first element that matches structptr, returns -1 on error
int		ea64FindAndRemove(EArray64Handle* handle, U64 value); // finds element, deletes it, returns same value as eaFind
void	ea64Swap(EArray64Handle* handle, int i, int j);		// exchange the i'th element with the j'th element
void	ea64Move(EArray64Handle* handle, int dest, int src);// shift left or right to move the src'th element to dest

EArray64Handle ea64Temp(U64 value);							// convert temporarily, returns static buffer

#define ea64QSort(earray, comparator) if (earray) qsort((earray), ea64Size(&(earray)), sizeof((earray)[0]), (comparator))
#define ea64BFind(earray, comparator, key) bfind(&(key), (earray), ea64Size(&(earray)), sizeof((earray)[0]), (comparator))

//utility function... given a string containing some number of unsigned ints separated by non-digits, pushes them all
//into the given earray
void	ea64PushUIntsFromString_dbg(EArray64Handle *handle, char *pString MEM_DBG_PARMS);
#define ea64PushUIntsFromString(handle, pString) ea64PushUIntsFromString_dbg(handle, pString MEM_DBG_PARMS_INIT)

//This returns 0 on underflow. Not the best behavior.
U64 ea64Tail(EArray64Handle* handle);

#define ea64Contains(handle,val) (-1!=ea64Find(handle,val))

/////////////////////////////////////////////////// StringArray util
//   These are some extra util functions for char** that are EArrays
//   They assume you want case-insensitivity

typedef char** StringArray;
int StringArrayFind(const char** array, const char* elem);	// returns index, or -1
int StringArrayNFind(const char ** array, const char* elem); //returns index, or -1
int StringArrayIntersection(char*** result, char** lhs, char** rhs);  // dumb implementation, just for small arrays
void StringArrayPrint(char* buf, int buflen, char** array);	// makes "(str, str, str)"
void StringArrayJoin(const char** array, const char* join, char** result);

////////////////////////////////////////////////////////// private


__forceinline static U32 U32fromF32(F32 x)			{	return *((U32 *) (void*)&x);	}
__forceinline static F32 F32fromU32(U32 x)			{	return *((F32 *) (void*)&x);	}

__forceinline static U64 U64fromF64(F64 x)			{	return *((U64 *) (void*)&x);	}
__forceinline static F64 F64fromU64(U64 x)			{	return *((F64 *) (void*)&x);	}

#define eaCheckNonConst(x)							(0?((*(x) = 0),1):1)

// Generic-size type #defines.

#define eaTypeVerify(t, hand)							((1/((sizeof(**(hand)) == sizeof(U##t))?1:0)) && (0?((**(hand)==1),eaCheckNonConst(*(hand)),eaCheckNonConst((hand)),0):1))
#define eaTypeVerifyConst(t, hand)						((1/((sizeof(**(hand)) == sizeof(U##t))?1:0)) && (0?((**(hand)==1)):1))
#define eaTypeNoParams(t, func, hand)					(ea##t##Verify(hand), func((EArray##t##Handle*)hand))
#define eaTypeNoParamsConst(t, func, hand)				(ea##t##VerifyConst(hand), func((ccEArray##t##Handle*)hand))
#define eaTypeChkConstReturnVoid(t, func, hand, ...)	(ea##t##VerifyConst(hand), func((ccEArray##t##Handle*)hand, ##__VA_ARGS__), 0)
#define eaTypeChkReturnVoid(t, func, hand, ...)			(ea##t##Verify(hand), func((EArray##t##Handle*)hand, ##__VA_ARGS__))
#define eaTypeChkConst(t, func, hand, ...)				(ea##t##VerifyConst(hand), func((ccEArray##t##Handle*)hand, ##__VA_ARGS__))
#define eaTypeChk(t, func, hand, ...)					(ea##t##Verify(hand), func((EArray##t##Handle*)hand, ##__VA_ARGS__))
#define eaTypeChkReturnVoid2(t, func, dst, src, ...)	((ea##t##VerifyConst(src) && ea##t##Verify(dst)), func((EArray##t##Handle*) (dst), (cEArray##t##Handle*) (src), ##__VA_ARGS__), 0)
#define eaTypeChk2(t, func, dst, src, ...)				((ea##t##VerifyConst(src) && ea##t##Verify(dst)), func((EArray##t##Handle*) (dst), (cEArray##t##Handle*) (src), ##__VA_ARGS__))

// 32-bit type #defines.

#define ea32Verify(hand)								eaTypeVerify(32, hand)
#define ea32VerifyConst(hand)							eaTypeVerifyConst(32, hand)
#define ea32NoParams(func, hand)						eaTypeNoParams(32, func, hand)
#define ea32NoParamsConst(func, hand)					eaTypeNoParamsConst(32, func, hand)
#define ea32ChkConstReturnVoid(func, hand, ...)			eaTypeChkConstReturnVoid(32, func, hand, ##__VA_ARGS__)
#define ea32ChkReturnVoid(func, hand, ...)				eaTypeChkReturnVoid(32, func, hand, ##__VA_ARGS__)
#define ea32ChkConst(func, hand, ...)					eaTypeChkConst(32, func, hand, ##__VA_ARGS__)
#define ea32Chk(func, hand, ...)						eaTypeChk(32, func, hand, ##__VA_ARGS__)
#define ea32ChkReturnVoid2(func, dst, src, ...)			eaTypeChkReturnVoid2(32, func, dst, src, ##__VA_ARGS__)
#define ea32Chk2(func, dst, src, ...)					eaTypeChk2(32, func, dst, src, ##__VA_ARGS__)

#define ea32Size(handle)								(ea32VerifyConst(handle),eaTypeSize(32, handle))
#define ea32USize(handle)								((U32)ea32Size(handle))

// 64-bit type #defines.

#define ea64Verify(hand)								eaTypeVerify(64, hand)
#define ea64VerifyConst(hand)							eaTypeVerifyConst(64, hand)
#define ea64NoParams(func, hand)						eaTypeNoParams(64, func, hand)
#define ea64NoParamsConst(func, hand)					eaTypeNoParamsConst(64, func, hand)
#define ea64ChkConstReturnVoid(func, hand, ...)			eaTypeChkConstReturnVoid(64, func, hand, ##__VA_ARGS__)
#define ea64ChkReturnVoid(func, hand, ...)				eaTypeChkReturnVoid(64, func, hand, ##__VA_ARGS__)
#define ea64ChkConst(func, hand, ...)					eaTypeChkConst(64, func, hand, ##__VA_ARGS__)
#define ea64Chk(func, hand, ...)						eaTypeChk(64, func, hand, ##__VA_ARGS__)
#define ea64ChkReturnVoid2(func, dst, src, ...)			eaTypeChkReturnVoid2(64, func, dst, src, ##__VA_ARGS__)
#define ea64Chk2(func, dst, src, ...)					eaTypeChk2(64, func, dst, src, ##__VA_ARGS__)

#define ea64Size(handle)								eaTypeSize(64, handle)
#define ea64USize(handle)								((U32)ea64Size(handle))


typedef int* EArrayIntHandle;
typedef F32* EArrayF32Handle;

int eafCompare(const F32*const* array1, const F32*const* array2);
int eaiCompare(const int*const* array1, const int*const* array2);


__forceinline static int eaiSizeSlow(SA_PRE_NN_VALID SA_POST_NN_RELEMS(1) cEArray32Handle *handle)
{
	EArray32* pArray;
	if (!(*handle))
		return 0;
	pArray = EArray32FromHandle(*handle);
	return pArray->count;
}


__forceinline static int eafSizeSlow(SA_PRE_NN_VALID SA_POST_NN_RELEMS(1) cEArrayF32Handle *handle)
{
	EArray32* pArray;
	if (!(*handle))
		return 0;
	pArray = EArray32FromHandle(*handle);
	return pArray->count;
}

#define eaiStackCreate(handle,size)			ea32StackCreate(handle,size)
#define eaiCreate(handle)					ea32ChkReturnVoid( ea32Create_dbg, handle, __FILE__, __LINE__)
#define eaiDestroy(handle)					ea32ChkReturnVoid( ea32Destroy, handle )
#define eaiSetSize(handle, size)			ea32ChkReturnVoid( ea32SetSize_dbg, handle, size, __FILE__, __LINE__)
#define eaiSetSizeFast(handle, size)		ea32ChkReturnVoid( ea32SetSizeFast_dbg, handle, size, __FILE__, __LINE__)
#define seaiSetSize(handle, size)			ea32ChkReturnVoid( ea32SetSize_dbg, handle, size, caller_fname, line)
#define steaiSetSize(handle, size, mem_struct_ptr)		ea32ChkReturnVoid( ea32SetSize_dbg, handle, size, (mem_struct_ptr)->caller_fname, (mem_struct_ptr)->line)
#define eaiSetSize_dbg(handle, size, fname, linenum)	ea32ChkReturnVoid( ea32SetSize_dbg, handle, size, fname, linenum)
#define eaiSize(handle)						ea32NoParamsConst( ea32Size, handle)
#define eaiUSize(handle)					ea32NoParamsConst( ea32USize, handle)
#define eaiSetCapacity(handle, size)		ea32ChkReturnVoid( ea32SetCapacity_dbg, handle, size, __FILE__, __LINE__)
#define eaiCapacity(handle)					ea32Chk( ea32Capacity, handle )
#define eaiMemUsage(handle, absoluteUsage)	ea32Chk( ea32MemUsage, handle, (absoluteUsage))
#define eaiCompress(dst, src, alloc, param)	ea32ChkReturnVoid2(ea32Compress_dbg, dst, src, alloc, param, __FILE__, __LINE__)

#define eaiPush(handle, elem)				ea32Chk( ea32Push, handle, (elem))
#define eaiPush_dbg(handle, elem, fname, linenum)	ea32Chk( ea32Push_dbg, handle, (elem), fname, linenum)
#define seaiPush(handle, elem)				ea32Chk( ea32Push_dbg, handle, (elem), caller_fname, line)
#define steaiPush(handle, elem, mem_struct_ptr)		ea32Chk( ea32Push_dbg, handle, (elem), (mem_struct_ptr)->caller_fname, (mem_struct_ptr)->line)
#define eaiPushEArray(handle, src)			ea32Chk( ea32PushArray, handle, src)
#define eaiPushUnique(handle, elem)			ea32Chk( ea32PushUnique, handle, (elem))
#define eaiPush2(handle, vec)				{ eaiPush(handle, vec[0]); eaiPush(handle, vec[1]); }
#define eaiPush3(handle, vec)				{ eaiPush(handle, vec[0]); eaiPush(handle, vec[1]); eaiPush(handle, vec[2]); }
#define eaiPush4(handle, vec)				{ eaiPush(handle, vec[0]); eaiPush(handle, vec[1]); eaiPush(handle, vec[2]); eaiPush(handle, vec[3]); }

#define eaiPop(handle)						ea32Chk( ea32Pop, handle )
#define eaiReverse(handle)					ea32ChkReturnVoid( ea32Reverse, handle)
#define eaiClear(handle)					ea32ChkReturnVoid( ea32Clear, handle )
#define eaiClearFast(handle)				ea32ChkReturnVoid( ea32ClearFast, handle )
#define eaiCopy(dst, src)					ea32ChkReturnVoid2(ea32Copy_dbg, dst, src, __FILE__, __LINE__)
#define eaiAppend(dst, src)					ea32ChkReturnVoid2(ea32Append_dbg, dst, src, __FILE__, __LINE__)

#define eaiSet(handle, elem, i)				ea32ChkReturnVoid( ea32Set_dbg, handle, (elem), i, __FILE__, __LINE__)
#define eaiGet(handle, i)					ea32ChkConst( ea32Get, handle, i)
#define eaiInsert(handle, elem, i)			ea32Chk( ea32Insert_dbg, handle, (elem), i, __FILE__, __LINE__)
#define eaiRemove(handle, i)				ea32Chk( ea32Remove, handle, i)
#define eaiRemoveFast(handle, i)			ea32Chk( ea32RemoveFast, handle, i)
#define eaiFind(handle, elem)				ea32ChkConst( ea32Find, handle, (elem))
#define eaiFindAndRemove(handle, elem)		ea32Chk( ea32FindAndRemove, handle, (elem))
#define eaiFindAndRemoveFast(handle, elem)	ea32Chk( ea32FindAndRemoveFast, handle, (elem))
#define eaiSwap(handle, i, j)				ea32ChkReturnVoid( ea32Swap, handle, i, j)
#define eaiMove(handle, dest, src)			ea32ChkReturnVoid( ea32Move, handle, dest, src)
#define eaiRemoveRange(handle, i, count)	ea32Chk( ea32RemoveRange, handle, i, count)

#define eaiBInsert(handle, elem)            ea32Chk( ea32BInsert_dbg, handle, (elem), __FILE__, __LINE__)
#define eaiBInsertUnique(handle, elem)      ea32Chk( ea32BInsertUnique_dbg, handle, (elem), __FILE__, __LINE__)
#define eaiBFindAndRemove(handle, elem)     ea32Chk( ea32BFindAndRemove, handle, (elem))
#define eaiBFind(earray, key) bfind(&(key), (earray), eaiSize(&(earray)), sizeof((earray)[0]), (intCmp))
#define eaiQSortG(handle, comparator)		if(handle) qsortG((handle), eaiSize(&(handle)), sizeof((handle)[0]), (comparator))
#define eaiQSort(handle, comparator)		if(handle) qsort((handle), eaiSize(&(handle)), sizeof((handle)[0]), (comparator))
#define eaiQSort_s(handle, comparator, context)	if(handle) qsort_s((handle), eaiSize(&(handle)), sizeof((handle)[0]), (comparator), context)


/////////////////////////////////////////////////// EArrayF32's
// The same internals as EArray32.  Declare as F32*.  

#define eafStackCreate(handle,size)			eaiStackCreate(handle,size)
#define eafCreate(handle)					eaiCreate(handle)
#define eafDestroy(handle)					eaiDestroy(handle)
#define eafSetSize(handle, size)			eaiSetSize(handle, size)
#define eafSize(handle)						eaiSize(handle)
#define eafSetCapacity(handle, size)		eaiSetCapacity(handle, size)
#define eafCapacity(handle)					eaiCapacity(handle)
#define eafMemUsage(handle, absoluteUsage)	eaiMemUsage(handle, absoluteUsage)
#define eafCompress(dst, src, alloc, param)	eaiCompress(dst, src, alloc, param)


#define eafPush(handle, elem)				eaiPush((EArray32Handle*)handle, U32fromF32(elem))
#define eafPush_dbg(handle, elem, fname, linenum)	eaiPush_dbg(handle, U32fromF32(elem), fname, linenum)
#define eafPushUnique(handle, elem)			eaiPushUnique(handle, U32fromF32(elem))
#define eafPush2(handle, vec)				{ eafPush(handle, vec[0]); eafPush(handle, vec[1]); }
#define eafPush3(handle, vec)				{ eafPush(handle, vec[0]); eafPush(handle, vec[1]) ; eafPush(handle, vec[2]); }
#define eafPush4(handle, vec)				{ eafPush(handle, vec[0]); eafPush(handle, vec[1]) ; eafPush(handle, vec[2]) ; eafPush(handle, vec[3]); }
#define eafPop(handle)						F32fromU32( eaiPop(handle) )
#define eafReverse(handle)					eaiReverse(handle)
#define eafClear(handle)					eaiClear(handle)
#define eafClearFast(handle)				eaiClearFast(handle)
#define eafCopy(dst, src)					eaiCopy(dst, src)

#define eafSet(handle, elem, i)				eaiSet(handle, U32fromF32((F32) (elem)), i)
#define eafGet(handle, i)					F32fromU32( eaiGet(handle, i) )
#define eafInsert(handle, elem, i)			eaiInsert( handle, U32fromF32((F32) (elem)), i)
#define eafRemove(handle, i)				F32fromU32( eaiRemove(handle, i) )
#define eafRemoveFast(handle, i)			F32fromU32( eaiRemoveFast(handle, i) )
#define eafFind(handle, elem)				eaiFind(handle, U32fromF32((F32) (elem)))
#define eafFindAndRemove(handle, elem)		eaiFindAndRemove(handle, U32fromF32((F32) (elem)))
#define eafSwap(handle, i, j)				eaiSwap(handle, i, j)
#define eafMove(handle, dest, src)			eaiMove(handle, dest, src)

#define eafQSort(handle)					if(handle) qsort((handle), eafSize(&(handle)), sizeof((handle)[0]), (eafSortComparator))


int eafSortComparator(const float *f1, const float *f2);


///////////////////////////////////////////////////////////////////
// 64 bit earrays

#define eai64StackCreate(handle,size)			ea64StackCreate(handle,size)
#define eai64Create(handle)						ea64ChkReturnVoid( ea64Create, handle )
#define eai64Destroy(handle)					ea64ChkReturnVoid( ea64Destroy, handle )
#define eai64SetSize(handle, size)				ea64ChkReturnVoid( ea64SetSize_dbg, handle, size MEM_DBG_PARMS_INIT)
#define eai64Size(handle)						ea64NoParamsConst( ea64Size, handle)
#define eai64USize(handle)						ea64NoParamsConst( ea64USize, handle)
#define eai64SetCapacity(handle, size)			ea64ChkReturnVoid( ea64SetCapacity_dbg, handle, size MEM_DBG_PARMS_INIT)
#define eai64Capacity(handle)					ea64Chk( ea64Capacity, handle )
#define eai64MemUsage(handle, absoluteUsage)	ea64Chk( ea64MemUsage, handle, (U64 (absoluteUsage)))
#define eai64Compress(dst, src, alloc, param)	ea64ChkReturnVoid2(ea64Compress, dst, src, alloc, param)

#define eai64Push(handle, elem)					ea64Chk( ea64Push, handle, (elem))
#define eai64PushUnique(handle, elem)			ea64Chk( ea64PushUnique, handle, (elem))
#define eai64Pop(handle)						ea64Chk( ea64Pop, handle )
#define eai64Reverse(handle)					ea64ChkReturnVoid( ea64Reverse, handle)
#define eai64Clear(handle)						ea64ChkReturnVoid( ea64Clear, handle )
#define eai64Copy(dst, src)						ea64ChkReturnVoid2(ea64Copy_dbg, dst, src MEM_DBG_PARMS_INIT)

#define eai64Set(handle, elem, i)				ea64ChkReturnVoid( ea64Set, handle, (elem), i)
#define eai64Get(handle, i)						ea64Chk( ea64Get, handle, i)
#define eai64Insert(handle, elem, i)			ea64Chk( ea64Insert_dbg, handle, (elem), i MEM_DBG_PARMS_INIT)
#define eai64Remove(handle, i)					ea64Chk( ea64Remove, handle, i)
#define eai64RemoveFast(handle, i)				ea64Chk( ea64RemoveFast, handle, i)
#define eai64Find(handle, elem)					ea64ChkConst( ea64Find, handle, (elem))
#define eai64FindAndRemove(handle, elem)		ea64Chk( ea64FindAndRemove, handle, (elem))
#define eai64Swap(handle, i, j)					ea64ChkReturnVoid( ea64Swap, handle, i, j)
#define eai64Move(handle, dest, src)			ea64ChkReturnVoid( ea64Move, handle, dest, src)

#define eai64QSort(handle)						if(handle) qsort((handle), eai64Size(&(handle)), sizeof((handle)[0]), (eai64SortComparator))

int eai64SortComparator(const U64 *int1, const U64 *int2);

/////////////////////////////////////////////////// EArrayF64's
// The same internals as EArray64.  Declare as F64*.  

#define eaf64StackCreate(handle,size)			eai64StackCreate(handle,size)
#define eaf64Create(handle)						eai64Create(handle)
#define eaf64Destroy(handle)					eai64Destroy(handle)
#define eaf64SetSize(handle, size)				eai64SetSize(handle, size)
#define eaf64Size(handle)						eai64Size(handle)
#define eaf64SetCapacity(handle, size)			eai64SetCapacity(handle, size)
#define eaf64Capacity(handle)					eai64Capacity(handle)
#define eaf64MemUsage(handle, absoluteUsage)	eai64MemUsage(handle, absoluteUsage)
#define eaf64Compress(dst, src, alloc, param)	eai64Compress(dst, src, alloc, param)


#define eaf64Push(handle, elem)					eai64Push(handle, U64fromF64(elem))
#define eaf64PushUnique(handle, elem)			eai64PushUnique(handle, U64fromF64(elem))
#define eaf64Push3(handle, vec)					{ eaf64Push(handle, vec[0]); eaf64Push(handle, vec[1]) ; eaf64Push(handle, vec[2]); }
#define eaf64Pop(handle)						F64fromU64( eai64Pop(handle) )
#define eaf64Reverse(handle)					eai64Reverse(handle)
#define eaf64Clear(handle)						eai64Clear(handle)
#define eaf64Copy(dst, src)						eai64Copy(dst, src)

#define eaf64Set(handle, elem, i)				ea32Set(handle, U64fromF64((F64) (elem)), i)
#define eaf64Get(handle, i)						F64fromU64( eai64Get(handle, i) )
#define eaf64Insert(handle, elem, i)			eai64Insert( handle, U64fromF64((F64) (elem)), i)
#define eaf64Remove(handle, i)					F64fromU64( eai64Remove(handle, i) )
#define eaf64RemoveFast(handle, i)				F64fromU64( eai64RemoveFast(handle, i) )
#define eaf64Find(handle, elem)					eai64Find(handle, U64fromF64((F64) (elem)))
#define eaf64FindAndRemove(handle, elem)		eai64FindAndRemove(handle, U64fromF64((F64) (elem)))
#define eaf64Swap(handle, i, j)					eai64Swap(handle, i, j)
#define eaf64Move(handle, dest, src)			eai64Move(handle, dest, src)

// Note, runs backwards
#define FOR_EACH_IN_EARRAY(ea, typ, p) { int i##p##Index; for(i##p##Index=eaSize(&(ea))-1; i##p##Index>=0; --i##p##Index) { typ* p = (ea)[i##p##Index];

#define FOR_EACH_IN_EARRAY_INT(ea, typ, p) { int i##p##Index; for(i##p##Index=eaiSize(&(ea))-1; i##p##Index>=0; --i##p##Index) { typ p = (ea)[i##p##Index];

#define FOR_EACH_IN_CONST_EARRAY(ea, typ, p) { int i##p##Index; for(i##p##Index=eaSize(&(ea))-1; i##p##Index>=0; --i##p##Index) { const typ * const p = (ea)[i##p##Index];

#define FOR_EACH_IN_EARRAY_FORWARDS(ea, typ, p) { int ci##p##Num = eaSize(&(ea)); int i##p##Index; for(i##p##Index=0; i##p##Index<ci##p##Num; ++i##p##Index) { typ* p = (ea)[i##p##Index];

#define FOR_EACH_IN_CONST_EARRAY_FORWARDS(ea, typ, p) { int ci##p##Num = eaSize(&(ea)); int i##p##Index; for(i##p##Index=0; i##p##Index<ci##p##Num; ++i##p##Index) { const typ * const p = (ea)[i##p##Index];

#define FOR_EACH_IDX(ea, p) (i##p##Index)

#define FOR_EACH_COUNT(p) (ci##p##Num)

// these only work when going forward
#define FOR_EACH_REMOVE_CURRENT(ea, p) { eaRemove(&(ea), i##p##Index); i##p##Index--; ci##p##Num--; p = NULL; }
#define FOR_EACH_INSERT_BEFORE_CURRENT(ea, p, new_p) { eaInsert(&(ea), new_p, i##p##Index); ci##p##Num++; }

#define FOR_EACH_END } } 

//don't ever call this
void Earray_InitSystem(void);

__forceinline void ea32Clear(EArray32Handle* handle)
{
	if (*handle)
	{
		EArray32* pArray = EArray32FromHandle(*handle);
		pArray->count = 0;
	}
}

__forceinline void ea32ClearFast(EArray32Handle* handle)
{
	if (*handle)
	{
		EArray32* pArray = EArray32FromHandle(*handle);
		pArray->count = 0;
	}
}

__forceinline void eaClear_dbg(cEArrayHandle* handle)
{
	if(*handle)
	{
		EArray* pArray = EArrayFromHandle(*handle);
		pArray->count = 0;
	}
}

__forceinline void eaClearFast(cEArrayHandle* handle)
{
	if(*handle)
	{
		EArray* pArray = EArrayFromHandle(*handle);
	
		pArray->count = 0;
	}
}

C_DECLARATIONS_END

#endif // __EARRAY_H
