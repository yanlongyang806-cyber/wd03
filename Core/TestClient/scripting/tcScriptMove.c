#if 0

#include "tcScriptInternal.h"
#include "tcScriptShared.h"

#include "ClientControllerLib.h"
#include "EString.h"
#include "GlobalTypeEnum.h"
#include "mathutil.h"
#include "TestClientCommon.h"

#include "Autogen/GameClientLib_commands_autogen_CommandFuncs.h"
#include "AutoGen/UtilitiesLib_commands_autogen_CommandFuncs.h"

extern TestClientGlobalState gTestClientGlobalState;

GLUA_FUNC( get_pos )
{
	GLUA_ARG_COUNT_CHECK( get_pos, 0, 0 );

	if(!gTestClientGlobalState.pLatestState->pMyEnt)
	{
		glua_pushNumber(0.0);
		glua_pushNumber(0.0);
		glua_pushNumber(0.0);
		break;
	}

	glua_pushNumber(gTestClientGlobalState.pLatestState->pMyEnt->vPos[0]);
	glua_pushNumber(gTestClientGlobalState.pLatestState->pMyEnt->vPos[1]);
	glua_pushNumber(gTestClientGlobalState.pLatestState->pMyEnt->vPos[2]);
}
GLUA_END

GLUA_FUNC( get_pyr )
{
	GLUA_ARG_COUNT_CHECK( get_pyr, 0, 0 );

	if(!gTestClientGlobalState.pLatestState->pMyEnt)
	{
		glua_pushNumber(0.0);
		glua_pushNumber(0.0);
		glua_pushNumber(0.0);
		break;
	}

	glua_pushNumber(gTestClientGlobalState.pLatestState->pMyEnt->vPyr[0]);
	glua_pushNumber(gTestClientGlobalState.pLatestState->pMyEnt->vPyr[1]);
	glua_pushNumber(gTestClientGlobalState.pLatestState->pMyEnt->vPyr[2]);
}
GLUA_END

GLUA_FUNC( set_pos )
{
	Vec3 newPos;

	GLUA_ARG_COUNT_CHECK( set_pos, 3, 3	);
	GLUA_ARG_CHECK( set_pos, 1, GLUA_TYPE_NUMBER | GLUA_TYPE_INT, false, "" );
	GLUA_ARG_CHECK( set_pos, 2, GLUA_TYPE_NUMBER | GLUA_TYPE_INT, false, "" );
	GLUA_ARG_CHECK( set_pos, 3, GLUA_TYPE_NUMBER | GLUA_TYPE_INT, false, "" );

	newPos[0] = glua_getNumber(1);
	newPos[1] = glua_getNumber(2);
	newPos[2] = glua_getNumber(3);

	cmd_setpos(newPos);
}
GLUA_END

GLUA_FUNC( set_pyr )
{
	Vec3 newPyr;

	GLUA_ARG_COUNT_CHECK( set_pyr, 3, 3 );
	GLUA_ARG_CHECK( set_pyr, 1, GLUA_TYPE_NUMBER | GLUA_TYPE_INT, false, "" );
	GLUA_ARG_CHECK( set_pyr, 2, GLUA_TYPE_NUMBER | GLUA_TYPE_INT, false, "" );
	GLUA_ARG_CHECK( set_pyr, 3, GLUA_TYPE_NUMBER | GLUA_TYPE_INT, false, "" );

	newPyr[0] = glua_getNumber(1);
	newPyr[1] = glua_getNumber(2);
	newPyr[2] = glua_getNumber(3);

	cmd_setpyr(newPyr);
}
GLUA_END

GLUA_FUNC( move_stop )
{
	GLUA_ARG_COUNT_CHECK( move_stop, 0, 0 );

	ClientController_SendCommandToClient("ec me mmAttachToClient me");
	ClientController_SendCommandToClient("SetFollow 0");
	ClientController_SendCommandToClient("forward 0");
	ClientController_SendCommandToClient("backward 0");
	ClientController_SendCommandToClient("left 0");
	ClientController_SendCommandToClient("right 0");
	ClientController_SendCommandToClient("turn_left 0");
	ClientController_SendCommandToClient("turn_right 0");
	ClientController_SendCommandToClient("up 0");
	ClientController_SendCommandToClient("down 0");
	ClientController_SendCommandToClient("ThrottleSet 0.0");
}
GLUA_END

GLUA_FUNC( get_map )
{
	glua_pushString(gTestClientGlobalState.pLatestState->pchMapName);
}
GLUA_END

GLUA_FUNC( move_to_map )
{
	char *pCmd = NULL;

	GLUA_ARG_COUNT_CHECK( move_to_map, 1, 1 );
	GLUA_ARG_CHECK( move_to_map, 1, GLUA_TYPE_STRING, false, "" );

	estrPrintf(&pCmd, "mapmove %s", glua_getString(1));
	ClientController_SendCommandToClient(pCmd);
	estrDestroy(&pCmd);
}
GLUA_END

GLUA_FUNC( is_in_space )
{
	glua_pushBoolean(gTestClientGlobalState.pLatestState->bSTOSpaceshipMovement);
}
GLUA_END

void TestClient_InitMove(LuaContext *pContext)
{
	lua_State *_lua_ = pContext->luaState;
	int namespaceTbl = _glua_create_tbl(_lua_, "tc", 0);

	GLUA_DECL( namespaceTbl )
	{
		glua_func( get_pos ),
		glua_func( get_pyr ),
		glua_func( set_pos ),
		glua_func( set_pyr ),
		glua_func( move_stop ),
		glua_func( get_map ),
		glua_func( move_to_map ),
		glua_func( is_in_space )
	}
	GLUA_DECL_END
}

#endif