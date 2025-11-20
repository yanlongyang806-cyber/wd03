/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslGroupProject.h"
#include "GroupProjectCommon.h"
#include "ExpressionMinimal.h"

// Returns true when the group project data for a map is all set up.
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GroupProjectMapDataReady);
ExprFuncReturnVal
exprGroupProject_GroupProjectMapDataReady(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pRet, 
    /* GroupProjectType */ int projectType)
{
    bool ready = gslGroupProject_GroupProjectMapDataReady(projectType, iPartitionIdx);

    *pRet = ready;
    return ExprFuncReturnFinished;
}

// Returns true when the group project data for a map is all set up.
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GuildProjectMapDataReady);
ExprFuncReturnVal
exprGroupProject_GuildProjectMapDataReady(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pRet)
{
    return exprGroupProject_GroupProjectMapDataReady(pContext, iPartitionIdx, pRet, GroupProjectType_Guild);
}

// Gets the value of a GroupProjectNumeric associated with the current map.
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GetGroupProjectNumericValueFromMap);
ExprFuncReturnVal
exprGroupProject_GetGroupProjectNumericValueFromMap(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pRet, 
    /* GroupProjectType */ int projectType, const char *projectName, const char *numericName, ACMD_EXPR_ERRSTRING errString)
{
    S32 value = 0;

    if ( !gslGroupProject_GetGroupProjectNumericValueFromMap(iPartitionIdx, projectType, projectName, numericName, &value, errString) )
    {
        return ExprFuncReturnError;
    }
    else
    {
        *pRet = value;
        return ExprFuncReturnFinished;
    }
}

// Gets the value of a GroupProjectNumeric associated with the guild owning the current map.
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GetGuildProjectNumericValueFromMap);
ExprFuncReturnVal
exprGuildProject_GetGuildProjectNumericValueFromMap(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pRet, 
    const char *projectName, const char *numericName, ACMD_EXPR_ERRSTRING errString)
{
    return exprGroupProject_GetGroupProjectNumericValueFromMap(pContext, iPartitionIdx, pRet, GroupProjectType_Guild, projectName, numericName, errString);
}

// Gets the value of a GroupProjectNumeric associated with the current map.
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GetGroupProjectUnlockFromMap);
ExprFuncReturnVal
exprGroupProject_GetGroupProjectUnlockFromMap(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pRet, 
    /* GroupProjectType */ int projectType, const char *projectName, const char *unlockName, ACMD_EXPR_ERRSTRING errString)
{
    GroupProjectState *projectState;
    GroupProjectUnlockDefRef *unlockDefRef;

    // Find the project state.
    projectState = gslGroupProject_GetGroupProjectStateForMap(projectType, projectName, iPartitionIdx);
    if ( projectState == NULL )
    {
        // If the project state can't be found, then check and see if the named project even exists.
        if ( RefSystem_ReferentFromString(g_GroupProjectDict, projectName) == NULL )
        {
            estrPrintf(errString, "GroupProjectDef %s does not exist", projectName);
            return ExprFuncReturnError;
        }

        *pRet = false;
        return ExprFuncReturnFinished;
    }

    // Find the unlock.
    unlockDefRef = eaIndexedGetUsingString(&projectState->unlocks, unlockName);

    // Return whether the unlock is set.
    *pRet = ( unlockDefRef != NULL );

    return ExprFuncReturnFinished;
}

// Gets the value of a GroupProjectNumeric associated with the guild owning the current map.
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GetGuildProjectUnlockFromMap);
ExprFuncReturnVal
exprGuildProject_GetGuildProjectUnlockFromMap(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pRet, 
    const char *projectName, const char *unlockName, ACMD_EXPR_ERRSTRING errString)
{
    return exprGroupProject_GetGroupProjectUnlockFromMap(pContext, iPartitionIdx, pRet, GroupProjectType_Guild, projectName, unlockName, errString);
}
