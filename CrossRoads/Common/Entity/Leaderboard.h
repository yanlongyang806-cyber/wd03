/***************************************************************************
*     Copyright (c) 2005-2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "GlobalTypeEnum.h"

#include "Message.h"

#ifndef LEADERBOARD_H__
#define LEADREBOARD_H__

typedef struct DisplayMessage DisplayMessage;
typedef struct Expression Expression;
typedef struct Entity Entity;

AUTO_ENUM;
typedef enum LeaderboardType{
	kLeaderboardType_Ongoing,
	kLeaderboardType_Interval,
	kLeaderboardType_OneTime
}LeaderboardType;

AUTO_ENUM;
typedef enum LeaderboardRankingType{
	kLeaderboardRanking_Accumulate,
	kLeaderboardRanking_TrueSkill,
	kLeaderboardRanking_Elo
}LeaderboardRankingType;

AUTO_STRUCT AST_CONTAINER;
typedef struct LeaderboardRank{
	F32 fMean;			AST(PERSIST NO_TRANSACT)//Average skill (Mu)
	F32 fDeviation;		AST(PERSIST NO_TRANSACT)//how accurate that skill ranking is (Sigma)
}LeaderboardRank;

AUTO_STRUCT;
typedef struct LeaderboardEval
{
	LeaderboardRankingType eRankingType;
	
	//Accumulate
	Expression *pPointsExpr;				AST(NAME(PointExprBlock) REDUNDANT_STRUCT(PointExpr, parse_Expression_StructParam) LATEBIND)		

	//True skill and Elo
	LeaderboardRank sDefaultRank;			AST(NAME(DefaultRank))
	F32 fRankingBeta; 						AST(DEFAULT(4.1666))
	F32 fRankingDynamicsFactor;				AST(DEFAULT(0.8333))
}LeaderboardEval;


AUTO_STRUCT;
typedef struct LeaderboardDef {
	const char *pchLeaderboardKey;		AST(KEY NAME(LeaderboardKey) POOL_STRING STRUCTPARAM)

	DisplayMessage pDisplayMessage;		AST(STRUCT(parse_DisplayMessage) NAME(DisplayMessage) NAME(DisplayName))
	DisplayMessage pDescriptionMessage;	AST(STRUCT(parse_DisplayMessage) NAME(DescriptionMessage) NAME(Description))

	LeaderboardType eType;

	LeaderboardEval sEval;				AST(EMBEDDED_FLAT)
	
	U32 iDateStart;						NO_AST
	U32 iDateInterval;					NO_AST

	char *pchDateStart;	 
	U32 iIntervalHours;
	U32 iIntervalDays;
}LeaderboardDef;

AUTO_STRUCT;
typedef struct LeaderboardDataDef {
	const char *pchKey;					AST(KEY STRUCTPARAM POOL_STRING)
	Expression *pValueExpr;				AST(NAME(ValueExprBlock) REDUNDANT_STRUCT(ValueExpr, parse_Expression_StructParam) LATEBIND)			
}LeaderboardDataDef;

AUTO_STRUCT;
typedef struct LeaderboardDataEntry{
	const char *pchKey;					AST(POOL_STRING KEY)
	char *pchValue;						AST(NAME(value))
}LeaderboardDataEntry;

AUTO_STRUCT;
typedef struct LeaderboardData {
	ContainerID ePlayerID;					AST(KEY NAME(playerID) )
	LeaderboardDataEntry **ppEntry;			
}LeaderboardData;

AUTO_STRUCT;
typedef struct LeaderboardDefs
{
	LeaderboardDef **ppLeaderboards;	AST(NAME(Leaderboard))
	LeaderboardDataDef **ppDataDefs;	AST(NAME(LeaderboardData))
} LeaderboardDefs;

AUTO_STRUCT;
typedef struct LeaderboardPageEntry
{
	int iRank;								AST(KEY)
	ContainerID ePlayerID;					AST(NAME(playerID))
	F32 fScore;								AST(NAME(score))
	LeaderboardDataEntry **ppPlayerData;	AST(NAME(PlayerData))
}LeaderboardPageEntry;

AUTO_STRUCT;
typedef struct LeaderboardPage
{
	const char *pchLeaderboardKey;			AST(POOL_STRING)
	int iInterval;
	
	LeaderboardPageEntry **ppEntries;	
}LeaderboardPage;

AUTO_STRUCT;
typedef struct LeaderboardPageRequest {
	const char *pchLeaderboardKey;			AST(POOL_STRING)
	int iInterval;
	U32 iRankingsPerPage;

	U32 iPageSearch;
	ContainerID ePlayerSearch;
}LeaderboardPageRequest;

AUTO_STRUCT;
typedef struct LeaderboardPageCB {
	EntityRef entRequester;
}LeaderboardPageCB;

AUTO_STRUCT;
typedef struct Ranking {
	ContainerID  ePlayerID;				AST(NAME(playerID) KEY)
	F32 fScore;							AST(NAME(score))
	int iLastUpdate;					AST(NAME(lastUpdate))
	LeaderboardRank sRank;				AST(EMBEDDED_FLAT)
}Ranking;

AUTO_STRUCT AST_CONTAINER;
typedef struct LeaderboardStats
{
	const char *pchLeaderboard;			AST(POOL_STRING PERSIST NO_TRANSACT KEY)
	F32 fPoints;						AST(PERSIST NO_TRANSACT)
	LeaderboardRank sRank;				AST(PERSIST NO_TRANSACT)
	U32 iLastTimestamp;					AST(PERSIST NO_TRANSACT)
}LeaderboardStats;

AUTO_STRUCT;
typedef struct LeaderboardTeam
{
	ContainerID *piTeam;
}LeaderboardTeam;

extern LeaderboardDefs g_sLeaderboardDefs;

void leaderboard_load(void);

LeaderboardDef *leaderboardDef_Find(const char *pchName);

void entity_updateLockedLeaderboardStats(Entity *pEntity);
void entity_updateLeaderboardStatsAll(Entity *pEntity);

void entity_validateLeaderboardStats(Entity *pEntity);
void entity_updateLeaderboardData(Entity *pEntity);

#endif