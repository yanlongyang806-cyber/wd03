#ifndef _RDRDEVICETRACE_H_
#define _RDRDEVICETRACE_H_
GCC_SYSTEM

#include "utils.h"
#include "memlog.h"

// Controls tracing renderer window and device events. Do not check with a value
// other than 1.
//
// 0 = Disable tracing.
// 1 = Tracing only critical events to the memlog (production code uses this).
// 2 = Trace events to OutputDebugString.
// 3 = Trace events to OutputDebugString, and include all frame timeline events in RT and MT.
//
// Use TRACE_DEVICE for general info about the device.
// Use TRACE_WINDOW for logging the device window specifically.
// Use TRACE_FRAME for logging frame events/info, as this is very high-volume.
#define TRACE_DEVICE_LEVEL 1

#if TRACE_DEVICE_LEVEL >= 2
// WARNING: Using printf here is very convenient for debugging windowing problems, but can
// deadlock the render thread and main thread during loading, so use with care!
//#define M_TRACE_DEVICE( rdr_device, FMT, ... )	printf( "[%u] "FMT"\n", (frame_count), __VA_ARGS__ )
// WARNING: Without a debugger attached, OutputDebugStringf uses printf. Please see above 
// caveat about deadlocks.
#define M_TRACE_DEVICE( frame_count, FMT, ... )	OutputDebugStringf( "[%u] "FMT"\n", (frame_count), __VA_ARGS__ )
#elif TRACE_DEVICE_LEVEL >= 1
#define M_TRACE_DEVICE( frame_count, FMT, ... )	memlog_printf( NULL, "[%u] "FMT"\n", (frame_count), __VA_ARGS__ )
#else
#define M_TRACE_DEVICE( frame_count, FMT, ... )
#endif

#define TRACE_DEVICE( DEV, FMT, ... ) M_TRACE_DEVICE( (DEV) ? (DEV)->frame_count : 0, FMT, __VA_ARGS__ )
#define TRACE_DEVICE_MT( DEV, FMT, ... ) M_TRACE_DEVICE( (DEV) ? (DEV)->frame_count_nonthread : 0, FMT, __VA_ARGS__ )
#define TRACE_WINDOW( FMT, ... ) TRACE_DEVICE(&device->device_base, FMT, __VA_ARGS__ )
#define TRACE_WINDOW_MT TRACE_DEVICE_MT


#define DEBUG_TRACE_FRAMES (TRACE_DEVICE_LEVEL >= 3) 
#if DEBUG_TRACE_FRAMES
#define TRACE_FRAME TRACE_DEVICE
#define TRACE_FRAME_MT TRACE_DEVICE_MT
#else
#define TRACE_FRAME( DEV, FMT, ... )
#define TRACE_FRAME_MT( DEV, FMT, ... )
#endif

#endif //_RDRDEVICETRACE_H_


