/***************************************************************************
*     Copyright (c) 2005-2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct Leaderboard Leaderboard;
typedef struct LeaderboardData LeaderboardData;

void leaderboardDBInit(void);
void leaderboardDBAddToHog(Leaderboard *pLeaderboard);
void leaderboardDataDBAddToHog(LeaderboardData *pData);