#ifndef LUAINTERNALS_H
#define LUAINTERNALS_H

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "gluax.h"

typedef struct LuaContext
{
	lua_State *luaState;
	char *lastError;
	lua_Hook hookFunc;
	int hookMask;
	int hookCount;
} LuaContext;

#define GLUA_GET_CONTEXT() luaL_getparent(_lua_)

#endif
