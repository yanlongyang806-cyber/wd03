/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

typedef struct Entity Entity;
typedef struct PlayerStat PlayerStat;
typedef struct PlayerStatsInfo PlayerStatsInfo;

typedef void(*PlayerStatUpdateFunc)(const char *pchPooledPlayerStatName, U32 uOldValue, U32 uNewValue, void *pUserData);

// functions to start or stop tracking on a PlayerStat
void playerstats_BeginTracking(PlayerStatsInfo *pStatsInfo, const char *pchPooledStatName, PlayerStatUpdateFunc updateFunc, void *pUserData);
void playerstats_StopTrackingAllForListener(PlayerStatsInfo *pStatsInfo, void *pUserData);


// Sets a stat value by name... don't use this
const char* playerstats_SetByName(Entity *pPlayerEnt, const char *pchStatName, U32 uValue);

// Gets a stat value by name
U32 playerstat_GetValue(PlayerStatsInfo *pStatsInfo, const char *pchStatName);

void playerstats_SetStat(Entity *pEnt, PlayerStatsInfo *pStatsInfo, PlayerStat *pStat, U32 uNewValue);
PlayerStat* playerstats_GetOrCreateStat(PlayerStatsInfo *pStatsInfo, const char *pchPooledStatName);

// Callbacks to load/unload a map
void playerstats_MapLoad(bool bFullInit);
void playerstats_MapUnload(void);