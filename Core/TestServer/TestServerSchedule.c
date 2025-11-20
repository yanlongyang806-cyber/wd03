#include "TestServerSchedule.h"
#include "EString.h"
#include "TestServerLua.h"
#include "TestServerReport.h"
#include "timing.h"
#include "windefinclude.h"

#include "TestServerSchedule_c_ast.h"

AUTO_STRUCT;
typedef struct TestServerScheduleEntry
{
	// Day and time to run the thing
	int	iDay;
	int iHour;
	int iMinute;
	int iSecond;

	// Info for repeating the thing
	int iRepeatInterval; // if 0, repeat at occurrences of specified day/time
	int iNumRepeats;
	int	iTimesRun;
	int	iNextRun;

	// Does it take absolute precedence?
	bool bImportant;
} TestServerScheduleEntry;

AUTO_STRUCT;
typedef struct TestServerSchedule
{
	char						*pScript; AST(ESTRING KEY)
	TestServerScheduleEntry		**ppEntries;
} TestServerSchedule;

static TestServerSchedule **eaSchedules = NULL;
static CRITICAL_SECTION cs_eaSchedules;

void TestServer_InitSchedules(void)
{
	InitializeCriticalSection(&cs_eaSchedules);
}

static TestServerSchedule *TestServer_AddSchedule(const char *script)
{
	TestServerSchedule *pSchedule = StructCreate(parse_TestServerSchedule);
	estrPrintf(&pSchedule->pScript, "%s", script);

	EnterCriticalSection(&cs_eaSchedules);
	if(!eaSchedules)
	{
		eaIndexedEnable(&eaSchedules, parse_TestServerSchedule);
	}

	eaIndexedAdd(&eaSchedules, pSchedule);
	LeaveCriticalSection(&cs_eaSchedules);

	return pSchedule;
}

static bool TestServer_CalculateNextRun(TestServerScheduleEntry *pEntry)
{
	// Increment run count, if it has limited repeats and we exceed those...
	if(pEntry->iNumRepeats && ++pEntry->iTimesRun > pEntry->iNumRepeats)
	{
		// ...then we're done running it.
		pEntry->iNextRun = 0;
		return false;
	}

	// If it has previously run, calculate the next run using the timing of the last
	if(pEntry->iNextRun)
	{
		// If it is set to repeat at an interval, then just increment by that interval
		if(pEntry->iRepeatInterval)
		{
			pEntry->iNextRun += pEntry->iRepeatInterval;
		}
		// Otherwise, run at next occurrence of specified day/time, which is next week
		else
		{
			struct tm nextRunTime;

			pEntry->iNextRun += 604800;
			timeMakeLocalTimeStructFromSecondsSince2000(pEntry->iNextRun, &nextRunTime);

			// Daylight savings fixup
			if(nextRunTime.tm_hour != pEntry->iHour)
			{
				pEntry->iNextRun -= ((pEntry->iHour + 24 - nextRunTime.tm_hour) % 24) * 3600;
			}
		}
	}
	else
	{
		struct tm nextRunTime;
		time_t nextRunTime_t;

		timeMakeLocalTimeStructFromSecondsSince2000(timeSecondsSince2000(), &nextRunTime);
		
		// Increment day and time fields so they all match the intended day/time
		nextRunTime.tm_mday += (pEntry->iDay + 7 - nextRunTime.tm_wday) % 7;
		nextRunTime.tm_hour += pEntry->iHour - nextRunTime.tm_hour;
		nextRunTime.tm_min += pEntry->iMinute - nextRunTime.tm_min;
		nextRunTime.tm_sec += pEntry->iSecond - nextRunTime.tm_sec;
		nextRunTime_t = mktime(&nextRunTime);
		
		pEntry->iNextRun = timeGetSecondsSince2000FromLocalTimeStruct(&nextRunTime);
	}

	return true;
}

AUTO_COMMAND ACMD_NAME(AddScheduleEntry);
void TestServer_AddScheduleEntry(const char *script, int day, int hr, int min, int sec, int interval, int repeats, bool important)
{
	TestServerSchedule *pSchedule;
	TestServerScheduleEntry *pEntry;
	
	EnterCriticalSection(&cs_eaSchedules);
	pSchedule = eaIndexedGetUsingString(&eaSchedules, script);

	if(!pSchedule)
	{
		pSchedule = TestServer_AddSchedule(script);
	}
	
	pEntry = StructCreate(parse_TestServerScheduleEntry);
	pEntry->iDay = day;
	pEntry->iHour = hr;
	pEntry->iMinute = min;
	pEntry->iSecond = sec;
	pEntry->iRepeatInterval = interval;
	pEntry->iNumRepeats = repeats;
	pEntry->bImportant = important;

	TestServer_CalculateNextRun(pEntry);
	eaPush(&pSchedule->ppEntries, pEntry);
	LeaveCriticalSection(&cs_eaSchedules);
}

AUTO_COMMAND ACMD_NAME(ClearSchedules);
void TestServer_ClearSchedules(void)
{
	EnterCriticalSection(&cs_eaSchedules);
	eaDestroyStruct(&eaSchedules, parse_TestServerSchedule);
	LeaveCriticalSection(&cs_eaSchedules);
}

void TestServer_ScheduleTick(void)
{
	int now = timeSecondsSince2000();

	EnterCriticalSection(&cs_eaSchedules);
	FOR_EACH_IN_EARRAY(eaSchedules, TestServerSchedule, pSchedule)
	{
		bool run = false;
		bool important = false;

		FOR_EACH_IN_EARRAY(pSchedule->ppEntries, TestServerScheduleEntry, pEntry)
		{
			assert(pEntry->iNextRun);

			if(now >= pEntry->iNextRun)
			{
				run = true;
				
				if(pEntry->bImportant)
				{
					important = pEntry->bImportant;
				}

				if(!TestServer_CalculateNextRun(pEntry))
				{
					eaRemove(&pSchedule->ppEntries, ipEntryIndex);
					StructDestroy(parse_TestServerScheduleEntry, pEntry);
				}
			}
		}
		FOR_EACH_END

		if(run)
		{
			if(important)
			{
				TestServer_CancelAllScripts();
				TestServer_CancelAllReports();
				TestServer_RunScriptNow(pSchedule->pScript);
			}
			else
			{
				TestServer_RunScript(pSchedule->pScript);
			}
		}

		if(!eaSize(&pSchedule->ppEntries))
		{
			eaRemove(&eaSchedules, ipScheduleIndex);
			StructDestroy(parse_TestServerSchedule, pSchedule);
			continue;
		}
	}
	FOR_EACH_END
	LeaveCriticalSection(&cs_eaSchedules);
}

#include "TestServerSchedule_c_ast.c"