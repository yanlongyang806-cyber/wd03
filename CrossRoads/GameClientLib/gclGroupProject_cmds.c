/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GroupProjectCommon.h"
#include "gclGroupProject.h"
#include "StashTable.h"
#include "StringCache.h"

#include "AutoGen/GroupProjectCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD ACMD_CATEGORY(GroupProject);
void
gclGroupProject_ReceiveProjectDefsByType(/* GroupProjectType */ int projectType, GroupProjectDefs *projectDefs)
{
    GroupProjectDefs *tmpDefs;

    // If it doesn't exist, create the stash table used to map project types to project defs.
    if ( g_GroupProjectDefsByType == NULL )
    {
        g_GroupProjectDefsByType = stashTableCreateInt(1);
    }

    // Remove any previous mapping for this type.
    if( stashIntRemovePointer(g_GroupProjectDefsByType, projectType, &tmpDefs) )
    {
        if ( tmpDefs )
        {
            StructDestroy(parse_GroupProjectDefs, tmpDefs);
        }
    }

    // Make a local copy of the group project defs.
    tmpDefs = StructClone(parse_GroupProjectDefs, projectDefs);

    // Save the group project defs in the mapping stash table.
    stashIntAddPointer(g_GroupProjectDefsByType, projectType, tmpDefs, false);

    return;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD ACMD_CATEGORY(GroupProject);
void
gclGroupProject_ContributionNotify(ContributionNotifyData *notifyData)
{
    S32 i;
    ContributionNotifyData *tmpData = NULL;

    for (i = eaSize(&g_GroupProjectContributions) - 1; i >= 0; i--)
    {
        tmpData = g_GroupProjectContributions[i];
        if( tmpData->projectContainerID == notifyData->projectContainerID && tmpData->projectcontainerType == notifyData->projectcontainerType && tmpData->projectName == notifyData->projectName && tmpData->bucketName == notifyData->bucketName )
        {
            tmpData = eaRemove(&g_GroupProjectContributions, i);
            break;
        }
    }

    if( i >= 0 )
    {
		StructDestroy(parse_ContributionNotifyData, tmpData);
    }

    tmpData = StructClone(parse_ContributionNotifyData, notifyData);

    eaPush(&g_GroupProjectContributions, tmpData);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD ACMD_CATEGORY(GroupProject);
void
gclGroupProject_SetMapState(const char *allegiance, U32 ownerType, U32 ownerID)
{
	if (g_GroupProjectMapClientState.allegiance)
		StructFreeStringSafe(&g_GroupProjectMapClientState.allegiance);
	if (allegiance && *allegiance)
		g_GroupProjectMapClientState.allegiance = StructAllocString(allegiance);
	g_GroupProjectMapClientState.ownerType = ownerType;
	g_GroupProjectMapClientState.ownerID = ownerID;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD ACMD_CATEGORY(GroupProject);
void
gclGroupProject_ReceiveRewards(const char *projectName, const char *taskName, InvRewardRequest *rewardRequest)
{
    gclGroupProject_SetTaskRewards(projectName, taskName, rewardRequest);
}
