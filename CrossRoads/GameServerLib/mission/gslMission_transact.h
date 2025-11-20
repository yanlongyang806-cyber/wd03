/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef MISSION_TRANSACT_H
#define MISSION_TRANSACT_H

#include "mission_enums.h"
#include "objTransactions.h"

typedef U32 EntityRef; // This is an opaque entity identifier, which is implemented elsewhere
typedef struct ContactRewardChoices ContactRewardChoices;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct GameActionDoorDestinationVarArray GameActionDoorDestinationVarArray;
typedef struct ItemChangeReason ItemChangeReason;
typedef struct Mission Mission;
typedef struct MissionDef MissionDef;
typedef struct MissionInfo MissionInfo;
typedef struct MissionOfferParams MissionOfferParams;
typedef struct Entity Entity;
typedef struct UGCProject UGCProject;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct NOCONST(Guild) NOCONST(Guild);
typedef struct NOCONST(Mission) NOCONST(Mission);
typedef struct NOCONST(ShardVariableContainer) NOCONST(ShardVariableContainer);
typedef struct NOCONST(UGCProject) NOCONST(UGCProject);
typedef struct WorldVariableArray WorldVariableArray;
typedef struct RewardGatedDataInOut RewardGatedDataInOut;
typedef enum enumTransactionOutcome enumTransactionOutcome;

// Adds a new mission of the specified name to the player
void missioninfo_AddMission(int iPartitionIdx, MissionInfo* info, MissionDef* missionDef, const MissionOfferParams *pParams, TransactionReturnCallback cb, UserData data);
void missioninfo_AddMissionByName(int iPartitionIdx, MissionInfo* info, const char* missionName, TransactionReturnCallback cb, UserData data);

// Clear the missionToGrant field on the entity (used so the login server can "grant" a mission) 
void missioninfo_ClearMissionToGrant(Entity* pEnt);

// change the cooldown for a mission that is in a cooldown persiod
void missioninfo_ChangeCooldown(Entity *pEnt, const char *pcMissionName, bool bAdjustTime, U32 uTm, bool bAdjustCount, U32 uCount);

// Runs the transaction to turn in/complete a Mission
void mission_TurnInMissionInternal(MissionInfo* info, Mission* mission, SA_PARAM_OP_VALID ContactRewardChoices* rewardChoices);

// Drop the Mission
void missioninfo_DropMission(Entity *pEnt, MissionInfo* info, Mission* mission);
enumTransactionOutcome mission_tr_UpdateCooldownAndDropMission(ATR_ARGS, NOCONST(Entity)* ent, const char* missionName, U32 startTime, const ItemChangeReason *pReason, /*NOCONST(UGCProject)* pUGCProject, */GameAccountDataExtract *pExtract);
enumTransactionOutcome mission_tr_DropMission(ATR_ARGS, NOCONST(Entity)* ent, const char* missionName, const ItemChangeReason *pReason, /*NOCONST(UGCProject)* pUGCProject, */GameAccountDataExtract *pExtract);

// Resets a player's mission info as if the player were just created
void missioninfo_ResetMissionInfo(Entity* playerEnt);

// Transition a mission to a new state.  Upon completion of the transition, the actions will be triggered
void mission_tr_CompleteMission(int iPartitionIdx, Mission* mission, bool bForcePermanentComplete);
void mission_tr_FailMission(int iPartitionIdx, Mission* mission);
void mission_tr_UncompleteMission(int iPartitionIdx, Mission* mission);

// Callbacks that occur when a Mission/MissionInfo is created or destroyed from a Transaction
// These are called in gslTransactions.c
void mission_tr_MissionPostCreateCB(Entity *pEnt, Mission *mission, Mission *parentMission);
void mission_tr_MissionPreDestroyCB(Entity *pEnt, Mission *mission, Mission *parentMission);
void mission_tr_MissionInfoPostCreateCB(Entity *pEnt, MissionInfo *info);
void mission_tr_MissionInfoPreDestroyCB(Entity *pEnt, MissionInfo *info);

// Transaction Helpers to add Missions
enumTransactionOutcome mission_trh_AddChildMission(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets, ATH_ARG CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer, ATH_ARG NOCONST(Guild)* pGuild, ATH_ARG NOCONST(Mission)* parentMission, MissionDef *pDef, WorldVariableArray* pMapVariableArray, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);
enumTransactionOutcome mission_trh_AddChildMissionNoLocking(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, ATH_ARG NOCONST(Mission)* parentMission, MissionDef *pDef, MissionDef *pRootDef);
enumTransactionOutcome mission_trh_AddMission(ATR_ARGS, NOCONST(Entity)* ent, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets, ATH_ARG CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer, ATH_ARG NOCONST(Guild)* pGuild, const char* missionName, int iMissionLevel, const MissionOfferParams *pParams, WorldVariableArray* pMapVariables, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);
enumTransactionOutcome mission_trh_AddMissionNoLocking(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, const char* missionName, int iMissionLevel, const MissionOfferParams *pParams, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);
NOCONST(Mission)* mission_trh_GetOrCreateMission(ATR_ARGS, NOCONST(Entity)* ent, const char *pchRootMissionName, const char *pchMissionName, int iMissionLevel, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);

// Utilities to see what data must be locked by a mission transaction
bool missiondef_HasOnStartRewards(MissionDef *pDef);
bool missiondef_HasSuccessRewards(MissionDef *pDef);
bool missiondef_HasFailureRewards(MissionDef *pDef);
bool missiondef_MustLockInventoryForStateChange(MissionDef *pDef, MissionDef *pRootDef, MissionState state);
bool missiondef_MustLockInventoryOnStart(MissionDef *pDef, MissionDef *pRootDef);
bool missiondef_MustLockMissionsForStateChange(MissionDef *pDef, MissionDef *pRootDef, MissionState state);
bool missiondef_MustLockMissionsOnStart(MissionDef *pDef, MissionDef *pRootDef);
bool missiondef_MustLockNemesisForStateChange(MissionDef *pDef, MissionDef *pRootDef, MissionState state);
bool missiondef_MustLockNemesisOnStart(MissionDef *pDef, MissionDef *pRootDef);
bool missiondef_MustLockNPCEMailOnStart(MissionDef *pDef, MissionDef *pRootDef);
bool missiondef_MustLockNPCEMailForStateChange(MissionDef *pDef, MissionDef *pRootDef, MissionState state);
bool missiondef_MustLockGameAccountOnStart(MissionDef *pDef, MissionDef *pRootDef);
bool missiondef_MustLockGameAccountForStateChange(MissionDef *pDef, MissionDef *pRootDef, MissionState state);
bool missiondef_MustLockShardVariablesOnStart(MissionDef *pDef, MissionDef *pRootDef);
bool missiondef_MustLockShardVariablesForStateChange(MissionDef *pDef, MissionDef *pRootDef, MissionState state);
bool missiondef_MustLockActivityLogOnStart(MissionDef *pDef, MissionDef *pRootDef);
bool missiondef_MustLockActivityLogForStateChange(MissionDef *pDef, MissionDef *pRootDef, MissionState state);
bool missiondef_MustLockGuildActivityLogOnStart(bool inGuild, MissionDef *pDef, MissionDef *pRootDef);
bool missiondef_MustLockGuildActivityLogForStateChange(bool inGuild, MissionDef *pDef, MissionDef *pRootDef, MissionState state);
bool missiondef_MustLockGuildOnStart(MissionDef *pDef, MissionDef *pRootDef);

// ------------------------------------------------------------------------------------
// Special transactions for Perks
// ------------------------------------------------------------------------------------

// Transaction to add a Mission if it doesn't exist, and update the Mission's Event Log
void mission_tr_PersistMissionAndUpdateEventCount(Entity *pEnt, Mission *pMission, const char *pchEventName, int iEventCount, bool bSet);

// Transaction to flag a Perk as "discovered" and persist it if necessary
void mission_tr_DiscoverPerk(Mission *pMission);

// ------------------------------------------------------------------------------------
// Transactions for Mission Requests
// ------------------------------------------------------------------------------------

void missionrequest_SetRequestedMission(Entity* pEnt, U32 uRequestID, MissionDef *pNewDef);
void missionrequest_ForceComplete(Entity* pEnt, U32 uRequestID);

// ------------------------------------------------------------------------------------
// Misc utilities
// ------------------------------------------------------------------------------------

// Cleans up any "Recently Completed Secondary Missions" that have expired
void mission_trh_CleanupRecentSecondaryList(ATH_ARG NOCONST(Entity)* pEnt);
void mission_trh_FixupPerkTitles(ATH_ARG NOCONST(Entity)* pEnt, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);

S32 mission_trh_GetEventCount(ATH_ARG NOCONST(Mission)* pMission, const char* pchEventName);

// Get the seed to use for generating rewards for a specific mission
U32 mission_GetRewardSeed(Entity* pEnt, Mission* pMission, MissionDef* pDef);

U32 mission_GetRewardSeedEx(U32 uEntID, MissionDef* pDef, U32 uMissionStartTime, U32 uTimesCompleted);

// Used to create rewardgateddata for the character. 
RewardGatedDataInOut *mission_trh_CreateRewardGatedData(ATH_ARG NOCONST(Entity)* pEnt);

// Helper to fail the mission add
void missioninfo_AddMission_Fail(TransactionReturnCallback cb, UserData data);

#endif
