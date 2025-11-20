#if 0

#include "tclScriptInternal.h"
#include "tclScriptShared.h"
#include "TestClientLib.h"
#include "TestClientLib_Scripting.h"
#include "error.h"
#include "EString.h"
#include "GlobalTypes.h"
#include "timing.h"

#include "../../CrossRoads/GameClientLib/Autogen/GameClientLib_commands_autogen_CommandFuncs.h"

TestClientEventCallback **gppCallbacks = NULL;
TestClientCallbackType *gpEvents = NULL;
int gCallbacks = 0;
static tag_int tag_callback_type;
bool gbFirstTime = true;


extern bool gbInitState;
extern bool gbScriptLoaded;
extern U32 gLuaTimer;
extern LuaContext *gLuaContext;
extern TestClientGlobalState gTestClientGlobals;

int TestClientLib_RegisterTimedCallback(int iFuncRef, F32 fDelay, bool bPersist)
{
	TestClientEventCallback *pCallback = calloc(1, sizeof(TestClientEventCallback));

	pCallback->iCallbackHandle = ++gCallbacks;
	pCallback->iCallbackRef = iFuncRef;
	pCallback->eType = TC_CALLBACK_TICK;
	pCallback->fCallbackDelay = fDelay;
	pCallback->fLastCallback = timerElapsed(gLuaTimer);
	pCallback->bPersistent = bPersist;

	eaPush(&gppCallbacks, pCallback);
	return pCallback->iCallbackHandle;
}

int TestClientLib_RegisterEventCallback(TestClientCallbackType eType, int iFuncRef, bool bPersist)
{
	TestClientEventCallback *pCallback = calloc(1, sizeof(TestClientEventCallback));

	pCallback->iCallbackHandle = ++gCallbacks;
	pCallback->iCallbackRef = iFuncRef;
	pCallback->eType = eType;
	pCallback->fCallbackDelay = 0.0f;
	pCallback->fLastCallback = timerElapsed(gLuaTimer);
	pCallback->bPersistent = bPersist;

	eaPush(&gppCallbacks, pCallback);
	return pCallback->iCallbackHandle;
}

int TestClientLib_RegisterNotifyCallback(char *pchNotifyName, int iFuncRef, bool bPersist)
{
	TestClientEventCallback *pCallback = calloc(1, sizeof(TestClientEventCallback));

	pCallback->iCallbackHandle = ++gCallbacks;
	pCallback->iCallbackRef = iFuncRef;
	pCallback->eType = TC_CALLBACK_NOTIFY;
	pCallback->fCallbackDelay = 0.0f;
	pCallback->fLastCallback = timerElapsed(gLuaTimer);
	pCallback->bPersistent = bPersist;
	estrCopy2(&pCallback->pchNotifyName, pchNotifyName);

	eaPush(&gppCallbacks, pCallback);
	return pCallback->iCallbackHandle;
}

// We don't really care what kind of callback is being unregistered, to be honest
bool TestClientLib_UnregisterCallback(int iCallbackHandle)
{
	if(iCallbackHandle <= 0)
	{
		return false;
	}

	FOR_EACH_IN_EARRAY(gppCallbacks, TestClientEventCallback, pCallback)
	{
		if(pCallback && pCallback->iCallbackHandle == iCallbackHandle)
		{
			pCallback->bRemove = true;
			return true;
		}
	}
	FOR_EACH_END

	return false;
}

void TestClientLib_Dispatch(TestClientCallbackType eType)
{
	lua_State *L = gLuaContext->luaState;

	if(eType == TC_CALLBACK_PRESPAWN)
	{
		if(gbScriptLoaded)
		{
			lua_getglobal(L, "OnPreSpawn");

			if(lua_isfunction(L, lua_gettop(L)) && !luaContextCall(gLuaContext, 0))
			{
				const char *pError = luaContextGetLastError(gLuaContext);
				printf("%s\n", pError);
			}
		}
	}
	else if(eType == TC_CALLBACK_SPAWN)
	{
		gbInitState = false;

		if(gbScriptLoaded)
		{
			if(gbFirstTime)
			{
				gbFirstTime = false;
				lua_getglobal(L, "OnFirstSpawn");

				if(lua_isfunction(L, lua_gettop(L)) && !luaContextCall(gLuaContext, 0))
				{
					const char *pError = luaContextGetLastError(gLuaContext);
					printf("%s\n", pError);
				}
			}

			lua_getglobal(L, "OnSpawn");

			if(lua_isfunction(L, lua_gettop(L)) && !luaContextCall(gLuaContext, 0))
			{
				const char *pError = luaContextGetLastError(gLuaContext);
				printf("%s\n", pError);
			}
		}

		cmd_setpos(gTestClientGlobals.curPos);
		gTestClientGlobals.bInitedPos = true;
	}
	else
	{
		eaiPush(&gpEvents, eType);
	}
}

void TestClientLib_IssueCallbacksForType(TestClientCallbackType eType)
{
	int args = 0;
	TestClientChatEntry *pChat = NULL;
	TestClientNotifyEntry *pNotify = NULL;
	lua_State *L = gLuaContext->luaState;

	FOR_EACH_IN_EARRAY_FORWARDS(gppCallbacks, TestClientEventCallback, pCallback)
	{
		if(pCallback->eType != eType || pCallback->bRemove)
		{
			continue;
		}

		if(timerElapsed(gLuaTimer) - pCallback->fLastCallback < pCallback->fCallbackDelay)
		{
			continue;
		}

		pCallback->fLastCallback = timerElapsed(gLuaTimer);
		lua_rawgeti(L, LUA_REGISTRYINDEX, pCallback->iCallbackRef);

		// Set up custom parameters to be pushed
		switch(eType)
		{
		case TC_CALLBACK_CHAT:
			if(!pChat)
			{
				pChat = eaRemove(&gTestClientGlobals.ppQueuedChat, 0);
			}

			lua_pushstring(L, pChat->pChannel);
			lua_pushstring(L, pChat->pSender);
			lua_pushstring(L, pChat->pMessage);
			args = 3;
		xcase TC_CALLBACK_NOTIFY:
			if(!pNotify)
			{
				pNotify = eaRemove(&gTestClientGlobals.ppNotifications, 0);
			}

			lua_pushstring(L, pNotify->pName);
			lua_pushstring(L, pNotify->pObject);
			lua_pushstring(L, pNotify->pString);
			args = 3;
			break;
		}

		if(!luaContextCall(gLuaContext, args))
		{
			const char *pError = luaContextGetLastError(gLuaContext);
			printf("%s\n", pError);
		}

		if(!pCallback->bPersistent)
		{
			pCallback->bRemove = true;
		}
	}
	FOR_EACH_END

	if(pChat)
	{
		estrDestroy(&pChat->pChannel);
		estrDestroy(&pChat->pSender);
		estrDestroy(&pChat->pMessage);
		free(pChat);
	}

	if(pNotify)
	{
		estrDestroy(&pNotify->pName);
		estrDestroy(&pNotify->pObject);
		estrDestroy(&pNotify->pString);
		free(pNotify);
	}
}

void TestClientLib_CallbackTick(void)
{
	lua_State *L = gLuaContext->luaState;
	const char *pError = NULL;
	TestClientCallbackType *pEvents = NULL;
	int args = 0;
	int i = 0;

	eaiCopy(&pEvents, &gpEvents);
	eaiPush(&pEvents, TC_CALLBACK_TICK);
	eaiClear(&gpEvents);
		
	for(i = 0; i < eaiSize(&pEvents); ++i)
	{
		if(pEvents[i] == TC_CALLBACK_LUA_COMMAND)
		{
			char *pCommand = eaRemove(&gTestClientGlobals.ppLuaCommands, 0);

			if(!luaContextLoadScript(gLuaContext, NULL, pCommand, (int)strlen(pCommand)))
			{
				pError = luaContextGetLastError(gLuaContext);
				printf("%s\n", pError);
			}

			estrDestroy(&pCommand);
		}
		else
		{
			TestClientLib_IssueCallbacksForType(pEvents[i]);
		}
	}

	FOR_EACH_IN_EARRAY(gppCallbacks, TestClientEventCallback, pCallback)
	{
		if(pCallback->bRemove)
		{
			eaRemove(&gppCallbacks, ipCallbackIndex);
			estrDestroy(&pCallback->pchNotifyName);
			free(pCallback);
		}
	}
	FOR_EACH_END

	eaiClear(&pEvents);
}

GLUA_FUNC( register_tick )
{
	F32 fDelay = 1.0f;
	int iRef;

	GLUA_ARG_COUNT_CHECK( register_tick, 1, 2 );
	GLUA_ARG_CHECK( register_tick, 1, GLUA_TYPE_FUNCTION, false, "" );
	GLUA_ARG_CHECK( register_tick, 2, GLUA_TYPE_INT | GLUA_TYPE_NUMBER, true, "" );

	if(glua_argn() > 1)
	{
		fDelay = glua_getNumber(2);
		lua_pop(_lua_, 1);
	}

	iRef = lua_ref(_lua_, 1);
	glua_pushInteger(TestClientLib_RegisterTimedCallback(iRef, fDelay, true));
}
GLUA_END

GLUA_FUNC( register_alarm )
{
	F32 fDelay = 0.001f;
	int iRef;

	GLUA_ARG_COUNT_CHECK( register_alarm, 1, 2 );
	GLUA_ARG_CHECK( register_alarm, 1, GLUA_TYPE_FUNCTION, false, "" );
	GLUA_ARG_CHECK( register_alarm, 2, GLUA_TYPE_INT | GLUA_TYPE_NUMBER, true, "" );

	if(glua_argn() > 1)
	{
		fDelay = glua_getNumber(2);
		lua_pop(_lua_, 1);
	}

	iRef = lua_ref(_lua_, 1);
	glua_pushInteger(TestClientLib_RegisterTimedCallback(iRef, fDelay, false));
}
GLUA_END

GLUA_FUNC( register_event )
{
	int iRef;
	bool bPersist = true;

	GLUA_ARG_COUNT_CHECK( register_event, 2, 3 );
	GLUA_ARG_CHECK( register_event, 1, GLUA_TYPE_USERDATA, false, "" );
	GLUA_ARG_CHECK( register_event, 2, GLUA_TYPE_FUNCTION, false, "" );
	GLUA_ARG_CHECK( register_event, 3, GLUA_TYPE_BOOLEAN, true, "" );

	if(glua_argn() > 2)
	{
		bPersist = glua_getBoolean(3);
		lua_pop(_lua_, 1);
	}

	iRef = lua_ref(_lua_, 1);
	
	glua_pushInteger(TestClientLib_RegisterEventCallback(glua_getEnum(1, tag_callback_type), iRef, bPersist));
}
GLUA_END

GLUA_FUNC( register_notify )
{
	int iRef;
	bool bPersist = true;

	GLUA_ARG_COUNT_CHECK( register_notify, 2, 3 );
	GLUA_ARG_CHECK( register_notify, 1, GLUA_TYPE_STRING | GLUA_TYPE_NIL, false, "" );
	GLUA_ARG_CHECK( register_notify, 2, GLUA_TYPE_FUNCTION, false, "" );
	GLUA_ARG_CHECK( register_notify, 3, GLUA_TYPE_BOOLEAN, true, "" );

	if(glua_argn() > 2)
	{
		bPersist = glua_getBoolean(3);
		lua_pop(_lua_, 1);
	}

	iRef = lua_ref(_lua_, 1);

	glua_pushInteger(TestClientLib_RegisterNotifyCallback(glua_isNil(1) ? "all" : glua_getString(1), iRef, bPersist));
}
GLUA_END

GLUA_FUNC( unregister_tick )
{
	GLUA_ARG_COUNT_CHECK( unregister_tick, 1, 1	);
	GLUA_ARG_CHECK( unregister_tick, 1, GLUA_TYPE_INT, false, "" );

	glua_pushBoolean(TestClientLib_UnregisterCallback(glua_getInteger(1)));
}
GLUA_END

GLUA_FUNC( unregister_alarm )
{
	GLUA_ARG_COUNT_CHECK( unregister_alarm, 1, 1 );
	GLUA_ARG_CHECK( unregister_alarm, 1, GLUA_TYPE_INT, false, "" );

	glua_pushBoolean(TestClientLib_UnregisterCallback(glua_getInteger(1)));
}
GLUA_END

GLUA_FUNC( unregister_event )
{
	GLUA_ARG_COUNT_CHECK( unregister_event, 1, 1 );
	GLUA_ARG_CHECK( unregister_event, 1, GLUA_TYPE_INT, false, "" );

	glua_pushBoolean(TestClientLib_UnregisterCallback(glua_getInteger(1)));
}
GLUA_END

GLUA_FUNC( unregister_notify )
{
	GLUA_ARG_COUNT_CHECK( unregister_notify, 1, 1 );
	GLUA_ARG_CHECK( unregister_notify, 1, GLUA_TYPE_INT, false, "" );

	glua_pushBoolean(TestClientLib_UnregisterCallback(glua_getInteger(1)));
}
GLUA_END

void tclScriptCallbacksInit(LuaContext *pContext)
{
	lua_State *_lua_ = pContext->luaState;
	int namespaceTbl = _glua_create_tbl(_lua_, "tc", 0);

	GLUA_DECL( namespaceTbl )
	{
		glua_func( register_tick ),
		glua_func( register_alarm ),
		glua_func( register_event ),
		glua_func( register_notify ),
		glua_func( unregister_tick ),
		glua_func( unregister_alarm ),
		glua_func( unregister_event ),
		glua_func( unregister_notify ),
		glua_enum( "callback_type", &tag_callback_type ),
		glua_const( TC_CALLBACK_CHAT ),
		glua_const( TC_CALLBACK_DEATH ),
		glua_const( TC_CALLBACK_TEAM_INVITE ),
		glua_const( TC_CALLBACK_TEAM_REQUEST ),
		glua_const( TC_CALLBACK_MAP_CHANGE ),
		glua_const( TC_CALLBACK_CONTACT_DIALOG ),
		glua_const( TC_CALLBACK_ATTACKED ),
	}
	GLUA_DECL_END
}

#endif