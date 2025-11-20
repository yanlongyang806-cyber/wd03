
#include "referencesystem.h"
#include "itemEnums.h"


#ifndef ITEMUPGRADE_H__
#define ITEMUPGRADE_H__

typedef struct ItemDef ItemDef;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct NOCONST(InventoryBag) NOCONST(InventoryBag);
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct Entity Entity;
typedef struct Expression Expression;
typedef struct PowerDef PowerDef;

AUTO_STRUCT;
typedef struct ItemDefRefContainer
{
	REF_TO(ItemDef) hItemDef;
}ItemDefRefContainer;

AUTO_STRUCT;
typedef struct ItemUpgradeLadder
{
	const char *pchItemPrefix;
	ItemDefRefContainer **ppItems;
	SkillType eSkillType;
}ItemUpgradeLadder;

AUTO_STRUCT;
typedef struct ItemUpgradeTiers
{
	int iLevel;								AST(STRUCTPARAM)
	F32 fChance;
	Expression *pExprChance;				AST(NAME(ExprChance, ExprChanceBlock), REDUNDANT_STRUCT(chanceExpr, parse_Expression_StructParam), LATEBIND)

	Expression *pSkillTypeUpgradeChance;	AST(NAME(ExprUpgradeChance, ExprUpgradeChanceBlock), REDUNDANT_STRUCT(UpgradeSkillChanceExpr, parse_Expression_StructParam), LATEBIND)
	int iItemsRequired;
	char *pchRankStr;						AST(NAME(RankString))
}ItemUpgradeTiers;

AUTO_STRUCT;
typedef struct ItemUpgradeModifiers
{
	REF_TO(ItemDef) hItemDef;				AST(NAME(ItemDef))
	F32 fModifyChance;						AST(NAME(ModifyChance))
	F32 fOverrideChance;					AST(NAME(OverrideChance))
	int iModifyRequired;					AST(NAME(ModifyRequired))
	int iOverrideRequired;					AST(NAME(OverrideRequired))
	S32 iUpgradeToRank;						AST(NAME(UpgradeToRank))

	U32 bNoLossOnFailure: 1;				AST(NAME(NoLossOnFailure))

	int iTierMin;							AST(NAME(TierMin))
	int iTierMax;							AST(NAME(TierMax))

	U32 bOnlyConsumeModifierOnFail : 1;		AST(NAME(OnlyConsumeModifierOnFail))
}ItemUpgradeModifiers;

AUTO_STRUCT;
typedef struct ItemUpgradeBonus
{
	F32 fModifyChance;						AST(NAME(ModifyChance))
	
	REF_TO(PowerDef) hMustOwnPower;			AST(NAME(MustOwnPower))
}ItemUpgradeBonus;

AUTO_STRUCT;
typedef struct ItemUpgradeMaxStack
{
	int iMaxStack;							AST(STRUCTPARAM)
	F32 fUpgradeTime;						AST(STRUCTPARAM)
	Expression *pRequired;					AST(STRUCTPARAM NAME(ExprRequired, ExprRequiredBlock), REDUNDANT_STRUCT(Required, parse_Expression_StructParam), LATEBIND)
}ItemUpgradeMaxStack;

AUTO_STRUCT;
typedef struct ItemUpgradeNames
{
	const char *pchName;					AST(STRUCTPARAM)
	SkillType eSkillType;				AST(STRUCTPARAM)
}ItemUpgradeNames;

AUTO_STRUCT;
typedef struct ItemUpgradeConfig
{
	ItemUpgradeNames **ppItemNames;				AST(NAME(ItemName))

	ItemUpgradeLadder **ppLadders;			AST(NAME(UpgradeLadder))
	ItemUpgradeTiers **ppTiers;				AST(NAME(Tier))
	ItemUpgradeModifiers **ppModifiers;		AST(NAME(Modifier))
	ItemUpgradeMaxStack **ppMaxStack;		AST(NAME(MaxStack))
	ItemUpgradeBonus **ppBonus;				AST(NAME(Bonus))

	F32 fDefaultUpgradeTime;				AST(NAME(UpgradeTime))
}ItemUpgradeConfig;

AUTO_STRUCT;
typedef struct ItemUpgradeStack
{
	InvBagIDs eSrcBagId;
	int iSrcSlotIdx;
	U64 uSrcItemId;						AST(NAME(SrcItemId))
	InvBagIDs eModBagId;
	int iModSlotIdx;
	U64 uModItemId;
	int iStackRemaining;
	const char *pchModDef;
	F32 fChance;

	int eEntID;
}ItemUpgradeStack;

AUTO_ENUM;
typedef enum ItemUpgradeResult
{
	kItemUpgradeResult_None = 0,
	kItemUpgradeResult_Success,
	kItemUpgradeResult_Failure,
	kItemUpgradeResult_Broken,
	kItemUpgradeResult_Waiting, //Waiting for a result
	kItemUpgradeResult_FailureNoLoss,	// the source item didn't loss anything
	kItemUpgradeResult_UserCancelled,	// user cancelled the job

}ItemUpgradeResult;

AUTO_STRUCT;
typedef struct ItemUpgradeInfo
{
	ItemUpgradeStack *pCurrentStack;
	F32 fUpgradeTime;
	F32 fFullUpgradeTime;
	ItemUpgradeResult eLastResult;
}ItemUpgradeInfo;

void itemUpgrade_BeginStack(Entity *pEnt, int iStackAmount, InvBagIDs eSrcBagID, int SrcSlotIdx, U64 uSrcItemID, InvBagIDs eModBagID, int ModSlotIdx, U64 uModItemID);
void itemUpgrade_UpgradeStack(Entity *pEnt, ItemUpgradeStack *pStack);
int itemUpgrade_GetMaxStackAllowed(Entity *pEnt);
F32 itemUpgrade_GetUpgradeTime(Entity *pEnt);
F32 itemUpgrade_getChanceUsingModifier(Entity *pEnt, ItemUpgradeLadder *pLadder, ItemUpgradeTiers *pTier, ItemUpgradeModifiers *pModifier);
F32 itemUpgrade_GetChanceForItem(Entity *pEnt, ItemDef *pBaseItem, ItemDef *pModifier);

ItemDef *itemUpgrade_GetUpgrade(ATH_ARG NOCONST(Entity) *pEnt, ItemDef *pBaseItem, int *iCount, ItemDef *pModifier);
SkillType itemUpgrade_GetSkillUpgrade(Entity *pEnt, ItemDef *pBaseItem);
ItemUpgradeModifiers *itemUpgrade_FindModifier(ItemDef *pModifier);

void itemUpgrade_CancelJob(Entity *pEnt);

int itemUpgrade_FindCurrentRank(ItemDef *pBaseItem, ItemUpgradeLadder **ppLadderOut);

#endif