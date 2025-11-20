#include "TestServerLua.h"
#include "crypt.h"
#include "earray.h"
#include "error.h"
#include "fileutil.h"
#include "luaInternals.h"
#include "luaScriptLib.h"
#include "luasocket.h"
#include "lxplib.h"
#include <math.h>
#include "mime.h"
#include "objTransactions.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "TestServerExpression.h"
#include "TestServerGlobal.h"
#include "TestServerHttp.h"
#include "TestServerIntegration.h"
#include "TestServerMetric.h"
#include "TestServerReport.h"
#include "TestServerSchedule.h"
#include "timing.h"
#include "utils.h"
#include "WTCmdPacket.h"

#include "TestServerLua_h_ast.h"

static LuaContext *gLuaContext = NULL;
static WorkerThread *gLuaThread = NULL;
static int gLuaTimer = 0;

static CRITICAL_SECTION cs_luaShared;
static TestScript **eaScripts = NULL;
static int lua_interrupt = 0;

static void TestServer_QueueScript_internal(const char *script, bool bImmediate, bool bRaw)
{
	TestScript *pScript = StructCreate(parse_TestScript);
	estrCopy2(&pScript->pchScript, script);
	pScript->bRaw = bRaw;
	pScript->eState = TSS_Queued;

	EnterCriticalSection(&cs_luaShared);
	if(bImmediate && eaSize(&eaScripts) > 0)
	{
		eaInsert(&eaScripts, pScript, 1);
	}
	else
	{
		eaPush(&eaScripts, pScript);
	}

	if(eaSize(&eaScripts) == 1)
	{
		lua_interrupt = 0;
		wtQueueCmd(gLuaThread, WT_CMD_EXECUTE_SCRIPT, NULL, 0);
	}
	LeaveCriticalSection(&cs_luaShared);
}

// lol blatant copy-paste of Lua's built-in print
GLUA_FUNC(print)
{
	int n = glua_argn();
	int i;
	char *pcPrintStr = NULL;

	lua_getglobal(_lua_, "tostring");
	for(i = 1; i <= n; i++)
	{
		const char *s;
		lua_pushvalue(_lua_, -1);  /* function to be called */
		lua_pushvalue(_lua_, i);   /* value to print */
		lua_call(_lua_, 1, 1);
		s = lua_tostring(_lua_, -1);  /* get result */
		if(s == NULL)
			return luaL_error(_lua_, LUA_QL("tostring") " must return a string to " LUA_QL("print"));
		if(i > 1)
			estrAppend2(&pcPrintStr, "\t");
		estrAppend2(&pcPrintStr, s);
		lua_pop(_lua_, 1);  /* pop result */
	}
	estrAppend2(&pcPrintStr, "\n");
	TestServer_ConsolePrintf("%s", pcPrintStr);
}
GLUA_END

GLUA_FUNC(time_print)
{
	int n = glua_argn();
	char timestr[100];
	struct tm tim = {0};

	timeMakeLocalTimeStructFromSecondsSince2000(timeSecondsSince2000(), &tim);
	strftime(SAFESTR(timestr), "%Y-%m-%d %H:%M:%S | ", &tim);
	TestServer_ConsolePrintf("%s", timestr);

	lua_getglobal(_lua_, "print");
	lua_insert(_lua_, lua_gettop(_lua_)-n);
	luaContextCall(GLUA_GET_CONTEXT(), n);
}
GLUA_END

GLUA_FUNC(indent_print)
{
	int n = glua_argn();
	int indent = glua_getInteger(1);
	int i;
	char *estr = NULL;

	for (i = 0; i < indent; i++)
	{
		estrConcatf(&estr, "\t"); 
	}

	if (estr)
	{
		TestServer_ConsolePrintf(estr);
		estrDestroy(&estr);
	}

	lua_getglobal(_lua_, "print");
	lua_insert(_lua_, lua_gettop(_lua_)-n+1);
	luaContextCall(GLUA_GET_CONTEXT(), n-1);
}
GLUA_END


GLUA_FUNC(sha_256)
{
	char buf[128];

	GLUA_ARG_COUNT_CHECK(sha_256, 1, 1);
	GLUA_ARG_CHECK(sha_256, 1, GLUA_TYPE_STRING, false, "string");

	cryptCalculateSHAHashWithHex(glua_getString(1), buf);
	glua_pushString(buf);
}
GLUA_END

GLUA_FUNC(encode_64)
{
	const unsigned char *str;
	char buf[256];

	str = glua_getString(1);
	encodeBase64String(str, strlen(str), SAFESTR(buf));
	glua_pushString(buf);
}
GLUA_END

GLUA_FUNC(decode_64)
{
	const char *str;
	char buf[256];

	str = glua_getString(1);
	buf[decodeBase64String(str, strlen(str), SAFESTR(buf))] = 0;
	glua_pushString(buf);
}
GLUA_END

GLUA_FUNC(decode_utf8)
{
	const char *str;
	unsigned int code = glua_getInteger(1);

	str = WideToUTF8CodepointConvert(code);
	glua_pushString(str);
}
GLUA_END

GLUA_FUNC(get_script_name)
{
	char *pchScript;

	GLUA_ARG_COUNT_CHECK(get_script_name, 0, 0);
	
	EnterCriticalSection(&cs_luaShared);
	pchScript = eaScripts[0]->pchScript;
	LeaveCriticalSection(&cs_luaShared);

	glua_pushString(pchScript);
}
GLUA_END

GLUA_FUNC(get_time)
{
	GLUA_ARG_COUNT_CHECK(get_time, 0, 0);

	glua_pushNumber(timerElapsed(gLuaTimer));
}
GLUA_END

static int siInGlobalAtomic = false;

GLUA_FUNC(done)
{
	GLUA_ARG_COUNT_CHECK(done, 0, 0);

	EnterCriticalSection(&cs_luaShared);
	eaScripts[0]->eState = TSS_Completed;
	LeaveCriticalSection(&cs_luaShared);

	if(siInGlobalAtomic)
	{
		siInGlobalAtomic = false;
		TestServer_GlobalAtomicEnd();
	}

	wtQueueMsg(gLuaThread, WT_CMD_SCRIPT_COMPLETE, NULL, 0);
	luaSuppressNextError();
	glua_error("TS.Completed");
}
GLUA_END

GLUA_FUNC(var_atomic_begin)
{
	GLUA_ARG_COUNT_CHECK(var_atomic_begin, 0, 0);
	
	if(!siInGlobalAtomic)
	{
		siInGlobalAtomic = true;
		TestServer_GlobalAtomicBegin();
	}
}
GLUA_END

GLUA_FUNC(var_atomic_end)
{
	GLUA_ARG_COUNT_CHECK(var_atomic_end, 0, 0);

	if(siInGlobalAtomic)
	{
		siInGlobalAtomic = false;
		TestServer_GlobalAtomicEnd();
	}
}
GLUA_END

GLUA_FUNC(var_eval)
{
	const char *name;
	const char *scope;
	TestServerGlobalType eType;
	float fResult;
	bool bResult;

	GLUA_ARG_COUNT_CHECK(var_eval, 2, 2);
	GLUA_ARG_CHECK(var_eval, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(var_eval, 2, GLUA_TYPE_STRING, false, "variable name");

	scope = glua_getString(1);
	name = glua_getString(2);

	TestServer_GlobalAtomicBegin();
	eType = TestServer_GetGlobalType(scope, name);

	if(eType == TSG_Expression)
	{
		if(TestServer_IsArithmeticExpression(scope, name) && TestServer_EvalArithmeticExpression(scope, name, TSE_Op_NoOp, &fResult))
		{
			glua_pushNumber(fResult);
		}
		else if(TestServer_IsBooleanExpression(scope, name) && TestServer_EvalBooleanExpression(scope, name, TSE_Op_NoOp, &bResult))
		{
			glua_pushBoolean(bResult);
		}
		else
		{
			glua_pushNil();
		}
	}
	else
	{
		glua_pushNil();
	}
	TestServer_GlobalAtomicEnd();
}
GLUA_END

GLUA_FUNC(var_get)
{
	const char *name;
	const char *scope;
	int pos;
	TestServerGlobalValueType eType;

	GLUA_ARG_COUNT_CHECK(var_get, 3, 3);
	GLUA_ARG_CHECK(var_get, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(var_get, 2, GLUA_TYPE_STRING, false, "variable name");
	GLUA_ARG_CHECK(var_get, 3, GLUA_TYPE_INT, false, "position");

	scope = glua_getString(1);
	name = glua_getString(2);
	pos = glua_getInteger(3);

	TestServer_GlobalAtomicBegin();
	eType = TestServer_GetGlobalValueType(scope, name, pos);

	switch(eType)
	{
	case TSGV_Integer:
		glua_pushInteger(TestServer_GetGlobal_Integer(scope, name, pos));
	xcase TSGV_Boolean:
		glua_pushBoolean(TestServer_GetGlobal_Boolean(scope, name, pos));
	xcase TSGV_Float:
		glua_pushNumber(TestServer_GetGlobal_Float(scope, name, pos));
	xcase TSGV_String:
		glua_pushString(TestServer_GetGlobal_String(scope, name, pos));
	xcase TSGV_Password:
		glua_pushString(TestServer_GetGlobal_Password(scope, name, pos));
	xcase TSGV_Global:
		glua_pushTable();
		glua_pushString(TestServer_GetGlobal_RefScope(scope, name, pos));
		glua_tblKey("scope");
		glua_pushString(TestServer_GetGlobal_RefName(scope, name, pos));
		glua_tblKey("name");
	xdefault:
		glua_pushNil();
	}
	TestServer_GlobalAtomicEnd();
}
GLUA_END

GLUA_FUNC(var_get_count)
{
	GLUA_ARG_COUNT_CHECK(var_get_count, 2, 2);
	GLUA_ARG_CHECK(var_get_count, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(var_get_count, 2, GLUA_TYPE_STRING, false, "variable name");

	glua_pushInteger(TestServer_GetGlobalValueCount(glua_getString(1), glua_getString(2)));
}
GLUA_END

GLUA_FUNC(var_get_type)
{
	int pos = 0;

	GLUA_ARG_COUNT_CHECK(var_get_type, 2, 3);
	GLUA_ARG_CHECK(var_get_type, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(var_get_type, 2, GLUA_TYPE_STRING, false, "variable name");
	GLUA_ARG_CHECK(var_get_type, 3, GLUA_TYPE_INT | GLUA_TYPE_NIL, true, "position");

	if(glua_argn() > 2 && glua_isInteger(3))
	{
		pos = glua_getInteger(3);
	}

	if(pos < 0)
	{
		glua_pushInteger(TestServer_GetGlobalType(glua_getString(1), glua_getString(2)));
	}
	else
	{
		glua_pushInteger(TestServer_GetGlobalValueType(glua_getString(1), glua_getString(2), pos));
	}
}
GLUA_END

GLUA_FUNC(var_get_label)
{
	GLUA_ARG_COUNT_CHECK(var_get_label, 3, 3);
	GLUA_ARG_CHECK(var_get_label, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(var_get_label, 2, GLUA_TYPE_STRING, false, "variable name");
	GLUA_ARG_CHECK(var_get_label, 3, GLUA_TYPE_INT, false, "position");

	glua_pushString(TestServer_GetGlobalValueLabel(glua_getString(1), glua_getString(2), glua_getInteger(3)));
}
GLUA_END

GLUA_FUNC(var_set)
{
	const char *name;
	const char *scope;
	int pos;
	TestServerGlobalValueType eType;

	GLUA_ARG_COUNT_CHECK(var_set, 4, 5);
	GLUA_ARG_CHECK(var_set, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(var_set, 2, GLUA_TYPE_STRING, false, "variable name");
	GLUA_ARG_CHECK(var_set, 3, GLUA_TYPE_INT, false, "position");
	GLUA_ARG_CHECK(var_set, 4, GLUA_TYPE_INT | GLUA_TYPE_BOOLEAN | GLUA_TYPE_NUMBER | GLUA_TYPE_STRING | GLUA_TYPE_TABLE | GLUA_TYPE_NIL, false, "value");
	GLUA_ARG_CHECK(var_set, 5, GLUA_TYPE_INT | GLUA_TYPE_NIL, true, "type");

	scope = glua_getString(1);
	name = glua_getString(2);
	pos = glua_getInteger(3);

	if(glua_argn() > 4 && glua_isInteger(5))
	{
		eType = glua_getInteger(5);
	}
	else
	{
		switch(glua_type(4))
		{
		case GLUA_TYPE_INT:
			eType = TSGV_Integer;
		xcase GLUA_TYPE_BOOLEAN:
			eType = TSGV_Boolean;
		xcase GLUA_TYPE_NUMBER:
			eType = TSGV_Float;
		xcase GLUA_TYPE_STRING:
			eType = TSGV_String;
		xcase GLUA_TYPE_TABLE:
			eType = TSGV_Global;
		xdefault:
			eType = TSGV_Unset;
			break;
		}
	}

	switch(eType)
	{
	case TSGV_Integer:
		TestServer_SetGlobal_Integer(scope, name, pos, glua_getInteger(4));
	xcase TSGV_Boolean:
		TestServer_SetGlobal_Boolean(scope, name, pos, glua_getBoolean(4));
	xcase TSGV_Float:
		TestServer_SetGlobal_Float(scope, name, pos, glua_getNumber(4));
	xcase TSGV_String:
		TestServer_SetGlobal_String(scope, name, pos, glua_getString(4));
	xcase TSGV_Password:
		TestServer_SetGlobal_Password(scope, name, pos, glua_getString(4));
	xcase TSGV_Global:
		{
			const char *subscope;
			const char *subname;

			glua_getTable(4);
			subscope = glua_getTable_str("scope");
			subname = glua_getTable_str("name");
			TestServer_SetGlobal_Ref(scope, name, pos, subscope, subname);
		}
	xcase TSGV_Unset:
		TestServer_ClearGlobalValue(scope, name, pos);
	xdefault:
		break;
	}
}
GLUA_END

GLUA_FUNC(var_insert)
{
	const char *name;
	const char *scope;
	int pos;
	TestServerGlobalValueType eType;

	GLUA_ARG_COUNT_CHECK(var_insert, 4, 5);
	GLUA_ARG_CHECK(var_insert, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(var_insert, 2, GLUA_TYPE_STRING, false, "variable name");
	GLUA_ARG_CHECK(var_insert, 3, GLUA_TYPE_INT, false, "position");
	GLUA_ARG_CHECK(var_insert, 4, GLUA_TYPE_INT | GLUA_TYPE_BOOLEAN | GLUA_TYPE_NUMBER | GLUA_TYPE_STRING | GLUA_TYPE_TABLE, false, "value");
	GLUA_ARG_CHECK(var_insert, 5, GLUA_TYPE_INT | GLUA_TYPE_NIL, true, "type");

	scope = glua_getString(1);
	name = glua_getString(2);
	pos = glua_getInteger(3);

	if(glua_argn() > 4 && glua_isInteger(5))
	{
		eType = glua_getInteger(5);
	}
	else
	{
		switch(glua_type(4))
		{
		case GLUA_TYPE_INT:
			eType = TSGV_Integer;
		xcase GLUA_TYPE_BOOLEAN:
			eType = TSGV_Boolean;
		xcase GLUA_TYPE_NUMBER:
			eType = TSGV_Float;
		xcase GLUA_TYPE_STRING:
			eType = TSGV_String;
		xcase GLUA_TYPE_TABLE:
			eType = TSGV_Global;
		xdefault:
			eType = TSGV_Unset;
			break;
		}
	}

	switch(eType)
	{
	case TSGV_Integer:
		TestServer_InsertGlobal_Integer(scope, name, pos, glua_getInteger(4));
	xcase TSGV_Boolean:
		TestServer_InsertGlobal_Boolean(scope, name, pos, glua_getBoolean(4));
	xcase TSGV_Float:
		TestServer_InsertGlobal_Float(scope, name, pos, glua_getNumber(4));
	xcase TSGV_String:
		TestServer_InsertGlobal_String(scope, name, pos, glua_getString(4));
	xcase TSGV_Password:
		TestServer_InsertGlobal_Password(scope, name, pos, glua_getString(4));
	xcase TSGV_Global:
		{
			const char *subscope;
			const char *subname;

			glua_getTable(4);
			subscope = glua_getTable_str("scope");
			subname = glua_getTable_str("name");
			TestServer_InsertGlobal_Ref(scope, name, pos, subscope, subname);
		}
	xdefault:
		break;
	}
}
GLUA_END

GLUA_FUNC(var_persist)
{
	bool bPersist = true;

	GLUA_ARG_COUNT_CHECK(var_persist, 2, 3);
	GLUA_ARG_CHECK(var_persist, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(var_persist, 2, GLUA_TYPE_STRING, false, "variable name");
	GLUA_ARG_CHECK(var_persist, 3, GLUA_TYPE_BOOLEAN, true, "persist");

	if(glua_argn() > 2)
	{
		bPersist = glua_getBoolean(3);
	}

	TestServer_PersistGlobal(glua_getString(1), glua_getString(2), bPersist);
}
GLUA_END

GLUA_FUNC(var_set_label)
{
	GLUA_ARG_COUNT_CHECK(var_set_label, 4, 4);
	GLUA_ARG_CHECK(var_set_label, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(var_set_label, 2, GLUA_TYPE_STRING, false, "variable name");
	GLUA_ARG_CHECK(var_set_label, 3, GLUA_TYPE_INT, false, "position");
	GLUA_ARG_CHECK(var_set_label, 4, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "label");

	TestServer_SetGlobalValueLabel(glua_getString(1), glua_getString(2), glua_getInteger(3), glua_getString(4));
}
GLUA_END

GLUA_FUNC(var_clear)
{
	int pos = -1;

	GLUA_ARG_COUNT_CHECK(var_clear, 2, 3);
	GLUA_ARG_CHECK(var_clear, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(var_clear, 2, GLUA_TYPE_STRING, false, "variable name");
	GLUA_ARG_CHECK(var_clear, 3, GLUA_TYPE_INT, true, "position");

	if(glua_argn() > 2)
	{
		pos = glua_getInteger(3);
	}

	TestServer_ClearGlobalValue(glua_getString(1), glua_getString(2), pos);
}
GLUA_END

GLUA_FUNC(set_exp_add)
{
	GLUA_ARG_COUNT_CHECK(set_exp_var_add, 2, 2);
	GLUA_ARG_CHECK(set_exp_var_add, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(set_exp_var_add, 2, GLUA_TYPE_STRING, false, "expression name");

	TestServer_NewExpression(glua_getString(1), glua_getString(2), TSE_Op_Add);
}
GLUA_END

GLUA_FUNC(set_exp_sub)
{
	GLUA_ARG_COUNT_CHECK(set_exp_var_sub, 2, 2);
	GLUA_ARG_CHECK(set_exp_var_sub, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(set_exp_var_sub, 2, GLUA_TYPE_STRING, false, "expression name");

	TestServer_NewExpression(glua_getString(1), glua_getString(2), TSE_Op_Sub);
}
GLUA_END

GLUA_FUNC(set_exp_mul)
{
	GLUA_ARG_COUNT_CHECK(set_exp_var_mul, 2, 2);
	GLUA_ARG_CHECK(set_exp_var_mul, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(set_exp_var_mul, 2, GLUA_TYPE_STRING, false, "expression name");

	TestServer_NewExpression(glua_getString(1), glua_getString(2), TSE_Op_Mul);
}
GLUA_END

GLUA_FUNC(set_exp_div)
{
	GLUA_ARG_COUNT_CHECK(set_exp_var_div, 2, 2);
	GLUA_ARG_CHECK(set_exp_var_div, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(set_exp_var_div, 2, GLUA_TYPE_STRING, false, "expression name");

	TestServer_NewExpression(glua_getString(1), glua_getString(2), TSE_Op_Div);
}
GLUA_END

GLUA_FUNC(set_exp_and)
{
	GLUA_ARG_COUNT_CHECK(set_exp_var_and, 2, 2);
	GLUA_ARG_CHECK(set_exp_var_and, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(set_exp_var_and, 2, GLUA_TYPE_STRING, false, "expression name");

	TestServer_NewExpression(glua_getString(1), glua_getString(2), TSE_Op_And);
}
GLUA_END

GLUA_FUNC(set_exp_or)
{
	GLUA_ARG_COUNT_CHECK(set_exp_var_or, 2, 2);
	GLUA_ARG_CHECK(set_exp_var_or, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(set_exp_var_or, 2, GLUA_TYPE_STRING, false, "expression name");

	TestServer_NewExpression(glua_getString(1), glua_getString(2), TSE_Op_Or);
}
GLUA_END

GLUA_FUNC(set_exp_not)
{
	GLUA_ARG_COUNT_CHECK(set_exp_var_not, 2, 2);
	GLUA_ARG_CHECK(set_exp_var_not, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(set_exp_var_not, 2, GLUA_TYPE_STRING, false, "expression name");

	TestServer_NewExpression(glua_getString(1), glua_getString(2), TSE_Op_Not);
}
GLUA_END

GLUA_FUNC(push_metric)
{
	float val = 0.0f;

	GLUA_ARG_COUNT_CHECK(push_metric, 2, 3);
	GLUA_ARG_CHECK(push_metric, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(push_metric, 2, GLUA_TYPE_STRING, false, "metric name");
	GLUA_ARG_CHECK(push_metric, 3, GLUA_TYPE_INT | GLUA_TYPE_NUMBER, true, "value");

	if(glua_argn() > 2)
	{
		val = glua_getNumber(3);
	}

	glua_pushInteger(TestServer_PushMetric(glua_getString(1), glua_getString(2), val));
}
GLUA_END

GLUA_FUNC(get_metric_count)
{
	GLUA_ARG_COUNT_CHECK(get_metric_count, 2, 2);
	GLUA_ARG_CHECK(get_metric_count, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(get_metric_count, 2, GLUA_TYPE_STRING, false, "metric name");

	glua_pushInteger(TestServer_GetMetricCount(glua_getString(1), glua_getString(2)));
}
GLUA_END

GLUA_FUNC(get_metric_total)
{
	GLUA_ARG_COUNT_CHECK(get_metric_total, 2, 2);
	GLUA_ARG_CHECK(get_metric_total, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(get_metric_total, 2, GLUA_TYPE_STRING, false, "metric name");

	glua_pushNumber(TestServer_GetMetricTotal(glua_getString(1), glua_getString(2)));
}
GLUA_END

GLUA_FUNC(get_metric_average)
{
	GLUA_ARG_COUNT_CHECK(get_metric_average, 2, 2);
	GLUA_ARG_CHECK(get_metric_average, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(get_metric_average, 2, GLUA_TYPE_STRING, false, "metric name");

	glua_pushNumber(TestServer_GetMetricAverage(glua_getString(1), glua_getString(2)));
}
GLUA_END

GLUA_FUNC(get_metric_minimum)
{
	GLUA_ARG_COUNT_CHECK(get_metric_minimum, 2, 2);
	GLUA_ARG_CHECK(get_metric_minimum, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(get_metric_minimum, 2, GLUA_TYPE_STRING, false, "metric name");

	glua_pushNumber(TestServer_GetMetricMinimum(glua_getString(1), glua_getString(2)));
}
GLUA_END

GLUA_FUNC(get_metric_maximum)
{
	GLUA_ARG_COUNT_CHECK(get_metric_maximum, 2, 2);
	GLUA_ARG_CHECK(get_metric_maximum, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(get_metric_maximum, 2, GLUA_TYPE_STRING, false, "metric name");

	glua_pushNumber(TestServer_GetMetricMaximum(glua_getString(1), glua_getString(2)));
}
GLUA_END

GLUA_FUNC(clear_metric)
{
	GLUA_ARG_COUNT_CHECK(clear_metric, 2, 2);
	GLUA_ARG_CHECK(clear_metric, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(clear_metric, 2, GLUA_TYPE_STRING, false, "metric name");

	TestServer_ClearMetric(glua_getString(1), glua_getString(2));
}
GLUA_END

GLUA_FUNC(queue_report)
{
	GLUA_ARG_COUNT_CHECK(queue_report, 2, 2);
	GLUA_ARG_CHECK(queue_report, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(queue_report, 2, GLUA_TYPE_STRING, false, "report name");

	TestServer_QueueReport(glua_getString(1), glua_getString(2));
}
GLUA_END

GLUA_FUNC(issue_report)
{
	GLUA_ARG_COUNT_CHECK(issue_report, 2, 2);
	GLUA_ARG_CHECK(issue_report, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "scope name");
	GLUA_ARG_CHECK(issue_report, 2, GLUA_TYPE_STRING, false, "report name");

	TestServer_IssueReport(glua_getString(1), glua_getString(2));
}
GLUA_END

GLUA_FUNC(queue_script)
{
	GLUA_ARG_COUNT_CHECK(queue_script, 1, 1);
	GLUA_ARG_CHECK(queue_script, 1, GLUA_TYPE_STRING, false, "script name");

	TestServer_QueueScript_internal(glua_getString(1), false, false);
}
GLUA_END

GLUA_FUNC(queue_script_now)
{
	GLUA_ARG_COUNT_CHECK(queue_script_now, 1, 1);
	GLUA_ARG_CHECK(queue_script_now, 1, GLUA_TYPE_STRING, false, "script name");

	TestServer_QueueScript_internal(glua_getString(1), true, false);
}
GLUA_END

GLUA_FUNC(clear_schedules)
{
	GLUA_ARG_COUNT_CHECK(clear_schedules, 0, 0);

	TestServer_ClearSchedules();
}
GLUA_END

GLUA_FUNC(schedule_script)
{
	GLUA_ARG_COUNT_CHECK(schedule_script, 8, 8);
	GLUA_ARG_CHECK(schedule_script, 1, GLUA_TYPE_STRING, false, "script name");
	GLUA_ARG_CHECK(schedule_script, 2, GLUA_TYPE_INT | GLUA_TYPE_NUMBER, false, "day of the week");
	GLUA_ARG_CHECK(schedule_script, 3, GLUA_TYPE_INT | GLUA_TYPE_NUMBER, false, "hour");
	GLUA_ARG_CHECK(schedule_script, 4, GLUA_TYPE_INT | GLUA_TYPE_NUMBER, false, "minute");
	GLUA_ARG_CHECK(schedule_script, 5, GLUA_TYPE_INT | GLUA_TYPE_NUMBER, false, "second");
	GLUA_ARG_CHECK(schedule_script, 6, GLUA_TYPE_INT | GLUA_TYPE_NUMBER, false, "repetition interval");
	GLUA_ARG_CHECK(schedule_script, 7, GLUA_TYPE_INT | GLUA_TYPE_NUMBER, false, "number of repetitions");
	GLUA_ARG_CHECK(schedule_script, 8, GLUA_TYPE_BOOLEAN, false, "important");

	TestServer_AddScheduleEntry(glua_getString(1), glua_getInteger(2), glua_getInteger(3), glua_getInteger(4), glua_getInteger(5), glua_getInteger(6), glua_getInteger(7), glua_getBoolean(8));
}
GLUA_END

static FileScanAction TestServer_VerifyTSScript(char *dir, struct _finddata32_t *data, LuaContext *pContext)
{
	if(!stricmp(data->name, ".svn"))
	{
		return FSA_NO_EXPLORE_DIRECTORY;
	}

	if(!strEndsWith(data->name, ".lua"))
	{
		return FSA_EXPLORE_DIRECTORY;
	}

	// Ends with .lua, must be a Lua script
	{
		char fn[CRYPTIC_MAX_PATH];

		sprintf(fn, "%s/%s", dir, data->name);
		
		if(!luaLoadScript(pContext, fn, false))
		{
			TestServer_ConsolePrintfColor(COLOR_RED | COLOR_BRIGHT, "%s\n", luaContextGetLastError(pContext));
			return FSA_NO_EXPLORE_DIRECTORY;
		}

		return FSA_EXPLORE_DIRECTORY;
	}
}

void TestServer_lt_DebugHook(lua_State *L, lua_Debug *ar)
{
	// do nothing unless certain conditions are met
	if(lua_interrupt == 1)
	{
		char *pchGlobalName = NULL;

		EnterCriticalSection(&cs_luaShared);
		lua_interrupt = 0;

		if(eaScripts[0]->eState == TSS_Completed)
		{
			luaSuppressNextError();
		}
		else
		{
			eaScripts[0]->eState = TSS_Interrupted;
		}
		LeaveCriticalSection(&cs_luaShared);

		wtQueueMsg(gLuaThread, WT_CMD_SCRIPT_COMPLETE, NULL, 0);
		lua_pushstring(L, "TS.Interrupted");
		lua_error(L);
	}
}

static LuaContext *TestServer_InitLuaContext(void)
{
	LuaContext *pContext = luaContextCreate();
	lua_State *_lua_ = pContext->luaState;
	int _namespace_tbl = glua_create_tbl("ts", 0);

	luaOpenLib(pContext, luaopen_socket_core, "socket.core");
	luaOpenLib(pContext, luaopen_mime_core, "mime.core");
	luaOpenLib(pContext, luaopen_lxp, "lxp");

	GLUA_DECL(0)
	{
		glua_func(print),
		glua_func(time_print),
		glua_func(indent_print)
	}
	GLUA_DECL_END

	GLUA_DECL(_namespace_tbl)
	{
		glua_func(sha_256),
		glua_func(encode_64),
		glua_func(decode_64),
		glua_func(decode_utf8),
		glua_func(get_script_name),
		glua_func(get_time),
		glua_func(done),
		glua_func(var_atomic_begin),
		glua_func(var_atomic_end),
		glua_func(var_eval),
		glua_func(var_get),
		glua_func(var_get_count),
		glua_func(var_get_type),
		glua_func(var_get_label),
		glua_func(var_set),
		glua_func(var_insert),
		glua_func(var_persist),
		glua_func(var_set_label),
		glua_func(var_clear),
		glua_func(set_exp_add),
		glua_func(set_exp_sub),
		glua_func(set_exp_mul),
		glua_func(set_exp_div),
		glua_func(set_exp_and),
		glua_func(set_exp_or),
		glua_func(set_exp_not),
		glua_func(push_metric),
		glua_func(get_metric_count),
		glua_func(get_metric_total),
		glua_func(get_metric_average),
		glua_func(get_metric_minimum),
		glua_func(get_metric_maximum),
		glua_func(clear_metric),
		glua_func(queue_report),
		glua_func(issue_report),
		glua_func(queue_script),
		glua_func(queue_script_now),
		glua_func(clear_schedules),
		glua_func(schedule_script),

		glua_const2(Integer, TSGV_Integer),
		glua_const2(Boolean, TSGV_Boolean),
		glua_const2(Float, TSGV_Float),
		glua_const2(String, TSGV_String),
		glua_const2(Password, TSGV_Password),
		glua_const2(Ref, TSGV_Global),

		glua_const2(Single, TSG_Single),
		glua_const2(Array, TSG_Array),
		glua_const2(Expression, TSG_Expression),
		glua_const2(Metric, TSG_Metric),
	}
	GLUA_DECL_END

	luaSetHook(pContext, TestServer_lt_DebugHook, LUA_MASKCALL|LUA_MASKRET|LUA_MASKLINE, 1);

	return pContext;
}

void TestServerStatus_LuaPopulate(TestServerStatus *data)
{
	EnterCriticalSection(&cs_luaShared);
	FOR_EACH_IN_EARRAY_FORWARDS(eaScripts, TestScript, pScript)
	{
		eaPush(&data->scripts, StructClone(parse_TestScript, pScript));
	}
	FOR_EACH_END
	LeaveCriticalSection(&cs_luaShared);
}

AUTO_COMMAND ACMD_NAME(RunScript);
void TestServer_RunScript(const ACMD_SENTENCE fn)
{
	TestServer_QueueScript_internal(fn, false, false);
}

AUTO_COMMAND ACMD_NAME(RunScriptNow);
void TestServer_RunScriptNow(const ACMD_SENTENCE fn)
{
	TestServer_QueueScript_internal(fn, true, false);
}

AUTO_COMMAND ACMD_NAME(RunScriptRaw);
void TestServer_RunScriptRaw(const char *script)
{
	TestServer_QueueScript_internal(script, false, true);
}

AUTO_COMMAND ACMD_NAME(RunScriptRawNow);
void TestServer_RunScriptRawNow(const char *script)
{
	TestServer_QueueScript_internal(script, true, true);
}

AUTO_COMMAND ACMD_NAME(VerifyScriptRaw);
void TestServer_VerifyScriptRaw(const char *script)
{
	LuaContext *pContext;

	TestServer_ConsolePrintf("Verifying raw script...\n");

	pContext = TestServer_InitLuaContext();

	if(!luaLoadScriptRaw(pContext, "raw chunk", script, (int)strlen(script), false))
	{
		TestServer_ConsolePrintfColor(COLOR_RED | COLOR_BRIGHT, "%s\n", luaContextGetLastError(pContext));
	}

	luaContextDestroy(&pContext);

	TestServer_ConsolePrintf("...done.\n");
}

AUTO_COMMAND ACMD_NAME(VerifyScripts);
void TestServer_VerifyScripts(void)
{
	LuaContext *pContext;

	TestServer_ConsolePrintf("Verifying Test Server scripts...\n");

	pContext = TestServer_InitLuaContext();
	fileScanAllDataDirs("server/TestServer/scripts", TestServer_VerifyTSScript, pContext);
	luaContextDestroy(&pContext);

	TestServer_ConsolePrintf("...done.\n");
}

static void TestServer_lt_ExecuteScript(void *user_data, void *data, WTCmdPacket *packet)
{
	char *script = NULL;
	bool bRaw = false;
	bool bFail = false;

	if(gLuaContext)
	{
		luaContextDestroy(&gLuaContext);
	}

	EnterCriticalSection(&cs_luaShared);
	// normally it is STUPID STUPID STUPID to have multiple pointers to an estring
	// but since this is only temporary anyway, it's no problem
	script = eaScripts[0]->pchScript;
	bRaw = eaScripts[0]->bRaw;
	eaScripts[0]->eState = TSS_Active;
	LeaveCriticalSection(&cs_luaShared);

	gLuaContext = TestServer_InitLuaContext();

	if(bRaw)
	{
		if(!luaLoadScriptRaw(gLuaContext, "raw chunk", script, estrLength(&script), true))
		{
			bFail = true;
		}
	}
	else if(!luaLoadScript(gLuaContext, script, true))
	{
		bFail = true;
	}

	if(bFail)
	{
		EnterCriticalSection(&cs_luaShared);
		if(eaScripts[0]->eState == TSS_Active)
		{
			eaScripts[0]->eState = TSS_Interrupted;
			wtQueueMsg(gLuaThread, WT_CMD_SCRIPT_COMPLETE, NULL, 0);

			if(!luaContextErrorWasSuppressed(gLuaContext))
			{
				TestServer_ConsolePrintfColor(COLOR_RED | COLOR_BRIGHT, "%s\n", luaContextGetLastError(gLuaContext));
			}
		}
		LeaveCriticalSection(&cs_luaShared);

		if(siInGlobalAtomic)
		{
			siInGlobalAtomic = false;
			TestServer_GlobalAtomicEnd();
		}
	}
	else if(bRaw)
	{
		EnterCriticalSection(&cs_luaShared);
		eaScripts[0]->eState = TSS_Completed;
		LeaveCriticalSection(&cs_luaShared);

		if(siInGlobalAtomic)
		{
			siInGlobalAtomic = false;
			TestServer_GlobalAtomicEnd();
		}
		wtQueueMsg(gLuaThread, WT_CMD_SCRIPT_COMPLETE, NULL, 0);
	}
}

static void TestServer_ScriptComplete(void *user_data, void *data, WTCmdPacket *packet)
{
	TestScript *pScript;

	EnterCriticalSection(&cs_luaShared);
	pScript = eaRemove(&eaScripts, 0);
	StructDestroy(parse_TestScript, pScript);
	lua_interrupt = 0;

	if(eaSize(&eaScripts) > 0)
	{
		wtQueueCmd(gLuaThread, WT_CMD_EXECUTE_SCRIPT, NULL, 0);
	}
	LeaveCriticalSection(&cs_luaShared);
}

void TestServer_StartLuaThread(void)
{
	InitializeCriticalSection(&cs_luaShared);
	luaSetScriptDir("server/TestServer/scripts");
	luaSetModuleDir("server/TestServer/scripts/modules");

	gLuaThread = wtCreate(1024, 1024, NULL, "TestServerLuaThread");
	wtSetThreaded(gLuaThread, true, 0, false);
	wtRegisterCmdDispatch(gLuaThread, WT_CMD_EXECUTE_SCRIPT, TestServer_lt_ExecuteScript);
	wtRegisterMsgDispatch(gLuaThread, WT_CMD_SCRIPT_COMPLETE, TestServer_ScriptComplete);
	wtStart(gLuaThread);
	gLuaTimer = timerAlloc();
}

void TestServer_MonitorLuaThread(void)
{
	wtMonitor(gLuaThread);
}

AUTO_COMMAND ACMD_NAME(InterruptScript);
void TestServer_InterruptScript(void)
{
	EnterCriticalSection(&cs_luaShared);
	lua_interrupt = 1;
	LeaveCriticalSection(&cs_luaShared);
}

AUTO_COMMAND ACMD_NAME(CancelScript);
void TestServer_CancelScript(int iIndex)
{
	EnterCriticalSection(&cs_luaShared);
	if(iIndex > 0 && iIndex < eaSize(&eaScripts))
	{
		StructDestroy(parse_TestScript, eaRemove(&eaScripts, iIndex));
	}
	LeaveCriticalSection(&cs_luaShared);
}

AUTO_COMMAND ACMD_NAME(CancelAllScripts);
void TestServer_CancelAllScripts(void)
{
	EnterCriticalSection(&cs_luaShared);
	FOR_EACH_IN_EARRAY(eaScripts, TestScript, pScript)
	{
		if(ipScriptIndex)
		{
			eaRemove(&eaScripts, ipScriptIndex);
			StructDestroy(parse_TestScript, pScript);
		}
	}
	FOR_EACH_END

	lua_interrupt = 1;
	LeaveCriticalSection(&cs_luaShared);
}

#include "TestServerLua_h_ast.c"