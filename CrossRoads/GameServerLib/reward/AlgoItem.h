#ifndef ALGOITEM_H
#define ALGOITEM_H

typedef struct Entity Entity;
typedef struct Critter Critter;
typedef struct InventoryBag InventoryBag;
typedef struct RewardTable RewardTable;
typedef struct RewardContext RewardContext;
typedef struct Item Item;


#define NUM_ALGO_LEVELS 60
#define NUM_ALGO_COLORS 5

#define ALGO_TOTAL_WEIGHT	10000.0

AUTO_STRUCT;
typedef struct AlgoEntryDef
{
	int weight[NUM_ALGO_COLORS];

}AlgoEntryDef;

AUTO_STRUCT;
typedef struct AlgoTableDef
{
	char *pchName;	AST( STRUCTPARAM KEY )
	AlgoEntryDef **ppAlgoEntry; AST(NAME(AlgoEntry))

}AlgoTableDef;


AUTO_STRUCT;
typedef struct AlgoTables
{
	AlgoTableDef **ppAlgoTables; AST(NAME(AlgoTable))
    int MaxLevel;               AST(DEFAULT(40))
	F32 fItemPowerScale;		AST(NAME(AlgoItemPowerScale) ADDNAMES(ExtraCostConversionPercent) DEFAULT(1))
}AlgoTables;

extern AlgoTables g_AlgoTables;

Item* algoitem_generate(int iPartitionIdx, RewardContext *pContext, U32 *pSeed);
Item* algoitem_generate_quality(int iPartitionIdx, RewardContext *pContext, int Quality, U32 *pSeed);
Item* algoitem_generate_from_base(int iPartitionIdx, RewardContext *pContext, ItemDef* pItemDef, RewardTable* pRewardTable, int totalCost, int minRange, int maxRange, U32 *pSeed);
Item* algoitem_generate_shortcut(RewardContext *pContext, ItemDef* pItemDef, U32* pSeed);


#include "AutoGen/algoitem_h_ast.h"

#endif
