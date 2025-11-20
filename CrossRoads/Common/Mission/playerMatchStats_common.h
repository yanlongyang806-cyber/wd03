#ifndef PLAYER_MATCH_STATS_H
#define PLAYER_MATCH_STATS_H

typedef U32 EntityRef;
typedef U32 ContainerID;
typedef struct MissionDefRef MissionDefRef;

AUTO_STRUCT; 
typedef struct PlayerMatchStat
{
	const char *pchStatName;		AST(POOL_STRING KEY)
	U32 uValue;
} PlayerMatchStat;

AUTO_STRUCT;
typedef struct PlayerMatchInfo
{
	char *					pchPlayerName;		
	const char *			pchFactionName;	AST(POOL_STRING)

	int						iPartitionIdx;
	EntityRef				erEntity;
	ContainerID				iContainerID;
	// the snapshot state of the player stats
	PlayerMatchStat**		eaPlayerStats;

	// the missions this player completed during the match
	MissionDefRef**			eaPlayerMissionsCompleted;
} PlayerMatchInfo;

AUTO_STRUCT;
typedef struct PerMatchPlayerStatList
{
	PlayerMatchInfo		**eaPlayerMatchStats;

} PerMatchPlayerStatList;



PlayerMatchInfo* playermatchstats_FindPlayerByEntRef(PerMatchPlayerStatList *pPerMatchStats, EntityRef erEnt);
PlayerMatchInfo* playermatchstats_FindPlayerByContainerID(PerMatchPlayerStatList *pPerMatchStats, ContainerID iID);
U32 playermatchstats_GetValue(const PlayerMatchInfo *pMatchInfo, const char *pchStatsName);


#endif