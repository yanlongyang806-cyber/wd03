/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "CombatEnums.h"
#include "partition_enums.h"
#include "mapstate_common.h"

typedef struct ContactDef ContactDef;
typedef struct ContactDialogOptionData ContactDialogOptionData;
typedef struct Entity Entity;
typedef struct GameEvent GameEvent;
typedef struct MapStateValue MapStateValue;
typedef struct Mission Mission;
typedef struct OpenMission OpenMission;
typedef struct Packet Packet;
typedef struct PlayerMapValues PlayerMapValues;
typedef struct SyncDialog SyncDialog;
typedef enum ScoreboardState ScoreboardState;
typedef struct WorldVariable WorldVariable;
typedef struct MapState MapState;

extern S64 g_ulAbsTimes[MAX_ACTUAL_PARTITIONS+1];
#define ABS_TIME_PARTITION(idx) g_ulAbsTimes[idx]
#define ABS_TIME_SINCE_PARTITION(idx, abstime) ((abstime) ? (ABS_TIME_PARTITION(idx) - (abstime)) : UINT_MAX)
#define ABS_TIME_PASSED_PARTITION(idx, t, dt) (t && ABS_TIME_SINCE_PARTITION(idx, t)>SEC_TO_ABS_TIME(dt))
S64 mapState_GetTime(int iPartitionIdx);

MapState* mapState_FromPartitionIdx(int iPartitionIdx);
MapState* mapState_FromEnt(Entity *pEnt);

bool mapState_SetValue(int iPartitionIdx, SA_PARAM_NN_STR const char *pcValueName, int iNewValue, bool bAddIfMissing);
bool mapState_SetString(int iPartitionIdx, SA_PARAM_NN_STR const char *pcValueName, const char *pcString, bool bAddIfMissing);
bool mapState_AddValue(int iPartitionIdx, SA_PARAM_NN_STR const char *pcValueName, int iStartingValue, SA_PARAM_OP_VALID GameEvent *pEvent);
bool mapState_AddString(int iPartitionIdx, SA_PARAM_NN_STR const char *pcValueName, const char *pcStartingValue);

void mapState_AddPublicVar(int iPartitionIdx, WorldVariable* pVar);
void mapState_SetPublicVar(int iPartitionIdx, WorldVariable* pVar, bool bAddIfMissing);

// Prototype values are added for each player on the map.  Used for scoreboards
bool mapState_AddPrototypePlayerValue(int iPartitionIdx, const char *pcValueName, int iStartingValue, GameEvent *pEvent);

// Player value management
void mapState_InitPlayerValues(MapState *pState, Entity *pPlayerEnt);
void mapState_AddPlayerValues(Entity *pPlayerEnt);
void mapState_ClearAllCommandsForPlayer(MapState *pState, Entity *pPlayerEnt);
void mapState_ClearAllCommandsForPlayerEx(MapState *pState, PlayerMapValues *pPlayerValues);
void mapState_ClearAllPlayerCommandsTargetingEnt(MapState *pState, Entity *pTargetEnt);

// Team value management
void mapState_InitTeamValues(MapState *pState, int iTeamID);
void mapState_ClearAllCommandsForTeam(MapState *pState, int iTeamID, Entity *pPetOwner);
void mapState_ClearAllTeamCommandsTargetingEnt(MapState *pState, Entity *pTargetEnt);
void mapState_DestroyTeamValues(MapState *pState, int iIndex);

// Team OR player value management
void mapState_UpdatePlayerInfoAttackTarget(MapState * pState, SA_PARAM_NN_VALID Entity *pOwner, Entity *pPetEnt, EntityRef erTarget, PetTargetType eType, bool bAddAsFirstTarget, bool onePerType);
void mapState_ClearAllCommandsForOwner(MapState * pState, Entity *pOwner);

// Set the current scoreboard
void mapState_SetScoreboard(int iPartitionIdx, const char *pcNewScoreboardName);
void mapState_SetScoreboardState(int iPartitionIdx, ScoreboardState eState);
void mapState_SetScoreboardTimer(int iPartitionIdx, U32 uTimestamp, bool bCountdown);
void mapState_SetScoreboardOvertime(int iPartitionIdx, bool bOvertime);
void mapState_SetScoreboardTotalMatchTime(int iPartitionIdx, U32 matchTime);

// Sync Dialogs
bool mapState_AddSyncDialog(Entity* pEnt, ContactDef *pContactDef, ContactDialogOptionData *pData);
void mapState_RemoveSyncDialog(int iPartitionIdx, SyncDialog* pSyncDialog);

// Hidden nodes
void mapState_UpdateNodeEntry(int iPartitionIdx, const char *pcNodeName, bool bHidden, bool bDisabled, EntityRef uShowUntilEnt );
void mapState_ClearNodeEntries(int iPartitionIdx);
void mapState_ClearAllNodeEntries(void);

// Open missions
void mapState_UpdateOpenMissions(int iPartitionIdx, OpenMission **eaOpenMisions);

void mapState_ServerAppendMapStateToPacket(Packet *pPak, bool bFullSend, int iPartitionIdx);
void mapState_ApplyDiffToOldState(int iPartitionIdx, Packet *pPak);
void mapState_ClearDirtyBits(void);

void mapState_SetPowersSpeedRecharge(int iPartitionIdx, F32 speed);

void mapState_UpdateAllPartitions(F32 fTimeStep);
void mapState_UpdateCombatLevelsForAllPartitions(void);

bool mapState_CanSidekick(int iPartitionIdx);
void mapState_SetBolsterLevel(int iPartitionIdx, BolsterType eBolsterType, S32 iBolsterLevel);

void mapState_StopTrackingEvents(int iPartitionIdx);
void mapState_StopTrackingPlayerEvents(MapState *pState);

// Difficulty Scale
void mapState_SetDifficulty(int iPartitionIdx, int iDifficulty);
void mapState_SetDifficultyIfNotInitialized(int iPartitionIdx, int iDifficulty);
bool mapState_IsDifficultyInitialized(int iPartitionIdx);
void mapState_SetDifficultyInitialized(int iPartitionIdx);

void mapState_SetPVPQueuesDisabled(int iPartitionIdx, bool bDisabled);

void mapState_PartitionLoad(int iPartitionIdx);
void mapState_PartitionUnload(int iPartitionIdx);

void mapState_MapLoad(void);
void mapState_MapUnload(void);

// Cutscenes
void mapState_CutscenePlayed(int iPartitionIdx, SA_PARAM_NN_STR const char *pcCutsceneName);
bool mapState_HasCutscenePlayed(int iPartitionIdx, SA_PARAM_NN_STR const char *pcCutsceneName);

// Respawn Count
U32 mapState_GetPlayerSpawnCount(Entity *pEnt);
U32 mapState_GetPlayerLastRespawnTime(Entity *pEnt);
void mapState_SetPlayerSpawnCount(Entity *pEnt, U32 uiSpawnCount);
