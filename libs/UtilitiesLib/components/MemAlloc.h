#pragma once
GCC_SYSTEM

#define EnterEmergencyMallocBufferMode()
#define LeaveEmergencyMallocBufferMode()

// Place new heaps at the end of the list, but before LAST_HEAP.
// When adding heaps here, add a name for them to HeapNameFromID
typedef enum CRYPTIC_USER_HEAP_ENUM
{
	CRYPTIC_FIRST_HEAP = 10,

	CRYPTIC_CONTAINER_HEAP,	// Heap for use with ObjectDB containers
	CRYPTIC_PACKET_HEAP,	// Heap for use with network packets
	CRYPTIC_CHURN_HEAP,		// Use this heap only for short-lived data
	CRYPTIC_STRUCT_STRING_HEAP, //used for struct strings if -SpecialHeapForStructStrings is set
	CRYPTIC_TSMP_HEAP, //used for thread safe memory pools if -SpecialHeapForTSMPs is set
	CRYPTIC_SUBSCRIPTION_HEAP, // Used to hold the subscription cache


	// Add new heaps just before this line.
	CRYPTIC_END_HEAP,
} CRYPTIC_USER_HEAP;

const char *HeapNameFromID(int special_heap);

// Highest special heap index
#define CRYPTIC_LAST_HEAP (CRYPTIC_END_HEAP - 1)

SA_RET_OP_VALID void *calloc_timed_canfail(size_t num, size_t size, int blockType, bool can_fail, const char *filename, int linenumber);
#define calloc_special_heap(num,size,special_heap) calloc_timed_canfail(num,size,special_heap,false,__FILE__,__LINE__)
#define calloc_canfail(num,size) calloc_timed_canfail(num,size,_NORMAL_BLOCK,true,__FILE__,__LINE__)
#define calloc_canfail_special_heap(num,size,special_heap) calloc_timed_canfail(num,size,special_heap,true,__FILE__,__LINE__)

SA_RET_OP_VALID void *malloc_timed_canfail(size_t size,int blockType,bool can_fail, const char *filename, int linenumber);
#define malloc_special_heap(size,special_heap) malloc_timed_canfail(size,special_heap,false,__FILE__,__LINE__)
#define malloc_canfail(size) malloc_timed_canfail(size,_NORMAL_BLOCK,true,__FILE__,__LINE__)
#define malloc_canfail_special_heap(size,special_heap) malloc_timed_canfail(size,special_heap,true,__FILE__,__LINE__)

SA_RET_OP_VALID void *realloc_timed_canfail(void *p, size_t size,int blockType,bool can_fail, const char *filename, int linenumber);
#define realloc_special_heap(p,size,special_heap) realloc_timed_canfail(p,size,special_heap,false,__FILE__,__LINE__)
#define realloc_canfail(p,size) realloc_timed_canfail(p,size,_NORMAL_BLOCK,true,__FILE__,__LINE__)
#define realloc_canfail_special_heap(p,size,special_heap) realloc_timed_canfail(p,size,special_heap,true,__FILE__,__LINE__)

#if !_PS3
//void EnterEmergencyMallocBufferMode(void);
//void LeaveEmergencyMallocBufferMode(void);
typedef void* (*CRTMallocFunc)(size_t size, int blockType, const char *filename, int linenumber);
typedef void* (*CRTCallocFunc)(size_t num, size_t size, int blockType, const char *filename, int linenumber);
typedef void* (*CRTReallocFunc)(void *data, size_t newSize, int blockType, const char *filename, int linenumber);
typedef void (*CRTFreeFunc)(void *data, int blockType);
void setMemoryAllocators(CRTMallocFunc m, CRTCallocFunc c, CRTReallocFunc r, CRTFreeFunc f);
void enableFastAlloc();
#endif

#if _PS3
#define XMemAllocPersistantPhysical(x)
#define XMemAllocSetBlamee(x)
#else
void XMemAllocPersistantPhysical(bool allowed);
void XMemAllocSetBlamee(const char *blamee);
#endif
