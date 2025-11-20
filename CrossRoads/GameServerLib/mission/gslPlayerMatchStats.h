#ifndef GSLPLAYER_MATCH_STATS_H
#define GSLPLAYER_MATCH_STATS_H

typedef struct Entity Entity;
typedef struct Mission Mission;

int playermatchstats_IsEnabled(int iPartitionIdx);

int playermatchstats_ShouldTrackMatchStats(void);

void playermatchstats_PartitionLoad(int iPartitionIdx, int bFullInit);
void playermatchstats_PartitionUnload(int iPartitionIdx);

void playermatchstats_MapLoad(int bFullInit);
void playermatchstats_MapUnload(void);

int playermatchstats_RegisterPlayer(Entity *pPlayerEnt);

void playermatchstats_PlayerLeaving(Entity *pPlayerEnt);

void playermatchstats_ReportCompletedMission(SA_PARAM_NN_VALID Entity *pPlayerEnt, SA_PARAM_NN_VALID Mission *pMission);

void playermatchstats_SendMatchStatsToPlayers(int iPartitionIndex);


#endif