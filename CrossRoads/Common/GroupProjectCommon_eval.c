/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GroupProjectCommon.h"
#include "Expression.h"

#if GAMESERVER
#include "gslGroupProject.h"
#include "gslEventTracker.h"
#include "GameEvent.h"
#include "mission_common.h"
#endif

#if GAMECLIENT
#include "gclGroupProject.h"
#endif

extern const char *g_ProjectStateVarName;

// Gets the value of a GroupProjectNumeric.
AUTO_EXPR_FUNC(GroupProject) ACMD_NAME(GetGroupProjectNumericValue);
ExprFuncReturnVal
exprGroupProject_GetGroupProjectNumericValue(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pRet, 
    const char *numericName, ACMD_EXPR_ERRSTRING errString)
{
    GroupProjectState *projectState;
    GroupProjectNumericData *numericData;

    // Find the project state.
    projectState = exprContextGetVarPointerUnsafePooled(pContext, g_ProjectStateVarName);
    if ( projectState == NULL )
    {
        estrPrintf(errString, "GroupProjectState not found");
        return ExprFuncReturnError;
    }

    // Find the numeric.
    numericData = eaIndexedGetUsingString(&projectState->numericData, numericName);
    if ( numericData == NULL )
    {
        GroupProjectDef *projectDef = GET_REF(projectState->projectDef);
        if ( projectDef )
        {
            if ( eaIndexedGetUsingString(&projectDef->validNumerics, numericName) )
            {
                // The numeric is valid, so return 0 which is the default value.
                *pRet = 0;
                return ExprFuncReturnFinished;
            }
        }
        estrPrintf(errString, "GroupProjectNumericData %s not found", numericName);
        return ExprFuncReturnError;
    }

    // Return the value.
    *pRet = numericData->numericVal;
    return ExprFuncReturnFinished;
}

// Gets the value of a GroupProjectNumeric.
AUTO_EXPR_FUNC(GroupProject) ACMD_NAME(GetGroupProjectUnlock);
ExprFuncReturnVal
exprGroupProject_GetGroupProjectUnlock(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pRet, 
    const char *unlockName, ACMD_EXPR_ERRSTRING errString)
{
    GroupProjectState *projectState;
    GroupProjectUnlockDefRef *unlockDefRef;

    // Find the project state.
    projectState = exprContextGetVarPointerUnsafePooled(pContext, g_ProjectStateVarName);
    if ( projectState == NULL )
    {
        estrPrintf(errString, "GroupProjectState not found");
        return ExprFuncReturnError;
    }

    // Find the unlock.
    unlockDefRef = eaIndexedGetUsingString(&projectState->unlocks, unlockName);

    // Return whether the unlock is set.
    *pRet = ( unlockDefRef != NULL );

    return ExprFuncReturnFinished;
}

// Gets the value of a GroupProjectNumeric associated with the current map.
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(CheckGuildAllegianceForGroupProjectMap);
ExprFuncReturnVal
exprGroupProject_CheckGuildAllegianceForGroupProjectMap(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pRet, const char *allegiance)
{
    const char *mapAllegiance = NULL;

#if GAMESERVER
    mapAllegiance = gslGroupProject_GetGuildAllegianceForGroupProjectMap(iPartitionIdx);
#endif

#if GAMECLIENT
    gclGroupProject_GetMapState();
    mapAllegiance = g_GroupProjectMapClientState.allegiance;
#endif

    if ( mapAllegiance == NULL )
    {
        *pRet = 0;
        return ExprFuncReturnFinished;
    }

    // Returns true if the allegiance of the map matches the passed in allegiance.
    *pRet = ( stricmp(mapAllegiance, allegiance) == 0 );

    return ExprFuncReturnFinished;
}

// Gets the value of a GroupProjectNumeric from a player.
AUTO_EXPR_FUNC(Player) ACMD_NAME(GetGroupProjectNumericValueFromPlayer);
ExprFuncReturnVal
exprGroupProject_GetGroupProjectNumericValueFromPlayer(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pRet, 
    /* GroupProjectType */ int projectType, const char *projectName, const char *numericName, ACMD_EXPR_ERRSTRING errString)
{
    Entity *playerEnt = exprContextGetVarPointerUnsafe(pContext, "Player");

    if ( GroupProject_GetNumericFromPlayerExprHelper(playerEnt, projectType, projectName, numericName, pRet, errString) )
    {
        return ExprFuncReturnFinished;
    }
    else
    {
        return ExprFuncReturnError;
    }
}

// Gets the value of a GroupProjectUnlock from a player.
AUTO_EXPR_FUNC(Player) ACMD_NAME(GetGroupProjectUnlockFromPlayer);
ExprFuncReturnVal
exprGroupProject_GetGroupProjectUnlockFromPlayer(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pRet, 
    /* GroupProjectType */ int projectType, const char *projectName, const char *unlockName, ACMD_EXPR_ERRSTRING errString)
{
    Entity *playerEnt = exprContextGetVarPointerUnsafe(pContext, "Player");

    if ( GroupProject_GetUnlockFromPlayerExprHelper(playerEnt, projectType, projectName, unlockName, pRet, errString) )
    {
        return ExprFuncReturnFinished;
    }
    else
    {
        return ExprFuncReturnError;
    }
}

// Gets the value of a Guild GroupProjectNumeric from a player.
AUTO_EXPR_FUNC(Player) ACMD_NAME(GetGuildProjectNumericValueFromPlayer);
ExprFuncReturnVal
exprGroupProject_GetGuildProjectNumericValueFromPlayer(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pRet, 
    const char *projectName, const char *numericName, ACMD_EXPR_ERRSTRING errString)
{
    return exprGroupProject_GetGroupProjectNumericValueFromPlayer(pContext, iPartitionIdx, pRet, GroupProjectType_Guild, projectName, numericName, errString);
}

// Gets the value of a Guild GroupProjectUnlock from a player.
AUTO_EXPR_FUNC(Player) ACMD_NAME(GetGuildProjectUnlockFromPlayer);
ExprFuncReturnVal
exprGroupProject_GetGuildProjectUnlockFromPlayer(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pRet, 
    const char *projectName, const char *unlockName, ACMD_EXPR_ERRSTRING errString)
{
    return exprGroupProject_GetGroupProjectUnlockFromPlayer(pContext, iPartitionIdx, pRet, GroupProjectType_Guild, projectName, unlockName, errString);
}

// Gets the value of a Player GroupProjectNumeric from a player.
AUTO_EXPR_FUNC(Player,Mission) ACMD_NAME(GetPlayerProjectNumericValueFromPlayer);
ExprFuncReturnVal
exprGroupProject_GetPlayerProjectNumericValueFromPlayer(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pRet, 
    const char *projectName, const char *numericName, ACMD_EXPR_ERRSTRING errString)
{
    return exprGroupProject_GetGroupProjectNumericValueFromPlayer(pContext, iPartitionIdx, pRet, GroupProjectType_Player, projectName, numericName, errString);
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal
exprGroupProject_Unlock_LoadVerify(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pRet, 
    const char *projectName, const char *unlockName, ACMD_EXPR_ERRSTRING errString)
{
#if GAMESERVER
    MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
    if (pMissionDef) {
        GameEvent *pEvent = StructCreate(parse_GameEvent);
        char *estrBuffer = NULL;

        estrPrintf(&estrBuffer, "GroupProjectTaskComplete_%s", projectName);
        pEvent->type = EventType_GroupProjectTaskCompleted;
        pEvent->pchEventName = allocAddString(estrBuffer);
        pEvent->pchGroupProjectName = allocAddString(projectName);
        pEvent->tMatchSource = TriState_Yes;

        eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);

        estrDestroy(&estrBuffer);
    }
#endif

    return ExprFuncReturnFinished;
}

// Gets the value of a Player GroupProjectUnlock from a player.
AUTO_EXPR_FUNC(Player,Mission) ACMD_NAME(GetPlayerProjectUnlockFromPlayer) ACMD_EXPR_STATIC_CHECK(exprGroupProject_Unlock_LoadVerify);
ExprFuncReturnVal
exprGroupProject_GetPlayerProjectUnlockFromPlayer(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pRet, 
    const char *projectName, const char *unlockName, ACMD_EXPR_ERRSTRING errString)
{
    return exprGroupProject_GetGroupProjectUnlockFromPlayer(pContext, iPartitionIdx, pRet, GroupProjectType_Player, projectName, unlockName, errString);
}