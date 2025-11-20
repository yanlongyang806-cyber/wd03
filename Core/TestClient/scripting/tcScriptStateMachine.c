#if 0

#include "tcScriptInternal.h"
#include "tcScriptShared.h"

#include "earray.h"
#include "luaInternals.h"
#include "LuaScriptLib.h"
#include "StashTable.h"
#include "timing.h"

// Intended to eventually be moved into a lib, like "LuaUtilitiesLib"

typedef struct LuaScriptState
{
	char *pchStateName;
	int pchEnterCallback;
	int pchLeaveCallback;
	int pchTickCallback;
	F32 fTimeEnteredState;
	bool bActive;
	bool bEntering;
	bool bLeaving;
	bool bTicking;
} LuaScriptState;

StashTable sLuaScriptStateTable;
LuaScriptState **sppActiveLuaStates;
extern LuaContext *gLuaContext;
extern int gLuaTimer;

void TestClient_EnterState(const char *state)
{
	LuaScriptState *pState;

	if(!stashFindPointer(sLuaScriptStateTable, state, &pState))
	{
		printf("Could not find state \"%s\"!\n", state);
		return;
	}

	if(pState->bTicking)
	{
		return;
	}

	if(!pState->bActive)
	{
		eaPush(&sppActiveLuaStates, pState);
	}

	pState->bActive = true;
	pState->bEntering = true;
	pState->bTicking = true;
	pState->fTimeEnteredState = timerElapsed(gLuaTimer);
}

void TestClient_LeaveState(const char *state)
{
	LuaScriptState *pState = NULL;

	if(!stashFindPointer(sLuaScriptStateTable, state, &pState))
	{
		printf("Could not find state \"%s\"!\n", state);
		return;
	}

	if(!pState->bTicking)
	{
		return;
	}

	pState->bLeaving = true;
	pState->bTicking = false;
}

void TestClient_LeaveAllStates()
{
	FOR_EACH_IN_EARRAY(sppActiveLuaStates, LuaScriptState, pState)
	{
		pState->bLeaving = true;
		pState->bTicking = false;
	}
	FOR_EACH_END
}

void TestClient_StateMachineTick(void)
{
	lua_State *L = gLuaContext->luaState;

	// Walk the list and dispatch enter callbacks
	FOR_EACH_IN_EARRAY(sppActiveLuaStates, LuaScriptState, pState)
	{
		if(!pState->bEntering || (pState->bLeaving && pState->bTicking))
		{
			continue;
		}

		if(pState->pchEnterCallback != LUA_REFNIL)
		{
			lua_rawgeti(L, LUA_REGISTRYINDEX, pState->pchEnterCallback);
			lua_pushstring(L, pState->pchStateName);

			if(!luaContextCall(gLuaContext, 1))
			{
				printf("%s\n", luaContextGetLastError(gLuaContext));
			}
		}

		pState->bEntering = false;
	}
	FOR_EACH_END

	FOR_EACH_IN_EARRAY(sppActiveLuaStates, LuaScriptState, pState)
	{
		if(pState->bEntering || pState->bLeaving || !pState->bTicking || (pState->pchTickCallback == LUA_REFNIL))
		{
			continue;
		}

		lua_rawgeti(L, LUA_REGISTRYINDEX, pState->pchTickCallback);
		lua_pushstring(L, pState->pchStateName);
		
		if(!luaContextCall(gLuaContext, 1))
		{
			printf("%s\n", luaContextGetLastError(gLuaContext));
		}
	}
	FOR_EACH_END

	FOR_EACH_IN_EARRAY(sppActiveLuaStates, LuaScriptState, pState)
	{
		if(!pState->bLeaving)
		{
			continue;
		}

		if(pState->pchLeaveCallback != LUA_REFNIL)
		{
			lua_rawgeti(L, LUA_REGISTRYINDEX, pState->pchLeaveCallback);
			lua_pushstring(L, pState->pchStateName);
			
			if(!luaContextCall(gLuaContext, 1))
			{
				printf("%s\n", luaContextGetLastError(gLuaContext));
			}
		}

		pState->bLeaving = false;

		if(!pState->bEntering)
		{
			pState->bActive = false;
			eaRemove(&sppActiveLuaStates, ipStateIndex);
		}
	}
	FOR_EACH_END
}

GLUA_FUNC( register_state )
{
	LuaScriptState *pState = NULL;

	GLUA_ARG_COUNT_CHECK( register_state, 4, 4 );
	GLUA_ARG_CHECK( register_state, 1, GLUA_TYPE_STRING, false, "state identifier string" );
	GLUA_ARG_CHECK( register_state, 2, GLUA_TYPE_FUNCTION | GLUA_TYPE_NIL, false, "state-enter callback" );
	GLUA_ARG_CHECK( register_state, 3, GLUA_TYPE_FUNCTION | GLUA_TYPE_NIL, false, "state-exit callback" );
	GLUA_ARG_CHECK( register_state, 4, GLUA_TYPE_FUNCTION | GLUA_TYPE_NIL, false, "state-tick callback" );

	pState = calloc(1, sizeof(LuaScriptState));
	pState->pchStateName = strdup(glua_getString(1));
	pState->pchTickCallback = luaL_ref(_lua_, LUA_REGISTRYINDEX);
	pState->pchLeaveCallback = luaL_ref(_lua_, LUA_REGISTRYINDEX);
	pState->pchEnterCallback = luaL_ref(_lua_, LUA_REGISTRYINDEX);

	if(!sLuaScriptStateTable)
	{
		sLuaScriptStateTable = stashTableCreateWithStringKeys(0, StashDefault);
	}

	if(stashFindPointer(sLuaScriptStateTable, pState->pchStateName, NULL))
	{
		glua_errorN("Attempted to register duplicate state \"%s\"!", pState->pchStateName);
		free(pState->pchStateName);
		free(pState);
		break;
	}

	stashAddPointer(sLuaScriptStateTable, pState->pchStateName, pState, false);
}
GLUA_END

GLUA_FUNC( enter_state )
{
	GLUA_ARG_COUNT_CHECK( enter_state, 1, 1 );
	GLUA_ARG_CHECK( enter_state, 1, GLUA_TYPE_STRING, false, "state identifier string" );

	TestClient_EnterState(glua_getString(1));
}
GLUA_END

GLUA_FUNC( leave_state )
{
	GLUA_ARG_COUNT_CHECK( leave_state, 1, 1 );
	GLUA_ARG_CHECK( leave_state, 1, GLUA_TYPE_STRING, false, "state identifier string" );

	TestClient_LeaveState(glua_getString(1));
}
GLUA_END

GLUA_FUNC( leave_all_states )
{
	GLUA_ARG_COUNT_CHECK( leave_all_states, 0, 0 );
	
	TestClient_LeaveAllStates();
}
GLUA_END

GLUA_FUNC( switch_to_state )
{
	GLUA_ARG_COUNT_CHECK( switch_to_state, 1, 1 );
	GLUA_ARG_CHECK( switch_to_state, 1, GLUA_TYPE_STRING, false, "state identifier string" );

	if(!stashFindPointer(sLuaScriptStateTable, glua_getString(1), NULL))
	{
		glua_errorN("Could not find state \"%s\"!\n", glua_getString(1));
		break;
	}

	TestClient_LeaveAllStates();
	TestClient_EnterState(glua_getString(1));
}
GLUA_END

GLUA_FUNC( is_in_state )
{
	LuaScriptState *pState = NULL;

	GLUA_ARG_COUNT_CHECK( is_in_state, 1, 1 );
	GLUA_ARG_CHECK( is_in_state, 1, GLUA_TYPE_STRING, false, "state identifier string" );

	if(!stashFindPointer(sLuaScriptStateTable, glua_getString(1), &pState))
	{
		glua_errorN("Could not find state \"%s\"!\n", glua_getString(1));
		break;
	}

	glua_pushBoolean(pState->bActive);
}
GLUA_END

GLUA_FUNC( time_in_state )
{
	LuaScriptState *pState = NULL;

	GLUA_ARG_COUNT_CHECK( time_in_state, 1, 1 );
	GLUA_ARG_CHECK( time_in_state, 1, GLUA_TYPE_STRING, false, "state identifier string" );

	if(!stashFindPointer(sLuaScriptStateTable, glua_getString(1), &pState))
	{
		glua_errorN("Could not find state \"%s\"!\n", glua_getString(1));
		break;
	}

	if(!pState->bActive)
	{
		glua_pushNumber(0);
		break;
	}

	glua_pushNumber(timerElapsed(gLuaTimer) - pState->fTimeEnteredState);
}
GLUA_END

GLUA_FUNC( get_states )
{
	GLUA_ARG_COUNT_CHECK( get_states, 0, 0 );

	glua_pushTable();

	FOR_EACH_IN_EARRAY_FORWARDS(sppActiveLuaStates, LuaScriptState, pState)
	{
		glua_pushString(pState->pchStateName);
		glua_tblKey_i(ipStateIndex+1);
	}
	FOR_EACH_END
}
GLUA_END

void TestClient_InitStateMachine(LuaContext *pContext)
{
	lua_State *_lua_ = pContext->luaState;
	int namespaceTbl = _glua_create_tbl(_lua_, "tc", 0);

	GLUA_DECL( namespaceTbl )
	{
		glua_func( register_state ),
		glua_func( enter_state ),
		glua_func( leave_state ),
		glua_func( leave_all_states ),
		glua_func( switch_to_state ),
		glua_func( is_in_state ),
		glua_func( time_in_state ),
		glua_func( get_states )
	}
	GLUA_DECL_END
}

#endif