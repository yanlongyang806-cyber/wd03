/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslLogin2_GetCharacterDetail.h"
#include "Login2CharacterDetail.h"
#include "Login2ServerCommon.h"
#include "timing.h"
#include "StringCache.h"
#include "StashTable.h"
#include "aslLogin2_Error.h"
#include "stdtypes.h"
#include "referencesystem.h"
#include "Entity.h"

#include "AutoGen/Login2CharacterDetail_h_ast.h"
#include "AutoGen/aslLogin2_GetCharacterDetail_c_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"

// StashTable to keep track of GetCharacterDetail requests that are in progress.
static StashTable s_PendingGetCharacterDetailRequests = NULL;

AUTO_STRUCT;
typedef struct GetCharacterDetailState
{
    ContainerID accountID;
    ContainerID playerID;
    STRING_POOLED shardName;                AST(POOL_STRING)

    U32 timeStarted;
    U64 requestToken;

    bool failed;
    bool returnActivePuppets;

    // String used to record errors for logging.
    STRING_MODIFIABLE errorString;          AST(ESTRING)

    // Completion callback data.
    GetCharacterDetailCB cbFunc;            NO_AST
    void *userData;                         NO_AST
} GetCharacterDetailState;

static bool
AddActiveRequest(GetCharacterDetailState *getDetailState)
{
    // Create the stash table if it does not already exist;
    if ( s_PendingGetCharacterDetailRequests == NULL )
    {
        s_PendingGetCharacterDetailRequests = stashTableCreateInt(200);
    }

    return stashIntAddPointer(s_PendingGetCharacterDetailRequests, (int)getDetailState->playerID, getDetailState, false);
}

static GetCharacterDetailState *
GetActiveRequest(ContainerID playerID)
{
    GetCharacterDetailState *getDetailState = NULL;

    // Create the stash table if it does not already exist;
    if ( s_PendingGetCharacterDetailRequests == NULL )
    {
        s_PendingGetCharacterDetailRequests = stashTableCreateInt(50);
        return NULL;
    }

    if ( stashIntFindPointer(s_PendingGetCharacterDetailRequests, (int)playerID, &getDetailState) == false )
    {
        return NULL;
    }

    return getDetailState;
}

static void
RemoveActiveRequest(ContainerID playerID)
{
    GetCharacterDetailState *getDetailState = NULL;

    stashIntRemovePointer(s_PendingGetCharacterDetailRequests, (int)playerID, &getDetailState);
}

static void
GetCharacterDetailComplete(GetCharacterDetailState *getDetailState, Login2CharacterDetail *characterDetail)
{
    RemoveActiveRequest(getDetailState->playerID);

    if ( getDetailState->failed )
    {
        aslLogin2_Log("aslLogin2_GetCharacterDetail failed for playerID %u. %s", getDetailState->accountID, getDetailState->errorString);
    }
    else
    {
        if ( characterDetail->playerEnt )
        {
            aslLogin2_Log("aslLogin2_GetCharacterDetail succeeded for playerID %u.", getDetailState->accountID);
        }
        else
        {
            aslLogin2_Log("aslLogin2_GetCharacterDetail no character returned for playerID %u.", getDetailState->accountID);
        }
    }

    // Notify the caller that we are done.
    // Note that it is up to the callback or other related code to clean up the character detail.
    if ( getDetailState->cbFunc )
    {
        (* getDetailState->cbFunc)(characterDetail, getDetailState->userData);
    }

    // Clean up state.
    StructDestroy(parse_GetCharacterDetailState, getDetailState);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(OBJECTDB);
void
aslLogin2_ReturnCharacterDetail(U32 playerID, Login2CharacterDetailDBReturn *detailReturn, U64 requestToken, bool failed, char *errorString)
{
    GetCharacterDetailState *getDetailState;
    Entity *playerEnt = NULL;
    Login2CharacterDetail *characterDetail;

    getDetailState = GetActiveRequest(playerID);

    // If we can't find the right GetCharacterDetailState, then there is nothing we can do with the returned character detail.
    if ( getDetailState == NULL || requestToken != getDetailState->requestToken )
    {
        aslLogin2_Log("aslLogin2_ReturnCharacterDetail: received response for unknown request.  playerID =  %u, requestToken = %llu", playerID, requestToken);
        return;
    }

    characterDetail = StructCreate(parse_Login2CharacterDetail);
    characterDetail->playerID = playerID;

    if ( detailReturn )
    {
        int i;

        if ( detailReturn->playerCharacterString )
        {
            playerEnt = StructCreateWithComment(parse_Entity, "Login2 character detail entity.");
            if ( ParserReadText(detailReturn->playerCharacterString, parse_Entity, playerEnt, 0) == false )
            {
                StructDestroy(parse_Entity, playerEnt);
                playerEnt = NULL;
            }
        }

        characterDetail->playerEnt = playerEnt;

        for ( i = eaSize(&detailReturn->activePuppetStrings) - 1; i >= 0; i-- )
        {
            Entity *puppetEnt = StructCreateWithComment(parse_Entity, "Login2 character detail puppet entity.");
            if ( ParserReadText(detailReturn->activePuppetStrings[i], parse_Entity, puppetEnt, 0) == false )
            {
                StructDestroy(parse_Entity, puppetEnt);
                puppetEnt = NULL;
            }
            else
            {
                eaPush(&characterDetail->activePuppetEnts, puppetEnt);
            }
        }
    }
	getDetailState->failed = failed;
	estrCopy2(&getDetailState->errorString, errorString);
    GetCharacterDetailComplete(getDetailState, characterDetail);
}

void
aslLogin2_GetCharacterDetail(ContainerID accountID, ContainerID playerID, const char *shardName, bool returnActivePuppets, GetCharacterDetailCB cbFunc, void *userData)
{
    Login2InterShardDestination myDestination = {0};
    GetCharacterDetailState *getDetailState = StructCreate(parse_GetCharacterDetailState);

    getDetailState->accountID = accountID;
    getDetailState->playerID = playerID;
    getDetailState->shardName = allocAddString(shardName);
    getDetailState->returnActivePuppets = returnActivePuppets;
    getDetailState->timeStarted = timeSecondsSince2000();
    getDetailState->requestToken = Login2_GenerateRequestToken();
    getDetailState->cbFunc = cbFunc;
    getDetailState->userData = userData;

    if ( AddActiveRequest(getDetailState) == false )
    {
        getDetailState->failed = true;

        estrConcatf(&getDetailState->errorString, "Duplicate GetCharacterDetail request for playerID %u\n", playerID);

        GetCharacterDetailComplete(getDetailState, NULL);
        return;
    }

    // Fill in the destination struct, which will tell the remote shard where to send the response.
    Login2_FillDestinationStruct(&myDestination, getDetailState->requestToken);

    RemoteCommand_Intershard_dbLogin2_GetCharacterDetail(shardName, GLOBALTYPE_OBJECTDB, 0, &myDestination, accountID, playerID, returnActivePuppets);
}

#include "AutoGen/aslLogin2_GetCharacterDetail_c_ast.c"