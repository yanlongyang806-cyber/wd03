/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include <math.h>

#include "fileutil.h"
#include "Expression.h"
#include "FolderCache.h"
#include "StringCache.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "GameBranch.h"
#include "GlobalEnums.h"
#include "WorldGrid.h"
#include "Character.h"

#include "Powers.h"
#include "PowerVars.h"
#include "itemCommon.h"
#include "itemCommon_h_ast.h"
#include "itemEnums.h"
#include "itemEnums_h_ast.h"
#include "inventoryCommon.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

ExprContext *g_pItemContext = NULL;
bool bContextInUse = false;
ExprContext *g_pChildItemContext = NULL; // For nested evaluation
bool bChildContextInUse = false;
static const char *s_pcVarLevel;
static int s_hVarLevel = 0;
static const char *s_pcVarQuality;
static int s_hVarQuality = 0;
static const char *s_pcVarEP;
static int s_hVarEP = 0;
static const char *s_pcVarPlayer;
static int s_hVarPlayer = 0;
static const char *s_pcVarItem;
static int s_hVarItem = 0;
static const char *s_pcVarItemDef;
static int s_hVarItemDef = 0;
static const char *s_pcVarGem;
static int s_hVarGem = 0;
static const char *s_pcVarGemDef;
static int s_hVarGemDef = 0;
static const char *s_pcVarAppearanceDef;
static int s_hVarAppearanceDef = 0;
static const char *s_pcVarItemPowerDef;
static int s_hVarItemPowerDef = 0;
static const char *s_pcVarEffectiveLevel;
static int s_hVarEffectiveLevel;
static const char *s_pcVarGemLevel;
static int s_hVarGemLevel;
static const char *s_pcVarDestinationSlot;
static int s_hVarDestinationSlot;
ItemVars s_ItemVars;

void itemeval_EvalGem(int iPartitionIdx, Expression *pExpr, ItemDef *pDef, ItemPowerDef *pItemPowerDef, Item *pItem, ItemDef *pGemDef, ItemDef *pAppearanceDef, Entity *pEnt, S32 iLevel, ItemQuality eQuality, S32 iEP, const char *pcFilename, S32 iDestinationSlot, MultiVal * pResult)
{
	if (pExpr) {
		if (g_pItemContext && !bContextInUse) 
		{
			bool bValid = false;
			bContextInUse = true;
			exprContextSetSelfPtr(g_pItemContext, pEnt);
			exprContextSetSilentErrors(g_pItemContext, false);
			exprContextSetIntVarPooledCached(g_pItemContext, s_pcVarLevel, iLevel, &s_hVarLevel);
			exprContextSetIntVarPooledCached(g_pItemContext, s_pcVarQuality, eQuality, &s_hVarQuality);
			exprContextSetIntVarPooledCached(g_pItemContext, s_pcVarEP, iEP, &s_hVarEP);
			exprContextSetIntVarPooledCached(g_pItemContext, s_pcVarEffectiveLevel, pItem ? item_GetLevel(pItem) : pDef->iLevel, &s_hVarEffectiveLevel);
			exprContextSetIntVarPooledCached(g_pItemContext, s_pcVarGemLevel, pItem ? item_GetGemPowerLevel(pItem) : pDef->iLevel, &s_hVarGemLevel);
			exprContextSetIntVarPooledCached(g_pItemContext, s_pcVarDestinationSlot, iDestinationSlot, &s_hVarDestinationSlot);
			exprContextSetPointerVarPooledCached(g_pItemContext, s_pcVarPlayer, (Entity*)pEnt, parse_Entity, true, true, &s_hVarPlayer);
			exprContextSetPointerVarPooledCached(g_pItemContext, s_pcVarItem, (Item*)pItem, parse_Item, true, true, &s_hVarItem);
			exprContextSetPointerVarPooledCached(g_pItemContext, s_pcVarItemDef, (ItemDef*)pDef, parse_ItemDef, true, true, &s_hVarItemDef);
			exprContextSetPointerVarPooledCached(g_pItemContext, s_pcVarItemPowerDef, (Item*)pItemPowerDef, parse_ItemPowerDef, true, true, &s_hVarItemPowerDef);
			exprContextSetPointerVarPooledCached(g_pItemContext, s_pcVarGemDef, (ItemDef*)pGemDef, parse_ItemDef, true, true, &s_hVarGemDef);
			exprContextSetPointerVarPooledCached(g_pItemContext, s_pcVarAppearanceDef, (ItemDef*)pAppearanceDef, parse_ItemDef, true, true, &s_hVarGemDef);
			exprContextSetPartition(g_pItemContext, iPartitionIdx);
			exprEvaluate(pExpr, g_pItemContext, pResult);
			bContextInUse = false;
		} 
		else if (g_pChildItemContext && !bChildContextInUse) 
		{
			bool bValid = false;
			bChildContextInUse = true;
			exprContextSetSelfPtr(g_pChildItemContext, pEnt);
			exprContextSetSilentErrors(g_pChildItemContext, false);
			exprContextSetIntVarPooledCached(g_pChildItemContext, s_pcVarLevel, iLevel, &s_hVarLevel);
			exprContextSetIntVarPooledCached(g_pChildItemContext, s_pcVarQuality, eQuality, &s_hVarQuality);
			exprContextSetIntVarPooledCached(g_pChildItemContext, s_pcVarEP, iEP, &s_hVarEP);
			exprContextSetIntVarPooledCached(g_pChildItemContext, s_pcVarEffectiveLevel, pItem ? item_GetLevel(pItem) : pDef->iLevel, &s_hVarEffectiveLevel);
			exprContextSetIntVarPooledCached(g_pChildItemContext, s_pcVarGemLevel, pItem ? item_GetGemPowerLevel(pItem) : pDef->iLevel, &s_hVarGemLevel);
			exprContextSetIntVarPooledCached(g_pChildItemContext, s_pcVarDestinationSlot, iDestinationSlot, &s_hVarDestinationSlot);
			exprContextSetPointerVarPooledCached(g_pChildItemContext, s_pcVarPlayer, (Entity*)pEnt, parse_Entity, true, true, &s_hVarPlayer);
			exprContextSetPointerVarPooledCached(g_pChildItemContext, s_pcVarItem, (Item*)pItem, parse_Item, true, true, &s_hVarItem);
			exprContextSetPointerVarPooledCached(g_pChildItemContext, s_pcVarItemDef, (ItemDef*)pDef, parse_ItemDef, true, true, &s_hVarItemDef);
			exprContextSetPointerVarPooledCached(g_pChildItemContext, s_pcVarItemPowerDef, (Item*)pItemPowerDef, parse_ItemPowerDef, true, true, &s_hVarItemPowerDef);
			exprContextSetPointerVarPooledCached(g_pChildItemContext, s_pcVarGemDef, (ItemDef*)pGemDef, parse_ItemDef, true, true, &s_hVarGemDef);
			exprContextSetPointerVarPooledCached(g_pChildItemContext, s_pcVarAppearanceDef, (ItemDef*)pAppearanceDef, parse_ItemDef, true, true, &s_hVarGemDef);
			exprContextSetPartition(g_pChildItemContext, iPartitionIdx);
			exprEvaluate(pExpr, g_pChildItemContext, pResult);
			bChildContextInUse = false;
		}
		else 
		{
			ErrorFilenamef(pcFilename, "Error executing item expression 'NULL or in use Context':\n%s", exprGetCompleteString(pExpr));
		}
	}
}

void itemeval_Eval(int iPartitionIdx, Expression *pExpr, ItemDef *pDef, ItemPowerDef *pItemPowerDef, Item *pItem,  Entity *pEnt, S32 iLevel, ItemQuality eQuality, S32 iEP, const char *pcFilename, S32 iDestinationSlot, MultiVal * pResult)
{
	itemeval_EvalGem(iPartitionIdx,pExpr,pDef,pItemPowerDef,pItem,NULL,NULL,pEnt,iLevel,eQuality,iEP,pcFilename,iDestinationSlot,pResult);
}

S32 itemeval_GetTransmutationCost(int iPartitionIdx, Entity *pEnt, Item* pStats, Item* pAppearance)
{
	if (g_ItemConfig.pItemTransmuteCost)
	{
		ItemDef* pAppearanceDef = SAFE_GET_REF3(pAppearance, pSpecialProps, pTransmutationProps, hTransmutatedItemDef);
		ItemDef* pStatsDef = GET_REF(pStats->hItem);
		MultiVal mv = {0};
		
		if (!pAppearanceDef)
			pAppearanceDef = GET_REF(pAppearance->hItem);

		itemeval_EvalGem(iPartitionIdx,g_ItemConfig.pItemTransmuteCost,pStatsDef,NULL,NULL,NULL,pAppearanceDef,pEnt,0,0,0,NULL,0,&mv);
		return MultiValGetInt(&mv, NULL);
	}
	return 0;
}

int itemeval_GetIntResult(MultiVal * pResult, const char *pcFilename, Expression *pExpr)
{
	bool bValid;
	int iResult = MultiValGetInt(pResult, &bValid);

	if (!bValid) {
		if (pResult->type == MULTI_INVALID) {
			ErrorFilenamef(pcFilename, "Error executing item expression '%s':\n%s", pResult->str, exprGetCompleteString(pExpr));
		} else {
			ErrorFilenamef(pcFilename, "Item expression returned incorrect data type:\n%s", exprGetCompleteString(pExpr));
		}
	}

	return iResult;
}

float itemeval_GetFloatResult(MultiVal * pResult, const char *pcFilename, Expression *pExpr)
{
	bool bValid;
	float fResult = MultiValGetFloat(pResult, &bValid);

	if (!bValid) {
		if (pResult->type == MULTI_INVALID) {
			ErrorFilenamef(pcFilename, "Error executing item expression '%s':\n%s", pResult->str, exprGetCompleteString(pExpr));
		} else {
			ErrorFilenamef(pcFilename, "Item expression returned incorrect data type:\n%s", exprGetCompleteString(pExpr));
		}
	}

	return fResult;
}

AUTO_STARTUP(ItemEval) ASTRT_DEPS(ItemQualities);
void
itemeval_StartupLate(void)
{
    if ( g_pItemContext == NULL )
    {
        Errorf("itemeval_StartupLate called before item context is created");
        return;
    }
    if ( g_pItemContext == NULL )
    {
        Errorf("itemeval_StartupLate called before child item context is created");
        return;
    }

    exprContextAddStaticDefineIntAsVars(g_pItemContext, ItemQualityEnum, "ItemQuality_");
    exprContextAddStaticDefineIntAsVars(g_pChildItemContext, ItemQualityEnum, "ItemQuality_");
}

AUTO_RUN;
int itemeval_Startup(void)
{
	ExprFuncTable* stFuncs;

	s_pcVarLevel = allocAddStaticString("Level");
	s_pcVarQuality = allocAddStaticString("Quality");
	s_pcVarEP = allocAddStaticString("EP");
	s_pcVarPlayer = allocAddStaticString("Player");
	s_pcVarItem = allocAddStaticString("Item");
	s_pcVarItemDef = allocAddStaticString("ItemDef");
	s_pcVarItemPowerDef = allocAddStaticString("ItemPowerDef");
	s_pcVarEffectiveLevel = allocAddStaticString("EffectiveLevel");
	s_pcVarGemLevel = allocAddStaticString("GemEffectiveLevel");
	s_pcVarDestinationSlot = allocAddStaticString("DestinationSlot");
	s_pcVarGemDef = allocAddStaticString("GemDef");
	s_pcVarAppearanceDef = allocAddStaticString("AppearanceDef");
	stFuncs = exprContextCreateFunctionTable();
	exprContextAddFuncsToTableByTag(stFuncs, "gameutil");
	exprContextAddFuncsToTableByTag(stFuncs, "player");
	exprContextAddFuncsToTableByTag(stFuncs, "util");
	exprContextAddFuncsToTableByTag(stFuncs, "ItemEval");
	exprContextAddFuncsToTableByTag(stFuncs, "CEFuncsCharacter");
	exprContextAddFuncsToTableByTag(stFuncs, "CEFuncsGeneric");
	exprContextAddFuncsToTableByTag(stFuncs, "CEFuncsSelf");
	exprContextAddFuncsToTableByTag(stFuncs, "PTECharacter");

	g_pItemContext = exprContextCreate();
	exprContextSetFuncTable(g_pItemContext, stFuncs);
	exprContextSetSelfPtr(g_pItemContext, NULL);
	exprContextSetAllowRuntimeSelfPtr(g_pItemContext);
	exprContextSetAllowRuntimePartition(g_pItemContext);
	exprContextSetIntVarPooledCached(g_pItemContext, s_pcVarLevel, 0, &s_hVarLevel);
	exprContextSetIntVarPooledCached(g_pItemContext, s_pcVarQuality, 0, &s_hVarQuality);
	exprContextSetIntVarPooledCached(g_pItemContext, s_pcVarEP, 0, &s_hVarEP);
	exprContextSetIntVarPooledCached(g_pItemContext, s_pcVarEffectiveLevel, 0, &s_hVarEffectiveLevel);
	exprContextSetIntVarPooledCached(g_pItemContext, s_pcVarGemLevel, 0, &s_hVarGemLevel);
	exprContextSetIntVarPooledCached(g_pItemContext, s_pcVarDestinationSlot, 0, &s_hVarDestinationSlot);
	exprContextSetPointerVarPooledCached(g_pItemContext, s_pcVarPlayer, NULL, parse_Entity, true, true, &s_hVarPlayer);
	exprContextSetPointerVarPooledCached(g_pItemContext, s_pcVarItem, NULL, parse_Item, true, true, &s_hVarItem);
	exprContextSetPointerVarPooledCached(g_pItemContext, s_pcVarItemDef, NULL, parse_ItemDef, true, true, &s_hVarItemDef);
	exprContextSetPointerVarPooledCached(g_pItemContext, s_pcVarItemPowerDef, NULL, parse_ItemPowerDef, true, true, &s_hVarItemPowerDef);
	exprContextSetPointerVarPooledCached(g_pItemContext, s_pcVarGemDef, NULL, parse_ItemDef, true, true, &s_hVarGemDef);
	exprContextSetPointerVarPooledCached(g_pItemContext, s_pcVarAppearanceDef, NULL, parse_ItemDef, true, true, &s_hVarAppearanceDef);

	g_pChildItemContext = exprContextCreate();
	exprContextSetFuncTable(g_pChildItemContext, stFuncs);
	exprContextSetSelfPtr(g_pChildItemContext, NULL);
	exprContextSetIntVarPooledCached(g_pChildItemContext, s_pcVarLevel, 0, &s_hVarLevel);
	exprContextSetIntVarPooledCached(g_pChildItemContext, s_pcVarQuality, 0, &s_hVarQuality);
	exprContextSetIntVarPooledCached(g_pChildItemContext, s_pcVarEP, 0, &s_hVarEP);
	exprContextSetIntVarPooledCached(g_pChildItemContext, s_pcVarEffectiveLevel, 0, &s_hVarEffectiveLevel);
	exprContextSetIntVarPooledCached(g_pChildItemContext, s_pcVarGemLevel, 0, &s_hVarGemLevel);
	exprContextSetIntVarPooledCached(g_pChildItemContext, s_pcVarDestinationSlot, 0, &s_hVarDestinationSlot);
	exprContextSetPointerVarPooledCached(g_pChildItemContext, s_pcVarPlayer, NULL, parse_Entity, true, true, &s_hVarPlayer);
	exprContextSetPointerVarPooledCached(g_pChildItemContext, s_pcVarItem, NULL, parse_Item, true, true, &s_hVarItem);
	exprContextSetPointerVarPooledCached(g_pChildItemContext, s_pcVarItemDef, NULL, parse_ItemDef, true, true, &s_hVarItemDef);
	exprContextSetPointerVarPooledCached(g_pChildItemContext, s_pcVarItemPowerDef, NULL, parse_ItemPowerDef, true, true, &s_hVarItemPowerDef);
	exprContextSetPointerVarPooledCached(g_pChildItemContext, s_pcVarGemDef, NULL, parse_ItemDef, true, true, &s_hVarGemDef);
	exprContextSetPointerVarPooledCached(g_pChildItemContext, s_pcVarAppearanceDef, NULL, parse_ItemDef, true, true, &s_hVarAppearanceDef);

	exprContextSetParent(g_pChildItemContext, g_pItemContext);
	
	return 1;
}

// Register the current set if item vars with the EconPoints expression context
void itemvar_Register(void)
{
	S32 i;
	
	for (i = eaSize(&s_ItemVars.ppItemVars)-1; i >= 0; i--) {
		bool bValid;
		F64 fValue = MultiValGetInt(&s_ItemVars.ppItemVars[i]->mvValue, &bValid);
		if (bValid) {
			exprContextSetFloatVarPooledCached(g_pItemContext, allocAddString(s_ItemVars.ppItemVars[i]->pchName), fValue, &s_ItemVars.ppItemVars[i]->hVarHandle);
		} else {
			ErrorFilenamef(s_ItemVars.ppItemVars[i]->cpchFile, "Invalid item var %s:\n%s", s_ItemVars.ppItemVars[i]->pchName, s_ItemVars.ppItemVars[i]->mvValue.str);
		}
	}
}

// Some aspect of the list of item vars was changed, handle that yo!
static int itemvar_Reload_SubStructCallback(void *pStruct, void *pOldStruct, ParseTable *pTPI, eParseReloadCallbackType eType)
{
	if(eType==eParseReloadCallbackType_Add)
	{
		// Added a item var, add it to stash table
		stashAddPointer(s_ItemVars.stItemVars,((ItemVar *)pStruct)->pchName,pStruct,0);
	}
	else if(eType==eParseReloadCallbackType_Delete)
	{
		// Removed a item var, remove it from the stash table
		stashRemovePointer(s_ItemVars.stItemVars,((ItemVar *)pStruct)->pchName,NULL);
	}
	else if(eType==eParseReloadCallbackType_Update)
	{
		// Nada
	}
	return 1;
}

// Reload item vars top level callback
static void itemvar_Reload(const char *pchRelPath, int UNUSED_when)
{
	// Yes, this leaks the array.  Tough luck.
	loadstart_printf("Reloading item vars...");
	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);
	if(!ParserReloadFile(pchRelPath, parse_ItemVars, &s_ItemVars, itemvar_Reload_SubStructCallback, 0))
	{
		// Something went wrong, how do we handle that?
	}
	itemvar_Register();
	loadend_printf(" done (%d vars)", eaSize(&s_ItemVars.ppItemVars));
}

// Returns the named variable, or NULL if the variable doesn't exist
MultiVal *itemvar_Find(const char *pchName)
{
	ItemVar *ivar = NULL;
	stashFindPointer(s_ItemVars.stItemVars,pchName,&ivar);
	if(ivar)
	{
		return &ivar->mvValue;
	}
	else
	{
		devassertmsg(0, "Bad item var");
		return NULL;
	}
}

AUTO_FIXUPFUNC;
TextParserResult itemvars_Fixup(ItemVars *iVars, enumTextParserFixupType eFixupType, void *pExtraData)
{
	int i;
	bool r = true;
	switch (eFixupType)
	{
	case FIXUPTYPE_POST_BINNING_DURING_LOADFILES:	// Intentional fallthrough
	case FIXUPTYPE_POST_LOAD_DURING_LOADFILES_FINAL_LOCATION:
		if(!s_ItemVars.stItemVars)
			s_ItemVars.stItemVars = stashTableCreateWithStringKeys(16,StashDefault);
		for(i=eaSize(&iVars->ppItemVars)-1; i>=0; i--)
		{
			r = stashAddPointer(s_ItemVars.stItemVars,iVars->ppItemVars[i]->pchName,iVars->ppItemVars[i],true)
				&& r;
		}
		break;
	}

	// We've 'inflated' the stash table prior to putting it in shared memory.  Now empty it without
	//  deflating so it doesn't have a bunch of bad pointers
	if(eFixupType==FIXUPTYPE_POST_BINNING_DURING_LOADFILES)
	{
		stashTableClear(s_ItemVars.stItemVars);
	}

	return r;
}

// Loads all item vars
AUTO_STARTUP(ItemVars);
void itemvariables_Load(void)
{
	char *pBuffer = NULL;
	loadstart_printf("Loading item variables...");
	
	ParserLoadFiles(	GameBranch_GetDirectory(&pBuffer, "defs/itemvars"),
						".itemvar",
						GameBranch_GetFilename(&pBuffer, "ItemVars.bin"),
						PARSER_OPTIONALFLAG, parse_ItemVars, &s_ItemVars);
	itemvar_Register();
	
	FolderCacheSetCallback(
		FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, 
		GameBranch_FixupPath(&pBuffer, "defs/itemvars/*.itemvar", true, false),
		itemvar_Reload);
	
	loadend_printf(" done (%d vars).", eaSize(&s_ItemVars.ppItemVars) );
	estrDestroy(&pBuffer);
}

AUTO_EXPR_FUNC(ItemEval) ACMD_NAME(PowerTable);
F32 itemeval_PowerTable(ACMD_EXPR_SC_TYPE(PowerTable) const char* pcTableName, U32 iIndex)
{
	return powertable_Lookup(pcTableName, iIndex);
}

// look up an item var based on the Def's name
AUTO_EXPR_FUNC(ItemEval) ACMD_NAME(ItemVarFromItemDef);
F32 itemeval_ItemVarFromItemDef(const char* pcTableStem, SA_PARAM_OP_VALID ItemDef *pDef)
{
	char tmp[MAX_PATH];
	MultiVal *mv;
	if(!pDef)
		return 0.f;
	if (pcTableStem && pcTableStem[0])
	{	
		sprintf(tmp,"%s_%s",pcTableStem,pDef->pchName);
	}
	else
	{
		sprintf(tmp,"%s",pDef->pchName);
	}
	mv = itemvar_Find(tmp);

	if (mv)
		return MultiValGetFloat(mv, NULL);
	return 0.f;
}

// look up an item var based on the ItemPowerDef's name
AUTO_EXPR_FUNC(ItemEval) ACMD_NAME(ItemVarFromItemPowerDef);
F32 itemeval_ItemVarFromItemPowerDef(const char* pcTableStem, SA_PARAM_OP_VALID ItemPowerDef *pDef)
{
	char tmp[MAX_PATH];
	MultiVal *mv;
	if(!pDef)
		return 0.f;
	if (pcTableStem && pcTableStem[0])
	{	
		sprintf(tmp,"%s_%s",pcTableStem,pDef->pchName);
	}
	else
	{
		sprintf(tmp,"%s",pDef->pchName);
	}
	mv = itemvar_Find(tmp);

	if (mv)
		return MultiValGetFloat(mv, NULL);
	return 0.f;
}


// look up a power table by its ItemQuality and stem. e.g. ItemEPs_Sp_Weapon_Common
AUTO_EXPR_FUNC(ItemEval) ACMD_NAME(PowerTableFromQuality);
F32 itemeval_PowerTableFromQuality(const char* pcTableStem, int qual, U32 iIndex)
{
	char const *quality = StaticDefineIntRevLookup(ItemQualityEnum,qual);
	char tmp[MAX_PATH];
	if(!quality)
		return 0.f;
	sprintf(tmp,"%s_%s",pcTableStem,quality);
	return powertable_Lookup(tmp, iIndex);
}

// @todo -AB: testing :09/14/09
// ACMD_EXPR_STATIC_CHECK(itemeval_PowerTableFromQuality);
// int check_itemeval_PowerTableFromQuality(ExprContext *pContext, int qual, U32 iIndex)
// {
// 	char const *quality = StaticDefineIntRevLookup(ItemQualityEnum,qual);
// 	if(!quality)
// 		return 0;
// }



AUTO_EXPR_FUNC(ItemEval) ACMD_NAME(PowerTableLerp);
F32 itemeval_PowerTableLerp(ACMD_EXPR_SC_TYPE(PowerTable) const char* pcTableName, F32 fIndex)
{
	S32 iIndex = floor(fIndex);
	F32 fLowVal = powertable_Lookup(pcTableName, iIndex);
	if(fIndex != (F32)iIndex) {
		F32 fHighVal = powertable_Lookup(pcTableName, iIndex+1);
		fLowVal += (fHighVal-fLowVal) * (fIndex-(F32)iIndex);
	}
	return fLowVal;
}

AUTO_EXPR_FUNC(ItemEval) ACMD_NAME(GetItemPowerValue);
S32 itemeval_GetItemPowerValue(ExprContext *pContext, SA_PARAM_NN_VALID Item *pItem)
{
	Entity *pEnt = exprContextGetVarPointerUnsafePooled(pContext, "Player");
	S32 i;
	S32 iResult = 0;
	if (pItem->pAlgoProps)
	{
		for (i = eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs)-1; i >= 0; i--) {
			ItemPowerDef *pPowerDef = GET_REF(pItem->pAlgoProps->ppItemPowerDefRefs[i]->hItemPowerDef);
			if (pPowerDef) {
				iResult += itempower_GetEPValue(entGetPartitionIdx(pEnt), pEnt, pPowerDef, GET_REF(pItem->hItem), item_GetMinLevel(pItem), item_GetQuality(pItem));
			}
		}
	}
	return iResult;
}


AUTO_EXPR_FUNC(ItemEval) ACMD_NAME(GetIngredientValue);
S32 itemeval_GetIngredientValue(ExprContext *pContext, SA_PARAM_NN_VALID Item *pItem)
{
	Entity *pEnt = exprContextGetVarPointerUnsafePooled(pContext, "Player");
	S32 i;
	S32 iResult = 0;
	int iPartitionIdx;
	ItemDef *pItemDef = GET_REF(pItem->hItem);
	ItemDef *pRecipeDef = pItemDef ? GET_REF(pItemDef->hCraftRecipe) : NULL;
	
	if (!pRecipeDef || !pRecipeDef->pCraft) {
		return 0;
	}
	
	iPartitionIdx = (pEnt ? entGetPartitionIdx(pEnt) : PARTITION_UNINITIALIZED);

	for (i = eaSize(&pRecipeDef->pCraft->ppPart)-1; i >= 0; i--) {
		ItemDef *pIngredientDef = GET_REF(pRecipeDef->pCraft->ppPart[i]->hItem);
		if (pIngredientDef) {
			iResult += item_GetDefEPValue(iPartitionIdx, pEnt, pIngredientDef, pIngredientDef->iLevel, pIngredientDef->Quality)
					* pRecipeDef->pCraft->ppPart[i]->fCount * pRecipeDef->pCraft->ppPart[i]->fWeight;
		}
	}
	return iResult;
}

AUTO_EXPR_FUNC(ItemEval) ACMD_NAME(ItemIsEquipped);
int itemeval_ItemIsEquipped(SA_PARAM_NN_VALID Entity *pEnt, ACMD_EXPR_RES_DICT(ItemDef) const char *pcItemName)
{
	int i;
	
	if (!pEnt->pInventoryV2) {
		return false;
	}
	
	for (i = eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; i >= 0; i--) {
		if (invbag_flags(pEnt->pInventoryV2->ppInventoryBags[i]) & InvBagFlag_EquipBag) {
			if (inv_bag_GetItemByName(pEnt->pInventoryV2->ppInventoryBags[i], pcItemName) != NULL) {
				return true;
			}
		}
	}
	
	return false;
}

AUTO_EXPR_FUNC(UIGen, ItemEval, entityutil) ACMD_NAME(GetItemCount);
int itemeval_GetItemCount(SA_PARAM_OP_VALID Entity *pEnt, ACMD_EXPR_RES_DICT(ItemDef) const char *pcItemName)
{
	ItemDef *pDef = (ItemDef*)RefSystem_ReferentFromString(g_hItemDict,pcItemName);

	if (!pEnt || !pEnt->pInventoryV2 || !pDef) {
		return false;
	}

	if (pDef->eType == kItemType_Numeric) {
		return inv_GetNumericItemValue(pEnt, pcItemName);
	} else {
		return item_CountOwned(pEnt, pDef);
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen, ItemEval) ACMD_NAME(GetItemChargesLeft);
S32 itemeval_GetItemChargesLeft(SA_PARAM_OP_VALID Item *pItem)
{
	if (pItem)
	{
		int i;
		for (i = 0; i < eaSize(&pItem->ppPowers); i++)
		{
			Power *pPower = pItem->ppPowers[i];
			int iChargesLeft;

			if (!pPower)
				continue;

			iChargesLeft = power_GetChargesLeft(pPower);
			if (iChargesLeft != -1)
				return iChargesLeft;
		}
	}
	
	return -1;
}

AUTO_EXPR_FUNC(UIGen, ItemEval) ACMD_NAME(GetItemChargesMax);
S32 itemeval_GetItemChargesMax(SA_PARAM_OP_VALID Item *pItem)
{
	if (pItem)
	{
		int i;
		for (i = 0; i < eaSize(&pItem->ppPowers); i++)
		{
			Power *pPower = pItem->ppPowers[i];
			PowerDef *pPowerDef = SAFE_GET_REF(pPower, hDef);

			if (!pPowerDef)
				continue;

			if (pPowerDef->iCharges)
				return pPowerDef->iCharges;
		}
	}
	
	return -1;
}

// This gets the maximum between the recharge and the cooldown of the first valid power found on the item
AUTO_EXPR_FUNC(UIGen, ItemEval) ACMD_NAME(GetItemMaxOfRechargeAndCooldown);
F32 itemeval_GetItemMaxOfRechargeAndCooldown(SA_PARAM_OP_VALID Item *pItem)
{
	if (pItem)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pItem->ppPowers, Power, pPower)
		{
			F32 fRecharge = 0.f;
			F32 fCooldown = 0.f;
			PowerDef *pPowerDef = SAFE_GET_REF(pPower, hDef);

			if (!pPowerDef) 
				continue;
			fRecharge = powerdef_GetRechargeDefault(pPowerDef);
			if (fRecharge == POWERS_FOREVER) 
				fRecharge = 0.f;
			fCooldown = powerdef_GetCooldown(pPowerDef);

			return MAX(fRecharge, fCooldown);
		}
		FOR_EACH_END
	}

	return 0.f;
}

// gets the number of charges left on the power and if the item has a stack, 
// it will return the total charges for all items in the stack
AUTO_EXPR_FUNC(UIGen, ItemEval) ACMD_NAME(GetItemChargesAndCount);
S32 itemeval_GetItemChargesAndCount(SA_PARAM_OP_VALID Item *pItem)
{
	if (pItem)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pItem->ppPowers, Power, pPower)
		{
			int iChargesLeft;
			if (!pPower)
				continue;

			iChargesLeft = power_GetChargesLeft(pPower);
			if (iChargesLeft != -1)
			{
				PowerDef *pDef = GET_REF(pPower->hDef);
				if (pItem->count > 1)
				{
					return iChargesLeft + (pItem->count-1) * pDef->iCharges;
				}
				return iChargesLeft;
			}
		}
		FOR_EACH_END

		return pItem->count;
	}
	
	return 0;
}

AUTO_EXPR_FUNC(ItemEval) ACMD_NAME(ItemHasItemPower);
bool itemeval_ItemHasItemPower(ExprContext *pContext, SA_PARAM_NN_VALID Item *pItem, ACMD_EXPR_RES_DICT(ItemPowerDef) const char* pchItemPowerName)
{
	ItemPowerDef* pPowerDef = RefSystem_ReferentFromString(g_hItemPowerDict, pchItemPowerName);
	ItemDef* pDef = GET_REF(pItem->hItem);
	int i;

	if (!pDef || !pPowerDef)
		return false;

	//check both item and def for the power ref
	if (pItem->pAlgoProps)
	{
		for (i = 0; i < eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs); i++)
		{
			if (GET_REF(pItem->pAlgoProps->ppItemPowerDefRefs[i]->hItemPowerDef) == pPowerDef)
				return true;
		}
	}
	for (i = 0; i < eaSize(&pDef->ppItemPowerDefRefs); i++)
	{
		if (GET_REF(pDef->ppItemPowerDefRefs[i]->hItemPowerDef) == pPowerDef)
			return true;
	}
	return false;
}

AUTO_EXPR_FUNC(ItemEval) ACMD_NAME(ItemHasItemPowerCategory);
bool itemeval_ItemHasItemPowerCategory(ExprContext *pContext, SA_PARAM_NN_VALID Item *pItem, ACMD_EXPR_ENUM(ItemPowerCategory) const char* pchCatName)
{
	S32 iCat = StaticDefineIntGetInt(ItemPowerCategoryEnum, pchCatName);
	return iCat & getItempowerCategoriesForItem(pItem);
}

// Used for reward grant expression
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NAME(CharacterGetItemCount);
int itemeval_CharacterGetItemCount(SA_PARAM_OP_VALID Character *sourceCharacter, ACMD_EXPR_RES_DICT(ItemDef) const char *pcItemName)
{
	ItemDef *pDef = (ItemDef*)RefSystem_ReferentFromString(g_hItemDict,pcItemName);

	Entity *pEnt = sourceCharacter? sourceCharacter->pEntParent : NULL;

	if (!pEnt || !pEnt->pInventoryV2 || !pDef) {
		return false;
	}

	if (pDef->eType == kItemType_Numeric) {
		return inv_GetNumericItemValue(pEnt, pcItemName);
	} else {
		return item_CountOwned(pEnt, pDef);
	}
	return 0;
}

AUTO_EXPR_FUNC(util) ACMD_NAME(GetCurrentMapType);
const char* exprFuncGetCurrentMapType(void)
{
	ZoneMapType eType = zmapInfoGetMapType(NULL);
	const char *pchType = StaticDefineIntRevLookup(ZoneMapTypeEnum, eType);
	return NULL_TO_EMPTY(pchType);
}

#include "RegionRules.h"
#include "AutoGen/RegionRules_h_ast.h"

AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NAME(CheckVehicleRules);
bool exprFuncCheckVehicleRules(ExprContext *pContext, const char *pchVehicleRules)
{
	Entity *pPlayer = exprContextGetSelfPtr(pContext);
	Vec3 vPos; 
	RegionRules *pRules = NULL;
	VehicleRules eVehicleRules = StaticDefineIntGetInt(VehicleRulesEnum,pchVehicleRules);

	if(!pPlayer)
		return false;

	entGetPos(pPlayer,vPos);

	pRules = RegionRulesFromVec3(vPos);

	if(pRules && eVehicleRules == pRules->eVehicleRules)
		return true;

	return false;
}