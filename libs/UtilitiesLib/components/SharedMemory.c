/************************************************************************
* SharedMemory module
* 
* This is designed so that multiple processes that all need access to the
* same static data can share it.  The procedure is for one process to
* acquire a lock on the memory and then fill it out, and any subsequent
* requests from other processes for the shared memory block will return
* a pointer into the data filled in by the first process.  Writing to
* this data after it is unlocked and shared between processes should be
* possible, but has not been thoroughly tested.
*
* The sharedMemoryChunk() functions operate on a given chunk, while the
* sharedMemory() functions go through the management layer that keeps
* track of where things are between different processes.
*
************************************************************************/



#if !PLATFORM_CONSOLE

#include "SharedMemory.h"
#include "wininclude.h"

#include <stdio.h>
#include <conio.h>
#include "Alerts.h"
#include "cmdparse.h"
#include "MemoryMonitor.h"
#include "strings_opt.h"
#include "sysutil.h"
#include "osdependent.h"
#include "stringcache.h"
#include "GlobalTypes.h"
#include "earray.h"
#include "file.h"
#include "crypt.h"
#include "UtilitiesLib.h"
#include "winutil.h"
#include "utils.h"
#include "utf8.h"
#include "EString.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

void WriteVMapNow(void);

//NMM:Disable checked
#pragma warning(disable:6031)

#define SM_MANAGER_NAME_PREFIX	"SharedMemoryManager"

typedef enum SharedMemoryState {
	SMS_READY, // Has been filled in by another process or this process
	SMS_LOCKED, // Has been locked by this process and needs to be allocated
	SMS_ALLOCATED, // Has had it's size set and allocated, and the caller needs to fill it in
	SMS_ERROR, // General error flag
	SMS_MANUALLOCK, // Has been manually locked by a caller for read/write
} SharedMemoryState;

typedef struct SharedMemoryHandle {
	void *data;
	size_t size;
	size_t bytes_alloced;
	char name[SHARED_HANDLE_NAME_LEN];
	SharedMemoryState state;
	void* hMutex;
	void* hMapFile;
    bool bDataShouldBeFreed;
} SharedMemoryHandle;

#ifdef WIN32

#pragma warning(disable:4028) // parameter differs from declaration

static int bDefaultSharedMemoryOnInDevMode = true;

static int print_shared_memory_messages=0;

static void *shared_memory_base_address = ((void*)0x30000000);

// This starts out as READONLY, and is changed to WRITECOPY if writecopy mode is entered
static DWORD dwDefaultMemoryAccess = PAGE_READONLY;

static SharedMemoryMode shared_memory_mode = SMM_UNINITED;
static SYSTEM_INFO sysinfo; // After inited, it stores the allocation granularity
static bool write_copy_enabled = 0;
static bool gSemiSharedMode = 0;

// Used by "isSharedMemory()" macro to shortcut the need to call "isSharedMemory_dbg()"
bool g_bSharedMemoryPointerSet = false;

static DWORD dwOldDefaultMemoryAccess = 0;
void sharedMemoryPushDefaultMemoryAccess(unsigned long dwMemoryAccess)
{
	assert(dwOldDefaultMemoryAccess == 0);
	dwOldDefaultMemoryAccess = dwDefaultMemoryAccess;
	dwDefaultMemoryAccess = dwMemoryAccess;
}
void sharedMemoryPopDefaultMemoryAccess(void)
{
	assert(dwOldDefaultMemoryAccess!=0);
	dwDefaultMemoryAccess = dwOldDefaultMemoryAccess;
	dwOldDefaultMemoryAccess = 0;

}



void sharedMemorySetMode(SharedMemoryMode mode)
{
	shared_memory_mode = mode;
}

SharedMemoryMode sharedMemoryGetMode(void)
{
	return shared_memory_mode;
}

void sharedMemorySetBaseAddress(void *base_address)
{
	shared_memory_base_address = base_address;
	sharedMemorySetMode(SMM_UNINITED);
}


// Makes sure the system supports shared memory, if not, disables it
void sharedMemoryInit(void) {
	MEMORY_BASIC_INFORMATION mbi;

	if (shared_memory_mode != SMM_UNINITED)
		return;

	if (!IsUsingWin2kOrXp()) {
		printf("Disabling shard memory: It is not supported by the operating system.\n");
		sharedMemorySetMode(SMM_DISABLED);
		return;
	}
	
	GetSystemInfo(&sysinfo);
	if (!(sysinfo.lpMinimumApplicationAddress <= shared_memory_base_address &&
		sysinfo.lpMaximumApplicationAddress > shared_memory_base_address))
	{
		printf("Disabling shared memory: Program address space does not include our desired address.\n");
		sharedMemorySetMode(SMM_DISABLED);
		return;
	}

	VirtualQuery((void*)shared_memory_base_address, &mbi, sizeof(mbi));

	if (mbi.State != MEM_FREE) {
		printf("Disabling shared memory: Our desired address is not free in this process space.\n");
		sharedMemorySetMode(SMM_DISABLED);
		return;
	}

	sharedMemorySetMode(SMM_ENABLED);
}

SharedMemoryHandle **eaSharedHandles;

SharedMemoryHandle *sharedMemoryCreate(void)
{
	SharedMemoryHandle *pHandle = calloc(sizeof(SharedMemoryHandle), 1);
	eaPush(&eaSharedHandles, pHandle);
	return pHandle;
}

uintptr_t roundUpToMemoryBlock(uintptr_t origsize) {
	return (((origsize + sysinfo.dwAllocationGranularity - 1) / 
		sysinfo.dwAllocationGranularity) * sysinfo.dwAllocationGranularity);
}


// Frees all handles to the shared memory, but leaves it around for the OS to clean up
// or other processes to use
void sharedMemoryChunkDestroy(SharedMemoryHandle *handle)
{
#define SAFE_CLOSE(x)	\
	if (x) {			\
	CloseHandle(x);	\
	x = NULL;		\
	}
	SAFE_CLOSE(handle->hMapFile);
	SAFE_CLOSE(handle->hMutex);
	free(handle);
}

void sharedMemoryDestroy(SharedMemoryHandle *handle)
{
	eaFindAndRemove(&eaSharedHandles, handle);
	if (shared_memory_mode == SMM_DISABLED) {
		if (handle && handle->data && handle->bDataShouldBeFreed)
			free(handle->data);
		if (handle)
			free(handle);
		return;
	}
	if (handle->size) {
		// skip shared heap pages
		if (strncmp(handle->name, "sharedHeapAllocPage", 19)) {
			memMonitorUpdateStatsShared(handle->name, -(S32)roundUpToMemoryBlock(handle->size));
		}
	}

	// Just pass through
	sharedMemoryChunkDestroy(handle);
}

static char* sharedMemoryProcessNameSuffix;

static const char* sharedMemoryGetProcessNamePrefix(void)
{
	ATOMIC_INIT_BEGIN;
	{
		char	buffer[1000];
		U32		checksum = 0;

		cryptAdlerFileAndFileNameNoAlloc(getExecutableName(), &checksum);
		RegisterGenericGlobalTypes();
		sprintf(buffer,
				"%s_%s_%s_%d_%8.8X_",
				GlobalTypeToName(GetAppGlobalType()),
				GetProductName(),
				isDevelopmentMode() ? "DEV_" : "",
				UtilitiesLib_GetSharedMachineIndex(),
				checksum);
		sharedMemoryProcessNameSuffix = strdup(buffer);
	}
	ATOMIC_INIT_END;

	return sharedMemoryProcessNameSuffix;
}

static void sharedMemoryMakeOSMappingName(	const char* name,
											char* bufferOut,
											size_t bufferOutLen)
{
	snprintf_s(	bufferOut,
				bufferOutLen,
				"MemMapFile_%s%s",
				sharedMemoryGetProcessNamePrefix(),
				name);
}

// General flow:
// First, lock the Mutex.
//  then, determine if mmapped file exists
//   If so, it's already been created, and we're good to go, release the Mutex
//   If not, we need to create it!  Create the mmapped file, return control to the caller
//      so they can fill it in, and then release the mutex when we're done
SM_AcquireResult sharedMemoryChunkAcquire(SharedMemoryHandle **phandle, const char *name, void *desired_address)
{
	char osName[MAX_PATH];
	void *lpMapAddress;
	SharedMemoryHandle *handle;

	sharedMemoryInit();

	assert(shared_memory_mode!=SMM_DISABLED);

	assert(phandle);
	if (*phandle == NULL) {
		*phandle = (SharedMemoryHandle*)sharedMemoryCreate();
	}

	handle = *phandle;

	assert(strlen(name) < SHARED_HANDLE_NAME_LEN);

	strcpy(handle->name, name);
	sprintf(osName,
			"Mutex_%s%s",
			sharedMemoryGetProcessNamePrefix(),
			handle->name);
	
	handle->hMutex = CreateMutex_UTF8(	NULL,  // no security attributes
									FALSE, // initially not owned
									osName);  // name of mutex

	if (handle->hMutex == NULL) {
		handle->hMutex=INVALID_HANDLE_VALUE;
		MessageBox(NULL, L"Error creating mutex", L"Error", MB_ICONINFORMATION);
		handle->state = SMS_ERROR;
		return SMAR_Error;
	}
	// Grab the mutex (release it when we want the app to end)
	WaitForSingleObject(handle->hMutex, INFINITE);
	// We now have the mutex because either
	//  a) it was abandoned by another thread, or
	//  b) the other thread finished creating the shared memory, or
	//  c) we initially created the mutex

	sharedMemoryMakeOSMappingName(handle->name, SAFESTR(osName));

	// Now, let's check to see if the memory mapped file exists
	handle->hMapFile = OpenFileMapping_UTF8(	FILE_MAP_ALL_ACCESS, // read/write permission 
										FALSE,		// Do not inherit the name
										osName);		// of the mapping object. 

	if (handle->hMapFile == NULL) 
	{
		// The file doesn't exist, we need to create it (but only after we know how much data there is)!
		handle->state = SMS_LOCKED;
		handle->data = NULL; // Compute the desired address later

		// Tell the caller to fill this in
		return SMAR_FirstCaller;
	} 

	// We acquired the mutex lock, and the file is already there, we can just unlock it and use the data!
	handle->size = 0; // This doesn't work: GetFileSize(handle->hMapFile, NULL);
	handle->bytes_alloced = 0; 

	lpMapAddress = MapViewOfFileExSafe(handle->hMapFile, osName, desired_address, 0);

	if (lpMapAddress == NULL) { 
		goto fail;
	}
	if (desired_address != NULL && lpMapAddress != desired_address) {
		goto fail;
	}

	handle->data = lpMapAddress;
	handle->state = SMS_READY;
	ReleaseMutex(handle->hMutex);

	return SMAR_DataAcquired;

fail:
	//MessageBox(NULL, "Error mapping view of file!", "Error", MB_ICONINFORMATION);
	ReleaseMutex(handle->hMutex);
	handle->state = SMS_ERROR;
	return SMAR_Error;

}

// Allocates the required amount of shared memory and returns a pointer to it.
//   This can only be called after sharedMemoryChunkAcquire and only if
//   sharedMemoryChunkAquire returned 1
void *sharedMemoryChunkSetSize(SharedMemoryHandle *handle, uintptr_t size)
{
	char osName[MAX_PATH];
	void *lpMapAddress;
	assert(handle && handle->state == SMS_LOCKED && handle->hMapFile == NULL);

	sharedMemoryMakeOSMappingName(handle->name, SAFESTR(osName));

	if (!handle->data) {
		goto fail;
	}

	// Create the Mem mapped file
	handle->hMapFile = CreateFileMappingSafe(PAGE_READWRITE, size, osName, 0);

	if (handle->hMapFile == NULL) {
		goto fail;
	}

	lpMapAddress = MapViewOfFileExSafe(handle->hMapFile, osName, handle->data, 0);

	if (lpMapAddress == NULL) { 
		goto fail;
	}

	if (handle->data != NULL && lpMapAddress != handle->data) {
		goto fail;
	}

	handle->data = lpMapAddress; // noop
	handle->size = size;
	handle->state = SMS_ALLOCATED;
	handle->bytes_alloced = 0;

	return handle->data;

fail:
	//MessageBox(NULL, "Error creating/mapping view of file!", "Error", MB_ICONINFORMATION);
	handle->state = SMS_ERROR;
	ReleaseMutex(handle->hMutex);
	return NULL;

}

// Unlocks a block of shared memory, so that other processes can now access it
//   This can only be called after sharedMemoryChunkAcquire and only if
//   sharedMemoryChunkAquire returned 1, and after sharedMemoryChunkSetSize has
//   been called, or after sharedMemoryChunkLock has been called
void sharedMemoryChunkUnlock(SharedMemoryHandle *handle)
{
	assert(handle->state == SMS_ALLOCATED || handle->state == SMS_MANUALLOCK || handle->state == SMS_LOCKED);

	ReleaseMutex(handle->hMutex);
	handle->state = SMS_READY;
}

void sharedMemoryChunkLock(SharedMemoryHandle *handle)
{
	assert(handle->state == SMS_READY);

	WaitForSingleObject(handle->hMutex, INFINITE);

	handle->state = SMS_MANUALLOCK;

}




//////////////////////////////////////////////////////////////////////////
// Shared Memory Management (wrapper)

typedef struct SharedMemoryAllocList {
	struct SharedMemoryAllocList *next;
	uintptr_t size;
	void *base_address;
	int reference_count;
	char name[CRYPTIC_MAX_PATH]; // In memory, this is actually a variable length, null terminated string
} SharedMemoryAllocList;

typedef struct SharedMemoryAllocHeader {
	SharedMemoryAllocList *head;
	void *next_free_node; // The next place we can safely cast to an AllocList node
	void *next_free_block; // The next address in process space we can use to allocated shared memory
} SharedMemoryAllocHeader;

static char manager_name[CRYPTIC_MAX_PATH] = SM_MANAGER_NAME_PREFIX; // Set dynamically below based on DataDir
static size_t manager_size = 128*4*1024;
static SharedMemoryHandle *pManager = NULL;
static SharedMemoryAllocHeader *pHeader = NULL; // Should always be equal to pManager->data;
static SharedMemoryAllocList fake_shared_memory; // Up to one element might be in "fake" shared memory mode
static int manager_lock; // Number of locks of manager, only do actual lock/unlock once, so we can hold multiple chunk locks at once
static bool s_deadBefore = false;  // Set to true if we detect that the shared memory on this machine has been marked as dead by another process

static void managerLock(void)
{
	if (manager_lock == 0)
		sharedMemoryChunkLock(pManager);
	manager_lock++;
}


static void managerUnlock(void)
{
	manager_lock--;
	if (manager_lock == 0)
		sharedMemoryChunkUnlock(pManager);
}

//
// This function is called to remove any reference to the manager shared memory segment by this process.  If this is the
//  last process holding a handle to the segment, the operating system will destroy it.
//
void sharedMemoryDestroyManager(void)
{
	if ( pManager )
	{
		sharedMemoryDestroy(pManager);
	}
}

// Adds a node to the alloc list (only call this *after* the node has been filled in, otherwise
// we can get wacky issues between processes thinking it's filled in when it's not)
static void addNodeToAllocList(void *base_address, size_t size, const char *name)
{
	size_t len;
	SharedMemoryAllocList *newnode;
	assert(pHeader);

	// Find the new node
	newnode = (SharedMemoryAllocList*)pHeader->next_free_node;
	
	// Update next_free_node
	len = (int)strlen(name) + 1;
	pHeader->next_free_node = &newnode->name[0] + len;

	if ((char*)pHeader->next_free_node < (char*)pHeader + manager_size) {

		// Fill in the node
		newnode->base_address = base_address;
		newnode->size = size;
		newnode->next = pHeader->head;
		newnode->reference_count = 1;
		strcpy_s(newnode->name, len, name);

		pHeader->head = newnode;

	} else {
		assert(!"Ran out of memory in the shared memory manager!");
		sharedMemorySetMode(SMM_DISABLED);
	}

	if (print_shared_memory_messages)
		printf("Added new node at 0x%p, named '%s', used %Id/%Id bytes in smm\n", base_address, name, (char*)pHeader->next_free_node - (char*)pHeader, manager_size);
	// Update next_free_block
	pHeader->next_free_block = (char*)base_address + size;

	// Report to MemMonitor
	// skip shared heap pages
	if (strncmp(name, "sharedHeapAllocPage", 19)!=0 && strcmp(name, manager_name)!=0) {
		memMonitorUpdateStatsShared(name, roundUpToMemoryBlock(size));
	}
}

static SharedMemoryAllocList *findNodeInAllocList(const char *name)
{
	SharedMemoryAllocList *walk;
	assert(pHeader);

	for (walk = pHeader->head; walk; walk = walk->next) {
		if (strcmp(walk->name, name)==0) {
			return walk;
		}
	}
	return NULL;	
}

static void removeNodeFromAllocList(SharedMemoryAllocList *node)
{
	assert(pHeader);
	if (node == pHeader->head) {
		pHeader->head = node->next;
	} else {
		SharedMemoryAllocList *walk;

		for (walk = pHeader->head; walk; walk = walk->next) {
			if (walk->next == node) {
				walk->next = node->next;
				break;
			}
		}
	}
	node->next = NULL;
	strcpy_s(node->name, 6, "freed");
}

static void *findNextFreeBlock(uintptr_t size)
{
	MEMORY_BASIC_INFORMATION mbi;
	assert(pHeader);
	
	while (true)
	{	
		pHeader->next_free_block = (void*)roundUpToMemoryBlock((uintptr_t)pHeader->next_free_block);
	
		if (!VirtualQuery(pHeader->next_free_block, &mbi, sizeof(mbi)))
		{
			// We ran out of possible pages
			return NULL;
		}

		if (mbi.State == MEM_FREE && mbi.RegionSize >= size) 
		{
			return pHeader->next_free_block;
		}

		pHeader->next_free_block = (char *)pHeader->next_free_block + mbi.RegionSize; // try next block
	}
}

// Set up a different ShardMemory name space for each project based on data directory
static void sharedMemoryAutoSetManagerName(void)
{
	char *s;

	ATOMIC_INIT_BEGIN;
	{
		strcpy(manager_name, SM_MANAGER_NAME_PREFIX);

		if (IsClient())
			strcat(manager_name, "_Client");
		if (strstri(GetCommandLine(), "beaconclient"))
			strcat(manager_name, "_BeaconClient");

		for (s=manager_name + strlen(SM_MANAGER_NAME_PREFIX); *s; s++) {
			if (*s==':' || *s=='\\' || *s=='/' || *s=='.') {
				*s = '_';
			} else {
				*s = toupper((unsigned char)*s);
			}
		}
	}
	ATOMIC_INIT_END;
}

// Return true if any shared memory segments have been marked as DEAD
static bool sharedMemoryMarkedDead(void)
{
    SharedMemoryAllocList *walk;

    if (!pHeader) {
        // No shared memory!
        return false;
    }
    if (gSemiSharedMode && isProductionMode()) {
        // Don't want to mark shared memory as bad for other servers, just disconnect this one
        return false;
    }
    walk = pHeader->head;
    while (walk) {
        if (strStartsWith(walk->name, "DEAD")) {
            return true;
        }
        walk = walk->next;
    }

    return false;
}

void printSharedMemoryUsageToString(char **estrOut)
{
    SharedMemoryAllocList *walk;
    long long total_size=0;
    long long actual_size=0;
    SharedMemoryHandle *handle=NULL;

    if (!pHeader) {
        estrAppend2(estrOut, "Shared memory not accessible by this process or an error has occured\n");
        return;
    }
    walk = pHeader->head;
    estrAppend2(estrOut, "Shared memory usage:\n");
    while (walk) {
        estrConcatf(estrOut, "  0x%p:%8Id bytes, %2d refs, \"%s\"\n", walk->base_address, walk->size, walk->reference_count, walk->name);
        total_size += walk->size;
        actual_size += roundUpToMemoryBlock(walk->size);
        walk = walk->next;
    }
    estrConcatf(estrOut, "Total: %"FORM_LL"d bytes, %"FORM_LL"d actual\n", total_size, actual_size);
}

SM_AcquireResult sharedMemoryAcquire(SharedMemoryHandle **phandle, const char *name)
{
	SharedMemoryHandle *handle;
	SharedMemoryAllocList *plist;
	SM_AcquireResult ret;
	DWORD dwDummy;
    bool acquiredManager = false;

	sharedMemoryInit();

	if (shared_memory_mode == SMM_DISABLED) {
		return SMAR_Error;
	}

	assert(phandle);
	if (*phandle == NULL) {
		*phandle = (SharedMemoryHandle*)sharedMemoryCreate();
	}

	handle = *phandle;

	// First acquire (and init) the memory manager
	if (pManager==NULL) {
		sharedMemoryAutoSetManagerName();
		ret = sharedMemoryChunkAcquire(&pManager, manager_name, shared_memory_base_address);
		if (ret == SMAR_Error) {
			printf("Disabling shared memory: Error acquiring shared memory for the memory manager!\n");
			if (isProductionMode()) {
				sharedMemoryQueueAlertString("Disabling shared memory: Error acquiring shared memory for the memory manager!");
			}
			sharedMemorySetMode(SMM_DISABLED);
			return SMAR_Error;
		}
        acquiredManager = true;
	} else {
		ret = SMAR_DataAcquired; // Pretend we just got a view of it (since we already have one!)
	}
	if (ret == SMAR_FirstCaller) {
		// Needs to be filled in!
		char *data;
		pManager->data = shared_memory_base_address;
		data = sharedMemoryChunkSetSize(pManager, manager_size);
		manager_lock = 1;

		if (data == NULL) {
			printf("Disabling shared memory: Error acquiring base address!\n");
			if (isProductionMode()) {
				sharedMemoryQueueAlertString("Disabling shared memory: Error acquiring base address!");
			}
			sharedMemorySetMode(SMM_DISABLED);
			return SMAR_Error;
		}

		// Fill it in (with 1 structure describing this entry)
		pHeader = (SharedMemoryAllocHeader*)data;
		pHeader->head = NULL;
		pHeader->next_free_node = data + sizeof(SharedMemoryAllocHeader);
		pHeader->next_free_block = data + manager_size; // Needs to be rounded up to granularity

		addNodeToAllocList(data, manager_size, manager_name);

		// We have now created the list, but we want to leave it locked until
		//  the caller of this function fills in his data and we can save it

	} else {
		// The memory is already there, just lock it!
		managerLock();
		pHeader = (SharedMemoryAllocHeader*)pManager->data;
        if ( acquiredManager )
        {
            // This is the first time through here in this process, we have just acquired the manager chunk,
            //  and it has already been initialized.  We check here to see if it has any dead chunks marked in it.
            s_deadBefore = sharedMemoryMarkedDead();
        }
    }

	g_bSharedMemoryPointerSet = true; // Set this for "isSharedMemory()" macro to know to call in

	// We now have the memory manager locked, let's get some shared memory at the appropriate
	//   address and return it to the caller, or create it if it doesn't exist
	if (plist = findNodeInAllocList(name)) {
		// Already in the list, great!
		// Acquire a view of this shared memory
		ret = sharedMemoryChunkAcquire(&handle, name, plist->base_address);
		if (ret==SMAR_Error) {
			char *estrError = NULL;

            estrPrintf(&estrError, "Disabling shared memory: Failed to acquire chunk %s, so disabling all chunks!\n", name);
            printSharedMemoryUsageToString(&estrError);

			// could not acquire it
			//assert(!"Could not acquire view of memory, perhaps it's being mapped twice?");
			sharedMemoryCancelAllLocks();
			managerUnlock();
			// Disable shared memory from here on out, because we don't want to grab a chunk of shared memory
			// that has pointers into this chunk that was never acquired.			
			printf("%s", estrError);
            if ( !s_deadBefore ) {
                WriteVMapNow();
                if (isProductionMode()) {
                // Only alert before shared memory is marked dead the first time.
				    sharedMemoryQueueAlertString(estrError);
                }
			}
			sharedMemorySetMode(SMM_DISABLED);
			sharedMemoryEnableEditorMode();
			estrDestroy(&estrError);
			return ret; // SMAR_Error
		} else if (ret==SMAR_DataAcquired) {
			plist->reference_count++;
			// Acquired it, and it's already filled in, excellent!
			// Verify size value if its there, and set it if it's not
			assert(handle->size==0);
			handle->size = plist->size;
			// Make the memory read-only or copy on write
			VirtualProtect(handle->data, handle->size, dwDefaultMemoryAccess, &dwDummy);

			// Just unlock things and send it back to the client
			managerUnlock();
			// Report to MemMonitor
			// skip shared heap pages
			if (strncmp(handle->name, "sharedHeapAllocPage", 19)) {
				memMonitorUpdateStatsShared(handle->name, roundUpToMemoryBlock(handle->size));
			}
			return ret; // SMAR_DataAcquired
		} else {
			assert(ret==SMAR_FirstCaller);
			// The entry is in the allocation list, but it's not in memory anymore, all
			// processes referencing it must have exited... this new process can put it
			// wherever it wants

			// VAS 20121023 -- IMPORTANT! Since sharedMemorySetSize no longer allows
			// resizing of a chunk that's already in the alloc list, we have to remove
			// this one here. This is expected to be okay, because even if it were
			// still in the list, we'd just replace the entry with a new one at unlock
			removeNodeFromAllocList(plist);

			// The client needs to fill stuff in, let's leave it all locked and return!
			return ret; // SMAR_FirstCaller
		}
	} else {
		// The memory is not currently in the list, that means we will need to 
		//  add it to the list after it's filled in!
		void *base_address = NULL; // Fill in the base address when setting size

		// Acquire a view of this shared memory
		ret = sharedMemoryChunkAcquire(&handle, name, base_address);
		if (ret==SMAR_Error) {
			char *estrError = NULL;

            estrPrintf(&estrError, "Disabling shared memory: Could not acquire chunk '%s', perhaps it's being mapped twice!\n", name);
            printSharedMemoryUsageToString(&estrError);

			// could not acquire it
			// This sometimes happens with "Attempt to access an invalid address." as the return
			//   perhaps some DLL grabs the memory from under us...
			//assert(!"Could not acquire view of memory, perhaps it's being mapped twice?");
			sharedMemoryCancelAllLocks();
			managerUnlock();

			printf("%s", estrError);
            if ( !s_deadBefore ) {
                WriteVMapNow();
                if (isProductionMode()) {
                // Only alert before shared memory is marked dead the first time.
				    sharedMemoryQueueAlertString(estrError);
                }
			}
			sharedMemorySetMode(SMM_DISABLED);
			sharedMemoryEnableEditorMode();
			estrDestroy(&estrError);
			return ret;
		} else if (ret==SMAR_DataAcquired) {
			char *estrError = NULL;

            estrPrintf(&estrError, "Disabling shared memory: Acquired chunk '%s', but manager doesn't know about it!\n", name);
            printSharedMemoryUsageToString(&estrError);

			// Acquired it, and it's already filled in, but the
			//  manager doesn't know about it, so it's been reloaded somewhere.
			//  Return error and disable shared memory
			sharedMemoryCancelAllLocks();
			managerUnlock();

            printf("%s", estrError);
            if ( !s_deadBefore ) {
                WriteVMapNow();
                if (isProductionMode()) {
                    // Only alert before shared memory is marked dead the first time.
                    sharedMemoryQueueAlertString(estrError);
                }
            }

			sharedMemorySetMode(SMM_DISABLED);
			sharedMemoryEnableEditorMode();
			estrDestroy(&estrError);
			return SMAR_Error;
		} else {
			assert(ret==SMAR_FirstCaller);
			// The entry is not in memory and not in the list (expected)
			// The client needs to fill stuff in, let's leave it all locked and return!
			return ret;
		}
	}
	assert(0);
}

void *sharedMemorySetSize(SharedMemoryHandle *handle, uintptr_t size)
{
	void *ret;
	SharedMemoryAllocList *plist;

	assert(shared_memory_mode == SMM_ENABLED);
	// Resizing shared memory chunks is no longer supported
	if (plist = findNodeInAllocList(handle->name)) 
	{
		assertmsg(0,"Cannot resize shared memory chunks!");
	}

	handle->data = findNextFreeBlock(size);

	// Just pass through
	ret = sharedMemoryChunkSetSize(handle, size);
	if (ret == NULL) { // unlock on error
		char *estrError = NULL;

        estrPrintf(&estrError, "Disabling shared memory: Unabled to resize chunk '%s'\n", handle->name);
        printSharedMemoryUsageToString(&estrError);

		sharedMemoryCancelAllLocks();
		managerUnlock();
		// Just allocate memory for it, pretend it's shared
		handle->data = malloc(size);
        handle->bDataShouldBeFreed = true;
		handle->size = size;

        printf("%s", estrError);
        if ( !s_deadBefore ) {
            WriteVMapNow();
            if (isProductionMode()) {
                // Only alert before shared memory is marked dead the first time.
                sharedMemoryQueueAlertString(estrError);
            }
        }

		sharedMemorySetMode(SMM_DISABLED);
		sharedMemoryEnableEditorMode();
		estrDestroy(&estrError);
		//assert(!fake_shared_memory.base_address);
		fake_shared_memory.base_address = handle->data;
		fake_shared_memory.size = size;
		fake_shared_memory.reference_count = 1;
		strcpy(fake_shared_memory.name, "FakeSharedMemory");
	} else {
		// Succeeded, but...
		// Do this so isSharedMemory tests while loading behave right
		fake_shared_memory.base_address = handle->data;
		fake_shared_memory.size = handle->size;
	}

	g_bSharedMemoryPointerSet = true; // Set this for "isSharedMemory()" macro to know to call in

	return ret;
}


size_t sharedMemoryGetSize(SharedMemoryHandle *handle)
{
	if ( handle )
		return handle->size;
	return 0;
}

size_t sharedMemoryBytesAlloced(SharedMemoryHandle *handle)
{
	if ( handle )
		return handle->bytes_alloced;
	return 0;
}
void sharedMemorySetBytesAlloced(SharedMemoryHandle *handle, size_t bytes_alloced)
{
	if ( handle )
		handle->bytes_alloced = bytes_alloced;
}

bool sharedMemoryIsLocked(SharedMemoryHandle *handle)
{
	if (!handle)
	{
		return false;
	}
	if (handle->state == SMS_MANUALLOCK || handle->state == SMS_LOCKED || handle->state == SMS_ALLOCATED)
	{
		return true;
	}
	return false;
}

void sharedMemoryUnlock(SharedMemoryHandle *handle)
{
	DWORD dwDummy;

	if (handle->state == SMS_MANUALLOCK || handle->state == SMS_LOCKED) {
		// Just a normal unlock, pass it through and let it go
		sharedMemoryChunkUnlock(handle);
		// Make the memory writeable
		VirtualProtect(handle->data, handle->size, dwDefaultMemoryAccess, &dwDummy);
		managerUnlock();
		return;
	} else if (handle->state == SMS_ERROR) {
		// If the handle is in an erroneous state, just ignore the request
		return;		
	} else {
		SharedMemoryAllocList *plist;
		assert(handle->state == SMS_ALLOCATED);
		if (shared_memory_mode == SMM_DISABLED) {
			// If memory is disabled, just unlock it if you can
			// Just a normal unlock, pass it through and let it go
			sharedMemoryChunkUnlock(handle);
			// Make the memory writeable
			VirtualProtect(handle->data, handle->size, dwDefaultMemoryAccess, &dwDummy);
			managerUnlock();
			return;
		}
		// The client just finished filling in the data, it is now safe to add it to the
		// allocation list!
		if (plist = findNodeInAllocList(handle->name)) {		
			// Remove old entry from the list, and create a new one!
			removeNodeFromAllocList(plist);
			addNodeToAllocList(handle->data, handle->size, handle->name);

		} else {
			// It's a new one, add it to the list!
			addNodeToAllocList(handle->data, handle->size, handle->name);
			// Then unlock everything
		}
		sharedMemoryChunkUnlock(handle);
		// Make the memory read-only or copy on write
		VirtualProtect(handle->data, handle->size, dwDefaultMemoryAccess, &dwDummy);

		managerUnlock();
	}
}

void sharedMemoryCommitButKeepLocked(SharedMemoryHandle *handle)
{
	if (shared_memory_mode == SMM_DISABLED) {
		return;
	} else if (handle->state == SMS_MANUALLOCK) {
		// Do nothing
		return;
	} else if (handle->state == SMS_ERROR) {
		// If the handle is in an erroneous state, just ignore the request
		return;		
	} else {
		SharedMemoryAllocList *plist;
		assert(handle->state == SMS_ALLOCATED);		
		// The client just finished filling in the data, it is now safe to add it to the
		// allocation list!
		if (plist = findNodeInAllocList(handle->name)) {
			
			removeNodeFromAllocList(plist);
			addNodeToAllocList(handle->data, handle->size, handle->name);

		} else {
			// It's a new one, add it to the list!
			addNodeToAllocList(handle->data, handle->size, handle->name);
			// Then unlock everything
		}
		handle->state = SMS_MANUALLOCK;
	}
}

void sharedMemoryLock(SharedMemoryHandle *handle)
{
	DWORD dwDummy;
	assert(shared_memory_mode == SMM_ENABLED);
	if (shared_memory_mode == SMM_DISABLED)
		return;
	// Just pass through
	managerLock();
	sharedMemoryChunkLock(handle);
	// Make the memory writeable
	VirtualProtect(handle->data, handle->size, PAGE_READWRITE, &dwDummy);
}


void *sharedMemoryAlloc(SharedMemoryHandle *handle, size_t size)
{
	void *ret;
	//assert(shared_memory_mode == SMM_ENABLED);
	//if (shared_memory_mode == SMM_DISABLED) {
	//	return malloc(size);
	//}
	if (handle->bytes_alloced + size > handle->size) {
		assert(!"Trying to alloc more memory from a shared memory block than was originally inited!");
		return NULL;
	}
	ret = (char*)handle->data + handle->bytes_alloced;
	handle->bytes_alloced += size;

	if (size)
	{
		memset(ret, 0, size);
	}
	return ret;
}

void printSharedMemoryUsage(void)
{
    static char *s_outputString = NULL;

    estrClear(&s_outputString);
    printSharedMemoryUsageToString(&s_outputString);
    printf("%s", s_outputString);
}

// Tests to see if a pointer is in a range that is from shared memory
int isSharedMemory_dbg(const void *ptr)
{
	SharedMemoryAllocList *walk;
	static SharedMemoryAllocList *last_walk_saved;
	SharedMemoryAllocList *last_walk = last_walk_saved;
 
	if (fake_shared_memory.base_address) {
		if (ptr >= fake_shared_memory.base_address && ((char*)ptr <= (char*)fake_shared_memory.base_address + fake_shared_memory.size))
			return true;
	}
	if (!pHeader) {
		// No shared memory!
		return false;
	}
	if (ptr < (void*)pHeader) {
		// It's earlier than the base address (this is dependent on the current implementation)
		return false;
	}
	if (last_walk && ptr >= last_walk->base_address && ((char*)ptr <= (char*)last_walk->base_address + last_walk->size))
		return true;
	walk = pHeader->head;
	while (walk)
	{
		if (ptr >= walk->base_address && ((char*)ptr <= (char*)walk->base_address + walk->size))
		{
			last_walk_saved = walk;
			return true;
		}
		walk = walk->next;
	}
	return false;
}

// Takes a pointer, and it it's part of shared memory removes the shared memory block from
//  the shared list so other instances will not be able to access it
void sharedMemoryUnshare(void *ptr)
{
	SharedMemoryAllocList *walk;
	if (!pHeader) {
		// No shared memory!
		return;
	}
	if (ptr < (void*)pHeader) {
		// It's earlier than the base address (this is dependent on the current implementation)
		return;
	}
	walk = pHeader->head;
	while (walk) {
		if (ptr >= walk->base_address && ((char*)ptr <= (char*)walk->base_address + walk->size)) {
			// found it!
			strncpy_s(walk->name, strlen(walk->name)+1, "DEAD", MIN(4, strlen(walk->name)));
			return;
		}
		walk = walk->next;
	}
}
	
int isSharedMemoryEditorMode(void)
{
	return write_copy_enabled;
}

void sharedMemoryCancelAllLocks(void)
{	
	if (!pHeader) {
		// No shared memory!
		return;
	}
	if (write_copy_enabled) {
		return;
	}

	if (manager_lock != 0)
	{
		int i;
		for (i = 0; i < eaSize(&eaSharedHandles); i++)
		{
			SharedMemoryHandle *pSharedHandle = eaSharedHandles[i];
			if (pSharedHandle != pManager)
			{
				if (pSharedHandle->state == SMS_MANUALLOCK || pSharedHandle->state == SMS_ALLOCATED)
					sharedMemoryUnlock(pSharedHandle);
				sharedMemoryUnshare(pSharedHandle->data);
				pSharedHandle->state = SMS_ERROR;
			}
		}
	}

}

void sharedMemoryEnableEditorMode(void)
{
	DWORD oldValue;
	SharedMemoryAllocList *walk;
	if (write_copy_enabled) {
		return;
	}
	write_copy_enabled = 1;
	assertmsg(manager_lock == 0, "You CANNOT enter editor mode while shared memory is still locked");
	dwDefaultMemoryAccess = PAGE_WRITECOPY;
	if (!pHeader) {
		// No shared memory!
		return;
	}
	if (gSemiSharedMode && isProductionMode()) {
		// Don't want to mark shared memory as bad for other servers, just disconnect this one
		return;
	}

    // Only alert before the first time shared memory is marked DEAD on the machine.
	if (isProductionMode() && !s_deadBefore) {
		sharedMemoryQueueAlertString("Entering Shared Memory Editor Mode (which marks all shared memory on the machine as no longer usable for future servers on this machine)");
	}
	printf("Entering Shared Memory Editor Mode (which marks all shared memory on the machine as no longer usable for future servers on this machine)\n");
	walk = pHeader->head;
	while (walk) {
		if (!strStartsWith(walk->name, SM_MANAGER_NAME_PREFIX)) { // Can't set the manager to WriteCopy, otherwise we can't unshare things!
			strncpy_s(walk->name, strlen(walk->name)+1, "DEAD", MIN(4, strlen(walk->name))); // Disable it for future servers as well
			VirtualProtect(walk->base_address,walk->size,PAGE_WRITECOPY,&oldValue);
		}
		walk = walk->next;
	}
}

void sharedMemoryProcessCommandLine(void)
{
	bool enabled = false;
	bool byCommandLine = false;
	char buf[100];

	if (bDefaultSharedMemoryOnInDevMode) 
	{
		// If default on in dev mode, then it should always be on when possible
		enabled = true;
	}
	else
	{
		// productionmode defaults to on, devmode to off
		if(!fileIsUsingDevData())
			enabled = true;
		if(ParseCommandOutOfCommandLine("productionmode", buf))
			enabled = atoi(buf);
	}

	// enablesharedmemory 0 or 1 both override the default
	if(ParseCommandOutOfCommandLine("EnableSharedMemory", buf))
	{
		enabled = atoi(buf);
		byCommandLine = true;
	}
	// nosharedmemory 0 or 1 both override the default
	if(ParseCommandOutOfCommandLine("NoSharedMemory", buf))
	{
		enabled = !atoi(buf);
		byCommandLine = true;
	}

	if(!enabled)
	{
		if (byCommandLine)
			printf("Shared memory disabled by command line\n");
		sharedMemorySetMode(SMM_DISABLED);
	}
}




//////////////////////////////////////////////////////////////////////////
// Test function


void testSharedMemory(void)
{
	SharedMemoryHandle *handle=NULL;
	int ret;
	char *data=NULL;
	char *c;
	char c2;
#define TEST_SIZE 1024*1024*50+1 // 50MB and a byte
#define TEST_STRING "This is our test string!"

	printf("Acquiring memory...\n");
	ret = sharedMemoryAcquire(&handle, "SMTEST");
	assert(ret!=-1);
	if (ret == 1) { // We need to fill in the data!
		printf("We got it first, fill it in...");
		data = sharedMemorySetSize(handle, TEST_SIZE);
		assert(data);
		strcpy_s(data, TEST_SIZE, TEST_STRING);
		for (c=data + strlen(TEST_STRING) + 1; c < data +TEST_SIZE; c++) {
			*c = (char)c;
		}
		printf("press any key to unlock.\n");
		_getch();
		sharedMemoryUnlock(handle);
	} else {
		printf("Somone else created it, let's just use it.\n");
		_getch();
		data = handle->data;
		for (c=data; c < data +TEST_SIZE; c++) {
			c2 = *c;
		}
	}
	printf("Shared memory is at 0x%p and contains \"%s\"\n", data, data);
	assert(strcmp(data, TEST_STRING)==0);
	printf("Press any key to destroy.\n");
	_getch();
	sharedMemoryDestroy(handle);

}

void *sharedMemoryGetDataPtr(SharedMemoryHandle *handle)
{
	return handle->data;
}

#endif // WIN32

#else

// If we're XBOX, Just ignore any calls, as if it was disabled

#include "SharedMemory.h"

void sharedMemorySetMode(SharedMemoryMode mode)
{
}

SharedMemoryMode sharedMemoryGetMode(void)
{
	return SMM_DISABLED;
}

void sharedMemorySetBaseAddress(void *base_address)
{
}

SharedMemoryHandle *sharedMemoryCreate(void)
{
	return NULL;
}

void sharedMemoryPushDefaultMemoryAccess(unsigned long dwMemoryAccess)
{
}
void sharedMemoryPopDefaultMemoryAccess(void)
{
}

SM_AcquireResult sharedMemoryAcquire(SharedMemoryHandle **phandle, const char *name)
{
	return SMAR_Error;
}

void *sharedMemorySetSize(SharedMemoryHandle *handle, uintptr_t size)
{
	return 0;
}

uintptr_t sharedMemoryGetSize(SharedMemoryHandle *handle)
{
	return 0;
}

void sharedMemoryUnlock(SharedMemoryHandle *handle)
{
}

void sharedMemoryLock(SharedMemoryHandle *handle)
{
}

bool sharedMemoryIsLocked(SharedMemoryHandle *handle)
{
	return false;
}

void sharedMemoryCommitButKeepLocked(SharedMemoryHandle *handle)
{
}

void *sharedMemoryAlloc(SharedMemoryHandle *handle, uintptr_t size){
	return NULL;
}

void sharedMemoryDestroy(SharedMemoryHandle *handle)
{
}

int isSharedMemory(const void *ptr)
{
	return false;
}

void sharedMemoryUnshare(void *ptr)
{

}

void printSharedMemoryUsage(void)
{
}
void testSharedMemory(void)
{
}

void *sharedMemoryGetDataPtr(SharedMemoryHandle *handle)
{
	return NULL;
}

void sharedMemoryEnableEditorMode(void)
{

}

void sharedMemoryCancelAllLocks(void)
{

}

size_t sharedMemoryBytesAlloced(SharedMemoryHandle *handle)
{
	return 0;
}

void sharedMemorySetBytesAlloced(SharedMemoryHandle *handle, size_t bytes_alloced)
{

}


#endif // !PLATFORM_CONSOLE

// Disable shared memory
AUTO_COMMAND ACMD_CMDLINE;
void NoSharedMemory(int disable)
{
	// This command is now also handled in WAIT_FOR_DEBUGGER
#if !PLATFORM_CONSOLE
	if(disable)
	{
		printf("Shared memory disabled by command line '-NoSharedMemory'\n");
		sharedMemorySetMode(SMM_DISABLED);
	}
#endif
}

AUTO_COMMAND ACMD_CMDLINE;
void EnableSharedMemory(int enable)
{
	if(!enable)
	{
		printf("Shared memory disabled by command line '-EnableSharedMemory 0'\n");
		sharedMemorySetMode(SMM_DISABLED);
	}
}

// Disable shared memory
AUTO_COMMAND ACMD_CMDLINE;
void SemiSharedMemory(int enable)
{
#if !PLATFORM_CONSOLE
	if(enable)
	{
		printf("Shared memory set to 'semi' mode by command line '-SemiSharedMemory'\n");
		gSemiSharedMode = true;
	}
#endif
}

AUTO_COMMAND;
void printSharedMemory(void)
{
	printSharedMemoryUsage();
}

void *sharedMemoryDebugGetScratch(const char *chunk_name, size_t size)
{
#if !PLATFORM_CONSOLE
	DWORD oldValue;
	SharedMemoryHandle *handle = NULL;
	SM_AcquireResult res = sharedMemoryAcquire(&handle, chunk_name);
	if (res == SMAR_DataAcquired)
	{
		assert(size == sharedMemoryGetSize(handle));
		VirtualProtect(handle->data,size,PAGE_READWRITE,&oldValue);
		return handle->data;
	}
	if (res == SMAR_FirstCaller)
	{
		sharedMemorySetSize(handle, size);
		sharedMemoryUnlock(handle);
		VirtualProtect(handle->data,size,PAGE_READWRITE,&oldValue);
		return handle->data;
	}
#endif // !PLATFORM_CONSOLE
	assertmsg(0, "Got un-handled return for sharedMemoryDebugGetScratch");
}

static char *queuedSharedMemoryAlertString = NULL;
static bool queuedAlertsSent = false;

void
sharedMemoryQueueAlertString(const char *str)
{
    if ( queuedAlertsSent )
    {
        // Queued alerts have been sent already, so just go ahead and send this alert now rather than queueing.
        TriggerAlert(allocAddString("SHAREDMEMORYDISABLED"), str, ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, GetAppGlobalType(), GetAppGlobalID(), GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0);
    }
    else
    {
	    estrAppend2(&queuedSharedMemoryAlertString, str);
    }
}

void
sharedMemoryProcessQueuedAlerts(void)
{
	if (queuedSharedMemoryAlertString && queuedSharedMemoryAlertString[0])
	{
		TriggerAlert(allocAddString("SHAREDMEMORYDISABLED"), queuedSharedMemoryAlertString, ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, GetAppGlobalType(), GetAppGlobalID(), GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0);
	}

    queuedAlertsSent = true;
}