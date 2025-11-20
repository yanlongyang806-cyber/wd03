#include "luaLibIntrinsics.h"
#include "luaInternals.h"
#include "earray.h"
#include "estring.h"
#include "logging.h"
#include <time.h>
#include "timing.h"
#include "utils.h"
#include "UTF8.h"

typedef struct LuaInitVar
{
	char *pcVarName;
	char *pcValue;
	bool bInsert;
} LuaInitVar;

static LuaInitVar **sppLuaInitVars = NULL;

AUTO_COMMAND ACMD_NAME(LuaSet) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
void luaInitSet(const char *pcVarName, const char *pcValue)
{
	LuaInitVar *pInitVar = calloc(1, sizeof(LuaInitVar));

	pInitVar->pcVarName = strdup(pcVarName);
	pInitVar->pcValue = strdup(pcValue);
	pInitVar->bInsert = false;

	eaPush(&sppLuaInitVars, pInitVar);
}

AUTO_COMMAND ACMD_NAME(LuaInsert) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
void luaInitInsert(const char *pcVarName, const char *pcValue)
{
	LuaInitVar *pInitVar = calloc(1, sizeof(LuaInitVar));

	pInitVar->pcVarName = strdup(pcVarName);
	pInitVar->pcValue = strdup(pcValue);
	pInitVar->bInsert = true;

	eaPush(&sppLuaInitVars, pInitVar);
}

AUTO_COMMAND ACMD_NAME(LuaRun) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
void luaInitRun(const char *pcScriptString)
{
	LuaInitVar *pInitVar = calloc(1, sizeof(LuaInitVar));

	pInitVar->pcVarName = strdup(pcScriptString);
	pInitVar->pcValue = NULL;
	pInitVar->bInsert = false;

	eaPush(&sppLuaInitVars, pInitVar);
}

void luaInitVars(LuaContext *pLuaContext)
{
	int i;

	for(i = eaSize(&sppLuaInitVars) - 1; i >= 0; --i)
	{
		LuaInitVar *pInitVar = eaRemove(&sppLuaInitVars, i);
		char *pLuaCmd = NULL;

		if(!pInitVar->pcValue)
		{
			estrPrintf(&pLuaCmd, "%s", pInitVar->pcVarName);
			luaLoadScriptRaw(pLuaContext, NULL, pLuaCmd, estrLength(&pLuaCmd), true);
			estrDestroy(&pLuaCmd);
		}
		else if(pInitVar->bInsert)
		{
			estrPrintf(&pLuaCmd, "%s = %s or { }", pInitVar->pcVarName, pInitVar->pcVarName);
			luaLoadScriptRaw(pLuaContext, NULL, pLuaCmd, estrLength(&pLuaCmd), true);
			estrPrintf(&pLuaCmd, "table.insert(%s, %s or \"%s\")", pInitVar->pcVarName, pInitVar->pcValue, pInitVar->pcValue);
			luaLoadScriptRaw(pLuaContext, NULL, pLuaCmd, estrLength(&pLuaCmd), true);
			estrDestroy(&pLuaCmd);
		}
		else
		{
			estrPrintf(&pLuaCmd, "%s = %s or \"%s\"", pInitVar->pcVarName, pInitVar->pcValue, pInitVar->pcValue);
			luaLoadScriptRaw(pLuaContext, NULL, pLuaCmd, estrLength(&pLuaCmd), true);
			estrDestroy(&pLuaCmd);
		}
	}
}

GLUA_FUNC(log)
{
	GLUA_ARG_COUNT_CHECK( log, 1, 1 );
	GLUA_ARG_CHECK( log, 1, GLUA_TYPE_STRING, false, "" );

	log_printf(LOG_LUA, "%s", glua_getString(1));
}
GLUA_END

GLUA_FUNC(get_cwd)
{
	char *pCWD = NULL;

	GLUA_ARG_COUNT_CHECK( get_cwd, 0, 0 );

	GetCurrentDirectory_UTF8(&pCWD);
	glua_pushString(pCWD);
	
	estrDestroy(&pCWD);
}
GLUA_END

GLUA_FUNC(set_cwd)
{
	GLUA_ARG_COUNT_CHECK( set_cwd, 1, 1 );
	GLUA_ARG_CHECK( set_cwd, 1, GLUA_TYPE_STRING, false, "directory" );

	SetCurrentDirectory_UTF8(glua_getString(1));
}
GLUA_END

GLUA_FUNC(time_print)
{
	int n = glua_argn();
	char timestr[100];
	struct tm tim = {0};

	timeMakeLocalTimeStructFromSecondsSince2000(timeSecondsSince2000(), &tim);
	strftime(SAFESTR(timestr), "%Y-%m-%d %H:%M:%S | ", &tim);
	printf("%s", timestr);

	lua_getglobal(_lua_, "print");
	lua_insert(_lua_, lua_gettop(_lua_)-n);
	luaContextCall(GLUA_GET_CONTEXT(), n);
}
GLUA_END

typedef struct LuaProcess
{
	QueryableProcessHandle	*pHandle;
	int						iHandle;
} LuaProcess;

static LuaProcess **eaProcesses = NULL;
static int iProcesses = 0;

GLUA_FUNC(app_run)
{
	LuaProcess *pProc = NULL;

	GLUA_ARG_COUNT_CHECK(app_run, 1, 1);
	GLUA_ARG_CHECK(app_run, 1, GLUA_TYPE_STRING, false, "executable name");

	pProc = calloc(1, sizeof(LuaProcess));
	pProc->pHandle = StartQueryableProcess(glua_getString(1), NULL, 0, 1, 0, NULL);
	pProc->iHandle = ++iProcesses;
	eaPush(&eaProcesses, pProc);
	glua_pushInteger(pProc->iHandle);
}
GLUA_END

GLUA_FUNC(app_check)
{
	int i;
	int iHandle;

	GLUA_ARG_COUNT_CHECK(app_check, 1, 1);
	GLUA_ARG_CHECK(app_check, 1, GLUA_TYPE_INT, false, "process handle");

	iHandle = glua_getInteger(1);

	for(i = 0; i < eaSize(&eaProcesses); ++i)
	{
		if(eaProcesses[i]->iHandle == iHandle)
		{
			if(QueryableProcessComplete(&eaProcesses[i]->pHandle, NULL))
			{
				free(eaRemove(&eaProcesses, i));
				glua_pushBoolean(true);
			}
			else
			{
				glua_pushBoolean(false);
			}
			break;
		}
	}
}
GLUA_END

GLUA_FUNC(app_close)
{
	int i;
	int iHandle;

	GLUA_ARG_COUNT_CHECK(app_close, 1, 1);
	GLUA_ARG_CHECK(app_close, 1, GLUA_TYPE_INT, false, "process handle");

	iHandle = glua_getInteger(1);

	for(i = 0; i < eaSize(&eaProcesses); ++i)
	{
		if(eaProcesses[i]->iHandle == iHandle)
		{
			KillQueryableProcess(&eaProcesses[i]->pHandle);
			free(eaRemove(&eaProcesses, i));
			break;
		}
	}
}
GLUA_END

GLUA_FUNC(app_kill)
{
	int i;
	int iHandle;

	GLUA_ARG_COUNT_CHECK(app_kill, 1, 1);
	GLUA_ARG_CHECK(app_kill, 1, GLUA_TYPE_INT, false, "process handle");

	iHandle = glua_getInteger(1);

	for(i = 0; i < eaSize(&eaProcesses); ++i)
	{
		if(eaProcesses[i]->iHandle == iHandle)
		{
			KillQueryableProcess(&eaProcesses[i]->pHandle);
			free(eaRemove(&eaProcesses, i));
			break;
		}
	}
}
GLUA_END

void luaLibIntrinsicsRegister(LuaContext *pLuaContext)
{
	lua_State *_lua_ = pLuaContext->luaState;

//  Use this line instead to make functions namespaced as prefix.func() instead of func()
//	int namespaceTbl = _glua_create_tbl( _lua_, "prefix", 0 );
	int namespaceTbl = 0; 

	GLUA_DECL(namespaceTbl)
	{
		glua_func(log),
		glua_func(get_cwd),
		glua_func(set_cwd),
		glua_func(time_print),
		glua_func(app_run),
		glua_func(app_check),
		glua_func(app_close),
		glua_func(app_kill),
	}
	GLUA_DECL_END
}
