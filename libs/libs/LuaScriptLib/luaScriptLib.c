#include "luaScriptLib.h"
#include "luaInternals.h"
#include "luaLibIntrinsics.h"

#include "estring.h"
#include "file.h"
#include "StashTable.h"
#include "timing_profiler_interface.h"
#include "utils.h"

static char sLuaScriptDir[CRYPTIC_MAX_PATH] = "";
static char sLuaModuleDir[CRYPTIC_MAX_PATH] = "modules";

static bool lua_debug = 0;
StashTable slua_debug_profiling = NULL;

AUTO_CMD_INT(lua_debug, luadebug) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);

void luaEnableDebug(bool bEnabled)
{
	lua_debug = bEnabled;
}

void luaSetHook(LuaContext *pContext, lua_Hook func, int mask, int count)
{
	if(lua_debug)
	{
		pContext->hookFunc = func;
		pContext->hookMask = mask;
		pContext->hookCount = count;
	}
	else if(func)
	{
		lua_sethook(pContext->luaState, func, mask, count);
	}
	else
	{
		lua_sethook(pContext->luaState, NULL, 0, 0);
	}
}

int luaRequireModule(lua_State *L);
void luaDebugHook(lua_State *L, lua_Debug *ar);

// ----------------------------------------------------------------------

void luaSetScriptDir(const char *dir)
{
	strcpy(sLuaScriptDir, dir);
}

void luaSetModuleDir(const char *dir)
{
	strcpy(sLuaModuleDir, dir);
}

// ----------------------------------------------------------------------

void luaOpenLib(LuaContext *pLuaContext, void *func, const char *name)
{
	lua_pushcfunction(pLuaContext->luaState, func);
	lua_pushstring(pLuaContext->luaState, name);
	lua_call(pLuaContext->luaState, 1, 0);
}

LuaContext * luaContextCreate()
{
	LuaContext *pLuaContext = calloc(1, sizeof(LuaContext));

	pLuaContext->luaState = lua_open();
	luaL_setparent(pLuaContext->luaState, pLuaContext);

	luaOpenLib(pLuaContext, luaopen_base,    "");
	luaOpenLib(pLuaContext, luaopen_string,  LUA_STRLIBNAME);
	luaOpenLib(pLuaContext, luaopen_math,    LUA_MATHLIBNAME);
	luaOpenLib(pLuaContext, luaopen_table,   LUA_TABLIBNAME);
	luaOpenLib(pLuaContext, luaopen_io,      LUA_IOLIBNAME);
	luaOpenLib(pLuaContext, luaopen_os,		 LUA_OSLIBNAME);
	luaOpenLib(pLuaContext, luaopen_package, LUA_LOADLIBNAME);
	luaOpenLib(pLuaContext, luaopen_debug,	 LUA_DBLIBNAME);
	luaLibIntrinsicsRegister(pLuaContext);
	// Load any other guaranteed-to-exist modules here (system, core?)

	// Add a package loader to plug into the Cryptic file system
	lua_getglobal(pLuaContext->luaState, "package");
	lua_getfield(pLuaContext->luaState, -1, "loaders");
	lua_pushcfunction(pLuaContext->luaState, luaRequireModule);
	lua_rawseti(pLuaContext->luaState, -2, 5);
	lua_pop(pLuaContext->luaState, 2);

	if(lua_debug)
	{
		lua_sethook(pLuaContext->luaState, luaDebugHook, LUA_MASKCALL|LUA_MASKRET|LUA_MASKLINE, 1);
	}

	return pLuaContext;
}

void luaContextDestroy(LuaContext **ppLuaContext)
{
	LuaContext *pLuaContext = *ppLuaContext;
	lua_close(pLuaContext->luaState);
	estrDestroy(&pLuaContext->lastError);
	free(pLuaContext);

	*ppLuaContext = NULL;
}

bool luaContextErrorWasSuppressed(LuaContext *pLuaContext)
{
	return !pLuaContext->lastError;
}

const char * luaContextGetLastError(LuaContext *pLuaContext)
{
	return (pLuaContext->lastError) ? pLuaContext->lastError : "";
}

// ----------------------------------------------------------------------
static bool sSuppressNext = false;
static bool sSuppressAll = false;

void luaSuppressNextError(void)
{
	sSuppressNext = true;
}

void luaSuppressAllErrors(void)
{
	sSuppressNext = true;
	sSuppressAll = true;
}

void luaStopSuppressingErrors(void)
{
	sSuppressNext = false;
	sSuppressAll = false;
}

static int luaErrorHandler(lua_State *L) 
{
	LuaContext *pLuaContext = luaL_getparent(L);

	// If we're suppressing errors, leave now and don't do anything.
	if(sSuppressNext)
	{
		estrDestroy(&pLuaContext->lastError);
		sSuppressNext = sSuppressAll;
		return 1;
	}

	// Get a stack traceback
	lua_getglobal(L, "debug");
	lua_pushstring(L, "traceback");
	lua_gettable(L, -2);
	lua_remove(L, -2);
	lua_call(L, 0, 1);

	// There are now two strings on the stack; the error, and the traceback
	estrPrintf(&pLuaContext->lastError, "Lua Runtime Error: %s", lua_tostring(L, -2));
	estrConcatf(&pLuaContext->lastError, "\nStack Traceback: %s", lua_tostring(L, -1));

	// Replace the stack with the concatenated error message.
	lua_pop(L, 2);
	lua_pushstring(L, pLuaContext->lastError);

	return 1;
}

static void luaDebugHook(lua_State *L, lua_Debug *ar)
{
	LuaContext *pContext = luaL_getparent(L);

	if(ar->event != LUA_HOOKLINE)
	{
		lua_getinfo(L, "Sn", ar);
	}
	
	switch(ar->event)
	{
	case LUA_HOOKCALL:
		{
			PERFINFO_TYPE **ppPerfInfo = NULL;
			StashElement pElem = NULL;
			const char *pcStashKey = NULL;
			char *pcFuncTimer = NULL;
			S32 didDisable;

			autoTimerDisableRecursion(&didDisable);

			if(ar->name)
			{
				estrPrintf(&pcFuncTimer, "%s", ar->name);
			}
			else
			{
				estrPrintf(&pcFuncTimer, "%s", ar->what);
			}

			if(ar->linedefined > 0)
			{
				estrConcatf(&pcFuncTimer, " (%s:%d)", ar->source, ar->linedefined);
			}
			
			if(!slua_debug_profiling)
			{
				slua_debug_profiling = stashTableCreateWithStringKeys(8, StashDeepCopyKeys_NeverRelease);
			}

			if(stashFindElement(slua_debug_profiling, pcFuncTimer, &pElem))
			{
				ppPerfInfo = (PERFINFO_TYPE **)stashElementGetPointer(pElem);
			}
			else
			{
				ppPerfInfo = calloc(1, sizeof(PERFINFO_TYPE *));
				stashAddPointer(slua_debug_profiling, pcFuncTimer, ppPerfInfo, true);
			}

			stashGetKey(slua_debug_profiling, pcFuncTimer, &pcStashKey);
			estrDestroy(&pcFuncTimer);
			autoTimerEnableRecursion(didDisable);
			PERFINFO_AUTO_START_STATIC(pcStashKey, ppPerfInfo, 1);
		}
	xcase LUA_HOOKRET:
	case LUA_HOOKTAILRET:
		PERFINFO_AUTO_STOP();
	}

	if(pContext->hookFunc && pContext->hookMask & (1 << ar->event) || (pContext->hookMask & LUA_MASKRET && ar->event == LUA_HOOKTAILRET))
	{
		pContext->hookFunc(L, ar);
	}
}

bool luaCompileScript(LuaContext *pLuaContext, const char *name, const char *buf, int len, bool bUnload)
{
	bool bSuccess = true;
	lua_State *L = pLuaContext->luaState;
	int status;

	estrClear(&pLuaContext->lastError);
	status = luaL_loadbuffer(L, buf, len, name);

	if(status != 0)
	{
		estrPrintf(&pLuaContext->lastError, "Lua Compilation Error: %s", lua_tostring(L, -1));
		return false;
	}

	if(bUnload)
	{
		lua_remove(L, -1);
	}

	return !status;
}

bool luaContextCall(LuaContext *pContext, int args)
{
	bool bSuccess = true;
	lua_State *L = pContext->luaState;
	int ret;
	int top = lua_gettop(L);

	lua_pushcfunction(L, luaErrorHandler);
	lua_insert(L, top-args);

	ret = lua_pcall(L, args, 0, top-args);

	if (ret)
	{
		// pLuaContext->lastError should already be populated by luaErrorHandler()
		bSuccess = false;
	}

	lua_remove(L, top-args);

	return bSuccess;
}

// ----------------------------------------------------------------------

char *luaFixupPath(const char *inpath, const char *dir)
{
	char *outpath = NULL;

	if(strStartsWith(inpath, dir))
	{
		estrPrintf(&outpath, "%s", inpath);
	}
	else
	{
		estrPrintf(&outpath, "%s/%s", dir, inpath);
	}

	estrReplaceOccurrences(&outpath, ".", "/");

	if(strEndsWith(outpath, "/lua"))
	{
		outpath[estrLength(&outpath)-4] = '.';
	}
	else
	{
		estrConcatf(&outpath, ".lua");
	}

	return outpath;
}

bool luaLoadScript(LuaContext *pLuaContext, const char *fn, bool bRun)
{
	char *filePath;
	char *file;
	int len;

	filePath = luaFixupPath(fn, sLuaScriptDir);
	file = fileAlloc(filePath, &len);

	if(!file)
	{
		if(lua_debug)
		{
			printfColor(COLOR_RED | COLOR_BRIGHT, "Error reading script at %s.\n", filePath);
		}

		estrDestroy(&filePath);
		return false;
	}

	estrDestroy(&filePath);

	if(!luaCompileScript(pLuaContext, fn, file, len, !bRun))
	{
		if(lua_debug && !luaContextErrorWasSuppressed(pLuaContext))
		{
			printfColor(COLOR_RED | COLOR_BRIGHT, "%s\n", luaContextGetLastError(pLuaContext));
		}

		fileFree(file);
		return false;
	}

	fileFree(file);

	if(!bRun)
	{
		return true;
	}

	if(!luaContextCall(pLuaContext, 0))
	{
		if(lua_debug && !luaContextErrorWasSuppressed(pLuaContext))
		{
			printfColor(COLOR_RED | COLOR_BRIGHT, "%s\n", luaContextGetLastError(pLuaContext));
		}

		return false;
	}

	return true;
}

bool luaLoadScriptRaw(LuaContext *pLuaContext, const char *name, const char *buf, int len, bool bRun)
{
	estrClear(&pLuaContext->lastError);

	if(!luaCompileScript(pLuaContext, name, buf, len, !bRun))
	{
		if(lua_debug && !luaContextErrorWasSuppressed(pLuaContext))
		{
			printfColor(COLOR_RED | COLOR_BRIGHT, "%s\n", luaContextGetLastError(pLuaContext));
		}

		return false;
	}

	if(!bRun)
	{
		return true;
	}

	if(!luaContextCall(pLuaContext, 0))
	{
		if(lua_debug && !luaContextErrorWasSuppressed(pLuaContext))
		{
			printfColor(COLOR_RED | COLOR_BRIGHT, "%s\n", luaContextGetLastError(pLuaContext));
		}

		return false;
	}

	return true;
}

int luaLoadModule(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	char *ncname, *fragment, *temp, *tempstate = NULL;
	char *filePath;
	char *file;
	int len;
	
	filePath = luaFixupPath(name, sLuaModuleDir);
	file = fileAlloc(filePath, &len);

	if(!file)
	{
		lua_pushfstring(L, "Error reading module at %s.", filePath);
		estrDestroy(&filePath);
		lua_error(L);
		return 0;
	}

	estrDestroy(&filePath);

	if(!luaLoadScriptRaw(luaL_getparent(L), name, file, len, true))
	{
		lua_pushfstring(L, "Error loading module %s: %s", name, luaContextGetLastError(luaL_getparent(L)));
		lua_error(L);
		fileFree(file);
		return 0;
	}

	fileFree(file);

	ncname = strdup(name);
	fragment = strtok_s(ncname, "/", &tempstate);

	while((temp = strtok_s(NULL, "/", &tempstate)))
	{
		fragment = temp;
	}

	temp = strtok_s(fragment, ".", &tempstate);
	lua_getglobal(L, temp);

	while((temp = strtok_s(NULL, ".", &tempstate)))
	{
		if(!lua_istable(L, -1))
		{
			break;
		}

		if(!stricmp(temp, "lua"))
		{
			break;
		}

		lua_getfield(L, -1, temp);
		lua_remove(L, -2);
	}

	free(ncname);

	if(lua_istable(L, -1))
	{
		lua_getfield(L, -1, "Load");

		if(lua_isfunction(L, -1))
		{
			luaContextCall(luaL_getparent(L), 0);
		}
		else
		{
			lua_remove(L, -1);
		}

		return 1;
	}
	else
	{
		lua_remove(L, -1);
		return 0;
	}
}

int luaRequireModule(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	char *filePath;

	filePath = luaFixupPath(name, sLuaModuleDir);

	if(fileExists(filePath))
	{
		lua_pushcfunction(L, luaLoadModule);
	}
	else
	{
		lua_pushfstring(L, "No module at %s", filePath);
	}

	estrDestroy(&filePath);
	return 1;
}