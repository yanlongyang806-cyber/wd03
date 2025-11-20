/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef POWER_TREE_TRANSACTIONS_H__
#define POWER_TREE_TRANSACTIONS_H__

// Forward Declarations
typedef struct CharacterTraining CharacterTraining;
typedef struct Entity Entity;
typedef struct ItemChangeReason ItemChangeReason;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct NOCONST(GameAccountData) NOCONST(GameAccountData);
typedef struct PowerTreeSteps PowerTreeSteps;
typedef struct RespecCBData RespecCBData;
typedef struct TransactionReturnVal TransactionReturnVal;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef enum enumTransactionOutcome enumTransactionOutcome;
typedef void (*UserDataCallback)(void *userdata);
typedef enum PTRespecGroupType PTRespecGroupType;

// Actual transaction, exposed for other transactions to call directly

// Entry point for all PowerTree respecs
// NO other entry points for removing Trees, Nodes or Enhancements are legal
// Wraps entity_PowerTreeStepsRespecHelper and entity_PowerTreeStepsAddHelper,
//  which do not perform validation, so all validation must be performed before
//  calling this transaction.
enumTransactionOutcome trEntity_PowerTreeStepsRespec(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(GameAccountData)* pData, PowerTreeSteps *pStepsRespec, PowerTreeSteps *pStepsBuy, const ItemChangeReason *pReason, int eRespecType);



// Adds all appropriate PowerTree AutoBuy Trees and Nodes to the Entity
void entity_PowerTreeAutoBuyEx(int iPartitionIdx, Entity *pEnt, Entity *pPayer, bool bFullRespec);
#define entity_PowerTreeAutoBuy(iPartitionIdx, pEnt, pPayer) entity_PowerTreeAutoBuyEx(iPartitionIdx, pEnt, pPayer, false)

// Adds one rank of the Enhancement to the Entity's PowerTree Node
void entity_PowerTreeNodeEnhance(int iPartitionIdx, Entity *pEnt, const char *pchTree, const char *pchNode, const char *pchEnhancement);

// Adds one rank to the Entity's PowerTree Node
void entity_PowerTreeNodeInceaseRank(int iPartitionIdx, Entity *pEnt, const char *pchTree, const char *pchNode);

// Adds the PowerTreeSteps to the Entity, optionally passes along the payer and return val
void entity_PowerTreeStepsBuy(int iPartitionIdx,
							  Entity *pEnt,
							  Entity *pPayer,
							  PowerTreeSteps *pStepsRequested,
							  TransactionReturnVal *pReturnVal);

// data used for callbacks to Respec_CB
RespecCBData* Respec_CreateCBData(Entity *pEnt, bool bIsForcedRespec);

// Respec callback
void Respec_CB(TransactionReturnVal *pReturn, void *pData);

// Respecs the PowerTreeSteps from the Entity, optionally passes along the post-respec buys and return val
void entity_PowerTreeStepsRespec(int iPartitionIdx,
								 Entity *pEnt,
								 PowerTreeSteps *pStepsRequestedRespec,
								 PowerTreeSteps *pStepsRequestedBuy,
								 TransactionReturnVal *pReturnVal,
								 PTRespecGroupType eRespecType,
								 S32 bStepRespec);




// Performs a respec/buy as necessary to the the ranks as requested, regardless of restrictions
void entity_PowerTreeAddWithoutRules(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_STR const char *pchTree, int iRank, int iRankEnh);


// Wrapper for trCharacter_StartTraining(ForEnt). Assumes arguments are valid.
void character_StartTraining(int iPartitionIdx, Entity* pOwner, Entity* pEnt, 
							 const char* pchOldNode, const char* pchNewNode, S32 iNewNodeRank,
							 const char* pchTrainingNumeric, S32 iTrainingCost,
							 F32 fRefundPercent, U32 uiStartTime, U32 uiEndTime, U64 uiItemID, bool bRemoveItem, bool bFromStore,
							 S32 eType, UserDataCallback pCallback, void* pCallbackData);

// Wrapper for trCharacter_CompleteTraining. Calls trCharacter_CancelTraining if "complete" transaction fails.
void character_CompleteTraining(int iPartitionIdx, Entity* pOwner, Entity* pEnt, CharacterTraining* pTraining, GameAccountDataExtract *pExtract);

// Wrapper for trCharacter_CancelTraining.
void character_CancelTraining(Entity* pOwner, Entity* pEnt, CharacterTraining* pTraining);


#endif
