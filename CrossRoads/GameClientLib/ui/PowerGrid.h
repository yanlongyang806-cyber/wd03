#pragma once
GCC_SYSTEM
#ifndef GCL_UI_POWER_GRID_H
#define GCL_UI_POWER_GRID_H

#include "gclUIGen.h"
typedef struct PowerTreeDef PowerTreeDef;
typedef struct PowerTreeDefRef PowerTreeDefRef;
typedef struct PTTypeDef PTTypeDef;
typedef struct PTGroupDef PTGroupDef;
typedef struct PTNodeDef PTNodeDef;
typedef struct PowerDef PowerDef;
typedef struct PowerTree PowerTree;
typedef struct PTNode PTNode ;
typedef struct Power Power;
typedef struct AutoDescPower AutoDescPower;
typedef struct AutoDescAttribMod AutoDescAttribMod;
typedef struct CharacterTraining CharacterTraining;
typedef struct MicroTransactionDef MicroTransactionDef;
typedef struct GameAccountDataExtract GameAccountDataExtract;

AUTO_ENUM;
typedef enum PowerTreeFilterType
{
	PowerTreeFilterType_All,
	PowerTreeFilterType_Available,
	PowerTreeFilterType_Trained,
	PowerTreeFilterType_Count,
} PowerTreeFilterType;

AUTO_STRUCT;
typedef struct PowerListNode
{
	REF_TO(PowerTreeDef) hTreeDef; AST(NAME(TreeDef) REFDICT(PowerTreeDef))
	REF_TO(PTGroupDef) hGroupDef; AST(NAME(GroupDef) REFDICT(PowerTreeGroupDef))
	REF_TO(PTNodeDef) hNodeDef; AST(NAME(NodeDef) REFDICT(PowerTreeNodeDef))
	REF_TO(PowerDef) hPowerDef; AST(NAME(PowerDef))

	PowerTree *pTree; AST(UNOWNED)
	PTNode *pNode; AST(UNOWNED)
	Power *pPower; AST(UNOWNED)
	Entity *pEnt; AST(UNOWNED)

	const char *pchPowerIcon; AST(POOL_STRING)
	const char *pchButtonIcon; AST(POOL_STRING)
	const char *pchShiftButtonIcon; AST(POOL_STRING)

	// The rank this node represents
	S32 iRank;
	S32 iMaxRank;

	// Minimum level to buy this node.
	S32 iLevel;

	// Training data
	CharacterTraining* pTrainingInfo; AST(UNOWNED)

	// If this node requires a certain microtransaction, then this ref is set to it.
	MicroTransactionDef *pRequiredMicroTransaction;	AST( NAME(RequiredMicroTransaction) UNOWNED)

	// If true, this is just a basic header and not an individual power.
	U32 bIsHeader : 1;

	// If true, this is just a group header and not an individual power.
	U32 bIsGroup : 1;

	// If true, this is just a tree header and not an individual power.
	U32 bIsTree : 1;

	// If true, this is a child of the tree the user is currently viewing,
	// and not the tree per se.
	U32 bIsChildTree : 1;

	// If true, this is still waiting for some data to fill in.
	U32 bIsLoading : 1;

	// If true, the list of advantages will be displayed in the cart.  
	U32 bShowEnhancements : 1;			AST(DEFAULT(1))

	// If true, the power is owned. For UI display purposes only. 
	U32 bIsOwned : 1;

	// If true, the next rank can be bought. For UI display purposes only. 
	U32 bCanIncrement : 1;

	// If true, the power is available. For UI display purposes only. 
	U32 bIsAvailable : 1;

	// If true, the power is available for the fake entity. For UI display purposes only.
	U32 bIsAvailableForFakeEnt : 1;

	// If true, this node is currently being trained
	U32 bIsTraining : 1;

	//If true, this is supposed to have nothing in it
	U32 bIsEmpty : 1;

	// True if the product cannot be bought because the player is already entitled to it.
	// For example, things that subscribers have access to that non-subscribers have to buy.
	U32 bAlreadyEntitled : 1;

	// True if the product is free for premium players.
	U32 bPremiumEntitlement : 1;

	// The number of powers within the same level. 
	// Only filled in the PowersUI_GetPaddedPowerListByPurposeAndLevelSorting function
	S32 iNumPowersInSameLevel;
} PowerListNode;

extern ParseTable parse_PowerListNode[];
#define TYPE_parse_PowerListNode PowerListNode

AUTO_STRUCT;
typedef struct PowerPurposeListNode
{
	// The name of the purpose
	const char* pchPurposeName;		AST(NAME(PurposeName) KEY)

	// The list of powers corresponding to this category
	PowerListNode **eaPowerList;

	// This is a hack to keep track of the sizes of each PowerList
	// to avoid unnecessary allocations
	S32 iListSize;
} PowerPurposeListNode;

AUTO_STRUCT;
typedef struct PowerCartListNode
{
	// Flags this row as an enhancement rather than a Power
	bool bIsEnhancement : 1;

	// Flags this row as a rank
	bool bIsRank : 1;

	// The power to display if this node represents a power
	PowerListNode *pPowerListNode;

	// If this node is an enhancement, display this. 
	PTNodeUpgrade *pUpgrade;

} PowerCartListNode;

AUTO_STRUCT;
typedef struct PowerTreeUIData
{	
	PowerTreeDefRef **eaPTRefs;
} PowerTreeUIData;

AUTO_STRUCT;
typedef struct PowerTreeHolder
{
	PowerTreeDefRef *pTreeDefRef;
	const char *pchTreeName; AST(POOL_STRING)
	bool bShowTree;
} PowerTreeHolder;

AUTO_STRUCT;
typedef struct PTUICategoryListNode
{
	//PowerTreeUICategory eCategoryEnum;
	const char* pchUICategoryName;		AST(NAME(Name) POOL_STRING)
	bool bShowCategory;
	PowerTreeHolder **eaTreeHolder;		AST(UNOWNED)

	// This is a hack to keep track of the sizes of each PowerList
	// to avoid unnecessary allocations
	S32 iListSize;
} PTUICategoryListNode;

AUTO_STRUCT;
typedef struct TrainerOption
{	
	char* pchOption;				AST(ESTRING)
} TrainerOption;

const char* exprEntGetXBoxButtonForPowerID(SA_PARAM_OP_VALID Entity *pEntity, int iID);
const char* exprEntGetXBoxShiftButtonForPowerID(SA_PARAM_OP_VALID Entity *pEntity, int iID);

bool isPowerOwned(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_VALID PowerDef *pPowerDef);
bool isPowerUnfiltered(SA_PARAM_NN_VALID PTNodeDef *pNodeDef, const char* pchFilterTokens);
bool isPowerAttribModUnfiltered(SA_PARAM_NN_VALID PTNodeDef *pNodeDef, const char* pchAttribModTokens);

typedef struct PowerNodeFilterCallbackData
{
	Entity* pRealEnt; 
	Entity* pFakeEnt;
	PowerTreeDef* pTreeDef;
	PTGroupDef* pGroupDef;
	PTNodeDef* pNodeDef; 
	PowerDef* pPowerDef;
	bool bIsOwned;
	bool bIsAvailable;
	bool bIsAvailableForFakeEnt;
} PowerNodeFilterCallbackData;

extern bool g_bCartRespecRequest;

typedef bool (*PowerNodeFilterCallback)(PowerNodeFilterCallbackData* pData, void* pUserData);

void gclPowersUIRequestRefsTreeType(PTTypeDef *pTypeDef);
void gclPowersUIRequestRefsTree(PowerTreeDef *pTreeDef);
void gclPowersUIRequestRefsTreeName(const char *pchTreeDef);

bool gclNodeMatchesAttributeAffectingPower(SA_PARAM_OP_VALID PTNodeDef* pNodeDef, SA_PARAM_OP_VALID PowerDef* pDef);
bool gclPowerNodePassesFilter(	Entity* pRealEnt, Entity *pFakeEnt, 
								PTGroupDef *pGroupDef, PTNodeDef *pNodeDef,
								PowerDef* pAttribsAffectingPowerDef,
								int iFilterMask, const char* pchTextFilter,
								PowerNodeFilterCallback pCallback, void* pCallbackData );

void FillPowerListNode(	Entity *pEnt,
						PowerListNode *pListNode,
						PowerTree *pTree, PowerTreeDef *pTreeDef,
						const char *pchGroup, PTGroupDef *pGroupDef,
						PTNode *pNode, PTNodeDef *pNodeDef );

void FillPowerListNodeForEnt(Entity* pRealEnt, Entity *pFakeEnt,
							 PowerListNode *pListNode,
							 PowerTree *pTree, PowerTreeDef *pTreeDef,
							 const char *pchGroup, PTGroupDef *pGroupDef,
							 PTNode *pNode, PTNodeDef *pNodeDef);

void FillPowerListNodeFromFilterData( PowerNodeFilterCallbackData* d, PowerListNode *pListNode );

S32 SortPowerListNodeByPurpose( const PowerListNode** pNodeA, const PowerListNode** pNodeB );

//returns the amount of ranks added or removed, or 0 if there is an error
//the passed in pGen is a UIGenList which is filled with invalid nodes if modifying ranks causes the power tree to become invalid
S32 gclGenExprPowerCartModifyPowerTreeNodeRank(SA_PARAM_OP_VALID UIGen *pGen,
											   SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity* pFakeEnt, 
											   SA_PARAM_OP_VALID PowerListNode* pListNode, S32 iRankDifference);

const char* gclAutoDescPower(Entity *pEnt, SA_PARAM_OP_VALID Power *pPower, PowerDef *pPowerDef, const char* pchPowerMessageKey, const char* pchAttribModsMessageKey, bool bTooltip);

S32 gclGetPowerAndAdvantageList(S32 iLength, SA_PARAM_NN_VALID PowerCartListNode ***peaCartNodes, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID PTNode **eaNodes, SA_PARAM_OP_VALID PTNodeDef **eaNodeDefs, const char* pchCostTables, bool bExcludeUnownedAdvantages, bool bIncludeAllPowers);

#endif
