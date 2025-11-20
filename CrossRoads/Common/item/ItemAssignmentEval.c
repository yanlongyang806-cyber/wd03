/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "error.h"
#include "Expression.h"
#include "FolderCache.h"
#include "ItemAssignments.h"
#include "itemCommon.h"
#include "mission_common.h"
#include "StringCache.h"
#include "inventoryCommon.h"

#include "AutoGen/ItemAssignments_h_ast.h"
#include "AutoGen/itemCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static int s_hVarPlayer;
static const char* s_pchVarOutcome;
static int s_hVarOutcome = 0;
static const char* s_pchVarModifier;
static int s_hVarModifier = 0;
static const char* s_pchVarSlottedItem;
static int s_hVarSlottedItem = 0;
static const char* s_pchVarItemCategory;
static int s_hVarItemCategory = 0;
static const char* s_pchVarTotalItemCategories;
static int s_hVarTotalItemCategories = 0;
static const char* s_pchVarSecondsRemaining;
static int s_hVarSecondsRemaining = 0;
static const char* s_pchVarSlotDef;
static int s_hVarSlotDef = 0;
static ItemAssignmentVars s_ItemAssignmentVars = {0};
static const char* s_pchVarCompletedDetails;
static int s_hVarCompletedDetails = 0;

AUTO_RUN;
void ItemAssignmentsEval_Init(void)
{
	s_pchVarOutcome = allocAddStaticString("Outcome");
	s_pchVarModifier = allocAddStaticString("Modifier");
	s_pchVarSlottedItem = allocAddStaticString("SlottedItem");
	s_pchVarItemCategory = allocAddString("ItemCategory");
	s_pchVarTotalItemCategories = allocAddString("TotalItemCategories");
	s_pchVarSecondsRemaining = allocAddString("SecondsRemaining");
	s_pchVarSlotDef = allocAddString("SlotDef");
	s_pchVarCompletedDetails = allocAddString("CompletedDetails");
}

ExprContext* ItemAssignments_GetContextEx(Entity* pEnt, 
                                          ItemAssignmentCompletedDetails *pCompletedDetails,
										  ItemAssignmentOutcome* pOutcome, 
										  ItemAssignmentOutcomeModifier* pMod, 
										  Item* pSlottedItem, 
										  ItemAssignmentSlot *pSlotDef, 
										  ItemCategory eItemCategoryUI,
										  S32 iSecondsRemaining)
{
	static ExprContext *s_pItemAssignmentsContext = NULL;
	int iTotalItemCategories = 0;

	if (!s_pItemAssignmentsContext)
	{
		ExprFuncTable* stTable;

		s_pItemAssignmentsContext = exprContextCreate();
		stTable = exprContextCreateFunctionTable();

		exprContextAddFuncsToTableByTag(stTable,"entity");
		exprContextAddFuncsToTableByTag(stTable,"entityutil");
		exprContextAddFuncsToTableByTag(stTable,"player");
		exprContextAddFuncsToTableByTag(stTable,"util");
		exprContextAddFuncsToTableByTag(stTable,"ItemAssignments");

		exprContextSetFuncTable(s_pItemAssignmentsContext, stTable);

		exprContextSetAllowRuntimePartition(s_pItemAssignmentsContext);
		exprContextSetAllowRuntimeSelfPtr(s_pItemAssignmentsContext);

		assert(g_PlayerVarName != NULL);
	}
	
	if (pMod) {
		iTotalItemCategories = eaiSize(&pMod->peItemCategories);
	}
	exprContextSetPointerVarPooledCached(s_pItemAssignmentsContext,g_PlayerVarName,pEnt,parse_Entity,false,true,&s_hVarPlayer);
	exprContextSetPointerVarPooledCached(s_pItemAssignmentsContext,s_pchVarOutcome,pOutcome,parse_ItemAssignmentOutcome,true,true,&s_hVarOutcome);
	exprContextSetPointerVarPooledCached(s_pItemAssignmentsContext,s_pchVarModifier,pMod,parse_ItemAssignmentOutcomeModifier,true,true,&s_hVarModifier);
	exprContextSetPointerVarPooledCached(s_pItemAssignmentsContext,s_pchVarSlottedItem,pSlottedItem,parse_Item,false,true,&s_hVarSlottedItem);
	exprContextSetPointerVarPooledCached(s_pItemAssignmentsContext,s_pchVarSlotDef,pSlotDef,parse_ItemAssignmentSlot,false,true,&s_hVarSlotDef);
	exprContextSetPointerVarPooledCached(s_pItemAssignmentsContext,s_pchVarCompletedDetails,pCompletedDetails,parse_ItemAssignmentCompletedDetails,true,true,&s_hVarCompletedDetails);
	exprContextSetIntVarPooledCached(s_pItemAssignmentsContext,s_pchVarItemCategory,eItemCategoryUI,&s_hVarItemCategory);
	exprContextSetIntVarPooledCached(s_pItemAssignmentsContext,s_pchVarTotalItemCategories,iTotalItemCategories,&s_hVarTotalItemCategories);
	exprContextSetIntVarPooledCached(s_pItemAssignmentsContext,s_pchVarSecondsRemaining,iSecondsRemaining,&s_hVarSecondsRemaining);

	if (pEnt) {
		exprContextSetSelfPtr(s_pItemAssignmentsContext, pEnt);
		exprContextSetPartition(s_pItemAssignmentsContext, entGetPartitionIdx(pEnt));
	} else {
		exprContextClearSelfPtrAndPartition(s_pItemAssignmentsContext);
	}
	return s_pItemAssignmentsContext;
}

static MultiVal* ItemAssignment_FindVar(const char* pchName)
{
	ItemAssignmentVar* pVar = eaIndexedGetUsingString(&s_ItemAssignmentVars.eaVars, pchName);

	if (pVar)
	{
		return &pVar->mvValue;
	}

	ErrorDetailsf("%s",pchName);
	Errorf("Bad ItemAssignmentVar");
	return NULL;
}

static void ItemAssignmentVars_LoadInternal(const char *pchPath, S32 iWhen)
{
	StructReset(parse_ItemAssignmentVars, &s_ItemAssignmentVars);
	eaIndexedEnable(&s_ItemAssignmentVars.eaVars, parse_ItemAssignmentVar);

	loadstart_printf("Item Assignment Vars... ");

	ParserLoadFiles(NULL, 
		"defs/config/ItemAssignmentVars.def", 
		"ItemAssignmentVars.bin", 
		PARSER_OPTIONALFLAG, 
		parse_ItemAssignmentVars,
		&s_ItemAssignmentVars);

	loadend_printf(" done (%d Vars).", eaSize(&s_ItemAssignmentVars.eaVars));
}

AUTO_STARTUP(ItemAssignmentVars);
void ItemAssignmentVars_Load(void)
{
	ItemAssignmentVars_LoadInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/ItemAssignmentVars.def", ItemAssignmentVars_LoadInternal);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Expressions
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_EXPR_FUNC(ItemAssignments) ACMD_NAME(GetItemAssignmentVar);
F32 ItemAssignments_ExprGetVar(const char* pchVarName)
{
	F32 fResult = 0.0f;
	MultiVal *pMV = ItemAssignment_FindVar(pchVarName);
	if(pMV)
	{
		fResult = MultiValGetFloat(pMV, NULL);
	}
	return fResult;
}

// If iMaxCount is greater than 0, then stop counting when iCount >= iMaxCount
AUTO_EXPR_FUNC(ItemAssignments) ACMD_NAME(SlottedItemCountAffectedItemCategories);
S32 ItemAssignments_ExprSlottedItemCountAffectedItemCategories(ExprContext* pContext, S32 iMaxCount)
{
	ItemAssignmentOutcomeModifier* pMod = (ItemAssignmentOutcomeModifier*)exprContextGetVarPointerPooled(pContext, s_pchVarModifier, parse_ItemAssignmentOutcomeModifier);
	MultiVal* pItemCategory = exprContextGetSimpleVarPooled(pContext, s_pchVarItemCategory);
	Item* pItem = (Item*)exprContextGetVarPointerPooled(pContext, s_pchVarSlottedItem, parse_Item);
	ItemDef* pItemDef = SAFE_GET_REF(pItem, hItem);
	S32 iCount = 0;

	if (pMod && (pItemDef || pItemCategory))
	{
		S32 i;
		for (i = eaiSize(&pMod->peItemCategories)-1; i >= 0; i--)
		{
			ItemCategory eCategory = kItemCategory_None;
			if (pItemCategory)
			{
				eCategory = (ItemCategory)MultiValGetInt(pItemCategory, NULL);
			}
			if (eCategory != kItemCategory_None)
			{
				if (eCategory == pMod->peItemCategories[i])
				{
					iCount++;
					break;
				}
			}
			else if (pItemDef)
			{
				if (eaiFind(&pItemDef->peCategories, pMod->peItemCategories[i]) >= 0)
				{
					iCount++;
					if (iMaxCount > 0 && iCount >= iMaxCount)
					{
						break;
					}
				}
			}
		}
	}
	return iCount;
}

AUTO_EXPR_FUNC(ItemAssignments) ACMD_NAME(SlottedItemGetQualityWeight);
F32 ItemAssignments_ExprSlottedItemGetQualityWeight(ExprContext* pContext)
{
	ItemAssignmentOutcome* pOutcome = (ItemAssignmentOutcome*)exprContextGetVarPointerPooled(pContext, s_pchVarOutcome, parse_ItemAssignmentOutcome);
	const char* pchOutcomeName = SAFE_MEMBER(pOutcome, pchName);
	Item* pItem = (Item*)exprContextGetVarPointerPooled(pContext, s_pchVarSlottedItem, parse_Item);
	ItemDef* pItemDef = SAFE_GET_REF(pItem, hItem);
	ItemQuality eQuality = kItemQuality_None;

	if (pItemDef)
	{
		eQuality = pItemDef->Quality;
	}
	return ItemAssignments_GetQualityWeight(eQuality, pchOutcomeName);
}

AUTO_EXPR_FUNC(ItemAssignments) ACMD_NAME(SlottedItemGetQualityDurationScale);
F32 ItemAssignments_ExprSlottedItemGetQualityDurationScale(ExprContext* pContext)
{
	Item* pItem = (Item*)exprContextGetVarPointerPooled(pContext, s_pchVarSlottedItem, parse_Item);
	ItemDef* pItemDef = SAFE_GET_REF(pItem, hItem);
	ItemQuality eQuality = kItemQuality_None;

	if (pItemDef)
	{
		eQuality = pItemDef->Quality;
	}
	return ItemAssignments_GetQualityDurationScale(eQuality);
}

// checks if the current slot being evaluated is optional or not
AUTO_EXPR_FUNC(ItemAssignments) ACMD_NAME(IsSlottedItemInOptionalSlot);
bool ItemAssignments_ExprIsSlottedItemInOptionalSlot(ExprContext* pContext)
{
	ItemAssignmentSlot *pSlotDef = (ItemAssignmentSlot*)exprContextGetVarPointerPooled(pContext, s_pchVarSlotDef, parse_ItemAssignmentSlot);
	return pSlotDef ? pSlotDef->bIsOptional : false;
}

AUTO_EXPR_FUNC(ItemAssignments) ACMD_NAME(GetCategoriesAboveRank);
int ItemAssignments_ExprGetCategoriesAboveRank(ExprContext *pContext, int iCompareValue)
{
	Entity *pEnt = exprContextGetSelfPtr(pContext);
	int iReturn = 0;
	const ItemAssignmentCategorySettings **eaCateogries = NULL;
		
	if(pEnt)
	{
		eaCateogries = ItemAssignmentCategory_GetCategoryList();

		FOR_EACH_IN_EARRAY(eaCateogries, const ItemAssignmentCategorySettings, pCategory)
		{
			if (pCategory->pchNumericRank1)
			{
				if(inv_GetNumericItemValue(pEnt, pCategory->pchNumericRank1) >= iCompareValue)
					iReturn++;
			}
		}
		FOR_EACH_END
	}

	return iReturn;
}

AUTO_EXPR_FUNC(ItemAssignments) ACMD_NAME(GetAssignmentSpeedBonus);
F32 ItemAssignments_ExprGetAssignmentSpeedBonus(ExprContext *pContext)
{
	ItemAssignmentCompletedDetails* pDetails = (ItemAssignmentCompletedDetails*)exprContextGetVarPointerPooled(pContext, s_pchVarCompletedDetails, parse_ItemAssignmentCompletedDetails);
	F32 fReturn = 0.0f;
	if(pDetails)
	{
		ItemAssignmentDef *pDef = GET_REF(pDetails->hDef);

		if(pDef)
			fReturn = 1.0f - ((F32)pDetails->uDuration / (F32)pDef->uDuration);
	}

	return fReturn;
}

AUTO_EXPR_FUNC(ItemAssignments) ACMD_NAME(OutcomeHasResultRank);
bool ItemAssignments_ExprGetAssignmentResultRank(ExprContext *pContext, const char *pchResultRank)
{
	ItemAssignmentCompletedDetails* pDetails = (ItemAssignmentCompletedDetails*)exprContextGetVarPointerPooled(pContext, s_pchVarCompletedDetails, parse_ItemAssignmentCompletedDetails);
	
	if(stricmp(pchResultRank,pDetails->pchOutcome) == 0)
			return true;

	return false;
}

