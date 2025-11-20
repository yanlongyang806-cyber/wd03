#pragma once

#include "memcheck.h"

#include "cmdparse.h"

GCC_SYSTEM

C_DECLARATIONS_BEGIN

typedef struct StashTableImp *StashTable;

typedef struct DbgWatch
{
	char *watch_name;
	void *data;
	ParseTable *table;
	bool needs_reset;
} DbgWatch;

typedef struct DbgState
{
	//Debug and Testing stuff

	F32		test1;  //a handful of floats to have around for off the cuff debugging
	F32		test2;
	F32		test3;
	F32		test4;
	F32		test5;
	F32		test6;
	F32		test7;
	F32		test8;
	F32		test9;
	F32		test10;
	int		testInt1;
	Vec3	testVec1;
	Vec3	testVec2;
	Vec3	testVec3;
	Vec3	testVec4;
	int		testDisplayValues;
	int		test1flicker;
	int		test2flicker;
	int		test3flicker;
	int		test4flicker;
	int		test5flicker;
	int		test6flicker;
	int		test7flicker;
	int		test8flicker;
	int		test9flicker;
	int		test10flicker;

	DbgWatch **active_debug_watches;
} DbgState;

extern DbgState dbg_state;

void dbgOncePerFrame(void);
void dbgAddWatchEx(SA_PARAM_NN_STR const char *watch_name, SA_PARAM_NN_VALID void *data, SA_PARAM_OP_VALID ParseTable *parse_table, bool needs_reset);
#define dbgAddWatch(watch_name, data, parse_table) dbgAddWatchEx(watch_name, data, parse_table, false)
#define dbgAddIntWatch(watch_name, var) dbgAddWatchEx(watch_name, &(var), NULL, false)
#define dbgAddClearedIntWatch(watch_name, var) dbgAddWatchEx(watch_name, &(var), NULL, true)
void dbgShowWatch(SA_PARAM_NN_STR const char *watch_name);
void dbgHideWatch(SA_PARAM_NN_STR const char *watch_name);

C_DECLARATIONS_END
