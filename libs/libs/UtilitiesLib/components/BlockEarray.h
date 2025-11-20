#pragma once

#include "memcheck.h"

typedef struct ParseTable ParseTable;
typedef void* (*CustomMemoryAllocator)(void* data, size_t size);

#define BEA_HANDLE(ppHandle) ppHandle, sizeof((*ppHandle)[0])

#define BLOCK_EARRAY_INTERNAL_OVERHEAD 16

typedef enum enumBeaFlags
{
	BEAFLAG_NO_ZERO				= 1 << 0,	//don't zero out new blocks
	BEAPUSHFLAG_NO_STRUCT_INIT	= 1 << 16,	//even though we're providing a TPI, don't do any StructIniting. 
} enumBeaFlags;

//pushes an empty block on the end of the array, memsets it to 0, returns a pointer
void *beaPushEmptyEx(void **ppHandle, U32 iBlockSize, ParseTable *pTPI, enumBeaFlags eFlags, CustomMemoryAllocator memAllocator, void *customData MEM_DBG_PARMS);
#define beaPushEmpty(ppHandle) beaPushEmptyEx(BEA_HANDLE(ppHandle), NULL, 0, NULL, NULL MEM_DBG_PARMS_INIT)
#define beaPushEmptyStruct(ppHandle, pTPI) beaPushEmptyEx(BEA_HANDLE(ppHandle), pTPI, 0, NULL, NULL MEM_DBG_PARMS_INIT)



//returns the number of blocks currently in this array
int beaSize(const void * const *ppHandle);
#define beaUSize(ppHandle) ((U32)beaSize(ppHandle))

//if larger than the current size, fills in zeroes. If smaller, frees them. If expanding, 
//sets capacity equal to size. So if you're going to do pushing, do setCapacity first then setSize
void beaSetSizeEx(void **ppHandle, U32 iBlockSize, ParseTable *pTPI, U32 iNewSize, enumBeaFlags eFlags, CustomMemoryAllocator memAllocator, void *customData, bool force MEM_DBG_PARMS);
#define beaSetSize(ppHandle, iNewSize) beaSetSizeEx(BEA_HANDLE(ppHandle), NULL, iNewSize, 0, NULL, NULL, false MEM_DBG_PARMS_INIT)
#define beaSetSizeStruct(ppHandle, pTPI, iNewSize) beaSetSizeEx(BEA_HANDLE(ppHandle), pTPI, iNewSize, 0, NULL, NULL, false MEM_DBG_PARMS_INIT)

//returns how many blocks worth of allocated memory there are
int beaCapacity(void **ppHandle);

//if bAbsoluteUsage is set, return how much is allocated, otherwise return how much is in active use
int beaMemUsage(void **ppHandle, bool bAbsoluteUsage);

//allocates a certain number of blocks of memory... if larger than the current size, increases. If smaller,
//potentially destroys things already there
void beaSetCapacityEx(void **ppHandle, U32 iBlockSize, ParseTable *pTPI, U32 iNewCapacity, enumBeaFlags eFlags, CustomMemoryAllocator memAllocator, void *customData MEM_DBG_PARMS);
#define beaSetCapacity(ppHandle, iNewCapacity) beaSetCapacityEx(BEA_HANDLE(ppHandle), NULL, iNewCapacity, 0, NULL, NULL MEM_DBG_PARMS_INIT)
#define beaSetCapacityStruct(ppHandle, pTPI, iNewCapacity) beaSetCapacityEx(BEA_HANDLE(ppHandle), pTPI, iNewCapacity, 0, NULL, NULL MEM_DBG_PARMS_INIT)

//returns the byte size of the blocks in this array, or 0 if unininitialized
int beaBlockSize(void **ppHandle);

//destroys the array entirely, optionally doing a destructor or structDeInit on each
//member
void beaDestroyEx(void **ppHandle, U32 iBlockSize, ParseTable *pTPI);
#define beaDestroy(ppHandle) beaDestroyEx(BEA_HANDLE(ppHandle), NULL)
#define beaDestroyStruct(ppHandle, pTPI) beaDestroyEx(BEA_HANDLE(ppHandle), pTPI)

void beaRemoveEx(void **ppHandle, U32 iBlockSize, ParseTable *pTPI, U32 iIndex);
#define beaRemove(ppHandle, iIndex) beaRemoveEx(BEA_HANDLE(ppHandle), NULL, iIndex);
#define beaRemoveStruct(ppHandle, pTPI, iIndex) beaRemoveEx(BEA_HANDLE(ppHandle), pTPI, iIndex);

void beaRemoveFastEx(void **ppHandle, U32 iBlockSize, ParseTable *pTPI, U32 iIndex);
#define beaRemoveFast(ppHandle, iIndex) beaRemoveFastEx(BEA_HANDLE(ppHandle), NULL, iIndex);
#define beaRemoveFastStruct(ppHandle, pTPI, iIndex) beaRemoveFastEx(BEA_HANDLE(ppHandle), pTPI, iIndex);

//inserts a new empty block at the specified index, returns a pointer to it
void* beaInsertEmptyEx(void **ppHandle, U32 iBlockSize, ParseTable *pTPI, U32 iIndex, enumBeaFlags eFlags, CustomMemoryAllocator memAllocator, void *customData MEM_DBG_PARMS);
#define beaInsertEmpty(ppHandle, iIndex) beaInsertEmptyEx(BEA_HANDLE(ppHandle), NULL, iIndex, 0, NULL, NULL MEM_DBG_PARMS_INIT)
#define beaInsertEmptyStruct(ppHandle, pTPI, iIndex) beaInsertEmptyEx(BEA_HANDLE(ppHandle), pTPI, iIndex, 0, NULL, NULL MEM_DBG_PARMS_INIT)
