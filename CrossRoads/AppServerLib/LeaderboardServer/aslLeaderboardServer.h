/***************************************************************************
*     Copyright (c) 2005-2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/
#pragma once

#include "Leaderboard.h"

//Interval in seconds between auto saves
#define LEADERBOARD_AUTOSAVE_INTERVAL 600

AUTO_ENUM;
typedef enum LeaderboardStatus {
	kLeaderboard_Waiting = 0,
	kLeaderboard_Current,
	kLeaderboard_Locked,
}LeaderboardStatus;

AUTO_STRUCT;
typedef struct Leaderboard {
	char *pchKey;						AST(KEY) //Key name: <leaderboardName>_<interval>
	int eStatus;						AST(SUBTABLE(LeaderboardStatusEnum))
	const char *pchLeaderboard;			AST(POOL_STRING)//The name of the leaderboard def being used
	Ranking **ppRankings;				AST(NO_INDEX UNOWNED) //UNSORTED, the key of Ranking is the player ID. This needs to be sorted by the current rank
	Ranking **ppPlayerSorted;			//An array with all the same elements as ppRankings, but sorted by player ID instead
	int iDateStart;					
	int iDateEnd;						
	int iInterval;					
	int iUpdates;							
}Leaderboard;

void LeaderboardInit(void);
int LeaderboardLibOncePerFrame(F32 fElapsed);

void leaderboarddata_postLoad(LeaderboardData *pData);
void leaderboard_postLoad(Leaderboard *pLeaderboard);

void leaderboard_autoSave(void);