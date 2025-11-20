/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslLogin2_GetCharacterChoices.h"
#include "aslLogin2_Error.h"
#include "Login2Common.h"
#include "Login2ServerCommon.h"
#include "ShardCluster.h"
#include "earray.h"
#include "timing.h"
#include "StringCache.h"
#include "StashTable.h"

#include "AutoGen/aslLogin2_GetCharacterChoices_c_ast.h"
#include "AutoGen/Login2Common_h_ast.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"

static StashTable s_ActiveCharacterChoiceRequests = NULL;

static U32 s_GetCharacterChoicesRemoteShardTimeout = 60;
// The maximum number of entity fixup transactions that can be running at one time on each loginserver.
AUTO_CMD_INT(s_GetCharacterChoicesRemoteShardTimeout, GetCharacterChoicesRemoteShardTimeout) ACMD_AUTO_SETTING(Loginserver, LOGINSERVER);

AUTO_STRUCT;
typedef struct ChoicesShardState
{
    STRING_POOLED shardName;    AST(KEY POOL_STRING)

    // the request ID of the current request sent to the shard that has not been responded to yet.
    U64 activeRequestID;

    // set to true if we can't talk to this shard.
    bool requestFailed;

    // character choices that have been returned from this shard.
    Login2CharacterChoices *returnedChoices;
} ChoicesShardState;

AUTO_STRUCT;
typedef struct GetCharacterChoicesState
{
    ContainerID accountID;
    U32 timeStarted;
    bool failed;

    // Count the number of replies received and shards that we didn't request from.
    S32 repliesReceived;

    EARRAY_OF(ChoicesShardState) shardStates;

    // String used to record errors for logging.
    STRING_MODIFIABLE errorString;          AST(ESTRING)

    // Completion callback data.
    GetCharacterChoicesCB cbFunc;           NO_AST
    void *userData;                         NO_AST
} GetCharacterChoicesState;

static bool
AddActiveRequest(GetCharacterChoicesState *choicesState)
{
    // Create the stash table if it does not already exist;
    if ( s_ActiveCharacterChoiceRequests == NULL )
    {
        s_ActiveCharacterChoiceRequests = stashTableCreateInt(200);
    }

    return stashIntAddPointer(s_ActiveCharacterChoiceRequests, (int)choicesState->accountID, choicesState, false);
}

static GetCharacterChoicesState *
GetActiveRequest(ContainerID accountID)
{
    GetCharacterChoicesState *choicesState = NULL;

    // Create the stash table if it does not already exist;
    if ( s_ActiveCharacterChoiceRequests == NULL )
    {
        s_ActiveCharacterChoiceRequests = stashTableCreateInt(200);
        return NULL;
    }

    if ( stashIntFindPointer(s_ActiveCharacterChoiceRequests, (int)accountID, &choicesState) == false )
    {
        return NULL;
    }

    return choicesState;
}

static void
RemoveActiveRequest(ContainerID accountID)
{
    GetCharacterChoicesState *choicesState = NULL;

    stashIntRemovePointer(s_ActiveCharacterChoiceRequests, (int)accountID, &choicesState);
}

// Handle the completion of the GetCharacterChoices operation.  Log any errors and call the callback to return results to the caller.
static void
GetCharacterChoicesComplete(GetCharacterChoicesState *choicesState)
{
    Login2CharacterChoices *characterChoices;
    int i;

    RemoveActiveRequest(choicesState->accountID);

    characterChoices = StructCreate(parse_Login2CharacterChoices);
    characterChoices->accountID = choicesState->accountID;
    eaIndexedEnable(&characterChoices->characterChoices, parse_Login2CharacterChoice);
    
    for ( i = eaSize(&choicesState->shardStates) - 1; i >= 0; i-- )
    {
        ChoicesShardState *shardState = choicesState->shardStates[i];

        if ( shardState )
        {
            if ( shardState->requestFailed || 
                ( ( shardState->returnedChoices == NULL ) && ( shardState->activeRequestID != 0 ) ) )
            {
                // Record which shards we do not have a valid response from.
                eaPush(&characterChoices->missingShardNames, (char *)shardState->shardName);
                aslLogin2_Log("aslLogin2_GetCharacterChoices for accountID %u failed to get characters from shard %s.", choicesState->accountID, shardState->shardName);
            }
            else
            {
                int j;

                // Move the choices from the individual shard returned choices to the unified one that will be returned to our caller.
                // By moving them we avoid extra memory allocations and a bunch of data copying.
                for ( j = eaSize(&shardState->returnedChoices->characterChoices) - 1; j >= 0; j-- )
                {
                    Login2CharacterChoice *characterChoice = eaPop(&shardState->returnedChoices->characterChoices);
                    eaPush(&characterChoices->characterChoices, characterChoice);
                }
            }
        }
    }

    aslLogin2_Log("aslLogin2_GetCharacterChoices succeeded for accountID %u.  %d characters returned.", choicesState->accountID, eaSize(&characterChoices->characterChoices));

    // Notify the caller that we are done.
    // Note that it is up to the callback or other related code to clean up the character choices.
    if ( choicesState->cbFunc )
    {
        (* choicesState->cbFunc)(characterChoices, choicesState->userData);
    }

    // Clean up state.
    StructDestroy(parse_GetCharacterChoicesState, choicesState);
}

// This is the inter-shard remote command that the various ObjectDBs call to return character choices.
AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(OBJECTDB);
void
aslLogin2_ReturnCharacterChoices(U32 accountID, U64 requestToken, Login2CharacterChoices *characterChoices)
{
    GetCharacterChoicesState *choicesState;
    int i;

    choicesState = GetActiveRequest(accountID);

    // Log and return if we don't have an active request for the given account ID.
    if ( choicesState == NULL )
    {
        aslLogin2_Log("Received character choices for account that does not have an active request.  AccountID = %u, RequestToken = %llu", accountID, requestToken);
        return;
    }

    // Search shard states looking for one with a matching request ID.
    for ( i = eaSize(&choicesState->shardStates) - 1; i >= 0; i-- )
    {
        ChoicesShardState *shardState = choicesState->shardStates[i];
        if ( shardState->activeRequestID == requestToken )
        {
            // Mark this shard as no longer having an active request.
            shardState->activeRequestID = 0;

            // Free any previous character choices for the shard.
            if ( shardState->returnedChoices == NULL )
            {
                StructDestroy(parse_Login2CharacterChoices, shardState->returnedChoices);
            }

            // Copy the returned choices and save them on the shard state.
            shardState->returnedChoices = StructClone(parse_Login2CharacterChoices, characterChoices);

            // Increment number of replies received.
            choicesState->repliesReceived++;

            // If we have received replies from every shard then we are done.
            if ( choicesState->repliesReceived >= eaSize(&choicesState->shardStates) )
            {
                GetCharacterChoicesComplete(choicesState);
            }
            return;
        }
    }

    // If we get here then the response isn't one we were waiting for.
    aslLogin2_Log("Received character choices with request token that does not match an active request.  AccountID = %llu, RequestToken = %u", accountID, requestToken);
    return;
}

static U32 s_LastTickTime = 0;

void
aslLogin2_CharacterChoicesTick(void)
{
    U32 currentTime = timeSecondsSince2000();
    EARRAY_OF(GetCharacterChoicesState) timedOutRequests = NULL;

    // Only update once per second.
    if ( s_LastTickTime != currentTime )
    {
        int i;

        FOR_EACH_IN_STASHTABLE(s_ActiveCharacterChoiceRequests, GetCharacterChoicesState, tmpChoicesState)
        {
            if ( ( tmpChoicesState->timeStarted + s_GetCharacterChoicesRemoteShardTimeout ) < currentTime )
            {
                // Record all requests that have timed out.  We can't just remove them in this loop because we can't modify the stash table while iterating.
                eaPush(&timedOutRequests, tmpChoicesState);
            }
        }
        FOR_EACH_END;

        // Complete any timed out requests.
        for ( i = eaSize(&timedOutRequests) - 1; i >= 0; i-- )
        {
            GetCharacterChoicesState *choicesState = eaPop(&timedOutRequests);
            GetCharacterChoicesComplete(choicesState);
        }

        s_LastTickTime = currentTime;
    }
}

// Get the character choices for an account, including from other shards if the current shard is part of a cluster.
// The callback will be called when the character choices are ready.
void
aslLogin2_GetCharacterChoices(ContainerID accountID, GetCharacterChoicesCB cbFunc, void *userData)
{
    Cluster_Overview *clusterOverview;
    GetCharacterChoicesState *choicesState;
    int i;
    Login2InterShardDestination myDestination = {0};

    // Create our state struct.
    choicesState = StructCreate(parse_GetCharacterChoicesState);
    choicesState->accountID = accountID;
    choicesState->timeStarted = timeSecondsSince2000();
    choicesState->cbFunc = cbFunc;
    choicesState->userData = userData;

    clusterOverview = Login2_GetShardClusterOverview();
    if ( clusterOverview == NULL )
    {
        aslLogin2_Log("aslLogin2_GetCharacterChoices could not find a cluster overview.");
        GetCharacterChoicesComplete(choicesState);
        return;
    }

    if ( AddActiveRequest(choicesState) == false )
    {
        // If we get here it probably means we got duplicate requests for the same account.  For now we will cancel the new request,
        //  but it might be better if we canceled the old one instead.
        aslLogin2_Log("aslLogin2_GetCharacterChoices got duplicate request for accountID %u.", accountID);
        GetCharacterChoicesComplete(choicesState);
        return;
    }

    // Send a request for character choices to each shard in the cluster.
    for ( i = eaSize(&clusterOverview->ppShards) - 1; i >= 0; i-- )
    {
        ClusterShardSummary *shardSummary = clusterOverview->ppShards[i];

        ChoicesShardState *shardState = StructCreate(parse_ChoicesShardState);
        shardState->shardName = allocAddString(shardSummary->pShardName);

        eaPush(&choicesState->shardStates, shardState);

        // Only contact a shard if we believe we can talk to it.
        if ( ( shardSummary->eState == CLUSTERSHARDSTATE_CONNECTED ) || ( shardSummary->eState == CLUSTERSHARDSTATE_THATS_ME ) )
        {
            // Generate an ID we can use to identify the response.
            shardState->activeRequestID = Login2_GenerateRequestToken();

            // Fill in the destination struct, which will tell the database where to send the response.
            Login2_FillDestinationStruct(&myDestination, shardState->activeRequestID);

            // Send the request to the ObjectDB on the (possibly) remote shard for it's character choices.
            RemoteCommand_Intershard_dbLogin2_GetCharacterChoices(shardState->shardName, GLOBALTYPE_OBJECTDB, 0, &myDestination, accountID);
        }
        else
        {
            // Count a shard that we do not request from as having sent a reply.
            choicesState->repliesReceived++;

            shardState->requestFailed = true;
        }
    }
}

#include "AutoGen/aslLogin2_GetCharacterChoices_c_ast.c"