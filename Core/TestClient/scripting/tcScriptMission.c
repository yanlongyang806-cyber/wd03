#if 0

#include "tcScriptInternal.h"
#include "tcScriptShared.h"

#include "ClientControllerLib.h"
#include "earray.h"
#include "EString.h"
#include "TestClientCommon.h"

extern TestClientGlobalState gTestClientGlobalState;

#define GET_MISSION_TAG(p) TestClientMission *p; \
	p = eaIndexedGetUsingString(&gTestClientGlobalState.pLatestState->ppMyMissions, ((TestClientMissionWrapper*)obj)->indexName); \
	if(!p) { glua_pushNil(); break; }
#define GET_MISSION_TAG_WITH_FAIL(p, func, err)	TestClientMission *p; \
	p = eaIndexedGetUsingString(&gTestClientGlobalState.pLatestState->ppMyMissions, ((TestClientMissionWrapper*)obj)->indexName); \
	if(!p) { func((err)); break; }

GLUA_VAR_FUNC_DECLARE( mission_name );
GLUA_VAR_FUNC_DECLARE( mission_scoped_name );
GLUA_VAR_FUNC_DECLARE( mission_is_child );
GLUA_VAR_FUNC_DECLARE( mission_has_children );
GLUA_VAR_FUNC_DECLARE( mission_in_progress );
GLUA_VAR_FUNC_DECLARE( mission_completed );
GLUA_VAR_FUNC_DECLARE( mission_succeeded );
GLUA_VAR_FUNC_DECLARE( mission_needs_return );
GLUA_VAR_FUNC_DECLARE( mission_level );
GLUA_VAR_FUNC_DECLARE( mission_parent );
GLUA_VAR_FUNC_DECLARE( mission_children );

GLUA_TAG_DEFINE( tag_mission )
{
	GLUA_TAG_VARIABLE( "name", mission_name, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "scoped_name", mission_scoped_name, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "is_child", mission_is_child, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "has_children", mission_has_children, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "in_progress", mission_in_progress, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "completed", mission_completed, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "succeeded", mission_succeeded, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "needs_return", mission_needs_return, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "level", mission_level, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "parent", mission_parent, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "children", mission_children, GLUA_VAR_READONLY )
}
GLUA_TAG_END( tag_mission );

GLUA_VAR_FUNC( mission_name )
{
	GET_MISSION_TAG_WITH_FAIL(pMission, glua_pushString, "INVALID");

	glua_pushString(pMission->pchName);
}
GLUA_END

GLUA_VAR_FUNC( mission_scoped_name )
{
	GET_MISSION_TAG_WITH_FAIL(pMission, glua_pushString, "INVALID");

	glua_pushString(pMission->pchIndexName);
}
GLUA_END

GLUA_VAR_FUNC( mission_is_child )
{
	GET_MISSION_TAG_WITH_FAIL(pMission, glua_pushBoolean, false);

	glua_pushBoolean(pMission->bIsChild);
}
GLUA_END

GLUA_VAR_FUNC( mission_has_children )
{
	GET_MISSION_TAG_WITH_FAIL(pMission, glua_pushBoolean, false);

	glua_pushBoolean(eaSize(&pMission->ppChildren) > 0);
}
GLUA_END

GLUA_VAR_FUNC( mission_in_progress )
{
	GET_MISSION_TAG_WITH_FAIL(pMission, glua_pushBoolean, false);

	glua_pushBoolean(pMission->bInProgress);
}
GLUA_END

GLUA_VAR_FUNC( mission_completed )
{
	GET_MISSION_TAG_WITH_FAIL(pMission, glua_pushBoolean, false);

	glua_pushBoolean(pMission->bCompleted);
}
GLUA_END

GLUA_VAR_FUNC( mission_succeeded )
{
	GET_MISSION_TAG_WITH_FAIL(pMission, glua_pushBoolean, false);

	glua_pushBoolean(pMission->bSucceeded);
}
GLUA_END

GLUA_VAR_FUNC( mission_needs_return )
{
	GET_MISSION_TAG_WITH_FAIL(pMission, glua_pushBoolean, false);

	glua_pushBoolean(pMission->bNeedsReturn);
}
GLUA_END

GLUA_VAR_FUNC( mission_level )
{
	GET_MISSION_TAG_WITH_FAIL(pMission, glua_pushInteger, 0);

	glua_pushInteger(pMission->iLevel);
}
GLUA_END

GLUA_VAR_FUNC( mission_parent )
{
	TestClientMissionWrapper mission = {0};
	GET_MISSION_TAG(pMission);

	if(!pMission->pchParent)
	{
		glua_pushNil();
	}

	strcpy(mission.indexName, pMission->pchParent);
	TestClient_PushMission(&mission);
}
GLUA_END

GLUA_VAR_FUNC( mission_children )
{
	TestClientMissionWrapper mission = {0};
	int i;
	GET_MISSION_TAG(pMission);

	glua_pushTable();

	if(eaSize(&pMission->ppChildren) == 0)
	{
		break;
	}

	for(i = 0; i < eaSize(&pMission->ppChildren); ++i)
	{
		strcpy(mission.indexName, pMission->ppChildren[i]);
		TestClient_PushMission(&mission);
		glua_tblKey(mission.indexName);
	}
}
GLUA_END

GLUA_FUNC( in_contact_dialog )
{
	glua_pushBoolean(gTestClientGlobalState.pLatestState->pContactDialog != NULL);
}
GLUA_END

GLUA_FUNC( get_contact_dialog_text )
{
	if(!gTestClientGlobalState.pLatestState->pContactDialog)
	{
		glua_pushNil();
		glua_pushNil();
		break;
	}

	glua_pushString(gTestClientGlobalState.pLatestState->pContactDialog->pchText1);
	glua_pushString(gTestClientGlobalState.pLatestState->pContactDialog->pchText2);
}
GLUA_END

GLUA_FUNC( get_contact_dialog_options )
{
	int i;

	glua_pushTable();

	if(!gTestClientGlobalState.pLatestState->pContactDialog)
	{
		break;
	}

	for(i = 0; i < eaSize(&gTestClientGlobalState.pLatestState->pContactDialog->ppOptions); ++i)
	{
		glua_pushTable();
		glua_pushString(gTestClientGlobalState.pLatestState->pContactDialog->ppOptions[i]->pchKey);
		glua_tblKey("key");
		glua_pushString(gTestClientGlobalState.pLatestState->pContactDialog->ppOptions[i]->pchName);
		glua_tblKey("name");
		glua_pushBoolean(gTestClientGlobalState.pLatestState->pContactDialog->ppOptions[i]->bIsMission);
		glua_tblKey("is_mission");
		glua_pushBoolean(gTestClientGlobalState.pLatestState->pContactDialog->ppOptions[i]->bIsMissionComplete);
		glua_tblKey("is_completed_mission");
		glua_tblKey_i(i+1);
	}
}
GLUA_END

GLUA_FUNC( contact_respond )
{
	int iOption;
	const char *pchOption = NULL;
	char *cmd = NULL;

	GLUA_ARG_COUNT_CHECK( contact_respond, 1, 1 );
	GLUA_ARG_CHECK( contact_respond, 1, GLUA_TYPE_STRING | GLUA_TYPE_INT, false, "contact dialog option" );

	if(!gTestClientGlobalState.pLatestState->pContactDialog)
	{
		break;
	}

	if(glua_type(1) == GLUA_TYPE_INT)
	{
		iOption = glua_getInteger(1);
		estrPrintf(&cmd, "TestClientContactRespond \"%s\" \"%s\"", gTestClientGlobalState.pLatestState->pContactDialog->ppOptions[iOption-1]->pchKey, gTestClientGlobalState.pLatestState->pContactDialog->bHasReward ? gTestClientGlobalState.pLatestState->pContactDialog->rewardChoices[0] : "");
	}
	else
	{
		pchOption = glua_getString(1);
		estrPrintf(&cmd, "TestClientContactRespond \"%s\" \"%s\"", pchOption, gTestClientGlobalState.pLatestState->pContactDialog->bHasReward ? gTestClientGlobalState.pLatestState->pContactDialog->rewardChoices[0] : "");
	}

	ClientController_SendCommandToClient(cmd);
	estrDestroy(&cmd);
}
GLUA_END

GLUA_FUNC( get_missions )
{
	TestClientMissionWrapper mission = {0};
	int i;

	GLUA_ARG_COUNT_CHECK( get_missions, 0, 0 );

	glua_pushTable();

	if(eaSize(&gTestClientGlobalState.pLatestState->ppMyMissions) == 0)
	{
		break;
	}

	for(i = 0; i < eaSize(&gTestClientGlobalState.pLatestState->ppMyMissions); ++i)
	{
		if(gTestClientGlobalState.pLatestState->ppMyMissions[i]->bIsChild)
		{
			continue;
		}

		strcpy(mission.indexName, gTestClientGlobalState.pLatestState->ppMyMissions[i]->pchIndexName);
		TestClient_PushMission(&mission);
		glua_tblKey(mission.indexName);
	}
}
GLUA_END

GLUA_FUNC( get_num_missions )
{
	GLUA_ARG_COUNT_CHECK( get_num_missions, 0, 0 );

	glua_pushInteger(eaSize(&gTestClientGlobalState.pLatestState->ppMyMissions));
}
GLUA_END

GLUA_FUNC( get_scoped_mission )
{
	TestClientMissionWrapper mission = {0};
	int i;

	GLUA_ARG_COUNT_CHECK( get_scoped_mission, 1, 1 );
	GLUA_ARG_CHECK( get_scoped_mission, 1, GLUA_TYPE_STRING, false, "scoped mission name" );

	if(eaSize(&gTestClientGlobalState.pLatestState->ppMyMissions) == 0 ||
		(i = eaIndexedFindUsingString(&gTestClientGlobalState.pLatestState->ppMyMissions, glua_getString(1))) == -1)
	{
		glua_pushNil();
		break;
	}

	strcpy(mission.indexName, gTestClientGlobalState.pLatestState->ppMyMissions[i]->pchIndexName);
	TestClient_PushMission(&mission);
}
GLUA_END

void *_TestClient_GetMission(lua_State *L, int argn)
{
	return _glua_getUserdata(L, argn, tag_mission, NULL);
}

void _TestClient_PushMission(lua_State *L, TestClientMissionWrapper *obj)
{
	_glua_pushuserdata(L, tag_mission, obj, sizeof(*obj), NULL);
}

void TestClient_InitMission(LuaContext *pContext)
{
	lua_State *_lua_ = pContext->luaState;
	int namespaceTbl = _glua_create_tbl(_lua_, "tc", 0);

	GLUA_DECL( namespaceTbl )
	{
		glua_func( in_contact_dialog ),
		glua_func( get_contact_dialog_options ),
		glua_func( get_contact_dialog_text ),
		glua_func( contact_respond ),
		glua_func( get_missions ),
		glua_func( get_num_missions ),
		glua_func( get_scoped_mission ),
		glua_tag( "mission", &tag_mission ),
		glua_tagdispatch( tag_mission ),
	}
	GLUA_DECL_END
}

#endif