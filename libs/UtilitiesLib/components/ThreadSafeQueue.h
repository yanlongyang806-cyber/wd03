#pragma once
GCC_SYSTEM

#ifndef _PS3

//Uses XMCore in the XDK in 32bit, implements for 64bit.

#include "wininclude.h"

// Use our implementation on all Windows platforms, instead of using Xbox on Win32
#if defined(_WIN64) || defined(_WIN32)

typedef void *(*XLockFreeMemoryAllocate)(void *context, DWORD dwSize);
typedef void (*XLockFreeMemoryFree)(void *context, void *pAddress);


typedef struct _XLOCKFREE_CREATE {
	//    DWORD attributes;
	//    PVOID addWaitContext;
	//    PVOID removeWaitContext;
	//    DWORD addWaitTime;
	//    DWORD removeWaitTime;
	//    XLockFreeWait addWait;
	//    XLockFreeWait removeWait;
	DWORD structureSize;
	//    DWORD allocationLength;
	DWORD maximumLength;
	XLockFreeMemoryAllocate allocate;
	XLockFreeMemoryFree free;
	//    PVOID memoryContext;
	} XLOCKFREE_CREATE;

typedef void *XLOCKFREE_HANDLE;



HRESULT XLFQueueCreate(XLOCKFREE_CREATE *info, XLOCKFREE_HANDLE* queue);
void    XLFQueueDestroy(XLOCKFREE_HANDLE queue);

HRESULT XLFQueueAdd(XLOCKFREE_HANDLE queue, void* data);
HRESULT XLFQueueRemove(XLOCKFREE_HANDLE queue, void** data);

BOOL WINAPI XLFQueueIsEmpty(IN XLOCKFREE_HANDLE queue);

HRESULT WINAPI XLFQueueGetEntryCount(IN XLOCKFREE_HANDLE queue, OUT LONG* entries);

////////////////////
// Error codes    //
////////////////////
#define FACILITY_XLOCKFREE 0x31
#define XLOCKFREE_STRUCTURE_FULL                ((HRESULT)0x80310001L)
#define XLOCKFREE_STRUCTURE_EMPTY               ((HRESULT)0x80310002L)
#define XLOCKFREE_INVALID_ACTION                ((HRESULT)0x80310003L) // When releasing a lock the action must be XLF_LOCK_SHARED or XLF_LOCK_EXCLUSIVE
#define XLOCKFREE_INVALID_UNLOCK                ((HRESULT)0x80310004L) 
#define XLOCKFREE_PENDING_UPGRADE               ((HRESULT)0x80310005L)
#define XLOCKFREE_PENDING_EXCLUSIVE_LOCK        ((HRESULT)0x80310006L)
#define XLOCKFREE_PENDING_RECURSIVE_LOCK        ((HRESULT)0x80310007L)


#else //use the xdk lib's thread safe queue

#include "xmcore.h"

#pragma comment(lib, "../../3rdparty/xdk/win32lib/xmcorew.lib")

#endif


#endif