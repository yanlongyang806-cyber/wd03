/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslLogin2_Redirect.h"
#include "aslLogin2_StateMachine.h"
#include "aslLogin2_ClientComm.h"
#include "aslLogin2_Error.h"
#include "aslLogin2_Util.h"
#include "Login2ServerCommon.h"
#include "Login2Common.h"
#include "textparser.h"
#include "earray.h"
#include "StashTable.h"
#include "timing.h"
#include "stdtypes.h"
#include "sock.h"

#include "AutoGen/aslLogin2_Redirect_h_ast.h"
#include "AutoGen/aslLogin2_StateMachine_h_ast.h"
#include "AutoGen/Login2Common_h_ast.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"

extern StaticDefineInt PlayerTypeEnum[];

static StashTable s_pendingRedirects = NULL;

extern int gLoginServerListeningPort;

// Gets a pending redirect with the given accountID, removing it from the pending table.
Login2RedirectInfo *
aslLogin2_GetPendingRedirect(ContainerID accountID)
{
    Login2RedirectInfo *redirectInfo = NULL;

    if  ( s_pendingRedirects == NULL )
    {
        return NULL;
    }

    if ( !stashIntRemovePointer(s_pendingRedirects, accountID, &redirectInfo) )
    {
        return NULL;
    }

    return redirectInfo;
}

// Adds a pending redirect to the table.  Removes any previous redirect for the same account.
bool
aslLogin2_AddPendingRedirect(Login2RedirectInfo *redirectInfo)
{
    Login2RedirectInfo *oldRedirectInfo;

    if ( s_pendingRedirects == NULL )
    {
        s_pendingRedirects = stashTableCreateInt(32);
    }

    // If an old redirect exists, clean it up.
    oldRedirectInfo = aslLogin2_GetPendingRedirect(redirectInfo->accountID);
    if ( oldRedirectInfo != NULL )
    {
        StructDestroy(parse_Login2RedirectInfo, oldRedirectInfo);
    }

    return stashIntAddPointer(s_pendingRedirects, redirectInfo->accountID, redirectInfo, false);
}

// Fill in the structure that is sent to the remote shard when redirecting a client login.
// It is a subset of Login2State.
// IMPORTANT NOTE: This function fills the redirectInfo with pointers into data owned by the loginState.  The redirectInfo
//  filled in by this function should never have StructDestroy(), StructReset), etc. called on it, because that will free
//  data that it doesn't own. 
void
aslLogin2_FillRedirectInfo(Login2State *loginState, Login2RedirectInfo *redirectInfo)
{
    redirectInfo->accountID = loginState->accountID;
    redirectInfo->accountName = loginState->accountName;
    redirectInfo->accountDisplayName = loginState->accountDisplayName;
    redirectInfo->pweAccountName = loginState->pweAccountName;
    redirectInfo->clientIP = loginState->clientIP;
    redirectInfo->clientLanguageID = loginState->clientLanguageID;
    redirectInfo->clientAccessLevel = loginState->clientAccessLevel;
    redirectInfo->playerType = loginState->playerType;
    redirectInfo->affiliate = loginState->affiliate;
    redirectInfo->transferCookie = loginState->loginCookie;
    redirectInfo->timeRequested = timeSecondsSince2000();
    redirectInfo->selectedCharacterChoice = loginState->selectedCharacterChoice;
    redirectInfo->characterCreationData = loginState->characterCreationData;
    redirectInfo->requestedUGCEdit = loginState->requestedUGCEdit;
    redirectInfo->characterSelectionData = loginState->characterSelectionData;

    redirectInfo->gamePermissionsFromAccountPermissions = loginState->gamePermissionsFromAccountPermissions;

    redirectInfo->isLifetime = loginState->isLifetime;
    redirectInfo->isPress = loginState->isPress;
    redirectInfo->ignoreQueue = loginState->ignoreQueue;
    redirectInfo->isQueueVIP = loginState->isQueueVIP;
    redirectInfo->UGCCharacterPlayOnly = loginState->UGCCharacterPlayOnly;

    redirectInfo->debugStateHistory = loginState->debugStateHistory;
}

// Find a matching redirect and apply it to the loginState.
bool
aslLogin2_CreateLoginStateForRedirect(Login2State *loginState)
{
    Login2RedirectInfo *redirectInfo;

    if ( loginState->redirectLoginPacketData == NULL )
    {
        aslLogin2_Log("aslLogin2_CreateLoginStateForRedirect: no redirect packet");
        return false;
    }

    redirectInfo = aslLogin2_GetPendingRedirect(loginState->redirectLoginPacketData->accountID);
    if ( redirectInfo == NULL )
    {
        aslLogin2_Log("aslLogin2_CreateLoginStateForRedirect: redirectInfo not found");
        return false;
    }

    // Client IP must match.
    if ( loginState->clientIP != redirectInfo->clientIP )
    {
        aslLogin2_Log("aslLogin2_CreateLoginStateForRedirect: client IP does not match.  accountID = %u, clientIP = %u, redirectIP = %u", redirectInfo->accountID, loginState->clientIP, redirectInfo->clientIP);
        StructDestroy(parse_Login2RedirectInfo, redirectInfo);
        return false;
    }

    // Transfer cookie must match.
    if ( loginState->redirectLoginPacketData->transferCookie != redirectInfo->transferCookie )
    {
        aslLogin2_Log("aslLogin2_CreateLoginStateForRedirect: transfer cookie does not match.  accountID = %u, clientCookie = %llu, redirectCookie = %llu", redirectInfo->accountID, loginState->redirectLoginPacketData->transferCookie, redirectInfo->transferCookie);
        StructDestroy(parse_Login2RedirectInfo, redirectInfo);
        return false;
    }

    aslLogin2_SetAccountID(loginState, redirectInfo->accountID);
    loginState->selectedCharacterChoice = StructClone(parse_Login2CharacterChoice, redirectInfo->selectedCharacterChoice);
    loginState->accountName = StructAllocString(redirectInfo->accountName);
    loginState->accountDisplayName = StructAllocString(redirectInfo->accountDisplayName);
    loginState->pweAccountName = StructAllocString(redirectInfo->pweAccountName);
    loginState->clientIP = redirectInfo->clientIP;
    loginState->clientLanguageID = redirectInfo->clientLanguageID;
    loginState->clientAccessLevel = redirectInfo->clientAccessLevel;
    loginState->playerType = redirectInfo->playerType;
    loginState->affiliate = redirectInfo->affiliate;
    loginState->requestedUGCEdit = redirectInfo->requestedUGCEdit;

    if ( loginState->selectedCharacterChoice )
    {
        loginState->selectedCharacterID = loginState->selectedCharacterChoice->containerID;
    }

    loginState->characterSelectionData = StructClone(parse_Login2CharacterSelectionData, redirectInfo->characterSelectionData);
    loginState->characterCreationData = StructClone(parse_Login2CharacterCreationData, redirectInfo->characterCreationData);

    eaClearFast(&loginState->gamePermissionsFromAccountPermissions);
    eaCopy(&loginState->gamePermissionsFromAccountPermissions, &redirectInfo->gamePermissionsFromAccountPermissions);

    loginState->isLifetime = redirectInfo->isLifetime;
    loginState->isPress = redirectInfo->isPress;
    loginState->ignoreQueue = redirectInfo->ignoreQueue;
    loginState->isQueueVIP = redirectInfo->isQueueVIP;
    loginState->UGCCharacterPlayOnly = redirectInfo->UGCCharacterPlayOnly;
    loginState->afterRedirect = true;

    eaClearStruct(&loginState->debugStateHistory, parse_Login2StateDebugInfo);
    eaCopyStructs(&redirectInfo->debugStateHistory, &loginState->debugStateHistory, parse_Login2StateDebugInfo);

    StructDestroy(parse_Login2RedirectInfo, redirectInfo);
    return true;
}

// This command runs in the source shard.  It is called by the remote shard to return the address info that the client needs to do the redirect.
AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(APPSERVER);
void
aslLogin2_RedirectLogin_Return(U64 returnToken, ACMD_OWNABLE(Login2RedirectDestinationInfo) ppRedirectdestinationInfo)
{
    Login2State *loginState;

    loginState = aslLogin2_GetActiveLoginState(returnToken);
    if ( loginState == NULL )
    {
        aslLogin2_Log("aslLogin2_RedirectLogin_Return: Received return for unknown loginState.  returnToken = %llu", returnToken);
        return;
    }

    if ( loginState->redirectDestinationInfo != NULL )
    {
        StructDestroy(parse_Login2RedirectDestinationInfo, loginState->redirectDestinationInfo);
    }

    // Take ownership of the redirectDestinationInfo struct.
    loginState->redirectDestinationInfo = *ppRedirectdestinationInfo;
    *ppRedirectdestinationInfo = NULL;

    return;
}

// This command runs in the remote shard.  It records the redirect info from the source shard and sends back its address, which the
//  source shard will need to send to the client.
AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(APPSERVER);
void
aslLogin2_RedirectLogin_Remote(Login2InterShardDestination *returnDestination, ACMD_OWNABLE(Login2RedirectInfo) ppRedirectInfo)
{
    Login2RedirectDestinationInfo redirectDestinationInfo = {0};

    // Save the redirect info, taking ownership so that it doesn't get automatically freed.
    aslLogin2_AddPendingRedirect(*ppRedirectInfo);
    *ppRedirectInfo = NULL;

    // Get address info which the client will need to connect.
    redirectDestinationInfo.destinationIP = getHostPublicIp();
    redirectDestinationInfo.destinationPort = gLoginServerListeningPort;

    // Return address info to the calling shard.
    RemoteCommand_Intershard_aslLogin2_RedirectLogin_Return(returnDestination->shardName, returnDestination->serverType, returnDestination->serverID,
        returnDestination->requestToken, &redirectDestinationInfo);
}

void
aslLogin2_RedirectLogin(Login2State *loginState, const char *shardName)
{
    Login2InterShardDestination myDestination = {0};
    Login2RedirectInfo redirectInfo;

    // Fill in the redirectInfo with data from the loginState.  It is NOT deep copied, so we don't need to destroy redirectInfo.
    aslLogin2_FillRedirectInfo(loginState, &redirectInfo);

    // Fill in the destination struct, which will tell the remote shard where to send the response.
    Login2_FillDestinationStruct(&myDestination, loginState->loginCookie);

    RemoteCommand_Intershard_aslLogin2_RedirectLogin_Remote(shardName, GLOBALTYPE_LOGINSERVER, aslLogin2_GetRandomServerOfTypeInShard(shardName, GLOBALTYPE_LOGINSERVER), &myDestination, &redirectInfo);
}

#include "AutoGen/aslLogin2_Redirect_h_ast.c"