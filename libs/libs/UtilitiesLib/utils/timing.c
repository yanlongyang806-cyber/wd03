#define TIMING_C

#include <string.h>
#include "utils.h"
#include "file.h"

#include <error.h>
#include "MemoryPool.h"
#include "strings_opt.h"
#include "memlog.h"
#include "StashTable.h"
#include "estring.h"
#include "earray.h"

#include "wininclude.h"
#include "timing.h"
#include "timing_profiler.h"
#include "timing_profiler_interface.h"
#include "alerts.h"
#include "sysutil.h"
#include "ContinuousBuilderSupport.h"
#include "StringUtil.h"
#include "UTF8.h"
#include "cmdparse.h"

#if _PS3

#include <sys/sys_time.h>
#include <sys/time_util.h>

#else
#include <stdio.h>
#include <limits.h>
#include <time.h>

#if PROFILE_PERF
#include <xtl.h>
#endif

#if !_XBOX
	// This pragma forces including winmm.lib, which is needed for the timeGetTime below.
	#pragma comment(lib, "winmm.lib")
#endif

#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););



int	timing_server_offset = 0;
int timing_debug_offset = 0;		// offset added to timeGetTime()

static FILE *spFileForPrintf = NULL;

int gbTimeStampAllPrintfs = 0;
AUTO_CMD_INT(gbTimeStampAllPrintfs, TimeStampAllPrintfs);

// Turn off most printing to the console.
static bool sbDisablePrintf = false;
AUTO_CMD_INT(sbDisablePrintf, DisablePrintf);

// If set, call this function instead of actually printing.
static VprintfFunc vprintf_func = NULL;

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void ForkPrintfsToFile(char *pFileName)
{
	printfEnterCS();

	// If we've overridden printf to do something else, ignore this command line option.
	// This lets DLLs that have their printf redirected to the main process' printf work correctly with -ForkPrintfsToFile.
	if (vprintf_func)
		return;

	mkdirtree_const(pFileName);
	spFileForPrintf = fopen(pFileName, "wb");

	printfLeaveCS();
}

// If set, serialize all printf() calls to preserve colors when multiple threads are using printf() simultaneously.
// This was added by msimpson in 2007, probably for of his physics debugging code, but I think it's a bad idea.  It causes deadlocks in various
// exceptional circumstances, and poor performance in situations that would otherwise be OK.  I think the minor cosmetic problems of concurrent
// colored printf is not worth the risk.  It's possible that this is something that we want in only in dev mode, or perhaps only for certain
//app types, but for now, I'm just going to leave it off by default.
static bool serialize_printf = false;
AUTO_CMD_INT(serialize_printf, SerializePrintf);

#if _PS3

uint32_t GetTickCount(void) {

    uint64_t x;

    SYS_TIMEBASE_GET(x);

    static uint64_t freq;
    static double to_ms;
    if(!freq) {

        freq = sys_time_get_timebase_frequency();
        to_ms = 1000.0 / (double)freq;
    }

    int32_t v = (int32_t)( (double)x * to_ms );

    return (uint32_t)v;
}

unsigned long timerCpuSpeed()
{
	return CLOCKS_PER_SEC;
}

unsigned long timerCpuTicks()
{
	return (unsigned long)clock();
}

S64 timerCpuSpeed64(void)
{
    static uint64_t freq;

    if(!freq)
        freq = sys_time_get_timebase_frequency();
    return freq;
}

S64 timerCpuTicks64(void)
{
    uint64_t x;

    SYS_TIMEBASE_GET(x);
    return x;
}

U64 timeGetCPUCyclesPerSecond(void)
{
// Get the processor speed info.
	static U64	mhz;

	if(!mhz)
	{
        mhz = 40*timerCpuSpeed64();
    }

    return mhz;
}

#else

//----------------------------------------------------------------------------------------------
//
// These have to be at the top, because these are used by timing code, so we don't want them timed.
//
// #undef EnterCriticalSection
// #undef TryEnterCriticalSection
// #undef LeaveCriticalSection
// #undef Sleep
//
//----------------------------------------------------------------------------------------------


/*
	Time functions:
	timerCpuTicks / timerCpuSpeed
		32 bit timers that are scaled to flip over about once every 24 hours
	timerCpuTicks64 / timerCpuSpeed64
		the raw result from the high speed cpu timer
	timerAlloc, timerFree, timerStart, timerElapsed, timerPause, timerUnpause, timerAdd
		float convenience functions that use the 64 bit timer internally

	Date functions:
		timeSecondsSince2000, timeGetSecondsSince2000FromString, timeMakeDateStringFromSecondsSince2000
			Should be good till 2136
*/

int shiftval;

U64 timeGetCPUCyclesPerSecond(void)
{
// Get the processor speed info.
	static U64 cyclesPerSecond;

	if(!cyclesPerSecond)
	{
#if !_XBOX
		int		result;
		HKEY	hKey;
		int		data = 0;
		int		dataSize = sizeof(int);
		result = RegOpenKeyEx (HKEY_LOCAL_MACHINE, L"Hardware\\Description\\System\\CentralProcessor\\0", 0, KEY_QUERY_VALUE, &hKey);
		if (result != ERROR_SUCCESS)
			return 0;
		result = RegQueryValueEx (hKey, L"~MHz", NULL, NULL, (LPBYTE)&data, &dataSize);
		RegCloseKey (hKey);

		cyclesPerSecond = data;
		cyclesPerSecond *= 1000 * 1000;
#else

#if 0
		// DJR The following code measures - approximately - the actual frequency of the 
		// timebase register updates, which are read using __mftb.
		// The value code produces is around 49877364, which corresponds to the Xbox 360
		// documentation for QueryPerformanceFrequency. The docs state the timer runs ~0.25%
		// slower than the 50 MHz value indicated by QueryPerformanceFrequency, or 49.875 MHz,
		// but that the actual speed varies between 49.85 and 49.90 MHz.
		{
			S64 startTicks, endTicks;
			DWORD startMsTicks = GetTickCount(), currMsTick;

			GET_CPU_TICKS_64( startTicks );
			do
			{
				currMsTick = GetTickCount();
			}
			while ( currMsTick - startMsTicks < 10000 );

			GET_CPU_TICKS_64( endTicks );
			cyclesPerSecond = ( endTicks - startTicks ) * 1000LL / ( currMsTick - startMsTicks );
			OutputDebugStringf( "%d %f\n", cyclesPerSecond, cyclesPerSecond * 0.001f );
		}
#endif
		QueryPerformanceFrequency((LARGE_INTEGER *)&cyclesPerSecond);
#endif
	}

	return cyclesPerSecond;
}

void timerGetProfilerFileName(const char* input, char* output, size_t output_size, int makeDirs)
{
	if(strncmp(input,"\\\\",2)==0 || strchr(input,':'))
	{
		strcpy_s(output, output_size, input);
	}
	else
	{
		char* prefix = ".profiler";
		U32 prefixLen = (U32)strlen(prefix);
		char buf[1000];

		fileSpecialDir("profiler", SAFESTR(buf));

		sprintf_s(SAFESTR2(output),
				"%s/%s%s",
				buf,
				input,
				strlen(input) <= prefixLen || stricmp(strchr(input, 0)/* + strlen(fname) - prefixLen*/, prefix) ? prefix : "");
	}

	if(makeDirs)
	{
		char fileDir[1000];
		strcpy(fileDir, output);
		makeDirectories(getDirectoryName(fileDir));
	}
}

bool g_force_bad_cpu;
// Forces the timing code to use the code that gets ran on bad multi-core/cpu systems whose timers drift per-core
AUTO_CMD_INT(g_force_bad_cpu, force_bad_cpu) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

S64 __fastcall timerCpuTicks64()
{
	S64 x = 0;
	static S32 isBadCPU;
	static S64 baseBadCPU;

	if(!isBadCPU){
		static DWORD tls_testtime_index=0;
		S64 *ptesttime;
		STATIC_THREAD_ALLOC(ptesttime);

		QueryPerformanceCounter((LARGE_INTEGER *)&x);

		if(*ptesttime){
			S64 diff = x - *ptesttime;
			if(diff < 0 || g_force_bad_cpu){
				baseBadCPU = *ptesttime;
				isBadCPU = 1;
			}
		}
		*ptesttime = x;
	}

	#if !_XBOX
		// NOT an "else", because isBadCPU can change in above block.
		if(isBadCPU){
			static S32 baseTimeMS;
			S32 curTimeMS = timeGetTime();

			if(!baseTimeMS){
				timeBeginPeriod(1);
				baseTimeMS = curTimeMS;
			}

			x = baseBadCPU + (S64)(curTimeMS - baseTimeMS) * timerCpuSpeed64() / 1000;
		}
	#endif

	return x;
}

S64 timerCpuSpeed64()
{
	static S64 speed;
	
	ATOMIC_INIT_BEGIN;
		QueryPerformanceFrequency((LARGE_INTEGER *)&speed);
	ATOMIC_INIT_END;
	
	return speed;
}

unsigned long timerCpuTicks()
{
	S64 x;
	U32 t;

	PERFINFO_AUTO_START_FUNC();

	if (!shiftval)
	{
		timerCpuSpeed();
	}
	x = timerCpuTicks64();
	t = (U32)((x >> shiftval) & 0xffffffff);
	if (!t) // Most code does not handle a timerCpuTicks() value of 0 gracefully
		t = 1;
	PERFINFO_AUTO_STOP();
 	return t;
}

U32 timerCpuMs(void)
{
	U32 r = timeGetTime();
	if (!r)
		r = 1;
	return r;
}

AUTO_COMMAND;
U32 timeGetTimeTestResolution()
{
	int i;
	U32 time1 = timeGetTime();
	U32 time2;
	U32 totalTime = 0;
	U32 numSamples = 0;

	for(i = 100000; i >= 0; i--)
	{
		time2 = timeGetTime();
		if(time1 != time2)
		{
			totalTime += time2 - time1;
			numSamples++;
			time1 = time2;
		}
	}

	printf("timeGetTime() avg resolution: %.1f\n", (F32)totalTime/numSamples);
	return totalTime/numSamples;
}


unsigned long timerCpuSpeed()
{
	static S32 freq = 0;

	if (!freq)
	{
		S64 mhz = timerCpuSpeed64();

		if (!shiftval)
			shiftval = log2((int)(mhz / 20000)); // U32 timer should go for at least 24 hours without flipping over.
 		freq = (S32)(mhz >> shiftval);
	}
	return freq;
}

#endif

U32 timerCpuSeconds(void)
{
	S64 x;
	U32 t;

	x = timerCpuTicks64() / timerCpuSpeed64();
	t = (U32)x & 0xffffffff;
	if (!t)
		t = 1;
	return t;
}

F32 timerSeconds64(S64 dt)
{
	return (F32)dt / (F32)timerCpuSpeed64();
}

F32 timerSeconds(U32 dt)
{
	return (F32)dt / (F32)timerCpuSpeed();
}

typedef struct
{
	S64			start;
	S64			elapsed;
	U8			paused;
	U8			in_use;
	U32			uid_mask;
	const char* fileName;
	int			fileLine;
	int			threadID;
} TimerInfo;

static TimerInfo *timerInfos = NULL;
static U32 maxTimers = 128;

#define TIMER_INDEX(x)		((x) & 0xffff)
#define TIMER_UID(x)		((x) & 0xffff0000)
#define VALID_TIMER(x)		((	TIMER_INDEX(x) < maxTimers &&				\
								TIMER_UID(x) == timerInfos[TIMER_INDEX(x)].uid_mask &&	\
								timerInfos[TIMER_INDEX(x)].in_use) ?					\
								(x = TIMER_INDEX(x)), 1 :(devassertmsgf(0, "Bad timer handle 0x%x.", x),0))

void timerSetMaxTimers(int numTimers)
{
	devassert(timerInfos == NULL);

	maxTimers = numTimers;
}

void timerInit()
{
	timerInfos = calloc(maxTimers, sizeof(TimerInfo));

	devassert(timerInfos);
}

void timerStart(U32 timer)
{
	if(!VALID_TIMER(timer)){
		return;
	}

	timerInfos[timer].elapsed = 0;
	timerInfos[timer].paused = 0;
	timerInfos[timer].start = timerCpuTicks64();
}

void timerAdd(U32 timer,F32 seconds)
{
	if(!VALID_TIMER(timer)){
		return;
	}

	timerInfos[timer].elapsed += (S64)((F32)timerCpuSpeed64() * seconds);
}

void timerPause(U32 timer)
{
	if(!VALID_TIMER(timer)){
		return;
	}

	if (timerInfos[timer].paused)
		return;

	timerInfos[timer].elapsed += timerCpuTicks64() - timerInfos[timer].start;
	timerInfos[timer].paused = 1;
}

void timerUnpause(U32 timer)
{
	if(!VALID_TIMER(timer)){
		return;
	}

	if (!timerInfos[timer].paused){
		return;
	}

	timerInfos[timer].start = timerCpuTicks64();
	timerInfos[timer].paused = 0;
}

F32 timerElapsed(U32 timer)
{
	S64		dt;
	F32		secs;

	PERFINFO_AUTO_START_FUNC();

	if(!VALID_TIMER(timer)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	dt = timerInfos[timer].elapsed;
	if (!timerInfos[timer].paused)
		dt += timerCpuTicks64() - timerInfos[timer].start;
	secs = (F32)((F32)dt / (F32)timerCpuSpeed64());
	PERFINFO_AUTO_STOP();
	return secs;
}

F32 timerElapsedAndStart(U32 timer)
{
	S64 curTime;
	S64	dt;
	F32	secs;

	if(!VALID_TIMER(timer)){
		return 0;
	}

	dt = timerInfos[timer].elapsed;
	if (!timerInfos[timer].paused)
	{
		curTime = timerCpuTicks64();
		dt += curTime - timerInfos[timer].start;
		timerInfos[timer].start = curTime;
	}
	secs = (F32)((F32)dt / (F32)timerCpuSpeed64());
	return secs;
}

U32 timerAllocDbg(const char* fileName, int fileLine)
{
	static	CRITICAL_SECTION cs;
	static	U32 uid;

	ATOMIC_INIT_BEGIN;
	{
		InitializeCriticalSection(&cs);
	}
	ATOMIC_INIT_END;

	EnterCriticalSection(&cs);

	if(!timerInfos)
	{
		timerInit();
	}

	devassert(timerInfos);

	FOR_BEGIN(i, (S32)maxTimers);
	{
		if(!timerInfos[i].in_use)
		{
			uid++;

			if(!uid || uid >= 0x8000){
				uid = 1;
			}

			timerInfos[i].in_use = 1;
			LeaveCriticalSection(&cs);
			timerInfos[i].fileName = fileName;
			timerInfos[i].fileLine = fileLine;
			timerInfos[i].threadID = GetCurrentThreadId();
			timerInfos[i].uid_mask = (U32)uid << 16;
			i |= timerInfos[i].uid_mask;
			timerStart(i);
			return i;
		}
	}
	FOR_END;

	LeaveCriticalSection(&cs);

	ATOMIC_INIT_BEGIN;
	{
		assertmsg(0, "ran out of timers!");
	}
	ATOMIC_INIT_END;

	return ~0;
}

void timerFree(U32 timer)
{
	if(!VALID_TIMER(timer)){
		return;
	}

	timerInfos[timer].in_use = 0;
}

S64 timeY2KOffset()
{
	static S64 offset = 0;

	if (!offset)
	{
#if _PS3
        struct tm y2k = {};
        y2k.tm_year = 2000-1900;
        y2k.tm_mday = 1;
        offset = mktime(&y2k);
#else
		SYSTEMTIME y2k = {0};
		y2k.wYear	= 2000;
		y2k.wMonth	= 1;
		y2k.wDay	= 1;
		SystemTimeToFileTime(&y2k,(FILETIME*)&offset);
#endif
	}

	return offset;
}

// returns hours:mins:secs
char* timeMakeOffsetStringFromSeconds_s(char *offsetstr, size_t offsetstr_size, U32 seconds)
{
	U32 secs = seconds % 60;
	U32 mins = (seconds /= 60) % 60;
	U32 hours = (seconds /= 60);
	if (hours)
		sprintf_s(SAFESTR2(offsetstr), "%d:%02d:%02d", hours, mins, secs);
	else
		sprintf_s(SAFESTR2(offsetstr), "%d:%02d", mins, secs);
	return offsetstr;
}

void timeSecondsGetHoursMinutesSeconds(S32 iSeconds, S32 pHMS[3], bool bUseFirstOnly)
{
	pHMS[0] = iSeconds / 3600;
	if (pHMS[0] > 0 && bUseFirstOnly) return;
	pHMS[1] = (iSeconds / 60) % 60;
	if (pHMS[1] > 0 && bUseFirstOnly) return;
	pHMS[2] = iSeconds % 60;
}

// internal helper functions

void timerSystemTimeFromSecondsSince2000(SYSTEMTIME *t, U32 seconds)
{
	S64			x;

	x = seconds;
	x *= WINTICKSPERSEC;
	x += timeY2KOffset();

	FileTimeToSystemTime((FILETIME*)&x, t);
}

S64 timerFileTimeFromSecondsSince2000(U32 seconds)
{
		S64			x;

	x = seconds;
	x *= WINTICKSPERSEC;
	x += timeY2KOffset();

	return x;
}

U32 timerSecondsSince2000FromFileTime(S64 iFileTime)
{
	iFileTime -= timeY2KOffset();
	iFileTime /= WINTICKSPERSEC;

	return iFileTime;
}



U32 timerSecondsSince2000FromSystemTime(SYSTEMTIME *t)
{
	S64 x;
	SystemTimeToFileTime(t, (FILETIME*)&x);

	x -= timeY2KOffset();
	x /= WINTICKSPERSEC;

	return x;
}

#if !PLATFORM_CONSOLE
U32 timeSecondsSince2000FromFileTime(FILETIME *pFileTime)
{
	S64 x = *((S64*)pFileTime);
	x -= timeY2KOffset();
	x /= WINTICKSPERSEC;

	return x;
}
#endif


U32 timerSecondsSince2000FromLocalSystemTime(SYSTEMTIME *t)
{
#if _PS3
    return timerSecondsSince2000FromSystemTime(t);
#else
	S64 x1;
	S64 x2;
	SystemTimeToFileTime(t, (FILETIME*)&x1);
	LocalFileTimeToFileTime((FILETIME*)&x1, (FILETIME*)&x2);

	x2 -= timeY2KOffset();
	x2 /= WINTICKSPERSEC;

	return x2;
#endif
}


void timerLocalSystemTimeFromSecondsSince2000(SYSTEMTIME *t, U32 seconds)
{
#if _PS3
    return timerSystemTimeFromSecondsSince2000(t, seconds);
#else
	S64			x;
	FILETIME ft;

	x = seconds;
	x *= WINTICKSPERSEC;
	x += timeY2KOffset();

	FileTimeToLocalFileTime((FILETIME*)&x,&ft);
	FileTimeToSystemTime(&ft, t);	
#endif
}

void timerDaylightLocalSystemTimeFromSecondsSince2000(SYSTEMTIME *t, U32 seconds)
{
#if _PS3
	return timerSystemTimeFromSecondsSince2000(t, seconds);
#else
	S64			x;
	SYSTEMTIME st;

	x = seconds;
	x *= WINTICKSPERSEC;
	x += timeY2KOffset();

	FileTimeToSystemTime((FILETIME*)&x, &st);
	SystemTimeToTzSpecificLocalTime(NULL, &st, t);
#endif
}

static char *timerFormatDateString(char *datestr, size_t datestr_size, SYSTEMTIME *t)
{
	static char static_datestr[20];

	if (!datestr)
	{
		datestr = static_datestr;
		datestr_size = ARRAY_SIZE_CHECKED(static_datestr);
	}

	assert(datestr_size >= 20);

	//sprintf(datestr,"%04d-%02d-%02d %02d:%02d:%02d",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond); // JE: sprintf is slow, I hates it
	STR_COMBINE_BEGIN_S(datestr, datestr_size)
	STR_COMBINE_CAT_D2((t->wYear / 100));
	STR_COMBINE_CAT_D2((t->wYear % 100));
	STR_COMBINE_CAT_C('-');
	STR_COMBINE_CAT_D2(t->wMonth);
	STR_COMBINE_CAT_C('-');
	STR_COMBINE_CAT_D2(t->wDay);
	STR_COMBINE_CAT_C(' ');
	STR_COMBINE_CAT_D2(t->wHour);
	STR_COMBINE_CAT_C(':');
	STR_COMBINE_CAT_D2(t->wMinute);
	STR_COMBINE_CAT_C(':');
	STR_COMBINE_CAT_D2(t->wSecond);
	STR_COMBINE_END(datestr);

	return datestr;
}

static char *timerFormatISO8601(char *datestr, size_t datestr_size, SYSTEMTIME *t)
{	//yyyymmddThh:mm:ss
	static char static_datestr[18];

	if (!datestr)
	{
		datestr = static_datestr;
		datestr_size = ARRAY_SIZE_CHECKED(static_datestr);
	}

	assert(datestr_size >= 18);

	STR_COMBINE_BEGIN_S(datestr, datestr_size)
	STR_COMBINE_CAT_D2((t->wYear / 100));
	STR_COMBINE_CAT_D2((t->wYear % 100));
	STR_COMBINE_CAT_D2(t->wMonth);
	STR_COMBINE_CAT_D2(t->wDay);
	STR_COMBINE_CAT_C('T');
	STR_COMBINE_CAT_D2(t->wHour);
	STR_COMBINE_CAT_C(':');
	STR_COMBINE_CAT_D2(t->wMinute);
	STR_COMBINE_CAT_C(':');
	STR_COMBINE_CAT_D2(t->wSecond);
	STR_COMBINE_END(datestr);

	return datestr;
}

static char *timerFormatDateStringFilename(char *datestr, size_t datestr_size, SYSTEMTIME *t)
{
	static char static_datestr[20];

	if (!datestr)
	{
		datestr = static_datestr;
		datestr_size = ARRAY_SIZE_CHECKED(static_datestr);
	}

	assert(datestr_size >= 20);

	//sprintf(datestr,"%04d-%02d-%02d_%02d-%02d-%02d",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond); // JE: sprintf is slow, I hates it
	STR_COMBINE_BEGIN_S(datestr, datestr_size)
	STR_COMBINE_CAT_D2((t->wYear / 100));
	STR_COMBINE_CAT_D2((t->wYear % 100));
	STR_COMBINE_CAT_C('-');
	STR_COMBINE_CAT_D2(t->wMonth);
	STR_COMBINE_CAT_C('-');
	STR_COMBINE_CAT_D2(t->wDay);
	STR_COMBINE_CAT_C('_');
	STR_COMBINE_CAT_D2(t->wHour);
	STR_COMBINE_CAT_C('-');
	STR_COMBINE_CAT_D2(t->wMinute);
	STR_COMBINE_CAT_C('-');
	STR_COMBINE_CAT_D2(t->wSecond);
	STR_COMBINE_END(datestr);

	return datestr;
}

time_t timeGetTimeFromDateString(const char *dateStr, const char *timeStr)
{
	int M, D, Y, H, m, sec=0;
	char *c;
	struct tm curtime = {0};
	char buf[11];
	char *day, *time;

	strncpy(buf, dateStr, 10);
	day = buf;
	for (c=day; *c; c++) // make an array of ints by subtracting '0' from all numerical characters
		if (strchr("0123456789", *c))
			*c-='0';
	if (day[4] == '-') { // 2007-01-23
		M=day[5]*10+day[6];
		D=day[8]*10+day[9];
		Y=day[2]*10+day[3];
	} else if (day[2] == '-') { // 01-23-2007 (Needed for Gimme to read CoH log files!)
		M=day[0]*10+day[1];
		D=day[3]*10+day[4];
		Y=day[8]*10+day[9];
	} else if (strlen(dateStr)==6)
	{
		Y=day[0]*10+day[1];
		M=day[2]*10+day[3];
		D=day[4]*10+day[5];
	} else {
		M = D = Y = 0;
	}
	curtime.tm_mon=M-1;
	curtime.tm_year=Y+100;
	curtime.tm_isdst=-1;
	curtime.tm_mday=D;
	if (timeStr) {
		strncpy(buf, timeStr, 8);
		time = buf;
		for (c=time; *c; c++) // make an array of ints by subtracting '0' from all numerical characters
			if (strchr("0123456789", *c))
				*c-='0';
		H=time[0]*10+time[1];
		m=time[3]*10+time[4];
		sec=time[6]*10+time[7];
		curtime.tm_hour=H;
		curtime.tm_min=m;
		curtime.tm_sec=sec;
	}
	return mktime(&curtime);
}

// All of the following are helper productions for timeGetSecondsSince2000FromHttpDateString().

#define PARSE_CHAR(C) do {if (**(string) != (C)) return false; ++*(string);}while(0)
#define PARSE_CHECK() do {if (!(success)) return false;}while(0)

static bool parseWkday(const char **string, struct tm *t)
{
	if (strStartsWith(*string, "Mon"))
		t->tm_wday = 1;
	else if (strStartsWith(*string, "Tue"))
		t->tm_wday = 2;
	else if (strStartsWith(*string, "Wed"))
		t->tm_wday = 3;
	else if (strStartsWith(*string, "Thu"))
		t->tm_wday = 4;
	else if (strStartsWith(*string, "Fri"))
		t->tm_wday = 5;
	else if (strStartsWith(*string, "Sat"))
		t->tm_wday = 6;
	else if (strStartsWith(*string, "Sun"))
		t->tm_wday = 0;
	else
		return false;
	*string += 3;
	return true;
}

static bool parseWeekday(const char **string, struct tm *t)
{
	const char monday[]= "Monday";
	const char tuesday[]= "Tuesday";
	const char wednesday[]= "Wednesday";
	const char thursday[]= "Thursday";
	const char friday[]= "Friday";
	const char saturday[]= "Saturday";
	const char sunday[]= "Sunday";

	if (strStartsWith(*string, monday))
	{
		t->tm_wday = 1;
		*string += sizeof(monday) - 1;
	}
	else if (strStartsWith(*string, tuesday))
	{
		t->tm_wday = 2;
		*string += sizeof(tuesday) - 1;
	}
	else if (strStartsWith(*string, wednesday))
	{
		t->tm_wday = 3;
		*string += sizeof(wednesday) - 1;
	}
	else if (strStartsWith(*string, thursday))
	{
		t->tm_wday = 4;
		*string += sizeof(thursday) - 1;
	}
	else if (strStartsWith(*string, friday))
	{
		t->tm_wday = 5;
		*string += sizeof(friday) - 1;
	}
	else if (strStartsWith(*string, saturday))
	{
		t->tm_wday = 6;
		*string += sizeof(saturday) - 1;
	}
	else if (strStartsWith(*string, sunday))
	{
		t->tm_wday = 0;
		*string += sizeof(sunday) - 1;
	}
	else
		return false;

	return true;
}

static bool parseMonth(const char **string, struct tm *t)
{
	if (strStartsWith(*string, "Jan"))
		t->tm_mon = 0;
	else if (strStartsWith(*string, "Feb"))
		t->tm_mon = 1;
	else if (strStartsWith(*string, "Mar"))
		t->tm_mon = 2;
	else if (strStartsWith(*string, "Apr"))
		t->tm_mon = 3;
	else if (strStartsWith(*string, "May"))
		t->tm_mon = 4;
	else if (strStartsWith(*string, "Jun"))
		t->tm_mon = 5;
	else if (strStartsWith(*string, "Jul"))
		t->tm_mon = 6;
	else if (strStartsWith(*string, "Aug"))
		t->tm_mon = 7;
	else if (strStartsWith(*string, "Sep"))
		t->tm_mon = 8;
	else if (strStartsWith(*string, "Oct"))
		t->tm_mon = 9;
	else if (strStartsWith(*string, "Nov"))
		t->tm_mon = 10;
	else if (strStartsWith(*string, "Dec"))
		t->tm_mon = 11;
	else
		return false;
	*string += 3;
	return true;
}

static bool parseNDigit(const char **string, int *number, size_t n)
{
	size_t i;
	*number = 0;
	for (i = 0; i != n; ++i)
	{
		if (!isdigit((*string)[0]))
			return false;
		*number *= 10;
		*number += (*string)[0] - '0';
		++*string;
	}
	return true;
}

static bool parseTimezone(const char **string, int *offset)
{
	// These timezones are from RFC 822, excluding the military zones due to RFC 1123.
	// Added some reasonable accomodation to the TZ rules:
	//   -Added "UTC"
	//   -Tries to parse non-four digit offsets
	if (**string == '+' || **string == '-')
	{
		int sign = **string == '+' ? 1 : -1;
		int hour;
		int minute;
		bool success = parseNDigit(string, &hour, 2);
		if (!success)
			success = parseNDigit(string, &hour, 1);
		PARSE_CHECK();
		if (hour > 12 || hour < 12)
			return false;
		success = parseNDigit(string, &minute, 2);
		if (success && minute < 0 || minute > 59)
			return false;
		else if (!success)
			minute = 0;
		*offset = hour * 60 + minute;
		return true;
	}
	else if (strStartsWith(*string, "GMT"))
		*offset = 0;
	else if (strStartsWith(*string, "UTC"))
		*offset = 0;
	else if (strStartsWith(*string, "UT"))
	{
		*string += 2;
		*offset = 0;
		return true;
	}
	else if (strStartsWith(*string, "EST"))
		*offset = -5*60;
	else if (strStartsWith(*string, "EDT"))
		*offset = -4*60;
	else if (strStartsWith(*string, "CST"))
		*offset = -6*60;
	else if (strStartsWith(*string, "CDT"))
		*offset = -5*60;
	else if (strStartsWith(*string, "MST"))
		*offset = -7*60;
	else if (strStartsWith(*string, "MDT"))
		*offset = -6*60;
	else if (strStartsWith(*string, "PST"))
		*offset = -8*60;
	else if (strStartsWith(*string, "PDT"))
		*offset = -7*60;
	else
		return false;
	*string += 3;
	return true;
}

static bool parseDate1(const char **string, struct tm *t)
{
	bool success;

	success = parseNDigit(string, &t->tm_mday, 2);
	PARSE_CHECK();
	if (t->tm_mday < 1 || t->tm_mday > 31)
		return false;

	PARSE_CHAR(' ');

	success = parseMonth(string, t);
	PARSE_CHECK();

	PARSE_CHAR(' ');

	success = parseNDigit(string, &t->tm_year, 4);
	t->tm_year -= 1900;
	if (t->tm_year < 0)
		return false;
	PARSE_CHECK();

	return true;
}

static bool parseDate2(const char **string, struct tm *t)
{
	bool success;

	success = parseNDigit(string, &t->tm_mday, 2);
	PARSE_CHECK();
	if (t->tm_mday < 1 || t->tm_mday > 31)
		return false;

	PARSE_CHAR('-');

	success = parseMonth(string, t);
	PARSE_CHECK();

	PARSE_CHAR('-');

	success = parseNDigit(string, &t->tm_year, 2);
	PARSE_CHECK();
	if (t->tm_year < 0)
		return false;

	return true;
}

static bool parseDate3(const char **string, struct tm *t)
{
	bool success;

	success = parseMonth(string, t);
	PARSE_CHECK();

	PARSE_CHAR(' ');

	if (**string == ' ')
	{
		PARSE_CHAR(' ');
		success = parseNDigit(string, &t->tm_mday, 1);
	}
	else
		success = parseNDigit(string, &t->tm_mday, 2);
	PARSE_CHECK();

	return true;
}

static bool parseTime(const char **string, struct tm *t)
{
	bool success;

	success = parseNDigit(string, &t->tm_hour, 2);
	PARSE_CHECK();
	if (t->tm_hour < 0 || t->tm_hour > 23)
		return false;

	PARSE_CHAR(':');

	success = parseNDigit(string, &t->tm_min, 2);
	PARSE_CHECK();
	if (t->tm_min < 0 || t->tm_min > 59)
		return false;
	
	PARSE_CHECK();

	PARSE_CHAR(':');

	success = parseNDigit(string, &t->tm_sec, 2);
	PARSE_CHECK();
	if (t->tm_sec < 0 || t->tm_sec > 60)
		return false;

	return true;
}

static bool parseRfc1123(struct tm *t, int *offset, const char *httpdate)
{
	bool success;
	const char **string = &httpdate;

	success = parseWkday(string, t);
	PARSE_CHECK();

	PARSE_CHAR(',');

	PARSE_CHAR(' ');

	success = parseDate1(string, t);
	PARSE_CHECK();

	PARSE_CHAR(' ');

	success = parseTime(string, t);
	PARSE_CHECK();

	PARSE_CHAR(' ');

	success = parseTimezone(string, offset);
	PARSE_CHECK();

	if (**string)
		return false;

	return true;
}

static bool parseRfc850(struct tm *t, int *offset, const char *httpdate)
{
	bool success;
	const char **string = &httpdate;
	time_t now_time;
	struct tm now;
	int diff;
	errno_t err;

	success = parseWeekday(string, t);
	PARSE_CHECK();

	PARSE_CHAR(',');

	PARSE_CHAR(' ');

	success = parseDate2(string, t);
	PARSE_CHECK();

	PARSE_CHAR(' ');

	success = parseTime(string, t);
	PARSE_CHECK();

	PARSE_CHAR(' ');

	success = parseTimezone(string, offset);
	PARSE_CHECK();

	if (**string)
		return false;

	// Adjust two-digit year according to RFC 2616 19.3/5.1, with slight modification.
	now_time = time(NULL);
	err = gmtime_s(&now, &now_time);
	if (!err)
	{
		diff = t->tm_year - now.tm_year;
		t->tm_year += -(diff/100)*100;
	}

	return true;
}

static bool parseIso9899(struct tm *t, int *offset, const char *httpdate)
{
	bool success;
	const char **string = &httpdate;

	success = parseWkday(string, t);
	PARSE_CHECK();

	PARSE_CHAR(' ');

	success = parseDate3(string, t);
	PARSE_CHECK();

	PARSE_CHAR(' ');

	success = parseTime(string, t);
	PARSE_CHECK();

	PARSE_CHAR(' ');

	success = parseNDigit(string, &t->tm_year, 4);
	t->tm_year -= 1900;
	if (t->tm_year < 0)
		return false;
	PARSE_CHECK();

	if (**string)
		return false;

	return true;
}

#undef PARSE_CHAR
#undef PARSE_CHECK

// Parse an HTTP date (RFC 2616).
// This parsing is based on the grammar provided in RFC 2616 3.3.1, guided by the robustness and tolerance
// recommendations in the first note here and in 19.3.  Each respective standard was consulted for additional
// details, particularly for time zone handling.  Despite this accomodation, we reject stuff that's just flat
// out not OK.
U32 timeGetSecondsSince2000FromHttpDateString(const char *httpdate)
{
	struct tm t;
	int offset = 0;
	bool success;
	U32 ss2000;

	// Try to parse the date in each date format.
	success = parseRfc1123(&t, &offset, httpdate);
	if (!success)
		success = parseRfc850(&t, &offset, httpdate);
	if (!success)
		success = parseIso9899(&t, &offset, httpdate);

	// If none of them are recognized, return 0.
	if (!success)
		return 0;

	// Convert to SS2000.
	t.tm_isdst = false;
	t.tm_yday = -1;
	ss2000 = timeGetSecondsSince2000FromTimeStruct(&t);
	ss2000 += offset * 60;
	return ss2000;
}

static char *timerFormatDateNoTimeString(char *datestr, size_t datestr_size, SYSTEMTIME *t)
{
	static char static_datestr[20];

	if (!datestr)
	{
		datestr = static_datestr;
		datestr_size = ARRAY_SIZE_CHECKED(static_datestr);
	}

	assert(datestr_size >= 20);

	//sprintf(datestr,"%04d-%02d-%02d",t.wYear,t.wMonth,t.wDay); // JE: sprintf is slow, I hates it
	STR_COMBINE_BEGIN_S(datestr, datestr_size)
	STR_COMBINE_CAT_D2((t->wYear / 100));
	STR_COMBINE_CAT_D2((t->wYear % 100));
	STR_COMBINE_CAT_C('-');
	STR_COMBINE_CAT_D2(t->wMonth);
	STR_COMBINE_CAT_C('-');
	STR_COMBINE_CAT_D2(t->wDay);
	STR_COMBINE_END(datestr);

	return datestr;
}

static char *timerFormatDateStringGimme(char *datestr, size_t datestr_size, SYSTEMTIME *t)
{
	static char static_datestr[20];

	if (!datestr)
	{
		datestr = static_datestr;
		datestr_size = ARRAY_SIZE_CHECKED(static_datestr);
	}

	assert(datestr_size >= 20);

	//sprintf(datestr,"%02d:%02d:%02d",t.wHour,t.wMinute,t.wSecond); // JE: sprintf is slow, I hates it
	STR_COMBINE_BEGIN_S(datestr, datestr_size)
	STR_COMBINE_CAT_D2(t->wMonth);
	STR_COMBINE_CAT_D2(t->wDay);
	STR_COMBINE_CAT_D2(t->wYear-2000);	// TODO: Y3K!
	STR_COMBINE_CAT_D2(t->wHour);
	STR_COMBINE_CAT_C(':');
	STR_COMBINE_CAT_D2(t->wMinute);
	STR_COMBINE_CAT_C(':');
	STR_COMBINE_CAT_D2(t->wSecond);
	STR_COMBINE_END(datestr);

	return datestr;
}

static char *timerFormatTimeStringEx(char *datestr, size_t datestr_size, SYSTEMTIME *t, 
									 bool bShowSeconds, bool b24Hour )
{
	static char static_datestr[20];

	if (!datestr)
	{
		datestr = static_datestr;
		datestr_size = ARRAY_SIZE_CHECKED(static_datestr);
	}

	assert(datestr_size >= 20);

	STR_COMBINE_BEGIN_S(datestr, datestr_size)
	if ( b24Hour || t->wHour <= 12 )
	{
		STR_COMBINE_CAT_D2(t->wHour);
	}
	else
	{
		STR_COMBINE_CAT_D2(t->wHour-12);
	}
	STR_COMBINE_CAT_C(':');
	STR_COMBINE_CAT_D2(t->wMinute);
	if ( bShowSeconds )
	{
		STR_COMBINE_CAT_C(':');
		STR_COMBINE_CAT_D2(t->wSecond);
	}
	STR_COMBINE_END(datestr);

	return datestr;
}

static char *timerFormatTimeString(char *datestr, size_t datestr_size, SYSTEMTIME *t)
{
	static char static_datestr[20];

	if (!datestr)
	{
		datestr = static_datestr;
		datestr_size = ARRAY_SIZE_CHECKED(static_datestr);
	}

	assert(datestr_size >= 20);

	//sprintf(datestr,"%02d:%02d:%02d",t.wHour,t.wMinute,t.wSecond); // JE: sprintf is slow, I hates it
	STR_COMBINE_BEGIN_S(datestr, datestr_size)
	STR_COMBINE_CAT_D2(t->wHour);
	STR_COMBINE_CAT_C(':');
	STR_COMBINE_CAT_D2(t->wMinute);
	STR_COMBINE_CAT_C(':');
	STR_COMBINE_CAT_D2(t->wSecond);
	STR_COMBINE_END(datestr);

	return datestr;
}

static char *timerFormatLogDateString(char *datestr, size_t datestr_size, SYSTEMTIME *t)
{
	static char static_datestr[20];

	if (!datestr)
	{
		datestr = static_datestr;
		datestr_size = ARRAY_SIZE_CHECKED(static_datestr);
	}

	assert(datestr_size >= 20);

	//sprintf(datestr,"%02d%02d%02d %02d:%02d:%02d", (t.wYear % 100) ,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);	 // JE: sprintf is slow, I hates it
	STR_COMBINE_BEGIN_S(datestr, datestr_size)
	STR_COMBINE_CAT_D2((t->wYear % 100));
	STR_COMBINE_CAT_D2(t->wMonth);
	STR_COMBINE_CAT_D2(t->wDay);
	STR_COMBINE_CAT_C(' ');
	STR_COMBINE_CAT_D2(t->wHour);
	STR_COMBINE_CAT_C(':');
	STR_COMBINE_CAT_D2(t->wMinute);
	STR_COMBINE_CAT_C(':');
	STR_COMBINE_CAT_D2(t->wSecond);
	STR_COMBINE_END(datestr);

	return datestr;
}

U32 timeGetSecondsSince2000FromLogDateString(const char *pStr)
{
	SYSTEMTIME t = {0};

	if (!(isdigit(pStr[0]) && isdigit(pStr[1]) && isdigit(pStr[2]) && isdigit(pStr[3]) && isdigit(pStr[4]) && isdigit(pStr[5]) && 
		isdigit(pStr[7]) && isdigit(pStr[8]) && isdigit(pStr[10]) && isdigit(pStr[11]) && isdigit(pStr[13]) && isdigit(pStr[14])))
	{
		return 0;
	}

	if (pStr[6] != ' ' || pStr[9] != ':' || pStr[12] != ':')
	{
		return 0;
	}

	t.wYear = 2000 + (pStr[0] - '0') * 10 + pStr[1] - '0';
	t.wMonth = (pStr[2] - '0') * 10 + pStr[3] - '0';
	t.wDay = (pStr[4] - '0') * 10 + pStr[5] - '0';

	t.wHour = (pStr[7] - '0') * 10 + pStr[8] - '0';
	t.wMinute = (pStr[10] - '0') * 10 + pStr[11] - '0';
	t.wSecond = (pStr[13] - '0') * 10 + pStr[14] - '0';

	return timerSecondsSince2000FromSystemTime(&t);
}




static char *timerFormatRFC822String(char *datestr, size_t datestr_size, SYSTEMTIME *t)
{
	static char static_datestr[30];

	if (!datestr)
	{
		datestr = static_datestr;
		datestr_size = ARRAY_SIZE_CHECKED(static_datestr);
	}

	assert(datestr_size >= 30);

	STR_COMBINE_BEGIN_S(datestr, datestr_size)
	switch(t->wDayOfWeek) {
		xcase 0:	STR_COMBINE_CAT("Sun, ");
		xcase 1:	STR_COMBINE_CAT("Mon, ");
		xcase 2:	STR_COMBINE_CAT("Tue, ");
		xcase 3:	STR_COMBINE_CAT("Wed, ");
		xcase 4:	STR_COMBINE_CAT("Thu, ");
		xcase 5:	STR_COMBINE_CAT("Fri, ");
		xcase 6:	STR_COMBINE_CAT("Sat, ");
	}
	STR_COMBINE_CAT_D2(t->wDay);
	switch(t->wMonth) {
		xcase 1:	STR_COMBINE_CAT(" Jan ");
		xcase 2:	STR_COMBINE_CAT(" Feb ");
		xcase 3:	STR_COMBINE_CAT(" Mar ");
		xcase 4:	STR_COMBINE_CAT(" Apr ");
		xcase 5:	STR_COMBINE_CAT(" May ");
		xcase 6:	STR_COMBINE_CAT(" Jun ");
		xcase 7:	STR_COMBINE_CAT(" Jul ");
		xcase 8:	STR_COMBINE_CAT(" Aug ");
		xcase 9:	STR_COMBINE_CAT(" Sep ");
		xcase 10:	STR_COMBINE_CAT(" Oct ");
		xcase 11:	STR_COMBINE_CAT(" Nov ");
		xcase 12:	STR_COMBINE_CAT(" Dec ");
	}
	STR_COMBINE_CAT_D(t->wYear);
	STR_COMBINE_CAT_C(' ');
	STR_COMBINE_CAT_D2(t->wHour);
	STR_COMBINE_CAT_C(':');
	STR_COMBINE_CAT_D2(t->wMinute);
	STR_COMBINE_CAT_C(':');
	STR_COMBINE_CAT_D2(t->wSecond);
	STR_COMBINE_CAT(" GMT");
	STR_COMBINE_END(datestr);

	return datestr;
}

void GetTziPac(TIME_ZONE_INFORMATION* tziPAC)
{
	// Used for PAC conversions.
	if (tziPAC==NULL)
	{
		return;
	}
	
	// Copied from GetTimeZoneInformation when in PST
	ZeroMemory(tziPAC, sizeof(*tziPAC));
	tziPAC->Bias = 480;
	tziPAC->StandardDate.wMonth = 11;
	tziPAC->StandardDate.wDayOfWeek = 0;
	tziPAC->StandardDate.wDay = 1;
	tziPAC->StandardDate.wHour = 2;

	tziPAC->DaylightDate.wMonth = 3;
	tziPAC->DaylightDate.wDayOfWeek = 0;
	tziPAC->DaylightDate.wDay = 2;
	tziPAC->DaylightDate.wHour = 2;
	tziPAC->DaylightBias = -60;
}



// public wrappers

char *timeMakeDateStringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds)
{
	SYSTEMTIME t;
	timerSystemTimeFromSecondsSince2000(&t,seconds);
	return timerFormatDateString(datestr,datestr_size,&t);	
}

char *timeMakePACDateStringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds)
{
	SYSTEMTIME t_utc = {0};
	SYSTEMTIME t_pac = {0};
	TIME_ZONE_INFORMATION tziPAC;
	timerSystemTimeFromSecondsSince2000(&t_utc,seconds);

	GetTziPac(&tziPAC);
	SystemTimeToTzSpecificLocalTime(&tziPAC, &t_utc, &t_pac);	
	return timerFormatDateString(datestr,datestr_size,&t_pac);	
}

char *timeMakeFilenameDateStringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds)
{
	SYSTEMTIME t;
	timerSystemTimeFromSecondsSince2000(&t,seconds);
	return timerFormatDateStringFilename(datestr,datestr_size,&t);	
}

char *timeMakeFilenameLocalDateStringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds)
{
	SYSTEMTIME t;
	timerLocalSystemTimeFromSecondsSince2000(&t,seconds);
	return timerFormatDateStringFilename(datestr,datestr_size,&t);	
}

char *timeMakeDateNoTimeStringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds)
{
	SYSTEMTIME t;
	timerSystemTimeFromSecondsSince2000(&t,seconds);
	return timerFormatDateNoTimeString(datestr,datestr_size,&t);
}

char *timeMakeTimeStringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds)
{
	SYSTEMTIME t;
	timerSystemTimeFromSecondsSince2000(&t,seconds);
	return timerFormatTimeString(datestr,datestr_size,&t);
}

char *timeMakeRFC822StringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds)
{
	SYSTEMTIME t;
	timerSystemTimeFromSecondsSince2000(&t,seconds);
	return timerFormatRFC822String(datestr,datestr_size,&t);
};

// local versions

char *timeMakeLocalDateStringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds)
{
	SYSTEMTIME t;
	timerLocalSystemTimeFromSecondsSince2000(&t,seconds);
	return timerFormatDateString(datestr,datestr_size,&t);
}

char *timeMakeLocalIso8601StringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds)
{
	SYSTEMTIME t;
	if (seconds) {
		timerLocalSystemTimeFromSecondsSince2000(&t,seconds);
		return timerFormatISO8601(datestr,datestr_size,&t);
	}
	else
	{
		assert(datestr_size >= 18);
		sprintf_s(SAFESTR2(datestr), "00000000T00:00:00");
		return datestr;
	}
}

char *timeMakeLocalDateNoTimeStringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds)
{
	SYSTEMTIME t;
	timerLocalSystemTimeFromSecondsSince2000(&t,seconds);
	return timerFormatDateNoTimeString(datestr,datestr_size,&t);
}

char *timeMakeLocalTimeStringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds)
{
	SYSTEMTIME t;
	timerLocalSystemTimeFromSecondsSince2000(&t,seconds);
	return timerFormatTimeString(datestr,datestr_size,&t);
}

// current time versions

char *timeMakeLocalDateString_s(char *datestr, size_t datestr_size)
{
	SYSTEMTIME t;
	GetLocalTime(&t);
	return timerFormatDateString(datestr,datestr_size,&t);
}

char *timeMakeLocalDateStringGimme_s(char *datestr, size_t datestr_size)
{
	SYSTEMTIME t;
	GetLocalTime(&t);
	return timerFormatDateStringGimme(datestr, datestr_size, &t);
}

char *timeMakeLocalDateNoTimeString_s(char *datestr, size_t datestr_size)
{
	SYSTEMTIME t;
	GetLocalTime(&t);
	return timerFormatDateNoTimeString(datestr,datestr_size,&t);
}

char *timeMakeLocalTimeString_s(char *datestr, size_t datestr_size)
{
	SYSTEMTIME t;
	GetLocalTime(&t);
	return timerFormatTimeString(datestr,datestr_size,&t);
}

char *timeMakeLocalTimeStringEx_s(char *datestr, size_t datestr_size, bool bShowSeconds, bool b24Hour)
{
	SYSTEMTIME t;
	GetLocalTime(&t);
	return timerFormatTimeStringEx(datestr,datestr_size,&t,bShowSeconds,b24Hour);
}

// same as timeMakeDateStringFromSecondsSince2000() but uses format "YYMMDD HH:MM:SS"
char *timeMakeLogDateStringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds)
{
	SYSTEMTIME t;
	timerSystemTimeFromSecondsSince2000(&t,seconds);
	return timerFormatLogDateString(datestr,datestr_size,&t);
}

char *timeMakeLogDateString_s(char *datestr, size_t datestr_size)
{
	SYSTEMTIME t;
	GetSystemTime(&t);
	return timerFormatLogDateString(datestr,datestr_size,&t);
}

__time32_t timeMakeLocalTimeFromSecondsSince2000(U32 seconds)
{
	struct tm time;
	SYSTEMTIME	t;

	timerLocalSystemTimeFromSecondsSince2000(&t,seconds);

	time.tm_hour = t.wHour;
	time.tm_mday = t.wDay;
	time.tm_min = t.wMinute;
	time.tm_mon = t.wMonth - 1;
	time.tm_sec = t.wSecond;
	time.tm_year = t.wYear - 1900;
	time.tm_isdst = -1;
	return _mktime32(&time);
}

__time32_t timeMakeTimeFromSecondsSince2000(U32 seconds)
{
	struct tm time;
	SYSTEMTIME	t;

	timerSystemTimeFromSecondsSince2000(&t,seconds);

	time.tm_hour = t.wHour;
	time.tm_mday = t.wDay;
	time.tm_min = t.wMinute;
	time.tm_mon = t.wMonth - 1;
	time.tm_sec = t.wSecond;
	time.tm_year = t.wYear - 1900;
	time.tm_isdst = -1;
	return _mktime32(&time);
}


// MAK - I need the day of the week filled out in my time struct.
//   source: http://www.tondering.dk/claus/cal/node3.html
//   this returns 0 for sunday through 6 for saturday, as struct tm needs
int dayOfWeek(int year, int month, int day)
{
	// note: integer division is actually intended in all the following
	int a = (14 - month) / 12;
	int y = year - a;
	int m = month + 12*a - 2;
	return (day + y + y/4 - y/100 + y/400 + 31*m/12) % 7;
}

int timeDaysInMonth(int month, int year)
{
	static int days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

	if (month == 1)
		return days[month] + ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0);
	else
		return days[month];

}

__time32_t timeMakeLocalTimeStructFromSecondsSince2000(U32 seconds, struct tm *ptime)
{
	SYSTEMTIME	t;

	timerLocalSystemTimeFromSecondsSince2000(&t,seconds);

	ptime->tm_hour = t.wHour;
	ptime->tm_mday = t.wDay;
	ptime->tm_min = t.wMinute;
	ptime->tm_mon = t.wMonth - 1;
	ptime->tm_sec = t.wSecond;
	ptime->tm_year = t.wYear - 1900;
	ptime->tm_isdst = -1;
	ptime->tm_wday = dayOfWeek(t.wYear, t.wMonth, t.wDay);

	return _mktime32(ptime);
}

__time32_t timeMakeDaylightLocalTimeStructFromSecondsSince2000(U32 seconds, struct tm *ptime)
{
	SYSTEMTIME	t;

	timerDaylightLocalSystemTimeFromSecondsSince2000(&t,seconds);

	ptime->tm_hour = t.wHour;
	ptime->tm_mday = t.wDay;
	ptime->tm_min = t.wMinute;
	ptime->tm_mon = t.wMonth - 1;
	ptime->tm_sec = t.wSecond;
	ptime->tm_year = t.wYear - 1900;
	ptime->tm_isdst = -1;
	ptime->tm_wday = dayOfWeek(t.wYear, t.wMonth, t.wDay);

	return _mktime32(ptime);
}

__time32_t timeMakeTimeStructFromSecondsSince2000(U32 seconds, struct tm *ptime)
{
	SYSTEMTIME	t;

	timerSystemTimeFromSecondsSince2000(&t,seconds);

	ptime->tm_hour = t.wHour;
	ptime->tm_mday = t.wDay;
	ptime->tm_min = t.wMinute;
	ptime->tm_mon = t.wMonth - 1;
	ptime->tm_sec = t.wSecond;
	ptime->tm_year = t.wYear - 1900;
	ptime->tm_isdst = -1;
	ptime->tm_wday = dayOfWeek(t.wYear, t.wMonth, t.wDay);

	return _mktime32(ptime);
}

U32 timeGetSecondsSince2000FromLocalTimeStruct(struct tm *ptime)
{
	SYSTEMTIME	t = {0};
	FILETIME	local;
	S64			x,y2koffset = timeY2KOffset();
	U32			seconds;
	bool bResult;

	t.wYear   = ptime->tm_year + 1900;
	t.wMonth  = ptime->tm_mon + 1;
	t.wDay    = ptime->tm_mday;
	t.wHour   = ptime->tm_hour;
	t.wMinute = ptime->tm_min;
	t.wSecond = ptime->tm_sec;

	bResult = SystemTimeToFileTime(&t,&local);

#if !PLATFORM_CONSOLE
	if (!bResult)
	{
		char *pErrorBuf = NULL;

		FormatMessage_UTF8(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), 0, &pErrorBuf, NULL);
		Errorf("SystemTimeToFileTime failure: %s", pErrorBuf);
		estrDestroy(&pErrorBuf);

		return 0;
	}
#endif

	LocalFileTimeToFileTime(&local,(FILETIME *)&x);

	if (x < y2koffset)
	{
		// If the time is negative, return 0 instead of large positive time
		return 0;
	}

	seconds = (U32)((x  - y2koffset) / WINTICKSPERSEC);

	return seconds;
}

U32 timeGetSecondsSince2000FromTimeStruct(struct tm *ptime)
{
	SYSTEMTIME	t = {0};
	S64			x,y2koffset = timeY2KOffset();
	U32			seconds;

	t.wYear   = ptime->tm_year + 1900;
	t.wMonth  = ptime->tm_mon + 1;
	t.wDay    = ptime->tm_mday;
	t.wHour   = ptime->tm_hour;
	t.wMinute = ptime->tm_min;
	t.wSecond = ptime->tm_sec;

	SystemTimeToFileTime( &t, (FILETIME *)&x );

	if (x < y2koffset)
	{
		// If the time is negative, return 0 instead of large positive time
		return 0;
	}

	seconds = (U32)((x  - y2koffset) / WINTICKSPERSEC);

	return seconds;
}

__time32_t timeClampTimeToMinute(__time32_t t)
{
	struct tm timeinfo = {0};
	_localtime32_s(&timeinfo, &t);
	timeinfo.tm_sec = 0;
	return _mktime32(&timeinfo);
}

__time32_t timeClampTimeToHour(__time32_t t)
{
	struct tm timeinfo = {0};
	_localtime32_s(&timeinfo, &t);
	timeinfo.tm_min = timeinfo.tm_sec = 0;
	return _mktime32(&timeinfo);
}

__time32_t timeClampTimeToDay(__time32_t t)
{
	struct tm timeinfo = {0};
	_localtime32_s(&timeinfo, &t);
	timeinfo.tm_hour = timeinfo.tm_min = timeinfo.tm_sec = 0;
	return _mktime32(&timeinfo);
}

U32 timeClampSecondsSince2000ToHour(U32 seconds, int rounded)
{
	struct tm tms;
	if (rounded)
		seconds += 29;	// totally arbritrary, but should help if we're getting any lag jitter
	timeMakeTimeStructFromSecondsSince2000(seconds, &tms);
	tms.tm_min = tms.tm_sec = tms.tm_isdst = 0;
	return timeGetSecondsSince2000FromTimeStruct(&tms);
}

U32 timeClampSecondsSince2000ToMinutes(U32 seconds, U32 minutes)
{
	struct tm tms;
	timeMakeTimeStructFromSecondsSince2000(seconds, &tms);
	tms.tm_sec = tms.tm_isdst = 0;
	tms.tm_min -= tms.tm_min % minutes;
	return timeGetSecondsSince2000FromTimeStruct(&tms);
}

//parses a time of the format "Mon Apr 30 16:19:05 2007"
U32 timeGetSecondsSince2000FromSystemStyleString(char *s)
{
	SYSTEMTIME	t = {0};
	S64			x, y2koffset = timeY2KOffset();
	U32			seconds;
	int			len = (int)strlen(s);

	if (len < 24)
	{
		return 0;
	}

	t.wYear		= (WORD)atoi(s+20);
	if (t.wYear <= 2000)
	{
		return 0;
	}
	
	t.wDay		= (WORD)atoi(s+8);
	t.wHour		= (WORD)atoi(s+11);
	t.wMinute	= (WORD)atoi(s+14);
	t.wSecond	= (WORD)atoi(s+17);

	if (strncmp(s + 4, "Jan", 3) == 0)
	{
		t.wMonth = 1;
	}
	else if (strncmp(s + 4, "Feb", 3) == 0)
	{
		t.wMonth = 2;
	}
	else if (strncmp(s + 4, "Mar", 3) == 0)
	{
		t.wMonth = 3;
	}
	else if (strncmp(s + 4, "Apr", 3) == 0)
	{
		t.wMonth = 4;
	}
	else if (strncmp(s + 4, "May", 3) == 0)
	{
		t.wMonth = 5;
	}
	else if (strncmp(s + 4, "Jun", 3) == 0)
	{
		t.wMonth = 6;
	}
	else if (strncmp(s + 4, "Jul", 3) == 0)
	{
		t.wMonth = 7;
	}
	else if (strncmp(s + 4, "Aug", 3) == 0)
	{
		t.wMonth = 8;
	}
	else if (strncmp(s + 4, "Sep", 3) == 0)
	{
		t.wMonth = 9;
	}
	else if (strncmp(s + 4, "Oct", 3) == 0)
	{
		t.wMonth = 10;
	}
	else if (strncmp(s + 4, "Nov", 3) == 0)
	{
		t.wMonth = 11;
	}
	else if (strncmp(s + 4, "Dec", 3) == 0)
	{
		t.wMonth = 12;
	}
	else
	{
		return 0;
	}


	SystemTimeToFileTime(&t,(FILETIME *)&x);

	if (x < y2koffset)
	{
		// If the time is negative, return 0 instead of large positive time
		return 0;
	}

	seconds = (U32)((x  - y2koffset) / WINTICKSPERSEC);
	return seconds;
}

// Specific and local way to do various date formats. If this expands
//  any more than the 3 conversions it started with, or needs to be
//  used elsewhere, it should be refactored.
U32 timeGetSecondsSince2000FromGenericString(const char* sDateTimeStr)
{
	if (sDateTimeStr)
	{
		if (isdigit(sDateTimeStr[0]))
		{
			return(timeGetSecondsSince2000FromDateString(sDateTimeStr));
		}
		else if (strnicmp(sDateTimeStr, "utc", 3) == 0)
		{
			return(timeGetSecondsSince2000FromDateString(&(sDateTimeStr[3])));
		}
		else if (strnicmp(sDateTimeStr, "pac", 3) == 0)
		{
			return (timeGetSecondsSince2000FromPACDateString(&(sDateTimeStr[3])));
		}
	}
	return(0);
}

U32 timeGetSecondsSince2000FromDateString(const char *s)
{
	SYSTEMTIME	t = {0};
	S64			x, y2koffset = timeY2KOffset();
	U32			seconds;
	int			len = (int)strlen(s);

	// Canonical string is in this form:
	// "%04d-%02d-%02d %02d:%02d:%02d" ,t->year,t->month,t->day,t->hour,t->minute,t->second

	if(len>0)
		t.wYear		= (WORD)atoi(s+0);
	if(len>5)
		t.wMonth	= (WORD)atoi(s+5);
	if(len>8)
		t.wDay		= (WORD)atoi(s+8);
	if(len>11)
		t.wHour		= (WORD)atoi(s+11);
	if(len>14)
		t.wMinute	= (WORD)atoi(s+14);
	if(len>17)
		t.wSecond	= (WORD)atoi(s+17);

	if (t.wYear == 0)
	{
		// Invalid format
		return 0;
	}

	SystemTimeToFileTime(&t,(FILETIME *)&x);

	if (x < y2koffset)
	{
		// If the time is negative, return 0 instead of large positive time
		return 0;
	}

	seconds = (U32)((x  - y2koffset) / WINTICKSPERSEC);
	return seconds;
}

U32 timeGetSecondsSince2000FromLocalDateString(const char *s)
{
	SYSTEMTIME	t = {0};
	FILETIME	local;
	S64			x, y2koffset = timeY2KOffset();
	U32			seconds;
	int			len = (int)strlen(s);

	// Canonical string is in this form:
	// "%04d-%02d-%02d %02d:%02d:%02d" ,t->year,t->month,t->day,t->hour,t->minute,t->second

	if(len>0)
		t.wYear		= (WORD)atoi(s+0);
	if(len>5)
		t.wMonth	= (WORD)atoi(s+5);
	if(len>8)
		t.wDay		= (WORD)atoi(s+8);
	if(len>11)
		t.wHour		= (WORD)atoi(s+11);
	if(len>14)
		t.wMinute	= (WORD)atoi(s+14);
	if(len>17)
		t.wSecond	= (WORD)atoi(s+17);

	if (t.wYear == 0)
	{
		// Invalid format
		return 0;
	}

	SystemTimeToFileTime(&t,&local);
	LocalFileTimeToFileTime(&local,(FILETIME *)&x);

	if (x < y2koffset)
	{
		// If the time is negative, return 0 instead of large positive time
		return 0;
	}

	seconds = (U32)((x  - y2koffset) / WINTICKSPERSEC);
	return seconds;
}


// Adjusts from pacific time and considers the daylightsavingsness of the date being converted
U32 timeGetSecondsSince2000FromPACDateString(const char *s)
{
	SYSTEMTIME	t_pac = {0};
	SYSTEMTIME t_utc = {0};
	TIME_ZONE_INFORMATION tziPAC;
	S64			x, y2koffset = timeY2KOffset();
	U32			seconds;
	int			len = (int)strlen(s);

	// Canonical string is in this form:
	// "%04d-%02d-%02d %02d:%02d:%02d" ,t->year,t->month,t->day,t->hour,t->minute,t->second

	if(len>0)
		t_pac.wYear		= (WORD)atoi(s+0);
	if(len>5)
		t_pac.wMonth	= (WORD)atoi(s+5);
	if(len>8)
		t_pac.wDay		= (WORD)atoi(s+8);
	if(len>11)
		t_pac.wHour		= (WORD)atoi(s+11);
	if(len>14)
		t_pac.wMinute	= (WORD)atoi(s+14);
	if(len>17)
		t_pac.wSecond	= (WORD)atoi(s+17);

	if (t_pac.wYear == 0)
	{
		// Invalid format
		return 0;

	}

	GetTziPac(&tziPAC);
	TzSpecificLocalTimeToSystemTime(&tziPAC, &t_pac, &t_utc);	
	SystemTimeToFileTime(&t_utc,(FILETIME *)&x);

	if (x < y2koffset)
	{
		// If the time is negative, return 0 instead of large positive time
		return 0;
	}

	seconds = (U32)((x  - y2koffset) / WINTICKSPERSEC);
	return seconds;
}


U32 timeGetSecondsSince2000FromIso8601String(const char *s)
{
	SYSTEMTIME	t = {0};
	FILETIME	local;
	S64			x, y2koffset = timeY2KOffset();
	U32			seconds;
	int			len = (int)strlen(s);
	char		temp[5];

	// Canonical string is in this form:
	// "%04d%02d%02dT%02d:%02d:%02d" ,t->year,t->month,t->day,t->hour,t->minute,t->second

	if (len < 17)
	{
		return 0;
	}

	strncpy(temp, s, 4);
	temp[4] = 0;
	t.wYear		= (WORD)atoi(temp);

	strncpy(temp, s+4, 2);
	temp[2] = 0;
	t.wMonth	= (WORD)atoi(temp);

	t.wDay		= (WORD)atoi(s+6);

	t.wHour		= (WORD)atoi(s+9);

	t.wMinute	= (WORD)atoi(s+12);

	t.wSecond	= (WORD)atoi(s+15);

	if (!t.wYear|| !t.wMonth || !t.wDay)
	{
		// Invalid format
		return 0;
	}

	SystemTimeToFileTime(&t,&local);
	LocalFileTimeToFileTime(&local,(FILETIME *)&x);

	if (x < y2koffset)
	{
		// If the time is negative, return 0 instead of large positive time
		return 0;
	}

	seconds = (U32)((x  - y2koffset) / WINTICKSPERSEC);
	return seconds;
}

static struct {
	U32 usingCache;
	U32 secondsCached;
} ss2000;

void timeSecondsSince2000EnableCache(S32 enabled)
{
	ss2000.usingCache = !!enabled;
}


static int sTimePrintfInterval = 0;
//if you add -TimePrintfInterval 60 to the command line, the server will printf the time every 60 seconds
AUTO_CMD_INT(sTimePrintfInterval, TimePrintfInterval);


AUTO_RUN;
void timeSecondsSince2000Update(void)
{
	S64			x;
 	GetSystemTimeAsFileTime((FILETIME *)&x);

	ss2000.secondsCached = (U32)((x  - timeY2KOffset()) /
		WINTICKSPERSEC + timing_debug_offset);

	if (sTimePrintfInterval)
	{
		static U32 sNextTimePrintfTime = 0;

		if (!sNextTimePrintfTime || sNextTimePrintfTime <= ss2000.secondsCached)
		{
			sNextTimePrintfTime = ss2000.secondsCached + sTimePrintfInterval;
			printf("%s\n", timeGetLocalDateStringFromSecondsSince2000(ss2000.secondsCached));
		}
	}

}

// returns seconds since 2000 based on local machine clock.  
// On clients, consider using timeServerSecondsSince2000() instead; it adds the client/server time offset.
U32 timeSecondsSince2000(void)
{
	if(ss2000.usingCache)
	{
		static volatile long ticks = 0;
		static volatile bool generated_devassert = false;

		// Generate devassertmsg() if caching is requested, but does not appear to be running yet.
		// Only do it once, to avoid spamming and, more importantly, reentrancy that could cause infinite recursion.
		if (!ss2000.secondsCached && !generated_devassert)
		{
			bool do_devassert = false;
			ATOMIC_INIT_BEGIN;
			do_devassert = true;
			generated_devassert = true;
			ATOMIC_INIT_END;
			if (do_devassert)
			{
				devassertmsg(ss2000.secondsCached, "Called timeSecondsSince2000() before it got"
					" its initial update, call timeSecondsSince2000Update() earlier");
				timeSecondsSince2000Update();
			}
		}

		// Update sometimes.
		if(!(InterlockedIncrement(&ticks) % 1000))
			timeSecondsSince2000Update();
	}
	else
	{
		timeSecondsSince2000Update();
	}

	return ss2000.secondsCached;
}

U32 timeSecondsSince2000_ForceRecalc(void)
{
	S64			x;
 	GetSystemTimeAsFileTime((FILETIME *)&x);

	return (U32)((x  - timeY2KOffset()) /
		WINTICKSPERSEC + timing_debug_offset);
}

S64 timeMsecsSince2000(void)
{
	S64		x;
 	GetSystemTimeAsFileTime((FILETIME *)&x);
	return (x  - timeY2KOffset()) / WINTICKSPERMSEC + timing_debug_offset*1000;
}

S32 timeLocalOffsetFromUTC(void)
{
#if _XBOX
	devassertmsg(false, "This function does not work on the Xbox 360.");
	return 0;
#else
	static S32 offset;
	if (!offset)
	{
		S64 utc, local;
		GetSystemTimeAsFileTime((FILETIME *)&utc);
		FileTimeToLocalFileTime((FILETIME *)&utc,(FILETIME *)&local);

		offset = ((S32)(local/ WINTICKSPERSEC) - (S32)(utc / WINTICKSPERSEC));
	}
	return offset;
#endif
}

S32 timeDaylightLocalOffsetFromUTC(U32 seconds)
{
#if _XBOX
	devassertmsg(false, "This function does not work on the Xbox 360.");
	return 0;
#else
	S64 utc, local;
	SYSTEMTIME st, t;

	utc = seconds;
	utc *= WINTICKSPERSEC;
	utc += timeY2KOffset();

	FileTimeToSystemTime((FILETIME*)&utc, &st);
	SystemTimeToTzSpecificLocalTime(NULL, &st, &t);
	SystemTimeToFileTime(&t, (FILETIME *)&local);

	return (S32)(local / WINTICKSPERSEC) - (S32)(utc / WINTICKSPERSEC);
#endif
}

// returns the ISO 8601-format offset the local timezone to utc
void timeLocalOffsetStringFromUTC(char *offsetstr, size_t offsetstr_size)
{
	char *buffer = NULL;
	S32 offset = timeLocalOffsetFromUTC();
	S32 hour = offset / 60 / 60;
	U32 minute = offset / 60 % 60;

	estrStackCreate(&buffer);
	estrPrintf(&buffer, "%+03ld:%02lu", hour, minute);
	if (estrLength(&buffer) < offsetstr_size)
		strcpy_s(offsetstr, offsetstr_size, buffer);
	estrDestroy(&buffer);
}

void timeSetServerDelta(U32 servertime)
{
	//(ServerTime is assumed to be GMT, thus timing_server_offset should be a tiny number)
	{
		int nonLocalDelta = (int)servertime - (int)timeSecondsSince2000();
		// we don't adjust by less than a second - this is to reduce jitter on the delta
		if (ABS_UNS_DIFF(nonLocalDelta, timing_server_offset) > 1)
			timing_server_offset = nonLocalDelta;
	}
}

U32 timeServerSecondsSince2000(void)
{
	return (U32)(timeSecondsSince2000() + timing_server_offset);
}

U32 timeLocalSecondsToServerSeconds(U32 localSeconds)
{
	return (U32)(localSeconds + timing_server_offset);
}

U32 timeServerSecondsToLocalSeconds(U32 serverSeconds)
{
	return (U32)(serverSeconds - timing_server_offset);
}

void timerTest()
{
	int		t = timerAlloc();
	int		i;

	S64* timer_buf = calloc(1000000, sizeof(S64));

	for(i=0;i<1000000;i++)
	{
		timer_buf[i] = timerCpuTicks64();
	}
	printf("%f\n",timerElapsed(t));
	printf("");
	timerFree(t);

	free( timer_buf );
}



#if !_PS3 && (defined(_DEBUG) || defined(PROFILE))

#define DEBUG_DEADLOCKS 0
#if !DEBUG_DEADLOCKS
	#define DEADLOCK_CHECK(cs, name, enter, justTry)
#else

#define DEADLOCK_CHECK(cs, name, enter, justTry) deadLockCheck(cs, name, enter, justTry)

static CRITICAL_SECTION csDeadLockCheck;
static bool csDeadLockCheckInited;
#define MAX_CS 600
typedef struct DeadLockDebugLock
{
	int lock_index;
	int count;
} DeadLockDebugLock;

typedef struct DeadLockDebugPerThread
{
	DeadLockDebugLock locks[MAX_CS];
	int lock_count;
} DeadLockDebugPerThread;

typedef struct DeadLockDebugPerLock
{
	CRITICAL_SECTION *cs;
	const char *name;
	int parents[MAX_CS];
	int parent_count;
} DeadLockDebugPerLock;

static DeadLockDebugPerLock deadlockcheck_locks[MAX_CS];
static int deadlockcheck_lock_count;
static MemLog deadlockcheck_memlog;
static int deadlockcheck_warned[100][2];
static int deadlockcheck_warned_count;

void deadLockCheck(CRITICAL_SECTION *cs, const char *name, bool enter, bool justTry)
{
	int i, j;
	int lock_index=-1;
	DeadLockDebugPerThread *perThreadData;
	STATIC_THREAD_ALLOC(perThreadData);

	if (!csDeadLockCheckInited) // Guaranteed to happen before we create any threads, so this is safe
	{
		InitializeCriticalSection(&csDeadLockCheck);
		csDeadLockCheckInited = true;
	}

	EnterCriticalSection(&csDeadLockCheck);

	// Find us an entry in the global list
	for (i=0; i<deadlockcheck_lock_count; i++)
	{
		if (deadlockcheck_locks[i].cs == cs)
		{
			assert(lock_index == -1);
			lock_index = i;
		}
	}
	if (lock_index==-1)
	{
		assert(enter);
		assertmsg(deadlockcheck_lock_count < ARRAY_SIZE(deadlockcheck_locks), "Too many critical sections, increase MAX_CS");
		lock_index = deadlockcheck_lock_count;
		deadlockcheck_locks[lock_index].cs = cs;
		deadlockcheck_locks[lock_index].name = name;
		deadlockcheck_lock_count++;
	}

	deadlockcheck_memlog.careAboutThreadId = true;
	memlog_printf(&deadlockcheck_memlog, "%s %d (%s)", enter?"ENTER":"LEAVE", lock_index, name);

	if (enter)
	{
		bool bAlreadyIn=false;

		// See if we're already in it in this thread, if so, no checks need to be done
		for (i=0; i<perThreadData->lock_count; i++)
		{
			if (perThreadData->locks[i].lock_index == lock_index)
			{
				assert(!bAlreadyIn);
				bAlreadyIn = true;
				perThreadData->locks[i].count++;
			}
		}
		if (!bAlreadyIn)
		{
			// Add to list
			assert(perThreadData->lock_count < ARRAY_SIZE(perThreadData->locks));
			perThreadData->locks[perThreadData->lock_count].lock_index = lock_index;
			perThreadData->locks[perThreadData->lock_count].count = 1;
			perThreadData->lock_count++;
			if (!justTry)
			{
				// Update my parent list
				for (i=0; i<perThreadData->lock_count-1; i++)
				{
					int parent_lock_index = perThreadData->locks[i].lock_index;
					bool b=false;
					for (j=0; j<deadlockcheck_locks[lock_index].parent_count; j++)
						if (deadlockcheck_locks[lock_index].parents[j] == parent_lock_index)
							b = true;
					if (!b)
						deadlockcheck_locks[lock_index].parents[deadlockcheck_locks[lock_index].parent_count++] = parent_lock_index;
				}
				// Check for a potential deadlock
				// It's a deadlock if any of my parents (in perThreadData->locks) have currently or previously
				//  had me as a parent
				// E.g. a deadlock can possibly occur if in one area of code we grab CS A, and then B, and then
				//   another section of code grabs B and then A.
				// Note: this is *not* necessarily true if more CSes are involved, e.g. the access pattern of
				//    C A B  and C B A are valid and deadlock-free (as long as A and B are *only* grabbed
				//    simultaneously when we have also C) - *but* in that case it's likely that the code
				//    can be optimized to only have CS C.
				// Note: this is possibly not fully taking into account if some of the critical section entries
				//    are done with TryEnterCriticalSection, which will likely have few deadlocks if handled well.
				for (i=0; i<perThreadData->lock_count-1; i++)
				{
					int parent_lock_index = perThreadData->locks[i].lock_index;
					for (j=0; j<deadlockcheck_locks[parent_lock_index].parent_count; j++)
					{
						if (deadlockcheck_locks[parent_lock_index].parents[j] == lock_index)
						{
							int k;
							bool bWarned=false;
							int i0 = MIN(lock_index, parent_lock_index);
							int i1 = MAX(lock_index, parent_lock_index);
							for (k=0; k<deadlockcheck_warned_count; k++)
							{
								if (deadlockcheck_warned[k][0] == i0 &&
									deadlockcheck_warned[k][1] == i1)
								{
									bWarned = true;
								}
							}
							if (!bWarned)
							{
								deadlockcheck_warned[deadlockcheck_warned_count][0] = i0;
								deadlockcheck_warned[deadlockcheck_warned_count++][1] = i1;
								//assertmsgf(0, "Detected possible deadlock with critical sections %s and %s\n", deadlockcheck_locks[lock_index].name, deadlockcheck_locks[parent_lock_index].name);
								printfColor(COLOR_RED|COLOR_BRIGHT, "Detected possible deadlock with critical sections %s and %s\n", deadlockcheck_locks[lock_index].name, deadlockcheck_locks[parent_lock_index].name);
							}
							//assertmsgf(0, "Detected possible deadlock with critical sections %s and %s\n", deadlockcheck_locks[lock_index].name, deadlockcheck_locks[parent_lock_index].name);
						}
					}
				}
			}
		}
	} else {
		// Leaving, just update data structures
		bool bFound=false;
		for (i=0; i<perThreadData->lock_count; i++)
		{
			if (perThreadData->locks[i].lock_index == lock_index)
			{
				assert(!bFound);
				if	(perThreadData->locks[i].count > 1)
				{
					perThreadData->locks[i].count--;
				} else {
					memmove(&perThreadData->locks[i], &perThreadData->locks[i+1], (perThreadData->lock_count - i - 1) * sizeof(perThreadData->locks[0]));
					perThreadData->lock_count--;
				}
				bFound = true;
			}
		}
		assertmsg(bFound, "Leaving a critical section this thread does not own?");
	}

	LeaveCriticalSection(&csDeadLockCheck);
}
#endif

// Enter/LeaveCriticalSection timed wrappers.

void timed_EnterCriticalSection(CRITICAL_SECTION* cs, const char* name, PERFINFO_TYPE** pi)
{
	PERFINFO_AUTO_START_STATIC_BLOCKING(name, pi, 1);
		DEADLOCK_CHECK(cs, name, true, false);
		EnterCriticalSection(cs);
	PERFINFO_AUTO_STOP();
}

BOOL timed_TryEnterCriticalSection(CRITICAL_SECTION* cs, const char* name)
{
	BOOL bRet;
	PERFINFO_AUTO_START_BLOCKING("TryEnterCriticalSection", 1);
		bRet = TryEnterCriticalSection(cs);
		if(bRet)
		{
			DEADLOCK_CHECK(cs, name, true, true);
		}
	PERFINFO_AUTO_STOP();
	return bRet;
}

void timed_LeaveCriticalSection(CRITICAL_SECTION* cs, const char* name, PERFINFO_TYPE** pi)
{
	PERFINFO_AUTO_START_BLOCKING_L2("LeaveCriticalSection", 1);
		PERFINFO_AUTO_START_STATIC_BLOCKING_L2(name, pi, 1);
			DEADLOCK_CHECK(cs, name, false, false);
			LeaveCriticalSection(cs);
		PERFINFO_AUTO_STOP_L2();
	PERFINFO_AUTO_STOP_L2();
}

void timed_Sleep(U32 ms, const char* name, PERFINFO_TYPE** pi)
{
	PERFINFO_AUTO_START_STATIC_BLOCKING(name, pi, 1);
		Sleep(ms);
	PERFINFO_AUTO_STOP();
}

void timed_SleepEx(U32 ms, BOOL alertable, const char* name, PERFINFO_TYPE** pi)
{
	PERFINFO_AUTO_START_STATIC_BLOCKING(name, pi, 1);
		SleepEx(ms, alertable);
	PERFINFO_AUTO_STOP();
}

U32 timed_WaitForSingleObject(HANDLE h, U32 ms, const char* name, PERFINFO_TYPE** pi)
{
	U32 ret;
	PERFINFO_AUTO_START_STATIC_BLOCKING(name, pi, 1);
		ret = WaitForSingleObject(h, ms);
	PERFINFO_AUTO_STOP();
	return ret;
}

U32 timed_WaitForSingleObjectEx(HANDLE h, U32 ms, S32 alertable, const char* name, PERFINFO_TYPE** pi)
{
	U32 ret;
	PERFINFO_AUTO_START_STATIC_BLOCKING(name, pi, 1);
		ret = WaitForSingleObjectEx(h, ms, alertable);
	PERFINFO_AUTO_STOP();
	return ret;
}

#if !PLATFORM_CONSOLE
U32 timed_MsgWaitForMultipleObjects(U32 count, HANDLE* handles, S32 waitForAll, U32 ms, U32 wakeMask)
{
	U32 ret;
	PERFINFO_AUTO_START_BLOCKING("MsgWaitForMultipleObjects", 1);
		ret = MsgWaitForMultipleObjects(count, handles, waitForAll, ms, wakeMask);
	PERFINFO_AUTO_STOP();
	return ret;
}

U32 timed_MsgWaitForMultipleObjectsEx(U32 count, HANDLE* handles, U32 ms, U32 wakeMask, U32 flags)
{
	U32 ret;
	PERFINFO_AUTO_START_BLOCKING("MsgWaitForMultipleObjectsEx", 1);
		ret = MsgWaitForMultipleObjectsEx(count, handles, ms, wakeMask, flags);
	PERFINFO_AUTO_STOP();
	return ret;
}
#endif

U32 timed_WaitForMultipleObjects(U32 count, HANDLE* handles, S32 waitForAll, U32 ms)
{
	U32 ret;
	PERFINFO_AUTO_START_BLOCKING("WaitForMultipleObjects", 1);
		ret = WaitForMultipleObjects(count, handles, waitForAll, ms);
	PERFINFO_AUTO_STOP();
	return ret;
}

U32 timed_WaitForMultipleObjectsEx(U32 count, HANDLE* handles, S32 waitForAll, U32 ms, S32 isAlertable)
{
	U32 ret;
	PERFINFO_AUTO_START_BLOCKING("WaitForMultipleObjectsEx", 1);
		ret = WaitForMultipleObjectsEx(count, handles, waitForAll, ms, isAlertable);
	PERFINFO_AUTO_STOP();
	return ret;
}

BOOL timed_SetEvent(HANDLE h)
{
	BOOL ret;
	PERFINFO_AUTO_START_BLOCKING("SetEvent", 1);
		ret = SetEvent(h);
	PERFINFO_AUTO_STOP();
	return ret;
}

BOOL timed_ResetEvent(HANDLE h){
	BOOL ret;
	PERFINFO_AUTO_START_BLOCKING("ResetEvent", 1);
		ret = ResetEvent(h);
	PERFINFO_AUTO_STOP();
	return ret;
}


#endif

// Starts recording profiling information to the given filename
AUTO_COMMAND ACMD_CATEGORY(Profile, Standard) ACMD_ACCESSLEVEL(9) ACMD_APPSPECIFICACCESSLEVEL(GLOBALTYPE_CLIENT, 0);
void timerRecordStart(const char *filename)
{
#if !_PS3
	char filePath[MAX_PATH];
	timerGetProfilerFileName(filename, SAFESTR(filePath), 1);
	timerRecordThreadStart(filePath);
#endif
}

// Stops any current profiler recording or playback
AUTO_COMMAND ACMD_CATEGORY(Profile, Standard) ACMD_ACCESSLEVEL(9) ACMD_APPSPECIFICACCESSLEVEL(GLOBALTYPE_CLIENT, 0);
void timerRecordEnd(void)
{
#if !_PS3
	timerRecordThreadStop();
#endif
}

// Plays back previously recorded profiler information, at the given filename
AUTO_COMMAND ACMD_CATEGORY(Profile, Standard) ACMD_ACCESSLEVEL(9);
void timerRecordPlay(const char *filename)
{
#if !_XBOX
	char cmd[1024];
	sprintf(cmd, "C:/Night/tools/bin/Profiler.exe -autoLoadProfile \"%s\"", filename);
	system_detach(cmd, false, false);
#endif
}

typedef struct FrameLockedTimer {
	U32				totalFrames;
	
	U32				ticksPerSecond;
	U32				ticksPerProcess;
	U32				accTicks;
	U32				accTicksUsed;

	F32				fixedRateSecondsPerFrame;

	U64				accCpuTicks;
	U64				lastCpuTicks;
	U64				lastCpuTicksReal; // lastCpuTicks has some accumulated ticks when running with a scaled timestep

	struct {
		F32			processRatio;
		F32			frameSeconds;
		F32			frameSecondsReal;
		U32			frameCpuTicks;
		U32			milliseconds;
		U32			deltaMilliseconds;
		U32			processes;
		U32			deltaProcesses;
	} cur, prev;
} FrameLockedTimer;

FrameLockedTimer* ulFLT = NULL;
S64 ulAbsTime = 0;

void frameLockedTimerCreate(FrameLockedTimer** timerOut,
							U32 ticksPerSecond,
							U32	ticksPerProcess)
{
	FrameLockedTimer* timer;

	if(!timerOut){
		return;
	}

	*timerOut = timer = callocStruct(FrameLockedTimer);

	timer->ticksPerProcess = ticksPerProcess;
	timer->ticksPerSecond = ticksPerSecond;
	timer->lastCpuTicks = timerCpuTicks64();
	timer->lastCpuTicksReal = timerCpuTicks64();
}

void frameLockedTimerSetFixedRate(	FrameLockedTimer* timer,
									F32 secondsPerFrame)
{
	timer->fixedRateSecondsPerFrame = secondsPerFrame;
}

void frameLockedTimerDestroy(FrameLockedTimer** timerInOut){
	if(timerInOut){
		SAFE_FREE(*timerInOut);
	}
}

void frameLockedTimerStartNewFrame(	FrameLockedTimer* timer,
									F32 timeStepScale)
{
	// MS: This function is incredibly convoluted, but trust me that it works.
	
	PERFINFO_AUTO_START_FUNC();
	
	MAX1(timeStepScale, 0.00001f);

	if(timer){
		U64 cpuTicksPerSecond = timerCpuSpeed64();

		U64 curCpuTicks = timerCpuTicks64();
		U64 deltaCpuTicksReal = curCpuTicks - timer->lastCpuTicks;
		U64 deltaCpuTicks;

		U32 curMilliseconds = timeGetTime();
		U32 deltaMillisecondsReal = curMilliseconds - timer->cur.milliseconds;
		U32 deltaMilliseconds = timer->fixedRateSecondsPerFrame ?
									timer->fixedRateSecondsPerFrame * 1000 :
									deltaMillisecondsReal * timeStepScale;

		U64 accCpuTicks;
		U64 ticksPerProcess = timer->ticksPerProcess;
		U64 ticksPerSecond = timer->ticksPerSecond;
		U64 accTicks = 0;
		U32 curProcesses = 0;

		if(deltaCpuTicksReal < 0){
			deltaCpuTicksReal = 0;
		}
		
		deltaCpuTicks = timer->fixedRateSecondsPerFrame ?
							timer->fixedRateSecondsPerFrame * cpuTicksPerSecond :
							deltaCpuTicksReal * timeStepScale;

		timer->totalFrames++;

		// Copy the current values to the previous slot.

		timer->prev = timer->cur;
		
		if(timeStepScale == 0.f){
			timer->lastCpuTicks += deltaCpuTicksReal;
		}else{
			// JE: Not using curCpuTicks so timeStepScale doesn't lose ticks.
			timer->lastCpuTicks += (U64)(deltaCpuTicks / timeStepScale);
		}

		// Calculate the current stuff.

		if(deltaCpuTicks > cpuTicksPerSecond / 2){
			deltaCpuTicks = cpuTicksPerSecond / 2;
		}

		// Accumulate up to one second of CPU ticks.
		
		accCpuTicks = timer->accCpuTicks + deltaCpuTicks;

		while(accCpuTicks >= cpuTicksPerSecond){
			accCpuTicks -= cpuTicksPerSecond;

			accTicks += timer->ticksPerSecond;
		}
		
		timer->accCpuTicks = accCpuTicks;
		
		// Add in the remainder of the cpu ticks, scaled to ticksPerSecond.

		accTicks += (ticksPerSecond * accCpuTicks) / cpuTicksPerSecond;

		// Subtract off how many of the seconds worth of user ticks have been used.
		
		accTicks -= timer->accTicksUsed;

		// While the total is more than a process, increment the process counter.

		while(accTicks >= ticksPerProcess){
			accTicks -= ticksPerProcess;

			curProcesses++;

			timer->accTicksUsed += ticksPerProcess;
		}
		
		// Clamp the used user tick count back to within one second.

		while(timer->accTicksUsed >= ticksPerSecond){
			timer->accTicksUsed -= ticksPerSecond;
		}

		// Set all the current values.

		timer->cur.processRatio = (F32)accTicks / ticksPerProcess;
		
		timer->cur.deltaProcesses = curProcesses;
		timer->cur.processes += curProcesses;

		timer->cur.deltaMilliseconds = deltaMilliseconds;
		if(timeStepScale != 0.f){
			timer->cur.milliseconds += deltaMilliseconds / timeStepScale;
		}

		timer->cur.frameSeconds = (F32)deltaCpuTicks / cpuTicksPerSecond;
		timer->cur.frameSecondsReal = (F32)(curCpuTicks - timer->lastCpuTicksReal) / cpuTicksPerSecond;
		timer->lastCpuTicksReal = curCpuTicks;

		assert(timer->cur.frameSeconds >= 0.f);
		assert(timer->cur.frameSecondsReal >= 0.f);
		assert(timer->prev.frameSeconds >= 0.f);
		assert(timer->prev.frameSecondsReal >= 0.f);

		timer->cur.frameCpuTicks = (ticksPerSecond * deltaCpuTicks) / cpuTicksPerSecond;
	}
	
	PERFINFO_AUTO_STOP();
}

void frameLockedTimerSetCurProcesses(	FrameLockedTimer* timer,
										U32 processes)
{
	timer->cur.deltaProcesses = processes;
}

void frameLockedTimerGetTotalFrames(const FrameLockedTimer* timer,
									U32* totalFramesOut)
{
	if(timer){
		if(totalFramesOut){
			*totalFramesOut = timer->totalFrames;
		}
	}
}

void frameLockedTimerGetCurTimes(	const FrameLockedTimer* timer,
									F32* curFrameSecondsOut,
									U32* curMillisecondsOut,
									U32* deltaMillisecondsOut)
{
	if(timer){
		if(curFrameSecondsOut){
			*curFrameSecondsOut = timer->cur.frameSeconds;
		}

		if(curMillisecondsOut){
			*curMillisecondsOut = timer->cur.milliseconds;
		}

		if(deltaMillisecondsOut){
			*deltaMillisecondsOut = timer->cur.deltaMilliseconds;
		}
	}
}

void frameLockedTimerGetCurTimesReal(	const FrameLockedTimer* timer,
										F32* curFrameSecondsOut)
{
	if(timer){
		if(curFrameSecondsOut){
			*curFrameSecondsOut = timer->cur.frameSecondsReal;
		}
	}
}

void frameLockedTimerGetPrevTimes(	const FrameLockedTimer* timer,
									F32* curFrameSecondsOut,
									U32* curMillisecondsOut,
									U32* deltaMillisecondsOut)
{
	if(timer){
		if(curFrameSecondsOut){
			*curFrameSecondsOut = timer->prev.frameSeconds;
		}

		if(curMillisecondsOut){
			*curMillisecondsOut = timer->prev.milliseconds;
		}

		if(deltaMillisecondsOut){
			*deltaMillisecondsOut = timer->prev.deltaMilliseconds;
		}
	}
}

void frameLockedTimerGetPrevTimesReal(	const FrameLockedTimer* timer,
										F32* curFrameSecondsOut)
{
	if(timer){
		if(curFrameSecondsOut){
			*curFrameSecondsOut = timer->prev.frameSecondsReal;
		}
	}
}

void frameLockedTimerGetProcessRatio(	const FrameLockedTimer* timer,
										F32* curProcessRatioOut,
										F32* prevProcessRatioOut)
{
	if(timer){
		if(curProcessRatioOut){
			*curProcessRatioOut = timer->cur.processRatio;
		}

		if(prevProcessRatioOut){
			*prevProcessRatioOut = timer->prev.processRatio;
		}
	}
}

void frameLockedTimerGetProcesses(	const FrameLockedTimer* timer,
									U32* curProcessesOut,
									U32* curProcessesDeltaOut,
									U32* prevProcessesOut,
									U32* prevDeltaProcessesOut)
{
	if(timer){
		if(curProcessesOut){
			*curProcessesOut = timer->cur.processes;
		}
		
		if(curProcessesDeltaOut){
			*curProcessesDeltaOut = timer->cur.deltaProcesses;
		}
		
		if(prevProcessesOut){
			*prevProcessesOut = timer->prev.processes;
		}
		
		if(prevDeltaProcessesOut){
			*prevDeltaProcessesOut = timer->prev.deltaProcesses;
		}
	}
}

void frameLockedTimerGetCurSeconds(	const FrameLockedTimer* timer,
									F32* secondsSoFarOut)
{
	S64 cpuTicksSoFar = timerCpuTicks64() - timer->lastCpuTicksReal;

	if(secondsSoFarOut){
		*secondsSoFarOut = (F32)cpuTicksSoFar / timerCpuSpeed64();
		assert(*secondsSoFarOut >= 0);
	}
}

void frameLockedTimerGetFrameTicks(	const FrameLockedTimer* timer,
									U32* ticksOut)
{
	if(	timer &&
		ticksOut)
	{
		*ticksOut = timer->cur.frameCpuTicks;
	}
}


U32 timeGetSecondsSince2000FromWindowsTime32(__time32_t iTime)
{
	struct tm timeStruct;

	_localtime32_s(&timeStruct, &iTime);

	return timeGetSecondsSince2000FromLocalTimeStruct(&timeStruct);
}

char *timeGetGimmeStringFromSecondsSince2000(U32 iSeconds)
{
	static char retString[20] = "";
	SYSTEMTIME t;

	timerSystemTimeFromSecondsSince2000(&t,iSeconds);

	sprintf(retString, "%02d%02d%02d%02d:%02d:%02d", 
		t.wMonth, t.wDay, t.wYear % 100, t.wHour, t.wMinute, t.wSecond);

	return retString;
}

char *timeGetLocalGimmeStringFromSecondsSince2000(U32 iSeconds)
{
	static char retString[20] = "";
	SYSTEMTIME t;

	timerLocalSystemTimeFromSecondsSince2000(&t,iSeconds);

	sprintf(retString, "%02d%02d%02d%02d:%02d:%02d", 
		t.wMonth, t.wDay, t.wYear % 100, t.wHour, t.wMinute, t.wSecond);

	return retString;
}

//gets seconds from string of form MMDDYYHH{:MM{:SS}}
U32 timeGetSecondsSince2000FromGimmeString_internal(const char *pString, bool bLocal)
{
	char *pWorkString = NULL;
	int iLen;
	SYSTEMTIME t = {0};
	int i;

	estrStackCreate(&pWorkString);
	estrCopy2(&pWorkString, pString);
	estrTrimLeadingAndTrailingWhitespace(&pWorkString);

	iLen = estrLength(&pWorkString);

	if (!(iLen == 8 || iLen == 11 || iLen == 14))
	{
		estrDestroy(&pWorkString);
		return 0;
	}

	for (i=0; i < 8; i++)
	{
		if (!isdigit(pWorkString[i]))
		{
			estrDestroy(&pWorkString);
			return 0;
		}
	}

	t.wMonth = (pWorkString[0] - '0') * 10 + pWorkString[1] - '0';
	t.wDay = (pWorkString[2] - '0') * 10 + pWorkString[3] - '0';
	t.wYear = 2000 + (pWorkString[4] - '0') * 10 + pWorkString[5] - '0';
	t.wHour = (pWorkString[6] - '0') * 10 + pWorkString[7] - '0';

	if (iLen > 8)
	{

		if (pWorkString[8] != ':' || !isdigit(pWorkString[9]) || !isdigit(pWorkString[10]))
		{
			estrDestroy(&pWorkString);
			return 0;
		}

		t.wMinute = (pWorkString[9] - '0') * 10 + pWorkString[10] - '0';

		if (iLen > 11)
		{

			if (pWorkString[11] != ':' || !isdigit(pWorkString[12]) || !isdigit(pWorkString[13]))
			{
				estrDestroy(&pWorkString);
				return 0;
			}

			t.wSecond = (pWorkString[12] - '0') * 10 + pWorkString[13] - '0';
		}
	}

	estrDestroy(&pWorkString);

	if (bLocal)
	{
		return timerSecondsSince2000FromLocalSystemTime(&t);
	}
	else
	{
		return timerSecondsSince2000FromSystemTime(&t);
	}
}

U32 timeGetSecondsSince2000FromGimmeString(const char *pString)
{
	return timeGetSecondsSince2000FromGimmeString_internal(pString, false);
}

U32 timeGetSecondsSince2000FromLocalGimmeString(const char *pString)
{
	return timeGetSecondsSince2000FromGimmeString_internal(pString, true);
}





// Here's a bunched of timed stdlib functions from stdtypes.h

#undef printf
#undef sprintf
#undef vsprintf
#undef _vscprintf
#undef vprintf

typedef struct NetLink NetLink;
extern NetLink *gpNetLinkForPrintf;
int NetLinkVPrintf(NetLink *pNetLink, const char *format, va_list argptr);

static CRITICAL_SECTION csPrintfTimed;
static int printf_timer;

// This function allows color consistency when multiple threads are doing colored printf() at the same time.
void printfEnterCS(void)
{
	if (!serialize_printf)
		return;

	PERFINFO_AUTO_START_FUNC_L2();
	ATOMIC_INIT_BEGIN;
	{
		InitializeCriticalSection(&csPrintfTimed);
		printf_timer = timerAlloc();
	}
	ATOMIC_INIT_END;
	EnterCriticalSection(&csPrintfTimed);
	timerUnpause(printf_timer);
	PERFINFO_AUTO_STOP_L2();
}

void printfLeaveCS(void)
{
	if (!serialize_printf)
		return;

	PERFINFO_AUTO_START_FUNC_L2();
	timerPause(printf_timer);
	LeaveCriticalSection(&csPrintfTimed);
	PERFINFO_AUTO_STOP_L2();
}

AUTO_COMMAND;
F32 printf_time(void)
{
	return timerElapsed(printf_timer);
}

//NOTE NOTE this is now a dummy function, the actual work is done with a ParseCommandOutOfCommandLine
static bool enableUseCRTprintf=false;
AUTO_CMD_INT(enableUseCRTprintf, useCRTprintf) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;

static bool useCRTprintf=true; // starts by using the CRT until we can parse the command line and decide otherwise
// Forces the use of the (slow) CRT printf instead of WriteConsole for console output



int printf_timed(const char *format, ...)
{
	int result;
	va_list argptr;

	PERFINFO_AUTO_START("printf", 1);
		va_start(argptr, format);
		
		result = vprintf_timed(format, argptr);

		va_end(argptr);
	PERFINFO_AUTO_STOP();
	

	return result;
}

int sprintf_timed(char *buffer, size_t sizeOfBuffer, const char *format, ...)
{
	int result;
	va_list argptr;

	PERFINFO_AUTO_START("sprintf", 1);
		va_start(argptr, format);
		result = vsprintf_s(buffer, sizeOfBuffer, format, argptr);
		va_end(argptr);
	PERFINFO_AUTO_STOP();

	return result;
}

int vsprintf_timed(char *buffer, size_t sizeOfBuffer, const char *format, va_list argptr)
{
	int result;

	PERFINFO_AUTO_START("vsprintf", 1);
		result = vsprintf_s(buffer, sizeOfBuffer, format, argptr);
	PERFINFO_AUTO_STOP();

	return result;
}

int _vscprintf_timed(const char *format, va_list argptr)
{
	int result;

	PERFINFO_AUTO_START("_vscprintf", 1);
		printfEnterCS();
		result = _vscprintf(format, argptr);
		printfLeaveCS();
	PERFINFO_AUTO_STOP();

	return result;
}

void setVprintfFunc(VprintfFunc func)
{
	vprintf_func = func;
}

S32 createConsoleOnPrintf;

typedef struct vprintf_timed_threadData
{
	char *buffer;
} vprintf_timed_threadData;

int vprintf_timed(const char *format, va_list argptr)
{
	int result = 0;
	char *pDateString = NULL;
	vprintf_timed_threadData *threadData;
	STATIC_THREAD_ALLOC(threadData);

	PERFINFO_AUTO_START("vprintf", 1);
	
	#if !PLATFORM_CONSOLE
	if(createConsoleOnPrintf){
		ATOMIC_INIT_BEGIN;
			newConsoleWindow();
		ATOMIC_INIT_END;
	}
	#endif
	
	if (vprintf_func) {
		assert(vprintf_func != vprintf_timed); // Infinite recursion!  Although it's probably a vprintf_timed in a different DLL/module
		result = vprintf_func(format, argptr);


		if (spFileForPrintf)
		{
			int len;

			estrClear(&threadData->buffer);
			len = estrConcatfv(&threadData->buffer, format, argptr);

			printfEnterCS();
			{
				static U32 iLastTime = 0;
				U32 iCurTime = timeSecondsSince2000();

				if (iCurTime != iLastTime)
				{
					iLastTime = iCurTime;
					fprintf(spFileForPrintf, "%s", timeGetLocalDateStringFromSecondsSince2000(iCurTime));
					fprintf(spFileForPrintf, ": ");
				}
			}
			fwrite(threadData->buffer, estrLength(&threadData->buffer), 1, spFileForPrintf);
			fflush(spFileForPrintf);
			printfLeaveCS();
		}
	} else {
		int len;
		printfEnterCS();

		PERFINFO_AUTO_START("estrConcatfv", 1);

		estrClear(&threadData->buffer);
		len = estrConcatfv(&threadData->buffer, format, argptr);

		PERFINFO_AUTO_STOP_START("vprintf", 1);
		if (!sbDisablePrintf)
		{
			static bool sbFirstTime = true;

			if (sbFirstTime)
			{
				char temp[11] = "";
				sbFirstTime = false;

				ParseCommandOutOfCommandLine("useCRTprintf", temp);
				if (temp[0])
				{
					useCRTprintf = atoi(temp);
				}
				else
				{
					useCRTprintf = false;
				}
			}

			if (gbTimeStampAllPrintfs)
			{
				static U32 iLastTime = 0;
				U32 iCurTime = timeSecondsSince2000();

				if (iCurTime != iLastTime)
				{
					iLastTime = iCurTime;
					consolePushColor();
					consoleSetColor(0, COLOR_RED | COLOR_GREEN | COLOR_BLUE);
					printf(timeGetLocalDateStringFromSecondsSince2000(iCurTime));
					printf(": ");
					consolePopColor();

				}
			}

			

	

			if (!useCRTprintf) {
				static HANDLE hConsole;
				DWORD dwNumberOfCharsWritten;
				if (!hConsole)
					hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
				if (!hConsole || !WriteConsole_UTF8(hConsole, threadData->buffer, len, &dwNumberOfCharsWritten, NULL))
				{
					if (hConsole)
					{
						// Had a console, but failed anyway, will continue to fail, most likely
						OutputDebugString(L"WriteConsole() failed, reverting to CRT printf (expected if writing to a pipe).\n");
						useCRTprintf = true;
					}
					result = print_UTF8(threadData->buffer);
						printf("%s", threadData->buffer);
				} else {
					result = dwNumberOfCharsWritten;
				}
			} else {
				result = print_UTF8(threadData->buffer);
			}

			if (spFileForPrintf)
			{
				{
					static U32 iLastTime = 0;
					U32 iCurTime = timeSecondsSince2000();

					if (iCurTime != iLastTime)
					{
						iLastTime = iCurTime;
						fprintf(spFileForPrintf, "%s", timeGetLocalDateStringFromSecondsSince2000(iCurTime));
						fprintf(spFileForPrintf, ": ");
					}
				}

				fwrite(threadData->buffer, estrLength(&threadData->buffer), 1, spFileForPrintf);
				fflush(spFileForPrintf);
			}

		}
		PERFINFO_AUTO_STOP();

#if !PLATFORM_CONSOLE
		PERFINFO_AUTO_START("debug", 1);
		{
			char *s;
			if(IsDebuggerPresent())
			{
				PERFINFO_AUTO_START("OutputDebugString", 1);
				OutputDebugString_UTF8(threadData->buffer);
				PERFINFO_AUTO_STOP();
			}
			for (s = threadData->buffer; *s==' ' || *s=='\n'; s++);
			if (*s) // There is a character which is not a space
				memlog_printf(0, "%s", threadData->buffer);
		}
		PERFINFO_AUTO_STOP();
#endif

		if (gpNetLinkForPrintf)
		{
			NetLinkVPrintf(gpNetLinkForPrintf, format, argptr);
		}
		printfLeaveCS();
	}
	PERFINFO_AUTO_STOP();

	return result;
}

#if PROFILE_PERF
#if !defined(INLINE_GETPROCESSOR)
int timeGetCurrProcessor()
{
	return GetCurrentProcessorNumber();
}
#endif

extern char gBreakOnName[ 256 ];
char gBreakOnName[ 256 ] = { "mpAlloc_dbg" };
static int debugBreakDummy = 0;

void timeLog( const PerformanceInfo * info, int nProcessor, S64 currentTime )
{
	if ( gBreakOnName[ 0 ] && !strcmp( info->locName, gBreakOnName ) )
	{
		++debugBreakDummy;
	}
	OutputDebugStringf( "%10.5f %.3f %d %s\n", 
		(double)currentTime / timeGetCPUCyclesPerSecond(),
		(double)( currentTime - timing_state.logTime[ nProcessor ] ) / timeGetCPUCyclesPerSecond(),
		nProcessor,	info->locName
		);
	timing_state.logTime[ nProcessor ] = currentTime;
}
#endif


U64 microsecondsSince1601(void)
{
	U64 retVal = 0;
	FILETIME time = {0};

	GetSystemTimeAsFileTime(&time);
	retVal = (((U64)time.dwHighDateTime<<32) + (U64)time.dwLowDateTime)/10;
	return retVal;
}


typedef enum enumIntervalType
{
	INTERVALS_1SECOND,
	INTERVALS_5SECOND,
	INTERVALS_15SECOND,
	INTERVALS_30SECOND,
	INTERVALS_1MINUTE,
	INTERVALS_5MINUTE,
	INTERVALS_15MINUTE,
	INTERVALS_30MINUTE,
	INTERVALS_1HOUR,
	INTERVALS_2HOUR,
	INTERVALS_6HOUR,
	INTERVALS_12HOUR,
	INTERVALS_1DAY,
	INTERVALS_2DAY,
	INTERVALS_5DAY,
	INTERVALS_15DAY,
	INTERVALS_1MONTH,
	INTERVALS_2MONTH,
	INTERVALS_6MONTH,
	INTERVALS_1YEAR,

	INTERVALS_LAST,
} enumIntervalType;

static int sIntervalLengths[] = 
{
	1,	
	5,
	15,
	30,
	60,
	60 * 5,
	60 * 15,
	60 * 30,
	60 * 60,
	60 * 60 * 2,
	60 * 60 * 6,
	60 * 60 * 12,
	60 * 60 * 24,
	2 * 60 * 60 * 24,
	5 * 60 * 60 * 24,
	15 * 60 * 60 * 24,
	31 * 60 * 60 * 24 + 10, //adding 10 seconds from here on up so that we don't get hung up on some freaky-ass leap-second corner case
							//(it will get rounded away in RecorrectTime()
	2 * 31 * 60 * 60 * 24 + 10,
	6 * 31 * 60 * 60 * 24 + 10,
	367 * 60 * 60 * 24 + 10,


};

//re-rounds off a time that is being incremented in months and years, which do not have a constant
//length in seconds
static void RecorrectTime(U32 *piTime, enumIntervalType eInterval)
{
	struct tm t = {0};

	switch (eInterval)
	{
	case INTERVALS_1MONTH:
	case INTERVALS_2MONTH:
	case INTERVALS_6MONTH:
		timeMakeLocalTimeStructFromSecondsSince2000(*piTime, &t);

		t.tm_mday = 1;
		t.tm_hour = 0;
		t.tm_min = 0;
		t.tm_sec = 0;

		*piTime = timeGetSecondsSince2000FromLocalTimeStruct(&t);
		break;

	case INTERVALS_1YEAR:
		timeMakeLocalTimeStructFromSecondsSince2000(*piTime, &t);

		t.tm_mon = 0;
		t.tm_mday = 1;
		t.tm_hour = 0;
		t.tm_min = 0;
		t.tm_sec = 0;

		*piTime = timeGetSecondsSince2000FromLocalTimeStruct(&t);
		break;
	}
}







static U32 FindPreviousInterval(U32 iStartingTime, enumIntervalType eInterval)
{

	struct tm t = {0};
	U32 iOutTime, iNextTime;
	int iInterval = sIntervalLengths[eInterval];

	timeMakeLocalTimeStructFromSecondsSince2000(iStartingTime, &t);

	switch (eInterval)
	{
	case INTERVALS_1YEAR:
		t.tm_mon = 0;
		t.tm_mday = 1;
		t.tm_hour = 0;
		t.tm_min = 0;
		t.tm_sec = 0;
		break;

	case INTERVALS_1MONTH:
	case INTERVALS_2MONTH:
	case INTERVALS_6MONTH:
		t.tm_mday = 1;
		t.tm_hour = 0;
		t.tm_min = 0;
		t.tm_sec = 0;
		break;

	case INTERVALS_2HOUR:
	case INTERVALS_6HOUR:
	case INTERVALS_12HOUR:
	case INTERVALS_1DAY:
	case INTERVALS_2DAY:
	case INTERVALS_5DAY:
	case INTERVALS_15DAY:
		t.tm_hour = 0;
		t.tm_min = 0;
		t.tm_sec = 0;
		break;

	case INTERVALS_5MINUTE:
	case INTERVALS_15MINUTE:
	case INTERVALS_30MINUTE:
	case INTERVALS_1HOUR:
		t.tm_min = 0;
		t.tm_sec = 0;
		break;

	case INTERVALS_1SECOND:
	case INTERVALS_5SECOND:
	case INTERVALS_15SECOND:
	case INTERVALS_30SECOND:
	case INTERVALS_1MINUTE:
		t.tm_sec = 0;
		break;
	}

	iOutTime =  timeGetSecondsSince2000FromLocalTimeStruct(&t);
	iNextTime = iOutTime + iInterval;

	while (iNextTime < iStartingTime)
	{
		iOutTime = iNextTime;
		iNextTime += iInterval;
		RecorrectTime(&iNextTime, eInterval);
	}

	return iOutTime;
}

static enumIntervalType FindBestInterval(U32 iRange, int iMinPoints)
{
	int i;

	for (i=0; i < INTERVALS_LAST; i++)
	{
		int iCurCount = iRange / sIntervalLengths[i];
		if (iCurCount < iMinPoints)
		{
			return (i > 0 ? i-1 : 0);
		}
	}

	return i - 1;
}

static char *sWeekDayNames[] = 
{
	"Sun",
	"Mon",
	"Tue",
	"Wed",
	"Thu",
	"Fri",
	"Sat",
};

static char *sMonthNames[] = 
{
	"Jan",
	"Feb",
	"Mar",
	"Apr",
	"May",
	"Jun",
	"Jul",
	"Aug",
	"Sep",
	"Oct",
	"Nov",
	"Dec",
};


char *GetMonthName(int iMonthNum)
{
	assert(iMonthNum >= 1 && iMonthNum <= 12);
	return sMonthNames[iMonthNum - 1];
}

static void GetLogicalTimeName(U32 iInTime, enumIntervalType eInterval, char *pOutName, int iOutNameSize)
{
	struct tm t;
	timeMakeLocalTimeStructFromSecondsSince2000(iInTime, &t);

	switch (eInterval)
	{
	case INTERVALS_1YEAR:
		snprintf_s(pOutName, iOutNameSize, "%d", t.tm_year + 1900);
		break;

	case INTERVALS_6MONTH:
		snprintf_s(pOutName, iOutNameSize, "%s '%02d", sMonthNames[t.tm_mon], t.tm_year % 100);
		break;

	case INTERVALS_2MONTH:
	case INTERVALS_1MONTH:
		snprintf_s(pOutName, iOutNameSize, "%s", sMonthNames[t.tm_mon]);
		break;

	case INTERVALS_15DAY:
	case INTERVALS_5DAY:
	case INTERVALS_2DAY:
		snprintf_s(pOutName, iOutNameSize, "%s, %s %d", sWeekDayNames[t.tm_wday], sMonthNames[t.tm_mon], t.tm_mday);
		break;

	case INTERVALS_1DAY:
		snprintf_s(pOutName, iOutNameSize, "%s", sWeekDayNames[t.tm_wday]);
		break;

	case INTERVALS_12HOUR:
	case INTERVALS_6HOUR:
	case INTERVALS_2HOUR:
		snprintf_s(pOutName, iOutNameSize, "%s %d:00", sWeekDayNames[t.tm_wday], t.tm_hour);
		break;

	case INTERVALS_1HOUR:
	case INTERVALS_30MINUTE:
	case INTERVALS_15MINUTE:
	case INTERVALS_5MINUTE:
	case INTERVALS_1MINUTE:
		snprintf_s(pOutName, iOutNameSize, "%d:%02d", t.tm_hour, t.tm_min);
		break;

	case INTERVALS_30SECOND:
	case INTERVALS_15SECOND:
	case INTERVALS_5SECOND:
	case INTERVALS_1SECOND:
		snprintf_s(pOutName, iOutNameSize, "%d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
		break;
	}
}

void timeGetLogicallyNamedIntervals(U32 iStartingTime, U32 iEndingTime, int iMinPoints,
	NamedTime ***pppOutNames)
{
	U32 iRange = iEndingTime - iStartingTime;
	enumIntervalType eInterval = FindBestInterval(iRange, iMinPoints);
	U32 iCurTime = FindPreviousInterval(iStartingTime, eInterval);


	while (iCurTime <= iEndingTime)
	{
		NamedTime *pTime = calloc(sizeof(NamedTime), 1);
		pTime->iTime = iCurTime;
		GetLogicalTimeName(iCurTime, eInterval, SAFESTR(pTime->name));

		eaPush(pppOutNames, pTime);
		
		iCurTime += sIntervalLengths[eInterval];
		RecorrectTime(&iCurTime, eInterval);
	}
}
		
/*
AUTO_RUN;
void intervalTest(void)
{
	NamedTime **ppNames = NULL;

	int i, j;
	int iInterval = 10;
	U32 iCurTime = timeSecondsSince2000();

	newConsoleWindow();

	for (i = 0; i < 25; i++)
	{
		printf("Interval :%d seconds\n", iInterval);
		timeGetLogicallyNamedIntervals(iCurTime, iCurTime + iInterval, 3, &ppNames);

		for (j=0; j < eaSize(&ppNames); j++)
		{
			printf("%s (%s)\n", ppNames[j]->name, timeGetLocalDateStringFromSecondsSince2000(ppNames[j]->iTime));
		}

		eaDestroyEx(&ppNames, NULL);

		iInterval *= 2;
	}

	{
		int iBrk = 0;
	}
}

*/

//parses a time of the format "Mon Apr 30 16:19:05 2007"
char *timeGetLocalSystemStyleStringFromSecondsSince2000(U32 iTime)
{
	static char outString[26];
	struct tm t;
	timeMakeLocalTimeStructFromSecondsSince2000(iTime, &t);

	sprintf_s(SAFESTR(outString), "%s %s %02d %02d:%02d:%02d %04d",
		sWeekDayNames[t.tm_wday], sMonthNames[t.tm_mon],
		t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, t.tm_year + 1900);

	return outString;
}

//macro to make %d second%s print out "1 second" but "2 seconds"
#define PLURALIZE(x) x, (x) == 1 ? "" : "s"

void timeSecondsDurationToPrettyEString(U32 iNumSeconds, char **ppEString)
{
	if (iNumSeconds == 0)
	{
		estrPrintf(ppEString, "0 seconds");
	}
	else if (iNumSeconds < 60)
	{
		estrPrintf(ppEString, "%d second%s", PLURALIZE(iNumSeconds));
	}
	else if (iNumSeconds < 60 * 60)
	{
		estrPrintf(ppEString, "%d minute%s %d second%s", PLURALIZE(iNumSeconds / 60), PLURALIZE(iNumSeconds % 60));
	}
	else if (iNumSeconds < 60 * 60 * 24)
	{
		estrPrintf(ppEString, "%d hour%s %d minute%s", PLURALIZE(iNumSeconds / (60 * 60)), PLURALIZE((iNumSeconds / 60) % 60));
	}
	else
	{
		estrPrintf(ppEString, "%d day%s %d hour%s", PLURALIZE(iNumSeconds / (60 * 60 * 24)), PLURALIZE((iNumSeconds / (60 * 60)) % 24));
	}
}

char *GetPrettyDurationString(U32 iNumSeconds)
{
	static char *pRetString = NULL;
	timeSecondsDurationToPrettyEString(iNumSeconds, &pRetString);
	return pRetString;
}



void timeSecondsDurationToShortEString(U32 iNumSeconds, char **ppEString)
{
	if (iNumSeconds == 0)
	{
		estrPrintf(ppEString, "00");
	}
	else if (iNumSeconds < 60)
	{
		estrPrintf(ppEString, "%ds", iNumSeconds);
	}
	else if (iNumSeconds < 60 * 60)
	{
		estrPrintf(ppEString, "%dm%ds", iNumSeconds / 60, iNumSeconds % 60);
	}
	else if (iNumSeconds < 60 * 60 * 24)
	{
		estrPrintf(ppEString, "%dh%dm", iNumSeconds / (60 * 60), (iNumSeconds / 60) % 60);
	}
	else
	{
		estrPrintf(ppEString, "%dd%dh", iNumSeconds / (60 * 60 * 24), (iNumSeconds / (60 * 60)) % 24);
	}
}


/*typedef struct _SYSTEMTIME {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;*/

static bool timeSystemFieldIsInRange (WORD check, WORD start, WORD end, SA_PARAM_NN_VALID bool *pbStartMatch, SA_PARAM_NN_VALID bool *pbEndMatched)
{
	if (check < start && !*pbStartMatch)
		return false;
	if (check > end && !*pbEndMatched)
		return false;

	if (start < check)
		*pbStartMatch = true;
	if (check < end)
		*pbEndMatched = true;
	return true;
}

bool timeSystemIsInRange (SYSTEMTIME * time, SYSTEMTIME *ptimeStart, SYSTEMTIME *ptimeEnd, SYSTEMTIME *flags)
{
	bool bMatchStart = false;
	bool bMatchEnd = false;

	if (flags->wDayOfWeek)
	{
		// this is a special case; does not fit into hierarchy with other conditions
		if (time->wDayOfWeek < ptimeStart->wDayOfWeek || time->wDayOfWeek > ptimeEnd->wDayOfWeek)
			return false;
	}

	if (flags->wYear)
	{
		if (!bMatchStart || !bMatchEnd)
			if (!timeSystemFieldIsInRange(time->wYear, ptimeStart->wYear, ptimeEnd->wYear, &bMatchStart, &bMatchEnd))
				return false;
	}

	if (flags->wMonth)
	{
		if (!bMatchStart || !bMatchEnd)
			if (!timeSystemFieldIsInRange(time->wMonth, ptimeStart->wMonth, ptimeEnd->wMonth, &bMatchStart, &bMatchEnd))
				return false;
	}

	if (flags->wDay)
	{
		if (!bMatchStart || !bMatchEnd)
			if (!timeSystemFieldIsInRange(time->wDay, ptimeStart->wDay, ptimeEnd->wDay, &bMatchStart, &bMatchEnd))
				return false;
	}

	bMatchStart = bMatchEnd = false;
	// Times are non-independent
	if (flags->wHour)
	{
		if (!bMatchStart || !bMatchEnd)
			if (!timeSystemFieldIsInRange(time->wHour, ptimeStart->wHour, ptimeEnd->wHour, &bMatchStart, &bMatchEnd))
				return false;
	}

	if (flags->wMinute)
	{
		if (!bMatchStart || !bMatchEnd)
			if (!timeSystemFieldIsInRange(time->wMinute, ptimeStart->wMinute, ptimeEnd->wMinute, &bMatchStart, &bMatchEnd))
				return false;
	}

	if (flags->wSecond)
	{
		if (!bMatchStart || !bMatchEnd)
			if (!timeSystemFieldIsInRange(time->wSecond, ptimeStart->wSecond, ptimeEnd->wSecond, &bMatchStart, &bMatchEnd))
				return false;
	}

	if (flags->wMilliseconds)
	{
		if (!bMatchStart || !bMatchEnd)
			if (!timeSystemFieldIsInRange(time->wMilliseconds, ptimeStart->wMilliseconds, ptimeEnd->wMilliseconds, &bMatchStart, &bMatchEnd))
				return false;
	}

	return true;
}

bool timeLocalIsInRange (U32 time, SYSTEMTIME *ptimeStart, SYSTEMTIME *ptimeEnd, SYSTEMTIME *flags)
{
	SYSTEMTIME systime;
	timerLocalSystemTimeFromSecondsSince2000(&systime, time);

	return timeSystemIsInRange (&systime, ptimeStart, ptimeEnd, flags);
}

bool timeUTCIsInRange (U32 time, SYSTEMTIME *ptimeStart, SYSTEMTIME *ptimeEnd)
{
	/*SYSTEMTIME systime;
	timerLocalSystemTimeFromSecondsSince2000(&systime, time);

	return timeSystemIsInRange (&systime, ptimeStart, ptimeEnd);*/
	// TODO?
	return false;
}

void clearSystemTimeStruct(SYSTEMTIME *time)
{
	time->wDayOfWeek = 0;
	time->wHour = time->wMinute = time->wSecond = time->wMilliseconds = 0;
	time->wDay = time->wMonth = time->wYear = 0;
}


U32 timePatchFileTimeToSecondsSince2000(U32 time)
{
	return time - MAGIC_SS2000_TO_FILETIME;
}

U32 timeSecondsSince2000ToPatchFileTime(U32 seconds)
{
	return seconds + MAGIC_SS2000_TO_FILETIME;
}

// Timing History functions - allows tracking arbitrary timing data for performance testing
#define incForBuffer(x, s) ((x)+1) % s

TimingHistory *timingHistoryCreate(int bufsize)
{
	TimingHistory *hist = calloc(1, sizeof(TimingHistory));
	int i;

	hist->buf = calloc(bufsize, sizeof(F32));
	hist->size = bufsize;

	for(i = 0; i < bufsize; ++i)
	{
		hist->buf[i] = 0.0;
	}

	hist->last = bufsize - 1;
	hist->count = 0;
	hist->timer = timerAlloc();
	timerStart(hist->timer);

	return hist;
}

void timingHistoryDestroy(TimingHistory *hist)
{
	free(hist->buf);
	timerFree(hist->timer);
	free(hist);
}

void timingHistoryClear(TimingHistory *hist)
{
	int i;

	if(!hist)
	{
		return;
	}
	
	for(i = 0; i < hist->size; ++i)
	{
		hist->buf[i] = 0.0;
	}

	hist->last = hist->size - 1;
	hist->count = 0;
}

void timingHistoryPush(TimingHistory *hist)
{
	PERFINFO_AUTO_START_FUNC();
	if(!hist)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	hist->last = incForBuffer(hist->last, hist->size);
	hist->buf[hist->last] = timerElapsed(hist->timer);

	if(hist->count < hist->size)
	{
		++hist->count;
	}
	PERFINFO_AUTO_STOP();
}

int timingHistoryMostInInterval(TimingHistory *hist, F32 interval)
{
	int i, j, last, size, max = 0;

	if(!hist)
	{
		return 0;
	}

	if(interval < 0.0)
	{
		return 0;
	}

	if(hist->count == 0)
	{
		return 0;
	}

	if(interval == 0.0)
	{
		return 1;
	}

	last = hist->last;
	size = hist->size;
	i = (last + size - hist->count + 1) % size;

	for(j = i; j != last; i = incForBuffer(i, size))
	{
		while(hist->buf[incForBuffer(j, size)] - hist->buf[i] <= interval && j != last)
		{
			j = incForBuffer(j, size);
		}

		if((j + size - i) % size + 1 > max)
		{
			max = (j + size - i) % size + 1;
		}
	}

	return max;
}

F32 timingHistoryAverageInInterval(TimingHistory *hist, F32 interval)
{
	unsigned int i, j, last, size, count = 0;
	U64 total = 0;
	PERFINFO_AUTO_START_FUNC();

	if(!hist)
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	if(interval < 0.0)
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	if(hist->count == 0)
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	if(interval == 0.0)
	{
		PERFINFO_AUTO_STOP();
		return 1;
	}

	last = hist->last;
	size = hist->size;
	i = (last + size - hist->count + 1) % size;

	for(j = i; j != last; i = incForBuffer(i, size))
	{
		while(hist->buf[incForBuffer(j, size)] - hist->buf[i] <= interval && j != last)
		{
			j = incForBuffer(j, size);
		}

		++count;
		total += (j + size - i) % size + 1;
	}

	PERFINFO_AUTO_STOP();
	return (F32)total/count;
}

F32 timingHistoryShortestForCount(TimingHistory *hist, int count)
{
	F32 interval;
	int i, size, last;

	if(!hist)
	{
		return 0.0;
	}

	if(count < 2)
	{
		return 0.0;
	}

	if(hist->count == 0)
	{
		return 0.0;
	}

	if(count > hist->count)
	{
		count = hist->count;
	}

	last = hist->last;
	size = hist->size;
	i = (last + size - hist->count + 1) % size;
	interval = hist->buf[last] - hist->buf[i];

	for(; i != (last + size - count + 1) % size; i = incForBuffer(i, size))
	{
		if(hist->buf[(i + count - 1) % size] - hist->buf[i] < interval)
		{
			interval = hist->buf[(i + count - 1) % size] - hist->buf[i];
		}
	}

	return interval;
}

F32 timingHistoryAverageForCount(TimingHistory *hist, int count)
{
	F32 total;
	int i, size, last;

	if(!hist)
	{
		return 0.0;
	}

	if(count < 2)
	{
		return 0.0;
	}

	if(hist->count == 0)
	{
		return 0.0;
	}

	if(count > hist->count)
	{
		count = hist->count;
	}

	last = hist->last;
	size = hist->size;
	i = (last + size - hist->count + 1) % size;
	total = hist->buf[last] - hist->buf[i];

	for(; i != (last + size - count + 1) % size; i = incForBuffer(i, size))
	{
		total += hist->buf[(i + count - 1) % size] - hist->buf[i];
	}

	return total/(hist->count - count + 1);
}

int timingHistoryInLastInterval(TimingHistory *hist, F32 interval)
{
	int i, last, size;

	if(!hist)
	{
		return 0;
	}

	if(interval < 0.0)
	{
		return 0;
	}

	if(hist->count == 0)
	{
		return 0;
	}

	if(interval == 0.0)
	{
		return 1;
	}

	last = hist->last;
	size = hist->size;
	i = (last + size - hist->count + 1) % size;

	while(hist->buf[last] - hist->buf[i] > interval)
	{
		i = incForBuffer(i, size);
	}

	return (last + size - i) % size + 1;
}

F32 timingHistoryForLastCount(TimingHistory *hist, int count)
{
	F32 interval;
	int i, last, size;

	if(!hist)
	{
		return 0.0;
	}

	if(count < 2)
	{
		return 0.0;
	}

	if(hist->count == 0)
	{
		return 0.0;
	}

	if(count > hist->count)
	{
		count = hist->count;
	}

	last = hist->last;
	size = hist->size;
	i = (last + size - count + 1) % size;
	interval = hist->buf[last] - hist->buf[i];

	return interval;
}

void timingHistoryDumpToFile(TimingHistory *hist, F32 interval, const char *filename)
{
	FILE *file = NULL;
	int start = 0;
	int end = 0;
	int curIdx = 0;
	F32 curTime = 0;
	unsigned int count = 0;

	if (!hist)
	{
		return;
	}

	if (interval < 0)
	{
		return;
	}

	file = fopen(filename, "wb");
	if (!file)
	{
		return;
	}

	if (hist->count < hist->size)
	{
		start = 0;
		end = hist->count - 1;
	}
	else
	{
		start = hist->last + 1;
		if (start >= hist->size)
		{
			start = 0;
		}
		end = hist->last;
	}

	curTime = floor(hist->buf[start]);
	curIdx = start;
	do
	{
		F32 timeEntry = 0;
		timeEntry = hist->buf[curIdx];

		if (timeEntry - curTime >= interval)
		{
			fprintf(file, "%u,%u\n", (unsigned int)curTime, count);
			count = 0;
			curTime += interval;
		}

		if (timeEntry - curTime < interval)
		{
			count++;
			curIdx++;
			if (curIdx >= hist->size)
			{
				curIdx = 0;
			}
		}
	} while (curIdx != end + 1);

	fprintf(file, "%u,%u\n", (unsigned int)curTime, count);

	fclose(file);
}

//////////////////////////////////////////////////////////////////////////
//
// Stardate conversion code for STO.
//
//////////////////////////////////////////////////////////////////////////

// Calculate stardates based loosely on the algorithm described here:
// http://trekguide.com/Stardates.htm#TNGcalculator
// Which assumes:
// 1) exactly 1000 stardates per year
// 2) stardate 0 is on May 25, 2322 at midnight
//
// To compute stardates we add 400 years to the current date, and then
// apply the stardate algorithm, since the game is set exactly 400 years
// from now.
#define STARDATE_SECONDS_PER_DAY (60*60*24)
#define STARDATE_SECONDS_PER_YEAR (STARDATE_SECONDS_PER_DAY*365)

// Stardate on Jan 1, 2409 at midnight
#define STARDATE_2409 86605.48f

static U32 secondsBeforeMonth[] = {
	0,								// Jan
	31 * STARDATE_SECONDS_PER_DAY,	// Feb
	59 * STARDATE_SECONDS_PER_DAY,	// Mar
	90 * STARDATE_SECONDS_PER_DAY,	// Apr
	120 * STARDATE_SECONDS_PER_DAY,	// May
	151 * STARDATE_SECONDS_PER_DAY,	// Jun
	181 * STARDATE_SECONDS_PER_DAY,	// Jul
	212 * STARDATE_SECONDS_PER_DAY,	// Aug
	243 * STARDATE_SECONDS_PER_DAY,	// Sep
	273 * STARDATE_SECONDS_PER_DAY,	// Oct
	304 * STARDATE_SECONDS_PER_DAY,	// Nov
	334 * STARDATE_SECONDS_PER_DAY	// Dec
};

static bool
stardate_isLeapYear(SYSTEMTIME *t)
{
	// XXX - Might want to replace this with a table lookup to get rid of all the mods.
	return ( ( ( t->wYear % 4 ) == 0 ) && ( ( t->wYear % 100 ) != 0 ) ) || ( ( t->wYear % 400 ) == 0 );
}

//
// return the fraction through the year that the given date is
//
static float
stardate_offsetInYear(SYSTEMTIME *t)
{
	U32 sec;
	U32 leapAdjust = 0;

	sec = secondsBeforeMonth[t->wMonth - 1] + ( ( t->wDay - 1 ) * STARDATE_SECONDS_PER_DAY ) + ( ( ( t->wHour * 60 ) + t->wMinute ) * 60 ) + t->wSecond;
	if ( stardate_isLeapYear(t) )
	{
		leapAdjust = STARDATE_SECONDS_PER_DAY;

		if ( t->wMonth >= 3 )
		{
			// if it is a leap year, and after February, then add an extra day
			sec += STARDATE_SECONDS_PER_DAY;
		}
	}

	// divide number of seconds into the year the date is by the number of seconds in a year
	// Note that in a leap year we add an extra day to the divisor, so the number of stardates
	// per real day is different in normal years and leap years.  This is just a hack to keep
	// a constant 1000 stardates per year.
	return ((float)sec) / ((float)(STARDATE_SECONDS_PER_YEAR + leapAdjust));
}

//
// Compute a Star Trek stardate from the given time.  Note that we
// add 400 to the year so that current times will be converted to game
// times.
//
float
timerStardateFromSecondsSince2000(U32 seconds)
{
	SYSTEMTIME t;
	float offset;
	U32 yearsFrom2009;
	float stardate;

	timerSystemTimeFromSecondsSince2000(&t,seconds);

	offset = stardate_offsetInYear(&t);

	yearsFrom2009 = t.wYear - 2009;

	stardate = STARDATE_2409 + ( ( yearsFrom2009 + offset ) * 1000 );

	return stardate;
}

//As currently written, this will be off by one hour for one day immediately after each
//daylight savings time switch.
U32 FindNextSS2000WhichMatchesLocalHourOfDay(U32 iCurTime, int iMilitaryTime)
{
	int iHours = iMilitaryTime / 100;
	int iMinutes = iMilitaryTime % 100;
	U32 iRetVal;
	SYSTEMTIME t = {0};
	bool bNeedTomorrow = false;

	timerLocalSystemTimeFromSecondsSince2000(&t, iCurTime);

	//it's later today than the specified time... we need tomorrow
	if (t.wHour > iHours || (t.wHour == iHours && t.wMinute >= iMinutes))
	{
		bNeedTomorrow = true;
	}

	t.wHour = iHours;
	t.wMinute = iMinutes;
	t.wSecond = 0;
	t.wMilliseconds = 0;


	iRetVal = timerSecondsSince2000FromLocalSystemTime(&t);

	return bNeedTomorrow ? iRetVal + SECONDS_PER_DAY : iRetVal;
}


		

U32 timerSecondsSince2000FromLocalSystemTime(SYSTEMTIME *t);

// Return true if printf() is disabled.
bool IsPrintfDisabled()
{
	devassert(sbDisablePrintf == true || sbDisablePrintf == false);
	return sbDisablePrintf;
}

// Disable printf if parameter is true, otherwise enable it.
void DisablePrintf(bool bDisable)
{
	devassert(bDisable == true || bDisable == false);
	sbDisablePrintf = bDisable;
}

static int disableCoarseTimer;
AUTO_CMD_INT(disableCoarseTimer, disableCoarseTimer);

void coarseTimerEnable(bool enable)
{
	disableCoarseTimer = !enable;
}

#define COARSE_TIMER_DEBUG

typedef struct CoarseTimerInstance CoarseTimerInstance;

typedef struct CoarseTimerInstance
{
	CoarseTimerInstance** children;
	CoarseTimerInstance* parent;
	const char* tag;
#ifdef COARSE_TIMER_DEBUG
	char* checkTag;
#endif
	const char* trivia;
	U32 time;
	U32 selfTime; //time minus accumulated time of all children, only calculated if you 
		//call coarseTimerRecursivelyCalculateSelfTimes
	U32 unfinished : 1;
} CoarseTimerInstance;

typedef struct CoarseTimerManager
{
	CoarseTimerInstance** allocList;

	CoarseTimerInstance* start;
	CoarseTimerInstance* cur;
	U32 threadId;
} CoarseTimerManager;

CoarseTimerManager* globalCoarseTimerManager;

int checkForGlobalManager(CoarseTimerManager** manager)
{
	if(globalCoarseTimerManager)
	{
		*manager = globalCoarseTimerManager;
		return true;
	}
	else
		return false;
}

void coarseTimerRecursivelyCalculateSelfTimes(CoarseTimerInstance *pParent, CoarseTimerInstance **ppOutLongestFoundTime)
{
	U32 iChildrenSumTime = 0;

	FOR_EACH_IN_EARRAY(pParent->children, CoarseTimerInstance, pChild)
	{
		coarseTimerRecursivelyCalculateSelfTimes(pChild, ppOutLongestFoundTime);
		iChildrenSumTime += pChild->time;
	}
	FOR_EACH_END;

	//vaguely possible due to a rounding error or something
	if (iChildrenSumTime > pParent->time)
	{
		pParent->selfTime = 0;
	}
	else
	{
		pParent->selfTime = pParent->time - iChildrenSumTime;
	}

	if (ppOutLongestFoundTime)
	{
		if (!(*ppOutLongestFoundTime) || pParent->selfTime > (*ppOutLongestFoundTime)->selfTime)
		{
			*ppOutLongestFoundTime = pParent;
		}
	}
}

void coarseTimerAddInstance_dbg(CoarseTimerManager* manager, const char* tag, const char* triviaStr MEM_DBG_PARMS)
{
	CoarseTimerInstance* timer;

	if(disableCoarseTimer)
		return;

	if(!manager && !checkForGlobalManager(&manager))
		return;

	PERFINFO_AUTO_START_FUNC();

	devassert(GetCurrentThreadId() == manager->threadId);

	if(eaSize(&manager->allocList) == 0)
	{
		int i;
		int count = 100;
		CoarseTimerInstance *instances = calloc(count, sizeof(CoarseTimerInstance));
		for(i=0; i<count; i++)
			eaPush(&manager->allocList, &instances[i]);
	}
	timer = eaPop(&manager->allocList);
	ZeroStruct(timer);

	timer->tag = tag;
#if defined(COARSE_TIMER_DEBUG) && defined(_FULLDEBUG)
	timer->checkTag = strdup(tag);
#endif
	timer->time = timeGetTime();
	timer->unfinished = 1;

	if(triviaStr)
		timer->trivia = triviaStr;

	if(manager->cur)
	{
		devassertmsgf(manager->start, "Can't have cur without start (tag: %s)", tag);
		eaPush(&manager->cur->children, timer);

		timer->parent = manager->cur;
		manager->cur = timer;
	}
	else
	{
		devassertmsgf(!manager->cur && !manager->start, "Can't have one of cur/start and not the other... (tag: %s)", tag);
		manager->cur = manager->start = timer;
	}

	PERFINFO_AUTO_STOP();
}

void coarseTimerStopInstance_dbg(CoarseTimerManager* manager, const char* tag, const char* triviaStr MEM_DBG_PARMS)
{
	CoarseTimerInstance* timer;


	if(disableCoarseTimer)
		return;

	if(!manager && !checkForGlobalManager(&manager))
		return;

	PERFINFO_AUTO_START_FUNC();

	devassert(GetCurrentThreadId() == manager->threadId);

	timer = manager->cur;

	if(!timer || timer->tag != tag)
	{
		devassertmsgf(timer && timer->tag == tag, "Timer stop for %s does not match current tag of %s", tag, timer->tag);
		// TODO: add mismatched timer recovery
		PERFINFO_AUTO_STOP();
		return;
	}

#if defined(COARSE_TIMER_DEBUG) && defined(_FULLDEBUG)
	devassert(!stricmp(timer->tag, timer->checkTag));
#endif

	timer->time = timeGetTime() - timer->time;
	devassert(timer->unfinished);
	timer->unfinished = 0;
	manager->cur = timer->parent;

	if(triviaStr)
	{
		devassertmsgf(!timer->trivia, "Can't specify a trivia string for both start and end (tag: %s)", tag);
		timer->trivia = triviaStr;
	}
	
	PERFINFO_AUTO_STOP();
}

void coarseTimerInstanceDestroy(CoarseTimerManager *manager, CoarseTimerInstance* timer)
{
#if defined(COARSE_TIMER_DEBUG) && defined(_FULLDEBUG)
	devassert(!stricmp(timer->tag, timer->checkTag));
	SAFE_FREE(timer->checkTag);
#endif
	FOR_EACH_IN_EARRAY(timer->children, CoarseTimerInstance, child)
	{
		coarseTimerInstanceDestroy(manager, child);
	}
	FOR_EACH_END;
	eaDestroy(&timer->children);
	eaPush(&manager->allocList, timer);
}

CoarseTimerManager* coarseTimerCreateManager(int setAsGlobal)
{
	CoarseTimerManager* manager;

	if(disableCoarseTimer)
		return NULL;

	manager = callocStruct(CoarseTimerManager);
	manager->threadId = GetCurrentThreadId();

	if(setAsGlobal)
	{
		devassertmsg(!globalCoarseTimerManager, "There can be only one... globalCoarseTimerManager");
		globalCoarseTimerManager = manager;
	}

	return manager;
}

static void coarseTimerClearEx(CoarseTimerManager* manager, S32 destroying);

void coarseTimerDestroyManager(CoarseTimerManager *manager)
{
	if (disableCoarseTimer)
		return;

	if (globalCoarseTimerManager == manager)
		globalCoarseTimerManager = NULL;

	coarseTimerClearEx(manager, 1);
	free(manager);
}

void coarseTimerClearGlobalManager(void)
{
	globalCoarseTimerManager = NULL;
}

void coarseTimerInstanceVerify(CoarseTimerInstance* timer)
{
	int i;

#if defined(COARSE_TIMER_DEBUG) && defined(_FULLDEBUG)
	devassert(!stricmp(timer->tag, timer->checkTag));
#endif

	for(i = 0; i < eaSize(&timer->children); i++)
		coarseTimerInstanceVerify(timer->children[i]);
}

void coarseTimerVerify(CoarseTimerManager* manager)
{
	if(!manager->start)
		return;

#if defined(COARSE_TIMER_DEBUG) && defined(_FULLDEBUG)
	PERFINFO_AUTO_START_FUNC();
	coarseTimerInstanceVerify(manager->start);
	PERFINFO_AUTO_STOP();
#endif
}

static void coarseTimerClearEx(CoarseTimerManager* manager, S32 destroying)
{
	if(disableCoarseTimer)
		return;

	if(!manager && !checkForGlobalManager(&manager))
		return;

	PERFINFO_AUTO_START_FUNC();

#if defined(COARSE_TIMER_DEBUG) && defined(_FULLDEBUG)
	coarseTimerVerify(manager);
#endif

	devassert(GetCurrentThreadId() == manager->threadId);

	if(!destroying)
	{
		devassertmsg(!manager->cur && (!manager->start || !manager->start->unfinished),
			"Clearing with unfinished timers");
	}

	if(manager->start)
		coarseTimerInstanceDestroy(manager, manager->start);
	manager->start = NULL;
	manager->cur = NULL;
	PERFINFO_AUTO_STOP();
}

void coarseTimerClear(CoarseTimerManager* manager)
{
	coarseTimerClearEx(manager, 0);
}

static int coarseTimerInstancePrune(CoarseTimerManager *manager, CoarseTimerInstance* timer, U32 pruneTime)
{
	int i;

	devassertmsgf(!timer->unfinished, "Found unfinished timer while pruning (tag: %s)", timer->tag);

	for(i = eaSize(&timer->children)-1; i >= 0; i--)
	{
		if(coarseTimerInstancePrune(manager, timer->children[i], pruneTime))
			coarseTimerInstanceDestroy(manager, eaRemove(&timer->children, i));
	}

	if(eaSize(&timer->children))
		return false;
	else if(timer->time > pruneTime)
		return false;
	else
		return true;
}

// Recursively prune all timers with less time than pruneTime
void coarseTimerPrune(CoarseTimerManager* manager, U32 pruneTime)
{
	if(!manager && !checkForGlobalManager(&manager))
		return;

	devassert(GetCurrentThreadId() == manager->threadId);

	devassertmsg(!manager->start->unfinished && !manager->cur,
		"Not sure why you'd ever want to prune inside an actual frame");
	coarseTimerInstancePrune(manager, manager->start, pruneTime);
}

static void coarseTimerInstancePrint(CoarseTimerInstance* timer, char** outStr, int depth)
{
	int i;

	devassertmsgf(!timer->unfinished, "Found unfinished timer while printing (tag: %s)", timer->tag);

	for(i = depth - 1; i >= 0; i--)
		estrConcat(outStr, "  ", 1);
	estrConcatf(outStr, "%s%u %s", timer->unfinished ? "*****" : "", timer->time, timer->tag);
	if(timer->trivia)
		estrConcatf(outStr, "(%s)", timer->trivia);
	estrConcat(outStr, "\n", 1);

	for(i = 0; i < eaSize(&timer->children); i++)
		coarseTimerInstancePrint(timer->children[i], outStr, depth+1);
}

void coarseTimerPrint(CoarseTimerManager* manager, char** outStr, char **outLongestItem)
{
	CoarseTimerInstance *pLongest = NULL;

	if(disableCoarseTimer)
		return;

#if defined(COARSE_TIMER_DEBUG) && defined(_FULLDEBUG)
	coarseTimerVerify(manager);
#endif

	devassert(GetCurrentThreadId() == manager->threadId);

	devassertmsg(!manager->start->unfinished && !manager->cur,
		"Not sure why you'd ever wanna do this inside an actual frame");
	coarseTimerInstancePrint(manager->start, outStr, 0);

	if (outLongestItem)
	{
		coarseTimerRecursivelyCalculateSelfTimes(manager->start, &pLongest);
		if (pLongest)
		{
			estrCopy2(outLongestItem, pLongest->tag);
			if (pLongest->trivia)
			{
				estrConcatf(outLongestItem, " (%s)", pLongest->trivia);
			}
		}
		else
		{
			estrCopy2(outLongestItem, "UNDEFINED");
		}
	}
}

static bool sbDontCountThisFrame = false;

void coarseTimerFrameCheck_DontCountThisFrame(void)
{
	sbDontCountThisFrame = true;
}

void coarseTimerFrameCheck(float fSlowTime, int iResetTime, int iFramesToSkipBeforeStarting, enumCoarseTimerFrameCheckWhatToDoOnSlowFlags eFlags,
	CoarseTimerFrameCheckCB pCB)
{
	static S64 siLastTime = 0;
	static U32 siNextOKAlertTime = 0;

	static int siCounter = 0;
	static CoarseTimerManager *spGlobalManager = NULL;

	if (siCounter < iFramesToSkipBeforeStarting)
	{
		siCounter++;
		return;
	}

	if ((isDevelopmentMode() || g_isContinuousBuilder) && !(eFlags & COARSE_DEVMODEALSO))
	{
		return;
	}

	if (!siLastTime)
	{
		siLastTime = timeGetTime();
		spGlobalManager = coarseTimerCreateManager(true);
		
	}
	else
	{
		S64 iNewTime = timeGetTime();
		S64 iElapsed = iNewTime - siLastTime;
		siLastTime = iNewTime;
		coarseTimerStopInstance(spGlobalManager, "frame");

		if (iElapsed > fSlowTime * 1000.0f && !sbDontCountThisFrame)
		{
			U32 iCurTime = timeSecondsSince2000();

			if (siNextOKAlertTime <= iCurTime)
			{
				char *pTimingString = NULL;
				char *pLongestItemName = NULL;

				siNextOKAlertTime = timeSecondsSince2000() + iResetTime;

				coarseTimerPrune(spGlobalManager, 0);
				coarseTimerPrint(spGlobalManager, &pTimingString, (eFlags & COARSE_ALERT) ? &pLongestItemName : NULL);

				if (eFlags & COARSE_ALERT)
				{
					char *pAlertKeyToUse;

					estrStackCreate(&pAlertKeyToUse);
					
					if (pLongestItemName)
					{
						estrMakeAllAlphaNumAndUnderscores(&pLongestItemName);
						estrTrimLeadingAndTrailingUnderscores(&pLongestItemName);
						string_toupper(pLongestItemName);
					}
					
					estrPrintf(&pAlertKeyToUse, "COARSE_SLOW_FRAME_%s", pLongestItemName ? pLongestItemName : "");
	
					WARNING_NETOPS_ALERT(pAlertKeyToUse, "%s had a %u ms frame. Coarse details: %s\n",
						GlobalTypeToName(GetAppGlobalType()), (U32)iElapsed, pTimingString);
					estrDestroy(&pAlertKeyToUse);
				}

				if (pCB)
				{
					pCB(iElapsed, pTimingString);
				}

				if (eFlags & COARSE_XPERF)
				{
					xperfDump("slowFrame");
				}

				estrDestroy(&pTimingString);
				estrDestroy(&pLongestItemName);
			}
		}
	}

	sbDontCountThisFrame = false;
	coarseTimerClear(spGlobalManager);
	coarseTimerAddInstance(spGlobalManager, "frame");
}


enumCASDHResult CoarseAutoStartStaticDefineHelper(PERFINFO_TYPE ***pppPerfInfos, const char **ppCharVar /*NOT AN ESTRING*/, int iVal,
	int *piMinVal, int *piMaxVal, StaticDefineInt *pStaticDefine, int *piOutPerfInfoSlotNum)
{
	
	if (!(*pppPerfInfos)) 
	{			
		DefineGetMinAndMaxInt(pStaticDefine, piMinVal, piMaxVal);								
		if (!(*piMinVal) && !(*piMaxVal) || (*piMaxVal) < (*piMinVal) || (*piMaxVal) - (*piMinVal) > MAX_COARSE_STATIC_DEFINE_SIZE) 
		{ 
			return CASDH_BADSTATICDEFINE;
		} 
		else
		{	
			(*pppPerfInfos) = calloc(sizeof(PERFINFO_TYPE*) * ((*piMaxVal) - (*piMinVal) + 2), 1);			
		}
	}	
		
	if (iVal >= (*piMinVal) && iVal <= (*piMaxVal)) 
	{		
		*piOutPerfInfoSlotNum = iVal - (*piMinVal);
		(*ppCharVar) = StaticDefineIntRevLookup(pStaticDefine, iVal); 
		if (!(*ppCharVar))
		{
			return CASDH_UNKNOWN;
		}
		else
		{
			return CASDH_SUCCESS;
		}
	}																  
	else 
	{ 	
		*piOutPerfInfoSlotNum = (*piMaxVal) - (*piMinVal) + 1;
		return CASDH_OUTOFRANGE;
	} 
}
	
//takes the ticks from timerCpuTicks64 and makes a pretty string, in seconds, milliseconds, or raw ticks
void timeTicksToPrettyEString(S64 iNumTicks, char **ppEString)
{
	float fSeconds = timerSeconds64(iNumTicks);

	if (fSeconds > 60.0f)
	{
		timeSecondsDurationToPrettyEString((int)fSeconds, ppEString);
		estrInsertf(ppEString, 0, "%I64d ticks - ", iNumTicks);
		return;
	}

	if (fSeconds >= 1.0f)
	{
		estrPrintf(ppEString, "%I64d ticks - %.2f seconds", iNumTicks, fSeconds);
		return;
	}

	if (fSeconds > 0.01f)
	{
		estrPrintf(ppEString, "%I64d ticks - %d mSecs", iNumTicks, (int)(fSeconds * 1000));
		return;
	}


	if (fSeconds > 0.001f)
	{
		estrPrintf(ppEString, "%I64d ticks - %.2f mSecs", iNumTicks, fSeconds * 1000);
		return;
	}

	if (fSeconds > 0.00001f)
	{
		estrPrintf(ppEString, "%I64d ticks - %d uSecs", iNumTicks, (int)(fSeconds * 1000000));
		return;
	}


	if (fSeconds > 0.000001f)
	{
		estrPrintf(ppEString, "%I64d ticks - %.2f uSecs", iNumTicks, fSeconds * 1000000);
		return;
	}

	estrPrintf(ppEString, "%I64d ticks (< 1 uSec)", iNumTicks);
}
