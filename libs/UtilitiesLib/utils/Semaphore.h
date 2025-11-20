// Fast intra-process semaphore

// This is a fast, low-overhead semaphore optimized for the following situations:
//   -Intra-process
//   -High contention
//   -Lock held only briefly
// The performance of the semaphore can be tuned for lock length (for instance, where the lock length is much longer than the time
// necessary to acquire the lock) by adjusting the spin count parameter, but this is only necessary for the most extreme optimization
// scenarios.  In the default configuration, the lock overhead should be a small proportion of the time spent inside the lock in all
// but the most degenerate cases.
// This semaphore is not appropriate in the following situations:
//   -Inter-process synchronization
//   -Fairness among threads
//   -Very small count values, such as a binary semaphore: It will work, but performance will not be optimal

// See Semaphore.c for implementation details.

#ifndef CRYPTIC_SEMAPHORE_H
#define CRYPTIC_SEMAPHORE_H



// *** Internal implementation stuff: skip to next section for public interface ***

// Implementation: If you add anything here, it must also be added at the beginning of struct CrypticSemaphoreImplInternal.
// Debugging information:
//   Number of free slots = iCrypticSemaphoreInternalFastCount + (iWaiters ? 0 : 1)
//     (Note that iCrypticSemaphoreInternalFastCount can be temporarily negative.)
//   Number of waiters = iCrypticSemaphoreInternalWaiters - 1
//     (This is also equal to the internal value of the Windows semaphore
//   To see internal state for debugging, cast to (CrypticSemaphoreImplInternal *).
//   To verify the Windows semaphore value, use the following in WinDbg:
//     !handle [put semaphore handle value here] f
struct CrypticSemaphoreImplExternal
{
	volatile long iCrypticSemaphoreInternalFastCount;	// WARNING: Do not use from external code unless you want it to be subtly wrong and broken.
	volatile long iCrypticSemaphoreInternalWaiters;		// WARNING: Do not use from external code unless you want it to be subtly wrong and broken.
};

typedef struct CrypticSemaphoreImplExternal *CrypticSemaphore;

// Do not call this directly.
void semaphoreSignal_dbg(CrypticSemaphore semaphore, long count);

// Do not call this directly.
void semaphoreWait_dbg(CrypticSemaphore semaphore);

// Do not call this directly.
void semaphoreInit_dbg(SA_PARAM_NN_VALID CrypticSemaphore *pSemaphore, long iMaxCount, unsigned uSpinCount MEM_DBG_PARMS);



// *** Public interface ***

// Initialize a semaphore to a particular count.  CrypticSemaphore must be zeroed, but this function may be called more than once.
// Subsequent calls do nothing and are ignored, even if iMaxCount is different.  This call is thread-safe.
#define semaphoreInit(pSemaphore, iMaxCount, uSpinCount) semaphoreInit_inline(pSemaphore, iMaxCount, uSpinCount MEM_DBG_PARMS_INIT)
__forceinline static void semaphoreInit_inline(CrypticSemaphore *pSemaphore, long iMaxCount, unsigned uSpinCount MEM_DBG_PARMS)
{
	if (!*pSemaphore)
		semaphoreInit_dbg(pSemaphore, iMaxCount, uSpinCount MEM_DBG_PARMS_CALL);
}

// Destroy a semaphore.
// This call is not thread-safe: the caller must ensure all other users have terminated.
void semaphoreDestroy(CrypticSemaphore *pSemaphore);

// V(N): Add N to a semaphore, which might free up to N blocked threads.
// WARNING: Passing a negative number here is not allowed, and will screw things up really bad.
// This relies on the ordering of CrypticSemaphoreImplInternal.
__forceinline static void semaphoreSignalMulti(CrypticSemaphore semaphore, long count)
{
	S64 iFastCountAndWaiters;
	S64 iWaiters;
	S64 iFastCountAndWaitersWithSignal;
	S64 iFastCountAndWaitersNow;

	do
	{

		// Read current iFastCount and iWaiters values.
		iFastCountAndWaiters = *(S64 *)&semaphore->iCrypticSemaphoreInternalFastCount;
	
		// Separate out iWaiters value, and add count to the iFasterValue.
#if !(defined(_M_IX86) || defined (_M_AMD64)) || defined(PLATFORM_CONSOLE)
#error TODO Add big endian support
#endif
		iWaiters = iFastCountAndWaiters >> 32;
		iFastCountAndWaitersWithSignal = ((U32)iFastCountAndWaiters + count) + (iWaiters << 32);	// iFastCountAndWaiters can be negative.
	
		// If iWaiters isn't zero, we need to release the Windows semaphore.
		if (iWaiters != 0)
		{
			semaphoreSignal_dbg(semaphore, count);
			return;
		}

		// If the above assumptions are still true, add count to iFastCount.
		iFastCountAndWaitersNow = _InterlockedCompareExchange64((S64 *)&semaphore->iCrypticSemaphoreInternalFastCount,
			iFastCountAndWaitersWithSignal, iFastCountAndWaiters);

		// If something changed, start over from the beginning.
	}
	while (iFastCountAndWaitersNow != iFastCountAndWaiters);
}

// V(): Add one to a semaphore, which might free one blocked thread.
__forceinline static void semaphoreSignal(CrypticSemaphore semaphore)
{
	semaphoreSignalMulti(semaphore, 1);
}

// P(): Subtract one from a semaphore, which will block if the value is already zero.
__forceinline static void semaphoreWait(CrypticSemaphore semaphore)
{
	long new_count = _InterlockedDecrement(&semaphore->iCrypticSemaphoreInternalFastCount);
	if (new_count < 0)
		semaphoreWait_dbg(semaphore);
}

// Set a new maximum count value of a semaphore.
void semaphoreSetMax(CrypticSemaphore semaphore, long iMaxCount);

// Like semaphoreSetMax(), but don't return until it has been set.
void semaphoreSetMaxAndWait(CrypticSemaphore semaphore, long iMaxCount);



// Safeguard against people relying on auto-complete and similar.
#define iCrypticSemaphoreInternalFastCount whatever_you_are_doing_is_a_really_bad_idea
#define iCrypticSemaphoreInternalWaiters whatever_you_are_doing_is_a_really_bad_idea

#endif  // CRYPTIC_SEMAPHORE_H
