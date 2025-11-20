/***************************************************************************
*     Copyright (c) 2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#ifndef GSLTEAM_H
#define GSLTEAM_H
GCC_SYSTEM

typedef struct QueuePartitionInfo QueuePartitionInfo;

void gslTeam_RemoveMemberFromPendingList(QueuePartitionInfo* pInfo, int iMemberID);
void gslTeam_AutoTeamUpdate(QueuePartitionInfo *pInfo);
void gslTeam_CleanupLocalTeams(QueuePartitionInfo *pInfo);

#endif //GSLTEAM_H
