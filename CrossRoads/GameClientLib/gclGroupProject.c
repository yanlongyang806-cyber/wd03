/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GroupProjectCommon.h"
#include "gclGroupProject.h"
#include "StashTable.h"
#include "file.h"
#include "Entity.h"
#include "Expression.h"
#include "GameClientLib.h"
#include "UICore.h"
#include "Guild.h"
#include "Player.h"
#include "inventoryCommon.h"
#include "gclEntity.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

StashTable g_GroupProjectDefsByType = NULL;
StashTable g_DonationTaskRewards = NULL;
ContributionNotifyData **g_GroupProjectContributions = NULL;
GroupProjectMapClientState g_GroupProjectMapClientState = {0};

GroupProjectDefs *
gclGroupProject_GetProjectDefsForType(GroupProjectType projectType)
{
    GroupProjectDefs *projectDefs;

    if ( g_GroupProjectDefsByType == NULL )
    {
        // If it doesn't exist, create the stash table used to map project types to project defs.
        g_GroupProjectDefsByType = stashTableCreateInt(1);
    }
    else if ( !isDevelopmentMode() && stashIntFindPointer(g_GroupProjectDefsByType, projectType, &projectDefs ) )
    {
        // If we already have the defs cached, then just return them.
        return projectDefs;
    }

    // Create mapping to NULL defs.  This is to prevent making multiple requests.  The mapping will be replaced when the results are returned.
    stashIntAddPointer(g_GroupProjectDefsByType, projectType, NULL, false);
    
    // Ask the server to send the defs.
    ServerCmd_gslGroupProject_RequestProjectDefsByType(projectType);

    return NULL;
}

void
gclGroupProject_SubscribeToGuildProject(void)
{
    ServerCmd_gslGroupProject_SubscribeToGuildProject();
}

void
gclGroupProject_SubscribeToPlayerProject(void)
{
    ServerCmd_gslGroupProject_SubscribeToPlayerProject();
}

ContributionNotifyData *
gclGroupProject_GetContributionNotify(Entity *player, GroupProjectState *projectState, DonationTaskSlot *donationSlot, const char *bucketName)
{
    S32 i;
    ContributionNotifyData *tmpData;
    GroupProjectDef *projectDef = projectState ? GET_REF(projectState->projectDef) : NULL;
    DonationTaskDef *task = donationSlot ? GET_REF(donationSlot->taskDef) : NULL;

    if( player == NULL || projectDef == NULL || task == NULL )
    {
        return NULL;
    }

    for (i = eaSize(&g_GroupProjectContributions) - 1; i >= 0; i--)
    {
        tmpData = g_GroupProjectContributions[i];
        if( tmpData->projectName == projectDef->name && tmpData->taskName == task->name && tmpData->bucketName == bucketName )
        {
            if( tmpData->playerID == entGetContainerID(player) )
            {
                return tmpData;
            }
            else
            {
                return NULL;
            }
        }
    }

    return NULL;
}

void
gclGroupProject_ResetContributionNotify(GroupProjectState *projectState, DonationTaskSlot *donationSlot, const char *bucketName)
{
    S32 i;
    ContributionNotifyData *tmpData;
    GroupProjectDef *projectDef = projectState ? GET_REF(projectState->projectDef) : NULL;
    DonationTaskDef *task = donationSlot ? GET_REF(donationSlot->taskDef) : NULL;

    if( projectDef == NULL || task == NULL )
    {
        return;
    }

    for (i = eaSize(&g_GroupProjectContributions) - 1; i >= 0; i--)
    {
        tmpData = g_GroupProjectContributions[i];
        if( tmpData->projectName == projectDef->name && tmpData->taskName == task->name && tmpData->bucketName == bucketName )
        {
            tmpData->playerID = 0;
            break;
        }
    }
}

bool
gclGroupProject_GetMapState(void)
{
    static U32 s_LastRequestTime;
    static U32 s_FrameLatch;

    if (gGCLState.totalElapsedTimeMs >= s_LastRequestTime
        && g_ui_State.uiFrameCount >= s_FrameLatch)
    {
        ServerCmd_gslGroupProject_GetMapState();
        s_LastRequestTime = gGCLState.totalElapsedTimeMs + 30000;
    }
    s_FrameLatch = g_ui_State.uiFrameCount + 5;
    return true;
}

void
gclGroupProject_SetTaskRewards(const char *projectName, const char *taskName, InvRewardRequest *rewardRequest)
{
    GroupProjectDef *projectDef = RefSystem_ReferentFromString("GroupProjectDef", projectName);
    DonationTaskRewardData *rewardData;
    StashTable rewardsTable;

    if ( projectDef == NULL )
    {
        return;
    }

    // Check the project type
    switch ( projectDef->type )
    {
    case GroupProjectType_Player:
        if (g_DonationTaskRewards == NULL)
            g_DonationTaskRewards = stashTableCreateAddress(16);
        rewardsTable = g_DonationTaskRewards;
        break;
    default:
        return;
    }

    taskName = allocAddString(taskName);
    if ( taskName == NULL )
    {
        return;
    }

    if ( !stashAddressFindPointer(rewardsTable, taskName, &rewardData) || rewardData == NULL )
    {
        rewardData = callocStruct(DonationTaskRewardData);
    }

    if ( rewardData != NULL )
    {
        stashAddressAddPointer(g_DonationTaskRewards, taskName, rewardData, true);

        rewardData->lastRewardsTime = timeSecondsSince2000();

        if (rewardRequest)
        {
            if ( rewardData->lastRewards == NULL )
                rewardData->lastRewards = StructCreate(parse_InvRewardRequest);
            else
                StructReset(parse_InvRewardRequest, rewardData->lastRewards);

            inv_FillRewardRequestClient(entActivePlayerPtr(), rewardRequest, rewardData->lastRewards, false);
        }
        else if ( rewardData->lastRewards != NULL )
        {
            StructDestroySafe(parse_InvRewardRequest, &rewardData->lastRewards);
        }
    }
}

InvRewardRequest *
gclGroupProject_GetTaskRewards(const char *projectName, const char *taskName)
{
    GroupProjectDef *projectDef = RefSystem_ReferentFromString("GroupProjectDef", projectName);
    DonationTaskRewardData *rewardData;
    StashTable rewardsTable;

    if ( projectDef == NULL )
    {
        return NULL;
    }

    // Check the project type
    switch ( projectDef->type )
    {
    case GroupProjectType_Player:
        if (g_DonationTaskRewards == NULL)
            g_DonationTaskRewards = stashTableCreateAddress(16);
        rewardsTable = g_DonationTaskRewards;
        break;
    default:
        return NULL;
    }

    taskName = allocAddString(taskName);
    if ( taskName == NULL )
    {
        return NULL;
    }

    if ( !stashAddressFindPointer(rewardsTable, taskName, &rewardData) || rewardData == NULL )
    {
        rewardData = callocStruct(DonationTaskRewardData);
    }

    if ( rewardData )
    {
        // Does the task not have any rewards?
        if ( rewardData->lastRewardsTime > 0 && rewardData->lastRewards == NULL )
        {
            return rewardData->lastRewards;
        }

        // Cache received rewards for 15 minutes
        if ( rewardData->lastRewardsTime > 0 && rewardData->lastRewardsTime - timeSecondsSince2000() < 900 )
        {
            return rewardData->lastRewards;
        }

        // Refresh the rewards with a minimum retry interval of 30 seconds
        // only if the request isn't being spammed every frame.
        if (gGCLState.totalElapsedTimeMs >= rewardData->lastRequestTime
            && g_ui_State.uiFrameCount >= rewardData->lastRequestFrame)
        {
            ServerCmd_gslPlayerProject_GetRewardBags(projectName, taskName);
            rewardData->lastRequestFrame = gGCLState.totalElapsedTimeMs + 30000;
        }
        rewardData->lastRequestFrame = g_ui_State.uiFrameCount + 5;

        return rewardData->lastRewards;
    }

    return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsOnGuildOwnedMap);
bool
exprIsOnGuildOwnedMap(SA_PARAM_OP_VALID Entity *pEnt)
{
    Guild *pGuild = guild_GetGuild(pEnt);
    gclGroupProject_GetMapState();
    if (pGuild && g_GroupProjectMapClientState.ownerType == GLOBALTYPE_GUILD)
    {
        return pGuild->iContainerID == g_GroupProjectMapClientState.ownerID;
    }
    return false;
}
