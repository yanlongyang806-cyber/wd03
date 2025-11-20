/*
 * Simple CPU usage profiling module (Common)
 *
 * The goal here is to take basic CPU cycle counts for select GameServer threads and place them in a graph on a GameClient.
 * That way content creators can have an idea of what in their maps is taking up server CPU cycles.
 *
 * It should help answer these questions: "If the number of civilians in a large static zone is reduced from 500 to 200, what
 * is the impact on GameServer performance?"
 *
 * To enable, run this command as an AL9 player while logged into a GameServer: simpleCpuUsage 1
 *
 * Notes
 * 1) Only one player at a time can view the graph. The graph data is placed on that Entity->Player->SimpleCpuUsageData.
 *    The side effect is that if you enable it for yourself while someone else is viewing the graph, it will disappear for the other player.
 * 2) When simpleCpuUsage is 0, then no CPU usage data is collected at all and Entity->Player->SimpleCpuUsageData will be NULL (destroyed)
 */
#pragma once

#include "timing.h"

// When adding to SimpleCpuUsageThread, add an RGB color for it to gclSimpleCpuUsage.c: gclSimpleCpu_DrawFrames
AUTO_ENUM;
typedef enum SimpleCpuUsageThread
{
	// GSLBaseStates.c: gslRunning_BeginFrame to gslRunning_EndFrame
	SIMPLE_CPU_USAGE_THREAD_GAMESERVER_MAIN,

	// gslSendToClient.c: gslGeneralUpdateThreadMain
	SIMPLE_CPU_USAGE_THREAD_GAMESERVER_SENDTOCLIENT,

	// EntityMovementManagerBG.c: mmProcessingThreadMain
	SIMPLE_CPU_USAGE_THREAD_MMBG_MAIN,

	// WorldColl.c: wcThreadMain
	SIMPLE_CPU_USAGE_THREAD_WORLDCOLL_MAIN,

	// netSendThread.c: pktSendThread
	SIMPLE_CPU_USAGE_THREAD_PKTSEND,
} SimpleCpuUsageThread;
extern StaticDefineInt SimpleCpuUsageThreadEnum[];

AUTO_STRUCT AST_THREADSAFE_MEMPOOL;
typedef struct SimpleCpuThreadData
{
	SimpleCpuUsageThread thread;					AST(NAME(Thread))

	S64 s64TimerCpuTicks;							AST(NAME(TimerCpuTicks64))
} SimpleCpuThreadData;
extern ParseTable parse_SimpleCpuThreadData[];
#define TYPE_parse_SimpleCpuThreadData SimpleCpuThreadData

AUTO_STRUCT;
typedef struct SimpleCpuFrameData
{
	SimpleCpuThreadData **eaSimpleCpuThreadData;	AST(NAME(SimpleCpuThreadData))
} SimpleCpuFrameData;
extern ParseTable parse_SimpleCpuFrameData[];
#define TYPE_parse_SimpleCpuFrameData SimpleCpuFrameData

#define SIMPLE_CPU_DATA_MAX_FRAME_COUNT	900

AUTO_STRUCT;
typedef struct SimpleCpuData
{
	F32 fMaxCyclesFor30Fps;							AST(NAME(MaxCyclesFor30Fps))

	S32 iNextFrameIndex;							AST(NAME(NextFrameIndex))

	SimpleCpuFrameData **eaSimpleCpuFrameData;		AST(NAME(SimpleCpuFrameData))
} SimpleCpuData;
extern ParseTable parse_SimpleCpuData[];
#define TYPE_parse_SimpleCpuData SimpleCpuData

// Make this define 0 to make the clocking calls go away entirely
#define SIMPLE_CPU_ENABLED	1

// When this is 0, the clocking calls are made, but NOOP, because no one is capturing the frame data. Use the accessor functions below, please.
extern bool g_SimpleCpuEnabled;

// enables/disables the capturing of data. Any data pulled out with simpleCpu_CaptureFrames must be destroyed separately.
void simpleCpu_SetEnabled(bool enabled);

// determine if the capturing of data is enabled
bool simpleCpu_IsEnabled(void);

// Use the macro below instead of calling this directly
static __forceinline S64 simpleCpu_Ticks()
{
	return g_SimpleCpuEnabled ? timerCpuTicks64() : 0;
}
#if SIMPLE_CPU_ENABLED
	#define SIMPLE_CPU_DECLARE_TICKS(var)							S64 var
	#define SIMPLE_CPU_TICKS(var)									STATEMENT(var = simpleCpu_Ticks();)
#else
	#define SIMPLE_CPU_DECLARE_TICKS(var)
	#define SIMPLE_CPU_TICKS(var)
#endif

// Register CPU ticks with a thread enum. Use the macro below instead of calling this directly
void simpleCpu_ThreadClock(SimpleCpuUsageThread thread, S64 s64TimerCpuTicksStart, S64 s64TimerCpuTicksEnd);
#if SIMPLE_CPU_ENABLED
	#define SIMPLE_CPU_THREAD_CLOCK(thread, startvar, endvar)		STATEMENT(simpleCpu_ThreadClock(thread, startvar, endvar);)
#else
	#define SIMPLE_CPU_THREAD_CLOCK(thread, startvar, endvar)
#endif

// Capture the frames clocked since the last capture.
void simpleCpu_CaptureFrames(SimpleCpuThreadData ***peaSimpleCpuThreadDataOut);
