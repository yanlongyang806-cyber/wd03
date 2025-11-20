#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GlobalTypeEnum.h"

typedef struct InvRewardRequest InvRewardRequest;

extern StashTable g_GroupProjectDefsByType;
extern ContributionNotifyData **g_GroupProjectContributions;

typedef struct GroupProjectMapClientState
{
    char *allegiance;
    GlobalType ownerType;
    U32 ownerID;
} GroupProjectMapClientState;

typedef struct DonationTaskRewardData
{
    U32 lastRequestTime;
    U32 lastRequestFrame;
    U32 lastRewardsTime;
    InvRewardRequest *lastRewards;
} DonationTaskRewardData;

extern GroupProjectMapClientState g_GroupProjectMapClientState;

GroupProjectDefs *
gclGroupProject_GetProjectDefsForType(GroupProjectType projectType);
void
gclGroupProject_SubscribeToGuildProject(void);
void
gclGroupProject_SubscribeToPlayerProject(void);
ContributionNotifyData *
gclGroupProject_GetContributionNotify(Entity *player, GroupProjectState *projectState, DonationTaskSlot *donationSlot, const char *bucketName);
void
gclGroupProject_ResetContributionNotify(GroupProjectState *projectState, DonationTaskSlot *donationSlot, const char *bucketName);
bool
gclGroupProject_GetMapState(void);
void
gclGroupProject_SetTaskRewards(const char *projectName, const char *taskName, InvRewardRequest *rewardRequest);
InvRewardRequest *
gclGroupProject_GetTaskRewards(const char *projectName, const char *taskName);
