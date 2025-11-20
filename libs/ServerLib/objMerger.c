#include "error.h"
#include "EString.h"
#include "objMerger.h"
#include "wininclude.h"
#include "utf8.h"

// Merger mutex owned by this process, if any.
static HANDLE shMergerMutex = NULL;

// Get the Windows global mutex name associated with a merger.
static void CreateStackMergerMutexName(char **estrMutexName, const char *pMergerName)
{
	devassert(!*estrMutexName);
	estrStackCreate(estrMutexName);
	estrCopy2(estrMutexName, "Global\\CrypticMergeLock-");
	estrAppend2(estrMutexName, pMergerName);
}

// Return true if a merger is running.
bool IsMergerRunning(const char *pMergerName)
{
	char *name = NULL;
	HANDLE mutex;
	int err;
	bool success;

	// Return true if we're the merger.
	if (shMergerMutex)
		return true;

	// Attempt to open mutex.
	CreateStackMergerMutexName(&name, pMergerName);
	mutex = OpenMutex_UTF8(0, false, name);
	err = GetLastError();
	estrDestroy(&name);

	// Check if it worked.
	if (!mutex)
	{
		if (err == ERROR_ACCESS_DENIED)
			return true;
		if (err != ERROR_FILE_NOT_FOUND)
			WinErrorf(err, "IsMergerRunning(): Unable to open mutex \"%s\"", name);
		return false;
	}

	// The mutex exists.
	success = CloseHandle(mutex);
	if (!success)
		WinErrorf(GetLastError(), "UnlockMerger(): Unable to release lock.");

	return true;
}

// Acquire a lock that indicates that the specified merger is running.  Return false if one is already running.
bool LockMerger(const char *pMergerName)
{
	char *name = NULL;
	int err;

	// Don't allow multiple locks or recursive locks.
	if (shMergerMutex)
		return false;

	// Create and acquire mutex.
	CreateStackMergerMutexName(&name, pMergerName);
	shMergerMutex = CreateMutex_UTF8(NULL, true, name);
	err = GetLastError();
	if (shMergerMutex && err == ERROR_ALREADY_EXISTS)
	{
		bool success;
		ANALYSIS_ASSUME(shMergerMutex != NULL);
		success = CloseHandle(shMergerMutex);
		shMergerMutex = NULL;
		if (!success)
			WinErrorf(GetLastError(), "UnlockMerger(): Unable to release lock.");
		estrDestroy(&name);
		return false;
	}

	// Check for errors.
	if (!shMergerMutex)
	{
		WinErrorf(err, "LockMerger(): Unable to acquire \"%s\"", name);
		estrDestroy(&name);
		return false;
	}

	estrDestroy(&name);
	return true;
}

// Release the merger lock, indicating that the merger is no longer running.
void UnlockMerger()
{
	bool success;

	devassert(shMergerMutex);
	success = CloseHandle(shMergerMutex);
	shMergerMutex = NULL;
	if (!success)
		WinErrorf(GetLastError(), "UnlockMerger(): Unable to release lock.");
}
