#pragma once
/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "stdtypes.h"

typedef enum PlayerType PlayerType;
typedef struct Login2StateDebugInfo Login2StateDebugInfo;
typedef U32 ContainerID;
typedef struct Login2State Login2State;
typedef struct Login2CharacterChoice Login2CharacterChoice;
typedef struct Login2CharacterCreationData Login2CharacterCreationData;
typedef struct Login2CharacterSelectionData Login2CharacterSelectionData;

// The structure that is sent to the remote shard when redirecting a client login.
// It is a subset of Login2State.
AUTO_STRUCT;
typedef struct Login2RedirectInfo
{
    ContainerID accountID;
    const char *accountName;
    const char *accountDisplayName;
    const char *pweAccountName;
    U32 clientIP;
    U32 clientLanguageID;
    S32 clientAccessLevel;
    PlayerType playerType;
    const char *affiliate;          AST(POOL_STRING)

    Login2CharacterChoice *selectedCharacterChoice;
    Login2CharacterCreationData *characterCreationData;
    Login2CharacterSelectionData *characterSelectionData;

    U32 timeRequested;
    U64 transferCookie;

    // This array container the names of any GamePermissions that are granted via account permissions.
    STRING_EARRAY gamePermissionsFromAccountPermissions;    AST(POOL_STRING)

    bool isLifetime;
    bool isPress;
    bool ignoreQueue;
    bool isQueueVIP;
    bool UGCCharacterPlayOnly;
    bool requestedUGCEdit;

    // Debug Data
    EARRAY_OF(Login2StateDebugInfo) debugStateHistory;
} Login2RedirectInfo;

AUTO_STRUCT;
typedef struct Login2RedirectDestinationInfo
{
    U32 destinationIP;
    U32 destinationPort;
} Login2RedirectDestinationInfo;

bool aslLogin2_CreateLoginStateForRedirect(Login2State *loginState);
void aslLogin2_RedirectLogin(Login2State *loginState, const char *shardName);