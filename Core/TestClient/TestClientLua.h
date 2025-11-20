#pragma once

#include "luaInternals.h"

void TestClient_InitLua(void);
void TestClient_LuaExecute(const char *script);
void TestClient_LuaTick(void);