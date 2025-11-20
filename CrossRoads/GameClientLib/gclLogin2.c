/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gclLogin2.h"
#include "Login2Common.h"
#include "Login2CharacterDetail.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "Player.h"
#include "Character.h"
#include "stdtypes.h"
#include "earray.h"
#include "net.h"
#include "CharacterClass.h"
#include "gclLogin.h"
#include "GlobalComm.h"
#include "gclEntity.h"

#include "AutoGen/Login2CharacterDetail_h_ast.h"
#include "AutoGen/gclLogin2_h_ast.h"
#include "AutoGen/gclLogin2_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("Login2CharacterCreationData", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("Login2CharacterChoices", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("Login2CharacterChoice", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("Login2CharacterSelectionData", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("Login2CharacterDetail", BUDGET_GameSystems););

//
// A cache of character details, which are actually full entity copies of just PERSIST SUBSCRIBE fields.
//

#define REQUESTED_CHARACTER_CACHE_TIMEOUT 5

AUTO_STRUCT;
typedef struct RequestedCharacterDetailState
{
    ContainerID playerID;           AST(KEY)
    U32 timeRequested;

    FetchEntityDetailCB cbFunc;     NO_AST
    void *cbData;                   NO_AST
} RequestedCharacterDetailState;

static EARRAY_OF(Login2CharacterDetail) s_CharacterDetailCache = NULL;
static EARRAY_OF(RequestedCharacterDetailState) s_RequestedCharacterDetail = NULL;

static void
FetchComplete(ContainerID playerID, GCLLogin2FetchResult result)
{
    RequestedCharacterDetailState *requestedCharacterDetailState;

    requestedCharacterDetailState = eaIndexedRemoveUsingInt(&s_RequestedCharacterDetail, playerID);
    if ( requestedCharacterDetailState )
    {
        if ( requestedCharacterDetailState->cbFunc )
        {
            (* requestedCharacterDetailState->cbFunc)(playerID, result, requestedCharacterDetailState->cbData);
        }

        StructDestroy(parse_RequestedCharacterDetailState, requestedCharacterDetailState);
    }
}

void
gclLogin2_CharacterDetailCache_Clear(void)
{
    int i;

    // Turn on indexing for both arrays.
    eaIndexedEnable(&s_CharacterDetailCache, parse_Login2CharacterDetail);
    eaIndexedEnable(&s_RequestedCharacterDetail, parse_RequestedCharacterDetailState);

    // Clear the details cache.
    eaClearStruct(&s_CharacterDetailCache, parse_Login2CharacterDetail);

    // Clear the list of pending requests.
    for ( i = eaSize(&s_RequestedCharacterDetail) - 1; i >= 0; i-- )
    {
        FetchComplete(s_RequestedCharacterDetail[i]->playerID, FetchResult_Timeout);
    }
}

Login2CharacterDetail *
gclLogin2_CharacterDetailCache_Get(ContainerID characterID)
{
    Login2CharacterDetail *characterDetail;

    characterDetail = eaIndexedGetUsingInt(&s_CharacterDetailCache, characterID);

    return characterDetail;
}

//
// This function initiates a fetch of character details from the login server.  The character details include a full copy of the player 
//  Entity struct (PERSIST SUBSCRIBE fields only), and optionally any active puppets.  Calling this function can result in some heavyweight
//  ObjectDB operations such as unpacking multiple containers and onlining an offline character, so care should be taken to not cause it to
//  be called on more characters than absolutely necessary.
//
// This function is currently only called for the character that is currently selected in the character selection UI.  IT IS VERY IMPORTANT
//  THAT THIS FUNCTION NOT BE CALLED FOR EVERY CHARACTER IN THE LIST BEING SHOWN ON THE CHARACTER SELECT SCREEN.  Doing so will add extreme 
//  load to the ObjectDB, and possibly kill the shard.  A fail object awaits those who do not heed this warning.
//
void
gclLogin2_CharacterDetailCache_Fetch(ContainerID characterID, FetchEntityDetailCB cbFunc, void *cbData)
{
    RequestedCharacterDetailState *requestedCharacterDetailState;
    bool doFetch;
    U32 curTime = timeSecondsSince2000();

    devassertmsg(eaIndexedGetUsingInt(&s_CharacterDetailCache, characterID) == NULL, "gclLogin2_CharacterDetailCache_Fetch: attempting to fetch when character already exists in the cache");

    requestedCharacterDetailState = eaIndexedGetUsingInt(&s_RequestedCharacterDetail, characterID);
    if ( requestedCharacterDetailState )
    {
        // request is already pending.

        if ( requestedCharacterDetailState->timeRequested + REQUESTED_CHARACTER_CACHE_TIMEOUT < curTime )
        {
            // Last request timed out.  Call the callback to finish it.
            if ( requestedCharacterDetailState->cbFunc )
            {
                (* requestedCharacterDetailState->cbFunc)(characterID, FetchResult_Timeout, requestedCharacterDetailState->cbData);
            }

            // Replace the timed out request with the new one.
            requestedCharacterDetailState->cbFunc = cbFunc;
            requestedCharacterDetailState->cbData = cbData;
            requestedCharacterDetailState->timeRequested = curTime;
            doFetch = true;
        }
        else
        {
            // Last request hasn't timed out yet, so cancel this one with "pending" response.
            (* cbFunc)(characterID, FetchResult_Pending, cbData);
            doFetch = false;
        }
    }
    else
    {
        // No pending request.  Add this request.
        requestedCharacterDetailState = StructCreate(parse_RequestedCharacterDetailState);
        requestedCharacterDetailState->playerID = characterID;
        requestedCharacterDetailState->timeRequested = curTime;
        requestedCharacterDetailState->cbFunc = cbFunc;
        requestedCharacterDetailState->cbData = cbData;
        eaIndexedAdd(&s_RequestedCharacterDetail, requestedCharacterDetailState);
        doFetch = true;
    }

    // Ask for the character detail from the login server.
    if ( doFetch )
    {
        Packet *pkt;

        // Ask the login server for the character detail.
        pkt = pktCreate(gpLoginLink, TOLOGIN_LOGIN2_REQUESTCHARACTERDETAIL);
        pktSendU32(pkt, characterID);
        pktSend(&pkt);
    }
}
Entity *
gclLogin2_CharacterDetailCache_GetEntity(ContainerID characterID)
{
    Login2CharacterDetail *characterDetail = gclLogin2_CharacterDetailCache_Get(characterID);

    if ( characterDetail )
    {
        return characterDetail->playerEnt;
    }

    return NULL;
}

void
gclLogin2_CharacterDetailCache_Add(Login2CharacterDetail *characterDetail)
{
    Login2CharacterDetail *oldDetail;
    RequestedCharacterDetailState *requestedCharacterDetailState;

    requestedCharacterDetailState = eaIndexedGetUsingInt(&s_RequestedCharacterDetail, characterDetail->playerID);
    if ( requestedCharacterDetailState == NULL )
    {
        // the character detail does not match a pending request.
        StructDestroy(parse_Login2CharacterDetail, characterDetail);
        return;
    }

    // Does an old character detail for this character exist?
    oldDetail = eaIndexedRemoveUsingInt(&s_CharacterDetailCache, characterDetail->playerID);
    if ( oldDetail != NULL )
    {
        // If an old character detail exists, destroy it.
        StructDestroy(parse_Login2CharacterDetail, oldDetail);
    }

    // Add the detail to the cache.
    eaIndexedAdd(&s_CharacterDetailCache, characterDetail);

    FetchComplete(characterDetail->playerID, FetchResult_Succeeded);
}

// Find a puppet entity with matching type in the character details for the given character.
Entity *
gclLogin2_CharacterDetailCache_GetPuppet(ContainerID playerID, CharClassTypes puppetClassType)
{
    Login2CharacterDetail *characterDetail = gclLogin2_CharacterDetailCache_Get(playerID);
    int i;

    if ( characterDetail == NULL )
    {
        return NULL;
    }

    if ( characterDetail )
    {
        for ( i = eaSize(&characterDetail->activePuppetEnts) - 1; i >= 0; i-- )
        {
            Entity *tmpPuppetEnt = characterDetail->activePuppetEnts[i];
            if ( tmpPuppetEnt->pChar )
            {
                CharacterClass *characterClass = GET_REF(tmpPuppetEnt->pChar->hClass);
                if ( characterClass && characterClass->eType == puppetClassType )
                {
                    return tmpPuppetEnt;
                }
            }
        }
    }

    return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AddictionPolicyActive);
int 
exprAddictionPolicyActive(void)
{
    Entity *pEnt = entActivePlayerPtr();

    return ( pEnt && pEnt->pPlayer && pEnt->pPlayer->addictionPlaySessionEndTime ) ;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AddictionPolicyRemainingPlayTime);
U32 
exprAddictionPolicyRemainingPlayTime(void)
{
    Entity *pEnt = entActivePlayerPtr();

    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->addictionPlaySessionEndTime )
    {
        if ( pEnt->pPlayer->addictionPlaySessionEndTime > timeSecondsSince2000() )
        {
            return pEnt->pPlayer->addictionPlaySessionEndTime - timeSecondsSince2000();
        }
    }
    return 0;
}

#include "AutoGen/gclLogin2_h_ast.c"
#include "AutoGen/gclLogin2_c_ast.c"