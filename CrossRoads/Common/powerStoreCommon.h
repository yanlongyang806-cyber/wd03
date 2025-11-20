#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#include "Message.h"
#include "ReferenceSystem.h"

typedef struct ContactDef ContactDef;
typedef struct ExprContext ExprContext;
typedef struct Expression Expression;
typedef struct ItemDef ItemDef;
typedef struct PowerTreeDef PowerTreeDef;
typedef struct PTNodeDef PTNodeDef;

extern ExprContext *g_pPowerStoreContext;

AUTO_ENUM;
typedef enum PowerStoreTrainerType
{
	kPowerStoreTrainerType_FromStore,
	kPowerStoreTrainerType_FromEntity,
	kPowerStoreTrainerType_FromItem,
} PowerStoreTrainerType;


// Defines a single power in a store
AUTO_STRUCT;
typedef struct PowerStorePowerDef
{
	REF_TO(PowerTreeDef) hTree;		AST(NAME(PowerTree))
	REF_TO(PTNodeDef) hNode;		AST(NAME(PowerTreeNode))
		// The PTNodeDef to purchase
	S32 iNodeRank;					AST(NAME(Rank) DEFAULT(1))
		// Set the node to this rank (note that this is 1-based on purpose)

	int iValue;						AST(NAME(Value))
	Expression *pExprCanBuy;		AST(LATEBIND NAME(ExprCanBuy))
	DisplayMessage cantBuyMessage;	AST(STRUCT(parse_DisplayMessage))
} PowerStorePowerDef;

extern ParseTable parse_PowerStorePowerDef[];
#define TYPE_parse_PowerStorePowerDef PowerStorePowerDef


// Defines a power store's inventory
AUTO_STRUCT;
typedef struct PowerStoreDef
{
	// Store name that uniquely identifies a power store.  Required.
	char *pcName;						AST(STRUCTPARAM KEY POOL_STRING)

	// Filename
	char *pcFilename;					AST(CURRENTFILE)

	// Scope helps determine filename
	char *pcScope;						AST(POOL_STRING SERVER_ONLY)

	// Comments for designers (not used at runtime)
	char *pcNotes;						AST(SERVER_ONLY)

	// List of powers this store can have
	PowerStorePowerDef **eaInventory;

	// The default numeric to use as a currency for powers
	REF_TO(ItemDef) hCurrency;

	bool bIsOfficerTrainer;				AST(ADDNAMES(IsTrainer) SERVER_ONLY)

} PowerStoreDef;

extern ParseTable parse_PowerStoreDef[];
#define TYPE_parse_PowerStoreDef PowerStoreDef


AUTO_STRUCT;
typedef struct PowerStoreCostInfo {
	REF_TO(ItemDef) hCurrency;
	int iCount;
	bool bTooExpensive;
} PowerStoreCostInfo;

extern ParseTable parse_PowerStoreCostInfo[];
#define TYPE_parse_PowerStoreCostInfo PowerStoreCostInfo

// This is used to display a Store Power in the Store UI
AUTO_STRUCT;
typedef struct PowerStorePowerInfo
{
	REF_TO(PowerTreeDef) hTree;
	REF_TO(PTNodeDef) hNode;
	int iNodeRank;					AST( NAME(Rank) )
		// Set the node to this rank (0-based)
	int iCount;

	bool bFailsRequirements : 1;
	char *pcRequirementsText;		AST( ESTRING )

	// When buying items, we use a Store Name and an Index
	const char *pcStoreName;		AST( POOL_STRING )
	int iIndex;

	// How much the item costs (or how much it will sell for)
	PowerStoreCostInfo **eaCostInfo;
	// PowerStore trainer type
	PowerStoreTrainerType eTrainerType;

} PowerStorePowerInfo;

extern ParseTable parse_PowerStorePowerInfo[];
#define TYPE_parse_PowerStorePowerInfo PowerStorePowerInfo

extern DictionaryHandle g_PowerStoreDictionary;

PowerStoreDef* powerstore_DefFromName(char* storeName);
bool powerstore_Validate(PowerStoreDef *pDef);

