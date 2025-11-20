#ifndef POWER_TREE_HELPER_H
#define POWER_TREE_HELPER_H

GCC_SYSTEM

typedef struct NOCONST(Character) NOCONST(Character);
typedef struct NOCONST(PTNode) NOCONST(PTNode);
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct NOCONST(PowerTree) NOCONST(PowerTree);
typedef struct NOCONST(Power) NOCONST(Power);
typedef struct NOCONST(PTNodeEnhancementTracker) NOCONST(PTNodeEnhancementTracker);
typedef struct NOCONST(GameAccountData) NOCONST(GameAccountData);
typedef struct PowerTreeDef PowerTreeDef;
typedef struct PTPurchaseRequirements PTPurchaseRequirements;
typedef struct PTGroupDef PTGroupDef;
typedef struct PTNodeDef PTNodeDef;
typedef struct PTNodeEnhancementDef PTNodeEnhancementDef;
typedef struct PowerTreeStep PowerTreeStep;
typedef struct PowerTreeSteps PowerTreeSteps;
typedef struct Entity Entity;
typedef struct ItemChangeReason ItemChangeReason;
typedef enum PTRespecGroupType PTRespecGroupType;

extern ParseTable parse_Critter[];
#define TYPE_parse_Critter Critter

#define POWER_TREE_VERSION_NUMBITS 24
#define POWER_TREE_FULL_RESPEC_VERSION_NUMBITS 8

AUTO_STRUCT;
typedef struct TrainerUnlockCBData
{
	U32 uiTrainerType;
	U32 uiTrainerID;
	REF_TO(PTNodeDef) hNodeDef;
} TrainerUnlockCBData;

AUTO_STRUCT;
typedef struct PowerTreeValidateResults
{
	char* estrFailure; AST(ESTRING)
		// The reason for failure
	PowerTreeStep** ppFailedSteps; 
		// Failed steps, if requested
} PowerTreeValidateResults;

// Returns the PowerTree owned by the Entity with the given def, if it exists
NOCONST(PowerTree) *entity_FindPowerTreeHelper(ATH_ARG NOCONST(Entity) *pEnt, PowerTreeDef *pTreeDef);

// Returns the PTNode in the PowerTree with the given def, if it exists.  Matches references
NOCONST(PTNode) *powertree_FindNodeHelper(ATH_ARG NOCONST(PowerTree) *pTree, PTNodeDef *pNodeDef);

// Returns the PTNode owned by the Entity with the given def, if it exists
NOCONST(PTNode) *entity_FindPowerTreeNodeHelper(ATH_ARG NOCONST(Entity) *pEnt, PTNodeDef *pNodeDef);



// Adds the PowerTree from the given PowerTreeDef
//  Only validates that the Entity doesn't already have the tree
S32 entity_PowerTreeAddHelper(ATH_ARG NOCONST(Entity)* pEnt, PowerTreeDef *pTreeDef);

// Adds the Steps specified, in order, to pEnt.
// Does not perform validation, beyond some bare-minimum sanity checks
// Does not do "convenience" purchasing - so it will fail if you ask it to buy a Node
//  and the Entity doesn't have that Node's Tree yet.
S32 entity_PowerTreeStepsAddHelper(ATR_ARGS, ATH_ARG NOCONST(Entity)* pPayer, ATH_ARG NOCONST(Entity)* pEnt, PowerTreeSteps *pSteps, const ItemChangeReason *pReason);

// Removes the Steps specified, in order, from pEnt.
// Does not perform validation, beyond some bare-minimum sanity checks
// Charges the Entity as specified on each Step
S32 entity_PowerTreeStepsRespecHelper(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(GameAccountData)* pData, PowerTreeSteps *pSteps, const ItemChangeReason *pReason, int eRespecType);


// Functions for building PowerTreeSteps

// Runs the entity_PowerTreeAutoBuyHelper, returns whether anything was purchased.
//  If pSteps is not NULL it is filled in by the helper.
S32 entity_PowerTreeAutoBuySteps(int iPartitionIdx, Entity *pEnt, Entity *pPayer, PowerTreeSteps *pSteps);

// Runs the entity_PowerTreeNodeEnhanceHelper, returns whether it was successful.
//  If pSteps is not NULL it is filled in by the helper.
S32 entity_PowerTreeNodeEnhanceSteps(int iPartitionIdx,
									 Entity *pEnt,
									 const char *pchTree,
									 const char *pchNode,
									 const char *pchEnhancement,
									 PowerTreeSteps *pSteps);

// Runs the entity_PowerTreeNodeIncreaseRankHelper, returns whether it was successful.
//  If pSteps is not NULL it is filled in by the helper.
S32 entity_PowerTreeNodeIncreaseRankSteps(int iPartitionIdx,
										  Entity *pEnt,
										  const char *pchTree,
										  const char *pchNode,
										  PowerTreeSteps *pSteps);

// Checks if the Entity can buy the specified steps.  If pPayer is specified it is passed along.
//  If pStepsResult is not NULL it is filled in with the actual steps that need to be bought.
// The NoClone option is for when this is called from another function which is already
//  operating on fake Entities, which means this function doesn't clone the provided Entities
//  AND the side-effects of any changes hang around on the provided Entities.
S32 entity_PowerTreeStepsBuySteps(int iPartitionIdx,
								  Entity *pEnt,
								  Entity *pPayer,
								  PowerTreeSteps *pStepsRequested,
								  PowerTreeSteps *pStepsResult,
								  S32 bNoClone);

// Checks if the Entity can respec the specified Steps, followed by optional purchases of the specified Steps.
//  If the results Steps are not NULL they are filled in with the actual Steps that need to be respec'd and added.
// This uses pPayer ONLY for validation, because it's always been that way, and changing it could have unintended
//  side-effects
// The NoClone option is for when this is called from another function which is already
//  operating on fake Entities, which means this function doesn't clone the provided Entities
//  AND the side-effects of any changes hang around on the provided Entities.
// if step respec is set to true
S32 entity_PowerTreeStepsRespecSteps(int iPartitionIdx,
									 Entity *pEnt,
									 Entity *pPayer,
									 PowerTreeSteps *pStepsRequestedRespec,
									 PowerTreeSteps *pStepsRequestedBuy,
									 PowerTreeSteps *pStepsResultRespec,
									 PowerTreeSteps *pStepsResultBuy,
									 S32 bNoClone,
									 S32 bStepRespec);

// STO-specific function to check if the Entity can replace a Node with another Node to be escrow'd.
//  If the results Steps are not NULL they are filled in with the actual Steps that need to be respec'd and added.
S32 entity_PowerTreeNodeReplaceEscrowSteps(int iPartitionIdx,
										   Entity *pEnt,
										   Entity *pPayer,
										   const char *pchOldTree,
										   const char *pchOldNode,
										   const char *pchNewTree,
										   const char *pchNewNode,
										   PowerTreeSteps *pStepsResultRespec,
										   PowerTreeSteps *pStepsResultBuy);



// Fills the steps structure with the steps for the Character's PowerTrees, in order of newest to oldest
void character_GetPowerTreeSteps(ATH_ARG NOCONST(Character) *pchar, PowerTreeSteps *pSteps, bool bDoingStepRetCon, PTRespecGroupType eRespecGroupType);

// Fills the steps structure with the advantage steps for the Character's PowerTrees, in order of newest to oldest
void character_GetPowerTreeAdvantages(ATH_ARG NOCONST(Character) *pchar, PowerTreeSteps *pSteps);

// Fills in the Character's ppPointsSpent with points spent by the Character's PowerTrees
void character_UpdatePointsSpentPowerTrees(ATH_ARG NOCONST(Character) *pchar, S32 bOnlyLowerPoints);

// Locks all the Character's power trees and nodes as Permanent.
void character_LockAllPowerTrees(ATH_ARG NOCONST(Character) *pChar);

// Calculates the iCostRespec for each step in the steps structure.  Costs all the steps if iStepsToCost is 0.
void character_PowerTreeStepsCostRespec(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID PowerTreeSteps *pSteps, S32 iStepsToCost);

int character_RespecPointsSpentRequirement(int iPartitionIdx, Character *pChar);

// Simple helper to calculate the total cost of a respec
S32 GetPowerTreeSteps_TotalCost(PowerTreeSteps *pSteps);

//Used by power trees validate and player types
void entity_PowerTreeResetSpentNumericsOnFakeEnt( ATH_ARG NOCONST(Entity) *pEnt, NOCONST(Entity) *pFakeEnt );

// Returns true if the Entity's PowerTrees are valid in the state defined by pSteps
//  ppchFailure can be provided as an estring for detailed explanation in the case of failure.
S32 entity_PowerTreesValidateSteps(int iPartitionIdx, SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity *pPayer, SA_PARAM_NN_VALID NOCONST(Entity) *pentFake, SA_PARAM_NN_VALID PowerTreeSteps *pSteps, SA_PARAM_OP_VALID PowerTreeValidateResults *pResults, bool bGetAllFailedSteps);

// Returns true if the Entity's PowerTrees are valid in the current state.
//  ppchFailure can be provided as an estring for detailed explanation in the case of failure.
S32 entity_PowerTreesValidate(int iPartitionIdx, SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity *pPayer, SA_PARAM_OP_VALID PowerTreeValidateResults *pResults);

// Adds or increases the rank of a PowerTree Node
//  If the Node was in escrow, this unlocks it and adds the 0-rank Power
NOCONST(PTNode)* entity_PowerTreeNodeAddHelper(ATR_ARGS, ATH_ARG NOCONST(Entity) *pPayer, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(PowerTree) *pTree, SA_PARAM_NN_VALID PTNodeDef *pNodeDef, const ItemChangeReason *pReason);

// Increases the rank of a PowerTree Node
// This DOES modify the Entity, but it is NOT an ATH.
NOCONST(PTNode) *entity_PowerTreeNodeIncreaseRankHelper(int iPartitionIdx, NOCONST(Entity) *pEnt, NOCONST(Entity) *pPayer, const char *pchTree, const char *pchNode, S32 bSlave, S32 bIsTraining, S32 bSkipAutoBuy, PowerTreeSteps *pSteps);

// Decreases the rank of a PowerTree Node
// This DOES modify the Entity, but it is NOT an ATH.
S32 entity_PowerTreeNodeDecreaseRankHelper(NOCONST(Entity) *pEnt, const char *pchTree, const char *pchNode, bool bAlwaysRefundPoints, S32 bEscrow, PowerTreeSteps *pSteps);



// Like entity_PowerTreeNodeIncreaseRankHelper(), except only works on Nodes you don't already own,
//  and if you CAN'T actually buy the Node, it puts it into escrow instead.
// This DOES modify the Entity, but it is NOT an ATH.
NOCONST(PTNode) *entity_PowerTreeNodeEscrowHelper(int iPartitionIdx, NOCONST(Entity) *pEnt, NOCONST(Entity) *pPayer, const char* pchTree, const char* pchNode, PowerTreeSteps *pSteps);

bool entity_CanReplacePowerTreeNodeInEscrow(int iPartitionIdx,
											NOCONST(Entity) *pBuyer, 
											NOCONST(Entity) *pEnt, 
											const char* pchOldTree, const char* pchOldNode, 
											const char* pchNewTree, const char* pchNewNode,
											bool bCheckPropagation);

// Returns the cost of a particular rank of a node for the Entity.  Returns -1 on an error.
int entity_PowerTreeNodeRankCostHelper(ATH_ARG SA_PARAM_OP_VALID NOCONST(Character)* pChar,
									   SA_PARAM_NN_VALID PTNodeDef *pNodeDef,
									   int iRank);

// Returns the sum cost of ranks [0..iRank] of a node for the Entity.
int entity_PowerTreeNodeRanksCostHelper(ATH_ARG SA_PARAM_OP_VALID NOCONST(Character)* pChar,
										PTNodeDef *pNodeDef,
										int iRank);

// Attempts to add (or remove) a level from the Entity's Character's Tree's Node's Enhancement, 
//  returns if the change was successful. If pSteps is not NULL it is filled in.
// This DOES modify the Entity, but it is NOT an ATH.
S32 entity_PowerTreeNodeEnhanceHelper(int iPartitionIdx,
									  SA_PARAM_NN_VALID NOCONST(Entity)* pEnt,
									  SA_PARAM_NN_STR const char *pchTree,
									  SA_PARAM_NN_STR const char *pchNode,
									  SA_PARAM_NN_STR const char *pchEnhancement,
									  int bAdd,
									  PowerTreeSteps *pSteps);


int entity_PointsSpentInTreeUnderLevel(ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(PowerTree) *pTree, const char *pchPoints, int iLevelCap);
int entity_PointsSpentInTree(ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(PowerTree) *pTree, const char *pchPoints);
int entity_PointsEarned(ATH_ARG NOCONST(Entity) *pEnt, const char *pchPoints);
int entity_PointsSpent(ATH_ARG NOCONST(Entity) *pEnt, const char *pchPoints);
int entity_PointsSpentTryNumeric(ATH_ARG NOCONST(Entity) *pBuyer, ATH_ARG NOCONST(Entity) *pEnt, const char *pchPoints);
int entity_GetMaxSpendablePointsInTree(Entity* pEnt, PowerTreeDef* pTreeDef, const char* pchPoints);
int entity_PointsRemaining(ATH_ARG NOCONST(Entity) *pBuyer, ATH_ARG NOCONST(Entity) *pEnt, PowerTreeDef *pTreeDef, const char *pchPoints);

// Auto buys any free PowerTree Nodes for the Entity.
// This DOES modify the Entity, but it is NOT an ATH.
int entity_PowerTreeAutoBuyHelperEx(int iPartitionIdx,NOCONST(Entity)* pEnt, NOCONST(Entity)* pPayer, PowerTreeSteps *pSteps, PTNodeDef **ppFailNodeDef, int *piFailRank, NOCONST(Entity) **ppFakeEnt);
#define entity_PowerTreeAutoBuyHelper(iPartitionIdx, pEnt, pPayer, pSteps) entity_PowerTreeAutoBuyHelperEx(iPartitionIdx, pEnt, pPayer, pSteps, NULL, NULL, NULL)

// Attempts to add or remove a PowerTree from the Entity.
// This DOES modify the Entity, but it is NOT an ATH.
int entity_PowerTreeModifyHelperEx(int iPartitionIdx, NOCONST(Entity) *pEnt, NOCONST(Entity) *pPayer, const char *pchTree, int bAdd, PowerTreeSteps *pSteps, NOCONST(Entity) **ppFakeEnt);
#define entity_PowerTreeModifyHelper(iPartitionIdx, pEnt, pPayer, pchTree, bAdd, pSteps) entity_PowerTreeModifyHelperEx(iPartitionIdx, pEnt, pPayer, pchTree, bAdd, pSteps, NULL)

// Fixes the uiTimeCreated field on the PowerTree with a best guess, if it's not set
void powertree_FixTimeCreatedHelper(ATH_ARG SA_PARAM_NN_VALID NOCONST(PowerTree) *pTree);

// Fixes up the Enhancements and Enhancement tracker arrays of every PTNode owned by the Character
void character_FixPTNodeEnhancementsHelper(ATH_ARG NOCONST(Character) *pchar);

S32 EntityPTPurchaseReqsHelper(ATR_ARGS, ATH_ARG NOCONST(Entity) *pBuyer, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(PowerTree) *pTree, PTPurchaseRequirements *pRequirements);
S32 entity_CanBuyPowerTreeNodeIgnorePointsRankHelper(ATR_ARGS, ATH_ARG NOCONST(Entity) *pBuyer, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(PowerTree) *pTree, PTGroupDef *pGroup, PTNodeDef *pNodeDef, int iRank);
bool entity_CanBuyPowerTreeNodeHelper(ATR_ARGS, int iPartitionIdx, ATH_ARG NOCONST(Entity) *pBuyer, ATH_ARG SA_PARAM_NN_VALID NOCONST(Entity) *pEnt, PTGroupDef *pGroup, PTNodeDef *pNodeDef, int iRank, S32 bRequireNextRank, S32 bCheckGroupMax, S32 bSlave, S32 bIsTraining);

bool entity_CanBuyPowerTreeEnhHelper(int iPartitionIdx, NOCONST(Entity) *pEnt, PTNodeDef *pNodeDef, PTNodeEnhancementDef *pEnh);

// Returns true if the Entity can buy the PowerTreeDef.  Must set bTemporary to true when testing for
//  Temporary trees, or false when testing for permanent purchases.
S32 entity_CanBuyPowerTreeHelper(int iPartitionIdx, ATH_ARG NOCONST(Entity)* pEnt, SA_PARAM_NN_VALID PowerTreeDef *pTree, S32 bTemporary);

bool entity_CanBuyPowerTreeGroupHelper(ATR_ARGS, int iPartitionIdx, ATH_ARG NOCONST(Entity) *pBuyer, ATH_ARG NOCONST(Entity) *pEnt, PTGroupDef *pGroup);

S32 entity_FindTrainingLevelHelper(ATH_ARG NOCONST(Entity)* pBuyer, ATH_ARG NOCONST(Entity)* pEnt);
S32 entity_Find_TableTrainingLevel(NOCONST(Entity)* pBuyer, NOCONST(Entity)* pEnt, const char *pchTableName);

int entity_PowerTreeNodeEnhPointsSpentHelper(ATH_ARG NOCONST(Entity) *pEnt, PTNodeDef *pNodeDef);

// Returns the EnhancementTracker in the PTNode with the given def, if it exists
NOCONST(PTNodeEnhancementTracker) *powertreenode_FindEnhancementTrackerHelper(ATH_ARG NOCONST(PTNode) *pNode, PowerDef *pEnhDef);

// Returns the rank of the Enhancement in the PTNode with the given def, if it exists, otherwise returns 0
int powertreenode_FindEnhancementRankHelper(ATH_ARG NOCONST(PTNode) *pNode, PowerDef *pEnhDef);

// Refreshes Powers from Temporary PowerTrees for the Character.  Will destroy all old Powers and
//  make appropriate new ones from scratch.  Returns true if anything was added or removed.  Does
//  not automatically call character_ResetPowersArray().
S32 character_UpdateTemporaryPowerTrees(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar);

S32 entity_PowerTreeRemoveHelper(ATH_ARG NOCONST(Entity)* pEnt, PowerTreeDef *pTreeDef);
// Updates the current PowerTree version number whenever a PowerTree changes on the character
void character_UpdatePowerTreeVersion(ATH_ARG NOCONST(Entity) *pEnt);
// Updates the current PowerTree "full respec" version whenever a game-wide PowerTree reset occurs
void character_UpdateFullRespecVersion(ATH_ARG NOCONST(Entity) *pEnt);

// Return the correct numeric item for this respec by type
const char *PowerTree_GetRespecTokensPurchaseItem(PTRespecGroupType eRespecType);

S32 PowerTree_GetNumericRespecCost(ATH_ARG NOCONST(Entity) *pEnt, PTRespecGroupType eRespecType);

#endif
