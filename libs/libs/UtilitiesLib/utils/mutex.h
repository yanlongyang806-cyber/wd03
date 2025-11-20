#pragma once

// Lock object: this must be zero-initialized.
// This is actually a wrapper for CRITICAL_SECTION, specifically constructed so that it doesn't have to pull in <windows.h>
typedef struct CrypticLock {
	struct _RTL_CRITICAL_SECTION *criticalSection;		// Internal critical section object, non-zero if initialized.  This also forces proper alignment.
#ifdef _WIN64
	char storage[40];		// sizeof(CRITICAL_SECTION) on x64
#else
	char storage[24];		// sizeof(CRITICAL_SECTION) on x86
#endif
} CrypticLock;

// Acquire lock.  If necessary, initialize it.
void Lock(CrypticLock *lock);

// Acquire lock, if noone currently has it locked.  If necessary, initialize it.  Return true if we got it.
bool TryLock(CrypticLock *lock);

// Release lock.
void Unlock(CrypticLock *lock);

// Destroy a lock to free resources associated with it.
void DestroyCrypticLock(CrypticLock *lock);

// Normal muticies must be released in the same thread as what grabbed it, these
//  get acquired/released in a utility thread.
//  2.7x as much time compared to just a single-thread mutex (approx 0.05ms to acquire and release)

// WARNING: ThreadAgnosticMutexes are for between-PROCESS locking (such as file access locks), not
//			for between-THREAD locking (such as what you're probably looking for).  You should probably
//			use a CriticalSection unless you know the difference.  You can grab a thread agnostic mutex
//			from multiple threads simultaneously, but only a single process.
#if _PS3
//... in fact they are never used as such for PS3
#define acquireThreadAgnosticMutex(pname) AcquireNamedMutex(pname)
#define releaseThreadAgnosticMutex(h) ReleaseNamedMutex(h)
#else
typedef struct ThreadAgnosticMutexStruct *ThreadAgnosticMutex;
ThreadAgnosticMutex acquireThreadAgnosticMutex(const char *name);
void releaseThreadAgnosticMutex(ThreadAgnosticMutex hMutex);
#endif

//make a string into a string that is a legal mutex name 
void makeLegalMutexName(char mutexName[CRYPTIC_MAX_PATH], const char *pInName);


// usage: put return testThreadAgnosticMutex(argc, argv); in main()
int testThreadAgnosticMutex(int argc, char **argv);

// for testing
#define DEBUG_RWL_TIMEOUTS 0

#if DEBUG_RWL_TIMEOUTS
void printTimeouts(void);
#else
#define printTimeouts()
#endif

void readLockU32(U32* lock);
void readUnlockU32(U32* lock);
void writeLockU32(U32* lock, U32 hasReadLock);
void writeUnlockU32(U32* lock);

typedef struct ReadWriteLock ReadWriteLock;

void InitializeReadWriteLock(ReadWriteLock *rwl);
ReadWriteLock *CreateReadWriteLock(void);
void DestroyReadWriteLock(ReadWriteLock *rwl);
void rwlReadLock(ReadWriteLock* rwl);
void rwlReadUnlock(ReadWriteLock* rwl);
void rwlWriteLock(ReadWriteLock* rwl, U32 hasReadLock);
void rwlWriteUnlock(ReadWriteLock* rwl);

#if !_PS3
// Functions to acquire and count global mutexes.
//
// Returns: valid handle if acquired, NULL otherwise.
//
// prefix: can be NULL.
// suffix: can be NULL.
// pid: if -1, uses current pid.

typedef void *HANDLE;  // FIXME: Eliminate this.

HANDLE acquireMutexHandleByPID(const char* prefix, S32 pid, const char* suffix);
S32 countMutexesByPID(const char* prefix, const char* suffix);

typedef struct WaitThreadGroup WaitThreadGroup;
typedef void (*WaitThreadGroupCallback)(void* userPointer,
										HANDLE handle,
										void* handleUserPointer);

S32 wtgCreate(WaitThreadGroup** wtgOut);

S32 wtgAddHandle(	WaitThreadGroup* wtg,
					HANDLE handle,
					WaitThreadGroupCallback callback,
					void* userPointer);

void wtgRemoveHandle(	WaitThreadGroup* wtg,
						HANDLE handle,
						void* userPointer);

void wtgWait(	WaitThreadGroup* wtg,
				void* userPointer,
				U32 msTimeout);
				
#endif

// CrypticalSection is a specialized Critical Section replacement designed by Martin.
// The main feature is that it has some additional information that shows up in the Cryptic Profiler regarding lock contention.
// You probably shouldn't use this unless you have a specific reason to.  Use CrypticLock instead.
typedef struct CrypticalSection {
	U32				ownerThreadID;
	U32				ownerEnterCount;
	U32				sharedEnterCount;
	U32				createEventLock;
	void*			eventWait;
	U64				cyclesStart;
} CrypticalSection;

void csEnter(CrypticalSection* cs);
void csLeave(CrypticalSection* cs);
