/***************************************************************************



***************************************************************************/

// These functions are used to register callbacks that run every X seconds.
// The functions take the precise time since the last call (or since it was added
// if this is the first call), and a user data pointer. Functions run in the
// order they were added, and the same function can be added multiple times.

#ifndef UTILS_TIMEDCALLBACK_H
#define UTILS_TIMEDCALLBACK_H
#pragma once
GCC_SYSTEM

typedef struct TimedCallback TimedCallback;

typedef void (*TimedCallbackFunc)(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData);

typedef struct TimedCallback
{
	TimedCallbackFunc callback;
	UserData userData;
	const char* name;

	union
	{
		F32 interval; //used by "normal" timedcallbacks
		U32 iSecsSince2000ToRunAt; //used by timed callbacks requested for a specific SS222
	};

	// This is used by the internal loop to count how long it's been since
	// this function was run. Don't mess with it.
	F32 intervalCount;

	// If true, this function will be removed after running.
	bool runOnce;

	// If true, time scaling is applied to the function.
	bool scale;

	// If true, this callback will be removed on the next tick.
	bool remove;
} TimedCallback;

// Called once by utilitiesLibStartup.
void TimedCallback_Startup(void);

// Register a function to run every 'interval' seconds.
SA_ORET_NN_VALID TimedCallback *TimedCallback_Add_dbg(SA_PARAM_NN_VALID TimedCallbackFunc callback, UserData userData, F32 interval, SA_PARAM_NN_STR const char* name);
#define TimedCallback_Add(callback, userData, interval) TimedCallback_Add_dbg(callback, userData, interval, "Add:"#callback)

// Run this function only once, this many seconds from now, with this data.
SA_ORET_NN_VALID TimedCallback *TimedCallback_Run_dbg(SA_PARAM_NN_VALID TimedCallbackFunc callback, UserData userData, F32 secondsFromNow, SA_PARAM_NN_STR const char* name);
#define TimedCallback_Run(callback, userData, secondsFromNow) TimedCallback_Run_dbg(callback, userData, secondsFromNow, "Run:"#callback)

// Run this command only once, this many seconds from now.  Assumes string is persistent
SA_ORET_NN_VALID TimedCallback *TimedCallback_RunCmdParse_dbg(SA_PARAM_NN_STR const char *command, F32 secondsFromNow, SA_PARAM_NN_STR const char* name);
#define TimedCallback_RunCmdParse(command, secondsFromNow) TimedCallback_RunCmdParse_dbg(command, secondsFromNow, "Cmd:"#command)

// Run this function only once, at this many seconds from the year 2000, with this data.
SA_ORET_NN_VALID TimedCallback *TimedCallback_RunAt_dbg(SA_PARAM_NN_VALID TimedCallbackFunc callback, UserData userData, U32 secondsSince2k, SA_PARAM_NN_STR const char* name);
#define TimedCallback_RunAt(callback, userData, secondsSince2k) TimedCallback_RunAt_dbg(callback, userData, secondsSince2k, "RunAt:"#callback)

//Run this function only once, at this time of day (ie, "1800 = 6 p.m.") local time, the next time it is that time of day
//
//Note that there's a tiny slop period, so if at 3:59:59 you call for something to be run at 4:00, it will actually run 24 hours and
//1 second in the future, not 1 second.
//
//If you want something to run every day at a given time, have each calling of it call this again.
SA_ORET_NN_VALID TimedCallback *TimedCallback_RunAtTimeOfDay_dbg(SA_PARAM_NN_VALID TimedCallbackFunc callback, UserData userData, U32 iTimeOfDay, SA_PARAM_NN_STR const char* name);
#define TimedCallback_RunAtTimeOfDay(callback, userData, iTimeOfDay) TimedCallback_RunAtTimeOfDay_dbg(callback, userData, iTimeOfDay, "RunAtTimeOfDay:"#callback)

// If callback is in the list of functions to run, flag it for removal, and
// return true. Otherwise, return false.
bool TimedCallback_Remove(SA_PRE_NN_VALID SA_POST_P_FREE TimedCallback *callback);

bool TimedCallback_RemoveByFunction(SA_PARAM_NN_VALID TimedCallbackFunc callback);

// Called by the main loop of whatever is running, with the time since the
// last call. If a function's time interval is shorter than the timestep,
// it will still only be called once (so an interval of 0 will get called
// every frame).
void TimedCallback_Tick(F32 timestep, F32 scale);

#endif