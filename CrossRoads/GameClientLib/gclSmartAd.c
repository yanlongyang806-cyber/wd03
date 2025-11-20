/***************************************************************************
*     Copyright (c) 2005-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#include "gclSmartAd.h"
#include "Expression.h"
#include "Entity.h"
#include "GlobalTypeEnum.h"
#include "rand.h"
#include "qsortG.h"
#include "gclEntity.h"
#include "MicroTransactions.h"
#include "Player.h"

#include "AutoGen/gclSmartAd_h_ast.h"
#include "autogen/Expression_h_ast.h"
#include "autogen/entity_h_ast.h"


DefineContext *g_DefineSmartAddAutoTags;
DefineContext *g_pDefineSmartAddDisplayTags;

SmartAds g_SmartAds = {0};
int *g_eaCurrentAds = NULL;
ExprContext* gpContext = NULL;
bool bSmartAdsSetup = false;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

void SmartAdGenerate(SmartAdDef *pDef)
{
	if(pDef->pExcludeExpr)
	{
		exprGenerate(pDef->pExcludeExpr,gpContext);
	}

	if(pDef->pIncludeExpr)
	{
		exprGenerate(pDef->pIncludeExpr,gpContext);
	}
}

bool SmartAdEval(Entity *pPlayer, Expression *pExpr)
{
	if(verify(gpContext))
	{
		MultiVal mv = {0};
		int iResult = 0;
		exprContextSetPointerVar(gpContext,"Player",pPlayer,parse_Entity,true,true);
		exprContextSetPartition(gpContext,0);
		exprContextSetSelfPtr(gpContext,pPlayer);

		exprEvaluate(pExpr,gpContext,&mv);
		iResult = MultiValGetInt(&mv,NULL);

		if(iResult)
			return true;
	}

	return false;
}

void gclSmartAds_FillAds(int iNumberofAds, int **eaValidDisplayTags)
{
	F32 fChoice, fRandMax = 0;
	F32 fHighestPriority = FLT_MIN;
	int i, iAddNum;

	if(ea32Size(&g_eaCurrentAds) > 0)
		ea32Clear(&g_eaCurrentAds);

	for(iAddNum=0;iAddNum<iNumberofAds;iAddNum++)
	{
		for(i=0;i<eaSize(&g_SmartAds.ppDefs);i++)
		{
			g_SmartAds.ppDefs[i]->bIncludeInPick = false;
			
			if(!g_SmartAds.ppDefs[i]->bIsValidAd)
				continue;

			if(ea32Find(&g_eaCurrentAds,(int)g_SmartAds.ppDefs[i]->eTag) != -1)
				continue;

			if(eaValidDisplayTags && ea32Find(eaValidDisplayTags,(int)g_SmartAds.ppDefs[i]->eDisplayTag) != -1)
				continue;

			// Exclude ads that are already below our highest priority ad, since we know those won't make it in
			if(g_SmartAds.ppDefs[i]->fPriority < fHighestPriority)
				continue;

			fHighestPriority = g_SmartAds.ppDefs[i]->fPriority;

			fRandMax += g_SmartAds.ppDefs[i]->fWeight;

			g_SmartAds.ppDefs[i]->bIncludeInPick = true;
		}

		// Since the highest priority is likely to have changed during the first pass,
		// make a second pass now that we know what the true highest priority is, and
		// cull any ads that are below max priority
		for(i=0;i<eaSize(&g_SmartAds.ppDefs);i++)
		{
			if(!g_SmartAds.ppDefs[i]->bIncludeInPick)
				continue;

			if(g_SmartAds.ppDefs[i]->fPriority < fHighestPriority)
			{
				fRandMax -= g_SmartAds.ppDefs[i]->fWeight;
				g_SmartAds.ppDefs[i]->bIncludeInPick = false;
			}
		}

		if(fRandMax == 0)
			break;

		fChoice = randomPositiveF32() * fRandMax;

		fRandMax = 0;

		for(i=0;i<eaSize(&g_SmartAds.ppDefs);i++)
		{
			if(!g_SmartAds.ppDefs[i]->bIncludeInPick)
				continue;

			fRandMax += g_SmartAds.ppDefs[i]->fWeight;

			if(fRandMax >= fChoice)
				break;
		}

		ea32Push(&g_eaCurrentAds,(int)g_SmartAds.ppDefs[i]->eTag);
	}

	ea32QSort(g_eaCurrentAds,cmpU32);
}

SmartAdAutoTag gclSmartAds_GetAd(int iIndex)
{
	if(ea32Size(&g_eaCurrentAds) > iIndex)
	{
		return (SmartAdAutoTag)g_eaCurrentAds[iIndex];
	}

	return kSmartAutoTag_NONE;
}

bool SmartAd_PlayerCheck(Entity *pPlayer, SmartAdDef *pDef)
{
	if(pDef->pExcludeExpr && SmartAdEval(pPlayer,pDef->pExcludeExpr))
		return false;

	if(!pDef->pIncludeExpr || !SmartAdEval(pPlayer,pDef->pIncludeExpr))
		return false;

	return true;
}

void gclSmartAds_EvaulateAds(void)
{
	int i;
	Entity *pPlayer = entActivePlayerPtr();

	bSmartAdsSetup = false;
	for(i=0;i<eaSize(&g_SmartAds.ppDefs);i++)
	{
		SmartAdDef *pDef = g_SmartAds.ppDefs[i];

		if(SmartAd_PlayerCheck(pPlayer,pDef))
		{
			pDef->bIsValidAd = true;
			bSmartAdsSetup = true;
		}
	}
}

SmartAdAutoTag gclSmartAds_GetQuickAd()
{
	gclSmartAds_FillAds(1,NULL);
	return gclSmartAds_GetAd(0);
}

bool gclSmartAds_IsSetUp(void)
{
	return bSmartAdsSetup;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SmartAds_GenerateAds);
void gclSmartAdsExpr_GenerateAds(int iNumberOfAds)
{
	gclSmartAds_FillAds(iNumberOfAds,NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SmartAds_GenerateAdsWithDisplayTags);
void gclSmartAdsExpr_GenerateAdsWithDisplayTags(int iNumberOfAds, const char *pchDisplayTags)
{
	STRING_EARRAY eaElements = NULL;
	int *eaTags = NULL;

	DivideString(pchDisplayTags, ",", &eaElements,
		DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE|DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS|
		DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE|DIVIDESTRING_POSTPROCESS_ESTRINGS);

	EARRAY_CONST_FOREACH_BEGIN(eaElements, iCurElement, iNumElements);
	{
		char *pElement = eaElements[iCurElement];
		DisplayTags eTag = StaticDefineIntGetInt(DisplayTagsEnum,pElement);

		if(eTag != -1)
			ea32Push(&eaTags,(int)eTag);
	}
	EARRAY_FOREACH_END;

	eaDestroyEString(&eaElements);

	gclSmartAds_FillAds(iNumberOfAds,&eaTags);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SmartAds_PickAd);
int gclSmartAdsExpr_PickAd(int iAdIndex)
{
	return (int)gclSmartAds_GetAd(iAdIndex);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SmartAds_PickAdString);
const char *gclSmartAdsExpr_PickAdString(int iAdIndex)
{
	SmartAdAutoTag eTag = gclSmartAds_GetAd(iAdIndex);

	return StaticDefineIntRevLookup(SmartAdAutoTagEnum,eTag);
}

AUTO_EXPR_FUNC(SmartAds) ACMD_NAME(SmartAds_HasPurchased);
int gclSmartAdsExpr_CStoreHasPurchased(const char *pchCStoreItem)
{
	Entity *pPlayer = entActivePlayerPtr();
	if  (mictrotrans_HasPurchasedEx(GET_REF(pPlayer->pPlayer->pPlayerAccountData->hData),pchCStoreItem))
		return 1;

	return 0;
}

AUTO_RUN;
void SmartAdAutoRun(void)
{
	const char *tags[] = { "util", "entity", "entityutil", "gameutil", "SmartAds"};
	ExprFuncTable *pFuncTable;
	int i;

	gpContext = exprContextCreate();	

	// Create the function table
	pFuncTable = exprContextCreateFunctionTable();
	for(i=0; i<ARRAY_SIZE(tags); i++)
		exprContextAddFuncsToTableByTag(pFuncTable, tags[i]);

	exprContextAddStaticDefineIntAsVars(gpContext,SmartAdAutoTagEnum,"SmartAd_");

	exprContextSetFuncTable(gpContext, pFuncTable);

	exprContextSetPointerVar(gpContext, "player", NULL, parse_Entity, true, false);
	exprContextSetSelfPtr(gpContext,NULL);

	exprContextSetAllowRuntimeSelfPtr(gpContext);
	exprContextSetAllowRuntimePartition(gpContext);
}

SmartAdDisplayTags g_SmartAdDisplayTags = {0};

AUTO_STARTUP(SmartAds);
void SmartAdsStartup(void)
{
	int i;
	char *pchTemp = NULL;

	estrStackCreateSize(&pchTemp,20);

	g_DefineSmartAddAutoTags = DefineCreate();
	g_pDefineSmartAddDisplayTags = DefineCreate();

	ParserLoadFiles(NULL, "defs/smartads/DisplayCategories.def","smartadsDisplay.bin", PARSER_OPTIONALFLAG, parse_SmartAdDisplayTags, &g_SmartAdDisplayTags);

	for(i=0;i<eaSize(&g_SmartAdDisplayTags.ppTags);i++)
	{
		DefineAddInt(g_pDefineSmartAddDisplayTags, g_SmartAdDisplayTags.ppTags[i]->pchKey, i);
	}

	ParserLoadFiles("defs/smartads/", ".smartads", "smartads.bin", PARSER_OPTIONALFLAG, parse_SmartAds, &g_SmartAds);

	for(i=0;i<eaSize(&g_SmartAds.ppDefs);i++)
	{
		DefineAddInt(g_DefineSmartAddAutoTags,g_SmartAds.ppDefs[i]->pchKey,i);
		SmartAdGenerate(g_SmartAds.ppDefs[i]);
		g_SmartAds.ppDefs[i]->eTag = (SmartAdAutoTag)i;

		g_SmartAds.ppDefs[i]->fPriority = 0.0;
		if (g_SmartAds.ppDefs[i]->eDisplayTag != kDisplayTag_NONE && g_SmartAds.ppDefs[i]->eDisplayTag < eaSize(&g_SmartAdDisplayTags.ppTags))
		{
			g_SmartAds.ppDefs[i]->fPriority = g_SmartAdDisplayTags.ppTags[g_SmartAds.ppDefs[i]->eDisplayTag]->fPriority;
		}
	}

	estrDestroy(&pchTemp);
}

#include "AutoGen/gclSmartAd_h_ast.c"