#ifndef GSL_SCOREBOARD_H
#define GSL_SCOREBOARD_H

GCC_SYSTEM

#include "pvp_common.h"

typedef struct CritterFaction CritterFaction;

AUTO_STRUCT;
typedef struct ScoreboardPartitionInfo
{
	int iPartitionIdx; AST(KEY)

	REF_TO(CritterFaction) hDefaultFaction;

	ScoreboardEntityList Scores;

	bool bRemoveInactivePlayerScores;
} ScoreboardPartitionInfo;

ScoreboardPartitionInfo* gslScoreboard_PartitionInfoFromIdx(int iPartitionIdx);
ScoreboardPartitionInfo* gslScoreboard_CreateInfo(int iPartitionIdx);
void gslScoreboard_SetDefaultFaction(int iPartitionIdx, CritterFaction* pFaction);
void gslScoreboard_CreateGroup(int iPartitionIdx, CritterFaction* pFaction, Message* pDisplayMessage, const char *pchGroupTexture);
void gslScoreboard_CreatePlayerScoreData(Entity* pEnt);
void gslScoreboard_DestroyPlayerScoreData(Entity* pEnt);
void gslScoreboard_Reset(int iPartitionIdx);
void gslScoreboard_PartitionUnload(int iPartitionIdx);
void gslScoreboard_MapUnload(void);
void gslScoreboard_Tick(F32 fElapsedTime);

#endif //GSL_SCOREBOARD_H
