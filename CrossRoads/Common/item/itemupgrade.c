#include "itemupgrade.h"

#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "inventoryCommon.h"
#include "textparser.h"
#include "stdtypes.h"
#include "entity.h"
#include "rand.h"
#include "AutoTransDefs.h"
#include "Expression.h"
#include "Character.h"

#include "AutoGen/itemupgrade_h_ast.h"
#include "autogen/Entity_h_ast.h"

#include "GlobalTypes.h"

#define ITEM_UPGRADE_FILE "defs/config/ItemUpgrade.def"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

ItemUpgradeConfig g_sItemUpgradeConfig = {0};
static ExprContext *s_pContext = NULL;
const char *s_pcSkillType;
const char *s_pcPlayer;
const char *s_pcSkillLevel;
static int s_hSkillType;
static int s_hSkillLevel;

AUTO_RUN;
void itemUpgrade_SetupContext(void)
{
	ExprFuncTable* stTable;

	s_pContext = exprContextCreate();
	exprContextSetAllowRuntimePartition(s_pContext);
	exprContextSetAllowRuntimeSelfPtr(s_pContext);

	// Functions
	//  Generic, Self, Character, ApplicationSimple
	stTable = exprContextCreateFunctionTable();
	exprContextAddFuncsToTableByTag(stTable, "util");
	exprContextAddFuncsToTableByTag(stTable, "CEFuncsGeneric");
	exprContextAddFuncsToTableByTag(stTable, "CEFuncsSelf");
	exprContextAddFuncsToTableByTag(stTable, "CEFuncsCharacter");
	exprContextAddFuncsToTableByTag(stTable, "Player");
	
	exprContextSetFuncTable(s_pContext, stTable);
	exprContextSetSelfPtr(s_pContext,NULL);

	s_pcSkillType = allocAddStaticString("LadderSkillType");
	s_pcPlayer = allocAddStaticString("Player");
	s_pcSkillLevel = allocAddStaticString("Craftingskilllevel");

	exprContextSetStringVarPooledCached(s_pContext,s_pcSkillType,StaticDefineIntRevLookup(SkillTypeEnum,kSkillType_None),&s_hSkillType);
	exprContextSetIntVarPooledCached(s_pContext,s_pcSkillLevel,0,&s_hSkillLevel);
	exprContextSetPointerVar(s_pContext,s_pcPlayer,NULL,parse_Entity,true,true);
}

void itemUpgrade_GenerateExpr(Expression *pExpr)
{
	exprGenerate(pExpr,s_pContext);
}

void itemUpgrade_EvalExprEx(Entity *pEnt, SkillType eSkillType, Expression *pExpr, MultiVal *mValOut)
{
	const char *pchSkillType = StaticDefineIntRevLookup(SkillTypeEnum,eSkillType);
	int iSkillLevel = inv_GetNumericItemValue(pEnt, "Skilllevel");

	if(!pExpr)
		return;

	exprContextSetStringVarPooledCached(s_pContext,s_pcSkillType,pchSkillType,&s_hSkillType);
	exprContextSetIntVarPooledCached(s_pContext,s_pcSkillLevel,iSkillLevel,&s_hSkillLevel);

	exprContextSetPointerVar(s_pContext,s_pcPlayer,pEnt,parse_Entity,true,true);

	exprContextSetSelfPtr(s_pContext,pEnt);
	exprContextSetPartition(s_pContext,entGetPartitionIdx(pEnt));

	exprEvaluate(pExpr,s_pContext,mValOut);
}

bool itemUpgrade_EvalExpr(Entity *pEnt, SkillType eSkillType, Expression *pExpr)
{
	MultiVal mVal;

	itemUpgrade_EvalExprEx(pEnt,eSkillType,pExpr,&mVal);

	if(mVal.int32)
		return true;

	return false;
}

int itemUpgrade_GetMaxStackAllowed(Entity *pEnt)
{
	int i;
	int iReturn = 1;

	for(i=0;i<eaSize(&g_sItemUpgradeConfig.ppMaxStack);i++)
	{
		if(g_sItemUpgradeConfig.ppMaxStack[i]->iMaxStack > iReturn && itemUpgrade_EvalExpr(pEnt,0,g_sItemUpgradeConfig.ppMaxStack[i]->pRequired))
		{
			iReturn = g_sItemUpgradeConfig.ppMaxStack[i]->iMaxStack;
		}
	}

	return iReturn;
}

F32 itemUpgrade_GetUpgradeTime(Entity *pEnt)
{
	F32 fReturn = g_sItemUpgradeConfig.fDefaultUpgradeTime;
	int i;

	for(i=0;i<eaSize(&g_sItemUpgradeConfig.ppMaxStack);i++)
	{
		if(g_sItemUpgradeConfig.ppMaxStack[i]->fUpgradeTime && g_sItemUpgradeConfig.ppMaxStack[i]->fUpgradeTime < fReturn && itemUpgrade_EvalExpr(pEnt,0,g_sItemUpgradeConfig.ppMaxStack[i]->pRequired))
		{
			fReturn = g_sItemUpgradeConfig.ppMaxStack[i]->fUpgradeTime;
		}
	}

	return fReturn;
}

AUTO_STARTUP(ItemUpgrade) ASTRT_DEPS(Items);
void itemUpgrade_Load(void)
{
	int i;
	char *estrName = NULL;


	ParserLoadFiles(NULL, ITEM_UPGRADE_FILE, "ItemUpgrade.bin", PARSER_OPTIONALFLAG, parse_ItemUpgradeConfig, &g_sItemUpgradeConfig);

	if(g_sItemUpgradeConfig.ppItemNames)
	{
		estrStackCreate(&estrName);

		for(i=0;i<eaSize(&g_sItemUpgradeConfig.ppItemNames);i++)
		{
			int iStep = 0;
			ItemUpgradeLadder *pNewLadder = StructCreate(parse_ItemUpgradeLadder);

			pNewLadder->pchItemPrefix = StructAllocString(g_sItemUpgradeConfig.ppItemNames[i]->pchName);
			pNewLadder->eSkillType = g_sItemUpgradeConfig.ppItemNames[i]->eSkillType;
		
			eaPush(&g_sItemUpgradeConfig.ppLadders,pNewLadder);

			for(iStep = 0;iStep<eaSize(&g_sItemUpgradeConfig.ppTiers);iStep++)
			{
				ItemDef *pDef = NULL;
				
				estrClear(&estrName);

				if(strstr(g_sItemUpgradeConfig.ppItemNames[i]->pchName,"%d"))
				{
					estrPrintf_dbg(&estrName,__FILE__,__LINE__,g_sItemUpgradeConfig.ppItemNames[i]->pchName,g_sItemUpgradeConfig.ppTiers[iStep]->iLevel);
				}
				else if(strstr(g_sItemUpgradeConfig.ppItemNames[i]->pchName,"%s"))
				{
					estrPrintf_dbg(&estrName,__FILE__,__LINE__,g_sItemUpgradeConfig.ppItemNames[i]->pchName,g_sItemUpgradeConfig.ppTiers[iStep]->pchRankStr);
				}
				else
				{
					estrPrintf(&estrName,"%s%d",g_sItemUpgradeConfig.ppItemNames[i]->pchName,g_sItemUpgradeConfig.ppTiers[iStep]->iLevel);
				}
				
				pDef = (ItemDef*)RefSystem_ReferentFromString(g_hItemDict,estrName);

				if(pDef || IsClient())
				{
					ItemDefRefContainer *pContainer = StructCreate(parse_ItemDefRefContainer);

					SET_HANDLE_FROM_STRING(g_hItemDict,estrName,pContainer->hItemDef);
					
					eaPush(&pNewLadder->ppItems,pContainer);
				}
				else
				{
					break;
				}
			}
		}

		for(i=0;i<eaSize(&g_sItemUpgradeConfig.ppMaxStack);i++)
		{
			if(g_sItemUpgradeConfig.ppMaxStack[i]->pRequired)
			{
				itemUpgrade_GenerateExpr(g_sItemUpgradeConfig.ppMaxStack[i]->pRequired);
			}
		}

		for(i=0;i<eaSize(&g_sItemUpgradeConfig.ppTiers);i++)
		{
			if(g_sItemUpgradeConfig.ppTiers[i]->pExprChance)
			{
				itemUpgrade_GenerateExpr(g_sItemUpgradeConfig.ppTiers[i]->pExprChance);
			}

			if(g_sItemUpgradeConfig.ppTiers[i]->pSkillTypeUpgradeChance)
			{
				itemUpgrade_GenerateExpr(g_sItemUpgradeConfig.ppTiers[i]->pSkillTypeUpgradeChance);
			}
		}

		if(!g_sItemUpgradeConfig.fDefaultUpgradeTime)
			g_sItemUpgradeConfig.fDefaultUpgradeTime = 2.0f;

		estrDestroy(&estrName);
	}
}

int itemUpgrade_FindCurrentRank(ItemDef *pBaseItem, ItemUpgradeLadder **ppLadderOut)
{
	int i,iReturn = 0;

	if(!pBaseItem)
		return -1;

	for(i=0;i<eaSize(&g_sItemUpgradeConfig.ppLadders);i++)
	{
		if(strstr(g_sItemUpgradeConfig.ppItemNames[i]->pchName,"%") || strnicmp(g_sItemUpgradeConfig.ppLadders[i]->pchItemPrefix, pBaseItem->pchName, strlen(g_sItemUpgradeConfig.ppLadders[i]->pchItemPrefix))==0)
		{
			ItemUpgradeLadder *pLadder = g_sItemUpgradeConfig.ppLadders[i];
			if(ppLadderOut)
			{
				*ppLadderOut = g_sItemUpgradeConfig.ppLadders[i];
			}

			for(iReturn=0;iReturn<eaSize(&pLadder->ppItems);iReturn++)
			{
				if(GET_REF(pLadder->ppItems[iReturn]->hItemDef) == pBaseItem)
					return iReturn;
			}
		}	
	}

	if(ppLadderOut)
		*ppLadderOut = NULL;

	return -1;
}

ItemUpgradeModifiers *itemUpgrade_FindModifier(ItemDef *pModifier)
{
	int i;

	for(i=0;i<eaSize(&g_sItemUpgradeConfig.ppModifiers);i++)
	{
		if(GET_REF(g_sItemUpgradeConfig.ppModifiers[i]->hItemDef) == pModifier)
			return g_sItemUpgradeConfig.ppModifiers[i];
	}

	return NULL;
}

int itemUpgrade_getRequiredUsingModifier(ItemUpgradeTiers *pTier, ItemUpgradeModifiers *pModifier)
{
	int iRequiredItemCount = pTier->iItemsRequired;

	if(pModifier)
	{
		if(pModifier->iOverrideRequired)
			iRequiredItemCount = pModifier->iOverrideRequired;
		else if(pModifier->iModifyRequired)
			iRequiredItemCount *= pModifier->iModifyRequired;
	}

	return iRequiredItemCount;
}


F32 itemUpgrade_getChanceUsingModifier(Entity *pEnt, ItemUpgradeLadder *pLadder, ItemUpgradeTiers *pTier, ItemUpgradeModifiers *pModifier)
{
	F32 fChance = pTier->fChance;

	if(pTier->pExprChance)
	{
		MultiVal mVal = {0};
		itemUpgrade_EvalExprEx(pEnt,pLadder->eSkillType,pTier->pExprChance,&mVal);
		
		fChance = MultiValGetFloat(&mVal,NULL);
	}

	if(pModifier)
	{
		if(pModifier->fOverrideChance)
			fChance = pModifier->fOverrideChance;
		else if(pModifier->fModifyChance)
			fChance += pModifier->fModifyChance;
	}

	return fChance;
}

F32 itemUpgrade_getBonusChance(Entity *pEnt)
{
	F32 fChance = 0.0f;
	int i;

	for(i=0;i<eaSize(&g_sItemUpgradeConfig.ppBonus);i++)
	{
		ItemUpgradeBonus *pBonus = g_sItemUpgradeConfig.ppBonus[i];

		if(IS_HANDLE_ACTIVE(pBonus->hMustOwnPower) && character_FindPowerByDef(pEnt->pChar,GET_REF(pBonus->hMustOwnPower)))
		{
			fChance += pBonus->fModifyChance;
		}
	}

	return fChance;
}

F32 itemUpgrade_GetChanceForItem(Entity *pEnt, ItemDef *pBaseItem, ItemDef *pModifier)
{
	ItemUpgradeLadder *pLadder = NULL;
	int iCurrentRank = 0;
	ItemUpgradeModifiers *pModifierDef = NULL;

	iCurrentRank = itemUpgrade_FindCurrentRank(pBaseItem,&pLadder);

	if(iCurrentRank == -1 || !pLadder)
		return 0.0f;

	if(iCurrentRank >= eaSize(&pLadder->ppItems))
		return 0.0f;

	if(iCurrentRank >= eaSize(&g_sItemUpgradeConfig.ppTiers))
		return 0.0f;

	if(pModifier)
	{
		pModifierDef = itemUpgrade_FindModifier(pModifier);

		if(!pModifierDef || iCurrentRank < pModifierDef->iTierMin || (pModifierDef->iTierMax && iCurrentRank > pModifierDef->iTierMax))
			return 0.0f;
	}

	return MIN(itemUpgrade_getChanceUsingModifier(pEnt,pLadder,g_sItemUpgradeConfig.ppTiers[iCurrentRank],pModifierDef) + itemUpgrade_getBonusChance(pEnt),1.0f);
}

SkillType itemUpgrade_GetSkillUpgrade(Entity *pEnt, ItemDef *pBaseItem)
{
	ItemUpgradeLadder *pLadder = NULL;
	int iCurrentRank = 0;
	ItemUpgradeModifiers *pModifierDef = NULL;

	iCurrentRank = itemUpgrade_FindCurrentRank(pBaseItem,&pLadder);

	if(iCurrentRank == -1 || !pLadder)
		return kSkillType_None;

	if(iCurrentRank >= eaSize(&pLadder->ppItems))
		return kSkillType_None;

	if(iCurrentRank >= eaSize(&g_sItemUpgradeConfig.ppTiers))
		return kSkillType_None;

	if(g_sItemUpgradeConfig.ppTiers[iCurrentRank]->pSkillTypeUpgradeChance)
	{
		if(entity_HasSkill(pEnt,pLadder->eSkillType))
		{
			F32 fChance = 0.0f;
			MultiVal mVal = {0};

			itemUpgrade_EvalExprEx(pEnt,pLadder->eSkillType,g_sItemUpgradeConfig.ppTiers[iCurrentRank]->pSkillTypeUpgradeChance,&mVal);

			fChance = MultiValGetFloat(&mVal,NULL);

			if(fChance && randomPositiveF32() <= fChance)
			{
				return pLadder->eSkillType;
			}
		}
	}

	return kSkillType_None;
}

AUTO_TRANS_HELPER;
ItemDef *itemUpgrade_GetUpgrade(ATH_ARG NOCONST(Entity) *pEnt, ItemDef *pBaseItem, int *iCount, ItemDef *pModifier)
{
	ItemUpgradeLadder *pLadder = NULL;
	ItemUpgradeModifiers *pModifierDef = NULL;
	int iCurrentRank = 0;
	int iRequiredItems = 0;

	iCurrentRank = itemUpgrade_FindCurrentRank(pBaseItem,&pLadder);

	if(iCurrentRank == -1 
		|| !pLadder
		|| iCurrentRank >= eaSize(&pLadder->ppItems)
		|| iCurrentRank >= eaSize(&g_sItemUpgradeConfig.ppTiers))
	{
		*iCount = -1;
		return NULL;
	}

	if(pModifier)
	{
		pModifierDef = itemUpgrade_FindModifier(pModifier);

		if(!pModifierDef || iCurrentRank < pModifierDef->iTierMin || (pModifierDef->iTierMax && iCurrentRank > pModifierDef->iTierMax))
		{
			*iCount = -1;
			return NULL;
		}
	}

	iRequiredItems = itemUpgrade_getRequiredUsingModifier(g_sItemUpgradeConfig.ppTiers[iCurrentRank],pModifierDef);

	if(*iCount >= iRequiredItems)
	{
		ItemDefRefContainer *pDefCon;
		ItemDef *pDef;

		// check if this is a go to max rank modifier
		if(pModifierDef && pModifierDef->iUpgradeToRank > iCurrentRank+1)
		{
			pDefCon = eaGet(&pLadder->ppItems, pModifierDef->iUpgradeToRank);
		}
		else
		{
			pDefCon = eaGet(&pLadder->ppItems, iCurrentRank+1);
		}

		pDef = SAFE_GET_REF(pDefCon, hItemDef);

		if (pDef)
		{
			*iCount = iRequiredItems;
			return pDef;
		}
	}

	*iCount = iRequiredItems;
	return NULL;
}


#include "AutoGen/itemupgrade_h_ast.c"