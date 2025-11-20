/***************************************************************************



***************************************************************************/

#include "earray.h"
#include "wininclude.h"
#include "cmdparse.h"
#include "stashtable.h"

#include "TimedCallback.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

static TimedCallback **sppNormalCallbacks = NULL;
static TimedCallback **sppSS2000Callbacks = NULL;
static CRITICAL_SECTION	s_CallbacksCrit;
static bool s_CallbackInited=false;

void TimedCallback_Startup()
{
	if (!s_CallbackInited)
	{
		InitializeCriticalSection(&s_CallbacksCrit);
		s_CallbackInited = true;
	}
}


int SortCallbacksBySS2000(const TimedCallback **ppCB1, const TimedCallback **ppCB2)
{
	U32 iTime1 = (*ppCB1)->iSecsSince2000ToRunAt;
	U32 iTime2 = (*ppCB2)->iSecsSince2000ToRunAt;

	if (iTime2 < iTime1)
	{
		return 1;
	}

	if (iTime2 > iTime1)
	{
		return -1;
	}
	return 0;
}


static TimedCallback *TimedCallback_AddInternal(TimedCallbackFunc callback, UserData userData, bool runOnce, F32 time, U32 iSecsSince2000ToRunAt, const char* name)
{
	if (s_CallbackInited)
	{
		TimedCallback *cb = callocStruct(TimedCallback);
		cb->callback = callback;
		cb->userData = userData;
		cb->name = name;

		if (iSecsSince2000ToRunAt)
		{
			cb->iSecsSince2000ToRunAt = iSecsSince2000ToRunAt;
		}
		else
		{
			cb->interval = time;
			cb->runOnce = runOnce;
		}

		EnterCriticalSection(&s_CallbacksCrit);
		if (iSecsSince2000ToRunAt)
		{
			eaPush(&sppSS2000Callbacks, cb);
			eaQSort(sppSS2000Callbacks, SortCallbacksBySS2000);
		}
		else
		{
			eaPush(&sppNormalCallbacks, cb);
		}
		LeaveCriticalSection(&s_CallbacksCrit);
		return cb;
	} else {
		printf("Warning: TimedCallback_AddInternal called before TimedCallback_Startup(), callback ignored.\n");
		return NULL;
	}
}


TimedCallback *TimedCallback_Add_dbg(TimedCallbackFunc callback, UserData userData, F32 interval, const char* name)
{
	return TimedCallback_AddInternal(callback, userData, false, interval, 0, name);
}

TimedCallback *TimedCallback_Run_dbg(TimedCallbackFunc callback, UserData userData, F32 secondsFromNow, const char* name)
{
	return TimedCallback_AddInternal(callback, userData, true, secondsFromNow, 0, name);
}

void timedCallbackCmdParse(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	globCmdParse(userData);
}

TimedCallback *TimedCallback_RunCmdParse_dbg(const char *command, F32 secondsFromNow, const char* name)
{
	return TimedCallback_Run_dbg(timedCallbackCmdParse, (UserData)command, secondsFromNow, name);
}


TimedCallback *TimedCallback_RunAt_dbg(TimedCallbackFunc callback, UserData userData, U32 secondsSince2k, const char* name)
{
	return TimedCallback_AddInternal(callback, userData, false, 0, secondsSince2k, name);
}

void TimedCallback_RemoveInternal(TimedCallback *callback)
{
	EnterCriticalSection(&s_CallbacksCrit);
	if(eaFindAndRemove(&sppNormalCallbacks, callback) >= 0)
	{
		free(callback);
		LeaveCriticalSection(&s_CallbacksCrit);
		return;
	}

	if(eaFindAndRemove(&sppSS2000Callbacks, callback) >= 0)
	{
		free(callback);
		LeaveCriticalSection(&s_CallbacksCrit);
		return;
	}

	LeaveCriticalSection(&s_CallbacksCrit);
}

bool TimedCallback_Remove(TimedCallback *callback)
{
	EnterCriticalSection(&s_CallbacksCrit);
	if(eaFind(&sppNormalCallbacks, callback) >= 0)
	{
		callback->remove = true;
		LeaveCriticalSection(&s_CallbacksCrit);
		return true;
	}

	if(eaFind(&sppSS2000Callbacks, callback) >= 0)
	{
		callback->remove = true;
		LeaveCriticalSection(&s_CallbacksCrit);
		return true;
	}

	LeaveCriticalSection(&s_CallbacksCrit);

	return false;
}

bool TimedCallback_RemoveByFunction(TimedCallbackFunc callback)
{
	S32 i;
	bool bFound = false;
	EnterCriticalSection(&s_CallbacksCrit);
	for (i = eaSize(&sppNormalCallbacks) - 1; i >= 0; i--)
	{
		if (sppNormalCallbacks[i]->callback == callback)
		{
			sppNormalCallbacks[i]->remove = true;
			bFound = true;
		}
	}

	for (i = eaSize(&sppSS2000Callbacks) - 1; i >= 0; i--)
	{
		if (sppSS2000Callbacks[i]->callback == callback)
		{
			sppSS2000Callbacks[i]->remove = true;
			bFound = true;
		}
	}

	LeaveCriticalSection(&s_CallbacksCrit);
	return bFound;
}

void TimedCallback_Tick(F32 timestep, F32 scale)
{
	const U32 maxStackBytes = 128 * 1024;
	U32 i, iNormalCount, iSS2000Count;
	TimedCallback **ppNormalCopy = NULL;
	TimedCallback **ppSS2000Copy = NULL;
	size_t iBytesAllocedNormal = 0, iBytesAllocedSS2000 = 0;
	U32 iCurTime = timeSecondsSince2000();

	PERFINFO_AUTO_START_FUNC();

	EnterCriticalSection(&s_CallbacksCrit);
	iNormalCount = eaSize(&sppNormalCallbacks);
	if(iNormalCount){
		iBytesAllocedNormal = sizeof(ppNormalCopy[0]) * iNormalCount;
		if(iBytesAllocedNormal > maxStackBytes){
			ppNormalCopy = malloc(iBytesAllocedNormal);
		}else{
			ppNormalCopy = _alloca(iBytesAllocedNormal);
		}
		memcpy(ppNormalCopy, sppNormalCallbacks, iBytesAllocedNormal);
	}

	iSS2000Count = eaSize(&sppSS2000Callbacks);
	if(iSS2000Count){
		iBytesAllocedSS2000 = sizeof(ppSS2000Copy[0]) * iSS2000Count;
		if(iBytesAllocedSS2000 > maxStackBytes){
			ppSS2000Copy = malloc(iBytesAllocedSS2000);
		}else{
			ppSS2000Copy = _alloca(iBytesAllocedSS2000);
		}
		memcpy(ppSS2000Copy, sppSS2000Callbacks, iBytesAllocedSS2000);
	}

	LeaveCriticalSection(&s_CallbacksCrit);

	for (i = 0; i < iNormalCount; i++)
	{
		TimedCallback *callback = ppNormalCopy[i];
		// if callback is flagged for removal, remove it
#pragma warning(suppress:6001) // /analyze flags "Using uninitialized memory '*callback'"
		if (callback->remove)
		{
			TimedCallback_RemoveInternal(callback);
			continue;
		}

		// timestep, as it comes in, is already scaled. if the scale is off
		// for this callback, unscale the time.
		callback->intervalCount += timestep / (callback->scale ? 1 : (scale?scale:0.00001));
		if (callback->intervalCount >= callback->interval)
		{
			PerfInfoGuard*	piGuard;
			S32				piStarted = 0;

			if(	callback->name &&
				PERFINFO_RUN_CONDITIONS)
			{
				static StashTable	st;
				StaticCmdPerf*		p;

				if(!st){
					st = stashTableCreateWithStringKeys(10, StashDefault);
				}

				if(!stashFindPointer(st, callback->name, &p)){
					p = callocStruct(StaticCmdPerf);
					p->name = callback->name;
					stashAddPointer(st, p->name, p, false);
				}

				PERFINFO_AUTO_START_STATIC_GUARD(p->name, &p->pi, 1, &piGuard);

				piStarted = 1;
			}

			callback->callback(callback, callback->intervalCount, callback->userData);

			if(piStarted){
				PERFINFO_AUTO_STOP_GUARD(&piGuard);
			}

			callback->intervalCount = 0.f;
			if (callback->runOnce)
				TimedCallback_Remove(callback);
		}
	}

	for (i = 0; i < iSS2000Count; i++)
	{
		TimedCallback *callback = ppSS2000Copy[i];
		// if callback is flagged for removal, remove it
#pragma warning(suppress:6001) // /analyze flags "Using uninitialized memory '*callback'"
		if (callback->remove)
		{
			TimedCallback_RemoveInternal(callback);
			continue;
		}

		if (callback->iSecsSince2000ToRunAt <= iCurTime)
		{
			callback->callback(callback, 0, callback->userData);
			TimedCallback_Remove(callback);
		}
		else
		{
			//we keep these sorted, so once we hit one that's not ready to call, all remaining ones must be not ready to call
			break;
		}
	}

	if(iBytesAllocedNormal > maxStackBytes){
		free(ppNormalCopy);
	}
	if(iBytesAllocedSS2000 > maxStackBytes){
		free(ppSS2000Copy);
	}

	PERFINFO_AUTO_STOP();
}


TimedCallback *TimedCallback_RunAtTimeOfDay_dbg(TimedCallbackFunc callback, UserData userData, U32 iTimeOfDay, const char* name)
{
	SYSTEMTIME t = {0};
	
	int iReqTimeHours = iTimeOfDay / 100;
	int iReqTimeMinutes = iTimeOfDay % 100;
	int iReqSeconds = iReqTimeHours * 3600 + iReqTimeMinutes * 60;
	int iCurSeconds;
	int iSecondsInFuture;

	timerLocalSystemTimeFromSecondsSince2000(&t, timeSecondsSince2000());

	iCurSeconds = t.wHour * 3600 + t.wMinute * 60 + t.wSecond;

	iSecondsInFuture = iReqSeconds - iCurSeconds;
	if (iSecondsInFuture < 5)
	{
		iSecondsInFuture += 24 * 3600;
	}

	return TimedCallback_RunAt_dbg(callback, userData, timeSecondsSince2000() + iSecondsInFuture, name);
}
