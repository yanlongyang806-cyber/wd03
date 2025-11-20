/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GAMEACTION_H
#define GAMEACTION_H

typedef struct WorldGameActionProperties WorldGameActionProperties;
typedef struct WorldGameActionBlock WorldGameActionBlock;
typedef struct WorldVariable WorldVariable;
typedef struct WorldVariableArray WorldVariableArray;
typedef struct MissionDef MissionDef;
typedef struct Entity Entity;
typedef struct GameActionDoorDestinationVarArray GameActionDoorDestinationVarArray;
typedef struct ItemChangeReason ItemChangeReason;
typedef struct NOCONST(Mission) NOCONST(Mission);
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct NOCONST(Guild) NOCONST(Guild);
typedef struct NOCONST(GameAccountData) NOCONST(GameAccountData);
typedef struct NOCONST(ShardVariableContainer) NOCONST(ShardVariableContainer);
typedef struct Mission Mission;
typedef struct MissionInfo MissionInfo;
typedef enum enumTransactionOutcome enumTransactionOutcome;

//----------------------------------------------------------------
// Utility
//----------------------------------------------------------------

GameActionDoorDestinationVarArray* gameaction_GenerateDoorDestinationVariables(int iPartitionIdx, const WorldGameActionProperties** eaActions);

// Use these to see which version of the RunActions transaction to call
bool gameaction_MustLockInventory(CONST_EARRAY_OF(WorldGameActionProperties)* pppActions, MissionDef *pRootDef);
bool gameaction_MustLockMissions(CONST_EARRAY_OF(WorldGameActionProperties)* pppActions, MissionDef *pRootDef);
bool gameaction_MustLockNemesis(CONST_EARRAY_OF(WorldGameActionProperties)* pppActions, MissionDef *pRootDef);
bool gameaction_MustLockNPCEMail(CONST_EARRAY_OF(WorldGameActionProperties)* pppActions, MissionDef *pRootDef);
bool gameaction_MustLockGameAccount(CONST_EARRAY_OF(WorldGameActionProperties)* pppActions, MissionDef *pRootDef);
bool gameaction_MustLockShardVariables(CONST_EARRAY_OF(WorldGameActionProperties)* pppActions, MissionDef *pRootDef);
bool gameaction_MustLockActivityLog(CONST_EARRAY_OF(WorldGameActionProperties)* pppActions, MissionDef *pRootDef);
bool gameaction_MustLockGuildActivityLog(bool inGuild, CONST_EARRAY_OF(WorldGameActionProperties)* pppActions, MissionDef *pRootDef);
bool gameaction_MustLockGuild(CONST_EARRAY_OF(WorldGameActionProperties)* pppActions, MissionDef *pRootDef);

// TRUE if the player is eligible to perform all of these actions
bool gameaction_PlayerIsEligible(Entity *pEnt, const WorldGameActionBlock *pActionBlock);

// TRUE if the gameaction block can provide a Display String that describes itself
bool gameaction_CanProvideDisplayString(const WorldGameActionBlock *pActionBlock);

// StructAllocStrings a Display String for this GameActionBlock
bool gameaction_GetDisplayString(Entity *pEnt, const WorldGameActionBlock *pActionBlock, char **estrBuffer);

void gameaction_GenerateExpression(WorldGameActionProperties *pAction);

//----------------------------------------------------------------
// Functions that run a Transaction to execute all Actions
//----------------------------------------------------------------

// Executes a list of Actions for the player, then runs the callback
typedef void (*GameActionExecuteCB) (enumTransactionOutcome eOutcome, Entity *pEnt, void *pData);
void gameaction_RunActions(Entity *pEnt, const WorldGameActionBlock *pActions, const ItemChangeReason *pReason, GameActionExecuteCB callback, void *pData);


//----------------------------------------------------------------
// Transaction Helpers to execute a list of Actions
//----------------------------------------------------------------

// Each of these functions locks different subsets of the entity.  This is, unfortunately, the only way in which
// transactions currently allow locking

enumTransactionOutcome gameaction_trh_RunActionsLockAll(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, ATH_ARG CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer, ATH_ARG NOCONST(Guild)* pGuild, CONST_EARRAY_OF(WorldGameActionProperties)* actions, WorldVariableArray* pMapVariableArray, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);

enumTransactionOutcome gameaction_trh_RunActionsLockVarsOnly(ATR_ARGS, ATH_ARG CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer, CONST_EARRAY_OF(WorldGameActionProperties)* actions);

enumTransactionOutcome gameaction_trh_RunActionsLockAllEntity(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, WorldVariableArray* pVariables, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);

enumTransactionOutcome gameaction_trh_RunActionsLockAllEntityWithGuild(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, ATH_ARG NOCONST(Guild)* pGuild, CONST_EARRAY_OF(WorldGameActionProperties)* actions, WorldVariableArray* pMapVariableArray, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);

enumTransactionOutcome gameaction_trh_RunActionsNoInventory(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, WorldVariableArray *pMapVariableArray, GameAccountDataExtract *pExtract);

enumTransactionOutcome gameaction_trh_RunActionsNoMissions(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, WorldVariableArray* pMapVariableArray, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);

enumTransactionOutcome gameaction_trh_RunActionsNoNemesis(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, WorldVariableArray* pMapVariableArray, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const  char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);

enumTransactionOutcome gameaction_trh_RunActionsNoInventoryOrMissions(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, GameAccountDataExtract *pExtract);

enumTransactionOutcome gameaction_trh_RunActionsNoInventoryOrNemesis(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, WorldVariableArray *pMapVariableArray, GameAccountDataExtract *pExtract);

enumTransactionOutcome gameaction_trh_RunActionsNoMissionsOrNemesis(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, WorldVariableArray* pMapVariableArray, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);

enumTransactionOutcome gameaction_trh_RunActionsLockNPCEMailOnly(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, GameAccountDataExtract *pExtract);

enumTransactionOutcome gameaction_trh_RunActionsLockActivityLogOnly(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions);

enumTransactionOutcome gameaction_trh_RunActionsLockActivityLogWithGuildOnly(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, ATH_ARG NOCONST(Guild) *pGuild, CONST_EARRAY_OF(WorldGameActionProperties)* actions);

enumTransactionOutcome gameaction_trh_RunActionsLockGuildOnly(ATR_ARGS, ATH_ARG NOCONST(Guild) *pGuild, CONST_EARRAY_OF(WorldGameActionProperties)* actions);


// Not actually a transaction helper, because it locks nothing
enumTransactionOutcome gameaction_trh_RunActionsNoLocking(ATR_ARGS,CONST_EARRAY_OF(WorldGameActionProperties)* actions);

// This treats all GrantSubMission actions as submissions of the given Mission
enumTransactionOutcome gameaction_trh_RunActionsSubMissions(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, ATH_ARG CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer, ATH_ARG NOCONST(Guild)* pGuild, ATH_ARG NOCONST(Mission)* pMission, MissionDef *pDef, CONST_EARRAY_OF(WorldGameActionProperties)* actions, WorldVariableArray* pMapVariableArray, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);

enumTransactionOutcome gameaction_trh_RunActionsSubMissionsNoLocking(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, ATH_ARG NOCONST(Mission)* pMission, MissionDef *pDef, MissionDef *pRootDef, CONST_EARRAY_OF(WorldGameActionProperties)* actions);


//----------------------------------------------------------------
// Functions to execute a list of Actions for non-persisted Missions
//----------------------------------------------------------------

// Runs actions without a transaction; throws errors for actions that require transactions.
void gameaction_np_RunActions(int iPartitionIdx, Mission* pNonPersistedMission, CONST_EARRAY_OF(WorldGameActionProperties)* actions);

// This treats all GrantSubMission actions as submissions of the given Mission
// Runs actions without a transaction; throws errors for actions that require transactions.
void gameaction_np_RunActionsSubMissions(int iPartitionIdx, Mission* pNonPersistedMission, CONST_EARRAY_OF(WorldGameActionProperties)* actions);

// This treats all GrantSubMission actions as submissions of the given Mission
// Only executes GrantSubMission actions, does not throw errors for other actions
void gameaction_np_RunActionsSubMissionsOnly(int iPartitionIdx, Mission* pNonPersistedMission, CONST_EARRAY_OF(WorldGameActionProperties)* actions);

#endif
