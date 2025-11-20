#if !PLATFORM_CONSOLE

#include "SharedHeap.h"
#include "SharedMemory.h"
#include "sharedmalloc.h"
#include "StashTable.h"

#include "wininclude.h"
#include "error.h"
#include <stdio.h>
#include "sysutil.h"
#include "strings_opt.h"
#include "file.h"
#include "timing.h"
#include "utils.h"
#include "winutil.h"
#include "utf8.h"

//#define SHARED_HEAP_PARANOID_MODE

#pragma warning(push)
#pragma warning(disable:4028) // parameter differs from declaration

#define MAX_SHARED_HEAP_HASH_TABLE_SIZE 131072
#define SHARED_HEAP_LOCK_LOG_MAX_STRING_LENGTH 256

typedef enum SharedHeapStatus {
	SHARED_HEAP_DISABLED,
	SHARED_HEAP_ENABLED
} SharedHeapStatus;

static SharedHeapStatus shared_heap_status = SHARED_HEAP_ENABLED;

typedef struct SharedHeapLockLog
{
	char cHandleName[256];
	char cFile[256];
	U32 uiLineNum;
	int iPID;
} SharedHeapLockLog;

#define CIRCULAR_LOCK_LOG_SIZE 128
typedef struct SharedHeapMemoryManager
{
	StashTable	mmHashTable;
	mspace		sharedMSpace;
	U32			uiNumPagesAllocated;
	bool bMMLocked;
	bool bEmergencyMutexReleaseLeverPulled;
	SharedHeapLockLog circularLockLog[CIRCULAR_LOCK_LOG_SIZE];
	U32			uiCurrentLockLogIndex;
	size_t		uiTotalAllocated;
} SharedHeapMemoryManager;
 
SharedHeapMemoryManager* sharedHeapMM;


void sharedHeapTurnOff( const char* pcReason )
{
	if ( shared_heap_status == SHARED_HEAP_ENABLED && stricmp(pcReason, "disabled") != 0 )
	{
		if (strstriConst(pcReason, "reloading"))
			printf("Shared heap disabled: %s\n", pcReason );
		else
			Errorf("Shared heap disabled: %s\n", pcReason );
	}
	shared_heap_status = SHARED_HEAP_DISABLED;
}

bool sharedHeapEnabled(void)
{
	return (shared_heap_status == SHARED_HEAP_ENABLED);
}

void* sharedMalloc( size_t bytes )
{
	assert(sharedHeapMM && sharedHeapMM->sharedMSpace );
	return mspace_malloc(sharedHeapMM->sharedMSpace, bytes);
}

void* sharedCalloc( size_t num_elements, size_t bytes )
{
	assert(sharedHeapMM && sharedHeapMM->sharedMSpace );
	return mspace_calloc(sharedHeapMM->sharedMSpace, num_elements, bytes);
}
void sharedFree( void* mem )
{
	assert(sharedHeapMM && sharedHeapMM->sharedMSpace );
	mspace_free(sharedHeapMM->sharedMSpace, mem);
}
static void initSharedHeapLockLog(void)
{
	memset(sharedHeapMM->circularLockLog, 0, sizeof(SharedHeapLockLog) * CIRCULAR_LOCK_LOG_SIZE);
	sharedHeapMM->uiCurrentLockLogIndex = 0;
}
static void pushLockLogging(const char* pcHandleName, const char* pcFile, U32 uiLineNum, int iPID )
{
	SharedHeapLockLog* pLogEntry = &sharedHeapMM->circularLockLog[sharedHeapMM->uiCurrentLockLogIndex];
	if ( pcHandleName )
		strncpy(pLogEntry->cHandleName, pcHandleName, _TRUNCATE);
	else
		strcpy(pLogEntry->cHandleName, "no handle given");
	strncpy(pLogEntry->cFile, pcFile, _TRUNCATE);
	pLogEntry->uiLineNum = uiLineNum;
	pLogEntry->iPID = iPID;

	sharedHeapMM->uiCurrentLockLogIndex = ( sharedHeapMM->uiCurrentLockLogIndex + 1 ) % CIRCULAR_LOCK_LOG_SIZE;
}

SharedHeapHandle* sharedHeapCreateHandle(void)
{

	return sharedMalloc(sizeof(SharedHeapHandle));
}

StashTable createSharedHeapMMHashTable(void)
{
	/*
	StashTable newHashTable = createHashTableInSharedHeap();
	initHashTable(newHashTable, MAX_SHARED_HEAP_HASH_TABLE_SIZE);
	newHashTable = stashTableCreateWithStringKeys(STASHSIZEPLEASE,  StashDefault);
	*/
	return stashTableCreateWithStringKeys(MAX_SHARED_HEAP_HASH_TABLE_SIZE, StashSharedHeap);
}


static HANDLE sharedHeapMMMutex; // we each have one of these
static bool bWeHaveSharedHeapMMMutex;



// Find the shared hash table, and point at it
void initSharedHeapMemoryManager(void)
{
	SharedMemoryHandle* pHandle = NULL;
	SM_AcquireResult ret;

	sharedHeapMMMutex = CreateMutex( 
		NULL,  // no security attributes
		FALSE, // initially not owned
		L"sharedHeapMemoryManagerMutex"
		);  // name of mutex

	if (sharedHeapMMMutex == NULL) {
		sharedHeapMMMutex=INVALID_HANDLE_VALUE;
		MessageBox(NULL, L"Error creating mutex", L"Error", MB_ICONINFORMATION);

		// Give up on it
		sharedHeapTurnOff( "Can't create mutex in initSharedHeapMemoryManager" );
		return;
	}

	// Now, grab that mutex
	WaitForSingleObject( 
		sharedHeapMMMutex,   // handle to mutex
		INFINITE      // Wait for ever
		);   
	bWeHaveSharedHeapMMMutex = true;


	sharedMemoryPushDefaultMemoryAccess( PAGE_READWRITE );
	ret = sharedMemoryAcquire(&pHandle, "sharedHeapMemoryManager");
	if ( ret == SMAR_DataAcquired)
	{
		// Someone already made the shared hash table, and its address is stored
		sharedHeapMM = sharedMemoryGetDataPtr(pHandle);
		sharedMemoryPopDefaultMemoryAccess();
		sharedHeapMM->bMMLocked = false;
		ReleaseMutex(sharedHeapMMMutex);
		bWeHaveSharedHeapMMMutex = false;
	}
	else if ( ret == SMAR_FirstCaller )
	{
		StashTable sharedHeapMMHashTable;
		// Shared hash table not yet created, so we'll do it:
		sharedHeapMM = sharedMemorySetSize(pHandle, sizeof(SharedHeapMemoryManager)); // this sharedMemoryhandle will only hold the address to the shared hash table
		if ( !sharedHeapMM )
		{
			// Give up on it
			free (sharedMemoryGetDataPtr(pHandle));
			sharedHeapTurnOff( "Failed to set shared heap MM size\n" );
			ReleaseMutex(sharedHeapMMMutex);
			bWeHaveSharedHeapMMMutex = false;
			return;
		}


		sharedHeapMM->bMMLocked = true;


		shared_heap_status = SHARED_HEAP_ENABLED; // success
		sharedHeapMM->uiNumPagesAllocated = 0;
		sharedHeapMM->mmHashTable = NULL;

		sharedMemoryUnlock(pHandle);
		sharedMemoryPopDefaultMemoryAccess();

		sharedHeapMM->sharedMSpace = create_mspace(0, 0);
		assert( sharedHeapMM->sharedMSpace );

		sharedHeapMMHashTable = createSharedHeapMMHashTable();

		sharedHeapMM->mmHashTable = sharedHeapMMHashTable;

		initSharedHeapLockLog();

		sharedHeapMM->bMMLocked = false;
		ReleaseMutex(sharedHeapMMMutex);
		bWeHaveSharedHeapMMMutex = false;
	}
	else if ( ret == SMAR_Error )
	{
		sharedHeapTurnOff( "Failed to acquire sharedHeapMemoryManager handle" );
		sharedHeapMM = NULL;
		ReleaseMutex(sharedHeapMMMutex);
		bWeHaveSharedHeapMMMutex = false;
		sharedMemoryPopDefaultMemoryAccess();
		return;
	}

}

SharedHeapAcquireResult sharedHeapAcquireEx(SharedHeapHandle** ppHandle, const char* pcName, const char* pcFile, U32 uiLineNum)
{
	SharedHeapHandle* pImpHandle;

	if ( shared_heap_status == SHARED_HEAP_DISABLED )
		return SHAR_Error;

	assert(ppHandle);

	if ( !sharedHeapMM )
	{
		// Find the shared hash table, and point at it
		initSharedHeapMemoryManager();
	}

	if ( !sharedHeapMM )
	{
		assert(shared_heap_status == SHARED_HEAP_DISABLED );
		return SHAR_Error;
	}

	assert( sharedHeapMM->mmHashTable );

	// Lock it!
	sharedHeapMemoryManagerLockEx( pcName, pcFile, uiLineNum);

	if ( stashFindPointer(sharedHeapMM->mmHashTable, pcName, ppHandle) ) // Ok, the element is already in the hash table
	{
		// If it's in the hash table, it is already allocated.
		// Up the ref count
		(*ppHandle)->uiRefCount++;
		assert((*ppHandle)->uiRefCount > 0);// we haven't looped
		sharedHeapMemoryManagerUnlock();
		return SHAR_DataAcquired;
	}

	// It doesn't already exist in the shared hash table,
	// so allocate it and put it in the hash table

	pImpHandle = sharedHeapCreateHandle();
	if ( !pImpHandle )
	{
		sharedHeapMemoryManagerUnlock();
		return SHAR_Error;
	}

	pImpHandle->data = NULL;
	assert( strlen(pcName) < MAX_HEAP_REF_STRING_LENGTH );
	strncpy(pImpHandle->pcName, pcName, _TRUNCATE);
	pImpHandle->uiRefCount = 0;
	pImpHandle->uiSize = 0;

	stashAddPointer(sharedHeapMM->mmHashTable, pImpHandle->pcName, pImpHandle, false);

	*ppHandle = pImpHandle;

	return SHAR_FirstCaller;
}


bool sharedHeapAlloc(SharedHeapHandle* pHandle, size_t size)
{
	assert( pHandle );
	assert( pHandle && pHandle->data == NULL);
	assert(size);
	pHandle->data = sharedMalloc(size);
//	assert(pHandle->data);
	if ( !pHandle->data )
	{
		// Free the handle
		stashRemovePointer(sharedHeapMM->mmHashTable, pHandle->pcName, NULL);
		sharedFree( pHandle );
		return false;
	}
	pHandle->uiRefCount = 1;
	pHandle->uiSize = size;
	sharedHeapMM->uiTotalAllocated += size;
	return true;
}


void sharedHeapRelease(SharedHeapHandle* pHandle )
{
	assert( pHandle );
	assert(pHandle->uiRefCount > 0);// we haven't looped
	pHandle->uiRefCount--;
	if ( pHandle->uiRefCount == 0 )
	{
		// Free it
		assert( pHandle->data && pHandle->pcName[0] );
		sharedHeapMM->uiTotalAllocated -= pHandle->uiSize;
		stashRemovePointer(sharedHeapMM->mmHashTable, pHandle->pcName, NULL);
		sharedFree( pHandle->data );
		sharedFree( pHandle );
	}

}

void sharedHeapReleaseNeverAllocated(SharedHeapHandle* pHandle )
{
	assert( pHandle );
	assert(pHandle->uiRefCount == 0);
	{
		// Free it
		assert( pHandle->pcName[0] );
		sharedHeapMM->uiTotalAllocated -= pHandle->uiSize;
		stashRemovePointer(sharedHeapMM->mmHashTable, pHandle->pcName, NULL);
		sharedFree( pHandle->data );
		sharedFree( pHandle );
	}
}


typedef struct SharedMemoryHandleLL
{
	struct SharedMemoryHandleLL* pNext;
	SharedMemoryHandle* pHandle;
} SharedMemoryHandleLL;

static U32 uiNumLocalSharedPagesAllocated;
static SharedMemoryHandleLL* pFirstLLHandle;

void sharedHeapPushPageHandleLL( SharedMemoryHandle* pNewPageHandle )
{
	SharedMemoryHandleLL* pLLHandle = pFirstLLHandle;
	if ( !pFirstLLHandle )
	{
		pFirstLLHandle = malloc(sizeof(SharedMemoryHandleLL));
		pFirstLLHandle->pHandle = pNewPageHandle;
		pFirstLLHandle->pNext = NULL;
		return;
	}

	assert( pLLHandle );
	while ( pLLHandle->pNext )
	{
		pLLHandle = pLLHandle->pNext;
	}
	pLLHandle->pNext = malloc(sizeof(SharedMemoryHandleLL));
	pLLHandle->pNext->pHandle = pNewPageHandle;
	pLLHandle->pNext->pNext = NULL;
}

void sharedHeapSetPermissionsAllPages( DWORD dwPageAccess )
{
	// Make all pages readonly
	if ( uiNumLocalSharedPagesAllocated > 0 )
	{
		DWORD dwDummy;
		SharedMemoryHandleLL* pLLHandle = pFirstLLHandle;
		assert( pLLHandle );
		while ( pLLHandle )
		{
			VirtualProtect(sharedMemoryGetDataPtr(pLLHandle->pHandle), sharedMemoryGetSize(pLLHandle->pHandle), dwPageAccess, &dwDummy);
			pLLHandle = pLLHandle->pNext;
		}
	}
}





const char* makeSharedPageName( U32 uiNumpage )
{
	static char cNameStorage[64];
	sprintf(cNameStorage, "sharedHeapAllocPage%d", uiNumpage);
	return cNameStorage;
}

void sharedHeapMemoryManagerLockEx( const char* pcHandleName, const char* pcFile, U32 uiLine)
{
	assert( sharedHeapMMMutex );
	WaitForSingleObject( 
		sharedHeapMMMutex,   // handle to mutex
		INFINITE
		);   // Wait for ever
	sharedHeapMM->bMMLocked = true;
	bWeHaveSharedHeapMMMutex = true;

	// The last to lock it!
	pushLockLogging(pcHandleName, pcFile, uiLine, _getpid());

	// Make all known pages readwrite
#ifdef SHARED_HEAP_PARANOID_MODE
	sharedHeapSetPermissionsAllPages( PAGE_READWRITE );
#endif
	
	// Get up to date with mapped pages
	sharedMemoryPushDefaultMemoryAccess(PAGE_READWRITE);
	while ( uiNumLocalSharedPagesAllocated < sharedHeapMM->uiNumPagesAllocated )
	{
		SharedMemoryHandle* pHandle = NULL;

		SM_AcquireResult ret = sharedMemoryAcquire( &pHandle, makeSharedPageName(uiNumLocalSharedPagesAllocated));
		assert( ret == SMAR_DataAcquired );
		sharedHeapPushPageHandleLL( pHandle );

		uiNumLocalSharedPagesAllocated++;
	}
	sharedMemoryPopDefaultMemoryAccess();

}

U32 getSharedHeapMemoryManagerPageAllocs()
{
	return uiNumLocalSharedPagesAllocated;
}

void sharedHeapMemoryManagerUnlock()
{
	assert( sharedHeapMM->bMMLocked ); // should probably make sure it's OUR lock somehow
	assert( sharedHeapMM->uiNumPagesAllocated == uiNumLocalSharedPagesAllocated );

	// If we're in paranoid mode, set everything to readonly...
	// ... a good way to catch shared heap write bugs
#ifdef SHARED_HEAP_PARANOID_MODE
	sharedHeapSetPermissionsAllPages( PAGE_READONLY);
#endif

	assert( sharedHeapMMMutex );
	sharedHeapMM->bMMLocked = false;
	ReleaseMutex(sharedHeapMMMutex);
	bWeHaveSharedHeapMMMutex = false;
}

bool sharedHeapLazySyncHandlesOnly()
{
	DWORD dwResult;
	if ( shared_heap_status == SHARED_HEAP_DISABLED )
		return false;
		
	PERFINFO_AUTO_START_FUNC();
	
	assert( sharedHeapMMMutex );
	WaitForSingleObjectWithReturn(sharedHeapMMMutex, 0, dwResult);   
	bWeHaveSharedHeapMMMutex = true;


	if ( dwResult == WAIT_TIMEOUT || dwResult == WAIT_FAILED)
	{
		PERFINFO_AUTO_STOP();
		return false; // someone has it, or it's all broken
	}

	// Lock it
	sharedHeapMM->bMMLocked = true;

	// Get up to date with shared heap handles
	while ( uiNumLocalSharedPagesAllocated < sharedHeapMM->uiNumPagesAllocated )
	{
		char cMemMapName[256];
		STR_COMBINE_BEGIN(cMemMapName);
		STR_COMBINE_CAT("MemMapFile_");
		STR_COMBINE_CAT(makeSharedPageName(uiNumLocalSharedPagesAllocated++));
		STR_COMBINE_END(cMemMapName);

		OpenFileMappingSafe( FILE_MAP_READ, false, cMemMapName, 0);
	}

	// Unlock it
	sharedHeapMM->bMMLocked = false;
	ReleaseMutex(sharedHeapMMMutex);
	bWeHaveSharedHeapMMMutex = false;

	PERFINFO_AUTO_STOP();

	return true;
}

bool sharedHeapLazySync()
{
	DWORD dwResult;
	if ( shared_heap_status == SHARED_HEAP_DISABLED )
		return false;
	assert( sharedHeapMMMutex );
	WaitForSingleObjectWithReturn(sharedHeapMMMutex, 0, dwResult);   
	bWeHaveSharedHeapMMMutex = true;


	if ( dwResult == WAIT_TIMEOUT || dwResult == WAIT_FAILED)
		return false; // someone has it, or it's all broken

	// Lock it
	sharedHeapMM->bMMLocked = true;
	
	// Get up to date with shared heap pages
	sharedMemoryPushDefaultMemoryAccess(PAGE_READWRITE);
	while ( uiNumLocalSharedPagesAllocated < sharedHeapMM->uiNumPagesAllocated )
	{
		SharedMemoryHandle* pHandle = NULL;

		SM_AcquireResult ret = sharedMemoryAcquire( &pHandle, makeSharedPageName(uiNumLocalSharedPagesAllocated));
		if ( ret == SHAR_Error )
		{
			char *pBuf = NULL;
			FormatMessage_UTF8(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), 0, &pBuf, NULL);
			sharedHeapTurnOff(pBuf);
			sharedMemoryPopDefaultMemoryAccess();
			sharedHeapMM->bMMLocked = false;
			bWeHaveSharedHeapMMMutex = false;
			ReleaseMutex(sharedHeapMMMutex);
			return false;
		}
		assert( ret == SMAR_DataAcquired );
		sharedHeapPushPageHandleLL( pHandle );

		uiNumLocalSharedPagesAllocated++;
	}
	sharedMemoryPopDefaultMemoryAccess();


	// Unlock it
	sharedHeapMM->bMMLocked = false;
	bWeHaveSharedHeapMMMutex = false;
	ReleaseMutex(sharedHeapMMMutex);
	return true;
}

void incSharedHeapMemoryManagerPageAllocs( SharedMemoryHandle* pNewHandle)
{
	assert( sharedHeapMM->bMMLocked ); // should probably make sure it's OUR lock somehow
	sharedHeapMM->uiNumPagesAllocated++;
	uiNumLocalSharedPagesAllocated++;

	sharedHeapPushPageHandleLL( pNewHandle );
}

size_t getHandleAllocatedSize(SharedHeapHandle* pHandle)
{
	return pHandle->uiSize;
}



size_t reserveSharedHeapSpace( size_t uiReserveSize )
{
	SharedHeapAcquireResult ret;
	SharedHeapHandle* pHandle = NULL;
	const size_t uiMinReserveSize = 1024 * 1024 * 8;
	size_t uiAmountReserved = 0;

	if ( uiReserveSize < uiMinReserveSize )
		return 0;

	// Acquire the reserve handle from shared memory
	ret = sharedHeapAcquire(&pHandle, "reserveAlloc");

	// If we are the first caller
	if ( ret == SHAR_FirstCaller )
	{
		if ( !sharedHeapAlloc( pHandle, uiReserveSize) )
		{
			// failure
			// try again, with a smaller reserve size
			size_t uiNewReserveSize = uiReserveSize >> 1; // try half
			sharedHeapMemoryManagerUnlock();
			uiAmountReserved = reserveSharedHeapSpace( uiNewReserveSize );
		}
		else //success!
		{

			// A bit of a hack:
			// Manually free the data, so that sharedMalloc can work with that space
			sharedFree( pHandle->data );
			sharedHeapMM->uiTotalAllocated -= uiReserveSize;
			pHandle->data = NULL;

			uiAmountReserved = uiReserveSize;
			sharedHeapMemoryManagerUnlock();
		}
	}
	else if ( ret == SHAR_DataAcquired )
	{
		uiAmountReserved = getHandleAllocatedSize( pHandle );
	}
	else if ( ret == SHAR_Error )
	{
		// no shared heap
		uiAmountReserved = 0;
	}


	if ( !uiAmountReserved )
	{
		// Just disable shared heap and give up
		sharedHeapTurnOff( "Failed to reserve any shared heap space");
	}

	return uiAmountReserved; 
}


void printSharedHeapInfo()
{
	FILE* sharedHeapInfoFile;
	// For now, just print the mspace info
	if ( !sharedHeapMM || !sharedHeapMM->sharedMSpace )
	{
		printf("No shared heap MSPACE, means no info!\n");
		return;
	}

	sharedHeapInfoFile = NULL;

	while (!sharedHeapInfoFile)
	{
		sharedHeapInfoFile = fopen( "c:/sheepdump.csv", "wt" );
		if (!sharedHeapInfoFile)
		{
			printf("Let go of c:/sheepdump.csv, please!\n");
			Sleep(1000);
		}
	}	
	fprintf(sharedHeapInfoFile, "Size,Name\n");



	printf("Heap breakdown:\n");
	printf("Alloc size | Name\n");
	printf("---------------------------------------------------------\n");
	{
		StashTableIterator iter;
		StashElement elem;
		sharedHeapMemoryManagerLock();
		stashGetIterator( sharedHeapMM->mmHashTable, &iter );
		while(stashGetNextElement(&iter, &elem))
		{
			SharedHeapHandle *pImpHandle = stashElementGetPointer(elem);
			printf("%10lu - %s\n", pImpHandle->uiSize, pImpHandle->pcName);
			fprintf(sharedHeapInfoFile,"%lu,%s\n", pImpHandle->uiSize, pImpHandle->pcName);
		}
	}

	fclose(sharedHeapInfoFile);
	printf("Shared Heap Memory Manager Info:\n");
	printf("---------------------------------------------------------\n");
	printf("Heap allocator's view (on disk):\n");
	mspace_malloc_stats(sharedHeapMM->sharedMSpace);
	printf("---------------------------------------------------------\n");
	printf("Game heap manager's view (virtual):\n");
	printf("Allocated bytes  = %10lu\n", (unsigned long)(sharedHeapMM->uiTotalAllocated));
	printf("---------------------------------------------------------\n");

	sharedHeapMemoryManagerUnlock();

}

void testSharedHeap( )
{
	SharedHeapAcquireResult ret;
	SharedHeapHandle* pHandle = NULL;


	ret = sharedHeapAcquire(&pHandle, "testHeapAlloc");

	if ( ret == SHAR_FirstCaller )
	{
		char cString[32] = "Test String!";
		bool bSuccess;
		size_t size = 1024 * 1024 * 128;
		assert( pHandle );
		bSuccess = sharedHeapAlloc(pHandle, size );
		assert( bSuccess );
		assert( pHandle->data );
		strcpy_s(pHandle->data, size, cString);

		sharedHeapMemoryManagerUnlock();

		pHandle = NULL;

		ret = sharedHeapAcquire(&pHandle, "testHeapAlloc");
	}

	assert( ret == SHAR_DataAcquired );

	printf("Test String! = %s\n", (char*)pHandle->data);

	sharedHeapMemoryManagerLock();

	sharedHeapRelease(pHandle);
	sharedHeapRelease(pHandle);

	sharedHeapMemoryManagerUnlock();


}

#define DEFAULT_SHARED_HEAP_RESERVE_SIZE (384 * 1024 * 1024)

static size_t uiStartingReservationSize = DEFAULT_SHARED_HEAP_RESERVE_SIZE;
size_t getSharedHeapReservationSize( )
{
	return uiStartingReservationSize;
}
void setSharedHeapReservationSize( size_t uiSize )
{
	uiStartingReservationSize = uiSize;
}


void sharedHeapEmergencyMutexRelease()
{
	if ( bWeHaveSharedHeapMMMutex )
	{
		if ( sharedHeapMM )
			sharedHeapMM->bEmergencyMutexReleaseLeverPulled = true;
		ReleaseMutex(sharedHeapMMMutex);
	}
}

#pragma warning(pop)

#else

#include "SharedHeap.h"

bool sharedHeapAlloc(SharedHeapHandle* pHandle, size_t size)
{
	return 0;
}

SharedHeapAcquireResult sharedHeapAcquireEx(SharedHeapHandle** ppHandle, const char* pcName, const char* pcFile, U32 uiLineNum)
{
	return SHAR_Error;
}

void sharedHeapMemoryManagerLockEx( const char* pcHandleName, const char* pcFile, U32 uiLine)
{

}

void sharedHeapRelease(SharedHeapHandle* pHandle )
{

}

void sharedHeapTurnOff( const char* pcReason )
{

}

void sharedHeapMemoryManagerUnlock(void)
{

}

#endif
