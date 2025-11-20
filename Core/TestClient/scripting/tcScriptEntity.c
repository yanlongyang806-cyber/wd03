#if 0

#include "tcScriptInternal.h"
#include "tcScriptShared.h"

#include "ClientControllerLib.h"
#include "earray.h"
#include "EString.h"
#include "GlobalTypes.h"
#include "mathutil.h"
#include "StashTable.h"
#include "StringCache.h"
#include "TestClient.h"
#include "TestClientCommon.h"

#include "Autogen/GameClientLib_commands_autogen_CommandFuncs.h"

// -------------------------------------------------------------------------
// Entity tag declaration
// Represents an entity within the Lua context
// -------------------------------------------------------------------------
#define GET_ENTITY_TAG(p) TestClientEntity *p; \
							p = eaIndexedGetUsingInt(&gTestClientGlobalState.pLatestState->ppEnts, ((TestClientEntityWrapper*)obj)->iRef); \
							if(!p) { glua_pushNil(); break; }
#define GET_ENTITY_TAG_WITH_FAIL(p, func, err)	TestClientEntity *p; \
							p = eaIndexedGetUsingInt(&gTestClientGlobalState.pLatestState->ppEnts, ((TestClientEntityWrapper*)obj)->iRef); \
							if(!p) { func((err)); break; }

GLUA_VAR_FUNC_DECLARE( entity_name );
GLUA_VAR_FUNC_DECLARE( entity_ref );
GLUA_VAR_FUNC_DECLARE( entity_id );
GLUA_VAR_FUNC_DECLARE( entity_hp );
GLUA_VAR_FUNC_DECLARE( entity_max_hp );
GLUA_VAR_FUNC_DECLARE( entity_level );
GLUA_VAR_FUNC_DECLARE( entity_target );
GLUA_VAR_FUNC_DECLARE( entity_shields );
GLUA_VAR_FUNC_DECLARE( entity_pos );
GLUA_VAR_FUNC_DECLARE( entity_pyr );
GLUA_VAR_FUNC_DECLARE( entity_distance );
GLUA_VAR_FUNC_DECLARE( entity_dead );
GLUA_VAR_FUNC_DECLARE( entity_valid );
GLUA_VAR_FUNC_DECLARE( entity_hostile );
GLUA_VAR_FUNC_DECLARE( entity_exists );
GLUA_VAR_FUNC_DECLARE( entity_casting );
GLUA_VAR_FUNC_DECLARE( entity_has_mission );
GLUA_VAR_FUNC_DECLARE( entity_is_important );
GLUA_VAR_FUNC_DECLARE( entity_has_completed_mission );
GLUA_VAR_FUNC_DECLARE( entity_nearby_friends );
GLUA_VAR_FUNC_DECLARE( entity_nearby_hostiles );
GLUA_VAR_FUNC_DECLARE( entity_nearby_objects );

GLUA_TAG_DEFINE( tag_entity )
{
	GLUA_TAG_VARIABLE( "name", entity_name, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "ref", entity_ref, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "id", entity_id, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "hp", entity_hp, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "max_hp", entity_max_hp, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "shields", entity_shields, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "level", entity_level, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "target", entity_target, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "position", entity_pos, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "pyr", entity_pyr, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "distance", entity_distance, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "dead", entity_dead, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "valid", entity_valid, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "hostile", entity_hostile, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "exists", entity_exists, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "casting", entity_casting, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "has_mission", entity_has_mission, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "is_important", entity_is_important, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "has_completed_mission", entity_has_completed_mission, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "nearby_friends", entity_nearby_friends, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "nearby_hostiles", entity_nearby_hostiles, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "nearby_objects", entity_nearby_objects, GLUA_VAR_READONLY ),
}
GLUA_TAG_END( tag_entity )

// Access the entity's name
GLUA_VAR_FUNC( entity_name )
{
	GET_ENTITY_TAG_WITH_FAIL(pEnt, glua_pushString, "INVALID");
	glua_pushString(pEnt->pchName);
}
GLUA_END

// Access the entity's EntityRef
GLUA_VAR_FUNC( entity_ref )
{
	glua_pushInteger(((TestClientEntityWrapper *)obj)->iRef);
}
GLUA_END

// Access the entity's container ID
GLUA_VAR_FUNC( entity_id )
{
	GET_ENTITY_TAG_WITH_FAIL(pEnt, glua_pushInteger, 0);
	glua_pushInteger(pEnt->iID);
}
GLUA_END

// Access the entity's HP
GLUA_VAR_FUNC( entity_hp )
{
	GET_ENTITY_TAG_WITH_FAIL(pEnt, glua_pushNumber, 0.0);
	glua_pushNumber(pEnt->fHP);
}
GLUA_END

// Access the entity's max HP
GLUA_VAR_FUNC( entity_max_hp )
{
	GET_ENTITY_TAG_WITH_FAIL(pEnt, glua_pushNumber, 0.0);
	glua_pushNumber(pEnt->fMaxHP);
}
GLUA_END

GLUA_VAR_FUNC( entity_shields )
{
	int i;
	GET_ENTITY_TAG(pEnt);

	glua_pushTable();

	for(i = 0; i < 4; ++i)
	{
		glua_pushNumber(pEnt->fShields[i]);
		glua_tblKey_i(i+1);
	}
}
GLUA_END

// Access the entity's level
GLUA_VAR_FUNC( entity_level )
{
	GET_ENTITY_TAG_WITH_FAIL(pEnt, glua_pushInteger, 0);
	glua_pushInteger(pEnt->iLevel);
}
GLUA_END

// Access the entity's target
GLUA_VAR_FUNC( entity_target )
{
	TestClientEntityWrapper ent = {0};
	GET_ENTITY_TAG_WITH_FAIL(pEnt, TestClient_PushEntity, &ent);

	ent.iRef = pEnt->iTarget;
	TestClient_PushEntity(&ent);
}
GLUA_END

// Access the entity's position
GLUA_VAR_FUNC( entity_pos )
{
	GET_ENTITY_TAG(pEnt);

	glua_pushTable();
	glua_pushNumber(pEnt->vPos[0]);
	glua_tblKey_i(1);
	glua_pushNumber(pEnt->vPos[1]);
	glua_tblKey_i(2);
	glua_pushNumber(pEnt->vPos[2]);
	glua_tblKey_i(3);
}
GLUA_END

GLUA_VAR_FUNC( entity_pyr )
{
	GET_ENTITY_TAG(pEnt);

	glua_pushTable();
	glua_pushNumber(pEnt->vPyr[0]);
	glua_tblKey_i(1);
	glua_pushNumber(pEnt->vPyr[1]);
	glua_tblKey_i(2);
	glua_pushNumber(pEnt->vPyr[2]);
	glua_tblKey_i(3);
}
GLUA_END

// Access the entity's distance [from the Test Client]
GLUA_VAR_FUNC( entity_distance )
{
	GET_ENTITY_TAG_WITH_FAIL(pEnt, glua_pushNumber, 0.0);
	glua_pushNumber(pEnt->fDistance);
}
GLUA_END

// Find out if the entity is dead
GLUA_VAR_FUNC( entity_dead )
{
	GET_ENTITY_TAG_WITH_FAIL(pEnt, glua_pushBoolean, false);
	glua_pushBoolean(pEnt->bDead);
}
GLUA_END

GLUA_VAR_FUNC( entity_valid )
{
	GET_ENTITY_TAG_WITH_FAIL(pEnt, glua_pushBoolean, false);
	glua_pushBoolean(true);
}
GLUA_END

// Find out if the entity is hostile
GLUA_VAR_FUNC( entity_hostile )
{
	GET_ENTITY_TAG_WITH_FAIL(pEnt, glua_pushBoolean, false);
	glua_pushBoolean(pEnt->bHostile);
}
GLUA_END

// Find out if the entity exists
GLUA_VAR_FUNC( entity_exists )
{
	GET_ENTITY_TAG_WITH_FAIL(pEnt, glua_pushBoolean, false);
	glua_pushBoolean(true);
}
GLUA_END

// Find out if the entity is casting
GLUA_VAR_FUNC( entity_casting )
{
	GET_ENTITY_TAG_WITH_FAIL(pEnt, glua_pushBoolean, false);
	glua_pushBoolean(pEnt->bCasting);
}
GLUA_END

GLUA_VAR_FUNC( entity_has_mission )
{
	int i;

	GET_ENTITY_TAG_WITH_FAIL(pEnt, glua_pushBoolean, false);

	for(i = 0; i < eaSize(&gTestClientGlobalState.pLatestState->ppContacts); ++i)
	{
		if(pEnt->iRef == gTestClientGlobalState.pLatestState->ppContacts[i]->iRef)
		{
			glua_pushBoolean(gTestClientGlobalState.pLatestState->ppContacts[i]->bHasMission);
			break;
		}
	}

	if(i == eaSize(&gTestClientGlobalState.pLatestState->ppContacts))
	{
		glua_pushBoolean(false);
	}
}
GLUA_END

GLUA_VAR_FUNC( entity_is_important )
{
	int i;

	GET_ENTITY_TAG_WITH_FAIL(pEnt, glua_pushBoolean, false);

	for(i = 0; i < eaSize(&gTestClientGlobalState.pLatestState->ppContacts); ++i)
	{
		if(pEnt->iRef == gTestClientGlobalState.pLatestState->ppContacts[i]->iRef)
		{
			glua_pushBoolean(gTestClientGlobalState.pLatestState->ppContacts[i]->bIsImportant);
			break;
		}
	}

	if(i == eaSize(&gTestClientGlobalState.pLatestState->ppContacts))
	{
		glua_pushBoolean(false);
	}
}
GLUA_END

GLUA_VAR_FUNC( entity_has_completed_mission )
{
	int i;

	GET_ENTITY_TAG_WITH_FAIL(pEnt, glua_pushBoolean, false);

	for(i = 0; i < eaSize(&gTestClientGlobalState.pLatestState->ppContacts); ++i)
	{
		if(pEnt->iRef == gTestClientGlobalState.pLatestState->ppContacts[i]->iRef)
		{
			glua_pushBoolean(gTestClientGlobalState.pLatestState->ppContacts[i]->bHasMissionComplete);
			break;
		}
	}

	if(i == eaSize(&gTestClientGlobalState.pLatestState->ppContacts))
	{
		glua_pushBoolean(false);
	}
}
GLUA_END

GLUA_VAR_FUNC( entity_nearby_friends )
{
	int i;
	GET_ENTITY_TAG(pEnt);

	glua_pushTable();

	if(eaiSize(&pEnt->piNearbyFriends) == 0)
	{
		break;
	}

	for(i = 0; i < eaiSize(&pEnt->piNearbyFriends); ++i)
	{
		TestClientEntityWrapper ent;
		ent.iRef = pEnt->piNearbyFriends[i];
		TestClient_PushEntity(&ent);
		glua_tblKey_i(i+1);
	}
}
GLUA_END

GLUA_VAR_FUNC( entity_nearby_hostiles )
{
	int i;
	GET_ENTITY_TAG(pEnt);

	glua_pushTable();

	if(eaiSize(&pEnt->piNearbyHostiles) == 0)
	{
		break;
	}

	for(i = 0; i < eaiSize(&pEnt->piNearbyHostiles); ++i)
	{
		TestClientEntityWrapper ent;
		ent.iRef = pEnt->piNearbyHostiles[i];
		TestClient_PushEntity(&ent);
		glua_tblKey_i(i+1);
	}
}
GLUA_END

GLUA_VAR_FUNC( entity_nearby_objects )
{
	int i;
	GET_ENTITY_TAG(pEnt);

	glua_pushTable();

	if(eaSize(&pEnt->ppNearbyObjects) == 0)
	{
		break;
	}

	for(i = 0; i < eaSize(&pEnt->ppNearbyObjects); ++i)
	{
		TestClientObjectWrapper object;
		strcpy(object.key, pEnt->ppNearbyObjects[i]);
		TestClient_PushObject(&object);
		glua_tblKey_i(i+1);
	}
}
GLUA_END
// -------------------------------------------------------------------------

// -------------------------------------------------------------------------
// Object tag declaration
// Represents an interactible/destructible within the Lua context
// -------------------------------------------------------------------------
#define GET_OBJECT_TAG(p) TestClientObject *p; \
	p = eaIndexedGetUsingString(&gTestClientGlobalState.pLatestState->ppObjects, ((TestClientObjectWrapper*)obj)->key); \
	if(!p) { glua_pushNil(); break; }
#define GET_OBJECT_TAG_WITH_FAIL(p, func, err)	TestClientObject *p; \
	p = eaIndexedGetUsingString(&gTestClientGlobalState.pLatestState->ppObjects, ((TestClientObjectWrapper*)obj)->key); \
	if(!p) { func((err)); break; }

GLUA_VAR_FUNC_DECLARE( object_key );
GLUA_VAR_FUNC_DECLARE( object_name );
GLUA_VAR_FUNC_DECLARE( object_has_entity );
GLUA_VAR_FUNC_DECLARE( object_entity );
GLUA_VAR_FUNC_DECLARE( object_distance );
GLUA_VAR_FUNC_DECLARE( object_position );
GLUA_VAR_FUNC_DECLARE( object_is_door );
GLUA_VAR_FUNC_DECLARE( object_is_destructible );
GLUA_VAR_FUNC_DECLARE( object_is_clickable );
GLUA_VAR_FUNC_DECLARE( object_exists );

GLUA_TAG_DEFINE( tag_object )
{
	GLUA_TAG_VARIABLE( "key", object_key, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "name", object_name, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "has_entity", object_has_entity, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "entity", object_entity, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "distance", object_distance, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "position", object_position, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "is_door", object_is_door, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "is_destructible", object_is_destructible, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "is_clickable", object_is_clickable, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "exists", object_exists, GLUA_VAR_READONLY )
}
GLUA_TAG_END( tag_object )

GLUA_VAR_FUNC( object_key )
{
	glua_pushString(((TestClientObjectWrapper *)obj)->key);
}
GLUA_END

GLUA_VAR_FUNC( object_name )
{
	GET_OBJECT_TAG(pObj);

	glua_pushString(pObj->pchName);
}
GLUA_END

GLUA_VAR_FUNC( object_has_entity )
{
	GET_OBJECT_TAG_WITH_FAIL(pObj, glua_pushBoolean, false);

	glua_pushBoolean(pObj->iRef > 0);
}
GLUA_END

GLUA_VAR_FUNC( object_entity )
{
	TestClientEntityWrapper ent;
	GET_OBJECT_TAG(pObj);

	ent.iRef = pObj->iRef;
	TestClient_PushEntity(&ent);
}
GLUA_END

GLUA_VAR_FUNC( object_distance )
{
	GET_OBJECT_TAG_WITH_FAIL(pObj, glua_pushNumber, 0.0f);

	glua_pushNumber(pObj->fDistance);
}
GLUA_END

GLUA_VAR_FUNC( object_position )
{
	GET_OBJECT_TAG(pObj);

	glua_pushTable();
	glua_pushNumber(pObj->vPos[0]);
	glua_tblKey_i(1);
	glua_pushNumber(pObj->vPos[1]);
	glua_tblKey_i(2);
	glua_pushNumber(pObj->vPos[2]);
	glua_tblKey_i(3);
}
GLUA_END

GLUA_VAR_FUNC( object_is_door )
{
	GET_OBJECT_TAG_WITH_FAIL(pObj, glua_pushBoolean, false);

	glua_pushBoolean(pObj->bDoor);
}
GLUA_END

GLUA_VAR_FUNC( object_is_destructible )
{
	GET_OBJECT_TAG_WITH_FAIL(pObj, glua_pushBoolean, false);

	glua_pushBoolean(pObj->bDestructible);
}
GLUA_END

GLUA_VAR_FUNC( object_is_clickable )
{
	GET_OBJECT_TAG_WITH_FAIL(pObj, glua_pushBoolean, false);

	glua_pushBoolean(pObj->bClickable);
}
GLUA_END

GLUA_VAR_FUNC( object_exists )
{
	GET_OBJECT_TAG_WITH_FAIL(pObj, glua_pushBoolean, false);

	glua_pushBoolean(true);
}
GLUA_END

// -------------------------------------------------------------------------
// Power tag declaration
// Represents a power within the Lua context
// -------------------------------------------------------------------------
GLUA_VAR_FUNC_DECLARE( power_name );
GLUA_VAR_FUNC_DECLARE( power_id );
GLUA_VAR_FUNC_DECLARE( power_hostile );
GLUA_VAR_FUNC_DECLARE( power_range );

GLUA_TAG_DEFINE( tag_power )
{
	GLUA_TAG_VARIABLE( "name", power_name, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "id", power_id, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "hostile", power_hostile, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "range", power_range, GLUA_VAR_READONLY )
}
GLUA_TAG_END( tag_power )

GLUA_VAR_FUNC( power_name )
{
	TestClientPower *pPower = (TestClientPower *)obj;

	glua_pushString(pPower->pchName);
}
GLUA_END

GLUA_VAR_FUNC( power_id )
{
	TestClientPower *pPower = (TestClientPower *)obj;

	glua_pushInteger(pPower->iID);
}
GLUA_END

GLUA_VAR_FUNC( power_hostile )
{
	TestClientPower *pPower = (TestClientPower *)obj;

	glua_pushBoolean(pPower->bAttack);
}
GLUA_END

GLUA_VAR_FUNC( power_range )
{
	TestClientPower *pPower = (TestClientPower *)obj;

	glua_pushNumber(pPower->fRange);
}
GLUA_END
// -------------------------------------------------------------------------

GLUA_FUNC( target )
{

	GLUA_ARG_COUNT_CHECK( target, 1, 1 );
	GLUA_ARG_CHECK( target, 1, GLUA_TYPE_USERDATA | GLUA_TYPE_NIL, false, "entity or object reference" );

	if(glua_isNil(1))
	{
		cmd_Target_Clear();
		break;
	}

	if(glua_isUserdata(1, tag_entity))
	{
		TestClientEntityWrapper *pEnt;
		pEnt = TestClient_GetEntity(1);
		cmd_TestClient_Target(pEnt->iRef);
	}
	else if(glua_isUserdata(1, tag_object))
	{
		TestClientObjectWrapper *pObj;
		pObj = TestClient_GetObject(1);
		cmd_TestClient_TargetObject(pObj->key);
	}
}
GLUA_END

GLUA_FUNC( assist )
{
	TestClientEntityWrapper *pEnt;

	GLUA_ARG_COUNT_CHECK( assist, 1, 1 );
	GLUA_ARG_CHECK( assist, 1, GLUA_TYPE_USERDATA | GLUA_TYPE_NIL, false, "entity reference" );

	if(glua_isNil(1))
	{
		cmd_Target_Clear();
		break;
	}

	pEnt = TestClient_GetEntity(1);
	cmd_TestClient_Assist(pEnt->iRef);
}
GLUA_END

GLUA_FUNC( get_target )
{
	TestClientEntityWrapper ent;

	GLUA_ARG_COUNT_CHECK( get_target, 0, 0 );

	if(!gTestClientGlobalState.pLatestState->pMyEnt)
	{
		glua_pushNil();
		break;
	}

	ent.iRef = gTestClientGlobalState.pLatestState->pMyEnt->iTarget;
	TestClient_PushEntity(&ent);
}
GLUA_END

GLUA_FUNC( get_self )
{
	TestClientEntityWrapper ent;

	GLUA_ARG_COUNT_CHECK( get_self, 0, 0 );

	ent.iRef = gTestClientGlobalState.pLatestState->iMyRef;
	TestClient_PushEntity(&ent);
}
GLUA_END

GLUA_FUNC( get_self_id )
{
	GLUA_ARG_COUNT_CHECK( get_self_id, 0, 0 );

	glua_pushInteger(gTestClientGlobalState.pLatestState->iID);
}
GLUA_END

GLUA_FUNC( get_self_ref )
{
	GLUA_ARG_COUNT_CHECK( get_self_ref, 0, 0 );

	glua_pushInteger(gTestClientGlobalState.pLatestState->iMyRef);
}
GLUA_END

GLUA_FUNC( get_hp )
{
	GLUA_ARG_COUNT_CHECK( get_hp, 0, 0 );

	if(!gTestClientGlobalState.pLatestState->pMyEnt)
	{
		glua_pushNumber(0.0);
		break;
	}

	glua_pushNumber(gTestClientGlobalState.pLatestState->pMyEnt->fHP);
}
GLUA_END

GLUA_FUNC( get_max_hp )
{
	GLUA_ARG_COUNT_CHECK( get_max_hp, 0, 0 );

	if(!gTestClientGlobalState.pLatestState->pMyEnt)
	{
		glua_pushNumber(0.0);
		break;
	}

	glua_pushNumber(gTestClientGlobalState.pLatestState->pMyEnt->fMaxHP);
}
GLUA_END

GLUA_FUNC( get_shields )
{
	int i;

	GLUA_ARG_COUNT_CHECK( get_shields, 0, 0 );

	glua_pushTable();

	if(!gTestClientGlobalState.pLatestState->pMyEnt)
	{
		break;
	}

	for(i = 0; i < 4; ++i)
	{
		glua_pushNumber(gTestClientGlobalState.pLatestState->pMyEnt->fShields[i]);
		glua_tblKey_i(i+1);
	}
}
GLUA_END

GLUA_FUNC( is_dead )
{
	GLUA_ARG_COUNT_CHECK( is_dead, 0, 0 );

	if(!gTestClientGlobalState.pLatestState->pMyEnt)
	{
		glua_pushBoolean(false);
		break;
	}

	glua_pushBoolean(gTestClientGlobalState.pLatestState->pMyEnt->bDead);
}
GLUA_END

GLUA_FUNC( is_casting )
{
	GLUA_ARG_COUNT_CHECK( is_casting, 0, 0 );

	if(!gTestClientGlobalState.pLatestState->pMyEnt)
	{
		glua_pushBoolean(false);
		break;
	}

	glua_pushBoolean(gTestClientGlobalState.pLatestState->pMyEnt->bCasting);
}
GLUA_END

GLUA_FUNC( is_in_team )
{
	GLUA_ARG_COUNT_CHECK( is_in_team, 0, 0 );

	glua_pushBoolean(gTestClientGlobalState.pLatestState->bIsTeamed);
}
GLUA_END

GLUA_FUNC( get_team )
{
	int i;

	GLUA_ARG_COUNT_CHECK( get_team, 0, 0 );

	glua_pushTable();

	if(!gTestClientGlobalState.pLatestState->bIsTeamed)
	{
		break;
	}

	for(i = 0; i < gTestClientGlobalState.pLatestState->iNumMembers; ++i)
	{
		TestClientEntityWrapper ent;
		TestClientEntity *pEnt = eaIndexedGetUsingInt(&gTestClientGlobalState.pLatestState->ppEnts, i+1);

		if(!pEnt)
		{
			glua_pushNil();
		}
		else
		{
			if(pEnt->iRefIfTeamed)
			{
				ent.iRef = pEnt->iRefIfTeamed;
			}
			else
			{
				ent.iRef = i+1;
			}

			TestClient_PushEntity(&ent);
		}

		glua_tblKey_i(i+1);
	}
}
GLUA_END

GLUA_FUNC( get_team_requests )
{
	int i;

	GLUA_ARG_COUNT_CHECK( get_team_requests, 0, 0 );

	if(!gTestClientGlobalState.pLatestState->bIsTeamed)
	{
		glua_pushNil();
	}

	glua_pushTable();

	for(i = 0; i < gTestClientGlobalState.pLatestState->iNumRequests; ++i)
	{
		TestClientEntityWrapper ent;
		TestClientEntity *pEnt = eaIndexedGetUsingInt(&gTestClientGlobalState.pLatestState->ppEnts, i+6);

		if(!pEnt)
		{
			glua_pushNil();
		}
		else
		{
			if(pEnt->iRefIfTeamed)
			{
				ent.iRef = pEnt->iRefIfTeamed;
			}
			else
			{
				ent.iRef = i+1;
			}

			TestClient_PushEntity(&ent);
		}

		glua_tblKey_i(i+1);
	}
}
GLUA_END

GLUA_FUNC( away_team_bypass )
{
	cmd_TestClientAwayTeamOptIn();
}
GLUA_END

GLUA_FUNC( get_nearest_friend )
{
	TestClientEntityWrapper ent;
	
	if(!gTestClientGlobalState.pLatestState->pMyEnt || eaiSize(&gTestClientGlobalState.pLatestState->pMyEnt->piNearbyFriends) == 0)
	{
		glua_pushNil();
		break;
	}

	ent.iRef = gTestClientGlobalState.pLatestState->pMyEnt->piNearbyFriends[0];
	TestClient_PushEntity(&ent);
}
GLUA_END

GLUA_FUNC( get_nearest_hostile )
{
	TestClientEntityWrapper ent;
	
	if(!gTestClientGlobalState.pLatestState->pMyEnt || eaiSize(&gTestClientGlobalState.pLatestState->pMyEnt->piNearbyHostiles) == 0)
	{
		glua_pushNil();
		break;
	}

	ent.iRef = gTestClientGlobalState.pLatestState->pMyEnt->piNearbyHostiles[0];
	TestClient_PushEntity(&ent);
}
GLUA_END

GLUA_FUNC( get_nearby_friends )
{
	int i;
	TestClientEntityWrapper ent;
	
	GLUA_ARG_COUNT_CHECK( get_nearby_friends, 0, 0 );

	glua_pushTable();

	if(!gTestClientGlobalState.pLatestState->pMyEnt || eaiSize(&gTestClientGlobalState.pLatestState->pMyEnt->piNearbyFriends) == 0)
	{
		break;
	}

	for(i = 0; i < eaiSize(&gTestClientGlobalState.pLatestState->pMyEnt->piNearbyFriends); ++i)
	{
		ent.iRef = gTestClientGlobalState.pLatestState->pMyEnt->piNearbyFriends[i];
		TestClient_PushEntity(&ent);
		glua_tblKey_i(i+1);
	}
}
GLUA_END

GLUA_FUNC( get_nearby_hostiles )
{
	int i;
	TestClientEntityWrapper ent;
	
	GLUA_ARG_COUNT_CHECK( get_nearby_hostiles, 0, 0 );

	glua_pushTable();

	if(!gTestClientGlobalState.pLatestState->pMyEnt || eaiSize(&gTestClientGlobalState.pLatestState->pMyEnt->piNearbyHostiles) == 0)
	{
		break;
	}

	for(i = 0; i < eaiSize(&gTestClientGlobalState.pLatestState->pMyEnt->piNearbyHostiles); ++i)
	{
		ent.iRef = gTestClientGlobalState.pLatestState->pMyEnt->piNearbyHostiles[i];
		TestClient_PushEntity(&ent);
		glua_tblKey_i(i+1);
	}
}
GLUA_END

GLUA_FUNC( get_nearby_objects )
{
	int i;
	TestClientObjectWrapper obj;

	GLUA_ARG_COUNT_CHECK( get_nearby_objects, 0, 0 );

	glua_pushTable();

	if(!gTestClientGlobalState.pLatestState->pMyEnt || eaSize(&gTestClientGlobalState.pLatestState->pMyEnt->ppNearbyObjects) == 0)
	{
		break;
	}

	for(i = 0; i < eaSize(&gTestClientGlobalState.pLatestState->pMyEnt->ppNearbyObjects); ++i)
	{
		strcpy(obj.key, gTestClientGlobalState.pLatestState->pMyEnt->ppNearbyObjects[i]);
		TestClient_PushObject(&obj);
		glua_tblKey_i(i+1);
	}
}
GLUA_END

GLUA_FUNC( get_interact_options )
{
	int i;

	GLUA_ARG_COUNT_CHECK( get_interact_options, 0, 0 );

	glua_pushTable();

	if(!eaSize(&gTestClientGlobalState.pLatestState->ppInteracts))
	{
		glua_pushNil();
		glua_tblKey_i(1);
		break;
	}

	for(i = 0; i < eaSize(&gTestClientGlobalState.pLatestState->ppInteracts); ++i)
	{
		TestClientEntityWrapper ent = {0};
		glua_pushTable();
		ent.iRef = gTestClientGlobalState.pLatestState->ppInteracts[i]->iRef;
		TestClient_PushEntity(&ent);
		glua_tblKey("ent");
		glua_pushString(gTestClientGlobalState.pLatestState->ppInteracts[i]->pchString);
		glua_tblKey("name");
		glua_tblKey_i(i+1);
	}
}
GLUA_END

GLUA_FUNC( interact )
{
	int i;
	char *str = NULL;

	GLUA_ARG_COUNT_CHECK( interact, 0, 1 );
	GLUA_ARG_CHECK( interact, 1, GLUA_TYPE_INT, true, "integer index" );

	if(glua_argn() < 1)
	{
		i = 0;
	}
	else
	{
		i = glua_getInteger(1);

		if(i < 0)
		{
			i = -(i+1);
			estrPrintf(&str, "interactremote %d", i);
			ClientController_SendCommandToClient(str);
			estrDestroy(&str);
			break;
		}
		else
		{
			--i;
			
			if(i < 0 || i >= eaSize(&gTestClientGlobalState.pLatestState->ppInteracts))
			{
				break;
			}
		}
	}

	if(gTestClientGlobalState.pLatestState->ppInteracts[i]->iRef)
	{
		estrPrintf(&str, "interactwithent %d", gTestClientGlobalState.pLatestState->ppInteracts[i]->iRef);
	}
	else
	{
		estrPrintf(&str, "interactnonprompted %d", i);
	}

	ClientController_SendCommandToClient(str);
	estrDestroy(&str);
}
GLUA_END

GLUA_FUNC( get_nearby_contacts )
{
	int i;
	TestClientEntityWrapper ent;

	GLUA_ARG_COUNT_CHECK( get_nearby_contacts, 0, 0 );

	glua_pushTable();

	if(!eaSize(&gTestClientGlobalState.pLatestState->ppContacts))
	{
		ent.iRef = 0;
		TestClient_PushEntity(&ent);
		glua_tblKey_i(1);
		break;
	}

	for(i = 0; i < eaSize(&gTestClientGlobalState.pLatestState->ppContacts); ++i)
	{
		ent.iRef = gTestClientGlobalState.pLatestState->ppContacts[i]->iRef;
		TestClient_PushEntity(&ent);
		glua_tblKey_i(i+1);
	}
}
GLUA_END

GLUA_FUNC( learn_power_tree )
{
	char *cmd = NULL;

	GLUA_ARG_COUNT_CHECK( learn_power_tree, 1, 1 );
	GLUA_ARG_CHECK( learn_power_tree, 1, GLUA_TYPE_STRING, false, "" );

	estrPrintf(&cmd, "TestClient LearnPowerTree %s", glua_getString(1));
	ClientController_SendCommandToClient(cmd);
	estrDestroy(&cmd);
}
GLUA_END

GLUA_FUNC( get_powers )
{
	GLUA_ARG_COUNT_CHECK( get_powers, 0, 0 );

	glua_pushTable();

	if(!gTestClientGlobalState.pLatestState->ppPowers)
	{
		TestClientPower power = {0};
		power.iID = 0;
		power.bAttack = false;
		power.fRange = 0.0;
		power.pchName = allocAddString("INVALID_POWER");
		TestClient_PushPower(&power);
		glua_tblKey_i(1);
		break;
	}

	FOR_EACH_IN_EARRAY_FORWARDS(gTestClientGlobalState.pLatestState->ppPowers, TestClientPower, pPower)
	{
		TestClientPower power = {0};

		power.iID = pPower->iID;
		power.pchName = pPower->pchName;
		power.bAttack = pPower->bAttack;
		power.fRange = pPower->fRange;

		TestClient_PushPower(&power);
		glua_tblKey_i(ipPowerIndex+1);
	}
	FOR_EACH_END
}
GLUA_END

GLUA_FUNC( ent_from_ref )
{
	TestClientEntityWrapper ent;

	GLUA_ARG_COUNT_CHECK( ent_from_ref, 1, 1 );
	GLUA_ARG_CHECK( ent_from_ref, 1, GLUA_TYPE_INT | GLUA_TYPE_NUMBER, false, "" );

	ent.iRef = glua_getInteger(1);

	TestClient_PushEntity(&ent);
}
GLUA_END

void *_TestClient_GetEntity(lua_State *L, int argn)
{
	return _glua_getUserdata(L, argn, tag_entity, NULL);
}

void _TestClient_PushEntity(lua_State *L, TestClientEntityWrapper *ent)
{
	_glua_pushuserdata_raw(L, tag_entity, ent, sizeof(*ent), 0);
}

void *_TestClient_GetPower(lua_State *L, int argn)
{
	return _glua_getUserdata(L, argn, tag_power, NULL);
}

void _TestClient_PushPower(lua_State *L, TestClientPower *pow)
{
	_glua_pushuserdata_raw(L, tag_power, pow, sizeof(*pow), 0);
}

void *_TestClient_GetObject(lua_State *L, int argn)
{
	return _glua_getUserdata(L, argn, tag_object, NULL);
}

void _TestClient_PushObject(lua_State *L, TestClientObjectWrapper *obj)
{
	_glua_pushuserdata(L, tag_object, obj, sizeof(*obj), NULL);
}

void TestClient_InitEntity(LuaContext *pContext)
{
	lua_State *_lua_ = pContext->luaState;
	int namespaceTbl = _glua_create_tbl(_lua_, "tc", 0);

	GLUA_DECL( namespaceTbl )
	{
		glua_func( target ),
		glua_func( assist ),
		glua_func( get_target ),
		glua_func( get_self ),
		glua_func( get_self_id ),
		glua_func( get_self_ref ),
		glua_func( get_hp ),
		glua_func( get_max_hp ),
		glua_func( get_shields ),
		glua_func( is_dead ),
		glua_func( is_casting ),
		glua_func( is_in_team ),
		glua_func( get_team ),
		glua_func( get_team_requests ),
		glua_func( away_team_bypass ),
		glua_func( get_nearest_hostile ),
		glua_func( get_nearby_hostiles ),
		glua_func( get_nearest_friend ),
		glua_func( get_nearby_friends ),
		glua_func( get_nearby_objects ),
		glua_func( get_interact_options ),
		glua_func( interact ),
		glua_func( get_nearby_contacts ),
		glua_func( learn_power_tree ),
		glua_func( get_powers ),
		glua_func( ent_from_ref ),
		glua_tag( "entity", &tag_entity ),
		glua_tagdispatch( tag_entity ),
		glua_tag( "object", &tag_object ),
		glua_tagdispatch( tag_object ),
		glua_tag( "power", &tag_power ),
		glua_tagdispatch( tag_power )
	}
	GLUA_DECL_END
}

#endif