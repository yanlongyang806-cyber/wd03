#pragma once
GCC_SYSTEM

#include "stdtypes.h"

// enable these to enable more levels of timing information
// can also be enabled per file by defining these before including any header files
// #define PERFINFO_L2
// #define PERFINFO_L3

//#include "error.h"

C_DECLARATIONS_BEGIN

//----[ Begin AutoTimer Stuff ]--------------------------------------------------------------------

typedef struct PerfInfoStaticData	PerfInfoStaticData;
typedef struct Packet				Packet;
typedef struct PerfInfoGuard		PerfInfoGuard;

typedef struct AutoTimersPublicState {
	S32 connected;
	S32	enabled;
} AutoTimersPublicState;

extern AutoTimersPublicState autoTimersPublicState;

#if _XBOX && defined(PROFILE)
#define PIX_NAMED_EVENTS // Adds about 0.6ms/frame, but gives massively better information in PIX
#endif

#if _PS3
    #define GET_CPU_TICKS_64(x) SYS_TIMEBASE_GET(x)
#elif _XBOX
	#define GET_CPU_TICKS_64(x) x = __mftb();
#else
	U64 __rdtsc(void);  // from intrin.h, but intrin.h doesn't compile with our string defines
	#define GET_CPU_TICKS_64(x) x = __rdtsc();
#endif

#if _PS3
	#define DISABLE_PERFORMANCE_COUNTERS 1
#else
	#define DISABLE_PERFORMANCE_COUNTERS 0
#endif

#if DISABLE_PERFORMANCE_COUNTERS
	#define PERFINFO_BLOCK(x)
	#define PERFINFO_RUN_CONDITIONS		(0)
#else
	#define PERFINFO_BLOCK(x)			x
	#define PERFINFO_RUN_CONDITIONS		(autoTimersPublicState.enabled)
#endif

#define PERFINFO_RUN(x)					PERFINFO_BLOCK({if(PERFINFO_RUN_CONDITIONS){x}})
#define PERFINFO_STATEMENT(x)			STATEMENT(PERFINFO_RUN(x))

//- AUTO TIMER CORE --------------------------------------------------------------------------------

#define PERFINFO_AUTO_START_STATIC(locNameStatic, piStaticPtr, inc)								\
	PERFINFO_STATEMENT(																			\
		autoTimerBeginCPU(locNameStatic, piStaticPtr);											\
	)

#define PERFINFO_AUTO_START_STATIC_FORCE(locNameStatic, piStaticPtr, inc, td)						\
	STATEMENT(PERFINFO_BLOCK({																	\
		autoTimerBeginCPUWithTD(locNameStatic, piStaticPtr, td);											\
	}))

#define PERFINFO_AUTO_START_STATIC_GUARD(locNameStatic, piStaticPtr, inc, guard)				\
	PERFINFO_STATEMENT(																			\
		autoTimerBeginCPUGuard(locNameStatic, piStaticPtr, guard);								\
	)

#define PERFINFO_AUTO_START_STATIC_BLOCKING(locNameStatic, piStaticPtr, inc)					\
	PERFINFO_STATEMENT(																			\
		autoTimerBeginCPUBlocking(	locNameStatic, piStaticPtr);								\
	)

#define PERFINFO_AUTO_START(locNameParam,inc)													\
	PERFINFO_STATEMENT(																			\
		static PerfInfoStaticData* piStatic;													\
		static const char* locNameStatic = locNameParam;										\
		autoTimerBeginCPU(locNameStatic, &piStatic);											\
	)

#define PERFINFO_AUTO_START_GUARD(locNameParam,inc,guard)										\
	PERFINFO_STATEMENT(																			\
		static PerfInfoStaticData* piStatic;													\
		static const char* locNameStatic = locNameParam;										\
		autoTimerBeginCPUGuard(locNameStatic, &piStatic, guard);								\
	)

#define PERFINFO_AUTO_START_BLOCKING(locNameParam,inc)											\
	PERFINFO_STATEMENT(																			\
		static PerfInfoStaticData* piStatic;													\
		static const char* locNameStatic = locNameParam;										\
		autoTimerBeginCPUBlocking(locNameStatic, &piStatic);									\
	)

#define PERFINFO_ADD_FAKE_CPU(amount)		PERFINFO_STATEMENT(autoTimerAddFakeCPU(amount);)

#define PERFINFO_AUTO_STOP()				PERFINFO_STATEMENT(autoTimerEndCPU();)
#define PERFINFO_AUTO_STOP_GUARD(guard)		PERFINFO_STATEMENT(autoTimerEndCPUGuard(guard);)
#define PERFINFO_AUTO_STOP_CHECKED(locName)	PERFINFO_STATEMENT(autoTimerEndCPUCheckLocation(locName);)
#define PERFINFO_AUTO_STOP_CHECKED_FORCE(locName,td)	STATEMENT(PERFINFO_BLOCK(autoTimerEndCPUCheckLocationWithTD(locName,td);))

#define PERFINFO_AUTO_STOP_START(locName,inc)													\
	PERFINFO_STATEMENT(																			\
		PERFINFO_AUTO_STOP();																	\
		PERFINFO_AUTO_START(locName,inc);														\
	)

#define PERFINFO_AUTO_START_FUNC()				PERFINFO_AUTO_START(__FUNCTION__,1)
#define PERFINFO_AUTO_START_FUNC_GUARD(guard)	PERFINFO_AUTO_START_GUARD(__FUNCTION__,1,guard)
#define PERFINFO_AUTO_STOP_FUNC()				PERFINFO_AUTO_STOP_CHECKED(__FUNCTION__)

#ifdef PERFINFO_L2
	#define PERFINFO_AUTO_START_L2(locName,inc)			PERFINFO_AUTO_START(locName,inc)
	#define PERFINFO_AUTO_START_FUNC_L2()				PERFINFO_AUTO_START_FUNC()
	#define PERFINFO_AUTO_STOP_L2()						PERFINFO_AUTO_STOP()
	#define PERFINFO_AUTO_STOP_CHECKED_L2(locName)		PERFINFO_AUTO_STOP_CHECKED(locName)
	#define PERFINFO_AUTO_STOP_START_L2(locName,inc)	PERFINFO_AUTO_STOP_START(locName,inc)
	#define PERFINFO_AUTO_START_BLOCKING_L2(locName,inc)			PERFINFO_AUTO_START_BLOCKING(locName,inc)
	#define PERFINFO_AUTO_START_STATIC_BLOCKING_L2(locName,pi,inc)	PERFINFO_AUTO_START_STATIC_BLOCKING(locName,pi,inc)
#else
	#define PERFINFO_AUTO_START_L2(locName,inc)			{}
	#define PERFINFO_AUTO_START_FUNC_L2()				{}
	#define PERFINFO_AUTO_STOP_L2()						{}
	#define PERFINFO_AUTO_STOP_CHECKED_L2(locName)		{}
	#define PERFINFO_AUTO_STOP_START_L2(locName,inc)	{}
	#define PERFINFO_AUTO_START_BLOCKING_L2(locName,inc)			{}
	#define PERFINFO_AUTO_START_STATIC_BLOCKING_L2(locName,pi,inc)	{}
#endif

#ifdef PERFINFO_L3
	#define PERFINFO_AUTO_START_L3(locName,inc)			PERFINFO_AUTO_START(locName,inc)
	#define PERFINFO_AUTO_START_FUNC_L3()				PERFINFO_AUTO_START_FUNC()
	#define PERFINFO_AUTO_STOP_L3()						PERFINFO_AUTO_STOP()
	#define PERFINFO_AUTO_STOP_CHECKED_L3(locName)		PERFINFO_AUTO_STOP_CHECKED(locName)
	#define PERFINFO_AUTO_STOP_START_L3(locName,inc)	PERFINFO_AUTO_STOP_START(locName,inc)
#else
	#define PERFINFO_AUTO_START_L3(locName,inc)			{}
	#define PERFINFO_AUTO_START_FUNC_L3()				{}
	#define PERFINFO_AUTO_STOP_L3()						{}
	#define PERFINFO_AUTO_STOP_CHECKED_L3(locName)		{}
	#define PERFINFO_AUTO_STOP_START_L3(locName,inc)	{}
#endif

#define PERFINFO_AUTO_PIX_START(locNameStatic)
#define PERFINFO_AUTO_PIX_STOP()

#if _PS3
	#define timerBeginSection(x)
	#define timerEndSection()
#else
	__forceinline void timerBeginSection(const char *name)
	{
		#if defined(PROFILE) && defined(_XBOX) && !defined(PIX_NAMED_EVENTS)
			PIXBeginNamedEvent(0, name);
		#endif
	}

	__forceinline void timerEndSection(void)
	{
		#if defined(PROFILE) && defined(_XBOX) && !defined(PIX_NAMED_EVENTS)
			PIXEndNamedEvent();
		#endif
	}
#endif

#define PERFINFO_AUTO_START_PIX(locNameParam,inc)	\
	timerBeginSection(locNameParam);				\
	PERFINFO_AUTO_START(locNameParam,inc)

#define PERFINFO_AUTO_START_PIX_GUARD(locNameParam,inc,guard)	\
	timerBeginSection(locNameParam);							\
	PERFINFO_AUTO_START_GUARD(locNameParam,inc,guard)

#define PERFINFO_AUTO_STOP_PIX()			\
	PERFINFO_AUTO_STOP();					\
	timerEndSection();

#define PERFINFO_AUTO_STOP_PIX_GUARD(guard)		\
	PERFINFO_AUTO_STOP_GUARD(guard);			\
	timerEndSection();

#define PERFINFO_AUTO_START_FUNC_PIX()		\
	timerBeginSection(__FUNCTION__);		\
	PERFINFO_AUTO_START_FUNC()

#define PERFINFO_AUTO_START_FUNC_PIX_GUARD(guard)	\
	timerBeginSection(__FUNCTION__);				\
	PERFINFO_AUTO_START_FUNC_GUARD(guard)

#define PERFINFO_AUTO_STOP_FUNC_PIX()		\
	PERFINFO_AUTO_STOP_FUNC();				\
	timerEndSection();

#define PERFINFO_AUTO_STOP_CHECKED_PIX(locNameParam)	\
	PERFINFO_AUTO_STOP_CHECKED(locNameParam);			\
	timerEndSection();

//- MISC -------------------------------------------------------------------------------------------

#define START_MISC_COUNT_STATIC(startVal, locNameStatic, piStaticPtr)							\
	PERFINFO_STATEMENT(autoTimerBeginMisc(	startVal, locNameStatic, piStaticPtr);)

#define START_MISC_COUNT(startVal, locNameParam)												\
	PERFINFO_STATEMENT(																			\
		static PerfInfoStaticData* piStatic;													\
		static const char* locNameStatic = locNameParam;										\
		PERFINFO_RUN(autoTimerBeginMisc(startVal, locNameStatic, &piStatic);)					\
	)

#define STOP_MISC_COUNT(stopVal)																\
	PERFINFO_RUN(autoTimerEndMisc(stopVal);)

#define ADD_MISC_COUNT(val, locNameParam)														\
	START_MISC_COUNT(0, locNameParam);															\
	STOP_MISC_COUNT(val)

#ifdef PERFINFO_L2
	#define START_MISC_COUNT_STATIC_L2(startVal, locNameStatic, piStaticPtr)	START_MISC_COUNT_STATIC(startVal, locNameStatic, piStaticPtr)
	#define START_MISC_COUNT_L2(startVal, locNameParam)							START_MISC_COUNT(startVal, locNameParam)
	#define STOP_MISC_COUNT_L2(stopVal)											STOP_MISC_COUNT(stopVal)
	#define ADD_MISC_COUNT_L2(val, locNameParam)								ADD_MISC_COUNT(val, locNameParam)
#else
	#define START_MISC_COUNT_STATIC_L2(startVal, locNameStatic, piStaticPtr)	{}
	#define START_MISC_COUNT_L2(startVal, locNameParam)							{}
	#define STOP_MISC_COUNT_L2(stopVal)											{}
	#define ADD_MISC_COUNT_L2(val, locNameParam)								{}
#endif

#ifdef PERFINFO_L3
	#define START_MISC_COUNT_STATIC_L3(startVal, locNameStatic, piStaticPtr)	START_MISC_COUNT_STATIC(startVal, locNameStatic, piStaticPtr)
	#define START_MISC_COUNT_L3(startVal, locNameParam)							START_MISC_COUNT(startVal, locNameParam)
	#define STOP_MISC_COUNT_L3(stopVal)											STOP_MISC_COUNT(stopVal)
	#define ADD_MISC_COUNT_L3(val, locNameParam)								ADD_MISC_COUNT(val, locNameParam)
#else
	#define START_MISC_COUNT_STATIC_L3(startVal, locNameStatic, piStaticPtr)	{}
	#define START_MISC_COUNT_L3(startVal, locNameParam)							{}
	#define STOP_MISC_COUNT_L3(stopVal)											{}
	#define ADD_MISC_COUNT_L3(val, locNameParam)								{}
#endif

// Convenience struct for use with PERFINFO_AUTO_START_STATIC() in message handlers
typedef struct StaticCmdPerf {
	const char*			name;
	PERFINFO_TYPE*		pi;
} StaticCmdPerf;

//- BITS -------------------------------------------------------------------------------------------

#define START_BIT_COUNT_STATIC(pak, piStaticPtr, locNameStatic)									\
	PERFINFO_STATEMENT(autoTimerBeginBits(	pak, locNameStatic, piStaticPtr);)

#define START_BIT_COUNT(pak, locNameParam)														\
	PERFINFO_STATEMENT(																			\
		static PerfInfoStaticData* piStatic;													\
		static const char* locNameStatic = locNameParam;										\
		autoTimerBeginBits(	pak, locNameStatic, &piStatic);										\
	)

#define STOP_BIT_COUNT(pak)																		\
	PERFINFO_STATEMENT(autoTimerEndBits(pak);)

//--------------------------------------------------------------------------------------------------

// This block of functions is all to expose the timer functionality to an outside system, so we can have
// profiler-like functionality inside the app.  (Specifically for the gens, right now).  You should not
// run any of this in production mode
typedef struct TimingProfilerReport
{
	char const * pchTimerName;
	int iTime;
} TimingProfilerReport;

int timerGetPIOverBudget(int iBudget,TimingProfilerReport paResults[10],struct TimerThreadData * td,int iFrames);
struct TimerThreadData* timerGetCurrentThreadData(void);

typedef void timerConnectedCallback(bool bConnected);
void timerSetConnectedCallback(timerConnectedCallback);
struct TimerThreadData* timerMakeSpecialTD();

// Auto timer functions.
 
// Note: If you add a new timer function, you must also add it to AutoTimerData.

void 	__fastcall autoTimerAddFakeCPU(U64 amount);

void 	__fastcall autoTimerBeginCPU(	const char* locName,
										PerfInfoStaticData** piStatic);

void 	__fastcall autoTimerBeginCPUWithTD(	const char* locName,
										PerfInfoStaticData** piStatic,
										struct TimerThreadData *td);

void 	__fastcall autoTimerBeginCPUGuard(	const char* locName,
											PerfInfoStaticData** piStatic,
											PerfInfoGuard** guard);

void 	__fastcall autoTimerBeginCPUBlocking(	const char* locName,
												PerfInfoStaticData** piStatic);

void 	__fastcall autoTimerBeginCPUBlockingGuard(	const char* locName,
													PerfInfoStaticData** piStatic,
													PerfInfoGuard** guard);

void 	__fastcall autoTimerBeginBits(	Packet* pak,
										const char* locName,
										PerfInfoStaticData** piStatic);

void 	__fastcall autoTimerBeginMisc(	U64 startVal,
										const char* locName,
										PerfInfoStaticData** piStatic);

void 	__fastcall autoTimerEndCPU(void);

void 	__fastcall autoTimerEndCPUGuard(PerfInfoGuard** guard);

void 	__fastcall autoTimerEndCPUCheckLocation(const char* locName);

void 	__fastcall autoTimerEndCPUCheckLocationWithTD(const char* locName, struct TimerThreadData *td);

void 	__fastcall autoTimerEndBits(Packet* pak);

void 	__fastcall autoTimerEndMisc(U64 stopVal);

// Note: If you add a new timer function, you must also add it to AutoTimerData.

//----[ End AutoTimer Stuff ]-----------------------------------------------------------------------

C_DECLARATIONS_END
