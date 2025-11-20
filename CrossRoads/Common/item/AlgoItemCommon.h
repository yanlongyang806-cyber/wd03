#pragma once
GCC_SYSTEM


AUTO_STRUCT;
typedef struct CompCountEntry
{
	int count;  AST(STRUCTPARAM)

}CompCountEntry;


AUTO_STRUCT;
typedef struct ComponentCountQualityEntry
{
	CompCountEntry **ppCompCountEntry; AST(NAME(CompCountEntry))

}ComponentCountQualityEntry;


AUTO_STRUCT;
typedef struct ComponentCountLevelEntry
{
	ComponentCountQualityEntry **ppComponentCountQualityEntry; AST(NAME(ComponentCountQualityEntry))

}ComponentCountLevelEntry;


AUTO_STRUCT;
typedef struct ComponentCountTableEntry
{
	ComponentCountLevelEntry **ppComponentCountLevelEntry; AST(NAME(ComponentCountLevelEntry))

}ComponentCountTableEntry;


AUTO_STRUCT;
typedef struct CraftingCountAdjustDef
{
	//int level[NUM_ALGO_LEVELS];
	int level[50];  //temp hack to cap data to 50 levels.  Most other data is 60 levels, but algo spreadsheet only goes top 50

}CraftingCountAdjustDef;

#define ALGOITEMLEVELSMAX 50

AUTO_STRUCT;
typedef struct AlgoItemLevelsDef
{
	char *pchName;	AST( STRUCTPARAM KEY )
	//int level[NUM_ALGO_LEVELS];
	int level[ALGOITEMLEVELSMAX];  //temp hack to cap data to 50 levels.  Most other data is 60 levels, but algo spreadsheet only goes top 50

}AlgoItemLevelsDef;


AUTO_STRUCT;
typedef struct CommonAlgoTables
{
	CraftingCountAdjustDef **ppCraftingCountAdjust; AST(NAME(CraftingCountAdjust))
	ComponentCountTableEntry **ppComponentCountTableEntry; AST(NAME(ComponentCountTableEntry))
	AlgoItemLevelsDef **ppAlgoItemLevels; AST(NAME(AlgoItemLevels))
}CommonAlgoTables;

extern CommonAlgoTables g_CommonAlgoTables;

