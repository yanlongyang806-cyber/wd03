#ifndef NO_MEMCHECK_H

#ifndef _TIMING_H
#define _TIMING_H
GCC_SYSTEM

// ************************** Cryptic time systems **************************
//
// There are a variety of different time systems used by Cryptic code.  The following is a summary of them.
//
// Seconds Since 2000 (SS2000, secsince2000)  **IF IN DOUBT, USE THIS ONE**
// Specification: Integer number of complete seconds since 12:00 PM January 1, 2000 UTC, not including leap seconds.
// Derived from: system date-time
// Time zone: Always in UTC
// Precision: Second
// Accuracy: Accurate to system date-time
// Notes: This is the primary time system used by Cryptic, and it should be used for all new code for which it is suitable.
//        To convert to time_t manually, add 946684800.  In code, use timeSecondsSince2000ToPatchFileTime().
//        To convert to Excel serial date in Excel, use =SS200DATE/(60*60*24)+36526-7/24
// Warning: This may be unsafe to use for some gameplay calculations, because large date adjustments (for instance, coarse time synchronization)
//          will affect it.  Many game systems use it anyway, hoping that system time adjustments over 1 second will be rare.
//
// Cryptic timer (timers, timerAlloc)
// Specification: Unspecified, generally related to cycle count
// Derived from: In most cases, from Windows QueryPerformanceCounter(), which is derived in some undocumented way from the system date-time,
//               the CPU cycle count (rdtsc), hardware timer interrupts, and possibly other things.  On non-Windows platforms, it uses clock()
// Time zone: N/A
// Precision: Subsecond, generally sub-millisecond, generally the highest available other than raw cycle counds
// Accuracy: Generally fairly stable
// Notes: This is the preferred mechanism for achieving sub-second accuracy in timing calculations that require it, such as game systems, movement, etc.
//        This time standard has no direct calendar mapping, and will tend to drift slightly with respect to actual time, since it is never
//        adjusted for date/time changes.  If mapping to the system date-time is necessary, use milliseconds since 2000.
//
// Milliseconds Since 2000 (MsecsSince2000)
// Specification: Integer number of milliseconds since 12:00 PM January 1, 2000 UTC, not including leap seconds.
// Derived from: Windows "system time"
// Time zone: Always in UTC
// Precision: Millisecond representation, but on systems may not have full millisecond granularity
// Accuracy: Accurate to system date-time
// Warning: Like Seconds Since 2000, this is unsafe to use for gameplay calculations.  It's more unsafe because millisecond-range clock adjustments are very common
// on some systems.  In general, this function should not be used for gameplay calculations on the client, and only with care on the server.  If a mapping
// to calendar time is not needed, it is better to use a Cryptic timer.
//
// Windows "file time" (FILETIME)
// Specification: Integer number of 100-nanosecond intervals since 12:00 PM January 1, 1601 UTC, not including leap seconds.
// Derived from: system date-time
// Time zone: Always in UTC
// Precision: 100-nanosecond representation, but many sources of this data do not actually provide this granularity
// Accuracy: Accurate to system date-time
// Notes: Internal representation in the kernel of filesystem time metadata; actually filesystems generally don't store the full Windows file time resolution
//
// Windows "system time" (SYSTEMTIME)
// Specification: Decomposed time, with millisecond resolution, excluding leap seconds
// Derived from: system date-time
// Time zone: Arbitrary time zone, usually UTC or system local time zone
// Precision: millisecond, but on systems may not have full millisecond granularity
// Accuracy: Accurate to system date-time
// Notes: Some Cryptic code uses this representation to decompose times.
//
// time_t (C time, ctime, Unix time, unixtime)
// Specification: Integer number of complete seconds since 12:00 PM January 1, 1970 in the relevant time zone, not including leap seconds.
// Derived from: system date-time
// Time zone: Arbitrary time zone, usually UTC or system local time zone
// Precision: Secondly
// Accuracy: Accurate to system date-time
// Notes: Frequently used for cross-platform or Unix-compatible code, since this is the primary time standard on Unix systems
// Warning: Care should be taken when using time_t values to preserve time zone information, since they may be in an arbitrary time zone.  The time()
//          function returns them in UTC, which is safest.
//
// clock_t (C clock)
// Specification: Unspecified, floating point
// Derived from: Unspecified, frequently hardware timer interrupt
// Time zone: N/A
// Precision: Unspecified sub-second, CLOCKS_PER_SEC, frequently actual timing granularity in tens of milliseconds
// Accuracy: Accurate to system date-time
// Notes: This is the standard C mechanism for sub-second timing, and it is generally portable, but it is inferior in most ways to Cryptic timers or
//        Windows timeGetTime().  Use it when outside of the Cryptic engine when cross-platform portability is needed.
//
// timeGetTime (Windows Multimedia Timer)
// Specification: Unspecified
// Derived from: Unknown
// Time zone: N/A
// Precision: Millisecond representation, usually near-millisecond precision, but may have tens of milliseconds granularity on old systems
// Accuracy: Accurate for short time span difference measurements
// Notes: This time source is very popular because it is the most accurate sub-second Windows timer other than those based on clock cycle, and it's available
// on all modern versions of Windows, including Win 9x.  QPC-based timers tend to be more accurate, but they're generally slightly slower.  It wraps every
// ~50 days, and is not stable with respect to actual time.
//
// Windows QueryPerformanceCounter (QPC)
// See Cryptic timers, which wrap this.  It's better to use them, since they attempt to correct the "bad cpu" clock defect that QPC can experience.
//
// rdtsc (Intel cycle counter)
// Specification: Cycle count since CPU startup
// Derived from: N/A
// Time zone: N/A
// Precision: Exact cycle count
// Accuracy: Exact, subject to instruction scheduling/serialization effects
// Notes: This is the fastest and most precise timer available on Intel systems, but it can be dramatically unstable, and the underlying clock rate can vary.
//        It is generally only used for profiling.
// Warning: Due to the lack of stability, this should not be used for gameplay timing.  QPC-based mechanisms such as Cryptic timers should be used instead.
//
//
// ************************** Cryptic time/date formats **************************
//
// Cryptic unfortunately does not use a single unified date format, or even a single unified time zone specifier.  Different contexts use different date formats,
// generally based on historical considerations.
//
// Cryptic date formats are based either on UTC or on localtime.  Cryptic standard local time is Pacific Time, including DST, which is internationally known as
// Los Angeles time.  Cryptic systems always use this local time, even when they are located in another time zone (such as the datacenter in Boston).
//
// One limitation of most Cryptic date formats based on local time is that they are ambiguous one hour per year.  Specifically, in the fall adjustment from
// daylight time to standard time, the 2 AM hour occurs twice, consecutively.  All representations which do not include a timezone specifier that differentiates
// between daylight time and standard time are ambiguous.  This means that some of our systems dependent on time formats malfunction around this time.  Oh well.
//
// PAC time is a more recent introduction to allow for easier specifification of event times by designers. PAC times are considered to be generic Pacific Times
// as is in effect at Cryptic in California. If the given date/time is during standard time, it will be converted to UTC as UTC-8.  If the given date/time
// is during daylight savings, it will be converted to UTC as UTC-7. There is still some ambiguity during the changeover hour when
// the daylight savings begins or ends. PAC01:00:00 on the Sunday of the Spring change will map to the same UTC time as PAC02:00:00. For the fall change,
// PAC01:59:00 maps to a UTC time 61 minutes before PAC02:00:00.  There is no way to specify a UTC time in the intervening 60 minutes via a PAC time.
// Currently the only functions using PAC time are 'timeGetSecondsSince2000FromPACDateString' which uses the YYYY-MM-DD HH:MM:SS format and the
// 'timeMakePACDateStringFromSecondsSince2000' functions. The only sytem
// using it is the event system for specifying times in .event files. NOTE: The current conversion routine uses an explictly set up time zone structure that
// matches Pacific Time as it is defined in 2012 (changes on 2nd Sunday in March and 1st Sunday in November). If this changes, that function will need to be
// updated.
//
// The following is a list of common time formats that we use:
//
// Log dates
// Primary purpose: Logging
// Time zone: UTC
// Precision: 1 second
// Format: YYMMDD HH:MM:SS
// Description: These are used by all of the log system and things that use the log system, such as the ObjectDB.  Note that the reference in log files
//              is generally the originator of the log, not the Log Server, so it's common to see paradoxical or unsynched sequences of timestamps in logs
//              due to synchronization error.
//
// Gimme times
// Primary purpose: Human date input
// Time zone: Cryptic local time
// Precision: 1 second
// Format: MMDDYY HH:MM:SS (MM and SS are optional)
// Description: These are the primary format for inputting dates in local time for most systems where this is done, such as builders, Gimme, and the Patch System.
// 
// Basic Cryptic formatted date string
// Primary purpose: Human date output
// Time zone: unspecified, but almost always local time
// Precision: 1 second
// Format: YYYY-MM-DD HH:MM:SS
// Description: This is the most common date format for human consumption, but not generally accepted for input.
// It should be used with care because its timezone is ambiguous although the default treatment (timeGetSecondsSince2000FromDateString)
// converts it as an unambiguous UTC time. This is currently the only format that can convert as a PAC time.
//
// Seconds Since 2000
// Primary purpose: Raw date output
// Time zone: UTC
// Precision: 1 second
// Format: just a decimal-formatted integer
// Description: Occasionally, a system will output a raw SS2000 value as a decimal-formatted integer.  This has the advantage of being very simple and avoiding
// time zone issues, but it's hard for humans to work with quickly.
//
// Internet date formats
// The Cryptic codebase contains support for most popular date formats used on the Internet, and they are used in various places for compatibility with
// other systems.
//
//
// ************************** Fundamental basis of time in computers **************************
//
// The essential nature of time is unknown.  However, humans attempt to model and approximate it for practical purposes, generally using scientific principles.
//
// The primary basis of time for computational purposes is International Atomic Time.  It is maintained by a scientific process that attempts to maximize
// stability using globally-distributed high-precision atomic clocks.  These clocks are generally based on the oscillation of cesium fluorescence within a laser
// beam at very low temperatures.  Various mechanisms are used to compare, stabilize, and average these clocks, generally using orbital satellites.  The
// result of the averaging process is the IAT, and it is the closest approximation humans have to actual time.  Very roughly, the IAT probably has an uncertainty
// of about one nanosecond per day.
//
// For cultural reasons, it is desirable for time to be synchronized to an astronomical basis.  This is done by adding leap seconds to IAT, forming Universal
// Coordinated Time (UTC).  In the current system, leap seconds may be added during two specific intervals in every year, or they may not be added at all.
// The determination to add leap seconds is done by a scientific process based on astronomical observations.  As of this writing (2011-09-20), this has been
// done 34 times, meaning UTC is 34 seconds behind IAT.
//
// Currently, Cryptic gets its time information primarily from the Internet NTP network.  This is a stratum-based public network of servers that use the NTP
// protocol to synchronize, and construct a time average.  The two primary sources of data for NTP are atomic clocks that are directly synchronized to IAT,
// and servers running a low-cost GPS system.  (GPS satellites are accurately synchronized to IAT, and it is possible to synchronize to these satellites
// using an ordinary GPS receiver.)  The Cryptic NTP design is currently subject to some flux, but the general idea of it is that there is one server
// that synchronizes to a pool of public NTP servers, and the remainder of the machines on Cryptic networks synchronize from it, either directly or indirectly.
//
// Most Cryptic machines are Windows machines.  SNTP is the basic time synchronization mechanism Windows uses, but unfortunately it is not very good.  So
// most Cryptic servers are configured to use Microsoft's w32time NTP implementation.  Unfortunately, the implementation is crap, and even on a local LAN,
// it is generally incapable of subsecond accuracy.  Cryptic is currently testing a third-party NTP solution that is more accurate.  Independent of
// synchronization accuracy, methods differ in synchronization smoothness.  SNTP synchronization will leave a sharp discontinuity in time, which decent
// third-party NTP solutions will smooth out the synchronizations; high-quality NTP implementations do not have explicit synchronization, but continuously
// modify the clock tick rate to stay in synch at all times and avoid discontinuities or noticeable time dilation or contraction.
//
// Customer machine time synchronization is entirely up to the customers; they might be automatically asynchronized, manually synchronized, or not synchronized
// at all.  Generally, Cryptic code assumes the clock is basically stable, but all code must cope with the fact that their clock might be substantially off.
// Systems that depend upon long-term clock stability (such as movement) must compensate for clock instability internally.
//
// PC hardware, including servers, maintain a battery-backed quartz clock that provides basic timekeeping.  However, most modern operating systems, including
// Windows, do not use it as the system clock.  Instead, the system clock is based on counting periodic hardware-generated interrupts.
//
// Windows has the limitation that it does not understand leap seconds, and has no way of representing them, a flaw that is propagated into all Cryptic time
// systems.  The consequence of this is that every leap second will unavoidably cause some sort of aberration in time around the leap second event, depending
// on the time synchronization mechanism.  In most cases, Windows will simply increment the timer by a second on the next time update after the leap second,
// which means there will be an uncertainty of about a second during this time.  Better synchronization mechanisms will try to increase the length of seconds
// around the leap second event to avoid the discontinuity, but this isn't perfect, and can lead to weirdness when two systems are trying to stay in synch
// with each other (such as in movement).  Most good quality operating systems (not including Windows, obviously) intrinsically handle leap seconds, and
// there is no time discontinuity.  Even on these systems, though, many legacy applications do not understand leap seconds, and so the system will maintain
// a second time standard for their use, that either has a one-second discontinuity for the leap second, or rounds it out as described above.
//
//
// ************************** WARNING! **************************
//
// Do not use the C runtime functions for getting times from files, such as stat(), findfirstfind(), and similar.  The times they return are unreliable.

#include "stdtypes.h"
#include "error.h"

#include "timing_profiler.h"

#define MAGIC_SS2000_TO_FILETIME 946684800 

#if _PS3 && !SPU
#include <sys/time_util.h>
#endif

#if _XBOX
#include <ppcintrinsics.h>
#endif

#if !_XBOX && defined(PROFILE) && !_PS3
#include <d3d9.h>
#endif

#if _XBOX && defined(PROFILE)
#define PROFILE_PERF 0
#define PIX_NAMED_EVENTS // Adds about 0.6ms/frame, but gives massively better information in PIX
#else
#define PROFILE_PERF 0
#endif

#if _PS3

    #undef PROFILE_PERF

	#ifdef DISABLE_PERFORMANCE_COUNTERS
		#undef DISABLE_PERFORMANCE_COUNTERS
	#endif 
    #define DISABLE_PERFORMANCE_COUNTERS 1

#else
// enable these to enable more levels of timing information
// can also be enabled per file by defining these before including any header files
#if !defined(_XBOX)
// Need this enabled or various commands like timerRecordStart do nothing/we get no performance information
#define PERFINFO_L1
#endif
// #define PERFINFO_L2
// #define PERFINFO_L3
#endif

#if _XBOX && defined(PROFILE)

#ifndef max
	#define POP_MINMAX
#pragma push_macro("min")
#pragma push_macro("max")
	#undef max
	#undef min
#endif

#include <xtl.h>

#ifdef POP_MINMAX
#pragma pop_macro("min")
#pragma pop_macro("max")
#endif

#endif

C_DECLARATIONS_BEGIN

// Use to determine max timers at run-time
void timerSetMaxTimers(int numTimers);
void timerInit();

// Manage manually created timers
void timerStart(U32 timer);
void timerAdd(U32 timer,F32 seconds);
void timerPause(U32 timer);
void timerUnpause(U32 timer);
F32 timerElapsed(U32 timer);
F32 timerElapsedAndStart(U32 timer);
U32 timerAllocDbg(const char* fileName, int fileLine);
#define timerAlloc() timerAllocDbg(__FILE__, __LINE__)
void timerFree(U32 timer);
void timerPrintAll(void);

// Access the CPU timer
unsigned long timerCpuSpeed(void);
unsigned long timerCpuTicks(void); // SLOW
F32 timerSeconds(U32 dt); // convert ticks to seconds
U32 timerCpuMs(void); // (faster) - timeGetTime() but returns non-0.  wraps every 49 days

S64 timerCpuSpeed64(void);
S64 __fastcall timerCpuTicks64(void);
F32 timerSeconds64(S64 dt); // convert 64bit ticks to seconds
#define timerGetSecondsAsFloat() (timerSeconds64(timerCpuTicks64()))

U32 timerCpuSeconds(void); // seconds since startup
U64 timeGetCPUCyclesPerSecond(void); // theoretical cycles/s of this machine

// Control of auto timer recording and playing
void timerGetProfilerFileName(const char* input, char* output, size_t output_size, int makeDirs);
void timerRecordStart(const char *filename);
void timerRecordEnd(void);
void timerRecordPlay(const char *filename);


// General time functions
#if _PS3
#define WINTICKSPERSEC _CPS // 1000000
#define WINTICKSPERMSEC (_CPS/1000)
#else
#define WINTICKSPERSEC 10000000
#define WINTICKSPERMSEC 10000
#endif

// Convenient defines for human-scale times. Not clever; don't use for anything
// important like billing.
#define SECONDS(x) (x)
#define MINUTES(x) (SECONDS(x) * 60)
#define HOURS(x) (MINUTES(x) * 60)
#define DAYS(x) (HOURS(x) * 24)
#define WEEKS(x) (DAYS(x) * 7)

// This is IN UTC
void timeSecondsSince2000EnableCache(S32 enabled);
void timeSecondsSince2000Update(void);

//forces it to be calculated every time. Use in weird situations where there isn't a main loop running with utilitieslibOncePerFrame
U32 timeSecondsSince2000_ForceRecalc(void);
U32 timeSecondsSince2000(void);
S64 timeMsecsSince2000(void); // Does not work on XBOX (precision of 1.5seconds or something).  Do not call on client.  Use timerAlloc(), etc.

__forceinline static U32 timeDiffSecondsSince2000(U32 ss2k) {return ss2k - timeSecondsSince2000();}

// Converting between strings and times. ALL OF THESE ARE IN UTC except for the PAC functions
// These functions are for internal and back end use, and do not do proper localization
// Functions that print out any form of AM or PM should not be here, and should be with
// the localization functions

U32 timeGetSecondsSince2000FromGenericString(const char* sDateTimeStr);

#define timeMakeDateStringFromSecondsSince2000(datestr, seconds) timeMakeDateStringFromSecondsSince2000_s(SAFESTR(datestr), seconds)
#define timeGetDateStringFromSecondsSince2000(seconds) timeMakeDateStringFromSecondsSince2000_s(NULL,0,seconds)
char *timeMakeDateStringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds);

#define timeMakePACDateStringFromSecondsSince2000(datestr, seconds) timeMakePACDateStringFromSecondsSince2000_s(SAFESTR(datestr), seconds)
#define timeGetPACDateStringFromSecondsSince2000(seconds) timeMakePACDateStringFromSecondsSince2000_s(NULL,0,seconds)
char *timeMakePACDateStringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds);

#define timeMakeDateNoTimeStringFromSecondsSince2000(datestr, seconds) timeMakeDateNoTimeStringFromSecondsSince2000_s(SAFESTR(datestr), seconds)
#define timeGetDateNoTimeStringFromSecondsSince2000(seconds) timeMakeDateNoTimeStringFromSecondsSince2000_s(NULL,0,seconds)
char *timeMakeDateNoTimeStringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds);

#define timeMakeTimeStringFromSecondsSince2000(datestr, seconds) timeMakeTimeStringFromSecondsSince2000_s(SAFESTR(datestr), seconds)
#define timeGetTimeStringFromSecondsSince2000(seconds) timeMakeTimeStringFromSecondsSince2000_s(NULL,0,seconds)
char *timeMakeTimeStringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds);

#define timeMakeRFC822StringFromSecondsSince2000(datestr, seconds) timeMakeRFC822StringFromSecondsSince2000_s(SAFESTR(datestr), seconds)
#define timeGetRFC822StringFromSecondsSince2000(seconds) timeMakeRFC822StringFromSecondsSince2000_s(NULL,0,seconds)
char *timeMakeRFC822StringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds);

// Filename safe version of DateString. Still parseable by the same functions
#define timeMakeFilenameDateStringFromSecondsSince2000(datestr, seconds) timeMakeFilenameDateStringFromSecondsSince2000_s(SAFESTR(datestr), seconds)
#define timeGetFilenameDateStringFromSecondsSince2000(seconds) timeMakeFilenameDateStringFromSecondsSince2000_s(NULL,0,seconds)
char *timeMakeFilenameDateStringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds);

#define timeMakeFilenameLocalDateStringFromSecondsSince2000(datestr, seconds) timeMakeFilenameLocalDateStringFromSecondsSince2000_s(SAFESTR(datestr), seconds)
#define timeGetFilenameLocalDateStringFromSecondsSince2000(seconds) timeMakeFilenameLocalDateStringFromSecondsSince2000_s(NULL,0,seconds)
char *timeMakeFilenameLocalDateStringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds);

// Local timezone versions of above

#define timeMakeLocalDateStringFromSecondsSince2000(datestr, seconds) timeMakeLocalDateStringFromSecondsSince2000_s(SAFESTR(datestr), seconds)
#define timeGetLocalDateStringFromSecondsSince2000(seconds) timeMakeLocalDateStringFromSecondsSince2000_s(NULL,0,seconds)
char *timeMakeLocalDateStringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds);

char *timeMakeLocalIso8601StringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds);
#define timeMakeLocalIso8601StringFromSecondsSince2000(datestr, seconds) timeMakeLocalIso8601StringFromSecondsSince2000_s(SAFESTR(datestr), seconds)

#define timeMakeLocalDateNoTimeStringFromSecondsSince2000(datestr, seconds) timeMakeLocalDateNoTimeStringFromSecondsSince2000_s(SAFESTR(datestr), seconds)
#define timeGetLocalDateNoTimeStringFromSecondsSince2000(seconds) timeMakeLocalDateNoTimeStringFromSecondsSince2000_s(NULL,0,seconds)
char *timeMakeLocalDateNoTimeStringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds);

#define timeMakeLocalTimeStringFromSecondsSince2000(datestr, seconds) timeMakeLocalTimeStringFromSecondsSince2000_s(SAFESTR(datestr), seconds)
#define timeGetLocalTimeStringFromSecondsSince2000(seconds) timeMakeLocalTimeStringFromSecondsSince2000_s(NULL,0,seconds)
char *timeMakeLocalTimeStringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds);

// Current local time versions of above

#define timeMakeLocalDateString(datestr) timeMakeLocalDateString_s(SAFESTR(datestr))
#define timeGetLocalDateString() timeMakeLocalDateString_s(NULL,0)
#define timeMakeLocalDateStringGimme(datestr) timeMakeLocalDateStringGimme_s(SAFESTR(datestr))
char *timeMakeLocalDateString_s(char *datestr, size_t datestr_size);
char *timeMakeLocalDateStringGimme_s(char *datestr, size_t datestr_size);	// Format: MMDDYYHH:mm:ss

#define timeMakeLocalDateNoTimeString(datestr) timeMakeLocalDateNoTimeString_s(SAFESTR(datestr))
#define timeGetLocalDateNoTimeString() timeMakeLocalDateNoTimeString_s(NULL,0)
char *timeMakeLocalDateNoTimeString_s(char *datestr, size_t datestr_size);

#define timeMakeLocalTimeString(datestr) timeMakeLocalTimeString_s(SAFESTR(datestr))
#define timeGetLocalTimeString() timeMakeLocalTimeString_s(NULL,0)
char *timeMakeLocalTimeString_s(char *datestr, size_t datestr_size);

#define timeGetLocalTimeStringEx(bShowSeconds,b24Hour) timeMakeLocalTimeStringEx_s(NULL,0,bShowSeconds,b24Hour)
char *timeMakeLocalTimeStringEx_s(char *datestr, size_t datestr_size, bool bShowSeconds, bool b24Hour);

// Log date formatting, in form "YYMMDD HH:MM:SS"
// No local forms of these, because logs are always in UTC

#define timeMakeLogDateStringFromSecondsSince2000(datestr, seconds) timeMakeLogDateStringFromSecondsSince2000_s(SAFESTR(datestr), seconds)
#define timeGetLogDateStringFromSecondsSince2000(seconds) timeMakeLogDateStringFromSecondsSince2000_s(NULL,0, seconds)
char *timeMakeLogDateStringFromSecondsSince2000_s(char *datestr, size_t datestr_size, U32 seconds); // shows "YYMMDD HH:MM:SS" (for db log)

#define timeMakeLogDateString(datestr) timeMakeLogDateString_s(SAFESTR(datestr))
#define timeGetLogDateString() timeMakeLogDateString_s(NULL,0)
char *timeMakeLogDateString_s(char *datestr, size_t datestr_size); 

//does no safety/format checking.
U32 timeGetSecondsSince2000FromLogDateString(const char *pLogString);

//note that you should generally not use this function, as it makes time-zone type assumptions
//that are specifically needed by the build scripting system but are perhaps not generally useful
char *timeGetGimmeStringFromSecondsSince2000(U32 iSeconds);
char *timeGetLocalGimmeStringFromSecondsSince2000(U32 iSeconds);
//gets seconds from string of form MMDDYYHH{:MM{:SS}}
U32 timeGetSecondsSince2000FromGimmeString(const char *pString);
U32 timeGetSecondsSince2000FromLocalGimmeString(const char *pString);




// Converting between other time formats

U32 timeGetSecondsSince2000FromDateString(const char *s);
U32 timeGetSecondsSince2000FromLocalDateString(const char *s);
U32 timeGetSecondsSince2000FromPACDateString(const char *s);	// Adjusts from pacific time and considers the daylightsavingsness of the date being converted
U32 timeGetSecondsSince2000FromIso8601String(const char *s);

//parses a time of the format "Mon Apr 30 16:19:05 2007"
U32 timeGetSecondsSince2000FromSystemStyleString(char *s);
char *timeGetLocalSystemStyleStringFromSecondsSince2000(U32 iTime);

typedef struct _SYSTEMTIME SYSTEMTIME;
void timerSystemTimeFromSecondsSince2000(SYSTEMTIME *t, U32 seconds);
U32 timerSecondsSince2000FromSystemTime(SYSTEMTIME *t);
void timerLocalSystemTimeFromSecondsSince2000(SYSTEMTIME *t, U32 seconds);
void timerDaylightLocalSystemTimeFromSecondsSince2000(SYSTEMTIME *t, U32 seconds);
U32 timerSecondsSince2000FromLocalSystemTime(SYSTEMTIME *t);



time_t timeGetTimeFromDateString(const char *date, const char *time); // Time can be NULL

// Parse an HTTP date (RFC 2616).
U32 timeGetSecondsSince2000FromHttpDateString(const char *httpdate);

void timeSecondsGetHoursMinutesSeconds(S32 iSeconds, S32 pHMS[3], bool bUseFirstOnly);

#define timeMakeOffsetStringFromSeconds(offsetstr, seconds) timeMakeOffsetStringFromSeconds_s(SAFESTR(offsetstr), seconds)
char* timeMakeOffsetStringFromSeconds_s(char *offsetstr, size_t offsetstr_size, U32 seconds); // shows hours:mins:secs

__time32_t timeMakeLocalTimeFromSecondsSince2000(U32 seconds);
__time32_t timeMakeTimeFromSecondsSince2000(U32 seconds);

__time32_t timeMakeTimeStructFromSecondsSince2000(U32 seconds, struct tm *ptime);
__time32_t timeMakeLocalTimeStructFromSecondsSince2000(U32 seconds, struct tm *ptime);
__time32_t timeMakeDaylightLocalTimeStructFromSecondsSince2000(U32 seconds, struct tm *ptime);

U32 timeGetSecondsSince2000FromLocalTimeStruct(struct tm *ptime);
U32 timeGetSecondsSince2000FromTimeStruct(struct tm *ptime);

U32 timeGetSecondsSince2000FromWindowsTime32(__time32_t iTime);

//Floors the seconds. Converts 1:23:45 to 1:23:00
__time32_t timeClampTimeToMinute(__time32_t t);
//Floors the minutes and seconds. Converts 1:23:45 to 1:00:00
__time32_t timeClampTimeToHour(__time32_t t);
//Floors the hours, minutes, and seconds. Converts 1:23:45 to 0:00:00
__time32_t timeClampTimeToDay(__time32_t t);


S64 timerFileTimeFromSecondsSince2000(U32 seconds);
U32 timerSecondsSince2000FromFileTime(S64 iFileTime);

// Misc time-related functions
S32 timeLocalOffsetFromUTC(void); // returns the offset in seconds of the local timezone to utc
S32 timeDaylightLocalOffsetFromUTC(U32 seconds); // returns the offset in seconds of the local timezone to utc for the given time
void timeLocalOffsetStringFromUTC(char *offsetstr, size_t offsetstr_size); // returns the ISO 8601-format offset the local timezone to utc
S64 timeY2KOffset(void);
int dayOfWeek(int year, int month, int day);
int timeDaysInMonth(int month, int year);
U32 timeClampSecondsSince2000ToHour(U32 seconds, int rounded);
U32 timeClampSecondsSince2000ToMinutes(U32 seconds, U32 minutes); // Must be a divisor of 60

// Returns if the time (in seconds) is within the start and end time, as restricted by the flags
// Date (year-month-day) and times (hour-min-seconds-milliseconds) are independent of each other, 
// and Day-of-the-Week is separate from both (and Sunday = 0, Saturday = 6)
bool timeSystemIsInRange (SYSTEMTIME * time, SYSTEMTIME *ptimeStart, SYSTEMTIME *ptimeEnd, SYSTEMTIME *flags);
bool timeLocalIsInRange (U32 time, SYSTEMTIME *ptimeStart, SYSTEMTIME *ptimeEnd, SYSTEMTIME *flags);
void clearSystemTimeStruct(SYSTEMTIME *time);

// client uses these functions to loosely syncronize with server for task timing
extern int timing_server_offset; // Difference between local UTC and remote UTC
void timeSetServerDelta(U32 servertime);
U32 timeServerSecondsSince2000(void); // uses server delta & local time

U32 timeLocalSecondsToServerSeconds(U32 localSeconds);
U32 timeServerSecondsToLocalSeconds(U32 serverSeconds);

//given a starting and ending time in secssince2000, and a desired minimum number of intermediate points,
//picks a similar number of "logical" points between those times, and gives them "logical" names.
//
//For instance, if you provide an 8-day span and ask for 6 tick points, you'll get 
//"Mon", "Tue", "Wed"
//
//(times are all local)
//
//If you provide an hour span and ask for 3 tick points, you'll get
//5:00, 5:15, 5:30" etc.
typedef struct
{
	char name[128];
	U32 iTime;
} NamedTime;


void timeGetLogicallyNamedIntervals(U32 iStartingTime, U32 iEndingTime, int iMinPoints,
	NamedTime ***pppOutNames);

// stardate time conversion for STO
float timerStardateFromSecondsSince2000(U32 seconds);

// FrameLockedTimer.

typedef struct FrameLockedTimer FrameLockedTimer;
extern FrameLockedTimer* ulFLT;
extern S64 ulAbsTime;

#define ABS_TIME_PER_SEC				3000
#define ABS_TIME						ulAbsTime
#define ABS_TIME_SINCE(cpuTicks)		((cpuTicks) ? (ABS_TIME - (cpuTicks)) : UINT_MAX)
#define ABS_TIME_TO_SEC(cpuTicks)		((cpuTicks)/(F32)ABS_TIME_PER_SEC)
#define SEC_TO_ABS_TIME(sec)			((sec) * ABS_TIME_PER_SEC)
#define ABS_TIME_PASSED(t, dt)			(t && ABS_TIME_SINCE(t)>SEC_TO_ABS_TIME(dt))

void	frameLockedTimerCreate(	FrameLockedTimer** timerOut,
								U32 ticksPerSecond,
								U32	ticksPerProcess);

void	frameLockedTimerSetFixedRate(	FrameLockedTimer* timer,
										F32 secondsPerFrame);

void	frameLockedTimerDestroy(FrameLockedTimer** timerInOut);

void	frameLockedTimerStartNewFrame(FrameLockedTimer* timer, F32 timeStepScale);

void	frameLockedTimerSetCurProcesses(FrameLockedTimer* timer,
										U32 processes);

void	frameLockedTimerGetTotalFrames(	const FrameLockedTimer* timer,
										U32* totalFramesOut);

void	frameLockedTimerGetCurTimes(const FrameLockedTimer* timer,
									F32* curFrameSecondsOut,
									U32* curMillisecondsOut,
									U32* deltaMillisecondsOut);

void	frameLockedTimerGetCurTimesReal(const FrameLockedTimer* timer,
										F32* curFrameSecondsOut);

void	frameLockedTimerGetPrevTimes(	const FrameLockedTimer* timer,
										F32* curFrameSecondsOut,
										U32* curMillisecondsOut,
										U32* deltaMillisecondsOut);

void	frameLockedTimerGetPrevTimesReal(	const FrameLockedTimer* timer,
											F32* curFrameSecondsOut);

void	frameLockedTimerGetProcesses(	const FrameLockedTimer* timer,
										U32* curProcessesOut,
										U32* curProcessesDeltaOut,
										U32* prevProcessesOut,
										U32* prevDeltaProcessesOut);
										
void	frameLockedTimerGetProcessRatio(const FrameLockedTimer* timer,
										F32* curProcessRatioOut,
										F32* prevProcessRatioOut);

void	frameLockedTimerGetCurSeconds(	const FrameLockedTimer* timer,
										F32* secondsSoFarOut);

void	frameLockedTimerGetFrameTicks(	const FrameLockedTimer* timer,
										U32* totalTicksOut);

U64		microsecondsSince1601(void);


//converts a number of seconds into a pretty string (ie, "5 minutes 15 seconds" or "10 days 3 hours"),
//puts in estring
void timeSecondsDurationToPrettyEString(U32 iNumSeconds, char **ppEString);

//converts a number of seconds into an abbreviated string (ie, "5m15s" or "10d3h"),
//puts in estring
void timeSecondsDurationToShortEString(U32 iNumSeconds, char **ppEString);

char *GetPrettyDurationString(U32 iNumSeconds);

//takes the ticks from timerCpuTicks64 and makes a pretty string. It will have both the raw tick count
//and a converted time
void timeTicksToPrettyEString(S64 iNumTicks, char **ppEString);


U32 timePatchFileTimeToSecondsSince2000(U32 time);
U32 timeSecondsSince2000ToPatchFileTime(U32 time);

#if !PLATFORM_CONSOLE
typedef struct _FILETIME FILETIME;

U32 timeSecondsSince2000FromFileTime(FILETIME *pFileTime);
#endif

#if PLATFORM_CONSOLE
#define timeGetTime() GetTickCount()
#endif

typedef int (*VprintfFunc)(const char *format, va_list argptr);
void setVprintfFunc(VprintfFunc func);

// Timing History functions -- allows tracking arbitrary timing data for simple performance testing
typedef struct TimingHistory
{
	F32 *buf;
	int size;
	int last;
	int count;
	int timer;
} TimingHistory;

TimingHistory *timingHistoryCreate(int bufsize);
void timingHistoryDestroy(TimingHistory *hist);
void timingHistoryClear(TimingHistory *hist);
void timingHistoryPush(TimingHistory *hist);
int timingHistoryMostInInterval(TimingHistory *hist, F32 interval);
F32 timingHistoryAverageInInterval(TimingHistory *hist, F32 interval);
F32 timingHistoryShortestForCount(TimingHistory *hist, int count);
F32 timingHistoryAverageForCount(TimingHistory *hist, int count);
int timingHistoryInLastInterval(TimingHistory *hist, F32 interval);
F32 timingHistoryForLastCount(TimingHistory *hist, int count);
void timingHistoryDumpToFile(TimingHistory *hist, F32 interval, const char *filename);

//makes it easy to do things every day at the same time. Takes in a "time of day" (military time), 
//returns SS2000 of the next time that it will be that time in local time. Note that "900" means 9 a.m., 
//not 900 minutes or 900 seconds
//
//As currently written, this will be off by one hour for one day immediately after each
//daylight savings time switch.
U32 FindNextSS2000WhichMatchesLocalHourOfDay(U32 iCurTime /*SS2000*/, int iMilitaryTime);

// printf-disabling code is here because it hooks into the timed_printf mechanism.

// Return true if printf() is disabled.
bool IsPrintfDisabled(void);

// Disable printf if parameter is true, otherwise enable it.
void DisablePrintf(bool bDisable);

void printfEnterCS(void);
void printfLeaveCS(void);

extern int gbTimeStampAllPrintfs;
#define printf_NoTimeStamp(fmt, ...) { int __temp = gbTimeStampAllPrintfs; gbTimeStampAllPrintfs = false; printf(fmt, __VA_ARGS__); gbTimeStampAllPrintfs = __temp;}

//1 = "Jan", etc
char *GetMonthName(int iMonthNum);

// CoarseTimer
// Times in milliseconds so that it can hopefully be always on, has some dopey pruning
// and string printing functions for output
typedef struct CoarseTimerManager CoarseTimerManager;

CoarseTimerManager* coarseTimerCreateManager(int setAsGlobal);
void coarseTimerDestroyManager(CoarseTimerManager *manager);

void coarseTimerEnable(bool enable);

void coarseTimerPrune(SA_PARAM_NN_VALID CoarseTimerManager* manager, U32 pruneTime);
void coarseTimerClear(SA_PARAM_NN_VALID CoarseTimerManager* manager);

//prints a recursive full report in outStr, the name of the single longest item in outLongestItem
void coarseTimerPrint(SA_PARAM_NN_VALID CoarseTimerManager* manager, char** outStr, char **outLongestItem);

// These macros enforce string literals to make it hard to corrupt coarse timer memory
#define coarseTimerAddInstance(manager, tag) \
	coarseTimerAddInstance_dbg(manager, tag "", NULL MEM_DBG_PARMS_INIT)
#define coarseTimerStopInstance(manager, tag) \
	coarseTimerStopInstance_dbg(manager, tag "", NULL MEM_DBG_PARMS_INIT)

// Trivia must be string pooled (or otherwise you must ensure it lives longer than the timer instance)
#define coarseTimerAddInstanceWithTrivia(manager, tag, trivia) \
	coarseTimerAddInstance_dbg(manager, tag "", trivia MEM_DBG_PARMS_INIT)
#define coarseTimerStopInstanceWithTrivia(manager, tag, trivia) \
	coarseTimerStopInstance_dbg(manager, tag "", trivia MEM_DBG_PARMS_INIT)

// If you use these macros it's your responsibility to make sure the strings stay around long
// enough (probably needs to be indefinitely unless you're really confident)
#define coarseTimerAddInstanceStatic(manager, tag) \
	coarseTimerAddInstance_dbg(manager, tag, NULL MEM_DBG_PARMS_INIT)
#define coarseTimerStopInstanceStatic(manager, tag) \
	coarseTimerStopInstance_dbg(manager, tag, NULL MEM_DBG_PARMS_INIT)

void coarseTimerAddInstance_dbg(SA_PARAM_OP_VALID CoarseTimerManager* manager, SA_PARAM_NN_STR const char* tag, SA_PARAM_OP_STR const char* trivia MEM_DBG_PARMS);
void coarseTimerStopInstance_dbg(SA_PARAM_OP_VALID CoarseTimerManager* manager, SA_PARAM_NN_STR const char* tag, SA_PARAM_OP_STR const char* trivia MEM_DBG_PARMS);


#define COARSE_AUTO_START_FUNC() do { coarseTimerAddInstance(NULL, __FUNCTION__);  PERFINFO_AUTO_START_FUNC(); } while (0)
#define COARSE_AUTO_STOP_FUNC() do { PERFINFO_AUTO_STOP_FUNC(); coarseTimerStopInstance(NULL, __FUNCTION__); } while (0)

#define COARSE_AUTO_START(name) do { coarseTimerAddInstance(NULL, name); PERFINFO_AUTO_START(name, 1); } while (0)
#define COARSE_AUTO_STOP(name) do { PERFINFO_AUTO_STOP(); coarseTimerStopInstance(NULL, name); } while (0)

typedef enum enumCoarseTimerFrameCheckWhatToDoOnSlowFlags
{
	COARSE_ALERT = 1 << 0,
	COARSE_XPERF = 1 << 1,
	COARSE_DEVMODEALSO = 1 << 2, //also do alerts/whatever in dev mode, default is don't
} enumCoarseTimerFrameCheckWhatToDoOnSlowFlags;

typedef void (*CoarseTimerFrameCheckCB)(U32 iElapsed, char *pSummaryString);

void coarseTimerFrameCheck(float fSlowTime, int iResetTime, int iFramesToSkipBeforeStarting, enumCoarseTimerFrameCheckWhatToDoOnSlowFlags eFlags,
	CoarseTimerFrameCheckCB pCB);

void coarseTimerFrameCheck_DontCountThisFrame(void);

#define COARSE_WRAP(foo) do {COARSE_AUTO_START(#foo); PERFINFO_AUTO_START(#foo, 1); {foo;} PERFINFO_AUTO_STOP(); COARSE_AUTO_STOP(#foo);} while (0)
#define MAX_COARSE_STATIC_DEFINE_SIZE 4096

#define FILE_AND_LINE __FILE__ ":" STRINGIZE(__LINE__)

typedef enum enumCASDHResult
{
	CASDH_SUCCESS,
	CASDH_BADSTATICDEFINE,
	CASDH_UNKNOWN,
	CASDH_OUTOFRANGE,
} enumCASDHResult;


enumCASDHResult CoarseAutoStartStaticDefineHelper(PERFINFO_TYPE ***pppPerfInfos, const char **ppCharVar /*NOT AN ESTRING*/, int iVal,
	int *piMinVal, int *piMaxVal, StaticDefineInt *pStaticDefine, int *piOutPerfInfoSlotNum);



#define COARSE_AUTO_START_STATIC_DEFINE(charVarName, iVal, staticDefineName)  {									\
	static int iMinVal = 0; static int iMaxVal = 0; static PERFINFO_TYPE **sppPerfInfos = NULL;	int perfInfoSlotNum; enumCASDHResult eResult;\
	eResult = CoarseAutoStartStaticDefineHelper(&sppPerfInfos, &charVarName, iVal, &iMinVal, &iMaxVal, staticDefineName, &perfInfoSlotNum); \
	switch(eResult) {																					 \
		case CASDH_SUCCESS: PERFINFO_AUTO_START_STATIC(charVarName, &sppPerfInfos[perfInfoSlotNum], 1); break; \
		case CASDH_BADSTATICDEFINE: PERFINFO_AUTO_START("BadStaticDefine" FILE_AND_LINE, 1); charVarName = "BadStaticDefine" FILE_AND_LINE; break; \
		case CASDH_UNKNOWN: PERFINFO_AUTO_START_STATIC("UnknownSD" FILE_AND_LINE, &sppPerfInfos[perfInfoSlotNum], 1); charVarName = "UnknownSD" FILE_AND_LINE; break; \
		case CASDH_OUTOFRANGE: PERFINFO_AUTO_START_STATIC("OutOfRangeSD" FILE_AND_LINE, &sppPerfInfos[perfInfoSlotNum], 1); charVarName = "OutOfRangeSD" FILE_AND_LINE; break; } \
	coarseTimerAddInstanceStatic(NULL, charVarName); }


#define COARSE_AUTO_STOP_STATIC_DEFINE(charVarName) {coarseTimerStopInstanceStatic(NULL, charVarName); PERFINFO_AUTO_STOP(); }



// if you're the game client, you should be thinking 
// in server time, everyone else, what the clock says
#ifndef GAMECLIENT
#   define autoSecsSince2k() timeSecondsSince2000()
#else
#   define autoSecsSince2k() timeServerSecondsSince2000()
#endif


C_DECLARATIONS_END

// End mkproto
#endif

#endif
