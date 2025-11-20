#include "TestClientLua.h"

#include "ClientControllerLib.h"
#include "cmdparse.h"
#include "ControllerScriptingSupport.h"
#include "earray.h"
#include "EString.h"
#include "file.h"
#include "logging.h"
#include "luaScriptLib.h"
#include "mathutil.h"
#include "ServerLib.h"
#include "structNet.h"
#include "TestClient.h"
#include "testclient_comm.h"
#include "TestClientCommon.h"
#include "TestServerIntegration.h"
#include "TestClientScripting.h"
#include "tokenstore.h"
#include "timing.h"
#include "utilitiesLib.h"
#include "utils.h"

#include "TestClientCommon_h_ast.h"

#include "Autogen/GameClientLib_commands_autogen_CommandFuncs.h"

static int *spiCallbacks = NULL;
static LuaContext *sLuaContext = NULL;

GLUA_FUNC( succeed )
{
	char *cmd = NULL;

	GLUA_ARG_COUNT_CHECK( succeed, 1, 1 );
	GLUA_ARG_CHECK( succeed, 1, GLUA_TYPE_STRING, false, "" );

	//estrPrintf(&cmd, "TestClient Send %u %s", gTestClientGlobalState.iOwnerID, glua_getString(1));
	ClientController_SendCommandToClient(cmd);
	estrDestroy(&cmd);

	//RemoteCommand_TestClient_Succeed(GLOBALTYPE_TESTCLIENTSERVER, 0, gTestClientGlobals.pLatestState->iID, glua_getString(1));
}
GLUA_END

GLUA_FUNC( fail )
{
	char *cmd = NULL;

	GLUA_ARG_COUNT_CHECK( fail, 1, 1 );
	GLUA_ARG_CHECK( fail, 1, GLUA_TYPE_STRING, false, "" );

	//estrPrintf(&cmd, "TestClient Send %u %s", gTestClientGlobalState.iOwnerID, glua_getString(1));
	ClientController_SendCommandToClient(cmd);
	estrDestroy(&cmd);

	//RemoteCommand_TestClient_Fail(GLOBALTYPE_TESTCLIENTSERVER, 0, gTestClientGlobals.pLatestState->iID, glua_getString(1));
}
GLUA_END

GLUA_FUNC( status )
{
	GLUA_ARG_COUNT_CHECK( status, 1, 1 );
	GLUA_ARG_CHECK( status, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "" );

	if(glua_isNil(1))
	{
		estrDestroy(&gTestClientGlobalState.pcStatus);
	}
	else
	{
		estrCopy2(&gTestClientGlobalState.pcStatus, glua_getString(1));
	}
}
GLUA_END

GLUA_FUNC(get_status)
{
	GLUA_ARG_COUNT_CHECK(get_status, 0, 0);

	if(gTestClientGlobalState.pcStatus)
	{
		glua_pushString(gTestClientGlobalState.pcStatus);
	}
	else
	{
		glua_pushNil();
	}
}
GLUA_END

GLUA_FUNC(prod_mode)
{
	GLUA_ARG_COUNT_CHECK(prod_mode, 0, 0);
	glua_pushBoolean(isProductionMode());
}
GLUA_END

GLUA_FUNC(version)
{
	GLUA_ARG_COUNT_CHECK(version, 0, 0);
	glua_pushString(GetUsefulVersionString());
}
GLUA_END

GLUA_FUNC(game_full)
{
	GLUA_ARG_COUNT_CHECK(game_full, 0, 0);
	glua_pushString(GetProductName());
}
GLUA_END

GLUA_FUNC(get_exec_dir)
{
	GLUA_ARG_COUNT_CHECK(get_exec_dir, 0, 0);
	glua_pushString(ClientController_GetExecDirectoryForClient());
}
GLUA_END

GLUA_FUNC(set_exec_dir)
{
	GLUA_ARG_COUNT_CHECK(set_exec_dir, 1, 1);
	GLUA_ARG_CHECK(set_exec_dir, 1, GLUA_TYPE_STRING, false, "exec directory");
	ClientController_SetExecDirectoryForClient(glua_getString(1));
}
GLUA_END

GLUA_FUNC( error )
{
	GLUA_ARG_COUNT_CHECK( error, 1, 1 );
	GLUA_ARG_CHECK( error, 1, GLUA_TYPE_STRING, false, "" );

	Errorf("%s", glua_getString(1));
}
GLUA_END

GLUA_FUNC( send_to_owner )
{
	char *cmd = NULL;

	GLUA_ARG_COUNT_CHECK( send_to_owner, 1, 1 );
	GLUA_ARG_CHECK( send_to_owner, 1, GLUA_TYPE_STRING, false, "" );

	estrPrintf(&cmd, "TestClient Send %u %s", gTestClientGlobalState.iOwnerID, glua_getString(1));
	ClientController_SendCommandToClient(cmd);
	estrDestroy(&cmd);
}
GLUA_END

// EVERYTHING ABOVE HERE IS OBSOLETE

GLUA_FUNC(tc_log)
{
	GLUA_ARG_COUNT_CHECK(log, 1, 1);
	GLUA_ARG_CHECK(log, 1, GLUA_TYPE_STRING, false, "");

	log_printf(LOG_TC_TESTCLIENT, "%s", glua_getString(1));
}
GLUA_END

GLUA_FUNC(exit)
{
	GLUA_ARG_COUNT_CHECK(exit, 0, 0);
	gTestClientGlobalState.bRunning = false;
}
GLUA_END

GLUA_FUNC(execute)
{
	GLUA_ARG_COUNT_CHECK(execute, 1, 1);
	GLUA_ARG_CHECK(execute, 1, GLUA_TYPE_STRING, false, "command");
	ClientController_SendCommandToClient(glua_getString(1));
}
GLUA_END

GLUA_FUNC(local_execute)
{
	GLUA_ARG_COUNT_CHECK(local_execute, 1, 1);
	GLUA_ARG_CHECK(local_execute, 1, GLUA_TYPE_STRING, false, "command");
	globCmdParse(glua_getString(1));
}
GLUA_END

GLUA_FUNC(register_tick)
{
	GLUA_ARG_COUNT_CHECK(register_tick, 1, 1);
	GLUA_ARG_CHECK(register_tick, 1, GLUA_TYPE_FUNCTION, false, "tick function");
	eaiPush(&spiCallbacks, lua_ref(_lua_, 1));
}
GLUA_END

GLUA_FUNC(get_script)
{
	GLUA_ARG_COUNT_CHECK(get_script, 0, 0);
	glua_pushString(gTestClientGlobalState.cScriptName);
}
GLUA_END

GLUA_FUNC(get_time)
{
	GLUA_ARG_COUNT_CHECK(get_time, 0, 0);
	glua_pushNumber(TestClient_GetScriptTime());
}
GLUA_END

GLUA_FUNC(get_id)
{
	GLUA_ARG_COUNT_CHECK(get_id, 0, 0);
	glua_pushInteger(gTestClientGlobalState.iID);
}
GLUA_END

GLUA_FUNC(get_fps)
{
	GLUA_ARG_COUNT_CHECK(get_fps, 0, 0);
	glua_pushInteger(gTestClientGlobalState.iFPS);
}
GLUA_END

GLUA_FUNC(client_exe_state)
{
	GLUA_ARG_COUNT_CHECK(client_exe_state, 0, 0);
	glua_pushInteger(ClientController_MonitorState());
}
GLUA_END

GLUA_FUNC(client_fsm_state)
{
	GLUA_ARG_COUNT_CHECK(client_fsm_state, 0, 0);
	glua_pushString(ClientController_GetClientFSMState());
}
GLUA_END

/*
static void *spLastGameStateStruct = NULL;
static bool sbExpectGameStateStructChange = false;
bool TestClient_GameState_LuaPreArrayCB(ParseTable pti[], void *pStruct, int column, void *pCBData)
{
	lua_State *L = pCBData;

	PERFINFO_AUTO_START_FUNC();
	if(TokenStoreStorageTypeIsAnArray(TokenStoreGetStorageType(pti[column].type)))
	{
		lua_pushstring(L, pti[column].name);
		lua_newtable(L);
		lua_settable(L, -3);

		if(TokenStoreGetNumElems(pti, column, pStruct, NULL) > 0)
		{
			lua_pushstring(L, pti[column].name);
			lua_gettable(L, -2);
		}
	}
	PERFINFO_AUTO_STOP();

	return false;
}

bool TestClient_GameState_LuaPostArrayCB(ParseTable pti[], void *pStruct, int column, void *pCBData)
{
	lua_State *L = pCBData;

	PERFINFO_AUTO_START_FUNC();

	if(pStruct != spLastGameStateStruct)
	{
		spLastGameStateStruct = pStruct;

		if(sbExpectGameStateStructChange)
			sbExpectGameStateStructChange = false;
		else
			lua_pop(L, 1);
	}

	if(TokenStoreStorageTypeIsAnArray(TokenStoreGetStorageType(pti[column].type)) && TokenStoreGetNumElems(pti, column, pStruct, NULL) > 0)
		lua_pop(L, 1);

	PERFINFO_AUTO_STOP();

	return false;
}

bool TestClient_GameState_LuaTraverseCB(ParseTable pti[], void *pStruct, int column, int index, void *pCBData)
{
	lua_State *L = pCBData;

	PERFINFO_AUTO_START_FUNC();

	if(pStruct != spLastGameStateStruct)
	{
		spLastGameStateStruct = pStruct;

		if(sbExpectGameStateStructChange)
			sbExpectGameStateStructChange = false;
		else
			lua_pop(L, 1);
	}

	if(pti[column].type == TOK_START || pti[column].type == TOK_END)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	if(TOK_HAS_SUBTABLE(pti[column].type))
	{
		void *pSubStruct = TokenStoreGetPointer(pti, column, pStruct, index, NULL);

		if(TokenStoreStorageTypeIsAnArray(TokenStoreGetStorageType(pti[column].type)))
		{
			ParseTable *pSubTable = pti[column].subtable;
			int key = ParserGetTableKeyColumn(pSubTable);

			if(key == -1)
			{
				lua_pushinteger(L, index+1);
				lua_newtable(L);
				lua_settable(L, -3);

				if(pSubStruct)
				{
					sbExpectGameStateStructChange = true;
					lua_pushinteger(L, index + 1);
					lua_gettable(L, -2);
				}
			}
			else if(TypeIsInt(TOK_GET_TYPE(pSubTable[key].type)))
			{
				// pSubStruct almost has to exist
				int iSubKey = TokenStoreGetInt(pSubTable, key, pSubStruct, 0, NULL);

				sbExpectGameStateStructChange = true;
				lua_pushinteger(L, iSubKey);
				lua_newtable(L);
				lua_settable(L, -3);
				lua_pushinteger(L, iSubKey);
				lua_gettable(L, -2);
			}
			else
			{
				// pSubStruct almost has to exist
				const char *pSubKey = TokenStoreGetString(pSubTable, key, pSubStruct, 0, NULL);

				sbExpectGameStateStructChange = true;
				lua_pushstring(L, pSubKey);
				lua_newtable(L);
				lua_settable(L, -3);
				lua_pushstring(L, pSubKey);
				lua_gettable(L, -2);
			}
		}
		else if(pSubStruct)
		{
			sbExpectGameStateStructChange = true;
			lua_pushstring(L, pti[column].name);
			lua_newtable(L);
			lua_settable(L, -3);
			lua_pushstring(L, pti[column].name);
			lua_gettable(L, -2);
		}
	}
	else
	{
		if(TokenStoreStorageTypeIsAnArray(TokenStoreGetStorageType(pti[column].type)))
			lua_pushinteger(L, index + 1);
		else
			lua_pushstring(L, pti[column].name);

		if(TypeIsInt(TOK_GET_TYPE(pti[column].type)))
		{
			S64 iValue = TokenStoreGetIntAuto(pti, column, pStruct, index, NULL);
			lua_pushinteger(L, iValue);
			lua_settable(L, -3);
		}
		else if(TOK_GET_TYPE(pti[column].type) & TOK_F32_X)
		{
			F32 fValue = TokenStoreGetF32(pti, column, pStruct, index, NULL);
			lua_pushnumber(L, fValue);
			lua_settable(L, -3);
		}
		else if(TOK_GET_TYPE(pti[column].type) & TOK_STRING_X)
		{
			const char *pcValue = TokenStoreGetString(pti, column, pStruct, index, NULL);
			lua_pushstring(L, pcValue);
			lua_settable(L, -3);
		}
		else
			lua_pop(L, 1);
	}

	PERFINFO_AUTO_STOP();

	return false;
}

GLUA_FUNC(client_game_state)
{
	GLUA_ARG_COUNT_CHECK(client_game_state, 0, 0);
	glua_pushTable();
	spLastGameStateStruct = NULL;
	sbExpectGameStateStructChange = true;
	ParserTraverseParseTable(parse_TestClientStateUpdate, gTestClientGlobalState.pLatestState, 0, TOK_PARSETABLE_INFO, TestClient_GameState_LuaTraverseCB, TestClient_GameState_LuaPreArrayCB, TestClient_GameState_LuaPostArrayCB, _lua_);
}
GLUA_END*/

void TestClient_RegisterLuaFuncs(LuaContext *pContext)
{
	lua_State *_lua_ = pContext->luaState;
	int namespaceTbl = _glua_create_tbl(_lua_, "tc", 0);

	GLUA_DECL(namespaceTbl)
	{
		glua_func2(log, tc_log),
		glua_func(exit),
		glua_func(execute),
		glua_func(get_status),
		glua_func(local_execute),
		glua_func(register_tick),
		glua_func(get_script),
		glua_func(get_time),
		glua_func(get_id),
		glua_func(get_fps),

		glua_func(prod_mode),
		glua_func(version),
		glua_func(game_full),
		glua_func(get_exec_dir),
		glua_func(set_exec_dir),

		// Client status
		glua_func(client_exe_state),
		glua_func(client_fsm_state),
// 		glua_func(client_game_state),
		glua_const2(NOT_RUNNING, CC_NOT_RUNNING),
		glua_const2(RUNNING, CC_RUNNING),
		glua_const2(CONNECTED, CC_CONNECTED),
		glua_const2(CRASHED, CC_CRASHED),
		glua_const2(CRASH_COMPLETE, CC_CRASH_COMPLETE),
// 		glua_func( reset ),
// 		glua_func( status ),
// 		glua_func( send_to_owner ),
// 		glua_func2( log, tc_log ),
// 		glua_func( error ),
// 		glua_enum( "packet_corruption_type", &tag_packet_corruption ),
// 		glua_const( TC_PACKETCORRUPTION_DEFAULT )
	}
	GLUA_DECL_END
}

void TestClient_InitLua(void)
{
	lua_State *L = NULL;

	// Set up the Lua script/module dirs
	luaSetScriptDir("server/TestClient/scripts");
	luaSetModuleDir("server/TestClient/modules");

	// Initialize the Lua context and register our functions
	sLuaContext = luaContextCreate();
	L = sLuaContext->luaState;
	TestClient_RegisterLuaFuncs(sLuaContext);

	// Read in the command script
	if(!luaLoadScript(sLuaContext, gTestClientGlobalState.cScriptName, true))
	{
		return;
	}

	luaInitVars(sLuaContext);
	lua_getglobal(L, "OnLoad");

	if(!lua_isfunction(L, -1))
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "Lua script \"%s\" has no OnLoad function - it will be unable to function!\n", gTestClientGlobalState.cScriptName);
		return;
	}

	if(!luaContextCall(sLuaContext, 0))
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "%s\n", luaContextGetLastError(sLuaContext));
	}
}

void TestClient_LuaExecute(const char *script)
{
	if(!luaLoadScriptRaw(sLuaContext, "issued command", script, (int)strlen(script), true))
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "%s\n", luaContextGetLastError(sLuaContext));
	}
}

void TestClient_LuaTick(void)
{
	lua_State *L = sLuaContext->luaState;
	int i;

	for(i = 0; i < eaiSize(&spiCallbacks); ++i)
	{
		lua_getref(L, spiCallbacks[i]);

		if(!luaContextCall(sLuaContext, 0))
		{
			printfColor(COLOR_RED | COLOR_BRIGHT, "%s\n", luaContextGetLastError(sLuaContext));
		}
	}
}