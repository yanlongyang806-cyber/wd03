// Fast intra-process semaphore

// This is a relatively normal two-stage locking implementation.  It consists of an atomic count fast semaphore,
// a slower Win32 semaphore, and a count and mutex for signaling waiters from the queue.
//
// In the first stage, we try to acquire the semaphore quickly using interlocked arithmetic.  If this fails, we
// spin for a brief while trying to acquire the lock in case it is about to be released.  This is an optimization
// only on multiprocessor machines where the lock is not being held very long, but is generally safe to do even
// if the lock is being held longer because it will be small compared to the time spent using the kernel lock
// and actual operation inside the lock.
//
// If the first stage fails, the second stage is just a fallback to the regular Win32 semaphore.  When signaling
// the semaphore, we first check for kernel waiters.  This is done using the double-checked locking pattern, to
// atomically release the waiter, but optimize for the case where there is no kernel waiter.  If there are no
// waiters, we just do a fast atomic increment instead.
//
// One funny bit about this implementation is that the way the Win32 semaphore is used is a bit strange in that
// the first thread to use it actually will not wait on it.  This is an unnecessary system call, which might
// slow us down for small max count values.  However, it's not possible to create a Win32 semaphore with a
// value of 0, and that doesn't make sense anyway.  It's possible some other Win32 synchronization primitive
// is more appropriate here; the obvious one is an Event, but it is harder to use safely.

#include "semaphore.h"
#include "timing.h"
#include "wininclude.h"

// By default, try to spin this many times acquiring the semaphore before we fall back to the kernel queue.
#define CRYPTIC_SEMAPHORE_SPIN_COUNT 100

#ifndef _WIN32
#error CrypticSemaphore is not implemented on this platform
#endif

// This is the fields in CrypticSemaphoreImplExternal, plus additional internal fields.
// The external fields are the one the inline fast phase one needs; all the rest are internal,
// to avoid pulling in Windows headers.
// Any fields added in the External section must be synchronized to struct CrypticSemaphoreImplExternal.
// If the external fields are reordered, semaphoreSignalMulti() must be adjusted for _InterlockedCompareExchange64().
struct CrypticSemaphoreImplInternal
{
	// External
	volatile long iFastCount;			// First stage spin lock, when using CRYPTIC_SEMAPHORE_FAST.
	volatile long iWaiters;				// Number of threads waiting on the semaphore object

	// Internal
	HANDLE semaphore;					// Binary semaphore, for fallback queued wait
	long iMaxCount;						// Initial semaphore value
	unsigned uSpinCount;				// Number of times to spin before falling back to kernel semaphore
	CRITICAL_SECTION mutex;				// Mutex for waiters release
};

// Get the internal pointer for a semaphore object.
static struct CrypticSemaphoreImplInternal *GetInternalPointer(CrypticSemaphore semaphore)
{
	return (struct CrypticSemaphoreImplInternal *)semaphore;
}

// Fallback signal
void semaphoreSignal_dbg(CrypticSemaphore external, long count)
{
	struct CrypticSemaphoreImplInternal *semaphore;

	PERFINFO_AUTO_START_FUNC_L2();

	semaphore = GetInternalPointer(external);
	
	// Check for anyone waiting on the kernel object, and release one of them if there is.
	// Inside the critical section, other threads can increase iWaiters, but not decrease it, so once we know that
	// iWaiters is nonzero, it is safe to release without worrying about an ABA conflict.
	EnterCriticalSection(&semaphore->mutex);
	if (semaphore->iWaiters)
	{
		bool success;
		long waiters_to_release = MIN(semaphore->iWaiters, count);
		assert(waiters_to_release >= 0);
		InterlockedExchangeAdd(&semaphore->iWaiters, -waiters_to_release);
		success = ReleaseSemaphore(semaphore->semaphore, waiters_to_release, NULL);
		assertmsgf(success, "Thread synchronization failure: ReleaseSemaphore() has failed (GetLastError() = %d)", GetLastError());
		count -= waiters_to_release;
	}
	LeaveCriticalSection(&semaphore->mutex);

	PERFINFO_AUTO_STOP_L2();

	// Try again.
	// Note: We rely on tail call optimization here.
	if (count)
		semaphoreSignalMulti(external, count);
}

// Fallback wait
void semaphoreWait_dbg(CrypticSemaphore external)
{
	struct CrypticSemaphoreImplInternal *semaphore;
	unsigned spin_count;
	unsigned max_spin_count;
	DWORD result;

	PERFINFO_AUTO_START_FUNC_L2();

	semaphore = GetInternalPointer(external);

	// Undo the failed lock.
	InterlockedIncrement(&semaphore->iFastCount);

	// Spin for a limited number of times to try to acquire the semaphore without falling back to the kernel queue.
	max_spin_count = semaphore->uSpinCount;
	for (spin_count = 0; spin_count != max_spin_count; ++spin_count)
	{
		long new_count = InterlockedDecrement(&semaphore->iFastCount);
		if (new_count >= 0)
		{
			PERFINFO_AUTO_STOP_L2();
			return;
		}
		InterlockedIncrement(&semaphore->iFastCount);
	}

	// Fall back to the OS semaphore queue.
	InterlockedIncrement(&semaphore->iWaiters);
	WaitForSingleObjectWithReturn(semaphore->semaphore, INFINITE, result);
	assertmsgf(result == WAIT_OBJECT_0, "Thread synchronization failure: WaitForSingleObject() has returned %u (GetLastError() = %d)", result, GetLastError());

	PERFINFO_AUTO_STOP_L2();
}

// Initialize.
// The double-checked locking pattern is used here so that redundant calls to this function are efficient.
void semaphoreInit_dbg(SA_PARAM_NN_VALID CrypticSemaphore *pExternal, long iMaxCount, unsigned uSpinCount MEM_DBG_PARMS)
{
	static CRITICAL_SECTION global_semaphore_init;
	volatile CrypticSemaphore *pExternalVolatile = pExternal;
	struct CrypticSemaphoreImplInternal *semaphore;

	PERFINFO_AUTO_START_FUNC();

	// Validate parameters.
	assert(iMaxCount > 0);

	// Acquire the global semaphore initialization mutex, so that this semaphore is only initialized once.
	ATOMIC_INIT_BEGIN;
	InitializeCriticalSection(&global_semaphore_init);
	ATOMIC_INIT_END;
	EnterCriticalSection(&global_semaphore_init);
	if (*pExternalVolatile)
	{
		LeaveCriticalSection(&global_semaphore_init);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Initialize semaphore.
	semaphore = smalloc(sizeof(*semaphore));
	semaphore->iFastCount = iMaxCount - 1;  // One for the fallback semaphore
	semaphore->iWaiters = 0;
	semaphore->semaphore = CreateSemaphore(NULL, 1, 2048, NULL);	// Note: You might want to set the max value here to 1.  However, this value can
																	// exceed 1 due to the fact that iWaiters is incremented before waiting.  Instead,
																	// this value is set to the maximum amount of threads expected in runtime operation.
																	// The current value is taken from the CreateThread() documentation in the Windows
																	// SDK, 9/7/2011.
	assertmsgf(semaphore->semaphore, "Thread synchronization failure: CreateSemaphore() has failed (GetLastError() = %d)", GetLastError());
	semaphore->iMaxCount = iMaxCount;
	semaphore->uSpinCount = uSpinCount ? uSpinCount - 1 : CRYPTIC_SEMAPHORE_SPIN_COUNT;
	InitializeCriticalSection(&semaphore->mutex);

	// Assign result and leave.
	*pExternal = (struct CrypticSemaphoreImplExternal *)semaphore;
	LeaveCriticalSection(&global_semaphore_init);

	PERFINFO_AUTO_STOP_FUNC();
}

// Destroy a semaphore.
// This call is not thread-safe.
void semaphoreDestroy(CrypticSemaphore *pExternal)
{
	struct CrypticSemaphoreImplInternal *semaphore;
	bool success;
	long previous;

	PERFINFO_AUTO_START_FUNC();

	semaphore = GetInternalPointer(*pExternal);

	// Make sure it's OK to destroy right now.
	success = TryEnterCriticalSection(&semaphore->mutex);
	devassert(success);
	devassert(semaphore->iFastCount == semaphore->iMaxCount - 1);
	devassert(semaphore->iWaiters == 0);
	success = ReleaseSemaphore(semaphore->semaphore, 1, &previous);
	devassert(success && previous == 1);
	LeaveCriticalSection(&semaphore->mutex);

	// Release Win32 objects.
	success = CloseHandle(semaphore->semaphore);
	devassertmsgf(success, "CloseHandle() err = %d", GetLastError());
	DeleteCriticalSection(&semaphore->mutex);

	// Free memory.
	free(semaphore);

	// Set pointer to null.
	*pExternal = NULL;

	PERFINFO_AUTO_STOP_FUNC();
}

// Thread data for semaphoreSetMaxAndWaitThread()
struct semaphoreSetMaxAndWaitThreadData {
	CrypticSemaphore semaphore;
	long iMaxCount;
};

// Call semaphoreSetMaxAndWait(), in a background thread.
void semaphoreSetMaxAndWaitThread(void *parameter)
{
	struct semaphoreSetMaxAndWaitThreadData *data = parameter;
	semaphoreSetMaxAndWait(data->semaphore, data->iMaxCount);
}

// Set the maximum count value of a semaphore.
void semaphoreSetMax(CrypticSemaphore external, long iMaxCount)
{
	struct CrypticSemaphoreImplInternal *semaphore;
	struct semaphoreSetMaxAndWaitThreadData *data;

	PERFINFO_AUTO_START_FUNC();

	semaphore = GetInternalPointer(external);
	if (semaphore->iMaxCount > iMaxCount)
	{
		uintptr_t thread;

		// We need to spawn a background thread to decrease the count, because it may take a while.
		data = malloc(sizeof(*data));
		data->semaphore = external;
		data->iMaxCount = iMaxCount;
		thread = _beginthread(semaphoreSetMaxAndWaitThread, 0, data);
		assertmsgf(thread != -1, "Thread synchronization failure: _beginthread() has failed (errno() = %d)", errno);
	}
	else if (semaphore->iMaxCount == iMaxCount)
	{
		// No change necessary.
	}
	else
	{

		// Increasing the count can be done immediately.
		while (semaphore->iMaxCount < iMaxCount)
		{
			semaphoreSignal(external);
			++semaphore->iMaxCount;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

// Like semaphoreSetMax(), but don't return until it has been set.
void semaphoreSetMaxAndWait(CrypticSemaphore external, long iMaxCount)
{
	struct CrypticSemaphoreImplInternal *semaphore;

	PERFINFO_AUTO_START_FUNC();

	semaphore = GetInternalPointer(external);
	if (semaphore->iMaxCount > iMaxCount)
	{
		// Keep waiting until the value is right.
		while (semaphore->iMaxCount > iMaxCount)
		{
			semaphoreWait(external);
			--semaphore->iMaxCount;
		}
	}
	else if (semaphore->iMaxCount == iMaxCount)
	{
		// No change necessary.
	}
	else
	{

		// Use semaphoreSetMax() because there will be no wait.
		semaphoreSetMax(external, iMaxCount);
	}

	PERFINFO_AUTO_STOP_FUNC();
}
