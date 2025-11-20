/***************************************************************************
*     Copyright (c) 2005-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Character.h"
#include "CharacterClass.h"
#include "CombatConfig.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "Entity.h"
#include "Expression.h"
#include "Entity_h_ast.h"
#include "EntityBuild.h"
#include "EntitySavedData.h"
#include "AutoGen/EntityBuild_h_ast.h"
#include "AutoGen/EntitySavedData_h_ast.h"
#include "inventoryCommon.h"
#include "rewardCommon.h"
#include "powerVars.h"
#include "StringCache.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static EntityBuildDef s_entityBuildDef = {0};

static const char *s_pchSource = NULL;

static ExprContext* entity_BuildGetContext(void)
{
	static ExprContext* s_pContext = NULL;

	if(!s_pContext)
	{
		ExprFuncTable* stTable;

		s_pContext = exprContextCreate();

		stTable = exprContextCreateFunctionTable();
		// set up the right function tables
		exprContextAddFuncsToTableByTag(stTable,"player");
		exprContextAddFuncsToTableByTag(stTable,"CEFuncsCharacter");
		exprContextAddFuncsToTableByTag(stTable,"util");
		exprContextSetFuncTable(s_pContext, stTable);

		/* set up the context variables*/
		exprContextSetPointerVar(s_pContext,s_pchSource,NULL,parse_Character, true,false);
	}

	return s_pContext;
}

AUTO_RUN;
void EntityBuildContextInitStrings(void)
{
	s_pchSource = allocAddStaticString("Source");

}

AUTO_STARTUP(EntityBuild) ASTRT_DEPS(Powers);
void EntityBuildLoad(void)
{
	loadstart_printf("Loading EntityBuild def...");
	
	ParserLoadFiles(NULL, "defs/EntityBuild.def", "EntityBuild.bin",  PARSER_OPTIONALFLAG, parse_EntityBuildDef, &s_entityBuildDef);

	loadend_printf(" done.");


	if (!IsClient() && s_entityBuildDef.eaBuildSlots)
	{
		// create the context and generate all the expressions
		ExprContext *pContext = entity_BuildGetContext();

		FOR_EACH_IN_EARRAY_FORWARDS(s_entityBuildDef.eaBuildSlots, EntityBuildSlotDef, pDef)
			if (pDef->pExprCanChangeToBuild)
			{
				exprGenerate(pDef->pExprCanChangeToBuild, pContext);
			}
		FOR_EACH_END
	}
	
}


// Returns the entity build slot def by index
EntityBuildSlotDef* entity_BuildGetSlotDef(U32 index)
{
	return eaGet(&s_entityBuildDef.eaBuildSlots, index);
}

// Returns the entity's current build slot def
SA_RET_OP_VALID EntityBuildSlotDef* entity_BuildGetCurrentSlotDef(SA_PARAM_NN_VALID Entity *pent)
{
	if(pent->pSaved)
		return eaGet(&s_entityBuildDef.eaBuildSlots, pent->pSaved->uiIndexBuild);

	return NULL;
}

// Gets the Entity's current EntityBuild.  May return NULL if then Entity doesn't have a current EntityBuild.
EntityBuild *entity_BuildGetCurrent(Entity *pent)
{
	EntityBuild *pBuild = NULL;
	if(pent->pSaved)
	{
		pBuild =  entity_BuildGet(pent,pent->pSaved->uiIndexBuild);
	}
	return pBuild;
}

// Gets the Entity's EntityBuild at the given index.  May return NULL if then Entity doesn't have an EntityBuild at that index.
EntityBuild *entity_BuildGet(Entity *pent, S32 iIndex)
{
	EntityBuild *pBuild = NULL;
	if(pent->pSaved && iIndex >= 0)
	{
		S32 s = eaSize(&pent->pSaved->ppBuilds);
		if(iIndex < s)
		{
			pBuild = pent->pSaved->ppBuilds[iIndex];
		}
	}
	return pBuild;
}

// Returns the current index of the build.
// Returns -1 if no build current or on error
S32 entity_BuildGetCurrentIndex(Entity *pent)
{
	if (pent && pent->pSaved)
	{
		return pent->pSaved->uiIndexBuild;
	}
	return -1;
}

AUTO_TRANS_HELPER;
S32 entity_BuildMaxSlots(ATH_ARG NOCONST(Entity) *pent)
{
	int iLevel = entity_trh_GetSavedExpLevelLimited(pent);

	S32 iMaxBuilds = 0;

	// Add the number you get per level
	if(powertable_Find("BuildSlots"))
	{
		iMaxBuilds += entity_PowerTableLookupAtHelper(pent, "BuildSlots", iLevel-1);
	}

	// Add the number of numerics you have
	iMaxBuilds += inv_trh_GetNumericValue(ATR_EMPTY_ARGS, pent, "BuildSlots");

	// Return the max number of builds available, which has a minimum value of 1 unless that
	//  feature is turned off in the CombatConfig
	return MAX(g_CombatConfig.bBuildMinIsZero ? 0 : 1, iMaxBuilds);
}

// Returns true if the Entity is currently allowed to create a new EntityBuild
AUTO_TRANS_HELPER;
S32 entity_BuildCanCreate(ATH_ARG NOCONST(Entity) *pent)
{
	S32 bValid = true;

	S32 iMaxBuilds = entity_BuildMaxSlots(pent);

	if(eaSize(&pent->pSaved->ppBuilds) >= iMaxBuilds)
	{
		bValid = false;
	}
	return bValid;
}

AUTO_TRANS_HELPER;
S32 entity_trh_BuildTimeToWaitOutOfCombat(ATH_ARG NOCONST(Entity) *pent)
{
	S32 iTimeToWait = 0;
	if(NONNULL(pent->pSaved))
	{
		if(!pent->pSaved->uiTimestampBuildSet)
		{
			iTimeToWait = 0;
		}
		else
		{
#ifndef GAMECLIENT
			U32 uiTimestampNow = timeSecondsSince2000();
#else
			U32 uiTimestampNow = timeServerSecondsSince2000();
#endif

			iTimeToWait = g_CombatConfig.iBuildTimeWait - (uiTimestampNow - pent->pSaved->uiTimestampBuildSet);
		}
	}
	return MAX(0,iTimeToWait);
}

S32 entity_BuildTimeToWaitCombat(Entity *pent)
{
	S32 iTimeToWait = 0;
	if(pent->pSaved)
	{
		if(!pent->pSaved->uiTimestampBuildSet)
		{
			iTimeToWait = 0;
		}
		else
		{
#ifndef GAMECLIENT
			U32 uiTimestampNow = timeSecondsSince2000();
#else
			U32 uiTimestampNow = timeServerSecondsSince2000();
#endif

			if(pent->pChar && pent->pChar->uiTimeCombatExit)
			{
				iTimeToWait = g_CombatConfig.iBuildTimeWaitCombat - (uiTimestampNow - pent->pSaved->uiTimestampBuildSet);
			}
		}
	}
	return MAX(0,iTimeToWait);
}

S32 entity_BuildTimeToWait(Entity *pEnt, U32 iBuild)
{
	EntityBuild *pBuild = pEnt && pEnt->pSaved ? eaGet(&pEnt->pSaved->ppBuilds, iBuild) : NULL;
	S32 iDelay = pEnt ? MAX(entity_BuildTimeToWaitOutOfCombat(pEnt), entity_BuildTimeToWaitCombat(pEnt)) : 0;

	// If the build in question is valid, and it changes a costume, also check the costume change cooldown
	if (pBuild && (pEnt->pSaved->costumeData.iActiveCostume != pBuild->chCostume))
	{
#ifndef GAMECLIENT
		S32 iCurrentTime = timeSecondsSince2000();
#else
		S32 iCurrentTime = timeServerSecondsSince2000();
#endif
		MAX1(iDelay, (S32)(pEnt->pSaved->uiTimestampCostumeSet+g_CostumeConfig.iChangeCooldown) - iCurrentTime);

	}

	return iDelay;
}

static S32 entity_BuildCheckBuildSlotExpr(Entity *pent, EntityBuildSlotDef* pBuild)
{
	ExprContext *pContext = entity_BuildGetContext();
	if (pContext)
	{
		MultiVal answer = {0};
		
		exprContextSetPointerVar(pContext,s_pchSource,pent->pChar,parse_Character, true,false);
		exprContextSetSelfPtr(pContext, pent);
		exprContextSetPartition(pContext, entGetPartitionIdx(pent));
		
		exprEvaluate(pBuild->pExprCanChangeToBuild, pContext, &answer);

		exprContextSetSelfPtr(pContext, NULL);
		exprContextSetPointerVar(pContext,s_pchSource,NULL,parse_Character, true,false);

		if(answer.type == MULTI_INT)
		{
			return QuickGetInt(&answer);
		}
		return false;
	}

	return false;
}

// Returns true if the Entity is currently allowed to set its EntityBuild
S32 entity_BuildCanSet(Entity *pent, U32 iBuild)
{
	// check the wait time
	if (entity_BuildTimeToWait(pent, iBuild) <= 0)
	{
		EntityBuildSlotDef* pBuild = entity_BuildGetSlotDef(iBuild);
	
		if (pBuild && pBuild->pExprCanChangeToBuild)
		{	// 
			return entity_BuildCheckBuildSlotExpr(pent, pBuild);
		}
		return true;
	}
		
	return false;
}

// Returns true if the string is a legal name for an EntityBuild
S32 entity_BuildNameLegal(const char *pchName)
{
	size_t s;

	if(!pchName)
		return false;

	s = strlen(pchName);
	if(s >= MAX_NAME_LEN_ENTITYBUILD)
		return false;

	while(*pchName)
	{
		if(!isalnum(*pchName) && !*pchName == ' ')
			return false;

		pchName++;
	}

	return true;
}

// Returns true if the Item id is used in any of the Entity's builds
S32 entity_BuildsUseItemID(Entity *pent, U64 ulItemID)
{
	if(pent->pSaved)
	{
		int i,j;
		for(i=eaSize(&pent->pSaved->ppBuilds)-1; i>=0; i--)
		{
			EntityBuild *pBuild = pent->pSaved->ppBuilds[i];
			for(j=eaSize(&pBuild->ppItems)-1; j>=0; j--)
			{
				if(pBuild->ppItems[j]->ulItemID==ulItemID)
				{
					return true;
				}
			}
		}
	}
	return false;
}

// Returns the current index of the build.
// Returns -1 if no build current or on error
AUTO_EXPR_FUNC(player); 
S32 EntityBuildGetCurrentIndex(ExprContext *pContext)
{
	Entity *pent = exprContextGetSelfPtr(pContext);
	if (pent && pent->pSaved)
	{
		return pent->pSaved->uiIndexBuild;
	}
	return -1;
}

#include "AutoGen/EntityBuild_h_ast.c"
