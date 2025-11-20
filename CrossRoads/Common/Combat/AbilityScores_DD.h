/***************************************************************************
*     Copyright (c) 2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#pragma once
GCC_SYSTEM

typedef struct NOCONST(AssignedStats) NOCONST(AssignedStats);

// The minimum ability score for D&D rules
#define DD_MIN_ABILITY_SCORE 8

// The number of stat points given to distribute to the ability scores
#define DD_MAX_STAT_POINTS 32

extern const int g_DDAbilityStatPointCostLookupTable[11];

// If bCheckPointsRemaining is set to true, function makes sure that points left is 0
bool DDIsAbilityScoreSetValid(NOCONST(AssignedStats) **eaAssignedStats, bool bCheckPointsRemaining);

// Returns -1 in case there is an invalid value, otherwise returns the points left
S32 DDGetPointsLeftForAbilityScores(NOCONST(AssignedStats) **eaAssignedStats);

F32 DDGetBaseAbilityScore(const char *pszAttribName);
F32 DDApplyStatPointsToBaseAbilityScore(const char *pszAttribName, NOCONST(AssignedStats) *pAssignedStats);
F32 DDGetAbilityScoreFromAssignedStats(NOCONST(AssignedStats) **eaAssignedStats, const char *pszAttribName);