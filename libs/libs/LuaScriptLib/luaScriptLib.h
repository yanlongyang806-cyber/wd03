#ifndef LUASCRIPTLIB_H
#define LUASCRIPTLIB_H

typedef struct LuaContext LuaContext;
typedef struct lua_State lua_State;
typedef struct lua_Debug lua_Debug;
typedef void (*lua_Hook)(lua_State *L, lua_Debug *ar);

void luaSetScriptDir(const char *dir);
void luaSetModuleDir(const char *dir);
void luaEnableDebug(bool bEnabled);
void luaSetHook(LuaContext *pContext, lua_Hook func, int mask, int count);

LuaContext *luaContextCreate();
void luaInitSet(const char *pcVarName, const char *pcValue);
void luaInitInsert(const char *pcVarName, const char *pcValue);
void luaInitRun(const char *pcScriptString);
void luaInitVars(LuaContext *pLuaContext);
void luaOpenLib(LuaContext *pLuaContext, void *func, const char *name);
void luaContextDestroy(LuaContext **ppLuaContext);

void luaSuppressNextError(void);
void luaSuppressAllErrors(void);
void luaStopSuppressingErrors(void);
bool luaContextErrorWasSuppressed(LuaContext *pLuaContext);
const char *luaContextGetLastError(LuaContext *pLuaContext);

bool luaCompileScript(LuaContext *pLuaContext, const char *name, const char *buf, int len, bool bUnload);
bool luaContextCall(LuaContext *pLuaContext, int args);
bool luaLoadScript(LuaContext *pLuaContext, const char *fn, bool bRun);
bool luaLoadScriptRaw(LuaContext *pLuaContext, const char *name, const char *buf, int len, bool bRun);

#endif
