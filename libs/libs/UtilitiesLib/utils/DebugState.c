#include "DebugState.h"
#include "StashTable.h"
#include "earray.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

DbgState dbg_state;

Cmd dbg_cmds[] = {
#define PARSE_TEST_VAR(i) \
	{ 9, "test" #i, NULL, 0, NULL, "NONE", NULL, {{ NULL, MULTI_FLOAT, &dbg_state.test##i, sizeof(dbg_state.test##i) }}, 0,				\
						"temporary debugging params odds and ends" },		\
	{ 9, "test" #i "flicker",  NULL, 0, NULL, "NONE", NULL, {{ NULL, MULTI_INT, &dbg_state.test##i##flicker, sizeof(dbg_state.test##i##flicker) }}, 0,	\
						"temporary debugging params odds and ends" },
	PARSE_TEST_VAR(1)
	PARSE_TEST_VAR(2)
	PARSE_TEST_VAR(3)
	PARSE_TEST_VAR(4)
	PARSE_TEST_VAR(5)
	PARSE_TEST_VAR(6)
	PARSE_TEST_VAR(7)
	PARSE_TEST_VAR(8)
	PARSE_TEST_VAR(9)
	PARSE_TEST_VAR(10)
	{ 9, "testInt1",  NULL, 0, NULL, "NONE", NULL, { CMDINT(dbg_state.testInt1) }, 0,
						"temporary debugging params odds and ends" },
	{ 9, "testDisplay",  NULL, 0, NULL, "NONE", NULL, { CMDINT(dbg_state.testDisplayValues) },0,
						"temporary debugging params odds and ends" },
	{ 0 },
};

void dbgOncePerFrame(void)
{
	// Do flickering
#define DOFLICKER(var)	\
	if (dbg_state.test##var##flicker) dbg_state.test##var = (F32)!dbg_state.test##var;
	DOFLICKER(1);
	DOFLICKER(2);
	DOFLICKER(3);
	DOFLICKER(4);
	DOFLICKER(5);
	DOFLICKER(6);
	DOFLICKER(7);
	DOFLICKER(8);
	DOFLICKER(9);
	DOFLICKER(10);
}

AUTO_RUN;
void dbgRegisterCommands(void)
{
	cmdAddCmdArrayToList(&gGlobalCmdList,dbg_cmds);
}

StashTable all_debug_watches = NULL;

void dbgAddWatchEx(const char *watch_name, void *data, ParseTable *parse_table, bool needs_reset)
{
	DbgWatch *watch;
	StashElement element;

	if (!all_debug_watches)
		all_debug_watches = stashTableCreateWithStringKeys(128, StashDefault);

	if (stashFindPointer(all_debug_watches, watch_name, &watch))
		return;

	watch = calloc(1,sizeof(*watch));
	watch->data = data;
	watch->table = parse_table;
	watch->needs_reset = needs_reset;
	assert(stashAddPointerAndGetElement(all_debug_watches, watch_name, watch, false, &element));
	watch->watch_name = stashElementGetStringKey(element);
}

AUTO_COMMAND ACMD_NAME("show_dbg");
void dbgShowWatch(const char *watch_name ACMD_NAMELIST(all_debug_watches, STASHTABLE) )
{
	DbgWatch *watch;
	
	if (!all_debug_watches)
		return;

	if (stashFindPointer(all_debug_watches, watch_name, &watch))
		eaPushUnique(&dbg_state.active_debug_watches, watch);
}

AUTO_COMMAND ACMD_NAME("hide_dbg");
void dbgHideWatch(const char *watch_name ACMD_NAMELIST(all_debug_watches, STASHTABLE))
{
	DbgWatch *watch;

	if (!all_debug_watches)
		return;

	if (stashFindPointer(all_debug_watches, watch_name, &watch))
		eaFindAndRemove(&dbg_state.active_debug_watches, watch);
}
